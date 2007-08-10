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
#include "checkin.h"
#include <assert.h>

/*
** Generate text describing all changes.  Prepend zPrefix to each line
** of output.
**
** We assume that vfile_check_signature has been run.
*/
static void status_report(Blob *report, const char *zPrefix){
  Stmt q;
  int nPrefix = strlen(zPrefix);
  db_prepare(&q, 
    "SELECT pathname, deleted, chnged, rid FROM vfile "
    "WHERE file_is_selected(id) AND (chnged OR deleted OR rid=0) ORDER BY 1"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3)==0;
    blob_append(report, zPrefix, nPrefix);
    if( isNew ){
      blob_appendf(report, "ADDED    %s\n", zPathname);
    }else if( isDeleted ){
      blob_appendf(report, "DELETED  %s\n", zPathname);
    }else if( isChnged==2 ){
      blob_appendf(report, "UPDATED_BY_MERGE %s\n", zPathname);
    }else if( isChnged==3 ){
      blob_appendf(report, "ADDED_BY_MERGE %s\n", zPathname);
    }else{
      blob_appendf(report, "EDITED   %s\n", zPathname);
    }
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid FROM vmerge JOIN blob ON merge=rid"
                 " WHERE id=0");
  while( db_step(&q)==SQLITE_ROW ){
    blob_append(report, zPrefix, nPrefix);
    blob_appendf(report, "MERGED_WITH %s\n", db_column_text(&q, 0));
  }
  db_finalize(&q);
}

/*
** COMMAND: changes
**
** Report on the current status of all files.
*/
void changes_cmd(void){
  Blob report;
  int vid;
  db_must_be_within_tree();
  blob_zero(&report);
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid);
  status_report(&report, "");
  blob_write_to_file(&report, "-");
}

/*
** COMMAND: status
*/
void status_cmd(void){
  int vid;
  db_must_be_within_tree();
       /* 012345678901234 */
  printf("repository:   %s\n", db_lget("repository",""));
  printf("local-root:   %s\n", g.zLocalRoot);
  printf("server-code:  %s\n", db_get("server-code", ""));
  vid = db_lget_int("checkout", 0);
  if( vid ){
    show_common_info(vid, "checkout:", 0);
  }
  changes_cmd();
}

/*
** COMMAND: ls
**
** Show all files currently in the repository
*/
void ls_cmd(void){
  int vid;
  Stmt q;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid);
  db_prepare(&q, "SELECT pathname, deleted, rid, chnged FROM vfile"
                 " ORDER BY 1");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isNew = db_column_int(&q,2)==0;
    int chnged = db_column_int(&q,3);
    if( isNew ){
      printf("ADDED     %s\n", zPathname);
    }else if( isDeleted ){
      printf("DELETED   %s\n", zPathname);
    }else if( chnged ){
      printf("EDITED    %s\n", zPathname);
    }else{
      printf("UNCHANGED %s\n", zPathname);
    }
  }
  db_finalize(&q);
}

/*
** COMMAND: extra
**
** Print a list of all files in the source tree that are not part of
** the project
*/
void extra_cmd(void){
  Blob path;
  Stmt q;
  db_must_be_within_tree();
  db_multi_exec("CREATE TEMP TABLE sfile(x TEXT PRIMARY KEY)");
  chdir(g.zLocalRoot);
  blob_zero(&path);
  vfile_scan(0, &path);
  db_multi_exec("DELETE FROM sfile WHERE x='FOSSIL'");
  db_prepare(&q, 
      "SELECT x FROM sfile"
      " WHERE x NOT IN ('manifest','_FOSSIL_')"
      " ORDER BY 1");
  while( db_step(&q)==SQLITE_ROW ){
    printf("%s\n", db_column_text(&q, 0));
  }
  db_finalize(&q);
}

