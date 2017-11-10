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
** This file contains code used to rebuild the database.
*/
#include "config.h"
#include "rebuild.h"
#include <assert.h>
#include <errno.h>

/*
** Update the schema as necessary
*/
static void rebuild_update_schema(void){
  /* Verify that the PLINK table has a new column added by the
  ** 2014-11-28 schema change.  Create it if necessary.  This code
  ** can be removed in the future, once all users have upgraded to the
  ** 2014-11-28 or later schema.
  */
  if( !db_table_has_column("repository","plink","baseid") ){
    db_multi_exec(
      "ALTER TABLE repository.plink ADD COLUMN baseid;"
    );
  }

  /* Verify that the MLINK table has the newer columns added by the
  ** 2015-01-24 schema change.  Create them if necessary.  This code
  ** can be removed in the future, once all users have upgraded to the
  ** 2015-01-24 or later schema.
  */
  if( !db_table_has_column("repository","mlink","isaux") ){
    db_begin_transaction();
    db_multi_exec(
      "ALTER TABLE repository.mlink ADD COLUMN pmid INTEGER DEFAULT 0;"
      "ALTER TABLE repository.mlink ADD COLUMN isaux BOOLEAN DEFAULT 0;"
    );
    db_end_transaction(0);
  }

  /* Add the user.mtime column if it is missing. (2011-04-27)
  */
  if( !db_table_has_column("repository", "user", "mtime") ){
    db_multi_exec(
      "CREATE TEMP TABLE temp_user AS SELECT * FROM user;"
      "DROP TABLE user;"
      "CREATE TABLE user(\n"
      "  uid INTEGER PRIMARY KEY,\n"
      "  login TEXT UNIQUE,\n"
      "  pw TEXT,\n"
      "  cap TEXT,\n"
      "  cookie TEXT,\n"
      "  ipaddr TEXT,\n"
      "  cexpire DATETIME,\n"
      "  info TEXT,\n"
      "  mtime DATE,\n"
      "  photo BLOB\n"
      ");"
      "INSERT OR IGNORE INTO user"
        " SELECT uid, login, pw, cap, cookie,"
               " ipaddr, cexpire, info, now(), photo FROM temp_user;"
      "DROP TABLE temp_user;"
    );
  }

  /* Add the config.mtime column if it is missing.  (2011-04-27)
  */
  if( !db_table_has_column("repository", "config", "mtime") ){
    db_multi_exec(
      "ALTER TABLE config ADD COLUMN mtime INTEGER;"
      "UPDATE config SET mtime=now();"
    );
  }

  /* Add the shun.mtime and shun.scom columns if they are missing.
  ** (2011-04-27)
  */
  if( !db_table_has_column("repository", "shun", "mtime") ){
    db_multi_exec(
      "ALTER TABLE shun ADD COLUMN mtime INTEGER;"
      "ALTER TABLE shun ADD COLUMN scom TEXT;"
      "UPDATE shun SET mtime=now();"
    );
  }

  /* Add the reportfmt.mtime column if it is missing. (2011-04-27)
  */
  if( !db_table_has_column("repository", "reportfmt", "mtime") ){
    static const char zCreateReportFmtTable[] =
    @ -- An entry in this table describes a database query that generates a
    @ -- table of tickets.
    @ --
    @ CREATE TABLE IF NOT EXISTS reportfmt(
    @    rn INTEGER PRIMARY KEY,  -- Report number
    @    owner TEXT,              -- Owner of this report format (not used)
    @    title TEXT UNIQUE,       -- Title of this report
    @    mtime INTEGER,           -- Time last modified.  Seconds since 1970
    @    cols TEXT,               -- A color-key specification
    @    sqlcode TEXT             -- An SQL SELECT statement for this report
    @ );
    ;
    db_multi_exec(
      "CREATE TEMP TABLE old_fmt AS SELECT * FROM reportfmt;"
      "DROP TABLE reportfmt;"
    );
    db_multi_exec("%s", zCreateReportFmtTable/*safe-for-%s*/);
    db_multi_exec(
      "INSERT OR IGNORE INTO reportfmt(rn,owner,title,cols,sqlcode,mtime)"
        " SELECT rn, owner, title, cols, sqlcode, now() FROM old_fmt;"
      "INSERT OR IGNORE INTO reportfmt(rn,owner,title,cols,sqlcode,mtime)"
        " SELECT rn, owner, title || ' (' || rn || ')', cols, sqlcode, now()"
        "   FROM old_fmt;"
    );
  }

  /* Add the concealed.mtime column if it is missing. (2011-04-27)
  */
  if( !db_table_has_column("repository", "concealed", "mtime") ){
    db_multi_exec(
      "ALTER TABLE concealed ADD COLUMN mtime INTEGER;"
      "UPDATE concealed SET mtime=now();"
    );
  }

  /* Do the fossil-2.0 updates to the schema.  (2017-02-28)
  */
  rebuild_schema_update_2_0();
}

