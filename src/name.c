/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code used to convert user-supplied object names into
** canonical UUIDs.
**
** A user-supplied object name is any unique prefix of a valid UUID but
** not necessarily in canonical form.  
*/
#include "config.h"
#include "name.h"
#include <assert.h>

/*
** This routine takes a user-entered UUID which might be in mixed
** case and might only be a prefix of the full UUID and converts it
** into the full-length UUID in canonical form.
**
** If the input is not a UUID or a UUID prefix, then try to resolve
** the name as a tag.  If multiple tags match, pick the latest.
** If the input name matches "tag:*" then always resolve as a tag.
**
** If the input is not a tag, then try to match it as an ISO-8601 date
** string YYYY-MM-DD HH:MM:SS and pick the nearest check-in to that date.
** If the input is of the form "date:*" or "localtime:*" or "utc:*" then
** always resolve the name as a date.
**
** Return 0 on success.  Return 1 if the name cannot be resolved.
** Return 2 name is ambiguous.
*/
int name_to_uuid(Blob *pName, int iErrPriority, const char *zType){
  int rc;
  int sz;
  sz = blob_size(pName);
  if( sz>UUID_SIZE || sz<4 || !validate16(blob_buffer(pName), sz) ){
    char *zUuid;
    const char *zName = blob_str(pName);
    if( memcmp(zName, "tag:", 4)==0 ){
      zName += 4;
      zUuid = tag_to_uuid(zName, zType);
    }else{
      zUuid = tag_to_uuid(zName, zType);
      if( zUuid==0 ){
        zUuid = date_to_uuid(zName, zType);
      }
    }
    if( zUuid ){
      blob_reset(pName);
      blob_append(pName, zUuid, -1);
      free(zUuid);
      return 0;
    }
    fossil_error(iErrPriority, "not a valid object name: %s", zName);
    return 1;
  }
  blob_materialize(pName);
  canonical16(blob_buffer(pName), sz);
  if( sz==UUID_SIZE ){
    rc = db_int(1, "SELECT 0 FROM blob WHERE uuid=%B", pName);
    if( rc ){
      fossil_error(iErrPriority, "no such artifact: %b", pName);
      blob_reset(pName);
    }
  }else if( sz<UUID_SIZE && sz>=4 ){
    Stmt q;
    db_prepare(&q, "SELECT uuid FROM blob WHERE uuid GLOB '%b*'", pName);
    if( db_step(&q)!=SQLITE_ROW ){
      char *zUuid;
      db_finalize(&q);
      zUuid = tag_to_uuid(blob_str(pName), "*");
      if( zUuid ){
        blob_reset(pName);
        blob_append(pName, zUuid, -1);
        free(zUuid);
        return 0;
      }
      fossil_error(iErrPriority, "no artifacts match the prefix \"%b\"", pName);
      return 1;
    }
    blob_reset(pName);
    blob_append(pName, db_column_text(&q, 0), db_column_bytes(&q, 0));
    if( db_step(&q)==SQLITE_ROW ){
      fossil_error(iErrPriority, 
         "multiple artifacts match"
      );
      blob_reset(pName);
      db_finalize(&q);
      return 2;
    }
    db_finalize(&q);
    rc = 0;
  }else{
    rc = 0;
  }
  return rc;
}

/*
** Return TRUE if the string begins with an ISO8601 date: YYYY-MM-DD.
*/
static int is_date(const char *z){
  if( !fossil_isdigit(z[0]) ) return 0;
  if( !fossil_isdigit(z[1]) ) return 0;
  if( !fossil_isdigit(z[2]) ) return 0;
  if( !fossil_isdigit(z[3]) ) return 0;
  if( z[4]!='-') return 0;
  if( !fossil_isdigit(z[5]) ) return 0;
  if( !fossil_isdigit(z[6]) ) return 0;
  if( z[7]!='-') return 0;
  if( !fossil_isdigit(z[8]) ) return 0;
  if( !fossil_isdigit(z[9]) ) return 0;
  return 1;
}

