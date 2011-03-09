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
** This file contains code used to generate ZIP archives.
*/
#include <assert.h>
#include <zlib.h>
#include "config.h"
#include "tar.h"

/*
** State information for the tarball builder.
*/
static struct tarball_t {
  unsigned char *aHdr;      /* Space for building headers */
  char *zSpaces;            /* Spaces for padding */
} tball;

/*
** Begin the process of generating a tarball.
**
** Initialize the GZIP compressor and the table of directory names.
*/
static void tar_begin(void){
  assert( tball.aHdr==0 );
  tball.aHdr = fossil_malloc(1024);
  tball.zSpaces = (char*)&tball.aHdr[512];
  memset(tball.aHdr, 0, 512);
  memcpy(&tball.aHdr[108], "0000000", 8);  /* Owner ID */
  memcpy(&tball.aHdr[116], "0000000", 8);  /* Group ID */
  gzip_begin();
  db_multi_exec(
    "CREATE TEMP TABLE dir(name UNIQUE);"
  );
}

/*
** Build a header for a file or directory and write that header
** into the growing tarball.
*/
static void tar_add_header(
  const char *zName,     /* Name of the object */
  int nName,             /* Number of characters in zName */
  int iMode,             /* Mode.  0644 or 0755 */
  unsigned int mTime,    /* File modification time */
  int iSize,             /* Size of the object in bytes */
  int iType              /* Type of object.  0==file.  5==directory */
){
  unsigned int cksum = 0;
  int i;
  assert( nName<100 );
  memcpy(tball.aHdr, zName, nName);
  memset(&tball.aHdr[nName], 0, 100-nName);
  sqlite3_snprintf(8, (char*)&tball.aHdr[100], "%07o", iMode);
  sqlite3_snprintf(12, (char*)&tball.aHdr[124], "%011o", iSize);
  sqlite3_snprintf(12, (char*)&tball.aHdr[136], "%011o", mTime);
  memset(&tball.aHdr[148], ' ', 8);
  tball.aHdr[156] = iType + '0';
  for(i=0; i<512; i++) cksum += tball.aHdr[i];
  sqlite3_snprintf(7, (char*)&tball.aHdr[148], "%06o", cksum);
  tball.aHdr[154] = 0;
  gzip_step((char*)tball.aHdr, 512);
}

/*
** Recursively add an directory entry for the given file if those
** directories have not previously been seen.
*/
static void tar_add_directory_of(
  const char *zName,      /* Name of directory including final "/" */
  int nName,              /* Characters in zName */
  unsigned int mTime      /* Modification time */
){
  int i;
  for(i=nName-1; i>0 && zName[i]!='/'; i--){}
  if( i<=0 ) return;
  db_multi_exec("INSERT OR IGNORE INTO dir VALUES('%.*q')", i, zName);
  if( sqlite3_changes(g.db)==0 ) return;
  tar_add_directory_of(zName, i-1, mTime);
  tar_add_header(zName, i, 0755, mTime, 0, 5);
}

/*
** Add a single file to the growing tarball.
*/
static void tar_add_file(
  const char *zName,               /* Name of the file.  nul-terminated */
  Blob *pContent,                  /* Content of the file */
  int isExe,                       /* True for executable files */
  unsigned int mTime               /* Last modification time of the file */
){
  int nName = strlen(zName);
  int n = blob_size(pContent);
  int lastPage;

  tar_add_directory_of(zName, nName, mTime);
  tar_add_header(zName, nName, isExe ? 0755 : 0644, mTime, n, 0);
  gzip_step(blob_buffer(pContent), n);
  lastPage = n % 512;
  if( lastPage!=0 ){
    gzip_step(tball.zSpaces, 512 - lastPage);
  }
}

/*
** Finish constructing the tarball.  Put the content of the tarball
** in Blob pOut.
*/
static void tar_finish(Blob *pOut){
  db_multi_exec("DROP TABLE dir");
  gzip_finish(pOut);
  fossil_free(tball.aHdr);
  tball.aHdr = 0;
}


/*
** COMMAND: test-tarball
**
** Generate a GZIP-compresssed tarball in the file given by the first argument
** that contains files given in the second and subsequent arguments.
*/
void test_tarball_cmd(void){
  int i;
  Blob zip;
  Blob file;
  if( g.argc<3 ){
    usage("ARCHIVE FILE....");
  }
  sqlite3_open(":memory:", &g.db);
  tar_begin();
  for(i=3; i<g.argc; i++){
    blob_zero(&file);
    blob_read_from_file(&file, g.argv[i]);
    tar_add_file(g.argv[i], &file,
                 file_isexe(g.argv[i]), file_mtime(g.argv[i]));
    blob_reset(&file);
  }
  tar_finish(&zip);
  blob_write_to_file(&zip, g.argv[2]);
}

