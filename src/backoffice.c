/*
** Copyright (c) 2018 D. Richard Hipp
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
** This file contains code used to manage a background processes that
** occur after user interaction with the repository.  Examples of
** backoffice processing includes:
**
**    *  Sending alerts and notifications
**    *  Processing the email queue
**    *  Handling post-receive hooks
**    *  Automatically syncing to peer repositories
**
** Backoffice processing is automatically started whenever there are
** changes to the repository.  The backoffice process dies off after
** a period of inactivity.
**
** Steps are taken to ensure that only a single backoffice process is
** running at a time.  Otherwise, there could be race conditions that
** cause adverse effects such as multiple alerts for the same changes.
**
** At the same time, we do not want a backoffice process to run forever.
** Backoffice processes should die off after doing whatever work they need
** to do.  In this way, we avoid having lots of idle processes in the
** process table, doing nothing on rarely accessed repositories, and
** if the Fossil binary is updated on a system, the backoffice processes
** will restart using the new binary automatically.
**
** At any point in time there should be at most two backoffice processes.
** There is a main process that is doing the actual work, and there is
** a second stand-by process that is waiting for the main process to finish
** and that will become the main process after a delay.
**
** After any successful web page reply, the backoffice_check_if_needed()
** routine is called.  That routine checks to see if both one or both of
** the backoffice processes are already running.  That routine remembers the
** status in a global variable.
**
** Later, after the repository database is closed, the
** backoffice_run_if_needed() routine is called.  If the prior call
** to backoffice_check_if_needed() indicated that backoffice processing
** might be required, the run_if_needed() attempts to kick off a backoffice
** process.
**
** All work performed by the backoffice is in the backoffice_work()
** routine.
*/
#if defined(_WIN32)
# if defined(_WIN32_WINNT)
#  undef _WIN32_WINNT
# endif
# define _WIN32_WINNT 0x501
#endif
#include "config.h"
#include "backoffice.h"
#include <time.h>
#if defined(_WIN32)
# include <windows.h>
# include <stdio.h>
# include <process.h>
# if defined(__MINGW32__)
#  include <wchar.h>
# endif
# define GETPID (int)GetCurrentProcessId
#else
# include <unistd.h>
# include <sys/types.h>
# include <signal.h>
# include <errno.h>
# include <sys/time.h>
# include <sys/resource.h>
# include <fcntl.h>
# define GETPID getpid
#endif
#include <time.h>

/*
** The BKOFCE_LEASE_TIME is the amount of time for which a single backoffice
** processing run is valid.  Each backoffice run monopolizes the lease for
** at least this amount of time.  Hopefully all backoffice processing is
** finished much faster than this - usually in less than a second.  But
** regardless of how long each invocation lasts, successive backoffice runs
** must be spaced out by at least this much time.
*/
#define BKOFCE_LEASE_TIME   60    /* Length of lease validity in seconds */

#if LOCAL_INTERFACE
/*
** An instance of the following object describes a lease on the backoffice
** processing timeslot.  This lease is used to help ensure that no more than
** one process is running backoffice at a time.
*/
struct Lease {
  sqlite3_uint64 idCurrent; /* process ID for the current lease holder */
  sqlite3_uint64 tmCurrent; /* Expiration of the current lease */
  sqlite3_uint64 idNext;    /* process ID for the next lease holder on queue */
  sqlite3_uint64 tmNext;    /* Expiration of the next lease */
};
#endif

/***************************************************************************
** Local state variables
**
** Set to prevent backoffice processing from ever entering sleep or
** otherwise taking a long time to complete.  Set this when a user-visible
** process might need to wait for backoffice to complete.
*/
static int backofficeNoDelay = 0;

/* This variable is set to the name of a database on which backoffice
** should run if backoffice process is needed.  It is set by the
** backoffice_check_if_needed() routine which must be run while the database
** file is open.  Later, after the database is closed, the
** backoffice_run_if_needed() will consult this variable to see if it
** should be a no-op.
**
** The magic string "x" in this variable means "do not run the backoffice".
*/
static char *backofficeDb = 0;

/*
** Log backoffice activity to a file named here.  If not NULL, this
** overrides the "backoffice-logfile" setting of the database.  If NULL,
** the "backoffice-logfile" setting is used instead.
*/
static const char *backofficeLogfile = 0;

/*
** Write the log message into this open file.
*/
static FILE *backofficeFILE = 0;

