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
#include "json_finfo.h"

#if INTERFACE
#include "json_detail.h"
#endif

/*
** Implements the /json/finfo page/command.
**
*/
cson_value * json_page_finfo(){
  cson_object * pay = NULL;
  cson_array * checkins = NULL;
  char const * zFilename = NULL;
  Blob sql = empty_blob;
  Stmt q = empty_Stmt;
  char const * zAfter = NULL;
  char const * zBefore = NULL;
  int limit = -1;
  int currentRow = 0;
  char const * zCheckin = NULL;
  char sort = -1;
  if(!g.perm.Read){
    json_set_err(FSL_JSON_E_DENIED,"Requires 'o' privileges.");
    return NULL;
  }
  json_warn( FSL_JSON_W_UNKNOWN, "Achtung: the output of the finfo command is up for change.");

  /* For the "name" argument we have to jump through some hoops to make sure that we don't
     get the fossil-internally-assigned "name" option.
  */
  zFilename = json_find_option_cstr2("name",NULL,NULL, g.json.dispatchDepth+1);
  if(!zFilename || !*zFilename){
    json_set_err(FSL_JSON_E_MISSING_ARGS, "Missing 'name' parameter.");
    return NULL;
  }

  if(0==db_int(0,"SELECT 1 FROM filename WHERE name=%Q",zFilename)){
    json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND, "File entry not found.");
    return NULL;
  }

  zBefore = json_find_option_cstr("before",NULL,"b");
  zAfter = json_find_option_cstr("after",NULL,"a");
  limit = json_find_option_int("limit",NULL,"n", -1);
  zCheckin = json_find_option_cstr("checkin",NULL,"ci");

  blob_append_sql(&sql,
/*0*/   "SELECT b.uuid,"
/*1*/   "   ci.uuid,"
/*2*/   "   (SELECT uuid FROM blob WHERE rid=mlink.fid),"  /* Current file uuid */
/*3*/   "   cast(strftime('%%s',event.mtime) AS INTEGER),"
/*4*/   "   coalesce(event.euser, event.user),"
/*5*/   "   coalesce(event.ecomment, event.comment),"
/*6*/   " (SELECT uuid FROM blob WHERE rid=mlink.pid),"  /* Parent file uuid */
/*7*/   "   event.bgcolor,"
/*8*/   " b.size,"
/*9*/   " (mlink.pid==0) AS isNew,"
/*10*/  " (mlink.fid==0) AS isDel"
        "  FROM mlink, blob b, event, blob ci, filename"
        " WHERE filename.name=%Q"
        "   AND mlink.fnid=filename.fnid"
        "   AND b.rid=mlink.fid"
        "   AND event.objid=mlink.mid"
        "   AND event.objid=ci.rid",
        zFilename
               );

  if( zCheckin && *zCheckin ){
    char * zU = NULL;
    int rc = name_to_uuid2( zCheckin, "ci", &zU );
    /*printf("zCheckin=[%s], zU=[%s]", zCheckin, zU);*/
    if(rc<=0){
      json_set_err((rc<0) ? FSL_JSON_E_AMBIGUOUS_UUID : FSL_JSON_E_RESOURCE_NOT_FOUND,
                   "Check-in UUID %s.", (rc<0) ? "is ambiguous" : "not found");
      blob_reset(&sql);
      return NULL;
    }
    blob_append_sql(&sql, " AND ci.uuid='%q'", zU);
    free(zU);
  }else{
    if( zAfter && *zAfter ){
      blob_append_sql(&sql, " AND event.mtime>=julianday('%q')", zAfter);
      sort = 1;
    }else if( zBefore && *zBefore ){
      blob_append_sql(&sql, " AND event.mtime<=julianday('%q')", zBefore);
    }
  }

  blob_append_sql(&sql," ORDER BY event.mtime %s /*sort*/", (sort>0?"ASC":"DESC"));
  /*printf("SQL=\n%s\n",blob_str(&sql));*/
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);

  pay = cson_new_object();
  cson_object_set(pay, "name", json_new_string(zFilename));
  if( limit > 0 ){
    cson_object_set(pay, "limit", json_new_int(limit));
  }
  checkins = cson_new_array();
  cson_object_set(pay, "checkins", cson_array_value(checkins));
  while( db_step(&q)==SQLITE_ROW ){
    cson_object * row = cson_new_object();
    int const isNew = db_column_int(&q,9);
    int const isDel = db_column_int(&q,10);
    cson_array_append( checkins, cson_object_value(row) );
    cson_object_set(row, "checkin", json_new_string( db_column_text(&q,1) ));
    cson_object_set(row, "uuid", json_new_string( db_column_text(&q,2) ));
    /*cson_object_set(row, "parentArtifact", json_new_string( db_column_text(&q,6) ));*/
    cson_object_set(row, "timestamp", json_new_int( db_column_int64(&q,3) ));
    cson_object_set(row, "user", json_new_string( db_column_text(&q,4) ));
    cson_object_set(row, "comment", json_new_string( db_column_text(&q,5) ));
    /*cson_object_set(row, "bgColor", json_new_string( db_column_text(&q,7) ));*/
    cson_object_set(row, "size", json_new_int( db_column_int64(&q,8) ));
    cson_object_set(row, "state",
                    json_new_string(json_artifact_status_to_string(isNew,isDel)));
    if( (0 < limit) && (++currentRow >= limit) ){
      break;
    }
  }
  db_finalize(&q);

  return pay ? cson_object_value(pay) : NULL;
}



#endif /* FOSSIL_ENABLE_JSON */
