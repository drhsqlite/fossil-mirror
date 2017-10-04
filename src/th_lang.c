
/*
** This file contains the implementation of all of the TH language
** built-in commands.
**
** All built-in commands are implemented using the public interface
** declared in th.h, so this file serves as both a part of the language
** implementation and an example of how to extend the language with
** new commands.
*/

#include "config.h"
#include "th.h"
#include <string.h>
#include <assert.h>

int Th_WrongNumArgs(Th_Interp *interp, const char *zMsg){
  Th_ErrorMessage(interp, "wrong # args: should be \"", zMsg, -1);
  return TH_ERROR;
}

/*
** Syntax:
**
**   catch script ?varname?
*/
static int catch_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int rc;

  if( argc!=2 && argc!=3 ){
    return Th_WrongNumArgs(interp, "catch script ?varname?");
  }

  rc = Th_Eval(interp, 0, argv[1], -1);
  if( argc==3 ){
    int nResult;
    const char *zResult = Th_GetResult(interp, &nResult);
    Th_SetVar(interp, argv[2], argl[2], zResult, nResult);
  }

  Th_SetResultInt(interp, rc);
  return TH_OK;
}

/*
** TH Syntax:
**
**   if expr1 body1 ?elseif expr2 body2? ? ?else? bodyN?
*/
static int if_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int rc = TH_OK;

  int iCond;           /* Result of evaluating expression */
  int i;

  const char *zResult;
  int nResult;

  if( argc<3 ){
    goto wrong_args;
  }

  for(i=0; i<argc && rc==TH_OK; i+=3){
    if( i>argc-3 ){
      i = argc-3;
      iCond = 1;
    }else{
      if( TH_OK!=Th_Expr(interp, argv[i+1], argl[i+1]) ){
        return TH_ERROR;
      }
      zResult = Th_GetResult(interp, &nResult);
      rc = Th_ToInt(interp, zResult, nResult, &iCond);
    }
    if( iCond && rc==TH_OK ){
      rc = Th_Eval(interp, 0, argv[i+2], -1);
      break;
    }
  }

  return rc;

wrong_args:
  return Th_WrongNumArgs(interp, "if ...");
}

/*
** TH Syntax:
**
**   expr expr
*/
static int expr_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "expr expression");
  }

  return Th_Expr(interp, argv[1], argl[1]);
}

/*
** Evaluate the th1 script (zBody, nBody) in the local stack frame.
** Return the result of the evaluation, except if the result
** is TH_CONTINUE, return TH_OK instead.
*/
static int eval_loopbody(Th_Interp *interp, const char *zBody, int nBody){
  int rc = Th_Eval(interp, 0, zBody, nBody);
  if( rc==TH_CONTINUE ){
    rc = TH_OK;
  }
  return rc;
}

/*
** TH Syntax:
**
**   for init condition incr script
*/
static int for_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int rc;
  int iCond;

  if( argc!=5 ){
    return Th_WrongNumArgs(interp, "for init condition incr script");
  }

  /* Evaluate the 'init' script */
  rc = Th_Eval(interp, 0, argv[1], -1);

  while( rc==TH_OK
     && TH_OK==(rc = Th_Expr(interp, argv[2], -1))
     && TH_OK==(rc = Th_ToInt(interp, Th_GetResult(interp, 0), -1, &iCond))
     && iCond
     && TH_OK==(rc = eval_loopbody(interp, argv[4], argl[4]))
  ){
    rc = Th_Eval(interp, 0, argv[3], -1);
  }

  if( rc==TH_BREAK ) rc = TH_OK;
  return rc;
}

/*
** TH Syntax:
**
**   list ?arg1 ?arg2? ...?
*/
static int list_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  char *zList = 0;
  int nList = 0;
  int i;

  for(i=1; i<argc; i++){
    Th_ListAppend(interp, &zList, &nList, argv[i], argl[i]);
  }

  Th_SetResult(interp, zList, nList);
  Th_Free(interp, zList);

  return TH_OK;
}

