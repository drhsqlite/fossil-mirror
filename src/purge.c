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
** This file contains code used to implement the "purge" command and
** related functionality for removing check-ins from a repository.  It also
** manages the graveyard of purged content.
*/
#include "config.h"
#include "purge.h"
#include <assert.h>

/*
** SQL code used to initialize the schema of the graveyard.
**
** The purgeevent table contains one entry for each purge event.  For each
** purge event, multiple artifacts might have been removed.  Each removed
** artifact is stored as an entry in the purgeitem table.
**
** The purgeevent and purgeitem tables are not synced, even by the
** "fossil config" command.  They exist only as a backup in case of a
** mistaken purge or for content recovery in case there is a bug in the
** purge command.
*/
static const char zPurgeInit[] =
@ CREATE TABLE IF NOT EXISTS "%w".purgeevent(
@   peid INTEGER PRIMARY KEY,  -- Unique ID for the purge event
@   ctime DATETIME,            -- When purge occurred.  Seconds since 1970.
@   pnotes TEXT                -- Human-readable notes about the purge event
@ );
@ CREATE TABLE IF NOT EXISTS "%w".purgeitem(
@   piid INTEGER PRIMARY KEY,  -- ID for the purge item
@   peid INTEGER REFERENCES purgeevent ON DELETE CASCADE, -- Purge event
@   orid INTEGER,              -- Original RID before purged
@   uuid TEXT NOT NULL,        -- hash of the purged artifact
@   srcid INTEGER,             -- Basis purgeitem for delta compression
@   isPrivate BOOLEAN,         -- True if artifact was originally private
@   sz INT NOT NULL,           -- Uncompressed size of the purged artifact
@   desc TEXT,                 -- Brief description of this artifact
@   data BLOB                  -- Compressed artifact content
@ );
;

/*
** Flags for the purge_artifact_list() function.
*/
#if INTERFACE
#define PURGE_MOVETO_GRAVEYARD  0x0001    /* Move artifacts in graveyard */
#define PURGE_EXPLAIN_ONLY      0x0002    /* Show what would have happened */
#define PURGE_PRINT_SUMMARY     0x0004    /* Print a summary report at end */
#endif

