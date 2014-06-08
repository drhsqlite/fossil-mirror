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
#include "main.h"
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h> /* atexit() */
#if defined(_WIN32)
#  include <windows.h>
#else
#  include <errno.h> /* errno global */
#endif
#include "zlib.h"
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

/*
** Number of elements in an array
*/
#define count(X)  (sizeof(X)/sizeof(X[0]))

/*
** Size of a UUID in characters
*/
#define UUID_SIZE 40

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
  char Write;            /* i: xfer inbound. checkin */
  char Read;             /* o: xfer outbound. checkout */
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
  void *library;         /* The Tcl library module handle. */
  void *xFindExecutable; /* See tcl_FindExecutableProc in th_tcl.c. */
  void *xCreateInterp;   /* See tcl_CreateInterpProc in th_tcl.c. */
  void *xDeleteInterp;   /* See tcl_DeleteInterpProc in th_tcl.c. */
  void *xFinalize;       /* See tcl_FinalizeProc in th_tcl.c. */
  Tcl_Interp *interp;    /* The on-demand created Tcl interpreter. */
  int useObjProc;        /* Non-zero if an objProc can be called directly. */
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
  int useAttach;          /* True if global_config is attached to repository */
  const char *zConfigDbName;/* Path of the config database. NULL if not open */
  sqlite3_int64 now;      /* Seconds since 1970 */
  int repositoryOpen;     /* True if the main repository database is open */
  char *zRepositoryName;  /* Name of the repository database */
  const char *zMainDbType;/* "configdb", "localdb", or "repository" */
  const char *zConfigDbType;  /* "configdb", "localdb", or "repository" */
  int localOpen;          /* True if the local database is open */
  char *zLocalRoot;       /* The directory holding the  local database */
  int minPrefix;          /* Number of digits needed for a distinct UUID */
  int fSqlTrace;          /* True if --sqltrace flag is present */
  int fSqlStats;          /* True if --sqltrace or --sqlstats are present */
  int fSqlPrint;          /* True if -sqlprint flag is present */
  int fQuiet;             /* True if -quiet flag is present */
  int fHttpTrace;         /* Trace outbound HTTP requests */
  char *zHttpAuth;        /* HTTP Authorization user:pass information */
  int fSystemTrace;       /* Trace calls to fossil_system(), --systemtrace */
  int fSshTrace;          /* Trace the SSH setup traffic */
  int fSshClient;         /* HTTP client flags for SSH client */
  char *zSshCmd;          /* SSH command string */
  int fNoSync;            /* Do not do an autosync ever.  --nosync */
  char *zPath;            /* Name of webpage being served */
  char *zExtra;           /* Extra path information past the webpage name */
  char *zBaseURL;         /* Full text of the URL being served */
  char *zTop;             /* Parent directory of zPath */
  const char *zContentType;  /* The content type of the input HTTP request */
  int iErrPriority;       /* Priority of current error message */
  char *zErrMsg;          /* Text of an error message */
  int sslNotAvailable;    /* SSL is not available.  Do not redirect to https: */
  Blob cgiIn;             /* Input to an xfer www method */
  int cgiOutput;          /* Write error and status messages to CGI */
  int xferPanic;          /* Write error messages in XFER protocol */
  int fullHttpReply;      /* True for full HTTP reply.  False for CGI reply */
  Th_Interp *interp;      /* The TH1 interpreter */
  char *th1Setup;         /* The TH1 post-creation setup script, if any */
  FILE *httpIn;           /* Accept HTTP input from here */
  FILE *httpOut;          /* Send HTTP output here */
  int xlinkClusterOnly;   /* Set when cloning.  Only process clusters */
  int fTimeFormat;        /* 1 for UTC.  2 for localtime.  0 not yet selected */
  int *aCommitFile;       /* Array of files to be committed */
  int markPrivate;        /* All new artifacts are private if true */
  int clockSkewSeen;      /* True if clocks on client and server out of sync */
  int wikiFlags;          /* Wiki conversion flags applied to %w and %W */
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

  /* Information used to populate the RCVFROM table */
  int rcvid;              /* The rcvid.  0 if not yet defined. */
  char *zIpAddr;          /* The remote IP address */
  char *zNonce;           /* The nonce used for login */

  /* permissions used by the server */
  struct FossilUserPerms perm;

#ifdef FOSSIL_ENABLE_TCL
  /* all Tcl related context necessary for integration */
  struct TclContext tcl;
#endif

  /* For defense against Cross-site Request Forgery attacks */
  char zCsrfToken[12];    /* Value of the anti-CSRF token */
  int okCsrf;             /* Anti-CSRF token is present and valid */

  int parseCnt[10];       /* Counts of artifacts parsed */
  FILE *fDebug;           /* Write debug information here, if the file exists */
  int fNoThHook;          /* Disable all TH1 command/webpage hooks */
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
    cson_value * authToken;    /* authentication token */
    char const * jsonp;        /* Name of JSONP function wrapper. */
    unsigned char dispatchDepth /* Tells JSON command dispatching
                                   which argument we are currently
                                   working on. For this purpose, arg#0
                                   is the "json" path/CLI arg.
                                */;
    struct {                   /* "garbage collector" */
      cson_value * v;
      cson_array * a;
    } gc;
    struct {                   /* JSON POST data. */
      cson_value * v;
      cson_array * a;
      int offset;              /* Tells us which PATH_INFO/CLI args
                                  part holds the "json" command, so
                                  that we can account for sub-repos
                                  and path prefixes.  This is handled
                                  differently for CLI and CGI modes.
                               */
      char const * commandStr  /*"command" request param.*/;
    } cmd;
    struct {                   /* JSON POST data. */
      cson_value * v;
      cson_object * o;
    } post;
    struct {                   /* GET/COOKIE params in JSON mode. */
      cson_value * v;
      cson_object * o;
    } param;
    struct {
      cson_value * v;
      cson_object * o;
    } reqPayload;              /* request payload object (if any) */
    cson_array * warnings;     /* response warnings */
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
** The table of web pages supported by this application is generated
** automatically by the "mkindex" program and written into a file
** named "page_index.h".  We include that file here to get access
** to the table.
*/
#include "page_index.h"

