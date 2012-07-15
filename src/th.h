#include "config.h"

#define TH_USE_SQLITE
#ifdef TH_USE_SQLITE
#include "sqlite3.h"
#endif

/*
** TH_USE_OUTBUF, if defined, enables the "ob" family of functions.
** They are functionally similar to PHP's ob_start(), ob_end(), etc.
** family of functions, providing output capturing/buffering.
*/
#define TH_USE_OUTBUF
/*#undef TH_USE_OUTBUF*/
#ifndef INTERFACE
#include "blob.h"
#endif


/* This header file defines the external interface to the custom Scripting
** Language (TH) interpreter.  TH is very similar to TCL but is not an
** exact clone.
*/

/*
** Th_output_f() specifies a generic output routine for use by Th_Vtab
** and friends. Its first argument is the data to write, the second is
** the number of bytes to write, and the 3rd is an
** implementation-specific state pointer (may be NULL, depending on
** the implementation). The return value is the number of bytes output
** (which may differ from len due to encoding and whatnot).  On error
** a negative value must be returned.
*/
typedef int (*Th_output_f)( char const * zData, int len, void * pState );

struct Th_Vtab_Output {
  Th_output_f f;   /* output handler */
  void * pState;   /* final argument for xOut() */
  char enabled;    /* if 0, Th_output() does nothing. */
};
typedef struct Th_Vtab_Output Th_Vtab_Output;

/*
** Before creating an interpreter, the application must allocate and
** populate an instance of the following structure. It must remain valid
** for the lifetime of the interpreter.
*/
struct Th_Vtab {
  void *(*xRealloc)(void *, unsigned int);
  Th_Vtab_Output out;
};
typedef struct Th_Vtab Th_Vtab;


/*
** Opaque handle for interpeter.
*/
typedef struct Th_Interp Th_Interp;


/* 
** Create and delete interpreters. 
*/
Th_Interp * Th_CreateInterp(Th_Vtab *pVtab);
void Th_DeleteInterp(Th_Interp *);

/* 
** Evaluate an TH program in the stack frame identified by parameter
** iFrame, according to the following rules:
**
**   * If iFrame is 0, this means the current frame.
**
**   * If iFrame is negative, then the nth frame up the stack, where n is 
**     the absolute value of iFrame. A value of -1 means the calling
**     procedure.
**
**   * If iFrame is +ve, then the nth frame from the bottom of the stack.
**     An iFrame value of 1 means the toplevel (global) frame.
*/
int Th_Eval(Th_Interp *interp, int iFrame, const char *zProg, int nProg);

/*
** Evaluate a TH expression. The result is stored in the 
** interpreter result.
*/
int Th_Expr(Th_Interp *interp, const char *, int);

/* 
** Access TH variables in the current stack frame. If the variable name
** begins with "::", the lookup is in the top level (global) frame. 
*/
int Th_GetVar(Th_Interp *, const char *, int);
int Th_SetVar(Th_Interp *, const char *, int, const char *, int);
int Th_LinkVar(Th_Interp *, const char *, int, int, const char *, int);
int Th_UnsetVar(Th_Interp *, const char *, int);

typedef int (*Th_CommandProc)(Th_Interp *, void *, int, const char **, int *);

/* 
** Register new commands. 
*/
int Th_CreateCommand(
  Th_Interp *interp, 
  const char *zName, 
  /* int (*xProc)(Th_Interp *, void *, int, const char **, int *), */
  Th_CommandProc xProc,
  void *pContext,
  void (*xDel)(Th_Interp *, void *)
);

/* 
** Delete or rename commands.
*/
int Th_RenameCommand(Th_Interp *, const char *, int, const char *, int);

/* 
** Push a new stack frame (local variable context) onto the interpreter 
** stack, call the function supplied as parameter xCall with the two 
** context arguments, 
**
**   xCall(interp, pContext1, pContext2)
**
** , then pop the frame off of the interpreter stack. The value returned
** by the xCall() function is returned as the result of this function.
**
** This is intended for use by the implementation of commands such as
** those created by [proc].
*/
int Th_InFrame(Th_Interp *interp,
  int (*xCall)(Th_Interp *, void *pContext1, void *pContext2),
  void *pContext1,
  void *pContext2
);

