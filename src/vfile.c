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

/*
** The input is guaranteed to be a 40- or 64-character well-formed
** artifact hash.  Find its rid.
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
** If the UUID is not found and phantomize is 1 or 2, then attempt to
** create a phantom record.  A private phantom is created for 2 and
** a public phantom is created for 1.
*/
int uuid_to_rid(const char *zUuid, int phantomize){
  int rid, sz;
  char z[HNAME_MAX+1];

  sz = strlen(zUuid);
  if( !hname_validate(zUuid, sz) ){
    return 0;  /* Not a valid hash */
  }
  memcpy(z, zUuid, sz+1);
  canonical16(z, sz);
  rid = fast_uuid_to_rid(z);
  if( rid==0 && phantomize ){
    rid = content_new(zUuid, phantomize-1);
  }
  return rid;
}


/*
** Load a vfile from a record ID.  Return the number of files with
** missing content.
*/
int load_vfile_from_rid(int vid){
  int rid, size, nMissing;
  Stmt ins, ridq;
  Manifest *p;
  ManifestFile *pFile;

  if( db_exists("SELECT 1 FROM vfile WHERE vid=%d", vid) ){
    return 0;
  }

  db_begin_transaction();
  p = manifest_get(vid, CFTYPE_MANIFEST, 0);
  if( p==0 ) {
    db_end_transaction(1);
    return 0;
  }
  db_prepare(&ins,
    "INSERT INTO vfile(vid,isexe,islink,rid,mrid,pathname) "
    " VALUES(:vid,:isexe,:islink,:id,:id,:name)");
  db_prepare(&ridq, "SELECT rid,size FROM blob WHERE uuid=:uuid");
  db_bind_int(&ins, ":vid", vid);
  manifest_file_rewind(p);
  nMissing = 0;
  while( (pFile = manifest_file_next(p,0))!=0 ){
    if( pFile->zUuid==0 || uuid_is_shunned(pFile->zUuid) ) continue;
    db_bind_text(&ridq, ":uuid", pFile->zUuid);
    if( db_step(&ridq)==SQLITE_ROW ){
      rid = db_column_int(&ridq, 0);
      size = db_column_int(&ridq, 1);
    }else{
      rid = 0;
      size = 0;
    }
    db_reset(&ridq);
    if( rid==0 || size<0 ){
      fossil_warning("content missing for %s", pFile->zName);
      nMissing++;
      continue;
    }
    db_bind_int(&ins, ":isexe", ( manifest_file_mperm(pFile)==PERM_EXE ));
    db_bind_int(&ins, ":id", rid);
    db_bind_text(&ins, ":name", pFile->zName);
    db_bind_int(&ins, ":islink", ( manifest_file_mperm(pFile)==PERM_LNK ));
    db_step(&ins);
    db_reset(&ins);
  }
  db_finalize(&ridq);
  db_finalize(&ins);
  manifest_destroy(p);
  db_end_transaction(0);
  return nMissing;
}

#if INTERFACE
/*
** The cksigFlags parameter to vfile_check_signature() is an OR-ed
** combination of the following bits:
*/
#define CKSIG_ENOTFILE  0x001   /* non-file FS objects throw an error */
#define CKSIG_HASH      0x002   /* Verify file content using hashing */
#define CKSIG_SETMTIME  0x004   /* Set mtime to last check-out time */

#endif /* INTERFACE */

