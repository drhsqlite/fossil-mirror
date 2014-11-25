/*
** Copyright (c) 2014 D. Richard Hipp
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
** This file contains code used to implement and manage a "bundle" file.
*/
#include "config.h"
#include "bundle.h"
#include <assert.h>

/*
** SQL code used to initialize the schema of a bundle.
*/
static const char zBundleInit[] = 
@ CREATE TABLE IF NOT EXISTS "%w".bconfig(
@   bcname TEXT,
@   bcvalue ANY
@ );
@ CREATE TABLE IF NOT EXISTS "%w".bblob(
@   blobid INTEGER PRIMARY KEY,
@   uuid TEXT NOT NULL,
@   sz INT NOT NULL,
@   delta ANY,
@   data BLOB
@ );
;

/*
** Attach a bundle file to the current database connection using the
** attachment name zBName.
*/
static void bundle_attach_file(
  const char *zFile,       /* Name of the file that contains the bundle */
  const char *zBName,      /* Attachment name */
  int doInit               /* Initialize a new bundle, if true */
){
  db_multi_exec("ATTACH %Q AS %Q;", zFile, zBName);
  db_multi_exec(zBundleInit /*works-like:"%w%w"*/, zBName, zBName);
}

/*
** List the content of a bundle
*/
static void bundle_ls(void){
  
}

/*
** Implement the "fossil bundle append BUNDLE FILE..." command.
*/
static void bundle_append(void){
  char *zFilename;
  Blob content, hash;
  int i;
  Stmt q;

  verify_all_options();
  db_prepare(&q, 
    "INSERT INTO bblob(blobid, uuid, sz, delta, data) "
    "VALUES(NULL, $uuid, $sz, NULL, $data)");
  db_begin_transaction();
  for(i=4; i<g.argc; i++){
    int sz;
    blob_read_from_file(&content, g.argv[i]);
    sz = blob_size(&content);
    sha1sum_blob(&content, &hash);
    blob_compress(&content, &content);
    db_bind_text(&q, "$uuid", blob_str(&hash));
    db_bind_int(&q, "$sz", sz);
    db_bind_blob(&q, "$data", &content);
    db_step(&q);
    db_reset(&q);
    blob_reset(&content);
    blob_reset(&hash);
  }
  db_end_transaction(0);
  db_finalize(&q);
}
  

/*
** COMMAND: bundle
**
** Usage: %fossil bundle SUBCOMMAND ARGS...
**
**   fossil bundle export BUNDLE ?OPTIONS?
**
**      Generate a new bundle, in the file named BUNDLE, that constains a
**      subset of the check-ins in the repository (usually a single branch)
**      as determined by OPTIONS.  OPTIONS include:
**
**         --branch BRANCH            Package all check-ins on BRANCH.
**         --from TAG1 --to TAG2      Package check-ins between TAG1 and TAG2.
**         --m COMMENT                Add the comment to the bundle.
**         --standalone               The bundle will not include any deltas
**                                       against files not in the bundle.
**         --explain                  Just explain what would have happened.
**
**   fossil bundle import BUNDLE ?--publish? ?--explain?
**
**      Import the bundle in file BUNDLE into the repository.  The --publish
**      option makes the import public.  The --explain option makes no changes
**      to the repository but rather explains what would have happened.
**
**   fossil bundle ls BUNDLE
**
**      List the contents of BUNDLE on standard output
**
**   fossil bundle append BUNDLE FILE...
**
**      Add files named on the command line to BUNDLE.  This subcommand has
**      little practical use and is mostly intended for testing.
**
**   fossil bundle extract BUNDLE ?UUID? ?FILE?
**
**      Extract artifacts from the bundle.  With no arguments, all artifacts
**      are extracted into files named by the UUID.  If a specific UUID is
**      specified, then only that one artifact is extracted.  If a FILE
**      argument is also given, then the artifact is extracted into that
**      particular file.
**
** SUMMARY:
**   fossil bundle export BUNDLEFILE ?OPTIONS?
**          --branch BRANCH
**          --from TAG1 --to TAG2
**          --explain
**   fossil bundle import BUNDLEFILE ?OPTIONS?
**          --publish
**          --explain
**   fossil bundle ls BUNDLEFILE
*/
void bundle_cmd(void){
  const char *zSubcmd;
  const char *zBundleFile;
  int n;
  if( g.argc<4 ) usage("SUBCOMMAND BUNDLE ?ARGUMENTS?");
  zSubcmd = g.argv[2];
  db_find_and_open_repository(0,0);
  zBundleFile = g.argv[3];
  bundle_attach_file(zBundleFile, "b1", 1);
  n = (int)strlen(zSubcmd);
  if( strncmp(zSubcmd, "export", n)==0 ){
    fossil_print("Not yet implemented...\n");
  }else if( strncmp(zSubcmd, "import", n)==0 ){
    fossil_print("Not yet implemented...\n");
  }else if( strncmp(zSubcmd, "ls", n)==0 ){
    bundle_ls();
  }else if( strncmp(zSubcmd, "append", n)==0 ){
    bundle_append();
  }else if( strncmp(zSubcmd, "extract", n)==0 ){
    fossil_print("Not yet implemented...\n");
  }else{
    fossil_fatal("unknown subcommand for bundle: %s", zSubcmd);
  }
}
