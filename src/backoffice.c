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
** There is a main process that is doing the actually work, and there is
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
** All work performance by the backoffice is in the backoffice_work()
** routine.
*/
#include "config.h"
#include "backoffice.h"
#include <time.h>
#if defined(_WIN32)
# include <windows.h>
#else
# include <unistd.h>
# include <sys/types.h>
# include <signal.h>
#endif

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
*/
static char *backofficeDb = 0;

/* End of state variables
****************************************************************************/

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
*/
static void backofficeReadLease(Lease *pLease){
  Stmt q;
  memset(pLease, 0, sizeof(*pLease));
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
}

/*
** Write a lease to the backoffice property
*/
static void backofficeWriteLease(Lease *pLease){
  db_multi_exec(
    "REPLACE INTO repository.config(name,value,mtime)"
    " VALUES('backoffice','%lld %lld %lld %lld',now())",
    pLease->idCurrent, pLease->tmCurrent,
    pLease->idNext, pLease->tmNext);
}

/*
** Check to see if the process identified by pid is alive.  If
** we cannot prove the the process is dead, return true.
*/
static int backofficeProcessExists(sqlite3_uint64 pid){
#if defined(_WIN32)
  return 1;
#else
  return pid>0 && kill((pid_t)pid, 0)==0;
#endif 
}

/*
** Check to see if the process identified by pid has finished.  If
** we cannot prove the the process is still running, return true.
*/
static int backofficeProcessDone(sqlite3_uint64 pid){
#if defined(_WIN32)
  return 1;
#else
  return pid<=0 || kill((pid_t)pid, 0)!=0;
#endif 
}

/*
** Return a process id number for the current process
*/
static sqlite3_uint64 backofficeProcessId(void){
#if defined(_WIN32)
  return (sqlite3_uint64)GetCurrentProcessId();
#else
  return (sqlite3_uint64)getpid();
#endif
}

/*
** Set an alarm to cause the process to exit after "x" seconds.  This
** prevents any kind of bug from keeping a backoffice process running
** indefinitely.
*/
#if !defined(_WIN32)
static void backofficeSigalrmHandler(int x){
  fossil_panic("backoffice timeout");
}
#endif
static void backofficeTimeout(int x){
#if !defined(_WIN32)
  signal(SIGALRM, backofficeSigalrmHandler);
  alarm(x);
#endif
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
** If a primary backoffice is running, but a on-deck backoffice is
** needed, this routine becomes that on-desk backoffice.
*/
static void backoffice_thread(void){
  Lease x;
  sqlite3_uint64 tmNow;
  sqlite3_uint64 idSelf;
  int lastWarning = 0;
  int warningDelay = 30;
  static int once = 0;

  backoffice_error_check_one(&once);
  backofficeTimeout(BKOFCE_LEASE_TIME*2);
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
      break;
    }
    if( x.tmCurrent<tmNow && backofficeProcessDone(x.idCurrent) ){
      /* This process can start doing backoffice work immediately */
      x.idCurrent = idSelf;
      x.tmCurrent = tmNow + BKOFCE_LEASE_TIME;
      x.idNext = 0;
      x.tmNext = 0;
      backofficeWriteLease(&x);
      db_end_transaction(0);
      if( g.fAnyTrace ){
        fprintf(stderr, "/***** Begin Backoffice Processing %d *****/\n",
                        getpid());
      }
      backoffice_work();
      break;
    }
    if( backofficeNoDelay || db_get_boolean("backoffice-nodelay",1) ){
      /* If the no-delay flag is set, exit immediately rather than queuing
      ** up.  Assume that some future request will come along and handle any
      ** necessary backoffice work. */
      db_end_transaction(0);
      break;
    }
    /* This process needs to queue up and wait for the current lease
    ** to expire before continuing. */
    x.idNext = idSelf;
    x.tmNext = (tmNow>x.tmCurrent ? tmNow : x.tmCurrent) + BKOFCE_LEASE_TIME;
    backofficeWriteLease(&x);
    db_end_transaction(0);
    if( g.fAnyTrace ){
      fprintf(stderr, "/***** Backoffice On-deck %d *****/\n",  getpid());
    }
    if( x.tmCurrent >= tmNow ){
      sqlite3_sleep(1000*(x.tmCurrent - tmNow + 1));
    }else{
      if( lastWarning+warningDelay < tmNow ){
        fossil_warning(
           "backoffice process %lld still running after %d seconds",
           x.idCurrent, (int)(BKOFCE_LEASE_TIME + tmNow - x.tmCurrent));
        lastWarning = tmNow;
        warningDelay *= 2;
      }
      sqlite3_sleep(1000);
    }
  }
  return;
}

/*
** This routine runs to do the backoffice processing.  When adding new
** backoffice processing tasks, add them here.
*/
void backoffice_work(void){
  email_backoffice(0);
}

/*
** COMMAND: backoffice
**
** Usage: backoffice [-R repository]
**
** Run backoffice processing.  This might be done by a cron job or
** similar to make sure backoffice processing happens periodically.
*/
void backoffice_command(void){
  verify_all_options();
  db_find_and_open_repository(0,0);
  backoffice_thread();
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
#if !defined(_WIN32)
  {
    pid_t pid = fork();
    if( pid>0 ){
      /* This is the parent in a successful fork().  Return immediately. */
      if( g.fAnyTrace ){
        fprintf(stderr, 
          "/***** Subprocess %d creates backoffice child %d *****/\n",
          getpid(), (int)pid);
      }
      return;
    }
    if( pid==0 ){
      /* This is the child of a successful fork().  Run backoffice. */
      db_open_repository(backofficeDb);
      backofficeDb = "x";
      backoffice_thread();
      db_close(1);
      if( g.fAnyTrace ){
        fprintf(stderr, "/***** Backoffice Child %d exits *****/\n", getpid());
      }
      exit(0);
    }
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
