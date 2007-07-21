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

/*
** Parse the given URL.  Populate variables in the global "g" structure.
**
**      g.urlIsFile      True if this is a file URL
**      g.urlName        Hostname for HTTP:.  Filename for FILE:
**      g.urlPort        Port name for HTTP.
**      g.urlPath        Path name for HTTP.
**      g.urlCanonical   The URL in canonical form
**
*/
void url_parse(const char *zUrl){
  int i, j, c;
  char *zFile;
  if( strncmp(zUrl, "http:", 5)==0 ){
    g.urlIsFile = 0;
    for(i=7; (c=zUrl[i])!=0 && c!=':' && c!='/'; i++){}
    g.urlName = mprintf("%.*s", i-7, &zUrl[7]);
    for(j=0; g.urlName[j]; j++){ g.urlName[j] = tolower(g.urlName[j]); }
    if( c==':' ){
      g.urlPort = 0;
      i++;
      while( (c = zUrl[i])!=0 && isdigit(c) ){
        g.urlPort = g.urlPort*10 + c - '0';
        i++;
      }
    }else{
      g.urlPort = 80;
    }
    g.urlPath = mprintf(&zUrl[i]);
    dehttpize(g.urlName);
    dehttpize(g.urlPath);
    g.urlCanonical = mprintf("http://%T:%d%T", g.urlName, g.urlPort, g.urlPath);
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