/*
** TH Syntax:
**
**   lindex list index
*/
static int lindex_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int iElem;
  int rc;

  char **azElem;
  int *anElem;
  int nCount;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "lindex list index");
  }

  if( TH_OK!=Th_ToInt(interp, argv[2], argl[2], &iElem) ){
    return TH_ERROR;
  }

  rc = Th_SplitList(interp, argv[1], argl[1], &azElem, &anElem, &nCount);
  if( rc==TH_OK ){
    if( iElem<nCount && iElem>=0 ){
      Th_SetResult(interp, azElem[iElem], anElem[iElem]);
    }else{
      Th_SetResult(interp, 0, 0);
    }
    Th_Free(interp, azElem);
  }

  return rc;
}

/*
** TH Syntax:
**
**   llength list
*/
static int llength_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int nElem;
  int rc;

  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "llength list");
  }

  rc = Th_SplitList(interp, argv[1], argl[1], 0, 0, &nElem);
  if( rc==TH_OK ){
    Th_SetResultInt(interp, nElem);
  }

  return rc;
}

/*
** TH Syntax:
**
**   lsearch list string
*/
static int lsearch_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int rc;
  char **azElem;
  int *anElem;
  int nCount;
  int i;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "lsearch list string");
  }

  rc = Th_SplitList(interp, argv[1], argl[1], &azElem, &anElem, &nCount);
  if( rc==TH_OK ){
    Th_SetResultInt(interp, -1);
    for(i=0; i<nCount; i++){
      if( anElem[i]==argl[2] && 0==memcmp(azElem[i], argv[2], argl[2]) ){
        Th_SetResultInt(interp, i);
        break;
      }
    }
    Th_Free(interp, azElem);
  }

  return rc;
}

/*
** TH Syntax:
**
**   set varname ?value?
*/
static int set_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  if( argc!=2 && argc!=3 ){
    return Th_WrongNumArgs(interp, "set varname ?value?");
  }

  if( argc==3 ){
    Th_SetVar(interp, argv[1], argl[1], argv[2], argl[2]);
  }
  return Th_GetVar(interp, argv[1], argl[1]);
}

/*
** When a new command is created using the built-in [proc] command, an
** instance of the following structure is allocated and populated. A
** pointer to the structure is passed as the context (second) argument
** to function proc_call1() when the new command is executed.
*/
typedef struct ProcDefn ProcDefn;
struct ProcDefn {
  int nParam;                /* Number of formal (non "args") parameters */
  char **azParam;            /* Parameter names */
  int *anParam;              /* Lengths of parameter names */
  char **azDefault;          /* Default values */
  int *anDefault;            /* Lengths of default values */
  int hasArgs;               /* True if there is an "args" parameter */
  char *zProgram;            /* Body of proc */
  int nProgram;              /* Number of bytes at zProgram */
  char *zUsage;              /* Usage message */
  int nUsage;                /* Number of bytes at zUsage */
};

/* This structure is used to temporarily store arguments passed to an
** invocation of a command created using [proc]. A pointer to an
** instance is passed as the second argument to the proc_call2() function.
*/
typedef struct ProcArgs ProcArgs;
struct ProcArgs {
  int argc;
  const char **argv;
  int *argl;
};

