
/*
** The implementation of the TH core. This file contains the parser, and
** the implementation of the interface in th.h.
*/

#include "config.h"
#include "th.h"
#include <string.h>
#include <assert.h>

/*
** Values used for element values in the tcl_platform array.
*/

#if !defined(TH_ENGINE)
#  define TH_ENGINE          "TH1"
#endif

#if !defined(TH_PLATFORM)
#  if defined(_WIN32) || defined(WIN32)
#    define TH_PLATFORM      "windows"
#  else
#    define TH_PLATFORM      "unix"
#  endif
#endif

/*
** Forward declarations for structures defined below.
*/

typedef struct Th_Command        Th_Command;
typedef struct Th_Frame          Th_Frame;
typedef struct Th_Variable       Th_Variable;
typedef struct Th_InterpAndList  Th_InterpAndList;

/*
** Interpreter structure.
*/
struct Th_Interp {
  Th_Vtab *pVtab;     /* Copy of the argument passed to Th_CreateInterp() */
  char *zResult;      /* Current interpreter result (Th_Malloc()ed) */
  int nResult;        /* number of bytes in zResult */
  Th_Hash *paCmd;     /* Table of registered commands */
  Th_Frame *pFrame;   /* Current execution frame */
  int isListMode;     /* True if thSplitList() should operate in "list" mode */
};

/*
** Each TH command registered using Th_CreateCommand() is represented
** by an instance of the following structure stored in the Th_Interp.paCmd
** hash-table.
*/
struct Th_Command {
  int (*xProc)(Th_Interp *, void *, int, const char **, int *);
  void *pContext;
  void (*xDel)(Th_Interp *, void *);
};

/*
** Each stack frame (variable scope) is represented by an instance
** of this structure. Variable values set using the Th_SetVar command
** are stored in the Th_Frame.paVar hash table member of the associated
** stack frame object.
**
** When an interpreter is created, a single Th_Frame structure is also
** allocated - the global variable scope. Th_Interp.pFrame (the current
** interpreter frame) is initialised to point to this Th_Frame. It is
** not deleted for the lifetime of the interpreter (because the global
** frame never goes out of scope).
**
** New stack frames are created by the Th_InFrame() function. Before
** invoking its callback function, Th_InFrame() allocates a new Th_Frame
** structure with pCaller set to the current frame (Th_Interp.pFrame),
** and sets the current frame to the new frame object. After the callback
** has been invoked, the allocated Th_Frame is deleted and the value
** of the current frame pointer restored.
**
** By default, the Th_SetVar(), Th_UnsetVar() and Th_GetVar() functions
** access variable values in the current frame. If they need to access
** the global frame, they do so by traversing the pCaller pointer list.
** Likewise, the Th_LinkVar() function uses the pCaller pointers to
** link to variables located in the global or other stack frames.
*/
struct Th_Frame {
  Th_Hash *paVar;               /* Variables defined in this scope */
  Th_Frame *pCaller;            /* Calling frame */
};

/*
** This structure represents a value assigned to a th1 variable.
**
** The Th_Frame.paVar hash table maps from variable name (a th1 string)
** to a pointer to an instance of the following structure. More than
** one hash table entry may map to a single structure if variable
** links have been created using Th_LinkVar(). The number of references
** is stored in Th_Variable.nRef.
**
** For scalar variables, Th_Variable.zData is never 0. Th_Variable.nData
** stores the number of bytes in the value pointed to by zData.
**
** For an array variable, Th_Variable.zData is 0 and pHash points to
** a hash table mapping between array key name (a th1 string) and
** a pointer to the Th_Variable structure holding the scalar
** value.
*/
struct Th_Variable {
  int nRef;                   /* Number of references to this structure */
  int nData;                  /* Number of bytes at Th_Variable.zData */
  char *zData;                /* Data for scalar variables */
  Th_Hash *pHash;             /* Data for array variables */
};

/*
** This structure is used to pass complete context information to the
** hash iteration callback functions that need a Th_Interp and a list
** to operate on, e.g. thListAppendHashKey().
*/
struct Th_InterpAndList {
  Th_Interp *interp;          /* Associated interpreter context */
  char **pzList;              /* IN/OUT: Ptr to ptr to list */
  int *pnList;                /* IN/OUT: Current length of *pzList */
};

/*
** Hash table API:
*/
#define TH_HASHSIZE 257
struct Th_Hash {
  Th_HashEntry *a[TH_HASHSIZE];
};

static int thEvalLocal(Th_Interp *, const char *, int);
static int thSplitList(Th_Interp*, const char*, int, char***, int **, int*);

static int thHexdigit(char c);
static int thEndOfLine(const char *, int);

static int  thPushFrame(Th_Interp*, Th_Frame*);
static void thPopFrame(Th_Interp*);

static int thFreeVariable(Th_HashEntry*, void*);
static int thFreeCommand(Th_HashEntry*, void*);

/*
** The following are used by both the expression and language parsers.
** Given that the start of the input string (z, n) is a language
** construct of the relevant type (a command enclosed in [], an escape
** sequence etc.), these functions determine the number of bytes
** of the input consumed by the construct. For example:
**
**   int nByte;
**   thNextCommand(interp, "[expr $a+1] $nIter", 18, &nByte);
**
** results in variable nByte being set to 11. Or,
**
**   thNextVarname(interp, "$a+1", 4, &nByte);
**
** results in nByte being set to 2.
*/
static int thNextCommand(Th_Interp*, const char *z, int n, int *pN);
static int thNextEscape (Th_Interp*, const char *z, int n, int *pN);
static int thNextVarname(Th_Interp*, const char *z, int n, int *pN);
static int thNextNumber (Th_Interp*, const char *z, int n, int *pN);
static int thNextInteger (Th_Interp*, const char *z, int n, int *pN);
static int thNextSpace  (Th_Interp*, const char *z, int n, int *pN);

/*
** Given that the input string (z, n) contains a language construct of
** the relevant type (a command enclosed in [], an escape sequence
** like "\xFF" or a variable reference like "${varname}", perform
** substitution on the string and store the resulting string in
** the interpreter result.
*/
static int thSubstCommand(Th_Interp*, const char *z, int n);
static int thSubstEscape (Th_Interp*, const char *z, int n);
static int thSubstVarname(Th_Interp*, const char *z, int n);

/*
** Given that there is a th1 word located at the start of the input
** string (z, n), determine the length in bytes of that word. If the
** isCmd argument is non-zero, then an unescaped ";" byte not
** located inside of a block or quoted string is considered to mark
** the end of the word.
*/
static int thNextWord(Th_Interp*, const char *z, int n, int *pN, int isCmd);

/*
** Perform substitution on the word contained in the input string (z, n).
** Store the resulting string in the interpreter result.
*/
static int thSubstWord(Th_Interp*, const char *z, int n);

/*
** The Buffer structure and the thBufferXXX() functions are used to make
** memory allocation easier when building up a result.
*/
struct Buffer {
  char *zBuf;
  int nBuf;
  int nBufAlloc;
};
typedef struct Buffer Buffer;
static int  thBufferWrite(Th_Interp *interp, Buffer *, const char *, int);
static void thBufferInit(Buffer *);
static void thBufferFree(Th_Interp *interp, Buffer *);

/*
** Append nAdd bytes of content copied from zAdd to the end of buffer
** pBuffer. If there is not enough space currently allocated, resize
** the allocation to make space.
*/
static int thBufferWrite(
  Th_Interp *interp,
  Buffer *pBuffer,
  const char *zAdd,
  int nAdd
){
  int nReq;

  if( nAdd<0 ){
    nAdd = th_strlen(zAdd);
  }
  nReq = pBuffer->nBuf+nAdd+1;

  if( nReq>pBuffer->nBufAlloc ){
    char *zNew;
    int nNew;

    nNew = nReq*2;
    zNew = (char *)Th_Malloc(interp, nNew);
    memcpy(zNew, pBuffer->zBuf, pBuffer->nBuf);
    Th_Free(interp, pBuffer->zBuf);
    pBuffer->nBufAlloc = nNew;
    pBuffer->zBuf = zNew;
  }

  memcpy(&pBuffer->zBuf[pBuffer->nBuf], zAdd, nAdd);
  pBuffer->nBuf += nAdd;
  pBuffer->zBuf[pBuffer->nBuf] = '\0';

  return TH_OK;
}
#define thBufferWrite(a,b,c,d) thBufferWrite(a,b,(const char *)c,d)

/*
** Initialize the Buffer structure pointed to by pBuffer.
*/
static void thBufferInit(Buffer *pBuffer){
  memset(pBuffer, 0, sizeof(Buffer));
}

/*
** Zero the buffer pointed to by pBuffer and free the associated memory
** allocation.
*/
static void thBufferFree(Th_Interp *interp, Buffer *pBuffer){
  Th_Free(interp, pBuffer->zBuf);
  thBufferInit(pBuffer);
}

/*
** Assuming parameter c contains a hexadecimal digit character,
** return the corresponding value of that digit. If c is not
** a hexadecimal digit character, -1 is returned.
*/
static int thHexdigit(char c){
  switch (c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
  }
  return -1;
}

/*
** Argument pEntry points to an entry in a stack frame hash table
** (Th_Frame.paVar). Decrement the reference count of the Th_Variable
** structure that the entry points to. Free the Th_Variable if its
** reference count reaches 0.
**
** Argument pContext is a pointer to the interpreter structure.
**
** Returns non-zero if the Th_Variable was actually freed.
*/
static int thFreeVariable(Th_HashEntry *pEntry, void *pContext){
  Th_Variable *pValue = (Th_Variable *)pEntry->pData;
  pValue->nRef--;
  assert( pValue->nRef>=0 );
  if( pValue->nRef==0 ){
    Th_Interp *interp = (Th_Interp *)pContext;
    Th_Free(interp, pValue->zData);
    if( pValue->pHash ){
      Th_HashIterate(interp, pValue->pHash, thFreeVariable, pContext);
      Th_HashDelete(interp, pValue->pHash);
    }
    Th_Free(interp, pValue);
    pEntry->pData = 0;
    return 1;
  }
  return 0;
}

/*
** Argument pEntry points to an entry in the command hash table
** (Th_Interp.paCmd). Delete the Th_Command structure that the
** entry points to.
**
** Argument pContext is a pointer to the interpreter structure.
**
** Always returns non-zero.
*/
static int thFreeCommand(Th_HashEntry *pEntry, void *pContext){
  Th_Command *pCommand = (Th_Command *)pEntry->pData;
  if( pCommand->xDel ){
    pCommand->xDel((Th_Interp *)pContext, pCommand->pContext);
  }
  Th_Free((Th_Interp *)pContext, pEntry->pData);
  pEntry->pData = 0;
  return 1;
}

/*
** Argument pEntry points to an entry in a hash table.  The key is
** the list element to be added.
**
** Argument pContext is a pointer to the Th_InterpAndList structure.
**
** Always returns non-zero.
*/
static int thListAppendHashKey(Th_HashEntry *pEntry, void *pContext){
  Th_InterpAndList *pInterpAndList = (Th_InterpAndList *)pContext;
  Th_ListAppend(pInterpAndList->interp, pInterpAndList->pzList,
                pInterpAndList->pnList, pEntry->zKey, pEntry->nKey);
  return 1;
}

/*
** Push a new frame onto the stack.
*/
static int thPushFrame(Th_Interp *interp, Th_Frame *pFrame){
  pFrame->paVar = Th_HashNew(interp);
  pFrame->pCaller = interp->pFrame;
  interp->pFrame = pFrame;
  return TH_OK;
}

/*
** Pop a frame off the top of the stack.
*/
static void thPopFrame(Th_Interp *interp){
  Th_Frame *pFrame = interp->pFrame;
  Th_HashIterate(interp, pFrame->paVar, thFreeVariable, (void *)interp);
  Th_HashDelete(interp, pFrame->paVar);
  interp->pFrame = pFrame->pCaller;
}

