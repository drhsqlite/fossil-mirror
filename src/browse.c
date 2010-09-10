/*
** Copyright (c) 2008 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

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
** This file contains code to implement the file browser web interface.
*/
#include "config.h"
#include "browse.h"
#include <assert.h>

/*
** This is the implemention of the "pathelement(X,N)" SQL function.
**
** If X is a unix-like pathname (with "/" separators) and N is an
** integer, then skip the initial N characters of X and return the
** name of the path component that begins on the N+1th character
** (numbered from 0).  If the path component is a directory (if
** it is followed by other path components) then prepend "/".
**
** Examples:
**
**      pathelement('abc/pqr/xyz', 4)  ->  '/pqr'
**      pathelement('abc/pqr', 4)      ->  'pqr'
**      pathelement('abc/pqr/xyz', 0)  ->  '/abc'
*/
static void pathelementFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *z;
  int len, n, i;
  char *zOut;

  assert( argc==2 );
  z = sqlite3_value_text(argv[0]);
  if( z==0 ) return;
  len = sqlite3_value_bytes(argv[0]);
  n = sqlite3_value_int(argv[1]);
  if( len<=n ) return;
  if( n>0 && z[n-1]!='/' ) return;
  for(i=n; i<len && z[i]!='/'; i++){}
  if( i==len ){
    sqlite3_result_text(context, (char*)&z[n], len-n, SQLITE_TRANSIENT);
  }else{
    zOut = sqlite3_mprintf("/%.*s", i-n, &z[n]);
    sqlite3_result_text(context, zOut, i-n+1, sqlite3_free);
  }
}

/*
** Given a pathname which is a relative path from the root of
** the repository to a file or directory, compute a string which
** is an HTML rendering of that path with hyperlinks on each
** directory component of the path where the hyperlink redirects
** to the "dir" page for the directory.
**
** There is no hyperlink on the file element of the path.
**
** The computed string is appended to the pOut blob.  pOut should
** have already been initialized.
*/
void hyperlinked_path(const char *zPath, Blob *pOut){
  int i, j;
  char *zSep = "";

  for(i=0; zPath[i]; i=j){
    for(j=i; zPath[j] && zPath[j]!='/'; j++){}
    if( zPath[j] && g.okHistory ){
      blob_appendf(pOut, "%s<a href=\"%s/dir?name=%#T\">%#h</a>", 
                   zSep, g.zBaseURL, j, zPath, j-i, &zPath[i]);
    }else{
      blob_appendf(pOut, "%s%#h", zSep, j-i, &zPath[i]);
    }
    zSep = "/";
    while( zPath[j]=='/' ){ j++; }
  }
}


