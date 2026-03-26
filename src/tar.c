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
#include "config.h"
#include <assert.h>
#include <zlib.h>
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
** Convert a string so that it contains only lower-case ASCII, digits,
** "_" and "-".  Changes are made in-place.
*/
static void sanitize_name(char *zName){
  int i;
  char c;
  if( zName==0 ) return;
  for(i=0; (c = zName[i])!=0; i++){
    if( fossil_isupper(c) ){
      zName[i] = fossil_tolower(c);
    }else if( !fossil_isalnum(c) && c!='_' && c!='-' ){
      if( c<=0x7f ){
        zName[i] = '_';
      }else{
                /*  123456789 123456789 123456  */
        zName[i] = "abcdefghijklmnopqrstuvwxyz"[(unsigned)c%26];
      }
    }
  }
}

/*
** Compute a sensible base-name for an archive file (tarball, ZIP, or SQLAR)
** based on the rid of the check-in contained in that file.
**
**      PROJECTNAME-DATETIME-HASHPREFIX
**
** So that the name will be safe to use as a URL or a filename on any system,
** the name is only allowed to contain lower-case ASCII alphabetics,
** digits, '_' and '-'.  Upper-case ASCII is converted to lower-case.  All
** other bytes are mapped into a lower-case alphabetic.
**
** The value returned is obtained from mprintf() or fossil_strdup() and should
** be released by the caller using fossil_free().
*/
char *archive_base_name(int rid){
  char *zPrefix;
  char *zName;
  zPrefix = db_get("short-project-name",0);
  if( zPrefix==0 || zPrefix[0]==0 ){
    zPrefix = db_get("project-name","unnamed");
  }
  zName = db_text(0,
    "SELECT %Q||"
          " strftime('-%%Y%%m%%d%%H%%M%%S-',event.mtime)||"
          " substr(blob.uuid,1,10)"
     " FROM blob, event"
    " WHERE blob.rid=%d"
      " AND event.objid=%d",
    zPrefix, rid, rid);
  fossil_free(zPrefix);
  sanitize_name(zName);
  return zName;
}

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
** Verify that all characters in 'zName' are in the
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
**  copy string pSrc into pDst, truncating or padding with 0 if necessary
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
  blob_appendf(&tball.pax, "%d %s=%*.*s\n",
               blen, zField, nValue, nValue, zValue);
  /* this _must_ be right */
  if((int)blob_size(&tball.pax) != blen){
    fossil_panic("internal error: PAX tar header has bad length");
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
  if( i<tball.nPrevDirAlloc
   && strncmp(tball.zPrevDir, zName, i)==0
   && tball.zPrevDir[i]==0 ) return;
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
**
**   -h|--dereference   Follow symlinks and archive the files they point to
*/
void test_tarball_cmd(void){
  int i;
  Blob zip;
  int eFType = SymFILE;
  if( g.argc<3 ){
    usage("ARCHIVE [options] FILE....");
  }
  if( find_option("dereference","h",0) ){
    eFType = ExtFILE;
  }
  sqlite3_open(":memory:", &g.db);
  tar_begin(-1);
  for(i=3; i<g.argc; i++){
    Blob file;
    blob_zero(&file);
    blob_read_from_file(&file, g.argv[i], eFType);
    tar_add_file(g.argv[i], &file, file_perm(0,eFType), file_mtime(0,eFType));
    blob_reset(&file);
  }
  tar_finish(&zip);
  blob_write_to_file(&zip, g.argv[2]);
}

/*
** Given the RID for a check-in, construct a tarball containing
** all files in that check-in that match pGlob (or all files if
** pGlob is NULL).
**
** If RID is for an object that is not a real manifest, then the
** resulting tarball contains a single file which is the RID
** object.  pInclude and pExclude are ignored in this case.
**
** If the RID object does not exist in the repository, then
** pTar is zeroed.
**
** zDir is a "synthetic" subdirectory which all files get
** added to as part of the tarball. It may be 0 or an empty string, in
** which case it is ignored. The intention is to create a tarball which
** politely expands into a subdir instead of filling your current dir
** with source files. For example, pass an artifact hash or "ProjectName".
**
*/
void tarball_of_checkin(
  int rid,             /* The RID of the check-in from which to form a tarball*/
  Blob *pTar,          /* Write the tarball into this blob */
  const char *zDir,    /* Directory prefix for all file added to tarball */
  Glob *pInclude,      /* Only add files matching this pattern */
  Glob *pExclude,      /* Exclude files matching this pattern */
  int listFlag         /* Show filenames on stdout */
){
  Blob mfile, hash, file;
  Manifest *pManifest;
  ManifestFile *pFile;
  Blob filename;
  int nPrefix;
  char *zName = 0;
  unsigned int mTime;

  content_get(rid, &mfile);
  if( blob_size(&mfile)==0 ){
    blob_zero(pTar);
    return;
  }
  blob_set_dynamic(&hash, rid_to_uuid(rid));
  blob_zero(&filename);

  if( zDir && zDir[0] ){
    blob_appendf(&filename, "%s/", zDir);
  }
  nPrefix = blob_size(&filename);

  pManifest = manifest_get(rid, CFTYPE_MANIFEST, 0);
  if( pManifest ){
    int flg, eflg = 0;
    mTime = (unsigned)((pManifest->rDate - 2440587.5)*86400.0);
    if( pTar ) tar_begin(mTime);
    flg = db_get_manifest_setting(blob_str(&hash));
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

      if( eflg & (MFESTFLG_RAW|MFESTFLG_UUID) ){
        if( eflg & MFESTFLG_RAW ){
          blob_append(&filename, "manifest", -1);
          zName = blob_str(&filename);
          if( listFlag ) fossil_print("%s\n", zName);
          if( pTar ){
            tar_add_file(zName, &mfile, 0, mTime);
          }
        }
      }
      blob_reset(&mfile);
      if( eflg & MFESTFLG_UUID ){
        blob_resize(&filename, nPrefix);
        blob_append(&filename, "manifest.uuid", -1);
        zName = blob_str(&filename);
        if( listFlag ) fossil_print("%s\n", zName);
        if( pTar ){
          blob_append(&hash, "\n", 1);
          tar_add_file(zName, &hash, 0, mTime);
        }
      }
      if( eflg & MFESTFLG_TAGS ){
        blob_resize(&filename, nPrefix);
        blob_append(&filename, "manifest.tags", -1);
        zName = blob_str(&filename);
        if( listFlag ) fossil_print("%s\n", zName);
        if( pTar ){
          Blob tagslist;
          blob_zero(&tagslist);
          get_checkin_taglist(rid, &tagslist);
          tar_add_file(zName, &tagslist, 0, mTime);
          blob_reset(&tagslist);
        }
      }
    }
    manifest_file_rewind(pManifest);
    while( (pFile = manifest_file_next(pManifest,0))!=0 ){
      int fid;
      if( pInclude!=0 && !glob_match(pInclude, pFile->zName) ) continue;
      if( glob_match(pExclude, pFile->zName) ) continue;
      fid = uuid_to_rid(pFile->zUuid, 0);
      if( fid ){
        blob_resize(&filename, nPrefix);
        blob_append(&filename, pFile->zName, -1);
        zName = blob_str(&filename);
        if( listFlag ) fossil_print("%s\n", zName);
        if( pTar ){
          content_get(fid, &file);
          tar_add_file(zName, &file, manifest_file_mperm(pFile), mTime);
          blob_reset(&file);
        }
      }
    }
  }else{
    blob_append(&filename, blob_str(&hash), 16);
    zName = blob_str(&filename);
    if( listFlag ) fossil_print("%s\n", zName);
    if( pTar ){
      mTime = db_int64(0, "SELECT (julianday('now') -  2440587.5)*86400.0;");
      tar_begin(mTime);
      tar_add_file(zName, &mfile, 0, mTime);
    }
  }
  manifest_destroy(pManifest);
  blob_reset(&mfile);
  blob_reset(&hash);
  blob_reset(&filename);
  if( pTar ) tar_finish(pTar);
}