/*
** The first part of the string (zInput,nInput) contains an escape
** sequence. Set *pnEscape to the number of bytes in the escape sequence.
** If there is a parse error, return TH_ERROR and set the interpreter
** result to an error message. Otherwise return TH_OK.
*/
static int thNextEscape(
  Th_Interp *interp,
  const char *zInput,
  int nInput,
  int *pnEscape
){
  int i = 2;

  assert(nInput>0);
  assert(zInput[0]=='\\');

  if( nInput<=1 ){
    return TH_ERROR;
  }

  switch( zInput[1] ){
    case 'x': i = 4;
  }

  if( i>nInput ){
    return TH_ERROR;
  }
  *pnEscape = i;
  return TH_OK;
}

/*
** The first part of the string (zInput,nInput) contains a variable
** reference. Set *pnVarname to the number of bytes in the variable
** reference. If there is a parse error, return TH_ERROR and set the
** interpreter result to an error message. Otherwise return TH_OK.
*/
int thNextVarname(
  Th_Interp *interp,
  const char *zInput,
  int nInput,
  int *pnVarname
){
  int i;

  assert(nInput>0);
  assert(zInput[0]=='$');

  if( nInput>0 && zInput[1]=='{' ){
    for(i=2; i<nInput && zInput[i]!='}'; i++);
    if( i==nInput ){
      return TH_ERROR;
    }
    i++;
  }else{
    i = 1;
    if( nInput>2 && zInput[1]==':' && zInput[2]==':' ){
      i += 2;
    }
    for(; i<nInput; i++){
      if( zInput[i]=='(' ){
        for(i++; i<nInput; i++){
          if( zInput[i]==')' ) break;
          if( zInput[i]=='\\' ) i++;
          if( zInput[i]=='{' || zInput[i]=='[' || zInput[i]=='"' ){
            int nWord;
            int rc = thNextWord(interp, &zInput[i], nInput-i, &nWord, 0);
            if( rc!=TH_OK ){
              return rc;
            }
            i += nWord;
          }
        }
        if( i>=nInput ){
          Th_ErrorMessage(interp, "Unmatched brackets:", zInput, nInput);
          return TH_ERROR;
        }
        i++;
        break;
      }
      if( !th_isalnum(zInput[i]) && zInput[i]!='_' ) break;
    }
  }

  *pnVarname = i;
  return TH_OK;
}

/*
** The first part of the string (zInput,nInput) contains a command
** enclosed in a "[]" block. Set *pnCommand to the number of bytes in
** the variable reference. If there is a parse error, return TH_ERROR
** and set the interpreter result to an error message. Otherwise return
** TH_OK.
*/
int thNextCommand(
  Th_Interp *interp,
  const char *zInput,
  int nInput,
  int *pnCommand
){
  int nBrace = 0;
  int nSquare = 0;
  int i;

  assert(nInput>0);
  assert( zInput[0]=='[' || zInput[0]=='{' );

  for(i=0; i<nInput && (i==0 || nBrace>0 || nSquare>0); i++){
    switch( zInput[i] ){
      case '\\': i++; break;
      case '{': nBrace++; break;
      case '}': nBrace--; break;
      case '[': nSquare++; break;
      case ']': nSquare--; break;
    }
  }
  if( nBrace || nSquare ){
    return TH_ERROR;
  }

  *pnCommand = i;

  return TH_OK;
}

/*
** Set *pnSpace to the number of whitespace bytes at the start of
** input string (zInput, nInput). Always return TH_OK.
*/
int thNextSpace(
  Th_Interp *interp,
  const char *zInput,
  int nInput,
  int *pnSpace
){
  int i;
  for(i=0; i<nInput && th_isspace(zInput[i]); i++);
  *pnSpace = i;
  return TH_OK;
}

/*
** The first byte of the string (zInput,nInput) is not white-space.
** Set *pnWord to the number of bytes in the th1 word that starts
** with this byte. If a complete word cannot be parsed or some other
** error occurs, return TH_ERROR and set the interpreter result to
** an error message. Otherwise return TH_OK.
**
** If the isCmd argument is non-zero, then an unescaped ";" byte not
** located inside of a block or quoted string is considered to mark
** the end of the word.
*/
static int thNextWord(
  Th_Interp *interp,
  const char *zInput,
  int nInput,
  int *pnWord,
  int isCmd
){
  int iEnd = 0;

  assert( !th_isspace(zInput[0]) );

  if( zInput[0]=='"' ){
    /* The word is terminated by the next unescaped '"' character. */
    iEnd++;
    while( iEnd<nInput && zInput[iEnd]!='"' ){
      if( zInput[iEnd]=='\\' ){
        iEnd++;
      }
      iEnd++;
    }
    iEnd++;
  }else{
    int nBrace = 0;
    int nSq = 0;
    while( iEnd<nInput && (nBrace>0 || nSq>0 ||
      (!th_isspace(zInput[iEnd]) && (!isCmd || zInput[iEnd]!=';'))
    )){
      switch( zInput[iEnd] ){
        case '\\': iEnd++; break;
        case '{': if( nSq==0 ) nBrace++; break;
        case '}': if( nSq==0 ) nBrace--; break;
        case '[': if( nBrace==0 ) nSq++; break;
        case ']': if( nBrace==0 ) nSq--; break;
      }
      iEnd++;
    }
    if( nBrace>0 || nSq>0 ){
      /* Parse error */
      Th_SetResult(interp, "parse error", -1);
      return TH_ERROR;
    }
  }

  if( iEnd>nInput ){
    /* Parse error */
    Th_SetResult(interp, "parse error", -1);
    return TH_ERROR;
  }
  *pnWord = iEnd;
  return TH_OK;
}

/*
** The input string (zWord, nWord) contains a th1 script enclosed in
** a [] block. Perform substitution on the input string and store the
** resulting string in the interpreter result.
*/
static int thSubstCommand(
  Th_Interp *interp,
  const char *zWord,
  int nWord
){
  assert(nWord>=2);
  assert(zWord[0]=='[' && zWord[nWord-1]==']');
  return thEvalLocal(interp, &zWord[1], nWord-2);
}

/*
** The input string (zWord, nWord) contains a th1 variable reference
** (a '$' byte followed by a variable name). Perform substitution on
** the input string and store the resulting string in the interpreter
** result.
*/
static int thSubstVarname(
  Th_Interp *interp,
  const char *zWord,
  int nWord
){
  assert(nWord>=1);
  assert(zWord[0]=='$');
  assert(nWord==1 || zWord[1]!='{' || zWord[nWord-1]=='}');
  if( nWord>1 && zWord[1]=='{' ){
    zWord++;
    nWord -= 2;
  }else if( zWord[nWord-1]==')' ){
    int i;
    for(i=1; i<nWord && zWord[i]!='('; i++);
    if( i<nWord ){
      Buffer varname;
      int nInner;
      const char *zInner;

      int rc = thSubstWord(interp, &zWord[i+1], nWord-i-2);
      if( rc!=TH_OK ) return rc;

      zInner = Th_GetResult(interp, &nInner);
      thBufferInit(&varname);
      thBufferWrite(interp, &varname, &zWord[1], i);
      thBufferWrite(interp, &varname, zInner, nInner);
      thBufferWrite(interp, &varname, ")", 1);
      rc = Th_GetVar(interp, varname.zBuf, varname.nBuf);
      thBufferFree(interp, &varname);
      return rc;
    }
  }
  return Th_GetVar(interp, &zWord[1], nWord-1);
}

/*
** The input string (zWord, nWord) contains a th1 escape sequence.
** Perform substitution on the input string and store the resulting
** string in the interpreter result.
*/
static int thSubstEscape(
  Th_Interp *interp,
  const char *zWord,
  int nWord
){
  char c;

  assert(nWord>=2);
  assert(zWord[0]=='\\');

  switch( zWord[1] ){
    case 'x': {
      assert(nWord==4);
      c = ((thHexdigit(zWord[2])<<4) + thHexdigit(zWord[3]));
      break;
    }
    case 'n': {
      c = '\n';
      break;
    }
    default: {
      assert(nWord==2);
      c = zWord[1];
      break;
    }
  }

  Th_SetResult(interp, &c, 1);
  return TH_OK;
}

/*
** The input string (zWord, nWord) contains a th1 word. Perform
** substitution on the input string and store the resulting
** string in the interpreter result.
*/
static int thSubstWord(
  Th_Interp *interp,
  const char *zWord,
  int nWord
){
  int rc = TH_OK;
  Buffer output;
  int i;

  thBufferInit(&output);

  if( nWord>1 && (zWord[0]=='{' && zWord[nWord-1]=='}') ){
    rc = thBufferWrite(interp, &output, &zWord[1], nWord-2);
  }else{

    /* If the word is surrounded by double-quotes strip these away. */
    if( nWord>1 && (zWord[0]=='"' && zWord[nWord-1]=='"') ){
      zWord++;
      nWord -= 2;
    }

    for(i=0; rc==TH_OK && i<nWord; i++){
      int nGet;

      int (*xGet)(Th_Interp *, const char*, int, int *) = 0;
      int (*xSubst)(Th_Interp *, const char*, int) = 0;

      switch( zWord[i] ){
        case '\\':
          xGet = thNextEscape; xSubst = thSubstEscape;
          break;
        case '[':
          if( !interp->isListMode ){
            xGet = thNextCommand; xSubst = thSubstCommand;
            break;
          }
        case '$':
          if( !interp->isListMode ){
            xGet = thNextVarname; xSubst = thSubstVarname;
            break;
          }
        default: {
          thBufferWrite(interp, &output, &zWord[i], 1);
          continue; /* Go to the next iteration of the for(...) loop */
        }
      }

      rc = xGet(interp, &zWord[i], nWord-i, &nGet);
      if( rc==TH_OK ){
        rc = xSubst(interp, &zWord[i], nGet);
      }
      if( rc==TH_OK ){
        const char *zRes;
        int nRes;
        zRes = Th_GetResult(interp, &nRes);
        rc = thBufferWrite(interp, &output, zRes, nRes);
        i += (nGet-1);
      }
    }
  }

  if( rc==TH_OK ){
    Th_SetResult(interp, output.zBuf, output.nBuf);
  }
  thBufferFree(interp, &output);
  return rc;
}

/*
** Return true if one of the following is true of the buffer pointed
** to by zInput, length nInput:
**
**   + It is empty, or
**   + It contains nothing but white-space, or
**   + It contains no non-white-space characters before the first
**     newline character.
**
** Otherwise return false.
*/
static int thEndOfLine(const char *zInput, int nInput){
  int i;
  for(i=0; i<nInput && zInput[i]!='\n' && th_isspace(zInput[i]); i++);
  return ((i==nInput || zInput[i]=='\n')?1:0);
}

