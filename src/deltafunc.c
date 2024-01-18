/*
** Copyright (c) 2019 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** This module implements SQL interfaces to the delta logic.  The code
** here is adapted from the ext/misc/fossildelta.c extension in SQLite.
*/
#include "config.h"
#include "deltafunc.h"

/*
** SQL functions:  delta_create(X,Y)
**
** Return a delta that will transform X into Y.
*/
static void deltaCreateFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *aOrig; int nOrig;  /* old blob */
  const char *aNew;  int nNew;   /* new blob */
  char *aOut;        int nOut;   /* output delta */

  assert( argc==2 );
  if( sqlite3_value_type(argv[0])==SQLITE_NULL ) return;
  if( sqlite3_value_type(argv[1])==SQLITE_NULL ) return;
  nOrig = sqlite3_value_bytes(argv[0]);
  aOrig = (const char*)sqlite3_value_blob(argv[0]);
  nNew = sqlite3_value_bytes(argv[1]);
  aNew = (const char*)sqlite3_value_blob(argv[1]);
  aOut = sqlite3_malloc64(nNew+70);
  if( aOut==0 ){
    sqlite3_result_error_nomem(context);
  }else{
    nOut = delta_create(aOrig, nOrig, aNew, nNew, aOut);
    if( nOut<0 ){
      sqlite3_free(aOut);
      sqlite3_result_error(context, "cannot create fossil delta", -1);
    }else{
      sqlite3_result_blob(context, aOut, nOut, sqlite3_free);
    }
  }
}

/*
** SQL functions:  delta_apply(X,D)
**
** Return the result of applying delta D to input X.
*/
static void deltaApplyFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *aOrig;   int nOrig;        /* The X input */
  const char *aDelta;  int nDelta;       /* The input delta (D) */
  char *aOut;          int nOut, nOut2;  /* The output */

  assert( argc==2 );
  if( sqlite3_value_type(argv[0])==SQLITE_NULL ) return;
  if( sqlite3_value_type(argv[1])==SQLITE_NULL ) return;
  nOrig = sqlite3_value_bytes(argv[0]);
  aOrig = (const char*)sqlite3_value_blob(argv[0]);
  nDelta = sqlite3_value_bytes(argv[1]);
  aDelta = (const char*)sqlite3_value_blob(argv[1]);

  /* Figure out the size of the output */
  nOut = delta_output_size(aDelta, nDelta);
  if( nOut<0 ){
    sqlite3_result_error(context, "corrupt fossil delta", -1);
    return;
  }
  aOut = sqlite3_malloc64((sqlite3_int64)nOut+1);
  if( aOut==0 ){
    sqlite3_result_error_nomem(context);
  }else{
    nOut2 = delta_apply(aOrig, nOrig, aDelta, nDelta, aOut);
    if( nOut2!=nOut ){
      sqlite3_free(aOut);
      sqlite3_result_error(context, "corrupt fossil delta", -1);
    }else{
      sqlite3_result_blob(context, aOut, nOut, sqlite3_free);
    }
  }
}


/*
** SQL functions:  delta_output_size(D)
**
** Return the size of the output that results from applying delta D.
*/
static void deltaOutputSizeFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *aDelta;  int nDelta;       /* The input delta (D) */
  int nOut;                              /* Size of output */
  assert( argc==1 );
  if( sqlite3_value_type(argv[0])==SQLITE_NULL ) return;
  nDelta = sqlite3_value_bytes(argv[0]);
  aDelta = (const char*)sqlite3_value_blob(argv[0]);

  /* Figure out the size of the output */
  nOut = delta_output_size(aDelta, nDelta);
  if( nOut<0 ){
    sqlite3_result_error(context, "corrupt fossil delta", -1);
    return;
  }else{
    sqlite3_result_int(context, nOut);
  }
}

