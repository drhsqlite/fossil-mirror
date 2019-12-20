/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** This module codes the main() procedure that runs first when the
** program is invoked.
*/
#include "VERSION.h"
#include "config.h"
#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>
#  define isatty(h) _isatty(h)
#  define GETPID (int)GetCurrentProcessId
#endif
#include "main.h"
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h> /* atexit() */
#if !defined(_WIN32)
#  include <errno.h> /* errno global */
#  include <unistd.h>
#  include <signal.h>
#  define GETPID getpid
#endif
#ifdef FOSSIL_ENABLE_SSL
#  include "openssl/crypto.h"
#endif
#if defined(FOSSIL_ENABLE_MINIZ)
#  define MINIZ_HEADER_FILE_ONLY
#  include "miniz.c"
#else
#  include <zlib.h>
#endif
#if INTERFACE
#ifdef FOSSIL_ENABLE_TCL
#  include "tcl.h"
#endif
#ifdef FOSSIL_ENABLE_JSON
#  include "cson_amalgamation.h" /* JSON API. */
#  include "json_detail.h"
#endif
#ifdef HAVE_BACKTRACE
# include <execinfo.h>
#endif

/*
** Default length of a timeout for serving an HTTP request.  Changable
** using the "--timeout N" command-line option or via "timeout: N" in the
** CGI script.
*/
#ifndef FOSSIL_DEFAULT_TIMEOUT
# define FOSSIL_DEFAULT_TIMEOUT 600  /* 10 minutes */
#endif

/*
** Maximum number of auxiliary parameters on reports
*/
#define MX_AUX  5

/*
** Holds flags for fossil user permissions.
*/
struct FossilUserPerms {
  char Setup;            /* s: use Setup screens on web interface */
  char Admin;            /* a: administrative permission */
  char Delete;           /* d: delete wiki or tickets */
  char Password;         /* p: change password */
  char Query;            /* q: create new reports */
  char Write;            /* i: xfer inbound. check-in */
  char Read;             /* o: xfer outbound. check-out */
  char Hyperlink;        /* h: enable the display of hyperlinks */
  char Clone;            /* g: clone */
  char RdWiki;           /* j: view wiki via web */
  char NewWiki;          /* f: create new wiki via web */
  char ApndWiki;         /* m: append to wiki via web */
  char WrWiki;           /* k: edit wiki via web */
  char ModWiki;          /* l: approve and publish wiki content (Moderator) */
  char RdTkt;            /* r: view tickets via web */
  char NewTkt;           /* n: create new tickets */
  char ApndTkt;          /* c: append to tickets via the web */
  char WrTkt;            /* w: make changes to tickets via web */
  char ModTkt;           /* q: approve and publish ticket changes (Moderator) */
  char Attach;           /* b: add attachments */
  char TktFmt;           /* t: create new ticket report formats */
  char RdAddr;           /* e: read email addresses or other private data */
  char Zip;              /* z: download zipped artifact via /zip URL */
  char Private;          /* x: can send and receive private content */
  char WrUnver;          /* y: can push unversioned content */
  char RdForum;          /* 2: Read forum posts */
  char WrForum;          /* 3: Create new forum posts */
  char WrTForum;         /* 4: Post to forums not subject to moderation */
  char ModForum;         /* 5: Moderate (approve or reject) forum posts */
  char AdminForum;       /* 6: Grant capability 4 to other users */
  char EmailAlert;       /* 7: Sign up for email notifications */
  char Announce;         /* A: Send announcements */
  char Debug;            /* D: show extra Fossil debugging features */
  /* These last two are included to block infinite recursion */
  char XReader;          /* u: Inherit all privileges of "reader" */
  char XDeveloper;       /* v: Inherit all privileges of "developer" */
};

#ifdef FOSSIL_ENABLE_TCL
/*
** All Tcl related context information is in this structure.  This structure
** definition has been copied from and should be kept in sync with the one in
** "th_tcl.c".
*/
struct TclContext {
  int argc;              /* Number of original (expanded) arguments. */
  char **argv;           /* Full copy of the original (expanded) arguments. */
  void *hLibrary;        /* The Tcl library module handle. */
  void *xFindExecutable; /* See tcl_FindExecutableProc in th_tcl.c. */
  void *xCreateInterp;   /* See tcl_CreateInterpProc in th_tcl.c. */
  void *xDeleteInterp;   /* See tcl_DeleteInterpProc in th_tcl.c. */
  void *xFinalize;       /* See tcl_FinalizeProc in th_tcl.c. */
  Tcl_Interp *interp;    /* The on-demand created Tcl interpreter. */
  int useObjProc;        /* Non-zero if an objProc can be called directly. */
  int useTip285;         /* Non-zero if TIP #285 is available. */
  char *setup;           /* The optional Tcl setup script. */
  void *xPreEval;        /* Optional, called before Tcl_Eval*(). */
  void *pPreContext;     /* Optional, provided to xPreEval(). */
  void *xPostEval;       /* Optional, called after Tcl_Eval*(). */
  void *pPostContext;    /* Optional, provided to xPostEval(). */
};
#endif

struct Global {
  int argc; char **argv;  /* Command-line arguments to the program */
  char *nameOfExe;        /* Full path of executable. */
  const char *zErrlog;    /* Log errors to this file, if not NULL */
  int isConst;            /* True if the output is unchanging & cacheable */
  const char *zVfsName;   /* The VFS to use for database connections */
  sqlite3 *db;            /* The connection to the databases */
  sqlite3 *dbConfig;      /* Separate connection for global_config table */
  char *zAuxSchema;       /* Main repository aux-schema */
  int dbIgnoreErrors;     /* Ignore database errors if true */
  char *zConfigDbName;    /* Path of the config database. NULL if not open */
  sqlite3_int64 now;      /* Seconds since 1970 */
  int repositoryOpen;     /* True if the main repository database is open */
  unsigned iRepoDataVers;  /* Initial data version for repository database */
  char *zRepositoryOption; /* Most recent cached repository option value */
  char *zRepositoryName;  /* Name of the repository database file */
  char *zLocalDbName;     /* Name of the local database file */
  char *zOpenRevision;    /* Check-in version to use during database open */
  const char *zCmdName;   /* Name of the Fossil command currently running */
  int localOpen;          /* True if the local database is open */
  char *zLocalRoot;       /* The directory holding the  local database */
  int minPrefix;          /* Number of digits needed for a distinct UUID */
  int eHashPolicy;        /* Current hash policy.  One of HPOLICY_* */
  int fSqlTrace;          /* True if --sqltrace flag is present */
  int fSqlStats;          /* True if --sqltrace or --sqlstats are present */
  int fSqlPrint;          /* True if --sqlprint flag is present */
  int fCgiTrace;          /* True if --cgitrace is enabled */
  int fQuiet;             /* True if -quiet flag is present */
  int fJail;              /* True if running with a chroot jail */
  int fHttpTrace;         /* Trace outbound HTTP requests */
  int fAnyTrace;          /* Any kind of tracing */
  char *zHttpAuth;        /* HTTP Authorization user:pass information */
  int fSystemTrace;       /* Trace calls to fossil_system(), --systemtrace */
  int fSshTrace;          /* Trace the SSH setup traffic */
  int fSshClient;         /* HTTP client flags for SSH client */
  int fNoHttpCompress;    /* Do not compress HTTP traffic (for debugging) */
  char *zSshCmd;          /* SSH command string */
  int fNoSync;            /* Do not do an autosync ever.  --nosync */
  int fIPv4;              /* Use only IPv4, not IPv6. --ipv4 */
  char *zPath;            /* Name of webpage being served */
  char *zExtra;           /* Extra path information past the webpage name */
  char *zBaseURL;         /* Full text of the URL being served */
  char *zHttpsURL;        /* zBaseURL translated to https: */
  char *zTop;             /* Parent directory of zPath */
  const char *zExtRoot;   /* Document root for the /ext sub-website */
  const char *zContentType;  /* The content type of the input HTTP request */
  int iErrPriority;       /* Priority of current error message */
  char *zErrMsg;          /* Text of an error message */
  int sslNotAvailable;    /* SSL is not available.  Do not redirect to https: */
  Blob cgiIn;             /* Input to an xfer www method */
  int cgiOutput;          /* 0: command-line 1: CGI. 2: after CGI */
  int xferPanic;          /* Write error messages in XFER protocol */
  int fullHttpReply;      /* True for full HTTP reply.  False for CGI reply */
  Th_Interp *interp;      /* The TH1 interpreter */
  char *th1Setup;         /* The TH1 post-creation setup script, if any */
  int th1Flags;           /* The TH1 integration state flags */
  FILE *httpIn;           /* Accept HTTP input from here */
  FILE *httpOut;          /* Send HTTP output here */
  int xlinkClusterOnly;   /* Set when cloning.  Only process clusters */
  int fTimeFormat;        /* 1 for UTC.  2 for localtime.  0 not yet selected */
  int *aCommitFile;       /* Array of files to be committed */
  int markPrivate;        /* All new artifacts are private if true */
  char *ckinLockFail;     /* Check-in lock failure received from server */
  int clockSkewSeen;      /* True if clocks on client and server out of sync */
  int wikiFlags;          /* Wiki conversion flags applied to %W */
  char isHTTP;            /* True if server/CGI modes, else assume CLI. */
  char javascriptHyperlink; /* If true, set href= using script, not HTML */
  Blob httpHeader;        /* Complete text of the HTTP request header */
  UrlData url;            /* Information about current URL */
  const char *zLogin;     /* Login name.  NULL or "" if not logged in. */
  const char *zSSLIdentity;  /* Value of --ssl-identity option, filename of
                             ** SSL client identity */
  int useLocalauth;       /* No login required if from 127.0.0.1 */
  int noPswd;             /* Logged in without password (on 127.0.0.1) */
  int userUid;            /* Integer user id */
  int isHuman;            /* True if access by a human, not a spider or bot */
  int comFmtFlags;        /* Zero or more "COMMENT_PRINT_*" bit flags, should be
                          ** accessed through get_comment_format(). */

  /* Information used to populate the RCVFROM table */
  int rcvid;              /* The rcvid.  0 if not yet defined. */
  char *zIpAddr;          /* The remote IP address */
  char *zNonce;           /* The nonce used for login */

  /* permissions available to current user */
  struct FossilUserPerms perm;

  /* permissions available to current user or to "anonymous".
  ** This is the logical union of perm permissions above with
  ** the value that perm would take if g.zLogin were "anonymous". */
  struct FossilUserPerms anon;

#ifdef FOSSIL_ENABLE_TCL
  /* all Tcl related context necessary for integration */
  struct TclContext tcl;
#endif

  /* For defense against Cross-site Request Forgery attacks */
  char zCsrfToken[12];    /* Value of the anti-CSRF token */
  int okCsrf;             /* Anti-CSRF token is present and valid */

  int parseCnt[10];       /* Counts of artifacts parsed */
  FILE *fDebug;           /* Write debug information here, if the file exists */
#ifdef FOSSIL_ENABLE_TH1_HOOKS
  int fNoThHook;          /* Disable all TH1 command/webpage hooks */
#endif
  int thTrace;            /* True to enable TH1 debugging output */
  Blob thLog;             /* Text of the TH1 debugging output */

  int isHome;             /* True if rendering the "home" page */

  /* Storage for the aux() and/or option() SQL function arguments */
  int nAux;                    /* Number of distinct aux() or option() values */
  const char *azAuxName[MX_AUX]; /* Name of each aux() or option() value */
  char *azAuxParam[MX_AUX];      /* Param of each aux() or option() value */
  const char *azAuxVal[MX_AUX];  /* Value of each aux() or option() value */
  const char **azAuxOpt[MX_AUX]; /* Options of each option() value */
  int anAuxCols[MX_AUX];         /* Number of columns for option() values */
  int allowSymlinks;             /* Cached "allow-symlinks" option */
  int mainTimerId;               /* Set to fossil_timer_start() */
  int nPendingRequest;           /* # of HTTP requests in "fossil server" */
  int nRequest;                  /* Total # of HTTP request */
#ifdef FOSSIL_ENABLE_JSON
  struct FossilJsonBits {
    int isJsonMode;            /* True if running in JSON mode, else
                                  false. This changes how errors are
                                  reported. In JSON mode we try to
                                  always output JSON-form error
                                  responses and always exit() with
                                  code 0 to avoid an HTTP 500 error.
                               */
    int resultCode;            /* used for passing back specific codes
                               ** from /json callbacks. */
    int errorDetailParanoia;   /* 0=full error codes, 1=%10, 2=%100, 3=%1000 */
    cson_output_opt outOpt;    /* formatting options for JSON mode. */
    cson_value *authToken;     /* authentication token */
    const char *jsonp;         /* Name of JSONP function wrapper. */
    unsigned char dispatchDepth /* Tells JSON command dispatching
                                   which argument we are currently
                                   working on. For this purpose, arg#0
                                   is the "json" path/CLI arg.
                                */;
    struct {                   /* "garbage collector" */
      cson_value *v;
      cson_array *a;
    } gc;
    struct {                   /* JSON POST data. */
      cson_value *v;
      cson_array *a;
      int offset;              /* Tells us which PATH_INFO/CLI args
                                  part holds the "json" command, so
                                  that we can account for sub-repos
                                  and path prefixes.  This is handled
                                  differently for CLI and CGI modes.
                               */
      const char *commandStr   /*"command" request param.*/;
    } cmd;
    struct {                   /* JSON POST data. */
      cson_value *v;
      cson_object *o;
    } post;
    struct {                   /* GET/COOKIE params in JSON mode. */
      cson_value *v;
      cson_object *o;
    } param;
    struct {
      cson_value *v;
      cson_object *o;
    } reqPayload;              /* request payload object (if any) */
    cson_array *warnings;      /* response warnings */
    int timerId;               /* fetched from fossil_timer_start() */
  } json;
#endif /* FOSSIL_ENABLE_JSON */
};

/*
** Macro for debugging:
*/
#define CGIDEBUG(X)  if( g.fDebug ) cgi_debug X

#endif

Global g;

/*
** Infrastructure for fossil_atexit_free_this().
*/
static struct {
  void* list[20];  /* Pointers to pass to fossil_free() during
                    ** atexit(). */
  int n;            /* Number of items currently in this->list. */
} fossilFreeAtExit = { {0}, 0 };

