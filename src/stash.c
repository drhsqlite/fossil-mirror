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
**
** Historical schema changes:
**
**   2019-01-19:   stash.hash and stashfile.hash columns added.  The
**                 corresponding stash.vid and stashfile.rid columns are
**                 retained for compatibility with older versions of
**                 fossil but are no longer used.
**
**   2016-10-16:   Change the PRIMARY KEY on stashfile from (origname,stashid)
**                 to (newname,stashid).
**
**   2011-09-01:   stashfile.isLink column added
**
*/
static const char zStashInit[] =
@ CREATE TABLE IF NOT EXISTS localdb.stash(
@   stashid INTEGER PRIMARY KEY,     -- Unique stash identifier
@   vid INTEGER,                     -- Legacy baseline RID value. Do not use.
@   hash TEXT,                       -- The SHA hash for the baseline
@   comment TEXT,                    -- Comment for this stash.  Or NULL
@   ctime TIMESTAMP                  -- When the stash was created
@ );
@ CREATE TABLE IF NOT EXISTS localdb.stashfile(
@   stashid INTEGER REFERENCES stash,  -- Stash that contains this file
@   isAdded BOOLEAN,                   -- True if this is an added file
@   isRemoved BOOLEAN,                 -- True if this file is deleted
@   isExec BOOLEAN,                    -- True if file is executable
@   isLink BOOLEAN,                    -- True if file is a symlink
@   rid INTEGER,                       -- Legacy baseline RID value. Do not use
@   hash TEXT,                         -- Hash for baseline or NULL
@   origname TEXT,                     -- Original filename
@   newname TEXT,                      -- New name for file at next check-in
@   delta BLOB,                        -- Delta from baseline or raw content
@   PRIMARY KEY(newname, stashid)
@ );
@ INSERT OR IGNORE INTO vvar(name, value) VALUES('stash-next', 1);
;

/*
** Make sure the stash and stashfile tables exist and have been
** upgraded to their latest format.  Create and upgrade the tables
** as necessary.
*/
static void stash_tables_exist_and_current(void){
  if( db_table_has_column("localdb","stashfile","hash") ){
    /* The schema is up-to-date.  But it could be that an older version
    ** of Fossil that does no know about the stash.hash and stashfile.hash
    ** columns has run since the schema was updated, and added entries that
    ** have NULL hash columns.  Check for this case, and fill in any missing
    ** hash values.
    */
    if( db_int(0, "SELECT hash IS NULL FROM stash"
                  " ORDER BY stashid DESC LIMIT 1")
    ){
      db_multi_exec(
        "UPDATE stash"
        "   SET hash=(SELECT uuid FROM blob WHERE blob.rid=stash.vid)"
        " WHERE hash IS NULL;"
        "UPDATE stashfile"
        "   SET hash=(SELECT uuid FROM blob WHERE blob.rid=stashfile.rid)"
        " WHERE hash IS NULL AND rid>0;"
      );
    }
    return;
  }

  if( !db_table_exists("localdb","stashfile")
   || !db_table_exists("localdb","stash")
  ){
    /* Tables do not exist.  Create them from scratch. */
    db_multi_exec("DROP TABLE IF EXISTS localdb.stash;");
    db_multi_exec("DROP TABLE IF EXISTS localdb.stashfile;");
    db_multi_exec(zStashInit /*works-like:""*/);
    return;
  }

  /* The tables exists but are not necessarily current.  Upgrade them
  ** to the latest format.
  **
  ** We can assume the 2011-09-01 format that includes the stashfile.isLink
  ** column.  The only upgrades we need to worry about the PRIMARY KEY
  ** change on 2016-10-16 and the addition of the "hash" columns on
  ** 2019-01-19.
  */
  db_multi_exec(
    "ALTER TABLE localdb.stash RENAME TO old_stash;"
    "ALTER TABLE localdb.stashfile RENAME TO old_stashfile;"
  );
  db_multi_exec(zStashInit /*works-like:""*/);
  db_multi_exec(
    "INSERT INTO localdb.stash(stashid,vid,hash,comment,ctime)"
    " SELECT stashid, vid,"
    "   (SELECT uuid FROM blob WHERE blob.rid=old_stash.vid),"
    "   comment, ctime FROM old_stash;"
    "DROP TABLE old_stash;"
  );
  db_multi_exec(
    "INSERT INTO localdb.stashfile(stashid,isAdded,isRemoved,isExec,"
                                  "isLink,rid,hash,origname,newname,delta)"
    " SELECT stashid, isAdded, isRemoved, isExec, isLink, rid,"
    "   (SELECT uuid FROM blob WHERE blob.rid=old_stashfile.rid),"
    "   origname, newname, delta FROM old_stashfile;"
    "DROP TABLE old_stashfile;"
  );
}

