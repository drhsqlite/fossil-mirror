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
void hyperlinked_path(const char *zPath, Blob *pOut, const char *zCI){
  int i, j;
  char *zSep = "";

  for(i=0; zPath[i]; i=j){
    for(j=i; zPath[j] && zPath[j]!='/'; j++){}
    if( zPath[j] && g.okHistory ){
      if( zCI ){
        blob_appendf(pOut, "%s<a href=\"%s/dir?ci=%S&amp;name=%#T\">%#h</a>", 
                     zSep, g.zTop, zCI, j, zPath, j-i, &zPath[i]);
      }else{
        blob_appendf(pOut, "%s<a href=\"%s/dir?name=%#T\">%#h</a>", 
                     zSep, g.zTop, j, zPath, j-i, &zPath[i]);
      }
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
  int nD = zD ? strlen(zD)+1 : 0;
  int mxLen;
  int nCol, nRow;
  int cnt, i;
  char *zPrefix;
  Stmt q;
  const char *zCI = P("ci");
  int rid = 0;
  char *zUuid = 0;
  Blob dirname;
  Manifest *pM = 0;
  const char *zSubdirLink;

  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  style_header("File List");
  sqlite3_create_function(g.db, "pathelement", 2, SQLITE_UTF8, 0,
                          pathelementFunc, 0, 0);

  /* If the name= parameter is an empty string, make it a NULL pointer */
  if( zD && strlen(zD)==0 ){ zD = 0; }

  /* If a specific check-in is requested, fetch and parse it.  If the
  ** specific check-in does not exist, clear zCI.  zCI==0 will cause all
  ** files from all check-ins to be displayed.
  */
  if( zCI ){
    pM = manifest_get_by_name(zCI, &rid);
    if( pM ){
      zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    }else{
      zCI = 0;
    }
  }


  /* Compute the title of the page */  
  blob_zero(&dirname);
  if( zD ){
    blob_append(&dirname, "in directory ", -1);
    hyperlinked_path(zD, &dirname, zCI);
    zPrefix = mprintf("%h/", zD);
  }else{
    blob_append(&dirname, "in the top-level directory", -1);
    zPrefix = "";
  }
  if( zCI ){
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
      style_submenu_element("All", "All", "%s/dir", g.zTop);
    }
  }else{
    int hasTrunk;
    @ <h2>The union of all files from all check-ins
    @ %s(blob_str(&dirname))</h2>
    hasTrunk = db_exists(
                  "SELECT 1 FROM tagxref WHERE tagid=%d AND value='trunk'",
                  TAG_BRANCH);
    zSubdirLink = mprintf("%s/dir?name=%T", g.zTop, zPrefix);
    if( zD ){
      style_submenu_element("Top", "Top", "%s/dir", g.zTop);
      style_submenu_element("Tip", "Tip", "%s/dir?name=%t&amp;ci=tip",
                            g.zTop, zD);
      if( hasTrunk ){
        style_submenu_element("Trunk", "Trunk", "%s/dir?name=%t&amp;ci=trunk",
                               g.zTop,zD);
      }
    }else{
      style_submenu_element("Tip", "Tip", "%s/dir?ci=tip", g.zTop);
      if( hasTrunk ){
        style_submenu_element("Trunk", "Trunk", "%s/dir?ci=trunk", g.zTop);
      }
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
  );
  if( zCI ){
    Stmt ins;
    ManifestFile *pFile;
    ManifestFile *pPrev = 0;
    int nPrev = 0;
    int c;

    db_prepare(&ins,
       "INSERT OR IGNORE INTO localfiles VALUES(pathelement(:x,0), :u)"
    );
    manifest_file_rewind(pM);
    while( (pFile = manifest_file_next(pM,0))!=0 ){
      if( nD>0 && memcmp(pFile->zName, zD, nD-1)!=0 ) continue;
      if( pPrev
       && memcmp(&pFile->zName[nD],&pPrev->zName[nD],nPrev)==0
       && (pFile->zName[nD+nPrev]==0 || pFile->zName[nD+nPrev]=='/')
      ){
        continue;
      }
      db_bind_text(&ins, ":x", &pFile->zName[nD]);
      db_bind_text(&ins, ":u", pFile->zUuid);
      db_step(&ins);
      db_reset(&ins);
      pPrev = pFile;
      for(nPrev=0; (c=pPrev->zName[nD+nPrev]) && c!='/'; nPrev++){}
      if( c=='/' ) nPrev++;
    }
    db_finalize(&ins);
  }else if( zD ){
    db_multi_exec(
      "INSERT OR IGNORE INTO localfiles"
      " SELECT pathelement(name,%d), NULL FROM filename"
      "  WHERE name GLOB '%q/*'",
      nD, zD
    );
  }else{
    db_multi_exec(
      "INSERT OR IGNORE INTO localfiles"
      " SELECT pathelement(name,0), NULL FROM filename"
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
      @ <li><a href="%s(g.zTop)/artifact?name=%s(zUuid)">%h(zFN)</a></li>
    }else{
      @ <li><a href="%s(g.zTop)/finfo?name=%T(zPrefix)%T(zFN)">%h(zFN)
      @     </a></li>
    }
  }
  db_finalize(&q);
  manifest_destroy(pM);
  @ </ul></td></tr></table>
  style_footer();
}
