/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This header file defines the interface that the SQLite library
** presents to client programs.  If a C-function, structure, datatype,
** or constant definition does not appear in this file, then it is
** not a published API of SQLite, is subject to change without
** notice, and should not be referenced by programs that use SQLite.
**
** Some of the definitions that are in this file are marked as
** "experimental".  Experimental interfaces are normally new
** features recently added to SQLite.  We do not anticipate changes
** to experimental interfaces but reserve the right to make minor changes
** if experience from use "in the wild" suggest such changes are prudent.
**
** The official C-language API documentation for SQLite is derived
** from comments in this file.  This file is the authoritative source
** on how SQLite interfaces are suppose to operate.
**
** The name of this file under configuration management is "sqlite.h.in".
** The makefile makes some minor changes to this file (such as inserting
** the version number) and changes its name to "sqlite4.h" as
** part of the build process.
*/
#ifndef _SQLITE4_H_
#define _SQLITE4_H_
#include <stdarg.h>     /* Needed for the definition of va_list */

/*
** Make sure we can call this stuff from C++.
*/
#ifdef __cplusplus
extern "C" {
#endif


/*
** Add the ability to override 'extern'
*/
#ifndef SQLITE4_EXTERN
# define SQLITE4_EXTERN extern
#endif

#ifndef SQLITE4_API
# define SQLITE4_API
#endif


/*
** These no-op macros are used in front of interfaces to mark those
** interfaces as either deprecated or experimental.  New applications
** should not use deprecated interfaces - they are support for backwards
** compatibility only.  Application writers should be aware that
** experimental interfaces are subject to change in point releases.
**
** These macros used to resolve to various kinds of compiler magic that
** would generate warning messages when they were used.  But that
** compiler magic ended up generating such a flurry of bug reports
** that we have taken it all out and gone back to using simple
** noop macros.
*/
#define SQLITE4_DEPRECATED
#define SQLITE4_EXPERIMENTAL

/*
** Ensure these symbols were not defined by some previous header file.
*/
#ifdef SQLITE4_VERSION
# undef SQLITE4_VERSION
#endif
#ifdef SQLITE4_VERSION_NUMBER
# undef SQLITE4_VERSION_NUMBER
#endif

/*
** CAPIREF: Run-time Environment Object
**
** An instance of the following object defines the run-time environment 
** for an SQLite4 database connection.  This object defines the interface
** to appropriate mutex routines, memory allocation routines, a
** pseudo-random number generator, real-time clock, and the key-value
** backend stores.
*/
typedef struct sqlite4_env sqlite4_env;

/*
** CAPIREF: Find the default run-time environment
**
** Return a pointer to the default run-time environment.
*/
SQLITE4_API sqlite4_env *sqlite4_env_default(void);

/*
** CAPIREF: Size of an sqlite4_env object
**
** Return the number of bytes of memory needed to hold an sqlite4_env
** object.  This number varies from one machine to another, and from
** one release of SQLite to another.
*/
SQLITE4_API int sqlite4_env_size(void);

/*
** CAPIREF: Configure a run-time environment
*/
SQLITE4_API int sqlite4_env_config(sqlite4_env*, int op, ...);

/*
** CAPIREF: Configuration options for sqlite4_env_config().
*/
#define SQLITE4_ENVCONFIG_INIT          1   /* size, template */
#define SQLITE4_ENVCONFIG_SINGLETHREAD  2   /* */
#define SQLITE4_ENVCONFIG_MULTITHREAD   3   /* */
#define SQLITE4_ENVCONFIG_SERIALIZED    4   /* */
#define SQLITE4_ENVCONFIG_MUTEX         5   /* sqlite4_mutex_methods* */
#define SQLITE4_ENVCONFIG_GETMUTEX      6   /* sqlite4_mutex_methods* */
#define SQLITE4_ENVCONFIG_MALLOC        7   /* sqlite4_mem_methods* */
#define SQLITE4_ENVCONFIG_GETMALLOC     8   /* sqlite4_mem_methods* */
#define SQLITE4_ENVCONFIG_MEMSTATUS     9   /* boolean */
#define SQLITE4_ENVCONFIG_LOOKASIDE    10   /* size, count */
#define SQLITE4_ENVCONFIG_LOG          11   /* xLog, pArg */
#define SQLITE4_ENVCONFIG_KVSTORE_PUSH 12   /* name, factory */
#define SQLITE4_ENVCONFIG_KVSTORE_POP  13   /* name */
#define SQLITE4_ENVCONFIG_KVSTORE_GET  14   /* name, *factor */


/*
** CAPIREF: Compile-Time Library Version Numbers
**
** ^(The [SQLITE4_VERSION] C preprocessor macro in the sqlite4.h header
** evaluates to a string literal that is the SQLite version in the
** format "X.Y.Z" where X is the major version number (always 3 for
** SQLite3) and Y is the minor version number and Z is the release number.)^
** ^(The [SQLITE4_VERSION_NUMBER] C preprocessor macro resolves to an integer
** with the value (X*1000000 + Y*1000 + Z) where X, Y, and Z are the same
** numbers used in [SQLITE4_VERSION].)^
** The SQLITE4_VERSION_NUMBER for any given release of SQLite will also
** be larger than the release from which it is derived.  Either Y will
** be held constant and Z will be incremented or else Y will be incremented
** and Z will be reset to zero.
**
** Since version 3.6.18, SQLite source code has been stored in the
** <a href="http://www.fossil-scm.org/">Fossil configuration management
** system</a>.  ^The SQLITE4_SOURCE_ID macro evaluates to
** a string which identifies a particular check-in of SQLite
** within its configuration management system.  ^The SQLITE4_SOURCE_ID
** string contains the date and time of the check-in (UTC) and an SHA1
** hash of the entire source tree.
**
** See also: [sqlite4_libversion()],
** [sqlite4_libversion_number()], [sqlite4_sourceid()],
** [sqlite_version()] and [sqlite_source_id()].
*/
#define SQLITE4_VERSION        "4.0.0"
#define SQLITE4_VERSION_NUMBER 4000000
#define SQLITE4_SOURCE_ID      "2012-06-29 15:58:49 2aa05e9008ff9e3630161995cdb256351cc45f9b"

/*
** CAPIREF: Run-Time Library Version Numbers
** KEYWORDS: sqlite4_version, sqlite4_sourceid
**
** These interfaces provide the same information as the [SQLITE4_VERSION],
** [SQLITE4_VERSION_NUMBER], and [SQLITE4_SOURCE_ID] C preprocessor macros
** but are associated with the library instead of the header file.  ^(Cautious
** programmers might include assert() statements in their application to
** verify that values returned by these interfaces match the macros in
** the header, and thus insure that the application is
** compiled with matching library and header files.
**
** <blockquote><pre>
** assert( sqlite4_libversion_number()==SQLITE4_VERSION_NUMBER );
** assert( strcmp(sqlite4_sourceid(),SQLITE4_SOURCE_ID)==0 );
** assert( strcmp(sqlite4_libversion(),SQLITE4_VERSION)==0 );
** </pre></blockquote>)^
**
** ^The sqlite4_libversion() function returns a pointer to a string
** constant that contains the text of [SQLITE4_VERSION].  ^The
** sqlite4_libversion_number() function returns an integer equal to
** [SQLITE4_VERSION_NUMBER].  ^The sqlite4_sourceid() function returns 
** a pointer to a string constant whose value is the same as the 
** [SQLITE4_SOURCE_ID] C preprocessor macro.
**
** See also: [sqlite_version()] and [sqlite_source_id()].
*/
SQLITE4_API const char *sqlite4_libversion(void);
SQLITE4_API const char *sqlite4_sourceid(void);
SQLITE4_API int sqlite4_libversion_number(void);

/*
** CAPIREF: Run-Time Library Compilation Options Diagnostics
**
** ^The sqlite4_compileoption_used() function returns 0 or 1 
** indicating whether the specified option was defined at 
** compile time.  ^The SQLITE4_ prefix may be omitted from the 
** option name passed to sqlite4_compileoption_used().  
**
** ^The sqlite4_compileoption_get() function allows iterating
** over the list of options that were defined at compile time by
** returning the N-th compile time option string.  ^If N is out of range,
** sqlite4_compileoption_get() returns a NULL pointer.  ^The SQLITE4_ 
** prefix is omitted from any strings returned by 
** sqlite4_compileoption_get().
**
** ^Support for the diagnostic functions sqlite4_compileoption_used()
** and sqlite4_compileoption_get() may be omitted by specifying the 
** [SQLITE4_OMIT_COMPILEOPTION_DIAGS] option at compile time.
**
** See also: SQL functions [sqlite_compileoption_used()] and
** [sqlite_compileoption_get()] and the [compile_options pragma].
*/
#ifndef SQLITE4_OMIT_COMPILEOPTION_DIAGS
SQLITE4_API int sqlite4_compileoption_used(const char *zOptName);
SQLITE4_API const char *sqlite4_compileoption_get(int N);
#endif

/*
** CAPIREF: Test To See If The Library Is Threadsafe
**
** ^The sqlite4_threadsafe(E) function returns zero if the [sqlite4_env]
** object is configured in such a way that it should only be used by a
** single thread at a time.  In other words, this routine returns zero
** if the environment is configured as [SQLITE4_ENVCONFIG_SINGLETHREAD].
**
** ^The sqlite4_threadsafe(E) function returns one if multiple
** [database connection] objects associated with E can be used at the
** same time in different threads, so long as no single [database connection]
** object is used by two or more threads at the same time.  This
** corresponds to [SQLITE4_ENVCONFIG_MULTITHREAD].
**
** ^The sqlite4_threadsafe(E) function returns two if the same
** [database connection] can be used at the same time from two or more
** separate threads.  This setting corresponds to [SQLITE4_ENVCONFIG_SERIALIZED].
**
** Note that SQLite4 is always threadsafe in this sense: Two or more
** objects each associated with different [sqlite4_env] objects can
** always be used at the same time in separate threads.
*/
SQLITE4_API int sqlite4_threadsafe(sqlite4_env*);

/*
** CAPIREF: Database Connection Handle
** KEYWORDS: {database connection} {database connections}
**
** Each open SQLite database is represented by a pointer to an instance of
** the opaque structure named "sqlite4".  It is useful to think of an sqlite4
** pointer as an object.  The [sqlite4_open()]
** interface is its constructors, and [sqlite4_close()]
** is its destructor.  There are many other interfaces (such as
** [sqlite4_prepare], [sqlite4_create_function()], and
** [sqlite4_busy_timeout()] to name but three) that are methods on an
** sqlite4 object.
*/
typedef struct sqlite4 sqlite4;

/*
** CAPIREF: 64-Bit Integer Types
** KEYWORDS: sqlite_int64 sqlite_uint64
**
** Because there is no cross-platform way to specify 64-bit integer types
** SQLite includes typedefs for 64-bit signed and unsigned integers.
**
** The sqlite4_int64 and sqlite4_uint64 are the preferred type definitions.
** The sqlite_int64 and sqlite_uint64 types are supported for backwards
** compatibility only.
**
** ^The sqlite4_int64 and sqlite_int64 types can store integer values
** between -9223372036854775808 and +9223372036854775807 inclusive.  ^The
** sqlite4_uint64 and sqlite_uint64 types can store integer values 
** between 0 and +18446744073709551615 inclusive.
*/
#ifdef SQLITE4_INT64_TYPE
  typedef SQLITE4_INT64_TYPE sqlite_int64;
  typedef unsigned SQLITE4_INT64_TYPE sqlite_uint64;
#elif defined(_MSC_VER) || defined(__BORLANDC__)
  typedef __int64 sqlite_int64;
  typedef unsigned __int64 sqlite_uint64;
#else
  typedef long long int sqlite_int64;
  typedef unsigned long long int sqlite_uint64;
#endif
typedef sqlite_int64 sqlite4_int64;
typedef sqlite_uint64 sqlite4_uint64;

/*
** CAPIREF: String length type
**
** A type for measuring the length of the string.  Like size_t but
** does not require &lt;stddef.h&gt;
*/
typedef int sqlite4_size_t;

/*
** If compiling for a processor that lacks floating point support,
** substitute integer for floating-point.
*/
#ifdef SQLITE4_OMIT_FLOATING_POINT
# define double sqlite4_int64
#endif

/*
** CAPIREF: Closing A Database Connection
**
** ^The sqlite4_close() routine is the destructor for the [sqlite4] object.
** ^Calls to sqlite4_close() return SQLITE4_OK if the [sqlite4] object is
** successfully destroyed and all associated resources are deallocated.
**
** Applications must [sqlite4_finalize | finalize] all [prepared statements]
** and [sqlite4_blob_close | close] all [BLOB handles] associated with
** the [sqlite4] object prior to attempting to close the object.  ^If
** sqlite4_close() is called on a [database connection] that still has
** outstanding [prepared statements] or [BLOB handles], then it returns
** SQLITE4_BUSY.
**
** ^If [sqlite4_close()] is invoked while a transaction is open,
** the transaction is automatically rolled back.
**
** The C parameter to [sqlite4_close(C)] must be either a NULL
** pointer or an [sqlite4] object pointer obtained
** from [sqlite4_open()] and not previously closed.
** ^Calling sqlite4_close() with a NULL pointer argument is a 
** harmless no-op.
*/
SQLITE4_API int sqlite4_close(sqlite4 *);

/*
** The type for a callback function.
** This is legacy and deprecated.  It is included for historical
** compatibility and is not documented.
*/
typedef int (*sqlite4_callback)(void*,int,char**, char**);

/*
** CAPIREF: One-Step Query Execution Interface
**
** The sqlite4_exec() interface is a convenience wrapper around
** [sqlite4_prepare()], [sqlite4_step()], and [sqlite4_finalize()],
** that allows an application to run multiple statements of SQL
** without having to use a lot of C code. 
**
** ^The sqlite4_exec() interface runs zero or more UTF-8 encoded,
** semicolon-separate SQL statements passed into its 2nd argument,
** in the context of the [database connection] passed in as its 1st
** argument.  ^If the callback function of the 3rd argument to
** sqlite4_exec() is not NULL, then it is invoked for each result row
** coming out of the evaluated SQL statements.  ^The 4th argument to
** sqlite4_exec() is relayed through to the 1st argument of each
** callback invocation.  ^If the callback pointer to sqlite4_exec()
** is NULL, then no callback is ever invoked and result rows are
** ignored.
**
** ^If an error occurs while evaluating the SQL statements passed into
** sqlite4_exec(), then execution of the current statement stops and
** subsequent statements are skipped.  ^If the 5th parameter to sqlite4_exec()
** is not NULL then any error message is written into memory obtained
** from [sqlite4_malloc()] and passed back through the 5th parameter.
** To avoid memory leaks, the application should invoke [sqlite4_free()]
** on error message strings returned through the 5th parameter of
** of sqlite4_exec() after the error message string is no longer needed.
** ^If the 5th parameter to sqlite4_exec() is not NULL and no errors
** occur, then sqlite4_exec() sets the pointer in its 5th parameter to
** NULL before returning.
**
** ^If an sqlite4_exec() callback returns non-zero, the sqlite4_exec()
** routine returns SQLITE4_ABORT without invoking the callback again and
** without running any subsequent SQL statements.
**
** ^The 2nd argument to the sqlite4_exec() callback function is the
** number of columns in the result.  ^The 3rd argument to the sqlite4_exec()
** callback is an array of pointers to strings obtained as if from
** [sqlite4_column_text()], one for each column.  ^If an element of a
** result row is NULL then the corresponding string pointer for the
** sqlite4_exec() callback is a NULL pointer.  ^The 4th argument to the
** sqlite4_exec() callback is an array of pointers to strings where each
** entry represents the name of corresponding result column as obtained
** from [sqlite4_column_name()].
**
** ^If the 2nd parameter to sqlite4_exec() is a NULL pointer, a pointer
** to an empty string, or a pointer that contains only whitespace and/or 
** SQL comments, then no SQL statements are evaluated and the database
** is not changed.
**
** Restrictions:
**
** <ul>
** <li> The application must insure that the 1st parameter to sqlite4_exec()
**      is a valid and open [database connection].
** <li> The application must not close [database connection] specified by
**      the 1st parameter to sqlite4_exec() while sqlite4_exec() is running.
** <li> The application must not modify the SQL statement text passed into
**      the 2nd parameter of sqlite4_exec() while sqlite4_exec() is running.
** </ul>
*/
SQLITE4_API int sqlite4_exec(
  sqlite4*,                                  /* An open database */
  const char *sql,                           /* SQL to be evaluated */
  int (*callback)(void*,int,char**,char**),  /* Callback function */
  void *,                                    /* 1st argument to callback */
  char **errmsg                              /* Error msg written here */
);

/*
** CAPIREF: Result Codes
** KEYWORDS: SQLITE4_OK {error code} {error codes}
** KEYWORDS: {result code} {result codes}
**
** Many SQLite functions return an integer result code from the set shown
** here in order to indicate success or failure.
**
** New error codes may be added in future versions of SQLite.
**
** See also: [SQLITE4_IOERR_READ | extended result codes],
** [sqlite4_vtab_on_conflict()] [SQLITE4_ROLLBACK | result codes].
*/
#define SQLITE4_OK           0   /* Successful result */
/* beginning-of-error-codes */
#define SQLITE4_ERROR        1   /* SQL error or missing database */
#define SQLITE4_INTERNAL     2   /* Internal logic error in SQLite */
#define SQLITE4_PERM         3   /* Access permission denied */
#define SQLITE4_ABORT        4   /* Callback routine requested an abort */
#define SQLITE4_BUSY         5   /* The database file is locked */
#define SQLITE4_LOCKED       6   /* A table in the database is locked */
#define SQLITE4_NOMEM        7   /* A malloc() failed */
#define SQLITE4_READONLY     8   /* Attempt to write a readonly database */
#define SQLITE4_INTERRUPT    9   /* Operation terminated by sqlite4_interrupt()*/
#define SQLITE4_IOERR       10   /* Some kind of disk I/O error occurred */
#define SQLITE4_CORRUPT     11   /* The database disk image is malformed */
#define SQLITE4_NOTFOUND    12   /* Unknown opcode in sqlite4_file_control() */
#define SQLITE4_FULL        13   /* Insertion failed because database is full */
#define SQLITE4_CANTOPEN    14   /* Unable to open the database file */
#define SQLITE4_PROTOCOL    15   /* Database lock protocol error */
#define SQLITE4_EMPTY       16   /* Database is empty */
#define SQLITE4_SCHEMA      17   /* The database schema changed */
#define SQLITE4_TOOBIG      18   /* String or BLOB exceeds size limit */
#define SQLITE4_CONSTRAINT  19   /* Abort due to constraint violation */
#define SQLITE4_MISMATCH    20   /* Data type mismatch */
#define SQLITE4_MISUSE      21   /* Library used incorrectly */
#define SQLITE4_NOLFS       22   /* Uses OS features not supported on host */
#define SQLITE4_AUTH        23   /* Authorization denied */
#define SQLITE4_FORMAT      24   /* Auxiliary database format error */
#define SQLITE4_RANGE       25   /* 2nd parameter to sqlite4_bind out of range */
#define SQLITE4_NOTADB      26   /* File opened that is not a database file */
#define SQLITE4_ROW         100  /* sqlite4_step() has another row ready */
#define SQLITE4_DONE        101  /* sqlite4_step() has finished executing */
#define SQLITE4_INEXACT     102  /* xSeek method of storage finds nearby ans */
/* end-of-error-codes */

/*
** CAPIREF: Extended Result Codes
** KEYWORDS: {extended error code} {extended error codes}
** KEYWORDS: {extended result code} {extended result codes}
**
** In its default configuration, SQLite API routines return one of 26 integer
** [SQLITE4_OK | result codes].  However, experience has shown that many of
** these result codes are too coarse-grained.  They do not provide as
** much information about problems as programmers might like.  In an effort to
** address this, newer versions of SQLite (version 3.3.8 and later) include
** support for additional result codes that provide more detailed information
** about errors. The extended result codes are enabled or disabled
** on a per database connection basis using the
** [sqlite4_extended_result_codes()] API.
**
** Some of the available extended result codes are listed here.
** One may expect the number of extended result codes will be expand
** over time.  Software that uses extended result codes should expect
** to see new result codes in future releases of SQLite.
**
** The SQLITE4_OK result code will never be extended.  It will always
** be exactly zero.
*/
#define SQLITE4_IOERR_READ              (SQLITE4_IOERR | (1<<8))
#define SQLITE4_IOERR_SHORT_READ        (SQLITE4_IOERR | (2<<8))
#define SQLITE4_IOERR_WRITE             (SQLITE4_IOERR | (3<<8))
#define SQLITE4_IOERR_FSYNC             (SQLITE4_IOERR | (4<<8))
#define SQLITE4_IOERR_DIR_FSYNC         (SQLITE4_IOERR | (5<<8))
#define SQLITE4_IOERR_TRUNCATE          (SQLITE4_IOERR | (6<<8))
#define SQLITE4_IOERR_FSTAT             (SQLITE4_IOERR | (7<<8))
#define SQLITE4_IOERR_UNLOCK            (SQLITE4_IOERR | (8<<8))
#define SQLITE4_IOERR_RDLOCK            (SQLITE4_IOERR | (9<<8))
#define SQLITE4_IOERR_DELETE            (SQLITE4_IOERR | (10<<8))
#define SQLITE4_IOERR_BLOCKED           (SQLITE4_IOERR | (11<<8))
#define SQLITE4_IOERR_NOMEM             (SQLITE4_IOERR | (12<<8))
#define SQLITE4_IOERR_ACCESS            (SQLITE4_IOERR | (13<<8))
#define SQLITE4_IOERR_CHECKRESERVEDLOCK (SQLITE4_IOERR | (14<<8))
#define SQLITE4_IOERR_LOCK              (SQLITE4_IOERR | (15<<8))
#define SQLITE4_IOERR_CLOSE             (SQLITE4_IOERR | (16<<8))
#define SQLITE4_IOERR_DIR_CLOSE         (SQLITE4_IOERR | (17<<8))
#define SQLITE4_IOERR_SHMOPEN           (SQLITE4_IOERR | (18<<8))
#define SQLITE4_IOERR_SHMSIZE           (SQLITE4_IOERR | (19<<8))
#define SQLITE4_IOERR_SHMLOCK           (SQLITE4_IOERR | (20<<8))
#define SQLITE4_IOERR_SHMMAP            (SQLITE4_IOERR | (21<<8))
#define SQLITE4_IOERR_SEEK              (SQLITE4_IOERR | (22<<8))
#define SQLITE4_LOCKED_SHAREDCACHE      (SQLITE4_LOCKED |  (1<<8))
#define SQLITE4_BUSY_RECOVERY           (SQLITE4_BUSY   |  (1<<8))
#define SQLITE4_CANTOPEN_NOTEMPDIR      (SQLITE4_CANTOPEN | (1<<8))
#define SQLITE4_CORRUPT_VTAB            (SQLITE4_CORRUPT | (1<<8))
#define SQLITE4_READONLY_RECOVERY       (SQLITE4_READONLY | (1<<8))
#define SQLITE4_READONLY_CANTLOCK       (SQLITE4_READONLY | (2<<8))

/*
** CAPIREF: Flags For File Open Operations
**
** These bit values are intended for use as options in the
** [sqlite4_open()] interface
*/
#define SQLITE4_OPEN_READONLY         0x00000001  /* Ok for sqlite4_open() */
#define SQLITE4_OPEN_READWRITE        0x00000002  /* Ok for sqlite4_open() */
#define SQLITE4_OPEN_CREATE           0x00000004  /* Ok for sqlite4_open() */

/* NB:  The above must not overlap with the SQLITE4_KVOPEN_xxxxx flags
** defined below */


/*
** CAPIREF: Mutex Handle
**
** The mutex module within SQLite defines [sqlite4_mutex] to be an
** abstract type for a mutex object.  The SQLite core never looks
** at the internal representation of an [sqlite4_mutex].  It only
** deals with pointers to the [sqlite4_mutex] object.
**
** Mutexes are created using [sqlite4_mutex_alloc()].
*/
typedef struct sqlite4_mutex sqlite4_mutex;
struct sqlite4_mutex {
  struct sqlite4_mutex_methods *pMutexMethods;
  /* Subclasses will typically add additional fields */
};

/*
** CAPIREF: Initialize The SQLite Library
**
** ^The sqlite4_initialize(A) routine initializes an sqlite4_env object A.
** ^The sqlite4_shutdown(A) routine
** deallocates any resources that were allocated by sqlite4_initialize(A).
**
** A call to sqlite4_initialize(A) is an "effective" call if it is
** the first time sqlite4_initialize(A) is invoked during the lifetime of
** A, or if it is the first time sqlite4_initialize(A) is invoked
** following a call to sqlite4_shutdown(A).  ^(Only an effective call
** of sqlite4_initialize(A) does any initialization or A.  All other calls
** are harmless no-ops.)^
**
** A call to sqlite4_shutdown(A) is an "effective" call if it is the first
** call to sqlite4_shutdown(A) since the last sqlite4_initialize(A).  ^(Only
** an effective call to sqlite4_shutdown(A) does any deinitialization.
** All other valid calls to sqlite4_shutdown(A) are harmless no-ops.)^
**
** The sqlite4_initialize(A) interface is threadsafe, but sqlite4_shutdown(A)
** is not.  The sqlite4_shutdown(A) interface must only be called from a
** single thread.  All open [database connections] must be closed and all
** other SQLite resources must be deallocated prior to invoking
** sqlite4_shutdown(A).
**
** ^The sqlite4_initialize(A) routine returns [SQLITE4_OK] on success.
** ^If for some reason, sqlite4_initialize(A) is unable to initialize
** the sqlite4_env object A (perhaps it is unable to allocate a needed
** resource such as a mutex) it returns an [error code] other than [SQLITE4_OK].
**
** ^The sqlite4_initialize() routine is called internally by many other
** SQLite interfaces so that an application usually does not need to
** invoke sqlite4_initialize() directly.  For example, [sqlite4_open()]
** calls sqlite4_initialize() so the SQLite library will be automatically
** initialized when [sqlite4_open()] is called if it has not be initialized
** already.  ^However, if SQLite is compiled with the [SQLITE4_OMIT_AUTOINIT]
** compile-time option, then the automatic calls to sqlite4_initialize()
** are omitted and the application must call sqlite4_initialize() directly
** prior to using any other SQLite interface.  For maximum portability,
** it is recommended that applications always invoke sqlite4_initialize()
** directly prior to using any other SQLite interface.  Future releases
** of SQLite may require this.  In other words, the behavior exhibited
** when SQLite is compiled with [SQLITE4_OMIT_AUTOINIT] might become the
** default behavior in some future release of SQLite.
*/
SQLITE4_API int sqlite4_initialize(sqlite4_env*);
SQLITE4_API int sqlite4_shutdown(sqlite4_env*);

/*
** CAPIREF: Configure database connections
**
** The sqlite4_db_config() interface is used to make configuration
** changes to a [database connection].  The interface is similar to
** [sqlite4_env_config()] except that the changes apply to a single
** [database connection] (specified in the first argument).
**
** The second argument to sqlite4_db_config(D,V,...)  is the
** [SQLITE4_DBCONFIG_LOOKASIDE | configuration verb] - an integer code 
** that indicates what aspect of the [database connection] is being configured.
** Subsequent arguments vary depending on the configuration verb.
**
** ^Calls to sqlite4_db_config() return SQLITE4_OK if and only if
** the call is considered successful.
*/
SQLITE4_API int sqlite4_db_config(sqlite4*, int op, ...);

/*
** CAPIREF: Run-time environment of a database connection
**
** Return the sqlite4_env object to which the database connection
** belongs.
*/
SQLITE4_API sqlite4_env *sqlite4_db_env(sqlite4*);

/*
** CAPIREF: Memory Allocation Routines
**
** An instance of this object defines the interface between SQLite
** and low-level memory allocation routines.
**
** This object is used in only one place in the SQLite interface.
** A pointer to an instance of this object is the argument to
** [sqlite4_env_config()] when the configuration option is
** [SQLITE4_ENVCONFIG_MALLOC] or [SQLITE4_ENVCONFIG_GETMALLOC].  
** By creating an instance of this object
** and passing it to [sqlite4_env_config]([SQLITE4_ENVCONFIG_MALLOC])
** during configuration, an application can specify an alternative
** memory allocation subsystem for SQLite to use for all of its
** dynamic memory needs.
**
** Note that SQLite comes with several [built-in memory allocators]
** that are perfectly adequate for the overwhelming majority of applications
** and that this object is only useful to a tiny minority of applications
** with specialized memory allocation requirements.  This object is
** also used during testing of SQLite in order to specify an alternative
** memory allocator that simulates memory out-of-memory conditions in
** order to verify that SQLite recovers gracefully from such
** conditions.
**
** The xMalloc, xRealloc, and xFree methods must work like the
** malloc(), realloc() and free() functions from the standard C library.
** ^SQLite guarantees that the second argument to
** xRealloc is always a value returned by a prior call to xRoundup.
**
** xSize should return the allocated size of a memory allocation
** previously obtained from xMalloc or xRealloc.  The allocated size
** is always at least as big as the requested size but may be larger.
**
** The xRoundup method returns what would be the allocated size of
** a memory allocation given a particular requested size.  Most memory
** allocators round up memory allocations at least to the next multiple
** of 8.  Some allocators round up to a larger multiple or to a power of 2.
** Every memory allocation request coming in through [sqlite4_malloc()]
** or [sqlite4_realloc()] first calls xRoundup.  If xRoundup returns 0, 
** that causes the corresponding memory allocation to fail.
**
** The xInit method initializes the memory allocator.  (For example,
** it might allocate any require mutexes or initialize internal data
** structures.  The xShutdown method is invoked (indirectly) by
** [sqlite4_shutdown()] and should deallocate any resources acquired
** by xInit.  The pMemEnv pointer is used as the only parameter to
** xInit and xShutdown.
**
** SQLite holds the [SQLITE4_MUTEX_STATIC_MASTER] mutex when it invokes
** the xInit method, so the xInit method need not be threadsafe.  The
** xShutdown method is only called from [sqlite4_shutdown()] so it does
** not need to be threadsafe either.  For all other methods, SQLite
** holds the [SQLITE4_MUTEX_STATIC_MEM] mutex as long as the
** [SQLITE4_CONFIG_MEMSTATUS] configuration option is turned on (which
** it is by default) and so the methods are automatically serialized.
** However, if [SQLITE4_CONFIG_MEMSTATUS] is disabled, then the other
** methods must be threadsafe or else make their own arrangements for
** serialization.
**
** SQLite will never invoke xInit() more than once without an intervening
** call to xShutdown().
*/
typedef struct sqlite4_mem_methods sqlite4_mem_methods;
struct sqlite4_mem_methods {
  void *(*xMalloc)(void*,sqlite4_size_t); /* Memory allocation function */
  void (*xFree)(void*,void*);             /* Free a prior allocation */
  void *(*xRealloc)(void*,void*,int);     /* Resize an allocation */
  sqlite4_size_t (*xSize)(void*,void*);   /* Return the size of an allocation */
  int (*xInit)(void*);                    /* Initialize the memory allocator */
  void (*xShutdown)(void*);               /* Deinitialize the allocator */
  void (*xBeginBenign)(void*);            /* Enter a benign malloc region */
  void (*xEndBenign)(void*);              /* Leave a benign malloc region */
  void *pMemEnv;                         /* 1st argument to all routines */
};


/*
** CAPIREF: Database Connection Configuration Options
**
** These constants are the available integer configuration options that
** can be passed as the second argument to the [sqlite4_db_config()] interface.
**
** New configuration options may be added in future releases of SQLite.
** Existing configuration options might be discontinued.  Applications
** should check the return code from [sqlite4_db_config()] to make sure that
** the call worked.  ^The [sqlite4_db_config()] interface will return a
** non-zero [error code] if a discontinued or unsupported configuration option
** is invoked.
**
** <dl>
** <dt>SQLITE4_DBCONFIG_LOOKASIDE</dt>
** <dd> ^This option takes three additional arguments that determine the 
** [lookaside memory allocator] configuration for the [database connection].
** ^The first argument (the third parameter to [sqlite4_db_config()] is a
** pointer to a memory buffer to use for lookaside memory.
** ^The first argument after the SQLITE4_DBCONFIG_LOOKASIDE verb
** may be NULL in which case SQLite will allocate the
** lookaside buffer itself using [sqlite4_malloc()]. ^The second argument is the
** size of each lookaside buffer slot.  ^The third argument is the number of
** slots.  The size of the buffer in the first argument must be greater than
** or equal to the product of the second and third arguments.  The buffer
** must be aligned to an 8-byte boundary.  ^If the second argument to
** SQLITE4_DBCONFIG_LOOKASIDE is not a multiple of 8, it is internally
** rounded down to the next smaller multiple of 8.  ^(The lookaside memory
** configuration for a database connection can only be changed when that
** connection is not currently using lookaside memory, or in other words
** when the "current value" returned by
** [sqlite4_db_status](D,[SQLITE4_CONFIG_LOOKASIDE],...) is zero.
** Any attempt to change the lookaside memory configuration when lookaside
** memory is in use leaves the configuration unchanged and returns 
** [SQLITE4_BUSY].)^</dd>
**
** <dt>SQLITE4_DBCONFIG_ENABLE_FKEY</dt>
** <dd> ^This option is used to enable or disable the enforcement of
** [foreign key constraints].  There should be two additional arguments.
** The first argument is an integer which is 0 to disable FK enforcement,
** positive to enable FK enforcement or negative to leave FK enforcement
** unchanged.  The second parameter is a pointer to an integer into which
** is written 0 or 1 to indicate whether FK enforcement is off or on
** following this call.  The second parameter may be a NULL pointer, in
** which case the FK enforcement setting is not reported back. </dd>
**
** <dt>SQLITE4_DBCONFIG_ENABLE_TRIGGER</dt>
** <dd> ^This option is used to enable or disable [CREATE TRIGGER | triggers].
** There should be two additional arguments.
** The first argument is an integer which is 0 to disable triggers,
** positive to enable triggers or negative to leave the setting unchanged.
** The second parameter is a pointer to an integer into which
** is written 0 or 1 to indicate whether triggers are disabled or enabled
** following this call.  The second parameter may be a NULL pointer, in
** which case the trigger setting is not reported back. </dd>
**
** </dl>
*/
#define SQLITE4_DBCONFIG_LOOKASIDE       1001  /* void* int int */
#define SQLITE4_DBCONFIG_ENABLE_FKEY     1002  /* int int* */
#define SQLITE4_DBCONFIG_ENABLE_TRIGGER  1003  /* int int* */


/*
** CAPIREF: Last Insert Rowid
**
** ^Each entry in an SQLite table has a unique 64-bit signed
** integer key called the [ROWID | "rowid"]. ^The rowid is always available
** as an undeclared column named ROWID, OID, or _ROWID_ as long as those
** names are not also used by explicitly declared columns. ^If
** the table has a column of type [INTEGER PRIMARY KEY] then that column
** is another alias for the rowid.
**
** ^This routine returns the [rowid] of the most recent
** successful [INSERT] into the database from the [database connection]
** in the first argument.  ^As of SQLite version 3.7.7, this routines
** records the last insert rowid of both ordinary tables and [virtual tables].
** ^If no successful [INSERT]s
** have ever occurred on that database connection, zero is returned.
**
** ^(If an [INSERT] occurs within a trigger or within a [virtual table]
** method, then this routine will return the [rowid] of the inserted
** row as long as the trigger or virtual table method is running.
** But once the trigger or virtual table method ends, the value returned 
** by this routine reverts to what it was before the trigger or virtual
** table method began.)^
**
** ^An [INSERT] that fails due to a constraint violation is not a
** successful [INSERT] and does not change the value returned by this
** routine.  ^Thus INSERT OR FAIL, INSERT OR IGNORE, INSERT OR ROLLBACK,
** and INSERT OR ABORT make no changes to the return value of this
** routine when their insertion fails.  ^(When INSERT OR REPLACE
** encounters a constraint violation, it does not fail.  The
** INSERT continues to completion after deleting rows that caused
** the constraint problem so INSERT OR REPLACE will always change
** the return value of this interface.)^
**
** ^For the purposes of this routine, an [INSERT] is considered to
** be successful even if it is subsequently rolled back.
**
** This function is accessible to SQL statements via the
** [last_insert_rowid() SQL function].
**
** If a separate thread performs a new [INSERT] on the same
** database connection while the [sqlite4_last_insert_rowid()]
** function is running and thus changes the last insert [rowid],
** then the value returned by [sqlite4_last_insert_rowid()] is
** unpredictable and might not equal either the old or the new
** last insert [rowid].
*/
SQLITE4_API sqlite4_int64 sqlite4_last_insert_rowid(sqlite4*);

/*
** CAPIREF: Count The Number Of Rows Modified
**
** ^This function returns the number of database rows that were changed
** or inserted or deleted by the most recently completed SQL statement
** on the [database connection] specified by the first parameter.
** ^(Only changes that are directly specified by the [INSERT], [UPDATE],
** or [DELETE] statement are counted.  Auxiliary changes caused by
** triggers or [foreign key actions] are not counted.)^ Use the
** [sqlite4_total_changes()] function to find the total number of changes
** including changes caused by triggers and foreign key actions.
**
** ^Changes to a view that are simulated by an [INSTEAD OF trigger]
** are not counted.  Only real table changes are counted.
**
** ^(A "row change" is a change to a single row of a single table
** caused by an INSERT, DELETE, or UPDATE statement.  Rows that
** are changed as side effects of [REPLACE] constraint resolution,
** rollback, ABORT processing, [DROP TABLE], or by any other
** mechanisms do not count as direct row changes.)^
**
** A "trigger context" is a scope of execution that begins and
** ends with the script of a [CREATE TRIGGER | trigger]. 
** Most SQL statements are
** evaluated outside of any trigger.  This is the "top level"
** trigger context.  If a trigger fires from the top level, a
** new trigger context is entered for the duration of that one
** trigger.  Subtriggers create subcontexts for their duration.
**
** ^Calling [sqlite4_exec()] or [sqlite4_step()] recursively does
** not create a new trigger context.
**
** ^This function returns the number of direct row changes in the
** most recent INSERT, UPDATE, or DELETE statement within the same
** trigger context.
**
** ^Thus, when called from the top level, this function returns the
** number of changes in the most recent INSERT, UPDATE, or DELETE
** that also occurred at the top level.  ^(Within the body of a trigger,
** the sqlite4_changes() interface can be called to find the number of
** changes in the most recently completed INSERT, UPDATE, or DELETE
** statement within the body of the same trigger.
** However, the number returned does not include changes
** caused by subtriggers since those have their own context.)^
**
** See also the [sqlite4_total_changes()] interface, the
** [count_changes pragma], and the [changes() SQL function].
**
** If a separate thread makes changes on the same database connection
** while [sqlite4_changes()] is running then the value returned
** is unpredictable and not meaningful.
*/
SQLITE4_API int sqlite4_changes(sqlite4*);

/*
** CAPIREF: Total Number Of Rows Modified
**
** ^This function returns the number of row changes caused by [INSERT],
** [UPDATE] or [DELETE] statements since the [database connection] was opened.
** ^(The count returned by sqlite4_total_changes() includes all changes
** from all [CREATE TRIGGER | trigger] contexts and changes made by
** [foreign key actions]. However,
** the count does not include changes used to implement [REPLACE] constraints,
** do rollbacks or ABORT processing, or [DROP TABLE] processing.  The
** count does not include rows of views that fire an [INSTEAD OF trigger],
** though if the INSTEAD OF trigger makes changes of its own, those changes 
** are counted.)^
** ^The sqlite4_total_changes() function counts the changes as soon as
** the statement that makes them is completed (when the statement handle
** is passed to [sqlite4_reset()] or [sqlite4_finalize()]).
**
** See also the [sqlite4_changes()] interface, the
** [count_changes pragma], and the [total_changes() SQL function].
**
** If a separate thread makes changes on the same database connection
** while [sqlite4_total_changes()] is running then the value
** returned is unpredictable and not meaningful.
*/
SQLITE4_API int sqlite4_total_changes(sqlite4*);

/*
** CAPIREF: Interrupt A Long-Running Query
**
** ^This function causes any pending database operation to abort and
** return at its earliest opportunity. This routine is typically
** called in response to a user action such as pressing "Cancel"
** or Ctrl-C where the user wants a long query operation to halt
** immediately.
**
** ^It is safe to call this routine from a thread different from the
** thread that is currently running the database operation.  But it
** is not safe to call this routine with a [database connection] that
** is closed or might close before sqlite4_interrupt() returns.
**
** ^If an SQL operation is very nearly finished at the time when
** sqlite4_interrupt() is called, then it might not have an opportunity
** to be interrupted and might continue to completion.
**
** ^An SQL operation that is interrupted will return [SQLITE4_INTERRUPT].
** ^If the interrupted SQL operation is an INSERT, UPDATE, or DELETE
** that is inside an explicit transaction, then the entire transaction
** will be rolled back automatically.
**
** ^The sqlite4_interrupt(D) call is in effect until all currently running
** SQL statements on [database connection] D complete.  ^Any new SQL statements
** that are started after the sqlite4_interrupt() call and before the 
** running statements reaches zero are interrupted as if they had been
** running prior to the sqlite4_interrupt() call.  ^New SQL statements
** that are started after the running statement count reaches zero are
** not effected by the sqlite4_interrupt().
** ^A call to sqlite4_interrupt(D) that occurs when there are no running
** SQL statements is a no-op and has no effect on SQL statements
** that are started after the sqlite4_interrupt() call returns.
**
** If the database connection closes while [sqlite4_interrupt()]
** is running then bad things will likely happen.
*/
SQLITE4_API void sqlite4_interrupt(sqlite4*);

/*
** CAPIREF: Determine If An SQL Statement Is Complete
**
** These routines are useful during command-line input to determine if the
** currently entered text seems to form a complete SQL statement or
** if additional input is needed before sending the text into
** SQLite for parsing.  ^These routines return 1 if the input string
** appears to be a complete SQL statement.  ^A statement is judged to be
** complete if it ends with a semicolon token and is not a prefix of a
** well-formed CREATE TRIGGER statement.  ^Semicolons that are embedded within
** string literals or quoted identifier names or comments are not
** independent tokens (they are part of the token in which they are
** embedded) and thus do not count as a statement terminator.  ^Whitespace
** and comments that follow the final semicolon are ignored.
**
** ^These routines return 0 if the statement is incomplete.  ^If a
** memory allocation fails, then SQLITE4_NOMEM is returned.
**
** ^These routines do not parse the SQL statements thus
** will not detect syntactically incorrect SQL.
**
** ^(If SQLite has not been initialized using [sqlite4_initialize()] prior 
** to invoking sqlite4_complete16() then sqlite4_initialize() is invoked
** automatically by sqlite4_complete16().  If that initialization fails,
** then the return value from sqlite4_complete16() will be non-zero
** regardless of whether or not the input SQL is complete.)^
**
** The input to [sqlite4_complete()] must be a zero-terminated
** UTF-8 string.
**
** The input to [sqlite4_complete16()] must be a zero-terminated
** UTF-16 string in native byte order.
*/
SQLITE4_API int sqlite4_complete(const char *sql);
SQLITE4_API int sqlite4_complete16(const void *sql);


/*
** CAPIREF: Formatted String Printing Functions
**
** These routines are work-alikes of the "printf()" family of functions
** from the standard C library.
**
** ^The sqlite4_mprintf() and sqlite4_vmprintf() routines write their
** results into memory obtained from [sqlite4_malloc()].
** The strings returned by these two routines should be
** released by [sqlite4_free()].  ^Both routines return a
** NULL pointer if [sqlite4_malloc()] is unable to allocate enough
** memory to hold the resulting string.
**
** ^(The sqlite4_snprintf() routine is similar to "snprintf()" from
** the standard C library.  The result is written into the
** buffer supplied as the first parameter whose size is given by
** the second parameter.)^  The return value from sqltie4_snprintf()
** is the number of bytes actually written into the buffer, not
** counting the zero terminator.  The buffer is always zero-terminated
** as long as it it at least one byte in length.
**
** The sqlite4_snprintf() differs from the standard library snprintf()
** routine in two ways:  (1) sqlite4_snprintf() returns the number of
** bytes actually written, not the number of bytes that would have been
** written if the buffer had been infinitely long.  (2) If the buffer is
** at least one byte long, sqlite4_snprintf() always zero-terminates its
** result.
**
** ^As long as the buffer size is greater than zero, sqlite4_snprintf()
** guarantees that the buffer is always zero-terminated.  ^The second
** parameter "n" is the total size of the buffer, including space for
** the zero terminator.  So the longest string that can be completely
** written will be n-1 characters.
**
** ^The sqlite4_vsnprintf() routine is a varargs version of sqlite4_snprintf().
**
** These routines all implement some additional formatting
** options that are useful for constructing SQL statements.
** All of the usual printf() formatting options apply.  In addition, there
** is are "%q", "%Q", and "%z" options.
**
** ^(The %q option works like %s in that it substitutes a nul-terminated
** string from the argument list.  But %q also doubles every '\'' character.
** %q is designed for use inside a string literal.)^  By doubling each '\''
** character it escapes that character and allows it to be inserted into
** the string.
**
** For example, assume the string variable zText contains text as follows:
**
** <blockquote><pre>
**  char *zText = "It's a happy day!";
** </pre></blockquote>
**
** One can use this text in an SQL statement as follows:
**
** <blockquote><pre>
**  char *zSQL = sqlite4_mprintf("INSERT INTO table VALUES('%q')", zText);
**  sqlite4_exec(db, zSQL, 0, 0, 0);
**  sqlite4_free(zSQL);
** </pre></blockquote>
**
** Because the %q format string is used, the '\'' character in zText
** is escaped and the SQL generated is as follows:
**
** <blockquote><pre>
**  INSERT INTO table1 VALUES('It''s a happy day!')
** </pre></blockquote>
**
** This is correct.  Had we used %s instead of %q, the generated SQL
** would have looked like this:
**
** <blockquote><pre>
**  INSERT INTO table1 VALUES('It's a happy day!');
** </pre></blockquote>
**
** This second example is an SQL syntax error.  As a general rule you should
** always use %q instead of %s when inserting text into a string literal.
**
** ^(The %Q option works like %q except it also adds single quotes around
** the outside of the total string.  Additionally, if the parameter in the
** argument list is a NULL pointer, %Q substitutes the text "NULL" (without
** single quotes).)^  So, for example, one could say:
**
** <blockquote><pre>
**  char *zSQL = sqlite4_mprintf("INSERT INTO table VALUES(%Q)", zText);
**  sqlite4_exec(db, zSQL, 0, 0, 0);
**  sqlite4_free(zSQL);
** </pre></blockquote>
**
** The code above will render a correct SQL statement in the zSQL
** variable even if the zText variable is a NULL pointer.
**
** ^(The "%z" formatting option works like "%s" but with the
** addition that after the string has been read and copied into
** the result, [sqlite4_free()] is called on the input string.)^
*/
SQLITE4_API char *sqlite4_mprintf(sqlite4_env*, const char*,...);
SQLITE4_API char *sqlite4_vmprintf(sqlite4_env*, const char*, va_list);
SQLITE4_API sqlite4_size_t sqlite4_snprintf(char*,sqlite4_size_t,const char*, ...);
SQLITE4_API sqlite4_size_t sqlite4_vsnprintf(char*,sqlite4_size_t,const char*, va_list);

/*
** CAPIREF: Memory Allocation Subsystem
**
** The SQLite core uses these three routines for all of its own
** internal memory allocation needs.
**
** ^The sqlite4_malloc() routine returns a pointer to a block
** of memory at least N bytes in length, where N is the parameter.
** ^If sqlite4_malloc() is unable to obtain sufficient free
** memory, it returns a NULL pointer.  ^If the parameter N to
** sqlite4_malloc() is zero or negative then sqlite4_malloc() returns
** a NULL pointer.
**
** ^Calling sqlite4_free() with a pointer previously returned
** by sqlite4_malloc() or sqlite4_realloc() releases that memory so
** that it might be reused.  ^The sqlite4_free() routine is
** a no-op if is called with a NULL pointer.  Passing a NULL pointer
** to sqlite4_free() is harmless.  After being freed, memory
** should neither be read nor written.  Even reading previously freed
** memory might result in a segmentation fault or other severe error.
** Memory corruption, a segmentation fault, or other severe error
** might result if sqlite4_free() is called with a non-NULL pointer that
** was not obtained from sqlite4_malloc() or sqlite4_realloc().
**
** ^(The sqlite4_realloc() interface attempts to resize a
** prior memory allocation to be at least N bytes, where N is the
** second parameter.  The memory allocation to be resized is the first
** parameter.)^ ^ If the first parameter to sqlite4_realloc()
** is a NULL pointer then its behavior is identical to calling
** sqlite4_malloc(N) where N is the second parameter to sqlite4_realloc().
** ^If the second parameter to sqlite4_realloc() is zero or
** negative then the behavior is exactly the same as calling
** sqlite4_free(P) where P is the first parameter to sqlite4_realloc().
** ^sqlite4_realloc() returns a pointer to a memory allocation
** of at least N bytes in size or NULL if sufficient memory is unavailable.
** ^If M is the size of the prior allocation, then min(N,M) bytes
** of the prior allocation are copied into the beginning of buffer returned
** by sqlite4_realloc() and the prior allocation is freed.
** ^If sqlite4_realloc() returns NULL, then the prior allocation
** is not freed.
**
** ^The memory returned by sqlite4_malloc() and sqlite4_realloc()
** is always aligned to at least an 8 byte boundary, or to a
** 4 byte boundary if the [SQLITE4_4_BYTE_ALIGNED_MALLOC] compile-time
** option is used.
**
** The pointer arguments to [sqlite4_free()] and [sqlite4_realloc()]
** must be either NULL or else pointers obtained from a prior
** invocation of [sqlite4_malloc()] or [sqlite4_realloc()] that have
** not yet been released.
**
** The application must not read or write any part of
** a block of memory after it has been released using
** [sqlite4_free()] or [sqlite4_realloc()].
*/
SQLITE4_API void *sqlite4_malloc(sqlite4_env*, sqlite4_size_t);
SQLITE4_API void *sqlite4_realloc(sqlite4_env*, void*, sqlite4_size_t);
SQLITE4_API void sqlite4_free(sqlite4_env*, void*);

/*
** CAPIREF: Memory Allocator Statistics
**
** SQLite provides these two interfaces for reporting on the status
** of the [sqlite4_malloc()], [sqlite4_free()], and [sqlite4_realloc()]
** routines, which form the built-in memory allocation subsystem.
**
** ^The [sqlite4_memory_used(E)] routine returns the number of bytes
** of memory currently outstanding (malloced but not freed) for 
** sqlite4_env environment E.
** ^The [sqlite4_memory_highwater(E)] routine returns the maximum
** value of [sqlite4_memory_used(E)] since the high-water mark
** was last reset.  ^The values returned by [sqlite4_memory_used()] and
** [sqlite4_memory_highwater()] include any overhead
** added by SQLite in its implementation of [sqlite4_malloc()],
** but not overhead added by the any underlying system library
** routines that [sqlite4_malloc()] may call.
**
** ^The memory high-water mark is reset to the current value of
** [sqlite4_memory_used(E)] if and only if the R parameter to
** [sqlite4_memory_highwater(E,R)] is true.  ^The value returned
** by [sqlite4_memory_highwater(E,1)] is the high-water mark
** prior to the reset.
*/
SQLITE4_API sqlite4_uint64 sqlite4_memory_used(sqlite4_env*);
SQLITE4_API sqlite4_uint64 sqlite4_memory_highwater(sqlite4_env*, int resetFlag);

/*
** CAPIREF: Pseudo-Random Number Generator
**
** ^A call to this routine stores N bytes of pseudo-randomness into buffer P.
*/
SQLITE4_API void sqlite4_randomness(sqlite4_env*, int N, void *P);

/*
** CAPIREF: Compile-Time Authorization Callbacks
**
** ^This routine registers an authorizer callback with a particular
** [database connection], supplied in the first argument.
** ^The authorizer callback is invoked as SQL statements are being compiled
** by [sqlite4_prepare()] or its variants [sqlite4_prepare()],
** [sqlite4_prepare16()] and [sqlite4_prepare16_v2()].  ^At various
** points during the compilation process, as logic is being created
** to perform various actions, the authorizer callback is invoked to
** see if those actions are allowed.  ^The authorizer callback should
** return [SQLITE4_OK] to allow the action, [SQLITE4_IGNORE] to disallow the
** specific action but allow the SQL statement to continue to be
** compiled, or [SQLITE4_DENY] to cause the entire SQL statement to be
** rejected with an error.  ^If the authorizer callback returns
** any value other than [SQLITE4_IGNORE], [SQLITE4_OK], or [SQLITE4_DENY]
** then the [sqlite4_prepare()] or equivalent call that triggered
** the authorizer will fail with an error message.
**
** When the callback returns [SQLITE4_OK], that means the operation
** requested is ok.  ^When the callback returns [SQLITE4_DENY], the
** [sqlite4_prepare()] or equivalent call that triggered the
** authorizer will fail with an error message explaining that
** access is denied. 
**
** ^The first parameter to the authorizer callback is a copy of the third
** parameter to the sqlite4_set_authorizer() interface. ^The second parameter
** to the callback is an integer [SQLITE4_COPY | action code] that specifies
** the particular action to be authorized. ^The third through sixth parameters
** to the callback are zero-terminated strings that contain additional
** details about the action to be authorized.
**
** ^If the action code is [SQLITE4_READ]
** and the callback returns [SQLITE4_IGNORE] then the
** [prepared statement] statement is constructed to substitute
** a NULL value in place of the table column that would have
** been read if [SQLITE4_OK] had been returned.  The [SQLITE4_IGNORE]
** return can be used to deny an untrusted user access to individual
** columns of a table.
** ^If the action code is [SQLITE4_DELETE] and the callback returns
** [SQLITE4_IGNORE] then the [DELETE] operation proceeds but the
** [truncate optimization] is disabled and all rows are deleted individually.
**
** An authorizer is used when [sqlite4_prepare | preparing]
** SQL statements from an untrusted source, to ensure that the SQL statements
** do not try to access data they are not allowed to see, or that they do not
** try to execute malicious statements that damage the database.  For
** example, an application may allow a user to enter arbitrary
** SQL queries for evaluation by a database.  But the application does
** not want the user to be able to make arbitrary changes to the
** database.  An authorizer could then be put in place while the
** user-entered SQL is being [sqlite4_prepare | prepared] that
** disallows everything except [SELECT] statements.
**
** Applications that need to process SQL from untrusted sources
** might also consider lowering resource limits using [sqlite4_limit()]
** and limiting database size using the [max_page_count] [PRAGMA]
** in addition to using an authorizer.
**
** ^(Only a single authorizer can be in place on a database connection
** at a time.  Each call to sqlite4_set_authorizer overrides the
** previous call.)^  ^Disable the authorizer by installing a NULL callback.
** The authorizer is disabled by default.
**
** The authorizer callback must not do anything that will modify
** the database connection that invoked the authorizer callback.
** Note that [sqlite4_prepare()] and [sqlite4_step()] both modify their
** database connections for the meaning of "modify" in this paragraph.
**
** ^When [sqlite4_prepare()] is used to prepare a statement, the
** statement might be re-prepared during [sqlite4_step()] due to a 
** schema change.  Hence, the application should ensure that the
** correct authorizer callback remains in place during the [sqlite4_step()].
**
** ^Note that the authorizer callback is invoked only during
** [sqlite4_prepare()] or its variants.  Authorization is not
** performed during statement evaluation in [sqlite4_step()], unless
** as stated in the previous paragraph, sqlite4_step() invokes
** sqlite4_prepare() to reprepare a statement after a schema change.
*/
SQLITE4_API int sqlite4_set_authorizer(
  sqlite4*,
  int (*xAuth)(void*,int,const char*,const char*,const char*,const char*),
  void *pUserData
);

/*
** CAPIREF: Authorizer Return Codes
**
** The [sqlite4_set_authorizer | authorizer callback function] must
** return either [SQLITE4_OK] or one of these two constants in order
** to signal SQLite whether or not the action is permitted.  See the
** [sqlite4_set_authorizer | authorizer documentation] for additional
** information.
**
** Note that SQLITE4_IGNORE is also used as a [SQLITE4_ROLLBACK | return code]
** from the [sqlite4_vtab_on_conflict()] interface.
*/
#define SQLITE4_DENY   1   /* Abort the SQL statement with an error */
#define SQLITE4_IGNORE 2   /* Don't allow access, but don't generate an error */

/*
** CAPIREF: Authorizer Action Codes
**
** The [sqlite4_set_authorizer()] interface registers a callback function
** that is invoked to authorize certain SQL statement actions.  The
** second parameter to the callback is an integer code that specifies
** what action is being authorized.  These are the integer action codes that
** the authorizer callback may be passed.
**
** These action code values signify what kind of operation is to be
** authorized.  The 3rd and 4th parameters to the authorization
** callback function will be parameters or NULL depending on which of these
** codes is used as the second parameter.  ^(The 5th parameter to the
** authorizer callback is the name of the database ("main", "temp",
** etc.) if applicable.)^  ^The 6th parameter to the authorizer callback
** is the name of the inner-most trigger or view that is responsible for
** the access attempt or NULL if this access attempt is directly from
** top-level SQL code.
*/
/******************************************* 3rd ************ 4th ***********/
#define SQLITE4_CREATE_INDEX          1   /* Index Name      Table Name      */
#define SQLITE4_CREATE_TABLE          2   /* Table Name      NULL            */
#define SQLITE4_CREATE_TEMP_INDEX     3   /* Index Name      Table Name      */
#define SQLITE4_CREATE_TEMP_TABLE     4   /* Table Name      NULL            */
#define SQLITE4_CREATE_TEMP_TRIGGER   5   /* Trigger Name    Table Name      */
#define SQLITE4_CREATE_TEMP_VIEW      6   /* View Name       NULL            */
#define SQLITE4_CREATE_TRIGGER        7   /* Trigger Name    Table Name      */
#define SQLITE4_CREATE_VIEW           8   /* View Name       NULL            */
#define SQLITE4_DELETE                9   /* Table Name      NULL            */
#define SQLITE4_DROP_INDEX           10   /* Index Name      Table Name      */
#define SQLITE4_DROP_TABLE           11   /* Table Name      NULL            */
#define SQLITE4_DROP_TEMP_INDEX      12   /* Index Name      Table Name      */
#define SQLITE4_DROP_TEMP_TABLE      13   /* Table Name      NULL            */
#define SQLITE4_DROP_TEMP_TRIGGER    14   /* Trigger Name    Table Name      */
#define SQLITE4_DROP_TEMP_VIEW       15   /* View Name       NULL            */
#define SQLITE4_DROP_TRIGGER         16   /* Trigger Name    Table Name      */
#define SQLITE4_DROP_VIEW            17   /* View Name       NULL            */
#define SQLITE4_INSERT               18   /* Table Name      NULL            */
#define SQLITE4_PRAGMA               19   /* Pragma Name     1st arg or NULL */
#define SQLITE4_READ                 20   /* Table Name      Column Name     */
#define SQLITE4_SELECT               21   /* NULL            NULL            */
#define SQLITE4_TRANSACTION          22   /* Operation       NULL            */
#define SQLITE4_UPDATE               23   /* Table Name      Column Name     */
#define SQLITE4_ATTACH               24   /* Filename        NULL            */
#define SQLITE4_DETACH               25   /* Database Name   NULL            */
#define SQLITE4_ALTER_TABLE          26   /* Database Name   Table Name      */
#define SQLITE4_REINDEX              27   /* Index Name      NULL            */
#define SQLITE4_ANALYZE              28   /* Table Name      NULL            */
#define SQLITE4_CREATE_VTABLE        29   /* Table Name      Module Name     */
#define SQLITE4_DROP_VTABLE          30   /* Table Name      Module Name     */
#define SQLITE4_FUNCTION             31   /* NULL            Function Name   */
#define SQLITE4_SAVEPOINT            32   /* Operation       Savepoint Name  */
#define SQLITE4_COPY                  0   /* No longer used */

/*
** CAPIREF: Tracing And Profiling Functions
**
** These routines register callback functions that can be used for
** tracing and profiling the execution of SQL statements.
**
** ^The callback function registered by sqlite4_trace() is invoked at
** various times when an SQL statement is being run by [sqlite4_step()].
** ^The sqlite4_trace() callback is invoked with a UTF-8 rendering of the
** SQL statement text as the statement first begins executing.
** ^(Additional sqlite4_trace() callbacks might occur
** as each triggered subprogram is entered.  The callbacks for triggers
** contain a UTF-8 SQL comment that identifies the trigger.)^
**
** ^The callback function registered by sqlite4_profile() is invoked
** as each SQL statement finishes.  ^The profile callback contains
** the original statement text and an estimate of wall-clock time
** of how long that statement took to run.  ^The profile callback
** time is in units of nanoseconds, however the current implementation
** is only capable of millisecond resolution so the six least significant
** digits in the time are meaningless.  Future versions of SQLite
** might provide greater resolution on the profiler callback.  The
** sqlite4_profile() function is considered experimental and is
** subject to change in future versions of SQLite.
*/
SQLITE4_API void *sqlite4_trace(sqlite4*, void(*xTrace)(void*,const char*), void*);
SQLITE4_API SQLITE4_EXPERIMENTAL void *sqlite4_profile(sqlite4*,
   void(*xProfile)(void*,const char*,sqlite4_uint64), void*);

/*
** CAPIREF: Query Progress Callbacks
**
** ^The sqlite4_progress_handler(D,N,X,P) interface causes the callback
** function X to be invoked periodically during long running calls to
** [sqlite4_exec()] and [sqlite4_step()] for
** database connection D.  An example use for this
** interface is to keep a GUI updated during a large query.
**
** ^The parameter P is passed through as the only parameter to the 
** callback function X.  ^The parameter N is the number of 
** [virtual machine instructions] that are evaluated between successive
** invocations of the callback X.
**
** ^Only a single progress handler may be defined at one time per
** [database connection]; setting a new progress handler cancels the
** old one.  ^Setting parameter X to NULL disables the progress handler.
** ^The progress handler is also disabled by setting N to a value less
** than 1.
**
** ^If the progress callback returns non-zero, the operation is
** interrupted.  This feature can be used to implement a
** "Cancel" button on a GUI progress dialog box.
**
** The progress handler callback must not do anything that will modify
** the database connection that invoked the progress handler.
** Note that [sqlite4_prepare()] and [sqlite4_step()] both modify their
** database connections for the meaning of "modify" in this paragraph.
**
*/
SQLITE4_API void sqlite4_progress_handler(sqlite4*, int, int(*)(void*), void*);

/*
** CAPIREF: Opening A New Database Connection
**
** ^These routines open an SQLite4 database file as specified by the 
** URI argument.
** ^(A [database connection] handle is usually
** returned in *ppDb, even if an error occurs.  The only exception is that
** if SQLite is unable to allocate memory to hold the [sqlite4] object,
** a NULL will be written into *ppDb instead of a pointer to the [sqlite4]
** object.)^ ^(If the database is opened (and/or created) successfully, then
** [SQLITE4_OK] is returned.  Otherwise an [error code] is returned.)^ ^The
** [sqlite4_errmsg()] routine can be used to obtain
** an English language description of the error following a failure of any
** of the sqlite4_open() routines.
**
** Whether or not an error occurs when it is opened, resources
** associated with the [database connection] handle should be released by
** passing it to [sqlite4_close()] when it is no longer required.
**
*/
SQLITE4_API int sqlite4_open(
  sqlite4_env *pEnv,     /* Run-time environment. NULL means use the default */
  const char *filename,  /* Database filename (UTF-8) */
  sqlite4 **ppDb,        /* OUT: SQLite db handle */
  ...                    /* Optional parameters.  Zero terminates options */
);

/*
** CAPIREF: Obtain Values For URI Parameters
**
** These are utility routines, useful to VFS implementations, that check
** to see if a database file was a URI that contained a specific query 
** parameter, and if so obtains the value of that query parameter.
**
** If F is the database filename pointer passed into the xOpen() method of 
** a VFS implementation when the flags parameter to xOpen() has one or 
** more of the [SQLITE4_OPEN_URI] or [SQLITE4_OPEN_MAIN_DB] bits set and
** P is the name of the query parameter, then
** sqlite4_uri_parameter(F,P) returns the value of the P
** parameter if it exists or a NULL pointer if P does not appear as a 
** query parameter on F.  If P is a query parameter of F
** has no explicit value, then sqlite4_uri_parameter(F,P) returns
** a pointer to an empty string.
**
** The sqlite4_uri_boolean(F,P,B) routine assumes that P is a boolean
** parameter and returns true (1) or false (0) according to the value
** of P.  The value of P is true if it is "yes" or "true" or "on" or 
** a non-zero number and is false otherwise.  If P is not a query parameter
** on F then sqlite4_uri_boolean(F,P,B) returns (B!=0).
**
** The sqlite4_uri_int64(F,P,D) routine converts the value of P into a
** 64-bit signed integer and returns that integer, or D if P does not
** exist.  If the value of P is something other than an integer, then
** zero is returned.
** 
** If F is a NULL pointer, then sqlite4_uri_parameter(F,P) returns NULL and
** sqlite4_uri_boolean(F,P,B) returns B.  If F is not a NULL pointer and
** is not a database file pathname pointer that SQLite passed into the xOpen
** VFS method, then the behavior of this routine is undefined and probably
** undesirable.
*/
SQLITE4_API const char *sqlite4_uri_parameter(const char *zFilename, const char *zParam);
SQLITE4_API int sqlite4_uri_boolean(const char *zFile, const char *zParam, int bDefault);
SQLITE4_API sqlite4_int64 sqlite4_uri_int64(const char*, const char*, sqlite4_int64);


/*
** CAPIREF: Error Codes And Messages
**
** ^The sqlite4_errcode() interface returns the numeric 
** [extended result code] for the most recent failed sqlite4_* API call
** associated with a [database connection]. If a prior API call failed
** but the most recent API call succeeded, the return value from
** sqlite4_errcode() is undefined.
**
** ^The sqlite4_errmsg() and sqlite4_errmsg16() return English-language
** text that describes the error, as either UTF-8 or UTF-16 respectively.
** ^(Memory to hold the error message string is managed internally.
** The application does not need to worry about freeing the result.
** However, the error string might be overwritten or deallocated by
** subsequent calls to other SQLite interface functions.)^
**
** When the serialized [threading mode] is in use, it might be the
** case that a second error occurs on a separate thread in between
** the time of the first error and the call to these interfaces.
** When that happens, the second error will be reported since these
** interfaces always report the most recent result.  To avoid
** this, each thread can obtain exclusive use of the [database connection] D
** by invoking [sqlite4_mutex_enter]([sqlite4_db_mutex](D)) before beginning
** to use D and invoking [sqlite4_mutex_leave]([sqlite4_db_mutex](D)) after
** all calls to the interfaces listed here are completed.
**
** If an interface fails with SQLITE4_MISUSE, that means the interface
** was invoked incorrectly by the application.  In that case, the
** error code and message may or may not be set.
*/
SQLITE4_API int sqlite4_errcode(sqlite4 *db);
SQLITE4_API const char *sqlite4_errmsg(sqlite4*);
SQLITE4_API const void *sqlite4_errmsg16(sqlite4*);

/*
** CAPIREF: SQL Statement Object
** KEYWORDS: {prepared statement} {prepared statements}
**
** An instance of this object represents a single SQL statement.
** This object is variously known as a "prepared statement" or a
** "compiled SQL statement" or simply as a "statement".
**
** The life of a statement object goes something like this:
**
** <ol>
** <li> Create the object using [sqlite4_prepare()] or a related
**      function.
** <li> Bind values to [host parameters] using the sqlite4_bind_*()
**      interfaces.
** <li> Run the SQL by calling [sqlite4_step()] one or more times.
** <li> Reset the statement using [sqlite4_reset()] then go back
**      to step 2.  Do this zero or more times.
** <li> Destroy the object using [sqlite4_finalize()].
** </ol>
**
** Refer to documentation on individual methods above for additional
** information.
*/
typedef struct sqlite4_stmt sqlite4_stmt;

/*
** CAPIREF: Run-time Limits
**
** ^(This interface allows the size of various constructs to be limited
** on a connection by connection basis.  The first parameter is the
** [database connection] whose limit is to be set or queried.  The
** second parameter is one of the [limit categories] that define a
** class of constructs to be size limited.  The third parameter is the
** new limit for that construct.)^
**
** ^If the new limit is a negative number, the limit is unchanged.
** ^(For each limit category SQLITE4_LIMIT_<i>NAME</i> there is a 
** [limits | hard upper bound]
** set at compile-time by a C preprocessor macro called
** [limits | SQLITE4_MAX_<i>NAME</i>].
** (The "_LIMIT_" in the name is changed to "_MAX_".))^
** ^Attempts to increase a limit above its hard upper bound are
** silently truncated to the hard upper bound.
**
** ^Regardless of whether or not the limit was changed, the 
** [sqlite4_limit()] interface returns the prior value of the limit.
** ^Hence, to find the current value of a limit without changing it,
** simply invoke this interface with the third parameter set to -1.
**
** Run-time limits are intended for use in applications that manage
** both their own internal database and also databases that are controlled
** by untrusted external sources.  An example application might be a
** web browser that has its own databases for storing history and
** separate databases controlled by JavaScript applications downloaded
** off the Internet.  The internal databases can be given the
** large, default limits.  Databases managed by external sources can
** be given much smaller limits designed to prevent a denial of service
** attack.  Developers might also want to use the [sqlite4_set_authorizer()]
** interface to further control untrusted SQL.  The size of the database
** created by an untrusted script can be contained using the
** [max_page_count] [PRAGMA].
**
** New run-time limit categories may be added in future releases.
*/
SQLITE4_API int sqlite4_limit(sqlite4*, int id, int newVal);

/*
** CAPIREF: Run-Time Limit Categories
** KEYWORDS: {limit category} {*limit categories}
**
** These constants define various performance limits
** that can be lowered at run-time using [sqlite4_limit()].
** The synopsis of the meanings of the various limits is shown below.
** Additional information is available at [limits | Limits in SQLite].
**
** <dl>
** [[SQLITE4_LIMIT_LENGTH]] ^(<dt>SQLITE4_LIMIT_LENGTH</dt>
** <dd>The maximum size of any string or BLOB or table row, in bytes.<dd>)^
**
** [[SQLITE4_LIMIT_SQL_LENGTH]] ^(<dt>SQLITE4_LIMIT_SQL_LENGTH</dt>
** <dd>The maximum length of an SQL statement, in bytes.</dd>)^
**
** [[SQLITE4_LIMIT_COLUMN]] ^(<dt>SQLITE4_LIMIT_COLUMN</dt>
** <dd>The maximum number of columns in a table definition or in the
** result set of a [SELECT] or the maximum number of columns in an index
** or in an ORDER BY or GROUP BY clause.</dd>)^
**
** [[SQLITE4_LIMIT_EXPR_DEPTH]] ^(<dt>SQLITE4_LIMIT_EXPR_DEPTH</dt>
** <dd>The maximum depth of the parse tree on any expression.</dd>)^
**
** [[SQLITE4_LIMIT_COMPOUND_SELECT]] ^(<dt>SQLITE4_LIMIT_COMPOUND_SELECT</dt>
** <dd>The maximum number of terms in a compound SELECT statement.</dd>)^
**
** [[SQLITE4_LIMIT_VDBE_OP]] ^(<dt>SQLITE4_LIMIT_VDBE_OP</dt>
** <dd>The maximum number of instructions in a virtual machine program
** used to implement an SQL statement.  This limit is not currently
** enforced, though that might be added in some future release of
** SQLite.</dd>)^
**
** [[SQLITE4_LIMIT_FUNCTION_ARG]] ^(<dt>SQLITE4_LIMIT_FUNCTION_ARG</dt>
** <dd>The maximum number of arguments on a function.</dd>)^
**
** [[SQLITE4_LIMIT_ATTACHED]] ^(<dt>SQLITE4_LIMIT_ATTACHED</dt>
** <dd>The maximum number of [ATTACH | attached databases].)^</dd>
**
** [[SQLITE4_LIMIT_LIKE_PATTERN_LENGTH]]
** ^(<dt>SQLITE4_LIMIT_LIKE_PATTERN_LENGTH</dt>
** <dd>The maximum length of the pattern argument to the [LIKE] or
** [GLOB] operators.</dd>)^
**
** [[SQLITE4_LIMIT_VARIABLE_NUMBER]]
** ^(<dt>SQLITE4_LIMIT_VARIABLE_NUMBER</dt>
** <dd>The maximum index number of any [parameter] in an SQL statement.)^
**
** [[SQLITE4_LIMIT_TRIGGER_DEPTH]] ^(<dt>SQLITE4_LIMIT_TRIGGER_DEPTH</dt>
** <dd>The maximum depth of recursion for triggers.</dd>)^
** </dl>
*/
#define SQLITE4_LIMIT_LENGTH                    0
#define SQLITE4_LIMIT_SQL_LENGTH                1
#define SQLITE4_LIMIT_COLUMN                    2
#define SQLITE4_LIMIT_EXPR_DEPTH                3
#define SQLITE4_LIMIT_COMPOUND_SELECT           4
#define SQLITE4_LIMIT_VDBE_OP                   5
#define SQLITE4_LIMIT_FUNCTION_ARG              6
#define SQLITE4_LIMIT_ATTACHED                  7
#define SQLITE4_LIMIT_LIKE_PATTERN_LENGTH       8
#define SQLITE4_LIMIT_VARIABLE_NUMBER           9
#define SQLITE4_LIMIT_TRIGGER_DEPTH            10

/*
** CAPIREF: Compiling An SQL Statement
** KEYWORDS: {SQL statement compiler}
**
** To execute an SQL query, it must first be compiled into a byte-code
** program using one of these routines.
**
** The first argument, "db", is a [database connection] obtained from a
** prior successful call to [sqlite4_open()].
** The database connection must not have been closed.
**
** The second argument, "zSql", is the statement to be compiled, encoded
** as either UTF-8 or UTF-16.  The sqlite4_prepare()
** interface uses UTF-8, and sqlite4_prepare16()
** uses UTF-16.
**
** ^If the nByte argument is less than zero, then zSql is read up to the
** first zero terminator. ^If nByte is non-negative, then it is the maximum
** number of  bytes read from zSql.  ^When nByte is non-negative, the
** zSql string ends at either the first '\000' or '\u0000' character or
** the nByte-th byte, whichever comes first. If the caller knows
** that the supplied string is nul-terminated, then there is a small
** performance advantage to be gained by passing an nByte parameter that
** is equal to the number of bytes in the input string <i>including</i>
** the nul-terminator bytes as this saves SQLite from having to
** make a copy of the input string.
**
** ^If pzTail is not NULL then *pzTail is made to point to the first byte
** past the end of the first SQL statement in zSql.  These routines only
** compile the first statement in zSql, so *pzTail is left pointing to
** what remains uncompiled.
**
** ^*ppStmt is left pointing to a compiled [prepared statement] that can be
** executed using [sqlite4_step()].  ^If there is an error, *ppStmt is set
** to NULL.  ^If the input text contains no SQL (if the input is an empty
** string or a comment) then *ppStmt is set to NULL.
** The calling procedure is responsible for deleting the compiled
** SQL statement using [sqlite4_finalize()] after it has finished with it.
** ppStmt may not be NULL.
**
** ^On success, the sqlite4_prepare() family of routines return [SQLITE4_OK];
** otherwise an [error code] is returned.
*/
SQLITE4_API int sqlite4_prepare(
  sqlite4 *db,            /* Database handle */
  const char *zSql,       /* SQL statement, UTF-8 encoded */
  int nByte,              /* Maximum length of zSql in bytes. */
  sqlite4_stmt **ppStmt,  /* OUT: Statement handle */
  const char **pzTail     /* OUT: Pointer to unused portion of zSql */
);

/*
** CAPIREF: Retrieving Statement SQL
**
** ^This interface can be used to retrieve a saved copy of the original
** SQL text used to create a [prepared statement] if that statement was
** compiled using either [sqlite4_prepare()] or [sqlite4_prepare16_v2()].
*/
SQLITE4_API const char *sqlite4_sql(sqlite4_stmt *pStmt);

/*
** CAPIREF: Determine If An SQL Statement Writes The Database
**
** ^The sqlite4_stmt_readonly(X) interface returns true (non-zero) if
** and only if the [prepared statement] X makes no direct changes to
** the content of the database file.
**
** Note that [application-defined SQL functions] or
** [virtual tables] might change the database indirectly as a side effect.  
** ^(For example, if an application defines a function "eval()" that 
** calls [sqlite4_exec()], then the following SQL statement would
** change the database file through side-effects:
**
** <blockquote><pre>
**    SELECT eval('DELETE FROM t1') FROM t2;
** </pre></blockquote>
**
** But because the [SELECT] statement does not change the database file
** directly, sqlite4_stmt_readonly() would still return true.)^
**
** ^Transaction control statements such as [BEGIN], [COMMIT], [ROLLBACK],
** [SAVEPOINT], and [RELEASE] cause sqlite4_stmt_readonly() to return true,
** since the statements themselves do not actually modify the database but
** rather they control the timing of when other statements modify the 
** database.  ^The [ATTACH] and [DETACH] statements also cause
** sqlite4_stmt_readonly() to return true since, while those statements
** change the configuration of a database connection, they do not make 
** changes to the content of the database files on disk.
*/
SQLITE4_API int sqlite4_stmt_readonly(sqlite4_stmt *pStmt);

/*
** CAPIREF: Determine If A Prepared Statement Has Been Reset
**
** ^The sqlite4_stmt_busy(S) interface returns true (non-zero) if the
** [prepared statement] S has been stepped at least once using 
** [sqlite4_step(S)] but has not run to completion and/or has not 
** been reset using [sqlite4_reset(S)].  ^The sqlite4_stmt_busy(S)
** interface returns false if S is a NULL pointer.  If S is not a 
** NULL pointer and is not a pointer to a valid [prepared statement]
** object, then the behavior is undefined and probably undesirable.
**
** This interface can be used in combination [sqlite4_next_stmt()]
** to locate all prepared statements associated with a database 
** connection that are in need of being reset.  This can be used,
** for example, in diagnostic routines to search for prepared 
** statements that are holding a transaction open.
*/
SQLITE4_API int sqlite4_stmt_busy(sqlite4_stmt*);

/*
** CAPIREF: Dynamically Typed Value Object
** KEYWORDS: {protected sqlite4_value} {unprotected sqlite4_value}
**
** SQLite uses the sqlite4_value object to represent all values
** that can be stored in a database table. SQLite uses dynamic typing
** for the values it stores.  ^Values stored in sqlite4_value objects
** can be integers, floating point values, strings, BLOBs, or NULL.
**
** An sqlite4_value object may be either "protected" or "unprotected".
** Some interfaces require a protected sqlite4_value.  Other interfaces
** will accept either a protected or an unprotected sqlite4_value.
** Every interface that accepts sqlite4_value arguments specifies
** whether or not it requires a protected sqlite4_value.
**
** The terms "protected" and "unprotected" refer to whether or not
** a mutex is held.  An internal mutex is held for a protected
** sqlite4_value object but no mutex is held for an unprotected
** sqlite4_value object.  If SQLite is compiled to be single-threaded
** (with [SQLITE4_THREADSAFE=0] and with [sqlite4_threadsafe()] returning 0)
** or if SQLite is run in one of reduced mutex modes 
** [SQLITE4_CONFIG_SINGLETHREAD] or [SQLITE4_CONFIG_MULTITHREAD]
** then there is no distinction between protected and unprotected
** sqlite4_value objects and they can be used interchangeably.  However,
** for maximum code portability it is recommended that applications
** still make the distinction between protected and unprotected
** sqlite4_value objects even when not strictly required.
**
** ^The sqlite4_value objects that are passed as parameters into the
** implementation of [application-defined SQL functions] are protected.
** ^The sqlite4_value object returned by
** [sqlite4_column_value()] is unprotected.
** Unprotected sqlite4_value objects may only be used with
** [sqlite4_result_value()] and [sqlite4_bind_value()].
** The [sqlite4_value_blob | sqlite4_value_type()] family of
** interfaces require protected sqlite4_value objects.
*/
typedef struct Mem sqlite4_value;

/*
** CAPIREF: SQL Function Context Object
**
** The context in which an SQL function executes is stored in an
** sqlite4_context object.  ^A pointer to an sqlite4_context object
** is always first parameter to [application-defined SQL functions].
** The application-defined SQL function implementation will pass this
** pointer through into calls to [sqlite4_result_int | sqlite4_result()],
** [sqlite4_aggregate_context()], [sqlite4_user_data()],
** [sqlite4_context_db_handle()], [sqlite4_get_auxdata()],
** and/or [sqlite4_set_auxdata()].
*/
typedef struct sqlite4_context sqlite4_context;

/*
** CAPIREF: Binding Values To Prepared Statements
** KEYWORDS: {host parameter} {host parameters} {host parameter name}
** KEYWORDS: {SQL parameter} {SQL parameters} {parameter binding}
**
** ^(In the SQL statement text input to [sqlite4_prepare()] and its variants,
** literals may be replaced by a [parameter] that matches one of following
** templates:
**
** <ul>
** <li>  ?
** <li>  ?NNN
** <li>  :VVV
** <li>  @VVV
** <li>  $VVV
** </ul>
**
** In the templates above, NNN represents an integer literal,
** and VVV represents an alphanumeric identifier.)^  ^The values of these
** parameters (also called "host parameter names" or "SQL parameters")
** can be set using the sqlite4_bind_*() routines defined here.
**
** ^The first argument to the sqlite4_bind_*() routines is always
** a pointer to the [sqlite4_stmt] object returned from
** [sqlite4_prepare()] or its variants.
**
** ^The second argument is the index of the SQL parameter to be set.
** ^The leftmost SQL parameter has an index of 1.  ^When the same named
** SQL parameter is used more than once, second and subsequent
** occurrences have the same index as the first occurrence.
** ^The index for named parameters can be looked up using the
** [sqlite4_bind_parameter_index()] API if desired.  ^The index
** for "?NNN" parameters is the value of NNN.
** ^The NNN value must be between 1 and the [sqlite4_limit()]
** parameter [SQLITE4_LIMIT_VARIABLE_NUMBER] (default value: 999).
**
** ^The third argument is the value to bind to the parameter.
**
** ^(In those routines that have a fourth argument, its value is the
** number of bytes in the parameter.  To be clear: the value is the
** number of <u>bytes</u> in the value, not the number of characters.)^
** ^If the fourth parameter is negative, the length of the string is
** the number of bytes up to the first zero terminator.
** If a non-negative fourth parameter is provided to sqlite4_bind_text()
** or sqlite4_bind_text16() then that parameter must be the byte offset
** where the NUL terminator would occur assuming the string were NUL
** terminated.  If any NUL characters occur at byte offsets less than 
** the value of the fourth parameter then the resulting string value will
** contain embedded NULs.  The result of expressions involving strings
** with embedded NULs is undefined.
**
** ^The fifth argument to sqlite4_bind_blob(), sqlite4_bind_text(), and
** sqlite4_bind_text16() is a destructor used to dispose of the BLOB or
** string after SQLite has finished with it.  ^The destructor is called
** to dispose of the BLOB or string even if the call to sqlite4_bind_blob(),
** sqlite4_bind_text(), or sqlite4_bind_text16() fails.  
** ^If the fifth argument is
** the special value [SQLITE4_STATIC], then SQLite assumes that the
** information is in static, unmanaged space and does not need to be freed.
** ^If the fifth argument has the value [SQLITE4_TRANSIENT], then
** SQLite makes its own private copy of the data immediately, before
** the sqlite4_bind_*() routine returns.
**
** ^The sqlite4_bind_zeroblob() routine binds a BLOB of length N that
** is filled with zeroes.  ^A zeroblob uses a fixed amount of memory
** (just an integer to hold its size) while it is being processed.
** Zeroblobs are intended to serve as placeholders for BLOBs whose
** content is later written using
** [sqlite4_blob_open | incremental BLOB I/O] routines.
** ^A negative value for the zeroblob results in a zero-length BLOB.
**
** ^If any of the sqlite4_bind_*() routines are called with a NULL pointer
** for the [prepared statement] or with a prepared statement for which
** [sqlite4_step()] has been called more recently than [sqlite4_reset()],
** then the call will return [SQLITE4_MISUSE].  If any sqlite4_bind_()
** routine is passed a [prepared statement] that has been finalized, the
** result is undefined and probably harmful.
**
** ^Bindings are not cleared by the [sqlite4_reset()] routine.
** ^Unbound parameters are interpreted as NULL.
**
** ^The sqlite4_bind_* routines return [SQLITE4_OK] on success or an
** [error code] if anything goes wrong.
** ^[SQLITE4_RANGE] is returned if the parameter
** index is out of range.  ^[SQLITE4_NOMEM] is returned if malloc() fails.
**
** See also: [sqlite4_bind_parameter_count()],
** [sqlite4_bind_parameter_name()], and [sqlite4_bind_parameter_index()].
*/
SQLITE4_API int sqlite4_bind_blob(sqlite4_stmt*, int, const void*, int n, void(*)(void*));
SQLITE4_API int sqlite4_bind_double(sqlite4_stmt*, int, double);
SQLITE4_API int sqlite4_bind_int(sqlite4_stmt*, int, int);
SQLITE4_API int sqlite4_bind_int64(sqlite4_stmt*, int, sqlite4_int64);
SQLITE4_API int sqlite4_bind_null(sqlite4_stmt*, int);
SQLITE4_API int sqlite4_bind_text(sqlite4_stmt*, int, const char*, int n, void(*)(void*));
SQLITE4_API int sqlite4_bind_text16(sqlite4_stmt*, int, const void*, int, void(*)(void*));
SQLITE4_API int sqlite4_bind_value(sqlite4_stmt*, int, const sqlite4_value*);
SQLITE4_API int sqlite4_bind_zeroblob(sqlite4_stmt*, int, int n);

/*
** CAPIREF: Number Of SQL Parameters
**
** ^This routine can be used to find the number of [SQL parameters]
** in a [prepared statement].  SQL parameters are tokens of the
** form "?", "?NNN", ":AAA", "$AAA", or "@AAA" that serve as
** placeholders for values that are [sqlite4_bind_blob | bound]
** to the parameters at a later time.
**
** ^(This routine actually returns the index of the largest (rightmost)
** parameter. For all forms except ?NNN, this will correspond to the
** number of unique parameters.  If parameters of the ?NNN form are used,
** there may be gaps in the list.)^
**
** See also: [sqlite4_bind_blob|sqlite4_bind()],
** [sqlite4_bind_parameter_name()], and
** [sqlite4_bind_parameter_index()].
*/
SQLITE4_API int sqlite4_bind_parameter_count(sqlite4_stmt*);

/*
** CAPIREF: Name Of A Host Parameter
**
** ^The sqlite4_bind_parameter_name(P,N) interface returns
** the name of the N-th [SQL parameter] in the [prepared statement] P.
** ^(SQL parameters of the form "?NNN" or ":AAA" or "@AAA" or "$AAA"
** have a name which is the string "?NNN" or ":AAA" or "@AAA" or "$AAA"
** respectively.
** In other words, the initial ":" or "$" or "@" or "?"
** is included as part of the name.)^
** ^Parameters of the form "?" without a following integer have no name
** and are referred to as "nameless" or "anonymous parameters".
**
** ^The first host parameter has an index of 1, not 0.
**
** ^If the value N is out of range or if the N-th parameter is
** nameless, then NULL is returned.  ^The returned string is
** always in UTF-8 encoding even if the named parameter was
** originally specified as UTF-16 in [sqlite4_prepare16()] or
** [sqlite4_prepare16_v2()].
**
** See also: [sqlite4_bind_blob|sqlite4_bind()],
** [sqlite4_bind_parameter_count()], and
** [sqlite4_bind_parameter_index()].
*/
SQLITE4_API const char *sqlite4_bind_parameter_name(sqlite4_stmt*, int);

/*
** CAPIREF: Index Of A Parameter With A Given Name
**
** ^Return the index of an SQL parameter given its name.  ^The
** index value returned is suitable for use as the second
** parameter to [sqlite4_bind_blob|sqlite4_bind()].  ^A zero
** is returned if no matching parameter is found.  ^The parameter
** name must be given in UTF-8 even if the original statement
** was prepared from UTF-16 text using [sqlite4_prepare16_v2()].
**
** See also: [sqlite4_bind_blob|sqlite4_bind()],
** [sqlite4_bind_parameter_count()], and
** [sqlite4_bind_parameter_index()].
*/
SQLITE4_API int sqlite4_bind_parameter_index(sqlite4_stmt*, const char *zName);

/*
** CAPIREF: Reset All Bindings On A Prepared Statement
**
** ^Contrary to the intuition of many, [sqlite4_reset()] does not reset
** the [sqlite4_bind_blob | bindings] on a [prepared statement].
** ^Use this routine to reset all host parameters to NULL.
*/
SQLITE4_API int sqlite4_clear_bindings(sqlite4_stmt*);

/*
** CAPIREF: Number Of Columns In A Result Set
**
** ^Return the number of columns in the result set returned by the
** [prepared statement]. ^This routine returns 0 if pStmt is an SQL
** statement that does not return data (for example an [UPDATE]).
**
** See also: [sqlite4_data_count()]
*/
SQLITE4_API int sqlite4_column_count(sqlite4_stmt *pStmt);

/*
** CAPIREF: Column Names In A Result Set
**
** ^These routines return the name assigned to a particular column
** in the result set of a [SELECT] statement.  ^The sqlite4_column_name()
** interface returns a pointer to a zero-terminated UTF-8 string
** and sqlite4_column_name16() returns a pointer to a zero-terminated
** UTF-16 string.  ^The first parameter is the [prepared statement]
** that implements the [SELECT] statement. ^The second parameter is the
** column number.  ^The leftmost column is number 0.
**
** ^The returned string pointer is valid until either the [prepared statement]
** is destroyed by [sqlite4_finalize()] or until the statement is automatically
** reprepared by the first call to [sqlite4_step()] for a particular run
** or until the next call to
** sqlite4_column_name() or sqlite4_column_name16() on the same column.
**
** ^If sqlite4_malloc() fails during the processing of either routine
** (for example during a conversion from UTF-8 to UTF-16) then a
** NULL pointer is returned.
**
** ^The name of a result column is the value of the "AS" clause for
** that column, if there is an AS clause.  If there is no AS clause
** then the name of the column is unspecified and may change from
** one release of SQLite to the next.
*/
SQLITE4_API const char *sqlite4_column_name(sqlite4_stmt*, int N);
SQLITE4_API const void *sqlite4_column_name16(sqlite4_stmt*, int N);

/*
** CAPIREF: Source Of Data In A Query Result
**
** ^These routines provide a means to determine the database, table, and
** table column that is the origin of a particular result column in
** [SELECT] statement.
** ^The name of the database or table or column can be returned as
** either a UTF-8 or UTF-16 string.  ^The _database_ routines return
** the database name, the _table_ routines return the table name, and
** the origin_ routines return the column name.
** ^The returned string is valid until the [prepared statement] is destroyed
** using [sqlite4_finalize()] or until the statement is automatically
** reprepared by the first call to [sqlite4_step()] for a particular run
** or until the same information is requested
** again in a different encoding.
**
** ^The names returned are the original un-aliased names of the
** database, table, and column.
**
** ^The first argument to these interfaces is a [prepared statement].
** ^These functions return information about the Nth result column returned by
** the statement, where N is the second function argument.
** ^The left-most column is column 0 for these routines.
**
** ^If the Nth column returned by the statement is an expression or
** subquery and is not a column value, then all of these functions return
** NULL.  ^These routine might also return NULL if a memory allocation error
** occurs.  ^Otherwise, they return the name of the attached database, table,
** or column that query result column was extracted from.
**
** ^As with all other SQLite APIs, those whose names end with "16" return
** UTF-16 encoded strings and the other functions return UTF-8.
**
** ^These APIs are only available if the library was compiled with the
** [SQLITE4_ENABLE_COLUMN_METADATA] C-preprocessor symbol.
**
** If two or more threads call one or more of these routines against the same
** prepared statement and column at the same time then the results are
** undefined.
**
** If two or more threads call one or more
** [sqlite4_column_database_name | column metadata interfaces]
** for the same [prepared statement] and result column
** at the same time then the results are undefined.
*/
SQLITE4_API const char *sqlite4_column_database_name(sqlite4_stmt*,int);
SQLITE4_API const void *sqlite4_column_database_name16(sqlite4_stmt*,int);
SQLITE4_API const char *sqlite4_column_table_name(sqlite4_stmt*,int);
SQLITE4_API const void *sqlite4_column_table_name16(sqlite4_stmt*,int);
SQLITE4_API const char *sqlite4_column_origin_name(sqlite4_stmt*,int);
SQLITE4_API const void *sqlite4_column_origin_name16(sqlite4_stmt*,int);

/*
** CAPIREF: Declared Datatype Of A Query Result
**
** ^(The first parameter is a [prepared statement].
** If this statement is a [SELECT] statement and the Nth column of the
** returned result set of that [SELECT] is a table column (not an
** expression or subquery) then the declared type of the table
** column is returned.)^  ^If the Nth column of the result set is an
** expression or subquery, then a NULL pointer is returned.
** ^The returned string is always UTF-8 encoded.
**
** ^(For example, given the database schema:
**
** CREATE TABLE t1(c1 VARIANT);
**
** and the following statement to be compiled:
**
** SELECT c1 + 1, c1 FROM t1;
**
** this routine would return the string "VARIANT" for the second result
** column (i==1), and a NULL pointer for the first result column (i==0).)^
**
** ^SQLite uses dynamic run-time typing.  ^So just because a column
** is declared to contain a particular type does not mean that the
** data stored in that column is of the declared type.  SQLite is
** strongly typed, but the typing is dynamic not static.  ^Type
** is associated with individual values, not with the containers
** used to hold those values.
*/
SQLITE4_API const char *sqlite4_column_decltype(sqlite4_stmt*,int);
SQLITE4_API const void *sqlite4_column_decltype16(sqlite4_stmt*,int);

/*
** CAPIREF: Evaluate An SQL Statement
**
** After a [prepared statement] has been prepared using [sqlite4_prepare()],
** this function must be called one or more times to evaluate the statement.
**
** ^This routine can return any of the other [result codes] or
** [extended result codes].
**
** ^[SQLITE4_BUSY] means that the database engine was unable to acquire the
** database locks it needs to do its job.  ^If the statement is a [COMMIT]
** or occurs outside of an explicit transaction, then you can retry the
** statement.  If the statement is not a [COMMIT] and occurs within an
** explicit transaction then you should rollback the transaction before
** continuing.
**
** ^[SQLITE4_DONE] means that the statement has finished executing
** successfully.  sqlite4_step() should not be called again on this virtual
** machine without first calling [sqlite4_reset()] to reset the virtual
** machine back to its initial state.
**
** ^If the SQL statement being executed returns any data, then [SQLITE4_ROW]
** is returned each time a new row of data is ready for processing by the
** caller. The values may be accessed using the [column access functions].
** sqlite4_step() is called again to retrieve the next row of data.
**
** ^[SQLITE4_ERROR] means that a run-time error (such as a constraint
** violation) has occurred.  sqlite4_step() should not be called again on
** the VM. More information may be found by calling [sqlite4_errmsg()].
**
** [SQLITE4_MISUSE] means that the this routine was called inappropriately.
** Perhaps it was called on a [prepared statement] that has
** already been [sqlite4_finalize | finalized] or on one that had
** previously returned [SQLITE4_ERROR] or [SQLITE4_DONE].  Or it could
** be the case that the same database connection is being used by two or
** more threads at the same moment in time.
*/
SQLITE4_API int sqlite4_step(sqlite4_stmt*);

/*
** CAPIREF: Number of columns in a result set
**
** ^The sqlite4_data_count(P) interface returns the number of columns in the
** current row of the result set of [prepared statement] P.
** ^If prepared statement P does not have results ready to return
** (via calls to the [sqlite4_column_int | sqlite4_column_*()] of
** interfaces) then sqlite4_data_count(P) returns 0.
** ^The sqlite4_data_count(P) routine also returns 0 if P is a NULL pointer.
** ^The sqlite4_data_count(P) routine returns 0 if the previous call to
** [sqlite4_step](P) returned [SQLITE4_DONE].  ^The sqlite4_data_count(P)
** will return non-zero if previous call to [sqlite4_step](P) returned
** [SQLITE4_ROW], except in the case of the [PRAGMA incremental_vacuum]
** where it always returns zero since each step of that multi-step
** pragma returns 0 columns of data.
**
** See also: [sqlite4_column_count()]
*/
SQLITE4_API int sqlite4_data_count(sqlite4_stmt *pStmt);

/*
** CAPIREF: Fundamental Datatypes
** KEYWORDS: SQLITE4_TEXT
**
** ^(Every value in SQLite has one of five fundamental datatypes:
**
** <ul>
** <li> 64-bit signed integer
** <li> 64-bit IEEE floating point number
** <li> string
** <li> BLOB
** <li> NULL
** </ul>)^
**
** These constants are codes for each of those types.
*/
#define SQLITE4_INTEGER  1
#define SQLITE4_FLOAT    2
#define SQLITE4_TEXT     3
#define SQLITE4_BLOB     4
#define SQLITE4_NULL     5

/*
** CAPIREF: Result Values From A Query
** KEYWORDS: {column access functions}
**
** These routines form the "result set" interface.
**
** ^These routines return information about a single column of the current
** result row of a query.  ^In every case the first argument is a pointer
** to the [prepared statement] that is being evaluated (the [sqlite4_stmt*]
** that was returned from [sqlite4_prepare()].
** and the second argument is the index of the column for which information
** should be returned. ^The leftmost column of the result set has the index 0.
** ^The number of columns in the result can be determined using
** [sqlite4_column_count()].
**
** If the SQL statement does not currently point to a valid row, or if the
** column index is out of range, the result is undefined.
** These routines may only be called when the most recent call to
** [sqlite4_step()] has returned [SQLITE4_ROW] and neither
** [sqlite4_reset()] nor [sqlite4_finalize()] have been called subsequently.
** If any of these routines are called after [sqlite4_reset()] or
** [sqlite4_finalize()] or after [sqlite4_step()] has returned
** something other than [SQLITE4_ROW], the results are undefined.
** If [sqlite4_step()] or [sqlite4_reset()] or [sqlite4_finalize()]
** are called from a different thread while any of these routines
** are pending, then the results are undefined.
**
** ^The sqlite4_column_type() routine returns the
** [SQLITE4_INTEGER | datatype code] for the initial data type
** of the result column.  ^The returned value is one of [SQLITE4_INTEGER],
** [SQLITE4_FLOAT], [SQLITE4_TEXT], [SQLITE4_BLOB], or [SQLITE4_NULL].  The value
** returned by sqlite4_column_type() is only meaningful if no type
** conversions have occurred as described below.  After a type conversion,
** the value returned by sqlite4_column_type() is undefined.  Future
** versions of SQLite may change the behavior of sqlite4_column_type()
** following a type conversion.
**
** ^If the result is a BLOB or UTF-8 string then the sqlite4_column_bytes()
** routine returns the number of bytes in that BLOB or string.
** ^If the result is a UTF-16 string, then sqlite4_column_bytes() converts
** the string to UTF-8 and then returns the number of bytes.
** ^If the result is a numeric value then sqlite4_column_bytes() uses
** [sqlite4_snprintf()] to convert that value to a UTF-8 string and returns
** the number of bytes in that string.
** ^If the result is NULL, then sqlite4_column_bytes() returns zero.
**
** ^If the result is a BLOB or UTF-16 string then the sqlite4_column_bytes16()
** routine returns the number of bytes in that BLOB or string.
** ^If the result is a UTF-8 string, then sqlite4_column_bytes16() converts
** the string to UTF-16 and then returns the number of bytes.
** ^If the result is a numeric value then sqlite4_column_bytes16() uses
** [sqlite4_snprintf()] to convert that value to a UTF-16 string and returns
** the number of bytes in that string.
** ^If the result is NULL, then sqlite4_column_bytes16() returns zero.
**
** ^The values returned by [sqlite4_column_bytes()] and 
** [sqlite4_column_bytes16()] do not include the zero terminators at the end
** of the string.  ^For clarity: the values returned by
** [sqlite4_column_bytes()] and [sqlite4_column_bytes16()] are the number of
** bytes in the string, not the number of characters.
**
** ^Strings returned by sqlite4_column_text() and sqlite4_column_text16(),
** even empty strings, are always zero-terminated.  ^The return
** value from sqlite4_column_blob() for a zero-length BLOB is a NULL pointer.
**
** ^The object returned by [sqlite4_column_value()] is an
** [unprotected sqlite4_value] object.  An unprotected sqlite4_value object
** may only be used with [sqlite4_bind_value()] and [sqlite4_result_value()].
** If the [unprotected sqlite4_value] object returned by
** [sqlite4_column_value()] is used in any other way, including calls
** to routines like [sqlite4_value_int()], [sqlite4_value_text()],
** or [sqlite4_value_bytes()], then the behavior is undefined.
**
** These routines attempt to convert the value where appropriate.  ^For
** example, if the internal representation is FLOAT and a text result
** is requested, [sqlite4_snprintf()] is used internally to perform the
** conversion automatically.  ^(The following table details the conversions
** that are applied:
**
** <blockquote>
** <table border="1">
** <tr><th> Internal<br>Type <th> Requested<br>Type <th>  Conversion
**
** <tr><td>  NULL    <td> INTEGER   <td> Result is 0
** <tr><td>  NULL    <td>  FLOAT    <td> Result is 0.0
** <tr><td>  NULL    <td>   TEXT    <td> Result is NULL pointer
** <tr><td>  NULL    <td>   BLOB    <td> Result is NULL pointer
** <tr><td> INTEGER  <td>  FLOAT    <td> Convert from integer to float
** <tr><td> INTEGER  <td>   TEXT    <td> ASCII rendering of the integer
** <tr><td> INTEGER  <td>   BLOB    <td> Same as INTEGER->TEXT
** <tr><td>  FLOAT   <td> INTEGER   <td> Convert from float to integer
** <tr><td>  FLOAT   <td>   TEXT    <td> ASCII rendering of the float
** <tr><td>  FLOAT   <td>   BLOB    <td> Same as FLOAT->TEXT
** <tr><td>  TEXT    <td> INTEGER   <td> Use atoi()
** <tr><td>  TEXT    <td>  FLOAT    <td> Use atof()
** <tr><td>  TEXT    <td>   BLOB    <td> No change
** <tr><td>  BLOB    <td> INTEGER   <td> Convert to TEXT then use atoi()
** <tr><td>  BLOB    <td>  FLOAT    <td> Convert to TEXT then use atof()
** <tr><td>  BLOB    <td>   TEXT    <td> Add a zero terminator if needed
** </table>
** </blockquote>)^
**
** The table above makes reference to standard C library functions atoi()
** and atof().  SQLite does not really use these functions.  It has its
** own equivalent internal routines.  The atoi() and atof() names are
** used in the table for brevity and because they are familiar to most
** C programmers.
**
** Note that when type conversions occur, pointers returned by prior
** calls to sqlite4_column_blob(), sqlite4_column_text(), and/or
** sqlite4_column_text16() may be invalidated.
** Type conversions and pointer invalidations might occur
** in the following cases:
**
** <ul>
** <li> The initial content is a BLOB and sqlite4_column_text() or
**      sqlite4_column_text16() is called.  A zero-terminator might
**      need to be added to the string.</li>
** <li> The initial content is UTF-8 text and sqlite4_column_bytes16() or
**      sqlite4_column_text16() is called.  The content must be converted
**      to UTF-16.</li>
** <li> The initial content is UTF-16 text and sqlite4_column_bytes() or
**      sqlite4_column_text() is called.  The content must be converted
**      to UTF-8.</li>
** </ul>
**
** ^Conversions between UTF-16be and UTF-16le are always done in place and do
** not invalidate a prior pointer, though of course the content of the buffer
** that the prior pointer references will have been modified.  Other kinds
** of conversion are done in place when it is possible, but sometimes they
** are not possible and in those cases prior pointers are invalidated.
**
** The safest and easiest to remember policy is to invoke these routines
** in one of the following ways:
**
** <ul>
**  <li>sqlite4_column_text() followed by sqlite4_column_bytes()</li>
**  <li>sqlite4_column_blob() followed by sqlite4_column_bytes()</li>
**  <li>sqlite4_column_text16() followed by sqlite4_column_bytes16()</li>
** </ul>
**
** In other words, you should call sqlite4_column_text(),
** sqlite4_column_blob(), or sqlite4_column_text16() first to force the result
** into the desired format, then invoke sqlite4_column_bytes() or
** sqlite4_column_bytes16() to find the size of the result.  Do not mix calls
** to sqlite4_column_text() or sqlite4_column_blob() with calls to
** sqlite4_column_bytes16(), and do not mix calls to sqlite4_column_text16()
** with calls to sqlite4_column_bytes().
**
** ^The pointers returned are valid until a type conversion occurs as
** described above, or until [sqlite4_step()] or [sqlite4_reset()] or
** [sqlite4_finalize()] is called.  ^The memory space used to hold strings
** and BLOBs is freed automatically.  Do <b>not</b> pass the pointers returned
** [sqlite4_column_blob()], [sqlite4_column_text()], etc. into
** [sqlite4_free()].
**
** ^(If a memory allocation error occurs during the evaluation of any
** of these routines, a default value is returned.  The default value
** is either the integer 0, the floating point number 0.0, or a NULL
** pointer.  Subsequent calls to [sqlite4_errcode()] will return
** [SQLITE4_NOMEM].)^
*/
SQLITE4_API const void *sqlite4_column_blob(sqlite4_stmt*, int iCol);
SQLITE4_API int sqlite4_column_bytes(sqlite4_stmt*, int iCol);
SQLITE4_API int sqlite4_column_bytes16(sqlite4_stmt*, int iCol);
SQLITE4_API double sqlite4_column_double(sqlite4_stmt*, int iCol);
SQLITE4_API int sqlite4_column_int(sqlite4_stmt*, int iCol);
SQLITE4_API sqlite4_int64 sqlite4_column_int64(sqlite4_stmt*, int iCol);
SQLITE4_API const unsigned char *sqlite4_column_text(sqlite4_stmt*, int iCol);
SQLITE4_API const void *sqlite4_column_text16(sqlite4_stmt*, int iCol);
SQLITE4_API int sqlite4_column_type(sqlite4_stmt*, int iCol);
SQLITE4_API sqlite4_value *sqlite4_column_value(sqlite4_stmt*, int iCol);

/*
** CAPIREF: Destroy A Prepared Statement Object
**
** ^The sqlite4_finalize() function is called to delete a [prepared statement].
** ^If the most recent evaluation of the statement encountered no errors
** or if the statement is never been evaluated, then sqlite4_finalize() returns
** SQLITE4_OK.  ^If the most recent evaluation of statement S failed, then
** sqlite4_finalize(S) returns the appropriate [error code] or
** [extended error code].
**
** ^The sqlite4_finalize(S) routine can be called at any point during
** the life cycle of [prepared statement] S:
** before statement S is ever evaluated, after
** one or more calls to [sqlite4_reset()], or after any call
** to [sqlite4_step()] regardless of whether or not the statement has
** completed execution.
**
** ^Invoking sqlite4_finalize() on a NULL pointer is a harmless no-op.
**
** The application must finalize every [prepared statement] in order to avoid
** resource leaks.  It is a grievous error for the application to try to use
** a prepared statement after it has been finalized.  Any use of a prepared
** statement after it has been finalized can result in undefined and
** undesirable behavior such as segfaults and heap corruption.
*/
SQLITE4_API int sqlite4_finalize(sqlite4_stmt *pStmt);

/*
** CAPIREF: Reset A Prepared Statement Object
**
** The sqlite4_reset() function is called to reset a [prepared statement]
** object back to its initial state, ready to be re-executed.
** ^Any SQL statement variables that had values bound to them using
** the [sqlite4_bind_blob | sqlite4_bind_*() API] retain their values.
** Use [sqlite4_clear_bindings()] to reset the bindings.
**
** ^The [sqlite4_reset(S)] interface resets the [prepared statement] S
** back to the beginning of its program.
**
** ^If the most recent call to [sqlite4_step(S)] for the
** [prepared statement] S returned [SQLITE4_ROW] or [SQLITE4_DONE],
** or if [sqlite4_step(S)] has never before been called on S,
** then [sqlite4_reset(S)] returns [SQLITE4_OK].
**
** ^If the most recent call to [sqlite4_step(S)] for the
** [prepared statement] S indicated an error, then
** [sqlite4_reset(S)] returns an appropriate [error code].
**
** ^The [sqlite4_reset(S)] interface does not change the values
** of any [sqlite4_bind_blob|bindings] on the [prepared statement] S.
*/
SQLITE4_API int sqlite4_reset(sqlite4_stmt *pStmt);

/*
** CAPIREF: Create Or Redefine SQL Functions
** KEYWORDS: {function creation routines}
** KEYWORDS: {application-defined SQL function}
** KEYWORDS: {application-defined SQL functions}
**
** ^These functions (collectively known as "function creation routines")
** are used to add SQL functions or aggregates or to redefine the behavior
** of existing SQL functions or aggregates.  The only differences between
** these routines are the text encoding expected for
** the second parameter (the name of the function being created)
** and the presence or absence of a destructor callback for
** the application data pointer.
**
** ^The first parameter is the [database connection] to which the SQL
** function is to be added.  ^If an application uses more than one database
** connection then application-defined SQL functions must be added
** to each database connection separately.
**
** ^The second parameter is the name of the SQL function to be created or
** redefined.  ^The length of the name is limited to 255 bytes in a UTF-8
** representation, exclusive of the zero-terminator.  ^Note that the name
** length limit is in UTF-8 bytes, not characters nor UTF-16 bytes.  
** ^Any attempt to create a function with a longer name
** will result in [SQLITE4_MISUSE] being returned.
**
** ^The third parameter (nArg)
** is the number of arguments that the SQL function or
** aggregate takes. ^If this parameter is -1, then the SQL function or
** aggregate may take any number of arguments between 0 and the limit
** set by [sqlite4_limit]([SQLITE4_LIMIT_FUNCTION_ARG]).  If the third
** parameter is less than -1 or greater than 127 then the behavior is
** undefined.
**
** ^The fourth parameter, eTextRep, specifies what
** [SQLITE4_UTF8 | text encoding] this SQL function prefers for
** its parameters.  Every SQL function implementation must be able to work
** with UTF-8, UTF-16le, or UTF-16be.  But some implementations may be
** more efficient with one encoding than another.  ^An application may
** invoke sqlite4_create_function() or sqlite4_create_function16() multiple
** times with the same function but with different values of eTextRep.
** ^When multiple implementations of the same function are available, SQLite
** will pick the one that involves the least amount of data conversion.
** If there is only a single implementation which does not care what text
** encoding is used, then the fourth argument should be [SQLITE4_ANY].
**
** ^(The fifth parameter is an arbitrary pointer.  The implementation of the
** function can gain access to this pointer using [sqlite4_user_data()].)^
**
** ^The sixth, seventh and eighth parameters, xFunc, xStep and xFinal, are
** pointers to C-language functions that implement the SQL function or
** aggregate. ^A scalar SQL function requires an implementation of the xFunc
** callback only; NULL pointers must be passed as the xStep and xFinal
** parameters. ^An aggregate SQL function requires an implementation of xStep
** and xFinal and NULL pointer must be passed for xFunc. ^To delete an existing
** SQL function or aggregate, pass NULL pointers for all three function
** callbacks.
**
** ^(If the ninth parameter to sqlite4_create_function_v2() is not NULL,
** then it is destructor for the application data pointer. 
** The destructor is invoked when the function is deleted, either by being
** overloaded or when the database connection closes.)^
** ^The destructor is also invoked if the call to
** sqlite4_create_function_v2() fails.
** ^When the destructor callback of the tenth parameter is invoked, it
** is passed a single argument which is a copy of the application data 
** pointer which was the fifth parameter to sqlite4_create_function_v2().
**
** ^It is permitted to register multiple implementations of the same
** functions with the same name but with either differing numbers of
** arguments or differing preferred text encodings.  ^SQLite will use
** the implementation that most closely matches the way in which the
** SQL function is used.  ^A function implementation with a non-negative
** nArg parameter is a better match than a function implementation with
** a negative nArg.  ^A function where the preferred text encoding
** matches the database encoding is a better
** match than a function where the encoding is different.  
** ^A function where the encoding difference is between UTF16le and UTF16be
** is a closer match than a function where the encoding difference is
** between UTF8 and UTF16.
**
** ^Built-in functions may be overloaded by new application-defined functions.
**
** ^An application-defined function is permitted to call other
** SQLite interfaces.  However, such calls must not
** close the database connection nor finalize or reset the prepared
** statement in which the function is running.
*/
SQLITE4_API int sqlite4_create_function(
  sqlite4 *db,
  const char *zFunctionName,
  int nArg,
  int eTextRep,
  void *pApp,
  void (*xFunc)(sqlite4_context*,int,sqlite4_value**),
  void (*xStep)(sqlite4_context*,int,sqlite4_value**),
  void (*xFinal)(sqlite4_context*)
);
SQLITE4_API int sqlite4_create_function16(
  sqlite4 *db,
  const void *zFunctionName,
  int nArg,
  int eTextRep,
  void *pApp,
  void (*xFunc)(sqlite4_context*,int,sqlite4_value**),
  void (*xStep)(sqlite4_context*,int,sqlite4_value**),
  void (*xFinal)(sqlite4_context*)
);
SQLITE4_API int sqlite4_create_function_v2(
  sqlite4 *db,
  const char *zFunctionName,
  int nArg,
  int eTextRep,
  void *pApp,
  void (*xFunc)(sqlite4_context*,int,sqlite4_value**),
  void (*xStep)(sqlite4_context*,int,sqlite4_value**),
  void (*xFinal)(sqlite4_context*),
  void(*xDestroy)(void*)
);

/*
** CAPIREF: Text Encodings
**
** These constant define integer codes that represent the various
** text encodings supported by SQLite.
*/
#define SQLITE4_UTF8           1
#define SQLITE4_UTF16LE        2
#define SQLITE4_UTF16BE        3
#define SQLITE4_UTF16          4    /* Use native byte order */
#define SQLITE4_ANY            5    /* sqlite4_create_function only */
#define SQLITE4_UTF16_ALIGNED  8    /* sqlite4_create_collation only */

/*
** CAPIREF: Deprecated Functions
** DEPRECATED
**
** These functions are [deprecated].  In order to maintain
** backwards compatibility with older code, these functions continue 
** to be supported.  However, new applications should avoid
** the use of these functions.  To help encourage people to avoid
** using these functions, we are not going to tell you what they do.
*/
#ifndef SQLITE4_OMIT_DEPRECATED
SQLITE4_API SQLITE4_DEPRECATED int sqlite4_aggregate_count(sqlite4_context*);
SQLITE4_API SQLITE4_DEPRECATED int sqlite4_expired(sqlite4_stmt*);
SQLITE4_API SQLITE4_DEPRECATED int sqlite4_transfer_bindings(sqlite4_stmt*, sqlite4_stmt*);
SQLITE4_API SQLITE4_DEPRECATED int sqlite4_global_recover(void);
#endif

/*
** CAPIREF: Obtaining SQL Function Parameter Values
**
** The C-language implementation of SQL functions and aggregates uses
** this set of interface routines to access the parameter values on
** the function or aggregate.
**
** The xFunc (for scalar functions) or xStep (for aggregates) parameters
** to [sqlite4_create_function()] and [sqlite4_create_function16()]
** define callbacks that implement the SQL functions and aggregates.
** The 3rd parameter to these callbacks is an array of pointers to
** [protected sqlite4_value] objects.  There is one [sqlite4_value] object for
** each parameter to the SQL function.  These routines are used to
** extract values from the [sqlite4_value] objects.
**
** These routines work only with [protected sqlite4_value] objects.
** Any attempt to use these routines on an [unprotected sqlite4_value]
** object results in undefined behavior.
**
** ^These routines work just like the corresponding [column access functions]
** except that  these routines take a single [protected sqlite4_value] object
** pointer instead of a [sqlite4_stmt*] pointer and an integer column number.
**
** ^The sqlite4_value_text16() interface extracts a UTF-16 string
** in the native byte-order of the host machine.  ^The
** sqlite4_value_text16be() and sqlite4_value_text16le() interfaces
** extract UTF-16 strings as big-endian and little-endian respectively.
**
** ^(The sqlite4_value_numeric_type() interface attempts to apply
** numeric affinity to the value.  This means that an attempt is
** made to convert the value to an integer or floating point.  If
** such a conversion is possible without loss of information (in other
** words, if the value is a string that looks like a number)
** then the conversion is performed.  Otherwise no conversion occurs.
** The [SQLITE4_INTEGER | datatype] after conversion is returned.)^
**
** Please pay particular attention to the fact that the pointer returned
** from [sqlite4_value_blob()], [sqlite4_value_text()], or
** [sqlite4_value_text16()] can be invalidated by a subsequent call to
** [sqlite4_value_bytes()], [sqlite4_value_bytes16()], [sqlite4_value_text()],
** or [sqlite4_value_text16()].
**
** These routines must be called from the same thread as
** the SQL function that supplied the [sqlite4_value*] parameters.
*/
SQLITE4_API const void *sqlite4_value_blob(sqlite4_value*);
SQLITE4_API int sqlite4_value_bytes(sqlite4_value*);
SQLITE4_API int sqlite4_value_bytes16(sqlite4_value*);
SQLITE4_API double sqlite4_value_double(sqlite4_value*);
SQLITE4_API int sqlite4_value_int(sqlite4_value*);
SQLITE4_API sqlite4_int64 sqlite4_value_int64(sqlite4_value*);
SQLITE4_API const unsigned char *sqlite4_value_text(sqlite4_value*);
SQLITE4_API const void *sqlite4_value_text16(sqlite4_value*);
SQLITE4_API const void *sqlite4_value_text16le(sqlite4_value*);
SQLITE4_API const void *sqlite4_value_text16be(sqlite4_value*);
SQLITE4_API int sqlite4_value_type(sqlite4_value*);
SQLITE4_API int sqlite4_value_numeric_type(sqlite4_value*);

/*
** CAPIREF: Obtain Aggregate Function Context
**
** Implementations of aggregate SQL functions use this
** routine to allocate memory for storing their state.
**
** ^The first time the sqlite4_aggregate_context(C,N) routine is called 
** for a particular aggregate function, SQLite
** allocates N of memory, zeroes out that memory, and returns a pointer
** to the new memory. ^On second and subsequent calls to
** sqlite4_aggregate_context() for the same aggregate function instance,
** the same buffer is returned.  Sqlite3_aggregate_context() is normally
** called once for each invocation of the xStep callback and then one
** last time when the xFinal callback is invoked.  ^(When no rows match
** an aggregate query, the xStep() callback of the aggregate function
** implementation is never called and xFinal() is called exactly once.
** In those cases, sqlite4_aggregate_context() might be called for the
** first time from within xFinal().)^
**
** ^The sqlite4_aggregate_context(C,N) routine returns a NULL pointer if N is
** less than or equal to zero or if a memory allocate error occurs.
**
** ^(The amount of space allocated by sqlite4_aggregate_context(C,N) is
** determined by the N parameter on first successful call.  Changing the
** value of N in subsequent call to sqlite4_aggregate_context() within
** the same aggregate function instance will not resize the memory
** allocation.)^
**
** ^SQLite automatically frees the memory allocated by 
** sqlite4_aggregate_context() when the aggregate query concludes.
**
** The first parameter must be a copy of the
** [sqlite4_context | SQL function context] that is the first parameter
** to the xStep or xFinal callback routine that implements the aggregate
** function.
**
** This routine must be called from the same thread in which
** the aggregate SQL function is running.
*/
SQLITE4_API void *sqlite4_aggregate_context(sqlite4_context*, int nBytes);

/*
** CAPIREF: User Data For Functions
**
** ^The sqlite4_user_data() interface returns a copy of
** the pointer that was the pUserData parameter (the 5th parameter)
** of the [sqlite4_create_function()]
** and [sqlite4_create_function16()] routines that originally
** registered the application defined function.
**
** This routine must be called from the same thread in which
** the application-defined function is running.
*/
SQLITE4_API void *sqlite4_user_data(sqlite4_context*);

/*
** CAPIREF: Database Connection For Functions
**
** ^The sqlite4_context_db_handle() interface returns a copy of
** the pointer to the [database connection] (the 1st parameter)
** of the [sqlite4_create_function()]
** and [sqlite4_create_function16()] routines that originally
** registered the application defined function.
*/
SQLITE4_API sqlite4 *sqlite4_context_db_handle(sqlite4_context*);
SQLITE4_API sqlite4_env *sqlite4_context_env(sqlite4_context*);

/*
** CAPIREF: Function Auxiliary Data
**
** The following two functions may be used by scalar SQL functions to
** associate metadata with argument values. If the same value is passed to
** multiple invocations of the same SQL function during query execution, under
** some circumstances the associated metadata may be preserved. This may
** be used, for example, to add a regular-expression matching scalar
** function. The compiled version of the regular expression is stored as
** metadata associated with the SQL value passed as the regular expression
** pattern.  The compiled regular expression can be reused on multiple
** invocations of the same function so that the original pattern string
** does not need to be recompiled on each invocation.
**
** ^The sqlite4_get_auxdata() interface returns a pointer to the metadata
** associated by the sqlite4_set_auxdata() function with the Nth argument
** value to the application-defined function. ^If no metadata has been ever
** been set for the Nth argument of the function, or if the corresponding
** function parameter has changed since the meta-data was set,
** then sqlite4_get_auxdata() returns a NULL pointer.
**
** ^The sqlite4_set_auxdata() interface saves the metadata
** pointed to by its 3rd parameter as the metadata for the N-th
** argument of the application-defined function.  Subsequent
** calls to sqlite4_get_auxdata() might return this data, if it has
** not been destroyed.
** ^If it is not NULL, SQLite will invoke the destructor
** function given by the 4th parameter to sqlite4_set_auxdata() on
** the metadata when the corresponding function parameter changes
** or when the SQL statement completes, whichever comes first.
**
** SQLite is free to call the destructor and drop metadata on any
** parameter of any function at any time.  ^The only guarantee is that
** the destructor will be called before the metadata is dropped.
**
** ^(In practice, metadata is preserved between function calls for
** expressions that are constant at compile time. This includes literal
** values and [parameters].)^
**
** These routines must be called from the same thread in which
** the SQL function is running.
*/
SQLITE4_API void *sqlite4_get_auxdata(sqlite4_context*, int N);
SQLITE4_API void sqlite4_set_auxdata(sqlite4_context*, int N, void*, void (*)(void*));


/*
** CAPIREF: Constants Defining Special Destructor Behavior
**
** These are special values for the destructor that is passed in as the
** final argument to routines like [sqlite4_result_blob()].  ^If the destructor
** argument is SQLITE4_STATIC, it means that the content pointer is constant
** and will never change.  It does not need to be destroyed.  ^The
** SQLITE4_TRANSIENT value means that the content will likely change in
** the near future and that SQLite should make its own private copy of
** the content before returning.
**
** The typedef is necessary to work around problems in certain
** C++ compilers.  See ticket #2191.
*/
typedef void (*sqlite4_destructor_type)(void*);
SQLITE4_API void sqlite4_dynamic(void*);
#define SQLITE4_STATIC      ((sqlite4_destructor_type)0)
#define SQLITE4_TRANSIENT   ((sqlite4_destructor_type)-1)
#define SQLITE4_DYNAMIC     (sqlite4_dynamic)


/*
** CAPIREF: Setting The Result Of An SQL Function
**
** These routines are used by the xFunc or xFinal callbacks that
** implement SQL functions and aggregates.  See
** [sqlite4_create_function()] and [sqlite4_create_function16()]
** for additional information.
**
** These functions work very much like the [parameter binding] family of
** functions used to bind values to host parameters in prepared statements.
** Refer to the [SQL parameter] documentation for additional information.
**
** ^The sqlite4_result_blob() interface sets the result from
** an application-defined function to be the BLOB whose content is pointed
** to by the second parameter and which is N bytes long where N is the
** third parameter.
**
** ^The sqlite4_result_zeroblob() interfaces set the result of
** the application-defined function to be a BLOB containing all zero
** bytes and N bytes in size, where N is the value of the 2nd parameter.
**
** ^The sqlite4_result_double() interface sets the result from
** an application-defined function to be a floating point value specified
** by its 2nd argument.
**
** ^The sqlite4_result_error() and sqlite4_result_error16() functions
** cause the implemented SQL function to throw an exception.
** ^SQLite uses the string pointed to by the
** 2nd parameter of sqlite4_result_error() or sqlite4_result_error16()
** as the text of an error message.  ^SQLite interprets the error
** message string from sqlite4_result_error() as UTF-8. ^SQLite
** interprets the string from sqlite4_result_error16() as UTF-16 in native
** byte order.  ^If the third parameter to sqlite4_result_error()
** or sqlite4_result_error16() is negative then SQLite takes as the error
** message all text up through the first zero character.
** ^If the third parameter to sqlite4_result_error() or
** sqlite4_result_error16() is non-negative then SQLite takes that many
** bytes (not characters) from the 2nd parameter as the error message.
** ^The sqlite4_result_error() and sqlite4_result_error16()
** routines make a private copy of the error message text before
** they return.  Hence, the calling function can deallocate or
** modify the text after they return without harm.
** ^The sqlite4_result_error_code() function changes the error code
** returned by SQLite as a result of an error in a function.  ^By default,
** the error code is SQLITE4_ERROR.  ^A subsequent call to sqlite4_result_error()
** or sqlite4_result_error16() resets the error code to SQLITE4_ERROR.
**
** ^The sqlite4_result_toobig() interface causes SQLite to throw an error
** indicating that a string or BLOB is too long to represent.
**
** ^The sqlite4_result_nomem() interface causes SQLite to throw an error
** indicating that a memory allocation failed.
**
** ^The sqlite4_result_int() interface sets the return value
** of the application-defined function to be the 32-bit signed integer
** value given in the 2nd argument.
** ^The sqlite4_result_int64() interface sets the return value
** of the application-defined function to be the 64-bit signed integer
** value given in the 2nd argument.
**
** ^The sqlite4_result_null() interface sets the return value
** of the application-defined function to be NULL.
**
** ^The sqlite4_result_text(), sqlite4_result_text16(),
** sqlite4_result_text16le(), and sqlite4_result_text16be() interfaces
** set the return value of the application-defined function to be
** a text string which is represented as UTF-8, UTF-16 native byte order,
** UTF-16 little endian, or UTF-16 big endian, respectively.
** ^SQLite takes the text result from the application from
** the 2nd parameter of the sqlite4_result_text* interfaces.
** ^If the 3rd parameter to the sqlite4_result_text* interfaces
** is negative, then SQLite takes result text from the 2nd parameter
** through the first zero character.
** ^If the 3rd parameter to the sqlite4_result_text* interfaces
** is non-negative, then as many bytes (not characters) of the text
** pointed to by the 2nd parameter are taken as the application-defined
** function result.  If the 3rd parameter is non-negative, then it
** must be the byte offset into the string where the NUL terminator would
** appear if the string where NUL terminated.  If any NUL characters occur
** in the string at a byte offset that is less than the value of the 3rd
** parameter, then the resulting string will contain embedded NULs and the
** result of expressions operating on strings with embedded NULs is undefined.
** ^If the 4th parameter to the sqlite4_result_text* interfaces
** or sqlite4_result_blob is a non-NULL pointer, then SQLite calls that
** function as the destructor on the text or BLOB result when it has
** finished using that result.
** ^If the 4th parameter to the sqlite4_result_text* interfaces or to
** sqlite4_result_blob is the special constant SQLITE4_STATIC, then SQLite
** assumes that the text or BLOB result is in constant space and does not
** copy the content of the parameter nor call a destructor on the content
** when it has finished using that result.
** ^If the 4th parameter to the sqlite4_result_text* interfaces
** or sqlite4_result_blob is the special constant SQLITE4_TRANSIENT
** then SQLite makes a copy of the result into space obtained from
** from [sqlite4_malloc()] before it returns.
**
** ^The sqlite4_result_value() interface sets the result of
** the application-defined function to be a copy the
** [unprotected sqlite4_value] object specified by the 2nd parameter.  ^The
** sqlite4_result_value() interface makes a copy of the [sqlite4_value]
** so that the [sqlite4_value] specified in the parameter may change or
** be deallocated after sqlite4_result_value() returns without harm.
** ^A [protected sqlite4_value] object may always be used where an
** [unprotected sqlite4_value] object is required, so either
** kind of [sqlite4_value] object can be used with this interface.
**
** If these routines are called from within the different thread
** than the one containing the application-defined function that received
** the [sqlite4_context] pointer, the results are undefined.
*/
SQLITE4_API void sqlite4_result_blob(sqlite4_context*, const void*, int, void(*)(void*));
SQLITE4_API void sqlite4_result_double(sqlite4_context*, double);
SQLITE4_API void sqlite4_result_error(sqlite4_context*, const char*, int);
SQLITE4_API void sqlite4_result_error16(sqlite4_context*, const void*, int);
SQLITE4_API void sqlite4_result_error_toobig(sqlite4_context*);
SQLITE4_API void sqlite4_result_error_nomem(sqlite4_context*);
SQLITE4_API void sqlite4_result_error_code(sqlite4_context*, int);
SQLITE4_API void sqlite4_result_int(sqlite4_context*, int);
SQLITE4_API void sqlite4_result_int64(sqlite4_context*, sqlite4_int64);
SQLITE4_API void sqlite4_result_null(sqlite4_context*);
SQLITE4_API void sqlite4_result_text(sqlite4_context*, const char*, int, void(*)(void*));
SQLITE4_API void sqlite4_result_text16(sqlite4_context*, const void*, int, void(*)(void*));
SQLITE4_API void sqlite4_result_text16le(sqlite4_context*, const void*, int,void(*)(void*));
SQLITE4_API void sqlite4_result_text16be(sqlite4_context*, const void*, int,void(*)(void*));
SQLITE4_API void sqlite4_result_value(sqlite4_context*, sqlite4_value*);
SQLITE4_API void sqlite4_result_zeroblob(sqlite4_context*, int n);

/*
** CAPIREF: Define New Collating Sequences
**
** ^This function adds, removes, or modifies a [collation] associated
** with the [database connection] specified as the first argument.
**
** ^The name of the collation is a UTF-8 string.
** ^Collation names that compare equal according to [sqlite4_strnicmp()] are
** considered to be the same name.
**
** ^(The third argument (eTextRep) must be one of the constants:
** <ul>
** <li> [SQLITE4_UTF8],
** <li> [SQLITE4_UTF16LE],
** <li> [SQLITE4_UTF16BE],
** <li> [SQLITE4_UTF16], or
** <li> [SQLITE4_UTF16_ALIGNED].
** </ul>)^
** ^The eTextRep argument determines the encoding of strings passed
** to the collating function callback, xCallback.
** ^The [SQLITE4_UTF16] and [SQLITE4_UTF16_ALIGNED] values for eTextRep
** force strings to be UTF16 with native byte order.
** ^The [SQLITE4_UTF16_ALIGNED] value for eTextRep forces strings to begin
** on an even byte address.
**
** ^The fourth argument, pArg, is an application data pointer that is passed
** through as the first argument to the collating function callback.
**
** ^The fifth argument, xCallback, is a pointer to the comparision function.
** ^The sixth argument, xMakeKey, is a pointer to a function that generates
** a sort key.
** ^Multiple functions can be registered using the same name but
** with different eTextRep parameters and SQLite will use whichever
** function requires the least amount of data transformation.
** ^If the xCallback argument is NULL then the collating function is
** deleted.  ^When all collating functions having the same name are deleted,
** that collation is no longer usable.
**
** ^The collating function callback is invoked with a copy of the pArg 
** application data pointer and with two strings in the encoding specified
** by the eTextRep argument.  The collating function must return an
** integer that is negative, zero, or positive
** if the first string is less than, equal to, or greater than the second,
** respectively.  A collating function must always return the same answer
** given the same inputs.  If two or more collating functions are registered
** to the same collation name (using different eTextRep values) then all
** must give an equivalent answer when invoked with equivalent strings.
** The collating function must obey the following properties for all
** strings A, B, and C:
**
** <ol>
** <li> If A==B then B==A.
** <li> If A==B and B==C then A==C.
** <li> If A&lt;B THEN B&gt;A.
** <li> If A&lt;B and B&lt;C then A&lt;C.
** </ol>
**
** If a collating function fails any of the above constraints and that
** collating function is  registered and used, then the behavior of SQLite
** is undefined.
**
** ^Collating functions are deleted when they are overridden by later
** calls to the collation creation functions or when the
** [database connection] is closed using [sqlite4_close()].
**
** ^The xDestroy callback is <u>not</u> called if the 
** sqlite4_create_collation() function fails.  Applications that invoke
** sqlite4_create_collation() with a non-NULL xDestroy argument should 
** check the return code and dispose of the application data pointer
** themselves rather than expecting SQLite to deal with it for them.
** This is different from every other SQLite interface.  The inconsistency 
** is unfortunate but cannot be changed without breaking backwards 
** compatibility.
**
** See also:  [sqlite4_collation_needed()] and [sqlite4_collation_needed16()].
*/
SQLITE4_API int sqlite4_create_collation(
  sqlite4*, 
  const char *zName, 
  int eTextRep, 
  void *pArg,
  int(*xCompare)(void*,int,const void*,int,const void*),
  int(*xMakeKey)(void*,int,const void*,int,void*),
  void(*xDestroy)(void*)
);

/*
** CAPIREF: Collation Needed Callbacks
**
** ^To avoid having to register all collation sequences before a database
** can be used, a single callback function may be registered with the
** [database connection] to be invoked whenever an undefined collation
** sequence is required.
**
** ^If the function is registered using the sqlite4_collation_needed() API,
** then it is passed the names of undefined collation sequences as strings
** encoded in UTF-8. ^If sqlite4_collation_needed16() is used,
** the names are passed as UTF-16 in machine native byte order.
** ^A call to either function replaces the existing collation-needed callback.
**
** ^(When the callback is invoked, the first argument passed is a copy
** of the second argument to sqlite4_collation_needed() or
** sqlite4_collation_needed16().  The second argument is the database
** connection.  The third argument is one of [SQLITE4_UTF8], [SQLITE4_UTF16BE],
** or [SQLITE4_UTF16LE], indicating the most desirable form of the collation
** sequence function required.  The fourth parameter is the name of the
** required collation sequence.)^
**
** The callback function should register the desired collation using
** [sqlite4_create_collation()], [sqlite4_create_collation16()], or
** [sqlite4_create_collation_v2()].
*/
SQLITE4_API int sqlite4_collation_needed(
  sqlite4*, 
  void*, 
  void(*)(void*,sqlite4*,int eTextRep,const char*)
);
SQLITE4_API int sqlite4_collation_needed16(
  sqlite4*, 
  void*,
  void(*)(void*,sqlite4*,int eTextRep,const void*)
);

/*
** CAPIREF: Suspend Execution For A Short Time
**
** The sqlite4_sleep() function causes the current thread to suspend execution
** for at least a number of milliseconds specified in its parameter.
**
** If the operating system does not support sleep requests with
** millisecond time resolution, then the time will be rounded up to
** the nearest second. The number of milliseconds of sleep actually
** requested from the operating system is returned.
**
** ^SQLite implements this interface by calling the xSleep()
** method of the default [sqlite4_vfs] object.  If the xSleep() method
** of the default VFS is not implemented correctly, or not implemented at
** all, then the behavior of sqlite4_sleep() may deviate from the description
** in the previous paragraphs.
*/
SQLITE4_API int sqlite4_sleep(int);

/*
** CAPIREF: Test For Auto-Commit Mode
** KEYWORDS: {autocommit mode}
**
** ^The sqlite4_get_autocommit() interface returns non-zero or
** zero if the given database connection is or is not in autocommit mode,
** respectively.  ^Autocommit mode is on by default.
** ^Autocommit mode is disabled by a [BEGIN] statement.
** ^Autocommit mode is re-enabled by a [COMMIT] or [ROLLBACK].
**
** If certain kinds of errors occur on a statement within a multi-statement
** transaction (errors including [SQLITE4_FULL], [SQLITE4_IOERR],
** [SQLITE4_NOMEM], [SQLITE4_BUSY], and [SQLITE4_INTERRUPT]) then the
** transaction might be rolled back automatically.  The only way to
** find out whether SQLite automatically rolled back the transaction after
** an error is to use this function.
**
** If another thread changes the autocommit status of the database
** connection while this routine is running, then the return value
** is undefined.
*/
SQLITE4_API int sqlite4_get_autocommit(sqlite4*);

/*
** CAPIREF: Find The Database Handle Of A Prepared Statement
**
** ^The sqlite4_db_handle interface returns the [database connection] handle
** to which a [prepared statement] belongs.  ^The [database connection]
** returned by sqlite4_db_handle is the same [database connection]
** that was the first argument
** to the [sqlite4_prepare()] call (or its variants) that was used to
** create the statement in the first place.
*/
SQLITE4_API sqlite4 *sqlite4_db_handle(sqlite4_stmt*);

/*
** CAPIREF: Return The Filename For A Database Connection
**
** ^The sqlite4_db_filename(D,N) interface returns a pointer to a filename
** associated with database N of connection D.  ^The main database file
** has the name "main".  If there is no attached database N on the database
** connection D, or if database N is a temporary or in-memory database, then
** a NULL pointer is returned.
**
** ^The filename returned by this function is the output of the
** xFullPathname method of the [VFS].  ^In other words, the filename
** will be an absolute pathname, even if the filename used
** to open the database originally was a URI or relative pathname.
*/
SQLITE4_API const char *sqlite4_db_filename(sqlite4 *db, const char *zDbName);

/*
** CAPIREF: Find the next prepared statement
**
** ^This interface returns a pointer to the next [prepared statement] after
** pStmt associated with the [database connection] pDb.  ^If pStmt is NULL
** then this interface returns a pointer to the first prepared statement
** associated with the database connection pDb.  ^If no prepared statement
** satisfies the conditions of this routine, it returns NULL.
**
** The [database connection] pointer D in a call to
** [sqlite4_next_stmt(D,S)] must refer to an open database
** connection and in particular must not be a NULL pointer.
*/
SQLITE4_API sqlite4_stmt *sqlite4_next_stmt(sqlite4 *pDb, sqlite4_stmt *pStmt);

/*
** CAPIREF: Free Memory Used By A Database Connection
**
** ^The sqlite4_db_release_memory(D) interface attempts to free as much heap
** memory as possible from database connection D.
*/
SQLITE4_API int sqlite4_db_release_memory(sqlite4*);

/*
** CAPIREF: Extract Metadata About A Column Of A Table
**
** ^This routine returns metadata about a specific column of a specific
** database table accessible using the [database connection] handle
** passed as the first function argument.
**
** ^The column is identified by the second, third and fourth parameters to
** this function. ^The second parameter is either the name of the database
** (i.e. "main", "temp", or an attached database) containing the specified
** table or NULL. ^If it is NULL, then all attached databases are searched
** for the table using the same algorithm used by the database engine to
** resolve unqualified table references.
**
** ^The third and fourth parameters to this function are the table and column
** name of the desired column, respectively. Neither of these parameters
** may be NULL.
**
** ^Metadata is returned by writing to the memory locations passed as the 5th
** and subsequent parameters to this function. ^Any of these arguments may be
** NULL, in which case the corresponding element of metadata is omitted.
**
** ^(<blockquote>
** <table border="1">
** <tr><th> Parameter <th> Output<br>Type <th>  Description
**
** <tr><td> 5th <td> const char* <td> Data type
** <tr><td> 6th <td> const char* <td> Name of default collation sequence
** <tr><td> 7th <td> int         <td> True if column has a NOT NULL constraint
** <tr><td> 8th <td> int         <td> True if column is part of the PRIMARY KEY
** <tr><td> 9th <td> int         <td> True if column is [AUTOINCREMENT]
** </table>
** </blockquote>)^
**
** ^The memory pointed to by the character pointers returned for the
** declaration type and collation sequence is valid only until the next
** call to any SQLite API function.
**
** ^If the specified table is actually a view, an [error code] is returned.
**
** ^If the specified column is "rowid", "oid" or "_rowid_" and an
** [INTEGER PRIMARY KEY] column has been explicitly declared, then the output
** parameters are set for the explicitly declared column. ^(If there is no
** explicitly declared [INTEGER PRIMARY KEY] column, then the output
** parameters are set as follows:
**
** <pre>
**     data type: "INTEGER"
**     collation sequence: "BINARY"
**     not null: 0
**     primary key: 1
**     auto increment: 0
** </pre>)^
**
** ^(This function may load one or more schemas from database files. If an
** error occurs during this process, or if the requested table or column
** cannot be found, an [error code] is returned and an error message left
** in the [database connection] (to be retrieved using sqlite4_errmsg()).)^
**
** ^This API is only available if the library was compiled with the
** [SQLITE4_ENABLE_COLUMN_METADATA] C-preprocessor symbol defined.
*/
SQLITE4_API int sqlite4_table_column_metadata(
  sqlite4 *db,                /* Connection handle */
  const char *zDbName,        /* Database name or NULL */
  const char *zTableName,     /* Table name */
  const char *zColumnName,    /* Column name */
  char const **pzDataType,    /* OUTPUT: Declared data type */
  char const **pzCollSeq,     /* OUTPUT: Collation sequence name */
  int *pNotNull,              /* OUTPUT: True if NOT NULL constraint exists */
  int *pPrimaryKey,           /* OUTPUT: True if column part of PK */
  int *pAutoinc               /* OUTPUT: True if column is auto-increment */
);

/*
** CAPIREF: Load An Extension
**
** ^This interface loads an SQLite extension library from the named file.
**
** ^The sqlite4_load_extension() interface attempts to load an
** SQLite extension library contained in the file zFile.
**
** ^The entry point is zProc.
** ^zProc may be 0, in which case the name of the entry point
** defaults to "sqlite4_extension_init".
** ^The sqlite4_load_extension() interface returns
** [SQLITE4_OK] on success and [SQLITE4_ERROR] if something goes wrong.
** ^If an error occurs and pzErrMsg is not 0, then the
** [sqlite4_load_extension()] interface shall attempt to
** fill *pzErrMsg with error message text stored in memory
** obtained from [sqlite4_malloc()]. The calling function
** should free this memory by calling [sqlite4_free()].
**
** ^Extension loading must be enabled using
** [sqlite4_enable_load_extension()] prior to calling this API,
** otherwise an error will be returned.
**
** See also the [load_extension() SQL function].
*/
SQLITE4_API int sqlite4_load_extension(
  sqlite4 *db,          /* Load the extension into this database connection */
  const char *zFile,    /* Name of the shared library containing extension */
  const char *zProc,    /* Entry point.  Derived from zFile if 0 */
  char **pzErrMsg       /* Put error message here if not 0 */
);

/*
** CAPIREF: Enable Or Disable Extension Loading
**
** ^So as not to open security holes in older applications that are
** unprepared to deal with extension loading, and as a means of disabling
** extension loading while evaluating user-entered SQL, the following API
** is provided to turn the [sqlite4_load_extension()] mechanism on and off.
**
** ^Extension loading is off by default. See ticket #1863.
** ^Call the sqlite4_enable_load_extension() routine with onoff==1
** to turn extension loading on and call it with onoff==0 to turn
** it back off again.
*/
SQLITE4_API int sqlite4_enable_load_extension(sqlite4 *db, int onoff);

/*
** The interface to the virtual-table mechanism is currently considered
** to be experimental.  The interface might change in incompatible ways.
** If this is a problem for you, do not use the interface at this time.
**
** When the virtual-table mechanism stabilizes, we will declare the
** interface fixed, support it indefinitely, and remove this comment.
*/

/*
** Structures used by the virtual table interface
*/
typedef struct sqlite4_vtab sqlite4_vtab;
typedef struct sqlite4_index_info sqlite4_index_info;
typedef struct sqlite4_vtab_cursor sqlite4_vtab_cursor;
typedef struct sqlite4_module sqlite4_module;

/*
** CAPIREF: Virtual Table Object
** KEYWORDS: sqlite4_module {virtual table module}
**
** This structure, sometimes called a "virtual table module", 
** defines the implementation of a [virtual tables].  
** This structure consists mostly of methods for the module.
**
** ^A virtual table module is created by filling in a persistent
** instance of this structure and passing a pointer to that instance
** to [sqlite4_create_module()] or [sqlite4_create_module_v2()].
** ^The registration remains valid until it is replaced by a different
** module or until the [database connection] closes.  The content
** of this structure must not change while it is registered with
** any database connection.
*/
struct sqlite4_module {
  int iVersion;
  int (*xCreate)(sqlite4*, void *pAux,
               int argc, const char *const*argv,
               sqlite4_vtab **ppVTab, char**);
  int (*xConnect)(sqlite4*, void *pAux,
               int argc, const char *const*argv,
               sqlite4_vtab **ppVTab, char**);
  int (*xBestIndex)(sqlite4_vtab *pVTab, sqlite4_index_info*);
  int (*xDisconnect)(sqlite4_vtab *pVTab);
  int (*xDestroy)(sqlite4_vtab *pVTab);
  int (*xOpen)(sqlite4_vtab *pVTab, sqlite4_vtab_cursor **ppCursor);
  int (*xClose)(sqlite4_vtab_cursor*);
  int (*xFilter)(sqlite4_vtab_cursor*, int idxNum, const char *idxStr,
                int argc, sqlite4_value **argv);
  int (*xNext)(sqlite4_vtab_cursor*);
  int (*xEof)(sqlite4_vtab_cursor*);
  int (*xColumn)(sqlite4_vtab_cursor*, sqlite4_context*, int);
  int (*xRowid)(sqlite4_vtab_cursor*, sqlite4_int64 *pRowid);
  int (*xUpdate)(sqlite4_vtab *, int, sqlite4_value **, sqlite4_int64 *);
  int (*xBegin)(sqlite4_vtab *pVTab);
  int (*xSync)(sqlite4_vtab *pVTab);
  int (*xCommit)(sqlite4_vtab *pVTab);
  int (*xRollback)(sqlite4_vtab *pVTab);
  int (*xFindFunction)(sqlite4_vtab *pVtab, int nArg, const char *zName,
                       void (**pxFunc)(sqlite4_context*,int,sqlite4_value**),
                       void **ppArg);
  int (*xRename)(sqlite4_vtab *pVtab, const char *zNew);
  /* The methods above are in version 1 of the sqlite_module object. Those 
  ** below are for version 2 and greater. */
  int (*xSavepoint)(sqlite4_vtab *pVTab, int);
  int (*xRelease)(sqlite4_vtab *pVTab, int);
  int (*xRollbackTo)(sqlite4_vtab *pVTab, int);
};

/*
** CAPIREF: Virtual Table Indexing Information
** KEYWORDS: sqlite4_index_info
**
** The sqlite4_index_info structure and its substructures is used as part
** of the [virtual table] interface to
** pass information into and receive the reply from the [xBestIndex]
** method of a [virtual table module].  The fields under **Inputs** are the
** inputs to xBestIndex and are read-only.  xBestIndex inserts its
** results into the **Outputs** fields.
**
** ^(The aConstraint[] array records WHERE clause constraints of the form:
**
** <blockquote>column OP expr</blockquote>
**
** where OP is =, &lt;, &lt;=, &gt;, or &gt;=.)^  ^(The particular operator is
** stored in aConstraint[].op using one of the
** [SQLITE4_INDEX_CONSTRAINT_EQ | SQLITE4_INDEX_CONSTRAINT_ values].)^
** ^(The index of the column is stored in
** aConstraint[].iColumn.)^  ^(aConstraint[].usable is TRUE if the
** expr on the right-hand side can be evaluated (and thus the constraint
** is usable) and false if it cannot.)^
**
** ^The optimizer automatically inverts terms of the form "expr OP column"
** and makes other simplifications to the WHERE clause in an attempt to
** get as many WHERE clause terms into the form shown above as possible.
** ^The aConstraint[] array only reports WHERE clause terms that are
** relevant to the particular virtual table being queried.
**
** ^Information about the ORDER BY clause is stored in aOrderBy[].
** ^Each term of aOrderBy records a column of the ORDER BY clause.
**
** The [xBestIndex] method must fill aConstraintUsage[] with information
** about what parameters to pass to xFilter.  ^If argvIndex>0 then
** the right-hand side of the corresponding aConstraint[] is evaluated
** and becomes the argvIndex-th entry in argv.  ^(If aConstraintUsage[].omit
** is true, then the constraint is assumed to be fully handled by the
** virtual table and is not checked again by SQLite.)^
**
** ^The idxNum and idxPtr values are recorded and passed into the
** [xFilter] method.
** ^[sqlite4_free()] is used to free idxPtr if and only if
** needToFreeIdxPtr is true.
**
** ^The orderByConsumed means that output from [xFilter]/[xNext] will occur in
** the correct order to satisfy the ORDER BY clause so that no separate
** sorting step is required.
**
** ^The estimatedCost value is an estimate of the cost of doing the
** particular lookup.  A full scan of a table with N entries should have
** a cost of N.  A binary search of a table of N entries should have a
** cost of approximately log(N).
*/
struct sqlite4_index_info {
  /* Inputs */
  int nConstraint;           /* Number of entries in aConstraint */
  struct sqlite4_index_constraint {
     int iColumn;              /* Column on left-hand side of constraint */
     unsigned char op;         /* Constraint operator */
     unsigned char usable;     /* True if this constraint is usable */
     int iTermOffset;          /* Used internally - xBestIndex should ignore */
  } *aConstraint;            /* Table of WHERE clause constraints */
  int nOrderBy;              /* Number of terms in the ORDER BY clause */
  struct sqlite4_index_orderby {
     int iColumn;              /* Column number */
     unsigned char desc;       /* True for DESC.  False for ASC. */
  } *aOrderBy;               /* The ORDER BY clause */
  /* Outputs */
  struct sqlite4_index_constraint_usage {
    int argvIndex;           /* if >0, constraint is part of argv to xFilter */
    unsigned char omit;      /* Do not code a test for this constraint */
  } *aConstraintUsage;
  int idxNum;                /* Number used to identify the index */
  char *idxStr;              /* String, possibly obtained from sqlite4_malloc */
  int needToFreeIdxStr;      /* Free idxStr using sqlite4_free() if true */
  int orderByConsumed;       /* True if output is already ordered */
  double estimatedCost;      /* Estimated cost of using this index */
};

/*
** CAPIREF: Virtual Table Constraint Operator Codes
**
** These macros defined the allowed values for the
** [sqlite4_index_info].aConstraint[].op field.  Each value represents
** an operator that is part of a constraint term in the wHERE clause of
** a query that uses a [virtual table].
*/
#define SQLITE4_INDEX_CONSTRAINT_EQ    2
#define SQLITE4_INDEX_CONSTRAINT_GT    4
#define SQLITE4_INDEX_CONSTRAINT_LE    8
#define SQLITE4_INDEX_CONSTRAINT_LT    16
#define SQLITE4_INDEX_CONSTRAINT_GE    32
#define SQLITE4_INDEX_CONSTRAINT_MATCH 64

/*
** CAPIREF: Register A Virtual Table Implementation
**
** ^These routines are used to register a new [virtual table module] name.
** ^Module names must be registered before
** creating a new [virtual table] using the module and before using a
** preexisting [virtual table] for the module.
**
** ^The module name is registered on the [database connection] specified
** by the first parameter.  ^The name of the module is given by the 
** second parameter.  ^The third parameter is a pointer to
** the implementation of the [virtual table module].   ^The fourth
** parameter is an arbitrary client data pointer that is passed through
** into the [xCreate] and [xConnect] methods of the virtual table module
** when a new virtual table is be being created or reinitialized.
**
** ^The sqlite4_create_module_v2() interface has a fifth parameter which
** is a pointer to a destructor for the pClientData.  ^SQLite will
** invoke the destructor function (if it is not NULL) when SQLite
** no longer needs the pClientData pointer.  ^The destructor will also
** be invoked if the call to sqlite4_create_module_v2() fails.
** ^The sqlite4_create_module()
** interface is equivalent to sqlite4_create_module_v2() with a NULL
** destructor.
*/
SQLITE4_API int sqlite4_create_module(
  sqlite4 *db,               /* SQLite connection to register module with */
  const char *zName,         /* Name of the module */
  const sqlite4_module *p,   /* Methods for the module */
  void *pClientData          /* Client data for xCreate/xConnect */
);
SQLITE4_API int sqlite4_create_module_v2(
  sqlite4 *db,               /* SQLite connection to register module with */
  const char *zName,         /* Name of the module */
  const sqlite4_module *p,   /* Methods for the module */
  void *pClientData,         /* Client data for xCreate/xConnect */
  void(*xDestroy)(void*)     /* Module destructor function */
);

/*
** CAPIREF: Virtual Table Instance Object
** KEYWORDS: sqlite4_vtab
**
** Every [virtual table module] implementation uses a subclass
** of this object to describe a particular instance
** of the [virtual table].  Each subclass will
** be tailored to the specific needs of the module implementation.
** The purpose of this superclass is to define certain fields that are
** common to all module implementations.
**
** ^Virtual tables methods can set an error message by assigning a
** string obtained from [sqlite4_mprintf()] to zErrMsg.  The method should
** take care that any prior string is freed by a call to [sqlite4_free()]
** prior to assigning a new string to zErrMsg.  ^After the error message
** is delivered up to the client application, the string will be automatically
** freed by sqlite4_free() and the zErrMsg field will be zeroed.
*/
struct sqlite4_vtab {
  const sqlite4_module *pModule;  /* The module for this virtual table */
  int nRef;                       /* NO LONGER USED */
  char *zErrMsg;                  /* Error message from sqlite4_mprintf() */
  /* Virtual table implementations will typically add additional fields */
};

/*
** CAPIREF: Virtual Table Cursor Object
** KEYWORDS: sqlite4_vtab_cursor {virtual table cursor}
**
** Every [virtual table module] implementation uses a subclass of the
** following structure to describe cursors that point into the
** [virtual table] and are used
** to loop through the virtual table.  Cursors are created using the
** [sqlite4_module.xOpen | xOpen] method of the module and are destroyed
** by the [sqlite4_module.xClose | xClose] method.  Cursors are used
** by the [xFilter], [xNext], [xEof], [xColumn], and [xRowid] methods
** of the module.  Each module implementation will define
** the content of a cursor structure to suit its own needs.
**
** This superclass exists in order to define fields of the cursor that
** are common to all implementations.
*/
struct sqlite4_vtab_cursor {
  sqlite4_vtab *pVtab;      /* Virtual table of this cursor */
  /* Virtual table implementations will typically add additional fields */
};

/*
** CAPIREF: Declare The Schema Of A Virtual Table
**
** ^The [xCreate] and [xConnect] methods of a
** [virtual table module] call this interface
** to declare the format (the names and datatypes of the columns) of
** the virtual tables they implement.
*/
SQLITE4_API int sqlite4_declare_vtab(sqlite4*, const char *zSQL);

/*
** CAPIREF: Overload A Function For A Virtual Table
**
** ^(Virtual tables can provide alternative implementations of functions
** using the [xFindFunction] method of the [virtual table module].  
** But global versions of those functions
** must exist in order to be overloaded.)^
**
** ^(This API makes sure a global version of a function with a particular
** name and number of parameters exists.  If no such function exists
** before this API is called, a new function is created.)^  ^The implementation
** of the new function always causes an exception to be thrown.  So
** the new function is not good for anything by itself.  Its only
** purpose is to be a placeholder function that can be overloaded
** by a [virtual table].
*/
SQLITE4_API int sqlite4_overload_function(sqlite4*, const char *zFuncName, int nArg);

/*
** CAPIREF: Mutexes
**
** The SQLite core uses these routines for thread
** synchronization. Though they are intended for internal
** use by SQLite, code that links against SQLite is
** permitted to use any of these routines.
**
** The SQLite source code contains multiple implementations
** of these mutex routines.  An appropriate implementation
** is selected automatically at compile-time.  ^(The following
** implementations are available in the SQLite core:
**
** <ul>
** <li>   SQLITE4_MUTEX_PTHREADS
** <li>   SQLITE4_MUTEX_W32
** <li>   SQLITE4_MUTEX_NOOP
** </ul>)^
**
** ^The SQLITE4_MUTEX_NOOP implementation is a set of routines
** that does no real locking and is appropriate for use in
** a single-threaded application.  ^The SQLITE4_MUTEX_PTHREADS
** and SQLITE4_MUTEX_W32 implementations
** are appropriate for use on Unix and Windows.
**
** ^(If SQLite is compiled with the SQLITE4_MUTEX_APPDEF preprocessor
** macro defined (with "-DSQLITE4_MUTEX_APPDEF=1"), then no mutex
** implementation is included with the library. In this case the
** application must supply a custom mutex implementation using the
** [SQLITE4_CONFIG_MUTEX] option of the sqlite4_env_config() function
** before calling sqlite4_initialize() or any other public sqlite4_
** function that calls sqlite4_initialize().)^
**
** ^The sqlite4_mutex_alloc() routine allocates a new
** mutex and returns a pointer to it. ^If it returns NULL
** that means that a mutex could not be allocated.  ^SQLite
** will unwind its stack and return an error.  ^(The argument
** to sqlite4_mutex_alloc() is one of these integer constants:
**
** <ul>
** <li>  SQLITE4_MUTEX_FAST
** <li>  SQLITE4_MUTEX_RECURSIVE
** </ul>)^
**
** ^The new mutex is recursive when SQLITE4_MUTEX_RECURSIVE
** is used but not necessarily so when SQLITE4_MUTEX_FAST is used.
** The mutex implementation does not need to make a distinction
** between SQLITE4_MUTEX_RECURSIVE and SQLITE4_MUTEX_FAST if it does
** not want to.  ^SQLite will only request a recursive mutex in
** cases where it really needs one.  ^If a faster non-recursive mutex
** implementation is available on the host platform, the mutex subsystem
** might return such a mutex in response to SQLITE4_MUTEX_FAST.
**
** ^The sqlite4_mutex_free() routine deallocates a previously
** allocated mutex. 
**
** ^The sqlite4_mutex_enter() and sqlite4_mutex_try() routines attempt
** to enter a mutex.  ^If another thread is already within the mutex,
** sqlite4_mutex_enter() will block and sqlite4_mutex_try() will return
** SQLITE4_BUSY.  ^The sqlite4_mutex_try() interface returns [SQLITE4_OK]
** upon successful entry.  ^(Mutexes created using
** SQLITE4_MUTEX_RECURSIVE can be entered multiple times by the same thread.
** In such cases the,
** mutex must be exited an equal number of times before another thread
** can enter.)^  ^(If the same thread tries to enter any other
** kind of mutex more than once, the behavior is undefined.
** SQLite will never exhibit
** such behavior in its own use of mutexes.)^
**
** ^(Some systems (for example, Windows 95) do not support the operation
** implemented by sqlite4_mutex_try().  On those systems, sqlite4_mutex_try()
** will always return SQLITE4_BUSY.  The SQLite core only ever uses
** sqlite4_mutex_try() as an optimization so this is acceptable behavior.)^
**
** ^The sqlite4_mutex_leave() routine exits a mutex that was
** previously entered by the same thread.   ^(The behavior
** is undefined if the mutex is not currently entered by the
** calling thread or is not currently allocated.  SQLite will
** never do either.)^
**
** ^If the argument to sqlite4_mutex_enter(), sqlite4_mutex_try(), or
** sqlite4_mutex_leave() is a NULL pointer, then all three routines
** behave as no-ops.
**
** See also: [sqlite4_mutex_held()] and [sqlite4_mutex_notheld()].
*/
SQLITE4_API sqlite4_mutex *sqlite4_mutex_alloc(sqlite4_env*, int);
SQLITE4_API void sqlite4_mutex_free(sqlite4_mutex*);
SQLITE4_API void sqlite4_mutex_enter(sqlite4_mutex*);
SQLITE4_API int sqlite4_mutex_try(sqlite4_mutex*);
SQLITE4_API void sqlite4_mutex_leave(sqlite4_mutex*);

/*
** CAPIREF: Mutex Methods Object
**
** An instance of this structure defines the low-level routines
** used to allocate and use mutexes.
**
** Usually, the default mutex implementations provided by SQLite are
** sufficient, however the user has the option of substituting a custom
** implementation for specialized deployments or systems for which SQLite
** does not provide a suitable implementation. In this case, the user
** creates and populates an instance of this structure to pass
** to sqlite4_env_config() along with the [SQLITE4_CONFIG_MUTEX] option.
** Additionally, an instance of this structure can be used as an
** output variable when querying the system for the current mutex
** implementation, using the [SQLITE4_CONFIG_GETMUTEX] option.
**
** ^The xMutexInit method defined by this structure is invoked as
** part of system initialization by the sqlite4_initialize() function.
** ^The xMutexInit routine is called by SQLite exactly once for each
** effective call to [sqlite4_initialize()].
**
** ^The xMutexEnd method defined by this structure is invoked as
** part of system shutdown by the sqlite4_shutdown() function. The
** implementation of this method is expected to release all outstanding
** resources obtained by the mutex methods implementation, especially
** those obtained by the xMutexInit method.  ^The xMutexEnd()
** interface is invoked exactly once for each call to [sqlite4_shutdown()].
**
** ^(The remaining seven methods defined by this structure (xMutexAlloc,
** xMutexFree, xMutexEnter, xMutexTry, xMutexLeave, xMutexHeld and
** xMutexNotheld) implement the following interfaces (respectively):
**
** <ul>
**   <li>  [sqlite4_mutex_alloc()] </li>
**   <li>  [sqlite4_mutex_free()] </li>
**   <li>  [sqlite4_mutex_enter()] </li>
**   <li>  [sqlite4_mutex_try()] </li>
**   <li>  [sqlite4_mutex_leave()] </li>
**   <li>  [sqlite4_mutex_held()] </li>
**   <li>  [sqlite4_mutex_notheld()] </li>
** </ul>)^
**
** The only difference is that the public sqlite4_XXX functions enumerated
** above silently ignore any invocations that pass a NULL pointer instead
** of a valid mutex handle. The implementations of the methods defined
** by this structure are not required to handle this case, the results
** of passing a NULL pointer instead of a valid mutex handle are undefined
** (i.e. it is acceptable to provide an implementation that segfaults if
** it is passed a NULL pointer).
**
** The xMutexInit() method must be threadsafe.  ^It must be harmless to
** invoke xMutexInit() multiple times within the same process and without
** intervening calls to xMutexEnd().  Second and subsequent calls to
** xMutexInit() must be no-ops.
**
** ^xMutexInit() must not use SQLite memory allocation ([sqlite4_malloc()]
** and its associates).  ^Similarly, xMutexAlloc() must not use SQLite memory
** allocation for a static mutex.  ^However xMutexAlloc() may use SQLite
** memory allocation for a fast or recursive mutex.
**
** ^SQLite will invoke the xMutexEnd() method when [sqlite4_shutdown()] is
** called, but only if the prior call to xMutexInit returned SQLITE4_OK.
** If xMutexInit fails in any way, it is expected to clean up after itself
** prior to returning.
*/
typedef struct sqlite4_mutex_methods sqlite4_mutex_methods;
struct sqlite4_mutex_methods {
  int (*xMutexInit)(void*);
  int (*xMutexEnd)(void*);
  sqlite4_mutex *(*xMutexAlloc)(void*,int);
  void (*xMutexFree)(sqlite4_mutex *);
  void (*xMutexEnter)(sqlite4_mutex *);
  int (*xMutexTry)(sqlite4_mutex *);
  void (*xMutexLeave)(sqlite4_mutex *);
  int (*xMutexHeld)(sqlite4_mutex *);
  int (*xMutexNotheld)(sqlite4_mutex *);
  void *pMutexEnv;
};

/*
** CAPIREF: Mutex Verification Routines
**
** The sqlite4_mutex_held() and sqlite4_mutex_notheld() routines
** are intended for use inside assert() statements.  ^The SQLite core
** never uses these routines except inside an assert() and applications
** are advised to follow the lead of the core.  ^The SQLite core only
** provides implementations for these routines when it is compiled
** with the SQLITE4_DEBUG flag.  ^External mutex implementations
** are only required to provide these routines if SQLITE4_DEBUG is
** defined and if NDEBUG is not defined.
**
** ^These routines should return true if the mutex in their argument
** is held or not held, respectively, by the calling thread.
**
** ^The implementation is not required to provide versions of these
** routines that actually work. If the implementation does not provide working
** versions of these routines, it should at least provide stubs that always
** return true so that one does not get spurious assertion failures.
**
** ^If the argument to sqlite4_mutex_held() is a NULL pointer then
** the routine should return 1.   This seems counter-intuitive since
** clearly the mutex cannot be held if it does not exist.  But
** the reason the mutex does not exist is because the build is not
** using mutexes.  And we do not want the assert() containing the
** call to sqlite4_mutex_held() to fail, so a non-zero return is
** the appropriate thing to do.  ^The sqlite4_mutex_notheld()
** interface should also return 1 when given a NULL pointer.
*/
#ifndef NDEBUG
SQLITE4_API int sqlite4_mutex_held(sqlite4_mutex*);
SQLITE4_API int sqlite4_mutex_notheld(sqlite4_mutex*);
#endif

/*
** CAPIREF: Mutex Types
**
** The [sqlite4_mutex_alloc()] interface takes a single argument
** which is one of these integer constants.
**
** The set of static mutexes may change from one SQLite release to the
** next.  Applications that override the built-in mutex logic must be
** prepared to accommodate additional static mutexes.
*/
#define SQLITE4_MUTEX_FAST             0
#define SQLITE4_MUTEX_RECURSIVE        1

/*
** CAPIREF: Retrieve the mutex for a database connection
**
** ^This interface returns a pointer the [sqlite4_mutex] object that 
** serializes access to the [database connection] given in the argument
** when the [threading mode] is Serialized.
** ^If the [threading mode] is Single-thread or Multi-thread then this
** routine returns a NULL pointer.
*/
SQLITE4_API sqlite4_mutex *sqlite4_db_mutex(sqlite4*);

/*
** CAPIREF: Low-Level Control Of Database Backends
**
** ^The [sqlite4_kvstore_control()] interface makes a direct call to the
** xControl method of the key-value store associated with the particular 
** database identified by the second argument. ^The name of the database 
** is "main" for the main database or "temp" for the TEMP database, or the 
** name that appears after the AS keyword for databases that were added 
** using the [ATTACH] SQL command. ^A NULL pointer can be used in place 
** of "main" to refer to the main database file.
**
** ^The third and fourth parameters to this routine are passed directly 
** through to the second and third parameters of the
** sqlite4_kv_methods.xControl method. ^The return value of the xControl
** call becomes the return value of this routine.
**
** ^If the second parameter (zDbName) does not match the name of any
** open database file, then SQLITE4_ERROR is returned.  ^This error
** code is not remembered and will not be recalled by [sqlite4_errcode()]
** or [sqlite4_errmsg()]. The underlying xControl method might also return 
** SQLITE4_ERROR. There is no way to distinguish between an incorrect zDbName 
** and an SQLITE4_ERROR return from the underlying xControl method.
*/
SQLITE4_API int sqlite4_kvstore_control(sqlite4*, const char *zDbName, int op, void*);

/*
** <dl>
** <dt>SQLITE4_KVCTRL_LSM_HANDLE</dt><dd>
**
** <dt>SQLITE4_KVCTRL_SYNCHRONOUS</dt><dd>
** This op is used to configure or query the synchronous level of the 
** database backend (either OFF, NORMAL or FULL). The fourth parameter passed 
** to kvstore_control should be of type (int *). Call the value that the
** parameter points to N. If N is initially 0, 1 or 2, then the database 
** backend should attempt to change the synchronous level to OFF, NORMAL 
** or FULL, respectively. Regardless of its initial value, N is set to 
** the current (possibly updated) synchronous level before returning (
** 0, 1 or 2).
*/
#define SQLITE4_KVCTRL_LSM_HANDLE       1
#define SQLITE4_KVCTRL_SYNCHRONOUS      2
#define SQLITE4_KVCTRL_LSM_FLUSH        3
#define SQLITE4_KVCTRL_LSM_MERGE        4
#define SQLITE4_KVCTRL_LSM_CHECKPOINT   5

/*
** CAPIREF: Testing Interface
**
** ^The sqlite4_test_control() interface is used to read out internal
** state of SQLite and to inject faults into SQLite for testing
** purposes.  ^The first parameter is an operation code that determines
** the number, meaning, and operation of all subsequent parameters.
**
** This interface is not for use by applications.  It exists solely
** for verifying the correct operation of the SQLite library.  Depending
** on how the SQLite library is compiled, this interface might not exist.
**
** The details of the operation codes, their meanings, the parameters
** they take, and what they do are all subject to change without notice.
** Unlike most of the SQLite API, this function is not guaranteed to
** operate consistently from one release to the next.
*/
SQLITE4_API int sqlite4_test_control(int op, ...);

/*
** CAPIREF: Testing Interface Operation Codes
**
** These constants are the valid operation code parameters used
** as the first argument to [sqlite4_test_control()].
**
** These parameters and their meanings are subject to change
** without notice.  These values are for testing purposes only.
** Applications should not use any of these parameters or the
** [sqlite4_test_control()] interface.
*/
#define SQLITE4_TESTCTRL_FIRST                    1
#define SQLITE4_TESTCTRL_FAULT_INSTALL            2
#define SQLITE4_TESTCTRL_ASSERT                   3
#define SQLITE4_TESTCTRL_ALWAYS                   4
#define SQLITE4_TESTCTRL_RESERVE                  5
#define SQLITE4_TESTCTRL_OPTIMIZATIONS            6
#define SQLITE4_TESTCTRL_ISKEYWORD                7
#define SQLITE4_TESTCTRL_LOCALTIME_FAULT          8
#define SQLITE4_TESTCTRL_EXPLAIN_STMT             9
#define SQLITE4_TESTCTRL_LAST                     9

/*
** CAPIREF: SQLite Runtime Status
**
** ^This interface is used to retrieve runtime status information
** about the performance of SQLite, and optionally to reset various
** highwater marks.  ^The first argument is an integer code for
** the specific parameter to measure.  ^(Recognized integer codes
** are of the form [status parameters | SQLITE4_STATUS_...].)^
** ^The current value of the parameter is returned into *pCurrent.
** ^The highest recorded value is returned in *pHighwater.  ^If the
** resetFlag is true, then the highest record value is reset after
** *pHighwater is written.  ^(Some parameters do not record the highest
** value.  For those parameters
** nothing is written into *pHighwater and the resetFlag is ignored.)^
** ^(Other parameters record only the highwater mark and not the current
** value.  For these latter parameters nothing is written into *pCurrent.)^
**
** ^The sqlite4_status() routine returns SQLITE4_OK on success and a
** non-zero [error code] on failure.
**
** This routine is threadsafe but is not atomic.  This routine can be
** called while other threads are running the same or different SQLite
** interfaces.  However the values returned in *pCurrent and
** *pHighwater reflect the status of SQLite at different points in time
** and it is possible that another thread might change the parameter
** in between the times when *pCurrent and *pHighwater are written.
**
** See also: [sqlite4_db_status()]
*/
SQLITE4_API int sqlite4_env_status(
  sqlite4_env *pEnv,
  int op,
  sqlite4_uint64 *pCurrent,
  sqlite4_uint64 *pHighwater,
  int resetFlag
);


/*
** CAPIREF: Status Parameters
** KEYWORDS: {status parameters}
**
** These integer constants designate various run-time status parameters
** that can be returned by [sqlite4_status()].
**
** <dl>
** [[SQLITE4_STATUS_MEMORY_USED]] ^(<dt>SQLITE4_STATUS_MEMORY_USED</dt>
** <dd>This parameter is the current amount of memory checked out
** using [sqlite4_malloc()], either directly or indirectly.  The
** figure includes calls made to [sqlite4_malloc()] by the application
** and internal memory usage by the SQLite library.  Scratch memory
** controlled by [SQLITE4_CONFIG_SCRATCH] and auxiliary page-cache
** memory controlled by [SQLITE4_CONFIG_PAGECACHE] is not included in
** this parameter.  The amount returned is the sum of the allocation
** sizes as reported by the xSize method in [sqlite4_mem_methods].</dd>)^
**
** [[SQLITE4_STATUS_MALLOC_SIZE]] ^(<dt>SQLITE4_STATUS_MALLOC_SIZE</dt>
** <dd>This parameter records the largest memory allocation request
** handed to [sqlite4_malloc()] or [sqlite4_realloc()] (or their
** internal equivalents).  Only the value returned in the
** *pHighwater parameter to [sqlite4_status()] is of interest.  
** The value written into the *pCurrent parameter is undefined.</dd>)^
**
** [[SQLITE4_STATUS_MALLOC_COUNT]] ^(<dt>SQLITE4_STATUS_MALLOC_COUNT</dt>
** <dd>This parameter records the number of separate memory allocations
** currently checked out.</dd>)^
**
** [[SQLITE4_STATUS_PARSER_STACK]] ^(<dt>SQLITE4_STATUS_PARSER_STACK</dt>
** <dd>This parameter records the deepest parser stack.  It is only
** meaningful if SQLite is compiled with [YYTRACKMAXSTACKDEPTH].</dd>)^
** </dl>
**
** New status parameters may be added from time to time.
*/
#define SQLITE4_ENVSTATUS_MEMORY_USED          0
#define SQLITE4_ENVSTATUS_MALLOC_SIZE          1
#define SQLITE4_ENVSTATUS_MALLOC_COUNT         2
#define SQLITE4_ENVSTATUS_PARSER_STACK         3

/*
** CAPIREF: Database Connection Status
**
** ^This interface is used to retrieve runtime status information 
** about a single [database connection].  ^The first argument is the
** database connection object to be interrogated.  ^The second argument
** is an integer constant, taken from the set of
** [SQLITE4_DBSTATUS options], that
** determines the parameter to interrogate.  The set of 
** [SQLITE4_DBSTATUS options] is likely
** to grow in future releases of SQLite.
**
** ^The current value of the requested parameter is written into *pCur
** and the highest instantaneous value is written into *pHiwtr.  ^If
** the resetFlg is true, then the highest instantaneous value is
** reset back down to the current value.
**
** ^The sqlite4_db_status() routine returns SQLITE4_OK on success and a
** non-zero [error code] on failure.
**
** See also: [sqlite4_status()] and [sqlite4_stmt_status()].
*/
SQLITE4_API int sqlite4_db_status(sqlite4*, int op, int *pCur, int *pHiwtr, int resetFlg);

/*
** CAPIREF: Status Parameters for database connections
** KEYWORDS: {SQLITE4_DBSTATUS options}
**
** These constants are the available integer "verbs" that can be passed as
** the second argument to the [sqlite4_db_status()] interface.
**
** New verbs may be added in future releases of SQLite. Existing verbs
** might be discontinued. Applications should check the return code from
** [sqlite4_db_status()] to make sure that the call worked.
** The [sqlite4_db_status()] interface will return a non-zero error code
** if a discontinued or unsupported verb is invoked.
**
** <dl>
** [[SQLITE4_DBSTATUS_LOOKASIDE_USED]] ^(<dt>SQLITE4_DBSTATUS_LOOKASIDE_USED</dt>
** <dd>This parameter returns the number of lookaside memory slots currently
** checked out.</dd>)^
**
** [[SQLITE4_DBSTATUS_LOOKASIDE_HIT]] ^(<dt>SQLITE4_DBSTATUS_LOOKASIDE_HIT</dt>
** <dd>This parameter returns the number malloc attempts that were 
** satisfied using lookaside memory. Only the high-water value is meaningful;
** the current value is always zero.)^
**
** [[SQLITE4_DBSTATUS_LOOKASIDE_MISS_SIZE]]
** ^(<dt>SQLITE4_DBSTATUS_LOOKASIDE_MISS_SIZE</dt>
** <dd>This parameter returns the number malloc attempts that might have
** been satisfied using lookaside memory but failed due to the amount of
** memory requested being larger than the lookaside slot size.
** Only the high-water value is meaningful;
** the current value is always zero.)^
**
** [[SQLITE4_DBSTATUS_LOOKASIDE_MISS_FULL]]
** ^(<dt>SQLITE4_DBSTATUS_LOOKASIDE_MISS_FULL</dt>
** <dd>This parameter returns the number malloc attempts that might have
** been satisfied using lookaside memory but failed due to all lookaside
** memory already being in use.
** Only the high-water value is meaningful;
** the current value is always zero.)^
**
** [[SQLITE4_DBSTATUS_CACHE_USED]] ^(<dt>SQLITE4_DBSTATUS_CACHE_USED</dt>
** <dd>This parameter returns the approximate number of of bytes of heap
** memory used by all pager caches associated with the database connection.)^
** ^The highwater mark associated with SQLITE4_DBSTATUS_CACHE_USED is always 0.
**
** [[SQLITE4_DBSTATUS_SCHEMA_USED]] ^(<dt>SQLITE4_DBSTATUS_SCHEMA_USED</dt>
** <dd>This parameter returns the approximate number of of bytes of heap
** memory used to store the schema for all databases associated
** with the connection - main, temp, and any [ATTACH]-ed databases.)^ 
** ^The full amount of memory used by the schemas is reported, even if the
** schema memory is shared with other database connections due to
** [shared cache mode] being enabled.
** ^The highwater mark associated with SQLITE4_DBSTATUS_SCHEMA_USED is always 0.
**
** [[SQLITE4_DBSTATUS_STMT_USED]] ^(<dt>SQLITE4_DBSTATUS_STMT_USED</dt>
** <dd>This parameter returns the approximate number of of bytes of heap
** and lookaside memory used by all prepared statements associated with
** the database connection.)^
** ^The highwater mark associated with SQLITE4_DBSTATUS_STMT_USED is always 0.
** </dd>
**
** [[SQLITE4_DBSTATUS_CACHE_HIT]] ^(<dt>SQLITE4_DBSTATUS_CACHE_HIT</dt>
** <dd>This parameter returns the number of pager cache hits that have
** occurred.)^ ^The highwater mark associated with SQLITE4_DBSTATUS_CACHE_HIT 
** is always 0.
** </dd>
**
** [[SQLITE4_DBSTATUS_CACHE_MISS]] ^(<dt>SQLITE4_DBSTATUS_CACHE_MISS</dt>
** <dd>This parameter returns the number of pager cache misses that have
** occurred.)^ ^The highwater mark associated with SQLITE4_DBSTATUS_CACHE_MISS 
** is always 0.
** </dd>
** </dl>
*/
#define SQLITE4_DBSTATUS_LOOKASIDE_USED       0
#define SQLITE4_DBSTATUS_CACHE_USED           1
#define SQLITE4_DBSTATUS_SCHEMA_USED          2
#define SQLITE4_DBSTATUS_STMT_USED            3
#define SQLITE4_DBSTATUS_LOOKASIDE_HIT        4
#define SQLITE4_DBSTATUS_LOOKASIDE_MISS_SIZE  5
#define SQLITE4_DBSTATUS_LOOKASIDE_MISS_FULL  6
#define SQLITE4_DBSTATUS_CACHE_HIT            7
#define SQLITE4_DBSTATUS_CACHE_MISS           8
#define SQLITE4_DBSTATUS_MAX                  8   /* Largest defined DBSTATUS */


/*
** CAPIREF: Prepared Statement Status
**
** ^(Each prepared statement maintains various
** [SQLITE4_STMTSTATUS counters] that measure the number
** of times it has performed specific operations.)^  These counters can
** be used to monitor the performance characteristics of the prepared
** statements.  For example, if the number of table steps greatly exceeds
** the number of table searches or result rows, that would tend to indicate
** that the prepared statement is using a full table scan rather than
** an index.  
**
** ^(This interface is used to retrieve and reset counter values from
** a [prepared statement].  The first argument is the prepared statement
** object to be interrogated.  The second argument
** is an integer code for a specific [SQLITE4_STMTSTATUS counter]
** to be interrogated.)^
** ^The current value of the requested counter is returned.
** ^If the resetFlg is true, then the counter is reset to zero after this
** interface call returns.
**
** See also: [sqlite4_status()] and [sqlite4_db_status()].
*/
SQLITE4_API int sqlite4_stmt_status(sqlite4_stmt*, int op,int resetFlg);

/*
** CAPIREF: Status Parameters for prepared statements
** KEYWORDS: {SQLITE4_STMTSTATUS counter} {SQLITE4_STMTSTATUS counters}
**
** These preprocessor macros define integer codes that name counter
** values associated with the [sqlite4_stmt_status()] interface.
** The meanings of the various counters are as follows:
**
** <dl>
** [[SQLITE4_STMTSTATUS_FULLSCAN_STEP]] <dt>SQLITE4_STMTSTATUS_FULLSCAN_STEP</dt>
** <dd>^This is the number of times that SQLite has stepped forward in
** a table as part of a full table scan.  Large numbers for this counter
** may indicate opportunities for performance improvement through 
** careful use of indices.</dd>
**
** [[SQLITE4_STMTSTATUS_SORT]] <dt>SQLITE4_STMTSTATUS_SORT</dt>
** <dd>^This is the number of sort operations that have occurred.
** A non-zero value in this counter may indicate an opportunity to
** improvement performance through careful use of indices.</dd>
**
** [[SQLITE4_STMTSTATUS_AUTOINDEX]] <dt>SQLITE4_STMTSTATUS_AUTOINDEX</dt>
** <dd>^This is the number of rows inserted into transient indices that
** were created automatically in order to help joins run faster.
** A non-zero value in this counter may indicate an opportunity to
** improvement performance by adding permanent indices that do not
** need to be reinitialized each time the statement is run.</dd>
** </dl>
*/
#define SQLITE4_STMTSTATUS_FULLSCAN_STEP     1
#define SQLITE4_STMTSTATUS_SORT              2
#define SQLITE4_STMTSTATUS_AUTOINDEX         3


/*
** CAPIREF: Unlock Notification
**
** ^When running in shared-cache mode, a database operation may fail with
** an [SQLITE4_LOCKED] error if the required locks on the shared-cache or
** individual tables within the shared-cache cannot be obtained. See
** [SQLite Shared-Cache Mode] for a description of shared-cache locking. 
** ^This API may be used to register a callback that SQLite will invoke 
** when the connection currently holding the required lock relinquishes it.
** ^This API is only available if the library was compiled with the
** [SQLITE4_ENABLE_UNLOCK_NOTIFY] C-preprocessor symbol defined.
**
** See Also: [Using the SQLite Unlock Notification Feature].
**
** ^Shared-cache locks are released when a database connection concludes
** its current transaction, either by committing it or rolling it back. 
**
** ^When a connection (known as the blocked connection) fails to obtain a
** shared-cache lock and SQLITE4_LOCKED is returned to the caller, the
** identity of the database connection (the blocking connection) that
** has locked the required resource is stored internally. ^After an 
** application receives an SQLITE4_LOCKED error, it may call the
** sqlite4_unlock_notify() method with the blocked connection handle as 
** the first argument to register for a callback that will be invoked
** when the blocking connections current transaction is concluded. ^The
** callback is invoked from within the [sqlite4_step] or [sqlite4_close]
** call that concludes the blocking connections transaction.
**
** ^(If sqlite4_unlock_notify() is called in a multi-threaded application,
** there is a chance that the blocking connection will have already
** concluded its transaction by the time sqlite4_unlock_notify() is invoked.
** If this happens, then the specified callback is invoked immediately,
** from within the call to sqlite4_unlock_notify().)^
**
** ^If the blocked connection is attempting to obtain a write-lock on a
** shared-cache table, and more than one other connection currently holds
** a read-lock on the same table, then SQLite arbitrarily selects one of 
** the other connections to use as the blocking connection.
**
** ^(There may be at most one unlock-notify callback registered by a 
** blocked connection. If sqlite4_unlock_notify() is called when the
** blocked connection already has a registered unlock-notify callback,
** then the new callback replaces the old.)^ ^If sqlite4_unlock_notify() is
** called with a NULL pointer as its second argument, then any existing
** unlock-notify callback is canceled. ^The blocked connections 
** unlock-notify callback may also be canceled by closing the blocked
** connection using [sqlite4_close()].
**
** The unlock-notify callback is not reentrant. If an application invokes
** any sqlite4_xxx API functions from within an unlock-notify callback, a
** crash or deadlock may be the result.
**
** ^Unless deadlock is detected (see below), sqlite4_unlock_notify() always
** returns SQLITE4_OK.
**
** <b>Callback Invocation Details</b>
**
** When an unlock-notify callback is registered, the application provides a 
** single void* pointer that is passed to the callback when it is invoked.
** However, the signature of the callback function allows SQLite to pass
** it an array of void* context pointers. The first argument passed to
** an unlock-notify callback is a pointer to an array of void* pointers,
** and the second is the number of entries in the array.
**
** When a blocking connections transaction is concluded, there may be
** more than one blocked connection that has registered for an unlock-notify
** callback. ^If two or more such blocked connections have specified the
** same callback function, then instead of invoking the callback function
** multiple times, it is invoked once with the set of void* context pointers
** specified by the blocked connections bundled together into an array.
** This gives the application an opportunity to prioritize any actions 
** related to the set of unblocked database connections.
**
** <b>Deadlock Detection</b>
**
** Assuming that after registering for an unlock-notify callback a 
** database waits for the callback to be issued before taking any further
** action (a reasonable assumption), then using this API may cause the
** application to deadlock. For example, if connection X is waiting for
** connection Y's transaction to be concluded, and similarly connection
** Y is waiting on connection X's transaction, then neither connection
** will proceed and the system may remain deadlocked indefinitely.
**
** To avoid this scenario, the sqlite4_unlock_notify() performs deadlock
** detection. ^If a given call to sqlite4_unlock_notify() would put the
** system in a deadlocked state, then SQLITE4_LOCKED is returned and no
** unlock-notify callback is registered. The system is said to be in
** a deadlocked state if connection A has registered for an unlock-notify
** callback on the conclusion of connection B's transaction, and connection
** B has itself registered for an unlock-notify callback when connection
** A's transaction is concluded. ^Indirect deadlock is also detected, so
** the system is also considered to be deadlocked if connection B has
** registered for an unlock-notify callback on the conclusion of connection
** C's transaction, where connection C is waiting on connection A. ^Any
** number of levels of indirection are allowed.
**
** <b>The "DROP TABLE" Exception</b>
**
** When a call to [sqlite4_step()] returns SQLITE4_LOCKED, it is almost 
** always appropriate to call sqlite4_unlock_notify(). There is however,
** one exception. When executing a "DROP TABLE" or "DROP INDEX" statement,
** SQLite checks if there are any currently executing SELECT statements
** that belong to the same connection. If there are, SQLITE4_LOCKED is
** returned. In this case there is no "blocking connection", so invoking
** sqlite4_unlock_notify() results in the unlock-notify callback being
** invoked immediately. If the application then re-attempts the "DROP TABLE"
** or "DROP INDEX" query, an infinite loop might be the result.
**
** One way around this problem is to check the extended error code returned
** by an sqlite4_step() call. ^(If there is a blocking connection, then the
** extended error code is set to SQLITE4_LOCKED_SHAREDCACHE. Otherwise, in
** the special "DROP TABLE/INDEX" case, the extended error code is just 
** SQLITE4_LOCKED.)^
*/
SQLITE4_API int sqlite4_unlock_notify(
  sqlite4 *pBlocked,                          /* Waiting connection */
  void (*xNotify)(void **apArg, int nArg),    /* Callback function to invoke */
  void *pNotifyArg                            /* Argument to pass to xNotify */
);


/*
** CAPIREF: String Comparison
**
** ^The [sqlite4_strnicmp()] API allows applications and extensions to
** compare the contents of two buffers containing UTF-8 strings in a
** case-independent fashion, using the same definition of case independence 
** that SQLite uses internally when comparing identifiers.
*/
SQLITE4_API int sqlite4_strnicmp(const char *, const char *, int);

/*
** CAPIREF: Error Logging Interface
**
** ^The [sqlite4_log()] interface writes a message into the error log
** established by the [SQLITE4_CONFIG_LOG] option to [sqlite4_env_config()].
** ^If logging is enabled, the zFormat string and subsequent arguments are
** used with [sqlite4_snprintf()] to generate the final output string.
**
** The sqlite4_log() interface is intended for use by extensions such as
** virtual tables, collating functions, and SQL functions.  While there is
** nothing to prevent an application from calling sqlite4_log(), doing so
** is considered bad form.
**
** The zFormat string must not be NULL.
**
** To avoid deadlocks and other threading problems, the sqlite4_log() routine
** will not use dynamically allocated memory.  The log message is stored in
** a fixed-length buffer on the stack.  If the log message is longer than
** a few hundred characters, it will be truncated to the length of the
** buffer.
*/
SQLITE4_API void sqlite4_log(sqlite4_env*, int iErrCode, const char *zFormat, ...);

/*
** CAPIREF: Virtual Table Interface Configuration
**
** This function may be called by either the [xConnect] or [xCreate] method
** of a [virtual table] implementation to configure
** various facets of the virtual table interface.
**
** If this interface is invoked outside the context of an xConnect or
** xCreate virtual table method then the behavior is undefined.
**
** At present, there is only one option that may be configured using
** this function. (See [SQLITE4_VTAB_CONSTRAINT_SUPPORT].)  Further options
** may be added in the future.
*/
SQLITE4_API int sqlite4_vtab_config(sqlite4*, int op, ...);

/*
** CAPIREF: Virtual Table Configuration Options
**
** These macros define the various options to the
** [sqlite4_vtab_config()] interface that [virtual table] implementations
** can use to customize and optimize their behavior.
**
** <dl>
** <dt>SQLITE4_VTAB_CONSTRAINT_SUPPORT
** <dd>Calls of the form
** [sqlite4_vtab_config](db,SQLITE4_VTAB_CONSTRAINT_SUPPORT,X) are supported,
** where X is an integer.  If X is zero, then the [virtual table] whose
** [xCreate] or [xConnect] method invoked [sqlite4_vtab_config()] does not
** support constraints.  In this configuration (which is the default) if
** a call to the [xUpdate] method returns [SQLITE4_CONSTRAINT], then the entire
** statement is rolled back as if [ON CONFLICT | OR ABORT] had been
** specified as part of the users SQL statement, regardless of the actual
** ON CONFLICT mode specified.
**
** If X is non-zero, then the virtual table implementation guarantees
** that if [xUpdate] returns [SQLITE4_CONSTRAINT], it will do so before
** any modifications to internal or persistent data structures have been made.
** If the [ON CONFLICT] mode is ABORT, FAIL, IGNORE or ROLLBACK, SQLite 
** is able to roll back a statement or database transaction, and abandon
** or continue processing the current SQL statement as appropriate. 
** If the ON CONFLICT mode is REPLACE and the [xUpdate] method returns
** [SQLITE4_CONSTRAINT], SQLite handles this as if the ON CONFLICT mode
** had been ABORT.
**
** Virtual table implementations that are required to handle OR REPLACE
** must do so within the [xUpdate] method. If a call to the 
** [sqlite4_vtab_on_conflict()] function indicates that the current ON 
** CONFLICT policy is REPLACE, the virtual table implementation should 
** silently replace the appropriate rows within the xUpdate callback and
** return SQLITE4_OK. Or, if this is not possible, it may return
** SQLITE4_CONSTRAINT, in which case SQLite falls back to OR ABORT 
** constraint handling.
** </dl>
*/
#define SQLITE4_VTAB_CONSTRAINT_SUPPORT 1

/*
** CAPIREF: Determine The Virtual Table Conflict Policy
**
** This function may only be called from within a call to the [xUpdate] method
** of a [virtual table] implementation for an INSERT or UPDATE operation. ^The
** value returned is one of [SQLITE4_ROLLBACK], [SQLITE4_IGNORE], [SQLITE4_FAIL],
** [SQLITE4_ABORT], or [SQLITE4_REPLACE], according to the [ON CONFLICT] mode
** of the SQL statement that triggered the call to the [xUpdate] method of the
** [virtual table].
*/
SQLITE4_API int sqlite4_vtab_on_conflict(sqlite4 *);

/*
** CAPIREF: Conflict resolution modes
**
** These constants are returned by [sqlite4_vtab_on_conflict()] to
** inform a [virtual table] implementation what the [ON CONFLICT] mode
** is for the SQL statement being evaluated.
**
** Note that the [SQLITE4_IGNORE] constant is also used as a potential
** return value from the [sqlite4_set_authorizer()] callback and that
** [SQLITE4_ABORT] is also a [result code].
*/
#define SQLITE4_ROLLBACK 1
/* #define SQLITE4_IGNORE 2 // Also used by sqlite4_authorizer() callback */
#define SQLITE4_FAIL     3
/* #define SQLITE4_ABORT 4  // Also an error code */
#define SQLITE4_REPLACE  5


/*
** CAPI4REF:  Length of a key-value storage key or data field
**
** The length of the key or data for a key-value storage entry is
** stored in a variable of this type.
*/
typedef int sqlite4_kvsize;

/*
** CAPI4REF: Key-Value Storage Engine Object
**
** An instance of a subclass of the following object defines a
** connection to a storage engine.
*/
typedef struct sqlite4_kvstore sqlite4_kvstore;
struct sqlite4_kvstore {
  const struct sqlite4_kv_methods *pStoreVfunc;  /* Methods */
  sqlite4_env *pEnv;                      /* Runtime environment for kvstore */
  int iTransLevel;                        /* Current transaction level */
  unsigned kvId;                          /* Unique ID used for tracing */
  unsigned fTrace;                        /* True to enable tracing */
  char zKVName[12];                       /* Used for debugging */
  /* Subclasses will typically append additional fields */
};

/*
** CAPI4REF:  Key-Value Storage Engine Cursor Object
**
** An instance of a subclass of the following object defines a cursor
** used to scan through a key-value storage engine.
*/
typedef struct sqlite4_kvcursor sqlite4_kvcursor;
struct sqlite4_kvcursor {
  sqlite4_kvstore *pStore;                /* The owner of this cursor */
  const struct sqlite4_kv_methods *pStoreVfunc;  /* Methods */
  sqlite4_env *pEnv;                      /* Runtime environment  */
  int iTransLevel;                        /* Current transaction level */
  unsigned curId;                         /* Unique ID for tracing */
  unsigned fTrace;                        /* True to enable tracing */
  /* Subclasses will typically add additional fields */
};

/*
** CAPI4REF: Key-value storage engine virtual method table
**
** A Key-Value storage engine is defined by an instance of the following
** object.
*/
struct sqlite4_kv_methods {
  int iVersion;
  int szSelf;
  int (*xReplace)(
         sqlite4_kvstore*,
         const unsigned char *pKey, sqlite4_kvsize nKey,
         const unsigned char *pData, sqlite4_kvsize nData);
  int (*xOpenCursor)(sqlite4_kvstore*, sqlite4_kvcursor**);
  int (*xSeek)(sqlite4_kvcursor*,
               const unsigned char *pKey, sqlite4_kvsize nKey, int dir);
  int (*xNext)(sqlite4_kvcursor*);
  int (*xPrev)(sqlite4_kvcursor*);
  int (*xDelete)(sqlite4_kvcursor*);
  int (*xKey)(sqlite4_kvcursor*,
              const unsigned char **ppKey, sqlite4_kvsize *pnKey);
  int (*xData)(sqlite4_kvcursor*, sqlite4_kvsize ofst, sqlite4_kvsize n,
               const unsigned char **ppData, sqlite4_kvsize *pnData);
  int (*xReset)(sqlite4_kvcursor*);
  int (*xCloseCursor)(sqlite4_kvcursor*);
  int (*xBegin)(sqlite4_kvstore*, int);
  int (*xCommitPhaseOne)(sqlite4_kvstore*, int);
  int (*xCommitPhaseTwo)(sqlite4_kvstore*, int);
  int (*xRollback)(sqlite4_kvstore*, int);
  int (*xRevert)(sqlite4_kvstore*, int);
  int (*xClose)(sqlite4_kvstore*);
  int (*xControl)(sqlite4_kvstore*, int, void*);
};
typedef struct sqlite4_kv_methods sqlite4_kv_methods;

/*
** CAPI4REF: Key-value storage engine open flags
**
** Allowed values to the flags parameter of an sqlite4_kvstore object
** factory.
**
** The flags parameter to the sqlite4_kvstore factory (the fourth parameter)
** is an OR-ed combination of these values and the
** [SQLITE4_OPEN_READONLY | SQLITE4_OPEN_xxxxx] flags that appear as 
** arguments to [sqlite4_open()].
*/
#define SQLITE4_KVOPEN_TEMPORARY       0x00010000  /* A temporary database */
#define SQLITE4_KVOPEN_NO_TRANSACTIONS 0x00020000  /* No transactions needed */


/*
** CAPI4REF: Representation Of Numbers
**
** Every number in SQLite is represented in memory by an instance of
** the following object.
*/
typedef struct sqlite4_num sqlite4_num;
struct sqlite4_num {
  unsigned char sign;     /* Sign of the overall value */
  unsigned char approx;   /* True if the value is approximate */
  unsigned short e;       /* The exponent. */
  sqlite4_uint64 m;       /* The significant */
};

/*
** CAPI4REF: Operations On SQLite Number Objects
*/
SQLITE4_API sqlite4_num sqlite4_num_add(sqlite4_num, sqlite4_num);
SQLITE4_API sqlite4_num sqlite4_num_sub(sqlite4_num, sqlite4_num);
SQLITE4_API sqlite4_num sqlite4_num_mul(sqlite4_num, sqlite4_num);
SQLITE4_API sqlite4_num sqlite4_num_div(sqlite4_num, sqlite4_num);
SQLITE4_API int sqlite4_num_isinf(sqlite4_num);
SQLITE4_API int sqlite4_num_isnan(sqlite4_num);
SQLITE4_API sqlite4_num sqlite4_num_round(sqlite4_num, int iDigit);
SQLITE4_API int sqlite4_num_compare(sqlite4_num, sqlite4_num);
SQLITE4_API sqlite4_num sqlite4_num_from_text(const char*, int n, unsigned flags);
SQLITE4_API sqlite4_num sqlite4_num_from_int64(sqlite4_int64);
SQLITE4_API sqlite4_num sqlite4_num_from_double(double);
SQLITE4_API int sqlite4_num_to_int32(sqlite4_num, int*);
SQLITE4_API int sqlite4_num_to_int64(sqlite4_num, sqlite4_int64*);
SQLITE4_API double sqlite4_num_to_double(sqlite4_num);
SQLITE4_API int sqlite4_num_to_text(sqlite4_num, char*);

/*
** CAPI4REF: Flags For Text-To-Numeric Conversion
*/
#define SQLITE4_PREFIX_ONLY         0x10
#define SQLITE4_IGNORE_WHITESPACE   0x20

/*
** Undo the hack that converts floating point types to integer for
** builds on processors without floating point support.
*/
#ifdef SQLITE4_OMIT_FLOATING_POINT
# undef double
#endif

#ifdef __cplusplus
}  /* End of the 'extern "C"' block */
#endif
#endif

