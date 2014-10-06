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

/*
** For the fossil_timer_xxx() family of functions...
*/
#ifdef _WIN32
# include <windows.h>
#else
# include <sys/time.h>
# include <sys/resource.h>
# include <unistd.h>
# include <fcntl.h>
# include <errno.h>
#endif


/*
** Exit.  Take care to close the database first.
*/
NORETURN void fossil_exit(int rc){
  db_close(1);
  exit(rc);
}

/*
** Malloc and free routines that cannot fail
*/
void *fossil_malloc(size_t n){
  void *p = malloc(n==0 ? 1 : n);
  if( p==0 ) fossil_panic("out of memory");
  return p;
}
void fossil_free(void *p){
  free(p);
}
void *fossil_realloc(void *p, size_t n){
  p = realloc(p, n);
  if( p==0 ) fossil_panic("out of memory");
  return p;
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
  rc = _wsystem(zUnicode);
  fossil_unicode_free(zUnicode);
  free(zNewCmd);
#else
  /* On unix, evaluate the command directly.
  */
  if( g.fSystemTrace ) fprintf(stderr, "SYSTEM: %s\n", zOrigCmd);

  /* Unix systems should never shell-out while processing an HTTP request,
  ** either via CGI, SCGI, or direct HTTP.  The following assert verifies
  ** this.  And the following assert proves that Fossil is not vulnerable
  ** to the ShellShock or BashDoor bug.
  */
  assert( g.cgiOutput==0 );

  /* The regular system() call works to get a shell on unix */
  rc = system(zOrigCmd);
#endif
  return rc;
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
    int a, b;
    do{
      a = *zA++;
      b = *zB++;
    }while( a==b && a!=0 );
    return ((unsigned char)a) - (unsigned char)b;
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
  static char once = 0;
  if(!once){
    once = 1;
    memset(&fossilTimerList, 0,
           count(fossilTimerList));
  }
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
      fossil_fatal("Invalid call to fetch a non-allocated "
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
      fossil_fatal("Invalid call to reset a non-allocated "
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
** Returns TRUE if zSym is exactly UUID_SIZE bytes long and contains
** only lower-case ASCII hexadecimal values.
*/
int fossil_is_uuid(const char *zSym){
  return zSym
    && (UUID_SIZE==strlen(zSym))
    && validate16(zSym, UUID_SIZE);
}
