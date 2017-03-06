/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to help verify the integrity of
** the repository.
**
** This file primarily implements the verify_before_commit() interface.
** Any function can call verify_before_commit() with a record id (RID)
** as an argument.  Then before the next change to the database commits,
** this routine will reach in and check that the record can be extracted
** correctly from the BLOB table.
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
  Blob uuid, content;
  if( content_size(rid, 0)<0 ){
    return;  /* No way to verify phantoms */
  }
  blob_zero(&uuid);
  db_blob(&uuid, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( !hname_validate(blob_buffer(&uuid), blob_size(&uuid)) ){
    fossil_fatal("not a valid rid: %d", rid);
  }
  if( content_get(rid, &content) ){
    if( !hname_verify_hash(&content, blob_buffer(&uuid), blob_size(&uuid)) ){
      fossil_fatal("hash of rid %d does not match its uuid (%b)",
                    rid, &uuid);
    }
    blob_reset(&content);
  }
  blob_reset(&uuid);
}

/*
** The following bag holds the rid for every record that needs
** to be verified.
*/
static Bag toVerify;
static int inFinalVerify = 0;

/*
** This routine is called just prior to each commit operation.
**
** Invoke verify_rid() on every record that has been added or modified
** in the repository, in order to make sure that the repository is sane.
*/
static int verify_at_commit(void){
  int rid;
  content_clear_cache();
  inFinalVerify = 1;
  rid = bag_first(&toVerify);
  while( rid>0 ){
    verify_rid(rid);
    rid = bag_next(&toVerify, rid);
  }
  bag_clear(&toVerify);
  inFinalVerify = 0;
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
    db_commit_hook(verify_at_commit, 1000);
    isInit = 1;
  }
  assert( !inFinalVerify );
  if( rid>0 ){
    bag_insert(&toVerify, rid);
  }
}

/*
** Cancel all pending verification operations.
*/
void verify_cancel(void){
  bag_clear(&toVerify);
}

/*
** COMMAND: test-verify-all
**
** Verify all records in the repository.
*/
void verify_all_cmd(void){
  Stmt q;
  int cnt = 0;
  db_must_be_within_tree();
  db_prepare(&q, "SELECT rid FROM blob");
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    verify_before_commit(rid);
    cnt++;
    assert( bag_count(&toVerify)==cnt );
  }
  db_finalize(&q);
  verify_at_commit();
  assert( bag_count(&toVerify)==0 );
}
