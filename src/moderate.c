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
     "CREATE TABLE IF NOT EXISTS repository.modreq(\n"
     "  objid INTEGER PRIMARY KEY,\n"        /* Record pending approval */
     "  attachRid INT,\n"                    /* Object attached */
     "  tktid TEXT\n"                        /* Associated ticket id */
     ");\n"
  );
}

/*
** Return TRUE if the modreq table exists
*/
int moderation_table_exists(void){
  return db_table_exists("repository", "modreq");
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
** Check to see if the object identified by RID is used for anything.
*/
static int object_used(int rid){
  static const char *const aTabField[] = {
     "modreq",     "attachRid",
     "mlink",      "mid",
     "mlink",      "fid",
     "tagxref",    "srcid",
     "tagxref",    "rid",
  };
  int i;
  for(i=0; i<count(aTabField); i+=2){
    if( db_exists("SELECT 1 FROM \"%w\" WHERE \"%w\"=%d",
                  aTabField[i], aTabField[i+1], rid) ) return 1;
  }
  return 0;
}

/*
** Delete a moderation item given by objid
*/
void moderation_disapprove(int objid){
  Stmt q;
  char *zTktid;
  int attachRid = 0;
  int rid;
  if( !moderation_pending(objid) ) return;
  db_begin_transaction();
  rid = objid;
  while( rid && content_is_private(rid) ){
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
      "DELETE FROM private WHERE rid=%d;"
      "DELETE FROM attachment WHERE attachid=%d;",
      rid, rid, rid, rid, rid, rid
    );
    zTktid = db_text(0, "SELECT tktid FROM modreq WHERE objid=%d", rid);
    if( zTktid && zTktid[0] ){
      ticket_rebuild_entry(zTktid);
      fossil_free(zTktid);
    }
    attachRid = db_int(0, "SELECT attachRid FROM modreq WHERE objid=%d", rid);
    if( rid==objid ){
      db_multi_exec("DELETE FROM modreq WHERE objid=%d", rid);
    }
    if( attachRid && object_used(attachRid) ) attachRid = 0;
    admin_log("Disapproved moderation of rid %d.", rid);
    rid = attachRid;
  }
  db_end_transaction(0);
}

/*
** Approve an object held for moderation.
*/
void moderation_approve(int rid){
  if( !moderation_pending(rid) ) return;
  db_begin_transaction();
  db_multi_exec(
    "DELETE FROM private WHERE rid=%d;"
    "INSERT OR IGNORE INTO unclustered VALUES(%d);"
    "INSERT OR IGNORE INTO unsent VALUES(%d);",
    rid, rid, rid
  );
  db_multi_exec("DELETE FROM modreq WHERE objid=%d", rid);
  admin_log("Approved moderation of rid %d.", rid);
  db_end_transaction(0);
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
  if( !g.perm.RdWiki && !g.perm.RdTkt ){
    login_needed(g.anon.RdWiki && g.anon.RdTkt);
    return;
  }
  style_header("Pending Moderation Requests");
  @ <h2>All Pending Moderation Requests</h2>
  if( moderation_table_exists() ){
    blob_init(&sql, timeline_query_for_www(), -1);
    blob_append_sql(&sql,
        " AND event.objid IN (SELECT objid FROM modreq)"
        " ORDER BY event.mtime DESC"
    );
    db_prepare(&q, "%s", blob_sql_text(&sql));
    www_print_timeline(&q, 0, 0, 0, 0, 0);
    db_finalize(&q);
  }
  style_footer();
}
