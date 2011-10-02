/*
** Copyright (c) 2011 D. Richard Hipp
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
*/

#include "VERSION.h"
#include "config.h"
#include "json_timeline.h"

#if INTERFACE
#include "json_detail.h"
#endif

static cson_value * json_timeline_ci();
static cson_value * json_timeline_ticket();
/*
** Mapping of /json/timeline/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Timeline[] = {
{"c", json_timeline_ci, 0},
{"ci", json_timeline_ci, 0},
{"com", json_timeline_ci, 0},
{"commit", json_timeline_ci, 0},
{"t", json_timeline_ticket, 0},
{"ticket", json_timeline_ticket, 0},
{"w", json_timeline_wiki, 0},
{"wi", json_timeline_wiki, 0},
{"wik", json_timeline_wiki, 0},
{"wiki", json_timeline_wiki, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};


/*
** Implements the /json/timeline family of pages/commands. Far from
** complete.
**
*/
cson_value * json_page_timeline(){
  return json_page_dispatch_helper(&JsonPageDefs_Timeline[0]);
}

/*
** Create a temporary table suitable for storing timeline data.
*/
static void json_timeline_temp_table(void){
  /* Field order MUST match that from json_timeline_query()!!! */
  static const char zSql[] = 
    @ CREATE TEMP TABLE IF NOT EXISTS json_timeline(
    @   sortId INTEGER PRIMARY KEY,
    @   rid INTEGER,
    @   uuid TEXT,
    @   mtime INTEGER,
    @   timestampString TEXT,
    @   comment TEXT,
    @   user TEXT,
    @   isLeaf BOOLEAN,
    @   bgColor TEXT,
    @   eventType TEXT,
    @   tags TEXT,
    @   tagId INTEGER,
    @   brief TEXT
    @ )
  ;
  db_multi_exec(zSql);
}

/*
** Return a pointer to a constant string that forms the basis
** for a timeline query for the JSON interface.
*/
const char const * json_timeline_query(void){
  /* Field order MUST match that from json_timeline_temp_table()!!! */
  static const char zBaseSql[] =
    @ SELECT
    @   NULL,
    @   blob.rid,
    @   uuid,
    @   strftime('%%s',event.mtime),
    @   datetime(event.mtime,'utc'),
    @   coalesce(ecomment, comment),
    @   coalesce(euser, user),
    @   blob.rid IN leaf,
    @   bgcolor,
    @   event.type,
    @   (SELECT group_concat(substr(tagname,5), ',') FROM tag, tagxref
    @     WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @       AND tagxref.rid=blob.rid AND tagxref.tagtype>0),
    @   tagid,
    @   brief
    @  FROM event JOIN blob 
    @ WHERE blob.rid=event.objid
  ;
  return zBaseSql;
}

/*
** Helper for the timeline family of functions.  Possibly appends 1
** AND clause and an ORDER BY clause to pSql, depending on the state
** of the "after" ("a") or "before" ("b") environment parameters.
** This function gives "after" precedence over "before", and only
** applies one of them.
**
** Returns -1 if it adds a "before" clause, 1 if it adds
** an "after" clause, and 0 if adds only an order-by clause.
*/
static char json_timeline_add_time_clause(Blob *pSql){
  char const * zAfter = NULL;
  char const * zBefore = NULL;
  if( g.isHTTP ){
    /**
       FIXME: we are only honoring STRING values here, not int (for
       passing Unix Epoch times).
    */
    zAfter = json_getenv_cstr("after");
    if(!zAfter || !*zAfter){
      zAfter = json_getenv_cstr("a");
    }
    if(!zAfter){
      zBefore = json_getenv_cstr("before");
      if(!zBefore||!*zBefore){
        zBefore = json_getenv_cstr("b");
      }
    }
  }else{
    zAfter = find_option("after","a",1);
    zBefore = zAfter ? NULL : find_option("before","b",1);
  }
  if(zAfter&&*zAfter){
    while( fossil_isspace(*zAfter) ) ++zAfter;
    blob_appendf(pSql,
                 " AND event.mtime>=(SELECT julianday(%Q,'utc')) "
                 " ORDER BY event.mtime ASC ",
                 zAfter);
    return 1;
  }else if(zBefore && *zBefore){
    while( fossil_isspace(*zBefore) ) ++zBefore;
    blob_appendf(pSql,
                 " AND event.mtime<=(SELECT julianday(%Q,'utc')) "
                 " ORDER BY event.mtime DESC ",
                 zBefore);
    return -1;
  }else{
    blob_append(pSql," ORDER BY event.mtime DESC ", -1);
    return 0;
  }
}

