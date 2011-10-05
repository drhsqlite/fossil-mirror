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
/* the short forms are only enabled in CLI mode, to avoid
   that we end up with HTTP clients using 3 different names
   for the same requests.
*/
{"c", json_timeline_ci, -1},
{"checkin", json_timeline_ci, 0},
{"ci", json_timeline_ci, -1},
{"com", json_timeline_ci, -1},
{"commit", json_timeline_ci, 0},
{"t", json_timeline_ticket, -1},
{"tkt", json_timeline_ticket, -1},
{"ticket", json_timeline_ticket, 0},
{"w", json_timeline_wiki, -1},
{"wi", json_timeline_wiki, -1},
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
#if 0
  /* The original timeline code does not require 'h' access,
     but it arguably should. For JSON mode i think one could argue
     that History permissions are required.
  */
  if(! g.perm.History && !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED, "Timeline requires 'h' or 'o' access.");
    return NULL;
  }
#endif
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
** Internal helper to append query information if the
** "tag" or "branch" request properties (CLI: --tag/--branch)
** are set. Limits the query to a particular branch/tag.
**
** tag works like HTML mode's "t" option and branch works like HTML
** mode's "r" option. They are very similar, but subtly different -
** tag mode shows only entries with a given tag but branch mode can
** also reveal some with "related" tags (meaning they were merged into
** the requested branch).
**
** pSql is the target blob to append the query [subset]
** to.
**
** Returns a positive value if it modifies pSql, 0 if it
** does not. It returns a negative value if the tag
** provided to the request was not found (pSql is not modified
** in that case.
**
** If payload is not NULL then on success its "tag" or "branch"
** property is set to the tag/branch name found in the request.
**
** Only one of "tag" or "branch" modes will work at a time, and if
** both are specified, which one takes precedence is unspecified.
*/
static char json_timeline_add_tag_branch_clause(Blob *pSql,
                                                cson_object * pPayload){
  char const * zTag = NULL;
  char const * zBranch = NULL;
  int tagid = 0;
  if(! g.perm.Read ){
    return 0;
  }
  zTag = json_find_option_cstr("tag",NULL,NULL);
  if(!zTag || !*zTag){
    zBranch = json_find_option_cstr("branch",NULL,NULL);
    if(!zBranch || !*zBranch){
      return 0;
    }
    zTag = zBranch;
  }
  tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname='sym-%q'",
                 zTag);
  if(tagid<=0){
    return -1;
  }
  if(pPayload){
    cson_object_set( pPayload, zBranch ? "branch" : "tag", json_new_string(zTag) );
  }
  blob_appendf(pSql,
               " AND ("
               " EXISTS(SELECT 1 FROM tagxref"
               "        WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)",
               tagid);
  if(zBranch){
    /* from "r" flag code in page_timeline().*/
    blob_appendf(pSql,
                 " OR EXISTS(SELECT 1 FROM plink JOIN tagxref ON rid=cid"
                 "    WHERE tagid=%d AND tagtype>0 AND pid=blob.rid)",
                 tagid);
#if 0 /* from the undocumented "mionly" flag in page_timeline() */
    blob_appendf(pSql,
                 " OR EXISTS(SELECT 1 FROM plink JOIN tagxref ON rid=pid"
                 "    WHERE tagid=%d AND tagtype>0 AND cid=blob.rid)",
                 tagid);
#endif
  }
  blob_append(pSql," ) ",3);
  return 1;
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
  int rc = 0;
  zAfter = json_find_option_cstr("after",NULL,"a");
  zBefore = zAfter ? NULL : json_find_option_cstr("before",NULL,"b");

  if(zAfter&&*zAfter){
    while( fossil_isspace(*zAfter) ) ++zAfter;
    blob_appendf(pSql,
                 " AND event.mtime>=(SELECT julianday(%Q,'utc')) "
                 " ORDER BY event.mtime ASC ",
                 zAfter);
    rc = 1;
  }else if(zBefore && *zBefore){
    while( fossil_isspace(*zBefore) ) ++zBefore;
    blob_appendf(pSql,
                 " AND event.mtime<=(SELECT julianday(%Q,'utc')) "
                 " ORDER BY event.mtime DESC ",
                 zBefore);
    rc = -1;
  }
  return rc;
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
  if(!g.isHTTP){/* CLI mode */
    char const * arg = find_option("limit","n",1);
    if(arg && *arg){
      limit = atoi(arg);
    }
  }
  if( (limit<0) && fossil_is_json() ){
    limit = json_getenv_int("limit",-1);
  }
  return (limit<0) ? defaultLimit : limit;
}