/*
** Write backoffice log messages on this BLOB. to this connection:
*/
static Blob *backofficeBlob = 0;

/*
** Non-zero for extra logging detail.
*/
static int backofficeLogDetail = 0;

/* End of state variables
****************************************************************************/

/*
** This function emits a diagnostic message related to the processing in
** this module.
*/
#if defined(_WIN32)
# define BKOFCE_ALWAYS_TRACE   (1)
extern void sqlite3_win32_write_debug(const char *, int);
#else
# define BKOFCE_ALWAYS_TRACE   (0)
#endif
static void backofficeTrace(const char *zFormat, ...){
  char *zMsg = 0;
  if( BKOFCE_ALWAYS_TRACE || g.fAnyTrace ){
    va_list ap;
    va_start(ap, zFormat);
    zMsg = sqlite3_vmprintf(zFormat, ap);
    va_end(ap);
#if defined(_WIN32)
    sqlite3_win32_write_debug(zMsg, -1);
#endif
  }
  if( g.fAnyTrace ) fprintf(stderr, "%s", zMsg);
  if( zMsg ) sqlite3_free(zMsg);
}

/*
** Do not allow backoffice processes to sleep waiting on a timeslot.
** They must either do their work immediately or exit.
**
** In a perfect world, this interface would not exist, as there would
** never be a problem with waiting backoffice threads.  But in some cases
** a backoffice will delay a UI thread, so we don't want them to run for
** longer than needed.
*/
void backoffice_no_delay(void){
  backofficeNoDelay = 1;
}

/*
** Sleeps for the specified number of milliseconds -OR- until interrupted
** by another thread (if supported by the underlying platform).  Non-zero
** will be returned if the sleep was interrupted.
*/
static int backofficeSleep(int milliseconds){
#if defined(_WIN32)
  assert( milliseconds>=0 );
  if( SleepEx((DWORD)milliseconds, TRUE)==WAIT_IO_COMPLETION ){
    return 1;
  }
#else
  sqlite3_sleep(milliseconds);
#endif
  return 0;
}

/*
** Parse a unsigned 64-bit integer from a string.  Return a pointer
** to the character of z[] that occurs after the integer.
*/
static const char *backofficeParseInt(const char *z, sqlite3_uint64 *pVal){
  *pVal = 0;
  if( z==0 ) return 0;
  while( fossil_isspace(z[0]) ){ z++; }
  while( fossil_isdigit(z[0]) ){
    *pVal = (*pVal)*10 + z[0] - '0';
    z++;
  }
  return z;
}

/*
** Read the "backoffice" property and parse it into a Lease object.
**
** The backoffice property should consist of four integers:
**
**    (1)  Process ID for the active backoffice process.
**    (2)  Time (seconds since 1970) for when the active backoffice
**         lease expires.
**    (3)  Process ID for the on-deck backoffice process.
**    (4)  Time when the on-deck process should expire.
**
** No other process should start active backoffice processing until
** process (1) no longer exists and the current time exceeds (2).
*/
static void backofficeReadLease(Lease *pLease){
  Stmt q;
  memset(pLease, 0, sizeof(*pLease));
  db_unprotect(PROTECT_CONFIG);
  db_prepare(&q, "SELECT value FROM repository.config"
                 " WHERE name='backoffice'");
  if( db_step(&q)==SQLITE_ROW ){
    const char *z = db_column_text(&q,0);
    z = backofficeParseInt(z, &pLease->idCurrent);
    z = backofficeParseInt(z, &pLease->tmCurrent);
    z = backofficeParseInt(z, &pLease->idNext);
    backofficeParseInt(z, &pLease->tmNext);
  }
  db_finalize(&q);
  db_protect_pop();
}

/*
** Return a string that describes how long it has been since the
** last backoffice run.  The string is obtained from fossil_malloc().
*/
char *backoffice_last_run(void){
  Lease x;
  sqlite3_uint64 tmNow;
  double rAge;
  backofficeReadLease(&x);
  tmNow = time(0);
  if( x.tmCurrent==0 ){
    return fossil_strdup("never");
  }
  if( tmNow<=(x.tmCurrent-BKOFCE_LEASE_TIME) ){
    return fossil_strdup("moments ago");
  }
  rAge = (tmNow - (x.tmCurrent-BKOFCE_LEASE_TIME))/86400.0;
  return mprintf("%z ago", human_readable_age(rAge));
}

