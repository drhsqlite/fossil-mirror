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
** versions that are leaves which are decended from
** version iBase.
*/
void compute_leaves(int iBase){
  Bag seen;       /* Descendants seen */
  Bag pending;    /* Unpropagated descendants */

  db_multi_exec(
    "CREATE TEMP TABLE IF NOT EXISTS leaves("
    "  rid INTEGER PRIMARY KEY"
    ");"
    "DELETE FROM leaves;"
  );
  bag_init(&seen);
  bag_init(&pending);
  bag_insert(&pending, iBase);
  while( bag_count(&pending) ){
    int rid = bag_first(&pending);
    int cnt = 0;
    Stmt q;
    bag_remove(&pending, rid);
    db_prepare(&q, "SELECT cid FROM plink WHERE pid=%d", rid);
    while( db_step(&q)==SQLITE_ROW ){
      int cid = db_column_int(&q, 0);
      if( bag_insert(&seen, cid) ){
        bag_insert(&pending, cid);
      }
      cnt++;
    }
    db_finalize(&q);
    if( cnt==0 ){
      db_multi_exec("INSERT INTO leaves VALUES(%d)", rid);
    }
  }
  bag_clear(&pending);
  bag_clear(&seen);
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
  compute_leaves(base);
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
** Usage: %fossil leaves
**
** Find leaves of all branches.
*/
void branches_cmd(void){
  Stmt q;

  db_must_be_within_tree();
  db_prepare(&q,
    "%s"
    "   AND blob.rid IN"
    "       (SELECT cid FROM plink EXCEPT SELECT pid FROM plink)"
    " ORDER BY event.mtime DESC",
    timeline_query_for_tty()
  );
  print_timeline(&q, 2000);
  db_finalize(&q);
}

/*
** WEBPAGE:  leaves
**
** Find leaves of all branches.
*/
void leaves_page(void){
  Stmt q;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }

  style_header("Leaves");
  login_anonymous_available();
  db_prepare(&q,
    "%s"
    "   AND blob.rid IN"
    "       (SELECT cid FROM plink EXCEPT SELECT pid FROM plink)"
    " ORDER BY event.mtime DESC",
    timeline_query_for_www()
  );
  www_print_timeline(&q);
  db_finalize(&q);
  @ <script>
  @ function xin(id){
  @ }
  @ function xout(id){
  @ }
  @ </script>
  style_footer();
}
