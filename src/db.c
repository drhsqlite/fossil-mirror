/*
** Copyright (c) 2006 D. Richard Hipp
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
** Code for interfacing to the various databases.
**
** There are three separate database files that fossil interacts
** with:
**
**    (1)  The "user" database in ~/.fossil
**
**    (2)  The "repository" database
**
**    (3)  A local checkout database named "_FOSSIL_" or ".fslckout"
**         and located at the root of the local copy of the source tree.
**
*/
#include "config.h"
#if defined(_WIN32)
#  if USE_SEE
#    include <windows.h>
#  endif
#else
#  include <pwd.h>
#endif
#if USE_SEE && !defined(SQLITE_HAS_CODEC)
#  define SQLITE_HAS_CODEC
#endif
#include <sqlite3.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "db.h"

#if INTERFACE
/*
** An single SQL statement is represented as an instance of the following
** structure.
*/
struct Stmt {
  Blob sql;               /* The SQL for this statement */
  sqlite3_stmt *pStmt;    /* The results of sqlite3_prepare_v2() */
  Stmt *pNext, *pPrev;    /* List of all unfinalized statements */
  int nStep;              /* Number of sqlite3_step() calls */
};

/*
** Copy this to initialize a Stmt object to a clean/empty state. This
** is useful to help avoid assertions when performing cleanup in some
** error handling cases.
*/
#define empty_Stmt_m {BLOB_INITIALIZER,NULL, NULL, NULL, 0}
#endif /* INTERFACE */
const struct Stmt empty_Stmt = empty_Stmt_m;

/*
** Call this routine when a database error occurs.
*/
static void db_err(const char *zFormat, ...){
  va_list ap;
  char *z;
  int rc = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
#ifdef FOSSIL_ENABLE_JSON
  if( g.json.isJsonMode ){
    json_err( 0, z, 1 );
    if( g.isHTTP ){
      rc = 0 /* avoid HTTP 500 */;
    }
  }
  else
#endif /* FOSSIL_ENABLE_JSON */
  if( g.xferPanic ){
    cgi_reset_content();
    @ error Database\serror:\s%F(z)
      cgi_reply();
  }
  else if( g.cgiOutput ){
    g.cgiOutput = 0;
    cgi_printf("<h1>Database Error</h1>\n<p>%h</p>\n", z);
    cgi_reply();
  }else{
    fprintf(stderr, "%s: %s\n", g.argv[0], z);
  }
  free(z);
  db_force_rollback();
  fossil_exit(rc);
}

/*
** All static variable that a used by only this file are gathered into
** the following structure.
*/
static struct DbLocalData {
  int nBegin;               /* Nesting depth of BEGIN */
  int doRollback;           /* True to force a rollback */
  int nCommitHook;          /* Number of commit hooks */
  Stmt *pAllStmt;           /* List of all unfinalized statements */
  int nPrepare;             /* Number of calls to sqlite3_prepare_v2() */
  int nDeleteOnFail;        /* Number of entries in azDeleteOnFail[] */
  struct sCommitHook {
    int (*xHook)(void);         /* Functions to call at db_end_transaction() */
    int sequence;               /* Call functions in sequence order */
  } aHook[5];
  char *azDeleteOnFail[3];  /* Files to delete on a failure */
  char *azBeforeCommit[5];  /* Commands to run prior to COMMIT */
  int nBeforeCommit;        /* Number of entries in azBeforeCommit */
  int nPriorChanges;        /* sqlite3_total_changes() at transaction start */
} db = {0, 0, 0, 0, 0, 0, };

/*
** Arrange for the given file to be deleted on a failure.
*/
void db_delete_on_failure(const char *zFilename){
  assert( db.nDeleteOnFail<count(db.azDeleteOnFail) );
  db.azDeleteOnFail[db.nDeleteOnFail++] = fossil_strdup(zFilename);
}

/*
** This routine is called by the SQLite commit-hook mechanism
** just prior to each commit.  All this routine does is verify
** that nBegin really is zero.  That insures that transactions
** cannot commit by any means other than by calling db_end_transaction()
** below.
**
** This is just a safety and sanity check.
*/
static int db_verify_at_commit(void *notUsed){
  if( db.nBegin ){
    fossil_panic("illegal commit attempt");
    return 1;
  }
  return 0;
}

/*
** Begin and end a nested transaction
*/
void db_begin_transaction(void){
  if( db.nBegin==0 ){
    db_multi_exec("BEGIN");
    sqlite3_commit_hook(g.db, db_verify_at_commit, 0);
    db.nPriorChanges = sqlite3_total_changes(g.db);
  }
  db.nBegin++;
}
void db_end_transaction(int rollbackFlag){
  if( g.db==0 ) return;
  if( db.nBegin<=0 ) return;
  if( rollbackFlag ) db.doRollback = 1;
  db.nBegin--;
  if( db.nBegin==0 ){
    int i;
    if( db.doRollback==0 && db.nPriorChanges<sqlite3_total_changes(g.db) ){
      i = 0;
      while( db.nBeforeCommit ){
        db.nBeforeCommit--;
        sqlite3_exec(g.db, db.azBeforeCommit[i], 0, 0, 0);
        sqlite3_free(db.azBeforeCommit[i]);
        i++;
      }
      leaf_do_pending_checks();
    }
    for(i=0; db.doRollback==0 && i<db.nCommitHook; i++){
      db.doRollback |= db.aHook[i].xHook();
    }
    while( db.pAllStmt ){
      db_finalize(db.pAllStmt);
    }
    db_multi_exec("%s", db.doRollback ? "ROLLBACK" : "COMMIT");
    db.doRollback = 0;
  }
}

/*
** Force a rollback and shutdown the database
*/
void db_force_rollback(void){
  int i;
  static int busy = 0;
  sqlite3_stmt *pStmt = 0;
  if( busy || g.db==0 ) return;
  busy = 1;
  undo_rollback();
  while( (pStmt = sqlite3_next_stmt(g.db,pStmt))!=0 ){
    sqlite3_reset(pStmt);
  }
  while( db.pAllStmt ){
    db_finalize(db.pAllStmt);
  }
  if( db.nBegin ){
    sqlite3_exec(g.db, "ROLLBACK", 0, 0, 0);
    db.nBegin = 0;
  }
  busy = 0;
  db_close(0);
  for(i=0; i<db.nDeleteOnFail; i++){
    file_delete(db.azDeleteOnFail[i]);
  }
}

/*
** Install a commit hook.  Hooks are installed in sequence order.
** It is an error to install the same commit hook more than once.
**
** Each commit hook is called (in order of ascending sequence) at
** each commit operation.  If any commit hook returns non-zero,
** the subsequence commit hooks are omitted and the transaction
** rolls back rather than commit.  It is the responsibility of the
** hooks themselves to issue any error messages.
*/
void db_commit_hook(int (*x)(void), int sequence){
  int i;
  assert( db.nCommitHook < count(db.aHook) );
  for(i=0; i<db.nCommitHook; i++){
    assert( x!=db.aHook[i].xHook );
    if( db.aHook[i].sequence>sequence ){
      int s = sequence;
      int (*xS)(void) = x;
      sequence = db.aHook[i].sequence;
      x = db.aHook[i].xHook;
      db.aHook[i].sequence = s;
      db.aHook[i].xHook = xS;
    }
  }
  db.aHook[db.nCommitHook].sequence = sequence;
  db.aHook[db.nCommitHook].xHook = x;
  db.nCommitHook++;
}

#if INTERFACE
/*
** Possible flags to db_vprepare
*/
#define DB_PREPARE_IGNORE_ERROR  0x001  /* Suppress errors */
#define DB_PREPARE_PERSISTENT    0x002  /* Stmt will stick around for a while */
#endif

/*
** Prepare a Stmt.  Assume that the Stmt is previously uninitialized.
** If the input string contains multiple SQL statements, only the first
** one is processed.  All statements beyond the first are silently ignored.
*/
int db_vprepare(Stmt *pStmt, int flags, const char *zFormat, va_list ap){
  int rc;
  int prepFlags = 0;
  char *zSql;
  blob_zero(&pStmt->sql);
  blob_vappendf(&pStmt->sql, zFormat, ap);
  va_end(ap);
  zSql = blob_str(&pStmt->sql);
  db.nPrepare++;
  if( flags & DB_PREPARE_PERSISTENT ){
    prepFlags = SQLITE_PREPARE_PERSISTENT;
  }
  rc = sqlite3_prepare_v3(g.db, zSql, -1, prepFlags, &pStmt->pStmt, 0);
  if( rc!=0 && (flags & DB_PREPARE_IGNORE_ERROR)!=0 ){
    db_err("%s\n%s", sqlite3_errmsg(g.db), zSql);
  }
  pStmt->pNext = pStmt->pPrev = 0;
  pStmt->nStep = 0;
  return rc;
}
int db_prepare(Stmt *pStmt, const char *zFormat, ...){
  int rc;
  va_list ap;
  va_start(ap, zFormat);
  rc = db_vprepare(pStmt, 0, zFormat, ap);
  va_end(ap);
  return rc;
}
int db_prepare_ignore_error(Stmt *pStmt, const char *zFormat, ...){
  int rc;
  va_list ap;
  va_start(ap, zFormat);
  rc = db_vprepare(pStmt, DB_PREPARE_IGNORE_ERROR, zFormat, ap);
  va_end(ap);
  return rc;
}
int db_static_prepare(Stmt *pStmt, const char *zFormat, ...){
  int rc = SQLITE_OK;
  if( blob_size(&pStmt->sql)==0 ){
    va_list ap;
    va_start(ap, zFormat);
    rc = db_vprepare(pStmt, DB_PREPARE_PERSISTENT, zFormat, ap);
    pStmt->pNext = db.pAllStmt;
    pStmt->pPrev = 0;
    if( db.pAllStmt ) db.pAllStmt->pPrev = pStmt;
    db.pAllStmt = pStmt;
    va_end(ap);
  }
  return rc;
}

/*
** Return the index of a bind parameter
*/
static int paramIdx(Stmt *pStmt, const char *zParamName){
  int i = sqlite3_bind_parameter_index(pStmt->pStmt, zParamName);
  if( i==0 ){
    db_err("no such bind parameter: %s\nSQL: %b", zParamName, &pStmt->sql);
  }
  return i;
}
/*
** Bind an integer, string, or Blob value to a named parameter.
*/
int db_bind_int(Stmt *pStmt, const char *zParamName, int iValue){
  return sqlite3_bind_int(pStmt->pStmt, paramIdx(pStmt, zParamName), iValue);
}
int db_bind_int64(Stmt *pStmt, const char *zParamName, i64 iValue){
  return sqlite3_bind_int64(pStmt->pStmt, paramIdx(pStmt, zParamName), iValue);
}
int db_bind_double(Stmt *pStmt, const char *zParamName, double rValue){
  return sqlite3_bind_double(pStmt->pStmt, paramIdx(pStmt, zParamName), rValue);
}
int db_bind_text(Stmt *pStmt, const char *zParamName, const char *zValue){
  return sqlite3_bind_text(pStmt->pStmt, paramIdx(pStmt, zParamName), zValue,
                           -1, SQLITE_STATIC);
}
int db_bind_text16(Stmt *pStmt, const char *zParamName, const char *zValue){
  return sqlite3_bind_text16(pStmt->pStmt, paramIdx(pStmt, zParamName), zValue,
                             -1, SQLITE_STATIC);
}
int db_bind_null(Stmt *pStmt, const char *zParamName){
  return sqlite3_bind_null(pStmt->pStmt, paramIdx(pStmt, zParamName));
}
int db_bind_blob(Stmt *pStmt, const char *zParamName, Blob *pBlob){
  return sqlite3_bind_blob(pStmt->pStmt, paramIdx(pStmt, zParamName),
                          blob_buffer(pBlob), blob_size(pBlob), SQLITE_STATIC);
}

/* bind_str() treats a Blob object like a TEXT string and binds it
** to the SQL variable.  Contrast this to bind_blob() which treats
** the Blob object like an SQL BLOB.
*/
int db_bind_str(Stmt *pStmt, const char *zParamName, Blob *pBlob){
  return sqlite3_bind_text(pStmt->pStmt, paramIdx(pStmt, zParamName),
                          blob_buffer(pBlob), blob_size(pBlob), SQLITE_STATIC);
}

/*
** Step the SQL statement.  Return either SQLITE_ROW or an error code
** or SQLITE_OK if the statement finishes successfully.
*/
int db_step(Stmt *pStmt){
  int rc;
  rc = sqlite3_step(pStmt->pStmt);
  pStmt->nStep++;
  return rc;
}

/*
** Print warnings if a query is inefficient.
*/
static void db_stats(Stmt *pStmt){
#ifdef FOSSIL_DEBUG
  int c1, c2, c3;
  const char *zSql = sqlite3_sql(pStmt->pStmt);
  if( zSql==0 ) return;
  c1 = sqlite3_stmt_status(pStmt->pStmt, SQLITE_STMTSTATUS_FULLSCAN_STEP, 1);
  c2 = sqlite3_stmt_status(pStmt->pStmt, SQLITE_STMTSTATUS_AUTOINDEX, 1);
  c3 = sqlite3_stmt_status(pStmt->pStmt, SQLITE_STMTSTATUS_SORT, 1);
  if( c1>pStmt->nStep*4 && strstr(zSql,"/*scan*/")==0 ){
    fossil_warning("%d scan steps for %d rows in [%s]", c1, pStmt->nStep, zSql);
  }else if( c2 ){
    fossil_warning("%d automatic index rows in [%s]", c2, zSql);
  }else if( c3 && strstr(zSql,"/*sort*/")==0 && strstr(zSql,"/*scan*/")==0 ){
    fossil_warning("sort w/o index in [%s]", zSql);
  }
  pStmt->nStep = 0;
#endif
}

/*
** Reset or finalize a statement.
*/
int db_reset(Stmt *pStmt){
  int rc;
  db_stats(pStmt);
  rc = sqlite3_reset(pStmt->pStmt);
  db_check_result(rc);
  return rc;
}
int db_finalize(Stmt *pStmt){
  int rc;
  db_stats(pStmt);
  blob_reset(&pStmt->sql);
  rc = sqlite3_finalize(pStmt->pStmt);
  db_check_result(rc);
  pStmt->pStmt = 0;
  if( pStmt->pNext ){
    pStmt->pNext->pPrev = pStmt->pPrev;
  }
  if( pStmt->pPrev ){
    pStmt->pPrev->pNext = pStmt->pNext;
  }else if( db.pAllStmt==pStmt ){
    db.pAllStmt = pStmt->pNext;
  }
  pStmt->pNext = 0;
  pStmt->pPrev = 0;
  return rc;
}

