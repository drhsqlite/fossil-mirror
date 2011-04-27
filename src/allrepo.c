/*
** Copyright (c) 2008 D. Richard Hipp
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
** This file contains code to implement the "all" command-line method.
*/
#include "config.h"
#include "allrepo.h"
#include <assert.h>

/*
** The input string is a filename.  Return a new copy of this
** filename if the filename requires quoting due to special characters
** such as spaces in the name.
**
** If the filename cannot be safely quoted, return a NULL pointer.
**
** Space to hold the returned string is obtained from malloc.  A new
** string is returned even if no quoting is needed.
*/
static char *quoteFilename(const char *zFilename){
  int i, c;
  int needQuote = 0;
  for(i=0; (c = zFilename[i])!=0; i++){
    if( c=='"' ) return 0;
    if( fossil_isspace(c) ) needQuote = 1;
    if( c=='\\' && zFilename[i+1]==0 ) return 0;
    if( c=='$' ) return 0;
  }
  if( needQuote ){
    return mprintf("\"%s\"", zFilename);
  }else{
    return mprintf("%s", zFilename);
  }
}


/*
** COMMAND: all
**
** Usage: %fossil all (list|ls|pull|push|rebuild|sync)
**
** The ~/.fossil file records the location of all repositories for a
** user.  This command performs certain operations on all repositories
** that can be useful before or after a period of disconnected operation.
**
** On Win32 systems, the file is named "_fossil" and is located in
** %LOCALAPPDATA%, %APPDATA% or %HOMEPATH%.
**
** Available operations are:
**
**    ignore     Arguments are repositories that should be ignored
**               by subsequent list, pull, push, rebuild, and sync.
**
**    list | ls  Display the location of all repositories
**
**    pull       Run a "pull" operation on all repositories
**
**    push       Run a "push" on all repositories
**
**    rebuild    Rebuild on all repositories
**
**    sync       Run a "sync" on all repositories
**
** Respositories are automatically added to the set of known repositories
** when one of the following commands against the repository: clone, info,
** pull, push, or sync.  Even previously ignored repositories are added back
** to the list of repositories by these commands.
*/
void all_cmd(void){
  int n;
  Stmt q;
  const char *zCmd;
  char *zSyscmd;
  char *zFossil;
  char *zQFilename;
  int nMissing;
  int stopOnError = find_option("dontstop",0,0)==0;
  int rc;
  
  if( g.argc<3 ){
    usage("list|ls|pull|push|rebuild|sync");
  }
  n = strlen(g.argv[2]);
  db_open_config(1);
  zCmd = g.argv[2];
  if( strncmp(zCmd, "list", n)==0 || strncmp(zCmd,"ls",n)==0 ){
    zCmd = "list";
  }else if( strncmp(zCmd, "push", n)==0 ){
    zCmd = "push -autourl -R";
  }else if( strncmp(zCmd, "pull", n)==0 ){
    zCmd = "pull -autourl -R";
  }else if( strncmp(zCmd, "rebuild", n)==0 ){
    zCmd = "rebuild";
  }else if( strncmp(zCmd, "sync", n)==0 ){
    zCmd = "sync -autourl -R";
  }else if( strncmp(zCmd, "test-integrity", n)==0 ){
    zCmd = "test-integrity";
  }else if( strncmp(zCmd, "ignore", n)==0 ){
    int j;
    db_begin_transaction();
    for(j=3; j<g.argc; j++){
      db_multi_exec("DELETE FROM global_config WHERE name GLOB 'repo:%q'",
         g.argv[j]);
    }
    db_end_transaction(0);
    return;
  }else{
    fossil_fatal("\"all\" subcommand should be one of: "
                 "ignore list ls push pull rebuild sync");
  }
  zFossil = quoteFilename(fossil_nameofexe());
  nMissing = 0;
  db_prepare(&q,
     "SELECT DISTINCT substr(name, 6) COLLATE nocase"
     "  FROM global_config"
     " WHERE substr(name, 1, 5)=='repo:' ORDER BY 1"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFilename = db_column_text(&q, 0);
    if( access(zFilename, 0) ){
      nMissing++;
      continue;
    }
    if( !file_is_canonical(zFilename) ) nMissing++;
    if( zCmd[0]=='l' ){
      fossil_print("%s\n", zFilename);
      continue;
    }
    zQFilename = quoteFilename(zFilename);
    zSyscmd = mprintf("%s %s %s", zFossil, zCmd, zQFilename);
    fossil_print("%s\n", zSyscmd);
    fflush(stdout);
    rc = fossil_system(zSyscmd);
    free(zSyscmd);
    free(zQFilename);
    if( stopOnError && rc ){
      nMissing = 0;
      break;
    }
  }
  
  /* If any repositories whose names appear in the ~/.fossil file could not
  ** be found, remove those names from the ~/.fossil file.
  */
  if( nMissing ){
    db_begin_transaction();
    db_reset(&q);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zFilename = db_column_text(&q, 0);
      if( access(zFilename, 0) ){
        char *zRepo = mprintf("repo:%s", zFilename);
        db_unset(zRepo, 1);
        free(zRepo);
      }else if( !file_is_canonical(zFilename) ){
        Blob cname;
        char *zRepo = mprintf("repo:%s", zFilename);
        db_unset(zRepo, 1);
        free(zRepo);
        file_canonical_name(zFilename, &cname);
        zRepo = mprintf("repo:%s", blob_str(&cname));
        db_set(zRepo, "1", 1);
        free(zRepo);
      }
    }
    db_reset(&q);
    db_end_transaction(0);
  }
  db_finalize(&q);
}