/*
** 2010 August 30
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
*/

#ifndef _SQLITE3RTREE_H_
#define _SQLITE3RTREE_H_


#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite4_rtree_geometry sqlite4_rtree_geometry;

/*
** Register a geometry callback named zGeom that can be used as part of an
** R-Tree geometry query as follows:
**
**   SELECT ... FROM <rtree> WHERE <rtree col> MATCH $zGeom(... params ...)
*/
SQLITE4_API int sqlite4_rtree_geometry_callback(
  sqlite4 *db,
  const char *zGeom,
  int (*xGeom)(sqlite4_rtree_geometry *, int nCoord, double *aCoord, int *pRes),
  void *pContext
);


/*
** A pointer to a structure of the following type is passed as the first
** argument to callbacks registered using rtree_geometry_callback().
*/
struct sqlite4_rtree_geometry {
  void *pContext;                 /* Copy of pContext passed to s_r_g_c() */
  int nParam;                     /* Size of array aParam[] */
  double *aParam;                 /* Parameters passed to SQL geom function */
  void *pUser;                    /* Callback implementation user data */
  void (*xDelUser)(void *);       /* Called by SQLite to clean up pUser */
};


#ifdef __cplusplus
}  /* end of the 'extern "C"' block */
#endif

#endif  /* ifndef _SQLITE3RTREE_H_ */

