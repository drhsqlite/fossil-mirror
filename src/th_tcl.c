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
#  define USE_TCL_EVALOBJV   1
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
** Define the Tcl shared library name, some exported function names, and some
** cross-platform macros for use with the Tcl stubs mechanism, when enabled.
 */
#if defined(USE_TCL_STUBS)
#  if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    ifndef TCL_LIBRARY_NAME
#      define TCL_LIBRARY_NAME "tcl86.dll\0"
#    endif
#    ifndef TCL_MINOR_OFFSET
#      define TCL_MINOR_OFFSET (4)
#    endif
#    ifndef dlopen
#      define dlopen(a,b) (void *)LoadLibrary((a))
#    endif
#    ifndef dlsym
#      define dlsym(a,b) GetProcAddress((HANDLE)(a),(b))
#    endif
#    ifndef dlclose
#      define dlclose(a) FreeLibrary((HANDLE)(a))
#    endif
#  else
#    include <dlfcn.h>
#    if defined(__CYGWIN__)
#      ifndef TCL_LIBRARY_NAME
#        define TCL_LIBRARY_NAME "libtcl8.6.dll\0"
#      endif
#      ifndef TCL_MINOR_OFFSET
#        define TCL_MINOR_OFFSET (8)
#      endif
#    elif defined(__APPLE__)
#      ifndef TCL_LIBRARY_NAME
#        define TCL_LIBRARY_NAME "libtcl8.6.dylib\0"
#      endif
#      ifndef TCL_MINOR_OFFSET
#        define TCL_MINOR_OFFSET (8)
#      endif
#    else
#      ifndef TCL_LIBRARY_NAME
#        define TCL_LIBRARY_NAME "libtcl8.6.so\0"
#      endif
#      ifndef TCL_MINOR_OFFSET
#        define TCL_MINOR_OFFSET (8)
#      endif
#    endif /* defined(__CYGWIN__) */
#  endif /* defined(_WIN32) */
#  ifndef TCL_FINDEXECUTABLE_NAME
#    define TCL_FINDEXECUTABLE_NAME "_Tcl_FindExecutable"
#  endif
#  ifndef TCL_CREATEINTERP_NAME
#    define TCL_CREATEINTERP_NAME "_Tcl_CreateInterp"
#  endif
#  ifndef TCL_DELETEINTERP_NAME
#    define TCL_DELETEINTERP_NAME "_Tcl_DeleteInterp"
#  endif
#endif /* defined(USE_TCL_STUBS) */

/*
** The function types for Tcl_FindExecutable and Tcl_CreateInterp are needed
** when the Tcl library is being loaded dynamically by a stubs-enabled
** application (i.e. the inverse of using a stubs-enabled package).  These are
** the only Tcl API functions that MUST be called prior to being able to call
** Tcl_InitStubs (i.e. because it requires a Tcl interpreter).  For complete
** cleanup if the Tcl stubs initialization fails somehow, the Tcl_DeleteInterp
** function type is also required.
 */
typedef void (tcl_FindExecutableProc) (const char * argv0);
typedef Tcl_Interp *(tcl_CreateInterpProc) (void);
typedef void (tcl_DeleteInterpProc) (Tcl_Interp *interp);

/*
** The function types for the "hook" functions to be called before and after a
** TH1 command makes a call to evaluate a Tcl script.  If the "pre" function
** returns anything but TH_OK, then evaluation of the Tcl script is skipped and
** that value is used as the return code.  If the "post" function returns
** anything other than its rc argument, that will become the new return code
** for the command.
 */
typedef int (tcl_NotifyProc) (
  void *pContext,    /* The context for this notification. */
  Th_Interp *interp, /* The TH1 interpreter being used. */
  void *ctx,         /* The original TH1 command context. */
  int argc,          /* Number of arguments for the TH1 command. */
  const char **argv, /* Array of arguments for the TH1 command. */
  int *argl,         /* Array of lengths for the TH1 command arguments. */
  int rc             /* Recommended notification return value. */
);

/*
** Are we using our own private implementation of the Tcl stubs mechanism?  If
** this is enabled, it prevents the user from having to link against the Tcl
** stubs library for the target platform, which may not be readily available.
 */
