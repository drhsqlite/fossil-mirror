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

#if INTERFACE
/*
** Return value from the remote_repo_info() command.  zRepoName is the
** input.  All other fields are outputs.
*/
struct RepoInfo {
  char *zRepoName;      /* Name of the repository file */
  int isValid;          /* True if zRepoName is a valid Fossil repository */
  int isRepolistSkin;   /* 1 or 2 if this repository wants to be the skin
                        ** for the repository list.  2 means do use this
                        ** repository but do not display it in the list. */
  char *zProjName;      /* Project Name.  Memory from fossil_malloc() */
  char *zLoginGroup;    /* Name of login group, or NULL.  Malloced() */
  double rMTime;        /* Last update.  Julian day number */
};
#endif

/*
** Discover information about the repository given by
** pRepo->zRepoName.  The discovered information is stored in other
** fields of the RepoInfo object.
*/
static void remote_repo_info(RepoInfo *pRepo){
  sqlite3 *db;
  sqlite3_stmt *pStmt;
  int rc;

  pRepo->isRepolistSkin = 0;
  pRepo->isValid = 0;
  pRepo->zProjName = 0;
  pRepo->zLoginGroup = 0;
  pRepo->rMTime = 0.0;

  g.dbIgnoreErrors++;
  rc = sqlite3_open_v2(pRepo->zRepoName, &db, SQLITE_OPEN_READWRITE, 0);
  if( rc ) goto finish_repo_list;
  rc = sqlite3_prepare_v2(db, "SELECT value FROM config"
                              " WHERE name='repolist-skin'",
                          -1, &pStmt, 0);
  if( rc ) goto finish_repo_list;
  if( sqlite3_step(pStmt)==SQLITE_ROW ){
    pRepo->isRepolistSkin = sqlite3_column_int(pStmt,0);
  }
  sqlite3_finalize(pStmt);
  if( rc ) goto finish_repo_list;
  rc = sqlite3_prepare_v2(db, "SELECT value FROM config"
                              " WHERE name='project-name'",
                          -1, &pStmt, 0);
  if( rc ) goto finish_repo_list;
  if( sqlite3_step(pStmt)==SQLITE_ROW ){
    pRepo->zProjName = fossil_strdup((char*)sqlite3_column_text(pStmt,0));
  }
  sqlite3_finalize(pStmt);
  rc = sqlite3_prepare_v2(db, "SELECT value FROM config"
                              " WHERE name='login-group-name'",
                          -1, &pStmt, 0);
  if( rc==SQLITE_OK && sqlite3_step(pStmt)==SQLITE_ROW ){
    pRepo->zLoginGroup = fossil_strdup((char*)sqlite3_column_text(pStmt,0));
  }
  sqlite3_finalize(pStmt);
  rc = sqlite3_prepare_v2(db, "SELECT max(mtime) FROM event", -1, &pStmt, 0);
  if( rc==SQLITE_OK && sqlite3_step(pStmt)==SQLITE_ROW ){
    pRepo->rMTime = sqlite3_column_double(pStmt,0);
  }
  pRepo->isValid = 1;
  sqlite3_finalize(pStmt);
finish_repo_list:
  g.dbIgnoreErrors--;
  sqlite3_close(db);
}