/*
** Look at every VFILE entry with the given vid and update VFILE.CHNGED field
** according to whether or not the file has changed.
** - 0 means no change.
** - 1 means edited.
** - 2 means changed due to a merge.
** - 3 means added by a merge.
** - 4 means changed due to an integrate merge.
** - 5 means added by an integrate merge.
** - 6 means became executable but has unmodified contents.
** - 7 means became a symlink whose target equals its old contents.
** - 8 means lost executable status but has unmodified contents.
** - 9 means lost symlink status and has contents equal to its old target.
**
** If VFILE.DELETED is true or if VFILE.RID is zero, then the file was either
** removed from configuration management via "fossil rm" or added via
** "fossil add", respectively, and in both cases we always know that
** the file has changed without having the check the size, mtime,
** or on-disk content.
**
** If the size of the file has changed, then we always know that the file
** changed without having to look at the mtime or on-disk content.
**
** The mtime of the file is only a factor if the mtime-changes setting
** is false and the CKSIG_HASH flag is false.  If the mtime-changes
** setting is true (or undefined - it defaults to true) or if CKSIG_HASH
** is true, then we do not trust the mtime and will examine the on-disk
** content to determine if a file really is the same.
**
** If the mtime is used, it is used only to determine if files are the same.
** If the mtime of a file has changed, we still examine the on-disk content
** to see whether or not the edit was a null-edit.
*/
void vfile_check_signature(int vid, unsigned int cksigFlags){
  int nErr = 0;
  Stmt q;
  int useMtime = (cksigFlags & CKSIG_HASH)==0
                    && db_get_boolean("mtime-changes", 1);

  db_begin_transaction();
  db_prepare(&q, "SELECT id, %Q || pathname,"
                 "       vfile.mrid, deleted, chnged, uuid, size, mtime,"
                 "      CASE WHEN isexe THEN %d WHEN islink THEN %d ELSE %d END"
                 "  FROM vfile LEFT JOIN blob ON vfile.mrid=blob.rid"
                 " WHERE vid=%d ", g.zLocalRoot, PERM_EXE, PERM_LNK, PERM_REG,
                 vid);
  while( db_step(&q)==SQLITE_ROW ){
    int id, rid, isDeleted;
    const char *zName;
    int chnged = 0;
    int oldChnged;
#ifndef _WIN32
    int origPerm;
    int currentPerm;
#endif
    i64 oldMtime;
    i64 currentMtime;
    i64 origSize;
    i64 currentSize;

    id = db_column_int(&q, 0);
    zName = db_column_text(&q, 1);
    rid = db_column_int(&q, 2);
    isDeleted = db_column_int(&q, 3);
    oldChnged = chnged = db_column_int(&q, 4);
    oldMtime = db_column_int64(&q, 7);
    origSize = db_column_int64(&q, 6);
    currentSize = file_wd_size(zName);
    currentMtime = file_wd_mtime(0);
#ifndef _WIN32
    origPerm = db_column_int(&q, 8);
    currentPerm = file_wd_perm(zName);
#endif
    if( chnged==0 && (isDeleted || rid==0) ){
      /* "fossil rm" or "fossil add" always change the file */
      chnged = 1;
    }else if( !file_wd_isfile_or_link(0) && currentSize>=0 ){
      if( cksigFlags & CKSIG_ENOTFILE ){
        fossil_warning("not an ordinary file: %s", zName);
        nErr++;
      }
      chnged = 1;
    }
    if( origSize!=currentSize ){
      if( chnged!=1 ){
        /* A file size change is definitive - the file has changed.  No
        ** need to check the mtime or hash */
        chnged = 1;
      }
    }else if( chnged==1 && rid!=0 && !isDeleted ){
      /* File is believed to have changed but it is the same size.
      ** Double check that it really has changed by looking at content. */
      const char *zUuid = db_column_text(&q, 5);
      int nUuid = db_column_bytes(&q, 5);
      assert( origSize==currentSize );
      if( hname_verify_file_hash(zName, zUuid, nUuid) ) chnged = 0;
    }else if( (chnged==0 || chnged==2 || chnged==4)
           && (useMtime==0 || currentMtime!=oldMtime) ){
      /* For files that were formerly believed to be unchanged or that were
      ** changed by merging, if their mtime changes, or unconditionally
      ** if --hash is used, check to see if they have been edited by
      ** looking at their artifact hashes */
      const char *zUuid = db_column_text(&q, 5);
      int nUuid = db_column_bytes(&q, 5);
      assert( origSize==currentSize );
      if( !hname_verify_file_hash(zName, zUuid, nUuid) ) chnged = 1;
    }
    if( (cksigFlags & CKSIG_SETMTIME) && (chnged==0 || chnged==2 || chnged==4) ){
      i64 desiredMtime;
      if( mtime_of_manifest_file(vid,rid,&desiredMtime)==0 ){
        if( currentMtime!=desiredMtime ){
          file_set_mtime(zName, desiredMtime);
          currentMtime = file_wd_mtime(zName);
        }
      }
    }
#ifndef _WIN32
    if( origPerm!=PERM_LNK && currentPerm==PERM_LNK ){
       /* Changing to a symlink takes priority over all other change types. */
       chnged = 7;
    }else if( chnged==0 || chnged==6 || chnged==7 || chnged==8 || chnged==9 ){
       /* Confirm metadata change types. */
      if( origPerm==currentPerm ){
        chnged = 0;
      }else if( currentPerm==PERM_EXE ){
        chnged = 6;
      }else if( origPerm==PERM_EXE ){
        chnged = 8;
      }else if( origPerm==PERM_LNK ){
        chnged = 9;
      }
    }
#endif
    if( currentMtime!=oldMtime || chnged!=oldChnged ){
      db_multi_exec("UPDATE vfile SET mtime=%lld, chnged=%d WHERE id=%d",
                    currentMtime, chnged, id);
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
    db_prepare(&q, "SELECT id, %Q || pathname, mrid, isexe, islink"
                   "  FROM vfile"
                   " WHERE vid=%d AND mrid>0",
                   g.zLocalRoot, vid);
  }else{
    assert( vid==0 && id>0 );
    db_prepare(&q, "SELECT id, %Q || pathname, mrid, isexe, islink"
                   "  FROM vfile"
                   " WHERE id=%d AND mrid>0",
                   g.zLocalRoot, id);
  }
  while( db_step(&q)==SQLITE_ROW ){
    int id, rid, isExe, isLink;
    const char *zName;

    id = db_column_int(&q, 0);
    zName = db_column_text(&q, 1);
    rid = db_column_int(&q, 2);
    isExe = db_column_int(&q, 3);
    isLink = db_column_int(&q, 4);
    content_get(rid, &content);
    if( file_is_the_same(&content, zName) ){
      blob_reset(&content);
      if( file_wd_setexe(zName, isExe) ){
        db_multi_exec("UPDATE vfile SET mtime=%lld WHERE id=%d",
                      file_wd_mtime(zName), id);
      }
      continue;
    }
    if( promptFlag && file_wd_size(zName)>=0 ){
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
      } else if( cReply!='y' && cReply!='Y' ){
        blob_reset(&content);
        continue;
      }
    }
    if( verbose ) fossil_print("%s\n", &zName[nRepos]);
    if( file_wd_isdir(zName)==1 ){
      /*TODO(dchest): remove directories? */
      fossil_fatal("%s is directory, cannot overwrite", zName);
    }
    if( file_wd_size(zName)>=0 && (isLink || file_wd_islink(0)) ){
      file_delete(zName);
    }
    if( isLink ){
      symlink_create(blob_str(&content), zName);
    }else{
      blob_write_to_file(&content, zName);
    }
    file_wd_setexe(zName, isExe);
    blob_reset(&content);
    db_multi_exec("UPDATE vfile SET mtime=%lld WHERE id=%d",
                  file_wd_mtime(zName), id);
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
    file_delete(zName);
  }
  db_finalize(&q);
  db_multi_exec("UPDATE vfile SET mtime=NULL WHERE vid=%d AND mrid>0", vid);
}

