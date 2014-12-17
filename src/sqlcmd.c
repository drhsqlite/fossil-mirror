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
#if defined(FOSSIL_ENABLE_MINIZ)
#  define MINIZ_HEADER_FILE_ONLY
#  include "miniz.c"
#else
#  include <zlib.h>
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

  pIn = sqlite3_value_blob(argv[0]);
  nIn = sqlite3_value_bytes(argv[0]);
  nOut = 13 + nIn + (nIn+999)/1000;
  pOut = sqlite3_malloc( nOut+4 );
  pOut[0] = nIn>>24 & 0xff;
  pOut[1] = nIn>>16 & 0xff;
  pOut[2] = nIn>>8 & 0xff;
  pOut[3] = nIn & 0xff;
  compress(&pOut[4], &nOut, pIn, nIn);
  sqlite3_result_blob(context, pOut, nOut+4, sqlite3_free);
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
  re_add_sql_func(db);
  g.zMainDbType = "repository";
  foci_register(db);
  ftsearch_add_sql_func(db);
  g.repositoryOpen = 1;
  g.db = db;
  return SQLITE_OK;
}

/*
** COMMAND: sqlite3
**
** Usage: %fossil sqlite3 ?DATABASE? ?OPTIONS?
**
** Run the standalone sqlite3 command-line shell on DATABASE with OPTIONS.
** If DATABASE is omitted, then the repository that serves the working
** directory is opened.
**
** WARNING:  Careless use of this command can corrupt a Fossil repository
** in ways that are unrecoverable.  Be sure you know what you are doing before
** running any SQL commands that modifies the repository database.
*/
void cmd_sqlite3(void){
  extern int sqlite3_shell(int, char**);
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  db_close(1);
  sqlite3_shutdown();
  sqlite3_shell(g.argc-1, g.argv+1);
  sqlite3_cancel_auto_extension((void(*)(void))sqlcmd_autoinit);
  g.db = 0;
  g.zMainDbType = 0;
  g.repositoryOpen = 0;
  g.localOpen = 0;
}

/*
** This routine is called by the patched sqlite3 command-line shell in order
** to load the name and database connection for the open Fossil database.
*/
void fossil_open(const char **pzRepoName){
  sqlite3_auto_extension((void(*)(void))sqlcmd_autoinit);
  *pzRepoName = g.zRepositoryName;
}
