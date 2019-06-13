/*
** Copyright (c) 2006 D. Richard Hipp
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
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** File utilities.
*/
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "file.h"

/*
** On Windows, include the Platform SDK header file.
*/
#ifdef _WIN32
# include <direct.h>
# include <windows.h>
# include <sys/utime.h>
#else
# include <sys/time.h>
#endif

#if INTERFACE

/* Many APIs take a eFType argument which must be one of ExtFILE, RepoFILE,
** or SymFILE.
**
** The difference is in the handling of symbolic links.  RepoFILE should be
** used for files that are under management by a Fossil repository.  ExtFILE
** should be used for files that are not under management.  SymFILE is for
** a few special cases such as the "fossil test-tarball" command when we never
** want to follow symlinks.
**
** If RepoFILE is used and if the allow-symlinks setting is true and if
** the object is a symbolic link, then the object is treated like an ordinary
** file whose content is name of the object to which the symbolic link
** points.
**
** If ExtFILE is used or allow-symlinks is false, then operations on a
** symbolic link are the same as operations on the object to which the
** symbolic link points.
**
** SymFILE is like RepoFILE except that it always uses the target filename of
** a symbolic link as the content, instead of the content of the object
** that the symlink points to.  SymFILE acts as if allow-symlinks is always ON.
*/
#define ExtFILE    0  /* Always follow symlinks */
#define RepoFILE   1  /* Follow symlinks if and only if allow-symlinks is OFF */
#define SymFILE    2  /* Never follow symlinks */

#include <dirent.h>
#if defined(_WIN32)
# define DIR _WDIR
# define dirent _wdirent
# define opendir _wopendir
# define readdir _wreaddir
# define closedir _wclosedir
#endif /* _WIN32 */

#if defined(_WIN32) && (defined(__MSVCRT__) || defined(_MSC_VER))
/*
** File status information for windows systems.
*/
struct fossilStat {
    i64 st_size;
    i64 st_mtime;
    int st_mode;
};
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
# define fossil_isdirsep(a)    (((a) == '/') || ((a) == '\\'))
#else
# define fossil_isdirsep(a)    ((a) == '/')
#endif

#endif /* INTERFACE */

#if !defined(_WIN32) || !(defined(__MSVCRT__) || defined(_MSC_VER))
/*
** File status information for unix systems
*/
# define fossilStat stat
#endif

/*
** On Windows S_ISLNK always returns FALSE.
*/
#if !defined(S_ISLNK)
# define S_ISLNK(x) (0)
#endif

/*
** Local state information for the file status routines
*/
static struct {
  struct fossilStat fileStat;  /* File status from last fossil_stat() */
  int fileStatValid;           /* True if fileStat is valid */
} fx;

/*
** Fill *buf with information about zFilename.
**
** If zFilename refers to a symbolic link:
**
**  (A) If allow-symlinks is on and eFType is RepoFILE, then fill
**      *buf with information about the symbolic link itself.
**
**  (B) If allow-symlinks is off or eFType is ExtFILE, then fill
**      *buf with information about the object that the symbolic link
**      points to.
*/
static int fossil_stat(
  const char *zFilename,  /* name of file or directory to inspect. */
  struct fossilStat *buf, /* pointer to buffer where info should go. */
  int eFType              /* Look at symlink itself if RepoFILE and enabled. */
){
  int rc;
  void *zMbcs = fossil_utf8_to_path(zFilename, 0);
#if !defined(_WIN32)
  if( eFType>=RepoFILE && (eFType==SymFILE || db_allow_symlinks()) ){
    rc = lstat(zMbcs, buf);
  }else{
    rc = stat(zMbcs, buf);
  }
#else
  rc = win32_stat(zMbcs, buf, eFType);
#endif
  fossil_path_free(zMbcs);
  return rc;
}

/*
** Clears the fx.fileStat variable and its associated validity flag.
*/
static void resetStat(){
  fx.fileStatValid = 0;
  memset(&fx.fileStat, 0, sizeof(struct fossilStat));
}

/*
** Fill in the fx.fileStat variable for the file named zFilename.
** If zFilename==0, then use the previous value of fx.fileStat if
** there is a previous value.
**
** Return the number of errors.  No error messages are generated.
*/
static int getStat(const char *zFilename, int eFType){
  int rc = 0;
  if( zFilename==0 ){
    if( fx.fileStatValid==0 ) rc = 1;
  }else{
    if( fossil_stat(zFilename, &fx.fileStat, eFType)!=0 ){
      fx.fileStatValid = 0;
      rc = 1;
    }else{
      fx.fileStatValid = 1;
      rc = 0;
    }
  }
  return rc;
}

/*
** Return the size of a file in bytes.  Return -1 if the file does not
** exist.  If zFilename is NULL, return the size of the most recently
** stat-ed file.
*/
i64 file_size(const char *zFilename, int eFType){
  return getStat(zFilename, eFType) ? -1 : fx.fileStat.st_size;
}

/*
** Return the modification time for a file.  Return -1 if the file
** does not exist.  If zFilename is NULL return the size of the most
** recently stat-ed file.
*/
i64 file_mtime(const char *zFilename, int eFType){
  return getStat(zFilename, eFType) ? -1 : fx.fileStat.st_mtime;
}

/*
** Return the mode bits for a file.  Return -1 if the file does not
** exist.  If zFilename is NULL return the size of the most recently
** stat-ed file.
*/
int file_mode(const char *zFilename, int eFType){
  return getStat(zFilename, eFType) ? -1 : fx.fileStat.st_mode;
}

/*
** Return TRUE if either of the following are true:
**
**   (1) zFilename is an ordinary file
**
**   (2) allow_symlinks is on and zFilename is a symbolic link to
**       a file, directory, or other object
*/
int file_isfile_or_link(const char *zFilename){
  if( getStat(zFilename, RepoFILE) ){
    return 0;  /* stat() failed.  Return false. */
  }
  return S_ISREG(fx.fileStat.st_mode) || S_ISLNK(fx.fileStat.st_mode);
}

/*
** Return TRUE if the named file is an ordinary file.  Return false
** for directories, devices, fifos, symlinks, etc.
*/
int file_isfile(const char *zFilename, int eFType){
  return getStat(zFilename, eFType) ? 0 : S_ISREG(fx.fileStat.st_mode);
}

/*
** Create a symbolic link named zLinkFile that points to zTargetFile.
**
** If allow-symlinks is off, create an ordinary file named zLinkFile
** with the name of zTargetFile as its content.
**/
void symlink_create(const char *zTargetFile, const char *zLinkFile){
#if !defined(_WIN32)
  if( db_allow_symlinks() ){
    int i, nName;
    char *zName, zBuf[1000];

    nName = strlen(zLinkFile);
    if( nName>=sizeof(zBuf) ){
      zName = mprintf("%s", zLinkFile);
    }else{
      zName = zBuf;
      memcpy(zName, zLinkFile, nName+1);
    }
    nName = file_simplify_name(zName, nName, 0);
    for(i=1; i<nName; i++){
      if( zName[i]=='/' ){
        zName[i] = 0;
        if( file_mkdir(zName, ExtFILE, 1) ){
          fossil_fatal_recursive("unable to create directory %s", zName);
          return;
        }
        zName[i] = '/';
      }
    }
    if( symlink(zTargetFile, zName)!=0 ){
      fossil_fatal_recursive("unable to create symlink \"%s\"", zName);
    }
    if( zName!=zBuf ) free(zName);
  }else
#endif
  {
    Blob content;
    blob_set(&content, zTargetFile);
    blob_write_to_file(&content, zLinkFile);
    blob_reset(&content);
  }
}

/*
** Copy symbolic link from zFrom to zTo.
*/
void symlink_copy(const char *zFrom, const char *zTo){
  Blob content;
  blob_read_link(&content, zFrom);
  symlink_create(blob_str(&content), zTo);
  blob_reset(&content);
}

/*
** Return file permissions (normal, executable, or symlink):
**   - PERM_EXE on Unix if file is executable;
**   - PERM_LNK on Unix if file is symlink and allow-symlinks option is on;
**   - PERM_REG for all other cases (regular file, directory, fifo, etc).
**
** If eFType is ExtFile then symbolic links are followed and so this
** routine can only return PERM_EXE and PERM_REG.
**
** On windows, this routine returns only PERM_REG.
*/
int file_perm(const char *zFilename, int eFType){
#if !defined(_WIN32)
  if( !getStat(zFilename, RepoFILE) ){
     if( S_ISREG(fx.fileStat.st_mode) && ((S_IXUSR)&fx.fileStat.st_mode)!=0 )
      return PERM_EXE;
    else if( db_allow_symlinks() && S_ISLNK(fx.fileStat.st_mode) )
      return PERM_LNK;
  }
#endif
  return PERM_REG;
}

