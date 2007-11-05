/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains an implementation of the "subscript" interpreter.
**
** Subscript attempts to be an extremely light-weight scripting
** language.  It contains the barest of bare essentials.  It is
** stack-based and forth-like.  Everything is in a single global
** namespace.  There is only a single datatype of zero-terminated
** string.  The stack is of fixed, limited depth.  The symbal table
** is of a limited and fixed size.
**
** TOKENS:
**
**      *  All tokens are separated from each other by whitespace.
**      *  Leading and trailing whitespace is ignored.
**      *  Text within nested {...} is a single string token.  The outermost
**         curly braces are not part of the token.
**      *  An identifier with a leading "/" is a string token.
**      *  A token that looks like a number is a string token.
**      *  An identifier token is called a "verb".
**
** PROCESSING:
**
**      *  The input is divided into tokens.  Whitespace is discarded.
**         String and verb tokens are passed into the engine.
**      *  String tokens are pushed onto the stack.
**      *  If a verb token corresponds to a procedure, that procedure is
**         run.  The procedure might use, pop, or pull elements from 
**         the stack.
**      *  If a verb token corresponds to a variable, the value of that
**         variable is pushed onto the stack.
**
** This module attempts to be completely self-contained so that it can
** be portable to other projects.
*/
#include "config.h"
#include "subscript.h"
#include <assert.h>

#if INTERFACE
typedef struct Subscript Subscript;
#define SBS_OK      0
#define SBS_ERROR   1
#endif

/*
** Configuration constants
*/
#define SBSCONFIG_NHASH    41         /* Size of the hash table */
#define SBSCONFIG_NSTACK   10         /* Maximum stack depth */
#define SBSCONFIG_ERRSIZE  100        /* Maximum size of an error message */

/*
** Available token types:
*/
#define SBSTT_WHITESPACE  1    /* ex:   \040   */
#define SBSTT_NAME        2    /* ex:   /abcde  */
#define SBSTT_VERB        3    /* ex:   abcde   */
#define SBSTT_STRING      4    /* ex:   {...}   */
#define SBSTT_INTEGER     5    /* Integer including option sign */
#define SBSTT_INCOMPLETE  6    /* Unterminated string token */
#define SBSTT_UNKNOWN     7    /* Unknown token */
#define SBSTT_EOF         8    /* End of input */

/*
** Values are stored in the hash table as instances of the following
** structure.
*/
typedef struct SbSValue SbSValue;
struct SbSValue {
  int flags;        /* Bitmask of SBSVAL_* values */
  union {
    struct {
      int size;        /* Number of bytes in string, not counting final zero */
      char *z;         /* Pointer to string content */
    } str;          /* Value if SBSVAL_STR */
    struct {
      int (*xVerb)(Subscript*, void*);     /* Function to do the work */
      void *pArg;                          /* 2nd parameter to xVerb */
    } verb;         /* Value if SBSVAL_VERB */
  } u;              
};
#define SBSVAL_VERB    0x0001      /* Value stored in u.verb */
#define SBSVAL_STR     0x0002      /* Value stored in u.str */ 
#define SBSVAL_DYN     0x0004      /* u.str.z is dynamically allocated */
#define SBSVAL_EXEC    0x0008      /* u.str.z is a script */

/*
** An entry in the hash table is an instance of this structure.
*/
typedef struct SbsHashEntry SbsHashEntry;
struct SbsHashEntry {
  SbsHashEntry *pNext;     /* Next entry with the same hash on zKey */
  SbSValue val;            /* The payload */
  int nKey;               /* Length of the key */
  char zKey[0];           /* The key */
};

/*
** A hash table is an instance of the following structure.
*/
typedef struct SbsHashTab SbsHashTab;
struct SbsHashTab {
  SbsHashEntry *aHash[SBSCONFIG_NHASH];  /* The hash table */
};

/*
** An instance of the Subscript interpreter
*/
struct Subscript {
  int nStack;                      /* Number of entries on stack */
  SbsHashTab symTab;                /* The symbol table */
  char zErrMsg[SBSCONFIG_ERRSIZE];  /* Space to write an error message */
  SbSValue aStack[SBSCONFIG_NSTACK]; /* The stack */
};


