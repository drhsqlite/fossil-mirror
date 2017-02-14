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
**
** The pParent parameter points to the response payload object.  It
** _may_ be used to populate "top-level" information in the response
** payload, but normally this is neither necessary nor desired.
*/
typedef cson_value * (*artifact_f)( cson_object * pParent, int rid );

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
** Generates a JSON Array reference holding the parent UUIDs (as strings).
** If it finds no matches then it returns NULL (OOM is a fatal error).
**
** Returned value is NULL or an Array owned by the caller.
*/
cson_value * json_parent_uuids_for_ci( int rid ){
  Stmt q = empty_Stmt;
  cson_array * pParents = NULL;
  db_prepare( &q,
              "SELECT uuid FROM plink, blob"
              " WHERE plink.cid=%d AND blob.rid=plink.pid"
              " ORDER BY plink.isprim DESC",
              rid );
  while( SQLITE_ROW==db_step(&q) ){
    if(!pParents) {
      pParents = cson_new_array();
    }
    cson_array_append( pParents, cson_sqlite3_column_to_value( q.pStmt, 0 ) );
  }
  db_finalize(&q);
  return cson_array_value(pParents);
}

/*
** Generates an artifact Object for the given rid,
** which must refer to a Check-in.
**
** Returned value is NULL or an Object owned by the caller.
*/
cson_value * json_artifact_for_ci( int rid, char showFiles ){
  cson_value * v = NULL;
  Stmt q = empty_Stmt;
  static cson_value * eventTypeLabel = NULL;
  if(!eventTypeLabel){
    eventTypeLabel = json_new_string("checkin");
    json_gc_add("$EVENT_TYPE_LABEL(commit)", eventTypeLabel);
  }

  db_prepare(&q,
             "SELECT b.uuid, "
             " cast(strftime('%%s',e.mtime) as int), "
             " strftime('%%s',e.omtime),"
             " e.user, "
             " e.comment"
             " FROM blob b, event e"
             " WHERE b.rid=%d"
             "   AND e.objid=%d",
             rid, rid
             );
  if( db_step(&q)==SQLITE_ROW ){
    cson_object * o;
    cson_value * tmpV = NULL;
    const char *zUuid = db_column_text(&q, 0);
    const char *zUser;
    const char *zComment;
    char * zEUser, * zEComment;
    i64 mtime, omtime;
    v = cson_value_new_object();
    o = cson_value_get_object(v);
#define SET(K,V) cson_object_set(o,(K), (V))
    SET("type", eventTypeLabel );
    SET("uuid",json_new_string(zUuid));
    SET("isLeaf", cson_value_new_bool(is_a_leaf(rid)));

    mtime = db_column_int64(&q,1);
    SET("timestamp",json_new_int(mtime));
    omtime = db_column_int64(&q,2);
    if(omtime && (omtime!=mtime)){
      SET("originTime",json_new_int(omtime));
    }

    zUser = db_column_text(&q,3);
    zEUser = db_text(0,
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_USER, rid);
    if(zEUser){
      SET("user", json_new_string(zEUser));
      if(0!=fossil_strcmp(zEUser,zUser)){
        SET("originUser",json_new_string(zUser));
      }
      free(zEUser);
    }else{
      SET("user",json_new_string(zUser));
    }

    zComment = db_column_text(&q,4);
    zEComment = db_text(0,
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_COMMENT, rid);
    if(zEComment){
      SET("comment",json_new_string(zEComment));
      if(0 != fossil_strcmp(zEComment,zComment)){
        SET("originComment", json_new_string(zComment));
      }
      free(zEComment);
    }else{
      SET("comment",json_new_string(zComment));
    }

    tmpV = json_parent_uuids_for_ci(rid);
    if(tmpV){
      SET("parents", tmpV);
    }

    tmpV = json_tags_for_checkin_rid(rid,0);
    if(tmpV){
      SET("tags",tmpV);
    }

    if( showFiles ){
      tmpV = json_get_changed_files(rid, 1);
      if(tmpV){
        SET("files",tmpV);
      }
    }

#undef SET
  }
  db_finalize(&q);
  return v;
}

/*
** Very incomplete/incorrect impl of /json/artifact/TICKET_ID.
*/
cson_value * json_artifact_ticket( cson_object * zParent, int rid ){
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

  pTktChng = manifest_get(rid, CFTYPE_TICKET, 0);
  if( pTktChng==0 ){
    g.json.resultCode = FSL_JSON_E_MANIFEST_READ_FAILED;
    return NULL;
  }
  pay = cson_new_object();
  cson_object_set(pay, "eventType", eventTypeLabel );
  cson_object_set(pay, "uuid", json_new_string(pTktChng->zTicketUuid));
  cson_object_set(pay, "user", json_new_string(pTktChng->zUser));
  cson_object_set(pay, "timestamp", json_julian_to_timestamp(pTktChng->rDate));
  manifest_destroy(pTktChng);
  return cson_object_value(pay);
}

