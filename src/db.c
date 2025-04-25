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
**    (1)  The "configdb" database in ~/.fossil or ~/.config/fossil.db
**         or in %LOCALAPPDATA%/_fossil
**
**    (2)  The "repository" database
**
**    (3)  A local check-out database named "_FOSSIL_" or ".fslckout"
**         and located at the root of the local copy of the source tree.
**
*/
#include "config.h"
#if defined(_WIN32)
#  if USE_SEE
#    include <windows.h>
#    define GETPID (int)GetCurrentProcessId
#  endif
#else
#  include <pwd.h>
#  if USE_SEE
#    define GETPID getpid
#  endif
#endif
#if USE_SEE && !defined(SQLITE_HAS_CODEC)
#  define SQLITE_HAS_CODEC
#endif
#if USE_SEE && defined(__linux__)
#  include <sys/uio.h>
#endif
#include <sqlite3.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

/* BUGBUG: This (PID_T) does not work inside of INTERFACE block. */
#if USE_SEE
#if defined(_WIN32)
typedef DWORD PID_T;
#else
typedef pid_t PID_T;
#endif
#endif

#include "db.h"

#if INTERFACE
/*
** Type definitions used for handling the saved encryption key for SEE.
*/
#if !defined(_WIN32)
typedef void *LPVOID;
typedef size_t SIZE_T;
#endif

/*
** Operations for db_maybe_handle_saved_encryption_key_for_process, et al.
*/
#define SEE_KEY_READ  ((int)0)
#define SEE_KEY_WRITE ((int)1)
#define SEE_KEY_ZERO  ((int)2)

/*
** An single SQL statement is represented as an instance of the following
** structure.
*/
struct Stmt {
  Blob sql;               /* The SQL for this statement */
  sqlite3_stmt *pStmt;    /* The results of sqlite3_prepare_v2() */
  Stmt *pNext, *pPrev;    /* List of all unfinalized statements */
  int nStep;              /* Number of sqlite3_step() calls */
  int rc;                 /* Error from db_vprepare() */
};

/*
** Copy this to initialize a Stmt object to a clean/empty state. This
** is useful to help avoid assertions when performing cleanup in some
** error handling cases.
*/
#define empty_Stmt_m {BLOB_INITIALIZER,NULL, NULL, NULL, 0, 0}
#endif /* INTERFACE */
const struct Stmt empty_Stmt = empty_Stmt_m;

/*
** Call this routine when a database error occurs.
** This routine throws a fatal error.  It does not return.
*/
static void db_err(const char *zFormat, ...){
  va_list ap;
  char *z;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
#ifdef FOSSIL_ENABLE_JSON
  if( g.json.isJsonMode!=0 ){
    /*
    ** Avoid calling into the JSON support subsystem if it
    ** has not yet been initialized, e.g. early SQLite log
    ** messages, etc.
    */
    json_bootstrap_early();
    json_err( 0, z, 1 );
  }
  else
#endif /* FOSSIL_ENABLE_JSON */
  if( g.xferPanic && g.cgiOutput==1 ){
    cgi_reset_content();
    @ error Database\serror:\s%F(z)
    cgi_reply();
  }
  fossil_fatal("Database error: %s", z);
}

/*
** Check a result code.  If it is not SQLITE_OK, print the
** corresponding error message and exit.
*/
static void db_check_result(int rc, Stmt *pStmt){
  if( rc!=SQLITE_OK ){
    db_err("SQL error (%d,%d: %s) while running [%s]",
       rc, sqlite3_extended_errcode(g.db),
       sqlite3_errmsg(g.db), blob_str(&pStmt->sql));
  }
}

/*
** All static variable that a used by only this file are gathered into
** the following structure.
*/
static struct DbLocalData {
  unsigned protectMask;     /* Prevent changes to database */
  int nBegin;               /* Nesting depth of BEGIN */
  int doRollback;           /* True to force a rollback */
  int nCommitHook;          /* Number of commit hooks */
  int wrTxn;                /* Outer-most TNX is a write */
  Stmt *pAllStmt;           /* List of all unfinalized statements */
  int nPrepare;             /* Number of calls to sqlite3_prepare_v2() */
  int nDeleteOnFail;        /* Number of entries in azDeleteOnFail[] */
  struct sCommitHook {
    int (*xHook)(void);         /* Functions to call at db_end_transaction() */
    int sequence;               /* Call functions in sequence order */
  } aHook[6];
  char *azDeleteOnFail[3];  /* Files to delete on a failure */
  char *azBeforeCommit[5];  /* Commands to run prior to COMMIT */
  int nBeforeCommit;        /* Number of entries in azBeforeCommit */
  int nPriorChanges;        /* sqlite3_total_changes() at transaction start */
  const char *zStartFile;   /* File in which transaction was started */
  int iStartLine;           /* Line of zStartFile where transaction started */
  int (*xAuth)(void*,int,const char*,const char*,const char*,const char*);
  void *pAuthArg;           /* Argument to the authorizer */
  const char *zAuthName;    /* Name of the authorizer */
  int bProtectTriggers;     /* True if protection triggers already exist */
  int nProtect;             /* Slots of aProtect used */
  unsigned aProtect[12];    /* Saved values of protectMask */
  int pauseDmlLog;          /* Ignore pDmlLog if positive */
  Blob *pDmlLog;            /* Append DML statements here, of not NULL */
} db = {
  PROTECT_USER|PROTECT_CONFIG|PROTECT_BASELINE,  /* protectMask */
  0, 0, 0, 0, 0, 0, 0, {{0}}, {0}, {0}, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}};

/*
** Arrange for the given file to be deleted on a failure.
*/
void db_delete_on_failure(const char *zFilename){
  assert( db.nDeleteOnFail<count(db.azDeleteOnFail) );
  if( zFilename==0 ) return;
  db.azDeleteOnFail[db.nDeleteOnFail++] = fossil_strdup(zFilename);
}

/*
** Return the transaction nesting depth.  0 means we are currently
** not in a transaction.
*/
int db_transaction_nesting_depth(void){
  return db.nBegin;
}

