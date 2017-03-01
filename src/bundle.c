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
**
** The bblob.delta field can be an integer, a text string, or NULL.
** If an integer, then the corresponding blobid is the delta basis.
** If a text string, then that string is a SHA1 hash for the delta
** basis, which is presumably in the master repository.  If NULL, then
** data contains content without delta compression.
*/
static const char zBundleInit[] =
@ CREATE TABLE IF NOT EXISTS "%w".bconfig(
@   bcname TEXT,
@   bcvalue ANY
@ );
@ CREATE TABLE IF NOT EXISTS "%w".bblob(
@   blobid INTEGER PRIMARY KEY,      -- Blob ID
@   uuid TEXT NOT NULL,              -- hash of expanded blob
@   sz INT NOT NULL,                 -- Size of blob after expansion
@   delta ANY,                       -- Delta compression basis, or NULL
@   notes TEXT,                      -- Description of content
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
  int rc;
  char *zErrMsg = 0;
  char *zSql;
  if( !doInit && file_size(zFile)<0 ){
    fossil_fatal("no such file: %s", zFile);
  }
  assert( g.db );
  zSql = sqlite3_mprintf("ATTACH %Q AS %Q", zFile, zBName);
  if( zSql==0 ) fossil_fatal("out of memory");
  rc = sqlite3_exec(g.db, zSql, 0, 0, &zErrMsg);
  sqlite3_free(zSql);
  if( rc!=SQLITE_OK || zErrMsg ){
    if( zErrMsg==0 ) zErrMsg = (char*)sqlite3_errmsg(g.db);
    fossil_fatal("not a valid bundle: %s", zFile);
  }
  if( doInit ){
    db_multi_exec(zBundleInit /*works-like:"%w%w"*/, zBName, zBName);
  }else{
    sqlite3_stmt *pStmt;
    zSql = sqlite3_mprintf("SELECT bcname, bcvalue"
                           "  FROM \"%w\".bconfig", zBName);
    if( zSql==0 ) fossil_fatal("out of memory");
    rc = sqlite3_prepare(g.db, zSql, -1, &pStmt, 0);
    if( rc ) fossil_fatal("not a valid bundle: %s", zFile);
    sqlite3_free(zSql);
    sqlite3_finalize(pStmt);
    zSql = sqlite3_mprintf("SELECT blobid, uuid, sz, delta, notes, data"
                           "  FROM \"%w\".bblob", zBName);
    if( zSql==0 ) fossil_fatal("out of memory");
    rc = sqlite3_prepare(g.db, zSql, -1, &pStmt, 0);
    if( rc ) fossil_fatal("not a valid bundle: %s", zFile);
    sqlite3_free(zSql);
    sqlite3_finalize(pStmt);
  }
}

/*
**  fossil bundle ls BUNDLE ?OPTIONS?
**
** Display the content of a bundle in human-readable form.
*/
static void bundle_ls_cmd(void){
  Stmt q;
  sqlite3_int64 sumSz = 0;
  sqlite3_int64 sumLen = 0;
  int bDetails = find_option("details","l",0)!=0;
  verify_all_options();
  if( g.argc!=4 ) usage("ls BUNDLE ?OPTIONS?");
  bundle_attach_file(g.argv[3], "b1", 0);
  db_prepare(&q,
    "SELECT bcname, bcvalue FROM bconfig"
    " WHERE typeof(bcvalue)='text'"
    "   AND bcvalue NOT GLOB char(0x2a,0x0a,0x2a);"
  );
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%s: %s\n", db_column_text(&q,0), db_column_text(&q,1));
  }
  db_finalize(&q);
  fossil_print("%.78c\n",'-');
  if( bDetails ){
    db_prepare(&q,
      "SELECT blobid, substr(uuid,1,10), coalesce(substr(delta,1,10),''),"
      "       sz, length(data), notes"
      "  FROM bblob"
    );
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%4d %10s %10s %8d %8d %s\n",
        db_column_int(&q,0),
        db_column_text(&q,1),
        db_column_text(&q,2),
        db_column_int(&q,3),
        db_column_int(&q,4),
        db_column_text(&q,5));
      sumSz += db_column_int(&q,3);
      sumLen += db_column_int(&q,4);
    }
    db_finalize(&q);
    fossil_print("%27s %8lld %8lld\n", "Total:", sumSz, sumLen);
  }else{
    db_prepare(&q,
      "SELECT substr(uuid,1,16), notes FROM bblob"
    );
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%16s %s\n",
        db_column_text(&q,0),
        db_column_text(&q,1));
    }
    db_finalize(&q);
  }
}