/*
** Update the stash.vid and stashfile.rid values after a RID renumbering
** event.
*/
void stash_rid_renumbering_event(void){
  if( !db_table_has_column("localdb","stash","hash") ){
    /* If the stash schema was the older style that lacked hash value, then
    ** recovery is not possible.  Save off the old data, then reset the stash
    ** to empty. */
    if( db_table_exists("localdb","stash") ){
      db_multi_exec("ALTER TABLE stash RENAME TO broken_stash;");
      fossil_print("Unrecoverable stash content stored in \"broken_stash\"\n");
    }
    if( db_table_exists("localdb","stashfile") ){
      db_multi_exec("ALTER TABLE stashfile RENAME TO broken_stashfile;");
      fossil_print("Unrecoverable stashfile content stored"
                   " in \"broken_stashfile\"\n");
    }
  }else{
    /* Reset stash.vid and stash.rid values based on hashes */
    db_multi_exec(
      "UPDATE stash"
      "   SET vid=(SELECT rid FROM blob WHERE blob.uuid=stash.hash);"
      "UPDATE stashfile"
      "   SET rid=(SELECT rid FROM blob WHERE blob.uuid=stashfile.hash)"
      " WHERE hash IS NOT NULL;"
    );
  }
}

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
  file_tree_name(zFile, &fname, 0, 1);
  zTreename = blob_str(&fname);
  blob_zero(&sql);
  blob_append_sql(&sql,
    "SELECT deleted, isexe, islink, mrid, pathname, coalesce(origname,pathname)"
    "  FROM vfile"
    " WHERE vid=%d AND (chnged OR deleted OR origname NOT NULL OR mrid==0)",
    vid
  );
  if( fossil_strcmp(zTreename,".")!=0 ){
    blob_append_sql(&sql,
      "   AND (pathname GLOB '%q/*' OR origname GLOB '%q/*'"
            "  OR pathname=%Q OR origname=%Q)",
      zTreename, zTreename, zTreename, zTreename
    );
  }
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  db_prepare(&ins,
     "INSERT INTO stashfile(stashid, isAdded, isRemoved, isExec, isLink, rid, "
                           "hash, origname, newname, delta)"
     "VALUES(%d,:isadd,:isrm,:isexe,:islink,:rid,"
     "(SELECT uuid FROM blob WHERE rid=:rid),:orig,:new,:content)",
     stashid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int deleted = db_column_int(&q, 0);
    int rid = db_column_int(&q, 3);
    const char *zName = db_column_text(&q, 4);
    const char *zOrig = db_column_text(&q, 5);
    char *zPath = mprintf("%s%s", g.zLocalRoot, zName);
    Blob content;

    db_bind_int(&ins, ":rid", rid);
    db_bind_int(&ins, ":isadd", rid==0);
    db_bind_int(&ins, ":isrm", deleted);
    db_bind_int(&ins, ":isexe", db_column_int(&q, 1));
    db_bind_int(&ins, ":islink", db_column_int(&q, 2));
    db_bind_text(&ins, ":orig", zOrig);
    db_bind_text(&ins, ":new", zName);

    if( rid==0 ){
      /* A new file */
      blob_read_from_file(&content, zPath, RepoFILE);
      db_bind_blob(&ins, ":content", &content);
    }else if( deleted ){
      blob_zero(&content);
      db_bind_null(&ins, ":content");
    }else{
      /* A modified file */
      Blob orig;
      Blob disk;

      blob_read_from_file(&disk, zPath, RepoFILE);
      content_get(rid, &orig);
      blob_delta_create(&orig, &disk, &content);
      blob_reset(&orig);
      blob_reset(&disk);
      db_bind_blob(&ins, ":content", &content);
    }
    db_bind_int(&ins, ":islink", file_islink(zPath));
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
static int stash_create(void){
  const char *zComment;              /* Comment to add to the stash */
  int stashid;                       /* ID of the new stash */
  int vid;                           /* Current check-out */

  zComment = find_option("comment", "m", 1);
  (void)fossil_text_editor();
  verify_all_options();
  if( zComment==0 ){
    Blob prompt;                       /* Prompt for stash comment */
    Blob comment;                      /* User comment reply */
#if defined(_WIN32) || defined(__CYGWIN__)
    int bomSize;
    const unsigned char *bom = get_utf8_bom(&bomSize);
    blob_init(&prompt, (const char *) bom, bomSize);
#else
    blob_zero(&prompt);
#endif
    blob_append(&prompt,
       "\n"
       "# Enter a description of what is being stashed.  Lines beginning\n"
       "# with \"#\" are ignored.  Stash comments are plain text except\n"
       "# newlines are not preserved.\n",
       -1);
    prompt_for_user_comment(&comment, &prompt);
    blob_reset(&prompt);
    zComment = blob_str(&comment);
  }
  stashid = db_lget_int("stash-next", 1);
  db_lset_int("stash-next", stashid+1);
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid, 0);
  db_multi_exec(
    "INSERT INTO stash(stashid,vid,hash,comment,ctime)"
    "VALUES(%d,%d,(SELECT uuid FROM blob WHERE rid=%d),%Q,julianday('now'))",
    stashid, vid, vid, zComment
  );
  if( g.argc>3 ){
    int i;
    for(i=3; i<g.argc; i++){
      stash_add_file_or_dir(stashid, vid, g.argv[i]);
    }
  }else{
    stash_add_file_or_dir(stashid, vid, g.zLocalRoot);
  }
  return stashid;
}

