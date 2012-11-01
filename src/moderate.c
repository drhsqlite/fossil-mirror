/*
** Copyright (c) 2012 D. Richard Hipp
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
** This file contains code used to deal with moderator actions for
** Wiki and Tickets.
*/
#include "config.h"
#include "moderate.h"
#include <assert.h>

/*
** Create a table to represent pending moderation requests, if the
** table does not already exist.
*/
void moderation_table_create(void){
  db_multi_exec(
     "CREATE TABLE IF NOT EXISTS modreq(\n"
     "  objid INTEGER PRIMARY KEY,\n"        /* Record pending approval */
     "  parent INTEGER REFERENCES modreq,\n" /* Parent record */
     "  tktid TEXT\n"                        /* Associated ticket id */
     ");\n"
  );
}

/*
** Return TRUE if the modreq table exists
*/
int moderation_table_exists(void){
  static int modreqExists = -1;
  if( modreqExists<0 ){
    modreqExists = db_exists("SELECT 1 FROM %s.sqlite_master"
                             " WHERE name='modreq'", db_name("repository"));
  }
  return modreqExists;
}

/*
** Return TRUE if the object specified is being held for moderation.
*/
int moderation_pending(int rid){
  static Stmt q;
  int rc;
  if( rid==0 || !moderation_table_exists() ) return 0;
  db_static_prepare(&q, "SELECT 1 FROM modreq WHERE objid=:objid");
  db_bind_int(&q, ":objid", rid);
  rc = db_step(&q)==SQLITE_ROW;
  db_reset(&q);
  return rc;
}

/*
** Delete a moderation item given by rid
*/
void moderation_disapprove(int rid){
  Stmt q;
  char *zTktid;
  if( !moderation_pending(rid) ) return;
  if( content_is_private(rid) ){
    db_prepare(&q, "SELECT rid FROM delta WHERE srcid=%d", rid);
    while( db_step(&q)==SQLITE_ROW ){
      int ridUser = db_column_int(&q, 0);
      content_undelta(ridUser);
    }
    db_finalize(&q);
    db_multi_exec(
      "DELETE FROM blob WHERE rid=%d;"
      "DELETE FROM delta WHERE rid=%d;"
      "DELETE FROM event WHERE objid=%d;"
      "DELETE FROM tagxref WHERE rid=%d;"
      "DELETE FROM private WHERE rid=%d;",
      rid, rid, rid, rid, rid
    );
    zTktid = db_text(0, "SELECT tktid FROm modreq WHERE objid=%d", rid);
    if( zTktid ){
      ticket_rebuild_entry(zTktid);
      fossil_free(zTktid);
    }
  }
  db_prepare(&q, "SELECT objid FROM modreq WHERE parent=%d AND "
                 "UNION SELECT parent FROM modreq WHERE objid=%d",
                 rid, rid);
  while( db_step(&q)==SQLITE_ROW ){
    int other = db_column_int(&q, 0);
    if( other==rid ) continue;
    moderation_approve(other);
  }
  db_finalize(&q);
  db_multi_exec("DELETE FROM modreq WHERE objid=%d", rid);
}

/*
** Approve an object held for moderation.
*/
void moderation_approve(int rid){
  Stmt q;
  if( !moderation_pending(rid) ) return;
  db_multi_exec("DELETE FROM private WHERE rid=%d;", rid);
  db_prepare(&q, "SELECT objid FROM modreq WHERE parent=%d AND "
                 "UNION SELECT parent FROM modreq WHERE objid=%d",
                 rid, rid);
  while( db_step(&q)==SQLITE_ROW ){
    int other = db_column_int(&q, 0);
    if( other==rid ) continue;
    moderation_approve(other);
  }
  db_finalize(&q);
  db_multi_exec("DELETE FROM modreq WHERE objid=%d", rid);
}

/*
** WEBPAGE: modreq
**
** Show all pending moderation request
*/
void modreq_page(void){
  Blob sql;
  Stmt q;

  login_check_credentials();
  if( !g.perm.RdWiki && !g.perm.RdTkt ){ login_needed(); return; }
  style_header("Pending Moderation Requests");
  blob_init(&sql, timeline_query_for_www(), -1);
  blob_appendf(&sql,
      " AND event.objid IN (SELECT objid FROM modreq)"
      " ORDER BY event.mtime DESC"
  );
  db_prepare(&q, blob_str(&sql));
  www_print_timeline(&q, 0, 0, 0, 0);
  db_finalize(&q);
  style_footer();
}
