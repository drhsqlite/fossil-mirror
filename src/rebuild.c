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
** COMMAND:  rebuild
**
** Usage: %fossil rebuild REPOSITORY
**
** Reconstruct the named repository database from the core
** records.  Run this command after updating the fossil
** executable in a way that changes the database schema.
*/
void rebuild_database(void){
  Stmt s;
  int errCnt;
  int forceFlag;
  char *zTable;

  forceFlag = find_option("force","f",0)!=0;
  if( g.argc!=3 ){
    usage("REPOSITORY-FILENAME");
  }
  errCnt = 0;
  db_open_repository(g.argv[2]);
  db_begin_transaction();
  db_multi_exec(
    "CREATE INDEX IF NOT EXISTS delta_i1 ON delta(srcid);"
  );
  for(;;){
    zTable = db_text(0,
       "SELECT name FROM sqlite_master"
       " WHERE type='table'"
       " AND name NOT IN ('blob','delta','rcvfrom','user','config')");
    if( zTable==0 ) break;
    db_multi_exec("DROP TABLE %Q", zTable);
    free(zTable);
  }
  db_multi_exec(zRepositorySchema2);

  db_prepare(&s, "SELECT rid, size FROM blob");
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      Blob content;
      content_get(rid, &content);
      manifest_crosslink(rid, &content);
      blob_reset(&content);
    }else{
      db_multi_exec("INSERT INTO phantom VALUES(%d)", rid);
    }
  }

  if( errCnt && !forceFlag ){
    printf("%d errors. Rolling back changes. Use --force to force a commit.\n",
            errCnt);
    db_end_transaction(1);
  }else{
    db_end_transaction(0);
  }
}