/*
** Update the repository schema for Fossil version 2.0.  (2017-02-28)
**   (1) Change the CHECK constraint on BLOB.UUID so that the length
**       is greater than or equal to 40, not exactly equal to 40.
*/
void rebuild_schema_update_2_0(void){
  char *z = db_text(0, "SELECT sql FROM repository.sqlite_master WHERE name='blob'");
  if( z ){
    /* Search for:  length(uuid)==40
    **              0123456789 12345   */
    int i;
    for(i=10; z[i]; i++){
      if( z[i]=='=' && strncmp(&z[i-6],"(uuid)==40",10)==0 ){
        z[i] = '>';
        db_multi_exec(
           "PRAGMA writable_schema=ON;"
           "UPDATE repository.sqlite_master SET sql=%Q WHERE name LIKE 'blob';"
           "PRAGMA writable_schema=OFF;",
           z
        );
        break;
      }
    }
    fossil_free(z);
  }
}

/*
** Variables used to store state information about an on-going "rebuild"
** or "deconstruct".
*/
static int totalSize;       /* Total number of artifacts to process */
static int processCnt;      /* Number processed so far */
static int ttyOutput;       /* Do progress output */
static Bag bagDone;         /* Bag of records rebuilt */

static char *zFNameFormat;  /* Format string for filenames on deconstruct */
static int prefixLength;    /* Length of directory prefix for deconstruct */


/*
** Draw the percent-complete message.
** The input is actually the permill complete.
*/
static void percent_complete(int permill){
  static int lastOutput = -1;
  if( permill>lastOutput ){
    fossil_print("  %d.%d%% complete...\r", permill/10, permill%10);
    fflush(stdout);
    lastOutput = permill;
  }
}


/*
** Called after each artifact is processed
*/
static void rebuild_step_done(int rid){
  /* assert( bag_find(&bagDone, rid)==0 ); */
  bag_insert(&bagDone, rid);
  if( ttyOutput ){
    processCnt++;
    if (!g.fQuiet && totalSize>0) {
      percent_complete((processCnt*1000)/totalSize);
    }
  }
}

/*
** Rebuild cross-referencing information for the artifact
** rid with content pBase and all of its descendants.  This
** routine clears the content buffer before returning.
**
** If the zFNameFormat variable is set, then this routine is
** called to run "fossil deconstruct" instead of the usual
** "fossil rebuild".  In that case, instead of rebuilding the
** cross-referencing information, write the file content out
** to the appropriate directory.
**
** In both cases, this routine automatically recurses to process
** other artifacts that are deltas off of the current artifact.
** This is the most efficient way to extract all of the original
** artifact content from the Fossil repository.
*/
static void rebuild_step(int rid, int size, Blob *pBase){
  static Stmt q1;
  Bag children;
  Blob copy;
  Blob *pUse;
  int nChild, i, cid;

  while( rid>0 ){

    /* Fix up the "blob.size" field if needed. */
    if( size!=blob_size(pBase) ){
      db_multi_exec(
         "UPDATE blob SET size=%d WHERE rid=%d", blob_size(pBase), rid
      );
    }

    /* Find all children of artifact rid */
    db_static_prepare(&q1, "SELECT rid FROM delta WHERE srcid=:rid");
    db_bind_int(&q1, ":rid", rid);
    bag_init(&children);
    while( db_step(&q1)==SQLITE_ROW ){
      int cid = db_column_int(&q1, 0);
      if( !bag_find(&bagDone, cid) ){
        bag_insert(&children, cid);
      }
    }
    nChild = bag_count(&children);
    db_reset(&q1);

    /* Crosslink the artifact */
    if( nChild==0 ){
      pUse = pBase;
    }else{
      blob_copy(&copy, pBase);
      pUse = &copy;
    }
    if( zFNameFormat==0 ){
      /* We are doing "fossil rebuild" */
      manifest_crosslink(rid, pUse, MC_NONE);
    }else{
      /* We are doing "fossil deconstruct" */
      char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      char *zFile = mprintf(zFNameFormat /*works-like:"%s:%s"*/,
                            zUuid, zUuid+prefixLength);
      blob_write_to_file(pUse,zFile);
      free(zFile);
      free(zUuid);
      blob_reset(pUse);
    }
    assert( blob_is_reset(pUse) );
    rebuild_step_done(rid);

    /* Call all children recursively */
    rid = 0;
    for(cid=bag_first(&children), i=1; cid; cid=bag_next(&children, cid), i++){
      static Stmt q2;
      int sz;
      db_static_prepare(&q2, "SELECT content, size FROM blob WHERE rid=:rid");
      db_bind_int(&q2, ":rid", cid);
      if( db_step(&q2)==SQLITE_ROW && (sz = db_column_int(&q2,1))>=0 ){
        Blob delta, next;
        db_ephemeral_blob(&q2, 0, &delta);
        blob_uncompress(&delta, &delta);
        blob_delta_apply(pBase, &delta, &next);
        blob_reset(&delta);
        db_reset(&q2);
        if( i<nChild ){
          rebuild_step(cid, sz, &next);
        }else{
          /* Tail recursion */
          rid = cid;
          size = sz;
          blob_reset(pBase);
          *pBase = next;
        }
      }else{
        db_reset(&q2);
        blob_reset(pBase);
      }
    }
    bag_clear(&children);
  }
}

