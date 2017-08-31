/*
** Copyright (c) 2014 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@sqlite.org
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This module implements the userspace side of a Fuse Filesystem that
** contains all check-ins for a fossil repository.
**
** This module is a mostly a no-op unless compiled with -DFOSSIL_HAVE_FUSEFS.
** The FOSSIL_HAVE_FUSEFS should be omitted on systems that lack support for
** the Fuse Filesystem, of course.
*/
#ifdef FOSSIL_HAVE_FUSEFS
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include "fusefs.h"

#define FUSE_USE_VERSION 26
#include <fuse.h>

/*
** Global state information about the archive
*/
static struct sGlobal {
  /* A cache of a single check-in manifest */
  int rid;                  /* rid for the cached manifest */
  char *zSymName;           /* Symbolic name corresponding to rid */
  Manifest *pMan;           /* The cached manifest */
  /* A cache of a single file within a single check-in */
  int iFileRid;             /* Check-in ID for the cached file */
  ManifestFile *pFile;      /* Name of a cached file */
  Blob content;             /* Content of the cached file */
  /* Parsed path */
  char *az[3];              /* 0=type, 1=id, 2=path */
} fusefs;

/*
** Clear the fusefs.az[] array.
*/
static void fusefs_clear_path(void){
  int i;
  for(i=0; i<count(fusefs.az); i++){
    fossil_free(fusefs.az[i]);
    fusefs.az[i] = 0;
  }
}

/*
** Split of the input path into 0, 1, 2, or 3 elements in fusefs.az[].
** Return the number of elements.
**
** Any prior path parse is deleted.
*/
static int fusefs_parse_path(const char *zPath){
  int i, j;
  fusefs_clear_path();
  if( strcmp(zPath,"/")==0 ) return 0;
  for(i=0, j=1; i<2 && zPath[j]; i++){
    int jStart = j;
    while( zPath[j] && zPath[j]!='/' ){ j++; }
    fusefs.az[i] = mprintf("%.*s", j-jStart, &zPath[jStart]);
    if( zPath[j] ) j++;
  }
  if( zPath[j] ) fusefs.az[i++] = fossil_strdup(&zPath[j]);
  return i;
}

/*
** Reclaim memory used by the fusefs local variable.
*/
static void fusefs_reset(void){
  blob_reset(&fusefs.content);
  manifest_destroy(fusefs.pMan);
  fusefs.pMan = 0;
  fossil_free(fusefs.zSymName);
  fusefs.zSymName = 0;
  fusefs.pFile = 0;
}

/*
** Load manifest rid into the cache.
*/
static void fusefs_load_rid(int rid, const char *zSymName){
  if( fusefs.rid==rid && fusefs.pMan!=0 ) return;
  fusefs_reset();
  fusefs.zSymName = fossil_strdup(zSymName);
  fusefs.pMan = manifest_get(rid, CFTYPE_MANIFEST, 0);
  fusefs.rid = rid;
}

/*
** Locate the rid corresponding to a symbolic name
*/
static int fusefs_name_to_rid(const char *zSymName){
  if( fusefs.rid>0 && strcmp(zSymName, fusefs.zSymName)==0 ){
    return fusefs.rid;
  }else{
    return symbolic_name_to_rid(zSymName, "ci");
  }
}