/*
** This function splits the supplied th1 list (contained in buffer zList,
** size nList) into elements and performs word-substitution on each
** element. If the Th_Interp.isListMode variable is true, then only
** escape sequences are substituted (used by the Th_SplitList() function).
** If Th_Interp.isListMode is false, then variable and command substitution
** is also performed (used by Th_Eval()).
**
** If zList/nList does not contain a valid list, TH_ERROR is returned
** and an error message stored in interp.
**
** If TH_OK is returned and pazElem is not NULL, the caller should free the
** pointer written to (*pazElem) using Th_Free(). This releases memory
** allocated for both the (*pazElem) and (*panElem) arrays. Example:
**
**     char **argv;
**     int *argl;
**     int argc;
**
**     // After this call, argv and argl point to valid arrays. The
**     // number of elements in each is argc.
**     //
**     Th_SplitList(interp, zList, nList, &argv, &argl, &argc);
**
**     // Free all memory allocated by Th_SplitList(). The arrays pointed
**     // to by argv and argl are invalidated by this call.
**     //
**     Th_Free(interp, argv);
**
*/
static int thSplitList(
  Th_Interp *interp,      /* Interpreter context */
  const char *zList,      /* Pointer to buffer containing input list */
  int nList,              /* Size of buffer pointed to by zList */
  char ***pazElem,        /* OUT: Array of list elements */
  int **panElem,          /* OUT: Lengths of each list element */
  int *pnCount            /* OUT: Number of list elements */
){
  int rc = TH_OK;

  Buffer strbuf;
  Buffer lenbuf;
  int nCount = 0;

  const char *zInput = zList;
  int nInput = nList;

  thBufferInit(&strbuf);
  thBufferInit(&lenbuf);

  while( nInput>0 ){
    const char *zWord;
    int nWord;

    thNextSpace(interp, zInput, nInput, &nWord);
    zInput += nWord;
    nInput = nList-(zInput-zList);

    if( TH_OK!=(rc = thNextWord(interp, zInput, nInput, &nWord, 0))
     || TH_OK!=(rc = thSubstWord(interp, zInput, nWord))
    ){
      goto finish;
    }
    zInput = &zInput[nWord];
    nInput = nList-(zInput-zList);
    if( nWord>0 ){
      zWord = Th_GetResult(interp, &nWord);
      thBufferWrite(interp, &strbuf, zWord, nWord);
      thBufferWrite(interp, &strbuf, "\0", 1);
      thBufferWrite(interp, &lenbuf, &nWord, sizeof(int));
      nCount++;
    }
  }
  assert((lenbuf.nBuf/sizeof(int))==nCount);

  assert((pazElem && panElem) || (!pazElem && !panElem));
  if( pazElem && rc==TH_OK ){
    int i;
    char *zElem;
    int *anElem;
    char **azElem = Th_Malloc(interp,
      sizeof(char*) * nCount +       /* azElem */
      sizeof(int) * nCount +         /* anElem */
      strbuf.nBuf                    /* space for list element strings */
    );
    anElem = (int *)&azElem[nCount];
    zElem = (char *)&anElem[nCount];
    memcpy(anElem, lenbuf.zBuf, lenbuf.nBuf);
    memcpy(zElem, strbuf.zBuf, strbuf.nBuf);
    for(i=0; i<nCount;i++){
      azElem[i] = zElem;
      zElem += (anElem[i] + 1);
    }
    *pazElem = azElem;
    *panElem = anElem;
  }
  if( pnCount ){
    *pnCount = nCount;
  }

 finish:
  thBufferFree(interp, &strbuf);
  thBufferFree(interp, &lenbuf);
  return rc;
}

/*
** Evaluate the th1 script contained in the string (zProgram, nProgram)
** in the current stack frame.
*/
static int thEvalLocal(Th_Interp *interp, const char *zProgram, int nProgram){
  int rc = TH_OK;
  const char *zInput = zProgram;
  int nInput = nProgram;

  while( rc==TH_OK && nInput ){
    Th_HashEntry *pEntry;
    int nSpace;
    const char *zFirst;

    char **argv;
    int *argl;
    int argc;

    assert(nInput>=0);

    /* Skip a semi-colon */
    if( *zInput==';' ){
      zInput++;
      nInput--;
    }

    /* Skip past leading white-space. */
    thNextSpace(interp, zInput, nInput, &nSpace);
    zInput += nSpace;
    nInput -= nSpace;
    zFirst = zInput;

    /* Check for a comment. If found, skip to the end of the line. */
    if( zInput[0]=='#' ){
      while( !thEndOfLine(zInput, nInput) ){
        zInput++;
        nInput--;
      }
      continue;
    }

    /* Gobble up input a word at a time until the end of the command
    ** (a semi-colon or end of line).
    */
    while( rc==TH_OK && *zInput!=';' && !thEndOfLine(zInput, nInput) ){
      int nWord=0;
      thNextSpace(interp, zInput, nInput, &nSpace);
      rc = thNextWord(interp, &zInput[nSpace], nInput-nSpace, &nWord, 1);
      zInput += (nSpace+nWord);
      nInput -= (nSpace+nWord);
    }
    if( rc!=TH_OK ) continue;

    /* Split the command into an array of words. This call also does
    ** substitution of each individual word.
    */
    rc = thSplitList(interp, zFirst, zInput-zFirst, &argv, &argl, &argc);
    if( rc!=TH_OK ) continue;

    if( argc>0 ){

      /* Look up the command name in the command hash-table. */
      pEntry = Th_HashFind(interp, interp->paCmd, argv[0], argl[0], 0);
      if( !pEntry ){
        Th_ErrorMessage(interp, "no such command: ", argv[0], argl[0]);
        rc = TH_ERROR;
      }

      /* Call the command procedure. */
      if( rc==TH_OK ){
        Th_Command *p = (Th_Command *)(pEntry->pData);
        const char **azArg = (const char **)argv;
        rc = p->xProc(interp, p->pContext, argc, azArg, argl);
      }

      /* If an error occurred, add this command to the stack trace report. */
      if( rc==TH_ERROR ){
        char *zRes;
        int nRes;
        char *zStack = 0;
        int nStack = 0;

        zRes = Th_TakeResult(interp, &nRes);
        if( TH_OK==Th_GetVar(interp, (char *)"::th_stack_trace", -1) ){
          zStack = Th_TakeResult(interp, &nStack);
        }
        Th_ListAppend(interp, &zStack, &nStack, zFirst, zInput-zFirst);
        Th_SetVar(interp, (char *)"::th_stack_trace", -1, zStack, nStack);
        Th_SetResult(interp, zRes, nRes);
        Th_Free(interp, zRes);
        Th_Free(interp, zStack);
      }
    }

    Th_Free(interp, argv);
  }

  return rc;
}

/*
** Interpret an integer frame identifier passed to either Th_Eval() or
** Th_LinkVar(). If successful, return a pointer to the identified
** Th_Frame structure. If unsuccessful (no such frame), return 0 and
** leave an error message in the interpreter result.
**
** Argument iFrame is interpreted as follows:
**
**   * If iFrame is 0, this means the current frame.
**
**   * If iFrame is negative, then the nth frame up the stack, where
**     n is the absolute value of iFrame. A value of -1 means the
**     calling procedure.
**
**   * If iFrame is +ve, then the nth frame from the bottom of the
**     stack. An iFrame value of 1 means the toplevel (global) frame.
*/
static Th_Frame *getFrame(Th_Interp *interp, int iFrame){
  Th_Frame *p = interp->pFrame;
  int i;
  if( iFrame>0 ){
    for(i=0; p; i++){
      p = p->pCaller;
    }
    iFrame = (i*-1) + iFrame;
    p = interp->pFrame;
  }
  for(i=0; p && i<(iFrame*-1); i++){
    p = p->pCaller;
  }

  if( !p ){
    char *zFrame;
    int nFrame;
    Th_SetResultInt(interp, iFrame);
    zFrame = Th_TakeResult(interp, &nFrame);
    Th_ErrorMessage(interp, "no such frame:", zFrame, nFrame);
    Th_Free(interp, zFrame);
  }
  return p;
}


/*
** Evaluate th1 script (zProgram, nProgram) in the frame identified by
** argument iFrame. Leave either an error message or a result in the
** interpreter result and return a th1 error code (TH_OK, TH_ERROR,
** TH_RETURN, TH_CONTINUE or TH_BREAK).
*/
int Th_Eval(Th_Interp *interp, int iFrame, const char *zProgram, int nProgram){
  int rc = TH_OK;
  Th_Frame *pSavedFrame = interp->pFrame;

  /* Set Th_Interp.pFrame to the frame that this script is to be
  ** evaluated in. The current frame is saved in pSavedFrame and will
  ** be restored before this function returns.
  */
  interp->pFrame = getFrame(interp, iFrame);

  if( !interp->pFrame ){
    rc = TH_ERROR;
  }else{
    int nInput = nProgram;

    if( nInput<0 ){
      nInput = th_strlen(zProgram);
    }
    rc = thEvalLocal(interp, zProgram, nInput);
  }

  interp->pFrame = pSavedFrame;
  return rc;
}

/*
** Input string (zVarname, nVarname) contains a th1 variable name. It
** may be a simple scalar variable name or it may be a reference
** to an array member. The variable name may or may not begin with
** "::", indicating that the name refers to a global variable, not
** a local scope one.
**
** This function inspects and categorizes the supplied variable name.
**
** If the name is a global reference, *pisGlobal is set to true. Otherwise
** false. Output string (*pzOuter, *pnOuter) is set to the variable name
** if it is a scalar reference, or the name of the array if it is an
** array variable. If the variable is a scalar, *pzInner is set to 0.
** If it is an array variable, (*pzInner, *pnInner) is set to the
** array key name.
*/
static int thAnalyseVarname(
  const char *zVarname,
  int nVarname,
  const char **pzOuter,      /* OUT: Pointer to scalar/array name */
  int *pnOuter,              /* OUT: Number of bytes at *pzOuter */
  const char **pzInner,      /* OUT: Pointer to array key (or null) */
  int *pnInner,              /* OUT: Number of bytes at *pzInner */
  int *pisGlobal             /* OUT: Set to true if this is a global ref */
){
  const char *zOuter = zVarname;
  int nOuter;
  const char *zInner = 0;
  int nInner = 0;
  int isGlobal = 0;
  int i;

  if( nVarname<0 ){
    nVarname = th_strlen(zVarname);
  }
  nOuter = nVarname;

  /* If the variable name starts with "::", then do the lookup is in the
  ** uppermost (global) frame.
  */
  if( nVarname>2 && zVarname[0]==':' && zVarname[1]==':' ){
    zOuter += 2;
    nOuter -= 2;
    isGlobal = 1;
  }

  /* Check if this is an array reference. */
  if( zOuter[nOuter-1]==')' ){
    for(i=0; i<nOuter; i++){
      if( zOuter[i]=='(' ){
        zInner = &zOuter[i+1];
        nInner = nOuter-i-2;
        nOuter = i;
        break;
      }
    }
  }

  *pzOuter = zOuter;
  *pnOuter = nOuter;
  *pzInner = zInner;
  *pnInner = nInner;
  *pisGlobal = isGlobal;
  return TH_OK;
}

/*
** The Find structure is used to return extra information to callers of the
** thFindValue function.  The fields within it are populated by thFindValue
** as soon as the necessary information is available.  Callers should check
** each field of interest upon return.
*/

struct Find {
  Th_HashEntry *pValueEntry; /* Pointer to the scalar or array hash entry */
  Th_HashEntry *pElemEntry;  /* Pointer to array element hash entry, if any */
  const char *zElem;         /* Name of array element, if applicable */
  int nElem;                 /* Length of array element name, if applicable */
};
typedef struct Find Find;

/*
** Input string (zVar, nVar) contains a variable name. This function locates
** the Th_Variable structure associated with the named variable. The
** variable name may be a global or local scalar or array variable
**
** If the create argument is non-zero and the named variable does not exist
** it is created. Otherwise, an error is left in the interpreter result
** and NULL returned.
**
** If the arrayok argument is false and the named variable is an array,
** an error is left in the interpreter result and NULL returned. If
** arrayok is true an array name is Ok.
*/

