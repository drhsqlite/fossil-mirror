/*
** Copyright (c) 2011 D. Richard Hipp
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
** This file contains code used to generate tarballs.
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
  char *zPrevDir;           /* Name of directory for previous entry */
  int nPrevDirAlloc;        /* size of zPrevDir */
  Blob pax;                 /* PAX data */
} tball;


/*
** field lengths of 'ustar' name and prefix fields.
*/
#define USTAR_NAME_LEN    100
#define USTAR_PREFIX_LEN  155


/*
** Begin the process of generating a tarball.
**
** Initialize the GZIP compressor and the table of directory names.
*/
static void tar_begin(sqlite3_int64 mTime){
  assert( tball.aHdr==0 );
  tball.aHdr = fossil_malloc(512+512);
  memset(tball.aHdr, 0, 512+512);
  tball.zSpaces = (char*)&tball.aHdr[512];
  /* zPrevDir init */
  tball.zPrevDir = NULL;
  tball.nPrevDirAlloc = 0;
  /* scratch buffer init */
  blob_zero(&tball.pax);

  memcpy(&tball.aHdr[108], "0000000", 8);  /* Owner ID */
  memcpy(&tball.aHdr[116], "0000000", 8);  /* Group ID */
  memcpy(&tball.aHdr[257], "ustar\00000", 8);  /* POSIX.1 format */
  memcpy(&tball.aHdr[265], "nobody", 7);   /* Owner name */
  memcpy(&tball.aHdr[297], "nobody", 7);   /* Group name */
  gzip_begin(mTime);
  db_multi_exec(
    "CREATE TEMP TABLE dir(name UNIQUE);"
  );
}


/*
** verify that lla characters in 'zName' are in the
** ISO646 (=ASCII) character set.
*/
static int is_iso646_name(
  const char *zName,     /* file path */
  int nName              /* path length */
){
  int i;
  for(i = 0; i < nName; i++){
    unsigned char c = (unsigned char)zName[i];
    if( c>0x7e ) return 0;
  }
  return 1;
}


/*
**   copy string pSrc into pDst, truncating or padding with 0 if necessary
*/
static void padded_copy(
  char *pDest,
  int nDest,
  const char *pSrc,
  int nSrc
){
  if(nSrc >= nDest){
    memcpy(pDest, pSrc, nDest);
  }else{
    memcpy(pDest, pSrc, nSrc);
    memset(&pDest[nSrc], 0, nDest - nSrc);
  }
}



/******************************************************************************
**
** The 'tar' format has evolved over time. Initially the name was stored
** in a 100 byte null-terminated field 'name'. File path names were
** limited to 99 bytes.
**
** The Posix.1 'ustar' format added a 155 byte field 'prefix', allowing
** for up to 255 characters to be stored. The full file path is formed by
** concatenating the field 'prefix', a slash, and the field 'name'. This
** gives some measure of compatibility with programs that only understand
** the oldest format.
**
** The latest Posix extension is called the 'pax Interchange Format'.
** It removes all the limitations of the previous two formats by allowing
** the storage of arbitrary-length attributes in a separate object that looks
** like a file to programs that do not understand this extension. So the
** contents of the 'name' and 'prefix' fields should contain values that allow
** versions of tar that do not understand this extension to still do
** something useful.
**
******************************************************************************/

/*
** The position we use to split a file path into the 'name' and 'prefix'
** fields needs to meet the following criteria:
**
**   - not at the beginning or end of the string
**   - the position must contain a slash
**   - no more than 100 characters follow the slash
**   - no more than 155 characters precede it
**
** The routine 'find_split_pos' finds a split position. It will meet the
** criteria of listed above if such a position exists. If no such
** position exists it generates one that useful for generating the
** values used for backward compatibility.
*/
static int find_split_pos(
  const char *zName,     /* file path */
  int nName              /* path length */
){
  int i, split = 0;
  /* only search if the string needs splitting */
  if(nName > USTAR_NAME_LEN){
    for(i = 1; i+1 < nName; i++)
      if(zName[i] == '/'){
        split = i+1;
        /* if the split position is within USTAR_NAME_LEN bytes from
         * the end we can quit */
        if(nName - split <= USTAR_NAME_LEN) break;
      }
  }
  return split;
}


/*
** attempt to split the file name path to meet 'ustar' header
** criteria.
*/
static int tar_split_path(
  const char *zName,     /* path */
  int nName,             /* path length */
  char *pName,           /* name field */
  char *pPrefix          /* prefix field */
){
  int split = find_split_pos(zName, nName);
  /* check whether both pieces fit */
  if(nName - split > USTAR_NAME_LEN || split > USTAR_PREFIX_LEN+1){
    return 0; /* no */
  }

  /* extract name */
  padded_copy(pName, USTAR_NAME_LEN, &zName[split], nName - split);

  /* extract prefix */
  padded_copy(pPrefix, USTAR_PREFIX_LEN, zName, (split > 0 ? split - 1 : 0));

  return 1; /* success */
}


