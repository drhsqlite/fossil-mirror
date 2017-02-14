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

#include "sqlite3.h"
#include "th.h"
#include "tcl.h"

/*
** This macro is used to verify that the header version of Tcl meets some
** minimum requirement.
*/
#define MINIMUM_TCL_VERSION(major, minor) \
  ((TCL_MAJOR_VERSION > (major)) || \
   ((TCL_MAJOR_VERSION == (major)) && (TCL_MINOR_VERSION >= (minor))))

/*
** These macros are designed to reduce the redundant code required to marshal
** arguments from TH1 to Tcl.
*/
#define USE_ARGV_TO_OBJV() \
  int objc;                \
  Tcl_Obj **objv;          \
  int obji;

#define COPY_ARGV_TO_OBJV()                                         \
  objc = argc-1;                                                    \
  objv = (Tcl_Obj **)ckalloc((unsigned)(objc * sizeof(Tcl_Obj *))); \
  for(obji=1; obji<argc; obji++){                                   \
    objv[obji-1] = Tcl_NewStringObj(argv[obji], argl[obji]);        \
    Tcl_IncrRefCount(objv[obji-1]);                                 \
  }

#define FREE_ARGV_TO_OBJV()         \
  for(obji=1; obji<argc; obji++){   \
    Tcl_DecrRefCount(objv[obji-1]); \
    objv[obji-1] = 0;               \
  }                                 \
  ckfree((char *)objv);             \
  objv = 0;

/*
** Fetch the Tcl interpreter from the specified void pointer, cast to a Tcl
** context.
*/
#define GET_CTX_TCL_INTERP(ctx) \
  ((struct TclContext *)(ctx))->interp

/*
** Fetch the (logically boolean) value from the specified void pointer that
** indicates whether or not we can/should use direct objProc calls.
*/
#define GET_CTX_TCL_USEOBJPROC(ctx) \
  ((struct TclContext *)(ctx))->useObjProc

/*
** This is the name of an environment variable that may refer to a Tcl library
** directory or file name.  If this environment variable is set [to anything],
** its value will be used when searching for a Tcl library to load.
*/
#ifndef TCL_PATH_ENV_VAR_NAME
#  define TCL_PATH_ENV_VAR_NAME  "FOSSIL_TCL_PATH"
#endif

/*
** Define the Tcl shared library name, some exported function names, and some
** cross-platform macros for use with the Tcl stubs mechanism, when enabled.
*/
#if defined(USE_TCL_STUBS)
#  if defined(_WIN32)
#    if !defined(WIN32_LEAN_AND_MEAN)
#      define WIN32_LEAN_AND_MEAN
#    endif
#    if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0502)
#      undef _WIN32_WINNT
#      define _WIN32_WINNT 0x0502 /* SetDllDirectory, Windows XP SP2 */
#    endif
#    include <windows.h>
#    ifndef TCL_DIRECTORY_SEP
#      define TCL_DIRECTORY_SEP '\\'
#    endif
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
#    ifndef TCL_DIRECTORY_SEP
#      define TCL_DIRECTORY_SEP '/'
#    endif
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
#    define TCL_FINDEXECUTABLE_NAME "_Tcl_FindExecutable\0"
#  endif
#  ifndef TCL_CREATEINTERP_NAME
#    define TCL_CREATEINTERP_NAME "_Tcl_CreateInterp\0"
#  endif
#  ifndef TCL_DELETEINTERP_NAME
#    define TCL_DELETEINTERP_NAME "_Tcl_DeleteInterp\0"
#  endif
#  ifndef TCL_FINALIZE_NAME
#    define TCL_FINALIZE_NAME "_Tcl_Finalize\0"
#  endif
#endif /* defined(USE_TCL_STUBS) */

/*
** If this constant is defined to non-zero, the Win32 SetDllDirectory function
** will be used during the Tcl library loading process if the path environment
** variable for Tcl was set.
*/
#ifndef TCL_USE_SET_DLL_DIRECTORY
#  if defined(_WIN32) && defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0502)
#    define TCL_USE_SET_DLL_DIRECTORY (1)
#  else
#    define TCL_USE_SET_DLL_DIRECTORY (0)
#  endif
#endif /* TCL_USE_SET_DLL_DIRECTORY */

