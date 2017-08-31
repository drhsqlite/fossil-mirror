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
** This file contains code used to find the most recent common
** ancestor of time versions of the same file.  We call this
** common ancestor the "pivot" in a 3-way merge.
*/
#include "config.h"
#include "pivot.h"
#include <assert.h>


/*
** Set the primary file.  The primary version is one of the two
** files that have a common ancestor.  The other file is the secondary.
** There can be multiple secondaries but only a single primary.
** The primary must be set first.
**
** In the merge algorithm, the file being merged in is the primary.
** The current check-out or other files that have been merged into
** the current checkout are the secondaries.
**
** The act of setting the primary resets the pivot-finding algorithm.
*/
void pivot_set_primary(int rid){
  /* Set up table used to do the search */
  db_multi_exec(
    "CREATE TEMP TABLE IF NOT EXISTS aqueue("
    "  rid INTEGER PRIMARY KEY,"  /* The record id for this version */
    "  mtime REAL,"               /* Time when this version was created */
    "  pending BOOLEAN,"          /* True if we have not check this one yet */
    "  src BOOLEAN"               /* 1 for primary.  0 for others */
    ");"
    "DELETE FROM aqueue;"
    "CREATE INDEX IF NOT EXISTS aqueue_idx1 ON aqueue(pending, mtime);"
  );

  /* Insert the primary record */
  db_multi_exec(
    "INSERT INTO aqueue(rid, mtime, pending, src)"
    "  SELECT %d, mtime, 1, 1 FROM event WHERE objid=%d AND type='ci' LIMIT 1",
    rid, rid
  );
}

/*
** Set a secondary file.  The primary file must be set first.  There
** must be at least one secondary but there can be more than one if
** desired.
*/
void pivot_set_secondary(int rid){
  /* Insert the primary record */
  db_multi_exec(
    "INSERT OR IGNORE INTO aqueue(rid, mtime, pending, src)"
    "  SELECT %d, mtime, 1, 0 FROM event WHERE objid=%d AND type='ci'",
    rid, rid
  );
}

/*
** Find the most recent common ancestor of the primary and one of
** the secondaries.  Return its rid.  Return 0 if no common ancestor
** can be found.
**
** If ignoreMerges is true, follow only "primary" parent links.
*/
int pivot_find(int ignoreMerges){
  Stmt q1, q2, u1, i1;
  int rid = 0;

  /* aqueue must contain at least one primary and one other.  Otherwise
  ** we abort early
  */
  if( db_int(0, "SELECT count(distinct src) FROM aqueue")<2 ){
    fossil_fatal("lack both primary and secondary files");
  }

  /* Prepare queries we will be needing
  **
  ** The first query finds the oldest pending version on the aqueue.  This
  ** will be next one searched.
  */
  db_prepare(&q1, "SELECT rid FROM aqueue WHERE pending"
                  " ORDER BY pending DESC, mtime DESC");

  /* Check to see if the record :rid is a common ancestor.  The result
  ** set contains one or more rows if it is and is the empty set if it
  ** is not.
  */
  db_prepare(&q2,
    "SELECT 1 FROM aqueue A, plink, aqueue B"
    " WHERE plink.pid=:rid"
    "   AND plink.cid=B.rid"
    "   AND A.rid=:rid"
    "   AND A.src!=B.src %s",
    ignoreMerges ? "AND plink.isprim" : ""
  );

  /* Mark the :rid record has having been checked.  It is not the
  ** common ancestor.
  */
  db_prepare(&u1,
    "UPDATE aqueue SET pending=0 WHERE rid=:rid"
  );

  /* Add to the queue all ancestors of :rid.
  */
  db_prepare(&i1,
    "INSERT OR IGNORE INTO aqueue "
    "SELECT plink.pid,"
    "       coalesce((SELECT mtime FROM plink X WHERE X.cid=plink.pid), 0.0),"
    "       1,"
    "       aqueue.src "
    "  FROM plink, aqueue"
    " WHERE plink.cid=:rid"
    "   AND aqueue.rid=:rid %s",
    ignoreMerges ? "AND plink.isprim" : ""
  );

  while( db_step(&q1)==SQLITE_ROW ){
    rid = db_column_int(&q1, 0);
    db_reset(&q1);
    db_bind_int(&q2, ":rid", rid);
    if( db_step(&q2)==SQLITE_ROW ){
       break;
    }
    db_reset(&q2);
    db_bind_int(&i1, ":rid", rid);
    db_exec(&i1);
    db_bind_int(&u1, ":rid", rid);
    db_exec(&u1);
    rid = 0;
  }
  db_finalize(&q1);
  db_finalize(&q2);
  db_finalize(&i1);
  db_finalize(&u1);
  return rid;
}

/*
** COMMAND: test-find-pivot
**
** Usage: %fossil test-find-pivot ?options? PRIMARY SECONDARY ...
**
** Test the pivot_find() procedure.
**
** Options:
**    --ignore-merges       Ignore merges for discovering name pivots
*/
void test_find_pivot(void){
  int i, rid;
  int ignoreMerges = find_option("ignore-merges",0,0)!=0;
  int showDetails = find_option("details",0,0)!=0;
  if( g.argc<4 ){
    usage("?options? PRIMARY SECONDARY ...");
  }
  db_must_be_within_tree();
  pivot_set_primary(name_to_rid(g.argv[2]));
  for(i=3; i<g.argc; i++){
    pivot_set_secondary(name_to_rid(g.argv[i]));
  }
  rid = pivot_find(ignoreMerges);
  printf("pivot=%s\n",
         db_text("?","SELECT uuid FROM blob WHERE rid=%d",rid)
  );
  if( showDetails ){
    Stmt q;
    db_prepare(&q,
      "SELECT substr(uuid,1,12), aqueue.rid, datetime(aqueue.mtime),"
             " aqueue.pending, aqueue.src\n"
      "  FROM aqueue JOIN blob ON aqueue.rid=blob.rid\n"
      " ORDER BY aqueue.mtime DESC"
    );
    while( db_step(&q)==SQLITE_ROW ){
      printf("\"%s\",%d,\"%s\",%d,%d\n",
        db_column_text(&q, 0),
        db_column_int(&q, 1),
        db_column_text(&q, 2),
        db_column_int(&q, 3),
        db_column_int(&q, 4));
    }
    db_finalize(&q);
  }
}
