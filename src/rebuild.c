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
    db_unprotect(PROTECT_ALL);
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
    db_protect_pop();
  }

  /* Add the config.mtime column if it is missing.  (2011-04-27)
  */
  if( !db_table_has_column("repository", "config", "mtime") ){
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "ALTER TABLE config ADD COLUMN mtime INTEGER;"
      "UPDATE config SET mtime=now();"
    );
    db_protect_pop();
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

  /* Add the user.jx and reportfmt.jx columns if they are missing. (2022-11-18)
  */
  user_update_user_table();
  report_update_reportfmt_table();
}

/*
** Update the repository schema for Fossil version 2.0.  (2017-02-28)
**   (1) Change the CHECK constraint on BLOB.UUID so that the length
**       is greater than or equal to 40, not exactly equal to 40.
*/
void rebuild_schema_update_2_0(void){
  char *z = db_text(0, "SELECT sql FROM repository.sqlite_schema"
                       " WHERE name='blob'");
  if( z ){
    /* Search for:  length(uuid)==40
    **              0123456789 12345   */
    int i;
    for(i=10; z[i]; i++){
      if( z[i]=='=' && strncmp(&z[i-6],"(uuid)==40",10)==0 ){
        int rc = 0;
        z[i] = '>';
        sqlite3_db_config(g.db, SQLITE_DBCONFIG_DEFENSIVE, 0, &rc);
        db_multi_exec(
           "PRAGMA writable_schema=ON;"
           "UPDATE repository.sqlite_schema SET sql=%Q WHERE name LIKE 'blob';"
           "PRAGMA writable_schema=OFF;",
           z
        );
        sqlite3_db_config(g.db, SQLITE_DBCONFIG_DEFENSIVE, 1, &rc);
        break;
      }
    }
    fossil_free(z);
  }
  db_multi_exec(
    "CREATE VIEW IF NOT EXISTS "
    "  repository.artifact(rid,rcvid,size,atype,srcid,hash,content) AS "
    "    SELECT blob.rid,rcvid,size,1,srcid,uuid,content"
    "      FROM blob LEFT JOIN delta ON (blob.rid=delta.rid);"
  );
}

/*
** Variables used to store state information about an on-going "rebuild"
** or "deconstruct".
*/
static int totalSize;          /* Total number of artifacts to process */
static int processCnt;         /* Number processed so far */
static int ttyOutput;          /* Do progress output */
static Bag bagDone = Bag_INIT; /* Bag of records rebuilt */

