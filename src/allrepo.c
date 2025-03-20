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
** Build a string that contains all of the command-line options
** specified as arguments.  collect_argument() is used for stand-alone
** options and collect_argument_value() is used for options that are
** followed by an argument value.
*/
static void collect_argument(Blob *pExtra,const char *zArg,const char *zShort){
  const char *z = find_option(zArg, zShort, 0);
  if( z!=0 ){
    blob_appendf(pExtra, " %s", z);
  }
}
static void collect_argument_value(
    Blob *pExtra, const char *zArg, const char *zShort
){
  const char *zValue = find_option(zArg, zShort, 1);
  if( zValue ){
    if( zValue[0] ){
      blob_appendf(pExtra, " --%s %$", zArg, zValue);
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
** COMMAND: all               abbrv-subcom
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
**    backup      Backup all repositories.  The argument must be the name of
**                a directory into which all backup repositories are written.
**
**    cache       Manages the cache used for potentially expensive web
**                pages.  Any additional arguments are passed on verbatim
**                to the cache command.
**
**    changes     Shows all local check-outs that have uncommitted changes.
**                This operation has no additional options.
**
**    clean       Delete all "extra" files in all local check-outs.  Extreme
**                caution should be exercised with this command because its
**                effects cannot be undone.  Use of the --dry-run option to
**                carefully review the local check-outs to be operated upon
**                and the --whatif option to carefully review the files to
**                be deleted beforehand is highly recommended.  The command
**                line options supported by the clean command itself, if any
**                are present, are passed along verbatim.
**
**    config      Only the "config pull AREA" command works.
**
**    dbstat      Run the "dbstat" command on all repositories.
**
**    extras      Shows "extra" files from all local check-outs.  The command
**                line options supported by the extra command itself, if any
**                are present, are passed along verbatim.
**
**    fts-config  Run the "fts-config" command on all repositories.
**
**    git CMD     Do the "git export" or "git status" command (whichever
**                is specified by CMD) on all repositories for which
**                a Git mirror has been previously established.
**
**    info        Run the "info" command on all repositories.
**
**    pull        Run a "pull" operation on all repositories.  Only the
**                --verbose and --share-links options are supported.
**
**    push        Run a "push" on all repositories.  Only the --verbose
**                option is supported.
**
**    rebuild     Rebuild on all repositories.  The command line options
**                supported by the rebuild command itself, if any are
**                present, are passed along verbatim.  The --force option
**                is not supported.
**
**    remote      Show remote hosts for all repositories.
**
**    repack      Look for extra compression in all repositories.
**
**    sync        Run a "sync" on all repositories.  Only the --verbose
**                and --unversioned and --share-links options are supported.
**
**    set[tings]  Run the "settings" command on all repositories.
**                This command is useful for settings like "max-loadavg" which
**                you usually want to be the same across all repositories
**                on a server.
**
**    unset       Run the "unset" command on all repositories
**
**    server      Run the "server" commands on all repositories.
**                The root URI gives a listing of all repos.
**
**    ui          Run the "ui" command on all repositories.  Like "server"
**                but bind to the loopback TCP address only, enable
**                the --localauth option and automatically launch a
**                web-browser
**
**    whatis      Run the "whatis" command on all repositories.  Only
**                show output for repositories that have a match.
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
**                local check-outs to be ignored instead.
**
**    list | ls   Display the location of all repositories.  The -c|--ckout
**                option causes all local check-outs to be listed instead.
**
** Repositories are automatically added to the set of known repositories
** when one of the following commands are run against the repository:
** clone, info, pull, push, or sync.  Even previously ignored repositories
** are added back to the list of repositories by these commands.
**
** Options:
**   --dry-run         If given, display instead of run actions
**   --showfile        Show the repository or check-out being operated upon
**   --stop-on-error   Halt immediately if any subprocess fails
*/
void all_cmd(void){
  Stmt q;
  const char *zCmd;
  char *zSyscmd;
  Blob extra;
  int useCheckouts = 0;
  int quiet = 0;
  int dryRunFlag = 0;
  int showFile = find_option("showfile",0,0)!=0;
  int stopOnError;
  int nToDel = 0;
  int showLabel = 0;

  (void)find_option("dontstop",0,0);   /* Legacy.  Now the default */
  stopOnError = find_option("stop-on-error",0,0)!=0;
  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("test",0,0)!=0; /* deprecated */
  }

  if( g.argc<3 ){
    usage("SUBCOMMAND ...");
  }
  db_open_config(1, 0);
  blob_zero(&extra);
  zCmd = g.argv[2];
  if( !login_is_nobody() ) blob_appendf(&extra, " -U %s", g.zLogin);
  if( fossil_strcmp(zCmd, "ui")==0
      || fossil_strcmp(zCmd, "server")==0 ){
    g.argv[1] = g.argv[2];
    g.argv[2] = "/";
    cmd_webserver();
    return;
  }
  if( fossil_strcmp(zCmd, "list")==0 || fossil_strcmp(zCmd,"ls")==0 ){
    zCmd = "list";
    useCheckouts = find_option("ckout","c",0)!=0;
  }else if( fossil_strcmp(zCmd, "backup")==0 ){
    char *zDest;
    zCmd = "backup -R";
    collect_argument(&extra, "overwrite",0);
    if( g.argc!=4 ) usage("backup DIRECTORY");
    zDest = g.argv[3];
    if( file_isdir(zDest, ExtFILE)!=1 ){
      fossil_fatal("argument to \"fossil all backup\" must be a directory");
    }
    blob_appendf(&extra, " %$", zDest);
  }else if( fossil_strcmp(zCmd, "clean")==0 ){
    zCmd = "clean --chdir";
    collect_argument(&extra, "allckouts",0);
    collect_argument_value(&extra, "case-sensitive", 0);
    collect_argument_value(&extra, "clean", 0);
    collect_argument(&extra, "dirsonly",0);
    collect_argument(&extra, "disable-undo",0);
    collect_argument(&extra, "dotfiles",0);
    collect_argument(&extra, "emptydirs",0);
    collect_argument(&extra, "force","f");
    collect_argument_value(&extra, "ignore", 0);
    collect_argument_value(&extra, "keep", 0);
    collect_argument(&extra, "no-prompt",0);
    collect_argument(&extra, "temp",0);
    collect_argument(&extra, "verbose","v");
    collect_argument(&extra, "whatif",0);
    useCheckouts = 1;
  }else if( fossil_strcmp(zCmd, "config")==0 ){
    zCmd = "config -R";
    collect_argv(&extra, 3);
    (void)find_option("legacy",0,0);
    (void)find_option("overwrite",0,0);
    verify_all_options();
    if( g.argc!=5 || fossil_strcmp(g.argv[3],"pull")!=0 ){
      usage("configure pull AREA ?OPTIONS?");
    }
  }else if( fossil_strcmp(zCmd, "dbstat")==0 ){
    zCmd = "dbstat --omit-version-info -R";
    showLabel = 1;
    quiet = 1;
    collect_argument(&extra, "brief", "b");
    collect_argument(&extra, "db-check", 0);
    collect_argument(&extra, "db-verify", 0);
  }else if( fossil_strcmp(zCmd, "extras")==0 ){
    if( showFile ){
      zCmd = "extras --chdir";
    }else{
      zCmd = "extras --header --chdir";
    }
    collect_argument(&extra, "abs-paths",0);
    collect_argument_value(&extra, "case-sensitive", 0);
    collect_argument(&extra, "dotfiles",0);
    collect_argument_value(&extra, "ignore", 0);
    collect_argument(&extra, "rel-paths",0);
    useCheckouts = 1;
    stopOnError = 0;
    quiet = 1;
  }else if( fossil_strcmp(zCmd, "git")==0 ){
    if( g.argc<4 ){
      usage("git (export|status)");
    }else{
      if( fossil_strcmp(g.argv[3], "export")==0 ){
        zCmd = "git export --if-mirrored -R";
      }else if( fossil_strcmp(g.argv[3], "status")==0 ){
        zCmd = "git status --by-all -q -R";
        quiet = 1;
      }else{
        usage("git (export|status)");
      }
    }
  }else if( fossil_strcmp(zCmd, "push")==0 ){
    zCmd = "push -autourl -R";
    collect_argument(&extra, "verbose","v");
  }else if( fossil_strcmp(zCmd, "pull")==0 ){
    zCmd = "pull -autourl -R";
    collect_argument(&extra, "verbose","v");
    collect_argument(&extra, "share-links",0);
  }else if( fossil_strcmp(zCmd, "rebuild")==0 ){
    zCmd = "rebuild";
    collect_argument(&extra, "analyze",0);
    collect_argument(&extra, "cluster",0);
    collect_argument(&extra, "compress",0);
    collect_argument(&extra, "compress-only",0);
    collect_argument(&extra, "noverify",0);
    collect_argument_value(&extra, "pagesize", 0);
    collect_argument(&extra, "vacuum",0);
    collect_argument(&extra, "deanalyze",0); /* Deprecated */
    collect_argument(&extra, "analyze",0);
    collect_argument(&extra, "wal",0);
    collect_argument(&extra, "stats",0);
    collect_argument(&extra, "index",0);
    collect_argument(&extra, "noindex",0);
    collect_argument(&extra, "ifneeded", 0);
  }else if( fossil_strcmp(zCmd, "remote")==0 ){
    showLabel = 1;
    quiet = 1;
    collect_argument(&extra, "show-passwords", 0);
    if( g.argc==3 ){
      zCmd = "remote -R";
    }else if( g.argc!=4 ){
      usage("remote ?config-data|list|ls?");
    }else if( fossil_strcmp(g.argv[3],"ls")==0
           || fossil_strcmp(g.argv[3],"list")==0 ){
      zCmd = "remote ls -R";
    }else if( fossil_strcmp(g.argv[3],"ls")==0
           || fossil_strcmp(g.argv[3],"list")==0 ){
      zCmd = "remote ls -R";
    }else if( fossil_strcmp(g.argv[3],"config-data")==0 ){
      zCmd = "remote config-data -R";
    }else{
      usage("remote ?config-data|list|ls?");
    }
  }else if( fossil_strcmp(zCmd, "repack")==0 ){
    zCmd = "repack";
  }else if( fossil_strcmp(zCmd, "set")==0
            || fossil_strcmp(zCmd, "setting")==0
            || fossil_strcmp(zCmd, "settings")==0 ){
    zCmd = "settings -R";
    collect_argument(&extra, "changed", 0);
    collect_argv(&extra, 3);
  }else if( fossil_strcmp(zCmd, "unset")==0 ){
    zCmd = "unset -R";
    collect_argv(&extra, 3);
  }else if( fossil_strcmp(zCmd, "fts-config")==0 ){
    zCmd = "fts-config -R";
    collect_argv(&extra, 3);
  }else if( fossil_strcmp(zCmd, "sync")==0 ){
    zCmd = "sync -autourl -R";
    collect_argument(&extra, "share-links",0);
    collect_argument(&extra, "verbose","v");
    collect_argument(&extra, "unversioned","u");
    collect_argument(&extra, "all",0);
  }else if( fossil_strcmp(zCmd, "test-integrity")==0 ){
    collect_argument(&extra, "db-only", "d");
    collect_argument(&extra, "parse", 0);
    collect_argument(&extra, "quick", "q");
    zCmd = "test-integrity";
  }else if( fossil_strcmp(zCmd, "test-orphans")==0 ){
    zCmd = "test-orphans -R";
  }else if( fossil_strcmp(zCmd, "test-missing")==0 ){
    zCmd = "test-missing -q -R";
    collect_argument(&extra, "notshunned",0);
  }else if( fossil_strcmp(zCmd, "changes")==0 ){
    zCmd = "changes --quiet --header --chdir";
    useCheckouts = 1;
    stopOnError = 0;
    quiet = 1;
  }else if( fossil_strcmp(zCmd, "ignore")==0 ){
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
        db_unprotect(PROTECT_CONFIG);
        db_multi_exec("%s", blob_sql_text(&sql));
        db_protect_pop();
      }
    }
    db_end_transaction(0);
    blob_reset(&sql);
    blob_reset(&fn);
    blob_reset(&extra);
    return;
  }else if( fossil_strcmp(zCmd, "add")==0 ){
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
      if( !file_isfile(z, ExtFILE) ) continue;
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
        db_unprotect(PROTECT_CONFIG);
        db_multi_exec("%s", blob_sql_text(&sql));
        db_protect_pop();
      }
    }
    db_end_transaction(0);
    blob_reset(&sql);
    blob_reset(&fn);
    blob_reset(&extra);
    return;
  }else if( fossil_strcmp(zCmd, "info")==0 ){
    zCmd = "info";
    showLabel = 1;
    quiet = 1;
  }else if( fossil_strcmp(zCmd, "cache")==0 ){
    zCmd = "cache -R";
    showLabel = 1;
    collect_argv(&extra, 3);
  }else if( fossil_strcmp(zCmd, "whatis")==0 ){
    zCmd = "whatis -q -R";
    quiet = 1;
    collect_argument(&extra, "file", "f");
    collect_argument_value(&extra, "type", 0);
    collect_argv(&extra, 3);
  }else{
    fossil_fatal("\"all\" subcommand should be one of: "
      "add cache changes clean dbstat extras fts-config git ignore "
      "info list ls pull push rebuild remote "
      "server settings sync ui unset whatis");
  }
  verify_all_options();
  db_multi_exec(
     "CREATE TEMP TABLE repolist(\n"
     "  name TEXT, -- Filename\n"
     "  tag TEXT,  -- Key for the GLOBAL_CONFIG table entry\n"
     "  inode TEXT -- Unique identifier for this file\n"
     ");\n"

     /* The seenFile() table holds inode names for entries that have
     ** already been processed.  */
     "CREATE TEMP TABLE seenFile(x TEXT COLLATE nocase);\n"

     /* The toDel() table holds the "tag" for entries that need to be
     ** deleted because they are redundant or no longer exist */
     "CREATE TEMP TABLE toDel(x TEXT);\n"
  );
  sqlite3_create_function(g.db, "inode", 1, SQLITE_UTF8, 0,
                          file_inode_sql_func, 0, 0);
  if( useCheckouts ){
    db_multi_exec(
       "INSERT INTO repolist "
       "SELECT substr(name, 7), name, inode(substr(name,7))"
       "  FROM global_config"
       " WHERE substr(name, 1, 6)=='ckout:'"
       " ORDER BY 1"
    );
  }else{
    db_multi_exec(
       "INSERT INTO repolist "
       "SELECT substr(name, 6), name, inode(substr(name,6))"
       "  FROM global_config"
       " WHERE substr(name, 1, 5)=='repo:'"
       " ORDER BY 1"
    );
  }
  db_prepare(&q,"SELECT name, tag, inode FROM repolist ORDER BY 1");
  while( db_step(&q)==SQLITE_ROW ){
    int rc;
    const char *zFilename = db_column_text(&q, 0);
    const char *zInode = db_column_text(&q,2);
#if !USE_SEE
    if( sqlite3_strglob("*.efossil", zFilename)==0 ) continue;
#endif
    if( file_access(zFilename, F_OK)
     || !file_is_canonical(zFilename)
     || (useCheckouts && file_isdir(zFilename, ExtFILE)!=1)
     || db_exists("SELECT 1 FROM temp.seenFile where x=%Q", zInode)
    ){
      db_multi_exec("INSERT INTO toDel VALUES(%Q)", db_column_text(&q, 1));
      nToDel++;
      continue;
    }
    db_multi_exec("INSERT INTO seenFile(x) VALUES(%Q)", zInode);
    if( zCmd[0]=='l' ){
      fossil_print("%s\n", zFilename);
      continue;
    }else if( showFile ){
      fossil_print("%s: %s\n", useCheckouts ? "check-out" : "repository",
                   zFilename);
    }
    zSyscmd = mprintf("%$ %s %$%s",
                      g.nameOfExe, zCmd, zFilename, blob_str(&extra));
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
    if( rc ){
      if( stopOnError ) break;
      /* If there is an error, pause briefly, but do not stop.  The brief
      ** pause is so that if the prior command failed with Ctrl-C then there
      ** will be time to stop the whole thing with a second Ctrl-C. */
      sqlite3_sleep(330);
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
      db_unprotect(PROTECT_CONFIG);
      db_multi_exec("%s", zSql /*safe-for-%s*/ );
      db_protect_pop();
    }
  }
}
