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
** This file contains code for miscellaneous utility routines.
*/
#include "config.h"
#include "util.h"
#if defined(USE_MMAN_H)
# include <sys/mman.h>
# include <unistd.h>
#endif
#include <math.h>

/*
** For the fossil_timer_xxx() family of functions...
*/
#ifdef _WIN32
# include <windows.h>
# include <io.h>
#else
# include <sys/time.h>
# include <sys/resource.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <unistd.h>
# include <fcntl.h>
# include <errno.h>
#endif

/*
** Returns the same as the platform's isatty() or _isatty() function.
*/
int fossil_isatty(int fd){
#ifdef _WIN32
  return _isatty(fd);
#else
  return isatty(fd);
#endif
}

/*
** Returns the same as the platform's fileno() or _fileno() function.
*/
int fossil_fileno(FILE *p){
#ifdef _WIN32
  return _fileno(p);
#else
  return fileno(p);
#endif
}

/*
** Exit.  Take care to close the database first.
*/
NORETURN void fossil_exit(int rc){
  db_close(1);
#ifndef _WIN32
  if( g.fAnyTrace ){
    fprintf(stderr, "/***** Subprocess %d exit(%d) *****/\n", getpid(), rc);
    fflush(stderr);
  }
#endif
  exit(rc);
}

