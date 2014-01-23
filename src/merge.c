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
** This file contains code used to merge two or more branches into
** a single tree.
*/
#include "config.h"
#include "merge.h"
#include <assert.h>

/*
** Print information about a particular check-in.
*/
void print_checkin_description(int rid, int indent, const char *zLabel){
  Stmt q;
  db_prepare(&q,
     "SELECT datetime(mtime%s),"
     "       coalesce(euser,user), coalesce(ecomment,comment),"
     "       (SELECT uuid FROM blob WHERE rid=%d),"
     "       (SELECT group_concat(substr(tagname,5), ', ') FROM tag, tagxref"
     "         WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid"
     "           AND tagxref.rid=%d AND tagxref.tagtype>0)"
     "  FROM event WHERE objid=%d", timeline_utc(), rid, rid, rid);
  if( db_step(&q)==SQLITE_ROW ){
    const char *zTagList = db_column_text(&q, 4);
    char *zCom;
    if( zTagList && zTagList[0] ){
      zCom = mprintf("%s (%s)", db_column_text(&q, 2), zTagList);
    }else{
      zCom = mprintf("%s", db_column_text(&q,2));
    }
    fossil_print("%-*s [%S] by %s on %s\n%*s", 
       indent-1, zLabel,
       db_column_text(&q, 3),
       db_column_text(&q, 1),
       db_column_text(&q, 0),
       indent, "");
    comment_print(zCom, indent, 78);
    fossil_free(zCom);
  }
  db_finalize(&q);
}