/*
** Implementation of stat()
*/
static int fusefs_getattr(const char *zPath, struct stat *stbuf){
  int n, rid;
  ManifestFile *pFile;
  char *zDir;
  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
  n = fusefs_parse_path(zPath);
  if( n==0 ){
    stbuf->st_mode = S_IFDIR | 0555;
    stbuf->st_nlink = 2;
    return 0;
  }
  if( strcmp(fusefs.az[0],"checkins")!=0 ) return -ENOENT;
  if( n==1 ){
    stbuf->st_mode = S_IFDIR | 0111;
    stbuf->st_nlink = 2;
    return 0;
  }
  rid = fusefs_name_to_rid(fusefs.az[1]);
  if( rid<=0 ) return -ENOENT;
  if( n==2 ){
    stbuf->st_mode = S_IFDIR | 0555;
    stbuf->st_nlink = 2;
    return 0;
  }
  fusefs_load_rid(rid, fusefs.az[1]);
  if( fusefs.pMan==0 ) return -ENOENT;
  stbuf->st_mtime = (fusefs.pMan->rDate - 2440587.5)*86400.0;
  pFile = manifest_file_seek(fusefs.pMan, fusefs.az[2], 0);
  if( pFile ){
    static Stmt q;
    stbuf->st_mode = S_IFREG |
              (manifest_file_mperm(pFile)==PERM_EXE ? 0555 : 0444);
    stbuf->st_nlink = 1;
    db_static_prepare(&q, "SELECT size FROM blob WHERE uuid=$uuid");
    db_bind_text(&q, "$uuid", pFile->zUuid);
    if( db_step(&q)==SQLITE_ROW ){
      stbuf->st_size = db_column_int(&q, 0);
    }
    db_reset(&q);
    return 0;
  }
  zDir = mprintf("%s/", fusefs.az[2]);
  pFile = manifest_file_seek(fusefs.pMan, zDir, 1);
  fossil_free(zDir);
  if( pFile==0 ) return -ENOENT;
  n = (int)strlen(fusefs.az[2]);
  if( strncmp(fusefs.az[2], pFile->zName, n)!=0 ) return -ENOENT;
  if( pFile->zName[n]!='/' ) return -ENOENT;
  stbuf->st_mode = S_IFDIR | 0555;
  stbuf->st_nlink = 2;
  return 0;
}

/*
** Implementation of readdir()
*/
static int fusefs_readdir(
  const char *zPath,
  void *buf,
  fuse_fill_dir_t filler,
  off_t offset,
  struct fuse_file_info *fi
){
  int n, rid;
  ManifestFile *pFile;
  const char *zPrev = "";
  int nPrev = 0;
  char *z;
  int cnt = 0;
  n = fusefs_parse_path(zPath);
  if( n==0 ){
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, "checkins", NULL, 0);
    return 0;
  }
  if( strcmp(fusefs.az[0],"checkins")!=0 ) return -ENOENT;
  if( n==1 ) return -ENOENT;
  rid = fusefs_name_to_rid(fusefs.az[1]);
  if( rid<=0 ) return -ENOENT;
  fusefs_load_rid(rid, fusefs.az[1]);
  if( fusefs.pMan==0 ) return -ENOENT;
  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  manifest_file_rewind(fusefs.pMan);
  if( n==2 ){
    while( (pFile = manifest_file_next(fusefs.pMan, 0))!=0 ){
      if( nPrev>0 && strncmp(pFile->zName, zPrev, nPrev)==0
                  && pFile->zName[nPrev]=='/' ) continue;
      zPrev = pFile->zName;
      for(nPrev=0; zPrev[nPrev] && zPrev[nPrev]!='/'; nPrev++){}
      z = mprintf("%.*s", nPrev, zPrev);
      filler(buf, z, NULL, 0);
      fossil_free(z);
      cnt++;
    }
  }else{
    char *zBase = mprintf("%s/", fusefs.az[2]);
    int nBase = (int)strlen(zBase);
    while( (pFile = manifest_file_next(fusefs.pMan, 0))!=0 ){
      if( strcmp(pFile->zName, zBase)>=0 ) break;
    }
    while( pFile && strncmp(zBase, pFile->zName, nBase)==0 ){
      if( nPrev==0 || strncmp(pFile->zName+nBase, zPrev, nPrev)!=0 ){
        zPrev = pFile->zName+nBase;
        for(nPrev=0; zPrev[nPrev] && zPrev[nPrev]!='/'; nPrev++){}
        if( zPrev[nPrev]=='/' ){
          z = mprintf("%.*s", nPrev, zPrev);
          filler(buf, z, NULL, 0);
          fossil_free(z);
        }else{
          filler(buf, zPrev, NULL, 0);
          nPrev = 0;
        }
        cnt++;
      }
      pFile = manifest_file_next(fusefs.pMan, 0);
    }
    fossil_free(zBase);
  }
  return cnt>0 ? 0 : -ENOENT;
}