/*
** Implement the "fossil bundle append BUNDLE FILE..." command.  Add
** the named files into the BUNDLE.  Create the BUNDLE if it does not
** alraedy exist.
*/
static void bundle_append_cmd(void){
  Blob content, hash;
  int i;
  Stmt q;

  verify_all_options();
  bundle_attach_file(g.argv[3], "b1", 1);
  db_prepare(&q,
    "INSERT INTO bblob(blobid, uuid, sz, delta, data, notes) "
    "VALUES(NULL, $uuid, $sz, NULL, $data, $filename)");
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
    db_bind_text(&q, "$filename", g.argv[i]);
    db_step(&q);
    db_reset(&q);
    blob_reset(&content);
    blob_reset(&hash);
  }
  db_end_transaction(0);
  db_finalize(&q);
}

/*
** Identify a subsection of the check-in tree using command-line switches.
** There must be one of the following switch available:
**
**     --branch BRANCHNAME          All check-ins on the most recent
**                                  instance of BRANCHNAME
**     --from TAG1 [--to TAG2]      Check-in TAG1 and all primary descendants
**                                  up to and including TAG2
**     --checkin TAG                Check-in TAG only
**
** Store the RIDs for all applicable check-ins in the zTab table that
** should already exist.  Invoke fossil_fatal() if any kind of error is
** seen.
*/
void subtree_from_arguments(const char *zTab){
  const char *zBr;
  const char *zFrom;
  const char *zTo;
  const char *zCkin;
  int rid = 0, endRid;

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
    fossil_fatal("need one of: --branch, --from, --checkin");
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
                        "    AND value=%Q and rid=plink.cid)",
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
** Show the subset of check-ins that match the supplied options.  This
** command is used to test the subtree_from_options() subroutine in the
** implementation and does not really have any other practical use that
** we know of.
**
** Options:
**    --branch BRANCH           Include only check-ins on BRANCH
**    --from TAG                Start the subtree at TAG
**    --to TAG                  End the subtree at TAG
**    --checkin TAG             The subtree is the single check-in TAG
**    --all                     Include FILE and TAG artifacts
**    --exclusive               Include FILES exclusively on check-ins
*/
void test_subtree_cmd(void){
  int bAll = find_option("all",0,0)!=0;
  int bExcl = find_option("exclusive",0,0)!=0;
  db_find_and_open_repository(0,0);
  db_begin_transaction();
  db_multi_exec("CREATE TEMP TABLE tobundle(rid INTEGER PRIMARY KEY);");
  subtree_from_arguments("tobundle");
  verify_all_options();
  if( bAll ) find_checkin_associates("tobundle",bExcl);
  describe_artifacts_to_stdout("IN tobundle", 0);
  db_end_transaction(1);
}

/* fossil bundle export BUNDLE ?OPTIONS?
**
** OPTIONS:
**   --branch BRANCH --from TAG --to TAG
**   --checkin TAG
**   --standalone
*/
static void bundle_export_cmd(void){
  int bStandalone = find_option("standalone",0,0)!=0;
  int mnToBundle;   /* Minimum RID in the bundle */
  Stmt q;

  /* Decode the arguments (like --branch) that specify which artifacts
  ** should be in the bundle */
  db_multi_exec("CREATE TEMP TABLE tobundle(rid INTEGER PRIMARY KEY);");
  subtree_from_arguments("tobundle");
  find_checkin_associates("tobundle", 0);
  verify_all_options();
  describe_artifacts("IN tobundle");

  if( g.argc!=4 ) usage("export BUNDLE ?OPTIONS?");
  /* Create the new bundle */
  bundle_attach_file(g.argv[3], "b1", 1);
  db_begin_transaction();

  /* Add 'mtime' and 'project-code' entries to the bconfig table */
  db_multi_exec(
    "INSERT INTO bconfig(bcname,bcvalue)"
    " VALUES('mtime',datetime('now'));"
  );
  db_multi_exec(
    "INSERT INTO bconfig(bcname,bcvalue)"
    " SELECT name, value FROM config"
    "  WHERE name IN ('project-code','parent-project-code');"
  );

  /* Directly copy content from the repository into the bundle as long
  ** as the repository content is a delta from some other artifact that
  ** is also in the bundle.
  */
  db_multi_exec(
    "REPLACE INTO bblob(blobid,uuid,sz,delta,data,notes) "
    " SELECT"
    "   tobundle.rid,"
    "   blob.uuid,"
    "   blob.size,"
    "   delta.srcid,"
    "   blob.content,"
    "   (SELECT summary FROM description WHERE rid=blob.rid)"
    " FROM tobundle, blob, delta"
    " WHERE blob.rid=tobundle.rid"
    "   AND delta.rid=tobundle.rid"
    "   AND delta.srcid IN tobundle;"
  );

  /* For all the remaining artifacts, we need to construct their deltas
  ** manually.
  */
  mnToBundle = db_int(0,"SELECT min(rid) FROM tobundle");
  db_prepare(&q,
     "SELECT rid FROM tobundle"
     " WHERE rid NOT IN (SELECT blobid FROM bblob)"
     " ORDER BY +rid;"
  );
  while( db_step(&q)==SQLITE_ROW ){
    Blob content;
    int rid = db_column_int(&q,0);
    int deltaFrom = 0;

    /* Get the raw, uncompressed content of the artifact into content */
    content_get(rid, &content);

    /* Try to find another artifact, not within the bundle, that is a
    ** plausible candidate for being a delta basis for the content.  Set
    ** deltaFrom to the RID of that other artifact.  Leave deltaFrom set
    ** to zero if the content should not be delta-compressed
    */
    if( !bStandalone ){
      if( db_exists("SELECT 1 FROM plink WHERE cid=%d",rid) ){
        deltaFrom = db_int(0,
           "SELECT max(cid) FROM plink"
           " WHERE cid<%d", mnToBundle);
      }else{
        deltaFrom = db_int(0,
           "SELECT max(fid) FROM mlink"
           " WHERE fnid=(SELECT fnid FROM mlink WHERE fid=%d)"
           "   AND fid<%d", rid, mnToBundle);
      }
    }

    /* Try to insert the artifact as a delta
    */
    if( deltaFrom ){
      Blob basis, delta;
      content_get(deltaFrom, &basis);
      blob_delta_create(&basis, &content, &delta);
      if( blob_size(&delta)>0.9*blob_size(&content) ){
        deltaFrom = 0;
      }else{
        Stmt ins;
        blob_compress(&delta, &delta);
        db_prepare(&ins,
          "REPLACE INTO bblob(blobid,uuid,sz,delta,data,notes)"
          " SELECT %d, uuid, size, (SELECT uuid FROM blob WHERE rid=%d),"
          "  :delta, (SELECT summary FROM description WHERE rid=blob.rid)"
          "  FROM blob WHERE rid=%d", rid, deltaFrom, rid);
        db_bind_blob(&ins, ":delta", &delta);
        db_step(&ins);
        db_finalize(&ins);
      }
      blob_reset(&basis);
      blob_reset(&delta);
    }

    /* If unable to insert the artifact as a delta, insert full-text */
    if( deltaFrom==0 ){
      Stmt ins;
      blob_compress(&content, &content);
      db_prepare(&ins,
        "REPLACE INTO bblob(blobid,uuid,sz,delta,data,notes)"
        " SELECT rid, uuid, size, NULL, :content,"
        "        (SELECT summary FROM description WHERE rid=blob.rid)"
          " FROM blob WHERE rid=%d", rid);
      db_bind_blob(&ins, ":content", &content);
      db_step(&ins);
      db_finalize(&ins);
    }
    blob_reset(&content);
  }
  db_finalize(&q);

  db_end_transaction(0);
}


/*
** There is a TEMP table bix(blobid,delta) containing a set of purgeitems
** that need to be transferred to the BLOB table.  This routine does
** all items that have srcid=iSrc.  The pBasis blob holds the content
** of the source document if iSrc>0.
*/
static void bundle_import_elements(int iSrc, Blob *pBasis, int isPriv){
  Stmt q;
  static Bag busy;
  assert( pBasis!=0 || iSrc==0 );
  if( iSrc>0 ){
    if( bag_find(&busy, iSrc) ){
      fossil_fatal("delta loop while uncompressing bundle artifacts");
    }
    bag_insert(&busy, iSrc);
  }
  db_prepare(&q,
     "SELECT uuid, data, bblob.delta, bix.blobid"
     "  FROM bix, bblob"
     " WHERE bix.delta=%d"
     "   AND bix.blobid=bblob.blobid;",
     iSrc
  );
  while( db_step(&q)==SQLITE_ROW ){
    Blob h1, c1, c2;
    int rid;
    blob_zero(&h1);
    db_column_blob(&q, 0, &h1);
    blob_zero(&c1);
    db_column_blob(&q, 1, &c1);
    blob_uncompress(&c1, &c1);
    blob_zero(&c2);
    if( db_column_type(&q,2)==SQLITE_TEXT && db_column_bytes(&q,2)>=HNAME_MIN ){
      Blob basis;
      rid = db_int(0,"SELECT rid FROM blob WHERE uuid=%Q",
                   db_column_text(&q,2));
      content_get(rid, &basis);
      blob_delta_apply(&basis, &c1, &c2);
      blob_reset(&basis);
      blob_reset(&c1);
    }else if( pBasis ){
      blob_delta_apply(pBasis, &c1, &c2);
      blob_reset(&c1);
    }else{
      c2 = c1;
    }
    if( hname_verify_hash(&c2, blob_buffer(&h1), blob_size(&h1))==0 ){
      fossil_fatal("artifact hash error on %b", &h1);
    }
    rid = content_put_ex(&c2, blob_str(&h1), 0, 0, isPriv);
    if( rid==0 ){
      fossil_fatal("%s", g.zErrMsg);
    }else{
      if( !isPriv ) content_make_public(rid);
      content_get(rid, &c1);
      manifest_crosslink(rid, &c1, MC_NO_ERRORS);
      db_multi_exec("INSERT INTO got(rid) VALUES(%d)",rid);
    }
    bundle_import_elements(db_column_int(&q,3), &c2, isPriv);
    blob_reset(&c2);
  }
  db_finalize(&q);
  if( iSrc>0 ) bag_remove(&busy, iSrc);
}

/*
** Extract an item from content from the bundle
*/
static void bundle_extract_item(
  int blobid,          /* ID of the item to extract */
  Blob *pOut           /* Write the content into this blob */
){
  Stmt q;
  Blob x, basis, h1;
  static Bag busy;

  db_prepare(&q, "SELECT uuid, delta, data FROM bblob"
                 " WHERE blobid=%d", blobid);
  if( db_step(&q)!=SQLITE_ROW ){
    db_finalize(&q);
    fossil_fatal("no such item: %d", blobid);
  }
  if( bag_find(&busy, blobid) ) fossil_fatal("delta loop");
  blob_zero(&x);
  db_column_blob(&q, 2, &x);
  blob_uncompress(&x, &x);
  if( db_column_type(&q,1)==SQLITE_INTEGER ){
    bundle_extract_item(db_column_int(&q,1), &basis);
    blob_delta_apply(&basis, &x, pOut);
    blob_reset(&basis);
    blob_reset(&x);
  }else if( db_column_type(&q,1)==SQLITE_TEXT ){
    int rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q",
                     db_column_text(&q,1));
    if( rid==0 ){
      fossil_fatal("cannot find delta basis %s", db_column_text(&q,1));
    }
    content_get(rid, &basis);
    db_column_blob(&q, 2, &x);
    blob_delta_apply(&basis, &x, pOut);
    blob_reset(&basis);
    blob_reset(&x);
  }else{
    *pOut = x;
  }
  blob_zero(&h1);
  db_column_blob(&q, 0, &h1);
  if( hname_verify_hash(pOut, blob_buffer(&h1), blob_size(&h1))==0 ){
    fossil_fatal("incorrect hash for artifact %b", &h1);
  }
  blob_reset(&h1);
  bag_remove(&busy, blobid);
  db_finalize(&q);
}