/*
** If zMem is not NULL and there is space in fossil's atexit cleanup
** queue, zMem is added to that queue so that it will be passed to
** fossil_free() during the atexit() phase of app shutdown. If the
** queue is full or zMem is NULL, this function has no side effects.
**
** This is intended to be called by routines which allocate heap
** memory for static-scope values which otherwise won't be freed, and
** the static queue size is relatively small.
*/
void fossil_atexit_free_this(void * zMem){
  if(zMem!=0
     && fossilFreeAtExit.n < (sizeof(fossilFreeAtExit.list)
                              / sizeof(fossilFreeAtExit.list[0]))){
    fossilFreeAtExit.list[fossilFreeAtExit.n++] = zMem;
  }
}

/*
** atexit() handler which frees up "some" of the resources
** used by fossil.
*/
static void fossil_atexit(void) {
  static int once = 0;
  if( once++ ) return; /* Ensure that this routine only runs once */
#if USE_SEE
  /*
  ** Zero, unlock, and free the saved database encryption key now.
  */
  db_unsave_encryption_key();
#endif
#if defined(_WIN32) || defined(__BIONIC__)
  /*
  ** Free the secure getpass() buffer now.
  */
  freepass();
#endif
#if defined(_WIN32) && !defined(_WIN64) && defined(FOSSIL_ENABLE_TCL) && \
    defined(USE_TCL_STUBS)
  /*
  ** If Tcl is compiled on Windows using the latest MinGW, Fossil can crash
  ** when exiting while a stubs-enabled Tcl is still loaded.  This is due to
  ** a bug in MinGW, see:
  **
  **     http://comments.gmane.org/gmane.comp.gnu.mingw.user/41724
  **
  ** The workaround is to manually unload the loaded Tcl library prior to
  ** exiting the process.  This issue does not impact 64-bit Windows.
  */
  unloadTcl(g.interp, &g.tcl);
#endif
#ifdef FOSSIL_ENABLE_JSON
  cson_value_free(g.json.gc.v);
  memset(&g.json, 0, sizeof(g.json));
#endif
  if(g.db){
    db_close(0);
  }
  fossil_free(g.ckinLockFail);
  fossil_free(g.zAuxSchema);
  fossil_free(g.zBaseURL);
  fossil_free(g.zConfigDbName);
  fossil_free(g.zErrMsg);
  fossil_free(g.zExtra);
  fossil_free(g.zHttpAuth);
  fossil_free(g.zHttpsURL);
  fossil_free(g.zIpAddr);
  fossil_free(g.zLocalDbName);
  fossil_free(g.zLocalRoot);
  fossil_free(g.zNonce);
  fossil_free(g.zOpenRevision);
  fossil_free(g.zPath);
  fossil_free(g.zRepositoryName);
  fossil_free(g.zRepositoryOption);
  fossil_free(g.zSshCmd);
  fossil_free(g.zTop);
  manifest_clear_cache();
  content_clear_cache(1);
  rebuild_clear_cache();
  if(fossilFreeAtExit.n>0){
    int i;
    for(i = 0; i < fossilFreeAtExit.n; ++i){
      fossil_free(fossilFreeAtExit.list[i]);
      fossilFreeAtExit.list[i] = 0;
    }
    fossilFreeAtExit.n = 0;
  }
  /*
  ** FIXME: The next two lines cannot always be enabled; however, they
  **        are very useful for tracking down TH1 memory leaks.
  */
  if( fossil_getenv("TH1_DELETE_INTERP")!=0 ){
    if( g.interp ){
      Th_DeleteInterp(g.interp); g.interp = 0;
    }
    assert( Th_GetOutstandingMalloc()==0 );
  }
}

/*
** Convert all arguments from mbcs (or unicode) to UTF-8. Then
** search g.argv for arguments "--args FILENAME". If found, then
** (1) remove the two arguments from g.argv
** (2) Read the file FILENAME
** (3) Use the contents of FILE to replace the two removed arguments:
**     (a) Ignore blank lines in the file
**     (b) Each non-empty line of the file is an argument, except
**     (c) If the line begins with "-" and contains a space, it is broken
**         into two arguments at the space.
*/
void expand_args_option(int argc, void *argv){
  Blob file = empty_blob;   /* Content of the file */
  Blob line = empty_blob;   /* One line of the file */
  unsigned int nLine;       /* Number of lines in the file*/
  unsigned int i, j, k;     /* Loop counters */
  int n;                    /* Number of bytes in one line */
  unsigned int nArg;        /* Number of new arguments */
  char *z;                  /* General use string pointer */
  char **newArgv;           /* New expanded g.argv under construction */
  const char *zFileName;    /* input file name */
  FILE *inFile;             /* input FILE */
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
#endif

  g.argc = argc;
  g.argv = argv;
  sqlite3_initialize();
#if defined(_WIN32) && defined(BROKEN_MINGW_CMDLINE)
  for(i=0; i<g.argc; i++) g.argv[i] = fossil_mbcs_to_utf8(g.argv[i]);
#else
  for(i=0; i<g.argc; i++) g.argv[i] = fossil_path_to_utf8(g.argv[i]);
#endif
#if defined(_WIN32)
  GetModuleFileNameW(NULL, buf, MAX_PATH);
  g.nameOfExe = fossil_path_to_utf8(buf);
#else
  g.nameOfExe = g.argv[0];
#endif
  for(i=1; i<g.argc-1; i++){
    z = g.argv[i];
    if( z[0]!='-' ) continue;
    z++;
    if( z[0]=='-' ) z++;
    if( z[0]==0 ) return;   /* Stop searching at "--" */
    if( fossil_strcmp(z, "args")==0 ) break;
  }
  if( i>=g.argc-1 ) return;

  zFileName = g.argv[i+1];
  if( strcmp(zFileName,"-")==0 ){
    inFile = stdin;
  }else if( !file_isfile(zFileName, ExtFILE) ){
    fossil_fatal("Not an ordinary file: \"%s\"", zFileName);
  }else{
    inFile = fossil_fopen(zFileName,"rb");
    if( inFile==0 ){
      fossil_fatal("Cannot open -args file [%s]", zFileName);
    }
  }
  blob_read_from_channel(&file, inFile, -1);
  if(stdin != inFile){
    fclose(inFile);
  }
  inFile = NULL;
  blob_to_utf8_no_bom(&file, 1);
  z = blob_str(&file);
  for(k=0, nLine=1; z[k]; k++) if( z[k]=='\n' ) nLine++;
  if( nLine>100000000 ) fossil_fatal("too many command-line arguments");
  nArg = g.argc + nLine*2;
  newArgv = fossil_malloc( sizeof(char*)*nArg );
  for(j=0; j<i; j++) newArgv[j] = g.argv[j];

  blob_rewind(&file);
  while( (n = blob_line(&file, &line))>0 ){
    if( n<1 ){
      /* Reminder: corner-case: a line with 1 byte and no newline. */
      continue;
    }
    z = blob_buffer(&line);
    if('\n'==z[n-1]){
      z[n-1] = 0;
    }

    if((n>1) && ('\r'==z[n-2])){
      if(n==2) continue /*empty line*/;
      z[n-2] = 0;
    }
    if(!z[0]) continue;
    if( j>=nArg ){
      fossil_fatal("malformed command-line arguments");
    }
    newArgv[j++] = z;
    if( z[0]=='-' ){
      for(k=1; z[k] && !fossil_isspace(z[k]); k++){}
      if( z[k] ){
        z[k] = 0;
        k++;
        if( z[k] ) newArgv[j++] = &z[k];
      }
    }
  }
  i += 2;
  while( i<g.argc ) newArgv[j++] = g.argv[i++];
  newArgv[j] = 0;
  g.argc = j;
  g.argv = newArgv;
}

#ifdef FOSSIL_ENABLE_TCL
/*
** Make a deep copy of the provided argument array and return it.
*/
static char **copy_args(int argc, char **argv){
  char **zNewArgv;
  int i;
  zNewArgv = fossil_malloc( sizeof(char*)*(argc+1) );
  memset(zNewArgv, 0, sizeof(char*)*(argc+1));
  for(i=0; i<argc; i++){
    zNewArgv[i] = fossil_strdup(argv[i]);
  }
  return zNewArgv;
}
#endif

/*
** Returns a name for a SQLite return code.
*/
static const char *fossil_sqlite_return_code_name(int rc){
  static char zCode[30];
  switch( rc & 0xff ){
    case SQLITE_OK:         return "SQLITE_OK";
    case SQLITE_ERROR:      return "SQLITE_ERROR";
    case SQLITE_INTERNAL:   return "SQLITE_INTERNAL";
    case SQLITE_PERM:       return "SQLITE_PERM";
    case SQLITE_ABORT:      return "SQLITE_ABORT";
    case SQLITE_BUSY:       return "SQLITE_BUSY";
    case SQLITE_LOCKED:     return "SQLITE_LOCKED";
    case SQLITE_NOMEM:      return "SQLITE_NOMEM";
    case SQLITE_READONLY:   return "SQLITE_READONLY";
    case SQLITE_INTERRUPT:  return "SQLITE_INTERRUPT";
    case SQLITE_IOERR:      return "SQLITE_IOERR";
    case SQLITE_CORRUPT:    return "SQLITE_CORRUPT";
    case SQLITE_NOTFOUND:   return "SQLITE_NOTFOUND";
    case SQLITE_FULL:       return "SQLITE_FULL";
    case SQLITE_CANTOPEN:   return "SQLITE_CANTOPEN";
    case SQLITE_PROTOCOL:   return "SQLITE_PROTOCOL";
    case SQLITE_EMPTY:      return "SQLITE_EMPTY";
    case SQLITE_SCHEMA:     return "SQLITE_SCHEMA";
    case SQLITE_TOOBIG:     return "SQLITE_TOOBIG";
    case SQLITE_CONSTRAINT: return "SQLITE_CONSTRAINT";
    case SQLITE_MISMATCH:   return "SQLITE_MISMATCH";
    case SQLITE_MISUSE:     return "SQLITE_MISUSE";
    case SQLITE_NOLFS:      return "SQLITE_NOLFS";
    case SQLITE_AUTH:       return "SQLITE_AUTH";
    case SQLITE_FORMAT:     return "SQLITE_FORMAT";
    case SQLITE_RANGE:      return "SQLITE_RANGE";
    case SQLITE_NOTADB:     return "SQLITE_NOTADB";
    case SQLITE_NOTICE:     return "SQLITE_NOTICE";
    case SQLITE_WARNING:    return "SQLITE_WARNING";
    case SQLITE_ROW:        return "SQLITE_ROW";
    case SQLITE_DONE:       return "SQLITE_DONE";
    default: {
      sqlite3_snprintf(sizeof(zCode), zCode, "SQLite return code %d", rc);
    }
  }
  return zCode;
}

/* Error logs from SQLite */
static void fossil_sqlite_log(void *notUsed, int iCode, const char *zErrmsg){
  sqlite3_stmt *p;
  Blob msg;
#ifdef __APPLE__
  /* Disable the file alias warning on apple products because Time Machine
  ** creates lots of aliases and the warnings alarm people. */
  if( iCode==SQLITE_WARNING ) return;
#endif
#ifndef FOSSIL_DEBUG
  /* Disable the automatic index warning except in FOSSIL_DEBUG builds. */
  if( iCode==SQLITE_WARNING_AUTOINDEX ) return;
#endif
  if( iCode==SQLITE_SCHEMA ) return;
  if( g.dbIgnoreErrors ) return;
#ifdef SQLITE_READONLY_DIRECTORY
  if( iCode==SQLITE_READONLY_DIRECTORY ){
    zErrmsg = "database is in a read-only directory";
  }
#endif
  blob_init(&msg, 0, 0);
  blob_appendf(&msg, "%s(%d): %s",
     fossil_sqlite_return_code_name(iCode), iCode, zErrmsg);
  if( g.db ){
    for(p=sqlite3_next_stmt(g.db, 0); p; p=sqlite3_next_stmt(g.db,p)){
      const char *zSql;
      if( !sqlite3_stmt_busy(p) ) continue;
      zSql = sqlite3_sql(p);
      if( zSql==0 ) continue;
      blob_appendf(&msg, "\nSQL: %s", zSql);
    }
  }
  fossil_warning("%s", blob_str(&msg));
  blob_reset(&msg);
}

/*
** This function attempts to find command line options known to contain
** bitwise flags and initializes the associated global variables.  After
** this function executes, all global variables (i.e. in the "g" struct)
** containing option-settable bitwise flag fields must be initialized.
*/
static void fossil_init_flags_from_options(void){
  const char *zValue = find_option("comfmtflags", 0, 1);
  if( zValue==0 ){
    zValue = find_option("comment-format", 0, 1);
  }
  if( zValue ){
    g.comFmtFlags = atoi(zValue);
  }else{
    g.comFmtFlags = COMMENT_PRINT_UNSET;   /* Command-line option not found. */
  }
}

/*
** Check to see if the Fossil binary contains an appended repository
** file using the appendvfs extension.  If so, change command-line arguments
** to cause Fossil to launch with "fossil ui" on that repo.
*/
static int fossilExeHasAppendedRepo(void){
  extern int deduceDatabaseType(const char*,int);
  if( 2==deduceDatabaseType(g.nameOfExe,0) ){
    static char *azAltArgv[] = { 0, "ui", 0, 0 };
    azAltArgv[0] = g.nameOfExe;
    azAltArgv[2] = g.nameOfExe;
    g.argv = azAltArgv;
    g.argc = 3;
    return 1;
  }else{
    return 0;
  }
}

/*
** This procedure runs first.
*/
#if defined(FOSSIL_FUZZ)
  /* Do not include a main() procedure when building for fuzz testing.
  ** libFuzzer will supply main(). */
#elif defined(_WIN32) && !defined(BROKEN_MINGW_CMDLINE)
  int _dowildcard = -1; /* This turns on command-line globbing in MinGW-w64 */
  int wmain(int argc, wchar_t **argv){ return fossil_main(argc, argv); }
#elif defined(_WIN32)
  int _CRT_glob = 0x0001; /* See MinGW bug #2062 */
  int main(int argc, char **argv){ return fossil_main(argc, argv); }
#else
  int main(int argc, char **argv){ return fossil_main(argc, argv); }
#endif