/* 
** Valid return codes for xProc callbacks.
*/
#define TH_OK       0
#define TH_ERROR    1
#define TH_BREAK    2
#define TH_RETURN   3
#define TH_CONTINUE 4

/* 
** Set and get the interpreter result. 
*/
int Th_SetResult(Th_Interp *, const char *, int);
const char *Th_GetResult(Th_Interp *, int *);
char *Th_TakeResult(Th_Interp *, int *);

/*
** Set an error message as the interpreter result. This also
** sets the global stack-trace variable $::th_stack_trace.
*/
int Th_ErrorMessage(Th_Interp *, const char *, const char *, int);

/* 
** Access the memory management functions associated with the specified
** interpreter.
*/
void *Th_Malloc(Th_Interp *, int);
void Th_Free(Th_Interp *, void *);
void *Th_Realloc(Th_Interp *, void *, int);
/* 
** Functions for handling TH lists.
*/
int Th_ListAppend(Th_Interp *, char **, int *, const char *, int);
int Th_SplitList(Th_Interp *, const char *, int, char ***, int **, int *);

int Th_StringAppend(Th_Interp *, char **, int *, const char *, int);

/* 
** Functions for handling numbers and pointers.
*/
int Th_ToInt(Th_Interp *, const char *, int, int *);
int Th_ToDouble(Th_Interp *, const char *, int, double *);
int Th_SetResultInt(Th_Interp *, int);
int Th_SetResultDouble(Th_Interp *, double);
int Th_TryInt(Th_Interp *, const char * zArg, int nArg, int * piOut);
int Th_TryDouble(Th_Interp *, const char * zArg, int nArg, double * pfOut);

/*
** Drop in replacements for the corresponding standard library functions.
*/
int th_strlen(const char *);
int th_isdigit(char);
int th_isspace(char);
int th_isalnum(char);
int th_isspecial(char);
char *th_strdup(Th_Interp *interp, const char *z, int n);

/*
** Interfaces to register the language extensions.
*/
int th_register_language(Th_Interp *interp);            /* th_lang.c */
int th_register_sqlite(Th_Interp *interp);              /* th_main.c */
int th_register_argv(Th_Interp *interp);                /* th_main.c */
int th_register_vfs(Th_Interp *interp);                 /* th_vfs.c */
int th_register_testvfs(Th_Interp *interp);             /* th_testvfs.c */
int th_register_tcl(Th_Interp *interp, void *pContext); /* th_tcl.c */
int th_register_ob(Th_Interp * interp);                 /* th.c */
/*
** General purpose hash table from th_lang.c.
*/
typedef struct Th_Hash      Th_Hash;
typedef struct Th_HashEntry Th_HashEntry;
struct Th_HashEntry {
  void *pData;
  char *zKey;
  int nKey;
  Th_HashEntry *pNext;     /* Internal use only */
};
Th_Hash *Th_HashNew(Th_Interp *);
void Th_HashDelete(Th_Interp *, Th_Hash *);
void Th_HashIterate(Th_Interp*,Th_Hash*,void (*x)(Th_HashEntry*, void*),void*);
Th_HashEntry *Th_HashFind(Th_Interp*, Th_Hash*, const char*, int, int);

/*
** Useful functions from th_lang.c.
*/
int Th_WrongNumArgs(Th_Interp *interp, const char *zMsg);
/*
** Works like Th_WrongNumArgs() but expects (zCmdName,zCmdLen) to be
** the current command's (name,length), i.e. (argv[0],argl[0]).
*/
int Th_WrongNumArgs2(Th_Interp *interp, const char *zCmdName,
                     int zCmdLen, const char *zMsg);

typedef struct Th_SubCommand {char *zName; Th_CommandProc xProc;} Th_SubCommand;
int Th_CallSubCommand(Th_Interp*,void*,int,const char**,int*,Th_SubCommand*);
/*
** Works similarly to Th_CallSubCommand() but adjusts argc/argv/argl
** by 1 before passing on the call to the subcommand.
*/
int Th_CallSubCommand2(Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl, Th_SubCommand *aSub);
/*
** Sends the given data through vTab->out.f() if vTab->out.enabled is
** true, otherwise this is a no-op. Returns 0 or higher on success, *
** a negative value if vTab->out.f is NULL.
*/
int Th_Vtab_output( Th_Vtab *vTab, char const * zData, int len );

