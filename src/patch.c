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
** Additional windows configuration for popen */
#if defined(_WIN32)
#  include <io.h>
#  include <fcntl.h>
#  undef popen
#  define popen _popen
#  undef pclose
#  define pclose _pclose
#endif

/*
** Flags passed from the main patch_cmd() routine into subfunctions used
** to implement the various subcommands.
*/
#define PATCH_DRYRUN   0x0001
#define PATCH_VERBOSE  0x0002
#define PATCH_FORCE    0x0004

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
void patch_create(const char *zOut, FILE *out){
  int vid;

  if( zOut && file_isdir(zOut, ExtFILE)!=0 ){
    fossil_fatal("patch file already exists: %s", zOut);
  }
  add_content_sql_commands(g.db);
  deltafunc_init(g.db);
  sqlite3_create_function(g.db, "read_co_file", 1, SQLITE_UTF8, 0,
                          readfileFunc, 0, 0);
  sqlite3_create_function(g.db, "mkdelta", 2, SQLITE_UTF8, 0,
                          mkdeltaFunc, 0, 0);
  db_multi_exec("ATTACH %Q AS patch;", zOut ? zOut : ":memory:");
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

  /* Merges */
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

  /* Write the database to standard output if zOut==0 */
  if( zOut==0 ){
    sqlite3_int64 sz;
    unsigned char *pData;
    pData = sqlite3_serialize(g.db, "patch", &sz, 0);
    if( pData==0 ){
      fossil_fatal("out of memory");
    }
#ifdef _WIN32
    fflush(out);
    _setmode(_fileno(out), _O_BINARY);
#endif
    fwrite(pData, sz, 1, out);
    sqlite3_free(pData); 
    fflush(out);
  }
}