/*
** Return the rowid of the most recent insert
*/
int db_last_insert_rowid(void){
  i64 x = sqlite3_last_insert_rowid(g.db);
  if( x<0 || x>(i64)2147483647 ){
    fossil_fatal("rowid out of range (0..2147483647)");
  }
  return (int)x;
}

/*
** Return the number of rows that were changed by the most recent
** INSERT, UPDATE, or DELETE.  Auxiliary changes caused by triggers
** or other side effects are not counted.
*/
int db_changes(void){
  return sqlite3_changes(g.db);
}

/*
** Extract text, integer, or blob values from the N-th column of the
** current row.
*/
int db_column_type(Stmt *pStmt, int N){
  return sqlite3_column_type(pStmt->pStmt, N);
}
int db_column_bytes(Stmt *pStmt, int N){
  return sqlite3_column_bytes(pStmt->pStmt, N);
}
int db_column_int(Stmt *pStmt, int N){
  return sqlite3_column_int(pStmt->pStmt, N);
}
i64 db_column_int64(Stmt *pStmt, int N){
  return sqlite3_column_int64(pStmt->pStmt, N);
}
double db_column_double(Stmt *pStmt, int N){
  return sqlite3_column_double(pStmt->pStmt, N);
}
const char *db_column_text(Stmt *pStmt, int N){
  return (char*)sqlite3_column_text(pStmt->pStmt, N);
}
const char *db_column_raw(Stmt *pStmt, int N){
  return (const char*)sqlite3_column_blob(pStmt->pStmt, N);
}
const char *db_column_name(Stmt *pStmt, int N){
  return (char*)sqlite3_column_name(pStmt->pStmt, N);
}
int db_column_count(Stmt *pStmt){
  return sqlite3_column_count(pStmt->pStmt);
}
char *db_column_malloc(Stmt *pStmt, int N){
  return mprintf("%s", db_column_text(pStmt, N));
}
void db_column_blob(Stmt *pStmt, int N, Blob *pBlob){
  blob_append(pBlob, sqlite3_column_blob(pStmt->pStmt, N),
              sqlite3_column_bytes(pStmt->pStmt, N));
}

/*
** Initialize a blob to an ephemeral copy of the content of a
** column in the current row.  The data in the blob will become
** invalid when the statement is stepped or reset.
*/
void db_ephemeral_blob(Stmt *pStmt, int N, Blob *pBlob){
  blob_init(pBlob, sqlite3_column_blob(pStmt->pStmt, N),
              sqlite3_column_bytes(pStmt->pStmt, N));
}

/*
** Check a result code.  If it is not SQLITE_OK, print the
** corresponding error message and exit.
*/
void db_check_result(int rc){
  if( rc!=SQLITE_OK ){
    db_err("SQL error: %s", sqlite3_errmsg(g.db));
  }
}

/*
** Execute a single prepared statement until it finishes.
*/
int db_exec(Stmt *pStmt){
  int rc;
  while( (rc = db_step(pStmt))==SQLITE_ROW ){}
  rc = db_reset(pStmt);
  db_check_result(rc);
  return rc;
}

/*
** Print the output of one or more SQL queries on standard output.
** This routine is used for debugging purposes only.
*/
int db_debug(const char *zSql, ...){
  Blob sql;
  int rc = SQLITE_OK;
  va_list ap;
  const char *z, *zEnd;
  sqlite3_stmt *pStmt;
  blob_init(&sql, 0, 0);
  va_start(ap, zSql);
  blob_vappendf(&sql, zSql, ap);
  va_end(ap);
  z = blob_str(&sql);
  while( rc==SQLITE_OK && z[0] ){
    pStmt = 0;
    rc = sqlite3_prepare_v2(g.db, z, -1, &pStmt, &zEnd);
    if( rc!=SQLITE_OK ) break;
    if( pStmt ){
      int nRow = 0;
      db.nPrepare++;
      while( sqlite3_step(pStmt)==SQLITE_ROW ){
        int i, n;
        if( nRow++ > 0 ) fossil_print("\n");
        n = sqlite3_column_count(pStmt);
        for(i=0; i<n; i++){
          fossil_print("%s = %s\n", sqlite3_column_name(pStmt, i),
                       sqlite3_column_text(pStmt,i));
        }
      }
      rc = sqlite3_finalize(pStmt);
      if( rc ) db_err("%s: {%.*s}", sqlite3_errmsg(g.db), (int)(zEnd-z), z);
    }
    z = zEnd;
  }
  blob_reset(&sql);
  return rc;
}

/*
** Execute multiple SQL statements.
*/
int db_multi_exec(const char *zSql, ...){
  Blob sql;
  int rc = SQLITE_OK;
  va_list ap;
  const char *z, *zEnd;
  sqlite3_stmt *pStmt;
  blob_init(&sql, 0, 0);
  va_start(ap, zSql);
  blob_vappendf(&sql, zSql, ap);
  va_end(ap);
  z = blob_str(&sql);
  while( rc==SQLITE_OK && z[0] ){
    pStmt = 0;
    rc = sqlite3_prepare_v2(g.db, z, -1, &pStmt, &zEnd);
    if( rc ){
      db_err("%s: {%s}", sqlite3_errmsg(g.db), z);
    }else if( pStmt ){
      db.nPrepare++;
      while( sqlite3_step(pStmt)==SQLITE_ROW ){}
      rc = sqlite3_finalize(pStmt);
      if( rc ) db_err("%s: {%.*s}", sqlite3_errmsg(g.db), (int)(zEnd-z), z);
    }
    z = zEnd;
  }
  blob_reset(&sql);
  return rc;
}

/*
** Optionally make the following changes to the database if feasible and
** convenient.  Do not start a transaction for these changes, but only
** make these changes if other changes are also being made.
*/
void db_optional_sql(const char *zDb, const char *zSql, ...){
  if( db_is_writeable(zDb) && db.nBeforeCommit < count(db.azBeforeCommit) ){
    va_list ap;
    va_start(ap, zSql);
    db.azBeforeCommit[db.nBeforeCommit++] = sqlite3_vmprintf(zSql, ap);
    va_end(ap);
  }
}

/*
** Execute a query and return a single integer value.
*/
i64 db_int64(i64 iDflt, const char *zSql, ...){
  va_list ap;
  Stmt s;
  i64 rc;
  va_start(ap, zSql);
  db_vprepare(&s, 0, zSql, ap);
  va_end(ap);
  if( db_step(&s)!=SQLITE_ROW ){
    rc = iDflt;
  }else{
    rc = db_column_int64(&s, 0);
  }
  db_finalize(&s);
  return rc;
}
int db_int(int iDflt, const char *zSql, ...){
  va_list ap;
  Stmt s;
  int rc;
  va_start(ap, zSql);
  db_vprepare(&s, 0, zSql, ap);
  va_end(ap);
  if( db_step(&s)!=SQLITE_ROW ){
    rc = iDflt;
  }else{
    rc = db_column_int(&s, 0);
  }
  db_finalize(&s);
  return rc;
}

/*
** Return TRUE if the query would return 1 or more rows.  Return
** FALSE if the query result would be an empty set.
*/
int db_exists(const char *zSql, ...){
  va_list ap;
  Stmt s;
  int rc;
  va_start(ap, zSql);
  db_vprepare(&s, 0, zSql, ap);
  va_end(ap);
  if( db_step(&s)!=SQLITE_ROW ){
    rc = 0;
  }else{
    rc = 1;
  }
  db_finalize(&s);
  return rc;
}


/*
** Execute a query and return a floating-point value.
*/
double db_double(double rDflt, const char *zSql, ...){
  va_list ap;
  Stmt s;
  double r;
  va_start(ap, zSql);
  db_vprepare(&s, 0, zSql, ap);
  va_end(ap);
  if( db_step(&s)!=SQLITE_ROW ){
    r = rDflt;
  }else{
    r = db_column_double(&s, 0);
  }
  db_finalize(&s);
  return r;
}

/*
** Execute a query and append the first column of the first row
** of the result set to blob given in the first argument.
*/
void db_blob(Blob *pResult, const char *zSql, ...){
  va_list ap;
  Stmt s;
  va_start(ap, zSql);
  db_vprepare(&s, 0, zSql, ap);
  va_end(ap);
  if( db_step(&s)==SQLITE_ROW ){
    blob_append(pResult, sqlite3_column_blob(s.pStmt, 0),
                         sqlite3_column_bytes(s.pStmt, 0));
  }
  db_finalize(&s);
}

/*
** Execute a query.  Return the first column of the first row
** of the result set as a string.  Space to hold the string is
** obtained from malloc().  If the result set is empty, return
** zDefault instead.
*/
char *db_text(const char *zDefault, const char *zSql, ...){
  va_list ap;
  Stmt s;
  char *z;
  va_start(ap, zSql);
  db_vprepare(&s, 0, zSql, ap);
  va_end(ap);
  if( db_step(&s)==SQLITE_ROW ){
    z = mprintf("%s", sqlite3_column_text(s.pStmt, 0));
  }else if( zDefault ){
    z = mprintf("%s", zDefault);
  }else{
    z = 0;
  }
  db_finalize(&s);
  return z;
}

/*
** Initialize a new database file with the given schema.  If anything
** goes wrong, call db_err() to exit.
*/
void db_init_database(
  const char *zFileName,   /* Name of database file to create */
  const char *zSchema,     /* First part of schema */
  ...                      /* Additional SQL to run.  Terminate with NULL. */
){
  sqlite3 *db;
  int rc;
  const char *zSql;
  va_list ap;

  db = db_open(zFileName);
  sqlite3_exec(db, "BEGIN EXCLUSIVE", 0, 0, 0);
  rc = sqlite3_exec(db, zSchema, 0, 0, 0);
  if( rc!=SQLITE_OK ){
    db_err("%s", sqlite3_errmsg(db));
  }
  va_start(ap, zSchema);
  while( (zSql = va_arg(ap, const char*))!=0 ){
    rc = sqlite3_exec(db, zSql, 0, 0, 0);
    if( rc!=SQLITE_OK ){
      db_err("%s", sqlite3_errmsg(db));
    }
  }
  va_end(ap);
  sqlite3_exec(db, "COMMIT", 0, 0, 0);
  sqlite3_close(db);
}

/*
** Function to return the number of seconds since 1970.  This is
** the same as strftime('%s','now') but is more compact.
*/
void db_now_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  sqlite3_result_int64(context, time(0));
}

/*
** Function to return the check-in time for a file.
**
**      checkin_mtime(CKINID,RID)
**
** CKINID:  The RID for the manifest for a check-in.
** RID:     The RID of a file in CKINID for which the check-in time
**          is desired.
**
** Returns: The check-in time in seconds since 1970.
*/
void db_checkin_mtime_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  i64 mtime;
  int rc = mtime_of_manifest_file(sqlite3_value_int(argv[0]),
                                  sqlite3_value_int(argv[1]), &mtime);
  if( rc==0 ){
    sqlite3_result_int64(context, mtime);
  }
}

/*
** SQL wrapper around the symbolic_name_to_rid() C-language API.
** Examples:
**
**     symbolic_name_to_rid('trunk');
**     symbolic_name_to_rid('trunk','w');
**
*/
void db_sym2rid_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *arg;
  const char *type;
  if(1 != argc && 2 != argc){
    sqlite3_result_error(context, "Expecting one or two arguments", -1);
    return;
  }
  arg = (const char*)sqlite3_value_text(argv[0]);
  if(!arg){
    sqlite3_result_error(context, "Expecting a STRING argument", -1);
  }else{
    int rid;
    type = (2==argc) ? (const char*)sqlite3_value_text(argv[1]) : 0;
    if(!type) type = "ci";
    rid = symbolic_name_to_rid( arg, type );
    if(rid<0){
      sqlite3_result_error(context, "Symbolic name is ambiguous.", -1);
    }else if(0==rid){
      sqlite3_result_null(context);
    }else{
      sqlite3_result_int64(context, rid);
    }
  }
}

/*
** The toLocal() SQL function returns a string that is an argument to a
** date/time function that is appropriate for modifying the time for display.
** If UTC time display is selected, no modification occurs.  If local time
** display is selected, the time is adjusted appropriately.
**
** Example usage:
**
**         SELECT datetime('now',toLocal());
*/
void db_tolocal_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if( g.fTimeFormat==0 ){
    if( db_get_int("timeline-utc", 1) ){
      g.fTimeFormat = 1;
    }else{
      g.fTimeFormat = 2;
    }
  }
  if( g.fTimeFormat==1 ){
    sqlite3_result_text(context, "0 seconds", -1, SQLITE_STATIC);
  }else{
    sqlite3_result_text(context, "localtime", -1, SQLITE_STATIC);
  }
}

/*
** The fromLocal() SQL function returns a string that is an argument to a
** date/time function that is appropriate to convert an input time to UTC.
** If UTC time display is selected, no modification occurs.  If local time
** display is selected, the time is adjusted from local to UTC.
**
** Example usage:
**
**         SELECT julianday(:user_input,fromLocal());
*/
void db_fromlocal_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if( g.fTimeFormat==0 ){
    if( db_get_int("timeline-utc", 1) ){
      g.fTimeFormat = 1;
    }else{
      g.fTimeFormat = 2;
    }
  }
  if( g.fTimeFormat==1 ){
    sqlite3_result_text(context, "0 seconds", -1, SQLITE_STATIC);
  }else{
    sqlite3_result_text(context, "utc", -1, SQLITE_STATIC);
  }
}


/*
** Register the SQL functions that are useful both to the internal
** representation and to the "fossil sql" command.
*/
void db_add_aux_functions(sqlite3 *db){
  sqlite3_create_function(db, "checkin_mtime", 2, SQLITE_UTF8, 0,
                          db_checkin_mtime_function, 0, 0);
  sqlite3_create_function(db, "symbolic_name_to_rid", 1, SQLITE_UTF8, 0,
                          db_sym2rid_function, 0, 0);
  sqlite3_create_function(db, "symbolic_name_to_rid", 2, SQLITE_UTF8, 0,
                          db_sym2rid_function, 0, 0);
  sqlite3_create_function(db, "now", 0, SQLITE_UTF8, 0,
                          db_now_function, 0, 0);
  sqlite3_create_function(db, "toLocal", 0, SQLITE_UTF8, 0,
                          db_tolocal_function, 0, 0);
  sqlite3_create_function(db, "fromLocal", 0, SQLITE_UTF8, 0,
                          db_fromlocal_function, 0, 0);
}

#if USE_SEE
/*
** This is a pointer to the saved database encryption key string.
*/
static char *zSavedKey = 0;

/*
** This is the size of the saved database encryption key, in bytes.
*/
size_t savedKeySize = 0;