/*
** When using an extension header we still need to put something
** reasonable in the name and prefix fields. This is probably as
** good as it gets.
*/
static void approximate_split_path(
  const char *zName,     /* path */
  int nName,             /* path length */
  char *pName,           /* name field */
  char *pPrefix,         /* prefix field */
  int bHeader            /* is this a 'x' type tar header? */
){
  int split;

  /* if this is a Pax Interchange header prepend "PaxHeader/"
  ** so we can tell files apart from metadata */
  if( bHeader ){
    blob_reset(&tball.pax);
    blob_appendf(&tball.pax, "PaxHeader/%*.*s", nName, nName, zName);
    zName = blob_buffer(&tball.pax);
    nName = blob_size(&tball.pax);
  }

  /* find the split position */
  split = find_split_pos(zName, nName);

  /* extract a name, truncate if needed */
  padded_copy(pName, USTAR_NAME_LEN, &zName[split], nName - split);

  /* extract a prefix field, truncate when needed */
  padded_copy(pPrefix, USTAR_PREFIX_LEN, zName, (split > 0 ? split-1 : 0));
}


/*
** add a Pax Interchange header to the scratch buffer
**
** format: <length> <key>=<value>\n
** the tricky part is that each header contains its own
** size in decimal, counting that length.
*/
static void add_pax_header(
  const char *zField,
  const char *zValue,
  int nValue
){
  /* calculate length without length field */
  int blen = strlen(zField) + nValue + 3;
  /* calculate the length of the length field */
  int next10 = 1;
  int n;
  for(n = blen; n > 0; ){
    blen++; next10 *= 10;
    n /= 10;
  }
  /* adding the length extended the length field? */
  if(blen > next10){
    blen++;
  }
  /* build the string */
  blob_appendf(&tball.pax, "%d %s=%*.*s\n", blen, zField, nValue, nValue, zValue);
  /* this _must_ be right */
  if(blob_size(&tball.pax) != blen){
    fossil_fatal("internal error: PAX tar header has bad length");
  }
}


/*
** set the header type, calculate the checksum and output
** the header
*/
static void cksum_and_write_header(
  char cType
){
  unsigned int cksum = 0;
  int i;
  memset(&tball.aHdr[148], ' ', 8);
  tball.aHdr[156] = cType;
  for(i=0; i<512; i++) cksum += tball.aHdr[i];
  sqlite3_snprintf(8, (char*)&tball.aHdr[148], "%07o", cksum);
  tball.aHdr[155] = 0;
  gzip_step((char*)tball.aHdr, 512);
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
  char cType             /* Type of object:  
                            '0'==file. '2'==symlink. '5'==directory */
){
  /* set mode and modification time */
  sqlite3_snprintf(8, (char*)&tball.aHdr[100], "%07o", iMode);
  sqlite3_snprintf(12, (char*)&tball.aHdr[136], "%011o", mTime);

  /* see if we need to output a Pax Interchange Header */
  if( !is_iso646_name(zName, nName)
   || !tar_split_path(zName, nName, (char*)tball.aHdr, (char*)&tball.aHdr[345])
  ){
    int lastPage;
    /* add a file name for interoperability with older programs */
    approximate_split_path(zName, nName, (char*)tball.aHdr,
                           (char*)&tball.aHdr[345], 1);

    /* generate the Pax Interchange path header */
    blob_reset(&tball.pax);
    add_pax_header("path", zName, nName);

    /* set the header length, and write the header */
    sqlite3_snprintf(12, (char*)&tball.aHdr[124], "%011o",
                     blob_size(&tball.pax));
    cksum_and_write_header('x');

    /* write the Pax Interchange data */
    gzip_step(blob_buffer(&tball.pax), blob_size(&tball.pax));
    lastPage = blob_size(&tball.pax) % 512;
    if( lastPage!=0 ){
      gzip_step(tball.zSpaces, 512 - lastPage);
    }

    /* generate an approximate path for the regular header */
    approximate_split_path(zName, nName, (char*)tball.aHdr,
                           (char*)&tball.aHdr[345], 0);
  }
  /* set the size */
  sqlite3_snprintf(12, (char*)&tball.aHdr[124], "%011o", iSize);

  /* write the regular header */
  cksum_and_write_header(cType);
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
  if( i < tball.nPrevDirAlloc && tball.zPrevDir[i]==0 &&
        memcmp(tball.zPrevDir, zName, i)==0 ) return;
  db_multi_exec("INSERT OR IGNORE INTO dir VALUES('%#q')", i, zName);
  if( sqlite3_changes(g.db)==0 ) return;
  tar_add_directory_of(zName, i-1, mTime);
  tar_add_header(zName, i, 0755, mTime, 0, '5');
  if( i >= tball.nPrevDirAlloc ){
    int nsize = tball.nPrevDirAlloc * 2;
    if(i+1 > nsize)
      nsize = i+1;
    tball.zPrevDir = fossil_realloc(tball.zPrevDir, nsize);
    tball.nPrevDirAlloc = nsize;
  }
  memcpy(tball.zPrevDir, zName, i);
  tball.zPrevDir[i] = 0;
}


