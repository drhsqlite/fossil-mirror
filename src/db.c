/*
** Copyright (c) 2006 D. Richard Hipp
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
#if ! defined(_WIN32)
#  include <pwd.h>
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
  sqlite3_stmt *pStmt;    /* The results of sqlite3_prepare() */
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
  static const char zRebuildMsg[] =
      "If you have recently updated your fossil executable, you might\n"
      "need to run \"fossil all rebuild\" to bring the repository\n"
      "schemas up to date.\n";
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
    cgi_printf("<h1>Database Error</h1>\n"
               "<pre>%h</pre>\n<p>%s</p>\n", z, zRebuildMsg);
    cgi_reply();
  }else{
    fprintf(stderr, "%s: %s\n\n%s", g.argv[0], z, zRebuildMsg);
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
  int nPrepare;             /* Number of calls to sqlite3_prepare() */
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
      while( db.nBeforeCommit ){
        db.nBeforeCommit--;
        sqlite3_exec(g.db, db.azBeforeCommit[db.nBeforeCommit], 0, 0, 0);
        sqlite3_free(db.azBeforeCommit[db.nBeforeCommit]);
      }
      leaf_do_pending_checks();
    }
    for(i=0; db.doRollback==0 && i<db.nCommitHook; i++){
      db.doRollback |= db.aHook[i].xHook();
    }
    while( db.pAllStmt ){
      db_finalize(db.pAllStmt);
    }
    db_multi_exec(db.doRollback ? "ROLLBACK" : "COMMIT");
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

