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
#if defined(FOSSIL_ENABLE_MINIZ)
#  define MINIZ_HEADER_FILE_ONLY
#  include "miniz.c"
#else
#  include <zlib.h>
#endif

#ifndef _WIN32
#  include "linenoise.h"
#endif

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
  nIn = sqlite3_value_bytes(argv[0]);
  nOut = (pIn[0]<<24) + (pIn[1]<<16) + (pIn[2]<<8) + pIn[3];
  pOut = sqlite3_malloc( nOut+1 );
  rc = uncompress(pOut, &nOut, &pIn[4], nIn-4);
  if( rc==Z_OK ){
    sqlite3_result_blob(context, pOut, nOut, sqlite3_free);
  }else{
    sqlite3_free(pOut);
    sqlite3_result_error(context, "input is not zlib compressed", -1);
  }
}

/*
** Add the content(), compress(), and decompress() SQL functions to
** database connection db.
*/
int add_content_sql_commands(sqlite3 *db){
  sqlite3_create_function(db, "content", 1, SQLITE_UTF8, 0,
                          sqlcmd_content, 0, 0);
  sqlite3_create_function(db, "compress", 1, SQLITE_UTF8, 0,
                          sqlcmd_compress, 0, 0);
  sqlite3_create_function(db, "decompress", 1, SQLITE_UTF8, 0,
                          sqlcmd_decompress, 0, 0);
  return SQLITE_OK;
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
  add_content_sql_commands(db);
  db_add_aux_functions(db);
  re_add_sql_func(db);
  search_sql_setup(db);
  foci_register(db);
  g.repositoryOpen = 1;
  g.db = db;
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
  return SQLITE_OK;
}

/*
** atexit() handler that cleans up global state modified by this module.
*/
static void sqlcmd_atexit(void) {
  g.zConfigDbName = 0; /* prevent panic */
}

/*
** This routine is called by the patched sqlite3 command-line shell in order
** to load the name and database connection for the open Fossil database.
*/
void fossil_open(const char **pzRepoName){
  sqlite3_auto_extension((void(*)(void))sqlcmd_autoinit);
  *pzRepoName = g.zRepositoryName;
}

#if USE_SEE
/*
** This routine is called by the patched sqlite3 command-line shell in order
** to load the encryption key for the open Fossil database.  The memory that
** is pointed to by the value placed in pzKey must be obtained from SQLite.
*/
void fossil_key(const char **pzKey, int *pnKey){
  char *zSavedKey = db_get_saved_encryption_key();
  char *zKey;
  size_t savedKeySize = db_get_saved_encryption_key_size();
  size_t nByte;

  if( zSavedKey==0 || savedKeySize==0 ) return;
  nByte = savedKeySize * sizeof(char);
  zKey = sqlite3_malloc( (int)nByte );
  if( zKey ){
    memcpy(zKey, zSavedKey, nByte);
    *pzKey = zKey;
    if( fossil_getenv("FOSSIL_USE_SEE_TEXTKEY")==0 ){
      *pnKey = (int)strlen(zKey);
    }else{
      *pnKey = -1;
    }
  }else{
    fossil_fatal("failed to allocate %u bytes for key", nByte);
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
** COMMAND: sqlite3
**
** Usage: %fossil sql ?OPTIONS?
**
** Run the standalone sqlite3 command-line shell on DATABASE with SHELL_OPTS.
** If DATABASE is omitted, then the repository that serves the working
** directory is opened.  See https://www.sqlite.org/cli.html for additional
** information.
**
** Options:
**
**    --no-repository           Skip opening the repository database.
**
**    -R REPOSITORY             Use REPOSITORY as the repository database
**
** WARNING:  Careless use of this command can corrupt a Fossil repository
** in ways that are unrecoverable.  Be sure you know what you are doing before
** running any SQL commands that modify the repository database.
**
** The following extensions to the usual SQLite commands are provided:
**
**    content(X)                Return the content of artifact X.  X can be an
**                              artifact hash or prefix or a tag.
**
**    compress(X)               Compress text X.
**
**    decompress(X)             Decompress text X.  Undoes the work of
**                              compress(X).
**
**    checkin_mtime(X,Y)        Return the mtime for the file Y (a BLOB.RID)
**                              found in check-in X (another BLOB.RID value).
**
**    symbolic_name_to_rid(X)   Return the BLOB.RID corresponding to symbolic
**                              name X.
**
**    now()                     Return the number of seconds since 1970.
**
**    REGEXP                    The REGEXP operator works, unlike in
**                              standard SQLite.
**
**    files_of_checkin(X)       A table-valued function that returns info on
**                              all files contained in check-in X.  Example:
**                                SELECT * FROM files_of_checkin('trunk');
*/
void cmd_sqlite3(void){
  int noRepository;
  const char *zConfigDb;
  extern int sqlite3_shell(int, char**);
#ifdef FOSSIL_ENABLE_TH1_HOOKS
  g.fNoThHook = 1;
#endif
  noRepository = find_option("no-repository", 0, 0)!=0;
  if( !noRepository ){
    db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  }
  db_open_config(1,0);
  zConfigDb = g.zConfigDbName;
  fossil_close(1, noRepository);
  sqlite3_shutdown();
#ifndef _WIN32
  linenoiseSetMultiLine(1);
#endif
  atexit(sqlcmd_atexit);
  g.zConfigDbName = zConfigDb;
  sqlite3_shell(g.argc-1, g.argv+1);
  sqlite3_cancel_auto_extension((void(*)(void))sqlcmd_autoinit);
  fossil_close(0, noRepository);
}
