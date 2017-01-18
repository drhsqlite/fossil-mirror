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
     "SELECT datetime(mtime,toLocal()),"
     "       coalesce(euser,user), coalesce(ecomment,comment),"
     "       (SELECT uuid FROM blob WHERE rid=%d),"
     "       (SELECT group_concat(substr(tagname,5), ', ') FROM tag, tagxref"
     "         WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid"
     "           AND tagxref.rid=%d AND tagxref.tagtype>0)"
     "  FROM event WHERE objid=%d", rid, rid, rid);
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
    comment_print(zCom, db_column_text(&q,2), indent, -1, g.comFmtFlags);
    fossil_free(zCom);
  }
  db_finalize(&q);
}


/* Pick the most recent leaf that is (1) not equal to vid and (2)
** has not already been merged into vid and (3) the leaf is not
** closed and (4) the leaf is in the same branch as vid.
**
** Set vmergeFlag to control whether the vmerge table is checked.
*/
int fossil_find_nearest_fork(int vid, int vmergeFlag){
  Blob sql;
  Stmt q;
  int rid = 0;

  blob_zero(&sql);
  blob_append_sql(&sql,
    "SELECT leaf.rid"
    "  FROM leaf, event"
    " WHERE leaf.rid=event.objid"
    "   AND leaf.rid!=%d",                                /* Constraint (1) */
    vid
  );
  if( vmergeFlag ){
    blob_append_sql(&sql,
      "   AND leaf.rid NOT IN (SELECT merge FROM vmerge)"  /* Constraint (2) */
    );
  }
  blob_append_sql(&sql,
    "   AND NOT EXISTS(SELECT 1 FROM tagxref"            /* Constraint (3) */
                  "     WHERE rid=leaf.rid"
                  "       AND tagid=%d"
                  "       AND tagtype>0)"
    "   AND (SELECT value FROM tagxref"                  /* Constraint (4) */
          "   WHERE tagid=%d AND rid=%d AND tagtype>0) ="
          " (SELECT value FROM tagxref"
          "   WHERE tagid=%d AND rid=leaf.rid AND tagtype>0)"
    " ORDER BY event.mtime DESC LIMIT 1",
    TAG_CLOSED, TAG_BRANCH, vid, TAG_BRANCH
  );
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  if( db_step(&q)==SQLITE_ROW ){
    rid = db_column_int(&q, 0);
  }
  db_finalize(&q);
  return rid;
}

/*
** Check content that was received with rcvid and return true if any
** fork was created.
*/
int fossil_any_has_fork(int rcvid){
  static Stmt q;
  int fForkSeen = 0;

  if( rcvid==0 ) return 0;
  db_static_prepare(&q,
    "  SELECT pid FROM plink WHERE pid>0 AND isprim"
    "     AND cid IN (SELECT blob.rid FROM blob"
    "   WHERE rcvid=:rcvid)");
  db_bind_int(&q, ":rcvid", rcvid);
  while( !fForkSeen && db_step(&q)==SQLITE_ROW ){
    int pid = db_column_int(&q, 0);
    if( count_nonbranch_children(pid)>1 ){
      compute_leaves(pid,1);
      if( db_int(0, "SELECT count(*) FROM leaves")>1 ){
        int rid = db_int(0, "SELECT rid FROM leaves, event"
                            " WHERE event.objid=leaves.rid"
                            " ORDER BY event.mtime DESC LIMIT 1");
        fForkSeen = fossil_find_nearest_fork(rid, db_open_local(0))!=0;
      }
    }
  }
  db_finalize(&q);
  return fForkSeen;
}