/*
** Apply a stash to the current check-out.
*/
static void stash_apply(int stashid, int nConflict){
  int vid;
  Stmt q;
  db_prepare(&q,
     "SELECT blob.rid, isRemoved, isExec, isLink, origname, newname, delta"
     "  FROM stashfile, blob WHERE stashid=%d AND blob.uuid=stashfile.hash"
     " UNION ALL SELECT 0, isRemoved, isExec, isLink, origname, newname, delta"
     "  FROM stashfile WHERE stashid=%d AND stashfile.hash IS NULL",
     stashid, stashid
  );
  vid = db_lget_int("checkout",0);
  db_multi_exec("CREATE TEMP TABLE sfile(pathname TEXT PRIMARY KEY %s)",
                filename_collation());
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    int isRemoved = db_column_int(&q, 1);
    int isExec = db_column_int(&q, 2);
    int isLink = db_column_int(&q, 3);
    const char *zOrig = db_column_text(&q, 4);
    const char *zNew = db_column_text(&q, 5);
    char *zOPath = mprintf("%s%s", g.zLocalRoot, zOrig);
    char *zNPath = mprintf("%s%s", g.zLocalRoot, zNew);
    Blob delta;
    undo_save(zNew);
    blob_zero(&delta);
    if( rid==0 ){
      db_multi_exec("INSERT OR IGNORE INTO sfile(pathname) VALUES(%Q)", zNew);
      db_ephemeral_blob(&q, 6, &delta);
      blob_write_to_file(&delta, zNPath);
      file_setexe(zNPath, isExec);
    }else if( isRemoved ){
      fossil_print("DELETE %s\n", zOrig);
      file_delete(zOPath);
    }else if( file_unsafe_in_tree_path(zNPath) ){
      /* Ignore the unsafe path */
    }else{
      Blob a, b, out, disk;
      int isNewLink = file_islink(zOPath);
      db_ephemeral_blob(&q, 6, &delta);
      blob_read_from_file(&disk, zOPath, RepoFILE);
      content_get(rid, &a);
      blob_delta_apply(&a, &delta, &b);
      if( isLink == isNewLink && blob_compare(&disk, &a)==0 ){
        if( isLink || isNewLink ){
          file_delete(zNPath);
        }
        if( isLink ){
          symlink_create(blob_str(&b), zNPath);
        }else{
          blob_write_to_file(&b, zNPath);
        }
        file_setexe(zNPath, isExec);
        fossil_print("UPDATE %s\n", zNew);
      }else{
        int rc;
        if( isLink || isNewLink ){
          rc = -1;
          blob_zero(&b); /* because we reset it later */
          fossil_print("***** Cannot merge symlink %s\n", zNew);
        }else{
          rc = merge_3way(&a, zOPath, &b, &out, MERGE_KEEP_FILES);
          blob_write_to_file(&out, zNPath);
          blob_reset(&out);
          file_setexe(zNPath, isExec);
        }
        if( rc ){
          fossil_print("CONFLICT %s\n", zNew);
          nConflict++;
        }else{
          fossil_print("MERGE %s\n", zNew);
        }
      }
      blob_reset(&a);
      blob_reset(&b);
      blob_reset(&disk);
    }
    blob_reset(&delta);
    if( fossil_strcmp(zOrig,zNew)!=0 ){
      undo_save(zOrig);
      file_delete(zOPath);
      db_multi_exec(
        "UPDATE vfile SET pathname='%q', origname='%q'"
        " WHERE pathname='%q' %s AND vid=%d",
        zNew, zOrig, zOrig, filename_collation(), vid
      );
    }
  }
  stash_add_files_in_sfile(vid);
  db_finalize(&q);
  if( nConflict ){
    fossil_print(
      "WARNING: %d merge conflicts - see messages above for details.\n",
      nConflict);
  }
}

