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
** This file contains code used to find decendants of a version
** or leaves of a version tree.
*/
#include "config.h"
#include "descendants.h"
#include <assert.h>


/*
** Create a temporary table named "leaves" if it does not
** already exist.  Load this table with the RID of all
** check-ins that are leaves which are decended from
** check-in iBase.  If iBase==0, find all leaves within the
** entire check-in hierarchy.
**
** A "leaf" is a check-in that has no children in the same branch.
**
** The closeMode flag determines behavior associated with the "closed"
** tag:
**
**    closeMode==0       Show all leaves regardless of the "closed" tag.
**
**    closeMode==1       Show only leaves without the "closed" tag.
**
**    closeMode==2       Show only leaves with the "closed" tag.
**
** The default behavior is to ignore closed leaves (closeMode==0).  To
** Show all leaves, use closeMode==1.  To show only closed leaves, use
** closeMode==2.
*/
void compute_leaves(int iBase, int closeMode){

  /* Create the LEAVES table if it does not already exist.  Make sure
  ** it is empty.
  */
  db_multi_exec(
    "CREATE TEMP TABLE IF NOT EXISTS leaves("
    "  rid INTEGER PRIMARY KEY"
    ");"
    "DELETE FROM leaves;"
  );

  /* We are checking all descendants of iBase.  If iBase==0, then
  ** use a short-cut to find all leaves anywhere in the hierarchy.
  */
  if( iBase<=0 ){
    db_multi_exec(
      "INSERT OR IGNORE INTO leaves"
      "  SELECT cid FROM plink"
      "  EXCEPT"
      "  SELECT pid FROM plink"
      "   WHERE coalesce((SELECT value FROM tagxref"
                         " WHERE tagid=%d AND rid=plink.pid),'trunk')"
           " == coalesce((SELECT value FROM tagxref"
                         " WHERE tagid=%d AND rid=plink.cid),'trunk');",
      TAG_BRANCH, TAG_BRANCH
    );
  }else{
    Bag seen;     /* Descendants seen */
    Bag pending;  /* Unpropagated descendants */
    Stmt q1;      /* Query to find children of a check-in */
    Stmt isBr;    /* Query to check to see if a check-in starts a new branch */
    Stmt ins;     /* INSERT statement for a new record */

    /* Initialize the bags. */
    bag_init(&seen);
    bag_init(&pending);
    bag_insert(&pending, iBase);

    /* This query returns all non-branch-merge children of check-in :rid.
    **
    ** If a a child is a merge of a fork within the same branch, it is 
    ** returned.  Only merge children in different branches are excluded.
    */
    db_prepare(&q1,
      "SELECT cid FROM plink"
      " WHERE pid=:rid"
      "   AND (isprim"
      "        OR coalesce((SELECT value FROM tagxref"
                        "   WHERE tagid=%d AND rid=plink.pid), 'trunk')"
                 "=coalesce((SELECT value FROM tagxref"
                        "   WHERE tagid=%d AND rid=plink.cid), 'trunk'))",
      TAG_BRANCH, TAG_BRANCH
    );
  
    /* This query returns a single row if check-in :rid is the first
    ** check-in of a new branch.
    */
    db_prepare(&isBr, 
       "SELECT 1 FROM tagxref"
       " WHERE rid=:rid AND tagid=%d AND tagtype=2"
       "   AND srcid>0",
       TAG_BRANCH
    );
  
    /* This statement inserts check-in :rid into the LEAVES table.
    */
    db_prepare(&ins, "INSERT OR IGNORE INTO leaves VALUES(:rid)");
  
    while( bag_count(&pending) ){
      int rid = bag_first(&pending);
      int cnt = 0;
      bag_remove(&pending, rid);
      db_bind_int(&q1, ":rid", rid);
      while( db_step(&q1)==SQLITE_ROW ){
        int cid = db_column_int(&q1, 0);
        if( bag_insert(&seen, cid) ){
          bag_insert(&pending, cid);
        }
        db_bind_int(&isBr, ":rid", cid);
        if( db_step(&isBr)==SQLITE_DONE ){
          cnt++;
        }
        db_reset(&isBr);
      }
      db_reset(&q1);
      if( cnt==0 && !is_a_leaf(rid) ){
        cnt++;
      }
      if( cnt==0 ){
        db_bind_int(&ins, ":rid", rid);
        db_step(&ins);
        db_reset(&ins);
      }
    }
    db_finalize(&ins);
    db_finalize(&isBr);
    db_finalize(&q1);
    bag_clear(&pending);
    bag_clear(&seen);
  }
  if( closeMode==1 ){
    db_multi_exec(
      "DELETE FROM leaves WHERE rid IN"
      "  (SELECT leaves.rid FROM leaves, tagxref"
      "    WHERE tagxref.rid=leaves.rid "
      "      AND tagxref.tagid=%d"
      "      AND tagxref.tagtype>0)",
      TAG_CLOSED
    );
  }else if( closeMode==2 ){
    db_multi_exec(
      "DELETE FROM leaves WHERE rid NOT IN"
      "  (SELECT leaves.rid FROM leaves, tagxref"
      "    WHERE tagxref.rid=leaves.rid "
      "      AND tagxref.tagid=%d"
      "      AND tagxref.tagtype>0)",
      TAG_CLOSED
    );
  }
}

