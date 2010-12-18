/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file implements the undo/redo functionality.
*/
#include "config.h"
#include "undo.h"



/*
** Undo the change to the file zPathname.  zPathname is the pathname
** of the file relative to the root of the repository.  If redoFlag is
** true the redo a change.  If there is nothing to undo (or redo) then
** this routine is a noop.
*/
static void undo_one(const char *zPathname, int redoFlag){
  Stmt q;
  char *zFullname;
  db_prepare(&q,
    "SELECT content, existsflag FROM undo WHERE pathname=%Q AND redoflag=%d",
     zPathname, redoFlag
  );
  if( db_step(&q)==SQLITE_ROW ){
    int old_exists;
    int new_exists;
    Blob current;
    Blob new;
    zFullname = mprintf("%s/%s", g.zLocalRoot, zPathname);
    new_exists = file_size(zFullname)>=0;
    if( new_exists ){
      blob_read_from_file(&current, zFullname);
    }else{
      blob_zero(&current);
    }
    blob_zero(&new);
    old_exists = db_column_int(&q, 1);
    if( old_exists ){
      db_ephemeral_blob(&q, 0, &new);
    }
    if( old_exists ){
      if( new_exists ){
        printf("%s %s\n", redoFlag ? "REDO" : "UNDO", zPathname);
      }else{
        printf("NEW %s\n", zPathname);
      }
      blob_write_to_file(&new, zFullname);
    }else{
      printf("DELETE %s\n", zPathname);
      unlink(zFullname);
    }
    blob_reset(&new);
    free(zFullname);
    db_finalize(&q);
    db_prepare(&q, 
       "UPDATE undo SET content=:c, existsflag=%d, redoflag=NOT redoflag"
       " WHERE pathname=%Q",
       new_exists, zPathname
    );
    if( new_exists ){
      db_bind_blob(&q, ":c", &current);
    }
    db_step(&q);
    blob_reset(&current);
  }
  db_finalize(&q);
}

/*
** Undo or redo changes to the filesystem.  Undo the changes in the
** same order that they were originally carried out - undo the oldest
** change first and undo the most recent change last.
*/
static void undo_all_filesystem(int redoFlag){
  Stmt q;
  db_prepare(&q,
     "SELECT pathname FROM undo"
     " WHERE redoflag=%d"
     " ORDER BY rowid",
     redoFlag
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q, 0);
    undo_one(zPathname, redoFlag);
  }
  db_finalize(&q);
}

/*
** Undo or redo all undoable or redoable changes.
*/
static void undo_all(int redoFlag){
  int ucid;
  int ncid;
  undo_all_filesystem(redoFlag);
  db_multi_exec(
    "CREATE TEMP TABLE undo_vfile_2 AS SELECT * FROM vfile;"
    "DELETE FROM vfile;"
    "INSERT INTO vfile SELECT * FROM undo_vfile;"
    "DELETE FROM undo_vfile;"
    "INSERT INTO undo_vfile SELECT * FROM undo_vfile_2;"
    "DROP TABLE undo_vfile_2;"
    "CREATE TEMP TABLE undo_vmerge_2 AS SELECT * FROM vmerge;"
    "DELETE FROM vmerge;"
    "INSERT INTO vmerge SELECT * FROM undo_vmerge;"
    "DELETE FROM undo_vmerge;"
    "INSERT INTO undo_vmerge SELECT * FROM undo_vmerge_2;"
    "DROP TABLE undo_vmerge_2;"
  );
  ncid = db_lget_int("undo_checkout", 0);
  ucid = db_lget_int("checkout", 0);
  db_lset_int("undo_checkout", ucid);
  db_lset_int("checkout", ncid);
}

/*
** Reset the undo memory.
*/
void undo_reset(void){
  static const char zSql[] =
    @ DROP TABLE IF EXISTS undo;
    @ DROP TABLE IF EXISTS undo_vfile;
    @ DROP TABLE IF EXISTS undo_vmerge;
    ;
  db_multi_exec(zSql);
  db_lset_int("undo_available", 0);
  db_lset_int("undo_checkout", 0);
}

/*
** The following variable stores the original command-line of the
** command that is a candidate to be undone.
*/
static char *undoCmd = 0;

/*
** Capture the current command-line and store it as part of the undo
** state.  This routine is called before options are extracted from the
** command-line so that we can record the complete command-line.
*/
void undo_capture_command_line(void){
  Blob cmdline;
  int i;
  assert( undoCmd==0 );
  blob_zero(&cmdline);
  for(i=1; i<g.argc; i++){
    if( i>1 ) blob_append(&cmdline, " ", 1);
    blob_append(&cmdline, g.argv[i], -1);
  }
  undoCmd = blob_str(&cmdline);
}

/*
** This flag is true if we are in the process of collecting file changes
** for undo.  When this flag is false, undo_save() is a no-op.
*/
static int undoActive = 0;