/*
** Return TRUE if the named file is an executable.  Return false
** for directories, devices, fifos, symlinks, etc.
*/
int file_isexe(const char *zFilename, int eFType){
  return file_perm(zFilename, eFType)==PERM_EXE;
}

/*
** Return TRUE if the named file is a symlink and symlinks are allowed.
** Return false for all other cases.
**
** This routines RepoFILE - that zFilename is always a file under management.
**
** On Windows, always return False.
*/
int file_islink(const char *zFilename){
  return file_perm(zFilename, RepoFILE)==PERM_LNK;
}

/*
** Return 1 if zFilename is a directory.  Return 0 if zFilename
** does not exist.  Return 2 if zFilename exists but is something
** other than a directory.
*/
int file_isdir(const char *zFilename, int eFType){
  int rc;
  char *zFN;

  zFN = mprintf("%s", zFilename);
  file_simplify_name(zFN, -1, 0);
  rc = getStat(zFN, eFType);
  if( rc ){
    rc = 0; /* It does not exist at all. */
  }else if( S_ISDIR(fx.fileStat.st_mode) ){
    rc = 1; /* It exists and is a real directory. */
  }else{
    rc = 2; /* It exists and is something else. */
  }
  free(zFN);
  return rc;
}


/*
** Wrapper around the access() system call.
*/
int file_access(const char *zFilename, int flags){
  int rc;
  void *zMbcs = fossil_utf8_to_path(zFilename, 0);
#ifdef _WIN32
  rc = win32_access(zMbcs, flags);
#else
  rc = access(zMbcs, flags);
#endif
  fossil_path_free(zMbcs);
  return rc;
}

/*
** Wrapper around the chdir() system call.
** If bChroot=1, do a chroot to this dir as well
** (UNIX only)
*/
int file_chdir(const char *zChDir, int bChroot){
  int rc;
  void *zPath = fossil_utf8_to_path(zChDir, 1);
#ifdef _WIN32
  rc = win32_chdir(zPath, bChroot);
#else
  rc = chdir(zPath);
  if( !rc && bChroot ){
    rc = chroot(zPath);
    if( !rc ) rc = chdir("/");
  }
#endif
  fossil_path_free(zPath);
  return rc;
}

/*
** Find an unused filename similar to zBase with zSuffix appended.
**
** Make the name relative to the working directory if relFlag is true.
**
** Space to hold the new filename is obtained form mprintf() and should
** be freed by the caller.
*/
char *file_newname(const char *zBase, const char *zSuffix, int relFlag){
  char *z = 0;
  int cnt = 0;
  z = mprintf("%s-%s", zBase, zSuffix);
  while( file_size(z, ExtFILE)>=0 ){
    fossil_free(z);
    z = mprintf("%s-%s-%d", zBase, zSuffix, cnt++);
  }
  if( relFlag ){
    Blob x;
    file_relative_name(z, &x, 0);
    fossil_free(z);
    z = blob_str(&x);
  }
  return z;
}

/*
** Return the tail of a file pathname.  The tail is the last component
** of the path.  For example, the tail of "/a/b/c.d" is "c.d".
*/
const char *file_tail(const char *z){
  const char *zTail = z;
  if( !zTail ) return 0;
  while( z[0] ){
    if( fossil_isdirsep(z[0]) ) zTail = &z[1];
    z++;
  }
  return zTail;
}

/*
** Return the directory of a file path name.  The directory is all components
** except the last one.  For example, the directory of "/a/b/c.d" is "/a/b".
** If there is no directory, NULL is returned; otherwise, the returned memory
** should be freed via fossil_free().
*/
char *file_dirname(const char *z){
  const char *zTail = file_tail(z);
  if( zTail && zTail!=z ){
    return mprintf("%.*s", (int)(zTail-z-1), z);
  }else{
    return 0;
  }
}

/* SQL Function:  file_dirname(NAME)
**
** Return the directory for NAME
*/
void file_dirname_sql_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zName = (const char*)sqlite3_value_text(argv[0]);
  char *zDir;
  if( zName==0 ) return;
  zDir = file_dirname(zName);
  if( zDir ){
    sqlite3_result_text(context,zDir,-1,fossil_free);
  }
}


/*
** Rename a file or directory.
** Returns zero upon success.
*/
int file_rename(
  const char *zFrom,
  const char *zTo,
  int isFromDir,
  int isToDir
){
  int rc;
#if defined(_WIN32)
  wchar_t *zMbcsFrom = fossil_utf8_to_path(zFrom, isFromDir);
  wchar_t *zMbcsTo = fossil_utf8_to_path(zTo, isToDir);
  rc = _wrename(zMbcsFrom, zMbcsTo);
#else
  char *zMbcsFrom = fossil_utf8_to_path(zFrom, isFromDir);
  char *zMbcsTo = fossil_utf8_to_path(zTo, isToDir);
  rc = rename(zMbcsFrom, zMbcsTo);
#endif
  fossil_path_free(zMbcsTo);
  fossil_path_free(zMbcsFrom);
  return rc;
}

/*
** Copy the content of a file from one place to another.
*/
void file_copy(const char *zFrom, const char *zTo){
  FILE *in, *out;
  int got;
  char zBuf[8192];
  in = fossil_fopen(zFrom, "rb");
  if( in==0 ) fossil_fatal("cannot open \"%s\" for reading", zFrom);
  file_mkfolder(zTo, ExtFILE, 0, 0);
  out = fossil_fopen(zTo, "wb");
  if( out==0 ) fossil_fatal("cannot open \"%s\" for writing", zTo);
  while( (got=fread(zBuf, 1, sizeof(zBuf), in))>0 ){
    fwrite(zBuf, 1, got, out);
  }
  fclose(in);
  fclose(out);
}

/*
** COMMAND: test-file-copy
**
** Usage: %fossil test-file-copy SOURCE DESTINATION
**
** Make a copy of the file at SOURCE into a new name DESTINATION.  Any
** directories in the path leading up to DESTINATION that do not already
** exist are created automatically.
*/
void test_file_copy(void){
  if( g.argc!=4 ){
    fossil_fatal("Usage: %s test-file-copy SOURCE DESTINATION", g.argv[0]);
  }
  file_copy(g.argv[2], g.argv[3]);
}

/*
** Set or clear the execute bit on a file.  Return true if a change
** occurred and false if this routine is a no-op.
**
** This routine assumes RepoFILE as the eFType.  In other words, if
** zFilename is a symbolic link, it is the object that zFilename points
** to that is modified.
*/
int file_setexe(const char *zFilename, int onoff){
  int rc = 0;
#if !defined(_WIN32)
  struct stat buf;
  if( fossil_stat(zFilename, &buf, RepoFILE)!=0 || S_ISLNK(buf.st_mode) ){
    return 0;
  }
  if( onoff ){
    int targetMode = (buf.st_mode & 0444)>>2;
    if( (buf.st_mode & 0100)==0 ){
      chmod(zFilename, buf.st_mode | targetMode);
      rc = 1;
    }
  }else{
    if( (buf.st_mode & 0100)!=0 ){
      chmod(zFilename, buf.st_mode & ~0111);
      rc = 1;
    }
  }
#endif /* _WIN32 */
  return rc;
}

/*
** Set the mtime for a file.
*/
void file_set_mtime(const char *zFilename, i64 newMTime){
#if !defined(_WIN32)
  char *zMbcs;
  struct timeval tv[2];
  memset(tv, 0, sizeof(tv[0])*2);
  tv[0].tv_sec = newMTime;
  tv[1].tv_sec = newMTime;
  zMbcs = fossil_utf8_to_path(zFilename, 0);
  utimes(zMbcs, tv);
#else
  struct _utimbuf tb;
  wchar_t *zMbcs = fossil_utf8_to_path(zFilename, 0);
  tb.actime = newMTime;
  tb.modtime = newMTime;
  _wutime(zMbcs, &tb);
#endif
  fossil_path_free(zMbcs);
}

/*
** COMMAND: test-set-mtime
**
** Usage: %fossil test-set-mtime FILENAME DATE/TIME
**
** Sets the mtime of the named file to the date/time shown.
*/
void test_set_mtime(void){
  const char *zFile;
  char *zDate;
  i64 iMTime;
  if( g.argc!=4 ){
    usage("FILENAME DATE/TIME");
  }
  db_open_or_attach(":memory:", "mem");
  iMTime = db_int64(0, "SELECT strftime('%%s',%Q)", g.argv[3]);
  zFile = g.argv[2];
  file_set_mtime(zFile, iMTime);
  iMTime = file_mtime(zFile, RepoFILE);
  zDate = db_text(0, "SELECT datetime(%lld, 'unixepoch')", iMTime);
  fossil_print("Set mtime of \"%s\" to %s (%lld)\n", zFile, zDate, iMTime);
}

