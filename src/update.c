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
** This file contains code used to merge the changes in the current
** checkout into a different version and switch to that version.
*/
#include "config.h"
#include "update.h"
#include <assert.h>

/*
** Return true if artifact rid is a version
*/
int is_a_version(int rid){
  return db_exists("SELECT 1 FROM event WHERE objid=%d AND type='ci'", rid);
}

/* This variable is set if we are doing an internal update.  It is clear
** when running the "update" command.
*/
static int internalUpdate = 0;
static int internalConflictCnt = 0;

/*
** Do an update to version vid.  
**
** Start an undo session but do not terminate it.  Do not autosync.
*/
int update_to(int vid){
  int savedArgc;
  char **savedArgv;
  char *newArgv[3];
  newArgv[0] = g.argv[0];
  newArgv[1] = "update";
  newArgv[2] = 0;
  savedArgv = g.argv;
  savedArgc = g.argc;
  g.argc = 2;
  g.argv = newArgv;
  internalUpdate = vid;
  internalConflictCnt = 0;
  update_cmd();
  g.argc = savedArgc;
  g.argv = savedArgv;
  return internalConflictCnt;
}

/*
** COMMAND: update
**
** Usage: %fossil update ?OPTIONS? ?VERSION? ?FILES...?
**
** Change the version of the current checkout to VERSION.  Any uncommitted
** changes are retained and applied to the new checkout.
**
** The VERSION argument can be a specific version or tag or branch name.
** If the VERSION argument is omitted, then the leaf of the subtree
** that begins at the current version is used, if there is only a single
** leaf.  VERSION can also be "current" to select the leaf of the current
** version or "latest" to select the most recent check-in.
**
** If one or more FILES are listed after the VERSION then only the
** named files are candidates to be updated.  If FILES is omitted, all
** files in the current checkout are subject to be updated.  Using
** a directory name for one of the FILES arguments is the same as
** using every subdirectory and file beneath that directory.
**
** The -n or --dry-run option causes this command to do a "dry run".  It
** prints out what would have happened but does not actually make any
** changes to the current checkout or the repository.
**
** The -v or --verbose option prints status information about unchanged
** files in addition to those file that actually do change.
**
** Options:
**   --case-sensitive <BOOL> override case-sensitive setting
**   --debug          print debug information on stdout
**   --latest         acceptable in place of VERSION, update to latest version
**   -n|--dry-run     If given, display instead of run actions
**   -v|--verbose     print status information about all files
**
** See also: revert
*/
void update_cmd(void){
  int vid;              /* Current version */
  int tid=0;            /* Target version - version we are changing to */
  Stmt q;
  int latestFlag;       /* --latest.  Pick the latest version if true */
  int dryRunFlag;       /* -n or --dry-run.  Do a dry run */
  int verboseFlag;      /* -v or --verbose.  Output extra information */
  int debugFlag;        /* --debug option */
  int setmtimeFlag;     /* --setmtime.  Set mtimes on files */
  int nChng;            /* Number of file renames */
  int *aChng;           /* Array of file renames */
  int i;                /* Loop counter */
  int nConflict = 0;    /* Number of merge conflicts */
  int nOverwrite = 0;   /* Number of unmanaged files overwritten */
  int nUpdate = 0;      /* Number of changes of any kind */
  Stmt mtimeXfer;       /* Statement to transfer mtimes */

  if( !internalUpdate ){
    undo_capture_command_line();
    url_proxy_options();
  }
  latestFlag = find_option("latest",0, 0)!=0;
  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("nochange",0,0)!=0; /* deprecated */
  }
  verboseFlag = find_option("verbose","v",0)!=0;
  debugFlag = find_option("debug",0,0)!=0;
  setmtimeFlag = find_option("setmtime",0,0)!=0;
  capture_case_sensitive_option();
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  user_select();
  if( !dryRunFlag && !internalUpdate ){
    autosync(SYNC_PULL + SYNC_VERBOSE*verboseFlag);
  }
  
  /* Create any empty directories now, as well as after the update,
  ** so changes in settings are reflected now */
  if( !dryRunFlag ) ensure_empty_dirs_created();

  if( internalUpdate ){
    tid = internalUpdate;
  }else if( g.argc>=3 ){
    if( fossil_strcmp(g.argv[2], "current")==0 ){
      /* If VERSION is "current", then use the same algorithm to find the
      ** target as if VERSION were omitted. */
    }else if( fossil_strcmp(g.argv[2], "latest")==0 ){
      /* If VERSION is "latest", then use the same algorithm to find the
      ** target as if VERSION were omitted and the --latest flag is present.
      */
      latestFlag = 1;
    }else{
      tid = name_to_typed_rid(g.argv[2],"ci");
      if( tid==0 ){
        fossil_fatal("no such version: %s", g.argv[2]);
      }else if( !is_a_version(tid) ){
        fossil_fatal("no such version: %s", g.argv[2]);
      }
    }
  }
  
  /* If no VERSION is specified on the command-line, then look for a
  ** descendent of the current version.  If there are multiple descendants,
  ** look for one from the same branch as the current version.  If there
  ** are still multiple descendants, show them all and refuse to update
  ** until the user selects one.
  */
  if( tid==0 ){
    int closeCode = 1;
    compute_leaves(vid, closeCode);
    if( !db_exists("SELECT 1 FROM leaves") ){
      closeCode = 0;
      compute_leaves(vid, closeCode);
    }
    if( !latestFlag && db_int(0, "SELECT count(*) FROM leaves")>1 ){
      db_multi_exec(
        "DELETE FROM leaves WHERE rid NOT IN"
        "   (SELECT leaves.rid FROM leaves, tagxref"
        "     WHERE leaves.rid=tagxref.rid AND tagxref.tagid=%d"
        "       AND tagxref.value==(SELECT value FROM tagxref"
                                   " WHERE tagid=%d AND rid=%d))",
        TAG_BRANCH, TAG_BRANCH, vid
      );
      if( db_int(0, "SELECT count(*) FROM leaves")>1 ){
        compute_leaves(vid, closeCode);
        db_prepare(&q, 
          "%s "
          "   AND event.objid IN leaves"
          " ORDER BY event.mtime DESC",
          timeline_query_for_tty()
        );
        print_timeline(&q, -100, 79, 0);
        db_finalize(&q);
        fossil_fatal("Multiple descendants");
      }
    }
    tid = db_int(0, "SELECT rid FROM leaves, event"
                    " WHERE event.objid=leaves.rid"
                    " ORDER BY event.mtime DESC"); 
    if( tid==0 ) tid = vid;
  }

  if( tid==0 ){
    return;
  }

  db_begin_transaction();
  vfile_check_signature(vid, CKSIG_ENOTFILE);
  if( !dryRunFlag && !internalUpdate ) undo_begin();
  load_vfile_from_rid(tid);

  /*
  ** The record.fn field is used to match files against each other.  The
  ** FV table contains one row for each each unique filename in
  ** in the current checkout, the pivot, and the version being merged.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS fv;"
    "CREATE TEMP TABLE fv("
    "  fn TEXT %s PRIMARY KEY,"   /* The filename relative to root */
    "  idv INTEGER,"              /* VFILE entry for current version */
    "  idt INTEGER,"              /* VFILE entry for target version */
    "  chnged BOOLEAN,"           /* True if current version has been edited */
    "  islinkv BOOLEAN,"          /* True if current file is a link */
    "  islinkt BOOLEAN,"          /* True if target file is a link */
    "  ridv INTEGER,"             /* Record ID for current version */
    "  ridt INTEGER,"             /* Record ID for target */
    "  isexe BOOLEAN,"            /* Does target have execute permission? */
    "  deleted BOOLEAN DEFAULT 0,"/* File marked by "rm" to become unmanaged */
    "  fnt TEXT %s"               /* Filename of same file on target version */
    ");",
    filename_collation(), filename_collation()
  );

  /* Add files found in the current version
  */
  db_multi_exec(
    "INSERT OR IGNORE INTO fv(fn,fnt,idv,idt,ridv,ridt,isexe,chnged,deleted)"
    " SELECT pathname, pathname, id, 0, rid, 0, isexe, chnged, deleted"
    "   FROM vfile WHERE vid=%d",
    vid
  );

  /* Compute file name changes on V->T.  Record name changes in files that
  ** have changed locally.
  */
  find_filename_changes(vid, tid, 1, &nChng, &aChng, debugFlag ? "V->T": 0);
  if( nChng ){
    for(i=0; i<nChng; i++){
      db_multi_exec(
        "UPDATE fv"
        "   SET fnt=(SELECT name FROM filename WHERE fnid=%d)"
        " WHERE fn=(SELECT name FROM filename WHERE fnid=%d) AND chnged",
        aChng[i*2+1], aChng[i*2]
      );
    }
    fossil_free(aChng);
  }

  /* Add files found in the target version T but missing from the current
  ** version V.
  */
  db_multi_exec(
    "INSERT OR IGNORE INTO fv(fn,fnt,idv,idt,ridv,ridt,isexe,chnged)"
    " SELECT pathname, pathname, 0, 0, 0, 0, isexe, 0 FROM vfile"
    "  WHERE vid=%d"
    "    AND pathname %s NOT IN (SELECT fnt FROM fv)",
    tid, filename_collation()
  );

  /*
  ** Compute the file version ids for T
  */
  db_multi_exec(
    "UPDATE fv SET"
    " idt=coalesce((SELECT id FROM vfile WHERE vid=%d AND fnt=pathname),0),"
    " ridt=coalesce((SELECT rid FROM vfile WHERE vid=%d AND fnt=pathname),0)",
    tid, tid
  );

  /*
  ** Add islink information
  */
  db_multi_exec(
    "UPDATE fv SET"
    " islinkv=coalesce((SELECT islink FROM vfile"
                       " WHERE vid=%d AND fnt=pathname),0),"
    " islinkt=coalesce((SELECT islink FROM vfile"
                       " WHERE vid=%d AND fnt=pathname),0)",
    vid, tid
  );


  if( debugFlag ){
    db_prepare(&q,
       "SELECT rowid, fn, fnt, chnged, ridv, ridt, isexe,"
       "       islinkv, islinkt FROM fv"
    );
    while( db_step(&q)==SQLITE_ROW ){
       fossil_print("%3d: ridv=%-4d ridt=%-4d chnged=%d isexe=%d"
                    " islinkv=%d  islinkt=%d\n",
          db_column_int(&q, 0),
          db_column_int(&q, 4),
          db_column_int(&q, 5),
          db_column_int(&q, 3),
          db_column_int(&q, 6),
          db_column_int(&q, 7),
          db_column_int(&q, 8));
       fossil_print("     fnv = [%s]\n", db_column_text(&q, 1));
       fossil_print("     fnt = [%s]\n", db_column_text(&q, 2));
    }
    db_finalize(&q);
  }

  /* If FILES appear on the command-line, remove from the "fv" table
  ** every entry that is not named on the command-line or which is not
  ** in a directory named on the command-line.
  */
  if( g.argc>=4 ){
    Blob sql;              /* SQL statement to purge unwanted entries */
    Blob treename;         /* Normalized filename */
    int i;                 /* Loop counter */
    const char *zSep;      /* Term separator */

    blob_zero(&sql);
    blob_append(&sql, "DELETE FROM fv WHERE ", -1);
    zSep = "";
    for(i=3; i<g.argc; i++){
      file_tree_name(g.argv[i], &treename, 1);
      if( file_wd_isdir(g.argv[i])==1 ){
        if( blob_size(&treename) != 1 || blob_str(&treename)[0] != '.' ){
          blob_appendf(&sql, "%sfn NOT GLOB '%b/*' ", zSep, &treename);
        }else{
          blob_reset(&sql);
          break;
        }
      }else{
        blob_appendf(&sql, "%sfn<>%B ", zSep, &treename);
      }
      zSep = "AND ";
      blob_reset(&treename);
    }
    db_multi_exec(blob_str(&sql));
    blob_reset(&sql);
  }

  /*
  ** Alter the content of the checkout so that it conforms with the
  ** target
  */
  db_prepare(&q, 
    "SELECT fn, idv, ridv, idt, ridt, chnged, fnt,"
    "       isexe, islinkv, islinkt, deleted FROM fv ORDER BY 1"
  );
  db_prepare(&mtimeXfer,
    "UPDATE vfile SET mtime=(SELECT mtime FROM vfile WHERE id=:idv)"
    " WHERE id=:idt"
  );
  assert( g.zLocalRoot!=0 );
  assert( strlen(g.zLocalRoot)>0 );
  assert( g.zLocalRoot[strlen(g.zLocalRoot)-1]=='/' );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);  /* The filename from root */
    int idv = db_column_int(&q, 1);             /* VFILE entry for current */
    int ridv = db_column_int(&q, 2);            /* RecordID for current */
    int idt = db_column_int(&q, 3);             /* VFILE entry for target */
    int ridt = db_column_int(&q, 4);            /* RecordID for target */
    int chnged = db_column_int(&q, 5);          /* Current is edited */
    const char *zNewName = db_column_text(&q,6);/* New filename */
    int isexe = db_column_int(&q, 7);           /* EXE perm for new file */
    int islinkv = db_column_int(&q, 8);         /* Is current file is a link */
    int islinkt = db_column_int(&q, 9);         /* Is target file is a link */
    int deleted = db_column_int(&q, 10);        /* Marked for deletion */
    char *zFullPath;                            /* Full pathname of the file */
    char *zFullNewPath;                         /* Full pathname of dest */
    char nameChng;                              /* True if the name changed */

    zFullPath = mprintf("%s%s", g.zLocalRoot, zName);
    zFullNewPath = mprintf("%s%s", g.zLocalRoot, zNewName);
    nameChng = fossil_strcmp(zName, zNewName);
    nUpdate++;
    if( deleted ){
      db_multi_exec("UPDATE vfile SET deleted=1 WHERE id=%d", idt);
    }
    if( idv>0 && ridv==0 && idt>0 && ridt>0 ){
      /* Conflict.  This file has been added to the current checkout
      ** but also exists in the target checkout.  Use the current version.
      */
      fossil_print("CONFLICT %s\n", zName);
      nConflict++;
    }else if( idt>0 && idv==0 ){
      /* File added in the target. */
      if( file_wd_isfile_or_link(zFullPath) ){
        fossil_print("ADD %s - overwrites an unmanaged file\n", zName);
        nOverwrite++;
      }else{
        fossil_print("ADD %s\n", zName);
      }
      undo_save(zName);
      if( !dryRunFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt>0 && idv>0 && ridt!=ridv && (chnged==0 || deleted) ){
      /* The file is unedited.  Change it to the target version */
      undo_save(zName);
      if( deleted ){
        fossil_print("UPDATE %s - change to unmanaged file\n", zName);
      }else{
        fossil_print("UPDATE %s\n", zName);
      }
      if( !dryRunFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt>0 && idv>0 && !deleted && file_wd_size(zFullPath)<0 ){
      /* The file missing from the local check-out. Restore it to the
      ** version that appears in the target. */
      fossil_print("UPDATE %s\n", zName);
      undo_save(zName);
      if( !dryRunFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt==0 && idv>0 ){
      if( ridv==0 ){
        /* Added in current checkout.  Continue to hold the file as
        ** as an addition */
        db_multi_exec("UPDATE vfile SET vid=%d WHERE id=%d", tid, idv);
      }else if( chnged ){
        /* Edited locally but deleted from the target.  Do not track the
        ** file but keep the edited version around. */
        fossil_print("CONFLICT %s - edited locally but deleted by update\n",
                     zName);
        nConflict++;
      }else{
        fossil_print("REMOVE %s\n", zName);
        undo_save(zName);
        if( !dryRunFlag ) file_delete(zFullPath);
      }
    }else if( idt>0 && idv>0 && ridt!=ridv && chnged ){
      /* Merge the changes in the current tree into the target version */
      Blob r, t, v;
      int rc;
      if( nameChng ){
        fossil_print("MERGE %s -> %s\n", zName, zNewName);
      }else{
        fossil_print("MERGE %s\n", zName);
      }
      if( islinkv || islinkt /* || file_wd_islink(zFullPath) */ ){
        fossil_print("***** Cannot merge symlink %s\n", zNewName);
        nConflict++;        
      }else{
        unsigned mergeFlags = dryRunFlag ? MERGE_DRYRUN : 0;
        undo_save(zName);
        content_get(ridt, &t);
        content_get(ridv, &v);
        rc = merge_3way(&v, zFullPath, &t, &r, mergeFlags);
        if( rc>=0 ){
          if( !dryRunFlag ){
            blob_write_to_file(&r, zFullNewPath);
            file_wd_setexe(zFullNewPath, isexe);
          }
          if( rc>0 ){
            fossil_print("***** %d merge conflicts in %s\n", rc, zNewName);
            nConflict++;
          }
        }else{
          if( !dryRunFlag ){
            blob_write_to_file(&t, zFullNewPath);
            file_wd_setexe(zFullNewPath, isexe);
          }
          fossil_print("***** Cannot merge binary file %s\n", zNewName);
          nConflict++;
        }
      }
      if( nameChng && !dryRunFlag ) file_delete(zFullPath);
      blob_reset(&v);
      blob_reset(&t);
      blob_reset(&r);
    }else{
      nUpdate--;
      if( chnged ){
        if( verboseFlag ) fossil_print("EDITED %s\n", zName);
      }else{
        db_bind_int(&mtimeXfer, ":idv", idv);
        db_bind_int(&mtimeXfer, ":idt", idt);
        db_step(&mtimeXfer);
        db_reset(&mtimeXfer);
        if( verboseFlag ) fossil_print("UNCHANGED %s\n", zName);
      }
    }
    free(zFullPath);
    free(zFullNewPath);
  }
  db_finalize(&q);
  db_finalize(&mtimeXfer);
  fossil_print("%.79c\n",'-');
  if( nUpdate==0 ){
    show_common_info(tid, "checkout:", 1, 0);
    fossil_print("%-13s None. Already up-to-date\n", "changes:");
  }else{
    show_common_info(tid, "updated-to:", 1, 0);
    fossil_print("%-13s %d file%s modified.\n", "changes:",
                 nUpdate, nUpdate>1 ? "s" : "");
  }

  /* Report on conflicts
  */
  if( !dryRunFlag ){
    Stmt q;
    int nMerge = 0;
    db_prepare(&q, "SELECT uuid, id FROM vmerge JOIN blob ON merge=rid"
                   " WHERE id<=0");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zLabel = "merge";
      switch( db_column_int(&q, 1) ){
        case -1:  zLabel = "cherrypick merge"; break;
        case -2:  zLabel = "backout merge";    break;
      }
      fossil_warning("uncommitted %s against %S.",
                     zLabel, db_column_text(&q, 0));
      nMerge++;
    }
    db_finalize(&q);
    
    if( nConflict ){
      if( internalUpdate ){
        internalConflictCnt = nConflict;
        nConflict = 0;
      }else{
        fossil_warning("WARNING: %d merge conflicts", nConflict);
      }
    }
    if( nOverwrite ){
      fossil_warning("WARNING: %d unmanaged files were overwritten",
                     nOverwrite);
    }
    if( nMerge ){
      fossil_warning("WARNING: %d uncommitted prior merges", nMerge);
    }
  }
  
  /*
  ** Clean up the mid and pid VFILE entries.  Then commit the changes.
  */
  if( dryRunFlag ){
    db_end_transaction(1);  /* With --dry-run, rollback changes */
  }else{
    ensure_empty_dirs_created();
    if( g.argc<=3 ){
      /* All files updated.  Shift the current checkout to the target. */
      db_multi_exec("DELETE FROM vfile WHERE vid!=%d", tid);
      checkout_set_all_exe(tid);
      manifest_to_disk(tid);
      db_lset_int("checkout", tid);
    }else{
      /* A subset of files have been checked out.  Keep the current
      ** checkout unchanged. */
      db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
    }
    if( !internalUpdate ) undo_finish();
    if( setmtimeFlag ) vfile_check_signature(tid, CKSIG_SETMTIME);
    db_end_transaction(0);
  }
}

