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
** Macros for debugging
*/
#if 0
# define CONTENT_TRACE(X)  printf X;
#else
# define CONTENT_TRACE(X)
#endif

/*
** The artifact retrival cache
*/
#define MX_CACHE_CNT  50    /* Maximum number of positive cache entries */
#define EXPELL_INTERVAL 5   /* How often to expell from a full cache */
static struct {
  int n;               /* Current number of positive cache entries */
  int nextAge;         /* Age counter for implementing LRU */
  int skipCnt;         /* Used to limit entries expelled from cache */
  struct {             /* One instance of this for each cache entry */
    int rid;                  /* Artifact id */
    int age;                  /* Age.  Newer is larger */
    Blob content;             /* Content of the artifact */
  } a[MX_CACHE_CNT];   /* The positive cache */

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
** Clear the content cache.
*/
void content_clear_cache(void){
  int i;
  for(i=0; i<contentCache.n; i++){
    blob_reset(&contentCache.a[i].content);
  }
  bag_clear(&contentCache.missing);
  bag_clear(&contentCache.available);
  contentCache.n = 0;
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
  if( content_is_available(srcid) ){
    bag_insert(&contentCache.available, rid);
    return 1;
  }else{
    bag_insert(&contentCache.missing, rid);
    return 0;
  }
}

/*
** Mark artifact rid as being available now.  Update the cache to
** show that everything that was formerly unavailable because rid
** was missing is now available.
*/
static void content_mark_available(int rid){
  Bag pending;
  Stmt q;
  if( bag_find(&contentCache.available, rid) ) return;
  bag_init(&pending);
  bag_insert(&pending, rid);
  while( (rid = bag_first(&pending))!=0 ){
    bag_remove(&pending, rid);
    bag_remove(&contentCache.missing, rid);
    bag_insert(&contentCache.available, rid);
    db_prepare(&q, "SELECT rid FROM delta WHERE srcid=%d", rid);
    while( db_step(&q)==SQLITE_ROW ){
      int nx = db_column_int(&q, 0);
      bag_insert(&pending, nx);
    }
    db_finalize(&q);
  }
  bag_clear(&pending);
}

/*
** Extract the content for ID rid and put it into the
** uninitialized blob.  Return 1 on success.  If the record
** is a phantom, zero pBlob and return 0.
*/
int content_get(int rid, Blob *pBlob){
  Stmt q;
  Blob src;
  int srcid;
  int rc = 0;
  int i;
  static Bag inProcess;

  assert( g.repositoryOpen );
  blob_zero(pBlob);

  /* Early out if we know the content is not available */
  if( bag_find(&contentCache.missing, rid) ){
    CONTENT_TRACE(("%*smiss from cache: %d\n",
                    bag_count(&inProcess), "", rid))
    return 0;
  }

  /* Look for the artifact in the cache first */
  for(i=0; i<contentCache.n; i++){
    if( contentCache.a[i].rid==rid ){
      *pBlob = contentCache.a[i].content;
      blob_zero(&contentCache.a[i].content);
      contentCache.n--;
      if( i<contentCache.n ){
        contentCache.a[i] = contentCache.a[contentCache.n];
      }
      CONTENT_TRACE(("%*shit cache: %d\n", 
                    bag_count(&inProcess), "", rid))
      return 1;
    }
  }

  /* See if we need to apply a delta to find this artifact */
  srcid = findSrcid(rid);
  CONTENT_TRACE(("%*ssearching for %d.  Need %d.\n",
                 bag_count(&inProcess), "", rid, srcid))


  if( srcid ){
    /* Yes, a delta is required */
    if( bag_find(&inProcess, srcid) ){
      db_multi_exec(
        "UPDATE blob SET content=NULL, size=-1 WHERE rid=%d;"
        "DELETE FROM delta WHERE rid=%d;"
        "INSERT OR IGNORE INTO phantom VALUES(%d);",
        srcid, srcid, srcid
      );
      blob_zero(pBlob);
      return 0;
    }
    bag_insert(&inProcess, srcid);

    if( content_get(srcid, &src) ){
      db_prepare(&q, "SELECT content FROM blob WHERE rid=%d AND size>=0", rid);
      if( db_step(&q)==SQLITE_ROW ){
        Blob delta;
        db_ephemeral_blob(&q, 0, &delta);
        blob_uncompress(&delta, &delta);
        blob_init(pBlob,0,0);
        blob_delta_apply(&src, &delta, pBlob);
        blob_reset(&delta);
        rc = 1;
      }
      db_finalize(&q);

      /* Save the srcid artifact in the cache */
      if( contentCache.n<MX_CACHE_CNT ){
        i = contentCache.n++;
      }else if( ((contentCache.skipCnt++)%EXPELL_INTERVAL)!=0 ){
        i = -1;
      }else{
        int j, best;
        best = contentCache.nextAge+1;
        i = -1;
        for(j=0; j<contentCache.n; j++){
          if( contentCache.a[j].age<best ){
            i = j;
            best = contentCache.a[j].age;
          }
        }
        CONTENT_TRACE(("%*sexpell %d from cache\n",
                       bag_count(&inProcess), "", contentCache.a[i].rid))
        blob_reset(&contentCache.a[i].content);
      }
      if( i>=0 ){
        contentCache.a[i].content = src;
        contentCache.a[i].age = contentCache.nextAge++;
        contentCache.a[i].rid = srcid;
        CONTENT_TRACE(("%*sadd %d to cache\n",
                       bag_count(&inProcess), "", srcid))
      }else{
        blob_reset(&src);
      }
    }
    bag_remove(&inProcess, srcid);
  }else{
    /* No delta required.  Read content directly from the database */
    db_prepare(&q, "SELECT content FROM blob WHERE rid=%d AND size>=0", rid);
    if( db_step(&q)==SQLITE_ROW ){
      db_ephemeral_blob(&q, 0, pBlob);
      blob_uncompress(pBlob, pBlob);
      rc = 1;
    }
    db_finalize(&q);
  }
  if( rc==0 ){
    bag_insert(&contentCache.missing, rid);
  }else{
    bag_insert(&contentCache.available, rid);
  }
  return rc;
}

/*
** Get the contents of a file within a given baseline.
*/
int content_get_historical_file(
  const char *revision,    /* Name of the baseline containing the file */
  const char *file,        /* Name of the file */
  Blob *content            /* Write file content here */
){
  Blob mfile;
  Manifest m;
  int i, rid=0;
  
  rid = name_to_rid(revision);
  content_get(rid, &mfile);
  
  if( manifest_parse(&m, &mfile) ){
    for(i=0; i<m.nFile; i++){
      if( strcmp(m.aFile[i].zName, file)==0 ){
        rid = uuid_to_rid(m.aFile[i].zUuid, 0);
        return content_get(rid, content);
      }
    }
    fossil_panic("file: %s does not exist in revision: %s", file, revision);
  }else{
    fossil_panic("could not parse manifest for revision: %s", revision);
  }
  
  return 0;
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
** When a record is converted from a phantom to a real record,
** if that record has other records that are derived by delta,
** then call manifest_crosslink() on those other records.
*/
void after_dephantomize(int rid, int linkFlag){
  Stmt q;
  db_prepare(&q, "SELECT rid FROM delta WHERE srcid=%d", rid);
  while( db_step(&q)==SQLITE_ROW ){
    int tid = db_column_int(&q, 0);
    after_dephantomize(tid, 1);
  }
  db_finalize(&q);
  if( linkFlag ){
    Blob content;
    content_get(rid, &content);
    manifest_crosslink(rid, &content);
    blob_reset(&content);
  }
}

/*
** Write content into the database.  Return the record ID.  If the
** content is already in the database, just return the record ID.
**
** If srcId is specified, then pBlob is delta content from
** the srcId record.  srcId might be a phantom.
**
** A phantom is written if pBlob==0.  If pBlob==0 or if srcId is
** specified then the UUID is set to zUuid.  Otherwise zUuid is
** ignored.  In the future this might change such that the content
** hash is checked against zUuid to make sure it is correct.
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
  if( pBlob && srcId==0 ){
    sha1sum_blob(pBlob, &hash);
  }else{
    blob_init(&hash, zUuid, -1);
  }
  if( pBlob==0 ){
    size = -1;
  }else{
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
    if( pBlob ){
      blob_compress(pBlob, &cmpr);
      db_bind_blob(&s1, ":data", &cmpr);
    }
    db_exec(&s1);
    rid = db_last_insert_rowid();
    if( !pBlob ){
      db_multi_exec("INSERT OR IGNORE INTO phantom VALUES(%d)", rid);
    }
  }

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
** Change the storage of rid so that it is a delta of srcid.
**
** If rid is already a delta from some other place then no
** conversion occurs and this is a no-op unless force==1.
**
** If srcid is a delta that depends on rid, then srcid is
** converted to undeltaed text.
**
** If either rid or srcid contain less than 50 bytes, or if the
** resulting delta does not achieve a compression of at least 25% on
** its own the rid is left untouched.
**
** NOTE: IMHO the creation of the delta should be defered until after
** the blob sizes have been checked. Doing it before the check as is
** done now the code will generate a delta just to immediately throw
** it away, wasting space and time.
*/
void content_deltify(int rid, int srcid, int force){
  int s;
  Blob data, src, delta;
  Stmt s1, s2;
  if( srcid==rid ) return;
  if( !force && findSrcid(rid)>0 ) return;
  s = srcid;
  while( (s = findSrcid(s))>0 ){
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
