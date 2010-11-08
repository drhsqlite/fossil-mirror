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
** File utilities
*/
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file.h"

/*
** The file status information from the most recent stat() call.
**
** Use _stati64 rather than stat on windows, in order to handle files
** larger than 2GB.
*/
#if defined(_WIN32) && defined(__MSVCRT__)
  static struct _stati64 fileStat;
# define stat _stati64
#else
  static struct stat fileStat;
#endif
static int fileStatValid = 0;

/*
** Fill in the fileStat variable for the file named zFilename.
** If zFilename==0, then use the previous value of fileStat if
** there is a previous value.
**
** Return the number of errors.  No error messages are generated.
*/
static int getStat(const char *zFilename){
  int rc = 0;
  if( zFilename==0 ){
    if( fileStatValid==0 ) rc = 1;
  }else{
    if( stat(zFilename, &fileStat)!=0 ){
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
  return getStat(zFilename) ? -1 : fileStat.st_size;
}

/*
** Return the modification time for a file.  Return -1 if the file
** does not exist.  If zFilename is NULL return the size of the most
** recently stat-ed file.
*/
i64 file_mtime(const char *zFilename){
  return getStat(zFilename) ? -1 : fileStat.st_mtime;
}

/*
** Return TRUE if the named file is an ordinary file.  Return false
** for directories, devices, fifos, symlinks, etc.
*/
int file_isfile(const char *zFilename){
  return getStat(zFilename) ? 0 : S_ISREG(fileStat.st_mode);
}

/*
** Return TRUE if the named file is an executable.  Return false
** for directories, devices, fifos, symlinks, etc.
*/
int file_isexe(const char *zFilename){
  if( getStat(zFilename) || !S_ISREG(fileStat.st_mode) ) return 0;
#if defined(_WIN32)
#  if defined(__DMC__) || defined(_MSC_VER)
#    define S_IXUSR  _S_IEXEC
#  endif
  return ((S_IXUSR)&fileStat.st_mode)!=0;
#else
  return ((S_IXUSR|S_IXGRP|S_IXOTH)&fileStat.st_mode)!=0;
#endif
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
    file_simplify_name(zFN, -1);
    rc = getStat(zFN);
    free(zFN);
  }else{
    rc = getStat(0);
  }
  return rc ? 0 : (S_ISDIR(fileStat.st_mode) ? 1 : 2);
}

/*
** Return the tail of a file pathname.  The tail is the last component
** of the path.  For example, the tail of "/a/b/c.d" is "c.d".
*/
const char *file_tail(const char *z){
  const char *zTail = z;
  while( z[0] ){
    if( z[0]=='/' ) zTail = &z[1];
    z++;
  }
  return zTail;
}

/*
** Copy the content of a file from one place to another.
*/
void file_copy(const char *zFrom, const char *zTo){
  FILE *in, *out;
  int got;
  char zBuf[8192];
  in = fopen(zFrom, "rb");
  if( in==0 ) fossil_fatal("cannot open \"%s\" for reading", zFrom);
  out = fopen(zTo, "wb");
  if( out==0 ) fossil_fatal("cannot open \"%s\" for writing", zTo);
  while( (got=fread(zBuf, 1, sizeof(zBuf), in))>0 ){
    fwrite(zBuf, 1, got, out);
  }
  fclose(in);
  fclose(out);
}

/*
** Set or clear the execute bit on a file.
*/
void file_setexe(const char *zFilename, int onoff){
#if !defined(_WIN32)
  struct stat buf;
  if( stat(zFilename, &buf)!=0 ) return;
  if( onoff ){
    if( (buf.st_mode & 0111)!=0111 ){
      chmod(zFilename, buf.st_mode | 0111);
    }
  }else{
    if( (buf.st_mode & 0111)!=0 ){
      chmod(zFilename, buf.st_mode & ~0111);
    }
  }
#endif /* _WIN32 */
}

/*
** Create the directory named in the argument, if it does not already
** exist.  If forceFlag is 1, delete any prior non-directory object 
** with the same name.
**
** Return the number of errors.
*/
int file_mkdir(const char *zName, int forceFlag){
  int rc = file_isdir(zName);
  if( rc==2 ){
    if( !forceFlag ) return 1;
    unlink(zName);
  }
  if( rc!=1 ){
#if defined(_WIN32)
    return mkdir(zName);
#else
    return mkdir(zName, 0755);
#endif
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
**     *  Does not contain any of these characters in the path: "\*[]?"
**     *  Does not end with "/".
**     *  Does not contain two or more "/" characters in a row.
**     *  Contains at least one character
*/
int file_is_simple_pathname(const char *z){
  int i;
  char c = z[0];
  if( c=='/' || c==0 ) return 0;
  if( c=='.' ){
    if( z[1]=='/' || z[1]==0 ) return 0;
    if( z[1]=='.' && (z[2]=='/' || z[2]==0) ) return 0;
  }
  for(i=0; (c=z[i])!=0; i++){
    if( c=='\\' || c=='*' || c=='[' || c==']' || c=='?' ){
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
** Simplify a filename by
**
**  * removing any trailing and duplicate /
**  * removing /./
**  * removing /A/../
**
** Changes are made in-place.  Return the new name length.
*/
int file_simplify_name(char *z, int n){
  int i, j;
  if( n<0 ) n = strlen(z);
#if defined(_WIN32)
  for(i=0; i<n; i++){
    if( z[i]=='\\' ) z[i] = '/';
  }
#endif
  while( n>1 && z[n-1]=='/' ){ n--; }
  for(i=j=0; i<n; i++){
    if( z[i]=='/' ){
      if( z[i+1]=='/' ) continue;
      if( z[i+1]=='.' && (i+2==n || z[i+2]=='/') ){
        i += 1;
        continue;
      }
      if( z[i+1]=='.' && i+2<n && z[i+2]=='.' && (i+3==n || z[i+3]=='/') ){
        while( j>0 && z[j-1]!='/' ){ j--; }
        if( j>0 ){ j--; }
        i += 2;
        continue;
      }
    }
    z[j++] = z[i];
  }
  z[j] = 0;
  return j;
}

/*
** Compute a canonical pathname for a file or directory.
** Make the name absolute if it is relative.
** Remove redundant / characters
** Remove all /./ path elements.
** Convert /A/../ to just /
*/
void file_canonical_name(const char *zOrigName, Blob *pOut){
  if( zOrigName[0]=='/' 
#if defined(_WIN32)
      || zOrigName[0]=='\\'
      || (strlen(zOrigName)>3 && zOrigName[1]==':'
           && (zOrigName[2]=='\\' || zOrigName[2]=='/'))
#endif
  ){
    blob_set(pOut, zOrigName);
    blob_materialize(pOut);
  }else{
    char zPwd[2000];
    if( getcwd(zPwd, sizeof(zPwd)-20)==0 ){
      fprintf(stderr, "pwd too big: max %d\n", (int)sizeof(zPwd)-20);
      fossil_exit(1);
    }
    blob_zero(pOut);
    blob_appendf(pOut, "%//%/", zPwd, zOrigName);
  }
  blob_resize(pOut, file_simplify_name(blob_buffer(pOut), blob_size(pOut)));
}

/*
** COMMAND:  test-canonical-name
** Usage: %fossil test-canonical-name FILENAME...
**
** Test the operation of the canonical name generator.
** Also test Fossil's ability to measure attributes of a file.
*/
void cmd_test_canonical_name(void){
  int i;
  Blob x;
  blob_zero(&x);
  for(i=2; i<g.argc; i++){
    char zBuf[100];
    const char *zName = g.argv[i];
    file_canonical_name(zName, &x);
    printf("%s\n", blob_buffer(&x));
    blob_reset(&x);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_size(zName));
    printf("  file_size   = %s\n", zBuf);
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", file_mtime(zName));
    printf("  file_mtime  = %s\n", zBuf);
    printf("  file_isfile = %d\n", file_isfile(zName));
    printf("  file_isexe  = %d\n", file_isexe(zName));
    printf("  file_isdir  = %d\n", file_isdir(zName));
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
#if defined(_WIN32)
    && (z[0]==0 || z[1]!=':' || z[2]!='/')
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
** Compute a pathname for a file or directory that is relative
** to the current directory.
*/
void file_relative_name(const char *zOrigName, Blob *pOut){
  char *zPath;
  blob_set(pOut, zOrigName);
  blob_resize(pOut, file_simplify_name(blob_buffer(pOut), blob_size(pOut))); 
  zPath = blob_buffer(pOut);
  if( zPath[0]=='/' ){
    int i, j;
    Blob tmp;
    char zPwd[2000];
    if( getcwd(zPwd, sizeof(zPwd)-20)==0 ){
      fprintf(stderr, "pwd too big: max %d\n", (int)sizeof(zPwd)-20);
      fossil_exit(1);
    }
    for(i=1; zPath[i] && zPwd[i]==zPath[i]; i++){}
    if( zPath[i]==0 ){
      blob_reset(pOut);
      if( zPwd[i]==0 ){
        blob_append(pOut, ".", 1);
      }else{
        blob_append(pOut, "..", 2);
        for(j=i+1; zPwd[j]; j++){
          if( zPwd[j]=='/' ) {
            blob_append(pOut, "/..", 3);
          }
        }
      }
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
    blob_set(&tmp, "../");
    for(j=i; zPwd[j]; j++){
      if( zPwd[j]=='/' ) {
        blob_append(&tmp, "../", 3);
      }
    }
    blob_append(&tmp, &zPath[i], -1);
    blob_reset(pOut);
    memcpy(pOut, &tmp, sizeof(tmp));
  }
}

/*
** COMMAND:  test-relative-name
**
** Test the operation of the relative name generator.
*/
void cmd_test_relative_name(void){
  int i;
  Blob x;
  blob_zero(&x);
  for(i=2; i<g.argc; i++){
    file_relative_name(g.argv[i], &x);
    printf("%s\n", blob_buffer(&x));
    blob_reset(&x);
  }
}

/*
** Compute a pathname for a file relative to the root of the local
** tree.  Return TRUE on success.  On failure, print and error
** message and quit if the errFatal flag is true.  If errFatal is
** false, then simply return 0.
**
** The root of the tree is defined by the g.zLocalRoot variable.
*/
int file_tree_name(const char *zOrigName, Blob *pOut, int errFatal){
  int n;
  Blob full;
  int nFull;
  char *zFull;

  db_must_be_within_tree();
  file_canonical_name(zOrigName, &full);
  n = strlen(g.zLocalRoot);
  assert( n>0 && g.zLocalRoot[n-1]=='/' );
  nFull = blob_size(&full);
  zFull = blob_buffer(&full);

  /* Special case.  zOrigName refers to g.zLocalRoot directory. */
  if( nFull==n-1 && memcmp(g.zLocalRoot, zFull, nFull)==0 ){
    blob_append(pOut, ".", 1);
    return 1;
  }

  if( nFull<=n || memcmp(g.zLocalRoot, zFull, n) ){
    blob_reset(&full);
    if( errFatal ){
      fossil_fatal("file outside of checkout tree: %s", zOrigName);
    }
    return 0;
  }
  blob_zero(pOut);
  blob_append(pOut, &zFull[n], nFull-n);
  return 1;
}

/*
** COMMAND:  test-tree-name
**
** Test the operation of the tree name generator.
*/
void cmd_test_tree_name(void){
  int i;
  Blob x;
  blob_zero(&x);
  for(i=2; i<g.argc; i++){
    if( file_tree_name(g.argv[i], &x, 1) ){
      printf("%s\n", blob_buffer(&x));
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
** Construct a random temporary filename into zBuf[].
*/
void file_tempname(int nBuf, char *zBuf){
  static const char *azDirs[] = {
     "/var/tmp",
     "/usr/tmp",
     "/tmp",
     "/temp",
     ".",
  };
  static const unsigned char zChars[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789";
  unsigned int i, j;
  struct stat buf;
  const char *zDir = ".";
  
  for(i=0; i<sizeof(azDirs)/sizeof(azDirs[0]); i++){
    if( stat(azDirs[i], &buf) ) continue;
    if( !S_ISDIR(buf.st_mode) ) continue;
    if( access(azDirs[i], 07) ) continue;
    zDir = azDirs[i];
    break;
  }

  /* Check that the output buffer is large enough for the temporary file 
  ** name. If it is not, return SQLITE_ERROR.
  */
  if( (strlen(zDir) + 17) >= (size_t)nBuf ){
    fossil_fatal("insufficient space for temporary filename");
  }

  do{
    sqlite3_snprintf(nBuf-17, zBuf, "%s/", zDir);
    j = (int)strlen(zBuf);
    sqlite3_randomness(15, &zBuf[j]);
    for(i=0; i<15; i++, j++){
      zBuf[j] = (char)zChars[ ((unsigned char)zBuf[j])%(sizeof(zChars)-1) ];
    }
    zBuf[j] = 0;
  }while( access(zBuf,0)==0 );
}