/*
** Tries to figure out a timeline query length limit base on
** environment parameters. If it can it returns that value,
** else it returns some statically defined default value.
**
** Never returns a negative value. 0 means no limit.
*/
static int json_timeline_limit(){
  static const int defaultLimit = 20;
  int limit = -1;
  if( g.json.post.v ){
    limit = json_getenv_int("limit",-1);
    if(limit<0){
      limit = json_getenv_int("n",-1);
    }
  }else if(!g.isHTTP){/* CLI mode */
    char const * arg = find_option("limit","n",1);
    if(arg && *arg){
      limit = atoi(arg);
    }
  }
  return (limit<0) ? defaultLimit : limit;
}

/*
** Internal helper for the json_timeline_EVENTTYPE() family of
** functions. zEventType must be one of (ci, w, t). pSql must be a
** cleanly-initialized, empty Blob to store the sql in. If pPayload is
** not NULL it is assumed to be the pending response payload. If
** json_timeline_limit() returns non-0, this function adds a LIMIT
** clause to the generated SQL and (if pPayload is not NULL) adds the
** limit value as the "limit" property of pPayload.
*/
static void json_timeline_setup_sql( char const * zEventType,
                                     Blob * pSql,
                                     cson_object * pPayload ){
  int limit;
  assert( zEventType && *zEventType && pSql );
  json_timeline_temp_table();
  blob_append(pSql, "INSERT OR IGNORE INTO json_timeline ", -1);
  blob_append(pSql, json_timeline_query(), -1 );
  blob_appendf(pSql, " AND event.type IN(%Q) ", zEventType);
  json_timeline_add_time_clause(pSql);
  limit = json_timeline_limit();
  if(limit){
    blob_appendf(pSql,"LIMIT %d ",limit);
  }
  if(pPayload){
    cson_object_set(pPayload, "limit",json_new_int(limit));
  }

}