/*
** Check to see if the "sym-trunk" tag exists.  If not, create it
** and attach it to the very first check-in.
*/
static void rebuild_tag_trunk(void){
  int tagid = db_int(0, "SELECT 1 FROM tag WHERE tagname='sym-trunk'");
  int rid;
  char *zUuid;

  if( tagid>0 ) return;
  rid = db_int(0, "SELECT pid FROM plink AS x WHERE NOT EXISTS("
                  "  SELECT 1 FROM plink WHERE cid=x.pid)");
  if( rid==0 ) return;

  /* Add the trunk tag to the root of the whole tree */
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( zUuid==0 ) return;
  tag_add_artifact("sym-", "trunk", zUuid, 0, 2, 0, 0);
  tag_add_artifact("", "branch", zUuid, "trunk", 2, 0, 0);
}

/*
** Core function to rebuild the information in the derived tables of a
** fossil repository from the blobs. This function is shared between
** 'rebuild_database' ('rebuild') and 'reconstruct_cmd'
** ('reconstruct'), both of which have to regenerate this information
** from scratch.
**
** If the randomize parameter is true, then the BLOBs are deliberately
** extracted in a random order.  This feature is used to test the
** ability of fossil to accept records in any order and still
** construct a sane repository.
*/
int rebuild_db(int randomize, int doOut, int doClustering){
  Stmt s, q;
  int errCnt = 0;
  int incrSize;
  Blob sql;

  bag_init(&bagDone);
  ttyOutput = doOut;
  processCnt = 0;
  if (ttyOutput && !g.fQuiet) {
    percent_complete(0);
  }
  rebuild_update_schema();
  blob_init(&sql, 0, 0);
  db_prepare(&q,
     "SELECT name FROM sqlite_master /*scan*/"
     " WHERE type='table'"
     " AND name NOT IN ('admin_log', 'blob','delta','rcvfrom','user','alias',"
                       "'config','shun','private','reportfmt',"
                       "'concealed','accesslog','modreq',"
                       "'purgeevent','purgeitem','unversioned')"
     " AND name NOT GLOB 'sqlite_*'"
     " AND name NOT GLOB 'fx_*'"
  );
  while( db_step(&q)==SQLITE_ROW ){
    blob_appendf(&sql, "DROP TABLE IF EXISTS \"%w\";\n", db_column_text(&q,0));
  }
  db_finalize(&q);
  db_multi_exec("%s", blob_str(&sql)/*safe-for-%s*/);
  blob_reset(&sql);
  db_multi_exec("%s", zRepositorySchema2/*safe-for-%s*/);
  ticket_create_table(0);
  shun_artifacts();

  db_multi_exec(
     "INSERT INTO unclustered"
     " SELECT rid FROM blob EXCEPT SELECT rid FROM private"
  );
  db_multi_exec(
     "DELETE FROM unclustered"
     " WHERE rid IN (SELECT rid FROM shun JOIN blob USING(uuid))"
  );
  db_multi_exec(
    "DELETE FROM config WHERE name IN ('remote-code', 'remote-maxid')"
  );
  db_multi_exec(
    "UPDATE user SET mtime=strftime('%%s','now') WHERE mtime IS NULL"
  );

  /* The following should be count(*) instead of max(rid). max(rid) is
  ** an adequate approximation, however, and is much faster for large
  ** repositories. */
  totalSize = db_int(0, "SELECT max(rid) FROM blob");
  incrSize = totalSize/100;
  totalSize += incrSize*2;
  db_prepare(&s,
     "SELECT rid, size FROM blob /*scan*/"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
     "   AND NOT EXISTS(SELECT 1 FROM delta WHERE rid=blob.rid)"
  );
  manifest_crosslink_begin();
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      Blob content;
      content_get(rid, &content);
      rebuild_step(rid, size, &content);
    }
  }
  db_finalize(&s);
  db_prepare(&s,
     "SELECT rid, size FROM blob"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
  );
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      if( !bag_find(&bagDone, rid) ){
        Blob content;
        content_get(rid, &content);
        rebuild_step(rid, size, &content);
      }
    }else{
      db_multi_exec("INSERT OR IGNORE INTO phantom VALUES(%d)", rid);
      rebuild_step_done(rid);
    }
  }
  db_finalize(&s);
  manifest_crosslink_end(MC_NONE);
  rebuild_tag_trunk();
  if( ttyOutput && !g.fQuiet && totalSize>0 ){
    processCnt += incrSize;
    percent_complete((processCnt*1000)/totalSize);
  }
  if( doClustering ) create_cluster();
  if( ttyOutput && !g.fQuiet && totalSize>0 ){
    processCnt += incrSize;
    percent_complete((processCnt*1000)/totalSize);
  }
  if(!g.fQuiet && ttyOutput ){
    percent_complete(1000);
    fossil_print("\n");
  }
  return errCnt;
}