/*
** Return a pointer to a string that is the code point where the
** current transaction was started.
*/
char *db_transaction_start_point(void){
  return mprintf("%s:%d", db.zStartFile, db.iStartLine);
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
** Silently add the filename and line number as parameter to each
** db_begin_transaction call.
*/
#if INTERFACE
#define db_begin_transaction()    db_begin_transaction_real(__FILE__,__LINE__)
#define db_begin_write()          db_begin_write_real(__FILE__,__LINE__)
#define db_commit_transaction()   db_end_transaction(0)
#define db_rollback_transaction() db_end_transaction(1)
#endif

/*
** Begin a nested transaction
*/
void db_begin_transaction_real(const char *zStartFile, int iStartLine){
  if( db.nBegin==0 ){
    db_multi_exec("BEGIN");
    sqlite3_commit_hook(g.db, db_verify_at_commit, 0);
    db.nPriorChanges = sqlite3_total_changes(g.db);
    db.doRollback = 0;
    db.zStartFile = zStartFile;
    db.iStartLine = iStartLine;
    db.wrTxn = 0;
  }
  db.nBegin++;
}
/*
** Begin a new transaction for writing.
*/
void db_begin_write_real(const char *zStartFile, int iStartLine){
  if( db.nBegin==0 ){
    if( !db_is_writeable("repository") ){
      db_multi_exec("BEGIN");
    }else{
      db_multi_exec("BEGIN IMMEDIATE");
      sqlite3_commit_hook(g.db, db_verify_at_commit, 0);
      db.nPriorChanges = sqlite3_total_changes(g.db);
      db.doRollback = 0;
      db.zStartFile = zStartFile;
      db.iStartLine = iStartLine;
      db.wrTxn = 1;
    }
  }else if( !db.wrTxn ){
    fossil_warning("read txn at %s:%d might cause SQLITE_BUSY "
       "for the write txn at %s:%d",
       db.zStartFile, db.iStartLine, zStartFile, iStartLine);
  }
  db.nBegin++;
}

/* End a transaction previously started using db_begin_transaction()
** or db_begin_write().
*/
void db_end_transaction(int rollbackFlag){
  if( g.db==0 ) return;
  if( db.nBegin<=0 ){
    fossil_warning("Extra call to db_end_transaction");
    return;
  }
  if( rollbackFlag ){
    db.doRollback = 1;
    if( g.fSqlTrace ) fossil_trace("-- ROLLBACK by request\n");
  }
  db.nBegin--;
  if( db.nBegin==0 ){
    int i;
    if( db.doRollback==0 && db.nPriorChanges<sqlite3_total_changes(g.db) ){
      i = 0;
      db_protect_only(PROTECT_SENSITIVE);
      while( db.nBeforeCommit ){
        db.nBeforeCommit--;
        sqlite3_exec(g.db, db.azBeforeCommit[i], 0, 0, 0);
        sqlite3_free(db.azBeforeCommit[i]);
        i++;
      }
      leaf_do_pending_checks();
      db_protect_pop();
    }
    for(i=0; db.doRollback==0 && i<db.nCommitHook; i++){
      int rc = db.aHook[i].xHook();
      if( rc ){
        db.doRollback = 1;
        if( g.fSqlTrace ) fossil_trace("-- ROLLBACK due to aHook[%d]\n", i);
      }
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
** Flag bits for db_protect() and db_unprotect() indicating which parts
** of the databases should be write protected or write enabled, respectively.
*/
#define PROTECT_USER       0x01  /* USER table */
#define PROTECT_CONFIG     0x02  /* CONFIG and GLOBAL_CONFIG tables */
#define PROTECT_SENSITIVE  0x04  /* Sensitive and/or global settings */
#define PROTECT_READONLY   0x08  /* everything except TEMP tables */
#define PROTECT_BASELINE   0x10  /* protection system is working */
#define PROTECT_ALL        0x1f  /* All of the above */
#define PROTECT_NONE       0x00  /* Nothing.  Everything is open */
#endif /* INTERFACE */

/*
** Enable or disable database write protections.
**
**    db_protext(X)         Add protects on X
**    db_unprotect(X)       Remove protections on X
**    db_protect_only(X)    Remove all prior protections then set
**                          protections to only X.
**
** Each of these routines pushes the previous protection mask onto
** a finite-size stack.  Each should be followed by a call to
** db_protect_pop() to pop the stack and restore the protections that
** existed prior to the call.  The protection mask stack has a limited
** depth, so take care not to nest calls too deeply.
**
** About Database Write Protection
** -------------------------------
**
** This is *not* a primary means of defending the application from
** attack.  Fossil should be secure even if this mechanism is disabled.
** The purpose of database write protection is to provide an additional
** layer of defense in case SQL injection bugs somehow slip into other
** parts of the system.  In other words, database write protection is
** not the primary defense but rather defense in depth.
**
** This mechanism mostly focuses on the USER table, to prevent an
** attacker from giving themselves Admin privilegs, and on the
** CONFIG table and especially "sensitive" settings such as
** "diff-command" or "editor" that if compromised by an attacker
** could lead to an RCE.
**
** By default, the USER and CONFIG tables are read-only.  Various
** subsystems that legitimately need to change those tables can
** temporarily do so using:
**
**     db_unprotect(PROTECT_xxx);
**     // make the legitmate changes here
**     db_protect_pop();
**
** Code that runs inside of reduced protections should be carefully
** reviewed to ensure that it is harmless and not subject to SQL
** injection.
**
** Read-only operations (such as many web pages like /timeline)
** can invoke db_protect(PROTECT_ALL) to effectively make the database
** read-only.  TEMP tables (which are often used for these kinds of
** pages) are still writable, however.
**
** The PROTECT_SENSITIVE protection is a subset of PROTECT_CONFIG
** that blocks changes to all of the global_config table, but only
** "sensitive" settings in the config table.  PROTECT_SENSITIVE
** relies on triggers and the protected_setting() SQL function to
** prevent changes to sensitive settings.
**
** PROTECT_READONLY is set for any HTTP request for which the HTTP_REFERER
** is not the same origin.  This is an additional defense against cross-site-
** scripting attacks.  As with all of these defenses, this is only an extra
** backup layer.  Fossil should be proof against XSS attacks even without this.
**
** Any violation of these security restrictions results in a SECURITY message
** in the server log (if enabled).  A violation of any of these restrictions
** probably indicates a bug in Fossil and should be reported to the
** developers.
**
** Additional Notes
** ----------------
**
** Calls to routines like db_set() and db_unset() temporarily disable
** the PROTECT_CONFIG protection.  The assumption is that these calls
** cannot be invoked by an SQL injection and are thus safe.  Make sure
** this is the case by always using a string literal as the name argument
** to db_set() and db_unset() and friend, not a variable that might
** be compromised by an attack.
*/
void db_protect_only(unsigned flags){
  if( db.nProtect>=count(db.aProtect)-2 ){
    fossil_panic("too many db_protect() calls");
  }
  db.aProtect[db.nProtect++] = db.protectMask;
  if( (flags & PROTECT_SENSITIVE)!=0
   && db.bProtectTriggers==0
   && g.repositoryOpen
  ){
    /* Create the triggers needed to protect sensitive settings from
    ** being created or modified the first time that PROTECT_SENSITIVE
    ** is enabled.  Deleting a sensitive setting is harmless, so there
    ** is not trigger to block deletes.  After being created once, the
    ** triggers persist for the life of the database connection. */
    unsigned savedProtectMask = db.protectMask;
    db.protectMask = 0;
    db_multi_exec(
      "CREATE TEMP TRIGGER protect_1 BEFORE INSERT ON config"
      " WHEN protected_setting(new.name) BEGIN"
      "  SELECT raise(abort,'not authorized');"
      "END;\n"
      "CREATE TEMP TRIGGER protect_2 BEFORE UPDATE ON config"
      " WHEN protected_setting(new.name) BEGIN"
      "  SELECT raise(abort,'not authorized');"
      "END;\n"
    );
    db.bProtectTriggers = 1;
    db.protectMask = savedProtectMask;
  }
  db.protectMask = flags;
}
void db_protect(unsigned flags){
  db_protect_only(db.protectMask | flags);
}
void db_unprotect(unsigned flags){
  if( db.nProtect>=count(db.aProtect)-2 ){
    fossil_panic("too many db_unprotect() calls");
  }
  db.aProtect[db.nProtect++] = db.protectMask;
  db.protectMask &= ~(flags|PROTECT_READONLY);
}
void db_protect_pop(void){
  if( db.nProtect<1 ){
    fossil_panic("too many db_protect_pop() calls");
  }
  db.protectMask = db.aProtect[--db.nProtect];
}
int db_is_protected(unsigned flags){
  return (db.protectMask & flags)!=0;
}

/*
** Verify that the desired database write protections are in place.
** Throw a fatal error if not.
*/
void db_assert_protected(unsigned flags){
  if( (flags & db.protectMask)!=flags ){
    fossil_fatal("missing database write protection bits: %02x",
                 flags & ~db.protectMask);
  }
}

/*
** Assert that either all protections are off (including PROTECT_BASELINE
** which is usually always enabled), or the setting named in the argument
** is no a sensitive setting.
**
** This assert() is used to verify that the db_set() and db_set_int()
** interfaces do not modify a sensitive setting.
*/
void db_assert_protection_off_or_not_sensitive(const char *zName){
  if( db.protectMask!=0 && db_setting_is_protected(zName) ){
    fossil_panic("unauthorized change to protected setting \"%s\"", zName);
  }
}

/*
** Every Fossil database connection automatically registers the following
** overarching authenticator callback, and leaves it registered for the
** duration of the connection.  This authenticator will call any
** sub-authenticators that are registered using db_set_authorizer().
**
** == Testing Notes ==
**
** Run Fossil as using a command like this:
**
**     ./fossil sql --test --errorlog -
**
** Then enter SQL commands like one of these:
**
**     SELECT db_protect('user');
**     SELECT db_protect('config');
**     SELECT db_protect('sensitive');
**     SELECT db_protect('readonly');
**     SELECT db_protect('all');
**
** Then try to do SQL statements that would violate the constraints and
** verify that SECURITY warnings appear in the error log output.  See
** also the sqlcmd_db_protect() function in sqlcmd.c.
*/
int db_top_authorizer(
  void *pNotUsed,
  int eCode,
  const char *z0,
  const char *z1,
  const char *z2,
  const char *z3
){
  int rc = SQLITE_OK;
  switch( eCode ){
    case SQLITE_INSERT:
    case SQLITE_UPDATE:
    case SQLITE_DELETE: {
      if( (db.protectMask & PROTECT_USER)!=0
          && sqlite3_stricmp(z0,"user")==0 ){
        fossil_errorlog(
          "SECURITY: authorizer blocks DML on protected USER table\n");
        rc = SQLITE_DENY;
      }else if( (db.protectMask & PROTECT_CONFIG)!=0 &&
               (sqlite3_stricmp(z0,"config")==0 ||
                sqlite3_stricmp(z0,"global_config")==0) ){
        fossil_errorlog(
          "SECURITY: authorizer blocks DML on protected table \"%s\"\n", z0);
        rc = SQLITE_DENY;
      }else if( (db.protectMask & PROTECT_SENSITIVE)!=0 &&
                sqlite3_stricmp(z0,"global_config")==0 ){
        fossil_errorlog(
          "SECURITY: authorizer blocks DML on protected GLOBAL_CONFIG table\n");
        rc = SQLITE_DENY;
      }else if( (db.protectMask & PROTECT_READONLY)!=0
                && (sqlite3_stricmp(z2, "repository")==0
                    || sqlite3_stricmp(z2,"configdb")==0
                    || sqlite3_stricmp(z2,"localdb")==0) ){
        /* The READONLY constraint only applies to persistent database files.
        ** "temp" and "mem1" and other transient databases are not
        ** constrained by READONLY. */
        fossil_errorlog(
          "SECURITY: authorizer blocks DML on table \"%s\" due to the "
          "request coming from a different origin\n", z0);
        rc = SQLITE_DENY;
      }
      break;
    }
    case SQLITE_DROP_TEMP_TRIGGER: {
      /* Do not allow the triggers that enforce PROTECT_SENSITIVE
      ** to be dropped */
      fossil_errorlog(
        "SECURITY: authorizer blocks attempt to drop a temporary trigger\n");
      rc = SQLITE_DENY;
      break;
    }
  }
  if( db.xAuth && rc==SQLITE_OK ){
    rc = db.xAuth(db.pAuthArg, eCode, z0, z1, z2, z3);
  }
  return rc;
}

/*
** Set or unset the query authorizer callback function
*/
void db_set_authorizer(
  int(*xAuth)(void*,int,const char*,const char*,const char*,const char*),
  void *pArg,
  const char *zName /* for tracing */
){
  if( db.xAuth ){
    fossil_panic("multiple active db_set_authorizer() calls");
  }
  db.xAuth = xAuth;
  db.pAuthArg = pArg;
  db.zAuthName = zName;
  if( g.fSqlTrace ) fossil_trace("-- set authorizer %s\n", zName);
}
void db_clear_authorizer(void){
  if( db.zAuthName && g.fSqlTrace ){
    fossil_trace("-- discontinue authorizer %s\n", db.zAuthName);
  }
  db.xAuth = 0;
  db.pAuthArg = 0;
  db.zAuthName = 0;
}

#if INTERFACE
/*
** Possible flags to db_vprepare
*/
#define DB_PREPARE_IGNORE_ERROR  0x001  /* Suppress errors */
#define DB_PREPARE_PERSISTENT    0x002  /* Stmt will stick around for a while */
#endif

/*
** If zSql is a DML statement, append it db.pDmlLog.
*/
static void db_append_dml(const char *zSql){
  size_t nSql;
  if( db.pDmlLog==0 ) return;
  if( db.pauseDmlLog ) return;
  if( zSql==0 ) return;
  nSql = strlen(zSql);
  while( nSql>0 && fossil_isspace(zSql[0]) ){ nSql--; zSql++; }
  while( nSql>0 && fossil_isspace(zSql[nSql-1]) ) nSql--;
  if( nSql<6 ) return;
  if( fossil_strnicmp(zSql, "SELECT", 6)==0 ) return;
  if( fossil_strnicmp(zSql, "PRAGMA", 6)==0 ) return;
  blob_append(db.pDmlLog, zSql, nSql);
  if( zSql[nSql-1]!=';' ) blob_append_char(db.pDmlLog, ';');
  blob_append_char(db.pDmlLog, '\n');
}

/*
** Set the Blob to which DML statement text should be appended.  Set it
** to zero to stop appending DML statement text.
*/
void db_append_dml_to_blob(Blob *pBlob){
  db.pDmlLog = pBlob;
}

/*
** Pause or unpause the DML log
*/
void db_pause_dml_log(void){    db.pauseDmlLog++; }
void db_unpause_dml_log(void){  db.pauseDmlLog--; }

/*
** Prepare a Stmt.  Assume that the Stmt is previously uninitialized.
** If the input string contains multiple SQL statements, only the first
** one is processed.  All statements beyond the first are silently ignored.
*/
int db_vprepare(Stmt *pStmt, int flags, const char *zFormat, va_list ap){
  int rc;
  int prepFlags = 0;
  char *zSql;
  const char *zExtra = 0;
  blob_zero(&pStmt->sql);
  blob_vappendf(&pStmt->sql, zFormat, ap);
  va_end(ap);
  zSql = blob_str(&pStmt->sql);
  db.nPrepare++;
  db_append_dml(zSql);
  if( flags & DB_PREPARE_PERSISTENT ){
    prepFlags = SQLITE_PREPARE_PERSISTENT;
  }
  rc = sqlite3_prepare_v3(g.db, zSql, -1, prepFlags, &pStmt->pStmt, &zExtra);
  if( rc!=0 && (flags & DB_PREPARE_IGNORE_ERROR)==0 ){
    db_err("%s\n%s", sqlite3_errmsg(g.db), zSql);
  }else if( zExtra && !fossil_all_whitespace(zExtra) ){
    db_err("surplus text follows SQL: \"%s\"", zExtra);
  }
  pStmt->pNext = db.pAllStmt;
  pStmt->pPrev = 0;
  if( db.pAllStmt ) db.pAllStmt->pPrev = pStmt;
  db.pAllStmt = pStmt;
  pStmt->nStep = 0;
  pStmt->rc = rc;
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

/* This variant of db_prepare() checks to see if the statement has
** already been prepared, and if it has it becomes a no-op.
*/
int db_static_prepare(Stmt *pStmt, const char *zFormat, ...){
  int rc = SQLITE_OK;
  if( blob_size(&pStmt->sql)==0 ){
    va_list ap;
    va_start(ap, zFormat);
    rc = db_vprepare(pStmt, DB_PREPARE_PERSISTENT, zFormat, ap);
    va_end(ap);
  }
  return rc;
}

/* Return TRUE if static Stmt object pStmt has been initialized.
*/
int db_static_stmt_is_init(Stmt *pStmt){
  return blob_size(&pStmt->sql)>0;
}

/* Prepare a statement using text placed inside a Blob
** using blob_append_sql().
*/
int db_prepare_blob(Stmt *pStmt, Blob *pSql){
  int rc;
  char *zSql;
  pStmt->sql = *pSql;
  blob_init(pSql, 0, 0);
  zSql = blob_sql_text(&pStmt->sql);
  db.nPrepare++;
  rc = sqlite3_prepare_v3(g.db, zSql, -1, 0, &pStmt->pStmt, 0);
  if( rc!=0 ){
    db_err("%s\n%s", sqlite3_errmsg(g.db), zSql);
  }
  pStmt->pNext = pStmt->pPrev = 0;
  pStmt->nStep = 0;
  pStmt->rc = rc;
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
  if( pStmt->pStmt==0 ) return pStmt->rc;
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
  if( g.fSqlStats ){ db_stats(pStmt); }
  rc = sqlite3_reset(pStmt->pStmt);
  db_check_result(rc, pStmt);
  return rc;
}
int db_finalize(Stmt *pStmt){
  int rc;
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
  if( g.fSqlStats ){ db_stats(pStmt); }
  blob_reset(&pStmt->sql);
  rc = sqlite3_finalize(pStmt->pStmt);
  db_check_result(rc, pStmt);
  pStmt->pStmt = 0;
  return rc;
}

/*
** Return the rowid of the most recent insert
*/
int db_last_insert_rowid(void){
  i64 x = sqlite3_last_insert_rowid(g.db);
  if( x<0 || x>(i64)2147483647 ){
    fossil_panic("rowid out of range (0..2147483647)");
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
  return fossil_strdup_nn(db_column_text(pStmt, N));
}
void db_column_blob(Stmt *pStmt, int N, Blob *pBlob){
  blob_append(pBlob, sqlite3_column_blob(pStmt->pStmt, N),
              sqlite3_column_bytes(pStmt->pStmt, N));
}
Blob db_column_text_as_blob(Stmt *pStmt, int N){
  Blob x;
  blob_init(&x, (char*)sqlite3_column_text(pStmt->pStmt,N),
            sqlite3_column_bytes(pStmt->pStmt,N));
  return x;
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
** Execute a single prepared statement until it finishes.
*/
int db_exec(Stmt *pStmt){
  int rc;
  while( (rc = db_step(pStmt))==SQLITE_ROW ){}
  rc = db_reset(pStmt);
  db_check_result(rc, pStmt);
  return rc;
}

/*
** COMMAND: test-db-exec-error
** Usage: %fossil test-db-exec-error
**
** Invoke the db_exec() interface with an erroneous SQL statement
** in order to verify the error handling logic.
*/
void db_test_db_exec_cmd(void){
  Stmt err;
  db_find_and_open_repository(0,0);
  db_prepare(&err, "INSERT INTO repository.config(name) VALUES(NULL);");
  db_exec(&err);
}

/*
** COMMAND: test-db-prepare
** Usage: %fossil test-db-prepare ?OPTIONS? SQL-STATEMENT
**
** Options:
**   --auth-report   Enable the ticket report query authorizer
**   --auth-ticket   Enable the ticket schema query authorizer
**
** Invoke db_prepare() on the SQL input.  Report any errors encountered.
** This command is used to verify error detection logic in the db_prepare()
** utility routine.
*/
void db_test_db_prepare(void){
  const int fAuthReport = find_option("auth-report",0,0)!=0;
  const int fAuthSchema = find_option("auth-ticket",0,0)!=0;
  char * zReportErr = 0; /* auth-report error string. */
  int nSchemaErr = 0;    /* Number of auth-ticket errors. */
  Stmt err;

  if(fAuthReport + fAuthSchema > 1){
    fossil_fatal("Only one of --auth-report or --auth-ticket "
                 "may be used.");
  }
  db_find_and_open_repository(0,0);
  verify_all_options();
  if( g.argc!=3 ) usage("?OPTIONS? SQL");
  if(fAuthReport){
    report_restrict_sql(&zReportErr);
  }else if(fAuthSchema){
    ticket_restrict_sql(&nSchemaErr);
  }
  db_prepare(&err, "%s", g.argv[2]/*safe-for-%s*/);
  db_finalize(&err);
  if(fAuthReport){
    report_unrestrict_sql();
    if(zReportErr){
      fossil_warning("Report authorizer error: %s\n", zReportErr);
      fossil_free(zReportErr);
    }
  }else if(fAuthSchema){
    ticket_unrestrict_sql();
    if(nSchemaErr){
      fossil_warning("Ticket schema authorizer error count: %d\n",
                     nSchemaErr);
    }
  }
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
** Execute multiple SQL statements.  The input text is executed
** directly without any formatting.
*/
int db_exec_sql(const char *z){
  int rc = SQLITE_OK;
  sqlite3_stmt *pStmt;
  const char *zEnd;
  while( rc==SQLITE_OK && z[0] ){
    pStmt = 0;
    rc = sqlite3_prepare_v2(g.db, z, -1, &pStmt, &zEnd);
    if( rc ){
      db_err("%s: {%s}", sqlite3_errmsg(g.db), z);
    }else if( pStmt ){
      db.nPrepare++;
      db_append_dml(sqlite3_sql(pStmt));
      while( sqlite3_step(pStmt)==SQLITE_ROW ){}
      rc = sqlite3_finalize(pStmt);
      if( rc ) db_err("%s: {%.*s}", sqlite3_errmsg(g.db), (int)(zEnd-z), z);
    }
    z = zEnd;
  }
  return rc;
}

/*
** Execute multiple SQL statements using printf-style formatting.
*/
int db_multi_exec(const char *zSql, ...){
  Blob sql;
  int rc;
  va_list ap;

  blob_init(&sql, 0, 0);
  va_start(ap, zSql);
  blob_vappendf(&sql, zSql, ap);
  va_end(ap);
  rc = db_exec_sql(blob_str(&sql));
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
    z = fossil_strdup_nn((const char*)sqlite3_column_text(s.pStmt, 0));
  }else{
    z = fossil_strdup(zDefault);
  }
  db_finalize(&s);
  return z;
}

/*
** Initialize a new database file with the given schema.  If anything
** goes wrong, call db_err() to exit.
**
** If zFilename is NULL, then create an empty repository in an in-memory
** database.
*/
void db_init_database(
  const char *zFileName,   /* Name of database file to create */
  const char *zSchema,     /* First part of schema */
  ...                      /* Additional SQL to run.  Terminate with NULL. */
){
  sqlite3 *xdb;
  int rc;
  const char *zSql;
  va_list ap;

  xdb = db_open(zFileName ? zFileName : ":memory:");
  sqlite3_exec(xdb, "BEGIN EXCLUSIVE", 0, 0, 0);
  rc = sqlite3_exec(xdb, zSchema, 0, 0, 0);
  if( rc!=SQLITE_OK ){
    db_err("%s", sqlite3_errmsg(xdb));
  }
  va_start(ap, zSchema);
  while( (zSql = va_arg(ap, const char*))!=0 ){
    rc = sqlite3_exec(xdb, zSql, 0, 0, 0);
    if( rc!=SQLITE_OK ){
      db_err("%s", sqlite3_errmsg(xdb));
    }
  }
  va_end(ap);
  sqlite3_exec(xdb, "COMMIT", 0, 0, 0);
  if( zFileName || g.db!=0 ){
    sqlite3_close(xdb);
  }else{
    g.db = xdb;
  }
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
** SETTING: timeline-utc      boolean default=on
**
** If the timeline-utc setting is true, then Fossil tries to understand
** and display all time values using UTC.  If this setting is false, Fossil
** tries to understand and display time values using the local timezone.
**
** The word "timeline" in the name of this setting is historical.
** This setting applies to all user interfaces of Fossil,
** not just the timeline.
**
** Note that when accessing Fossil using the web interface, the localtime
** used is the localtime on the server, not on the client.
*/
/*
** Return true if Fossil is set to display times as UTC.  Return false
** if it wants to display times using the local timezone.
**
** False is returned if display is set to localtime even if the localtime
** happens to be the same as UTC.
*/
int fossil_ui_utctime(void){
  if( g.fTimeFormat==0 ){
    if( db_get_int("timeline-utc", 1) ){
      g.fTimeFormat = 1; /* UTC */
    }else{
      g.fTimeFormat = 2; /* Localtime */
    }
  }
  return g.fTimeFormat==1;
}

/*
** Return true if Fossil is set to display times using the local timezone.
*/
int fossil_ui_localtime(void){
  return fossil_ui_utctime()==0;
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
  if( fossil_ui_utctime() ){
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
  if( fossil_ui_utctime() ){
    sqlite3_result_text(context, "0 seconds", -1, SQLITE_STATIC);
  }else{
    sqlite3_result_text(context, "utc", -1, SQLITE_STATIC);
  }
}

/*
** If the input is a hexadecimal string, convert that string into a BLOB.
** If the input is not a hexadecimal string, return NULL.
*/
void db_hextoblob(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *zIn = sqlite3_value_text(argv[0]);
  int nIn = sqlite3_value_bytes(argv[0]);
  unsigned char *zOut;
  if( zIn==0 ) return;
  if( nIn&1 ) return;
  if( !validate16((const char*)zIn, nIn) ) return;
  zOut = sqlite3_malloc64( nIn/2 + 1 );
  if( zOut==0 ){
    sqlite3_result_error_nomem(context);
    return;
  }
  decode16(zIn, zOut, nIn);
  sqlite3_result_blob(context, zOut, nIn/2, sqlite3_free);
}

/*
** Return the XOR-obscured version of the input text.  Useful for
** updating authentication strings in Fossil settings.  To change
** the password locally stored for sync, for instance:
**
**    echo "UPDATE config
**        SET value = obscure('monkey123')
**        WHERE name = 'last-sync-pw'" |
**      fossil sql
**
** Note that user.pw uses a different obscuration algorithm, but
** you don't need to use 'fossil sql' for that anyway.  Just call
**
**    fossil user pass monkey123
**
** to change the local user entry's password in the same way.
**
** 2022-12-30:  If the user-data pointer is not NULL, then operate
** as unobscure() rather than obscure().  The obscure() variant of
** this routine is commonly available.  But unobscure is (currently)
** only registered by the "fossil remote config-data --show-passwords"
** command.
*/
void db_obscure(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *zIn = sqlite3_value_text(argv[0]);
  int nIn = sqlite3_value_bytes(argv[0]);
  char *zOut, *zTemp;
  if( 0==zIn ) return;
  if( 0==(zOut = sqlite3_malloc64( nIn * 2 + 3 )) ){
    sqlite3_result_error_nomem(context);
    return;
  }
  if( sqlite3_user_data(context)==0 ){
    zTemp = obscure((char*)zIn);
  }else{
    zTemp = unobscure((char*)zIn);
  }
  fossil_strcpy(zOut, zTemp);
  fossil_free(zTemp);
  sqlite3_result_text(context, zOut, strlen(zOut), sqlite3_free);
}

/*
** Return True if zName is a protected (a.k.a. "sensitive") setting.
*/
int db_setting_is_protected(const char *zName){
  const Setting *pSetting = zName ? db_find_setting(zName,0) : 0;
  return pSetting!=0 && pSetting->sensitive!=0;
}

/*
** Implement the protected_setting(X) SQL function.  This function returns
** true if X is the name of a protected (security-sensitive) setting and
** the db.protectSensitive flag is enabled.  It returns false otherwise.
*/
LOCAL void db_protected_setting_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zSetting;
  if( (db.protectMask & PROTECT_SENSITIVE)==0 ){
    sqlite3_result_int(context, 0);
    return;
  }
  zSetting = (const char*)sqlite3_value_text(argv[0]);
  sqlite3_result_int(context, db_setting_is_protected(zSetting));
}

/*
** Copied from SQLite ext/misc/uint.c...
**
** Compare text in lexicographic order, except strings of digits
** compare in numeric order.
**
** This version modified to also ignore case.
*/
static int uintNocaseCollFunc(
  void *notUsed,
  int nKey1, const void *pKey1,
  int nKey2, const void *pKey2
){
  const unsigned char *zA = (const unsigned char*)pKey1;
  const unsigned char *zB = (const unsigned char*)pKey2;
  int i=0, j=0, x;
  (void)notUsed;
  while( i<nKey1 && j<nKey2 ){
    if( fossil_isdigit(zA[i]) && fossil_isdigit(zB[j]) ){
      int k;
      while( i<nKey1 && zA[i]=='0' ){ i++; }
      while( j<nKey2 && zB[j]=='0' ){ j++; }
      k = 0;
      while( i+k<nKey1 && fossil_isdigit(zA[i+k])
          && j+k<nKey2 && fossil_isdigit(zB[j+k]) ){
        k++;
      }
      if( i+k<nKey1 && fossil_isdigit(zA[i+k]) ){
        return +1;
      }else if( j+k<nKey2 && fossil_isdigit(zB[j+k]) ){
        return -1;
      }else{
        x = memcmp(zA+i, zB+j, k);
        if( x ) return x;
        i += k;
        j += k;
      }
    }else
    if( zA[i]!=zB[j]
     && (x = fossil_tolower(zA[i]) - fossil_tolower(zB[j]))!=0
    ){
      return x;
    }else{
      i++;
      j++;
    }
  }
  return (nKey1 - i) - (nKey2 - j);
}


/*
** Register the SQL functions that are useful both to the internal
** representation and to the "fossil sql" command.
*/
void db_add_aux_functions(sqlite3 *db){
  sqlite3_create_collation(db, "uintnocase", SQLITE_UTF8,0,uintNocaseCollFunc);
  sqlite3_create_function(db, "checkin_mtime", 2,
                          SQLITE_UTF8|SQLITE_DETERMINISTIC|SQLITE_INNOCUOUS,
                          0, db_checkin_mtime_function, 0, 0);
  sqlite3_create_function(db, "symbolic_name_to_rid", 1,
                          SQLITE_UTF8|SQLITE_DETERMINISTIC|SQLITE_INNOCUOUS,
                          0, db_sym2rid_function, 0, 0);
  sqlite3_create_function(db, "symbolic_name_to_rid", 2,
                          SQLITE_UTF8|SQLITE_DETERMINISTIC|SQLITE_INNOCUOUS,
                          0, db_sym2rid_function, 0, 0);
  sqlite3_create_function(db, "now", 0,
                          SQLITE_UTF8|SQLITE_INNOCUOUS, 0,
                          db_now_function, 0, 0);
  sqlite3_create_function(db, "toLocal", 0,
                          SQLITE_UTF8|SQLITE_DETERMINISTIC|SQLITE_INNOCUOUS,
                          0, db_tolocal_function, 0, 0);
  sqlite3_create_function(db, "fromLocal", 0,
                          SQLITE_UTF8|SQLITE_DETERMINISTIC|SQLITE_INNOCUOUS,
                          0, db_fromlocal_function, 0, 0);
  sqlite3_create_function(db, "hextoblob", 1,
                          SQLITE_UTF8|SQLITE_DETERMINISTIC|SQLITE_INNOCUOUS,
                          0, db_hextoblob, 0, 0);
  sqlite3_create_function(db, "capunion", 1, SQLITE_UTF8, 0,
                          0, capability_union_step, capability_union_finalize);
  sqlite3_create_function(db, "fullcap", 1, SQLITE_UTF8, 0,
                          capability_fullcap, 0, 0);
  sqlite3_create_function(db, "find_emailaddr", 1, SQLITE_UTF8, 0,
                          alert_find_emailaddr_func, 0, 0);
  sqlite3_create_function(db, "display_name", 1, SQLITE_UTF8, 0,
                          alert_display_name_func, 0, 0);
  sqlite3_create_function(db, "obscure", 1, SQLITE_UTF8, 0,
                          db_obscure, 0, 0);
  sqlite3_create_function(db, "protected_setting", 1, SQLITE_UTF8, 0,
                          db_protected_setting_func, 0, 0);
  sqlite3_create_function(db, "win_reserved", 1, SQLITE_UTF8, 0,
                          db_win_reserved_func,0,0);
  sqlite3_create_function(db, "url_nouser", 1, SQLITE_UTF8, 0,
                          url_nouser_func,0,0);
  sqlite3_create_function(db, "chat_msg_from_event", 4,
        SQLITE_UTF8 | SQLITE_INNOCUOUS, 0,
        chat_msg_from_event, 0, 0);
  sqlite3_create_function(db, "inode", 1, SQLITE_UTF8, 0,
                          file_inode_sql_func,0,0);
  sqlite3_create_function(db, "artifact_to_json", 1, SQLITE_UTF8, 0,
                          artifact_to_json_sql_func,0,0);

}

#if USE_SEE
/*
** This is a pointer to the saved database encryption key string.
*/
static char *zSavedKey = 0;

/*
** This is the size of the saved database encryption key, in bytes.
*/
static size_t savedKeySize = 0;

/*
** This function returns non-zero if there is a saved database encryption
** key available.
*/
int db_have_saved_encryption_key(){
  return db_is_valid_saved_encryption_key(zSavedKey, savedKeySize);
}

/*
** This function returns non-zero if the specified database encryption key
** is valid.
*/
int db_is_valid_saved_encryption_key(const char *p, size_t n){
  if( p==0 ) return 0;
  if( n==0 ) return 0;
  if( p[0]==0 ) return 0;
  return 1;
}

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
** This function arranges for the saved database encryption key buffer
** to be allocated and then sets up the environment variable to allow
** a child process to initialize it with the actual database encryption
** key.
*/
void db_setup_for_saved_encryption_key(){
  void *p = NULL;
  size_t n = 0;
  size_t pageSize = 0;
  Blob pidKey;

  assert( !db_have_saved_encryption_key() );
  db_unsave_encryption_key();
  fossil_get_page_size(&pageSize);
  assert( pageSize>0 );
  p = fossil_secure_alloc_page(&n);
  assert( p!=NULL );
  assert( n==pageSize );
  blob_zero(&pidKey);
  blob_appendf(&pidKey, "%lu:%p:%u", (unsigned long)GETPID(), p, n);
  fossil_setenv("FOSSIL_SEE_PID_KEY", blob_str(&pidKey));
  zSavedKey = p;
  savedKeySize = n;
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

  assert( !db_have_saved_encryption_key() );
  blobSize = blob_size(pKey);
  if( blobSize==0 ) return;
  fossil_get_page_size(&pageSize);
  assert( pageSize>0 );
  if( blobSize>pageSize ){
    fossil_panic("key blob too large: %u versus %u", blobSize, pageSize);
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
static void db_set_saved_encryption_key(
  Blob *pKey
){
  if( zSavedKey!=NULL ){
    size_t blobSize = blob_size(pKey);
    if( blobSize==0 ){
      db_unsave_encryption_key();
    }else{
      if( blobSize>savedKeySize ){
        fossil_panic("key blob too large: %u versus %u",
                     blobSize, savedKeySize);
      }
      fossil_secure_zero(zSavedKey, savedKeySize);
      memcpy(zSavedKey, blob_str(pKey), blobSize);
    }
  }else{
    db_save_encryption_key(pKey);
  }
}

/*
** WEBPAGE: setseekey
**
** Sets the sets the saved database encryption key to one that gets passed
** via the "key" query string parameter.  If the saved database encryption
** key has already been set, does nothing.  This web page does not produce
** any output on success or failure.  No permissions are required and none
** are checked (partially due to lack of encrypted database access).
**
** Query parameters:
**
**   key                 The string to set as the saved database encryption
**                       key.
*/
void db_set_see_key_page(void){
  Blob key;
  const char *zKey;
  if( db_have_saved_encryption_key() ){
    fossil_trace("SEE: encryption key was already set\n");
    return;
  }
  zKey = P("key");
  blob_init(&key, 0, 0);
  if( zKey!=0 ){
    PID_T processId;
    blob_set(&key, zKey);
    db_set_saved_encryption_key(&key);
    processId = db_maybe_handle_saved_encryption_key_for_process(
      SEE_KEY_WRITE
    );
    fossil_trace("SEE: set encryption key for process %lu, length %u\n",
                 (unsigned long)processId, blob_size(&key));
  }else{
    fossil_trace("SEE: no encryption key specified\n");
  }
  blob_reset(&key);
}

/*
** WEBPAGE: unsetseekey
**
** Sets the saved database encryption key to zeros in the current and parent
** Fossil processes.  This web page does not produce any output on success
** or failure.  Setup permission is required.
*/
void db_unset_see_key_page(void){
  PID_T processId;
  login_check_credentials();
  if( !g.perm.Setup ){ login_needed(0); return; }
  processId = db_maybe_handle_saved_encryption_key_for_process(
    SEE_KEY_ZERO
  );
  fossil_trace("SEE: unset encryption key for process %lu\n",
               (unsigned long)processId);
}

/*
** This function reads the saved database encryption key from the
** specified Fossil parent process.  This is only necessary (or
** functional) on Windows or Linux.
*/
static void db_read_saved_encryption_key_from_process(
  PID_T processId, /* Identifier for Fossil parent process. */
  LPVOID pAddress, /* Pointer to saved key buffer in the parent process. */
  SIZE_T nSize     /* Size of saved key buffer in the parent process. */
){
  void *p = NULL;
  size_t n = 0;
  size_t pageSize = 0;

  fossil_get_page_size(&pageSize);
  assert( pageSize>0 );
  if( nSize>pageSize ){
    fossil_panic("key too large: %u versus %u", nSize, pageSize);
  }
  p = fossil_secure_alloc_page(&n);
  assert( p!=NULL );
  assert( n==pageSize );
  assert( n>=nSize );
  {
#if defined(_WIN32)
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, processId);
    if( hProcess!=NULL ){
      SIZE_T nRead = 0;
      if( ReadProcessMemory(hProcess, pAddress, p, nSize, &nRead) ){
        CloseHandle(hProcess);
        if( nRead==nSize ){
          db_unsave_encryption_key();
          zSavedKey = p;
          savedKeySize = n;
        }else{
          fossil_secure_free_page(p, n);
          fossil_panic("bad size read, %u out of %u bytes at %p from pid %lu",
                       nRead, nSize, pAddress, processId);
        }
      }else{
        CloseHandle(hProcess);
        fossil_secure_free_page(p, n);
        fossil_panic("failed read, %u bytes at %p from pid %lu: %lu", nSize,
                     pAddress, processId, GetLastError());
      }
    }else{
      fossil_secure_free_page(p, n);
      fossil_panic("failed to open pid %lu: %lu", processId, GetLastError());
    }
#elif defined(__linux__)
    ssize_t nRead;
    struct iovec liov = {0};
    struct iovec riov = {0};
    liov.iov_base = p;
    liov.iov_len = n;
    riov.iov_base = pAddress;
    riov.iov_len = nSize;
    nRead = process_vm_readv(processId, &liov, 1, &riov, 1, 0);
    if( nRead==nSize ){
      db_unsave_encryption_key();
      zSavedKey = p;
      savedKeySize = n;
    }else{
      fossil_secure_free_page(p, n);
      fossil_panic("bad size read, %zd out of %zu bytes at %p from pid %lu",
                   nRead, nSize, pAddress, (unsigned long)processId);
    }
#else
    fossil_secure_free_page(p, n);
    fossil_trace("db_read_saved_encryption_key_from_process unsupported");
#endif
  }
}

/*
** This function writes the saved database encryption key into the
** specified Fossil parent process.  This is only necessary (or
** functional) on Windows or Linux.
*/
static void db_write_saved_encryption_key_to_process(
  PID_T processId, /* Identifier for Fossil parent process. */
  LPVOID pAddress, /* Pointer to saved key buffer in the parent process. */
  SIZE_T nSize     /* Size of saved key buffer in the parent process. */
){
  void *p = db_get_saved_encryption_key();
  size_t n = db_get_saved_encryption_key_size();
  size_t pageSize = 0;

  fossil_get_page_size(&pageSize);
  assert( pageSize>0 );
  if( nSize>pageSize ){
    fossil_panic("key too large: %u versus %u", nSize, pageSize);
  }
  assert( p!=NULL );
  assert( n==pageSize );
  assert( n>=nSize );
  {
#if defined(_WIN32)
    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION|PROCESS_VM_WRITE,
                                  FALSE, processId);
    if( hProcess!=NULL ){
      SIZE_T nWrite = 0;
      if( WriteProcessMemory(hProcess, pAddress, p, nSize, &nWrite) ){
        CloseHandle(hProcess);
        if( nWrite!=nSize ){
          fossil_panic("bad size write, %u out of %u bytes at %p from pid %lu",
                       nWrite, nSize, pAddress, processId);
        }
      }else{
        CloseHandle(hProcess);
        fossil_panic("failed write, %u bytes at %p from pid %lu: %lu", nSize,
                     pAddress, processId, GetLastError());
      }
    }else{
      fossil_panic("failed to open pid %lu: %lu", processId, GetLastError());
    }
#elif defined(__linux__)
    ssize_t nWrite;
    struct iovec liov = {0};
    struct iovec riov = {0};
    liov.iov_base = p;
    liov.iov_len = n;
    riov.iov_base = pAddress;
    riov.iov_len = nSize;
    nWrite = process_vm_writev(processId, &liov, 1, &riov, 1, 0);
    if( nWrite!=nSize ){
      fossil_panic("bad size write, %zd out of %zu bytes at %p from pid %lu",
                   nWrite, nSize, pAddress, (unsigned long)processId);
    }
#else
    fossil_trace("db_write_saved_encryption_key_to_process unsupported");
#endif
  }
}

/*
** This function zeros the saved database encryption key in the specified
** Fossil parent process.  This is only necessary (or functional) on
** Windows or Linux.
*/
static void db_zero_saved_encryption_key_in_process(
  PID_T processId, /* Identifier for Fossil parent process. */
  LPVOID pAddress, /* Pointer to saved key buffer in the parent process. */
  SIZE_T nSize     /* Size of saved key buffer in the parent process. */
){
  void *p = NULL;
  size_t n = 0;
  size_t pageSize = 0;

  fossil_get_page_size(&pageSize);
  assert( pageSize>0 );
  if( nSize>pageSize ){
    fossil_panic("key too large: %u versus %u", nSize, pageSize);
  }
  p = fossil_secure_alloc_page(&n);
  assert( p!=NULL );
  assert( n==pageSize );
  assert( n>=nSize );
  {
#if defined(_WIN32)
    HANDLE hProcess = OpenProcess(PROCESS_VM_OPERATION|PROCESS_VM_WRITE,
                                  FALSE, processId);
    if( hProcess!=NULL ){
      SIZE_T nWrite = 0;
      if( WriteProcessMemory(hProcess, pAddress, p, nSize, &nWrite) ){
        CloseHandle(hProcess);
        fossil_secure_free_page(p, n);
        if( nWrite!=nSize ){
          fossil_panic("bad size zero, %u out of %u bytes at %p from pid %lu",
                       nWrite, nSize, pAddress, processId);
        }
      }else{
        CloseHandle(hProcess);
        fossil_secure_free_page(p, n);
        fossil_panic("failed zero, %u bytes at %p from pid %lu: %lu", nSize,
                     pAddress, processId, GetLastError());
      }
    }else{
      fossil_secure_free_page(p, n);
      fossil_panic("failed to open pid %lu: %lu", processId, GetLastError());
    }
#elif defined(__linux__)
    ssize_t nWrite;
    struct iovec liov = {0};
    struct iovec riov = {0};
    liov.iov_base = p;
    liov.iov_len = n;
    riov.iov_base = pAddress;
    riov.iov_len = nSize;
    nWrite = process_vm_writev(processId, &liov, 1, &riov, 1, 0);
    if( nWrite!=nSize ){
      fossil_secure_free_page(p, n);
      fossil_panic("bad size zero, %zd out of %zu bytes at %p from pid %lu",
                   nWrite, nSize, pAddress, (unsigned long)processId);
    }
#else
    fossil_secure_free_page(p, n);
    fossil_trace("db_zero_saved_encryption_key_in_process unsupported");
#endif
  }
}

/*
** This function evaluates the specified TH1 script and attempts to parse
** its result as a colon-delimited triplet containing a process identifier,
** address, and size (in bytes) of the database encryption key.  This is
** only necessary (or functional) on Windows or Linux.
*/
static PID_T db_handle_saved_encryption_key_for_process_via_th1(
  const char *zConfig, /* The TH1 script to evaluate. */
  int eType            /* Non-zero to write key to parent process -OR-
                        * zero to read it from the parent process. */
){
  PID_T processId = 0;
  int rc;
  char *zResult;
  char *zPwd = file_getcwd(0, 0);
  Th_FossilInit(TH_INIT_DEFAULT | TH_INIT_NEED_CONFIG | TH_INIT_NO_REPO);
  rc = Th_Eval(g.interp, 0, zConfig, -1);
  zResult = (char*)Th_GetResult(g.interp, 0);
  if( rc!=TH_OK ){
    fossil_fatal("script for pid key failed: %s", zResult);
  }
  if( zResult ){
    LPVOID pAddress = NULL;
    SIZE_T nSize = 0;
    parse_pid_key_value(zResult, &processId, &pAddress, &nSize);
    if( eType==SEE_KEY_READ ){
      db_read_saved_encryption_key_from_process(processId, pAddress, nSize);
    }else if( eType==SEE_KEY_WRITE ){
      db_write_saved_encryption_key_to_process(processId, pAddress, nSize);
    }else if( eType==SEE_KEY_ZERO ){
      db_zero_saved_encryption_key_in_process(processId, pAddress, nSize);
    }else{
      fossil_panic("unsupported SEE key operation %d", eType);
    }
  }
  file_chdir(zPwd, 0);
  fossil_free(zPwd);
  return processId;
}

/*
** This function sets the saved database encryption key to one that gets
** read from the specified Fossil parent process, if applicable.  This is
** only necessary (or functional) on Windows or Linux.
*/
PID_T db_maybe_handle_saved_encryption_key_for_process(int eType){
  PID_T processId = 0;
  g.zPidKey = find_option("usepidkey",0,1);
  if( !g.zPidKey ){
    g.zPidKey = fossil_getenv("FOSSIL_SEE_PID_KEY");
  }
  if( g.zPidKey ){
    LPVOID pAddress = NULL;
    SIZE_T nSize = 0;
    parse_pid_key_value(g.zPidKey, &processId, &pAddress, &nSize);
    if( eType==SEE_KEY_READ ){
      db_read_saved_encryption_key_from_process(processId, pAddress, nSize);
    }else if( eType==SEE_KEY_WRITE ){
      db_write_saved_encryption_key_to_process(processId, pAddress, nSize);
    }else if( eType==SEE_KEY_ZERO ){
      db_zero_saved_encryption_key_in_process(processId, pAddress, nSize);
    }else{
      fossil_panic("unsupported SEE key operation %d", eType);
    }
  }else{
    const char *zSeeDbConfig = find_option("seedbcfg",0,1);
    if( !zSeeDbConfig ){
      zSeeDbConfig = fossil_getenv("FOSSIL_SEE_DB_CONFIG");
    }
    if( zSeeDbConfig ){
      processId = db_handle_saved_encryption_key_for_process_via_th1(
        zSeeDbConfig, eType
      );
    }
  }
  return processId;
}
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
  Blob bNameCheck = BLOB_INITIALIZER;

  if( g.fSqlTrace ) fossil_trace("-- sqlite3_open: [%s]\n", zDbName);
  file_canonical_name(zDbName, &bNameCheck, 0)
    /* For purposes of the apndvfs check, g.nameOfExe and zDbName must
    ** both be canonicalized, else chances are very good that they
    ** will not match even if they're the same file. Details:
    ** https://fossil-scm.org/forum/forumpost/16880a28aad1a868 */;
  if( strcmp(blob_str(&bNameCheck), g.nameOfExe)==0 ){
    extern int sqlite3_appendvfs_init(
      sqlite3 *, char **, const sqlite3_api_routines *
    );
    sqlite3_appendvfs_init(0,0,0);
    g.zVfsName = "apndvfs";
  }
  blob_reset(&bNameCheck);
  rc = sqlite3_open_v2(
       zDbName, &db,
       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
       g.zVfsName
  );
  if( rc!=SQLITE_OK ){
    db_err("[%s]: %s", zDbName, sqlite3_errmsg(db));
  }
  db_maybe_set_encryption_key(db, zDbName);
  sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_FKEY, 0, &rc);
  sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_TRIGGER, 0, &rc);
  sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &rc);
  sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DDL, 0, &rc);
  sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DML, 0, &rc);
  sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, &rc);
  sqlite3_busy_timeout(db, 15000);
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
  if( g.fSqlTrace ) sqlite3_trace_v2(db, SQLITE_TRACE_PROFILE, db_sql_trace, 0);
  db_add_aux_functions(db);
  re_add_sql_func(db);  /* The REGEXP operator */
  foci_register(db);    /* The "files_of_checkin" virtual table */
  sqlite3_set_authorizer(db, db_top_authorizer, db);
  db_register_fts5(db) /* in search.c */;
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
  if( db_table_exists(zLabel,"sqlite_schema") ) return;
  blob_init(&key, 0, 0);
  db_maybe_obtain_encryption_key(zDbName, &key);
  if( fossil_getenv("FOSSIL_USE_SEE_TEXTKEY")==0 ){
    char *zCmd = sqlite3_mprintf("ATTACH DATABASE %Q AS %Q KEY %Q",
                                 zDbName, zLabel, blob_str(&key));
    db_exec_sql(zCmd);
    fossil_secure_zero(zCmd, strlen(zCmd));
    sqlite3_free(zCmd);
  }else{
    char *zCmd = sqlite3_mprintf("ATTACH DATABASE %Q AS %Q KEY ''",
                                 zDbName, zLabel);
    db_exec_sql(zCmd);
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
    fossil_panic("Fossil requires a version of SQLite that supports the "
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
  if( rc==SQLITE_OK ){
    while( db_step(&q)==SQLITE_ROW ){
      if( fossil_strcmp(db_column_text(&q,1),zLabel)==0 ){
        iSlot = db_column_int(&q, 0);
        break;
      }
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
** Close the per-user configuration database file
*/
void db_close_config(){
  int iSlot = db_database_slot("configdb");
  if( iSlot>0 ){
    db_detach("configdb");
  }else if( g.dbConfig ){
    sqlite3_wal_checkpoint(g.dbConfig, 0);
    sqlite3_close(g.dbConfig);
    g.dbConfig = 0;
  }else if( g.db && 0==iSlot ){
    int rc;
    sqlite3_wal_checkpoint(g.db, 0);
    rc = sqlite3_close(g.db);
    if( g.fSqlTrace ) fossil_trace("-- db_close_config(%d)\n", rc);
    g.db = 0;
    g.repositoryOpen = 0;
    g.localOpen = 0;
  }else{
    return;
  }
  fossil_free(g.zConfigDbName);
  g.zConfigDbName = 0;
}

/*
** Compute the name of the configuration database.  If unable to find the
** database, return 0 if isOptional is true, or panic if isOptional is false.
**
** Space to hold the result comes from fossil_malloc().
*/
static char *db_configdb_name(int isOptional){
  char *zHome;        /* Home directory */
  char *zDbName;      /* Name of the database file */


  /* On Windows, look for these directories, in order:
  **
  **    FOSSIL_HOME
  **    LOCALAPPDATA
  **    APPDATA
  **    USERPROFILE
  **    HOMEDRIVE HOMEPATH
  */
#if defined(_WIN32) || defined(__CYGWIN__)
  zHome = fossil_getenv("FOSSIL_HOME");
  if( zHome==0 ){
    zHome = fossil_getenv("LOCALAPPDATA");
    if( zHome==0 ){
      zHome = fossil_getenv("APPDATA");
      if( zHome==0 ){
        zHome = fossil_getenv("USERPROFILE");
        if( zHome==0 ){
          char *zDrive = fossil_getenv("HOMEDRIVE");
          char *zPath = fossil_getenv("HOMEPATH");
          if( zDrive && zPath ) zHome = mprintf("%s%s", zDrive, zPath);
        }
      }
    }
  }
  zDbName = mprintf("%//_fossil", zHome);
  fossil_free(zHome);
  return zDbName;

#else /* if unix */
  char *zXdgHome;

  /* For unix. a 5-step algorithm is used.
  ** See ../www/tech_overview.wiki for discussion.
  **
  ** Step 1:  If FOSSIL_HOME exists -> $FOSSIL_HOME/.fossil
  */
  zHome = fossil_getenv("FOSSIL_HOME");
  if( zHome!=0 ) return mprintf("%s/.fossil", zHome);

  /* Step 2:  If HOME exists and file $HOME/.fossil exists -> $HOME/.fossil
  */
  zHome = fossil_getenv("HOME");
  if( zHome ){
    zDbName = mprintf("%s/.fossil", zHome);
    if( file_size(zDbName, ExtFILE)>1024*3 ){
      return zDbName;
    }
    fossil_free(zDbName);
  }

  /* Step 3: if XDG_CONFIG_HOME exists -> $XDG_CONFIG_HOME/fossil.db
  */
  zXdgHome = fossil_getenv("XDG_CONFIG_HOME");
  if( zXdgHome!=0 ){
    return mprintf("%s/fossil.db", zXdgHome);
  }

  /* The HOME variable is required in order to continue.
  */
  if( zHome==0 ){
    if( isOptional ) return 0;
    fossil_fatal("cannot locate home directory - please set one of the "
                 "FOSSIL_HOME, XDG_CONFIG_HOME, or HOME environment "
                 "variables");
  }

  /* Step 4: If $HOME/.config is a directory -> $HOME/.config/fossil.db
  */
  zXdgHome = mprintf("%s/.config", zHome);
  if( file_isdir(zXdgHome, ExtFILE)==1 ){
    fossil_free(zXdgHome);
    return mprintf("%s/.config/fossil.db", zHome);
  }

  /* Step 5: Otherwise -> $HOME/.fossil
  */
  return mprintf("%s/.fossil", zHome);
#endif /* unix */
}

/*
** Open the configuration database.  Create the database anew if
** it does not already exist.
**
** If the useAttach flag is 0 (the usual case) then the configuration
** database is opened on a separate database connection g.dbConfig.
** This prevents the database from becoming locked on long check-in or sync
** operations which hold an exclusive transaction.  In a few cases, though,
** it is convenient for the database to be attached to the main database
** connection so that we can join between the various databases.  In that
** case, invoke this routine with useAttach as 1.
*/
int db_open_config(int useAttach, int isOptional){
  char *zDbName;
  if( g.zConfigDbName ){
    int alreadyAttached = db_database_slot("configdb")>0;
    if( useAttach==alreadyAttached ) return 1; /* Already open. */
    db_close_config();
  }
  zDbName = db_configdb_name(isOptional);
  if( zDbName==0 ) return 0;
  if( file_size(zDbName, ExtFILE)<1024*3 ){
    char *zHome = file_dirname(zDbName);
    int rc;
    if( file_isdir(zHome, ExtFILE)==0 ){
      file_mkdir(zHome, ExtFILE, 0);
    }
    rc = file_access(zHome, W_OK);
    if( rc ){
      if( isOptional ) return 0;
      fossil_fatal("home directory \"%s\" must be writeable", zHome);
    }
    db_init_database(zDbName, zConfigSchema, (char*)0);
    fossil_free(zHome);
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

  if( file_access(zDbName, F_OK) ) return 0;
  lsize = file_size(zDbName, ExtFILE);
  if( lsize%1024!=0 || lsize<4096 ) return 0;
  db_open_or_attach(zDbName, "localdb");

  /* Check to see if the check-out database has the lastest schema changes.
  ** The most recent schema change (2019-01-19) is the addition of the
  ** vmerge.mhash and vfile.mhash fields.  If the schema has the vmerge.mhash
  ** column, assume everything else is up-to-date.
  */
  if( db_table_has_column("localdb","vmerge","mhash") ){
    return 1;   /* This is a check-out database with the latest schema */
  }

  /* If there is no vfile table, then assume we have picked up something
  ** that is not even close to being a valid check-out database */
  if( !db_table_exists("localdb","vfile") ){
    return 0;  /* Not a  DB */
  }

  /* If the "isexe" column is missing from the vfile table, then
  ** add it now.   This code added on 2010-03-06.  After all users have
  ** upgraded, this code can be safely deleted.
  */
  if( !db_table_has_column("localdb","vfile","isexe") ){
    db_multi_exec("ALTER TABLE vfile ADD COLUMN isexe BOOLEAN DEFAULT 0");
  }

  /* If "islink"/"isLink" columns are missing from tables, then
  ** add them now.   This code added on 2011-01-17 and 2011-08-27.
  ** After all users have upgraded, this code can be safely deleted.
  */
  if( !db_table_has_column("localdb","vfile","isLink") ){
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

  /* The design of the check-out database changed on 2019-01-19 adding the mhash
  ** column to vfile and vmerge and changing the UNIQUE index on vmerge into
  ** a PRIMARY KEY that includes the new mhash column.  However, we must have
  ** the repository database at hand in order to do the migration, so that
  ** step is deferred. */
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
** In db_open_local_v2(), if the bRootOnly flag is true, then only
** look in the CWD for the check-out database.  Do not scan upwards in
** the file hierarchy.
**
** This routine always opens the user database regardless of whether or
** not the repository database is found.  If the _FOSSIL_ or .fslckout file
** is found, it is attached to the open database connection too.
*/
int db_open_local_v2(const char *zDbName, int bRootOnly){
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
        /* Found a valid check-out database file */
        g.zLocalDbName = fossil_strdup(zPwd);
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
    if( bRootOnly ) break;
    n--;
    while( n>1 && zPwd[n]!='/' ){ n--; }
    while( n>1 && zPwd[n-1]=='/' ){ n--; }
    zPwd[n] = 0;
  }

  /* A check-out database file could not be found */
  return 0;
}
int db_open_local(const char *zDbName){
  return db_open_local_v2(zDbName, 0);
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
      char * zFree = zRepo;
      zRepo = mprintf("%s%s", g.zLocalRoot, zRepo);
      fossil_free(zFree);
      zFree = zRepo;
      zRepo = file_canonical_name_dup(zFree);
      fossil_free(zFree);
    }
  }
  return zRepo;
}

/*
** Returns non-zero if support for symlinks is currently enabled.
*/
int db_allow_symlinks(void){
  return g.allowSymlinks;
}

/*
** Return TRUE if the file in the argument seems like it might be an
** SQLite database file that contains a Fossil repository schema.
*/
int db_looks_like_a_repository(const char *zDbName){
  sqlite3 *db = 0;
  i64 sz;
  int rc;
  int res = 0;
  sqlite3_stmt *pStmt = 0;

  sz = file_size(zDbName, ExtFILE);
  if( sz<16834 ) return 0;
  db = db_open(zDbName);
  if( !db ) return 0;
  if( !g.zVfsName && sz%512 ) return 0;
  rc = sqlite3_prepare_v2(db,
       "SELECT count(*) FROM sqlite_schema"
       " WHERE name COLLATE nocase IN"
       "('blob','delta','rcvfrom','user','config','mlink','plink');",
       -1, &pStmt, 0);
  if( rc ) goto is_repo_end;
  rc = sqlite3_step(pStmt);
  if( rc!=SQLITE_ROW ) goto is_repo_end;
  if( sqlite3_column_int(pStmt, 0)!=7 ) goto is_repo_end;
  res = 1;

is_repo_end:
  sqlite3_finalize(pStmt);
  sqlite3_close(db);
  return res;
}

/*
** COMMAND: test-is-repo
** Usage: %fossil test-is-repo FILENAME...
**
** Test whether the specified files look like a SQLite database
** containing a Fossil repository schema.
*/
void test_is_repo(void){
  int i;
  verify_all_options();
  for(i=2; i<g.argc; i++){
    fossil_print("%s: %s\n",
       db_looks_like_a_repository(g.argv[i]) ? "yes" : " no",
       g.argv[i]
    );
  }
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
  if( !db_looks_like_a_repository(zDbName) ){
    if( file_access(zDbName, F_OK) ){
#ifdef FOSSIL_ENABLE_JSON
      g.json.resultCode = FSL_JSON_E_DB_NOT_FOUND;
#endif
      fossil_fatal("repository does not exist or"
                   " is in an unreadable directory: %s", zDbName);
    }else if( file_access(zDbName, R_OK) ){
#ifdef FOSSIL_ENABLE_JSON
      g.json.resultCode = FSL_JSON_E_DENIED;
#endif
      fossil_fatal("read permission denied for repository %s", zDbName);
    }else{
#ifdef FOSSIL_ENABLE_JSON
      g.json.resultCode = FSL_JSON_E_DB_NOT_VALID;
#endif
      fossil_fatal("not a valid repository: %s", zDbName);
    }
  }
  g.zRepositoryName = fossil_strdup(zDbName);
  db_open_or_attach(g.zRepositoryName, "repository");
  g.repositoryOpen = 1;
  sqlite3_file_control(g.db, "repository", SQLITE_FCNTL_DATA_VERSION,
                       &g.iRepoDataVers);

  /* Cache "allow-symlinks" option, because we'll need it on every stat call */
  g.allowSymlinks = db_get_boolean("allow-symlinks",0);

  g.zAuxSchema = db_get("aux-schema","");
  g.eHashPolicy = db_get_int("hash-policy",-1);
  if( g.eHashPolicy<0 ){
    g.eHashPolicy = hname_default_policy();
    db_set_int("hash-policy", g.eHashPolicy, 0);
  }

#if 0  /* No longer automatic.  Need to run "fossil rebuild" to migrate */
  /* Make a change to the CHECK constraint on the BLOB table for
  ** version 2.0 and later.
  */
  rebuild_schema_update_2_0();   /* Do the Fossil-2.0 schema updates */
#endif

  /* Additional checks that occur when opening the check-out database */
  if( g.localOpen ){

    /* If the repository database that was just opened has been
    ** eplaced by a clone of the same project, with different RID
    ** values, then renumber the RID values stored in various tables
    ** of the check-out database, so that the repository and check-out
    ** databases align.
    */
    if( !db_fingerprint_ok() ){
      if( find_option("no-rid-adjust",0,0)!=0 ){
        /* The --no-rid-adjust command-line option bypasses the RID value
        ** updates. Intended for use during debugging, especially to be
        ** able to run "fossil sql" after a database swap. */
        fossil_print(
          "WARNING: repository change detected, but no adjust made.\n"
        );
      }else if( find_option("rid-renumber-dryrun",0,0)!=0 ){
        /* the --rid-renumber-dryrun option shows how RID values would be
        ** renumbered, but does not actually perform the renumbering.
        ** This is a debugging-only option. */
        vfile_rid_renumbering_event(1);
        exit(0);
      }else{
        char *z;
        stash_rid_renumbering_event();
        vfile_rid_renumbering_event(0);
        undo_reset();
        bisect_reset();
        z = db_fingerprint(0, 1);
        db_lset("fingerprint", z);
        fossil_free(z);
        fossil_print(
          "WARNING: The repository database has been replaced by a clone.\n"
          "Bisect history and undo have been lost.\n"
        );
      }
    }

    /* Make sure the check-out database schema migration of 2019-01-20
    ** has occurred.
    **
    ** The 2019-01-19 migration is the addition of the vmerge.mhash and
    ** vfile.mhash columns and making the vmerge.mhash column part of the
    ** PRIMARY KEY for vmerge.
    */
    if( !db_table_has_column("localdb", "vfile", "mhash") ){
      db_multi_exec("ALTER TABLE vfile ADD COLUMN mhash;");
      db_multi_exec(
        "UPDATE vfile"
        "   SET mhash=(SELECT uuid FROM blob WHERE blob.rid=vfile.mrid)"
        " WHERE mrid!=rid;"
      );
      if( !db_table_has_column("localdb", "vmerge", "mhash") ){
        db_exec_sql("ALTER TABLE vmerge RENAME TO old_vmerge;");
        db_exec_sql(zLocalSchemaVmerge);
        db_exec_sql(
           "INSERT OR IGNORE INTO vmerge(id,merge,mhash)"
           "  SELECT id, merge, blob.uuid FROM old_vmerge, blob"
           "   WHERE old_vmerge.merge=blob.rid;"
           "DROP TABLE old_vmerge;"
        );
      }
    }
  }
}

/*
** Return true if there have been any changes to the repository
** database since it was opened.
**
** Changes to "config" and "localdb" and "temp" do not count.
** This routine only returns true if there have been changes
** to "repository".
*/
int db_repository_has_changed(void){
  unsigned int v;
  if( !g.repositoryOpen ) return 0;
  sqlite3_file_control(g.db, "repository", SQLITE_FCNTL_DATA_VERSION, &v);
  return g.iRepoDataVers != v;
}

/*
** Flags for the db_find_and_open_repository() function.
*/
#if INTERFACE
#define OPEN_OK_NOT_FOUND       0x001   /* Do not error out if not found */
#define OPEN_ANY_SCHEMA         0x002   /* Do not error if schema is wrong */
#define OPEN_SUBSTITUTE         0x004   /* Fake in-memory repo if not found */
#endif

/*
** Try to find the repository and open it.  Use the -R or --repository
** option to locate the repository.  If no such option is available, then
** use the repository of the open check-out if there is one.
**
** Error out if the repository cannot be opened.
*/
void db_find_and_open_repository(int bFlags, int nArgUsed){
  const char *zRep = find_repository_option();
  if( zRep && file_isdir(zRep, ExtFILE)==1 ){
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
  if( bFlags & OPEN_OK_NOT_FOUND ){
    /* No errors if the database is not found */
    if( bFlags & OPEN_SUBSTITUTE ){
      db_create_repository(0);
    }
  }else{
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
** Use this command to avoid having to close and reopen a check-out
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
    fossil_fatal("not in a local check-out");
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
    fossil_fatal("current directory is not within an open check-out");
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
  sqlite3_set_authorizer(g.db, 0, 0);
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
  if( db.nBegin ){
    if( reportErrors ){
      fossil_warning("Transaction started at %s:%d never commits",
                     db.zStartFile, db.iStartLine);
    }
    db_end_transaction(1);
  }
  pStmt = 0;
  sqlite3_busy_timeout(g.db, 0);
  g.dbIgnoreErrors++; /* Stop "database locked" warnings */
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
      db_unprotect(PROTECT_ALL);
      db_multi_exec("VACUUM localdb;");
      db_protect_pop();
    }
  }

  if( g.db ){
    int rc;
    sqlite3_wal_checkpoint(g.db, 0);
    rc = sqlite3_close(g.db);
    if( g.fSqlTrace ) fossil_trace("-- sqlite3_close(%d)\n", rc);
    if( rc==SQLITE_BUSY && reportErrors ){
      while( (pStmt = sqlite3_next_stmt(g.db, pStmt))!=0 ){
        fossil_warning("unfinalized SQL statement: [%s]", sqlite3_sql(pStmt));
      }
    }
    g.db = 0;
  }
  g.repositoryOpen = 0;
  g.localOpen = 0;
  db.bProtectTriggers = 0;
  assert( g.dbConfig==0 );
  assert( g.zConfigDbName==0 );
  backoffice_run_if_needed();
}

/*
** Close the database as quickly as possible without unnecessary processing.
*/
void db_panic_close(void){
  if( g.db ){
    int rc;
    sqlite3_wal_checkpoint(g.db, 0);
    rc = sqlite3_close(g.db);
    if( g.fSqlTrace ) fossil_trace("-- sqlite3_close(%d)\n", rc);
    db_clear_authorizer();
  }
  g.db = 0;
  g.repositoryOpen = 0;
  g.localOpen = 0;
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
  db_unprotect(PROTECT_USER);
  db_multi_exec(
     "INSERT OR IGNORE INTO user(login, info) VALUES(%Q,'')", zUser
  );
  db_multi_exec(
     "UPDATE user SET cap='s', pw=%Q"
     " WHERE login=%Q", fossil_random_password(10), zUser
  );
  if( !setupUserOnly ){
    db_multi_exec(
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('anonymous',hex(randomblob(8)),'hz','Anon');"
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('nobody','','gjorz','Nobody');"
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('developer','','ei','Dev');"
       "INSERT OR IGNORE INTO user(login,pw,cap,info)"
       "   VALUES('reader','','kptw','Reader');"
    );
  }
  db_protect_pop();
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

  db_unprotect(PROTECT_ALL);
  db_set("content-schema", CONTENT_SCHEMA, 0);
  db_set("aux-schema", AUX_SCHEMA_MAX, 0);
  db_set("rebuilt", get_version(), 0);
  db_multi_exec(
      "INSERT INTO config(name,value,mtime)"
      " VALUES('server-code', lower(hex(randomblob(20))),now());"
      "INSERT INTO config(name,value,mtime)"
      " VALUES('project-code', lower(hex(randomblob(20))),now());"
  );
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
  db_protect_pop();

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
** COMMAND: new#
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
** associated permissions will be copied.  In case of SQL errors, rebuild the
** template repository and try again.
**
** Options:
**    --template      FILE         Copy settings from repository file
**    -A|--admin-user USERNAME     Select given USERNAME as admin user
**    --date-override DATETIME     Use DATETIME as time of the initial check-in
**    --sha1                       Use an initial hash policy of "sha1"
**    --project-name  STRING       The name of the project "project name in
**                                 quotes"
**    --project-desc  STRING       The description of the project "project
**                                 description in quotes"
**
** DATETIME may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
** year-month-day form, it may be truncated, the "T" may be replaced by
** a space, and it may also name a timezone offset from UTC as "-HH:MM"
** (westward) or "+HH:MM" (eastward). Either no timezone suffix or "Z"
** means UTC.
**
** See also: [[clone]]
*/
void create_repository_cmd(void){
  char *zPassword;
  const char *zTemplate;      /* Repository from which to copy settings */
  const char *zDate;          /* Date of the initial check-in */
  const char *zDefaultUser;   /* Optional name of the default user */
  const char *zProjectName;   /* Optional project name of the repo */
  const char *zProjectDesc;   /* Optional project description "description
                              ** of project in quotes" */
  int bUseSha1 = 0;           /* True to set the hash-policy to sha1 */


  zTemplate = find_option("template",0,1);
  zDate = find_option("date-override",0,1);
  zDefaultUser = find_option("admin-user","A",1);
  bUseSha1 = find_option("sha1",0,0)!=0;
  zProjectName = find_option("project-name", 0, 1);
  zProjectDesc = find_option("project-desc", 0, 1);
  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=3 ){
    usage("REPOSITORY-NAME");
  }

  if( -1 != file_size(g.argv[2], ExtFILE) ){
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
  if( zProjectName ) db_set("project-name", zProjectName, 0);
  if( zProjectDesc ) db_set("project-description", zProjectDesc, 0);
  if( zDate==0 ) zDate = "now";
  db_initial_setup(zTemplate, zDate, zDefaultUser);
  db_end_transaction(0);
  if( zTemplate ) db_detach("settingSrc");
  if( zProjectName ) fossil_print("project-name: %s\n", zProjectName);
  if( zProjectDesc ) fossil_print("project-description: %s\n", zProjectDesc);
  fossil_print("project-id: %s\n", db_get("project-code", 0));
  fossil_print("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  fossil_print("admin-user: %s (initial remote-access password is \"%s\")\n",
               g.zLogin, zPassword);
  hash_user_password(g.zLogin);
}

/*
** SQL functions for debugging.
**
** The print() function writes its arguments on stdout, but only
** if the -sqlprint command-line option is turned on.
*/
void db_sql_print(
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

/*
** Callback for sqlite3_trace_v2();
*/
int db_sql_trace(unsigned m, void *notUsed, void *pP, void *pX){
  sqlite3_stmt *pStmt = (sqlite3_stmt*)pP;
  char *zSql;
  int n;
  const char *zArg = (const char*)pX;
  char zEnd[100];
  if( m & SQLITE_TRACE_CLOSE ){
    /* If we are tracking closes, that means we want to clean up static
    ** prepared statements. */
    while( db.pAllStmt ){
      db_finalize(db.pAllStmt);
    }
    return 0;
  }
  if( zArg[0]=='-' ) return 0;
  if( m & SQLITE_TRACE_PROFILE ){
    sqlite3_int64 nNano = *(sqlite3_int64*)pX;
    double rMillisec = 0.000001 * nNano;
    int nRun = sqlite3_stmt_status(pStmt, SQLITE_STMTSTATUS_RUN, 0);
    int nVmStep = sqlite3_stmt_status(pStmt, SQLITE_STMTSTATUS_VM_STEP, 1);
    sqlite3_snprintf(sizeof(zEnd),zEnd," /* %.3fms, %r run, %d vm-steps */\n",
        rMillisec, nRun, nVmStep);
  }else{
    zEnd[0] = '\n';
    zEnd[1] = 0;
  }
  zSql = sqlite3_expanded_sql(pStmt);
  n = (int)strlen(zSql);
  fossil_trace("%s%s%s", zSql, (n>0 && zSql[n-1]==';') ? "" : ";", zEnd);
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
** Implementation of the "win_reserved(X)" SQL function, a wrapper
** for file_is_win_reserved(X) which returns true if X is
** a Windows-reserved filename.
*/
LOCAL void db_win_reserved_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char * zName = (const char *)sqlite3_value_text(argv[0]);
  if( zName!=0 ){
    sqlite3_result_int(context, file_is_win_reserved(zName)!=0);
  }
}

/*
** Convert the input string into an artifact hash.  Make a notation in the
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
    zOut = fossil_strdup_nn(zKey);
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
** work on the configuration database instead of on the repository database.
** Be sure to swap them back after doing the operation.
**
** If the configuration database has already been opened as the main database
** or is attached to the main database, no connection swaps are required so
** this routine is a no-op.
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
**
** zCkin is normally NULL.  In that case, the versioned setting is
** take from the local check-out, if a local checkout exists, or from
** checkin named by the g.zOpenRevision global variable.  If zCkin is
** not NULL, then zCkin is the name of the specific checkin from which
** versioned setting value is taken.  When zCkin is not NULL, the cache
** is bypassed.
*/
char *db_get_versioned(
  const char *zName,
  char *zNonVersionedSetting,
  const char *zCkin
){
  char *zVersionedSetting = 0;
  int noWarn = 0;
  int found = 0;
  struct _cacheEntry {
    struct _cacheEntry *next;
    const char *zName, *zValue;
  } *cacheEntry = 0;
  static struct _cacheEntry *cache = 0;

  if( !g.localOpen && g.zOpenRevision==0 && zCkin==0 ){
    return zNonVersionedSetting;
  }

  /* Look up name in cache */
  if( zCkin==0 ){
    cacheEntry = cache;
    while( cacheEntry!=0 ){
      if( fossil_strcmp(cacheEntry->zName, zName)==0 ){
        zVersionedSetting = fossil_strdup(cacheEntry->zValue);
        break;
      }
      cacheEntry = cacheEntry->next;
    }
  }

  /* Attempt to read value from file in check-out if there wasn't a cache hit.*/
  if( cacheEntry==0 ){
    Blob versionedPathname;
    Blob setting;
    blob_init(&versionedPathname, 0, 0);
    blob_init(&setting, 0, 0);
    if( !g.localOpen || zCkin!=0 ){
      /* Repository is in the process of being opened, but files have not been
       * written to disk. Load from the database. */
      blob_appendf(&versionedPathname, ".fossil-settings/%s", zName);
      if( historical_blob(zCkin ? zCkin : g.zOpenRevision,
                          blob_str(&versionedPathname),
                          &setting, 0)
      ){
        found = 1;
      }
    }else{
      blob_appendf(&versionedPathname, "%s.fossil-settings/%s",
                   g.zLocalRoot, zName);
      if( file_size(blob_str(&versionedPathname), ExtFILE)>=0 ){
        /* File exists, and contains the value for this setting. Load from
        ** the file. */
        const char *zFile = blob_str(&versionedPathname);
        if( blob_read_from_file(&setting, zFile, ExtFILE)>=0 ){
          found = 1;
        }
        /* See if there's a no-warn flag */
        blob_append(&versionedPathname, ".no-warn", -1);
        if( file_size(blob_str(&versionedPathname), ExtFILE)>=0 ){
          noWarn = 1;
        }
      }
    }
    blob_reset(&versionedPathname);
    if( found ){
      blob_strip_comment_lines(&setting, &setting);
      blob_trim(&setting); /* Avoid non-obvious problems with line endings
                           ** on boolean properties */
      zVersionedSetting = fossil_strdup(blob_str(&setting));
    }
    blob_reset(&setting);

    /* Store result in cache, which can be the value or 0 if not found */
    if( zCkin==0 ){
      cacheEntry = (struct _cacheEntry*)fossil_malloc(sizeof(*cacheEntry));
      cacheEntry->next = cache;
      cacheEntry->zName = zName;
      cacheEntry->zValue = fossil_strdup(zVersionedSetting);
      cache = cacheEntry;
    }
  }

  /* Display a warning? */
  if( zVersionedSetting!=0
   && zNonVersionedSetting!=0
   && zNonVersionedSetting[0]!='\0'
   && zCkin==0
   && !noWarn
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
    static Stmt q1;
    const char *zRes;
    db_static_prepare(&q1, "SELECT value FROM config WHERE name=$n");
    db_bind_text(&q1, "$n", zName);
    if( db_step(&q1)==SQLITE_ROW && (zRes = db_column_text(&q1,0))!=0 ){
      z = fossil_strdup(zRes);
    }
    db_reset(&q1);
  }
  if( z==0 && g.zConfigDbName ){
    static Stmt q2;
    const char *zRes;
    db_swap_connections();
    db_static_prepare(&q2, "SELECT value FROM global_config WHERE name=$n");
    db_swap_connections();
    db_bind_text(&q2, "$n", zName);
    if( db_step(&q2)==SQLITE_ROW && (zRes = db_column_text(&q2,0))!=0 ){
      z = fossil_strdup(zRes);
    }
    db_reset(&q2);
  }
  if( pSetting!=0 && pSetting->versionable ){
    /* This is a versionable setting, try and get the info from a
    ** checked-out file */
    char * zZ = z;
    z = db_get_versioned(zName, z, 0);
    if(zZ != z){
      fossil_free(zZ);
    }
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
char *db_get_mtime(const char *zName, const char *zFormat,
                   const char *zDefault){
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
  const CmdOrPage *pCmd = 0;
  db_assert_protection_off_or_not_sensitive(zName);
  if( zValue!=0 && zValue[0]==0
   && dispatch_name_search(zName, CMDFLAG_SETTING, &pCmd)==0
   && (pCmd->eCmdFlags & CMDFLAG_KEEPEMPTY)==0
  ){
    /* Changing a setting to an empty string is the same as unsetting it,
    ** unless that setting has the keep-empty flag. */
    db_unset(zName/*works-like:"x"*/, globalFlag);
    return;
  }
  db_unprotect(PROTECT_CONFIG);
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
  db_protect_pop();
}
void db_unset(const char *zName, int globalFlag){
  db_begin_transaction();
  db_unprotect(PROTECT_CONFIG);
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
  db_protect_pop();
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
    static Stmt q;
    db_static_prepare(&q, "SELECT value FROM config WHERE name=$n");
    db_bind_text(&q, "$n", zName);
    rc = db_step(&q);
    if( rc==SQLITE_ROW ){
      v = db_column_int(&q, 0);
    }
    db_reset(&q);
  }else{
    rc = SQLITE_DONE;
  }
  if( rc==SQLITE_DONE && g.zConfigDbName ){
    static Stmt q2;
    db_swap_connections();
    db_static_prepare(&q2, "SELECT value FROM global_config WHERE name=$n");
    db_swap_connections();
    db_bind_text(&q2, "$n", zName);
    if( db_step(&q2)==SQLITE_ROW ){
      v = db_column_int(&q2, 0);
    }
    db_reset(&q2);
  }
  return v;
}
i64 db_large_file_size(void){
  /* Return size of the largest file that is not considered oversized */
  return strtoll(db_get("large-file-size","20000000"),0,0);
}
void db_set_int(const char *zName, int value, int globalFlag){
  db_assert_protection_off_or_not_sensitive(zName);
  db_unprotect(PROTECT_CONFIG);
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
  db_protect_pop();
}
int db_get_boolean(const char *zName, int dflt){
  char *zVal = db_get(zName, dflt ? "on" : "off");
  if( is_truth(zVal) ){
    dflt = 1;
  }else if( is_false(zVal) ){
    dflt = 0;
  }
  fossil_free(zVal);
  return dflt;
}
int db_get_versioned_boolean(const char *zName, int dflt){
  char *zVal = db_get_versioned(zName, 0, 0);
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
int db_lget_boolean(const char *zName, int dflt){
  char *zVal = db_lget(zName, dflt ? "on" : "off");
  if( is_truth(zVal) ){
    dflt = 1;
  }else if( is_false(zVal) ){
    dflt = 0;
  }
  fossil_free(zVal);
  return dflt;
}
void db_lset_int(const char *zName, int value){
  db_multi_exec("REPLACE INTO vvar(name,value) VALUES(%Q,%d)", zName, value);
}

/* Va-args versions of db_get(), db_set(), and db_unset()
**
** codecheck1.c verifies that the format string for db_set_mprintf()
** and db_unset_mprintf() begins with an ASCII character prefix.  We
** don't want that format string to begin with %s or %d as that might
** allow an injection attack to set or overwrite arbitrary settings.
*/
char *db_get_mprintf(const char *zDefault, const char *zFormat, ...){
  va_list ap;
  char *zName;
  char *zResult;
  va_start(ap, zFormat);
  zName = vmprintf(zFormat, ap);
  va_end(ap);
  zResult = db_get(zName, zDefault);
  fossil_free(zName);
  return zResult;
}
void db_set_mprintf(const char *zNew, int iGlobal, const char *zFormat, ...){
  va_list ap;
  char *zName;
  va_start(ap, zFormat);
  zName = vmprintf(zFormat, ap);
  va_end(ap);
  db_set(zName/*works-like:"x"*/, zNew, iGlobal);
  fossil_free(zName);
}
void db_unset_mprintf(int iGlobal, const char *zFormat, ...){
  va_list ap;
  char *zName;
  va_start(ap, zFormat);
  zName = vmprintf(zFormat, ap);
  va_end(ap);
  db_unset(zName/*works-like:"x"*/, iGlobal);
  fossil_free(zName);
}

/*
** Get a setting that is tailored to subsystem.  The return value is
** NULL if the setting does not exist, or a string obtained from mprintf()
** if the setting is available.
**
** The actual setting can be a comma-separated list of values of the form:
**
**    *   VALUE
**    *   SUBSYSTEM=VALUE
**
** A VALUE without the SUBSYSTEM= prefix is the default.  This routine
** returns the VALUE that with the matching SUBSYSTEM, or the default
** VALUE if there is no match.
*/
char *db_get_for_subsystem(const char *zName, const char *zSubsys){
  int nSubsys;
  char *zToFree = 0;
  char *zCopy;
  char *zNext;
  char *zResult = 0;
  const char *zSetting = db_get(zName, 0);
  if( zSetting==0 ) return 0;
  zCopy = zToFree = fossil_strdup(zSetting);
  if( zSubsys==0 ) zSubsys = "";
  nSubsys = (int)strlen(zSubsys);
  while( zCopy ){
    zNext = strchr(zCopy, ',');
    if( zNext ){
      zNext[0] = 0;
      do{ zNext++; }while( fossil_isspace(zNext[0]) );
      if( zNext[0]==0 ) zNext = 0;
    }
    if( strchr(zCopy,'=')==0 ){
      if( zResult==0 ) zResult = zCopy;
    }else
    if( nSubsys
     && strncmp(zCopy, zSubsys, nSubsys)==0
     && zCopy[nSubsys]=='='
    ){
      zResult = &zCopy[nSubsys+1];
      break;
    }
    zCopy = zNext;
  }
  if( zResult ) zResult = fossil_strdup(zResult);
  fossil_free(zToFree);
  return zResult;
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
**
** "manifest" is a versionable setting.  But we do not issue a warning
** if there is a conflict.  Instead, the value returned is the value for
** the versioned setting if the versioned setting exists, or the ordinary
** setting otherwise.
**
** The argument zCkin is the specific check-in for which we want the
** manifest setting.
*/
int db_get_manifest_setting(const char *zCkin){
  int flg;
  char *zVal;
  
  /* Look for the versioned setting first */
  zVal = db_get_versioned("manifest", 0, zCkin);

  if( zVal==0 && g.repositoryOpen ){
    /* No versioned setting, look for the repository setting second */
    zVal = db_text(0, "SELECT value FROM config WHERE name='manifest'");
  }
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
** COMMAND: test-manifest-setting
**
** Usage: %fossil test-manifest-setting VERSION VERSION ...
**
** Display the value for the "manifest" setting for various versions
** of the repository.
*/
void test_manfest_setting_cmd(void){
  int i;
  db_find_and_open_repository(0, 0);
  for(i=2; i<g.argc; i++){
    int m = db_get_manifest_setting(g.argv[i]);
    fossil_print("%s:\n", g.argv[i]);
    fossil_print("   flags = 0x%02x\n", m);
    if( m & MFESTFLG_RAW ){
      fossil_print("   manifest\n");
    }
    if( m & MFESTFLG_UUID ){
      fossil_print("   manifest.uuid\n");
    }
    if( m & MFESTFLG_TAGS ){
      fossil_print("   manifest.tags\n");
    }
  }
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
** If running from a local check-out, also record the root of the check-out
** as follows:
**
**       ckout:%s
**
** Where %s is the check-out root.  The value is the repository file.
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

  db_unprotect(PROTECT_CONFIG);
  db_multi_exec(
     "DELETE FROM global_config WHERE name %s = %Q;",
     filename_collation(), zRepoSetting
  );
  db_multi_exec(
     "INSERT OR IGNORE INTO global_config(name,value)"
     "VALUES(%Q,1);",
     zRepoSetting
  );
  db_protect_pop();
  fossil_free(zRepoSetting);
  if( g.localOpen && g.zLocalRoot && g.zLocalRoot[0] ){
    Blob localRoot;
    file_canonical_name(g.zLocalRoot, &localRoot, 1);
    zCkoutSetting = mprintf("ckout:%q", blob_str(&localRoot));
    db_unprotect(PROTECT_CONFIG);
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
    db_protect_pop();
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
** Usage: %fossil open REPOSITORY ?VERSION? ?OPTIONS?
**
** Open a new connection to the repository name REPOSITORY.  A check-out
** for the repository is created with its root at the current working
** directory, or in DIR if the "--workdir DIR" is used.  If VERSION is
** specified then that version is checked out.  Otherwise the most recent
** check-in on the main branch (usually "trunk") is used.
**
** REPOSITORY can be the filename for a repository that already exists on the
** local machine or it can be a URI for a remote repository.  If REPOSITORY
** is a URI in one of the formats recognized by the [[clone]] command, the
** remote repo is first cloned, then the clone is opened. The clone will be
** stored in the current directory, or in DIR if the "--repodir DIR" option
** is used. The name of the clone will be taken from the last term of the URI.
** For "http:" and "https:" URIs, you can append an extra term to the end of
** the URI to get any repository name you like. For example:
**
**     fossil open https://fossil-scm.org/home/new-name
**
** The base URI for cloning is "https://fossil-scm.org/home".  The extra
** "new-name" term means that the cloned repository will be called
** "new-name.fossil".
**
** Options:
**   --empty           Initialize check-out as being empty, but still connected
**                     with the local repository. If you commit this check-out,
**                     it will become a new "initial" commit in the repository.
**   -f|--force        Continue with the open even if the working directory is
**                     not empty, or if auto-sync fails.
**   --force-missing   Force opening a repository with missing content
**   -k|--keep         Only modify the manifest file(s)
**   --nested          Allow opening a repository inside an opened check-out
**   --nosync          Do not auto-sync the repository prior to opening even
**                     if the autosync setting is on.
**   --proxy PROXY     Use PROXY as http proxy during sync operation
**   --repodir DIR     If REPOSITORY is a URI that will be cloned, store
**                     the clone in DIR rather than in "."
**   --setmtime        Set timestamps of all files to match their SCM-side
**                     times (the timestamp of the last check-in which modified
**                     them).
**   --verbose         If passed a URI then this flag is passed on to the clone
**                     operation, otherwise it has no effect
**   --workdir DIR     Use DIR as the working directory instead of ".". The DIR
**                     directory is created if it does not exist.
**
** See also: [[close]], [[clone]]
*/
void cmd_open(void){
  int emptyFlag;
  int keepFlag;
  int forceMissingFlag;
  int allowNested;
  int setmtimeFlag;              /* --setmtime.  Set mtimes on files */
  int bForce = 0;                /* --force.  Open even if non-empty dir */
  static char *azNewArgv[] = { 0, "checkout", "--prompt", 0, 0, 0, 0 };
  const char *zWorkDir;          /* --workdir value */
  const char *zRepo = 0;         /* Name of the repository file */
  const char *zRepoDir = 0;      /* --repodir value */
  char *zPwd;                    /* Initial working directory */
  int isUri = 0;                 /* True if REPOSITORY is a URI */
  int nLocal;                    /* Number of preexisting files in cwd */
  int bVerbose = 0;              /* --verbose option for clone */

  url_proxy_options();
  emptyFlag = find_option("empty",0,0)!=0;
  keepFlag = find_option("keep","k",0)!=0;
  forceMissingFlag = find_option("force-missing",0,0)!=0;
  allowNested = find_option("nested",0,0)!=0;
  setmtimeFlag = find_option("setmtime",0,0)!=0;
  zWorkDir = find_option("workdir",0,1);
  zRepoDir = find_option("repodir",0,1);
  bForce = find_option("force","f",0)!=0;
  if( find_option("nosync",0,0) ) g.fNoSync = 1;
  bVerbose = find_option("verbose",0,0)!=0;
  zPwd = file_getcwd(0,0);

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=3 && g.argc!=4 ){
    usage("REPOSITORY-FILENAME ?VERSION?");
  }
  zRepo = g.argv[2];
  if( sqlite3_strglob("http://*", zRepo)==0
   || sqlite3_strglob("https://*", zRepo)==0
   || sqlite3_strglob("ssh:*", zRepo)==0
   || sqlite3_strglob("file:*", zRepo)==0
  ){
    isUri = 1;
  }

  /* If --workdir is specified, change to the requested working directory */
  if( zWorkDir ){
    if( !isUri ){
      zRepo = file_canonical_name_dup(zRepo);
    }
    if( zRepoDir ){
      zRepoDir = file_canonical_name_dup(zRepoDir);
    }
    if( file_isdir(zWorkDir, ExtFILE)!=1 ){
      file_mkfolder(zWorkDir, ExtFILE, 0, 0);
      if( file_mkdir(zWorkDir, ExtFILE, 0) ){
        fossil_fatal("cannot create directory %s", zWorkDir);
      }
    }
    if( file_chdir(zWorkDir, 0) ){
      fossil_fatal("unable to make %s the working directory", zWorkDir);
    }
  }
  if( keepFlag==0
   && bForce==0
   && (nLocal = file_directory_size(".", 0, 1))>0
   && (nLocal>1 || isUri || !file_in_cwd(zRepo))
  ){
    fossil_fatal("directory %s is not empty\n"
                 "use the -f (--force) option to override\n"
                 "or the -k (--keep) option to keep local files unchanged",
                 file_getcwd(0,0));
  }

  if( db_open_local_v2(0, allowNested) ){
    fossil_fatal("there is already an open tree at %s", g.zLocalRoot);
  }

  /* If REPOSITORY looks like a URI, then try to clone it first */
  if( isUri ){
    char *zNewBase;   /* Base name of the cloned repository file */
    const char *zUri; /* URI to clone */
    int rc;           /* Result code from fossil_system() */
    Blob cmd;         /* Clone command to be run */
    char *zCmd;       /* String version of the clone command */

    zUri = zRepo;
    zNewBase = url_to_repo_basename(zUri);
    if( zNewBase==0 ){
      fossil_fatal("unable to deduce a repository name from the url \"%s\"",
                   zUri);
    }
    if( zRepoDir==0 ){
      zRepo = mprintf("%s.fossil", zNewBase);
    }else{
      zRepo = mprintf("%s/%s.fossil", zRepoDir, zNewBase);
    }
    fossil_free(zNewBase);
    blob_init(&cmd, 0, 0);
    blob_append_escaped_arg(&cmd, g.nameOfExe, 1);
    blob_append(&cmd, " clone", -1);
    if(0!=bVerbose){
      blob_append(&cmd, " --verbose", -1);
    }
    blob_append_escaped_arg(&cmd, zUri, 1);
    blob_append_escaped_arg(&cmd, zRepo, 1);
    zCmd = blob_str(&cmd);
    fossil_print("%s\n", zCmd);
    if( zWorkDir ) file_chdir(zPwd, 0);
    rc = fossil_system(zCmd);
    if( rc ){
      fossil_fatal("clone of %s failed", zUri);
    }
    blob_reset(&cmd);
    if( zWorkDir ) file_chdir(zWorkDir, 0);
  }else if( zRepoDir ){
    fossil_fatal("the --repodir option only makes sense if the REPOSITORY "
                 "argument is a URI that begins with http:, https:, ssh:, "
                 "or file:");
  }

  db_open_config(0,0);
  db_open_repository(zRepo);

  /* Figure out which revision to open. */
  if( !emptyFlag ){
    if( g.argc==4 ){
      g.zOpenRevision = g.argv[3];
    }else if( db_exists("SELECT 1 FROM event WHERE type='ci'") ){
      g.zOpenRevision = db_get("main-branch", 0);
    }
    if( autosync_loop(SYNC_PULL, !bForce, "open") && !bForce ){
      fossil_fatal("unable to auto-sync the repository");
    }
  }


#if defined(_WIN32) || defined(__CYGWIN__)
# define LOCALDB_NAME "./_FOSSIL_"
#else
# define LOCALDB_NAME "./.fslckout"
#endif
  db_init_database(LOCALDB_NAME, zLocalSchema, zLocalSchemaVmerge,
#ifdef FOSSIL_LOCAL_WAL
                   "COMMIT; PRAGMA journal_mode=WAL; BEGIN;",
#endif
                   (char*)0);
  db_delete_on_failure(LOCALDB_NAME);
  db_open_local(0);
  db_lset("repository", zRepo);
  db_record_repository_filename(zRepo);
  db_set_checkout(0);
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
  if( setmtimeFlag ){
    int const vid = db_lget_int("checkout", 0);
    if(vid!=0){
      vfile_check_signature(vid, CKSIG_SETMTIME);
    }
  }
  g.argc = 2;
  info_cmd();
}

/*
** Return true if pSetting has its default value assuming its
** current value is zVal.
*/
int setting_has_default_value(const Setting *pSetting, const char *zVal){
  if( zVal==0 ) return 1;
  if( pSetting->def==0 ) return 0;
  if( pSetting->width==0 ){
    return is_false(pSetting->def)==is_false(zVal);
  }
  if( fossil_strcmp(pSetting->def, zVal)==0 ) return 1;
  if( is_false(zVal) && is_false(pSetting->def) ) return 1;
  if( is_truth(zVal) && is_truth(pSetting->def) ) return 1;
  return 0;
}

/*
** Print the current value of a setting identified by the pSetting
** pointer.
**
** Only show the value, not the setting name, if valueOnly is true.
**
** Show nothing if bIfChng is true and the setting is not currently set
** or is set to its default value.
*/
void print_setting(const Setting *pSetting, int valueOnly, int bIfChng){
  Stmt q;
  int versioned = 0;
  if( pSetting->versionable && g.localOpen ){
    /* Check to see if this is overridden by a versionable settings file */
    Blob versionedPathname;
    blob_zero(&versionedPathname);
    blob_appendf(&versionedPathname, "%s.fossil-settings/%s",
                 g.zLocalRoot, pSetting->name);
    if( file_size(blob_str(&versionedPathname), ExtFILE)>=0 ){
      versioned = 1;
    }
    blob_reset(&versionedPathname);
  }
  if( valueOnly && versioned ){
    const char *zVal = db_get_versioned(pSetting->name, NULL, NULL);
    if( !bIfChng || (zVal!=0 && fossil_strcmp(zVal, pSetting->def)!=0) ){
      fossil_print("%s\n", db_get_versioned(pSetting->name, NULL, NULL));
    }else{
      versioned = 0;
    }
    return;
  }
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
    const char *zVal = db_column_text(&q,1);
    if( bIfChng && setting_has_default_value(pSetting,zVal) ){
      if( versioned ){
        fossil_print("%-24s (versioned)\n", pSetting->name);
        versioned = 0;
      }
    }else if( valueOnly ){
      fossil_print("%s\n", db_column_text(&q, 1));
    }else{
      const char *zVal = (const char*)db_column_text(&q,1);
      const char *zName = (const char*)db_column_text(&q,0);
      if( zVal==0 ) zVal = "NULL";
      if( strchr(zVal,'\n')==0 ){
        fossil_print("%-24s %-11s %s\n", pSetting->name, zName, zVal);
      }else{
        fossil_print("%-24s %-11s\n", pSetting->name, zName);
        while( zVal[0] ){
          char *zNL = strchr(zVal, '\n');
          if( zNL==0 ){
            fossil_print("    %s\n", zVal);
            break;
          }else{
            int n = (int)(zNL - zVal);
            while( n>0 && fossil_isspace(zVal[n-1]) ){ n--; }
            fossil_print("    %.*s\n", n, zVal);
            zVal = zNL+1;
          }
        }
      }
    }
  }else if( bIfChng ){
    /* Display nothing */
    versioned = 0;
  }else if( valueOnly ){
    fossil_print("\n");
  }else{
    fossil_print("%-24s\n", pSetting->name);
  }
  if( versioned ){
    fossil_print("  (overridden by contents of file .fossil-settings/%s)\n",
                 pSetting->name);
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
** width is the length for the edit field on the behavior page, 0 is
** used for on/off checkboxes. A negative value indicates that that
** page should not render this setting. Such values may be rendered
** separately/manually on another page, e.g., /setup_access, and are
** exposed via the CLI settings command.
**
** The behaviour page doesn't use a special layout. It lists all
** set-commands and displays the 'set'-help as info.
*/
struct Setting {
  const char *name;     /* Name of the setting */
  const char *var;      /* Internal variable name used by db_set() */
  int width;            /* Width of display.  0 for boolean values and
                        ** negative for values which should not appear
                        ** on the /setup_settings page. */
  char versionable;     /* Is this setting versionable? */
  char forceTextArea;   /* Force using a text area for display? */
  char sensitive;       /* True if this a security-sensitive setting */
  const char *def;      /* Default value */
};

#endif /* INTERFACE */

/*
** SETTING: access-log      boolean default=on
**
** When the access-log setting is enabled, all login attempts (successful
** and unsuccessful) on the web interface are recorded in the "access" table
** of the repository.
*/
/*
** SETTING: admin-log       boolean default=on
**
** When the admin-log setting is enabled, configuration changes are recorded
** in the "admin_log" table of the repository.
*/
/*
** SETTING: allow-symlinks  boolean default=off sensitive
**
** When allow-symlinks is OFF, Fossil does not see symbolic links
** (a.k.a "symlinks") on disk as a separate class of object.  Instead Fossil
** sees the object that the symlink points to.  Fossil will only manage files
** and directories, not symlinks.  When a symlink is added to a repository,
** the object that the symlink points to is added, not the symlink itself.
**
** When allow-symlinks is ON, Fossil sees symlinks on disk as a separate
** object class that is distinct from files and directories.  When a symlink
** is added to a repository, Fossil stores the target filename. In other
** words, Fossil stores the symlink itself, not the object that the symlink
** points to.
**
** Symlinks are not cross-platform. They are not available on all
** operating systems and file systems. Hence the allow-symlinks setting is
** OFF by default, for portability.
*/
/*
** SETTING: auto-captcha    boolean default=on variable=autocaptcha
** If enabled, the /login page provides a button that will automatically
** fill in the captcha password.  This makes things easier for human users,
** at the expense of also making logins easier for malicious robots.
*/
/*
** SETTING: auto-hyperlink  width=16 default=1
**
** If non-zero, enable hyperlinks on web pages even for users that lack
** the "h" privilege as long as the UserAgent string in the HTTP request
** (The HTTP_USER_AGENT cgi variable) looks like it comes from a human and
** not a robot.  Details depend on the value of the setting.
**
**   (0)  Off:  No adjustments are made to the 'h' privilege based on
**        the user agent.
**
**   (1)  UserAgent and Javascript:  The the href= values of hyperlinks
**        initially point to /honeypot and are changed to point to the
**        correct target by javascript that runs after the page loads.
**        The auto-hyperlink-delay and auto-hyperlink-mouseover settings
**        influence that javascript.
**
**   (2)  UserAgent only:  If the HTTP_USER_AGENT looks human
**        then generate hyperlinks, otherwise do not.
**
** Better robot exclusion is obtained when this setting is 1 versus 2.
** However, a value of 1 causes the visited/unvisited colors of hyperlinks
** to stop working on Safari-derived web browsers.  When this setting is 2,
** the hyperlinks work better on Safari, but more robots are able to sneak
** in.
*/
/*
** SETTING: auto-hyperlink-delay     width=16 default=0
**
** When the auto-hyperlink setting is 1, the javascript that runs to set
** the href= attributes of hyperlinks delays by this many milliseconds
** after the page load.  Suggested values:  50 to 200.
*/
/*
** SETTING: auto-hyperlink-mouseover  boolean default=off
**
** When the auto-hyperlink setting is 1 and this setting is on, the
** javascript that runs to set the href= attributes of hyperlinks waits
** until either a mousedown or mousemove event is seen.  This helps
** to distinguish real users from robots. For maximum robot defense,
** the recommended setting is ON.
*/
/*
** SETTING: auto-shun       boolean default=on
** If enabled, automatically pull the shunning list
** from a server to which the client autosyncs.
*/
/*
** SETTING: autosync        width=16 default=on
** This setting determines when autosync occurs.  The setting is a
** string that provides a lot of flexibility for determining when and
** when not to autosync.  Examples:
**
**    on                     Always autosync for command where autosync
**                           makes sense ("commit", "merge", "open", "update")
**
**    off                    Never autosync.
**
**    pullonly               Only to pull autosyncs
**
**    all                    Sync with all remotes
**
**    on,open=off            Autosync for most commands, but not for "open"
**
**    off,commit=pullonly    Do not autosync, except do a pull before each
**                           "commit", presumably to avoid undesirable
**                           forks.
**
** The syntax is a comma-separated list of VALUE and COMMAND=VALUE entries.
** A plain VALUE entry is the default that is used if no COMMAND matches.
** Otherwise, the VALUE of the matching command is used.
**
** The "all" value is special in that it applies to the "sync" command in
** addition to "commit", "merge", "open", and "update".
*/
/*
** SETTING: autosync-tries  width=16 default=1
** If autosync is enabled setting this to a value greater
** than zero will cause autosync to try no more than this
** number of attempts if there is a sync failure.
*/
/*
** SETTING: backoffice-nodelay boolean default=off
** If backoffice-nodelay is true, then the backoffice processing
** will never invoke sleep().  If it has nothing useful to do,
** it simply exits.
*/
/*
** SETTING: backoffice-disable boolean default=off
** If backoffice-disable is true, then the automatic backoffice
** processing is disabled.  Automatic backoffice processing is the
** backoffice work that normally runs after each web page is
** rendered.  Backoffice processing that is triggered by the
** "fossil backoffice" command is unaffected by this setting.
**
** Backoffice processing does things such as delivering
** email notifications.  So if this setting is true, and if
** there is no cron job periodically running "fossil backoffice",
** email notifications and other work normally done by the
** backoffice will not occur.
*/
/*
** SETTING: backoffice-logfile width=40 sensitive
** If backoffice-logfile is not an empty string and is a valid
** filename, then a one-line message is appended to that file
** every time the backoffice runs.  This can be used for debugging,
** to ensure that backoffice is running appropriately.
*/
/*
** SETTING: binary-glob     width=40 versionable block-text
** The VALUE of this setting is a list of GLOB patterns matching files
** that should be treated as "binary" for committing and merging
** purposes.  Example: *.jpg,*.png  The parsing rules are complex;
** see https://fossil-scm.org/home/doc/trunk/www/globs.md#syntax
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
** The VALUE of this setting is a list of GLOB patterns matching files
** that the "clean" command will delete without prompting or allowing
** undo.  Example: *.a,*.o,*.so  The parsing rules are complex;
** see https://fossil-scm.org/home/doc/trunk/www/globs.md#syntax
*/
/*
** SETTING: clearsign       boolean default=off
** When enabled, fossil will attempt to sign all commits
** with gpg or ssh.  When disabled, commits will be unsigned.
*/
/*
** SETTING: comment-format  width=16 default=1
** Set the algorithm for printing timeline comments to the console.
**
** Possible values are:
**    1     Use the original comment printing algorithm:
**             *   Leading and trialing whitespace is removed
**             *   Internal whitespace is converted into a single space (0x20)
**             *   Line breaks occurs at whitespace or hyphens if possible
**          This is the recommended value and the default.
**
** Or a bitwise combination of the following flags:
**    2     Trim leading and trailing CR and LF characters.
**    4     Trim leading and trailing white space characters.
**    8     Attempt to break lines on word boundaries.
**   16     Break lines before the original comment embedded in other text.
**
** Note: To preserve line breaks and/or other whitespace within comment text,
** make this setting some integer value that omits the "1" bit.
*/
/*
** SETTING: crlf-glob       width=40 versionable block-text
** The VALUE of this setting is a list of GLOB patterns matching files
** in which it is allowed to have CR, CR+LF or mixed line endings,
** suppressing Fossil's normal warning about this. Set it to "*" to
** disable CR+LF checking entirely.  Example: *.md,*.txt
** The crnl-glob setting is a compatibility alias.
*/
/*
** SETTING: crnl-glob       width=40 versionable block-text
** This is an alias for the crlf-glob setting.
*/
/*
** SETTING: default-perms   width=16 default=u sensitive keep-empty
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
** SETTING: diff-command    width=40 sensitive
** The value is an external command to run when performing a diff.
** If undefined, the internal text diff will be used.
*/
/*
** SETTING: dont-commit     boolean default=off
** If enabled, prevent committing to this repository, as an extra precaution
** against accidentally checking in to a repository intended to be read-only.
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
** SETTING: editor          width=32 sensitive
** The value is an external command that will launch the
** text editor command used for check-in comments.
**
** If this value is not set, then environment variables VISUAL and
** EDITOR are consulted, in that order.  If neither of those are set,
** then a search is made for common text editors, including
** "notepad", "nano", "pico", "jove", "edit", "vi", "vim", and "ed".
**
** If this setting is false ("off", "no", "false", or "0") then no
** text editor is used.
*/
/*
** SETTING: empty-dirs      width=40 versionable block-text
** The value is a list of pathnames parsed according to the same rules as
** the *-glob settings.  On update and checkout commands, if no directory
** exists with that name, an empty directory will be be created, even if
** it must create one or more parent directories.
*/
/*
** SETTING: encoding-glob   width=40 versionable block-text
** The VALUE of this setting is a list of GLOB patterns matching files that
** the "commit" command will ignore when issuing warnings about text files
** that may use another encoding than ASCII or UTF-8. Set to "*" to disable
** encoding checking.  Example: *.md,*.txt  The parsing rules are complex;
** see https://fossil-scm.org/home/doc/trunk/www/globs.md#syntax
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
** SETTING: fileedit-glob       width=40 block-text
** The VALUE of this setting is a list of GLOB patterns matching files
** which are allowed to be edited using the /fileedit page.  An empty list
** suppresses the feature.  Example: *.md,*.txt  The parsing rules are
** complex; see https://fossil-scm.org/home/doc/trunk/www/globs.md#syntax
** Note that /fileedit cannot edit binary files, so the list should not
** contain any globs for, e.g., images or PDFs.
*/
/*
** SETTING: forbid-delta-manifests    boolean default=off
** If enabled on a client, new delta manifests are prohibited on
** commits.  If enabled on a server, whenever a client attempts
** to obtain a check-in lock during auto-sync, the server will
** send the "pragma avoid-delta-manifests" statement in its reply,
** which will cause the client to avoid generating a delta
** manifest.
*/
/*
** SETTING: gdiff-command    width=40 sensitive
** The value is an external command to run when performing a graphical
** diff. If undefined, a --tk diff is done if commands "tclsh" and "wish"
** are on PATH, or a --by diff is done if "tclsh" or "wish" are unavailable.
*/
/*
** SETTING: gmerge-command   width=40 sensitive
** The value is a graphical merge conflict resolver command operating
** on four files.  Examples:
**
**     kdiff3 "%baseline" "%original" "%merge" -o "%output"
**     xxdiff "%original" "%baseline" "%merge" -M "%output"
**     meld "%baseline" "%original" "%merge" --output "%output"
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
** login screen requests to HTTPS.
*/
/*
** SETTING: ignore-glob      width=40 versionable block-text
** The VALUE of this setting is a list of GLOB patterns matching files that
** the "add", "addremove", "clean", and "extras" commands will ignore.
** Example: *.log,notes.txt  The parsing rules are complex; see
** https://fossil-scm.org/home/doc/trunk/www/globs.md#syntax
*/
/*
** SETTING: keep-glob        width=40 versionable block-text
** The VALUE of this setting is a list of GLOB patterns matching files that
** the "clean" command must not delete.  Example: build/precious.exe
** The parsing rules are complex; see
** https://fossil-scm.org/home/doc/trunk/www/globs.md#syntax
*/
/*
** SETTING: localauth        boolean default=off
** If enabled, require that HTTP connections from the loopback
** address (127.0.0.1) be authenticated by password.  If false,
** some HTTP requests might be granted full "Setup" user
** privileges without having to present login credentials.
** This mechanism allows the "fossil ui" command to provide
** full access to the repository without requiring the user to
** log in first.
**
** In order for full "Setup" privilege to be granted without a
** login, the following conditions must be met:
**
**   (1)  This setting ("localauth") must be off
**   (2)  The HTTP request arrive over the loopback TCP/IP
**        address (127.0.01) or else via SSH.
**   (3)  The request must be HTTP, not HTTPS. (This
**        restriction is designed to help prevent accidentally
**        providing "Setup" privileges to requests arriving
**        over a reverse proxy.)
**   (4)  The command that launched the fossil server must be
**        one of the following:
**        (a) "fossil ui"
**        (b) "fossil server" with the --localauth option
**        (c) "fossil http" with the --localauth option
**        (d) CGI with the "localauth" setting in the cgi script.
**
** For maximum security, set "localauth" to 1.  However, because
** of the other restrictions (2) through (4), it should be safe
** to leave "localauth" set to 0 in most installations, and
** especially on cloned repositories on workstations. Leaving
** "localauth" at 0 makes the "fossil ui" command more convenient
** to use.
*/
/*
** SETTING: lock-timeout  width=25 default=60
** This is the number of seconds that a check-in lock will be held on
** the server before the lock expires.  The default is a 60-second delay.
** Set this value to zero to disable the check-in lock mechanism.
**
** This value should be set on the server to which users auto-sync
** their work.  This setting has no effect on client repositories.  The
** check-in lock mechanism is only effective if all users are auto-syncing
** to the same server.
**
** Check-in locks are an advisory mechanism designed to help prevent
** accidental forks due to a check-in race in installations where many
** users are  committing to the same branch and auto-sync is enabled.
** As forks are harmless, there is no danger in disabling this mechanism.
** However, keeping check-in locks turned on can help prevent unnecessary
** confusion.
*/
/*
** SETTING: main-branch      width=40 default=trunk
** The value is the primary branch for the project.
*/
/*
** SETTING: manifest         width=5 versionable
** If enabled, automatically create files "manifest" and "manifest.uuid"
** in every check-out.
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
** SETTING: mimetypes        width=40 versionable block-text
** A list of file extension-to-mimetype mappings, one per line. e.g.
** "foo application/x-foo". File extensions are compared
** case-insensitively in the order listed in this setting.  A leading
** '.' on file extensions is permitted but not required.
*/
/*
** SETTING: mtime-changes    boolean default=on
** Use file modification times (mtimes) to detect when
** files have been modified.  If disabled, all managed files
** are hashed to detect changes, which can be slow for large
** projects.
*/
/*
** SETTING: mv-rm-files      boolean default=off
** If enabled, the "mv" and "rename" commands will also move
** the associated files within the check-out -AND- the "rm"
** and "delete" commands will also remove the associated
** files from within the check-out.
*/
/*
** SETTING: pgp-command      width=40 sensitive
** Command used to clear-sign manifests at check-in.
** Default value is "gpg --clearsign -o".
** For SSH, use e.g. "ssh-keygen -q -Y sign -n fossilscm -f ~/.ssh/id_ed25519"
*/
/*
** SETTING: proxy            width=32 default=system
** URL of the HTTP proxy. If undefined or "system", the "http_proxy"
** environment variable is consulted. If "off", a direct HTTP connection is
** used.
*/
/*
** SETTING: redirect-to-https   default=0 width=2
** Specifies whether or not to redirect unencrypted "http://" requests to
** encrypted "https://" URIs. A value of 0 (the default) means do not
** redirect, 1 means to redirect only the /login page, and 2
** means to always redirect.
**
** For security, a value of 2 is recommended.  The default value is 0
** because not all sites are TLS-capable.  But you should definitely enable
** TLS and change this setting to 2 for all public-facing repositories.
*/
/*
** SETTING: relative-paths   boolean default=on
** When showing changes and extras, report paths relative
** to the current working directory.
*/
/*
** SETTING: repo-cksum       boolean default=on
** Compute checksums over all files in each check-out as a double-check
** of correctness.  Disable this on large repositories for a performance
** improvement.
*/
/*
** SETTING: repolist-skin    width=2 default=0
** If non-zero then use this repository as the skin for a repository list
** such as created by the one of:
**
**    1)  fossil server DIRECTORY --repolist
**    2)  fossil ui DIRECTORY --repolist
**    3)  fossil http DIRECTORY --repolist
**    4)  (The "repolist" option in a CGI script)
**    5)  fossil all ui
**    6)  fossil all server
**
** All repositories are searched (in lexicographical order) and the first
** repository with a non-zero "repolist-skin" value is used as the skin
** for the repository list page.  If none of the repositories on the list
** have a non-zero "repolist-skin" setting then the repository list is
** displayed using unadorned HTML ("skinless"), with the page title taken
** from the FOSSIL_REPOLIST_TITLE environment variable.
**
** If repolist-skin has a value of 2, then the repository is omitted from
** the list in use cases 1 through 4, but not for 5 and 6.
*/
/*
** SETTING: self-pw-reset    boolean default=off sensitive
** Allow users to request that an email containing a hyperlink
** to the /resetpw page be sent to their email address of record,
** thus allowing forgetful users to reset their forgotten passwords
** without administrator involvement.
*/
/*
** SETTING: self-register    boolean default=off sensitive
** Allow users to register themselves through the HTTP UI.
** This is useful if you want to see other names than
** "Anonymous" in e.g. ticketing system. On the other hand
** users can not be deleted.
*/
/*
** SETTING: ssh-command      width=40 sensitive
** The command used to talk to a remote machine with  the "ssh://" protocol.
*/

/*
** SETTING: ssl-ca-location  width=40 sensitive
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
**
** This setting is overridden by environment variables
** SSL_CERT_FILE and SSL_CERT_DIR.
*/
/*
** SETTING: ssl-identity     width=40 sensitive
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
** SETTING: tcl              boolean default=off sensitive
** If enabled Tcl integration commands will be added to the TH1
** interpreter, allowing arbitrary Tcl expressions and
** scripts to be evaluated from TH1.  Additionally, the Tcl
** interpreter will be able to evaluate arbitrary TH1
** expressions and scripts.
*/
/*
** SETTING: tcl-setup        width=40 block-text sensitive
** This is the setup script to be evaluated after creating
** and initializing the Tcl interpreter.  By default, this
** is empty and no extra setup is performed.
*/
#endif /* FOSSIL_ENABLE_TCL */
/*
** SETTING: tclsh            width=80 default=tclsh sensitive
** Name of the external TCL interpreter used for such things
** as running the GUI diff viewer launched by the --tk option
** of the various "diff" commands.
*/
#ifdef FOSSIL_ENABLE_TH1_DOCS
/*
** SETTING: th1-docs         boolean default=off sensitive
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
** SETTING: th1-setup        width=40 block-text sensitive
** This is the setup script to be evaluated after creating
** and initializing the TH1 interpreter.  By default, this
** is empty and no extra setup is performed.
*/
/*
** SETTING: th1-uri-regexp   width=40 block-text
** Specify which URI's are allowed in HTTP requests from
** TH1 scripts.  If empty, no HTTP requests are allowed
** whatsoever.
*/
/*
** SETTING: default-csp      width=40 block-text keep-empty
**
** The text of the Content Security Policy that is included
** in the Content-Security-Policy: header field of the HTTP
** reply and in the default HTML <head> section that is added when the
** skin header does not specify a <head> section.  The text "$nonce"
** is replaced by the random nonce that is created for each web page.
**
** If this setting is an empty string or is omitted, then
** the following default Content Security Policy is used:
**
**     default-src 'self' data:;
**     script-src 'self' 'nonce-$nonce';
**     style-src 'self' 'unsafe-inline';
**     img-src * data:;
**
** The default CSP is recommended.  The main reason to change
** this setting would be to add CDNs from which it is safe to
** load additional content.
*/
/*
** SETTING: uv-sync          boolean default=off
** If true, automatically send unversioned files as part
** of a "fossil clone" or "fossil sync" command.  The
** default is false, in which case the -u option is
** needed to clone or sync unversioned files.
*/
/*
** SETTING: web-browser      width=30 sensitive
** A shell command used to launch your preferred
** web browser when given a URL as an argument.
** Defaults to "start" on windows, "open" on Mac,
** and "firefox" on Unix.
*/
/*
** SETTING: large-file-size     width=10 default=200000000
** Fossil considers any file whose size is greater than this value
** to be a "large file".  Fossil might issue warnings if you try to
** "add" or "commit" a "large file".  Set this value to 0 or less
** to disable all such warnings.
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
** configuration database.  If both a local and a global value exists for a
** setting, the local value takes precedence.  This command normally operates
** on the local settings.  Use the --global option to change global settings.
**
** Options:
**   --changed  Only show settings if the value differs from the default
**   --exact    Only consider exact name matches
**   --global   Set or unset the given property globally instead of
**              setting or unsetting it for the open repository only
**   --value    Only show the value of a given property (implies --exact)
**
** See also: [[configuration]]
*/
void setting_cmd(void){
  int i;
  int globalFlag = find_option("global","g",0)!=0;
  int bIfChng = find_option("changed",0,0)!=0;
  int exactFlag = find_option("exact",0,0)!=0;
  int valueFlag = find_option("value",0,0)!=0;
  /* Undocumented "--test-for-subsystem SUBSYS" option used to test
  ** the db_get_for_subsystem() interface: */
  const char *zSubsys = find_option("test-for-subsystem",0,1);
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
  if( valueFlag ){
    if( g.argc!=3 ){
      fossil_fatal("--value is only supported when qurying a given property");
    }
  }

  if( g.argc==2 ){
    for(i=0; i<nSetting; i++){
      print_setting(&aSetting[i], 0, bIfChng);
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
      if( n!=(int)strlen(pSetting[0].name) && pSetting[1].name &&
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
        db_unset(pSetting->name/*works-like:"x"*/, globalFlag);
      }else{
        db_protect_only(PROTECT_NONE);
        db_set(pSetting->name/*works-like:"x"*/, g.argv[3], globalFlag);
        db_protect_pop();
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
        if( zSubsys ){
          char *zValue = db_get_for_subsystem(pSetting->name, zSubsys);
          fossil_print("%s (subsystem %s) ->",  pSetting->name, zSubsys);
          if( zValue ){
            fossil_print(" [%s]", zValue);
            fossil_free(zValue);
          }
          fossil_print("\n");
        }else{
          print_setting(pSetting, valueFlag, bIfChng);
        }
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
  g.repositoryOpen = 0;
  g.localOpen = 0;
}

/*
** COMMAND: test-without-rowid
**
** Usage: %fossil test-without-rowid FILENAME...
**
** Change the Fossil repository FILENAME to make use of the WITHOUT ROWID
** optimization.  FILENAME can also be the configuration database file
** (~/.fossil or ~/.config/fossil.db) or a local .fslckout or _FOSSIL_ file.
**
** The purpose of this command is for testing the WITHOUT ROWID capabilities
** of SQLite.  There is no big advantage to using WITHOUT ROWID in Fossil.
**
** Options:
**    -n|--dry-run     No changes.  Just print what would happen.
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
      "SELECT name, sql FROM main.sqlite_schema "
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
  if( !db_table_exists("repository","admin_log") ){
    once = 1;
    db_multi_exec(
      "CREATE TABLE repository.admin_log(\n"
      " id INTEGER PRIMARY KEY,\n"
      " time INTEGER, -- Seconds since 1970\n"
      " page TEXT,    -- path of page\n"
      " who TEXT,     -- User who made the change\n"
      " what TEXT     -- What changed\n"
      ")"
    );
  }
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
** (2) The local check-out database
** (3) The global configuration database
*/
void test_database_name_cmd(void){
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  fossil_print("Repository database: %s\n", g.zRepositoryName);
  fossil_print("Local database:      %s\n", g.zLocalDbName);
  fossil_print("Config database:     %s\n", g.zConfigDbName);
}

/*
** Compute a "fingerprint" on the repository.  A fingerprint is used
** to verify that that the repository has not been replaced by a clone
** of the same repository.  More precisely, a fingerprint is used to
** verify that the mapping between SHA3 hashes and RID values is unchanged.
**
** The check-out database ("localdb") stores RID values.  When associating
** a check-out database against a repository database, it is useful to verify
** the fingerprint so that we know tha the RID values in the check-out
** database still correspond to the correct entries in the BLOB table of
** the repository.
**
** The fingerprint is based on the RCVFROM table.  When constructing a
** new fingerprint, use the most recent RCVFROM entry.  (Set rcvid==0 to
** accomplish this.)  When verifying an old fingerprint, use the same
** RCVFROM entry that generated the fingerprint in the first place.
**
** The fingerprint consists of the rcvid, a "/", and the MD5 checksum of
** the remaining fields of the RCVFROM table entry.  MD5 is used for this
** because it is 4x faster than SHA3 and 5x faster than SHA1, and there
** are no security concerns - this is just a checksum, not a security
** token.
*/
char *db_fingerprint(int rcvid, int iVersion){
  char *z = 0;
  Blob sql = BLOB_INITIALIZER;
  Stmt q;
  if( iVersion==0 ){
    /* The original fingerprint algorithm used "quote(mtime)".  But this
    ** could give slightly different answers depending on how the floating-
    ** point hardware is configured.  For example, it gave different
    ** answers on native Linux versus running under valgrind.  */
    blob_append_sql(&sql,
      "SELECT rcvid, quote(uid), quote(mtime), quote(nonce), quote(ipaddr)"
      "  FROM rcvfrom"
    );
  }else{
    /* These days, we use "datetime(mtime)" for more consistent answers */
    blob_append_sql(&sql,
      "SELECT rcvid, quote(uid), datetime(mtime), quote(nonce), quote(ipaddr)"
      "  FROM rcvfrom"
    );
  }
  if( rcvid<=0 ){
    blob_append_sql(&sql, " ORDER BY rcvid DESC LIMIT 1");
  }else{
    blob_append_sql(&sql, " WHERE rcvid=%d", rcvid);
  }
  db_prepare_blob(&q, &sql);
  blob_reset(&sql);
  if( db_step(&q)==SQLITE_ROW ){
    int i;
    md5sum_init();
    for(i=1; i<=4; i++){
      md5sum_step_text(db_column_text(&q,i),-1);
    }
    z = mprintf("%d/%s",db_column_int(&q,0),md5sum_finish(0));
  }
  db_finalize(&q);
  return z;
}

/*
** COMMAND: test-fingerprint
**
** Usage: %fossil test-fingerprint ?RCVID?
**
** Display the repository fingerprint using the supplied RCVID or
** using the latest RCVID if none is given on the command line.
** Show both the legacy and the newer version of the fingerprint,
** and the currently stored fingerprint if there is one.
*/
void test_fingerprint(void){
  int rcvid = 0;
  db_find_and_open_repository(OPEN_ANY_SCHEMA,0);
  if( g.argc==3 ){
    rcvid = atoi(g.argv[2]);
  }else if( g.argc!=2 ){
    fossil_fatal("wrong number of arguments");
  }
  fossil_print("legacy:              %z\n", db_fingerprint(rcvid, 0));
  fossil_print("version-1:           %z\n", db_fingerprint(rcvid, 1));
  if( g.localOpen ){
    fossil_print("localdb:             %z\n", db_lget("fingerprint","(none)"));
    fossil_print("db_fingerprint_ok(): %d\n", db_fingerprint_ok());
  }
  fossil_print("Fossil version:      %s - %.10s %.19s\n",
    RELEASE_VERSION, MANIFEST_DATE, MANIFEST_UUID);
}

/*
** Set the value of the "checkout" entry in the VVAR table.
**
** Also set "fingerprint" and "checkout-hash".
*/
void db_set_checkout(int rid){
  char *z;
  db_lset_int("checkout", rid);
  if (rid != 0) {
    z = db_text(0,"SELECT uuid FROM blob WHERE rid=%d",rid);
    db_lset("checkout-hash", z);
    fossil_free(z);
    z = db_fingerprint(0, 1);
    db_lset("fingerprint", z);
    fossil_free(z);
  }
}

/*
** Verify that the fingerprint recorded in the "fingerprint" entry
** of the VVAR table matches the fingerprint on the currently
** connected repository.  Return true if the fingerprint is ok, and
** return false if the fingerprint does not match.
*/
int db_fingerprint_ok(void){
  char *zCkout;   /* The fingerprint recorded in the check-out database */
  char *zRepo;    /* The fingerprint of the repository */
  int rc;         /* Result */

  if( !db_lget_int("checkout", 0) ){
    /* We have an empty check-out, fingerprint is still NULL. */
    return 2;
  }
  zCkout = db_text(0,"SELECT value FROM localdb.vvar WHERE name='fingerprint'");
  if( zCkout==0 ){
    /* This is an older check-out that does not record a fingerprint.
    ** We have to assume everything is ok */
    return 2;
  }
  zRepo = db_fingerprint(atoi(zCkout), 1);
  rc = fossil_strcmp(zCkout,zRepo)==0;
  fossil_free(zRepo);
  /* If the initial test fails, try again using the older fingerprint
  ** algorithm */
  if( !rc ){
    zRepo = db_fingerprint(atoi(zCkout), 0);
    rc = fossil_strcmp(zCkout,zRepo)==0;
    fossil_free(zRepo);
  }
  fossil_free(zCkout);
  return rc;
}

/*
** Adds the given rid to the UNSENT table.
*/
void db_add_unsent(int rid){
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", rid);
}