/*
** The function types for Tcl_FindExecutable and Tcl_CreateInterp are needed
** when the Tcl library is being loaded dynamically by a stubs-enabled
** application (i.e. the inverse of using a stubs-enabled package).  These are
** the only Tcl API functions that MUST be called prior to being able to call
** Tcl_InitStubs (i.e. because it requires a Tcl interpreter).  For complete
** cleanup if the Tcl stubs initialization fails somehow, the Tcl_DeleteInterp
** and Tcl_Finalize function types are also required.
*/
typedef void (tcl_FindExecutableProc) (const char *);
typedef Tcl_Interp *(tcl_CreateInterpProc) (void);
typedef void (tcl_DeleteInterpProc) (Tcl_Interp *);
typedef void (tcl_FinalizeProc) (void);

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
**       functions in terms of the "tclStubsPtr" variable when the define
**       USE_TCL_STUBS is present during compilation.
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
        "could not initialize Tcl stubs: incompatible version",
        (const char *)"", 0);
    return TH_ERROR;
  }
  return TH_OK;
}
#endif /* defined(FOSSIL_ENABLE_TCL_PRIVATE_STUBS) */

/*
** Is the loaded version of Tcl one where querying and/or calling the objProc
** for a command does not work for some reason?  The following special cases
** are currently handled by this function:
**
** 1. All versions of Tcl 8.4 have a bug that causes a crash when calling into
**    the Tcl_GetCommandFromObj function via stubs (i.e. the stubs table entry
**    is NULL).
**
** 2. Various beta builds of Tcl 8.6, namely 1 and 2, have an NRE-specific bug
**    in Tcl_EvalObjCmd (SF bug #3399564) that cause a panic when calling into
**    the objProc directly.
**
** For both of the above cases, the Tcl_EvalObjv function must be used instead
** of the more direct route of querying and calling the objProc directly.
*/
static int canUseObjProc(){
  int major = -1, minor = -1, patchLevel = -1, type = -1;

  Tcl_GetVersion(&major, &minor, &patchLevel, &type);
  if( major<0 || minor<0 || patchLevel<0 || type<0 ){
    return 0; /* NOTE: Invalid version info, assume bad. */
  }
  if( major==8 && minor==4 ){
    return 0; /* NOTE: Disabled on Tcl 8.4, missing public API. */
  }
  if( major==8 && minor==6 && type==TCL_BETA_RELEASE && patchLevel<3 ){
    return 0; /* NOTE: Disabled on Tcl 8.6b1/b2, SF bug #3399564. */
  }
  return 1;   /* NOTE: For all other cases, assume good. */
}

/*
** Is the loaded version of Tcl one where TIP #285 (asynchronous script
** cancellation) is available?  This should return non-zero only for Tcl
** 8.6 and higher.
*/
static int canUseTip285(){
#if MINIMUM_TCL_VERSION(8, 6)
  int major = -1, minor = -1, patchLevel = -1, type = -1;

  Tcl_GetVersion(&major, &minor, &patchLevel, &type);
  if( major<0 || minor<0 || patchLevel<0 || type<0 ){
    return 0; /* NOTE: Invalid version info, assume bad. */
  }
  return (major>8 || (major==8 && minor>=6));
#else
  return 0;
#endif
}

/*
** Creates and initializes a Tcl interpreter for use with the specified TH1
** interpreter.  Stores the created Tcl interpreter in the Tcl context supplied
** by the caller.  This must be declared here because quite a few functions in
** this file need to use it before it can be defined.
*/
static int createTclInterp(Th_Interp *interp, void *pContext);

