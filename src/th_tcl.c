/*
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
*******************************************************************************
** This file contains code used to bridge the TH1 and Tcl scripting languages.
*/

#include "config.h"

#ifdef FOSSIL_ENABLE_TCL

#include "jim.h"
#include "tcl.h"

/*
** Are we being compiled against Tcl 8.6 or higher?
 */
#if (TCL_MAJOR_VERSION > 8) || \
    ((TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 6))
/*
** Workaround NRE-specific issue in Tcl_EvalObjCmd (SF bug #3399564) by using
** Tcl_EvalObjv instead of invoking the objProc directly.
 */
#define USE_TCL_EVALOBJV   1
#endif

/*
** These macros are designed to reduce the redundant code required to marshal
** arguments from TH1 to Tcl.
 */
#define USE_ARGV_TO_OBJV() \
  int objc;                \
  Tcl_Obj **objv;          \
  int i;

#define COPY_ARGV_TO_OBJV()                                         \
  objc = argc-1;                                                    \
  objv = (Tcl_Obj **)ckalloc((unsigned)(objc * sizeof(Tcl_Obj *))); \
  for(i=1; i<argc; i++){                                            \
    objv[i-1] = Tcl_NewStringObj(Jim_String(argv[i]), Jim_Length(argv[i]));                 \
    Tcl_IncrRefCount(objv[i-1]);                                    \
  }

#define FREE_ARGV_TO_OBJV()      \
  for(i=1; i<argc; i++){         \
    Tcl_DecrRefCount(objv[i-1]); \
  }                              \
  ckfree((char *)objv);

/*
** Fetch the Tcl interpreter from the specified void pointer, cast to a Tcl
** context.
 */
#define GET_CTX_TCL_INTERP(ctx) \
  ((struct TclContext *)(ctx))->interp

/*
** Creates and initializes a Tcl interpreter for use with the specified TH1
** interpreter.  Stores the created Tcl interpreter in the Tcl context supplied
** by the caller.  This must be declared here because quite a few functions in
** this file need to use it before it can be defined.
 */
static int createTclInterp(Jim_Interp *interp, void *pContext);

/*
** Returns the Tcl interpreter result as a string with the associated length.
** If the Tcl interpreter or the Tcl result are NULL, the length will be 0.
** If the length pointer is NULL, the length will not be stored.
 */
static char *getTclResult(
  Tcl_Interp *pInterp,
  int *pN
){
  Tcl_Obj *resultPtr;
  if( !pInterp ){ /* This should not happen. */
    if( pN ) *pN = 0;
    return 0;
  }
  resultPtr = Tcl_GetObjResult(pInterp);
  if( !resultPtr ){ /* This should not happen either? */
    if( pN ) *pN = 0;
    return 0;
  }
  return Tcl_GetStringFromObj(resultPtr, pN);
}

/*
** Tcl context information used by TH1.  This structure definition has been
** copied from and should be kept in sync with the one in "main.c".
*/
struct TclContext {
  int argc;
  char **argv;
  Tcl_Interp *interp;
};

/*
** Syntax:
**
**   tclEval arg ?arg ...?
*/
static int tclEval_command(Jim_Interp *interp, int argc, Jim_Obj *const *argv
){
  Tcl_Interp *tclInterp;
  Tcl_Obj *objPtr;
  int rc;
  int nResult;
  const char *zResult;
  void *ctx = Jim_CmdPrivData(interp);

  if ( createTclInterp(interp, ctx)!=JIM_OK ){
    return JIM_ERR;
  }
  if( argc<2 ){
    Jim_WrongNumArgs(interp, 1, argv, "arg ?arg ...?");
    return JIM_ERR;
  }
  tclInterp = GET_CTX_TCL_INTERP(ctx);
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Jim_SetResultString(interp, "invalid Tcl interpreter", -1);
    return JIM_ERR;
  }
  Tcl_Preserve((ClientData)tclInterp);
  if( argc==2 ){
    objPtr = Tcl_NewStringObj(Jim_String(argv[1]), Jim_Length(argv[1]));
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_EvalObjEx(tclInterp, objPtr, 0);
    Tcl_DecrRefCount(objPtr);
  }else{
    USE_ARGV_TO_OBJV();
    COPY_ARGV_TO_OBJV();
    objPtr = Tcl_ConcatObj(objc, objv);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_EvalObjEx(tclInterp, objPtr, 0);
    Tcl_DecrRefCount(objPtr);
    FREE_ARGV_TO_OBJV();
  }
  zResult = getTclResult(tclInterp, &nResult);
  Jim_SetResultString(interp, zResult, nResult);
  Tcl_Release((ClientData)tclInterp);
  return rc;
}