/*
** Each time a command created using [proc] is invoked, a new
** th1 stack frame is allocated (for the proc's local variables) and
** this function invoked.
**
** Argument pContext1 points to the associated ProcDefn structure.
** Argument pContext2  points to a ProcArgs structure that contains
** the arguments passed to this specific invocation of the proc.
*/
static int proc_call2(Th_Interp *interp, void *pContext1, void *pContext2){
  int i;
  ProcDefn *p = (ProcDefn *)pContext1;
  ProcArgs *pArgs = (ProcArgs *)pContext2;

  /* Check if there are the right number of arguments. If there are
  ** not, generate a usage message for the command.
  */
  if( (pArgs->argc>(p->nParam+1) && !p->hasArgs)
   || (pArgs->argc<=(p->nParam) && !p->azDefault[pArgs->argc-1])
  ){
    char *zUsage = 0;
    int nUsage = 0;
    Th_StringAppend(interp, &zUsage, &nUsage, pArgs->argv[0], pArgs->argl[0]);
    Th_StringAppend(interp, &zUsage, &nUsage, p->zUsage, p->nUsage);
    Th_StringAppend(interp, &zUsage, &nUsage, (const char *)"", 1);
    Th_WrongNumArgs(interp, zUsage);
    Th_Free(interp, zUsage);
    return TH_ERROR;
  }

  /* Populate the formal proc parameters. */
  for(i=0; i<p->nParam; i++){
    const char *zVal;
    int nVal;
    if( pArgs->argc>(i+1) ){
      zVal = pArgs->argv[i+1];
      nVal = pArgs->argl[i+1];
    }else{
      zVal = p->azDefault[i];
      nVal = p->anDefault[i];
    }
    Th_SetVar(interp, p->azParam[i], p->anParam[i], zVal, nVal);
  }

  /* Populate the "args" parameter, if it exists */
  if( p->hasArgs ){
    char *zArgs = 0;
    int nArgs = 0;
    for(i=p->nParam+1; i<pArgs->argc; i++){
      Th_ListAppend(interp, &zArgs, &nArgs, pArgs->argv[i], pArgs->argl[i]);
    }
    Th_SetVar(interp, (const char *)"args", -1, zArgs, nArgs);
    if(zArgs){
      Th_Free(interp, zArgs);
    }
  }

  Th_SetResult(interp, 0, 0);
  return Th_Eval(interp, 0, p->zProgram, p->nProgram);
}

/*
** This function is the command callback registered for all commands
** created using the [proc] command. The second argument, pContext,
** is a pointer to the associated ProcDefn structure.
*/
static int proc_call1(
  Th_Interp *interp,
  void *pContext,
  int argc,
  const char **argv,
  int *argl
){
  int rc;

  ProcDefn *p = (ProcDefn *)pContext;
  ProcArgs procargs;

  /* Call function proc_call2(), which will call Th_Eval() to evaluate
  ** the body of the [proc], in a new Th stack frame. This is so that
  ** the proc body has its own local variable context.
  */
  procargs.argc = argc;
  procargs.argv = argv;
  procargs.argl = argl;
  rc = Th_InFrame(interp, proc_call2, (void *)p, (void *)&procargs);

  if( rc==TH_RETURN ){
    rc = TH_OK;
  }
  if( rc==TH_RETURN2 ){
    rc = TH_RETURN;
  }
  return rc;
}

/*
** This function is registered as the delete callback for all commands
** created using the built-in [proc] command. It is called automatically
** when a command created using [proc] is deleted.
**
** It frees the ProcDefn structure allocated when the command was created.
*/
static void proc_del(Th_Interp *interp, void *pContext){
  ProcDefn *p = (ProcDefn *)pContext;
  Th_Free(interp, (void *)p->zUsage);
  Th_Free(interp, (void *)p);
}

