/*
** This file contains code used to bridge the TH1 and Tcl scripting languages.
*/

#include "config.h"
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
** Returns the Tcl interpreter result as a string with the associated length.
** If the Tcl interpreter or the Tcl result are NULL, the length will be 0.
** If the length pointer is NULL, the length will not be stored.
 */
static char *getTclResult(Tcl_Interp *pInterp, int *pN){
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

  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclExpr arg ?arg ...?");
  }
  tclInterp = (Tcl_Interp *)ctx;
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

  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclInvoke command ?arg ...?");
  }
  tclInterp = (Tcl_Interp *)ctx;
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
** Register the Tcl language commands with interpreter interp.
** Usually this is called soon after interpreter creation.
*/
int th_register_tcl(Th_Interp *interp){
  int i;
  Tcl_Interp *tclInterp = Tcl_CreateInterp();

  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
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
  /* Add the TH1 integration commands to Tcl. */
  Tcl_CallWhenDeleted(tclInterp, Th1DeleteProc, interp);
  Tcl_CreateObjCommand(tclInterp, "th1Eval", Th1EvalObjCmd, interp, NULL);
  Tcl_CreateObjCommand(tclInterp, "th1Expr", Th1ExprObjCmd, interp, NULL);
  /* Add the Tcl integration commands to TH1. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    void *ctx = aCommand[i].pContext;
    /* Use Tcl interpreter for context? */
    if( !ctx ) ctx = tclInterp;
    Th_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc, ctx, 0);
  }
  return TH_OK;
}
