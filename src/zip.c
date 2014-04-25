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
#include "config.h"
#include <assert.h>
#include <zlib.h>
#include "zip.h"

/*
** Write a 16- or 32-bit integer as little-endian into the given buffer.
*/
static void put16(char *z, int v){
  z[0] = v & 0xff;
  z[1] = (v>>8) & 0xff;
}
static void put32(char *z, int v){
  z[0] = v & 0xff;
  z[1] = (v>>8) & 0xff;
  z[2] = (v>>16) & 0xff;
  z[3] = (v>>24) & 0xff;
}

/*
** Variables in which to accumulate a growing ZIP archive.
*/
static Blob body;    /* The body of the ZIP archive */
static Blob toc;     /* The table of contents */
static int nEntry;   /* Number of files */
static int dosTime;  /* DOS-format time */
static int dosDate;  /* DOS-format date */
static int unixTime; /* Seconds since 1970 */
static int nDir;     /* Number of entries in azDir[] */
static char **azDir; /* Directory names already added to the archive */

/*
** Initialize a new ZIP archive.
*/
void zip_open(void){
  blob_zero(&body);
  blob_zero(&toc);
  nEntry = 0;
  dosTime = 0;
  dosDate = 0;
  unixTime = 0;
}

/*
** Set the date and time values from an ISO8601 date string.
*/
void zip_set_timedate_from_str(const char *zDate){
  int y, m, d;
  int H, M, S;

  y = atoi(zDate);
  m = atoi(&zDate[5]);
  d = atoi(&zDate[8]);
  H = atoi(&zDate[11]);
  M = atoi(&zDate[14]);
  S = atoi(&zDate[17]);
  dosTime = (H<<11) + (M<<5) + (S>>1);
  dosDate = ((y-1980)<<9) + (m<<5) + d;
}

/*
** Set the date and time from a julian day number.
*/
void zip_set_timedate(double rDate){
  char *zDate = db_text(0, "SELECT datetime(%.17g)", rDate);
  zip_set_timedate_from_str(zDate);
  fossil_free(zDate);
  unixTime = (rDate - 2440587.5)*86400.0;
}

/*
** If the given filename includes one or more directory entries, make
** sure the directories are already in the archive.  If they are not
** in the archive, add them.
*/
void zip_add_folders(char *zName){
  int i, c;
  int j;
  for(i=0; zName[i]; i++){
    if( zName[i]=='/' ){
      c = zName[i+1];
      zName[i+1] = 0;
      for(j=0; j<nDir; j++){
        if( fossil_strcmp(zName, azDir[j])==0 ) break;
      }
      if( j>=nDir ){
        nDir++;
        azDir = fossil_realloc(azDir, sizeof(azDir[0])*nDir);
        azDir[j] = mprintf("%s", zName);
        zip_add_file(zName, 0, 0);
      }
      zName[i+1] = c;
    }
  }
}

