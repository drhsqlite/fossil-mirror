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
** This file contains code used to check-in versions of the project
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
    "SELECT pathname, deleted, chnged, rid, coalesce(origname!=pathname,0)"
    "  FROM vfile "
    " WHERE file_is_selected(id)"
    "   AND (chnged OR deleted OR rid=0 OR pathname!=origname) ORDER BY 1"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3)==0;
    int isRenamed = db_column_int(&q,4);
    char *zFullName = mprintf("%s/%s", g.zLocalRoot, zPathname);
    blob_append(report, zPrefix, nPrefix);
    if( isDeleted ){
      blob_appendf(report, "DELETED  %s\n", zPathname);
    }else if( access(zFullName, 0) ){
      blob_appendf(report, "MISSING  %s\n", zPathname);
    }else if( isNew ){
      blob_appendf(report, "ADDED    %s\n", zPathname);
    }else if( isDeleted ){
      blob_appendf(report, "DELETED  %s\n", zPathname);
    }else if( isChnged==2 ){
      blob_appendf(report, "UPDATED_BY_MERGE %s\n", zPathname);
    }else if( isChnged==3 ){
      blob_appendf(report, "ADDED_BY_MERGE %s\n", zPathname);
    }else if( isChnged==1 ){
      blob_appendf(report, "EDITED   %s\n", zPathname);
    }else if( isRenamed ){
      blob_appendf(report, "RENAMED  %s\n", zPathname);
    }
    free(zFullName);
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
** Usage: %fossil changes
**
** Report on the edit status of all files in the current checkout.
** See also the "status" and "extra" commands.
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
**
** Usage: %fossil status
**
** Report on the status of the current checkout.
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
** Usage: %fossil ls
**
** Show the names of all files in the current checkout
*/
void ls_cmd(void){
  int vid;
  Stmt q;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid);
  db_prepare(&q,
     "SELECT pathname, deleted, rid, chnged, coalesce(origname!=pathname,0)"
     "  FROM vfile"
     " ORDER BY 1"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isNew = db_column_int(&q,2)==0;
    int chnged = db_column_int(&q,3);
    int renamed = db_column_int(&q,4);
    char *zFullName = mprintf("%s/%s", g.zLocalRoot, zPathname);
    if( isNew ){
      printf("ADDED     %s\n", zPathname);
    }else if( access(zFullName, 0) ){
      printf("MISSING   %s\n", zPathname);
    }else if( isDeleted ){
      printf("DELETED   %s\n", zPathname);
    }else if( chnged ){
      printf("EDITED    %s\n", zPathname);
    }else if( renamed ){
      printf("RENAMED   %s\n", zPathname);
    }else{
      printf("UNCHANGED %s\n", zPathname);
    }
    free(zFullName);
  }
  db_finalize(&q);
}

/*
** COMMAND: extras
** Usage: %fossil extras ?--dotfiles?
**
** Print a list of all files in the source tree that are not part of
** the current checkout.  See also the "clean" command.
**
** Files and subdirectories whose names begin with "." are normally
** ignored but can be included by adding the --dotfiles option.
*/
void extra_cmd(void){
  Blob path;
  Blob repo;
  Stmt q;
  int n;
  int allFlag = find_option("dotfiles",0,0)!=0;
  db_must_be_within_tree();
  db_multi_exec("CREATE TEMP TABLE sfile(x TEXT PRIMARY KEY)");
  n = strlen(g.zLocalRoot);
  blob_init(&path, g.zLocalRoot, n-1);
  vfile_scan(0, &path, blob_size(&path), allFlag);
  db_prepare(&q, 
      "SELECT x FROM sfile"
      " WHERE x NOT IN ('manifest','manifest.uuid','_FOSSIL_')"
      " ORDER BY 1");
  if( file_tree_name(g.zRepositoryName, &repo, 0) ){
    db_multi_exec("DELETE FROM sfile WHERE x=%B", &repo);
  }
  while( db_step(&q)==SQLITE_ROW ){
    printf("%s\n", db_column_text(&q, 0));
  }
  db_finalize(&q);
}

