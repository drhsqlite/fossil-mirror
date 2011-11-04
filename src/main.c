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
#include <sys/types.h>
#include <sys/stat.h>


#if INTERFACE

#ifdef FOSSIL_ENABLE_TCL
#include "tcl.h"
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
  char History;          /* h: access historical information. */
  char Clone;            /* g: clone */
  char RdWiki;           /* j: view wiki via web */
  char NewWiki;          /* f: create new wiki via web */
  char ApndWiki;         /* m: append to wiki via web */
  char WrWiki;           /* k: edit wiki via web */
  char RdTkt;            /* r: view tickets via web */
  char NewTkt;           /* n: create new tickets */
  char ApndTkt;          /* c: append to tickets via the web */
  char WrTkt;            /* w: make changes to tickets via web */
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
  int argc;
  char **argv;
  Tcl_Interp *interp;
};
#endif

/*
** All global variables are in this structure.
*/
struct Global {
  int argc; char **argv;  /* Command-line arguments to the program */
  int isConst;            /* True if the output is unchanging */
  sqlite3 *db;            /* The connection to the databases */
  sqlite3 *dbConfig;      /* Separate connection for global_config table */
  int useAttach;          /* True if global_config is attached to repository */
  int configOpen;         /* True if the config database is open */
  sqlite3_int64 now;      /* Seconds since 1970 */
  int repositoryOpen;     /* True if the main repository database is open */
  char *zRepositoryName;  /* Name of the repository database */
  const char *zMainDbType;/* "configdb", "localdb", or "repository" */
  const char *zHome;      /* Name of user home directory */
  int localOpen;          /* True if the local database is open */
  char *zLocalRoot;       /* The directory holding the  local database */
  int minPrefix;          /* Number of digits needed for a distinct UUID */
  int fSqlTrace;          /* True if --sqltrace flag is present */
  int fSqlStats;          /* True if --sqltrace or --sqlstats are present */
  int fSqlPrint;          /* True if -sqlprint flag is present */
  int fQuiet;             /* True if -quiet flag is present */
  int fHttpTrace;         /* Trace outbound HTTP requests */
  int fSystemTrace;       /* Trace calls to fossil_system(), --systemtrace */
  int fNoSync;            /* Do not do an autosync even.  --nosync */
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
  FILE *httpIn;           /* Accept HTTP input from here */
  FILE *httpOut;          /* Send HTTP output here */
  int xlinkClusterOnly;   /* Set when cloning.  Only process clusters */
  int fTimeFormat;        /* 1 for UTC.  2 for localtime.  0 not yet selected */
  int *aCommitFile;       /* Array of files to be committed */
  int markPrivate;        /* All new artifacts are private if true */
  int clockSkewSeen;      /* True if clocks on client and server out of sync */

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
  char *urlFossil;        /* The path of the ?fossil=path suffix on ssh: */
  int dontKeepUrl;        /* Do not persist the URL */

