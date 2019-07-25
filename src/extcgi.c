/*
** Copyright (c) 2019 D. Richard Hipp
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
**   drh@sqlite.org
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code to invoke CGI-based extensions to the
** Fossil server via the /ext webpage.
**
** The /ext webpage acts like a recursive webserver, relaying the
** HTTP request to some other component - usually another CGI.
**
** Before doing the relay, /ext examines the login cookie to see
** if the HTTP request is coming from a validated user, and if so
** /ext sets some additional environment variables that the extension
** CGI script can use.  In this way, the extension CGI scripts use the
** same login system as the main repository, and appear to be
** an integrated part of the repository.
*/
#include "config.h"
#include "extcgi.h"
#include <assert.h>

#if defined(_WIN32) || defined(WIN32)
# undef popen
# define popen _popen
# undef pclose
# define pclose _pclose
#endif

/*
** These are the environment variables that should be set for CGI
** extension programs:
*/
static const char *azCgiEnv[] = {
   "AUTH_TYPE",
   "AUTH_CONTENT",
   "CONTENT_LENGTH",
   "CONTENT_TYPE",
   "DOCUMENT_ROOT",
   "FOSSIL_CAPABILITIES",
   "FOSSIL_REPOSITORY",
   "FOSSIL_USER",
   "GATEWAY_INTERFACE",
   "HTTP_ACCEPT",
   /* "HTTP_ACCEPT_ENCODING", // omitted from sub-cgi */
   "HTTP_COOKIE",
   "HTTP_HOST",
   "HTTP_IF_MODIFIED_SINCE",
   "HTTP_IF_NONE_MATCH",
   "HTTP_REFERER",
   "HTTP_USER_AGENT",
   "PATH_INFO",
   "QUERY_STRING",
   "REMOTE_ADDR",
   "REMOTE_USER",
   "REQUEST_METHOD",
   "REQUEST_URI",
   "SCRIPT_DIRECTORY",
   "SCRIPT_FILENAME",
   "SCRIPT_NAME",
   "SERVER_NAME",
   "SERVER_PORT",
   "SERVER_PROTOCOL",
};

