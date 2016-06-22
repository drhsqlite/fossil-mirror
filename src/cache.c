/*
** Copyright (c) 2014 D. Richard Hipp
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
**
*******************************************************************************
**
** This file implements a cache for expense operations such as
** /zip and /tarball.
*/
#include "config.h"
#include <sqlite3.h>
#include "cache.h"

/*
** Construct the name of the repository cache file
*/
static char *cacheName(void){
  int i;
  int n;

  if( g.zRepositoryName==0 ) return 0;
  n = (int)strlen(g.zRepositoryName);
  for(i=n-1; i>=0; i--){
    if( g.zRepositoryName[i]=='/' ){ i = n; break; }
    if( g.zRepositoryName[i]=='.' ) break;
  }
  if( i<0 ) i = n;
  return mprintf("%.*s.cache", i, g.zRepositoryName);
}

/*
** Attempt to open the cache database, if such a database exists.
** Make sure the cache table exists within that database.
*/
static sqlite3 *cacheOpen(int bForce){
  char *zDbName;
  sqlite3 *db = 0;
  int rc;
  i64 sz;

  zDbName = cacheName();
  if( zDbName==0 ) return 0;
  if( bForce==0 ){
    sz = file_size(zDbName);
    if( sz<=0 ){
      fossil_free(zDbName);
      return 0;
    }
  }
  rc = sqlite3_open(zDbName, &db);
  fossil_free(zDbName);
  if( rc ){
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_exec(db,
     "PRAGMA page_size=8192;"
     "CREATE TABLE IF NOT EXISTS blob(id INTEGER PRIMARY KEY, data BLOB);"
     "CREATE TABLE IF NOT EXISTS cache("
       "key TEXT PRIMARY KEY,"     /* Key used to access the cache */
       "id INT REFERENCES blob,"   /* The cache content */
       "sz INT,"                   /* Size of content in bytes */
       "tm INT,"                   /* Last access time (unix timestampe) */
       "nref INT"                  /* Number of uses */
     ");"
     "CREATE TRIGGER IF NOT EXISTS cacheDel AFTER DELETE ON cache BEGIN"
     "  DELETE FROM blob WHERE id=OLD.id;"
     "END;",
     0, 0, 0);
  if( rc!=SQLITE_OK ){
    sqlite3_close(db);
    return 0;
  }
  return db;
}

/*
** Attempt to construct a prepared statement for the cache database.
*/
static sqlite3_stmt *cacheStmt(sqlite3 *db, const char *zSql){
  sqlite3_stmt *pStmt = 0;
  int rc;

  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
  if( rc ){
    sqlite3_finalize(pStmt);
    pStmt = 0;
  }
  return pStmt;
}

/*
** This routine implements an SQL function that renders a large integer
** compactly:  ex: 12.3MB
*/
static void cache_sizename(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  char zBuf[30];
  double v, x;
  assert( argc==1 );
  v = sqlite3_value_double(argv[0]);
  x = v<0.0 ? -v : v;
  if( x>=1e9 ){
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%.1fGB", v/1e9);
  }else if( x>=1e6 ){
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%.1fMB", v/1e6);
  }else if( x>=1e3 ){
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%.1fKB", v/1e3);
  }else{
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%gB", v);
  }
  sqlite3_result_text(context, zBuf, -1, SQLITE_TRANSIENT);
}

/*
** Register the sizename() SQL function with the SQLite database
** connection.
*/
static void cache_register_sizename(sqlite3 *db){
  sqlite3_create_function(db, "sizename", 1, SQLITE_UTF8, 0,
                          cache_sizename, 0, 0);
}

/*
** Attempt to write pContent into the cache.  If the cache file does
** not exist, then this routine is a no-op.  Older cache entries might
** be deleted.
*/
void cache_write(Blob *pContent, const char *zKey){
  sqlite3 *db;
  sqlite3_stmt *pStmt;
  int rc = 0;
  int nKeep;

  db = cacheOpen(0);
  if( db==0 ) return;
  sqlite3_busy_timeout(db, 10000);
  sqlite3_exec(db, "BEGIN IMMEDIATE", 0, 0, 0);
  pStmt = cacheStmt(db, "INSERT INTO blob(data) VALUES(?1)");
  if( pStmt==0 ) goto cache_write_end;
  sqlite3_bind_blob(pStmt, 1, blob_buffer(pContent), blob_size(pContent),
                    SQLITE_STATIC);
  if( sqlite3_step(pStmt)!=SQLITE_DONE ) goto cache_write_end;
  sqlite3_finalize(pStmt);
  pStmt = cacheStmt(db,
      "INSERT OR IGNORE INTO cache(key,sz,tm,nref,id)"
      "VALUES(?1,?2,strftime('%s','now'),1,?3)"
  );
  if( pStmt==0 ) goto cache_write_end;
  sqlite3_bind_text(pStmt, 1, zKey, -1, SQLITE_STATIC);
  sqlite3_bind_int(pStmt, 2, blob_size(pContent));
  sqlite3_bind_int(pStmt, 3, sqlite3_last_insert_rowid(db));
  if( sqlite3_step(pStmt)!=SQLITE_DONE) goto cache_write_end;
  rc = sqlite3_changes(db);

  /* If the write was successful, truncate the cache to keep at most
  ** max-cache-entry entries in the cache */
  if( rc ){
    nKeep = db_get_int("max-cache-entry",10);
    sqlite3_finalize(pStmt);
    pStmt = cacheStmt(db,
                 "DELETE FROM cache WHERE rowid IN ("
                    "SELECT rowid FROM cache ORDER BY tm DESC"
                    " LIMIT -1 OFFSET ?1)");
    if( pStmt ){
      sqlite3_bind_int(pStmt, 1, nKeep);
      sqlite3_step(pStmt);
    }
  }

cache_write_end:
  sqlite3_finalize(pStmt);
  sqlite3_exec(db, rc ? "COMMIT" : "ROLLBACK", 0, 0, 0);
  sqlite3_close(db);
}

/*
** Attempt to read content out of the cache with the given zKey.  Return
** non-zero on success and zero if unable to locate the content.
**
** Possible reasons for returning zero:
**   (1)  This server does not implement a cache
**   (2)  The requested element is not in the cache
*/
int cache_read(Blob *pContent, const char *zKey){
  sqlite3 *db;
  sqlite3_stmt *pStmt;
  int rc = 0;

  db = cacheOpen(0);
  if( db==0 ) return 0;
  sqlite3_busy_timeout(db, 10000);
  sqlite3_exec(db, "BEGIN IMMEDIATE", 0, 0, 0);
  pStmt = cacheStmt(db,
    "SELECT blob.data FROM cache, blob"
    " WHERE cache.key=?1 AND cache.id=blob.id");
  if( pStmt==0 ) goto cache_read_done;
  sqlite3_bind_text(pStmt, 1, zKey, -1, SQLITE_STATIC);
  if( sqlite3_step(pStmt)==SQLITE_ROW ){
    blob_append(pContent, sqlite3_column_blob(pStmt, 0),
                          sqlite3_column_bytes(pStmt, 0));
    rc = 1;
    sqlite3_reset(pStmt);
    pStmt = cacheStmt(db,
              "UPDATE cache SET nref=nref+1, tm=strftime('%s','now')"
              " WHERE key=?1");
    if( pStmt ){
      sqlite3_bind_text(pStmt, 1, zKey, -1, SQLITE_STATIC);
      sqlite3_step(pStmt);
    }
  }
  sqlite3_finalize(pStmt);
cache_read_done:
  sqlite3_exec(db, "COMMIT", 0, 0, 0);
  sqlite3_close(db);
  return rc;
}

/*
** Create a cache database for the current repository if no such
** database already exists.
*/
void cache_initialize(void){
  sqlite3_close(cacheOpen(1));
}

/*
** COMMAND: cache*
**
** Usage: %fossil cache SUBCOMMAND
**
** Manage the cache used for potentially expensive web pages such as
** /zip and /tarball.   SUBCOMMAND can be:
**
**    clear        Remove all entries from the cache.
**
**    init         Create the cache file if it does not already exist.
**
**    list|ls      List the keys and content sizes and other stats for
**                 all entries currently in the cache.
**
**    status       Show a summary of the cache status.
**
** The cache is stored in a file that is distinct from the repository
** but that is held in the same directory as the repository.  The cache
** file can be deleted in order to completely disable the cache.
*/
void cache_cmd(void){
  const char *zCmd;
  int nCmd;
  sqlite3 *db;
  sqlite3_stmt *pStmt;

  db_find_and_open_repository(0,0);
  zCmd = g.argc>=3 ? g.argv[2] : "";
  nCmd = (int)strlen(zCmd);
  if( nCmd<=1 ){
    fossil_fatal("Usage: %s cache SUBCOMMAND", g.argv[0]);
  }
  if( strncmp(zCmd, "init", nCmd)==0 ){
    db = cacheOpen(0);
    sqlite3_close(db);
    if( db ){
      fossil_print("cache already exists in file %z\n", cacheName());
    }else{
      db = cacheOpen(1);
      sqlite3_close(db);
      if( db ){
        fossil_print("cache created in file %z\n", cacheName());
      }else{
        fossil_fatal("unable to create cache file %z", cacheName());
      }
    }
  }else if( strncmp(zCmd, "clear", nCmd)==0 ){
    db = cacheOpen(0);
    if( db ){
      sqlite3_exec(db, "DELETE FROM cache; DELETE FROM blob; VACUUM;",0,0,0);
      sqlite3_close(db);
      fossil_print("cache cleared\n");
    }else{
      fossil_print("nothing to clear; cache does not exist\n");
    }
  }else if(( strncmp(zCmd, "list", nCmd)==0 ) || ( strncmp(zCmd, "ls", nCmd)==0 )){
    db = cacheOpen(0);
    if( db==0 ){
      fossil_print("cache does not exist\n");
    }else{
      int nEntry = 0;
      char *zDbName = cacheName();
      cache_register_sizename(db);
      pStmt = cacheStmt(db,
           "SELECT key, sizename(sz), nRef, datetime(tm,'unixepoch')"
           "  FROM cache"
           " ORDER BY tm DESC"
      );
      if( pStmt ){
        while( sqlite3_step(pStmt)==SQLITE_ROW ){
          fossil_print("%s %4d %8s %s\n",
             sqlite3_column_text(pStmt, 3),
             sqlite3_column_int(pStmt, 2),
             sqlite3_column_text(pStmt, 1),
             sqlite3_column_text(pStmt, 0));
          nEntry++;
        }
        sqlite3_finalize(pStmt);
      }
      sqlite3_close(db);
      fossil_print("Entries: %d  Cache-file Size: %lld\n",
                   nEntry, file_size(zDbName));
      fossil_free(zDbName);
    }
  }else if( strncmp(zCmd, "status", nCmd)==0 ){
    fossil_print("TBD...\n");
  }else{
    fossil_fatal("Unknown subcommand \"%s\"."
                 " Should be one of: clear init list status", zCmd);
  }
}

/*
** WEBPAGE: cachestat
**
** Show information about the webpage cache.  Requires Admin privilege.
*/
void cache_page(void){
  sqlite3 *db;
  sqlite3_stmt *pStmt;
  char zBuf[100];

  login_check_credentials();
  if( !g.perm.Setup ){ login_needed(0); return; }
  style_header("Web Cache Status");
  db = cacheOpen(0);
  if( db==0 ){
    @ The web-page cache is disabled for this repository
  }else{
    char *zDbName = cacheName();
    cache_register_sizename(db);
    pStmt = cacheStmt(db,
         "SELECT key, sizename(sz), nRef, datetime(tm,'unixepoch')"
         "  FROM cache"
         " ORDER BY tm DESC"
    );
    if( pStmt ){
      @ <ol>
      while( sqlite3_step(pStmt)==SQLITE_ROW ){
        const unsigned char *zName = sqlite3_column_text(pStmt,0);
        @ <li><p>%z(href("%R/cacheget?key=%T",zName))%h(zName)</a><br />
        @ size: %s(sqlite3_column_text(pStmt,1))
        @ hit-count: %d(sqlite3_column_int(pStmt,2))
        @ last-access: %s(sqlite3_column_text(pStmt,3))</p></li>
      }
      sqlite3_finalize(pStmt);
      @ </ol>
    }
    zDbName = cacheName();
    bigSizeName(sizeof(zBuf), zBuf, file_size(zDbName));
    @ <p>cache-file name: %h(zDbName)</p>
    @ <p>cache-file size: %s(zBuf)</p>
    fossil_free(zDbName);
    sqlite3_close(db);
  }
  style_footer();
}

/*
** WEBPAGE: cacheget
**
** Usage:  /cacheget?key=KEY
**
** Download a single entry for the cache, identified by KEY.
** This page is normally a hyperlink from the /cachestat page.
** Requires Admin privilege.
*/
void cache_getpage(void){
  const char *zKey;
  Blob content;

  login_check_credentials();
  if( !g.perm.Setup ){ login_needed(0); return; }
  zKey = PD("key","");
  blob_zero(&content);
  if( cache_read(&content, zKey)==0 ){
    style_header("Cache Download Error");
    @ The cache does not contain any entry with this key: "%h(zKey)"
    style_footer();
    return;
  }
  cgi_set_content(&content);
  cgi_set_content_type("application/x-compressed");
}
