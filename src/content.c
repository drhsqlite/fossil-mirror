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
** Procedures store and retrieve records from the repository
*/
#include "config.h"
#include "content.h"
#include <assert.h>

/*
** Return the srcid associated with rid.  Or return 0 if rid is 
** original content and not a delta.
*/
static int findSrcid(int rid, const char *zDb){
  Stmt qsrc;
  int srcid;
  if( zDb ){
    db_prepare(&qsrc, "SELECT srcid FROM %s.delta WHERE rid=%d", zDb, rid);
  }else{
    db_prepare(&qsrc, "SELECT srcid FROM delta WHERE rid=%d", rid);
  }
  if( db_step(&qsrc)==SQLITE_ROW ){
    srcid = db_column_int(&qsrc, 0);
  }else{
    srcid = 0;
  }
  db_finalize(&qsrc);
  return srcid;
}

/*
** Extract the content for ID rid and put it into the
** uninitialized blob.
*/
void content_get_from_db(int rid, Blob *pBlob, const char *zDb){
  Stmt q;
  int srcid;
  assert( g.repositoryOpen );
  srcid = findSrcid(rid, zDb);
  if( zDb ){
    db_prepare(&q, 
       "SELECT content FROM %s.blob WHERE rid=%d AND size>=0",
       zDb, rid
    );
  }else{
    db_prepare(&q, 
       "SELECT content FROM blob WHERE rid=%d AND size>=0",
       rid
    );
  }
  if( srcid ){
    Blob src;
    content_get_from_db(srcid, &src, zDb);
    if( db_step(&q)==SQLITE_ROW ){
      Blob delta;
      db_ephemeral_blob(&q, 0, &delta);
      blob_uncompress(&delta, &delta);
      blob_init(pBlob,0,0);
      blob_delta_apply(&src, &delta, pBlob);
      blob_reset(&delta);
    }else{
      blob_init(pBlob, 0, 0);
    }
    blob_reset(&src);
  }else{
    if( db_step(&q)==SQLITE_ROW ){
      db_ephemeral_blob(&q, 0, pBlob);
      blob_uncompress(pBlob, pBlob);
    }else{
      blob_init(pBlob,0, 0);
    }
  }
  db_finalize(&q);
}
void content_get(int rid, Blob *pBlob){
  content_get_from_db(rid, pBlob, 0);
}

/*
** COMMAND:  test-content-get
**
** Extract a blob from the database and write it into a file.
*/
void test_content_get_cmd(void){
  int rid;
  Blob content;
  const char *zFile;
  if( g.argc!=4 && g.argc!=3 ) usage("RECORDID ?FILENAME?");
  zFile = g.argc==4 ? g.argv[3] : "-";
  db_must_be_within_tree();
  rid = name_to_rid(g.argv[2]);
  content_get(rid, &content);
  blob_write_to_file(&content, zFile);
}

/*
** COMMAND:  test-content-rawget
**
** Extract a blob from the database and write it into a file.  This
** version does not expand the delta.
*/
void test_content_rawget_cmd(void){
  int rid;
  Blob content;
  const char *zFile;
  if( g.argc!=4 && g.argc!=3 ) usage("RECORDID ?FILENAME?");
  zFile = g.argc==4 ? g.argv[3] : "-";
  db_must_be_within_tree();
  rid = name_to_rid(g.argv[2]);
  blob_zero(&content);
  db_blob(&content, "SELECT content FROM blob WHERE rid=%d", rid);
  blob_uncompress(&content, &content);
  blob_write_to_file(&content, zFile);
}

