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
** This file contains code used to rebuild the database.
*/
#include "config.h"
#include "rebuild.h"
#include <assert.h>

/*
** Schema changes
*/
static const char zSchemaUpdates[] =
@ -- Index on the delta table
@ --
@ CREATE INDEX IF NOT EXISTS delta_i1 ON delta(srcid);
@
@ -- Artifacts that should not be processed are identified in the
@ -- "shun" table.  Artifacts that are control-file forgeries or
@ -- spam or artifacts whose contents violate administrative policy
@ -- can be shunned in order to prevent them from contaminating
@ -- the repository.
@ --
@ -- Shunned artifacts do not exist in the blob table.  Hence they
@ -- have not artifact ID (rid) and we thus must store their full
@ -- UUID.
@ --
@ CREATE TABLE IF NOT EXISTS shun(uuid UNIQUE);
@
@ -- Artifacts that should not be pushed are stored in the "private"
@ -- table.  
@ --
@ CREATE TABLE IF NOT EXISTS private(rid INTEGER PRIMARY KEY);
@
@ -- An entry in this table describes a database query that generates a
@ -- table of tickets.
@ --
@ CREATE TABLE IF NOT EXISTS reportfmt(
@    rn integer primary key,  -- Report number
@    owner text,              -- Owner of this report format (not used)
@    title text,              -- Title of this report
@    cols text,               -- A color-key specification
@    sqlcode text             -- An SQL SELECT statement for this report
@ );
@
@ -- Some ticket content (such as the originators email address or contact
@ -- information) needs to be obscured to protect privacy.  This is achieved
@ -- by storing an SHA1 hash of the content.  For display, the hash is
@ -- mapped back into the original text using this table.  
@ --
@ -- This table contains sensitive information and should not be shared
@ -- with unauthorized users.
@ --
@ CREATE TABLE IF NOT EXISTS concealed(
@   hash TEXT PRIMARY KEY,
@   content TEXT
@ );
;

/*
** Variables used for progress information
*/
static int totalSize;       /* Total number of artifacts to process */
static int processCnt;      /* Number processed so far */
static int ttyOutput;       /* Do progress output */
static Bag bagDone;         /* Bag of records rebuilt */

/*
** Called after each artifact is processed
*/
static void rebuild_step_done(rid){
  /* assert( bag_find(&bagDone, rid)==0 ); */
  bag_insert(&bagDone, rid);
  if( ttyOutput ){
    processCnt++;
    printf("%d (%d%%)...\r", processCnt, (processCnt*100/totalSize));
    fflush(stdout);
  }
}

/*
** Rebuild cross-referencing information for the artifact
** rid with content pBase and all of its descendants.  This
** routine clears the content buffer before returning.
*/
static void rebuild_step(int rid, int size, Blob *pBase){
  Stmt q1;
  Bag children;
  Blob copy;
  Blob *pUse;
  int nChild, i, cid;

  /* Fix up the "blob.size" field if needed. */
  if( size!=blob_size(pBase) ){
    db_multi_exec(
       "UPDATE blob SET size=%d WHERE rid=%d", blob_size(pBase), rid
    );
  }

  /* Find all children of artifact rid */
  db_prepare(&q1, "SELECT rid FROM delta WHERE srcid=%d", rid);
  bag_init(&children);
  while( db_step(&q1)==SQLITE_ROW ){
    int cid = db_column_int(&q1, 0);
    if( !bag_find(&bagDone, cid) ){
      bag_insert(&children, cid);
    }
  }
  nChild = bag_count(&children);
  db_finalize(&q1);

  /* Crosslink the artifact */
  if( nChild==0 ){
    pUse = pBase;
  }else{
    blob_copy(&copy, pBase);
    pUse = &copy;
  }
  manifest_crosslink(rid, pUse);
  blob_reset(pUse);

  /* Call all children recursively */
  for(cid=bag_first(&children), i=1; cid; cid=bag_next(&children, cid), i++){
    Stmt q2;
    int sz;
    if( nChild==i ){
      pUse = pBase;
    }else{
      blob_copy(&copy, pBase);
      pUse = &copy;
    }
    db_prepare(&q2, "SELECT content, size FROM blob WHERE rid=%d", cid);
    if( db_step(&q2)==SQLITE_ROW && (sz = db_column_int(&q2,1))>=0 ){
      Blob delta;
      db_ephemeral_blob(&q2, 0, &delta);
      blob_uncompress(&delta, &delta);
      blob_delta_apply(pUse, &delta, pUse);
      blob_reset(&delta);
      db_finalize(&q2);
      rebuild_step(cid, sz, pUse);
    }else{
      db_finalize(&q2);
      blob_reset(pUse);
    }
  }
  bag_clear(&children);
  rebuild_step_done(rid);
}

