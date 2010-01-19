/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code used to check-out versions of the project
** from the local repository.
*/
#include "config.h"
#include "add.h"
#include <assert.h>
#include <dirent.h>

/*
** Set to true if files whose names begin with "." should be
** included when processing a recursive "add" command.
*/
static int includeDotFiles = 0;
    
/*
** Add a single file
*/
static void add_one_file(const char *zName, int vid, Blob *pOmit){
  Blob pathname;
  const char *zPath;
      
  file_tree_name(zName, &pathname, 1);
  zPath = blob_str(&pathname);
  if( strcmp(zPath, "manifest")==0
   || strcmp(zPath, "_FOSSIL_")==0
   || strcmp(zPath, "manifest.uuid")==0
   || blob_compare(&pathname, pOmit)==0
  ){
    fossil_warning("cannot add %s", zPath);
  }else{
    if( !file_is_simple_pathname(zPath) ){
      fossil_fatal("filename contains illegal characters: %s", zPath);
    }
    if( db_exists("SELECT 1 FROM vfile WHERE pathname=%Q", zPath) ){
      db_multi_exec("UPDATE vfile SET deleted=0 WHERE pathname=%Q", zPath);
    }else{
      db_multi_exec(
        "INSERT INTO vfile(vid,deleted,rid,mrid,pathname)"
        "VALUES(%d,0,0,0,%Q)", vid, zPath);
    }
    printf("ADDED  %s\n", zPath);
  }
  blob_reset(&pathname);
}

/*
** All content of the zDir directory to the SFILE table.
*/
void add_directory_content(const char *zDir){
  DIR *d;
  int origSize;
  struct dirent *pEntry;
  Blob path;

  blob_zero(&path);
  blob_append(&path, zDir, -1);
  origSize = blob_size(&path);
  d = opendir(zDir);
  if( d ){
    while( (pEntry=readdir(d))!=0 ){
      char *zPath;
      if( pEntry->d_name[0]=='.' ){
        if( !includeDotFiles ) continue;
        if( pEntry->d_name[1]==0 ) continue;
        if( pEntry->d_name[1]=='.' && pEntry->d_name[2]==0 ) continue;
      }
      blob_appendf(&path, "/%s", pEntry->d_name);
      zPath = blob_str(&path);
      if( file_isdir(zPath)==1 ){
        add_directory_content(zPath);
      }else if( file_isfile(zPath) ){
        db_multi_exec("INSERT INTO sfile VALUES(%Q)", zPath);
      }
      blob_resize(&path, origSize);
    }
  }
  closedir(d);
  blob_reset(&path);
}

/*
** Add all content of a directory.
*/
void add_directory(const char *zDir, int vid, Blob *pOmit){
  Stmt q;
  add_directory_content(zDir);
  db_prepare(&q, "SELECT x FROM sfile ORDER BY x");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    add_one_file(zName, vid, pOmit);
  }
  db_finalize(&q);
  db_multi_exec("DELETE FROM sfile");
}

/*
** COMMAND: add
**
** Usage: %fossil add FILE...
**
** Make arrangements to add one or more files to the current checkout 
** at the next commit.
**
** When adding files recursively, filenames that begin with "." are
** excluded by default.  To include such files, add the "--dotfiles"
** option to the command-line.
*/
void add_cmd(void){
  int i;
  int vid;
  Blob repo;

  includeDotFiles = find_option("dotfiles",0,0)!=0;
  db_must_be_within_tree();
  vid = db_lget_int("checkout",0);
  if( vid==0 ){
    fossil_panic("no checkout to add to");
  }
  db_begin_transaction();
  if( !file_tree_name(g.zRepositoryName, &repo, 0) ){
    blob_zero(&repo);
  }
  db_multi_exec("CREATE TEMP TABLE sfile(x TEXT PRIMARY KEY)");
  for(i=2; i<g.argc; i++){
    char *zName;
    int isDir;

    zName = mprintf("%/", g.argv[i]);
    isDir = file_isdir(zName);
    if( isDir==1 ){
      add_directory(zName, vid, &repo);
    }else if( isDir==0 ){
      fossil_fatal("not found: %s", zName);
    }else if( access(zName, R_OK) ){
      fossil_fatal("cannot open %s", zName);
    }else{
      add_one_file(zName, vid, &repo);
    }
    free(zName);
  }
  db_end_transaction(0);
}

