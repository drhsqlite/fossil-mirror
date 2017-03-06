#ifdef FOSSIL_ENABLE_JSON
/*
** Copyright (c) 2011-12 D. Richard Hipp
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
static cson_value * json_wiki_preview();
static cson_value * json_wiki_save();
static cson_value * json_wiki_diff();
/*
** Mapping of /json/wiki/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Wiki[] = {
{"create", json_wiki_create, 0},
{"diff", json_wiki_diff, 0},
{"get", json_wiki_get, 0},
{"list", json_wiki_list, 0},
{"preview", json_wiki_preview, 0},
{"save", json_wiki_save, 0},
{"timeline", json_timeline_wiki,0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};


/*
** Implements the /json/wiki family of pages/commands.
**
*/
cson_value * json_page_wiki(){
  return json_page_dispatch_helper(JsonPageDefs_Wiki);
}

/*
** Returns the UUID for the given wiki blob RID, or NULL if not
** found. The returned string is allocated via db_text() and must be
** free()d by the caller.
*/
char * json_wiki_get_uuid_for_rid( int rid )
{
  return db_text(NULL,
             "SELECT b.uuid FROM tag t, tagxref x, blob b"
             " WHERE x.tagid=t.tagid AND t.tagname GLOB 'wiki-*' "
             " AND b.rid=x.rid AND b.rid=%d"
             " ORDER BY x.mtime DESC LIMIT 1",
             rid
             );
}

/*
** Tries to load a wiki page from the given rid creates a JSON object
** representation of it. If the page is not found then NULL is
** returned. If contentFormat is positive then the page content
** is HTML-ized using fossil's conventional wiki format, if it is
** negative then no parsing is performed, if it is 0 then the content
** is not returned in the response. If contentFormat is 0 then the
** contentSize reflects the number of bytes, not characters, stored in
** the page.
**
** The returned value, if not NULL, is-a JSON Object owned by the
** caller. If it returns NULL then it may set g.json's error state.
*/
cson_value * json_get_wiki_page_by_rid(int rid, int contentFormat){
  Manifest * pWiki = NULL;
  if( NULL == (pWiki = manifest_get(rid, CFTYPE_WIKI, 0)) ){
    json_set_err( FSL_JSON_E_UNKNOWN,
                  "Error reading wiki page from manifest (rid=%d).",
                  rid );
    return NULL;
  }else{
    unsigned int len = 0;
    cson_object * pay = cson_new_object();
    char const * zBody = pWiki->zWiki;
    char const * zFormat = NULL;
    char * zUuid = json_wiki_get_uuid_for_rid(rid);
    cson_object_set(pay,"name",json_new_string(pWiki->zWikiTitle));
    cson_object_set(pay,"uuid",json_new_string(zUuid));
    free(zUuid);
    zUuid = NULL;
    if( pWiki->nParent > 0 ){
      cson_object_set( pay, "parent", json_new_string(pWiki->azParent[0]) )
        /* Reminder: wiki pages do not branch and have only one parent
           (except for the initial version, which has no parents). */;
    }
    /*cson_object_set(pay,"rid",json_new_int((cson_int_t)rid));*/
    cson_object_set(pay,"user",json_new_string(pWiki->zUser));
    cson_object_set(pay,FossilJsonKeys.timestamp,
                    json_julian_to_timestamp(pWiki->rDate));
    if(0 == contentFormat){
      cson_object_set(pay,"size",
                      json_new_int((cson_int_t)(zBody?strlen(zBody):0)));
    }else{
      if( contentFormat>0 ){/*HTML-ize it*/
        Blob content = empty_blob;
        Blob raw = empty_blob;
        zFormat = "html";
        if(zBody && *zBody){
          const char *zMimetype = pWiki->zMimetype;
          if( zMimetype==0 ) zMimetype = "text/x-fossil-wiki";
          zMimetype = wiki_filter_mimetypes(zMimetype);
          blob_append(&raw,zBody,-1);
          if( fossil_strcmp(zMimetype, "text/x-fossil-wiki")==0 ){
            wiki_convert(&raw,&content,0);
          }else if( fossil_strcmp(zMimetype, "text/x-markdown")==0 ){
            markdown_to_html(&raw,0,&content);
          }else if( fossil_strcmp(zMimetype, "text/plain")==0 ){
            htmlize_to_blob(&content,blob_str(&raw),blob_size(&raw));
          }else{
            json_set_err( FSL_JSON_E_UNKNOWN,
                          "Unsupported MIME type '%s' for wiki page '%s'.",
                          zMimetype, pWiki->zWikiTitle );
            blob_reset(&content);
            blob_reset(&raw);
            cson_free_object(pay);
            manifest_destroy(pWiki);
            return NULL;
          }
          len = (unsigned int)blob_size(&content);
        }
        cson_object_set(pay,"size",json_new_int((cson_int_t)len));
        cson_object_set(pay,"content",
                        cson_value_new_string(blob_buffer(&content),len));
        blob_reset(&content);
        blob_reset(&raw);
      }else{/*raw format*/
        zFormat = "raw";
        len = zBody ? strlen(zBody) : 0;
        cson_object_set(pay,"size",json_new_int((cson_int_t)len));
        cson_object_set(pay,"content",cson_value_new_string(zBody,len));
      }
      cson_object_set(pay,"contentFormat",json_new_string(zFormat));

    }
    /*TODO: add 'T' (tag) fields*/
    /*TODO: add the 'A' card (file attachment) entries?*/
    manifest_destroy(pWiki);
    return cson_object_value(pay);
  }
}