/*
** COMMAND: tarball*
**
** Usage: %fossil tarball VERSION OUTPUTFILE [OPTIONS]
**
** Generate a compressed tarball for a specified version.  If the --name
** option is used, its argument becomes the name of the top-level directory
** in the resulting tarball.  If --name is omitted, the top-level directory
** name is derived from the project name, the check-in date and time, and
** the artifact ID of the check-in.
**
** The GLOBLIST argument to --exclude and --include can be a comma-separated
** list of glob patterns, where each glob pattern may optionally be enclosed
** in "..." or '...' so that it may contain commas.  If a file matches both
** --include and --exclude then it is excluded.
**
** If OUTPUTFILE is an empty string or "/dev/null" then no tarball is
** actually generated.  This feature can be used in combination with
** the --list option to get a list of the filenames that would be in the
** tarball had it actually been generated.  Note that --list shows only
** filenames.  "tar tzf" shows both filenames and subdirectory names.
**
** Options:
**   -X|--exclude GLOBLIST   Comma-separated list of GLOBs of files to exclude
**   --include GLOBLIST      Comma-separated list of GLOBs of files to include
**   -l|--list               Show archive content on stdout
**   --name DIRECTORYNAME    The name of the top-level directory in the archive
**   -R REPOSITORY           Specify a Fossil repository
*/
void tarball_cmd(void){
  int rid;
  Blob tarball;
  const char *zName;
  Glob *pInclude = 0;
  Glob *pExclude = 0;
  const char *zInclude;
  const char *zExclude;
  int listFlag = 0;
  const char *zOut;
  zName = find_option("name", 0, 1);
  zExclude = find_option("exclude", "X", 1);
  if( zExclude ) pExclude = glob_create(zExclude);
  zInclude = find_option("include", 0, 1);
  if( zInclude ) pInclude = glob_create(zInclude);
  db_find_and_open_repository(0, 0);
  listFlag = find_option("list","l",0)!=0;

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
  zOut = g.argv[3];
  if( fossil_strcmp("/dev/null",zOut)==0 || fossil_strcmp("",zOut)==0 ){
    zOut = 0;
  }

  if( zName==0 ){
    zName = archive_base_name(rid);
  }
  tarball_of_checkin(rid, zOut ? &tarball : 0,
                     zName, pInclude, pExclude, listFlag);
  glob_free(pInclude);
  glob_free(pExclude);
  if( listFlag ) fflush(stdout);
  if( zOut ){
    blob_write_to_file(&tarball, zOut);
    blob_reset(&tarball);
  }
}