  const char *zLogin;     /* Login name.  "" if not logged in. */
  const char *zSSLIdentity;  /* Value of --ssl-identity option, filename of SSL client identity */
  int useLocalauth;       /* No login required if from 127.0.0.1 */
  int noPswd;             /* Logged in without password (on 127.0.0.1) */
  int userUid;            /* Integer user id */

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
** Search g.argv for arguments "--args FILENAME".  If found, then
** (1) remove the two arguments from g.argv
** (2) Read the file FILENAME
** (3) Use the contents of FILE to replace the two removed arguments:
**     (a) Ignore blank lines in the file
**     (b) Each non-empty line of the file is an argument, except
**     (c) If the line begins with "-" and contains a space, it is broken
**         into two arguments at the space.
*/
static void expand_args_option(void){
  Blob file = empty_blob;   /* Content of the file */
  Blob line = empty_blob;   /* One line of the file */
  unsigned int nLine;       /* Number of lines in the file*/
  unsigned int i, j, k;     /* Loop counters */
  int n;                    /* Number of bytes in one line */
  char *z;            /* General use string pointer */
  char **newArgv;     /* New expanded g.argv under construction */
  char const * zFileName;   /* input file name */
  FILE * zInFile;           /* input FILE */
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
    : fopen(zFileName,"rb");
  if(!zInFile){
    fossil_panic("Cannot open -args file [%s]", zFileName);
  }else{
    blob_read_from_channel(&file, zInFile, -1);
    if(stdin != zInFile){
      fclose(zInFile);
    }
    zInFile = NULL;
  }
  z = blob_str(&file);
  for(k=0, nLine=1; z[k]; k++) if( z[k]=='\n' ) nLine++;
  newArgv = fossil_malloc( sizeof(char*)*(g.argc + nLine*2) );
  for(j=0; j<i; j++) newArgv[j] = g.argv[j];
  
  blob_rewind(&file);
  while( (n = blob_line(&file, &line))>0 ){
    if( n<=1 ) continue;
    z = blob_buffer(&line);
    z[n-1] = 0;
    if((n>1) && ('\r'==z[n-2])){
      if(n==2) continue /*empty line*/;
      z[n-2] = 0;
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

/*
** This procedure runs first.
*/
int main(int argc, char **argv){
  const char *zCmdName = "unknown";
  int idx;
  int rc;
  int i;

#ifdef FOSSIL_ENABLE_TCL
  g.tcl.argc = argc;
  g.tcl.argv = argv;
  g.tcl.interp = 0;
#endif

  sqlite3_config(SQLITE_CONFIG_LOG, fossil_sqlite_log, 0);
  g.now = time(0);
  g.argc = argc;
  g.argv = argv;
  expand_args_option();
  argc = g.argc;
  argv = g.argv;
  for(i=0; i<argc; i++) g.argv[i] = fossil_mbcs_to_utf8(argv[i]);
  if( getenv("GATEWAY_INTERFACE")!=0 && !find_option("nocgi", 0, 0)){
    zCmdName = "cgi";
  }else if( argc<2 ){
    fossil_print(
       "Usage: %s COMMAND ...\n"
       "   or: %s help           -- for a list of common commands\n"
       "   or: %s help COMMMAND  -- for help with the named command\n"
       "   or: %s commands       -- for a list of all commands\n",
       argv[0], argv[0], argv[0], argv[0]);
    fossil_exit(1);
  }else{
    g.fQuiet = find_option("quiet", 0, 0)!=0;
    g.fSqlTrace = find_option("sqltrace", 0, 0)!=0;
    g.fSqlStats = find_option("sqlstats", 0, 0)!=0;
    g.fSystemTrace = find_option("systemtrace", 0, 0)!=0;
    if( g.fSqlTrace ) g.fSqlStats = 1;
    g.fSqlPrint = find_option("sqlprint", 0, 0)!=0;
    g.fHttpTrace = find_option("httptrace", 0, 0)!=0;
    g.zLogin = find_option("user", "U", 1);
    g.zSSLIdentity = find_option("ssl-identity", 0, 1);
    if( find_option("help",0,0)!=0 ){
      /* --help anywhere on the command line is translated into
      ** "fossil help argv[1] argv[2]..." */
      int i;
      char **zNewArgv = fossil_malloc( sizeof(char*)*(g.argc+2) );
      for(i=1; i<g.argc; i++) zNewArgv[i+1] = argv[i];
      zNewArgv[i+1] = 0;
      zNewArgv[0] = argv[0];
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
                   argv[0], zCmdName, argv[0]);
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
                 argv[0], zCmdName, argv[0], blob_str(&couldbe), argv[0]);
    fossil_exit(1);
  }
  aCommand[idx].xFunc();
  fossil_exit(0);
  /*NOT_REACHED*/
  return 0;
}

/*
** The following variable becomes true while processing a fatal error
** or a panic.  If additional "recursive-fatal" errors occur while
** shutting down, the recursive errors are silently ignored.
*/
static int mainInFatalError = 0;

/*
** Return the name of the current executable.
*/
const char *fossil_nameofexe(void){
#ifdef _WIN32
  return _pgmptr;
#else
  return g.argv[0];
#endif
}

/*
** Exit.  Take care to close the database first.
*/
NORETURN void fossil_exit(int rc){
  db_close(1);
  exit(rc);
}

/*
** Print an error message, rollback all databases, and quit.  These
** routines never return.
*/
NORETURN void fossil_panic(const char *zFormat, ...){
  char *z;
  va_list ap;
  static int once = 1;
  mainInFatalError = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiOutput && once ){
    once = 0;
    cgi_printf("<p class=\"generalError\">%h</p>", z);
    cgi_reply();
  }else{
    char *zOut = mprintf("%s: %s\n", fossil_nameofexe(), z);
    fossil_puts(zOut, 1);
  }
  db_force_rollback();
  fossil_exit(1);
}
NORETURN void fossil_fatal(const char *zFormat, ...){
  char *z;
  va_list ap;
  mainInFatalError = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiOutput ){
    g.cgiOutput = 0;
    cgi_printf("<p class=\"generalError\">%h</p>", z);
    cgi_reply();
  }else{
    char *zOut = mprintf("\r%s: %s\n", fossil_nameofexe(), z);
    fossil_puts(zOut, 1);
  }
  db_force_rollback();
  fossil_exit(1);
}

/* This routine works like fossil_fatal() except that if called
** recursively, the recursive call is a no-op.
**
** Use this in places where an error might occur while doing
** fatal error shutdown processing.  Unlike fossil_panic() and
** fossil_fatal() which never return, this routine might return if
** the fatal error handing is already in process.  The caller must
** be prepared for this routine to return.
*/
void fossil_fatal_recursive(const char *zFormat, ...){
  char *z;
  va_list ap;
  if( mainInFatalError ) return;
  mainInFatalError = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiOutput ){
    g.cgiOutput = 0;
    cgi_printf("<p class=\"generalError\">%h</p>", z);
    cgi_reply();
  }else{
    char *zOut = mprintf("\r%s: %s\n", fossil_nameofexe(), z);
    fossil_puts(zOut, 1);
  }
  db_force_rollback();
  fossil_exit(1);
}