/*
** Number of neighbors to search
*/
#define N_NEIGHBOR 5

/*
** Attempt to convert more full-text blobs into delta-blobs for
** storage efficiency.
*/
void extra_deltification(void){
  Stmt q;
  int aPrev[N_NEIGHBOR];
  int nPrev;
  int rid;
  int prevfnid, fnid;
  db_begin_transaction();

  /* Look for manifests that have not been deltaed and try to make them
  ** children of one of the 5 chronologically subsequent check-ins
  */
  db_prepare(&q,
     "SELECT rid FROM event, blob"
     " WHERE blob.rid=event.objid"
     "   AND event.type='ci'"
     "   AND NOT EXISTS(SELECT 1 FROM delta WHERE rid=blob.rid)"
     " ORDER BY event.mtime DESC"
  );
  nPrev = 0;
  while( db_step(&q)==SQLITE_ROW ){
    rid = db_column_int(&q, 0);
    if( nPrev>0 ){
      content_deltify(rid, aPrev, nPrev, 0);
    }
    if( nPrev<N_NEIGHBOR ){
      aPrev[nPrev++] = rid;
    }else{
      int i;
      for(i=0; i<N_NEIGHBOR-1; i++) aPrev[i] = aPrev[i+1];
      aPrev[N_NEIGHBOR-1] = rid;
    }
  }
  db_finalize(&q);

  /* For individual files that have not been deltaed, try to find
  ** a parent which is an undeltaed file with the same name in a
  ** more recent branch.
  */
  db_prepare(&q,
     "SELECT DISTINCT blob.rid, mlink.fnid FROM blob, mlink, plink"
     " WHERE NOT EXISTS(SELECT 1 FROM delta WHERE rid=blob.rid)"
     "   AND mlink.fid=blob.rid"
     "   AND mlink.mid=plink.cid"
     "   AND plink.cid=mlink.mid"
     " ORDER BY mlink.fnid, plink.mtime DESC"
  );
  prevfnid = 0;
  while( db_step(&q)==SQLITE_ROW ){
    rid = db_column_int(&q, 0);
    fnid = db_column_int(&q, 1);
    if( fnid!=prevfnid ) nPrev = 0;
    prevfnid = fnid;
    if( nPrev>0 ){
      content_deltify(rid, aPrev, nPrev, 0);
    }
    if( nPrev<N_NEIGHBOR ){
      aPrev[nPrev++] = rid;
    }else{
      int i;
      for(i=0; i<N_NEIGHBOR-1; i++) aPrev[i] = aPrev[i+1];
      aPrev[N_NEIGHBOR-1] = rid;
    }
  }
  db_finalize(&q);

  db_end_transaction(0);
}


/* Reconstruct the private table.  The private table contains the rid
** of every manifest that is tagged with "private" and every file that
** is not used by a manifest that is not private.
*/
static void reconstruct_private_table(void){
  db_multi_exec(
    "CREATE TEMP TABLE private_ckin(rid INTEGER PRIMARY KEY);"
    "INSERT INTO private_ckin "
        " SELECT rid FROM tagxref WHERE tagid=%d AND tagtype>0;"
    "INSERT OR IGNORE INTO private"
        " SELECT fid FROM mlink"
        " EXCEPT SELECT fid FROM mlink WHERE mid NOT IN private_ckin;"
    "INSERT OR IGNORE INTO private SELECT rid FROM private_ckin;"
    "DROP TABLE private_ckin;", TAG_PRIVATE
  );
  fix_private_blob_dependencies(0);
}