/*
** This is a helper routine for tar_uuid_from_name().  It handles
** the case where *pzName contains no "/" character.  Check for
** format (3).  Return the hash if the name matches format (3),
** or return NULL if it does not.
*/
static char *format_three_parser(const char *zName){
  int iDot = 0;    /* Index in zName[] of the first '.' */
  int iDash1 = 0;  /* Index in zName[] of the '-' before the timestamp */
  int iDash2 = 0;  /* Index in zName[] of the '-' between timestamp and hash */
  int nHash;       /* Size of the hash */
  char *zHash;     /* A copy of the hash value */
  char *zDate;     /* Copy of the timestamp */
  char *zUuid;     /* Final result */
  int i;           /* Loop query */
  Stmt q;          /* Query to verify that hash and timestamp agree */

  for(i=0; zName[i]; i++){
    char c = zName[i];
    if( c=='.' ){ iDot = i;  break; }
    if( c=='-' ){ iDash1 = iDash2; iDash2 = i; }
    if( !fossil_isalnum(c) && c!='_' && c!='-' ){ break; }
  }
  if( iDot==0 ) return 0;
  if( iDash1==0 ) return 0;
  nHash = iDot - iDash2 - 1;
  if( nHash<8 ) return 0;                /* HASH value too short */  
  if( (iDash2 - iDash1)!=15 ) return 0;  /* Wrong timestamp size */
  zHash = fossil_strndup(&zName[iDash2+1], nHash);
  zDate = fossil_strndup(&zName[iDash1+1], 14);
  db_prepare(&q, 
    "SELECT blob.uuid"
    "  FROM blob JOIN event ON event.objid=blob.rid"
    " WHERE blob.uuid GLOB '%q*'"
    "   AND strftime('%%Y%%m%%d%%H%%M%%S',event.mtime)='%q'", 
    zHash, zDate
  );
  fossil_free(zHash);
  fossil_free(zDate);
  if( db_step(&q)==SQLITE_ROW ){
    zUuid = fossil_strdup(db_column_text(&q,0));
  }else{
    zUuid = 0;
  }
  db_finalize(&q);
  return zUuid;
}

