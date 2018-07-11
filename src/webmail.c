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
** Implementation of web pages for managing the email storage tables
** (if they exist):
**
**     emailbox
**     emailblob
**     emailroute
*/
#include "config.h"
#include "webmail.h"
#include <assert.h>

/*
** WEBPAGE:  webmail
**
** This page can be used to read content from the EMAILBOX table
** that contains email received by the "fossil smtpd" command.
*/
void webmail_page(void){
  int emailid;
  Stmt q;
  Blob sql;
  int showAll = 0;
  login_check_credentials();
  if( g.zLogin==0 ){
    login_needed(0);
    return;
  }
  if( !db_table_exists("repository","emailbox") ){
    style_header("Webmail Not Available");
    @ <p>This repository is not configured to provide webmail</p>
    style_footer();
    return;
  }
  add_content_sql_commands(g.db);
  emailid = atoi(PD("id","0"));
  if( emailid>0 ){
    blob_init(&sql, 0, 0);
    blob_append_sql(&sql, "SELECT decompress(etxt)"
                          " FROM emailblob WHERE emailid=%d",
                          emailid);
    if( !g.perm.Admin ){
      blob_append_sql(&sql, " AND EXISTS(SELECT 1 FROM emailbox WHERE"
                            " euser=%Q AND emsgid=emailid)", g.zLogin);
    }
    db_prepare_blob(&q, &sql);
    blob_reset(&sql);
    if( db_step(&q)==SQLITE_ROW ){
      style_header("Message %d",emailid);
      @ <pre>%h(db_column_text(&q, 0))</pre>
      style_footer();
      db_finalize(&q);
      return;
    }
    db_finalize(&q);
  }
  style_header("Webmail");
  blob_init(&sql, 0, 0);
  blob_append_sql(&sql,
        /*    0       1                           2        3        4      5 */
    "SELECT efrom, datetime(edate,'unixepoch'), estate, esubject, emsgid, euser"
    " FROM emailbox"
  );
  if( g.perm.Admin ){
    const char *zUser = P("user");
    if( P("all")!=0 ){
      /* Show all email messages */
      showAll = 1;
    }else{
      style_submenu_element("All", "%R/webmail?all");
      if( zUser ){
        blob_append_sql(&sql, " WHERE euser=%Q", zUser);
      }else{
        blob_append_sql(&sql, " WHERE euser=%Q", g.zLogin);
      }
    }
  }else{
    blob_append_sql(&sql, " WHERE euser=%Q", g.zLogin);
  }
  blob_append_sql(&sql, " ORDER BY edate DESC limit 50");
  db_prepare_blob(&q, &sql);
  blob_reset(&sql);
  @ <ol>
  while( db_step(&q)==SQLITE_ROW ){
    int emailid = db_column_int(&q,4);
    const char *zFrom = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zSubject = db_column_text(&q, 3);
    @ <li>
    if( showAll ){
      const char *zTo = db_column_text(&q,5);
      @ <a href="%R/webmail?user=%t(zTo)">%h(zTo)</a>:
    }
    @ <a href="%R/webmail?id=%d(emailid)">%h(zFrom) &rarr; %h(zSubject)</a>
    @ %h(zDate)
  }
  db_finalize(&q);
  @ </ol>
  style_footer(); 
}
