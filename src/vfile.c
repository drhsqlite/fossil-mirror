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
** Procedures for managing the VFILE table.
*/
#include "config.h"
#include "vfile.h"
#include <assert.h>
#include <sys/types.h>
#include <dirent.h>

/*
** Given a UUID, return the corresponding record ID.  If the UUID
** does not exist, then return 0.
**
** For this routine, the UUID must be exact.  For a match against
** user input with mixed case, use resolve_uuid().
**
** If the UUID is not found and phantomize is 1, then attempt to 
** create a phantom record.
*/
int uuid_to_rid(const char *zUuid, int phantomize){
  int rid, sz;
  char z[UUID_SIZE];
  
  sz = strlen(zUuid);
  if( sz!=UUID_SIZE || !validate16(zUuid, sz) ){
    return 0;
  }
  strcpy(z, zUuid);
  canonical16(z, sz);
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q", z);
  if( rid==0 && phantomize ){
    rid = content_put(0, zUuid);
  }
  return rid;
}

/*
** Build a catalog of all files in a baseline.
** We scan the baseline file for lines of the form:
**
**     F NAME UUID
**
** Each such line makes an entry in the VFILE table.
*/
void vfile_build(int vid, Blob *p){
  int rid;
  char *zName, *zUuid;
  Stmt ins;
  Blob line, token, name, uuid;
  int seenHeader = 0;
  db_begin_transaction();
  db_multi_exec("DELETE FROM vfile WHERE vid=%d", vid);
  db_prepare(&ins,
    "INSERT INTO vfile(vid,rid,mrid,pathname) "
    " VALUES(:vid,:id,:id,:name)");
  db_bind_int(&ins, ":vid", vid);
  while( blob_line(p, &line) ){
    char *z = blob_buffer(&line);
    if( z[0]=='-' ){
      if( seenHeader ) break;
      while( blob_line(p, &line)>1 ){}
      if( blob_line(p, &line)==0 ) break;
    }
    seenHeader = 1;
    if( z[0]!='F' || z[1]!=' ' ) continue;
    blob_token(&line, &token);  /* Skip the "F" token */
    if( blob_token(&line, &name)==0 ) break;
    if( blob_token(&line, &uuid)==0 ) break;
    zName = blob_str(&name);
    defossilize(zName);
    zUuid = blob_str(&uuid);
    rid = uuid_to_rid(zUuid, 0);
    if( rid>0 && file_is_simple_pathname(zName) ){
      db_bind_int(&ins, ":id", rid);
      db_bind_text(&ins, ":name", zName);
      db_step(&ins);
      db_reset(&ins);
    }
    blob_reset(&name);
    blob_reset(&uuid);
  }
  db_finalize(&ins);
  db_end_transaction(0);
}

/*
** Check the file signature of the disk image for every VFILE of vid.
**
** Set the VFILE.CHNGED field on every file that has changed.  Also 
** set VFILE.CHNGED on every folder that contains a file or folder 
** that has changed.
**
** If VFILE.DELETED is null or if VFILE.RID is zero, then we can assume
** the file has changed without having the check the on-disk image.
*/
void vfile_check_signature(int vid){
  Stmt q;
  Blob fileCksum, origCksum;

  db_begin_transaction();
  db_prepare(&q, "SELECT id, %Q || pathname,"
                 "       vfile.mrid, deleted, chnged, uuid"
                 "  FROM vfile LEFT JOIN blob ON vfile.mrid=blob.rid"
                 " WHERE vid=%d ", g.zLocalRoot, vid);
  while( db_step(&q)==SQLITE_ROW ){
    int id, rid, isDeleted;
    const char *zName;
    int chnged = 0;
    int oldChnged;

    id = db_column_int(&q, 0);
    zName = db_column_text(&q, 1);
    rid = db_column_int(&q, 2);
    isDeleted = db_column_int(&q, 3);
    oldChnged = db_column_int(&q, 4);
    if( oldChnged>=2 ){
      chnged = oldChnged;
    }else if( isDeleted || rid==0 ){
      chnged = 1;
    }
    if( chnged!=1 ){
      db_ephemeral_blob(&q, 5, &origCksum);
      if( sha1sum_file(zName, &fileCksum) ){
        blob_zero(&fileCksum);
      }
      if( blob_compare(&fileCksum, &origCksum) ){
        chnged = 1;
      }
      blob_reset(&origCksum);
      blob_reset(&fileCksum);
    }
    if( chnged!=oldChnged ){
      db_multi_exec("UPDATE vfile SET chnged=%d WHERE id=%d", chnged, id);
    }
  }
  db_finalize(&q);
  db_end_transaction(0);
}