/* All the work of main() is done by a separate procedure "fossil_main()".
** We have to break this out, because fossil_main() is sometimes called
** separately (by the "shell" command) but we do not want atwait() handlers
** being called by separate invocations of fossil_main().
*/
int fossil_main(int argc, char **argv){
  const char *zCmdName = "unknown";
  const CmdOrPage *pCmd = 0;
  int rc;

#if !defined(_WIN32_WCE)
  if( fossil_getenv("FOSSIL_BREAK") ){
    if( isatty(0) && isatty(2) ){
      fprintf(stderr,
          "attach debugger to process %d and press any key to continue.\n",
          GETPID());
      fgetc(stdin);
    }else{
#if defined(_WIN32) || defined(WIN32)
      DebugBreak();
#elif defined(SIGTRAP)
      raise(SIGTRAP);
#endif
    }
  }
#endif

  fossil_limit_memory(1);
  if( sqlite3_libversion_number()<3014000 ){
    fossil_panic("Unsuitable SQLite version %s, must be at least 3.14.0",
                 sqlite3_libversion());
  }
  sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
  sqlite3_config(SQLITE_CONFIG_LOG, fossil_sqlite_log, 0);
  memset(&g, 0, sizeof(g));
  g.now = time(0);
  g.httpHeader = empty_blob;
#ifdef FOSSIL_ENABLE_JSON
#if defined(NDEBUG)
  g.json.errorDetailParanoia = 2 /* FIXME: make configurable
                                    One problem we have here is that this
                                    code is needed before the db is opened,
                                    so we can't sql for it.*/;
#else
  g.json.errorDetailParanoia = 0;
#endif
  g.json.outOpt = cson_output_opt_empty;
  g.json.outOpt.addNewline = 1;
  g.json.outOpt.indentation = 1 /* in CGI/server mode this can be configured */;
#endif /* FOSSIL_ENABLE_JSON */
  expand_args_option(argc, argv);
#ifdef FOSSIL_ENABLE_TCL
  memset(&g.tcl, 0, sizeof(TclContext));
  g.tcl.argc = g.argc;
  g.tcl.argv = copy_args(g.argc, g.argv); /* save full arguments */
#endif
  g.mainTimerId = fossil_timer_start();
  capture_case_sensitive_option();
  g.zVfsName = find_option("vfs",0,1);
  if( g.zVfsName==0 ){
    g.zVfsName = fossil_getenv("FOSSIL_VFS");
  }
  if( g.zVfsName ){
    sqlite3_vfs *pVfs = sqlite3_vfs_find(g.zVfsName);
    if( pVfs ){
      sqlite3_vfs_register(pVfs, 1);
    }else{
      fossil_fatal("no such VFS: \"%s\"", g.zVfsName);
    }
  }
  if( fossil_getenv("GATEWAY_INTERFACE")!=0 && !find_option("nocgi", 0, 0)){
    zCmdName = "cgi";
    g.isHTTP = 1;
  }else if( g.argc<2 && !fossilExeHasAppendedRepo() ){
    fossil_print(
       "Usage: %s COMMAND ...\n"
       "   or: %s help           -- for a list of common commands\n"
       "   or: %s help COMMAND   -- for help with the named command\n",
       g.argv[0], g.argv[0], g.argv[0]);
    fossil_print(
      "\nCommands and filenames may be passed on to fossil from a file\n"
      "by using:\n"
      "\n    %s --args FILENAME ...\n",
      g.argv[0]
    );
    fossil_print(
      "\nEach line of the file is assumed to be a filename unless it starts\n"
      "with '-' and contains a space, in which case it is assumed to be\n"
      "another flag and is treated as such. --args FILENAME may be used\n"
      "in conjunction with any other flags.\n");
    fossil_exit(1);
  }else{
    const char *zChdir = find_option("chdir",0,1);
    g.isHTTP = 0;
    g.rcvid = 0;
    g.fQuiet = find_option("quiet", 0, 0)!=0;
    g.fSqlTrace = find_option("sqltrace", 0, 0)!=0;
    g.fSqlStats = find_option("sqlstats", 0, 0)!=0;
    g.fSystemTrace = find_option("systemtrace", 0, 0)!=0;
    g.fSshTrace = find_option("sshtrace", 0, 0)!=0;
    g.fCgiTrace = find_option("cgitrace", 0, 0)!=0;
    g.fSshClient = 0;
    g.zSshCmd = 0;
    if( g.fSqlTrace ) g.fSqlStats = 1;
    g.fHttpTrace = find_option("httptrace", 0, 0)!=0;
#ifdef FOSSIL_ENABLE_TH1_HOOKS
    g.fNoThHook = find_option("no-th-hook", 0, 0)!=0;
#endif
    g.fAnyTrace = g.fSqlTrace|g.fSystemTrace|g.fSshTrace|
                  g.fHttpTrace|g.fCgiTrace;
    g.zHttpAuth = 0;
    g.zLogin = find_option("user", "U", 1);
    g.zSSLIdentity = find_option("ssl-identity", 0, 1);
    g.zErrlog = find_option("errorlog", 0, 1);
    fossil_init_flags_from_options();
    if( find_option("utc",0,0) ) g.fTimeFormat = 1;
    if( find_option("localtime",0,0) ) g.fTimeFormat = 2;
    if( zChdir && file_chdir(zChdir, 0) ){
      fossil_fatal("unable to change directories to %s", zChdir);
    }
    if( find_option("help",0,0)!=0 ){
      /* If --help is found anywhere on the command line, translate the command
       * to "fossil help cmdname" where "cmdname" is the first argument that
       * does not begin with a "-" character.  If all arguments start with "-",
       * translate to "fossil help argv[1] argv[2]...". */
      int i, nNewArgc;
      char **zNewArgv = fossil_malloc( sizeof(char*)*(g.argc+2) );
      zNewArgv[0] = g.argv[0];
      zNewArgv[1] = "help";
      for(i=1; i<g.argc; i++){
        if( g.argv[i][0]!='-' ){
          nNewArgc = 3;
          zNewArgv[2] = g.argv[i];
          zNewArgv[3] = 0;
          break;
        }
      }
      if( i==g.argc ){
        for(i=1; i<g.argc; i++) zNewArgv[i+1] = g.argv[i];
        nNewArgc = g.argc+1;
        zNewArgv[i+1] = 0;
      }
      g.argc = nNewArgc;
      g.argv = zNewArgv;
    }
    zCmdName = g.argv[1];
  }
#ifndef _WIN32
  /* There is a bug in stunnel4 in which it sometimes starts up client
  ** processes without first opening file descriptor 2 (standard error).
  ** If this happens, and a subsequent open() of a database returns file
  ** descriptor 2, and then an assert() fires and writes on fd 2, that
  ** can corrupt the data file.  To avoid this problem, make sure open()
  ** will never return file descriptor 2 or less. */
  if( !is_valid_fd(2) ){
    int nTry = 0;
    int fd = 0;
    int x = 0;
    do{
      fd = open("/dev/null",O_WRONLY);
      if( fd>=2 ) break;
      if( fd<0 ) x = errno;
    }while( nTry++ < 2 );
    if( fd<2 ){
      g.cgiOutput = 1;
      g.httpOut = stdout;
      g.fullHttpReply = !g.isHTTP;
      fossil_panic("file descriptor 2 is not open. (fd=%d, errno=%d)",
                   fd, x);
    }
  }
#endif
  g.zCmdName = zCmdName;
  rc = dispatch_name_search(zCmdName, CMDFLAG_COMMAND|CMDFLAG_PREFIX, &pCmd);
  if( rc==1 ){
#ifdef FOSSIL_ENABLE_TH1_HOOKS
    if( !g.isHTTP && !g.fNoThHook ){
      rc = Th_CommandHook(zCmdName, 0);
    }else{
      rc = TH_OK;
    }
    if( rc==TH_OK || rc==TH_RETURN || rc==TH_CONTINUE ){
      if( rc==TH_OK || rc==TH_RETURN ){
#endif
        fossil_fatal("%s: unknown command: %s\n"
                     "%s: use \"help\" for more information",
                     g.argv[0], zCmdName, g.argv[0]);
#ifdef FOSSIL_ENABLE_TH1_HOOKS
      }
      if( !g.isHTTP && !g.fNoThHook && (rc==TH_OK || rc==TH_CONTINUE) ){
        Th_CommandNotify(zCmdName, 0);
      }
    }
    fossil_exit(0);
#endif
  }else if( rc==2 ){
    Blob couldbe;
    blob_init(&couldbe,0,0);
    dispatch_matching_names(zCmdName, &couldbe);
    fossil_print("%s: ambiguous command prefix: %s\n"
                 "%s: could be any of:%s\n"
                 "%s: use \"help\" for more information\n",
                 g.argv[0], zCmdName, g.argv[0], blob_str(&couldbe), g.argv[0]);
    fossil_exit(1);
  }
  atexit( fossil_atexit );
#ifdef FOSSIL_ENABLE_TH1_HOOKS
  /*
  ** The TH1 return codes from the hook will be handled as follows:
  **
  ** TH_OK: The xFunc() and the TH1 notification will both be executed.
  **
  ** TH_ERROR: The xFunc() will be skipped, the TH1 notification will be
  **           skipped.  If the xFunc() is being hooked, the error message
  **           will be emitted.
  **
  ** TH_BREAK: The xFunc() and the TH1 notification will both be skipped.
  **
  ** TH_RETURN: The xFunc() will be executed, the TH1 notification will be
  **            skipped.
  **
  ** TH_CONTINUE: The xFunc() will be skipped, the TH1 notification will be
  **              executed.
  */
  if( !g.isHTTP && !g.fNoThHook ){
    rc = Th_CommandHook(pCmd->zName, pCmd->eCmdFlags);
  }else{
    rc = TH_OK;
  }
  if( rc==TH_OK || rc==TH_RETURN || rc==TH_CONTINUE ){
    if( rc==TH_OK || rc==TH_RETURN ){
#endif
      pCmd->xFunc();
#ifdef FOSSIL_ENABLE_TH1_HOOKS
    }
    if( !g.isHTTP && !g.fNoThHook && (rc==TH_OK || rc==TH_CONTINUE) ){
      Th_CommandNotify(pCmd->zName, pCmd->eCmdFlags);
    }
  }
#endif
  fossil_exit(0);
  /*NOT_REACHED*/
  return 0;
}

/*
** Print a usage comment and quit
*/
void usage(const char *zFormat){
  fossil_fatal("Usage: %s %s %s", g.argv[0], g.argv[1], zFormat);
}

/*
** Remove n elements from g.argv beginning with the i-th element.
*/
static void remove_from_argv(int i, int n){
  int j;
  for(j=i+n; j<g.argc; i++, j++){
    g.argv[i] = g.argv[j];
  }
  g.argc = i;
}


/*
** Look for a command-line option.  If present, remove it from the
** argument list and return a pointer to either the flag's name (if
** hasArg==0), sans leading - or --, or its value (if hasArg==1).
** Return NULL if the flag is not found.
**
** zLong is the "long" form of the flag and zShort is the
** short/abbreviated form (typically a single letter, but it may be
** longer). zLong must not be NULL, but zShort may be.
**
** hasArg==0 means the option is a flag.  It is either present or not.
** hasArg==1 means the option has an argument, in which case a pointer
** to the argument's value is returned. For zLong, a flag value (if
** hasValue==1) may either be in the form (--flag=value) or (--flag
** value). For zShort, only the latter form is accepted.
**
** If a standalone argument of "--" is encountered in the argument
** list while searching for the given flag(s), this routine stops
** searching and NULL is returned.
*/
const char *find_option(const char *zLong, const char *zShort, int hasArg){
  int i;
  int nLong;
  const char *zReturn = 0;
  assert( hasArg==0 || hasArg==1 );
  nLong = strlen(zLong);
  for(i=1; i<g.argc; i++){
    char *z;
    if( i+hasArg >= g.argc ) break;
    z = g.argv[i];
    if( z[0]!='-' ) continue;
    z++;
    if( z[0]=='-' ){
      if( z[1]==0 ){
        /* Stop processing at "--" without consuming it.
           verify_all_options() will consume this flag. */
        break;
      }
      z++;
    }
    if( strncmp(z,zLong,nLong)==0 ){
      if( hasArg && z[nLong]=='=' ){
        zReturn = &z[nLong+1];
        remove_from_argv(i, 1);
        break;
      }else if( z[nLong]==0 ){
        zReturn = g.argv[i+hasArg];
        remove_from_argv(i, 1+hasArg);
        break;
      }
    }else if( fossil_strcmp(z,zShort)==0 ){
      zReturn = g.argv[i+hasArg];
      remove_from_argv(i, 1+hasArg);
      break;
    }
  }
  return zReturn;
}

/* Return true if zOption exists in the command-line arguments,
** but do not remove it from the list or otherwise process it.
*/
int has_option(const char *zOption){
  int i;
  int n = (int)strlen(zOption);
  for(i=1; i<g.argc; i++){
    char *z = g.argv[i];
    if( z[0]!='-' ) continue;
    z++;
    if( z[0]=='-' ){
      if( z[1]==0 ){
        /* Stop processing at "--" */
        break;
      }
      z++;
    }
    if( strncmp(z,zOption,n)==0 && (z[n]==0 || z[n]=='=') ) return 1;
  }
  return 0;
}

/*
** Look for multiple occurrences of a command-line option with the
** corresponding argument.
**
** Return a malloc allocated array of pointers to the arguments.
**
** pnUsedArgs is used to store the number of matched arguments.
**
** Caller is responsible for freeing allocated memory by passing the
** head of the array (not each entry) to fossil_free(). (The
** individual entries have the same lifetime as values returned from
** find_option().)
*/
const char **find_repeatable_option(
  const char *zLong,
  const char *zShort,
  int *pnUsedArgs
){
  const char *zOption;
  const char **pzArgs = 0;
  int nAllocArgs = 0;
  int nUsedArgs = 0;

  while( (zOption = find_option(zLong, zShort, 1))!=0 ){
    if( pzArgs==0 && nAllocArgs==0 ){
      nAllocArgs = 1;
      pzArgs = fossil_malloc( nAllocArgs*sizeof(pzArgs[0]) );
    }else if( nAllocArgs<=nUsedArgs ){
      nAllocArgs = nAllocArgs*2;
      pzArgs = fossil_realloc( (void *)pzArgs, nAllocArgs*sizeof(pzArgs[0]) );
    }
    pzArgs[nUsedArgs++] = zOption;
  }
  *pnUsedArgs = nUsedArgs;
  return pzArgs;
}