/*
** Sends the given output through pInterp's v-table's output
** implementation. See Th_Vtab_output() for the argument and
** return value semantics.
*/
int Th_output( Th_Interp *pInterp, char const * zData, int len );

/*
** Th_output_f() implementation which sends its output to either
** pState (which must be NULL or a (FILE*)) or stdout (if pState is
** NULL).
*/
int Th_output_f_FILE( char const * zData, int len, void * pState );

typedef struct Th_Command_Reg Th_Command_Reg;
/*
** A helper type for holding lists of function registration information.
** For use with Th_register_commands().
*/
struct Th_Command_Reg {
  const char *zName;     /* Function name. */
  Th_CommandProc xProc;  /* Callback function */
  void *pContext;        /* Arbitrary data for the callback. */
};

/* mkindex cannot do enums enum Th_Render_Flags { */
#define Th_Render_Flags_DEFAULT 0
#define Th_Render_Flags_NO_DOLLAR_DEREF (1 << 1)
/*};*/

int Th_Render(const char *z, int flags);

/*
** Registers a list of commands with the interpreter. pList must be a non-NULL
** pointer to an array of Th_Command_Reg objects, the last one of which MUST
** have a NULL zName field (that is the end-of-list marker).
** Returns TH_OK on success, "something else" on error.
*/
int Th_register_commands( Th_Interp * interp, Th_Command_Reg const * pList );

#ifdef TH_USE_SQLITE

/*
** Adds the given prepared statement to the interpreter. Returns the
** statements opaque identifier (a positive value). Ownerships of
** pStmt is transfered to interp and it must be cleaned up by the
** client by calling Th_FinalizeStmt(), passing it the value returned
** by this function.
**
** If interp is destroyed before all statements are finalized,
** it will finalize them but may emit a warning message.
*/
int Th_AddStmt(Th_Interp *interp, sqlite3_stmt * pStmt);

/*
** Expects stmtId to be a statement identifier returned by
** Th_AddStmt(). On success, finalizes the statement and returns 0.
** On error (statement not found) non-0 is returned. After this
** call, some subsequent call to Th_AddStmt() may return the
** same statement ID.
*/
int Th_FinalizeStmt(Th_Interp *interp, int stmtId);

/*
** Fetches the statement with the given ID, as returned by
** Th_AddStmt(). Returns NULL if stmtId does not refer (or no longer
** refers) to a statement added via Th_AddStmt().
*/
sqlite3_stmt * Th_GetStmt(Th_Interp *interp, int stmtId);
#endif

#ifdef TH_USE_OUTBUF
/*
** Manager of a stack of Blob objects for output buffering.
*/
struct Th_Ob_Man {
  Blob ** aBuf;        /* Stack of Blobs */
  int nBuf;            /* Number of blobs */
  int cursor;          /* Current level (-1=not active) */
  Th_Interp * interp;  /* The associated interpreter */
  Th_Vtab ** aVtab;    /* Stack of Vtabs (they get restored
                          when a buffering level is popped).
                          Has nBuf entries.

                          FIXME? Only swap out the "out" members, and
                          not xRealloc (that could get us into
                          trouble, but we currently only use one
                          realloc impl).
                       */
};

typedef struct Th_Ob_Man Th_Ob_Man;

/*
** Returns the ob manager for the given interpreter.
*/
Th_Ob_Man * Th_ob_manager(Th_Interp *ignored);

/*
** Returns the top-most Blob in pMan's stack, or NULL
** if buffering is not active.
*/
Blob * Th_ob_current( Th_Ob_Man * pMan );

/*
** Pushes a new blob onto pMan's stack. On success
** returns TH_OK and assigns *pOut (if pOut is not NULL)
** to the new blob (which is owned by pMan). On error
** pOut is not modified and non-0 is returned.
*/
int Th_ob_push( Th_Ob_Man * pMan, Blob ** pOut );

/*
** Pops the top-most output buffer off the stack and returns
** it. Returns NULL if there is no current buffer.  When the last
** buffer is popped, pMan's internals are cleaned up.
**
** The caller owns the returned object and must eventually call
** blob_reset() on it and Th_Free() it.
*/
Blob * Th_ob_pop( Th_Ob_Man * pMan );

#endif
/* TH_USE_OUTBUF */