/*
** Convert a symbolic tag name into the UUID of a check-in that contains
** that tag.  If the tag appears on multiple check-ins, return the UUID
** of the most recent check-in with the tag.
**
** If the input string is of the form:
**
**      tag:date
**
** Then return the UUID of the oldest check-in with that tag that is
** not older than 'date'.
**
** An input of "tip" returns the most recent check-in.
**
** Memory to hold the returned string comes from malloc() and needs to
** be freed by the caller.
*/
char *tag_to_uuid(const char *zTag, const char *zType){
  int vid;
  char *zUuid;

  if( zType==0 || zType[0]==0 ) zType = "*";
  zUuid = db_text(0,
       "SELECT blob.uuid"
       "  FROM tag, tagxref, event, blob"
       " WHERE tag.tagname='sym-%q' "
       "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype>0 "
       "   AND event.objid=tagxref.rid "
       "   AND blob.rid=event.objid "
       "   AND event.type GLOB '%q'"
       " ORDER BY event.mtime DESC /*sort*/",
       zTag, zType
    );
  if( zUuid==0 ){
    int nTag = strlen(zTag);
    int i;
    for(i=0; i<nTag-10; i++){
      if( zTag[i]==':' && is_date(&zTag[i+1]) ){
        char *zDate = mprintf("%s", &zTag[i+1]);
        char *zTagBase = mprintf("%.*s", i, zTag);
        int nDate = strlen(zDate);
        int useUtc = 0;
        if( sqlite3_strnicmp(&zDate[nDate-3],"utc",3)==0 ){
          nDate -= 3;
          zDate[nDate] = 0;
          useUtc = 1;
        }
        zUuid = db_text(0,
          "SELECT blob.uuid"
          "  FROM tag, tagxref, event, blob"
          " WHERE tag.tagname='sym-%q' "
          "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype>0 "
          "   AND event.objid=tagxref.rid "
          "   AND blob.rid=event.objid "
          "   AND event.mtime<=julianday(%Q %s)"
          "   AND event.type GLOB '%q'"
          " ORDER BY event.mtime DESC /*sort*/ ",
          zTagBase, zDate, (useUtc ? "" : ",'utc'"), zType
        );
        break;
      }
    }
    if( zUuid==0 && fossil_strcmp(zTag, "tip")==0 ){
      zUuid = db_text(0,
        "SELECT blob.uuid"
        "  FROM event, blob"
        " WHERE event.type='ci'"
        "   AND blob.rid=event.objid"
        " ORDER BY event.mtime DESC"
      );
    }
    if( zUuid==0 && g.localOpen && (vid=db_lget_int("checkout",0))!=0 ){
      if( fossil_strcmp(zTag, "current")==0 ){
        zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
      }else if( fossil_strcmp(zTag, "prev")==0 
                || fossil_strcmp(zTag, "previous")==0 ){
        zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid="
                           "(SELECT pid FROM plink WHERE cid=%d AND isprim)",
                           vid);
      }else if( fossil_strcmp(zTag, "next")==0 ){
        zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid="
                           "(SELECT cid FROM plink WHERE pid=%d"
                           "  ORDER BY isprim DESC, mtime DESC)",
                           vid);
      }
    }
  }
  return zUuid;
}

/*
** Convert a date/time string into a UUID.
**
** Input forms accepted:
**
**    date:DATE
**    local:DATE
**    utc:DATE
**
** The DATE is interpreted as localtime unless the "utc:" prefix is used
** or a "utc" string appears at the end of the DATE string.
*/
char *date_to_uuid(const char *zDate, const char *zType){
  int useUtc = 0;
  int n;
  char *zCopy = 0;
  char *zUuid;

  if( memcmp(zDate, "date:", 5)==0 ){
    zDate += 5;
  }else if( memcmp(zDate, "local:", 6)==0 ){
    zDate += 6;
  }else if( memcmp(zDate, "utc:", 4)==0 ){
    zDate += 4;
    useUtc = 1;
  }
  n = strlen(zDate);
  if( n<10 || !is_date(zDate) ) return 0;
  if( n>4 && sqlite3_strnicmp(&zDate[n-3], "utc", 3)==0 ){
    zCopy = mprintf("%s", zDate);
    zCopy[n-3] = 0;
    zDate = zCopy;
    n -= 3;
    useUtc = 1;
  }
  if( zType==0 || zType[0]==0 ) zType = "*";
  zUuid = db_text(0,
    "SELECT (SELECT uuid FROM blob WHERE rid=event.objid)"
    "  FROM event"
    " WHERE mtime<=julianday(%Q %s) AND type GLOB '%q'"
    " ORDER BY mtime DESC LIMIT 1",
    zDate, useUtc ? "" : ",'utc'", zType
  );
  free(zCopy);
  return zUuid;
}