/*
** This function returns the saved database encryption key -OR- zero if
** no database encryption key is saved.
*/
char *db_get_saved_encryption_key(){
  return zSavedKey;
}

/*
** This function returns the size of the saved database encryption key
** -OR- zero if no database encryption key is saved.
*/
size_t db_get_saved_encryption_key_size(){
  return savedKeySize;
}

/*
** This function arranges for the database encryption key to be securely
** saved in non-pagable memory (on platforms where this is possible).
*/
static void db_save_encryption_key(
  Blob *pKey
){
  void *p = NULL;
  size_t n = 0;
  size_t pageSize = 0;
  size_t blobSize = 0;

  blobSize = blob_size(pKey);
  if( blobSize==0 ) return;
  fossil_get_page_size(&pageSize);
  assert( pageSize>0 );
  if( blobSize>pageSize ){
    fossil_fatal("key blob too large: %u versus %u", blobSize, pageSize);
  }
  p = fossil_secure_alloc_page(&n);
  assert( p!=NULL );
  assert( n==pageSize );
  assert( n>=blobSize );
  memcpy(p, blob_str(pKey), blobSize);
  zSavedKey = p;
  savedKeySize = n;
}

/*
** This function arranges for the saved database encryption key to be
** securely zeroed, unlocked (if necessary), and freed.
*/
void db_unsave_encryption_key(){
  fossil_secure_free_page(zSavedKey, savedKeySize);
  zSavedKey = NULL;
  savedKeySize = 0;
}

/*
** This function sets the saved database encryption key to the specified
** string value, allocating or freeing the underlying memory if needed.
*/
void db_set_saved_encryption_key(
  Blob *pKey
){
  if( zSavedKey!=NULL ){
    size_t blobSize = blob_size(pKey);
    if( blobSize==0 ){
      db_unsave_encryption_key();
    }else{
      if( blobSize>savedKeySize ){
        fossil_fatal("key blob too large: %u versus %u",
                     blobSize, savedKeySize);
      }
      fossil_secure_zero(zSavedKey, savedKeySize);
      memcpy(zSavedKey, blob_str(pKey), blobSize);
    }
  }else{
    db_save_encryption_key(pKey);
  }
}

#if defined(_WIN32)
/*
** This function sets the saved database encryption key to one that gets
** read from the specified Fossil parent process.  This is only necessary
** (or functional) on Windows.
*/
void db_read_saved_encryption_key_from_process(
  DWORD processId, /* Identifier for Fossil parent process. */
  LPVOID pAddress, /* Pointer to saved key buffer in the parent process. */
  SIZE_T nSize     /* Size of saved key buffer in the parent process. */
){
  void *p = NULL;
  size_t n = 0;
  size_t pageSize = 0;
  HANDLE hProcess = NULL;

  fossil_get_page_size(&pageSize);
  assert( pageSize>0 );
  if( nSize>pageSize ){
    fossil_fatal("key too large: %u versus %u", nSize, pageSize);
  }
  p = fossil_secure_alloc_page(&n);
  assert( p!=NULL );
  assert( n==pageSize );
  assert( n>=nSize );
  hProcess = OpenProcess(PROCESS_VM_READ, FALSE, processId);
  if( hProcess!=NULL ){
    SIZE_T nRead = 0;
    if( ReadProcessMemory(hProcess, pAddress, p, nSize, &nRead) ){
      CloseHandle(hProcess);
      if( nRead==nSize ){
        db_unsave_encryption_key();
        zSavedKey = p;
        savedKeySize = n;
      }else{
        fossil_fatal("bad size read, %u out of %u bytes at %p from pid %lu",
                     nRead, nSize, pAddress, processId);
      }
    }else{
      CloseHandle(hProcess);
      fossil_fatal("failed read, %u bytes at %p from pid %lu: %lu", nSize,
                   pAddress, processId, GetLastError());
    }
  }else{
    fossil_fatal("failed to open pid %lu: %lu", processId, GetLastError());
  }
}
#endif /* defined(_WIN32) */
#endif /* USE_SEE */

/*
** If the database file zDbFile has a name that suggests that it is
** encrypted, then prompt for the database encryption key and return it
** in the blob *pKey.  Or, if the encryption key has previously been
** requested, just return a copy of the previous result.  The blob in
** *pKey must be initialized.
*/
static void db_maybe_obtain_encryption_key(
  const char *zDbFile,   /* Name of the database file */
  Blob *pKey             /* Put the encryption key here */
){
#if USE_SEE
  if( sqlite3_strglob("*.efossil", zDbFile)==0 ){
    char *zKey = db_get_saved_encryption_key();
    if( zKey ){
      blob_set(pKey, zKey);
    }else{
      char *zPrompt = mprintf("\rencryption key for '%s': ", zDbFile);
      prompt_for_password(zPrompt, pKey, 0);
      fossil_free(zPrompt);
      db_set_saved_encryption_key(pKey);
    }
  }
#endif
}


/*
** Sets the encryption key for the database, if necessary.
*/
void db_maybe_set_encryption_key(sqlite3 *db, const char *zDbName){
  Blob key;
  blob_init(&key, 0, 0);
  db_maybe_obtain_encryption_key(zDbName, &key);
  if( blob_size(&key)>0 ){
    if( fossil_getenv("FOSSIL_USE_SEE_TEXTKEY")==0 ){
      char *zCmd = sqlite3_mprintf("PRAGMA key(%Q)", blob_str(&key));
      sqlite3_exec(db, zCmd, 0, 0, 0);
      fossil_secure_zero(zCmd, strlen(zCmd));
      sqlite3_free(zCmd);
#if USE_SEE
    }else{
      sqlite3_key(db, blob_str(&key), -1);
#endif
    }
  }
  blob_reset(&key);
}

/*
** Open a database file.  Return a pointer to the new database
** connection.  An error results in process abort.
*/
LOCAL sqlite3 *db_open(const char *zDbName){
  int rc;
  sqlite3 *db;

  if( g.fSqlTrace ) fossil_trace("-- sqlite3_open: [%s]\n", zDbName);
  rc = sqlite3_open_v2(
       zDbName, &db,
       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
       g.zVfsName
  );
  if( rc!=SQLITE_OK ){
    db_err("[%s]: %s", zDbName, sqlite3_errmsg(db));
  }
  db_maybe_set_encryption_key(db, zDbName);
  sqlite3_busy_timeout(db, 5000);
  sqlite3_wal_autocheckpoint(db, 1);  /* Set to checkpoint frequently */
  sqlite3_create_function(db, "user", 0, SQLITE_UTF8, 0, db_sql_user, 0, 0);
  sqlite3_create_function(db, "cgi", 1, SQLITE_UTF8, 0, db_sql_cgi, 0, 0);
  sqlite3_create_function(db, "cgi", 2, SQLITE_UTF8, 0, db_sql_cgi, 0, 0);
  sqlite3_create_function(db, "print", -1, SQLITE_UTF8, 0,db_sql_print,0,0);
  sqlite3_create_function(
    db, "is_selected", 1, SQLITE_UTF8, 0, file_is_selected,0,0
  );
  sqlite3_create_function(
    db, "if_selected", 3, SQLITE_UTF8, 0, file_is_selected,0,0
  );
  if( g.fSqlTrace ) sqlite3_trace_v2(db, SQLITE_TRACE_STMT, db_sql_trace, 0);
  db_add_aux_functions(db);
  re_add_sql_func(db);  /* The REGEXP operator */
  foci_register(db);    /* The "files_of_checkin" virtual table */
  sqlite3_exec(db, "PRAGMA foreign_keys=OFF;", 0, 0, 0);
  return db;
}


/*
** Detaches the zLabel database.
*/
void db_detach(const char *zLabel){
  db_multi_exec("DETACH DATABASE %Q", zLabel);
}

/*
** zDbName is the name of a database file.  Attach zDbName using
** the name zLabel.
*/
void db_attach(const char *zDbName, const char *zLabel){
  Blob key;
  blob_init(&key, 0, 0);
  db_maybe_obtain_encryption_key(zDbName, &key);
  if( fossil_getenv("FOSSIL_USE_SEE_TEXTKEY")==0 ){
    char *zCmd = sqlite3_mprintf("ATTACH DATABASE %Q AS %Q KEY %Q",
                                 zDbName, zLabel, blob_str(&key));
    db_multi_exec(zCmd /*works-like:""*/);
    fossil_secure_zero(zCmd, strlen(zCmd));
    sqlite3_free(zCmd);
  }else{
    char *zCmd = sqlite3_mprintf("ATTACH DATABASE %Q AS %Q KEY ''",
                                 zDbName, zLabel);
    db_multi_exec(zCmd /*works-like:""*/);
    sqlite3_free(zCmd);
#if USE_SEE
    if( blob_size(&key)>0 ){
      sqlite3_key_v2(g.db, zLabel, blob_str(&key), -1);
    }
#endif
  }
  blob_reset(&key);
}

/*
** Change the schema name of the "main" database to zLabel.
** zLabel must be a static string that is unchanged for the life of
** the database connection.
**
** After calling this routine, db_database_slot(zLabel) should
** return 0.
*/
void db_set_main_schemaname(sqlite3 *db, const char *zLabel){
  if( sqlite3_db_config(db, SQLITE_DBCONFIG_MAINDBNAME, zLabel) ){
    fossil_fatal("Fossil requires a version of SQLite that supports the "
                 "SQLITE_DBCONFIG_MAINDBNAME interface.");
  }
}

/*
** Return the slot number for database zLabel.  The first database
** opened is slot 0.  The "temp" database is slot 1.  Attached databases
** are slots 2 and higher.
**
** Return -1 if zLabel does not match any open database.
*/
int db_database_slot(const char *zLabel){
  int iSlot = -1;
  int rc;
  Stmt q;
  if( g.db==0 ) return iSlot;
  rc = db_prepare_ignore_error(&q, "PRAGMA database_list");
  if( rc!=SQLITE_OK ) return iSlot;
  while( db_step(&q)==SQLITE_ROW ){
    if( fossil_strcmp(db_column_text(&q,1),zLabel)==0 ){
      iSlot = db_column_int(&q, 0);
      break;
    }
  }
  db_finalize(&q);
  return iSlot;
}

/*
** zDbName is the name of a database file.  If no other database
** file is open, then open this one.  If another database file is
** already open, then attach zDbName using the name zLabel.
*/
void db_open_or_attach(const char *zDbName, const char *zLabel){
  if( !g.db ){
    g.db = db_open(zDbName);
    db_set_main_schemaname(g.db, zLabel);
  }else{
    db_attach(zDbName, zLabel);
  }
}

/*
** Close the per-user database file in ~/.fossil
*/
void db_close_config(){
  int iSlot = db_database_slot("configdb");
  if( iSlot>0 ){
    db_detach("configdb");
    g.zConfigDbName = 0;
  }else if( g.dbConfig ){
    sqlite3_wal_checkpoint(g.dbConfig, 0);
    sqlite3_close(g.dbConfig);
    g.dbConfig = 0;
    g.zConfigDbName = 0;
  }else if( g.db && 0==iSlot ){
    sqlite3_wal_checkpoint(g.db, 0);
    sqlite3_close(g.db);
    g.db = 0;
    g.zConfigDbName = 0;
  }
}

/*
** Open the user database in "~/.fossil".  Create the database anew if
** it does not already exist.
**
** If the useAttach flag is 0 (the usual case) then the user database is
** opened on a separate database connection g.dbConfig.  This prevents
** the ~/.fossil database from becoming locked on long check-in or sync
** operations which hold an exclusive transaction.  In a few cases, though,
** it is convenient for the ~/.fossil to be attached to the main database
** connection so that we can join between the various databases.  In that
** case, invoke this routine with useAttach as 1.
*/
int db_open_config(int useAttach, int isOptional){
  char *zDbName;
  char *zHome;
  if( g.zConfigDbName ){
    int alreadyAttached = db_database_slot("configdb")>0;
    if( useAttach==alreadyAttached ) return 1; /* Already open. */
    db_close_config();
  }
  zHome = fossil_getenv("FOSSIL_HOME");
#if defined(_WIN32) || defined(__CYGWIN__)
  if( zHome==0 ){
    zHome = fossil_getenv("LOCALAPPDATA");
    if( zHome==0 ){
      zHome = fossil_getenv("APPDATA");
      if( zHome==0 ){
        char *zDrive = fossil_getenv("HOMEDRIVE");
        char *zPath = fossil_getenv("HOMEPATH");
        if( zDrive && zPath ) zHome = mprintf("%s%s", zDrive, zPath);
      }
    }
  }
  if( zHome==0 ){
    if( isOptional ) return 0;
    fossil_fatal("cannot locate home directory - please set the "
                 "FOSSIL_HOME, LOCALAPPDATA, APPDATA, or HOMEPATH "
                 "environment variables");
  }
#else
  if( zHome==0 ){
    zHome = fossil_getenv("HOME");
  }
  if( zHome==0 ){
    if( isOptional ) return 0;
    fossil_fatal("cannot locate home directory - please set the "
                 "FOSSIL_HOME or HOME environment variables");
  }
#endif
  if( file_isdir(zHome)!=1 ){
    if( isOptional ) return 0;
    fossil_fatal("invalid home directory: %s", zHome);
  }
#if defined(_WIN32) || defined(__CYGWIN__)
  /* . filenames give some window systems problems and many apps problems */
  zDbName = mprintf("%//_fossil", zHome);
#else
  zDbName = mprintf("%s/.fossil", zHome);
#endif
  if( file_size(zDbName)<1024*3 ){
    if( file_access(zHome, W_OK) ){
      if( isOptional ) return 0;
      fossil_fatal("home directory %s must be writeable", zHome);
    }
    db_init_database(zDbName, zConfigSchema, (char*)0);
  }
  if( file_access(zDbName, W_OK) ){
    if( isOptional ) return 0;
    fossil_fatal("configuration file %s must be writeable", zDbName);
  }
  if( useAttach ){
    db_open_or_attach(zDbName, "configdb");
    g.dbConfig = 0;
  }else{
    g.dbConfig = db_open(zDbName);
    db_set_main_schemaname(g.dbConfig, "configdb");
  }
  g.zConfigDbName = zDbName;
  return 1;
}

/*
** Return TRUE if zTable exists.
*/
int db_table_exists(
  const char *zDb,      /* One of: NULL, "configdb", "localdb", "repository" */
  const char *zTable    /* Name of table */
){
  return sqlite3_table_column_metadata(g.db, zDb, zTable, 0,
                                       0, 0, 0, 0, 0)==SQLITE_OK;
}

