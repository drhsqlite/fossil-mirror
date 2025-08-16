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
** This file contains code used to generate ZIP and SQLAR archives.
*/
#include "config.h"
#include <assert.h>
#include <zlib.h>
#include "zip.h"

/*
** Type of archive to build.
*/
#define ARCHIVE_ZIP   0
#define ARCHIVE_SQLAR 1

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

typedef struct Archive Archive;
struct Archive {
  int eType;                      /* Type of archive (SQLAR or ZIP) */
  Blob *pBlob;                    /* Output blob */
  Blob tmp;                       /* Blob used as temp space for compression */
  sqlite3 *db;                    /* Db used to assemble sqlar archive */
  sqlite3_stmt *pInsert;          /* INSERT statement for SQLAR */
  sqlite3_vfs vfs;                /* VFS object */
};

/*
** Ensure that blob pBlob is at least nMin bytes in size.
*/
static void zip_blob_minsize(Blob *pBlob, int nMin){
  if( (int)blob_size(pBlob)<nMin ){
    blob_resize(pBlob, nMin);
  }
}

/*************************************************************************
** Implementation of "archive" VFS. A VFS designed to store the contents
** of a new database in a Blob. Used to construct sqlar archives in
** memory.
*/
typedef struct ArchiveFile ArchiveFile;
struct ArchiveFile {
  sqlite3_file base;              /* Base class */
  Blob *pBlob;
};

static int archiveClose(sqlite3_file *pFile){
  return SQLITE_OK;
}
static int archiveRead(
    sqlite3_file *pFile, void *pBuf, int iAmt, sqlite3_int64 iOfst
){
  assert( iOfst==0 || iOfst==24 );
  return SQLITE_IOERR_SHORT_READ;
}
static int archiveWrite(
    sqlite3_file *pFile, const void *pBuf, int iAmt, sqlite3_int64 iOfst
){
  ArchiveFile *pAF = (ArchiveFile*)pFile;
  int nMin = (int)iOfst + iAmt;
  char *aBlob;                    /* Output buffer */

  zip_blob_minsize(pAF->pBlob, nMin);
  aBlob = blob_buffer(pAF->pBlob);
  memcpy(&aBlob[iOfst], pBuf, iAmt);
  return SQLITE_OK;
}
static int archiveTruncate(sqlite3_file *pFile, sqlite3_int64 size){
  return SQLITE_OK;
}
static int archiveSync(sqlite3_file *pFile, int flags){
  return SQLITE_OK;
}
static int archiveFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize){
  *pSize = 0;
  return SQLITE_OK;
}
static int archiveLock(sqlite3_file *pFile, int eLock){
  return SQLITE_OK;
}
static int archiveUnlock(sqlite3_file *pFile, int eLock){
  return SQLITE_OK;
}
static int archiveCheckReservedLock(sqlite3_file *pFile, int *pResOut){
  *pResOut = 0;
  return SQLITE_OK;
}
static int archiveFileControl(sqlite3_file *pFile, int op, void *pArg){
  if( op==SQLITE_FCNTL_SIZE_HINT ){
    ArchiveFile *pAF = (ArchiveFile*)pFile;
    zip_blob_minsize(pAF->pBlob, (int)(*(sqlite3_int64*)pArg));
  }
  return SQLITE_NOTFOUND;
}
static int archiveSectorSize(sqlite3_file *pFile){
  return 512;
}
static int archiveDeviceCharacteristics(sqlite3_file *pFile){
  return 0;
}

