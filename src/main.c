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
#  define GETPID (int)GetCurrentProcessId
#endif

/* BUGBUG: This (PID_T) does not work inside of INTERFACE block. */
#if USE_SEE
#if defined(_WIN32)
typedef DWORD PID_T;
#else
typedef pid_t PID_T;
#endif
#endif

#include "main.h"
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h> /* atexit() */
#include <zlib.h>
#if !defined(_WIN32)
#  include <errno.h> /* errno global */
#  include <unistd.h>
#  include <signal.h>
#  define GETPID getpid
#endif
#ifdef FOSSIL_ENABLE_SSL
#  include "openssl/crypto.h"
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
  char Password;         /* p: change password */
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
  char Chat;             /* C: read or write the chatroom */
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
  char **argvOrig;        /* Original g.argv prior to removing options */
  char *nameOfExe;        /* Full path of executable. */
  const char *zErrlog;    /* Log errors to this file, if not NULL */
  const char *zPhase;     /* Phase of operation, for use by the error log
                          ** and for deriving $canonical_page TH1 variable */
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
  int minPrefix;          /* Number of digits needed for a distinct hash */
  int eHashPolicy;        /* Current hash policy.  One of HPOLICY_* */
  int fSqlTrace;          /* True if --sqltrace flag is present */
  int fSqlStats;          /* True if --sqltrace or --sqlstats are present */
  int fSqlPrint;          /* True if --sqlprint flag is present */
  int fCgiTrace;          /* True if --cgitrace is enabled */
  int fQuiet;             /* True if -quiet flag is present */
  int fJail;              /* True if running with a chroot jail */
  int fHttpTrace;         /* Trace outbound HTTP requests */
  int fAnyTrace;          /* Any kind of tracing */
  int fAllowACME;         /* Deliver files from .well-known */
  char *zHttpAuth;        /* HTTP Authorization user:pass information */
  int fSystemTrace;       /* Trace calls to fossil_system(), --systemtrace */
  int fSshTrace;          /* Trace the SSH setup traffic */
  int fSshClient;         /* HTTP client flags for SSH client */
  int fNoHttpCompress;    /* Do not compress HTTP traffic (for debugging) */
  char *zSshCmd;          /* SSH command string */
  const char *zHttpCmd;   /* External program to do HTTP requests */
  int fNoSync;            /* Do not do an autosync ever.  --nosync */
  int fIPv4;              /* Use only IPv4, not IPv6. --ipv4 */
  char *zPath;            /* Name of webpage being served (may be NULL) */
  char *zExtra;           /* Extra path information past the webpage name */
  char *zBaseURL;         /* Full text of the URL being served */
  char *zHttpsURL;        /* zBaseURL translated to https: */
  char *zTop;             /* Parent directory of zPath */
  int nExtraURL;          /* Extra bytes added to SCRIPT_NAME */
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
  int httpUseSSL;         /* True to use an SSL codec for HTTP traffic */
  void *httpSSLConn;      /* The SSL connection */
  int xlinkClusterOnly;   /* Set when cloning.  Only process clusters */
  int fTimeFormat;        /* 1 for UTC.  2 for localtime.  0 not yet selected */
  int *aCommitFile;       /* Array of files to be committed */
  int markPrivate;        /* All new artifacts are private if true */
  char *ckinLockFail;     /* Check-in lock failure received from server */
  int clockSkewSeen;      /* True if clocks on client and server out of sync */
  int wikiFlags;          /* Wiki conversion flags applied to %W */
  char isHTTP;            /* True if server/CGI modes, else assume CLI. */
  char jsHref;            /* If true, set href= using javascript, not HTML */
  Blob httpHeader;        /* Complete text of the HTTP request header */
  UrlData url;            /* Information about current URL */
  const char *zLogin;     /* Login name.  NULL or "" if not logged in. */
  const char *zCkoutAlias;   /* doc/ uses this branch as an alias for "ckout" */
  const char *zMainMenuFile; /* --mainmenu FILE from server/ui/cgi */
  const char *zSSLIdentity;  /* Value of --ssl-identity option, filename of
                             ** SSL client identity */
  const char *zCgiFile;      /* Name of the CGI file */
  const char *zReqType;      /* Type of request: "HTTP", "CGI", "SCGI" */
#if USE_SEE
  const char *zPidKey;    /* Saved value of the --usepidkey option.  Only
                           * applicable when using SEE on Windows or Linux. */
#endif
  int useLocalauth;       /* No login required if from 127.0.0.1 */
  int noPswd;             /* Logged in without password (on 127.0.0.1) */
  int userUid;            /* Integer user id */
  int isRobot;            /* True if the client is definitely a robot.  False
                          ** negatives are common for this flag */
  int comFmtFlags;        /* Zero or more "COMMENT_PRINT_*" bit flags, should be
                          ** accessed through get_comment_format(). */
  const char *zSockName;  /* Name of the unix-domain socket file */
  const char *zSockMode;  /* File permissions for unix-domain socket */
  const char *zSockOwner; /* Owner, or owner:group for unix-domain socket */

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
  char zCsrfToken[16];    /* Value of the anti-CSRF token */
  int okCsrf;             /* -1:  unsafe
                          **  0:  unknown
                          **  1:  same origin
                          **  2:  same origin + is POST
                          **  3:  same origin, POST, valid csrf token */

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
  int bAvoidDeltaManifests;      /* Avoid using delta manifests if true */

  /* State for communicating specific details between the inbound HTTP
  ** header parser (cgi.c), xfer.c, and http.c. */
  struct {
    char *zLoginCard;       /* Inbound "x-f-l-c" Cookie header. */
    int fLoginCardMode;     /* If non-0, emit login cards in outbound
                            ** requests as a HTTP cookie instead of as
                            ** part of the payload. Gets activated
                            ** on-demand based on xfer traffic
                            ** contents. Values, for
                            ** diagnostic/debugging purposes: 0x01=CLI
                            ** --flag, 0x02=cgi_setup_query_string(),
                            ** 0x04=page_xfer(),
                            ** 0x08=client_sync(). */
    int remoteVersion;      /* Remote fossil version. Used for negotiating
                            ** how to handle the login card. */
  } syncInfo;
#ifdef FOSSIL_ENABLE_JSON
  struct FossilJsonBits {
    int isJsonMode;            /* True if running in JSON mode, else
                                  false. This changes how errors are
                                  reported. In JSON mode we try to
                                  always output JSON-form error
                                  responses and always (in CGI mode)
                                  exit() with code 0 to avoid an HTTP
                                  500 error.
                               */
    int preserveRc;            /* Do not convert error codes into 0.
                                * This is primarily intended for use
                                * by the test suite. */
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
  int ftntsIssues[4];     /* Counts for misref, strayed, joined, overnested */
  int diffCnt[3];         /* Counts for DIFF_NUMSTAT: files, ins, del */
};

/*
** Macro for debugging:
*/
#define CGIDEBUG(X)  if( g.fDebug ) cgi_debug X

#endif

Global g;

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
#if defined(_WIN32) || (defined(__BIONIC__) && !defined(FOSSIL_HAVE_GETPASS))
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
#if !defined(_WIN32)
  if( g.zSockName && file_issocket(g.zSockName) ){
    unlink(g.zSockName);
  }