/*
** This routine purges multiple artifacts from the repository, transfering
** those artifacts into the PURGEITEM table.
**
** Prior to invoking this routine, the caller must create a (TEMP) table
** named zTab that contains the RID of every artifact to be purged.
**
** This routine does the following:
**
**    (1) Create the purgeevent and purgeitem tables, if required
**    (2) Create a new purgeevent
**    (3) Make sure no DELTA table entries depend on purged artifacts
**    (4) Create new purgeitem entries for each purged artifact
**    (5) Remove purged artifacts from the BLOB table
**    (6) Remove references to purged artifacts in the following tables:
**         (a) EVENT
**         (b) PRIVATE
**         (c) MLINK
**         (d) PLINK
**         (e) LEAF
**         (f) UNCLUSTERED
**         (g) UNSENT
**         (h) BACKLINK
**         (i) ATTACHMENT
**         (j) TICKETCHNG
**    (7) If any ticket artifacts were removed (6j) then rebuild the
**        corresponding ticket entries.  Possibly remove entries from
**        the ticket table.
**
** Steps 1-4 (saving the purged artifacts into the graveyard) are only
** undertaken if the moveToGraveyard flag is true.
*/
int purge_artifact_list(
  const char *zTab,       /* TEMP table containing list of RIDS to be purged */
  const char *zNote,      /* Text of the purgeevent.pnotes field */
  unsigned purgeFlags     /* zero or more PURGE_* flags */
){
  int peid = 0;                 /* New purgeevent ID */
  Stmt q;                       /* General-use prepared statement */
  char *z;

  assert( g.repositoryOpen );   /* Main database must already be open */
  db_begin_transaction();
  z = sqlite3_mprintf("IN \"%w\"", zTab);
  describe_artifacts(z);
  sqlite3_free(z);
  describe_artifacts_to_stdout(0, 0);

  /* The explain-only flags causes this routine to list the artifacts
  ** that would have been purged but to not actually make any changes
  ** to the repository.
  */
  if( purgeFlags & PURGE_EXPLAIN_ONLY ){
    db_end_transaction(0);
    return 0;
  }

  /* Make sure we are not removing a manifest that is the baseline of some
  ** manifest that is being left behind.  This step is not strictly necessary.
  ** is is just a safety check. */
  if( purge_baseline_out_from_under_delta(zTab) ){
    fossil_fatal("attempt to purge a baseline manifest without also purging "
                 "all of its deltas");
  }

  /* Make sure that no delta that is left behind requires a purged artifact
  ** as its basis.  If such artifacts exist, go ahead and undelta them now.
  */
  db_prepare(&q, "SELECT rid FROM delta WHERE srcid IN \"%w\""
                 " AND rid NOT IN \"%w\"", zTab, zTab);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    content_undelta(rid);
    verify_before_commit(rid);
  }
  db_finalize(&q);

  /* Construct the graveyard and copy the artifacts to be purged into the
  ** graveyard */
  if( purgeFlags & PURGE_MOVETO_GRAVEYARD ){
    db_multi_exec(zPurgeInit /*works-like:"%w%w"*/,
                  "repository", "repository");
    db_multi_exec(
      "INSERT INTO purgeevent(ctime,pnotes) VALUES(now(),%Q)", zNote
    );
    peid = db_last_insert_rowid();
    db_prepare(&q, "SELECT rid FROM delta WHERE rid IN \"%w\""
                   " AND srcid NOT IN \"%w\"", zTab, zTab);
    while( db_step(&q)==SQLITE_ROW ){
      int rid = db_column_int(&q, 0);
      content_undelta(rid);
    }
    db_finalize(&q);
    db_multi_exec(
      "INSERT INTO purgeitem(peid,orid,uuid,sz,isPrivate,desc,data)"
      "  SELECT %d, rid, uuid, size,"
      "    EXISTS(SELECT 1 FROM private WHERE private.rid=blob.rid),"
      "    (SELECT summary FROM description WHERE rid=blob.rid),"
      "    content"
      "    FROM blob WHERE rid IN \"%w\"",
      peid, zTab
    );
    db_multi_exec(
      "UPDATE purgeitem"
      "   SET srcid=(SELECT piid FROM purgeitem px, delta"
                    " WHERE px.orid=delta.srcid"
                    "   AND delta.rid=purgeitem.orid)"
      " WHERE peid=%d",
      peid
    );
  }

  /* Remove the artifacts being purged.  Also remove all references to those
  ** artifacts from the secondary tables. */
  db_multi_exec("DELETE FROM blob WHERE rid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM delta WHERE rid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM delta WHERE srcid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM event WHERE objid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM private WHERE rid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM mlink WHERE mid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM plink WHERE pid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM plink WHERE cid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM leaf WHERE rid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM phantom WHERE rid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM unclustered WHERE rid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM unsent WHERE rid IN \"%w\"", zTab);
  db_multi_exec("DELETE FROM tagxref"
                " WHERE rid IN \"%w\""
                "    OR srcid IN \"%w\""
                "    OR origid IN \"%w\"", zTab, zTab, zTab);
  db_multi_exec("DELETE FROM backlink WHERE srctype=0 AND srcid IN \"%w\"",
                zTab);
  db_multi_exec(
    "CREATE TEMP TABLE \"%w_tickets\" AS"
    " SELECT DISTINCT tkt_uuid FROM ticket WHERE tkt_id IN"
    "    (SELECT tkt_id FROM ticketchng WHERE tkt_rid IN \"%w\")",
    zTab, zTab);
  db_multi_exec("DELETE FROM ticketchng WHERE tkt_rid IN \"%w\"", zTab);
  db_prepare(&q, "SELECT tkt_uuid FROM \"%w_tickets\"", zTab);
  while( db_step(&q)==SQLITE_ROW ){
    ticket_rebuild_entry(db_column_text(&q, 0));
  }
  db_finalize(&q);
  /* db_multi_exec("DROP TABLE \"%w_tickets\"", zTab); */

  /* Mission accomplished */
  db_end_transaction(0);

  if( purgeFlags & PURGE_PRINT_SUMMARY ){
    fossil_print("%d artifacts purged\n",
                  db_int(0, "SELECT count(*) FROM \"%w\";", zTab));
    fossil_print("undoable using \"%s purge undo %d\".\n",
                  g.nameOfExe, peid);
  }
  return peid;
}