/*
** If any files are associated with the given rid, a JSON array
** containing information about them is returned (and is owned by the
** caller). If no files are associated with it then NULL is returned.
*/
cson_value * json_get_changed_files(int rid){
  cson_value * rowsV = NULL;
  cson_array * rows = NULL;
  Stmt q;
  db_prepare(&q, 
#if 0
             "SELECT (mlink.pid==0) AS isNew,"
             "       (mlink.fid==0) AS isDel,"
             "       filename.name AS name"
             " FROM mlink, filename"
             " WHERE mid=%d"
             " AND pid!=fid"
             " AND filename.fnid=mlink.fnid"
             " ORDER BY 3 /*sort*/",
#else
           "SELECT (pid==0) AS isnew,"
           "       (fid==0) AS isdel,"
           "       (SELECT name FROM filename WHERE fnid=mlink.fnid) AS name,"
           "       (SELECT uuid FROM blob WHERE rid=fid) as uuid,"
           "       (SELECT uuid FROM blob WHERE rid=pid) as prevUuid"
           "  FROM mlink"
           " WHERE mid=%d AND pid!=fid"
           " ORDER BY 3 /*sort*/",
#endif
             rid
             );
  while( (SQLITE_ROW == db_step(&q)) ){
    cson_value * rowV = cson_value_new_object();
    cson_object * row = cson_value_get_object(rowV);
    int const isNew = db_column_int(&q,0);
    int const isDel = db_column_int(&q,1);
    if(!rowsV){
      rowsV = cson_value_new_array();
      rows = cson_value_get_array(rowsV);
    }
    cson_object_set(row, "name", json_new_string(db_column_text(&q,2)));
    cson_object_set(row, "uuid", json_new_string(db_column_text(&q,3)));
    if(!isNew){
      cson_object_set(row, "prevUuid", json_new_string(db_column_text(&q,4)));
    }
    cson_object_set(row, "state",
                    json_new_string(isNew
                                    ? "added"
                                    : (isDel
                                       ? "removed"
                                       : "modified")));
    cson_array_append( rows, rowV );
  }
  db_finalize(&q);
  return rowsV;
}
/*
** Implementation of /json/timeline/ci.
**
** Still a few TODOs (like figuring out how to structure
** inheritance info).
*/
static cson_value * json_timeline_ci(){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  cson_value * tmp = NULL;
  cson_value * listV = NULL;
  cson_array * list = NULL;
  int check = 0;
  int showFiles = 0;
  Stmt q;
  char warnRowToJsonFailed = 0;
  char warnStringToArrayFailed = 0;
  Blob sql = empty_blob;
  if( !g.perm.Read/* && !g.perm.RdTkt && !g.perm.RdWiki*/ ){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }
  if( g.isHTTP ){
    showFiles = json_getenv_bool("showFiles",0);
  }else{
    showFiles = 0!=find_option("show-files", "f",0);
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  json_timeline_setup_sql( "ci", &sql, pay );
#define SET(K) if(0!=(check=cson_object_set(pay,K,tmp))){ \
    g.json.resultCode = (cson_rc.AllocError==check) \
      ? FSL_JSON_E_ALLOC : FSL_JSON_E_UNKNOWN; \
    goto error;\
  }
#if 0
  /* only for testing! */
  tmp = cson_value_new_string(blob_buffer(&sql),strlen(blob_buffer(&sql)));
  SET("timelineSql");
#endif
  db_multi_exec(blob_buffer(&sql));
  blob_reset(&sql);
  db_prepare(&q, "SELECT "
             " rid AS rid,"
             " uuid AS uuid,"
             " mtime AS timestamp,"
#if 0
             " timestampString AS timestampString,"
#endif
             " comment AS comment, "
             " user AS user,"
             " isLeaf AS isLeaf," /*FIXME: convert to JSON bool */
             " bgColor AS bgColor," /* why always null? */
             " eventType AS eventType"
#if 0
             " tags AS tags"
             /*tagId is always null?*/
             " tagId AS tagId"
#endif
             " FROM json_timeline"
             " ORDER BY sortId");
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  tmp = listV;
  SET("timeline");
  while( (SQLITE_ROW == db_step(&q) )){
    /* convert each row into a JSON object...*/
    int const rid = db_column_int(&q,0);
    cson_value * rowV = json_artifact_for_ci(rid, showFiles);
    cson_object * row = cson_value_get_object(rowV);
    if(!row){
      if( !warnRowToJsonFailed ){
        warnRowToJsonFailed = 1;
        json_warn( FSL_JSON_W_ROW_TO_JSON_FAILED,
                   "Could not convert at least one timeline result row to JSON." );
      }
      continue;
    }
    cson_array_append(list, rowV);
  }
#undef SET
  goto ok;
  error:
  assert( 0 != g.json.resultCode );
  cson_value_free(payV);
  payV = NULL;
  ok:
  db_finalize(&q);
  return payV;
}

