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
** related functionality for removing checkins from a repository.  It also
** manages the graveyard of purged content.
*/
#include "config.h"
#include "purge.h"
#include <assert.h>

/*
** SQL code used to initialize the schema of a bundle.
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
@   uuid TEXT NOT NULL,        -- SHA1 hash of the purged artifact
@   srcid INTEGER,             -- Basis purgeitem for delta compression
@   isPrivate BOOLEAN,         -- True if artifact was originally private
@   sz INT NOT NULL,           -- Uncompressed size of the purged artifact
@   data BLOB                  -- Compressed artifact content
@ );
;

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
*/
int purge_artifact_list(
  const char *zTab,       /* TEMP table containing list of RIDS to be purged */
  const char *zNote       /* Text of the purgeevent.pnotes field */
){
  int peid = 0;                 /* New purgeevent ID */
  Stmt q;                       /* General-use prepared statement */

  assert( g.repositoryOpen );   /* Main database must already be open */
  db_begin_transaction();
  if( purge_baseline_out_from_under_delta(zTab) ){
    fossil_fatal("attempt to purge a baseline manifest without also purging "
                 "all of its deltas");
  }
  db_multi_exec(zPurgeInit /*works-like:"%w%w"*/, 
                db_name("repository"), db_name("repository"));
  db_multi_exec(
    "INSERT INTO purgeevent(ctime,pnotes) VALUES(now(),%Q)", zNote
  );
  peid = db_last_insert_rowid();
  db_prepare(&q, "SELECT rid FROM delta WHERE srcid IN \"%w\""
                 " AND rid NOT IN \"%w\"", zTab, zTab);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    content_undelta(rid);
    verify_before_commit(rid);
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT rid FROM delta WHERE rid IN \"%w\""
                 " AND srcid NOT IN \"%w\"", zTab, zTab);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    content_undelta(rid);
  }
  db_finalize(&q);
  db_multi_exec(
    "INSERT INTO purgeitem(peid,orid,uuid,sz,isPrivate,data)"
    "  SELECT %d, rid, uuid, size,"
    "    EXISTS(SELECT 1 FROM private WHERE private.rid=blob.rid),"
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
  db_multi_exec("DROP TABLE \"%w_tickets\"", zTab);
  db_end_transaction(0);
  return peid;
}

/*
** The TEMP table named zTab contains RIDs for a set of checkins.  
**
** Check to see if any checkin in zTab is a baseline manifest for some
** delta manifest that is not in zTab.  Return true if zTab contains a
** baseline for a delta that is not in zTab.
**
** This is a database integrity preservation check.  The checkins in zTab
** are about to be deleted or otherwise made inaccessible.  This routine
** is checking to ensure that purging the checkins in zTab will not delete
** a baseline manifest out from under a delta.
*/
int purge_baseline_out_from_under_delta(const char *zTab){
  return db_int(0,
    "SELECT 1 FROM plink WHERE baseid IN \"%w\" AND cid NOT IN \"%w\"",
    zTab, zTab);
}


