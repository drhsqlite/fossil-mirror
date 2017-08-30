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
#if defined(FOSSIL_ENABLE_MINIZ)
#  define MINIZ_HEADER_FILE_ONLY
#  include "miniz.c"
#else
#  include <zlib.h>
#endif
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
  if( pFile ){ /* This is a file, possibly empty... */
    iMethod = (nBlob>0) ? 8 : 0; /* Cannot compress zero bytes. */
    switch( mPerm ){
      case PERM_LNK:   iMode = 0120755;   break;
      case PERM_EXE:   iMode = 0100755;   break;
      default:         iMode = 0100644;   break;
    }
  }else{       /* This is a directory, no blob... */
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
** object.  The pInclude and pExclude parameters are ignored in this case.
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
void zip_of_checkin(
  int rid,            /* The RID of the checkin to construct the ZIP archive from */
  Blob *pZip,         /* Write the ZIP archive content into this blob */
  const char *zDir,   /* Top-level directory of the ZIP archive */
  Glob *pInclude,     /* Only include files that match this pattern */
  Glob *pExclude      /* Exclude files that match this pattern */
){
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
  blob_set_dynamic(&hash, rid_to_uuid(rid));
  blob_zero(&filename);
  zip_open();

  if( zDir && zDir[0] ){
    blob_appendf(&filename, "%s/", zDir);
  }
  nPrefix = blob_size(&filename);

  pManifest = manifest_get(rid, CFTYPE_MANIFEST, 0);
  if( pManifest ){
    int flg, eflg = 0;
    char *zName = 0;
    zip_set_timedate(pManifest->rDate);
    flg = db_get_manifest_setting();
    if( flg ){
      /* eflg is the effective flags, taking include/exclude into account */
      if( (pInclude==0 || glob_match(pInclude, "manifest"))
       && !glob_match(pExclude, "manifest")
       && (flg & MFESTFLG_RAW) ){
        eflg |= MFESTFLG_RAW;
      }
      if( (pInclude==0 || glob_match(pInclude, "manifest.uuid"))
       && !glob_match(pExclude, "manifest.uuid")
       && (flg & MFESTFLG_UUID) ){
        eflg |= MFESTFLG_UUID;
      }
      if( (pInclude==0 || glob_match(pInclude, "manifest.tags"))
       && !glob_match(pExclude, "manifest.tags")
       && (flg & MFESTFLG_TAGS) ){
        eflg |= MFESTFLG_TAGS;
      }

      if( eflg & MFESTFLG_RAW ){
        blob_append(&filename, "manifest", -1);
        zName = blob_str(&filename);
        zip_add_folders(zName);
        sterilize_manifest(&mfile);
        zip_add_file(zName, &mfile, 0);
      }
      if( eflg & MFESTFLG_UUID ){
        blob_append(&hash, "\n", 1);
        blob_resize(&filename, nPrefix);
        blob_append(&filename, "manifest.uuid", -1);
        zName = blob_str(&filename);
        zip_add_folders(zName);
        zip_add_file(zName, &hash, 0);
      }
      if( eflg & MFESTFLG_TAGS ){
        Blob tagslist;
        blob_zero(&tagslist);
        get_checkin_taglist(rid, &tagslist);
        blob_resize(&filename, nPrefix);
        blob_append(&filename, "manifest.tags", -1);
        zName = blob_str(&filename);
        zip_add_folders(zName);
        zip_add_file(zName, &tagslist, 0);
        blob_reset(&tagslist);
      }
    }
    manifest_file_rewind(pManifest);
    while( (pFile = manifest_file_next(pManifest,0))!=0 ){
      int fid;
      if( pInclude!=0 && !glob_match(pInclude, pFile->zName) ) continue;
      if( glob_match(pExclude, pFile->zName) ) continue;
      fid = uuid_to_rid(pFile->zUuid, 0);
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
  }
  blob_reset(&mfile);
  manifest_destroy(pManifest);
  blob_reset(&filename);
  blob_reset(&hash);
  zip_close(pZip);
}

/*
** COMMAND: zip*
**
** Usage: %fossil zip VERSION OUTPUTFILE [OPTIONS]
**
** Generate a ZIP archive for a check-in.  If the --name option is
** used, its argument becomes the name of the top-level directory in the
** resulting ZIP archive.  If --name is omitted, the top-level directory
** name is derived from the project name, the check-in date and time, and
** the artifact ID of the check-in.
**
** The GLOBLIST argument to --exclude and --include can be a comma-separated
** list of glob patterns, where each glob pattern may optionally be enclosed
** in "..." or '...' so that it may contain commas.  If a file matches both
** --include and --exclude then it is excluded.
**
** Options:
**   -X|--exclude GLOBLIST   Comma-separated list of GLOBs of files to exclude
**   --include GLOBLIST      Comma-separated list of GLOBs of files to include
**   --name DIRECTORYNAME    The name of the top-level directory in the archive
**   -R REPOSITORY           Specify a Fossil repository
*/
void zip_cmd(void){
  int rid;
  Blob zip;
  const char *zName;
  Glob *pInclude = 0;
  Glob *pExclude = 0;
  const char *zInclude;
  const char *zExclude;
  zName = find_option("name", 0, 1);
  zExclude = find_option("exclude", "X", 1);
  if( zExclude ) pExclude = glob_create(zExclude);
  zInclude = find_option("include", 0, 1);
  if( zInclude ) pInclude = glob_create(zInclude);
  db_find_and_open_repository(0, 0);

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=4 ){
    usage("VERSION OUTPUTFILE");
  }
  g.zOpenRevision = g.argv[2];
  rid = name_to_typed_rid(g.argv[2], "ci");
  if( rid==0 ){
    fossil_fatal("Check-in not found: %s", g.argv[2]);
    return;
  }

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
  zip_of_checkin(rid, &zip, zName, pInclude, pExclude);
  glob_free(pInclude);
  glob_free(pExclude);
  blob_write_to_file(&zip, g.argv[3]);
  blob_reset(&zip);
}

/*
** WEBPAGE: zip
** URL: /zip
**
** Generate a ZIP archive for the check-in specified by the "r"
** query parameter.  Return that ZIP archive as the HTTP reply content.
**
** Query parameters:
**
**   name=NAME[.zip]     The base name of the output file.  The default
**                       value is a configuration parameter in the project
**                       settings.  A prefix of the name, omitting the
**                       extension, is used as the top-most directory name.
**
**   r=TAG               The check-in that is turned into a ZIP archive.
**                       Defaults to "trunk".  This query parameter used to
**                       be called "uuid" and the older "uuid" name is still
**                       accepted for backwards compatibility.  If this
**                       query paramater is omitted, the latest "trunk"
**                       check-in is used.
**
**   in=PATTERN          Only include files that match the comma-separate
**                       list of GLOB patterns in PATTERN, as with ex=
**
**   ex=PATTERN          Omit any file that match PATTERN.  PATTERN is a
**                       comma-separated list of GLOB patterns, where each
**                       pattern can optionally be quoted using ".." or '..'.
**                       Any file matching both ex= and in= is excluded.
*/
void baseline_zip_page(void){
  int rid;
  const char *z;
  char *zName, *zRid, *zKey;
  int nName, nRid;
  const char *zInclude;         /* The in= query parameter */
  const char *zExclude;         /* The ex= query parameter */
  Blob cacheKey;                /* The key to cache */
  Glob *pInclude = 0;           /* The compiled in= glob pattern */
  Glob *pExclude = 0;           /* The compiled ex= glob pattern */
  Blob zip;                     /* ZIP archive accumulated here */

  login_check_credentials();
  if( !g.perm.Zip ){ login_needed(g.anon.Zip); return; }
  load_control();
  zName = mprintf("%s", PD("name",""));
  nName = strlen(zName);
  z = P("r");
  if( z==0 ) z = P("uuid");
  if( z==0 ) z = "trunk";
  g.zOpenRevision = zRid = fossil_strdup(z);
  nRid = strlen(zRid);
  zInclude = P("in");
  if( zInclude ) pInclude = glob_create(zInclude);
  zExclude = P("ex");
  if( zExclude ) pExclude = glob_create(zExclude);
  if( nName>4 && fossil_strcmp(&zName[nName-4], ".zip")==0 ){
    /* Special case:  Remove the ".zip" suffix.  */
    nName -= 4;
    zName[nName] = 0;
  }else{
    /* If the file suffix is not ".zip" then just remove the
    ** suffix up to and including the last "." */
    for(nName=strlen(zName)-1; nName>5; nName--){
      if( zName[nName]=='.' ){
        zName[nName] = 0;
        break;
      }
    }
  }
  rid = symbolic_name_to_rid(nRid?zRid:zName, "ci");
  if( rid<=0 ){
    cgi_set_status(404, "Not Found");
    @ Not found
    return;
  }
  if( nRid==0 && nName>10 ) zName[10] = 0;

  /* Compute a unique key for the cache entry based on query parameters */
  blob_init(&cacheKey, 0, 0);
  blob_appendf(&cacheKey, "/zip/%z", rid_to_uuid(rid));
  blob_appendf(&cacheKey, "/%q", zName);
  if( zInclude ) blob_appendf(&cacheKey, ",in=%Q", zInclude);
  if( zExclude ) blob_appendf(&cacheKey, ",ex=%Q", zExclude);
  zKey = blob_str(&cacheKey);

  if( P("debug")!=0 ){
    style_header("ZIP Archive Generator Debug Screen");
    @ zName = "%h(zName)"<br />
    @ rid = %d(rid)<br />
    if( zInclude ){
      @ zInclude = "%h(zInclude)"<br />
    }
    if( zExclude ){
      @ zExclude = "%h(zExclude)"<br />
    }
    @ zKey = "%h(zKey)"
    style_footer();
    return;
  }
  if( referred_from_login() ){
    style_header("ZIP Archive Download");
    @ <form action='%R/zip/%h(zName).zip'>
    cgi_query_parameters_to_hidden();
    @ <p>ZIP Archive named <b>%h(zName).zip</b> holding the content
    @ of check-in <b>%h(zRid)</b>:
    @ <input type="submit" value="Download" />
    @ </form>
    style_footer();
    return;
  }
  blob_zero(&zip);
  if( cache_read(&zip, zKey)==0 ){
    zip_of_checkin(rid, &zip, zName, pInclude, pExclude);
    cache_write(&zip, zKey);
  }
  glob_free(pInclude);
  glob_free(pExclude);
  fossil_free(zName);
  fossil_free(zRid);
  blob_reset(&cacheKey);
  cgi_set_content(&zip);
  cgi_set_content_type("application/zip");
}
