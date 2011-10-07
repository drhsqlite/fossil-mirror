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

#include "config.h"
#include "json_diff.h"

#if INTERFACE
#include "json_detail.h"
#endif



/*
** Append the difference between two RIDs to the output
*/
cson_value * json_generate_diff(const char *zFrom, const char *zTo,
                                int nContext){
  int fromid;
  int toid;
  int outLen;
  Blob from = empty_blob, to = empty_blob, out = empty_blob;
  cson_value * rc = NULL;
  char const * zType = "ci";
  fromid = name_to_typed_rid(zFrom, "*");
  if(fromid<=0){
      json_set_err(FSL_JSON_E_UNRESOLVED_UUID,
                   "Could not resolve 'from' ID.");
      return NULL;
  }
  toid = name_to_typed_rid(zTo, "*");
  if(toid<=0){
      json_set_err(FSL_JSON_E_UNRESOLVED_UUID,
                   "Could not resolve 'from' ID.");
      return NULL;
  }
  content_get(fromid, &from);
  content_get(toid, &to);
  blob_zero(&out);
  text_diff(&from, &to, &out, nContext, 1);
  blob_reset(&from);
  blob_reset(&to);
  outLen = blob_size(&out);
  if(outLen>0){
    rc = cson_value_new_string(blob_buffer(&out), blob_size(&out));
  }
  blob_reset(&out);
  return rc;
}

/*
** Implementation of the /json/diff page.
**
*/
cson_value * json_page_diff(){
  cson_object * pay = NULL;
  cson_value * v = NULL;
  char const * zFrom;
  char const * zTo;
  int nContext = 0;
  if(!g.perm.Read){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'o' permissions.");
    return NULL;
  }
  zFrom = json_find_option_cstr("v1",NULL,NULL);
  if(!zFrom){
    zFrom = json_command_arg(2);
  }
  if(!zFrom){
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "Required 'v1' parameter is missing.");
    return NULL;
  }
  zTo = json_find_option_cstr("v2",NULL,NULL);
  if(!zTo){
    zTo = json_command_arg(3);
  }
  if(!zTo){
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "Required 'v2' parameter is missing.");
    return NULL;
  }
  nContext = json_find_option_int("context",NULL,"c",5);
  v = json_generate_diff(zFrom, zTo, nContext);
  if(!v){
    if(!g.json.resultCode){
      json_set_err(FSL_JSON_E_UNKNOWN,
                   "Generating diff failed for unknown reason.");
    }
    return NULL;
  }
  pay = cson_new_object();
  cson_object_set(pay, "from", json_new_string(zFrom));
  cson_object_set(pay, "to", json_new_string(zTo));
  cson_object_set(pay, "diff", v);
  v = 0;
  
  return pay ? cson_object_value(pay) : NULL;
}