/*
** WEBPAGE: dir
**
** Query parameters:
**
**    name=PATH        Directory to display.  Required.
**    ci=LABEL         Show only files in this check-in.  Optional.
*/
void page_dir(void){
  const char *zD = P("name");
  int mxLen;
  int nCol, nRow;
  int cnt, i;
  char *zPrefix;
  Stmt q;
  const char *zCI = P("ci");
  int rid = 0;
  Blob content;
  Blob dirname;
  Manifest m;
  const char *zSubdirLink;

  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  style_header("File List");
  sqlite3_create_function(g.db, "pathelement", 2, SQLITE_UTF8, 0,
                          pathelementFunc, 0, 0);

  /* If the name= parameter is an empty string, make it a NULL pointer */
  if( zD && strlen(zD)==0 ){ zD = 0; }

  /* If a specific check-in is requested, fetch and parse it. */
  if( zCI && (rid = name_to_rid(zCI))!=0 && content_get(rid, &content) ){
    if( !manifest_parse(&m, &content) || m.type!=CFTYPE_MANIFEST ){
      zCI = 0;
    }
  }

  /* Compute the title of the page */  
  blob_zero(&dirname);
  if( zD ){
    blob_append(&dirname, "in directory ", -1);
    hyperlinked_path(zD, &dirname);
    zPrefix = mprintf("%h/", zD);
  }else{
    blob_append(&dirname, "in the top-level directory", -1);
    zPrefix = "";
  }
  if( zCI ){
    char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    char zShort[20];
    memcpy(zShort, zUuid, 10);
    zShort[10] = 0;
    @ <h2>Files of check-in [<a href="vinfo?name=%T(zUuid)">%s(zShort)</a>]
    @ %s(blob_str(&dirname))</h2>
    zSubdirLink = mprintf("%s/dir?ci=%S&amp;name=%T", g.zTop, zUuid, zPrefix);
    if( zD ){
      style_submenu_element("Top", "Top", "%s/dir?ci=%S", g.zTop, zUuid);
      style_submenu_element("All", "All", "%s/dir?name=%t", g.zTop, zD);
    }else{
      style_submenu_element("All", "All", "%s/dir", g.zBaseURL);
    }
  }else{
    @ <h2>The union of all files from all check-ins
    @ %s(blob_str(&dirname))</h2>
    zSubdirLink = mprintf("%s/dir?name=%T", g.zBaseURL, zPrefix);
    if( zD ){
      style_submenu_element("Top", "Top", "%s/dir", g.zBaseURL);
      style_submenu_element("Tip", "Tip", "%s/dir?name=%t&ci=tip",
                            g.zBaseURL, zD);
      style_submenu_element("Trunk", "Trunk", "%s/dir?name=%t&ci=trunk",
                             g.zBaseURL,zD);
    }else{
      style_submenu_element("Tip", "Tip", "%s/dir?ci=tip", g.zBaseURL);
      style_submenu_element("Trunk", "Trunk", "%s/dir?ci=trunk", g.zBaseURL);
    }
  }

  /* Compute the temporary table "localfiles" containing the names
  ** of all files and subdirectories in the zD[] directory.  
  **
  ** Subdirectory names begin with "/".  This causes them to sort
  ** first and it also gives us an easy way to distinguish files
  ** from directories in the loop that follows.
  */
  db_multi_exec(
     "CREATE TEMP TABLE localfiles(x UNIQUE NOT NULL, u);"
     "CREATE TEMP TABLE allfiles(x UNIQUE NOT NULL, u);"
  );
  if( zCI ){
    Stmt ins;
    int i;
    db_prepare(&ins, "INSERT INTO allfiles VALUES(:x, :u)");
    for(i=0; i<m.nFile; i++){
      db_bind_text(&ins, ":x", m.aFile[i].zName);
      db_bind_text(&ins, ":u", m.aFile[i].zUuid);
      db_step(&ins);
      db_reset(&ins);
    }
    db_finalize(&ins);
  }else{
    db_multi_exec(
      "INSERT INTO allfiles SELECT name, NULL FROM filename"
    );
  }
  if( zD ){
    db_multi_exec(
       "INSERT OR IGNORE INTO localfiles "
       "  SELECT pathelement(x,%d), u FROM allfiles"
       "   WHERE x GLOB '%q/*'",
       strlen(zD)+1, zD
    );
  }else{
    db_multi_exec(
       "INSERT OR IGNORE INTO localfiles "
       "  SELECT pathelement(x,0), u FROM allfiles"
    );
  }

  /* Generate a multi-column table listing the contents of zD[]
  ** directory.
  */
  mxLen = db_int(12, "SELECT max(length(x)) FROM localfiles /*scan*/");
  cnt = db_int(0, "SELECT count(*) FROM localfiles /*scan*/");
  nCol = 4;
  nRow = (cnt+nCol-1)/nCol;
  db_prepare(&q, "SELECT x, u FROM localfiles ORDER BY x /*scan*/");
  @ <table class="browser"><tr><td class="browser"><ul class="browser">
  i = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFN;
    if( i==nRow ){
      @ </ul></td><td class="browser"><ul class="browser">
      i = 0;
    }
    i++;
    zFN = db_column_text(&q, 0);
    if( zFN[0]=='/' ){
      zFN++;
      @ <li><a href="%s(zSubdirLink)%T(zFN)">%h(zFN)/</a></li>
    }else if( zCI ){
      const char *zUuid = db_column_text(&q, 1);
      @ <li><a href="%s(g.zBaseURL)/artifact?name=%s(zUuid)">%h(zFN)</a></li>
    }else{
      @ <li><a href="%s(g.zBaseURL)/finfo?name=%T(zPrefix)%T(zFN)">%h(zFN)</a></li>
    }
  }
  db_finalize(&q);
  @ </ul></td></tr></table>
  style_footer();
}