/*
** Check to see if the input string is of one of the following
** two the forms:
**
**        check-in-name/filename.ext                       (1)
**        tag-name/check-in-name/filename.ext              (2)
**        project-datetime-hash.ext                        (3)
**
** In other words, check to see if the input string contains either
** a check-in name or a tag-name and a check-in name separated by
** a slash.  There must be between 0 or 2 "/" characters.  In the
** second form, tag-name must be an individual tag (not a branch-tag)
** that is found on the check-in identified by the check-in-name.
**
** If the condition is true, then:
**
**   *  Make *pzName point to the filename suffix only
**   *  return a copy of the check-in name in memory from mprintf().
**
** If the condition is false, leave *pzName unchanged and return either
** NULL or an empty string.  Normally NULL is returned, however an
** empty string is returned for format (2) if check-in-name does not
** match tag-name.
**
** Format (2) is specifically designed to allow URLs like this:
**
**      /tarball/release/UUID/PROJECT.tar.gz
**
** Such URLs will pass through most anti-robot filters because of the
** "/tarball/release" prefix will match the suggested "robot-exception"
** pattern and can still refer to an historic release rather than just
** the most recent release.
**
** Format (3) is designed to allow URLs like this:
**
**     /tarball/fossil-20251018193920-d6c9aee97df.tar.gz
**
** In other words, filename itself contains sufficient information to
** uniquely identify the check-in, including a timestamp of the form
** YYYYMMDDHHMMSS and a prefix of the check-in hash.  The timestamp
** and hash must immediately precede the first "." in the name.
*/
char *tar_uuid_from_name(char **pzName){
  char *zName = *pzName;      /* Original input */
  int n1 = 0;                 /* Bytes in first prefix (tag-name) */
  int n2 = 0;                 /* Bytes in second prefix (check-in-name) */
  int n = 0;                  /* max(n1,n2) */
  int i;                      /* Loop counter */
  for(i=n1=n2=0; zName[i]; i++){
    if( zName[i]=='/' ){
      if( n1==0 ){
        n = n1 = i;
      }else if( n2==0 ){
        n = n2 = i;
      }else{
        return 0;   /* More than two "/" characters seen */
      }
    }
  }
  if( n1==0 ){
    /* Check for format (3) */
    return format_three_parser(*pzName);
  }
  if( zName[n+1]==0 ){
    return 0;    /* No filename suffix */
  }
  if( n2==0 ){
    /* Format (1): check-in name only.  The check-in-name is not verified */
    zName[n1] = 0;
    *pzName = fossil_strdup(&zName[n1+1]);
    return zName;
  }else if( n2>n1+1 ){
    /* Format (2): tag-name/check-in-name.  Verify that check-in-name is real
    ** and that the check-in has the tag named by tag-name.
    */
    char *zCkin = mprintf("%.*s", n2-n1-1, &zName[n1+1]);
    char *zTag;
    int rid = symbolic_name_to_rid(zCkin,"ci");
    int hasTag;
    if( rid<=0 ){
      fossil_free(zCkin);
      return fossil_strdup("");
    }
    zTag = mprintf("%.*s", n1, zName);
    hasTag = db_exists(
      "SELECT 1 FROM tagxref, tag"
      " WHERE tagxref.rid=%d"
      "   AND tag.tagid=tagxref.tagid"
      "   AND tagxref.tagtype=1"
      "   AND tag.tagname='sym-%q'",
      rid, zTag
    );
    fossil_free(zTag);
    if( !hasTag ){
      fossil_free(zCkin);
      return fossil_strdup("");
    }
    *pzName = fossil_strdup(&zName[n2+1]);
    return zCkin;             
  }else{
    return 0;
  }
}