/*
** Implementation of /json/timeline/wiki.
**
*/
cson_value * json_timeline_wiki(){
  /* This code is 95% the same as json_timeline_ci(), by the way. */
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  cson_value * tmp = NULL;
  cson_value * listV = NULL;
  cson_array * list = NULL;
  int check = 0;
  Stmt q;
  Blob sql = empty_blob;
  if( !g.perm.Read || !g.perm.RdWiki ){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  json_timeline_setup_sql( "w", &sql, pay );
#define SET(K) if(0!=(check=cson_object_set(pay,K,tmp))){ \
    g.json.resultCode = (cson_rc.AllocError==check) \
      ? FSL_JSON_E_ALLOC : FSL_JSON_E_UNKNOWN; \
    goto error;\
  }
#if 0
  /* only for testing! */
  tmp = cson_value_new_string(blob_buffer(&sql),strlen(blob_buffer(&sql)));
  SET("timelineSql");
#endif
  db_multi_exec(blob_buffer(&sql));
  blob_reset(&sql);
  db_prepare(&q, "SELECT rid AS rid,"
             " uuid AS uuid,"
             " mtime AS timestamp,"
#if 0
             " timestampString AS timestampString,"
#endif
             " comment AS comment, "
             " user AS user,"
             " eventType AS eventType"
#if 0
             /* can wiki pages have tags? */
             " tags AS tags," /*FIXME: split this into
                                a JSON array*/
             " tagId AS tagId,"
#endif
             " FROM json_timeline"
             " ORDER BY sortId",
             -1);
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  tmp = listV;
  SET("timeline");
  json_stmt_to_array_of_obj(&q, listV);
#undef SET
  goto ok;
  error:
  assert( 0 != g.json.resultCode );
  cson_value_free(payV);
  payV = NULL;
  ok:
  db_finalize(&q);
  return payV;
}

/*
** Implementation of /json/timeline/ticket.
**
*/
static cson_value * json_timeline_ticket(){
  /* This code is 95% the same as json_timeline_ci(), by the way. */
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  cson_value * tmp = NULL;
  cson_value * listV = NULL;
  cson_array * list = NULL;
  int check = 0;
  Stmt q;
  Blob sql = empty_blob;
  if( !g.perm.Read || !g.perm.RdTkt ){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  json_timeline_setup_sql( "t", &sql, pay );
  db_multi_exec(blob_buffer(&sql));
#define SET(K) if(0!=(check=cson_object_set(pay,K,tmp))){ \
    g.json.resultCode = (cson_rc.AllocError==check) \
      ? FSL_JSON_E_ALLOC : FSL_JSON_E_UNKNOWN; \
    goto error;\
  }

#if 0
  /* only for testing! */
  tmp = cson_value_new_string(blob_buffer(&sql),strlen(blob_buffer(&sql)));
  SET("timelineSql");
#endif

  blob_reset(&sql);
  db_prepare(&q, "SELECT rid AS rid,"
             " uuid AS uuid,"
             " mtime AS timestamp,"
#if 0
             " timestampString AS timestampString,"
#endif
             " user AS user,"
             " eventType AS eventType,"
             " comment AS comment,"
             " brief AS briefComment"
             " FROM json_timeline"
             " ORDER BY sortId",
             -1);
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  tmp = listV;
  SET("timeline");
  while( (SQLITE_ROW == db_step(&q) )){
    /* convert each row into a JSON object...*/
    int rc;
    int const rid = db_column_int(&q,0);
    Manifest * pMan = NULL;
    cson_value * rowV = cson_sqlite3_row_to_object(q.pStmt);
    cson_object * row = cson_value_get_object(rowV);
    if(!row){
      json_warn( FSL_JSON_W_ROW_TO_JSON_FAILED,
                 "Could not convert at least one timeline result row to JSON." );
      continue;
    }
    pMan = manifest_get(rid, CFTYPE_TICKET);
    assert( pMan && "Manifest is NULL!?!" );
    if( pMan ){
      /* FIXME: certainly there's a more efficient way for use to get
         the ticket UUIDs?
      */
      cson_object_set(row,"ticketUuid",json_new_string(pMan->zTicketUuid));
      manifest_destroy(pMan);
    }
    rc = cson_array_append( list, rowV );
    if( 0 != rc ){
      cson_value_free(rowV);
      g.json.resultCode = (cson_rc.AllocError==rc)
        ? FSL_JSON_E_ALLOC
        : FSL_JSON_E_UNKNOWN;
      goto error;
    }
  }
#undef SET
  goto ok;
  error:
  assert( 0 != g.json.resultCode );
  cson_value_free(payV);
  payV = NULL;
  ok:
  db_finalize(&q);
  return payV;
}