/*
** Delete a file.
**
** If zFilename is a symbolic link, then it is the link itself that is
** removed, not the object that zFilename points to.
**
** Returns zero upon success.
*/
int file_delete(const char *zFilename){
  int rc;
#ifdef _WIN32
  wchar_t *z = fossil_utf8_to_path(zFilename, 0);
  rc = _wunlink(z);
#else
  char *z = fossil_utf8_to_path(zFilename, 0);
  rc = unlink(zFilename);
#endif
  fossil_path_free(z);
  return rc;
}

/* SQL Function:  file_delete(NAME)
**
** Remove file NAME.  Return zero on success and non-zero if anything goes
** wrong.
*/
void file_delete_sql_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zName = (const char*)sqlite3_value_text(argv[0]);
  int rc;
  if( zName==0 ){
    rc = 1;
  }else{
    rc = file_delete(zName);
  }
  sqlite3_result_int(context, rc);
}

/*
** Create a directory called zName, if it does not already exist.
** If forceFlag is 1, delete any prior non-directory object
** with the same name.
**
** Return the number of errors.
*/
int file_mkdir(const char *zName, int eFType, int forceFlag){
  int rc = file_isdir(zName, eFType);
  if( rc==2 ){
    if( !forceFlag ) return 1;
    file_delete(zName);
  }
  if( rc!=1 ){
#if defined(_WIN32)
    wchar_t *zMbcs = fossil_utf8_to_path(zName, 1);
    rc = _wmkdir(zMbcs);
#else
    char *zMbcs = fossil_utf8_to_path(zName, 1);
    rc = mkdir(zName, 0755);
#endif
    fossil_path_free(zMbcs);
    return rc;
  }
  return 0;
}

/*
** Create the tree of directories in which zFilename belongs, if that sequence
** of directories does not already exist.
**
** On success, return zero.  On error, return errorReturn if positive, otherwise
** print an error message and abort.
*/
int file_mkfolder(
  const char *zFilename,   /* Pathname showing directories to be created */
  int eFType,              /* Follow symlinks if ExtFILE */
  int forceFlag,           /* Delete non-directory objects in the way */
  int errorReturn          /* What to do when an error is seen */
){
  int nName, rc = 0;
  char *zName;

  nName = strlen(zFilename);
  zName = mprintf("%s", zFilename);
  nName = file_simplify_name(zName, nName, 0);
  while( nName>0 && zName[nName-1]!='/' ){ nName--; }
  if( nName ){
    zName[nName-1] = 0;
    if( file_isdir(zName, eFType)!=1 ){
      rc = file_mkfolder(zName, eFType, forceFlag, errorReturn);
      if( rc==0 ){
        if( file_mkdir(zName, eFType, forceFlag)
         && file_isdir(zName, eFType)!=1
        ){
          if( errorReturn <= 0 ){
            fossil_fatal_recursive("unable to create directory %s", zName);
          }
          rc = errorReturn;
        }
      }
    }
  }
  free(zName);
  return rc;
}

/*
** Removes the directory named in the argument, if it exists.  The directory
** must be empty and cannot be the current directory or the root directory.
**
** Returns zero upon success.
*/
int file_rmdir(const char *zName){
  int rc = file_isdir(zName, RepoFILE);
  if( rc==2 ) return 1; /* cannot remove normal file */
  if( rc==1 ){
#if defined(_WIN32)
    wchar_t *zMbcs = fossil_utf8_to_path(zName, 1);
    rc = _wrmdir(zMbcs);
#else
    char *zMbcs = fossil_utf8_to_path(zName, 1);
    rc = rmdir(zName);
#endif
    fossil_path_free(zMbcs);
    return rc;
  }
  return 0;
}

/* SQL Function: rmdir(NAME)
**
** Try to remove the directory NAME.  Return zero on success and non-zero
** for failure.
*/
void file_rmdir_sql_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zName = (const char*)sqlite3_value_text(argv[0]);
  int rc;
  if( zName==0 ){
    rc = 1;
  }else{
    rc = file_rmdir(zName);
  }
  sqlite3_result_int(context, rc);
}

/*
** Return true if the filename given is a valid filename for
** a file in a repository.  Valid filenames follow all of the
** following rules:
**
**     *  Does not begin with "/"
**     *  Does not contain any path element named "." or ".."
**     *  Does not contain any of these characters in the path: "\"
**     *  Does not end with "/".
**     *  Does not contain two or more "/" characters in a row.
**     *  Contains at least one character
**
** Invalid UTF8 characters result in a false return if bStrictUtf8 is
** true.  If bStrictUtf8 is false, invalid UTF8 characters are silently
** ignored. See http://en.wikipedia.org/wiki/UTF-8#Invalid_byte_sequences
** and http://en.wikipedia.org/wiki/Unicode (for the noncharacters)
**
** The bStrictUtf8 flag is true for new inputs, but is false when parsing
** legacy manifests, for backwards compatibility.
*/
int file_is_simple_pathname(const char *z, int bStrictUtf8){
  int i;
  unsigned char c = (unsigned char) z[0];
  char maskNonAscii = bStrictUtf8 ? 0x80 : 0x00;
  if( c=='/' || c==0 ) return 0;
  if( c=='.' ){
    if( z[1]=='/' || z[1]==0 ) return 0;
    if( z[1]=='.' && (z[2]=='/' || z[2]==0) ) return 0;
  }
  for(i=0; (c=(unsigned char)z[i])!=0; i++){
    if( c & maskNonAscii ){
      if( (z[++i]&0xc0)!=0x80 ){
        /* Invalid first continuation byte */
        return 0;
      }
      if( c<0xc2 ){
        /* Invalid 1-byte UTF-8 sequence, or 2-byte overlong form. */
        return 0;
      }else if( (c&0xe0)==0xe0 ){
        /* 3-byte or more */
        int unicode;
        if( c&0x10 ){
          /* Unicode characters > U+FFFF are not supported.
           * Windows XP and earlier cannot handle them.
           */
          return 0;
        }
        /* This is a 3-byte UTF-8 character */
        unicode = ((c&0x0f)<<12) + ((z[i]&0x3f)<<6) + (z[i+1]&0x3f);
        if( unicode <= 0x07ff ){
          /* overlong form */
          return 0;
        }else if( unicode>=0xe000 ){
          /* U+E000..U+FFFF */
          if( (unicode<=0xf8ff) || (unicode>=0xfffe) ){
            /* U+E000..U+F8FF are for private use.
             * U+FFFE..U+FFFF are noncharacters. */
            return 0;
          } else if( (unicode>=0xfdd0) && (unicode<=0xfdef) ){
            /* U+FDD0..U+FDEF are noncharacters. */
            return 0;
          }
        }else if( (unicode>=0xd800) && (unicode<=0xdfff) ){
          /* U+D800..U+DFFF are for surrogate pairs. */
          return 0;
        }
        if( (z[++i]&0xc0)!=0x80 ){
          /* Invalid second continuation byte */
          return 0;
        }
      }
    }else if( bStrictUtf8 && (c=='\\') ){
      return 0;
    }
    if( c=='/' ){
      if( z[i+1]=='/' ) return 0;
      if( z[i+1]=='.' ){
        if( z[i+2]=='/' || z[i+2]==0 ) return 0;
        if( z[i+2]=='.' && (z[i+3]=='/' || z[i+3]==0) ) return 0;
      }
    }
  }
  if( z[i-1]=='/' ) return 0;
  return 1;
}

/*
** If the last component of the pathname in z[0]..z[j-1] is something
** other than ".." then back it out and return true.  If the last
** component is empty or if it is ".." then return false.
*/
static int backup_dir(const char *z, int *pJ){
  int j = *pJ;
  int i;
  if( j<=0 ) return 0;
  for(i=j-1; i>0 && z[i-1]!='/'; i--){}
  if( z[i]=='.' && i==j-2 && z[i+1]=='.' ) return 0;
  *pJ = i-1;
  return 1;
}

