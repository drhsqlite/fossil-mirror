/*
** Copyright (c) 2010 D. Richard Hipp
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
** This module contains the code that initializes the "sqlite3" command-line
** shell against the repository database.  The command-line shell itself
** is a copy of the "shell.c" code from SQLite.  This file contains logic
** to initialize the code in shell.c.
*/
#include "config.h"
#include "sqlcmd.h"
#include <stdlib.h> /* atexit() */
#include <zlib.h>
#ifndef _WIN32
#  include "linenoise.h"
#endif

/*
** True if the "fossil sql" command has the --test flag.  False otherwise.
*/
static int local_bSqlCmdTest = 0;

/*
** Implementation of the "content(X)" SQL function.  Return the complete
** content of artifact identified by X as a blob.
*/
static void sqlcmd_content(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int rid;
  Blob cx;
  const char *zName;
  assert( argc==1 );
  zName = (const char*)sqlite3_value_text(argv[0]);
  if( zName==0 ) return;
  g.db = sqlite3_context_db_handle(context);
  g.repositoryOpen = 1;
  rid = name_to_rid(zName);
  if( rid==0 ) return;
  if( content_get(rid, &cx) ){
    sqlite3_result_blob(context, blob_buffer(&cx), blob_size(&cx),
                                 SQLITE_TRANSIENT);
    blob_reset(&cx);
  }
}

/*
** Implementation of the "compress(X)" SQL function.  The input X is
** compressed using zLib and the output is returned.
*/
static void sqlcmd_compress(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned char *pOut;
  unsigned int nIn;
  unsigned long int nOut;
  int rc;

  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  nOut = 13 + nIn + (nIn+999)/1000;
  pOut = sqlite3_malloc( nOut+4 );
  pOut[0] = nIn>>24 & 0xff;
  pOut[1] = nIn>>16 & 0xff;
  pOut[2] = nIn>>8 & 0xff;
  pOut[3] = nIn & 0xff;
  rc = compress(&pOut[4], &nOut, pIn, nIn);
  if( rc==Z_OK ){
    sqlite3_result_blob(context, pOut, nOut+4, sqlite3_free);
  }else if( rc==Z_MEM_ERROR ){
    sqlite3_free(pOut);
    sqlite3_result_error_nomem(context);
  }else{
    sqlite3_free(pOut);
    sqlite3_result_error(context, "input cannot be zlib compressed", -1);
  }
}

/*
** Implementation of the "decompress(X)" SQL function.  The argument X
** is a blob which was obtained from compress(Y).  The output will be
** the value Y.
*/
static void sqlcmd_decompress(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *pIn;
  unsigned char *pOut;
  unsigned int nIn;
  unsigned long int nOut;
  int rc;

  pIn = sqlite3_value_blob(argv[0]);
  if( pIn==0 ) return;
  nIn = sqlite3_value_bytes(argv[0]);
  if( nIn<4 ) return;
  nOut = (pIn[0]<<24) + (pIn[1]<<16) + (pIn[2]<<8) + pIn[3];
  pOut = sqlite3_malloc( nOut+1 );
  rc = uncompress(pOut, &nOut, &pIn[4], nIn-4);
  if( rc==Z_OK ){
    sqlite3_result_blob(context, pOut, nOut, sqlite3_free);
  }else if( rc==Z_MEM_ERROR ){
    sqlite3_free(pOut);
    sqlite3_result_error_nomem(context);
  }else{
    sqlite3_free(pOut);
    sqlite3_result_error(context, "input is not zlib compressed", -1);
  }
}

/*
** Implementation of the "gather_artifact_stats(X)" SQL function.
** That function merely calls the gather_artifact_stats() function
** in stat.c to populate the ARTSTAT temporary table.
*/
static void sqlcmd_gather_artifact_stats(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  gather_artifact_stats(1);
}

/*
** Add the content(), compress(), decompress(), and
** gather_artifact_stats() SQL functions to database connection db.
*/
int add_content_sql_commands(sqlite3 *db){
  sqlite3_create_function(db, "content", 1, SQLITE_UTF8, 0,
                          sqlcmd_content, 0, 0);
  sqlite3_create_function(db, "compress", 1, SQLITE_UTF8, 0,
                          sqlcmd_compress, 0, 0);
  sqlite3_create_function(db, "decompress", 1, SQLITE_UTF8, 0,
                          sqlcmd_decompress, 0, 0);
  sqlite3_create_function(db, "gather_artifact_stats", 0, SQLITE_UTF8, 0,
                          sqlcmd_gather_artifact_stats, 0, 0);
  return SQLITE_OK;
}

