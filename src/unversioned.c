/*
** Copyright (c) 2016 D. Richard Hipp
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
** This file contains code used to implement unversioned file interfaces.
*/
#include "config.h"
#include <assert.h>
#if defined(FOSSIL_ENABLE_MINIZ)
#  define MINIZ_HEADER_FILE_ONLY
#  include "miniz.c"
#else
#  include <zlib.h>
#endif
#include "unversioned.h"
#include <time.h>

/*
** SQL code to implement the tables needed by the unversioned.
*/
static const char zUnversionedInit[] =
@ CREATE TABLE IF NOT EXISTS repository.unversioned(
@   uvid INTEGER PRIMARY KEY AUTOINCREMENT,  -- unique ID for this file
@   name TEXT UNIQUE,            -- Name of the uv file
@   rcvid INTEGER,               -- Where received from
@   mtime DATETIME,              -- timestamp.  Seconds since 1970.
@   hash TEXT,                   -- Content hash.  NULL if a delete marker
@   sz INTEGER,                  -- size of content after decompression
@   encoding INT,                -- 0: plaintext.  1: zlib compressed
@   content BLOB                 -- content of the file.  NULL if oversized
@ );
;

/*
** Make sure the unversioned table exists in the repository.
*/
void unversioned_schema(void){
  if( !db_table_exists("repository", "unversioned") ){
    db_multi_exec(zUnversionedInit/*works-like:""*/);
  }
}

/*
** Return a string which is the hash of the unversioned content.
** This is the hash used by repositories to compare content before
** exchanging a catalog.  So all repositories must compute this hash
** in exactly the same way.
**
** If debugFlag is set, force the value to be recomputed and write
** the text of the hashed string to stdout.
*/
const char *unversioned_content_hash(int debugFlag){
  const char *zHash = debugFlag ? 0 : db_get("uv-hash", 0);
  if( zHash==0 ){
    Stmt q;
    db_prepare(&q,
      "SELECT printf('%%s %%s %%s\n',name,datetime(mtime,'unixepoch'),hash)"
      "  FROM unversioned"
      " WHERE hash IS NOT NULL"
      " ORDER BY name"
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *z = db_column_text(&q, 0);
      if( debugFlag ) fossil_print("%s", z);
      sha1sum_step_text(z,-1);
    }
    db_finalize(&q);
    db_set("uv-hash", sha1sum_finish(0), 0);
    zHash = db_get("uv-hash",0);
  }
  return zHash;
}

/*
** Initialize pContent to be the content of an unversioned file zName.
**
** Return 0 on success.  Return 1 if zName is not found.
*/
int unversioned_content(const char *zName, Blob *pContent){
  Stmt q;
  int rc = 1;
  blob_init(pContent, 0, 0);
  db_prepare(&q, "SELECT encoding, content FROM unversioned WHERE name=%Q", zName);
  if( db_step(&q)==SQLITE_ROW ){
    db_column_blob(&q, 1, pContent);
    if( db_column_int(&q, 0)==1 ){
      blob_uncompress(pContent, pContent);
    }
    rc = 0;
  }
  db_finalize(&q);
  return rc;
}

/*
** Write unversioned content into the database.
*/
static void unversioned_write(
  const char *zUVFile,               /* Name of the unversioned file */
  Blob *pContent,                    /* File content */
  sqlite3_int64 mtime                /* Modification time */
){
  Stmt ins;
  Blob compressed;
  Blob hash;

  db_prepare(&ins,
    "REPLACE INTO unversioned(name,rcvid,mtime,hash,sz,encoding,content)"
    " VALUES(:name,:rcvid,:mtime,:hash,:sz,:encoding,:content)"
  );
  sha1sum_blob(pContent, &hash);
  blob_compress(pContent, &compressed);
  db_bind_text(&ins, ":name", zUVFile);
  db_bind_int(&ins, ":rcvid", g.rcvid);
  db_bind_int64(&ins, ":mtime", mtime);
  db_bind_text(&ins, ":hash", blob_str(&hash));
  db_bind_int(&ins, ":sz", blob_size(pContent));
  if( blob_size(&compressed) <= 0.8*blob_size(pContent) ){
    db_bind_int(&ins, ":encoding", 1);
    db_bind_blob(&ins, ":content", &compressed);
  }else{
    db_bind_int(&ins, ":encoding", 0);
    db_bind_blob(&ins, ":content", pContent);
  }
  db_step(&ins);
  blob_reset(&compressed);
  blob_reset(&hash);
  db_finalize(&ins);
  db_unset("uv-hash", 0);
}


