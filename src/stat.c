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
    sqlite3_snprintf(nOut, zOut, "%,lld bytes", v);
  }else if( v<1000000000 ){
    sqlite3_snprintf(nOut, zOut, "%,lld bytes (%.1fMB)",
                    v, (double)v/1000000.0);
  }else{
    sqlite3_snprintf(nOut, zOut, "%,lld bytes (%.1fGB)",
                    v, (double)v/1000000000.0);
  }
}

/*
** Return the approximate size as KB, MB, GB, or TB.
*/
void approxSizeName(int nOut, char *zOut, sqlite3_int64 v){
  if( v<1000 ){
    sqlite3_snprintf(nOut, zOut, "%,lld bytes", v);
  }else if( v<1000000 ){
    sqlite3_snprintf(nOut, zOut, "%.1fKB", (double)v/1000.0);
  }else if( v<1000000000 ){
    sqlite3_snprintf(nOut, zOut, "%.1fMB", (double)v/1000000.0);
  }else{
    sqlite3_snprintf(nOut, zOut, "%.1fGB", (double)v/1000000000.0);
  }
}

/*
** Generate stats for the email notification subsystem.
*/
void stats_for_email(void){
  const char *zDest = db_get("email-send-method",0);
  int nSub, nASub, nPend, nDPend;
  const char *zDir, *zDb, *zCmd, *zRelay;
  int iCutoff;
  double rDigest;
  @ <tr><th>Outgoing&nbsp;Email:</th><td>
  if( fossil_strcmp(zDest,"pipe")==0
   && (zCmd = db_get("email-send-command",0))!=0
  ){
    @ Piped to command "%h(zCmd)"
  }else
  if( fossil_strcmp(zDest,"db")==0
   && (zDb = db_get("email-send-db",0))!=0
  ){
    sqlite3 *db;
    sqlite3_stmt *pStmt;
    int rc;
    @ Queued to database "%h(zDb)"
    g.dbIgnoreErrors++;
    rc = sqlite3_open(zDb, &db);
    if( rc==SQLITE_OK ){
      rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM email",-1,&pStmt,0);
      if( rc==SQLITE_OK && sqlite3_step(pStmt)==SQLITE_ROW ){
        @ (%,d(sqlite3_column_int(pStmt,0)) messages,
        @ %,d(file_size(zDb,ExtFILE)) bytes)
      }
      sqlite3_finalize(pStmt);
    }
    g.dbIgnoreErrors--;
    if( rc ){
      @ &larr; cannot access database!
    }
    sqlite3_close(db);
  }else
  if( fossil_strcmp(zDest,"dir")==0
   && (zDir = db_get("email-send-dir",0))!=0
  ){
    @ Written to files in "%h(zDir)"
    @ (%,d(file_directory_size(zDir,0,1)) messages)
  }else
  if( fossil_strcmp(zDest,"relay")==0
   && (zRelay = db_get("email-send-relayhost",0))!=0
  ){
    @ Relay to %h(zRelay) using SMTP
  }
  else{
    @ Off
  }
  @ </td></tr>
  nPend = db_int(0,"SELECT count(*) FROM pending_alert WHERE NOT sentSep");
  nDPend = db_int(0,"SELECT count(*) FROM pending_alert"
                    " WHERE NOT sentDigest");
  @ <tr><th>Pending&nbsp;Alerts:</th><td>
  @ %,d(nPend) normal, %,d(nDPend) digest
  @ </td></tr>
  if( g.perm.Admin ){
    @ <tr><th><a href="%R/subscribers">Subscribers:</a></th><td>
  }else{
    @ <tr><th>Subscribers:</th><td>
  }
  nSub = db_int(0, "SELECT count(*) FROM subscriber");
  iCutoff = db_get_int("email-renew-cutoff",0);
  nASub = db_int(0, "SELECT count(*) FROM subscriber WHERE sverified"
                   " AND NOT sdonotcall AND octet_length(ssub)>1"
                   " AND lastContact>=%d;", iCutoff);
  @ %,d(nASub) active, %,d(nSub) total
  @ </td></tr>
  rDigest = db_double(-1.0, "SELECT (julianday('now') - value)*24.0"
                            " FROM config WHERE name='email-last-digest'");
  if( rDigest>0.0 ){
    @ <tr><th>Last Digest:</th><td>Approximately \
    if( rDigest>48.0 ){
      @ %.1f(rDigest/24.0) days ago</td>
    }else{
      @ %.1f(rDigest) hours ago</td>
    }
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
  int brief;
  const char *p;
  char *z;
  int Y, M, D;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  brief = P("brief")!=0;
  style_header("Repository Statistics");
  style_adunit_config(ADUNIT_RIGHT_OK);
  if( g.perm.Admin ){
    style_submenu_element("URLs", "urllist");
    style_submenu_element("Schema", "repo_schema");
    style_submenu_element("Web-Cache", "cachestat");
  }
  style_submenu_element("Activity Reports", "reports");
  style_submenu_element("Hash Collisions", "hash-collisions");
  style_submenu_element("Artifacts", "bloblist");
  if( sqlite3_compileoption_used("ENABLE_DBSTAT_VTAB") ){
    style_submenu_element("Table Sizes", "repo-tabsize");
  }
  if( g.perm.Admin || g.perm.Setup || db_get_boolean("test_env_enable",0) ){
    style_submenu_element("Environment", "test-env");
  }
  @ <table class="label-value">
  fsize = file_size(g.zRepositoryName, ExtFILE);
  @ <tr><th>Repository&nbsp;Size:</th><td>%,lld(fsize) bytes</td>
  @ </td></tr>
  if( !brief ){
    @ <tr><th>Number&nbsp;Of&nbsp;Artifacts:</th><td>
    n = db_int(0, "SELECT count(*) FROM blob WHERE content IS NOT NULL");
    m = db_int(0, "SELECT count(*) FROM delta");
    @ %,d(n) (%,d(n-m) fulltext and %,d(m) deltas)
    if( g.perm.Write ){
      @ <a href='%R/artifact_stats'>Details</a>
    }
    @ </td></tr>
    if( n>0 ){
      int a, b;
      Stmt q;
      @ <tr><th>Uncompressed&nbsp;Artifact&nbsp;Size:</th><td>
      db_prepare(&q, "SELECT total(size), avg(size), max(size)"
                     " FROM blob WHERE content IS NOT NULL /*scan*/");
      db_step(&q);
      t = db_column_int64(&q, 0);
      szAvg = db_column_int(&q, 1);
      szMax = db_column_int(&q, 2);
      db_finalize(&q);
      @ %,d(szAvg) bytes average, %,d(szMax) bytes max, %,lld(t) total
      @ </td></tr>
      @ <tr><th>Compression&nbsp;Ratio:</th><td>
      if( t/fsize < 5 ){
        b = 10;
        a = t/(fsize/10);
      }else{
        b = 1;
        a = t/fsize;
      }
      @ %d(a):%d(b)
      @ </td></tr>
    }
    if( db_table_exists("repository","unversioned") ){
      Stmt q;
      char zStored[100];
      db_prepare(&q,
        "SELECT count(*), sum(sz), sum(octet_length(content))"
        "  FROM unversioned"
        " WHERE length(hash)>1"
      );
      if( db_step(&q)==SQLITE_ROW && (n = db_column_int(&q,0))>0 ){
        sqlite3_int64 iStored, pct;
        iStored = db_column_int64(&q,2);
        pct = (iStored*100 + fsize/2)/fsize;
        approxSizeName(sizeof(zStored), zStored, iStored);
        @ <tr><th>Unversioned&nbsp;Files:</th><td>
        @ %z(href("%R/uvlist"))%d(n) files</a>,
        @ %s(zStored) compressed, %d(pct)%% of total repository space
        @ </td></tr>
      }
      db_finalize(&q);
    }
    @ <tr><th>Number&nbsp;Of&nbsp;Check-ins:</th><td>
    n = db_int(0, "SELECT count(*) FROM event WHERE type='ci' /*scan*/");
    @ %,d(n)
    @ </td></tr>
    @ <tr><th>Number&nbsp;Of&nbsp;Files:</th><td>
    n = db_int(0, "SELECT count(*) FROM filename /*scan*/");
    @ %,d(n)
    @ </td></tr>
    @ <tr><th>Number&nbsp;Of&nbsp;Wiki&nbsp;Pages:</th><td>
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
                  " WHERE +tagname GLOB 'wiki-*'");
    @ %,d(n)
    @ </td></tr>
    if( g.perm.Chat && db_table_exists("repository","chat") ){
      sqlite3_int64 sz = 0;
      char zSz[100];
      n = db_int(0, "SELECT max(msgid) FROM chat");
      m = db_int(0, "SELECT count(*) FROM chat WHERE mdel IS NOT TRUE");
      sz = db_int64(0, "SELECT sum(coalesce(octet_length(xmsg),0)+"
                                  "coalesce(octet_length(file),0)) FROM chat");
      approxSizeName(sizeof(zSz), zSz, sz);
      @ <tr><th>Number&nbsp;Of&nbsp;Chat&nbsp;Messages:</th>
      @ <td>%,d(n) (%,d(m) still alive, %s(zSz) in size)</td></tr>
    }
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
                  " WHERE +tagname GLOB 'tkt-*'");
    if( n>0 ){
      @ <tr><th>Number&nbsp;Of&nbsp;Tickets:</th><td>%,d(n)</td></tr>
    }
    if( db_table_exists("repository","forumpost") ){
      n = db_int(0, "SELECT count(*) FROM forumpost/*scan*/");
      if( n>0 ){
        int nThread = db_int(0, "SELECT count(*) FROM forumpost"
                                " WHERE froot=fpid");
        @ <tr><th>Number&nbsp;Of&nbsp;Forum&nbsp;Posts:</th>
        @ <td>%,d(n) on %d(nThread) threads</td></tr>
      }
    }
  }
  @ <tr><th>Project&nbsp;Age:</th><td>
  z = db_text(0, "SELECT timediff('now',(SELECT min(mtime) FROM event));");
  sscanf(z, "+%d-%d-%d", &Y, &M, &D);
  if( Y>0 ){
    @ %d(Y) year%s(Y==1?"":"s") \
  }
  if( M>0 ){
    @ %d(M) month%s(M==1?"":"s") \
  }
  if( D>0 || (Y==0 && M==0) ){
    @ %d(D) day%s(D==1?"":"s")
  }
  @ </td></tr>
  p = db_get("project-code", 0);
  if( p ){
    @ <tr><th>Project&nbsp;ID:</th>
    @     <td>%h(p) %h(db_get("project-name",""))</td></tr>
  }
  p = db_get("parent-project-code", 0);
  if( p ){
    @ <tr><th>Parent&nbsp;Project&nbsp;ID:</th>
    @      <td>%h(p) %h(db_get("parent-project-name",""))</td></tr>
  }
  /* @ <tr><th>Server&nbsp;ID:</th><td>%h(db_get("server-code",""))</td></tr> */
  @ <tr><th>Fossil&nbsp;Version:</th><td>
  @ %h(MANIFEST_DATE) %h(MANIFEST_VERSION)
  @ (%h(RELEASE_VERSION)) <a href='version?verbose'>(details)</a>
  @ </td></tr>
  @ <tr><th>SQLite&nbsp;Version:</th><td>%.19s(sqlite3_sourceid())
  @ [%.10s(&sqlite3_sourceid()[20])] (%s(sqlite3_libversion()))
  @ <a href='version?verbose'>(details)</a></td></tr>
  if( g.perm.Admin ){
    const char *zCgi = P("SERVER_SOFTWARE");
    @ <tr><th>OpenSSL&nbsp;Version:</th>
    @     <td>%z(fossil_openssl_version())</td></tr>
    if( zCgi ){
      @ <tr><th>Web&nbsp;Server:</th><td>%s(zCgi)</td></tr>
    }
  }
  if( g.eHashPolicy!=HPOLICY_AUTO ){
    @ <tr><th>Schema&nbsp;Version:</th><td>%h(g.zAuxSchema),
    @ %s(hpolicy_name())</td></tr>
  }else{
    @ <tr><th>Schema&nbsp;Version:</th><td>%h(g.zAuxSchema)</td></tr>
  }
  @ <tr><th>Repository Rebuilt:</th><td>
  @ %h(db_get_mtime("rebuilt","%Y-%m-%d %H:%M:%S","Never"))
  @ By Fossil %h(db_get("rebuilt","Unknown"))</td></tr>
  @ <tr><th>Database&nbsp;Stats:</th><td>
  @ %,d(db_int(0, "PRAGMA repository.page_count")) pages,
  @ %d(db_int(0, "PRAGMA repository.page_size")) bytes/page,
  @ %,d(db_int(0, "PRAGMA repository.freelist_count")) free pages,
  @ %s(db_text(0, "PRAGMA repository.encoding")),
  @ %s(db_text(0, "PRAGMA repository.journal_mode")) mode
  @ </td></tr>
  if( g.perm.Admin && g.zErrlog && g.zErrlog[0] ){
    i64 szFile = file_size(g.zErrlog, ExtFILE);
    if( szFile>=0 ){
      @ <tr><th>Error Log:</th>
      @ <td><a href='%R/errorlog'>%h(g.zErrlog)</a> (%,lld(szFile) bytes)
    }
    @ </td></tr>
  }
  if( g.perm.Admin ){
    @ <tr><th>Backoffice:</th>
    @ <td>Last run: %z(backoffice_last_run())</td></tr>
  }
  if( g.perm.Admin && alert_enabled() ){
    stats_for_email();
  }

  @ </table>
  style_finish_page();
}

