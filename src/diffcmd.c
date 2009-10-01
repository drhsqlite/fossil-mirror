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
** This file contains code used to implement the "diff" command
*/
#include "config.h"
#include "diffcmd.h"
#include <assert.h>

/*
** Shell-escape the given string.  Append the result to a blob.
*/
static void shell_escape(Blob *pBlob, const char *zIn){
  int n = blob_size(pBlob);
  int k = strlen(zIn);
  int i, c;
  char *z;
  for(i=0; (c = zIn[i])!=0; i++){
    if( isspace(c) || c=='"' || (c=='\\' && zIn[i+1]!=0) ){
      blob_appendf(pBlob, "\"%s\"", zIn);
      z = blob_buffer(pBlob);
      for(i=n+1; i<=n+k; i++){
        if( z[i]=='"' ) z[i] = '_';
      }
      return;
    }
  }
  blob_append(pBlob, zIn, -1);
}

/*
** Run the fossil diff command separately for every file in the current
** checkout that has changed.
*/
static void diff_all(int internalDiff,  const char *zRevision){
  Stmt q;
  Blob cmd;
  int nCmdBase;
  int vid;
  
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid);
  blob_zero(&cmd);
  shell_escape(&cmd, g.argv[0]);
  blob_append(&cmd, " diff ", -1);
  if( internalDiff ){
    blob_append(&cmd, "-i ", -1);
  }
  if( zRevision ){
    blob_append(&cmd, "-r ", -1);
    shell_escape(&cmd, zRevision);
    blob_append(&cmd, " ", 1);
  }
  nCmdBase = blob_size(&cmd);
  db_prepare(&q, 
    "SELECT pathname, deleted, chnged, rid FROM vfile "
    "WHERE chnged OR deleted OR rid=0 ORDER BY 1"
  );

  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3)==0;
    char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    cmd.nUsed = nCmdBase;
    if( isDeleted ){
      printf("DELETED  %s\n", zPathname);
    }else if( access(zFullName, 0) ){
      printf("MISSING  %s\n", zPathname);
    }else if( isNew ){
      printf("ADDED    %s\n", zPathname);
    }else if( isDeleted ){
      printf("DELETED  %s\n", zPathname);
    }else if( isChnged==3 ){
      printf("ADDED_BY_MERGE %s\n", zPathname);
    }else{
      printf("Index: %s\n======================================="
             "============================\n",
             zPathname
      );
      shell_escape(&cmd, zFullName);
      printf("%s\n", blob_str(&cmd));
      fflush(stdout);
      portable_system(blob_str(&cmd));
    }
    free(zFullName);
  }
  db_finalize(&q);
}

/*
** COMMAND: diff
** COMMAND: gdiff
**
** Usage: %fossil diff|gdiff ?-i? ?-r REVISION? FILE...
**
** Show the difference between the current version of a file (as it
** exists on disk) and that same file as it was checked out.
**
** diff will show a textual diff while gdiff will attempt to run a
** graphical diff command that you have setup. If the choosen command
** is not yet configured, the internal textual diff command will be
** used.
**
** If -i is supplied for either diff or gdiff, the internal textual
** diff command will be executed.
**
** Here are a few external diff command settings, for example:
**
**   %fossil setting diff-command diff
**
**   %fossil setting gdiff-command tkdiff
**   %fossil setting gdiff-command eskill22
**   %fossil setting gdiff-command tortoisemerge
**   %fossil setting gdiff-command meld
**   %fossil setting gdiff-command xxdiff
**   %fossil setting gdiff-command kdiff3
*/
void diff_cmd(void){
  int isGDiff;               /* True for gdiff.  False for normal diff */
  const char *zFile;         /* Name of file to diff */
  const char *zRevision;     /* Version of file to diff against current */
  Blob cmd;                  /* The diff command-line for external diff */
  Blob fname;                /* */
  Blob vname;
  Blob record;
  int cnt=0;
  int internalDiff;          /* True to use the internal diff engine */

  isGDiff = g.argv[1][0]=='g';
  internalDiff = find_option("internal","i",0)!=0;
  zRevision = find_option("revision", "r", 1);
  verify_all_options();
  db_must_be_within_tree();

  if( !isGDiff && g.argc==2 ){
    diff_all(internalDiff, zRevision);
    return;
  }
  if( g.argc<3 ){
    usage("?OPTIONS? FILE");
  }

  if( internalDiff==0 ){
    const char *zExternalCommand;
    if( !isGDiff ){
      zExternalCommand = db_get("diff-command", 0);
    }else{
      zExternalCommand = db_get("gdiff-command", 0);
    }
    if( zExternalCommand==0 ){
      internalDiff=1;
    }else{
      blob_zero(&cmd);
      blob_appendf(&cmd,"%s ",zExternalCommand);
    }
  }
  zFile = g.argv[g.argc-1];
  file_tree_name(zFile, &fname, 1);

  blob_zero(&vname);
  do{
    blob_reset(&vname);
    blob_appendf(&vname, "%s~%d", zFile, cnt++);
  }while( access(blob_str(&vname),0)==0 );

  if( zRevision==0 ){
    int rid = db_int(0, "SELECT rid FROM vfile WHERE pathname=%B", &fname);
    if( rid==0 ){
      fossil_fatal("no history for file: %b", &fname);
    }
    content_get(rid, &record);
  }else{
    historical_version_of_file(zRevision, blob_str(&fname), &record);
  }
  if( internalDiff ){
    Blob out;
    Blob current;
    blob_zero(&current);
    blob_read_from_file(&current, zFile);
    blob_zero(&out);
    text_diff(&record, &current, &out, 5);
    printf("--- %s\n+++ %s\n", blob_str(&fname), blob_str(&fname));
    printf("%s\n", blob_str(&out));
    blob_reset(&current);
    blob_reset(&out);
  }else{
    blob_write_to_file(&record, blob_str(&vname));
    blob_reset(&record);
    blob_appendf(&cmd, "%s ", blob_str(&vname));
    shell_escape(&cmd, zFile);
    portable_system(blob_str(&cmd));
    unlink(blob_str(&vname));
    blob_reset(&vname);
    blob_reset(&cmd);
  }
  blob_reset(&fname);
}

/*
** This function implements a cross-platform "system()" interface.
*/
int portable_system(char *zOrigCmd){
  int rc;
#ifdef __MINGW32__
  /* On windows, we have to put double-quotes around the entire command.
  ** Who knows why - this is just the way windows works.
  */
  char *zNewCmd = mprintf("\"%s\"", zOrigCmd);
  rc = system(zNewCmd);
  free(zNewCmd);
#else
  /* On unix, evaluate the command directly.
  */
  rc = system(zOrigCmd);
#endif 
  return rc; 
}
