/*
** Copyright (c) 2021 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** This file contains code used to implement the "diff" command
*/
#include "config.h"
#include "patch.h"
#include <assert.h>

/*
** Implementation of the "readfile(X)" SQL function.  The entire content
** of the checkout file named X is read and returned as a BLOB.
*/
static void readfileFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zName;
  Blob x;
  sqlite3_int64 sz;
  (void)(argc);  /* Unused parameter */
  zName = (const char*)sqlite3_value_text(argv[0]);
  if( zName==0 || (zName[0]=='-' && zName[1]==0) ) return;
  sz = blob_read_from_file(&x, zName, RepoFILE);
  sqlite3_result_blob64(context, x.aData, sz, SQLITE_TRANSIENT);
  blob_reset(&x);
}

/*
** mkdelta(X,Y)
**
** X is an numeric artifact id.  Y is a filename.
**
** Compute a compressed delta that carries X into Y.  Or return NULL
** if X is equal to Y.
*/
static void mkdeltaFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zFile;
  Blob x, y;
  int rid;
  char *aOut;
  int nOut;
  sqlite3_int64 sz;

  rid = sqlite3_value_int(argv[0]);
  if( !content_get(rid, &x) ){
    sqlite3_result_error(context, "mkdelta(X,Y): no content for X", -1);
    return;
  }
  zFile = (const char*)sqlite3_value_text(argv[1]);
  if( zFile==0 ){
    sqlite3_result_error(context, "mkdelta(X,Y): NULL Y argument", -1);
    blob_reset(&x);
    return;
  }
  sz = blob_read_from_file(&y, zFile, RepoFILE);
  if( sz<0 ){
    sqlite3_result_error(context, "mkdelta(X,Y): cannot read file Y", -1);
    blob_reset(&x);
    return;
  }
  aOut = sqlite3_malloc64(sz+70);
  if( aOut==0 ){
    sqlite3_result_error_nomem(context);
    blob_reset(&y);
    blob_reset(&x);
    return;
  }
  if( blob_size(&x)==blob_size(&y)
   && memcmp(blob_buffer(&x), blob_buffer(&y), blob_size(&x))==0
  ){
    blob_reset(&y);
    blob_reset(&x);
    return;
  }
  nOut = delta_create(blob_buffer(&x),blob_size(&x),
                      blob_buffer(&y),blob_size(&y), aOut);
  blob_reset(&x);
  blob_reset(&y);
  blob_init(&x, aOut, nOut);
  blob_compress(&x, &x);
  sqlite3_result_blob64(context, blob_buffer(&x), blob_size(&x),
                        SQLITE_TRANSIENT);
  blob_reset(&x);
}