/*
** Check the status of unversioned file zName.  "mtime" and "zHash" are the
** time of last change and SHA1 hash of a copy of this file on a remote
** server.  Return an integer status code as follows:
**
**    0:     zName does not exist in the unversioned table.
**    1:     zName exists and should be replaced by the mtime/zHash remote.
**    2:     zName exists and is the same as zHash but has a older mtime
**    3:     zName exists and is identical to mtime/zHash in all respects.
**    4:     zName exists and is the same as zHash but has a newer mtime.
**    5:     zName exists and should override the mtime/zHash remote.
*/
int unversioned_status(const char *zName, sqlite3_int64 mtime, const char *zHash){
  int iStatus = 0;
  Stmt q;
  db_prepare(&q, "SELECT mtime, hash FROM unversioned WHERE name=%Q", zName);
  if( db_step(&q)==SQLITE_ROW ){
    const char *zLocalHash = db_column_text(&q, 1);
    int hashCmp;
    sqlite3_int64 iLocalMtime = db_column_int64(&q, 0);
    int mtimeCmp = iLocalMtime<mtime ? -1 : (iLocalMtime==mtime ? 0 : +1);
    if( zLocalHash==0 ) zLocalHash = "-";
    hashCmp = strcmp(zLocalHash, zHash);
    if( hashCmp==0 ){
      iStatus = 3 + mtimeCmp;
    }else if( mtimeCmp<0 || (mtimeCmp==0 && hashCmp<0) ){
      iStatus = 1;
    }else{
      iStatus = 5;
    }
  }
  db_finalize(&q);
  return iStatus;
}

/*
** Extract command-line options for the "revert" and "sync" subcommands
*/
static int unversioned_sync_flags(unsigned syncFlags){
  if( find_option("verbose","v",0)!=0 ){
    syncFlags |= SYNC_UV_TRACE | SYNC_VERBOSE;
  }
  if( find_option("dryrun","n",0)!=0 ){
    syncFlags |= SYNC_UV_DRYRUN | SYNC_UV_TRACE | SYNC_VERBOSE;
  }
  return syncFlags;
}

/*
** Return true if the zName contains any whitespace
*/
static int contains_whitespace(const char *zName){
  while( zName[0] ){
    if( fossil_isspace(zName[0]) ) return 1;
    zName++;
  }
  return 0;
}