/*
** Given an input string z of length n, identify the token that
** starts at z[0].  Write the token type into *pTokenType and
** return the length of the token.
*/
static int sbs_next_token(const char *z, int n, int *pTokenType){
  int c;
  if( n<=0 || z[0]==0 ){
    *pTokenType = SBSTT_EOF;
    return 0;
  }
  c = z[0];
  if( isspace(c) ){
    int i;
    *pTokenType = SBSTT_WHITESPACE;
    for(i=1; i<n && isspace(z[i]); i++){}
    return i;
  }
  if( c=='#' ){
    int i;
    for(i=1; i<n && z[i] && z[i-1]!='\n'; i++){}
    *pTokenType = SBSTT_WHITESPACE;
    return i;
  }
  if( c=='{' ){
    int depth = 1;
    int i;
    for(i=1; i<n && z[i]; i++){
      if( z[i]=='{' ){
        depth++;
      }else if( z[i]=='}' ){
        depth--;
        if( depth==0 ){
          i++;
          break;
        }
      }
    }
    if( depth ){
      *pTokenType = SBSTT_INCOMPLETE;
    }else{
      *pTokenType = SBSTT_STRING;
    }
    return i;
  }
  if( c=='/' && n>=2 && isalpha(z[1]) ){
    int i;
    for(i=2; i<n && (isalnum(z[i]) || z[i]=='_'); i++){}
    *pTokenType = SBSTT_NAME;
    return i;
  }
  if( isalpha(c) ){
    int i;
    for(i=1; i<n && (isalnum(z[i]) || z[i]=='_'); i++){}
    *pTokenType = SBSTT_VERB;
    return i;
  }
  if( isdigit(c) || ((c=='-' || c=='+') && n>=2 && isdigit(z[1])) ){
    int i;
    for(i=1; i<n && isdigit(z[i]); i++){}
    *pTokenType = SBSTT_INTEGER;
    return i;
  }
  *pTokenType = SBSTT_UNKNOWN;
  return 1;
}


/*
** Release any memory allocated by a value.
*/
static void sbs_value_reset(SbSValue *p){
  if( p->flags & SBSVAL_DYN ){
    free(p->u.str.z);
    p->flags = SBSVAL_STR;
    p->u.str.z = "";
    p->u.str.size = 0;
  }
}

/*
** Compute a hash on a string.
*/
static int sbs_hash(const char *z, int n){
  int h = 0;
  int i;
  for(i=0; i<n; i++){
    h ^= (h<<1) | z[i];
  }
  h &= 0x7ffffff;
  return h % SBSCONFIG_NHASH;
}

/*
** Look up a value in the hash table.  Return a pointer to the value.
** Return NULL if not found.
*/
static const SbSValue *sbs_fetch(
  SbsHashTab *pHash, 
  const char *zKey, 
  int nKey
){
  int h;
  SbsHashEntry *p;

  if( nKey<0 ) nKey = strlen(zKey);
  h = sbs_hash(zKey, nKey);
  for(p = pHash->aHash[h]; p; p=p->pNext){
    if( p->nKey==nKey && memcmp(p->zKey,zKey,nKey)==0 ){
      return &p->val;
    }
  }
  return 0;
}

/*
** Store a value in the hash table.  Overwrite any prior value stored
** under the same name.
**
** If the value in the 4th argument needs to be reset or freed,
** the hash table will take over responsibiliity for doing so.
*/
static int sbs_store(
  SbsHashTab *pHash,       /* Insert into this hash table */
  const char *zKey,        /* The key */
  int nKey,                /* Size of the key */
  const SbSValue *pValue   /* The value to be stored */
){
  int h;
  SbsHashEntry *p, *pNew;

  if( nKey<0 ) nKey = strlen(zKey);
  h = sbs_hash(zKey, nKey);
  for(p = pHash->aHash[h]; p; p=p->pNext){
    if( p->nKey==nKey && memcmp(p->zKey,zKey,nKey)==0 ){
      sbs_value_reset(&p->val);
      memcpy(&p->val, pValue, sizeof(p->val));
      return SBS_OK;
    }
  }
  pNew = malloc( sizeof(*pNew) + nKey );
  if( pNew ){
    pNew->nKey = nKey;
    memcpy(pNew->zKey, zKey, nKey+1);
    memcpy(&pNew->val, pValue, sizeof(pNew->val));
    pNew->pNext = pHash->aHash[h];
    pHash->aHash[h] = pNew;
    return SBS_OK;
  }
  return SBS_ERROR;
}

/*
** Reset a hash table.
*/
static void sbs_hash_reset(SbsHashTab *pHash){
  int i;
  SbsHashEntry *p, *pNext;
  for(i=0; i<SBSCONFIG_NHASH; i++){
    for(p=pHash->aHash[i]; p; p=pNext){
      pNext = p->pNext;
      sbs_value_reset(&p->val);
      free(p);
    }
  }
  memset(pHash, 0, sizeof(*pHash));
}