/*
** The TEMP table named zTab contains RIDs for a set of check-ins.
**
** Check to see if any check-in in zTab is a baseline manifest for some
** delta manifest that is not in zTab.  Return true if zTab contains a
** baseline for a delta that is not in zTab.
**
** This is a database integrity preservation check.  The check-ins in zTab
** are about to be deleted or otherwise made inaccessible.  This routine
** is checking to ensure that purging the check-ins in zTab will not delete
** a baseline manifest out from under a delta.
*/
int purge_baseline_out_from_under_delta(const char *zTab){
  if( !db_table_has_column("repository","plink","baseid") ){
    /* Skip this check if the current database is an older schema that
    ** does not contain the PLINK.BASEID field. */
    return 0;
  }else{
    return db_int(0,
      "SELECT 1 FROM plink WHERE baseid IN \"%w\" AND cid NOT IN \"%w\"",
      zTab, zTab);
  }
}


/*
** The TEMP table named zTab contains the RIDs for a set of check-in
** artifacts.  Expand this set (by adding new entries to zTab) to include
** all other artifacts that are used by the check-ins in
** the original list.
**
** If the bExclusive flag is true, then the set is only expanded by
** artifacts that are used exclusively by the check-ins in the set.
** When bExclusive is false, then all artifacts used by the check-ins
** are added even if those artifacts are also used by other check-ins
** not in the set.
**
** The "fossil publish" command with the (undocumented) --test and
** --exclusive options can be used for interactiving testing of this
** function.
*/
void find_checkin_associates(const char *zTab, int bExclusive){
  db_begin_transaction();

  /* Compute the set of files that need to be added to zTab */
  db_multi_exec("CREATE TEMP TABLE \"%w_files\"(fid INTEGER PRIMARY KEY)",zTab);
  db_multi_exec(
    "INSERT OR IGNORE INTO \"%w_files\"(fid)"
    "  SELECT fid FROM mlink WHERE fid!=0 AND mid IN \"%w\"",
    zTab, zTab
  );
  if( bExclusive ){
    /* But take out all files that are referenced by check-ins not in zTab */
    db_multi_exec(
      "DELETE FROM \"%w_files\""
      " WHERE fid IN (SELECT fid FROM mlink"
                     " WHERE fid IN \"%w_files\""
                     "   AND mid NOT IN \"%w\")",
      zTab, zTab, zTab
    );
  }

  /* Compute the set of tags that need to be added to zTag */
  db_multi_exec("CREATE TEMP TABLE \"%w_tags\"(tid INTEGER PRIMARY KEY)",zTab);
  db_multi_exec(
    "INSERT OR IGNORE INTO \"%w_tags\"(tid)"
    "  SELECT DISTINCT srcid FROM tagxref WHERE rid in \"%w\" AND srcid!=0",
    zTab, zTab
  );
  if( bExclusive ){
    /* But take out tags that references some check-ins in zTab and other
    ** check-ins not in zTab.  The current Fossil implementation never creates
    ** such tags, so the following should usually be a no-op.  But the file
    ** format specification allows such tags, so we should check for them.
    */
    db_multi_exec(
      "DELETE FROM \"%w_tags\""
      " WHERE tid IN (SELECT srcid FROM tagxref"
                     " WHERE srcid IN \"%w_tags\""
                     "   AND rid NOT IN \"%w\")",
      zTab, zTab, zTab
    );
  }

  /* Transfer the extra artifacts into zTab */
  db_multi_exec(
    "INSERT OR IGNORE INTO \"%w\" SELECT fid FROM \"%w_files\";"
    "INSERT OR IGNORE INTO \"%w\" SELECT tid FROM \"%w_tags\";"
    "DROP TABLE \"%w_files\";"
    "DROP TABLE \"%w_tags\";",
    zTab, zTab, zTab, zTab, zTab, zTab
  );

  db_end_transaction(0);
}

/*
** Display the content of a single purge event.
*/
static void purge_list_event_content(int peid){
  Stmt q;
  sqlite3_int64 sz = 0;
  db_prepare(&q, "SELECT piid, substr(uuid,1,16), srcid, isPrivate,"
                 "       length(data), desc"
                 " FROM purgeitem WHERE peid=%d", peid);
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("     %5d %s %4s %c %10d %s\n",
       db_column_int(&q,0),
       db_column_text(&q,1),
       db_column_text(&q,2),
       db_column_int(&q,3) ? 'P' : ' ',
       db_column_int(&q,4),
       db_column_text(&q,5));
    sz += db_column_int(&q,4);
  }
  db_finalize(&q);
  fossil_print("%.11c%16s%.8c%10lld\n", ' ', "Total:", ' ', sz);
}

