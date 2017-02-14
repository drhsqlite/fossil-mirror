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

#include "config.h"
#include "json_diff.h"

#if INTERFACE
#include "json_detail.h"
#endif



/*
** Generates a diff between two versions (zFrom and zTo), using nContext
** content lines in the output. On success, returns a new JSON String
** object. On error it sets g.json's error state and returns NULL.
**
** If fSbs is true (non-0) them side-by-side diffs are used.
**
** If fHtml is true then HTML markup is added to the diff.
*/
cson_value * json_generate_diff(const char *zFrom, const char *zTo,
                                int nContext, char fSbs,
                                char fHtml){
  int fromid;
  int toid;
  int outLen;
  Blob from = empty_blob, to = empty_blob, out = empty_blob;
  cson_value * rc = NULL;
  int flags = (DIFF_CONTEXT_MASK & nContext)
    | (fSbs ? DIFF_SIDEBYSIDE : 0)
    | (fHtml ? DIFF_HTML : 0);
  fromid = name_to_typed_rid(zFrom, "*");
  if(fromid<=0){
      json_set_err(FSL_JSON_E_UNRESOLVED_UUID,
                   "Could not resolve 'from' ID.");
      return NULL;
  }
  toid = name_to_typed_rid(zTo, "*");
  if(toid<=0){
      json_set_err(FSL_JSON_E_UNRESOLVED_UUID,
                   "Could not resolve 'to' ID.");
      return NULL;
  }
  content_get(fromid, &from);
  content_get(toid, &to);
  blob_zero(&out);
  text_diff(&from, &to, &out, 0, flags);
  blob_reset(&from);
  blob_reset(&to);
  outLen = blob_size(&out);
  if(outLen>=0){
    rc = cson_value_new_string(blob_buffer(&out),
                               (unsigned int)blob_size(&out));
  }
  blob_reset(&out);
  return rc;
}

/*
** Implementation of the /json/diff page.
**
** Arguments:
**
** v1=1st version to diff
** v2=2nd version to diff
**
** Can come from GET, POST.payload, CLI -v1/-v2 or as positional
** parameters following the command name (in HTTP and CLI modes).
**
*/
cson_value * json_page_diff(){
  cson_object * pay = NULL;
  cson_value * v = NULL;
  char const * zFrom;
  char const * zTo;
  int nContext = 0;
  char doSBS;
  char doHtml;
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
  doSBS = json_find_option_bool("sbs",NULL,"y",0);
  doHtml = json_find_option_bool("html",NULL,"h",0);
  v = json_generate_diff(zFrom, zTo, nContext, doSBS, doHtml);
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

#endif /* FOSSIL_ENABLE_JSON */
