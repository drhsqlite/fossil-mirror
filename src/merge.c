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
** COMMAND: merge
**
** Usage: %fossil merge [--cherrypick] [--backout] VERSION
**
** The argument is a version that should be merged into the current
** checkout.  All changes from VERSION back to the nearest common
** ancestor are merged.  Except, if either of the --cherrypick or
** --backout options are used only the changes associated with the
** single check-in VERSION are merged.  The --backout option causes
** the changes associated with VERSION to be removed from the current
** checkout rather than added.
**
** Only file content is merged.  The result continues to use the
** file and directory names from the current checkout even if those
** names might have been changed in the branch being merged in.
**
** Other options:
**
**   --detail                Show additional details of the merge
**
**   --binary GLOBPATTERN    Treat files that match GLOBPATTERN as binary
**                           and do not try to merge parallel changes.  This
**                           option overrides the "binary-glob" setting.
*/
void merge_cmd(void){
  int vid;              /* Current version */
  int mid;              /* Version we are merging against */
  int pid;              /* The pivot version - most recent common ancestor */
  int detailFlag;       /* True if the --detail option is present */
  int pickFlag;         /* True if the --cherrypick option is present */
  int backoutFlag;      /* True if the --backout optioni is present */
  const char *zBinGlob; /* The value of --binary */
  Stmt q;

  detailFlag = find_option("detail",0,0)!=0;
  pickFlag = find_option("cherrypick",0,0)!=0;
  backoutFlag = find_option("backout",0,0)!=0;
  zBinGlob = find_option("binary",0,1);
  if( g.argc!=3 ){
    usage("VERSION");
  }
  db_must_be_within_tree();
  if( zBinGlob==0 ) zBinGlob = db_get("binary-glob",0);
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("nothing is checked out");
  }
  mid = name_to_rid(g.argv[2]);
  if( mid==0 ){
    fossil_fatal("not a version: %s", g.argv[2]);
  }
  if( !is_a_version(mid) ){
    fossil_fatal("not a version: %s", g.argv[2]);
  }
  if( pickFlag || backoutFlag ){
    pid = db_int(0, "SELECT pid FROM plink WHERE cid=%d AND isprim", mid);
    if( pid<=0 ){
      fossil_fatal("cannot find an ancestor for %s", g.argv[2]);
    }
    if( backoutFlag ){
      int t = pid;
      pid = mid;
      mid = t;
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
  if( !is_a_version(pid) ){
    fossil_fatal("not a version: record #%d", pid);
  }
  vfile_check_signature(vid, 1);
  db_begin_transaction();
  undo_begin();
  load_vfile_from_rid(mid);
  load_vfile_from_rid(pid);

  /*
  ** The vfile.pathname field is used to match files against each other.  The
  ** FV table contains one row for each each unique filename in
  ** in the current checkout, the pivot, and the version being merged.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS fv;"
    "CREATE TEMP TABLE fv("
    "  fn TEXT PRIMARY KEY,"      /* The filename */
    "  idv INTEGER,"              /* VFILE entry for current version */
    "  idp INTEGER,"              /* VFILE entry for the pivot */
    "  idm INTEGER,"              /* VFILE entry for version merging in */
    "  chnged BOOLEAN,"           /* True if current version has been edited */
    "  ridv INTEGER,"             /* Record ID for current version */
    "  ridp INTEGER,"             /* Record ID for pivot */
    "  ridm INTEGER"              /* Record ID for merge */
    ");"
    "INSERT OR IGNORE INTO fv"
    " SELECT pathname, 0, 0, 0, 0, 0, 0, 0 FROM vfile"
  );
  db_prepare(&q,
    "SELECT id, pathname, rid FROM vfile"
    " WHERE vid=%d", pid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int id = db_column_int(&q, 0);
    const char *fn = db_column_text(&q, 1);
    int rid = db_column_int(&q, 2);
    db_multi_exec(
      "UPDATE fv SET idp=%d, ridp=%d WHERE fn=%Q",
      id, rid, fn
    );
  }
  db_finalize(&q);
  db_prepare(&q,
    "SELECT id, pathname, rid FROM vfile"
    " WHERE vid=%d", mid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int id = db_column_int(&q, 0);
    const char *fn = db_column_text(&q, 1);
    int rid = db_column_int(&q, 2);
    db_multi_exec(
      "UPDATE fv SET idm=%d, ridm=%d WHERE fn=%Q",
      id, rid, fn
    );
  }
  db_finalize(&q);
  db_prepare(&q,
    "SELECT id, pathname, rid, chnged FROM vfile"
    " WHERE vid=%d", vid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int id = db_column_int(&q, 0);
    const char *fn = db_column_text(&q, 1);
    int rid = db_column_int(&q, 2);
    int chnged = db_column_int(&q, 3);
    db_multi_exec(
      "UPDATE fv SET idv=%d, ridv=%d, chnged=%d WHERE fn=%Q",
      id, rid, chnged, fn
    );
  }
  db_finalize(&q);

  /*
  ** Find files in mid and vid but not in pid and report conflicts.
  ** The file in mid will be ignored.  It will be treated as if it
  ** does not exist.
  */
  db_prepare(&q,
    "SELECT idm FROM fv WHERE idp=0 AND idv>0 AND idm>0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idm = db_column_int(&q, 0);
    char *zName = db_text(0, "SELECT pathname FROM vfile WHERE id=%d", idm);
    printf("WARNING: conflict on %s\n", zName);
    free(zName);
    db_multi_exec("UPDATE fv SET idm=0 WHERE idm=%d", idm);
  }
  db_finalize(&q);

  /*
  ** Add to vid files that are not in pid but are in mid
  */
  db_prepare(&q, 
    "SELECT idm, rowid, fn FROM fv WHERE idp=0 AND idv=0 AND idm>0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idm = db_column_int(&q, 0);
    int rowid = db_column_int(&q, 1);
    int idv;
    const char *zName;
    db_multi_exec(
      "INSERT INTO vfile(vid,chnged,deleted,rid,mrid,pathname)"
      "  SELECT %d,3,0,rid,mrid,pathname FROM vfile WHERE id=%d",
      vid, idm
    );
    idv = db_last_insert_rowid();
    db_multi_exec("UPDATE fv SET idv=%d WHERE rowid=%d", idv, rowid);
    zName = db_column_text(&q, 2);
    printf("ADDED %s\n", zName);
    undo_save(zName);
    vfile_to_disk(0, idm, 0, 0);
  }
  db_finalize(&q);
  
  /*
  ** Find files that have changed from pid->mid but not pid->vid. 
  ** Copy the mid content over into vid.
  */
  db_prepare(&q,
    "SELECT idv, ridm FROM fv"
    " WHERE idp>0 AND idv>0 AND idm>0"
    "   AND ridm!=ridp AND ridv=ridp AND NOT chnged"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    int ridm = db_column_int(&q, 1);
    char *zName = db_text(0, "SELECT pathname FROM vfile WHERE id=%d", idv);
    /* Copy content from idm over into idv.  Overwrite idv. */
    printf("UPDATE %s\n", zName);
    undo_save(zName);
    db_multi_exec(
      "UPDATE vfile SET mrid=%d, chnged=2 WHERE id=%d", ridm, idv
    );
    vfile_to_disk(0, idv, 0, 0);
    free(zName);
  }
  db_finalize(&q);

  /*
  ** Do a three-way merge on files that have changes pid->mid and pid->vid
  */
  db_prepare(&q,
    "SELECT ridm, idv, ridp, ridv, %s FROM fv"
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
    int rc;
    char *zName = db_text(0, "SELECT pathname FROM vfile WHERE id=%d", idv);
    char *zFullPath;
    Blob m, p, v, r;
    /* Do a 3-way merge of idp->idm into idp->idv.  The results go into idv. */
    if( detailFlag ){
      printf("MERGE %s  (pivot=%d v1=%d v2=%d)\n", zName, ridp, ridm, ridv);
    }else{
      printf("MERGE %s\n", zName);
    }
    undo_save(zName);
    zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
    content_get(ridp, &p);
    content_get(ridm, &m);
    blob_zero(&v);
    blob_read_from_file(&v, zFullPath);
    if( isBinary ){
      rc = -1;
      blob_zero(&r);
    }else{
      rc = blob_merge(&p, &m, &v, &r);
    }
    if( rc>=0 ){
      blob_write_to_file(&r, zFullPath);
      if( rc>0 ){
        printf("***** %d merge conflicts in %s\n", rc, zName);
      }
    }else{
      printf("***** Cannot merge binary file %s\n", zName);
    }
    free(zName);
    blob_reset(&p);
    blob_reset(&m);
    blob_reset(&v);
    blob_reset(&r);
    db_multi_exec("INSERT OR IGNORE INTO vmerge(id,merge) VALUES(%d,%d)",
                  idv,ridm);
  }
  db_finalize(&q);

  /*
  ** Drop files from vid that are in pid but not in mid
  */
  db_prepare(&q,
    "SELECT idv FROM fv"
    " WHERE idp>0 AND idv>0 AND idm=0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    char *zName = db_text(0, "SELECT pathname FROM vfile WHERE id=%d", idv);
    /* Delete the file idv */
    printf("DELETE %s\n", zName);
    undo_save(zName);
    db_multi_exec(
      "UPDATE vfile SET deleted=1 WHERE id=%d", idv
    );
    free(zName);
  }
  db_finalize(&q);
  
  /*
  ** Clean up the mid and pid VFILE entries.  Then commit the changes.
  */
  db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
  if( !pickFlag ){
    db_multi_exec("INSERT OR IGNORE INTO vmerge(id,merge) VALUES(0,%d)", mid);
  }
  undo_finish();
  db_end_transaction(0);
}
