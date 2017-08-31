#ifdef FOSSIL_ENABLE_JSON
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
#include "json_tag.h"

#if INTERFACE
#include "json_detail.h"
#endif


static cson_value * json_tag_add();
static cson_value * json_tag_cancel();
static cson_value * json_tag_find();
static cson_value * json_tag_list();
/*
** Mapping of /json/tag/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Tag[] = {
{"add", json_tag_add, 0},
{"cancel", json_tag_cancel, 0},
{"find", json_tag_find, 0},
{"list", json_tag_list, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** Implements the /json/tag family of pages/commands.
**
*/
cson_value * json_page_tag(){
  return json_page_dispatch_helper(&JsonPageDefs_Tag[0]);
}


/*
** Impl of /json/tag/add.
*/
static cson_value * json_tag_add(){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  char const * zName = NULL;
  char const * zCheckin = NULL;
  char fRaw = 0;
  char fPropagate = 0;
  char const * zValue = NULL;
  const char *zPrefix = NULL;

  if( !g.perm.Write ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'i' permissions.");
    return NULL;
  }
  fRaw = json_find_option_bool("raw",NULL,NULL,0);
  fPropagate = json_find_option_bool("propagate",NULL,NULL,0);
  zName = json_find_option_cstr("name",NULL,NULL);
  zPrefix = fRaw ? "" : "sym-";
  if(!zName || !*zName){
    if(!fossil_has_json()){
      zName = json_command_arg(3);
    }
    if(!zName || !*zName){
      json_set_err(FSL_JSON_E_MISSING_ARGS,
                   "'name' parameter is missing.");
      return NULL;
    }
  }

  zCheckin = json_find_option_cstr("checkin",NULL,NULL);
  if( !zCheckin ){
    if(!fossil_has_json()){
      zCheckin = json_command_arg(4);
    }
    if(!zCheckin || !*zCheckin){
      json_set_err(FSL_JSON_E_MISSING_ARGS,
                   "'checkin' parameter is missing.");
      return NULL;
    }
  }


  zValue = json_find_option_cstr("value",NULL,NULL);
  if(!zValue && !fossil_has_json()){
    zValue = json_command_arg(5);
  }

  db_begin_transaction();
  tag_add_artifact(zPrefix, zName, zCheckin, zValue,
                   1+fPropagate,NULL/*DateOvrd*/,NULL/*UserOvrd*/);
  db_end_transaction(0);

  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  cson_object_set(pay, "name", json_new_string(zName) );
  cson_object_set(pay, "value", (zValue&&*zValue)
                  ? json_new_string(zValue)
                  : cson_value_null());
  cson_object_set(pay, "propagate", cson_value_new_bool(fPropagate));
  cson_object_set(pay, "raw", cson_value_new_bool(fRaw));
  {
    Blob uu = empty_blob;
    int rc;
    blob_append(&uu, zName, -1);
    rc = name_to_uuid(&uu, 9, "*");
    if(0!=rc){
      json_set_err(FSL_JSON_E_UNKNOWN,"Could not convert name back to UUID!");
      blob_reset(&uu);
      goto error;
    }
    cson_object_set(pay, "appliedTo", json_new_string(blob_buffer(&uu)));
    blob_reset(&uu);
  }

  goto ok;
  error:
  assert( 0 != g.json.resultCode );
  cson_value_free(payV);
  payV = NULL;
  ok:
  return payV;
}


/*
** Impl of /json/tag/cancel.
*/
static cson_value * json_tag_cancel(){
  char const * zName = NULL;
  char const * zCheckin = NULL;
  char fRaw = 0;
  const char *zPrefix = NULL;

  if( !g.perm.Write ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'i' permissions.");
    return NULL;
  }

  fRaw = json_find_option_bool("raw",NULL,NULL,0);
  zPrefix = fRaw ? "" : "sym-";
  zName = json_find_option_cstr("name",NULL,NULL);
  if(!zName || !*zName){
    if(!fossil_has_json()){
      zName = json_command_arg(3);
    }
    if(!zName || !*zName){
      json_set_err(FSL_JSON_E_MISSING_ARGS,
                   "'name' parameter is missing.");
      return NULL;
    }
  }

  zCheckin = json_find_option_cstr("checkin",NULL,NULL);
  if( !zCheckin ){
    if(!fossil_has_json()){
      zCheckin = json_command_arg(4);
    }
    if(!zCheckin || !*zCheckin){
      json_set_err(FSL_JSON_E_MISSING_ARGS,
                   "'checkin' parameter is missing.");
      return NULL;
    }
  }
  /* FIXME?: verify that the tag is currently active. We have no real
     error case unless we do that.
  */
  db_begin_transaction();
  tag_add_artifact(zPrefix, zName, zCheckin, NULL, 0, 0, 0);
  db_end_transaction(0);
  return NULL;
}


