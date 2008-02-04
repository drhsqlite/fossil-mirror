/*
** Copyright (c) 2008 D. Richard Hipp
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
** WEBPAGE: dir
**
** Query parameters:
**
**    name=PATH        Directory to display.  Required.
*/
void page_dir(void){
  const char *zD = P("name");
  int mxLen;
  int nCol, nRow;
  int cnt, i;
  char *zPrefix;
  Stmt q;

  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  style_header("File List");
  sqlite3_create_function(g.db, "pathelement", 2, SQLITE_UTF8, 0,
                          pathelementFunc, 0, 0);

  /* If the name= parameter is an empty string, make it a NULL pointer */
  if( zD && strlen(zD)==0 ){ zD = 0; }

  /* Compute the title of the page */  
  if( zD ){
    int i, j;
    char *zCopy;
    Blob title;

    blob_zero(&title);
    zCopy = sqlite3_mprintf("%s/", zD);
    blob_appendf(&title,
       "Files in directory <a href=\"%s/dir\"><i>root</i></a>",
       g.zBaseURL
    );
    for(i=0; zD[i]; i=j){
      for(j=i; zD[j] && zD[j]!='/'; j++){}
      if( zD[j] ){
        zCopy[j] = 0;
        blob_appendf(&title, "/<a href=\"%s/dir?name=%T\">%h</a>", 
                     g.zBaseURL, zCopy, &zCopy[i]);
        zCopy[j] = '/';
      }else{
        blob_appendf(&title, "/%h", &zCopy[i]);
      }
      while( zD[j]=='/' ){ j++; }
    }
    @ <h2>%s(blob_str(&title))</h2>
    blob_reset(&title);
    zPrefix = zCopy;
  }else{
    @ <h2>Files in the top-level directory</h2>
    zPrefix = "";
  }

  /* Compute the temporary table "localfiles" containing the names
  ** of all files and subdirectories in the zD[] directory.  
  **
  ** Subdirectory names begin with "/".  This causes them to sort
  ** first and it also gives us an easy way to distinguish files
  ** from directories in the loop that follows.
  */
  if( zD ){
    db_multi_exec(
       "CREATE TEMP TABLE localfiles(x UNIQUE NOT NULL);"
       "INSERT OR IGNORE INTO localfiles "
       "  SELECT pathelement(name,%d) FROM filename"
       "   WHERE +name GLOB '%q/*'",
       strlen(zD)+1, zD
    );
  }else{
    db_multi_exec(
       "CREATE TEMP TABLE localfiles(x UNIQUE NOT NULL);"
       "INSERT OR IGNORE INTO localfiles "
       "  SELECT pathelement(name,0) FROM filename"
    );
  }

  /* Generate a multi-column table listing the contents of zD[]
  ** directory.
  */
  mxLen = db_int(12, "SELECT max(length(x)) FROM localfiles");
  cnt = db_int(0, "SELECT count(*) FROM localfiles");
  nCol = 4;
  nRow = (cnt+nCol-1)/nCol;
  db_prepare(&q, "SELECT x FROM localfiles ORDER BY x");
  @ <table border="0" width="100%%"><tr><td valign="top" width="25%%">
  i = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFName;
    if( i==nRow ){
      @ </td><td valign="top" width="25%%">
      i = 0;
    }
    i++;
    zFName = db_column_text(&q, 0);
    if( zFName[0]=='/' ){
      zFName++;
      @ <li><a href="%s(g.zBaseURL)/dir?name=%T(zPrefix)%T(zFName)">
      @     %h(zFName)/</a></li>
    }else{
      @ <li><a href="%s(g.zBaseURL)/finfo?name=%T(zPrefix)%T(zFName)">
      @     %h(zFName)</a></li>
    }
  }
  db_finalize(&q);
  @ </td></tr></table>
  style_footer();
}