/*
** Prepare a commit comment.  Let the user modify it using the
** editor specified in the global_config table or either
** the VISUAL or EDITOR environment variable.
**
** Store the final commit comment in pComment.  pComment is assumed
** to be uninitialized - any prior content is overwritten.
*/
static void prepare_commit_comment(Blob *pComment){
  const char *zEditor;
  char *zCmd;
  char *zFile;
  Blob text, line;
  char *zComment;
  int i;
  blob_set(&text,
    "\n# Enter comments on this commit.  Lines beginning with # are ignored\n"
    "#\n"
  );
  status_report(&text, "# ");
  zEditor = db_global_get("editor", 0);
  if( zEditor==0 ){
    zEditor = getenv("VISUAL");
  }
  if( zEditor==0 ){
    zEditor = getenv("EDITOR");
  }
  if( zEditor==0 ){
    zEditor = "ed";
  }
  zFile = db_text(0, "SELECT '%qci-comment-' || hex(randomblob(6)) || '.txt'",
                   g.zLocalRoot);
  blob_write_to_file(&text, zFile);
  zCmd = mprintf("%s %s", zEditor, zFile);
  if( system(zCmd) ){
    fossil_panic("editor aborted");
  }
  blob_reset(&text);
  blob_read_from_file(&text, zFile);
  unlink(zFile);
  free(zFile);
  blob_zero(pComment);
  while( blob_line(&text, &line) ){
    int i, n;
    char *z;
    n = blob_size(&line);
    z = blob_buffer(&line);
    for(i=0; i<n && isspace(z[i]);  i++){}
    if( i<n && z[i]=='#' ) continue;
    blob_appendf(pComment, "%b\n", &line);
  }
  blob_reset(&text);
  zComment = blob_str(pComment);
  i = strlen(zComment);
  while( i>0 && isspace(zComment[i-1]) ){ i--; }
  blob_resize(pComment, i);
}

/*
** Populate the Global.aCommitFile[] based on the command line arguments
** to a [commit] command. Global.aCommitFile is an array of integers
** sized at (N+1), where N is the number of arguments passed to [commit].
** The contents are the [id] values from the vfile table corresponding
** to the filenames passed as arguments.
**
** The last element of aCommitFile[] is always 0 - indicating the end
** of the array.
**
** If there were no arguments passed to [commit], aCommitFile is not
** allocated and remains NULL. Other parts of the code interpret this
** to mean "all files".
*/
void select_commit_files(void){
  if( g.argc>2 ){
    int ii;
    Blob b;
    blob_zero(&b);
    g.aCommitFile = malloc(sizeof(int)*(g.argc-1));

    for(ii=2; ii<g.argc; ii++){
      int iId;
      if( !file_tree_name(g.argv[ii], &b) ){
        fossil_fatal("file is not in tree: %s", g.argv[ii]);
      }
      iId = db_int(-1, "SELECT id FROM vfile WHERE pathname=%Q", blob_str(&b));
      if( iId<0 ){
        fossil_fatal("fossil knows nothing about: %s", g.argv[ii]);
      }
      g.aCommitFile[ii-2] = iId;
      blob_reset(&b);
    }
    g.aCommitFile[ii-2] = 0;
  }
}

