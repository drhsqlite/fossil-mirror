/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
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
*/
int unsaved_changes(unsigned int cksigFlags){
  int vid;
  db_must_be_within_tree();
  vid = db_lget_int("checkout",0);
  vfile_check_signature(vid, cksigFlags|CKSIG_ENOTFILE);
  return db_exists("SELECT 1 FROM vfile WHERE chnged"
                   " OR coalesce(origname!=pathname,0)");
}

/*
** Undo the current check-out.  Unlink all files from the disk.
** Clear the VFILE table.
*/
void uncheckout(int vid){
  if( vid>0 ){
    vfile_unlink(vid);
  }
  db_multi_exec("DELETE FROM vfile WHERE vid=%d", vid);
}


/*
** Given the abbreviated UUID name of a version, load the content of that
** version in the VFILE table.  Return the VID for the version.
**
** If anything goes wrong, panic.
*/
int load_vfile(const char *zName, int forceMissingFlag){
  Blob uuid;
  int vid;

  blob_init(&uuid, zName, -1);
  if( name_to_uuid(&uuid, 1, "ci") ){
    fossil_fatal("%s", g.zErrMsg);
  }
  vid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &uuid);
  if( vid==0 ){
    fossil_fatal("no such check-in: %s", g.argv[2]);
  }
  if( !is_a_version(vid) ){
    fossil_fatal("object [%S] is not a check-in", blob_str(&uuid));
  }
  if( load_vfile_from_rid(vid) && !forceMissingFlag ){
    fossil_fatal("missing content, unable to checkout");
  };
  return vid;
}

/*
** Set or clear the vfile.isexe flag for a file.
*/
static void set_or_clear_isexe(const char *zFilename, int vid, int onoff){
  static Stmt s;
  db_static_prepare(&s,
    "UPDATE vfile SET isexe=:isexe"
    " WHERE vid=:vid AND pathname=:path AND isexe!=:isexe"
  );
  db_bind_int(&s, ":isexe", onoff);
  db_bind_int(&s, ":vid", vid);
  db_bind_text(&s, ":path", zFilename);
  db_step(&s);
  db_reset(&s);
}

/*
** Set or clear the execute permission bit (as appropriate) for all
** files in the current check-out, and replace files that have
** symlink bit with actual symlinks.
*/
void checkout_set_all_exe(int vid){
  Blob filename;
  int baseLen;
  Manifest *pManifest;
  ManifestFile *pFile;

  /* Check the EXE permission status of all files
  */
  pManifest = manifest_get(vid, CFTYPE_MANIFEST, 0);
  if( pManifest==0 ) return;
  blob_zero(&filename);
  blob_appendf(&filename, "%s", g.zLocalRoot);
  baseLen = blob_size(&filename);
  manifest_file_rewind(pManifest);
  while( (pFile = manifest_file_next(pManifest, 0))!=0 ){
    int isExe;
    blob_append(&filename, pFile->zName, -1);
    isExe = pFile->zPerm && strstr(pFile->zPerm, "x");
    file_wd_setexe(blob_str(&filename), isExe);
    set_or_clear_isexe(pFile->zName, vid, isExe);
    blob_resize(&filename, baseLen);
  }
  blob_reset(&filename);
  manifest_destroy(pManifest);
}


/*
** If the "manifest" setting is true, then automatically generate
** files named "manifest" and "manifest.uuid" containing, respectively,
** the text of the manifest and the artifact ID of the manifest.
** If the manifest setting is set, but is not a boolean value, then treat
** each character as a flag to enable writing "manifest", "manifest.uuid" or
** "manifest.tags".
*/
void manifest_to_disk(int vid){
  char *zManFile;
  Blob manifest;
  Blob taglist;
  int flg;

  flg = db_get_manifest_setting();

  if( flg & MFESTFLG_RAW ){
    blob_zero(&manifest);
    content_get(vid, &manifest);
    sterilize_manifest(&manifest);
    zManFile = mprintf("%smanifest", g.zLocalRoot);
    blob_write_to_file(&manifest, zManFile);
    free(zManFile);
  }else{
    if( !db_exists("SELECT 1 FROM vfile WHERE pathname='manifest'") ){
      zManFile = mprintf("%smanifest", g.zLocalRoot);
      file_delete(zManFile);
      free(zManFile);
    }
  }
  if( flg & MFESTFLG_UUID ){
    Blob hash;
    zManFile = mprintf("%smanifest.uuid", g.zLocalRoot);
    blob_set_dynamic(&hash, rid_to_uuid(vid));
    blob_append(&hash, "\n", 1);
    blob_write_to_file(&hash, zManFile);
    free(zManFile);
    blob_reset(&hash);
  }else{
    if( !db_exists("SELECT 1 FROM vfile WHERE pathname='manifest.uuid'") ){
      zManFile = mprintf("%smanifest.uuid", g.zLocalRoot);
      file_delete(zManFile);
      free(zManFile);
    }
  }
  if( flg & MFESTFLG_TAGS ){
    blob_zero(&taglist);
    zManFile = mprintf("%smanifest.tags", g.zLocalRoot);
    get_checkin_taglist(vid, &taglist);
    blob_write_to_file(&taglist, zManFile);
    free(zManFile);
    blob_reset(&taglist);
  }else{
    if( !db_exists("SELECT 1 FROM vfile WHERE pathname='manifest.tags'") ){
      zManFile = mprintf("%smanifest.tags", g.zLocalRoot);
      file_delete(zManFile);
      free(zManFile);
    }
  }
}

