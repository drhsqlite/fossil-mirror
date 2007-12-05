/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
**
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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
**    (3)  A local checkout database named "FOSSIL" and located at the
**         root of the local copy of the source tree.
**
*/
#include "config.h"
#ifndef __MINGW32__
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
};
#endif /* INTERFACE */

/*
** Call this routine when a database error occurs.
*/
static void db_err(const char *zFormat, ...){
  va_list ap;
  char *z;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.cgiPanic ){
    g.cgiPanic = 0;
    cgi_printf("<p><font color=\"red\">%h</font></p>", z);
    style_footer();
    cgi_reply();
  }else{
    fprintf(stderr, "%s: %s\n", g.argv[0], z);
  }
  db_force_rollback();
  exit(1);
}

static int nBegin = 0;      /* Nesting depth of BEGIN */
static int doRollback = 0;  /* True to force a rollback */
static int nCommitHook = 0; /* Number of commit hooks */
static struct sCommitHook {
  int (*xHook)(void);  /* Functions to call at db_end_transaction() */
  int sequence;        /* Call functions in sequence order */
} aHook[5];

/*
** This routine is called by the SQLite commit-hook mechanism
** just prior to each omit.  All this routine does is verify
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
  if( nBegin ){
    sqlite3_exec(g.db, "ROLLBACK", 0, 0, 0);
  }
  nBegin = 0;
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
** Prepare or reprepare the sqlite3 statement from the raw SQL text.
*/
static void reprepare(Stmt *pStmt){
  sqlite3_stmt *pNew;
  if( sqlite3_prepare(g.db, blob_buffer(&pStmt->sql), -1, &pNew, 0)!=0 ){
    db_err("%s\n%s", blob_str(&pStmt->sql), sqlite3_errmsg(g.db));
  }
  if( pStmt->pStmt ){
    sqlite3_transfer_bindings(pStmt->pStmt, pNew);
    sqlite3_finalize(pStmt->pStmt);
  }
  pStmt->pStmt = pNew;
}

/*
** Prepare a Stmt.  Assume that the Stmt is previously uninitialized.
** If the input string contains multiple SQL statements, only the first
** one is processed.  All statements beyond the first are silently ignored.
*/
int db_vprepare(Stmt *pStmt, const char *zFormat, va_list ap){
  blob_zero(&pStmt->sql);
  blob_vappendf(&pStmt->sql, zFormat, ap);
  va_end(ap);
  pStmt->pStmt = 0;
  reprepare(pStmt);
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
    db_err("no such bind parameter: %s\n%b", zParamName, &pStmt->sql);
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
  int rc = SQLITE_OK;
  int limit = 3;
  while( limit-- ){
    rc = sqlite3_step(pStmt->pStmt);
    if( rc==SQLITE_ERROR ){
      rc = sqlite3_reset(pStmt->pStmt);
    }
    if( rc==SQLITE_SCHEMA ){
      reprepare(pStmt);
    }else{
      break;
    }
  }
  return rc;
}