/*
** The TEMP table named zTab contains the RIDs for a set of checkin
** artifacts.  Expand this set (by adding new entries to zTab) to include
** all other artifacts that are used exclusively by the set of checkins in
** the original list.
*/
void find_checkin_associates(const char *zTab){
  db_begin_transaction();

  /* Compute the set of files that need to be added to zTab */
  db_multi_exec("CREATE TEMP TABLE \"%w_files\"(fid INTEGER PRIMARY KEY)",zTab);
  db_multi_exec(
    "INSERT OR IGNORE INTO \"%w_files\"(fid)"
    "  SELECT fid FROM mlink WHERE fid!=0 AND mid IN \"%w\"",
    zTab, zTab
  );
  /* But take out all files that are referenced by check-ins not in zTab */
  db_multi_exec(
    "DELETE FROM \"%w_files\""
    " WHERE fid IN (SELECT fid FROM mlink"
                   " WHERE fid IN \"%w_files\""
                   "   AND mid NOT IN \"%w\")",
    zTab, zTab, zTab
  );

  /* Compute the set of tags that need to be added to zTag */
  db_multi_exec("CREATE TEMP TABLE \"%w_tags\"(tid INTEGER PRIMARY KEY)",zTab);
  db_multi_exec(
    "INSERT OR IGNORE INTO \"%w_tags\"(tid)"
    "  SELECT DISTINCT srcid FROM tagxref WHERE rid in \"%w\" AND srcid!=0",
    zTab, zTab
  );
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
  sqlite3_int64 sz1 = 0;
  sqlite3_int64 sz2 = 0;
  db_prepare(&q, "SELECT piid, substr(uuid,1,16), srcid, isPrivate,"
                 "       sz, length(data)"
                 " FROM purgeitem WHERE peid=%d", peid);
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("     %5d %s %4s %c %10d %10d\n",
       db_column_int(&q,0),
       db_column_text(&q,1),
       db_column_text(&q,2),
       db_column_int(&q,3) ? 'P' : ' ',
       db_column_int(&q,4),
       db_column_int(&q,5));
    sz1 += db_column_int(&q,4);
    sz2 += db_column_int(&q,5);
  }
  db_finalize(&q);
  fossil_print("%.11c%16s%.8c%10lld %10lld\n", ' ', "Total:", ' ', sz1, sz2);
}

/*
** Extract the content for purgeitem number piid into a Blob.  Return
** the number of errors.
*/
static int purge_extract_item(
  int piid,            /* ID of the item to extract */
  Blob *pOut,          /* Write the content into this blob */
  Blob *pHash,         /* If not NULL, write the hash into this blob */
  int *pIsPrivate      /* If not NULL, write the isPrivate flag here */
){
  Stmt q;
  int srcid;
  Blob h1, h2, x;
  static Bag busy;

  db_prepare(&q, "SELECT uuid, srcid, isPrivate, data FROM purgeitem"
                 " WHERE piid=%d", piid);
  if( db_step(&q)!=SQLITE_ROW ){
    db_finalize(&q);
    fossil_fatal("missing purge-item %d", piid);
  }
  if( bag_find(&busy, piid) ) return 1;
  if( pIsPrivate ) *pIsPrivate = db_column_int(&q, 2);
  srcid = db_column_int(&q, 1);
  blob_zero(pOut);
  blob_zero(&x);
  db_column_blob(&q, 3, &x);
  blob_uncompress(&x, pOut);
  blob_reset(&x);
  if( srcid>0 ){
    Blob baseline, out;
    bag_insert(&busy, piid);
    purge_extract_item(srcid, &baseline, 0, 0);
    blob_zero(&out);
    blob_delta_apply(&baseline, pOut, &out);
    blob_reset(pOut);
    *pOut = out;
    blob_reset(&baseline);
  }
  bag_remove(&busy, piid);
  blob_zero(&h1);
  db_column_blob(&q, 0, &h1);
  sha1sum_blob(pOut, &h2);
  if( blob_compare(&h1, &h2)!=0 ){
    fossil_fatal("SHA1 hash mismatch - wanted %s, got %s",
                 blob_str(&h1), blob_str(&h2));
  }
  if( pHash ){
    *pHash = h1;
  }else{
    blob_reset(&h1);
  }
  blob_reset(&h2);
  db_finalize(&q);
  return 0;
}

