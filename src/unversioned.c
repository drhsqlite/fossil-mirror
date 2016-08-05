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
@ CREATE TABLE IF NOT EXISTS "%w".unversioned(
@   name TEXT PRIMARY KEY,       -- Name of the uv file
@   rcvid INTEGER,               -- Where received from
@   mtime DATETIME,              -- timestamp.  Seconds since 1970.
@   hash TEXT,                   -- Content hash
@   sz INTEGER,                  -- size of content after decompression
@   content BLOB                 -- zlib compressed content
@ ) WITHOUT ROWID;
;

/*
** Make sure the unversioned table exists in the repository.
*/
void unversioned_schema(void){
  if( !db_table_exists("repository", "unversioned") ){
    db_multi_exec(zUnversionedInit /*works-like:"%w"*/, db_name("repository"));
  }
}

/*
** COMMAND: unversioned
**
** Usage: %fossil unversioned SUBCOMMAND ARGS...
**
** Unversioned files (UV-files) are artifacts that are synced and are available
** for download but which do not preserve history.  Only the most recent version
** of each UV-file is retained.  Changes to an UV-file are permanent and cannot
** be undone, so use appropriate caution with this command.
**
** Subcommands:
**
**    add FILE ?INPUT?     Add or update an unversioned file FILE in the local
**                         repository so that it matches the INPUT file on disk.
**                         Content is read from stdin if INPUT is "-" or omitted.
**                         Changes are not pushed to other repositories until
**                         the next sync.
**
**    cat FILE ?OUTFILE?   Write the content of unversioned file FILE to OUTFILE
**                         on disk, or to stdout if OUTFILE is "-" or is omitted.
**
**    list | ls            Show all unversioned files held in the local repository.
**
**    revert ?URL?         Restore the state of all unversioned files in the local
**                         repository to match the remote repository URL.
**
**    rm FILE ...          Remove an unversioned files from the local repository.
**                         Changes are not pushed to other repositories until
**                         the next sync.
**
**    sync ?URL?           Synchronize the state of all unversioned files with
**                         the remote repository URL.  The most recent version of
**                         each file is propagate to all repositories and all
**                         prior versions are permanently forgotten.
**
** Options:
**
**   --mtime TIMESTAMP     Use TIMESTAMP instead of "now" for "add" and "rm".
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
    const char *zFile;
    const char *zIn;
    Blob file;
    Blob hash;
    Blob compressed;
    Stmt ins;
    if( g.argc!=4 && g.argc!=5 ) usage("add FILE ?INPUT?");
    zFile = g.argv[3];
    if( !file_is_simple_pathname(zFile,1) ){
      fossil_fatal("'%Q' is not an acceptable filename", zFile);
    }
    zIn = g.argc==5 ? g.argv[4] : "-";
    blob_init(&file,0,0);
    blob_read_from_file(&file, zIn);
    sha1sum_blob(&file, &hash);
    blob_compress(&file, &compressed);
    db_begin_transaction();
    content_rcvid_init();
    db_prepare(&ins,
      "REPLACE INTO unversioned(name,rcvid,mtime,hash,sz,content)"
      " VALUES(:name,:rcvid,:mtime,:hash,:sz,:content)"
    );
    db_bind_text(&ins, ":name", zFile);
    db_bind_int(&ins, ":rcvid", g.rcvid);
    db_bind_int64(&ins, ":mtime", mtime);
    db_bind_text(&ins, ":hash", blob_str(&hash));
    db_bind_int(&ins, ":sz", blob_size(&file));
    db_bind_blob(&ins, ":content", &compressed);
    db_step(&ins);
    db_finalize(&ins);
    blob_reset(&compressed);
    blob_reset(&hash);
    blob_reset(&file);
    /* Clear the uvhash cache */
    db_end_transaction(0);
  }else if( memcmp(zCmd, "cat", nCmd)==0 ){
  }else if( memcmp(zCmd, "list", nCmd)==0 || memcmp(zCmd, "ls", nCmd)==0 ){
    Stmt q;
    db_prepare(&q,
      "SELECT hash, datetime(mtime,'unixepoch'), sz, name, content IS NULL"
      "   FROM unversioned"
      "  WHERE hash IS NOT NULL"
      "  ORDER BY name;"
    );
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%12.12s %s %8d %s%s\n",
         db_column_text(&q,0),
         db_column_text(&q,1),
         db_column_int(&q,2),
         db_column_text(&q,3),
         db_column_int(&q,4) ? " ** no content ** ": ""
      );
    }
    db_finalize(&q);
  }else if( memcmp(zCmd, "revert", nCmd)==0 || memcmp(zCmd,"sync",nCmd)==0 ){
    fossil_fatal("not yet implemented...");
  }else if( memcmp(zCmd, "rm", nCmd)==0 ){
  }else{
    usage("add|cat|ls|revert|rm|sync");
  }
}

#if 0
***************************************************************************
DESIGN NOTES
Web interface:

    /uv/NAME
    /uvctrl
    /uvctrl?add=NAME&content=CONTENT
    /uvctrl?rm=NAME

Sync protocol:

Client sends

    pragma uvhash UVHASH

If the server support UV and if the UVHASH is different, then the
server replies with one or more of:

    uvigot NAME TIMESTAMP HASH

The HASH argument is omitted from deleted uv files.  The UVHASH in
the initial pragma is simply the SHA1 of the uvigot lines, each
terminated by \n, in lexicographical order.

The client examines the uvigot lines and for each difference
issues either:

    uvgimme NAME

or

    uvfile NAME TIMESTAMP SIZE FLAGS \n CONTENT

The client sends uvgimme if 

   (a) it does not possess NAME or
   (b) if the NAME it holds has an earlier timestamp than TIMESTAMP or
   (c) if the NAME it holds has the exact timestamp TIMESTAMP but a
       lexicographically earliers HASH.

Otherwise the client sends a uvfile.  The client also sends uvfile
cards for each unversioned file it holds which was not named by any
uvigot card.

On the uvfile card, the FLAGS value is an unsigned integer with
the meaning assigned to the following bits:

   0x0001     Record is deleted.  No CONTENT transmitted.

   0x0002     CONTENT is zlib compressed.  SIZE is the compressed size.

   0x0004     CONTENT is oversize and is omitted.  HASH sent instead.  SIZE
              is the uncompressed size

New FLAGS values may be added in future releases.

Internal representation:

   CREATE TABLE unversioned(
     name TEXT PRIMARY KEY,       -- Name of the uv file
     rcvid INTEGER,               -- Where received from
     mtime DATETIME,              -- timestamp.  Julian day
     hash TEXT,                   -- Content hash
     sz INTEGER,                  -- size of content after decompression
     content BLOB                 -- zlib compressed content
   ) WITHOUT ROWID;

The hash field is NULL for deleted content.  The content field is
NULL for content that is unavailable.

Other notes:

The mimetype of UV content is determine by the suffix on the
filename.

UV content can be sent to any user with read/check-out privilege 'o'.
New UV content will be accepted from users with write/check-in privilege 'i'.

The current UVHASH value can be cached in the config table under the
name of "uvhash".

Clients that are UV-aware and would like to be initialized with UV
content may send "pragma uvhash 0" upon initial clone, and the server
will include all necessary uvfile cards in its replies.

Clients or servers may send "pragma uv-size-limit SIZE" to inform the
other side that UV files larger than SIZE should be transmitted using
the "4" flag ("content omitted").  The hash is an extra parameter on
the end of a uvfile/4 card.

Clients and servers reject any UVFile with a timestamp that is too
far in the future.
***************************************************************************
#endif
