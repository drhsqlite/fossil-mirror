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
@   blobid INTEGER PRIMARY KEY,      -- Blob ID
@   uuid TEXT NOT NULL,              -- SHA1 hash of expanded blob
@   sz INT NOT NULL,                 -- Size of blob after expansion
@   delta INT REFERENCES bblob,      -- Delta compression basis, or NULL
@   data BLOB                        -- compressed content
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
** Identify a subsection of the checkin tree using command-line switches.
** There must be one of the following switch available:
**
**     --branch BRANCHNAME          All checkins on the most recent
**                                  instance of BRANCHNAME
**     --from TAG1 [--to TAG2]      Checkin TAG1 and all primary descendants
**                                  up to and including TAG2
**     --checkin TAG                Checkin TAG only
**
** Store the RIDs for all applicable checkins in the zTab table that 
** should already exist.  Invoke fossil_fatal() if any kind of error is
** seen.
*/
void subtree_from_arguments(const char *zTab){
  const char *zBr;
  const char *zFrom;
  const char *zTo;
  const char *zCkin;
  int rid, endRid;

  zBr = find_option("branch",0,1);
  zFrom = find_option("from",0,1);
  zTo = find_option("to",0,1);
  zCkin = find_option("checkin",0,1);
  if( zCkin ){
    if( zFrom ) fossil_fatal("cannot use both --checkin and --from");
    if( zBr ) fossil_fatal("cannot use both --checkin and --branch");
    rid = symbolic_name_to_rid(zCkin, "ci");
    endRid = rid;
  }else{
    endRid = zTo ? name_to_typed_rid(zTo, "ci") : 0;
  }
  if( zFrom ){
    rid = name_to_typed_rid(zFrom, "ci");
  }else if( zBr ){
    rid = name_to_typed_rid(zBr, "br");
  }else if( zCkin==0 ){
    fossil_fatal("need on of: --branch, --from, --checkin");
  }
  db_multi_exec("INSERT OR IGNORE INTO \"%w\" VALUES(%d)", zTab, rid);
  if( rid!=endRid ){
    Blob sql;
    blob_zero(&sql);
    blob_appendf(&sql,
       "WITH RECURSIVE child(rid) AS (VALUES(%d) UNION ALL "
       "  SELECT cid FROM plink, child"
       "   WHERE plink.pid=child.rid"
       "     AND plink.isPrim", rid);
    if( endRid>0 ){
      double endTime = db_double(0.0, "SELECT mtime FROM event WHERE objid=%d",
                                 endRid);
      blob_appendf(&sql,
        "    AND child.rid!=%d"
        "    AND (SELECT mtime FROM event WHERE objid=plink.cid)<=%.17g",
        endRid, endTime
      );
    }
    if( zBr ){
      blob_appendf(&sql,
         "     AND EXISTS(SELECT 1 FROM tagxref"
                        "  WHERE tagid=%d AND tagtype>0"
                        "    AND value=%Q and rid=plink.cid))",
         TAG_BRANCH, zBr);
    }
    blob_appendf(&sql, ") INSERT OR IGNORE INTO \"%w\" SELECT rid FROM child;",
                 zTab);
    db_multi_exec("%s", blob_str(&sql)/*safe-for-%s*/);
  }
}

/*
** COMMAND: test-subtree
**
** Usage: %fossil test-subtree ?OPTIONS?
**
** Show the subset of checkins that match the supplied options.  This
** command is used to test the subtree_from_options() subroutine in the
** implementation and does not really have any other practical use that
** we know of.
**
** Options:
**    --branch BRANCH           Include only checkins on BRANCH
**    --from TAG                Start the subtree at TAG
**    --to TAG                  End the subtree at TAG
**    --checkin TAG             The subtree is the single checkin TAG
*/
void test_subtree_cmd(void){
  Stmt q;
  db_find_and_open_repository(0,0);
  db_begin_transaction();
  db_multi_exec("CREATE TEMP TABLE tobundle(rid INTEGER PRIMARY KEY);");
  subtree_from_arguments("tobundle");
  db_prepare(&q,
    "SELECT "
    "  (SELECT substr(uuid,1,10) FROM blob WHERE rid=tobundle.rid),"
    "  (SELECT substr(comment,1,30) FROM event WHERE objid=tobundle.rid),"
    "  tobundle.rid"
    " FROM tobundle;"
  );
  while( db_step(&q)==SQLITE_ROW ){
     fossil_print("%5d %s %s\n", 
        db_column_int(&q, 2),
        db_column_text(&q, 0),
        db_column_text(&q, 1));
  }
  db_finalize(&q);
  db_end_transaction(1);
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
