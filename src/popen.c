/*
** Copyright (c) 2010 D. Richard Hipp
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
** This file contains an implementation of a bi-directional popen().
*/
#include "config.h"
#include "popen.h"

/*
** Create a child process running shell command "zCmd".  *ppOut is
** a FILE that becomes the standard input of the child process.  
** (The caller writes to *ppOut in order to send text to the child.)
** *ppIn is stdout from the child process.  (The caller
** reads from *ppIn in order to receive input from the child.)
** Note that *ppIn is an unbuffered file descriptor, not a FILE.
** The process ID of the child is written into *pChildPid.
**
** Return the number of errors.
*/
int popen2(const char *zCmd, int *pfdIn, FILE **ppOut, int *pChildPid){
#ifdef __MINGW32__
  return 1;   /* Not implemented on windows, yet */
#else
  int pin[2], pout[2];
  *pfdIn = 0;
  *ppOut = 0;
  *pChildPid = 0;

  if( pipe(pin)<0 ){
    return 1;
  }
  if( pipe(pout)<0 ){
    close(pin[0]);
    close(pin[1]);
    return 1;
  }
  *pChildPid = fork();
  if( *pChildPid<0 ){
    close(pin[0]);
    close(pin[1]);
    close(pout[0]);
    close(pout[1]);
    *pChildPid = 0;
    return 1;
  }
  if( *pChildPid==0 ){
    /* This is the child process */
    close(0);
    dup(pout[0]);
    close(pout[0]);
    close(pout[1]);
    close(1);
    dup(pin[1]);
    close(pin[0]);
    close(pin[1]);
    execl("/bin/sh", "/bin/sh", "-c", zCmd, (char*)0);
    return 1;
  }else{
    /* This is the parent process */
    close(pin[1]);
    *pfdIn = pin[0];
    close(pout[0]);
    *ppOut = fdopen(pout[1], "w");
    return 0;
  }
#endif
}

/*
** Close the connection to a child process previously created using
** popen2().  Kill off the child process, then close the pipes.
*/
void pclose2(int fdIn, FILE *pOut, int childPid){
#ifdef __MINGW32__
  /* Not implemented, yet */
#else
  kill(childPid, SIGINT);
  close(fdIn);
  fclose(pOut);
#endif
}