/*****************************************************************************
** Table-valued SQL function:   delta_parse(DELTA)
**
** Schema:
**
**     CREATE TABLE delta_parse(
**       op TEXT,
**       a1 INT,
**       a2 ANY,
**       delta HIDDEN BLOB
**     );
**
** Given an input DELTA, this function parses the delta and returns
** rows for each entry in the delta.  The op column has one of the
** values SIZE, COPY, INSERT, CHECKSUM, ERROR.
**
** Assuming no errors, the first row has op='SIZE'.  a1 is the size of
** the output in bytes and a2 is NULL.
**
** After the initial SIZE row, there are zero or more 'COPY' and/or 'INSERT'
** rows.  A COPY row means content is copied from the source into the
** output.  Column a1 is the number of bytes to copy and a2 is the offset
** into source from which to begin copying.  An INSERT row means to
** insert text into the output stream.  Column a1 is the number of bytes
** to insert and column is a BLOB that contains the text to be inserted.
**
** The last row of a well-formed delta will have an op value of 'CHECKSUM'.
** The a1 column will be the value of the checksum and a2 will be NULL.
**
** If the input delta is not well-formed, then a row with an op value
** of 'ERROR' is returned.  The a1 value of the ERROR row is the offset
** into the delta where the error was encountered and a2 is NULL.
*/
typedef struct deltaparsevtab_vtab deltaparsevtab_vtab;
typedef struct deltaparsevtab_cursor deltaparsevtab_cursor;
struct deltaparsevtab_vtab {
  sqlite3_vtab base;  /* Base class - must be first */
  /* No additional information needed */
};
struct deltaparsevtab_cursor {
  sqlite3_vtab_cursor base;  /* Base class - must be first */
  char *aDelta;              /* The delta being parsed */
  int nDelta;                /* Number of bytes in the delta */
  int iCursor;               /* Current cursor location */
  int eOp;                   /* Name of current operator */
  unsigned int a1, a2;       /* Arguments to current operator */
  int iNext;                 /* Next cursor value */
};

/* Operator names:
*/
static const char *const azOp[] = {
  "SIZE", "COPY", "INSERT", "CHECKSUM", "ERROR", "EOF"
};
#define DELTAPARSE_OP_SIZE         0
#define DELTAPARSE_OP_COPY         1
#define DELTAPARSE_OP_INSERT       2
#define DELTAPARSE_OP_CHECKSUM     3
#define DELTAPARSE_OP_ERROR        4
#define DELTAPARSE_OP_EOF          5

/*
** Read bytes from *pz and convert them into a positive integer.  When
** finished, leave *pz pointing to the first character past the end of
** the integer.  The *pLen parameter holds the length of the string
** in *pz and is decremented once for each character in the integer.
*/
static unsigned int deltaGetInt(const char **pz, int *pLen){
  static const signed char zValue[] = {
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,   -1, -1, -1, -1, -1, -1, -1, -1,
     0,  1,  2,  3,  4,  5,  6,  7,    8,  9, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, 16,   17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32,   33, 34, 35, -1, -1, -1, -1, 36,
    -1, 37, 38, 39, 40, 41, 42, 43,   44, 45, 46, 47, 48, 49, 50, 51,
    52, 53, 54, 55, 56, 57, 58, 59,   60, 61, 62, -1, -1, -1, 63, -1,
  };
  unsigned int v = 0;
  int c;
  unsigned char *z = (unsigned char*)*pz;
  unsigned char *zStart = z;
  while( (c = zValue[0x7f&*(z++)])>=0 ){
     v = (v<<6) + c;
  }
  z--;
  *pLen -= z - zStart;
  *pz = (char*)z;
  return v;
}

/*
** The deltaparsevtabConnect() method is invoked to create a new
** deltaparse virtual table.
**
** Think of this routine as the constructor for deltaparsevtab_vtab objects.
**
** All this routine needs to do is:
**
**    (1) Allocate the deltaparsevtab_vtab object and initialize all fields.
**
**    (2) Tell SQLite (via the sqlite3_declare_vtab() interface) what the
**        result set of queries against the virtual table will look like.
*/
static int deltaparsevtabConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  deltaparsevtab_vtab *pNew;
  int rc;

  rc = sqlite3_declare_vtab(db,
           "CREATE TABLE x(op,a1,a2,delta HIDDEN)"
       );
  /* For convenience, define symbolic names for the index to each column. */
#define DELTAPARSEVTAB_OP     0
#define DELTAPARSEVTAB_A1     1
#define DELTAPARSEVTAB_A2     2
#define DELTAPARSEVTAB_DELTA  3
  if( rc==SQLITE_OK ){
    pNew = sqlite3_malloc64( sizeof(*pNew) );
    *ppVtab = (sqlite3_vtab*)pNew;
    if( pNew==0 ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
  }
  return rc;
}

