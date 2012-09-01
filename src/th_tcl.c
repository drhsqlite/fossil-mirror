/*
** Copyright (c) 2011 D. Richard Hipp
** Copyright (c) 2011 Joe Mistachkin
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code used to bridge the TH1 and Tcl scripting languages.
*/
#include "config.h"

#ifdef FOSSIL_ENABLE_TCL

#include "th.h"
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
    objv[i-1] = Tcl_NewStringObj(argv[i], argl[i]);                 \
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
static int createTclInterp(Th_Interp *interp, void *pContext);

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
static int tclEval_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Tcl_Interp *tclInterp;
  Tcl_Obj *objPtr;
  int rc;
  int nResult;
  const char *zResult;

  if ( createTclInterp(interp, ctx)!=TH_OK ){
    return TH_ERROR;
  }
  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclEval arg ?arg ...?");
  }
  tclInterp = GET_CTX_TCL_INTERP(ctx);
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  Tcl_Preserve((ClientData)tclInterp);
  if( argc==2 ){
    objPtr = Tcl_NewStringObj(argv[1], argl[1]);
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
  Th_SetResult(interp, zResult, nResult);
  Tcl_Release((ClientData)tclInterp);
  return rc;
}

/*
** Syntax:
**
**   tclExpr arg ?arg ...?
*/
static int tclExpr_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Tcl_Interp *tclInterp;
  Tcl_Obj *objPtr;
  Tcl_Obj *resultObjPtr;
  int rc;
  int nResult;
  const char *zResult;

  if ( createTclInterp(interp, ctx)!=TH_OK ){
    return TH_ERROR;
  }
  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclExpr arg ?arg ...?");
  }
  tclInterp = GET_CTX_TCL_INTERP(ctx);
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  Tcl_Preserve((ClientData)tclInterp);
  if( argc==2 ){
    objPtr = Tcl_NewStringObj(argv[1], argl[1]);
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
  Th_SetResult(interp, zResult, nResult);
  if( rc==TCL_OK ) Tcl_DecrRefCount(resultObjPtr);
  Tcl_Release((ClientData)tclInterp);
  return rc;
}

/*
** Syntax:
**
**   tclInvoke command ?arg ...?
*/
static int tclInvoke_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Tcl_Interp *tclInterp;
#ifndef USE_TCL_EVALOBJV
  Tcl_Command command;
  Tcl_CmdInfo cmdInfo;
#endif
  int rc;
  int nResult;
  const char *zResult;
#ifndef USE_TCL_EVALOBJV
  Tcl_Obj *objPtr;
#endif
  USE_ARGV_TO_OBJV();

  if ( createTclInterp(interp, ctx)!=TH_OK ){
    return TH_ERROR;
  }
  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclInvoke command ?arg ...?");
  }
  tclInterp = GET_CTX_TCL_INTERP(ctx);
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  Tcl_Preserve((ClientData)tclInterp);
#ifndef USE_TCL_EVALOBJV
  objPtr = Tcl_NewStringObj(argv[1], argl[1]);
  Tcl_IncrRefCount(objPtr);
  command = Tcl_GetCommandFromObj(tclInterp, objPtr);
  if( !command || Tcl_GetCommandInfoFromToken(command,&cmdInfo)==0 ){
    Th_ErrorMessage(interp, "Tcl command not found:", argv[1], argl[1]);
    Tcl_DecrRefCount(objPtr);
    Tcl_Release((ClientData)tclInterp);
    return TH_ERROR;
  }
  if( !cmdInfo.objProc ){
    Th_ErrorMessage(interp, "Cannot invoke Tcl command:", argv[1], argl[1]);
    Tcl_DecrRefCount(objPtr);
    Tcl_Release((ClientData)tclInterp);
    return TH_ERROR;
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
  Th_SetResult(interp, zResult, nResult);
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
  Th_Interp *th1Interp;
  int nArg;
  const char *arg;
  int rc;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "arg");
    return TCL_ERROR;
  }
  th1Interp = (Th_Interp *)clientData;
  if( !th1Interp ){
    Tcl_AppendResult(interp, "invalid TH1 interpreter", NULL);
    return TCL_ERROR;
  }
  arg = Tcl_GetStringFromObj(objv[1], &nArg);
  rc = Th_Eval(th1Interp, 0, arg, nArg);
  arg = Th_GetResult(th1Interp, &nArg);
  Tcl_SetObjResult(interp, Tcl_NewStringObj(arg, nArg));
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
  Th_Interp *th1Interp;
  int nArg;
  const char *arg;
  int rc;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "arg");
    return TCL_ERROR;
  }
  th1Interp = (Th_Interp *)clientData;
  if( !th1Interp ){
    Tcl_AppendResult(interp, "invalid TH1 interpreter", NULL);
    return TCL_ERROR;
  }
  arg = Tcl_GetStringFromObj(objv[1], &nArg);
  rc = Th_Expr(th1Interp, arg, nArg);
  arg = Th_GetResult(th1Interp, &nArg);
  Tcl_SetObjResult(interp, Tcl_NewStringObj(arg, nArg));
  return rc;
}