static Th_Variable *thFindValue(
  Th_Interp *interp,
  const char *zVar,       /* Pointer to variable name */
  int nVar,               /* Number of bytes at nVar */
  int create,             /* If true, create the variable if not found */
  int arrayok,            /* If true, an array is Ok. Otherwise array==error */
  int noerror,            /* If false, set interpreter result to error */
  Find *pFind             /* If non-zero, place output here */
){
  const char *zOuter;
  int nOuter;
  const char *zInner;
  int nInner;
  int isGlobal;

  Th_HashEntry *pEntry;
  Th_Frame *pFrame = interp->pFrame;
  Th_Variable *pValue;

  thAnalyseVarname(zVar, nVar, &zOuter, &nOuter, &zInner, &nInner, &isGlobal);
  if( pFind ){
    memset(pFind, 0, sizeof(Find));
    pFind->zElem = zInner;
    pFind->nElem = nInner;
  }
  if( isGlobal ){
    while( pFrame->pCaller ) pFrame = pFrame->pCaller;
  }

  pEntry = Th_HashFind(interp, pFrame->paVar, zOuter, nOuter, create);
  assert(pEntry || create<=0);
  if( pFind ){
    pFind->pValueEntry = pEntry;
  }
  if( !pEntry ){
    goto no_such_var;
  }

  pValue = (Th_Variable *)pEntry->pData;
  if( !pValue ){
    assert(create);
    pValue = Th_Malloc(interp, sizeof(Th_Variable));
    pValue->nRef = 1;
    pEntry->pData = (void *)pValue;
  }

  if( zInner ){
    if( pValue->zData ){
      if( !noerror ){
        Th_ErrorMessage(interp, "variable is a scalar:", zOuter, nOuter);
      }
      return 0;
    }
    if( !pValue->pHash ){
      if( !create ){
        goto no_such_var;
      }
      pValue->pHash = Th_HashNew(interp);
    }
    pEntry = Th_HashFind(interp, pValue->pHash, zInner, nInner, create);
    assert(pEntry || create<=0);
    if( pFind ){
      pFind->pElemEntry = pEntry;
    }
    if( !pEntry ){
      goto no_such_var;
    }
    pValue = (Th_Variable *)pEntry->pData;
    if( !pValue ){
      assert(create);
      pValue = Th_Malloc(interp, sizeof(Th_Variable));
      pValue->nRef = 1;
      pEntry->pData = (void *)pValue;
    }
  }else{
    if( pValue->pHash && !arrayok ){
      if( !noerror ){
        Th_ErrorMessage(interp, "variable is an array:", zOuter, nOuter);
      }
      return 0;
    }
  }

  return pValue;

no_such_var:
  if( !noerror ){
    Th_ErrorMessage(interp, "no such variable:", zVar, nVar);
  }
  return 0;
}

/*
** String (zVar, nVar) must contain the name of a scalar variable or
** array member. Look up the variable, store its current value in
** the interpreter result and return TH_OK.
**
** If the named variable does not exist, return TH_ERROR and leave
** an error message in the interpreter result.
*/
int Th_GetVar(Th_Interp *interp, const char *zVar, int nVar){
  Th_Variable *pValue;

  pValue = thFindValue(interp, zVar, nVar, 0, 0, 0, 0);
  if( !pValue ){
    return TH_ERROR;
  }
  if( !pValue->zData ){
    Th_ErrorMessage(interp, "no such variable:", zVar, nVar);
    return TH_ERROR;
  }

  return Th_SetResult(interp, pValue->zData, pValue->nData);
}

/*
** Return true if variable (zVar, nVar) exists.
*/
int Th_ExistsVar(Th_Interp *interp, const char *zVar, int nVar){
  Th_Variable *pValue = thFindValue(interp, zVar, nVar, 0, 1, 1, 0);
  return pValue && (pValue->zData || pValue->pHash);
}

/*
** Return true if array variable (zVar, nVar) exists.
*/
int Th_ExistsArrayVar(Th_Interp *interp, const char *zVar, int nVar){
  Th_Variable *pValue = thFindValue(interp, zVar, nVar, 0, 1, 1, 0);
  return pValue && !pValue->zData && pValue->pHash;
}

/*
** String (zVar, nVar) must contain the name of a scalar variable or
** array member. If the variable does not exist it is created. The
** variable is set to the value supplied in string (zValue, nValue).
**
** If (zVar, nVar) refers to an existing array, TH_ERROR is returned
** and an error message left in the interpreter result.
*/
int Th_SetVar(
  Th_Interp *interp,
  const char *zVar,
  int nVar,
  const char *zValue,
  int nValue
){
  Th_Variable *pValue;

  pValue = thFindValue(interp, zVar, nVar, 1, 0, 0, 0);
  if( !pValue ){
    return TH_ERROR;
  }

  if( nValue<0 ){
    nValue = th_strlen(zValue);
  }
  if( pValue->zData ){
    Th_Free(interp, pValue->zData);
    pValue->zData = 0;
  }

  assert(zValue || nValue==0);
  pValue->zData = Th_Malloc(interp, nValue+1);
  pValue->zData[nValue] = '\0';
  memcpy(pValue->zData, zValue, nValue);
  pValue->nData = nValue;

  return TH_OK;
}

/*
** Create a variable link so that accessing variable (zLocal, nLocal) is
** the same as accessing variable (zLink, nLink) in stack frame iFrame.
*/
int Th_LinkVar(
  Th_Interp *interp,                 /* Interpreter */
  const char *zLocal, int nLocal,    /* Local varname */
  int iFrame,                        /* Stack frame of linked var */
  const char *zLink, int nLink       /* Linked varname */
){
  Th_Frame *pSavedFrame = interp->pFrame;
  Th_Frame *pFrame;
  Th_HashEntry *pEntry;
  Th_Variable *pValue;

  pFrame = getFrame(interp, iFrame);
  if( !pFrame ){
    return TH_ERROR;
  }
  pSavedFrame = interp->pFrame;
  interp->pFrame = pFrame;
  pValue = thFindValue(interp, zLink, nLink, 1, 1, 0, 0);
  interp->pFrame = pSavedFrame;

  pEntry = Th_HashFind(interp, interp->pFrame->paVar, zLocal, nLocal, 1);
  if( pEntry->pData ){
    Th_ErrorMessage(interp, "variable exists:", zLocal, nLocal);
    return TH_ERROR;
  }
  pEntry->pData = (void *)pValue;
  pValue->nRef++;

  return TH_OK;
}

/*
** Input string (zVar, nVar) must contain the name of a scalar variable,
** an array, or an array member. If the identified variable exists, it
** is deleted and TH_OK returned. Otherwise, an error message is left
** in the interpreter result and TH_ERROR is returned.
*/
int Th_UnsetVar(Th_Interp *interp, const char *zVar, int nVar){
  Find find;
  Th_Variable *pValue;
  Th_HashEntry *pEntry;
  int rc = TH_ERROR;

  pValue = thFindValue(interp, zVar, nVar, 0, 1, 0, &find);
  if( !pValue ){
    return rc;
  }

  if( pValue->zData || pValue->pHash ){
    rc = TH_OK;
  }else {
    Th_ErrorMessage(interp, "no such variable:", zVar, nVar);
  }

  /*
  ** The variable may be shared by more than one frame; therefore, make sure
  ** it is actually freed prior to freeing the parent structure.  The values
  ** for the variable must be freed now so the variable appears undefined in
  ** all frames.  The hash entry in the current frame must also be deleted
  ** now; otherwise, if the current stack frame is later popped, it will try
  ** to delete a variable which has already been freed.
  */
  if( find.zElem ){
    pEntry = find.pElemEntry;
  }else{
    pEntry = find.pValueEntry;
  }
  assert( pEntry );
  assert( pValue );
  if( thFreeVariable(pEntry, (void *)interp) ){
    if( find.zElem ){
      Th_Variable *pValue2 = find.pValueEntry->pData;
      Th_HashFind(interp, pValue2->pHash, find.zElem, find.nElem, -1);
    }else if( pEntry->pData ){
      Th_Free(interp, pEntry->pData);
      pEntry->pData = 0;
    }
  }else{
    if( pValue->zData ){
      Th_Free(interp, pValue->zData);
      pValue->zData = 0;
    }
    if( pValue->pHash ){
      Th_HashIterate(interp, pValue->pHash, thFreeVariable, (void *)interp);
      Th_HashDelete(interp, pValue->pHash);
      pValue->pHash = 0;
    }
    if( find.zElem ){
      Th_Variable *pValue2 = find.pValueEntry->pData;
      Th_HashFind(interp, pValue2->pHash, find.zElem, find.nElem, -1);
    }
  }
  if( !find.zElem ){
    Th_HashFind(interp, interp->pFrame->paVar, zVar, nVar, -1);
  }
  return rc;
}

/*
** Return an allocated buffer containing a copy of string (z, n). The
** caller is responsible for eventually calling Th_Free() to free
** the returned buffer.
*/
char *th_strdup(Th_Interp *interp, const char *z, int n){
  char *zRes;
  if( n<0 ){
    n = th_strlen(z);
  }
  zRes = Th_Malloc(interp, n+1);
  memcpy(zRes, z, n);
  zRes[n] = '\0';
  return zRes;
}

/*
** Argument zPre must be a nul-terminated string. Set the interpreter
** result to a string containing the contents of zPre, followed by
** a space (" ") character, followed by a copy of string (z, n).
**
** In other words, the equivalent of:
*
**     printf("%s %.*s", zPre, n, z);
**
** Example:
**
**     Th_ErrorMessage(interp, "no such variable:", zVarname, nVarname);
**
*/
int Th_ErrorMessage(Th_Interp *interp, const char *zPre, const char *z, int n){
  if( interp ){
    char *zRes = 0;
    int nRes = 0;

    Th_SetVar(interp, (char *)"::th_stack_trace", -1, 0, 0);

    Th_StringAppend(interp, &zRes, &nRes, zPre, -1);
    if( zRes[nRes-1]=='"' ){
      Th_StringAppend(interp, &zRes, &nRes, z, n);
      Th_StringAppend(interp, &zRes, &nRes, (const char *)"\"", 1);
    }else{
      Th_StringAppend(interp, &zRes, &nRes, (const char *)" ", 1);
      Th_StringAppend(interp, &zRes, &nRes, z, n);
    }

    Th_SetResult(interp, zRes, nRes);
    Th_Free(interp, zRes);
  }

  return TH_OK;
}

/*
** Set the current interpreter result by taking a copy of the buffer
** pointed to by z, size n bytes. TH_OK is always returned.
*/
int Th_SetResult(Th_Interp *pInterp, const char *z, int n){

  /* Free the current result */
  Th_Free(pInterp, pInterp->zResult);
  pInterp->zResult = 0;
  pInterp->nResult = 0;

  if( n<0 ){
    n = th_strlen(z);
  }

  if( z && n>0 ){
    char *zResult;
    zResult = Th_Malloc(pInterp, n+1);
    memcpy(zResult, z, n);
    zResult[n] = '\0';
    pInterp->zResult = zResult;
    pInterp->nResult = n;
  }

  return TH_OK;
}

/*
** Return a pointer to the buffer containing the current interpreter
** result. If pN is not NULL, set *pN to the size of the returned
** buffer.
*/
const char *Th_GetResult(Th_Interp *pInterp, int *pN){
  assert(pInterp->zResult || pInterp->nResult==0);
  if( pN ){
    *pN = pInterp->nResult;
  }
  return (pInterp->zResult ? pInterp->zResult : (const char *)"");
}

/*
** Return a pointer to the buffer containing the current interpreter
** result. If pN is not NULL, set *pN to the size of the returned
** buffer.
**
** This function is the same as Th_GetResult() except that the
** caller is responsible for eventually calling Th_Free() on the
** returned buffer. The internal interpreter result is cleared
** after this function is called.
*/
char *Th_TakeResult(Th_Interp *pInterp, int *pN){
  if( pN ){
    *pN = pInterp->nResult;
  }
  if( pInterp->zResult ){
    char *zResult = pInterp->zResult;
    pInterp->zResult = 0;
    pInterp->nResult = 0;
    return zResult;
  }else{
    return (char *)Th_Malloc(pInterp, 1);
  }
}


/*
** Wrappers around the supplied malloc() and free()
*/
void *Th_Malloc(Th_Interp *pInterp, int nByte){
  void *p = pInterp->pVtab->xMalloc(nByte);
  if( p ){
    memset(p, 0, nByte);
  }
  return p;
}
void Th_Free(Th_Interp *pInterp, void *z){
  if( z ){
    pInterp->pVtab->xFree(z);
  }
}

