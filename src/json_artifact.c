#include "VERSION.h"
#include "config.h"
#include "json_artifact.h"

#if INTERFACE
#include "json_detail.h"
#endif

/*
** Internal callback for /json/artifact handlers. rid and uid refer to
** the rid/uid of a given type of artifact, and each callback is
** specialized to return a JSON form of one type of artifact.
*/
typedef cson_value * (*artifact_f)( int rid, char const * uid );

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
**
** TODO: consolidate the result structure (and its generation) with
** /json/timeline/ci.
*/
static cson_value * json_artifact_ci( int rid, char const * zUuid ){
  cson_value * v = cson_value_new_object();
  cson_object * o = cson_value_get_object(v);
  char const * zParent = NULL;
  Stmt q;
  assert( NULL != zUuid );
  cson_object_set(o,"isLeaf", cson_value_new_bool(is_a_leaf(rid)));
  zParent = db_text(0,
    "SELECT uuid FROM plink, blob"
    " WHERE plink.cid=%d AND blob.rid=plink.pid AND plink.isprim",
    rid
  );

  db_prepare(&q, 
             "SELECT uuid, mtime, user, comment,"
             "       omtime"
             "  FROM blob, event"
             " WHERE blob.rid=%d"
             "   AND event.objid=%d",
             rid, rid
             );
  if( db_step(&q)==SQLITE_ROW ){
    /*const char *zUuid = db_column_text(&q, 0);*/
    char * zTmp;
    const char *zUser;
    const char *zComment;
    char * zEUser, * zEComment;
    int mtime, omtime;
    cson_value * fileList = NULL;
#define SET(K,V) cson_object_set(o,(K), (V))
    SET("uuid",json_new_string(zUuid));
    zUser = db_column_text(&q,2);
    SET("user",json_new_string(zUser));
    zEUser = db_text(0,
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_USER, rid);
    if(zEUser){
      SET("editedBy", json_new_string(zEUser));
      free(zEUser);
    }

    zComment = db_column_text(&q,3);
    SET("comment",json_new_string(zComment));
    zEComment = db_text(0, 
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_COMMENT, rid);
    if(zEComment){
      SET("editedComment", json_new_string(zEComment));
      free(zEComment);
    }

    mtime = db_column_int(&q,1);
    SET("mtime",json_new_int(mtime));
    omtime = db_column_int(&q,4);
    if(omtime && (omtime!=mtime)){
      SET("omtime",json_new_int(omtime));
    }

    if(zParent){
      SET("parentUuid", json_new_string(zParent));
    }

    fileList = json_timeline_get_changed_files(rid);
    if(fileList){
      SET("files",fileList);
    }

#undef SET
  }else{
    cson_value_free(v);
    v = NULL;
  }
  db_finalize(&q);
  return v;
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
** Impl of /json/artifact
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
      goto end_ok;
    }
    if( db_exists("SELECT 1 FROM tag WHERE tagname GLOB 'event-%q*'", zName) ){
      zType = "tag";
      goto end_ok;
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
  zUuid = zName = blob_str(&uuid);
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid='%s'", zName);
  if(0==rid){
    g.json.resultCode = FSL_JSON_E_RESOURCE_NOT_FOUND;
    goto error;
  }
  if( db_exists("SELECT 1 FROM mlink WHERE mid=%d", rid)
      || db_exists("SELECT 1 FROM plink WHERE cid=%d", rid)
      || db_exists("SELECT 1 FROM plink WHERE pid=%d", rid)){
    zType = "checkin";
    goto end_ok;
  }else if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                      " WHERE rid=%d AND tagname LIKE 'wiki-%%'", rid) ){
    zType = "wiki";
    goto end_ok;
  }else if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                      " WHERE rid=%d AND tagname LIKE 'tkt-%%'", rid) ){
    zType = "ticket";
    goto end_ok;
  }else{
    g.json.resultCode = FSL_JSON_E_RESOURCE_NOT_FOUND;
    goto error;
  }

  error:
  assert( 0 != g.json.resultCode );
  goto veryend;

  end_ok:
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  assert( NULL != zType );
  cson_object_set( pay, "type", json_new_string(zType) );
  cson_object_set( pay, "id", json_new_string(zName) );
  cson_object_set( pay, "rid", cson_value_new_integer(rid) );
  ArtifactDispatchEntry const * disp = &ArtifactDispatchList[0];
  for( ; disp->name; ++disp ){
    if(0!=strcmp(disp->name, zType)){
      continue;
    }else{
      cson_value * entry;
      if( ! (*disp->permCheck)() ){
        break;
      }
      entry = (*disp->func)(rid, zUuid);
      if(entry){
        cson_object_set(pay, "artifact", entry);
      }
      break;
    }
  }
  veryend:
  blob_reset(&uuid);
  return payV;
}