/*
** TH Syntax:
**
**   proc name arglist code
*/
static int proc_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int rc;
  const char *zName;

  ProcDefn *p;
  int nByte;
  int i;
  char *zSpace;

  char **azParam;
  int *anParam;
  int nParam;

  char *zUsage = 0;                /* Build up a usage message here */
  int nUsage = 0;                  /* Number of bytes at zUsage */

  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "proc name arglist code");
  }
  if( Th_SplitList(interp, argv[2], argl[2], &azParam, &anParam, &nParam) ){
    return TH_ERROR;
  }

  /* Allocate the new ProcDefn structure. */
  nByte = sizeof(ProcDefn) +                        /* ProcDefn structure */
      (sizeof(char *) + sizeof(int)) * nParam +     /* azParam, anParam */
      (sizeof(char *) + sizeof(int)) * nParam +     /* azDefault, anDefault */
      argl[3] +                                     /* zProgram */
      argl[2];    /* Space for copies of parameter names and default values */
  p = (ProcDefn *)Th_Malloc(interp, nByte);

  /* If the last parameter in the parameter list is "args", then set the
  ** ProcDefn.hasArgs flag. The "args" parameter does not require an
  ** entry in the ProcDefn.azParam[] or ProcDefn.azDefault[] arrays.
  */
  if( nParam>0 ){
    if( anParam[nParam-1]==4 && 0==memcmp(azParam[nParam-1], "args", 4) ){
      p->hasArgs = 1;
      nParam--;
    }
  }

  p->nParam    = nParam;
  p->azParam   = (char **)&p[1];
  p->anParam   = (int *)&p->azParam[nParam];
  p->azDefault = (char **)&p->anParam[nParam];
  p->anDefault = (int *)&p->azDefault[nParam];
  p->zProgram = (char *)&p->anDefault[nParam];
  memcpy(p->zProgram, argv[3], argl[3]);
  p->nProgram = argl[3];
  zSpace = &p->zProgram[p->nProgram];

  for(i=0; i<nParam; i++){
    char **az;
    int *an;
    int n;
    if( Th_SplitList(interp, azParam[i], anParam[i], &az, &an, &n) ){
      goto error_out;
    }
    if( n<1 || n>2 ){
      const char expected[] = "expected parameter, got \"";
      Th_ErrorMessage(interp, expected, azParam[i], anParam[i]);
      Th_Free(interp, az);
      goto error_out;
    }
    p->anParam[i] = an[0];
    p->azParam[i] = zSpace;
    memcpy(zSpace, az[0], an[0]);
    zSpace += an[0];
    if( n==2 ){
      p->anDefault[i] = an[1];
      p->azDefault[i] = zSpace;
      memcpy(zSpace, az[1], an[1]);
      zSpace += an[1];
    }

    Th_StringAppend(interp, &zUsage, &nUsage, (const char *)" ", 1);
    if( n==2 ){
      Th_StringAppend(interp, &zUsage, &nUsage, (const char *)"?", 1);
      Th_StringAppend(interp, &zUsage, &nUsage, az[0], an[0]);
      Th_StringAppend(interp, &zUsage, &nUsage, (const char *)"?", 1);
    }else{
      Th_StringAppend(interp, &zUsage, &nUsage, az[0], an[0]);
    }

    Th_Free(interp, az);
  }
  assert( zSpace-(char *)p<=nByte );

  /* If there is an "args" parameter, append it to the end of the usage
  ** message. Set ProcDefn.zUsage to point at the usage message. It will
  ** be freed along with the rest of the proc-definition by proc_del().
  */
  if( p->hasArgs ){
    Th_StringAppend(interp, &zUsage, &nUsage, (const char *)" ?args...?", -1);
  }
  p->zUsage = zUsage;
  p->nUsage = nUsage;

  /* Register the new command with the th1 interpreter. */
  zName = argv[1];
  rc = Th_CreateCommand(interp, zName, proc_call1, (void *)p, proc_del);
  if( rc==TH_OK ){
    Th_SetResult(interp, 0, 0);
  }

  Th_Free(interp, azParam);
  return TH_OK;

 error_out:
  Th_Free(interp, azParam);
  Th_Free(interp, zUsage);
  return TH_ERROR;
}

/*
** TH Syntax:
**
**   rename oldcmd newcmd
*/
static int rename_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "rename oldcmd newcmd");
  }
  return Th_RenameCommand(interp, argv[1], argl[1], argv[2], argl[2]);
}

/*
** TH Syntax:
**
**   break    ?value...?
**   continue ?value...?
**   ok       ?value...?
**   error    ?value...?
*/
static int simple_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  if( argc!=1 && argc!=2 ){
    return Th_WrongNumArgs(interp, "return ?value?");
  }
  if( argc==2 ){
    Th_SetResult(interp, argv[1], argl[1]);
  }
  return FOSSIL_PTR_TO_INT(ctx);
}