/*
** COMMAND: dbstat
**
** Usage: %fossil dbstat OPTIONS
**
** Shows statistics and global information about the repository and/or
** verify the integrity of a repository.
**
** Options:
**   -b|--brief           Only show essential elements
**   --db-check           Run "PRAGMA quick_check" on the repository database
**   --db-verify          Run a full verification of the repository integrity.
**                        This involves decoding and reparsing all artifacts
**                        and can take significant time.
**   --omit-version-info  Omit the SQLite and Fossil version information
*/
void dbstat_cmd(void){
  i64 t, fsize;
  int n, m;
  int szMax, szAvg;
  int brief;
  int omitVers;            /* Omit Fossil and SQLite version information */
  int dbCheck;             /* True for the --db-check option */
  const int colWidth = -19 /* printf alignment/width for left column */;
  const char *p, *z;

  brief = find_option("brief", "b",0)!=0;
  omitVers = find_option("omit-version-info", 0, 0)!=0;
  dbCheck = find_option("db-check",0,0)!=0;
  if( find_option("db-verify",0,0)!=0 ) dbCheck = 2;
  db_find_and_open_repository(0,0);

  /* We should be done with options.. */
  verify_all_options();

  if( (z = db_get("project-name",0))!=0
   || (z = db_get("short-project-name",0))!=0
  ){
    fossil_print("%*s%s\n", colWidth, "project-name:", z);
  }
  fsize = file_size(g.zRepositoryName, ExtFILE);
  fossil_print( "%*s%,lld bytes\n", colWidth, "repository-size:", fsize);
  if( !brief ){
    n = db_int(0, "SELECT count(*) FROM blob WHERE content IS NOT NULL");
    m = db_int(0, "SELECT count(*) FROM delta");
    fossil_print("%*s%,d (stored as %,d full text and %,d deltas)\n",
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
      fossil_print( "%*s%,d average, "
                    "%,d max, %,lld total\n",
                    colWidth, "artifact-sizes:",
                    szAvg, szMax, t);
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
    fossil_print("%*s%,d\n", colWidth, "check-ins:", n);
    n = db_int(0, "SELECT count(*) FROM filename /*scan*/");
    fossil_print("%*s%,d across all branches\n", colWidth, "files:", n);
    n = db_int(0, "SELECT count(*) FROM ("
               "SELECT DISTINCT substr(tagname,6) "
               "FROM tag JOIN tagxref USING('tagid')"
               " WHERE tagname GLOB 'wiki-*'"
               " AND TYPEOF(tagxref.value+0)='integer'"
               ")");
    m = db_int(0, "SELECT COUNT(*) FROM event WHERE type='w'");
    fossil_print("%*s%,d (%,d changes)\n", colWidth, "wiki-pages:", n, m);
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
                  " WHERE tagname GLOB 'tkt-*'");
    m = db_int(0, "SELECT COUNT(*) FROM event WHERE type='t'");
    fossil_print("%*s%,d (%,d changes)\n", colWidth, "tickets:", n, m);
    n = db_int(0, "SELECT COUNT(*) FROM event WHERE type='e'");
    fossil_print("%*s%,d\n", colWidth, "events:", n);
    if( db_table_exists("repository","forumpost") ){
      n = db_int(0, "SELECT count(*) FROM forumpost/*scan*/");
      if( n>0 ){
        int nThread = db_int(0, "SELECT count(*) FROM forumpost"
                                " WHERE froot=fpid");
        fossil_print("%*s%,d (on %,d threads)\n", colWidth, "forum-posts:",
                     n, nThread);
      }
    }
    n = db_int(0, "SELECT COUNT(*) FROM event WHERE type='g'");
    fossil_print("%*s%,d\n", colWidth, "tag-changes:", n);
    z = db_text(0, "SELECT datetime(mtime) || ' - about ' ||"
                   " CAST(julianday('now') - mtime AS INTEGER)"
                   " || ' days ago' FROM event "
                   " ORDER BY mtime DESC LIMIT 1");
    fossil_print("%*s%s\n", colWidth, "latest-change:", z);
  }
  n = db_int(0, "SELECT julianday('now') - (SELECT min(mtime) FROM event)"
                " + 0.99");
  fossil_print("%*s%,d days or approximately %.2f years.\n",
               colWidth, "project-age:", n, n/365.2425);
  if( !brief ){
    p = db_get("project-code", 0);
    if( p ){
      fossil_print("%*s%s\n", colWidth, "project-id:", p);
    }
  }
#if 0
  /* Server-id is not useful information any more */
  fossil_print("%*s%s\n", colWidth, "server-id:", db_get("server-code", 0));
#endif
  fossil_print("%*s%s\n", colWidth, "schema-version:", g.zAuxSchema);
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
  fossil_print("%*s%,d pages, %d bytes/pg, %,d free pages, "
               "%s, %s mode\n",
               colWidth, "database-stats:",
               db_int(0, "PRAGMA repository.page_count"),
               db_int(0, "PRAGMA repository.page_size"),
               db_int(0, "PRAGMA repository.freelist_count"),
               db_text(0, "PRAGMA repository.encoding"),
               db_text(0, "PRAGMA repository.journal_mode"));
  if( dbCheck ){
    if( dbCheck<2 ){
      char *zRes = db_text(0, "PRAGMA repository.quick_check(1)");
      fossil_print("%*s%s\n", colWidth, "database-check:", zRes);
    }else{
      char *newArgv[3];
      newArgv[0] = g.argv[0];
      newArgv[1] = "test-integrity";
      newArgv[2] = 0;
      g.argv = newArgv;
      g.argc = 2;
      fossil_print("Full repository verification follows:\n");
      test_integrity();
    }
  }
}

