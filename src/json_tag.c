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
  char propagate = 0;
  char raw = 0;
  char const * zPrefix = NULL; /* raw ? "" : "sym-" */
  g.json.resultCode = FSL_JSON_E_NYI;
  return payV;
}


/*
** Impl of /json/tag/cancel.
*/
static cson_value * json_tag_cancel(){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  g.json.resultCode = FSL_JSON_E_NYI;
  return payV;
}


/*
** Impl of /json/tag/find.
*/
static cson_value * json_tag_find(){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  g.json.resultCode = FSL_JSON_E_NYI;
  return payV;
}


/*
** Impl for /json/tag/list
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
       Tags for a specific checkin. Output format:

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
                   "Could not find artifact for checkin [%s].",
                   zCheckin);
      goto error;
    }
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
    
    ["sym-tagname", "sym-tagname2",...]

    Non-raw:

    ["tagname", "tagname2",...]

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
    db_prepare(&q, blob_buffer(&sql));
    blob_reset(&sql);
    cson_object_set(pay, "includeTickets", cson_value_new_bool(fTicket) );
    while( SQLITE_ROW == db_step(&q) ){
      const char *zName = db_column_text(&q, 0);
      if(NULL==arV){
        arV = cson_value_new_array();
        ar = cson_value_get_array(arV);
        tagsVal = arV;
        cson_object_set(pay, "tags", arV);
      }
      if(!fTicket && (0==strncmp(zName, "tkt-", 4))) continue;
      else if( !fRaw && (0==strncmp(zName, "sym-", 4))){
        zName += 4;
        assert( *zName );
      }
      cson_array_append(ar, json_new_string(zName));
    }
    db_finalize(&q);
    if( ! arV ){
      arV = cson_value_null();
    }
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
