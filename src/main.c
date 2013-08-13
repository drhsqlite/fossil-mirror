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
#include "config.h"
#include "main.h"
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h> /* atexit() */
#include "zlib.h"
#ifdef FOSSIL_ENABLE_SSL
#  include "openssl/opensslv.h"
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
  Tcl_Interp *interp;    /* The on-demand created Tcl interpreter. */
  char *setup;           /* The optional Tcl setup script. */
  void *xPreEval;        /* Optional, called before Tcl_Eval*(). */
  void *pPreContext;     /* Optional, provided to xPreEval(). */
  void *xPostEval;       /* Optional, called after Tcl_Eval*(). */
  void *pPostContext;    /* Optional, provided to xPostEval(). */
};
#endif

/*
** All global variables are in this structure.
*/
struct Global {
  int argc; char **argv;  /* Command-line arguments to the program */
  char *nameOfExe;        /* Full path of executable. */
  int isConst;            /* True if the output is unchanging */
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
  int fSystemTrace;       /* Trace calls to fossil_system(), --systemtrace */
  int fSshTrace;          /* Trace the SSH setup traffic */
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

  int urlIsFile;          /* True if a "file:" url */
  int urlIsHttps;         /* True if a "https:" url */
  int urlIsSsh;           /* True if an "ssh:" url */
  char *urlName;          /* Hostname for http: or filename for file: */
  char *urlHostname;      /* The HOST: parameter on http headers */
  char *urlProtocol;      /* "http" or "https" */
  int urlPort;            /* TCP port number for http: or https: */
  int urlDfltPort;        /* The default port for the given protocol */
  char *urlPath;          /* Pathname for http: */
  char *urlUser;          /* User id for http: */
  char *urlPasswd;        /* Password for http: */
  char *urlCanonical;     /* Canonical representation of the URL */
  char *urlProxyAuth;     /* Proxy-Authorizer: string */
  char *urlFossil;        /* The fossil query parameter on ssh: */
  char *urlShell;         /* The shell query parameter on ssh: */
  unsigned urlFlags;      /* Boolean flags controlling URL processing */

  const char *zLogin;     /* Login name.  "" if not logged in. */
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
#ifdef FOSSIL_ENABLE_JSON
  cson_value_free(g.json.gc.v);
  memset(&g.json, 0, sizeof(g.json));
#endif
  free(g.zErrMsg);
  if(g.db){
    db_close(0);
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
  FILE * zInFile;           /* input FILE */
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
  zInFile = (0==strcmp("-",zFileName))
    ? stdin
    : fossil_fopen(zFileName,"rb");
  if(!zInFile){
    fossil_panic("Cannot open -args file [%s]", zFileName);
  }else{
    blob_read_from_channel(&file, zInFile, -1);
    if(stdin != zInFile){
      fclose(zInFile);
    }
    zInFile = NULL;
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
  fossil_warning("%s: %s", sqlite_error_code_name(iCode), zErrmsg);
}

/*
** This procedure runs first.
*/
#if defined(_WIN32) && !defined(BROKEN_MINGW_CMDLINE)
int _dowildcard = -1; /* This turns on command-line globbing in MinGW-w64 */
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
  const char *zCmdName = "unknown";
  int idx;
  int rc;
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
    if( g.fSqlTrace ) g.fSqlStats = 1;
    g.fSqlPrint = find_option("sqlprint", 0, 0)!=0;
    g.fHttpTrace = find_option("httptrace", 0, 0)!=0;
    g.zLogin = find_option("user", "U", 1);
    g.zSSLIdentity = find_option("ssl-identity", 0, 1);
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
  rc = name_search(zCmdName, aCommand, count(aCommand), &idx);
  if( rc==1 ){
    fossil_fatal("%s: unknown command: %s\n"
                 "%s: use \"help\" for more information\n",
                   g.argv[0], zCmdName, g.argv[0]);
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
  aCommand[idx].xFunc();
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
  fossil_print("This is fossil version " RELEASE_VERSION " "
               MANIFEST_VERSION " " MANIFEST_DATE " UTC\n");
  if(!find_option("verbose","v",0)){
    return;
  }else{
    fossil_print("Compiled on %s %s using %s (%d-bit)\n",
                 __DATE__, __TIME__, COMPILER_NAME, sizeof(void*)*8);
    fossil_print("SQLite %s %.30s\n", SQLITE_VERSION, SQLITE_SOURCE_ID);
    fossil_print("zlib %s\n", ZLIB_VERSION);
#if defined(FOSSIL_ENABLE_SSL)
    fossil_print("SSL (%s)\n", OPENSSL_VERSION_TEXT);
#endif
#if defined(FOSSIL_ENABLE_TCL)
    fossil_print("TCL (Tcl %s)\n", TCL_PATCH_LEVEL);
#endif
#if defined(FOSSIL_ENABLE_TCL_STUBS)
    fossil_print("TCL_STUBS\n");
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

    @ <h1>Available pages:</h1>
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
void set_base_url(const char *zAltBase){
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
void process_one_web_page(const char *zNotFound, Glob *pFileGlob){
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
  if( g.zContentType && memcmp(g.zContentType, "application/x-fossil", 20)==0 ){
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
        if( g.zLogin==0 ) zUser = "nobody";
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
      cgi_set_parameter_nocopy("name", g.zExtra);
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
    aWebpage[idx].xFunc();
  }

  /* Return the result.
  */
  cgi_reply();
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
