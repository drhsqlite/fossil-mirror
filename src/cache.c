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
    sz = file_size(zDbName, ExtFILE);
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
  sqlite3_busy_timeout(db, 5000);
  if( sqlite3_table_column_metadata(db,0,"blob","key",0,0,0,0,0)!=SQLITE_OK ){
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
       0, 0, 0
    );
    if( rc!=SQLITE_OK ){
      sqlite3_close(db);
      return 0;
    }
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
  ** max-cache-entry entries in the cache.
  **
  ** The cache entry replacement algorithm is approximately LRU
  ** (least recently used).  However, each access of an entry buys
  ** that entry an extra hour of grace, so that more commonly accessed
  ** entries are held in cache longer.  The extra "grace" allotted to
  ** an entry is limited to 2 days worth.
  */
  if( rc ){
    nKeep = db_get_int("max-cache-entry",10);
    sqlite3_finalize(pStmt);
    pStmt = cacheStmt(db,
                 "DELETE FROM cache WHERE rowid IN ("
                    "SELECT rowid FROM cache"
                    " ORDER BY (tm + 3600*min(nRef,48)) DESC"
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
** SETTING: max-cache-entry                 width=10 default=10
**
** This is the maximum number of entries to allow in the web-cache
** for tarballs, ZIP-archives, and SQL-archives.
*/

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
** COMMAND: cache*                    abbrv-subcom
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
**    size ?N?     Query or set the maximum number of entries in the cache.
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
  }else if( strncmp(zCmd, "list", nCmd)==0
        ||  strncmp(zCmd, "ls", nCmd)==0
        ||  strncmp(zCmd, "status", nCmd)==0
  ){
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
          if( zCmd[0]=='l' ){
            fossil_print("%s %4d %8s %s\n",
               sqlite3_column_text(pStmt, 3),
               sqlite3_column_int(pStmt, 2),
               sqlite3_column_text(pStmt, 1),
               sqlite3_column_text(pStmt, 0));
          }
          nEntry++;
        }
        sqlite3_finalize(pStmt);
      }
      sqlite3_close(db);
      fossil_print(
         "Filename:        %s\n"
         "Entries:         %d\n"
         "max-cache-entry: %d\n"
         "Cache-file Size: %,lld\n",
         zDbName,
         nEntry,
         db_get_int("max-cache-entry",10),
         file_size(zDbName, ExtFILE)
      );
      fossil_free(zDbName);
    }
  }else if( strncmp(zCmd, "size", nCmd)==0 ){
    if( g.argc>=4 ){
      int n = atoi(g.argv[3]);
      if( n>=5 ) db_set_int("max-cache-entry",n,0);
    }
    fossil_print("max-cache-entry: %d\n", db_get_int("max-cache-entry",10));
  }else{
    fossil_fatal("Unknown subcommand \"%s\"."
                 " Should be one of: clear init list size status", zCmd);
  }
}

/*
** Given a cache key, find the check-in hash and return it as a separate
** string.  The returned string is obtained from fossil_malloc() and must
** be freed by the caller.
**
** Return NULL if not found.
**
** The key is usually in a format like these:
**
**    /tarball/HASH/NAME
**    /zip/HASH/NAME
**    /sqlar/HASH/NAME
*/
static char *cache_hash_of_key(const char *zKey){
  int i;
  if( zKey==0 ) return 0;
  if( zKey[0]!='/' ) return 0;
  zKey++;
  while( zKey[0] && zKey[0]!='/' ) zKey++;
  if( zKey[0]==0 ) return 0;
  zKey++;
  for(i=0; zKey[i] && zKey[i]!='/'; i++){}
  if( !validate16(zKey, i) ) return 0;
  return fossil_strndup(zKey, i);
}


/*
** WEBPAGE: cachestat
**
** Show information about the webpage cache.  Requires Setup privilege.
*/
void cache_page(void){
  sqlite3 *db = 0;
  sqlite3_stmt *pStmt;
  int doInit;
  char *zDbName = cacheName();
  int nEntry = 0;
  int mxEntry = 0;
  char zBuf[100];

  login_check_credentials();
  if( !g.perm.Setup ){ login_needed(0); return; }
  style_set_current_feature("cache");
  style_header("Web Cache Status");
  style_submenu_element("Refresh","%R/cachestat");
  doInit = P("init")!=0 && cgi_csrf_safe(2);
  db = cacheOpen(doInit);
  if( db!=0 ){
    if( P("clear")!=0 && cgi_csrf_safe(2) ){
      sqlite3_exec(db, "DELETE FROM cache; DELETE FROM blob; VACUUM;",0,0,0);
    }
    cache_register_sizename(db);
    pStmt = cacheStmt(db,
         "SELECT key, sz, nRef, datetime(tm,'unixepoch')"
         "  FROM cache"
         " ORDER BY (tm + 3600*min(nRef,48)) DESC"
    );
    if( pStmt ){
      while( sqlite3_step(pStmt)==SQLITE_ROW ){
        const unsigned char *zName = sqlite3_column_text(pStmt,0);
        char *zHash = cache_hash_of_key((const char*)zName);
        if( nEntry==0 ){
          @ <h2>Current Cache Entries:</h2>
          @ <ol>
        }
        @ <li><p>%z(href("%R/cacheget?key=%T",zName))%h(zName)</a><br>
        @ size: %,lld(sqlite3_column_int64(pStmt,1)),
        @ hit-count: %d(sqlite3_column_int(pStmt,2)),
        @ last-access: %s(sqlite3_column_text(pStmt,3))Z \
        if( zHash ){
          @ &rarr; %z(href("%R/timeline?c=%S",zHash))checkin info</a>\
          fossil_free(zHash);
        }
        @ </p></li>
        nEntry++;
      }
      sqlite3_finalize(pStmt);
      if( nEntry ){
        @ </ol>
      }
    }
  }
  @ <h2>About The Web-Cache</h2>
  @ <p>
  @ The web-cache is a separate database file that holds cached copies
  @ tarballs, ZIP archives, and other pages that are expensive to compute
  @ and are likely to be reused.
  @ <form method="post">
  login_insert_csrf_secret();
  @ <ul>
  if( db==0 ){
    @ <li> Web-cache is currently disabled.
    @ <input type="submit" name="init" value="Enable">
  }else{
    bigSizeName(sizeof(zBuf), zBuf, file_size(zDbName, ExtFILE));
    mxEntry = db_get_int("max-cache-entry",10);
    @ <li> Filename of the cache database: <b>%h(zDbName)</b>
    @ <li> Size of the cache database: %s(zBuf)
    @ <li> Maximum number of entries: %d(mxEntry)
    @ <li> Number of cache entries used: %d(nEntry)
    @ <li> Change the max-cache-entry setting on the
    @ <a href="%R/setup_settings">Settings</a> page to adjust the
    @ maximum number of entries in the cache.
    @ <li><input type="submit" name="clear" value="Clear the cache">
    @ <li> Disable the cache by manually deleting the cache database file.
  }
  @ </ul>
  @ </form>
  fossil_free(zDbName);
  if( db ) sqlite3_close(db);
  style_finish_page();
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
    style_set_current_feature("cache");
    style_header("Cache Download Error");
    @ The cache does not contain any entry with this key: "%h(zKey)"
    style_finish_page();
    return;
  }
  cgi_set_content(&content);
  cgi_set_content_type("application/x-compressed");
}