/*
** TH Syntax:
**
**   return ?-code code? ?value?
*/
static int return_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int iCode = TH_RETURN;
  if( argc<1 || argc>4 ){
    return Th_WrongNumArgs(interp, "return ?-code code? ?value?");
  }
  if( argc>2 ){
    int rc = Th_ToInt(interp, argv[2], argl[2], &iCode);
    if( rc!=TH_OK ){
      return rc;
    }
  }
  if( argc==2 || argc==4 ){
    Th_SetResult(interp, argv[argc-1], argl[argc-1]);
  }
  return iCode;
}

/*
** TH Syntax:
**
**   string compare STRING1 STRING2
*/
static int string_compare_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  const char *zRight; int nRight;
  const char *zLeft; int nLeft;

  int i;
  int iRes = 0;

  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "string compare str1 str2");
  }

  zLeft = argv[2];
  nLeft = argl[2];
  zRight = argv[3];
  nRight = argl[3];

  for(i=0; iRes==0 && i<nLeft && i<nRight; i++){
    iRes = zLeft[i]-zRight[i];
  }
  if( iRes==0 ){
    iRes = nLeft-nRight;
  }

  if( iRes<0 ) iRes = -1;
  if( iRes>0 ) iRes = 1;

  return Th_SetResultInt(interp, iRes);
}

/*
** TH Syntax:
**
**   string first NEEDLE HAYSTACK
*/
static int string_first_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int nNeedle;
  int nHaystack;
  int iRes = -1;

  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "string first needle haystack");
  }

  nNeedle = argl[2];
  nHaystack = argl[3];

  if( nNeedle && nHaystack && nNeedle<=nHaystack ){
    const char *zNeedle = argv[2];
    const char *zHaystack = argv[3];
    int i;

    for(i=0; i<=(nHaystack-nNeedle); i++){
      if( 0==memcmp(zNeedle, &zHaystack[i], nNeedle) ){
        iRes = i;
        break;
      }
    }
  }

  return Th_SetResultInt(interp, iRes);
}

/*
** TH Syntax:
**
**   string index STRING INDEX
*/
static int string_index_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int iIndex;

  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "string index string index");
  }

  if( argl[3]==3 && 0==memcmp("end", argv[3], 3) ){
    iIndex = argl[2]-1;
  }else if( Th_ToInt(interp, argv[3], argl[3], &iIndex) ){
    Th_ErrorMessage(
        interp, "Expected \"end\" or integer, got:", argv[3], argl[3]);
    return TH_ERROR;
  }

  if( iIndex>=0 && iIndex<argl[2] ){
    return Th_SetResult(interp, &argv[2][iIndex], 1);
  }else{
    return Th_SetResult(interp, 0, 0);
  }
}

/*
** TH Syntax:
**
**   string is CLASS STRING
*/
static int string_is_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "string is class string");
  }
  if( argl[2]==5 && 0==memcmp(argv[2], "alnum", 5) ){
    int i;
    int iRes = 1;

    for(i=0; i<argl[3]; i++){
      if( !th_isalnum(argv[3][i]) ){
        iRes = 0;
      }
    }

    return Th_SetResultInt(interp, iRes);
  }else if( argl[2]==6 && 0==memcmp(argv[2], "double", 6) ){
    double fVal;
    if( Th_ToDouble(interp, argv[3], argl[3], &fVal)==TH_OK ){
      return Th_SetResultInt(interp, 1);
    }
    return Th_SetResultInt(interp, 0);
  }else if( argl[2]==7 && 0==memcmp(argv[2], "integer", 7) ){
    int iVal;
    if( Th_ToInt(interp, argv[3], argl[3], &iVal)==TH_OK ){
      return Th_SetResultInt(interp, 1);
    }
    return Th_SetResultInt(interp, 0);
  }else if( argl[2]==4 && 0==memcmp(argv[2], "list", 4) ){
    if( Th_SplitList(interp, argv[3], argl[3], 0, 0, 0)==TH_OK ){
      return Th_SetResultInt(interp, 1);
    }
    return Th_SetResultInt(interp, 0);
  }else{
    Th_ErrorMessage(interp,
        "Expected alnum, double, integer, or list, got:", argv[2], argl[2]);
    return TH_ERROR;
  }
}