/*
** Return a string which is the public URL used to access this repository.
** Or return a NULL pointer if this repository does not have a public
** access URL.
**
** Algorithm:
**
** The public URL is given by the email-url property.  But it is only
** returned if there have been one more more accesses (as recorded by
** "baseurl:URL" entries in the CONFIG table).
*/
const char *public_url(void){
  const char *zUrl = db_get("email-url", 0);
  if( zUrl==0 ) return 0;
  if( !db_exists("SELECT 1 FROM config WHERE name='baseurl:%q'", zUrl) ){
    return 0;
  }
  return zUrl;
}


/*
** WEBPAGE: urllist
**
** Show ways in which this repository has been accessed
*/
void urllist_page(void){
  Stmt q;
  int cnt;
  int total = 0;
  int showAll = P("all")!=0;
  int nOmitted;
  sqlite3_int64 iNow;
  char *zPriorRepo = 0;

  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(0); return; }

  style_set_current_feature("stat");
  style_header("URLs and Checkouts");
  style_adunit_config(ADUNIT_RIGHT_OK);
  style_submenu_element("Stat", "stat");
  style_submenu_element("Schema", "repo_schema");
  iNow = db_int64(0, "SELECT strftime('%%s','now')");


  db_prepare(&q, "SELECT substr(name,9), datetime(mtime,'unixepoch'), mtime"
                 "  FROM config WHERE name GLOB 'baseurl:*' ORDER BY 3 DESC");
  cnt = 0;
  nOmitted = 0;
  while( db_step(&q)==SQLITE_ROW ){
    if( cnt==0 ){
      @ <div class="section">URLs used to access this repository</div>
      @ <table border="0" width='100%%'>
    }
    if( !showAll && db_column_int64(&q,2)<(iNow - 3600*24*30) && cnt>8 ){
      nOmitted++;
    }else{
      @ <tr><td width='100%%'>%h(db_column_text(&q,0))</td>
      @ <td><nobr>%h(db_column_text(&q,1))</nobr></td></tr>
    }
    cnt++;
  }
  db_finalize(&q);

  if( nOmitted ){
    @ <tr><td><a href="urllist?all"><i>Show %d(nOmitted) more...</i></a>
  }
  if( cnt ){
    @ </table>
    total += cnt;
  }
  if( P("urlonly") ){
    style_finish_page();
    return;
  }


  db_prepare(&q, "SELECT substr(name,7), datetime(mtime,'unixepoch')"
                 "  FROM config WHERE name GLOB 'ckout:*' ORDER BY 2 DESC");
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPath = db_column_text(&q,0);
    if( vfile_top_of_checkout(zPath) ){
      if( cnt==0 ){
        @ <div class="section">Checkouts</div>
        @ <table border="0" width='100%%'>
      }
      @ <tr><td width='100%%'>%h(zPath)</td>
      @ <td><nobr>%h(db_column_text(&q,1))</nobr></td></tr>
      cnt++;
    }
  }
  db_finalize(&q);
  if( cnt ){
    @ </table>
    total += cnt;
  }

  cnt = 0;
  db_prepare(&q,
    "SELECT substr(name,10), datetime(mtime,'unixepoch')"
    "  FROM config WHERE name GLOB 'syncwith:*'"
    "UNION ALL "
    "SELECT substr(name,10), datetime(mtime,'unixepoch')"
    "  FROM config WHERE name GLOB 'syncfrom:*'"
    "UNION ALL "
    "SELECT substr(name,9), datetime(mtime,'unixepoch')"
    "  FROM config WHERE name GLOB 'gitpush:*'"
    "GROUP BY 1 ORDER BY 2 DESC"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zURL = db_column_text(&q,0);
    UrlData x;
    if( cnt==0 ){
      @ <div class="section">Recently synced with these URLs</div>
      @ <table border='0' width='100%%'>
    }
    memset(&x, 0, sizeof(x));
    url_parse_local(zURL, URL_OMIT_USER, &x);
    @ <tr><td width='100%%'><a href='%h(x.canonical)'>%h(x.canonical)</a>
    @ <td><nobr>%h(db_column_text(&q,1))</nobr></td></tr>
    cnt++;
    url_unparse(&x);
  }
  db_finalize(&q);
  if( cnt ){
    @ </table>
    total += cnt;
  }

  cnt = 0;
  db_prepare(&q,
    "SELECT"
    " substr(name,6),"
    " datetime(mtime,'unixepoch'),"
    " value->>'type',"
    " value->>'src'\n"
    "FROM config\n"
    "WHERE name GLOB 'link:*'\n"
    "AND json_valid(value)\n"
    "ORDER BY 4, 2 DESC"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUrl = db_column_text(&q, 0);
    const char *zType = db_column_text(&q, 2);
    const char *zSrc = db_column_text(&q, 3);
    if( zUrl==0 || zSrc==0 ) continue;
    if( cnt++==0 ){
      @ <div class="section">Links from other repositories</div>
      @ <table border='0' width='100%%'>
    }
    if( zPriorRepo==0 || strcmp(zPriorRepo,zSrc)!=0 ){
      fossil_free(zPriorRepo);
      zPriorRepo = fossil_strdup(zSrc);
      @ <tr><td colspan="4">\
      @ From <a href='%T(zSrc)'>%h(zSrc)</a>...</td></tr>
    }
    @ <tr><td>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</td>
    @ <td width='90%%'><a href='%h(zUrl)'>%h(zUrl)</a></td>
    if( zType ){
      @ <td>&nbsp;(%h(zType))&nbsp;</td>
    }else{
      @ <td>&nbsp;</td>
    }
    @ <td><nobr>%h(db_column_text(&q,1))</nobr></td></tr>
  }
  db_finalize(&q);
  fossil_free(zPriorRepo);
  if( cnt ){
    @ </table>
    total += cnt;
  }

  cnt = 0;
  db_prepare(&q,
    "SELECT"
    " value,"
    " url_nouser(value),"
    " substr(name,10),"
    " datetime(mtime,'unixepoch')"
    "FROM config\n"
    "WHERE name GLOB 'sync-url:*'\n"
    "ORDER BY 2"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUrl = db_column_text(&q, 0);
    const char *zLink = db_column_text(&q, 1);
    const char *zName = db_column_text(&q, 2);
    if( cnt++==0 ){
      @ <div class="section">Defined sync targets</div>
      @ <table border='0' width='100%%'>
    }
    @ <tr><td>%h(zName)</td><td>&nbsp;&nbsp;</td>
    @ <td width='95%%'><a href='%h(zLink)'>%h(zUrl)</a></td>
    @ <td><nobr>%h(db_column_text(&q,3))</nobr></td></tr>
  }
  db_finalize(&q);
  if( cnt ){
    @ </table>
    total += cnt;
  }


  if( total==0 ){
    @ <p>No record of any URLs or checkouts</p>
  }
  style_finish_page();
}