/*
** Return TRUE if zTable exists and contains column zColumn.
** Return FALSE if zTable does not exist or if zTable exists
** but lacks zColumn.
*/
int db_table_has_column(
  const char *zDb,       /* One of: NULL, "config", "localdb", "repository" */
  const char *zTable,    /* Name of table */
  const char *zColumn    /* Name of column in table */
){
  return sqlite3_table_column_metadata(g.db, zDb, zTable, zColumn,
                                       0, 0, 0, 0, 0)==SQLITE_OK;
}

/*
** Returns TRUE if zTable exists in the local database but lacks column
** zColumn
*/
static int db_local_table_exists_but_lacks_column(
  const char *zTable,
  const char *zColumn
){
  return db_table_exists("localdb", zTable)
      && !db_table_has_column("localdb", zTable, zColumn);
}

/*
** If zDbName is a valid local database file, open it and return
** true.  If it is not a valid local database file, return 0.
*/
static int isValidLocalDb(const char *zDbName){
  i64 lsize;
  char *zVFileDef;

  if( file_access(zDbName, F_OK) ) return 0;
  lsize = file_size(zDbName);
  if( lsize%1024!=0 || lsize<4096 ) return 0;
  db_open_or_attach(zDbName, "localdb");
  zVFileDef = db_text(0, "SELECT sql FROM localdb.sqlite_master"
                         " WHERE name=='vfile'");
  if( zVFileDef==0 ) return 0;

  /* If the "isexe" column is missing from the vfile table, then
  ** add it now.   This code added on 2010-03-06.  After all users have
  ** upgraded, this code can be safely deleted.
  */
  if( sqlite3_strglob("* isexe *", zVFileDef)!=0 ){
    db_multi_exec("ALTER TABLE vfile ADD COLUMN isexe BOOLEAN DEFAULT 0");
  }

  /* If "islink"/"isLink" columns are missing from tables, then
  ** add them now.   This code added on 2011-01-17 and 2011-08-27.
  ** After all users have upgraded, this code can be safely deleted.
  */
  if( sqlite3_strglob("* islink *", zVFileDef)!=0 ){
    db_multi_exec("ALTER TABLE vfile ADD COLUMN islink BOOLEAN DEFAULT 0");
    if( db_local_table_exists_but_lacks_column("stashfile", "isLink") ){
      db_multi_exec("ALTER TABLE stashfile ADD COLUMN isLink BOOL DEFAULT 0");
    }
    if( db_local_table_exists_but_lacks_column("undo", "isLink") ){
      db_multi_exec("ALTER TABLE undo ADD COLUMN isLink BOOLEAN DEFAULT 0");
    }
    if( db_local_table_exists_but_lacks_column("undo_vfile", "islink") ){
      db_multi_exec("ALTER TABLE undo_vfile ADD COLUMN islink BOOL DEFAULT 0");
    }
  }
  fossil_free(zVFileDef);
  return 1;
}

/*
** Locate the root directory of the local repository tree.  The root
** directory is found by searching for a file named "_FOSSIL_" or ".fslckout"
** that contains a valid repository database.
**
** For legacy, also look for ".fos".  The use of ".fos" is deprecated
** since "fos" has negative connotations in Hungarian, we are told.
**
** If no valid _FOSSIL_ or .fslckout file is found, we move up one level and
** try again. Once the file is found, the g.zLocalRoot variable is set
** to the root of the repository tree and this routine returns 1.  If
** no database is found, then this routine return 0.
**
** This routine always opens the user database regardless of whether or
** not the repository database is found.  If the _FOSSIL_ or .fslckout file
** is found, it is attached to the open database connection too.
*/
int db_open_local(const char *zDbName){
  int i, n;
  char zPwd[2000];
  static const char *(aDbName[]) = { "_FOSSIL_", ".fslckout", ".fos" };

  if( g.localOpen ) return 1;
  file_getcwd(zPwd, sizeof(zPwd)-20);
  n = strlen(zPwd);
  while( n>0 ){
    for(i=0; i<count(aDbName); i++){
      sqlite3_snprintf(sizeof(zPwd)-n, &zPwd[n], "/%s", aDbName[i]);
      if( isValidLocalDb(zPwd) ){
        if( db_open_config(0, 1)==0 ){
          return 0; /* Configuration could not be opened */
        }
        /* Found a valid checkout database file */
        g.zLocalDbName = mprintf("%s", zPwd);
        zPwd[n] = 0;
        while( n>0 && zPwd[n-1]=='/' ){
          n--;
          zPwd[n] = 0;
        }
        g.zLocalRoot = mprintf("%s/", zPwd);
        g.localOpen = 1;
        db_open_repository(zDbName);
        return 1;
      }
    }
    n--;
    while( n>1 && zPwd[n]!='/' ){ n--; }
    while( n>1 && zPwd[n-1]=='/' ){ n--; }
    zPwd[n] = 0;
  }

  /* A checkout database file could not be found */
  return 0;
}

/*
** Get the full pathname to the repository database file.  The
** local database (the _FOSSIL_ or .fslckout database) must have already
** been opened before this routine is called.
*/
const char *db_repository_filename(void){
  static char *zRepo = 0;
  assert( g.localOpen );
  assert( g.zLocalRoot );
  if( zRepo==0 ){
    zRepo = db_lget("repository", 0);
    if( zRepo && !file_is_absolute_path(zRepo) ){
      zRepo = mprintf("%s%s", g.zLocalRoot, zRepo);
    }
  }
  return zRepo;
}

/*
** Returns non-zero if the default value for the "allow-symlinks" setting
** is "on".  When on Windows, this always returns false.
*/
int db_allow_symlinks_by_default(void){
#if defined(_WIN32)
  return 0;
#else
  return 1;
#endif
}

/*
** Returns non-zero if support for symlinks is currently enabled.
*/
int db_allow_symlinks(void){
  return g.allowSymlinks;
}

/*
** Open the repository database given by zDbName.  If zDbName==NULL then
** get the name from the already open local database.
*/
void db_open_repository(const char *zDbName){
  if( g.repositoryOpen ) return;
  if( zDbName==0 ){
    if( g.localOpen ){
      zDbName = db_repository_filename();
    }
    if( zDbName==0 ){
      db_err("unable to find the name of a repository database");
    }
  }
  if( file_access(zDbName, R_OK) || file_size(zDbName)<1024 ){
    if( file_access(zDbName, F_OK) ){
#ifdef FOSSIL_ENABLE_JSON
      g.json.resultCode = FSL_JSON_E_DB_NOT_FOUND;
#endif
      fossil_panic("repository does not exist or"
                   " is in an unreadable directory: %s", zDbName);
    }else if( file_access(zDbName, R_OK) ){
#ifdef FOSSIL_ENABLE_JSON
      g.json.resultCode = FSL_JSON_E_DENIED;
#endif
      fossil_panic("read permission denied for repository %s", zDbName);
    }else{
#ifdef FOSSIL_ENABLE_JSON
      g.json.resultCode = FSL_JSON_E_DB_NOT_VALID;
#endif
      fossil_panic("not a valid repository: %s", zDbName);
    }
  }
  g.zRepositoryName = mprintf("%s", zDbName);
  db_open_or_attach(g.zRepositoryName, "repository");
  g.repositoryOpen = 1;
  /* Cache "allow-symlinks" option, because we'll need it on every stat call */
  g.allowSymlinks = db_get_boolean("allow-symlinks",
                                   db_allow_symlinks_by_default());
  g.zAuxSchema = db_get("aux-schema","");
  g.eHashPolicy = db_get_int("hash-policy",-1);
  if( g.eHashPolicy<0 ){
    g.eHashPolicy = hname_default_policy();
    db_set_int("hash-policy", g.eHashPolicy, 0);
  }

  /* Make a change to the CHECK constraint on the BLOB table for
  ** version 2.0 and later.
  */
  rebuild_schema_update_2_0();   /* Do the Fossil-2.0 schema updates */
}

/*
** Flags for the db_find_and_open_repository() function.
*/
#if INTERFACE
#define OPEN_OK_NOT_FOUND    0x001      /* Do not error out if not found */
#define OPEN_ANY_SCHEMA      0x002      /* Do not error if schema is wrong */
#endif

/*
** Try to find the repository and open it.  Use the -R or --repository
** option to locate the repository.  If no such option is available, then
** use the repository of the open checkout if there is one.
**
** Error out if the repository cannot be opened.
*/
void db_find_and_open_repository(int bFlags, int nArgUsed){
  const char *zRep = find_repository_option();
  if( zRep && file_isdir(zRep)==1 ){
    goto rep_not_found;
  }
  if( zRep==0 && nArgUsed && g.argc==nArgUsed+1 ){
    zRep = g.argv[nArgUsed];
  }
  if( zRep==0 ){
    if( db_open_local(0)==0 ){
      goto rep_not_found;
    }
    zRep = db_repository_filename();
    if( zRep==0 ){
      goto rep_not_found;
    }
  }
  db_open_repository(zRep);
  if( g.repositoryOpen ){
    if( (bFlags & OPEN_ANY_SCHEMA)==0 ) db_verify_schema();
    return;
  }
rep_not_found:
  if( (bFlags & OPEN_OK_NOT_FOUND)==0 ){
#ifdef FOSSIL_ENABLE_JSON
    g.json.resultCode = FSL_JSON_E_DB_NOT_FOUND;
#endif
    if( nArgUsed==0 ){
      fossil_fatal("use --repository or -R to specify the repository database");
    }else{
      fossil_fatal("specify the repository name as a command-line argument");
    }
  }
}

/*
** Return TRUE if the schema is out-of-date
*/
int db_schema_is_outofdate(void){
  return strcmp(g.zAuxSchema,AUX_SCHEMA_MIN)<0
      || strcmp(g.zAuxSchema,AUX_SCHEMA_MAX)>0;
}

/*
** Return true if the database is writeable
*/
int db_is_writeable(const char *zName){
  return g.db!=0 && !sqlite3_db_readonly(g.db, zName);
}

/*
** Verify that the repository schema is correct.  If it is not correct,
** issue a fatal error and die.
*/
void db_verify_schema(void){
  if( db_schema_is_outofdate() ){
#ifdef FOSSIL_ENABLE_JSON
    g.json.resultCode = FSL_JSON_E_DB_NEEDS_REBUILD;
#endif
    fossil_warning("incorrect repository schema version: "
          "current repository schema version is \"%s\" "
          "but need versions between \"%s\" and \"%s\".",
          g.zAuxSchema, AUX_SCHEMA_MIN, AUX_SCHEMA_MAX);
    fossil_fatal("run \"fossil rebuild\" to fix this problem");
  }
}


/*
** COMMAND: test-move-repository
**
** Usage: %fossil test-move-repository PATHNAME
**
** Change the location of the repository database on a local check-out.
** Use this command to avoid having to close and reopen a checkout
** when relocating the repository database.
*/
void move_repo_cmd(void){
  Blob repo;
  char *zRepo;
  if( g.argc!=3 ){
    usage("PATHNAME");
  }
  file_canonical_name(g.argv[2], &repo, 0);
  zRepo = blob_str(&repo);
  if( file_access(zRepo, F_OK) ){
    fossil_fatal("no such file: %s", zRepo);
  }
  if( db_open_local(zRepo)==0 ){
    fossil_fatal("not in a local checkout");
    return;
  }
  db_open_or_attach(zRepo, "test_repo");
  db_lset("repository", blob_str(&repo));
  db_record_repository_filename(blob_str(&repo));
  db_close(1);
}


/*
** Open the local database.  If unable, exit with an error.
*/
void db_must_be_within_tree(void){
  if( find_repository_option() ){
    fossil_fatal("the \"%s\" command only works from within an open check-out",
                 g.argv[1]);
  }
  if( db_open_local(0)==0 ){
    fossil_fatal("current directory is not within an open checkout");
  }
  db_open_repository(0);
  db_verify_schema();
}

/*
** Close the database connection.
**
** Check for unfinalized statements and report errors if the reportErrors
** argument is true.  Ignore unfinalized statements when false.
*/
void db_close(int reportErrors){
  sqlite3_stmt *pStmt;
  if( g.db==0 ) return;
  if( g.fSqlStats ){
    int cur, hiwtr;
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_LOOKASIDE_USED, &cur, &hiwtr, 0);
    fprintf(stderr, "-- LOOKASIDE_USED         %10d %10d\n", cur, hiwtr);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_LOOKASIDE_HIT, &cur, &hiwtr, 0);
    fprintf(stderr, "-- LOOKASIDE_HIT                     %10d\n", hiwtr);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE, &cur,&hiwtr,0);
    fprintf(stderr, "-- LOOKASIDE_MISS_SIZE               %10d\n", hiwtr);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL, &cur,&hiwtr,0);
    fprintf(stderr, "-- LOOKASIDE_MISS_FULL               %10d\n", hiwtr);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_CACHE_USED, &cur, &hiwtr, 0);
    fprintf(stderr, "-- CACHE_USED             %10d\n", cur);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_SCHEMA_USED, &cur, &hiwtr, 0);
    fprintf(stderr, "-- SCHEMA_USED            %10d\n", cur);
    sqlite3_db_status(g.db, SQLITE_DBSTATUS_STMT_USED, &cur, &hiwtr, 0);
    fprintf(stderr, "-- STMT_USED              %10d\n", cur);
    sqlite3_status(SQLITE_STATUS_MEMORY_USED, &cur, &hiwtr, 0);
    fprintf(stderr, "-- MEMORY_USED            %10d %10d\n", cur, hiwtr);
    sqlite3_status(SQLITE_STATUS_MALLOC_SIZE, &cur, &hiwtr, 0);
    fprintf(stderr, "-- MALLOC_SIZE                       %10d\n", hiwtr);
    sqlite3_status(SQLITE_STATUS_MALLOC_COUNT, &cur, &hiwtr, 0);
    fprintf(stderr, "-- MALLOC_COUNT           %10d %10d\n", cur, hiwtr);
    sqlite3_status(SQLITE_STATUS_PAGECACHE_OVERFLOW, &cur, &hiwtr, 0);
    fprintf(stderr, "-- PCACHE_OVFLOW          %10d %10d\n", cur, hiwtr);
    fprintf(stderr, "-- prepared statements    %10d\n", db.nPrepare);
  }
  while( db.pAllStmt ){
    db_finalize(db.pAllStmt);
  }
  db_end_transaction(1);
  pStmt = 0;
  g.dbIgnoreErrors++;  /* Stop "database locked" warnings from PRAGMA optimize */
  sqlite3_exec(g.db, "PRAGMA optimize", 0, 0, 0);
  g.dbIgnoreErrors--;
  db_close_config();

  /* If the localdb has a lot of unused free space,
  ** then VACUUM it as we shut down.
  */
  if( db_database_slot("localdb")>=0 ){
    int nFree = db_int(0, "PRAGMA localdb.freelist_count");
    int nTotal = db_int(0, "PRAGMA localdb.page_count");
    if( nFree>nTotal/4 ){
      db_multi_exec("VACUUM localdb;");
    }
  }

  if( g.db ){
    int rc;
    sqlite3_wal_checkpoint(g.db, 0);
    rc = sqlite3_close(g.db);
    if( rc==SQLITE_BUSY && reportErrors ){
      while( (pStmt = sqlite3_next_stmt(g.db, pStmt))!=0 ){
        fossil_warning("unfinalized SQL statement: [%s]", sqlite3_sql(pStmt));
      }
    }
    g.db = 0;
  }
  g.repositoryOpen = 0;
  g.localOpen = 0;
  assert( g.dbConfig==0 );
  assert( g.zConfigDbName==0 );
}


