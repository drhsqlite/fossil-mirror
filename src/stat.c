/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code to implement the stat web page
**
*/
#include <string.h>
#include "config.h"
#include "stat.h"

/*
** For a sufficiently large integer, provide an alternative
** representation as MB or GB or TB.
*/
static void bigSizeName(int nOut, char *zOut, sqlite3_int64 v){
  if( v<100000 ){
    sqlite3_snprintf(nOut, zOut, "%lld bytes", v);
  }else if( v<1000000000 ){
    sqlite3_snprintf(nOut, zOut, "%lld bytes (%.1fMB)",
                    v, (double)v/1000000.0);
  }else{
    sqlite3_snprintf(nOut, zOut, "%lld bytes (%.1fGB)",
                    v, (double)v/1000000000.0);
  }
}

/*
** WEBPAGE: stat
**
** Show statistics and global information about the repository.
*/
void stat_page(void){
  i64 t, fsize;
  int n, m;
  int szMax, szAvg;
  const char *zDb;
  int brief;
  char zBuf[100];

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }
  brief = P("brief")!=0;
  style_header("Repository Statistics");
  @ <table class="label-value">
  @ <tr><th>Repository&nbsp;Size:</th><td>
  fsize = file_size(g.zRepositoryName);
  bigSizeName(sizeof(zBuf), zBuf, fsize);
  @ %s(zBuf)
  @ </td></tr>
  if( !brief ){
    @ <tr><th>Number&nbsp;Of&nbsp;Artifacts:</th><td>
    n = db_int(0, "SELECT count(*) FROM blob");
    m = db_int(0, "SELECT count(*) FROM delta");
    @ %d(n) (stored as %d(n-m) full text and %d(m) delta blobs)
    @ </td></tr>
    if( n>0 ){
      int a, b;
      Stmt q;
      @ <tr><th>Uncompressed&nbsp;Artifact&nbsp;Size:</th><td>
      db_prepare(&q, "SELECT total(size), avg(size), max(size)"
                     " FROM blob WHERE size>0");
      db_step(&q);
      t = db_column_int64(&q, 0);
      szAvg = db_column_int(&q, 1);
      szMax = db_column_int(&q, 2);
      db_finalize(&q);
      bigSizeName(sizeof(zBuf), zBuf, t);
      @ %d(szAvg) bytes average, %d(szMax) bytes max, %s(zBuf) total
      @ </td></tr>
      @ <tr><th>Compression&nbsp;Ratio:</th><td>
      if( t/fsize < 5 ){
        b = 10;
        fsize /= 10;
      }else{
        b = 1;
      }
      a = t/fsize;
      @ %d(a):%d(b)
      @ </td></tr>
    }
    @ <tr><th>Number&nbsp;Of&nbsp;Check-ins:</th><td>
    n = db_int(0, "SELECT count(distinct mid) FROM mlink /*scan*/");
    @ %d(n)
    @ </td></tr>
    @ <tr><th>Number&nbsp;Of&nbsp;Files:</th><td>
    n = db_int(0, "SELECT count(*) FROM filename /*scan*/");
    @ %d(n)
    @ </td></tr>
    @ <tr><th>Number&nbsp;Of&nbsp;Wiki&nbsp;Pages:</th><td>
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
                  " WHERE +tagname GLOB 'wiki-*'");
    @ %d(n)
    @ </td></tr>
    @ <tr><th>Number&nbsp;Of&nbsp;Tickets:</th><td>
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
                  " WHERE +tagname GLOB 'tkt-*'");
    @ %d(n)
    @ </td></tr>
  }
  @ <tr><th>Duration&nbsp;Of&nbsp;Project:</th><td>
  n = db_int(0, "SELECT julianday('now') - (SELECT min(mtime) FROM event)"
                " + 0.99");
  @ %d(n) days
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%.2f", n/365.24);
  @ or approximately %s(zBuf) years
  @ </td></tr>
  @ <tr><th>Project&nbsp;ID:</th><td>%h(db_get("project-code",""))</td></tr>
  @ <tr><th>Server&nbsp;ID:</th><td>%h(db_get("server-code",""))</td></tr>

  @ <tr><th>Fossil&nbsp;Version:</th><td>
  @ %h(RELEASE_VERSION) %h(MANIFEST_DATE) %h(MANIFEST_VERSION)
  @ (%h(COMPILER_NAME))
  @ </td></tr>
  @ <tr><th>SQLite&nbsp;Version:</th><td>
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%.19s [%.10s] (%s)",
                   SQLITE_SOURCE_ID, &SQLITE_SOURCE_ID[20], SQLITE_VERSION);
  zDb = db_name("repository");
  @ %s(zBuf)
  @ </td></tr>
  @ <tr><th>Database&nbsp;Stats:</th><td>
  @ %d(db_int(0, "PRAGMA %s.page_count", zDb)) pages,
  @ %d(db_int(0, "PRAGMA %s.page_size", zDb)) bytes/page,
  @ %d(db_int(0, "PRAGMA %s.freelist_count", zDb)) free pages,
  @ %s(db_text(0, "PRAGMA %s.encoding", zDb)),
  @ %s(db_text(0, "PRAGMA %s.journal_mode", zDb)) mode
  @ </td></tr>

  @ </table>
  style_footer();
}

/* 
 * vim:ts=2:sts=2:et:sw=2:ft=c 
 */