/*
** Prepare a Stmt.  Assume that the Stmt is previously uninitialized.
** If the input string contains multiple SQL statements, only the first
** one is processed.  All statements beyond the first are silently ignored.
*/
int db_vprepare(Stmt *pStmt, int errOk, const char *zFormat, va_list ap){
  int rc;
  char *zSql;
  blob_zero(&pStmt->sql);
  blob_vappendf(&pStmt->sql, zFormat, ap);
  va_end(ap);
  zSql = blob_str(&pStmt->sql);
  db.nPrepare++;
  rc = sqlite3_prepare_v2(g.db, zSql, -1, &pStmt->pStmt, 0);
  if( rc!=0 && !errOk ){
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
  rc = db_vprepare(pStmt, 1, zFormat, ap);
  va_end(ap);
  return rc;
}
int db_static_prepare(Stmt *pStmt, const char *zFormat, ...){
  int rc = SQLITE_OK;
  if( blob_size(&pStmt->sql)==0 ){
    va_list ap;
    va_start(ap, zFormat);
    rc = db_vprepare(pStmt, 0, zFormat, ap);
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
    if( rc!=SQLITE_OK ) break;
    if( pStmt ){
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
char *db_text(char const *zDefault, const char *zSql, ...){
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
    db_err(sqlite3_errmsg(db));
  }
  va_start(ap, zSchema);
  while( (zSql = va_arg(ap, const char*))!=0 ){
    rc = sqlite3_exec(db, zSql, 0, 0, 0);
    if( rc!=SQLITE_OK ){
      db_err(sqlite3_errmsg(db));
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
** Open a database file.  Return a pointer to the new database
** connection.  An error results in process abort.
*/
LOCAL sqlite3 *db_open(const char *zDbName){
  int rc;
  const char *zVfs;
  sqlite3 *db;

#if defined(__CYGWIN__)
  zDbName = fossil_utf8_to_filename(zDbName);
#endif
  if( g.fSqlTrace ) fossil_trace("-- sqlite3_open: [%s]\n", zDbName);
  zVfs = fossil_getenv("FOSSIL_VFS");
  rc = sqlite3_open_v2(
       zDbName, &db,
       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
       zVfs
  );
  if( rc!=SQLITE_OK ){
    db_err("[%s]: %s", zDbName, sqlite3_errmsg(db));
  }
  sqlite3_busy_timeout(db, 5000);
  sqlite3_wal_autocheckpoint(db, 1);  /* Set to checkpoint frequently */
  sqlite3_create_function(db, "now", 0, SQLITE_ANY, 0, db_now_function, 0, 0);
  sqlite3_create_function(db, "checkin_mtime", 2, SQLITE_ANY, 0,
                          db_checkin_mtime_function, 0, 0);
  sqlite3_create_function(db, "user", 0, SQLITE_ANY, 0, db_sql_user, 0, 0);
  sqlite3_create_function(db, "cgi", 1, SQLITE_ANY, 0, db_sql_cgi, 0, 0);
  sqlite3_create_function(db, "cgi", 2, SQLITE_ANY, 0, db_sql_cgi, 0, 0);
  sqlite3_create_function(db, "print", -1, SQLITE_UTF8, 0,db_sql_print,0,0);
  sqlite3_create_function(
    db, "is_selected", 1, SQLITE_UTF8, 0, file_is_selected,0,0
  );
  sqlite3_create_function(
    db, "if_selected", 3, SQLITE_UTF8, 0, file_is_selected,0,0
  );
  if( g.fSqlTrace ) sqlite3_trace(db, db_sql_trace, 0);
  re_add_sql_func(db);
  sqlite3_exec(db, "PRAGMA foreign_keys=OFF;", 0, 0, 0);
  return db;
}


/*
** Detaches the zLabel database.
*/
void db_detach(const char *zLabel){
  db_multi_exec("DETACH DATABASE %s", zLabel);
}

/*
** zDbName is the name of a database file.  Attach zDbName using
** the name zLabel.
*/
void db_attach(const char *zDbName, const char *zLabel){
  db_multi_exec("ATTACH DATABASE %Q AS %s", zDbName, zLabel);
}

/*
** zDbName is the name of a database file.  If no other database
** file is open, then open this one.  If another database file is
** already open, then attach zDbName using the name zLabel.
*/
void db_open_or_attach(
  const char *zDbName,
  const char *zLabel,
  int *pWasAttached
){
  if( !g.db ){
    assert( g.zMainDbType==0 );
    g.db = db_open(zDbName);
    g.zMainDbType = zLabel;
    if( pWasAttached ) *pWasAttached = 0;
  }else{
    assert( g.zMainDbType!=0 );
    db_attach(zDbName, zLabel);
    if( pWasAttached ) *pWasAttached = 1;
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
void db_open_config(int useAttach){
  char *zDbName;
  char *zHome;
  if( g.zConfigDbName ) return;
#if defined(_WIN32) || defined(__CYGWIN__)
  zHome = fossil_getenv("LOCALAPPDATA");
  if( zHome==0 ){
    zHome = fossil_getenv("APPDATA");
    if( zHome==0 ){
      char *zDrive = fossil_getenv("HOMEDRIVE");
      zHome = fossil_getenv("HOMEPATH");
      if( zDrive && zHome ) zHome = mprintf("%s%s", zDrive, zHome);
    }
  }
  if( zHome==0 ){
    fossil_fatal("cannot locate home directory - "
                "please set the LOCALAPPDATA or APPDATA or HOMEPATH "
                "environment variables");
  }
#else
  zHome = fossil_getenv("HOME");
  if( zHome==0 ){
    fossil_fatal("cannot locate home directory - "
                 "please set the HOME environment variable");
  }
#endif
  if( file_isdir(zHome)!=1 ){
    fossil_fatal("invalid home directory: %s", zHome);
  }
#if defined(_WIN32) || defined(__CYGWIN__)
  /* . filenames give some window systems problems and many apps problems */
  zDbName = mprintf("%//_fossil", zHome);
#else
  if( file_access(zHome, W_OK) ){
    fossil_fatal("home directory %s must be writeable", zHome);
  }
  zDbName = mprintf("%s/.fossil", zHome);
#endif
  if( file_size(zDbName)<1024*3 ){
    db_init_database(zDbName, zConfigSchema, (char*)0);
  }
#if defined(_WIN32) || defined(__CYGWIN__)
  if( file_access(zDbName, W_OK) ){
    fossil_fatal("configuration file %s must be writeable", zDbName);
  }
#endif
  if( useAttach ){
    db_open_or_attach(zDbName, "configdb", &g.useAttach);
    g.dbConfig = 0;
    g.zConfigDbType = 0;
  }else{
    g.useAttach = 0;
    g.dbConfig = db_open(zDbName);
    g.zConfigDbType = "configdb";
  }
  g.zConfigDbName = zDbName;
}


/*
** Returns TRUE if zTable exists in the local database but lacks column
** zColumn
*/
static int db_local_table_exists_but_lacks_column(
  const char *zTable,
  const char *zColumn
){
  char *zDef = db_text(0, "SELECT sql FROM %s.sqlite_master"
                   " WHERE name=='%s' /*scan*/",
                   db_name("localdb"), zTable);
  int rc = 0;
  if( zDef ){
    char *zPattern = mprintf("* %s *", zColumn);
    rc = strglob(zPattern, zDef)==0;
    fossil_free(zPattern);
    fossil_free(zDef);
  }
  return rc;
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
  db_open_or_attach(zDbName, "localdb", 0);
  zVFileDef = db_text(0, "SELECT sql FROM %s.sqlite_master"
                         " WHERE name=='vfile'", db_name("localdb"));
  if( zVFileDef==0 ) return 0;

  /* If the "isexe" column is missing from the vfile table, then
  ** add it now.   This code added on 2010-03-06.  After all users have
  ** upgraded, this code can be safely deleted.
  */
  if( !strglob("* isexe *", zVFileDef) ){
    db_multi_exec("ALTER TABLE vfile ADD COLUMN isexe BOOLEAN DEFAULT 0");
  }

  /* If "islink"/"isLink" columns are missing from tables, then
  ** add them now.   This code added on 2011-01-17 and 2011-08-27.
  ** After all users have upgraded, this code can be safely deleted.
  */
  if( !strglob("* islink *", zVFileDef) ){
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
  static const char aDbName[][10] = { "_FOSSIL_", ".fslckout", ".fos" };

  if( g.localOpen) return 1;
  file_getcwd(zPwd, sizeof(zPwd)-20);
  n = strlen(zPwd);
  if( n==1 && zPwd[0]=='/' ) zPwd[0] = '.';
  while( n>0 ){
    for(i=0; i<count(aDbName); i++){
      sqlite3_snprintf(sizeof(zPwd)-n, &zPwd[n], "/%s", aDbName[i]);
      if( isValidLocalDb(zPwd) ){
        /* Found a valid checkout database file */
        zPwd[n] = 0;
        while( n>1 && zPwd[n-1]=='/' ){
          n--;
          zPwd[n] = 0;
        }
        g.zLocalRoot = mprintf("%s/", zPwd);
        g.localOpen = 1;
        db_open_config(0);
        db_open_repository(zDbName);
        return 1;
      }
    }
    n--;
    while( n>0 && zPwd[n]!='/' ){ n--; }
    while( n>0 && zPwd[n-1]=='/' ){ n--; }
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
    if( file_access(zDbName, 0) ){
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
#if defined(__CYGWIN__)
  g.zRepositoryName = fossil_utf8_to_filename(zDbName);
#else
  g.zRepositoryName = mprintf("%s", zDbName);
#endif
  db_open_or_attach(g.zRepositoryName, "repository", 0);
  g.repositoryOpen = 1;
  /* Cache "allow-symlinks" option, because we'll need it on every stat call */
  g.allowSymlinks = db_get_boolean("allow-symlinks", 0);
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
  const char *zRep = find_option("repository", "R", 1);
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
** Return the name of the database "localdb", "configdb", or "repository".
*/
const char *db_name(const char *zDb){
  assert( fossil_strcmp(zDb,"localdb")==0
       || fossil_strcmp(zDb,"configdb")==0
       || fossil_strcmp(zDb,"repository")==0 );
  if( fossil_strcmp(zDb, g.zMainDbType)==0 ) zDb = "main";
  return zDb;
}

/*
** Return TRUE if the schema is out-of-date
*/
int db_schema_is_outofdate(void){
  return db_exists("SELECT 1 FROM config"
                   " WHERE name='aux-schema'"
                   "   AND value<>'%s'", AUX_SCHEMA);
}

/*
** Return true if the database is writeable
*/
int db_is_writeable(const char *zName){
  return g.db!=0 && !sqlite3_db_readonly(g.db, db_name(zName));
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
    fossil_warning("incorrect repository schema version");
    fossil_warning("your repository has schema version \"%s\" "
          "but this binary expects version \"%s\"",
          db_get("aux-schema",0), AUX_SCHEMA);
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
  if( file_access(zRepo, 0) ){
    fossil_fatal("no such file: %s", zRepo);
  }
  if( db_open_local(zRepo)==0 ){
    fossil_fatal("not in a local checkout");
    return;
  }
  db_open_or_attach(zRepo, "test_repo", 0);
  db_lset("repository", blob_str(&repo));
  db_close(1);
}


/*
** Open the local database.  If unable, exit with an error.
*/
void db_must_be_within_tree(void){
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
  if( reportErrors ){
    while( (pStmt = sqlite3_next_stmt(g.db, pStmt))!=0 ){
      fossil_warning("unfinalized SQL statement: [%s]", sqlite3_sql(pStmt));
    }
  }
  g.repositoryOpen = 0;
  g.localOpen = 0;
  g.zConfigDbName = NULL;
  sqlite3_wal_checkpoint(g.db, 0);
  sqlite3_close(g.db);
  g.db = 0;
  g.zMainDbType = 0;
  if( g.dbConfig ){
    sqlite3_close(g.dbConfig);
    g.dbConfig = 0;
    g.zConfigDbType = 0;
  }
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
#if defined(_WIN32)
    zUser = fossil_getenv("USERNAME");
#else
    zUser = fossil_getenv("USER");
    if( zUser==0 ){
      zUser = fossil_getenv("LOGNAME");
    }
#endif
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
       "   VALUES('anonymous',hex(randomblob(8)),'hmncz','Anon');"
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('nobody','','gjor','Nobody');"
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
  const char *zSep = "";

  blob_zero(&x);
  blob_append(&x, "(", 1);
  for(i=0; ctrlSettings[i].name; i++){
    blob_appendf(&x, "%s'%s'", zSep, ctrlSettings[i].name);
    zSep = ",";
  }
  blob_append(&x, ")", 1);
  return blob_str(&x);
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
  const char *zDefaultUser,    /* Default user for the repository */
  int makeServerCodes          /* True to make new server & project codes */
){
  char *zDate;
  Blob hash;
  Blob manifest;

  db_set("content-schema", CONTENT_SCHEMA, 0);
  db_set("aux-schema", AUX_SCHEMA, 0);
  if( makeServerCodes ){
    db_multi_exec(
      "INSERT INTO config(name,value,mtime)"
      " VALUES('server-code', lower(hex(randomblob(20))),now());"
      "INSERT INTO config(name,value,mtime)"
      " VALUES('project-code', lower(hex(randomblob(20))),now());"
    );
  }
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
      "  WHERE (name IN %s OR name IN %s)"
      "    AND name NOT GLOB 'project-*';",
      configure_inop_rhs(CONFIGSET_ALL),
      db_setting_inop_rhs()
    );
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
    blob_appendf(&manifest, "P\n");
    md5sum_init();
    blob_appendf(&manifest, "R %s\n", md5sum_finish(0));
    blob_appendf(&manifest, "T *branch * trunk\n");
    blob_appendf(&manifest, "T *sym-trunk *\n");
    blob_appendf(&manifest, "U %F\n", g.zLogin);
    md5sum_blob(&manifest, &hash);
    blob_appendf(&manifest, "Z %b\n", &hash);
    blob_reset(&hash);
    rid = content_put(&manifest);
    manifest_crosslink(rid, &manifest);
  }
}

/*
** COMMAND: new*
** COMMAND: init
**
** Usage: %fossil new ?OPTIONS? FILENAME
**    Or: %fossil init ?OPTIONS? FILENAME
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
**    --template      FILE      copy settings from repository file
**    --admin-user|-A USERNAME  select given USERNAME as admin user
**    --date-override DATETIME  use DATETIME as time of the initial checkin
**
** See also: clone
*/
void create_repository_cmd(void){
  char *zPassword;
  const char *zTemplate;      /* Repository from which to copy settings */
  const char *zDate;          /* Date of the initial check-in */
  const char *zDefaultUser;   /* Optional name of the default user */

  zTemplate = find_option("template",0,1);
  zDate = find_option("date-override",0,1);
  zDefaultUser = find_option("admin-user","A",1);
  if( zDate==0 ) zDate = "now";
  if( g.argc!=3 ){
    usage("REPOSITORY-NAME");
  }
  db_create_repository(g.argv[2]);
  db_open_repository(g.argv[2]);
  db_open_config(0);
  if( zTemplate ) db_attach(zTemplate, "settingSrc");
  db_begin_transaction();
  db_initial_setup(zTemplate, zDate, zDefaultUser, 1);
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
LOCAL void db_sql_trace(void *notUsed, const char *zSql){
  int n = strlen(zSql);
  fossil_trace("%s%s\n", zSql, (n>0 && zSql[n-1]==';') ? "" : ";");
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
** Convert the input string into an SHA1.  Make a notation in the
** CONCEALED table so that the hash can be undo using the db_reveal()
** function at some later time.
**
** The value returned is stored in static space and will be overwritten
** on subsequent calls.
**
** If zContent is already a well-formed SHA1 hash, then return a copy
** of that hash, not a hash of the hash.
**
** The CONCEALED table is meant to obscure email addresses.  Every valid
** email address will contain a "@" character and "@" is not valid within
** an SHA1 hash so there is no chance that a valid email address will go
** unconcealed.
*/
char *db_conceal(const char *zContent, int n){
  static char zHash[42];
  Blob out;
  if( n==40 && validate16(zContent, n) ){
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
    const char *zTempDbType = g.zMainDbType;
    g.db = g.dbConfig;
    g.zMainDbType = g.zConfigDbType;
    g.dbConfig = dbTemp;
    g.zConfigDbType = zTempDbType;
  }
}

/*
** Logic for reading potentially versioned settings from
** .fossil-settings/<name> , and emits warnings if necessary.
** Returns the non-versioned value without modification if there is no
** versioned value.
*/
char *db_get_do_versionable(const char *zName, char *zNonVersionedSetting){
  char *zVersionedSetting = 0;
  int noWarn = 0;
  struct _cacheEntry {
    struct _cacheEntry *next;
    const char *zName, *zValue;
  } *cacheEntry = 0;
  static struct _cacheEntry *cache = 0;

  if( !g.localOpen) return zNonVersionedSetting;
  /* Look up name in cache */
  cacheEntry = cache;
  while( cacheEntry!=0 ){
    if( fossil_strcmp(cacheEntry->zName, zName)==0 ){
      zVersionedSetting = fossil_strdup(cacheEntry->zValue);
      break;
    }
    cacheEntry = cacheEntry->next;
  }
  /* Attempt to read value from file in checkout if there wasn't a cache hit
  ** and a checkout is open. */
  if( cacheEntry==0 ){
    Blob versionedPathname;
    char *zVersionedPathname;
    blob_zero(&versionedPathname);
    blob_appendf(&versionedPathname, "%s.fossil-settings/%s",
                 g.zLocalRoot, zName);
    zVersionedPathname = blob_str(&versionedPathname);
    if( file_size(zVersionedPathname)>=0 ){
      /* File exists, and contains the value for this setting. Load from
      ** the file. */
      Blob setting;
      blob_zero(&setting);
      if( blob_read_from_file(&setting, zVersionedPathname) >= 0 ){
        blob_trim(&setting); /* Avoid non-obvious problems with line endings
                             ** on boolean properties */
        zVersionedSetting = strdup(blob_str(&setting));
      }
      blob_reset(&setting);
      /* See if there's a no-warn flag */
      blob_append(&versionedPathname, ".no-warn", -1);
      if( file_size(blob_str(&versionedPathname))>=0 ){
        noWarn = 1;
      }
    }
    blob_reset(&versionedPathname);
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
        "versioned value from file .fossil-settings/%s (to silence this "
        "warning, either create an empty file named "
        ".fossil-settings/%s.no-warn or delete the non-versioned setting "
        " with \"fossil unset %s\")", zName, zName, zName, zName
    );
  }
  /* Prefer the versioned setting */
  return ( zVersionedSetting!=0 ) ? zVersionedSetting : zNonVersionedSetting;
}


/*
** Get and set values from the CONFIG, GLOBAL_CONFIG and VVAR table in the
** repository and local databases.
*/
char *db_get(const char *zName, char *zDefault){
  char *z = 0;
  int i;
  const struct stControlSettings *ctrlSetting = 0;
  /* Is this a setting? */
  for(i=0; ctrlSettings[i].name; i++){
    if( strcmp(ctrlSettings[i].name, zName)==0 ){
      ctrlSetting = &(ctrlSettings[i]);
      break;
    }
  }
  if( g.repositoryOpen ){
    z = db_text(0, "SELECT value FROM config WHERE name=%Q", zName);
  }
  if( z==0 && g.zConfigDbName ){
    db_swap_connections();
    z = db_text(0, "SELECT value FROM global_config WHERE name=%Q", zName);
    db_swap_connections();
  }
  if( ctrlSetting!=0 && ctrlSetting->versionable ){
    /* This is a versionable setting, try and get the info from a
    ** checked out file */
    z = db_get_do_versionable(zName, z);
  }
  if( z==0 ){
    z = zDefault;
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
char *db_lget(const char *zName, char *zDefault){
  return db_text((char*)zDefault,
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

/*
** Returns non-0 if the database (which must be open) table identified
** by zTableName has a column named zColName (case-sensitive), else
** returns 0.
*/
int db_table_has_column( char const *zTableName, char const *zColName ){
  Stmt q = empty_Stmt;
  int rc = 0;
  db_prepare( &q, "PRAGMA table_info(%Q)", zTableName );
  while(SQLITE_ROW == db_step(&q)){
    /* Columns: (cid, name, type, notnull, dflt_value, pk) */
    char const * zCol = db_column_text(&q, 1);
    if(0==fossil_strcmp(zColName, zCol)){
      rc = 1;
      break;
    }
  }
  db_finalize(&q);
  return rc;
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
  Blob full;
  if( zName==0 ){
    if( !g.localOpen ) return;
    zName = db_repository_filename();
  }
  file_canonical_name(zName, &full, 0);
  db_swap_connections();
  db_multi_exec(
     "INSERT OR IGNORE INTO global_config(name,value)"
     "VALUES('repo:%q',1)",
     blob_str(&full)
  );
  if( g.localOpen && g.zLocalRoot && g.zLocalRoot[0] ){
    Blob localRoot;
    file_canonical_name(g.zLocalRoot, &localRoot, 1);
    db_multi_exec(
      "REPLACE INTO global_config(name, value)"
      "VALUES('ckout:%q','%q');",
      blob_str(&localRoot), blob_str(&full)
    );
    db_swap_connections();
    db_optional_sql("repository",
        "REPLACE INTO config(name,value,mtime)"
        "VALUES('ckout:%q',1,now())",
        blob_str(&localRoot)
    );
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
**   --keep     Only modify the manifest and manifest.uuid files
**   --nested   Allow opening a repository inside an opened checkout
**
** See also: close
*/
void cmd_open(void){
  int vid;
  int keepFlag;
  int allowNested;
  static char *azNewArgv[] = { 0, "checkout", "--prompt", 0, 0, 0 };

  url_proxy_options();
  keepFlag = find_option("keep",0,0)!=0;
  allowNested = find_option("nested",0,0)!=0;
  if( g.argc!=3 && g.argc!=4 ){
    usage("REPOSITORY-FILENAME ?VERSION?");
  }
  if( !allowNested && db_open_local(0) ){
    fossil_panic("already within an open tree rooted at %s", g.zLocalRoot);
  }
  db_open_repository(g.argv[2]);
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
  db_lset("repository", g.argv[2]);
  db_record_repository_filename(g.argv[2]);
  vid = db_int(0, "SELECT pid FROM plink y"
                  " WHERE NOT EXISTS(SELECT 1 FROM plink x WHERE x.cid=y.pid)");
  if( vid==0 ){
    db_lset_int("checkout", 1);
  }else{
    char **oldArgv = g.argv;
    int oldArgc = g.argc;
    db_lset_int("checkout", vid);
    azNewArgv[0] = g.argv[0];
    g.argv = azNewArgv;
    g.argc = 3;
    if( oldArgc==4 ){
      azNewArgv[g.argc-1] = oldArgv[3];
    }else{
      azNewArgv[g.argc-1] = db_get("main-branch", "trunk");
    }
    if( keepFlag ){
      azNewArgv[g.argc++] = "--keep";
    }
    checkout_cmd();
    g.argc = 2;
    info_cmd();
  }
}

/*
** Print the value of a setting named zName
*/
static void print_setting(
  const struct stControlSettings *ctrlSetting,
  int localOpen
){
  Stmt q;
  if( g.repositoryOpen ){
    db_prepare(&q,
       "SELECT '(local)', value FROM config WHERE name=%Q"
       " UNION ALL "
       "SELECT '(global)', value FROM global_config WHERE name=%Q",
       ctrlSetting->name, ctrlSetting->name
    );
  }else{
    db_prepare(&q,
      "SELECT '(global)', value FROM global_config WHERE name=%Q",
      ctrlSetting->name
    );
  }
  if( db_step(&q)==SQLITE_ROW ){
    fossil_print("%-20s %-8s %s\n", ctrlSetting->name, db_column_text(&q, 0),
        db_column_text(&q, 1));
  }else{
    fossil_print("%-20s\n", ctrlSetting->name);
  }
  if( ctrlSetting->versionable && localOpen ){
    /* Check to see if this is overridden by a versionable settings file */
    Blob versionedPathname;
    blob_zero(&versionedPathname);
    blob_appendf(&versionedPathname, "%s/.fossil-settings/%s",
                 g.zLocalRoot, ctrlSetting->name);
    if( file_size(blob_str(&versionedPathname))>=0 ){
      fossil_print("  (overridden by contents of file .fossil-settings/%s)\n",
                   ctrlSetting->name);
    }
  }
  db_finalize(&q);
}


/*
** define all settings, which can be controlled via the set/unset
** command. var is the name of the internal configuration name for db_(un)set.
** If var is 0, the settings name is used.
** width is the length for the edit field on the behavior page, 0
** is used for on/off checkboxes.
** The behaviour page doesn't use a special layout. It lists all
** set-commands and displays the 'set'-help as info.
*/
#if INTERFACE
struct stControlSettings {
  char const *name;     /* Name of the setting */
  char const *var;      /* Internal variable name used by db_set() */
  int width;            /* Width of display.  0 for boolean values */
  int versionable;      /* Is this setting versionable? */
  char const *def;      /* Default value */
};
#endif /* INTERFACE */
struct stControlSettings const ctrlSettings[] = {
  { "access-log",    0,                0, 0, "off"                 },
  { "allow-symlinks",0,                0, 1, "off"                 },
  { "auto-captcha",  "autocaptcha",    0, 0, "on"                  },
  { "auto-hyperlink",0,                0, 0, "on",                 },
  { "auto-shun",     0,                0, 0, "on"                  },
  { "autosync",      0,                0, 0, "on"                  },
  { "binary-glob",   0,               40, 1, ""                    },
  { "clearsign",     0,                0, 0, "off"                 },
#if defined(_WIN32) || defined(__CYGWIN__) || defined(__DARWIN__) || defined(__APPLE__)
  { "case-sensitive",0,                0, 0, "off"                 },
#else
  { "case-sensitive",0,                0, 0, "on"                  },
#endif
  { "clean-glob",    0,               40, 1, ""                    },
  { "crnl-glob",     0,               40, 1, ""                    },
  { "default-perms", 0,               16, 0, "u"                   },
  { "diff-binary",   0,                0, 0, "on"                  },
  { "diff-command",  0,               40, 0, ""                    },
  { "dont-push",     0,                0, 0, "off"                 },
  { "editor",        0,               32, 0, ""                    },
  { "empty-dirs",    0,               40, 1, ""                    },
  { "encoding-glob",  0,              40, 1, ""                    },
  { "gdiff-command", 0,               40, 0, "gdiff"               },
  { "gmerge-command",0,               40, 0, ""                    },
  { "http-port",     0,               16, 0, "8080"                },
  { "https-login",   0,                0, 0, "off"                 },
  { "ignore-glob",   0,               40, 1, ""                    },
  { "keep-glob",     0,               40, 1, ""                    },
  { "localauth",     0,                0, 0, "off"                 },
  { "main-branch",   0,               40, 0, "trunk"               },
  { "manifest",      0,                0, 1, "off"                 },
  { "max-upload",    0,               25, 0, "250000"              },
  { "mtime-changes", 0,                0, 0, "on"                  },
  { "pgp-command",   0,               40, 0, "gpg --clearsign -o " },
  { "proxy",         0,               32, 0, "off"                 },
  { "relative-paths",0,                0, 0, "on"                  },
  { "repo-cksum",    0,                0, 0, "on"                  },
  { "self-register", 0,                0, 0, "off"                 },
  { "ssh-command",   0,               40, 0, ""                    },
  { "ssl-ca-location",0,              40, 0, ""                    },
  { "ssl-identity",  0,               40, 0, ""                    },
#ifdef FOSSIL_ENABLE_TCL
  { "tcl",           0,                0, 0, "off"                 },
  { "tcl-setup",     0,               40, 0, ""                    },
#endif
  { "th1-setup",     0,               40, 0, ""                    },
  { "web-browser",   0,               32, 0, ""                    },
  { "white-foreground", 0,             0, 0, "off"                 },
  { 0,0,0,0,0 }
};

/*
** COMMAND: settings
** COMMAND: unset*
**
** %fossil settings ?PROPERTY? ?VALUE? ?OPTIONS?
** %fossil unset PROPERTY ?OPTIONS?
**
** The "settings" command with no arguments lists all properties and their
** values.  With just a property name it shows the value of that property.
** With a value argument it changes the property for the current repository.
**
** Settings marked as versionable are overridden by the contents of the
** file named .fossil-settings/PROPERTY in the checked out files, if that
** file exists.
**
** The "unset" command clears a property setting.
**
**
**    access-log       If enabled, record successful and failed login attempts
**                     in the "accesslog" table.  Default: off
**
**    allow-symlinks   If enabled, don't follow symlinks, and instead treat
**     (versionable)   them as symlinks on Unix. Has no effect on Windows
**                     (existing links in repository created on Unix become
**                     plain-text files with link destination path inside).
**                     Default: off
**
**    auto-captcha     If enabled, the Login page provides a button to
**                     fill in the captcha password.  Default: on
**
**    auto-hyperlink   Use javascript to enable hyperlinks on web pages
**                     for all users (regardless of the "h" privilege) if the
**                     User-Agent string in the HTTP header look like it came
**                     from real person, not a spider or bot.  Default: on
**
**    auto-shun        If enabled, automatically pull the shunning list
**                     from a server to which the client autosyncs.
**                     Default: on
**
**    autosync         If enabled, automatically pull prior to commit
**                     or update and automatically push after commit or
**                     tag or branch creation.  If the value is "pullonly"
**                     then only pull operations occur automatically.
**                     Default: on
**
**    binary-glob      The VALUE is a comma or newline-separated list of
**     (versionable)   GLOB patterns that should be treated as binary files
**                     for committing and merging purposes.  Example: *.jpg
**
**    case-sensitive   If TRUE, the files whose names differ only in case
**                     care considered distinct.  If FALSE files whose names
**                     differ only in case are the same file.  Defaults to
**                     TRUE for unix and FALSE for Cygwin, Mac and Windows.
**
**    clean-glob       The VALUE is a comma or newline-separated list of GLOB
**     (versionable)   patterns specifying files that the "clean" command will
**                     delete without prompting even when the -force flag has
**                     not been used.  Example:  *.a *.lib *.o
**
**    clearsign        When enabled, fossil will attempt to sign all commits
**                     with gpg.  When disabled (the default), commits will
**                     be unsigned.  Default: off
**
**    crnl-glob        A comma or newline-separated list of GLOB patterns for
**     (versionable)   text files in which it is ok to have CR, CR+NL or mixed
**                     line endings. Set to "*" to disable CR+NL checking.
**
**    default-perms    Permissions given automatically to new users.  For more
**                     information on permissions see Users page in Server
**                     Administration of the HTTP UI. Default: u.
**
**    diff-binary      If TRUE (the default), permit files that may be binary
**                     or that match the "binary-glob" setting to be used with
**                     external diff programs.  If FALSE, skip these files.
**
**    diff-command     External command to run when performing a diff.
**                     If undefined, the internal text diff will be used.
**
**    dont-push        Prevent this repository from pushing from client to
**                     server.  Useful when setting up a private branch.
**
**    editor           Text editor command used for check-in comments.
**
**    empty-dirs       A comma or newline-separated list of pathnames. On
**     (versionable)   update and checkout commands, if no file or directory
**                     exists with that name, an empty directory will be
**                     created.
**
**    encoding-glob    The VALUE is a comma or newline-separated list of GLOB
**     (versionable)   patterns specifying files that the "commit" command will
**                     ignore when issuing warnings about text files that may
**                     use another encoding than ASCII or UTF-8. Set to "*"
**                     to disable encoding checking.
**
**    gdiff-command    External command to run when performing a graphical
**                     diff. If undefined, text diff will be used.
**
**    gmerge-command   A graphical merge conflict resolver command operating
**                     on four files.
**                     Ex: kdiff3 "%baseline" "%original" "%merge" -o "%output"
**                     Ex: xxdiff "%original" "%baseline" "%merge" -M "%output"
**                     Ex: meld "%baseline" "%original" "%merge" "%output"
**
**    http-port        The TCP/IP port number to use by the "server"
**                     and "ui" commands.  Default: 8080
**
**    https-login      Send login credentials using HTTPS instead of HTTP
**                     even if the login page request came via HTTP.
**
**    ignore-glob      The VALUE is a comma or newline-separated list of GLOB
**     (versionable)   patterns specifying files that the "add", "addremove",
**                     "clean", and "extra" commands will ignore.
**                     Example:  *.log customCode.c notes.txt
**
**    keep-glob        The VALUE is a comma or newline-separated list of GLOB
**     (versionable)   patterns specifying files that the "clean" command will
**                     keep.
**
**    localauth        If enabled, require that HTTP connections from
**                     127.0.0.1 be authenticated by password.  If
**                     false, all HTTP requests from localhost have
**                     unrestricted access to the repository.
**
**    main-branch      The primary branch for the project.  Default: trunk
**
**    manifest         If enabled, automatically create files "manifest" and
**     (versionable)   "manifest.uuid" in every checkout.  The SQLite and
**                     Fossil repositories both require this.  Default: off.
**
**    max-upload       A limit on the size of uplink HTTP requests.  The
**                     default is 250000 bytes.
**
**    mtime-changes    Use file modification times (mtimes) to detect when
**                     files have been modified.  (Default "on".)
**
**    pgp-command      Command used to clear-sign manifests at check-in.
**                     The default is "gpg --clearsign -o ".
**
**    proxy            URL of the HTTP proxy.  If undefined or "off" then
**                     the "http_proxy" environment variable is consulted.
**                     If the http_proxy environment variable is undefined
**                     then a direct HTTP connection is used.
**
**    relative-paths   When showing changes and extras, report paths relative
**                     to the current working directory.  Default: "on"
**
**    repo-cksum       Compute checksums over all files in each checkout
**                     as a double-check of correctness.  Defaults to "on".
**                     Disable on large repositories for a performance
**                     improvement.
**
**    self-register    Allow users to register themselves through the HTTP UI.
**                     This is useful if you want to see other names than
**                     "Anonymous" in e.g. ticketing system. On the other hand
**                     users can not be deleted. Default: off.
**
**    ssh-command      Command used to talk to a remote machine with
**                     the "ssh://" protocol.
**
**    ssl-ca-location  The full pathname to a file containing PEM encoded
**                     CA root certificates, or a directory of certificates
**                     with filenames formed from the certificate hashes as
**                     required by OpenSSL.
**                     If set, this will override the OS default list of
**                     OpenSSL CAs. If unset, the default list will be used.
**                     Some platforms may add additional certificates.
**                     Check your platform behaviour is as required if the
**                     exact contents of the CA root is critical for your
**                     application.
**
**    ssl-identity     The full pathname to a file containing a certificate
**                     and private key in PEM format. Create by concatenating
**                     the certificate and private key files.
**                     This identity will be presented to SSL servers to
**                     authenticate this client, in addition to the normal
**                     password authentication.
**
**    tcl              If enabled (and Fossil was compiled with Tcl support),
**                     Tcl integration commands will be added to the TH1
**                     interpreter, allowing arbitrary Tcl expressions and
**                     scripts to be evaluated from TH1.  Additionally, the Tcl
**                     interpreter will be able to evaluate arbitrary TH1
**                     expressions and scripts. Default: off.
**
**    tcl-setup        This is the setup script to be evaluated after creating
**                     and initializing the Tcl interpreter.  By default, this
**                     is empty and no extra setup is performed.
**
**    th1-setup        This is the setup script to be evaluated after creating
**                     and initializing the TH1 interpreter.  By default, this
**                     is empty and no extra setup is performed.
**
**    web-browser      A shell command used to launch your preferred
**                     web browser when given a URL as an argument.
**                     Defaults to "start" on windows, "open" on Mac,
**                     and "firefox" on Unix.
**
** Options:
**   --global   set or unset the given property globally instead of
**              setting or unsetting it for the open repository only.
**
** See also: configuration
*/
void setting_cmd(void){
  int i;
  int globalFlag = find_option("global","g",0)!=0;
  int unsetFlag = g.argv[1][0]=='u';
  db_open_config(1);
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
    int openLocal = db_open_local(0);
    for(i=0; ctrlSettings[i].name; i++){
      print_setting(&ctrlSettings[i], openLocal);
    }
  }else if( g.argc==3 || g.argc==4 ){
    const char *zName = g.argv[2];
    int isManifest;
    int n = strlen(zName);
    for(i=0; ctrlSettings[i].name; i++){
      if( strncmp(ctrlSettings[i].name, zName, n)==0 ) break;
    }
    if( !ctrlSettings[i].name ){
      fossil_fatal("no such setting: %s", zName);
    }
    isManifest = fossil_strcmp(ctrlSettings[i].name, "manifest")==0;
    if( isManifest && globalFlag ){
      fossil_fatal("cannot set 'manifest' globally");
    }
    if( unsetFlag ){
      db_unset(ctrlSettings[i].name, globalFlag);
    }else if( g.argc==4 ){
      db_set(ctrlSettings[i].name, g.argv[3], globalFlag);
    }else{
      isManifest = 0;
      print_setting(&ctrlSettings[i], db_open_local(0));
    }
    if( isManifest && g.localOpen ){
      manifest_to_disk(db_lget_int("checkout", 0));
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
** %fossil test-timespan TIMESTAMP
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
