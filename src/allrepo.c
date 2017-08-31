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
static void collect_argument(Blob *pExtra, const char *zArg, const char *zShort){
  const char *z = find_option(zArg, zShort, 0);
  if( z!=0 ){
    blob_appendf(pExtra, " %s", z);
  }
}
static void collect_argument_value(Blob *pExtra, const char *zArg){
  const char *zValue = find_option(zArg, 0, 1);
  if( zValue ){
    if( zValue[0] ){
      blob_appendf(pExtra, " --%s %s", zArg, zValue);
    }else{
      blob_appendf(pExtra, " --%s \"\"", zArg);
    }
  }
}
static void collect_argv(Blob *pExtra, int iStart){
  int i;
  for(i=iStart; i<g.argc; i++){
    blob_appendf(pExtra, " %s", g.argv[i]);
  }
}


/*
** COMMAND: all
**
** Usage: %fossil all SUBCOMMAND ...
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
**    cache       Manages the cache used for potentially expensive web
**                pages.  Any additional arguments are passed on verbatim
**                to the cache command.
**
**    changes     Shows all local checkouts that have uncommitted changes.
**                This operation has no additional options.
**
**    clean       Delete all "extra" files in all local checkouts.  Extreme
**                caution should be exercised with this command because its
**                effects cannot be undone.  Use of the --dry-run option to
**                carefully review the local checkouts to be operated upon
**                and the --whatif option to carefully review the files to
**                be deleted beforehand is highly recommended.  The command
**                line options supported by the clean command itself, if any
**                are present, are passed along verbatim.
**
**    config      Only the "config pull AREA" command works.
**
**    dbstat      Run the "dbstat" command on all repositories.
**
**    extras      Shows "extra" files from all local checkouts.  The command
**                line options supported by the extra command itself, if any
**                are present, are passed along verbatim.
**
**    fts-config  Run the "fts-config" command on all repositories.
**
**    info        Run the "info" command on all repositories.
**
**    pull        Run a "pull" operation on all repositories.  Only the
**                --verbose option is supported.
**
**    push        Run a "push" on all repositories.  Only the --verbose
**                option is supported.
**
**    rebuild     Rebuild on all repositories.  The command line options
**                supported by the rebuild command itself, if any are
**                present, are passed along verbatim.  The --force and
**                --randomize options are not supported.
**
**    sync        Run a "sync" on all repositories.  Only the --verbose
**                option is supported.
**
**    setting     Run the "setting", "set", or "unset" commands on all
**    set         repositories.  These command are particularly useful in
**    unset       conjunction with the "max-loadavg" setting which cannot
**                otherwise be set globally.
**
**    server      Run the "ui" or "server" commands on all repositories.
**    ui          The root URI gives a listing of all repos.
**
**
** In addition, the following maintenance operations are supported:
**
**    add         Add all the repositories named to the set of repositories
**                tracked by Fossil.  Normally Fossil is able to keep up with
**                this list by itself, but sometimes it can benefit from this
**                hint if you rename repositories.
**
**    ignore      Arguments are repositories that should be ignored by
**                subsequent clean, extras, list, pull, push, rebuild, and
**                sync operations.  The -c|--ckout option causes the listed
**                local checkouts to be ignored instead.
**
**    list | ls   Display the location of all repositories.  The -c|--ckout
**                option causes all local checkouts to be listed instead.
**
** Repositories are automatically added to the set of known repositories
** when one of the following commands are run against the repository:
** clone, info, pull, push, or sync.  Even previously ignored repositories
** are added back to the list of repositories by these commands.
**
** Options:
**   --showfile     Show the repository or checkout being operated upon.
**   --dontstop     Continue with other repositories even after an error.
**   --dry-run      If given, display instead of run actions.
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
  int showFile = find_option("showfile",0,0)!=0;
  int stopOnError = find_option("dontstop",0,0)==0;
  int nToDel = 0;
  int showLabel = 0;

  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("test",0,0)!=0; /* deprecated */
  }

  if( g.argc<3 ){
    usage("SUBCOMMAND ...");
  }
  n = strlen(g.argv[2]);
  db_open_config(1, 0);
  blob_zero(&extra);
  zCmd = g.argv[2];
  if( !login_is_nobody() ) blob_appendf(&extra, " -U %s", g.zLogin);
  if( strncmp(zCmd, "ui", n)==0 || strncmp(zCmd, "server", n)==0 ){
    g.argv[1] = g.argv[2];
    g.argv[2] = "/";
    cmd_webserver();
    return;
  }
  if( strncmp(zCmd, "list", n)==0 || strncmp(zCmd,"ls",n)==0 ){
    zCmd = "list";
    useCheckouts = find_option("ckout","c",0)!=0;
  }else if( strncmp(zCmd, "clean", n)==0 ){
    zCmd = "clean --chdir";
    collect_argument(&extra, "allckouts",0);
    collect_argument_value(&extra, "case-sensitive");
    collect_argument_value(&extra, "clean");
    collect_argument(&extra, "dirsonly",0);
    collect_argument(&extra, "disable-undo",0);
    collect_argument(&extra, "dotfiles",0);
    collect_argument(&extra, "emptydirs",0);
    collect_argument(&extra, "force","f");
    collect_argument_value(&extra, "ignore");
    collect_argument_value(&extra, "keep");
    collect_argument(&extra, "no-prompt",0);
    collect_argument(&extra, "temp",0);
    collect_argument(&extra, "verbose","v");
    collect_argument(&extra, "whatif",0);
    useCheckouts = 1;
  }else if( strncmp(zCmd, "config", n)==0 ){
    zCmd = "config -R";
    collect_argv(&extra, 3);
    (void)find_option("legacy",0,0);
    (void)find_option("overwrite",0,0);
    verify_all_options();
    if( g.argc!=5 || fossil_strcmp(g.argv[3],"pull")!=0 ){
      usage("configure pull AREA ?OPTIONS?");
    }
  }else if( strncmp(zCmd, "dbstat", n)==0 ){
    zCmd = "dbstat --omit-version-info -R";
    showLabel = 1;
    quiet = 1;
    collect_argument(&extra, "brief", "b");
    collect_argument(&extra, "db-check", 0);
  }else if( strncmp(zCmd, "extras", n)==0 ){
    if( showFile ){
      zCmd = "extras --chdir";
    }else{
      zCmd = "extras --header --chdir";
    }
    collect_argument(&extra, "abs-paths",0);
    collect_argument_value(&extra, "case-sensitive");
    collect_argument(&extra, "dotfiles",0);
    collect_argument_value(&extra, "ignore");
    collect_argument(&extra, "rel-paths",0);
    useCheckouts = 1;
    stopOnError = 0;
    quiet = 1;
  }else if( strncmp(zCmd, "push", n)==0 ){
    zCmd = "push -autourl -R";
    collect_argument(&extra, "verbose","v");
  }else if( strncmp(zCmd, "pull", n)==0 ){
    zCmd = "pull -autourl -R";
    collect_argument(&extra, "verbose","v");
  }else if( strncmp(zCmd, "rebuild", n)==0 ){
    zCmd = "rebuild";
    collect_argument(&extra, "cluster",0);
    collect_argument(&extra, "compress",0);
    collect_argument(&extra, "compress-only",0);
    collect_argument(&extra, "noverify",0);
    collect_argument_value(&extra, "pagesize");
    collect_argument(&extra, "vacuum",0);
    collect_argument(&extra, "deanalyze",0);
    collect_argument(&extra, "analyze",0);
    collect_argument(&extra, "wal",0);
    collect_argument(&extra, "stats",0);
    collect_argument(&extra, "index",0);
    collect_argument(&extra, "noindex",0);
    collect_argument(&extra, "ifneeded", 0);
  }else if( strncmp(zCmd, "setting", n)==0 ){
    zCmd = "setting -R";
    collect_argv(&extra, 3);
  }else if( strncmp(zCmd, "unset", n)==0 ){
    zCmd = "unset -R";
    collect_argv(&extra, 3);
  }else if( strncmp(zCmd, "fts-config", n)==0 ){
    zCmd = "fts-config -R";
    collect_argv(&extra, 3);
  }else if( strncmp(zCmd, "sync", n)==0 ){
    zCmd = "sync -autourl -R";
    collect_argument(&extra, "verbose","v");
    collect_argument(&extra, "unversioned","u");
  }else if( strncmp(zCmd, "test-integrity", n)==0 ){
    collect_argument(&extra, "parse", 0);
    zCmd = "test-integrity";
  }else if( strncmp(zCmd, "test-orphans", n)==0 ){
    zCmd = "test-orphans -R";
  }else if( strncmp(zCmd, "test-missing", n)==0 ){
    zCmd = "test-missing -q -R";
    collect_argument(&extra, "notshunned",0);
  }else if( strncmp(zCmd, "changes", n)==0 ){
    zCmd = "changes --quiet --header --chdir";
    useCheckouts = 1;
    stopOnError = 0;
    quiet = 1;
  }else if( strncmp(zCmd, "ignore", n)==0 ){
    int j;
    Blob fn = BLOB_INITIALIZER;
    Blob sql = BLOB_INITIALIZER;
    useCheckouts = find_option("ckout","c",0)!=0;
    verify_all_options();
    db_begin_transaction();
    for(j=3; j<g.argc; j++, blob_reset(&sql), blob_reset(&fn)){
      file_canonical_name(g.argv[j], &fn, useCheckouts?1:0);
      blob_append_sql(&sql,
         "DELETE FROM global_config WHERE name GLOB '%s:%q'",
         useCheckouts?"ckout":"repo", blob_str(&fn)
      );
      if( dryRunFlag ){
        fossil_print("%s\n", blob_sql_text(&sql));
      }else{
        db_multi_exec("%s", blob_sql_text(&sql));
      }
    }
    db_end_transaction(0);
    blob_reset(&sql);
    blob_reset(&fn);
    blob_reset(&extra);
    return;
  }else if( strncmp(zCmd, "add", n)==0 ){
    int j;
    Blob fn = BLOB_INITIALIZER;
    Blob sql = BLOB_INITIALIZER;
    verify_all_options();
    db_begin_transaction();
    for(j=3; j<g.argc; j++, blob_reset(&fn), blob_reset(&sql)){
      sqlite3 *db;
      int rc;
      const char *z;
      file_canonical_name(g.argv[j], &fn, 0);
      z = blob_str(&fn);
      if( !file_isfile(z) ) continue;
      g.dbIgnoreErrors++;
      rc = sqlite3_open(z, &db);
      if( rc!=SQLITE_OK ){ sqlite3_close(db); g.dbIgnoreErrors--; continue; }
      rc = sqlite3_exec(db, "SELECT rcvid FROM blob, delta LIMIT 1", 0, 0, 0);
      sqlite3_close(db);
      g.dbIgnoreErrors--;
      if( rc!=SQLITE_OK ) continue;
      blob_append_sql(&sql,
         "INSERT OR IGNORE INTO global_config(name,value)"
         "VALUES('repo:%q',1)", z
      );
      if( dryRunFlag ){
        fossil_print("%s\n", blob_sql_text(&sql));
      }else{
        db_multi_exec("%s", blob_sql_text(&sql));
      }
    }
    db_end_transaction(0);
    blob_reset(&sql);
    blob_reset(&fn);
    blob_reset(&extra);
    return;
  }else if( strncmp(zCmd, "info", n)==0 ){
    zCmd = "info";
    showLabel = 1;
    quiet = 1;
  }else if( strncmp(zCmd, "cache", n)==0 ){
    zCmd = "cache -R";
    showLabel = 1;
    collect_argv(&extra, 3);
  }else{
    fossil_fatal("\"all\" subcommand should be one of: "
                 "add cache changes clean dbstat extras fts-config ignore "
                 "info list ls pull push rebuild server setting sync ui unset");
  }
  verify_all_options();
  zFossil = quoteFilename(g.nameOfExe);
  db_multi_exec("CREATE TEMP TABLE repolist(name,tag);");
  if( useCheckouts ){
    db_multi_exec(
       "INSERT INTO repolist "
       "SELECT DISTINCT substr(name, 7), name COLLATE nocase"
       "  FROM global_config"
       " WHERE substr(name, 1, 6)=='ckout:'"
       " ORDER BY 1"
    );
  }else{
    db_multi_exec(
       "INSERT INTO repolist "
       "SELECT DISTINCT substr(name, 6), name COLLATE nocase"
       "  FROM global_config"
       " WHERE substr(name, 1, 5)=='repo:'"
       " ORDER BY 1"
    );
  }
  db_multi_exec("CREATE TEMP TABLE toDel(x TEXT)");
  db_prepare(&q, "SELECT name, tag FROM repolist ORDER BY 1");
  while( db_step(&q)==SQLITE_ROW ){
    int rc;
    const char *zFilename = db_column_text(&q, 0);
#if !USE_SEE
    if( sqlite3_strglob("*.efossil", zFilename)==0 ) continue;
#endif
    if( file_access(zFilename, F_OK)
     || !file_is_canonical(zFilename)
     || (useCheckouts && file_isdir(zFilename)!=1)
    ){
      db_multi_exec("INSERT INTO toDel VALUES(%Q)", db_column_text(&q, 1));
      nToDel++;
      continue;
    }
    if( zCmd[0]=='l' ){
      fossil_print("%s\n", zFilename);
      continue;
    }else if( showFile ){
      fossil_print("%s: %s\n", useCheckouts ? "checkout" : "repository",
                   zFilename);
    }
    zQFilename = quoteFilename(zFilename);
    zSyscmd = mprintf("%s %s %s%s",
                      zFossil, zCmd, zQFilename, blob_str(&extra));
    if( showLabel ){
      int len = (int)strlen(zFilename);
      int nStar = 80 - (len + 15);
      if( nStar<2 ) nStar = 1;
      fossil_print("%.13c %s %.*c\n", '*', zFilename, nStar, '*');
      fflush(stdout);
    }
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

  blob_reset(&extra);

  /* If any repositories whose names appear in the ~/.fossil file could not
  ** be found, remove those names from the ~/.fossil file.
  */
  if( nToDel>0 ){
    const char *zSql = "DELETE FROM global_config WHERE name IN toDel";
    if( dryRunFlag ){
      fossil_print("%s\n", zSql);
    }else{
      db_multi_exec("%s", zSql /*safe-for-%s*/ );
    }
  }
}