/*
** COMMAND: rebuild
**
** Usage: %fossil rebuild ?REPOSITORY? ?OPTIONS?
**
** Reconstruct the named repository database from the core
** records.  Run this command after updating the fossil
** executable in a way that changes the database schema.
**
** Options:
**   --analyze         Run ANALYZE on the database after rebuilding
**   --cluster         Compute clusters for unclustered artifacts
**   --compress        Strive to make the database as small as possible
**   --compress-only   Skip the rebuilding step. Do --compress only
**   --deanalyze       Remove ANALYZE tables from the database
**   --force           Force the rebuild to complete even if errors are seen
**   --ifneeded        Only do the rebuild if it would change the schema version
**   --index           Always add in the full-text search index
**   --noverify        Skip the verification of changes to the BLOB table
**   --noindex         Always omit the full-text search index
**   --pagesize N      Set the database pagesize to N. (512..65536 and power of 2)
**   --quiet           Only show output if there are errors
**   --randomize       Scan artifacts in a random order
**   --stats           Show artifact statistics after rebuilding
**   --vacuum          Run VACUUM on the database after rebuilding
**   --wal             Set Write-Ahead-Log journalling mode on the database
**
** See also: deconstruct, reconstruct
*/
void rebuild_database(void){
  int forceFlag;
  int randomizeFlag;
  int errCnt = 0;
  int omitVerify;
  int doClustering;
  const char *zPagesize;
  int newPagesize = 0;
  int activateWal;
  int runVacuum;
  int runDeanalyze;
  int runAnalyze;
  int runCompress;
  int showStats;
  int runReindex;
  int optNoIndex;
  int optIndex;
  int optIfNeeded;
  int compressOnlyFlag;

  omitVerify = find_option("noverify",0,0)!=0;
  forceFlag = find_option("force","f",0)!=0;
  randomizeFlag = find_option("randomize", 0, 0)!=0;
  doClustering = find_option("cluster", 0, 0)!=0;
  runVacuum = find_option("vacuum",0,0)!=0;
  runDeanalyze = find_option("deanalyze",0,0)!=0;
  runAnalyze = find_option("analyze",0,0)!=0;
  runCompress = find_option("compress",0,0)!=0;
  zPagesize = find_option("pagesize",0,1);
  showStats = find_option("stats",0,0)!=0;
  optIndex = find_option("index",0,0)!=0;
  optNoIndex = find_option("noindex",0,0)!=0;
  optIfNeeded = find_option("ifneeded",0,0)!=0;
  compressOnlyFlag = find_option("compress-only",0,0)!=0;
  if( compressOnlyFlag ) runCompress = runVacuum = 1;
  if( zPagesize ){
    newPagesize = atoi(zPagesize);
    if( newPagesize<512 || newPagesize>65536
        || (newPagesize&(newPagesize-1))!=0
    ){
      fossil_fatal("page size must be a power of two between 512 and 65536");
    }
  }
  activateWal = find_option("wal",0,0)!=0;
  if( g.argc==3 ){
    db_open_repository(g.argv[2]);
  }else{
    db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
    if( g.argc!=2 ){
      usage("?REPOSITORY-FILENAME?");
    }
    db_close(1);
    db_open_repository(g.zRepositoryName);
  }
  runReindex = search_index_exists() && !compressOnlyFlag;
  if( optIndex ) runReindex = 1;
  if( optNoIndex ) runReindex = 0;
  if( optIfNeeded && fossil_strcmp(db_get("aux-schema",""),AUX_SCHEMA_MAX)==0 ){
    return;
  }

  /* We should be done with options.. */
  verify_all_options();

  db_begin_transaction();
  if( !compressOnlyFlag ){
    search_drop_index();
    ttyOutput = 1;
    errCnt = rebuild_db(randomizeFlag, 1, doClustering);
    reconstruct_private_table();
  }
  db_multi_exec(
    "REPLACE INTO config(name,value,mtime) VALUES('content-schema',%Q,now());"
    "REPLACE INTO config(name,value,mtime) VALUES('aux-schema',%Q,now());"
    "REPLACE INTO config(name,value,mtime) VALUES('rebuilt',%Q,now());",
    CONTENT_SCHEMA, AUX_SCHEMA_MAX, get_version()
  );
  if( errCnt && !forceFlag ){
    fossil_print(
      "%d errors. Rolling back changes. Use --force to force a commit.\n",
      errCnt
    );
    db_end_transaction(1);
  }else{
    if( runCompress ){
      fossil_print("Extra delta compression... "); fflush(stdout);
      extra_deltification();
      runVacuum = 1;
    }
    if( omitVerify ) verify_cancel();
    db_end_transaction(0);
    if( runCompress ) fossil_print("done\n");
    db_close(0);
    db_open_repository(g.zRepositoryName);
    if( newPagesize ){
      db_multi_exec("PRAGMA page_size=%d", newPagesize);
      runVacuum = 1;
    }
    if( runDeanalyze ){
      db_multi_exec("DROP TABLE IF EXISTS sqlite_stat1;"
                    "DROP TABLE IF EXISTS sqlite_stat3;"
                    "DROP TABLE IF EXISTS sqlite_stat4;");
    }
    if( runAnalyze ){
      fossil_print("Analyzing the database... "); fflush(stdout);
      db_multi_exec("ANALYZE;");
      fossil_print("done\n");
    }
    if( runVacuum ){
      fossil_print("Vacuuming the database... "); fflush(stdout);
      db_multi_exec("VACUUM");
      fossil_print("done\n");
    }
    if( activateWal ){
      db_multi_exec("PRAGMA journal_mode=WAL;");
    }
  }
  if( runReindex ) search_rebuild_index();
  if( showStats ){
    static const struct { int idx; const char *zLabel; } aStat[] = {
       { CFTYPE_ANY,       "Artifacts:" },
       { CFTYPE_MANIFEST,  "Manifests:" },
       { CFTYPE_CLUSTER,   "Clusters:" },
       { CFTYPE_CONTROL,   "Tags:" },
       { CFTYPE_WIKI,      "Wikis:" },
       { CFTYPE_TICKET,    "Tickets:" },
       { CFTYPE_ATTACHMENT,"Attachments:" },
       { CFTYPE_EVENT,     "Events:" },
    };
    int i;
    int subtotal = 0;
    for(i=0; i<count(aStat); i++){
      int k = aStat[i].idx;
      fossil_print("%-15s %6d\n", aStat[i].zLabel, g.parseCnt[k]);
      if( k>0 ) subtotal += g.parseCnt[k];
    }
    fossil_print("%-15s %6d\n", "Other:", g.parseCnt[CFTYPE_ANY] - subtotal);
  }
}

