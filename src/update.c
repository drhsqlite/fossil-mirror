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

/*
** COMMAND: update
**
** Usage: %fossil update ?VERSION? ?FILES...?
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
** The -n or --nochange option causes this command to do a "dry run".  It
** prints out what would have happened but does not actually make any
** changes to the current checkout or the repository.
**
** The -v or --verbose option prints status information about unchanged
** files in addition to those file that actually do change.
*/
void update_cmd(void){
  int vid;              /* Current version */
  int tid=0;            /* Target version - version we are changing to */
  Stmt q;
  int latestFlag;       /* --latest.  Pick the latest version if true */
  int nochangeFlag;     /* -n or --nochange.  Do a dry run */
  int verboseFlag;      /* -v or --verbose.  Output extra information */

  url_proxy_options();
  latestFlag = find_option("latest",0, 0)!=0;
  nochangeFlag = find_option("nochange","n",0)!=0;
  verboseFlag = find_option("verbose","v",0)!=0;
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("cannot find current version");
  }
  if( db_exists("SELECT 1 FROM vmerge") ){
    fossil_fatal("cannot update an uncommitted merge");
  }
  if( !nochangeFlag ) autosync(AUTOSYNC_PULL);

  if( g.argc>=3 ){
    if( strcmp(g.argv[2], "current")==0 ){
      /* If VERSION is "current", then use the same algorithm to find the
      ** target as if VERSION were omitted. */
    }else if( strcmp(g.argv[2], "latest")==0 ){
      /* If VERSION is "latest", then use the same algorithm to find the
      ** target as if VERSION were omitted and the --latest flag is present.
      */
      latestFlag = 1;
    }else{
      tid = name_to_rid(g.argv[2]);
      if( tid==0 ){
        fossil_fatal("no such version: %s", g.argv[2]);
      }else if( !is_a_version(tid) ){
        fossil_fatal("no such version: %s", g.argv[2]);
      }
    }
  }
  
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

  if( !verboseFlag && (tid==vid)) return;  /* Nothing to update */
  db_begin_transaction();
  vfile_check_signature(vid, 1);
  if( !nochangeFlag ) undo_begin();
  load_vfile_from_rid(tid);

  /*
  ** The record.fn field is used to match files against each other.  The
  ** FV table contains one row for each each unique filename in
  ** in the current checkout, the pivot, and the version being merged.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS fv;"
    "CREATE TEMP TABLE fv("
    "  fn TEXT PRIMARY KEY,"      /* The filename relative to root */
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
      fprintf(stderr,"%s , %s\n",g.argv[i], blob_str(&treename));
      if( file_isdir(g.argv[i])==1 ){
        if( blob_size(&treename)>0 ){
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
    /* fprintf(stderr, "%s\n", blob_str(&sql)); */
    db_multi_exec(blob_str(&sql));
    blob_reset(&sql);
  }

  db_prepare(&q, 
    "SELECT fn, idv, ridv, idt, ridt, chnged FROM fv ORDER BY 1"
  );
  assert( g.zLocalRoot!=0 );
  assert( strlen(g.zLocalRoot)>1 );
  assert( g.zLocalRoot[strlen(g.zLocalRoot)-1]=='/' );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);  /* The filename from root */
    int idv = db_column_int(&q, 1);             /* VFILE entry for current */
    int ridv = db_column_int(&q, 2);            /* RecordID for current */
    int idt = db_column_int(&q, 3);             /* VFILE entry for target */
    int ridt = db_column_int(&q, 4);            /* RecordID for target */
    int chnged = db_column_int(&q, 5);          /* Current is edited */
    char *zFullPath;                            /* Full pathname of the file */

    zFullPath = mprintf("%s%s", g.zLocalRoot, zName);
    if( idv>0 && ridv==0 && idt>0 && ridt>0 ){
      /* Conflict.  This file has been added to the current checkout
      ** but also exists in the target checkout.  Use the current version.
      */
      printf("CONFLICT %s\n", zName);
    }else if( idt>0 && idv==0 ){
      /* File added in the target. */
      printf("ADD %s\n", zName);
      undo_save(zName);
      if( !nochangeFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt>0 && idv>0 && ridt!=ridv && chnged==0 ){
      /* The file is unedited.  Change it to the target version */
      printf("UPDATE %s\n", zName);
      undo_save(zName);
      if( !nochangeFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt>0 && idv>0 && file_size(zFullPath)<0 ){
      /* The file missing from the local check-out. Restore it to the
      ** version that appears in the target. */
      printf("UPDATE %s\n", zName);
      undo_save(zName);
      if( !nochangeFlag ) vfile_to_disk(0, idt, 0, 0);
    }else if( idt==0 && idv>0 ){
      if( ridv==0 ){
        /* Added in current checkout.  Continue to hold the file as
        ** as an addition */
        db_multi_exec("UPDATE vfile SET vid=%d WHERE id=%d", tid, idv);
      }else if( chnged ){
        /* Edited locally but deleted from the target.  Delete it. */
        printf("CONFLICT %s\n", zName);
      }else{
        char *zFullPath;
        printf("REMOVE %s\n", zName);
        undo_save(zName);
        zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
        if( !nochangeFlag ) unlink(zFullPath);
        free(zFullPath);
      }
    }else if( idt>0 && idv>0 && ridt!=ridv && chnged ){
      /* Merge the changes in the current tree into the target version */
      Blob e, r, t, v;
      int rc;
      printf("MERGE %s\n", zName);
      undo_save(zName);
      content_get(ridt, &t);
      content_get(ridv, &v);
      blob_zero(&e);
      blob_read_from_file(&e, zFullPath);
      rc = blob_merge(&v, &e, &t, &r);
      if( rc>=0 ){
        if( !nochangeFlag ) blob_write_to_file(&r, zFullPath);
        if( rc>0 ){
          printf("***** %d merge conflicts in %s\n", rc, zName);
        }
      }else{
        printf("***** Cannot merge binary file %s\n", zName);
      }
      blob_reset(&v);
      blob_reset(&e);
      blob_reset(&t);
      blob_reset(&r);
    }else if( verboseFlag ){
      if( chnged ){
        printf("EDITED %s\n", zName);
      }else{
        printf("UNCHANGED %s\n", zName);
      }
    }
    free(zFullPath);
  }
  db_finalize(&q);
  
  /*
  ** Clean up the mid and pid VFILE entries.  Then commit the changes.
  */
  if( nochangeFlag ){
    db_end_transaction(1);  /* With --nochange, rollback changes */
  }else{
    if( g.argc<=3 ){
      /* All files updated.  Shift the current checkout to the target. */
      db_multi_exec("DELETE FROM vfile WHERE vid!=%d", tid);
      checkout_set_all_exe(vid);
      manifest_to_disk(tid);
      db_lset_int("checkout", tid);
    }else{
      /* A subset of files have been checked out.  Keep the current
      ** checkout unchanged. */
      db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
    }
    undo_finish();
    db_end_transaction(0);
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
  int errCode              /* Error code if file not found.  Panic if 0. */
){
  Manifest *pManifest;
  ManifestFile *pFile;
  int rid=0;
  
  if( revision ){
    rid = name_to_rid(revision);
  }else{
    rid = db_lget_int("checkout", 0);
  }
  if( !is_a_version(rid) ){
    if( errCode>0 ) return errCode;
    fossil_fatal("no such checkin: %s", revision);
  }
  pManifest = manifest_get(rid, CFTYPE_MANIFEST);
  
  if( pManifest ){
    manifest_file_rewind(pManifest);
    while( (pFile = manifest_file_next(pManifest,0))!=0 ){
      if( strcmp(pFile->zName, file)==0 ){
        rid = uuid_to_rid(pFile->zUuid, 0);
        manifest_destroy(pManifest);
        return content_get(rid, content);
      }
    }
    manifest_destroy(pManifest);
    if( errCode<=0 ){
      fossil_fatal("file %s does not exist in checkin: %s", file, revision);
    }
  }else if( errCode<=0 ){
    fossil_panic("could not parse manifest for checkin: %s", revision);
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
** Revert all files if no file name is provided.
**
** If a file is reverted accidently, it can be restored using
** the "fossil undo" command.
*/
void revert_cmd(void){
  const char *zFile;
  const char *zRevision;
  Blob record;
  int i;
  int errCode;
  int rid = 0;
  Stmt q;
  
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
      db_multi_exec("REPLACE INTO torevert VALUES(%B)", &fname);
      blob_reset(&fname);
    }
  }else{
    int vid;
    vid = db_lget_int("checkout", 0);
    vfile_check_signature(vid, 0);
    db_multi_exec(
      "DELETE FROM vmerge;"
      "INSERT INTO torevert "
      "SELECT pathname"
      "  FROM vfile "
      " WHERE chnged OR deleted OR rid=0 OR pathname!=origname;"
    );
  }
  blob_zero(&record);
  db_prepare(&q, "SELECT name FROM torevert");
  while( db_step(&q)==SQLITE_ROW ){
    zFile = db_column_text(&q, 0);
    if( zRevision!=0 ){
      errCode = historical_version_of_file(zRevision, zFile, &record, 2);
    }else{
      rid = db_int(0, "SELECT rid FROM vfile WHERE pathname=%Q", zFile);
      if( rid==0 ){
        errCode = 2;
      }else{
        content_get(rid, &record);
        errCode = 0;
      }
    }

    if( errCode==2 ){
      fossil_warning("file not in repository: %s", zFile);
    }else{
      char *zFull = mprintf("%//%/", g.zLocalRoot, zFile);
      undo_save(zFile);
      blob_write_to_file(&record, zFull);
      printf("REVERTED: %s\n", zFile);
      free(zFull);
    }
    blob_reset(&record);
  }
  db_finalize(&q);
  undo_finish();
  db_end_transaction(0);
  printf("\"fossil undo\" is available to undo the changes shown above.\n");
}