/*
** Look for a repository command-line option.  If present, [re-]cache it in
** the global state and return the new pointer, freeing any previous value.
** If absent and there is no cached value, return NULL.
*/
const char *find_repository_option(){
  const char *zRepository = find_option("repository", "R", 1);
  if( zRepository ){
    if( g.zRepositoryOption ) fossil_free(g.zRepositoryOption);
    g.zRepositoryOption = mprintf("%s", zRepository);
  }
  return g.zRepositoryOption;
}

/*
** Verify that there are no unprocessed command-line options.  If
** Any remaining command-line argument begins with "-" print
** an error message and quit.
**
** Exception: if "--" is encountered, it is consumed from the argument
** list and this function immediately returns. The effect is to treat
** all arguments after "--" as non-flags (conventionally used to
** enable passing-in of filenames which start with a dash).
**
** This function must normally only be called one time per app
** invokation. The exception is commands which process their
** arguments, call this to confirm that there are no extraneous flags,
** then modify the arguments list for forwarding to another
** (sub)command (which itself will call this to confirm its own
** arguments).
*/
void verify_all_options(void){
  int i;
  for(i=1; i<g.argc; i++){
    const char * arg = g.argv[i];
    if( arg[0]=='-' ){
      if( arg[1]=='-' && arg[2]==0 ){
        /* Remove "--" from the list and treat all following
        ** arguments as non-flags. */
        remove_from_argv(i, 1);
        break;
      }else if( arg[1]!=0 ){
        fossil_fatal(
          "unrecognized command-line option or missing argument: %s",
          arg);
      }
    }
  }
}

/*
** This function returns a human readable version string.
*/
const char *get_version(){
  static const char version[] = RELEASE_VERSION " " MANIFEST_VERSION " "
                                MANIFEST_DATE " UTC";
  return version;
}

/*
** This function populates a blob with version information.  It is used by
** the "version" command and "test-version" web page.  It assumes the blob
** passed to it is uninitialized; otherwise, it will leak memory.
*/
static void get_version_blob(
  Blob *pOut,                 /* Write the manifest here */
  int bVerbose                /* Non-zero for full information. */
){
#if defined(FOSSIL_ENABLE_TCL)
  int rc;
  const char *zRc;
#endif
  Stmt q;
  size_t pageSize = 0;
  blob_zero(pOut);
  blob_appendf(pOut, "This is fossil version %s\n", get_version());
  if( !bVerbose ) return;
  blob_appendf(pOut, "Compiled on %s %s using %s (%d-bit)\n",
               __DATE__, __TIME__, COMPILER_NAME, sizeof(void*)*8);
  blob_appendf(pOut, "Schema version %s\n", AUX_SCHEMA_MAX);
  fossil_get_page_size(&pageSize);
  blob_appendf(pOut, "Detected memory page size is %lu bytes\n",
               (unsigned long)pageSize);
#if defined(FOSSIL_ENABLE_MINIZ)
  blob_appendf(pOut, "miniz %s, loaded %s\n", MZ_VERSION, mz_version());
#else
  blob_appendf(pOut, "zlib %s, loaded %s\n", ZLIB_VERSION, zlibVersion());
#endif
#if FOSSIL_HARDENED_SHA1
  blob_appendf(pOut, "hardened-SHA1 by Marc Stevens and Dan Shumow\n");
#endif
#if defined(FOSSIL_ENABLE_SSL)
  blob_appendf(pOut, "SSL (%s)\n", SSLeay_version(SSLEAY_VERSION));
#endif
#if defined(FOSSIL_HAVE_FUSEFS)
  blob_appendf(pOut, "libfuse %s, loaded %s\n", fusefs_inc_version(),
               fusefs_lib_version());
#endif
#if defined(FOSSIL_DEBUG)
  blob_append(pOut, "FOSSIL_DEBUG\n", -1);
#endif
#if defined(FOSSIL_ENABLE_DELTA_CKSUM_TEST)
  blob_append(pOut, "FOSSIL_ENABLE_DELTA_CKSUM_TEST\n", -1);
#endif
#if defined(FOSSIL_ENABLE_LEGACY_MV_RM)
  blob_append(pOut, "FOSSIL_ENABLE_LEGACY_MV_RM\n", -1);
#endif
#if defined(FOSSIL_ENABLE_EXEC_REL_PATHS)
  blob_append(pOut, "FOSSIL_ENABLE_EXEC_REL_PATHS\n", -1);
#endif
#if defined(FOSSIL_ENABLE_TH1_DOCS)
  blob_append(pOut, "FOSSIL_ENABLE_TH1_DOCS\n", -1);
#endif
#if defined(FOSSIL_ENABLE_TH1_HOOKS)
  blob_append(pOut, "FOSSIL_ENABLE_TH1_HOOKS\n", -1);
#endif
#if defined(FOSSIL_ENABLE_TCL)
  Th_FossilInit(TH_INIT_DEFAULT | TH_INIT_FORCE_TCL);
  rc = Th_Eval(g.interp, 0, "tclInvoke info patchlevel", -1);
  zRc = Th_ReturnCodeName(rc, 0);
  blob_appendf(pOut, "TCL (Tcl %s, loaded %s: %s)\n",
    TCL_PATCH_LEVEL, zRc, Th_GetResult(g.interp, 0)
  );
#endif
#if defined(USE_TCL_STUBS)
  blob_append(pOut, "USE_TCL_STUBS\n", -1);
#endif
#if defined(FOSSIL_ENABLE_TCL_STUBS)
  blob_append(pOut, "FOSSIL_TCL_STUBS\n", -1);
#endif
#if defined(FOSSIL_ENABLE_TCL_PRIVATE_STUBS)
  blob_append(pOut, "FOSSIL_ENABLE_TCL_PRIVATE_STUBS\n", -1);
#endif
#if defined(FOSSIL_ENABLE_JSON)
  blob_appendf(pOut, "JSON (API %s)\n", FOSSIL_JSON_API_VERSION);
#endif
#if defined(BROKEN_MINGW_CMDLINE)
  blob_append(pOut, "MBCS_COMMAND_LINE\n", -1);
#else
  blob_append(pOut, "UNICODE_COMMAND_LINE\n", -1);
#endif
#if defined(FOSSIL_DYNAMIC_BUILD)
  blob_append(pOut, "FOSSIL_DYNAMIC_BUILD\n", -1);
#else
  blob_append(pOut, "FOSSIL_STATIC_BUILD\n", -1);
#endif
#if defined(HAVE_PLEDGE)
  blob_append(pOut, "HAVE_PLEDGE\n", -1);
#endif
#if defined(USE_MMAN_H)
  blob_append(pOut, "USE_MMAN_H\n", -1);
#endif
#if defined(USE_SEE)
  blob_append(pOut, "USE_SEE\n", -1);
#endif
#if defined(FOSSIL_ALLOW_OUT_OF_ORDER_DATES)
  blob_append(pOut, "FOSSIL_ALLOW_OUT_OF_ORDER_DATES\n");
#endif
  blob_appendf(pOut, "SQLite %s %.30s\n", sqlite3_libversion(),
               sqlite3_sourceid());
  if( g.db==0 ) sqlite3_open(":memory:", &g.db);
  db_prepare(&q,
     "pragma compile_options");
  while( db_step(&q)==SQLITE_ROW ){
    const char *text = db_column_text(&q, 0);
    if( strncmp(text, "COMPILER", 8) ){
      blob_appendf(pOut, "SQLITE_%s\n", text);
    }
  }
  db_finalize(&q);
}

/*
** This function returns the user-agent string for Fossil, for
** use in HTTP(S) requests.
*/
const char *get_user_agent(){
  static const char version[] = "Fossil/" RELEASE_VERSION " (" MANIFEST_DATE
                                " " MANIFEST_VERSION ")";
  return version;
}


/*
** COMMAND: version
**
** Usage: %fossil version ?-verbose|-v?
**
** Print the source code version number for the fossil executable.
** If the verbose option is specified, additional details will
** be output about what optional features this binary was compiled
** with
*/
void version_cmd(void){
  Blob versionInfo;
  int verboseFlag = find_option("verbose","v",0)!=0;

  /* We should be done with options.. */
  verify_all_options();
  get_version_blob(&versionInfo, verboseFlag);
  fossil_print("%s", blob_str(&versionInfo));
}


/*
** WEBPAGE: version
**
** Show the version information for Fossil.
**
** Query parameters:
**
**    verbose       Show details
*/
void test_version_page(void){
  Blob versionInfo;
  int verboseFlag;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  verboseFlag = PD("verbose", 0) != 0;
  style_header("Version Information");
  style_submenu_element("Stat", "stat");
  get_version_blob(&versionInfo, verboseFlag);
  @ <pre>
  @ %h(blob_str(&versionInfo))
  @ </pre>
  style_footer();
}


/*
** Set the g.zBaseURL value to the full URL for the toplevel of
** the fossil tree.  Set g.zTop to g.zBaseURL without the
** leading "http://" and the host and port.
**
** The g.zBaseURL is normally set based on HTTP_HOST and SCRIPT_NAME
** environment variables.  However, if zAltBase is not NULL then it
** is the argument to the --baseurl option command-line option and
** g.zBaseURL and g.zTop is set from that instead.
*/
void set_base_url(const char *zAltBase){
  int i;
  const char *zHost;
  const char *zMode;
  const char *zCur;

  if( g.zBaseURL!=0 ) return;
  if( zAltBase ){
    int i, n, c;
    g.zTop = g.zBaseURL = mprintf("%s", zAltBase);
    if( strncmp(g.zTop, "http://", 7)==0 ){
      /* it is HTTP, replace prefix with HTTPS. */
      g.zHttpsURL = mprintf("https://%s", &g.zTop[7]);
    }else if( strncmp(g.zTop, "https://", 8)==0 ){
      /* it is already HTTPS, use it. */
      g.zHttpsURL = mprintf("%s", g.zTop);
    }else{
      fossil_fatal("argument to --baseurl should be 'http://host/path'"
                   " or 'https://host/path'");
    }
    for(i=n=0; (c = g.zTop[i])!=0; i++){
      if( c=='/' ){
        n++;
        if( n==3 ){
          g.zTop += i;
          break;
        }
      }
    }
    if( g.zTop==g.zBaseURL ){
      fossil_fatal("argument to --baseurl should be 'http://host/path'"
                   " or 'https://host/path'");
    }
    if( g.zTop[1]==0 ) g.zTop++;
  }else{
    zHost = PD("HTTP_HOST","");
    zMode = PD("HTTPS","off");
    zCur = PD("SCRIPT_NAME","/");
    i = strlen(zCur);
    while( i>0 && zCur[i-1]=='/' ) i--;
    if( fossil_stricmp(zMode,"on")==0 ){
      g.zBaseURL = mprintf("https://%s%.*s", zHost, i, zCur);
      g.zTop = &g.zBaseURL[8+strlen(zHost)];
      g.zHttpsURL = g.zBaseURL;
    }else{
      g.zBaseURL = mprintf("http://%s%.*s", zHost, i, zCur);
      g.zTop = &g.zBaseURL[7+strlen(zHost)];
      g.zHttpsURL = mprintf("https://%s%.*s", zHost, i, zCur);
    }
  }
  if( db_is_writeable("repository") ){
    if( !db_exists("SELECT 1 FROM config WHERE name='baseurl:%q'", g.zBaseURL)){
      db_multi_exec("INSERT INTO config(name,value,mtime)"
                    "VALUES('baseurl:%q',1,now())", g.zBaseURL);
    }else{
      db_optional_sql("repository",
           "REPLACE INTO config(name,value,mtime)"
           "VALUES('baseurl:%q',1,now())", g.zBaseURL
      );
    }
  }
}

/*
** Send an HTTP redirect back to the designated Index Page.
*/
NORETURN void fossil_redirect_home(void){
  cgi_redirectf("%s%s", g.zTop, db_get("index-page", "/index"));
}

/*
** If running as root, chroot to the directory containing the
** repository zRepo and then drop root privileges.  Return the
** new repository name.
**
** zRepo might be a directory itself.  In that case chroot into
** the directory zRepo.
**
** Assume the user-id and group-id of the repository, or if zRepo
** is a directory, of that directory.
**
** The noJail flag means that the chroot jail is not entered.  But
** privileges are still lowered to that of the user-id and group-id
** of the repository file.
*/
char *enter_chroot_jail(char *zRepo, int noJail){
#if !defined(_WIN32)
  if( getuid()==0 ){
    int i;
    struct stat sStat;
    Blob dir;
    char *zDir;
    if( g.db!=0 ){
      db_close(1);
    }

    file_canonical_name(zRepo, &dir, 0);
    zDir = blob_str(&dir);
    if( !noJail ){
      if( file_isdir(zDir, ExtFILE)==1 ){
        if( file_chdir(zDir, 1) ){
          fossil_panic("unable to chroot into %s", zDir);
        }
        g.fJail = 1;
        zRepo = "/";
      }else{
        for(i=strlen(zDir)-1; i>0 && zDir[i]!='/'; i--){}
        if( zDir[i]!='/' ) fossil_panic("bad repository name: %s", zRepo);
        if( i>0 ){
          zDir[i] = 0;
          if( file_chdir(zDir, 1) ){
            fossil_panic("unable to chroot into %s", zDir);
          }
          zDir[i] = '/';
        }
        zRepo = &zDir[i];
      }
    }
    if( stat(zRepo, &sStat)!=0 ){
      fossil_fatal("cannot stat() repository: %s", zRepo);
    }
    i = setgid(sStat.st_gid);
    i = i || setuid(sStat.st_uid);
    if(i){
      fossil_fatal("setgid/uid() failed with errno %d", errno);
    }
    if( g.db==0 && file_isfile(zRepo, ExtFILE) ){
      db_open_repository(zRepo);
    }
  }
#endif
  return zRepo;
}

/*
** Called whenever a crash is encountered while processing a webpage.
*/
void sigsegv_handler(int x){
#if HAVE_BACKTRACE
  void *array[20];
  size_t size;
  char **strings;
  size_t i;
  Blob out;
  size = backtrace(array, sizeof(array)/sizeof(array[0]));
  strings = backtrace_symbols(array, size);
  blob_init(&out, 0, 0);
  blob_appendf(&out, "Segfault");
  for(i=0; i<size; i++){
    blob_appendf(&out, "\n(%d) %s", i, strings[i]);
  }
  fossil_panic("%s", blob_str(&out));
#else
  fossil_panic("Segfault");
#endif
  exit(1);
}