/*
** This method is the destructor for deltaparsevtab_vtab objects.
*/
static int deltaparsevtabDisconnect(sqlite3_vtab *pVtab){
  deltaparsevtab_vtab *p = (deltaparsevtab_vtab*)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

/*
** Constructor for a new deltaparsevtab_cursor object.
*/
static int deltaparsevtabOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor){
  deltaparsevtab_cursor *pCur;
  pCur = sqlite3_malloc( sizeof(*pCur) );
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

/*
** Destructor for a deltaparsevtab_cursor.
*/
static int deltaparsevtabClose(sqlite3_vtab_cursor *cur){
  deltaparsevtab_cursor *pCur = (deltaparsevtab_cursor*)cur;
  sqlite3_free(pCur->aDelta);
  sqlite3_free(pCur);
  return SQLITE_OK;
}


/*
** Advance a deltaparsevtab_cursor to its next row of output.
*/
static int deltaparsevtabNext(sqlite3_vtab_cursor *cur){
  deltaparsevtab_cursor *pCur = (deltaparsevtab_cursor*)cur;
  const char *z;
  int i = 0;

  pCur->iCursor = pCur->iNext;
  z = pCur->aDelta + pCur->iCursor;
  pCur->a1 = deltaGetInt(&z, &i);
  switch( z[0] ){
    case '@': {
      z++;
      pCur->a2 = deltaGetInt(&z, &i);
      pCur->eOp = DELTAPARSE_OP_COPY;
      pCur->iNext = (int)(&z[1] - pCur->aDelta);
      break;
    }
    case ':': {
      z++;
      pCur->a2 = (unsigned int)(z - pCur->aDelta);
      pCur->eOp = DELTAPARSE_OP_INSERT;
      pCur->iNext = (int)(&z[pCur->a1] - pCur->aDelta);
      break;
    }
    case ';': {
      pCur->eOp = DELTAPARSE_OP_CHECKSUM;
      pCur->iNext = pCur->nDelta;
      break;
    }
    default: {
      if( pCur->iNext==pCur->nDelta ){
        pCur->eOp = DELTAPARSE_OP_EOF;
      }else{
        pCur->eOp = DELTAPARSE_OP_ERROR;
        pCur->iNext = pCur->nDelta;
      }
      break;
    }
  }
  return SQLITE_OK;
}

/*
** Return values of columns for the row at which the deltaparsevtab_cursor
** is currently pointing.
*/
static int deltaparsevtabColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  deltaparsevtab_cursor *pCur = (deltaparsevtab_cursor*)cur;
  switch( i ){
    case DELTAPARSEVTAB_OP: {
      sqlite3_result_text(ctx, azOp[pCur->eOp], -1, SQLITE_STATIC);
      break;
    }
    case DELTAPARSEVTAB_A1: {
      sqlite3_result_int(ctx, pCur->a1);
      break;
    }
    case DELTAPARSEVTAB_A2: {
      if( pCur->eOp==DELTAPARSE_OP_COPY ){
        sqlite3_result_int(ctx, pCur->a2);
      }else if( pCur->eOp==DELTAPARSE_OP_INSERT ){
        sqlite3_result_blob(ctx, pCur->aDelta+pCur->a2, pCur->a1,
                            SQLITE_TRANSIENT);
      }
      break;
    }
    case DELTAPARSEVTAB_DELTA: {
      sqlite3_result_blob(ctx, pCur->aDelta, pCur->nDelta, SQLITE_TRANSIENT);
      break;
    }
  }
  return SQLITE_OK;
}

/*
** Return the rowid for the current row.  In this implementation, the
** rowid is the same as the output value.
*/
static int deltaparsevtabRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  deltaparsevtab_cursor *pCur = (deltaparsevtab_cursor*)cur;
  *pRowid = pCur->iCursor;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int deltaparsevtabEof(sqlite3_vtab_cursor *cur){
  deltaparsevtab_cursor *pCur = (deltaparsevtab_cursor*)cur;
  return pCur->eOp==DELTAPARSE_OP_EOF;
}

