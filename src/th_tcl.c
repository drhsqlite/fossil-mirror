/*
** This file contains code used to bridge the TH1 and Tcl scripting languages.
*/

#include "config.h"
#include "th.h"
#include "tcl.h"

/*
** Syntax:
**
**   tclEval script
*/
static int tclEval_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Tcl_Interp *tclInterp = (Tcl_Interp *)ctx;
  Tcl_Obj *objPtr;
  int rc;
  int nResult;
  const char *zResult;

  if( argc<2 ){
    return Th_WrongNumArgs(interp, "tclEval arg ?arg ...?");
  }

  if( !ctx ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }

  if( argc==2 ){
    objPtr = Tcl_NewStringObj(argv[1], argl[1]);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_EvalObjEx(tclInterp, objPtr, 0);
    Tcl_DecrRefCount(objPtr);
  }else{
    int i;
    int objc = argc-1;
    Tcl_Obj **objv = ckalloc((unsigned) (objc * sizeof(Tcl_Obj *)));
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
    ckfree(objv);
  }

  objPtr = Tcl_GetObjResult(tclInterp);
  zResult = Tcl_GetStringFromObj(objPtr, &nResult);
  Th_SetResult(interp, zResult, nResult);
  return TH_OK;
}

/*
** Register the Tcl language commands with interpreter interp.
** Usually this is called soon after interpreter creation.
*/
int th_register_tcl(Th_Interp *interp){
  /* Array of Tcl commands. */
  struct _Command {
    const char *zName;
    Th_CommandProc xProc;
    void *pContext;
  } aCommand[] = {
    {"tclEval",   tclEval_command,   0},
    /* {"tclExpr",   tclExpr_command,   0}, */
    /* {"tclInvoke", tclInvoke_command, 0}, */
    {0, 0, 0}
  };
  int i;
  Tcl_Interp *tclInterp = Tcl_CreateInterp();

  if( !tclInterp ){
    return TH_ERROR;
  }

  if( Tcl_Init(tclInterp)!=TCL_OK ){
    Th_ErrorMessage(interp,
        "Tcl initialization error:", Tcl_GetStringResult(tclInterp), -1);
    Tcl_DeleteInterp(tclInterp);
    return TH_ERROR;
  }

  /* Add the language commands. */
  for(i=0; i<(sizeof(aCommand)/sizeof(aCommand[0])); i++){
    void *ctx = aCommand[i].pContext;
    if( !ctx ){
      ctx = tclInterp; /* NOTE: Use Tcl interpreter for context. */
    }
    Th_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc, ctx, 0);
  }

  return TH_OK;
}
