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
**    (3)  A local checkout database named "_FOSSIL_" or ".fos"
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
#endif /* INTERFACE */

/*
** Call this routine when a database error occurs.
*/
static void db_err(const char *zFormat, ...){
  va_list ap;
  char *z;
  static const char zRebuildMsg[] = 
      "If you have recently updated your fossil executable, you might\n"
      "need to run \"fossil all rebuild\" to bring the repository\n"
      "schemas up to date.\n";
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.xferPanic ){
    cgi_reset_content();
    @ error Database\serror:\s%F(z)
    cgi_reply();
  }
  if( g.cgiOutput ){
    g.cgiOutput = 0;
    cgi_printf("<h1>Database Error</h1>\n"
               "<pre>%h</pre><p>%s</p>", z, zRebuildMsg);
    cgi_reply();
  }else{
    fprintf(stderr, "%s: %s\n\n%s", fossil_nameofexe(), z, zRebuildMsg);
  }
  db_force_rollback();
  fossil_exit(1);
}

static int nBegin = 0;      /* Nesting depth of BEGIN */
static int isNewRepo = 0;   /* True if the repository is newly created */
static int doRollback = 0;  /* True to force a rollback */
static int nCommitHook = 0; /* Number of commit hooks */
static struct sCommitHook {
  int (*xHook)(void);  /* Functions to call at db_end_transaction() */
  int sequence;        /* Call functions in sequence order */
} aHook[5];
static Stmt *pAllStmt = 0;  /* List of all unfinalized statements */

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
  if( nBegin ){
    fossil_panic("illegal commit attempt");
    return 1;
  }
  return 0;
}

/*
** Begin and end a nested transaction
*/
void db_begin_transaction(void){
  if( nBegin==0 ){
    db_multi_exec("BEGIN");
    sqlite3_commit_hook(g.db, db_verify_at_commit, 0);
  }
  nBegin++;
}
void db_end_transaction(int rollbackFlag){
  if( g.db==0 ) return;
  if( nBegin<=0 ) return;
  if( rollbackFlag ) doRollback = 1;
  nBegin--;
  if( nBegin==0 ){
    int i;
    for(i=0; doRollback==0 && i<nCommitHook; i++){
      doRollback |= aHook[i].xHook();
    }
    db_multi_exec(doRollback ? "ROLLBACK" : "COMMIT");
    doRollback = 0;
  }
}
void db_force_rollback(void){
  static int busy = 0;
  if( busy || g.db==0 ) return;
  busy = 1;
  undo_rollback();
  if( nBegin ){
    sqlite3_exec(g.db, "ROLLBACK", 0, 0, 0);
    nBegin = 0;
    if( isNewRepo ){
      db_close();
      unlink(g.zRepositoryName);
    }
  }
  busy = 0;
}

/*
** Install a commit hook.  Hooks are installed in sequence order.
** It is an error to install the same commit hook more than once.
**
** Each commit hook is called (in order of accending sequence) at
** each commit operation.  If any commit hook returns non-zero,
** the subsequence commit hooks are omitted and the transaction
** rolls back rather than commit.  It is the responsibility of the
** hooks themselves to issue any error messages.
*/
void db_commit_hook(int (*x)(void), int sequence){
  int i;
  assert( nCommitHook < sizeof(aHook)/sizeof(aHook[1]) );
  for(i=0; i<nCommitHook; i++){
    assert( x!=aHook[i].xHook );
    if( aHook[i].sequence>sequence ){
      int s = sequence;
      int (*xS)(void) = x;
      sequence = aHook[i].sequence;
      x = aHook[i].xHook;
      aHook[i].sequence = s;
      aHook[i].xHook = xS;
    }
  }
  aHook[nCommitHook].sequence = sequence;
  aHook[nCommitHook].xHook = x;
  nCommitHook++;
}