/*
** Find the branch name and all symbolic tags for a particular check-in
** identified by "rid".
**
** The branch name is actually only extracted if this procedure is run
** from within a local check-out.  And the branch name is not the branch
** name for "rid" but rather the branch name for the current check-out.
** It is unclear if the rid parameter is always the same as the current
** check-out.
*/
void get_checkin_taglist(int rid, Blob *pOut){
  Stmt stmt;
  char *zCurrent;
  blob_reset(pOut);
  zCurrent = db_text(0, "SELECT value FROM tagxref"
                        " WHERE rid=%d AND tagid=%d", rid, TAG_BRANCH);
  blob_appendf(pOut, "branch %s\n", zCurrent);
  db_prepare(&stmt, "SELECT substr(tagname, 5)"
                    "  FROM tagxref, tag"
                    " WHERE tagxref.rid=%d"
                    "   AND tagxref.tagtype>0"
                    "   AND tag.tagid=tagxref.tagid"
                    "   AND tag.tagname GLOB 'sym-*'", rid);
  while( db_step(&stmt)==SQLITE_ROW ){
    const char *zName;
    zName = db_column_text(&stmt, 0);
    blob_appendf(pOut, "tag %s\n", zName);
  }
  db_reset(&stmt);
  db_finalize(&stmt);
}


/*
** COMMAND: checkout*
** COMMAND: co*
**
** Usage: %fossil checkout ?VERSION | --latest? ?OPTIONS?
**    or: %fossil co ?VERSION | --latest? ?OPTIONS?
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
** Options:
**    --force           Ignore edited files in the current checkout
**    --keep            Only update the manifest and manifest.uuid files
**    --force-missing   Force checkout even if content is missing
**
** See also: update
*/
void checkout_cmd(void){
  int forceFlag;                 /* Force checkout even if edits exist */
  int forceMissingFlag;          /* Force checkout even if missing content */
  int keepFlag;                  /* Do not change any files on disk */
  int latestFlag;                /* Checkout the latest version */
  char *zVers;                   /* Version to checkout */
  int promptFlag;                /* True to prompt before overwriting */
  int vid, prior;
  Blob cksum1, cksum1b, cksum2;

  db_must_be_within_tree();
  db_begin_transaction();
  forceFlag = find_option("force","f",0)!=0;
  forceMissingFlag = find_option("force-missing",0,0)!=0;
  keepFlag = find_option("keep",0,0)!=0;
  latestFlag = find_option("latest",0,0)!=0;
  promptFlag = find_option("prompt",0,0)!=0 || forceFlag==0;

  /* We should be done with options.. */
  verify_all_options();

  if( (latestFlag!=0 && g.argc!=2) || (latestFlag==0 && g.argc!=3) ){
     usage("VERSION|--latest ?--force? ?--keep?");
  }
  if( !forceFlag && unsaved_changes(0) ){
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
      return;
    }
  }else{
    zVers = g.argv[2];
  }
  vid = load_vfile(zVers, forceMissingFlag);
  if( prior==vid ){
    return;
  }
  if( !keepFlag ){
    uncheckout(prior);
  }
  db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
  if( !keepFlag ){
    vfile_to_disk(vid, 0, !g.fQuiet, promptFlag);
  }
  checkout_set_all_exe(vid);
  manifest_to_disk(vid);
  ensure_empty_dirs_created();
  db_lset_int("checkout", vid);
  undo_reset();
  db_multi_exec("DELETE FROM vmerge");
  if( !keepFlag && db_get_boolean("repo-cksum",1) ){
    vfile_aggregate_checksum_manifest(vid, &cksum1, &cksum1b);
    vfile_aggregate_checksum_disk(vid, &cksum2);
    if( blob_compare(&cksum1, &cksum2) ){
      fossil_print("WARNING: manifest checksum does not agree with disk\n");
    }
    if( blob_size(&cksum1b) && blob_compare(&cksum1, &cksum1b) ){
      fossil_print("WARNING: manifest checksum does not agree with manifest\n");
    }
  }
  db_end_transaction(0);
}

/*
** Unlink the local database file
*/
static void unlink_local_database(int manifestOnly){
  const char *zReserved;
  int i;
  for(i=0; (zReserved = fossil_reserved_name(i, 1))!=0; i++){
    if( manifestOnly==0 || zReserved[0]=='m' ){
      char *z;
      z = mprintf("%s%s", g.zLocalRoot, zReserved);
      file_delete(z);
      free(z);
    }
  }
}

/*
** COMMAND: close*
**
** Usage: %fossil close ?OPTIONS?
**
** The opposite of "open".  Close the current database connection.
** Require a -f or --force flag if there are unsaved changes in the
** current check-out or if there is non-empty stash.
**
** Options:
**   --force|-f  necessary to close a check out with uncommitted changes
**
** See also: open
*/
void close_cmd(void){
  int forceFlag = find_option("force","f",0)!=0;
  db_must_be_within_tree();

  /* We should be done with options.. */
  verify_all_options();

  if( !forceFlag && unsaved_changes(0) ){
    fossil_fatal("there are unsaved changes in the current checkout");
  }
  if( !forceFlag
   && db_table_exists("localdb","stash")
   && db_exists("SELECT 1 FROM localdb.stash")
  ){
    fossil_fatal("closing the checkout will delete your stash");
  }
  if( db_is_writeable("repository") ){
    char *zUnset = mprintf("ckout:%q", g.zLocalRoot);
    db_unset(zUnset, 1);
    fossil_free(zUnset);
  }
  unlink_local_database(1);
  db_close(1);
  unlink_local_database(0);
}