/*
** Create a new empty repository database with the given name.
**
** Only the schema is initialized.  The required VAR tables entries
** are not set by this routine and must be set separately in order
** to make the new file a valid database.
*/
void db_create_repository(const char *zFilename){
  db_init_database(
     zFilename,
     zRepositorySchema1,
     zRepositorySchemaDefaultReports,
     zRepositorySchema2,
     (char*)0
  );
  db_delete_on_failure(zFilename);
}

/*
** Create the default user accounts in the USER table.
*/
void db_create_default_users(int setupUserOnly, const char *zDefaultUser){
  const char *zUser = zDefaultUser;
  if( zUser==0 ){
    zUser = db_get("default-user", 0);
  }
  if( zUser==0 ){
    zUser = fossil_getenv("FOSSIL_USER");
  }
  if( zUser==0 ){
    zUser = fossil_getenv("USER");
  }
  if( zUser==0 ){
    zUser = fossil_getenv("LOGNAME");
  }
  if( zUser==0 ){
    zUser = fossil_getenv("USERNAME");
  }
  if( zUser==0 ){
    zUser = "root";
  }
  db_multi_exec(
     "INSERT OR IGNORE INTO user(login, info) VALUES(%Q,'')", zUser
  );
  db_multi_exec(
     "UPDATE user SET cap='s', pw=lower(hex(randomblob(3)))"
     " WHERE login=%Q", zUser
  );
  if( !setupUserOnly ){
    db_multi_exec(
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('anonymous',hex(randomblob(8)),'hmnc','Anon');"
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('nobody','','gjorz','Nobody');"
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('developer','','dei','Dev');"
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('reader','','kptw','Reader');"
    );
  }
}

/*
** Return a pointer to a string that contains the RHS of an IN operator
** that will select CONFIG table names that are in the list of control
** settings.
*/
const char *db_setting_inop_rhs(){
  Blob x;
  int i;
  int nSetting;
  const Setting *aSetting = setting_info(&nSetting);
  const char *zSep = "";

  blob_zero(&x);
  blob_append_sql(&x, "(");
  for(i=0; i<nSetting; i++){
    blob_append_sql(&x, "%s%Q", zSep/*safe-for-%s*/, aSetting[i].name);
    zSep = ",";
  }
  blob_append_sql(&x, ")");
  return blob_sql_text(&x);
}

/*
** Fill an empty repository database with the basic information for a
** repository. This function is shared between 'create_repository_cmd'
** ('new') and 'reconstruct_cmd' ('reconstruct'), both of which create
** new repositories.
**
** The zTemplate parameter determines if the settings for the repository
** should be copied from another repository.  If zTemplate is 0 then the
** settings will have their normal default values.  If zTemplate is
** non-zero, it is assumed that the caller of this function has already
** attached a database using the label "settingSrc".  If not, the call to
** this function will fail.
**
** The zInitialDate parameter determines the date of the initial check-in
** that is automatically created.  If zInitialDate is 0 then no initial
** check-in is created. The makeServerCodes flag determines whether or
** not server and project codes are invented for this repository.
*/
void db_initial_setup(
  const char *zTemplate,       /* Repository from which to copy settings. */
  const char *zInitialDate,    /* Initial date of repository. (ex: "now") */
  const char *zDefaultUser     /* Default user for the repository */
){
  char *zDate;
  Blob hash;
  Blob manifest;

  db_set("content-schema", CONTENT_SCHEMA, 0);
  db_set("aux-schema", AUX_SCHEMA_MAX, 0);
  db_set("rebuilt", get_version(), 0);
  db_set("admin-log", "1", 0);
  db_set("access-log", "1", 0);
  db_multi_exec(
      "INSERT INTO config(name,value,mtime)"
      " VALUES('server-code', lower(hex(randomblob(20))),now());"
      "INSERT INTO config(name,value,mtime)"
      " VALUES('project-code', lower(hex(randomblob(20))),now());"
  );
  if( !db_is_global("autosync") ) db_set_int("autosync", 1, 0);
  if( !db_is_global("localauth") ) db_set_int("localauth", 0, 0);
  if( !db_is_global("timeline-plaintext") ){
    db_set_int("timeline-plaintext", 1, 0);
  }
  db_create_default_users(0, zDefaultUser);
  if( zDefaultUser ) g.zLogin = zDefaultUser;
  user_select();

  if( zTemplate ){
    /*
    ** Copy all settings from the supplied template repository.
    */
    db_multi_exec(
      "INSERT OR REPLACE INTO config"
      " SELECT name,value,mtime FROM settingSrc.config"
      "  WHERE (name IN %s OR name IN %s OR name GLOB 'walias:/*')"
      "    AND name NOT GLOB 'project-*'"
      "    AND name NOT GLOB 'short-project-*';",
      configure_inop_rhs(CONFIGSET_ALL),
      db_setting_inop_rhs()
    );
    g.eHashPolicy = db_get_int("hash-policy", g.eHashPolicy);
    db_multi_exec(
      "REPLACE INTO reportfmt SELECT * FROM settingSrc.reportfmt;"
    );

    /*
    ** Copy the user permissions, contact information, last modified
    ** time, and photo for all the "system" users from the supplied
    ** template repository into the one being setup.  The other columns
    ** are not copied because they contain security information or other
    ** data specific to the other repository.  The list of columns copied
    ** by this SQL statement may need to be revised in the future.
    */
    db_multi_exec("UPDATE user SET"
      "  cap = (SELECT u2.cap FROM settingSrc.user u2"
      "         WHERE u2.login = user.login),"
      "  info = (SELECT u2.info FROM settingSrc.user u2"
      "          WHERE u2.login = user.login),"
      "  mtime = (SELECT u2.mtime FROM settingSrc.user u2"
      "           WHERE u2.login = user.login),"
      "  photo = (SELECT u2.photo FROM settingSrc.user u2"
      "           WHERE u2.login = user.login)"
      " WHERE user.login IN ('anonymous','nobody','developer','reader');"
    );
  }

  if( zInitialDate ){
    int rid;
    blob_zero(&manifest);
    blob_appendf(&manifest, "C initial\\sempty\\scheck-in\n");
    zDate = date_in_standard_format(zInitialDate);
    blob_appendf(&manifest, "D %s\n", zDate);
    md5sum_init();
    /* The R-card is necessary here because without it
     * fossil versions earlier than versions 1.27 would
     * interpret this artifact as a "control". */
    blob_appendf(&manifest, "R %s\n", md5sum_finish(0));
    blob_appendf(&manifest, "T *branch * trunk\n");
    blob_appendf(&manifest, "T *sym-trunk *\n");
    blob_appendf(&manifest, "U %F\n", g.zLogin);
    md5sum_blob(&manifest, &hash);
    blob_appendf(&manifest, "Z %b\n", &hash);
    blob_reset(&hash);
    rid = content_put(&manifest);
    manifest_crosslink(rid, &manifest, MC_NONE);
  }
}

/*
** COMMAND: new*
** COMMAND: init
**
** Usage: %fossil new ?OPTIONS? FILENAME
**    or: %fossil init ?OPTIONS? FILENAME
**
** Create a repository for a new project in the file named FILENAME.
** This command is distinct from "clone".  The "clone" command makes
** a copy of an existing project.  This command starts a new project.
**
** By default, your current login name is used to create the default
** admin user. This can be overridden using the -A|--admin-user
** parameter.
**
** By default, all settings will be initialized to their default values.
** This can be overridden using the --template parameter to specify a
** repository file from which to copy the initial settings.  When a template
** repository is used, almost all of the settings accessible from the setup
** page, either directly or indirectly, will be copied.  Normal users and
** their associated permissions will not be copied; however, the system
** default users "anonymous", "nobody", "reader", "developer", and their
** associated permissions will be copied.
**
** Options:
**    --template      FILE         Copy settings from repository file
**    --admin-user|-A USERNAME     Select given USERNAME as admin user
**    --date-override DATETIME     Use DATETIME as time of the initial check-in
**    --sha1                       Use a initial hash policy of "sha1"
**
** DATETIME may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
** year-month-day form, it may be truncated, the "T" may be replaced by
** a space, and it may also name a timezone offset from UTC as "-HH:MM"
** (westward) or "+HH:MM" (eastward). Either no timezone suffix or "Z"
** means UTC.
**
** See also: clone
*/
void create_repository_cmd(void){
  char *zPassword;
  const char *zTemplate;      /* Repository from which to copy settings */
  const char *zDate;          /* Date of the initial check-in */
  const char *zDefaultUser;   /* Optional name of the default user */
  int bUseSha1 = 0;           /* True to set the hash-policy to sha1 */


  zTemplate = find_option("template",0,1);
  zDate = find_option("date-override",0,1);
  zDefaultUser = find_option("admin-user","A",1);
  bUseSha1 = find_option("sha1",0,0)!=0;
  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=3 ){
    usage("REPOSITORY-NAME");
  }

  if( -1 != file_size(g.argv[2]) ){
    fossil_fatal("file already exists: %s", g.argv[2]);
  }

  db_create_repository(g.argv[2]);
  db_open_repository(g.argv[2]);
  db_open_config(0, 0);
  if( zTemplate ) db_attach(zTemplate, "settingSrc");
  db_begin_transaction();
  if( bUseSha1 ){
    g.eHashPolicy = HPOLICY_SHA1;
    db_set_int("hash-policy", HPOLICY_SHA1, 0);
  }
  if( zDate==0 ) zDate = "now";
  db_initial_setup(zTemplate, zDate, zDefaultUser);
  db_end_transaction(0);
  if( zTemplate ) db_detach("settingSrc");
  fossil_print("project-id: %s\n", db_get("project-code", 0));
  fossil_print("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  fossil_print("admin-user: %s (initial password is \"%s\")\n",
               g.zLogin, zPassword);
}

/*
** SQL functions for debugging.
**
** The print() function writes its arguments on stdout, but only
** if the -sqlprint command-line option is turned on.
*/
LOCAL void db_sql_print(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int i;
  if( g.fSqlPrint ){
    for(i=0; i<argc; i++){
      char c = i==argc-1 ? '\n' : ' ';
      fossil_print("%s%c", sqlite3_value_text(argv[i]), c);
    }
  }
}
LOCAL int db_sql_trace(unsigned m, void *notUsed, void *pP, void *pX){
  sqlite3_stmt *pStmt = (sqlite3_stmt*)pP;
  char *zSql;
  int n;
  const char *zArg = (const char*)pX;
  if( zArg[0]=='-' ) return 0;
  zSql = sqlite3_expanded_sql(pStmt);
  n = (int)strlen(zSql);
  fossil_trace("%s%s\n", zSql, (n>0 && zSql[n-1]==';') ? "" : ";");
  sqlite3_free(zSql);
  return 0;
}

/*
** Implement the user() SQL function.  user() takes no arguments and
** returns the user ID of the current user.
*/
LOCAL void db_sql_user(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if( g.zLogin!=0 ){
    sqlite3_result_text(context, g.zLogin, -1, SQLITE_STATIC);
  }
}

/*
** Implement the cgi() SQL function.  cgi() takes an argument which is
** a name of CGI query parameter. The value of that parameter is returned,
** if available. Optional second argument will be returned if the first
** doesn't exist as a CGI parameter.
*/
LOCAL void db_sql_cgi(sqlite3_context *context, int argc, sqlite3_value **argv){
  const char* zP;
  if( argc!=1 && argc!=2 ) return;
  zP = P((const char*)sqlite3_value_text(argv[0]));
  if( zP ){
    sqlite3_result_text(context, zP, -1, SQLITE_STATIC);
  }else if( argc==2 ){
    zP = (const char*)sqlite3_value_text(argv[1]);
    if( zP ) sqlite3_result_text(context, zP, -1, SQLITE_TRANSIENT);
  }
}

/*
** SQL function:
**
**       is_selected(id)
**       if_selected(id, X, Y)
**
** On the commit command, when filenames are specified (in order to do
** a partial commit) the vfile.id values for the named files are loaded
** into the g.aCommitFile[] array.  This function looks at that array
** to see if a file is named on the command-line.
**
** In the first form (1 argument) return TRUE if either no files are
** named on the command line (g.aCommitFile is NULL meaning that all
** changes are to be committed) or if id is found in g.aCommitFile[]
** (meaning that id was named on the command-line).
**
** In the second form (3 arguments) return argument X if true and Y
** if false.  Except if Y is NULL then always return X.
*/
LOCAL void file_is_selected(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int rc = 0;

  assert(argc==1 || argc==3);
  if( g.aCommitFile ){
    int iId = sqlite3_value_int(argv[0]);
    int ii;
    for(ii=0; g.aCommitFile[ii]; ii++){
      if( iId==g.aCommitFile[ii] ){
        rc = 1;
        break;
      }
    }
  }else{
    rc = 1;
  }
  if( argc==1 ){
    sqlite3_result_int(context, rc);
  }else{
    assert( argc==3 );
    assert( rc==0 || rc==1 );
    if( sqlite3_value_type(argv[2-rc])==SQLITE_NULL ) rc = 1-rc;
    sqlite3_result_value(context, argv[2-rc]);
  }
}

/*
** Convert the input string into a artifact hash.  Make a notation in the
** CONCEALED table so that the hash can be undo using the db_reveal()
** function at some later time.
**
** The value returned is stored in static space and will be overwritten
** on subsequent calls.
**
** If zContent is already a well-formed artifact hash, then return a copy
** of that hash, not a hash of the hash.
**
** The CONCEALED table is meant to obscure email addresses.  Every valid
** email address will contain a "@" character and "@" is not valid within
** a SHA1 hash so there is no chance that a valid email address will go
** unconcealed.
*/
char *db_conceal(const char *zContent, int n){
  static char zHash[HNAME_MAX+1];
  Blob out;
  if( hname_validate(zContent, n) ){
    memcpy(zHash, zContent, n);
    zHash[n] = 0;
  }else{
    sha1sum_step_text(zContent, n);
    sha1sum_finish(&out);
    sqlite3_snprintf(sizeof(zHash), zHash, "%s", blob_str(&out));
    blob_reset(&out);
    db_multi_exec(
       "INSERT OR IGNORE INTO concealed(hash,content,mtime)"
       " VALUES(%Q,%#Q,now())",
       zHash, n, zContent
    );
  }
  return zHash;
}