/*
** Array of Tcl integration commands.  Used when adding or removing the Tcl
** integration commands from TH1.
*/
static struct _Command {
  const char *zName;
  Th_CommandProc xProc;
  void *pContext;
} aCommand[] = {
  {"tclEval",   tclEval_command,   0},
  {"tclExpr",   tclExpr_command,   0},
  {"tclInvoke", tclInvoke_command, 0},
  {0, 0, 0}
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
  Th_Interp *th1Interp = (Th_Interp *)clientData;
  if( !th1Interp ) return;
  /* Remove the Tcl integration commands. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    Th_RenameCommand(th1Interp, aCommand[i].zName, -1, NULL, 0);
  }
}

/*
** Sets the "argv0", "argc", and "argv" script variables in the Tcl interpreter
** based on the supplied command line arguments.
 */
static int setTclArguments(
  Tcl_Interp *pInterp,
  int argc,
  char **argv
){
  Tcl_Obj *objPtr;
  Tcl_Obj *resultObjPtr;
  Tcl_Obj *listPtr;
  int rc = TCL_OK;
  if( argc<=0 || !argv ){
    return TCL_OK;
  }
  objPtr = Tcl_NewStringObj(argv[0], -1);
  Tcl_IncrRefCount(objPtr);
  resultObjPtr = Tcl_SetVar2Ex(pInterp, "argv0", NULL, objPtr,
      TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
  Tcl_DecrRefCount(objPtr);
  if( !resultObjPtr ){
    return TCL_ERROR;
  }
  objPtr = Tcl_NewIntObj(argc - 1);
  Tcl_IncrRefCount(objPtr);
  resultObjPtr = Tcl_SetVar2Ex(pInterp, "argc", NULL, objPtr,
      TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
  Tcl_DecrRefCount(objPtr);
  if( !resultObjPtr ){
    return TCL_ERROR;
  }
  listPtr = Tcl_NewListObj(0, NULL);
  Tcl_IncrRefCount(listPtr);
  if( argc>1 ){
    while( --argc ){
      objPtr = Tcl_NewStringObj(*++argv, -1);
      Tcl_IncrRefCount(objPtr);
      rc = Tcl_ListObjAppendElement(pInterp, listPtr, objPtr);
      Tcl_DecrRefCount(objPtr);
      if( rc!=TCL_OK ){
        break;
      }
    }
  }
  if( rc==TCL_OK ){
    resultObjPtr = Tcl_SetVar2Ex(pInterp, "argv", NULL, listPtr,
        TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
    if( !resultObjPtr ){
      rc = TCL_ERROR;
    }
  }
  Tcl_DecrRefCount(listPtr);
  return rc;
}

/*
** Creates and initializes a Tcl interpreter for use with the specified TH1
** interpreter.  Stores the created Tcl interpreter in the Tcl context supplied
** by the caller.
 */
static int createTclInterp(
  Th_Interp *interp,
  void *pContext
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  int argc;
  char **argv;
  char *argv0 = 0;
  Tcl_Interp *tclInterp;

  if ( !tclContext ){
    Th_ErrorMessage(interp,
        "Invalid Tcl context", (const char *)"", 0);
    return TH_ERROR;
  }
  if ( tclContext->interp ){
    return TH_OK;
  }
  argc = tclContext->argc;
  argv = tclContext->argv;
  if( argc>0 && argv ){
    argv0 = argv[0];
  }
  Tcl_FindExecutable(argv0);
  tclInterp = tclContext->interp = Tcl_CreateInterp();
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Th_ErrorMessage(interp,
        "Could not create Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  if( Tcl_Init(tclInterp)!=TCL_OK ){
    Th_ErrorMessage(interp,
        "Tcl initialization error:", Tcl_GetStringResult(tclInterp), -1);
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0;
    return TH_ERROR;
  }
  if( setTclArguments(tclInterp, argc, argv)!=TCL_OK ){
    Th_ErrorMessage(interp,
        "Tcl error setting arguments:", Tcl_GetStringResult(tclInterp), -1);
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0;
    return TH_ERROR;
  }
  /* Add the TH1 integration commands to Tcl. */
  Tcl_CallWhenDeleted(tclInterp, Th1DeleteProc, interp);
  Tcl_CreateObjCommand(tclInterp, "th1Eval", Th1EvalObjCmd, interp, NULL);
  Tcl_CreateObjCommand(tclInterp, "th1Expr", Th1ExprObjCmd, interp, NULL);
  return TH_OK;
}

/*
** Register the Tcl language commands with interpreter interp.
** Usually this is called soon after interpreter creation.
*/
int th_register_tcl(
  Th_Interp *interp,
  void *pContext
){
  int i;
  /* Add the Tcl integration commands to TH1. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    void *ctx;
    if ( !aCommand[i].zName || !aCommand[i].xProc ) continue;
    ctx = aCommand[i].pContext;
    /* Use Tcl interpreter for context? */
    if( !ctx ) ctx = pContext;
    Th_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc, ctx, 0);
  }
  return TH_OK;
}

#endif /* FOSSIL_ENABLE_TCL */
