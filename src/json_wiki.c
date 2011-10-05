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
#include "json_wiki.h"

#if INTERFACE
#include "json_detail.h"
#endif

static cson_value * json_wiki_create();
static cson_value * json_wiki_get();
static cson_value * json_wiki_list();
static cson_value * json_wiki_save();

/*
** Mapping of /json/wiki/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Wiki[] = {
{"create", json_wiki_create, 1},
{"get", json_wiki_get, 0},
{"list", json_wiki_list, 0},
{"save", json_wiki_save, 1},
{"timeline", json_timeline_wiki,0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};


/*
** Implements the /json/wiki family of pages/commands.
**
*/
cson_value * json_page_wiki(){
  return json_page_dispatch_helper(&JsonPageDefs_Wiki[0]);
}


/*
** Loads the given wiki page and creates a JSON object representation
** of it. If the page is not found then NULL is returned. If doParse
** is true then the page content is HTML-ized using fossil's
** conventional wiki format, else it is not parsed.
**
** The returned value, if not NULL, is-a JSON Object owned by the
** caller.
*/
cson_value * json_get_wiki_page(char const * zPageName, char doParse){
  int rid;
  Manifest *pWiki = 0;
  char const * zBody = NULL;
  char const * zFormat = NULL;
  char * zUuid = NULL;
  Stmt q;
  db_prepare(&q,
             "SELECT x.rid, b.uuid FROM tag t, tagxref x, blob b"
             " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q' "
             " AND b.rid=x.rid"
             " ORDER BY x.mtime DESC LIMIT 1",
             zPageName 
             );
  if( (SQLITE_ROW != db_step(&q)) ){
    return NULL;
  }
  rid = db_column_int(&q,0);
  zUuid = db_column_malloc(&q,1);
  db_finalize(&q);
  if( (pWiki = manifest_get(rid, CFTYPE_WIKI))!=0 ){
    zBody = pWiki->zWiki;
  }

  {
    unsigned int len;
    cson_value * payV = cson_value_new_object();
    cson_object * pay = cson_value_get_object(payV);
    cson_object_set(pay,"name",json_new_string(zPageName));
    cson_object_set(pay,"uuid",json_new_string(zUuid));
    free(zUuid);
    zUuid = NULL;
    cson_object_set(pay,"rid",json_new_int((cson_int_t)rid));
    cson_object_set(pay,"lastSavedBy",json_new_string(pWiki->zUser));
    cson_object_set(pay,FossilJsonKeys.timestamp, json_julian_to_timestamp(pWiki->rDate));
    cson_object_set(pay,"contentFormat",json_new_string(zFormat));
    if( doParse ){
      Blob content = empty_blob;
      Blob raw = empty_blob;
      blob_append(&raw,zBody,-1);
      wiki_convert(&raw,&content,0);
      len = strlen(zBody);
      len = (unsigned int)blob_size(&content);
      cson_object_set(pay,"contentLength",json_new_int((cson_int_t)len));
      cson_object_set(pay,"content",
                      cson_value_new_string(blob_buffer(&content),len));
      blob_reset(&content);
      blob_reset(&raw);
    }else{
      len = zBody ? strlen(zBody) : 0;
      cson_object_set(pay,"contentLength",json_new_int((cson_int_t)len));
      cson_object_set(pay,"content",cson_value_new_string(zBody,len));
    }
    /*TODO: add 'T' (tag) fields*/
    /*TODO: add the 'A' card (file attachment) entries?*/
    manifest_destroy(pWiki);
    return payV;
  }
}

/*
** Implementation of /json/wiki/get.
**
*/
static cson_value * json_wiki_get(){
  int rid;
  Manifest *pWiki = 0;
  char const * zBody = NULL;
  char const * zPageName;
  char const * zFormat = NULL;
  char * zUuid = NULL;
  Stmt q;
  if( !g.perm.RdWiki && !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'o' or 'j' access.");
    return NULL;
  }
  if(fossil_is_json()){
    zPageName = json_getenv_cstr("page");
  }
  zPageName = json_find_option_cstr("page",NULL,"p");
  if( !zPageName && cson_value_is_string(g.json.reqPayload.v) ){
      zPageName = cson_string_cstr(cson_value_get_string(g.json.reqPayload.v));
  }
  if(!zPageName){
    zPageName = json_command_arg(g.json.dispatchDepth+1);
  }
  if(!zPageName||!*zPageName){
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "'page' argument is missing.");
    return NULL;
  }

  zFormat = json_find_option_cstr("format",NULL,"f");
  if(!zFormat || !*zFormat){
    zFormat = "raw";
  }
  if( 'r' != *zFormat ){
    zFormat = "html";
  }
  return json_get_wiki_page(zPageName, 'h'==*zFormat);
}