/*
** Searches for the latest version of a wiki page with the given
** name. If found it behaves like json_get_wiki_page_by_rid(theRid,
** contentFormat), else it returns NULL.
*/
cson_value * json_get_wiki_page_by_name(char const * zPageName, int contentFormat){
  int rid;
  rid = db_int(0,
               "SELECT x.rid FROM tag t, tagxref x, blob b"
               " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q' "
               " AND b.rid=x.rid"
               " ORDER BY x.mtime DESC LIMIT 1",
               zPageName
             );
  if( 0==rid ){
    json_set_err( FSL_JSON_E_RESOURCE_NOT_FOUND, "Wiki page not found: %s",
                  zPageName );
    return NULL;
  }
  return json_get_wiki_page_by_rid(rid, contentFormat);
}


/*
** Searches json_find_option_ctr("format",NULL,"f") for a flag.
** If not found it returns defaultValue else it returns a value
** depending on the first character of the format option:
**
** [h]tml = 1
** [n]one = 0
** [r]aw = -1
**
** The return value is intended for use with
** json_get_wiki_page_by_rid() and friends.
*/
int json_wiki_get_content_format_flag( int defaultValue ){
  int contentFormat = defaultValue;
  char const * zFormat = json_find_option_cstr("format",NULL,"f");
  if( !zFormat || !*zFormat ){
    return contentFormat;
  }
  else if('r'==*zFormat){
    contentFormat = -1;
  }
  else if('h'==*zFormat){
    contentFormat = 1;
  }
  else if('n'==*zFormat){
    contentFormat = 0;
  }
  return contentFormat;
}

/*
** Helper for /json/wiki/get and /json/wiki/preview. At least one of
** zPageName (wiki page name) or zSymname must be set to a
** non-empty/non-NULL value. zSymname takes precedence.  On success
** the result of one of json_get_wiki_page_by_rid() or
** json_get_wiki_page_by_name() will be returned (owned by the
** caller). On error g.json's error state is set and NULL is returned.
*/
static cson_value * json_wiki_get_by_name_or_symname(char const * zPageName,
                                                     char const * zSymname,
                                                     int contentFormat ){
  if(!zSymname || !*zSymname){
    return json_get_wiki_page_by_name(zPageName, contentFormat);
  }else{
    int rid = symbolic_name_to_rid( zSymname ? zSymname : zPageName, "w" );
    if(rid<0){
      json_set_err(FSL_JSON_E_AMBIGUOUS_UUID,
                   "UUID [%s] is ambiguous.", zSymname);
      return NULL;
    }else if(rid==0){
      json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,
                   "UUID [%s] does not resolve to a wiki page.", zSymname);
      return NULL;
    }else{
      return json_get_wiki_page_by_rid(rid, contentFormat);
    }
  }
}