static char *zFNameFormat;  /* Format string for filenames on deconstruct */
static int cchFNamePrefix;  /* Length of directory prefix in zFNameFormat */
static const char *zDestDir;/* Destination directory on deconstruct */
static int prefixLength;    /* Length of directory prefix for deconstruct */
static int fKeepRid1;       /* Flag to preserve RID=1 on de- and reconstruct */


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
** Frees rebuild-level cached state. Intended only to be called by the
** app-level atexit() handler.
*/
void rebuild_clear_cache(){
  bag_clear(&bagDone);
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
    if( size!=(int)blob_size(pBase) ){
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
      if( rid==1 && fKeepRid1!=0 ){
        char *zFnDotRid1 = mprintf("%s/.rid1", zDestDir);
        char *zFnRid1 = zFile + cchFNamePrefix + 1; /* Skip directory slash */
        Blob bFileContents = empty_blob;
        blob_appendf(&bFileContents,
          "# The file holding the artifact with RID=1\n"
          "%s\n", zFnRid1);
        blob_write_to_file(&bFileContents, zFnDotRid1);
        blob_reset(&bFileContents);
        free(zFnDotRid1);
      }
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
*/
int rebuild_db(int doOut, int doClustering){
  Stmt s, q;
  int errCnt = 0;
  int incrSize;
  Blob sql;

  bag_clear(&bagDone);
  ttyOutput = doOut;
  processCnt = 0;
  if (ttyOutput && !g.fQuiet) {
    percent_complete(0);
  }
  manifest_disable_event_triggers();
  rebuild_update_schema();
  blob_init(&sql, 0, 0);
  db_unprotect(PROTECT_ALL);
#ifndef SQLITE_PREPARE_DONT_LOG
  g.dbIgnoreErrors++;
#endif
  db_prepare(&q,
     "SELECT name FROM pragma_table_list /*scan*/"
     " WHERE schema='repository' AND type IN ('table','virtual')"
     " AND name NOT IN ('admin_log', 'blob','delta','rcvfrom','user','alias',"
                       "'config','shun','private','reportfmt',"
                       "'concealed','accesslog','modreq',"
                       "'purgeevent','purgeitem','unversioned',"
                       "'subscriber','pending_alert','chat')"
     " AND name NOT GLOB 'sqlite_*'"
     " AND name NOT GLOB 'fx_*';"
  );
  while( db_step(&q)==SQLITE_ROW ){
    blob_appendf(&sql, "DROP TABLE IF EXISTS \"%w\";\n", db_column_text(&q,0));
  }
  db_finalize(&q);
#ifndef SQLITE_PREPARE_DONT_LOG
  g.dbIgnoreErrors--;
#endif
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
  db_protect_pop();
  return errCnt;
}

/*
** Number of neighbors to search
*/
#define N_NEIGHBOR 5

/*
** Attempt to convert more full-text blobs into delta-blobs for
** storage efficiency.  Return the number of bytes of storage space
** saved.
*/
i64 extra_deltification(int *pnDelta){
  Stmt q;
  int aPrev[N_NEIGHBOR];
  int nPrev;
  int rid;
  int prevfnid, fnid;
  int nDelta = 0;
  i64 nByte = 0;
  int nSaved;
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
      nSaved = content_deltify(rid, aPrev, nPrev, 0);
      if( nSaved>0 ){
        nDelta++;
        nByte += nSaved;
      }
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
      nSaved = content_deltify(rid, aPrev, nPrev, 0);
      if( nSaved>0 ){
        nDelta++;
        nByte += nSaved;
      }
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
  if( pnDelta!=0 ) *pnDelta = nDelta;
  return nByte;
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
** COMMAND: repack
**
** Usage: %fossil repack ?REPOSITORY?
**
** Perform extra delta-compression to try to minimize the size of the
** repository.  This command is simply a short-hand for:
**
**     fossil rebuild --compress-only
**
** The name for this command is stolen from the "git repack" command that
** does approximately the same thing in Git.
*/
void repack_command(void){
  i64 nByte = 0;
  int nDelta = 0;
  int runVacuum = 0;
  verify_all_options();
  if( g.argc==3 ){
    db_open_repository(g.argv[2]);
  }else if( g.argc==2 ){
    db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
    if( g.argc!=2 ){
      usage("?REPOSITORY-FILENAME?");
    }
    db_close(1);
    db_open_repository(g.zRepositoryName);
  }else{
    usage("?REPOSITORY-FILENAME?");
  }
  db_unprotect(PROTECT_ALL);
  nByte = extra_deltification(&nDelta);
  if( nDelta>0 ){
    if( nDelta==1 ){
      fossil_print("1 new delta saves %,lld bytes\n", nByte);
    }else{
      fossil_print("%d new deltas save %,lld bytes\n", nDelta, nByte);
    }
    runVacuum = 1;
  }else{
    fossil_print("no new compression opportunities found\n");
    runVacuum = db_int(0, "PRAGMA repository.freelist_count")>0;
  }
  if( runVacuum ){
    fossil_print("Vacuuming the database... "); fflush(stdout);
    db_multi_exec("VACUUM");
    fossil_print("done\n");
  }
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
**   --force           Force the rebuild to complete even if errors are seen
**   --ifneeded        Only do the rebuild if it would change the schema version
**   --index           Always add in the full-text search index
**   --noverify        Skip the verification of changes to the BLOB table
**   --noindex         Always omit the full-text search index
**   --pagesize N      Set the database pagesize to N (512..65536, power of 2)
**   --quiet           Only show output if there are errors
**   --stats           Show artifact statistics after rebuilding
**   --vacuum          Run VACUUM on the database after rebuilding
**   --wal             Set Write-Ahead-Log journalling mode on the database
*/
void rebuild_database(void){
  int forceFlag;
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
  doClustering = find_option("cluster", 0, 0)!=0;
  runVacuum = find_option("vacuum",0,0)!=0;
  runDeanalyze = find_option("deanalyze",0,0)!=0; /* Deprecated */
  runAnalyze = find_option("analyze",0,0)!=0;
  runCompress = find_option("compress",0,0)!=0;
  zPagesize = find_option("pagesize",0,1);
  showStats = find_option("stats",0,0)!=0;
  optIndex = find_option("index",0,0)!=0;
  optNoIndex = find_option("noindex",0,0)!=0;
  optIfNeeded = find_option("ifneeded",0,0)!=0;
  compressOnlyFlag = find_option("compress-only",0,0)!=0;
  if( compressOnlyFlag ) runCompress = 1;
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
  db_unprotect(PROTECT_ALL);
  if( !compressOnlyFlag ){
    search_drop_index();
    ttyOutput = 1;
    errCnt = rebuild_db(1, doClustering);
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
      i64 nByte = 0;
      int nDelta = 0;
      fossil_print("Extra delta compression... "); fflush(stdout);
      nByte = extra_deltification(&nDelta);
      if( nDelta>0 ){
        if( nDelta==1 ){
          fossil_print("1 new delta saves %,lld bytes", nByte);
        }else{
          fossil_print("%d new deltas save %,lld bytes", nDelta, nByte);
        }
        runVacuum = 1;
      }else{
        fossil_print("none found");
      }
      fflush(stdout);
    }
    if( omitVerify ) verify_cancel();
    db_end_transaction(0);
    if( runCompress ) fossil_print("\n");
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
  db_protect_pop();
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
** COMMAND: detach*
**
** Usage: %fossil detach ?REPOSITORY?
**
** Change the project-code and make other changes to REPOSITORY so that
** it becomes a new and distinct child project.  After being detached,
** REPOSITORY will not longer be able to push and pull from other clones
** of the original project.  However REPOSITORY will still be able to pull
** from those other clones using the --from-parent-project option of the
** "fossil pull" command.
**
** This is an experts-only command. You should not use this command unless
** you fully understand what you are doing.
**
** The original use-case for this command was to create test repositories
** from real-world working repositories that could be safely altered by
** making strange commits or other changes, without having to worry that
** those test changes would leak back into the original project via an
** accidental auto-sync.
*/
void test_detach_cmd(void){
  const char *zXfer[] = {
     "project-name",  "parent-project-name",
     "project-code",  "parent-project-code",
     "last-sync-url", "parent-project-url",
     "last-sync-pw",  "parent-project-pw"
  };
  int i;
  Blob ans;
  char cReply;
  db_find_and_open_repository(0, 2);
  prompt_user("This change will be difficult to undo. Are you sure (y/N)? ",
              &ans);
  cReply = blob_str(&ans)[0];
  if( cReply!='y' && cReply!='Y' ) return;
  db_begin_transaction();
  db_unprotect(PROTECT_CONFIG);
  for(i=0; i<ArraySize(zXfer)-1; i+=2 ){
    db_multi_exec(
      "REPLACE INTO config(name,value,mtime)"
        " SELECT %Q, value, now() FROM config WHERE name=%Q",
      zXfer[i+1], zXfer[i]
    );
  }
  db_multi_exec(
    "DELETE FROM config WHERE name IN"
    "(WITH pattern(x) AS (VALUES"
    "  ('baseurl:*'),"
    "  ('cert:*'),"
    "  ('ckout:*'),"
    "  ('gitpush:*'),"
    "  ('http-auth:*'),"
    "  ('last-sync-*'),"
    "  ('link:*'),"
    "  ('login-group-*'),"
    "  ('peer-*'),"
    "  ('subrepo:*'),"
    "  ('sync-*'),"
    "  ('syncfrom:*'),"
    "  ('syncwith:*'),"
    "  ('ssl-*')"
    ") SELECT name FROM config, pattern WHERE name GLOB x);"
    "UPDATE config SET value=lower(hex(randomblob(20)))"
    " WHERE name='project-code';"
    "UPDATE config SET value='detached-' || value"
    " WHERE name='project-name' AND value NOT GLOB 'detached-*';"
  );
  db_protect_pop();
  db_end_transaction(0);
  fossil_print("New project code: %s\n", db_get("project-code",""));
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
**   --force     Do not prompt for confirmation
**   --private   Only private branches are removed from the repository
**   --verily    Scrub real thoroughly (see above)
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
    db_unprotect(PROTECT_ALL);
    db_multi_exec(
      "PRAGMA secure_delete=ON;"
      "UPDATE user SET pw='';"
      "DELETE FROM config WHERE name IN"
      "(WITH pattern(x) AS (VALUES"
      "  ('baseurl:*'),"
      "  ('cert:*'),"
      "  ('ckout:*'),"
      "  ('draft[1-9]-*'),"
      "  ('gitpush:*'),"
      "  ('http-auth:*'),"
      "  ('last-sync-*'),"
      "  ('link:*'),"
      "  ('login-group-*'),"
      "  ('parent-project-*'),"
      "  ('peer-*'),"
      "  ('skin:*'),"
      "  ('subrepo:*'),"
      "  ('sync-*'),"
      "  ('syncfrom:*'),"
      "  ('syncwith:*'),"
      "  ('ssl-*')"
      ") SELECT name FROM config, pattern WHERE name GLOB x);"
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
        "DROP TABLE IF EXISTS chat;\n"
      );
    }
    db_protect_pop();
  }
  if( !bNeedRebuild ){
    db_end_transaction(0);
    db_unprotect(PROTECT_ALL);
    db_multi_exec("VACUUM;");
    db_protect_pop();
  }else{
    rebuild_db(1, 0);
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
  static int recursionLevel = 0;  /* Bookkeeping about the recursion level */
  static char *zFnRid1 = 0;       /* The file holding the artifact with RID=1 */
  static int cchPathInitial = 0;  /* The length of zPath on first recursion */

  recursionLevel++;
  if( recursionLevel==1 ){
    cchPathInitial = strlen(zPath);
    if( fKeepRid1!=0 ){
      char *zFnDotRid1 = mprintf("%s/.rid1", zPath);
      Blob bFileContents;
      if( blob_read_from_file(&bFileContents, zFnDotRid1, ExtFILE)!=-1 ){
        Blob line, value;
        while( blob_line(&bFileContents, &line)>0 ){
          if( blob_token(&line, &value)==0 ) continue;  /* Empty line */
          if( blob_buffer(&value)[0]=='#' ) continue;   /* Comment */
          blob_trim(&value);
          zFnRid1 = mprintf("%s/%s", zPath, blob_str(&value));
          break;
        }
        blob_reset(&bFileContents);
        if( zFnRid1 ){
          if( blob_read_from_file(&aContent, zFnRid1, ExtFILE)==-1 ){
            fossil_fatal("some unknown error occurred while reading \"%s\"",
                         zFnRid1);
          }else{
            recon_set_hash_policy(0, zFnRid1);
            content_put(&aContent);
            recon_restore_hash_policy();
            blob_reset(&aContent);
            fossil_print("\r%d", ++nFileRead);
            fflush(stdout);
          }
        }else{
          fossil_fatal("an error occurred while reading or parsing \"%s\"",
                       zFnDotRid1);
        }
      }
      free(zFnDotRid1);
    }
  }
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
          ? (file_isdir(zSubpath, ExtFILE)==1) : (pEntry->d_type==DT_DIR) )
#else
      if( file_isdir(zSubpath, ExtFILE)==1 )
#endif
      {
        recon_read_dir(zSubpath);
      }else if( fossil_strcmp(zSubpath, zFnRid1)!=0 ){
        blob_init(&path, 0, 0);
        blob_appendf(&path, "%s", zSubpath);
        if( blob_read_from_file(&aContent, blob_str(&path), ExtFILE)==-1 ){
          fossil_fatal("some unknown error occurred while reading \"%s\"",
                       blob_str(&path));
        }
        recon_set_hash_policy(cchPathInitial, blob_str(&path));
        content_put(&aContent);
        recon_restore_hash_policy();
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
  if( recursionLevel==1 && zFnRid1!=0 ) free(zFnRid1);
  recursionLevel--;
}

/*
** Helper functions called from recon_read_dir() to set and restore the correct
** hash policy for an artifact read from disk, inferred from the length of the
** path name.
*/
static int saved_eHashPolicy = -1;

void recon_set_hash_policy(
  const int cchPathPrefix,    /* Directory prefix length for zUuidAsFilePath */
  const char *zUuidAsFilePath /* Relative, well-formed, from recon_read_dir() */
){
  int cchUuidAsFilePath;
  const char *zHashPart;
  int cchHashPart = 0;
  int new_eHashPolicy = -1;
  assert( HNAME_COUNT==2 ); /* Review function if new hashes are implemented. */
  if( zUuidAsFilePath==0 ) return;
  cchUuidAsFilePath = strlen(zUuidAsFilePath);
  if( cchUuidAsFilePath==0 ) return;
  if( cchPathPrefix>=cchUuidAsFilePath ) return;
  for( zHashPart = zUuidAsFilePath + cchPathPrefix; *zHashPart; zHashPart++ ){
    if( *zHashPart!='/' ) cchHashPart++;
  }
  if( cchHashPart>=HNAME_LEN_K256 ){
    new_eHashPolicy = HPOLICY_SHA3;
  }else if( cchHashPart>=HNAME_LEN_SHA1 ){
    new_eHashPolicy = HPOLICY_SHA1;
  }
  if( new_eHashPolicy!=-1 ){
    saved_eHashPolicy = g.eHashPolicy;
    g.eHashPolicy = new_eHashPolicy;
  }
}

void recon_restore_hash_policy(){
  if( saved_eHashPolicy!=-1 ){
    g.eHashPolicy = saved_eHashPolicy;
    saved_eHashPolicy = -1;
  }
}

#if 0
/*
** COMMAND: test-hash-from-path*
**
** Usage: %fossil test-hash-from-path ?OPTIONS? DESTINATION UUID
**
** Generate a sample path name from DESTINATION and UUID, as the `deconstruct'
** command would do.  Then try to guess the hash policy from the path name, as
** the `reconstruct' command would do.
**
** No files or directories will be created.
**
** Options:
**   -L|--prefixlength N     Set the length of the names of the DESTINATION
**                           subdirectories to N
*/
void test_hash_from_path_cmd(void) {
  char *zDest;
  char *zUuid;
  char *zFile;
  const char *zHashPolicy = "unknown";
  const char *zPrefixOpt = find_option("prefixlength","L",1);
  int iPrefixLength;
  if( !zPrefixOpt ){
    iPrefixLength = 2;
  }else{
    iPrefixLength = atoi(zPrefixOpt);
    if( iPrefixLength<0 || iPrefixLength>9 ){
      fossil_fatal("N(%s) is not a valid prefix length!",zPrefixOpt);
    }
  }
  if( g.argc!=4 ){
    usage ("?OPTIONS? DESTINATION UUID");
  }
  zDest = g.argv[2];
  zUuid = g.argv[3];
  if( iPrefixLength ){
    zFNameFormat = mprintf("%s/%%.%ds/%%s",zDest,iPrefixLength);
  }else{
    zFNameFormat = mprintf("%s/%%s",zDest);
  }
  cchFNamePrefix = strlen(zDest);
  zFile = mprintf(zFNameFormat /*works-like:"%s:%s"*/,
                  zUuid, zUuid+iPrefixLength);
  recon_set_hash_policy(cchFNamePrefix,zFile);
  if( saved_eHashPolicy!=-1 ){
    zHashPolicy = hpolicy_name();
  }
  recon_restore_hash_policy();
  fossil_print(
    "\nPath Name:   %s"
    "\nHash Policy: %s\n",
    zFile,zHashPolicy);
  free(zFile);
  free(zFNameFormat);
  zFNameFormat = 0;
  cchFNamePrefix = 0;
}
#endif

/*
** Helper functions used by the `deconstruct' and `reconstruct' commands to
** save and restore the contents of the PRIVATE table.
*/
void private_export(char *zFileName)
{
  Stmt q;
  Blob fctx = empty_blob;
  blob_append(&fctx, "# The hashes of private artifacts\n", -1);
  db_prepare(&q,
    "SELECT uuid FROM blob WHERE rid IN ( SELECT rid FROM private );");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    blob_append(&fctx, zUuid, -1);
    blob_append(&fctx, "\n", -1);
  }
  db_finalize(&q);
  blob_write_to_file(&fctx, zFileName);
  blob_reset(&fctx);
}
void private_import(char *zFileName)
{
  Blob fctx;
  if( blob_read_from_file(&fctx, zFileName, ExtFILE)!=-1 ){
    Blob line, value;
    while( blob_line(&fctx, &line)>0 ){
      char *zUuid;
      int nUuid;
      if( blob_token(&line, &value)==0 ) continue;  /* Empty line */
      if( blob_buffer(&value)[0]=='#' ) continue;   /* Comment */
      blob_trim(&value);
      zUuid = blob_buffer(&value);
      nUuid = blob_size(&value);
      zUuid[nUuid] = 0;
      if( hname_validate(zUuid, nUuid)!=HNAME_ERROR ){
        canonical16(zUuid, nUuid);
        db_multi_exec(
          "INSERT OR IGNORE INTO private"
          " SELECT rid FROM blob WHERE uuid = %Q;",
          zUuid);
      }
    }
    blob_reset(&fctx);
  }
}

/*
** COMMAND: reconstruct*
**
** Usage: %fossil reconstruct ?OPTIONS? FILENAME DIRECTORY
**
** This command studies the artifacts (files) in DIRECTORY and reconstructs the
** Fossil record from them.  It places the new Fossil repository in FILENAME.
** Subdirectories are read, files with leading '.' in the filename are ignored.
**
** Options:
**   -K|--keep-rid1     Read the filename of the artifact with RID=1 from the
**                      file .rid in DIRECTORY.
**   -P|--keep-private  Mark the artifacts listed in the file .private in
**                      DIRECTORY as private in the new Fossil repository.
*/
void reconstruct_cmd(void) {
  char *zPassword;
  int fKeepPrivate;
  fKeepRid1 = find_option("keep-rid1","K",0)!=0;
  fKeepPrivate = find_option("keep-private","P",0)!=0;
  if( g.argc!=4 ){
    usage("FILENAME DIRECTORY");
  }
  if( file_isdir(g.argv[3], ExtFILE)!=1 ){
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

  rebuild_db(1, 1);

  /* Backwards compatibility: Mark check-ins with "+private" tags as private. */
  reconstruct_private_table();
  /* Newer method: Import the list of private artifacts to the PRIVATE table. */
  if( fKeepPrivate ){
    char *zFnDotPrivate = mprintf("%s/.private", g.argv[3]);
    private_import(zFnDotPrivate);
    free(zFnDotPrivate);
  }

  /* Skip the verify_before_commit() step on a reconstruct.  Most artifacts
  ** will have been changed and verification therefore takes a really, really
  ** long time.
  */
  verify_cancel();

  db_end_transaction(0);
  fossil_print("project-id: %s\n", db_get("project-code", 0));
  fossil_print("server-id: %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  fossil_print("admin-user: %s (initial password is \"%s\")\n", g.zLogin,
               zPassword);
  hash_user_password(g.zLogin);
}

/*
** COMMAND: deconstruct*
**
** Usage: %fossil deconstruct ?OPTIONS? DESTINATION
**
** This command exports all artifacts of a given repository and writes all
** artifacts to the file system.  The DESTINATION directory will be populated
** with subdirectories AA and files AA/BBBBBBBBB.., where AABBBBBBBBB.. is the
** 40+ character artifact ID, AA the first 2 characters.
** If -L|--prefixlength is given, the length (default 2) of the directory prefix
** can be set to 0,1,..,9 characters.
**
** Options:
**   -R|--repository REPO        Deconstruct given REPOSITORY
**   -K|--keep-rid1              Save the filename of the artifact with RID=1 to
**                               the file .rid1 in the DESTINATION directory
**   -L|--prefixlength N         Set the length of the names of the DESTINATION
**                               subdirectories to N
**   --private                   Include private artifacts
**   -P|--keep-private           Save the list of private artifacts to the file
**                               .private in the DESTINATION directory (implies
**                               the --private option)
*/
void deconstruct_cmd(void){
  const char *zPrefixOpt;
  Stmt        s;
  int privateFlag;
  int fKeepPrivate;

  fKeepRid1 = find_option("keep-rid1","K",0)!=0;
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
  fKeepPrivate = find_option("keep-private","P",0)!=0;
  if( fKeepPrivate ) privateFlag = 1;
  verify_all_options();
  /* check number of arguments */
  if( g.argc!=3 ){
    usage ("?OPTIONS? DESTINATION");
  }
  /* get and check argument destination directory */
  zDestDir = g.argv[g.argc-1];
  if( !*zDestDir  || !file_isdir(zDestDir, ExtFILE)) {
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
  cchFNamePrefix = strlen(zDestDir);

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

  /* Export the list of private artifacts. */
  if( fKeepPrivate ){
    char *zFnDotPrivate = mprintf("%s/.private", zDestDir);
    private_export(zFnDotPrivate);
    free(zFnDotPrivate);
  }

  if(!g.fQuiet && ttyOutput ){
    fossil_print("\n");
  }

  /* free filename format string */
  free(zFNameFormat);
  zFNameFormat = 0;
}
