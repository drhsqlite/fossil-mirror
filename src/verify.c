/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to help verify the integrity of the
** the repository
*/
#include "config.h"
#include "verify.h"
#include <assert.h>

/*
** Load the record identify by rid.  Make sure we can reproduce it
** without error.
**
** Panic if anything goes wrong.  If this procedure returns it means
** that everything is OK.
*/
static void verify_rid(int rid){
  Blob uuid, hash, content;
  blob_zero(&uuid);
  db_blob(&uuid, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( blob_size(&uuid)!=UUID_SIZE ){
    fossil_panic("not a valid rid: %d", rid);
  }
  content_get(rid, &content);
  sha1sum_blob(&content, &hash);
/*  blob_reset(&content); */
  if( blob_compare(&uuid, &hash) ){
printf("content=[%s]\n", blob_str(&content));
    fossil_panic("hash of rid %d (%b) does not match its uuid (%b)",
                  rid, &hash, &uuid);
  }
  blob_reset(&uuid);
  blob_reset(&hash);
}

/*
**  
*/
static int verify_at_commit(void *notUsed){
  Stmt q;
  db_prepare(&q, "SELECT rid FROM toverify");
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    verify_rid(rid);
  }
  db_finalize(&q);
  db_multi_exec("DELETE FROM toverify");
  return 0;
}

/*
** Arrange to verify a particular record prior to committing.
** 
** If the record rid is less than 1, then just initialize the
** verification system but do not record anything as needing
** verification.
*/
void verify_before_commit(int rid){
  static int isInit = 0;
  if( !isInit ){
    db_multi_exec(
       "CREATE TEMP TABLE toverify(rid INTEGER PRIMARY KEY);"
    );
    sqlite3_commit_hook(g.db, verify_at_commit, 0);
    isInit = 1;
  }
  if( rid>0 ){
    db_multi_exec(
      "INSERT OR IGNORE INTO toverify VALUES(%d)", rid
    );
  }
}

/*
** COMMAND: test-verify-all
**
** Verify all records in the repository.
*/
void verify_all_cmd(void){
  Stmt q;
  db_must_be_within_tree();
  db_prepare(&q, "SELECT rid FROM blob");
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    verify_rid(rid);
  }
  db_finalize(&q);
}