/*
** COMMAND:  test-name-to-id
**
** Convert a name to a full artifact ID.
*/
void test_name_to_id(void){
  int i;
  Blob name;
  db_must_be_within_tree();
  for(i=2; i<g.argc; i++){
    blob_init(&name, g.argv[i], -1);
    fossil_print("%s -> ", g.argv[i]);
    if( name_to_uuid(&name, 1, "*") ){
      fossil_print("ERROR: %s\n", g.zErrMsg);
      fossil_error_reset();
    }else{
      fossil_print("%s\n", blob_buffer(&name));
    }
    blob_reset(&name);
  }
}

/*
** Convert a name to a rid.  If the name is a small integer value then
** just use atoi() to do the conversion.  If the name contains alphabetic
** characters or is not an existing rid, then use name_to_uuid then
** convert the uuid to a rid.
**
** This routine is used by command-line routines to resolve command-line inputs
** into a rid.
*/
int name_to_typed_rid(const char *zName, const char *zType){
  int i;
  int rid;
  Blob name;

  if( zName==0 || zName[0]==0 ) return 0;
  blob_init(&name, zName, -1);
  if( name_to_uuid(&name, -1, zType) ){
    blob_reset(&name);
    for(i=0; zName[i] && fossil_isdigit(zName[i]); i++){}
    if( zName[i]==0 ){
      rid = atoi(zName);
      if( db_exists("SELECT 1 FROM blob WHERE rid=%d", rid) ){
        return rid;
      }
    }
    fossil_error(1, "no such artifact: %s", zName);
    return 0;
  }else{
    rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &name);
    blob_reset(&name);
  }
  return rid;
}
int name_to_rid(const char *zName){
  return name_to_typed_rid(zName, "*");
}

/*
** WEBPAGE: ambiguous
** URL: /ambiguous?name=UUID&src=WEBPAGE
** 
** The UUID given by the name paramager is ambiguous.  Display a page
** that shows all possible choices and let the user select between them.
*/
void ambiguous_page(void){
  Stmt q;
  const char *zName = P("name");  
  const char *zSrc = P("src");
  char *z;
  
  if( zName==0 || zName[0]==0 || zSrc==0 || zSrc[0]==0 ){
    fossil_redirect_home();
  }
  style_header("Ambiguous Artifact ID");
  @ <p>The artifact id <b>%h(zName)</b> is ambiguous and might
  @ mean any of the following:
  @ <ol>
  z = mprintf("%s", zName);
  canonical16(z, strlen(z));
  db_prepare(&q, "SELECT uuid, rid FROM blob WHERE uuid GLOB '%q*'", z);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    @ <li><p><a href="%s(g.zTop)/%T(zSrc)/%S(zUuid)">
    @ %S(zUuid)</a> -
    object_description(rid, 0, 0);
    @ </p></li>
  }
  @ </ol>
  style_footer();
}

/*
** Convert the name in CGI parameter zParamName into a rid and return that
** rid.  If the CGI parameter is missing or is not a valid artifact tag,
** return 0.  If the CGI parameter is ambiguous, redirect to a page that
** shows all possibilities and do not return.
*/
int name_to_rid_www(const char *zParamName){
  int i, rc;
  int rid;
  const char *zName = P(zParamName);
  Blob name;
  if(!zName && fossil_is_json()){
    zName = json_find_option_cstr(zParamName,NULL,NULL);
  }
  if( zName==0 || zName[0]==0 ) return 0;
  blob_init(&name, zName, -1);
  rc = name_to_uuid(&name, -1, "*");
  if( rc==1 ){
    blob_reset(&name);
    for(i=0; zName[i] && fossil_isdigit(zName[i]); i++){}
    if( zName[i]==0 ){
      rid = atoi(zName);
      if( db_exists("SELECT 1 FROM blob WHERE rid=%d", rid) ){
        return rid;
      }
    }
    return 0;
  }else if( rc==2 ){
    cgi_redirectf("%s/ambiguous/%T?src=%t", g.zTop, zName, g.zPath);
    return 0;
  }else{
    rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &name);
    blob_reset(&name);
  }
  return rid;
}
