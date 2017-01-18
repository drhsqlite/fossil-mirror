/*
** Copyright (c) 2016 D. Richard Hipp
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
** This module contains the code that implements the "fossil shell" command.
**
** The fossil shell prompts for lines of user input, then parses each line
** after the fashion of a standard Bourne shell and forks a child process
** to run the corresponding Fossil command.  This only works on Unix.
**
** The "fossil shell" command is intended for use with SEE-enabled fossil.
** It allows multiple commands to be issued without having to reenter the
** crypto phasephrase for each command.
*/
#include "config.h"
#include "fshell.h"
#include <ctype.h>

#ifndef _WIN32
# include "linenoise.h"
# include <sys/types.h>
# include <sys/wait.h>
#endif


/*
** COMMAND: shell*
**
** Usage: %fossil shell
**
** Prompt for lines of input from stdin.  Parse each line and evaluate
** it as a separate fossil command, in a child process.  The initial
** "fossil" is omitted from each line.
**
** This command only works on unix-like platforms that support fork().
** It is non-functional on Windows.
*/
void shell_cmd(void){
#ifdef _WIN32
  fossil_fatal("the 'shell' command is not supported on windows");
#else
  int nArg;
  int mxArg = 0;
  int n, i;
  char **azArg = 0;
  int fDebug;
  pid_t childPid;
  char *zLine = 0;
  fDebug = find_option("debug", 0, 0)!=0;
  db_find_and_open_repository(OPEN_ANY_SCHEMA|OPEN_OK_NOT_FOUND, 0);
  db_close(0);
  sqlite3_shutdown();
  linenoiseSetMultiLine(1);
  while( (free(zLine), zLine = linenoise("fossil> ")) ){
    /* Remember shell history within the current session */
    linenoiseHistoryAdd(zLine);

    /* Parse the line of input */
    n = (int)strlen(zLine);
    for(i=0, nArg=1; i<n; i++){
      while( fossil_isspace(zLine[i]) ){ i++; }
      if( i>=n ) break;
      if( nArg>=mxArg ){
        mxArg = nArg+10;
        azArg = fossil_realloc(azArg, sizeof(char*)*mxArg);
        if( nArg==1 ) azArg[0] = g.argv[0];
      }
      if( zLine[i]=='"' || zLine[i]=='\'' ){
        char cQuote = zLine[i];
        i++;
        azArg[nArg++] = &zLine[i];
        for(i++; i<n && zLine[i]!=cQuote; i++){}
      }else{
        azArg[nArg++] = &zLine[i];
        while( i<n && !isspace(zLine[i]) ){ i++; }
      }
      zLine[i] = 0;
    }

    /* If the --debug flag was used, display the parsed arguments */
    if( fDebug ){
      for(i=1; i<nArg; i++){
        fossil_print("argv[%d] = [%s]\n", i, azArg[i]);
      }
    }

    /* Special cases */
    if( nArg<2 ) continue;
    if( fossil_strcmp(azArg[1],"exit")==0 ) break;

    /* Fork a process to handle the command */
    childPid = fork();
    if( childPid<0 ){
      printf("could not fork a child process to handle the command\n");
      fflush(stdout);
      continue;
    }
    if( childPid==0 ){
      /* This is the child process */
      int main(int, char**);
      main(nArg, azArg);
      exit(0);
    }else{
      /* The parent process */
      int status;
      waitpid(childPid, &status, 0);
    }
  }
#endif
}