/*
** COMMAND: rm
** COMMAND: del
**
** Usage: %fossil rm FILE...
**    or: %fossil del FILE...
**
** Remove one or more files from the tree.
**
** This command does not remove the files from disk.  It just marks the
** files as no longer being part of the project.  In other words, future
** changes to the named files will not be versioned.
*/
void del_cmd(void){
  int i;
  int vid;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_panic("no checkout to remove from");
  }
  db_begin_transaction();
  for(i=2; i<g.argc; i++){
    char *zName;
    char *zPath;
    Blob pathname;

    zName = mprintf("%/", g.argv[i]);
    file_tree_name(zName, &pathname, 1);
    zPath = blob_str(&pathname);
    if( !db_exists(
             "SELECT 1 FROM vfile WHERE pathname=%Q AND NOT deleted", zPath) ){
      fossil_fatal("not in the repository: %s", zName);
    }else{
      db_multi_exec("UPDATE vfile SET deleted=1 WHERE pathname=%Q", zPath);
      printf("DELETED  %s\n", zPath);
    }
    blob_reset(&pathname);
    free(zName);
  }
  db_multi_exec("DELETE FROM vfile WHERE deleted AND rid=0");
  db_end_transaction(0);
}

/*
** Rename a single file.  
**
** The original name of the file is zOrig.  The new filename is zNew.
*/
static void mv_one_file(int vid, const char *zOrig, const char *zNew){
  printf("RENAME %s %s\n", zOrig, zNew);
  db_multi_exec(
    "UPDATE vfile SET pathname='%s' WHERE pathname='%s' AND vid=%d",
    zNew, zOrig, vid
  );
}

/*
** COMMAND: mv
** COMMAND: rename
**
** Usage: %fossil mv|rename OLDNAME NEWNAME
**    or: %fossil mv|rename OLDNAME... DIR
**
** Move or rename one or more files within the tree
**
** This command does not rename the files on disk.  This command merely
** records the fact that filenames have changed so that appropriate notations
** can be made at the next commit/checkin.
*/
void mv_cmd(void){
  int i;
  int vid;
  char *zDest;
  Blob dest;
  Stmt q;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_panic("no checkout rename files in");
  }
  if( g.argc<4 ){
    usage("OLDNAME NEWNAME");
  }
  zDest = g.argv[g.argc-1];
  db_begin_transaction();
  file_tree_name(zDest, &dest, 1);
  db_multi_exec(
    "UPDATE vfile SET origname=pathname WHERE origname IS NULL;"
  );
  db_multi_exec(
    "CREATE TEMP TABLE mv(f TEXT UNIQUE ON CONFLICT IGNORE, t TEXT);"
  );
  if( file_isdir(zDest)!=1 ){
    Blob orig;
    if( g.argc!=4 ){
      usage("OLDNAME NEWNAME");
    }
    file_tree_name(g.argv[2], &orig, 1);
    db_multi_exec(
      "INSERT INTO mv VALUES(%B,%B)", &orig, &dest
    );
  }else{
    if( blob_eq(&dest, ".") ){
      blob_reset(&dest);
    }else{
      blob_append(&dest, "/", 1);
    }
    for(i=2; i<g.argc-1; i++){
      Blob orig;
      char *zOrig;
      int nOrig;
      file_tree_name(g.argv[i], &orig, 1);
      zOrig = blob_str(&orig);
      nOrig = blob_size(&orig);
      db_prepare(&q,
         "SELECT pathname FROM vfile"
         " WHERE vid=%d"
         "   AND (pathname='%s' OR pathname GLOB '%s/*')"
         " ORDER BY 1",
         vid, zOrig, zOrig
      );
      while( db_step(&q)==SQLITE_ROW ){
        const char *zPath = db_column_text(&q, 0);
        int nPath = db_column_bytes(&q, 0);
        const char *zTail;
        if( nPath==nOrig ){
          zTail = file_tail(zPath);
        }else{
          zTail = &zPath[nOrig+1];
        }
        db_multi_exec(
          "INSERT INTO mv VALUES('%s','%s%s')",
          zPath, blob_str(&dest), zTail
        );
      }
      db_finalize(&q);
    }
  }
  db_prepare(&q, "SELECT f, t FROM mv ORDER BY f");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFrom = db_column_text(&q, 0);
    const char *zTo = db_column_text(&q, 1);
    mv_one_file(vid, zFrom, zTo);
  }
  db_finalize(&q);
  db_end_transaction(0);
}
