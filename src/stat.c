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
#include "VERSION.h"
#include "config.h"
#include <string.h>
#include "stat.h"

/*
** For a sufficiently large integer, provide an alternative
** representation as MB or GB or TB.
*/
void bigSizeName(int nOut, char *zOut, sqlite3_int64 v){
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
  const char *p;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }
  brief = P("brief")!=0;
  style_header("Repository Statistics");
  if( g.perm.Admin ){
    style_submenu_element("URLs", "URLs and Checkouts", "urllist");
    style_submenu_element("Schema", "Repository Schema", "repo_schema");
    style_submenu_element("Web-Cache", "Web-Cache Stats", "cachestat");
  }
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
    @ %d(n) (%d(n-m) fulltext and %d(m) deltas)
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
    n = db_int(0, "SELECT count(*) FROM event WHERE type='ci' /*scan*/");
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
  @ %d(n) days or approximately %.2f(n/365.2425) years.
  @ </td></tr>
  p = db_get("project-code", 0);
  if( p ){
    @ <tr><th>Project&nbsp;ID:</th><td>%h(p)</td></tr>
  }
  @ <tr><th>Server&nbsp;ID:</th><td>%h(db_get("server-code",""))</td></tr>
  @ <tr><th>Fossil&nbsp;Version:</th><td>
  @ %h(MANIFEST_DATE) %h(MANIFEST_VERSION)
  @ (%h(RELEASE_VERSION)) [compiled using %h(COMPILER_NAME)]
  @ </td></tr>
  @ <tr><th>SQLite&nbsp;Version:</th><td>%.19s(sqlite3_sourceid())
  @ [%.10s(&sqlite3_sourceid()[20])] (%s(sqlite3_libversion()))</td></tr>
  @ <tr><th>Repository Rebuilt:</th><td>
  @ %h(db_get_mtime("rebuilt","%Y-%m-%d %H:%M:%S","Never"))
  @ By Fossil %h(db_get("rebuilt","Unknown"))</td></tr>
  @ <tr><th>Database&nbsp;Stats:</th><td>
  zDb = db_name("repository");
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
** COMMAND: dbstat*
**
** Usage: %fossil dbstat ?-brief | -b?
**
** Shows statistics and global information about the repository.
**
** The (-brief|-b) option removes any "long-running" statistics, namely
** those whose calculations are known to slow down as the repository
** grows.
**
*/
void dbstat_cmd(void){
  i64 t, fsize;
  int n, m;
  int szMax, szAvg;
  const char *zDb;
  int brief;
  int omitVers;            /* Omit Fossil and SQLite version information */
  char zBuf[100];
  const int colWidth = -19 /* printf alignment/width for left column */;
  const char *p, *z;

  brief = find_option("brief", "b",0)!=0;
  omitVers = find_option("omit-version-info", 0, 0)!=0;
  db_find_and_open_repository(0,0);

  /* We should be done with options.. */
  verify_all_options();

  if( (z = db_get("project-name",0))!=0
   || (z = db_get("short-project-name",0))!=0
  ){
    fossil_print("%*s%s\n", colWidth, "project-name:", z);
  }
  fsize = file_size(g.zRepositoryName);
  bigSizeName(sizeof(zBuf), zBuf, fsize);
  fossil_print( "%*s%s\n", colWidth, "repository-size:", zBuf );
  if( !brief ){
    n = db_int(0, "SELECT count(*) FROM blob");
    m = db_int(0, "SELECT count(*) FROM delta");
    fossil_print("%*s%d (stored as %d full text and %d delta blobs)\n",
                 colWidth, "artifact-count:",
                 n, n-m, m);
    if( n>0 ){
      int a, b;
      Stmt q;
      db_prepare(&q, "SELECT total(size), avg(size), max(size)"
                     " FROM blob WHERE size>0");
      db_step(&q);
      t = db_column_int64(&q, 0);
      szAvg = db_column_int(&q, 1);
      szMax = db_column_int(&q, 2);
      db_finalize(&q);
      bigSizeName(sizeof(zBuf), zBuf, t);
      fossil_print( "%*s%d average, "
                    "%d max, %s total\n",
                    colWidth, "artifact-sizes:",
                    szAvg, szMax, zBuf);
      if( t/fsize < 5 ){
        b = 10;
        fsize /= 10;
      }else{
        b = 1;
      }
      a = t/fsize;
      fossil_print("%*s%d:%d\n", colWidth, "compression-ratio:", a, b);
    }
    n = db_int(0, "SELECT COUNT(*) FROM event e WHERE e.type='ci'");
    fossil_print("%*s%d\n", colWidth, "checkins:", n);
    n = db_int(0, "SELECT count(*) FROM filename /*scan*/");
    fossil_print("%*s%d across all branches\n", colWidth, "files:", n);
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
                  " WHERE tagname GLOB 'wiki-*'");
    m = db_int(0, "SELECT COUNT(*) FROM event WHERE type='w'");
    fossil_print("%*s%d (%d changes)\n", colWidth, "wikipages:", n, m);
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
                  " WHERE tagname GLOB 'tkt-*'");
    m = db_int(0, "SELECT COUNT(*) FROM event WHERE type='t'");
    fossil_print("%*s%d (%d changes)\n", colWidth, "tickets:", n, m);
    n = db_int(0, "SELECT COUNT(*) FROM event WHERE type='e'");
    fossil_print("%*s%d\n", colWidth, "events:", n);
    n = db_int(0, "SELECT COUNT(*) FROM event WHERE type='g'");
    fossil_print("%*s%d\n", colWidth, "tagchanges:", n);
    z = db_text(0, "SELECT datetime(mtime) ||"
                   " printf(' - about %%d days ago ',julianday('now') - mtime)"
                   " FROM event "
                   " ORDER BY mtime DESC LIMIT 1");
    fossil_print("%*s%s\n", colWidth, "latest-change:", z);
  }
  n = db_int(0, "SELECT julianday('now') - (SELECT min(mtime) FROM event)"
                " + 0.99");
  fossil_print("%*s%d days or approximately %.2f years.\n",
               colWidth, "project-age:", n, n/365.2425);
  p = db_get("project-code", 0);
  if( p ){
    fossil_print("%*s%s\n", colWidth, "project-id:", p);
  }
