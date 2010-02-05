/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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
** Return the number of errors.
*/
int name_to_uuid(Blob *pName, int iErrPriority){
  int rc;
  int sz;
  sz = blob_size(pName);
  if( sz>UUID_SIZE || sz<4 || !validate16(blob_buffer(pName), sz) ){
    char *zUuid;
    const char *zName = blob_str(pName);
    if( memcmp(zName, "tag:", 4)==0 ){
      zName += 4;
      zUuid = tag_to_uuid(zName);
    }else{
      zUuid = tag_to_uuid(zName);
      if( zUuid==0 ){
        zUuid = date_to_uuid(zName);
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
      zUuid = tag_to_uuid(blob_str(pName));
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
      return 1;
    }
    db_finalize(&q);
    rc = 0;
  }else{
    rc = 0;
  }
  return rc;
}

/*
** Convert a symbolic tag name into the UUID of a check-in that contains
** that tag.  If the tag appears on multiple check-ins, return the UUID
** of the most recent check-in with the tag.
**
** Memory to hold the returned string comes from malloc() and needs to
** be freed by the caller.
*/
char *tag_to_uuid(const char *zTag){
  char *zUuid = 
    db_text(0,
       "SELECT blob.uuid"
       "  FROM tag, tagxref, event, blob"
       " WHERE tag.tagname='sym-'||%Q "
       "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype>0 "
       "   AND event.objid=tagxref.rid "
       "   AND blob.rid=event.objid "
       " ORDER BY event.mtime DESC ",
       zTag
    );
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
char *date_to_uuid(const char *zDate){
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
  if( n<10 || zDate[4]!='-' || zDate[7]!='-' ) return 0;
  if( n>4 && sqlite3_strnicmp(&zDate[n-3], "utc", 3)==0 ){
    zCopy = mprintf("%s", zDate);
    zCopy[n-3] = 0;
    zDate = zCopy;
    n -= 3;
    useUtc = 1;
  }
  free(zCopy);
  zUuid = db_text(0,
    "SELECT (SELECT uuid FROM blob WHERE rid=event.objid)"
    "  FROM event"
    " WHERE mtime<=julianday(%Q %s) AND type='ci'"
    " ORDER BY mtime DESC LIMIT 1",
    zDate, useUtc ? "" : ",'utc'"
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
    printf("%s -> ", g.argv[i]);
    if( name_to_uuid(&name, 1) ){
      printf("ERROR: %s\n", g.zErrMsg);
      fossil_error_reset();
    }else{
      printf("%s\n", blob_buffer(&name));
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
** This routine is used in test routines to resolve command-line inputs
** into a rid.
*/
int name_to_rid(const char *zName){
  int i;
  int rid;
  Blob name;
  for(i=0; zName[i] && isdigit(zName[i]); i++){}
  if( zName[i]==0 ){
    rid = atoi(zName);
    if( db_exists("SELECT 1 FROM blob WHERE rid=%d", rid) ){
      return rid;
    }
  }
  blob_init(&name, zName, -1);
  if( name_to_uuid(&name, 1) ){
    fossil_fatal("%s", g.zErrMsg);
  }
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &name);
  blob_reset(&name);
  return rid;
}