/*
** Returns the TH1 return code corresponding to the specified Tcl
** return code.
*/
static int getTh1ReturnCode(
  int rc /* The Tcl return code value to convert. */
){
  switch( rc ){
    case /*0*/ TCL_OK:       return /*0*/ TH_OK;
    case /*1*/ TCL_ERROR:    return /*1*/ TH_ERROR;
    case /*2*/ TCL_RETURN:   return /*3*/ TH_RETURN;
    case /*3*/ TCL_BREAK:    return /*2*/ TH_BREAK;
    case /*4*/ TCL_CONTINUE: return /*4*/ TH_CONTINUE;
    default /*?*/:           return /*?*/ rc;
  }
}

/*
** Returns the Tcl return code corresponding to the specified TH1
** return code.
*/
static int getTclReturnCode(
  int rc /* The TH1 return code value to convert. */
){
  switch( rc ){
    case /*0*/ TH_OK:       return /*0*/ TCL_OK;
    case /*1*/ TH_ERROR:    return /*1*/ TCL_ERROR;
    case /*2*/ TH_BREAK:    return /*3*/ TCL_BREAK;
    case /*3*/ TH_RETURN:   return /*2*/ TCL_RETURN;
    case /*4*/ TH_CONTINUE: return /*4*/ TCL_CONTINUE;
    default /*?*/:          return /*?*/ rc;
  }
}

/*
** Returns a name for a Tcl return code.
*/
static const char *getTclReturnCodeName(
  int rc,
  int nullIfOk
){
  static char zRc[TCL_INTEGER_SPACE + 17]; /* "Tcl return code\0" */

  switch( rc ){
    case TCL_OK:       return nullIfOk ? 0 : "TCL_OK";
    case TCL_ERROR:    return "TCL_ERROR";
    case TCL_RETURN:   return "TCL_RETURN";
    case TCL_BREAK:    return "TCL_BREAK";
    case TCL_CONTINUE: return "TCL_CONTINUE";
    default: {
      sqlite3_snprintf(sizeof(zRc), zRc, "Tcl return code %d", rc);
    }
  }
  return zRc;
}

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
  void *hLibrary;     /* The Tcl library module handle. */
  tcl_FindExecutableProc *xFindExecutable; /* Tcl_FindExecutable() pointer. */
  tcl_CreateInterpProc *xCreateInterp;     /* Tcl_CreateInterp() pointer. */
  tcl_DeleteInterpProc *xDeleteInterp;     /* Tcl_DeleteInterp() pointer. */
  tcl_FinalizeProc *xFinalize;             /* Tcl_Finalize() pointer. */
  Tcl_Interp *interp; /* The on-demand created Tcl interpreter. */
  int useObjProc;     /* Non-zero if an objProc can be called directly. */
  int useTip285;      /* Non-zero if TIP #285 is available. */
  const char *setup;  /* The optional Tcl setup script. */
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

  if( !tclContext ){
    Th_ErrorMessage(interp,
        "invalid Tcl context", (const char *)"", 0);
    return TH_ERROR;
  }
  xNotifyProc = bIsPost ? tclContext->xPostEval : tclContext->xPreEval;
  if( xNotifyProc ){
    rc = xNotifyProc(bIsPost ?
        tclContext->pPostContext : tclContext->pPreContext,
        interp, ctx, argc, argv, argl, rc);
  }
  return rc;
}