/*
** Sub-impl of /json/artifact for check-ins.
*/
static cson_value * json_artifact_ci( cson_object * zParent, int rid ){
  if(!g.perm.Read){
    json_set_err( FSL_JSON_E_DENIED, "Viewing check-ins requires 'o' privileges." );
    return NULL;
  }else{
    cson_value * artV = json_artifact_for_ci(rid, 1);
    cson_object * art = cson_value_get_object(artV);
    if(art){
      cson_object_merge( zParent, art, CSON_MERGE_REPLACE );
      cson_free_object(art);
    }
    return cson_object_value(zParent);
  }
}

/*
** Internal mapping of /json/artifact/FOO commands/callbacks.
*/
static ArtifactDispatchEntry ArtifactDispatchList[] = {
{"checkin", json_artifact_ci},
{"file", json_artifact_file},
{"tag", NULL},
{"ticket", json_artifact_ticket},
{"wiki", json_artifact_wiki},
/* Final entry MUST have a NULL name. */
{NULL,NULL}
};

/*
** Internal helper which returns:
**
** If the "format" (CLI: -f) flag is set function returns the same as
** json_wiki_get_content_format_flag(), else it returns true (non-0)
** if either the includeContent (HTTP) or -content|-c boolean flags
** (CLI) are set.
*/
static int json_artifact_get_content_format_flag(){
  enum { MagicValue = -9 };
  int contentFormat = json_wiki_get_content_format_flag(MagicValue);
  if(MagicValue == contentFormat){
    contentFormat = json_find_option_bool("includeContent","content","c",0) /* deprecated */ ? -1 : 0;
  }
  return contentFormat;
}

extern int json_wiki_get_content_format_flag( int defaultValue ) /* json_wiki.c */;

cson_value * json_artifact_wiki(cson_object * zParent, int rid){
  if( ! g.perm.RdWiki ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'j' privileges.");
    return NULL;
  }else{
    enum { MagicValue = -9 };
    int const contentFormat = json_artifact_get_content_format_flag();
    return json_get_wiki_page_by_rid(rid, contentFormat);
  }
}

/*
** Internal helper for routines which add a "status" flag to file
** artifact data. isNew and isDel should be the "is this object new?"
** and "is this object removed?" flags of the underlying query.  This
** function returns a static string from the set (added, removed,
** modified), depending on the combination of the two args.
**
** Reminder to self: (mlink.pid==0) AS isNew, (mlink.fid==0) AS isDel
*/
char const * json_artifact_status_to_string( char isNew, char isDel ){
  return isNew
    ? "added"
    : (isDel
       ? "removed"
       : "modified");
}