/*
** COMMAND: test-detach
**
** Usage: %fossil test-detach  ?REPOSITORY?
**
** Change the project-code and make other changes in order to prevent
** the repository from ever again pushing or pulling to other
** repositories.  Used to create a "test" repository for development
** testing by cloning a working project repository.
*/
void test_detach_cmd(void){
  db_find_and_open_repository(0, 2);
  db_begin_transaction();
  db_multi_exec(
    "DELETE FROM config WHERE name='last-sync-url';"
    "UPDATE config SET value=lower(hex(randomblob(20)))"
    " WHERE name='project-code';"
    "UPDATE config SET value='detached-' || value"
    " WHERE name='project-name' AND value NOT GLOB 'detached-*';"
  );
  db_end_transaction(0);
}

/*
** COMMAND: test-create-clusters
**
** Create clusters for all unclustered artifacts if the number of unclustered
** artifacts exceeds the current clustering threshold.
*/
void test_createcluster_cmd(void){
  if( g.argc==3 ){
    db_open_repository(g.argv[2]);
  }else{
    db_find_and_open_repository(0, 0);
    if( g.argc!=2 ){
      usage("?REPOSITORY-FILENAME?");
    }
    db_close(1);
    db_open_repository(g.zRepositoryName);
  }
  db_begin_transaction();
  create_cluster();
  db_end_transaction(0);
}

/*
** COMMAND: test-clusters
**
** Verify that all non-private and non-shunned artifacts are accessible
** through the cluster chain.
*/
void test_clusters_cmd(void){
  Bag pending;
  Stmt q;
  int n;

  db_find_and_open_repository(0, 2);
  bag_init(&pending);
  db_multi_exec(
    "CREATE TEMP TABLE xdone(x INTEGER PRIMARY KEY);"
    "INSERT INTO xdone SELECT rid FROM unclustered;"
    "INSERT OR IGNORE INTO xdone SELECT rid FROM private;"
    "INSERT OR IGNORE INTO xdone"
         " SELECT blob.rid FROM shun JOIN blob USING(uuid);"
  );
  db_prepare(&q,
    "SELECT rid FROM unclustered WHERE rid IN"
    " (SELECT rid FROM tagxref WHERE tagid=%d)", TAG_CLUSTER
  );
  while( db_step(&q)==SQLITE_ROW ){
    bag_insert(&pending, db_column_int(&q, 0));
  }
  db_finalize(&q);
  while( bag_count(&pending)>0 ){
    Manifest *p;
    int rid = bag_first(&pending);
    int i;

    bag_remove(&pending, rid);
    p = manifest_get(rid, CFTYPE_CLUSTER, 0);
    if( p==0 ){
      fossil_fatal("bad cluster: rid=%d", rid);
    }
    for(i=0; i<p->nCChild; i++){
      const char *zUuid = p->azCChild[i];
      int crid = name_to_rid(zUuid);
      if( crid==0 ){
         fossil_warning("cluster (rid=%d) references unknown artifact %s",
                        rid, zUuid);
         continue;
      }
      db_multi_exec("INSERT OR IGNORE INTO xdone VALUES(%d)", crid);
      if( db_exists("SELECT 1 FROM tagxref WHERE tagid=%d AND rid=%d",
                    TAG_CLUSTER, crid) ){
        bag_insert(&pending, crid);
      }
    }
    manifest_destroy(p);
  }
  n = db_int(0, "SELECT count(*) FROM /*scan*/"
                "  (SELECT rid FROM blob EXCEPT SELECT x FROM xdone)");
  if( n==0 ){
    fossil_print("all artifacts reachable through clusters\n");
  }else{
    fossil_print("%d unreachable artifacts:\n", n);
    db_prepare(&q, "SELECT rid, uuid FROM blob WHERE rid NOT IN xdone");
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("  %3d %s\n", db_column_int(&q,0), db_column_text(&q,1));
    }
    db_finalize(&q);
  }
}

