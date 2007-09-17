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
** This file contains code used to merge two or more branches into
** a single tree.
*/
#include "config.h"
#include "merge.h"
#include <assert.h>


/*
** COMMAND: merge
**
** Usage: %fossil merge VERSION
**
** The argument is a version that should be merged into the current
** checkout. 
**
** Only file content is merged.  The result continues to use the
** file and directory names from the current check-out even if those
** names might have been changed in the branch being merged in.
*/
void merge_cmd(void){
  int vid;              /* Current version */
  int mid;              /* Version we are merging against */
  int pid;              /* The pivot version - most recent common ancestor */
  Stmt q;

  if( g.argc!=3 ){
    usage("VERSION");
  }
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_panic("nothing is checked out");
  }
  mid = name_to_rid(g.argv[2]);
  if( mid==0 ){
    fossil_panic("not a version: %s", g.argv[2]);
  }
  if( mid>1 && !db_exists("SELECT 1 FROM plink WHERE cid=%d", mid) ){
    fossil_panic("not a version: %s", g.argv[2]);
  }
  pivot_set_primary(mid);
  pivot_set_secondary(vid);
  db_prepare(&q, "SELECT merge FROM vmerge WHERE id=0");
  while( db_step(&q)==SQLITE_ROW ){
    pivot_set_secondary(db_column_int(&q,0));
  }
  db_finalize(&q);
  pid = pivot_find();
  if( pid<=0 ){
    fossil_panic("cannot find a common ancestor between the current"
                 "checkout and %s", g.argv[2]);
  }
  if( pid>1 && !db_exists("SELECT 1 FROM plink WHERE cid=%d", pid) ){
    fossil_panic("not a version: record #%d", mid);
  }
  vfile_check_signature(vid);
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
    vfile_to_disk(0, idm, 0);
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
    vfile_to_disk(0, idv, 0);
    free(zName);
  }
  db_finalize(&q);

  /*
  ** Do a three-way merge on files that have changes pid->mid and pid->vid
  */
  db_prepare(&q,
    "SELECT ridm, idv, ridp FROM fv"
    " WHERE idp>0 AND idv>0 AND idm>0"
    "   AND ridm!=ridp AND (ridv!=ridp OR chnged)"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int ridm = db_column_int(&q, 0);
    int idv = db_column_int(&q, 1);
    int ridp = db_column_int(&q, 2);
    char *zName = db_text(0, "SELECT pathname FROM vfile WHERE id=%d", idv);
    char *zFullPath;
    Blob m, p, v, r;
    /* Do a 3-way merge of idp->idm into idp->idv.  The results go into idv. */
    printf("MERGE %s\n", zName);
    undo_save(zName);
    zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
    free(zName);
    content_get(ridp, &p);
    content_get(ridm, &m);
    blob_zero(&v);
    blob_read_from_file(&v, zFullPath);
    blob_merge(&p, &m, &v, &r);
    blob_write_to_file(&r, zFullPath);
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
  db_multi_exec("INSERT OR IGNORE INTO vmerge(id,merge) VALUES(0,%d)", mid);
  db_end_transaction(0);
}