/*
** COMMAND: purge
**
** The purge command is used to remove content from a repository into a
** "graveyard" and also to show manage the graveyard and optionally restored
** content into the repository from the graveyard.
**
**   fossil purge list|ls [-l]
**
**      Show the graveyard of prior purges.  The -l option gives more
**      detail in the output.
**
**   fossil purge undo ID
**
**      Restore the content previously removed by purge ID.
**
**   fossil purge cat UUID ?FILENAME?
**
**      Whow the content of artifact UUID from the graveyard
**
**   fossil purge [checkin] TAGS... [--explain]
**
**      Move the checkins identified by TAGS and all of their descendants
**      out of the repository and into the graveyard.  If a TAG is a branch
**      name then it means all the checkins on that branch.  If the --explain
**      option appears, then the repository and graveyard are unchanged and
**      an explaination of what would have happened is shown instead.
**
** SUMMARY:
**   fossil purge [checkin] TAGS... [--explain]
**   fossil purge list
**   fossil purge undo ID
**   fossil purge cat UUID [FILENAME]
*/
void purge_cmd(void){
  const char *zSubcmd;
  int n;
  Stmt q;
  if( g.argc<3 ) usage("SUBCOMMAND ?ARGS?");
  zSubcmd = g.argv[2];
  db_find_and_open_repository(0,0);
  n = (int)strlen(zSubcmd);
  if( strncmp(zSubcmd, "list", n)==0 || strcmp(zSubcmd,"ls")==0 ){
    int showDetail = find_option("l","l",0)!=0;
    if( db_int(-1,"PRAGMA table_info('purgeevent')")<0 ) return;
    db_prepare(&q, "SELECT peid, datetime(ctime,'unixepoch','localtime')"
                   " FROM purgeevent");
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%4d on %s\n", db_column_int(&q,0), db_column_text(&q,1));
      if( showDetail ){
        purge_list_event_content(db_column_int(&q,0));
      }
    }
    db_finalize(&q);
  }else if( strncmp(zSubcmd, "undo", n)==0 ){
    fossil_print("Not yet implemented...\n");
  }else if( strncmp(zSubcmd, "cat", n)==0 ){
    const char *zOutFile;
    int piid;
    Blob content;
    if( g.argc!=4 && g.argc!=5 ) usage("cat UUID [FILENAME]");
    zOutFile = g.argc==5 ? g.argv[4] : "-";
    piid = db_int(0, "SELECT piid FROM purgeitem WHERE uuid LIKE '%q%%'",
                     g.argv[3]);
    if( piid==0 ) fossil_fatal("no such item: %s", g.argv[3]);
    purge_extract_item(piid, &content, 0, 0);
    blob_write_to_file(&content, zOutFile);
    blob_reset(&content);
  }else{
    int explainOnly = find_option("explain",0,0)!=0;
    int dryRun = find_option("dry-run",0,0)!=0;
    const char *zTag;
    int i;
    int vid;
    int nCkin;
    int nArtifact;
    verify_all_options();
    db_begin_transaction();
    i = strncmp(zSubcmd,"checkin",n)==0 ? 3 : 2;
    if( i>=g.argc ) usage("[checkin] TAGS... [--explain]");
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    for(; i<g.argc; i++){
      int r = symbolic_name_to_rid(g.argv[i], "br");
      if( r>0 ){
        compute_descendants(r, 1000000000);
      }else if( r==0 ){
        fossil_fatal("not found: %s", g.argv[i]);
      }else{
        fossil_fatal("ambiguous: %s\n", g.argv[i]);
      }
    }
    vid = db_lget_int("checkout",0);
    if( db_exists("SELECT 1 FROM ok WHERE rid=%d",vid) ){
      fossil_fatal("cannot purge the current checkout");
    }
    nCkin = db_int(0, "SELECT count(*) FROM ok");
    find_checkin_associates("ok");
    nArtifact = db_int(0, "SELECT count(*) FROM ok");
    if( explainOnly ){
      i = 0;
      db_prepare(&q, "SELECT rid FROM ok");
      while( db_step(&q)==SQLITE_ROW ){
        if( i++ > 0 ) fossil_print("%.78c\n",'-');
        whatis_rid(db_column_int(&q,0), 0);
      }
      db_finalize(&q);
    }else{
      int peid = purge_artifact_list("ok","");
      fossil_print("%d checkins and %d artifacts purged.\n", nCkin, nArtifact);
      fossil_print("undoable using \"%s purge undo %d\".\n",
                    g.nameOfExe, peid);
    }
    db_end_transaction(explainOnly||dryRun);
  }
}