/*
** WEBPAGE: tarball
** URL: /tarball/NAME.tar.gz
**  or: /tarball/VERSION/NAME.tar.gz
**  or: /tarball/TAG/VERSION/NAME.tar.gz
**
** Generate a compressed tarball for the check-in specified by VERSION.
** The tarball is called NAME.tar.gz and has a top-level directory called
** NAME.  If TAG is provided, then VERSION must hold TAG or else an error
** is returned.
**
** The optional VERSION element defaults to the name of the main branch
** (usually "trunk") per the r= rules below.
** All of the following URLs are equivalent:
**
**      /tarball/release/xyz.tar.gz
**      /tarball?r=release&name=xyz.tar.gz
**      /tarball/xyz.tar.gz?r=release
**      /tarball?name=release/xyz.tar.gz
**
** Query parameters:
**
**   name=[CKIN/]NAME    The optional CKIN component of the name= parameter
**                       identifies the check-in from which the tarball is
**                       constructed.  If CKIN is omitted and there is no
**                       r= query parameter, then use the name of the main
**                       branch (usually "trunk").  NAME is the
**                       name of the download file.  The top-level directory
**                       in the generated tarball is called by NAME with the
**                       file extension removed.
**
**   r=TAG               TAG identifies the check-in that is turned into a
**                       compressed tarball.  The default value is the name of
**                       the main branch (usually "trunk").
**                       If r= is omitted and if the name= query parameter
**                       contains one "/" character then the of part the
**                       name= value before the / becomes the TAG and the
**                       part of the name= value  after the / is the download
**                       filename.  If no check-in is specified by either
**                       name= or r=, then the name of the main branch
**                       (usually "trunk") is used.
**
**   in=PATTERN          Only include files that match the comma-separated
**                       list of GLOB patterns in PATTERN, as with ex=
**
**   ex=PATTERN          Omit any file that match PATTERN.  PATTERN is a
**                       comma-separated list of GLOB patterns, where each
**                       pattern can optionally be quoted using ".." or '..'.
**                       Any file matching both ex= and in= is excluded.
**
** Robot Defenses:
**
**   *    If "zip" appears in the robot-restrict setting, then robots are
**        not allowed to access this page.  Suspected robots will be
**        presented with a captcha.
**
**   *    If "zipX" appears in the robot-restrict setting, then robots are
**        restricted in the same way as with "zip", but with exceptions.
**        If the check-in for which an archive is requested is a leaf check-in
**        and if the robot-zip-leaf setting is true, then the request is
**        allowed.  Or if the check-in has a tag that matches any of the
**        GLOB patterns on the list in the robot-zip-tag setting, then the
**        request is allowed.  Otherwise, the usual robot defenses are
**        activated.
*/
void tarball_page(void){
  int rid;
  char *zName, *zRid, *zKey;
  int nName, nRid;
  const char *zInclude;         /* The in= query parameter */
  const char *zExclude;         /* The ex= query parameter */
  Blob cacheKey;                /* The key to cache */
  Glob *pInclude = 0;           /* The compiled in= glob pattern */
  Glob *pExclude = 0;           /* The compiled ex= glob pattern */
  Blob tarball;                 /* Tarball accumulated here */
  const char *z;

  login_check_credentials();
  if( !g.perm.Zip ){ login_needed(g.anon.Zip); return; }
  if( robot_restrict("zip") ) return;
  fossil_nice_default();
  zName = fossil_strdup(PD("name",""));
  z = P("r");
  if( z==0 ) z = P("uuid");
  if( z==0 ) z = tar_uuid_from_name(&zName);
  if( z==0 ) z = fossil_strdup(db_main_branch());
  g.zOpenRevision = zRid = fossil_strdup(z);
  nRid = strlen(zRid);
  zInclude = P("in");
  if( zInclude ) pInclude = glob_create(zInclude);
  zExclude = P("ex");
  if( zExclude ) pExclude = glob_create(zExclude);
  if( zInclude==0 && zExclude==0 ){
    etag_check_for_invariant_name(z);
  }
  nName = strlen(zName);
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
  rid = symbolic_name_to_rid(nRid?zRid:zName, "ci");
  if( rid==0 ){
    cgi_set_status(404, "Not Found");
    @ Not found
    return;
  }
  if( robot_restrict_zip(rid) ) return;
  if( nRid==0 && nName>10 ) zName[10] = 0;

  /* Compute a unique key for the cache entry based on query parameters */
  blob_init(&cacheKey, 0, 0);
  blob_appendf(&cacheKey, "/tarball/%z", rid_to_uuid(rid));
  blob_appendf(&cacheKey, "/%q", zName);
  if( zInclude ) blob_appendf(&cacheKey, ",in=%Q", zInclude);
  if( zExclude ) blob_appendf(&cacheKey, ",ex=%Q", zExclude);
  zKey = blob_str(&cacheKey);
  etag_check(ETAG_HASH, zKey);

  if( P("debug")!=0 ){
    style_header("Tarball Generator Debug Screen");
    @ zName = "%h(zName)"<br>
    @ rid = %d(rid)<br>
    if( zInclude ){
      @ zInclude = "%h(zInclude)"<br>
    }
    if( zExclude ){
      @ zExclude = "%h(zExclude)"<br>
    }
    @ zKey = "%h(zKey)"
    style_finish_page();
    return;
  }
  if( referred_from_login() ){
    style_header("Tarball Download");
    @ <form action='%R/tarball/%h(zName).tar.gz'>
    cgi_query_parameters_to_hidden();
    @ <p>Tarball named <b>%h(zName).tar.gz</b> holding the content
    @ of check-in <b>%h(zRid)</b>:
    @ <input type="submit" value="Download">
    @ </form>
    style_finish_page();
    return;
  }
  cgi_check_for_malice();
  blob_zero(&tarball);
  if( cache_read(&tarball, zKey)==0 ){
    tarball_of_checkin(rid, &tarball, zName, pInclude, pExclude, 0);
    cache_write(&tarball, zKey);
  }
  glob_free(pInclude);
  glob_free(pExclude);
  fossil_free(zName);
  fossil_free(zRid);
  g.zOpenRevision = 0;
  blob_reset(&cacheKey);
  cgi_set_content(&tarball);
  cgi_set_content_type("application/x-compressed");
}