/*
** Called if a server gets a SIGPIPE.  This often happens when a client
** webbrowser opens a connection but never sends the HTTP request
*/
void sigpipe_handler(int x){
#ifndef _WIN32
  if( g.fAnyTrace ){
    fprintf(stderr,"/***** sigpipe received by subprocess %d ****\n", getpid());
  }
#endif
  db_panic_close();
  exit(1);
}

/*
** Return true if it is appropriate to redirect requests to HTTPS.
**
** Redirect to https is appropriate if all of the above are true:
**    (1) The redirect-to-https flag has a valud of iLevel or greater.
**    (2) The current connection is http, not https or ssh
**    (3) The sslNotAvailable flag is clear
*/
int fossil_wants_https(int iLevel){
  if( g.sslNotAvailable ) return 0;
  if( db_get_int("redirect-to-https",0)<iLevel ) return 0;
  if( P("HTTPS")!=0 ) return 0;
  return 1;
}

/*
** Redirect to the equivalent HTTPS request if the current connection is
** insecure and if the redirect-to-https flag greater than or equal to 
** iLevel.  iLevel is 1 for /login pages and 2 for every other page.
*/
int fossil_redirect_to_https_if_needed(int iLevel){
  if( fossil_wants_https(iLevel) ){
    const char *zQS = P("QUERY_STRING");
    char *zURL;
    if( zQS==0 || zQS[0]==0 ){
      zURL = mprintf("%s%T", g.zHttpsURL, P("PATH_INFO"));
    }else if( zQS[0]!=0 ){
      zURL = mprintf("%s%T?%s", g.zHttpsURL, P("PATH_INFO"), zQS);
    }
    cgi_redirect_with_status(zURL, 301, "Moved Permanently");
    return 1;
  }
  return 0;
}

/*
** Preconditions:
**
**  * Environment variables are set up according to the CGI standard.
**
** If the repository is known, it has already been opened.  If unknown,
** then g.zRepositoryName holds the directory that contains the repository
** and the actual repository is taken from the first element of PATH_INFO.
**
** Process the webpage specified by the PATH_INFO or REQUEST_URI
** environment variable.
**
** If the repository is not known, then a search is done through the
** file hierarchy rooted at g.zRepositoryName for a suitable repository
** with a name of $prefix.fossil, where $prefix is any prefix of PATH_INFO.
** Or, if an ordinary file named $prefix is found, and $prefix matches
** pFileGlob and $prefix does not match "*.fossil*" and the mimetype of
** $prefix can be determined from its suffix, then the file $prefix is
** returned as static text.
**
** If no suitable webpage is found, try to redirect to zNotFound.
*/
static void process_one_web_page(
  const char *zNotFound,      /* Redirect here on a 404 if not NULL */
  Glob *pFileGlob,            /* Deliver static files matching */
  int allowRepoList           /* Send repo list for "/" URL */
){
  const char *zPathInfo = PD("PATH_INFO", "");
  char *zPath = NULL;
  int i;
  const CmdOrPage *pCmd = 0;
  const char *zBase = g.zRepositoryName;

#if !defined(_WIN32)
  signal(SIGSEGV, sigsegv_handler);
#endif

  /* Handle universal query parameters */
  if( PB("utc") ){
    g.fTimeFormat = 1;
  }else if( PB("localtime") ){
    g.fTimeFormat = 2;
  }

  /* If the repository has not been opened already, then find the
  ** repository based on the first element of PATH_INFO and open it.
  */
  if( !g.repositoryOpen ){
    char *zRepo;               /* Candidate repository name */
    char *zToFree = 0;         /* Malloced memory that needs to be freed */
    const char *zCleanRepo;    /* zRepo with surplus leading "/" removed */
    const char *zOldScript = PD("SCRIPT_NAME", "");  /* Original SCRIPT_NAME */
    char *zNewScript;          /* Revised SCRIPT_NAME after processing */
    int j, k;                  /* Loop variables */
    i64 szFile;                /* File size of the candidate repository */

    i = zPathInfo[0]!=0;
    if( fossil_strcmp(g.zRepositoryName, "/")==0 ){
      zBase++;
#if defined(_WIN32) || defined(__CYGWIN__)
      if( sqlite3_strglob("/[a-zA-Z]:/*", zPathInfo)==0 ) i = 4;
#endif
    }
    while( 1 ){
      while( zPathInfo[i] && zPathInfo[i]!='/' ){ i++; }

      /* The candidate repository name is some prefix of the PATH_INFO
      ** with ".fossil" appended */
      zRepo = zToFree = mprintf("%s%.*s.fossil",zBase,i,zPathInfo);
      if( g.fHttpTrace ){
        @ <!-- Looking for repository named "%h(zRepo)" -->
        fprintf(stderr, "# looking for repository named \"%s\"\n", zRepo);
      }


      /* For safety -- to prevent an attacker from accessing arbitrary disk
      ** files by sending a maliciously crafted request URI to a public
      ** server -- make sure the repository basename contains no
      ** characters other than alphanumerics, "/", "_", "-", and ".", and
      ** that "-" never occurs immediately after a "/" and that "." is always
      ** surrounded by two alphanumerics.  Any character that does not
      ** satisfy these constraints is converted into "_".
      */
      szFile = 0;
      for(j=strlen(zBase)+1, k=0; zRepo[j] && k<i-1; j++, k++){
        char c = zRepo[j];
        if( fossil_isalnum(c) ) continue;
#if defined(_WIN32) || defined(__CYGWIN__)
        /* Allow names to begin with "/X:/" on windows */
        if( c==':' && j==2 && sqlite3_strglob("/[a-zA-Z]:/*", zRepo)==0 ){
          continue;
        }
#endif
        if( c=='/' ) continue;
        if( c=='_' ) continue;
        if( c=='-' && zRepo[j-1]!='/' ) continue;
        if( c=='.' && fossil_isalnum(zRepo[j-1]) && fossil_isalnum(zRepo[j+1])){
          continue;
        }
        /* If we reach this point, it means that the request URI contains
        ** an illegal character or character combination.  Provoke a
        ** "Not Found" error. */
        szFile = 1;
        if( g.fHttpTrace ){
          @ <!-- Unsafe pathname rejected: "%h(zRepo)" -->
          fprintf(stderr, "# unsafe pathname rejected: %s\n", zRepo);
        }
        break;
      }

      /* Check to see if a file name zRepo exists.  If a file named zRepo
      ** does not exist, szFile will become -1.  If the file does exist,
      ** then szFile will become zero (for an empty file) or positive.
      ** Special case:  Assume any file with a basename of ".fossil" does
      ** not exist.
      */
      zCleanRepo = file_cleanup_fullpath(zRepo);
      if( szFile==0 && sqlite3_strglob("*/.fossil",zRepo)!=0 ){
        szFile = file_size(zCleanRepo, ExtFILE);
        if( g.fHttpTrace ){
          char zBuf[24];
          sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", szFile);
          @ <!-- file_size(%h(zCleanRepo)) is %s(zBuf) -->
          fprintf(stderr, "# file_size(%s) = %s\n", zCleanRepo, zBuf);
        }
      }

      /* If no file named by zRepo exists, remove the added ".fossil" suffix
      ** and check to see if there is a file or directory with the same
      ** name as the raw PATH_INFO text.
      */
      if( szFile<0 && i>0 ){
        const char *zMimetype;
        assert( fossil_strcmp(&zRepo[j], ".fossil")==0 );
        zRepo[j] = 0;  /* Remove the ".fossil" suffix */

        /* The PATH_INFO prefix seen so far is a valid directory.
        ** Continue the loop with the next element of the PATH_INFO */
        if( zPathInfo[i]=='/' && file_isdir(zCleanRepo, ExtFILE)==1 ){
          fossil_free(zToFree);
          i++;
          continue;
        }

        /* If zRepo is the name of an ordinary file that matches the
        ** "--file GLOB" pattern, then the CGI reply is the text of
        ** of the file.
        **
        ** For safety, do not allow any file whose name contains ".fossil"
        ** to be returned this way, to prevent complete repositories from
        ** being delivered accidently.  This is not intended to be a
        ** general-purpose web server.  The "--file GLOB" mechanism is
        ** designed to allow the delivery of a few static images or HTML
        ** pages.
        */
        if( pFileGlob!=0
         && file_isfile(zCleanRepo, ExtFILE)
         && glob_match(pFileGlob, file_cleanup_fullpath(zRepo))
         && sqlite3_strglob("*.fossil*",zRepo)!=0
         && (zMimetype = mimetype_from_name(zRepo))!=0
         && strcmp(zMimetype, "application/x-fossil-artifact")!=0
        ){
          Blob content;
          blob_read_from_file(&content, file_cleanup_fullpath(zRepo), ExtFILE);
          cgi_set_content_type(zMimetype);
          cgi_set_content(&content);
          cgi_reply();
          return;
        }
        zRepo[j] = '.';
      }

      /* If we reach this point, it means that the search of the PATH_INFO
      ** string is finished.  Either zRepo contains the name of the
      ** repository to be used, or else no repository could be found an
      ** some kind of error response is required.
      */
      if( szFile<1024 ){
        set_base_url(0);
        if( (zPathInfo[0]==0 || strcmp(zPathInfo,"/")==0)
                  && allowRepoList
                  && repo_list_page() ){
          /* Will return a list of repositories */
        }else if( zNotFound ){
          cgi_redirect(zNotFound);
        }else{
#ifdef FOSSIL_ENABLE_JSON
          if(g.json.isJsonMode){
            json_err(FSL_JSON_E_RESOURCE_NOT_FOUND,NULL,1);
            return;
          }
#endif
          @ <html><head>
          @ <meta name="viewport" \
          @ content="width=device-width, initial-scale=1.0">
          @ </head><body>
          @ <h1>Not Found</h1>
          @ </body>
          cgi_set_status(404, "not found");
          cgi_reply();
        }
        return;
      }
      break;
    }

    /* Add the repository name (without the ".fossil" suffix) to the end
    ** of SCRIPT_NAME and g.zTop and g.zBaseURL and remove the repository
    ** name from the beginning of PATH_INFO.
    */
    zNewScript = mprintf("%s%.*s", zOldScript, i, zPathInfo);
    if( g.zTop ) g.zTop = mprintf("%s%.*s", g.zTop, i, zPathInfo);
    if( g.zBaseURL ) g.zBaseURL = mprintf("%s%.*s", g.zBaseURL, i, zPathInfo);
    cgi_replace_parameter("PATH_INFO", &zPathInfo[i+1]);
    zPathInfo += i;
    cgi_replace_parameter("SCRIPT_NAME", zNewScript);
    db_open_repository(file_cleanup_fullpath(zRepo));
    if( g.fHttpTrace ){
      @ <!-- repository: "%h(zRepo)" -->
      @ <!-- translated PATH_INFO: "%h(zPathInfo)" -->
      @ <!-- translated SCRIPT_NAME: "%h(zNewScript)" -->
      fprintf(stderr,
          "# repository: [%s]\n"
          "# translated PATH_INFO = [%s]\n"
          "# translated SCRIPT_NAME = [%s]\n",
          zRepo, zPathInfo, zNewScript);
      if( g.zTop ){
        @ <!-- translated g.zTop: "%h(g.zTop)" -->
        fprintf(stderr, "# translated g.zTop = [%s]\n", g.zTop);
      }
      if( g.zBaseURL ){
        @ <!-- translated g.zBaseURL: "%h(g.zBaseURL)" -->
        fprintf(stderr, "# translated g.zBaseURL = [%s]\n", g.zBaseURL);
      }
    }
  }

  /* At this point, the appropriate repository database file will have
  ** been opened.
  **
  ** Check to see if the the PATH_INFO begins with "draft[1-9]" and if
  ** so activate the special handling for draft skins
  */
  if( zPathInfo && strncmp(zPathInfo,"/draft",6)==0
   && zPathInfo[6]>='1' && zPathInfo[6]<='9'
   && (zPathInfo[7]=='/' || zPathInfo[7]==0)
  ){
    int iSkin = zPathInfo[6] - '0';
    char *zNewScript;
    skin_use_draft(iSkin);
    zNewScript = mprintf("%T/draft%d", P("SCRIPT_NAME"), iSkin);
    if( g.zTop ) g.zTop = mprintf("%s/draft%d", g.zTop, iSkin);
    if( g.zBaseURL ) g.zBaseURL = mprintf("%s/draft%d", g.zBaseURL, iSkin);
    zPathInfo += 7;
    cgi_replace_parameter("PATH_INFO", zPathInfo);
    cgi_replace_parameter("SCRIPT_NAME", zNewScript);
  }

  /* If the content type is application/x-fossil or 
  ** application/x-fossil-debug, then a sync/push/pull/clone is
  ** desired, so default the PATH_INFO to /xfer
  */
  if( g.zContentType &&
      strncmp(g.zContentType, "application/x-fossil", 20)==0 ){
    /* Special case:  If the content mimetype shows that it is "fossil sync"
    ** payload, then pretend that the PATH_INFO is /xfer so that we always
    ** invoke the sync page. */
    zPathInfo = "/xfer";
  }

  /* Use the first element of PATH_INFO as the page name
  ** and deliver the appropriate page back to the user.
  */
  set_base_url(0);
  if( fossil_redirect_to_https_if_needed(2) ) return;
  if( zPathInfo==0 || zPathInfo[0]==0
      || (zPathInfo[0]=='/' && zPathInfo[1]==0) ){
    /* Second special case: If the PATH_INFO is blank, issue a redirect to
    ** the home page identified by the "index-page" setting in the repository
    ** CONFIG table, to "/index" if there no "index-page" setting. */
#ifdef FOSSIL_ENABLE_JSON
    if(g.json.isJsonMode){
      json_err(FSL_JSON_E_RESOURCE_NOT_FOUND,NULL,1);
      fossil_exit(0);
    }
#endif
    fossil_redirect_home() /*does not return*/;
  }else{
    zPath = mprintf("%s", zPathInfo);
  }

  /* Make g.zPath point to the first element of the path.  Make
  ** g.zExtra point to everything past that point.
  */
  while(1){
    g.zPath = &zPath[1];
    for(i=1; zPath[i] && zPath[i]!='/'; i++){}
    if( zPath[i]=='/' ){
      zPath[i] = 0;
      g.zExtra = &zPath[i+1];
    }else{
      g.zExtra = 0;
    }
    break;
  }
#ifdef FOSSIL_ENABLE_JSON
  /*
  ** Workaround to allow us to customize some following behaviour for
  ** JSON mode.  The problem is, we don't always know if we're in JSON
  ** mode at this point (namely, for GET mode we don't know but POST
  ** we do), so we snoop g.zPath and cheat a bit.
  */
  if( !g.json.isJsonMode && g.zPath && (0==strncmp("json",g.zPath,4)) ){
    g.json.isJsonMode = 1;
  }
#endif
  if( g.zExtra ){
    /* CGI parameters get this treatment elsewhere, but places like getfile
    ** will use g.zExtra directly.
    ** Reminder: the login mechanism uses 'name' differently, and may
    ** eventually have a problem/collision with this.
    **
    ** Disabled by stephan when running in JSON mode because this
    ** particular parameter name is very common and i have had no end
    ** of grief with this handling. The JSON API never relies on the
    ** handling below, and by disabling it in JSON mode I can remove
    ** lots of special-case handling in several JSON handlers.
    */
#ifdef FOSSIL_ENABLE_JSON
    if(!g.json.isJsonMode){
#endif
      dehttpize(g.zExtra);
      cgi_set_parameter_nocopy("name", g.zExtra, 1);
#ifdef FOSSIL_ENABLE_JSON
    }
#endif
  }

  /* Locate the method specified by the path and execute the function
  ** that implements that method.
  */
  if( dispatch_name_search(g.zPath-1, CMDFLAG_WEBPAGE, &pCmd)
   && dispatch_alias(g.zPath-1, &pCmd)
  ){
#ifdef FOSSIL_ENABLE_JSON
    if(g.json.isJsonMode){
      json_err(FSL_JSON_E_RESOURCE_NOT_FOUND,NULL,0);
    }else
#endif
    {
#ifdef FOSSIL_ENABLE_TH1_HOOKS
      int rc;
      if( !g.fNoThHook ){
        rc = Th_WebpageHook(g.zPath, 0);
      }else{
        rc = TH_OK;
      }
      if( rc==TH_OK || rc==TH_RETURN || rc==TH_CONTINUE ){
        if( rc==TH_OK || rc==TH_RETURN ){
#endif
          cgi_set_status(404,"Not Found");
          @ <h1>Not Found</h1>
          @ <p>Page not found: %h(g.zPath)</p>
#ifdef FOSSIL_ENABLE_TH1_HOOKS
        }
        if( !g.fNoThHook && (rc==TH_OK || rc==TH_CONTINUE) ){
          Th_WebpageNotify(g.zPath, 0);
        }
      }
#endif
    }
  }else if( pCmd->xFunc!=page_xfer && db_schema_is_outofdate() ){
#ifdef FOSSIL_ENABLE_JSON
    if(g.json.isJsonMode){
      json_err(FSL_JSON_E_DB_NEEDS_REBUILD,NULL,0);
    }else
#endif
    {
      @ <h1>Server Configuration Error</h1>
      @ <p>The database schema on the server is out-of-date.  Please ask
      @ the administrator to run <b>fossil rebuild</b>.</p>
    }
  }else{
    if( (pCmd->eCmdFlags & CMDFLAG_RAWCONTENT)==0 ){
      cgi_decode_post_parameters();
    }
    if( g.fCgiTrace ){
      fossil_trace("######## Calling %s #########\n", pCmd->zName);
      cgi_print_all(1, 1);
    }
#ifdef FOSSIL_ENABLE_TH1_HOOKS
    {
      /*
      ** The TH1 return codes from the hook will be handled as follows:
      **
      ** TH_OK: The xFunc() and the TH1 notification will both be executed.
      **
      ** TH_ERROR: The xFunc() will be skipped, the TH1 notification will be
      **           skipped.  If the xFunc() is being hooked, the error message
      **           will be emitted.
      **
      ** TH_BREAK: The xFunc() and the TH1 notification will both be skipped.
      **
      ** TH_RETURN: The xFunc() will be executed, the TH1 notification will be
      **            skipped.
      **
      ** TH_CONTINUE: The xFunc() will be skipped, the TH1 notification will be
      **              executed.
      */
      int rc;
      if( !g.fNoThHook ){
        rc = Th_WebpageHook(pCmd->zName+1, pCmd->eCmdFlags);
      }else{
        rc = TH_OK;
      }
      if( rc==TH_OK || rc==TH_RETURN || rc==TH_CONTINUE ){
        if( rc==TH_OK || rc==TH_RETURN ){
#endif
          pCmd->xFunc();
#ifdef FOSSIL_ENABLE_TH1_HOOKS
        }
        if( !g.fNoThHook && (rc==TH_OK || rc==TH_CONTINUE) ){
          Th_WebpageNotify(pCmd->zName+1, pCmd->eCmdFlags);
        }
      }
    }
#endif
  }

  /* Return the result.
  */
  cgi_reply();
}