static int archiveOpen(
  sqlite3_vfs *pVfs, const char *zName,
  sqlite3_file *pFile, int flags, int *pOutFlags
){
  static struct sqlite3_io_methods methods = {
    1,             /* iVersion */
    archiveClose,
    archiveRead,
    archiveWrite,
    archiveTruncate,
    archiveSync,
    archiveFileSize,
    archiveLock,
    archiveUnlock,
    archiveCheckReservedLock,
    archiveFileControl,
    archiveSectorSize,
    archiveDeviceCharacteristics,
    0, 0, 0, 0,
    0, 0
  };

  ArchiveFile *pAF = (ArchiveFile*)pFile;
  assert( flags & SQLITE_OPEN_MAIN_DB );

  pAF->base.pMethods = &methods;
  pAF->pBlob = (Blob*)pVfs->pAppData;

  return SQLITE_OK;
}
static int archiveDelete(sqlite3_vfs *pVfs, const char *zName, int syncDir){
  return SQLITE_OK;
}
static int archiveAccess(
    sqlite3_vfs *pVfs, const char *zName, int flags, int *pResOut
){
  *pResOut = 0;
  return SQLITE_OK;
}
static int archiveFullPathname(
  sqlite3_vfs *pVfs, const char *zIn, int nOut, char *zOut
){
  int n = strlen(zIn);
  memcpy(zOut, zIn, n+1);
  return SQLITE_OK;
}
static int archiveRandomness(sqlite3_vfs *pVfs, int nByte, char *zOut){
  memset(zOut, 0, nByte);
  return SQLITE_OK;
}
static int archiveSleep(sqlite3_vfs *pVfs, int microseconds){
  return SQLITE_OK;
}
static int archiveCurrentTime(sqlite3_vfs *pVfs, double *prOut){
  return SQLITE_OK;
}
static int archiveGetLastError(sqlite3_vfs *pVfs, int nBuf, char *aBuf){
  return SQLITE_OK;
}
/*
** End of "archive" VFS.
*************************************************************************/

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
  unixTime = (int)((rDate - 2440587.5)*86400.0);
}