/*
** Attempt to load and validate a patchfile identified by the first
** argument.
*/
void patch_attach(const char *zIn, FILE *in){
  Stmt q;
  if( g.db==0 ){
    sqlite3_open(":memory:", &g.db);
  }
  if( zIn==0 ){
    Blob buf;
    int rc;
    int sz;
    unsigned char *pData;
    blob_init(&buf, 0, 0);
#ifdef _WIN32
    _setmode(_fileno(in), _O_BINARY);
#endif
    sz = blob_read_from_channel(&buf, in, -1);
    pData = (unsigned char*)blob_buffer(&buf);
    db_multi_exec("ATTACH ':memory:' AS patch");
    if( g.fSqlTrace ){
      fossil_trace("-- deserialize(\"patch\", pData, %lld);\n", sz);
    }
    rc = sqlite3_deserialize(g.db, "patch", pData, sz, sz, 0);
    if( rc ){
      fossil_fatal("cannot open patch database: %s", sqlite3_errmsg(g.db));
    }
  }else if( !file_isfile(zIn, ExtFILE) ){
    fossil_fatal("no such file: %s", zIn);
  }else{
    db_multi_exec("ATTACH %Q AS patch", zIn);
  }
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
          " hash IS NULL AND delta IS NOT NULL,"  /* isNew */
          " delta IS NULL,"                       /* delete if origname NULL */
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
void patch_apply(unsigned mFlags){
  Stmt q;
  Blob cmd;

  if( (mFlags & PATCH_FORCE)==0 && unsaved_changes(0) ){
    fossil_fatal("there are unsaved changes in the current checkout");
  }
  blob_init(&cmd, 0, 0);
  file_chdir(g.zLocalRoot, 0);
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
    if( mFlags & PATCH_VERBOSE ){
      fossil_print("%-10s %s\n", "BASELINE", db_column_text(&q,0));
    }
  }
  db_finalize(&q);
  if( blob_size(&cmd)>0 ){
    if( mFlags & PATCH_DRYRUN ){
      fossil_print("%s\n", blob_str(&cmd));
    }else{
      int rc = fossil_system(blob_str(&cmd));
      if( rc ){
        fossil_fatal("unable to update to the baseline check-out: %s",
                     blob_str(&cmd));
      }
    }
  }
  blob_reset(&cmd);
  if( db_table_exists("patch","patchmerge") ){
    db_prepare(&q,
      "SELECT type, mhash, upper(type) FROM patch.patchmerge"
      " WHERE type IN ('merge','cherrypick','backout','integrate')"
      "   AND mhash NOT GLOB '*[^a-fA-F0-9]*';"
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zType = db_column_text(&q,0);
      blob_append_escaped_arg(&cmd, g.nameOfExe);
      if( strcmp(zType,"merge")==0 ){
        blob_appendf(&cmd, " merge %s\n", db_column_text(&q,1));
      }else{
        blob_appendf(&cmd, " merge --%s %s\n", zType, db_column_text(&q,1));
      }
      if( mFlags & PATCH_VERBOSE ){
        fossil_print("%-10s %s\n", db_column_text(&q,2), 
                    db_column_text(&q,0));
      }
    }
    db_finalize(&q);
    if( mFlags & PATCH_DRYRUN ){
      fossil_print("%s", blob_str(&cmd));
    }else{
      int rc = fossil_unsafe_system(blob_str(&cmd));
      if( rc ){
        fossil_fatal("unable to do merges:\n%s",
                     blob_str(&cmd));
      }
    }
    blob_reset(&cmd);
  }

  /* Deletions */
  db_prepare(&q, "SELECT pathname FROM patch.chng"
                 " WHERE origname IS NULL AND delta IS NULL");
  while( db_step(&q)==SQLITE_ROW ){
    blob_append_escaped_arg(&cmd, g.nameOfExe);
    blob_appendf(&cmd, " rm --hard %$\n", db_column_text(&q,0));
    if( mFlags & PATCH_VERBOSE ){
      fossil_print("%-10s %s\n", "DELETE", db_column_text(&q,0));
    }
  }
  db_finalize(&q);
  if( blob_size(&cmd)>0 ){
    if( mFlags & PATCH_DRYRUN ){
      fossil_print("%s", blob_str(&cmd));
    }else{
      int rc = fossil_unsafe_system(blob_str(&cmd));
      if( rc ){
        fossil_fatal("unable to do merges:\n%s",
                     blob_str(&cmd));
      }
    }
    blob_reset(&cmd);
  }

  /* Renames */
  db_prepare(&q,
    "SELECT origname, pathname FROM patch.chng"
    " WHERE origname IS NOT NULL"
    "   AND origname<>pathname"
  );
  while( db_step(&q)==SQLITE_ROW ){
    blob_append_escaped_arg(&cmd, g.nameOfExe);
    blob_appendf(&cmd, " mv --hard %$ %$\n",
        db_column_text(&q,0), db_column_text(&q,1));
    if( mFlags & PATCH_VERBOSE ){
      fossil_print("%-10s %s -> %s\n", "RENAME",
         db_column_text(&q,0), db_column_text(&q,1));
    }
  }
  db_finalize(&q);
  if( blob_size(&cmd)>0 ){
    if( mFlags & PATCH_DRYRUN ){
      fossil_print("%s", blob_str(&cmd));
    }else{
      int rc = fossil_unsafe_system(blob_str(&cmd));
      if( rc ){
        fossil_fatal("unable to rename files:\n%s",
                     blob_str(&cmd));
      }
    }
    blob_reset(&cmd);
  }

  /* Edits and new files */
  db_prepare(&q,
    "SELECT pathname, hash, isexe, islink, delta FROM patch.chng"
    " WHERE delta IS NOT NULL"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    const char *zHash = db_column_text(&q,1);
    int isExe = db_column_int(&q,2);
    int isLink = db_column_int(&q,3);
    Blob data;

    blob_init(&data, 0, 0);
    db_column_blob(&q, 4, &data);
    blob_uncompress(&data, &data);
    if( zHash ){
      Blob basis;
      int rid = fast_uuid_to_rid(zHash);
      int outSize, sz;
      char *aOut;
      if( rid==0 ){
        fossil_fatal("cannot locate basis artifact %s for %s",
                     zHash, zPathname);
      }
      if( !content_get(rid, &basis) ){
        fossil_fatal("cannot load basis artifact %d for %s", rid, zPathname);
      }
      outSize = delta_output_size(blob_buffer(&data),blob_size(&data));
      if( outSize<=0 ){
        fossil_fatal("malformed delta for %s", zPathname);
      }
      aOut = sqlite3_malloc64( outSize+1 );
      if( aOut==0 ){
        fossil_fatal("out of memory");
      }
      sz = delta_apply(blob_buffer(&basis), blob_size(&basis),
                       blob_buffer(&data), blob_size(&data), aOut);
      if( sz<0 ){
        fossil_fatal("malformed delta for %s", zPathname);
      }
      blob_reset(&basis);
      blob_reset(&data);
      blob_append(&data, aOut, sz);
      sqlite3_free(aOut);
      if( mFlags & PATCH_VERBOSE ){
        fossil_print("%-10s %s\n", "EDIT", zPathname);
      }
    }else{
      blob_append_escaped_arg(&cmd, g.nameOfExe);
      blob_appendf(&cmd, " add %$\n", zPathname);
      if( mFlags & PATCH_VERBOSE ){
        fossil_print("%-10s %s\n", "NEW", zPathname);
      }
    }
    if( (mFlags & PATCH_DRYRUN)==0 ){   
      if( isLink ){
        symlink_create(blob_str(&data), zPathname);
      }else{
        blob_write_to_file(&data, zPathname);
      }
      file_setexe(zPathname, isExe);
      blob_reset(&data);
    }
  }
  db_finalize(&q);
  if( blob_size(&cmd)>0 ){
    if( mFlags & PATCH_DRYRUN ){
      fossil_print("%s", blob_str(&cmd));
    }else{
      int rc = fossil_system(blob_str(&cmd));
      if( rc ){
        fossil_fatal("unable to add new files:\n%s",
                     blob_str(&cmd));
      }
    }
    blob_reset(&cmd);
  }
}

