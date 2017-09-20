/*
** Copyright (c) 2006 D. Richard Hipp
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
** File utilities.
**
** Functions named file_* are generic functions that always follow symlinks.
**
** Functions named file_wd_* are to be used for files inside working
** directories. They follow symlinks depending on 'allow-symlinks' setting.
*/
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
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

#include <dirent.h>
#if defined(_WIN32)
# define DIR _WDIR
# define dirent _wdirent
# define opendir _wopendir
# define readdir _wreaddir
# define closedir _wclosedir
#endif /* _WIN32 */

#if defined(_WIN32) && (defined(__MSVCRT__) || defined(_MSC_VER))
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
# define fossilStat stat
#endif

/*
** On Windows S_ISLNK always returns FALSE.
*/
#if !defined(S_ISLNK)
# define S_ISLNK(x) (0)
#endif
static int fileStatValid = 0;
static struct fossilStat fileStat;

/*
** Fill stat buf with information received from stat() or lstat().
** lstat() is called on Unix if isWd is TRUE and allow-symlinks setting is on.
**
*/
static int fossil_stat(
  const char *zFilename,  /* name of file or directory to inspect. */
  struct fossilStat *buf, /* pointer to buffer where info should go. */
  int isWd                /* non-zero to consider look at symlink itself. */
){
  int rc;
  void *zMbcs = fossil_utf8_to_path(zFilename, 0);
#if !defined(_WIN32)
  if( isWd && db_allow_symlinks() ){
    rc = lstat(zMbcs, buf);
  }else{
    rc = stat(zMbcs, buf);
  }
#else
  rc = win32_stat(zMbcs, buf, isWd);
#endif
  fossil_path_free(zMbcs);
  return rc;
}

/*
** Clears the fileStat variable and its associated validity flag.
*/
static void resetStat(){
  fileStatValid = 0;
  memset(&fileStat, 0, sizeof(struct fossilStat));
}