/*
** COMMAND: uv*
** COMMAND: unversioned
**
** Usage: %fossil unversioned SUBCOMMAND ARGS...
**    or: %fossil uv SUBCOMMAND ARGS..
**
** Unversioned files (UV-files) are artifacts that are synced and are available
** for download but which do not preserve history.  Only the most recent version
** of each UV-file is retained.  Changes to an UV-file are permanent and cannot
** be undone, so use appropriate caution with this command.
**
** Subcommands:
**
**    add FILE ...         Add or update an unversioned files in the local
**                         repository so that it matches FILE on disk.
**                         Use "--as UVFILE" to give the file a different name
**                         in the repository than what it called on disk.
**                         Changes are not pushed to other repositories until
**                         the next sync.
**
**    cat FILE ...         Concatenate the content of FILEs to stdout.
**
**    edit FILE            Bring up FILE in a text editor for modification.
**
**    export FILE OUTPUT   Write the content of FILE into OUTPUT on disk
**
**    list | ls            Show all unversioned files held in the local
**                         repository.
**
**    revert ?URL?         Restore the state of all unversioned files in the
**                         local repository to match the remote repository
**                         URL.
**
**                         Options:
**                            -v|--verbose     Extra diagnostic output
**                            -n|--dryrun      Show what would have happened
**
**    remove|rm|delete FILE ...
**                         Remove unversioned files from the local repository.
**                         Changes are not pushed to other repositories until
**                         the next sync.
**
**    sync ?URL?           Synchronize the state of all unversioned files with
**                         the remote repository URL.  The most recent version
**                         of each file is propagate to all repositories and
**                         all prior versions are permanently forgotten.
**
**                         Options:
**                            -v|--verbose     Extra diagnostic output
**                            -n|--dryrun      Show what would have happened
**
**    touch FILE ...       Update the TIMESTAMP on all of the listed files
**
** Options:
**
**   --mtime TIMESTAMP     Use TIMESTAMP instead of "now" for the "add",
**                         "edit", "remove", and "touch" subcommands.
*/
void unversioned_cmd(void){
  const char *zCmd;
  int nCmd;
  const char *zMtime = find_option("mtime", 0, 1);
  sqlite3_int64 mtime;
  db_find_and_open_repository(0, 0);
  unversioned_schema();
  zCmd = g.argc>=3 ? g.argv[2] : "x";
  nCmd = (int)strlen(zCmd);
  if( zMtime==0 ){
    mtime = time(0);
  }else{
    mtime = db_int(0, "SELECT strftime('%%s',%Q)", zMtime);
    if( mtime<=0 ) fossil_fatal("bad timestamp: %Q", zMtime);
  }
  if( memcmp(zCmd, "add", nCmd)==0 ){
    const char *zError = 0;
    const char *zIn;
    const char *zAs;
    Blob file;
    int i;

    zAs = find_option("as",0,1);
    if( zAs && g.argc!=4 ) usage("add DISKFILE --as UVFILE");
    verify_all_options();
    db_begin_transaction();
    content_rcvid_init("#!fossil unversioned add");
    for(i=3; i<g.argc; i++){
      zIn = zAs ? zAs : g.argv[i];
      if( zIn[0]==0 ){
        zError = "be empty string";
      }else if( zIn[0]=='/' ){
        zError = "be absolute";
      }else if ( !file_is_simple_pathname(zIn,1) ){
        zError = "contain complex paths";
      }else if( contains_whitespace(zIn) ){
        zError = "contain whitespace";
      }
      if( zError ){
        fossil_fatal("unversioned filenames may not %s: %Q", zError, zIn);
      }
      blob_init(&file,0,0);
      blob_read_from_file(&file, g.argv[i]);
      unversioned_write(zIn, &file, mtime);
      blob_reset(&file);
    }
    db_end_transaction(0);
  }else if( memcmp(zCmd, "cat", nCmd)==0 ){
    int i;
    verify_all_options();
    db_begin_transaction();
    for(i=3; i<g.argc; i++){
      Blob content;
      if( unversioned_content(g.argv[i], &content)==0 ){
        blob_write_to_file(&content, "-");
      }
      blob_reset(&content);
    }
    db_end_transaction(0);
  }else if( memcmp(zCmd, "edit", nCmd)==0 ){
    const char *zEditor;    /* Name of the text-editor command */
    const char *zTFile;     /* Temporary file */
    const char *zUVFile;    /* Name of the unversioned file */
    char *zCmd;             /* Command to run the text editor */
    Blob content;           /* Content of the unversioned file */

    verify_all_options();
    if( g.argc!=4) usage("edit UVFILE");
    zUVFile = g.argv[3];
    zEditor = fossil_text_editor();
    if( zEditor==0 ) fossil_fatal("no text editor - set the VISUAL env variable");
    zTFile = fossil_temp_filename();
    if( zTFile==0 ) fossil_fatal("cannot find a temporary filename");
    db_begin_transaction();
    content_rcvid_init("#!fossil unversioned edit");
    if( unversioned_content(zUVFile, &content) ){
      fossil_fatal("no such uv-file: %Q", zUVFile);
    }
    if( looks_like_binary(&content) ){
      fossil_fatal("cannot edit binary content");
    }
#if defined(_WIN32) || defined(__CYGWIN__)
    blob_add_cr(&content);
#endif
    blob_write_to_file(&content, zTFile);
    zCmd = mprintf("%s \"%s\"", zEditor, zTFile);
    if( fossil_system(zCmd) ){
      fossil_fatal("editor aborted: %Q", zCmd);
    }
    fossil_free(zCmd);
    blob_reset(&content);
    blob_read_from_file(&content, zTFile);
#if defined(_WIN32) || defined(__CYGWIN__)
    blob_to_lf_only(&content);
#endif
    file_delete(zTFile);
    if( zMtime==0 ) mtime = time(0);
    unversioned_write(zUVFile, &content, mtime);
    db_end_transaction(0);
    blob_reset(&content);
  }else if( memcmp(zCmd, "export", nCmd)==0 ){
    Blob content;
    verify_all_options();
    if( g.argc!=5 ) usage("export UVFILE OUTPUT");
    if( unversioned_content(g.argv[3], &content) ){
      fossil_fatal("no such uv-file: %Q", g.argv[3]);
    }
    blob_write_to_file(&content, g.argv[4]);
    blob_reset(&content);
  }else if( memcmp(zCmd, "hash", nCmd)==0 ){  /* undocumented */
    /* Show the hash value used during uv sync */
    int debugFlag = find_option("debug",0,0)!=0;
    fossil_print("%s\n", unversioned_content_hash(debugFlag));
  }else if( memcmp(zCmd, "list", nCmd)==0 || memcmp(zCmd, "ls", nCmd)==0 ){
    Stmt q;
    int allFlag = find_option("all","a",0)!=0;
    int longFlag = find_option("l",0,0)!=0 || (nCmd>1 && zCmd[1]=='i');
    verify_all_options();
    if( !longFlag ){
      if( allFlag ){
        db_prepare(&q, "SELECT name FROM unversioned ORDER BY name");
      }else{
        db_prepare(&q, "SELECT name FROM unversioned WHERE hash IS NOT NULL"
                       " ORDER BY name");
      }
      while( db_step(&q)==SQLITE_ROW ){
        fossil_print("%s\n", db_column_text(&q,0));
      }
    }else{
      db_prepare(&q,
        "SELECT hash, datetime(mtime,'unixepoch'), sz, length(content), name"
        "   FROM unversioned"
        "  ORDER BY name;"
      );
      while( db_step(&q)==SQLITE_ROW ){
        const char *zHash = db_column_text(&q, 0);
        const char *zNoContent = "";
        if( zHash==0 ){
          if( !allFlag ) continue;
          zHash = "(deleted)";
        }else if( db_column_type(&q,3)==SQLITE_NULL ){
          zNoContent = " (no content)";
        }
        fossil_print("%12.12s %s %8d %8d %s%s\n",
           zHash,
           db_column_text(&q,1),
           db_column_int(&q,2),
           db_column_int(&q,3),
           db_column_text(&q,4),
           zNoContent
        );
      }
    }
    db_finalize(&q);
  }else if( memcmp(zCmd, "revert", nCmd)==0 ){
    unsigned syncFlags = unversioned_sync_flags(SYNC_UNVERSIONED|SYNC_UV_REVERT);
    g.argv[1] = "sync";
    g.argv[2] = "--uv-noop";
    sync_unversioned(syncFlags);
  }else if( memcmp(zCmd, "remove", nCmd)==0 || memcmp(zCmd, "rm", nCmd)==0
         || memcmp(zCmd, "delete", nCmd)==0 ){
    int i;
    verify_all_options();
    db_begin_transaction();
    for(i=3; i<g.argc; i++){
      db_multi_exec(
        "UPDATE unversioned"
        "   SET hash=NULL, content=NULL, mtime=%lld, sz=0 WHERE name=%Q",
        mtime, g.argv[i]
      );
    }
    db_unset("uv-hash", 0);
    db_end_transaction(0);
  }else if( memcmp(zCmd,"sync",nCmd)==0 ){
    unsigned syncFlags = unversioned_sync_flags(SYNC_UNVERSIONED);
    g.argv[1] = "sync";
    g.argv[2] = "--uv-noop";
    sync_unversioned(syncFlags);
  }else if( memcmp(zCmd, "touch", nCmd)==0 ){
    int i;
    verify_all_options();
    db_begin_transaction();
    for(i=3; i<g.argc; i++){
      db_multi_exec(
        "UPDATE unversioned SET mtime=%lld WHERE name=%Q",
        mtime, g.argv[i]
      );
    }
    db_unset("uv-hash", 0);
    db_end_transaction(0);
  }else{
    usage("add|cat|edit|export|list|revert|remove|sync|touch");
  }
}

