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
** This file contains code used to check-out versions of the project
** from the local repository.
*/
#include "config.h"
#include "add.h"
#include <assert.h>


/*
** COMMAND: add
**
** Usage: %fossil add FILE...
**
** Make arrangements to add one or more files to the current checkout 
** at the next commit.
*/
void add_cmd(void){
  int i;
  int vid;

  db_must_be_within_tree();
  vid = db_lget_int("checkout",0);
  if( vid==0 ){
    fossil_panic("no checkout to add to");
  }
  db_begin_transaction();
  for(i=2; i<g.argc; i++){
    char *zName;
    char *zPath;
    Blob pathname;
    int isDir;

    zName = mprintf("%/", g.argv[i]);
    isDir = file_isdir(zName);
    if( isDir==1 ) continue;
    if( isDir==0 ){
      fossil_fatal("not found: %s", zName);
    }
    if( isDir==2 && access(zName, R_OK) ){
      fossil_fatal("cannot open %s", zName);
    }
    file_tree_name(zName, &pathname);
    zPath = blob_str(&pathname);
    if( strcmp(zPath, "manifest")==0 || strcmp(zPath, "_FOSSIL_")==0 ){
      fossil_fatal("cannot add %s", zPath);
    }
    if( !file_is_simple_pathname(zPath) ){
      fossil_fatal("filename contains illegal characters: %s", zPath);
    }
    if( db_exists("SELECT 1 FROM vfile WHERE pathname=%Q", zPath) ){
      db_multi_exec("UPDATE vfile SET deleted=0 WHERE pathname=%Q", zPath);
    }else{
      db_multi_exec(
        "INSERT INTO vfile(vid,deleted,rid,mrid,pathname)"
        "VALUES(%d,0,0,0,%Q)", vid, zPath);
    }
    blob_reset(&pathname);
    free(zName);
  }
  db_end_transaction(0);
}

/*
** COMMAND: rm
** COMMAND: del
**
** Usage: %fossil rm FILE...
**    or: %fossil del FILE...
** Remove one or more files from the tree.
*/
void del_cmd(void){
  int i;
  int vid;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_panic("no checkout to remove from");
  }
  db_begin_transaction();
  for(i=2; i<g.argc; i++){
    char *zName;
    char *zPath;
    Blob pathname;

    zName = mprintf("%/", g.argv[i]);
    file_tree_name(zName, &pathname);
    zPath = blob_str(&pathname);
    if( !db_exists(
             "SELECT 1 FROM vfile WHERE pathname=%Q AND NOT deleted", zPath) ){
      fossil_fatal("not in the repository: %s", zName);
    }else{
      db_multi_exec("UPDATE vfile SET deleted=1 WHERE pathname=%Q", zPath);
    }
    blob_reset(&pathname);
    free(zName);
  }
  db_multi_exec("DELETE FROM vfile WHERE deleted AND rid=0");
  db_end_transaction(0);
}