/*
** Reset or finalize a statement.
*/
int db_reset(Stmt *pStmt){
  int rc = sqlite3_reset(pStmt->pStmt);
  db_check_result(rc);
  return rc;
}
int db_finalize(Stmt *pStmt){
  int rc;
  blob_reset(&pStmt->sql);
  rc = sqlite3_finalize(pStmt->pStmt);
  db_check_result(rc);
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
  char *z = zDefault;
  va_start(ap, zSql);
  db_vprepare(&s, zSql, ap);
  va_end(ap);
  if( db_step(&s)==SQLITE_ROW ){
    z = mprintf("%s", sqlite3_column_text(s.pStmt, 0));
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

  rc = sqlite3_open(zFileName, &db);
  if( rc!=SQLITE_OK ){
    db_err(sqlite3_errmsg(g.db));
  }
  sqlite3_exec(db, "BEGIN EXCLUSIVE", 0, 0, 0);
  rc = sqlite3_exec(db, zSchema, 0, 0, 0);
  if( rc!=SQLITE_OK ){
    db_err(sqlite3_errmsg(g.db));
  }
  va_start(ap, zSchema);
  while( (zSql = va_arg(ap, const char*))!=0 ){
    rc = sqlite3_exec(db, zSql, 0, 0, 0);
    if( rc!=SQLITE_OK ){
      db_err(sqlite3_errmsg(g.db));
    }
  }
  va_end(ap);
  sqlite3_exec(db, "COMMIT", 0, 0, 0);
  sqlite3_close(db);
}

/*
** zDbName is the name of a database file.  If no other database
** file is open, then open this one.  If another database file is
** already open, then attach zDbName using the name zLabel.
*/
void db_open_or_attach(const char *zDbName, const char *zLabel){
  if( !g.db ){
    int rc = sqlite3_open(zDbName, &g.db);
    if( rc!=SQLITE_OK ){
      db_err(sqlite3_errmsg(g.db));
    }
    db_connection_init();
  }else{
    db_multi_exec("ATTACH DATABASE %Q AS %s", zDbName, zLabel);
  }
}

/*
** Open the user database in "~/.fossil".  Create the database anew if
** it does not already exist.
*/
void db_open_config(void){
  char *zDbName;
  const char *zHome;
#ifdef __MINGW32__
  zHome = getenv("LOCALAPPDATA");
  if( zHome==0 ){
    zHome = getenv("APPDATA");
    if( zHome==0 ){
      zHome = getenv("HOMEPATH");
    }
  }
#else
  zHome = getenv("HOME");
#endif
  if( zHome==0 ){
    db_err("cannot local home directory");
  }
#ifdef __MINGW32__
  /* . filenames give some window systems problems and many apps problems */
  zDbName = mprintf("%s/_fossil", zHome);
#else
  zDbName = mprintf("%s/.fossil", zHome);
#endif
  if( g.configOpen ) return;
  if( file_size(zDbName)<1024*3 ){
    db_init_database(zDbName, zConfigSchema, (char*)0);
  }
  db_open_or_attach(zDbName, "configdb");
  g.configOpen = 1;
}

/*
** If zDbName is a valid local database file, open it and return
** true.  If it is not a valid local database file, return 0.
*/
static int isValidLocalDb(const char *zDbName){
  i64 lsize;
  if( access(zDbName, F_OK) ) return 0;
  lsize = file_size(zDbName);
  if( lsize%1024!=0 || lsize<4096 ) return 0;
  db_open_or_attach(zDbName, "localdb");
  g.localOpen = 1;
  db_open_config();
  db_open_repository(0);
  return 1;
}

/*
** Locate the root directory of the local repository tree.  The root
** directory is found by searching for a file named "FOSSIL" that contains
** a valid repository database.
**
** If no valid FOSSIL file is found, we move up one level and try again.
** Once the file is found, the g.zLocalRoot variable is set to the root of
** the repository tree and this routine returns 1.  If no database is
** found, then this routine return 0.
**
** This routine always opens the user database regardless of whether or
** not the repository database is found.  If the FOSSIL file is found,
** it is attached to the open database connection too.
*/
int db_open_local(void){
  int n;
  char zPwd[2000];
  char *zPwdConv;
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
    strcpy(&zPwd[n], "/_FOSSIL_");
    if( isValidLocalDb(zPwd) ){
      /* Found a valid _FOSSIL_ file */
      zPwd[n] = 0;
      g.zLocalRoot = mprintf("%s/", zPwd);
      return 1;
    }
    n--;
    while( n>0 && zPwd[n]!='/' ){ n--; }
    while( n>0 && zPwd[n-1]=='/' ){ n--; }
    zPwd[n] = 0;
  }

  /* A _FOSSIL_ file could not be found */
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
    fossil_panic("no such repository: %s", zDbName);
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
void db_find_and_open_repository(void){
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
  fossil_fatal("use --repository or -R to specific the repository database");
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
  if( g.db==0 ) return;
  g.repositoryOpen = 0;
  g.localOpen = 0;
  g.configOpen = 0;
  sqlite3_close(g.db);
  g.db = 0;
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
}

/*
** Fill an empty repository database with the basic information for a
** repository. This function is shared between 'create_repository_cmd'
** ('new') and 'reconstruct_cmd' ('reconstruct'), both of which create
** new repositories.
**
** The makeInitialVersion flag determines whether or not an initial
** manifest is created.  The makeServerCodes flag determines whether or
** not server and project codes are invented for this repository.
*/
void db_initial_setup (int makeInitialVersion, int makeServerCodes){
  char *zDate;
  const char *zUser;
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
  if( !db_is_global("safemerge") ) db_set_int("safemerge", 0, 0);
  if( !db_is_global("localauth") ) db_set_int("localauth", 0, 0);
  zUser = db_get("default-user", 0);
  if( zUser==0 ){
#ifdef __MINGW32__
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
     "VALUES(%Q,'','s','')", zUser
  );
  db_multi_exec(
     "INSERT INTO user(login,pw,cap,info)"
     "   VALUES('anonymous','anonymous','hjkorw','Anon');"
     "INSERT INTO user(login,pw,cap,info)"
     "   VALUES('nobody','','jor','Nobody');"
  );
  user_select();

  if (makeInitialVersion){
    blob_zero(&manifest);
    blob_appendf(&manifest, "C initial\\sempty\\sbaseline\n");
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10]='T';
    blob_appendf(&manifest, "D %s\n", zDate);
    blob_appendf(&manifest, "P\n");
    md5sum_init();
    blob_appendf(&manifest, "R %s\n", md5sum_finish(0));
    blob_appendf(&manifest, "U %F\n", g.zLogin);
    md5sum_blob(&manifest, &hash);
    blob_appendf(&manifest, "Z %b\n", &hash);
    blob_reset(&hash);
    content_put(&manifest, 0, 0);
  }
}

