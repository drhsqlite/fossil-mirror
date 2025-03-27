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
** check-out into a different version and switch to that version.
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
** Change the version of the current check-out to VERSION.  Any
** uncommitted changes are retained and applied to the new check-out.
**
** The VERSION argument can be a specific version or tag or branch
** name.  If the VERSION argument is omitted, then the leaf of the
** subtree that begins at the current version is used, if there is
** only a single leaf.  VERSION can also be "current" to select the
** leaf of the current version or "latest" to select the most recent
** check-in.
**
** If one or more FILES are listed after the VERSION then only the
** named files are candidates to be updated, and any updates to them
** will be treated as edits to the current version. Using a directory
** name for one of the FILES arguments is the same as using every
** subdirectory and file beneath that directory.
**
** If FILES is omitted, all files in the current check-out are subject
** to being updated and the version of the current check-out is changed
** to VERSION. Any uncommitted changes are retained and applied to the
** new check-out.
**
** The -n or --dry-run option causes this command to do a "dry run".
** It prints out what would have happened but does not actually make
** any changes to the current check-out or the repository.
**
** The -v or --verbose option prints status information about
** unchanged files in addition to those file that actually do change.
**
** Options:
**   --case-sensitive BOOL   Override case-sensitive setting
**   --debug                 Print debug information on stdout
**   -n|--dry-run            If given, display instead of run actions
**   --force-missing         Force update if missing content after sync
**   -K|--keep-merge-files   On merge conflict, retain the temporary files
**                           used for merging, named *-baseline, *-original,
**                           and *-merge.
**   --latest                Acceptable in place of VERSION, update to
**                           latest version
**   --nosync                Do not auto-sync prior to update
**   --proxy PROXY           Use PROXY as http proxy during sync operation
**   --setmtime              Set timestamps of all files to match their
**                           SCM-side times (the timestamp of the last
**                           check-in which modified them).
**   -v|--verbose            Print status information about all files
**   -W|--width WIDTH        Width of lines (default is to auto-detect).
**                           Must be more than 20 or 0 (= no limit,
**                           resulting in a single line per entry).
**
** See also: [[revert]]
*/
void update_cmd(void){
  int vid;              /* Current version */
  int tid=0;            /* Target version - version we are changing to */
  Stmt q;
  int latestFlag;       /* --latest.  Pick the latest version if true */
  int dryRunFlag;       /* -n or --dry-run.  Do a dry run */
  int verboseFlag;      /* -v or --verbose.  Output extra information */
  int forceMissingFlag; /* --force-missing.  Continue if missing content */
  int debugFlag;        /* --debug option */
  int setmtimeFlag;     /* --setmtime.  Set mtimes on files */
  int keepMergeFlag;    /* True if --keep-merge-files is present */
  int nChng;            /* Number of file renames */
  int *aChng;           /* Array of file renames */
  int i;                /* Loop counter */
  int nConflict = 0;    /* Number of merge conflicts */
  int nOverwrite = 0;   /* Number of unmanaged files overwritten */
  int nUpdate = 0;      /* Number of changes of any kind */
  int bNosync = 0;      /* --nosync.  Omit the auto-sync */
  int width;            /* Width of printed comment lines */
  Stmt mtimeXfer;       /* Statement to transfer mtimes */
  const char *zWidth;   /* Width option string value */
  const char *zCurBrName;      /* Current branch name */
  const char *zNewBrName;      /* New branch name */
  const char *zBrChgMsg = "";  /* Message to display if branch changes */

  if( !internalUpdate ){
    undo_capture_command_line();
    url_proxy_options();
  }
  zWidth = find_option("width","W",1);
  if( zWidth ){
    width = atoi(zWidth);
    if( (width!=0) && (width<=20) ){
      fossil_fatal("-W|--width value must be >20 or 0");
    }
  }else{
    width = -1;
  }
  latestFlag = find_option("latest",0, 0)!=0;
  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("nochange",0,0)!=0; /* deprecated */
  }
  verboseFlag = find_option("verbose","v",0)!=0;
  forceMissingFlag = find_option("force-missing",0,0)!=0;
  debugFlag = find_option("debug",0,0)!=0;
  setmtimeFlag = find_option("setmtime",0,0)!=0;
  keepMergeFlag = find_option("keep-merge-files", "K",0)!=0;
  bNosync = find_option("nosync",0,0)!=0;

  /* We should be done with options.. */
  verify_all_options();

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  zCurBrName = branch_of_rid(vid);
  user_select();
  if( !dryRunFlag && !internalUpdate && !bNosync ){
    if( autosync_loop(SYNC_PULL + SYNC_VERBOSE*verboseFlag, 1, "update") ){
      fossil_fatal("update abandoned due to sync failure");
    }
  }

  /* Create any empty directories now, as well as after the update,
  ** so changes in settings are reflected now */
  if( !dryRunFlag ) ensure_empty_dirs_created(0);

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
       if( tid==0 || !is_a_version(tid) ){
        fossil_fatal("no such check-in: %s", g.argv[2]);
      }
    }
  }

  /* If no VERSION is specified on the command-line, then look for a
  ** descendant of the current version.  If there are multiple descendants,
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
        print_timeline(&q, -100, width, 0, 0);
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
  db_multi_exec(
     "CREATE TEMP TABLE dir_to_delete(name TEXT %s PRIMARY KEY)WITHOUT ROWID",
     filename_collation()
  );
  vfile_check_signature(vid, CKSIG_ENOTFILE);
  if( !dryRunFlag && !internalUpdate ) undo_begin();
  if( load_vfile_from_rid(tid) && !forceMissingFlag ){
    fossil_fatal("missing content, unable to update");
  };

  /*
  ** The record.fn field is used to match files against each other.  The
  ** FV table contains one row for each each unique filename in
  ** in the current check-out, the pivot, and the version being merged.
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
  if( vid ){
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
      file_tree_name(g.argv[i], &treename, 0, 1);
      if( file_isdir(g.argv[i], RepoFILE)==1 ){
        if( blob_size(&treename) != 1 || blob_str(&treename)[0] != '.' ){
          blob_append_sql(&sql, "%sfn NOT GLOB '%q/*' ",
                         zSep /*safe-for-%s*/, blob_str(&treename));
        }else{
          blob_reset(&sql);
          break;
        }
      }else{
        blob_append_sql(&sql, "%sfn<>%Q ",
                        zSep /*safe-for-%s*/, blob_str(&treename));
      }
      zSep = "AND ";
      blob_reset(&treename);
    }
    db_multi_exec("%s", blob_sql_text(&sql));
    blob_reset(&sql);
  }

  /*
  ** Alter the content of the check-out so that it conforms with the
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
  merge_info_init();
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
    const char *zOp = 0;                        /* Type of change.  */
    i64 sz = 0;                                 /* Size of the file */
    int nc = 0;                                 /* Number of conflicts */
    const char *zErrMsg = 0;                    /* Error message */

    zFullPath = mprintf("%s%s", g.zLocalRoot, zName);
    zFullNewPath = mprintf("%s%s", g.zLocalRoot, zNewName);
    sz = file_size(zFullNewPath, ExtFILE);
    nameChng = fossil_strcmp(zName, zNewName);
    nUpdate++;
    if( deleted ){
      db_multi_exec("UPDATE vfile SET deleted=1 WHERE id=%d", idt);
    }
    if( idv>0 && ridv==0 && idt>0 && ridt>0 ){
      /* Conflict.  This file has been added to the current check-out
      ** but also exists in the target check-out.  Use the current version.
      */
      fossil_print("CONFLICT %s\n", zName);
      nConflict++;
      zOp = "CONFLICT";
      nc = 1;
      zErrMsg = "duplicate file";
    }else if( idt>0 && idv==0 ){
      /* File added in the target. */
      if( file_isfile_or_link(zFullPath) ){
        /* Name of backup file with Original content */
        char *zOrig = file_newname(zFullPath, "original", 1);
        /* Backup previously unanaged file before to be overwritten */
        file_copy(zFullPath, zOrig);
        fossil_free(zOrig);
        fossil_print("ADD %s - overwrites an unmanaged file", zName);
        if( !dryRunFlag ) fossil_print(", original copy backed up locally");
        fossil_print("\n");
        nOverwrite++;
        nc = 1;
        zOp = "CONFLICT";
        zErrMsg = "new file overwrites unmanaged file";
      }else{
        fossil_print("ADD %s\n", zName);
      }
      if( !dryRunFlag && !internalUpdate ) undo_save(zName);
      if( !dryRunFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt>0 && idv>0 && ridt!=ridv && (chnged==0 || deleted) ){
      /* The file is unedited.  Change it to the target version */
      if( deleted ){
        fossil_print("UPDATE %s - change to unmanaged file\n", zName);
      }else{
        fossil_print("UPDATE %s\n", zName);
      }
      if( !dryRunFlag && !internalUpdate ) undo_save(zName);
      if( !dryRunFlag ) vfile_to_disk(0, idt, 0, 0);
      zOp = "UPDATE";
    }else if( idt>0 && idv>0 && !deleted && file_size(zFullPath, RepoFILE)<0 ){
      /* The file missing from the local check-out. Restore it to the
      ** version that appears in the target. */
      fossil_print("UPDATE %s\n", zName);
      if( !dryRunFlag && !internalUpdate ) undo_save(zName);
      if( !dryRunFlag ) vfile_to_disk(0, idt, 0, 0);
      zOp = "UPDATE";
    }else if( idt==0 && idv>0 ){
      if( ridv==0 ){
        /* Added in current check-out.  Continue to hold the file as
        ** as an addition */
        db_multi_exec("UPDATE vfile SET vid=%d WHERE id=%d", tid, idv);
      }else if( chnged ){
        /* Edited locally but deleted from the target.  Do not track the
        ** file but keep the edited version around. */
        fossil_print("CONFLICT %s - edited locally but deleted by update\n",
                     zName);
        zOp = "CONFLICT";
        zErrMsg = "edited locally but deleted by update";
        nc = 1;
        nConflict++;
      }else{
        fossil_print("REMOVE %s\n", zName);
        if( !dryRunFlag && !internalUpdate ) undo_save(zName);
        if( !dryRunFlag ){
          char *zDir;
          file_delete(zFullPath);
          zDir = file_dirname(zName);
          while( zDir!=0 ){
            char *zNext;
            db_multi_exec("INSERT OR IGNORE INTO dir_to_delete(name)"
                          "VALUES(%Q)", zDir);
            zNext = db_changes() ? file_dirname(zDir) : 0;
            fossil_free(zDir);
            zDir = zNext;
          }
        }
      }
    }else if( idt>0 && idv>0 && ridt!=ridv && chnged ){
      if( nameChng ){
        fossil_print("MERGE %s -> %s\n", zName, zNewName);
      }else{
        fossil_print("MERGE %s\n", zName);
      }
      if( islinkv || islinkt ){
        fossil_print("***** Cannot merge symlink %s\n", zNewName);
        zOp = "CONFLICT";
        nConflict++;
      }else{
        /* Merge the changes in the current tree into the target version */
        Blob r, t, v;
        int rc;
        unsigned mergeFlags = dryRunFlag ? MERGE_DRYRUN : 0;
        if(keepMergeFlag!=0) mergeFlags |= MERGE_KEEP_FILES;
        if( !dryRunFlag && !internalUpdate ) undo_save(zName);
        content_get(ridt, &t);
        content_get(ridv, &v);
        rc = merge_3way(&v, zFullPath, &t, &r, mergeFlags);
        if( rc>=0 ){
          if( !dryRunFlag ){
            blob_write_to_file(&r, zFullNewPath);
            file_setexe(zFullNewPath, isexe);
          }
          if( rc>0 ){
            nc = rc;
            zOp = "CONFLICT";
            zErrMsg = "merge conflicts";
            fossil_print("***** %d merge conflicts in %s\n", rc, zNewName);
            nConflict++;
          }else{
            zOp = "MERGE";
          }
        }else{
          if( !dryRunFlag ){
            if( !keepMergeFlag ){
              /* Name of backup file with Original content */
              char *zOrig = file_newname(zFullPath, "original", 1);
              /* Backup non-mergeable binary file when --keep-merge-files is
                 not specified */
              file_copy(zFullPath, zOrig);
              fossil_free(zOrig);
            }
            blob_write_to_file(&t, zFullNewPath);
            file_setexe(zFullNewPath, isexe);
          }
          fossil_print("***** Cannot merge binary file %s", zNewName);
          if( !dryRunFlag ){
            fossil_print(", original copy backed up locally");
          }
          fossil_print("\n");
          nConflict++;
          zOp = "ERROR";
          zErrMsg = "cannot merge binary file";
          nc = 1;
        }
        blob_reset(&v);
        blob_reset(&t);
        blob_reset(&r);
      }
      if( nameChng && !dryRunFlag ) file_delete(zFullPath);
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
    if( zOp!=0 ){
      db_multi_exec(
        "INSERT INTO mergestat(op,fnp,ridp,fn,ridv,sz,fnm,ridm,fnr,nc,msg)"
        "VALUES(%Q,%Q,%d,%Q,NULL,%lld,%Q,%d,%Q,%d,%Q)",
        /* op   */ zOp,
        /* fnp  */ zName,
        /* ridp */ ridv,
        /* fn   */ zNewName,
        /* sz   */ sz,
        /* fnm  */ zName,
        /* ridm */ ridt,
        /* fnr  */ zNewName,
        /* nc   */ nc,
        /* msg  */ zErrMsg
      );
    }
    free(zFullPath);
    free(zFullNewPath);
  }
  db_finalize(&q);
  db_finalize(&mtimeXfer);
  fossil_print("%.79c\n",'-');
  zNewBrName = branch_of_rid(tid);
  if( g.argc<3 && fossil_strcmp(zCurBrName, zNewBrName)!=0 ){
    zBrChgMsg = mprintf("  Branch changed from %s to %s.",
                           zCurBrName, zNewBrName);
  }
  if( nUpdate==0 ){
    show_common_info(tid, "checkout:", 1, 0);
    fossil_print("%-13s None. Already up-to-date.%s\n", "changes:", zBrChgMsg);
  }else{
    fossil_print("%-13s %.40s %s\n", "updated-from:", rid_to_uuid(vid),
                 db_text("", "SELECT datetime(mtime) || ' UTC' FROM event "
                         "  WHERE objid=%d", vid));
    show_common_info(tid, "updated-to:", 1, 0);
    fossil_print("%-13s %d file%s modified.%s\n", "changes:",
                 nUpdate, nUpdate>1 ? "s" : "", zBrChgMsg);
  }

  /* Report on conflicts
  */
  if( !dryRunFlag ){
    Stmt q;
    int nMerge = 0;
    db_prepare(&q, "SELECT mhash, id FROM vmerge WHERE id<=0");
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
    leaf_ambiguity_warning(tid, tid);

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
    fossil_warning("\nREMINDER: this was a dry run -"
                   " no files were actually changed "
                   "(checkout is still %.10s).", rid_to_uuid(vid));
  }else{
    char *zPwd;
    ensure_empty_dirs_created(1);
    sqlite3_create_function(g.db, "rmdir", 1, SQLITE_UTF8|SQLITE_DIRECTONLY, 0,
                            file_rmdir_sql_function, 0, 0);
    zPwd = file_getcwd(0,0);
    db_multi_exec(
      "SELECT rmdir(%Q||name) FROM dir_to_delete"
      " WHERE (%Q||name)<>%Q ORDER BY name DESC",
      g.zLocalRoot, g.zLocalRoot, zPwd
    );
    fossil_free(zPwd);
    if( g.argc<=3 ){
      /* All files updated.  Shift the current check-out to the target. */
      db_multi_exec("DELETE FROM vfile WHERE vid!=%d", tid);
      checkout_set_all_exe(tid);
      manifest_to_disk(tid);
      db_set_checkout(tid);
    }else{
      /* A subset of files have been checked out.  Keep the current
      ** check-out unchanged. */
      db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
    }
    if( !internalUpdate ) undo_finish();
    if( setmtimeFlag ) vfile_check_signature(tid, CKSIG_SETMTIME);
    db_end_transaction(0);
  }
}