/* If the CGI program contains one or more lines of the form
**
**    redirect:  repository-filename  http://hostname/path/%s
**
** then control jumps here.  Search each repository for an artifact ID
** or ticket ID that matches the "name" CGI parameter and for the
** first match, redirect to the corresponding URL with the "name" CGI
** parameter inserted.  Paint an error page if no match is found.
**
** If there is a line of the form:
**
**    redirect: * URL
**
** Then a redirect is made to URL if no match is found.  Otherwise a
** very primitive error message is returned.
*/
static void redirect_web_page(int nRedirect, char **azRedirect){
  int i;                             /* Loop counter */
  const char *zNotFound = 0;         /* Not found URL */
  const char *zName = P("name");
  set_base_url(0);
  if( zName==0 ){
    zName = P("SCRIPT_NAME");
    if( zName && zName[0]=='/' ) zName++;
  }
  if( zName && validate16(zName, strlen(zName)) ){
    for(i=0; i<nRedirect; i++){
      if( fossil_strcmp(azRedirect[i*2],"*")==0 ){
        zNotFound = azRedirect[i*2+1];
        continue;
      }
      db_open_repository(azRedirect[i*2]);
      if( db_exists("SELECT 1 FROM blob WHERE uuid GLOB '%q*'", zName) ||
          db_exists("SELECT 1 FROM ticket WHERE tkt_uuid GLOB '%q*'", zName) ){
        cgi_redirectf(azRedirect[i*2+1] /*works-like:"%s"*/, zName);
        return;
      }
      db_close(1);
    }
  }
  if( zNotFound ){
    cgi_redirectf(zNotFound /*works-like:"%s"*/, zName);
  }else{
    @ <html>
    @ <head><title>No Such Object</title></head>
    @ <body>
    @ <p>No such object: <b>%h(zName)</b></p>
    @ </body>
    cgi_reply();
  }
}

/*
** COMMAND: cgi*
**
** Usage: %fossil ?cgi? FILE
**
** This command causes Fossil to generate reply to a CGI request.
**
** The FILE argument is the name of a control file that provides Fossil
** with important information such as where to find its repository.  In
** a typical CGI deployment, FILE is the name of the CGI script and will
** typically look something like this:
**
**      #!/usr/bin/fossil
**      repository: /home/somebody/project.db
**
** The command name, "cgi", may be omitted if the GATEWAY_INTERFACE
** environment variable is set to "CGI", which should always be the
** case for CGI scripts run by a webserver.  Fossil ignores any lines
** that begin with "#".
**
** The following control lines are recognized:
**
**    repository: PATH         Name of the Fossil repository
**
**    directory:  PATH         Name of a directory containing many Fossil
**                             repositories whose names all end with ".fossil".
**                             There should only be one of "repository:"
**                             or "directory:"
**
**    notfound: URL            When in "directory:" mode, redirect to
**                             URL if no suitable repository is found.
**
**    repolist                 When in "directory:" mode, display a page
**                             showing a list of available repositories if
**                             the URL is "/".
**
**    localauth                Grant administrator privileges to connections
**                             from 127.0.0.1 or ::1.
**
**    skin: LABEL              Use the built-in skin called LABEL rather than
**                             the default.  If there are no skins called LABEL
**                             then this line is a no-op.
**
**    files: GLOBLIST          GLOBLIST is a comma-separated list of GLOB
**                             patterns that specify files that can be
**                             returned verbatim.  This feature allows Fossil
**                             to act as a web server returning static
**                             content.
**
**    setenv: NAME VALUE       Set environment variable NAME to VALUE.  Or
**                             if VALUE is omitted, unset NAME.
**
**    HOME: PATH               Shorthand for "setenv: HOME PATH"
**
**    debug: FILE              Causing debugging information to be written
**                             into FILE.
**
**    errorlog: FILE           Warnings, errors, and panics written to FILE.
**
**    timeout: SECONDS         Do not run for longer than SECONDS.  The default
**                             timeout is FOSSIL_DEFAULT_TIMEOUT (600) seconds.
**
**    extroot: DIR             Directory that is the root of the sub-CGI tree
**                             on the /ext page.
**
**    redirect: REPO URL       Extract the "name" query parameter and search
**                             REPO for a check-in or ticket that matches the
**                             value of "name", then redirect to URL.  There
**                             can be multiple "redirect:" lines that are
**                             processed in order.  If the REPO is "*", then
**                             an unconditional redirect to URL is taken.
**
** Most CGI files contain only a "repository:" line.  It is uncommon to
** use any other option.
**
** See also: http, server, winsrv
*/
void cmd_cgi(void){
  const char *zFile;
  const char *zNotFound = 0;
  char **azRedirect = 0;             /* List of repositories to redirect to */
  int nRedirect = 0;                 /* Number of entries in azRedirect */
  Glob *pFileGlob = 0;               /* Pattern for files */
  int allowRepoList = 0;             /* Allow lists of repository files */
  Blob config, line, key, value, value2;
  /* Initialize the CGI environment. */
  g.httpOut = stdout;
  g.httpIn = stdin;
  fossil_binary_mode(g.httpOut);
  fossil_binary_mode(g.httpIn);
  g.cgiOutput = 1;
  fossil_set_timeout(FOSSIL_DEFAULT_TIMEOUT);
  /* Find the name of the CGI control file */
  if( g.argc==3 && fossil_strcmp(g.argv[1],"cgi")==0 ){
    zFile = g.argv[2];
  }else if( g.argc>=2 ){
    zFile = g.argv[1];
  }else{
    cgi_panic("No CGI control file specified");
  }
  /* Read and parse the CGI control file. */
  blob_read_from_file(&config, zFile, ExtFILE);
  while( blob_line(&config, &line) ){
    if( !blob_token(&line, &key) ) continue;
    if( blob_buffer(&key)[0]=='#' ) continue;
    if( blob_eq(&key, "repository:") && blob_tail(&line, &value) ){
      /* repository: FILENAME
      **
      ** The name of the Fossil repository to be served via CGI.  Most
      ** fossil CGI scripts have a single non-comment line that contains
      ** this one entry.
      */
      blob_trim(&value);
      db_open_repository(blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "directory:") && blob_token(&line, &value) ){
      /* directory: DIRECTORY
      **
      ** If repository: is omitted, then terms of the PATH_INFO cgi parameter
      ** are appended to DIRECTORY looking for a repository (whose name ends
      ** in ".fossil") or a file in "files:".
      */
      db_close(1);
      g.zRepositoryName = mprintf("%s", blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "notfound:") && blob_token(&line, &value) ){
      /* notfound: URL
      **
      ** If using directory: and no suitable repository or file is found,
      ** then redirect to URL.
      */
      zNotFound = mprintf("%s", blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "localauth") ){
      /* localauth
      **
      ** Grant "administrator" privileges to users connecting with HTTP
      ** from IP address 127.0.0.1.  Do not bother checking credentials.
      */
      g.useLocalauth = 1;
      continue;
    }
    if( blob_eq(&key, "repolist") ){
      /* repolist
      **
      ** If using "directory:" and the URL is "/" then generate a page
      ** showing a list of available repositories.
      */
      allowRepoList = 1;
      continue;
    }
    if( blob_eq(&key, "redirect:") && blob_token(&line, &value)
            && blob_token(&line, &value2) ){
      /* See the header comment on the redirect_web_page() function
      ** above for details. */
      nRedirect++;
      azRedirect = fossil_realloc(azRedirect, 2*nRedirect*sizeof(char*));
      azRedirect[nRedirect*2-2] = mprintf("%s", blob_str(&value));
      azRedirect[nRedirect*2-1] = mprintf("%s", blob_str(&value2));
      blob_reset(&value);
      blob_reset(&value2);
      continue;
    }
    if( blob_eq(&key, "files:") && blob_token(&line, &value) ){
      /* files: GLOBLIST
      **
      ** GLOBLIST is a comma-separated list of filename globs.  For
      ** example:  *.html,*.css,*.js
      **
      ** If the repository: line is omitted and then PATH_INFO is searched
      ** for files that match any of these GLOBs and if any such file is
      ** found it is returned verbatim.  This feature allows "fossil server"
      ** to function as a primitive web-server delivering arbitrary content.
      */
      pFileGlob = glob_create(blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "setenv:") && blob_token(&line, &value) ){
      /* setenv: NAME VALUE
      ** setenv: NAME
      **
      ** Sets environment variable NAME to VALUE.  If VALUE is omitted, then
      ** the environment variable is unset.
      */
      blob_token(&line,&value2);
      fossil_setenv(blob_str(&value), blob_str(&value2));
      blob_reset(&value);
      blob_reset(&value2);
      continue;
    }
    if( blob_eq(&key, "errorlog:") && blob_token(&line, &value) ){
      /* errorlog: FILENAME
      **
      ** Causes messages from warnings, errors, and panics to be appended
      ** to FILENAME.
      */
      g.zErrlog = mprintf("%s", blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "extroot:") && blob_token(&line, &value) ){
      /* extroot: DIRECTORY
      **
      ** Enables the /ext webpage to use sub-cgi rooted at DIRECTORY
      */
      g.zExtRoot = mprintf("%s", blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "timeout:") && blob_token(&line, &value) ){
      /* timeout: SECONDS
      **
      ** Set an alarm() that kills the process after SECONDS.  The
      ** default value is FOSSIL_DEFAULT_TIMEOUT (600) seconds.
      */
      fossil_set_timeout(atoi(blob_str(&value)));
      continue;
    }
    if( blob_eq(&key, "HOME:") && blob_token(&line, &value) ){
      /* HOME: VALUE
      **
      ** Set CGI parameter "HOME" to VALUE.  This is legacy.  Use
      ** setenv: instead.
      */
      cgi_setenv("HOME", blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "skin:") && blob_token(&line, &value) ){
      /* skin: LABEL
      **
      ** Use one of the built-in skins defined by LABEL.  LABEL is the
      ** name of the subdirectory under the skins/ directory that holds
      ** the elements of the built-in skin.  If LABEL does not match,
      ** this directive is a silent no-op.
      */
      skin_use_alternative(blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "cgi-debug:") && blob_token(&line, &value) ){
      /* cgi-debug: FILENAME
      **
      ** Causes output from cgi_debug() and CGIDEBUG(()) calls to go
      ** into FILENAME.  Useful for debugging CGI configuration problems.
      */
      char *zNow = cgi_iso8601_datestamp();
      cgi_load_environment();
      g.fDebug = fossil_fopen(blob_str(&value), "ab");
      blob_reset(&value);
      cgi_debug("-------- BEGIN cgi at %s --------\n", zNow);
      fossil_free(zNow);
      cgi_print_all(1,2);
      continue;
    }
  }
  blob_reset(&config);
  if( g.db==0 && g.zRepositoryName==0 && nRedirect==0 ){
    cgi_panic("Unable to find or open the project repository");
  }
  cgi_init();
  if( nRedirect ){
    redirect_web_page(nRedirect, azRedirect);
  }else{
    process_one_web_page(zNotFound, pFileGlob, allowRepoList);
  }
}