#if 0
  /* Server-id is not useful information any more */
  fossil_print("%*s%s\n", colWidth, "server-id:", db_get("server-code", 0));
#endif
  if( !omitVers ){
    fossil_print("%*s%s %s [%s] (%s)\n",
                 colWidth, "fossil-version:",
                 MANIFEST_DATE, MANIFEST_VERSION, RELEASE_VERSION,
                 COMPILER_NAME);
    fossil_print("%*s%.19s [%.10s] (%s)\n",
                 colWidth, "sqlite-version:",
                 sqlite3_sourceid(), &sqlite3_sourceid()[20],
                 sqlite3_libversion());
  }
  zDb = db_name("repository");
  fossil_print("%*s%d pages, %d bytes/pg, %d free pages, "
               "%s, %s mode\n",
               colWidth, "database-stats:",
               db_int(0, "PRAGMA %s.page_count", zDb),
               db_int(0, "PRAGMA %s.page_size", zDb),
               db_int(0, "PRAGMA %s.freelist_count", zDb),
               db_text(0, "PRAGMA %s.encoding", zDb),
               db_text(0, "PRAGMA %s.journal_mode", zDb));

}

/*
** WEBPAGE: urllist
**
** Show ways in which this repository has been accessed
*/
void urllist_page(void){
  Stmt q;
  int cnt;
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(); return; }

  style_header("URLs and Checkouts");
  style_submenu_element("Stat", "Repository Stats", "stat");
  style_submenu_element("Schema", "Repository Schema", "repo_schema");
  @ <div class="section">URLs</div>
  @ <table border="0" width='100%%'>
  db_prepare(&q, "SELECT substr(name,9), datetime(mtime,'unixepoch')"
                 "  FROM config WHERE name GLOB 'baseurl:*' ORDER BY 2 DESC");
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    @ <tr><td width='100%%'>%h(db_column_text(&q,0))</td>
    @ <td><nobr>%h(db_column_text(&q,1))</nobr></td></tr>
    cnt++;
  }
  db_finalize(&q);
  if( cnt==0 ){
    @ <tr><td>(none)</td>
  }
  @ </table>
  @ <div class="section">Checkouts</div>
  @ <table border="0" width='100%%'>
  db_prepare(&q, "SELECT substr(name,7), datetime(mtime,'unixepoch')"
                 "  FROM config WHERE name GLOB 'ckout:*' ORDER BY 2 DESC");
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    @ <tr><td width='100%%'>%h(db_column_text(&q,0))</td>
    @ <td><nobr>%h(db_column_text(&q,1))</nobr></td></tr>
    cnt++;
  }
  db_finalize(&q);
  if( cnt==0 ){
    @ <tr><td>(none)</td>
  }
  @ </table>
  style_footer();
}

/*
** WEBPAGE: repo_schema
**
** Show the repository schema
*/
void repo_schema_page(void){
  Stmt q;
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(); return; }

  style_header("Repository Schema");
  style_submenu_element("Stat", "Repository Stats", "stat");
  style_submenu_element("URLs", "URLs and Checkouts", "urllist");
  db_prepare(&q, "SELECT sql FROM %s.sqlite_master WHERE sql IS NOT NULL",
             db_name("repository"));
  @ <pre>
  while( db_step(&q)==SQLITE_ROW ){
    @ %h(db_column_text(&q, 0));
  }
  @ </pre>
  db_finalize(&q);
  style_footer();
}
