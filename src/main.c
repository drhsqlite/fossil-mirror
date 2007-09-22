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
** All global variables are in this structure.
*/
struct Global {
  int argc; char **argv;  /* Command-line arguments to the program */
  int isConst;            /* True if the output is unchanging */
  sqlite3 *db;            /* The connection to the databases */
  int configOpen;         /* True if the config database is open */
  long long int now;      /* Seconds since 1970 */
  int repositoryOpen;     /* True if the main repository database is open */
  char *zRepositoryName;  /* Name of the repository database */
  int localOpen;          /* True if the local database is open */
  char *zLocalRoot;       /* The directory holding the  local database */
  int minPrefix;          /* Number of digits needed for a distinct UUID */
  int fSqlTrace;          /* True if -sqltrace flag is present */
  int fSqlPrint;          /* True if -sqlprint flag is present */
  int fHttpTrace;         /* Trace outbound HTTP requests */
  char *zPath;            /* Name of webpage being served */
  char *zExtra;           /* Extra path information past the webpage name */
  char *zBaseURL;         /* Full text of the URL being served */
  const char *zContentType;  /* The content type of the input HTTP request */
  int iErrPriority;       /* Priority of current error message */
  char *zErrMsg;          /* Text of an error message */
  Blob cgiIn;             /* Input to an xfer www method */
  int cgiPanic;           /* Write error messages to CGI */

  int *aCommitFile;

  int urlIsFile;          /* True if a "file:" url */
  char *urlName;          /* Hostname for http: or filename for file: */
  int urlPort;            /* TCP port number for http: */
  char *urlPath;          /* Pathname for http: */
  char *urlUser;          /* User id for http: */
  char *urlPasswd;        /* Password for http: */
  char *urlCanonical;     /* Canonical representation of the URL */

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

  FILE *fDebug;           /* Write debug information here, if the file exists */
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
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiPanic ){
    g.cgiPanic = 0;
    cgi_printf("<p><font color=\"red\">%h</font></p>", z);
    style_footer();
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
    style_footer();
    cgi_reply();
  }else{
    fprintf(stderr, "%s: %s\n", g.argv[0], z);
  }
  db_force_rollback();
  exit(1);
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
  const char *zReturn = 0;
  assert( hasArg==0 || hasArg==1 );
  for(i=2; i<g.argc; i++){
    char *z = g.argv[i];
    if( z[0]!='-' ) continue;
    z++;
    if( z[0]=='-' ){
      if( z[1]==0 ){
        remove_from_argv(i, 1);
        break;
      }
      z++;
    }
    if( strcmp(z,zLong)==0 || (zShort!=0 && strcmp(z,zShort)==0) ){
      zReturn = g.argv[i+hasArg];
      remove_from_argv(i, 1+hasArg);
      break;
    }
  }
  return zReturn;
}

/*
** Verify that there are no processed command-line options.  If
** Any remaining command-line argument begins with "-" print
** an error message and quit.
*/
void verify_all_options(void){
  int i;
  for(i=1; i<g.argc; i++){
    if( g.argv[i][0]=='-' ){
      fossil_fatal("unrecognized command-line option: %s", g.argv[i]);
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
** COMMAND: help
**
** Usage: %fossil help COMMAND
** Display information on how to use COMMAND
*/
void help_cmd(void){
  int rc, idx;
  const char *z;
  if( g.argc!=3 ){
    printf("Usage: %s help COMMAND.\nAvailable COMMANDs:\n", g.argv[0]);
    cmd_cmd_list();
    printf("This is fossil version " MANIFEST_VERSION " " MANIFEST_DATE "\n");
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
** RSS feeds need to reference absolute URLs so we need to calculate
** the base URL onto which we add components. This is basically
** cgi_redirect() stripped down and always returning an absolute URL.
*/
static char *get_base_url(void){
  int i;
  const char *zHost = PD("HTTP_HOST","");
  const char *zMode = PD("HTTPS","off");
  const char *zCur = PD("REQUEST_URI","/");

  for(i=0; zCur[i] && zCur[i]!='?' && zCur[i]!='#'; i++){}
  if( g.zExtra ){
    /* Skip to start of extra stuff, then pass over any /'s that might
    ** have separated the document root from the extra stuff. This
    ** ensures that the redirection actually redirects the root, not
    ** something deep down at the bottom of a URL.
    */
    i -= strlen(g.zExtra);
    while( i>0 && zCur[i-1]=='/' ){ i--; }
  }
  while( i>0 && zCur[i-1]!='/' ){ i--; }
  while( i>0 && zCur[i-1]=='/' ){ i--; }

  if( strcmp(zMode,"on")==0 ){
    return mprintf("https://%s%.*s", zHost, i, zCur);
  }
  return mprintf("http://%s%.*s", zHost, i, zCur);
}

/*
** Preconditions:
**
**    * Environment various are set up according to the CGI standard.
**    * The respository database has been located and opened.
** 
** Process the webpage specified by the PATH_INFO or REQUEST_URI
** environment variable.
*/
static void process_one_web_page(void){
  const char *zPathInfo;
  char *zPath;
  int idx;
  int i, j;

  /* Find the page that the user has requested, construct and deliver that
  ** page.
  */
  zPathInfo = P("PATH_INFO");
  if( zPathInfo==0 || zPathInfo[0]==0 ){
    const char *zUri;
    char *zBase;
    zUri = PD("REQUEST_URI","/");
    for(i=0; zUri[i] && zUri[i]!='?' && zUri[i]!='#'; i++){}
    for(j=i; j>0 && zUri[j-1]!='/'; j--){}
    zBase = mprintf("%.*s/index", i-j, &zUri[j]);
    cgi_redirect(zBase);
    cgi_reply();
    return;
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

    /* CGI parameters get this treatment elsewhere, but places like getfile
    ** will use g.zExtra directly.
    */
    dehttpize(g.zExtra);
  }else{
    g.zExtra = 0;
  }
  g.zBaseURL = get_base_url();

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
  if( g.argc!=2 && g.argc!=3 ){
    cgi_panic("no repository specified");
  }
  g.cgiPanic = 1;
  if( g.argc==3 ){
    db_open_repository(g.argv[2]);
  }else{
    db_must_be_within_tree();
  }
  cgi_handle_http_request();
  process_one_web_page();
}

/*
** COMMAND: server
**
** Usage: %fossil server ?-P|--port TCPPORT? ?REPOSITORY?
**
** Open a socket and begin listening and responding to HTTP requests on
** TCP port 8080, or on any other TCP port defined by the -P or
** --port option.  The optional argument is the name of the repository.
** The repository argument may be omitted if the working directory is
** within an open checkout.
*/
void cmd_webserver(void){
  int iPort;
  const char *zPort;

  zPort = find_option("port", "P", 1);
  if( zPort ){
    iPort = atoi(zPort);
  }else{
    iPort = 8080;
  }
  if( g.argc!=2 && g.argc!=3 ) usage("?REPOSITORY?");
  if( g.argc==2 ){
    db_must_be_within_tree();
    db_close();
  }
  cgi_http_server(iPort);
  if( g.fHttpTrace ){
    fprintf(stderr, "====== SERVER pid %d =======\n", getpid());
  }
  g.cgiPanic = 1;
  if( g.argc==2 ){
    db_must_be_within_tree();
  }else{
    db_open_repository(g.argv[2]);
  }
  cgi_handle_http_request();
  process_one_web_page();
}
