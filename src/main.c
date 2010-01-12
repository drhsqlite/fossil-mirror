/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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
** All global variables are in this structure.
*/
struct Global {
  int argc; char **argv;  /* Command-line arguments to the program */
  int isConst;            /* True if the output is unchanging */
  sqlite3 *db;            /* The connection to the databases */
  sqlite3 *dbConfig;      /* Separate connection for global_config table */
  int useAttach;          /* True if global_config is attached to repository */
  int configOpen;         /* True if the config database is open */
  long long int now;      /* Seconds since 1970 */
  int repositoryOpen;     /* True if the main repository database is open */
  char *zRepositoryName;  /* Name of the repository database */
  const char *zHome;      /* Name of user home directory */
  int localOpen;          /* True if the local database is open */
  char *zLocalRoot;       /* The directory holding the  local database */
  int minPrefix;          /* Number of digits needed for a distinct UUID */
  int fSqlTrace;          /* True if -sqltrace flag is present */
  int fSqlPrint;          /* True if -sqlprint flag is present */
  int fQuiet;             /* True if -quiet flag is present */
  int fHttpTrace;         /* Trace outbound HTTP requests */
  int fNoSync;            /* Do not do an autosync even.  --nosync */
  char *zPath;            /* Name of webpage being served */
  char *zExtra;           /* Extra path information past the webpage name */
  char *zBaseURL;         /* Full text of the URL being served */
  char *zTop;             /* Parent directory of zPath */
  const char *zContentType;  /* The content type of the input HTTP request */
  int iErrPriority;       /* Priority of current error message */
  char *zErrMsg;          /* Text of an error message */
  Blob cgiIn;             /* Input to an xfer www method */
  int cgiPanic;           /* Write error messages to CGI */
  int xferPanic;          /* Write error messages in XFER protocol */
  int fullHttpReply;      /* True for full HTTP reply.  False for CGI reply */
  Th_Interp *interp;      /* The TH1 interpreter */
  FILE *httpIn;           /* Accept HTTP input from here */
  FILE *httpOut;          /* Send HTTP output here */
  int xlinkClusterOnly;   /* Set when cloning.  Only process clusters */
  int fTimeFormat;        /* 1 for UTC.  2 for localtime.  0 not yet selected */
  int *aCommitFile;       /* Array of files to be committed */
  int markPrivate;        /* All new artifacts are private if true */

  int urlIsFile;          /* True if a "file:" url */
  int urlIsHttps;         /* True if a "https:" url */
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

  const char *zLogin;     /* Login name.  "" if not logged in. */
  int noPswd;             /* Logged in without password (on 127.0.0.1) */
  int userUid;            /* Integer user id */

  /* Information used to populate the RCVFROM table */
  int rcvid;              /* The rcvid.  0 if not yet defined. */
  char *zIpAddr;          /* The remote IP address */
  char *zNonce;           /* The nonce used for login */
  
  /* permissions used by the server */
  int okSetup;            /* s: use Setup screens on web interface */
  int okAdmin;            /* a: administrative permission */
  int okDelete;           /* d: delete wiki or tickets */
  int okPassword;         /* p: change password */
  int okQuery;            /* q: create new reports */
  int okWrite;            /* i: xfer inbound. checkin */
  int okRead;             /* o: xfer outbound. checkout */
  int okHistory;          /* h: access historical information. */
  int okClone;            /* g: clone */
  int okRdWiki;           /* j: view wiki via web */
  int okNewWiki;          /* f: create new wiki via web */
  int okApndWiki;         /* m: append to wiki via web */
  int okWrWiki;           /* k: edit wiki via web */
  int okRdTkt;            /* r: view tickets via web */
  int okNewTkt;           /* n: create new tickets */
  int okApndTkt;          /* c: append to tickets via the web */
  int okWrTkt;            /* w: make changes to tickets via web */
  int okTktFmt;           /* t: create new ticket report formats */
  int okRdAddr;           /* e: read email addresses or other private data */
  int okZip;              /* z: download zipped artifact via /zip URL */