/*
** Core function to rebuild the infomration in the derived tables of a
** fossil repository from the blobs. This function is shared between
** 'rebuild_database' ('rebuild') and 'reconstruct_cmd'
** ('reconstruct'), both of which have to regenerate this information
** from scratch.
**
** If the randomize parameter is true, then the BLOBs are deliberately
** extracted in a random order.  This feature is used to test the
** ability of fossil to accept records in any order and still
** construct a sane repository.
*/
int rebuild_db(int randomize, int doOut){
  Stmt s;
  int errCnt = 0;
  char *zTable;

  bag_init(&bagDone);
  ttyOutput = doOut;
  processCnt = 0;
  db_multi_exec(zSchemaUpdates);
  for(;;){
    zTable = db_text(0,
       "SELECT name FROM sqlite_master"
       " WHERE type='table'"
       " AND name NOT IN ('blob','delta','rcvfrom','user',"
                         "'config','shun','private','reportfmt',"
                         "'concealed')"
    );
    if( zTable==0 ) break;
    db_multi_exec("DROP TABLE %Q", zTable);
    free(zTable);
  }
  db_multi_exec(zRepositorySchema2);
  ticket_create_table(0);
  shun_artifacts();

  db_multi_exec(
     "INSERT INTO unclustered"
     " SELECT rid FROM blob EXCEPT SELECT rid FROM private"
  );
  db_multi_exec(
     "DELETE FROM unclustered"
     " WHERE rid IN (SELECT rid FROM shun JOIN blob USING(uuid))"
  );
  db_multi_exec(
    "DELETE FROM config WHERE name IN ('remote-code', 'remote-maxid')"
  );
  totalSize = db_int(0, "SELECT count(*) FROM blob");
  db_prepare(&s,
     "SELECT rid, size FROM blob"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
     "   AND NOT EXISTS(SELECT 1 FROM delta WHERE rid=blob.rid)"
  );
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      Blob content;
      content_get(rid, &content);
      rebuild_step(rid, size, &content);
    }
  }
  db_finalize(&s);
  db_prepare(&s,
     "SELECT rid, size FROM blob"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
  );
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      if( !bag_find(&bagDone, rid) ){
        Blob content;
        content_get(rid, &content);
        rebuild_step(rid, size, &content);
      }
    }else{
      db_multi_exec("INSERT OR IGNORE INTO phantom VALUES(%d)", rid);
      rebuild_step_done(rid);
    }
  }
  db_finalize(&s);
  if( ttyOutput ){
    printf("\n");
  }
  return errCnt;
}

/*
** COMMAND:  rebuild
**
** Usage: %fossil rebuild REPOSITORY
**
** Reconstruct the named repository database from the core
** records.  Run this command after updating the fossil
** executable in a way that changes the database schema.
*/
void rebuild_database(void){
  int forceFlag;
  int randomizeFlag;
  int errCnt;

  forceFlag = find_option("force","f",0)!=0;
  randomizeFlag = find_option("randomize", 0, 0)!=0;
  if( g.argc!=3 ){
    usage("REPOSITORY-FILENAME");
  }
  db_open_repository(g.argv[2]);
  db_begin_transaction();
  ttyOutput = 1;
  errCnt = rebuild_db(randomizeFlag, 1);
  if( errCnt && !forceFlag ){
    printf("%d errors. Rolling back changes. Use --force to force a commit.\n",
            errCnt);
    db_end_transaction(1);
  }else{
    db_end_transaction(0);
  }
}
