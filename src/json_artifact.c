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
#include "json_artifact.h"

#if INTERFACE
#include "json_detail.h"
#endif

/*
** Internal callback for /json/artifact handlers. rid refers to
** the rid of a given type of artifact, and each callback is
** specialized to return a JSON form of one type of artifact.
*/
typedef cson_value * (*artifact_f)( int rid );

typedef struct ArtifactDispatchEntry {
  /**
     Artifact type name, e.g. "checkin".
   */
  char const * name;
  /**
     JSON construction callback.
   */
  artifact_f func;
  /**
     Must return true if g.perm has the proper permissions to fetch
     this info, else false. If it returns false, func() is skipped
     (producing no extra payload output).
   */
  char (*permCheck)();
} ArtifactDispatchEntry;


/*
** Generates an artifact Object for the given rid/zUuid. rid
** must refer to a Checkin.
**
** Returned value is NULL or an Object owned by the caller.
*/
cson_value * json_artifact_for_ci( int rid, char showFiles ){
  char const * zParent = NULL;
  cson_value * v = NULL;
  Stmt q;
  static cson_value * eventTypeLabel = NULL;
  if(!eventTypeLabel){
    eventTypeLabel = json_new_string("checkin");
    json_gc_add("$EVENT_TYPE_LABEL(commit)", eventTypeLabel, 1);
  }
  zParent = db_text(0,
    "SELECT uuid FROM plink, blob"
    " WHERE plink.cid=%d AND blob.rid=plink.pid AND plink.isprim",
    rid
  );

  db_prepare(&q, 
             "SELECT uuid, "
             " strftime('%%s',mtime), "
             " user, "
             " comment,"
             " strftime('%%s',omtime)"
             " FROM blob, event"
             " WHERE blob.rid=%d"
             "   AND event.objid=%d",
             rid, rid
             );
  if( db_step(&q)==SQLITE_ROW ){
    cson_object * o;
    cson_value * tmpV = NULL;
    v = cson_value_new_object();
    o = cson_value_get_object(v);
    const char *zUuid = db_column_text(&q, 0);
    char * zTmp;
    const char *zUser;
    const char *zComment;
    char * zEUser, * zEComment;
    int mtime, omtime;
#define SET(K,V) cson_object_set(o,(K), (V))
    SET("type", eventTypeLabel );
    SET("uuid",json_new_string(zUuid));
    SET("isLeaf", cson_value_new_bool(is_a_leaf(rid)));
    zUser = db_column_text(&q,2);
    zEUser = db_text(0,
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_USER, rid);
    if(zEUser){
      SET("user", json_new_string(zEUser));
      if(0!=strcmp(zEUser,zUser)){
        SET("originUser",json_new_string(zUser));
      }
      free(zEUser);
    }else{
      SET("user",json_new_string(zUser));
    }

    zComment = db_column_text(&q,3);
    zEComment = db_text(0, 
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_COMMENT, rid);
    if(zEComment){
      SET("comment",json_new_string(zEComment));
      if(0 != strcmp(zEComment,zComment)){
        SET("originComment", json_new_string(zComment));
      }
      free(zEComment);
    }else{
      SET("comment",json_new_string(zComment));
    }

    mtime = db_column_int(&q,1);
    SET("mtime",json_new_int(mtime));
    omtime = db_column_int(&q,4);
    if(omtime && (omtime!=mtime)){
      SET("originTime",json_new_int(omtime));
    }

    if(zParent){
      SET("parentUuid", json_new_string(zParent));
    }

    tmpV = json_tags_for_rid(rid);
    if(tmpV){
      SET("tags",tmpV);
    }

    if( showFiles ){
      cson_value * fileList = json_get_changed_files(rid);
      if(fileList){
        SET("files",fileList);
      }
    }


#undef SET
  }
  db_finalize(&q);
  return v;
}

/*
** Sub-impl of /json/artifact for checkins.
*/
static cson_value * json_artifact_ci( int rid ){
  return json_artifact_for_ci(rid, 1);
}

static char perms_can_read(){
  return g.perm.Read ? 1 : 0;
}

