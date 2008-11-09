/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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
** Return the size of a file in bytes.  Return -1 if the file does not
** exist.
*/
i64 file_size(const char *zFilename){
  struct stat buf;
  if( stat(zFilename, &buf)!=0 ){
    return -1;
  }
  return buf.st_size;
}

/*
** Return the modification time for a file.  Return -1 if the file
** does not exist.
*/
i64 file_mtime(const char *zFilename){
  struct stat buf;
  if( stat(zFilename, &buf)!=0 ){
    return -1;
  }
  return buf.st_mtime;
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
** Return TRUE if the named file is an ordinary file.  Return false
** for directories, devices, fifos, symlinks, etc.
*/
int file_isfile(const char *zFilename){
  struct stat buf;
  if( stat(zFilename, &buf)!=0 ){
    return 0;
  }
  return S_ISREG(buf.st_mode);
}

/*
** Return TRUE if the named file is an executable.  Return false
** for directories, devices, fifos, symlinks, etc.
*/
int file_isexe(const char *zFilename){
  struct stat buf;
  if( stat(zFilename, &buf)!=0 ){
    return 0;
  }
  if( !S_ISREG(buf.st_mode) ) return 0;
#ifdef __MINGW32__
  return ((S_IXUSR)&buf.st_mode)!=0;
#else
  return ((S_IXUSR|S_IXGRP|S_IXOTH)&buf.st_mode)!=0;
#endif
}

/*
** Set or clear the execute bit on a file.
*/
void file_setexe(const char *zFilename, int onoff){
#ifndef __MINGW32__
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
#endif
}

/*
** Return 1 if zFilename is a directory.  Return 0 if zFilename
** does not exist.  Return 2 if zFilename exists but is something
** other than a directory.
*/
int file_isdir(const char *zFilename){
  struct stat buf;
  if( stat(zFilename, &buf)!=0 ){
    return 0;
  }
  return S_ISDIR(buf.st_mode) ? 1 : 2;
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
#ifdef __MINGW32__
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
  if( *z=='/' || *z==0 ) return 0;
  if( *z=='.' ){
    if( z[1]=='/' || z[1]==0 ) return 0;
    if( z[1]=='.' && (z[2]=='/' || z[2]==0) ) return 0;
  }
  for(i=0; z[i]; i++){
    if( z[i]=='\\' || z[i]=='*' || z[i]=='[' || z[i]==']' || z[i]=='?' ){
      return 0;
    }
    if( z[i]=='/' ){
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
**  * removing /A/./
**
** Changes are made in-place.  Return the new name length.
*/
int file_simplify_name(char *z, int n){
  int i, j;
  while( n>1 && z[n-1]=='/' ){ n--; }
  for(i=j=0; i<n; i++){
    if( z[i]=='/' ){
      if( z[i+1]=='/' ) continue;
      if( z[i+1]=='.' && i+2<n && z[i+2]=='/' ){
        i += 1;
        continue;
      }
      if( z[i+1]=='.' && i+3<n && z[i+2]=='.' && z[i+3]=='/' ){
        while( j>0 && z[j-1]!='/' ){ j--; }
        i += 3;
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
#ifdef __MINGW32__
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
      exit(1);
    }
    blob_zero(pOut);
    blob_appendf(pOut, "%//%/", zPwd, zOrigName);
  }
  blob_resize(pOut, file_simplify_name(blob_buffer(pOut), blob_size(pOut)));
}

/*
** COMMAND:  test-canonical-name
**
** Test the operation of the canonical name generator.
*/
void cmd_test_canonical_name(void){
  int i;
  Blob x;
  blob_zero(&x);
  for(i=2; i<g.argc; i++){
    file_canonical_name(g.argv[i], &x);
    printf("%s\n", blob_buffer(&x));
    blob_reset(&x);
  }
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
      exit(1);
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
  db_must_be_within_tree();
  file_canonical_name(zOrigName, &full);
  n = strlen(g.zLocalRoot);
  if( blob_size(&full)<=n || memcmp(g.zLocalRoot, blob_buffer(&full), n) ){
    blob_reset(&full);
    if( errFatal ){
      fossil_fatal("file outside of checkout tree: %s", zOrigName);
    }
    return 0;
  }
  blob_zero(pOut);
  blob_append(pOut, blob_buffer(&full)+n, blob_size(&full)-n);
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
