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
**   drh@sqlite.org
**
*******************************************************************************
**
** This file contains code used to implement the "stash" command.
*/
#include "config.h"
#include "stash.h"
#include <assert.h>


/*
** SQL code to implement the tables needed by the stash.
*/
static const char zStashInit[] = 
@ CREATE TABLE IF NOT EXISTS %s.stash(
@   stashid INTEGER PRIMARY KEY,     -- Unique stash identifier
@   vid INTEGER,                     -- The baseline check-out for this stash
@   comment TEXT,                    -- Comment for this stash.  Or NULL
@   ctime TIMESTAMP                  -- When the stash was created
@ );
@ CREATE TABLE IF NOT EXISTS %s.stashfile(
@   stashid INTEGER REFERENCES stash,  -- Stash that contains this file
@   rid INTEGER,                       -- Baseline content in BLOB table or 0.
@   isAdded BOOLEAN,                   -- True if this is an added file
@   isRemoved BOOLEAN,                 -- True if this file is deleted
@   isExec BOOLEAN,                    -- True if file is executable
@   origname TEXT,                     -- Original filename
@   newname TEXT,                      -- New name for file at next check-in
@   delta BLOB,                        -- Delta from baseline. Content if rid=0
@   PRIMARY KEY(origname, stashid)
@ );
@ INSERT OR IGNORE INTO vvar(name, value) VALUES('stash-next', 1);
;

/*
** Add zFName to the stash given by stashid.  zFName might be the name of a
** file or a directory.  If a directory, add all changed files contained
** within that directory.
*/
static void stash_add_file_or_dir(int stashid, int vid, const char *zFName){
  char *zFile;          /* Normalized filename */
  char *zTreename;      /* Name of the file in the tree */
  Blob fname;           /* Filename relative to root */
  Blob sql;             /* Query statement text */
  Stmt q;               /* Query against the vfile table */
  Stmt ins;             /* Insert statement */

  zFile = mprintf("%/", zFName);
  file_tree_name(zFile, &fname, 1);
  zTreename = blob_str(&fname);
  blob_zero(&sql);
  blob_appendf(&sql,
    "SELECT deleted, isexe, mrid, pathname, coalesce(origname,pathname)"
    "  FROM vfile"
    " WHERE vid=%d AND (chnged OR deleted OR origname NOT NULL OR mrid==0)",
    vid
  );
  if( strcmp(zTreename,".")!=0 ){
    blob_appendf(&sql,
      "   AND (pathname GLOB '%q/*' OR origname GLOB '%q/*'"
            "  OR pathname=%Q OR origname=%Q)",
      zTreename, zTreename, zTreename, zTreename
    );
  }
  db_prepare(&q, blob_str(&sql));
  blob_reset(&sql);
  db_prepare(&ins,
     "INSERT INTO stashfile(stashid, rid, isAdded, isRemoved, isExec,"
                           "origname, newname, delta)"
     "VALUES(%d,:rid,:isadd,:isrm,:isexe,:orig,:new,:content)",
     stashid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int deleted = db_column_int(&q, 0);
    int rid = db_column_int(&q, 2);
    const char *zName = db_column_text(&q, 3);
    const char *zOrig = db_column_text(&q, 4);
    char *zPath = mprintf("%s/%s", g.zLocalRoot, zName);
    Blob content;

    db_bind_int(&ins, ":rid", rid);
    db_bind_int(&ins, ":isadd", rid==0);
    db_bind_int(&ins, ":isrm", deleted);
#ifdef _WIN32
    db_bind_int(&ins, ":isexe", db_column_int(&q, 2));
#endif
    db_bind_text(&ins, ":orig", zOrig);
    db_bind_text(&ins, ":new", zName);
    if( rid==0 ){
      /* A new file */
      blob_read_from_file(&content, zPath);
      db_bind_blob(&ins, ":content", &content);
    }else if( deleted ){
      db_bind_null(&ins, ":content");
    }else{
      /* A modified file */
      Blob orig;
      Blob disk;
      blob_read_from_file(&disk, zPath);
      content_get(rid, &orig);
      blob_delta_create(&orig, &disk, &content);
      blob_reset(&orig);
      blob_reset(&disk);
      db_bind_blob(&ins, ":content", &content);
    }
    db_step(&ins);
    db_reset(&ins);
    fossil_free(zPath);
    blob_reset(&content);
  }
  db_finalize(&ins);
  db_finalize(&q);
  fossil_free(zFile);
  blob_reset(&fname);
}