/*
** This method is called to "rewind" the deltaparsevtab_cursor object back
** to the first row of output.  This method is always called at least
** once prior to any call to deltaparsevtabColumn() or deltaparsevtabRowid() or
** deltaparsevtabEof().
*/
static int deltaparsevtabFilter(
  sqlite3_vtab_cursor *pVtabCursor,
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  deltaparsevtab_cursor *pCur = (deltaparsevtab_cursor *)pVtabCursor;
  const char *a;
  int i = 0;
  pCur->eOp = DELTAPARSE_OP_ERROR;
  if( idxNum!=1 ){
    return SQLITE_OK;
  }
  pCur->nDelta = sqlite3_value_bytes(argv[0]);
  a = (const char*)sqlite3_value_blob(argv[0]);
  if( pCur->nDelta==0 || a==0 ){
    return SQLITE_OK;
  }
  pCur->aDelta = sqlite3_malloc64( pCur->nDelta+1 );
  if( pCur->aDelta==0 ){
    pCur->nDelta = 0;
    return SQLITE_NOMEM;
  }
  memcpy(pCur->aDelta, a, pCur->nDelta);
  pCur->aDelta[pCur->nDelta] = 0;
  a = pCur->aDelta;
  pCur->eOp = DELTAPARSE_OP_SIZE;
  pCur->a1 = deltaGetInt(&a, &i);
  if( a[0]!='\n' ){
    pCur->eOp = DELTAPARSE_OP_ERROR;
    pCur->a1 = pCur->a2 = 0;
    pCur->iNext = pCur->nDelta;
    return SQLITE_OK;
  }
  a++;
  pCur->iNext = (unsigned int)(a - pCur->aDelta);
  return SQLITE_OK;
}

/*
** SQLite will invoke this method one or more times while planning a query
** that uses the virtual table.  This routine needs to create
** a query plan for each invocation and compute an estimated cost for that
** plan.
*/
static int deltaparsevtabBestIndex(
  sqlite3_vtab *tab,
  sqlite3_index_info *pIdxInfo
){
  int i;
  for(i=0; i<pIdxInfo->nConstraint; i++){
    if( pIdxInfo->aConstraint[i].iColumn != DELTAPARSEVTAB_DELTA ) continue;
    if( pIdxInfo->aConstraint[i].usable==0 ) continue;
    if( pIdxInfo->aConstraint[i].op!=SQLITE_INDEX_CONSTRAINT_EQ ) continue;
    pIdxInfo->aConstraintUsage[i].argvIndex = 1;
    pIdxInfo->aConstraintUsage[i].omit = 1;
    pIdxInfo->estimatedCost = (double)1;
    pIdxInfo->estimatedRows = 10;
    pIdxInfo->idxNum = 1;
    return SQLITE_OK;
  }
  pIdxInfo->idxNum = 0;
  pIdxInfo->estimatedCost = (double)0x7fffffff;
  pIdxInfo->estimatedRows = 0x7fffffff;
  return SQLITE_CONSTRAINT;
}

/*
** This following structure defines all the methods for the
** virtual table.
*/
static sqlite3_module deltaparsevtabModule = {
  /* iVersion    */ 0,
  /* xCreate     */ 0,
  /* xConnect    */ deltaparsevtabConnect,
  /* xBestIndex  */ deltaparsevtabBestIndex,
  /* xDisconnect */ deltaparsevtabDisconnect,
  /* xDestroy    */ 0,
  /* xOpen       */ deltaparsevtabOpen,
  /* xClose      */ deltaparsevtabClose,
  /* xFilter     */ deltaparsevtabFilter,
  /* xNext       */ deltaparsevtabNext,
  /* xEof        */ deltaparsevtabEof,
  /* xColumn     */ deltaparsevtabColumn,
  /* xRowid      */ deltaparsevtabRowid,
  /* xUpdate     */ 0,
  /* xBegin      */ 0,
  /* xSync       */ 0,
  /* xCommit     */ 0,
  /* xRollback   */ 0,
  /* xFindMethod */ 0,
  /* xRename     */ 0,
  /* xSavepoint  */ 0,
  /* xRelease    */ 0,
  /* xRollbackTo */ 0,
  /* xShadowName */ 0,
  /* xIntegrity  */ 0
};

/*
** Invoke this routine to register the various delta functions.
*/
int deltafunc_init(sqlite3 *db){
  int rc = SQLITE_OK;
  rc = sqlite3_create_function(db, "delta_create", 2, SQLITE_UTF8, 0,
                               deltaCreateFunc, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "delta_apply", 2, SQLITE_UTF8, 0,
                                 deltaApplyFunc, 0, 0);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "delta_output_size", 1, SQLITE_UTF8, 0,
                                 deltaOutputSizeFunc, 0, 0);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_module(db, "delta_parse", &deltaparsevtabModule, 0);
  }
  return rc;
}