/*
** This routine is called for each check-in on the /download page to
** construct the "extra" information after the description.
*/
void download_extra(
  Stmt *pQuery,               /* Current row of the timeline query */
  int tmFlags,                /* Flags to www_print_timeline() */
  const char *zThisUser,      /* Suppress links to this user */
  const char *zThisTag        /* Suppress links to this tag */
){
  const char *zType = db_column_text(pQuery, 7);
  assert( zType!=0 );
  if( zType[0]!='c' ){
    timeline_extra(pQuery, tmFlags, zThisUser, zThisTag);
  }else{    
    int rid = db_column_int(pQuery, 0);
    const char *zUuid = db_column_text(pQuery, 1);
    char *zBrName = branch_of_rid(rid);
    char *zNm;

    if( tmFlags & TIMELINE_COLUMNAR ){
      @ <nobr>check-in:&nbsp;\
      @   %z(href("%R/info/%!S",zUuid))<span class='timelineHash'>\
      @   %S(zUuid)</span></a></nobr><br>
      if( fossil_strcmp(zBrName,"trunk")!=0 ){
        @ <nobr>branch:&nbsp;\
        @   %z(href("%R/timeline?r=%t",zBrName))%h(zBrName)</a></nobr><br>\
      }
    }else{
      if( (tmFlags & TIMELINE_CLASSIC)==0 ){
        @ check-in:&nbsp;%z(href("%R/info/%!S",zUuid))\
        @ <span class='timelineHash'>%S(zUuid)</span></a>
      }
      if( (tmFlags & TIMELINE_GRAPH)==0 && fossil_strcmp(zBrName,"trunk")!=0 ){
        @ branch:&nbsp;\
        @   %z(href("%R/timeline?r=%t",zBrName))%h(zBrName)</a>
      }
    }
    zNm = archive_base_name(rid);
    @ %z(href("%R/tarball/%s.tar.gz",zNm))\
    @    <button>Tarball</button></a>
    @  %z(href("%R/zip/%s.zip",zNm))\
    @    <button>ZIP&nbsp;Archive</button></a>
    fossil_free(zBrName);
    fossil_free(zNm);
  }
}

/*
** SETTING: suggested-downloads               width=70  block-text
**
** This setting controls the suggested tarball/ZIP downloads on the
** [[/download]] page.  The value is a TCL list.  Each set of four items
** defines a set of check-ins to be added to the suggestion list.
** The items in each group are:
**
** |    COUNT   TAG   MAX_AGE    COMMENT
**
** COUNT is the number of check-ins to match, starting with the most
** recent and working bacwards in time.  Check-ins match if they contain
** the tag TAG.  If MAX_AGE is not an empty string, then it specifies
** the maximum age of any matching check-in.  COMMENT is an optional
** comment for each match.
**
** The special value of "OPEN-LEAF" for TAG matches any check-in that
** is an open leaf.
**
** MAX_AGE is of the form "{AMT UNITS}"  where AMT is a floating point
** value and UNITS is one of "seconds", "hours", "days", "weeks", "months",
** or "years".  If MAX_AGE is an empty string then there is no age limit.
**
** If COMMENT is not an empty string, then it is an additional comment
** added to the output description of the suggested download.  The idea of
** COMMENT is to explain to the reader why a check-in is a suggested
** download.  
**
** Example:
**
** |       1   trunk     {}         {Latest Trunk Check-in}
** |       5   OPEN-LEAF {1 month}  {Active Branch}
** |       999 release   {1 year}   {Official Release}
**
** The value causes the /download page to show the union of the most
** recent trunk check-in of any age, the five most recent
** open leaves within the past month, and essentially
** all releases within the past year.  If the same check-in matches more
** than one rule, the COMMENT of the first match is used.
*/

