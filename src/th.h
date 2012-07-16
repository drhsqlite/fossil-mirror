#include "config.h"

/*
** TH_ENABLE_QUERY, if defined, enables the "query" family of functions.
** They provide SELECT-only access to the repository db.
*/
#define TH_ENABLE_QUERY

/*
** TH_ENABLE_OB, if defined, enables the "ob" family of functions.
** They are functionally similar to PHP's ob_start(), ob_end(), etc.
** family of functions, providing output capturing/buffering.
*/
#define TH_ENABLE_OB

/*
** TH_ENABLE_ARGV, if defined, enables the "argv" family of functions.
** They provide access to CLI arguments as well as GET/POST arguments.
** They do not provide access to POST data submitted in JSON mode.
*/
#define TH_ENABLE_ARGV

#ifdef TH_ENABLE_OB
#ifndef INTERFACE
#include "blob.h" /* maintenance reminder: also pulls in fossil_realloc() and friends */
#endif
#endif

/* This header file defines the external interface to the custom Scripting
** Language (TH) interpreter.  TH is very similar to TCL but is not an
** exact clone.
*/

/*
** Th_Output_f() specifies a generic output routine for use by
** Th_Vtab_OutputMethods and friends. Its first argument is the data to
** write, the second is the number of bytes to write, and the 3rd is
** an implementation-specific state pointer (may be NULL, depending on
** the implementation). The return value is the number of bytes output
** (which may differ from len due to encoding and whatnot).  On error
** a negative value must be returned.
*/
typedef int (*Th_Output_f)( char const * zData, int len, void * pState );

/*
** This structure defines the output state associated with a
** Th_Vtab. It is intended that a given Vtab be able to swap out
** output back-ends during its lifetime, e.g. to form a stack of
** buffers.
*/
struct Th_Vtab_OutputMethods {
  Th_Output_f write;   /* output handler */
    void (*dispose)( void * pState ); /* Called when the framework is done with
                                         this output handler,passed this object's
                                         pState pointer.. */
  void * pState;   /* final argument for write() and dispose()*/
  char enabled;    /* if 0, Th_Output() does nothing. */
};
typedef struct Th_Vtab_OutputMethods Th_Vtab_OutputMethods;

/*
** Shared Th_Vtab_OutputMethods instance used for copy-initialization. This
** implementation uses Th_Output_f_FILE as its write() impl and
** Th_Output_dispose_FILE() for cleanup. If its pState member is NULL
** it outputs to stdout, else pState must be a (FILE*) which it will
** output to.
*/
extern const Th_Vtab_OutputMethods Th_Vtab_OutputMethods_FILE;

/*
** Before creating an interpreter, the application must allocate and
** populate an instance of the following structure. It must remain valid
** for the lifetime of the interpreter.
*/
struct Th_Vtab {
  void *(*xRealloc)(void *, unsigned int); /**
                                           Re/deallocation routine. Must behave like
                                           realloc(3), with the minor extension that
                                           realloc(anything,positiveValue) _must_ return
                                           NULL on allocation error. The Standard's wording
                                           allows realloc() to return "some value suitable for
                                           passing to free()" on error, but because client code
                                           has no way of knowing if any non-NULL value is an error
                                           value, no sane realloc() implementation would/should
                                           return anything _but_ NULL on allocation error.
                                           */
  Th_Vtab_OutputMethods out;                      /** Output handler. TH functions which generate
                                               output should send it here (via Th_Output()).
                                           */
};
typedef struct Th_Vtab Th_Vtab;


/*
** Opaque handle for interpeter.
*/
typedef struct Th_Interp Th_Interp;


/* 
** Creates a new interpreter instance using the given v-table. pVtab
** must outlive the returned object, and pVtab->out.dispose() will be
** called when the interpreter is cleaned up. The optional "ob" API
** swaps out Vtab::out instances, so pVtab->out might not be active
** for the entire lifetime of the interpreter.
**
** Potential TODO: we "should probably" add a dispose() method to the
** Th_Vtab interface.
*/
Th_Interp * Th_CreateInterp(Th_Vtab *pVtab);

/*
** Frees up all resources associated with interp then frees interp.
*/
void Th_DeleteInterp(Th_Interp *interp);

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