/*
** Push a value onto the stack of an interpreter
*/
static int sbs_push(Subscript *p, SbSValue *pVal){
  if( p->nStack>=SBSCONFIG_NSTACK ){
    sqlite3_snprintf(SBSCONFIG_ERRSIZE, p->zErrMsg, "stack overflow");
    return SBS_ERROR;
  }
  p->aStack[p->nStack++] = *pVal;
  return SBS_OK;
}

/*
** Create a new subscript interpreter.  Return a pointer to the
** new interpreter, or return NULL if malloc fails.
*/
struct Subscript *SbS_Create(void){
  Subscript *p;
  p = malloc( sizeof(*p) );
  if( p ){
    memset(p, 0, sizeof(*p));
  }
  return p;
}

/*
** Destroy an subscript interpreter
*/
void SbS_Destroy(struct Subscript *p){
  int i;
  sbs_hash_reset(&p->symTab);
  for(i=0; i<p->nStack; i++){
    sbs_value_reset(&p->aStack[i]);
  }
  free(p);
}

/*
** Set the error message for an interpreter.  Verb implementations
** use this routine when they encounter an error.
*/
void SbS_SetErrorMessage(struct Subscript *p, const char *zErr){
  int nErr = strlen(zErr);
  if( nErr>sizeof(p->zErrMsg)-1 ){
    nErr = sizeof(p->zErrMsg)-1;
  }
  memcpy(p->zErrMsg, zErr, nErr);
  p->zErrMsg[nErr] = 0;
}

/*
** Return a pointer to the current error message for the
** interpreter.
*/
const char *SbS_GetErrorMessage(struct Subscript *p){
  return p->zErrMsg;
}

/*
** Add a new verb the given interpreter
*/
int SbS_AddVerb(
  struct Subscript *p,
  const char *zVerb,
  int (*xVerb)(struct Subscript*,void*),
  void *pArg
){
  SbSValue v;
  v.flags = SBSVAL_VERB;
  v.u.verb.xVerb = xVerb;
  v.u.verb.pArg = pArg;
  return sbs_store(&p->symTab, zVerb, -1, &v);
}

/*
** Push a string value onto the stack.
**
** If the 4th parameter is 0, then the string is static.
** If the 4th parameter is non-zero then the string was obtained
** from malloc and Subscript will take responsibility for freeing
** it.
**
** Return 0 on success and non-zero if there is an error.
*/
int SbS_Push(
  struct Subscript *p,  /* Push onto this interpreter */
  char *z,              /* String value to push */
  int n,                /* Length of the string, or -1 */
  int dyn               /* If true, z was obtained from malloc */
){
  SbSValue v;
  v.flags = SBSVAL_STR;
  if( dyn ){
    v.flags |= SBSVAL_DYN;
  }
  if( n<0 ) n = strlen(z);
  v.u.str.size = n;
  v.u.str.z = z;
  return sbs_push(p, &v);
}

/*
** Push an integer value onto the stack.
**
** This routine really just converts the integer into a string
** then calls SbS_Push.
*/
int SbS_PushInt(struct Subscript *p, int iVal){
  if( iVal==0 ){
    return SbS_Push(p, "0", 1, 0);
  }else if( iVal==1 ){
    return SbS_Push(p, "1", 1, 0);
  }else{
    char *z;
    int n;
    char zVal[50];
    sprintf(zVal, "%d", iVal);
    n = strlen(zVal);
    z = malloc( n+1 );
    if( z ){
      strcpy(z, zVal);
      return SbS_Push(p, z, n, 1);
    }else{
      return SBS_ERROR;
    }
  }
}

/*
** Pop and destroy zero or more values from the stack.
** Return the number of values remaining on the stack after
** the pops occur.
*/
int SbS_Pop(struct Subscript *p, int N){
  while( N>0 && p->nStack>0 ){
    p->nStack--;
    sbs_value_reset(&p->aStack[p->nStack]);
    N--;
  }
  return p->nStack;
}

/*
** Return the N-th element of the stack.  0 is the top of the stack.
** 1 is the first element down.  2 is the second element.  And so forth.
** Return NULL if there is no N-th element.
**
** The pointer returned is only valid until the value is popped
** from the stack.
*/
const char *SbS_StackValue(struct Subscript *p, int N, int *pSize){
  SbSValue *pVal;
  if( N<0 || N>=p->nStack ){
    return 0;
  }
  pVal = &p->aStack[p->nStack-N-1];
  if( (pVal->flags & SBSVAL_STR)==0 ){
    return 0;
  }
  *pSize = pVal->u.str.size;
  return pVal->u.str.z;
}