/*
** Create empty directories specified by the empty-dirs setting.
*/
void ensure_empty_dirs_created(int clearDirTable){
  char *zEmptyDirs = db_get("empty-dirs", 0);
  if( zEmptyDirs!=0 ){
    int i;
    Glob *pGlob = glob_create(zEmptyDirs);

    for(i=0; pGlob!=0 && i<pGlob->nPattern; i++){
      const char *zDir = pGlob->azPattern[i];
      char *zPath = mprintf("%s/%s", g.zLocalRoot, zDir);
      switch( file_isdir(zPath, RepoFILE) ){
        case 0: { /* doesn't exist */
          fossil_free(zPath);
          zPath = mprintf("%s/%s/x", g.zLocalRoot, zDir);
          if( file_mkfolder(zPath, RepoFILE, 0, 1)!=0 ) {
            fossil_warning("couldn't create directory %s as "
                           "required by empty-dirs setting", zDir);
          }
          break;
        }
        case 1: { /* exists, and is a directory */
          /* make sure this directory is not on the delete list */
          if( clearDirTable ){
            db_multi_exec(
              "DELETE FROM dir_to_delete WHERE name=%Q", zDir
            );
          }
          break;
        }
        case 2: { /* exists, but isn't a directory */
          fossil_warning("file %s found, but a directory is required "
                         "by empty-dirs setting", zDir);
        }
      }
      fossil_free(zPath);
    }
    glob_free(pGlob);
  }
}