#if defined(FOSSIL_ENABLE_TCL_PRIVATE_STUBS)
/*
** HACK: Using some preprocessor magic and a private static variable, redirect
**       the Tcl API calls [found within this file] to the function pointers
**       that will be contained in our private Tcl stubs table.  This takes
**       advantage of the fact that the Tcl headers always define the Tcl API
**       functions in terms of the "tclStubsPtr" variable.
 */
#define tclStubsPtr privateTclStubsPtr
static const TclStubs *tclStubsPtr = NULL;

/*
** Create a Tcl interpreter structure that mirrors just enough fields to get
** it up and running successfully with our private implementation of the Tcl
** stubs mechanism.
 */
struct PrivateTclInterp {
  char *result;
  Tcl_FreeProc *freeProc;
  int errorLine;
  const struct TclStubs *stubTable;
};

/*
** Fossil can now be compiled without linking to the actual Tcl stubs library.
** In that case, this function will be used to perform those steps that would
** normally be performed within the Tcl stubs library.
 */
static int initTclStubs(
  Th_Interp *interp,
  Tcl_Interp *tclInterp
){
  tclStubsPtr = ((struct PrivateTclInterp *)tclInterp)->stubTable;
  if( !tclStubsPtr || (tclStubsPtr->magic!=TCL_STUB_MAGIC) ){
    Th_ErrorMessage(interp,
        "could not initialize Tcl stubs: incompatible mechanism",
        (const char *)"", 0);
    return TH_ERROR;
  }
  /* NOTE: At this point, the Tcl API functions should be available. */
  if( Tcl_PkgRequireEx(tclInterp, "Tcl", "8.4", 0, (void *)&tclStubsPtr)==0 ){
    Th_ErrorMessage(interp,
        "could not create Tcl interpreter: incompatible version",
        (const char *)"", 0);
    return TH_ERROR;
  }
  return TH_OK;
}
#endif

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
  int argc;           /* Number of original arguments. */
  char **argv;        /* Full copy of the original arguments. */
  void *library;      /* The Tcl library module handle. */
  tcl_FindExecutableProc *xFindExecutable; /* Tcl_FindExecutable() pointer. */
  tcl_CreateInterpProc *xCreateInterp;     /* Tcl_CreateInterp() pointer. */
  tcl_DeleteInterpProc *xDeleteInterp;     /* Tcl_DeleteInterp() pointer. */
  Tcl_Interp *interp; /* The on-demand created Tcl interpreter. */
  char *setup;        /* The optional Tcl setup script. */
  tcl_NotifyProc *xPreEval;  /* Optional, called before Tcl_Eval*(). */
  void *pPreContext;         /* Optional, provided to xPreEval(). */
  tcl_NotifyProc *xPostEval; /* Optional, called after Tcl_Eval*(). */
  void *pPostContext;        /* Optional, provided to xPostEval(). */
};

