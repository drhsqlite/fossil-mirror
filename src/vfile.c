/*
** Copyright (c) 2007 D. Richard Hipp
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
** Procedures for managing the VFILE table.
*/
#include "config.h"
#include "vfile.h"
#include <assert.h>
#include <sys/types.h>
#if defined(__DMC__)
#include "dirent.h"
#else
#include <dirent.h>
#endif

/*
** The input is guaranteed to be a 40-character well-formed UUID.
** Find its rid.
*/
int fast_uuid_to_rid(const char *zUuid){
  static Stmt q;
  int rid;
  db_static_prepare(&q, "SELECT rid FROM blob WHERE uuid=:uuid");
  db_bind_text(&q, ":uuid", zUuid);
  if( db_step(&q)==SQLITE_ROW ){
    rid = db_column_int(&q, 0);
  }else{
    rid = 0;
  }
  db_reset(&q);
  return rid;
}

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
  char z[UUID_SIZE+1];
  
  sz = strlen(zUuid);
  if( sz!=UUID_SIZE || !validate16(zUuid, sz) ){
    return 0;
  }
  memcpy(z, zUuid, UUID_SIZE+1);
  canonical16(z, sz);
  rid = fast_uuid_to_rid(z);
  if( rid==0 && phantomize ){
    rid = content_new(zUuid);
  }
  return rid;
}

/*
** Verify that an object is not a phantom.  If the object is
** a phantom, output an error message and quick.
*/
static void vfile_verify_not_phantom(
  int rid,                  /* The RID to verify */
  const char *zFilename,    /* Filename.  Might be NULL */
  const char *zUuid         /* UUID.  Might be NULL */
){
  if( db_int(-1, "SELECT size FROM blob WHERE rid=%d", rid)<0
      && (zUuid==0 || !db_exists("SELECT 1 FROM shun WHERE uuid='%s'", zUuid)) ){
    if( zFilename ){
      fossil_fatal("content missing for %s", zFilename);
    }else{
      char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      if( zUuid ){
        fossil_fatal("content missing for [%.10s]", zUuid);
      }else{
        fossil_panic("bad object id: %d", rid);
      }
    }
  }
}