/*
** Install a new th1 command.
**
** If a command of the same name already exists, it is deleted automatically.
*/
int Th_CreateCommand(
  Th_Interp *interp,
  const char *zName,                 /* New command name */
  Th_CommandProc xProc,              /* Command callback proc */
  void *pContext,                    /* Value to pass as second arg to xProc */
  void (*xDel)(Th_Interp *, void *)  /* Command destructor callback */
){
  Th_HashEntry *pEntry;
  Th_Command *pCommand;

  pEntry = Th_HashFind(interp, interp->paCmd, (const char *)zName, -1, 1);
  if( pEntry->pData ){
    pCommand = pEntry->pData;
    if( pCommand->xDel ){
      pCommand->xDel(interp, pCommand->pContext);
    }
  }else{
    pCommand = Th_Malloc(interp, sizeof(Th_Command));
  }
  pCommand->xProc = xProc;
  pCommand->pContext = pContext;
  pCommand->xDel = xDel;
  pEntry->pData = (void *)pCommand;

  return TH_OK;
}

/*
** Rename the existing command (zName, nName) to (zNew, nNew). If nNew is 0,
** the command is deleted instead of renamed.
**
** If successful, TH_OK is returned. If command zName does not exist, or
** if command zNew already exists, an error message is left in the
** interpreter result and TH_ERROR is returned.
*/
int Th_RenameCommand(
  Th_Interp *interp,
  const char *zName,             /* Existing command name */
  int nName,                     /* Number of bytes at zName */
  const char *zNew,              /* New command name */
  int nNew                       /* Number of bytes at zNew */
){
  Th_HashEntry *pEntry;
  Th_HashEntry *pNewEntry;

  pEntry = Th_HashFind(interp, interp->paCmd, zName, nName, 0);
  if( !pEntry ){
    Th_ErrorMessage(interp, "no such command:", zName, nName);
    return TH_ERROR;
  }
  assert(pEntry->pData);

  if( nNew>0 ){
    pNewEntry = Th_HashFind(interp, interp->paCmd, zNew, nNew, 1);
    if( pNewEntry->pData ){
      Th_ErrorMessage(interp, "command exists:", zNew, nNew);
      return TH_ERROR;
    }
    pNewEntry->pData = pEntry->pData;
  }else{
    Th_Command *pCommand = (Th_Command *)(pEntry->pData);
    if( pCommand->xDel ){
      pCommand->xDel(interp, pCommand->pContext);
    }
    Th_Free(interp, pCommand);
  }

  Th_HashFind(interp, interp->paCmd, zName, nName, -1);
  return TH_OK;
}

/*
** Push a stack frame onto the interpreter stack, invoke the
** callback, and pop the frame back off again. See the implementation
** of [proc] (th_lang.c) for an example.
*/
int Th_InFrame(Th_Interp *interp,
  int (*xCall)(Th_Interp *, void *pContext1, void *pContext2),
  void *pContext1,
  void *pContext2
){
  Th_Frame frame;
  int rc;
  thPushFrame(interp, &frame);
  rc = xCall(interp, pContext1, pContext2);
  thPopFrame(interp);
  return rc;
}

/*
** Split a th1 list into its component elements. The list to split is
** passed via arguments (zList, nList). If successful, TH_OK is returned.
** If an error occurs (if (zList, nList) is not a valid list) an error
** message is left in the interpreter result and TH_ERROR returned.
**
** If successful, *pnCount is set to the number of elements in the list.
** panElem is set to point at an array of *pnCount integers - the lengths
** of the element values. *pazElem is set to point at an array of
** pointers to buffers containing the array element's data.
**
** To free the arrays allocated at *pazElem and *panElem, the caller
** should call Th_Free() on *pazElem only. Exactly one such call to
** Th_Free() must be made per call to Th_SplitList().
**
** Example:
**
**     int nElem;
**     int *anElem;
**     char **azElem;
**     int i;
**
**     Th_SplitList(interp, zList, nList, &azElem, &anElem, &nElem);
**     for(i=0; i<nElem; i++){
**       int nData = anElem[i];
**       char *zData = azElem[i];
**       ...
**     }
**
**     Th_Free(interp, azElem);
**
*/
int Th_SplitList(
  Th_Interp *interp,
  const char *zList,              /* Pointer to buffer containing list */
  int nList,                      /* Number of bytes at zList */
  char ***pazElem,                /* OUT: Array of pointers to element data */
  int **panElem,                  /* OUT: Array of element data lengths */
  int *pnCount                    /* OUT: Number of elements in list */
){
  int rc;
  interp->isListMode = 1;
  rc = thSplitList(interp, zList, nList, pazElem, panElem, pnCount);
  interp->isListMode = 0;
  if( rc ){
    Th_ErrorMessage(interp, "Expected list, got: \"", zList, nList);
  }
  return rc;
}

/*
** Append a new element to an existing th1 list. The element to append
** to the list is (zElem, nElem).
**
** A pointer to the existing list must be stored at *pzList when this
** function is called. The length must be stored in *pnList. The value
** of *pzList must either be NULL (in which case *pnList must be 0), or
** a pointer to memory obtained from Th_Malloc().
**
** This function calls Th_Free() to free the buffer at *pzList and sets
** *pzList to point to a new buffer containing the new list value. *pnList
** is similarly updated before returning. The return value is always TH_OK.
**
** Example:
**
**     char *zList = 0;
**     int nList = 0;
**     for (...) {
**       char *zElem = <some expression>;
**       Th_ListAppend(interp, &zList, &nList, zElem, -1);
**     }
**     Th_SetResult(interp, zList, nList);
**     Th_Free(interp, zList);
**
*/
int Th_ListAppend(
  Th_Interp *interp,           /* Interpreter context */
  char **pzList,               /* IN/OUT: Ptr to ptr to list */
  int *pnList,                 /* IN/OUT: Current length of *pzList */
  const char *zElem,           /* Data to append */
  int nElem                    /* Length of nElem */
){
  Buffer output;
  int i;

  int hasSpecialChar = 0;
  int hasEscapeChar = 0;
  int nBrace = 0;

  output.zBuf = *pzList;
  output.nBuf = *pnList;
  output.nBufAlloc = output.nBuf;

  if( nElem<0 ){
    nElem = th_strlen(zElem);
  }
  if( output.nBuf>0 ){
    thBufferWrite(interp, &output, " ", 1);
  }

  for(i=0; i<nElem; i++){
    char c = zElem[i];
    if( th_isspecial(c) ) hasSpecialChar = 1;
    if( c=='\\' ) hasEscapeChar = 1;
    if( c=='{' ) nBrace++;
    if( c=='}' ) nBrace--;
  }

  if( nElem==0 || (!hasEscapeChar && hasSpecialChar && nBrace==0) ){
    thBufferWrite(interp, &output, "{", 1);
    thBufferWrite(interp, &output, zElem, nElem);
    thBufferWrite(interp, &output, "}", 1);
  }else{
    for(i=0; i<nElem; i++){
      char c = zElem[i];
      if( th_isspecial(c) ) thBufferWrite(interp, &output, "\\", 1);
      thBufferWrite(interp, &output, &c, 1);
    }
  }

  *pzList = output.zBuf;
  *pnList = output.nBuf;

  return TH_OK;
}

/*
** Append a new element to an existing th1 string. This function uses
** the same interface as the Th_ListAppend() function.
*/
int Th_StringAppend(
  Th_Interp *interp,           /* Interpreter context */
  char **pzStr,                /* IN/OUT: Ptr to ptr to list */
  int *pnStr,                  /* IN/OUT: Current length of *pzStr */
  const char *zElem,           /* Data to append */
  int nElem                    /* Length of nElem */
){
  char *zNew;
  int nNew;

  if( nElem<0 ){
    nElem = th_strlen(zElem);
  }

  nNew = *pnStr + nElem;
  zNew = Th_Malloc(interp, nNew);
  memcpy(zNew, *pzStr, *pnStr);
  memcpy(&zNew[*pnStr], zElem, nElem);

  Th_Free(interp, *pzStr);
  *pzStr = zNew;
  *pnStr = nNew;

  return TH_OK;
}

/*
** Initialize an interpreter.
*/
static int thInitialize(Th_Interp *interp){
  assert(interp->pFrame);

  Th_SetVar(interp, (char *)"::tcl_platform(engine)", -1, TH_ENGINE, -1);
  Th_SetVar(interp, (char *)"::tcl_platform(platform)", -1, TH_PLATFORM, -1);

  return TH_OK;
}

/*
** Delete an interpreter.
*/
void Th_DeleteInterp(Th_Interp *interp){
  assert(interp->pFrame);
  assert(0==interp->pFrame->pCaller);

  /* Delete the contents of the global frame. */
  thPopFrame(interp);

  /* Delete any result currently stored in the interpreter. */
  Th_SetResult(interp, 0, 0);

  /* Delete all registered commands and the command hash-table itself. */
  Th_HashIterate(interp, interp->paCmd, thFreeCommand, (void *)interp);
  Th_HashDelete(interp, interp->paCmd);

  /* Delete the interpreter structure itself. */
  Th_Free(interp, (void *)interp);
}

/*
** Create a new interpreter.
*/
Th_Interp * Th_CreateInterp(Th_Vtab *pVtab){
  Th_Interp *p;

  /* Allocate and initialise the interpreter and the global frame */
  p = pVtab->xMalloc(sizeof(Th_Interp) + sizeof(Th_Frame));
  memset(p, 0, sizeof(Th_Interp));
  p->pVtab = pVtab;
  p->paCmd = Th_HashNew(p);
  thPushFrame(p, (Th_Frame *)&p[1]);
  thInitialize(p);

  return p;
}

/*
** These two types are used only by the expression module, where
** the expression module means the Th_Expr() and exprXXX() functions.
*/
typedef struct Operator Operator;
struct Operator {
  const char *zOp;
  int nOp;
  int eOp;
  int iPrecedence;
  int eArgType;
};
typedef struct Expr Expr;
struct Expr {
  Operator *pOp;
  Expr *pParent;
  Expr *pLeft;
  Expr *pRight;

  char *zValue;      /* Pointer to literal value */
  int nValue;        /* Length of literal value buffer */
};

/* Unary operators */
#define OP_UNARY_MINUS  2
#define OP_UNARY_PLUS   3
#define OP_BITWISE_NOT  4
#define OP_LOGICAL_NOT  5

/* Binary operators */
#define OP_MULTIPLY     6
#define OP_DIVIDE       7
#define OP_MODULUS      8
#define OP_ADD          9
#define OP_SUBTRACT    10
#define OP_LEFTSHIFT   11
#define OP_RIGHTSHIFT  12
#define OP_LT          13
#define OP_GT          14
#define OP_LE          15
#define OP_GE          16
#define OP_EQ          17
#define OP_NE          18
#define OP_SEQ         19
#define OP_SNE         20
#define OP_BITWISE_AND 21
#define OP_BITWISE_XOR 22
#define OP_BITWISE_OR  24
#define OP_LOGICAL_AND 25
#define OP_LOGICAL_OR  26

/* Other symbols */
#define OP_OPEN_BRACKET  27
#define OP_CLOSE_BRACKET 28

/* Argument types. Each operator in the expression syntax is defined
** as requiring either integer, number (real or integer) or string
** operands.
*/
#define ARG_INTEGER 1
#define ARG_NUMBER  2
#define ARG_STRING  3