/*
** COMMAND: clean
** Usage: %fossil clean ?--force? ?--dotfiles?
**
** Delete all "extra" files in the source tree.  "Extra" files are
** files that are not officially part of the checkout.  See also
** the "extra" command. This operation cannot be undone. 
**
** You will be prompted before removing each file. If you are
** sure you wish to remove all "extra" files you can specify the
** optional --force flag and no prmpts will be issued.
**
** Files and subdirectories whose names begin with "." are
** normally ignored.  They are included if the "--dotfiles" option
** is used.
*/
void clean_cmd(void){
  int allFlag;
  int dotfilesFlag;
  Blob path, repo;
  Stmt q;
  int n;
  allFlag = find_option("all","a",0)!=0;
  dotfilesFlag = find_option("dotfiles",0,0)!=0;
  db_must_be_within_tree();
  db_multi_exec("CREATE TEMP TABLE sfile(x TEXT PRIMARY KEY)");
  n = strlen(g.zLocalRoot);
  blob_init(&path, g.zLocalRoot, n-1);
  vfile_scan(0, &path, blob_size(&path), dotfilesFlag);
  db_prepare(&q, 
      "SELECT %Q || x FROM sfile"
      " WHERE x NOT IN ('manifest','manifest.uuid','_FOSSIL_')"
      " ORDER BY 1", g.zLocalRoot);
  if( file_tree_name(g.zRepositoryName, &repo, 0) ){
    db_multi_exec("DELETE FROM sfile WHERE x=%B", &repo);
  }
  while( db_step(&q)==SQLITE_ROW ){
    if( allFlag ){
      unlink(db_column_text(&q, 0));
    }else{
      Blob ans;
      char *prompt = mprintf("remove unmanaged file \"%s\" [y/N]? ",
                              db_column_text(&q, 0));
      blob_zero(&ans);
      prompt_user(prompt, &ans);
      if( blob_str(&ans)[0]=='y' ){
        unlink(db_column_text(&q, 0));
      }
    }
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
    "\n"
    "# Enter comments on this check-in.  Lines beginning with # are ignored.\n"
    "# The check-in comment follows wiki formatting rules.\n"
    "#\n"
  );
  if( g.markPrivate ){
    blob_append(&text,
      "# PRIVATE BRANCH: This check-in will be private and will not sync to\n"
      "# repositories.\n"
      "#\n", -1
    );
  }
  status_report(&text, "# ");
  zEditor = db_get("editor", 0);
  if( zEditor==0 ){
    zEditor = getenv("VISUAL");
  }
  if( zEditor==0 ){
    zEditor = getenv("EDITOR");
  }
  if( zEditor==0 ){
#ifdef __MINGW32__
    zEditor = "notepad";
#else
    zEditor = "ed";
#endif
  }
  zFile = db_text(0, "SELECT '%qci-comment-' || hex(randomblob(6)) || '.txt'",
                   g.zLocalRoot);
#ifdef __MINGW32__
  blob_add_cr(&text);
#endif
  blob_write_to_file(&text, zFile);
  zCmd = mprintf("%s \"%s\"", zEditor, zFile);
  printf("%s\n", zCmd);
  if( portable_system(zCmd) ){
    fossil_panic("editor aborted");
  }
  blob_reset(&text);
  blob_read_from_file(&text, zFile);
  blob_remove_cr(&text);
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
    if( i<n || blob_size(pComment)>0 ){
      blob_appendf(pComment, "%b", &line);
    }
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
      file_tree_name(g.argv[ii], &b, 1);
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
** Return true if the check-in with RID=rid is a leaf.
** A leaf has no children in the same branch. 
*/
int is_a_leaf(int rid){
  int rc;
  static const char zSql[] = 
    @ SELECT 1 FROM plink
    @  WHERE pid=%d
    @    AND coalesce((SELECT value FROM tagxref
    @                   WHERE tagid=%d AND rid=plink.pid), 'trunk')
    @       =coalesce((SELECT value FROM tagxref
    @                   WHERE tagid=%d AND rid=plink.cid), 'trunk')
  ;
  rc = db_int(0, zSql, rid, TAG_BRANCH, TAG_BRANCH);
  return rc==0;
}

/*
** COMMAND: ci
** COMMAND: commit
**
** Usage: %fossil commit ?OPTIONS? ?FILE...?
**
** Create a new version containing all of the changes in the current
** checkout.  You will be prompted to enter a check-in comment unless
** the "-m" option is used to specify a comment line.  You will be
** prompted for your GPG passphrase in order to sign the new manifest
** unless the "--nosign" options is used.  All files that have
** changed will be committed unless some subset of files is specified
** on the command line.
**
** The --branch option followed by a branch name cases the new check-in
** to be placed in the named branch.  The --bgcolor option can be followed
** by a color name (ex:  '#ffc0c0') to specify the background color of
** entries in the new branch when shown in the web timeline interface.
**
** A check-in is not permitted to fork unless the --force or -f
** option appears.  A check-in is not allowed against a closed check-in.
**
** The --private option creates a private check-in that is never synced.
** Children of private check-ins are automatically private.
**
** Options:
**
**    --comment|-m COMMENT-TEXT
**    --branch NEW-BRANCH-NAME
**    --bgcolor COLOR
**    --nosign
**    --force|-f
**    --private
**    
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
  int isAMerge = 0;      /* True if checking in a merge */
  int forceFlag = 0;     /* Force a fork */
  char *zManifestFile;   /* Name of the manifest file */
  int nBasename;         /* Length of "g.zLocalRoot/" */
  const char *zBranch;   /* Create a new branch with this name */
  const char *zBgColor;  /* Set background color when branching */
  const char *zDateOvrd; /* Override date string */
  const char *zUserOvrd; /* Override user name */
  Blob filename;         /* complete filename */
  Blob manifest;
  Blob muuid;            /* Manifest uuid */
  Blob mcksum;           /* Self-checksum on the manifest */
  Blob cksum1, cksum2;   /* Before and after commit checksums */
  Blob cksum1b;          /* Checksum recorded in the manifest */
 
  url_proxy_options();
  noSign = find_option("nosign",0,0)!=0;
  zComment = find_option("comment","m",1);
  forceFlag = find_option("force", "f", 0)!=0;
  zBranch = find_option("branch","b",1);
  zBgColor = find_option("bgcolor",0,1);
  if( find_option("private",0,0) ){
    g.markPrivate = 1;
    if( zBranch==0 ) zBranch = "private";
    if( zBgColor==0 ) zBgColor = "#fec084";  /* Orange */
  }
  zDateOvrd = find_option("date-override",0,1);
  zUserOvrd = find_option("user-override",0,1);
  db_must_be_within_tree();
  noSign = db_get_boolean("omitsign", 0)|noSign;
  if( db_get_boolean("clearsign", 1)==0 ){ noSign = 1; }
  verify_all_options();

  /* Get the ID of the parent manifest artifact */
  vid = db_lget_int("checkout", 0);
  if( content_is_private(vid) ){
    g.markPrivate = 1;
  }

  /*
  ** Autosync if autosync is enabled and this is not a private check-in.
  */
  if( !g.markPrivate ){
    autosync(AUTOSYNC_PULL);
  }

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
  isAMerge = db_exists("SELECT 1 FROM vmerge");
  if( g.aCommitFile && isAMerge ){
    fossil_fatal("cannot do a partial commit of a merge");
  }

  user_select();
  /*
  ** Check that the user exists.
  */
  if( !db_exists("SELECT 1 FROM user WHERE login=%Q", g.zLogin) ){
    fossil_fatal("no such user: %s", g.zLogin);
  }
  
  db_begin_transaction();
  db_record_repository_filename(0);
  rc = unsaved_changes();
  if( rc==0 && !isAMerge && !forceFlag ){
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

  /*
  ** Do not allow a commit that will cause a fork unless the --force flag
  ** is used or unless this is a private check-in.
  */
  if( zBranch==0 && forceFlag==0 && g.markPrivate==0 && !is_a_leaf(vid) ){
    fossil_fatal("would fork.  \"update\" first or use -f or --force.");
  }

  /*
  ** Do not allow a commit against a closed leaf 
  */
  if( db_exists("SELECT 1 FROM tagxref"
                " WHERE tagid=%d AND rid=%d AND tagtype>0",
                TAG_CLOSED, vid) ){
    fossil_fatal("cannot commit against a closed leaf");
  }

  vfile_aggregate_checksum_disk(vid, &cksum1);
  if( zComment ){
    blob_zero(&comment);
    blob_append(&comment, zComment, -1);
  }else{
    prepare_commit_comment(&comment);
    if( blob_size(&comment)==0 ){
      Blob ans;
      blob_zero(&ans);
      prompt_user("empty check-in comment.  continue [y/N]? ", &ans);
      if( blob_str(&ans)[0]!='y' ){
        db_end_transaction(1);
        exit(1);
      }
    }
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
    blob_reset(&content);
    if( rid>0 ){
      content_deltify(rid, nrid, 0);
    }
    db_multi_exec("UPDATE vfile SET mrid=%d, rid=%d WHERE id=%d", nrid,nrid,id);
    db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nrid);
  }
  db_finalize(&q);

  /* Create the manifest */
  blob_zero(&manifest);
  if( blob_size(&comment)==0 ){
    blob_append(&comment, "(no comment)", -1);
  }
  blob_appendf(&manifest, "C %F\n", blob_str(&comment));
  zDate = db_text(0, "SELECT datetime('%q')", zDateOvrd ? zDateOvrd : "now");
  zDate[10] = 'T';
  blob_appendf(&manifest, "D %s\n", zDate);
  db_prepare(&q,
    "SELECT pathname, uuid, origname, blob.rid"
    "  FROM vfile JOIN blob ON vfile.mrid=blob.rid"
    " WHERE NOT deleted AND vfile.vid=%d"
    " ORDER BY 1", vid);
  blob_zero(&filename);
  blob_appendf(&filename, "%s", g.zLocalRoot);
  nBasename = blob_size(&filename);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zOrig = db_column_text(&q, 2);
    int frid = db_column_int(&q, 3);
    const char *zPerm;
    blob_append(&filename, zName, -1);
    if( file_isexe(blob_str(&filename)) ){
      zPerm = " x";
    }else{
      zPerm = "";
    }
    blob_resize(&filename, nBasename);
    if( zOrig==0 || strcmp(zOrig,zName)==0 ){
      blob_appendf(&manifest, "F %F %s%s\n", zName, zUuid, zPerm);
    }else{
      if( zPerm[0]==0 ){ zPerm = " w"; }
      blob_appendf(&manifest, "F %F %s%s %F\n", zName, zUuid, zPerm, zOrig);
    }
    if( !g.markPrivate ) content_make_public(frid);
  }
  blob_reset(&filename);
  db_finalize(&q);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
  blob_appendf(&manifest, "P %s", zUuid);

  db_prepare(&q2, "SELECT merge FROM vmerge WHERE id=:id");
  db_bind_int(&q2, ":id", 0);
  while( db_step(&q2)==SQLITE_ROW ){
    int mid = db_column_int(&q2, 0);
    if( !g.markPrivate && content_is_private(mid) ) continue;
    zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", mid);
    if( zUuid ){
      blob_appendf(&manifest, " %s", zUuid);
      free(zUuid);
    }
  }
  db_reset(&q2);

  blob_appendf(&manifest, "\n");
  blob_appendf(&manifest, "R %b\n", &cksum1);
  if( zBranch && zBranch[0] ){
    Stmt q;
    if( zBgColor && zBgColor[0] ){
      blob_appendf(&manifest, "T *bgcolor * %F\n", zBgColor);
    }
    blob_appendf(&manifest, "T *branch * %F\n", zBranch);
    blob_appendf(&manifest, "T *sym-%F *\n", zBranch);

    /* Cancel all other symbolic tags */
    db_prepare(&q,
        "SELECT tagname FROM tagxref, tag"
        " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
        "   AND tagtype>0 AND tagname GLOB 'sym-*'"
        "   AND tagname!='sym-'||%Q"
        " ORDER BY tagname",
        vid, zBranch);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zTag = db_column_text(&q, 0);
      blob_appendf(&manifest, "T -%F *\n", zTag);
    }
    db_finalize(&q);
  }  
  blob_appendf(&manifest, "U %F\n", zUserOvrd ? zUserOvrd : g.zLogin);
  md5sum_blob(&manifest, &mcksum);
  blob_appendf(&manifest, "Z %b\n", &mcksum);
  zManifestFile = mprintf("%smanifest", g.zLocalRoot);
  if( !noSign && !g.markPrivate && clearsign(&manifest, &manifest) ){
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
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nvid);
  manifest_crosslink(nvid, &manifest);
  content_deltify(vid, nvid, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", nvid);
  printf("New_Version: %s\n", zUuid);
  zManifestFile = mprintf("%smanifest.uuid", g.zLocalRoot);
  blob_zero(&muuid);
  blob_appendf(&muuid, "%s\n", zUuid);
  blob_write_to_file(&muuid, zManifestFile);
  free(zManifestFile);
  blob_reset(&muuid);

  
  /* Update the vfile and vmerge tables */
  db_multi_exec(
    "DELETE FROM vfile WHERE (vid!=%d OR deleted) AND file_is_selected(id);"
    "DELETE FROM vmerge WHERE file_is_selected(id) OR id=0;"
    "UPDATE vfile SET vid=%d;"
    "UPDATE vfile SET rid=mrid, chnged=0, deleted=0, origname=NULL"
    " WHERE file_is_selected(id);"
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

  /* Clear the undo/redo stack */
  undo_reset();

  /* Commit */
  db_end_transaction(0);

  if( !g.markPrivate ){
    autosync(AUTOSYNC_PUSH);  
  }
  if( count_nonbranch_children(vid)>1 ){
    printf("**** warning: a fork has occurred *****\n");
  }
}

/*
** COMMAND: test-import-manifest
**
** Usage: %fossil test-import-manifest DATE COMMENT ?-p PARENT_RECORDID?... ?-f (FILE_RECORDID PATH)?...
**
** Create a new version containing the specified file
** revisions (if any), and child of the given PARENT version.
*/
void import_manifest_cmd(void){
  const char* zDate;    /* argument - timestamp, as seconds since epoch (int) */
  const char* zComment; /* argument - manifest comment */
  char* zDateFmt;       /* timestamp formatted for the manifest */
  int* zParents;        /* arguments - array of parent references */
  int zParentCount;     /* number of found parent references */
  Blob manifest;        /* container for the manifest to be generated */
  Blob mcksum;          /* Self-checksum on the manifest */
  Blob cksum, cksum2;   /* Before and after commit checksums */
  Blob cksum1b;         /* Checksum recorded in the manifest */
  const char* parent;   /* loop variable when collecting parent references */
  int i, mid;           /* Another loop index, and id of new manifest */
  Stmt q;               /* sql statement to query table of files */
  char* zMidUuid;       /* Uuid for the newly generated manifest */


#define USAGE ("DATE COMMENT ?-p|-parent PARENT_RID...? ?-f|-file (FILE_RID PATH)...?")

  /*
  ** Validate and process arguments, collect information.
  */

  db_must_be_within_tree();

  /* Mandatory arguments */
  if (g.argc < 4) {
    usage (USAGE);
  }

  zDate    = g.argv[2];
  zComment = g.argv[3];

  remove_from_argv (2,2);

  /* Pull the optional parent arguments
  **
  ** Note: In principle it is possible that the loop below extracts
  ** the wrong arguments, if we ever try to import a file whose path
  ** starts with -p/-parent. In that case however the removal of two
  ** arguments will leave the file bereft of an argument and the
  ** recheck of the number of arguments below should catch that.
  **
  ** For a test command this is acceptable, it won't have lots of
  ** safety nets.
  */

  zParentCount = 0;
  zParents = (int*)malloc(sizeof(int)*(1+g.argc));
  /* 1+, to be ok with the default even if no arguments around */

  while ((parent = find_option("parent","p",1)) != NULL) {
    /* Check and store ... */
    zParents [zParentCount] = name_to_rid (parent);
    zParentCount ++;
  }

  /*
  ** Fall back to the root manifest as parent if none were specified
  ** explicitly.
  */

  if (!zParentCount) {
    zParents [zParentCount] = 1; /* HACK: rid 1 is the baseline manifest
				 ** which was entered when the repository
				 ** was created via 'new'. It always has
				 ** rid 1.
				 */
    zParentCount ++;
  }

  /* Pull the file arguments, at least one has to be present. They are
  ** the only things we can have here, now, and they are triples of
  ** '-f FID PATH', so use of find_option is out, and we can check the
  ** number of arguments.
  **
  ** Note: We store the data in a temp. table, so that we later can
  **       pull it sorted, and also easily get the associated hash
  **       identifiers.
  **
  ** Note 2: We expect at least one file, otherwise the manifest won't
  ** be recognized as a baseline by the manifest parser.
  */

  if (((g.argc-2) % 3 != 0) || (g.argc < 5)) {
    usage (USAGE);
  }

  db_begin_transaction();
  db_multi_exec ("CREATE TEMP TABLE __im ("
		 "rid      INTEGER NOT NULL,"
		 "pathname TEXT    NOT NULL)" );

  while (g.argc > 2) {
    /* Check and store ... */
    if (strcmp("-f",   g.argv[2]) &&
	strcmp("-file",g.argv[2])) {
      usage (USAGE);
    }

    /* DANGER The %s for the path might lead itself to an injection
    ** attack. For now (i.e. testing) this is ok, but do something
    ** better in the future.
    */

    db_multi_exec("INSERT INTO __im VALUES(%d,'%s')",
		  name_to_rid (g.argv[3]), g.argv[4] );
    remove_from_argv (2,3);
  }

  verify_all_options();

  /*
  ** Determine the user the manifest will belong to, and check that
  ** this user exists.
  */

  user_select();
  if( !db_exists("SELECT 1 FROM user WHERE login=%Q", g.zLogin) ){
    fossil_fatal("no such user: %s", g.zLogin);
  }

  /*
  ** Now generate the manifest in memory.
  **
  ** Start with comment and date. The latter is converted to the
  ** proper format before insertion.
  */

  blob_zero(&manifest);

  if (!strlen(zComment)) {
    blob_appendf(&manifest, "C %F\n", "(no comment)");
  } else {
    blob_appendf(&manifest, "C %F\n", zComment);
  }

  zDateFmt = db_text(0, "SELECT datetime(%Q,'unixepoch')",zDate);
  zDateFmt[10] = 'T';
  blob_appendf(&manifest, "D %s\n", zDateFmt);
  free(zDateFmt);

  /*
  ** Follow with all the collected files, properly sorted. Here were
  ** also compute the checksum over the files (paths, sizes,
  ** contents), similar to what 'vfile_aggregate_checksum_repository'
  ** does.
  */

  md5sum_init();
  db_prepare(&q,
	     "SELECT pathname, uuid, __im.rid"
	     " FROM __im JOIN blob ON __im.rid=blob.rid"
	     " ORDER BY 1");

  while( db_step(&q)==SQLITE_ROW ){
    char zBuf[100];
    Blob file;
    const char *zName = db_column_text(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    int         zRid  = db_column_int (&q, 2);

    /* Extend the manifest */
    blob_appendf(&manifest, "F %F %s\n", zName, zUuid);

    /* Update the checksum */
    md5sum_step_text(zName, -1);
    blob_zero(&file);
    content_get(zRid, &file);
    sprintf(zBuf, " %d\n", blob_size(&file));
    md5sum_step_text(zBuf, -1);
    md5sum_step_blob(&file);
    blob_reset(&file);
  }
  db_finalize(&q);
  md5sum_finish (&cksum);

  /*
  ** Follow with all the specified parents. We know that there is at
  ** least one.
  */

  blob_appendf(&manifest, "P");
  for (i=0;i<zParentCount;i++) {
    char* zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", zParents[i]);
    blob_appendf(&manifest, " %s", zUuid);
    free(zUuid);
  }
  blob_appendf(&manifest, "\n");

  /*
  ** Complete the manifest with user name and the various checksums
  */

  blob_appendf(&manifest, "R %b\n", &cksum);
  blob_appendf(&manifest, "U %F\n", g.zLogin);
  md5sum_blob(&manifest, &mcksum);
  blob_appendf(&manifest, "Z %b\n", &mcksum);

  /*
  ** Now insert the new manifest, try to compress it relative to first
  ** parent (primary).
   */

  /*blob_write_to_file (&manifest, "TEST_MANIFEST");*/

  mid = content_put(&manifest, 0, 0);
  if( mid==0 ){
    fossil_panic("trouble committing manifest: %s", g.zErrMsg);
  }

  content_deltify(zParents[0], mid, 0);

  /* Verify that the repository checksum matches the expected checksum
  ** calculated before the checkin started (and stored as the R record
  ** of the manifest file).
  */

  vfile_aggregate_checksum_manifest(mid, &cksum2, &cksum1b);
  if( blob_compare(&cksum, &cksum1b) ){
    fossil_panic("manifest checksum does not agree with manifest: "
                 "%b versus %b", &cksum, &cksum1b);
  }
  if( blob_compare(&cksum, &cksum2) ){
    fossil_panic("tree checksum does not match manifest after commit: "
                 "%b versus %b", &cksum, &cksum2);
  }

  /*
  ** At last commit all changes, after getting rid of the temp
  ** holder for the files, and release allocated memory.
  */

  db_multi_exec("DROP TABLE __im");
  zMidUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", mid);
  db_end_transaction(0);
  free(zParents);

  /*
  ** At the very last inform the caller about the id and uuid of the
  ** new manifest.
  */


  printf("inserted as record %d, %s\n", mid, zMidUuid);
  free(zMidUuid);
  return;

#undef USAGE
}