/*
** Simplify a filename by
**
**  * Remove extended path prefix on windows and cygwin
**  * Convert all \ into / on windows and cygwin
**  * removing any trailing and duplicate /
**  * removing /./
**  * removing /A/../
**
** Changes are made in-place.  Return the new name length.
** If the slash parameter is non-zero, the trailing slash, if any,
** is retained.
*/
int file_simplify_name(char *z, int n, int slash){
  int i = 1, j;
  assert( z!=0 );
  if( n<0 ) n = strlen(z);

  /* On windows and cygwin convert all \ characters to /
   * and remove extended path prefix if present */
#if defined(_WIN32) || defined(__CYGWIN__)
  for(j=0; j<n; j++){
    if( z[j]=='\\' ) z[j] = '/';
  }
  if( n>3 && !memcmp(z, "//?/", 4) ){
    if( fossil_strnicmp(z+4,"UNC", 3) ){
      i += 4;
      z[0] = z[4];
    }else{
      i += 6;
      z[0] = '/';
    }
  }
#endif

  /* Removing trailing "/" characters */
  if( !slash ){
    while( n>1 && z[n-1]=='/' ){ n--; }
  }

  /* Remove duplicate '/' characters.  Except, two // at the beginning
  ** of a pathname is allowed since this is important on windows. */
  for(j=1; i<n; i++){
    z[j++] = z[i];
    while( z[i]=='/' && i<n-1 && z[i+1]=='/' ) i++;
  }
  n = j;

  /* Skip over zero or more initial "./" sequences */
  for(i=0; i<n-1 && z[i]=='.' && z[i+1]=='/'; i+=2){}

  /* Begin copying from z[i] back to z[j]... */
  for(j=0; i<n; i++){
    if( z[i]=='/' ){
      /* Skip over internal "/." directory components */
      if( z[i+1]=='.' && (i+2==n || z[i+2]=='/') ){
        i += 1;
        continue;
      }

      /* If this is a "/.." directory component then back out the
      ** previous term of the directory if it is something other than ".."
      ** or "."
      */
      if( z[i+1]=='.' && i+2<n && z[i+2]=='.' && (i+3==n || z[i+3]=='/')
       && backup_dir(z, &j)
      ){
        i += 2;
        continue;
      }
    }
    if( j>=0 ) z[j] = z[i];
    j++;
  }
  if( j==0 ) z[j++] = '/';
  z[j] = 0;
  return j;
}

/*
** COMMAND: test-simplify-name
**
** Usage: %fossil test-simplify-name FILENAME...
**
** Print the simplified versions of each FILENAME.
*/
void cmd_test_simplify_name(void){
  int i;
  char *z;
  for(i=2; i<g.argc; i++){
    z = mprintf("%s", g.argv[i]);
    fossil_print("[%s] -> ", z);
    file_simplify_name(z, -1, 0);
    fossil_print("[%s]\n", z);
    fossil_free(z);
  }
}

/*
** Get the current working directory.
**
** On windows, the name is converted from unicode to UTF8 and all '\\'
** characters are converted to '/'.  No conversions are needed on
** unix.
**
** Store the value of the CWD in zBuf which is nBuf bytes in size.
** or if zBuf==0, allocate space to hold the result using fossil_malloc().
*/
char *file_getcwd(char *zBuf, int nBuf){
  char zTemp[2000];
  if( zBuf==0 ){
    zBuf = zTemp;
    nBuf = sizeof(zTemp);
  }
#ifdef _WIN32
  win32_getcwd(zBuf, nBuf);
#else
  if( getcwd(zBuf, nBuf-1)==0 ){
    if( errno==ERANGE ){
      fossil_panic("pwd too big: max %d", nBuf-1);
    }else{
      fossil_panic("cannot find current working directory; %s",
                   strerror(errno));
    }
  }
#endif
  return zBuf==zTemp ? fossil_strdup(zBuf) : zBuf;
}

/*
** Return true if zPath is an absolute pathname.  Return false
** if it is relative.
*/
int file_is_absolute_path(const char *zPath){
  if( fossil_isdirsep(zPath[0])
#if defined(_WIN32) || defined(__CYGWIN__)
      || (fossil_isalpha(zPath[0]) && zPath[1]==':'
           && (fossil_isdirsep(zPath[2]) || zPath[2]=='\0'))
#endif
  ){
    return 1;
  }else{
    return 0;
  }
}

/*
** Compute a canonical pathname for a file or directory.
** Make the name absolute if it is relative.
** Remove redundant / characters
** Remove all /./ path elements.
** Convert /A/../ to just /
** If the slash parameter is non-zero, the trailing slash, if any,
** is retained.
*/
void file_canonical_name(const char *zOrigName, Blob *pOut, int slash){
  blob_zero(pOut);
  if( file_is_absolute_path(zOrigName) ){
    blob_appendf(pOut, "%/", zOrigName);
  }else{
    char zPwd[2000];
    file_getcwd(zPwd, sizeof(zPwd)-strlen(zOrigName));
    if( zPwd[0]=='/' && strlen(zPwd)==1 ){
      /* when on '/', don't add an extra '/' */
      if( zOrigName[0]=='.' && strlen(zOrigName)==1 ){
        /* '.' when on '/' mean '/' */
        blob_appendf(pOut, "%/", zPwd);
      }else{
        blob_appendf(pOut, "%/%/", zPwd, zOrigName);
      }
    }else{
      blob_appendf(pOut, "%//%/", zPwd, zOrigName);
    }
  }
#if defined(_WIN32) || defined(__CYGWIN__)
  {
    char *zOut;
    /*
    ** On Windows/cygwin, normalize the drive letter to upper case.
    */
    zOut = blob_str(pOut);
    if( fossil_islower(zOut[0]) && zOut[1]==':' && zOut[2]=='/' ){
      zOut[0] = fossil_toupper(zOut[0]);
    }
  }
#endif
  blob_resize(pOut, file_simplify_name(blob_buffer(pOut),
                                       blob_size(pOut), slash));
}

/*
** Emits the effective or raw stat() information for the specified
** file or directory, optionally preserving the trailing slash and
** resetting the cached stat() information.
*/
static void emitFileStat(
  const char *zPath,
  int slash,
  int reset
){
  char zBuf[200];
  char *z;
  Blob x;
  int rc;
  sqlite3_int64 iMtime;
  struct fossilStat testFileStat;
  memset(zBuf, 0, sizeof(zBuf));
  blob_zero(&x);
  file_canonical_name(zPath, &x, slash);
  fossil_print("[%s] -> [%s]\n", zPath, blob_buffer(&x));
  blob_reset(&x);
  memset(&testFileStat, 0, sizeof(struct fossilStat));
  rc = fossil_stat(zPath, &testFileStat, 0);
  fossil_print("  stat_rc                = %d\n", rc);
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", testFileStat.st_size);
  fossil_print("  stat_size              = %s\n", zBuf);
  z = db_text(0, "SELECT datetime(%lld, 'unixepoch')", testFileStat.st_mtime);
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld (%s)", testFileStat.st_mtime, z);
  fossil_free(z);
  fossil_print("  stat_mtime             = %s\n", zBuf);
  fossil_print("  stat_mode              = 0%o\n", testFileStat.st_mode);
  memset(&testFileStat, 0, sizeof(struct fossilStat));
  rc = fossil_stat(zPath, &testFileStat, 1);
  fossil_print("  l_stat_rc              = %d\n", rc);
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", testFileStat.st_size);
  fossil_print("  l_stat_size            = %s\n", zBuf);
  z = db_text(0, "SELECT datetime(%lld, 'unixepoch')", testFileStat.st_mtime);
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld (%s)", testFileStat.st_mtime, z);
  fossil_free(z);
  fossil_print("  l_stat_mtime           = %s\n", zBuf);
  fossil_print("  l_stat_mode            = 0%o\n", testFileStat.st_mode);
  if( reset ) resetStat();
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_size(zPath,ExtFILE));
  fossil_print("  file_size(ExtFILE)     = %s\n", zBuf);
  iMtime = file_mtime(zPath, ExtFILE);
  z = db_text(0, "SELECT datetime(%lld, 'unixepoch')", iMtime);
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld (%s)", iMtime, z);
  fossil_free(z);
  fossil_print("  file_mtime(ExtFILE)    = %s\n", zBuf);
  fossil_print("  file_mode(ExtFILE)     = 0%o\n", file_mode(zPath,ExtFILE));
  fossil_print("  file_isfile(ExtFILE)   = %d\n", file_isfile(zPath,ExtFILE));
  fossil_print("  file_isdir(ExtFILE)    = %d\n", file_isdir(zPath,ExtFILE));
  if( reset ) resetStat();
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_size(zPath,RepoFILE));
  fossil_print("  file_size(RepoFILE)    = %s\n", zBuf);
  iMtime = file_mtime(zPath,RepoFILE);
  z = db_text(0, "SELECT datetime(%lld, 'unixepoch')", iMtime);
  sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld (%s)", iMtime, z);
  fossil_free(z);
  fossil_print("  file_mtime(RepoFILE)   = %s\n", zBuf);
  fossil_print("  file_mode(RepoFILE)    = 0%o\n", file_mode(zPath,RepoFILE));
  fossil_print("  file_isfile(RepoFILE)  = %d\n", file_isfile(zPath,RepoFILE));
  fossil_print("  file_isfile_or_link    = %d\n", file_isfile_or_link(zPath));
  fossil_print("  file_islink            = %d\n", file_islink(zPath));
  fossil_print("  file_isexe(RepoFILE)   = %d\n", file_isexe(zPath,RepoFILE));
  fossil_print("  file_isdir(RepoFILE)   = %d\n", file_isdir(zPath,RepoFILE));
  if( reset ) resetStat();
}

