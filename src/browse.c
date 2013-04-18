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
** This is the implementation of the "pathelement(X,N)" SQL function.
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
void pathelementFunc(
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
    if( zPath[j] && g.perm.Hyperlink ){
      if( zCI ){
        char *zLink = href("%R/dir?ci=%S&name=%#T", zCI, j, zPath);
        blob_appendf(pOut, "%s%z%#h</a>", 
                     zSep, zLink, j-i, &zPath[i]);
      }else{
        char *zLink = href("%R/dir?name=%#T", j, zPath);
        blob_appendf(pOut, "%s%z%#h</a>", 
                     zSep, zLink, j-i, &zPath[i]);
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
  char *zD = fossil_strdup(P("name"));
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
  if( !g.perm.Read ){ login_needed(); return; }
  while( nD>1 && zD[nD-2]=='/' ){ zD[(--nD)-1] = 0; }
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
    zPrefix = mprintf("%s/", zD);
  }else{
    blob_append(&dirname, "in the top-level directory", -1);
    zPrefix = "";
  }
  if( zCI ){
    char zShort[20];
    memcpy(zShort, zUuid, 10);
    zShort[10] = 0;
    @ <h2>Files of check-in [%z(href("vinfo?name=%T",zUuid))%s(zShort)</a>]
    @ %s(blob_str(&dirname))</h2>
    zSubdirLink = mprintf("%R/dir?ci=%S&name=%T", zUuid, zPrefix);
    if( zD ){
      style_submenu_element("Top", "Top", "%R/dir?ci=%S", zUuid);
      style_submenu_element("All", "All", "%R/dir?name=%t", zD);
    }else{
      style_submenu_element("All", "All", "%R/dir");
      style_submenu_element("File Ages", "File Ages", "%R/fileage?name=%S",
                            zUuid);
    }
  }else{
    int hasTrunk;
    @ <h2>The union of all files from all check-ins
    @ %s(blob_str(&dirname))</h2>
    hasTrunk = db_exists(
                  "SELECT 1 FROM tagxref WHERE tagid=%d AND value='trunk'",
                  TAG_BRANCH);
    zSubdirLink = mprintf("%R/dir?name=%T", zPrefix);
    if( zD ){
      style_submenu_element("Top", "Top", "%R/dir");
      style_submenu_element("Tip", "Tip", "%R/dir?name=%t&ci=tip", zD);
      if( hasTrunk ){
        style_submenu_element("Trunk", "Trunk", "%R/dir?name=%t&ci=trunk",
                               zD);
      }
    }else{
      style_submenu_element("Tip", "Tip", "%R/dir?ci=tip");
      if( hasTrunk ){
        style_submenu_element("Trunk", "Trunk", "%R/dir?ci=trunk");
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
     "CREATE TEMP TABLE localfiles(x UNIQUE NOT NULL %s, u);",
     filename_collation()
  );
  if( zCI ){
    Stmt ins;
    ManifestFile *pFile;
    ManifestFile *pPrev = 0;
    int nPrev = 0;
    int c;
    int (*xCmp)(const char*,const char*,int);

    if( filenames_are_case_sensitive() ){
      xCmp = fossil_strncmp;
    }else{
      xCmp = fossil_strnicmp;
    }

    db_prepare(&ins,
       "INSERT OR IGNORE INTO localfiles VALUES(pathelement(:x,0), :u)"
    );
    manifest_file_rewind(pM);
    while( (pFile = manifest_file_next(pM,0))!=0 ){
      if( nD>0 
       && (xCmp(pFile->zName, zD, nD-1)!=0
           || pFile->zName[nD-1]!='/')
      ){
        continue;
      }
      if( pPrev
       && xCmp(&pFile->zName[nD],&pPrev->zName[nD],nPrev)==0
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
    if( filenames_are_case_sensitive() ){
      db_multi_exec(
        "INSERT OR IGNORE INTO localfiles"
        " SELECT pathelement(name,%d), NULL FROM filename"
        "  WHERE name GLOB '%q/*'",
        nD, zD
      );
    }else{
      db_multi_exec(
        "INSERT OR IGNORE INTO localfiles"
        " SELECT pathelement(name,%d), NULL FROM filename"
        "  WHERE name LIKE '%q/%%'",
        nD, zD
      );
    }
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
  if( mxLen<12 ) mxLen = 12;
  nCol = 100/mxLen;
  if( nCol<1 ) nCol = 1;
  if( nCol>5 ) nCol = 5;
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
      @ <li>%z(href("%s%T",zSubdirLink,zFN))%h(zFN)</a></li>
    }else if( zCI ){
      const char *zUuid = db_column_text(&q, 1);
      @ <li>%z(href("%R/artifact/%s",zUuid))%h(zFN)</a></li>
    }else{
      @ <li>%z(href("%R/finfo?name=%T%T",zPrefix,zFN))%h(zFN)
      @     </a></li>
    }
  }
  db_finalize(&q);
  manifest_destroy(pM);
  @ </ul></td></tr></table>
  style_footer();
}

/*
** Look at all file containing in the version "vid".  Construct a
** temporary table named "fileage" that contains the file-id for each
** files, the pathname, the check-in where the file was added, and the
** mtime on that checkin.
*/
int compute_fileage(int vid){
  Manifest *pManifest;
  ManifestFile *pFile;
  int nFile = 0;
  double vmtime;
  Stmt ins;
  Stmt q1, q2, q3;
  Stmt upd;
  db_multi_exec(
    /*"DROP TABLE IF EXISTS temp.fileage;"*/
    "CREATE TEMP TABLE fileage("
    "  fid INTEGER,"
    "  mid INTEGER,"
    "  mtime DATETIME,"
    "  pathname TEXT"
    ");"
    "CREATE INDEX fileage_fid ON fileage(fid);"
  );
  pManifest = manifest_get(vid, CFTYPE_MANIFEST);
  if( pManifest==0 ) return 1;
  manifest_file_rewind(pManifest);
  db_prepare(&ins, 
     "INSERT INTO temp.fileage(fid, pathname)"
     "  SELECT rid, :path FROM blob WHERE uuid=:uuid"
  );
  while( (pFile = manifest_file_next(pManifest, 0))!=0 ){
    db_bind_text(&ins, ":uuid", pFile->zUuid);
    db_bind_text(&ins, ":path", pFile->zName);
    db_step(&ins);
    db_reset(&ins);
    nFile++;
  }
  db_finalize(&ins);
  manifest_destroy(pManifest);
  db_prepare(&q1,"SELECT fid FROM mlink WHERE mid=:mid");
  db_prepare(&upd, "UPDATE fileage SET mid=:mid, mtime=:vmtime"
                      " WHERE fid=:fid AND mid IS NULL");
  db_prepare(&q2,"SELECT pid FROM plink WHERE cid=:vid AND isprim");
  db_prepare(&q3,"SELECT mtime FROM event WHERE objid=:vid");
  while( nFile>0 && vid>0 ){
    db_bind_int(&q3, ":vid", vid);
    if( db_step(&q3)==SQLITE_ROW ){
      vmtime = db_column_double(&q3, 0);
    }else{
      break;
    }
    db_reset(&q3);
    db_bind_int(&q1, ":mid", vid);
    db_bind_int(&upd, ":mid", vid);
    db_bind_double(&upd, ":vmtime", vmtime);
    while( db_step(&q1)==SQLITE_ROW ){
      db_bind_int(&upd, ":fid", db_column_int(&q1, 0));
      db_step(&upd);
      nFile -= db_changes();
      db_reset(&upd);
    }
    db_reset(&q1);
    db_bind_int(&q2, ":vid", vid);
    if( db_step(&q2)!=SQLITE_ROW ) break;
    vid = db_column_int(&q2, 0);
    db_reset(&q2);
  }
  db_finalize(&q1);
  db_finalize(&upd);
  db_finalize(&q2);
  db_finalize(&q3);
  return 0;
}

/*
** WEBPAGE:  fileage
**
** Parameters:
**   name=VERSION
*/
void fileage_page(void){
  int rid;
  const char *zName;
  char *zBaseTime;
  Stmt q;
  double baseTime;
  int lastMid = -1;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }
  zName = P("name");
  if( zName==0 ) zName = "tip";
  rid = symbolic_name_to_rid(zName, "ci");
  if( rid==0 ){
    fossil_fatal("not a valid check-in: %s", zName);
  }
  style_header("File Ages", zName);
  compute_fileage(rid);
  baseTime = db_double(0.0, "SELECT mtime FROM event WHERE objid=%d", rid);
  zBaseTime = db_text("","SELECT datetime(%.20g,'localtime')", baseTime);
  @ <h2>File Ages For Check-in
  @ %z(href("%R/info?name=%T",zName))%h(zName)</a></h2>
  @
  @ <p>The times given are relative 
  @ %z(href("%R/timeline?c=%T",zBaseTime))%s(zBaseTime)</a>, which is the
  @ check-in time for 
  @ %z(href("%R/info?name=%T",zName))%h(zName)</a></p>
  @
  @ <table border=0 cellspacing=0 cellpadding=0>
  db_prepare(&q,
    "SELECT mtime, (SELECT uuid FROM blob WHERE rid=fid), mid, pathname"
    "  FROM fileage"
    " ORDER BY mtime DESC, mid, pathname"
  );
  while( db_step(&q)==SQLITE_ROW ){
    double age = baseTime - db_column_double(&q, 0);
    int mid = db_column_int(&q, 2);
    const char *zFUuid = db_column_text(&q, 1);
    char zAge[200];
    if( lastMid!=mid ){
      @ <tr><td colspan=3><hr></tr>
      lastMid = mid;
      if( age*86400.0<120 ){
        sqlite3_snprintf(sizeof(zAge), zAge, "%d seconds", (int)(age*86400.0));
      }else if( age*1440.0<90 ){
        sqlite3_snprintf(sizeof(zAge), zAge, "%.1f minutes", age*1440.0);
      }else if( age*24.0<36 ){
        sqlite3_snprintf(sizeof(zAge), zAge, "%.1f hours", age*24.0);
      }else if( age<365.0 ){
        sqlite3_snprintf(sizeof(zAge), zAge, "%.1f days", age);
      }else{
        sqlite3_snprintf(sizeof(zAge), zAge, "%.2f years", age/365.0);
      }
    }else{
      zAge[0] = 0;
    }
    @ <tr>
    @ <td>%s(zAge)
    @ <td width="25">
    @ <td>%z(href("%R/artifact/%S?ln", zFUuid))%h(db_column_text(&q, 3))</a>
    @ </tr>
    @
  }
  @ <tr><td colspan=3><hr></tr>
  @ </table>
  db_finalize(&q);
  style_footer();  
}