/*
** A convenience routine for extracting an integer value from the
** stack.
*/
int SbS_StackValueInt(struct Subscript *p, int N){
  int n, v;
  int isNeg = 0;
  const char *z = SbS_StackValue(p, N, &n);
  v = 0;
  if( n==0 ) return 0;
  if( z[0]=='-' ){
    isNeg = 1;
    z++;
    n--;
  }else if( z[0]=='+' ){
    z++;
    n--;
  }
  while( n>0 && isdigit(z[0]) ){
    v = v*10 + z[0] - '0';
    z++;
    n--;
  }
  if( isNeg ){
    v = -v;
  }
  return v;
}

/*
** Retrieve the value of a variable from the interpreter.  Return
** NULL if no such variable is defined.  
**
** The returned string is not necessarily (probably not) zero-terminated.
** The string may be deallocated the next time anything is done to
** the interpreter.  Make a copy if you need it to persist.
*/
const char *SbS_Fetch(
  struct Subscript *p,   /* The interpreter we are interrogating */
  const char *zKey,        /* Name of the variable.  Case sensitive */
  int *pLength             /* Write the length here */
){
  const SbSValue *pVal;

  pVal = sbs_fetch(&p->symTab, zKey, -1);
  if( pVal==0 || (pVal->flags & SBSVAL_STR)==0 ){
    *pLength = 0;
    return 0;
  }else{
    *pLength = pVal->u.str.size;
    return pVal->u.str.z;
  }
}

/*
** Generate an error and return non-zero if the stack has
** fewer than N elements.  This is utility routine used in
** the implementation of verbs.
*/
int SbS_RequireStack(struct Subscript *p, int N, const char *zCmd){
  if( p->nStack>=N ) return 0;
  sqlite3_snprintf(sizeof(p->zErrMsg), p->zErrMsg,
     "\"%s\" requires at least %d stack elements - only found %d",
     zCmd, N, p->nStack);
  return 1;
}

/*
** Subscript command:       STRING NAME set
**
** Write the value of STRING into variable called NAME.
*/
static int setCmd(Subscript *p, void *pNotUsed){
  SbSValue *pTos;
  SbSValue *pNos;
  if( SbS_RequireStack(p, 2, "set") ) return SBS_ERROR;
  pTos = &p->aStack[--p->nStack];
  pNos = &p->aStack[--p->nStack];
  sbs_store(&p->symTab, pTos->u.str.z, pTos->u.str.size, pNos);
  sbs_value_reset(pTos);
  return 0;
}

/*
** Subscript command:      INTEGER not INTEGER
*/
static int notCmd(struct Subscript *p, void *pNotUsed){
  int n;
  if( SbS_RequireStack(p, 1, "not") ) return 1;
  n = SbS_StackValueInt(p, 0);
  SbS_Pop(p, 1);
  SbS_PushInt(p, !n);
  return 0;
}

#define SBSOP_ADD   1
#define SBSOP_SUB   2
#define SBSOP_MUL   3
#define SBSOP_DIV   4
#define SBSOP_AND   5
#define SBSOP_OR    6
#define SBSOP_MIN   7
#define SBSOP_MAX   8

/*
** Subscript command:      INTEGER INTEGER <binary-op> INTEGER
*/
static int bopCmd(struct Subscript *p, void *pOp){
  int a, b, c;
  if( SbS_RequireStack(p, 2, "BINARY-OP") ) return 1;
  a = SbS_StackValueInt(p, 0);
  b = SbS_StackValueInt(p, 1);
  switch( (int)pOp ){
    case SBSOP_ADD:  c = a+b;            break;
    case SBSOP_SUB:  c = a-b;            break;
    case SBSOP_MUL:  c = a*b;            break;
    case SBSOP_DIV:  c = b!=0 ? a/b : 0; break;
    case SBSOP_AND:  c = a && b;         break;
    case SBSOP_OR:   c = a || b;         break;
    case SBSOP_MIN:  c = a<b ? a : b;    break;
    case SBSOP_MAX:  c = a<b ? b : a;    break;
  }
  SbS_Pop(p, 2);
  SbS_PushInt(p, c);
  return 0;
}

/*
** Subscript command:     STRING hascap INTEGER
**
** Return true if the user has all of the capabilities listed.
*/
static int hascapCmd(struct Subscript *p, void *pNotUsed){
  const char *z;
  int i, n, a;
  if( SbS_RequireStack(p, 1, "hascap") ) return 1;
  z = SbS_StackValue(p, 0, &n);
  a = login_has_capability(z, n);
  SbS_Pop(p, 1);
  SbS_PushInt(p, a);
}