/* fossil bundle cat BUNDLE UUID...
**
** Write elements of a bundle on standard output
*/
static void bundle_cat_cmd(void){
  int i;
  Blob x;
  verify_all_options();
  if( g.argc<5 ) usage("cat BUNDLE UUID...");
  bundle_attach_file(g.argv[3], "b1", 1);
  blob_zero(&x);
  for(i=4; i<g.argc; i++){
    int blobid = db_int(0,"SELECT blobid FROM bblob WHERE uuid LIKE '%q%%'",
                        g.argv[i]);
    if( blobid==0 ){
      fossil_fatal("no such artifact in bundle: %s", g.argv[i]);
    }
    bundle_extract_item(blobid, &x);
    blob_write_to_file(&x, "-");
    blob_reset(&x);
  }
}


/* fossil bundle import BUNDLE ?OPTIONS?
**
** Attempt to import the changes contained in BUNDLE.  Make the change
** private so that they do not sync.
**
** OPTIONS:
**    --force           Import even if the project-code does not match
**    --publish         Imported changes are not private
*/
static void bundle_import_cmd(void){
  int forceFlag = find_option("force","f",0)!=0;
  int isPriv = find_option("publish",0,0)==0;
  char *zMissingDeltas;
  verify_all_options();
  if ( g.argc!=4 ) usage("import BUNDLE ?OPTIONS?");
  bundle_attach_file(g.argv[3], "b1", 1);

  /* Only import a bundle that was generated from a repo with the same
  ** project code, unless the --force flag is true */
  if( !forceFlag ){
    if( !db_exists("SELECT 1 FROM config, bconfig"
                  " WHERE config.name='project-code'"
                  "   AND bconfig.bcname='project-code'"
                  "   AND config.value=bconfig.bcvalue;")
    ){
      fossil_fatal("project-code in the bundle does not match the "
                   "repository project code.  (override with --force).");
    }
  }

  /* If the bundle contains deltas with a basis that is external to the
  ** bundle and those external basis files are missing from the local
  ** repo, then the delta encodings cannot be decoded and the bundle cannot
  ** be extracted. */
  zMissingDeltas = db_text(0,
      "SELECT group_concat(substr(delta,1,10),' ')"
      "  FROM bblob"
      " WHERE typeof(delta)='text' AND length(delta)>=%d"
      "   AND NOT EXISTS(SELECT 1 FROM blob WHERE uuid=bblob.delta)",
      HNAME_MIN);
  if( zMissingDeltas && zMissingDeltas[0] ){
    fossil_fatal("delta basis artifacts not found in repository: %s",
                 zMissingDeltas);
  }

  db_begin_transaction();
  db_multi_exec(
    "CREATE TEMP TABLE bix("
    "  blobid INTEGER PRIMARY KEY,"
    "  delta INTEGER"
    ");"
    "CREATE INDEX bixdelta ON bix(delta);"
    "INSERT INTO bix(blobid,delta)"
    "  SELECT blobid,"
    "         CASE WHEN typeof(delta)=='integer'"
    "              THEN delta ELSE 0 END"
    "    FROM bblob"
    "   WHERE NOT EXISTS(SELECT 1 FROM blob WHERE uuid=bblob.uuid AND size>=0);"
    "CREATE TEMP TABLE got(rid INTEGER PRIMARY KEY ON CONFLICT IGNORE);"
  );
  manifest_crosslink_begin();
  bundle_import_elements(0, 0, isPriv);
  manifest_crosslink_end(0);
  describe_artifacts_to_stdout("IN got", "Imported content:");
  db_end_transaction(0);
}