/*
** COMMAND: new
**
** Usage: %fossil new FILENAME
** Create a repository for a new project in the file named FILENAME.
** This command is distinct from "clone".  The "clone" command makes
** a copy of an existing project.  This command starts a new project.
*/
void create_repository_cmd(void){
  if( g.argc!=3 ){
    usage("REPOSITORY-NAME");
  }
  db_create_repository(g.argv[2]);
  db_open_repository(g.argv[2]);
  db_open_config();
  db_begin_transaction();
  db_initial_setup(1, 1);
  db_end_transaction(0);
  printf("project-id: %s\n", db_get("project-code", 0));
  printf("server-id:  %s\n", db_get("server-code", 0));
  printf("admin-user: %s (no password set yet!)\n", g.zLogin);
  printf("baseline:   %s\n", db_text(0, "SELECT uuid FROM blob"));
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
  printf("%s\n", zSql);
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
** This function registers auxiliary functions when the SQLite
** database connection is first established.
*/
LOCAL void db_connection_init(void){
  static int once = 1;
  if( once ){
    sqlite3_create_function(g.db, "print", -1, SQLITE_UTF8, 0,db_sql_print,0,0);
    sqlite3_create_function(
      g.db, "file_is_selected", 1, SQLITE_UTF8, 0, file_is_selected,0,0
    );
    if( g.fSqlTrace ){
      sqlite3_trace(g.db, db_sql_trace, 0);
    }
    once = 0;
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
    z = db_text(0, "SELECT value FROM global_config WHERE name=%Q", zName);
  }
  if( z==0 ){
    z = zDefault;
  }
  return z;
}
void db_set(const char *zName, const char *zValue, int globalFlag){
  db_begin_transaction();
  db_multi_exec("REPLACE INTO %sconfig(name,value) VALUES(%Q,%Q)",
                 globalFlag ? "global_" : "", zName, zValue);
  if( globalFlag && g.repositoryOpen ){
    db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
  }
  db_end_transaction(0);
}
int db_is_global(const char *zName){
  if( g.configOpen ){
    return db_exists("SELECT 1 FROM global_config WHERE name=%Q", zName);
  }else{
    return 0;
  }
}
int db_get_int(const char *zName, int dflt){
  int v;
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
    v = db_int(dflt, "SELECT value FROM global_config WHERE name=%Q", zName);
  }
  return v;
}
void db_set_int(const char *zName, int value, int globalFlag){
  db_begin_transaction();
  db_multi_exec("REPLACE INTO %sconfig(name,value) VALUES(%Q,%d)",
                globalFlag ? "global_" : "", zName, value);
  if( globalFlag && g.repositoryOpen ){
    db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
  }
  db_end_transaction(0);
}
int db_get_boolean(const char *zName, int dflt){
  static const char *azOn[] = { "on", "yes", "true", "1" };
  static const char *azOff[] = { "off", "no", "false", "0" };
  int i;
  char *zVal = db_get(zName, dflt ? "on" : "off");
  for(i=0; i<sizeof(azOn)/sizeof(azOn[0]); i++){
    if( strcmp(zVal,azOn[i])==0 ) return 1;
  }
  for(i=0; i<sizeof(azOff)/sizeof(azOff[0]); i++){
    if( strcmp(zVal,azOff[i])==0 ) return 0;
  }
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
** COMMAND: open
**
** Usage: open FILENAME
**
** Open a connection to the local repository in FILENAME.  A checkout
** for the repository is created with its root at the working directory.
** See also the "close" command.
*/
void cmd_open(void){
  Blob path;
  int vid;
  static char *azNewArgv[] = { 0, "update", "--latest", 0 };
  if( g.argc!=3 ){
    usage("REPOSITORY-FILENAME");
  }
  if( db_open_local() ){
    fossil_panic("already within an open tree rooted at %s", g.zLocalRoot);
  }
  file_canonical_name(g.argv[2], &path);
  db_open_repository(blob_str(&path));
  db_init_database("./_FOSSIL_", zLocalSchema, (char*)0);
  db_open_local();
  db_lset("repository", blob_str(&path));
  vid = db_int(0, "SELECT pid FROM plink y"
                  " WHERE NOT EXISTS(SELECT 1 FROM plink x WHERE x.cid=y.pid)");
  if( vid==0 ){
    db_lset_int("checkout", 1);
  }else{
    db_lset_int("checkout", vid);
    g.argv = azNewArgv;
    g.argc = 3;
    update_cmd();
  }
}

/*
** Print the value of a setting named zName
*/
static void print_setting(const char *zName){
  Stmt q;
  db_prepare(&q,
     "SELECT '(local)', value FROM config WHERE name=%Q"
     " UNION ALL "
     "SELECT '(global)', value FROM global_config WHERE name=%Q",
     zName, zName
  );
  if( db_step(&q)==SQLITE_ROW ){
    printf("%-20s %-8s %s\n", zName, db_column_text(&q, 0),
        db_column_text(&q, 1));
  }else{
    printf("%-20s\n", zName);
  }
  db_finalize(&q);
}


/*
** COMMAND: settings
** %fossil setting ?PROPERTY? ?VALUE? ?-global?
**
** With no arguments, list all properties and their values.  With just
** a property name, show the value of that property.  With a value
** argument, change the property for the current repository.
**
**    autosync         If enabled, automatically pull prior to
**                     commit or update and automatically push
**                     after commit or tag or branch creation.
**
**    clearsign        Command used to clear-sign manifests at check-in.
**                     The default is "gpg --clearsign -o ".
**
**    editor           Text editor command used for check-in comments.
**
**    localauth        If enabled, require that HTTP connections from
**                     127.0.0.1 be authenticated by password.  If
**                     false, all HTTP requests from localhost have
**                     unrestricted access to the repository.
**
**    omitsign         When enabled, fossil will not attempt to sign any
**                     commit with gpg. All commits will be unsigned.
**
**    safemerge        If enabled, when commit will cause a fork, the
**                     commit will not abort with warning. Also update
**                     will not be allowed if local changes exist.
**
**   diff-command      External command to run when performing a diff.
**                     If undefined, the internal text diff will be used.
**
**   gdiff-command     External command to run when performing a graphical
**                     diff. If undefined, text diff will be used.
*/
void setting_cmd(void){
  static const char *azName[] = {
    "autosync",
    "clearsign",
    "editor",
    "localauth",
    "omitsign",
    "safemerge",
    "diff-command",
    "gdiff-command",
  };
  int i;
  int globalFlag = find_option("global","g",0)!=0;
  db_find_and_open_repository();
  if( g.argc==2 ){
    for(i=0; i<sizeof(azName)/sizeof(azName[0]); i++){
      print_setting(azName[i]);
    }
  }else if( g.argc==3 || g.argc==4 ){
    const char *zName = g.argv[2];
    int n = strlen(zName);
    for(i=0; i<sizeof(azName)/sizeof(azName[0]); i++){
      if( strncmp(azName[i], zName, n)==0 ) break;
    }
    if( i>=sizeof(azName)/sizeof(azName[0]) ){
      fossil_fatal("no such setting: %s", zName);
    }
    if( g.argc==4 ){
      db_set(azName[i], g.argv[3], globalFlag);
    }else{
      print_setting(azName[i]);
    }
  }else{
    usage("?PROPERTY? ?VALUE?");
  }
}
