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
** COMMAND: tkdiff
**
** Usage: %fossil diff|tkdiff FILE...
** Show the difference between the current version of a file (as it
** exists on disk) and that same file as it was checked out.  Use
** either "diff -u" or "tkdiff".
*/
void diff_cmd(void){
  const char *zFile;
  Blob cmd;
  Blob fname;
  int i;
  char *zV1 = 0;
  char *zV2 = 0;

  if( g.argc<3 ){
    usage("?OPTIONS? FILE");
  }
  db_must_be_within_tree();
  blob_zero(&cmd);
  blob_appendf(&cmd, "%s ", g.argv[1]);
  for(i=2; i<g.argc-1; i++){
    const char *z = g.argv[i];
    if( (strcmp(z,"-v")==0 || strcmp(z,"--version")==0) && i<g.argc-2 ){
      if( zV1==0 ){
        zV1 = g.argv[i+1];
      }else if( zV2==0 ){
        zV2 = g.argv[i+1];
      }else{
        fossil_panic("too many versions");
      }
    }else{
      blob_appendf(&cmd, "%s ", z);
    }
  }
  zFile = g.argv[g.argc-1];
  if( !file_tree_name(zFile, &fname) ){
    fossil_panic("unknown file: %s", zFile);
  }
  if( zV1==0 ){
    int rid = db_int(0, "SELECT rid FROM vfile WHERE pathname=%B", &fname);
    Blob record;
    Blob vname;
    int cnt = 0;

    if( rid==0 ){
      fossil_panic("no history for file: %b", &fname);
    }
    blob_zero(&vname);
    do{
      blob_reset(&vname);
      blob_appendf(&vname, "%s~%d", zFile, cnt++);
    }while( access(blob_str(&vname),0)==0 );
    content_get(rid, &record);
    blob_write_to_file(&record, blob_str(&vname));
    blob_reset(&record);
    shell_escape(&cmd, blob_str(&vname));
    blob_appendf(&cmd, " ");
    shell_escape(&cmd, zFile);
    system(blob_str(&cmd));
    unlink(blob_str(&vname));
    blob_reset(&vname);
    blob_reset(&cmd);
  }else{
    fossil_panic("not yet implemented");
  }
  blob_reset(&fname);
}