/*
** Check to see if the directory named in zPath is the top of a checkout.
** In other words, check to see if directory pPath contains a file named
** "_FOSSIL_" or ".fslckout".  Return true or false.
*/
int vfile_top_of_checkout(const char *zPath){
  char *zFile;
  int fileFound = 0;

  zFile = mprintf("%s/_FOSSIL_", zPath);
  fileFound = file_size(zFile)>=1024;
  fossil_free(zFile);
  if( !fileFound ){
    zFile = mprintf("%s/.fslckout", zPath);
    fileFound = file_size(zFile)>=1024;
    fossil_free(zFile);
  }

  /* Check for ".fos" for legacy support.  But the use of ".fos" as the
  ** per-checkout database name is deprecated.  At some point, all support
  ** for ".fos" will end and this code should be removed.  This comment
  ** added on 2012-02-04.
  */
  if( !fileFound ){
    zFile = mprintf("%s/.fos", zPath);
    fileFound = file_size(zFile)>=1024;
    fossil_free(zFile);
  }
  return fileFound;
}

/*
** Return TRUE if zFile is a temporary file.  Return FALSE if not.
*/
static int is_temporary_file(const char *zName){
  static const char *const azTemp[] = {
     "baseline",
     "merge",
     "original",
     "output",
  };
  int i, j, n;

  if( sqlite3_strglob("ci-comment-????????????.txt", zName)==0 ) return 1;
  for(; zName[0]!=0; zName++){
    if( zName[0]=='/' && sqlite3_strglob("/ci-comment-????????????.txt", zName)==0 ){
      return 1;
    }
    if( zName[0]!='-' ) continue;
    for(i=0; i<count(azTemp); i++){
      n = (int)strlen(azTemp[i]);
      if( memcmp(azTemp[i], zName+1, n) ) continue;
      if( zName[n+1]==0 ) return 1;
      if( zName[n+1]=='-' ){
        for(j=n+2; zName[j] && fossil_isdigit(zName[j]); j++){}
        if( zName[j]==0 ) return 1;
      }
    }
  }
  return 0;
}

