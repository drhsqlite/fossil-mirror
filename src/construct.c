/*
** Copyright (c) 2007 Andreas Kupries
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
**   akupries@shaw.ca
**   
**
*******************************************************************************
**
** This file contains code used to de- and reconstruct a repository
** into and from an indicated directory.
*/
#include "config.h"
#include "construct.h"
#include <assert.h>
#include <sys/types.h>
#include <dirent.h>

/* This function recursively scans the directory hierarchy starting at
** zOrigin and enters all found files into the repository. The uuid is
** generated from the file contents, and not taken from the, possibly
** modified, file name. While function is able to handle the directory
** structure created by 'deconstruct' it can actually much more.
*/

static int import_origin(const char* zOrigin){
  DIR *d;
  int count = 0;
  const char *zFormat;
  const char *zDir = zOrigin;
  struct dirent *pEntry;

  if( zDir[0]==0 ){
     zDir = ".";
     zFormat = "%s%s";
  }else{
     zFormat = "%s/%s";
  }

  d = opendir(zDir);
  if( d ){
    while( (pEntry=readdir(d))!=0 ){
      char *zPath;
      if( pEntry->d_name[0]=='.' ) continue;
      zPath = mprintf(zFormat, zOrigin, pEntry->d_name);
      if( file_isdir(zPath)==1 ){
        count += import_origin(zPath);
      }else if( file_isfile(zPath) ){
	Blob zIn;
	blob_read_from_file (&zIn,zPath);
	content_put (&zIn, 0, 0);
	blob_reset (&zIn);
	count++;
      }
      free (zPath);
    }
  }
  closedir(d);
  return count;
}

/*
** COMMAND: deconstruct 
** Usage %fossil deconstruct ?-R|--repository REPOSITORY? DESTINATION
**
** Populates the indicated DESTINATION directory with copies of all
** files contained within the repository.  Files are named AA/bbbbb
** where AA is the first 2 characters of the uuid and bbbbb is the
** remaining 38 characters.
*/

void deconstruct_cmd(void){
  const char* zDestination;
  Blob zOut;
  Stmt q;
  if( (g.argc != 3) && (g.argc != 5) ){
    usage ("?-R|--repository REPOSITORY? DESTINATION");
  }
  db_find_and_open_repository ();
  zDestination = g.argv[g.argc-1];
  if( !file_isdir (zDestination) ){
    fossil_panic("not a directory: %s", zDestination);
  }
  /* Iterate over all blobs in the repository, retrieve their
   * contents, and write them to a file with a name based on their
   * uuid. Note: Non-writable destination causes bail-out in the first
   * call of blob_write_to_file.
   */
  db_prepare(&q, "SELECT rid,uuid FROM blob");
  while( db_step(&q)==SQLITE_ROW ){
    int         rid   = db_column_int (&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    char       *zFile = mprintf ("%s/%.2s/%s", zDestination, zUuid, zUuid + 2);
    content_get (rid,&zOut);
    blob_write_to_file (&zOut,zFile);
    blob_reset (&zOut);
    free(zFile);
  }
  db_finalize(&q);
}

/*
** COMMAND: reconstruct 
** Usage %fossil reconstruct REPOSITORY ORIGIN
**
** Creates the REPOSITORY and populates it with the files in the
** indicated ORIGIN directory.
*/

void reconstruct_cmd(void){
  const char* zOrigin;
  const char* zRepository;
  int fileCnt;
  int errCnt;

  if( g.argc != 4 ){
    usage ("REPOSITORY ORIGIN");
  }
  zRepository = g.argv[2];
  zOrigin     = g.argv[3];
  if( !file_isdir (zOrigin) ){
    fossil_panic("not a directory: %s", zOrigin);
  }

  /* Create the foundation */
  db_create_repository(zRepository);
  db_open_repository(zRepository);
  db_open_config();
  db_begin_transaction();

  db_initial_setup(0, 1);

  printf("project-id: %s\n", db_get("project-code", 0));
  printf("server-id:  %s\n", db_get("server-code", 0));
  printf("admin-user: %s (no password set yet!)\n", g.zLogin);
  printf("baseline:   %s\n", db_text(0, "SELECT uuid FROM blob"));

  /* Scan origin and insert all files found inside */
  fileCnt = import_origin (zOrigin);

  printf("imported:   %d %s\n", fileCnt, fileCnt == 1 ?
	 "file" : "files");

  /* Finalize the repository, rebuild the derived tables */
  errCnt = rebuild_db(0);

  if( errCnt ){
    printf("%d %s. Rolling back changes.\n", errCnt, errCnt == 1 ?
	   "error" : "errors");
    db_end_transaction(1);
  }else{
    db_end_transaction(0);
  }
}
