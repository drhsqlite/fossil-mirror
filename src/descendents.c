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
** versions that are leaves are which are decended from
** version iBase.
*/
void compute_leaves(int iBase){
  int generation = 0;
  int chngCnt = 0;

  db_multi_exec(
    "CREATE TEMP TABLE IF NOT EXISTS leaves("
    "  rid INTEGER PRIMARY KEY,"
    "  generation INTEGER"
    ");"
    "DELETE FROM leaves;"
    "INSERT INTO leaves VALUES(%d,0);",
    iBase
  );
  do{
    db_multi_exec(
      "INSERT OR IGNORE INTO leaves(rid,generation) "
      "SELECT cid, %d FROM plink"
      " WHERE pid IN (SELECT rid FROM leaves WHERE generation=%d)",
      generation+1, generation
    );
    generation++;
    chngCnt = db_changes();
  }while( chngCnt>0 );
  db_multi_exec(
    "DELETE FROM leaves"
    " WHERE EXISTS(SELECT 1 FROM plink WHERE pid=rid)"
  );
}

/*
** COMMAND:  leaves
**
** Find all leaf versions
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
    "SELECT uuid, datetime(event.mtime,'localtime'), comment"
    "  FROM leaves, blob, event"
    " WHERE blob.rid=leaves.rid"
    "   AND event.objid=leaves.rid"
    " ORDER BY event.mtime DESC"
  );
  print_timeline(&q, 20);
  db_finalize(&q);
}