#if INTERFACE
/*
** Values for the scanFlags parameter to vfile_scan().
*/
#define SCAN_ALL    0x001    /* Includes files that begin with "." */
#define SCAN_TEMP   0x002    /* Only Fossil-generated files like *-baseline */
#define SCAN_NESTED 0x004    /* Scan for empty dirs in nested checkouts */
#define SCAN_MTIME  0x008    /* Populate mtime column */
#define SCAN_SIZE   0x010    /* Populate size column */
#endif /* INTERFACE */

/*
** Load into table SFILE the name of every ordinary file in
** the directory pPath.   Omit the first nPrefix characters of
** of pPath when inserting into the SFILE table.
**
** Subdirectories are scanned recursively.
** Omit files named in VFILE.
**
** Files whose names begin with "." are omitted unless the SCAN_ALL
** flag is set.
**
** Any files or directories that match the glob patterns pIgnore*
** are excluded from the scan.  Name matching occurs after the
** first nPrefix characters are elided from the filename.
*/
void vfile_scan(
  Blob *pPath,           /* Directory to be scanned */
  int nPrefix,           /* Number of bytes in directory name */
  unsigned scanFlags,    /* Zero or more SCAN_xxx flags */
  Glob *pIgnore1,        /* Do not add files that match this GLOB */
  Glob *pIgnore2         /* Omit files matching this GLOB too */
){
  DIR *d;
  int origSize;
  struct dirent *pEntry;
  int skipAll = 0;
  static Stmt ins;
  static int depth = 0;
  void *zNative;

  origSize = blob_size(pPath);
  if( pIgnore1 || pIgnore2 ){
    blob_appendf(pPath, "/");
    if( glob_match(pIgnore1, &blob_str(pPath)[nPrefix+1]) ) skipAll = 1;
    if( glob_match(pIgnore2, &blob_str(pPath)[nPrefix+1]) ) skipAll = 1;
    blob_resize(pPath, origSize);
  }
  if( skipAll ) return;

  if( depth==0 ){
    db_prepare(&ins,
      "INSERT OR IGNORE INTO sfile(pathname%s%s) SELECT :file%s%s"
      "  WHERE NOT EXISTS(SELECT 1 FROM vfile WHERE"
      " pathname=:file %s)",
      scanFlags & SCAN_MTIME ? ", mtime"  : "",
      scanFlags & SCAN_SIZE  ? ", size"   : "",
      scanFlags & SCAN_MTIME ? ", :mtime" : "",
      scanFlags & SCAN_SIZE  ? ", :size"  : "",
      filename_collation()
    );
  }
  depth++;

  zNative = fossil_utf8_to_path(blob_str(pPath), 1);
  d = opendir(zNative);
  if( d ){
    while( (pEntry=readdir(d))!=0 ){
      char *zPath;
      char *zUtf8;
      if( pEntry->d_name[0]=='.' ){
        if( (scanFlags & SCAN_ALL)==0 ) continue;
        if( pEntry->d_name[1]==0 ) continue;
        if( pEntry->d_name[1]=='.' && pEntry->d_name[2]==0 ) continue;
      }
      zUtf8 = fossil_path_to_utf8(pEntry->d_name);
      blob_appendf(pPath, "/%s", zUtf8);
      zPath = blob_str(pPath);
      if( glob_match(pIgnore1, &zPath[nPrefix+1]) ||
          glob_match(pIgnore2, &zPath[nPrefix+1]) ){
        /* do nothing */
#ifdef _DIRENT_HAVE_D_TYPE
      }else if( (pEntry->d_type==DT_UNKNOWN || pEntry->d_type==DT_LNK)
          ? (file_wd_isdir(zPath)==1) : (pEntry->d_type==DT_DIR) ){
#else
      }else if( file_wd_isdir(zPath)==1 ){
#endif
        if( !vfile_top_of_checkout(zPath) ){
          vfile_scan(pPath, nPrefix, scanFlags, pIgnore1, pIgnore2);
        }
#ifdef _DIRENT_HAVE_D_TYPE
      }else if( (pEntry->d_type==DT_UNKNOWN || pEntry->d_type==DT_LNK)
          ? (file_wd_isfile_or_link(zPath)) : (pEntry->d_type==DT_REG) ){
#else
      }else if( file_wd_isfile_or_link(zPath) ){
#endif
        if( (scanFlags & SCAN_TEMP)==0 || is_temporary_file(zUtf8) ){
          db_bind_text(&ins, ":file", &zPath[nPrefix+1]);
          if( scanFlags & SCAN_MTIME ){
            db_bind_int(&ins, ":mtime", file_mtime(zPath));
          }
          if( scanFlags & SCAN_SIZE ){
            db_bind_int(&ins, ":size", file_size(zPath));
          }
          db_step(&ins);
          db_reset(&ins);
        }
      }
      fossil_path_free(zUtf8);
      blob_resize(pPath, origSize);
    }
    closedir(d);
  }
  fossil_path_free(zNative);

  depth--;
  if( depth==0 ){
    db_finalize(&ins);
  }
}

