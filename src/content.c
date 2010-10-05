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
** Procedures store and retrieve records from the repository
*/
#include "config.h"
#include "content.h"
#include <assert.h>

/*
** Macros for debugging
*/
#if 1
# define CONTENT_TRACE(X)  printf X;
#else
# define CONTENT_TRACE(X)
#endif

/*
** The artifact retrival cache
*/
static struct {
  i64 szTotal;         /* Total size of all entries in the cache */
  int n;               /* Current number of eache entries */
  int nAlloc;          /* Number of slots allocated in a[] */
  int nextAge;         /* Age counter for implementing LRU */
  int skipCnt;         /* Used to limit entries expelled from cache */
  struct cacheLine {   /* One instance of this for each cache entry */
    int rid;                  /* Artifact id */
    int age;                  /* Age.  Newer is larger */
    Blob content;             /* Content of the artifact */
  } *a;                /* The positive cache */
  Bag inCache;         /* Set of artifacts currently in cache */

  /*
  ** The missing artifact cache.
  **
  ** Artifacts whose record ID are in missingCache cannot be retrieved
  ** either because they are phantoms or because they are a delta that
  ** depends on a phantom.  Artifacts whose content we are certain is
  ** available are in availableCache.  If an artifact is in neither cache
  ** then its current availablity is unknown.
  */
  Bag missing;         /* Cache of artifacts that are incomplete */
  Bag available;       /* Cache of artifacts that are complete */
} contentCache;

/*
** Remove the oldest element from the content cache
*/
static void content_cache_expire_oldest(void){
  int i;
  int mnAge = contentCache.nextAge;
  int mn = -1;
  for(i=0; i<contentCache.n; i++){
    if( contentCache.a[i].age<mnAge ){
      mnAge = contentCache.a[i].age;
      mn = i;
    }
  }
  if( mn>=0 ){
    bag_remove(&contentCache.inCache, contentCache.a[mn].rid);
    contentCache.szTotal -= blob_size(&contentCache.a[mn].content);
    blob_reset(&contentCache.a[mn].content);
    contentCache.n--;
    contentCache.a[mn] = contentCache.a[contentCache.n];
  }
}

/*
** Add an entry to the content cache
*/
void content_cache_insert(int rid, Blob *pBlob){
  struct cacheLine *p;
  if( contentCache.n>500 || contentCache.szTotal>50000000 ){
    content_cache_expire_oldest();
  }
  if( contentCache.n>=contentCache.nAlloc ){
    contentCache.nAlloc = contentCache.nAlloc*2 + 10;
    contentCache.a = realloc(contentCache.a,
                             contentCache.nAlloc*sizeof(contentCache.a[0]));
    if( contentCache.a==0 ) fossil_panic("out of memory");
  }
  p = &contentCache.a[contentCache.n++];
  p->rid = rid;
  p->age = contentCache.nextAge++;
  contentCache.szTotal += blob_size(pBlob);
  p->content = *pBlob;
  blob_zero(pBlob);
  bag_insert(&contentCache.inCache, rid);
}

/*
** Clear the content cache.
*/
void content_clear_cache(void){
  int i;
  for(i=0; i<contentCache.n; i++){
    blob_reset(&contentCache.a[i].content);
  }
  bag_clear(&contentCache.missing);
  bag_clear(&contentCache.available);
  bag_clear(&contentCache.inCache);
  contentCache.n = 0;
  contentCache.szTotal = 0;
}

/*
** Return the srcid associated with rid.  Or return 0 if rid is 
** original content and not a delta.
*/
static int findSrcid(int rid){
  static Stmt q;
  int srcid;
  db_static_prepare(&q, "SELECT srcid FROM delta WHERE rid=:rid");
  db_bind_int(&q, ":rid", rid);
  if( db_step(&q)==SQLITE_ROW ){
    srcid = db_column_int(&q, 0);
  }else{
    srcid = 0;
  }
  db_reset(&q);
  return srcid;
}