/*
** Build a catalog of all files in a checkin.
*/
void vfile_build(int vid){
  int rid;
  Stmt ins;
  Manifest *p;
  ManifestFile *pFile;

  db_begin_transaction();
  vfile_verify_not_phantom(vid, 0, 0);
  p = manifest_get(vid, CFTYPE_MANIFEST);
  if( p==0 ) return;
  db_multi_exec("DELETE FROM vfile WHERE vid=%d", vid);
  db_prepare(&ins,
    "INSERT INTO vfile(vid,rid,mrid,pathname) "
    " VALUES(:vid,:id,:id,:name)");
  db_bind_int(&ins, ":vid", vid);
  manifest_file_rewind(p);
  while( (pFile = manifest_file_next(p,0))!=0 ){
    rid = uuid_to_rid(pFile->zUuid, 0);
    vfile_verify_not_phantom(rid, pFile->zName, pFile->zUuid);
    db_bind_int(&ins, ":id", rid);
    db_bind_text(&ins, ":name", pFile->zName);
    db_step(&ins);
    db_reset(&ins);
  }
  db_finalize(&ins);
  manifest_destroy(p);
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
**
** If the size of the file has changed, then we assume that it has
** changed.  If the mtime of the file has not changed and useSha1sum is false
** and the mtime-changes setting is true (the default) then we assume that
** the file has not changed.  If the mtime has changed, we go ahead and
** double-check that the file has changed by looking at its SHA1 sum.
*/
void vfile_check_signature(int vid, int notFileIsFatal, int useSha1sum){
  int nErr = 0;
  Stmt q;
  Blob fileCksum, origCksum;
  int checkMtime = useSha1sum==0 && db_get_boolean("mtime-changes", 1);

  db_begin_transaction();
  db_prepare(&q, "SELECT id, %Q || pathname,"
                 "       vfile.mrid, deleted, chnged, uuid, size, mtime"
                 "  FROM vfile LEFT JOIN blob ON vfile.mrid=blob.rid"
                 " WHERE vid=%d ", g.zLocalRoot, vid);
  while( db_step(&q)==SQLITE_ROW ){
    int id, rid, isDeleted;
    const char *zName;
    int chnged = 0;
    int oldChnged;
    i64 oldMtime;
    i64 currentMtime;

    id = db_column_int(&q, 0);
    zName = db_column_text(&q, 1);
    rid = db_column_int(&q, 2);
    isDeleted = db_column_int(&q, 3);
    oldChnged = db_column_int(&q, 4);
    oldMtime = db_column_int64(&q, 7);
    if( isDeleted ){
      chnged = 1;
    }else if( !file_isfile(zName) && file_size(0)>=0 ){
      if( notFileIsFatal ){
        fossil_warning("not an ordinary file: %s", zName);
        nErr++;
      }
      chnged = 1;
    }else if( oldChnged>=2 ){
      chnged = oldChnged;
    }else if( rid==0 ){
      chnged = 1;
    }
    if( chnged!=1 ){
      i64 origSize = db_column_int64(&q, 6);
      currentMtime = file_mtime(0);
      if( origSize!=file_size(0) ){
        /* A file size change is definitive - the file has changed.  No
        ** need to check the sha1sum */
        chnged = 1;
      }
    }
    if( chnged!=1 && (checkMtime==0 || currentMtime!=oldMtime) ){
      db_ephemeral_blob(&q, 5, &origCksum);
      if( sha1sum_file(zName, &fileCksum) ){
        blob_zero(&fileCksum);
      }
      if( blob_compare(&fileCksum, &origCksum) ){
        chnged = 1;
      }else if( currentMtime!=oldMtime ){
        db_multi_exec("UPDATE vfile SET mtime=%lld WHERE id=%d",
                      currentMtime, id);
      }
      blob_reset(&origCksum);
      blob_reset(&fileCksum);
    }
    if( chnged!=oldChnged && (chnged || !checkMtime) ){
      db_multi_exec("UPDATE vfile SET chnged=%d WHERE id=%d", chnged, id);
    }
  }
  db_finalize(&q);
  if( nErr ) fossil_fatal("abort due to prior errors");
  db_end_transaction(0);
}

/*
** Write all files from vid to the disk.  Or if vid==0 and id!=0
** write just the specific file where VFILE.ID=id.
*/
void vfile_to_disk(
  int vid,               /* vid to write to disk */
  int id,                /* Write this one file, if not zero */
  int verbose,           /* Output progress information */
  int promptFlag         /* Prompt user to confirm overwrites */
){
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
    if( file_is_the_same(&content, zName) ){
      blob_reset(&content);
      continue;
    }
    if( promptFlag && file_size(zName)>=0 ){
      Blob ans;
      char *zMsg;
      char cReply;
      zMsg = mprintf("overwrite %s (a=always/y/N)? ", zName);
      prompt_user(zMsg, &ans);
      free(zMsg);
      cReply = blob_str(&ans)[0];
      blob_reset(&ans);
      if( cReply=='a' || cReply=='A' ){
        promptFlag = 0;
        cReply = 'y';
      }
      if( cReply=='n' || cReply=='N' ){
        blob_reset(&content);
        continue;
      }
    }
    if( verbose ) printf("%s\n", &zName[nRepos]);
    blob_write_to_file(&content, zName);
    blob_reset(&content);
    db_multi_exec("UPDATE vfile SET mtime=%lld WHERE id=%d",
                  file_mtime(zName), id);
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
  db_multi_exec("UPDATE vfile SET mtime=NULL WHERE vid=%d AND mrid>0", vid);
}

/*
** Load into table SFILE the name of every ordinary file in
** the directory pPath.   Omit the first nPrefix characters of
** of pPath when inserting into the SFILE table.
**
** Subdirectories are scanned recursively.
** Omit files named in VFILE.vid
*/
void vfile_scan(int vid, Blob *pPath, int nPrefix, int allFlag){
  DIR *d;
  int origSize;
  const char *zDir;
  struct dirent *pEntry;
  static const char *zSql = "SELECT 1 FROM vfile "
                            " WHERE pathname=%Q AND NOT deleted";

  origSize = blob_size(pPath);
  zDir = blob_str(pPath);
  d = opendir(zDir);
  if( d ){
    while( (pEntry=readdir(d))!=0 ){
      char *zPath;
      if( pEntry->d_name[0]=='.' ){
        if( !allFlag ) continue;
        if( pEntry->d_name[1]==0 ) continue;
        if( pEntry->d_name[1]=='.' && pEntry->d_name[2]==0 ) continue;
      }
      blob_appendf(pPath, "/%s", pEntry->d_name);
      zPath = blob_str(pPath);
      if( file_isdir(zPath)==1 ){
        vfile_scan(vid, pPath, nPrefix, allFlag);
      }else if( file_isfile(zPath) && !db_exists(zSql, &zPath[nPrefix+1]) ){
        db_multi_exec("INSERT INTO sfile VALUES(%Q)", &zPath[nPrefix+1]);
      }
      blob_resize(pPath, origSize);
    }
  }
  closedir(d);
}

/*
** Compute an aggregate MD5 checksum over the disk image of every
** file in vid.  The file names are part of the checksum.  The resulting
** checksum is the same as is expected on the R-card of a manifest.
**
** This function operates differently if the Global.aCommitFile
** variable is not NULL. In that case, the disk image is used for
** each file in aCommitFile[] and the repository image
** is used for all others).
**
** Newly added files that are not contained in the repository are
** omitted from the checksum if they are not in Global.aCommitFile[].
**
** Newly deleted files are included in the checksum if they are not
** part of Global.aCommitFile[]
**
** Renamed files use their new name if they are in Global.aCommitFile[]
** and their original name if they are not in Global.aCommitFile[]
**
** Return the resulting checksum in blob pOut.
*/
void vfile_aggregate_checksum_disk(int vid, Blob *pOut){
  FILE *in;
  Stmt q;
  char zBuf[4096];

  db_must_be_within_tree();
  db_prepare(&q, 
      "SELECT %Q || pathname, pathname, origname, file_is_selected(id), rid"
      "  FROM vfile"
      " WHERE (NOT deleted OR NOT file_is_selected(id)) AND vid=%d"
      " ORDER BY pathname /*scan*/",
      g.zLocalRoot, vid
  );
  md5sum_init();
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFullpath = db_column_text(&q, 0);
    const char *zName = db_column_text(&q, 1);
    int isSelected = db_column_int(&q, 3);

    if( isSelected ){
      md5sum_step_text(zName, -1);
      in = fopen(zFullpath,"rb");
      if( in==0 ){
        md5sum_step_text(" 0\n", -1);
        continue;
      }
      fseek(in, 0L, SEEK_END);
      sqlite3_snprintf(sizeof(zBuf), zBuf, " %ld\n", ftell(in));
      fseek(in, 0L, SEEK_SET);
      md5sum_step_text(zBuf, -1);
      /*printf("%s %s %s",md5sum_current_state(),zName,zBuf); fflush(stdout);*/
      for(;;){
        int n;
        n = fread(zBuf, 1, sizeof(zBuf), in);
        if( n<=0 ) break;
        md5sum_step_text(zBuf, n);
      }
      fclose(in);
    }else{
      int rid = db_column_int(&q, 4);
      const char *zOrigName = db_column_text(&q, 2);
      char zBuf[100];
      Blob file;

      if( zOrigName ) zName = zOrigName;
      if( rid>0 ){
        md5sum_step_text(zName, -1);
        blob_zero(&file);
        content_get(rid, &file);
        sqlite3_snprintf(sizeof(zBuf), zBuf, " %d\n", blob_size(&file));
        md5sum_step_text(zBuf, -1);
        md5sum_step_blob(&file);
        blob_reset(&file);
      }
    }
  }
  db_finalize(&q);
  md5sum_finish(pOut);
}

