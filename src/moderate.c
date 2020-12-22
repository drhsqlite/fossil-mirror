/*
** Copyright (c) 2012 D. Richard Hipp
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
** If the rid object is being held for moderation, write out
** an "awaiting moderation" message and return true.
**
** If the object is not being held for moderation, simply return
** false without generating any output.
*/
int moderation_pending_www(int rid){
  int pending = moderation_pending(rid);
  if( pending ){
    @ <span class="modpending">(Awaiting Moderator Approval)</span>
  }
  return pending;
}


/*
** Return TRUE if there any pending moderation requests.
*/
int moderation_needed(void){
  if( !moderation_table_exists() ) return 0;
  return db_exists("SELECT 1 FROM modreq");
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
    if( db_table_exists("repository","forumpost") ){
      db_multi_exec("DELETE FROM forumpost WHERE fpid=%d", rid);
    }
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
void moderation_approve(char class, int rid){
  if( !moderation_pending(rid) ) return;
  db_begin_transaction();
  db_multi_exec(
    "DELETE FROM private WHERE rid=%d;"
    "INSERT OR IGNORE INTO unclustered VALUES(%d);"
    "INSERT OR IGNORE INTO unsent VALUES(%d);",
    rid, rid, rid
  );
  db_multi_exec("DELETE FROM modreq WHERE objid=%d", rid);
  admin_log("Approved moderation of rid %c-%d.", class, rid);
  if( class!='a' ) search_doc_touch(class, rid, 0);
  setup_incr_cfgcnt();
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
  if( !g.perm.ModWiki && !g.perm.ModTkt && !g.perm.ModForum ){
    login_needed(g.anon.ModWiki && g.anon.ModTkt && g.anon.ModForum);
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
    www_print_timeline(&q, 0, 0, 0, 0, 0, 0, 0);
    db_finalize(&q);
  }
  style_finish_page();
}

/*
** Disapproves any entries in the modreq table which belong to any
** user whose name is no longer found in the user table. This is only
** intended to be called after user deletion via /setup_uedit.
**
** To figure out whether a name exists it cross-references
** coalesce(event.euser, event.user) with user.login, limiting the
** selection to event entries where objid matches an entry in the
** modreq table.
**
** This is a no-op if called without g.perm.Admin permissions or if
** moderation_table_exists() returns false.
*/
void moderation_disapprove_for_missing_users(){
  Stmt q;
  if( !g.perm.Admin || !moderation_table_exists() ){
    return;
  }
  db_begin_transaction();
  db_prepare(&q,
    "SELECT objid FROM event WHERE objid IN "
    "(SELECT objid FROM modreq) "
    "AND coalesce(euser,user) NOT IN "
    "(SELECT login FROM user)"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int const objid = db_column_int(&q, 0);
    moderation_disapprove(objid);
  }
  db_finalize(&q);
  setup_incr_cfgcnt();
  db_end_transaction(0);
}