/*
** WEBPAGE: uvlist
**
** Display a list of all unversioned files in the repository.
** Query parameters:
**
**    byage=1          Order the initial display be decreasing age
**    showdel=0        Show deleted files
*/
void uvlist_page(void){
  Stmt q;
  sqlite3_int64 iNow;
  sqlite3_int64 iTotalSz = 0;
  int cnt = 0;
  int n = 0;
  const char *zOrderBy = "name";
  int showDel = 0;
  char zSzName[100];

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_header("Unversioned Files");
  if( !db_table_exists("repository","unversioned") ){
    @ No unversioned files on this server
    style_footer();
    return;
  }
  if( PB("byage") ) zOrderBy = "mtime DESC";
  if( PB("showdel") ) showDel = 1;
  db_prepare(&q,
    "SELECT"
    "   name,"
    "   mtime,"
    "   hash,"
    "   sz,"
    "   (SELECT login FROM rcvfrom, user"
    "     WHERE user.uid=rcvfrom.uid AND rcvfrom.rcvid=unversioned.rcvid),"
    "   rcvid"
    " FROM unversioned %s ORDER BY %s",
    showDel ? "" : "WHERE hash IS NOT NULL" /*safe-for-%s*/,
    zOrderBy/*safe-for-%s*/
  );
  iNow = db_int64(0, "SELECT strftime('%%s','now');");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    sqlite3_int64 mtime = db_column_int(&q, 1);
    const char *zHash = db_column_text(&q, 2);
    int isDeleted = zHash==0;
    int fullSize = db_column_int(&q, 3);
    char *zAge = human_readable_age((iNow - mtime)/86400.0);
    const char *zLogin = db_column_text(&q, 4);
    int rcvid = db_column_int(&q,5);
    if( zLogin==0 ) zLogin = "";
    if( (n++)==0 ){
      @ <div class="uvlist">
      @ <table cellpadding="2" cellspacing="0" border="1" id="uvtab">
      @ <thead><tr>
      @   <th> Name
      @   <th> Age
      @   <th> Size
      @   <th> User
      @   <th> SHA1
      if( g.perm.Admin ){
        @ <th> rcvid
      }
      @ </tr></thead>
      @ <tbody>
    }
    @ <tr>
    if( isDeleted ){
      sqlite3_snprintf(sizeof(zSzName), zSzName, "<i>Deleted</i>");
      zHash = "";
      fullSize = 0;
      @ <td> %h(zName) </td>
    }else{
      approxSizeName(sizeof(zSzName), zSzName, fullSize);
      iTotalSz += fullSize;
      cnt++;
      @ <td> <a href='%R/uv/%T(zName)'>%h(zName)</a> </td>
    }
    @ <td data-sortkey='%016llx(-mtime)'> %s(zAge) </td>
    @ <td data-sortkey='%08x(fullSize)'> %s(zSzName) </td>
    @ <td> %h(zLogin) </td>
    @ <td> %h(zHash) </td>
    if( g.perm.Admin ){
      if( rcvid ){
        @ <td> <a href="%R/rcvfrom?rcvid=%d(rcvid)">%d(rcvid)</a>
      }else{
        @ <td>
      }
    }
    @ </tr>
    fossil_free(zAge);
  }
  db_finalize(&q);
  if( n ){
    approxSizeName(sizeof(zSzName), zSzName, iTotalSz);
    @ </tbody>
    @ <tfoot><tr><td><b>Total over %d(cnt) files</b><td><td>%s(zSzName)
    @ <td><td>
    if( g.perm.Admin ){
      @ <td>
    }
    @ </tfoot>
    @ </table></div>
    output_table_sorting_javascript("uvtab","tkKttN",1);
  }else{
    @ No unversioned files on this server.
  }
  style_footer();
}