/*
** Make sure empty directories are created
*/
void ensure_empty_dirs_created(void){
  /* Make empty directories? */
  char *zEmptyDirs = db_get("empty-dirs", 0);
  if( zEmptyDirs!=0 ){
    char *bc;
    Blob dirName;
    Blob dirsList;

    blob_zero(&dirsList);
    blob_init(&dirsList, zEmptyDirs, strlen(zEmptyDirs));
    /* Replace commas by spaces */
    bc = blob_str(&dirsList);
    while( (*bc)!='\0' ){
      if( (*bc)==',' ) { *bc = ' '; }
      ++bc;
    }
    /* Make directories */
    blob_zero(&dirName);
    while( blob_token(&dirsList, &dirName) ){
      const char *zDir = blob_str(&dirName);
      /* Make full pathname of the directory */
      Blob path;
      const char *zPath;

      blob_zero(&path);
      blob_appendf(&path, "%s/%s", g.zLocalRoot, zDir);
      zPath = blob_str(&path);      
      /* Handle various cases of existence of the directory */
      switch( file_wd_isdir(zPath) ){
        case 0: { /* doesn't exist */
          if( file_mkdir(zPath, 0)!=0 ) {
            fossil_warning("couldn't create directory %s as "
                           "required by empty-dirs setting", zDir);
          }          
          break;
        }
        case 1: { /* exists, and is a directory */
          /* do nothing - required directory exists already */
          break;
        }
        case 2: { /* exists, but isn't a directory */
          fossil_warning("file %s found, but a directory is required "
                         "by empty-dirs setting", zDir);          
        }
      }
      blob_reset(&path);
    }
  }
}