/*
** Get the manifest record for a given revision, or the current check-out if
** zRevision is NULL.
*/
Manifest *historical_manifest(
  const char *zRevision    /* The check-in to query, or NULL for current */
){
  int vid;
  Manifest *pManifest;

  /* Determine the check-in manifest artifact ID.  Panic on failure. */
  if( zRevision ){
    vid = name_to_typed_rid(zRevision, "ci");
  }else if( !g.localOpen ){
    vid = name_to_typed_rid(db_get("main-branch", 0), "ci");
  }else{
    vid = db_lget_int("checkout", 0);
    if( !is_a_version(vid) ){
      if( vid==0 ) return 0;
      zRevision = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
      if( zRevision ){
        fossil_fatal("check-out artifact is not a check-in: %s", zRevision);
      }else{
        fossil_fatal("invalid check-out artifact ID: %d", vid);
      }
    }
  }

  /* Parse the manifest, given its artifact ID.  Panic on failure. */
  if( !(pManifest = manifest_get(vid, CFTYPE_MANIFEST, 0)) ){
    if( zRevision ){
      fossil_fatal("could not parse manifest for check-in: %s", zRevision);
    }else{
      fossil_fatal("could not parse manifest for current check-out");
    }
  }

  /* Return the manifest pointer.  The caller must use manifest_destroy() to
   * clean up when finished using the manifest. */
  return pManifest;
}

