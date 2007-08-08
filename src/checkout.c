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
#include "checkout.h"
#include <assert.h>

/*
** Check to see if there is an existing checkout that has been
** modified.  Return values:
**
**     0:   There is an existing checkout but it is unmodified
**     1:   There is a modified checkout - there are unsaved changes
**     2:   There is no existing checkout
*/
int unsaved_changes(void){
  int vid;
  db_must_be_within_tree();
  vid = db_lget_int("checkout",0);
  if( vid==0 ) return 2;
  vfile_check_signature(vid);
  return db_exists("SELECT 1 FROM vfile WHERE chnged");
}

/*
** Undo the current check-out.  Unlink all files from the disk.
** Clear the VFILE table.
*/
void uncheckout(int vid){
  if( vid==0 ) return;
  vfile_unlink(vid);
  db_multi_exec("DELETE FROM vfile WHERE vid=%d", vid);
}


/*
** Given the abbreviated UUID name of a version, load the content of that
** version in the VFILE table.  Return the VID for the version.
**
** If anything goes wrong, panic.
*/
int load_vfile(const char *zName){
  Blob uuid;
  int vid;

  blob_init(&uuid, zName, -1);
  if( name_to_uuid(&uuid, 1) ){
    fossil_panic(g.zErrMsg);
  }
  vid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &uuid);
  if( vid==0 ){
    fossil_panic("no such version: %s", g.argv[2]);
  }
  load_vfile_from_rid(vid);
  return vid;
}

/*
** Load a vfile from a record ID.
*/
void load_vfile_from_rid(int vid){
  Blob manifest;

  if( db_exists("SELECT 1 FROM vfile WHERE vid=%d", vid) ){
    return;
  }
  content_get(vid, &manifest);
  vfile_build(vid, &manifest);
  blob_reset(&manifest);
}

/*
** Read the manifest file given by vid out of the repository
** and store it in the root of the local check-out.
*/
void manifest_to_disk(int vid){
  char *zManFile;
  Blob manifest;

  blob_zero(&manifest);
  zManFile = mprintf("%smanifest", g.zLocalRoot);
  content_get(vid, &manifest);
  blob_write_to_file(&manifest, zManFile);
  free(zManFile);
  blob_reset(&manifest);
}

/*
** COMMAND: checkout
**
** Check out a version specified on the command-line.
*/
void checkout_cmd(void){
  int forceFlag;
  int noWrite;
  int vid, prior;
  Blob cksum1, cksum1b, cksum2;
  
  db_must_be_within_tree();
  db_begin_transaction();
  forceFlag = find_option("force","f",0)!=0;
  noWrite = find_option("dontwrite",0,0)!=0;
  if( g.argc!=3 ) usage("?--force? VERSION");
  if( !forceFlag && unsaved_changes()==1 ){
    fossil_panic("there are unsaved changes in the current checkout");
  }
  if( forceFlag ){
    db_multi_exec("DELETE FROM vfile");
    prior = 0;
  }else{
    prior = db_lget_int("checkout",0);
  }
  vid = load_vfile(g.argv[2]);
  if( prior==vid ){
    return;
  }
  if( !noWrite ){
    uncheckout(prior);
  }
  db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
  if( !noWrite ){
    vfile_to_disk(vid, 0, 1);
    manifest_to_disk(vid);
    db_lset_int("checkout", vid);
  }
  db_multi_exec("DELETE FROM vmerge");
  vfile_aggregate_checksum_manifest(vid, &cksum1, &cksum1b);
  vfile_aggregate_checksum_disk(vid, &cksum2);
  if( blob_compare(&cksum1, &cksum2) ){
    printf("WARNING: manifest checksum does not agree with disk\n");
  }
  if( blob_compare(&cksum1, &cksum1b) ){
    printf("WARNING: manifest checksum does not agree with manifest\n");
  }
  db_end_transaction(0);
}