/*
** Malloc and free routines that cannot fail
*/
void *fossil_malloc(size_t n){
  void *p = malloc(n==0 ? 1 : n);
  if( p==0 ) fossil_fatal("out of memory");
  return p;
}
void *fossil_malloc_zero(size_t n){
  void *p = malloc(n==0 ? 1 : n);
  if( p==0 ) fossil_fatal("out of memory");
  memset(p, 0, n);
  return p;
}
void fossil_free(void *p){
  free(p);
}
void *fossil_realloc(void *p, size_t n){
  p = realloc(p, n);
  if( p==0 ) fossil_fatal("out of memory");
  return p;
}
void fossil_secure_zero(void *p, size_t n){
  volatile unsigned char *vp = (volatile unsigned char *)p;
  size_t i;

  if( p==0 ) return;
  assert( n>0 );
  if( n==0 ) return;
  for(i=0; i<n; i++){ vp[i] ^= 0xFF; }
  for(i=0; i<n; i++){ vp[i] ^= vp[i]; }
}
void fossil_get_page_size(size_t *piPageSize){
#if defined(_WIN32)
  SYSTEM_INFO sysInfo;
  memset(&sysInfo, 0, sizeof(SYSTEM_INFO));
  GetSystemInfo(&sysInfo);
  *piPageSize = (size_t)sysInfo.dwPageSize;
#elif defined(USE_MMAN_H)
  *piPageSize = (size_t)sysconf(_SC_PAGE_SIZE);
#else
  *piPageSize = 4096; /* FIXME: What for POSIX? */
#endif
}
void *fossil_secure_alloc_page(size_t *pN){
  void *p;
  size_t pageSize = 0;

  fossil_get_page_size(&pageSize);
  assert( pageSize>0 );
  assert( pageSize%2==0 );
#if defined(_WIN32)
  p = VirtualAlloc(NULL, pageSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
  if( p==NULL ){
    fossil_fatal("VirtualAlloc failed: %lu\n", GetLastError());
  }
  if( !VirtualLock(p, pageSize) ){
    fossil_fatal("VirtualLock failed: %lu\n", GetLastError());
  }
#elif defined(USE_MMAN_H)
  p = mmap(0, pageSize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if( p==MAP_FAILED ){
    fossil_fatal("mmap failed: %d\n", errno);
  }
  if( mlock(p, pageSize) ){
    fossil_fatal("mlock failed: %d\n", errno);
  }
#else
  p = fossil_malloc(pageSize);
#endif
  fossil_secure_zero(p, pageSize);
  if( pN ) *pN = pageSize;
  return p;
}
void fossil_secure_free_page(void *p, size_t n){
  if( !p ) return;
  assert( n>0 );
  fossil_secure_zero(p, n);
#if defined(_WIN32)
  if( !VirtualUnlock(p, n) ){
    fossil_panic("VirtualUnlock failed: %lu\n", GetLastError());
  }
  if( !VirtualFree(p, 0, MEM_RELEASE) ){
    fossil_panic("VirtualFree failed: %lu\n", GetLastError());
  }
#elif defined(USE_MMAN_H)
  if( munlock(p, n) ){
    fossil_panic("munlock failed: %d\n", errno);
  }
  if( munmap(p, n) ){
    fossil_panic("munmap failed: %d\n", errno);
  }
#else
  fossil_free(p);
#endif
}

/*
** Duplicate a string.
*/
char *fossil_strndup(const char *zOrig, ssize_t len){
  char *z = 0;
  if( zOrig ){
    if( len<0 ) len = strlen(zOrig);
    z = fossil_malloc( len+1 );
    memcpy(z, zOrig, len);
    z[len] = 0;
  }
  return z;
}
char *fossil_strdup(const char *zOrig){
  return fossil_strndup(zOrig, -1);
}
char *fossil_strdup_nn(const char *zOrig){
  if( zOrig==0 ) return fossil_strndup("", 0);
  return fossil_strndup(zOrig, -1);
}

/*
** strcpy() workalike to squelch an unwarranted warning from OpenBSD.
*/
void fossil_strcpy(char *dest, const char *src){
  while( (*(dest++) = *(src++))!=0 ){}
}

/*
** Translate every upper-case character in the input string into
** its equivalent lower-case.
*/
char *fossil_strtolwr(char *zIn){
  char *zStart = zIn;
  if( zIn ){
    while( *zIn ){
      *zIn = fossil_tolower(*zIn);
      zIn++;
    }
  }
  return zStart;
}

/*
** This local variable determines the behavior of
** fossil_assert_safe_command_string():
**
**    0 (default)       fossil_panic() on an unsafe command string
**
**    1                 Print an error but continue process.  Used for
**                      testing of fossil_assert_safe_command_string().
**
**    2                 No-op.  Used to allow any arbitrary command string
**                      through fossil_system(), such as when invoking
**                      COMMAND in "fossil bisect run COMMAND".
*/
static int safeCmdStrTest = 0;

/*
** Check the input string to ensure that it is safe to pass into system().
** A string is unsafe for system() on unix if it contains any of the following:
**
**   *  Any occurrance of '$' or '`' except single-quoted or after \
**   *  Any of the following characters, unquoted:  ;|& or \n except
**      these characters are allowed as the very last character in the
**      string.
**   *  Unbalanced single or double quotes
**
** This routine is intended as a second line of defense against attack.
** It should never fail.  Dangerous shell strings should be detected and
** fixed before calling fossil_system().  This routine serves only as a
** safety net in case of bugs elsewhere in the system.
**
** If an unsafe string is seen, either abort (default) or print
** a warning message (if safeCmdStrTest is true).
*/
static void fossil_assert_safe_command_string(const char *z){
  int unsafe = 0;
#ifndef _WIN32
  /* Unix */
  int inQuote = 0;
  int i, c;
  for(i=0; !unsafe && (c = z[i])!=0; i++){
    switch( c ){
      case '$':
      case '`': {
        if( inQuote!='\'' ) unsafe = i+1;
        break;
      }
      case ';':
      case '|':
      case '&':
      case '\n': {
        if( inQuote!='\'' && z[i+1]!=0 ) unsafe = i+1;
        break;
      }
      case '"':
      case '\'': {
        if( inQuote==0 ){
          inQuote = c;
        }else if( inQuote==c ){
          inQuote = 0;
        }
        break;
      }
      case '\\': {
        if( z[i+1]==0 ){
          unsafe = i+1;
        }else if( inQuote!='\'' ){
          i++;
        }
        break;
      }
    }
  }
  if( inQuote ) unsafe = i;
#else
  /* Windows */
  int i, c;
  int inQuote = 0;
  for(i=0; !unsafe && (c = z[i])!=0; i++){
    switch( c ){
      case '>':
      case '<':
      case '|':
      case '&':
      case '\n': {
        if( inQuote==0 && z[i+1]!=0 ) unsafe = i+1;
        break;
      }
      case '"': {
        if( inQuote==c ){
          inQuote = 0;
        }else{
          inQuote = c;
        }
        break;
      }
      case '^': {
        if( !inQuote && z[i+1]!=0 ){
          i++;
        }
        break;
      }
    }
  }
  if( inQuote ) unsafe = i;
#endif
  if( unsafe && safeCmdStrTest<2 ){
    char *zMsg = mprintf("Unsafe command string: %s\n%*shere ----^",
                   z, unsafe+13, "");
    if( safeCmdStrTest ){
      fossil_print("%z\n", zMsg);
      fossil_free(zMsg);
    }else{
      fossil_panic("%s", zMsg);
    }
  }
}

/*
** This function implements a cross-platform "system()" interface.
*/
int fossil_system(const char *zOrigCmd){
  int rc;
#if defined(_WIN32)
  /* On windows, we have to put double-quotes around the entire command.
  ** Who knows why - this is just the way windows works.
  */
  char *zNewCmd = mprintf("\"%s\"", zOrigCmd);
  wchar_t *zUnicode = fossil_utf8_to_unicode(zNewCmd);
  if( g.fSystemTrace ) {
    fossil_trace("SYSTEM: %s\n", zNewCmd);
  }
  fossil_assert_safe_command_string(zOrigCmd);
  rc = _wsystem(zUnicode);
  fossil_unicode_free(zUnicode);
  free(zNewCmd);
#else
  /* On unix, evaluate the command directly.
  */
  if( g.fSystemTrace ) fprintf(stderr, "SYSTEM: %s\n", zOrigCmd);
  fossil_assert_safe_command_string(zOrigCmd);

  /* Unix systems should never shell-out while processing an HTTP request,
  ** either via CGI, SCGI, or direct HTTP.  The following assert verifies
  ** this.  And the following assert proves that Fossil is not vulnerable
  ** to the ShellShock or BashDoor bug.
  */
  assert( g.cgiOutput==0 );

  /* The regular system() call works to get a shell on unix */
  fossil_limit_memory(0);
  rc = system(zOrigCmd);
  fossil_limit_memory(1);
#endif
  return rc;
}

/*
** Like "fossil_system()" but does not check the command-string for
** potential security problems.
*/
int fossil_unsafe_system(const char *zOrigCmd){
  int rc;
  safeCmdStrTest = 2;
  rc = fossil_system(zOrigCmd);
  safeCmdStrTest = 0;
  return rc;
}

/*
** COMMAND: test-fossil-system
**
** Read lines of input and send them to fossil_system() for evaluation.
** Use this command to verify that fossil_system() will not run "unsafe"
** commands.
*/
void test_fossil_system_cmd(void){
  char zLine[10000];
  safeCmdStrTest = 1;
  while(1){
    size_t n;
    int rc;
    printf("system-test> ");
    fflush(stdout);
    if( !fgets(zLine, sizeof(zLine), stdin) ) break;
    n = strlen(zLine);
    while( n>0 && fossil_isspace(zLine[n-1]) ) n--;
    zLine[n] = 0;
    printf("cmd: [%s]\n", zLine);
    fflush(stdout);
    rc = fossil_system(zLine);
    printf("result: %d\n", rc);
  }
}

/*
** Like strcmp() except that it accepts NULL pointers.  NULL sorts before
** all non-NULL string pointers.  Also, this strcmp() is a binary comparison
** that does not consider locale.
*/
int fossil_strcmp(const char *zA, const char *zB){
  if( zA==0 ){
    if( zB==0 ) return 0;
    return -1;
  }else if( zB==0 ){
    return +1;
  }else{
    return strcmp(zA,zB);
  }
}
int fossil_strncmp(const char *zA, const char *zB, int nByte){
  if( zA==0 ){
    if( zB==0 ) return 0;
    return -1;
  }else if( zB==0 ){
    return +1;
  }else if( nByte>0 ){
    int a, b;
    do{
      a = *zA++;
      b = *zB++;
    }while( a==b && a!=0 && (--nByte)>0 );
    return ((unsigned char)a) - (unsigned char)b;
  }else{
    return 0;
  }
}

/*
** Case insensitive string comparison.
*/
int fossil_strnicmp(const char *zA, const char *zB, int nByte){
  if( zA==0 ){
    if( zB==0 ) return 0;
    return -1;
  }else if( zB==0 ){
    return +1;
  }
  if( nByte<0 ) nByte = strlen(zB);
  return sqlite3_strnicmp(zA, zB, nByte);
}
int fossil_stricmp(const char *zA, const char *zB){
  int nByte;
  int rc;
  if( zA==0 ){
    if( zB==0 ) return 0;
    return -1;
  }else if( zB==0 ){
    return +1;
  }
  nByte = strlen(zB);
  rc = sqlite3_strnicmp(zA, zB, nByte);
  if( rc==0 && zA[nByte] ) rc = 1;
  return rc;
}

/*
** Get user and kernel times in microseconds.
*/
void fossil_cpu_times(sqlite3_uint64 *piUser, sqlite3_uint64 *piKernel){
#ifdef _WIN32
  FILETIME not_used;
  FILETIME kernel_time;
  FILETIME user_time;
  GetProcessTimes(GetCurrentProcess(), &not_used, &not_used,
                  &kernel_time, &user_time);
  if( piUser ){
     *piUser = ((((sqlite3_uint64)user_time.dwHighDateTime)<<32) +
                         (sqlite3_uint64)user_time.dwLowDateTime + 5)/10;
  }
  if( piKernel ){
     *piKernel = ((((sqlite3_uint64)kernel_time.dwHighDateTime)<<32) +
                         (sqlite3_uint64)kernel_time.dwLowDateTime + 5)/10;
  }
#else
  struct rusage s;
  getrusage(RUSAGE_SELF, &s);
  if( piUser ){
    *piUser = ((sqlite3_uint64)s.ru_utime.tv_sec)*1000000 + s.ru_utime.tv_usec;
  }
  if( piKernel ){
    *piKernel =
              ((sqlite3_uint64)s.ru_stime.tv_sec)*1000000 + s.ru_stime.tv_usec;
  }
#endif
}

/*
** Return the resident set size for this process
*/
sqlite3_uint64 fossil_rss(void){
#ifdef _WIN32
  return 0;
#else
  struct rusage s;
  getrusage(RUSAGE_SELF, &s);
  return s.ru_maxrss*1024;
#endif
}


/*
** Internal helper type for fossil_timer_xxx().
 */
enum FossilTimerEnum {
  FOSSIL_TIMER_COUNT = 10 /* Number of timers we can track. */
};
static struct FossilTimer {
  sqlite3_uint64 u; /* "User" CPU times */
  sqlite3_uint64 s; /* "System" CPU times */
  int id; /* positive if allocated, else 0. */
} fossilTimerList[FOSSIL_TIMER_COUNT] = {{0,0,0}};

/*
** Stores the current CPU times into the shared timer list
** and returns that timer's internal ID. Pass that ID to
** fossil_timer_fetch() to get the elapsed time for that
** timer.
**
** The system has a fixed number of timers, and they can be
** "deallocated" by passing this function's return value to
** fossil_timer_stop() Adjust FOSSIL_TIMER_COUNT to set the number of
** available timers.
**
** Returns 0 on error (no more timers available), with 1+ being valid
** timer IDs.
*/
int fossil_timer_start(){
  int i;
  for( i = 0; i < FOSSIL_TIMER_COUNT; ++i ){
    struct FossilTimer * ft = &fossilTimerList[i];
    if(ft->id) continue;
    ft->id = i+1;
    fossil_cpu_times( &ft->u, &ft->s );
    break;
  }
  return (i<FOSSIL_TIMER_COUNT) ? i+1 : 0;
}

/*
** Returns the difference in CPU times in microseconds since
** fossil_timer_start() was called and returned the given timer ID (or
** since it was last reset). Returns 0 if timerId is out of range.
*/
sqlite3_uint64 fossil_timer_fetch(int timerId){
  if( timerId>0 && timerId<=FOSSIL_TIMER_COUNT ){
    struct FossilTimer * start = &fossilTimerList[timerId-1];
    if( !start->id ){
      fossil_panic("Invalid call to fetch a non-allocated "
                   "timer (#%d)", timerId);
      /*NOTREACHED*/
    }else{
      sqlite3_uint64 eu = 0, es = 0;
      fossil_cpu_times( &eu, &es );
      return (eu - start->u) + (es - start->s);
    }
  }
  return 0;
}

/*
** Resets the timer associated with the given ID, as obtained via
** fossil_timer_start(), to the current CPU time values.
*/
sqlite3_uint64 fossil_timer_reset(int timerId){
  if( timerId>0 && timerId<=FOSSIL_TIMER_COUNT ){
    struct FossilTimer * start = &fossilTimerList[timerId-1];
    if( !start->id ){
      fossil_panic("Invalid call to reset a non-allocated "
                   "timer (#%d)", timerId);
      /*NOTREACHED*/
    }else{
      sqlite3_uint64 const rc = fossil_timer_fetch(timerId);
      fossil_cpu_times( &start->u, &start->s );
      return rc;
    }
  }
  return 0;
}

/**
   "Deallocates" the fossil timer identified by the given timer ID.
   returns the difference (in uSec) between the last time that timer
   was started or reset. Returns 0 if timerId is out of range (but
   note that, due to system-level precision restrictions, this
   function might return 0 on success, too!). It is not legal to
   re-use the passed-in timerId after calling this until/unless it is
   re-initialized using fossil_timer_start() (NOT
   fossil_timer_reset()).
*/
sqlite3_uint64 fossil_timer_stop(int timerId){
  if(timerId<1 || timerId>FOSSIL_TIMER_COUNT){
    return 0;
  }else{
    sqlite3_uint64 const rc = fossil_timer_fetch(timerId);
    struct FossilTimer * t = &fossilTimerList[timerId-1];
    t->id = 0;
    t->u = t->s = 0U;
    return rc;
  }
}

/*
** Returns true (non-0) if the given timer ID (as returned from
** fossil_timer_start() is currently active.
*/
int fossil_timer_is_active( int timerId ){
  if(timerId<1 || timerId>FOSSIL_TIMER_COUNT){
    return 0;
  }else{
    const int rc = fossilTimerList[timerId-1].id;
    assert(!rc || (rc == timerId));
    return fossilTimerList[timerId-1].id;
  }
}

/*
** Return TRUE if fd is a valid open file descriptor.  This only
** works on unix.  The function always returns true on Windows.
*/
int is_valid_fd(int fd){
#ifdef _WIN32
  return 1;
#else
  return fcntl(fd, F_GETFL)!=(-1) || errno!=EBADF;
#endif
}

/*
** Returns TRUE if zSym is exactly HNAME_LEN_SHA1 or HNAME_LEN_K256
** bytes long and contains only lower-case ASCII hexadecimal values.
*/
int fossil_is_artifact_hash(const char *zSym){
  int sz = zSym ? (int)strlen(zSym) : 0;
  return (HNAME_LEN_SHA1==sz || HNAME_LEN_K256==sz) && validate16(zSym, sz);
}

/*
** Return true if the input string is NULL or all whitespace.
** Return false if the input string contains text.
*/
int fossil_all_whitespace(const char *z){
  if( z==0 ) return 1;
  while( fossil_isspace(z[0]) ){ z++; }
  return z[0]==0;
}

/*
** Return the name of the users preferred text editor.  Return NULL if
** not found.
**
** Search algorithm:
** (1) The value of the --editor command-line argument
** (2) The local "editor" setting
** (3) The global "editor" setting
** (4) The VISUAL environment variable
** (5) The EDITOR environment variable
** (6) Any of the following programs that are available:
**        notepad, nano, pico, jove, edit, vi, vim, ed,
**
** The search only occurs once, the first time this routine is called.
** Second and subsequent invocations always return the same value.
*/
const char *fossil_text_editor(void){
  static const char *zEditor = 0;
  const char *azStdEd[] = {
    "notepad", "nano", "pico", "jove", "edit", "vi", "vim", "ed"
  };
  int i = 0;
  if( zEditor==0 ){
    zEditor = find_option("editor",0,1);
  }
  if( zEditor==0 ){
    zEditor = db_get("editor", 0);
  }
  if( zEditor==0 ){
    zEditor = fossil_getenv("VISUAL");
  }
  if( zEditor==0 ){
    zEditor = fossil_getenv("EDITOR");
  }
  while( zEditor==0 && i<count(azStdEd) ){
    if( fossil_app_on_path(azStdEd[i],0) ){
      zEditor = azStdEd[i];
    }else{
      i++;
    }
  }
  if( zEditor && is_false(zEditor) ) zEditor = 0;
  return zEditor;
}

/*
** Construct a temporary filename.
**
** The returned string is obtained from sqlite3_malloc() and must be
** freed by the caller.
**
** See also:  file_tempname() and file_time_timename();
*/
char *fossil_temp_filename(void){
  char *zTFile = 0;
  const char *zDir;
  char cDirSep;
  char zSep[2];
  size_t nDir;
  u64 r[2];
#ifdef _WIN32
  char *zTempDirA = NULL;
  WCHAR zTempDirW[MAX_PATH+1];
  const DWORD dwTempSizeW = sizeof(zTempDirW)/sizeof(zTempDirW[0]);
  DWORD dwTempLenW;
#else
  int i;
  static const char *azTmp[] = {"/var/tmp","/usr/tmp","/tmp"};
#endif
  if( g.db ){
    sqlite3_file_control(g.db, 0, SQLITE_FCNTL_TEMPFILENAME, (void*)&zTFile);
    if( zTFile ) return zTFile;
  }
  sqlite3_randomness(sizeof(r), &r);
#if _WIN32
  cDirSep = '\\';
  dwTempLenW = GetTempPathW(dwTempSizeW, zTempDirW);
  if( dwTempLenW>0 && dwTempLenW<dwTempSizeW
      && ( zTempDirA = fossil_path_to_utf8(zTempDirW) )){
    zDir = zTempDirA;
  }else{
    zDir = fossil_getenv("LOCALAPPDATA");
    if( zDir==0 ) zDir = ".";
  }
#else
  for(i=0; i<(int)(sizeof(azTmp)/sizeof(azTmp[0])); i++){
    struct stat buf;
    zDir = azTmp[i];
    if( stat(zDir,&buf)==0 && S_ISDIR(buf.st_mode) && access(zDir,03)==0 ){
      break;
    }
  }
  if( i>=(int)(sizeof(azTmp)/sizeof(azTmp[0])) ) zDir = ".";
  cDirSep = '/';
#endif
  nDir = strlen(zDir);
  zSep[1] = 0;
  zSep[0] = (nDir && zDir[nDir-1]==cDirSep) ? 0 : cDirSep;
  zTFile = sqlite3_mprintf("%s%sfossil%016llx%016llx", zDir,zSep,r[0],r[1]);
#ifdef _WIN32
  if( zTempDirA ) fossil_path_free(zTempDirA);
#endif
  return zTFile;
}

/*
** Turn memory limits for stack and heap on and off.  The argument
** is true to turn memory limits on and false to turn them off.
**
** Memory limits should be enabled at startup, but then turned off
** before starting subprocesses.
*/
void fossil_limit_memory(int onOff){
#if defined(__unix__)
  static sqlite3_int64 origHeap = 10000000000LL;  /* 10GB */
  static sqlite3_int64 origStack =    8000000  ;  /*  8MB */
  struct rlimit x;

#if defined(RLIMIT_DATA)
  getrlimit(RLIMIT_DATA, &x);
  if( onOff ){
    origHeap = x.rlim_cur;
    if( sizeof(void*)<8 || sizeof(x.rlim_cur)<8 ){
      x.rlim_cur =  1000000000  ;  /* 1GB on 32-bit systems */
    }else{
      x.rlim_cur = 10000000000LL;  /* 10GB on 64-bit systems */
    }
  }else{
    x.rlim_cur = origHeap;
  }
  setrlimit(RLIMIT_DATA, &x);
#endif /* defined(RLIMIT_DATA) */
#if defined(RLIMIT_STACK)
  getrlimit(RLIMIT_STACK, &x);
  if( onOff ){
    origStack = x.rlim_cur;
    x.rlim_cur =  8000000;  /* 8MB */
  }else{
    x.rlim_cur = origStack;
  }
  setrlimit(RLIMIT_STACK, &x);
#endif /* defined(RLIMIT_STACK) */
#endif /* defined(__unix__) */
}

#if defined(HAVE_PLEDGE)
/*
** Interface to pledge() on OpenBSD 5.9 and later.
**
** On platforms that have pledge(), use this routine.
** On all other platforms, this routine does not exist, but instead
** a macro defined in config.h is used to provide a no-op.
*/
void fossil_pledge(const char *promises){
  if( pledge(promises, 0) ){
    fossil_panic("pledge(\"%s\",NULL) fails with errno=%d",
      promises, (int)errno);
  }
}
#endif /* defined(HAVE_PLEDGE) */

/*
** Construct a random password and return it as a string.  N is the
** recommended number of characters for the password.
**
** Space to hold the returned string is obtained from fossil_malloc()
** and should be freed by the caller.
*/
char *fossil_random_password(int N){
  char zSrc[60];
  int nSrc;
  int i;
  char z[60];

  /* Source characters for the password.  Omit characters like "0", "O",
  ** "1" and "I"  that might be easily confused */
  static const char zAlphabet[] =
           /*  0         1         2         3         4         5       */
           /*   123456789 123456789 123456789 123456789 123456789 123456 */
              "23456789abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ";

  if( N<8 ) N = 8;
  nSrc = sizeof(zAlphabet) - 1;
  if( N>nSrc ) N = nSrc;
  memcpy(zSrc, zAlphabet, nSrc);

  for(i=0; i<N; i++){
    unsigned r;
    sqlite3_randomness(sizeof(r), &r);
    r %= nSrc;
    z[i] = zSrc[r];
    zSrc[r] = zSrc[--nSrc];
  }
  z[i] = 0;
  return fossil_strdup(z);
}

/*
** COMMAND: test-random-password
**
** Usage: %fossil test-random-password [N] [--entropy]
**
** Generate a random password string of approximately N characters in length.
** If N is omitted, use 12.  Values of N less than 8 are changed to 8
** and greater than 57 and changed to 57.
**
** If the --entropy flag is included, the number of bits of entropy in
** the password is show as well.
*/
void test_random_password(void){
  int N = 12;
  int showEntropy = 0;
  int i;
  char *zPassword;
  for(i=2; i<g.argc; i++){
    const char *z = g.argv[i];
    if( z[0]=='-' && z[1]=='-' ) z++;
    if( strcmp(z,"-entropy")==0 ){
      showEntropy = 1;
    }else if( fossil_isdigit(z[0]) ){
      N = atoi(z);
      if( N<8 ) N = 8;
      if( N>57 ) N = 57;
    }else{
      usage("[N] [--entropy]");
    }
  }
  zPassword = fossil_random_password(N);
  if( showEntropy ){
    double et = 57.0;
    for(i=1; i<N; i++) et *= 57-i;
    fossil_print("%s (%d bits of entropy)\n", zPassword,
                 (int)(log(et)/log(2.0)));
  }else{
    fossil_print("%s\n", zPassword);
  }
  fossil_free(zPassword);
}

/*
** Return the number of decimal digits in a nonnegative integer.  This is useful
** when formatting text.
*/
int fossil_num_digits(int n){
  return n<      10 ? 1 : n<      100 ? 2 : n<      1000 ? 3
       : n<   10000 ? 4 : n<   100000 ? 5 : n<   1000000 ? 6
       : n<10000000 ? 7 : n<100000000 ? 8 : n<1000000000 ? 9 : 10;
}

/*
** Search for an executable on the PATH environment variable.
** Return true (1) if found and false (0) if not found.
**
** Print the full pathname of the first location if ePrint==1.  Print
** all pathnames for the executable if ePrint==2 or more.
*/
int fossil_app_on_path(const char *zBinary, int ePrint){
  const char *zPath = fossil_getenv("PATH");
  char *zFull;
  int i;
  int bExists;
  int bFound = 0;
  while( zPath && zPath[0] ){
#ifdef _WIN32
    while( zPath[0]==';' ) zPath++;
    for(i=0; zPath[i] && zPath[i]!=';'; i++){}
    zFull = mprintf("%.*s\\%s.exe", i, zPath, zBinary);
    bExists = file_access(zFull, R_OK);
    if( bExists!=0 ){
      fossil_free(zFull);
      zFull = mprintf("%.*s\\%s.bat", i, zPath, zBinary);
      bExists = file_access(zFull, R_OK);
    }
#else
    while( zPath[0]==':' ) zPath++;
    for(i=0; zPath[i] && zPath[i]!=':'; i++){}
    zFull = mprintf("%.*s/%s", i, zPath, zBinary);
    bExists = file_access(zFull, X_OK);
#endif
    if( bExists==0 && ePrint ){
      fossil_print("%s\n", zFull);
    }
    fossil_free(zFull);
    if( bExists==0 ){
      if( ePrint<2 ) return 1;
      bFound = 1;
    }
    zPath += i;
  }
  return bFound;
}

/*
** COMMAND: which*
**
** Usage: fossil which [-a] NAME ...
**
** For each NAME mentioned as an argument, print the first location on the
** on PATH of the executable with that name.  Or, show all locations on PATH
** for each argument if the -a option is used.
**
** This command is a substitute for the unix "which" command, which is not
** always available, especially on Windows.
*/
void test_app_on_path(void){
  int i;
  int ePrint = 1;
  if( find_option("all","a",0)!=0 ) ePrint = 2;
  verify_all_options();
  for(i=2; i<g.argc; i++){
    if( fossil_app_on_path(g.argv[i], ePrint)==0 ){
      fossil_print("NOT FOUND: %s\n", g.argv[i]);
    }
  }
}

/*
** Return the name of a command that will launch a web-browser.
*/
const char *fossil_web_browser(void){
  const char *zBrowser = 0;
#if defined(_WIN32)
  zBrowser = db_get("web-browser", "start \"\"");
#elif defined(__DARWIN__) || defined(__APPLE__) || defined(__HAIKU__)
  zBrowser = db_get("web-browser", "open");
#else
  zBrowser = db_get("web-browser", 0);
  if( zBrowser==0 ){
    static const char *const azBrowserProg[] =
        { "xdg-open", "gnome-open", "firefox", "google-chrome" };
    int i;
    zBrowser = "echo";
    for(i=0; i<count(azBrowserProg); i++){
      if( fossil_app_on_path(azBrowserProg[i],0) ){
        zBrowser = azBrowserProg[i];
        break;
      }
    }
    zBrowser = mprintf("%s 2>/dev/null", zBrowser);
  }
#endif
  return zBrowser;
}

/*
** On non-Windows systems, calls nice(2) with the given level. Errors
** are ignored. On Windows this is a no-op.
*/
void fossil_nice(int level){
#ifndef _WIN32
  /* dummy if() condition to avoid nuisance warning about unused result on
     certain compiler */
  if( nice(level) ){ /*ignored*/ }
#else
  (void)level;
#endif
}

/*
** Calls fossil_nice() with a default level.
*/
void fossil_nice_default(void){
  fossil_nice(19);
}