/*
** Append a single file to a growing ZIP archive.
**
** pFile is the file to be appended.  zName is the name
** that the file should be saved as.
*/
static void zip_add_file_to_zip(
  Archive *p,
  const char *zName,
  const Blob *pFile,
  int mPerm
){
  z_stream stream;
  int nameLen;
  int toOut = 0;
  int iStart;
  unsigned long iCRC = 0;
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
  nameLen = (int)strlen(zName);
  if( nameLen==0 ) return;
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

static void zip_add_file_to_sqlar(
  Archive *p,
  const char *zName,
  const Blob *pFile,
  int mPerm
){
  int nName = (int)strlen(zName);

  if( p->db==0 ){
    assert( p->vfs.zName==0 );
    p->vfs.zName = (const char*)mprintf("archivevfs%p", (void*)p);
    p->vfs.iVersion = 1;
    p->vfs.szOsFile = sizeof(ArchiveFile);
    p->vfs.mxPathname = 512;
    p->vfs.pAppData = (void*)p->pBlob;
    p->vfs.xOpen = archiveOpen;
    p->vfs.xDelete = archiveDelete;
    p->vfs.xAccess = archiveAccess;
    p->vfs.xFullPathname = archiveFullPathname;
    p->vfs.xRandomness = archiveRandomness;
    p->vfs.xSleep = archiveSleep;
    p->vfs.xCurrentTime = archiveCurrentTime;
    p->vfs.xGetLastError = archiveGetLastError;
    sqlite3_vfs_register(&p->vfs, 0);
    sqlite3_open_v2("file:xyz.db", &p->db,
        SQLITE_OPEN_CREATE|SQLITE_OPEN_READWRITE, p->vfs.zName
    );
    assert( p->db );
    blob_zero(&p->tmp);
    sqlite3_exec(p->db,
        "PRAGMA page_size=512;"
        "PRAGMA journal_mode = off;"
        "PRAGMA cache_spill = off;"
        "BEGIN;"
        "CREATE TABLE sqlar("
          "name TEXT PRIMARY KEY,  -- name of the file\n"
          "mode INT,               -- access permissions\n"
          "mtime INT,              -- last modification time\n"
          "sz INT,                 -- original file size\n"
          "data BLOB               -- compressed content\n"
        ");", 0, 0, 0
    );
    sqlite3_prepare(p->db,
        "INSERT INTO sqlar VALUES(?, ?, ?, ?, ?)", -1,
        &p->pInsert, 0
    );
    assert( p->pInsert );

    sqlite3_bind_int64(p->pInsert, 3, unixTime);
    blob_zero(p->pBlob);
  }

  if( nName==0 ) return;
  if( pFile==0 ){
    /* Directory. */
    if( zName[nName-1]=='/' ) nName--;
    sqlite3_bind_text(p->pInsert, 1, zName, nName, SQLITE_STATIC);
    sqlite3_bind_int(p->pInsert, 2, 040755);
    sqlite3_bind_int(p->pInsert, 4, 0);
    sqlite3_bind_null(p->pInsert, 5);
  }else{
    sqlite3_bind_text(p->pInsert, 1, zName, nName, SQLITE_STATIC);
    if( mPerm==PERM_LNK ){
      sqlite3_bind_int(p->pInsert, 2, 0120755);
      sqlite3_bind_int(p->pInsert, 4, -1);
      sqlite3_bind_text(p->pInsert, 5,
          blob_buffer(pFile), blob_size(pFile), SQLITE_STATIC
      );
    }else{
      unsigned int nIn = blob_size(pFile);
      unsigned long int nOut = nIn;
      sqlite3_bind_int(p->pInsert, 2, mPerm==PERM_EXE ? 0100755 : 0100644);
      sqlite3_bind_int(p->pInsert, 4, nIn);
      zip_blob_minsize(&p->tmp, nIn);
      compress( (unsigned char*)
          blob_buffer(&p->tmp), &nOut, (unsigned char*)blob_buffer(pFile), nIn
      );
      if( nOut>=(unsigned long)nIn ){
        sqlite3_bind_blob(p->pInsert, 5,
            blob_buffer(pFile), blob_size(pFile), SQLITE_STATIC
        );
      }else{
        sqlite3_bind_blob(p->pInsert, 5,
            blob_buffer(&p->tmp), nOut, SQLITE_STATIC
        );
      }
    }
  }

  sqlite3_step(p->pInsert);
  sqlite3_reset(p->pInsert);
}

static void zip_add_file(
  Archive *p,
  const char *zName,
  const Blob *pFile,
  int mPerm
){
  if( p->eType==ARCHIVE_ZIP ){
    zip_add_file_to_zip(p, zName, pFile, mPerm);
  }else{
    zip_add_file_to_sqlar(p, zName, pFile, mPerm);
  }
}

/*
** If the given filename includes one or more directory entries, make
** sure the directories are already in the archive.  If they are not
** in the archive, add them.
*/
static void zip_add_folders(Archive *p, char *zName){
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
        azDir[j] = fossil_strdup(zName);
        zip_add_file(p, zName, 0, 0);
      }
      zName[i+1] = c;
    }
  }
}

/*
** Free all the members of structure Archive allocated while processing
** an SQLAR request.
*/
static void free_archive(Archive *p){
  if( p->vfs.zName ){
    sqlite3_vfs_unregister(&p->vfs);
    fossil_free((char*)p->vfs.zName);
    p->vfs.zName = 0;
  }
  sqlite3_finalize(p->pInsert);
  p->pInsert = 0;
  sqlite3_close(p->db);
  p->db = 0;
}

/*
** Write the ZIP archive into the given BLOB.
*/
static void zip_close(Archive *p){
  int i;
  if( p->eType==ARCHIVE_ZIP ){
    int iTocStart;
    int iTocEnd;
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
    *(p->pBlob) = body;
    blob_zero(&body);
  }else{
    if( p->db ) sqlite3_exec(p->db, "COMMIT", 0, 0, 0);
    free_archive(p);
    blob_reset(&p->tmp);
  }

  nEntry = 0;
  for(i=0; i<nDir; i++){
    fossil_free(azDir[i]);
  }
  fossil_free(azDir);
  nDir = 0;
  azDir = 0;
}

/* Functions found in shell.c */
extern int sqlite3_fileio_init(sqlite3*,char**,const sqlite3_api_routines*);
extern int sqlite3_zipfile_init(sqlite3*,char**,const sqlite3_api_routines*);