/*
** Load the record ID rid and up to N-1 closest ancestors into
** the "ok" table.
*/
void compute_ancestors(int rid, int N){
  Bag seen;
  PQueue queue;
  bag_init(&seen);
  pqueue_init(&queue);
  bag_insert(&seen, rid);
  pqueue_insert(&queue, rid, 0.0);
  while( (N--)>0 && (rid = pqueue_extract(&queue))!=0 ){
    Stmt q;
    db_multi_exec("INSERT OR IGNORE INTO ok VALUES(%d)", rid);
    db_prepare(&q,
       "SELECT a.pid, b.mtime FROM plink a LEFT JOIN plink b ON b.cid=a.pid"
       " WHERE a.cid=%d", rid
    );
    while( db_step(&q)==SQLITE_ROW ){
      int pid = db_column_int(&q, 0);
      double mtime = db_column_double(&q, 1);
      if( bag_insert(&seen, pid) ){
        pqueue_insert(&queue, pid, -mtime);
      }
    }
    db_finalize(&q);
  }
  bag_clear(&seen);
  pqueue_clear(&queue);
}

/*
** Load the record ID rid and up to N-1 closest descendants into
** the "ok" table.
*/
void compute_descendants(int rid, int N){
  Bag seen;
  PQueue queue;
  bag_init(&seen);
  pqueue_init(&queue);
  bag_insert(&seen, rid);
  pqueue_insert(&queue, rid, 0.0);
  while( (N--)>0 && (rid = pqueue_extract(&queue))!=0 ){
    Stmt q;
    db_multi_exec("INSERT OR IGNORE INTO ok VALUES(%d)", rid);
    db_prepare(&q,"SELECT cid, mtime FROM plink WHERE pid=%d", rid);
    while( db_step(&q)==SQLITE_ROW ){
      int pid = db_column_int(&q, 0);
      double mtime = db_column_double(&q, 1);
      if( bag_insert(&seen, pid) ){
        pqueue_insert(&queue, pid, mtime);
      }
    }
    db_finalize(&q);
  }
  bag_clear(&seen);
  pqueue_clear(&queue);
}

/*
** COMMAND:  descendants
**
** Usage: %fossil descendants ?BASELINE-ID?
**
** Find all leaf descendants of the baseline specified or if the argument
** is omitted, of the baseline currently checked out.
*/
void descendants_cmd(void){
  Stmt q;
  int base;

  db_must_be_within_tree();
  if( g.argc==2 ){
    base = db_lget_int("checkout", 0);
  }else{
    base = name_to_rid(g.argv[2]);
  }
  if( base==0 ) return;
  compute_leaves(base, 0);
  db_prepare(&q,
    "%s"
    "   AND event.objid IN (SELECT rid FROM leaves)"
    " ORDER BY event.mtime DESC",
    timeline_query_for_tty()
  );
  print_timeline(&q, 20);
  db_finalize(&q);
}

/*
** COMMAND:  leaves
**
** Usage: %fossil leaves ?--all? ?--closed?
**
** Find leaves of all branches.  By default show only open leaves.
** The --all flag causes all leaves (closed and open) to be shown.
** The --closed flag shows only closed leaves.
*/
void leaves_cmd(void){
  Stmt q;
  int showAll = find_option("all", 0, 0)!=0;
  int showClosed = find_option("closed", 0, 0)!=0;

  db_must_be_within_tree();
  compute_leaves(0, showAll ? 0 : showClosed ? 2 : 1);
  db_prepare(&q,
    "%s"
    "   AND blob.rid IN leaves"
    " ORDER BY event.mtime DESC",
    timeline_query_for_tty()
  );
  print_timeline(&q, 2000);
  db_finalize(&q);
}

/*
** This routine is called while for each check-in that is rendered by
** the "leaves" page.  Add some additional hyperlink to show the 
** ancestors of the leaf.
*/
static void leaves_extra(int rid){
  if( g.okHistory ){
    @ <a href="%s(g.zTop)/timeline?p=%d(rid)">[timeline]</a>
  }
}

/*
** WEBPAGE:  leaves
**
** Find leaves of all branches.
*/
void leaves_page(void){
  Stmt q;
  int showAll = P("all")!=0;
  int showClosed = P("closed")!=0;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }

  if( !showAll ){
    style_submenu_element("All", "All", "leaves?all");
  }
  if( !showClosed ){
    style_submenu_element("Closed", "Closed", "leaves?closed");
  }
  if( showClosed || showAll ){
    style_submenu_element("Open", "Open", "leaves");
  }
  style_header("Leaves");
  login_anonymous_available();
  compute_leaves(0, showAll ? 0 : showClosed ? 2 : 1);
  style_sidebox_begin("Nomenclature:", "33%");
  @ <ol>
  @ <li> A <div class="sideboxDescribed">leaf</div>
  @ is a check-in with no descendants in the same branch.</li>
  @ <li> An <div class="sideboxDescribed">open leaf</div>
  @ is a leaf that does not have a "closed" tag
  @ and is thus assumed to still be in use.</li>
  @ <li> A <div class="sideboxDescribed">closed leaf</div>
  @ has a "closed" tag and is thus assumed to
  @ be historical and no longer in active use.</li>
  @ </ol>
  style_sidebox_end();

  if( showAll ){
    @ <h1>All leaves, both open and closed:</h1>
  }else if( showClosed ){
    @ <h1>Closed leaves:</h1>
  }else{
    @ <h1>Open leaves:</h1>
  }
  db_prepare(&q,
    "%s"
    "   AND blob.rid IN leaves"
    " ORDER BY event.mtime DESC",
    timeline_query_for_www()
  );
  www_print_timeline(&q, TIMELINE_LEAFONLY, 0, 0, leaves_extra);
  db_finalize(&q);
  @ <br />
  @ <script  type="text/JavaScript">
  @ function xin(id){
  @ }
  @ function xout(id){
  @ }
  @ </script>
  style_footer();
}