/*
** WEBPAGE: repo_schema
**
** Show the repository schema
*/
void repo_schema_page(void){
  Stmt q;
  Blob sql;
  const char *zArg = P("n");
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(0); return; }

  if( zArg!=0
   && db_table_exists("repository",zArg)
   && cgi_csrf_safe(1)
  ){
    if( P("analyze")!=0 ){
      db_multi_exec("ANALYZE \"%w\"", zArg);
    }else if( P("analyze200")!=0 ){
      db_multi_exec("PRAGMA analysis_limit=200; ANALYZE \"%w\"", zArg);
    }else if( P("deanalyze")!=0 ){
      db_unprotect(PROTECT_ALL);
      db_multi_exec("DELETE FROM repository.sqlite_stat1"
                    " WHERE tbl LIKE %Q", zArg);
      db_protect_pop();
    }
  }

  style_set_current_feature("stat");
  style_header("Repository Schema");
  style_adunit_config(ADUNIT_RIGHT_OK);
  style_submenu_element("Stat", "stat");
  style_submenu_element("URLs", "urllist");
  if( sqlite3_compileoption_used("ENABLE_DBSTAT_VTAB") ){
    style_submenu_element("Table Sizes", "repo-tabsize");
  }
  blob_init(&sql,
    "SELECT sql FROM repository.sqlite_schema WHERE sql IS NOT NULL", -1);
  if( zArg ){
    style_submenu_element("All", "repo_schema");
    blob_appendf(&sql, " AND (tbl_name=%Q OR name=%Q)", zArg, zArg);
  }
  blob_appendf(&sql, " ORDER BY tbl_name, type<>'table', name");
  db_prepare(&q, "%s", blob_str(&sql)/*safe-for-%s*/);
  blob_reset(&sql);
  @ <pre>
  while( db_step(&q)==SQLITE_ROW ){
    @ %h(db_column_text(&q, 0));
  }
  @ </pre>
  db_finalize(&q);
  if( db_table_exists("repository","sqlite_stat1") ){
    if( zArg ){
      db_prepare(&q,
        "SELECT tbl, idx, stat FROM repository.sqlite_stat1"
        " WHERE tbl LIKE %Q OR idx LIKE %Q"
        " ORDER BY tbl, idx", zArg, zArg);

      @ <hr>
      @ <pre>
      while( db_step(&q)==SQLITE_ROW ){
        const char *zTab = db_column_text(&q,0);
        const char *zIdx = db_column_text(&q,1);
        const char *zStat = db_column_text(&q,2);
        @ INSERT INTO sqlite_stat1 VALUES('%h(zTab)','%h(zIdx)','%h(zStat)');
      }
      @ </pre>
      db_finalize(&q);
    }else{
      style_submenu_element("Stat1","repo_stat1");
    }
  }
  @ <hr><form method="POST">
  @ <input type="submit" name="analyze" value="Run ANALYZE"><br />
  @ <input type="submit" name="analyze200"\
  @  value="Run ANALYZE with limit=200"><br />
  @ <input type="submit" name="deanalyze" value="De-ANALYZE">
  @ </form>

  style_finish_page();
}