/*
** WEBPAGE: ext  raw-content
**
** Relay an HTTP request to secondary CGI after first checking the
** login credentials and setting auxiliary environment variables
** so that the secondary CGI can be aware of the credentials and
** capabilities of the Fossil user.
**
** The /ext page is only functional if the "extroot: DIR" setting is
** found in the CGI script that launched Fossil, or if the "--extroot DIR"
** flag is present when Fossil is lauched using the "server", "ui", or
** "http" commands.  DIR must be an absolute pathname (relative to the
** chroot jail) of the root of the file hierarchy that implements the CGI
** functionality.  Executable files are CGI.  Non-executable files are
** static content.
**
** The path after the /ext is the path to the CGI script or static file
** relative to DIR. For security, this path may not contain characters
** other than ASCII letters or digits, ".", "-", "/", and "_".  If the
** "." or "-" characters are present in the path then they may not follow
** a "/".
*/
void ext_page(void){
  const char *zName = P("name");  /* Path information after /ext */
  char *zPath = 0;                /* Complete path from extroot */
  int nRoot;                      /* Number of bytes in the extroot name */
  int nName;                      /* Length of zName */
  char *zScript = 0;              /* Name of the CGI script */
  int nScript = 0;                /* Bytes in the CGI script name */
  const char *zFailReason = "???";/* Reason for failure */
  int i;                          /* Loop counter */
  const char *zMime = 0;          /* MIME type of the reply */
  int fdFromChild = -1;           /* File descriptor for reading from child */
  FILE *toChild = 0;              /* FILE for sending to child */
  FILE *fromChild = 0;            /* FILE for reading from child */
  int pidChild = 0;               /* Process id of the child */
  int rc;                         /* Reply code from subroutine call */
  int nContent = -1;              /* Content length */
  Blob reply;                     /* The reply */
  char zLine[1000];               /* One line of the CGI reply */

  blob_init(&reply, 0, 0);
  if( g.zExtRoot==0 ){
    zFailReason = "extroot is not set";
    goto ext_not_found;
  }
  if( file_is_absolute_path(g.zExtRoot)==0 ){
    zFailReason = "extroot is a relative pathname";
    goto ext_not_found;
  }
  if( zName==0 ){
    zFailReason = "no path beyond /ext";
    goto ext_not_found;
  }
  if( file_isdir(g.zExtRoot,ExtFILE)!=1 ){
    zFailReason = "extroot is not a directory";
    goto ext_not_found;
  }
  zPath = mprintf("%s/%s", g.zExtRoot, zName);
  nRoot = (int)strlen(g.zExtRoot);
  nName = (int)strlen(zName);
  if( file_isfile(zPath, ExtFILE) ){
    nScript = (int)strlen(zPath);
    zScript = zPath;
  }else{
    for(i=nRoot+1; zPath[i]; i++){
      char c = zPath[i];
      if( (c=='.' || c=='-') && zPath[i-1]=='/' ){
        zFailReason = "path element begins with '.' or '-'";
        goto ext_not_found;
      }
      if( !fossil_isalnum(c) && c!='_' && c!='-' && c!='.' && c!='/' ){
        zFailReason = "illegal character in path";
        goto ext_not_found;
      }
      if( c=='/' ){
        int isDir, isFile;
        zPath[i] = 0;
        isDir = file_isdir(zPath, ExtFILE);
        isFile = isDir==2 ? file_isfile(zPath, ExtFILE) : 0;
        zPath[i] = c;
        if( isDir==0 ){
          zFailReason = "path does not match any file or script";
          goto ext_not_found;
        }
        if( isFile!=0 ){
          zScript = mprintf("%.*s", i, zPath);
          nScript = i;
          break;
        }
      }
    }
  }
  if( nScript==0 ){
    zFailReason = "path does not match any file or script";
    goto ext_not_found;
  }
  assert( nScript>=nRoot+1 );
  style_set_current_page("ext/%s", &zScript[nRoot+1]);
  zMime = mimetype_from_name(zScript);
  if( zMime==0 ) zMime = "application/octet-stream";
  if( !file_isexe(zScript, ExtFILE) ){
    /* File is not executable.  Must be a regular file.  In that case,
    ** disallow extra path elements */
    if( zPath[nScript]!=0 ){
      zFailReason = "extra path elements after filename";
      goto ext_not_found;
    }
    blob_read_from_file(&reply, zScript, ExtFILE);
    document_render(&reply, zMime, zName, zName);
    return;
  }

  /* If we reach this point, that means we are dealing with an executable
  ** file name zScript.  Run that file as CGI.
  */
  cgi_replace_parameter("DOCUMENT_ROOT", g.zExtRoot);
  cgi_replace_parameter("SCRIPT_FILENAME", zScript);
  cgi_replace_parameter("SCRIPT_NAME",
        mprintf("%T/ext/%T",g.zTop,zScript+nRoot+1));
  cgi_replace_parameter("SCRIPT_DIRECTORY", file_dirname(zScript));
  cgi_replace_parameter("PATH_INFO", zName + strlen(zScript+nRoot+1));
  login_check_credentials();
  if( g.zLogin ){
    cgi_replace_parameter("REMOTE_USER", g.zLogin);
    cgi_set_parameter_nocopy("FOSSIL_USER", g.zLogin, 0);
  }
  cgi_set_parameter_nocopy("FOSSIL_REPOSITORY", g.zRepositoryName, 0);
  cgi_set_parameter_nocopy("FOSSIL_CAPABILITIES",
     db_text("","SELECT fullcap(cap) FROM user WHERE login=%Q",
             g.zLogin ? g.zLogin : "nobody"), 0);
  cgi_replace_parameter("GATEWAY_INTERFACE","CGI/1.0");
  for(i=0; i<sizeof(azCgiEnv)/sizeof(azCgiEnv[0]); i++){
    const char *zVal = P(azCgiEnv[i]);
    if( zVal ) fossil_setenv(azCgiEnv[i], zVal);
  }
  fossil_setenv("HTTP_ACCEPT_ENCODING","");
  rc = popen2(zScript, &fdFromChild, &toChild, &pidChild, 1);
  if( rc ){
    zFailReason = "cannot exec CGI child process";
    goto ext_not_found;
  }
  fromChild = fdopen(fdFromChild, "rb");
  if( fromChild==0 ){
    zFailReason = "cannot open FILE to read from CGI child process";
    goto ext_not_found;
  }
  if( blob_size(&g.cgiIn)>0 ){
    size_t nSent, toSend;
    unsigned char *data = (unsigned char*)blob_buffer(&g.cgiIn);
    toSend = (size_t)blob_size(&g.cgiIn);
    do{
      nSent = fwrite(data, 1, toSend, toChild);
      if( nSent<=0 ){
        zFailReason = "unable to send all content to the CGI child process";
        goto ext_not_found;
      }
      toSend -= nSent;
      data += nSent;
    }while( toSend>0 );
    fflush(toChild);
  }
  if( g.perm.Debug && P("fossil-ext-debug")!=0 ){
    /* For users with Debug privilege, if the "fossil-ext-debug" query
    ** parameter exists, then show raw output from the CGI */
    zMime = "text/plain";
  }else{
    while( fgets(zLine,sizeof(zLine),fromChild) ){
      for(i=0; zLine[i] && zLine[i]!='\r' && zLine[i]!='\n'; i++){}
      zLine[i] = 0;
      if( i==0 ) break;
      if( fossil_strnicmp(zLine,"Location:",9)==0 ){
        fclose(fromChild);
        fclose(toChild);
        cgi_redirect(&zLine[10]); /* no return */
      }else if( fossil_strnicmp(zLine,"Status:",7)==0 ){
        int j;
        for(i=7; fossil_isspace(zLine[i]); i++){}
        for(j=i; fossil_isdigit(zLine[j]); j++){}
        while( fossil_isspace(zLine[j]) ){ j++; }
        cgi_set_status(atoi(&zLine[i]), &zLine[j]);
      }else if( fossil_strnicmp(zLine,"Content-Length:",15)==0 ){
        nContent = atoi(&zLine[15]);
      }else if( fossil_strnicmp(zLine,"Content-Type:",13)==0 ){
        int j;
        for(i=13; fossil_isspace(zLine[i]); i++){}
        for(j=i; zLine[j] && zLine[j]!=';'; j++){}
        zMime = mprintf("%.*s", j-i, &zLine[i]);
      }
    }
  }
  blob_read_from_channel(&reply, fromChild, nContent);
  zFailReason = 0;  /* Indicate success */

ext_not_found:
  fossil_free(zPath);
  if( fromChild ){
    fclose(fromChild);
  }else if( fdFromChild>2 ){
    close(fdFromChild);
  }
  if( toChild ) fclose(toChild);
  if( zFailReason==0 ){
    document_render(&reply, zMime, zName, zName);
  }else{
    cgi_set_status(404, "Not Found");
    @ %h(zFailReason)
  }
  return;
}