/*
** Subscript command:      STRING puts
*/
static int putsCmd(struct Subscript *p, void *pNotUsed){
  int size;
  const char *z;
  if( SbS_RequireStack(p, 1, "puts") ) return 1;
  z = SbS_StackValue(p, 0, &size);
  if( g.cgiPanic ){
    char *zCopy = mprintf("%.*s", size, z);
    cgi_printf("%h", zCopy);
    free(zCopy);
  }else{
    printf("%.*s\n", size, z);
  }
  SbS_Pop(p, 1);
  return 0;
}


/*
** A table of built-in commands
*/
static const struct {
  const char *zCmd;
  int (*xCmd)(Subscript*,void*);
  void *pArg;
} aBuiltin[] = {
  { "add",    bopCmd,    (void*)SBSOP_AND    },
  { "and",    bopCmd,    (void*)SBSOP_AND    },
  { "div",    bopCmd,    (void*)SBSOP_DIV    },
  { "hascap", hascapCmd, 0                },
  { "max",    bopCmd,    (void*)SBSOP_MAX    },
  { "min",    bopCmd,    (void*)SBSOP_MIN    },
  { "mul",    bopCmd,    (void*)SBSOP_MUL    },
  { "not",    notCmd,    0                   },
  { "or",     bopCmd,    (void*)SBSOP_OR     },
  { "puts",   putsCmd,   0                   },
  { "set",    setCmd,    0                   },
  { "sub",    bopCmd,    (void*)SBSOP_SUB    },
};
  

/*
** Compare a zero-terminated string zPattern against
** an unterminated string zStr of length nStr.
*/
static int compare_cmd(const char *zPattern, const char *zStr, int nStr){
  int c = strncmp(zPattern, zStr, nStr);
  if( c==0 && zPattern[nStr]!=0 ){
    c = -1;
  }
  return c;
}

/*
** Evaluate the script given by the first nScript bytes of zScript[].
** Return 0 on success and non-zero for an error.
*/
int SbS_Eval(struct Subscript *p, const char *zScript, int nScript){
  int rc = SBS_OK;
  if( nScript<0 ) nScript = strlen(zScript);
  while( nScript>0 && rc==SBS_OK ){
    int n;
    int ttype;
    n = sbs_next_token(zScript, nScript, &ttype);
    switch( ttype ){
      case SBSTT_WHITESPACE: {
        break;
      }
      case SBSTT_EOF: {
        nScript = 0;
        break;
      }
      case SBSTT_INCOMPLETE:
      case SBSTT_UNKNOWN: {
        rc = SBS_ERROR;
        nScript = n;
        break;
      }
      case SBSTT_INTEGER: {
        rc = SbS_Push(p, (char*)zScript, n, 0);
        break;
      }
      case SBSTT_NAME: {
        rc = SbS_Push(p, (char*)&zScript[1], n-1, 0);
        break;
      }
      case SBSTT_STRING: {
        rc = SbS_Push(p, (char*)&zScript[1], n-2, 0);
        break;
      }
      case SBSTT_VERB: {
        /* First look up the verb in the hash table */
        const SbSValue *pVal = sbs_fetch(&p->symTab, (char*)zScript, n);
        if( pVal==0 ){
          /* If the verb is not in the hash table, look for a 
          ** built-in command */
          int upr = sizeof(aBuiltin)/sizeof(aBuiltin[0]) - 1;
          int lwr = 0;
          rc = SBS_ERROR;
          while( upr>=lwr ){
            int i = (upr+lwr)/2;
            int c = compare_cmd(aBuiltin[i].zCmd, zScript, n);
            if( c==0 ){
              rc = aBuiltin[i].xCmd(p, aBuiltin[i].pArg);
              break;
            }else if( c<0 ){
              upr = i-1;
            }else{
              lwr = i+1;
            }
          }
        }else if( pVal->flags & SBSVAL_VERB ){
          rc = pVal->u.verb.xVerb(p, pVal->u.verb.pArg);
        }else if( pVal->flags & SBSVAL_EXEC ){
          rc = SbS_Eval(p, pVal->u.str.z, pVal->u.str.size);
        }else{
          rc = SbS_Push(p, pVal->u.str.z, pVal->u.str.size, 0);
        }
        break;
      }
    }
    zScript += n;
    nScript -= n;
  }
  return rc;
}

/*
** COMMAND: test-subscript
*/
void test_subscript(void){
  Subscript *p;
  if( g.argc<3 ){
    usage("SCRIPT");
  }
  p = SbS_Create();
  SbS_Eval(p, g.argv[2], strlen(g.argv[2]));
}