/*
** WEBPAGE: repo_stat1
**
** Show the sqlite_stat1 table for the repository schema
*/
void repo_stat1_page(void){
  int bTabular;
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(0); return; }
  bTabular = PB("tabular");

  if( P("analyze")!=0 && cgi_csrf_safe(1) ){
    db_multi_exec("ANALYZE");
  }else if( P("analyze200")!=0 && cgi_csrf_safe(1) ){
    db_multi_exec("PRAGMA analysis_limit=200; ANALYZE;");
  }else if( P("deanalyze")!=0 && cgi_csrf_safe(1) ){
    db_unprotect(PROTECT_ALL);
    db_multi_exec("DELETE FROM repository.sqlite_stat1;");
    db_protect_pop();
  }
  style_set_current_feature("stat");
  style_header("Repository STAT1 Table");
  style_adunit_config(ADUNIT_RIGHT_OK);
  style_submenu_element("Stat", "stat");
  style_submenu_element("Schema", "repo_schema");
  style_submenu_checkbox("tabular", "Tabular", 0, 0);
  if( db_table_exists("repository","sqlite_stat1") ){
    Stmt q;
    db_prepare(&q,
      "SELECT tbl, idx, stat FROM repository.sqlite_stat1"
      " ORDER BY tbl, idx");
    if( bTabular ){
      @ <table border="1" cellpadding="0" cellspacing="0">
      @ <tr><th>Table<th>Index<th>Stat
    }else{
      @ <pre>
    }
    while( db_step(&q)==SQLITE_ROW ){
      const char *zTab = db_column_text(&q,0);
      const char *zIdx = db_column_text(&q,1);
      const char *zStat = db_column_text(&q,2);
      char *zUrl = href("%R/repo_schema?n=%t",zTab);
      if( bTabular ){
        @ <tr><td>%z(zUrl)%h(zTab)</a><td>%h(zIdx)<td>%h(zStat)
      }else{
        @ INSERT INTO sqlite_stat1 \
        @ VALUES('%z(zUrl)%h(zTab)</a>','%h(zIdx)','%h(zStat)');
      }
    }
    if( bTabular ){
      @ </table>
    }else{
      @ </pre>
    }
    db_finalize(&q);
  }
  @ <p><form method="POST">
  if( bTabular ){
    @ <input type="hidden" name="tabular" value="1">
  }
  @ <input type="submit" name="analyze" value="Run ANALYZE"><br />
  @ <input type="submit" name="analyze200"\
  @  value="Run ANALYZE with limit=200"><br>
  @ <input type="submit" name="deanalyze"\
  @  value="De-ANALYZE">
  @ </form>
  style_finish_page();
}