/*
** Internal impl of /wiki/save and /wiki/create. If createMode is 0
** and the page already exists then a
** FSL_JSON_E_RESOURCE_ALREADY_EXISTS error is triggered.  If
** createMode is false then the FSL_JSON_E_RESOURCE_NOT_FOUND is
** triggered if the page does not already exists.
**
** Note that the error triggered when createMode==0 and no such page
** exists is rather arbitrary - we could just as well create the entry
** here if it doesn't already exist. With that, save/create would
** become one operation. That said, i expect there are people who
** would categorize such behaviour as "being too clever" or "doing too
** much automatically" (and i would likely agree with them).
**
** If allowCreateIfExists is true then this function will allow a new
** page to be created even if createMode is false.
*/
static cson_value * json_wiki_create_or_save(char createMode,
                                             char allowCreateIfExists){
  Blob content = empty_blob;
  cson_value * nameV;
  cson_value * contentV;
  cson_value * emptyContent = NULL;
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  cson_string const * jstr = NULL;
  char const * zContent;
  char const * zBody = NULL;
  char const * zPageName;
  unsigned int contentLen = 0;
  int rid;
  if( (createMode && !g.perm.NewWiki)
      || (!createMode && !g.perm.WrWiki)){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires '%c' permissions.",
                 (createMode ? 'f' : 'k'));
    return NULL;
  }
  nameV = json_req_payload_get("name");
  if(!nameV){
    json_set_err( FSL_JSON_E_MISSING_ARGS,
                  "'name' parameter is missing.");
    return NULL;
  }
  zPageName = cson_string_cstr(cson_value_get_string(nameV));
  rid = db_int(0,
     "SELECT x.rid FROM tag t, tagxref x"
     " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q'"
     " ORDER BY x.mtime DESC LIMIT 1",
     zPageName
  );

  if(rid){
    if(createMode){
      json_set_err(FSL_JSON_E_RESOURCE_ALREADY_EXISTS,
                   "Wiki page '%s' already exists.",
                   zPageName);
      goto error;
    }
  }else if(!allowCreateIfExists){
    json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,
                 "Wiki page '%s' not found.",
                 zPageName);
    goto error;
  }

  contentV = json_req_payload_get("content");
  if( !contentV ){
    if( createMode || (!rid && allowCreateIfExists) ){
      contentV = emptyContent = cson_value_new_string("",0);
    }else{
      json_set_err(FSL_JSON_E_MISSING_ARGS,
                   "'content' parameter is missing.");
      goto error;
    }
  }
  if( !cson_value_is_string(nameV)
      || !cson_value_is_string(contentV)){
    json_set_err(FSL_JSON_E_INVALID_ARGS,
                 "'name' and 'content' parameters must be strings.");
    goto error;
  }
  jstr = cson_value_get_string(contentV);
  contentLen = (int)cson_string_length_bytes(jstr);
  if(contentLen){
    blob_append(&content, cson_string_cstr(jstr),contentLen);
  }
  wiki_cmd_commit(zPageName, 0==rid, &content);
  blob_reset(&content);

  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  cson_object_set( pay, "name", nameV );
  cson_object_set( pay, FossilJsonKeys.timestamp,
                   json_new_timestamp(-1) );

  goto ok;
  error:
  assert( 0 != g.json.resultCode );
  cson_value_free(payV);
  payV = NULL;
  ok:
  if( emptyContent ){
    /* We have some potentially tricky memory ownership
       here, which is why we handle emptyContent separately.

       This is, in fact, overkill because cson_value_new_string("",0)
       actually returns a shared singleton instance (i.e. doesn't
       allocate), but that is a cson implementation detail which i
       don't want leaking into this code...
    */
    cson_value_free(emptyContent);
  }
  return payV;

}

/*
** Implementation of /json/wiki/create.
*/
static cson_value * json_wiki_create(){
  return json_wiki_create_or_save(1,0);
}

/*
** Implementation of /json/wiki/save.
*/
static cson_value * json_wiki_save(){
  char const createIfNotExists = json_getenv_bool("createIfNotExists",0);
  return json_wiki_create_or_save(0,createIfNotExists);
}

/*
** Implementation of /json/wiki/list.
*/
static cson_value * json_wiki_list(){
  cson_value * listV = NULL;
  cson_array * list = NULL;
  Stmt q;
  if( !g.perm.RdWiki && !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'j' or 'o' permissions.");
    return NULL;
  }
  db_prepare(&q,"SELECT"
             " substr(tagname,6) as name"
             " FROM tag WHERE tagname GLOB 'wiki-*'"
             " ORDER BY lower(name)");
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  while( SQLITE_ROW == db_step(&q) ){
    cson_value * v = cson_sqlite3_column_to_value(q.pStmt,0);
    if(!v){
      json_set_err(FSL_JSON_E_UNKNOWN,
                   "Could not convert wiki name column to JSON.");
      goto error;
    }else if( 0 != cson_array_append( list, v ) ){
      cson_value_free(v);
      json_set_err(FSL_JSON_E_ALLOC,"Could not append wiki page name to array.")
        /* OOM (or maybe numeric overflow) are the only realistic
           error codes for that particular failure.*/;
      goto error;
    }
  }
  goto end;
  error:
  assert(0 != g.json.resultCode);
  cson_value_free(listV);
  listV = NULL;
  json_set_err(FSL_JSON_E_UNKNOWN,
               "Error creating wiki page list.");
  end:
  db_finalize(&q);
  return listV;
}