/*
** Given the RID for a checkin, construct a tarball containing
** all files in that checkin
**
** If RID is for an object that is not a real manifest, then the
** resulting tarball contains a single file which is the RID
** object.
**
** If the RID object does not exist in the repository, then
** pTar is zeroed.
**
** zDir is a "synthetic" subdirectory which all files get
** added to as part of the tarball. It may be 0 or an empty string, in
** which case it is ignored. The intention is to create a tarball which
** politely expands into a subdir instead of filling your current dir
** with source files. For example, pass a UUID or "ProjectName".
**
*/
void tarball_of_checkin(int rid, Blob *pTar, const char *zDir){
  Blob mfile, hash, file;
  Manifest *pManifest;
  ManifestFile *pFile;
  Blob filename;
  int nPrefix;
  char *zName;
  unsigned int mTime;

  content_get(rid, &mfile);
  if( blob_size(&mfile)==0 ){
    blob_zero(pTar);
    return;
  }
  blob_zero(&hash);
  blob_zero(&filename);
  tar_begin();

  if( zDir && zDir[0] ){
    blob_appendf(&filename, "%s/", zDir);
  }
  nPrefix = blob_size(&filename);

  pManifest = manifest_get(rid, CFTYPE_MANIFEST);
  if( pManifest ){
    mTime = (pManifest->rDate - 2440587.5)*86400.0;
    if( db_get_boolean("manifest", 0) ){
      blob_append(&filename, "manifest", -1);
      zName = blob_str(&filename);
      tar_add_file(zName, &mfile, 0, mTime);
      sha1sum_blob(&mfile, &hash);
      blob_reset(&mfile);
      blob_append(&hash, "\n", 1);
      blob_resize(&filename, nPrefix);
      blob_append(&filename, "manifest.uuid", -1);
      zName = blob_str(&filename);
      tar_add_file(zName, &hash, 0, mTime);
      blob_reset(&hash);
    }
    manifest_file_rewind(pManifest);
    while( (pFile = manifest_file_next(pManifest,0))!=0 ){
      int fid = uuid_to_rid(pFile->zUuid, 0);
      if( fid ){
        content_get(fid, &file);
        blob_resize(&filename, nPrefix);
        blob_append(&filename, pFile->zName, -1);
        zName = blob_str(&filename);
        tar_add_file(zName, &file, manifest_file_mperm(pFile), mTime);
        blob_reset(&file);
      }
    }
  }else{
    sha1sum_blob(&mfile, &hash);
    blob_append(&filename, blob_str(&hash), 16);
    zName = blob_str(&filename);
    mTime = db_int64(0, "SELECT (julianday('now') -  2440587.5)*86400.0;");
    tar_add_file(zName, &mfile, 0, mTime);
  }
  manifest_destroy(pManifest);
  blob_reset(&mfile);
  blob_reset(&filename);
  tar_finish(pTar);
}

/*
** COMMAND: tarball
**
** Usage: %fossil tarball VERSION OUTPUTFILE [--name DIRECTORYNAME]
**
** Generate a compressed tarball for a specified version.  If the --name
** option is used, its argument becomes the name of the top-level directory
** in the resulting tarball.  If --name is omitted, the top-level directory
** named is derived from the project name, the check-in date and time, and
** the artifact ID of the check-in.
*/
void tarball_cmd(void){
  int rid;
  Blob tarball;
  const char *zName;
  zName = find_option("name", 0, 1);
  db_find_and_open_repository(0, 0);
  if( g.argc!=4 ){
    usage("VERSION OUTPUTFILE");
  }
  rid = name_to_rid(g.argv[2]);
  if( zName==0 ){
    zName = db_text("default-name",
       "SELECT replace(%Q,' ','_') "
          " || strftime('_%%Y-%%m-%%d_%%H%%M%%S_', event.mtime) "
          " || substr(blob.uuid, 1, 10)"
       "  FROM event, blob"
       " WHERE event.objid=%d"
       "   AND blob.rid=%d",
       db_get("project-name", "unnamed"), rid, rid
    );
  }
  tarball_of_checkin(rid, &tarball, zName);
  blob_write_to_file(&tarball, g.argv[3]);
  blob_reset(&tarball);
}

/*
** WEBPAGE: tarball
** URL: /tarball/RID.tar.gz
**
** Generate a compressed tarball for a checkin.
** Return that tarball as the HTTP reply content.
*/
void tarball_page(void){
  int rid;
  char *zName, *zRid;
  int nName, nRid;
  Blob tarball;

  login_check_credentials();
  if( !g.okZip ){ login_needed(); return; }
  zName = mprintf("%s", PD("name",""));
  nName = strlen(zName);
  zRid = mprintf("%s", PD("uuid",""));
  nRid = strlen(zRid);
  for(nName=strlen(zName)-1; nName>5; nName--){
    if( zName[nName]=='.' ){
      zName[nName] = 0;
      break;
    }
  }
  rid = name_to_rid(nRid?zRid:zName);
  if( rid==0 ){
    @ Not found
    return;
  }
  if( nRid==0 && nName>10 ) zName[10] = 0;
  tarball_of_checkin(rid, &tarball, zName);
  free( zName );
  free( zRid );
  cgi_set_content(&tarball);
  cgi_set_content_type("application/x-compressed");
}