static Operator aOperator[] = {

  {"(",  1, OP_OPEN_BRACKET,   -1, 0},
  {")",  1, OP_CLOSE_BRACKET, -1, 0},

  /* Note: all unary operators have (iPrecedence==1) */
  {"-",  1, OP_UNARY_MINUS,    1, ARG_NUMBER},
  {"+",  1, OP_UNARY_PLUS,     1, ARG_NUMBER},
  {"~",  1, OP_BITWISE_NOT,    1, ARG_INTEGER},
  {"!",  1, OP_LOGICAL_NOT,    1, ARG_INTEGER},

  /* Binary operators. It is important to the parsing in Th_Expr() that
   * the two-character symbols ("==") appear before the one-character
   * ones ("="). And that the priorities of all binary operators are
   * integers between 2 and 12.
   */
  {"<<", 2, OP_LEFTSHIFT,      4, ARG_INTEGER},
  {">>", 2, OP_RIGHTSHIFT,     4, ARG_INTEGER},
  {"<=", 2, OP_LE,             5, ARG_NUMBER},
  {">=", 2, OP_GE,             5, ARG_NUMBER},
  {"==", 2, OP_EQ,             6, ARG_NUMBER},
  {"!=", 2, OP_NE,             6, ARG_NUMBER},
  {"eq", 2, OP_SEQ,            7, ARG_STRING},
  {"ne", 2, OP_SNE,            7, ARG_STRING},
  {"&&", 2, OP_LOGICAL_AND,   11, ARG_INTEGER},
  {"||", 2, OP_LOGICAL_OR,    12, ARG_INTEGER},

  {"*",  1, OP_MULTIPLY,       2, ARG_NUMBER},
  {"/",  1, OP_DIVIDE,         2, ARG_NUMBER},
  {"%",  1, OP_MODULUS,        2, ARG_INTEGER},
  {"+",  1, OP_ADD,            3, ARG_NUMBER},
  {"-",  1, OP_SUBTRACT,       3, ARG_NUMBER},
  {"<",  1, OP_LT,             5, ARG_NUMBER},
  {">",  1, OP_GT,             5, ARG_NUMBER},
  {"&",  1, OP_BITWISE_AND,    8, ARG_INTEGER},
  {"^",  1, OP_BITWISE_XOR,    9, ARG_INTEGER},
  {"|",  1, OP_BITWISE_OR,    10, ARG_INTEGER},

  {0,0,0,0,0}
};

/*
** The first part of the string (zInput,nInput) contains an integer.
** Set *pnVarname to the number of bytes in the numeric string.
*/
static int thNextInteger(
  Th_Interp *interp,
  const char *zInput,
  int nInput,
  int *pnLiteral
){
  int i;
  int (*isdigit)(char) = th_isdigit;
  char c;

  if( nInput<2) return TH_ERROR;
  assert(zInput[0]=='0');
  c = zInput[1];
  if( c>='A' && c<='Z' ) c += 'a' - 'A';
  if( c=='x' ){
    isdigit = th_ishexdig;
  }else if( c!='o' && c!='b' ){
    return TH_ERROR;
  }
  for(i=2; i<nInput; i++){
    c = zInput[i];
    if( !isdigit(c) ){
      break;
    }
  }
  *pnLiteral = i;
  return TH_OK;
}

/*
** The first part of the string (zInput,nInput) contains a number.
** Set *pnVarname to the number of bytes in the numeric string.
*/
static int thNextNumber(
  Th_Interp *interp,
  const char *zInput,
  int nInput,
  int *pnLiteral
){
  int i = 0;
  int seenDot = 0;
  for(; i<nInput; i++){
    char c = zInput[i];
    if( (seenDot || c!='.') && !th_isdigit(c) ) break;
    if( c=='.' ) seenDot = 1;
  }
  *pnLiteral = i;
  return TH_OK;
}

/*
** Free an expression tree.
*/
static void exprFree(Th_Interp *interp, Expr *pExpr){
  if( pExpr ){
    exprFree(interp, pExpr->pLeft);
    exprFree(interp, pExpr->pRight);
    Th_Free(interp, pExpr->zValue);
    Th_Free(interp, pExpr);
  }
}

/*
** Evaluate an expression tree.
*/
static int exprEval(Th_Interp *interp, Expr *pExpr){
  int rc = TH_OK;

  if( pExpr->pOp==0 ){
    /* A literal */
    rc = thSubstWord(interp, pExpr->zValue, pExpr->nValue);
  }else{
    int eArgType = 0;           /* Actual type of arguments */

    /* Argument values */
    int iLeft = 0;
    int iRight = 0;
    double fLeft;
    double fRight;

    /* Left and right arguments as strings */
    char *zLeft = 0; int nLeft = 0;
    char *zRight = 0; int nRight = 0;

    /* Evaluate left and right arguments, if they exist. */
    if( pExpr->pLeft ){
      rc = exprEval(interp, pExpr->pLeft);
      if( rc==TH_OK ){
        zLeft = Th_TakeResult(interp, &nLeft);
      }
    }
    if( rc==TH_OK && pExpr->pRight ){
      rc = exprEval(interp, pExpr->pRight);
      if( rc==TH_OK ){
        zRight = Th_TakeResult(interp, &nRight);
      }
    }

    /* Convert arguments to their required forms. */
    if( rc==TH_OK ){
      eArgType = pExpr->pOp->eArgType;
      if( eArgType==ARG_NUMBER ){
        if( (zLeft==0 || TH_OK==Th_ToInt(0, zLeft, nLeft, &iLeft))
         && (zRight==0 || TH_OK==Th_ToInt(0, zRight, nRight, &iRight))
        ){
          eArgType = ARG_INTEGER;
        }else if(
          (zLeft && TH_OK!=Th_ToDouble(interp, zLeft, nLeft, &fLeft)) ||
          (zRight && TH_OK!=Th_ToDouble(interp, zRight, nRight, &fRight))
        ){
          /* A type error. */
          rc = TH_ERROR;
        }
      }else if( eArgType==ARG_INTEGER ){
        rc = Th_ToInt(interp, zLeft, nLeft, &iLeft);
        if( rc==TH_OK && zRight ){
          rc = Th_ToInt(interp, zRight, nRight, &iRight);
        }
      }
    }

    if( rc==TH_OK && eArgType==ARG_INTEGER ){
      int iRes = 0;
      switch( pExpr->pOp->eOp ) {
        case OP_MULTIPLY:     iRes = iLeft*iRight;  break;
        case OP_DIVIDE:
          if( !iRight ){
            Th_ErrorMessage(interp, "Divide by 0:", zLeft, nLeft);
            rc = TH_ERROR;
            goto finish;
          }
          iRes = iLeft/iRight;
          break;
        case OP_MODULUS:
          if( !iRight ){
            Th_ErrorMessage(interp, "Modulo by 0:", zLeft, nLeft);
            rc = TH_ERROR;
            goto finish;
          }
          iRes = iLeft%iRight;
          break;
        case OP_ADD:          iRes = iLeft+iRight;  break;
        case OP_SUBTRACT:     iRes = iLeft-iRight;  break;
        case OP_LEFTSHIFT:    iRes = iLeft<<iRight; break;
        case OP_RIGHTSHIFT:   iRes = iLeft>>iRight; break;
        case OP_LT:           iRes = iLeft<iRight;  break;
        case OP_GT:           iRes = iLeft>iRight;  break;
        case OP_LE:           iRes = iLeft<=iRight; break;
        case OP_GE:           iRes = iLeft>=iRight; break;
        case OP_EQ:           iRes = iLeft==iRight; break;
        case OP_NE:           iRes = iLeft!=iRight; break;
        case OP_BITWISE_AND:  iRes = iLeft&iRight;  break;
        case OP_BITWISE_XOR:  iRes = iLeft^iRight;  break;
        case OP_BITWISE_OR:   iRes = iLeft|iRight;  break;
        case OP_LOGICAL_AND:  iRes = iLeft&&iRight; break;
        case OP_LOGICAL_OR:   iRes = iLeft||iRight; break;
        case OP_UNARY_MINUS:  iRes = -iLeft;        break;
        case OP_UNARY_PLUS:   iRes = +iLeft;        break;
        case OP_BITWISE_NOT:  iRes = ~iLeft;        break;
        case OP_LOGICAL_NOT:  iRes = !iLeft;        break;
        default: assert(!"Internal error");
      }
      Th_SetResultInt(interp, iRes);
    }else if( rc==TH_OK && eArgType==ARG_NUMBER ){
      switch( pExpr->pOp->eOp ) {
        case OP_MULTIPLY: Th_SetResultDouble(interp, fLeft*fRight);    break;
        case OP_DIVIDE:
          if( fRight==0.0 ){
            Th_ErrorMessage(interp, "Divide by 0:", zLeft, nLeft);
            rc = TH_ERROR;
            goto finish;
          }
          Th_SetResultDouble(interp, fLeft/fRight);
          break;
        case OP_ADD:         Th_SetResultDouble(interp, fLeft+fRight); break;
        case OP_SUBTRACT:    Th_SetResultDouble(interp, fLeft-fRight); break;
        case OP_LT:          Th_SetResultInt(interp, fLeft<fRight);    break;
        case OP_GT:          Th_SetResultInt(interp, fLeft>fRight);    break;
        case OP_LE:          Th_SetResultInt(interp, fLeft<=fRight);   break;
        case OP_GE:          Th_SetResultInt(interp, fLeft>=fRight);   break;
        case OP_EQ:          Th_SetResultInt(interp, fLeft==fRight);   break;
        case OP_NE:          Th_SetResultInt(interp, fLeft!=fRight);   break;
        case OP_UNARY_MINUS: Th_SetResultDouble(interp, -fLeft);       break;
        case OP_UNARY_PLUS:  Th_SetResultDouble(interp, +fLeft);       break;
        default: assert(!"Internal error");
      }
    }else if( rc==TH_OK ){
      int iEqual = 0;
      assert( eArgType==ARG_STRING );
      if( nRight==nLeft && 0==memcmp(zRight, zLeft, nRight) ){
        iEqual = 1;
      }
      switch( pExpr->pOp->eOp ) {
        case OP_SEQ:       Th_SetResultInt(interp, iEqual); break;
        case OP_SNE:       Th_SetResultInt(interp, !iEqual); break;
        default: assert(!"Internal error");
      }
    }

   finish:

    Th_Free(interp, zLeft);
    Th_Free(interp, zRight);
  }

  return rc;
}

/*
** Create an expression tree from an array of tokens. If successful,
** the root of the tree is stored in apToken[0].
*/
int exprMakeTree(Th_Interp *interp, Expr **apToken, int nToken){
  int iLeft;
  int i;
  int jj;

  assert(nToken>0);
#define ISTERM(x) (apToken[x] && (!apToken[x]->pOp || apToken[x]->pLeft))

  for(jj=0; jj<nToken; jj++){
    if( apToken[jj]->pOp && apToken[jj]->pOp->eOp==OP_OPEN_BRACKET ){
      int nNest = 1;
      int iLeft = jj;

      for(jj++; jj<nToken; jj++){
        Operator *pOp = apToken[jj]->pOp;
        if( pOp && pOp->eOp==OP_OPEN_BRACKET ) nNest++;
        if( pOp && pOp->eOp==OP_CLOSE_BRACKET ) nNest--;
        if( nNest==0 ) break;
      }
      if( jj==nToken ){
        return TH_ERROR;
      }
      if( (jj-iLeft)>1 ){
        if( exprMakeTree(interp, &apToken[iLeft+1], jj-iLeft-1) ){
          return TH_ERROR;
        }
        exprFree(interp, apToken[jj]);
        exprFree(interp, apToken[iLeft]);
        apToken[jj] = 0;
        apToken[iLeft] = 0;
      }
    }
  }

  iLeft = 0;
  for(jj=nToken-1; jj>=0; jj--){
    if( apToken[jj] ){
      if( apToken[jj]->pOp && apToken[jj]->pOp->iPrecedence==1
       && iLeft>0 && ISTERM(iLeft) ){
        apToken[jj]->pLeft = apToken[iLeft];
        apToken[jj]->pLeft->pParent = apToken[jj];
        apToken[iLeft] = 0;
      }
      iLeft = jj;
    }
  }
  for(i=2; i<=12; i++){
    iLeft = -1;
    for(jj=0; jj<nToken; jj++){
      Expr *pToken = apToken[jj];
      if( apToken[jj] ){
        if( pToken->pOp && !pToken->pLeft && pToken->pOp->iPrecedence==i ){
          int iRight = jj+1;
          for(; !apToken[iRight] && iRight<nToken; iRight++);
          if( iRight==nToken || iLeft<0 || !ISTERM(iRight) || !ISTERM(iLeft) ){
            return TH_ERROR;
          }
          pToken->pLeft = apToken[iLeft];
          apToken[iLeft] = 0;
          pToken->pLeft->pParent = pToken;
          pToken->pRight = apToken[iRight];
          apToken[iRight] = 0;
          pToken->pRight->pParent = pToken;
        }
        iLeft = jj;
      }
    }
  }
  for(jj=1; jj<nToken; jj++){
    assert( !apToken[jj] || !apToken[0] );
    if( apToken[jj] ){
      apToken[0] = apToken[jj];
      apToken[jj] = 0;
    }
  }

  return TH_OK;
}