/*
** Get the contents of a file within the checking "revision".  If
** revision==NULL then get the file content for the current checkout.
*/
int historical_version_of_file(
  const char *revision,    /* The checkin containing the file */
  const char *file,        /* Full treename of the file */
  Blob *content,           /* Put the content here */
  int *pIsLink,            /* Set to true if file is link. */
  int *pIsExe,             /* Set to true if file is executable */
  int *pIsBin,             /* Set to true if file is binary */
  int errCode              /* Error code if file not found.  Panic if 0. */
){
  Manifest *pManifest;
  ManifestFile *pFile;
  int rid=0;
  
  if( revision ){
    rid = name_to_typed_rid(revision,"ci");
  }else{
    rid = db_lget_int("checkout", 0);
  }
  if( !is_a_version(rid) ){
    if( errCode>0 ) return errCode;
    fossil_fatal("no such checkin: %s", revision);
  }
  pManifest = manifest_get(rid, CFTYPE_MANIFEST, 0);
  
  if( pManifest ){
    pFile = manifest_file_find(pManifest, file);
    if( pFile ){
      int rc;
      rid = uuid_to_rid(pFile->zUuid, 0);
      if( pIsExe ) *pIsExe = ( manifest_file_mperm(pFile)==PERM_EXE );
      if( pIsLink ) *pIsLink = ( manifest_file_mperm(pFile)==PERM_LNK );
      manifest_destroy(pManifest);
      rc = content_get(rid, content);
      if( rc && pIsBin ){
        *pIsBin = looks_like_binary(content);
      }
      return rc;
    }
    manifest_destroy(pManifest);
    if( errCode<=0 ){
      fossil_fatal("file %s does not exist in checkin: %s", file, revision);
    }
  }else if( errCode<=0 ){
    if( revision==0 ){
      revision = db_text("current", "SELECT uuid FROM blob WHERE rid=%d", rid);
    }
    fossil_fatal("could not parse manifest for checkin: %s", revision);
  }
  return errCode;
}