/*
** WEBPAGE: repo-tabsize
**
** Show relative sizes of tables in the repository database.
*/
void repo_tabsize_page(void){
  int nPageFree;
  sqlite3_int64 fsize;
  char zBuf[100];

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  cgi_check_for_malice();
  style_set_current_feature("stat");
  style_header("Repository Table Sizes");
  style_adunit_config(ADUNIT_RIGHT_OK);
  style_submenu_element("Stat", "stat");
  if( g.perm.Admin ){
    style_submenu_element("Schema", "repo_schema");
  }
  db_multi_exec(
    "CREATE TEMP TABLE trans(name TEXT PRIMARY KEY,tabname TEXT)WITHOUT ROWID;"
    "INSERT INTO trans(name,tabname)"
    "   SELECT name, tbl_name FROM repository.sqlite_schema;"
    "CREATE TEMP TABLE piechart(amt REAL, label TEXT);"
    "INSERT INTO piechart(amt,label)"
    "  SELECT sum(pageno),"
    "  coalesce((SELECT tabname FROM trans WHERE trans.name=dbstat.name),name)"
    "    FROM dbstat('repository',TRUE)"
    "   GROUP BY 2 ORDER BY 2;"
  );
  nPageFree = db_int(0, "PRAGMA repository.freelist_count");
  if( nPageFree>0 ){
    db_multi_exec(
      "INSERT INTO piechart(amt,label) VALUES(%d,'freelist')",
      nPageFree
    );
  }
  fsize = file_size(g.zRepositoryName, ExtFILE);
  approxSizeName(sizeof(zBuf), zBuf, fsize);
  @ <h2>Repository Size: %s(zBuf)</h2>
  @ <center><svg width='800' height='500'>
  piechart_render(800,500,PIE_OTHER|PIE_PERCENT);
  @ </svg></center>

  if( g.localOpen ){
    db_multi_exec(
      "DELETE FROM trans;"
      "INSERT INTO trans(name,tabname)"
      "   SELECT name, tbl_name FROM localdb.sqlite_schema;"
      "DELETE FROM piechart;"
      "INSERT INTO piechart(amt,label)"
      "  SELECT sum(pageno), "
      " coalesce((SELECT tabname FROM trans WHERE trans.name=dbstat.name),name)"
      "    FROM dbstat('localdb',TRUE)"
      "   GROUP BY 2 ORDER BY 2;"
    );
    nPageFree = db_int(0, "PRAGMA localdb.freelist_count");
    if( nPageFree>0 ){
      db_multi_exec(
        "INSERT INTO piechart(amt,label) VALUES(%d,'freelist')",
        nPageFree
      );
    }
    fsize = file_size(g.zLocalDbName, ExtFILE);
    approxSizeName(sizeof(zBuf), zBuf, fsize);
    @ <h2>%h(file_tail(g.zLocalDbName)) Size: %s(zBuf)</h2>
    @ <center><svg width='800' height='500'>
    piechart_render(800,500,PIE_OTHER|PIE_PERCENT);
    @ </svg></center>
  }
  style_finish_page();
}

/*
** Gather statistics on artifact types, counts, and sizes.
**
** Only populate the artstat.atype field if the bWithTypes parameter is true.
*/
void gather_artifact_stats(int bWithTypes){
  static const char zSql[] =
    @ CREATE TEMP TABLE artstat(
    @   id INTEGER PRIMARY KEY,   -- Corresponds to BLOB.RID
    @   atype TEXT,               -- 'data', 'manifest', 'tag', 'wiki', etc.
    @   isDelta BOOLEAN,          -- true if stored as a delta
    @   szExp,                    -- expanded, uncompressed size
    @   szCmpr                    -- size as stored on disk
    @ );
    @ INSERT INTO artstat(id,atype,isDelta,szExp,szCmpr)
    @    SELECT blob.rid, NULL,
    @           delta.rid IS NOT NULL,
    @           size, octet_length(content)
    @      FROM blob LEFT JOIN delta ON blob.rid=delta.rid
    @     WHERE content IS NOT NULL;
  ;
  static const char zSql2[] =
    @ UPDATE artstat SET atype='file'
    @  WHERE +id IN (SELECT fid FROM mlink);
    @ UPDATE artstat SET atype='manifest'
    @  WHERE id IN (SELECT objid FROM event WHERE type='ci') AND atype IS NULL;
    @ UPDATE artstat SET atype='forum'
    @  WHERE id IN (SELECT objid FROM event WHERE type='f') AND atype IS NULL;
    @ UPDATE artstat SET atype='cluster'
    @  WHERE atype IS NULL
    @    AND id IN (SELECT rid FROM tagxref
    @                WHERE tagid=(SELECT tagid FROM tag
    @                              WHERE tagname='cluster'));
    @ UPDATE artstat SET atype='ticket'
    @  WHERE atype IS NULL
    @    AND id IN (SELECT rid FROM tagxref
    @                WHERE tagid IN (SELECT tagid FROM tag
    @                              WHERE tagname GLOB 'tkt-*'));
    @ UPDATE artstat SET atype='wiki'
    @  WHERE atype IS NULL
    @    AND id IN (SELECT rid FROM tagxref
    @                WHERE tagid IN (SELECT tagid FROM tag
    @                              WHERE tagname GLOB 'wiki-*'));
    @ UPDATE artstat SET atype='technote'
    @  WHERE atype IS NULL
    @    AND id IN (SELECT rid FROM tagxref
    @                WHERE tagid IN (SELECT tagid FROM tag
    @                              WHERE tagname GLOB 'event-*'));
    @ UPDATE artstat SET atype='attachment'
    @  WHERE atype IS NULL
    @    AND id IN (SELECT attachid FROM attachment UNION
    @               SELECT blob.rid FROM attachment JOIN blob ON uuid=src);
    @ UPDATE artstat SET atype='tag'
    @  WHERE atype IS NULL
    @    AND id IN (SELECT srcid FROM tagxref);
    @ UPDATE artstat SET atype='tag'
    @  WHERE atype IS NULL
    @    AND id IN (SELECT objid FROM event WHERE type='g');
    @ UPDATE artstat SET atype='unused' WHERE atype IS NULL;
  ;
  db_multi_exec("%s", zSql/*safe-for-%s*/);
  if( bWithTypes ){
    db_multi_exec("%s", zSql2/*safe-for-%s*/);
  }
}