/*
** Show the diffs associate with a single stash.
*/
static void stash_diff(
  int stashid,             /* The stash entry to diff */
  int fBaseline,           /* Diff against original baseline check-in if true */
  DiffConfig *pCfg         /* Diff formatting options */
){
  Stmt q;
  Blob empty;
  int bWebpage = (pCfg->diffFlags & (DIFF_WEBPAGE|DIFF_JSON|DIFF_TCL))!=0;
  blob_zero(&empty);
  diff_begin(pCfg);
  db_prepare(&q,
     "SELECT blob.rid, isRemoved, isExec, isLink, origname, newname, delta"
     "  FROM stashfile, blob WHERE stashid=%d AND blob.uuid=stashfile.hash"
     " UNION ALL SELECT 0, isRemoved, isExec, isLink, origname, newname, delta"
     "  FROM stashfile WHERE stashid=%d AND stashfile.hash IS NULL",
     stashid, stashid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    int isRemoved = db_column_int(&q, 1);
    int isLink = db_column_int(&q, 3);
    const char *zOrig = db_column_text(&q, 4);
    const char *zNew = db_column_text(&q, 5);
    char *zOPath = mprintf("%s%s", g.zLocalRoot, zOrig);
    Blob a, b;
    pCfg->diffFlags &= (~DIFF_FILE_MASK);
    if( rid==0 ){
      db_ephemeral_blob(&q, 6, &a);
      if( !bWebpage ) fossil_print("ADDED %s\n", zNew);
      pCfg->diffFlags |= DIFF_FILE_ADDED;
      diff_print_index(zNew, pCfg, 0);
      diff_file_mem(&empty, &a, zNew, pCfg);
    }else if( isRemoved ){
      if( !bWebpage) fossil_print("DELETE %s\n", zOrig);
      pCfg->diffFlags |= DIFF_FILE_DELETED;
      diff_print_index(zNew, pCfg, 0);
      if( fBaseline ){
        content_get(rid, &a);
        diff_file_mem(&a, &empty, zOrig, pCfg);
      }
    }else{
      Blob delta;
      int isOrigLink = file_islink(zOPath);
      db_ephemeral_blob(&q, 6, &delta);
      if( !bWebpage ) fossil_print("CHANGED %s\n", zNew);
      if( !isOrigLink != !isLink ){
        diff_print_index(zNew, pCfg, 0);
        diff_print_filenames(zOrig, zNew, pCfg, 0);
        printf(DIFF_CANNOT_COMPUTE_SYMLINK);
      }else{
        content_get(rid, &a);
        blob_delta_apply(&a, &delta, &b);
        if( fBaseline ){
          diff_file_mem(&a, &b, zNew, pCfg);
        }else{
          pCfg->diffFlags ^= DIFF_INVERT;
          diff_file(&b, zOPath, zNew, pCfg, 0);
          pCfg->diffFlags ^= DIFF_INVERT;
        }
        blob_reset(&a);
        blob_reset(&b);
      }
      blob_reset(&delta);
    }
  }
  db_finalize(&q);
  diff_end(pCfg, 0);
}