/*
** WEBPAGE: /download
**
** Show a special no-graph timeline of recent important check-ins with
** an opportunity to pull tarballs and ZIPs.
*/
void download_page(void){
  Stmt q;                       /* The actual timeline query */
  const char *zTarlistCfg;      /* Configuration string */
  char **azItem;                /* Decomposed elements of zTarlistCfg */
  int *anItem;                  /* Bytes in each term of azItem[] */
  int nItem;                    /* Number of terms in azItem[] */
  int i;                        /* Loop counter */
  int tmFlags;                  /* Timeline display flags */
  int n;                        /* Number of suggested downloads */
  double rNow;                  /* Current time.  Julian day number */
  int bPlainTextCom;            /* Use plain-text comments */

  login_check_credentials();
  if( !g.perm.Zip ){ login_needed(g.anon.Zip); return; }

  style_set_current_feature("timeline");
  style_header("Suggested Downloads");

  zTarlistCfg = db_get("suggested-downloads","off");
  db_multi_exec(
    "CREATE TEMP TABLE tarlist(rid INTEGER PRIMARY KEY, com TEXT);"
  );
  rNow = db_double(0.0,"SELECT julianday()");
  if( !g.interp ) Th_FossilInit(0);
  Th_SplitList(g.interp, zTarlistCfg, (int)strlen(zTarlistCfg),
                   &azItem, &anItem, &nItem);
  bPlainTextCom = db_get_boolean("timeline-plaintext",0);
  for(i=0; i<nItem-3; i+=4){
    int cnt;             /* The number of instances of zLabel to use */
    char *zLabel;        /* The label to match */
    double rStart;       /* Starting time, Julian day number */
    char *zComment = 0;  /* Comment to apply */
    if( anItem[i]==1 && azItem[i][0]=='*' ){
      cnt = -1;
    }else if( anItem[i]<1 ){
      cnt = 0;
    }else{
      cnt = atoi(azItem[i]);
    }
    if( cnt==0 ) continue;
    zLabel = fossil_strndup(azItem[i+1],anItem[i+1]);
    if( anItem[i+2]==0 ){
      rStart = 0.0;
    }else{
      char *zMax = fossil_strndup(azItem[i+2], anItem[i+2]);
      double r = atof(zMax);
      if( strstr(zMax,"sec") ){
        rStart = rNow - r/86400.0;
      }else
      if( strstr(zMax,"hou") ){
        rStart = rNow - r/24.0;
      }else
      if( strstr(zMax,"da") ){
        rStart = rNow - r;
      }else
      if( strstr(zMax,"wee") ){
        rStart = rNow - r*7.0;
      }else
      if( strstr(zMax,"mon") ){
        rStart = rNow - r*30.44;
      }else
      if( strstr(zMax,"yea") ){
        rStart = rNow - r*365.24;
      }else
      { /* Default to seconds */
        rStart = rNow - r/86400.0;
      }
    }
    if( anItem[i+3]==0 ){
      zComment = fossil_strdup("");
    }else if( bPlainTextCom ){
      zComment = mprintf("** %.*s ** ", anItem[i+3], azItem[i+3]);
    }else{
      zComment = mprintf("<b>%.*s</b>\n<p>", anItem[i+3], azItem[i+3]);
    }
    if( fossil_strcmp("OPEN-LEAF",zLabel)==0 ){
      db_multi_exec(
        "INSERT OR IGNORE INTO tarlist(rid,com)"
         " SELECT leaf.rid, %Q FROM leaf, event"
          " WHERE event.objid=leaf.rid"
            " AND event.mtime>=%.6f"
            " AND NOT EXISTS(SELECT 1 FROM tagxref"
                            " WHERE tagxref.rid=leaf.rid"
                              " AND tagid=%d AND tagtype>0)"
          " ORDER BY event.mtime DESC LIMIT %d",
          zComment, rStart, TAG_CLOSED, cnt
      );
    }else{
      db_multi_exec(
        "WITH taglist(tid) AS"
            " (SELECT tagid FROM tag WHERE tagname GLOB 'sym-%q')"
        "INSERT OR IGNORE INTO tarlist(rid,com)"
        " SELECT event.objid, %Q FROM event CROSS JOIN tagxref"
        "  WHERE event.type='ci'"
        "    AND event.mtime>=%.6f"
        "    AND tagxref.tagid IN taglist"
        "    AND tagtype>0"
        "    AND tagxref.rid=event.objid"
        "  ORDER BY event.mtime DESC LIMIT %d",
        zLabel, zComment, rStart, cnt
      );
    }
    fossil_free(zLabel);
    fossil_free(zComment);
  }
  Th_Free(g.interp, azItem);

  n = db_int(0, "SELECT count(*) FROM tarlist");
  if( n==0 ){
    @ <h2>No tarball/ZIP suggestions are available at this time</h2>
  }else{
    @ <h2>%d(n) Tarball/ZIP Download Suggestion%s(n>1?"s":""):</h2>
    db_prepare(&q,
      "WITH matches AS (%s AND blob.rid IN (SELECT rid FROM tarlist))\n"
      "SELECT blobRid, uuid, timestamp,"
            " com||comment,"
            " user, leaf, bgColor, eventType, tags, tagid, brief, mtime"
      "  FROM matches JOIN tarlist ON tarlist.rid=blobRid"
      " ORDER BY matches.mtime DESC",
      timeline_query_for_www()
    );

    tmFlags = TIMELINE_DISJOINT | TIMELINE_NOSCROLL | TIMELINE_COLUMNAR
            | TIMELINE_BRCOLOR;
    www_print_timeline(&q, tmFlags, 0, 0, 0, 0, 0, download_extra);
    db_finalize(&q);
  }
  if( g.perm.Clone ){
    char *zNm = fossil_strdup(db_get("project-name","clone"));
    sanitize_name(zNm);    
    @ <hr>
    @ <h2>You Can Clone This Repository</h2>
    @
    @ <p>Clone this repository by running a command similar to the following:
    @ <blockquote><pre>
    @ fossil  clone  %s(g.zBaseURL)  %h(zNm).fossil
    @ </pre></blockquote>
    @ <p>A clone gives you local access to all historical content.
    @ Cloning is a bandwidth- and CPU-efficient alternative to extracting
    @ multiple tarballs and ZIPs.
    @ Do a web search for "fossil clone" or similar to find additional
    @ information about using a cloned Fossil repository.  Or ask your
    @ favorite AI how to extract content from a Fossil clone.
    fossil_free(zNm);
  }

  style_finish_page();
}

