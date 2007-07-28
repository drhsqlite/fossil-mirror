/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code for parsing URLs that appear on the command-line
*/
#include "config.h"
#include "url.h"

/* Parse a URI authority. The parsed syntax is:
**
**     [<username> : <password> @] <hostname> [: <port>]
**
** TODO: If the input string does not match this pattern, results are
** undefined (but should not crash or anything nasty like that).
*/
void url_parse_authority(char const *zUri, int *pIdx){
  char *zUser = 0;
  char *zPass = 0;
  char *zHost = 0;
  int iPort = 80;

  int iFirst = *pIdx;
  int iColon = -1;
  int ii;

  /* Scan for the magic "@". If the authority contains this character,
  ** then we need to parse a username and password.
  */
  for(ii=iFirst; zUri[ii] && zUri[ii]!='@' && zUri[ii]!= '/'; ii++){
    if( zUri[ii]==':' ) iColon = ii;
  }

  /* Parse the username and (optional) password. */
  if( zUri[ii]=='@' ){
    if( iColon>=0 ){
      zUser = mprintf("%.*s", iColon-iFirst, &zUri[iFirst]);
      zPass = mprintf("%.*s", ii-(iColon+1), &zUri[iColon+1]);
    }else{
      zUser = mprintf("%.*s", ii-iFirst, &zUri[iFirst]);
    }
    iFirst = ii+1;
  }

  /* Parse the hostname. */
  for(ii=iFirst; zUri[ii] && zUri[ii]!=':' && zUri[ii]!= '/'; ii++);
  zHost = mprintf("%.*s", ii-iFirst, &zUri[iFirst]);

  /* Parse the port number, if one is specified. */
  if( zUri[ii]==':' ){
    iPort = atoi(&zUri[ii+1]);
    for(ii=iFirst; zUri[ii] && zUri[ii]!= '/'; ii++);
  }

  /* Set the g.urlXXX variables to the parsed values. */
  dehttpize(zUser);
  dehttpize(zPass);
  dehttpize(zHost);
  g.urlUsername = zUser;
  g.urlPassword = zPass;
  g.urlName = zHost;
  g.urlPort = iPort;

  *pIdx = ii;
}

/*
** Based on the values already stored in the other g.urlXXX variables,
** set the g.urlCanonical variable.
*/
void url_set_canon(){
  g.urlCanonical = mprintf("http://%T%s%T%s%T:%d%T", 
    (g.urlUsername ? g.urlUsername : ""),
    (g.urlPassword ? ":" : ""),
    (g.urlPassword ? g.urlPassword : ""),
    (g.urlUsername ? "@" : ""),
    g.urlName, g.urlPort, g.urlPath
  );
  /* printf("%s\n", g.urlCanonical); */
}

/*
** Parse the given URL.  Populate variables in the global "g" structure.
**
**      g.urlIsFile      True if this is a file URL
**      g.urlName        Hostname for HTTP:.  Filename for FILE:
**      g.urlPort        Port name for HTTP.
**      g.urlPath        Path name for HTTP.
**      g.urlCanonical   The URL in canonical form
**
** If g.uriIsFile is false, indicating an http URI, then the following
** variables are also populated:
**
**      g.urlUsername
**      g.urlPassword
**
** TODO: At present, the only way to specify a username is to pass it
** as part of the URI. In the future, if no password is specified, 
** fossil should use the get_passphrase() routine (user.c) to obtain
** a password from the user.
*/
void url_parse(const char *zUrl){
  int i, j, c;
  char *zFile;
  if( strncmp(zUrl, "http:", 5)==0 ){
    g.urlIsFile = 0;

    i = 7;
    url_parse_authority(zUrl, &i);
    g.urlPath = mprintf(&zUrl[i]);
    dehttpize(g.urlPath);
    url_set_canon();

  }else if( strncmp(zUrl, "file:", 5)==0 ){
    g.urlIsFile = 1;
    if( zUrl[5]=='/' && zUrl[6]=='/' ){
      i = 7;
    }else{
      i = 5;
    }
    zFile = mprintf("%s", &zUrl[i]);
  }else if( file_isfile(zUrl) ){
    g.urlIsFile = 1;
    zFile = mprintf("%s", zUrl);
  }else if( file_isdir(zUrl)==1 ){
    zFile = mprintf("%s/FOSSIL", zUrl);
    if( file_isfile(zFile) ){
      g.urlIsFile = 1;
    }else{
      free(zFile);
      fossil_panic("unknown repository: %s", zUrl);
    }
  }else{
    fossil_panic("unknown repository: %s", zUrl);
  }
  if( g.urlIsFile ){
    Blob cfile;
    dehttpize(zFile);  
    file_canonical_name(zFile, &cfile);
    free(zFile);
    g.urlName = mprintf("%b", &cfile);
    g.urlCanonical = mprintf("file://%T", g.urlName);
    blob_reset(&cfile);
  }
}