/*
** Add an entry to the FV table for all files renamed between
** version N and the version specified by vid.
*/
static void add_renames(
  const char *zFnCol, /* The FV column for the filename in vid */
  int vid,            /* The desired version's RID */
  int nid,            /* Version N's RID */
  int revOk,          /* Ok to move backwards (child->parent) if true */
  const char *zDebug  /* Generate trace output if not NULL */
){
  int nChng;  /* Number of file name changes */
  int *aChng; /* An array of file name changes */
  int i;      /* Loop counter */
  find_filename_changes(nid, vid, revOk, &nChng, &aChng, zDebug);
  if( nChng==0 ) return;
  for(i=0; i<nChng; i++){
    char *zN, *zV;
    zN = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2]);
    zV = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2+1]);
    db_multi_exec(
      "INSERT OR IGNORE INTO fv(%s,fnn) VALUES(%Q,%Q)",
      zFnCol /*safe-for-%s*/, zV, zN
    );
    if( db_changes()==0 ){
      db_multi_exec(
        "UPDATE fv SET %s=%Q WHERE fnn=%Q",
        zFnCol /*safe-for-%s*/, zV, zN
      );
    }
    free(zN);
    free(zV);
  }
  free(aChng);
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
**   --force-missing         Force the merge even if there is missing content.
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
  int pid = 0;          /* The pivot version - most recent common ancestor P */
  int nid = 0;          /* The name pivot version "N" */
  int verboseFlag;      /* True if the -v|--verbose option is present */
  int integrateFlag;    /* True if the --integrate option is present */
  int pickFlag;         /* True if the --cherrypick option is present */
  int backoutFlag;      /* True if the --backout option is present */
  int dryRunFlag;       /* True if the --dry-run or -n option is present */
  int forceFlag;        /* True if the --force or -f option is present */
  int forceMissingFlag; /* True if the --force-missing option is present */
  const char *zBinGlob; /* The value of --binary */
  const char *zPivot;   /* The value of --baseline */
  int debugFlag;        /* True if --debug is present */
  int nConflict = 0;    /* Number of conflicts seen */
  int nOverwrite = 0;   /* Number of unmanaged files overwritten */
  char vAncestor = 'p'; /* If P is an ancestor of V then 'p', else 'n' */
  Stmt q;


  /* Notation:
  **
  **      V     The current checkout
  **      M     The version being merged in
  **      P     The "pivot" - the most recent common ancestor of V and M.
  **      N     The "name pivot" - for detecting renames
  */

  undo_capture_command_line();
  verboseFlag = find_option("verbose","v",0)!=0;
  forceMissingFlag = find_option("force-missing",0,0)!=0;
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
  verify_all_options();
  db_must_be_within_tree();
  if( zBinGlob==0 ) zBinGlob = db_get("binary-glob",0);
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("nothing is checked out");
  }
  if( !dryRunFlag ){
    if( autosync_loop(SYNC_PULL + SYNC_VERBOSE*verboseFlag,
                      db_get_int("autosync-tries", 1), 1) ){
      fossil_fatal("merge abandoned due to sync failure");
    }
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
    mid = fossil_find_nearest_fork(vid, db_open_local(0));
    if( mid==0 ){
      fossil_fatal("no unmerged forks of branch \"%s\"",
        db_text(0, "SELECT value FROM tagxref"
                   " WHERE tagid=%d AND rid=%d AND tagtype>0",
                   TAG_BRANCH, vid)
      );
    }
    db_prepare(&q,
      "SELECT blob.uuid,"
          "   datetime(event.mtime,toLocal()),"
          "   coalesce(ecomment, comment),"
          "   coalesce(euser, user)"
      "  FROM event, blob"
      " WHERE event.objid=%d AND blob.rid=%d",
      mid, mid
    );
    if( db_step(&q)==SQLITE_ROW ){
      char *zCom = mprintf("Merging fork [%S] at %s by %s: \"%s\"",
            db_column_text(&q, 0), db_column_text(&q, 1),
            db_column_text(&q, 3), db_column_text(&q, 2));
      comment_print(zCom, db_column_text(&q,2), 0, -1, g.comFmtFlags);
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
  }
  if( pickFlag || backoutFlag ){
    if( integrateFlag ){
      fossil_fatal("incompatible options: --integrate & --cherrypick or --backout");
    }
    pid = db_int(0, "SELECT pid FROM plink WHERE cid=%d AND isprim", mid);
    if( pid<=0 ){
      fossil_fatal("cannot find an ancestor for %s", g.argv[2]);
    }
  }else{
    if( !zPivot ){
      pivot_set_primary(mid);
      pivot_set_secondary(vid);
      db_prepare(&q, "SELECT merge FROM vmerge WHERE id=0");
      while( db_step(&q)==SQLITE_ROW ){
        pivot_set_secondary(db_column_int(&q,0));
      }
      db_finalize(&q);
      pid = pivot_find(0);
      if( pid<=0 ){
        fossil_fatal("cannot find a common ancestor between the current "
                     "checkout and %s", g.argv[2]);
      }
    }
    pivot_set_primary(mid);
    pivot_set_secondary(vid);
    nid = pivot_find(1);
    if( nid!=pid ){
      pivot_set_primary(nid);
      pivot_set_secondary(pid);
      nid = pivot_find(1);
    }
  }
  if( backoutFlag ){
    int t = pid;
    pid = mid;
    mid = t;
  }
  if( nid==0 ) nid = pid;
  if( !is_a_version(pid) ){
    fossil_fatal("not a version: record #%d", pid);
  }
  if( !forceFlag && mid==pid ){
    fossil_print("Merge skipped because it is a no-op. "
                 " Use --force to override.\n");
    return;
  }
  if( integrateFlag && !is_a_leaf(mid)){
    fossil_warning("ignoring --integrate: %s is not a leaf", g.argv[2]);
    integrateFlag = 0;
  }
  if( verboseFlag ){
    print_checkin_description(mid, 12, integrateFlag?"integrate:":"merge-from:");
    print_checkin_description(pid, 12, "baseline:");
  }
  vfile_check_signature(vid, CKSIG_ENOTFILE);
  db_begin_transaction();
  if( !dryRunFlag ) undo_begin();
  if( load_vfile_from_rid(mid) && !forceMissingFlag ){
    fossil_fatal("missing content, unable to merge");
  }
  if( load_vfile_from_rid(pid) && !forceMissingFlag ){
    fossil_fatal("missing content, unable to merge");
  }
  if( zPivot ){
    vAncestor = db_exists(
      "WITH RECURSIVE ancestor(id) AS ("
      "  VALUES(%d)"
      "  UNION"
      "  SELECT pid FROM plink, ancestor"
      "   WHERE cid=ancestor.id AND pid!=%d AND cid!=%d)"
      "SELECT 1 FROM ancestor WHERE id=%d LIMIT 1",
      vid, nid, pid, pid
    ) ? 'p' : 'n';
  }
  if( debugFlag ){
    char *z;
    z = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", nid);
    fossil_print("N=%d %z\n", nid, z);
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
    "  fn TEXT UNIQUE %s,"        /* The filename */
    "  idv INTEGER DEFAULT 0,"    /* VFILE entry for current version */
    "  idp INTEGER DEFAULT 0,"    /* VFILE entry for the pivot */
    "  idm INTEGER DEFAULT 0,"    /* VFILE entry for version merging in */
    "  chnged BOOLEAN,"           /* True if current version has been edited */
    "  ridv INTEGER DEFAULT 0,"   /* Record ID for current version */
    "  ridp INTEGER DEFAULT 0,"   /* Record ID for pivot */
    "  ridm INTEGER DEFAULT 0,"   /* Record ID for merge */
    "  isexe BOOLEAN,"            /* Execute permission enabled */
    "  fnp TEXT UNIQUE %s,"       /* The filename in the pivot */
    "  fnm TEXT UNIQUE %s,"       /* The filename in the merged version */
    "  fnn TEXT UNIQUE %s,"       /* The filename in the name pivot */
    "  islinkv BOOLEAN,"          /* True if current version is a symlink */
    "  islinkm BOOLEAN"           /* True if merged version in is a symlink */
    ");",
    filename_collation(), filename_collation(), filename_collation(),
    filename_collation()
  );

  /*
  ** Compute name changes from N to V, P, and M
  */
  add_renames("fn", vid, nid, 0, debugFlag ? "N->V" : 0);
  add_renames("fnp", pid, nid, 0, debugFlag ? "N->P" : 0);
  add_renames("fnm", mid, nid, backoutFlag, debugFlag ? "N->M" : 0);

  /*
  ** Add files found in V
  */
  db_multi_exec(
    "UPDATE OR IGNORE fv SET fn=coalesce(fn%c,fnn) WHERE fn IS NULL;"
    "REPLACE INTO fv(fn,fnp,fnm,fnn,idv,ridv,islinkv,isexe,chnged)"
    " SELECT pathname, fnp, fnm, fnn, id, rid, islink, vf.isexe, vf.chnged"
    "   FROM vfile vf"
    "   LEFT JOIN fv ON fn=coalesce(origname,pathname)"
    "    AND rid>0 AND vf.chnged NOT IN (3,5)"
    "  WHERE vid=%d;",
    vAncestor, vid
  );

  /*
  ** Add files found in P
  */
  db_multi_exec(
    "UPDATE OR IGNORE fv SET fnp=coalesce(fnn,"
    "   (SELECT coalesce(origname,pathname) FROM vfile WHERE id=idv))"
    " WHERE fnp IS NULL;"
    "INSERT OR IGNORE INTO fv(fnp)"
    " SELECT coalesce(origname,pathname) FROM vfile WHERE vid=%d;",
    pid
  );

  /*
  ** Add files found in M
  */
  db_multi_exec(
    "UPDATE OR IGNORE fv SET fnm=fnp WHERE fnm IS NULL;"
    "INSERT OR IGNORE INTO fv(fnm)"
    " SELECT pathname FROM vfile WHERE vid=%d;",
    mid
  );

  /*
  ** Compute the file version ids for P and M
  */
  if( pid==vid ){
    db_multi_exec(
      "UPDATE fv SET idp=idv, ridp=ridv WHERE ridv>0 AND chnged NOT IN (3,5)"
    );
  }else{
    db_multi_exec(
      "UPDATE fv SET"
      " idp=coalesce((SELECT id FROM vfile WHERE vid=%d AND fnp=pathname),0),"
      " ridp=coalesce((SELECT rid FROM vfile WHERE vid=%d AND fnp=pathname),0)",
      pid, pid
    );
  }
  db_multi_exec(
    "UPDATE fv SET"
    " idm=coalesce((SELECT id FROM vfile WHERE vid=%d AND fnm=pathname),0),"
    " ridm=coalesce((SELECT rid FROM vfile WHERE vid=%d AND fnm=pathname),0),"
    " islinkm=coalesce((SELECT islink FROM vfile"
                    " WHERE vid=%d AND fnm=pathname),0),"
    " isexe=coalesce((SELECT isexe FROM vfile WHERE vid=%d AND fnm=pathname),"
    "   isexe)",
    mid, mid, mid, mid
  );

  if( debugFlag ){
    db_prepare(&q,
       "SELECT rowid, fn, fnp, fnm, chnged, ridv, ridp, ridm, "
       "       isexe, islinkv, islinkm, fnn FROM fv"
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
       fossil_print("     fnn = [%s]\n", db_column_text(&q, 11));
    }
    db_finalize(&q);
  }

  /*
  ** Update the execute bit on files where it's changed from P->M but not P->V
  */
  db_prepare(&q,
    "SELECT idv, fn, fv.isexe FROM fv, vfile p, vfile v"
    " WHERE p.id=idp AND v.id=idv AND fv.isexe!=p.isexe AND v.isexe=p.isexe"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    const char *zName = db_column_text(&q, 1);
    int isExe = db_column_int(&q, 2);
    fossil_print("%s %s\n", isExe ? "EXECUTABLE" : "UNEXEC", zName);
    if( !dryRunFlag ){
      char *zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
      file_wd_setexe(zFullPath, isExe);
      free(zFullPath);
      db_multi_exec("UPDATE vfile SET isexe=%d WHERE id=%d", isExe, idv);
    }
  }
  db_finalize(&q);

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
    fossil_warning("WARNING: no common ancestor for %s", zName);
    free(zName);
    db_multi_exec("UPDATE fv SET idm=0 WHERE idm=%d", idm);
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
      if( !dryRunFlag ) undo_save(zName);
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
      fossil_warning("WARNING: local edits lost for %s", zName);
      nConflict++;
    }
    if( !dryRunFlag ) undo_save(zName);
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

  /* For certain sets of renames (e.g. A -> B and B -> A), a file that is
  ** being renamed must first be moved to a temporary location to avoid
  ** being overwritten by another rename operation. A row is added to the
  ** TMPRN table for each of these temporary renames.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS tmprn;"
    "CREATE TEMP TABLE tmprn(fn UNIQUE, tmpfn);"
  );

  /*
  ** Rename files that have taken a rename on P->M but which keep the same
  ** name on P->V.  If a file is renamed on P->V only or on both P->V and
  ** P->M then we retain the V name of the file.
  */
  db_prepare(&q,
    "SELECT idv, fnp, fnm, isexe FROM fv"
    " WHERE idv>0 AND idp>0 AND idm>0 AND fnp=fn AND fnm!=fnp"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    const char *zOldName = db_column_text(&q, 1);
    const char *zNewName = db_column_text(&q, 2);
    int isExe = db_column_int(&q, 3);
    fossil_print("RENAME %s -> %s\n", zOldName, zNewName);
    if( !dryRunFlag ) undo_save(zOldName);
    if( !dryRunFlag ) undo_save(zNewName);
    db_multi_exec(
      "UPDATE vfile SET pathname=NULL, origname=pathname"
      " WHERE vid=%d AND pathname=%Q;"
      "UPDATE vfile SET pathname=%Q, origname=coalesce(origname,pathname)"
      " WHERE id=%d;",
      vid, zNewName, zNewName, idv
    );
    if( !dryRunFlag ){
      char *zFullOldPath, *zFullNewPath;
      zFullOldPath = db_text(0,"SELECT tmpfn FROM tmprn WHERE fn=%Q", zOldName);
      if( !zFullOldPath ){
        zFullOldPath = mprintf("%s%s", g.zLocalRoot, zOldName);
      }
      zFullNewPath = mprintf("%s%s", g.zLocalRoot, zNewName);
      if( file_wd_size(zFullNewPath)>=0 ){
        Blob tmpPath;
        file_tempname(&tmpPath, "");
        db_multi_exec("INSERT INTO tmprn(fn,tmpfn) VALUES(%Q,%Q)",
                      zNewName, blob_str(&tmpPath));
        if( file_wd_islink(zFullNewPath) ){
          symlink_copy(zFullNewPath, blob_str(&tmpPath));
        }else{
          file_copy(zFullNewPath, blob_str(&tmpPath));
        }
        blob_reset(&tmpPath);
      }
      if( file_wd_islink(zFullOldPath) ){
        symlink_copy(zFullOldPath, zFullNewPath);
      }else{
        file_copy(zFullOldPath, zFullNewPath);
      }
      file_wd_setexe(zFullNewPath, isExe);
      file_delete(zFullOldPath);
      free(zFullNewPath);
      free(zFullOldPath);
    }
  }
  db_finalize(&q);

  /* A file that has been deleted and replaced by a renamed file will have a
  ** NULL pathname. Change it to something that makes the output of "status"
  ** and similar commands make sense for such files and that will (most likely)
  ** not be an actual existing pathname.
  */
  db_multi_exec(
    "UPDATE vfile SET pathname=origname || ' (overwritten by rename)'"
    " WHERE pathname IS NULL"
  );

  /*
  ** Add to V files that are not in V or P but are in M
  */
  db_prepare(&q,
    "SELECT idm, fnm FROM fv"
    " WHERE idp=0 AND idv=0 AND idm>0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idm = db_column_int(&q, 0);
    const char *zName;
    char *zFullName;
    db_multi_exec(
      "REPLACE INTO vfile(vid,chnged,deleted,rid,mrid,isexe,islink,pathname)"
      "  SELECT %d,%d,0,rid,mrid,isexe,islink,pathname FROM vfile WHERE id=%d",
      vid, integrateFlag?5:3, idm
    );
    zName = db_column_text(&q, 1);
    zFullName = mprintf("%s%s", g.zLocalRoot, zName);
    if( file_wd_isfile_or_link(zFullName)
        && !db_exists("SELECT 1 FROM fv WHERE fn=%Q", zName) ){
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
                   " no files were actually changed.");
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
  if( !dryRunFlag ) undo_finish();
  db_end_transaction(dryRunFlag);
}