/*
** Search for a function whose name matches zName.  Write a pointer to
** that function into *pxFunc and return 0.  If no match is found,
** return 1.  If the command is ambiguous return 2;
**
** The NameMap structure and the tables we are searching against are
** defined in the page_index.h header file which is automatically
** generated by mkindex.c program.
*/
static int name_search(
  const char *zName,       /* The name we are looking for */
  const NameMap *aMap,     /* Search in this array */
  int nMap,                /* Number of slots in aMap[] */
  int *pIndex              /* OUT: The index in aMap[] of the match */
){
  int upr, lwr, cnt, m, i;
  int n = strlen(zName);
  lwr = 0;
  upr = nMap-1;
  while( lwr<=upr ){
    int mid, c;
    mid = (upr+lwr)/2;
    c = fossil_strcmp(zName, aMap[mid].zName);
    if( c==0 ){
      *pIndex = mid;
      return 0;
    }else if( c<0 ){
      upr = mid - 1;
    }else{
      lwr = mid + 1;
    }
  }
  for(m=cnt=0, i=upr-2; cnt<2 && i<=upr+3 && i<nMap; i++){
    if( i<0 ) continue;
    if( strncmp(zName, aMap[i].zName, n)==0 ){
      m = i;
      cnt++;
    }
  }
  if( cnt==1 ){
    *pIndex = m;
    return 0;
  }
  return 1+(cnt>1);
}