/*
** Scans the specified base directory for any directories within it, while
** keeping a count of how many files they each contains, either directly or
** indirectly.
**
** Subdirectories are scanned recursively.
** Omit files named in VFILE.
**
** Directories whose names begin with "." are omitted unless the SCAN_ALL
** flag is set.
**
** Any directories that match the glob patterns pIgnore* are excluded from
** the scan.  Name matching occurs after the first nPrefix characters are
** elided from the filename.
**
** Returns the total number of files found.
*/
int vfile_dir_scan(
  Blob *pPath,           /* Base directory to be scanned */
  int nPrefix,           /* Number of bytes in base directory name */
  unsigned scanFlags,    /* Zero or more SCAN_xxx flags */
  Glob *pIgnore1,        /* Do not add directories that match this GLOB */
  Glob *pIgnore2         /* Omit directories matching this GLOB too */
){
  int result = 0;
  DIR *d;
  int origSize;
  struct dirent *pEntry;
  int skipAll = 0;
  static Stmt ins;
  static Stmt upd;
  static int depth = 0;
  void *zNative;

  origSize = blob_size(pPath);
  if( pIgnore1 || pIgnore2 ){
    blob_appendf(pPath, "/");
    if( glob_match(pIgnore1, &blob_str(pPath)[nPrefix+1]) ) skipAll = 1;
    if( glob_match(pIgnore2, &blob_str(pPath)[nPrefix+1]) ) skipAll = 1;
    blob_resize(pPath, origSize);
  }
  if( skipAll ) return result;

  if( depth==0 ){
    db_multi_exec("DROP TABLE IF EXISTS dscan_temp;"
                  "CREATE TEMP TABLE dscan_temp("
                  "  x TEXT PRIMARY KEY %s, y INTEGER)",
                  filename_collation());
    db_prepare(&ins,
       "INSERT OR IGNORE INTO dscan_temp(x, y) SELECT :file, :count"
       "  WHERE NOT EXISTS(SELECT 1 FROM vfile WHERE"
       " pathname GLOB :file || '/*' %s)", filename_collation()
    );
    db_prepare(&upd,
       "UPDATE OR IGNORE dscan_temp SET y = coalesce(y, 0) + 1"
       "  WHERE x=:file %s",
       filename_collation()
    );
  }
  depth++;

  zNative = fossil_utf8_to_path(blob_str(pPath), 1);
  d = opendir(zNative);
  if( d ){
    while( (pEntry=readdir(d))!=0 ){
      char *zOrigPath;
      char *zPath;
      char *zUtf8;
      if( pEntry->d_name[0]=='.' ){
        if( (scanFlags & SCAN_ALL)==0 ) continue;
        if( pEntry->d_name[1]==0 ) continue;
        if( pEntry->d_name[1]=='.' && pEntry->d_name[2]==0 ) continue;
      }
      zOrigPath = mprintf("%s", blob_str(pPath));
      zUtf8 = fossil_path_to_utf8(pEntry->d_name);
      blob_appendf(pPath, "/%s", zUtf8);
      zPath = blob_str(pPath);
      if( glob_match(pIgnore1, &zPath[nPrefix+1]) ||
          glob_match(pIgnore2, &zPath[nPrefix+1]) ){
        /* do nothing */
#ifdef _DIRENT_HAVE_D_TYPE
      }else if( (pEntry->d_type==DT_UNKNOWN || pEntry->d_type==DT_LNK)
          ? (file_wd_isdir(zPath)==1) : (pEntry->d_type==DT_DIR) ){
#else
      }else if( file_wd_isdir(zPath)==1 ){
#endif
        if( (scanFlags & SCAN_NESTED) || !vfile_top_of_checkout(zPath) ){
          char *zSavePath = mprintf("%s", zPath);
          int count = vfile_dir_scan(pPath, nPrefix, scanFlags, pIgnore1,
                                     pIgnore2);
          db_bind_text(&ins, ":file", &zSavePath[nPrefix+1]);
          db_bind_int(&ins, ":count", count);
          db_step(&ins);
          db_reset(&ins);
          fossil_free(zSavePath);
          result += count; /* found X normal files? */
        }
#ifdef _DIRENT_HAVE_D_TYPE
      }else if( (pEntry->d_type==DT_UNKNOWN || pEntry->d_type==DT_LNK)
          ? (file_wd_isfile_or_link(zPath)) : (pEntry->d_type==DT_REG) ){
#else
      }else if( file_wd_isfile_or_link(zPath) ){
#endif
        db_bind_text(&upd, ":file", zOrigPath);
        db_step(&upd);
        db_reset(&upd);
        result++; /* found 1 normal file */
      }
      fossil_path_free(zUtf8);
      blob_resize(pPath, origSize);
      fossil_free(zOrigPath);
    }
    closedir(d);
  }
  fossil_path_free(zNative);

  depth--;
  if( depth==0 ){
    db_finalize(&upd);
    db_finalize(&ins);
  }
  return result;
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
      "SELECT %Q || pathname, pathname, origname, is_selected(id), rid"
      "  FROM vfile"
      " WHERE (NOT deleted OR NOT is_selected(id)) AND vid=%d"
      " ORDER BY if_selected(id, pathname, origname) /*scan*/",
      g.zLocalRoot, vid
  );
  md5sum_init();
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFullpath = db_column_text(&q, 0);
    const char *zName = db_column_text(&q, 1);
    int isSelected = db_column_int(&q, 3);

    if( isSelected ){
      md5sum_step_text(zName, -1);
      if( file_wd_islink(zFullpath) ){
        /* Instead of file content, use link destination path */
        Blob pathBuf;

        sqlite3_snprintf(sizeof(zBuf), zBuf, " %ld\n",
                         blob_read_link(&pathBuf, zFullpath));
        md5sum_step_text(zBuf, -1);
        md5sum_step_text(blob_str(&pathBuf), -1);
        blob_reset(&pathBuf);
      }else{
        in = fossil_fopen(zFullpath,"rb");
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
      }
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
** Write a BLOB into a random filename.  Return the name of the file.
*/
char *write_blob_to_temp_file(Blob *pBlob){
  sqlite3_uint64 r;
  char *zOut = 0;
  do{
    sqlite3_free(zOut);
    sqlite3_randomness(8, &r);
    zOut = sqlite3_mprintf("file-%08llx", r);
  }while( file_size(zOut)>=0 );
  blob_write_to_file(pBlob, zOut);
  return zOut;
}

/*
** Do a file-by-file comparison of the content of the repository and
** the working check-out on disk.  Report any errors.
*/
void vfile_compare_repository_to_disk(int vid){
  sqlite3_int64 rc;
  Stmt q;
  Blob disk, repo;
  char *zOut;

  db_must_be_within_tree();
  db_prepare(&q,
      "SELECT %Q || pathname, pathname, rid FROM vfile"
      " WHERE NOT deleted AND vid=%d AND is_selected(id)"
      " ORDER BY if_selected(id, pathname, origname) /*scan*/",
      g.zLocalRoot, vid
  );
  md5sum_init();
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFullpath = db_column_text(&q, 0);
    const char *zName = db_column_text(&q, 1);
    int rid = db_column_int(&q, 2);

    blob_zero(&disk);
    if( file_wd_islink(zFullpath) ){
      rc = blob_read_link(&disk, zFullpath);
    }else{
      rc = blob_read_from_file(&disk, zFullpath);
    }
    if( rc<0 ){
      fossil_print("ERROR: cannot read file [%s]\n", zFullpath);
      blob_reset(&disk);
      continue;
    }
    blob_zero(&repo);
    content_get(rid, &repo);
    if( blob_size(&repo)!=blob_size(&disk) ){
      fossil_print("ERROR: [%s] is %d bytes on disk but %d in the repository\n",
             zName, blob_size(&disk), blob_size(&repo));
      zOut = write_blob_to_temp_file(&repo);
      fossil_print("NOTICE: Repository version of [%s] stored in [%s]\n",
                   zName, zOut);
      sqlite3_free(zOut);
      blob_reset(&disk);
      blob_reset(&repo);
      continue;
    }
    if( blob_compare(&repo, &disk) ){
      fossil_print(
          "ERROR: [%s] is different on disk compared to the repository\n",
          zName);
      zOut = write_blob_to_temp_file(&repo);
      fossil_print("NOTICE: Repository version of [%s] stored in [%s]\n",
          zName, zOut);
      sqlite3_free(zOut);
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

  db_prepare(&q, "SELECT pathname, origname, rid, is_selected(id)"
                 " FROM vfile"
                 " WHERE (NOT deleted OR NOT is_selected(id))"
                 "   AND rid>0 AND vid=%d"
                 " ORDER BY if_selected(id,pathname,origname) /*scan*/",
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
  Blob err;
  Manifest *pManifest;
  ManifestFile *pFile;
  char zBuf[100];

  blob_zero(pOut);
  blob_zero(&err);
  if( pManOut ){
    blob_zero(pManOut);
  }
  db_must_be_within_tree();
  pManifest = manifest_get(vid, CFTYPE_MANIFEST, &err);
  if( pManifest==0 ){
    fossil_fatal("manifest file (%d) is malformed:\n%s",
                 vid, blob_str(&err));
  }
  manifest_file_rewind(pManifest);
  while( (pFile = manifest_file_next(pManifest,0))!=0 ){
    if( pFile->zUuid==0 ) continue;
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
**
** Display the aggregate checksum for content computed in several
** different ways.  The aggregate checksum is used during "fossil commit"
** to double-check that the information about to be committed to the
** repository exactly matches the information currently in the check-out.
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
