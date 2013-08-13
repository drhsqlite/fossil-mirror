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
** This module contains code to implement CGI, HTTP, SCGI, and FastCGI
** servers.
*/
#include "config.h"
#include "server.h"
#include <sys/types.h>
#include <sys/stat.h>
#if defined(_WIN32)
#  include <windows.h>
#else
#  include <errno.h> /* errno global */
#endif

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
char *enter_chroot_jail(char *zRepo){
#if !defined(_WIN32)
  if( getuid()==0 ){
    int i;
    struct stat sStat;
    Blob dir;
    char *zDir;

    file_canonical_name(zRepo, &dir, 0);
    zDir = blob_str(&dir);
    if( file_isdir(zDir)==1 ){
      if( file_chdir(zDir, 1) ){
        fossil_fatal("unable to chroot into %s", zDir);
      }
      zRepo = "/";
    }else{
      for(i=strlen(zDir)-1; i>0 && zDir[i]!='/'; i--){}
      if( zDir[i]!='/' ) fossil_panic("bad repository name: %s", zRepo);
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
    if( g.db!=0 ){
      db_close(1);
      db_open_repository(zRepo);
    }
  }
#endif
  return zRepo;
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
**
** See also: cgi, server, winsrv
*/
void cmd_http(void){
  const char *zIpAddr;
  const char *zNotFound;
  const char *zHost;
  const char *zAltBase;
  const char *zFileGlob;

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
  find_server_repository(0);
  g.zRepositoryName = enter_chroot_jail(g.zRepositoryName);
  cgi_handle_http_request(zIpAddr);
  process_one_web_page(zNotFound, glob_create(zFileGlob));
}

/*
** Note that the following command is used by ssh:// processing.
**
** COMMAND: test-http
** Works like the http command but gives setup permission to all users.
*/
void cmd_test_http(void){
  Th_InitTraceLog();
  login_set_capabilities("sx", 0);
  g.useLocalauth = 1;
  cgi_set_parameter("REMOTE_ADDR", "127.0.0.1");
  g.httpIn = stdin;
  g.httpOut = stdout;
  find_server_repository(0);
  g.cgiOutput = 1;
  g.fullHttpReply = 1;
  cgi_handle_http_request(0);
  process_one_web_page(0, 0);
}


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
  cgi_handle_http_request(0);
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