  /* For defense against Cross-site Request Forgery attacks */
  char zCsrfToken[12];    /* Value of the anti-CSRF token */
  int okCsrf;             /* Anti-CSRF token is present and valid */

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
    c = strcmp(zName, aMap[mid].zName);
    if( c==0 ){
      *pIndex = mid;
      return 0;
    }else if( c<0 ){
      upr = mid - 1;
    }else{
      lwr = mid + 1;
    }
  }
  for(m=cnt=0, i=upr-2; i<=upr+3 && i<nMap; i++){
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
** This procedure runs first.
*/
int main(int argc, char **argv){
  const char *zCmdName;
  int idx;
  int rc;

  g.now = time(0);
  g.argc = argc;
  g.argv = argv;
  if( getenv("GATEWAY_INTERFACE")!=0 ){
    zCmdName = "cgi";
  }else if( argc<2 ){
    fprintf(stderr, "Usage: %s COMMAND ...\n", argv[0]);
    exit(1);
  }else{
    g.fQuiet = find_option("quiet", 0, 0)!=0;
    g.fSqlTrace = find_option("sqltrace", 0, 0)!=0;
    g.fSqlPrint = find_option("sqlprint", 0, 0)!=0;
    g.fHttpTrace = find_option("httptrace", 0, 0)!=0;
    g.zLogin = find_option("user", "U", 1);
    zCmdName = argv[1];
  }
  rc = name_search(zCmdName, aCommand, count(aCommand), &idx);
  if( rc==1 ){
    fprintf(stderr,"%s: unknown command: %s\n"
                   "%s: use \"help\" for more information\n",
                   argv[0], zCmdName, argv[0]);
    return 1;
  }else if( rc==2 ){
    fprintf(stderr,"%s: ambiguous command prefix: %s\n"
                   "%s: use \"help\" for more information\n",
                   argv[0], zCmdName, argv[0]);
    return 1;
  }
  aCommand[idx].xFunc();
  return 0;
}

/*
** Print an error message, rollback all databases, and quit.
*/
void fossil_panic(const char *zFormat, ...){
  char *z;
  va_list ap;
  static int once = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiPanic && once ){
    once = 0;
    cgi_printf("<p><font color=\"red\">%h</font></p>", z);
    cgi_reply();
  }else{
    fprintf(stderr, "%s: %s\n", g.argv[0], z);
  }
  db_force_rollback();
  exit(1);
}
void fossil_fatal(const char *zFormat, ...){
  char *z;
  va_list ap;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiPanic ){
    g.cgiPanic = 0;
    cgi_printf("<p><font color=\"red\">%h</font></p>", z);
    cgi_reply();
  }else{
    fprintf(stderr, "%s: %s\n", g.argv[0], z);
  }
  db_force_rollback();
  exit(1);
}
void fossil_warning(const char *zFormat, ...){
  char *z;
  va_list ap;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiPanic ){
    cgi_printf("<p><font color=\"red\">%h</font></p>", z);
  }else{
    fprintf(stderr, "%s: %s\n", g.argv[0], z);
  }
}

/*
** Print a usage comment and quit
*/
void usage(const char *zFormat){
  fprintf(stderr, "Usage: %s %s %s\n", g.argv[0], g.argv[1], zFormat);
  exit(1);
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
  for(i=2; i<g.argc; i++){
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
    }else if( zShort!=0 && strcmp(z,zShort)==0 ){
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
      printf("%s%-*s", zSpacer, mxLen, azWord[j]);
      zSpacer = "  ";
    }
    printf("\n");
  }
}

/*
** COM MAND: commands
**
** Usage: %fossil commands
** List all supported commands.
*/
void cmd_cmd_list(void){
  int i, nCmd;
  const char *aCmd[count(aCommand)];
  for(i=nCmd=0; i<count(aCommand); i++){
    if( strncmp(aCommand[i].zName,"test",4)==0 ) continue;
    /* if( strcmp(aCommand[i].zName, g.argv[1])==0 ) continue; */
    aCmd[nCmd++] = aCommand[i].zName;
  }
  multi_column_list(aCmd, nCmd);
}