/*
** Write a lease to the backoffice property
*/
static void backofficeWriteLease(Lease *pLease){
  db_unprotect(PROTECT_CONFIG);
  db_multi_exec(
    "REPLACE INTO repository.config(name,value,mtime)"
    " VALUES('backoffice','%lld %lld %lld %lld',now())",
    pLease->idCurrent, pLease->tmCurrent,
    pLease->idNext, pLease->tmNext);
  db_protect_pop();
}

/*
** Check to see if the specified Win32 process is still alive.  It
** should be noted that even if this function returns non-zero, the
** process may die before another operation on it can be completed.
*/
#if defined(_WIN32)
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#  define PROCESS_QUERY_LIMITED_INFORMATION  (0x1000)
#endif
static int backofficeWin32ProcessExists(DWORD dwProcessId){
  HANDLE hProcess;
  hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,dwProcessId);
  if( hProcess==NULL ) return 0;
  CloseHandle(hProcess);
  return 1;
}
#endif

/*
** Check to see if the process identified by pid is alive.  If
** we cannot prove that the process is dead, return true.
*/
static int backofficeProcessExists(sqlite3_uint64 pid){
#if defined(_WIN32)
  return pid>0 && backofficeWin32ProcessExists((DWORD)pid)!=0;
#else
  return pid>0 && kill((pid_t)pid, 0)==0;
#endif
}

/*
** Check to see if the process identified by pid has finished.  If
** we cannot prove that the process is still running, return true.
*/
static int backofficeProcessDone(sqlite3_uint64 pid){
#if defined(_WIN32)
  return pid<=0 || backofficeWin32ProcessExists((DWORD)pid)==0;
#else
  return pid<=0 || kill((pid_t)pid, 0)!=0;
#endif
}

/*
** Return a process id number for the current process
*/
static sqlite3_uint64 backofficeProcessId(void){
  return (sqlite3_uint64)GETPID();
}


/*
** COMMAND: test-process-id
**
** Usage: %fossil [--sleep N] PROCESS-ID ...
**
** Show the current process id, and also tell whether or not all other
** processes IDs on the command line are running or not.  If the --sleep N
** option is provide, then sleep for N seconds before exiting.
*/
void test_process_id_command(void){
  const char *zSleep = find_option("sleep",0,1);
  int i;
  verify_all_options();
  fossil_print("ProcessID for this process: %lld\n", backofficeProcessId());
  if( zSleep ) sqlite3_sleep(1000*atoi(zSleep));
  for(i=2; i<g.argc; i++){
    sqlite3_uint64 x = (sqlite3_uint64)atoi(g.argv[i]);
    fossil_print("ProcessId %lld: exists %d done %d\n",
                 x, backofficeProcessExists(x),
                    backofficeProcessDone(x));
  }
}

/*
** COMMAND: test-backoffice-lease
**
** Usage: %fossil test-backoffice-lease ?--reset?
**
** Print out information about the backoffice "lease" entry in the
** config table that controls whether or not backoffice should run.
**
** If the --reset option is given, the backoffice lease is reset.
** The use of the --reset option can be disruptive.  It can cause two
** or more backoffice processes to be run simultaneously.  Use it with
** caution.
*/
void test_backoffice_lease(void){
  sqlite3_int64 tmNow = time(0);
  Lease x;
  const char *zLease;
  db_find_and_open_repository(0,0);
  if( find_option("reset",0,0)!=0 ){
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "DELETE FROM repository.config WHERE name='backoffice'"
    );
    db_protect_pop();
  }
  verify_all_options();
  zLease = db_get("backoffice","");
  fossil_print("now:        %lld\n", tmNow);
  fossil_print("lease:      \"%s\"\n", zLease);
  backofficeReadLease(&x);
  fossil_print("idCurrent:  %-20lld", x.idCurrent);
  if( backofficeProcessExists(x.idCurrent) ) fossil_print(" (exists)");
  if( backofficeProcessDone(x.idCurrent) ) fossil_print(" (done)");
  fossil_print("\n");
  fossil_print("tmCurrent:  %-20lld", x.tmCurrent);
  if( x.tmCurrent>0 ){
    fossil_print(" (now%+d)\n",x.tmCurrent-tmNow);
  }else{
    fossil_print("\n");
  }
  fossil_print("idNext:     %-20lld", x.idNext);
  if( backofficeProcessExists(x.idNext) ) fossil_print(" (exists)");
  if( backofficeProcessDone(x.idNext) ) fossil_print(" (done)");
  fossil_print("\n");
  fossil_print("tmNext:     %-20lld", x.tmNext);
  if( x.tmNext>0 ){
    fossil_print(" (now%+d)\n",x.tmNext-tmNow);
  }else{
    fossil_print("\n");
  }
}