/*
** Do a file-by-file comparison of the content of the repository and
** the working check-out on disk.  Report any errors.
*/
void vfile_compare_repository_to_disk(int vid){
  int rc;
  Stmt q;
  Blob disk, repo;
  
  db_must_be_within_tree();
  db_prepare(&q, 
      "SELECT %Q || pathname, pathname, rid FROM vfile"
      " WHERE NOT deleted AND vid=%d AND file_is_selected(id)",
      g.zLocalRoot, vid
  );
  md5sum_init();
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFullpath = db_column_text(&q, 0);
    const char *zName = db_column_text(&q, 1);
    int rid = db_column_int(&q, 2);

    blob_zero(&disk);
    rc = blob_read_from_file(&disk, zFullpath);
    if( rc<0 ){
      printf("ERROR: cannot read file [%s]\n", zFullpath);
      blob_reset(&disk);
      continue;
    }
    blob_zero(&repo);
    content_get(rid, &repo);
    if( blob_size(&repo)!=blob_size(&disk) ){
      printf("ERROR: [%s] is %d bytes on disk but %d in the repository\n",
             zName, blob_size(&disk), blob_size(&repo));
      blob_reset(&disk);
      blob_reset(&repo);
      continue;
    }
    if( blob_compare(&repo, &disk) ){
      printf("ERROR: [%s] is different on disk compared to the repository\n",
             zName);
    }
    blob_reset(&disk);
    blob_reset(&repo);
  }
  db_finalize(&q);
}