/*
** Begin capturing a snapshot that can be undone.
*/
void undo_begin(void){
  int cid;
  const char *zDb = "localdb";
  static const char zSql[] = 
    @ CREATE TABLE %s.undo(
    @   pathname TEXT UNIQUE,             -- Name of the file
    @   redoflag BOOLEAN,                 -- 0 for undoable.  1 for redoable
    @   existsflag BOOLEAN,               -- True if the file exists
    @   content BLOB                      -- Saved content
    @ );
    @ CREATE TABLE %s.undo_vfile AS SELECT * FROM vfile;
    @ CREATE TABLE %s.undo_vmerge AS SELECT * FROM vmerge;
  ;
  undo_reset();
  if( strcmp(g.zMainDbType,zDb)==0 ) zDb = "main";
  db_multi_exec(zSql, zDb, zDb, zDb, zDb);
  cid = db_lget_int("checkout", 0);
  db_lset_int("undo_checkout", cid);
  db_lset_int("undo_available", 1);
  db_lset("undo_cmdline", undoCmd);
  undoActive = 1;
}

/*
** This flag is true if one or more files have changed and have been
** recorded in the undo log but the undo log has not yet been committed.
**
** If a fatal error occurs and this flag is set, that means we should
** rollback all the filesystem changes.
*/
static int undoNeedRollback = 0;

/*
** Save the current content of the file zPathname so that it
** will be undoable.  The name is relative to the root of the
** tree.
*/
void undo_save(const char *zPathname){
  char *zFullname;
  Blob content;
  int existsFlag;
  Stmt q;

  if( !undoActive ) return;
  zFullname = mprintf("%s/%s", g.zLocalRoot, zPathname);
  existsFlag = file_size(zFullname)>=0;
  db_prepare(&q,
    "INSERT OR IGNORE INTO undo(pathname,redoflag,existsflag,content)"
    " VALUES(%Q,0,%d,:c)",
    zPathname, existsFlag
  );
  if( existsFlag ){
    blob_read_from_file(&content, zFullname);
    db_bind_blob(&q, ":c", &content);
  }
  free(zFullname);
  db_step(&q);
  db_finalize(&q);
  if( existsFlag ){
    blob_reset(&content);
  }
  undoNeedRollback = 1;
}

/*
** Complete the undo process is one is currently in process.
*/
void undo_finish(void){
  if( undoActive ){
    undoActive = 0;
    undoNeedRollback = 0;
  }
}

/*
** This routine is called when the process aborts due to an error.
** If an undo was being accumulated but was not finished, attempt
** to rollback all of the filesystem changes.
**
** This rollback occurs, for example, if an "update" or "merge" operation
** could not run to completion because a file that needed to be written
** was locked or had permissions turned off.
*/
void undo_rollback(void){
  if( !undoNeedRollback ) return;
  assert( undoActive );
  undoNeedRollback = 0;
  undoActive = 0;
  printf("Rolling back prior filesystem changes...\n");
  undo_all_filesystem(0);
}

/*
** COMMAND: undo
** COMMAND: redo
**
** Usage: %fossil undo ?--explain? ?FILENAME...?
**    or: %fossil redo ?--explain? ?FILENAME...?
**
** Undo the most recent update or merge or revert operation.  If FILENAME is
** specified then restore the content of the named file(s) but otherwise
** leave the update or merge or revert in effect.  The redo command undoes
** the effect of the most recent undo.
**
** If the --explain option is present, not changes are made and instead
** the undo or redo command explains what actions the undo or redo would
** have done had the --explain been omitted.
**
** A single level of undo/redo is supported.  The undo/redo stack
** is cleared by the commit and checkout commands.
*/
void undo_cmd(void){
  int isRedo = g.argv[1][0]=='r';
  int undo_available;
  int explainFlag = find_option("explain", 0, 0)!=0;
  const char *zCmd = isRedo ? "redo" : "undo";
  db_must_be_within_tree();
  db_begin_transaction();
  undo_available = db_lget_int("undo_available", 0);
  if( explainFlag ){
    if( undo_available==0 ){
      printf("No undo or redo is available\n");
    }else{
      Stmt q;
      int nChng = 0;
      zCmd = undo_available==1 ? "undo" : "redo";
      printf("A %s is available for the following command:\n\n   %s %s\n\n",
              zCmd, g.argv[0], db_lget("undo_cmdline", "???"));
      db_prepare(&q,
        "SELECT existsflag, pathname FROM undo ORDER BY pathname"
      );
      while( db_step(&q)==SQLITE_ROW ){
        if( nChng==0 ){
          printf("The following file changes would occur if the "
                 "command above is %sne:\n\n", zCmd);
        }
        nChng++;
        printf("%s %s\n", 
           db_column_int(&q,0) ? "UPDATE" : "DELETE",
           db_column_text(&q, 1)
        );
      }
      db_finalize(&q);
      if( nChng==0 ){
        printf("No file changes would occur with this undo/redo.\n");
      }
    }
  }else if( g.argc==2 ){
    if( undo_available!=(1+isRedo) ){
      fossil_fatal("nothing to %s", zCmd);
    }
    undo_all(isRedo);
    db_lset_int("undo_available", 2-isRedo);
  }else if( g.argc>=3 ){
    int i;
    if( undo_available==0 ){
      fossil_fatal("nothing to %s", zCmd);
    }
    for(i=2; i<g.argc; i++){
      const char *zFile = g.argv[i];
      Blob path;
      file_tree_name(zFile, &path, 1);
      undo_one(blob_str(&path), isRedo);
      blob_reset(&path);
    }
  }
  db_end_transaction(0);
}