/*
** If backoffice processing is needed set the backofficeDb variable to the
** name of the database file.  If no backoffice processing is needed,
** this routine makes no changes to state.
*/
void backoffice_check_if_needed(void){
  Lease x;
  sqlite3_uint64 tmNow;

  if( backofficeDb ) return;
  if( g.zRepositoryName==0 ) return;
  if( g.db==0 ) return;
  if( !db_table_exists("repository","config") ) return;
  if( db_get_boolean("backoffice-disable",0) ) return;
  tmNow = time(0);
  backofficeReadLease(&x);
  if( x.tmNext>=tmNow && backofficeProcessExists(x.idNext) ){
    /* Another backoffice process is already queued up to run.  This
    ** process does not need to do any backoffice work. */
    return;
  }else{
    /* We need to run backup to be (at a minimum) on-deck */
    backofficeDb = fossil_strdup(g.zRepositoryName);
  }
}

/*
** Call this routine to disable backoffice
*/
void backoffice_disable(void){
  backofficeDb = "x";
}

/*
** Check for errors prior to running backoffice_thread() or backoffice_run().
*/
static void backoffice_error_check_one(int *pOnce){
  if( *pOnce ){
    fossil_panic("multiple calls to backoffice()");
  }
  *pOnce = 1;
  if( g.db==0 ){
    fossil_panic("database not open for backoffice processing");
  }
  if( db_transaction_nesting_depth()!=0 ){
    fossil_panic("transaction %s not closed prior to backoffice processing",
                 db_transaction_start_point());
  }
}

/* This is the main loop for backoffice processing.
**
** If another process is already working as the current backoffice and
** the on-deck backoffice, then this routine returns very quickly
** without doing any work.
**
** If no backoffice processes are running at all, this routine becomes
** the main backoffice.
**
** If a primary backoffice is running, but an on-deck backoffice is
** needed, this routine becomes that on-deck backoffice.
*/
static void backoffice_thread(void){
  Lease x;
  sqlite3_uint64 tmNow;
  sqlite3_uint64 idSelf;
  int lastWarning = 0;
  int warningDelay = 30;
  static int once = 0;

  if( sqlite3_db_readonly(g.db, 0) ) return;
  if( db_is_protected(PROTECT_READONLY) ) return;
  g.zPhase = "backoffice-pending";
  backoffice_error_check_one(&once);
  idSelf = backofficeProcessId();
  while(1){
    tmNow = time(0);
    db_begin_write();
    backofficeReadLease(&x);
    if( x.tmNext>=tmNow
     && x.idNext!=idSelf
     && backofficeProcessExists(x.idNext)
    ){
      /* Another backoffice process is already queued up to run.  This
      ** process does not need to do any backoffice work and can stop
      ** immediately. */
      db_end_transaction(0);
      backofficeTrace("/***** Backoffice Processing Not Needed In %d *****/\n",
                      GETPID());
      break;
    }
    if( x.tmCurrent<tmNow && backofficeProcessDone(x.idCurrent) ){
      /* This process can start doing backoffice work immediately */
      x.idCurrent = idSelf;
      x.tmCurrent = tmNow + BKOFCE_LEASE_TIME;
      x.idNext = 0;
      x.tmNext = 0;
      g.zPhase = "backoffice-work";
      backofficeWriteLease(&x);
      db_end_transaction(0);
      backofficeTrace("/***** Begin Backoffice Processing %d *****/\n",
                      GETPID());
      backoffice_work();
      break;
    }
    if( backofficeNoDelay || db_get_boolean("backoffice-nodelay",0) ){
      /* If the no-delay flag is set, exit immediately rather than queuing
      ** up.  Assume that some future request will come along and handle any
      ** necessary backoffice work. */
      db_end_transaction(0);
      backofficeTrace(
           "/***** Backoffice No-Delay Exit For %d *****/\n",
           GETPID());
      break;
    }
    /* This process needs to queue up and wait for the current lease
    ** to expire before continuing. */
    x.idNext = idSelf;
    x.tmNext = (tmNow>x.tmCurrent ? tmNow : x.tmCurrent) + BKOFCE_LEASE_TIME;
    backofficeWriteLease(&x);
    db_end_transaction(0);
    backofficeTrace("/***** Backoffice On-deck %d *****/\n",  GETPID());
    if( x.tmCurrent >= tmNow ){
      if( backofficeSleep(1000*(x.tmCurrent - tmNow + 1)) ){
        /* The sleep was interrupted by a signal from another thread. */
        backofficeTrace("/***** Backoffice Interrupt %d *****/\n", GETPID());
        db_end_transaction(0);
        break;
      }
    }else{
      if( (sqlite3_uint64)(lastWarning+warningDelay) < tmNow ){
        sqlite3_int64 runningFor = BKOFCE_LEASE_TIME + tmNow - x.tmCurrent;
        if( warningDelay>=240 && runningFor<1800 ){
          fossil_warning(
           "backoffice process %lld still running after %d seconds",
           x.idCurrent, runningFor);
        }
        lastWarning = tmNow;
        warningDelay *= 2;
      }
      if( backofficeSleep(1000) ){
        /* The sleep was interrupted by a signal from another thread. */
        backofficeTrace("/***** Backoffice Interrupt %d *****/\n", GETPID());
        db_end_transaction(0);
        break;
      }
    }
  }
  return;
}