/*
** COMMAND: scrub*
**
** Usage: %fossil scrub ?OPTIONS? ?REPOSITORY?
**
** The command removes sensitive information (such as passwords) from a
** repository so that the repository can be sent to an untrusted reader.
**
** By default, only passwords are removed.  However, if the --verily option
** is added, then private branches, concealed email addresses, IP
** addresses of correspondents, and similar privacy-sensitive fields
** are also purged.  If the --private option is used, then only private
** branches are removed and all other information is left intact.
**
** This command permanently deletes the scrubbed information. THE EFFECTS
** OF THIS COMMAND ARE IRREVERSIBLE. USE WITH CAUTION!
**
** The user is prompted to confirm the scrub unless the --force option
** is used.
**
** Options:
**   --force     do not prompt for confirmation
**   --private   only private branches are removed from the repository
**   --verily    scrub real thoroughly (see above)
*/
void scrub_cmd(void){
  int bVerily = find_option("verily",0,0)!=0;
  int bForce = find_option("force", "f", 0)!=0;
  int privateOnly = find_option("private",0,0)!=0;
  int bNeedRebuild = 0;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 2);
  db_close(1);
  db_open_repository(g.zRepositoryName);

  /* We should be done with options.. */
  verify_all_options();

  if( !bForce ){
    Blob ans;
    char cReply;
    prompt_user(
         "Scrubbing the repository will permanently delete information.\n"
         "Changes cannot be undone.  Continue (y/N)? ", &ans);
    cReply = blob_str(&ans)[0];
    if( cReply!='y' && cReply!='Y' ){
      fossil_exit(1);
    }
  }
  db_begin_transaction();
  if( privateOnly || bVerily ){
    bNeedRebuild = db_exists("SELECT 1 FROM private");
    delete_private_content();
  }
  if( !privateOnly ){
    db_multi_exec(
      "UPDATE user SET pw='';"
      "DELETE FROM config WHERE name GLOB 'last-sync-*';"
      "DELETE FROM config WHERE name GLOB 'peer-*';"
      "DELETE FROM config WHERE name GLOB 'login-group-*';"
      "DELETE FROM config WHERE name GLOB 'skin:*';"
      "DELETE FROM config WHERE name GLOB 'subrepo:*';"
    );
    if( bVerily ){
      db_multi_exec(
        "DELETE FROM concealed;\n"
        "UPDATE rcvfrom SET ipaddr='unknown';\n"
        "DROP TABLE IF EXISTS accesslog;\n"
        "UPDATE user SET photo=NULL, info='';\n"
        "DROP TABLE IF EXISTS purgeevent;\n"
        "DROP TABLE IF EXISTS purgeitem;\n"
        "DROP TABLE IF EXISTS admin_log;\n"
        "DROP TABLE IF EXISTS vcache;\n"
      );
    }
  }
  if( !bNeedRebuild ){
    db_end_transaction(0);
    db_multi_exec("VACUUM;");
  }else{
    rebuild_db(0, 1, 0);
    db_end_transaction(0);
  }
}

/*
** Recursively read all files from the directory zPath and install
** every file read as a new artifact in the repository.
*/
void recon_read_dir(char *zPath){
  DIR *d;
  struct dirent *pEntry;
  Blob aContent; /* content of the just read artifact */
  static int nFileRead = 0;
  void *zUnicodePath;
  char *zUtf8Name;

  zUnicodePath = fossil_utf8_to_path(zPath, 1);
  d = opendir(zUnicodePath);
  if( d ){
    while( (pEntry=readdir(d))!=0 ){
      Blob path;
      char *zSubpath;

      if( pEntry->d_name[0]=='.' ){
        continue;
      }
      zUtf8Name = fossil_path_to_utf8(pEntry->d_name);
      zSubpath = mprintf("%s/%s", zPath, zUtf8Name);
      fossil_path_free(zUtf8Name);
#ifdef _DIRENT_HAVE_D_TYPE
      if( (pEntry->d_type==DT_UNKNOWN || pEntry->d_type==DT_LNK)
          ? (file_isdir(zSubpath)==1) : (pEntry->d_type==DT_DIR) )
#else
      if( file_isdir(zSubpath)==1 )
#endif
      {
        recon_read_dir(zSubpath);
      }else{
        blob_init(&path, 0, 0);
        blob_appendf(&path, "%s", zSubpath);
        if( blob_read_from_file(&aContent, blob_str(&path))==-1 ){
          fossil_fatal("some unknown error occurred while reading \"%s\"",
                       blob_str(&path));
        }
        content_put(&aContent);
        blob_reset(&path);
        blob_reset(&aContent);
        fossil_print("\r%d", ++nFileRead);
        fflush(stdout);
      }
      free(zSubpath);
    }
    closedir(d);
  }else {
    fossil_fatal("encountered error %d while trying to open \"%s\".",
                  errno, g.argv[3]);
  }
  fossil_path_free(zUnicodePath);
}

