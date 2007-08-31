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
** This file contains code used to find decendents of a version
** or leaves of a version tree.
*/
#include "config.h"
#include "descendents.h"
#include <assert.h>


/*
** Create a temporary table named "leaves" if it does not
** already exist.  Load this table with the RID of all
** versions that are leaves which are decended from
** version iBase.
*/
void compute_leaves(int iBase){
  Bag seen;       /* Descendents seen */
  Bag pending;    /* Unpropagated descendents */

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
** COMMAND:  leaves
**
** Usage: %fossil leaves ?UUID?
** Find all leaf descendents of the current version or of the
** specified version.
*/
void leaves_cmd(void){
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
    "SELECT blob.rid, uuid, datetime(event.mtime,'localtime'), comment, 0,"
    "       (SELECT count(*) FROM plink WHERE cid=blob.rid)"
    "  FROM leaves, blob, event"
    " WHERE blob.rid=leaves.rid"
    "   AND event.objid=leaves.rid"
    " ORDER BY event.mtime DESC"
  );
  print_timeline(&q, 20);
  db_finalize(&q);
}

/*
** COMMAND:  branches
**
** Usage: %fossil branches
** Find leaves of all branches.
*/
void branches_cmd(void){
  Stmt q;

  db_must_be_within_tree();
  db_prepare(&q,
    "SELECT blob.rid, blob.uuid, datetime(event.mtime,'localtime'),"
    "       event.comment, 0,"
    "       (SELECT count(*) FROM plink WHERE cid=blob.rid)"
    "  FROM blob, event"
    " WHERE blob.rid IN"
    "       (SELECT cid FROM plink EXCEPT SELECT pid FROM plink)"
    "   AND event.objid=blob.rid"
    " ORDER BY event.mtime DESC"
  );
  print_timeline(&q, 20);
  db_finalize(&q);
}

#if 0
/*
** WEB PAGE:  leaves
**
** Find leaves of all branches.
*/
void branches_page(void){
  Stmt q;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }

  style_header("Leaves");
  db_prepare(&q,
    "SELECT blob.rid, blob.uuid, datetime(event.mtime,'localtime'),"
    "       event.comment, event.user, 1, 1, 0"
    "  FROM blob, event"
    " WHERE blob.rid IN"
    "       (SELECT cid FROM plink EXCEPT SELECT pid FROM plink)"
    "   AND event.objid=blob.rid"
    " ORDER BY event.mtime DESC"
  );
  www_print_timeline(&q, 0, 0, 0);
  db_finalize(&q);
  @ <script>
  @ function xin(id){
  @ }
  @ function xout(id){
  @ }
  @ </script>
  style_footer();
}
#endif