/*
** WEBPAGE: juvlist
**
** Return a complete list of unversioned files as JSON.  The JSON
** looks like this:
**
** [{"name":NAME,
**   "mtime":MTIME,
**   "hash":HASH,
**   "size":SIZE,
**   "user":USER}]
*/
void uvlist_json_page(void){
  Stmt q;
  char *zSep = "[";
  Blob json;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  cgi_set_content_type("text/json");
  if( !db_table_exists("repository","unversioned") ){
    blob_init(&json, "[]", -1);
    cgi_set_content(&json);
    return;
  }
  blob_init(&json, 0, 0);
  db_prepare(&q,
     "SELECT"
     "   name,"
     "   mtime,"
     "   hash,"
     "   sz,"
     "   (SELECT login FROM rcvfrom, user"
     "     WHERE user.uid=rcvfrom.uid AND rcvfrom.rcvid=unversioned.rcvid)"
     " FROM unversioned WHERE hash IS NOT NULL"
   );
   while( db_step(&q)==SQLITE_ROW ){
     const char *zName = db_column_text(&q, 0);
     sqlite3_int64 mtime = db_column_int(&q, 1);
     const char *zHash = db_column_text(&q, 2);
     int fullSize = db_column_int(&q, 3);
     const char *zLogin = db_column_text(&q, 4);
     if( zLogin==0 ) zLogin = "";
     blob_appendf(&json, "%s{\"name\":\"", zSep);
     zSep = ",\n ";
     blob_append_json_string(&json, zName);
     blob_appendf(&json, "\",\n  \"mtime\":%lld,\n  \"hash\":\"", mtime);
     blob_append_json_string(&json, zHash);
     blob_appendf(&json, "\",\n  \"size\":%d,\n  \"user\":\"", fullSize);
     blob_append_json_string(&json, zLogin);
     blob_appendf(&json, "\"}");
   }
   db_finalize(&q);
   blob_appendf(&json,"]\n");
   cgi_set_content(&json);
}
