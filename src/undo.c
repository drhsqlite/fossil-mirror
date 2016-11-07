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

#if INTERFACE
/*
** Possible return values from the undo_maybe_save() routine.
*/
#define UNDO_NONE     (0) /* Placeholder only used to initialize vars. */
#define UNDO_SAVED_OK (1) /* The specified file was saved succesfully. */
#define UNDO_DISABLED (2) /* File not saved, subsystem is disabled. */
#define UNDO_INACTIVE (3) /* File not saved, subsystem is not active. */
#define UNDO_TOOBIG   (4) /* File not saved, it exceeded a size limit. */
#endif

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
    "SELECT content, existsflag, isExe, isLink FROM undo"
    " WHERE pathname=%Q AND redoflag=%d",
     zPathname, redoFlag
  );
  if( db_step(&q)==SQLITE_ROW ){
    int old_exists;
    int new_exists;
    int old_exe;
    int new_exe;
    int new_link;
    int old_link;
    Blob current;
    Blob new;
    zFullname = mprintf("%s/%s", g.zLocalRoot, zPathname);
    old_link = db_column_int(&q, 3);
    new_exists = file_wd_size(zFullname)>=0;
    new_link = file_wd_islink(0);
    if( new_exists ){
      if( new_link ){
        blob_read_link(&current, zFullname);
      }else{
        blob_read_from_file(&current, zFullname);
      }
      new_exe = file_wd_isexe(0);
    }else{
      blob_zero(&current);
      new_exe = 0;
    }
    blob_zero(&new);
    old_exists = db_column_int(&q, 1);
    old_exe = db_column_int(&q, 2);
    if( old_exists ){
      db_ephemeral_blob(&q, 0, &new);
    }
    if( old_exists ){
      if( new_exists ){
        fossil_print("%s   %s\n", redoFlag ? "REDO" : "UNDO", zPathname);
      }else{
        fossil_print("NEW    %s\n", zPathname);
      }
      if( new_exists && (new_link || old_link) ){
        file_delete(zFullname);
      }
      if( old_link ){
        symlink_create(blob_str(&new), zFullname);
      }else{
        blob_write_to_file(&new, zFullname);
      }
      file_wd_setexe(zFullname, old_exe);
    }else{
      fossil_print("DELETE %s\n", zPathname);
      file_delete(zFullname);
    }
    blob_reset(&new);
    free(zFullname);
    db_finalize(&q);
    db_prepare(&q,
       "UPDATE undo SET content=:c, existsflag=%d, isExe=%d, isLink=%d,"
             " redoflag=NOT redoflag"
       " WHERE pathname=%Q",
       new_exists, new_exe, new_link, zPathname
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
  if( db_table_exists("localdb", "undo_stash") ){
    if( redoFlag ){
      db_multi_exec(
        "DELETE FROM stash WHERE stashid IN (SELECT stashid FROM undo_stash);"
        "DELETE FROM stashfile"
        " WHERE stashid NOT IN (SELECT stashid FROM stash);"
      );
    }else{
      db_multi_exec(
        "INSERT OR IGNORE INTO stash SELECT * FROM undo_stash;"
        "INSERT OR IGNORE INTO stashfile SELECT * FROM undo_stashfile;"
      );
    }
  }
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
    @ DROP TABLE IF EXISTS undo_stash;
    @ DROP TABLE IF EXISTS undo_stashfile;
    ;
  db_multi_exec(zSql /*works-like:""*/);
  db_lset_int("undo_available", 0);
  db_lset_int("undo_checkout", 0);
}

/*
** The following variable stores the original command-line of the
** command that is a candidate to be undone.
*/
static char *undoCmd = 0;

/*
** This flag is true if we are in the process of collecting file changes
** for undo.  When this flag is false, undo_save() is a no-op.
**
** The undoDisable flag, if set, prevents undo from being activated.
*/
static int undoActive = 0;
static int undoDisable = 0;


/*
** Capture the current command-line and store it as part of the undo
** state.  This routine is called before options are extracted from the
** command-line so that we can record the complete command-line.
*/
void undo_capture_command_line(void){
  Blob cmdline;
  int i;
  if( undoCmd!=0 || undoDisable ) return;
  blob_zero(&cmdline);
  for(i=1; i<g.argc; i++){
    if( i>1 ) blob_append(&cmdline, " ", 1);
    blob_append(&cmdline, g.argv[i], -1);
  }
  undoCmd = blob_str(&cmdline);
}

/*
** Begin capturing a snapshot that can be undone.
*/
void undo_begin(void){
  int cid;
  static const char zSql[] =
    @ CREATE TABLE localdb.undo(
    @   pathname TEXT UNIQUE,             -- Name of the file
    @   redoflag BOOLEAN,                 -- 0 for undoable.  1 for redoable
    @   existsflag BOOLEAN,               -- True if the file exists
    @   isExe BOOLEAN,                    -- True if the file is executable
    @   isLink BOOLEAN,                   -- True if the file is symlink
    @   content BLOB                      -- Saved content
    @ );
    @ CREATE TABLE localdb.undo_vfile AS SELECT * FROM vfile;
    @ CREATE TABLE localdb.undo_vmerge AS SELECT * FROM vmerge;
  ;
  if( undoDisable ) return;
  undo_reset();
  db_multi_exec(zSql/*works-like:""*/);
  cid = db_lget_int("checkout", 0);
  db_lset_int("undo_checkout", cid);
  db_lset_int("undo_available", 1);
  db_lset("undo_cmdline", undoCmd);
  undoActive = 1;
}

/*
** Permanently disable undo
*/
void undo_disable(void){
  undoDisable = 1;
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
  if( undoDisable ) return;
  if( undo_maybe_save(zPathname, -1)!=UNDO_SAVED_OK ){
    fossil_panic("failed to save undo information for path: %s",
                 zPathname);
  }
}

/*
** Possibly save the current content of the file zPathname so
** that it will be undoable.  The name is relative to the root
** of the tree.  The limit argument may be used to specify the
** maximum size for the file to be saved.  If the size of the
** specified file exceeds this size limit (in bytes), it will
** not be saved and an appropriate code will be returned.
**
** WARNING: Please do NOT call this function with a limit
**          value less than zero, call the undo_save()
**          function instead.
**
** The return value of this function will always be one of the
** following codes:
**
** UNDO_SAVED_OK: The specified file was saved succesfully.
**
** UNDO_DISABLED: The specified file was NOT saved, because the
**                "undo subsystem" is disabled.  This error may
**                indicate that a call to undo_disable() was
**                issued.
**
** UNDO_INACTIVE: The specified file was NOT saved, because the
**                "undo subsystem" is not active.  This error
**                may indicate that a call to undo_begin() is
**                missing.
**
**   UNDO_TOOBIG: The specified file was NOT saved, because it
**                exceeded the specified size limit.  It is
**                impossible for this value to be returned if
**                the specified size limit is less than zero
**                (i.e. unlimited).
*/
int undo_maybe_save(const char *zPathname, i64 limit){
  char *zFullname;
  i64 size;
  int result;

  if( undoDisable ) return UNDO_DISABLED;
  if( !undoActive ) return UNDO_INACTIVE;
  zFullname = mprintf("%s%s", g.zLocalRoot, zPathname);
  size = file_wd_size(zFullname);
  if( limit<0 || size<=limit ){
    int existsFlag = (size>=0);
    int isLink = file_wd_islink(zFullname);
    Stmt q;
    Blob content;
    db_prepare(&q,
      "INSERT OR IGNORE INTO"
      "   undo(pathname,redoflag,existsflag,isExe,isLink,content)"
      " VALUES(%Q,0,%d,%d,%d,:c)",
      zPathname, existsFlag, file_wd_isexe(zFullname), isLink
    );
    if( existsFlag ){
      if( isLink ){
        blob_read_link(&content, zFullname);
      }else{
        blob_read_from_file(&content, zFullname);
      }
      db_bind_blob(&q, ":c", &content);
    }
    db_step(&q);
    db_finalize(&q);
    if( existsFlag ){
      blob_reset(&content);
    }
    undoNeedRollback = 1;
    result = UNDO_SAVED_OK;
  }else{
    result = UNDO_TOOBIG;
  }
  free(zFullname);
  return result;
}

/*
** Returns an error message for the undo_maybe_save() return code.
** Currently, this function assumes that the caller is using the
** returned error message in a context prefixed with "because".
*/
const char *undo_save_message(int rc){
  static char zRc[32];

  switch( rc ){
    case UNDO_NONE:     return "undo is disabled for this operation";
    case UNDO_SAVED_OK: return "the save operation was successful";
    case UNDO_DISABLED: return "the undo subsystem is disabled";
    case UNDO_INACTIVE: return "the undo subsystem is inactive";
    case UNDO_TOOBIG:   return "the file is too big";
    default: {
      sqlite3_snprintf(sizeof(zRc), zRc, "of error code %d", rc);
    }
  }
  return zRc;
}

/*
** Make the current state of stashid undoable.
*/
void undo_save_stash(int stashid){
  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS localdb.undo_stash"
    "  AS SELECT * FROM stash WHERE 0;"
    "INSERT INTO undo_stash"
    " SELECT * FROM stash WHERE stashid=%d;",
    stashid
  );
  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS localdb.undo_stashfile"
    "  AS SELECT * FROM stashfile WHERE 0;"
    "INSERT INTO undo_stashfile"
    " SELECT * FROM stashfile WHERE stashid=%d;",
    stashid
  );
}