/*
** Fill in the fileStat variable for the file named zFilename.
** If zFilename==0, then use the previous value of fileStat if
** there is a previous value.
**
** If isWd is TRUE, do lstat() instead of stat() if allow-symlinks is on.
**
** Return the number of errors.  No error messages are generated.
*/
static int getStat(const char *zFilename, int isWd){
  int rc = 0;
  if( zFilename==0 ){
    if( fileStatValid==0 ) rc = 1;
  }else{
    if( fossil_stat(zFilename, &fileStat, isWd)!=0 ){
      fileStatValid = 0;
      rc = 1;
    }else{
      fileStatValid = 1;
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
i64 file_size(const char *zFilename){
  return getStat(zFilename, 0) ? -1 : fileStat.st_size;
}

/*
** Same as file_size(), but takes into account symlinks.
*/
i64 file_wd_size(const char *zFilename){
  return getStat(zFilename, 1) ? -1 : fileStat.st_size;
}

/*
** Return the modification time for a file.  Return -1 if the file
** does not exist.  If zFilename is NULL return the size of the most
** recently stat-ed file.
*/
i64 file_mtime(const char *zFilename){
  return getStat(zFilename, 0) ? -1 : fileStat.st_mtime;
}

/*
** Same as file_mtime(), but takes into account symlinks.
*/
i64 file_wd_mtime(const char *zFilename){
  return getStat(zFilename, 1) ? -1 : fileStat.st_mtime;
}

/*
** Return the mode bits for a file.  Return -1 if the file does not
** exist.  If zFilename is NULL return the size of the most recently
** stat-ed file.
*/
int file_mode(const char *zFilename){
  return getStat(zFilename, 0) ? -1 : fileStat.st_mode;
}

/*
** Same as file_mode(), but takes into account symlinks.
*/
int file_wd_mode(const char *zFilename){
  return getStat(zFilename, 1) ? -1 : fileStat.st_mode;
}

/*
** Return TRUE if the named file is an ordinary file or symlink
** and symlinks are allowed.
** Return false for directories, devices, fifos, etc.
*/
int file_wd_isfile_or_link(const char *zFilename){
  return getStat(zFilename, 1) ? 0 : S_ISREG(fileStat.st_mode) ||
                                     S_ISLNK(fileStat.st_mode);
}

/*
** Return TRUE if the named file is an ordinary file.  Return false
** for directories, devices, fifos, symlinks, etc.
*/
int file_isfile(const char *zFilename){
  return getStat(zFilename, 0) ? 0 : S_ISREG(fileStat.st_mode);
}

/*
** Same as file_isfile(), but takes into account symlinks.
*/
int file_wd_isfile(const char *zFilename){
  return getStat(zFilename, 1) ? 0 : S_ISREG(fileStat.st_mode);
}

/*
** Create symlink to file on Unix, or plain-text file with
** symlink target if "allow-symlinks" is off or we're on Windows.
**
** Arguments: target file (symlink will point to it), link file
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
          if( file_mkdir(zName, 1) ){
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
*/
int file_wd_perm(const char *zFilename){
#if !defined(_WIN32)
  if( !getStat(zFilename, 1) ){
     if( S_ISREG(fileStat.st_mode) && ((S_IXUSR)&fileStat.st_mode)!=0 )
      return PERM_EXE;
    else if( db_allow_symlinks() && S_ISLNK(fileStat.st_mode) )
      return PERM_LNK;
  }
#endif
  return PERM_REG;
}

/*
** Return TRUE if the named file is an executable.  Return false
** for directories, devices, fifos, symlinks, etc.
*/
int file_wd_isexe(const char *zFilename){
  return file_wd_perm(zFilename)==PERM_EXE;
}

/*
** Return TRUE if the named file is a symlink and symlinks are allowed.
** Return false for all other cases.
**
** On Windows, always return False.
*/
int file_wd_islink(const char *zFilename){
  return file_wd_perm(zFilename)==PERM_LNK;
}

/*
** Return 1 if zFilename is a directory.  Return 0 if zFilename
** does not exist.  Return 2 if zFilename exists but is something
** other than a directory.
*/
int file_isdir(const char *zFilename){
  int rc;

  if( zFilename ){
    char *zFN = mprintf("%s", zFilename);
    file_simplify_name(zFN, -1, 0);
    rc = getStat(zFN, 0);
    free(zFN);
  }else{
    rc = getStat(0, 0);
  }
  return rc ? 0 : (S_ISDIR(fileStat.st_mode) ? 1 : 2);
}

/*
** Same as file_isdir(), but takes into account symlinks.  Return 1 if
** zFilename is a directory -OR- a symlink that points to a directory.
** Return 0 if zFilename does not exist.  Return 2 if zFilename exists
** but is something other than a directory.
*/
int file_wd_isdir(const char *zFilename){
  int rc;
  char *zFN;

  zFN = mprintf("%s", zFilename);
  file_simplify_name(zFN, -1, 0);
  rc = getStat(zFN, 1);
  if( rc ){
    rc = 0; /* It does not exist at all. */
  }else if( S_ISDIR(fileStat.st_mode) ){
    rc = 1; /* It exists and is a real directory. */
  }else if( S_ISLNK(fileStat.st_mode) ){
    Blob content;
    blob_read_link(&content, zFN); /* It exists and is a link. */
    rc = file_wd_isdir(blob_str(&content)); /* Points to directory? */
    blob_reset(&content);
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
  while( file_size(z)>=0 ){
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
  file_mkfolder(zTo, 0, 0);
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
*/
int file_wd_setexe(const char *zFilename, int onoff){
  int rc = 0;
#if !defined(_WIN32)
  struct stat buf;
  if( fossil_stat(zFilename, &buf, 1)!=0 || S_ISLNK(buf.st_mode) ) return 0;
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
  iMTime = file_wd_mtime(zFile);
  zDate = db_text(0, "SELECT datetime(%lld, 'unixepoch')", iMTime);
  fossil_print("Set mtime of \"%s\" to %s (%lld)\n", zFile, zDate, iMTime);
}

/*
** Delete a file.
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

/*
** Create the directory named in the argument, if it does not already
** exist.  If forceFlag is 1, delete any prior non-directory object
** with the same name.
**
** Return the number of errors.
*/
int file_mkdir(const char *zName, int forceFlag){
  int rc = file_wd_isdir(zName);
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
int file_mkfolder(const char *zFilename, int forceFlag, int errorReturn){
  int nName, rc = 0;
  char *zName;

  nName = strlen(zFilename);
  zName = mprintf("%s", zFilename);
  nName = file_simplify_name(zName, nName, 0);
  while( nName>0 && zName[nName-1]!='/' ){ nName--; }
  if( nName ){
    zName[nName-1] = 0;
    if( file_wd_isdir(zName)!=1 ){
      rc = file_mkfolder(zName, forceFlag, errorReturn);
      if( rc==0 ){
        if( file_mkdir(zName, forceFlag) && file_wd_isdir(zName)!=1 ){
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
  int rc = file_wd_isdir(zName);
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
*/
void file_getcwd(char *zBuf, int nBuf){
#ifdef _WIN32
  win32_getcwd(zBuf, nBuf);
#else
  if( getcwd(zBuf, nBuf-1)==0 ){
    if( errno==ERANGE ){
      fossil_fatal("pwd too big: max %d", nBuf-1);
    }else{
      fossil_fatal("cannot find current working directory; %s",
                   strerror(errno));
    }
  }
#endif
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
  int raw,
  int slash,
  int reset
){
  char zBuf[100];
  Blob x;
  memset(zBuf, 0, sizeof(zBuf));
  blob_zero(&x);
  file_canonical_name(zPath, &x, slash);
  fossil_print("%s[%s] -> [%s]\n", raw ? "RAW " : "", zPath, blob_buffer(&x));
  blob_reset(&x);
  if( raw ){
    int rc;
    struct fossilStat testFileStat;
    memset(&testFileStat, 0, sizeof(struct fossilStat));
    rc = fossil_stat(zPath, &testFileStat, 0);
    fossil_print("  stat_rc      = %d\n", rc);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", testFileStat.st_size);
    fossil_print("  stat_size    = %s\n", zBuf);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", testFileStat.st_mtime);
    fossil_print("  stat_mtime   = %s\n", zBuf);
    fossil_print("  stat_mode    = %d\n", testFileStat.st_mode);
    memset(&testFileStat, 0, sizeof(struct fossilStat));
    rc = fossil_stat(zPath, &testFileStat, 1);
    fossil_print("  l_stat_rc    = %d\n", rc);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", testFileStat.st_size);
    fossil_print("  l_stat_size  = %s\n", zBuf);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", testFileStat.st_mtime);
    fossil_print("  l_stat_mtime = %s\n", zBuf);
    fossil_print("  l_stat_mode  = %d\n", testFileStat.st_mode);
  }else{
    if( reset ) resetStat();
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_wd_size(zPath));
    fossil_print("  file_size           = %s\n", zBuf);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_wd_mtime(zPath));
    fossil_print("  file_mtime          = %s\n", zBuf);
    fossil_print("  file_mode           = %d\n", file_wd_mode(zPath));
    fossil_print("  file_isfile         = %d\n", file_wd_isfile(zPath));
    fossil_print("  file_isfile_or_link = %d\n", file_wd_isfile_or_link(zPath));
    fossil_print("  file_islink         = %d\n", file_wd_islink(zPath));
    fossil_print("  file_isexe          = %d\n", file_wd_isexe(zPath));
    fossil_print("  file_isdir          = %d\n", file_wd_isdir(zPath));
    if( reset ) resetStat();
  }
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
**     --open-config        Open the configuration database first.
**     --slash              Trailing slashes, if any, are retained.
**     --reset              Reset cached stat() info for each file.
*/
void cmd_test_file_environment(void){
  int i;
  int slashFlag = find_option("slash",0,0)!=0;
  int resetFlag = find_option("reset",0,0)!=0;
  if( find_option("open-config", 0, 0)!=0 ){
    Th_OpenConfig(1);
  }
  fossil_print("filenames_are_case_sensitive() = %d\n",
               filenames_are_case_sensitive());
  fossil_print("db_allow_symlinks_by_default() = %d\n",
               db_allow_symlinks_by_default());
  fossil_print("db_allow_symlinks() = %d\n", db_allow_symlinks());
  for(i=2; i<g.argc; i++){
    emitFileStat(g.argv[i], 1, slashFlag, resetFlag);
    emitFileStat(g.argv[i], 0, slashFlag, resetFlag);
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
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_wd_size(zName));
    fossil_print("  file_size   = %s\n", zBuf);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_wd_mtime(zName));
    fossil_print("  file_mtime  = %s\n", zBuf);
    fossil_print("  file_isfile = %d\n", file_wd_isfile(zName));
    fossil_print("  file_isfile_or_link = %d\n",file_wd_isfile_or_link(zName));
    fossil_print("  file_islink = %d\n", file_wd_islink(zName));
    fossil_print("  file_isexe  = %d\n", file_wd_isexe(zName));
    fossil_print("  file_isdir  = %d\n", file_wd_isdir(zName));
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
** Construct a random temporary filename into pBuf starting with zPrefix.
*/
void file_tempname(Blob *pBuf, const char *zPrefix){
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
    if( !file_isdir(azDirs[i]) ) continue;
    zDir = azDirs[i];
    break;
  }

  do{
    blob_zero(pBuf);
    if( cnt++>20 ) fossil_panic("cannot generate a temporary filename");
    sqlite3_randomness(15, zRand);
    for(i=0; i<15; i++){
      zRand[i] = (char)zChars[ ((unsigned char)zRand[i])%(sizeof(zChars)-1) ];
    }
    zRand[15] = 0;
    blob_appendf(pBuf, "%s/%s-%s.txt", zDir, zPrefix ? zPrefix : "", zRand);
  }while( file_size(blob_str(pBuf))>=0 );

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
** COMMAND: test-tempname
** Usage:  fossil test-name BASENAME ...
**
** Generate temporary filenames derived from BASENAME
*/
void file_test_tempname(void){
  int i;
  Blob x = BLOB_INITIALIZER;
  for(i=2; i<g.argc; i++){
    file_tempname(&x, g.argv[i]);
    fossil_print("%s\n", blob_str(&x));
    blob_reset(&x);
  }
}


/*
** Return true if a file named zName exists and has identical content
** to the blob pContent.  If zName does not exist or if the content is
** different in any way, then return false.
*/
int file_is_the_same(Blob *pContent, const char *zName){
  i64 iSize;
  int rc;
  Blob onDisk;

  iSize = file_wd_size(zName);
  if( iSize<0 ) return 0;
  if( iSize!=blob_size(pContent) ) return 0;
  if( file_wd_islink(zName) ){
    blob_read_link(&onDisk, zName);
  }else{
    blob_read_from_file(&onDisk, zName);
  }
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