/*
** Output text "the largest N artifacts".  Make this text a hyperlink
** to bigbloblist if N is not too big.
*/
static void largest_n_artifacts(int N){
  if( N>250 ){
    @ (the largest %,d(N) artifacts)
  }else{
    @ (the <a href='%R/bigbloblist?n=%d(N)'>largest %d(N) artifacts</a>)
  }
}

/*
** WEBPAGE: artifact_stats
**
** Show information about the sizes of artifacts in this repository
*/
void artifact_stats_page(void){
  Stmt q;
  int nTotal = 0;            /* Total number of artifacts */
  int nDelta = 0;            /* Total number of deltas */
  int nFull = 0;             /* Total number of full-texts */
  double avgCmpr = 0.0;      /* Average size of an artifact after compression */
  double avgExp = 0.0;       /* Average size of an uncompressed artifact */
  int mxCmpr = 0;            /* Maximum compressed artifact size */
  int mxExp = 0;             /* Maximum uncompressed artifact size */
  sqlite3_int64 sumCmpr = 0; /* Total size of all compressed artifacts */
  sqlite3_int64 sumExp = 0;  /* Total size of all expanded artifacts */
  sqlite3_int64 sz1pct = 0;  /* Space used by largest 1% */
  sqlite3_int64 sz10pct = 0; /* Space used by largest 10% */
  sqlite3_int64 sz25pct = 0; /* Space used by largest 25% */
  sqlite3_int64 sz50pct = 0; /* Space used by largest 50% */
  int n50pct = 0;            /* Artifacts using the first 50% of space */
  int n;                     /* Loop counter */
  int medCmpr = 0;           /* Median compressed artifact size */
  int medExp = 0;            /* Median expanded artifact size */
  int med;
  double r;

  login_check_credentials();

  /* These stats are expensive to compute.  To disable them for
  ** user without check-in privileges, to prevent excessive usage by
  ** robots and random passers-by on the internet
  */
  if( !g.perm.Write && !db_get_boolean("artifact_stats_enable",0) ){
    login_needed(g.anon.Write);
    return;
  }
  cgi_check_for_malice();
  fossil_nice_default();

  style_set_current_feature("stat");
  style_header("Artifact Statistics");
  style_submenu_element("Repository Stats", "stat");
  style_submenu_element("Artifact List", "bloblist");
  gather_artifact_stats(1);

  db_prepare(&q,
    "SELECT count(*), sum(isDelta), max(szCmpr),"
    "       max(szExp), sum(szCmpr), sum(szExp)"
    "  FROM artstat"
  );
  db_step(&q);
  nTotal = db_column_int(&q,0);
  nDelta = db_column_int(&q,1);
  nFull = nTotal - nDelta;
  mxCmpr = db_column_int(&q, 2);
  mxExp = db_column_int(&q, 3);
  sumCmpr = db_column_int64(&q, 4);
  sumExp = db_column_int64(&q, 5);
  db_finalize(&q);
  if( nTotal==0 ){
    @ No artifacts in this repository!
    style_finish_page();
    return;
  }
  avgCmpr = (double)sumCmpr/nTotal;
  avgExp = (double)sumExp/nTotal;

  db_prepare(&q, "SELECT szCmpr FROM artstat ORDER BY 1 DESC");
  r = 0;
  n = 0;
  while( db_step(&q)==SQLITE_ROW ){
    r += db_column_int(&q, 0);
    if( n50pct==0 && r>=sumCmpr/2 ) n50pct = n;
    if( n==(nTotal+99)/100 ) sz1pct = (sqlite3_int64)r;
    if( n==(nTotal+9)/10 ) sz10pct = (sqlite3_int64)r;
    if( n==(nTotal+4)/5 ) sz25pct = (sqlite3_int64)r;
    if( n==(nTotal+1)/2 ){
      sz50pct = (sqlite3_int64)r;
      medCmpr = db_column_int(&q,0);
    }
    n++;
  }
  db_finalize(&q);

  @ <h1>Overall Artifact Size Statistics:</h1>
  @ <table class="label-value">
  @ <tr><th>Number of artifacts:</th><td>%,d(nTotal)</td></tr>
  @ <tr><th>Number of deltas:</th>\
  @ <td>%,d(nDelta) (%d(nDelta*100/nTotal)%%)</td></tr>
  @ <tr><th>Number of full-text:</th><td>%,d(nFull) \
  @ (%d(nFull*100/nTotal)%%)</td></tr>
  medExp = db_int(0, "SELECT szExp FROM artstat ORDER BY szExp"
                     " LIMIT 1 OFFSET %d", nTotal/2);
  @ <tr><th>Uncompressed artifact sizes:</th>\
  @ <td>largest: %,d(mxExp), average: %,d((int)avgExp), median: %,d(medExp)</td>
  @ <tr><th>Compressed artifact sizes:</th>\
  @ <td>largest: %,d(mxCmpr), average: %,d((int)avgCmpr), \
  @ median: %,d(medCmpr)</td>

  db_prepare(&q,
    "SELECT avg(szCmpr), max(szCmpr) FROM artstat WHERE isDelta"
  );
  if( db_step(&q)==SQLITE_ROW ){
    int mxDelta = db_column_int(&q,1);
    double avgDelta = db_column_double(&q,0);
    med = db_int(0, "SELECT szCmpr FROM artstat WHERE isDelta ORDER BY szCmpr"
                    " LIMIT 1 OFFSET %d", nDelta/2);
    @ <tr><th>Delta artifact sizes:</th>\
    @ <td>largest: %,d(mxDelta), average: %,d((int)avgDelta), \
    @ median: %,d(med)</td>
  }
  db_finalize(&q);
  r = db_double(0.0, "SELECT avg(szCmpr) FROM artstat WHERE NOT isDelta;");
  med = db_int(0, "SELECT szCmpr FROM artstat WHERE NOT isDelta ORDER BY szCmpr"
                  " LIMIT 1 OFFSET %d", nFull/2);
  @ <tr><th>Full-text artifact sizes:</th>
  @ <td>largest: %,d(mxCmpr), average: %,d((int)r), median: %,d(med)</td>
  @ </table>

  @ <h1>Artifact Size Distribution Facts:</h1>
  @ <ol>
  @ <li><p>The largest %.2f(n50pct*100.0/nTotal)%% of artifacts
  largest_n_artifacts(n50pct);
  @ use 50%% of the total artifact space.
  @ <li><p>The largest 1%% of artifacts
  largest_n_artifacts((nTotal+99)/100);
  @ use %lld(sz1pct*100/sumCmpr)%% of the total artifact space.
  @ <li><p>The largest 10%% of artifacts
  largest_n_artifacts((nTotal+9)/10);
  @ use %lld(sz10pct*100/sumCmpr)%% of the total artifact space.
  @ <li><p>The largest 25%% of artifacts
  largest_n_artifacts((nTotal+4)/5);
  @ use %lld(sz25pct*100/sumCmpr)%% of the total artifact space.
  @ <li><p>The largest 50%% of artifacts
  largest_n_artifacts((nTotal+1)/2);
  @ use %lld(sz50pct*100/sumCmpr)%% of the total artifact space.
  @ </ol>

  @ <h1>Artifact Sizes By Type:</h1>
  db_prepare(&q,
    "SELECT atype, count(*), sum(isDelta), sum(szCmpr), sum(szExp)"
    "  FROM artstat GROUP BY 1"
    " UNION ALL "
    "SELECT 'ALL', count(*), sum(isDelta), sum(szCmpr), sum(szExp)"
    "  FROM artstat"
    " ORDER BY 4;"
  );
  @ <table class='sortable' border='1' \
  @ data-column-types='tkkkkk' data-init-sort='5'>
  @ <thead><tr>
  @ <th>Artifact Type</th>
  @ <th>Count</th>
  @ <th>Full-Text</th>
  @ <th>Delta</th>
  @ <th>Compressed Size</th>
  @ <th>Uncompressed Size</th>
  @ </tr></thead><tbody>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zType = db_column_text(&q, 0);
    int nTotal = db_column_int(&q, 1);
    int nDelta = db_column_int(&q, 2);
    int nFull = nTotal - nDelta;
    sqlite3_int64 szCmpr = db_column_int64(&q, 3);
    sqlite3_int64 szExp = db_column_int64(&q, 4);
    @ <tr><td>%h(zType)</td>
    @ <td data-sortkey='%08x(nTotal)' align='right'>%,d(nTotal)</td>
    @ <td data-sortkey='%08x(nFull)' align='right'>%,d(nFull)</td>
    @ <td data-sortkey='%08x(nDelta)' align='right'>%,d(nDelta)</td>
    @ <td data-sortkey='%016llx(szCmpr)' align='right'>%,lld(szCmpr)</td>
    @ <td data-sortkey='%016llx(szExp)' align='right'>%,lld(szExp)</td>
  }
  @ </tbody></table>
  db_finalize(&q);

  if( db_exists("SELECT 1 FROM artstat WHERE atype='unused'") ){
    @ <h1>Unused Artifacts:</h1>
    db_prepare(&q,
      "SELECT artstat.id, blob.uuid, user.login,"
      "       datetime(rcvfrom.mtime), rcvfrom.rcvid"
      "  FROM artstat JOIN blob ON artstat.id=blob.rid"
      "       LEFT JOIN rcvfrom USING(rcvid)"
      "       LEFT JOIN user USING(uid)"
      " WHERE atype='unused'"
    );
    @ <table class='sortable' border='1' \
    @ data-column-types='ntttt' data-init-sort='0'>
    @ <thead><tr>
    @ <th>RecordID</th>
    @ <th>Hash</th>
    @ <th>User</th>
    @ <th>Date</th>
    @ <th>RcvID</th>
    @ </tr></thead><tbody>
    while( db_step(&q)==SQLITE_ROW ){
      int rid = db_column_int(&q, 0);
      const char *zHash = db_column_text(&q, 1);
      const char *zUser = db_column_text(&q, 2);
      const char *zDate = db_column_text(&q, 3);
      int iRcvid = db_column_int(&q, 4);
      @ <tr><td>%d(rid)</td>
      @ <td>%z(href("%R/info/%!S",zHash))%S(zHash)</a></td>
      @ <td>%h(zUser)</td>
      @ <td>%h(zDate)</td>
      @ <td>%z(href("%R/rcvfrom?rcvid=%d",iRcvid))%d(iRcvid)</a></td></tr>
    }
    @ </tbody></table></div>
    db_finalize(&q);
  }
  style_table_sorter();
  style_finish_page();
}