/*
** Attempt to look up the input in the CONCEALED table.  If found,
** and if the okRdAddr permission is enabled then return the
** original value for which the input is a hash.  If okRdAddr is
** false or if the lookup fails, return the original string content.
**
** In either case, the string returned is stored in space obtained
** from malloc and should be freed by the calling function.
*/
char *db_reveal(const char *zKey){
  char *zOut;
  if( g.perm.RdAddr ){
    zOut = db_text(0, "SELECT content FROM concealed WHERE hash=%Q", zKey);
  }else{
    zOut = 0;
  }
  if( zOut==0 ){
    zOut = mprintf("%s", zKey);
  }
  return zOut;
}

/*
** Return true if the string zVal represents "true" (or "false").
*/
int is_truth(const char *zVal){
  static const char *const azOn[] = { "on", "yes", "true", "1" };
  int i;
  for(i=0; i<count(azOn); i++){
    if( fossil_stricmp(zVal,azOn[i])==0 ) return 1;
  }
  return 0;
}
int is_false(const char *zVal){
  static const char *const azOff[] = { "off", "no", "false", "0" };
  int i;
  for(i=0; i<count(azOff); i++){
    if( fossil_stricmp(zVal,azOff[i])==0 ) return 1;
  }
  return 0;
}

/*
** Swap the g.db and g.dbConfig connections so that the various db_* routines
** work on the ~/.fossil database instead of on the repository database.
** Be sure to swap them back after doing the operation.
**
** If the ~/.fossil database has already been opened as the main database or
** is attached to the main database, no connection swaps are required so this
** routine is a no-op.
*/
void db_swap_connections(void){
  /*
  ** When swapping the main database connection with the config database
  ** connection, the config database connection must be open (not simply
  ** attached); otherwise, the swap would end up leaving the main database
  ** connection invalid, defeating the very purpose of this routine.  This
  ** same constraint also holds true when restoring the previously swapped
  ** database connection; otherwise, it means that no swap was performed
  ** because the main database connection was already pointing to the config
  ** database.
  */
  if( g.dbConfig ){
    sqlite3 *dbTemp = g.db;
    g.db = g.dbConfig;
    g.dbConfig = dbTemp;
  }
}

/*
** Try to read a versioned setting string from .fossil-settings/<name>.
**
** Return the text of the string if it is found.  Return NULL if not
** found.
**
** If the zNonVersionedSetting parameter is not NULL then it holds the
** non-versioned value for this setting.  If both a versioned and a
** non-versioned value exist and are not equal, then a warning message
** might be generated.
*/
char *db_get_versioned(const char *zName, char *zNonVersionedSetting){
  char *zVersionedSetting = 0;
  int noWarn = 0;
  int found = 0;
  struct _cacheEntry {
    struct _cacheEntry *next;
    const char *zName, *zValue;
  } *cacheEntry = 0;
  static struct _cacheEntry *cache = 0;

  if( !g.localOpen && g.zOpenRevision==0 ) return zNonVersionedSetting;
  /* Look up name in cache */
  cacheEntry = cache;
  while( cacheEntry!=0 ){
    if( fossil_strcmp(cacheEntry->zName, zName)==0 ){
      zVersionedSetting = fossil_strdup(cacheEntry->zValue);
      break;
    }
    cacheEntry = cacheEntry->next;
  }
  /* Attempt to read value from file in checkout if there wasn't a cache hit. */
  if( cacheEntry==0 ){
    Blob versionedPathname;
    Blob setting;
    blob_zero(&versionedPathname);
    blob_zero(&setting);
    blob_appendf(&versionedPathname, "%s.fossil-settings/%s",
                 g.zLocalRoot, zName);
    if( !g.localOpen ){
      /* Repository is in the process of being opened, but files have not been
       * written to disk. Load from the database. */
      Blob noWarnFile;
      if( historical_blob(g.zOpenRevision, blob_str(&versionedPathname),
          &setting, 0) ){
        found = 1;
      }
      /* See if there's a no-warn flag */
      blob_append(&versionedPathname, ".no-warn", -1);
      blob_zero(&noWarnFile);
      if( historical_blob(g.zOpenRevision, blob_str(&versionedPathname),
          &noWarnFile, 0) ){
        noWarn = 1;
      }
      blob_reset(&noWarnFile);
    }else if( file_size(blob_str(&versionedPathname))>=0 ){
      /* File exists, and contains the value for this setting. Load from
      ** the file. */
      if( blob_read_from_file(&setting, blob_str(&versionedPathname))>=0 ){
        found = 1;
      }
      /* See if there's a no-warn flag */
      blob_append(&versionedPathname, ".no-warn", -1);
      if( file_size(blob_str(&versionedPathname))>=0 ){
        noWarn = 1;
      }
    }
    blob_reset(&versionedPathname);
    if( found ){
      blob_trim(&setting); /* Avoid non-obvious problems with line endings
                           ** on boolean properties */
      zVersionedSetting = fossil_strdup(blob_str(&setting));
    }
    blob_reset(&setting);
    /* Store result in cache, which can be the value or 0 if not found */
    cacheEntry = (struct _cacheEntry*)fossil_malloc(sizeof(struct _cacheEntry));
    cacheEntry->next = cache;
    cacheEntry->zName = zName;
    cacheEntry->zValue = fossil_strdup(zVersionedSetting);
    cache = cacheEntry;
  }
  /* Display a warning? */
  if( zVersionedSetting!=0 && zNonVersionedSetting!=0
   && zNonVersionedSetting[0]!='\0' && !noWarn
  ){
    /* There's a versioned setting, and a non-versioned setting. Tell
    ** the user about the conflict */
    fossil_warning(
        "setting %s has both versioned and non-versioned values: using "
        "versioned value from file \"%/.fossil-settings/%s\" (to silence "
        "this warning, either create an empty file named "
        "\"%/.fossil-settings/%s.no-warn\" in the check-out root, or delete "
        "the non-versioned setting with \"fossil unset %s\")", zName,
        g.zLocalRoot, zName, g.zLocalRoot, zName, zName
    );
  }
  /* Prefer the versioned setting */
  return ( zVersionedSetting!=0 ) ? zVersionedSetting : zNonVersionedSetting;
}


/*
** Get and set values from the CONFIG, GLOBAL_CONFIG and VVAR table in the
** repository and local databases.
**
** If no such variable exists, return zDefault.  Or, if zName is the name
** of a setting, then the zDefault is ignored and the default value of the
** setting is returned instead.  If zName is a versioned setting, then
** versioned value takes priority.
*/
char *db_get(const char *zName, const char *zDefault){
  char *z = 0;
  const Setting *pSetting = db_find_setting(zName, 0);
  if( g.repositoryOpen ){
    z = db_text(0, "SELECT value FROM config WHERE name=%Q", zName);
  }
  if( z==0 && g.zConfigDbName ){
    db_swap_connections();
    z = db_text(0, "SELECT value FROM global_config WHERE name=%Q", zName);
    db_swap_connections();
  }
  if( pSetting!=0 && pSetting->versionable ){
    /* This is a versionable setting, try and get the info from a
    ** checked out file */
    z = db_get_versioned(zName, z);
  }
  if( z==0 ){
    if( zDefault==0 && pSetting && pSetting->def[0] ){
      z = fossil_strdup(pSetting->def);
    }else{
      z = fossil_strdup(zDefault);
    }
  }
  return z;
}
char *db_get_mtime(const char *zName, const char *zFormat, const char *zDefault){
  char *z = 0;
  if( g.repositoryOpen ){
    z = db_text(0, "SELECT mtime FROM config WHERE name=%Q", zName);
  }
  if( z==0 ){
    z = fossil_strdup(zDefault);
  }else if( zFormat!=0 ){
    z = db_text(0, "SELECT strftime(%Q,%Q,'unixepoch');", zFormat, z);
  }
  return z;
}
void db_set(const char *zName, const char *zValue, int globalFlag){
  db_begin_transaction();
  if( globalFlag ){
    db_swap_connections();
    db_multi_exec("REPLACE INTO global_config(name,value) VALUES(%Q,%Q)",
                   zName, zValue);
    db_swap_connections();
  }else{
    db_multi_exec("REPLACE INTO config(name,value,mtime) VALUES(%Q,%Q,now())",
                   zName, zValue);
  }
  if( globalFlag && g.repositoryOpen ){
    db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
  }
  db_end_transaction(0);
}
void db_unset(const char *zName, int globalFlag){
  db_begin_transaction();
  if( globalFlag ){
    db_swap_connections();
    db_multi_exec("DELETE FROM global_config WHERE name=%Q", zName);
    db_swap_connections();
  }else{
    db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
  }
  if( globalFlag && g.repositoryOpen ){
    db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
  }
  db_end_transaction(0);
}
int db_is_global(const char *zName){
  int rc = 0;
  if( g.zConfigDbName ){
    db_swap_connections();
    rc = db_exists("SELECT 1 FROM global_config WHERE name=%Q", zName);
    db_swap_connections();
  }
  return rc;
}
int db_get_int(const char *zName, int dflt){
  int v = dflt;
  int rc;
  if( g.repositoryOpen ){
    Stmt q;
    db_prepare(&q, "SELECT value FROM config WHERE name=%Q", zName);
    rc = db_step(&q);
    if( rc==SQLITE_ROW ){
      v = db_column_int(&q, 0);
    }
    db_finalize(&q);
  }else{
    rc = SQLITE_DONE;
  }
  if( rc==SQLITE_DONE && g.zConfigDbName ){
    db_swap_connections();
    v = db_int(dflt, "SELECT value FROM global_config WHERE name=%Q", zName);
    db_swap_connections();
  }
  return v;
}
void db_set_int(const char *zName, int value, int globalFlag){
  if( globalFlag ){
    db_swap_connections();
    db_multi_exec("REPLACE INTO global_config(name,value) VALUES(%Q,%d)",
                  zName, value);
    db_swap_connections();
  }else{
    db_multi_exec("REPLACE INTO config(name,value,mtime) VALUES(%Q,%d,now())",
                  zName, value);
  }
  if( globalFlag && g.repositoryOpen ){
    db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
  }
}
int db_get_boolean(const char *zName, int dflt){
  char *zVal = db_get(zName, dflt ? "on" : "off");
  if( is_truth(zVal) ) return 1;
  if( is_false(zVal) ) return 0;
  return dflt;
}
int db_get_versioned_boolean(const char *zName, int dflt){
  char *zVal = db_get_versioned(zName, 0);
  if( zVal==0 ) return dflt;
  if( is_truth(zVal) ) return 1;
  if( is_false(zVal) ) return 0;
  return dflt;
}
char *db_lget(const char *zName, const char *zDefault){
  return db_text(zDefault,
                 "SELECT value FROM vvar WHERE name=%Q", zName);
}
void db_lset(const char *zName, const char *zValue){
  db_multi_exec("REPLACE INTO vvar(name,value) VALUES(%Q,%Q)", zName, zValue);
}
int db_lget_int(const char *zName, int dflt){
  return db_int(dflt, "SELECT value FROM vvar WHERE name=%Q", zName);
}
void db_lset_int(const char *zName, int value){
  db_multi_exec("REPLACE INTO vvar(name,value) VALUES(%Q,%d)", zName, value);
}

#if INTERFACE
/* Manifest generation flags */
#define MFESTFLG_RAW  0x01
#define MFESTFLG_UUID 0x02
#define MFESTFLG_TAGS 0x04
#endif /* INTERFACE */

/*
** Get the manifest setting.  For backwards compatibility first check if the
** value is a boolean.  If it's not a boolean, treat each character as a flag
** to enable a manifest type.  This system puts certain boundary conditions on
** which letters can be used to represent flags (any permutation of flags must
** not be able to fully form one of the boolean values).
*/
int db_get_manifest_setting(void){
  int flg;
  char *zVal = db_get("manifest", 0);
  if( zVal==0 || is_false(zVal) ){
    return 0;
  }else if( is_truth(zVal) ){
    return MFESTFLG_RAW|MFESTFLG_UUID;
  }
  flg = 0;
  while( *zVal ){
    switch( *zVal ){
      case 'r': flg |= MFESTFLG_RAW;  break;
      case 'u': flg |= MFESTFLG_UUID; break;
      case 't': flg |= MFESTFLG_TAGS; break;
    }
    zVal++;
  }
  return flg;
}


/*
** Record the name of a local repository in the global_config() database.
** The repository filename %s is recorded as an entry with a "name" field
** of the following form:
**
**       repo:%s
**
** The value field is set to 1.
**
** If running from a local checkout, also record the root of the checkout
** as follows:
**
**       ckout:%s
**
** Where %s is the checkout root.  The value is the repository file.
*/
void db_record_repository_filename(const char *zName){
  char *zRepoSetting;
  char *zCkoutSetting;
  Blob full;
  if( zName==0 ){
    if( !g.localOpen ) return;
    zName = db_repository_filename();
  }
  file_canonical_name(zName, &full, 0);
  (void)filename_collation();  /* Initialize before connection swap */
  db_swap_connections();
  zRepoSetting = mprintf("repo:%q", blob_str(&full));
  db_multi_exec(
     "DELETE FROM global_config WHERE name %s = %Q;",
     filename_collation(), zRepoSetting
  );
  db_multi_exec(
     "INSERT OR IGNORE INTO global_config(name,value)"
     "VALUES(%Q,1);",
     zRepoSetting
  );
  fossil_free(zRepoSetting);
  if( g.localOpen && g.zLocalRoot && g.zLocalRoot[0] ){
    Blob localRoot;
    file_canonical_name(g.zLocalRoot, &localRoot, 1);
    zCkoutSetting = mprintf("ckout:%q", blob_str(&localRoot));
    db_multi_exec(
       "DELETE FROM global_config WHERE name %s = %Q;",
       filename_collation(), zCkoutSetting
    );
    db_multi_exec(
      "REPLACE INTO global_config(name, value)"
      "VALUES(%Q,%Q);",
      zCkoutSetting, blob_str(&full)
    );
    db_swap_connections();
    db_optional_sql("repository",
        "DELETE FROM config WHERE name %s = %Q;",
        filename_collation(), zCkoutSetting
    );
    db_optional_sql("repository",
        "REPLACE INTO config(name,value,mtime)"
        "VALUES(%Q,1,now());",
        zCkoutSetting
    );
    fossil_free(zCkoutSetting);
    blob_reset(&localRoot);
  }else{
    db_swap_connections();
  }
  blob_reset(&full);
}