/*
** Write all files from vid to the disk.  Or if vid==0 and id!=0
** write just the specific file where VFILE.ID=id.
*/
void vfile_to_disk(int vid, int id, int verbose){
  Stmt q;
  Blob content;
  int nRepos = strlen(g.zLocalRoot);

  if( vid>0 && id==0 ){
    db_prepare(&q, "SELECT id, %Q || pathname, mrid"
                   "  FROM vfile"
                   " WHERE vid=%d AND mrid>0",
                   g.zLocalRoot, vid);
  }else{
    assert( vid==0 && id>0 );
    db_prepare(&q, "SELECT id, %Q || pathname, mrid"
                   "  FROM vfile"
                   " WHERE id=%d AND mrid>0",
                   g.zLocalRoot, id);
  }
  while( db_step(&q)==SQLITE_ROW ){
    int id, rid;
    const char *zName;

    id = db_column_int(&q, 0);
    zName = db_column_text(&q, 1);
    rid = db_column_int(&q, 2);
    content_get(rid, &content);
    if( verbose ) printf("%s\n", &zName[nRepos]);
    blob_write_to_file(&content, zName);
  }
  db_finalize(&q);
}


/*
** Delete from the disk every file in VFILE vid.
*/
void vfile_unlink(int vid){
  Stmt q;
  db_prepare(&q, "SELECT %Q || pathname FROM vfile"
                 " WHERE vid=%d AND mrid>0", g.zLocalRoot, vid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName;

    zName = db_column_text(&q, 0);
    unlink(zName);
  }
  db_finalize(&q);
}

/*
** Load into table SFILE the name of every ordinary file in
** the directory pPath.  Subdirectories are scanned recursively.
** Omit files named in VFILE.vid
*/
void vfile_scan(int vid, Blob *pPath){
  DIR *d;
  int origSize;
  const char *zDir;
  const char *zFormat;
  struct dirent *pEntry;
  static const char *zSql = "SELECT 1 FROM vfile "
                            " WHERE pathname=%B AND NOT deleted";

  origSize = blob_size(pPath);
  zDir = blob_str(pPath);
  if( zDir[0]==0 ){
     zDir = ".";
     zFormat = "%s";
  }else{
     zFormat = "/%s";
  }
  d = opendir(zDir);
  if( d ){
    while( (pEntry=readdir(d))!=0 ){
      char *zPath;
      if( pEntry->d_name[0]=='.' ) continue;
      blob_appendf(pPath, zFormat, pEntry->d_name);
      zPath = blob_str(pPath);
      if( file_isdir(zPath)==1 ){
        vfile_scan(vid, pPath);
      }else if( file_isfile(zPath) && !db_exists(zSql,pPath) ){
        db_multi_exec("INSERT INTO sfile VALUES(%B)", pPath);
      }
      blob_resize(pPath, origSize);
    }
  }
  closedir(d);
}

/*
** Compute an aggregate MD5 checksum over the disk image of every
** file in vid.  The file names are part of the checksum.
**
** Return the resulting checksum in blob pOut.
*/
void vfile_aggregate_checksum_disk(int vid, Blob *pOut){
  FILE *in;
  Stmt q;
  char zBuf[4096];

  db_must_be_within_tree();
  db_prepare(&q, "SELECT %Q || pathname, pathname FROM vfile"
                 " WHERE NOT deleted AND vid=%d"
                 " ORDER BY pathname",
                 g.zLocalRoot, vid);
  md5sum_init();
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFullpath = db_column_text(&q, 0);
    const char *zName = db_column_text(&q, 1);
    md5sum_step_text(zName, -1);
    in = fopen(zFullpath,"rb");
    if( in==0 ){
      md5sum_step_text(" 0\n", -1);
      continue;
    }
    fseek(in, 0L, SEEK_END);
    sprintf(zBuf, " %ld\n", ftell(in));
    fseek(in, 0L, SEEK_SET);
    md5sum_step_text(zBuf, -1);
    for(;;){
      int n;
      n = fread(zBuf, 1, sizeof(zBuf), in);
      if( n<=0 ) break;
      md5sum_step_text(zBuf, n);
    }
    fclose(in);
  }
  db_finalize(&q);
  md5sum_finish(pOut);
}

/*
** Compute an aggregate MD5 checksum over the repository image of every
** file in vid.  The file names are part of the checksum.
**
** Return the resulting checksum in blob pOut.
*/
void vfile_aggregate_checksum_repository(int vid, Blob *pOut){
  Blob file;
  Stmt q;
  char zBuf[100];

  db_must_be_within_tree();
  db_prepare(&q, "SELECT pathname, rid FROM vfile"
                 " WHERE NOT deleted AND rid>0 AND vid=%d"
                 " ORDER BY pathname",
                 vid);
  blob_zero(&file);
  md5sum_init();
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    md5sum_step_text(zName, -1);
    content_get(rid, &file);
    sprintf(zBuf, " %d\n", blob_size(&file));
    md5sum_step_text(zBuf, -1);
    md5sum_step_blob(&file);
    blob_reset(&file);
  }
  db_finalize(&q);
  md5sum_finish(pOut);
}

/*
** COMMAND: test-agg-cksum
*/
void test_agg_cksum_cmd(void){
  int vid;
  Blob hash;
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  vfile_aggregate_checksum_disk(vid, &hash);
  printf("disk:    %s\n", blob_str(&hash));
  blob_reset(&hash);
  vfile_aggregate_checksum_repository(vid, &hash);
  printf("archive: %s\n", blob_str(&hash));
}
