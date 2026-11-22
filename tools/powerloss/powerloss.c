/*
** Copyright (c) 2026 Dan Shearer
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
**
*******************************************************************************
**
** This file is an LD_PRELOAD library for SQLite programs that simulates a
** power loss or killed process while writing to the database.
**
** Environment variables:
**
**    STOP_DIR    The working directory.  The program's files are under
**                STOP_DIR/run.  Without STOP_DIR no file is tracked.
**    STOP_AT     Which stop point number to stop at, counting from 1.
**                With 0, or without STOP_AT, the program runs to the
**                end, and just logs the stop points.
**    STOP_MODE   "kill" for process kill mode, anything else is power mode.
**
** Tracked files under STOP_DIR/run contain ".fossil" or ".fslckout".
** Each tracked file is copied to STOP_DIR/shadow when a run starts,
** and again at each fsync() or fdatasync() stop point.
**
** In kill mode each pwrite() and write() is also a stop point,
** plus each fwrite() to a tracked file opened with fopen().
**
** Every process of a run counts stop points and stores them in
** STOP_DIR/shadow/powerloss-state.
**
** In power mode, at stop point STOP_AT each tracked file is restored, some
** housekeeping is done and then it exits status 77.  In kill mode it exits
** with status 77 without changing any files, and the other processes
** continue.
**
** A process locks powerloss-state around each change to a tracked file.
** A process that locks powerloss-state after a power-mode stop exits
** with status 77.
*/

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAXFD 4096       /* Size of azFdPath[].  Higher fds are not tracked */
#define MAXFILE 64       /* Size of aFile[] */
#define MAXPATH 4096     /* Size of a buffer for a file name */
#define STOP_STATUS 77   /* Exit status of a process that the library stops */

static char *azFdPath[MAXFD];   /* Path of the tracked file open as each fd */
static char zDir[MAXPATH];      /* STOP_DIR/run, or "" without STOP_DIR */
static char zShadow[MAXPATH];   /* STOP_DIR/shadow */
static char zLog[MAXPATH];      /* STOP_DIR/log */
static char zState[MAXPATH+20]; /* STOP_DIR/shadow/powerloss-state */
static long nStopAt;            /* STOP_AT, or 0 */
static int bKillMode;           /* True in kill mode, false in power mode */
static char aCopyBuf[65536];    /* Buffer for copy_file() and copy_fd() */

/*
** The tracked files that the program has open as a FILE for writing.
** An unused entry has pFile==0.
*/
static struct {
  FILE *pFile;                  /* The open FILE */
  char *zPath;                  /* Its path, from strdup() */
} aFile[MAXFILE];

/*
** The C library's versions of the functions that this library replaces.
** powerloss_init() sets them.  fopen(), fopen64(), fclose() and fwrite()
** also look up their own on first use, in case the program calls one of
** them before powerloss_init() runs.
*/
static int (*real_open)(const char*, int, ...);
static int (*real_open64)(const char*, int, ...);
static int (*real_close)(int);
static int (*real_fsync)(int);
static int (*real_fdatasync)(int);
static int (*real_unlink)(const char*);
static ssize_t (*real_pwrite)(int, const void*, size_t, off_t);
static ssize_t (*real_pwrite64)(int, const void*, size_t, off64_t);
static ssize_t (*real_write)(int, const void*, size_t);
static FILE *(*real_fopen)(const char*, const char*);
static FILE *(*real_fopen64)(const char*, const char*);
static int (*real_fclose)(FILE*);
static size_t (*real_fwrite)(const void*, size_t, size_t, FILE*);

/*
** Returns true if the file name ends with "-shm".
*/
static int is_shm(const char *z){
  size_t n = strlen(z);
  return n>4 && strcmp(z+n-4, "-shm")==0;
}

/*
** This is the procedure to change for testing your specific SQLite program.
**
** For Fossil, we return true if z is under STOP_DIR/run and the last component
** of its name contains ".fossil" or ".fslckout".  That includes the
** journal, WAL and "-shm" files of a database.
**
*/
static int is_db(const char *z){
  const char *zBase = strrchr(z, '/');
  size_t nDir = strlen(zDir);
  zBase = zBase ? zBase+1 : z;
  return nDir && strncmp(z, zDir, nDir)==0 && z[nDir]=='/'
      && (strstr(zBase, ".fossil") || strstr(zBase, ".fslckout"));
}