/*
** TH Syntax:
**
**   string last NEEDLE HAYSTACK
*/
static int string_last_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int nNeedle;
  int nHaystack;
  int iRes = -1;

  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "string last needle haystack");
  }

  nNeedle = argl[2];
  nHaystack = argl[3];

  if( nNeedle && nHaystack && nNeedle<=nHaystack ){
    const char *zNeedle = argv[2];
    const char *zHaystack = argv[3];
    int i;

    for(i=nHaystack-nNeedle; i>=0; i--){
      if( 0==memcmp(zNeedle, &zHaystack[i], nNeedle) ){
        iRes = i;
        break;
      }
    }
  }

  return Th_SetResultInt(interp, iRes);
}

/*
** TH Syntax:
**
**   string length STRING
*/
static int string_length_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "string length string");
  }
  return Th_SetResultInt(interp, argl[2]);
}

/*
** TH Syntax:
**
**   string range STRING FIRST LAST
*/
static int string_range_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int iStart;
  int iEnd;

  if( argc!=5 ){
    return Th_WrongNumArgs(interp, "string range string first last");
  }

  if( argl[4]==3 && 0==memcmp("end", argv[4], 3) ){
    iEnd = argl[2];
  }else if( Th_ToInt(interp, argv[4], argl[4], &iEnd) ){
    Th_ErrorMessage(
        interp, "Expected \"end\" or integer, got:", argv[4], argl[4]);
    return TH_ERROR;
  }
  if( Th_ToInt(interp, argv[3], argl[3], &iStart) ){
    return TH_ERROR;
  }

  if( iStart<0 ) iStart = 0;
  if( iEnd>=argl[2] ) iEnd = argl[2]-1;
  if( iStart>iEnd ) iEnd = iStart-1;

  return Th_SetResult(interp, &argv[2][iStart], iEnd-iStart+1);
}

/*
** TH Syntax:
**
**   string repeat STRING COUNT
*/
static int string_repeat_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int n;
  int i;
  int nByte;
  char *zByte;

  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "string repeat string n");
  }
  if( Th_ToInt(interp, argv[3], argl[3], &n) ){
    return TH_ERROR;
  }

  nByte = argl[2] * n;
  zByte = Th_Malloc(interp, nByte+1);
  for(i=0; i<nByte; i+=argl[2]){
    memcpy(&zByte[i], argv[2], argl[2]);
  }

  Th_SetResult(interp, zByte, nByte);
  Th_Free(interp, zByte);
  return TH_OK;
}

/*
** TH Syntax:
**
**   string trim STRING
**   string trimleft STRING
**   string trimright STRING
*/
static int string_trim_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int n;
  const char *z;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "string trim string");
  }
  z = argv[2];
  n = argl[2];
  if( argl[1]<5 || argv[1][4]=='l' ){
    while( n && th_isspace(z[0]) ){ z++; n--; }
  }
  if( argl[1]<5 || argv[1][4]=='r' ){
    while( n && th_isspace(z[n-1]) ){ n--; }
  }
  Th_SetResult(interp, z, n);
  return TH_OK;
}

/*
** TH Syntax:
**
**   info exists VARNAME
*/
static int info_exists_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int rc;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "info exists var");
  }
  rc = Th_ExistsVar(interp, argv[2], argl[2]);
  Th_SetResultInt(interp, rc);
  return TH_OK;
}

/*
** TH Syntax:
**
**   info commands
*/
static int info_commands_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int rc;
  char *zElem = 0;
  int nElem = 0;

  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "info commands");
  }
  rc = Th_ListAppendCommands(interp, &zElem, &nElem);
  if( rc!=TH_OK ){
    return rc;
  }
  Th_SetResult(interp, zElem, nElem);
  if( zElem ) Th_Free(interp, zElem);
  return TH_OK;
}