/*
** Parse a string containing a TH expression to a list of tokens.
*/
static int exprParse(
  Th_Interp *interp,        /* Interpreter to leave error message in */
  const char *zExpr,        /* Pointer to input string */
  int nExpr,                /* Number of bytes at zExpr */
  Expr ***papToken,         /* OUT: Array of tokens. */
  int *pnToken              /* OUT: Size of token array */
){
  int i;

  int rc = TH_OK;
  int nNest = 0;
  int nToken = 0;
  Expr **apToken = 0;

  for(i=0; rc==TH_OK && i<nExpr; ){
    char c = zExpr[i];
    if( th_isspace(c) ){                                /* White-space     */
      i++;
    }else{
      Expr *pNew = (Expr *)Th_Malloc(interp, sizeof(Expr));
      const char *z = &zExpr[i];

      switch (c) {
        case '0':
          if( thNextInteger(interp, z, nExpr-i, &pNew->nValue)==TH_OK ){
            break;
          }
          /* fall through */
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9':
          thNextNumber(interp, z, nExpr-i, &pNew->nValue);
          break;

        case '$':
          thNextVarname(interp, z, nExpr-i, &pNew->nValue);
          break;

        case '{': case '[': {
          thNextCommand(interp, z, nExpr-i, &pNew->nValue);
          break;
        }

        case '"': {
          int iEnd = i;
          while( ++iEnd<nExpr && zExpr[iEnd]!='"' ){
            if( zExpr[iEnd]=='\\' ) iEnd++;
          }
          if( iEnd<nExpr ){
            pNew->nValue = iEnd+1-i;
          }
          break;
        }

        default: {
          int j;
          const char *zOp;
          for(j=0; (zOp=aOperator[j].zOp); j++){
            int nOp = aOperator[j].nOp;
            int nRemain = nExpr - i;
            int isMatch = 0;
            if( nRemain>=nOp && 0==memcmp(zOp, &zExpr[i], nOp) ){
              isMatch = 1;
            }
            if( isMatch ){
              if( aOperator[j].eOp==OP_CLOSE_BRACKET ){
                nNest--;
              }else if( nRemain>nOp ){
                if( aOperator[j].eOp==OP_OPEN_BRACKET ){
                  nNest++;
                }
              }else{
                /*
                ** This is not really a match because this operator cannot
                ** legally appear at the end of the string.
                */
                isMatch = 0;
              }
            }
            if( nToken>0 && aOperator[j].iPrecedence==1 ){
              Expr *pPrev = apToken[nToken-1];
              if( !pPrev->pOp || pPrev->pOp->eOp==OP_CLOSE_BRACKET ){
                continue;
              }
            }
            if( isMatch ){
              pNew->pOp = &aOperator[j];
              i += nOp;
              break;
            }
          }
        }
      }

      if( pNew->pOp || pNew->nValue ){
        if( pNew->nValue ){
          /* A terminal. Copy the string value. */
          assert( !pNew->pOp );
          pNew->zValue = Th_Malloc(interp, pNew->nValue);
          memcpy(pNew->zValue, z, pNew->nValue);
          i += pNew->nValue;
        }
        if( (nToken%16)==0 ){
          /* Grow the apToken array. */
          Expr **apTokenOld = apToken;
          apToken = Th_Malloc(interp, sizeof(Expr *)*(nToken+16));
          memcpy(apToken, apTokenOld, sizeof(Expr *)*nToken);
        }

        /* Put the new token at the end of the apToken array */
        apToken[nToken] = pNew;
        nToken++;
      }else{
        Th_Free(interp, pNew);
        rc = TH_ERROR;
      }
    }
  }

  if( nNest!=0 ){
    rc = TH_ERROR;
  }

  *papToken = apToken;
  *pnToken = nToken;
  return rc;
}

/*
** Evaluate the string (zExpr, nExpr) as a Th expression. Store
** the result in the interpreter interp and return TH_OK if
** successful. If an error occurs, store an error message in
** the interpreter result and return an error code.
*/
int Th_Expr(Th_Interp *interp, const char *zExpr, int nExpr){
  int rc;                           /* Return Code */
  int i;                            /* Loop counter */

  int nToken = 0;
  Expr **apToken = 0;

  if( nExpr<0 ){
    nExpr = th_strlen(zExpr);
  }

  /* Parse the expression to a list of tokens. */
  rc = exprParse(interp, zExpr, nExpr, &apToken, &nToken);

  /* If the parsing was successful, create an expression tree from
  ** the parsed list of tokens. If successful, apToken[0] is set
  ** to point to the root of the expression tree.
  */
  if( rc==TH_OK ){
    rc = exprMakeTree(interp, apToken, nToken);
  }

  if( rc!=TH_OK ){
    Th_ErrorMessage(interp, "syntax error in expression: \"", zExpr, nExpr);
  }

  /* Evaluate the expression tree. */
  if( rc==TH_OK ){
    rc = exprEval(interp, apToken[0]);
  }

  /* Free memory allocated by exprParse(). */
  for(i=0; i<nToken; i++){
    exprFree(interp, apToken[i]);
  }
  Th_Free(interp, apToken);

  return rc;
}

/*
** Allocate and return a pointer to a new hash-table. The caller should
** (eventually) delete the hash-table by passing it to Th_HashDelete().
*/
Th_Hash *Th_HashNew(Th_Interp *interp){
  Th_Hash *p;
  p = Th_Malloc(interp, sizeof(Th_Hash));
  return p;
}

/*
** Iterate through all values currently stored in the hash table. Invoke
** the callback function xCallback for each entry. The second argument
** passed to xCallback is a copy of the fourth argument passed to this
** function.  The return value from the callback function xCallback is
** ignored.
*/
void Th_HashIterate(
  Th_Interp *interp,
  Th_Hash *pHash,
  int (*xCallback)(Th_HashEntry *pEntry, void *pContext),
  void *pContext
){
  int i;
  for(i=0; i<TH_HASHSIZE; i++){
    Th_HashEntry *pEntry;
    Th_HashEntry *pNext;
    for(pEntry=pHash->a[i]; pEntry; pEntry=pNext){
      pNext = pEntry->pNext;
      xCallback(pEntry, pContext);
    }
  }
}

/*
** Helper function for Th_HashDelete().  Always returns non-zero.
*/
static int xFreeHashEntry(Th_HashEntry *pEntry, void *pContext){
  Th_Free((Th_Interp *)pContext, (void *)pEntry);
  return 1;
}

/*
** Free a hash-table previously allocated by Th_HashNew().
*/
void Th_HashDelete(Th_Interp *interp, Th_Hash *pHash){
  if( pHash ){
    Th_HashIterate(interp, pHash, xFreeHashEntry, (void *)interp);
    Th_Free(interp, pHash);
  }
}

/*
** This function is used to insert or delete hash table items, or to
** query a hash table for an existing item.
**
** If parameter op is less than zero, then the hash-table element
** identified by (zKey, nKey) is removed from the hash-table if it
** exists. NULL is returned.
**
** Otherwise, if the hash-table contains an item with key (zKey, nKey),
** a pointer to the associated Th_HashEntry is returned. If parameter
** op is greater than zero, then a new entry is added if one cannot
** be found. If op is zero, then NULL is returned if the item is
** not already present in the hash-table.
*/
Th_HashEntry *Th_HashFind(
  Th_Interp *interp,
  Th_Hash *pHash,
  const char *zKey,
  int nKey,
  int op                      /* -ve = delete, 0 = find, +ve = insert */
){
  unsigned int iKey = 0;
  int i;
  Th_HashEntry *pRet;
  Th_HashEntry **ppRet;

  if( nKey<0 ){
    nKey = th_strlen(zKey);
  }

  for(i=0; i<nKey; i++){
    iKey = (iKey<<3) ^ iKey ^ zKey[i];
  }
  iKey = iKey % TH_HASHSIZE;

  for(ppRet=&pHash->a[iKey]; (pRet=*ppRet); ppRet=&pRet->pNext){
    assert( pRet && ppRet && *ppRet==pRet );
    if( pRet->nKey==nKey && 0==memcmp(pRet->zKey, zKey, nKey) ) break;
  }

  if( op<0 && pRet ){
    assert( ppRet && *ppRet==pRet );
    *ppRet = pRet->pNext;
    Th_Free(interp, pRet);
    pRet = 0;
  }

  if( op>0 && !pRet ){
    pRet = (Th_HashEntry *)Th_Malloc(interp, sizeof(Th_HashEntry) + nKey);
    pRet->zKey = (char *)&pRet[1];
    pRet->nKey = nKey;
    memcpy(pRet->zKey, zKey, nKey);
    pRet->pNext = pHash->a[iKey];
    pHash->a[iKey] = pRet;
  }

  return pRet;
}

/*
** This function is the same as the standard strlen() function, except
** that it returns 0 (instead of being undefined) if the argument is
** a null pointer.
*/
int th_strlen(const char *zStr){
  int n = 0;
  if( zStr ){
    while( zStr[n] ) n++;
  }
  return n;
}

/* Whitespace characters:
**
**     ' '    0x20
**     '\t'   0x09
**     '\n'   0x0A
**     '\v'   0x0B
**     '\f'   0x0C
**     '\r'   0x0D
**
** Whitespace characters have the 0x01 flag set. Decimal digits have the
** 0x2 flag set. Single byte printable characters have the 0x4 flag set.
** Alphabet characters have the 0x8 bit set. Hexadecimal digits have the
** 0x20 flag set.
**
** The special list characters have the 0x10 flag set
**
**    { } [ ] \ ; ' "
**
**    " 0x22
**
*/
static unsigned char aCharProp[256] = {
  0,  0,  0,  0,  0,  0,  0,  0,     0,  1,  1,  1,  1,  1,  0,  0,   /* 0x0. */
  0,  0,  1,  1,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0,   /* 0x1. */
  5,  4, 20,  4,  4,  4,  4,  4,     4,  4,  4,  4,  4,  4,  4,  4,   /* 0x2. */
 38, 38, 38, 38, 38, 38, 38, 38,    38, 38,  4, 20,  4,  4,  4,  4,   /* 0x3. */
  4, 44, 44, 44, 44, 44, 44, 12,    12, 12, 12, 12, 12, 12, 12, 12,   /* 0x4. */
 12, 12, 12, 12, 12, 12, 12, 12,    12, 12, 12, 20, 20, 20,  4,  4,   /* 0x5. */
  4, 44, 44, 44, 44, 44, 44, 12,    12, 12, 12, 12, 12, 12, 12, 12,   /* 0x6. */
 12, 12, 12, 12, 12, 12, 12, 12,    12, 12, 12, 20,  4, 20,  4,  4,   /* 0x7. */

  0,  0,  0,  0,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0,   /* 0x8. */
  0,  0,  0,  0,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0,   /* 0x9. */
  0,  0,  0,  0,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0,   /* 0xA. */
  0,  0,  0,  0,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0,   /* 0xB. */
  0,  0,  0,  0,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0,   /* 0xC. */
  0,  0,  0,  0,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0,   /* 0xD. */
  0,  0,  0,  0,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0,   /* 0xE. */
  0,  0,  0,  0,  0,  0,  0,  0,     0,  0,  0,  0,  0,  0,  0,  0    /* 0xF. */
};

/*
** Clone of the standard isspace() and isdigit function/macros.
*/
int th_isspace(char c){
  return (aCharProp[(unsigned char)c] & 0x01);
}
int th_isdigit(char c){
  return (aCharProp[(unsigned char)c] & 0x02);
}
int th_isspecial(char c){
  return (aCharProp[(unsigned char)c] & 0x11);
}
int th_isalnum(char c){
  return (aCharProp[(unsigned char)c] & 0x0A);
}
int th_isalpha(char c){
  return (aCharProp[(unsigned char)c] & 0x08);
}
int th_ishexdig(char c){
  return (aCharProp[(unsigned char)c] & 0x20);
}
int th_isoctdig(char c){
  return ((c|7) == '7');
}
int th_isbindig(char c){
  return ((c|1) == '1');
}

