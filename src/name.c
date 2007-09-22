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
** the name as a tag.
**
** Return the number of errors.
*/
int name_to_uuid(Blob *pName, int iErrPriority){
  int rc;
  int sz;
  sz = blob_size(pName);
  if( sz>UUID_SIZE || sz<4 || !validate16(blob_buffer(pName), sz) ){
    Stmt q;
    Blob uuid;

    db_prepare(&q,
      "SELECT (SELECT uuid FROM blob WHERE rid=objid)"
      "  FROM tagxref JOIN event ON rid=objid"
      " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%B)"
      "   AND addflag"
      "   AND value IS NULL"
      " ORDER BY event.mtime DESC",
      pName
    );
    blob_zero(&uuid);
    if( db_step(&q)==SQLITE_ROW ){
      db_column_blob(&q, 0, &uuid);
    }
    db_finalize(&q);
    if( blob_size(&uuid)==0 ){
      fossil_error(iErrPriority, "not a valid object name: %b", pName);
      blob_reset(&uuid);
      return 1;
    }else{
      blob_reset(pName);
      *pName = uuid;
      return 0;
    }
  }
  blob_materialize(pName);
  canonical16(blob_buffer(pName), sz);
  if( sz==UUID_SIZE ){
    rc = db_int(1, "SELECT 0 FROM blob WHERE uuid=%B", pName);
    if( rc ){
      fossil_error(iErrPriority, "unknown object: %b", pName);
      blob_reset(pName);
    }
  }else if( sz<UUID_SIZE && sz>=4 ){
    char zOrig[UUID_SIZE+1];
    memcpy(zOrig, blob_buffer(pName), sz);
    zOrig[sz] = 0;
    blob_reset(pName);
    db_blob(pName, "SELECT uuid FROM blob WHERE uuid>='%s'", zOrig);
    if( blob_size(pName)!=UUID_SIZE ){
      fossil_error(iErrPriority, "no match: %s", zOrig);
      rc = 1;
    }else{
      zOrig[sz-1]++;
      if( db_exists("SELECT 1 FROM blob WHERE uuid>%B AND uuid<'%s'",
                    pName, zOrig) ){
         zOrig[sz-1]--;
         fossil_error(iErrPriority, "non-unique name prefix: %s", zOrig);
         rc = 1;
      }else{
         rc = 0;
      }
    }
  }
  return rc;
}

/*
** COMMAND:  test-name-to-uuid
**
** Convert a name to a full UUID.
*/
void test_name_to_uuid(void){
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
