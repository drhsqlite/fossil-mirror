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
** Build a string that contains all of the command-line options
** specified as arguments.  If the option name begins with "+" then
** it takes an argument.  Without the "+" it does not.
*/
static void collect_argument(Blob *pExtra, const char *zArg){
  if( find_option(zArg, 0, 0)!=0 ){
    blob_appendf(pExtra, " --%s", zArg);
  }
}
static void collect_argument_value(Blob *pExtra, const char *zArg){
  const char *zValue = find_option(zArg, 0, 1);
  if( zValue ){
    blob_appendf(pExtra, " --%s %s", zArg, zValue);
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
**               The -c|--ckout option causes the listed local checkouts
**               to be ignored instead.
**
**    list | ls  Display the location of all repositories.
**               The -c|--ckout option causes all local checkouts to be
**               list instead.
**
**    changes    Shows all local checkouts that have uncommitted changes
**
**    pull       Run a "pull" operation on all repositories
**
**    push       Run a "push" on all repositories
**
**    rebuild    Rebuild on all repositories
**
**    sync       Run a "sync" on all repositories
**
** Repositories are automatically added to the set of known repositories
** when one of the following commands are run against the repository: clone,
** info, pull, push, or sync.  Even previously ignored repositories are
** added back to the list of repositories by these commands.
*/
void all_cmd(void){
  int n;
  Stmt q;
  const char *zCmd;
  char *zSyscmd;
  char *zFossil;
  char *zQFilename;
  Blob extra;
  int useCheckouts = 0;
  int quiet = 0;
  int dryRunFlag = 0;
  int stopOnError = find_option("dontstop",0,0)==0;
  int rc;
  Bag outOfDate;
  
  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("test",0,0)!=0; /* deprecated */
  }

  if( g.argc<3 ){
    usage("changes|list|ls|pull|push|rebuild|sync");
  }
  n = strlen(g.argv[2]);
  db_open_config(1);
  blob_zero(&extra);
  zCmd = g.argv[2];
  if( strncmp(zCmd, "list", n)==0 || strncmp(zCmd,"ls",n)==0 ){
    zCmd = "list";
    useCheckouts = find_option("ckout","c",0)!=0;
  }else if( strncmp(zCmd, "push", n)==0 ){
    zCmd = "push -autourl -R";
    collect_argument(&extra, "verbose");
  }else if( strncmp(zCmd, "pull", n)==0 ){
    zCmd = "pull -autourl -R";
    collect_argument(&extra, "verbose");
  }else if( strncmp(zCmd, "rebuild", n)==0 ){
    zCmd = "rebuild";
    collect_argument(&extra, "cluster");
    collect_argument(&extra, "compress");
    collect_argument(&extra, "noverify");
    collect_argument_value(&extra, "pagesize");
    collect_argument(&extra, "vacuum");
    collect_argument(&extra, "deanalyze");
    collect_argument(&extra, "analyze");
    collect_argument(&extra, "wal");
    collect_argument(&extra, "stat");
  }else if( strncmp(zCmd, "sync", n)==0 ){
    zCmd = "sync -autourl -R";
    collect_argument(&extra, "verbose");
  }else if( strncmp(zCmd, "test-integrity", n)==0 ){
    zCmd = "test-integrity";
  }else if( strncmp(zCmd, "test-orphans", n)==0 ){
    zCmd = "test-orphans -R";
  }else if( strncmp(zCmd, "test-missing", n)==0 ){
    zCmd = "test-missing -q -R";
    collect_argument(&extra, "notshunned");
  }else if( strncmp(zCmd, "changes", n)==0 ){
    zCmd = "changes --quiet --header --chdir";
    useCheckouts = 1;
    stopOnError = 0;
    quiet = 1;
  }else if( strncmp(zCmd, "ignore", n)==0 ){
    int j;
    useCheckouts = find_option("ckout","c",0)!=0;
    verify_all_options();
    db_begin_transaction();
    for(j=3; j<g.argc; j++){
      char *zSql = mprintf("DELETE FROM global_config"
                           " WHERE name GLOB '%s:%q'",
                           useCheckouts?"ckout":"repo", g.argv[j]);
      if( dryRunFlag ){
        fossil_print("%s\n", zSql);
      }else{
        db_multi_exec("%s", zSql);
      }
      fossil_free(zSql);
    }
    db_end_transaction(0);
    return;
  }else{
    fossil_fatal("\"all\" subcommand should be one of: "
                 "changes ignore list ls push pull rebuild sync");
  }
  verify_all_options();
  zFossil = quoteFilename(g.nameOfExe);
  if( useCheckouts ){
    db_prepare(&q,
       "SELECT substr(name, 7) COLLATE nocase, max(rowid)"
       "  FROM global_config"
       " WHERE substr(name, 1, 6)=='ckout:'"
       " GROUP BY 1 ORDER BY 1"
    );
  }else{
    db_prepare(&q,
       "SELECT substr(name, 6) COLLATE nocase, max(rowid)"
       "  FROM global_config"
       " WHERE substr(name, 1, 5)=='repo:'"
       " GROUP BY 1 ORDER BY 1"
    );
  }
  bag_init(&outOfDate);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFilename = db_column_text(&q, 0);
    int rowid = db_column_int(&q, 1);
    if( file_access(zFilename, 0) || !file_is_canonical(zFilename) ){
      bag_insert(&outOfDate, rowid);
      continue;
    }
    if( useCheckouts && file_isdir(zFilename)!=1 ){
      bag_insert(&outOfDate, rowid);
      continue;
    }
    if( zCmd[0]=='l' ){
      fossil_print("%s\n", zFilename);
      continue;
    }
    zQFilename = quoteFilename(zFilename);
    zSyscmd = mprintf("%s %s %s%s",
                      zFossil, zCmd, zQFilename, blob_str(&extra));
    if( !quiet || dryRunFlag ){
      fossil_print("%s\n", zSyscmd);
      fflush(stdout);
    }
    rc = dryRunFlag ? 0 : fossil_system(zSyscmd);
    free(zSyscmd);
    free(zQFilename);
    if( stopOnError && rc ){
      break;
    }
  }
  db_finalize(&q);
  
  /* If any repositories whose names appear in the ~/.fossil file could not
  ** be found, remove those names from the ~/.fossil file.
  */
  if( bag_count(&outOfDate)>0 ){
    Blob sql;
    char *zSep = "(";
    int rowid;
    blob_zero(&sql);
    blob_appendf(&sql, "DELETE FROM global_config WHERE rowid IN ");
    for(rowid=bag_first(&outOfDate); rowid>0; rowid=bag_next(&outOfDate,rowid)){
      blob_appendf(&sql, "%s%d", zSep, rowid);
      zSep = ",";
    }
    blob_appendf(&sql, ")");
    if( dryRunFlag ){
      fossil_print("%s\n", blob_str(&sql));
    }else{
      db_multi_exec(blob_str(&sql));
    }
    blob_reset(&sql);
  }
}