/*
** COMMAND: merge
**
** Usage: %fossil merge ?OPTIONS? ?VERSION?
**
** The argument VERSION is a version that should be merged into the
** current checkout.  All changes from VERSION back to the nearest
** common ancestor are merged.  Except, if either of the --cherrypick or
** --backout options are used only the changes associated with the
** single check-in VERSION are merged.  The --backout option causes
** the changes associated with VERSION to be removed from the current
** checkout rather than added.
**
** If the VERSION argument is omitted, then Fossil attempts to find
** a recent fork on the current branch to merge.
**
** Only file content is merged.  The result continues to use the
** file and directory names from the current checkout even if those
** names might have been changed in the branch being merged in.
**
** Other options:
**
**   --baseline BASELINE     Use BASELINE as the "pivot" of the merge instead
**                           of the nearest common ancestor.  This allows
**                           a sequence of changes in a branch to be merged
**                           without having to merge the entire branch.
**
**   --binary GLOBPATTERN    Treat files that match GLOBPATTERN as binary
**                           and do not try to merge parallel changes.  This
**                           option overrides the "binary-glob" setting.
**
**   --case-sensitive BOOL   Override the case-sensitive setting.  If false,
**                           files whose names differ only in case are taken
**                           to be the same file.
**
**   -f|--force              Force the merge even if it would be a no-op.
**
**   --integrate             Merged branch will be closed when committing.
**
**   -n|--dry-run            If given, display instead of run actions
**
**   -v|--verbose            Show additional details of the merge
*/
void merge_cmd(void){
  int vid;              /* Current version "V" */
  int mid;              /* Version we are merging from "M" */
  int pid;              /* The pivot version - most recent common ancestor P */
  int verboseFlag;      /* True if the -v|--verbose option is present */
  int integrateFlag;    /* True if the --integrate option is present */
  int pickFlag;         /* True if the --cherrypick option is present */
  int backoutFlag;      /* True if the --backout option is present */
  int dryRunFlag;       /* True if the --dry-run or -n option is present */
  int forceFlag;        /* True if the --force or -f option is present */
  const char *zBinGlob; /* The value of --binary */
  const char *zPivot;   /* The value of --baseline */
  int debugFlag;        /* True if --debug is present */
  int nChng;            /* Number of file name changes */
  int *aChng;           /* An array of file name changes */
  int i;                /* Loop counter */
  int nConflict = 0;    /* Number of conflicts seen */
  int nOverwrite = 0;   /* Number of unmanaged files overwritten */
  Stmt q;


  /* Notation:
  **
  **      V     The current checkout
  **      M     The version being merged in
  **      P     The "pivot" - the most recent common ancestor of V and M.
  */

  undo_capture_command_line();
  verboseFlag = find_option("verbose","v",0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("detail",0,0)!=0; /* deprecated */
  }
  pickFlag = find_option("cherrypick",0,0)!=0;
  integrateFlag = find_option("integrate",0,0)!=0;
  backoutFlag = find_option("backout",0,0)!=0;
  debugFlag = find_option("debug",0,0)!=0;
  zBinGlob = find_option("binary",0,1);
  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("nochange",0,0)!=0; /* deprecated */
  }
  forceFlag = find_option("force","f",0)!=0;
  zPivot = find_option("baseline",0,1);
  capture_case_sensitive_option();
  verify_all_options();
  db_must_be_within_tree();
  if( zBinGlob==0 ) zBinGlob = db_get("binary-glob",0);
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("nothing is checked out");
  }

  /* Find mid, the artifactID of the version to be merged into the current
  ** check-out */
  if( g.argc==3 ){
    /* Mid is specified as an argument on the command-line */
    mid = name_to_typed_rid(g.argv[2], "ci");
    if( mid==0 || !is_a_version(mid) ){
      fossil_fatal("not a version: %s", g.argv[2]);
    }
  }else if( g.argc==2 ){
    /* No version specified on the command-line so pick the most recent
    ** leaf that is (1) not the version currently checked out and (2)
    ** has not already been merged into the current checkout and (3)
    ** the leaf is not closed and (4) the leaf is in the same branch
    ** as the current checkout. 
    */
    Stmt q;
    if( pickFlag || backoutFlag || integrateFlag){
      fossil_fatal("cannot use --backout, --cherrypick or --integrate with a fork merge");
    }
    mid = db_int(0,
      "SELECT leaf.rid"
      "  FROM leaf, event"
      " WHERE leaf.rid=event.objid"
      "   AND leaf.rid!=%d"                                /* Constraint (1) */
      "   AND leaf.rid NOT IN (SELECT merge FROM vmerge)"  /* Constraint (2) */
      "   AND NOT EXISTS(SELECT 1 FROM tagxref"            /* Constraint (3) */
                    "     WHERE rid=leaf.rid"
                    "       AND tagid=%d"
                    "       AND tagtype>0)"
      "   AND (SELECT value FROM tagxref"                  /* Constraint (4) */
            "   WHERE tagid=%d AND rid=%d AND tagtype>0) ="
            " (SELECT value FROM tagxref"
            "   WHERE tagid=%d AND rid=leaf.rid AND tagtype>0)"
      " ORDER BY event.mtime DESC LIMIT 1",
      vid, TAG_CLOSED, TAG_BRANCH, vid, TAG_BRANCH
    );
    if( mid==0 ){
      fossil_fatal("no unmerged forks of branch \"%s\"",
        db_text(0, "SELECT value FROM tagxref"
                   " WHERE tagid=%d AND rid=%d AND tagtype>0",
                   TAG_BRANCH, vid)
      );
    }
    db_prepare(&q,
      "SELECT blob.uuid,"
          "   datetime(event.mtime%s),"
          "   coalesce(ecomment, comment),"
          "   coalesce(euser, user)"
      "  FROM event, blob"
      " WHERE event.objid=%d AND blob.rid=%d",
      timeline_utc(), mid, mid
    );
    if( db_step(&q)==SQLITE_ROW ){
      char *zCom = mprintf("Merging fork [%S] at %s by %s: \"%s\"",
            db_column_text(&q, 0), db_column_text(&q, 1),
            db_column_text(&q, 3), db_column_text(&q, 2));
      comment_print(zCom, 0, 79);
      fossil_free(zCom);
    }
    db_finalize(&q);
  }else{
    usage("?OPTIONS? ?VERSION?");
    return;
  }

  if( zPivot ){
    pid = name_to_typed_rid(zPivot, "ci");
    if( pid==0 || !is_a_version(pid) ){
      fossil_fatal("not a version: %s", zPivot);
    }
    if( pickFlag ){
      fossil_fatal("incompatible options: --cherrypick & --baseline");
    }
  }else if( pickFlag || backoutFlag ){
    if( integrateFlag ){
      fossil_fatal("incompatible options: --integrate & --cherrypick or --backout");
    }
    pid = db_int(0, "SELECT pid FROM plink WHERE cid=%d AND isprim", mid);
    if( pid<=0 ){
      fossil_fatal("cannot find an ancestor for %s", g.argv[2]);
    }
  }else{
    pivot_set_primary(mid);
    pivot_set_secondary(vid);
    db_prepare(&q, "SELECT merge FROM vmerge WHERE id=0");
    while( db_step(&q)==SQLITE_ROW ){
      pivot_set_secondary(db_column_int(&q,0));
    }
    db_finalize(&q);
    pid = pivot_find();
    if( pid<=0 ){
      fossil_fatal("cannot find a common ancestor between the current "
                   "checkout and %s", g.argv[2]);
    }
  }
  if( backoutFlag ){
    int t = pid;
    pid = mid;
    mid = t;
  }
  if( !is_a_version(pid) ){
    fossil_fatal("not a version: record #%d", pid);
  }
  if( !forceFlag && mid==pid ){
    fossil_print("Merge skipped because it is a no-op. "
                 " Use --force to override.\n");
    return;
  }
  if( integrateFlag ){
    if( db_exists("SELECT 1 FROM vmerge WHERE id=-4")) {
      /* Fossil earlier than [55cacfcace] cannot handle this,
       * therefore disallow it. */
      fossil_fatal("Integration of another branch already in progress."
        "  Commit or Undo needed first", g.argv[2]);
    }
    if( !is_a_leaf(mid) ){
      fossil_warning("ignoring --integrate: %s is not a leaf", g.argv[2]);
      integrateFlag = 0;
    }
  }
  if( verboseFlag ){
    print_checkin_description(mid, 12, integrateFlag?"integrate:":"merge-from:");
    print_checkin_description(pid, 12, "baseline:");
  }
  vfile_check_signature(vid, CKSIG_ENOTFILE);
  db_begin_transaction();
  if( !dryRunFlag ) undo_begin();
  load_vfile_from_rid(mid);
  load_vfile_from_rid(pid);
  if( debugFlag ){
    char *z;
    z = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", pid);
    fossil_print("P=%d %z\n", pid, z);
    z = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", mid);
    fossil_print("M=%d %z\n", mid, z);
    z = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
    fossil_print("V=%d %z\n", vid, z);
  }

  /*
  ** The vfile.pathname field is used to match files against each other.  The
  ** FV table contains one row for each each unique filename in
  ** in the current checkout, the pivot, and the version being merged.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS fv;"
    "CREATE TEMP TABLE fv("
    "  fn TEXT PRIMARY KEY %s,"   /* The filename */
    "  idv INTEGER,"              /* VFILE entry for current version */
    "  idp INTEGER,"              /* VFILE entry for the pivot */
    "  idm INTEGER,"              /* VFILE entry for version merging in */
    "  chnged BOOLEAN,"           /* True if current version has been edited */
    "  ridv INTEGER,"             /* Record ID for current version */
    "  ridp INTEGER,"             /* Record ID for pivot */
    "  ridm INTEGER,"             /* Record ID for merge */
    "  isexe BOOLEAN,"            /* Execute permission enabled */
    "  fnp TEXT %s,"              /* The filename in the pivot */
    "  fnm TEXT %s,"              /* the filename in the merged version */
    "  islinkv BOOLEAN,"          /* True if current version is a symlink */
    "  islinkm BOOLEAN"           /* True if merged version in is a symlink */
    ");",
    filename_collation(), filename_collation(), filename_collation()
  );

  /* Add files found in V
  */
  db_multi_exec(
    "INSERT OR IGNORE"
    " INTO fv(fn,fnp,fnm,idv,idp,idm,ridv,ridp,ridm,isexe,chnged)"
    " SELECT pathname, pathname, pathname, id, 0, 0, rid, 0, 0, isexe, chnged "
    " FROM vfile WHERE vid=%d",
    vid
  );

  /*
  ** Compute name changes from P->V
  */
  find_filename_changes(pid, vid, 0, &nChng, &aChng, debugFlag ? "P->V" : 0);
  if( nChng ){
    for(i=0; i<nChng; i++){
      char *z;
      z = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2]);
      db_multi_exec(
        "UPDATE fv SET fnp=%Q, fnm=%Q"
        " WHERE fn=(SELECT name FROM filename WHERE fnid=%d)",
        z, z, aChng[i*2+1]
      );
      free(z);
    }
    fossil_free(aChng);
    db_multi_exec("UPDATE fv SET fnm=fnp WHERE fnp!=fn");
  }

  /* Add files found in P but not in V
  */
  db_multi_exec(
    "INSERT OR IGNORE"
    " INTO fv(fn,fnp,fnm,idv,idp,idm,ridv,ridp,ridm,isexe,chnged)"
    " SELECT pathname, pathname, pathname, 0, 0, 0, 0, 0, 0, isexe, 0 "
    "   FROM vfile"
    "  WHERE vid=%d AND pathname %s NOT IN (SELECT fnp FROM fv)",
    pid, filename_collation()
  );

  /*
  ** Compute name changes from P->M
  */
  find_filename_changes(pid, mid, 0, &nChng, &aChng, debugFlag ? "P->M" : 0);
  if( nChng ){
    if( nChng>4 ) db_multi_exec("CREATE INDEX fv_fnp ON fv(fnp)");
    for(i=0; i<nChng; i++){
      db_multi_exec(
        "UPDATE fv SET fnm=(SELECT name FROM filename WHERE fnid=%d)"
        " WHERE fnp=(SELECT name FROM filename WHERE fnid=%d)",
        aChng[i*2+1], aChng[i*2]
      );
    }
    fossil_free(aChng);
  }

  /* Add files found in M but not in P or V.
  */
  db_multi_exec(
    "INSERT OR IGNORE"
    " INTO fv(fn,fnp,fnm,idv,idp,idm,ridv,ridp,ridm,isexe,chnged)"
    " SELECT pathname, pathname, pathname, 0, 0, 0, 0, 0, 0, isexe, 0 "
    "   FROM vfile"
    "  WHERE vid=%d"
    "    AND pathname %s NOT IN (SELECT fnp FROM fv UNION SELECT fnm FROM fv)",
    mid, filename_collation()
  );

  /*
  ** Compute the file version ids for P and M.
  */
  db_multi_exec(
    "UPDATE fv SET"
    " idp=coalesce((SELECT id FROM vfile WHERE vid=%d AND fnp=pathname),0),"
    " ridp=coalesce((SELECT rid FROM vfile WHERE vid=%d AND fnp=pathname),0),"
    " idm=coalesce((SELECT id FROM vfile WHERE vid=%d AND fnm=pathname),0),"
    " ridm=coalesce((SELECT rid FROM vfile WHERE vid=%d AND fnm=pathname),0),"
    " islinkv=coalesce((SELECT islink FROM vfile"
                    " WHERE vid=%d AND fnm=pathname),0),"
    " islinkm=coalesce((SELECT islink FROM vfile"
                    " WHERE vid=%d AND fnm=pathname),0)",
    pid, pid, mid, mid, vid, mid
  );

  if( debugFlag ){
    db_prepare(&q,
       "SELECT rowid, fn, fnp, fnm, chnged, ridv, ridp, ridm, "
       "       isexe, islinkv, islinkm FROM fv"
    );
    while( db_step(&q)==SQLITE_ROW ){
       fossil_print("%3d: ridv=%-4d ridp=%-4d ridm=%-4d chnged=%d isexe=%d "
                    " islinkv=%d islinkm=%d\n",
          db_column_int(&q, 0),
          db_column_int(&q, 5),
          db_column_int(&q, 6),
          db_column_int(&q, 7),
          db_column_int(&q, 4),
          db_column_int(&q, 8),
          db_column_int(&q, 9),
          db_column_int(&q, 10));
       fossil_print("     fn  = [%s]\n", db_column_text(&q, 1));
       fossil_print("     fnp = [%s]\n", db_column_text(&q, 2));
       fossil_print("     fnm = [%s]\n", db_column_text(&q, 3));
    }
    db_finalize(&q);
  }

  /*
  ** Find files in M and V but not in P and report conflicts.
  ** The file in M will be ignored.  It will be treated as if it
  ** does not exist.
  */
  db_prepare(&q,
    "SELECT idm FROM fv WHERE idp=0 AND idv>0 AND idm>0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idm = db_column_int(&q, 0);
    char *zName = db_text(0, "SELECT pathname FROM vfile WHERE id=%d", idm);
    fossil_warning("WARNING - no common ancestor: %s", zName);
    free(zName);
    db_multi_exec("UPDATE fv SET idm=0 WHERE idm=%d", idm);
  }
  db_finalize(&q);

  /*
  ** Add to V files that are not in V or P but are in M
  */
  db_prepare(&q,
    "SELECT idm, rowid, fnm FROM fv AS x"
    " WHERE idp=0 AND idv=0 AND idm>0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idm = db_column_int(&q, 0);
    int rowid = db_column_int(&q, 1);
    int idv;
    const char *zName;
    char *zFullName;
    db_multi_exec(
      "INSERT INTO vfile(vid,chnged,deleted,rid,mrid,isexe,islink,pathname)"
      "  SELECT %d,%d,0,rid,mrid,isexe,islink,pathname FROM vfile WHERE id=%d",
      vid, integrateFlag?5:3, idm
    );
    idv = db_last_insert_rowid();
    db_multi_exec("UPDATE fv SET idv=%d WHERE rowid=%d", idv, rowid);
    zName = db_column_text(&q, 2);
    zFullName = mprintf("%s%s", g.zLocalRoot, zName);
    if( file_wd_isfile_or_link(zFullName) ){
      fossil_print("ADDED %s (overwrites an unmanaged file)\n", zName);
      nOverwrite++;
    }else{
      fossil_print("ADDED %s\n", zName);
    }
    fossil_free(zFullName);
    if( !dryRunFlag ){
      undo_save(zName);
      vfile_to_disk(0, idm, 0, 0);
    }
  }
  db_finalize(&q);
  
  /*
  ** Find files that have changed from P->M but not P->V. 
  ** Copy the M content over into V.
  */
  db_prepare(&q,
    "SELECT idv, ridm, fn, islinkm FROM fv"
    " WHERE idp>0 AND idv>0 AND idm>0"
    "   AND ridm!=ridp AND ridv=ridp AND NOT chnged"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    int ridm = db_column_int(&q, 1);
    const char *zName = db_column_text(&q, 2);
    int islinkm = db_column_int(&q, 3);
    /* Copy content from idm over into idv.  Overwrite idv. */
    fossil_print("UPDATE %s\n", zName);
    if( !dryRunFlag ){
      undo_save(zName);
      db_multi_exec(
        "UPDATE vfile SET mtime=0, mrid=%d, chnged=%d, islink=%d "
        " WHERE id=%d", ridm, integrateFlag?4:2, islinkm, idv
      );
      vfile_to_disk(0, idv, 0, 0);
    }
  }
  db_finalize(&q);

  /*
  ** Do a three-way merge on files that have changes on both P->M and P->V.
  */
  db_prepare(&q,
    "SELECT ridm, idv, ridp, ridv, %s, fn, isexe, islinkv, islinkm FROM fv"
    " WHERE idp>0 AND idv>0 AND idm>0"
    "   AND ridm!=ridp AND (ridv!=ridp OR chnged)",
    glob_expr("fv.fn", zBinGlob)
  );
  while( db_step(&q)==SQLITE_ROW ){
    int ridm = db_column_int(&q, 0);
    int idv = db_column_int(&q, 1);
    int ridp = db_column_int(&q, 2);
    int ridv = db_column_int(&q, 3);
    int isBinary = db_column_int(&q, 4);
    const char *zName = db_column_text(&q, 5);
    int isExe = db_column_int(&q, 6);
    int islinkv = db_column_int(&q, 7);
    int islinkm = db_column_int(&q, 8);
    int rc;
    char *zFullPath;
    Blob m, p, r;
    /* Do a 3-way merge of idp->idm into idp->idv.  The results go into idv. */
    if( verboseFlag ){
      fossil_print("MERGE %s  (pivot=%d v1=%d v2=%d)\n", 
                   zName, ridp, ridm, ridv);
    }else{
      fossil_print("MERGE %s\n", zName);
    }
    if( islinkv || islinkm /* || file_wd_islink(zFullPath) */ ){
      fossil_print("***** Cannot merge symlink %s\n", zName);
      nConflict++;        
    }else{
      undo_save(zName);
      zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
      content_get(ridp, &p);
      content_get(ridm, &m);
      if( isBinary ){
        rc = -1;
        blob_zero(&r);
      }else{
        unsigned mergeFlags = dryRunFlag ? MERGE_DRYRUN : 0;
        rc = merge_3way(&p, zFullPath, &m, &r, mergeFlags);
      }
      if( rc>=0 ){
        if( !dryRunFlag ){
          blob_write_to_file(&r, zFullPath);
          file_wd_setexe(zFullPath, isExe);
        }
        db_multi_exec("UPDATE vfile SET mtime=0 WHERE id=%d", idv);
        if( rc>0 ){
          fossil_print("***** %d merge conflicts in %s\n", rc, zName);
          nConflict++;
        }
      }else{
        fossil_print("***** Cannot merge binary file %s\n", zName);
        nConflict++;
      }
      blob_reset(&p);
      blob_reset(&m);
      blob_reset(&r);
    }
    db_multi_exec("INSERT OR IGNORE INTO vmerge(id,merge) VALUES(%d,%d)",
                  idv,ridm);
  }
  db_finalize(&q);

  /*
  ** Drop files that are in P and V but not in M
  */
  db_prepare(&q,
    "SELECT idv, fn, chnged FROM fv"
    " WHERE idp>0 AND idv>0 AND idm=0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    const char *zName = db_column_text(&q, 1);
    int chnged = db_column_int(&q, 2);
    /* Delete the file idv */
    fossil_print("DELETE %s\n", zName);
    if( chnged ){
      fossil_warning("WARNING: local edits lost for %s\n", zName);
      nConflict++;
    }
    undo_save(zName);
    db_multi_exec(
      "UPDATE vfile SET deleted=1 WHERE id=%d", idv
    );
    if( !dryRunFlag ){
      char *zFullPath = mprintf("%s%s", g.zLocalRoot, zName);
      file_delete(zFullPath);
      free(zFullPath);
    }
  }
  db_finalize(&q);

  /*
  ** Rename files that have taken a rename on P->M but which keep the same
  ** name o P->V.   If a file is renamed on P->V only or on both P->V and
  ** P->M then we retain the V name of the file.
  */
  db_prepare(&q,
    "SELECT idv, fnp, fnm FROM fv"
    " WHERE idv>0 AND idp>0 AND idm>0 AND fnp=fn AND fnm!=fnp"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    const char *zOldName = db_column_text(&q, 1);
    const char *zNewName = db_column_text(&q, 2);
    fossil_print("RENAME %s -> %s\n", zOldName, zNewName);
    undo_save(zOldName);
    undo_save(zNewName);
    db_multi_exec(
      "UPDATE vfile SET pathname=%Q, origname=coalesce(origname,pathname)"
      " WHERE id=%d AND vid=%d", zNewName, idv, vid
    );
    if( !dryRunFlag ){
      char *zFullOldPath = mprintf("%s%s", g.zLocalRoot, zOldName);
      char *zFullNewPath = mprintf("%s%s", g.zLocalRoot, zNewName);
      if( file_wd_islink(zFullOldPath) ){
        symlink_copy(zFullOldPath, zFullNewPath);
      }else{
        file_copy(zFullOldPath, zFullNewPath);
      }
      file_delete(zFullOldPath);
      free(zFullNewPath);
      free(zFullOldPath);
    }
  }
  db_finalize(&q);


  /* Report on conflicts
  */
  if( nConflict ){
    fossil_warning("WARNING: %d merge conflicts", nConflict);
  }
  if( nOverwrite ){
    fossil_warning("WARNING: %d unmanaged files were overwritten",
                   nOverwrite);
  }
  if( dryRunFlag ){
    fossil_warning("REMINDER: this was a dry run -"
                   " no file were actually changed.");
  }

  /*
  ** Clean up the mid and pid VFILE entries.  Then commit the changes.
  */
  db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
  if( pickFlag ){
    db_multi_exec("INSERT OR IGNORE INTO vmerge(id,merge) VALUES(-1,%d)",mid);
    /* For a cherry-pick merge, make the default check-in comment the same
    ** as the check-in comment on the check-in that is being merged in. */
    db_multi_exec(
       "REPLACE INTO vvar(name,value)"
       " SELECT 'ci-comment', coalesce(ecomment,comment) FROM event"
       "  WHERE type='ci' AND objid=%d",
       mid
    );
  }else if( backoutFlag ){
    db_multi_exec("INSERT OR IGNORE INTO vmerge(id,merge) VALUES(-2,%d)",pid);
  }else if( integrateFlag ){
    db_multi_exec("INSERT OR IGNORE INTO vmerge(id,merge) VALUES(-4,%d)",mid);
  }else{
    db_multi_exec("INSERT OR IGNORE INTO vmerge(id,merge) VALUES(0,%d)", mid);
  }
  undo_finish();
  db_end_transaction(dryRunFlag);
}
