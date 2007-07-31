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
** This file contains code used to clone a repository
*/
#include "config.h"
#include "clone.h"
#include <assert.h>



/*
** COMMAND: clone
**
** Make a clone of a repository in the local directory
**
**    fossil clone FILE-OR-URL NEWDATABASE
*/
void clone_cmd(void){
  if( g.argc!=4 ){
    usage("FILE-OR-URL NEW-REPOSITORY");
  }
  if( file_size(g.argv[3])>0 ){
    fossil_panic("file already exists: %s", g.argv[3]);
  }
  url_parse(g.argv[2]);
  db_create_repository(g.argv[3]);
  db_open_repository(g.argv[3]);
  user_select();
  db_set("content-schema", CONTENT_SCHEMA);
  db_set("aux-schema", AUX_SCHEMA);
  if( !g.urlIsFile ){
    db_set("last-sync-url", g.argv[2]);
  }
  db_multi_exec(
    "INSERT INTO config(name,value) VALUES('server-code', hex(randomblob(20)));"
  );
   if( g.urlIsFile ){
    Stmt q;
    db_multi_exec("ATTACH DATABASE %Q AS orig", g.urlName);
    db_begin_transaction();
    db_prepare(&q, 
      "SELECT name FROM orig.sqlite_master"
      " WHERE type='table'"
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zTab = db_column_text(&q, 0);
      db_multi_exec("INSERT OR IGNORE INTO %Q SELECT * FROM orig.%Q",
                    zTab, zTab);
    }
    db_finalize(&q);
    db_end_transaction(0);
  }else{
    client_sync(0,0,1);
  }
}