/*
** Returns true if z is a tracked file.
*/
static int is_tracked(const char *z){
  return is_db(z) && !is_shm(z);
}

/*
** Returns the absolute path of z with its symbolic links resolved, which
** is the path that SQLite opens.  Writes the path to zBuf, which has room
** for MAXPATH bytes.  Returns z itself without STOP_DIR, and for a file
** that does not exist.
*/
static const char *full_path(const char *z, char *zBuf){
  return zDir[0] && realpath(z, zBuf) ? zBuf : z;
}

/*
** Returns the path of the tracked file that is open as fd, or 0 if fd is
** not a tracked file.
*/
static const char *fd_path(int fd){
  return fd>=0 && fd<MAXFD ? azFdPath[fd] : 0;
}

/*
** Returns the index of the entry of aFile[] for f, or -1 if it has none.
** With f==0, the entry is an unused one, and -1 means that aFile[] is
** full.
*/
static int file_index(FILE *f){
  int i;
  for(i=0; i<MAXFILE && aFile[i].pFile!=f; i++){}
  return i<MAXFILE ? i : -1;
}

/*
** Writes the name of the shadow copy of the tracked file z to zOut.  The
** name is z's path under STOP_DIR/run with each "/" replaced by "%", so
** that the ".fslckout" files of two check-outs have separate copies.
*/
static void shadow_name(const char *z, char *zOut, size_t nOut){
  char *zSlash;
  size_t nShadow = strlen(zShadow);
  snprintf(zOut, nOut, "%s/%s", zShadow, z+strlen(zDir)+1);
  for(zSlash=strchr(zOut+nShadow+1, '/'); zSlash; zSlash=strchr(zSlash, '/')){
    *zSlash = '%';
  }
}