/*
** COMMAND: revert
**
** Usage: %fossil revert ?-r REVISION? ?FILE ...?
**
** Revert to the current repository version of FILE, or to
** the version associated with baseline REVISION if the -r flag
** appears.
**
** If FILE was part of a rename operation, both the original file
** and the renamed file are reverted.
**
** Revert all files if no file name is provided.
**
** If a file is reverted accidently, it can be restored using
** the "fossil undo" command.
**
** Options:
**   -r REVISION    revert given FILE(s) back to given REVISION
**
** See also: redo, undo, update
*/
void revert_cmd(void){
  const char *zFile;
  const char *zRevision;
  Blob record;
  int i;
  int errCode;
  Stmt q;

  undo_capture_command_line();  
  zRevision = find_option("revision", "r", 1);
  verify_all_options();
  
  if( g.argc<2 ){
    usage("?OPTIONS? [FILE] ...");
  }
  if( zRevision && g.argc<3 ){
    fossil_fatal("the --revision option does not work for the entire tree");
  }
  db_must_be_within_tree();
  db_begin_transaction();
  undo_begin();
  db_multi_exec("CREATE TEMP TABLE torevert(name UNIQUE);");

  if( g.argc>2 ){
    for(i=2; i<g.argc; i++){
      Blob fname;
      zFile = mprintf("%/", g.argv[i]);
      file_tree_name(zFile, &fname, 1);
      db_multi_exec(
        "REPLACE INTO torevert VALUES(%B);"
        "INSERT OR IGNORE INTO torevert"
        " SELECT pathname"
        "   FROM vfile"
        "  WHERE origname=%B;",
        &fname, &fname
      );
      blob_reset(&fname);
    }
  }else{
    int vid;
    vid = db_lget_int("checkout", 0);
    vfile_check_signature(vid, 0);
    db_multi_exec(
      "DELETE FROM vmerge;"
      "INSERT OR IGNORE INTO torevert "
      " SELECT pathname"
      "   FROM vfile "
      "  WHERE chnged OR deleted OR rid=0 OR pathname!=origname;"
    );
  }
  db_multi_exec(
    "INSERT OR IGNORE INTO torevert"
    " SELECT origname"
    "   FROM vfile"
    "  WHERE origname!=pathname AND pathname IN (SELECT name FROM torevert);"
  );
  blob_zero(&record);
  db_prepare(&q, "SELECT name FROM torevert");
  if( zRevision==0 ){
    int vid = db_lget_int("checkout", 0);
    zRevision = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
  }
  while( db_step(&q)==SQLITE_ROW ){
    int isExe = 0;
    int isLink = 0;
    char *zFull;
    zFile = db_column_text(&q, 0);
    zFull = mprintf("%/%/", g.zLocalRoot, zFile);
    errCode = historical_version_of_file(zRevision, zFile, &record,
                                         &isLink, &isExe, 0, 2);
    if( errCode==2 ){
      if( db_int(0, "SELECT rid FROM vfile WHERE pathname=%Q OR origname=%Q",
                 zFile, zFile)==0 ){
        fossil_print("UNMANAGE: %s\n", zFile);
      }else{
        undo_save(zFile);
        file_delete(zFull);
        fossil_print("DELETE: %s\n", zFile);
      }
      db_multi_exec(
        "UPDATE vfile"
        "   SET pathname=origname, origname=NULL"
        " WHERE pathname=%Q AND origname!=pathname;"
        "DELETE FROM vfile WHERE pathname=%Q",
        zFile, zFile
      );
    }else{
      sqlite3_int64 mtime;
      undo_save(zFile);
      if( file_wd_size(zFull)>=0 && (isLink || file_wd_islink(zFull)) ){
        file_delete(zFull);
      }
      if( isLink ){
        symlink_create(blob_str(&record), zFull);
      }else{
        blob_write_to_file(&record, zFull);
      }
      file_wd_setexe(zFull, isExe);
      fossil_print("REVERTED: %s\n", zFile);
      mtime = file_wd_mtime(zFull);
      db_multi_exec(
         "UPDATE vfile"
         "   SET mtime=%lld, chnged=0, deleted=0, isexe=%d, islink=%d,mrid=rid"
         " WHERE pathname=%Q OR origname=%Q",
         mtime, isExe, isLink, zFile, zFile
      );
    }
    blob_reset(&record);
    free(zFull);
  }
  db_finalize(&q);
  undo_finish();
  db_end_transaction(0);
}