/* Print a warning message */
void fossil_warning(const char *zFormat, ...){
  char *z;
  va_list ap;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiOutput ){
    cgi_printf("<p class=\"generalError\">%h</p>", z);
  }else{
    char *zOut = mprintf("\r%s: %s\n", fossil_nameofexe(), z);
    fossil_puts(zOut, 1);
    free(zOut);
  }
}

/*
** Malloc and free routines that cannot fail
*/
void *fossil_malloc(size_t n){
  void *p = malloc(n==0 ? 1 : n);
  if( p==0 ) fossil_panic("out of memory");
  return p;
}
void fossil_free(void *p){
  free(p);
}
void *fossil_realloc(void *p, size_t n){
  p = realloc(p, n);
  if( p==0 ) fossil_panic("out of memory");
  return p;
}

/*
** This function implements a cross-platform "system()" interface.
*/
int fossil_system(const char *zOrigCmd){
  int rc;
#if defined(_WIN32)
  /* On windows, we have to put double-quotes around the entire command.
  ** Who knows why - this is just the way windows works.
  */
  char *zNewCmd = mprintf("\"%s\"", zOrigCmd);
  char *zMbcs = fossil_utf8_to_mbcs(zNewCmd);
  if( g.fSystemTrace ) fprintf(stderr, "SYSTEM: %s\n", zMbcs);
  rc = system(zMbcs);
  fossil_mbcs_free(zMbcs);
  free(zNewCmd);
#else
  /* On unix, evaluate the command directly.
  */
  if( g.fSystemTrace ) fprintf(stderr, "SYSTEM: %s\n", zOrigCmd);
  rc = system(zOrigCmd);
#endif 
  return rc; 
}

/*
** Turn off any NL to CRNL translation on the stream given as an
** argument.  This is a no-op on unix but is necessary on windows.
*/
void fossil_binary_mode(FILE *p){
#if defined(_WIN32)
  _setmode(_fileno(p), _O_BINARY);
#endif
}



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
    default: {
      sqlite3_snprintf(sizeof(zCode),zCode,"error code %d",iCode);
    }
  }
  return zCode;
}

/* Error logs from SQLite */
void fossil_sqlite_log(void *notUsed, int iCode, const char *zErrmsg){
  fossil_warning("%s: %s", sqlite_error_code_name(iCode), zErrmsg);
}

