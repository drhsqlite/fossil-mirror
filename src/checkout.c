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
  vfile_check_signature(vid, 1);
  return db_exists("SELECT 1 FROM vfile WHERE chnged"
                   " OR coalesce(origname!=pathname,0)");
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
    fossil_fatal("no such check-in: %s", g.argv[2]);
  }
  if( !is_a_version(vid) ){
    fossil_fatal("object [%.10s] is not a check-in", blob_str(&uuid));
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
** Set or clear the vfile.isexe flag for a file.
*/
static void set_or_clear_isexe(const char *zFilename, int vid, int onoff){
  db_multi_exec("UPDATE vfile SET isexe=%d WHERE vid=%d and pathname=%Q",
                onoff, vid, zFilename);
}

/*
** Read the manifest file given by vid out of the repository
** and store it in the root of the local check-out.
*/
void manifest_to_disk(int vid){
  char *zManFile;
  Blob manifest;
  Blob hash;
  Blob filename;
  int baseLen;
  int i;
  Manifest m;

  blob_zero(&manifest);
  zManFile = mprintf("%smanifest", g.zLocalRoot);
  content_get(vid, &manifest);
  blob_write_to_file(&manifest, zManFile);
  free(zManFile);
  blob_zero(&hash);
  sha1sum_blob(&manifest, &hash);
  zManFile = mprintf("%smanifest.uuid", g.zLocalRoot);
  blob_append(&hash, "\n", 1);
  blob_write_to_file(&hash, zManFile);
  free(zManFile);
  blob_reset(&hash);
  manifest_parse(&m, &manifest);
  blob_zero(&filename);
  blob_appendf(&filename, "%s/", g.zLocalRoot);
  baseLen = blob_size(&filename);
  for(i=0; i<m.nFile; i++){ 
    int isExe;
    blob_append(&filename, m.aFile[i].zName, -1);
    isExe = m.aFile[i].zPerm && strstr(m.aFile[i].zPerm, "x");
    file_setexe(blob_str(&filename), isExe);
    set_or_clear_isexe(m.aFile[i].zName, vid, isExe);
    blob_resize(&filename, baseLen);
  }
  blob_reset(&filename);
  manifest_clear(&m);
}

/*
** COMMAND: checkout
**
** Usage: %fossil checkout VERSION ?-f|--force? ?--keep?
**
** Check out a version specified on the command-line.  This command
** will abort if there are edited files in the current checkout unless
** the --force option appears on the command-line.  The --keep option
** leaves files on disk unchanged, except the manifest and manifest.uuid
** files.
**
** The --latest flag can be used in place of VERSION to checkout the
** latest version in the repository.
**
** See also the "update" command.
*/
void checkout_cmd(void){
  int forceFlag;                 /* Force checkout even if edits exist */
  int keepFlag;                  /* Do not change any files on disk */
  int latestFlag;                /* Checkout the latest version */
  char *zVers;                   /* Version to checkout */
  int vid, prior;
  Blob cksum1, cksum1b, cksum2;
  
  db_must_be_within_tree();
  db_begin_transaction();
  forceFlag = find_option("force","f",0)!=0;
  keepFlag = find_option("keep",0,0)!=0;
  latestFlag = find_option("latest",0,0)!=0;
  if( (latestFlag!=0 && g.argc!=2) || (latestFlag==0 && g.argc!=3) ){
     usage("VERSION|--latest ?--force? ?--keep?");
  }
  if( !forceFlag && unsaved_changes()==1 ){
    fossil_fatal("there are unsaved changes in the current checkout");
  }
  if( forceFlag ){
    db_multi_exec("DELETE FROM vfile");
    prior = 0;
  }else{
    prior = db_lget_int("checkout",0);
  }
  if( latestFlag ){
    compute_leaves(db_lget_int("checkout",0), 1);
    zVers = db_text(0, "SELECT uuid FROM leaves, event, blob"
                       " WHERE event.objid=leaves.rid AND blob.rid=leaves.rid"
                       " ORDER BY event.mtime DESC");
    if( zVers==0 ){
      zVers = db_text(0, "SELECT uuid FROM event, blob"
                         " WHERE event.objid=blob.rid AND event.type='ci'"
                         " ORDER BY event.mtime DESC");
    }
    if( zVers==0 ){
      fossil_fatal("cannot locate \"latest\" checkout");
    }
  }else{
    zVers = g.argv[2];
  }
  vid = load_vfile(zVers);
  if( prior==vid ){
    return;
  }
  if( !keepFlag ){
    uncheckout(prior);
  }
  db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
  if( !keepFlag ){
    vfile_to_disk(vid, 0, 1);
  }
  manifest_to_disk(vid);
  db_lset_int("checkout", vid);
  undo_reset();
  db_multi_exec("DELETE FROM vmerge");
  if( !keepFlag ){
    vfile_aggregate_checksum_manifest(vid, &cksum1, &cksum1b);
    vfile_aggregate_checksum_disk(vid, &cksum2);
    if( blob_compare(&cksum1, &cksum2) ){
      printf("WARNING: manifest checksum does not agree with disk\n");
    }
    if( blob_compare(&cksum1, &cksum1b) ){
      printf("WARNING: manifest checksum does not agree with manifest\n");
    }
  }
  db_end_transaction(0);
}

/*
** Unlink the local database file
*/
void unlink_local_database(void){
  static const char *azFile[] = {
     "%s_FOSSIL_",
     "%s_FOSSIL_-journal",
     "%s.fos",
     "%s.fos-journal",
  };
  int i;
  for(i=0; i<sizeof(azFile)/sizeof(azFile[0]); i++){
    char *z = mprintf(azFile[i], g.zLocalRoot);
    unlink(z);
    free(z);
  }
}

/*
** COMMAND: close
**
** Usage: %fossil close ?-f|--force?
**
** The opposite of "open".  Close the current database connection.
** Require a -f or --force flag if there are unsaved changed in the
** current check-out.
*/
void close_cmd(void){
  int forceFlag = find_option("force","f",0)!=0;
  db_must_be_within_tree();
  if( !forceFlag && unsaved_changes()==1 ){
    fossil_fatal("there are unsaved changes in the current checkout");
  }
  db_close();
  unlink_local_database();
}
