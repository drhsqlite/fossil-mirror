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
**
** Implementations may assert() that rid refers to requested artifact
** type, since mismatches in the artifact types come from
** json_page_artifact() as opposed to client data.
*/
typedef cson_value * (*artifact_f)( int rid );

/*
** Internal per-artifact-type dispatching helper.
*/
typedef struct ArtifactDispatchEntry {
  /**
     Artifact type name, e.g. "checkin", "ticket", "wiki".
   */
  char const * name;

  /**
     JSON construction callback. Creates the contents for the
     payload.artifact property of /json/artifact responses.
  */
  artifact_f func;
} ArtifactDispatchEntry;


/*
** Generates an artifact Object for the given rid,
** which must refer to a Checkin.
**
** Returned value is NULL or an Object owned by the caller.
*/
cson_value * json_artifact_for_ci( int rid, char showFiles ){
  char * zParent = NULL;
  cson_value * v = NULL;
  Stmt q;
  static cson_value * eventTypeLabel = NULL;
  if(!eventTypeLabel){
    eventTypeLabel = json_new_string("checkin");
    json_gc_add("$EVENT_TYPE_LABEL(commit)", eventTypeLabel);
  }
  zParent = db_text(0,
    "SELECT uuid FROM plink, blob"
    " WHERE plink.cid=%d AND blob.rid=plink.pid AND plink.isprim",
    rid
  );

  db_prepare(&q, 
             "SELECT uuid, "
             " cast(strftime('%%s',mtime) as int), "
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
    const char *zUuid = db_column_text(&q, 0);
    char * zTmp;
    const char *zUser;
    const char *zComment;
    char * zEUser, * zEComment;
    int mtime, omtime;
    v = cson_value_new_object();
    o = cson_value_get_object(v);
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

    tmpV = json_tags_for_rid(rid,0);
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
  free(zParent);
  db_finalize(&q);
  return v;
}

/*
** Very incomplete/incorrect impl of /json/artifact/TICKET_ID.
*/
cson_value * json_artifact_ticket( int rid ){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  Manifest *pTktChng = NULL;
  static cson_value * eventTypeLabel = NULL;
  if(! g.perm.RdTkt ){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }
  if(!eventTypeLabel){
    eventTypeLabel = json_new_string("ticket");
    json_gc_add("$EVENT_TYPE_LABEL(ticket)", eventTypeLabel);
  }

  pTktChng = manifest_get(rid, CFTYPE_TICKET);
  if( pTktChng==0 ){
    g.json.resultCode = FSL_JSON_E_MANIFEST_READ_FAILED;
    return NULL;
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  cson_object_set(pay, "eventType", eventTypeLabel );
  cson_object_set(pay, "uuid", json_new_string(pTktChng->zTicketUuid));
  cson_object_set(pay, "user", json_new_string(pTktChng->zUser));
  cson_object_set(pay, "timestamp", json_julian_to_timestamp(pTktChng->rDate));
  manifest_destroy(pTktChng);
  return payV;
}

/*
** Sub-impl of /json/artifact for checkins.
*/
static cson_value * json_artifact_ci( int rid ){
  if(! g.perm.Read ){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }else{
    return json_artifact_for_ci(rid, 1);
  }
}

/*
** Internal mapping of /json/artifact/FOO commands/callbacks.
*/
static ArtifactDispatchEntry ArtifactDispatchList[] = {
{"checkin", json_artifact_ci},
{"tag", NULL},
{"ticket", json_artifact_ticket},
{"wiki", NULL},
/* Final entry MUST have a NULL name. */
{NULL,NULL}
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
  cson_value * entry = NULL;
  Blob uuid = empty_blob;
  int rc;
  int rid = 0;
  ArtifactDispatchEntry const * dispatcher = &ArtifactDispatchList[0];
  zName = json_find_option_cstr("uuid",NULL,"u");
  if(!zName||!*zName){
    zName = json_command_arg(g.json.dispatchDepth+1);
    if(!zName || !*zName) {
      json_set_err(FSL_JSON_E_MISSING_ARGS,
                   "Missing 'uuid' argument.");
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
      entry = (*dispatcher->func)(rid);
      break;
    }
  }
  if(!g.json.resultCode){
    assert( NULL != entry );
    assert( NULL != zType );
    payV = cson_value_new_object();
    pay = cson_value_get_object(payV);
    cson_object_set( pay, "type", json_new_string(zType) );
    /*cson_object_set( pay, "uuid", json_new_string(zUuid) );*/
    cson_object_set( pay, "name", json_new_string(zName ? zName : zUuid) );
    cson_object_set( pay, "rid", cson_value_new_integer(rid) );
    if(entry){
      cson_object_set(pay, "artifact", entry);
    }
  }else{
    assert((NULL == entry) && "Internal misuse - callback must return NULL on error.");
  }
  veryend:
  blob_reset(&uuid);
  return payV;
}

