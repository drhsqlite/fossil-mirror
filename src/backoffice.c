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
** regardless of how fast each invocations run, successive backoffice runs
** must be spaced out by at least this much time.
*/
#define BKOFCE_LEASE_TIME   60    /* Length of lease validity */

#if LOCAL_INTERFACE
/*
** An instance of the following object describes a lease on the backoffice
** processing timeslot.  This lease is used to help ensure that no more than
** one processing is running backoffice at a time.
*/
struct Lease {
  sqlite3_uint64 idCurrent;   /* ID for the current lease holder */
  sqlite3_uint64 tmCurrent;   /* Expiration of the current lease */
  sqlite3_uint64 idNext;      /* ID for the next lease holder on queue */
  sqlite3_uint64 tmNext;      /* Expiration of the next lease */
};
#endif

/*
** Set to prevent backoffice processing from every entering sleep or
** otherwise taking a long time to complete.  Set this when a user-visible
** process might need to wait for backoffice to complete.
*/
static int backofficeNoDelay = 0;

/*
** Disable the backoffice
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
** Check to see if the process identified by selfId is alive.  If
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
** Check to see if the process identified by selfId has finished.  If
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

/* This is the main public interface to the backoffice.  A process invokes this
** routine in an attempt to become the backoffice.  If another process is
** already working as the backoffice, this routine returns very quickly
** without doing any work - allowing the other process to continue.  But
** if no other processes are currently operating as the backoffice, this
** routine enters a loop to do background work periodically.
*/
void backoffice_run(void){
  Lease x;
  sqlite3_uint64 tmNow;
  sqlite3_uint64 idSelf;
  int lastWarning = 0;
  int warningDelay = 30;
  static int once = 0;

  if( once ){
    fossil_panic("multiple calls to backoffice_run()");
  }
  once = 1;
  if( g.db==0 ){
    fossil_panic("database not open for backoffice processing");
  }
  if( db_transaction_nesting_depth()!=0 ){
    fossil_panic("transaction %s not closed prior to backoffice processing",
                 db_transaction_start_point());
  }
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
** COMMAND: test-backoffice
**
** Usage: test-backoffice
**
** Run backoffice processing
*/
void test_backoffice_command(void){
  db_find_and_open_repository(0,0);
  backoffice_run();
}