/*
** Create a new stash based on the uncommitted changes currently in
** the working directory.
**
** If the "-m" or "--comment" command-line option is present, gather
** its argument as the stash comment.
**
** If files are named on the command-line, then only stash the named
** files.
*/
static void stash_create(void){
  const char *zComment;              /* Comment to add to the stash */
  int stashid;                       /* ID of the new stash */
  int vid;                           /* Current checkout */

  zComment = find_option("comment", "m", 1);
  verify_all_options();
  stashid = db_lget_int("stash-next", 1);
  db_lset_int("stash-next", stashid+1);
  vid = db_lget_int("checkout", 0);
  db_multi_exec(
    "INSERT INTO stash(stashid,vid,comment,ctime)"
    "VALUES(%d,%d,%Q,julianday('now'))",
    stashid, vid, zComment
  );
  if( g.argc>3 ){
    int i;
    for(i=3; i<g.argc; i++){
      stash_add_file_or_dir(stashid, vid, g.argv[i]);
    }
  }else{
    stash_add_file_or_dir(stashid, vid, ".");
  } 
}

/*
** Apply a stash to the current check-out.
*/
static void stash_apply(int stashid, int nConflict){
  Stmt q;
  db_prepare(&q,
     "SELECT rid, isRemoved, isExec, origname, newname, delta"
     "  FROM stashfile WHERE stashid=%d",
     stashid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    int isRemoved = db_column_int(&q, 1);
    const char *zOrig = db_column_text(&q, 3);
    const char *zNew = db_column_text(&q, 4);
    char *zOPath = mprintf("%s/%s", g.zLocalRoot, zOrig);
    char *zNPath = mprintf("%s/%s", g.zLocalRoot, zNew);
    undo_save(zNPath);
    Blob delta;
    if( rid==0 ){
      db_ephemeral_blob(&q, 5, &delta);
      blob_write_to_file(&delta, zNPath);
      printf("ADD %s\n", zNew);
    }else if( isRemoved ){
      printf("DELETE %s\n", zOrig);
      unlink(zOPath);
    }else{
      Blob a, b, out, disk;
      db_ephemeral_blob(&q, 5, &delta);
      blob_read_from_file(&disk, zOPath);     
      content_get(rid, &a);
      blob_delta_apply(&a, &delta, &b);
      if( blob_compare(&disk, &a)==0 ){
        blob_write_to_file(&b, zNPath);
        printf("UPDATE %s\n", zNew);
      }else{
        int rc = blob_merge(&a, &disk, &b, &out);
        blob_write_to_file(&out, zNPath);
        if( rc ){
          printf("CONFLICT %s\n", zNew);
          nConflict++;
        }else{
          printf("MERGE %s\n", zNew);
        }
        blob_reset(&out);
      }
      blob_reset(&a);
      blob_reset(&b);
      blob_reset(&delta);
    }
    if( strcmp(zOrig,zNew)!=0 ){
      undo_save(zOPath);
      unlink(zOPath);
    }
  }
  db_finalize(&q);
  if( nConflict ){
    printf("WARNING: merge conflicts - see messages above for details.\n");
  }
}

/*
** Drop the indicates stash
*/
static void stash_drop(int stashid){
  db_multi_exec(
    "DELETE FROM stash WHERE stashid=%d;"
    "DELETE FROM stashfile WHERE stashid=%d;",
    stashid, stashid
  );
}

/*
** If zStashId is non-NULL then interpret is as a stash number and
** return that number.  Or throw a fatal error if it is not a valid
** stash number.  If it is NULL, return the most recent stash or
** throw an error if the stash is empty.
*/
static int stash_get_id(const char *zStashId){
  int stashid = 0;
  if( zStashId==0 ){
    stashid = db_int(0, "SELECT max(stashid) FROM stash");
    if( stashid==0 ) fossil_fatal("empty stash");
  }else{
    stashid = atoi(zStashId);
    if( !db_exists("SELECT 1 FROM stash WHERE stashid=%d", stashid) ){
      fossil_fatal("no such stash: %d\n", stashid);
    }
  }
  return stashid;
}