/*
** Add a single file to the growing tarball.
*/
static void tar_add_file(
  const char *zName,               /* Name of the file.  nul-terminated */
  Blob *pContent,                  /* Content of the file */
  int mPerm,                       /* 1: executable file, 2: symlink */
  unsigned int mTime               /* Last modification time of the file */
){
  int nName = strlen(zName);
  int n = blob_size(pContent);
  int lastPage;
  char cType = '0';

  /* length check moved to tar_split_path */
  tar_add_directory_of(zName, nName, mTime);

  /* 
   * If we have a symlink, write its destination path (which is stored in
   * pContent) into header, and set content length to 0 to avoid storing path
   * as file content in the next step.  Since 'linkname' header is limited to
   * 100 bytes (-1 byte for terminating zero), if path is greater than that,
   * store symlink as a plain-text file. (Not sure how TAR handles long links.)
   */
  if( mPerm == PERM_LNK && n <= 100 ){
    sqlite3_snprintf(100, (char*)&tball.aHdr[157], "%s", blob_str(pContent));
    cType = '2';
    n = 0;
  }

  tar_add_header(zName, nName, ( mPerm==PERM_EXE ) ? 0755 : 0644, 
                 mTime, n, cType);
  if( n ){
    gzip_step(blob_buffer(pContent), n);
    lastPage = n % 512;
    if( lastPage!=0 ){
      gzip_step(tball.zSpaces, 512 - lastPage);
    }
  }
}

/*
** Finish constructing the tarball.  Put the content of the tarball
** in Blob pOut.
*/
static void tar_finish(Blob *pOut){
  db_multi_exec("DROP TABLE dir");
  gzip_step(tball.zSpaces, 512);
  gzip_step(tball.zSpaces, 512);
  gzip_finish(pOut);
  fossil_free(tball.aHdr);
  tball.aHdr = 0;
  fossil_free(tball.zPrevDir);
  tball.zPrevDir = NULL;
  tball.nPrevDirAlloc = 0;
  blob_reset(&tball.pax);
}


/*
** COMMAND: test-tarball
**
** Generate a GZIP-compressed tarball in the file given by the first argument
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
  tar_begin(0);
  for(i=3; i<g.argc; i++){
    blob_zero(&file);
    blob_read_from_file(&file, g.argv[i]);
    tar_add_file(g.argv[i], &file,
                 file_wd_perm(g.argv[i]), file_wd_mtime(g.argv[i]));
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

  if( zDir && zDir[0] ){
    blob_appendf(&filename, "%s/", zDir);
  }
  nPrefix = blob_size(&filename);

  pManifest = manifest_get(rid, CFTYPE_MANIFEST);
  if( pManifest ){
    mTime = (pManifest->rDate - 2440587.5)*86400.0;
    tar_begin(mTime);
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
    tar_begin(mTime);
    tar_add_file(zName, &mfile, 0, mTime);
  }
  manifest_destroy(pManifest);
  blob_reset(&mfile);
  blob_reset(&filename);
  tar_finish(pTar);
}

/*
** COMMAND: tarball*
**
** Usage: %fossil tarball VERSION OUTPUTFILE [--name DIRECTORYNAME] [-R|--repository REPO]
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
  rid = name_to_typed_rid(g.argv[2], "ci");
  if( rid==0 ){
    fossil_fatal("Checkin not found: %s", g.argv[2]);
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
  if( !g.perm.Zip ){ login_needed(); return; }
  zName = mprintf("%s", PD("name",""));
  nName = strlen(zName);
  zRid = mprintf("%s", PD("uuid","trunk"));
  nRid = strlen(zRid);
  if( nName>7 && fossil_strcmp(&zName[nName-7], ".tar.gz")==0 ){
    /* Special case:  Remove the ".tar.gz" suffix.  */
    nName -= 7;
    zName[nName] = 0;
  }else{
    /* If the file suffix is not ".tar.gz" then just remove the
    ** suffix up to and including the last "." */
    for(nName=strlen(zName)-1; nName>5; nName--){
      if( zName[nName]=='.' ){
        zName[nName] = 0;
        break;
      }
    }
  }
  rid = name_to_typed_rid(nRid?zRid:zName, "ci");
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