/*
** TH Syntax:
**
**   info vars
*/
static int info_vars_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int rc;
  char *zElem = 0;
  int nElem = 0;

  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "info vars");
  }
  rc = Th_ListAppendVariables(interp, &zElem, &nElem);
  if( rc!=TH_OK ){
    return rc;
  }
  Th_SetResult(interp, zElem, nElem);
  if( zElem ) Th_Free(interp, zElem);
  return TH_OK;
}

/*
** TH Syntax:
**
**   array exists VARNAME
*/
static int array_exists_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int rc;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "array exists var");
  }
  rc = Th_ExistsArrayVar(interp, argv[2], argl[2]);
  Th_SetResultInt(interp, rc);
  return TH_OK;
}

/*
** TH Syntax:
**
**   array names VARNAME
*/
static int array_names_command(
  Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl
){
  int rc;
  char *zElem = 0;
  int nElem = 0;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "array names varname");
  }
  rc = Th_ListAppendArray(interp, argv[2], argl[2], &zElem, &nElem);
  if( rc!=TH_OK ){
    return rc;
  }
  Th_SetResult(interp, zElem, nElem);
  if( zElem ) Th_Free(interp, zElem);
  return TH_OK;
}

/*
** TH Syntax:
**
**   unset VARNAME
*/
static int unset_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "unset var");
  }
  return Th_UnsetVar(interp, argv[1], argl[1]);
}

int Th_CallSubCommand(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl,
  const Th_SubCommand *aSub
){
  if( argc>1 ){
    int i;
    for(i=0; aSub[i].zName; i++){
      const char *zName = aSub[i].zName;
      if( th_strlen(zName)==argl[1] && 0==memcmp(zName, argv[1], argl[1]) ){
        return aSub[i].xProc(interp, ctx, argc, argv, argl);
      }
    }
  }
  if(argc<2){
    Th_ErrorMessage(interp, "Expected sub-command for", argv[0], argl[0]);
  }else{
    Th_ErrorMessage(interp, "Expected sub-command, got:", argv[1], argl[1]);
  }
  return TH_ERROR;
}

/*
** TH Syntax:
**
**   string compare   STR1 STR2
**   string first     NEEDLE HAYSTACK ?STARTINDEX?
**   string index     STRING INDEX
**   string is        CLASS STRING
**   string last      NEEDLE HAYSTACK ?STARTINDEX?
**   string length    STRING
**   string range     STRING FIRST LAST
**   string repeat    STRING COUNT
**   string trim      STRING
**   string trimleft  STRING
**   string trimright STRING
*/
static int string_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  static const Th_SubCommand aSub[] = {
    { "compare",   string_compare_command },
    { "first",     string_first_command },
    { "index",     string_index_command },
    { "is",        string_is_command },
    { "last",      string_last_command },
    { "length",    string_length_command },
    { "range",     string_range_command },
    { "repeat",    string_repeat_command },
    { "trim",      string_trim_command },
    { "trimleft",  string_trim_command },
    { "trimright", string_trim_command },
    { 0, 0 }
  };
  return Th_CallSubCommand(interp, ctx, argc, argv, argl, aSub);
}

/*
** TH Syntax:
**
**   info commands
**   info exists VARNAME
**   info vars
*/
static int info_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  static const Th_SubCommand aSub[] = {
    { "commands", info_commands_command },
    { "exists",   info_exists_command },
    { "vars",     info_vars_command },
    { 0, 0 }
  };
  return Th_CallSubCommand(interp, ctx, argc, argv, argl, aSub);
}

/*
** TH Syntax:
**
**   array exists VARNAME
**   array names VARNAME
*/
static int array_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  static const Th_SubCommand aSub[] = {
    { "exists", array_exists_command },
    { "names",  array_names_command },
    { 0, 0 }
  };
  return Th_CallSubCommand(interp, ctx, argc, argv, argl, aSub);
}