/*
** atexit() handler which frees up "some" of the resources
** used by fossil.
*/
static void fossil_atexit(void) {
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
  free(g.zErrMsg);
  if(g.db){
    db_close(0);
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
static void expand_args_option(int argc, void *argv){
  Blob file = empty_blob;   /* Content of the file */
  Blob line = empty_blob;   /* One line of the file */
  unsigned int nLine;       /* Number of lines in the file*/
  unsigned int i, j, k;     /* Loop counters */
  int n;                    /* Number of bytes in one line */
  char *z;                  /* General use string pointer */
  char **newArgv;           /* New expanded g.argv under construction */
  char const * zFileName;   /* input file name */
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
  for(i=0; i<g.argc; i++) g.argv[i] = fossil_filename_to_utf8(g.argv[i]);
#endif
#if defined(_WIN32)
  GetModuleFileNameW(NULL, buf, MAX_PATH);
  g.nameOfExe = fossil_filename_to_utf8(buf);
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
  inFile = (0==strcmp("-",zFileName))
    ? stdin
    : fossil_fopen(zFileName,"rb");
  if(!inFile){
    fossil_fatal("Cannot open -args file [%s]", zFileName);
  }else{
    blob_read_from_channel(&file, inFile, -1);
    if(stdin != inFile){
      fclose(inFile);
    }
    inFile = NULL;
  }
  blob_to_utf8_no_bom(&file, 1);
  z = blob_str(&file);
  for(k=0, nLine=1; z[k]; k++) if( z[k]=='\n' ) nLine++;
  newArgv = fossil_malloc( sizeof(char*)*(g.argc + nLine*2) );
  for(j=0; j<i; j++) newArgv[j] = g.argv[j];

  blob_rewind(&file);
  while( (n = blob_line(&file, &line))>0 ){
    if( n<1 ) continue
      /**
       ** Reminder: corner-case: a line with 1 byte and no newline.
       */;
    z = blob_buffer(&line);
    if('\n'==z[n-1]){
      z[n-1] = 0;
    }

    if((n>1) && ('\r'==z[n-2])){
      if(n==2) continue /*empty line*/;
      z[n-2] = 0;
    }
    if(!z[0]) continue;
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
** Return a name for an SQLite error code
*/
static const char *sqlite_error_code_name(int iCode){
  static char zCode[30];
  switch( iCode & 0xff ){
    case SQLITE_OK:         return "SQLITE_OK";
    case SQLITE_ERROR:      return "SQLITE_ERROR";
    case SQLITE_PERM:       return "SQLITE_PERM";
    case SQLITE_ABORT:      return "SQLITE_ABORT";
    case SQLITE_BUSY:       return "SQLITE_BUSY";
    case SQLITE_NOMEM:      return "SQLITE_NOMEM";
    case SQLITE_READONLY:   return "SQLITE_READONLY";
    case SQLITE_INTERRUPT:  return "SQLITE_INTERRUPT";
    case SQLITE_IOERR:      return "SQLITE_IOERR";
    case SQLITE_CORRUPT:    return "SQLITE_CORRUPT";
    case SQLITE_FULL:       return "SQLITE_FULL";
    case SQLITE_CANTOPEN:   return "SQLITE_CANTOPEN";
    case SQLITE_PROTOCOL:   return "SQLITE_PROTOCOL";
    case SQLITE_EMPTY:      return "SQLITE_EMPTY";
    case SQLITE_SCHEMA:     return "SQLITE_SCHEMA";
    case SQLITE_CONSTRAINT: return "SQLITE_CONSTRAINT";
    case SQLITE_MISMATCH:   return "SQLITE_MISMATCH";
    case SQLITE_MISUSE:     return "SQLITE_MISUSE";
    case SQLITE_NOLFS:      return "SQLITE_NOLFS";
    case SQLITE_FORMAT:     return "SQLITE_FORMAT";
    case SQLITE_RANGE:      return "SQLITE_RANGE";
    case SQLITE_NOTADB:     return "SQLITE_NOTADB";
    case SQLITE_WARNING:    return "SQLITE_WARNING";
    default: {
      sqlite3_snprintf(sizeof(zCode),zCode,"error code %d",iCode);
    }
  }
  return zCode;
}

/* Error logs from SQLite */
static void fossil_sqlite_log(void *notUsed, int iCode, const char *zErrmsg){
#ifdef __APPLE__
  /* Disable the file alias warning on apple products because Time Machine
  ** creates lots of aliases and the warning alarms people. */
  if( iCode==SQLITE_WARNING ) return;
#endif
  if( iCode==SQLITE_SCHEMA ) return;
  fossil_warning("%s: %s", sqlite_error_code_name(iCode), zErrmsg);
}

/*
** This procedure runs first.
*/
#if defined(_WIN32) && !defined(BROKEN_MINGW_CMDLINE)
int _dowildcard = -1; /* This turns on command-line globbing in MinGW-w64 */
int wmain(int argc, wchar_t **argv)
#else
#if defined(_WIN32)
int _CRT_glob = 0x0001; /* See MinGW bug #2062 */
#endif
int main(int argc, char **argv)
#endif
{
  const char *zCmdName = "unknown";
  int idx;
  int rc;
  if( sqlite3_libversion_number()<3008003 ){
    fossil_fatal("Unsuitable SQLite version %s, must be at least 3.8.3",
                 sqlite3_libversion());
  }
  sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
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
  }else if( g.argc<2 ){
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
    g.fQuiet = find_option("quiet", 0, 0)!=0;
    g.fSqlTrace = find_option("sqltrace", 0, 0)!=0;
    g.fSqlStats = find_option("sqlstats", 0, 0)!=0;
    g.fSystemTrace = find_option("systemtrace", 0, 0)!=0;
    g.fSshTrace = find_option("sshtrace", 0, 0)!=0;
    g.fSshClient = 0;
    g.zSshCmd = 0;
    if( g.fSqlTrace ) g.fSqlStats = 1;
    g.fSqlPrint = find_option("sqlprint", 0, 0)!=0;
    g.fHttpTrace = find_option("httptrace", 0, 0)!=0;
    g.fNoThHook = find_option("no-th-hook", 0, 0)!=0;
    g.zLogin = find_option("user", "U", 1);
    g.zHttpAuth = 0;
    g.zLogin = find_option("user", "U", 1);
    g.zSSLIdentity = find_option("ssl-identity", 0, 1);
    g.zErrlog = find_option("errorlog", 0, 1);
    if( find_option("utc",0,0) ) g.fTimeFormat = 1;
    if( find_option("localtime",0,0) ) g.fTimeFormat = 2;
    if( zChdir && file_chdir(zChdir, 0) ){
      fossil_fatal("unable to change directories to %s", zChdir);
    }
    if( find_option("help",0,0)!=0 ){
      /* --help anywhere on the command line is translated into
      ** "fossil help argv[1] argv[2]..." */
      int i;
      char **zNewArgv = fossil_malloc( sizeof(char*)*(g.argc+2) );
      for(i=1; i<g.argc; i++) zNewArgv[i+1] = g.argv[i];
      zNewArgv[i+1] = 0;
      zNewArgv[0] = g.argv[0];
      zNewArgv[1] = "help";
      g.argc++;
      g.argv = zNewArgv;
    }
    zCmdName = g.argv[1];
  }
#ifndef _WIN32
  if( !is_valid_fd(2) ) fossil_panic("file descriptor 2 not open");
  /* if( is_valid_fd(3) ) fossil_warning("file descriptor 3 is open"); */
#endif
  rc = name_search(zCmdName, aCommand, count(aCommand), &idx);
  if( rc==1 ){
    if( !g.isHTTP && !g.fNoThHook ){
      rc = Th_CommandHook(zCmdName, 0);
    }else{
      rc = TH_OK;
    }
    if( rc==TH_OK || rc==TH_RETURN || rc==TH_CONTINUE ){
      if( rc==TH_OK || rc==TH_RETURN ){
        fossil_fatal("%s: unknown command: %s\n"
                     "%s: use \"help\" for more information\n",
                     g.argv[0], zCmdName, g.argv[0]);
      }
      if( !g.isHTTP && !g.fNoThHook && (rc==TH_OK || rc==TH_CONTINUE) ){
        Th_CommandNotify(zCmdName, 0);
      }
    }
    fossil_exit(0);
  }else if( rc==2 ){
    int i, n;
    Blob couldbe;
    blob_zero(&couldbe);
    n = strlen(zCmdName);
    for(i=0; i<count(aCommand); i++){
      if( memcmp(zCmdName, aCommand[i].zName, n)==0 ){
        blob_appendf(&couldbe, " %s", aCommand[i].zName);
      }
    }
    fossil_print("%s: ambiguous command prefix: %s\n"
                 "%s: could be any of:%s\n"
                 "%s: use \"help\" for more information\n",
                 g.argv[0], zCmdName, g.argv[0], blob_str(&couldbe), g.argv[0]);
    fossil_exit(1);
  }
  atexit( fossil_atexit );
  /*
  ** The TH1 return codes from the hook will be handled as follows:
  **
  ** TH_OK: The xFunc() and the TH1 notification will both be executed.
  **
  ** TH_ERROR: The xFunc() and the TH1 notification will both be skipped.
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
    rc = Th_CommandHook(aCommand[idx].zName, aCommand[idx].cmdFlags);
  }else{
    rc = TH_OK;
  }
  if( rc==TH_OK || rc==TH_RETURN || rc==TH_CONTINUE ){
    if( rc==TH_OK || rc==TH_RETURN ){ aCommand[idx].xFunc(); }
    if( !g.isHTTP && !g.fNoThHook && (rc==TH_OK || rc==TH_CONTINUE) ){
      Th_CommandNotify(aCommand[idx].zName, aCommand[idx].cmdFlags);
    }
  }
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
** Look for a command-line option.  If present, return a pointer.
** Return NULL if missing.
**
** hasArg==0 means the option is a flag.  It is either present or not.
** hasArg==1 means the option has an argument.  Return a pointer to the
** argument.
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
        remove_from_argv(i, 1);
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

/*
** Verify that there are no unprocessed command-line options.  If
** Any remaining command-line argument begins with "-" print
** an error message and quit.
*/
void verify_all_options(void){
  int i;
  for(i=1; i<g.argc; i++){
    if( g.argv[i][0]=='-' ){
      fossil_fatal(
        "unrecognized command-line option, or missing argument: %s",
        g.argv[i]);
    }
  }
}

/*
** Print a list of words in multiple columns.
*/
static void multi_column_list(const char **azWord, int nWord){
  int i, j, len;
  int mxLen = 0;
  int nCol;
  int nRow;
  for(i=0; i<nWord; i++){
    len = strlen(azWord[i]);
    if( len>mxLen ) mxLen = len;
  }
  nCol = 80/(mxLen+2);
  if( nCol==0 ) nCol = 1;
  nRow = (nWord + nCol - 1)/nCol;
  for(i=0; i<nRow; i++){
    const char *zSpacer = "";
    for(j=i; j<nWord; j+=nRow){
      fossil_print("%s%-*s", zSpacer, mxLen, azWord[j]);
      zSpacer = "  ";
    }
    fossil_print("\n");
  }
}

/*
** List of commands starting with zPrefix, or all commands if zPrefix is NULL.
*/
static void command_list(const char *zPrefix, int cmdMask){
  int i, nCmd;
  int nPrefix = zPrefix ? strlen(zPrefix) : 0;
  const char *aCmd[count(aCommand)];
  for(i=nCmd=0; i<count(aCommand); i++){
    const char *z = aCommand[i].zName;
    if( (aCommand[i].cmdFlags & cmdMask)==0 ) continue;
    if( zPrefix && memcmp(zPrefix, z, nPrefix)!=0 ) continue;
    aCmd[nCmd++] = aCommand[i].zName;
  }
  multi_column_list(aCmd, nCmd);
}

/*
** COMMAND: test-list-webpage
**
** List all web pages
*/
void cmd_test_webpage_list(void){
  int i, nCmd;
  const char *aCmd[count(aCommand)];
  for(i=nCmd=0; i<count(aCommand); i++){
    if(0x08 & aCommand[i].cmdFlags){
      aCmd[nCmd++] = aWebpage[i].zName;
    }
  }
  assert(nCmd && "page list is empty?");
  multi_column_list(aCmd, nCmd);
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
  fossil_print("This is fossil version %s\n", get_version());
  if(!find_option("verbose","v",0)){
    return;
  }else{
#if defined(FOSSIL_ENABLE_TCL)
    int rc;
    const char *zRc;
#endif
    fossil_print("Compiled on %s %s using %s (%d-bit)\n",
                 __DATE__, __TIME__, COMPILER_NAME, sizeof(void*)*8);
    fossil_print("SQLite %s %.30s\n", sqlite3_libversion(), sqlite3_sourceid());
    fossil_print("Schema version %s\n", AUX_SCHEMA);
    fossil_print("zlib %s, loaded %s\n", ZLIB_VERSION, zlibVersion());
#if defined(FOSSIL_ENABLE_SSL)
    fossil_print("SSL (%s)\n", SSLeay_version(SSLEAY_VERSION));
#endif
#if defined(FOSSIL_ENABLE_TCL)
    Th_FossilInit(TH_INIT_DEFAULT | TH_INIT_FORCE_TCL);
    rc = Th_Eval(g.interp, 0, "tclInvoke info patchlevel", -1);
    zRc = Th_ReturnCodeName(rc, 0);
    fossil_print("TCL (Tcl %s, loaded %s: %s)\n",
      TCL_PATCH_LEVEL, zRc, Th_GetResult(g.interp, 0)
    );
#endif
#if defined(USE_TCL_STUBS)
    fossil_print("USE_TCL_STUBS\n");
#endif
#if defined(FOSSIL_ENABLE_TCL_STUBS)
    fossil_print("TCL_STUBS\n");
#endif
#if defined(FOSSIL_ENABLE_TCL_PRIVATE_STUBS)
    fossil_print("TCL_PRIVATE_STUBS\n");
#endif
#if defined(FOSSIL_ENABLE_JSON)
    fossil_print("JSON (API %s)\n", FOSSIL_JSON_API_VERSION);
#endif
  }
}


/*
** COMMAND: help
**
** Usage: %fossil help COMMAND
**    or: %fossil COMMAND -help
**
** Display information on how to use COMMAND.  To display a list of
** available commands one of:
**
**    %fossil help              Show common commands
**    %fossil help --a|-all     Show both common and auxiliary commands
**    %fossil help --t|-test    Show test commands only
**    %fossil help --x|-aux     Show auxiliary commands only
**    %fossil help --w|-www     Show list of WWW pages
*/
void help_cmd(void){
  int rc, idx, isPage = 0;
  const char *z;
  char const * zCmdOrPage;
  char const * zCmdOrPagePlural;
  if( g.argc<3 ){
    z = g.argv[0];
    fossil_print(
      "Usage: %s help COMMAND\n"
      "Common COMMANDs:  (use \"%s help -a|--all\" for a complete list)\n",
      z, z);
    command_list(0, CMDFLAG_1ST_TIER);
    version_cmd();
    return;
  }
  if( find_option("all","a",0) ){
    command_list(0, CMDFLAG_1ST_TIER | CMDFLAG_2ND_TIER);
    return;
  }
  else if( find_option("www","w",0) ){
    command_list(0, CMDFLAG_WEBPAGE);
    return;
  }
  else if( find_option("aux","x",0) ){
    command_list(0, CMDFLAG_2ND_TIER);
    return;
  }
  else if( find_option("test","t",0) ){
    command_list(0, CMDFLAG_TEST);
    return;
  }
  isPage = ('/' == *g.argv[2]) ? 1 : 0;
  if(isPage){
    zCmdOrPage = "page";
    zCmdOrPagePlural = "pages";
  }else{
    zCmdOrPage = "command";
    zCmdOrPagePlural = "commands";
  }
  rc = name_search(g.argv[2], aCommand, count(aCommand), &idx);
  if( rc==1 ){
    fossil_print("unknown %s: %s\nAvailable %s:\n",
                 zCmdOrPage, g.argv[2], zCmdOrPagePlural);
    command_list(0, isPage ? CMDFLAG_WEBPAGE : (0xff & ~CMDFLAG_WEBPAGE));
    fossil_exit(1);
  }else if( rc==2 ){
    fossil_print("ambiguous %s prefix: %s\nMatching %s:\n",
                 zCmdOrPage, g.argv[2], zCmdOrPagePlural);
    command_list(g.argv[2], 0xff);
    fossil_exit(1);
  }
  z = aCmdHelp[idx].zText;
  if( z==0 ){
    fossil_fatal("no help available for the %s %s",
                 aCommand[idx].zName, zCmdOrPage);
  }
  while( *z ){
    if( *z=='%' && strncmp(z, "%fossil", 7)==0 ){
      fossil_print("%s", g.argv[0]);
      z += 7;
    }else{
      putchar(*z);
      z++;
    }
  }
  putchar('\n');
}

/*
** WEBPAGE: help
** URL: /help/CMD
*/
void help_page(void){
  const char * zCmd = P("cmd");

  if( zCmd==0 ) zCmd = P("name");
  style_header("Command-line Help");
  if( zCmd ){
    int rc, idx;
    char *z, *s, *d;
    char const * zCmdOrPage = ('/'==*zCmd) ? "page" : "command";
    style_submenu_element("Command-List", "Command-List", "%s/help", g.zTop);
    @ <h1>The "%s(zCmd)" %s(zCmdOrPage):</h1>
    rc = name_search(zCmd, aCommand, count(aCommand), &idx);
    if( rc==1 ){
      @ unknown command: %s(zCmd)
    }else if( rc==2 ){
      @ ambiguous command prefix: %s(zCmd)
    }else{
      z = (char*)aCmdHelp[idx].zText;
      if( z==0 ){
        @ no help available for the %s(aCommand[idx].zName) command
      }else{
        z=s=d=mprintf("%s",z);
        while( *s ){
          if( *s=='%' && strncmp(s, "%fossil", 7)==0 ){
            s++;
          }else{
            *d++ = *s++;
          }
        }
        *d = 0;
        @ <blockquote><pre>
        @ %h(z)
        @ </pre></blockquote>
        fossil_free(z);
      }
    }
  }else{
    int i, j, n;

    @ <h1>Available commands:</h1>
    @ <table border="0"><tr>
    for(i=j=0; i<count(aCommand); i++){
      const char *z = aCommand[i].zName;
      if( '/'==*z || strncmp(z,"test",4)==0 ) continue;
      j++;
    }
    n = (j+6)/7;
    for(i=j=0; i<count(aCommand); i++){
      const char *z = aCommand[i].zName;
      if( '/'==*z || strncmp(z,"test",4)==0 ) continue;
      if( j==0 ){
        @ <td valign="top"><ul>
      }
      @ <li><a href="%s(g.zTop)/help?cmd=%s(z)">%s(z)</a></li>
      j++;
      if( j>=n ){
        @ </ul></td>
        j = 0;
      }
    }
    if( j>0 ){
      @ </ul></td>
    }
    @ </tr></table>

    @ <h1>Available web UI pages:</h1>
    @ (Only pages with help text are linked.)
    @ <table border="0"><tr>
    for(i=j=0; i<count(aCommand); i++){
      const char *z = aCommand[i].zName;
      if( '/'!=*z ) continue;
      j++;
    }
    n = (j+4)/5;
    for(i=j=0; i<count(aCommand); i++){
      const char *z = aCommand[i].zName;
      if( '/'!=*z ) continue;
      if( j==0 ){
        @ <td valign="top"><ul>
      }
      if( aCmdHelp[i].zText && *aCmdHelp[i].zText ){
        @ <li><a href="%s(g.zTop)/help?cmd=%s(z)">%s(z+1)</a></li>
      }else{
        @ <li>%s(z+1)</li>
      }
      j++;
      if( j>=n ){
        @ </ul></td>
        j = 0;
      }
    }
    if( j>0 ){
      @ </ul></td>
    }
    @ </tr></table>

    @ <h1>Unsupported commands:</h1>
    @ <table border="0"><tr>
    for(i=j=0; i<count(aCommand); i++){
      const char *z = aCommand[i].zName;
      if( strncmp(z,"test",4)!=0 ) continue;
      j++;
    }
    n = (j+3)/4;
    for(i=j=0; i<count(aCommand); i++){
      const char *z = aCommand[i].zName;
      if( strncmp(z,"test",4)!=0 ) continue;
      if( j==0 ){
        @ <td valign="top"><ul>
      }
      if( aCmdHelp[i].zText && *aCmdHelp[i].zText ){
        @ <li><a href="%s(g.zTop)/help?cmd=%s(z)">%s(z)</a></li>
      }else{
        @ <li>%s(z)</li>
      }
      j++;
      if( j>=n ){
        @ </ul></td>
        j = 0;
      }
    }
    if( j>0 ){
      @ </ul></td>
    }
    @ </tr></table>

  }
  style_footer();
}

/*
** WEBPAGE: test-all-help
**
** Show all help text on a single page.  Useful for proof-reading.
*/
void test_all_help_page(void){
  int i;
  style_header("Testpage: All Help Text");
  for(i=0; i<count(aCommand); i++){
    if( memcmp(aCommand[i].zName, "test", 4)==0 ) continue;
    @ <h2>%s(aCommand[i].zName):</h2>
    @ <blockquote><pre>
    @ %h(aCmdHelp[i].zText)
    @ </pre></blockquote>
  }
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
static void set_base_url(const char *zAltBase){
  int i;
  const char *zHost;
  const char *zMode;
  const char *zCur;

  if( g.zBaseURL!=0 ) return;
  if( zAltBase ){
    int i, n, c;
    g.zTop = g.zBaseURL = mprintf("%s", zAltBase);
    if( memcmp(g.zTop, "http://", 7)!=0 && memcmp(g.zTop,"https://",8)!=0 ){
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
    }else{
      g.zBaseURL = mprintf("http://%s%.*s", zHost, i, zCur);
      g.zTop = &g.zBaseURL[7+strlen(zHost)];
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
*/
static char *enter_chroot_jail(char *zRepo){
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
    if( file_isdir(zDir)==1 ){
      if( file_chdir(zDir, 1) ){
        fossil_fatal("unable to chroot into %s", zDir);
      }
      zRepo = "/";
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
    if( stat(zRepo, &sStat)!=0 ){
      fossil_fatal("cannot stat() repository: %s", zRepo);
    }
    i = setgid(sStat.st_gid);
    i = i || setuid(sStat.st_uid);
    if(i){
      fossil_fatal("setgid/uid() failed with errno %d", errno);
    }
    if( g.db==0 && file_isfile(zRepo) ){
      db_open_repository(zRepo);
    }
  }
#endif
  return zRepo;
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
** If the repository is not known, the a search is done through the
** file hierarchy rooted at g.zRepositoryName for a suitable repository
** with a name of $prefix.fossil, where $prefix is any prefix of PATH_INFO.
** Or, if an ordinary file named $prefix is found, and $prefix matches
** pFileGlob and $prefix does not match "*.fossil*" and the mimetype of
** $prefix can be determined from its suffix, then the file $prefix is
** returned as static text.
**
** If no suitable webpage is found, try to redirect to zNotFound.
*/
static void process_one_web_page(const char *zNotFound, Glob *pFileGlob){
  const char *zPathInfo;
  char *zPath = NULL;
  int idx;
  int i;

  /* If the repository has not been opened already, then find the
  ** repository based on the first element of PATH_INFO and open it.
  */
  zPathInfo = PD("PATH_INFO","");
  if( !g.repositoryOpen ){
    char *zRepo, *zToFree;
    const char *zOldScript = PD("SCRIPT_NAME", "");
    char *zNewScript;
    int j, k;
    i64 szFile;

    i = zPathInfo[0]!=0;
    while( 1 ){
      while( zPathInfo[i] && zPathInfo[i]!='/' ){ i++; }
      zRepo = zToFree = mprintf("%s%.*s.fossil",g.zRepositoryName,i,zPathInfo);

      /* To avoid mischief, make sure the repository basename contains no
      ** characters other than alphanumerics, "/", "_", "-", and ".", and
      ** that "-" never occurs immediately after a "/" and that "." is always
      ** surrounded by two alphanumerics.  Any character that does not
      ** satisfy these constraints is converted into "_".
      */
      szFile = 0;
      for(j=strlen(g.zRepositoryName)+1, k=0; zRepo[j] && k<i-1; j++, k++){
        char c = zRepo[j];
        if( fossil_isalnum(c) ) continue;
        if( c=='/' ) continue;
        if( c=='_' ) continue;
        if( c=='-' && zRepo[j-1]!='/' ) continue;
        if( c=='.' && fossil_isalnum(zRepo[j-1]) && fossil_isalnum(zRepo[j+1])){
          continue;
        }
        szFile = 1;
        break;
      }
      if( szFile==0 ){
        if( zRepo[0]=='/' && zRepo[1]=='/' ){ zRepo++; j--; }
        szFile = file_size(zRepo);
      }
      if( szFile<0 ){
        const char *zMimetype;
        assert( fossil_strcmp(&zRepo[j], ".fossil")==0 );
        zRepo[j] = 0;
        if( zPathInfo[i]=='/' && file_isdir(zRepo)==1 ){
          fossil_free(zToFree);
          i++;
          continue;
        }
        if( pFileGlob!=0
         && file_isfile(zRepo)
         && glob_match(pFileGlob, zRepo)
         && strglob("*.fossil*",zRepo)==0
         && (zMimetype = mimetype_from_name(zRepo))!=0
         && strcmp(zMimetype, "application/x-fossil-artifact")!=0
        ){
          Blob content;
          blob_read_from_file(&content, zRepo);
          cgi_set_content_type(zMimetype);
          cgi_set_content(&content);
          cgi_reply();
          return;
        }
        zRepo[j] = '.';
      }

      if( szFile<1024 ){
        set_base_url(0);
        if( zNotFound ){
          cgi_redirect(zNotFound);
        }else{
#ifdef FOSSIL_ENABLE_JSON
          if(g.json.isJsonMode){
            json_err(FSL_JSON_E_RESOURCE_NOT_FOUND,NULL,1);
            return;
          }
#endif
          @ <h1>Not Found</h1>
          cgi_set_status(404, "not found");
          cgi_reply();
        }
        return;
      }
      break;
    }
    zNewScript = mprintf("%s%.*s", zOldScript, i, zPathInfo);
    cgi_replace_parameter("PATH_INFO", &zPathInfo[i+1]);
    zPathInfo += i;
    cgi_replace_parameter("SCRIPT_NAME", zNewScript);
    db_open_repository(zRepo);
    if( g.fHttpTrace ){
      fprintf(stderr,
          "# repository: [%s]\n"
          "# new PATH_INFO = [%s]\n"
          "# new SCRIPT_NAME = [%s]\n",
          zRepo, zPathInfo, zNewScript);
    }
  }

  /* Find the page that the user has requested, construct and deliver that
  ** page.
  */
  if( g.zContentType &&
      strncmp(g.zContentType, "application/x-fossil", 20)==0 ){
    zPathInfo = "/xfer";
  }
  set_base_url(0);
  if( zPathInfo==0 || zPathInfo[0]==0
      || (zPathInfo[0]=='/' && zPathInfo[1]==0) ){
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
    char *zAltRepo = 0;
    g.zPath = &zPath[1];
    for(i=1; zPath[i] && zPath[i]!='/'; i++){}
    if( zPath[i]=='/' ){
      zPath[i] = 0;
      g.zExtra = &zPath[i+1];

      /* Look for sub-repositories.  A sub-repository is another repository
      ** that accepts the login credentials of the current repository.  A
      ** subrepository is identified by a CONFIG table entry "subrepo:NAME"
      ** where NAME is the first component of the path.  The value of the
      ** the CONFIG entries is the string "USER:FILENAME" where USER is the
      ** USER name to log in as in the subrepository and FILENAME is the
      ** repository filename.
      */
      zAltRepo = db_text(0, "SELECT value FROM config WHERE name='subrepo:%q'",
                         g.zPath);
      if( zAltRepo ){
        int nHost;
        int jj;
        char *zUser = zAltRepo;
        login_check_credentials();
        for(jj=0; zAltRepo[jj] && zAltRepo[jj]!=':'; jj++){}
        if( zAltRepo[jj]==':' ){
          zAltRepo[jj] = 0;
          zAltRepo += jj+1;
        }else{
          zUser = "nobody";
        }
        if( g.zLogin==0 || g.zLogin[0]==0 ) zUser = "nobody";
        if( zAltRepo[0]!='/' ){
          zAltRepo = mprintf("%s/../%s", g.zRepositoryName, zAltRepo);
          file_simplify_name(zAltRepo, -1, 0);
        }
        db_close(1);
        db_open_repository(zAltRepo);
        login_as_user(zUser);
        g.perm.Password = 0;
        zPath += i;
        nHost = g.zTop - g.zBaseURL;
        g.zBaseURL = mprintf("%z/%s", g.zBaseURL, g.zPath);
        g.zTop = g.zBaseURL + nHost;
        continue;
      }
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
  if( name_search(g.zPath, aWebpage, count(aWebpage), &idx) &&
      name_search("not_found", aWebpage, count(aWebpage), &idx) ){
#ifdef FOSSIL_ENABLE_JSON
    if(g.json.isJsonMode){
      json_err(FSL_JSON_E_RESOURCE_NOT_FOUND,NULL,0);
    }else
#endif
    {
      cgi_set_status(404,"Not Found");
      @ <h1>Not Found</h1>
      @ <p>Page not found: %h(g.zPath)</p>
    }
  }else if( aWebpage[idx].xFunc!=page_xfer && db_schema_is_outofdate() ){
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
    /*
    ** The TH1 return codes from the hook will be handled as follows:
    **
    ** TH_OK: The xFunc() and the TH1 notification will both be executed.
    **
    ** TH_ERROR: The xFunc() and the TH1 notification will both be skipped.
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
    if( !g.isHTTP && !g.fNoThHook ){
      rc = Th_WebpageHook(aWebpage[idx].zName, aWebpage[idx].cmdFlags);
    }else{
      rc = TH_OK;
    }
    if( rc==TH_OK || rc==TH_RETURN || rc==TH_CONTINUE ){
      if( rc==TH_OK || rc==TH_RETURN ){ aWebpage[idx].xFunc(); }
      if( !g.isHTTP && !g.fNoThHook && (rc==TH_OK || rc==TH_CONTINUE) ){
        Th_WebpageNotify(aWebpage[idx].zName, aWebpage[idx].cmdFlags);
      }
    }
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
** that matches the "name" CGI parameter and for the first match,
** redirect to the corresponding URL with the "name" CGI parameter
** inserted.  Paint an error page if no match is found.
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
      if( db_exists("SELECT 1 FROM blob WHERE uuid GLOB '%s*'", zName) ){
        cgi_redirectf(azRedirect[i*2+1], zName);
        return;
      }
      db_close(1);
    }
  }
  if( zNotFound ){
    cgi_redirectf(zNotFound, zName);
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
** Usage: %fossil ?cgi? SCRIPT
**
** The SCRIPT argument is the name of a file that is the CGI script
** that is being run.  The command name, "cgi", may be omitted if
** the GATEWAY_INTERFACE environment variable is set to "CGI" (which
** should always be the case for CGI scripts run by a webserver.)  The
** SCRIPT file should look something like this:
**
**      #!/usr/bin/fossil
**      repository: /home/somebody/project.db
**
** The second line defines the name of the repository.  After locating
** the repository, fossil will generate a webpage on stdout based on
** the values of standard CGI environment variables.
**
** See also: http, server, winsrv
*/
void cmd_cgi(void){
  const char *zFile;
  const char *zNotFound = 0;
  char **azRedirect = 0;             /* List of repositories to redirect to */
  int nRedirect = 0;                 /* Number of entries in azRedirect */
  Glob *pFileGlob = 0;               /* Pattern for files */
  Blob config, line, key, value, value2;
  if( g.argc==3 && fossil_strcmp(g.argv[1],"cgi")==0 ){
    zFile = g.argv[2];
  }else{
    zFile = g.argv[1];
  }
  g.httpOut = stdout;
  g.httpIn = stdin;
  fossil_binary_mode(g.httpOut);
  fossil_binary_mode(g.httpIn);
  g.cgiOutput = 1;
  blob_read_from_file(&config, zFile);
  while( blob_line(&config, &line) ){
    if( !blob_token(&line, &key) ) continue;
    if( blob_buffer(&key)[0]=='#' ) continue;
    if( blob_eq(&key, "debug:") && blob_token(&line, &value) ){
      g.fDebug = fossil_fopen(blob_str(&value), "ab");
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "errorlog:") && blob_token(&line, &value) ){
      g.zErrlog = mprintf("%s", blob_str(&value));
      continue;
    }
    if( blob_eq(&key, "HOME:") && blob_token(&line, &value) ){
      cgi_setenv("HOME", blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "repository:") && blob_tail(&line, &value) ){
      blob_trim(&value);
      db_open_repository(blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "directory:") && blob_token(&line, &value) ){
      db_close(1);
      g.zRepositoryName = mprintf("%s", blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "notfound:") && blob_token(&line, &value) ){
      zNotFound = mprintf("%s", blob_str(&value));
      blob_reset(&value);
      continue;
    }
    if( blob_eq(&key, "localauth") ){
      g.useLocalauth = 1;
      continue;
    }
    if( blob_eq(&key, "redirect:") && blob_token(&line, &value)
            && blob_token(&line, &value2) ){
      nRedirect++;
      azRedirect = fossil_realloc(azRedirect, 2*nRedirect*sizeof(char*));
      azRedirect[nRedirect*2-2] = mprintf("%s", blob_str(&value));
      azRedirect[nRedirect*2-1] = mprintf("%s", blob_str(&value2));
      blob_reset(&value);
      blob_reset(&value2);
      continue;
    }
    if( blob_eq(&key, "files:") && blob_token(&line, &value) ){
      pFileGlob = glob_create(blob_str(&value));
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
    process_one_web_page(zNotFound, pFileGlob);
  }
}

/*
** If g.argv[2] exists then it is either the name of a repository
** that will be used by a server, or else it is a directory that
** contains multiple repositories that can be served.  If g.argv[2]
** is a directory, the repositories it contains must be named
** "*.fossil".  If g.argv[2] does not exists, then we must be within
** a check-out and the repository to be served is the repository of
** that check-out.
**
** Open the repository to be served if it is known.  If g.argv[2] is
** a directory full of repositories, then set g.zRepositoryName to
** the name of that directory and the specific repository will be
** opened later by process_one_web_page() based on the content of
** the PATH_INFO variable.
**
** If disallowDir is set, then the directory full of repositories method
** is disallowed.
*/
static void find_server_repository(int disallowDir){
  if( g.argc<3 ){
    db_must_be_within_tree();
  }else if( file_isdir(g.argv[2])==1 ){
    if( disallowDir ){
      fossil_fatal("\"%s\" is a directory, not a repository file", g.argv[2]);
    }else{
      g.zRepositoryName = mprintf("%s", g.argv[2]);
      file_simplify_name(g.zRepositoryName, -1, 0);
    }
  }else{
    db_open_repository(g.argv[2]);
  }
}

/*
** undocumented format:
**
**        fossil http REPOSITORY INFILE OUTFILE IPADDR
**
** The argv==6 form is used by the win32 server only.
**
** COMMAND: http*
**
** Usage: %fossil http REPOSITORY ?OPTIONS?
**
** Handle a single HTTP request appearing on stdin.  The resulting webpage
** is delivered on stdout.  This method is used to launch an HTTP request
** handler from inetd, for example.  The argument is the name of the
** repository.
**
** If REPOSITORY is a directory that contains one or more repositories,
** either directly in REPOSITORY itself, or in subdirectories, and
** with names of the form "*.fossil" then the a prefix of the URL pathname
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
**   --localauth      enable automatic login for local connections
**   --host NAME      specify hostname of the server
**   --https          signal a request coming in via https
**   --nossl          signal that no SSL connections are available
**   --notfound URL   use URL as "HTTP 404, object not found" page.
**   --files GLOB     comma-separate glob patterns for static file to serve
**   --baseurl URL    base URL (useful with reverse proxies)
**   --scgi           Interpret input as SCGI rather than HTTP
**
** See also: cgi, server, winsrv
*/
void cmd_http(void){
  const char *zIpAddr;
  const char *zNotFound;
  const char *zHost;
  const char *zAltBase;
  const char *zFileGlob;
  int useSCGI;

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
  zNotFound = find_option("notfound", 0, 1);
  g.useLocalauth = find_option("localauth", 0, 0)!=0;
  g.sslNotAvailable = find_option("nossl", 0, 0)!=0;
  useSCGI = find_option("scgi", 0, 0)!=0;
  zAltBase = find_option("baseurl", 0, 1);
  if( zAltBase ) set_base_url(zAltBase);
  if( find_option("https",0,0)!=0 ) cgi_replace_parameter("HTTPS","on");
  zHost = find_option("host", 0, 1);
  if( zHost ) cgi_replace_parameter("HTTP_HOST",zHost);
  g.cgiOutput = 1;
  if( g.argc!=2 && g.argc!=3 && g.argc!=6 ){
    fossil_fatal("no repository specified");
  }
  g.fullHttpReply = 1;
  if( g.argc==6 ){
    g.httpIn = fossil_fopen(g.argv[3], "rb");
    g.httpOut = fossil_fopen(g.argv[4], "wb");
    zIpAddr = g.argv[5];
  }else{
    g.httpIn = stdin;
    g.httpOut = stdout;
    zIpAddr = 0;
  }
  if( zIpAddr==0 ){
    zIpAddr = cgi_ssh_remote_addr(0);
    if( zIpAddr && zIpAddr[0] ){
      g.fSshClient |= CGI_SSH_CLIENT;
    }
  }
  find_server_repository(0);
  g.zRepositoryName = enter_chroot_jail(g.zRepositoryName);
  if( useSCGI ){
    cgi_handle_scgi_request();
  }else if( g.fSshClient & CGI_SSH_CLIENT ){
    ssh_request_loop(zIpAddr, glob_create(zFileGlob));
  }else{
    cgi_handle_http_request(zIpAddr);
  }
  process_one_web_page(zNotFound, glob_create(zFileGlob));
}

/*
** Process all requests in a single SSH connection if possible.
*/
void ssh_request_loop(const char *zIpAddr, Glob *FileGlob){
  blob_zero(&g.cgiIn);
  do{
    cgi_handle_ssh_http_request(zIpAddr);
    process_one_web_page(0, FileGlob);
    blob_reset(&g.cgiIn);
  } while ( g.fSshClient & CGI_SSH_FOSSIL ||
          g.fSshClient & CGI_SSH_COMPAT );
}

/*
** Note that the following command is used by ssh:// processing.
**
** COMMAND: test-http
** Works like the http command but gives setup permission to all users.
**
*/
void cmd_test_http(void){
  const char *zIpAddr;    /* IP address of remote client */

  Th_InitTraceLog();
  login_set_capabilities("sx", 0);
  g.useLocalauth = 1;
  g.httpIn = stdin;
  g.httpOut = stdout;
  find_server_repository(0);
  g.cgiOutput = 1;
  g.fullHttpReply = 1;
  zIpAddr = cgi_ssh_remote_addr(0);
  if( zIpAddr && zIpAddr[0] ){
    g.fSshClient |= CGI_SSH_CLIENT;
    ssh_request_loop(zIpAddr, 0);
  }else{
    cgi_set_parameter("REMOTE_ADDR", "127.0.0.1");
    cgi_handle_http_request(0);
    process_one_web_page(0, 0);
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
** COMMAND: server*
** COMMAND: ui
**
** Usage: %fossil server ?OPTIONS? ?REPOSITORY?
**    Or: %fossil ui ?OPTIONS? ?REPOSITORY?
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
** more repositories with names ending in ".fossil".  In this case, the
** a prefix of the URL pathname is used to search the directory for an
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
** By default, the "ui" command provides full administrative access without
** having to log in.  This can be disabled by setting turning off the
** "localauth" setting.  Automatic login for the "server" command is available
** if the --localauth option is present and the "localauth" setting is off
** and the connection is from localhost.  The optional REPOSITORY argument
** to "ui" may be a directory and will function as "server" if and only if
** the --notfound option is used.
**
** Options:
**   --localauth         enable automatic login for requests from localhost
**   --localhost         listen on 127.0.0.1 only (always true for "ui")
**   -P|--port TCPPORT   listen to request on port TCPPORT
**   --th-trace          trace TH1 execution (for debugging purposes)
**   --baseurl URL       Use URL as the base (useful for reverse proxies)
**   --notfound URL      Redirect
**   --files GLOBLIST    Comma-separated list of glob patterns for static files
**   --scgi              Accept SCGI rather than HTTP
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
  const char *zAltBase;     /* Argument to the --baseurl option */
  const char *zFileGlob;    /* Static content must match this */
  char *zIpAddr = 0;        /* Bind to this IP address */

#if defined(_WIN32)
  const char *zStopperFile;    /* Name of file used to terminate server */
  zStopperFile = find_option("stopper", 0, 1);
#endif

  zFileGlob = find_option("files", 0, 1);
  g.useLocalauth = find_option("localauth", 0, 0)!=0;
  Th_InitTraceLog();
  zPort = find_option("port", "P", 1);
  zNotFound = find_option("notfound", 0, 1);
  zAltBase = find_option("baseurl", 0, 1);
  if( find_option("scgi", 0, 0)!=0 ) flags |= HTTP_SERVER_SCGI;
  if( zAltBase ){
    set_base_url(zAltBase);
  }
  if ( find_option("localhost", 0, 0)!=0 ){
    flags |= HTTP_SERVER_LOCALHOST;
  }
  if( g.argc!=2 && g.argc!=3 ) usage("?REPOSITORY?");
  isUiCmd = g.argv[1][0]=='u';
  if( isUiCmd ){
    flags |= HTTP_SERVER_LOCALHOST;
    g.useLocalauth = 1;
  }
  find_server_repository(isUiCmd && zNotFound==0);
  if( zPort ){
    int i;
    for(i=strlen(zPort)-1; i>=0 && zPort[i]!=':'; i--){}
    if( i>0 ){
      zIpAddr = mprintf("%.*s", i, zPort);
      zPort += i+1;
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
      for(i=0; i<sizeof(azBrowserProg)/sizeof(azBrowserProg[0]); i++){
        if( binaryOnPath(azBrowserProg[i]) ){
          zBrowser = azBrowserProg[i];
          break;
        }
      }
    }
#else
    zBrowser = db_get("web-browser", "open");
#endif
    if( zIpAddr ){
      zBrowserCmd = mprintf("%s http://%s:%%d/ &", zBrowser, zIpAddr);
    }else{
      zBrowserCmd = mprintf("%s http://localhost:%%d/ &", zBrowser);
    }
  }
  db_close(1);
  if( cgi_http_server(iPort, mxPort, zBrowserCmd, zIpAddr, flags) ){
    fossil_fatal("unable to listen on TCP socket %d", iPort);
  }
  g.sslNotAvailable = 1;
  g.httpIn = stdin;
  g.httpOut = stdout;
  if( g.fHttpTrace || g.fSqlTrace ){
    fprintf(stderr, "====== SERVER pid %d =======\n", getpid());
  }
  g.cgiOutput = 1;
  find_server_repository(isUiCmd && zNotFound==0);
  g.zRepositoryName = enter_chroot_jail(g.zRepositoryName);
  if( flags & HTTP_SERVER_SCGI ){
    cgi_handle_scgi_request();
  }else{
    cgi_handle_http_request(0);
  }
  process_one_web_page(zNotFound, glob_create(zFileGlob));
#else
  /* Win32 implementation */
  if( isUiCmd ){
    zBrowser = db_get("web-browser", "start");
    if( zIpAddr ){
      zBrowserCmd = mprintf("%s http://%s:%%d/ &", zBrowser, zIpAddr);
    }else{
      zBrowserCmd = mprintf("%s http://localhost:%%d/ &", zBrowser);
    }
  }
  db_close(1);
  if( win32_http_service(iPort, zNotFound, zFileGlob, flags) ){
    win32_http_server(iPort, mxPort, zBrowserCmd,
                      zStopperFile, zNotFound, zFileGlob, zIpAddr, flags);
  }
#endif
}

/*
** COMMAND:  test-echo
**
** Usage:  %fossil test-echo [--hex] ARGS...
**
** Echo all command-line arguments (enclosed in [...]) to the screen so that
** wildcard expansion behavior of the host shell can be investigated.
**
** With the --hex option, show the output as hexadecimal.  This can be used
** to verify the fossil_filename_to_utf8() routine on Windows and Mac.
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
