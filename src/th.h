/* This header file defines the external interface to the custom Scripting
** Language (TH) interpreter.  TH is very similar to Tcl but is not an
** exact clone.
**
** TH1 was original developed to run SQLite tests on SymbianOS.  This version
** of TH1 was repurposed as a scripted language for Fossil, and was heavily
** modified for that purpose, beginning in early 2008.
**
** More recently, TH1 has been enhanced to distinguish between regular text
** and "tainted" text.  "Tainted" text is text that might have originated
** from an outside source and hence might not be trustworthy.  To prevent
** cross-site scripting (XSS) and SQL-injections and similar attacks,
** tainted text should not be used for the following purposes:
**
**     *   executed as TH1 script or expression.
**     *   output as HTML or Javascript
**     *   used as part of an SQL query
**
** Tainted text can be converted into a safe form using commands like
** "htmlize".  And some commands ("query" and "expr") know how to use
** potentially tainted variable values directly, and thus can bypass
** the restrictions above.
**
** Whether a string is clean or tainted is determined by its length integer.
** TH1 limits strings to be no more than 0x0fffffff bytes bytes in length
** (about 268MB - more than sufficient for the purposes of Fossil).  The top
** bit of the length integer is the sign bit, of course.  The next three bits
** are reserved.  One of those, the 0x10000000 bit, marks tainted strings.
*/
#define TH1_MX_STRLEN     0x0fffffff      /* Maximum length of a TH1-C string */
#define TH1_TAINT_BIT     0x10000000      /* The taint bit */
#define TH1_SIGN          0x80000000

/* Convert an integer into a string length.  Negative values remain negative */
#define TH1_LEN(X)        ((TH1_SIGN|TH1_MX_STRLEN)&(X))

/* Return true if the string is tainted */
#define TH1_TAINTED(X)    (((X)&TH1_TAINT_BIT)!=0)

/* Remove taint from a string */
#define TH1_RM_TAINT(X)   ((X)&~TH1_TAINT_BIT)

/* Add taint to a string */
#define TH1_ADD_TAINT(X)  ((X)|TH1_TAINT_BIT)

/* If B is tainted, make A tainted too */
#define TH1_XFER_TAINT(A,B)  (A)|=(TH1_TAINT_BIT&(B))

/* Check to see if a string is too big for TH1 */
#define TH1_SIZECHECK(N)  if((N)>TH1_MX_STRLEN){Th_OversizeString();}
void Th_OversizeString(void);

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
Th_Interp * Th_CreateInterp(Th_Vtab *);
void Th_DeleteInterp(Th_Interp *);

/*
** Report taint in the string zStr,nStr.  That string represents "zTitle"
** If non-zero is returned error out of the caller.
*/
int Th_ReportTaint(Th_Interp*,const char*,const char*zStr,int nStr);

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
#if defined(TH_MEMDEBUG)
void *Th_DbgMalloc(Th_Interp *, int);
void Th_DbgFree(Th_Interp *, void *);
#endif

void *fossil_malloc_zero(size_t);
void *fossil_realloc(void *, size_t);
void fossil_free(void *);

#define Th_SysMalloc(I,N)     fossil_malloc_zero((N))
#define Th_SysRealloc(I,P,N)  fossil_realloc((P),(N))
#define Th_SysFree(I,P)       fossil_free((P))

#if defined(TH_MEMDEBUG)
#  define Th_Malloc(I,N)      Th_DbgMalloc((I),(N))
#  define Th_Free(I,P)        Th_DbgFree((I),(P))
#else
#  define Th_Malloc(I,N)      Th_SysMalloc((I),(N))
#  define Th_Realloc(I,P,N)   Th_SysRealloc((I),(P),(N))
#  define Th_Free(I,P)        Th_SysFree((I),(P))
#endif

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