/* fossil bundle purge BUNDLE
**
** Try to undo a prior "bundle import BUNDLE".
**
** If the --force option is omitted, then this will only work if
** there have been no check-ins or tags added that use the import.
**
** This routine never removes content that is not already in the bundle
** so the bundle serves as a backup.  The purge can be undone using
** "fossil bundle import BUNDLE".
*/
static void bundle_purge_cmd(void){
  int bForce = find_option("force",0,0)!=0;
  int bTest = find_option("test",0,0)!=0;  /* Undocumented --test option */
  const char *zFile = g.argv[3];
  verify_all_options();
  if ( g.argc!=4 ) usage("purge BUNDLE ?OPTIONS?");
  bundle_attach_file(zFile, "b1", 0);
  db_begin_transaction();

  /* Find all check-ins of the bundle */
  db_multi_exec(
    "CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY);"
    "INSERT OR IGNORE INTO ok SELECT blob.rid FROM bblob, blob, plink"
    " WHERE bblob.uuid=blob.uuid"
    "   AND plink.cid=blob.rid;"
  );

  /* Check to see if new check-ins have been committed to check-ins in
  ** the bundle.  Do not allow the purge if that is true and if --force
  ** is omitted.
  */
  if( !bForce ){
    Stmt q;
    int n = 0;
    db_prepare(&q,
      "SELECT cid FROM plink WHERE pid IN ok AND cid NOT IN ok"
    );
    while( db_step(&q)==SQLITE_ROW ){
      whatis_rid(db_column_int(&q,0),0);
      fossil_print("%.78c\n", '-');
      n++;
    }
    db_finalize(&q);
    if( n>0 ){
      fossil_fatal("check-ins above are derived from check-ins in the bundle.");
    }
  }

  /* Find all files associated with those check-ins that are used
  ** nowhere else. */
  find_checkin_associates("ok", 1);

  /* Check to see if any associated files are not in the bundle.  Issue
  ** an error if there are any, unless --force is used.
  */
  if( !bForce ){
    db_multi_exec(
       "CREATE TEMP TABLE err1(rid INTEGER PRIMARY KEY);"
       "INSERT INTO err1 "
       " SELECT blob.rid FROM ok CROSS JOIN blob"
       "     WHERE blob.rid=ok.rid"
       "       AND blob.uuid NOT IN (SELECT uuid FROM bblob);"
    );
    if( db_changes() ){
      describe_artifacts_to_stdout("IN err1", 0);
      fossil_fatal("artifacts above associated with bundle check-ins "
                   " are not in the bundle");
    }else{
      db_multi_exec("DROP TABLE err1;");
    }
  }

  if( bTest ){
    describe_artifacts_to_stdout(
      "IN (SELECT blob.rid FROM ok, blob, bblob"
      "     WHERE blob.rid=ok.rid AND blob.uuid=bblob.uuid)",
      "Purged artifacts found in the bundle:");
    describe_artifacts_to_stdout(
      "IN (SELECT blob.rid FROM ok, blob"
      "     WHERE blob.rid=ok.rid "
      "       AND blob.uuid NOT IN (SELECT uuid FROM bblob))",
      "Purged artifacts NOT in the bundle:");
    describe_artifacts_to_stdout(
      "IN (SELECT blob.rid FROM bblob, blob"
      "     WHERE blob.uuid=bblob.uuid "
      "       AND blob.rid NOT IN ok)",
      "Artifacts in the bundle but not purged:");
  }else{
    purge_artifact_list("ok",0,0);
  }
  db_end_transaction(0);
}

