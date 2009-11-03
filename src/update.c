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
  return db_exists("SELECT 1 FROM plink WHERE cid=%d", rid);
}

/*
** COMMAND: update
**
** Usage: %fossil update ?VERSION? ?--latest?
**
** The optional argument is a version that should become the current
** version.  If the argument is omitted, then use the leaf of the
** tree that begins with the current version, if there is only a 
** single leaf.  If there are a multiple leaves, the latest is used
** if the --latest flag is present.
**
** This command is different from the "checkout" in that edits are
** not overwritten.  Edits are merged into the new version.
*/
void update_cmd(void){
  int vid;              /* Current version */
  int tid=0;            /* Target version - version we are changing to */
  Stmt q;
  int latestFlag;       /* Pick the latest version if true */
  int forceFlag;        /* True force the update */

  url_proxy_options();
  latestFlag = find_option("latest",0, 0)!=0;
  forceFlag = find_option("force","f",0)!=0;
  if( g.argc!=3 && g.argc!=2 ){
    usage("?VERSION?");
  }
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("cannot find current version");
  }
  if( db_exists("SELECT 1 FROM vmerge") ){
    fossil_fatal("cannot update an uncommitted merge");
  }

  if( g.argc==3 ){
    tid = name_to_rid(g.argv[2]);
    if( tid==0 ){
      fossil_fatal("not a version: %s", g.argv[2]);
    }
    if( !is_a_version(tid) ){
      fossil_fatal("not a version: %s", g.argv[2]);
    }
  }
  autosync(AUTOSYNC_PULL);
  
  if( tid==0 ){
    compute_leaves(vid, 1);
    if( !latestFlag && db_int(0, "SELECT count(*) FROM leaves")>1 ){
      db_prepare(&q, 
        "%s "
        "   AND event.objid IN leaves"
        " ORDER BY event.mtime DESC",
        timeline_query_for_tty()
      );
      print_timeline(&q, 100);
      db_finalize(&q);
      fossil_fatal("Multiple descendants");
    }
    tid = db_int(0, "SELECT rid FROM leaves, event"
                    " WHERE event.objid=leaves.rid"
                    " ORDER BY event.mtime DESC"); 
  }

  db_begin_transaction();
  vfile_check_signature(vid);
  undo_begin();
  load_vfile_from_rid(tid);

  /*
  ** The record.fn field is used to match files against each other.  The
  ** FV table contains one row for each each unique filename in
  ** in the current checkout, the pivot, and the version being merged.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS fv;"
    "CREATE TEMP TABLE fv("
    "  fn TEXT PRIMARY KEY,"      /* The filename */
    "  idv INTEGER,"              /* VFILE entry for current version */
    "  idt INTEGER,"              /* VFILE entry for target version */
    "  chnged BOOLEAN,"           /* True if current version has been edited */
    "  ridv INTEGER,"             /* Record ID for current version */
    "  ridt INTEGER "             /* Record ID for target */
    ");"
    "INSERT OR IGNORE INTO fv"
    " SELECT pathname, 0, 0, 0, 0, 0 FROM vfile"
  );
  db_prepare(&q,
    "SELECT id, pathname, rid FROM vfile"
    " WHERE vid=%d", tid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int id = db_column_int(&q, 0);
    const char *fn = db_column_text(&q, 1);
    int rid = db_column_int(&q, 2);
    db_multi_exec(
      "UPDATE fv SET idt=%d, ridt=%d WHERE fn=%Q",
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

  db_prepare(&q, 
    "SELECT fn, idv, ridv, idt, ridt, chnged FROM fv ORDER BY 1"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    int idv = db_column_int(&q, 1);
    int ridv = db_column_int(&q, 2);
    int idt = db_column_int(&q, 3);
    int ridt = db_column_int(&q, 4);
    int chnged = db_column_int(&q, 5);

    if( idv>0 && ridv==0 && idt>0 ){
      /* Conflict.  This file has been added to the current checkout
      ** but also exists in the target checkout.  Use the current version.
      */
      printf("CONFLICT %s\n", zName);
    }else if( idt>0 && idv==0 ){
      /* File added in the target. */
      printf("ADD %s\n", zName);
      undo_save(zName);
      vfile_to_disk(0, idt, 0);
    }else if( idt>0 && idv>0 && ridt!=ridv && chnged==0 ){
      /* The file is unedited.  Change it to the target version */
      printf("UPDATE %s\n", zName);
      undo_save(zName);
      vfile_to_disk(0, idt, 0);
    }else if( idt==0 && idv>0 ){
      if( ridv==0 ){
        /* Added in current checkout.  Continue to hold the file as
        ** as an addition */
        db_multi_exec("UPDATE vfile SET vid=%d WHERE id=%d", tid, idv);
      }else if( chnged ){
        printf("CONFLICT %s\n", zName);
      }else{
        char *zFullPath;
        printf("REMOVE %s\n", zName);
        undo_save(zName);
        zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
        unlink(zFullPath);
        free(zFullPath);
      }
    }else if( idt>0 && idv>0 && ridt!=ridv && chnged ){
      /* Merge the changes in the current tree into the target version */
      Blob e, r, t, v;
      int rc;
      char *zFullPath;
      printf("MERGE %s\n", zName);
      undo_save(zName);
      zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
      content_get(ridt, &t);
      content_get(ridv, &v);
      blob_zero(&e);
      blob_read_from_file(&e, zFullPath);
      rc = blob_merge(&v, &e, &t, &r);
      if( rc>=0 ){
        blob_write_to_file(&r, zFullPath);
        if( rc>0 ){
          printf("***** %d merge conflicts in %s\n", rc, zName);
        }
      }else{
        printf("***** Cannot merge binary file %s\n", zName);
      }
      free(zFullPath);
      blob_reset(&v);
      blob_reset(&e);
      blob_reset(&t);
      blob_reset(&r);
      
    }
  }
  db_finalize(&q);
  
  /*
  ** Clean up the mid and pid VFILE entries.  Then commit the changes.
  */
  db_multi_exec("DELETE FROM vfile WHERE vid!=%d", tid);
  manifest_to_disk(tid);
  db_lset_int("checkout", tid);
  db_end_transaction(0);
}


/*
** Get the contents of a file within a given revision.
*/
int historical_version_of_file(
  const char *revision,    /* The baseline name containing the file */
  const char *file,        /* Full treename of the file */
  Blob *content            /* Put the content here */
){
  Blob mfile;
  Manifest m;
  int i, rid=0;
  
  rid = name_to_rid(revision);
  content_get(rid, &mfile);
  
  if( manifest_parse(&m, &mfile) ){
    for(i=0; i<m.nFile; i++){
      if( strcmp(m.aFile[i].zName, file)==0 ){
        rid = uuid_to_rid(m.aFile[i].zUuid, 0);
        return content_get(rid, content);
      }
    }
    fossil_fatal("file %s does not exist in baseline: %s", file, revision);
  }else{
    fossil_panic("could not parse manifest for baseline: %s", revision);
  }
  return 0;
}


/*
** COMMAND: revert
**
** Usage: %fossil revert ?--yes? ?-r REVISION? FILE
**
** Revert to the current repository version of FILE, or to
** the version associated with baseline REVISION if the -r flag
** appears.  This command will confirm your operation unless the
** file is missing or the --yes option is used.
**/
void revert_cmd(void){
  const char *zFile;
  const char *zRevision;
  Blob fname;
  Blob record;
  Blob ans;
  int rid = 0, yesRevert;
  
  yesRevert = find_option("yes", "y", 0)!=0;
  zRevision = find_option("revision", "r", 1);
  verify_all_options();
  
  if( g.argc!=3 ){
    usage("?OPTIONS FILE");
  }
  db_must_be_within_tree();
  
  zFile = mprintf("%/", g.argv[g.argc-1]);

  file_tree_name(zFile, &fname, 1);

  if( access(zFile, 0) ) yesRevert = 1;  
  if( yesRevert==0 ){
    char *prompt = mprintf("revert file %B? this will"
                           " destroy local changes [y/N]? ",
                           &fname);
    blob_zero(&ans);
    prompt_user(prompt, &ans);
    free( prompt );
    if( blob_str(&ans)[0]=='y' ){
      yesRevert = 1;
    }
  }

  if( yesRevert==1 && zRevision!=0 ){
    historical_version_of_file(zRevision, zFile, &record);
  }else if( yesRevert==1 ){
    rid = db_int(0, "SELECT rid FROM vfile WHERE pathname=%B", &fname);
    if( rid==0 ){
      fossil_panic("no history for file: %b", &fname);
    }
    content_get(rid, &record);
  }
  
  if( yesRevert==1 ){
    blob_write_to_file(&record, zFile);
    printf("%s reverted\n", zFile);
    blob_reset(&record);
    blob_reset(&fname);
  }else{
    printf("revert canceled\n");
  }
}