/*
** Undocumented test SQL functions:
**
**     db_protect(X)
**     db_protect_pop(X)
**
** These invoke the corresponding C routines.
**
** WARNING:
** Do not instantiate these functions for any Fossil webpage or command
** method other than the "fossil sql" command.  If an attacker gains access
** to these functions, he will be able to disable other defense mechanisms.
**
** This routines are for interactiving testing only.  They are experimental
** and undocumented (apart from this comments) and might go away or change
** in future releases.
**
** 2020-11-29:  These functions are now only available if the "fossil sql"
** command is started with the --test option.
*/
static void sqlcmd_db_protect(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  unsigned mask = 0;
  const char *z = (const char*)sqlite3_value_text(argv[0]);
  if( z!=0 && local_bSqlCmdTest ){
    if( sqlite3_stricmp(z,"user")==0 )      mask |= PROTECT_USER;
    if( sqlite3_stricmp(z,"config")==0 )    mask |= PROTECT_CONFIG;
    if( sqlite3_stricmp(z,"sensitive")==0 ) mask |= PROTECT_SENSITIVE;
    if( sqlite3_stricmp(z,"readonly")==0 )  mask |= PROTECT_READONLY;
    if( sqlite3_stricmp(z,"all")==0 )       mask |= PROTECT_ALL;
    db_protect(mask);
  }
}
static void sqlcmd_db_protect_pop(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if( !local_bSqlCmdTest ) db_protect_pop();
}

/*
** This is the "automatic extension" initializer that runs right after
** the connection to the repository database is opened.  Set up the
** database connection to be more useful to the human operator.
*/
static int sqlcmd_autoinit(
  sqlite3 *db,
  const char **pzErrMsg,
  const void *notUsed
){
  int mTrace = SQLITE_TRACE_CLOSE;
  add_content_sql_commands(db);
  db_add_aux_functions(db);
  re_add_sql_func(db);
  search_sql_setup(db);
  foci_register(db);
  deltafunc_init(db);
  helptext_vtab_register(db);
  builtin_vtab_register(db);
  g.repositoryOpen = 1;
  g.db = db;
  sqlite3_busy_timeout(db, 10000);
  sqlite3_db_config(db, SQLITE_DBCONFIG_MAINDBNAME, "repository");
  db_maybe_set_encryption_key(db, g.zRepositoryName);
  if( g.zLocalDbName ){
    char *zSql = sqlite3_mprintf("ATTACH %Q AS 'localdb' KEY ''",
                                 g.zLocalDbName);
    sqlite3_exec(db, zSql, 0, 0, 0);
    sqlite3_free(zSql);
  }
  if( g.zConfigDbName ){
    char *zSql = sqlite3_mprintf("ATTACH %Q AS 'configdb' KEY ''",
                                 g.zConfigDbName);
    sqlite3_exec(db, zSql, 0, 0, 0);
    sqlite3_free(zSql);
  }
  (void)timeline_query_for_tty();  /* Registers wiki_to_text() as side-effect */
  /* Arrange to trace close operations so that static prepared statements
  ** will get cleaned up when the shell closes the database connection */
  if( g.fSqlTrace ) mTrace |= SQLITE_TRACE_PROFILE;
  sqlite3_trace_v2(db, mTrace, db_sql_trace, 0);
  db_protect_only(PROTECT_NONE);
  sqlite3_set_authorizer(db, db_top_authorizer, db);
  if( local_bSqlCmdTest ){
    sqlite3_create_function(db, "db_protect", 1, SQLITE_UTF8, 0,
                            sqlcmd_db_protect, 0, 0);
    sqlite3_create_function(db, "db_protect_pop", 0, SQLITE_UTF8, 0,
                            sqlcmd_db_protect_pop, 0, 0);
    sqlite3_create_function(db, "shared_secret", 2, SQLITE_UTF8, 0,
                            sha1_shared_secret_sql_function, 0, 0);
  }
  return SQLITE_OK;
}

/*
** atexit() handler that cleans up global state modified by this module.
*/
static void sqlcmd_atexit(void) {
  g.zConfigDbName = 0; /* prevent panic */
}

/*
** This routine is called by the sqlite3 command-line shell to
** to load the name the Fossil repository database.
*/
void sqlcmd_get_dbname(const char **pzRepoName){
  *pzRepoName = g.zRepositoryName;
}

/*
** This routine is called by the sqlite3 command-line shell to do
** extra initialization prior to starting up the shell.
*/
void sqlcmd_init_proc(void){
  sqlite3_initialize();
  sqlite3_auto_extension((void(*)(void))sqlcmd_autoinit);
}