/*
** COMMAND: test-file-environment
**
** Usage: %fossil test-file-environment FILENAME...
**
** Display the effective file handling subsystem "settings" and then
** display file system information about the files specified, if any.
**
** Options:
**
**     --allow-symlinks BOOLEAN     Temporarily turn allow-symlinks on/off
**     --open-config                Open the configuration database first.
**     --slash                      Trailing slashes, if any, are retained.
**     --reset                      Reset cached stat() info for each file.
*/
void cmd_test_file_environment(void){
  int i;
  int slashFlag = find_option("slash",0,0)!=0;
  int resetFlag = find_option("reset",0,0)!=0;
  const char *zAllow = find_option("allow-symlinks",0,1);
  if( find_option("open-config", 0, 0)!=0 ){
    Th_OpenConfig(1);
  }
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  fossil_print("filenames_are_case_sensitive() = %d\n",
               filenames_are_case_sensitive());
  fossil_print("db_allow_symlinks_by_default() = %d\n",
               db_allow_symlinks_by_default());
  if( zAllow ){
    g.allowSymlinks = !is_false(zAllow);
  }
  fossil_print("db_allow_symlinks() = %d\n", db_allow_symlinks());
  for(i=2; i<g.argc; i++){
    emitFileStat(g.argv[i], slashFlag, resetFlag);
  }
}

/*
** COMMAND: test-canonical-name
**
** Usage: %fossil test-canonical-name FILENAME...
**
** Test the operation of the canonical name generator.
** Also test Fossil's ability to measure attributes of a file.
*/
void cmd_test_canonical_name(void){
  int i;
  Blob x;
  int slashFlag = find_option("slash",0,0)!=0;
  blob_zero(&x);
  for(i=2; i<g.argc; i++){
    char zBuf[100];
    const char *zName = g.argv[i];
    file_canonical_name(zName, &x, slashFlag);
    fossil_print("[%s] -> [%s]\n", zName, blob_buffer(&x));
    blob_reset(&x);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_size(zName,RepoFILE));
    fossil_print("  file_size           = %s\n", zBuf);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_mtime(zName,RepoFILE));
    fossil_print("  file_mtime          = %s\n", zBuf);
    fossil_print("  file_isfile         = %d\n", file_isfile(zName,RepoFILE));
    fossil_print("  file_isfile_or_link = %d\n", file_isfile_or_link(zName));
    fossil_print("  file_islink         = %d\n", file_islink(zName));
    fossil_print("  file_isexe          = %d\n", file_isexe(zName,RepoFILE));
    fossil_print("  file_isdir          = %d\n", file_isdir(zName,RepoFILE));
  }
}

/*
** Return TRUE if the given filename is canonical.
**
** Canonical names are full pathnames using "/" not "\" and which
** contain no "/./" or "/../" terms.
*/
int file_is_canonical(const char *z){
  int i;
  if( z[0]!='/'
#if defined(_WIN32) || defined(__CYGWIN__)
    && (!fossil_isupper(z[0]) || z[1]!=':' || z[2]!='/')
#endif
  ) return 0;

  for(i=0; z[i]; i++){
    if( z[i]=='\\' ) return 0;
    if( z[i]=='/' ){
      if( z[i+1]=='.' ){
        if( z[i+2]=='/' || z[i+2]==0 ) return 0;
        if( z[i+2]=='.' && (z[i+3]=='/' || z[i+3]==0) ) return 0;
      }
    }
  }
  return 1;
}

/*
** Return a pointer to the first character in a pathname past the
** drive letter.  This routine is a no-op on unix.
*/
char *file_without_drive_letter(char *zIn){
#ifdef _WIN32
  if( fossil_isalpha(zIn[0]) && zIn[1]==':' ) zIn += 2;
#endif
  return zIn;
}

/*
** Compute a pathname for a file or directory that is relative
** to the current directory.  If the slash parameter is non-zero,
** the trailing slash, if any, is retained.
*/
void file_relative_name(const char *zOrigName, Blob *pOut, int slash){
  char *zPath;
  blob_set(pOut, zOrigName);
  blob_resize(pOut, file_simplify_name(blob_buffer(pOut),
                                       blob_size(pOut), slash));
  zPath = file_without_drive_letter(blob_buffer(pOut));
  if( zPath[0]=='/' ){
    int i, j;
    Blob tmp;
    char *zPwd;
    char zBuf[2000];
    zPwd = zBuf;
    file_getcwd(zBuf, sizeof(zBuf)-20);
    zPwd = file_without_drive_letter(zBuf);
    i = 1;
#if defined(_WIN32) || defined(__CYGWIN__)
    while( zPath[i] && fossil_tolower(zPwd[i])==fossil_tolower(zPath[i]) ) i++;
#else
    while( zPath[i] && zPwd[i]==zPath[i] ) i++;
#endif
    if( zPath[i]==0 ){
      memcpy(&tmp, pOut, sizeof(tmp));
      if( zPwd[i]==0 ){
        blob_set(pOut, ".");
      }else{
        blob_set(pOut, "..");
        for(j=i+1; zPwd[j]; j++){
          if( zPwd[j]=='/' ){
            blob_append(pOut, "/..", 3);
          }
        }
        while( i>0 && (zPwd[i]!='/')) --i;
        blob_append(pOut, zPath+i, j-i);
      }
      if( slash && i>0 && zPath[strlen(zPath)-1]=='/'){
        blob_append(pOut, "/", 1);
      }
      blob_reset(&tmp);
      return;
    }
    if( zPwd[i]==0 && zPath[i]=='/' ){
      memcpy(&tmp, pOut, sizeof(tmp));
      blob_set(pOut, "./");
      blob_append(pOut, &zPath[i+1], -1);
      blob_reset(&tmp);
      return;
    }
    while( zPath[i-1]!='/' ){ i--; }
    if( zPwd[0]=='/' && strlen(zPwd)==1 ){
      /* If on '/', don't go to higher level */
      blob_zero(&tmp);
    }else{
      blob_set(&tmp, "../");
    }
    for(j=i; zPwd[j]; j++){
      if( zPwd[j]=='/' ){
        blob_append(&tmp, "../", 3);
      }
    }
    blob_append(&tmp, &zPath[i], -1);
    blob_reset(pOut);
    memcpy(pOut, &tmp, sizeof(tmp));
  }
}

/*
** COMMAND: test-relative-name
**
** Test the operation of the relative name generator.
*/
void cmd_test_relative_name(void){
  int i;
  Blob x;
  int slashFlag = find_option("slash",0,0)!=0;
  blob_zero(&x);
  for(i=2; i<g.argc; i++){
    file_relative_name(g.argv[i], &x, slashFlag);
    fossil_print("%s\n", blob_buffer(&x));
    blob_reset(&x);
  }
}