/*
** COMMAND: bundle
**
** Usage: %fossil bundle SUBCOMMAND ARGS...
**
**   fossil bundle append BUNDLE FILE...
**
**      Add files named on the command line to BUNDLE.  This subcommand has
**      little practical use and is mostly intended for testing.
**
**   fossil bundle cat BUNDLE UUID...
**
**      Extract one or more artifacts from the bundle and write them
**      consecutively on standard output.  This subcommand was designed
**      for testing and introspection of bundles and is not something
**      commonly used.
**
**   fossil bundle export BUNDLE ?OPTIONS?
**
**      Generate a new bundle, in the file named BUNDLE, that contains a
**      subset of the check-ins in the repository (usually a single branch)
**      described by the --branch, --from, --to, and/or --checkin options,
**      at least one of which is required.  If BUNDLE already exists, the
**      specified content is added to the bundle.
**
**         --branch BRANCH            Package all check-ins on BRANCH.
**         --from TAG1 --to TAG2      Package check-ins between TAG1 and TAG2.
**         --checkin TAG              Package the single check-in TAG
**         --standalone               Do no use delta-encoding against
**                                      artifacts not in the bundle
**
**   fossil bundle extend BUNDLE
**
**      The BUNDLE must already exist.  This subcommand adds to the bundle
**      any check-ins that are descendants of check-ins already in the bundle,
**      and any tags that apply to artifacts in the bundle.
**
**   fossil bundle import BUNDLE ?--publish?
**
**      Import all content from BUNDLE into the repository.  By default, the
**      imported files are private and will not sync.  Use the --publish
**      option to make the import public.
**
**   fossil bundle ls BUNDLE
**
**      List the contents of BUNDLE on standard output
**
**   fossil bundle purge BUNDLE
**
**      Remove from the repository all files that are used exclusively
**      by check-ins in BUNDLE.  This has the effect of undoing a
**      "fossil bundle import".
**
** SUMMARY:
**   fossil bundle append BUNDLE FILE...              Add files to BUNDLE
**   fossil bundle cat BUNDLE UUID...                 Extract file from BUNDLE
**   fossil bundle export BUNDLE ?OPTIONS?            Create a new BUNDLE
**          --branch BRANCH --from TAG1 --to TAG2       Check-ins to include
**          --checkin TAG                               Use only check-in TAG
**          --standalone                                Omit dependencies
**   fossil bundle extend BUNDLE                      Update with newer content
**   fossil bundle import BUNDLE ?OPTIONS?            Import a bundle
**          --publish                                   Publish the import
**          --force                                     Cross-repo import
**   fossil bundle ls BUNDLE                          List content of a bundle
**   fossil bundle purge BUNDLE                       Undo an import
**
** See also: publish
*/
void bundle_cmd(void){
  const char *zSubcmd;
  int n;
  if( g.argc<4 ) usage("SUBCOMMAND BUNDLE ?OPTIONS?");
  zSubcmd = g.argv[2];
  db_find_and_open_repository(0,0);
  n = (int)strlen(zSubcmd);
  if( strncmp(zSubcmd, "append", n)==0 ){
    bundle_append_cmd();
  }else if( strncmp(zSubcmd, "cat", n)==0 ){
    bundle_cat_cmd();
  }else if( strncmp(zSubcmd, "export", n)==0 ){
    bundle_export_cmd();
  }else if( strncmp(zSubcmd, "extend", n)==0 ){
    fossil_fatal("not yet implemented");
  }else if( strncmp(zSubcmd, "import", n)==0 ){
    bundle_import_cmd();
  }else if( strncmp(zSubcmd, "ls", n)==0 ){
    bundle_ls_cmd();
  }else if( strncmp(zSubcmd, "purge", n)==0 ){
    bundle_purge_cmd();
  }else{
    fossil_fatal("unknown subcommand for bundle: %s", zSubcmd);
  }
}
