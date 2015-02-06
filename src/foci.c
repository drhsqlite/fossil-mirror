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
** This routine implements an SQLite virtual table that gives all of the
** files associated with a single checkin.
**
** The filename "foci" is short for "Files Of CheckIn".
**
** Usage example:
**
**    CREATE VIRTUAL TABLE temp.foci USING files_of_checkin;
**                      -- ^^^^--- important!
**    SELECT * FROM foci WHERE checkinID=symbolic_name_to_rid('trunk');
**
** The symbolic_name_to_rid('trunk') function finds the BLOB.RID value 
** corresponding to the 'trunk' tag.  Then the files_of_checkin virtual table
** decodes the manifest defined by that BLOB and returns all files described
** by that manifest.  The "schema" for the temp.foci table is:
**
**     CREATE TABLE files_of_checkin(
**       checkinID    INTEGER,    -- RID for the checkin manifest
**       filename     TEXT,       -- Name of a file
**       uuid         TEXT,       -- SHA1 hash of the file
**       previousName TEXT,       -- Name of the file in previous checkin
**       perm         TEXT        -- Permissions on the file
**     );
**
*/
#include "config.h"
#include "foci.h"
#include <assert.h>

/*
** The schema for the virtual table:
*/
static const char zFociSchema[] =
@ CREATE TABLE files_of_checkin(
@  checkinID    INTEGER,    -- RID for the checkin manifest
@  filename     TEXT,       -- Name of a file
@  uuid         TEXT,       -- SHA1 hash of the file
@  previousName TEXT,       -- Name of the file in previous checkin
@  perm         TEXT        -- Permissions on the file
@ );
;

#if INTERFACE
/*
** The subclasses of sqlite3_vtab  and sqlite3_vtab_cursor tables
** that implement the files_of_checkin virtual table.
*/
struct FociTable {
  sqlite3_vtab base;        /* Base class - must be first */
};
struct FociCursor {
  sqlite3_vtab_cursor base; /* Base class - must be first */
  Manifest *pMan;           /* Current manifest */
  ManifestFile *pFile;      /* Current file */
  int iFile;                /* File index */
};
#endif /* INTERFACE */


/*
** Connect to or create a foci virtual table.
*/
static int fociConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  FociTable *pTab;

  pTab = (FociTable *)sqlite3_malloc(sizeof(FociTable));
  memset(pTab, 0, sizeof(FociTable));
  sqlite3_declare_vtab(db, zFociSchema);
  *ppVtab = &pTab->base;
  return SQLITE_OK;
}

/*
** Disconnect from or destroy a focivfs virtual table.
*/
static int fociDisconnect(sqlite3_vtab *pVtab){
  sqlite3_free(pVtab);
  return SQLITE_OK;
}

/*
** Available scan methods:
**
**   (0)     A full scan.  Visit every manifest in the repo.  (Slow)
**   (1)     checkinID=?.  visit only the single manifest specifed.
*/
static int fociBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  int i;
  pIdxInfo->estimatedCost = 10000.0;
  for(i=0; i<pIdxInfo->nConstraint; i++){
    if( pIdxInfo->aConstraint[i].iColumn==0
     && pIdxInfo->aConstraint[i].op==SQLITE_INDEX_CONSTRAINT_EQ ){
      pIdxInfo->idxNum = 1;
      pIdxInfo->estimatedCost = 1.0;
      pIdxInfo->aConstraintUsage[i].argvIndex = 1;
      pIdxInfo->aConstraintUsage[i].omit = 1;
      break;
    }
  }
  return SQLITE_OK;
}

/*
** Open a new focivfs cursor.
*/
static int fociOpen(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor){
  FociCursor *pCsr;
  pCsr = (FociCursor *)sqlite3_malloc(sizeof(FociCursor));
  memset(pCsr, 0, sizeof(FociCursor));
  pCsr->base.pVtab = pVTab;
  *ppCursor = (sqlite3_vtab_cursor *)pCsr;
  return SQLITE_OK;
}