/*
** COMMAND: stash
**
** Usage: %fossil COMMAND ARGS...
**
**    fossil stash
**    fossil stash save [-m COMMENT] [FILES...]
**
**         Save the current changes in the working tree as a new stash.
**         Then revert the changes back to the last check-in.  If FILES
**         are listed, then only stash and revert the named files.  The
**         "save" verb can be omitted if and only if there are no other
**         arguments.
**
**    fossil stash list
**
**         List all changes sets currently stashed.
**
**    fossil stash pop
**
**         Apply the most recently create stash to the current working
**         check-out.  Then delete that stash.  This is equivalent to
**         doing an "apply" and a "drop" against the most recent stash.
**
**    fossil stash apply STASHID
**
**         Apply the identified stash to the current working check-out.
**         But unlike "pop", keep the stash so that it can be used again.
**
**    fossil stash goto STASHID
**
**         Update to the baseline checkout for STASHID then apply the
**         changes of STASHID.  Keep STASHID so that it can be reused
**
**    fossil drop STASHID
**
**         Forget everything about STASHID.
**
**    fossil stash snapshot [-m COMMENT] [FILES...]
**
**         Save the current changes in the working tress as a new stash
**         but, unlike "save", do not revert those changes.
*/
void stash_cmd(void){
  const char *zDb = "localdb";
  const char *zCmd;
  int nCmd;
  undo_capture_command_line();
  db_must_be_within_tree();
  db_begin_transaction();
  if( strcmp(g.zMainDbType, zDb)==0 ) zDb = "main";
  db_multi_exec(zStashInit, zDb, zDb);
  if( g.argc<=2 ){
    zCmd = "save";
  }else{
    zCmd = g.argv[2];
  }
  nCmd = strlen(zCmd);
  if( memcmp(zCmd, "save", nCmd)==0 ){
    stash_create();
    undo_disable();
    g.argc = 2;
    revert_cmd();
  }else
  if( memcmp(zCmd, "snapshot", nCmd)==0 ){
    stash_create();
  }else
  if( memcmp(zCmd, "list", nCmd)==0 ){
    Stmt q;
    int n = 0;
    verify_all_options();
    db_prepare(&q,
       "SELECT stashid, (SELECT uuid FROM blob WHERE rid=vid),"
       "       comment, datetime(ctime) FROM stash"
       " ORDER BY ctime DESC"
    );
    while( db_step(&q)==SQLITE_ROW ){
      n++;
      const char *zCom;
      printf("%5d: [%.14s] on %s\n",
        db_column_int(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 3)
      );
      zCom = db_column_text(&q, 2);
      if( zCom && zCom[0] ){
        printf("       ");
        comment_print(zCom, 7, 79);
      }
    }
    db_finalize(&q);
    if( n==0 ) printf("empty stash\n");
  }else
  if( memcmp(zCmd, "drop", nCmd)==0 ){
    if( g.argc>4 ) usage("stash apply STASHID");
    int stashid = stash_get_id(g.argc==4 ? g.argv[3] : 0);
    stash_drop(stashid);
  }else
  if( memcmp(zCmd, "pop", nCmd)==0 ){
    if( g.argc>3 ) usage("stash pop");
    int stashid = stash_get_id(0);
    undo_begin();
    stash_apply(stashid, 0);
    undo_finish();
    stash_drop(stashid);
  }else
  if( memcmp(zCmd, "apply", nCmd)==0 ){
    if( g.argc>4 ) usage("stash apply STASHID");
    int stashid = stash_get_id(g.argc==4 ? g.argv[3] : 0);
    undo_begin();
    stash_apply(stashid, 0);
    undo_finish();
  }else
  if( memcmp(zCmd, "goto", nCmd)==0 ){
    if( g.argc>4 ) usage("stash apply STASHID");
    int stashid = stash_get_id(g.argc==4 ? g.argv[3] : 0);
    undo_begin();
    update_to(db_int(0, "SELECT vid FROM stash WHERE stashid=%d", stashid));
    stash_apply(stashid, 0);
    undo_finish();
  }else
  {
    usage("apply|drop|goto|list|pop|save|snapshot ARGS...");
  }
  db_end_transaction(0);
}