/*
** COMMAND: reconstruct*
**
** Usage: %fossil reconstruct FILENAME DIRECTORY
**
** This command studies the artifacts (files) in DIRECTORY and
** reconstructs the fossil record from them. It places the new
** fossil repository in FILENAME. Subdirectories are read, files
** with leading '.' in the filename are ignored.
**
** See also: deconstruct, rebuild
*/
void reconstruct_cmd(void) {
  char *zPassword;
  if( g.argc!=4 ){
    usage("FILENAME DIRECTORY");
  }
  if( file_isdir(g.argv[3])!=1 ){
    fossil_print("\"%s\" is not a directory\n\n", g.argv[3]);
    usage("FILENAME DIRECTORY");
  }
  db_create_repository(g.argv[2]);
  db_open_repository(g.argv[2]);

  /* We should be done with options.. */
  verify_all_options();

  db_open_config(0, 0);
  db_begin_transaction();
  db_initial_setup(0, 0, 0);

  fossil_print("Reading files from directory \"%s\"...\n", g.argv[3]);
  recon_read_dir(g.argv[3]);
  fossil_print("\nBuilding the Fossil repository...\n");

  rebuild_db(0, 1, 1);
  reconstruct_private_table();

  /* Skip the verify_before_commit() step on a reconstruct.  Most artifacts
  ** will have been changed and verification therefore takes a really, really
  ** long time.
  */
  verify_cancel();

  db_end_transaction(0);
  fossil_print("project-id: %s\n", db_get("project-code", 0));
  fossil_print("server-id: %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  fossil_print("admin-user: %s (initial password is \"%s\")\n", g.zLogin, zPassword);
}

/*
** COMMAND: deconstruct*
**
** Usage %fossil deconstruct ?OPTIONS? DESTINATION
**
**
** This command exports all artifacts of a given repository and
** writes all artifacts to the file system. The DESTINATION directory
** will be populated with subdirectories AA and files AA/BBBBBBBBB.., where
** AABBBBBBBBB.. is the 40+ character artifact ID, AA the first 2 characters.
** If -L|--prefixlength is given, the length (default 2) of the directory
** prefix can be set to 0,1,..,9 characters.
**
** Options:
**   -R|--repository REPOSITORY  deconstruct given REPOSITORY
**   -L|--prefixlength N         set the length of the names of the DESTINATION
**                               subdirectories to N
**   --private                   Include private artifacts.
**
** See also: rebuild, reconstruct
*/
void deconstruct_cmd(void){
  const char *zDestDir;
  const char *zPrefixOpt;
  Stmt        s;
  int privateFlag;

  /* get and check prefix length argument and build format string */
  zPrefixOpt=find_option("prefixlength","L",1);
  if( !zPrefixOpt ){
    prefixLength = 2;
  }else{
    if( zPrefixOpt[0]>='0' && zPrefixOpt[0]<='9' && !zPrefixOpt[1] ){
      prefixLength = (int)(*zPrefixOpt-'0');
    }else{
      fossil_fatal("N(%s) is not a valid prefix length!",zPrefixOpt);
    }
  }
  /* open repository and open query for all artifacts */
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  privateFlag = find_option("private",0,0)!=0;
  verify_all_options();
  /* check number of arguments */
  if( g.argc!=3 ){
    usage ("?OPTIONS? DESTINATION");
  }
  /* get and check argument destination directory */
  zDestDir = g.argv[g.argc-1];
  if( !*zDestDir  || !file_isdir(zDestDir)) {
    fossil_fatal("DESTINATION(%s) is not a directory!",zDestDir);
  }
#ifndef _WIN32
  if( file_access(zDestDir, W_OK) ){
    fossil_fatal("DESTINATION(%s) is not writeable!",zDestDir);
  }
#else
  /* write access on windows is not checked, errors will be
  ** detected on blob_write_to_file
  */
#endif
  if( prefixLength ){
    zFNameFormat = mprintf("%s/%%.%ds/%%s",zDestDir,prefixLength);
  }else{
    zFNameFormat = mprintf("%s/%%s",zDestDir);
  }

  bag_init(&bagDone);
  ttyOutput = 1;
  processCnt = 0;
  if (!g.fQuiet) {
    fossil_print("0 (0%%)...\r");
    fflush(stdout);
  }
  totalSize = db_int(0, "SELECT count(*) FROM blob");
  db_prepare(&s,
     "SELECT rid, size FROM blob /*scan*/"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
     "   AND NOT EXISTS(SELECT 1 FROM delta WHERE rid=blob.rid) %s",
     privateFlag==0 ? "AND rid NOT IN private" : ""
  );
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      Blob content;
      content_get(rid, &content);
      rebuild_step(rid, size, &content);
    }
  }
  db_finalize(&s);
  db_prepare(&s,
     "SELECT rid, size FROM blob"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid) %s",
     privateFlag==0 ? "AND rid NOT IN private" : ""
  );
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      if( !bag_find(&bagDone, rid) ){
        Blob content;
        content_get(rid, &content);
        rebuild_step(rid, size, &content);
      }
    }
  }
  db_finalize(&s);
  if(!g.fQuiet && ttyOutput ){
    fossil_print("\n");
  }

  /* free filename format string */
  free(zFNameFormat);
  zFNameFormat = 0;
}