/*
** Close a focivfs cursor.
*/
static int fociClose(sqlite3_vtab_cursor *pCursor){
  FociCursor *pCsr = (FociCursor *)pCursor;
  manifest_destroy(pCsr->pMan);
  sqlite3_free(pCsr);
  return SQLITE_OK;
}

/*
** Move a focivfs cursor to the next entry in the file.
*/
static int fociNext(sqlite3_vtab_cursor *pCursor){
  FociCursor *pCsr = (FociCursor *)pCursor;
  pCsr->pFile = manifest_file_next(pCsr->pMan, 0);
  pCsr->iFile++;
  return SQLITE_OK;
}

static int fociEof(sqlite3_vtab_cursor *pCursor){
  FociCursor *pCsr = (FociCursor *)pCursor;
  return pCsr->pFile==0;
}

static int fociFilter(
  sqlite3_vtab_cursor *pCursor,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  FociCursor *pCur = (FociCursor *)pCursor;
  manifest_destroy(pCur->pMan);
  if( idxNum ){
    pCur->pMan = manifest_get(sqlite3_value_int(argv[0]), CFTYPE_MANIFEST, 0);
    pCur->iFile = 0;
    manifest_file_rewind(pCur->pMan);
    pCur->pFile = manifest_file_next(pCur->pMan, 0);
  }else{
    pCur->pMan = 0;
    pCur->iFile = 0;
  }
  return SQLITE_OK;
}

static int fociColumn(
  sqlite3_vtab_cursor *pCursor,
  sqlite3_context *ctx,
  int i
){
  FociCursor *pCsr = (FociCursor *)pCursor;
  switch( i ){
    case 0:            /* checkinID */
      sqlite3_result_int(ctx, pCsr->pMan->rid);
      break;
    case 1:            /* filename */
      sqlite3_result_text(ctx, pCsr->pFile->zName, -1,
                          SQLITE_TRANSIENT);
      break;
    case 2:            /* uuid */
      sqlite3_result_text(ctx, pCsr->pFile->zUuid, -1,
                          SQLITE_TRANSIENT);
      break;
    case 3:            /* previousName */
      sqlite3_result_text(ctx, pCsr->pFile->zPrior, -1,
                          SQLITE_TRANSIENT);
      break;
    case 4:            /* perm */
      sqlite3_result_text(ctx, pCsr->pFile->zPerm, -1,
                          SQLITE_TRANSIENT);
      break;
  }
  return SQLITE_OK;
}

static int fociRowid(sqlite3_vtab_cursor *pCursor, sqlite_int64 *pRowid){
  FociCursor *pCsr = (FociCursor *)pCursor;
  *pRowid = pCsr->iFile;
  return SQLITE_OK;
}

int foci_register(sqlite3 *db){
  static sqlite3_module foci_module = {
    0,                            /* iVersion */
    fociConnect,                  /* xCreate */
    fociConnect,                  /* xConnect */
    fociBestIndex,                /* xBestIndex */
    fociDisconnect,               /* xDisconnect */
    fociDisconnect,               /* xDestroy */
    fociOpen,                     /* xOpen - open a cursor */
    fociClose,                    /* xClose - close a cursor */
    fociFilter,                   /* xFilter - configure scan constraints */
    fociNext,                     /* xNext - advance a cursor */
    fociEof,                      /* xEof - check for end of scan */
    fociColumn,                   /* xColumn - read data */
    fociRowid,                    /* xRowid - read data */
    0,                            /* xUpdate */
    0,                            /* xBegin */
    0,                            /* xSync */
    0,                            /* xCommit */
    0,                            /* xRollback */
    0,                            /* xFindMethod */
    0,                            /* xRename */
  };
  sqlite3_create_module(db, "files_of_checkin", &foci_module, 0);
  return SQLITE_OK;
}