/*
** COMMAND: open
**
** Usage: %fossil open FILENAME ?VERSION? ?OPTIONS?
**
** Open a connection to the local repository in FILENAME.  A checkout
** for the repository is created with its root at the working directory.
** If VERSION is specified then that version is checked out.  Otherwise
** the latest version is checked out.  No files other than "manifest"
** and "manifest.uuid" are modified if the --keep option is present.
**
** Options:
**   --empty           Initialize checkout as being empty, but still connected
**                     with the local repository. If you commit this checkout,
**                     it will become a new "initial" commit in the repository.
**   --keep            Only modify the manifest and manifest.uuid files
**   --nested          Allow opening a repository inside an opened checkout
**   --force-missing   Force opening a repository with missing content
**
** See also: close
*/
void cmd_open(void){
  int emptyFlag;
  int keepFlag;
  int forceMissingFlag;
  int allowNested;
  int allowSymlinks;
  static char *azNewArgv[] = { 0, "checkout", "--prompt", 0, 0, 0, 0 };

  url_proxy_options();
  emptyFlag = find_option("empty",0,0)!=0;
  keepFlag = find_option("keep",0,0)!=0;
  forceMissingFlag = find_option("force-missing",0,0)!=0;
  allowNested = find_option("nested",0,0)!=0;

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=3 && g.argc!=4 ){
    usage("REPOSITORY-FILENAME ?VERSION?");
  }
  if( !allowNested && db_open_local(0) ){
    fossil_fatal("already within an open tree rooted at %s", g.zLocalRoot);
  }
  db_open_repository(g.argv[2]);

  /* Figure out which revision to open. */
  if( !emptyFlag ){
    if( g.argc==4 ){
      g.zOpenRevision = g.argv[3];
    }else if( db_exists("SELECT 1 FROM event WHERE type='ci'") ){
      g.zOpenRevision = db_get("main-branch", "trunk");
    }
  }

  if( g.zOpenRevision ){
    /* Since the repository is open and we know the revision now,
    ** refresh the allow-symlinks flag.  Since neither the local
    ** checkout nor the configuration database are open at this
    ** point, this should always return the versioned setting,
    ** if any, or the default value, which is negative one.  The
    ** value negative one, in this context, means that the code
    ** below should fallback to using the setting value from the
    ** repository or global configuration databases only. */
    allowSymlinks = db_get_versioned_boolean("allow-symlinks", -1);
  }else{
    allowSymlinks = -1; /* Use non-versioned settings only. */
  }

#if defined(_WIN32) || defined(__CYGWIN__)
# define LOCALDB_NAME "./_FOSSIL_"
#else
# define LOCALDB_NAME "./.fslckout"
#endif
  db_init_database(LOCALDB_NAME, zLocalSchema,
#ifdef FOSSIL_LOCAL_WAL
                   "COMMIT; PRAGMA journal_mode=WAL; BEGIN;",
#endif
                   (char*)0);
  db_delete_on_failure(LOCALDB_NAME);
  db_open_local(0);
  if( allowSymlinks>=0 ){
    /* Use the value from the versioned setting, which was read
    ** prior to opening the local checkout (i.e. which is most
    ** likely empty and does not actually contain any versioned
    ** setting files yet).  Normally, this value would be given
    ** first priority within db_get_boolean(); however, this is
    ** a special case because we know the on-disk files may not
    ** exist yet. */
    g.allowSymlinks = allowSymlinks;
  }else{
    /* Since the local checkout may not have any files at this
    ** point, this will probably be the setting value from the
    ** repository or global configuration databases. */
    g.allowSymlinks = db_get_boolean("allow-symlinks",
                                     db_allow_symlinks_by_default());
  }
  db_lset("repository", g.argv[2]);
  db_record_repository_filename(g.argv[2]);
  db_lset_int("checkout", 0);
  azNewArgv[0] = g.argv[0];
  g.argv = azNewArgv;
  if( !emptyFlag ){
    g.argc = 3;
    if( g.zOpenRevision ){
      azNewArgv[g.argc-1] = g.zOpenRevision;
    }else{
      azNewArgv[g.argc-1] = "--latest";
    }
    if( keepFlag ){
      azNewArgv[g.argc++] = "--keep";
    }
    if( forceMissingFlag ){
      azNewArgv[g.argc++] = "--force-missing";
    }
    checkout_cmd();
  }
  g.argc = 2;
  info_cmd();
}

/*
** Print the current value of a setting identified by the pSetting
** pointer.
*/
static void print_setting(const Setting *pSetting){
  Stmt q;
  if( g.repositoryOpen ){
    db_prepare(&q,
       "SELECT '(local)', value FROM config WHERE name=%Q"
       " UNION ALL "
       "SELECT '(global)', value FROM global_config WHERE name=%Q",
       pSetting->name, pSetting->name
    );
  }else{
    db_prepare(&q,
      "SELECT '(global)', value FROM global_config WHERE name=%Q",
      pSetting->name
    );
  }
  if( db_step(&q)==SQLITE_ROW ){
    fossil_print("%-20s %-8s %s\n", pSetting->name, db_column_text(&q, 0),
        db_column_text(&q, 1));
  }else{
    fossil_print("%-20s\n", pSetting->name);
  }
  if( pSetting->versionable && g.localOpen ){
    /* Check to see if this is overridden by a versionable settings file */
    Blob versionedPathname;
    blob_zero(&versionedPathname);
    blob_appendf(&versionedPathname, "%s.fossil-settings/%s",
                 g.zLocalRoot, pSetting->name);
    if( file_size(blob_str(&versionedPathname))>=0 ){
      fossil_print("  (overridden by contents of file .fossil-settings/%s)\n",
                   pSetting->name);
    }
  }
  db_finalize(&q);
}

#if INTERFACE
/*
** Define all settings, which can be controlled via the set/unset
** command.
**
** var is the name of the internal configuration name for db_(un)set.
** If var is 0, the settings name is used.
**
** width is the length for the edit field on the behavior page, 0
** is used for on/off checkboxes.
**
** The behaviour page doesn't use a special layout. It lists all
** set-commands and displays the 'set'-help as info.
*/
struct Setting {
  const char *name;     /* Name of the setting */
  const char *var;      /* Internal variable name used by db_set() */
  int width;            /* Width of display.  0 for boolean values. */
  int versionable;      /* Is this setting versionable? */
  int forceTextArea;    /* Force using a text area for display? */
  const char *def;      /* Default value */
};
#endif /* INTERFACE */

/*
** SETTING: access-log      boolean default=off
**
** When the access-log setting is enabled, all login attempts (successful
** and unsuccessful) on the web interface are recorded in the "access" table
** of the repository.
*/
/*
** SETTING: admin-log       boolean default=off
**
** When the admin-log setting is enabled, configuration changes are recorded
** in the "admin_log" table of the repository.
*/
#if defined(_WIN32)
/*
** SETTING: allow-symlinks  boolean default=off versionable
** Allows symbolic links in the repository when enabled.
*/
#endif
#if !defined(_WIN32)
/*
** SETTING: allow-symlinks  boolean default=on versionable
** Allows symbolic links in the repository when enabled.
*/
#endif
/*
** SETTING: auto-captcha    boolean default=on variable=autocaptcha
** If enabled, the /login page provides a button that will automatically
** fill in the captcha password.  This makes things easier for human users,
** at the expense of also making logins easier for malecious robots.
*/
/*
** SETTING: auto-hyperlink  boolean default=on
** Use javascript to enable hyperlinks on web pages
** for all users (regardless of the "h" privilege) if the
** User-Agent string in the HTTP header look like it came
** from real person, not a spider or bot.
*/
/*
** SETTING: auto-shun       boolean default=on
** If enabled, automatically pull the shunning list
** from a server to which the client autosyncs.
*/
/*
** SETTING: autosync        width=16 default=on
** This setting can take either a boolean value or "pullonly"
** If enabled, automatically pull prior to commit
** or update and automatically push after commit or
** tag or branch creation.  If the value is "pullonly"
** then only pull operations occur automatically.
*/
/*
** SETTING: autosync-tries  width=16 default=1
** If autosync is enabled setting this to a value greater
** than zero will cause autosync to try no more than this
** number of attempts if there is a sync failure.
*/
/*
** SETTING: binary-glob     width=40 versionable block-text
** The VALUE of this setting is a comma or newline-separated list of
** GLOB patterns that should be treated as binary files
** for committing and merging purposes.  Example: *.jpg
*/
#if defined(_WIN32)||defined(__CYGWIN__)||defined(__DARWIN__)
/*
** SETTING: case-sensitive  boolean default=off
** If TRUE, the files whose names differ only in case
** are considered distinct.  If FALSE files whose names
** differ only in case are the same file.  Defaults to
** TRUE for unix and FALSE for Cygwin, Mac and Windows.
*/
#endif
#if !(defined(_WIN32)||defined(__CYGWIN__)||defined(__DARWIN__))
/*
** SETTING: case-sensitive  boolean default=on
** If TRUE, the files whose names differ only in case
** are considered distinct.  If FALSE files whose names
** differ only in case are the same file.  Defaults to
** TRUE for unix and FALSE for Cygwin, Mac and Windows.
*/
#endif
/*
** SETTING: clean-glob      width=40 versionable block-text
** The VALUE of this setting is a comma or newline-separated list of GLOB
** patterns specifying files that the "clean" command will
** delete without prompting or allowing undo.
** Example: *.a,*.lib,*.o
*/
/*
** SETTING: clearsign       boolean default=off
** When enabled, fossil will attempt to sign all commits
** with gpg.  When disabled, commits will be unsigned.
*/
/*
** SETTING: crlf-glob       width=40 versionable block-text
** The value is a comma or newline-separated list of GLOB patterns for
** text files in which it is ok to have CR, CR+LF or mixed
** line endings. Set to "*" to disable CR+LF checking.
** The crnl-glob setting is a compatibility alias.
*/
/*
** SETTING: crnl-glob       width=40 versionable block-text
** This is an alias for the crlf-glob setting
*/
/*
** SETTING: default-perms   width=16 default=u
** Permissions given automatically to new users.  For more
** information on permissions see the Users page in Server
** Administration of the HTTP UI.
*/
/*
** SETTING: diff-binary     boolean default=on
** If enabled, permit files that may be binary
** or that match the "binary-glob" setting to be used with
** external diff programs.  If disabled, skip these files.
*/
/*
** SETTING: diff-command    width=40
** The value is an external command to run when performing a diff.
** If undefined, the internal text diff will be used.
*/
/*
** SETTING: dont-push       boolean default=off
** If enabled, prevent this repository from pushing from client to
** server.  This can be used as an extra precaution to prevent
** accidental pushes to a public server from a private clone.
*/
/*
** SETTING: dotfiles        boolean versionable default=off
** If enabled, include --dotfiles option for all compatible commands.
*/
/*
** SETTING: editor          width=32
** The value is an external command that will launch the
** text editor command used for check-in comments.
*/
/*
** SETTING: empty-dirs      width=40 versionable block-text
** The value is a comma or newline-separated list of pathnames. On
** update and checkout commands, if no file or directory
** exists with that name, an empty directory will be
** created.
*/
/*
** SETTING: encoding-glob   width=40 versionable block-text
** The value is a comma or newline-separated list of GLOB
** patterns specifying files that the "commit" command will
** ignore when issuing warnings about text files that may
** use another encoding than ASCII or UTF-8. Set to "*"
** to disable encoding checking.
*/
#if defined(FOSSIL_ENABLE_EXEC_REL_PATHS)
/*
** SETTING: exec-rel-paths   boolean default=on
** When executing certain external commands (e.g. diff and
** gdiff), use relative paths.
*/
#endif
#if !defined(FOSSIL_ENABLE_EXEC_REL_PATHS)
/*
** SETTING: exec-rel-paths   boolean default=off
** When executing certain external commands (e.g. diff and
** gdiff), use relative paths.
*/
#endif
/*
** SETTING: gdiff-command    width=40 default=gdiff
** The value is an external command to run when performing a graphical
** diff. If undefined, text diff will be used.
*/
/*
** SETTING: gmerge-command   width=40
** The value is a graphical merge conflict resolver command operating
** on four files.  Examples:
**
**     kdiff3 "%baseline" "%original" "%merge" -o "%output"
**     xxdiff "%original" "%baseline" "%merge" -M "%output"
**     meld "%baseline" "%original" "%merge" "%output"
*/
/*
** SETTING: hash-digits      width=5 default=10
** The number of hexadecimal digits of the SHA3 hash to display.
*/
/*
** SETTING: http-port        width=16 default=8080
** The default TCP/IP port number to use by the "server"
** and "ui" commands.
*/
/*
** SETTING: https-login      boolean default=off
** If true, then the Fossil web server will redirect unencrypted
** login screeen requests to HTTPS.
*/
/*
** SETTING: ignore-glob      width=40 versionable block-text
** The value is a comma or newline-separated list of GLOB
** patterns specifying files that the "add", "addremove",
** "clean", and "extras" commands will ignore.
**
** Example:  *.log customCode.c notes.txt
*/
/*
** SETTING: keep-glob        width=40 versionable block-text
** The value is a comma or newline-separated list of GLOB
** patterns specifying files that the "clean" command will keep
*/
/*
** SETTING: localauth        boolean default=off
** If enabled, require that HTTP connections from
** 127.0.0.1 be authenticated by password.  If
** false, all HTTP requests from localhost have
** unrestricted access to the repository.
*/
/*
** SETTING: main-branch      width=40 default=trunk
** The value is the primary branch for the project.
*/
/*
** SETTING: manifest         width=5 versionable
** If enabled, automatically create files "manifest" and "manifest.uuid"
** in every checkout.
**
** Optionally use combinations of characters 'r' for "manifest",
** 'u' for "manifest.uuid" and 't' for "manifest.tags".  The SQLite
** and Fossil repositories both require manifests.
*/
/*
** SETTING: max-loadavg      width=25 default=0.0
** Some CPU-intensive web pages (ex: /zip, /tarball, /blame)
** are disallowed if the system load average goes above this
** value.  "0.0" means no limit.  This only works on unix.
** Only local settings of this value make a difference since
** when running as a web-server, Fossil does not open the
** global configuration database.
*/
/*
** SETTING: max-upload       width=25 default=250000
** A limit on the size of uplink HTTP requests.
*/
/*
** SETTING: mtime-changes    boolean default=on
** Use file modification times (mtimes) to detect when
** files have been modified.  If disabled, all managed files
** are hashed to detect changes, which can be slow for large
** projects.
*/
#if FOSSIL_ENABLE_LEGACY_MV_RM
/*
** SETTING: mv-rm-files      boolean default=off
** If enabled, the "mv" and "rename" commands will also move
** the associated files within the checkout -AND- the "rm"
** and "delete" commands will also remove the associated
** files from within the checkout.
*/
#endif
/*
** SETTING: pgp-command      width=40
** Command used to clear-sign manifests at check-in.
** Default value is "gpg --clearsign -o"
*/
/*
** SETTING: proxy            width=32 default=off
** URL of the HTTP proxy.  If undefined or "off" then
** the "http_proxy" environment variable is consulted.
** If the http_proxy environment variable is undefined
** then a direct HTTP connection is used.
*/
/*
** SETTING: relative-paths   boolean default=on
** When showing changes and extras, report paths relative
** to the current working directory.
*/
/*
** SETTING: repo-cksum       boolean default=on
** Compute checksums over all files in each checkout as a double-check
** of correctness.  Disable this on large repositories for a performance
** improvement.
*/
/*
** SETTING: self-register    boolean default=off
** Allow users to register themselves through the HTTP UI.
** This is useful if you want to see other names than
** "Anonymous" in e.g. ticketing system. On the other hand
** users can not be deleted.
*/
/*
** SETTING: ssh-command      width=40
** The command used to talk to a remote machine with  the "ssh://" protocol.
*/
/*
** SETTING: ssl-ca-location  width=40
** The full pathname to a file containing PEM encoded
** CA root certificates, or a directory of certificates
** with filenames formed from the certificate hashes as
** required by OpenSSL.
**
** If set, this will override the OS default list of
** OpenSSL CAs. If unset, the default list will be used.
** Some platforms may add additional certificates.
** Checking your platform behaviour is required if the
** exact contents of the CA root is critical for your
** application.
*/
/*
** SETTING: ssl-identity     width=40
** The full pathname to a file containing a certificate
** and private key in PEM format. Create by concatenating
** the certificate and private key files.
**
** This identity will be presented to SSL servers to
** authenticate this client, in addition to the normal
** password authentication.
*/
#ifdef FOSSIL_ENABLE_TCL
/*
** SETTING: tcl              boolean default=off
** If enabled Tcl integration commands will be added to the TH1
** interpreter, allowing arbitrary Tcl expressions and
** scripts to be evaluated from TH1.  Additionally, the Tcl
** interpreter will be able to evaluate arbitrary TH1
** expressions and scripts.
*/
/*
** SETTING: tcl-setup        width=40 versionable block-text
** This is the setup script to be evaluated after creating
** and initializing the Tcl interpreter.  By default, this
** is empty and no extra setup is performed.
*/
#endif /* FOSSIL_ENABLE_TCL */
#ifdef FOSSIL_ENABLE_TH1_DOCS
/*
** SETTING: th1-docs         boolean default=off
** If enabled, this allows embedded documentation files to contain
** arbitrary TH1 scripts that are evaluated on the server.  If native
** Tcl integration is also enabled, this setting has the
** potential to allow anybody with check-in privileges to
** do almost anything that the associated operating system
** user account could do.  Extreme caution should be used
** when enabling this setting.
*/
#endif
#ifdef FOSSIL_ENABLE_TH1_HOOKS
/*
** SETTING: th1-hooks        boolean default=off
** If enabled, special TH1 commands will be called before and
** after any Fossil command or web page.
*/
#endif
/*
** SETTING: th1-setup        width=40 versionable block-text
** This is the setup script to be evaluated after creating
** and initializing the TH1 interpreter.  By default, this
** is empty and no extra setup is performed.
*/
/*
** SETTING: th1-uri-regexp   width=40 versionable block-text
** Specify which URI's are allowed in HTTP requests from
** TH1 scripts.  If empty, no HTTP requests are allowed
** whatsoever.
*/
/*
** SETTING: uv-sync          boolean default=off
** If true, automatically send unversioned files as part
** of a "fossil clone" or "fossil sync" command.  The
** default is false, in which case the -u option is
** needed to clone or sync unversioned files.
*/
/*
** SETTING: web-browser      width=30
** A shell command used to launch your preferred
** web browser when given a URL as an argument.
** Defaults to "start" on windows, "open" on Mac,
** and "firefox" on Unix.
*/