/*
** Extract the content for purgeitem number piid into a Blob.  Return
** the number of errors.
*/
static int purge_extract_item(
  int piid,            /* ID of the item to extract */
  Blob *pOut           /* Write the content into this blob */
){
  Stmt q;
  int srcid;
  Blob h1, x;
  static Bag busy;

  db_prepare(&q, "SELECT uuid, srcid, data FROM purgeitem"
                 " WHERE piid=%d", piid);
  if( db_step(&q)!=SQLITE_ROW ){
    db_finalize(&q);
    fossil_fatal("missing purge-item %d", piid);
  }
  if( bag_find(&busy, piid) ) return 1;
  srcid = db_column_int(&q, 1);
  blob_zero(pOut);
  blob_zero(&x);
  db_column_blob(&q, 2, &x);
  blob_uncompress(&x, pOut);
  blob_reset(&x);
  if( srcid>0 ){
    Blob baseline, out;
    bag_insert(&busy, piid);
    purge_extract_item(srcid, &baseline);
    blob_zero(&out);
    blob_delta_apply(&baseline, pOut, &out);
    blob_reset(pOut);
    *pOut = out;
    blob_reset(&baseline);
  }
  bag_remove(&busy, piid);
  blob_zero(&h1);
  db_column_blob(&q, 0, &h1);
  if( hname_verify_hash(pOut, blob_buffer(&h1), blob_size(&h1))==0 ){
    fossil_fatal("incorrect artifact hash on %b", &h1);
  }
  blob_reset(&h1);
  db_finalize(&q);
  return 0;
}

/*
** There is a TEMP table ix(piid,srcid) containing a set of purgeitems
** that need to be transferred to the BLOB table.  This routine does
** all items that have srcid=iSrc.  The pBasis blob holds the content
** of the source document if iSrc>0.
*/
static void purge_item_resurrect(int iSrc, Blob *pBasis){
  Stmt q;
  static Bag busy;
  assert( pBasis!=0 || iSrc==0 );
  if( iSrc>0 ){
    if( bag_find(&busy, iSrc) ){
      fossil_fatal("delta loop while uncompressing purged artifacts");
    }
    bag_insert(&busy, iSrc);
  }
  db_prepare(&q,
     "SELECT uuid, data, isPrivate, ix.piid"
     "  FROM ix, purgeitem"
     " WHERE ix.srcid=%d"
     "   AND ix.piid=purgeitem.piid;",
     iSrc
  );
  while( db_step(&q)==SQLITE_ROW ){
    Blob h1, c1, c2;
    int isPriv, rid;
    blob_zero(&h1);
    db_column_blob(&q, 0, &h1);
    blob_zero(&c1);
    db_column_blob(&q, 1, &c1);
    blob_uncompress(&c1, &c1);
    blob_zero(&c2);
    if( pBasis ){
      blob_delta_apply(pBasis, &c1, &c2);
      blob_reset(&c1);
    }else{
      c2 = c1;
    }
    if( hname_verify_hash(&c2, blob_buffer(&h1), blob_size(&h1))==0 ){
      fossil_fatal("incorrect hash on %b", &h1);
    }
    isPriv = db_column_int(&q, 2);
    rid = content_put_ex(&c2, blob_str(&h1), 0, 0, isPriv);
    if( rid==0 ){
      fossil_fatal("%s", g.zErrMsg);
    }else{
      if( !isPriv ) content_make_public(rid);
      content_get(rid, &c1);
      manifest_crosslink(rid, &c1, MC_NO_ERRORS);
    }
    purge_item_resurrect(db_column_int(&q,3), &c2);
    blob_reset(&c2);
  }
  db_finalize(&q);
  if( iSrc>0 ) bag_remove(&busy, iSrc);
}