/*
** Find the filename of the patch file to be used by
** "fossil patch apply" or "fossil patch create".
**
** If the name is "-" return NULL.
**
** Otherwise, if there is a prior DIRECTORY argument, or if
** the --dir64 option is present, first chdir to the specified
** directory, and translate the name in the argument accordingly.
**
**
** The returned name is obtained from fossil_malloc() and should
** be freed by the caller.
*/
static char *patch_find_patch_filename(const char *zCmdName){
  const char *zDir64 = find_option("dir64",0,1);
  const char *zDir = 0;
  const char *zBaseName;
  char *zToFree = 0;
  char *zPatchFile = 0;
  if( zDir64 ){
    int n = 0;
    zToFree = decode64(zDir64, &n);
    zDir = zToFree;
  }
  verify_all_options();
  if( g.argc!=4 && g.argc!=5 ){
    usage(mprintf("%s [DIRECTORY] FILENAME", zCmdName));
  }
  if( g.argc==5 ){
    zDir = g.argv[3];
    zBaseName = g.argv[4];
  }else{
    zBaseName = g.argv[3];
  }
  if( fossil_strcmp(zBaseName, "-")==0 ){
    zPatchFile = 0;
  }else if( zDir ){
    zPatchFile = file_canonical_name_dup(g.argv[4]);
  }else{
    zPatchFile = fossil_strdup(g.argv[3]);
  }
  if( zDir && file_chdir(zDir,0) ){
    fossil_fatal("cannot change to directory \"%s\"", zDir);
  }
  fossil_free(zToFree);
  return zPatchFile;
}

