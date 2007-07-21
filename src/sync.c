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
** This file contains code used to push, pull, and sync a repository
*/
#include "config.h"
#include "sync.h"
#include <assert.h>

/*
** COMMAND: pull
**
** Pull changes in a remote repository into the local repository
*/
void pull_cmd(void){
  if( g.argc!=3 ){
    usage("FILE-OR-URL");
  }
  url_parse(g.argv[2]);
  db_must_be_within_tree();
  user_select();
  if( g.urlIsFile ){
    Stmt q;
    char *zRemote = g.urlName;
    if( !file_isfile(zRemote) ){
      zRemote = mprintf("%s/_FOSSIL_");
    }
    if( !file_isfile(zRemote) ){
      fossil_panic("no such repository: %s", zRemote);
    }
    db_multi_exec("ATTACH DATABASE %Q AS other", zRemote);
    db_begin_transaction();
    db_prepare(&q, 
      "SELECT rid FROM other.blob WHERE NOT EXISTS"
      " (SELECT 1 FROM blob WHERE uuid=other.blob.uuid)"
    );
    while( db_step(&q)==SQLITE_ROW ){
      int nrid;
      int rid = db_column_int(&q, 0);
      Blob rec;
      content_get_from_db(rid, &rec, "other");
      nrid = content_put(&rec, 0);
      manifest_crosslink(nrid, &rec);
    }
    db_finalize(&q);
    db_end_transaction(0);
  }else{
    client_sync(0,1,0);
  }
}

/*
** COMMAND: push
**
** Push changes in the local repository over into a remote repository
*/
void push_cmd(void){
  if( g.argc!=3 ){
    usage("FILE-OR-URL");
  }
  url_parse(g.argv[2]);
  db_must_be_within_tree();
  if( g.urlIsFile ){
    Blob remote;
    char *zRemote;
    file_canonical_name(g.urlName, &remote);
    zRemote = blob_str(&remote);
    if( file_isdir(zRemote)!=1 ){
      int i = strlen(zRemote);
      while( i>0 && zRemote[i]!='/' ){ i--; }
      zRemote[i] = 0;
    }
    if( chdir(zRemote) ){
      fossil_panic("unable to change the working directory to %s", zRemote);
    }
    db_close();
    g.argv[2] = g.zLocalRoot;
    pull_cmd();
  }else{
    client_sync(1,0,0);
  }
}


/*
** COMMAND: sync
**
** Synchronize the local repository with a remote repository
*/
void sync_cmd(void){
  if( g.argc!=3 ){
    usage("FILE-OR-URL");
  }
  url_parse(g.argv[2]);
  if( g.urlIsFile ){
    pull_cmd();
    db_close();
    push_cmd();
  }else{
    db_must_be_within_tree();
    client_sync(1,1,0);
  }
}