/*
** COMMAND: test-filezip
**
** Usage: %fossil test-filezip [OPTIONS] ZIPFILE [FILENAME...]
**
** This command uses Fossil infrastructure or read or create a ZIP
** archive named by the ZIPFILE argument.  With no options, a new
** ZIP archive is created and there must be at least one FILENAME
** argument. If the -l option is used, the contents of the named ZIP
** archive are listed on standard output.  With the -x argument, the
** contents of the ZIP archive are extracted.
**
** There are two purposes for this command:  (1) To server as a test
** platform for the Fossil ZIP archive generator, and (2) to provide
** rudimentary ZIP archive creation capabilities on platforms that do
** not have the "zip" command installed.
**
** Options:
**
**    -h|--dereference    Follow symlinks
**    -l|--list           List the contents of the ZIP archive
**    -x|--extract        Extract files from a ZIP archive
*/
void filezip_cmd(void){
  int eFType = SymFILE;
  int doList = 0;
  int doExtract = 0;
  char *zArchiveName;
  if( find_option("dereference","h",0)!=0 ){
    eFType = ExtFILE;
  }
  if( find_option("list","l",0)!=0 ){
    doList = 1;
  }
  if( find_option("extract","x",0)!=0 ){
    if( doList ){
      fossil_fatal("incompatible options: -l and -x");
    }
    doExtract = 1;
  }
  if( g.argc<3 ){
    usage("ARCHIVE FILES...");
  }
  zArchiveName = g.argv[2];
  sqlite3_open(":memory:", &g.db);
  if( doList ){
    /* Do a content listing of a ZIP archive */
    Stmt q;
    int nRow = 0;
    i64 szTotal = 0;
    if( file_size(zArchiveName, eFType)<0 ){
      fossil_fatal("No such ZIP archive: %s", zArchiveName);
    }
    if( g.argc>3 ){
      fossil_fatal("extra arguments after \"fossil test-filezip -l ARCHIVE\"");
    }
    sqlite3_zipfile_init(g.db, 0, 0);
    db_multi_exec("CREATE VIRTUAL TABLE z1 USING zipfile(%Q)", zArchiveName);
    db_prepare(&q, "SELECT sz, datetime(mtime,'unixepoch'), name FROM z1");
    while( db_step(&q)==SQLITE_ROW ){
      int sz = db_column_int(&q, 0);
      szTotal += sz;
      if( nRow==0 ){
        fossil_print("  Length      Date    Time    Name\n");
        fossil_print("---------  ---------- -----   ----\n");
      }
      nRow++;
      fossil_print("%9d  %.16s   %s\n", sz, db_column_text(&q,1),
                   db_column_text(&q,2));
    }
    if( nRow ){
      fossil_print("---------                     --------\n");
      fossil_print("%9lld  %16s   %d files\n", szTotal, "", nRow);
    }
    db_finalize(&q);
  }else if( doExtract ){
    /* Extract files from an existing ZIP archive */
    if( file_size(zArchiveName, eFType)<0 ){
      fossil_fatal("No such ZIP archive: %s", zArchiveName);
    }
    if( g.argc>3 ){
      fossil_fatal("extra arguments after \"fossil test-filezip -x ARCHIVE\"");
    }
    sqlite3_zipfile_init(g.db, 0, 0);
    sqlite3_fileio_init(g.db, 0, 0);
    db_multi_exec("CREATE VIRTUAL TABLE z1 USING zipfile(%Q)", zArchiveName);
    db_multi_exec("SELECT writefile(name,data) FROM z1");
  }else{
    /* Without the -x or -l options, construct a new ZIP archive */
    int i;
    Blob zip;
    Blob file;
    Archive sArchive;
    memset(&sArchive, 0, sizeof(Archive));
    sArchive.eType = ARCHIVE_ZIP;
    sArchive.pBlob = &zip;
    if( file_size(zArchiveName, eFType)>0 ){
      fossil_fatal("ZIP archive %s already exists", zArchiveName);
    }
    zip_open();
    for(i=3; i<g.argc; i++){
      double rDate;
      i64 iDate;
      blob_zero(&file);
      blob_read_from_file(&file, g.argv[i], eFType);
      iDate = file_mtime(g.argv[i], eFType);
      rDate = ((double)iDate)/86400.0 + 2440587.5;
      zip_set_timedate(rDate);
      zip_add_file(&sArchive, g.argv[i], &file, file_perm(0,eFType));
      blob_reset(&file);
    }
    zip_close(&sArchive);
    blob_write_to_file(&zip, g.argv[2]);
  }
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
** with source files. For example, pass a commit hash or "ProjectName".
**
*/
static void zip_of_checkin(
  int eType,          /* Type of archive (ZIP or SQLAR) */
  int rid,            /* The RID of the check-in to build the archive from */
  Blob *pZip,         /* Write the archive content into this blob */
  const char *zDir,   /* Top-level directory of the archive */
  Glob *pInclude,     /* Only include files that match this pattern */
  Glob *pExclude,     /* Exclude files that match this pattern */
  int listFlag        /* Print each file on stdout */
){
  Blob mfile, hash, file;
  Manifest *pManifest;
  ManifestFile *pFile;
  Blob filename;
  int nPrefix;

  Archive sArchive;
  memset(&sArchive, 0, sizeof(Archive));
  sArchive.eType = eType;
  sArchive.pBlob = pZip;
  blob_zero(&sArchive.tmp);
  if( pZip ) blob_zero(pZip);

  content_get(rid, &mfile);
  if( blob_size(&mfile)==0 ){
    return;
  }
  blob_set_dynamic(&hash, rid_to_uuid(rid));
  blob_zero(&filename);
  if( pZip ) zip_open();

  if( zDir && zDir[0] ){
    blob_appendf(&filename, "%s/", zDir);
  }
  nPrefix = blob_size(&filename);

  pManifest = manifest_get(rid, CFTYPE_MANIFEST, 0);
  if( pManifest ){
    int flg, eflg = 0;
    char *zName = 0;
    zip_set_timedate(pManifest->rDate);
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

      if( eflg & MFESTFLG_RAW ){
        blob_append(&filename, "manifest", -1);
        zName = blob_str(&filename);
        if( listFlag ) fossil_print("%s\n", zName);
        if( pZip ){
          zip_add_folders(&sArchive, zName);
          zip_add_file(&sArchive, zName, &mfile, 0);
        }
      }
      if( eflg & MFESTFLG_UUID ){
        blob_append(&hash, "\n", 1);
        blob_resize(&filename, nPrefix);
        blob_append(&filename, "manifest.uuid", -1);
        zName = blob_str(&filename);
        if( listFlag ) fossil_print("%s\n", zName);
        if( pZip ){
          zip_add_folders(&sArchive, zName);
          zip_add_file(&sArchive, zName, &hash, 0);
        }
      }
      if( eflg & MFESTFLG_TAGS ){
        blob_resize(&filename, nPrefix);
        blob_append(&filename, "manifest.tags", -1);
        zName = blob_str(&filename);
        if( listFlag ) fossil_print("%s\n", zName);
        if( pZip ){
          Blob tagslist;
          blob_zero(&tagslist);
          get_checkin_taglist(rid, &tagslist);
          zip_add_folders(&sArchive, zName);
          zip_add_file(&sArchive, zName, &tagslist, 0);
          blob_reset(&tagslist);
        }
      }
    }
    manifest_file_rewind(pManifest);
    if( pZip ) zip_add_file(&sArchive, "", 0, 0);
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
        if( pZip ){
          content_get(fid, &file);
          zip_add_folders(&sArchive, zName);
          zip_add_file(&sArchive, zName, &file, manifest_file_mperm(pFile));
          blob_reset(&file);
        }
      }
    }
  }
  blob_reset(&mfile);
  manifest_destroy(pManifest);
  blob_reset(&filename);
  blob_reset(&hash);
  if( pZip ){
    zip_close(&sArchive);
  }
}