/*
** Print a usage comment and quit
*/
void usage(const char *zFormat){
  fossil_fatal("Usage: %s %s %s\n", fossil_nameofexe(), g.argv[1], zFormat);
}

/*
** Remove n elements from g.argv beginning with the i-th element.
*/
void remove_from_argv(int i, int n){
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
    if (i+hasArg >= g.argc) break;
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
      fossil_fatal("unrecognized command-line option, or missing argument: %s", g.argv[i]);
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
  const char *aCmd[count(aWebpage)];
  for(i=nCmd=0; i<count(aWebpage); i++){
    aCmd[nCmd++] = aWebpage[i].zName;
  }
  multi_column_list(aCmd, nCmd);
}

/*
** COMMAND: version
**
** Usage: %fossil version
**
** Print the source code version number for the fossil executable.
*/
void version_cmd(void){
  fossil_print("This is fossil version " RELEASE_VERSION " "
                MANIFEST_VERSION " " MANIFEST_DATE " UTC\n");
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
**    %fossil help --all        Show both command and auxiliary commands
**    %fossil help --test       Show test commands only
**    %fossil help --aux        Show auxiliary commands only
*/
void help_cmd(void){
  int rc, idx;
  const char *z;
  if( g.argc<3 ){
    z = fossil_nameofexe();
    fossil_print(
      "Usage: %s help COMMAND\n"
      "Common COMMANDs:  (use \"%s help --all\" for a complete list)\n",
      z, z);
    command_list(0, CMDFLAG_1ST_TIER);
    version_cmd();
    return;
  }
  if( find_option("all",0,0) ){
    command_list(0, CMDFLAG_1ST_TIER | CMDFLAG_2ND_TIER);
    return;
  }
  if( find_option("aux",0,0) ){
    command_list(0, CMDFLAG_2ND_TIER);
    return;
  }
  if( find_option("test",0,0) ){
    command_list(0, CMDFLAG_TEST);
    return;
  }
  rc = name_search(g.argv[2], aCommand, count(aCommand), &idx);
  if( rc==1 ){
    fossil_print("unknown command: %s\nAvailable commands:\n", g.argv[2]);
    command_list(0, 0xff);
    fossil_exit(1);
  }else if( rc==2 ){
    fossil_print("ambiguous command prefix: %s\nMatching commands:\n",
                 g.argv[2]);
    command_list(g.argv[2], 0xff);
    fossil_exit(1);
  }
  z = aCmdHelp[idx];
  if( z==0 ){
    fossil_fatal("no help available for the %s command",
       aCommand[idx].zName);
  }
  while( *z ){
    if( *z=='%' && strncmp(z, "%fossil", 7)==0 ){
      fossil_print("%s", fossil_nameofexe());
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

    style_submenu_element("Command-List", "Command-List", "%s/help", g.zTop);
    @ <h1>The "%s(zCmd)" command:</h1>
    rc = name_search(zCmd, aCommand, count(aCommand), &idx);
    if( rc==1 ){
      @ unknown command: %s(zCmd)
    }else if( rc==2 ){
      @ ambiguous command prefix: %s(zCmd)
    }else{
      z = (char*)aCmdHelp[idx];
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
        free(z);
      }
    }
  }else{
    int i, j, n;

    @ <h1>Available commands:</h1>
    @ <table border="0"><tr>
    for(i=j=0; i<count(aCommand); i++){
      const char *z = aCommand[i].zName;
      if( strncmp(z,"test",4)==0 ) continue;
      j++;
    }
    n = (j+6)/7;
    for(i=j=0; i<count(aCommand); i++){
      const char *z = aCommand[i].zName;
      if( strncmp(z,"test",4)==0 ) continue;
      if( j==0 ){
        @ <td valign="top"><ul>
      }
      @ <li><a href="%s(g.zTop)/help?cmd=%s(z)">%s(z)</a>
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
    @ %h(aCmdHelp[i])
    @ </pre></blockquote>
  }
  style_footer();
}

/*
** Set the g.zBaseURL value to the full URL for the toplevel of
** the fossil tree.  Set g.zTop to g.zBaseURL without the
** leading "http://" and the host and port.
*/
void set_base_url(void){
  int i;
  const char *zHost;
  const char *zMode;
  const char *zCur;

  if( g.zBaseURL!=0 ) return;
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

    file_canonical_name(zRepo, &dir);
    zDir = blob_str(&dir);
    if( file_isdir(zDir)==1 ){
      if( chdir(zDir) || chroot(zDir) || chdir("/") ){
        fossil_fatal("unable to chroot into %s", zDir);
      }
      zRepo = "/";
    }else{
      for(i=strlen(zDir)-1; i>0 && zDir[i]!='/'; i--){}
      if( zDir[i]!='/' ) fossil_panic("bad repository name: %s", zRepo);
      zDir[i] = 0;
      if( chdir(zDir) || chroot(zDir) || chdir("/") ){
        fossil_fatal("unable to chroot into %s", zDir);
      }
      zDir[i] = '/';
      zRepo = &zDir[i];
    }
    if( stat(zRepo, &sStat)!=0 ){
      fossil_fatal("cannot stat() repository: %s", zRepo);
    }
    setgid(sStat.st_gid);
    setuid(sStat.st_uid);
    if( g.db!=0 ){
      db_close(1);
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
*/
static void process_one_web_page(const char *zNotFound){
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
      ** characters other than alphanumerics, "-", "/", and "_".
      */
      for(j=strlen(g.zRepositoryName)+1, k=0; zRepo[j] && k<i-1; j++, k++){
        if( !fossil_isalnum(zRepo[j]) && zRepo[j]!='-' && zRepo[j]!='/' ){
          zRepo[j] = '_';
        }
      }
      if( zRepo[0]=='/' && zRepo[1]=='/' ){ zRepo++; j--; }

      szFile = file_size(zRepo);
      if( zPathInfo[i]=='/' && szFile<0 ){
        assert( fossil_strcmp(&zRepo[j], ".fossil")==0 );
        zRepo[j] = 0;
        if( file_isdir(zRepo)==1 ){
          fossil_free(zToFree);
          i++;
          continue;
        }
        zRepo[j] = '.';
      }

      if( szFile<1024 ){
        if( zNotFound ){
          cgi_redirect(zNotFound);
        }else{
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
  set_base_url();
  if( zPathInfo==0 || zPathInfo[0]==0 
      || (zPathInfo[0]=='/' && zPathInfo[1]==0) ){
    fossil_redirect_home();
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
          file_simplify_name(zAltRepo, -1);
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
  if( g.zExtra ){
    /* CGI parameters get this treatment elsewhere, but places like getfile
    ** will use g.zExtra directly.
    */
    dehttpize(g.zExtra);
    cgi_set_parameter_nocopy("name", g.zExtra);
  }

  /* Locate the method specified by the path and execute the function
  ** that implements that method.
  */
  if( name_search(g.zPath, aWebpage, count(aWebpage), &idx) &&
      name_search("not_found", aWebpage, count(aWebpage), &idx) ){
    cgi_set_status(404,"Not Found");
    @ <h1>Not Found</h1>
    @ <p>Page not found: %h(g.zPath)</p>
  }else if( aWebpage[idx].xFunc!=page_xfer && db_schema_is_outofdate() ){
    @ <h1>Server Configuration Error</h1>
    @ <p>The database schema on the server is out-of-date.  Please ask
    @ the administrator to run <b>fossil rebuild</b>.</p>
  }else{
    aWebpage[idx].xFunc();
  }

  /* Return the result.
  */
  cgi_reply();
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
*/
void cmd_cgi(void){
  const char *zFile;
  const char *zNotFound = 0;
  char **azRedirect = 0;             /* List of repositories to redirect to */
  int nRedirect = 0;                 /* Number of entries in azRedirect */
  Blob config, line, key, value, value2;
  if( g.argc==3 && fossil_strcmp(g.argv[1],"cgi")==0 ){
    zFile = g.argv[2];
  }else{
    zFile = g.argv[1];
  }
  g.httpOut = stdout;
  g.httpIn = stdin;
#if defined(_WIN32)
  /* Set binary mode on windows to avoid undesired translations
  ** between \n and \r\n. */
  setmode(_fileno(g.httpOut), _O_BINARY);
  setmode(_fileno(g.httpIn), _O_BINARY);
#endif
#ifdef __EMX__
  /* Similar hack for OS/2 */
  setmode(fileno(g.httpOut), O_BINARY);
  setmode(fileno(g.httpIn), O_BINARY);
#endif
  g.cgiOutput = 1;
  blob_read_from_file(&config, zFile);
  while( blob_line(&config, &line) ){
    if( !blob_token(&line, &key) ) continue;
    if( blob_buffer(&key)[0]=='#' ) continue;
    if( blob_eq(&key, "debug:") && blob_token(&line, &value) ){
      g.fDebug = fopen(blob_str(&value), "a");
      blob_reset(&value);
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
  }
  blob_reset(&config);
  if( g.db==0 && g.zRepositoryName==0 && nRedirect==0 ){
    cgi_panic("Unable to find or open the project repository");
  }
  cgi_init();
  if( nRedirect ){
    redirect_web_page(nRedirect, azRedirect);
  }else{
    process_one_web_page(zNotFound);
  }
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
** very primative error message is returned.
*/
void redirect_web_page(int nRedirect, char **azRedirect){
  int i;                             /* Loop counter */
  const char *zNotFound = 0;         /* Not found URL */
  const char *zName = P("name");
  set_base_url();          
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
** If g.argv[2] exists then it is either the name of a repository
** that will be used by a server, or else it is a directory that
** contains multiple repositories that can be served.  If g.argv[2]
** is a directory, the repositories it contains must be named
** "*.fossil".  If g.argv[2] does not exists, then we must be within
** a check-out and the repository to be served is the repository of
** that check-out.
**
** Open the respository to be served if it is known.  If g.argv[2] is
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
  }else if( !disallowDir && file_isdir(g.argv[2])==1 ){
    g.zRepositoryName = mprintf("%s", g.argv[2]);
    file_simplify_name(g.zRepositoryName, -1);
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
** Usage: %fossil http REPOSITORY [--notfound URL] [--host HOSTNAME] [--https]
**
** Handle a single HTTP request appearing on stdin.  The resulting webpage
** is delivered on stdout.  This method is used to launch an HTTP request
** handler from inetd, for example.  The argument is the name of the 
** repository.
**
** If REPOSITORY is a directory that contains one or more respositories
** with names of the form "*.fossil" then the first element of the URL
** pathname selects among the various repositories.  If the pathname does
** not select a valid repository and the --notfound option is available,
** then the server redirects (HTTP code 302) to the URL of --notfound.
**
** The --host option can be used to specify the hostname for the server.
** The --https option indicates that the request came from HTTPS rather
** than HTTP.
**
** Other options:
**
**    --localauth      Password signin is not required if this is true and
**                     the input comes from 127.0.0.1 and the "localauth"
**                     setting is not disabled.
**
**    --nossl          SSL connections are not available so do not
**                     redirect from http: to https:.
*/
void cmd_http(void){
  const char *zIpAddr;
  const char *zNotFound;
  const char *zHost;
  zNotFound = find_option("notfound", 0, 1);
  g.useLocalauth = find_option("localauth", 0, 0)!=0;
  g.sslNotAvailable = find_option("nossl", 0, 0)!=0;
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
  find_server_repository(0);
  g.zRepositoryName = enter_chroot_jail(g.zRepositoryName);
  cgi_handle_http_request(zIpAddr);
  process_one_web_page(zNotFound);
}

/*
** Note that the following command is used by ssh:// processing.
**
** COMMAND: test-http
** Works like the http command but gives setup permission to all users.
*/
void cmd_test_http(void){
  login_set_capabilities("s", 0);
  g.httpIn = stdin;
  g.httpOut = stdout;
  find_server_repository(0);
  g.cgiOutput = 1;
  g.fullHttpReply = 1;
  cgi_handle_http_request(0);
  process_one_web_page(0);
}

#if !defined(_WIN32)
#if !defined(__DARWIN__) && !defined(__APPLE__)
/*
** Search for an executable on the PATH environment variable.
** Return true (1) if found and false (0) if not found.
*/
static int binaryOnPath(const char *zBinary){
  const char *zPath = getenv("PATH");
  char *zFull;
  int i;
  int bExists;
  while( zPath && zPath[0] ){
    while( zPath[0]==':' ) zPath++;
    for(i=0; zPath[i] && zPath[i]!=':'; i++){}
    zFull = mprintf("%.*s/%s", i, zPath, zBinary);
    bExists = file_access(zFull, X_OK);
    free(zFull);
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
** Usage: %fossil server ?-P|--port TCPPORT? ?REPOSITORY?
**    Or: %fossil ui ?-P|--port TCPPORT? ?REPOSITORY?
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
** In the "server" command, the REPOSITORY can be a directory (aka folder)
** that contains one or more respositories with names ending in ".fossil".
** In that case, the first element of the URL is used to select among the
** various repositories.
**
** By default, the "ui" command provides full administrative access without
** having to log in.  This can be disabled by setting turning off the
** "localauth" setting.  Automatic login for the "server" command is available
** if the --localauth option is present and the "localauth" setting is off
** and the connection is from localhost.
*/
void cmd_webserver(void){
  int iPort, mxPort;        /* Range of TCP ports allowed */
  const char *zPort;        /* Value of the --port option */
  char *zBrowser;           /* Name of web browser program */
  char *zBrowserCmd = 0;    /* Command to launch the web browser */
  int isUiCmd;              /* True if command is "ui", not "server' */
  const char *zNotFound;    /* The --notfound option or NULL */
  int flags = 0;            /* Server flags */

#if defined(_WIN32)
  const char *zStopperFile;    /* Name of file used to terminate server */
  zStopperFile = find_option("stopper", 0, 1);
#endif

  g.thTrace = find_option("th-trace", 0, 0)!=0;
  g.useLocalauth = find_option("localauth", 0, 0)!=0;
  if( g.thTrace ){
    blob_zero(&g.thLog);
  }
  zPort = find_option("port", "P", 1);
  zNotFound = find_option("notfound", 0, 1);
  if( g.argc!=2 && g.argc!=3 ) usage("?REPOSITORY?");
  isUiCmd = g.argv[1][0]=='u';
  if( isUiCmd ){
    flags |= HTTP_SERVER_LOCALHOST;
    g.useLocalauth = 1;
  }
  find_server_repository(isUiCmd);
  if( zPort ){
    iPort = mxPort = atoi(zPort);
  }else{
    iPort = db_get_int("http-port", 8080);
    mxPort = iPort+100;
  }
#if !defined(_WIN32)
  /* Unix implementation */
  if( isUiCmd ){
#if !defined(__DARWIN__) && !defined(__APPLE__)
    zBrowser = db_get("web-browser", 0);
    if( zBrowser==0 ){
      static char *azBrowserProg[] = { "xdg-open", "gnome-open", "firefox" };
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
    zBrowserCmd = mprintf("%s http://localhost:%%d/ &", zBrowser);
  }
  db_close(1);
  if( cgi_http_server(iPort, mxPort, zBrowserCmd, flags) ){
    fossil_fatal("unable to listen on TCP socket %d", iPort);
  }
  g.sslNotAvailable = 1;
  g.httpIn = stdin;
  g.httpOut = stdout;
  if( g.fHttpTrace || g.fSqlTrace ){
    fprintf(stderr, "====== SERVER pid %d =======\n", getpid());
  }
  g.cgiOutput = 1;
  find_server_repository(isUiCmd);
  g.zRepositoryName = enter_chroot_jail(g.zRepositoryName);
  cgi_handle_http_request(0);
  process_one_web_page(zNotFound);
#else
  /* Win32 implementation */
  if( isUiCmd ){
    zBrowser = db_get("web-browser", "start");
    zBrowserCmd = mprintf("%s http://127.0.0.1:%%d/", zBrowser);
  }
  db_close(1);
  if( win32_http_service(iPort, zNotFound, flags) ){
    win32_http_server(iPort, mxPort, zBrowserCmd,
                      zStopperFile, zNotFound, flags);
  }
#endif
}

/*
** COMMAND:  test-echo
**
** Echo all command-line arguments (enclosed in [...]) to the screen so that
** wildcard expansion behavior of the host shell can be investigated.
*/
void test_echo_cmd(void){
  int i;
  for(i=0; i<g.argc; i++){
    fossil_print("argv[%d] = [%s]\n", i, g.argv[i]);
  }
}