/*
** Generate a binary patch file and store it into the file
** named zOut.
*/
void patch_create(const char *zOut){
  int vid;

  if( file_isdir(zOut, ExtFILE)!=0 ){
    fossil_fatal("patch file already exists: %s", zOut);
  }
  add_content_sql_commands(g.db);
  deltafunc_init(g.db);
  sqlite3_create_function(g.db, "read_co_file", 1, SQLITE_UTF8, 0,
                          readfileFunc, 0, 0);
  sqlite3_create_function(g.db, "mkdelta", 2, SQLITE_UTF8, 0,
                          mkdeltaFunc, 0, 0);
  db_multi_exec("ATTACH %Q AS patch;", zOut);
  db_multi_exec(
    "PRAGMA patch.journal_mode=OFF;\n"
    "PRAGMA patch.page_size=512;\n"
    "CREATE TABLE patch.chng(\n"
    "  pathname TEXT,\n" /* Filename */
    "  origname TEXT,\n" /* Name before rename.  NULL if not renamed */
    "  hash TEXT,\n"     /* Baseline hash.  NULL for new files. */
    "  isexe BOOL,\n"    /* True if executable */
    "  islink BOOL,\n"   /* True if is a symbolic link */
    "  delta BLOB\n"     /* Delta.  NULL if file deleted or unchanged */
    ");"
    "CREATE TABLE patch.cfg(\n"
    "  key TEXT,\n"
    "  value ANY\n"
    ");"
  );
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid, CKSIG_ENOTFILE);
  db_multi_exec(
    "INSERT INTO patch.cfg(key,value)"
    "SELECT 'baseline',uuid FROM blob WHERE rid=%d", vid);
  if( db_exists("SELECT 1 FROM vmerge") ){
    db_multi_exec("INSERT INTO patch.cfg(key,value)VALUES('merged',1);");
  }
  
  /* New files */
  db_multi_exec(
    "INSERT INTO patch.chng(pathname,hash,isexe,islink,delta)"
    "  SELECT pathname, NULL, isexe, islink,"
    "         compress(read_co_file(%Q||pathname))"
    "    FROM vfile WHERE rid==0;",
    g.zLocalRoot
  );
  /* Deleted files */
  db_multi_exec(
    "INSERT INTO patch.chng(pathname,hash,isexe,islink,delta)"
    "  SELECT pathname, NULL, 0, 0, NULL"
    "    FROM vfile WHERE deleted;"
  );
  /* Changed files */
  db_multi_exec(
    "INSERT INTO patch.chng(pathname,origname,hash,isexe,islink,delta)"
    "  SELECT pathname, nullif(origname,pathname), blob.uuid, isexe, islink,"
            " mkdelta(blob.rid, %Q||pathname)"
    "    FROM vfile, blob"
    "   WHERE blob.rid=vfile.rid"
    "     AND NOT deleted AND (chnged OR origname<>pathname);",
    g.zLocalRoot
  );

  if( db_exists("SELECT 1 FROM localdb.vmerge WHERE id<=0") ){
    db_multi_exec(
      "CREATE TABLE patch.patchmerge(type TEXT,mhash TEXT);\n"
      "WITH tmap(id,type) AS (VALUES(0,'merge'),(-1,'cherrypick'),"
                                   "(-2,'backout'),(-4,'integrate'))"
      "INSERT INTO patch.patchmerge(type,mhash)"
      " SELECT tmap.type,vmerge.mhash FROM vmerge, tmap"
      "  WHERE tmap.id=vmerge.id;"
    );
  }
}

/*
** Attempt to load and validate a patchfile identified by the first
** argument.
*/
void patch_attach(const char *zIn){
  Stmt q;
  if( !file_isfile(zIn, ExtFILE) ){
    fossil_fatal("no such file: %s", zIn);
  }
  if( g.db==0 ){
    sqlite3_open(":memory:", &g.db);
  }
  db_multi_exec("ATTACH %Q AS patch", zIn);
  db_prepare(&q, "PRAGMA patch.quick_check");
  while( db_step(&q)==SQLITE_ROW ){
    if( fossil_strcmp(db_column_text(&q,0),"ok")!=0 ){
      fossil_fatal("file %s is not a well-formed Fossil patchfile", zIn);
    }
  }
  db_finalize(&q);
}

/*
** Show a summary of the content of a patch on standard output
*/
void patch_view(void){
  Stmt q;
  db_prepare(&q, "SELECT value FROM patch.cfg WHERE key='baseline'");
  if( db_step(&q)==SQLITE_ROW ){
    fossil_print("%-10s %s\n", "BASELINE", db_column_text(&q,0));
  }else{
    fossil_fatal("ERROR: Missing patch baseline");
  }
  db_finalize(&q);
  if( db_table_exists("patch","patchmerge") ){
    db_prepare(&q, "SELECT upper(type),mhash FROM patchmerge");
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%-10s %s\n",
        db_column_text(&q,0),
        db_column_text(&q,1));
    }
    db_finalize(&q);
  }
  db_prepare(&q,
    "SELECT pathname,"
          " hash IS NULL AND delta IS NOT NULL,"
          " delta IS NULL,"
          " origname"
    "  FROM patch.chng ORDER BY 1");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zClass = "EDIT";
    const char *zName = db_column_text(&q,0);
    const char *zOrigName = db_column_text(&q, 3);
    if( db_column_int(&q, 1) && zOrigName==0 ){
      zClass = "NEW";
    }else if( db_column_int(&q, 2) ){
      zClass = zOrigName==0 ? "DELETE" : 0;
    }
    if( zOrigName!=0 && zOrigName[0]!=0 ){
      fossil_print("%-10s %s -> %s\n", "RENAME",zOrigName,zName);
    }
    if( zClass ){
      fossil_print("%-10s %s\n", zClass, zName);
    }
  }
  db_finalize(&q);
}