/*
** Append to a message to the backoffice log, if the log is open.
*/
void backoffice_log(const char *zFormat, ...){
  va_list ap;
  if( backofficeBlob==0 ) return;
  blob_append_char(backofficeBlob, ' ');
  va_start(ap, zFormat);
  blob_vappendf(backofficeBlob, zFormat, ap);
  va_end(ap);
}

#if !defined(_WIN32)
/*
** Capture routine for signals while running backoffice.
*/
static void backoffice_signal_handler(int sig){
  const char *zSig = 0;
  if( sig==SIGSEGV ) zSig = "SIGSEGV";
  if( sig==SIGFPE )  zSig = "SIGFPE";
  if( sig==SIGABRT ) zSig = "SIGABRT";
  if( sig==SIGILL )  zSig = "SIGILL";
  if( zSig==0 ){
    backoffice_log("signal-%d", sig);
  }else{
    backoffice_log("%s", zSig);
  }
  fprintf(backofficeFILE, "%s\n", blob_str(backofficeBlob));
  fflush(backofficeFILE);
  exit(1);
}
#endif

#if !defined(_WIN32)
/*
** Convert a struct timeval into an integer number of microseconds
*/
static long long int tvms(struct timeval *p){
  return ((long long int)p->tv_sec)*1000000 + (long long int)p->tv_usec;
}
#endif


/*
** This routine runs to do the backoffice processing.  When adding new
** backoffice processing tasks, add them here.
*/
void backoffice_work(void){
  /* Log the backoffice run for testing purposes.  For production deployments
  ** the "backoffice-logfile" property should be unset and the following code
  ** should be a no-op. */
  const char *zLog = backofficeLogfile;
  Blob log;
  int nThis;
  int nTotal = 0;
#if !defined(_WIN32)
  struct timeval sStart, sEnd;
#endif
  if( zLog==0 ) zLog = db_get("backoffice-logfile",0);
  if( zLog && zLog[0] && (backofficeFILE = fossil_fopen(zLog,"a"))!=0 ){
    int i;
    char *zName = db_get("project-name",0);
#if !defined(_WIN32)
    gettimeofday(&sStart, 0);
    signal(SIGSEGV, backoffice_signal_handler);
    signal(SIGABRT, backoffice_signal_handler);
    signal(SIGFPE, backoffice_signal_handler);
    signal(SIGILL, backoffice_signal_handler);
#endif
    if( zName==0 ){
      zName = (char*)file_tail(g.zRepositoryName);
      if( zName==0 ) zName = "(unnamed)";
    }else{
      /* Convert all spaces in the "project-name" into dashes */
      for(i=0; zName[i]; i++){ if( zName[i]==' ' ) zName[i] = '-'; }
    }
    blob_init(&log, 0, 0);
    backofficeBlob = &log;
    blob_appendf(&log, "%s %s", db_text(0, "SELECT datetime('now')"), zName);
  }

  /* Here is where the actual work of the backoffice happens */
  g.zPhase = "backoffice-alerts";
  nThis = alert_backoffice(0);
  if( nThis ){ backoffice_log("%d alerts", nThis); nTotal += nThis; }
  g.zPhase = "backoffice-hooks";
  nThis = hook_backoffice();
  if( nThis ){ backoffice_log("%d hooks", nThis); nTotal += nThis; }
  g.zPhase = "backoffice-close";

  /* Close the log */
  if( backofficeFILE ){
    if( nTotal || backofficeLogDetail ){
      if( nTotal==0 ) backoffice_log("no-op");
#if !defined(_WIN32)
      gettimeofday(&sEnd,0);
      backoffice_log("elapse-time %d us", tvms(&sEnd) - tvms(&sStart));
#endif
      fprintf(backofficeFILE, "%s\n", blob_str(backofficeBlob));
    }
    fclose(backofficeFILE);
  }
}