/*
** Compute a full path name for a file in the local tree.  If
** the absolute flag is non-zero, the computed path will be
** absolute, starting with the root path of the local tree;
** otherwise, it will be relative to the root of the local
** tree.  In both cases, the root of the local tree is defined
** by the g.zLocalRoot variable.  Return TRUE on success.  On
** failure, print and error message and quit if the errFatal
** flag is true.  If errFatal is false, then simply return 0.
*/
int file_tree_name(
  const char *zOrigName,
  Blob *pOut,
  int absolute,
  int errFatal
){
  Blob localRoot;
  int nLocalRoot;
  char *zLocalRoot;
  Blob full;
  int nFull;
  char *zFull;
  int (*xCmp)(const char*,const char*,int);

  blob_zero(pOut);
  if( !g.localOpen ){
    if( absolute && !file_is_absolute_path(zOrigName) ){
      if( errFatal ){
        fossil_fatal("relative to absolute needs open checkout tree: %s",
                     zOrigName);
      }
      return 0;
    }else{
      /*
      ** The original path may be relative or absolute; however, without
      ** an open checkout tree, the only things we can do at this point
      ** is return it verbatim or generate a fatal error.  The caller is
      ** probably expecting a tree-relative path name will be returned;
      ** however, most places where this function is called already check
      ** if the local checkout tree is open, either directly or indirectly,
      ** which would make this situation impossible.  Alternatively, they
      ** could check the returned path using the file_is_absolute_path()
      ** function.
      */
      blob_appendf(pOut, "%s", zOrigName);
      return 1;
    }
  }
  file_canonical_name(g.zLocalRoot, &localRoot, 1);
  nLocalRoot = blob_size(&localRoot);
  zLocalRoot = blob_buffer(&localRoot);
  assert( nLocalRoot>0 && zLocalRoot[nLocalRoot-1]=='/' );
  file_canonical_name(zOrigName, &full, 0);
  nFull = blob_size(&full);
  zFull = blob_buffer(&full);
  if( filenames_are_case_sensitive() ){
    xCmp = fossil_strncmp;
  }else{
    xCmp = fossil_strnicmp;
  }

  /* Special case.  zOrigName refers to g.zLocalRoot directory. */
  if( (nFull==nLocalRoot-1 && xCmp(zLocalRoot, zFull, nFull)==0)
      || (nFull==1 && zFull[0]=='/' && nLocalRoot==1 && zLocalRoot[0]=='/') ){
    if( absolute ){
      blob_append(pOut, zLocalRoot, nLocalRoot);
    }else{
      blob_append(pOut, ".", 1);
    }
    blob_reset(&localRoot);
    blob_reset(&full);
    return 1;
  }

  if( nFull<=nLocalRoot || xCmp(zLocalRoot, zFull, nLocalRoot) ){
    blob_reset(&localRoot);
    blob_reset(&full);
    if( errFatal ){
      fossil_fatal("file outside of checkout tree: %s", zOrigName);
    }
    return 0;
  }
  if( absolute ){
    if( !file_is_absolute_path(zOrigName) ){
      blob_append(pOut, zLocalRoot, nLocalRoot);
    }
    blob_append(pOut, zOrigName, -1);
    blob_resize(pOut, file_simplify_name(blob_buffer(pOut),
                                         blob_size(pOut), 0));
  }else{
    blob_append(pOut, &zFull[nLocalRoot], nFull-nLocalRoot);
  }
  blob_reset(&localRoot);
  blob_reset(&full);
  return 1;
}

/*
** COMMAND: test-tree-name
**
** Test the operation of the tree name generator.
**
** Options:
**   --absolute           Return an absolute path instead of a relative one.
**   --case-sensitive B   Enable or disable case-sensitive filenames.  B is
**                        a boolean: "yes", "no", "true", "false", etc.
*/
void cmd_test_tree_name(void){
  int i;
  Blob x;
  int absoluteFlag = find_option("absolute",0,0)!=0;
  db_find_and_open_repository(0,0);
  blob_zero(&x);
  for(i=2; i<g.argc; i++){
    if( file_tree_name(g.argv[i], &x, absoluteFlag, 1) ){
      fossil_print("%s\n", blob_buffer(&x));
      blob_reset(&x);
    }
  }
}

/*
** Parse a URI into scheme, host, port, and path.
*/
void file_parse_uri(
  const char *zUri,
  Blob *pScheme,
  Blob *pHost,
  int *pPort,
  Blob *pPath
){
  int i, j;

  for(i=0; zUri[i] && zUri[i]>='a' && zUri[i]<='z'; i++){}
  if( zUri[i]!=':' ){
    blob_zero(pScheme);
    blob_zero(pHost);
    blob_set(pPath, zUri);
    return;
  }
  blob_init(pScheme, zUri, i);
  i++;
  if( zUri[i]=='/' && zUri[i+1]=='/' ){
    i += 2;
    j = i;
    while( zUri[i] && zUri[i]!='/' && zUri[i]!=':' ){ i++; }
    blob_init(pHost, &zUri[j], i-j);
    if( zUri[i]==':' ){
      i++;
      *pPort = atoi(&zUri[i]);
      while( zUri[i] && zUri[i]!='/' ){ i++; }
    }
  }else{
    blob_zero(pHost);
  }
  if( zUri[i]=='/' ){
    blob_set(pPath, &zUri[i]);
  }else{
    blob_set(pPath, "/");
  }
}