/*
** Typedef for Th interpreter callbacks, i.e. script-bound native C
** functions.
**
** The interp argument is the interpreter running the function. pState
** is arbitrary state which is passed to Th_CreateCommand(). arg
** contains the number of arguments (argument #0 is the command's
** name, in the same way that main()'s argv[0] is the binary's
** name). argv is the list of arguments. argl is an array argc items
** long which contains the length of each argument in the
** list. e.g. argv[0] is argl[0] bytes long.
*/
typedef int (*Th_CommandProc)(Th_Interp * interp, void * pState, int argc, const char ** argv, int * argl);

/* 
** Registers a new command with interp. zName must be a NUL-terminated
** name for the function. xProc is the native implementation of the
** function.  pContext is arbitrary data to pass as xProc()'s 2nd
** argument. xDel is an optional finalizer which should be called when
** interpreter is finalized. If xDel is not NULL then it is passed
** (interp,pContext) when interp is finalized.
**
** Return TH_OK on success.
*/
int Th_CreateCommand(
  Th_Interp *interp, 
  const char *zName, 
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
int th_register_vfs(Th_Interp *interp);                 /* th_vfs.c */
int th_register_testvfs(Th_Interp *interp);             /* th_testvfs.c */

/*
** Registers the TCL extensions. Only available if FOSSIL_ENABLE_TCL
** is enabled at compile-time.
*/
int th_register_tcl(Th_Interp *interp, void *pContext); /* th_tcl.c */

#ifdef TH_ENABLE_ARGV
/*
** Registers the "argv" API. See www/th1_argv.wiki.
*/
int th_register_argv(Th_Interp *interp);                /* th_main.c */
#endif

#ifdef TH_ENABLE_QUERY
/*
** Registers the "query" API. See www/th1_query.wiki.
*/
int th_register_query(Th_Interp *interp);              /* th_main.c */
#endif
#ifdef TH_ENABLE_OB
/*
** Registers the "ob" API. See www/th1_ob.wiki.
*/
int th_register_ob(Th_Interp * interp);                 /* th.c */
#endif

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

/*
** Generic "wrong number of arguments" helper which sets the error
** state of interp to the given message plus a generic prefix.
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
** by 1 before passing on the call to the subcommand. This allows them
** to function the same whether they are called as top-level commands
** or as sub-sub-commands.
*/
int Th_CallSubCommand2(Th_Interp *interp, void *ctx, int argc, const char **argv, int *argl, Th_SubCommand *aSub);

/*
** Sends the given data through vTab->out.f() if vTab->out.enabled is
** true, otherwise this is a no-op. Returns 0 or higher on success, *
** a negative value if vTab->out.f is NULL.
*/
int Th_Vtab_Output( Th_Vtab *vTab, char const * zData, int len );

/*
** Sends the given output through pInterp's vtab's output
** implementation. See Th_Vtab_OutputMethods() for the argument and
** return value semantics.
*/
int Th_Output( Th_Interp *pInterp, char const * zData, int len );

/*
** Enables or disables output of the current Vtab API, depending on
** whether flag is true (non-0) or false (0). Note that when output
** buffering/stacking is enabled (e.g. via the "ob" API) this modifies
** only the current output mechanism, and not any further down the
** stack.
*/
void Th_OutputEnable( Th_Interp *pInterp, char flag );

/*
** Returns true if output is enabled for the current output mechanism
** of pInterp, else false. See Th_OutputEnable().
*/
char Th_OutputEnabled( Th_Interp *pInterp );



/*
** A Th_Output_f() implementation which sends its output to either
** pState (which must be NULL or a (FILE*)) or stdout (if pState is
** NULL).
*/
int Th_Output_f_FILE( char const * zData, int len, void * pState );

/*
** A Th_Vtab_OutputMethods::dispose() impl for FILE handles. If pState is not
** one of the standard streams (stdin, stdout, stderr) then it is
** fclose()d.
*/
void Th_Output_dispose_FILE( void * pState );

/*
** A helper type for holding lists of function registration information.
** For use with Th_RegisterCommands().
*/
struct Th_Command_Reg {
  const char *zName;     /* Function name. */
  Th_CommandProc xProc;  /* Callback function */
  void *pContext;        /* Arbitrary data for the callback. */
};
typedef struct Th_Command_Reg Th_Command_Reg;

/*
** Th_Render_Flags_XXX are flags for Th_Render().
*/
/* makeheaders cannot do enums: enum Th_Render_Flags {...};*/
/*
** Default flags ("compatibility mode").
*/
#define Th_Render_Flags_DEFAULT 0
/*
** If set, Th_Render() will not process $var and $<var>
** variable references outside of TH1 blocks.
*/
#define Th_Render_Flags_NO_DOLLAR_DEREF (1 << 1)

/*
** Runs the given th1 program through Fossil's th1 interpreter. Flags
** may contain a bitmask made up of any of the Th_Render_Flags_XXX
** values.
*/
int Th_Render(const char *zTh1Program, int Th_Render_Flags);

/*
** Adds a piece of memory to the given interpreter, such that:
**
** a) it will be cleaned up when the interpreter is destroyed, by
** calling finalizer(interp, pData). The finalizer may be NULL.
** Cleanup happens in an unspecified/unpredictable order.
**
** b) it can be fetched via Th_GetData().
**
** If a given key is added more than once then any previous
** entry is cleaned up before adding it.
**
** Returns 0 on success, non-0 on allocation error.
*/
int Th_SetData( Th_Interp * interp, char const * key,
                 void * pData,
                 void (*finalizer)( Th_Interp *, void * ) );

/*
** Fetches data added via Th_SetData(), or NULL if no data
** has been associated with the given key.
*/
void * Th_GetData( Th_Interp * interp, char const * key );


/*
** Registers a list of commands with the interpreter. pList must be a non-NULL
** pointer to an array of Th_Command_Reg objects, the last one of which MUST
** have a NULL zName field (that is the end-of-list marker).
** Returns TH_OK on success, "something else" on error.
*/
int Th_RegisterCommands( Th_Interp * interp, Th_Command_Reg const * pList );

#ifdef TH_ENABLE_OB
/*
** Output buffer stack manager for TH. Used/managed by the Th_ob_xxx() functions.
*/
struct Th_Ob_Manager {
  Blob ** aBuf;        /* Stack of Blobs */
  int nBuf;            /* Number of blobs */
  int cursor;          /* Current level (-1=not active) */
  Th_Interp * interp;  /* The associated interpreter */
  Th_Vtab_OutputMethods * aOutput
                       /* Stack of output routines corresponding
                          to the current buffering level.
                          Has nBuf entries.
                       */;
};

/*
** Manager of a stack of Th_Vtab_Output objects for output buffering.
** It gets its name ("ob") from the similarly-named PHP functionality.
**
** See Th_Ob_GetManager().
**
** Potential TODO: remove the Blob from the interface and replace it
** with a Th_Output_f (or similar) which clients can pass in to have
** the data transfered from Th_Ob_Manager to them. We would also need to
** add APIs for clearing the buffer.
*/
typedef struct Th_Ob_Manager Th_Ob_Manager;

/*
** Returns the ob manager for the given interpreter. The manager gets
** installed by the th_register_ob(). In Fossil ob support is
** installed automatically if it is available at built time.
*/
Th_Ob_Manager * Th_Ob_GetManager(Th_Interp *ignored);

/*
** Returns the top-most Blob in pMan's stack, or NULL if buffering is
** not active or if the current buffering level does not refer to a
** blob. (Note: the latter will never currently be the case, but may
** be if the API is expanded to offer other output direction options,
** e.g.  (ob start file /tmp/foo.out).)
*/
Blob * Th_Ob_GetCurrentBuffer( Th_Ob_Manager * pMan );

/*
** Pushes a new blob onto pMan's stack. On success returns TH_OK and
** assigns *pOut (if pOut is not NULL) to the new blob (which is owned
** by pMan). On error pOut is not modified and non-0 is returned. The
** new blob can be cleaned up via Th_Ob_Pop() or Th_Ob_PopAndFree()
** (please read both to understand the difference!).
*/
int Th_Ob_Push( Th_Ob_Manager * pMan, Th_Vtab_OutputMethods const * pWriter, Blob ** pOut );

/*
** Pops the top-most output buffer off the stack and returns
** it. Returns NULL if there is no current buffer. When the last
** buffer is popped, pMan's internals are cleaned up (but pMan is not
** freed).
**
** The caller owns the returned object and must eventually clean it up
** by first passing it to blob_reset() and then Th_Free() it.
**
** See also: Th_Ob_PopAndFree().
*/
Blob * Th_Ob_Pop( Th_Ob_Manager * pMan );

/*
** Convenience form of Th_Ob_Pop() which pops and frees the
** top-most buffer. Returns 0 on success, non-0 if there is no
** stack to pop. Thus is can be used in a loop like:
**
** while( !Th_Ob_PopAndFree(theManager) ) {}
*/
int Th_Ob_PopAndFree( Th_Ob_Manager * pMan );

#endif
/* end TH_ENABLE_OB */
