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
** check-in iBase.
**
** A "leaf" is a check-in that has no children.  For the purpose
** of finding leaves, children marked with the "newbranch" tag are
** not counted as children.  For example:
**
**
**    A -> B -> C -> D
**          `-> E
**
** D and E are clearly leaves since they have no children.  If
** D has the "newbranch" tag, then C is also a leaf since its only
** child is marked as a newbranch.
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
  Bag seen;       /* Descendants seen */
  Bag pending;    /* Unpropagated descendants */
  Stmt q1;        /* Query to find children of a check-in */
  Stmt q2;        /* Query to detect if a merge is across branches */
  Stmt isBr;      /* Query to check to see if a check-in starts a new branch */
  Stmt ins;       /* INSERT statement for a new record */

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
  ** change iBase to be the root of the entire check-in hierarchy.
  */
  if( iBase<=0 ){
    iBase = db_int(0, "SELECT objid FROM event WHERE type='ci'"
                      " ORDER BY mtime LIMIT 1");
    if( iBase==0 ) return;
  }

  /* Initialize the bags. */
  bag_init(&seen);
  bag_init(&pending);
  bag_insert(&pending, iBase);

  /* This query returns all non-merge children of check-in :rid */
  db_prepare(&q1, "SELECT cid FROM plink WHERE pid=:rid AND isprim");

  /* This query returns all merge children of check-in :rid where
  ** the child and parent are on same branch.  The child and
  ** parent are assumed to be on same branch if they have
  ** the same set of propagated symbolic tags.
  */
  db_prepare(&q2,
     "SELECT cid FROM plink"
     " WHERE pid=:rid AND NOT isprim"
     "   AND (SELECT group_concat(x) FROM ("
     "          SELECT tag.tagid AS x FROM tagxref, tag"
     "           WHERE tagxref.rid=:rid AND tagxref.tagtype=2"
     "             AND tag.tagid=tagxref.tagid AND tagxref.srcid=0"
     "             AND tag.tagname GLOB 'sym-*'"
     "           ORDER BY 1))"
     "    == (SELECT group_concat(x) FROM ("
     "          SELECT tag.tagid AS x FROM tagxref, tag"
     "           WHERE tagxref.rid=plink.cid AND tagxref.tagtype=2"
     "             AND tag.tagid=tagxref.tagid AND tagxref.srcid=0"
     "             AND tag.tagname GLOB 'sym-*'"
     "           ORDER BY 1))"
  );

  /* This query returns a single row if check-in :rid is the first
  ** check-in of a new branch.  In other words, it returns a row if
  ** check-in :rid has the 'newbranch' tag.
  */
  db_prepare(&isBr, 
     "SELECT 1 FROM tagxref WHERE rid=:rid AND tagid=%d AND tagtype=1",
     TAG_NEWBRANCH
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
    if( cnt==0 ){
      db_bind_int(&q2, ":rid", rid);
      if( db_step(&q2)==SQLITE_ROW ){
        cnt++;
      }
      db_reset(&q2);
    }
    if( cnt==0 ){
      db_bind_int(&ins, ":rid", rid);
      db_step(&ins);
      db_reset(&ins);
    }
  }
  db_finalize(&ins);
  db_finalize(&isBr);
  db_finalize(&q2);
  db_finalize(&q1);
  bag_clear(&pending);
  bag_clear(&seen);
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
  @ <a href="%s(g.zBaseURL)/timeline?p=%d(rid)">[timeline]</a>
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
  @ <table width="33%%" align="right" border="1">
  @ <tr><td>
  @ <b>Nomenclature:</b>
  @ <ol>
  @ <li> A <b>leaf</b> is a check-in with no descendants.</li>
  @ <li> An <b>open leaf</b> is a leaf that does not have a "closed" tag
  @ and is thus assumed to still be in use.</li>
  @ <li> A <b>closed leaf</b> has a "closed" tag and is thus assumed to
  @ be historical and no longer in active use.</li>
  @ </ol>
  @ </td></tr></table>
  if( showAll ){
    @ <h1>All leaves, both open and closed</h1>
  }else if( showClosed ){
    @ <h1>Closed leaves only</h1>
  }else{
    @ <h1>All open leaves</h1>
  }
  db_prepare(&q,
    "%s"
    "   AND blob.rid IN leaves"
    " ORDER BY event.mtime DESC",
    timeline_query_for_www()
  );
  www_print_timeline(&q, TIMELINE_LEAFONLY, leaves_extra);
  db_finalize(&q);
  @ <br clear="both">
  @ <script>
  @ function xin(id){
  @ }
  @ function xout(id){
  @ }
  @ </script>
  style_footer();
}