/*
** Get the contents of a file within the check-in "zRevision".  If
** zRevision==NULL then get the file content for the current check-out.
*/
int historical_blob(
  const char *zRevision,   /* The check-in containing the file */
  const char *zFile,       /* Full treename of the file */
  Blob *pBlob,             /* Put the content here */
  int fatal                /* If nonzero, panic if file/artifact not found */
){
  int result = 0;

  /* Get the manifest for the requested check-in version.  This call unavoidably
   * panics on failure even if fatal is not set. */
  Manifest *pManifest = historical_manifest(zRevision);

  /* Try to find the file record within the manifest. */
  ManifestFile *pFile = manifest_file_find(pManifest, zFile);

  if( !pFile ){
    /* Process file-not-found errors. */
    if( fatal ){
      if( zRevision ){
        fossil_fatal("file %s does not exist in check-in %s", zFile, zRevision);
      }else{
        fossil_fatal("no such file: %s", zFile);
      }
    }
  }else{
    /* Get the file's contents. */
    result = content_get(fast_uuid_to_rid(pFile->zUuid), pBlob);

    /* Process artifact-not-found errors. */
    if( !result && fatal ){
      if( zRevision ){
        fossil_fatal("missing artifact %s for file %s in check-in %s",
            pFile->zUuid, zFile, zRevision);
      }else{
        fossil_fatal("missing artifact %s for file %s", pFile->zUuid, zFile);
      }
    }
  }

  /* Deallocate the parsed manifest structure. */
  manifest_destroy(pManifest);

  /* Return 1 on success and (assuming fatal is not set) 0 if not found. */
  return result;
}