/*
** Impl of /json/tag/find.
*/
static cson_value * json_tag_find(){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  cson_value * listV = NULL;
  cson_array * list = NULL;
  char const * zName = NULL;
  char const * zType = NULL;
  char const * zType2 = NULL;
  char fRaw = 0;
  Stmt q = empty_Stmt;
  int limit = 0;
  int tagid = 0;

  if( !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'o' permissions.");
    return NULL;
  }
  zName = json_find_option_cstr("name",NULL,NULL);
  if(!zName || !*zName){
    if(!fossil_has_json()){
      zName = json_command_arg(3);
    }
    if(!zName || !*zName){
      json_set_err(FSL_JSON_E_MISSING_ARGS,
                   "'name' parameter is missing.");
      return NULL;
    }
  }
  zType = json_find_option_cstr("type",NULL,"t");
  if(!zType || !*zType){
    zType = "*";
    zType2 = zType;
  }else{
    switch(*zType){
      case 'c': zType = "ci"; zType2 = "checkin"; break;
      case 'e': zType = "e"; zType2 = "event"; break;
      case 'w': zType = "w"; zType2 = "wiki"; break;
      case 't': zType = "t"; zType2 = "ticket"; break;
    }
  }

  limit = json_find_option_int("limit",NULL,"n",0);
  fRaw = json_find_option_bool("raw",NULL,NULL,0);

  tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname='%s' || %Q",
                 fRaw ? "" : "sym-",
                 zName);

  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  cson_object_set(pay, "name", json_new_string(zName));
  cson_object_set(pay, "raw", cson_value_new_bool(fRaw));
  cson_object_set(pay, "type", json_new_string(zType2));
  cson_object_set(pay, "limit", json_new_int(limit));

#if 1
  if( tagid<=0 ){
    cson_object_set(pay,"artifacts", cson_value_null());
    json_warn(FSL_JSON_W_TAG_NOT_FOUND, "Tag not found.");
    return payV;
  }
#endif

  if( fRaw ){
    db_prepare(&q,
               "SELECT blob.uuid FROM tagxref, blob"
               " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
               "   AND tagxref.tagtype>0"
               "   AND blob.rid=tagxref.rid"
               "%s LIMIT %d",
               zName,
               (limit>0)?"":"--", limit
               );
    while( db_step(&q)==SQLITE_ROW ){
      if(!listV){
        listV = cson_value_new_array();
        list = cson_value_get_array(listV);
      }
      cson_array_append(list, cson_sqlite3_column_to_value(q.pStmt,0));
    }
    db_finalize(&q);
  }else{
    char const * zSqlBase = /*modified from timeline_query_for_tty()*/
      " SELECT"
#if 0
      "   blob.rid AS rid,"
#endif
      "   uuid AS uuid,"
      "   cast(strftime('%s',event.mtime) as int) AS timestamp,"
      "   coalesce(ecomment,comment) AS comment,"
      "   coalesce(euser,user) AS user,"
      "   CASE event.type"
      "     WHEN 'ci' THEN 'checkin'"
      "     WHEN 'w' THEN 'wiki'"
      "     WHEN 'e' THEN 'event'"
      "     WHEN 't' THEN 'ticket'"
      "     ELSE 'unknown'"
      "   END"
      "   AS eventType"
      " FROM event, blob"
      " WHERE blob.rid=event.objid"
      ;
    /* FIXME: re-add tags. */
    db_prepare(&q,
               "%s"
               "  AND event.type GLOB '%q'"
               "  AND blob.rid IN ("
               "    SELECT rid FROM tagxref"
               "      WHERE tagtype>0 AND tagid=%d"
               "  )"
               " ORDER BY event.mtime DESC"
               "%s LIMIT %d",
               zSqlBase /*safe-for-%s*/, zType, tagid,
               (limit>0)?"":"--", limit
               );
    listV = json_stmt_to_array_of_obj(&q, NULL);
    db_finalize(&q);
  }

  if(!listV) {
    listV = cson_value_null();
  }
  cson_object_set(pay, "artifacts", listV);
  return payV;
}