/*
** Look up a control setting by its name.  Return a pointer to the Setting
** object, or NULL if there is no such setting.
**
** If allowPrefix is true, then the Setting returned is the first one for
** which zName is a prefix of the Setting name.
*/
Setting *db_find_setting(const char *zName, int allowPrefix){
  int lwr, mid, upr, c;
  int n = (int)strlen(zName) + !allowPrefix;
  int nSetting;
  const Setting *aSetting = setting_info(&nSetting);
  lwr = 0;
  upr = nSetting - 1;
  while( upr>=lwr ){
    mid = (upr+lwr)/2;
    c = fossil_strncmp(zName, aSetting[mid].name, n);
    if( c<0 ){
      upr = mid - 1;
    }else if( c>0 ){
      lwr = mid + 1;
    }else{
      if( allowPrefix ){
        while( mid>lwr && fossil_strncmp(zName, aSetting[mid-1].name, n)==0 ){
          mid--;
        }
      }
      return (Setting*)&aSetting[mid];
    }
  }
  return 0;
}

/*
** COMMAND: settings
** COMMAND: unset*
**
** Usage: %fossil settings ?SETTING? ?VALUE? ?OPTIONS?
**    or: %fossil unset SETTING ?OPTIONS?
**
** The "settings" command with no arguments lists all settings and their
** values.  With just a SETTING name it shows the current value of that setting.
** With a VALUE argument it changes the property for the current repository.
**
** Settings marked as versionable are overridden by the contents of the
** file named .fossil-settings/PROPERTY in the check-out root, if that
** file exists.
**
** The "unset" command clears a setting.
**
** Settings can have both a "local" repository-only value and "global" value
** that applies to all repositories.  The local values are stored in the
** "config" table of the repository and the global values are stored in the
** $HOME/.fossil file on unix or in the %LOCALAPPDATA%/_fossil file on Windows.
** If both a local and a global value exists for a setting, the local value
** takes precedence.  This command normally operates on the local settings.
** Use the --global option to change global settings.
**
** Options:
**   --global   set or unset the given property globally instead of
**              setting or unsetting it for the open repository only.
**
**   --exact    only consider exact name matches.
**
** See also: configuration
*/
void setting_cmd(void){
  int i;
  int globalFlag = find_option("global","g",0)!=0;
  int exactFlag = find_option("exact",0,0)!=0;
  int unsetFlag = g.argv[1][0]=='u';
  int nSetting;
  const Setting *aSetting = setting_info(&nSetting);
  find_repository_option();
  verify_all_options();
  db_open_config(1, 0);
  if( !globalFlag ){
    db_find_and_open_repository(OPEN_ANY_SCHEMA | OPEN_OK_NOT_FOUND, 0);
  }
  if( !g.repositoryOpen ){
    globalFlag = 1;
  }
  if( unsetFlag && g.argc!=3 ){
    usage("PROPERTY ?-global?");
  }

  if( g.argc==2 ){
    for(i=0; i<nSetting; i++){
      print_setting(&aSetting[i]);
    }
  }else if( g.argc==3 || g.argc==4 ){
    const char *zName = g.argv[2];
    int n = (int)strlen(zName);
    const Setting *pSetting = db_find_setting(zName, !exactFlag);
    if( pSetting==0 ){
      fossil_fatal("no such setting: %s", zName);
    }
    if( globalFlag && fossil_strcmp(pSetting->name, "manifest")==0 ){
      fossil_fatal("cannot set 'manifest' globally");
    }
    if( unsetFlag || g.argc==4 ){
      int isManifest = fossil_strcmp(pSetting->name, "manifest")==0;
      if( n!=strlen(pSetting[0].name) && pSetting[1].name &&
          fossil_strncmp(pSetting[1].name, zName, n)==0 ){
        Blob x;
        int i;
        blob_init(&x,0,0);
        for(i=0; pSetting[i].name; i++){
          if( fossil_strncmp(pSetting[i].name,zName,n)!=0 ) break;
          blob_appendf(&x, " %s", pSetting[i].name);
        }
        fossil_fatal("ambiguous setting \"%s\" - might be:%s",
                     zName, blob_str(&x));
      }
      if( globalFlag && isManifest ){
        fossil_fatal("cannot set 'manifest' globally");
      }
      if( unsetFlag ){
        db_unset(pSetting->name, globalFlag);
      }else{
        db_set(pSetting->name, g.argv[3], globalFlag);
      }
      if( isManifest && g.localOpen ){
        manifest_to_disk(db_lget_int("checkout", 0));
      }
    }else{
      while( pSetting->name ){
        if( exactFlag ){
          if( fossil_strcmp(pSetting->name,zName)!=0 ) break;
        }else{
          if( fossil_strncmp(pSetting->name,zName,n)!=0 ) break;
        }
        print_setting(pSetting);
        pSetting++;
      }
    }
  }else{
    usage("?PROPERTY? ?VALUE? ?-global?");
  }
}

/*
** The input in a timespan measured in days.  Return a string which
** describes that timespan in units of seconds, minutes, hours, days,
** or years, depending on its duration.
*/
char *db_timespan_name(double rSpan){
  if( rSpan<0 ) rSpan = -rSpan;
  rSpan *= 24.0*3600.0;  /* Convert units to seconds */
  if( rSpan<120.0 ){
    return sqlite3_mprintf("%.1f seconds", rSpan);
  }
  rSpan /= 60.0;         /* Convert units to minutes */
  if( rSpan<90.0 ){
    return sqlite3_mprintf("%.1f minutes", rSpan);
  }
  rSpan /= 60.0;         /* Convert units to hours */
  if( rSpan<=48.0 ){
    return sqlite3_mprintf("%.1f hours", rSpan);
  }
  rSpan /= 24.0;         /* Convert units to days */
  if( rSpan<=365.0 ){
    return sqlite3_mprintf("%.1f days", rSpan);
  }
  rSpan /= 356.24;         /* Convert units to years */
  return sqlite3_mprintf("%.1f years", rSpan);
}

/*
** COMMAND: test-timespan
**
** Usage: %fossil test-timespan TIMESTAMP
**
** Print the approximate span of time from now to TIMESTAMP.
*/
void test_timespan_cmd(void){
  double rDiff;
  if( g.argc!=3 ) usage("TIMESTAMP");
  sqlite3_open(":memory:", &g.db);
  rDiff = db_double(0.0, "SELECT julianday('now') - julianday(%Q)", g.argv[2]);
  fossil_print("Time differences: %s\n", db_timespan_name(rDiff));
  sqlite3_close(g.db);
  g.db = 0;
}

/*
** COMMAND: test-without-rowid
**
** Usage: %fossil test-without-rowid FILENAME...
**
** Change the Fossil repository FILENAME to make use of the WITHOUT ROWID
** optimization.  FILENAME can also be the ~/.fossil file or a local
** .fslckout or _FOSSIL_ file.
**
** The purpose of this command is for testing the WITHOUT ROWID capabilities
** of SQLite.  There is no big advantage to using WITHOUT ROWID in Fossil.
**
** Options:
**    --dryrun | -n         No changes.  Just print what would happen.
*/
void test_without_rowid(void){
  int i, j;
  Stmt q;
  Blob allSql;
  int dryRun = find_option("dry-run", "n", 0)!=0;
  for(i=2; i<g.argc; i++){
    db_open_or_attach(g.argv[i], "main");
    blob_init(&allSql, "BEGIN;\n", -1);
    db_prepare(&q,
      "SELECT name, sql FROM main.sqlite_master "
      " WHERE type='table' AND sql NOT LIKE '%%WITHOUT ROWID%%'"
      "   AND name IN ('global_config','shun','concealed','config',"
                    "  'plink','tagxref','backlink','vcache');"
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zTName = db_column_text(&q, 0);
      const char *zOrigSql = db_column_text(&q, 1);
      Blob newSql;
      blob_init(&newSql, 0, 0);
      for(j=0; zOrigSql[j]; j++){
        if( fossil_strnicmp(zOrigSql+j,"unique",6)==0 ){
          blob_append(&newSql, zOrigSql, j);
          blob_append(&newSql, "PRIMARY KEY", -1);
          zOrigSql += j+6;
          j = -1;
        }
      }
      blob_append(&newSql, zOrigSql, -1);
      blob_append_sql(&allSql,
         "ALTER TABLE \"%w\" RENAME TO \"x_%w\";\n"
         "%s WITHOUT ROWID;\n"
         "INSERT INTO \"%w\" SELECT * FROM \"x_%w\";\n"
         "DROP TABLE \"x_%w\";\n",
         zTName, zTName, blob_sql_text(&newSql), zTName, zTName, zTName
      );
      fossil_print("Converting table %s of %s to WITHOUT ROWID.\n",
                    zTName, g.argv[i]);
      blob_reset(&newSql);
    }
    blob_append_sql(&allSql, "COMMIT;\n");
    db_finalize(&q);
    if( dryRun ){
      fossil_print("SQL that would have been evaluated:\n");
      fossil_print("%.78c\n", '-');
      fossil_print("%s", blob_sql_text(&allSql));
    }else{
      db_multi_exec("%s", blob_sql_text(&allSql));
    }
    blob_reset(&allSql);
    db_close(1);
  }
}

/*
** Make sure the adminlog table exists.  Create it if it does not
*/
void create_admin_log_table(void){
  static int once = 0;
  if( once ) return;
  once = 1;
  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS repository.admin_log(\n"
    " id INTEGER PRIMARY KEY,\n"
    " time INTEGER, -- Seconds since 1970\n"
    " page TEXT,    -- path of page\n"
    " who TEXT,     -- User who made the change\n"
    " what TEXT     -- What changed\n"
    ")"
  );
}

/*
** Write a message into the admin_event table, if admin logging is
** enabled via the admin-log configuration option.
*/
void admin_log(const char *zFormat, ...){
  Blob what = empty_blob;
  va_list ap;
  if( !db_get_boolean("admin-log", 0) ){
      /* Potential leak here (on %z params) but
         the alternative is to let blob_vappendf()
         do it below. */
      return;
  }
  create_admin_log_table();
  va_start(ap,zFormat);
  blob_vappendf( &what, zFormat, ap );
  va_end(ap);
  db_multi_exec("INSERT INTO admin_log(time,page,who,what)"
                " VALUES(now(), %Q, %Q, %B)",
                g.zPath, g.zLogin, &what);
  blob_reset(&what);
}

/*
** COMMAND: test-database-names
**
** Print the names of the various database files:
** (1) The main repository database
** (2) The local checkout database
** (3) The global configuration database
*/
void test_database_name_cmd(void){
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  fossil_print("Repository database: %s\n", g.zRepositoryName);
  fossil_print("Local database:      %s\n", g.zLocalDbName);
  fossil_print("Config database:     %s\n", g.zConfigDbName);
}