/*
** Check to see if content is available for artifact "rid".  Return
** true if it is.  Return false if rid is a phantom or depends on
** a phantom.
*/
int content_is_available(int rid){
  int srcid;
  int depth = 0;  /* Limit to recursion depth */
  while( depth++ < 10000000 ){  
    if( bag_find(&contentCache.missing, rid) ){
      return 0;
    }
    if( bag_find(&contentCache.available, rid) ){
      return 1;
    }
    if( db_int(-1, "SELECT size FROM blob WHERE rid=%d", rid)<0 ){
      bag_insert(&contentCache.missing, rid);
      return 0;
    }
    srcid = findSrcid(rid);
    if( srcid==0 ){
      bag_insert(&contentCache.available, rid);
      return 1;
    }
    rid = srcid;
  }
  fossil_panic("delta-loop in repository");
  return 0;
}

/*
** Mark artifact rid as being available now.  Update the cache to
** show that everything that was formerly unavailable because rid
** was missing is now available.
*/
static void content_mark_available(int rid){
  Bag pending;
  static Stmt q;
  if( bag_find(&contentCache.available, rid) ) return;
  bag_init(&pending);
  bag_insert(&pending, rid);
  while( (rid = bag_first(&pending))!=0 ){
    bag_remove(&pending, rid);
    bag_remove(&contentCache.missing, rid);
    bag_insert(&contentCache.available, rid);
    db_static_prepare(&q, "SELECT rid FROM delta WHERE srcid=:rid");
    db_bind_int(&q, ":rid", rid);
    while( db_step(&q)==SQLITE_ROW ){
      int nx = db_column_int(&q, 0);
      bag_insert(&pending, nx);
    }
    db_reset(&q);
  }
  bag_clear(&pending);
}

/*
** Get the blob.content value for blob.rid=rid.  Return 1 on success or
** 0 on failure.
*/
static int content_of_blob(int rid, Blob *pBlob){
  static Stmt q;
  int rc = 0;
  db_static_prepare(&q, "SELECT content FROM blob WHERE rid=:rid AND size>=0");
  db_bind_int(&q, ":rid", rid);
  if( db_step(&q)==SQLITE_ROW ){
    db_ephemeral_blob(&q, 0, pBlob);
    blob_uncompress(pBlob, pBlob);
    rc = 1;
  }
  db_reset(&q);
  return rc;
}

/*
** Extract the content for ID rid and put it into the
** uninitialized blob.  Return 1 on success.  If the record
** is a phantom, zero pBlob and return 0.
*/
int content_get(int rid, Blob *pBlob){
  int rc;
  int i;
  int nextRid;

  assert( g.repositoryOpen );
  blob_zero(pBlob);
  if( rid==0 ) return 0;

  /* Early out if we know the content is not available */
  if( bag_find(&contentCache.missing, rid) ){
    return 0;
  }

  /* Look for the artifact in the cache first */
  if( bag_find(&contentCache.inCache, rid) ){
    for(i=0; i<contentCache.n; i++){
      if( contentCache.a[i].rid==rid ){
        blob_copy(pBlob, &contentCache.a[i].content);
        contentCache.a[i].age = contentCache.nextAge++;
        return 1;
      }
    }
  }

  nextRid = findSrcid(rid);
  if( nextRid==0 ){
    rc = content_of_blob(rid, pBlob);
  }else{
    int n = 1;
    int nAlloc = 10;
    int *a = 0;
    int mx;
    Blob delta, next;

    a = malloc( sizeof(a[0])*nAlloc );
    if( a==0 ) fossil_panic("out of memory");
    a[0] = rid;
    a[1] = nextRid;
    n = 1;
    while( !bag_find(&contentCache.inCache, nextRid)
        && (nextRid = findSrcid(nextRid))>0 ){
      n++;
      if( n>=nAlloc ){
        nAlloc = nAlloc*2 + 10;
        a = realloc(a, nAlloc*sizeof(a[0]));
        if( a==0 ) fossil_panic("out of memory");
      }
      a[n] = nextRid;
    }
    mx = n;
    rc = content_get(a[n], pBlob);
    n--;
    while( rc && n>=0 ){
      rc = content_of_blob(a[n], &delta);
      if( rc ){
        blob_delta_apply(pBlob, &delta, &next);
        blob_reset(&delta);
        if( (mx-n)%8==0 ){
          content_cache_insert(a[n+1], pBlob);
        }else{
          blob_reset(pBlob);
        }
        *pBlob = next;
      }
      n--;
    }
    free(a);
    if( !rc ) blob_reset(pBlob);
  }
  if( rc==0 ){
    bag_insert(&contentCache.missing, rid);
  }else{
    bag_insert(&contentCache.available, rid);
  }
  return rc;
}