/*
** TH1 command: tclEval arg ?arg ...?
**
** Evaluates the Tcl script and returns its result verbatim.  If a Tcl script
** error is generated, it will be transformed into a TH1 script error.  The
** Tcl interpreter will be created automatically if it has not been already.
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
    Tcl_DecrRefCount(objPtr); objPtr = 0;
  }else{
    USE_ARGV_TO_OBJV();
    COPY_ARGV_TO_OBJV();
    objPtr = Tcl_ConcatObj(objc, objv);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_EvalObjEx(tclInterp, objPtr, 0);
    Tcl_DecrRefCount(objPtr); objPtr = 0;
    FREE_ARGV_TO_OBJV();
  }
  zResult = getTclResult(tclInterp, &nResult);
  Th_SetResult(interp, zResult, nResult);
  Tcl_Release((ClientData)tclInterp);
  rc = notifyPreOrPostEval(1, interp, ctx, argc, argv, argl,
                           getTh1ReturnCode(rc));
  return rc;
}

/*
** TH1 command: tclExpr arg ?arg ...?
**
** Evaluates the Tcl expression and returns its result verbatim.  If a Tcl
** script error is generated, it will be transformed into a TH1 script error.
** The Tcl interpreter will be created automatically if it has not been
** already.
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
    Tcl_DecrRefCount(objPtr); objPtr = 0;
  }else{
    USE_ARGV_TO_OBJV();
    COPY_ARGV_TO_OBJV();
    objPtr = Tcl_ConcatObj(objc, objv);
    Tcl_IncrRefCount(objPtr);
    rc = Tcl_ExprObj(tclInterp, objPtr, &resultObjPtr);
    Tcl_DecrRefCount(objPtr); objPtr = 0;
    FREE_ARGV_TO_OBJV();
  }
  if( rc==TCL_OK ){
    zResult = Tcl_GetStringFromObj(resultObjPtr, &nResult);
  }else{
    zResult = getTclResult(tclInterp, &nResult);
  }
  Th_SetResult(interp, zResult, nResult);
  if( rc==TCL_OK ){
    Tcl_DecrRefCount(resultObjPtr); resultObjPtr = 0;
  }
  Tcl_Release((ClientData)tclInterp);
  rc = notifyPreOrPostEval(1, interp, ctx, argc, argv, argl,
                           getTh1ReturnCode(rc));
  return rc;
}

/*
** TH1 command: tclInvoke command ?arg ...?
**
** Invokes the Tcl command using the supplied arguments.  No additional
** substitutions are performed on the arguments.  The Tcl interpreter
** will be created automatically if it has not been already.
*/
static int tclInvoke_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Tcl_Interp *tclInterp;
  int rc = TH_OK;
  int nResult;
  const char *zResult;
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
#if !defined(USE_TCL_EVALOBJV) || !USE_TCL_EVALOBJV
  if( GET_CTX_TCL_USEOBJPROC(ctx) ){
    Tcl_Command command;
    Tcl_CmdInfo cmdInfo;
    Tcl_Obj *objPtr = Tcl_NewStringObj(argv[1], argl[1]);
    Tcl_IncrRefCount(objPtr);
    command = Tcl_GetCommandFromObj(tclInterp, objPtr);
    if( !command || Tcl_GetCommandInfoFromToken(command, &cmdInfo)==0 ){
      Th_ErrorMessage(interp, "Tcl command not found:", argv[1], argl[1]);
      Tcl_DecrRefCount(objPtr); objPtr = 0;
      Tcl_Release((ClientData)tclInterp);
      return TH_ERROR;
    }
    if( !cmdInfo.objProc ){
      Th_ErrorMessage(interp, "cannot invoke Tcl command:", argv[1], argl[1]);
      Tcl_DecrRefCount(objPtr); objPtr = 0;
      Tcl_Release((ClientData)tclInterp);
      return TH_ERROR;
    }
    Tcl_DecrRefCount(objPtr); objPtr = 0;
    COPY_ARGV_TO_OBJV();
    Tcl_ResetResult(tclInterp);
    rc = cmdInfo.objProc(cmdInfo.objClientData, tclInterp, objc, objv);
    FREE_ARGV_TO_OBJV();
  }else
#endif /* !defined(USE_TCL_EVALOBJV) || !USE_TCL_EVALOBJV */
  {
    COPY_ARGV_TO_OBJV();
    rc = Tcl_EvalObjv(tclInterp, objc, objv, 0);
    FREE_ARGV_TO_OBJV();
  }
  zResult = getTclResult(tclInterp, &nResult);
  Th_SetResult(interp, zResult, nResult);
  Tcl_Release((ClientData)tclInterp);
  rc = notifyPreOrPostEval(1, interp, ctx, argc, argv, argl,
                           getTh1ReturnCode(rc));
  return rc;
}

