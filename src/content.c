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
** The artifact retrieval cache
*/
static struct {
  i64 szTotal;         /* Total size of all entries in the cache */
  int n;               /* Current number of cache entries */
  int nAlloc;          /* Number of slots allocated in a[] */
  int nextAge;         /* Age counter for implementing LRU */
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
  ** then its current availability is unknown.
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
** Add an entry to the content cache.
**
** This routines hands responsibility for the artifact over to the cache.
** The cache will deallocate memory when it has finished with it.
*/
void content_cache_insert(int rid, Blob *pBlob){
  struct cacheLine *p;
  if( contentCache.n>500 || contentCache.szTotal>50000000 ){
    i64 szBefore;
    do{
      szBefore = contentCache.szTotal;
      content_cache_expire_oldest();
    }while( contentCache.szTotal>50000000 && contentCache.szTotal<szBefore );
  }
  if( contentCache.n>=contentCache.nAlloc ){
    contentCache.nAlloc = contentCache.nAlloc*2 + 10;
    contentCache.a = fossil_realloc(contentCache.a,
                             contentCache.nAlloc*sizeof(contentCache.a[0]));
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
int delta_source_rid(int rid){
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
** Return the blob.size field given blob.rid
*/
int content_size(int rid, int dflt){
  static Stmt q;
  int sz = dflt;
  db_static_prepare(&q, "SELECT size FROM blob WHERE rid=:r");
  db_bind_int(&q, ":r", rid);
  if( db_step(&q)==SQLITE_ROW ){
    sz = db_column_int(&q, 0);
  }
  db_reset(&q);
  return sz;
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
    if( content_size(rid, -1)<0 ){
      bag_insert(&contentCache.missing, rid);
      return 0;
    }
    srcid = delta_source_rid(rid);
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

  nextRid = delta_source_rid(rid);
  if( nextRid==0 ){
    rc = content_of_blob(rid, pBlob);
  }else{
    int n = 1;
    int nAlloc = 10;
    int *a = 0;
    int mx;
    Blob delta, next;

    a = fossil_malloc( sizeof(a[0])*nAlloc );
    a[0] = rid;
    a[1] = nextRid;
    n = 1;
    while( !bag_find(&contentCache.inCache, nextRid)
        && (nextRid = delta_source_rid(nextRid))>0 ){
      n++;
      if( n>=nAlloc ){
        if( n>db_int(0, "SELECT max(rid) FROM blob") ){
          fossil_panic("infinite loop in DELTA table");
        }
        nAlloc = nAlloc*2 + 10;
        a = fossil_realloc(a, nAlloc*sizeof(a[0]));
      }
      a[n] = nextRid;
    }
    mx = n;
    rc = content_get(a[n], pBlob);
    n--;
    while( rc && n>=0 ){
      rc = content_of_blob(a[n], &delta);
      if( rc ){
        if( blob_delta_apply(pBlob, &delta, &next)<0 ){
          rc = 1;
        }else{
          blob_reset(&delta);
          if( (mx-n)%8==0 ){
            content_cache_insert(a[n+1], pBlob);
          }else{
            blob_reset(pBlob);
          }
          *pBlob = next;
        }
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
** COMMAND: artifact*
**
** Usage: %fossil artifact ARTIFACT-ID ?OUTPUT-FILENAME? ?OPTIONS?
**
** Extract an artifact by its artifact hash and write the results on
** standard output, or if the optional 4th argument is given, in
** the named output file.
**
** Options:
**    -R|--repository FILE       Extract artifacts from repository FILE
**
** See also: finfo
*/
void artifact_cmd(void){
  int rid;
  Blob content;
  const char *zFile;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  if( g.argc!=4 && g.argc!=3 ) usage("ARTIFACT-ID ?FILENAME? ?OPTIONS?");
  zFile = g.argc==4 ? g.argv[3] : "-";
  rid = name_to_rid(g.argv[2]);
  if( rid==0 ){
    fossil_fatal("%s",g.zErrMsg);
  }
  content_get(rid, &content);
  blob_write_to_file(&content, zFile);
}

/*
** COMMAND: test-content-rawget
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
** The following flag is set to disable the automatic calls to
** manifest_crosslink() when a record is dephantomized.  This
** flag can be set (for example) when doing a clone when we know
** that rebuild will be run over all records at the conclusion
** of the operation.
*/
static int ignoreDephantomizations = 0;

/*
** When a record is converted from a phantom to a real record,
** if that record has other records that are derived by delta,
** then call manifest_crosslink() on those other records.
**
** If the formerly phantom record or any of the other records
** derived by delta from the former phantom are a baseline manifest,
** then also invoke manifest_crosslink() on the delta-manifests
** associated with that baseline.
**
** Tail recursion is used to minimize stack depth.
*/
void after_dephantomize(int rid, int linkFlag){
  Stmt q;
  int nChildAlloc = 0;
  int *aChild = 0;
  Blob content;

  if( ignoreDephantomizations ) return;
  while( rid ){
    int nChildUsed = 0;
    int i;

    /* Parse the object rid itself */
    if( linkFlag ){
      content_get(rid, &content);
      manifest_crosslink(rid, &content, MC_NONE);
      assert( blob_is_reset(&content) );
    }

    /* Parse all delta-manifests that depend on baseline-manifest rid */
    db_prepare(&q, "SELECT rid FROM orphan WHERE baseline=%d", rid);
    while( db_step(&q)==SQLITE_ROW ){
      int child = db_column_int(&q, 0);
      if( nChildUsed>=nChildAlloc ){
        nChildAlloc = nChildAlloc*2 + 10;
        aChild = fossil_realloc(aChild, nChildAlloc*sizeof(aChild));
      }
      aChild[nChildUsed++] = child;
    }
    db_finalize(&q);
    for(i=0; i<nChildUsed; i++){
      content_get(aChild[i], &content);
      manifest_crosslink(aChild[i], &content, MC_NONE);
      assert( blob_is_reset(&content) );
    }
    if( nChildUsed ){
      db_multi_exec("DELETE FROM orphan WHERE baseline=%d", rid);
    }

    /* Recursively dephantomize all artifacts that are derived by
    ** delta from artifact rid and which have not already been
    ** cross-linked.  */
    nChildUsed = 0;
    db_prepare(&q,
       "SELECT rid FROM delta"
       " WHERE srcid=%d"
       "   AND NOT EXISTS(SELECT 1 FROM mlink WHERE mid=delta.rid)",
       rid
    );
    while( db_step(&q)==SQLITE_ROW ){
      int child = db_column_int(&q, 0);
      if( nChildUsed>=nChildAlloc ){
        nChildAlloc = nChildAlloc*2 + 10;
        aChild = fossil_realloc(aChild, nChildAlloc*sizeof(aChild));
      }
      aChild[nChildUsed++] = child;
    }
    db_finalize(&q);
    for(i=1; i<nChildUsed; i++){
      after_dephantomize(aChild[i], 1);
    }

    /* Tail recursion for the common case where only a single artifact
    ** is derived by delta from rid... */
    rid = nChildUsed>0 ? aChild[0] : 0;
    linkFlag = 1;
  }
  free(aChild);
}

/*
** Turn dephantomization processing on or off.
*/
void content_enable_dephantomize(int onoff){
  ignoreDephantomizations = !onoff;
}

/*
** Make sure the g.rcvid global variable has been initialized.
**
** If the g.zIpAddr variable has not been set when this routine is
** called, use zSrc as the source of content for the rcvfrom
** table entry.
*/
void content_rcvid_init(const char *zSrc){
  if( g.rcvid==0 ){
    user_select();
    if( g.zIpAddr ) zSrc = g.zIpAddr;
    db_multi_exec(
       "INSERT INTO rcvfrom(uid, mtime, nonce, ipaddr)"
       "VALUES(%d, julianday('now'), %Q, %Q)",
       g.userUid, g.zNonce, zSrc
    );
    g.rcvid = db_last_insert_rowid();
  }
}

/*
** Write content into the database.  Return the record ID.  If the
** content is already in the database, just return the record ID.
**
** If srcId is specified, then pBlob is delta content from
** the srcId record.  srcId might be a phantom.
**
** pBlob is normally uncompressed text.  But if nBlob>0 then the
** pBlob value has already been compressed and nBlob is its uncompressed
** size.  If nBlob>0 then zUuid must be valid.
**
** zUuid is the UUID of the artifact, if it is specified.  When srcId is
** specified then zUuid must always be specified.  If srcId is zero,
** and zUuid is zero then the correct zUuid is computed from pBlob.
**
** If the record already exists but is a phantom, the pBlob content
** is inserted and the phatom becomes a real record.
**
** The original content of pBlob is not disturbed.  The caller continues
** to be responsible for pBlob.  This routine does *not* take over
** responsibility for freeing pBlob.
*/
int content_put_ex(
  Blob *pBlob,              /* Content to add to the repository */
  const char *zUuid,        /* artifact hash of reconstructed pBlob */
  int srcId,                /* pBlob is a delta from this entry */
  int nBlob,                /* pBlob is compressed. Original size is this */
  int isPrivate             /* The content should be marked private */
){
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
  db_begin_transaction();
  if( zUuid==0 ){
    assert( nBlob==0 );
    /* First check the auxiliary hash to see if there is already an artifact
    ** that uses the auxiliary hash name */
    hname_hash(pBlob, 1, &hash);
    rid = fast_uuid_to_rid(blob_str(&hash));
    if( rid==0 ){
      /* No existing artifact with the auxiliary hash name.  Therefore, use
      ** the primary hash name. */
      blob_reset(&hash);
      hname_hash(pBlob, 0, &hash);
    }
  }else{
    blob_init(&hash, zUuid, -1);
  }
  if( g.eHashPolicy==HPOLICY_AUTO && blob_size(&hash)>HNAME_LEN_SHA1 ){
    g.eHashPolicy = HPOLICY_SHA3;
    db_set_int("hash-policy", HPOLICY_SHA3, 0);
  }
  if( nBlob ){
    size = nBlob;
  }else{
    size = blob_size(pBlob);
    if( srcId ){
      size = delta_output_size(blob_buffer(pBlob), size);
    }
  }

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
  content_rcvid_init(0);

  if( nBlob ){
    cmpr = pBlob[0];
  }else{
    blob_compress(pBlob, &cmpr);
  }
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
      "VALUES(%d,%d,'%q',:data)",
       g.rcvid, size, blob_str(&hash)
    );
    db_bind_blob(&s1, ":data", &cmpr);
    db_exec(&s1);
    rid = db_last_insert_rowid();
    if( !pBlob ){
      db_multi_exec("INSERT OR IGNORE INTO phantom VALUES(%d)", rid);
    }
  }
  if( g.markPrivate || isPrivate ){
    db_multi_exec("INSERT INTO private VALUES(%d)", rid);
    markAsUnclustered = 0;
  }
  if( nBlob==0 ) blob_reset(&cmpr);

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
** This is the simple common case for inserting content into the
** repository.  pBlob is the content to be inserted.
**
** pBlob is uncompressed and is not deltaed.  It is exactly the content
** to be inserted.
**
** The original content of pBlob is not disturbed.  The caller continues
** to be responsible for pBlob.  This routine does *not* take over
** responsiblity for freeing pBlob.
*/
int content_put(Blob *pBlob){
  return content_put_ex(pBlob, 0, 0, 0, 0);
}


/*
** Create a new phantom with the given UUID and return its artifact ID.
*/
int content_new(const char *zUuid, int isPrivate){
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
  if( g.markPrivate || isPrivate ){
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
** COMMAND: test-content-put
**
** Usage: %fossil test-content-put FILE
**
** Read the content of FILE and add it to the Blob table as a new
** artifact using a direct call to content_put().
*/
void test_content_put_cmd(void){
  int rid;
  Blob content;
  if( g.argc!=3 ) usage("FILENAME");
  db_must_be_within_tree();
  user_select();
  blob_read_from_file(&content, g.argv[2]);
  rid = content_put(&content);
  fossil_print("inserted as record %d\n", rid);
}

/*
** Make sure the content at rid is the original content and is not a
** delta.
*/
void content_undelta(int rid){
  if( delta_source_rid(rid)>0 ){
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
** COMMAND: test-content-undelta
**
** Make sure the content at RECORDID is not a delta
*/
void test_content_undelta_cmd(void){
  int rid;
  if( g.argc!=3 ) usage("RECORDID");
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
** Try to change the storage of rid so that it is a delta from one
** of the artifacts given in aSrc[0]..aSrc[nSrc-1].  The aSrc[*] that
** gives the smallest delta is choosen.
**
** If rid is already a delta from some other place then no
** conversion occurs and this is a no-op unless force==1.  If force==1,
** then nSrc must also be 1.
**
** Never generate a delta that carries a private artifact into a public
** artifact.  Otherwise, when we go to send the public artifact on a
** sync operation, the other end of the sync will never be able to receive
** the source of the delta.  It is OK to delta private->private and
** public->private and public->public.  Just no private->public delta.
**
** If aSrc[bestSrc] is already a dleta that depends on rid, then it is
** converted to undeltaed text before the aSrc[bestSrc]->rid delta is
** created, in order to prevent a delta loop.
**
** If either rid or aSrc[i] contain less than 50 bytes, or if the
** resulting delta does not achieve a compression of at least 25%
** the rid is left untouched.
**
** Return 1 if a delta is made and 0 if no delta occurs.
*/
int content_deltify(int rid, int *aSrc, int nSrc, int force){
  int s;
  Blob data;           /* Content of rid */
  Blob src;            /* Content of aSrc[i] */
  Blob delta;          /* Delta from aSrc[i] to rid */
  Blob bestDelta;      /* Best delta seen so far */
  int bestSrc = 0;     /* Which aSrc is the source of the best delta */
  int rc = 0;          /* Value to return */
  int i;               /* Loop variable for aSrc[] */

  /* If rid is already a child (a delta) of some other artifact, return
  ** immediately if the force flags is false
  */
  if( !force && delta_source_rid(rid)>0 ) return 0;

  /* Get the complete content of the object to be delta-ed.  If the size
  ** is less than 50 bytes, then there really is no point in trying to do
  ** a delta, so return immediately
  */
  content_get(rid, &data);
  if( blob_size(&data)<50 ){
    /* Do not try to create a delta for objects smaller than 50 bytes */
    blob_reset(&data);
    return 0;
  }
  blob_init(&bestDelta, 0, 0);

  /* Loop over all candidate delta sources */
  for(i=0; i<nSrc; i++){
    int srcid = aSrc[i];
    if( srcid==rid ) continue;
    if( content_is_private(srcid) && !content_is_private(rid) ) continue;

    /* Compute all ancestors of srcid and make sure rid is not one of them.
    ** If rid is an ancestor of srcid, then making rid a decendent of srcid
    ** would create a delta loop. */
    s = srcid;
    while( (s = delta_source_rid(s))>0 ){
      if( s==rid ){
        content_undelta(srcid);
        break;
      }
    }
    if( s!=0 ) continue;

    content_get(srcid, &src);
    if( blob_size(&src)<50 ){
      /* The source is smaller then 50 bytes, so don't bother trying to use it*/
      blob_reset(&src);
      continue;
    }
    blob_delta_create(&src, &data, &delta);
    if( blob_size(&delta) < blob_size(&data)*0.75
     && (bestSrc<=0 || blob_size(&delta)<blob_size(&bestDelta))
    ){
      /* This is the best delta seen so far.  Remember it */
      blob_reset(&bestDelta);
      bestDelta = delta;
      bestSrc = srcid;
    }else{
      /* This delta is not a candidate for becoming the new parent of rid */
      blob_reset(&delta);
    }
    blob_reset(&src);
  }

  /* If there is a winning candidate for the new parent of rid, then
  ** make that candidate the new parent now */
  if( bestSrc>0 ){
    Stmt s1, s2;  /* Statements used to create the delta */
    blob_compress(&bestDelta, &bestDelta);
    db_prepare(&s1, "UPDATE blob SET content=:data WHERE rid=%d", rid);
    db_prepare(&s2, "REPLACE INTO delta(rid,srcid)VALUES(%d,%d)", rid, bestSrc);
    db_bind_blob(&s1, ":data", &bestDelta);
    db_begin_transaction();
    db_exec(&s1);
    db_exec(&s2);
    db_end_transaction(0);
    db_finalize(&s1);
    db_finalize(&s2);
    verify_before_commit(rid);
    rc = 1;
  }
  blob_reset(&data);
  blob_reset(&bestDelta);
  return rc;
}

/*
** COMMAND: test-content-deltify
**
** Usage:  %fossil RID SRCID SRCID ...  [-force]
**
** Convert the content at RID into a delta one of the from SRCIDs.
*/
void test_content_deltify_cmd(void){
  int nSrc;
  int *aSrc;
  int i;
  int bForce = find_option("force",0,0)!=0;
  if( g.argc<3 ) usage("[--force] RID SRCID SRCID...");
  aSrc = fossil_malloc( (g.argc-2)*sizeof(aSrc[0]) );
  nSrc = 0;
  for(i=2; i<g.argc; i++) aSrc[nSrc++] = atoi(g.argv[i]);
  db_must_be_within_tree();
  content_deltify(atoi(g.argv[2]), aSrc, nSrc, bForce);
}

/*
** Return true if Blob p looks like it might be a parsable control artifact.
*/
static int looks_like_control_artifact(Blob *p){
  const char *z = blob_buffer(p);
  int n = blob_size(p);
  if( n<10 ) return 0;
  if( strncmp(z, "-----BEGIN PGP SIGNED MESSAGE-----", 34)==0 ) return 1;
  if( z[0]<'A' || z[0]>'Z' || z[1]!=' ' || z[0]=='I' ) return 0;
  if( z[n-1]!='\n' ) return 0;
  return 1;
}

/*
** COMMAND: test-integrity
**
** Verify that all content can be extracted from the BLOB table correctly.
** If the BLOB table is correct, then the repository can always be
** successfully reconstructed using "fossil rebuild".
**
** Options:
**
**    --parse            Parse all manifests, wikis, tickets, events, and
**                       so forth, reporting any errors found.
*/
void test_integrity(void){
  Stmt q;
  Blob content;
  int n1 = 0;
  int n2 = 0;
  int nErr = 0;
  int total;
  int nCA = 0;
  int anCA[10];
  int bParse = find_option("parse",0,0)!=0;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 2);
  memset(anCA, 0, sizeof(anCA));

  /* Make sure no public artifact is a delta from a private artifact */
  db_prepare(&q,
    "SELECT "
    "   rid, (SELECT uuid FROM blob WHERE rid=delta.rid),"
    "   srcid, (SELECT uuid FROM blob WHERE rid=delta.srcid)"
    "  FROM delta"
    " WHERE srcid in private AND rid NOT IN private"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    const char *zId = db_column_text(&q, 1);
    int srcid = db_column_int(&q, 2);
    const char *zSrc = db_column_text(&q, 3);
    fossil_print(
      "public artifact %S (%d) is a delta from private artifact %S (%d)\n",
      zId, rid, zSrc, srcid
    );
    nErr++;
  }
  db_finalize(&q);

  db_prepare(&q, "SELECT rid, uuid, size FROM blob ORDER BY rid");
  total = db_int(0, "SELECT max(rid) FROM blob");
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    int nUuid = db_column_bytes(&q, 1);
    int size = db_column_int(&q, 2);
    n1++;
    fossil_print("  %d/%d\r", n1, total);
    fflush(stdout);
    if( size<0 ){
      fossil_print("skip phantom %d %s\n", rid, zUuid);
      continue;  /* Ignore phantoms */
    }
    content_get(rid, &content);
    if( blob_size(&content)!=size ){
      fossil_print("size mismatch on artifact %d: wanted %d but got %d\n",
                     rid, size, blob_size(&content));
      nErr++;
    }
    if( !hname_verify_hash(&content, zUuid, nUuid) ){
      fossil_print("wrong hash on artifact %d\n",rid);
      nErr++;
    }
    if( bParse && looks_like_control_artifact(&content) ){
      Blob err;
      int i, n;
      char *z;
      Manifest *p;
      char zFirstLine[400];
      blob_zero(&err);

      z = blob_buffer(&content);
      n = blob_size(&content);
      for(i=0; i<n && z[i] && z[i]!='\n' && i<sizeof(zFirstLine)-1; i++){}
      memcpy(zFirstLine, z, i);
      zFirstLine[i] = 0;
      p = manifest_parse(&content, 0, &err);
      if( p==0 ){
        fossil_print("manifest_parse failed for %s:\n%s\n",
               zUuid, blob_str(&err));
        if( strncmp(blob_str(&err), "line 1:", 7)==0 ){
          fossil_print("\"%s\"\n", zFirstLine);
        }
      }else{
        anCA[p->type]++;
        manifest_destroy(p);
        nCA++;
      }
      blob_reset(&err);
    }else{
      blob_reset(&content);
    }
    n2++;
  }
  db_finalize(&q);
  fossil_print("%d non-phantom blobs (out of %d total) checked:  %d errors\n",
               n2, n1, nErr);
  if( bParse ){
    static const char *const azType[] = { 0, "manifest", "cluster",
        "control", "wiki", "ticket", "attachment", "event" };
    int i;
    fossil_print("%d total control artifacts\n", nCA);
    for(i=1; i<count(azType); i++){
      if( anCA[i] ) fossil_print("  %d %ss\n", anCA[i], azType[i]);
    }
  }
  fossil_print("low-level database integrity-check: ");
  fossil_print("%s\n", db_text(0, "PRAGMA integrity_check(10)"));
}

/*
** COMMAND: test-orphans
**
** Search the repository for orphaned artifacts.
*/
void test_orphans(void){
  Stmt q;
  int cnt = 0;

  db_find_and_open_repository(0, 0);
  db_multi_exec(
    "CREATE TEMP TABLE used(id INTEGER PRIMARY KEY ON CONFLICT IGNORE);"
    "INSERT INTO used SELECT mid FROM mlink;"  /* Manifests */
    "INSERT INTO used SELECT fid FROM mlink;"  /* Files */
    "INSERT INTO used SELECT srcid FROM tagxref WHERE srcid>0;" /* Tags */
    "INSERT INTO used SELECT rid FROM tagxref;" /* Wiki & tickets */
    "INSERT INTO used SELECT rid FROM attachment JOIN blob ON src=uuid;"
    "INSERT INTO used SELECT attachid FROM attachment;"
    "INSERT INTO used SELECT objid FROM event;"
  );
  db_prepare(&q, "SELECT rid, uuid, size FROM blob WHERE rid NOT IN used");
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%7d %s size: %d\n",
      db_column_int(&q, 0),
      db_column_text(&q, 1),
      db_column_int(&q,2));
    cnt++;
  }
  db_finalize(&q);
  fossil_print("%d orphans\n", cnt);
}

/* Allowed flags for check_exists */
#define MISSING_SHUNNED   0x0001    /* Do not report shunned artifacts */

/* This is a helper routine for test-artifacts.
**
** Check to see that artifact zUuid exists in the repository.  If it does,
** return 0.  If it does not, generate an error message and return 1.
*/
static int check_exists(
  const char *zUuid,     /* The artifact we are checking for */
  unsigned flags,        /* Flags */
  Manifest *p,           /* The control artifact that references zUuid */
  const char *zRole,     /* Role of zUuid in p */
  const char *zDetail    /* Additional information, such as a filename */
){
  static Stmt q;
  int rc = 0;

  db_static_prepare(&q, "SELECT size FROM blob WHERE uuid=:uuid");
  if( zUuid==0 || zUuid[0]==0 ) return 0;
  db_bind_text(&q, ":uuid", zUuid);
  if( db_step(&q)==SQLITE_ROW ){
    int size = db_column_int(&q, 0);
    if( size<0 ) rc = 2;
  }else{
    rc = 1;
  }
  db_reset(&q);
  if( rc ){
    const char *zCFType = "control artifact";
    char *zSrc;
    char *zDate;
    const char *zErrType = "MISSING";
    if( db_exists("SELECT 1 FROM shun WHERE uuid=%Q", zUuid) ){
      if( flags & MISSING_SHUNNED ) return 0;
      zErrType = "SHUNNED";
    }
    switch( p->type ){
      case CFTYPE_MANIFEST:   zCFType = "check-in";    break;
      case CFTYPE_CLUSTER:    zCFType = "cluster";     break;
      case CFTYPE_CONTROL:    zCFType = "tag";         break;
      case CFTYPE_WIKI:       zCFType = "wiki";        break;
      case CFTYPE_TICKET:     zCFType = "ticket";      break;
      case CFTYPE_ATTACHMENT: zCFType = "attachment";  break;
      case CFTYPE_EVENT:      zCFType = "event";       break;
    }
    zSrc = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", p->rid);
    if( p->rDate>0.0 ){
      zDate = db_text(0, "SELECT datetime(%.17g)", p->rDate);
    }else{
      zDate = db_text(0,
         "SELECT datetime(rcvfrom.mtime)"
         "  FROM blob, rcvfrom"
         " WHERE blob.rcvid=rcvfrom.rcvid"
         "   AND blob.rid=%d", p->rid);
    }
    fossil_print("%s: %s\n         %s %s %S (%d) %s\n",
                  zErrType, zUuid, zRole, zCFType, zSrc, p->rid, zDate);
    if( zDetail && zDetail[0] ){
      fossil_print("         %s\n", zDetail);
    }
    fossil_free(zSrc);
    fossil_free(zDate);
    rc = 1;
  }
  return rc;
}

/*
** COMMAND: test-missing
**
** Usage: %fossil test-missing
**
** Look at every artifact in the repository and verify that
** all references are satisfied.  Report any referenced artifacts
** that are missing or shunned.
**
** Options:
**
**    --notshunned          Do not report shunned artifacts
**    --quiet               Only show output if there are errors
*/
void test_missing(void){
  Stmt q;
  Blob content;
  int nErr = 0;
  int nArtifact = 0;
  int i;
  Manifest *p;
  unsigned flags = 0;
  int quietFlag;

  if( find_option("notshunned", 0, 0)!=0 ) flags |= MISSING_SHUNNED;
  quietFlag = find_option("quiet","q",0)!=0;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  db_prepare(&q,
     "SELECT mid FROM mlink UNION "
     "SELECT srcid FROM tagxref WHERE srcid>0 UNION "
     "SELECT rid FROM tagxref UNION "
     "SELECT rid FROM attachment JOIN blob ON src=uuid UNION "
     "SELECT objid FROM event");
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    content_get(rid, &content);
    p = manifest_parse(&content, rid, 0);
    if( p ){
      nArtifact++;
      nErr += check_exists(p->zBaseline, flags, p, "baseline of", 0);
      nErr += check_exists(p->zAttachSrc, flags, p, "file of", 0);
      for(i=0; i<p->nFile; i++){
        nErr += check_exists(p->aFile[i].zUuid, flags, p, "file of",
                             p->aFile[i].zName);
      }
      for(i=0; i<p->nParent; i++){
        nErr += check_exists(p->azParent[i], flags, p, "parent of", 0);
      }
      for(i=0; i<p->nCherrypick; i++){
        nErr +=  check_exists(p->aCherrypick[i].zCPTarget+1, flags, p,
                              "cherry-pick target of", 0);
        nErr +=  check_exists(p->aCherrypick[i].zCPBase, flags, p,
                              "cherry-pick baseline of", 0);
      }
      for(i=0; i<p->nCChild; i++){
        nErr += check_exists(p->azCChild[i], flags, p, "in", 0);
      }
      for(i=0; i<p->nTag; i++){
        nErr += check_exists(p->aTag[i].zUuid, flags, p, "target of", 0);
      }
      manifest_destroy(p);
    }
  }
  db_finalize(&q);
  if( nErr>0 || quietFlag==0 ){
    fossil_print("%d missing or shunned references in %d control artifacts\n",
                 nErr, nArtifact);
  }
}

/*
** COMMAND: test-content-erase
**
** Usage: %fossil test-content-erase RID ....
**
** Remove all traces of one or more artifacts from the local repository.
**
** WARNING: This command destroys data and can cause you to lose work.
** Make sure you have a backup copy before using this command!
**
** WARNING: You must run "fossil rebuild" after this command to rebuild
** the metadata.
**
** Note that the arguments are the integer raw RID values from the BLOB table,
** not artifact hashs or labels.
*/
void test_content_erase(void){
  int i;
  Blob x;
  char c;
  Stmt q;
  prompt_user("This command erases information from the repository and\n"
              "might irrecoverably damage the repository.  Make sure you\n"
              "have a backup copy!\n"
              "Continue? (y/N)? ", &x);
  c = blob_str(&x)[0];
  blob_reset(&x);
  if( c!='y' && c!='Y' ) return;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  db_begin_transaction();
  db_prepare(&q, "SELECT rid FROM delta WHERE srcid=:rid");
  for(i=2; i<g.argc; i++){
    int rid = atoi(g.argv[i]);
    fossil_print("Erasing artifact %d (%s)\n",
                 rid, db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid));
    db_bind_int(&q, ":rid", rid);
    while( db_step(&q)==SQLITE_ROW ){
      content_undelta(db_column_int(&q,0));
    }
    db_reset(&q);
    db_multi_exec("DELETE FROM blob WHERE rid=%d", rid);
    db_multi_exec("DELETE FROM delta WHERE rid=%d", rid);
  }
  db_finalize(&q);
  db_end_transaction(0);
}