/*
** This function calls the configured xPreEval or xPostEval functions, if any.
** May have arbitrary side-effects.  This function returns the result of the
** called notification function or the value of the rc argument if there is no
** notification function configured.
*/
static int notifyPreOrPostEval(
  int bIsPost,
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl,
  int rc
){
  struct TclContext *tclContext = (struct TclContext *)ctx;
  tcl_NotifyProc *xNotifyProc;

  if ( !tclContext ){
    Th_ErrorMessage(interp,
        "invalid Tcl context", (const char *)"", 0);
    return TH_ERROR;
  }
  xNotifyProc = bIsPost ? tclContext->xPostEval : tclContext->xPreEval;
  if ( xNotifyProc ){
    rc = xNotifyProc(bIsPost ?
        tclContext->pPostContext : tclContext->pPreContext,
        interp, ctx, argc, argv, argl, rc);
  }
  return rc;
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
  int rc = TH_OK;
  int nResult;
  const char *zResult;

  if( createTclInterp(interp, ctx)!=TH_OK ){
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
  rc = notifyPreOrPostEval(0, interp, ctx, argc, argv, argl, rc);
  if( rc!=TH_OK ){
    return rc;
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
  rc = notifyPreOrPostEval(1, interp, ctx, argc, argv, argl, rc);
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
  int rc = TH_OK;
  int nResult;
  const char *zResult;

  if( createTclInterp(interp, ctx)!=TH_OK ){
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
  rc = notifyPreOrPostEval(0, interp, ctx, argc, argv, argl, rc);
  if( rc!=TH_OK ){
    return rc;
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
  rc = notifyPreOrPostEval(1, interp, ctx, argc, argv, argl, rc);
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
#if !defined(USE_TCL_EVALOBJV)
  Tcl_Command command;
  Tcl_CmdInfo cmdInfo;
#endif
  int rc = TH_OK;
  int nResult;
  const char *zResult;
#if !defined(USE_TCL_EVALOBJV)
  Tcl_Obj *objPtr;
#endif
  USE_ARGV_TO_OBJV();

  if( createTclInterp(interp, ctx)!=TH_OK ){
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
  rc = notifyPreOrPostEval(0, interp, ctx, argc, argv, argl, rc);
  if( rc!=TH_OK ){
    return rc;
  }
  Tcl_Preserve((ClientData)tclInterp);
#if !defined(USE_TCL_EVALOBJV)
  objPtr = Tcl_NewStringObj(argv[1], argl[1]);
  Tcl_IncrRefCount(objPtr);
  command = Tcl_GetCommandFromObj(tclInterp, objPtr);
  if( !command || Tcl_GetCommandInfoFromToken(command, &cmdInfo)==0 ){
    Th_ErrorMessage(interp, "Tcl command not found:", argv[1], argl[1]);
    Tcl_DecrRefCount(objPtr);
    Tcl_Release((ClientData)tclInterp);
    return TH_ERROR;
  }
  if( !cmdInfo.objProc ){
    Th_ErrorMessage(interp, "cannot invoke Tcl command:", argv[1], argl[1]);
    Tcl_DecrRefCount(objPtr);
    Tcl_Release((ClientData)tclInterp);
    return TH_ERROR;
  }
  Tcl_DecrRefCount(objPtr);
#endif
  COPY_ARGV_TO_OBJV();
#if defined(USE_TCL_EVALOBJV)
  rc = Tcl_EvalObjv(tclInterp, objc, objv, 0);
#else
  Tcl_ResetResult(tclInterp);
  rc = cmdInfo.objProc(cmdInfo.objClientData, tclInterp, objc, objv);
#endif
  FREE_ARGV_TO_OBJV();
  zResult = getTclResult(tclInterp, &nResult);
  Th_SetResult(interp, zResult, nResult);
  Tcl_Release((ClientData)tclInterp);
  rc = notifyPreOrPostEval(1, interp, ctx, argc, argv, argl, rc);
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
  Tcl_Obj *const objv[]
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
  Tcl_Obj *const objv[]
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
** When Tcl stubs support is enabled, attempts to dynamically load the Tcl
** shared library and fetch the function pointers necessary to create an
** interpreter and initialize the stubs mechanism; otherwise, simply setup
** the function pointers provided by the caller with the statically linked
** functions.
 */
static int loadTcl(
  Th_Interp *interp,
  void **pLibrary,
  tcl_FindExecutableProc **pxFindExecutable,
  tcl_CreateInterpProc **pxCreateInterp,
  tcl_DeleteInterpProc **pxDeleteInterp
){
#if defined(USE_TCL_STUBS)
  char fileName[] = TCL_LIBRARY_NAME;
#endif
  if( !pLibrary || !pxFindExecutable || !pxCreateInterp ){
    Th_ErrorMessage(interp,
        "invalid Tcl loader argument(s)", (const char *)"", 0);
    return TH_ERROR;
  }
#if defined(USE_TCL_STUBS)
  do {
    void *library = dlopen(fileName, RTLD_NOW | RTLD_GLOBAL);
    if( library ){
      tcl_FindExecutableProc *xFindExecutable;
      tcl_CreateInterpProc *xCreateInterp;
      tcl_DeleteInterpProc *xDeleteInterp;
      const char *procName = TCL_FINDEXECUTABLE_NAME;
      xFindExecutable = (tcl_FindExecutableProc *)dlsym(library, procName + 1);
      if( !xFindExecutable ){
        xFindExecutable = (tcl_FindExecutableProc *)dlsym(library, procName);
      }
      if( !xFindExecutable ){
        Th_ErrorMessage(interp,
            "could not locate Tcl_FindExecutable", (const char *)"", 0);
        dlclose(library);
        return TH_ERROR;
      }
      procName = TCL_CREATEINTERP_NAME;
      xCreateInterp = (tcl_CreateInterpProc *)dlsym(library, procName + 1);
      if( !xCreateInterp ){
        xCreateInterp = (tcl_CreateInterpProc *)dlsym(library, procName);
      }
      if( !xCreateInterp ){
        Th_ErrorMessage(interp,
            "could not locate Tcl_CreateInterp", (const char *)"", 0);
        dlclose(library);
        return TH_ERROR;
      }
      procName = TCL_DELETEINTERP_NAME;
      xDeleteInterp = (tcl_DeleteInterpProc *)dlsym(library, procName + 1);
      if( !xDeleteInterp ){
        xDeleteInterp = (tcl_DeleteInterpProc *)dlsym(library, procName);
      }
      if( !xDeleteInterp ){
        Th_ErrorMessage(interp,
            "could not locate Tcl_DeleteInterp", (const char *)"", 0);
        dlclose(library);
        return TH_ERROR;
      }
      *pLibrary = library;
      *pxFindExecutable = xFindExecutable;
      *pxCreateInterp = xCreateInterp;
      *pxDeleteInterp = xDeleteInterp;
      return TH_OK;
    }
  } while( --fileName[TCL_MINOR_OFFSET]>'3' ); /* Tcl 8.4+ */
  Th_ErrorMessage(interp,
      "could not load Tcl shared library \"" TCL_LIBRARY_NAME "\"",
      (const char *)"", 0);
  return TH_ERROR;
#else
  *pLibrary = 0;
  *pxFindExecutable = Tcl_FindExecutable;
  *pxCreateInterp = Tcl_CreateInterp;
  return TH_OK;
#endif
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
  char *setup;

  if ( !tclContext ){
    Th_ErrorMessage(interp,
        "invalid Tcl context", (const char *)"", 0);
    return TH_ERROR;
  }
  if ( tclContext->interp ){
    return TH_OK;
  }
  if( loadTcl(interp, &tclContext->library, &tclContext->xFindExecutable,
      &tclContext->xCreateInterp, &tclContext->xDeleteInterp)!=TH_OK ){
    return TH_ERROR;
  }
  argc = tclContext->argc;
  argv = tclContext->argv;
  if( argc>0 && argv ){
    argv0 = argv[0];
  }
  tclContext->xFindExecutable(argv0);
  tclInterp = tclContext->xCreateInterp();
  if( !tclInterp ){
    Th_ErrorMessage(interp,
        "could not create Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
#if defined(USE_TCL_STUBS)
#if defined(FOSSIL_ENABLE_TCL_PRIVATE_STUBS)
  if( initTclStubs(interp, tclInterp)!=TH_OK ){
    tclContext->xDeleteInterp(tclInterp);
    return TH_ERROR;
  }
#else
  if( !Tcl_InitStubs(tclInterp, "8.4", 0) ){
    Th_ErrorMessage(interp,
        "could not initialize Tcl stubs", (const char *)"", 0);
    tclContext->xDeleteInterp(tclInterp);
    return TH_ERROR;
  }
#endif /* defined(FOSSIL_ENABLE_TCL_PRIVATE_STUBS) */
#endif /* defined(USE_TCL_STUBS) */
  if( Tcl_InterpDeleted(tclInterp) ){
    Th_ErrorMessage(interp,
        "Tcl interpreter appears to be deleted", (const char *)"", 0);
    tclContext->xDeleteInterp(tclInterp); /* TODO: Redundant? */
    return TH_ERROR;
  }
  tclContext->interp = tclInterp;
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
  /* If necessary, evaluate the custom Tcl setup script. */
  setup = tclContext->setup;
  if( setup && Tcl_Eval(tclInterp, setup)!=TCL_OK ){
    Th_ErrorMessage(interp,
        "Tcl setup script error:", Tcl_GetStringResult(tclInterp), -1);
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0;
    return TH_ERROR;
  }
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