/*
** TH1 command: tclIsSafe
**
** Returns non-zero if the Tcl interpreter is "safe".  The Tcl interpreter
** will be created automatically if it has not been already.
*/
static int tclIsSafe_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  Tcl_Interp *tclInterp;

  if( createTclInterp(interp, ctx)!=TH_OK ){
    return TH_ERROR;
  }
  if( argc!=1 ){
    return Th_WrongNumArgs(interp, "tclIsSafe");
  }
  tclInterp = GET_CTX_TCL_INTERP(ctx);
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  Th_SetResultInt(interp, Tcl_IsSafe(tclInterp));
  return TH_OK;
}

/*
** TH1 command: tclMakeSafe
**
** Forces the Tcl interpreter into "safe" mode by removing all "unsafe"
** commands and variables.  This operation cannot be undone.  The Tcl
** interpreter will remain "safe" until the process terminates.
*/
static int tclMakeSafe_command(
  Th_Interp *interp,
  void *ctx,
  int argc,
  const char **argv,
  int *argl
){
  static int registerChans = 1;
  Tcl_Interp *tclInterp;
  int rc = TH_OK;

  if( createTclInterp(interp, ctx)!=TH_OK ){
    return TH_ERROR;
  }
  if( argc!=1 ){
    return Th_WrongNumArgs(interp, "tclMakeSafe");
  }
  tclInterp = GET_CTX_TCL_INTERP(ctx);
  if( !tclInterp || Tcl_InterpDeleted(tclInterp) ){
    Th_ErrorMessage(interp, "invalid Tcl interpreter", (const char *)"", 0);
    return TH_ERROR;
  }
  if( Tcl_IsSafe(tclInterp) ){
    Th_ErrorMessage(interp,
        "Tcl interpreter is already 'safe'", (const char *)"", 0);
    return TH_ERROR;
  }
  if( registerChans ){
    /*
    ** HACK: Prevent the call to Tcl_MakeSafe() from actually closing the
    **       standard channels instead of simply unregistering them from
    **       the Tcl interpreter.  This should only need to be done once
    **       per thread (process?).
    */
    registerChans = 0;
    Tcl_RegisterChannel(NULL, Tcl_GetStdChannel(TCL_STDIN));
    Tcl_RegisterChannel(NULL, Tcl_GetStdChannel(TCL_STDOUT));
    Tcl_RegisterChannel(NULL, Tcl_GetStdChannel(TCL_STDERR));
  }
  Tcl_Preserve((ClientData)tclInterp);
  if( Tcl_MakeSafe(tclInterp)!=TCL_OK ){
    int nResult;
    const char *zResult = getTclResult(tclInterp, &nResult);
    Th_ErrorMessage(interp,
        "could not make Tcl interpreter 'safe':", zResult, nResult);
    rc = TH_ERROR;
  }else{
    Th_SetResult(interp, 0, 0);
  }
  Tcl_Release((ClientData)tclInterp);
  return rc;
}

/*
** Tcl command: th1Eval arg
**
** Evaluates the TH1 script and returns its result verbatim.  If a TH1 script
** error is generated, it will be transformed into a Tcl script error.
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
  return getTclReturnCode(rc);
}

/*
** Tcl command: th1Expr arg
**
** Evaluates the TH1 expression and returns its result verbatim.  If a TH1
** script error is generated, it will be transformed into a Tcl script error.
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
  return getTclReturnCode(rc);
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
  {"tclEval",     tclEval_command,     0},
  {"tclExpr",     tclExpr_command,     0},
  {"tclInvoke",   tclInvoke_command,   0},
  {"tclIsSafe",   tclIsSafe_command,   0},
  {"tclMakeSafe", tclMakeSafe_command, 0},
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
  for(i=0; i<count(aCommand); i++){
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
char *fossil_getenv(const char *zName); /* file.h */
int file_isdir(const char *zPath);      /* file.h */
char *file_dirname(const char *zPath);  /* file.h */
void fossil_free(void *p);              /* util.h */

