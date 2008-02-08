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
  int i;
  char *z;
  blob_appendf(pBlob, "\"%s\"", zIn);
  z = blob_buffer(pBlob);
  for(i=n+1; i<=n+k; i++){
    if( z[i]=='"' ) z[i] = '_';
  }
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
  const char *zFile, *zRevision;
  Blob cmd;
  Blob fname;
  Blob vname;
  Blob record;
  int cnt=0,internalDiff;

  internalDiff = find_option("internal","i",0)!=0;
  zRevision = find_option("revision", "r", 1);
  verify_all_options();

  if( g.argc<3 ){
    usage("?OPTIONS? FILE");
  }
  db_must_be_within_tree();

  if( internalDiff==0 ){
    const char *zExternalCommand;
    if( strcmp(g.argv[1], "diff")==0 ){
      zExternalCommand = db_get("diff-command", 0);
    }else{
      zExternalCommand = db_get("gdiff-command", 0);
    }
    if( zExternalCommand==0 ){
      internalDiff=1;
    }
    blob_zero(&cmd);
    blob_appendf(&cmd, "%s ", zExternalCommand);
  }
  zFile = g.argv[g.argc-1];
  file_tree_name(zFile, &fname);

  blob_zero(&vname);
  do{
    blob_reset(&vname);
    blob_appendf(&vname, "%s~%d", zFile, cnt++);
  }while( access(blob_str(&vname),0)==0 );

  if( zRevision==0 ){
    int rid = db_int(0, "SELECT rid FROM vfile WHERE pathname=%B", &fname);
    if( rid==0 ){
      fossil_panic("no history for file: %b", &fname);
    }
    content_get(rid, &record);
  }else{
    historical_version_of_file(zRevision, zFile, &record);
  }
  if( internalDiff==1 ){
    Blob out;
    Blob current;
    blob_zero(&current);
    blob_read_from_file(&current, zFile);
    blob_zero(&out);
    text_diff(&record, &current, &out, 5);
    printf("%s\n", blob_str(&out));
    blob_reset(&current);
    blob_reset(&out);
  }else{
    blob_write_to_file(&record, blob_str(&vname));
    blob_reset(&record);
    shell_escape(&cmd, blob_str(&vname));
    blob_appendf(&cmd, " ");
    shell_escape(&cmd, zFile);
    system(blob_str(&cmd));
    unlink(blob_str(&vname));
    blob_reset(&vname);
    blob_reset(&cmd);
  }
  blob_reset(&fname);
}