/*
** COMMAND: backoffice*
**
** Usage: %fossil backoffice [OPTIONS...] [REPOSITORIES...]
**
** Run backoffice processing on the repositories listed.  If no
** repository is specified, run it on the repository of the local check-out.
**
** This might be done by a cron job or similar to make sure backoffice
** processing happens periodically.  Or, the --poll option can be used
** to run this command as a daemon that will periodically invoke backoffice
** on a collection of repositories.
**
** If only a single repository is named and --poll is omitted, then the
** backoffice work is done in-process.  But if there are multiple repositories
** or if --poll is used, a separate sub-process is started for each poll of
** each repository.
**
** Standard options:
**
**    --debug                 Show what this command is doing
**
**    --logfile FILE          Append a log of backoffice actions onto FILE
**
**    --min N                 When polling, invoke backoffice at least
**                            once every N seconds even if the repository
**                            never changes.  0 or negative means disable
**                            this feature.  Default: 3600 (once per hour).
**
**    --poll N                Repeat backoffice calls for repositories that
**                            change in approximately N-second intervals.
**                            N less than 1 turns polling off (the default).
**                            Recommended polling interval: 60 seconds.
**
**    --trace                 Enable debugging output on stderr
**
** Options intended for internal use only which may change or be
** discontinued in future releases:
**
**    --nodelay               Do not queue up or wait for a backoffice job
**                            to complete. If no work is available or if
**                            backoffice has run recently, return immediately.
**
**    --nolease               Always run backoffice, even if there is a lease
**                            conflict.  This option implies --nodelay.  This
**                            option is added to secondary backoffice commands
**                            that are invoked by the --poll option.
*/
void backoffice_command(void){
  int nPoll;
  int nMin;
  const char *zPoll;
  int bDebug = 0;
  int bNoLease = 0;
  unsigned int nCmd = 0;
  if( find_option("trace",0,0)!=0 ) g.fAnyTrace = 1;
  if( find_option("nodelay",0,0)!=0 ) backofficeNoDelay = 1;
  backofficeLogfile = find_option("logfile",0,1);
  zPoll = find_option("poll",0,1);
  nPoll = zPoll ? atoi(zPoll) : 0;
  zPoll = find_option("min",0,1);
  nMin = zPoll ? atoi(zPoll) : 3600;
  bDebug = find_option("debug",0,0)!=0;
  bNoLease = find_option("nolease",0,0)!=0;

  /* Silently consume the -R or --repository flag, leaving behind its
  ** argument. This is for legacy compatibility. Older versions of the
  ** backoffice command only ran on a single repository that was specified
  ** using the -R option. */
  (void)find_option("repository","R",0);

  verify_all_options();
  if( g.argc>3 || nPoll>0 ){
    /* Either there are multiple repositories named on the command-line
    ** or we are polling.  In either case, each backoffice should be run
    ** using a separate sub-process */
    int i;
    time_t iNow = 0;
    time_t ix;
    i64 *aLastRun = fossil_malloc( sizeof(i64)*g.argc );
    memset(aLastRun, 0, sizeof(i64)*g.argc );
    while( 1 /* exit via "break;" */){
      time_t iNext = time(0);
      for(i=2; i<g.argc; i++){
        Blob cmd;
        if( !file_isfile(g.argv[i], ExtFILE) ){
          continue;  /* Repo no longer exists.  Ignore it. */
        }
        if( iNow
         && iNow>file_mtime(g.argv[i], ExtFILE)
         && (nMin<=0 || aLastRun[i]+nMin>iNow)
        ){
          continue;  /* Not yet time to run this one */
        }
        blob_init(&cmd, 0, 0);
        blob_append_escaped_arg(&cmd, g.nameOfExe, 1);
        blob_append(&cmd, " backoffice --nodelay", -1);
        if( g.fAnyTrace ){
          blob_append(&cmd, " --trace", -1);
        }
        if( bDebug ){
          blob_append(&cmd, " --debug", -1);
        }
        if( nPoll>0 ){
          blob_append(&cmd, " --nolease", -1);
        }
        if( backofficeLogfile ){
          blob_append(&cmd, " --logfile", -1);
          blob_append_escaped_arg(&cmd, backofficeLogfile, 1);
        }
        blob_append_escaped_arg(&cmd, g.argv[i], 1);
        nCmd++;
        if( bDebug ){
          fossil_print("COMMAND[%u]: %s\n", nCmd, blob_str(&cmd));
        }
        fossil_system(blob_str(&cmd));
        aLastRun[i] = iNext;
        blob_reset(&cmd);
      }
      if( nPoll<1 ) break;
      iNow = iNext;
      ix = time(0);
      if( ix < iNow+nPoll ){
        sqlite3_int64 nMS = (iNow + nPoll - ix)*1000;
        if( bDebug )fossil_print("SLEEP: %lld\n", nMS);
        sqlite3_sleep((int)nMS);
      }
    }
  }else{
    /* Not polling and only one repository named.  Backoffice is run
    ** once by this process, which then exits */
    if( g.argc==3 ){
      g.zRepositoryOption = g.argv[2];
      g.argc--;
    }
    db_find_and_open_repository(0,0);
    if( bDebug ){
      backofficeLogDetail = 1;
    }
    if( bNoLease ){
      backoffice_work();
    }else{
      backoffice_thread();
    }
  }
}

