/*
** Copyright (c) 2018 D. Richard Hipp
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
** This module contains code to implement the repository list page when
** "fossil server" or "fossil ui" is serving a directory full of repositories.
*/
#include "config.h"
#include "repolist.h"

/*
** Generate a web-page that lists all repositories located under the
** g.zRepositoryName directory and return non-zero.
**
** For the special case when g.zRepositoryName a non-chroot-jail "/",
** compose the list using the "repo:" entries in the global_config
** table of the configuration database.  These entries comprise all
** of the repositories known to the "all" command.  The special case
** processing is disallowed for chroot jails because g.zRepositoryName
** is always "/" inside a chroot jail and so it cannot be used as a flag
** to signal the special processing in that case.  The special case
** processing is intended for the "fossil all ui" command which never
** runs in a chroot jail anyhow.
**
** Or, if no repositories can be located beneath g.zRepositoryName,
** return 0.
*/
int repo_list_page(void){
  Blob base;
  int n = 0;
  int allRepo;

  assert( g.db==0 );
  if( fossil_strcmp(g.zRepositoryName,"/")==0 && !g.fJail ){
    /* For the special case of the "repository directory" being "/",
    ** show all of the repositories named in the ~/.fossil database.
    **
    ** On unix systems, then entries are of the form "repo:/home/..."
    ** and on Windows systems they are like on unix, starting with a "/"
    ** or they can begin with a drive letter: "repo:C:/Users/...".  In either
    ** case, we want returned path to omit any initial "/".
    */
    db_open_config(1, 0);
    db_multi_exec(
       "CREATE TEMP VIEW sfile AS"
       "  SELECT ltrim(substr(name,6),'/') AS 'pathname' FROM global_config"
       "   WHERE name GLOB 'repo:*'"
    );
    allRepo = 1;
  }else{
    /* The default case:  All repositories under the g.zRepositoryName
    ** directory.
    */
    blob_init(&base, g.zRepositoryName, -1);
    sqlite3_open(":memory:", &g.db);
    db_multi_exec("CREATE TABLE sfile(pathname TEXT);");
    db_multi_exec("CREATE TABLE vfile(pathname);");
    vfile_scan(&base, blob_size(&base), 0, 0, 0);
    db_multi_exec("DELETE FROM sfile WHERE pathname NOT GLOB '*[^/].fossil'");
    allRepo = 0;
  }
  @ <html>
  @ <head>
  @ <base href="%s(g.zBaseURL)/" />
  @ <meta name="viewport" content="width=device-width, initial-scale=1.0">
  @ <title>Repository List</title>
  @ </head>
  @ <body>
  n = db_int(0, "SELECT count(*) FROM sfile");
  if( n>0 ){
    Stmt q;
    sqlite3_int64 iNow, iMTime;
    @ <h1 align="center">Fossil Repositories</h1>
    @ <table border="0" class="sortable" data-init-sort="1" \
    @ data-column-types="tnk"><thead>
    @ <tr><th>Filename<th width="20"><th>Last Modified</tr>
    @ </thead><tbody>
    db_prepare(&q, "SELECT pathname"
                   " FROM sfile ORDER BY pathname COLLATE nocase;");
    iNow = db_int64(0, "SELECT strftime('%%s','now')");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      int nName = (int)strlen(zName);
      char *zUrl;
      char *zAge;
      char *zFull;
      if( nName<7 ) continue;
      zUrl = sqlite3_mprintf("%.*s", nName-7, zName);
      if( zName[0]=='/'
#ifdef _WIN32
          || sqlite3_strglob("[a-zA-Z]:/*", zName)==0
#endif
      ){
        zFull = mprintf("%s", zName);
      }else if ( allRepo ){
        zFull = mprintf("/%s", zName);
      }else{
        zFull = mprintf("%s/%s", g.zRepositoryName, zName);
      }
      iMTime = file_mtime(zFull, ExtFILE);
      fossil_free(zFull);
      if( iMTime<=0 ){
        zAge = mprintf("...");
      }else{
        zAge = human_readable_age((iNow - iMTime)/86400.0);
      }
      if( sqlite3_strglob("*.fossil", zName)!=0 ){
        /* The "fossil server DIRECTORY" and "fossil ui DIRECTORY" commands
        ** do not work for repositories whose names do not end in ".fossil".
        ** So do not hyperlink those cases. */
        @ <tr><td>%h(zName)
      } else if( sqlite3_strglob("*/.*", zName)==0 ){
        /* Do not show hidden repos */
        @ <tr><td>%h(zName) (hidden)
      } else if( allRepo && sqlite3_strglob("[a-zA-Z]:/?*", zName)!=0 ){
        @ <tr><td><a href="%R/%T(zUrl)/home" target="_blank">/%h(zName)</a>
      }else{
        @ <tr><td><a href="%R/%T(zUrl)/home" target="_blank">%h(zName)</a>
      }
      @ <td></td><td data-sortkey='%010llx(iNow - iMTime)'>%h(zAge)</tr>
      fossil_free(zAge);
      sqlite3_free(zUrl);
    }
    @ </tbody></table>
  }else{
    @ <h1>No Repositories Found</h1>
  }
  @ <script>%s(builtin_text("sorttable.js"))</script>
  @ </body>
  @ </html>
  cgi_reply();
  sqlite3_close(g.db);
  g.db = 0;
  return n;
}

/*
** COMMAND: test-list-page
**
** Usage: %fossil test-list-page DIRECTORY
**
** Show all repositories underneath DIRECTORY.  Or if DIRECTORY is "/"
** show all repositories in the ~/.fossil file.
*/
void test_list_page(void){
  if( g.argc<3 ){
    g.zRepositoryName = "/";
  }else{
    g.zRepositoryName = g.argv[2];
  }
  g.httpOut = stdout;
  repo_list_page();
}