static ArtifactDispatchEntry ArtifactDispatchList[] = {
{"checkin", json_artifact_ci, perms_can_read},
{"tag", NULL, perms_can_read},
{"ticket", NULL, perms_can_read},
{"wiki", NULL, perms_can_read},
{NULL,NULL,NULL}
};

/*
** Impl of /json/artifact. This basically just determines the type of
** an artifact and forwards the real work to another function.
*/
cson_value * json_page_artifact(){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  char const * zName = NULL;
  char const * zType = NULL;
  char const * zUuid = NULL;
  Blob uuid = empty_blob;
  int rc;
  int rid;
  ArtifactDispatchEntry const * dispatcher = &ArtifactDispatchList[0];
  zName = g.isHTTP
    ? json_getenv_cstr("uuid")
    : find_option("uuid","u",1);
  if(!zName||!*zName){
    zName = json_command_arg(g.json.dispatchDepth+1);
    if(!zName || !*zName) {
      g.json.resultCode = FSL_JSON_E_MISSING_ARGS;
      return NULL;
    }
  }

  if( validate16(zName, strlen(zName)) ){
    if( db_exists("SELECT 1 FROM ticket WHERE tkt_uuid GLOB '%q*'", zName) ){
      zType = "ticket";
      goto handle_entry;
    }
    if( db_exists("SELECT 1 FROM tag WHERE tagname GLOB 'event-%q*'", zName) ){
      zType = "tag";
      goto handle_entry;
    }
  }
  blob_set(&uuid,zName);
  rc = name_to_uuid(&uuid,-1,"*");
  if(1==rc){
    g.json.resultCode = FSL_JSON_E_RESOURCE_NOT_FOUND;
    goto error;
  }else if(2==rc){
    g.json.resultCode = FSL_JSON_E_AMBIGUOUS_UUID;
    goto error;
  }
  zUuid = blob_str(&uuid);
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid='%s'", zUuid);
  if(0==rid){
    g.json.resultCode = FSL_JSON_E_RESOURCE_NOT_FOUND;
    goto error;
  }
  if( db_exists("SELECT 1 FROM mlink WHERE mid=%d", rid)
      || db_exists("SELECT 1 FROM plink WHERE cid=%d", rid)
      || db_exists("SELECT 1 FROM plink WHERE pid=%d", rid)){
    zType = "checkin";
    goto handle_entry;
  }else if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                      " WHERE rid=%d AND tagname LIKE 'wiki-%%'", rid) ){
    zType = "wiki";
    goto handle_entry;
  }else if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                      " WHERE rid=%d AND tagname LIKE 'tkt-%%'", rid) ){
    zType = "ticket";
    goto handle_entry;
  }else{
    g.json.resultCode = FSL_JSON_E_RESOURCE_NOT_FOUND;
    goto error;
  }

  error:
  assert( 0 != g.json.resultCode );
  goto veryend;

  handle_entry:
  assert( (NULL != zType) && "Internal dispatching error." );
  for( ; dispatcher->name; ++dispatcher ){
    if(0!=strcmp(dispatcher->name, zType)){
      continue;
    }else{
      if( ! (*dispatcher->permCheck)() ){
        g.json.resultCode = FSL_JSON_E_DENIED;
      }
      break;
    }
  }
  if(!g.json.resultCode){
    payV = cson_value_new_object();
    pay = cson_value_get_object(payV);
    assert( NULL != zType );
    cson_object_set( pay, "type", json_new_string(zType) );
    /*cson_object_set( pay, "uuid", json_new_string(zUuid) );*/
    cson_object_set( pay, "name", json_new_string(zName ? zName : zUuid) );
    cson_object_set( pay, "rid", cson_value_new_integer(rid) );
    if( !dispatcher->name ){
      cson_object_set(pay,"artifact",
                      json_new_string("TODO: handle this artifact type!"));
    }else {
      cson_value * entry = (*dispatcher->func)(rid);
      if(entry){
        cson_object_set(pay, "artifact", entry);
      }
    }
  }
  veryend:
  blob_reset(&uuid);
  return payV;
}