/*
** WEBPAGE: rchvdwnld
**
** Short for "archive download".  This page should have a single name=
** query parameter that is a check-in hash or symbolic name.  The resulting
** page offers a menu of possible download options for that check-in,
** including tarball, ZIP, or SQLAR.
**
** This is a utility page.  The /dir and /tree pages sometimes have a
** "Download" option in their submenu which redirects here.  Those pages
** used to have separate "Tarball" and "ZIP" submenu entries, but as
** submenu entries appear in alphabetical order, that caused the two
** submenu entries to be separated from one another, which is distracting.
**
** If the name= does not have a unique resolution, no error is generated.
** Instead, a redirect to the home page for the repository is made.
**
** Robots are excluded from this page if either of the keywords
** "zip" or "download" appear in the [[robot-restrict]] setting.
*/
void rchvdwnld_page(void){
  const char *zUuid;
  char *zBase;
  int nUuid;
  int rid;
  char *zTags;
  login_check_credentials();
  if( !g.perm.Zip ){ login_needed(g.anon.Zip); return; }
  if( robot_restrict("zip") || robot_restrict("download") ) return;

  zUuid = P("name");
  if( zUuid==0
   || (nUuid = (int)strlen(zUuid))<6
   || !validate16(zUuid,-1)
   || (rid = db_int(0, "SELECT rid FROM blob WHERE uuid GLOB '%q*'", zUuid))==0
   || !db_exists("SELECT 1 from event WHERE type='ci' AND objid=%d",rid)
  ){
    rid = symbolic_name_to_rid(zUuid, "ci");
    if( rid<=0 ){
      fossil_redirect_home();
    }
  }
  zUuid = db_text(zUuid, "SELECT uuid FROM blob WHERE rid=%d", rid);
  zTags = db_text(0,
    "SELECT if(cnt,' ('||tags||')','') FROM ("
      "SELECT group_concat(substr(tagname,5),', ') AS tags, count(*) AS cnt"
      "  FROM tag JOIN tagxref USING(tagid)"
      " WHERE rid=%d"
      "   AND tagtype=1"
      "   AND tagname GLOB 'sym-*'"
    ")",
    rid
  );
  style_header("Downloads For Check-in %!S", zUuid);
  zBase = archive_base_name(rid);
  @ <div class="section accordion">Downloads for check-in \
  @ %z(href("%R/info/%!S",zUuid))%S(zUuid)</a>%h(zTags)</div>
  @ <div class="accordion_panel">
  @ <table class="label-value">
  @ <tr>
  @ <th>Tarball:</th>
  @ <td>%z(href("%R/tarball/%s.tar.gz",zBase))\
  @ %s(g.zBaseURL)/tarball/%s(zBase).tar.gz</a></td>
  @ </tr>
  @
  @ <tr>
  @ <th>ZIP:</th>
  @ <td>%z(href("%R/zip/%s.zip",zBase))\
  @ %s(g.zBaseURL)/zip/%s(zBase).zip</a></td>
  @ </tr>
  @
  @ <tr>
  @ <th>SQLAR:</th>
  @ <td>%z(href("%R/sqlar/%s.sqlar",zBase))\
  @ %s(g.zBaseURL)/sqlar/%s(zBase).sqlar</a></td>
  @ </tr>
  @ </table></div>
  fossil_free(zBase);
  @ <div class="section accordion">Context</div><div class="accordion_panel">
  render_checkin_context(rid, 0, 0, 0);
  @ </div>
  builtin_request_js("accordion.js");
  style_finish_page();
}
