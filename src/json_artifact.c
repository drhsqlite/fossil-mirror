#include "VERSION.h"
#include "config.h"
#include "json_artifact.h"

#if INTERFACE
#include "json_detail.h"
#endif

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
  zName = blob_str(&uuid);
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

  veryend:
  blob_reset(&uuid);
  return payV;
}

