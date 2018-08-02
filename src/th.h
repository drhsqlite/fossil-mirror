
/* This header file defines the external interface to the custom Scripting
** Language (TH) interpreter.  TH is very similar to Tcl but is not an
** exact clone.
*/

/*
** Before creating an interpreter, the application must allocate and
** populate an instance of the following structure. It must remain valid
** for the lifetime of the interpreter.
*/
typedef struct Th_Vtab Th_Vtab;
struct Th_Vtab {
  void *(*xMalloc)(unsigned int);
  void (*xFree)(void *);
};

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
int Th_ExistsVar(Th_Interp *, const char *, int);
int Th_ExistsArrayVar(Th_Interp *, const char *, int);
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
#define TH_RETURN2  5

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

/*
** Functions for handling command and variable introspection.
*/
int Th_ListAppendCommands(Th_Interp *, char **, int *);
int Th_ListAppendVariables(Th_Interp *, char **, int *);
int Th_ListAppendArray(Th_Interp *, const char *, int, char **, int *);

/*
** Drop in replacements for the corresponding standard library functions.
*/
int th_strlen(const char *);
int th_isdigit(char);
int th_isspace(char);
int th_isalnum(char);
int th_isalpha(char);
int th_isspecial(char);
int th_ishexdig(char);
int th_isoctdig(char);
int th_isbindig(char);
char *th_strdup(Th_Interp *interp, const char *z, int n);

/*
** Interfaces to register the language extensions.
*/
int th_register_language(Th_Interp *interp);            /* th_lang.c */
int th_register_sqlite(Th_Interp *interp);              /* th_sqlite.c */
int th_register_vfs(Th_Interp *interp);                 /* th_vfs.c */
int th_register_testvfs(Th_Interp *interp);             /* th_testvfs.c */

#ifdef FOSSIL_ENABLE_TCL
/*
** Interfaces to the full Tcl core library from "th_tcl.c".
*/
int th_register_tcl(Th_Interp *, void *);
int unloadTcl(Th_Interp *, void *);
int evaluateTclWithEvents(Th_Interp *,void *,const char *,int,int,int,int);
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
void Th_HashIterate(Th_Interp*,Th_Hash*,int (*x)(Th_HashEntry*, void*),void*);
Th_HashEntry *Th_HashFind(Th_Interp*, Th_Hash*, const char*, int, int);

/*
** Useful functions from th_lang.c.
*/
int Th_WrongNumArgs(Th_Interp *interp, const char *zMsg);

typedef struct Th_SubCommand {const char *zName; Th_CommandProc xProc;} Th_SubCommand;
int Th_CallSubCommand(Th_Interp*,void*,int,const char**,int*,const Th_SubCommand*);