/*
** If g.argv[arg] exists then it is either the name of a repository
** that will be used by a server, or else it is a directory that
** contains multiple repositories that can be served.  If g.argv[arg]
** is a directory, the repositories it contains must be named
** "*.fossil".  If g.argv[arg] does not exist, then we must be within
** an open check-out and the repository to serve is the repository of
** that check-out.
**
** Open the repository to be served if it is known.  If g.argv[arg] is
** a directory full of repositories, then set g.zRepositoryName to
** the name of that directory and the specific repository will be
** opened later by process_one_web_page() based on the content of
** the PATH_INFO variable.
**
** If the fCreate flag is set, then create the repository if it
** does not already exist. Always use "auto" hash-policy in this case.
*/
static void find_server_repository(int arg, int fCreate){
  if( g.argc<=arg ){
    db_must_be_within_tree();
  }else{
    const char *zRepo = g.argv[arg];
    int isDir = file_isdir(zRepo, ExtFILE);
    if( isDir==1 ){
      g.zRepositoryName = mprintf("%s", zRepo);
      file_simplify_name(g.zRepositoryName, -1, 0);
    }else{
      if( isDir==0 && fCreate ){
        const char *zPassword;
        db_create_repository(zRepo);
        db_open_repository(zRepo);
        db_begin_transaction();
        g.eHashPolicy = HPOLICY_SHA3;
        db_set_int("hash-policy", HPOLICY_SHA3, 0);
        db_initial_setup(0, "now", g.zLogin);
        db_end_transaction(0);
        fossil_print("project-id: %s\n", db_get("project-code", 0));
        fossil_print("server-id:  %s\n", db_get("server-code", 0));
        zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
        fossil_print("admin-user: %s (initial password is \"%s\")\n",
                     g.zLogin, zPassword);
        cache_initialize();
        g.zLogin = 0;
        g.userUid = 0;
      }else{
        db_open_repository(zRepo);
      }
    }
  }
}

#if defined(_WIN32) && USE_SEE
/*
** This function attempts to parse a string value in the following
** format:
**
**     "%lu:%p:%u"
**
** There are three parts, which must be delimited by colons.  The
** first part is an unsigned long integer in base-10 (decimal) format.
** The second part is a numerical representation of a native pointer,
** in the appropriate implementation defined format.  The third part
** is an unsigned integer in base-10 (decimal) format.
**
** If the specified value cannot be parsed, for any reason, a fatal
** error will be raised and the process will be terminated.
*/
void parse_pid_key_value(
  const char *zPidKey, /* The value to be parsed. */
  DWORD *pProcessId,   /* The extracted process identifier. */
  LPVOID *ppAddress,   /* The extracted pointer value. */
  SIZE_T *pnSize       /* The extracted size value. */
){
  unsigned int nSize = 0;
  if( sscanf(zPidKey, "%lu:%p:%u", pProcessId, ppAddress, &nSize)==3 ){
    *pnSize = (SIZE_T)nSize;
  }else{
    fossil_fatal("failed to parse pid key");
  }
}
#endif

/*
** COMMAND: http*
**
** Usage: %fossil http ?REPOSITORY? ?OPTIONS?
**
** Handle a single HTTP request appearing on stdin.  The resulting webpage
** is delivered on stdout.  This method is used to launch an HTTP request
** handler from inetd, for example.  The argument is the name of the
** repository.
**
** If REPOSITORY is a directory that contains one or more repositories,
** either directly in REPOSITORY itself or in subdirectories, and
** with names of the form "*.fossil" then a prefix of the URL pathname
** selects from among the various repositories.  If the pathname does
** not select a valid repository and the --notfound option is available,
** then the server redirects (HTTP code 302) to the URL of --notfound.
** When REPOSITORY is a directory, the pathname must contain only
** alphanumerics, "_", "/", "-" and "." and no "-" may occur after a "/"
** and every "." must be surrounded on both sides by alphanumerics or else
** a 404 error is returned.  Static content files in the directory are
** returned if they match comma-separate GLOB pattern specified by --files
** and do not match "*.fossil*" and have a well-known suffix.
**
** The --host option can be used to specify the hostname for the server.
** The --https option indicates that the request came from HTTPS rather
** than HTTP. If --nossl is given, then SSL connections will not be available,
** thus also no redirecting from http: to https: will take place.
**
** If the --localauth option is given, then automatic login is performed
** for requests coming from localhost, if the "localauth" setting is not
** enabled.
**
** Options:
**   --baseurl URL    base URL (useful with reverse proxies)
**   --extroot DIR    document root for the /ext extension mechanism
**   --files GLOB     comma-separate glob patterns for static file to serve
**   --host NAME      specify hostname of the server
**   --https          signal a request coming in via https
**   --in FILE        Take input from FILE instead of standard input
**   --ipaddr ADDR    Assume the request comes from the given IP address
**   --localauth      enable automatic login for local connections
**   --nocompress     do not compress HTTP replies
**   --nodelay        omit backoffice processing if it would delay process exit
**   --nojail         drop root privilege but do not enter the chroot jail
**   --nossl          signal that no SSL connections are available
**   --notfound URL   use URL as "HTTP 404, object not found" page.
**   --out FILE       write results to FILE instead of to standard output
**   --repolist       If REPOSITORY is directory, URL "/" lists all repos
**   --scgi           Interpret input as SCGI rather than HTTP
**   --skin LABEL     Use override skin LABEL
**   --th-trace       trace TH1 execution (for debugging purposes)
**   --usepidkey      Use saved encryption key from parent process.  This is
**                    only necessary when using SEE on Windows.
**
** See also: cgi, server, winsrv
*/
void cmd_http(void){
  const char *zIpAddr = 0;
  const char *zNotFound;
  const char *zHost;
  const char *zAltBase;
  const char *zFileGlob;
  const char *zInFile;
  const char *zOutFile;
  int useSCGI;
  int noJail;
  int allowRepoList;
#if defined(_WIN32) && USE_SEE
  const char *zPidKey;
#endif

  Th_InitTraceLog();

  /* The winhttp module passes the --files option as --files-urlenc with
  ** the argument being URL encoded, to avoid wildcard expansion in the
  ** shell.  This option is for internal use and is undocumented.
  */
  zFileGlob = find_option("files-urlenc",0,1);
  if( zFileGlob ){
    char *z = mprintf("%s", zFileGlob);
    dehttpize(z);
    zFileGlob = z;
  }else{
    zFileGlob = find_option("files",0,1);
  }
  skin_override();
  zNotFound = find_option("notfound", 0, 1);
  noJail = find_option("nojail",0,0)!=0;
  allowRepoList = find_option("repolist",0,0)!=0;
  g.useLocalauth = find_option("localauth", 0, 0)!=0;
  g.sslNotAvailable = find_option("nossl", 0, 0)!=0;
  g.fNoHttpCompress = find_option("nocompress",0,0)!=0;
  g.zExtRoot = find_option("extroot",0,1);
  zInFile = find_option("in",0,1);
  if( zInFile ){
    backoffice_disable();
    g.httpIn = fossil_fopen(zInFile, "rb");
    if( g.httpIn==0 ) fossil_fatal("cannot open \"%s\" for reading", zInFile);
  }else{
    g.httpIn = stdin;
  }
  zOutFile = find_option("out",0,1);
  if( zOutFile ){
    g.httpOut = fossil_fopen(zOutFile, "wb");
    if( g.httpOut==0 ) fossil_fatal("cannot open \"%s\" for writing", zOutFile);
  }else{
    g.httpOut = stdout;
  }
  zIpAddr = find_option("ipaddr",0,1);
  useSCGI = find_option("scgi", 0, 0)!=0;
  zAltBase = find_option("baseurl", 0, 1);
  if( find_option("nodelay",0,0)!=0 ) backoffice_no_delay();
  if( zAltBase ) set_base_url(zAltBase);
  if( find_option("https",0,0)!=0 ){
    zIpAddr = fossil_getenv("REMOTE_HOST"); /* From stunnel */
    cgi_replace_parameter("HTTPS","on");
  }
  zHost = find_option("host", 0, 1);
  if( zHost ) cgi_replace_parameter("HTTP_HOST",zHost);

#if defined(_WIN32) && USE_SEE
  zPidKey = find_option("usepidkey", 0, 1);
  if( zPidKey ){
    DWORD processId = 0;
    LPVOID pAddress = NULL;
    SIZE_T nSize = 0;
    parse_pid_key_value(zPidKey, &processId, &pAddress, &nSize);
    db_read_saved_encryption_key_from_process(processId, pAddress, nSize);
  }
#endif

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=2 && g.argc!=3 ) usage("?REPOSITORY?");
  g.cgiOutput = 1;
  g.fullHttpReply = 1;
  find_server_repository(2, 0);
  if( zIpAddr==0 ){
    zIpAddr = cgi_ssh_remote_addr(0);
    if( zIpAddr && zIpAddr[0] ){
      g.fSshClient |= CGI_SSH_CLIENT;
    }
  }
  g.zRepositoryName = enter_chroot_jail(g.zRepositoryName, noJail);
  if( useSCGI ){
    cgi_handle_scgi_request();
  }else if( g.fSshClient & CGI_SSH_CLIENT ){
    ssh_request_loop(zIpAddr, glob_create(zFileGlob));
  }else{
    cgi_handle_http_request(zIpAddr);
  }
  process_one_web_page(zNotFound, glob_create(zFileGlob), allowRepoList);
}

/*
** Process all requests in a single SSH connection if possible.
*/
void ssh_request_loop(const char *zIpAddr, Glob *FileGlob){
  blob_zero(&g.cgiIn);
  do{
    cgi_handle_ssh_http_request(zIpAddr);
    process_one_web_page(0, FileGlob, 0);
    blob_reset(&g.cgiIn);
  } while ( g.fSshClient & CGI_SSH_FOSSIL ||
          g.fSshClient & CGI_SSH_COMPAT );
}

/*
** Note that the following command is used by ssh:// processing.
**
** COMMAND: test-http
**
** Works like the http command but gives setup permission to all users.
**
** Options:
**   --th-trace          trace TH1 execution (for debugging purposes)
**
*/
void cmd_test_http(void){
  const char *zIpAddr;    /* IP address of remote client */

  Th_InitTraceLog();
  login_set_capabilities("sx", 0);
  g.useLocalauth = 1;
  g.httpIn = stdin;
  g.httpOut = stdout;
  fossil_binary_mode(g.httpOut);
  fossil_binary_mode(g.httpIn);
  g.zExtRoot = find_option("extroot",0,1);
  find_server_repository(2, 0);
  g.cgiOutput = 1;
  g.fNoHttpCompress = 1;
  g.fullHttpReply = 1;
  zIpAddr = cgi_ssh_remote_addr(0);
  if( zIpAddr && zIpAddr[0] ){
    g.fSshClient |= CGI_SSH_CLIENT;
    ssh_request_loop(zIpAddr, 0);
  }else{
    cgi_set_parameter("REMOTE_ADDR", "127.0.0.1");
    cgi_handle_http_request(0);
    process_one_web_page(0, 0, 1);
  }
}

#if !defined(_WIN32)
#if !defined(__DARWIN__) && !defined(__APPLE__) && !defined(__HAIKU__)
/*
** Search for an executable on the PATH environment variable.
** Return true (1) if found and false (0) if not found.
*/
static int binaryOnPath(const char *zBinary){
  const char *zPath = fossil_getenv("PATH");
  char *zFull;
  int i;
  int bExists;
  while( zPath && zPath[0] ){
    while( zPath[0]==':' ) zPath++;
    for(i=0; zPath[i] && zPath[i]!=':'; i++){}
    zFull = mprintf("%.*s/%s", i, zPath, zBinary);
    bExists = file_access(zFull, X_OK);
    fossil_free(zFull);
    if( bExists==0 ) return 1;
    zPath += i;
  }
  return 0;
}
#endif
#endif

/*
** Respond to a SIGALRM by writing a message to the error log (if there
** is one) and exiting.
*/
#ifndef _WIN32
static void sigalrm_handler(int x){
  fossil_panic("TIMEOUT");
}
#endif

/*
** Arrange to timeout using SIGALRM after N seconds.  Or if N==0, cancel
** any pending timeout.
**
** Bugs:
** (1) This only works on unix systems.
** (2) Any call to sleep() or sqlite3_sleep() will cancel the alarm.
*/
void fossil_set_timeout(int N){
#ifndef _WIN32
  signal(SIGALRM, sigalrm_handler);
  alarm(N);
#endif
}