#if USE_SEE
/*
** This routine is called by the patched sqlite3 command-line shell in order
** to load the encryption key for the open Fossil database.  The memory that
** is pointed to by the value placed in pzKey must be obtained from malloc.
*/
void fossil_key(const char **pzKey, int *pnKey){
  char *zSavedKey = db_get_saved_encryption_key();
  char *zKey;
  size_t savedKeySize = db_get_saved_encryption_key_size();

  if( !db_is_valid_saved_encryption_key(zSavedKey, savedKeySize) ) return;
  zKey = (char*)malloc( savedKeySize );
  if( zKey ){
    memcpy(zKey, zSavedKey, savedKeySize);
    *pzKey = zKey;
    if( fossil_getenv("FOSSIL_USE_SEE_TEXTKEY")==0 ){
      *pnKey = (int)strlen(zKey);
    }else{
      *pnKey = -1;
    }
  }else{
    fossil_fatal("failed to allocate %u bytes for key", savedKeySize);
  }
}
#endif

/*
** This routine closes the Fossil databases and/or invalidates the global
** state variables that keep track of them.
*/
static void fossil_close(int bDb, int noRepository){
  if( bDb ) db_close(1);
  if( noRepository ) g.zRepositoryName = 0;
  g.db = 0;
  g.repositoryOpen = 0;
  g.localOpen = 0;
}

/*
** COMMAND: sql
** COMMAND: sqlite3*
**
** Usage: %fossil sql ?OPTIONS?
**
** Run the sqlite3 command-line shell on the Fossil repository
** identified by the -R option, or on the current repository.
** See https://www.sqlite.org/cli.html for additional information about
** the sqlite3 command-line shell.
**
** WARNING:  Careless use of this command can corrupt a Fossil repository
** in ways that are unrecoverable.  Be sure you know what you are doing before
** running any SQL commands that modify the repository database.  Use the
** --readonly option to prevent accidental damage to the repository.
**
** Options:
**    --no-repository           Skip opening the repository database
**    --readonly                Open the repository read-only.  No changes
**                              are allowed.  This is a recommended safety
**                              precaution to prevent repository damage.
**    -R REPOSITORY             Use REPOSITORY as the repository database
**    --test                    Enable some testing and analysis features
**                              that are normally disabled.
**
** All of the standard sqlite3 command-line shell options should also
** work.
**
** The following SQL extensions are provided with this Fossil-enhanced
** version of the sqlite3 command-line shell:
**
**    builtin                   A virtual table that contains one row for
**                              each datafile that is built into the Fossil
**                              binary.
**
**    checkin_mtime(X,Y)        Return the mtime for the file Y (a BLOB.RID)
**                              found in check-in X (another BLOB.RID value).
**
**    compress(X)               Compress text X with the same algorithm used
**                              to compress artifacts in the BLOB table.
**
**    content(X)                Return the content of artifact X. X can be an
**                              artifact hash or hash prefix or a tag. Artifacts
**                              are stored compressed and deltaed. This function
**                              does all necessary decompression and undeltaing.
**
**    decompress(X)             Decompress text X.  Undoes the work of
**                              compress(X).
**
**    delta_apply(X,D)          Apply delta D to source blob X and return
**                              the result.
**
**    delta_create(X,Y)         Create and return a delta that will convert
**                              X into Y.
**
**    delta_output_size(D)      Return the number of bytes of output to expect
**                              when applying delta D
**
**    delta_parse(D)            A table-valued function that deconstructs
**                              delta D and returns rows for each element of
**                              that delta.
**
**    files_of_checkin(X)       A table-valued function that returns info on
**                              all files contained in check-in X.  Example:
**
**                                  SELECT * FROM files_of_checkin('trunk');
**
**    helptext                  A virtual table with one row for each command,
**                              webpage, and setting together with the built-in
**                              help text.
**
**    now()                     Return the number of seconds since 1970.
**
**    obscure(T)                Obfuscate the text password T so that its
**                              original value is not readily visible.  Fossil
**                              uses this same algorithm when storing passwords
**                              of remote URLs.
**
**    regexp                    The REGEXP operator works, unlike in
**                              standard SQLite.
**
**    symbolic_name_to_rid(X)   Return the BLOB.RID corresponding to symbolic
**                              name X.
*/
void cmd_sqlite3(void){
  int noRepository;
  char *zConfigDb;
  extern int sqlite3_shell(int, char**);
#ifdef FOSSIL_ENABLE_TH1_HOOKS
  g.fNoThHook = 1;
#endif
  noRepository = find_option("no-repository", 0, 0)!=0;
  local_bSqlCmdTest = find_option("test",0,0)!=0;
  if( !noRepository ){
    db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  }
  db_open_config(1,0);
  zConfigDb = fossil_strdup(g.zConfigDbName);
  fossil_close(1, noRepository);
  sqlite3_shutdown();
#ifndef _WIN32
  linenoiseSetMultiLine(1);
#endif
  atexit(sqlcmd_atexit);
  g.zConfigDbName = zConfigDb;
  g.argv[1] = "-quote";
  sqlite3_shell(g.argc, g.argv);
  sqlite3_cancel_auto_extension((void(*)(void))sqlcmd_autoinit);
  fossil_close(0, noRepository);
}