/*
** Prepare a Stmt.  Assume that the Stmt is previously uninitialized.
** If the input string contains multiple SQL statements, only the first
** one is processed.  All statements beyond the first are silently ignored.
*/
int db_vprepare(Stmt *pStmt, const char *zFormat, va_list ap){
  char *zSql;
  blob_zero(&pStmt->sql);
  blob_vappendf(&pStmt->sql, zFormat, ap);
  va_end(ap);
  zSql = blob_str(&pStmt->sql);
  if( sqlite3_prepare_v2(g.db, zSql, -1, &pStmt->pStmt, 0)!=0 ){
    db_err("%s\n%s", sqlite3_errmsg(g.db), zSql);
  }
  pStmt->pNext = pStmt->pPrev = 0;
  pStmt->nStep = 0;
  return 0;
}
int db_prepare(Stmt *pStmt, const char *zFormat, ...){
  int rc;
  va_list ap;
  va_start(ap, zFormat);
  rc = db_vprepare(pStmt, zFormat, ap);
  va_end(ap);
  return rc;
}
int db_static_prepare(Stmt *pStmt, const char *zFormat, ...){
  int rc = SQLITE_OK;
  if( blob_size(&pStmt->sql)==0 ){
    va_list ap;
    va_start(ap, zFormat);
    rc = db_vprepare(pStmt, zFormat, ap);
    pStmt->pNext = pAllStmt;
    pStmt->pPrev = 0;
    if( pAllStmt ) pAllStmt->pPrev = pStmt;
    pAllStmt = pStmt;
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
** to the SQL variable.  Constrast this to bind_blob() which treats
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
  }else if( pAllStmt==pStmt ){
    pAllStmt = pStmt->pNext;
  }
  pStmt->pNext = 0;
  pStmt->pPrev = 0;
  return rc;
}

/*
** Return the rowid of the most recent insert
*/
i64 db_last_insert_rowid(void){
  return sqlite3_last_insert_rowid(g.db);
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
** Initialize a blob to an ephermeral copy of the content of a
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
  int rc;
  va_list ap;
  char *zErr = 0;
  blob_init(&sql, 0, 0);
  va_start(ap, zSql);
  blob_vappendf(&sql, zSql, ap);
  va_end(ap);
  rc = sqlite3_exec(g.db, blob_buffer(&sql), 0, 0, &zErr);
  if( rc!=SQLITE_OK ){
    db_err("%s\n%s", zErr, blob_buffer(&sql));
  }
  blob_reset(&sql);
  return rc;
}

