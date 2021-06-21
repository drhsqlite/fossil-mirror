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
  db_multi_exec("ATTACH %Q AS patch;", zOut);
  db_multi_exec(
    "PRAGMA patch.journal_mode=OFF;\n"
    "PRAGMA patch.page_size=512;\n"
    "CREATE TABLE patch.chng(\n"
    "  fname TEXT,\n"   /* Filename */
    "  hash TEXT,\n"    /* Baseline hash.  NULL for new files. */
    "  isexe BOOL,\n"   /* True if executable */
    "  islink BOOL,\n"  /* True if is a symbolic link */
    "  delta BLOB\n"    /* Delta.  NULL if file deleted */
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
    "INSERT INTO patch.chng(fname,hash,isexe,islink,delta)"
    "  SELECT pathname, NULL, isexe, islink,"
    "         compress(read_co_file(%Q||pathname))"
    "    FROM vfile WHERE rid==0;",
    g.zLocalRoot
  );
  /* Deleted files */
  db_multi_exec(
    "INSERT INTO patch.chng(fname,hash,isexe,islink,delta)"
    "  SELECT pathname, NULL, 0, 0, NULL"
    "    FROM vfile WHERE deleted;"
  );
  /* Changed files */
  db_multi_exec(
    "INSERT INTO patch.chng(fname,hash,isexe,islink,delta)"
    "  SELECT pathname, blob.uuid, isexe, islink,"
    "         compress(delta_create(content(blob.uuid),"
                          "read_co_file(%Q||pathname)))"
    "    FROM vfile, blob"
    "   WHERE blob.rid=vfile.rid AND NOT deleted AND chnged;",
    g.zLocalRoot
  );
}

/*
** Attempt to load and validate a patchfile identified by the first
** argument.
*/
void patch_attach(const char *zIn){
  Stmt q;
  if( !file_isfile(zIn, zIn) ){
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
    fossil_print("Patch against check-in %S\n", db_column_text(&q,0));
  }else{
    fossil_fatal("ERROR: Missing patch baseline");
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT fname, hash IS NULL AS isnew, delta IS NULL AS isdel"
                 "  FROM patch.chng ORDER BY 1");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zClass = "CHANGED";
    if( db_column_int(&q, 1) ){
      zClass = "NEW";
    }else if( db_column_int(&q, 2) ){
      zClass = "DELETED";
    }
    fossil_print("%-10s %s\n", zClass, db_column_text(&q,0));
  }
  db_finalize(&q);
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
**       Apply the changes in FILENAME to the current check-out.
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
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("apply FILENAME");
    }
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