/*
** COMMAND: server*
** COMMAND: ui
**
** Usage: %fossil server ?OPTIONS? ?REPOSITORY?
**    or: %fossil ui ?OPTIONS? ?REPOSITORY?
**
** Open a socket and begin listening and responding to HTTP requests on
** TCP port 8080, or on any other TCP port defined by the -P or
** --port option.  The optional argument is the name of the repository.
** The repository argument may be omitted if the working directory is
** within an open checkout.
**
** The "ui" command automatically starts a web browser after initializing
** the web server.  The "ui" command also binds to 127.0.0.1 and so will
** only process HTTP traffic from the local machine.
**
** The REPOSITORY can be a directory (aka folder) that contains one or
** more repositories with names ending in ".fossil".  In this case, a
** prefix of the URL pathname is used to search the directory for an
** appropriate repository.  To thwart mischief, the pathname in the URL must
** contain only alphanumerics, "_", "/", "-", and ".", and no "-" may
** occur after "/", and every "." must be surrounded on both sides by
** alphanumerics.  Any pathname that does not satisfy these constraints
** results in a 404 error.  Files in REPOSITORY that match the comma-separated
** list of glob patterns given by --files and that have known suffixes
** such as ".txt" or ".html" or ".jpeg" and do not match the pattern
** "*.fossil*" will be served as static content.  With the "ui" command,
** the REPOSITORY can only be a directory if the --notfound option is
** also present.
**
** For the special case REPOSITORY name of "/", the list global configuration
** database is consulted for a list of all known repositories.  The --repolist
** option is implied by this special case.  See also the "fossil all ui"
** command.
**
** By default, the "ui" command provides full administrative access without
** having to log in.  This can be disabled by turning off the "localauth"
** setting.  Automatic login for the "server" command is available if the
** --localauth option is present and the "localauth" setting is off and the
** connection is from localhost.  The "ui" command also enables --repolist
** by default.
**
** Options:
**   --baseurl URL       Use URL as the base (useful for reverse proxies)
**   --create            Create a new REPOSITORY if it does not already exist
**   --extroot DIR       Document root for the /ext extension mechanism
**   --files GLOBLIST    Comma-separated list of glob patterns for static files
**   --localauth         enable automatic login for requests from localhost
**   --localhost         listen on 127.0.0.1 only (always true for "ui")
**   --https             Indicates that the input is coming through a reverse
**                       proxy that has already translated HTTPS into HTTP.
**   --max-latency N     Do not let any single HTTP request run for more than N
**                       seconds (only works on unix)
**   --nocompress        Do not compress HTTP replies
**   --nojail            Drop root privileges but do not enter the chroot jail
**   --nossl             signal that no SSL connections are available (Always
**                       set by default for the "ui" command)
**   --notfound URL      Redirect
**   --page PAGE         Start "ui" on PAGE.  ex: --page "timeline?y=ci"
**   -P|--port TCPPORT   listen to request on port TCPPORT
**   --th-trace          trace TH1 execution (for debugging purposes)
**   --repolist          If REPOSITORY is dir, URL "/" lists repos.
**   --scgi              Accept SCGI rather than HTTP
**   --skin LABEL        Use override skin LABEL
**   --usepidkey         Use saved encryption key from parent process.  This is
**                       only necessary when using SEE on Windows.
**
** See also: cgi, http, winsrv
*/
void cmd_webserver(void){
  int iPort, mxPort;        /* Range of TCP ports allowed */
  const char *zPort;        /* Value of the --port option */
  const char *zBrowser;     /* Name of web browser program */
  char *zBrowserCmd = 0;    /* Command to launch the web browser */
  int isUiCmd;              /* True if command is "ui", not "server' */
  const char *zNotFound;    /* The --notfound option or NULL */
  int flags = 0;            /* Server flags */
#if !defined(_WIN32)
  int noJail;               /* Do not enter the chroot jail */
  const char *zTimeout = 0; /* Max runtime of any single HTTP request */
#endif
  int allowRepoList;         /* List repositories on URL "/" */
  const char *zAltBase;      /* Argument to the --baseurl option */
  const char *zFileGlob;     /* Static content must match this */
  char *zIpAddr = 0;         /* Bind to this IP address */
  int fCreate = 0;           /* The --create flag */
  const char *zInitPage = 0; /* Start on this page.  --page option */
#if defined(_WIN32) && USE_SEE
  const char *zPidKey;
#endif

#if defined(_WIN32)
  const char *zStopperFile;    /* Name of file used to terminate server */
  zStopperFile = find_option("stopper", 0, 1);
#endif

  if( g.zErrlog==0 ){
    g.zErrlog = "-";
  }
  g.zExtRoot = find_option("extroot",0,1);
  zFileGlob = find_option("files-urlenc",0,1);
  if( zFileGlob ){
    char *z = mprintf("%s", zFileGlob);
    dehttpize(z);
    zFileGlob = z;
  }else{
    zFileGlob = find_option("files",0,1);
  }
  skin_override();
#if !defined(_WIN32)
  noJail = find_option("nojail",0,0)!=0;
  zTimeout = find_option("max-latency",0,1);
#endif
  g.useLocalauth = find_option("localauth", 0, 0)!=0;
  Th_InitTraceLog();
  zPort = find_option("port", "P", 1);
  isUiCmd = g.argv[1][0]=='u';
  if( isUiCmd ){
    zInitPage = find_option("page", 0, 1);
  }
  zNotFound = find_option("notfound", 0, 1);
  allowRepoList = find_option("repolist",0,0)!=0;
  if( find_option("nocompress",0,0)!=0 ) g.fNoHttpCompress = 1;
  zAltBase = find_option("baseurl", 0, 1);
  fCreate = find_option("create",0,0)!=0;
  if( find_option("scgi", 0, 0)!=0 ) flags |= HTTP_SERVER_SCGI;
  if( zAltBase ){
    set_base_url(zAltBase);
  }
  g.sslNotAvailable = find_option("nossl", 0, 0)!=0 || isUiCmd;
  if( find_option("https",0,0)!=0 ){
    cgi_replace_parameter("HTTPS","on");
  }
  if( find_option("localhost", 0, 0)!=0 ){
    flags |= HTTP_SERVER_LOCALHOST;
  }

#if defined(_WIN32) && USE_SEE
  zPidKey = find_option("usepidkey", 0, 1);
  if( zPidKey ){
    DWORD processId = 0;
    LPVOID pAddress = NULL;
    SIZE_T nSize = 0;
    parse_pid_key_value(zPidKey, &processId, &pAddress, &nSize);
    db_read_saved_encryption_key_from_process(processId, pAddress, nSize);
  }
#endif

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=2 && g.argc!=3 ) usage("?REPOSITORY?");
  if( isUiCmd ){
    flags |= HTTP_SERVER_LOCALHOST|HTTP_SERVER_REPOLIST;
    g.useLocalauth = 1;
    allowRepoList = 1;
  }
  find_server_repository(2, fCreate);
  if( zInitPage==0 ){
    if( isUiCmd && g.localOpen ){
      zInitPage = "timeline?c=current";
    }else{
      zInitPage = "";
    }
  }
  if( zPort ){
    if( strchr(zPort,':') ){
      int i;
      for(i=strlen(zPort)-1; i>=0 && zPort[i]!=':'; i--){}
      if( i>0 ){
        if( zPort[0]=='[' && zPort[i-1]==']' ){
          zIpAddr = mprintf("%.*s", i-2, zPort+1);
        }else{
          zIpAddr = mprintf("%.*s", i, zPort);
        }
        zPort += i+1;
      }
    }
    iPort = mxPort = atoi(zPort);
  }else{
    iPort = db_get_int("http-port", 8080);
    mxPort = iPort+100;
  }
#if !defined(_WIN32)
  /* Unix implementation */
  if( isUiCmd ){
#if !defined(__DARWIN__) && !defined(__APPLE__) && !defined(__HAIKU__)
    zBrowser = db_get("web-browser", 0);
    if( zBrowser==0 ){
      static const char *const azBrowserProg[] =
          { "xdg-open", "gnome-open", "firefox", "google-chrome" };
      int i;
      zBrowser = "echo";
      for(i=0; i<count(azBrowserProg); i++){
        if( binaryOnPath(azBrowserProg[i]) ){
          zBrowser = azBrowserProg[i];
          break;
        }
      }
    }
#else
    zBrowser = db_get("web-browser", "open");
#endif
    if( zIpAddr==0 ){
      zBrowserCmd = mprintf("%s http://localhost:%%d/%s &",
                            zBrowser, zInitPage);
    }else if( strchr(zIpAddr,':') ){
      zBrowserCmd = mprintf("%s http://[%s]:%%d/%s &",
                            zBrowser, zIpAddr, zInitPage);
    }else{
      zBrowserCmd = mprintf("%s http://%s:%%d/%s &",
                            zBrowser, zIpAddr, zInitPage);
    }
  }
  if( g.repositoryOpen ) flags |= HTTP_SERVER_HAD_REPOSITORY;
  if( g.localOpen ) flags |= HTTP_SERVER_HAD_CHECKOUT;
  db_close(1);
  if( cgi_http_server(iPort, mxPort, zBrowserCmd, zIpAddr, flags) ){
    fossil_fatal("unable to listen on TCP socket %d", iPort);
  }
  /* For the parent process, the cgi_http_server() command above never
  ** returns (except in the case of an error).  Instead, for each incoming
  ** client connection, a child process is created, file descriptors 0
  ** and 1 are bound to that connection, and the child returns.
  **
  ** So, when control reaches this point, we are running as a
  ** child process, the HTTP or SCGI request is pending on file
  ** descriptor 0 and the reply should be written to file descriptor 1.
  */
  if( zTimeout ){
    fossil_set_timeout(atoi(zTimeout));
  }else{
    fossil_set_timeout(FOSSIL_DEFAULT_TIMEOUT);
  }
  g.httpIn = stdin;
  g.httpOut = stdout;

#if !defined(_WIN32)
  signal(SIGSEGV, sigsegv_handler);
  signal(SIGPIPE, sigpipe_handler);
#endif

  if( g.fAnyTrace ){
    fprintf(stderr, "/***** Subprocess %d *****/\n", getpid());
  }
  g.cgiOutput = 1;
  find_server_repository(2, 0);
  if( fossil_strcmp(g.zRepositoryName,"/")==0 ){
    allowRepoList = 1;
  }else{
    g.zRepositoryName = enter_chroot_jail(g.zRepositoryName, noJail);
  }
  if( flags & HTTP_SERVER_SCGI ){
    cgi_handle_scgi_request();
  }else{
    cgi_handle_http_request(0);
  }
  process_one_web_page(zNotFound, glob_create(zFileGlob), allowRepoList);
  if( g.fAnyTrace ){
    fprintf(stderr, "/***** Webpage finished in subprocess %d *****/\n",
            getpid());
  }
#else
  /* Win32 implementation */
  if( isUiCmd ){
    zBrowser = db_get("web-browser", "start");
    if( zIpAddr==0 ){
      zBrowserCmd = mprintf("%s http://localhost:%%d/%s &",
                            zBrowser, zInitPage);
    }else if( strchr(zIpAddr,':') ){
      zBrowserCmd = mprintf("%s http://[%s]:%%d/%s &",
                            zBrowser, zIpAddr, zInitPage);
    }else{
      zBrowserCmd = mprintf("%s http://%s:%%d/%s &",
                            zBrowser, zIpAddr, zInitPage);
    }
  }
  if( g.repositoryOpen ) flags |= HTTP_SERVER_HAD_REPOSITORY;
  if( g.localOpen ) flags |= HTTP_SERVER_HAD_CHECKOUT;
  db_close(1);
  if( allowRepoList ){
    flags |= HTTP_SERVER_REPOLIST;
  }
  if( win32_http_service(iPort, zAltBase, zNotFound, zFileGlob, flags) ){
    win32_http_server(iPort, mxPort, zBrowserCmd, zStopperFile,
                      zAltBase, zNotFound, zFileGlob, zIpAddr, flags);
  }
#endif
}

/*
** COMMAND: test-echo
**
** Usage:  %fossil test-echo [--hex] ARGS...
**
** Echo all command-line arguments (enclosed in [...]) to the screen so that
** wildcard expansion behavior of the host shell can be investigated.
**
** With the --hex option, show the output as hexadecimal.  This can be used
** to verify the fossil_path_to_utf8() routine on Windows and Mac.
*/
void test_echo_cmd(void){
  int i, j;
  if( find_option("hex",0,0)==0 ){
    fossil_print("g.nameOfExe = [%s]\n", g.nameOfExe);
    for(i=0; i<g.argc; i++){
      fossil_print("argv[%d] = [%s]\n", i, g.argv[i]);
    }
  }else{
    unsigned char *z, c;
    for(i=0; i<g.argc; i++){
      fossil_print("argv[%d] = [", i);
      z = (unsigned char*)g.argv[i];
      for(j=0; (c = z[j])!=0; j++){
        fossil_print("%02x", c);
      }
      fossil_print("]\n");
    }
  }
}

/*
** WEBPAGE: test-warning
**
** Test error and warning log operation.  This webpage is accessible to
** the administrator only.
**
**     case=1           Issue a fossil_warning() while generating the page.
**     case=2           Extra db_begin_transaction()
**     case=3           Extra db_end_transaction()
**     case=4           Error during SQL processing
**     case=5           Call the segfault handler
**     case=6           Call webpage_assert()
**     case=7           Call webpage_error()
*/
void test_warning_page(void){
  int iCase = atoi(PD("case","0"));
  int i;
  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  style_header("Warning Test Page");
  style_submenu_element("Error Log","%R/errorlog");
  if( iCase<1 || iCase>4 ){
    @ <p>Generate a message to the <a href="%R/errorlog">error log</a>
    @ by clicking on one of the following cases:
  }else{
    @ <p>This is the test page for case=%d(iCase).  All possible cases:
  }
  for(i=1; i<=7; i++){
    @ <a href='./test-warning?case=%d(i)'>[%d(i)]</a>
  }
  @ </p>
  @ <p><ol>
  @ <li value='1'> Call fossil_warning()
  if( iCase==1 ){
    fossil_warning("Test warning message from /test-warning");
  }
  @ <li value='2'> Call db_begin_transaction()
  if( iCase==2 ){
    db_begin_transaction();
  }
  @ <li value='3'> Call db_end_transaction()
  if( iCase==3 ){
    db_end_transaction(0);
  }
  @ <li value='4'> warning during SQL
  if( iCase==4 ){
    Stmt q;
    db_prepare(&q, "SELECT uuid FROM blob LIMIT 5");
    db_step(&q);
    sqlite3_log(SQLITE_ERROR, "Test warning message during SQL");
    db_finalize(&q);
  }
  @ <li value='5'> simulate segfault handling
  if( iCase==5 ){
    sigsegv_handler(0);
  }
  @ <li value='6'> call webpage_assert(0)
  if( iCase==6 ){
    webpage_assert( 5==7 );
  }
  @ <li value='7'> call webpage_error()"
  if( iCase==7 ){
    cgi_reset_content();
    webpage_error("Case 7 from /test-warning");
  }
  @ </ol>
  @ <p>End of test</p>
  style_footer();
}
