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
@   ctime DATETIME,            -- Julian day number when purge occurred
@   pnotes TEXT,               -- Human-readable notes about the purge event
@ );
@ CREATE TABLE IF NOT EXISTS "%w".purgeitem(
@   peid INTEGER REFERENCES purgeevent ON DELETE CASCADE, -- Purge event
@   uuid TEXT NOT NULL,        -- SHA1 hash of the purged artifact
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
void purge_artifact_list(
  const char *zTab,       /* TEMP table containing list of RIDS to be purged */
  const char *zNote       /* Text of the purgeevent.pnotes field */
){
  int peid = 0;                 /* New purgeevent ID */
  Stmt q;                       /* General-use prepared statement */

  assert( g.repositoryOpen );   /* Main database must already be open */
  db_begin_transaction();
  db_multi_exec(zPurgeInit /*works-like:"%w%w"*/, 
                db_name("repository"), db_name("repository"));
  db_multi_exec(
    "INSERT INTO purgeevent(ctime,pnotes) VALUES(now(),%Q)", zNote
  );
  peid = db_last_insert_rowid();
  db_prepare(&q, "SELECT rid FROM delta WHERE srcid IN \"%w\"", zTab);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    content_undelta(rid);
    verify_before_commit(rid);
  }
  db_finalize(&q);
  db_multi_exec(
    "INSERT INTO purgeitem(peid,uuid,sz,data)"
    "  SELECT %d, uuid, size, compress(content(uuid))"
    "    FROM blob WHERE rid IN \"%w\"",
    peid, zTab
  );
  db_multi_exec("DELETE FROM blob WHERE rid IN \"%w\"", zTab);
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
}

/*
** The TEMP table named zTab contains the RIDs for a set of checkin
** artifacts.  Expand this set (by adding new entries to zTab) to include
** all other facts that are used exclusively by the set of checkins in
** the original list.
*/
void purge_checkin_associates(const char *zTab){
  db_begin_transaction();
  
  db_end_transaction(0);
}