/*
** Construct a random temporary filename into pBuf where the name of
** the temporary file is derived from zBasis.  The suffix on the temp
** file is the same as the suffix on zBasis, and the temp file has
** the root of zBasis in its name.
**
** If zTag is not NULL, then try to create the temp-file using zTag
** as a differentiator.  If that fails, or if zTag is NULL, then use
** a bunch of random characters as the tag.
*/
void file_tempname(Blob *pBuf, const char *zBasis, const char *zTag){
#if defined(_WIN32)
  const char *azDirs[] = {
     0, /* GetTempPath */
     0, /* TEMP */
     0, /* TMP */
     ".",
  };
  char *z;
#else
  static const char *azDirs[] = {
     0, /* TMPDIR */
     "/var/tmp",
     "/usr/tmp",
     "/tmp",
     "/temp",
     ".",
  };
#endif
  static const unsigned char zChars[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789";
  unsigned int i;
  const char *zDir = ".";
  int cnt = 0;
  char zRand[16];
  int nBasis;
  const char *zSuffix;

#if defined(_WIN32)
  wchar_t zTmpPath[MAX_PATH];

  if( GetTempPathW(MAX_PATH, zTmpPath) ){
    azDirs[0] = fossil_path_to_utf8(zTmpPath);
    /* Removing trailing \ from the temp path */
    z = (char*)azDirs[0];
    i = (int)strlen(z)-1;
    if( i>0 && z[i]=='\\' ) z[i] = 0;
  }

  azDirs[1] = fossil_getenv("TEMP");
  azDirs[2] = fossil_getenv("TMP");
#else
  azDirs[0] = fossil_getenv("TMPDIR");
#endif

  for(i=0; i<count(azDirs); i++){
    if( azDirs[i]==0 ) continue;
    if( !file_isdir(azDirs[i], ExtFILE) ) continue;
    zDir = azDirs[i];
    break;
  }

  assert( zBasis!=0 );
  zSuffix = 0;
  for(i=0; zBasis[i]; i++){
    if( zBasis[i]=='/' || zBasis[i]=='\\' ){
      zBasis += i+1;
      i = -1;
    }else if( zBasis[i]=='.' ){
      zSuffix = zBasis + i;
    }
  }
  if( zSuffix==0 || zSuffix<=zBasis ){
    zSuffix = "";
    nBasis = i;
  }else{
    nBasis = (int)(zSuffix - zBasis);
  }
  if( nBasis==0 ){
    nBasis = 6;
    zBasis = "fossil";
  }
  do{
    blob_zero(pBuf);
    if( cnt++>20 ) fossil_panic("cannot generate a temporary filename");
    if( zTag==0 ){
      sqlite3_randomness(15, zRand);
      for(i=0; i<15; i++){
        zRand[i] = (char)zChars[ ((unsigned char)zRand[i])%(sizeof(zChars)-1) ];
      }
      zRand[15] = 0;
      zTag = zRand;
    }
    blob_appendf(pBuf, "%s/%.*s~%s%s", zDir, nBasis, zBasis, zTag, zSuffix);
    zTag = 0;
  }while( file_size(blob_str(pBuf), ExtFILE)>=0 );

#if defined(_WIN32)
  fossil_path_free((char *)azDirs[0]);
  fossil_path_free((char *)azDirs[1]);
  fossil_path_free((char *)azDirs[2]);
  /* Change all \ characters in the windows path into / so that they can
  ** be safely passed to a subcommand, such as by gdiff */
  z = blob_buffer(pBuf);
  for(i=0; z[i]; i++) if( z[i]=='\\' ) z[i] = '/';
#else
  fossil_path_free((char *)azDirs[0]);
#endif
}

/*
** Compute a temporary filename in zDir.  The filename is based on
** the current time.
*/
char *file_time_tempname(const char *zDir, const char *zSuffix){
  struct tm *tm;
  unsigned int r;
  static unsigned int cnt = 0;
  time_t t;
  t = time(0);
  tm = gmtime(&t);
  sqlite3_randomness(sizeof(r), &r);
  return mprintf("%s/%04d%02d%02d%02d%02d%02d%04d%06d%s",
      zDir, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, cnt++, r%1000000, zSuffix);
}


/*
** COMMAND: test-tempname
** Usage:  fossil test-name [--time SUFFIX] [--tag NAME] BASENAME ...
**
** Generate temporary filenames derived from BASENAME.  Use the --time
** option to generate temp names based on the time of day.  If --tag NAME
** is specified, try to use NAME as the differentiator in the temp file.
*/
void file_test_tempname(void){
  int i;
  const char *zSuffix = find_option("time",0,1);
  Blob x = BLOB_INITIALIZER;
  char *z;
  const char *zTag = find_option("tag",0,1);
  verify_all_options();
  for(i=2; i<g.argc; i++){
    if( zSuffix ){
      z = file_time_tempname(g.argv[i], zSuffix);
      fossil_print("%s\n", z);
      fossil_free(z);
    }else{
      file_tempname(&x, g.argv[i], zTag);
      fossil_print("%s\n", blob_str(&x));
      blob_reset(&x);
    }
  }
}


/*
** Return true if a file named zName exists and has identical content
** to the blob pContent.  If zName does not exist or if the content is
** different in any way, then return false.
**
** This routine assumes RepoFILE
*/
int file_is_the_same(Blob *pContent, const char *zName){
  i64 iSize;
  int rc;
  Blob onDisk;

  iSize = file_size(zName, RepoFILE);
  if( iSize<0 ) return 0;
  if( iSize!=blob_size(pContent) ) return 0;
  blob_read_from_file(&onDisk, zName, RepoFILE);
  rc = blob_compare(&onDisk, pContent);
  blob_reset(&onDisk);
  return rc==0;
}

/*
** Return the value of an environment variable as UTF8.
** Use fossil_path_free() to release resources.
*/
char *fossil_getenv(const char *zName){
#ifdef _WIN32
  wchar_t *uName = fossil_utf8_to_unicode(zName);
  void *zValue = _wgetenv(uName);
  fossil_unicode_free(uName);
#else
  char *zValue = getenv(zName);
#endif
  if( zValue ) zValue = fossil_path_to_utf8(zValue);
  return zValue;
}

/*
** Sets the value of an environment variable as UTF8.
*/
int fossil_setenv(const char *zName, const char *zValue){
  int rc;
  char *zString = mprintf("%s=%s", zName, zValue);
#ifdef _WIN32
  wchar_t *uString = fossil_utf8_to_unicode(zString);
  rc = _wputenv(uString);
  fossil_unicode_free(uString);
  fossil_free(zString);
#else
  rc = putenv(zString);
  /* NOTE: Cannot free the string on POSIX. */
  /* fossil_free(zString); */
#endif
  return rc;
}

/*
** Like fopen() but always takes a UTF8 argument.
**
** This function assumes ExtFILE. In other words, symbolic links
** are always followed.
*/
FILE *fossil_fopen(const char *zName, const char *zMode){
#ifdef _WIN32
  wchar_t *uMode = fossil_utf8_to_unicode(zMode);
  wchar_t *uName = fossil_utf8_to_path(zName, 0);
  FILE *f = _wfopen(uName, uMode);
  fossil_path_free(uName);
  fossil_unicode_free(uMode);
#else
  FILE *f = fopen(zName, zMode);
#endif
  return f;
}

/*
** Return non-NULL if zFilename contains pathname elements that
** are reserved on Windows.  The returned string is the disallowed
** path element.
*/
const char *file_is_win_reserved(const char *zPath){
  static const char *azRes[] = { "CON", "PRN", "AUX", "NUL", "COM", "LPT" };
  static char zReturn[5];
  int i;
  while( zPath[0] ){
    for(i=0; i<count(azRes); i++){
      if( sqlite3_strnicmp(zPath, azRes[i], 3)==0
       && ((i>=4 && fossil_isdigit(zPath[3])
                 && (zPath[4]=='/' || zPath[4]=='.' || zPath[4]==0))
          || (i<4 && (zPath[3]=='/' || zPath[3]=='.' || zPath[3]==0)))
      ){
        sqlite3_snprintf(5,zReturn,"%.*s", i>=4 ? 4 : 3, zPath);
        return zReturn;
      }
    }
    while( zPath[0] && zPath[0]!='/' ) zPath++;
    while( zPath[0]=='/' ) zPath++;
  }
  return 0;
}

/*
** COMMAND: test-valid-for-windows
** Usage:  fossil test-valid-for-windows FILENAME ....
**
** Show which filenames are not valid for Windows
*/
void file_test_valid_for_windows(void){
  int i;
  for(i=2; i<g.argc; i++){
    fossil_print("%s %s\n", file_is_win_reserved(g.argv[i]), g.argv[i]);
  }
}

/*
** Remove surplus "/" characters from the beginning of a full pathname.
** Extra leading "/" characters are benign on unix.  But on Windows
** machines, they must be removed.  Example:  Convert "/C:/fossil/xyx.fossil"
** into "C:/fossil/xyz.fossil". Cygwin should behave as Windows here.
*/
const char *file_cleanup_fullpath(const char *z){
#if defined(_WIN32) || defined(__CYGWIN__)
  if( z[0]=='/' && fossil_isalpha(z[1]) && z[2]==':' && z[3]=='/' ) z++;
#else
  while( z[0]=='/' && z[1]=='/' ) z++;
#endif
  return z;
}

/*
** Count the number of objects (files and subdirectores) in a given
** directory.  Return the count.  Return -1 of the object is not a
** directory.
*/
int file_directory_size(const char *zDir, const char *zGlob, int omitDotFiles){
  void *zNative;
  DIR *d;
  int n = -1;
  zNative = fossil_utf8_to_path(zDir,1);
  d = opendir(zNative);
  if( d ){
    struct dirent *pEntry;
    n = 0;
    while( (pEntry=readdir(d))!=0 ){
      if( pEntry->d_name[0]==0 ) continue;
      if( omitDotFiles && pEntry->d_name[0]=='.' ) continue;
      if( zGlob ){
        char *zUtf8 = fossil_path_to_utf8(pEntry->d_name);
        int rc = sqlite3_strglob(zGlob, zUtf8);
        fossil_path_free(zUtf8);
        if( rc ) continue;
      }
      n++;
    }
    closedir(d);
  }
  fossil_path_free(zNative);
  return n;
}

/*
** COMMAND: test-dir-size
**
** Usage: %fossil test-dir-size NAME [GLOB] [--nodots]
**
** Return the number of objects in the directory NAME.  If GLOB is
** provided, then only count objects that match the GLOB pattern.
** if --nodots is specified, omit files that begin with ".".
*/
void test_dir_size_cmd(void){
  int omitDotFiles = find_option("nodots",0,0)!=0;
  const char *zGlob;
  const char *zDir;
  verify_all_options();
  if( g.argc!=3 && g.argc!=4 ){
    usage("NAME [GLOB] [-nodots]");
  }
  zDir = g.argv[2];
  zGlob = g.argc==4 ? g.argv[3] : 0;
  fossil_print("%d\n", file_directory_size(zDir, zGlob, omitDotFiles));
}

/*
** Internal helper for touch_cmd(). zAbsName must be resolvable as-is
** to a file - this function does not expand/normalize it. i.e. it
** "really should" be an absolute path. zTreeName is strictly
** cosmetic: it is used when dryRunFlag or verboseFlag generate
** output. It is assumed to be a repo-relative or or subdir-relative
** filename.
**
** newMTime is the file's new timestamp (Unix epoch).
**
** Returns 1 if it sets zAbsName's mtime, 0 if it does not (indicating
** that the file already has that timestamp). Dies fatally if given an
** unresolvable filename. If dryRunFlag is true then it outputs the
** name of the file it would have timestamped but does not stamp the
** file. If verboseFlag is true, it outputs a message if the files
** timestamp is actually modified.
*/
static int touch_cmd_stamp_one_file(char const *zAbsName,
                                    char const *zTreeName,
                                    i64 newMtime, int dryRunFlag,
                                    int verboseFlag){
  i64 const currentMtime = file_mtime(zAbsName, 0);
  if(currentMtime<0){
    fossil_fatal("Cannot stat file: %s\n", zAbsName);
  }else if(currentMtime==newMtime){
    return 0;
  }else if( dryRunFlag!=0 ){
    fossil_print( "dry-run: %s\n", zTreeName );
  }else{
    file_set_mtime(zAbsName, newMtime);
    if( verboseFlag!=0 ){
      fossil_print( "touched %s\n", zTreeName );
    }
  }
  return 1;
}

/*
** Internal helper for touch_cmd(). If the given name is found in the
** given checkout version, which MUST be the checkout version
** currently populating the vfile table, the vfile.mrid value for the
** file is returned, else 0 is returned. zName must be resolvable
** as-is - this function performs neither expands nor normalizes it.
*/
static int touch_cmd_vfile_mrid( int vid, char const *zName ){
  int mrid = 0;
  static Stmt q = empty_Stmt_m;
  db_static_prepare(&q, "SELECT vfile.mrid "
             "FROM vfile LEFT JOIN blob ON vfile.mrid=blob.rid "
             "WHERE vid=:vid AND pathname=:pathname %s",
             filename_collation());
  db_bind_int(&q, ":vid", vid);
  db_bind_text(&q, ":pathname", zName);
  if(SQLITE_ROW==db_step(&q)){
    mrid = db_column_int(&q, 0);
  }
  db_reset(&q);
  return mrid;
}

/*
** COMMAND: touch*
**
** Usage: %fossil touch ?OPTIONS? ?FILENAME...?
**
** For each file in the current checkout matching one of the provided
** list of glob patterns and/or file names, the file's mtime is
** updated to a value specified by one of the flags --checkout,
** --checkin, or --now.
**
** If neither glob patterns nor filenames are provided, it operates on
** all files managed by the currently checked-out version.
**
** This command gets its name from the conventional Unix "touch"
** command.
**
** Options:
**   --now          Stamp each affected file with the current time.
**                  This is the default behavior.
**   -c|--checkin   Stamp each affected file with the time of the
**                  most recent check-in which modified that file.
**   -C|--checkout  Stamp each affected file with the time of the
**                  currently-checked-out version.
**   -g GLOBLIST    Comma-separated list of glob patterns. Default
**                  is to touch all SCM-controlled files.
**   -G GLOBFILE    Similar to -g but reads its globs from a
**                  fossil-conventional glob list file.
**   -v|-verbose    Outputs extra information about its globs
**                  and each file it touches.
**   -n|--dry-run   Outputs which files would require touching,
**                  but does not touch them.
**   -q|--quiet     Suppress warnings when skipping unmanaged
**                  or out-of-tree files.
**
** Only one of --now, --checkin, and --checkout may be used. The
** default is --now.
**
** Only one of -g or -G may be used. If neither is provided and no
** additional filenames are provided, the effect is as if a glob of
** '*' were provided. Note that all glob patterns provided via these
** flags are always evaluated as if they are relative to the top of
** the source tree, not the current working (sub)directory. Filenames
** provided without these flags, on the other hand, are treated as
** relative to the current directory.
**
*/
void touch_cmd(){
  const char * zGlobList; /* -g List of glob patterns */ 
  const char * zGlobFile; /* -G File of glob patterns */
  Glob * pGlob = 0;       /* List of glob patterns */
  int verboseFlag;
  int dryRunFlag;
  int vid;                /* Checkout version */
  int changeCount = 0;    /* Number of files touched */
  int quietFlag = 0;      /* -q|--quiet */
  int timeFlag;           /* -1==--checkin, 1==--checkout, 0==--now */
  i64 nowTime = 0;        /* Timestamp of --now or --checkout */
  Stmt q;
  Blob absBuffer = empty_blob;

  verboseFlag = find_option("verbose","v",0)!=0;
  quietFlag = find_option("quiet","q",0)!=0;
  dryRunFlag = find_option("dry-run","n",0)!=0
    || find_option("dryrun",0,0)!=0;
  zGlobList = find_option("glob", "g",1);
  zGlobFile = find_option("globfile", "G",1);

  if(zGlobList && zGlobFile){
    fossil_fatal("Options -g and -G may not be used together.");
  }

  {
    int const ci =
      (find_option("checkin","c",0) || find_option("check-in",0,0))
      ? 1 : 0;
    int const co = find_option("checkout","C",0) ? 1 : 0;
    int const now = find_option("now",0,0) ? 1 : 0;
    if(ci + co + now > 1){
      fossil_fatal("Options --checkin, --checkout, and --now may "
                   "not be used together.");
    }else if(co){
      timeFlag = 1;
      if(verboseFlag){
        fossil_print("Timestamp = current checkout version.\n");
      }
    }else if(ci){
      timeFlag = -1;
      if(verboseFlag){
        fossil_print("Timestamp = checkin in which each file was "
                     "most recently modified.\n");
      }
    }else{
      timeFlag = 0;
      if(verboseFlag){
        fossil_print("Timestamp = current system time.\n");
      }
    }
  }

  verify_all_options();

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if(vid==0){
    fossil_fatal("Cannot determine checkout version.");
  }

  if(zGlobList){
    pGlob = *zGlobList ? glob_create(zGlobList) : 0;
  }else if(zGlobFile){
    Blob globs = empty_blob;
    blob_read_from_file(&globs, zGlobFile, ExtFILE);
    pGlob = glob_create( globs.aData );
    blob_reset(&globs);
  }
  if( pGlob && verboseFlag!=0 ){
    int i;
    for(i=0; i<pGlob->nPattern; ++i){
      fossil_print("glob: %s\n", pGlob->azPattern[i]);
    }
  }

  db_begin_transaction();
  if(timeFlag==0){/*--now*/
    nowTime = time(0);
  }else if(timeFlag>0){/*--checkout: get the checkout
                         manifest's timestamp*/
    assert(vid>0);
    nowTime = db_int64(-1,
                       "SELECT CAST(strftime('%%s',"
                         "(SELECT mtime FROM event WHERE objid=%d)"
                       ") AS INTEGER)", vid);
    if(nowTime<0){
      fossil_fatal("Could not determine out checkout version's time!");
    }
  }else{ /* --checkin */
    assert(0 == nowTime);
  }
  if((pGlob && pGlob->nPattern>0)
     || g.argc<3 /* no non-flag arguments */ ){
    /*
    ** We have either globs or no trailing filenames (in which case an
    ** effective glob pattern of '*' is assumed). If there are neither
    ** globs nor filenames then we operate on all managed files.
    */
    db_prepare(&q,
               "SELECT vfile.mrid, pathname "
               "FROM vfile LEFT JOIN blob ON vfile.mrid=blob.rid "
               "WHERE vid=%d", vid);
    while(SQLITE_ROW==db_step(&q)){
      int const fid = db_column_int(&q, 0);
      const char * zName = db_column_text(&q, 1);
      i64 newMtime = nowTime;
      char const * zAbs = 0;         /* absolute path */
      absBuffer.nUsed = 0;
      assert(timeFlag<0 ? newMtime==0 : newMtime>0);
      if(pGlob){
        if(glob_match(pGlob, zName)==0) continue;
      }
      blob_appendf( &absBuffer, "%s%s", g.zLocalRoot, zName );
      zAbs = blob_str(&absBuffer);
      if( newMtime || mtime_of_manifest_file(vid, fid, &newMtime)==0 ){
        changeCount +=
          touch_cmd_stamp_one_file( zAbs, zName, newMtime,
                                    dryRunFlag, verboseFlag );
      }
    }
    db_finalize(&q);
  }
  glob_free(pGlob);
  pGlob = 0;
  if(g.argc>2){
    /*
    ** Trailing filenames on the command line. These require extra
    ** care to avoid modifying unmanaged or out-of-tree files and
    ** finding an associated --checkin timestamp.
    */
    int i;
    Blob treeNameBuf = empty_blob;
    for( i = 2; i < g.argc; ++i,
           blob_reset(&treeNameBuf) ){
      char const * zArg = g.argv[i];
      char const * zTreeFile;        /* repo-relative filename */
      char const * zAbs;             /* absolute filename */
      i64 newMtime = nowTime;
      int nameCheck;
      int fid;                       /* vfile.mrid of file */
      absBuffer.nUsed = 0;
      nameCheck = file_tree_name( zArg, &treeNameBuf, 0, 0 );
      if(nameCheck==0){
        if(quietFlag==0){
          fossil_print("SKIPPING out-of-tree file: %s\n", zArg);
        }
        continue;
      }
      zTreeFile = blob_str(&treeNameBuf);
      fid = touch_cmd_vfile_mrid( vid, zTreeFile );
      if(fid==0){
        if(quietFlag==0){
          fossil_print("SKIPPING unmanaged file: %s\n", zArg);
        }
        continue;
      }
      blob_appendf(&absBuffer, "%s%s", g.zLocalRoot, zTreeFile);
      zAbs = blob_str(&absBuffer);
      if(timeFlag<0){/*--checkin*/
        if(mtime_of_manifest_file( vid, fid, &newMtime )!=0){
          fossil_fatal("Could not resolve --checkin mtime of %s", zTreeFile);
        }
      }else{
        assert(newMtime>0);
      }
      changeCount +=
        touch_cmd_stamp_one_file( zAbs, zArg, newMtime,
                                  dryRunFlag, verboseFlag );
    }        
  }
  db_end_transaction(0);
  blob_reset(&absBuffer);
  if( dryRunFlag!=0 ){
    fossil_print("dry-run: would have touched %d file(s)\n",
                 changeCount);
  }
  fossil_print("Touched %d file(s)\n", changeCount);
}