/*
** Execute a query and return a single integer value.
*/
i64 db_int64(i64 iDflt, const char *zSql, ...){
  va_list ap;
  Stmt s;
  i64 rc;
  va_start(ap, zSql);
  db_vprepare(&s, zSql, ap);
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
  db_vprepare(&s, zSql, ap);
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
  db_vprepare(&s, zSql, ap);
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
  db_vprepare(&s, zSql, ap);
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
  db_vprepare(&s, zSql, ap);
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
char *db_text(char *zDefault, const char *zSql, ...){
  va_list ap;
  Stmt s;
  char *z;
  va_start(ap, zSql);
  db_vprepare(&s, zSql, ap);
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

#if defined(_WIN32)
extern char *sqlite3_win32_mbcs_to_utf8(const char*);
#endif

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

#if defined(_WIN32)
  zFileName = sqlite3_win32_mbcs_to_utf8(zFileName);
#endif
  rc = sqlite3_open(zFileName, &db);
  if( rc!=SQLITE_OK ){
    db_err(sqlite3_errmsg(db));
  }
  sqlite3_busy_timeout(db, 5000);
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
** Open a database file.  Return a pointer to the new database
** connection.  An error results in process abort.
*/
static sqlite3 *openDatabase(const char *zDbName){
  int rc;
  const char *zVfs;
  sqlite3 *db;

  zVfs = getenv("FOSSIL_VFS");
#if defined(_WIN32)
  zDbName = sqlite3_win32_mbcs_to_utf8(zDbName);
#endif
  rc = sqlite3_open_v2(
       zDbName, &db,
       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
       zVfs
  );
  if( rc!=SQLITE_OK ){
    db_err(sqlite3_errmsg(db));
  }
  sqlite3_busy_timeout(db, 5000); 
  sqlite3_wal_autocheckpoint(db, 1);  /* Set to checkpoint frequently */
  return db;
}


/*
** zDbName is the name of a database file.  If no other database
** file is open, then open this one.  If another database file is
** already open, then attach zDbName using the name zLabel.
*/
void db_open_or_attach(const char *zDbName, const char *zLabel){
  if( !g.db ){
    g.db = openDatabase(zDbName);
    g.zRepoDb = "main";
    db_connection_init();
  }else{
#if defined(_WIN32)
    zDbName = sqlite3_win32_mbcs_to_utf8(zDbName);
#endif
    db_multi_exec("ATTACH DATABASE %Q AS %s", zDbName, zLabel);
    g.zRepoDb = mprintf("%s", zLabel);
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
  const char *zHome;
  if( g.configOpen ) return;
#if defined(_WIN32)
  zHome = getenv("LOCALAPPDATA");
  if( zHome==0 ){
    zHome = getenv("APPDATA");
    if( zHome==0 ){
      zHome = getenv("HOMEPATH");
    }
  }
  if( zHome==0 ){
    fossil_fatal("cannot locate home directory - "
                "please set the HOMEPATH environment variable");
  }
#else
  zHome = getenv("HOME");
  if( zHome==0 ){
    fossil_fatal("cannot locate home directory - "
                 "please set the HOME environment variable");
  }
#endif
  if( file_isdir(zHome)!=1 ){
    fossil_fatal("invalid home directory: %s", zHome);
  }
#ifndef _WIN32
  if( access(zHome, W_OK) ){
    fossil_fatal("home directory %s must be writeable", zHome);
  }
#endif
  g.zHome = mprintf("%/", zHome);
#if defined(_WIN32)
  /* . filenames give some window systems problems and many apps problems */
  zDbName = mprintf("%//_fossil", zHome);
#else
  zDbName = mprintf("%s/.fossil", zHome);
#endif
  if( file_size(zDbName)<1024*3 ){
    db_init_database(zDbName, zConfigSchema, (char*)0);
  }
  g.useAttach = useAttach;
  if( useAttach ){
    db_open_or_attach(zDbName, "configdb");
    g.dbConfig = 0;
  }else{
    g.dbConfig = openDatabase(zDbName);
  }
  g.configOpen = 1;
  free(zDbName);
}

/*
** If zDbName is a valid local database file, open it and return
** true.  If it is not a valid local database file, return 0.
*/
static int isValidLocalDb(const char *zDbName){
  i64 lsize;
  int rc;
  sqlite3_stmt *pStmt;

  if( access(zDbName, F_OK) ) return 0;
  lsize = file_size(zDbName);
  if( lsize%1024!=0 || lsize<4096 ) return 0;
  db_open_or_attach(zDbName, "localdb");
  g.localOpen = 1;
  db_open_config(0);
  db_open_repository(0);

  /* If the "isexe" column is missing from the vfile table, then
  ** add it now.   This code added on 2010-03-06.  After all users have
  ** upgraded, this code can be safely deleted. 
  */
  rc = sqlite3_prepare(g.db, "SELECT isexe FROM vfile", -1, &pStmt, 0);
  sqlite3_finalize(pStmt);
  if( rc==SQLITE_ERROR ){
    sqlite3_exec(g.db, "ALTER TABLE vfile ADD COLUMN isexe BOOLEAN", 0, 0, 0);
  }

#if 0
  /* If the "mtime" column is missing from the vfile table, then
  ** add it now.   This code added on 2008-12-06.  After all users have
  ** upgraded, this code can be safely deleted. 
  */
  rc = sqlite3_prepare(g.db, "SELECT mtime FROM vfile", -1, &pStmt, 0);
  sqlite3_finalize(pStmt);
  if( rc==SQLITE_ERROR ){
    sqlite3_exec(g.db, "ALTER TABLE vfile ADD COLUMN mtime INTEGER", 0, 0, 0);
  }
#endif

#if 0
  /* If the "origname" column is missing from the vfile table, then
  ** add it now.   This code added on 2008-11-09.  After all users have
  ** upgraded, this code can be safely deleted. 
  */
  rc = sqlite3_prepare(g.db, "SELECT origname FROM vfile", -1, &pStmt, 0);
  sqlite3_finalize(pStmt);
  if( rc==SQLITE_ERROR ){
    sqlite3_exec(g.db, "ALTER TABLE vfile ADD COLUMN origname TEXT", 0, 0, 0);
  }
#endif

  return 1;
}

/*
** Locate the root directory of the local repository tree.  The root
** directory is found by searching for a file named "_FOSSIL_" or ".fos"
** that contains a valid repository database.
**
** If no valid _FOSSIL_ or .fos file is found, we move up one level and 
** try again. Once the file is found, the g.zLocalRoot variable is set
** to the root of the repository tree and this routine returns 1.  If
** no database is found, then this routine return 0.
**
** This routine always opens the user database regardless of whether or
** not the repository database is found.  If the _FOSSIL_ or .fos file
** is found, it is attached to the open database connection too.
*/
int db_open_local(void){
  int i, n;
  char zPwd[2000];
  char *zPwdConv;
  static const char *aDbName[] = { "/_FOSSIL_", "/.fos" };
  
  if( g.localOpen) return 1;
  if( getcwd(zPwd, sizeof(zPwd)-20)==0 ){
    db_err("pwd too big: max %d", sizeof(zPwd)-20);
  }
  n = strlen(zPwd);
  zPwdConv = mprintf("%/", zPwd);
  strncpy(zPwd, zPwdConv, 2000-20);
  free(zPwdConv);
  while( n>0 ){
    if( access(zPwd, W_OK) ) break;
    for(i=0; i<sizeof(aDbName)/sizeof(aDbName[0]); i++){
      strcpy(&zPwd[n], aDbName[i]);
      if( isValidLocalDb(zPwd) ){
        /* Found a valid checkout database file */
        zPwd[n] = 0;
        while( n>1 && zPwd[n-1]=='/' ){
          n--;
          zPwd[n] = 0;
        }
        g.zLocalRoot = mprintf("%s/", zPwd);
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
** Open the repository database given by zDbName.  If zDbName==NULL then
** get the name from the already open local database.
*/
void db_open_repository(const char *zDbName){
  if( g.repositoryOpen ) return;
  if( zDbName==0 ){
    if( g.localOpen ){
      zDbName = db_lget("repository", 0);
    }
    if( zDbName==0 ){
      db_err("unable to find the name of a repository database");
    }
  }
  if( access(zDbName, R_OK) || file_size(zDbName)<1024 ){
    if( access(zDbName, 0) ){
      fossil_panic("repository does not exist or"
                   " is in an unreadable directory: %s", zDbName);
    }else if( access(zDbName, R_OK) ){
      fossil_panic("read permission denied for repository %s", zDbName);
    }else{
      fossil_panic("not a valid repository: %s", zDbName);
    }
  }
  db_open_or_attach(zDbName, "repository");
  g.repositoryOpen = 1;
  g.zRepositoryName = mprintf("%s", zDbName);
}

/*
** Try to find the repository and open it.  Use the -R or --repository
** option to locate the repository.  If no such option is available, then
** use the repository of the open checkout if there is one.
**
** Error out if the repository cannot be opened.
*/
void db_find_and_open_repository(int errIfNotFound){
  const char *zRep = find_option("repository", "R", 1);
  if( zRep==0 ){
    if( db_open_local()==0 ){
      goto rep_not_found;
    }
    zRep = db_lget("repository", 0);
    if( zRep==0 ){
      goto rep_not_found;
    }
  }
  db_open_repository(zRep);
  if( g.repositoryOpen ){
    return;
  }
rep_not_found:
  if( errIfNotFound ){
    fossil_fatal("use --repository or -R to specify the repository database");
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
  if( db_open_local()==0 ){
    fossil_fatal("not in a local checkout");
    return;
  }
  file_canonical_name(g.argv[2], &repo);
  zRepo = blob_str(&repo);
  if( access(zRepo, 0) ){
    fossil_fatal("no such file: %s", zRepo);
  }
  db_open_or_attach(zRepo, "test_repo");
  db_lset("repository", blob_str(&repo));
  db_close();
}


/*
** Open the local database.  If unable, exit with an error.
*/
void db_must_be_within_tree(void){
  if( db_open_local()==0 ){
    fossil_fatal("not within an open checkout");
  }
  db_open_repository(0);
}

/*
** Close the database connection.
*/
void db_close(void){
  sqlite3_stmt *pStmt;
  if( g.db==0 ) return;
  while( pAllStmt ){
    db_finalize(pAllStmt);
  }
  db_end_transaction(1);
  pStmt = 0;
  while( (pStmt = sqlite3_next_stmt(g.db, pStmt))!=0 ){
    fossil_warning("unfinalized SQL statement: [%s]", sqlite3_sql(pStmt));
  }
  g.repositoryOpen = 0;
  g.localOpen = 0;
  g.configOpen = 0;
  sqlite3_wal_checkpoint(g.db, 0);
  sqlite3_close(g.db);
  g.db = 0;
  if( g.dbConfig ){
    sqlite3_close(g.dbConfig);
    g.dbConfig = 0;
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
     zRepositorySchema2,
     (char*)0
  );
  isNewRepo = 1;
}

/*
** Create the default user accounts in the USER table.
*/
void db_create_default_users(int setupUserOnly, const char *zDefaultUser){
  const char *zUser;
  zUser = db_get("default-user", 0);
  if( zUser==0 ){
    zUser = zDefaultUser;
  }
  if( zUser==0 ){
#if defined(_WIN32)
    zUser = getenv("USERNAME");
#else
    zUser = getenv("USER");
#endif
  }
  if( zUser==0 ){
    zUser = "root";
  }
  db_multi_exec(
     "INSERT INTO user(login, pw, cap, info)"
     "VALUES(%Q,lower(hex(randomblob(3))),'s','')", zUser
  );
  if( !setupUserOnly ){
    db_multi_exec(
       "INSERT INTO user(login,pw,cap,info)"
       "   VALUES('anonymous',hex(randomblob(8)),'ghmncz','Anon');"
       "INSERT INTO user(login,pw,cap,info)"
       "   VALUES('nobody','','jor','Nobody');"
       "INSERT INTO user(login,pw,cap,info)"
       "   VALUES('developer','','dei','Dev');"
       "INSERT INTO user(login,pw,cap,info)"
       "   VALUES('reader','','kptw','Reader');"
    );
  }
}

/*
** Fill an empty repository database with the basic information for a
** repository. This function is shared between 'create_repository_cmd'
** ('new') and 'reconstruct_cmd' ('reconstruct'), both of which create
** new repositories.
**
** The zInitialDate parameter determines the date of the initial check-in
** that is automatically created.  If zInitialDate is 0 then no initial
** check-in is created. The makeServerCodes flag determines whether or
** not server and project codes are invented for this repository.
*/
void db_initial_setup(
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
      "INSERT INTO config(name,value)"
      " VALUES('server-code', lower(hex(randomblob(20))));"
      "INSERT INTO config(name,value)"
      " VALUES('project-code', lower(hex(randomblob(20))));"
    );
  }
  if( !db_is_global("autosync") ) db_set_int("autosync", 1, 0);
  if( !db_is_global("localauth") ) db_set_int("localauth", 0, 0);
  db_create_default_users(0, zDefaultUser);
  user_select();

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
    rid = content_put(&manifest, 0, 0);
    manifest_crosslink(rid, &manifest);
  }
}

/*
** COMMAND: new
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
** Options:
**
**    --admin-user|-A USERNAME
**    --date-override DATETIME
**
*/
void create_repository_cmd(void){
  char *zPassword;
  const char *zDate;          /* Date of the initial check-in */
  const char *zDefaultUser;   /* Optional name of the default user */

  zDate = find_option("date-override",0,1);
  zDefaultUser = find_option("admin-user","A",1);
  if( zDate==0 ) zDate = "now";
  if( g.argc!=3 ){
    usage("REPOSITORY-NAME");
  }
  db_create_repository(g.argv[2]);
  db_open_repository(g.argv[2]);
  db_open_config(0);
  db_begin_transaction();
  db_initial_setup(zDate, zDefaultUser, 1);
  db_end_transaction(0);
  printf("project-id: %s\n", db_get("project-code", 0));
  printf("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  printf("admin-user: %s (initial password is \"%s\")\n", g.zLogin, zPassword);
}

/*
** SQL functions for debugging.
**
** The print() function writes its arguments on stdout, but only
** if the -sqlprint command-line option is turned on.
*/
static void db_sql_print(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int i;
  if( g.fSqlPrint ){
    for(i=0; i<argc; i++){
      char c = i==argc-1 ? '\n' : ' ';
      printf("%s%c", sqlite3_value_text(argv[i]), c);
    }
  }
}
static void db_sql_trace(void *notUsed, const char *zSql){
  int n = strlen(zSql);
  fprintf(stderr, "%s%s\n", zSql, (n>0 && zSql[n-1]==';') ? "" : ";");
}

/*
** Implement the user() SQL function.  user() takes no arguments and
** returns the user ID of the current user.
*/
static void db_sql_user(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if( g.zLogin!=0 ){
    sqlite3_result_text(context, g.zLogin, -1, SQLITE_STATIC);
  }
}

/*
** Implement the cgi() SQL function.  cgi() takes a an argument which is
** a name of CGI query parameter. The value of that parameter is returned, 
** if available. optional second argument will be returned if the first
** doesn't exist as a CGI parameter.
*/
static void db_sql_cgi(sqlite3_context *context, int argc, sqlite3_value **argv){
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
** This is used by the [commit] command.
**
** Return true if either:
**
**     a) Global.aCommitFile is NULL, or
**     b) Global.aCommitFile contains the integer passed as an argument.
**
** Otherwise return false.
*/
static void file_is_selected(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  assert(argc==1);
  if( g.aCommitFile ){
    int iId = sqlite3_value_int(argv[0]);
    int ii;
    for(ii=0; g.aCommitFile[ii]; ii++){
      if( iId==g.aCommitFile[ii] ){
        sqlite3_result_int(context, 1);
        return;
      }
    }
    sqlite3_result_int(context, 0);
  }else{
    sqlite3_result_int(context, 1);
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
    strcpy(zHash, blob_str(&out));
    blob_reset(&out);
    db_multi_exec(
       "INSERT OR IGNORE INTO concealed VALUES(%Q,%#Q)",
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
  if( g.okRdAddr ){
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
** This function registers auxiliary functions when the SQLite
** database connection is first established.
*/
LOCAL void db_connection_init(void){
  sqlite3_exec(g.db, "PRAGMA foreign_keys=OFF;", 0, 0, 0);
  sqlite3_create_function(g.db, "user", 0, SQLITE_ANY, 0, db_sql_user, 0, 0);
  sqlite3_create_function(g.db, "cgi", 1, SQLITE_ANY, 0, db_sql_cgi, 0, 0);
  sqlite3_create_function(g.db, "cgi", 2, SQLITE_ANY, 0, db_sql_cgi, 0, 0);
  sqlite3_create_function(g.db, "print", -1, SQLITE_UTF8, 0,db_sql_print,0,0);
  sqlite3_create_function(
    g.db, "file_is_selected", 1, SQLITE_UTF8, 0, file_is_selected,0,0
  );
  if( g.fSqlTrace ){
    sqlite3_trace(g.db, db_sql_trace, 0);
  }
}

/*
** Return true if the string zVal represents "true" (or "false").
*/
int is_truth(const char *zVal){
  static const char *azOn[] = { "on", "yes", "true", "1" };
  int i;
  for(i=0; i<sizeof(azOn)/sizeof(azOn[0]); i++){
    if( strcmp(zVal,azOn[i])==0 ) return 1;
  }
  return 0;
}
int is_false(const char *zVal){
  static const char *azOff[] = { "off", "no", "false", "0" };
  int i;
  for(i=0; i<sizeof(azOff)/sizeof(azOff[0]); i++){
    if( strcmp(zVal,azOff[i])==0 ) return 1;
  }
  return 0;
}

/*
** Swap the g.db and g.dbConfig connections so that the various db_* routines
** work on the ~/.fossil database instead of on the repository database.
** Be sure to swap them back after doing the operation.
**
** If g.useAttach that means the ~/.fossil database was opened with
** the useAttach flag set to 1.  In that case no connection swap is required
** so this routine is a no-op.
*/
void db_swap_connections(void){
  if( !g.useAttach ){
    sqlite3 *dbTemp = g.db;
    g.db = g.dbConfig;
    g.dbConfig = dbTemp;
  }
}

/*
** Get and set values from the CONFIG, GLOBAL_CONFIG and VVAR table in the
** repository and local databases.
*/
char *db_get(const char *zName, char *zDefault){
  char *z = 0;
  if( g.repositoryOpen ){
    z = db_text(0, "SELECT value FROM config WHERE name=%Q", zName);
  }
  if( z==0 && g.configOpen ){
    db_swap_connections();
    z = db_text(0, "SELECT value FROM global_config WHERE name=%Q", zName);
    db_swap_connections();
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
    db_multi_exec("REPLACE INTO config(name,value) VALUES(%Q,%Q)",
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
  if( g.configOpen ){
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
  if( rc==SQLITE_DONE && g.configOpen ){
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
    db_multi_exec("REPLACE INTO config(name,value) VALUES(%Q,%d)",
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
** Record the name of a local repository in the global_config() database.
** The repository filename %s is recorded as an entry with a "name" field
** of the following form:
**
**       repo:%s
**
** The value field is set to 1.
*/
void db_record_repository_filename(const char *zName){
  Blob full;
  if( zName==0 ){
    if( !g.localOpen ) return;
    zName = db_lget("repository", 0);
  }
  file_canonical_name(zName, &full);
  db_swap_connections();
  db_multi_exec(
     "INSERT OR IGNORE INTO global_config(name,value)"
     "VALUES('repo:%q',1)",
     blob_str(&full)
  );
  db_swap_connections();
  blob_reset(&full);
}

/*
** COMMAND: open
**
** Usage: %fossil open FILENAME ?VERSION? ?--keep?
**
** Open a connection to the local repository in FILENAME.  A checkout
** for the repository is created with its root at the working directory.
** If VERSION is specified then that version is checked out.  Otherwise
** the latest version is checked out.  No files other than "manifest"
** and "manifest.uuid" are modified if the --keep option is present.
**
** See also the "close" command.
*/
void cmd_open(void){
  Blob path;
  int vid;
  int keepFlag;
  static char *azNewArgv[] = { 0, "checkout", "--prompt", "--latest", 0, 0 };
  url_proxy_options();
  keepFlag = find_option("keep",0,0)!=0;
  if( g.argc!=3 && g.argc!=4 ){
    usage("REPOSITORY-FILENAME ?VERSION?");
  }
  if( db_open_local() ){
    fossil_panic("already within an open tree rooted at %s", g.zLocalRoot);
  }
  file_canonical_name(g.argv[2], &path);
  db_open_repository(blob_str(&path));
  db_init_database("./_FOSSIL_", zLocalSchema, (char*)0);
  db_open_local();
  db_lset("repository", blob_str(&path));
  db_record_repository_filename(blob_str(&path));
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
    g.argc = 4;
    if( oldArgc==4 ){
      azNewArgv[g.argc-1] = oldArgv[3];
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
static void print_setting(const char *zName){
  Stmt q;
  if( g.repositoryOpen ){
    db_prepare(&q,
       "SELECT '(local)', value FROM config WHERE name=%Q"
       " UNION ALL "
       "SELECT '(global)', value FROM global_config WHERE name=%Q",
       zName, zName
    );
  }else{
    db_prepare(&q,
      "SELECT '(global)', value FROM global_config WHERE name=%Q",
      zName
    );
  }
  if( db_step(&q)==SQLITE_ROW ){
    printf("%-20s %-8s %s\n", zName, db_column_text(&q, 0),
        db_column_text(&q, 1));
  }else{
    printf("%-20s\n", zName);
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
  char const *def;      /* Default value */
};
#endif /* INTERFACE */
struct stControlSettings const ctrlSettings[] = {
  { "auto-captcha",  "autocaptcha",    0, "on"                  },
  { "auto-shun",     0,                0, "on"                  },
  { "autosync",      0,                0, "on"                  },
  { "binary-glob",   0,               32, ""                    },
  { "clearsign",     0,                0, "off"                 },
  { "diff-command",  0,               16, ""                    },
  { "dont-push",     0,                0, "off"                 },
  { "editor",        0,               16, ""                    },
  { "gdiff-command", 0,               16, "gdiff"               },
  { "ignore-glob",   0,               40, ""                    },
  { "http-port",     0,               16, "8080"                },
  { "localauth",     0,                0, "off"                 },
  { "manifest",      0,                0, "off"                 },
  { "mtime-changes", 0,                0, "on"                  },
  { "pgp-command",   0,               32, "gpg --clearsign -o " },
  { "proxy",         0,               32, "off"                 },
  { "repo-cksum",    0,                0, "on"                  },
  { "ssh-command",   0,               32, ""                    },
  { "web-browser",   0,               32, ""                    },
  { 0,0,0,0 }
};

/*
** COMMAND: settings
** COMMAND: unset
** %fossil settings ?PROPERTY? ?VALUE? ?-global?
** %fossil unset PROPERTY ?-global?
**
** The "settings" command with no arguments lists all properties and their
** values.  With just a property name it shows the value of that property.
** With a value argument it changes the property for the current repository.
**
** The "unset" command clears a property setting.
**
**
**    auto-captcha     If enabled, the Login page provides a button to
**                     fill in the captcha password.  Default: on
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
**    binary-glob      The VALUE is a comma-separated list of GLOB patterns
**                     that should be treated as binary files for merging
**                     purposes.  Example:   *.xml
**
**    clearsign        When enabled, fossil will attempt to sign all commits
**                     with gpg.  When disabled (the default), commits will
**                     be unsigned.  Default: off
**
**    diff-command     External command to run when performing a diff.
**                     If undefined, the internal text diff will be used.
**
**    dont-push        Prevent this repository from pushing from client to
**                     server.  Useful when setting up a private branch.
**
**    editor           Text editor command used for check-in comments.
**
**    gdiff-command    External command to run when performing a graphical
**                     diff. If undefined, text diff will be used.
**
**    http-port        The TCP/IP port number to use by the "server"
**                     and "ui" commands.  Default: 8080
**
**    ignore-glob      The VALUE is a comma-separated list of GLOB patterns
**                     specifying files that the "extra" command will ignore.
**                     Example:  *.o,*.obj,*.exe
**
**    localauth        If enabled, require that HTTP connections from
**                     127.0.0.1 be authenticated by password.  If
**                     false, all HTTP requests from localhost have
**                     unrestricted access to the repository.
**
**    manifest         If enabled, automatically create files "manifest" and
**                     "manifest.uuid" in every checkout.  The SQLite and
**                     Fossil repositories both require this.  Default: off.
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
**    repo-cksum       Compute checksums over all files in each checkout
**                     as a double-check of correctness.  Defaults to "on".
**                     Disable on large repositories for a performance
**                     improvement.
**
**    ssh-command      Command used to talk to a remote machine with
**                     the "ssh://" protocol.
**
**    web-browser      A shell command used to launch your preferred
**                     web browser when given a URL as an argument.
**                     Defaults to "start" on windows, "open" on Mac,
**                     and "firefox" on Unix.
*/
void setting_cmd(void){
  int i;
  int globalFlag = find_option("global","g",0)!=0;
  int unsetFlag = g.argv[1][0]=='u';
  db_open_config(1);
  db_find_and_open_repository(0);
  if( !g.repositoryOpen ){
    globalFlag = 1;
  }
  if( unsetFlag && g.argc!=3 ){
    usage("PROPERTY ?-global?");
  }
  if( g.argc==2 ){
    for(i=0; ctrlSettings[i].name; i++){
      print_setting(ctrlSettings[i].name);
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
    isManifest = strcmp(ctrlSettings[i].name, "manifest")==0;
    if( isManifest && globalFlag ){
      fossil_fatal("cannot set 'manifest' globally");
    }
    if( unsetFlag ){
      db_unset(ctrlSettings[i].name, globalFlag);
    }else if( g.argc==4 ){
      db_set(ctrlSettings[i].name, g.argv[3], globalFlag);
    }else{
      isManifest = 0;
      print_setting(ctrlSettings[i].name);
    }
    if( isManifest ){
      manifest_to_disk(db_lget_int("checkout", 0));
    }
  }else{
    usage("?PROPERTY? ?VALUE?");
  }
}

/*
** The input in a a timespan measured in days.  Return a string which
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
  printf("Time differences: %s\n", db_timespan_name(rDiff));
  sqlite3_close(g.db);
  g.db = 0;
}
