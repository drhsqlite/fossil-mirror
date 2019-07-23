/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code to implement the /aux webpage.
**
** The /aux webpage acts like a recursive webserver, relaying the
** HTTP request to some other component - usually another CGI.
**
** Before doing the relay, /aux examines the login cookie to see
** if the HTTP request is coming from a validaded user, and if so
** /aux sets some additional environment variables that the child
** CGI script can use.  In this way, the child CGI scripts use the
** same login system as the main repository, and appear to be
** an integrated part of the repository.
*/
#include "config.h"
#include "auxwww.h"
#include <assert.h>

/*
** WEBPAGE: aux
**
** Relay an HTTP request to secondary CGI after first checking the
** login credentials and setting auxiliary environment variables
** so that the secondary CGI can be aware of the credentials and
** capabilities of the Fossil user.
**
** The /aux page is only functional if the "auxroot: DIR" setting is
** found in the CGI script that launched Fossil, or if the "--auxroot DIR"
** flag is present when Fossil is lauched using the "server", "ui", or
** "http" commands.  DIR must be an absolute pathname (relative to the
** chroot jail) of the root of the file hierarchy that implements the CGI
** functionality.  Executable files are CGI.  Non-executable files are
** static content.
**
** The path after the /aux is the path to the CGI script or static file
** relative to DIR. For security, this path may not contain characters
** other than ASCII letters or digits, ".", "-", "/", and "_".  If the
** "." or "-" characters are present in the path then they may not follow
** a "/".
*/
void aux_page(void){
  const char *zName = P("name");  /* Path information after /aux */
  char *zPath = 0;                /* Complete path from auxroot */
  int nRoot;                      /* Number of bytes in the auxroot name */
  char *zScript = 0;              /* Name of the CGI script */
  int nScript = 0;                /* Bytes in the CGI script name */
  const char *zFailReason = "???";/* Reason for failure */
  int i;                          /* Loop counter */

  if( g.zAuxRoot==0 ){
    zFailReason = "auxroot is not set";
    goto aux_not_found;
  }
  if( file_is_absolute_path(g.zAuxRoot)==0 ){
    zFailReason = "auxroot is a relative pathname";
    goto aux_not_found;
  }
  if( zName==0 ){
    zFailReason = "no path beyond /aux";
    goto aux_not_found;
  }
  if( file_isdir(g.zAuxRoot,ExtFILE)!=1 ){
    zFailReason = "auxroot is not a directory";
    goto aux_not_found;
  }
  zPath = mprintf("%s/%s", g.zAuxRoot, zName);
  nRoot = (int)strlen(g.zAuxRoot);
  for(i=nRoot+1; zPath[i]; i++){
    char c = zPath[i];
    if( (c=='.' || c=='-') && zPath[i-1]=='/' ){
      zFailReason = "path element begins with '.' or '-'";
      goto aux_not_found;
    }
    if( !fossil_isalnum(c) && c!='_' && c!='-' && c!='.' ){
      zFailReason = "illegal character in path";
      goto aux_not_found;
    }
    if( c=='/' ){
      int isDir, isFile;
      zPath[i] = 0;
      isDir = file_isdir(zPath, ExtFILE);
      isFile = isDir==2 ? file_isfile(zPath, ExtFILE) : 0;
      zPath[i] = c;
      if( isDir==0 ){
        zFailReason = "path does not match any file or script";
        goto aux_not_found;
      }
      if( isFile!=0 ){
        zScript = mprintf("%.*s", i, zPath);
        nScript = i;
        break;
      }
    }
  }
  if( nScript==0 ){
    zFailReason = "path does not match any file or script";
    goto aux_not_found;
  }
  if( !file_isexe(zScript, ExtFILE) ){
    /* File is not executable.  Must be a regular file.  In that case,
    ** disallow extra path elements */
    if( zPath[nScript]!=0 ){
      zFailReason = "extra path elements after filename";
      goto aux_not_found;
    }
  }
  login_check_credentials();

aux_not_found:
  fossil_free(zPath);
  cgi_set_status(404, "Not Found");
  @ %h(zFailReason)
  return;
}
