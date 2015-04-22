/*
** Copyright (c) 2011 D. Richard Hipp
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
** This file contains code used to manage the "leaf" table of the
** repository.
**
** The LEAF table contains the rids for all leaves in the check-in DAG.
** A leaf is a check-in that has no children in the same branch.
*/
#include "config.h"
#include "leaf.h"
#include <assert.h>


/*
** Return true if the check-in with RID=rid is a leaf.
**
** A leaf has no children in the same branch.
*/
int is_a_leaf(int rid){
  int rc;
  static const char zSql[] =
    @ SELECT 1 FROM plink
    @  WHERE pid=%d
    @    AND coalesce((SELECT value FROM tagxref
    @                   WHERE tagid=%d AND rid=plink.pid), 'trunk')
    @       =coalesce((SELECT value FROM tagxref
    @                   WHERE tagid=%d AND rid=plink.cid), 'trunk')
  ;
  rc = db_int(0, zSql /*works-like:"%d,%d,%d"*/,
              rid, TAG_BRANCH, TAG_BRANCH);
  return rc==0;
}

/*
** Count the number of primary non-branch children for the given check-in.
**
** A primary child is one where the parent is the primary parent, not
** a merge parent.  A "leaf" is a node that has zero children of any
** kind.  This routine counts only primary children.
**
** A non-branch child is one which is on the same branch as the parent.
*/
int count_nonbranch_children(int pid){
  int nNonBranch = 0;
  static Stmt q;
  static const char zSql[] =
    @ SELECT count(*) FROM plink
    @  WHERE pid=:pid AND isprim
    @    AND coalesce((SELECT value FROM tagxref
    @                   WHERE tagid=%d AND rid=plink.pid), 'trunk')
    @       =coalesce((SELECT value FROM tagxref
    @                   WHERE tagid=%d AND rid=plink.cid), 'trunk')
  ;
  db_static_prepare(&q, zSql /*works-like: "%d,%d"*/, TAG_BRANCH, TAG_BRANCH);
  db_bind_int(&q, ":pid", pid);
  if( db_step(&q)==SQLITE_ROW ){
    nNonBranch = db_column_int(&q, 0);
  }
  db_reset(&q);
  return nNonBranch;
}

/*
** Recompute the entire LEAF table.
**
** This can be expensive (5 seconds or so) for a really large repository.
** So it is only done for things like a rebuild.
*/
void leaf_rebuild(void){
  db_multi_exec(
    "DELETE FROM leaf;"
    "INSERT OR IGNORE INTO leaf"
    "  SELECT cid FROM plink"
    "  EXCEPT"
    "  SELECT pid FROM plink"
    "   WHERE coalesce((SELECT value FROM tagxref"
                       " WHERE tagid=%d AND rid=plink.pid),'trunk')"
         " == coalesce((SELECT value FROM tagxref"
                       " WHERE tagid=%d AND rid=plink.cid),'trunk')",
    TAG_BRANCH, TAG_BRANCH
  );
}

/*
** A bag of check-ins whose leaf status needs to be checked.
*/
static Bag needToCheck;

/*
** Check to see if check-in "rid" is a leaf and either add it to the LEAF
** table if it is, or remove it if it is not.
*/
void leaf_check(int rid){
  static Stmt checkIfLeaf;
  static Stmt addLeaf;
  static Stmt removeLeaf;
  int rc;

  db_static_prepare(&checkIfLeaf,
    "SELECT 1 FROM plink"
    " WHERE pid=:rid"
    "   AND coalesce((SELECT value FROM tagxref"
                    " WHERE tagid=%d AND rid=:rid),'trunk')"
       " == coalesce((SELECT value FROM tagxref"
                    " WHERE tagid=%d AND rid=plink.cid),'trunk');",
    TAG_BRANCH, TAG_BRANCH
  );
  db_bind_int(&checkIfLeaf, ":rid", rid);
  rc = db_step(&checkIfLeaf);
  db_reset(&checkIfLeaf);
  if( rc==SQLITE_ROW ){
    db_static_prepare(&removeLeaf, "DELETE FROM leaf WHERE rid=:rid");
    db_bind_int(&removeLeaf, ":rid", rid);
    db_step(&removeLeaf);
    db_reset(&removeLeaf);
  }else{
    db_static_prepare(&addLeaf, "INSERT OR IGNORE INTO leaf VALUES(:rid)");
    db_bind_int(&addLeaf, ":rid", rid);
    db_step(&addLeaf);
    db_reset(&addLeaf);
  }
}

/*
** Return an SQL expression (stored in memory obtained from fossil_malloc())
** that is true if the SQL variable named "zVar" contains the rid with
** a CLOSED tag.  In other words, return true if the leaf is closed.
**
** The result can be prefaced with a NOT operator to get all leaves that
** are open.
*/
char *leaf_is_closed_sql(const char *zVar){
  return mprintf(
    "EXISTS(SELECT 1 FROM tagxref AS tx"
           " WHERE tx.rid=%s"
             " AND tx.tagid=%d"
             " AND tx.tagtype>0)",
    zVar, TAG_CLOSED
  );
}

/*
** Schedule a leaf check for "rid" and its parents.
*/
void leaf_eventually_check(int rid){
  static Stmt parentsOf;

  db_static_prepare(&parentsOf,
     "SELECT pid FROM plink WHERE cid=:rid AND pid>0"
  );
  db_bind_int(&parentsOf, ":rid", rid);
  bag_insert(&needToCheck, rid);
  while( db_step(&parentsOf)==SQLITE_ROW ){
    bag_insert(&needToCheck, db_column_int(&parentsOf, 0));
  }
  db_reset(&parentsOf);
}

/*
** Do all pending leaf checks.
*/
void leaf_do_pending_checks(void){
  int rid;
  for(rid=bag_first(&needToCheck); rid; rid=bag_next(&needToCheck,rid)){
    leaf_check(rid);
  }
  bag_clear(&needToCheck);
}