static int loadTcl(
  Th_Interp *interp,
  void **phLibrary,
  tcl_FindExecutableProc **pxFindExecutable,
  tcl_CreateInterpProc **pxCreateInterp,
  tcl_DeleteInterpProc **pxDeleteInterp,
  tcl_FinalizeProc **pxFinalize
){
#if defined(USE_TCL_STUBS)
  const char *zEnvPath = fossil_getenv(TCL_PATH_ENV_VAR_NAME);
  char aFileName[] = TCL_LIBRARY_NAME;
#endif /* defined(USE_TCL_STUBS) */

  if( !phLibrary || !pxFindExecutable || !pxCreateInterp ||
      !pxDeleteInterp || !pxFinalize ){
    Th_ErrorMessage(interp,
        "invalid Tcl loader argument(s)", (const char *)"", 0);
    return TH_ERROR;
  }
#if defined(USE_TCL_STUBS)
  do {
    char *zFileName;
    void *hLibrary;
    if( !zEnvPath ){
      zFileName = aFileName; /* NOTE: Assume present in PATH. */
    }else if( file_isdir(zEnvPath)==1 ){
#if TCL_USE_SET_DLL_DIRECTORY
      SetDllDirectory(zEnvPath); /* NOTE: Maybe needed for "zlib1.dll". */
#endif /* TCL_USE_SET_DLL_DIRECTORY */
      /* NOTE: The environment variable contains a directory name. */
      zFileName = sqlite3_mprintf("%s%c%s%c", zEnvPath, TCL_DIRECTORY_SEP,
                                  aFileName, '\0');
    }else{
#if TCL_USE_SET_DLL_DIRECTORY
      char *zDirName = file_dirname(zEnvPath);
      if( zDirName ){
        SetDllDirectory(zDirName); /* NOTE: Maybe needed for "zlib1.dll". */
      }
#endif /* TCL_USE_SET_DLL_DIRECTORY */
      /* NOTE: The environment variable might contain a file name. */
      zFileName = sqlite3_mprintf("%s%c", zEnvPath, '\0');
#if TCL_USE_SET_DLL_DIRECTORY
      if( zDirName ){
        fossil_free(zDirName); zDirName = 0;
      }
#endif /* TCL_USE_SET_DLL_DIRECTORY */
    }
    if( !zFileName ) break;
    hLibrary = dlopen(zFileName, RTLD_NOW | RTLD_GLOBAL);
    /* NOTE: If the file name was allocated, free it now. */
    if( zFileName!=aFileName ){
      sqlite3_free(zFileName); zFileName = 0;
    }
    if( hLibrary ){
      tcl_FindExecutableProc *xFindExecutable;
      tcl_CreateInterpProc *xCreateInterp;
      tcl_DeleteInterpProc *xDeleteInterp;
      tcl_FinalizeProc *xFinalize;
      const char *procName = TCL_FINDEXECUTABLE_NAME;
      xFindExecutable = (tcl_FindExecutableProc *)dlsym(hLibrary, procName+1);
      if( !xFindExecutable ){
        xFindExecutable = (tcl_FindExecutableProc *)dlsym(hLibrary, procName);
      }
      if( !xFindExecutable ){
        Th_ErrorMessage(interp,
            "could not locate Tcl_FindExecutable", (const char *)"", 0);
        dlclose(hLibrary); hLibrary = 0;
        return TH_ERROR;
      }
      procName = TCL_CREATEINTERP_NAME;
      xCreateInterp = (tcl_CreateInterpProc *)dlsym(hLibrary, procName+1);
      if( !xCreateInterp ){
        xCreateInterp = (tcl_CreateInterpProc *)dlsym(hLibrary, procName);
      }
      if( !xCreateInterp ){
        Th_ErrorMessage(interp,
            "could not locate Tcl_CreateInterp", (const char *)"", 0);
        dlclose(hLibrary); hLibrary = 0;
        return TH_ERROR;
      }
      procName = TCL_DELETEINTERP_NAME;
      xDeleteInterp = (tcl_DeleteInterpProc *)dlsym(hLibrary, procName+1);
      if( !xDeleteInterp ){
        xDeleteInterp = (tcl_DeleteInterpProc *)dlsym(hLibrary, procName);
      }
      if( !xDeleteInterp ){
        Th_ErrorMessage(interp,
            "could not locate Tcl_DeleteInterp", (const char *)"", 0);
        dlclose(hLibrary); hLibrary = 0;
        return TH_ERROR;
      }
      procName = TCL_FINALIZE_NAME;
      xFinalize = (tcl_FinalizeProc *)dlsym(hLibrary, procName+1);
      if( !xFinalize ){
        xFinalize = (tcl_FinalizeProc *)dlsym(hLibrary, procName);
      }
      if( !xFinalize ){
        Th_ErrorMessage(interp,
            "could not locate Tcl_Finalize", (const char *)"", 0);
        dlclose(hLibrary); hLibrary = 0;
        return TH_ERROR;
      }
      *phLibrary = hLibrary;
      *pxFindExecutable = xFindExecutable;
      *pxCreateInterp = xCreateInterp;
      *pxDeleteInterp = xDeleteInterp;
      *pxFinalize = xFinalize;
      return TH_OK;
    }
  } while( --aFileName[TCL_MINOR_OFFSET]>'3' ); /* Tcl 8.4+ */
  aFileName[TCL_MINOR_OFFSET] = 'x';
  Th_ErrorMessage(interp,
      "could not load any supported Tcl 8.6, 8.5, or 8.4 shared library \"",
      aFileName, -1);
  return TH_ERROR;
#else
  *phLibrary = 0;
  *pxFindExecutable = Tcl_FindExecutable;
  *pxCreateInterp = Tcl_CreateInterp;
  *pxDeleteInterp = Tcl_DeleteInterp;
  *pxFinalize = Tcl_Finalize;
  return TH_OK;
#endif /* defined(USE_TCL_STUBS) */
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
  Tcl_DecrRefCount(objPtr); objPtr = 0;
  if( !resultObjPtr ){
    return TCL_ERROR;
  }
  objPtr = Tcl_NewIntObj(argc - 1);
  Tcl_IncrRefCount(objPtr);
  resultObjPtr = Tcl_SetVar2Ex(pInterp, "argc", NULL, objPtr,
      TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
  Tcl_DecrRefCount(objPtr); objPtr = 0;
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
      Tcl_DecrRefCount(objPtr); objPtr = 0;
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
  Tcl_DecrRefCount(listPtr); listPtr = 0;
  return rc;
}

/*
** Evaluate a Tcl script, creating the Tcl interpreter if necessary. If the
** Tcl script succeeds, start a Tcl event loop until there are no more events
** remaining to process -OR- the script calls [exit].  If the bWait argument
** is zero, only process events that are already in the queue; otherwise,
** process events until the script terminates the Tcl event loop.
*/
void fossil_print(const char *zFormat, ...); /* printf.h */

int evaluateTclWithEvents(
  Th_Interp *interp,
  void *pContext,
  const char *zScript,
  int nScript,
  int bCancel,
  int bWait,
  int bVerbose
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  Tcl_Interp *tclInterp;
  int rc;
  int flags = TCL_ALL_EVENTS;
  int useTip285;

  if( createTclInterp(interp, pContext)!=TH_OK ){
    return TH_ERROR;
  }
  tclInterp = tclContext->interp;
  useTip285 = bCancel ? tclContext->useTip285 : 0;
  rc = Tcl_EvalEx(tclInterp, zScript, nScript, TCL_EVAL_GLOBAL);
  if( rc!=TCL_OK ){
    if( bVerbose ){
      const char *zResult = getTclResult(tclInterp, 0);
      fossil_print("%s: ", getTclReturnCodeName(rc, 0));
      fossil_print("%s\n", zResult);
    }
    return rc;
  }
  if( !bWait ) flags |= TCL_DONT_WAIT;
  Tcl_Preserve((ClientData)tclInterp);
  while( Tcl_DoOneEvent(flags) ){
    if( Tcl_InterpDeleted(tclInterp) ){
      break;
    }
#if MINIMUM_TCL_VERSION(8, 6)
    if( useTip285 && Tcl_Canceled(tclInterp, 0)!=TCL_OK ){
      break;
    }
#endif
  }
  Tcl_Release((ClientData)tclInterp);
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
  const char *setup;

  if( !tclContext ){
    Th_ErrorMessage(interp,
        "invalid Tcl context", (const char *)"", 0);
    return TH_ERROR;
  }
  if( tclContext->interp ){
    return TH_OK;
  }
  if( loadTcl(interp, &tclContext->hLibrary, &tclContext->xFindExecutable,
              &tclContext->xCreateInterp, &tclContext->xDeleteInterp,
              &tclContext->xFinalize)!=TH_OK ){
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
    tclInterp = 0;
    return TH_ERROR;
  }
#else
  if( !Tcl_InitStubs(tclInterp, "8.4", 0) ){
    Th_ErrorMessage(interp,
        "could not initialize Tcl stubs", (const char *)"", 0);
    tclContext->xDeleteInterp(tclInterp);
    tclInterp = 0;
    return TH_ERROR;
  }
#endif /* defined(FOSSIL_ENABLE_TCL_PRIVATE_STUBS) */
#endif /* defined(USE_TCL_STUBS) */
  if( Tcl_InterpDeleted(tclInterp) ){
    Th_ErrorMessage(interp,
        "Tcl interpreter appears to be deleted", (const char *)"", 0);
    Tcl_DeleteInterp(tclInterp); /* TODO: Redundant? */
    tclInterp = 0;
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
  /*
  ** Determine (and cache) if an objProc can be called directly for a Tcl
  ** command invoked via the tclInvoke TH1 command.
  */
  tclContext->useObjProc = canUseObjProc();
  /*
  ** Determine (and cache) whether or not we can use TIP #285 (asynchronous
  ** script cancellation).
  */
  tclContext->useTip285 = canUseTip285();
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
** Finalizes and unloads the previously loaded Tcl library, if applicable.
*/
int unloadTcl(
  Th_Interp *interp,
  void *pContext
){
  struct TclContext *tclContext = (struct TclContext *)pContext;
  Tcl_Interp *tclInterp;
  tcl_FinalizeProc *xFinalize;
#if defined(USE_TCL_STUBS)
  void *hLibrary;
#endif /* defined(USE_TCL_STUBS) */

  if( !tclContext ){
    Th_ErrorMessage(interp,
        "invalid Tcl context", (const char *)"", 0);
    return TH_ERROR;
  }
  /*
  ** Grab the Tcl_Finalize function pointer prior to deleting the Tcl
  ** interpreter because the memory backing the Tcl stubs table will
  ** be going away.
  */
  xFinalize = tclContext->xFinalize;
  /*
  ** If the Tcl interpreter has been created, formally delete it now.
  */
  tclInterp = tclContext->interp;
  if( tclInterp ){
    Tcl_DeleteInterp(tclInterp);
    tclContext->interp = tclInterp = 0;
  }
  /*
  ** If the Tcl library is not finalized prior to unloading it, a deadlock
  ** can occur in some circumstances (i.e. the [clock] thread is running).
  */
  if( xFinalize ) xFinalize();
#if defined(USE_TCL_STUBS)
  /*
  ** If Tcl is compiled on Windows using the latest MinGW, Fossil can crash
  ** when exiting while a stubs-enabled Tcl is still loaded.  This is due to
  ** a bug in MinGW, see:
  **
  **     http://comments.gmane.org/gmane.comp.gnu.mingw.user/41724
  **
  ** The workaround is to manually unload the loaded Tcl library prior to
  ** exiting the process.
  */
  hLibrary = tclContext->hLibrary;
  if( hLibrary ){
    dlclose(hLibrary);
    tclContext->hLibrary = hLibrary = 0;
  }
#endif /* defined(USE_TCL_STUBS) */
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
  for(i=0; i<count(aCommand); i++){
    void *ctx;
    if( !aCommand[i].zName || !aCommand[i].xProc ) continue;
    ctx = aCommand[i].pContext;
    /* Use Tcl interpreter for context? */
    if( !ctx ) ctx = pContext;
    Th_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc, ctx, 0);
  }
  return TH_OK;
}

#endif /* FOSSIL_ENABLE_TCL */