/*
** COMMAND: purge*
**
** The purge command removes content from a repository and stores that content
** in a "graveyard".  The graveyard exists so that content can be recovered
** using the "fossil purge undo" command.  The "fossil purge obliterate"
** command empties the graveyard, making the content unrecoverable.
**
** ==== WARNING: This command can potentially destroy historical data and ====
** ==== leave your repository in a goofy state. Know what you are doing!  ====
** ==== Make a backup of your repository before using this command!       ====
**
** ==== FURTHER WARNING: This command is a work-in-progress and may yet   ====
** ==== contain bugs.                                                     ====
**
**   fossil purge artifacts UUID... ?OPTIONS?
**
**      Move arbitrary artifacts identified by the UUID list into the
**      graveyard.
**
**   fossil purge cat UUID...
**
**      Write the content of one or more artifacts in the graveyard onto
**      standard output.
**
**   fossil purge checkins TAGS... ?OPTIONS?
**
**      Move the check-ins or branches identified by TAGS and all of
**      their descendants out of the repository and into the graveyard.
**      If TAGS includes a branch name then it means all the check-ins
**      on the most recent occurrence of that branch.
**
**   fossil purge files NAME ... ?OPTIONS?
**
**      Move all instances of files called NAME into the graveyard.
**      NAME should be the name of the file relative to the root of the
**      repository.  If NAME is a directory, then all files within that
**      directory are moved.
**
**   fossil purge list|ls ?-l?
**
**      Show the graveyard of prior purges.  The -l option gives more
**      detail in the output.
**
**   fossil purge obliterate ID... ?--force?
**
**      Remove one or more purge events from the graveyard.  Once a purge
**      event is obliterated, it can no longer be undone.  The --force
**      option suppresses the confirmation prompt.
**
**   fossil purge tickets NAME ... ?OPTIONS?
**
**      TBD...
**
**   fossil purge undo ID
**
**      Restore the content previously removed by purge ID.
**
**   fossil purge wiki NAME ... ?OPTIONS?
**
**      TBD...
**
** COMMON OPTIONS:
**
**   --explain         Make no changes, but show what would happen.
**   --dry-run         An alias for --explain
**
** SUMMARY:
**   fossil purge artifacts UUID.. [OPTIONS]
**   fossil purge cat UUID...
**   fossil purge checkins TAGS... [OPTIONS]
**   fossil purge files FILENAME... [OPTIONS]
**   fossil purge list
**   fossil purge obliterate ID...
**   fossil purge tickets NAME... [OPTIONS]
**   fossil purge undo ID
**   fossil purge wiki NAME... [OPTIONS]
*/
void purge_cmd(void){
  int purgeFlags = PURGE_MOVETO_GRAVEYARD | PURGE_PRINT_SUMMARY;
  const char *zSubcmd;
  int n;
  int i;
  Stmt q;

  if( g.argc<3 ) usage("SUBCOMMAND ?ARGS?");
  zSubcmd = g.argv[2];
  db_find_and_open_repository(0,0);
  n = (int)strlen(zSubcmd);
  if( find_option("explain",0,0)!=0 || find_option("dry-run",0,0)!=0 ){
    purgeFlags |= PURGE_EXPLAIN_ONLY;
  }
  if( strncmp(zSubcmd, "artifacts", n)==0 ){
    verify_all_options();
    db_begin_transaction();
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    for(i=3; i<g.argc; i++){
      int r = name_to_typed_rid(g.argv[i], "");
      db_multi_exec("INSERT OR IGNORE INTO ok(rid) VALUES(%d);", r);
    }
    describe_artifacts_to_stdout("IN ok", 0);
    purge_artifact_list("ok", "", purgeFlags);
    db_end_transaction(0);
  }else if( strncmp(zSubcmd, "cat", n)==0 ){
    int i, piid;
    Blob content;
    if( g.argc<4 ) usage("cat UUID...");
    for(i=3; i<g.argc; i++){
      piid = db_int(0, "SELECT piid FROM purgeitem WHERE uuid LIKE '%q%%'",
                       g.argv[i]);
      if( piid==0 ) fossil_fatal("no such item: %s", g.argv[3]);
      purge_extract_item(piid, &content);
      blob_write_to_file(&content, "-");
      blob_reset(&content);
    }
  }else if( strncmp(zSubcmd, "checkins", n)==0 ){
    int vid;
    if( find_option("explain",0,0)!=0 || find_option("dry-run",0,0)!=0 ){
      purgeFlags |= PURGE_EXPLAIN_ONLY;
    }
    verify_all_options();
    db_begin_transaction();
    if( g.argc<=3 ) usage("checkins TAGS... [OPTIONS]");
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    for(i=3; i<g.argc; i++){
      int r = name_to_typed_rid(g.argv[i], "br");
      compute_descendants(r, 1000000000);
    }
    vid = db_lget_int("checkout",0);
    if( db_exists("SELECT 1 FROM ok WHERE rid=%d",vid) ){
      fossil_fatal("cannot purge the current checkout");
    }
    find_checkin_associates("ok", 1);
    purge_artifact_list("ok", "", purgeFlags);
    db_end_transaction(0);
  }else if( strncmp(zSubcmd, "files", n)==0 ){
    verify_all_options();
    db_begin_transaction();
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    for(i=3; i<g.argc; i++){
      db_multi_exec(
         "INSERT OR IGNORE INTO ok(rid) "
         "  SELECT fid FROM mlink, filename"
         "   WHERE mlink.fnid=filename.fnid"
         "     AND (filename.name=%Q OR filename.name GLOB '%q/*')",
         g.argv[i], g.argv[i]
      );
    }
    purge_artifact_list("ok", "", purgeFlags);
    db_end_transaction(0);
  }else if( strncmp(zSubcmd, "list", n)==0 || strcmp(zSubcmd,"ls")==0 ){
    int showDetail = find_option("l","l",0)!=0;
    if( !db_table_exists("repository","purgeevent") ) return;
    db_prepare(&q, "SELECT peid, datetime(ctime,'unixepoch',toLocal())"
                   " FROM purgeevent");
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%4d on %s\n", db_column_int(&q,0), db_column_text(&q,1));
      if( showDetail ){
        purge_list_event_content(db_column_int(&q,0));
      }
    }
    db_finalize(&q);
  }else if( strncmp(zSubcmd, "obliterate", n)==0 ){
    int i;
    int bForce = find_option("force","f",0)!=0;
    if( g.argc<4 ) usage("obliterate ID...");
    if( !bForce ){
      Blob ans;
      char cReply;
      prompt_user(
         "Obliterating the graveyard will permanently delete information.\n"
         "Changes cannot be undone.  Continue (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      if( cReply!='y' && cReply!='Y' ){
        fossil_exit(1);
      }
    }
    db_begin_transaction();
    for(i=3; i<g.argc; i++){
      int peid = atoi(g.argv[i]);
      if( !db_exists("SELECT 1 FROM purgeevent WHERE peid=%d",peid) ){
        fossil_fatal("no such purge event: %s", g.argv[i]);
      }
      db_multi_exec(
        "DELETE FROM purgeevent WHERE peid=%d;"
        "DELETE FROM purgeitem WHERE peid=%d;",
        peid, peid
      );
    }
    db_end_transaction(0);
  }else if( strncmp(zSubcmd, "tickets", n)==0 ){
    fossil_fatal("not yet implemented....");
  }else if( strncmp(zSubcmd, "undo", n)==0 ){
    int peid;
    if( g.argc!=4 ) usage("undo ID");
    peid = atoi(g.argv[3]);
    if( (purgeFlags & PURGE_EXPLAIN_ONLY)==0 ){
      db_begin_transaction();
      db_multi_exec(
        "CREATE TEMP TABLE ix("
        "  piid INTEGER PRIMARY KEY,"
        "  srcid INTEGER"
        ");"
        "CREATE INDEX ixsrcid ON ix(srcid);"
        "INSERT INTO ix(piid,srcid) "
        "  SELECT piid, coalesce(srcid,0) FROM purgeitem WHERE peid=%d;",
        peid
      );
      db_multi_exec(
        "DELETE FROM shun"
        " WHERE uuid IN (SELECT uuid FROM purgeitem WHERE peid=%d);",
        peid
      );
      manifest_crosslink_begin();
      purge_item_resurrect(0, 0);
      manifest_crosslink_end(0);
      db_multi_exec("DELETE FROM purgeevent WHERE peid=%d", peid);
      db_multi_exec("DELETE FROM purgeitem WHERE peid=%d", peid);
      db_end_transaction(0);
    }
  }else if( strncmp(zSubcmd, "wiki", n)==0 ){
    fossil_fatal("not yet implemented....");
  }else{
    fossil_fatal("unknown subcommand \"%s\".\n"
                 "should be one of:  cat, checkins, files, list, obliterate,"
                 " tickets, undo, wiki", zSubcmd);
  }
}