/*
** COMMAND: test-commands
**
** Usage: %fossil test-commands
**
** List all commands used for testing and debugging.
*/
void cmd_test_cmd_list(void){
  int i, nCmd;
  const char *aCmd[count(aCommand)];
  for(i=nCmd=0; i<count(aCommand); i++){
    if( strncmp(aCommand[i].zName,"test",4)!=0 ) continue;
    /* if( strcmp(aCommand[i].zName, g.argv[1])==0 ) continue; */
    aCmd[nCmd++] = aCommand[i].zName;
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
  printf("This is fossil version " MANIFEST_VERSION " " MANIFEST_DATE " UTC\n");
}


/*
** COMMAND: help
**
** Usage: %fossil help COMMAND
**
** Display information on how to use COMMAND
*/
void help_cmd(void){
  int rc, idx;
  const char *z;
  if( g.argc!=3 ){
    printf("Usage: %s help COMMAND.\nAvailable COMMANDs:\n", g.argv[0]);
    cmd_cmd_list();
    version_cmd();
    return;
  }
  rc = name_search(g.argv[2], aCommand, count(aCommand), &idx);
  if( rc==1 ){
    fossil_fatal("unknown command: %s", g.argv[2]);
  }else if( rc==2 ){
    fossil_fatal("ambiguous command prefix: %s", g.argv[2]);
  }
  z = aCmdHelp[idx];
  if( z==0 ){
    fossil_fatal("no help available for the %s command",
       aCommand[idx].zName);
  }
  while( *z ){
    if( *z=='%' && strncmp(z, "%fossil", 7)==0 ){
      printf("%s", g.argv[0]);
      z += 7;
    }else{
      putchar(*z);
      z++;
    }
  }
  putchar('\n');
}

/*
** Set the g.zBaseURL value to the full URL for the toplevel of
** the fossil tree.  Set g.zHomeURL to g.zBaseURL without the
** leading "http://" and the host and port.
*/
void set_base_url(void){
  int i;
  const char *zHost = PD("HTTP_HOST","");
  const char *zMode = PD("HTTPS","off");
  const char *zCur = PD("SCRIPT_NAME","/");

  i = strlen(zCur);
  while( i>0 && zCur[i-1]=='/' ) i--;
  if( strcmp(zMode,"on")==0 ){
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
void fossil_redirect_home(void){
  cgi_redirectf("%s%s", g.zBaseURL, db_get("index-page", "/index"));
}

/*
** Preconditions:
**
**    * Environment variables are set up according to the CGI standard.
**    * The respository database has been located and opened.
** 
** Process the webpage specified by the PATH_INFO or REQUEST_URI
** environment variable.
*/
static void process_one_web_page(void){
  const char *zPathInfo;
  char *zPath = NULL;
  int idx;
  int i;

  /* Find the page that the user has requested, construct and deliver that
  ** page.
  */
  set_base_url();
  zPathInfo = P("PATH_INFO");
  if( zPathInfo==0 || zPathInfo[0]==0 
      || (zPathInfo[0]=='/' && zPathInfo[1]==0) ){
    fossil_redirect_home();
  }else{
    zPath = mprintf("%s", zPathInfo);
  }

  /* Remove the leading "/" at the beginning of the path.
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
    */
    dehttpize(g.zExtra);
    cgi_set_parameter_nocopy("name", g.zExtra);
  }

  /* Prevent robots from indexing this site.
  */
  if( strcmp(g.zPath, "robots.txt")==0 ){
    cgi_set_content_type("text/plain");
    @ User-agent: *
    @ Disallow: /
    cgi_reply();
    exit(0);
  }
  
  /* Locate the method specified by the path and execute the function
  ** that implements that method.
  */
  if( name_search(g.zPath, aWebpage, count(aWebpage), &idx) &&
      name_search("not_found", aWebpage, count(aWebpage), &idx) ){
    cgi_set_status(404,"Not Found");
    @ <h1>Not Found</h1>
    @ <p>Page not found: %h(g.zPath)</p>
  }else{
    aWebpage[idx].xFunc();
  }

  /* Return the result.
  */
  cgi_reply();
}

/*
** COMMAND: cgi
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
  Blob config, line, key, value;
  if( g.argc==3 && strcmp(g.argv[1],"cgi")==0 ){
    zFile = g.argv[2];
  }else{
    zFile = g.argv[1];
  }
  g.httpOut = stdout;
  g.httpIn = stdin;
#ifdef __MINGW32__
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
  g.cgiPanic = 1;
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
    if( blob_eq(&key, "repository:") && blob_token(&line, &value) ){
      db_open_repository(blob_str(&value));
      blob_reset(&value);
      blob_reset(&config);
      break;
    }
  }
  if( g.db==0 ){
    cgi_panic("Unable to find or open the project repository");
  }
  cgi_init();
  process_one_web_page();
}

/*
** undocumented format:
**
**        fossil http REPOSITORY INFILE OUTFILE IPADDR
**
** The argv==6 form is used by the win32 server only.
**
** COMMAND: http
**
** Usage: %fossil http REPOSITORY
**
** Handle a single HTTP request appearing on stdin.  The resulting webpage
** is delivered on stdout.  This method is used to launch an HTTP request
** handler from inetd, for example.  The argument is the name of the 
** repository.
*/
void cmd_http(void){
  const char *zIpAddr;
  if( g.argc!=2 && g.argc!=3 && g.argc!=6 ){
    cgi_panic("no repository specified");
  }
#if !defined(__MINGW32__)
  if( g.argc==3 && getuid()==0 ){
    int i;
    char *zRepo = g.argv[2];
    struct stat sStat;
    for(i=strlen(zRepo)-1; i>0 && zRepo[i]!='/'; i--){}
    if( zRepo[i]=='/' ){
      zRepo[i] = 0;
      chdir(g.argv[2]);
      chroot(g.argv[2]);
      g.argv[2] = &zRepo[i+1];
    }
    if( stat(g.argv[2], &sStat)!=0 ){
      fossil_fatal("cannot stat() repository: %s", g.argv[2]);
    }
    setgid(sStat.st_gid);
    setuid(sStat.st_uid);
  }
#endif
  g.cgiPanic = 1;
  g.fullHttpReply = 1;
  if( g.argc==6 ){
    g.httpIn = fopen(g.argv[3], "rb");
    g.httpOut = fopen(g.argv[4], "wb");
    zIpAddr = g.argv[5];
  }else{
    g.httpIn = stdin;
    g.httpOut = stdout;
    zIpAddr = 0;
  }
  if( g.argc>=3 ){
    db_open_repository(g.argv[2]);
  }else{
    db_must_be_within_tree();
  }
  cgi_handle_http_request(zIpAddr);
  process_one_web_page();
}

/*
** COMMAND: test-http
** Works like the http command but gives setup permission to all users.
*/
void cmd_test_http(void){
  login_set_capabilities("s");
  cmd_http();
}

#ifndef __MINGW32__
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
    bExists = access(zFull, X_OK);
    free(zFull);
    if( bExists==0 ) return 1;
    zPath += i;
  }
  return 0;
}
#endif
#endif