#endif
  free(g.zErrMsg);
  if(g.db){
    db_close(0);
  }
  manifest_clear_cache();
  content_clear_cache(1);
  rebuild_clear_cache();
  /*
  ** FIXME: The next two lines cannot always be enabled; however, they
  **        are very useful for tracking down TH1 memory leaks.
  */
  if( fossil_getenv("TH1_DELETE_INTERP")!=0 ){
    if( g.interp ){
      Th_DeleteInterp(g.interp); g.interp = 0;
    }
#if defined(TH_MEMDEBUG)
    if( Th_GetOutstandingMalloc()!=0 ){
      fossil_print("Th_GetOutstandingMalloc() => %d\n",
                   Th_GetOutstandingMalloc());
    }
    assert( Th_GetOutstandingMalloc()==0 );
#endif
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

  g.argc = argc;
  g.argv = argv;
  sqlite3_initialize();
#if defined(_WIN32) && defined(BROKEN_MINGW_CMDLINE)
  for(i=0; (int)i<g.argc; i++) g.argv[i] = fossil_mbcs_to_utf8(g.argv[i]);
#else
  for(i=0; (int)i<g.argc; i++) g.argv[i] = fossil_path_to_utf8(g.argv[i]);
#endif
  g.nameOfExe = file_fullexename(g.argv[0]);
  for(i=1; (int)i<g.argc-1; i++){
    z = g.argv[i];
    if( z[0]!='-' ) continue;
    z++;
    if( z[0]=='-' ) z++;
    /* Maintenance reminder: we do not stop at a "--" flag here,
    ** instead delegating that to find_option(). Doing it here
    ** introduces some weird corner cases, as covered in forum thread
    ** 4382bbc66757c39f. e.g. (fossil -U -- --args ...) is handled
    ** differently when we stop at "--" here. */
    if( fossil_strcmp(z, "args")==0 ) break;
  }
  if( (int)i>=g.argc-1 ){
    g.argvOrig = fossil_malloc( sizeof(char*)*(g.argc+1) );
    memcpy(g.argvOrig, g.argv, sizeof(g.argv[0])*(g.argc+1));
    return;
  }

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
  newArgv = fossil_malloc( sizeof(char*)*nArg*2 + 2);
  for(j=0; j<i; j++) newArgv[j] = g.argv[j];

  blob_rewind(&file);
  while( nLine-->0 && (n = blob_line(&file, &line))>0 ){
    /* Reminder: ^^^ nLine check avoids that embedded NUL bytes in the
    ** --args file causes nLine to be less than blob_line() will end
    ** up reporting, as such a miscount leads to an illegal memory
    ** write. See forum post
    ** https://fossil-scm.org/forum/forumpost/7b34eecc1b8c for
    ** details */
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
  while( (int)i<g.argc ) newArgv[j++] = g.argv[i++];
  newArgv[j] = 0;
  g.argc = j;
  g.argv = newArgv;
  g.argvOrig = &g.argv[j+1];
  memcpy(g.argvOrig, g.argv, sizeof(g.argv[0])*(j+1));
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
** Initialize the g.comFmtFlags global variable.
**
** Global command-line options --comfmtflags or --comment-format can be
** used for this.  However, those command-line options are undocumented
** and deprecated.   They are here for backwards compatibility only.
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
  int wmain(int argc, wchar_t **argv){ return fossil_main(argc,(char**)argv); }
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

  g.zPhase = "init";
#if !defined(_WIN32_WCE)
  if( fossil_getenv("FOSSIL_BREAK") ){
    if( fossil_isatty(0) && fossil_isatty(2) ){
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

  fossil_printf_selfcheck();
  fossil_limit_memory(1);

  /* When updating the minimum SQLite version, change the number here,
  ** and also MINIMUM_SQLITE_VERSION value set in ../auto.def.  Take
  ** care that both places agree! */
  if( sqlite3_libversion_number()<3049000
   || strncmp(sqlite3_sourceid(),"2025-02-06",10)<0
  ){
    fossil_panic("Unsuitable SQLite version %s, must be at least 3.49.0",
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
  g.syncInfo.fLoginCardMode =
    /* The undocumented/unsupported --login-card-header provides a way
    ** to force use of the feature added by the xfer-login-card branch
    ** in 2025-07, intended for assisting in debugging any related
    ** issues. It can be removed once we reach the level of "implicit
    ** trust" in that feature. */
    find_option("login-card-header",0,0) ? 0x01 : 0;
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
  if( !find_option("nocgi", 0, 0) && fossil_getenv("GATEWAY_INTERFACE")!=0){
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
#ifdef FOSSIL_ENABLE_JSON
    g.json.preserveRc = find_option("json-preserve-rc", 0, 0)!=0;
#endif
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
#if USE_SEE
    db_maybe_handle_saved_encryption_key_for_process(SEE_KEY_READ);
#endif
    if( find_option("help",0,0)!=0 ){
      /* If --help is found anywhere on the command line, translate the command
       * to "fossil help cmdname" where "cmdname" is the first argument that
       * does not begin with a "-" character.  If all arguments start with "-",
       * translate to "fossil help argv[1] argv[2]...". */
      int i, nNewArgc;
      char **zNewArgv = fossil_malloc( sizeof(char*)*(g.argc+3) );
      zNewArgv[0] = g.argv[0];
      zNewArgv[1] = "help";
      zNewArgv[2] = "-c";
      for(i=1; i<g.argc; i++){
        if( g.argv[i][0]!='-' ){
          nNewArgc = 4;
          zNewArgv[3] = g.argv[i];
          zNewArgv[4] = 0;
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
#if 0
    }else if( g.argc==2 && file_is_repository(g.argv[1]) ){
      char **zNewArgv = fossil_malloc( sizeof(char*)*4 );
      zNewArgv[0] = g.argv[0];
      zNewArgv[1] = "ui";
      zNewArgv[2] = g.argv[1];
      zNewArgv[3] = 0;
      g.argc = 3;
      g.argv = zNewArgv;
#endif
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
  if( rc==1 && g.argc==2 && file_is_repository(g.argv[1]) ){
    /* If the command-line is "fossil ABC" and "ABC" is no a valid command,
    ** but "ABC" is the name of a repository file, make the command be
    ** "fossil ui ABC" instead.
    */
    char **zNewArgv = fossil_malloc( sizeof(char*)*4 );
    zNewArgv[0] = g.argv[0];
    zNewArgv[1] = "ui";
    zNewArgv[2] = g.argv[1];
    zNewArgv[3] = 0;
    g.argc = 3;
    g.argv = zNewArgv;
    g.zCmdName = zCmdName = "ui";
    rc = dispatch_name_search(zCmdName, CMDFLAG_COMMAND|CMDFLAG_PREFIX, &pCmd);
  }
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
    dispatch_matching_names(zCmdName, CMDFLAG_COMMAND, &couldbe);
    fossil_print("%s: ambiguous command prefix: %s\n"
                 "%s: could be any of:%s\n"
                 "%s: use \"help\" for more information\n",
                 g.argv[0], zCmdName, g.argv[0], blob_str(&couldbe), g.argv[0]);
    fossil_exit(1);
  }
#ifdef FOSSIL_ENABLE_JSON
  else if( rc==0 && strcmp("json",pCmd->zName)==0 ){
    g.json.isJsonMode = 1;
  }else{
    assert(!g.json.isJsonMode && "JSON-mode misconfiguration.");
  }
#endif
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
      g.zPhase = pCmd->zName;
      pCmd->xFunc();
      g.zPhase = "shutdown";
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
  memmove(&g.argv[i], &g.argv[i+n], sizeof(g.argv[i])*(g.argc-i-n));
  g.argc -= n;
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
        if( i+hasArg >= g.argc ) break;
        zReturn = g.argv[i+hasArg];
        remove_from_argv(i, 1+hasArg);
        break;
      }
    }else if( fossil_strcmp(z,zShort)==0 ){
      if( i+hasArg >= g.argc ) break;
      zReturn = g.argv[i+hasArg];
      remove_from_argv(i, 1+hasArg);
      break;
    }
  }
  return zReturn;
}

/*
** Restore an option previously removed by find_option().
*/
void restore_option(const char *zName, const char *zValue, int hasOpt){
  if( zValue==0 && hasOpt ) return;
  g.argv[g.argc++] = (char*)zName;
  if( hasOpt ) g.argv[g.argc++] = (char*)zValue;
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
    g.zRepositoryOption = fossil_strdup(zRepository);
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
void fossil_version_blob(
  Blob *pOut,                 /* Write the manifest here */
  int eVerbose                /* 0: brief.  1: more text,  2: lots of text */
){
#if defined(FOSSIL_ENABLE_TCL)
  int rc;
  const char *zRc;
#endif
  Stmt q;
  size_t pageSize = 0;
  blob_zero(pOut);
  blob_appendf(pOut, "This is fossil version %s\n", get_version());
  if( eVerbose<=0 ) return;

  blob_appendf(pOut, "Compiled on %s %s using %s (%d-bit)\n",
               __DATE__, __TIME__, COMPILER_NAME, sizeof(void*)*8);
  blob_appendf(pOut, "SQLite %s %.30s\n", sqlite3_libversion(),
               sqlite3_sourceid());
#if defined(FOSSIL_ENABLE_SSL)
  blob_appendf(pOut, "SSL (%s)\n", SSLeay_version(SSLEAY_VERSION));
#endif
  blob_appendf(pOut, "zlib %s, loaded %s\n", ZLIB_VERSION, zlibVersion());
#if defined(FOSSIL_HAVE_FUSEFS)
  blob_appendf(pOut, "libfuse %s, loaded %s\n", fusefs_inc_version(),
               fusefs_lib_version());
#endif
#if defined(FOSSIL_ENABLE_TCL)
  Th_FossilInit(TH_INIT_DEFAULT | TH_INIT_FORCE_TCL);
  rc = Th_Eval(g.interp, 0, "tclInvoke info patchlevel", -1);
  zRc = Th_ReturnCodeName(rc, 0);
  blob_appendf(pOut, "TCL (Tcl %s, loaded %s: %s)\n",
    TCL_PATCH_LEVEL, zRc, Th_GetResult(g.interp, 0)
  );
#endif
  if( eVerbose<=1 ) return;

  blob_appendf(pOut, "Schema version %s\n", AUX_SCHEMA_MAX);
  fossil_get_page_size(&pageSize);
  blob_appendf(pOut, "Detected memory page size is %lu bytes\n",
               (unsigned long)pageSize);
#if FOSSIL_HARDENED_SHA1
  blob_appendf(pOut, "hardened-SHA1 by Marc Stevens and Dan Shumow\n");
#endif
#if defined(FOSSIL_DEBUG)
  blob_append(pOut, "FOSSIL_DEBUG\n", -1);
#endif
#if defined(FOSSIL_ENABLE_DELTA_CKSUM_TEST)
  blob_append(pOut, "FOSSIL_ENABLE_DELTA_CKSUM_TEST\n", -1);
#endif
  blob_append(pOut, "FOSSIL_ENABLE_LEGACY_MV_RM\n", -1);
#if defined(FOSSIL_ENABLE_EXEC_REL_PATHS)
  blob_append(pOut, "FOSSIL_ENABLE_EXEC_REL_PATHS\n", -1);
#endif
#if defined(FOSSIL_ENABLE_TH1_DOCS)
  blob_append(pOut, "FOSSIL_ENABLE_TH1_DOCS\n", -1);
#endif
#if defined(FOSSIL_ENABLE_TH1_HOOKS)
  blob_append(pOut, "FOSSIL_ENABLE_TH1_HOOKS\n", -1);
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
  blob_append(pOut, "MARKDOWN\n", -1);
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
  blob_appendf(pOut, "USE_SEE (%s)\n",
               db_have_saved_encryption_key() ? "SET" : "UNSET");
#endif
#if defined(FOSSIL_ALLOW_OUT_OF_ORDER_DATES)
  blob_append(pOut, "FOSSIL_ALLOW_OUT_OF_ORDER_DATES\n");
#endif

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
** Usage: %fossil version ?-v|--verbose?
**
** Print the source code version number for the fossil executable.
** If the verbose option is specified, additional details will
** be output about what optional features this binary was compiled
** with.
**
** Repeat the -v option or use -vv for even more information.
*/
void version_cmd(void){
  Blob versionInfo;
  int verboseFlag = 0;

  while( find_option("verbose","v",0)!=0 ) verboseFlag++;
  while( find_option("vv",0,0)!=0 )        verboseFlag += 2;

  /* We should be done with options.. */
  verify_all_options();
  fossil_version_blob(&versionInfo, verboseFlag);
  fossil_print("%s", blob_str(&versionInfo));
  blob_reset(&versionInfo);
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
  verboseFlag = P("verbose")!=0 ? 2 : 1;
  style_header("Version Information");
  style_submenu_element("Stat", "stat");
  fossil_version_blob(&versionInfo, verboseFlag);
  @ <pre>
  @ %h(blob_str(&versionInfo))
  @ </pre>
  style_finish_page();
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
    g.zTop = g.zBaseURL = fossil_strdup(zAltBase);
    i = (int)strlen(g.zBaseURL);
    while( i>3 && g.zBaseURL[i-1]=='/' ){ i--; }
    g.zBaseURL[i] = 0;
    if( strncmp(g.zTop, "http://", 7)==0 ){
      /* it is HTTP, replace prefix with HTTPS. */
      g.zHttpsURL = mprintf("https://%s", &g.zTop[7]);
    }else if( strncmp(g.zTop, "https://", 8)==0 ){
      /* it is already HTTPS, use it. */
      g.zHttpsURL = fossil_strdup(g.zTop);
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
    if( n==2 ) g.zTop = "";
    if( g.zTop==g.zBaseURL ){
      fossil_fatal("argument to --baseurl should be 'http://host/path'"
                   " or 'https://host/path'");
    }
    if( g.zTop[1]==0 ) g.zTop++;
  }else{
    char *z;
    zMode = PD("HTTPS","off");
    zHost = PD("HTTP_HOST","");
    z = fossil_strdup(zHost);
    for(i=0; z[i]; i++){
      if( z[i]<='Z' && z[i]>='A' ) z[i] += 'a' - 'A';
    }
    if( fossil_strcmp(zMode,"on")==0 ){
      /* Remove trailing ":443" from the HOST, if any */
      if( i>4 && z[i-1]=='3' && z[i-2]=='4' && z[i-3]=='4' && z[i-4]==':' ){
        i -= 4;
      }
    }else{
      /* Remove trailing ":80" from the HOST */
      if( i>3 && z[i-1]=='0' && z[i-2]=='8' && z[i-3]==':' ) i -= 3;
    }
    if( i && z[i-1]=='.' ) i--;
    z[i] = 0;
    zCur = PD("SCRIPT_NAME","/");
    i = strlen(zCur);
    while( i>0 && zCur[i-1]=='/' ) i--;
    if( fossil_stricmp(zMode,"on")==0 ){
      g.zBaseURL = mprintf("https://%s%.*s", z, i, zCur);
      g.zTop = &g.zBaseURL[8+strlen(z)];
      g.zHttpsURL = g.zBaseURL;
    }else{
      g.zBaseURL = mprintf("http://%s%.*s", z, i, zCur);
      g.zTop = &g.zBaseURL[7+strlen(z)];
      g.zHttpsURL = mprintf("https://%s%.*s", z, i, zCur);
    }
    fossil_free(z);
  }

  /* Try to record the base URL as a CONFIG table entry with a name
  ** of the form:  "baseurl:BASE".  This keeps a record of how the
  ** the repository is used as a server, to help in answering questions
  ** like "where is the CGI script that references this repository?"
  **
  ** This is just a logging hint.  So don't worry if it cannot be done.
  ** Don't try this if the repository database is not writable, for
  ** example.
  **
  ** If g.useLocalauth is set, that (probably) means that we are running
  ** "fossil ui" and there is no point in logging those cases either.
  */
  if( db_is_writeable("repository") && !g.useLocalauth ){
    int nBase = (int)strlen(g.zBaseURL);
    char *zBase = g.zBaseURL;
    if( g.nExtraURL>0 && g.nExtraURL<nBase-6 ){
      zBase = fossil_strndup(g.zBaseURL, nBase - g.nExtraURL);
    }
    db_unprotect(PROTECT_CONFIG);
    if( !db_exists("SELECT 1 FROM config WHERE name='baseurl:%q'", zBase)){
      db_multi_exec("INSERT INTO config(name,value,mtime)"
                    "VALUES('baseurl:%q',1,now())", zBase);
    }else{
      db_optional_sql("repository",
           "REPLACE INTO config(name,value,mtime)"
           "VALUES('baseurl:%q',1,now())", zBase
      );
    }
    db_protect_pop();
    if( zBase!=g.zBaseURL ) fossil_free(zBase);
  }
}

/*
** Send an HTTP redirect back to the designated Index Page.
*/
NORETURN void fossil_redirect_home(void){
  /* In order for ?skin=... to work when visiting the site from
  ** a typical external link, we have to process it here, as
  ** that parameter gets lost during the redirect. We "could"
  ** pass the whole query string along instead, but that seems
  ** unnecessary. */
  if(cgi_setup_query_string() & 0x02){
    cookie_render();
  }
  cgi_redirectf("%R%s", db_get("index-page", "/index"));
}

/*
** If running as root, chroot to the directory containing the
** repository zRepo and then drop root privileges.  Return the
** new repository name.
**
** zRepo can be a directory.  If so and if the repo name was saved
** to g.zRepositoryName before we were called, we canonicalize the
** two paths and check that one is the prefix of the other, else you
** won't be able to open the repo inside the jail.  If it all works
** out, we return the "jailed" version of the repo name.
**
** Assume the user-id and group-id of the repository, or if zRepo
** is a directory, of that directory.
**
** The noJail flag means that the chroot jail is not entered.  But
** privileges are still lowered to that of the user-id and group-id
** of the repository file.
*/
static char *enter_chroot_jail(const char *zRepo, int noJail){
#if !defined(_WIN32)
  if( getuid()==0 ){
    int i;
    struct stat sStat;
    Blob dir;
    char *zDir;
    size_t nDir;
    if( g.db!=0 ){
      db_close(1);
    }

    file_canonical_name(zRepo, &dir, 0);
    zDir = blob_str(&dir);
    nDir = blob_size(&dir);
    if( !noJail ){
      if( file_isdir(zDir, ExtFILE)==1 ){
        /* Translate the repository name to the new root */
        if( g.zRepositoryName ){
          Blob repo;
          file_canonical_name(g.zRepositoryName, &repo, 0);
          zRepo = blob_str(&repo);
          if( strncmp(zRepo, zDir, nDir)!=0 ){
            fossil_fatal("repo %s not under chroot dir %s", zRepo, zDir);
          }
          zRepo += nDir;
          if( *zRepo == '\0' ) zRepo = "/";
        }else {
          zRepo = "/";
        }
        /* If a unix socket is defined, try to translate its name into
        ** the new root so that it can be delete by atexit().  If unable,
        ** just zero out the socket name. */
        if( g.zSockName ){
          if( strncmp(g.zSockName, zDir, nDir)==0
           && g.zSockName[nDir]=='/'
          ){
            g.zSockName += nDir;
          }else{
            g.zSockName = 0;
          }
        }
        if( file_chdir(zDir, 1) ){
          fossil_panic("unable to chroot into %s", zDir);
        }
      }else{
        for(i=strlen(zDir)-1; i>0 && zDir[i]!='/'; i--){}
        if( zDir[i]!='/' ) fossil_fatal("bad repository name: %s", zRepo);
        if( i>0 ){
          zDir[i] = 0;
          if( file_chdir(zDir, 1) ){
            fossil_fatal("unable to chroot into %s", zDir);
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
  return (char*)zRepo;  /* no longer const: always reassigned from blob_str() */
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
  blob_appendf(&out, "Segfault during %s in fossil %s",
               g.zPhase, MANIFEST_VERSION);
  for(i=0; i<size; i++){
    size_t len;
    const char *z = strings[i];
    if( i==0 ) blob_appendf(&out, "\nBacktrace:");
    len = strlen(strings[i]);
    if( z[0]=='[' && z[len-1]==']' ){
      blob_appendf(&out, " %.*s", (int)(len-2), &z[1]);
    }else{
      blob_appendf(&out, " %s", z);
    }
  }
  fossil_panic("%s", blob_str(&out));
#else
  fossil_panic("Segfault during %s in fossil %s",
               g.zPhase, MANIFEST_VERSION);
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
  g.zPhase = "sigpipe shutdown";
  db_panic_close();
  exit(1);
}

/*
** Return true if it is appropriate to redirect requests to HTTPS.
**
** Redirect to https is appropriate if all of the above are true:
**    (1) The redirect-to-https flag has a value of iLevel or greater.
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
** Send a 404 Not Found reply
*/
void fossil_not_found_page(void){
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
  cgi_set_status(404, "Not Found");
  cgi_reply();
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
  int isReadonly = 0;

  g.zPhase = "process_one_web_page";
#if !defined(_WIN32)
  signal(SIGSEGV, sigsegv_handler);
#endif

  /* Decode %HH escapes in PATHINFO */
  if( strchr(zPathInfo,'%') ){
    char *z = fossil_strdup(zPathInfo);
    dehttpize(z);
    zPathInfo = z;
  }

  /* Handle universal query parameters */
  if( PB("utc") ){
    g.fTimeFormat = 1;
  }else if( PB("localtime") ){
    g.fTimeFormat = 2;
  }
#ifdef FOSSIL_ENABLE_JSON
  /*
  ** Ensure that JSON mode is set up if we're visiting /json, to allow
  ** us to customize some following behaviour (error handling and only
  ** process JSON-mode POST data if we're actually in a /json
  ** page). This is normally set up before this routine is called, but
  ** it looks like the ssh_request_loop() approach to dispatching
  ** might bypass that.
  */
  if( g.json.isJsonMode==0 && json_request_is_json_api(zPathInfo)!=0 ){
    g.json.isJsonMode = 1;
    json_bootstrap_early();
  }
#endif
  /* If the repository has not been opened already, then find the
  ** repository based on the first element of PATH_INFO and open it.
  */
  if( !g.repositoryOpen ){
    char zBuf[24];
    const char *zRepoExt = ".fossil";
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
      size_t nBase = strlen(zBase);
      while( zPathInfo[i] && zPathInfo[i]!='/' ){ i++; }

      /* The candidate repository name is some prefix of the PATH_INFO
      ** with ".fossil" appended */
      zRepo = zToFree = mprintf("%s%.*s%s",zBase,i,zPathInfo,zRepoExt);
      if( g.fHttpTrace ){
        @ <!-- Looking for repository named "%h(zRepo)" -->
        fprintf(stderr, "# looking for repository named \"%s\"\n", zRepo);
      }


      /* Restrictions on the URI for security:
      **
      **    1.  Reject characters that are not ASCII alphanumerics,
      **        "-", "_", ".", "/", or unicode (above ASCII).
      **        In other words:  No ASCII punctuation or control characters
      **        other than "-", "_", "." and "/".
      **    2.  Exception to rule 1: Allow /X:/ where X is any ASCII
      **        alphabetic character at the beginning of the name on windows.
      **    3.  "-" may not occur immediately after "/"
      **    4.  "." may not be adjacent to another "." or to "/"
      **
      ** Any character does not satisfy these constraints a Not Found
      ** error is returned.
      */
      szFile = 0;
      for(j=nBase+1, k=0; zRepo[j] && k<i-1; j++, k++){
        char c = zRepo[j];
        if( c>='a' && c<='z' ) continue;
        if( c>='A' && c<='Z' ) continue;
        if( c>='0' && c<='9' ) continue;
        if( (c&0x80)==0x80 ) continue;
#if defined(_WIN32) || defined(__CYGWIN__)
        /* Allow names to begin with "/X:/" on windows */
        if( c==':' && j==2 && sqlite3_strglob("/[a-zA-Z]:/*", zRepo)==0 ){
          continue;
        }
#endif
        if( c=='/' ) continue;
        if( c=='_' ) continue;
        if( c=='-' && zRepo[j-1]!='/' ) continue;
        if( c=='.'
         && zRepo[j-1]!='.' && zRepo[j-1]!='/'
         && zRepo[j+1]!='.' && zRepo[j+1]!='/'
        ){
          continue;
        }
        if( c=='.' && g.fAllowACME && j==(int)nBase+1
         && strncmp(&zRepo[j-1],"/.well-known/",12)==0
        ){
          /* We allow .well-known as the top-level directory for ACME */
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
        if( szFile>0 && !file_isfile(zCleanRepo, ExtFILE) ){
          /* Only let szFile be non-negative if zCleanRepo really is a file
          ** and not a directory or some other filesystem object. */
          szFile = -1;
        }
        if( g.fHttpTrace ){
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
        assert( file_is_repository_extension(&zRepo[j]) );
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
         && glob_match(pFileGlob, file_cleanup_fullpath(zRepo+nBase))
         && !file_contains_repository_extension(zRepo)
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

        /* In support of the ACME protocol, files under the .well-known/
        ** directory is always accepted.
        */
        if( g.fAllowACME
         && strncmp(&zRepo[nBase],"/.well-known/",12)==0
         && file_isfile(zCleanRepo, ExtFILE)
        ){
          Blob content;
          blob_read_from_file(&content, file_cleanup_fullpath(zRepo), ExtFILE);
          cgi_set_content_type(mimetype_from_name(zRepo));
          cgi_set_content(&content);
          cgi_reply();
          return;
        }
        zRepo[j] = '.';
      }

      /* If we reach this point, it means that the search of the PATH_INFO
      ** string is finished.  Either zRepo contains the name of the
      ** repository to be used, or else no repository could be found and
      ** some kind of error response is required.
      */
      if( szFile<1024 ){
#if USE_SEE
        if( strcmp(zRepoExt,".fossil")==0 ){
          fossil_free(zToFree);
          zRepoExt = ".efossil";
          continue;
        }
#endif
        set_base_url(0);
        if( (zPathInfo[0]==0 || strcmp(zPathInfo,"/")==0)
                  && allowRepoList
                  && repo_list_page() ){
          /* Will return a list of repositories */
        }else if( zNotFound ){
          cgi_redirect(zNotFound);
        }else{
          fossil_not_found_page();
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
    if( g.zTop ) g.zTop = mprintf("%R%.*s", i, zPathInfo);
    if( g.zBaseURL ) g.zBaseURL = mprintf("%s%.*s", g.zBaseURL, i, zPathInfo);
    cgi_replace_parameter("PATH_INFO", &zPathInfo[i+1]);
    zPathInfo += i;
    cgi_replace_parameter("SCRIPT_NAME", zNewScript);
#if USE_SEE
    if( zPathInfo ){
      if( g.fHttpTrace ){
        sqlite3_snprintf(sizeof(zBuf), zBuf, "%d", i);
        @ <!-- see_path_info(%s(zBuf)) is %h(zPathInfo) -->
        fprintf(stderr, "# see_path_info(%d) = %s\n", i, zPathInfo);
      }
      if( strcmp(zPathInfo,"/setseekey")==0
       && strcmp(zRepoExt,".efossil")==0
       && !db_have_saved_encryption_key() ){
        db_set_see_key_page();
        cgi_reply();
        fossil_exit(0);
      }
    }
#endif
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
  */

  /*
  ** Check to see if the first term of PATH_INFO specifies an
  ** alternative skin.  This will be the case if the first term of
  ** PATH_INFO begins with "draftN/" where N is an integer between 1
  ** and 9. If so, activate the skin associated with that draft.
  */
  if( zPathInfo && strncmp(zPathInfo,"/draft",6)==0
   && zPathInfo[6]>='1' && zPathInfo[6]<='9'
   && (zPathInfo[7]=='/' || zPathInfo[7]==0)
  ){
    int iSkin = zPathInfo[6] - '0';
    char *zNewScript;
    if( db_int(0,"SELECT count(*) FROM config WHERE name GLOB 'draft%d-*'",
                iSkin)<5 ){
      fossil_not_found_page();
      fossil_exit(0);
    }
    skin_use_draft(iSkin);
    zNewScript = mprintf("%T/draft%d", P("SCRIPT_NAME"), iSkin);
    if( g.zTop ) g.zTop = mprintf("%R/draft%d", iSkin);
    if( g.zBaseURL ) g.zBaseURL = mprintf("%s/draft%d", g.zBaseURL, iSkin);
    zPathInfo += 7;
    g.nExtraURL += 7;
    cgi_replace_parameter("PATH_INFO", zPathInfo);
    cgi_replace_parameter("SCRIPT_NAME", zNewScript);
    etag_cancel();
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
    /* Second special case: If the PATH_INFO is blank, issue a
    ** temporary 302 redirect:
    **    (1) to "/ckout" if g.useLocalauth and g.localOpen are both set.
    **    (2) to the home page identified by the "index-page" setting
    **        in the repository CONFIG table
    **    (3) to "/index" if there no "index-page" setting in CONFIG
    */
#ifdef FOSSIL_ENABLE_JSON
    if(g.json.isJsonMode){
      json_err(FSL_JSON_E_RESOURCE_NOT_FOUND,NULL,1);
      fossil_exit(0);
    }
#endif
    if( g.useLocalauth && g.localOpen ){
      cgi_redirectf("%R/ckout");
    }else{
      fossil_redirect_home() /*does not return*/;
    }
  }else{
    zPath = fossil_strdup(zPathInfo);
  }

  /* Make g.zPath point to the first element of the path.  Make
  ** g.zExtra point to everything past that point.
  */
  g.zPath = &zPath[1];
  for(i=1; zPath[i] && zPath[i]!='/'; i++){}
  if( zPath[i]=='/' ){
    zPath[i] = 0;
    g.zExtra = &zPath[i+1];
  }else{
    g.zExtra = 0;
  }
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
    if(g.json.isJsonMode==0){
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
    if(g.json.isJsonMode!=0){
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
    if(g.json.isJsonMode!=0){
      json_err(FSL_JSON_E_DB_NEEDS_REBUILD,NULL,0);
    }else
#endif
    {
      @ <h1>Server Configuration Error</h1>
      @ <p>The database schema on the server is out-of-date.  Please ask
      @ the administrator to run <b>fossil rebuild</b>.</p>
    }
  }else{
    if(0==(CMDFLAG_LDAVG_EXEMPT & pCmd->eCmdFlags)){
      load_control();
    }
#ifdef FOSSIL_ENABLE_JSON
    {
      static int jsonOnce = 0;
      if( jsonOnce==0 && g.json.isJsonMode!=0 ){
        assert(json_is_bootstrapped_early());
        json_bootstrap_late();
        jsonOnce = 1;
      }
    }
#endif
    if( (pCmd->eCmdFlags & CMDFLAG_RAWCONTENT)==0 ){
      cgi_decode_post_parameters();
      if( !cgi_same_origin(0) ){
        isReadonly = 1;
        db_protect(PROTECT_READONLY);
      }
    }
    if( g.fCgiTrace ){
      fossil_trace("######## Calling %s #########\n", pCmd->zName);
      cgi_print_all(1, 1, 0);
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
          g.zPhase = pCmd->zName;
          pCmd->xFunc();
#ifdef FOSSIL_ENABLE_TH1_HOOKS
        }
        if( !g.fNoThHook && (rc==TH_OK || rc==TH_CONTINUE) ){
          Th_WebpageNotify(pCmd->zName+1, pCmd->eCmdFlags);
        }
      }
    }
#endif
    if( isReadonly ){
      db_protect_pop();
    }
  }

  /* Return the result.
  */
  g.zPhase = "web-page reply";
  cgi_reply();
}

/* If the CGI program contains one or more lines of the form
**
**    redirect:  repository-filename  http://hostname/path/%s
**
** then control jumps here.  Search each repository for an artifact ID
** or ticket ID that matches the "name" query parameter.  If there is
** no "name" query parameter, use PATH_INFO instead.  If a match is
** found, redirect to the corresponding URL.  Substitute "%s" in the
** URL with the value of the name query parameter before the redirect.
**
** If there is a line of the form:
**
**    redirect: * URL
**
** Then a redirect is made to URL if no match is found.  If URL contains
** "%s" then substitute the "name" query parameter.  If REPO is "*" and
** URL does not contains "%s" and does not contain "?" then append
** PATH_INFO and QUERY_STRING to the URL prior to the redirect.
**
** If no matches are found and if there is no "*" entry, then generate
** a primitive error message.
**
** USE CASES:
**
** (1)  Suppose you have two related projects projA and projB.  You can
**      use this feature to set up an /info page that covers both
**      projects.
**
**          redirect: /fossils/projA.fossil /proj-a/info/%s
**          redirect: /fossils/projB.fossil /proj-b/info/%s
**
**      Then visits to the /info/HASH page will redirect to the
**      first project that contains that hash.
**
** (2)  Use the "*" form for to redirect legacy URLs.  On the Fossil
**      website we have an CGI at http://fossil.com/index.html (note
**      ".com" instead of ".org") that looks like this:
**
**          #!/usr/bin/fossil
**          redirect: * https://fossil-scm.org/home
**
**      Thus requests to the .com website redirect to the .org website.
**      This form uses a 301 Permanent redirect.
**
**      On a "*" redirect, the PATH_INFO and QUERY_STRING of the query
**      that provoked the redirect are appended to the target.  So, for
**      example, if the input URL for the redirect above were
**      "http://www.fossil.com/index.html/timeline?c=20250404", then
**      the redirect would be to:
**
**           https://fossil-scm.org/home/timeline?c=20250404
**                                      ^^^^^^^^^^^^^^^^^^^^
**                                      Copied from input URL
*/
static void redirect_web_page(int nRedirect, char **azRedirect){
  int i;                             /* Loop counter */
  const char *zNotFound = 0;         /* Not found URL */
  const char *zName = P("name");
  set_base_url(0);
  if( zName==0 ){
    zName = P("PATH_INFO");
    if( zName && zName[0]=='/' ) zName++;
  }
  if( zName ){
    for(i=0; i<nRedirect; i++){
      if( fossil_strcmp(azRedirect[i*2],"*")==0 ){
        zNotFound = azRedirect[i*2+1];
        continue;
      }else if( validate16(zName, strlen(zName)) ){
        db_open_repository(azRedirect[i*2]);
        if( db_exists("SELECT 1 FROM blob WHERE uuid GLOB '%q*'", zName) ||
            db_exists("SELECT 1 FROM ticket WHERE tkt_uuid GLOB '%q*'",zName) ){
          cgi_redirectf(azRedirect[i*2+1] /*works-like:"%s"*/, zName);
          return;
        }
        db_close(1);
      }
    }
  }
  if( zNotFound ){
    Blob to;
    const char *z;
    if( strstr(zNotFound, "%s") ){
      char *zTarget = mprintf(zNotFound /*works-like:"%s"*/, zName);
      cgi_redirect_perm(zTarget);
    }
    if( strchr(zNotFound, '?') ){
      cgi_redirect_perm(zNotFound);
    }
    blob_init(&to, zNotFound, -1);
    z = P("PATH_INFO");
    if( z && z[0]=='/' ) blob_append(&to, z, -1);
    z = P("QUERY_STRING");
    if( z && z[0]!=0 ) blob_appendf(&to, "?%s", z);
    cgi_redirect_perm(blob_str(&to));
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
**                             the URL is "/".  Some control over the display
**                             is accomplished using environment variables.
**                             FOSSIL_REPOLIST_TITLE is the tital of the page.
**                             FOSSIL_REPOLIST_SHOW cause the "Description"
**                             column to display if it contains "description" as
**                             as a substring, and causes the Login-Group column
**                             to display if it contains the "login-group"
**                             substring.
**
**    localauth                Grant administrator privileges to connections
**                             from 127.0.0.1 or ::1.
**
**    nossl                    Signal that no SSL connections are available.
**
**    nocompress               Do not compress HTTP replies.
**
**    skin: LABEL              Use the built-in skin called LABEL rather than
**                             the default, or the default if LABEL is empty.
**                             If there are no skins called LABEL then this
**                             line is a no-op.
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
**    cgi-debug: FILE          Causing debugging information to be written
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
**                             When "*" is used a 301 permanent redirect is
**                             issued and the tail and query string from the
**                             original query are appeneded onto URL.
**
**    jsmode: VALUE            Specifies the delivery mode for JavaScript
**                             files. See the help text for the --jsmode
**                             flag of the http command.
**
**    mainmenu: FILE           Override the mainmenu config setting with the
**                             contents of the given file.
**
** Most CGI files contain only a "repository:" line.  It is uncommon to
** use any other option.
**
** The lines are processed in the order they are read, which is most
** significant for "errorlog:", which should be set before "repository:"
** so that any warnings from the database when opening the repository
** go to that log file.
**
** See also: [[http]], [[server]], [[winsrv]] [Windows only]
*/
void cmd_cgi(void){
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
  g.zReqType = "CGI";
  fossil_set_timeout(FOSSIL_DEFAULT_TIMEOUT);
  /* Find the name of the CGI control file */
  if( g.argc==3 && fossil_strcmp(g.argv[1],"cgi")==0 ){
    g.zCgiFile = g.argv[2];
  }else if( g.argc>=2 ){
    g.zCgiFile = g.argv[1];
  }else{
    cgi_panic("No CGI control file specified");
  }
  /* Read and parse the CGI control file. */
  blob_read_from_file(&config, g.zCgiFile, ExtFILE);
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
      g.zRepositoryName = fossil_strdup(blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "notfound:") && blob_token(&line, &value) ){
      /* notfound: URL
      **
      ** If using directory: and no suitable repository or file is found,
      ** then redirect to URL.
      */
      zNotFound = fossil_strdup(blob_str(&value));
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
    if( blob_eq(&key, "nossl") ){
      /* nossl
      **
      ** Signal that no SSL connections are available.
      */
      g.sslNotAvailable = 1;
      continue;
    }
    if( blob_eq(&key, "nocompress") ){
      /* nocompress
      **
      ** Do not compress HTTP replies.
      */
      g.fNoHttpCompress = 1;
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
      azRedirect[nRedirect*2-2] = fossil_strdup(blob_str(&value));
      azRedirect[nRedirect*2-1] = fossil_strdup(blob_str(&value2));
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
      char *zValue;
      blob_tail(&line,&value2);
      blob_trim(&value2);
      zValue = blob_str(&value2);
      while( fossil_isspace(zValue[0]) ){ zValue++; }
      fossil_setenv(blob_str(&value), zValue);
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
      g.zErrlog = fossil_strdup(blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "extroot:") && blob_token(&line, &value) ){
      /* extroot: DIRECTORY
      **
      ** Enables the /ext webpage to use sub-cgi rooted at DIRECTORY
      */
      g.zExtRoot = fossil_strdup(blob_str(&value));
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
    if( blob_eq(&key, "skin:") ){
      /* skin: LABEL
      **
      ** Use one of the built-in skins defined by LABEL.  LABEL is the
      ** name of the subdirectory under the skins/ directory that holds
      ** the elements of the built-in skin.  If LABEL does not match,
      ** this directive is a silent no-op. It may alternately be
      ** an absolute path to a directory which holds skin definition
      ** files (header.txt, footer.txt, etc.). If LABEL is empty,
      ** the skin stored in the CONFIG db table is used.
      */
      blob_token(&line, &value);
      fossil_free(skin_use_alternative(blob_str(&value), 1, SKIN_FROM_CGI));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "jsmode:") && blob_token(&line, &value) ){
      /* jsmode: MODE
      **
      ** Change how JavaScript resources are delivered with each HTML
      ** page.  MODE is "inline" to put all JS inline, or "separate" to
      ** cause each JS file to be requested using a separate HTTP request,
      ** or "bundled" to have all JS files to be fetched with a single
      ** auxiliary HTTP request. Noting, however, that "single" might
      ** actually mean more than one, depending on the script-timing
      ** requirements of any given page.
      */
      builtin_set_js_delivery_mode(blob_str(&value),0);
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "mainmenu:") && blob_token(&line, &value) ){
      /* mainmenu: FILENAME
      **
      ** Use the contents of FILENAME as the value of the site's
      ** "mainmenu" setting, overriding the contents (for this
      ** request) of the db-side setting or the hard-coded default.
      */
      g.zMainMenuFile = fossil_strdup(blob_str(&value));
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
      cgi_print_all(1,2,0);
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
      g.zRepositoryName = fossil_strdup(zRepo);
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
        hash_user_password(g.zLogin);
        cache_initialize();
        g.zLogin = 0;
        g.userUid = 0;
      }else{
        db_open_repository(zRepo);
      }
    }
  }
}

#if USE_SEE
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
  const char *zPidKey,   /* The value to be parsed. */
  PID_T *pProcessId,     /* The extracted process identifier. */
  LPVOID *ppAddress,     /* The extracted pointer value. */
  SIZE_T *pnSize         /* The extracted size value. */
){
  unsigned long processId = 0;
  unsigned int nSize = 0;
  if( sscanf(zPidKey, "%lu:%p:%u", &processId, ppAddress, &nSize)==3 ){
    *pProcessId = (PID_T)processId;
    *pnSize = (SIZE_T)nSize;
  }else{
    fossil_fatal("failed to parse pid key");
  }
}
#endif

/*
** WEBPAGE: test-pid
**
** Return the process identifier of the running Fossil server instance.
**
** Query parameters:
**
**   usepidkey           When present and available, also return the
**                       address and size, within this server process,
**                       of the saved database encryption key.  This
**                       is only supported when using SEE on Windows
**                       or Linux.
*/
void test_pid_page(void){
  login_check_credentials();
  if( !g.perm.Setup ){ login_needed(0); return; }
#if USE_SEE
  if( P("usepidkey")!=0 ){
    if( g.zPidKey ){
      @ %s(g.zPidKey)
      return;
    }else{
      const char *zSavedKey = db_get_saved_encryption_key();
      size_t savedKeySize = db_get_saved_encryption_key_size();
      if( zSavedKey!=0 && savedKeySize>0 ){
        @ %lu(GETPID()):%p(zSavedKey):%u(savedKeySize)
        return;
      }
    }
  }
#endif
  @ %d(GETPID())
}

/*
** Check for options to "fossil server" or "fossil ui" that imply that
** SSL should be used, and initialize the SSL decoder.
*/
static void decode_ssl_options(void){
#if FOSSIL_ENABLE_SSL
  const char *zCertFile = 0;
  const char *zKeyFile = 0;
  zCertFile = find_option("cert",0,1);
  zKeyFile = find_option("pkey",0,1);
  if( zCertFile ){
    g.httpUseSSL = 1;
    ssl_init_server(zCertFile, zKeyFile);
  }else if( zKeyFile ){
    fossil_fatal("--pkey without a corresponding --cert");
  }
#endif
}

/*
** COMMAND: http*
**
** Usage: %fossil http ?REPOSITORY? ?OPTIONS?
**
** Handle a single HTTP request appearing on stdin.  The resulting webpage
** is delivered on stdout.  This method is used to launch an HTTP request
** handler from inetd, for example.  The REPOSITORY argument is the name of
** the repository.
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
** Options:
**   --acme              Deliver files from the ".well-known" subdirectory
**   --baseurl URL       Base URL (useful with reverse proxies)
**   --cert FILE         Use TLS (HTTPS) encryption with the certificate (the
**                       fullchain.pem) taken from FILE.
**   --chroot DIR        Use directory for chroot instead of repository path.
**   --ckout-alias N     Treat URIs of the form /doc/N/... as if they were
**                          /doc/ckout/...
**   --extroot DIR       Document root for the /ext extension mechanism
**   --files GLOB        Comma-separate glob patterns for static file to serve
**   --host NAME         DNS Hostname of the server
**   --https             The HTTP request originated from https but has already
**                       been decoded by a reverse proxy.  Hence, URLs created
**                       by Fossil should use "https:" rather than "http:".
**   --in FILE           Take input from FILE instead of standard input
**   --ipaddr ADDR       Assume the request comes from the given IP address
**   --jsmode MODE       Determine how JavaScript is delivered with pages.
**                       Mode can be one of:
**                          inline       All JavaScript is inserted inline at
**                                       one or more points in the HTML file.
**                          separate     Separate HTTP requests are made for
**                                       each JavaScript file.
**                          bundled      Groups JavaScript files into one or
**                                       more bundled requests which
**                                       concatenate scripts together.
**                       Depending on the needs of any given page, inline
**                       and bundled modes might result in a single
**                       amalgamated script or several, but both approaches
**                       result in fewer HTTP requests than the separate mode.
**   --localauth         Connections from localhost are given "setup"
**                       privileges without having to log in
**   --mainmenu FILE     Override the mainmenu config setting with the contents
**                       of the given file
**   --nocompress        Do not compress HTTP replies
**   --nodelay           Omit backoffice processing if it would delay
**                       process exit
**   --nojail            Drop root privilege but do not enter the chroot jail
**   --nossl             Do not do http: to https: redirects, regardless of
**                       the redirect-to-https setting.
**   --notfound URL      Use URL as the "HTTP 404, object not found" page
**   --out FILE          Write the HTTP reply to FILE instead of to
**                       standard output
**   --pkey FILE         Read the private key used for TLS from FILE
**   --repolist          If REPOSITORY is directory, URL "/" lists all repos
**   --scgi              Interpret input as SCGI rather than HTTP
**   --skin LABEL        Use override skin LABEL. Use an empty string ("")
**                       to force use of the current local skin config.
**   --th-trace          Trace TH1 execution (for debugging purposes)
**   --usepidkey         Use saved encryption key from parent process. This is
**                       only necessary when using SEE on Windows or Linux.
**
** See also: [[cgi]], [[server]], [[winsrv]] [Windows only]
*/
void cmd_http(void){
  const char *zIpAddr = 0;
  const char *zNotFound;
  const char *zHost;
  const char *zAltBase;
  const char *zFileGlob;
  const char *zInFile;
  const char *zOutFile;
  const char *zChRoot;
  int useSCGI;
  int noJail;
  int allowRepoList;

  Th_InitTraceLog();
  builtin_set_js_delivery_mode(find_option("jsmode",0,1),0);

  /* The winhttp module passes the --files option as --files-urlenc with
  ** the argument being URL encoded, to avoid wildcard expansion in the
  ** shell.  This option is for internal use and is undocumented.
  */
  zFileGlob = find_option("files-urlenc",0,1);
  if( zFileGlob ){
    char *z = fossil_strdup(zFileGlob);
    dehttpize(z);
    zFileGlob = z;
  }else{
    zFileGlob = find_option("files",0,1);
  }
  skin_override();
  zNotFound = find_option("notfound", 0, 1);
  zChRoot = find_option("chroot",0,1);
  noJail = find_option("nojail",0,0)!=0;
  allowRepoList = find_option("repolist",0,0)!=0;
  g.useLocalauth = find_option("localauth", 0, 0)!=0;
  g.sslNotAvailable = find_option("nossl", 0, 0)!=0;
  g.fNoHttpCompress = find_option("nocompress",0,0)!=0;
  g.zExtRoot = find_option("extroot",0,1);
  g.zCkoutAlias = find_option("ckout-alias",0,1);
  g.zReqType = "HTTP";
  zInFile = find_option("in",0,1);
  if( zInFile ){
    backoffice_disable();
    g.httpIn = fossil_fopen(zInFile, "rb");
    if( g.httpIn==0 ) fossil_fatal("cannot open \"%s\" for reading", zInFile);
  }else{
    g.httpIn = stdin;
#if defined(_WIN32)
   _setmode(_fileno(stdin), _O_BINARY);
#endif
  }
  zOutFile = find_option("out",0,1);
  if( zOutFile ){
    g.httpOut = fossil_fopen(zOutFile, "wb");
    if( g.httpOut==0 ) fossil_fatal("cannot open \"%s\" for writing", zOutFile);
  }else{
    g.httpOut = stdout;
#if defined(_WIN32)
   _setmode(_fileno(stdout), _O_BINARY);
#endif
  }
  zIpAddr = find_option("ipaddr",0,1);
#if defined(_WIN32)
  /* The undocumented option "--as NAME" causes NAME to become
  ** the fake command name.  This only happens on Windows and only
  ** if preceded by --in, --out, and --ipaddr.  It is a work-around
  ** to get the original command-name down into the "http" command that
  ** is run in a subprocess to manage HTTP requests on Windows for
  ** commands like "fossil ui" and "fossil server".
  */
  if( zInFile && zOutFile && zIpAddr ){
    const char *z = find_option("as",0,1);
    if( z ) g.zCmdName = z;
  }
#endif
  useSCGI = find_option("scgi", 0, 0)!=0;
  if( useSCGI ) g.zReqType = "SCGI";
  zAltBase = find_option("baseurl", 0, 1);
  if( find_option("nodelay",0,0)!=0 ) backoffice_no_delay();
  if( zAltBase ) set_base_url(zAltBase);
  if( find_option("https",0,0)!=0 ){
    zIpAddr = fossil_getenv("REMOTE_HOST"); /* From stunnel */
    cgi_replace_parameter("HTTPS","on");
  }
  zHost = find_option("host", 0, 1);
  if( zHost ) cgi_replace_parameter("HTTP_HOST",zHost);
  g.zMainMenuFile = find_option("mainmenu",0,1);
  if( g.zMainMenuFile!=0 && file_size(g.zMainMenuFile,ExtFILE)<0 ){
    fossil_fatal("Cannot read --mainmenu file %s", g.zMainMenuFile);
  }
  decode_ssl_options();
  if( find_option("acme",0,0)!=0 ) g.fAllowACME = 1;

  /* We should be done with options.. */
  verify_all_options();
  if( g.httpUseSSL ){
    if( useSCGI ){
      fossil_fatal("SSL not (yet) supported for SCGI");
    }
    if( g.fSshClient & CGI_SSH_CLIENT ){
      fossil_fatal("SSL not compatible with SSH");
    }
    if( zInFile || zOutFile ){
      fossil_fatal("SSL usable only on a socket");
    }
    cgi_replace_parameter("HTTPS","on");
  }

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
  g.zRepositoryName = enter_chroot_jail(
      zChRoot ? zChRoot : g.zRepositoryName, noJail);
  if( useSCGI ){
    cgi_handle_scgi_request();
  }else if( g.fSshClient & CGI_SSH_CLIENT ){
    ssh_request_loop(zIpAddr, glob_create(zFileGlob));
  }else{
#if FOSSIL_ENABLE_SSL
    if( g.httpUseSSL ){
      g.httpSSLConn = ssl_new_server(0);
    }
#endif
    cgi_handle_http_request(zIpAddr);
  }
  process_one_web_page(zNotFound, glob_create(zFileGlob), allowRepoList);
#if FOSSIL_ENABLE_SSL
  if( g.httpUseSSL && g.httpSSLConn ){
    ssl_close_server(g.httpSSLConn);
    g.httpSSLConn = 0;
  }
#endif /* FOSSIL_ENABLE_SSL */
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
** COMMAND: test-http
**
** Works like the [[http]] command but gives setup permission to all users,
** or whatever permission is described by "--usercap CAP".
**
** This command can used for interactive debugging of web pages.  For
** example, one can put a simple HTTP request in a file like this:
**
**     echo 'GET /timeline' >request.txt
**
** Then run (in a debugger) a command like this:
**
**     fossil test-http <request.txt
**
** This command is also used internally by the "ssh" sync protocol.  Some
** special processing to support sync happens when this command is run
** and the SSH_CONNECTION environment variable is set.  Use the --test
** option on interactive sessions to avoid that special processing when
** using this command interactively over SSH.  A better solution would be
** to use a different command for "ssh" sync, but we cannot do that without
** breaking legacy.
**
** Options:
**   --csrf-safe N       Set cgi_csrf_safe() to to return N
**   --nobody            Pretend to be user "nobody"
**   --test              Do not do special "sync" processing when operating
**                       over an SSH link
**   --th-trace          Trace TH1 execution (for debugging purposes)
**   --usercap   CAP     User capability string (Default: "sxy")
*/
void cmd_test_http(void){
  const char *zIpAddr;    /* IP address of remote client */
  const char *zUserCap;
  int bTest = 0;
  const char *zCsrfSafe = find_option("csrf-safe",0,1);

  Th_InitTraceLog();
  if( zCsrfSafe ) g.okCsrf = atoi(zCsrfSafe);
  zUserCap = find_option("usercap",0,1);
  if( !find_option("nobody",0,0) ){
    if( zUserCap==0 ){
      g.useLocalauth = 1;
      zUserCap = "sxy";
    }
    login_set_capabilities(zUserCap, 0);
  }
  bTest = find_option("test",0,0)!=0;
  g.httpIn = stdin;
  g.httpOut = stdout;
  fossil_binary_mode(g.httpOut);
  fossil_binary_mode(g.httpIn);
  g.zExtRoot = find_option("extroot",0,1);
  find_server_repository(2, 0);
  g.zReqType = "HTTP";
  g.cgiOutput = 1;
  g.fNoHttpCompress = 1;
  g.fullHttpReply = 1;
  g.sslNotAvailable = 1;  /* Avoid attempts to redirect */
  zIpAddr = bTest ? 0 : cgi_ssh_remote_addr(0);
  if( zIpAddr && zIpAddr[0] ){
    g.fSshClient |= CGI_SSH_CLIENT;
    ssh_request_loop(zIpAddr, 0);
  }else{
    cgi_set_parameter("REMOTE_ADDR", "127.0.0.1");
    cgi_handle_http_request(0);
    process_one_web_page(0, 0, 1);
  }
}

/*
** Respond to a SIGALRM by writing a message to the error log (if there
** is one) and exiting.
*/
#ifndef _WIN32
static int nAlarmSeconds = 0;
static void sigalrm_handler(int x){
  sqlite3_uint64 tmUser = 0, tmKernel = 0;
  fossil_cpu_times(&tmUser, &tmKernel);
  if( fossil_strcmp(g.zPhase, "web-page reply")==0
   && tmUser+tmKernel<10000000
  ){
    /* Do not log time-outs during web-page reply unless more than
    ** 10 seconds of CPU time has been consumed */
    return;
  }
  fossil_panic("Timeout after %d seconds during %s"
               " - user %,llu µs, sys %,llu µs",
               nAlarmSeconds, g.zPhase, tmUser, tmKernel);
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
  nAlarmSeconds = N;
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
** --port option.  The optional REPOSITORY argument is the name of the
** Fossil repository to be served.  The REPOSITORY argument may be omitted
** if the working directory is within an open check-out, in which case the
** repository associated with that check-out is used.
**
** The "ui" command automatically starts a web browser after initializing
** the web server.  The "ui" command also binds to 127.0.0.1 and so will
** only process HTTP traffic from the local machine.
**
** If REPOSITORY is a directory name which is the root of a
** check-out, then use the repository associated with that check-out.
** This only works for the "fossil ui" command, not the "fossil server"
** command.
**
** If REPOSITORY begins with a "HOST:" or "USER@HOST:" prefix, then
** the command is run on the remote host specified and the results are
** tunneled back to the local machine via SSH.  This feature only works for
** the "fossil ui" command, not the "fossil server" command.  The name of the
** fossil executable on the remote host is specified by the --fossilcmd
** option, or if there is no --fossilcmd, it first tries "fossil" and if it
** is not found in the default $PATH set by SSH on the remote, it then adds
** "$HOME/bin:/usr/local/bin:/opt/homebrew/bin" to the PATH and tries again to
** run "fossil".
**
** REPOSITORY may also be a directory (aka folder) that contains one or
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
** For the special case REPOSITORY name of "/", the global configuration
** database is consulted for a list of all known repositories.  The --repolist
** option is implied by this special case.  The "fossil ui /" command is
** equivalent to "fossil all ui".  To see all repositories owned by "user"
** on machine "remote" via ssh, run "fossil ui user@remote:/".
**
** By default, the "ui" command provides full administrative access without
** having to log in.  This can be disabled by turning off the "localauth"
** setting.  Automatic login for the "server" command is available if the
** --localauth option is present and the "localauth" setting is off and the
** connection is from localhost.  The "ui" command also enables --repolist
** by default.
**
** Options:
**   --acme              Deliver files from the ".well-known" subdirectory
**   --baseurl URL       Use URL as the base (useful for reverse proxies)
**   --cert FILE         Use TLS (HTTPS) encryption with the certificate (the
**                       fullchain.pem) taken from FILE.
**   --chroot DIR        Use directory for chroot instead of repository path
**   --ckout-alias NAME  Treat URIs of the form /doc/NAME/... as if they were
**                       /doc/ckout/...
**   --create            Create a new REPOSITORY if it does not already exist
**   --errorlog FILE     Append HTTP error messages to FILE
**   --extpage FILE      Shortcut for "--extroot DIR --page ext/TAIL" where
**                       DIR is the directory holding FILE and TAIL is the
**                       filename at the end of FILE.  Only works for "ui".
**   --extroot DIR       Document root for the /ext extension mechanism
**   --files GLOBLIST    Comma-separated list of glob patterns for static files
**   --fossilcmd PATH    The pathname of the "fossil" executable on the remote
**                       system when REPOSITORY is remote.
**   --from PATH         Use PATH as the diff baseline for the /ckout page
**   --localauth         Enable automatic login for requests from localhost
**   --localhost         Listen on 127.0.0.1 only (always true for "ui")
**   --https             Indicates that the input is coming through a reverse
**                       proxy that has already translated HTTPS into HTTP.
**   --jsmode MODE       Determine how JavaScript is delivered with pages.
**                       Mode can be one of:
**                          inline       All JavaScript is inserted inline at
**                                       the end of the HTML file.
**                          separate     Separate HTTP requests are made for
**                                       each JavaScript file.
**                          bundled      One single separate HTTP fetches all
**                                       JavaScript concatenated together.
**                       Depending on the needs of any given page, inline
**                       and bundled modes might result in a single
**                       amalgamated script or several, but both approaches
**                       result in fewer HTTP requests than the separate mode.
**   --mainmenu FILE     Override the mainmenu config setting with the contents
**                       of the given file
**   --max-latency N     Do not let any single HTTP request run for more than N
**                       seconds (only works on unix)
**   -B|--nobrowser      Do not automatically launch a web-browser for the
**                       "fossil ui" command
**   --nocompress        Do not compress HTTP replies
**   --nojail            Drop root privileges but do not enter the chroot jail
**   --nossl             Do not force redirects to SSL even if the repository
**                       setting "redirect-to-https" requests it.  This is set
**                       by default for the "ui" command.
**   --notfound URL      Redirect to URL if a page is not found.
**   -p|--page PAGE      Start "ui" on PAGE.  ex: --page "timeline?y=ci"
**   --pkey FILE         Read the private key used for TLS from FILE
**   -P|--port [IP:]PORT  Listen on the given IP (optional) and port
**   --repolist          If REPOSITORY is dir, URL "/" lists repos
**   --scgi              Accept SCGI rather than HTTP
**   --skin LABEL        Use override skin LABEL, or the site's default skin if
**                       LABEL is an empty string.
**   --socket-mode MODE  File permissions to set for the unix socket created
**                       by the --socket-name option.
**   --socket-name NAME  Use a unix-domain socket called NAME instead of a
**                       TCP/IP socket.
**   --socket-owner USR  Try to set the owner of the unix socket to USR.
**                       USR can be of the form USER:GROUP to set both
**                       user and group.
**   --th-trace          Trace TH1 execution (for debugging purposes)
**   --usepidkey         Use saved encryption key from parent process.  This is
**                       only necessary when using SEE on Windows or Linux.
**
** See also: [[cgi]], [[http]], [[winsrv]] [Windows only]
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
  const char *zChRoot;      /* Use for chroot instead of repository path */
  int noJail;               /* Do not enter the chroot jail */
  const char *zTimeout = 0; /* Max runtime of any single HTTP request */
#endif
  int allowRepoList;         /* List repositories on URL "/" */
  const char *zAltBase;      /* Argument to the --baseurl option */
  const char *zFileGlob;     /* Static content must match this */
  char *zIpAddr = 0;         /* Bind to this IP address or UN socket */
  int fCreate = 0;           /* The --create flag */
  int fNoBrowser = 0;        /* Do not auto-launch web-browser */
  const char *zInitPage = 0; /* Start on this page.  --page option */
  int findServerArg = 2;     /* argv index for find_server_repository() */
  char *zRemote = 0;         /* Remote host on which to run "fossil ui" */
  const char *zJsMode;       /* The --jsmode parameter */
  const char *zFossilCmd =0; /* Name of "fossil" binary on remote system */
  const char *zFrom;         /* Value for --from */
  const char *zExtPage = 0;  /* Argument to --extpage */


#if USE_SEE
  db_setup_for_saved_encryption_key();
#endif

#if defined(_WIN32)
  const char *zStopperFile;    /* Name of file used to terminate server */
  zStopperFile = find_option("stopper", 0, 1);
#endif

  if( g.zErrlog==0 ){
    g.zErrlog = "-";
  }
  g.zExtRoot = find_option("extroot",0,1);
  zJsMode = find_option("jsmode",0,1);
  builtin_set_js_delivery_mode(zJsMode,0);
  zFileGlob = find_option("files-urlenc",0,1);
  if( zFileGlob ){
    char *z = fossil_strdup(zFileGlob);
    dehttpize(z);
    zFileGlob = z;
  }else{
    zFileGlob = find_option("files",0,1);
  }
  skin_override();
#if !defined(_WIN32)
  zChRoot = find_option("chroot",0,1);
  noJail = find_option("nojail",0,0)!=0;
  zTimeout = find_option("max-latency",0,1);
#endif
  g.useLocalauth = find_option("localauth", 0, 0)!=0;
  Th_InitTraceLog();
  zPort = find_option("port", "P", 1);
  isUiCmd = g.argv[1][0]=='u';
  if( isUiCmd ){
    zFrom = find_option("from", 0, 1);
    if( zFrom && zFrom==file_tail(zFrom) ){
      fossil_fatal("the argument to --from must be a pathname for"
                   " the \"ui\" command");
    }
    zExtPage = find_option("extpage",0,1);
    if( zExtPage ){
      char *zFullPath = file_canonical_name_dup(zExtPage);
      g.zExtRoot = file_dirname(zFullPath);
      zInitPage = mprintf("ext/%s",file_tail(zFullPath));
      fossil_free(zFullPath);
    }else{
      zInitPage = find_option("page", "p", 1);
      if( zInitPage && zInitPage[0]=='/' ) zInitPage++;
    }
    zFossilCmd = find_option("fossilcmd", 0, 1);
    if( zFrom && zInitPage==0 ){
      zInitPage = mprintf("ckout?exbase=%H", zFrom);
    }
  }
  zNotFound = find_option("notfound", 0, 1);
  allowRepoList = find_option("repolist",0,0)!=0;
  if( find_option("nocompress",0,0)!=0 ) g.fNoHttpCompress = 1;
  zAltBase = find_option("baseurl", 0, 1);
  fCreate = find_option("create",0,0)!=0;
  g.zReqType = "HTTP";
  if( find_option("scgi", 0, 0)!=0 ){
    g.zReqType = "SCGI";
    flags |= HTTP_SERVER_SCGI;
  }
  if( zAltBase ){
    set_base_url(zAltBase);
  }
  g.sslNotAvailable = find_option("nossl", 0, 0)!=0 || isUiCmd;
  fNoBrowser = find_option("nobrowser", "B", 0)!=0;
  decode_ssl_options();
  if( find_option("https",0,0)!=0 || g.httpUseSSL ){
    cgi_replace_parameter("HTTPS","on");
  }
  if( find_option("localhost", 0, 0)!=0 ){
    flags |= HTTP_SERVER_LOCALHOST;
  }
  g.zCkoutAlias = find_option("ckout-alias",0,1);
  g.zMainMenuFile = find_option("mainmenu",0,1);
  if( g.zMainMenuFile!=0 && file_size(g.zMainMenuFile,ExtFILE)<0 ){
    fossil_fatal("Cannot read --mainmenu file %s", g.zMainMenuFile);
  }
  if( find_option("acme",0,0)!=0 ) g.fAllowACME = 1;
  g.zSockMode = find_option("socket-mode",0,1);
  g.zSockName = find_option("socket-name",0,1);
  g.zSockOwner = find_option("socket-owner",0,1);
  if( g.zSockName ){
#if defined(_WIN32)
    fossil_fatal("unix sockets are not supported on Windows");
#endif
    if( zPort ){
      fossil_fatal("cannot specify a port number for a unix socket");
    }
    if( isUiCmd && !fNoBrowser ){
      fossil_fatal("cannot start a web-browser on a unix socket");
    }
    flags |= HTTP_SERVER_UNIXSOCKET;
  }

  /* Undocumented option:  --debug-nofork
  **
  ** This sets the HTTP_SERVER_NOFORK flag, which causes only the
  ** very first incoming TCP/IP connection to be processed.  Used for
  ** debugging, since debugging across a fork() can be tricky
  */
  if( find_option("debug-nofork",0,0)!=0 ){
    flags |= HTTP_SERVER_NOFORK;
#if !defined(_WIN32)
    /* Disable the timeout during debugging */
    zTimeout = "100000000";
#endif
  }
  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=2 && g.argc!=3 ) usage("?REPOSITORY?");
  if( g.httpUseSSL && (flags & HTTP_SERVER_SCGI)!=0 ){
    fossil_fatal("SCGI does not (yet) support TLS-encrypted connections");
  }
  if( isUiCmd && 3==g.argc && file_isdir(g.argv[2], ExtFILE)>0 ){
    /* If REPOSITORY arg is the root of a check-out,
    ** chdir to that check-out so that the current version
    ** gets highlighted in the timeline by default. */
    const char * zDir = g.argv[2];
    if(dir_has_ckout_db(zDir)){
      if(0!=file_chdir(zDir, 0)){
        fossil_fatal("Cannot chdir to %s", zDir);
      }
      findServerArg = g.argc;
      fCreate = 0;
      g.argv[2] = 0;
      --g.argc;
    }
  }
  if( isUiCmd && 3==g.argc
   && (zRemote = (char*)file_skip_userhost(g.argv[2]))!=0
  ){
    /* The REPOSITORY argument has a USER@HOST: or HOST: prefix */
    const char *zRepoTail = file_skip_userhost(g.argv[2]);
    unsigned x;
    int n;
    sqlite3_randomness(2,&x);
    zPort = mprintf("%d", 8100+(x%32000));
    n = (int)(zRepoTail - g.argv[2]) - 1;
    zRemote = mprintf("%.*s", n, g.argv[2]);
    g.argv[2] = (char*)zRepoTail;
  }
  if( isUiCmd ){
    flags |= HTTP_SERVER_LOCALHOST|HTTP_SERVER_REPOLIST;
    g.useLocalauth = 1;
    allowRepoList = 1;
  }
  if( !zRemote ){
    find_server_repository(findServerArg, fCreate);
  }
  if( zInitPage==0 ){
    zInitPage = "";
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
    if( iPort<=0 ) fossil_fatal("port number must be greater than zero");
  }else{
    iPort = db_get_int("http-port", 8080);
    mxPort = iPort+100;
  }
  if( isUiCmd && !fNoBrowser ){
    char *zBrowserArg;
    const char *zProtocol = g.httpUseSSL ? "https" : "http";
    db_open_config(0,0);
    zBrowser = fossil_web_browser();
    if( zIpAddr==0 ){
      zBrowserArg = mprintf("%s://localhost:%%d/%s", zProtocol, zInitPage);
    }else if( strchr(zIpAddr,':') ){
      zBrowserArg = mprintf("%s://[%s]:%%d/%s", zProtocol, zIpAddr, zInitPage);
    }else{
      zBrowserArg = mprintf("%s://%s:%%d/%s", zProtocol, zIpAddr, zInitPage);
    }
    zBrowserCmd = mprintf("%s %!$ &", zBrowser, zBrowserArg);
    fossil_free(zBrowserArg);
  }
  if( zRemote ){
    /* If a USER@HOST:REPO argument is supplied, then use SSH to run
    ** "fossil ui --nobrowser" on the remote system and to set up a
    ** tunnel from the local machine to the remote. */
    FILE *sshIn;
    Blob ssh;
    int bRunning = 0;    /* True when fossil starts up on the remote */
    int isRetry;         /* True if on the second attempt */
    char zLine[1000];

    blob_init(&ssh, 0, 0);
    for(isRetry=0; isRetry<2 && !bRunning; isRetry++){
      blob_reset(&ssh);
      transport_ssh_command(&ssh);
      blob_appendf(&ssh,
         " -t -L 127.0.0.1:%d:127.0.0.1:%d %!$",
         iPort, iPort, zRemote
      );
      if( zFossilCmd==0 ){
        if( ssh_needs_path_argument(zRemote,-1) ^ isRetry ){
          ssh_add_path_argument(&ssh);
        }
        blob_append_escaped_arg(&ssh, "fossil", 1);
      }else{
        blob_appendf(&ssh, " %$", zFossilCmd);
      }
      blob_appendf(&ssh, " ui --nobrowser --localauth --port 127.0.0.1:%d",
                   iPort);
      if( zNotFound ) blob_appendf(&ssh, " --notfound %!$", zNotFound);
      if( zFileGlob ) blob_appendf(&ssh, " --files-urlenc %T", zFileGlob);
      if( g.zCkoutAlias ) blob_appendf(&ssh," --ckout-alias %!$",g.zCkoutAlias);
      if( zExtPage ){
        if( !file_is_absolute_path(zExtPage) ){
          zExtPage = mprintf("%s/%s", g.argv[2], zExtPage);
        }
        blob_appendf(&ssh, " --extpage %$", zExtPage);
      }else if( g.zExtRoot ){
        blob_appendf(&ssh, " --extroot %$", g.zExtRoot);
      }
      if( skin_in_use() ) blob_appendf(&ssh, " --skin %s", skin_in_use());
      if( zJsMode ) blob_appendf(&ssh, " --jsmode %s", zJsMode);
      if( fCreate ) blob_appendf(&ssh, " --create");
      blob_appendf(&ssh, " %$", g.argv[2]);
      if( isRetry ){
        fossil_print("First attempt to run \"fossil\" on %s failed\n"
                     "Retry: ", zRemote);
      }
      fossil_print("%s\n", blob_str(&ssh));
      sshIn = popen(blob_str(&ssh), "r");
      if( sshIn==0 ){
        fossil_fatal("unable to %s", blob_str(&ssh));
      }
      while( fgets(zLine, sizeof(zLine), sshIn) ){
        fputs(zLine, stdout);
        fflush(stdout);
        if( !bRunning && sqlite3_strglob("*Listening for HTTP*",zLine)==0 ){
          bRunning = 1;
          if( isRetry ){
            ssh_needs_path_argument(zRemote,99);
          }
          db_close_config();
          if( zBrowserCmd ){
            char *zCmd = mprintf(zBrowserCmd/*works-like:"%d"*/,iPort);
            fossil_system(zCmd);
            fossil_free(zCmd);
            fossil_free(zBrowserCmd);
            zBrowserCmd = 0;
          }
        }
      }
      pclose(sshIn);
    }
    fossil_free(zBrowserCmd);
    return;
  }
  if( g.repositoryOpen ) flags |= HTTP_SERVER_HAD_REPOSITORY;
  if( g.localOpen ) flags |= HTTP_SERVER_HAD_CHECKOUT;
  db_close(1);
#if !defined(_WIN32)
  if( 1 ){
    /* Modern kernels suppress SIGTERM to PID 1 to prevent root from
    ** rebooting the system by nuking the init system.  The only way
    ** Fossil becomes that PID 1 is when it's running solo in a Linux
    ** container or similar, so we do want to exit immediately, to
    ** allow the container to shut down quickly.
    **
    ** This has to happen ahead of the other signal() calls below.
    ** They apply after the HTTP hit is handled, but this one needs
    ** to be registered while we're waiting for that to occur.
    **/
    signal(SIGTERM, fossil_exit);
    signal(SIGINT,  fossil_exit);
  }
#endif /* !WIN32 */

  /* Start up an HTTP server
  */
  fossil_setenv("SERVER_SOFTWARE", "fossil version " RELEASE_VERSION
                " " MANIFEST_VERSION " " MANIFEST_DATE);
#if !defined(_WIN32)
  /* Unix implementation */
  if( cgi_http_server(iPort, mxPort, zBrowserCmd, zIpAddr, flags) ){
    fossil_fatal("unable to listen on CGI socket");
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
  signal(SIGSEGV, sigsegv_handler);
  signal(SIGPIPE, sigpipe_handler);
  if( g.fAnyTrace ){
    fprintf(stderr, "/***** Subprocess %d *****/\n", getpid());
  }
  g.cgiOutput = 1;
  find_server_repository(2, 0);
  if( fossil_strcmp(g.zRepositoryName,"/")==0 ){
    allowRepoList = 1;
  }else{
    g.zRepositoryName = enter_chroot_jail(
        zChRoot ? zChRoot : g.zRepositoryName, noJail);
  }
  if( flags & HTTP_SERVER_SCGI ){
    cgi_handle_scgi_request();
  }else if( g.httpUseSSL ){
#if FOSSIL_ENABLE_SSL
    g.httpSSLConn = ssl_new_server(0);
#endif
    cgi_handle_http_request(0);
  }else{
    cgi_handle_http_request(0);
  }
  process_one_web_page(zNotFound, glob_create(zFileGlob), allowRepoList);
  if( g.fAnyTrace ){
    fprintf(stderr, "/***** Webpage finished in subprocess %d *****/\n",
            getpid());
  }
#if FOSSIL_ENABLE_SSL
  if( g.httpUseSSL && g.httpSSLConn ){
    ssl_close_server(g.httpSSLConn);
    g.httpSSLConn = 0;
  }
#endif /* FOSSIL_ENABLE_SSL */

#else /* WIN32 */
  /* Win32 implementation */
  if( fossil_strcmp(g.zRepositoryName,"/")==0 ){
    allowRepoList = 1;
  }
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
**     case=8           Simulate a timeout
**     case=9           Simulate a TH1 XSS vulnerability
**     case=10          Simulate a TH1 SQL-injection vulnerability
*/
void test_warning_page(void){
  int iCase = atoi(PD("case","0"));
  int i;
  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  style_set_current_feature("test");
  style_header("Warning Test Page");
  style_submenu_element("Error Log","%R/errorlog");
  @ <p>This page will generate various kinds of errors to test Fossil's
  @ reaction.  Depending on settings, a message might be written
  @ into the <a href="%R/errorlog">error log</a>.  Click on
  @ one of the following hyperlinks to generate a simulated error:
  for(i=1; i<=10; i++){
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
  @ <li value='7'> call webpage_error()
  if( iCase==7 ){
    cgi_reset_content();
    webpage_error("Case 7 from /test-warning");
  }
  @ <li value='8'> simulated timeout
  if( iCase==8 ){
    fossil_set_timeout(1);
    cgi_reset_content();
    sqlite3_sleep(1100);
  }
  @ <li value='9'> simulated TH1 XSS vulnerability
  @ <li value='10'> simulated TH1 SQL-injection vulnerability
  if( iCase==9 || iCase==10 ){
    const char *zR;
    int n, rc;
    static const char *zTH1[] = {
       /* case 9 */  "html [taint {<b>XSS</b>}]",
       /* case 10 */ "query [taint {SELECT 'SQL-injection' AS msg}] {\n"
                     "  html \"<b>[htmlize $msg]</b>\"\n"
                     "}"
    };
    rc = Th_Eval(g.interp, 0, zTH1[iCase==10], -1);
    zR = Th_GetResult(g.interp, &n);
    if( rc==TH_OK ){
      @ <pre class="th1result">%h(zR)</pre>
    }else{
      @ <pre class="th1error">%h(zR)</pre>
    }
  }
  @ </ol>
  @ <p>End of test</p>
  style_finish_page();
}