/*
** Drop the indicated stash
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
  int stashid;
  if( zStashId==0 ){
    stashid = db_int(0, "SELECT max(stashid) FROM stash");
    if( stashid==0 ) fossil_fatal("empty stash");
  }else{
    stashid = atoi(zStashId);
    if( !db_exists("SELECT 1 FROM stash WHERE stashid=%d", stashid) ){
      fossil_fatal("no such stash: %s", zStashId);
    }
  }
  return stashid;
}

/*
** COMMAND: stash
**
** Usage: %fossil stash SUBCOMMAND ARGS...
**
** > fossil stash
** > fossil stash save ?FILES...?
** > fossil stash snapshot ?FILES...?
**
**      Save the current changes in the working tree as a new stash.
**      Then revert the changes back to the last check-in.  If FILES
**      are listed, then only stash and revert the named files.  The
**      "save" verb can be omitted if and only if there are no other
**      arguments.  The "snapshot" verb works the same as "save" but
**      omits the revert, keeping the check-out unchanged.
**
**      Options:
**         --editor NAME                  Use the NAME editor to enter comment
**         -m|--comment COMMENT           Comment text for the new stash
**
**
** > fossil stash list|ls ?-v|--verbose? ?-W|--width NUM?
**
**      List all changes sets currently stashed.  Show information about
**      individual files in each changeset if -v or --verbose is used.
**
** > fossil stash show|cat ?STASHID? ?DIFF-OPTIONS?
** > fossil stash gshow|gcat ?STASHID? ?DIFF-OPTIONS?
**
**      Show the contents of a stash as a diff against its baseline.
**      With gshow and gcat, gdiff-command is used instead of internal
**      diff logic.
**
** > fossil stash pop
** > fossil stash apply ?STASHID?
**
**      Apply STASHID or the most recently created stash to the current
**      working check-out.  The "pop" command deletes that changeset from
**      the stash after applying it but the "apply" command retains the
**      changeset.
**
** > fossil stash goto ?STASHID?
**
**      Update to the baseline check-out for STASHID then apply the
**      changes of STASHID.  Keep STASHID so that it can be reused
**      This command is undoable.
**
** > fossil stash drop|rm ?STASHID? ?-a|--all?
**
**      Forget everything about STASHID.  Forget the whole stash if the
**      -a|--all flag is used.  Individual drops are undoable but -a|--all
**      is not.
**
** > fossil stash diff ?STASHID? ?DIFF-OPTIONS?
** > fossil stash gdiff ?STASHID? ?DIFF-OPTIONS?
**
**      Show diffs of the current working directory and what that
**      directory would be if STASHID were applied. With gdiff,
**      gdiff-command is used instead of internal diff logic.
**
** > fossil stash rename STASHID NEW-NAME
**
**      Change the description of the given STASHID entry to NEW-NAME.
*/
void stash_cmd(void){
  const char *zCmd;
  int nCmd;
  int stashid = 0;
  undo_capture_command_line();
  db_must_be_within_tree();
  db_open_config(0, 0);
  db_begin_transaction();
  stash_tables_exist_and_current();
  if( g.argc<=2 ){
    zCmd = "save";
  }else{
    zCmd = g.argv[2];
  }
  nCmd = strlen(zCmd);
  if( strncmp(zCmd, "save", nCmd)==0 ){
    if( unsaved_changes(0)==0 ){
      fossil_fatal("nothing to stash");
    }
    stashid = stash_create();
    undo_disable();
    if( g.argc>=2 ){
      int nFile = db_int(0, "SELECT count(*) FROM stashfile WHERE stashid=%d",
                         stashid);
      char **newArgv;
      int i = 2;
      Stmt q;
      if( nFile==0 ){
        fossil_fatal("No modified files match the provided pattern.");
      }
      newArgv = fossil_malloc( sizeof(char*)*(nFile+2) );
      db_prepare(&q,"SELECT origname FROM stashfile WHERE stashid=%d", stashid);
      while( db_step(&q)==SQLITE_ROW ){
        newArgv[i++] = mprintf("%s%s", g.zLocalRoot, db_column_text(&q, 0));
      }
      db_finalize(&q);
      newArgv[0] = g.argv[0];
      newArgv[1] = 0;
      g.argv = newArgv;
      g.argc = nFile+2;
    }
    /* Make sure the stash has committed before running the revert, so that
    ** we have a copy of the changes before deleting them. */
    db_commit_transaction();
    g.argv[1] = "revert";
    revert_cmd();
    fossil_print("stash %d saved\n", stashid);
    return;
  }else
  if( strncmp(zCmd, "snapshot", nCmd)==0 ){
    stash_create();
  }else
  if( strncmp(zCmd, "list", nCmd)==0 || strncmp(zCmd, "ls", nCmd)==0 ){
    Stmt q, q2;
    int n = 0, width;
    int verboseFlag = find_option("verbose","v",0)!=0;
    const char *zWidth = find_option("width","W",1);

    if( zWidth ){
      width = atoi(zWidth);
      if( (width!=0) && (width<=46) ){
        fossil_fatal("-W|--width value must be >46 or 0");
      }
    }else{
      width = -1;
    }
    if( !verboseFlag ){
      verboseFlag = find_option("detail","l",0)!=0; /* deprecated */
    }
    verify_all_options();
    db_prepare(&q,
       "SELECT stashid, hash, comment, datetime(ctime) FROM stash"
       " ORDER BY ctime"
    );
    if( verboseFlag ){
      db_prepare(&q2, "SELECT isAdded, isRemoved, origname, newname"
                      "  FROM stashfile WHERE stashid=$id");
    }
    while( db_step(&q)==SQLITE_ROW ){
      int stashid = db_column_int(&q, 0);
      const char *zCom;
      n++;
      fossil_print("%5d: [%.14s] on %s\n",
        stashid,
        db_column_text(&q, 1),
        db_column_text(&q, 3)
      );
      zCom = db_column_text(&q, 2);
      if( zCom && zCom[0] ){
        fossil_print("       ");
        comment_print(zCom, 0, 7, width, get_comment_format());
      }
      if( verboseFlag ){
        db_bind_int(&q2, "$id", stashid);
        while( db_step(&q2)==SQLITE_ROW ){
          int isAdded = db_column_int(&q2, 0);
          int isRemoved = db_column_int(&q2, 1);
          const char *zOrig = db_column_text(&q2, 2);
          const char *zNew = db_column_text(&q2, 3);
          if( isAdded ){
             fossil_print("          ADD %s\n", zNew);
          }else if( isRemoved ){
             fossil_print("          REMOVE %s\n", zOrig);
          }else if( fossil_strcmp(zOrig,zNew)!=0 ){
             fossil_print("          RENAME %s -> %s\n", zOrig, zNew);
          }else{
             fossil_print("          EDIT %s\n", zOrig);
          }
        }
        db_reset(&q2);
      }
    }
    db_finalize(&q);
    if( verboseFlag ) db_finalize(&q2);
    if( n==0 ) fossil_print("empty stash\n");
  }else
  if( strncmp(zCmd, "drop", nCmd)==0 || strncmp(zCmd, "rm", nCmd)==0 ){
    int allFlag = find_option("all", "a", 0)!=0;
    if( allFlag ){
      Blob ans;
      char cReply;
      prompt_user("This action is not undoable.  Continue (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      if( cReply=='y' || cReply=='Y' ){
        db_multi_exec("DELETE FROM stash; DELETE FROM stashfile;");
      }
    }else if( g.argc>=4 ){
      int i;
      undo_begin();
      for(i=3; i<g.argc; i++){
        stashid = stash_get_id(g.argv[i]);
        undo_save_stash(stashid);
        stash_drop(stashid);
      }
      undo_finish();
    }else{
      undo_begin();
      undo_save_stash(0);
      stash_drop(stashid);
      undo_finish();
    }
  }else
  if( strncmp(zCmd, "pop", nCmd)==0 ||  strncmp(zCmd, "apply", nCmd)==0 ){
    char *zCom = 0, *zDate = 0, *zHash = 0;
    int popped = *zCmd=='p';
    if( popped ){
      if( g.argc>3 ) usage("pop");
      stashid = stash_get_id(0);
    }else{
      if( g.argc>4 ) usage("apply STASHID");
      stashid = stash_get_id(g.argc==4 ? g.argv[3] : 0);
    }
    zCom = db_text(0, "SELECT comment FROM stash WHERE stashid=%d", stashid);
    zDate = db_text(0, "SELECT datetime(ctime) FROM stash WHERE stashid=%d",
        stashid);
    zHash = db_text(0, "SELECT hash FROM stash WHERE stashid=%d", stashid);
    undo_begin();
    stash_apply(stashid, 0);
    if( popped ) undo_save_stash(stashid);
    fossil_print("%s stash:\n%5d: [%.14s] from %s\n",
        popped ? "Popped" : "Applied", stashid, zHash, zDate);
    if( zCom && *zCom ){
      fossil_print("       ");
      comment_print(zCom, 0, 7, -1, get_comment_format());
    }
    fossil_free(zCom);
    fossil_free(zDate);
    fossil_free(zHash);
    undo_finish();
    if( popped ) stash_drop(stashid);
  }else
  if( strncmp(zCmd, "goto", nCmd)==0 ){
    int nConflict;
    int vid;
    if( g.argc>4 ) usage("apply STASHID");
    stashid = stash_get_id(g.argc==4 ? g.argv[3] : 0);
    undo_begin();
    vid = db_int(0, "SELECT blob.rid FROM stash,blob"
                    " WHERE stashid=%d AND blob.uuid=stash.hash", stashid);
    nConflict = update_to(vid);
    stash_apply(stashid, nConflict);
    db_multi_exec("UPDATE vfile SET mtime=0 WHERE pathname IN "
                  "(SELECT origname FROM stashfile WHERE stashid=%d)",
                  stashid);
    undo_finish();
  }else
  if( strncmp(zCmd, "diff", nCmd)==0
   || strncmp(zCmd, "gdiff", nCmd)==0
   || strncmp(zCmd, "show", nCmd)==0
   || strncmp(zCmd, "gshow", nCmd)==0
   || strncmp(zCmd, "cat", nCmd)==0
   || strncmp(zCmd, "gcat", nCmd)==0
  ){
    int fBaseline = 0;
    DiffConfig DCfg;

    if( strstr(zCmd,"show")!=0 || strstr(zCmd,"cat")!=0 ){
      fBaseline = 1;
    }
    if( find_option("tk",0,0)!=0 || gdiff_using_tk(zCmd[0]=='g') ){
      db_close(0);
      diff_tk(fBaseline ? "stash show" : "stash diff", 3);
      return;
    }
    diff_options(&DCfg, zCmd[0]=='g', 0);
    stashid = stash_get_id(g.argc==4 ? g.argv[3] : 0);
    stash_diff(stashid, fBaseline, &DCfg);
  }else
  if( strncmp(zCmd, "rename", nCmd)==0 ){
    if( g.argc!=5 ) usage("rename STASHID NAME");
    stashid = stash_get_id(g.argv[3]);
    db_multi_exec("UPDATE STASH SET COMMENT=%Q WHERE stashid=%d",
                  g.argv[4], stashid);
  }
  else if( strncmp(zCmd, "help", nCmd)==0 ){
    g.argv[1] = "help";
    g.argv[2] = "stash";
    g.argc = 3;
    help_cmd();
  }else
  {
    usage("SUBCOMMAND ARGS...");
  }
  db_end_transaction(0);
}