/*
** Copies the file zFrom to zTo, replacing zTo.  Leaves zTo as it was if
** zFrom cannot be opened, and stops at the first write that fails.
*/
static void copy_file(const char *zFrom, const char *zTo){
  ssize_t got;
  int out;
  int in = real_open(zFrom, O_RDONLY);
  if( in<0 ) return;
  out = real_open(zTo, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if( out>=0 ){
    while( (got = read(in, aCopyBuf, sizeof(aCopyBuf)))>0 ){
      if( real_write(out, aCopyBuf, got)!=got ) break;
    }
    real_close(out);
  }
  real_close(in);
}

/*
** Copies the file that is open as fd to zTo, reading through fd.  This is
** only for a file that the program has open already.
**
** Returns 0 if fd cannot be read, otherwise 1.
*/
static int copy_fd(int fd, const char *zTo){
  ssize_t got;
  off64_t off = 0;
  int out;
  got = pread64(fd, aCopyBuf, sizeof(aCopyBuf), 0);
  if( got<0 ) return 0;
  out = real_open(zTo, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if( out<0 ) return 1;
  while( got>0 ){
    if( real_write(out, aCopyBuf, got)!=got ) break;
    off += got;
    got = pread64(fd, aCopyBuf, sizeof(aCopyBuf), off);
  }
  real_close(out);
  return 1;
}

/*
** Visits every file under the directory zD and its subdirectories.
**
** If bRestore is false, copies each tracked file to its shadow copy.
**
** If bRestore is true, restores each tracked file from its shadow copy,
** deletes each tracked file that has no shadow copy, and deletes each
** "-shm" file of a database.  This leaves the files as they were at their
** last sync.
*/
static void shadow_walk(const char *zD, int bRestore){
  DIR *d = opendir(zD);
  struct dirent *e;
  char zPath[MAXPATH];
  char zCopy[MAXPATH];
  struct stat st;
  if( d==0 ) return;
  while( (e = readdir(d))!=0 ){
    if( strcmp(e->d_name, ".")==0 || strcmp(e->d_name, "..")==0 ) continue;
    snprintf(zPath, sizeof(zPath), "%s/%s", zD, e->d_name);
    if( lstat(zPath, &st)!=0 ) continue;
    if( S_ISDIR(st.st_mode) ){
      shadow_walk(zPath, bRestore);
      continue;
    }
    if( !is_db(zPath) ) continue;
    shadow_name(zPath, zCopy, sizeof(zCopy));
    if( is_shm(zPath) ){
      if( bRestore ) real_unlink(zPath);
    }else if( !bRestore ){
      copy_file(zPath, zCopy);
    }else if( access(zCopy, F_OK)==0 ){
      copy_file(zCopy, zPath);
    }else{
      real_unlink(zPath);
    }
  }
  closedir(d);
}

/*
** The content of the state file, STOP_DIR/shadow/powerloss-state, and the
** descriptor that has it locked.  The file is empty until the first
** process of the run has copied the tracked files.  After that it has the
** number of the last stop point and the stopped flag.
*/
typedef struct State State;
struct State {
  int fd;          /* The locked state file, or negative if it did not open */
  int bStarted;    /* True if the state file was not empty */
  long nPoint;     /* Number of the last stop point of the run */
  int bStopped;    /* True after a power-mode stop */
};

/*
** Writes p->nPoint and p->bStopped to the state file.  The caller has the
** lock.
*/
static void state_write(State *p){
  char z[64];
  int n = snprintf(z, sizeof(z), "%ld %d\n", p->nPoint, p->bStopped);
  if( ftruncate(p->fd, 0)==0 ) real_pwrite(p->fd, z, n, 0);
}

/*
** Opens the state file, waits for its lock, and reads the file into p.
** Exits with status STOP_STATUS if another process has stopped the run in
** power mode.
*/
static void state_lock(State *p){
  struct flock lk;
  char z[64];
  ssize_t n;
  memset(p, 0, sizeof(*p));
  p->fd = real_open(zState, O_RDWR|O_CREAT, 0644);
  if( p->fd<0 ) return;
  memset(&lk, 0, sizeof(lk));
  lk.l_type = F_WRLCK;
  lk.l_whence = SEEK_SET;
  while( fcntl(p->fd, F_SETLKW, &lk)!=0 && errno==EINTR ){}
  n = pread(p->fd, z, sizeof(z)-1, 0);
  if( n>0 ){
    z[n] = 0;
    p->bStarted = sscanf(z, "%ld %d", &p->nPoint, &p->bStopped)==2;
  }
  if( p->bStopped ) _exit(STOP_STATUS);
}

/*
** Closes the state file, which releases the lock.
*/
static void state_unlock(State *p){
  if( p->fd>=0 ) real_close(p->fd);
}

/*
** Counts a stop point for the call zOp on the tracked file zPath, and
** appends it to the log.  The caller has the lock.
**
** If this is stop point STOP_AT, the process exits with status
** STOP_STATUS.  In power mode it first sets the stopped flag in the state
** file and restores the tracked files.
*/
static void stop_point(State *p, const char *zOp, const char *zPath){
  FILE *f = fopen(zLog, "a");
  int bStop;
  p->nPoint++;
  if( f ){
    fprintf(f, "%ld %s %s %ld\n", p->nPoint, zOp, zPath+strlen(zDir),
            (long)getpid());
    fclose(f);
  }
  bStop = nStopAt>0 && p->nPoint==nStopAt;
  p->bStopped = bStop && !bKillMode;
  state_write(p);
  if( bStop ){
    if( !bKillMode ) shadow_walk(zDir, 1);
    _exit(STOP_STATUS);
  }
}

/*
** Ends the call zOp, which changed the tracked file zPath under the lock.
** The call is a stop point if bStop is true.  Releases the lock, and
** leaves errno as the call set it.
*/
static void changed(State *p, int bStop, const char *zOp, const char *zPath){
  int savedErrno = errno;
  if( bStop ) stop_point(p, zOp, zPath);
  state_unlock(p);
  errno = savedErrno;
}

/*
** Runs when the program loads the library.  Looks up the C library's
** functions and reads the environment variables.  In the first process of
** a run, which finds the state file empty, copies each tracked file to
** its shadow copy.
*/
__attribute__((constructor)) static void powerloss_init(void){
  const char *zWork;
  State s;
  char *zReal;
  real_open = dlsym(RTLD_NEXT, "open");
  real_open64 = dlsym(RTLD_NEXT, "open64");
  real_close = dlsym(RTLD_NEXT, "close");
  real_fsync = dlsym(RTLD_NEXT, "fsync");
  real_fdatasync = dlsym(RTLD_NEXT, "fdatasync");
  real_unlink = dlsym(RTLD_NEXT, "unlink");
  real_pwrite = dlsym(RTLD_NEXT, "pwrite");
  real_pwrite64 = dlsym(RTLD_NEXT, "pwrite64");
  real_write = dlsym(RTLD_NEXT, "write");
  real_fopen = dlsym(RTLD_NEXT, "fopen");
  real_fopen64 = dlsym(RTLD_NEXT, "fopen64");
  real_fclose = dlsym(RTLD_NEXT, "fclose");
  real_fwrite = dlsym(RTLD_NEXT, "fwrite");
  zWork = getenv("STOP_DIR");
  nStopAt = getenv("STOP_AT") ? atol(getenv("STOP_AT")) : 0;
  bKillMode = getenv("STOP_MODE") && strcmp(getenv("STOP_MODE"), "kill")==0;
  if( zWork==0 ) return;
  zReal = realpath(zWork, 0);
  if( zReal ) zWork = zReal;
  snprintf(zDir, sizeof(zDir), "%s/run", zWork);
  snprintf(zShadow, sizeof(zShadow), "%s/shadow", zWork);
  snprintf(zLog, sizeof(zLog), "%s/log", zWork);
  snprintf(zState, sizeof(zState), "%s/powerloss-state", zShadow);
  mkdir(zShadow, 0755);
  state_lock(&s);
  if( !s.bStarted ){
    shadow_walk(zDir, 0);
    state_write(&s);
  }
  state_unlock(&s);
}

/*
** Records the absolute path of z as the path of fd if z is a tracked
** file, and otherwise records that fd is not a tracked file.  Returns fd.
** The record of a tracked file is a copy of the path from strdup().
*/
static int opened(const char *z, int fd){
  char zAbs[MAXPATH];
  if( fd>=0 && fd<MAXFD ){
    z = full_path(z, zAbs);
    free(azFdPath[fd]);
    azFdPath[fd] = is_tracked(z) ? strdup(z) : 0;
  }
  return fd;
}

/*
** Replaces open().  Records the path of a tracked file.
*/
int open(const char *z, int flags, ...){
  mode_t m = 0;
  if( flags & O_CREAT ){
    va_list ap;
    va_start(ap, flags);
    m = (mode_t)va_arg(ap, int);
    va_end(ap);
  }
  return opened(z, real_open(z, flags, m));
}

/*
** Replaces open64().  Records the path of a tracked file.
*/
int open64(const char *z, int flags, ...){
  mode_t m = 0;
  if( flags & O_CREAT ){
    va_list ap;
    va_start(ap, flags);
    m = (mode_t)va_arg(ap, int);
    va_end(ap);
  }
  return opened(z, real_open64(z, flags, m));
}

/*
** Replaces close().  Deletes the record of a tracked file.
*/
int close(int fd){
  if( fd_path(fd) ){
    free(azFdPath[fd]);
    azFdPath[fd] = 0;
  }
  return real_close(fd);
}

/*
** Syncs fd with xSync, which is the C library's fsync() or fdatasync(),
** and returns its result.  For a tracked file, the sync runs under the
** lock, the library then copies the file to its shadow copy, and the call
** zOp is a stop point.
**
** The library reads through fd to make the shadow copy.  If fd cannot be
** read, because the program opened it for writing only, the library reads
** the file by its path.
*/
static int synced(int fd, int (*xSync)(int), const char *zOp){
  const char *z = fd_path(fd);
  State s;
  char zCopy[MAXPATH];
  int rc;
  int savedErrno;
  if( z==0 ) return xSync(fd);
  state_lock(&s);
  rc = xSync(fd);
  savedErrno = errno;
  shadow_name(z, zCopy, sizeof(zCopy));
  if( !copy_fd(fd, zCopy) ) copy_file(z, zCopy);
  stop_point(&s, zOp, z);
  state_unlock(&s);
  errno = savedErrno;
  return rc;
}

/*
** Replaces fsync().  A sync of a tracked file is a stop point.
*/
int fsync(int fd){
  return synced(fd, real_fsync, "fsync");
}

/*
** Replaces fdatasync().  A sync of a tracked file is a stop point.
*/
int fdatasync(int fd){
  return synced(fd, real_fdatasync, "fdatasync");
}

/*
** Replaces unlink().  The unlink of a tracked file runs under the lock.
** If it succeeds, the library deletes the file's shadow copy, and the
** call is a stop point.
*/
int unlink(const char *z){
  State s;
  char zCopy[MAXPATH];
  char zAbs[MAXPATH];
  const char *zFull = full_path(z, zAbs);
  int rc;
  if( !is_tracked(zFull) ) return real_unlink(z);
  state_lock(&s);
  rc = real_unlink(z);
  if( rc==0 ){
    shadow_name(zFull, zCopy, sizeof(zCopy));
    real_unlink(zCopy);
  }
  changed(&s, rc==0, "unlink", zFull);
  return rc;
}

/*
** Replaces pwrite().  A write to a tracked file runs under the lock, and
** in kill mode it is a stop point.
*/
ssize_t pwrite(int fd, const void *p, size_t n, off_t off){
  const char *z = fd_path(fd);
  State s;
  ssize_t rc;
  if( z==0 ) return real_pwrite(fd, p, n, off);
  state_lock(&s);
  rc = real_pwrite(fd, p, n, off);
  changed(&s, bKillMode, "pwrite", z);
  return rc;
}

/*
** Replaces pwrite64().  A write to a tracked file runs under the lock,
** and in kill mode it is a stop point, which the log records as "pwrite".
*/
ssize_t pwrite64(int fd, const void *p, size_t n, off64_t off){
  const char *z = fd_path(fd);
  State s;
  ssize_t rc;
  if( z==0 ) return real_pwrite64(fd, p, n, off);
  state_lock(&s);
  rc = real_pwrite64(fd, p, n, off);
  changed(&s, bKillMode, "pwrite", z);
  return rc;
}

/*
** Replaces write().  A write to a tracked file runs under the lock, and
** in kill mode it is a stop point.
*/
ssize_t write(int fd, const void *p, size_t n){
  const char *z = fd_path(fd);
  State s;
  ssize_t rc;
  if( z==0 ) return real_write(fd, p, n);
  state_lock(&s);
  rc = real_write(fd, p, n);
  changed(&s, bKillMode, "write", z);
  return rc;
}

/*
** Records the absolute path of z as the path of f in aFile[] if z is a
** tracked file and zMode opens it for writing.  The record is a copy of
** the path from strdup().  Does not record the file if aFile[] is full.
** Returns f.
*/
static FILE *fopened(const char *z, const char *zMode, FILE *f){
  char zAbs[MAXPATH];
  int i;
  if( f==0 || zDir[0]==0 || strpbrk(zMode, "wa+")==0 ) return f;
  z = full_path(z, zAbs);
  if( !is_tracked(z) ) return f;
  i = file_index(0);
  if( i>=0 ){
    aFile[i].pFile = f;
    aFile[i].zPath = strdup(z);
  }
  return f;
}

/*
** Replaces fopen().  Records a tracked file that is opened for writing.
*/
FILE *fopen(const char *z, const char *zMode){
  if( real_fopen==0 ) real_fopen = dlsym(RTLD_NEXT, "fopen");
  return fopened(z, zMode, real_fopen(z, zMode));
}

/*
** Replaces fopen64().  Records a tracked file that is opened for writing.
*/
FILE *fopen64(const char *z, const char *zMode){
  if( real_fopen64==0 ) real_fopen64 = dlsym(RTLD_NEXT, "fopen64");
  return fopened(z, zMode, real_fopen64(z, zMode));
}

/*
** Replaces fclose().  Deletes the record of f from aFile[].
*/
int fclose(FILE *f){
  int i = file_index(f);
  if( real_fclose==0 ) real_fclose = dlsym(RTLD_NEXT, "fclose");
  if( i>=0 ){
    free(aFile[i].zPath);
    aFile[i].zPath = 0;
    aFile[i].pFile = 0;
  }
  return real_fclose(f);
}

/*
** Replaces fwrite().  A write to a file in aFile[] runs under the lock,
** and in kill mode the call is a stop point.
**
** After a stop, the file has what the C library had written to it by
** then, which can be less than what the program gave to fwrite().
*/
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f){
  int i = file_index(f);
  const char *zPath = i>=0 ? aFile[i].zPath : 0;
  State s;
  size_t rc;
  if( real_fwrite==0 ) real_fwrite = dlsym(RTLD_NEXT, "fwrite");
  if( zPath==0 ) return real_fwrite(p, sz, n, f);
  state_lock(&s);
  rc = real_fwrite(p, sz, n, f);
  changed(&s, bKillMode, "fwrite", zPath);
  return rc;
}