/*
** Implementation of read()
*/
static int fusefs_read(
  const char *zPath,
  char *buf,
  size_t size,
  off_t offset,
  struct fuse_file_info *fi
){
  int n, rid;
  n = fusefs_parse_path(zPath);
  if( n<3 ) return -ENOENT;
  if( strcmp(fusefs.az[0], "checkins")!=0 ) return -ENOENT;
  rid = fusefs_name_to_rid(fusefs.az[1]);
  if( rid<=0 ) return -ENOENT;
  fusefs_load_rid(rid, fusefs.az[1]);
  if( fusefs.pFile!=0 && strcmp(fusefs.az[2], fusefs.pFile->zName)!=0 ){
    fusefs.pFile = 0;
    blob_reset(&fusefs.content);
  }
  fusefs.pFile = manifest_file_seek(fusefs.pMan, fusefs.az[2], 0);
  if( fusefs.pFile==0 ) return -ENOENT;
  rid = uuid_to_rid(fusefs.pFile->zUuid, 0);
  blob_reset(&fusefs.content);
  content_get(rid, &fusefs.content);
  if( offset>blob_size(&fusefs.content) ) return 0;
  if( offset+size>blob_size(&fusefs.content) ){
    size = blob_size(&fusefs.content) - offset;
  }
  memcpy(buf, blob_buffer(&fusefs.content)+offset, size);
  return size;
}

static struct fuse_operations fusefs_methods = {
  .getattr = fusefs_getattr,
  .readdir = fusefs_readdir,
  .read    = fusefs_read,
};

/*
** COMMAND: fusefs
**
** Usage: %fossil fusefs [--debug] DIRECTORY
**
** This command uses the Fuse Filesystem (FuseFS) to mount a directory
** at DIRECTORY that contains the content of all check-ins in the
** repository.  The names of files are DIRECTORY/checkins/VERSION/PATH
** where DIRECTORY is the root of the mount, VERSION is any valid
** check-in name (examples: "trunk" or "tip" or a tag or any unique
** prefix of an artifact hash, etc) and PATH is the pathname of the file in
** the check-in.  If DIRECTORY does not exist, then an attempt is made
** to create it.
**
** The DIRECTORY/checkins directory is not searchable so one cannot
** do "ls DIRECTORY/checkins" to get a listing of all possible check-in
** names.  There are countless variations on check-in names and it is
** impractical to list them all.  But all other directories are searchable
** and so the "ls" command will work everywhere else in the fusefs
** file hierarchy.
**
** The FuseFS typically only works on Linux, and then only on Linux
** systems that have the right kernel drivers and have installed the
** appropriate support libraries.
**
** After stopping the "fossil fusefs" command, it might also be necessary
** to run "fusermount -u DIRECTORY" to reset the FuseFS before using it
** again.
*/
void fusefs_cmd(void){
  char *zMountPoint;
  char *azNewArgv[5];
  int doDebug = find_option("debug","d",0)!=0;

  db_find_and_open_repository(0,0);
  verify_all_options();
  blob_init(&fusefs.content, 0, 0);
  if( g.argc!=3 ) usage("DIRECTORY");
  zMountPoint = g.argv[2];
  if( file_mkdir(zMountPoint, 0) ){
    fossil_fatal("cannot make directory [%s]", zMountPoint);
  }
  azNewArgv[0] = g.argv[0];
  azNewArgv[1] = doDebug ? "-d" : "-f";
  azNewArgv[2] = "-s";
  azNewArgv[3] = zMountPoint;
  azNewArgv[4] = 0;
  g.localOpen = 0;   /* Prevent tags like "current" and "prev" */
  fuse_main(4, azNewArgv, &fusefs_methods, NULL);
  fusefs_reset();
  fusefs_clear_path();
}
#endif /* FOSSIL_HAVE_FUSEFS */

/*
** Return version numbers for the FUSE header that was used at compile-time
** and/or the FUSE library that was loaded at runtime.
*/
const char *fusefs_lib_version(void){
#if defined(FOSSIL_HAVE_FUSEFS) && FUSE_MAJOR_VERSION>=3
  return fuse_pkgversion();
#else
  return "unknown";
#endif
}

const char *fusefs_inc_version(void){
#ifdef FOSSIL_HAVE_FUSEFS
  return COMPILER_STRINGIFY(FUSE_MAJOR_VERSION) "."
         COMPILER_STRINGIFY(FUSE_MINOR_VERSION);
#else
  return "unknown";
#endif
}