/*
** This is the main interface to backoffice from the rest of the system.
** This routine launches either backoffice_thread() directly or as a
** subprocess.
*/
void backoffice_run_if_needed(void){
  if( backofficeDb==0 ) return;
  if( strcmp(backofficeDb,"x")==0 ) return;
  if( g.db ) return;
  if( g.repositoryOpen ) return;
#if defined(_WIN32)
  {
    int i;
    intptr_t x;
    char *argv[4];
    wchar_t *ax[5];
    argv[0] = g.nameOfExe;
    argv[1] = "backoffice";
    argv[2] = "-R";
    argv[3] = backofficeDb;
    ax[4] = 0;
    for(i=0; i<=3; i++) ax[i] = fossil_utf8_to_unicode(argv[i]);
    x = _wspawnv(_P_NOWAIT, ax[0], (const wchar_t * const *)ax);
    for(i=0; i<=3; i++) fossil_unicode_free(ax[i]);
    backofficeTrace(
      "/***** Subprocess %d creates backoffice child %lu *****/\n",
      GETPID(), GetProcessId((HANDLE)x));
    if( x>=0 ) return;
  }
#else /* unix */
  {
    pid_t pid = fork();
    if( pid>0 ){
      /* This is the parent in a successful fork().  Return immediately. */
      backofficeTrace(
        "/***** Subprocess %d creates backoffice child %d *****/\n",
        GETPID(), (int)pid);
      return;
    }
    if( pid==0 ){
      /* This is the child of a successful fork().  Run backoffice. */
      int i;
      setsid();
      for(i=0; i<=2; i++){
        close(i);
        open("/dev/null", O_RDWR);
      }
      for(i=3; i<100; i++){ close(i); }
      g.fDebug = 0;
      g.httpIn = 0;
      g.httpOut = 0;
      db_open_repository(backofficeDb);
      backofficeDb = "x";
      backoffice_thread();
      db_close(1);
      backofficeTrace("/***** Backoffice Child %d exits *****/\n", GETPID());
      exit(0);
    }
    fossil_warning("backoffice process %d fork failed, errno %d", GETPID(),
                   errno);
  }
#endif
  /* Fork() failed or is unavailable.  Run backoffice in this process, but
  ** do so with the no-delay setting.
  */
  backofficeNoDelay = 1;
  db_open_repository(backofficeDb);
  backofficeDb = "x";
  backoffice_thread();
  db_close(1);
}