/*
** Implementation of zip_cmd and sqlar_cmd.
*/
static void archive_cmd(int eType){
  int rid;
  Blob zip;
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
  listFlag = find_option("list","l",0)!=0;
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
  zOut = g.argv[3];
  if( fossil_strcmp(zOut,"")==0 || fossil_strcmp(zOut,"/dev/null")==0 ){
    zOut = 0;
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
  zip_of_checkin(eType, rid, zOut ? &zip : 0,
                 zName, pInclude, pExclude, listFlag);
  glob_free(pInclude);
  glob_free(pExclude);
  if( zOut ){
    blob_write_to_file(&zip, zOut);
    blob_reset(&zip);
  }
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
** If OUTPUTFILE is an empty string or "/dev/null" then no ZIP archive is
** actually generated.  This feature can be used in combination with
** the --list option to get a list of the filenames that would be in the
** ZIP archive had it actually been generated.
**
** Options:
**   -X|--exclude GLOBLIST   Comma-separated list of GLOBs of files to exclude
**   --include GLOBLIST      Comma-separated list of GLOBs of files to include
**   -l|--list               Show archive content on stdout
**   --name DIRECTORYNAME    The name of the top-level directory in the archive
**   -R REPOSITORY           Specify a Fossil repository
*/
void zip_cmd(void){
  archive_cmd(ARCHIVE_ZIP);
}

/*
** COMMAND: sqlar*
**
** Usage: %fossil sqlar VERSION OUTPUTFILE [OPTIONS]
**
** Generate an SQLAR archive for a check-in.  If the --name option is
** used, its argument becomes the name of the top-level directory in the
** resulting SQLAR archive.  If --name is omitted, the top-level directory
** name is derived from the project name, the check-in date and time, and
** the artifact ID of the check-in.
**
** The GLOBLIST argument to --exclude and --include can be a comma-separated
** list of glob patterns, where each glob pattern may optionally be enclosed
** in "..." or '...' so that it may contain commas.  If a file matches both
** --include and --exclude then it is excluded.
**
** If OUTPUTFILE is an empty string or "/dev/null" then no SQLAR archive is
** actually generated.  This feature can be used in combination with
** the --list option to get a list of the filenames that would be in the
** SQLAR archive had it actually been generated.
**
** Options:
**   -X|--exclude GLOBLIST   Comma-separated list of GLOBs of files to exclude
**   --include GLOBLIST      Comma-separated list of GLOBs of files to include
**   -l|--list               Show archive content on stdout
**   --name DIRECTORYNAME    The name of the top-level directory in the archive
**   -R REPOSITORY           Specify a Fossil repository
*/
void sqlar_cmd(void){
  archive_cmd(ARCHIVE_SQLAR);
}

/*
** WEBPAGE: sqlar
** WEBPAGE: zip
**
** URLs:
**
**     /zip/[VERSION/]NAME.zip
**     /sqlar/[VERSION/]NAME.sqlar
**
** Generate a ZIP Archive or an SQL Archive for the check-in specified by
** VERSION.  The archive is called NAME.zip or NAME.sqlar and has a top-level
** directory called NAME.
**
** The optional VERSION element defaults to "trunk" per the r= rules below.
** All of the following URLs are equivalent:
**
**      /zip/release/xyz.zip
**      /zip?r=release&name=xyz.zip
**      /zip/xyz.zip?r=release
**      /zip?name=release/xyz.zip
**
** Query parameters:
**
**   name=[CKIN/]NAME    The optional CKIN component of the name= parameter
**                       identifies the check-in from which the archive is
**                       constructed.  If CKIN is omitted and there is no
**                       r= query parameter, then use "trunk".  NAME is the
**                       name of the download file.  The top-level directory
**                       in the generated archive is called by NAME with the
**                       file extension removed.
**
**   r=TAG               TAG identifies the check-in that is turned into an
**                       SQL or ZIP archive.  The default value is "trunk".
**                       If r= is omitted and if the name= query parameter
**                       contains one "/" character then the of part the
**                       name= value before the / becomes the TAG and the
**                       part of the name= value  after the / is the download
**                       filename.  If no check-in is specified by either
**                       name= or r=, then "trunk" is used.
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
  int eType = ARCHIVE_ZIP;      /* Type of archive to generate */
  char *zType;                  /* Human-readable archive type */

  login_check_credentials();
  if( !g.perm.Zip ){ login_needed(g.anon.Zip); return; }
  if( robot_restrict("zip") ) return;
  if( fossil_strcmp(g.zPath, "sqlar")==0 ){
    eType = ARCHIVE_SQLAR;
    zType = "SQL";
    /* For some reason, SQL-archives are like catnip for robots.  So
    ** don't allow them to be downloaded by user "nobody" */
    if( g.zLogin==0 ){ login_needed(g.anon.Zip); return; }
  }else{
    eType = ARCHIVE_ZIP;
    zType = "ZIP";
  }
  fossil_nice_default();
  zName = fossil_strdup(PD("name",""));
  z = P("r");
  if( z==0 ) z = P("uuid");
  if( z==0 ) z = tar_uuid_from_name(&zName);
  if( z==0 ) z = "trunk";
  nName = strlen(zName);
  g.zOpenRevision = zRid = fossil_strdup(z);
  nRid = strlen(zRid);
  zInclude = P("in");
  if( zInclude ) pInclude = glob_create(zInclude);
  zExclude = P("ex");
  if( zExclude ) pExclude = glob_create(zExclude);
  if( zInclude==0 && zExclude==0 ){
    etag_check_for_invariant_name(z);
  }
  if( eType==ARCHIVE_ZIP
   && nName>4
   && fossil_strcmp(&zName[nName-4], ".zip")==0
  ){
    /* Special case:  Remove the ".zip" suffix.  */
    nName -= 4;
    zName[nName] = 0;
  }else if( eType==ARCHIVE_SQLAR
   && nName>6
   && fossil_strcmp(&zName[nName-6], ".sqlar")==0
  ){
    /* Special case:  Remove the ".sqlar" suffix.  */
    nName -= 6;
    zName[nName] = 0;
  }else{
    /* If the file suffix is not ".zip" or ".sqlar" then just remove the
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
  blob_appendf(&cacheKey, "/%s/%z", g.zPath, rid_to_uuid(rid));
  blob_appendf(&cacheKey, "/%q", zName);
  if( zInclude ) blob_appendf(&cacheKey, ",in=%Q", zInclude);
  if( zExclude ) blob_appendf(&cacheKey, ",ex=%Q", zExclude);
  zKey = blob_str(&cacheKey);
  etag_check(ETAG_HASH, zKey);

  style_set_current_feature("zip");
  if( P("debug")!=0 ){
    style_header("%s Archive Generator Debug Screen", zType);
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
    style_header("%s Archive Download", zType);
    @ <form action='%R/%s(g.zPath)/%h(zName).%s(g.zPath)'>
    cgi_query_parameters_to_hidden();
    @ <p>%s(zType) Archive named <b>%h(zName).%s(g.zPath)</b>
    @ holding the content of check-in <b>%h(zRid)</b>:
    @ <input type="submit" value="Download">
    @ </form>
    style_finish_page();
    return;
  }
  cgi_check_for_malice();
  blob_zero(&zip);
  if( cache_read(&zip, zKey)==0 ){
    zip_of_checkin(eType, rid, &zip, zName, pInclude, pExclude, 0);
    cache_write(&zip, zKey);
  }
  glob_free(pInclude);
  glob_free(pExclude);
  fossil_free(zName);
  fossil_free(zRid);
  g.zOpenRevision = 0;
  blob_reset(&cacheKey);
  cgi_set_content(&zip);
  if( eType==ARCHIVE_ZIP ){
    cgi_set_content_type("application/zip");
  }else{
    cgi_set_content_type("application/sqlar");
  }
}