/*
** Write content into the database.  Return the record ID.  If the
** content is already in the database, just return the record ID.
**
** A phantom is written if pBlob==0.  If pBlob==0 then the UUID is set
** to zUuid.  Otherwise zUuid is ignored.
**
** If the record already exists but is a phantom, the pBlob content
** is inserted and the phatom becomes a real record.
*/
int content_put(Blob *pBlob, const char *zUuid){
  int size;
  int rid;
  Stmt s1;
  Blob cmpr;
  Blob hash;
  assert( g.repositoryOpen );
  if( pBlob==0 ){
    blob_init(&hash, zUuid, -1);
    size = -1;
  }else{
    sha1sum_blob(pBlob, &hash);
    size = blob_size(pBlob);
  }
  db_begin_transaction();

  /* Check to see if the entry already exists and if it does whether
  ** or not the entry is a phantom
  */
  db_prepare(&s1, "SELECT rid, size FROM blob WHERE uuid=%B", &hash);
  if( db_step(&s1)==SQLITE_ROW ){
    rid = db_column_int(&s1, 0);
    if( db_column_int(&s1, 1)>=0 || pBlob==0 ){
      /* Either the entry is not a phantom or it is a phantom but we
      ** have no data with which to dephathomize it.  In either case,
      ** there is nothing for use to do other than return the RID. */
      db_finalize(&s1);
      db_end_transaction(0);
      return rid;
    }
  }else{
    rid = 0;  /* No entry with the same UUID currently exists */
  }
  db_finalize(&s1);

  /* Construct a received-from ID if we do not already have one */
  if( g.rcvid==0 && pBlob!=0 ){
    db_multi_exec(
       "INSERT INTO rcvfrom(uid, mtime, nonce, ipaddr)"
       "VALUES(%d, julianday('now'), %Q, %Q)",
       g.userUid, g.zNonce, g.zIpAddr
    );
    g.rcvid = db_last_insert_rowid();
  }

  if( rid>0 ){
    /* We are just adding data to a phantom */
    assert( pBlob!=0 );
    db_prepare(&s1,
      "UPDATE blob SET rcvid=%d, size=%d, content=:data WHERE rid=%d",
       g.rcvid, size, rid
    );
    blob_compress(pBlob, &cmpr);
    db_bind_blob(&s1, ":data", &cmpr);
    db_exec(&s1);
  }else{
    /* We are creating a new entry */
    db_prepare(&s1,
      "INSERT INTO blob(rcvid,size,uuid,content)"
      "VALUES(%d,%d,'%s',:data)",
       g.rcvid, size, blob_str(&hash)
    );
    if( pBlob ){
      blob_compress(pBlob, &cmpr);
      db_bind_blob(&s1, ":data", &cmpr);
    }
    db_exec(&s1);
    rid = db_last_insert_rowid();
  }


  /* Finish the transaction and cleanup */
  db_finalize(&s1);
  db_end_transaction(0);
  blob_reset(&hash);

  /* Make arrangements to verify that the data can be recovered
  ** before we commit */
  if( pBlob ){
    blob_reset(&cmpr);
    verify_before_commit(rid);
  }
  return rid;
}

/*
** COMMAND:  test-content-put
**
** Extract a blob from the database and write it into a file.
*/
void test_content_put_cmd(void){
  int rid;
  Blob content;
  if( g.argc!=3 ) usage("FILENAME");
  db_must_be_within_tree();
  user_select();
  blob_read_from_file(&content, g.argv[2]);
  rid = content_put(&content, 0);
  printf("inserted as record %d\n", rid);
}

/*
** Make sure the content at rid is the original content and is not a
** delta.
*/
void content_undelta(int rid){
  if( findSrcid(rid, 0)>0 ){
    Blob x;
    Stmt s;
    content_get(rid, &x);
    db_prepare(&s, "UPDATE blob SET content=:c WHERE rid=%d", rid);
    db_bind_blob(&s, ":c", &x);
    db_exec(&s);
    db_finalize(&s);
    db_multi_exec("DELETE FROM delta WHERE rid=%d", rid);
  }
}

/*
** COMMAND:  test-content-undelta
**
** Make sure the content at RECORDID is not a delta
*/
void test_content_undelta_cmd(void){
  int rid;
  if( g.argc!=2 ) usage("RECORDID");
  db_must_be_within_tree();
  rid = atoi(g.argv[2]);
  content_undelta(rid);
}

/*
** Change the storage of rid so that it is a delta of srcid.
**
** If rid is already a delta from some other place then no
** conversion occurs and this is a no-op unless force==1.
**
** If srcid is a delta that depends on rid, then srcid is
** converted to undeltaed text.
*/
void content_deltify(int rid, int srcid, int force){
  int s;
  Blob data, src, delta;
  Stmt s1, s2;
  if( srcid==rid ) return;
  if( !force && findSrcid(rid, 0)>0 ) return;
  s = srcid;
  while( (s = findSrcid(s, 0))>0 ){
    if( s==rid ){
      content_undelta(srcid);
      break;
    }
  }
  content_get(srcid, &src);
  content_get(rid, &data);
  blob_delta_create(&src, &data, &delta);
  if( blob_size(&src)>=50 && blob_size(&data)>=50 &&
           blob_size(&delta) < blob_size(&data)*0.75 ){
    blob_compress(&delta, &delta);
    db_prepare(&s1, "UPDATE blob SET content=:data WHERE rid=%d", rid);
    db_prepare(&s2, "REPLACE INTO delta(rid,srcid)VALUES(%d,:sid)", rid);
    db_bind_blob(&s1, ":data", &delta);
    db_bind_int(&s2, ":sid", srcid);
    db_begin_transaction();
    db_exec(&s1);
    db_exec(&s2);
    db_end_transaction(0);
    db_finalize(&s1);
    db_finalize(&s2);
  }
  blob_reset(&src);
  blob_reset(&data);
  blob_reset(&delta);
  verify_before_commit(rid);
}

/*
** COMMAND:  test-content-deltify
**
** Convert the content at RID into a delta from SRCID.
*/
void test_content_deltify_cmd(void){
  if( g.argc!=5 ) usage("RID SRCID FORCE");
  db_must_be_within_tree();
  content_deltify(atoi(g.argv[2]), atoi(g.argv[3]), atoi(g.argv[4]));
}