/*
** Impl for /json/tag/list
**
** TODOs:
**
** Add -type TYPE (ci, w, e, t)
*/
static cson_value * json_tag_list(){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  cson_value const * tagsVal = NULL;
  char const * zCheckin = NULL;
  char fRaw = 0;
  char fTicket = 0;
  Stmt q = empty_Stmt;

  if( !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'o' permissions.");
    return NULL;
  }

  fRaw = json_find_option_bool("raw",NULL,NULL,0);
  fTicket = json_find_option_bool("includeTickets","tkt","t",0);
  zCheckin = json_find_option_cstr("checkin",NULL,NULL);
  if( !zCheckin ){
    zCheckin = json_command_arg( g.json.dispatchDepth + 1);
    if( !zCheckin && cson_value_is_string(g.json.reqPayload.v) ){
      zCheckin = cson_string_cstr(cson_value_get_string(g.json.reqPayload.v));
      assert(zCheckin);
    }
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  cson_object_set(pay, "raw", cson_value_new_bool(fRaw) );
  if( zCheckin ){
    /**
       Tags for a specific check-in. Output format:

       RAW mode:

       {
           "sym-tagname": (value || null),
           ...other tags...
       }

       Non-raw:

       {
          "tagname": (value || null),
          ...other tags...
       }
    */
    cson_value * objV = NULL;
    cson_object * obj = NULL;
    int const rid = name_to_rid(zCheckin);
    if(0==rid){
      json_set_err(FSL_JSON_E_UNRESOLVED_UUID,
                   "Could not find artifact for check-in [%s].",
                   zCheckin);
      goto error;
    }
    cson_object_set(pay, "checkin", json_new_string(zCheckin));
    db_prepare(&q,
               "SELECT tagname, value FROM tagxref, tag"
               " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
               "   AND tagtype>%d"
               " ORDER BY tagname",
               rid,
               fRaw ? -1 : 0
               );
    while( SQLITE_ROW == db_step(&q) ){
      const char *zName = db_column_text(&q, 0);
      const char *zValue = db_column_text(&q, 1);
      if( fRaw==0 ){
        if( 0!=strncmp(zName, "sym-", 4) ) continue;
        zName += 4;
        assert( *zName );
      }
      if(NULL==objV){
        objV = cson_value_new_object();
        obj = cson_value_get_object(objV);
        tagsVal = objV;
        cson_object_set( pay, "tags", objV );
      }
      if( zValue && zValue[0] ){
        cson_object_set( obj, zName, json_new_string(zValue) );
      }else{
        cson_object_set( obj, zName, cson_value_null() );
      }
    }
    db_finalize(&q);
  }else{/* all tags */
    /* Output format:

    RAW mode:

    ["tagname", "sym-tagname2",...]

    Non-raw:

    ["tagname", "tagname2",...]

    i don't really like the discrepancy in the format but this list
    can get really long and (A) most tags don't have values, (B) i
    don't want to bloat it more, and (C) cson_object_set() is O(N)
    (N=current number of properties) because it uses an unsorted list
    internally (for memory reasons), so this can slow down appreciably
    on a long list. The culprit is really tkt- tags, as there is one
    for each ticket (941 in the main fossil repo as of this writing).
    */
    Blob sql = empty_blob;
    cson_value * arV = NULL;
    cson_array * ar = NULL;
    blob_append(&sql,
                "SELECT tagname FROM tag"
                " WHERE EXISTS(SELECT 1 FROM tagxref"
                "               WHERE tagid=tag.tagid"
                "                 AND tagtype>0)",
                -1
                );
    if(!fTicket){
      blob_append(&sql, " AND tagname NOT GLOB('tkt-*') ", -1);
    }
    blob_append(&sql,
                " ORDER BY tagname", -1);
    db_prepare(&q, "%s", blob_sql_text(&sql));
    blob_reset(&sql);
    cson_object_set(pay, "includeTickets", cson_value_new_bool(fTicket) );
    while( SQLITE_ROW == db_step(&q) ){
      const char *zName = db_column_text(&q, 0);
      if(NULL==arV){
        arV = cson_value_new_array();
        ar = cson_value_get_array(arV);
        cson_object_set(pay, "tags", arV);
        tagsVal = arV;
      }
      else if( !fRaw && (0==strncmp(zName, "sym-", 4))){
        zName += 4;
        assert( *zName );
      }
      cson_array_append(ar, json_new_string(zName));
    }
    db_finalize(&q);
  }

  goto end;
  error:
  assert(0 != g.json.resultCode);
  cson_value_free(payV);
  payV = NULL;
  end:
  if( payV && !tagsVal ){
    cson_object_set( pay, "tags", cson_value_null() );
  }
  return payV;
}
#endif /* FOSSIL_ENABLE_JSON */