/*
** Apply the patch currently attached as database "patch".
**
** First update the check-out to be at "baseline".  Then loop through
** and update all files.
*/
void patch_apply(void){
  Stmt q;
  Blob cmd;
  blob_init(&cmd, 0, 0);
  db_prepare(&q,
    "SELECT patch.cfg.value"
    "  FROM patch.cfg, localdb.vvar"
    " WHERE patch.cfg.key='baseline'"
    "   AND localdb.vvar.name='checkout-hash'"
    "   AND patch.cfg.key<>localdb.vvar.name"
  );
  if( db_step(&q)==SQLITE_ROW ){
    blob_append_escaped_arg(&cmd, g.nameOfExe);
    blob_appendf(&cmd, " update %s", db_column_text(&q, 0));
  }
  db_finalize(&q);
  if( blob_size(&cmd)>0 ){
    int rc = fossil_system(blob_str(&cmd));
    if( rc ){
      fossil_fatal("unable to update to the baseline check-out: %s",
                   blob_str(&cmd));
    }
  }
  blob_reset(&cmd);
  
}


/*
** COMMAND: patch
**
** Usage: %fossil patch SUBCOMMAND ?ARGS ..?
**
** This command is used to creates, view, and apply Fossil binary patches.
** A Fossil binary patch is a single (binary) file that captures all of the
** uncommitted changes of a check-out.  Use Fossil binary patches to transfer
** proposed or incomplete changes between machines for testing or analysis.
**
** > fossil patch create FILENAME
**
**       Create a new binary patch in FILENAME that captures all uncommitted
**       changes in the current check-out.
**
** > fossil patch apply FILENAME
**
**       Apply the changes in FILENAME to the current check-out. Options:
**
**           -f|--force     Apply the patch even though there are unsaved
**                          changes in the current check-out.
**
** > fossil patch diff [DIFF-FLAGS] FILENAME
**
**       View the changes specified by the binary patch FILENAME in a
**       human-readable format.  The usual diff flags apply.
**
** > fossil patch push REMOTE-CHECKOUT
**
**       Create a patch for the current check-out, transfer that patch to
**       a remote machine (using ssh) and apply the patch there.
**
** > fossil patch pull REMOTE-CHECKOUT
**
**       Create a patch on a remote check-out, transfer that patch to the
**       local machine (using ssh) and apply the patch in the local checkout.
**
** > fossil patch view FILENAME
**
**       View a summary of the the changes in the binary patch FILENAME.
**
*/
void patch_cmd(void){
  const char *zCmd;
  size_t n;
  if( g.argc<3 ){
    patch_usage:
    usage("apply|create|pull|push|view");
  }
  zCmd = g.argv[2];
  n = strlen(zCmd);
  if( strncmp(zCmd, "apply", n)==0 ){
    int forceFlag = find_option("force","f",0)!=0;
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("apply FILENAME");
    }
    if( !forceFlag && unsaved_changes(0) ){
      fossil_fatal("there are unsaved changes in the current checkout");
    }
    patch_attach(g.argv[3]);
    patch_apply();
  }else
  if( strncmp(zCmd, "create", n)==0 ){
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("create FILENAME");
    }
    patch_create(g.argv[3]);
  }else
  if( strncmp(zCmd, "pull", n)==0 ){
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("pull REMOTE-CHECKOUT");
    }
    fossil_print("TBD...\n");
  }else
  if( strncmp(zCmd, "push", n)==0 ){
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("push REMOTE-CHECKOUT");
    }
    fossil_print("TBD...\n");
  }else
  if( strncmp(zCmd, "view", n)==0 ){
    /* u64 diffFlags = diff_options(); */
    verify_all_options();
    if( g.argc!=4 ){
      usage("view FILENAME");
    }
    patch_attach(g.argv[3]);
    patch_view();
  }else
  {
    goto patch_usage;
  } 
}