cson_value * json_artifact_file(cson_object * zParent, int rid){
  cson_object * pay = NULL;
  Stmt q = empty_Stmt;
  cson_array * checkin_arr = NULL;
  int contentFormat;
  i64 contentSize = -1;
  char * parentUuid;
  if( ! g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'o' privileges.");
    return NULL;
  }

  pay = zParent;

  contentFormat = json_artifact_get_content_format_flag();
  if( 0 != contentFormat ){
    Blob content = empty_blob;
    const char *zMime;
    char const * zFormat = (contentFormat<1) ? "raw" : "html";
    content_get(rid, &content);
    zMime = mimetype_from_content(&content);
    cson_object_set(zParent, "contentType",
                    json_new_string(zMime ? zMime : "text/plain"));
    if(!zMime){/* text/plain */
      if(0 < blob_size(&content)){
        if( 0 < contentFormat ){/*HTML-size it*/
          Blob html = empty_blob;
          wiki_convert(&content, &html, 0);
          assert( blob_size(&content) < blob_size(&html) );
          blob_swap( &html, &content );
          assert( blob_size(&content) > blob_size(&html) );
          blob_reset( &html );
        }/*else as-is*/
      }
      cson_object_set(zParent, "content",
                      cson_value_new_string(blob_str(&content),
                                            (unsigned int)blob_size(&content)));
    }/*else binary: ignore*/
    contentSize = blob_size(&content);
    cson_object_set(zParent, "contentSize", json_new_int(contentSize) );
    cson_object_set(zParent, "contentFormat", json_new_string(zFormat) );
    blob_reset(&content);
  }
  contentSize = db_int64(-1, "SELECT size FROM blob WHERE rid=%d", rid);
  assert( -1 < contentSize );
  cson_object_set(zParent, "size", json_new_int(contentSize) );

  parentUuid = db_text(NULL,
                       "SELECT DISTINCT p.uuid "
                       "FROM blob p, blob f, mlink m "
                       "WHERE m.pid=p.rid "
                       "AND m.fid=f.rid "
                       "AND f.rid=%d",
                       rid
                       );
  if(parentUuid){
    cson_object_set( zParent, "parent", json_new_string(parentUuid) );
    fossil_free(parentUuid);
  }

  /* Find check-ins associated with this file... */
  db_prepare(&q,
      "SELECT filename.name AS name, "
      "  (mlink.pid==0) AS isNew,"
      "  (mlink.fid==0) AS isDel,"
      "  cast(strftime('%%s',event.mtime) as int) AS timestamp,"
      "  coalesce(event.ecomment,event.comment) as comment,"
      "  coalesce(event.euser,event.user) as user,"
#if 0
      "  a.size AS size," /* same for all check-ins. */
#endif
      "  b.uuid as checkin, "
#if 0
      "  mlink.mperm as mperm,"
#endif
      "  coalesce((SELECT value FROM tagxref"
                      "  WHERE tagid=%d AND tagtype>0 AND "
                      " rid=mlink.mid),'trunk') as branch"
      "  FROM mlink, filename, event, blob a, blob b"
      " WHERE filename.fnid=mlink.fnid"
      "   AND event.objid=mlink.mid"
      "   AND a.rid=mlink.fid"
      "   AND b.rid=mlink.mid"
      "   AND mlink.fid=%d"
      "   ORDER BY filename.name, event.mtime",
      TAG_BRANCH, rid
    );
  /* TODO: add a "state" flag for the file in each check-in,
     e.g. "modified", "new", "deleted".
   */
  checkin_arr = cson_new_array();
  cson_object_set(pay, "checkins", cson_array_value(checkin_arr));
  while( (SQLITE_ROW==db_step(&q) ) ){
    cson_object * row = cson_value_get_object(cson_sqlite3_row_to_object(q.pStmt));
    /* FIXME: move this isNew/isDel stuff into an SQL CASE statement. */
    char const isNew = cson_value_get_bool(cson_object_get(row,"isNew"));
    char const isDel = cson_value_get_bool(cson_object_get(row,"isDel"));
    cson_object_set(row, "isNew", NULL);
    cson_object_set(row, "isDel", NULL);
    cson_object_set(row, "state",
                    json_new_string(json_artifact_status_to_string(isNew, isDel)));
    cson_array_append( checkin_arr, cson_object_value(row) );
  }
  db_finalize(&q);
  return cson_object_value(pay);
}

/*
** Impl of /json/artifact. This basically just determines the type of
** an artifact and forwards the real work to another function.
*/
cson_value * json_page_artifact(){
  cson_object * pay = NULL;
  char const * zName = NULL;
  char const * zType = NULL;
  char const * zUuid = NULL;
  cson_value * entry = NULL;
  Blob uuid = empty_blob;
  int rc;
  int rid = 0;
  ArtifactDispatchEntry const * dispatcher = &ArtifactDispatchList[0];
  zName = json_find_option_cstr2("name", NULL, NULL, g.json.dispatchDepth+1);
  if(!zName || !*zName) {
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "Missing 'name' argument.");
    return NULL;
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
  /* FIXME: check for a filename if all else fails. */
  if(1==rc){
    g.json.resultCode = FSL_JSON_E_RESOURCE_NOT_FOUND;
    goto error;
  }else if(2==rc){
    g.json.resultCode = FSL_JSON_E_AMBIGUOUS_UUID;
    goto error;
  }
  zUuid = blob_str(&uuid);
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q", zUuid);
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
  }else if ( db_exists("SELECT 1 FROM mlink WHERE fid = %d", rid) ){
    zType = "file";
    goto handle_entry;
  }else{
    g.json.resultCode = FSL_JSON_E_RESOURCE_NOT_FOUND;
    goto error;
  }

  error:
  assert( 0 != g.json.resultCode );
  goto veryend;

  handle_entry:
  pay = cson_new_object();
  assert( (NULL != zType) && "Internal dispatching error." );
  for( ; dispatcher->name; ++dispatcher ){
    if(0!=fossil_strcmp(dispatcher->name, zType)){
      continue;
    }else{
      entry = (*dispatcher->func)(pay, rid);
      break;
    }
  }
  if(!g.json.resultCode){
    assert( NULL != entry );
    assert( NULL != zType );
    cson_object_set( pay, "type", json_new_string(zType) );
    cson_object_set( pay, "uuid", json_new_string(zUuid) );
    /*cson_object_set( pay, "name", json_new_string(zName ? zName : zUuid) );*/
    /*cson_object_set( pay, "rid", cson_value_new_integer(rid) );*/
    if(cson_value_is_object(entry) && (cson_value_get_object(entry) != pay)){
      cson_object_set(pay, "artifact", entry);
    }
  }
  veryend:
  blob_reset(&uuid);
  if(g.json.resultCode && pay){
    cson_free_object(pay);
    pay = NULL;
  }
  return cson_object_value(pay);
}

#endif /* FOSSIL_ENABLE_JSON */