/*
** Compute an aggregate MD5 checksum over the repository image of every
** file in vid.  The file names are part of the checksum.  The resulting
** checksum is suitable for the R-card of a manifest.
**
** Return the resulting checksum in blob pOut.
*/
void vfile_aggregate_checksum_repository(int vid, Blob *pOut){
  Blob file;
  Stmt q;
  char zBuf[100];

  db_must_be_within_tree();
 
  db_prepare(&q, "SELECT pathname, origname, rid, file_is_selected(id)"
                 " FROM vfile"
                 " WHERE (NOT deleted OR NOT file_is_selected(id))"
                 "   AND rid>0 AND vid=%d"
                 " ORDER BY pathname /*scan*/",
                 vid);
  blob_zero(&file);
  md5sum_init();
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zOrigName = db_column_text(&q, 1);
    int rid = db_column_int(&q, 2);
    int isSelected = db_column_int(&q, 3);
    if( zOrigName && !isSelected ) zName = zOrigName;
    md5sum_step_text(zName, -1);
    content_get(rid, &file);
    sqlite3_snprintf(sizeof(zBuf), zBuf, " %d\n", blob_size(&file));
    md5sum_step_text(zBuf, -1);
    /*printf("%s %s %s",md5sum_current_state(),zName,zBuf); fflush(stdout);*/
    md5sum_step_blob(&file);
    blob_reset(&file);
  }
  db_finalize(&q);
  md5sum_finish(pOut);
}

/*
** Compute an aggregate MD5 checksum over the repository image of every
** file in manifest vid.  The file names are part of the checksum.  The
** resulting checksum is suitable for use as the R-card of a manifest.
**
** Return the resulting checksum in blob pOut.
**
** If pManOut is not NULL then fill it with the checksum found in the
** "R" card near the end of the manifest.
**
** In a well-formed manifest, the two checksums computed here, pOut and
** pManOut, should be identical.  
*/
void vfile_aggregate_checksum_manifest(int vid, Blob *pOut, Blob *pManOut){
  int fid;
  Blob file;
  Manifest *pManifest;
  ManifestFile *pFile;
  char zBuf[100];

  blob_zero(pOut);
  if( pManOut ){
    blob_zero(pManOut);
  }
  db_must_be_within_tree();
  pManifest = manifest_get(vid, CFTYPE_MANIFEST);
  if( pManifest==0 ){
    fossil_panic("manifest file (%d) is malformed", vid);
  }
  manifest_file_rewind(pManifest);
  while( (pFile = manifest_file_next(pManifest,0))!=0 ){
    fid = uuid_to_rid(pFile->zUuid, 0);
    md5sum_step_text(pFile->zName, -1);
    content_get(fid, &file);
    sqlite3_snprintf(sizeof(zBuf), zBuf, " %d\n", blob_size(&file));
    md5sum_step_text(zBuf, -1);
    md5sum_step_blob(&file);
    blob_reset(&file);
  }
  if( pManOut ){
    if( pManifest->zRepoCksum ){
      blob_append(pManOut, pManifest->zRepoCksum, -1);
    }else{
      blob_zero(pManOut);
    }
  }
  manifest_destroy(pManifest);
  md5sum_finish(pOut);
}

/*
** COMMAND: test-agg-cksum
*/
void test_agg_cksum_cmd(void){
  int vid;
  Blob hash, hash2;
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  vfile_aggregate_checksum_disk(vid, &hash);
  printf("disk:     %s\n", blob_str(&hash));
  blob_reset(&hash);
  vfile_aggregate_checksum_repository(vid, &hash);
  printf("archive:  %s\n", blob_str(&hash));
  blob_reset(&hash);
  vfile_aggregate_checksum_manifest(vid, &hash, &hash2);
  printf("manifest: %s\n", blob_str(&hash));
  printf("recorded: %s\n", blob_str(&hash2));
}