/*
** Generate a web-page that lists all repositories located under the
** g.zRepositoryName directory and return non-zero.
**
** For the special case when g.zRepositoryName is a non-chroot-jail "/",
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
** close g.db and return 0.
*/
int repo_list_page(void){
  Blob base;           /* document root for all repositories */
  int n = 0;           /* Number of repositories found */
  int allRepo;         /* True if running "fossil ui all".
                       ** False if a directory scan of base for repos */
  Blob html;           /* Html for the body of the repository list */
  char *zSkinRepo = 0; /* Name of the repository database used for skins */
  char *zSkinUrl = 0;  /* URL for the skin database */

  assert( g.db==0 );
  blob_init(&html, 0, 0);
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
    vfile_scan(&base, blob_size(&base), 0, 0, 0, ExtFILE);
    db_multi_exec("DELETE FROM sfile WHERE pathname NOT GLOB '*[^/].fossil'"
#if USE_SEE
                  " AND pathname NOT GLOB '*[^/].efossil'"
#endif
    );
    allRepo = 0;
  }
  n = db_int(0, "SELECT count(*) FROM sfile");
  if( n==0 ){
    sqlite3_close(g.db);
    g.db = 0;
    g.repositoryOpen = 0;
    g.localOpen = 0;
    return 0;
  }else{
    Stmt q;
    double rNow;
    blob_append_sql(&html,
      "<table border='0' class='sortable' data-init-sort='1'"
      " data-column-types='txtxkxt'><thead>\n"
      "<tr><th>Filename<th width='20'>"
      "<th>Project Name<th width='20'>"
      "<th>Last Modified<th width='20'>"
      "<th>Login Group</tr>\n"
      "</thead><tbody>\n");
    db_prepare(&q, "SELECT pathname"
                   " FROM sfile ORDER BY pathname COLLATE nocase;");
    rNow = db_double(0, "SELECT julianday('now')");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      int nName = (int)strlen(zName);
      int nSuffix = 7; /* ".fossil" */
      char *zUrl;
      char *zAge;
      char *zFull;
      RepoInfo x;
      sqlite3_int64 iAge;
#if USE_SEE
      int bEncrypted = sqlite3_strglob("*.efossil", zName)==0;
      if( bEncrypted ) nSuffix = 8; /* ".efossil" */
#endif
      if( nName<nSuffix ) continue;
      zUrl = sqlite3_mprintf("%.*s", nName-nSuffix, zName);
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
      x.zRepoName = zFull;
      remote_repo_info(&x);
      if( x.isRepolistSkin ){
        if( zSkinRepo==0 ){
          zSkinRepo = mprintf("%s", x.zRepoName);
          zSkinUrl = mprintf("%s", zUrl);
        }
      }
      fossil_free(zFull);
      if( !x.isValid
#if USE_SEE
       && !bEncrypted
#endif
      ){
        continue;
      }
      if( x.isRepolistSkin==2 && !allRepo ){
        /* Repositories with repolist-skin==2 are omitted from directory
        ** scan lists, but included in "fossil all ui" lists */
        continue;
      }
      if( rNow <= x.rMTime ){
        x.rMTime = rNow;
      }else if( x.rMTime<0.0 ){
        x.rMTime = rNow;
      }
      iAge = (sqlite3_int64)((rNow - x.rMTime)*86400);
      zAge = human_readable_age(rNow - x.rMTime);
      if( x.rMTime==0.0 ){
        /* This repository has no entry in the "event" table.
        ** Its age will still be maximum, so data-sortkey will work. */
        zAge = mprintf("unknown");
      }
      blob_append_sql(&html, "<tr><td valign='top'>");
      if( !file_ends_with_repository_extension(zName,0) ){
        /* The "fossil server DIRECTORY" and "fossil ui DIRECTORY" commands
        ** do not work for repositories whose names do not end in ".fossil".
        ** So do not hyperlink those cases. */
        blob_append_sql(&html,"%h",zName);
      } else if( sqlite3_strglob("*/.*", zName)==0 ){
        /* Do not show hyperlinks for hidden repos */
        blob_append_sql(&html, "%h (hidden)", zName);
      } else if( allRepo && sqlite3_strglob("[a-zA-Z]:/?*", zName)!=0 ){
        blob_append_sql(&html,
          "<a href='%R/%T/home' target='_blank'>/%h</a>\n",
          zUrl, zName);
      }else if( file_ends_with_repository_extension(zName,1) ){
        /* As described in
        ** https://fossil-scm.org/forum/info/f50f647c97c72fc1: if
        ** foo.fossil and foo/bar.fossil both exist and we create a
        ** link to foo/bar/... then the URI dispatcher will instead
        ** see that as a link to foo.fossil. In such cases, do not
        ** emit a link to foo/bar.fossil. */
        char * zDirPart = file_dirname(zName);
        if( db_exists("SELECT 1 FROM sfile "
                      "WHERE pathname=(%Q || '.fossil') COLLATE nocase"
#if USE_SEE
                      "  OR pathname=(%Q || '.efossil') COLLATE nocase"
#endif
                      , zDirPart
#if USE_SEE
                      , zDirPart
#endif
        ) ){
          blob_append_sql(&html,
            "<s>%h</s> (directory/repo name collision)\n",
            zName);
        }else{
          blob_append_sql(&html,
            "<a href='%R/%T/home' target='_blank'>%h</a>\n",
            zUrl, zName);
        }
        fossil_free(zDirPart);
      }else{
        blob_append_sql(&html,
          "<a href='%R/%T/home' target='_blank'>%h</a>\n",
          zUrl, zName);
      }
      if( x.zProjName ){
        blob_append_sql(&html, "<td></td><td>%h</td>\n", x.zProjName);
        fossil_free(x.zProjName);
      }else{
        blob_append_sql(&html, "<td></td><td></td>\n");
      }
      blob_append_sql(&html,
        "<td></td><td data-sortkey='%08x'>%h</td>\n",
        (int)iAge, zAge);
      fossil_free(zAge);
      if( x.zLoginGroup ){
        blob_append_sql(&html, "<td></td><td>%h</td></tr>\n", x.zLoginGroup);
        fossil_free(x.zLoginGroup);
      }else{
        blob_append_sql(&html, "<td></td><td></td></tr>\n");
      }
      sqlite3_free(zUrl);
    }
    db_finalize(&q);
    blob_append_sql(&html,"</tbody></table>\n");
  }
  if( zSkinRepo ){
    char *zNewBase = mprintf("%s/%s", g.zBaseURL, zSkinUrl);
    g.zBaseURL = 0;
    set_base_url(zNewBase);
    db_open_repository(zSkinRepo);
    fossil_free(zSkinRepo);
    fossil_free(zSkinUrl);
  }
  if( g.repositoryOpen ){
    /* This case runs if remote_repo_info() found a repository
    ** that has the "repolist_skin" property set to non-zero and left
    ** that repository open in g.db.  Use the skin of that repository
    ** for display. */
    login_check_credentials();
    style_set_current_feature("repolist");
    style_header("Repository List");
    @ %s(blob_str(&html))
    style_table_sorter();
    style_finish_page();
  }else{
    /* If no repositories were found that had the "repolist_skin"
    ** property set, then use a default skin */
    @ <html>
    @ <head>
    @ <base href="%s(g.zBaseURL)/">
    @ <meta name="viewport" content="width=device-width, initial-scale=1.0">
    @ <title>Repository List</title>
    @ </head>
    @ <body>
    @ <h1 align="center">Fossil Repositories</h1>
    @ %s(blob_str(&html))
    @ <script>%s(builtin_text("sorttable.js"))</script>
    @ </body>
    @ </html>
  }
  blob_reset(&html);
  cgi_reply();
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