/*
** COMMAND: commit
**
** Create a new version containing all of the changes in the current
** checkout. A commit is a three step process:
**
**   1) Add the new content to the blob table,
**   2) Create and add the new manifest to the blob table,
**   3) Update the vfile table,
**   4) Run checks to make sure everything is still internally consistent.
*/
void commit_cmd(void){
  int rc;
  int vid, nrid, nvid;
  Blob comment;
  const char *zComment;
  Stmt q;
  Stmt q2;
  char *zUuid, *zDate;
  int noSign = 0;        /* True to omit signing the manifest using GPG */
  char *zManifestFile;   /* Name of the manifest file */
  Blob manifest;
  Blob mcksum;           /* Self-checksum on the manifest */
  Blob cksum1, cksum2;   /* Before and after commit checksums */
  Blob cksum1b;          /* Checksum recorded in the manifest */
 
  noSign = find_option("nosign","",0)!=0;
  zComment = find_option("comment","m",1);
  db_must_be_within_tree();
  noSign = db_get_int("omit-ci-sig", 0)|noSign;
  verify_all_options();

  /* There are two ways this command may be executed. If there are
  ** no arguments following the word "commit", then all modified files
  ** in the checked out directory are committed. If one or more arguments
  ** follows "commit", then only those files are committed.
  **
  ** After the following function call has returned, the Global.aCommitFile[]
  ** array is allocated to contain the "id" field from the vfile table
  ** for each file to be committed. Or, if aCommitFile is NULL, all files
  ** should be committed.
  */
  select_commit_files();
  if( g.aCommitFile && db_exists("SELECT 1 FROM vmerge") ){
    fossil_fatal("cannot do a partial commit of a merge");
  }

  user_select();
  db_begin_transaction();
  rc = unsaved_changes();
  if( rc==0 ){
    fossil_panic("nothing has changed");
  }

  /* If one or more files that were named on the command line have not
  ** been modified, bail out now.
  */
  if( g.aCommitFile ){
    Blob unmodified;
    memset(&unmodified, 0, sizeof(Blob));
    blob_init(&unmodified, 0, 0);
    db_blob(&unmodified, 
      "SELECT pathname FROM vfile WHERE chnged = 0 AND file_is_selected(id)"
    );
    if( strlen(blob_str(&unmodified)) ){
      fossil_panic("file %s has not changed", blob_str(&unmodified));
    }
  }

  vid = db_lget_int("checkout", 0);
  vfile_aggregate_checksum_disk(vid, &cksum1);
  if( zComment ){
    blob_zero(&comment);
    blob_append(&comment, zComment, -1);
  }else{
    prepare_commit_comment(&comment);
  }

  /* Step 1: Insert records for all modified files into the blob 
  ** table. If there were arguments passed to this command, only
  ** the identified fils are inserted (if they have been modified).
  */
  db_prepare(&q,
    "SELECT id, %Q || pathname, mrid FROM vfile "
    "WHERE chnged==1 AND NOT deleted AND file_is_selected(id)"
    , g.zLocalRoot
  );
  while( db_step(&q)==SQLITE_ROW ){
    int id, rid;
    const char *zFullname;
    Blob content;

    id = db_column_int(&q, 0);
    zFullname = db_column_text(&q, 1);
    rid = db_column_int(&q, 2);

    blob_zero(&content);
    blob_read_from_file(&content, zFullname);
    nrid = content_put(&content, 0, 0);
    if( rid>0 ){
      content_deltify(rid, nrid, 0);
    }
    db_multi_exec("UPDATE vfile SET mrid=%d, rid=%d WHERE id=%d", nrid,nrid,id);
  }
  db_finalize(&q);

  /* Create the manifest */
  blob_zero(&manifest);
  blob_appendf(&manifest, "C %F\n", blob_str(&comment));
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
  blob_appendf(&manifest, "P %s", zUuid);

  db_prepare(&q2, "SELECT merge FROM vmerge WHERE id=:id");
  db_bind_int(&q2, ":id", 0);
  while( db_step(&q2)==SQLITE_ROW ){
    int mid = db_column_int(&q2, 0);
    zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", mid);
    if( zUuid ){
      blob_appendf(&manifest, " %s", zUuid);
      free(zUuid);
    }
  }
  db_reset(&q2);

  blob_appendf(&manifest, "\n");
  blob_appendf(&manifest, "R %b\n", &cksum1);
  blob_appendf(&manifest, "U %F\n", g.zLogin);
  md5sum_blob(&manifest, &mcksum);
  blob_appendf(&manifest, "Z %b\n", &mcksum);
  zManifestFile = mprintf("%smanifest", g.zLocalRoot);
  if( !noSign && clearsign(&manifest, &manifest) ){
    Blob ans;
    blob_zero(&ans);
    prompt_user("unable to sign manifest.  continue [y/N]? ", &ans);
    if( blob_str(&ans)[0]!='y' ){
      db_end_transaction(1);
      exit(1);
    }
  }
  blob_write_to_file(&manifest, zManifestFile);
  blob_reset(&manifest);
  blob_read_from_file(&manifest, zManifestFile);
  free(zManifestFile);
  nvid = content_put(&manifest, 0, 0);
  if( nvid==0 ){
    fossil_panic("trouble committing manifest: %s", g.zErrMsg);
  }
  manifest_crosslink(nvid, &manifest);
  content_deltify(vid, nvid, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", nvid);
  printf("New_Version: %s\n", zUuid);
  
  /* Update the vfile and vmerge tables */
  db_multi_exec(
    "DELETE FROM vfile WHERE (vid!=%d OR deleted) AND file_is_selected(id);"
    "DELETE FROM vmerge WHERE file_is_selected(id) OR id=0;"
    "UPDATE vfile SET vid=%d;"
    "UPDATE vfile SET rid=mrid, chnged=0, deleted=0 WHERE file_is_selected(id);"
    , vid, nvid
  );
  db_lset_int("checkout", nvid);

  /* Verify that the repository checksum matches the expected checksum
  ** calculated before the checkin started (and stored as the R record
  ** of the manifest file).
  */
  vfile_aggregate_checksum_repository(nvid, &cksum2);
  if( blob_compare(&cksum1, &cksum2) ){
    fossil_panic("tree checksum does not match repository after commit");
  }

  /* Verify that the manifest checksum matches the expected checksum */
  vfile_aggregate_checksum_manifest(nvid, &cksum2, &cksum1b);
  if( blob_compare(&cksum1, &cksum1b) ){
    fossil_panic("manifest checksum does not agree with manifest: "
                 "%b versus %b", &cksum1, &cksum1b);
  }
  if( blob_compare(&cksum1, &cksum2) ){
    fossil_panic("tree checksum does not match manifest after commit: "
                 "%b versus %b", &cksum1, &cksum2);
  }

  /* Verify that the commit did not modify any disk images. */
  vfile_aggregate_checksum_disk(nvid, &cksum2);
  if( blob_compare(&cksum1, &cksum2) ){
    fossil_panic("tree checksums before and after commit do not match");
  }

  /* Commit */
  db_end_transaction(0);  
}