/*
** COMMAND: revert
**
** Usage: %fossil revert ?OPTIONS? ?FILE ...?
**
** Revert to the current repository version of FILE, or to
** the baseline VERSION specified with -r flag.
**
** If FILE was part of a rename operation, both the original file
** and the renamed file are reverted.
**
** Using a directory name for any of the FILE arguments is the same
** as using every subdirectory and file beneath that directory.
**
** Revert all files if no file name is provided.
**
** If a file is reverted accidentally, it can be restored using
** the "fossil undo" command.
**
** Options:
**   --noundo                 Do not record changes in the undo/redo log.
**   -r|--revision VERSION    Revert given FILE(s) back to given
**                            VERSION
**
** See also: [[redo]], [[undo]], [[checkout]], [[update]]
*/
void revert_cmd(void){
  Manifest *pCoManifest;          /* Manifest of current check-out */
  Manifest *pRvManifest;          /* Manifest of selected revert version */
  ManifestFile *pCoFile;          /* File within current check-out manifest */
  ManifestFile *pRvFile;          /* File within revert version manifest */
  const char *zFile;              /* Filename relative to check-out root */
  const char *zRevision;          /* Selected revert version, NULL if current */
  Blob record = BLOB_INITIALIZER; /* Contents of each reverted file */
  int useUndo = 1;                /* True to record changes in UNDO */
  int i;
  Stmt q;
  int revertAll = 0;
  int revisionOptNotSupported = 0;

  undo_capture_command_line();
  zRevision = find_option("revision", "r", 1);
  useUndo = find_option("noundo", 0, 0)==0;
  verify_all_options();

  if( g.argc<2 ){
    usage("?OPTIONS? [FILE] ...");
  }
  if( zRevision && g.argc<3 ){
    fossil_fatal("directories or the entire tree can only be reverted"
                 " back to current version");
  }
  db_must_be_within_tree();

  /* Get manifests of revert version and (if different) current check-out. */
  pRvManifest = historical_manifest(zRevision);
  pCoManifest = zRevision ? historical_manifest(0) : 0;

  db_begin_transaction();
  if( useUndo ){
    undo_begin();
  }else{
    undo_reset();
  }
  db_multi_exec("CREATE TEMP TABLE torevert(name UNIQUE);");

  if( g.argc>2 ){
    for(i=2; i<g.argc; i++){
      Blob fname;
      zFile = mprintf("%/", g.argv[i]);
      blob_zero(&fname);
      file_tree_name(zFile, &fname, 0, 1);
      if( blob_eq(&fname, ".") ){
        if( zRevision ){
          revisionOptNotSupported = 1;
          break;
        }
        revertAll = 1;
        break;
      }else if( db_exists(
        "SELECT pathname"
        "  FROM vfile"
        " WHERE (substr(pathname,1,length('%q/'))='%q/'"
        "    OR  substr(origname,1,length('%q/'))='%q/');",
        blob_str(&fname), blob_str(&fname),
        blob_str(&fname), blob_str(&fname)) ){
        int vid;
        vid = db_lget_int("checkout", 0);
        vfile_check_signature(vid, 0);

        if( zRevision ){
          revisionOptNotSupported = 1;
          break;
        }
        db_multi_exec(
          "INSERT OR IGNORE INTO torevert"
          " SELECT pathname"
          "   FROM vfile"
          "  WHERE (substr(pathname,1,length('%q/'))='%q/'"
          "     OR  substr(origname,1,length('%q/'))='%q/')"
          "    AND (chnged OR deleted OR rid=0 OR pathname!=origname);",
          blob_str(&fname), blob_str(&fname),
          blob_str(&fname), blob_str(&fname)
        );
      }else{
        db_multi_exec(
          "REPLACE INTO torevert VALUES(%B);"
          "INSERT OR IGNORE INTO torevert"
          " SELECT pathname"
          "   FROM vfile"
          "  WHERE origname=%B;",
          &fname, &fname
        );
      }
      blob_reset(&fname);
    }
  }else{
    revertAll = 1;
  }

  if( revisionOptNotSupported ){
    fossil_fatal("directories or the entire tree can only be reverted"
                 " back to current version");
  }

  if ( revertAll ){
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
    char *zFull;
    zFile = db_column_text(&q, 0);
    zFull = mprintf("%/%/", g.zLocalRoot, zFile);
    pRvFile = pRvManifest? manifest_file_find(pRvManifest, zFile) : 0;
    if( !pRvFile ){
      if( db_int(0, "SELECT rid FROM vfile WHERE pathname=%Q OR origname=%Q",
                 zFile, zFile)==0 ){
        fossil_print("UNMANAGE %s\n", zFile);
      }else{
        if( useUndo ) undo_save(zFile);
        file_delete(zFull);
        fossil_print("DELETE   %s\n", zFile);
      }
      db_multi_exec(
        "UPDATE OR REPLACE vfile"
        "   SET pathname=origname, origname=NULL"
        " WHERE pathname=%Q AND origname!=pathname;"
        "DELETE FROM vfile WHERE pathname=%Q",
        zFile, zFile
      );
    }else if( file_unsafe_in_tree_path(zFull) ){
      /* Ignore this file */
    }else{
      sqlite3_int64 mtime;
      int rvChnged = 0;
      int rvPerm = manifest_file_mperm(pRvFile);

      /* Determine if reverted-to file is different than checked-out file. */
      if( pCoManifest && (pCoFile = manifest_file_find(pCoManifest, zFile)) ){
        rvChnged = manifest_file_mperm(pRvFile)!=rvPerm
                || fossil_strcmp(pRvFile->zUuid, pCoFile->zUuid)!=0;
      }

      /* Get contents of reverted-to file. */
      content_get(fast_uuid_to_rid(pRvFile->zUuid), &record);

      if( useUndo ) undo_save(zFile);
      if( file_size(zFull, RepoFILE)>=0
       && (rvPerm==PERM_LNK || file_islink(0))
      ){
        file_delete(zFull);
      }
      if( rvPerm==PERM_LNK ){
        symlink_create(blob_str(&record), zFull);
      }else{
        blob_write_to_file(&record, zFull);
      }
      file_setexe(zFull, rvPerm==PERM_EXE);
      fossil_print("REVERT   %s\n", zFile);
      mtime = file_mtime(zFull, RepoFILE);
      db_multi_exec(
         "UPDATE vfile"
         "   SET mtime=%lld, chnged=%d, deleted=0, isexe=%d, islink=%d,"
         "       mrid=rid, mhash=NULL"
         " WHERE pathname=%Q OR origname=%Q",
         mtime, rvChnged, rvPerm==PERM_EXE, rvPerm==PERM_LNK, zFile, zFile
      );
    }
    blob_reset(&record);
    free(zFull);
  }
  db_finalize(&q);
  if( useUndo) undo_finish();
  db_end_transaction(0);

  /* Deallocate parsed manifest structures. */
  manifest_destroy(pRvManifest);
  manifest_destroy(pCoManifest);
}
