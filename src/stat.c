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
** WEBPAGE: stat
**
** Show statistics and global information about the repository.
*/
void stat_page(void){
  i64 t;
  int n, m, fsize;
  char zBuf[100];
  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  style_header("Repository Statistics");
  @ <p><table class="label-value">
  @ <tr><th>Repository&nbsp;Size:</th><td>
  fsize = file_size(g.zRepositoryName);
  @ %d(fsize) bytes
  @ </td></tr>
  @ <tr><th>Number&nbsp;Of&nbsp;Artifacts:</th><td>
  n = db_int(0, "SELECT count(*) FROM blob");
  m = db_int(0, "SELECT count(*) FROM delta");
  @ %d(n) (stored as %d(n-m) full text and %d(m) delta blobs)
  @ </td></tr>
  if( n>0 ){
    int a, b;
    @ <tr><th>Uncompressed&nbsp;Artifact&nbsp;Size:</th><td>
    t = db_int64(0, "SELECT total(size) FROM blob WHERE size>0");
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", t);
    @ %d((int)(((double)t)/(double)n)) bytes average, %s(zBuf) bytes total
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
  @ <tr><th>Duration&nbsp;Of&nbsp;Project:</th><td>
  n = db_int(0, "SELECT julianday('now') - (SELECT min(mtime) FROM event)"
                " + 0.99");
  @ %d(n) days
  @ </td></tr>
  @ <tr><th>Project&nbsp;ID:</th><td>
  @ %h(db_get("project-code",""))
  @ </td></tr>
  @ <tr><th>Server&nbsp;ID:</th><td>
  @ %h(db_get("server-code",""))
  @ </td></tr>

  @ <tr><th>Fossil&nbsp;Version:</th><td>
  @ %h(MANIFEST_DATE) %h(MANIFEST_VERSION)
  @ </td></tr>
  @ <tr><th>SQLite&nbsp;Version:</th><td>
  @ %h(db_text(0, "SELECT substr(sqlite_source_id(),1,30)"))
  @ (%h(SQLITE_VERSION))
  @ </td></tr>
  @ <tr><th>Database&nbsp;Stats:</th><td>
  @ %d(db_int(0, "PRAGMA %s.page_count", g.zRepoDb)) pages,
  @ %d(db_int(0, "PRAGMA %s.page_size", g.zRepoDb)) bytes/page,
  @ %d(db_int(0, "PRAGMA %s.freelist_count", g.zRepoDb)) free pages,
  @ %s(db_text(0, "PRAGMA %s.encoding", g.zRepoDb)),
  @ %s(db_text(0, "PRAGMA %s.journal_mode", g.zRepoDb)) mode
  @ </td></tr>

  @ </table></p>
  style_footer();
}