/*
** Append a single file to a growing ZIP archive.
**
** pFile is the file to be appended.  zName is the name
** that the file should be saved as.
*/
void zip_add_file(const char *zName, const Blob *pFile, int mPerm){
  z_stream stream;
  int nameLen;
  int toOut = 0;
  int iStart;
  int iCRC = 0;
  int nByte = 0;
  int nByteCompr = 0;
  int nBlob;                 /* Size of the blob */
  int iMethod;               /* Compression method. */
  int iMode = 0644;          /* Access permissions */
  char *z;
  char zHdr[30];
  char zExTime[13];
  char zBuf[100];
  char zOutBuf[100000];

  /* Fill in as much of the header as we know.
  */
  nBlob = pFile ? blob_size(pFile) : 0;
  if( nBlob>0 ){
    iMethod = 8;
    switch( mPerm ){
      case PERM_LNK:   iMode = 0120755;   break;
      case PERM_EXE:   iMode = 0100755;   break;
      default:         iMode = 0100644;   break;
    }
  }else{
    iMethod = 0;
    iMode = 040755;
  }
  nameLen = strlen(zName);
  memset(zHdr, 0, sizeof(zHdr));
  put32(&zHdr[0], 0x04034b50);
  put16(&zHdr[4], 0x000a);
  put16(&zHdr[6], 0x0800);
  put16(&zHdr[8], iMethod);
  put16(&zHdr[10], dosTime);
  put16(&zHdr[12], dosDate);
  put16(&zHdr[26], nameLen);
  put16(&zHdr[28], 13);

  put16(&zExTime[0], 0x5455);
  put16(&zExTime[2], 9);
  zExTime[4] = 3;
  put32(&zExTime[5], unixTime);
  put32(&zExTime[9], unixTime);


  /* Write the header and filename.
  */
  iStart = blob_size(&body);
  blob_append(&body, zHdr, 30);
  blob_append(&body, zName, nameLen);
  blob_append(&body, zExTime, 13);

  if( nBlob>0 ){
    /* Write the compressed file.  Compute the CRC as we progress.
    */
    stream.zalloc = (alloc_func)0;
    stream.zfree = (free_func)0;
    stream.opaque = 0;
    stream.avail_in = blob_size(pFile);
    stream.next_in = (unsigned char*)blob_buffer(pFile);
    stream.avail_out = sizeof(zOutBuf);
    stream.next_out = (unsigned char*)zOutBuf;
    deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    iCRC = crc32(0, stream.next_in, stream.avail_in);
    while( stream.avail_in>0 ){
      deflate(&stream, 0);
      toOut = sizeof(zOutBuf) - stream.avail_out;
      blob_append(&body, zOutBuf, toOut);
      stream.avail_out = sizeof(zOutBuf);
      stream.next_out = (unsigned char*)zOutBuf;
    }
    do{
      stream.avail_out = sizeof(zOutBuf);
      stream.next_out = (unsigned char*)zOutBuf;
      deflate(&stream, Z_FINISH);
      toOut = sizeof(zOutBuf) - stream.avail_out;
      blob_append(&body, zOutBuf, toOut);
    }while( stream.avail_out==0 );
    nByte = stream.total_in;
    nByteCompr = stream.total_out;
    deflateEnd(&stream);

    /* Go back and write the header, now that we know the compressed file size.
    */
    z = &blob_buffer(&body)[iStart];
    put32(&z[14], iCRC);
    put32(&z[18], nByteCompr);
    put32(&z[22], nByte);
  }

  /* Make an entry in the tables of contents
  */
  memset(zBuf, 0, sizeof(zBuf));
  put32(&zBuf[0], 0x02014b50);
  put16(&zBuf[4], 0x0317);
  put16(&zBuf[6], 0x000a);
  put16(&zBuf[8], 0x0800);
  put16(&zBuf[10], iMethod);
  put16(&zBuf[12], dosTime);
  put16(&zBuf[14], dosDate);
  put32(&zBuf[16], iCRC);
  put32(&zBuf[20], nByteCompr);
  put32(&zBuf[24], nByte);
  put16(&zBuf[28], nameLen);
  put16(&zBuf[30], 9);
  put16(&zBuf[32], 0);
  put16(&zBuf[34], 0);
  put16(&zBuf[36], 0);
  put32(&zBuf[38], ((unsigned)iMode)<<16);
  put32(&zBuf[42], iStart);
  blob_append(&toc, zBuf, 46);
  blob_append(&toc, zName, nameLen);
  put16(&zExTime[2], 5);
  blob_append(&toc, zExTime, 9);
  nEntry++;
}


/*
** Write the ZIP archive into the given BLOB.
*/
void zip_close(Blob *pZip){
  int iTocStart;
  int iTocEnd;
  int i;
  char zBuf[30];

  iTocStart = blob_size(&body);
  blob_append(&body, blob_buffer(&toc), blob_size(&toc));
  iTocEnd = blob_size(&body);

  memset(zBuf, 0, sizeof(zBuf));
  put32(&zBuf[0], 0x06054b50);
  put16(&zBuf[4], 0);
  put16(&zBuf[6], 0);
  put16(&zBuf[8], nEntry);
  put16(&zBuf[10], nEntry);
  put32(&zBuf[12], iTocEnd - iTocStart);
  put32(&zBuf[16], iTocStart);
  put16(&zBuf[20], 0);
  blob_append(&body, zBuf, 22);
  blob_reset(&toc);
  *pZip = body;
  blob_zero(&body);
  nEntry = 0;
  for(i=0; i<nDir; i++){
    fossil_free(azDir[i]);
  }
  fossil_free(azDir);
  nDir = 0;
  azDir = 0;
}

/*
** COMMAND: test-filezip
**
** Generate a ZIP archive specified by the first argument that
** contains files given in the second and subsequent arguments.
*/
void filezip_cmd(void){
  int i;
  Blob zip;
  Blob file;
  if( g.argc<3 ){
    usage("ARCHIVE FILE....");
  }
  zip_open();
  for(i=3; i<g.argc; i++){
    blob_zero(&file);
    blob_read_from_file(&file, g.argv[i]);
    zip_add_file(g.argv[i], &file, file_wd_perm(g.argv[i]));
    blob_reset(&file);
  }
  zip_close(&zip);
  blob_write_to_file(&zip, g.argv[2]);
}

