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
** This routine implements eponymous virtual table for SQLite that gives
** all of the files associated with a single check-in.  The table works
** as a table-valued function.
**
** The source code filename "foci" is short for "Files of Check-in".
**
** Usage example:
**
**    SELECT * FROM files_of_checkin('trunk');
**
** The "schema" for the temp.foci table is:
**
**     CREATE TABLE files_of_checkin(
**       checkinID    INTEGER,    -- RID for the check-in manifest
**       filename     TEXT,       -- Name of a file
**       uuid         TEXT,       -- hash of the file
**       previousName TEXT,       -- Name of the file in previous check-in
**       perm         TEXT,       -- Permissions on the file
**       symname      TEXT HIDDEN -- Symbolic name of the check-in.
**     );
**
** The hidden symname column is (optionally) used as a query parameter to
** identify the particular check-in to parse.  The checkinID parameter
** (such is a unique numeric RID rather than symbolic name) can also be used
** to identify the check-in.  Example:
**
**    SELECT * FROM files_of_checkin
**     WHERE checkinID=symbolic_name_to_rid('trunk');
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
@  checkinID    INTEGER,    -- RID for the check-in manifest
@  filename     TEXT,       -- Name of a file
@  uuid         TEXT,       -- hash of the file
@  previousName TEXT,       -- Name of the file in previous check-in
@  perm         TEXT,       -- Permissions on the file
@  symname      TEXT HIDDEN -- Symbolic name of the check-in
@ );
;

#define FOCI_CHECKINID   0
#define FOCI_FILENAME    1
#define FOCI_UUID        2
#define FOCI_PREVNAME    3
#define FOCI_PERM        4
#define FOCI_SYMNAME     5

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
**   (1)     checkinID=?.  visit only the single manifest specified.
**   (2)     symName=?     visit only the single manifest specified.
*/
static int fociBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  int i;
  pIdxInfo->estimatedCost = 10000.0;
  for(i=0; i<pIdxInfo->nConstraint; i++){
    if( pIdxInfo->aConstraint[i].op==SQLITE_INDEX_CONSTRAINT_EQ
     && (pIdxInfo->aConstraint[i].iColumn==FOCI_CHECKINID
            || pIdxInfo->aConstraint[i].iColumn==FOCI_SYMNAME)
    ){
      if( pIdxInfo->aConstraint[i].iColumn==FOCI_CHECKINID ){
        pIdxInfo->idxNum = 1;
      }else{
        pIdxInfo->idxNum = 2;
      }
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
    int rid;
    if( idxNum==1 ){
      rid = sqlite3_value_int(argv[0]);
    }else{
      rid = symbolic_name_to_rid((const char*)sqlite3_value_text(argv[0]),"ci");
    }
    pCur->pMan = manifest_get(rid, CFTYPE_MANIFEST, 0);
    if( pCur->pMan ){
      manifest_file_rewind(pCur->pMan);
      pCur->pFile = manifest_file_next(pCur->pMan, 0);
    }
  }else{
    pCur->pMan = 0;
  }
  pCur->iFile = 0;
  return SQLITE_OK;
}

static int fociColumn(
  sqlite3_vtab_cursor *pCursor,
  sqlite3_context *ctx,
  int i
){
  FociCursor *pCsr = (FociCursor *)pCursor;
  switch( i ){
    case FOCI_CHECKINID:
      sqlite3_result_int(ctx, pCsr->pMan->rid);
      break;
    case FOCI_FILENAME:
      sqlite3_result_text(ctx, pCsr->pFile->zName, -1,
                          SQLITE_TRANSIENT);
      break;
    case FOCI_UUID:
      sqlite3_result_text(ctx, pCsr->pFile->zUuid, -1,
                          SQLITE_TRANSIENT);
      break;
    case FOCI_PREVNAME:
      sqlite3_result_text(ctx, pCsr->pFile->zPrior, -1,
                          SQLITE_TRANSIENT);
      break;
    case FOCI_PERM:
      sqlite3_result_text(ctx, pCsr->pFile->zPerm, -1,
                          SQLITE_TRANSIENT);
      break;
    case FOCI_SYMNAME:
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
    0,                            /* xSavepoint */
    0,                            /* xRelease */
    0                             /* xRollbackTo */
  };
  sqlite3_create_module(db, "files_of_checkin", &foci_module, 0);
  return SQLITE_OK;
}