/*
** Syntax:
**
**   tclExpr arg ?arg ...?
*/
static int tclExpr_command(Jim_Interp *interp, int argc, Jim_Obj *const *argv
){
  Tcl_Interp *tclInterp;
  Tcl_Obj *objPtr;
  Tcl_Obj *resultObjPtr;
  int rc;
  int nResult;
  const char *zResult;
  void *ctx = Jim_CmdPrivData(interp);

  if ( createTclInterp(interp, ctx)!=JIM_OK ){
    return JIM_ERR;
  }
  if( argc<2 ){
    Jim_WrongNumArgs(interp, 1, argv, "arg ?arg ...?");
    return JIM_ERR;
  }
  tclInterp = GET_CTX_TCL_INTERP(ctx);
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Jim_SetResultString(interp, "invalid Tcl interpreter", -1);
    return JIM_ERR;
  }
  Tcl_Preserve((ClientData)tclInterp);
  if( argc==2 ){
    objPtr = Tcl_NewStringObj(Jim_String(argv[1]), Jim_Length(argv[1]));
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_ExprObj(tclInterp, objPtr, &resultObjPtr);
    Tcl_DecrRefCount(objPtr);
  }else{
    USE_ARGV_TO_OBJV();
    COPY_ARGV_TO_OBJV();
    objPtr = Tcl_ConcatObj(objc, objv);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_ExprObj(tclInterp, objPtr, &resultObjPtr);
    Tcl_DecrRefCount(objPtr);
    FREE_ARGV_TO_OBJV();
  }
  if( rc==TCL_OK ){
    zResult = Tcl_GetStringFromObj(resultObjPtr, &nResult);
  }else{
    zResult = getTclResult(tclInterp, &nResult);
  }
  Jim_SetResultString(interp, zResult, nResult);
  if( rc==TCL_OK ) Tcl_DecrRefCount(resultObjPtr);
  Tcl_Release((ClientData)tclInterp);
  return rc;
}

/*
** Syntax:
**
**   tclInvoke command ?arg ...?
*/
static int tclInvoke_command(Jim_Interp *interp, int argc, Jim_Obj *const *argv
){
  Tcl_Interp *tclInterp;
#ifndef USE_TCL_EVALOBJV
  Tcl_Command command;
  Tcl_CmdInfo cmdInfo;
#endif
  int rc;
  int nResult;
  const char *zResult;
  void *ctx = Jim_CmdPrivData(interp);
#ifndef USE_TCL_EVALOBJV
  Tcl_Obj *objPtr;
#endif
  USE_ARGV_TO_OBJV();

  if ( createTclInterp(interp, ctx)!=JIM_OK ){
    return JIM_ERR;
  }
  if( argc<2 ){
    Jim_WrongNumArgs(interp, 1, argv, "command ?arg ...?");
    return JIM_ERR;
  }
  tclInterp = GET_CTX_TCL_INTERP(ctx);
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Jim_SetResultString(interp, "invalid Tcl interpreter", -1);
    return JIM_ERR;
  }
  Tcl_Preserve((ClientData)tclInterp);
#ifndef USE_TCL_EVALOBJV
  objPtr = Tcl_NewStringObj(Jim_String(argv[1]), Jim_Length(argv[1]));
  Tcl_IncrRefCount(objPtr);
  command = Tcl_GetCommandFromObj(tclInterp, objPtr);
  if( !command || Tcl_GetCommandInfoFromToken(command,&cmdInfo)==0 ){
    Jim_SetResultFormatted(interp, "Tcl command not found: %#s", argv[1]);
    Tcl_DecrRefCount(objPtr);
    Tcl_Release((ClientData)tclInterp);
    return JIM_ERR;
  }
  if( !cmdInfo.objProc ){
    Jim_SetResultFormatted(interp, "Cannot invoke command not found: %#s", argv[1]);
    Tcl_DecrRefCount(objPtr);
    Tcl_Release((ClientData)tclInterp);
    return JIM_ERR;
  }
  Tcl_DecrRefCount(objPtr);
#endif
  COPY_ARGV_TO_OBJV();
#ifdef USE_TCL_EVALOBJV
  rc = Tcl_EvalObjv(tclInterp, objc, objv, 0);
#else
  Tcl_ResetResult(tclInterp);
  rc = cmdInfo.objProc(cmdInfo.objClientData, tclInterp, objc, objv);
