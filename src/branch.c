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
** This file contains code used to create new branches within a repository.
*/
#include "config.h"
#include "branch.h"
#include <assert.h>

void branch_new(void){
  int vid, nvid, noSign;
  Stmt q;
  char *zBranch, *zUuid, *zDate, *zComment;
  const char *zColor;
  Blob manifest;
  Blob mcksum;           /* Self-checksum on the manifest */
  Blob cksum1, cksum2;   /* Before and after commit checksums */
  Blob cksum1b;          /* Checksum recorded in the manifest */
 
  noSign = find_option("nosign","",0)!=0;
  db_must_be_within_tree();
  noSign = db_get_int("omitsign", 0)|noSign;
  zColor = find_option("bgcolor","c",1);
  
  verify_all_options();
  
  /* fossil branch new name */
  if( g.argc<3 ){
    usage("branch new ?-bgcolor COLOR? BRANCH-NAME");
  }
  zBranch = g.argv[3];
  if( zBranch==0 || zBranch[0]==0 ){
    fossil_panic("branch name cannot be empty");
  }

  user_select();
  db_begin_transaction();
  if( unsaved_changes() ){
    fossil_panic("there are uncommitted changes. please commit first");
  }

  vid = db_lget_int("checkout", 0);
  vfile_aggregate_checksum_disk(vid, &cksum1);
  
  /* Create our new manifest */
  blob_zero(&manifest);
  zComment = mprintf("Branch created %s", zBranch);
  blob_appendf(&manifest, "C %F\n", zComment);
  zDate = db_text(0, "SELECT datetime('now')");
  zDate[10] = 'T';
  blob_appendf(&manifest, "D %s\n", zDate);

  db_prepare(&q,
    "SELECT pathname, uuid FROM vfile JOIN blob ON vfile.mrid=blob.rid"
    " WHERE NOT deleted AND vfile.vid=%d"
    " ORDER BY 1", vid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    blob_appendf(&manifest, "F %F %s\n", zName, zUuid);
  }
  db_finalize(&q);
  
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
  blob_appendf(&manifest, "P %s\n", zUuid);
  blob_appendf(&manifest, "R %b\n", &cksum1);
  
  if( zColor!=0 ){
    blob_appendf(&manifest, "T *bgcolor * %F\n", zColor);
    blob_appendf(&manifest, "T *sym-%F *\n", zBranch);
  }else{
    blob_appendf(&manifest, "T *sym-%F *\n", zBranch);
  }

  /* Cancel any tags that propagate */
  db_prepare(&q, 
      "SELECT tagname"
      "  FROM tagxref JOIN tag ON tagxref.tagid=tag.tagid"
      " WHERE rid=%d AND tagtype=2", vid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTagname = db_column_text(&q, 0);
    blob_appendf(&manifest, "T -%s *\n", zTagname);
  }
  db_finalize(&q);
  
  blob_appendf(&manifest, "U %F\n", g.zLogin);
  md5sum_blob(&manifest, &mcksum);
  blob_appendf(&manifest, "Z %b\n", &mcksum);
  if( !noSign && clearsign(&manifest, &manifest) ){
    Blob ans;
    blob_zero(&ans);
    prompt_user("unable to sign manifest.  continue [y/N]? ", &ans);
    if( blob_str(&ans)[0]!='y' ){
      db_end_transaction(1);
      exit(1);
    }
  }
  
  /*blob_write_to_file(&manifest, "manifest.new");*/

  nvid = content_put(&manifest, 0, 0);
  if( nvid==0 ){
    fossil_panic("trouble committing manifest: %s", g.zErrMsg);
  }
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nvid);
  manifest_crosslink(nvid, &manifest);
  content_deltify(vid, nvid, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", nvid);
  printf("Branch Version: %s\n", zUuid);
  printf("\n");
  printf("Notice: working copy not updated to the new branch. If\n");
  printf("        you wish to work on the new branch, update to\n");
  printf("        that branch first:\n");
  printf("\n");
  printf("        fossil update %s\n", zBranch);

  /* Verify that the manifest checksum matches the expected checksum */
  vfile_aggregate_checksum_repository(nvid, &cksum2);
  vfile_aggregate_checksum_manifest(nvid, &cksum2, &cksum1b);
  if( blob_compare(&cksum1, &cksum1b) ){
    fossil_panic("manifest checksum does not agree with manifest: "
                 "%b versus %b", &cksum1, &cksum1b);
  }
  
  /* Verify that the commit did not modify any disk images. */
  vfile_aggregate_checksum_disk(vid, &cksum2);
  if( blob_compare(&cksum1, &cksum2) ){
    fossil_panic("tree checksums before and after commit do not match");
  }

  /* Clear the undo/redo stack */
  undo_reset();

  /* Commit */
  db_end_transaction(0);
  
  /* Do an autosync push, if requested */
  autosync(0);
}

/*
** COMMAND: branch
**
** Usage: %fossil branch SUBCOMMAND ... ?-R|--repository FILE?
**
** Run various subcommands on the branches of the open repository or
** of the repository identified by the -R or --repository option.
**
**    %fossil branch new ?-bgcolor COLOR? BRANCH-NAME
**
**        Create a new branch BRANCH-NAME. You can optionally give
**        a commit message and branch color.
**
**    %fossil branch list
**
**        List all branches
**
*/
void branch_cmd(void){
  int n;
  db_find_and_open_repository();
  if( g.argc<3 ){
    usage("new|list ...");
  }
  n = strlen(g.argv[2]);
  if( n>=2 && strncmp(g.argv[2],"new",n)==0 ){
    branch_new();
  }else if( n>=2 && strncmp(g.argv[2],"list",n)==0 ){
    fossil_panic("branch list is not yet completed");
  }else{
    fossil_panic("branch subcommand should be one of: "
                 "new list");
  }
}