#ifndef LONGDOUBLE_TYPE
# define LONGDOUBLE_TYPE long double
#endif


/*
** Return TRUE if z is a pure numeric string.  Return FALSE if the
** string contains any character which is not part of a number. If
** the string is numeric and contains the '.' character, set *realnum
** to TRUE (otherwise FALSE).
**
** An empty string is considered non-numeric.
*/
static int sqlite3IsNumber(const char *z, int *realnum){
  int incr = 1;
  if( *z=='-' || *z=='+' ) z += incr;
  if( !th_isdigit(*(u8*)z) ){
    return 0;
  }
  z += incr;
  if( realnum ) *realnum = 0;
  while( th_isdigit(*(u8*)z) ){ z += incr; }
  if( *z=='.' ){
    z += incr;
    if( !th_isdigit(*(u8*)z) ) return 0;
    while( th_isdigit(*(u8*)z) ){ z += incr; }
    if( realnum ) *realnum = 1;
  }
  if( *z=='e' || *z=='E' ){
    z += incr;
    if( *z=='+' || *z=='-' ) z += incr;
    if( !th_isdigit(*(u8*)z) ) return 0;
    while( th_isdigit(*(u8*)z) ){ z += incr; }
    if( realnum ) *realnum = 1;
  }
  return *z==0;
}

/*
** The string z[] is an ascii representation of a real number.
** Convert this string to a double.
**
** This routine assumes that z[] really is a valid number.  If it
** is not, the result is undefined.
**
** This routine is used instead of the library atof() function because
** the library atof() might want to use "," as the decimal point instead
** of "." depending on how locale is set.  But that would cause problems
** for SQL.  So this routine always uses "." regardless of locale.
*/
static int sqlite3AtoF(const char *z, double *pResult){
  int sign = 1;
  const char *zBegin = z;
  LONGDOUBLE_TYPE v1 = 0.0;
  while( th_isspace(*(u8*)z) ) z++;
  if( *z=='-' ){
    sign = -1;
    z++;
  }else if( *z=='+' ){
    z++;
  }
  while( th_isdigit(*(u8*)z) ){
    v1 = v1*10.0 + (*z - '0');
    z++;
  }
  if( *z=='.' ){
    LONGDOUBLE_TYPE divisor = 1.0;
    z++;
    while( th_isdigit(*(u8*)z) ){
      v1 = v1*10.0 + (*z - '0');
      divisor *= 10.0;
      z++;
    }
    v1 /= divisor;
  }
  if( *z=='e' || *z=='E' ){
    int esign = 1;
    int eval = 0;
    LONGDOUBLE_TYPE scale = 1.0;
    z++;
    if( *z=='-' ){
      esign = -1;
      z++;
    }else if( *z=='+' ){
      z++;
    }
    while( th_isdigit(*(u8*)z) ){
      eval = eval*10 + *z - '0';
      z++;
    }
    while( eval>=64 ){ scale *= 1.0e+64; eval -= 64; }
    while( eval>=16 ){ scale *= 1.0e+16; eval -= 16; }
    while( eval>=4 ){ scale *= 1.0e+4; eval -= 4; }
    while( eval>=1 ){ scale *= 1.0e+1; eval -= 1; }
    if( esign<0 ){
      v1 /= scale;
    }else{
      v1 *= scale;
    }
  }
  *pResult = sign<0 ? -v1 : v1;
  return z - zBegin;
}

/*
** Try to convert the string passed as arguments (z, n) to an integer.
** If successful, store the result in *piOut and return TH_OK.
**
** If the string cannot be converted to an integer, return TH_ERROR.
** If the interp argument is not NULL, leave an error message in the
** interpreter result too.
*/
int Th_ToInt(Th_Interp *interp, const char *z, int n, int *piOut){
  int i = 0;
  int iOut = 0;
  int base = 10;
  int (*isdigit)(char) = th_isdigit;

  if( n<0 ){
    n = th_strlen(z);
  }

  if( n>1 && (z[0]=='-' || z[0]=='+') ){
    i = 1;
  }
  if( (n-i)>2 && z[i]=='0' ){
    if( z[i+1]=='x' || z[i+1]=='X' ){
      i += 2;
      base = 16;
      isdigit = th_ishexdig;
    }else if( z[i+1]=='o' || z[i+1]=='O' ){
      i += 2;
      base = 8;
      isdigit = th_isoctdig;
    }else if( z[i+1]=='b' || z[i+1]=='B' ){
      i += 2;
      base = 2;
      isdigit = th_isbindig;
    }
  }
  for(; i<n; i++){
    char c = z[i];
    if( !isdigit(c) ){
      Th_ErrorMessage(interp, "expected integer, got: \"", z, n);
      return TH_ERROR;
    }
    if( c>='a' ){
      c -= 'a'-10;
    }else if( c>='A' ){
      c -= 'A'-10;
    }else{
      c -= '0';
    }
    iOut = iOut * base + c;
  }

  if( n>0 && z[0]=='-' ){
    iOut *= -1;
  }

  *piOut = iOut;
  return TH_OK;
}

/*
** Try to convert the string passed as arguments (z, n) to a double.
** If successful, store the result in *pfOut and return TH_OK.
**
** If the string cannot be converted to a double, return TH_ERROR.
** If the interp argument is not NULL, leave an error message in the
** interpreter result too.
*/
int Th_ToDouble(
  Th_Interp *interp,
  const char *z,
  int n,
  double *pfOut
){
  if( !sqlite3IsNumber((const char *)z, 0) ){
    Th_ErrorMessage(interp, "expected number, got: \"", z, n);
    return TH_ERROR;
  }

  sqlite3AtoF((const char *)z, pfOut);
  return TH_OK;
}

/*
** Set the result of the interpreter to the th1 representation of
** the integer iVal and return TH_OK.
*/
int Th_SetResultInt(Th_Interp *interp, int iVal){
  int isNegative = 0;
  char zBuf[32];
  char *z = &zBuf[32];

  if( iVal<0 ){
    isNegative = 1;
    iVal = iVal * -1;
  }
  *(--z) = '\0';
  *(--z) = (char)(48+((unsigned)iVal%10));
  while( (iVal = ((unsigned)iVal/10))>0 ){
    *(--z) = (char)(48+((unsigned)iVal%10));
    assert(z>zBuf);
  }
  if( isNegative ){
    *(--z) = '-';
  }

  return Th_SetResult(interp, z, -1);
}

/*
** Set the result of the interpreter to the th1 representation of
** the double fVal and return TH_OK.
*/
int Th_SetResultDouble(Th_Interp *interp, double fVal){
  int i;                /* Iterator variable */
  double v = fVal;      /* Input value */
  char zBuf[128];       /* Output buffer */
  char *z = zBuf;       /* Output cursor */
  int iDot = 0;         /* Digit after which to place decimal point */
  int iExp = 0;         /* Exponent (NN in eNN) */
  const char *zExp;     /* String representation of iExp */

  /* Precision: */
  #define INSIGNIFICANT 0.000000000001
  #define ROUNDER       0.0000000000005
  double insignificant = INSIGNIFICANT;

  /* If the real value is negative, write a '-' character to the
   * output and transform v to the corresponding positive number.
   */
  if( v<0.0 ){
    *z++ = '-';
    v *= -1.0;
  }

  /* Normalize v to a value between 1.0 and 10.0. Integer
   * variable iExp is set to the exponent. i.e the original
   * value is (v * 10^iExp) (or the negative thereof).
   */
  if( v>0.0 ){
    while( (v+ROUNDER)>=10.0 ) { iExp++; v *= 0.1; }
    while( (v+ROUNDER)<1.0 )   { iExp--; v *= 10.0; }
  }
  v += ROUNDER;

  /* For a small (<12) positive exponent, move the decimal point
   * instead of using the "eXX" notation.
   */
  if( iExp>0 && iExp<12 ){
    iDot = iExp;
    iExp = 0;
  }

  /* For a small (>-4) negative exponent, write leading zeroes
   * instead of using the "eXX" notation.
   */
  if( iExp<0 && iExp>-4 ){
    *z++ = '0';
    *z++ = '.';
    for(i=0; i>(iExp+1); i--){
      *z++ = '0';
    }
    iDot = -1;
    iExp = 0;
  }

  /* Output the digits in real value v. The value of iDot determines
   * where (if at all) the decimal point is placed.
   */
  for(i=0; i<=(iDot+1) || v>=insignificant; i++){
    *z++ = (char)(48 + (int)v);
    v = (v - ((double)(int)v)) * 10.0;
    insignificant *= 10.0;
    if( iDot==i ){
      *z++ = '.';
    }
  }

  /* If the exponent is not zero, add the "eXX" notation to the
   * end of the string.
   */
  if( iExp!=0 ){
    *z++ = 'e';
    Th_SetResultInt(interp, iExp);
    zExp = Th_GetResult(interp, 0);
    for(i=0; zExp[i]; i++){
      *z++ = zExp[i];
    }
  }

  *z = '\0';
  return Th_SetResult(interp, zBuf, -1);
}

/*
** Appends all currently registered command names to the specified list
** and returns TH_OK upon success.  Any other return value indicates an
** error.
*/
int Th_ListAppendCommands(
  Th_Interp *interp,      /* Interpreter context */
  char **pzList,          /* OUT: List of command names */
  int *pnList             /* OUT: Number of command names */
){
  Th_InterpAndList *p = (Th_InterpAndList *)Th_Malloc(
    interp, sizeof(Th_InterpAndList)
  );
  p->interp = interp;
  p->pzList = pzList;
  p->pnList = pnList;
  Th_HashIterate(interp, interp->paCmd, thListAppendHashKey, p);
  Th_Free(interp, p);
  return TH_OK;
}

/*
** Appends all variable names for the current frame to the specified list
** and returns TH_OK upon success.  Any other return value indicates an
** error.  If the current frame cannot be obtained, TH_ERROR is returned.
*/
int Th_ListAppendVariables(
  Th_Interp *interp,      /* Interpreter context */
  char **pzList,          /* OUT: List of variable names */
  int *pnList             /* OUT: Number of variable names */
){
  Th_Frame *pFrame = getFrame(interp, 0);
  if( pFrame ){
    Th_InterpAndList *p = (Th_InterpAndList *)Th_Malloc(
      interp, sizeof(Th_InterpAndList)
    );
    p->interp = interp;
    p->pzList = pzList;
    p->pnList = pnList;
    Th_HashIterate(interp, pFrame->paVar, thListAppendHashKey, p);
    Th_Free(interp, p);
    return TH_OK;
  }else{
    return TH_ERROR;
  }
}

/*
** Appends all array element names for the specified array variable to the
** specified list and returns TH_OK upon success.  Any other return value
** indicates an error.
*/
int Th_ListAppendArray(
  Th_Interp *interp,      /* Interpreter context */
  const char *zVar,       /* Pointer to variable name */
  int nVar,               /* Number of bytes at nVar */
  char **pzList,          /* OUT: List of array element names */
  int *pnList             /* OUT: Number of array element names */
){
  Th_Variable *pValue = thFindValue(interp, zVar, nVar, 0, 1, 1, 0);
  if( pValue && !pValue->zData && pValue->pHash ){
    Th_InterpAndList *p = (Th_InterpAndList *)Th_Malloc(
      interp, sizeof(Th_InterpAndList)
    );
    p->interp = interp;
    p->pzList = pzList;
    p->pnList = pnList;
    Th_HashIterate(interp, pValue->pHash, thListAppendHashKey, p);
    Th_Free(interp, p);
  }else{
    *pzList = 0;
    *pnList = 0;
  }
  return TH_OK;
}