/*
** Given the RID for a manifest, construct a ZIP archive containing
** all files in the corresponding baseline.
**
** If RID is for an object that is not a real manifest, then the
** resulting ZIP archive contains a single file which is the RID
** object.
**
** If the RID object does not exist in the repository, then
** pZip is zeroed.
**
** zDir is a "synthetic" subdirectory which all zipped files get
** added to as part of the zip file. It may be 0 or an empty string,
** in which case it is ignored. The intention is to create a zip which
** politely expands into a subdir instead of filling your current dir
** with source files. For example, pass a UUID or "ProjectName".
**
*/
void zip_of_baseline(int rid, Blob *pZip, const char *zDir){
  Blob mfile, hash, file;
  Manifest *pManifest;
  ManifestFile *pFile;
  Blob filename;
  int nPrefix;

  content_get(rid, &mfile);
  if( blob_size(&mfile)==0 ){
    blob_zero(pZip);
    return;
  }
  blob_zero(&hash);
  blob_zero(&filename);
  zip_open();

  if( zDir && zDir[0] ){
    blob_appendf(&filename, "%s/", zDir);
  }
  nPrefix = blob_size(&filename);

  pManifest = manifest_get(rid, CFTYPE_MANIFEST, 0);
  if( pManifest ){
    char *zName;
    zip_set_timedate(pManifest->rDate);
    if( db_get_boolean("manifest", 0) ){
      blob_append(&filename, "manifest", -1);
      zName = blob_str(&filename);
      zip_add_folders(zName);
      zip_add_file(zName, &mfile, 0);
      sha1sum_blob(&mfile, &hash);
      blob_reset(&mfile);
      blob_append(&hash, "\n", 1);
      blob_resize(&filename, nPrefix);
      blob_append(&filename, "manifest.uuid", -1);
      zName = blob_str(&filename);
      zip_add_file(zName, &hash, 0);
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
        zip_add_folders(zName);
        zip_add_file(zName, &file, manifest_file_mperm(pFile));
        blob_reset(&file);
      }
    }
  }else{
    blob_reset(&mfile);
  }
  manifest_destroy(pManifest);
  blob_reset(&filename);
  zip_close(pZip);
}

/*
** COMMAND: zip*
**
** Usage: %fossil zip VERSION OUTPUTFILE [--name DIRECTORYNAME] [-R|--repository REPO]
**
** Generate a ZIP archive for a specified version.  If the --name option is
** used, it argument becomes the name of the top-level directory in the
** resulting ZIP archive.  If --name is omitted, the top-level directory
** named is derived from the project name, the check-in date and time, and
** the artifact ID of the check-in.
*/
void baseline_zip_cmd(void){
  int rid;
  Blob zip;
  const char *zName;
  zName = find_option("name", 0, 1);
  db_find_and_open_repository(0, 0);
  if( g.argc!=4 ){
    usage("VERSION OUTPUTFILE");
  }
  rid = name_to_typed_rid(g.argv[2],"ci");
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
  zip_of_baseline(rid, &zip, zName);
  blob_write_to_file(&zip, g.argv[3]);
}

/*
** WEBPAGE: zip
** URL: /zip/RID.zip
**
** Generate a ZIP archive for the baseline.
** Return that ZIP archive as the HTTP reply content.
**
** Optional URL Parameters:
**
** - name=base name of the output file. Defaults to
** something project/version-specific.
**
** - uuid=the version to zip (may be a tag/branch name).
** Defaults to trunk.
**
*/
void baseline_zip_page(void){
  int rid;
  char *zName, *zRid;
  int nName, nRid;
  Blob zip;
  char *zKey;

  login_check_credentials();
  if( !g.perm.Zip ){ login_needed(); return; }
  load_control();
  zName = mprintf("%s", PD("name",""));
  nName = strlen(zName);
  zRid = mprintf("%s", PD("uuid","trunk"));
  nRid = strlen(zRid);
  for(nName=strlen(zName)-1; nName>5; nName--){
    if( zName[nName]=='.' ){
      zName[nName] = 0;
      break;
    }
  }
  rid = name_to_typed_rid(nRid?zRid:zName,"ci");
  if( rid==0 ){
    @ Not found
    return;
  }
  if( nRid==0 && nName>10 ) zName[10] = 0;
  zKey = db_text(0, "SELECT '/zip/'||uuid||'/%q' FROM blob WHERE rid=%d",zName,rid);
  blob_zero(&zip);
  if( cache_read(&zip, zKey)==0 ){
    zip_of_baseline(rid, &zip, zName);
    cache_write(&zip, zKey);
  }
  fossil_free( zName );
  fossil_free( zRid );
  fossil_free( zKey );
  cgi_set_content(&zip);
  cgi_set_content_type("application/zip");
}