/*
** COMMAND:  artifact
**
** Usage: %fossil artifact ARTIFACT-ID  ?OUTPUT-FILENAME?
**
** Extract an artifact by its SHA1 hash and write the results on
** standard output, or if the optional 4th argument is given, in
** the named output file.
*/
void artifact_cmd(void){
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
** When a record is converted from a phantom to a real record,
** if that record has other records that are derived by delta,
** then call manifest_crosslink() on those other records.
**
** Tail recursion is used to minimize stack depth.
*/
void after_dephantomize(int rid, int linkFlag){
  Stmt q;
  int nChildAlloc = 0;
  int *aChild = 0;

  while( rid ){
    int nChildUsed = 0;
    int i;
    if( linkFlag ){
      Blob content;
      content_get(rid, &content);
      manifest_crosslink(rid, &content);
      blob_reset(&content);
    }
    db_prepare(&q, "SELECT rid FROM delta WHERE srcid=%d", rid);
    while( db_step(&q)==SQLITE_ROW ){
      int child = db_column_int(&q, 0);
      if( nChildUsed>=nChildAlloc ){
        nChildAlloc = nChildAlloc*2 + 10;
        aChild = realloc(aChild, nChildAlloc*sizeof(aChild));
        if( aChild==0 ) fossil_panic("out of memory");
      }
      aChild[nChildUsed++] = child;
    }
    db_finalize(&q);
    for(i=1; i<nChildUsed; i++){
      after_dephantomize(aChild[i], 1);
    }
    rid = nChildUsed>0 ? aChild[0] : 0;
    linkFlag = 1;
  }
  free(aChild);
}

/*
** Write content into the database.  Return the record ID.  If the
** content is already in the database, just return the record ID.
**
** If srcId is specified, then pBlob is delta content from
** the srcId record.  srcId might be a phantom.
**
** zUuid is the UUID of the artifact, if it is specified.  When srcId is
** specified then zUuid must always be specified.  If srcId is zero,
** and zUuid is zero then the correct zUuid is computed from pBlob.
**
** If the record already exists but is a phantom, the pBlob content
** is inserted and the phatom becomes a real record.
*/
int content_put(Blob *pBlob, const char *zUuid, int srcId){
  int size;
  int rid;
  Stmt s1;
  Blob cmpr;
  Blob hash;
  int markAsUnclustered = 0;
  int isDephantomize = 0;
  
  assert( g.repositoryOpen );
  assert( pBlob!=0 );
  assert( srcId==0 || zUuid!=0 );
  if( zUuid==0 ){
    assert( pBlob!=0 );
    sha1sum_blob(pBlob, &hash);
  }else{
    blob_init(&hash, zUuid, -1);
  }
  size = blob_size(pBlob);
  db_begin_transaction();

  /* Check to see if the entry already exists and if it does whether
  ** or not the entry is a phantom
  */
  db_prepare(&s1, "SELECT rid, size FROM blob WHERE uuid=%B", &hash);
  if( db_step(&s1)==SQLITE_ROW ){
    rid = db_column_int(&s1, 0);
    if( db_column_int(&s1, 1)>=0 || pBlob==0 ){
      /* Either the entry is not a phantom or it is a phantom but we
      ** have no data with which to dephantomize it.  In either case,
      ** there is nothing for us to do other than return the RID. */
      db_finalize(&s1);
      db_end_transaction(0);
      return rid;
    }
  }else{
    rid = 0;  /* No entry with the same UUID currently exists */
    markAsUnclustered = 1;
  }
  db_finalize(&s1);

  /* Construct a received-from ID if we do not already have one */
  if( g.rcvid==0 ){
    db_multi_exec(
       "INSERT INTO rcvfrom(uid, mtime, nonce, ipaddr)"
       "VALUES(%d, julianday('now'), %Q, %Q)",
       g.userUid, g.zNonce, g.zIpAddr
    );
    g.rcvid = db_last_insert_rowid();
  }

  blob_compress(pBlob, &cmpr);
  if( rid>0 ){
    /* We are just adding data to a phantom */
    db_prepare(&s1,
      "UPDATE blob SET rcvid=%d, size=%d, content=:data WHERE rid=%d",
       g.rcvid, size, rid
    );
    db_bind_blob(&s1, ":data", &cmpr);
    db_exec(&s1);
    db_multi_exec("DELETE FROM phantom WHERE rid=%d", rid);
    if( srcId==0 || content_is_available(srcId) ){
      isDephantomize = 1;
      content_mark_available(rid);
    }
  }else{
    /* We are creating a new entry */
    db_prepare(&s1,
      "INSERT INTO blob(rcvid,size,uuid,content)"
      "VALUES(%d,%d,'%b',:data)",
       g.rcvid, size, &hash
    );
    db_bind_blob(&s1, ":data", &cmpr);
    db_exec(&s1);
    rid = db_last_insert_rowid();
    if( !pBlob ){
      db_multi_exec("INSERT OR IGNORE INTO phantom VALUES(%d)", rid);
    }
    if( g.markPrivate ){
      db_multi_exec("INSERT INTO private VALUES(%d)", rid);
      markAsUnclustered = 0;
    }
  }
  blob_reset(&cmpr);

  /* If the srcId is specified, then the data we just added is
  ** really a delta.  Record this fact in the delta table.
  */
  if( srcId ){
    db_multi_exec("REPLACE INTO delta(rid,srcid) VALUES(%d,%d)", rid, srcId);
  }
  if( !isDephantomize && bag_find(&contentCache.missing, rid) && 
      (srcId==0 || content_is_available(srcId)) ){
    content_mark_available(rid);
  }
  if( isDephantomize ){
    after_dephantomize(rid, 0);
  }
  
  /* Add the element to the unclustered table if has never been
  ** previously seen.
  */
  if( markAsUnclustered ){
    db_multi_exec("INSERT OR IGNORE INTO unclustered VALUES(%d)", rid);
  }

  /* Finish the transaction and cleanup */
  db_finalize(&s1);
  db_end_transaction(0);
  blob_reset(&hash);

  /* Make arrangements to verify that the data can be recovered
  ** before we commit */
  verify_before_commit(rid);
  return rid;
}

/*
** Create a new phantom with the given UUID and return its artifact ID.
*/
int content_new(const char *zUuid){
  int rid;
  static Stmt s1, s2, s3;
  
  assert( g.repositoryOpen );
  db_begin_transaction();
  if( uuid_is_shunned(zUuid) ){
    db_end_transaction(0);
    return 0;
  }
  db_static_prepare(&s1,
    "INSERT INTO blob(rcvid,size,uuid,content)"
    "VALUES(0,-1,:uuid,NULL)"
  );
  db_bind_text(&s1, ":uuid", zUuid);
  db_exec(&s1);
  rid = db_last_insert_rowid();
  db_static_prepare(&s2,
    "INSERT INTO phantom VALUES(:rid)"
  );
  db_bind_int(&s2, ":rid", rid);
  db_exec(&s2);
  if( g.markPrivate ){
    db_multi_exec("INSERT INTO private VALUES(%d)", rid);
  }else{
    db_static_prepare(&s3,
      "INSERT INTO unclustered VALUES(:rid)"
    );
    db_bind_int(&s3, ":rid", rid);
    db_exec(&s3);
  }
  bag_insert(&contentCache.missing, rid);
  db_end_transaction(0);
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
  rid = content_put(&content, 0, 0);
  printf("inserted as record %d\n", rid);
}

/*
** Make sure the content at rid is the original content and is not a
** delta.
*/
void content_undelta(int rid){
  if( findSrcid(rid)>0 ){
    Blob x;
    if( content_get(rid, &x) ){
      Stmt s;
      db_prepare(&s, "UPDATE blob SET content=:c, size=%d WHERE rid=%d",
                     blob_size(&x), rid);
      blob_compress(&x, &x);
      db_bind_blob(&s, ":c", &x);
      db_exec(&s);
      db_finalize(&s);
      blob_reset(&x);
      db_multi_exec("DELETE FROM delta WHERE rid=%d", rid);
    }
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
** Return true if the given RID is marked as PRIVATE.
*/
int content_is_private(int rid){
  static Stmt s1;
  int rc;
  db_static_prepare(&s1,
    "SELECT 1 FROM private WHERE rid=:rid"
  );
  db_bind_int(&s1, ":rid", rid);
  rc = db_step(&s1);
  db_reset(&s1);
  return rc==SQLITE_ROW;  
}

/*
** Make sure an artifact is public.  
*/
void content_make_public(int rid){
  static Stmt s1;
  db_static_prepare(&s1,
    "DELETE FROM private WHERE rid=:rid"
  );
  db_bind_int(&s1, ":rid", rid);
  db_exec(&s1);
}

/*
** Change the storage of rid so that it is a delta of srcid.
**
** If rid is already a delta from some other place then no
** conversion occurs and this is a no-op unless force==1.
**
** Never generate a delta that carries a private artifact into a public
** artifact.  Otherwise, when we go to send the public artifact on a
** sync operation, the other end of the sync will never be able to receive
** the source of the delta.  It is OK to delta private->private and
** public->private and public->public.  Just no private->public delta.
**
** If srcid is a delta that depends on rid, then srcid is
** converted to undeltaed text.
**
** If either rid or srcid contain less than 50 bytes, or if the
** resulting delta does not achieve a compression of at least 25% on
** its own the rid is left untouched.
*/
void content_deltify(int rid, int srcid, int force){
  int s;
  Blob data, src, delta;
  Stmt s1, s2;
  if( srcid==rid ) return;
  if( !force && findSrcid(rid)>0 ) return;
  if( content_is_private(srcid) && !content_is_private(rid) ){
    return;
  }
  s = srcid;
  while( (s = findSrcid(s))>0 ){
    if( s==rid ){
      content_undelta(srcid);
      break;
    }
  }
  content_get(srcid, &src);
  if( blob_size(&src)<50 ){
    blob_reset(&src);
    return;
  }
  content_get(rid, &data);
  if( blob_size(&data)<50 ){
    blob_reset(&src);
    blob_reset(&data);
    return;
  }
  blob_delta_create(&src, &data, &delta);
  if( blob_size(&delta) < blob_size(&data)*0.75 ){
    blob_compress(&delta, &delta);
    db_prepare(&s1, "UPDATE blob SET content=:data WHERE rid=%d", rid);
    db_prepare(&s2, "REPLACE INTO delta(rid,srcid)VALUES(%d,%d)", rid, srcid);
    db_bind_blob(&s1, ":data", &delta);
    db_begin_transaction();
    db_exec(&s1);
    db_exec(&s2);
    db_end_transaction(0);
    db_finalize(&s1);
    db_finalize(&s2);
    verify_before_commit(rid);
  }
  blob_reset(&src);
  blob_reset(&data);
  blob_reset(&delta);
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