/*
** Complete the undo process is one is currently in process.
*/
void undo_finish(void){
  if( undoActive ){
    if( undoNeedRollback ){
      fossil_print(" \"fossil undo\" is available to undo changes"
             " to the working checkout.\n");
    }
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
  fossil_print("Rolling back prior filesystem changes...\n");
  undo_all_filesystem(0);
}

/*
** COMMAND: undo
** COMMAND: redo*
**
** Usage: %fossil undo ?OPTIONS? ?FILENAME...?
**    or: %fossil redo ?OPTIONS? ?FILENAME...?
**
** Undo the changes to the working checkout caused by the most recent
** of the following operations:
**
**    (1) fossil update             (5) fossil stash apply
**    (2) fossil merge              (6) fossil stash drop
**    (3) fossil revert             (7) fossil stash goto
**    (4) fossil stash pop
**
** The "fossil clean" operation can also be undone; however, this is
** currently limited to files that are less than 10MiB in size.
**
** If FILENAME is specified then restore the content of the named
** file(s) but otherwise leave the update or merge or revert in effect.
** The redo command undoes the effect of the most recent undo.
**
** If the -n|--dry-run option is present, no changes are made and instead
** the undo or redo command explains what actions the undo or redo would
** have done had the -n|--dry-run been omitted.
**
** A single level of undo/redo is supported.  The undo/redo stack
** is cleared by the commit and checkout commands.
**
** Options:
**   -n|--dry-run   do not make changes but show what would be done
**
** See also: commit, status
*/
void undo_cmd(void){
  int isRedo = g.argv[1][0]=='r';
  int undo_available;
  int dryRunFlag = find_option("dry-run", "n", 0)!=0;
  const char *zCmd = isRedo ? "redo" : "undo";

  if( !dryRunFlag ){
    dryRunFlag = find_option("explain", 0, 0)!=0;
  }
  db_must_be_within_tree();
  verify_all_options();
  db_begin_transaction();
  undo_available = db_lget_int("undo_available", 0);
  if( dryRunFlag ){
    if( undo_available==0 ){
      fossil_print("No undo or redo is available\n");
    }else{
      Stmt q;
      int nChng = 0;
      const char *zArticle = undo_available==1 ? "An" : "A";
      zCmd = undo_available==1 ? "undo" : "redo";
      fossil_print("%s %s is available for the following command:\n\n"
                   "   %s %s\n\n",
                   zArticle, zCmd, g.argv[0], db_lget("undo_cmdline", "???"));
      db_prepare(&q,
        "SELECT existsflag, pathname FROM undo ORDER BY pathname"
      );
      while( db_step(&q)==SQLITE_ROW ){
        if( nChng==0 ){
          fossil_print("The following file changes would occur if the "
                       "command above is %sne:\n\n", zCmd);
        }
        nChng++;
        fossil_print("%s %s\n",
           db_column_int(&q,0) ? "UPDATE" : "DELETE",
           db_column_text(&q, 1)
        );
      }
      db_finalize(&q);
      if( nChng==0 ){
        fossil_print("No file changes would occur with this undo/redo.\n");
      }
    }
  }else{
    int vid1 = db_lget_int("checkout", 0);
    int vid2;
    if( g.argc==2 ){
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
        file_tree_name(zFile, &path, 0, 1);
        undo_one(blob_str(&path), isRedo);
        blob_reset(&path);
      }
    }
    vid2 = db_lget_int("checkout", 0);
    if( vid1!=vid2 ){
      fossil_print("--------------------\n");
      show_common_info(vid2, "updated-to:", 1, 0);
    }
  }
  db_end_transaction(0);
}