/*
** Internal helper for the json_timeline_EVENTTYPE() family of
** functions. zEventType must be one of (ci, w, t). pSql must be a
** cleanly-initialized, empty Blob to store the sql in. If pPayload is
** not NULL it is assumed to be the pending response payload. If
** json_timeline_limit() returns non-0, this function adds a LIMIT
** clause to the generated SQL.
**
** If pPayload is not NULL then this might add properties to pPayload,
** reflecting options set in the request environment.
**
** Returns 0 on success. On error processing should not continue and
** the returned value should be used as g.json.resultCode.
*/
static int json_timeline_setup_sql( char const * zEventType,
                                    Blob * pSql,
                                    cson_object * pPayload ){
  int limit;
  assert( zEventType && *zEventType && pSql );
  json_timeline_temp_table();
  blob_append(pSql, "INSERT OR IGNORE INTO json_timeline ", -1);
  blob_append(pSql, json_timeline_query(), -1 );
  blob_appendf(pSql, " AND event.type IN(%Q) ", zEventType);
  if( json_timeline_add_tag_branch_clause(pSql, pPayload) < 0 ){
    return FSL_JSON_E_INVALID_ARGS;
  }
  json_timeline_add_time_clause(pSql);
  limit = json_timeline_limit();
  if(limit>=0){
    blob_appendf(pSql,"LIMIT %d ",limit);
  }
  if(pPayload){
    cson_object_set(pPayload, "limit", json_new_int(limit));
  }
  return 0;
}

/*
** If any files are associated with the given rid, a JSON array
** containing information about them is returned (and is owned by the
** caller). If no files are associated with it then NULL is returned.
*/
cson_value * json_get_changed_files(int rid){
  cson_value * rowsV = NULL;
  cson_array * rows = NULL;
  Stmt q = empty_Stmt;
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
           " ORDER BY name /*sort*/",
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
  char showFiles = -1/*magic number*/;
  Stmt q = empty_Stmt;
  char warnRowToJsonFailed = 0;
  char warnStringToArrayFailed = 0;
  Blob sql = empty_blob;
  if( !g.perm.Read ){
    /* IMO this falls more under the category of g.perm.History, but
       i'm following the original timeline impl here.
    */
    json_set_err( FSL_JSON_E_DENIED, "Checkin timeline requires 'o' access." );
    return NULL;
  }
  showFiles = json_find_option_bool("files",NULL,"f",0);
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  check = json_timeline_setup_sql( "ci", &sql, pay );
  if(check){
    json_set_err(check, "Query initialization failed.");
    goto error;
  }
#define SET(K) if(0!=(check=cson_object_set(pay,K,tmp))){ \
    json_set_err((cson_rc.AllocError==check)        \
                 ? FSL_JSON_E_ALLOC : FSL_JSON_E_UNKNOWN,\
                 "Object property insertion failed");     \
    goto error;\
  } (void)0

#if 0
  /* only for testing! */
  tmp = cson_value_new_string(blob_buffer(&sql),strlen(blob_buffer(&sql)));
  SET("timelineSql");
#endif
  db_multi_exec(blob_buffer(&sql));
  blob_reset(&sql);
  db_prepare(&q, "SELECT "
             " rid AS rid"
#if 0
             " uuid AS uuid,"
             " mtime AS timestamp,"
#  if 0
             " timestampString AS timestampString,"
#  endif
             " comment AS comment, "
             " user AS user,"
             " isLeaf AS isLeaf," /*FIXME: convert to JSON bool */
             " bgColor AS bgColor," /* why always null? */
             " eventType AS eventType"
#  if 0
             " tags AS tags"
             /*tagId is always null?*/
             " tagId AS tagId"
#  endif
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
  Stmt q = empty_Stmt;
  Blob sql = empty_blob;
  if( !g.perm.RdWiki && !g.perm.Read ){
    json_set_err( FSL_JSON_E_DENIED, "Wiki timeline requires 'o' or 'j' access.");
    return NULL;
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  check = json_timeline_setup_sql( "w", &sql, pay );
  if(check){
    json_set_err(check, "Query initialization failed.");
    goto error;
  }

#define SET(K) if(0!=(check=cson_object_set(pay,K,tmp))){ \
    json_set_err((cson_rc.AllocError==check)        \
                 ? FSL_JSON_E_ALLOC : FSL_JSON_E_UNKNOWN,       \
                 "Object property insertion failed."); \
    goto error;\
  } (void)0
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
  blob_reset(&sql);
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
  Stmt q = empty_Stmt;
  Blob sql = empty_blob;
  if( !g.perm.RdTkt && !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED, "Ticket timeline requires 'o' or 'r' access.");
    return NULL;
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  check = json_timeline_setup_sql( "t", &sql, pay );
  if(check){
    json_set_err(check, "Query initialization failed.");
    goto error;
  }

  db_multi_exec(blob_buffer(&sql));
#define SET(K) if(0!=(check=cson_object_set(pay,K,tmp))){ \
    json_set_err((cson_rc.AllocError==check)        \
                 ? FSL_JSON_E_ALLOC : FSL_JSON_E_UNKNOWN,      \
                 "Object property insertion failed."); \
    goto error;\
  } (void)0

#if 0
  /* only for testing! */
  tmp = cson_value_new_string(blob_buffer(&sql),strlen(blob_buffer(&sql)));
  SET("timelineSql");
#endif

  blob_reset(&sql);
  /*
    REMINDER/FIXME(?): we have both uuid (the change uuid?)  and
    ticketUuid (the actual ticket). This is different from the wiki
    timeline, where we only have the wiki page uuid.
   */
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
  blob_reset(&sql);
  db_finalize(&q);
  return payV;
}