/*
** Create a FILE* that will execute the remote side of a push or pull
** using ssh (probably) or fossil for local pushes and pulls.  Return
*/
static FILE *patch_remote_command(
  unsigned mFlags,             /* flags */
  const char *zThisCmd,        /* "push" or "pull" */
  const char *zRemoteCmd,      /* "apply" or "create" */
  const char *zRW              /* "w" or "r" */
){
  char *zRemote;
  char *zDir;
  Blob cmd;
  FILE *f;
  const char *zForce = (mFlags & PATCH_FORCE)!=0 ? " -f" : "";
  if( g.argc!=4 ){
    usage(mprintf("%s [USER@]HOST:DIRECTORY", zThisCmd));
  }
  zRemote = fossil_strdup(g.argv[3]);
  zDir = strchr(zRemote,':');
  if( zDir==0 ){
    zDir = zRemote;
    blob_init(&cmd, 0, 0);
    blob_append_escaped_arg(&cmd, g.nameOfExe);
    blob_appendf(&cmd, " patch %s%s %$ -", zRemoteCmd, zForce, zDir);
  }else{
    Blob remote;
    zDir[0] = 0;
    zDir++;
    transport_ssh_command(&cmd);
    blob_append_escaped_arg(&cmd, zRemote);
    blob_init(&remote, 0, 0);
    blob_appendf(&remote, "fossil patch %s%s --dir64 %z -", 
                 zRemoteCmd, zForce, encode64(zDir, -1));
    blob_append_escaped_arg(&cmd, blob_str(&remote));
    blob_reset(&remote);
  }
  if( mFlags & PATCH_VERBOSE ){
    fossil_print("# %s\n", blob_str(&cmd));
    fflush(stdout);
  }
  f = popen(blob_str(&cmd), zRW);
  if( f==0 ){
    fossil_fatal("cannot run command: %s", blob_str(&cmd));
  }
  blob_reset(&cmd);
  return f;
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
** > fossil patch create [DIRECTORY] FILENAME
**
**       Create a new binary patch in FILENAME that captures all uncommitted
**       changes in the check-out at DIRECTORY, or the current directory if
**       DIRECTORY is omitted.  If FILENAME is "-" then the binary patch
**       is written to standard output.
**
** > fossil patch apply [DIRECTORY] FILENAME
**
**       Apply the changes in FILENAME to the check-out a DIRECTORY, or
**       in the current directory if DIRECTORY is omitted. Options:
**
**           -f|--force     Apply the patch even though there are unsaved
**                          changes in the current check-out.
**           -n|--dryrun    Do nothing, but print what would have happened.
**           -v|--verbose   Extra output explaining what happens.
**
** > fossil patch push REMOTE-CHECKOUT
**
**       Create a patch for the current check-out, transfer that patch to
**       a remote machine (using ssh) and apply the patch there.  The
**       REMOTE-CHECKOUT is in one of the following formats:
**
**           *   DIRECTORY
**           *   HOST:DIRECTORY
**           *   USER@HOST:DIRECTORY
**
**       This command will only work if "fossil" is on the default PATH
**       of the remote machine.
**
** > fossil patch pull REMOTE-CHECKOUT
**
**       Create a patch on a remote check-out, transfer that patch to the
**       local machine (using ssh) and apply the patch in the local checkout.
**
**           -f|--force     Apply the patch even though there are unsaved
**                          changes in the current check-out.
**           -n|--dryrun    Do nothing, but print what would have happened.
**           -v|--verbose   Extra output explaining what happens.
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
    char *zIn;
    unsigned flags = 0;
    if( find_option("dryrun","n",0) )   flags |= PATCH_DRYRUN;
    if( find_option("verbose","v",0) )  flags |= PATCH_VERBOSE;
    if( find_option("force","f",0) )    flags |= PATCH_FORCE;
    zIn = patch_find_patch_filename("apply");
    db_must_be_within_tree();
    patch_attach(zIn, stdin);
    patch_apply(flags);
    fossil_free(zIn);
  }else
  if( strncmp(zCmd, "create", n)==0 ){
    char *zOut;
    zOut = patch_find_patch_filename("create");
    db_must_be_within_tree();
    patch_create(zOut, stdout);
    fossil_free(zOut);
  }else
  if( strncmp(zCmd, "pull", n)==0 ){
    FILE *pIn = 0;
    unsigned flags = 0;
    if( find_option("dryrun","n",0) )   flags |= PATCH_DRYRUN;
    if( find_option("verbose","v",0) )  flags |= PATCH_VERBOSE;
    if( find_option("force","f",0) )    flags |= PATCH_FORCE;
    db_must_be_within_tree();
    verify_all_options();
    pIn = patch_remote_command(flags & (~PATCH_FORCE), "pull", "create", "r");
    if( pIn ){
      patch_attach(0, pIn);
      pclose(pIn);
      patch_apply(flags);
    }
  }else
  if( strncmp(zCmd, "push", n)==0 ){
    FILE *pOut = 0;
    unsigned flags = 0;
    if( find_option("dryrun","n",0) )   flags |= PATCH_DRYRUN;
    if( find_option("verbose","v",0) )  flags |= PATCH_VERBOSE;
    if( find_option("force","f",0) )    flags |= PATCH_FORCE;
    db_must_be_within_tree();
    verify_all_options();
    pOut = patch_remote_command(flags, "push", "apply", "w");
    if( pOut ){
      patch_create(0, pOut);
      pclose(pOut);
    }
  }else
  if( strncmp(zCmd, "view", n)==0 ){
    const char *zIn;
    verify_all_options();
    if( g.argc!=4 ){
      usage("view FILENAME");
    }
    zIn = g.argv[3];
    if( fossil_strcmp(zIn, "-")==0 ) zIn = 0;
    patch_attach(zIn, stdin);
    patch_view();
  }else
  {
    goto patch_usage;
  } 
}