/*
** Convert the script level frame specification (used by the commands
** [uplevel] and [upvar]) in (zFrame, nFrame) to an integer frame as
** used by Th_LinkVar() and Th_Eval(). If successful, write the integer
** frame level to *piFrame and return TH_OK. Otherwise, return TH_ERROR
** and leave an error message in the interpreter result.
*/
static int thToFrame(
  Th_Interp *interp,
  const char *zFrame,
  int nFrame,
  int *piFrame
){
  int iFrame;
  if( th_isdigit(zFrame[0]) ){
    int rc = Th_ToInt(interp, zFrame, nFrame, &iFrame);
    if( rc!=TH_OK ) return rc;
    iFrame = iFrame * -1;
  }else if( zFrame[0]=='#' ){
    int rc = Th_ToInt(interp, &zFrame[1], nFrame-1, &iFrame);
    if( rc!=TH_OK ) return rc;
    iFrame = iFrame + 1;
  }else{
    return TH_ERROR;
  }
  *piFrame = iFrame;
  return TH_OK;
}

/*
** TH Syntax:
**
**   uplevel ?LEVEL? SCRIPT
*/
static int uplevel_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int iFrame = -1;

  if( argc!=2 && argc!=3 ){
    return Th_WrongNumArgs(interp, "uplevel ?level? script...");
  }
  if( argc==3 && TH_OK!=thToFrame(interp, argv[1], argl[1], &iFrame) ){
    return TH_ERROR;
  }
  return Th_Eval(interp, iFrame, argv[argc-1], -1);
}

/*
** TH Syntax:
**
**   upvar ?FRAME? OTHERVAR MYVAR ?OTHERVAR MYVAR ...?
*/
static int upvar_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int iVar = 1;
  int iFrame = -1;
  int rc = TH_OK;
  int i;

  if( TH_OK==thToFrame(0, argv[1], argl[1], &iFrame) ){
    iVar++;
  }
  if( argc==iVar || (argc-iVar)%2 ){
    return Th_WrongNumArgs(interp,
        "upvar frame othervar myvar ?othervar myvar...?");
  }
  for(i=iVar; rc==TH_OK && i<argc; i=i+2){
    rc = Th_LinkVar(interp, argv[i+1], argl[i+1], iFrame, argv[i], argl[i]);
  }
  return rc;
}

/*
** TH Syntax:
**
**   breakpoint ARGS
**
** This command does nothing at all. Its purpose in life is to serve
** as a point for setting breakpoints in a debugger.
*/
static int breakpoint_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  int cnt = 0;
  cnt++;
  return TH_OK;
}

/*
** Register the built-in th1 language commands with interpreter interp.
** Usually this is called soon after interpreter creation.
*/
int th_register_language(Th_Interp *interp){
  /* Array of built-in commands. */
  struct _Command {
    const char *zName;
    Th_CommandProc xProc;
    void *pContext;
  } aCommand[] = {
    {"array",    array_command,   0},
    {"catch",    catch_command,   0},
    {"expr",     expr_command,    0},
    {"for",      for_command,     0},
    {"if",       if_command,      0},
    {"info",     info_command,    0},
    {"lindex",   lindex_command,  0},
    {"list",     list_command,    0},
    {"llength",  llength_command, 0},
    {"lsearch",  lsearch_command, 0},
    {"proc",     proc_command,    0},
    {"rename",   rename_command,  0},
    {"set",      set_command,     0},
    {"string",   string_command,  0},
    {"unset",    unset_command,   0},
    {"uplevel",  uplevel_command, 0},
    {"upvar",    upvar_command,   0},

    {"breakpoint", breakpoint_command, 0},

    {"return",   return_command, 0},
    {"break",    simple_command, (void *)TH_BREAK},
    {"continue", simple_command, (void *)TH_CONTINUE},
    {"error",    simple_command, (void *)TH_ERROR},

    {0, 0, 0}
  };
  size_t i;

  /* Add the language commands. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    void *ctx;
    if ( !aCommand[i].zName || !aCommand[i].xProc ) continue;
    ctx = aCommand[i].pContext;
    Th_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc, ctx, 0);
  }

  return TH_OK;
}