/*
** Implementation of /json/wiki/get.
**
*/
static cson_value * json_wiki_get(){
  char const * zPageName;
  char const * zSymName = NULL;
  int contentFormat = -1;
  if( !g.perm.RdWiki && !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'o' or 'j' access.");
    return NULL;
  }
  zPageName = json_find_option_cstr2("name",NULL,"n",g.json.dispatchDepth+1);

  zSymName = json_find_option_cstr("uuid",NULL,"u");

  if((!zPageName||!*zPageName) && (!zSymName || !*zSymName)){
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "At least one of the 'name' or 'uuid' arguments must be provided.");
    return NULL;
  }

  /* TODO: see if we have a page named zPageName. If not, try to resolve
     zPageName as a UUID.
  */

  contentFormat = json_wiki_get_content_format_flag(contentFormat);
  return json_wiki_get_by_name_or_symname( zPageName, zSymName, contentFormat );
}

/*
** Implementation of /json/wiki/preview.
**
*/
static cson_value * json_wiki_preview(){
  char const * zContent = NULL;
  cson_value * pay = NULL;
  Blob contentOrig = empty_blob;
  Blob contentHtml = empty_blob;
  if( !g.perm.WrWiki ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'k' access.");
    return NULL;
  }
  zContent = cson_string_cstr(cson_value_get_string(g.json.reqPayload.v));
  if(!zContent) {
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "The 'payload' property must be a string containing the wiki code to preview.");
    return NULL;
  }
  blob_append( &contentOrig, zContent, (int)cson_string_length_bytes(cson_value_get_string(g.json.reqPayload.v)) );
  wiki_convert( &contentOrig, &contentHtml, 0 );
  blob_reset( &contentOrig );
  pay = cson_value_new_string( blob_str(&contentHtml), (unsigned int)blob_size(&contentHtml));
  blob_reset( &contentHtml );
  return pay;
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
** If allowCreateIfNotExists is true then this function will allow a new
** page to be created even if createMode is false.
*/
static cson_value * json_wiki_create_or_save(char createMode,
                                             char allowCreateIfNotExists){
  Blob content = empty_blob;  /* wiki  page content */
  cson_value * nameV;         /* wiki page name */
  char const * zPageName;     /* cstr form of page name */
  cson_value * contentV;      /* passed-in content */
  cson_value * emptyContent = NULL;  /* placeholder for empty content. */
  cson_value * payV = NULL;   /* payload/return value */
  cson_string const * jstr = NULL;  /* temp for cson_value-to-cson_string conversions. */
  char const * zMimeType = 0;
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
  if(!zPageName || !*zPageName){
    json_set_err(FSL_JSON_E_INVALID_ARGS,
                 "'name' parameter must be a non-empty string.");
    return NULL;
  }
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
  }else if(!createMode && !allowCreateIfNotExists){
    json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,
                 "Wiki page '%s' not found.",
                 zPageName);
    goto error;
  }

  contentV = json_req_payload_get("content");
  if( !contentV ){
    if( createMode || (!rid && allowCreateIfNotExists) ){
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
                 "'content' parameter must be a string.");
    goto error;
  }
  jstr = cson_value_get_string(contentV);
  contentLen = (int)cson_string_length_bytes(jstr);
  if(contentLen){
    blob_append(&content, cson_string_cstr(jstr),contentLen);
  }

  zMimeType = json_find_option_cstr("mimetype","mimetype","M");
  zMimeType = wiki_filter_mimetypes(zMimeType);

  wiki_cmd_commit(zPageName, rid, &content, zMimeType, 0);
  blob_reset(&content);
  /*
    Our return value here has a race condition: if this operation
    is called concurrently for the same wiki page via two requests,
    payV could reflect the results of the other save operation.
  */
  payV = json_get_wiki_page_by_name(
           cson_string_cstr(
             cson_value_get_string(nameV)),
             0);
  goto ok;
  error:
  assert( 0 != g.json.resultCode );
  assert( NULL == payV );
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
  char const * zGlob = NULL;
  Stmt q = empty_Stmt;
  Blob sql = empty_blob;
  char const verbose = json_find_option_bool("verbose",NULL,"v",0);
  char fInvert = json_find_option_bool("invert",NULL,"i",0);;

  if( !g.perm.RdWiki && !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'j' or 'o' permissions.");
    return NULL;
  }
  blob_append(&sql,"SELECT"
              " substr(tagname,6) as name"
              " FROM tag WHERE tagname GLOB 'wiki-*'",
              -1);
  zGlob = json_find_option_cstr("glob",NULL,"g");
  if(zGlob && *zGlob){
    blob_append_sql(&sql," AND name %s GLOB %Q",
                    fInvert ? "NOT" : "", zGlob);
  }else{
    zGlob = json_find_option_cstr("like",NULL,"l");
    if(zGlob && *zGlob){
      blob_append_sql(&sql," AND name %s LIKE %Q",
                      fInvert ? "NOT" : "", zGlob);
    }
  }
  blob_append(&sql," ORDER BY lower(name)", -1);
  db_prepare(&q,"%s", blob_sql_text(&sql));
  blob_reset(&sql);
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  while( SQLITE_ROW == db_step(&q) ){
    cson_value * v;
    if( verbose ){
      char const * name = db_column_text(&q,0);
      v = json_get_wiki_page_by_name(name,0);
    }else{
      v = cson_sqlite3_column_to_value(q.pStmt,0);
    }
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
  end:
  db_finalize(&q);
  return listV;
}