#endif
  FREE_ARGV_TO_OBJV();
  zResult = getTclResult(tclInterp, &nResult);
  Jim_SetResultString(interp, zResult, nResult);
  Tcl_Release((ClientData)tclInterp);
  return rc;
}

/*
** Syntax:
**
**   th1Eval arg
*/
static int Th1EvalObjCmd(
  ClientData clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  Jim_Interp *th1Interp;
  int nArg;
  const char *arg;
  int rc;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "arg");
    return TCL_ERROR;
  }
  th1Interp = (Jim_Interp *)clientData;
  if( !th1Interp ){
    Tcl_AppendResult(interp, "invalid TH1 interpreter", NULL);
    return TCL_ERROR;
  }
  arg = Tcl_GetStringFromObj(objv[1], &nArg);
  rc = Jim_Eval(th1Interp, arg);
  arg = Jim_String(Jim_GetResult(th1Interp));
  Tcl_SetObjResult(interp, Tcl_NewStringObj(arg, -1));
  return rc;
}

/*
** Syntax:
**
**   th1Expr arg
*/
static int Th1ExprObjCmd(
  ClientData clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  Jim_Interp *th1Interp;
  int nArg;
  const char *arg;
  int rc;
  Jim_Obj *exprResultObj;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "arg");
    return TCL_ERROR;
  }
  th1Interp = (Jim_Interp *)clientData;
  if( !th1Interp ){
    Tcl_AppendResult(interp, "invalid TH1 interpreter", NULL);
    return TCL_ERROR;
  }

  arg = Tcl_GetStringFromObj(objv[1], &nArg);
  rc = Jim_EvalExpression(th1Interp, Jim_NewStringObj(th1Interp, arg, -1), &exprResultObj);
  if (rc == JIM_OK) {
    arg = Jim_String(exprResultObj);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(arg, -1));
  }

  return rc;
}

/*
** Array of Tcl integration commands.  Used when adding or removing the Tcl
** integration commands from TH1.
*/
static struct _Command {
  const char *zName;
  Jim_CmdProc xProc;
} aCommand[] = {
  {"tclEval",   tclEval_command  },
  {"tclExpr",   tclExpr_command  },
  {"tclInvoke", tclInvoke_command},
};

/*
** Called if the Tcl interpreter is deleted.  Removes the Tcl integration
** commands from the TH1 interpreter.
 */
static void Th1DeleteProc(
  ClientData clientData,
  Tcl_Interp *interp
){
  int i;
  Jim_Interp *th1Interp = (Jim_Interp *)clientData;
  if( !th1Interp ) return;
  /* Remove the Tcl integration commands. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    Jim_DeleteCommand(th1Interp, aCommand[i].zName);
  }
}

/*
** Creates and initializes a Tcl interpreter for use with the specified TH1
** interpreter.  Stores the created Tcl interpreter in the Tcl context supplied
** by the caller.
 */
static int createTclInterp(
  Jim_Interp *interp,
  void *pContext
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  Tcl_Interp *tclInterp;

  if ( !tclContext ){
    Jim_SetResultString(interp, "Invalid Tcl context", -1);
    return JIM_ERR;
  }
  if ( tclContext->interp ){
    return JIM_OK;
  }
  if ( tclContext->argc>0 && tclContext->argv ) {
    Tcl_FindExecutable(tclContext->argv[0]);
  }
  tclInterp = tclContext->interp = Tcl_CreateInterp();
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Jim_SetResultString(interp, "Could not create Tcl interpreter", -1);
    return JIM_ERR;
  }
  if( Tcl_Init(tclInterp)!=TCL_OK ){
    Jim_SetResultFormatted(interp, "Tcl initialization error: %s", Tcl_GetStringResult(tclInterp));
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0;
    return JIM_ERR;
  }
  /* Add the TH1 integration commands to Tcl. */
  Tcl_CallWhenDeleted(tclInterp, Th1DeleteProc, interp);
  Tcl_CreateObjCommand(tclInterp, "th1Eval", Th1EvalObjCmd, interp, NULL);
  Tcl_CreateObjCommand(tclInterp, "th1Expr", Th1ExprObjCmd, interp, NULL);
  return JIM_OK;
}

/*
** Register the Tcl language commands with interpreter interp.
** Usually this is called soon after interpreter creation.
*/
int th_register_tcl(
  Jim_Interp *interp,
  void *pContext
){
  int i;
  /* Add the Tcl integration commands to TH1. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    /* Use Tcl interpreter for context? */
    Jim_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc, pContext, NULL);
  }
  return JIM_OK;
}

#endif /* FOSSIL_ENABLE_TCL */
