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
** This is the "automatic extensionn" initializer that runs right after
** the connection to the repository database is opened.  Set up the
** database connection to be more useful to the human operator.
*/
static int sqlcmd_autoinit(
  sqlite3 *db,
  const char **pzErrMsg,
  const void *notUsed
){
  sqlite3_create_function(db, "content", 1, SQLITE_ANY, 0,
                          sqlcmd_content, 0, 0);
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
void sqlite3_cmd(void){
  extern int sqlite3_shell(int, char**);
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  db_close();
  sqlite3_shutdown();
  sqlite3_shell(g.argc-1, g.argv+1);
}

/*
** This routine is called by the patched sqlite3 command-line shell in order
** to load the name and database connection for the open Fossil database.
*/
void fossil_open(const char **pzRepoName){
  sqlite3_auto_extension((void(*)(void))sqlcmd_autoinit);
  *pzRepoName = g.zRepositoryName;
}