static cson_value * json_wiki_diff(){
  char const * zV1 = NULL;
  char const * zV2 = NULL;
  cson_object * pay = NULL;
  int argPos = g.json.dispatchDepth;
  int r1 = 0, r2 = 0;
  Manifest * pW1 = NULL, *pW2 = NULL;
  Blob w1 = empty_blob, w2 = empty_blob, d = empty_blob;
  char const * zErrTag = NULL;
  u64 diffFlags;
  char * zUuid = NULL;
  if( !g.perm.Hyperlink ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'h' permissions.");
    return NULL;
  }


  zV1 = json_find_option_cstr2( "v1",NULL, NULL, ++argPos );
  zV2 = json_find_option_cstr2( "v2",NULL, NULL, ++argPos );
  if(!zV1 || !*zV1 || !zV2 || !*zV2) {
    json_set_err(FSL_JSON_E_INVALID_ARGS,
                 "Requires both 'v1' and 'v2' arguments.");
    return NULL;
  }

  r1 = symbolic_name_to_rid( zV1, "w" );
  zErrTag = zV1;
  if(r1<0){
    goto ambiguous;
  }else if(0==r1){
    goto invalid;
  }

  r2 = symbolic_name_to_rid( zV2, "w" );
  zErrTag = zV2;
  if(r2<0){
    goto ambiguous;
  }else if(0==r2){
    goto invalid;
  }

  zErrTag = zV1;
  pW1 = manifest_get(r1, CFTYPE_WIKI, 0);
  if( pW1==0 ) {
    goto manifest;
  }
  zErrTag = zV2;
  pW2 = manifest_get(r2, CFTYPE_WIKI, 0);
  if( pW2==0 ) {
    goto manifest;
  }

  blob_init(&w1, pW1->zWiki, -1);
  blob_zero(&w2);
  blob_init(&w2, pW2->zWiki, -1);
  blob_zero(&d);
  diffFlags = DIFF_IGNORE_EOLWS | DIFF_STRIP_EOLCR;
  text_diff(&w1, &w2, &d, 0, diffFlags);
  blob_reset(&w1);
  blob_reset(&w2);

  pay = cson_new_object();

  zUuid = json_wiki_get_uuid_for_rid( pW1->rid );
  cson_object_set(pay, "v1", json_new_string(zUuid) );
  free(zUuid);
  zUuid = json_wiki_get_uuid_for_rid( pW2->rid );
  cson_object_set(pay, "v2", json_new_string(zUuid) );
  free(zUuid);
  zUuid = NULL;

  manifest_destroy(pW1);
  manifest_destroy(pW2);

  cson_object_set(pay, "diff",
                  cson_value_new_string( blob_str(&d),
                                         (unsigned int)blob_size(&d)));

  return cson_object_value(pay);

  manifest:
  json_set_err(FSL_JSON_E_UNKNOWN,
               "Could not load wiki manifest for UUID [%s].",
               zErrTag);
  goto end;

  ambiguous:
  json_set_err(FSL_JSON_E_AMBIGUOUS_UUID,
               "UUID [%s] is ambiguous.", zErrTag);
  goto end;

  invalid:
  json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,
               "UUID [%s] not found.", zErrTag);
  goto end;

  end:
  cson_free_object(pay);
  return NULL;
}

#endif /* FOSSIL_ENABLE_JSON */