/*
** COMMAND: server
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
** the web server.
*/
void cmd_webserver(void){
  int iPort, mxPort;
  const char *zPort;
  char *zBrowser;
  char *zBrowserCmd = 0;

  g.thTrace = find_option("th-trace", 0, 0)!=0;
  if( g.thTrace ){
    blob_zero(&g.thLog);
  }
  zPort = find_option("port", "P", 1);
  if( g.argc!=2 && g.argc!=3 ) usage("?REPOSITORY?");
  if( g.argc==2 ){
    db_must_be_within_tree();
  }else{
    db_open_repository(g.argv[2]);
  }
  if( zPort ){
    iPort = mxPort = atoi(zPort);
  }else{
    iPort = db_get_int("http-port", 8080);
    mxPort = iPort+100;
  }
#ifndef __MINGW32__
  /* Unix implementation */
  if( g.argv[1][0]=='u' ){
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
  db_close();
  if( cgi_http_server(iPort, mxPort, zBrowserCmd) ){
    fossil_fatal("unable to listen on TCP socket %d", iPort);
  }
  g.httpIn = stdin;
  g.httpOut = stdout;
  if( g.fHttpTrace || g.fSqlTrace ){
    fprintf(stderr, "====== SERVER pid %d =======\n", getpid());
  }
  g.cgiPanic = 1;
  if( g.argc==2 ){
    db_must_be_within_tree();
  }else{
    db_open_repository(g.argv[2]);
  }
  cgi_handle_http_request(0);
  process_one_web_page();
#else
  /* Win32 implementation */
  if( g.argv[1][0]=='u' ){
    zBrowser = db_get("web-browser", "start");
    zBrowserCmd = mprintf("%s http://127.0.0.1:%%d/", zBrowser);
  }
  db_close();
  win32_http_server(iPort, mxPort, zBrowserCmd);
#endif
}
