/*
** This file contains code used to bridge the TH1 and Tcl scripting languages.
*/

#include "config.h"
#include "th.h"
#include "tcl.h"

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

  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclEval arg ?arg ...?");
  }
  tclInterp = (Tcl_Interp *)ctx;
  if( !tclInterp ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  if( argc==2 ){
    objPtr = Tcl_NewStringObj(argv[1], argl[1]);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_EvalObjEx(tclInterp, objPtr, 0);
    Tcl_DecrRefCount(objPtr);
  }else{
    int objc = argc-1;
    Tcl_Obj **objv = (Tcl_Obj **)ckalloc((unsigned)(objc * sizeof(Tcl_Obj *)));
    int i;
    for(i=1; i<argc; i++){
      objv[i-1] = Tcl_NewStringObj(argv[i], argl[i]);
      Tcl_IncrRefCount(objv[i-1]);
    }
    objPtr = Tcl_ConcatObj(objc, objv);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_EvalObjEx(tclInterp, objPtr, 0);
    Tcl_DecrRefCount(objPtr);
    for(i=1; i<argc; i++){
      Tcl_DecrRefCount(objv[i-1]);
    }
    ckfree((char *)objv);
  }
  objPtr = Tcl_GetObjResult(tclInterp);
  zResult = Tcl_GetStringFromObj(objPtr, &nResult);
  Th_SetResult(interp, zResult, nResult);
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

  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclExpr arg ?arg ...?");
  }
  tclInterp = (Tcl_Interp *)ctx;
  if( !tclInterp ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  if( argc==2 ){
    objPtr = Tcl_NewStringObj(argv[1], argl[1]);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_ExprObj(tclInterp, objPtr, &resultObjPtr);
    Tcl_DecrRefCount(objPtr);
  }else{
    int objc = argc-1;
    Tcl_Obj **objv = (Tcl_Obj **)ckalloc((unsigned)(objc * sizeof(Tcl_Obj *)));
    int i;
    for(i=1; i<argc; i++){
      objv[i-1] = Tcl_NewStringObj(argv[i], argl[i]);
      Tcl_IncrRefCount(objv[i-1]);
    }
    objPtr = Tcl_ConcatObj(objc, objv);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_ExprObj(tclInterp, objPtr, &resultObjPtr);
    Tcl_DecrRefCount(objPtr);
    for(i=1; i<argc; i++){
      Tcl_DecrRefCount(objv[i-1]);
    }
    ckfree((char *)objv);
  }
  zResult = Tcl_GetStringFromObj(resultObjPtr, &nResult);
  Th_SetResult(interp, zResult, nResult);
  Tcl_DecrRefCount(resultObjPtr);
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
  Tcl_CmdInfo cmdInfo;
  int objc;
  Tcl_Obj **objv;
  int i;
  int rc;
  int nResult;
  const char *zResult;
  Tcl_Obj *objPtr;

  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclInvoke command ?arg ...?");
  }
  tclInterp = (Tcl_Interp *)ctx;
  if( !tclInterp ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  if (Tcl_GetCommandInfo(tclInterp, argv[1], &cmdInfo) == 0){
    Th_ErrorMessage(interp, "Tcl command not found:", argv[1], argl[1]);
    return TH_ERROR;
  }
  objc = argc-1;
  objv = (Tcl_Obj **)ckalloc((unsigned)(objc * sizeof(Tcl_Obj *)));
  for(i=1; i<argc; i++){
    objv[i-1] = Tcl_NewStringObj(argv[i], argl[i]);
    Tcl_IncrRefCount(objv[i-1]);
  }
  Tcl_Preserve((ClientData)tclInterp);
  Tcl_ResetResult(tclInterp);
  rc = cmdInfo.objProc(cmdInfo.objClientData, tclInterp, objc, objv);
  for(i=1; i<argc; i++){
    Tcl_DecrRefCount(objv[i-1]);
  }
  ckfree((char *)objv);
  objPtr = Tcl_GetObjResult(tclInterp);
  zResult = Tcl_GetStringFromObj(objPtr, &nResult);
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
  Th_Interp *th1Interp = (Th_Interp *)clientData;
  int nArg;
  const char *arg;
  int rc;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "arg");
    return TCL_ERROR;
  }
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
  Th_Interp *th1Interp = (Th_Interp *)clientData;
  int nArg;
  const char *arg;
  int rc;

  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "arg");
    return TCL_ERROR;
  }
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
  if ( !th1Interp ) return;
  /* Remove the Tcl integration commands. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    Th_RenameCommand(th1Interp, aCommand[i].zName, -1, NULL, 0);
  }
}

/*
** Register the Tcl language commands with interpreter interp.
** Usually this is called soon after interpreter creation.
*/
int th_register_tcl(Th_Interp *interp){
  int i;
  Tcl_Interp *tclInterp = Tcl_CreateInterp();

  if( !tclInterp ){
    Th_ErrorMessage(interp,
        "Could not create Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  if( Tcl_Init(tclInterp)!=TCL_OK ){
    Th_ErrorMessage(interp,
        "Tcl initialization error:", Tcl_GetStringResult(tclInterp), -1);
    Tcl_DeleteInterp(tclInterp);
    return TH_ERROR;
  }
  Tcl_CallWhenDeleted(tclInterp, Th1DeleteProc, interp);
  Tcl_CreateObjCommand(tclInterp, "th1Eval", Th1EvalObjCmd, interp, NULL);
  Tcl_CreateObjCommand(tclInterp, "th1Expr", Th1ExprObjCmd, interp, NULL);
  /* Add the Tcl integration commands. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    void *ctx = aCommand[i].pContext;
    /* Use Tcl interpreter for context? */
    if( !ctx ) ctx = tclInterp;
    Th_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc, ctx, 0);
  }
  return TH_OK;
}
