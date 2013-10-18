/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to check-in versions of the project
** from the local repository.
*/
#include "config.h"
#include "checkin.h"
#include <assert.h>

/*
** Generate text describing all changes.  Prepend zPrefix to each line
** of output.
**
** We assume that vfile_check_signature has been run.
**
** If missingIsFatal is true, then any files that are missing or which
** are not true files results in a fatal error.
*/
static void status_report(
  Blob *report,          /* Append the status report here */
  const char *zPrefix,   /* Prefix on each line of the report */
  int missingIsFatal,    /* MISSING and NOT_A_FILE are fatal errors */
  int cwdRelative        /* Report relative to the current working dir */
){
  Stmt q;
  int nPrefix = strlen(zPrefix);
  int nErr = 0;
  Blob rewrittenPathname;
  Blob where;
  const char *zName;
  int i;

  blob_zero(&where);
  for(i=2; i<g.argc; i++) {
    Blob fname;
    file_tree_name(g.argv[i], &fname, 1);
    zName = blob_str(&fname);
    if( fossil_strcmp(zName, ".")==0 ) {
      blob_reset(&where);
      break;
    }
    blob_appendf(&where, " %s (pathname=%Q %s) "
                 "OR (pathname>'%q/' %s AND pathname<'%q0' %s)",
                 (blob_size(&where)>0) ? "OR" : "AND", zName,
                 filename_collation(), zName, filename_collation(),
                 zName, filename_collation());
  }

  db_prepare(&q,
    "SELECT pathname, deleted, chnged, rid, coalesce(origname!=pathname,0)"
    "  FROM vfile "
    " WHERE is_selected(id) %s"
    "   AND (chnged OR deleted OR rid=0 OR pathname!=origname) ORDER BY 1",
    blob_str(&where)
  );
  blob_zero(&rewrittenPathname);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    const char *zDisplayName = zPathname;
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3)==0;
    int isRenamed = db_column_int(&q,4);
    char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    if( cwdRelative ){
      file_relative_name(zFullName, &rewrittenPathname, 0);
      zDisplayName = blob_str(&rewrittenPathname);
      if( zDisplayName[0]=='.' && zDisplayName[1]=='/' ){
        zDisplayName += 2;  /* no unnecessary ./ prefix */
      }
    }
    blob_append(report, zPrefix, nPrefix);
    if( isDeleted ){
      blob_appendf(report, "DELETED    %s\n", zDisplayName);
    }else if( !file_wd_isfile_or_link(zFullName) ){
      if( file_access(zFullName, 0)==0 ){
        blob_appendf(report, "NOT_A_FILE %s\n", zDisplayName);
        if( missingIsFatal ){
          fossil_warning("not a file: %s", zDisplayName);
          nErr++;
        }
      }else{
        blob_appendf(report, "MISSING    %s\n", zDisplayName);
        if( missingIsFatal ){
          fossil_warning("missing file: %s", zDisplayName);
          nErr++;
        }
      }
    }else if( isNew ){
      blob_appendf(report, "ADDED      %s\n", zDisplayName);
    }else if( isDeleted ){
      blob_appendf(report, "DELETED    %s\n", zDisplayName);
    }else if( isChnged ){
      if( isChnged==2 ){
        blob_appendf(report, "UPDATED_BY_MERGE %s\n", zDisplayName);
      }else if( isChnged==3 ){
        blob_appendf(report, "ADDED_BY_MERGE %s\n", zDisplayName);
      }else if( isChnged==4 ){
        blob_appendf(report, "UPDATED_BY_INTEGRATE %s\n", zDisplayName);
      }else if( isChnged==5 ){
        blob_appendf(report, "ADDED_BY_INTEGRATE %s\n", zDisplayName);
      }else if( file_contains_merge_marker(zFullName) ){
        blob_appendf(report, "CONFLICT   %s\n", zDisplayName);
      }else{
        blob_appendf(report, "EDITED     %s\n", zDisplayName);
      }
    }else if( isRenamed ){
      blob_appendf(report, "RENAMED    %s\n", zDisplayName);
    }else{
      report->nUsed -= nPrefix;
    }
    free(zFullName);
  }
  blob_reset(&rewrittenPathname);
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid, id FROM vmerge JOIN blob ON merge=rid"
                 " WHERE id<=0");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zLabel = "MERGED_WITH";
    switch( db_column_int(&q, 1) ){
      case -1:  zLabel = "CHERRYPICK ";  break;
      case -2:  zLabel = "BACKOUT    ";  break;
      case -4:  zLabel = "INTEGRATE  ";  break;
    }
    blob_append(report, zPrefix, nPrefix);
    blob_appendf(report, "%s %s\n", zLabel, db_column_text(&q, 0));
  }
  db_finalize(&q);
  if( nErr ){
    fossil_fatal("aborting due to prior errors");
  }
}

/*
** Use the "relative-paths" setting and the --abs-paths and
** --rel-paths command line options to determine whether the
** status report should be shown relative to the current
** working directory.
*/
static int determine_cwd_relative_option()
{
  int relativePaths = db_get_boolean("relative-paths", 1);
  int absPathOption = find_option("abs-paths", 0, 0)!=0;
  int relPathOption = find_option("rel-paths", 0, 0)!=0;
  if( absPathOption ){ relativePaths = 0; }
  if( relPathOption ){ relativePaths = 1; }
  return relativePaths;
}

/*
** COMMAND: changes
**
** Usage: %fossil changes ?OPTIONS?
**
** Report on the edit status of all files in the current checkout.
**
** Pathnames are displayed according to the "relative-paths" setting,
** unless overridden by the --abs-paths or --rel-paths options.
**
** Options:
**    --abs-paths       Display absolute pathnames.
**    --rel-paths       Display pathnames relative to the current working
**                      directory.
**    --sha1sum         Verify file status using SHA1 hashing rather
**                      than relying on file mtimes.
**    --header          Identify the repository if there are changes
**    -v|--verbose      Say "(none)" if there are no changes
**
** See also: extra, ls, status
*/
void changes_cmd(void){
  Blob report;
  int vid;
  int useSha1sum = find_option("sha1sum", 0, 0)!=0;
  int showHdr = find_option("header",0,0)!=0;
  int verboseFlag = find_option("verbose","v",0)!=0;
  int cwdRelative = 0;
  db_must_be_within_tree();
  cwdRelative = determine_cwd_relative_option();
  blob_zero(&report);
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid, useSha1sum ? CKSIG_SHA1 : 0);
  status_report(&report, "", 0, cwdRelative);
  if( verboseFlag && blob_size(&report)==0 ){
    blob_append(&report, "  (none)\n", -1);
  }
  if( showHdr && blob_size(&report)>0 ){
    fossil_print("Changes for %s at %s:\n", db_get("project-name","???"),
                 g.zLocalRoot);
  }
  blob_write_to_file(&report, "-");
  blob_reset(&report);
}

/*
** COMMAND: status
**
** Usage: %fossil status ?OPTIONS?
**
** Report on the status of the current checkout.
**
** Pathnames are displayed according to the "relative-paths" setting,
** unless overridden by the --abs-paths or --rel-paths options.
**
** Options:
**
**    --abs-paths       Display absolute pathnames.
**    --rel-paths       Display pathnames relative to the current working
**                      directory.
**    --sha1sum         Verify file status using SHA1 hashing rather
**                      than relying on file mtimes.
**
** See also: changes, extra, ls
*/
void status_cmd(void){
  int vid;
  db_must_be_within_tree();
       /* 012345678901234 */
  fossil_print("repository:   %s\n", db_repository_filename());
  fossil_print("local-root:   %s\n", g.zLocalRoot);
  if( g.zConfigDbName ){
    fossil_print("config-db:    %s\n", g.zConfigDbName);
  }
  vid = db_lget_int("checkout", 0);
  if( vid ){
    show_common_info(vid, "checkout:", 1, 1);
  }
  db_record_repository_filename(0);
  changes_cmd();
}

/*
** COMMAND: ls
**
** Usage: %fossil ls ?OPTIONS? ?VERSION? ?FILENAMES?
**
** Show the names of all files in the current checkout.  The -v provides
** extra information about each file.  If FILENAMES are included, only
** the files listed (or their children if they are directories) are shown.
**
** Options:
**   --age           Show when each file was committed
**   -v|--verbose    Provide extra information about each file.
**
** See also: changes, extra, status
*/
void ls_cmd(void){
  int vid;
  Stmt q;
  int verboseFlag;
  int showAge;
  char *zOrderBy = "pathname";
  Blob where;
  int i;
  const char *zName;

  verboseFlag = find_option("verbose","v", 0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("l","l", 0)!=0; /* deprecated */
  }
  showAge = find_option("age",0,0)!=0;
  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( find_option("t","t",0)!=0 ){
    if( showAge ){
      zOrderBy = mprintf("checkin_mtime(%d,rid) DESC", vid);
    }else{
      zOrderBy = "mtime DESC";
    }
  }
  verify_all_options();
  blob_zero(&where);
  for(i=2; i<g.argc; i++){
    Blob fname;
    file_tree_name(g.argv[i], &fname, 1);
    zName = blob_str(&fname);
    if( fossil_strcmp(zName, ".")==0 ) {
      blob_reset(&where);
      break;
    }
    blob_appendf(&where, " %s (pathname=%Q %s) "
                 "OR (pathname>'%q/' %s AND pathname<'%q0' %s)",
                 (blob_size(&where)>0) ? "OR" : "WHERE", zName,
                 filename_collation(), zName, filename_collation(),
                 zName, filename_collation());
  }
  vfile_check_signature(vid, 0);
  if( showAge ){
    db_prepare(&q,
       "SELECT pathname, deleted, rid, chnged, coalesce(origname!=pathname,0),"
       "       datetime(checkin_mtime(%d,rid),'unixepoch','localtime')"
       "  FROM vfile %s"
       " ORDER BY %s", vid, blob_str(&where), zOrderBy
    );
  }else{
    db_prepare(&q,
       "SELECT pathname, deleted, rid, chnged, coalesce(origname!=pathname,0)"
       "  FROM vfile %s"
       " ORDER BY %s", blob_str(&where), zOrderBy
    );
  }
  blob_reset(&where);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isNew = db_column_int(&q,2)==0;
    int chnged = db_column_int(&q,3);
    int renamed = db_column_int(&q,4);
    char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    const char *type = "";
    if( verboseFlag ){
      if( isNew ){
        type = "ADDED      ";
      }else if( isDeleted ){
        type = "DELETED    ";
      }else if( !file_wd_isfile_or_link(zFullName) ){
        if( file_access(zFullName, 0)==0 ){
          type = "NOT_A_FILE ";
        }else{
          type = "MISSING    ";
        }
      }else if( chnged ){
        if( chnged==2 ){
          type = "UPDATED_BY_MERGE ";
        }else if( chnged==3 ){
          type = "ADDED_BY_MERGE ";
        }else if( chnged==4 ){
          type = "UPDATED_BY_INTEGRATE ";
        }else if( chnged==5 ){
          type = "ADDED_BY_INTEGRATE ";
        }else if( file_contains_merge_marker(zFullName) ){
          type = "CONFLICT   ";
        }else{
          type = "EDITED     ";
        }
      }else if( renamed ){
        type = "RENAMED    ";
      }else{
        type = "UNCHANGED  ";
      }
    }
    if( showAge ){
      fossil_print("%s%s  %s\n", type, db_column_text(&q, 5), zPathname);
    }else{
      fossil_print("%s%s\n", type, zPathname);
    }
    free(zFullName);
  }
  db_finalize(&q);
}

/*
** Create a TEMP table named SFILE and add all unmanaged files named on
** the command-line to that table.  If directories are named, then add
** all unmanaged files contained underneath those directories.  If there
** are no files or directories named on the command-line, then add all
** unmanaged files anywhere in the checkout.
*/
static void locate_unmanaged_files(
  int argc,           /* Number of command-line arguments to examine */
  char **argv,        /* values of command-line arguments */
  unsigned scanFlags, /* Zero or more SCAN_xxx flags */
  Glob *pIgnore1,     /* Do not add files that match this GLOB */
  Glob *pIgnore2      /* Omit files matching this GLOB too */
){
  Blob name;   /* Name of a candidate file or directory */
  char *zName; /* Name of a candidate file or directory */
  int isDir;   /* 1 for a directory, 0 if doesn't exist, 2 for anything else */
  int i;       /* Loop counter */
  int nRoot;   /* length of g.zLocalRoot */

  db_multi_exec("CREATE TEMP TABLE sfile(x TEXT PRIMARY KEY %s)",
                filename_collation());
  nRoot = (int)strlen(g.zLocalRoot);
  if( argc==0 ){
    blob_init(&name, g.zLocalRoot, nRoot - 1);
    vfile_scan(&name, blob_size(&name), scanFlags, pIgnore1, pIgnore2);
    blob_reset(&name);
  }else{
    for(i=0; i<argc; i++){
      file_canonical_name(argv[i], &name, 0);
      zName = blob_str(&name);
      isDir = file_wd_isdir(zName);
      if( isDir==1 ){
        vfile_scan(&name, nRoot-1, scanFlags, pIgnore1, pIgnore2);
      }else if( isDir==0 ){
        fossil_warning("not found: %s", &zName[nRoot]);
      }else if( file_access(zName, R_OK) ){
        fossil_fatal("cannot open %s", &zName[nRoot]);
      }else{
        db_multi_exec(
           "INSERT OR IGNORE INTO sfile(x) VALUES(%Q)",
           &zName[nRoot]
        );
      }
      blob_reset(&name);
    }
  }
}

/*
** COMMAND: extras
** Usage: %fossil extras ?OPTIONS? ?PATH1 ...?
**
** Print a list of all files in the source tree that are not part of
** the current checkout.  See also the "clean" command. If paths are
** specified, only files in the given directories will be listed.
**
** Files and subdirectories whose names begin with "." are normally
** ignored but can be included by adding the --dotfiles option.
**
** The GLOBPATTERN is a comma-separated list of GLOB expressions for
** files that are ignored.  The GLOBPATTERN specified by the "ignore-glob"
** is used if the --ignore option is omitted.
**
** Pathnames are displayed according to the "relative-paths" setting,
** unless overridden by the --abs-paths or --rel-paths options.
**
** Options:
**    --abs-paths      Display absolute pathnames.
**    --case-sensitive <BOOL> override case-sensitive setting
**    --dotfiles       include files beginning with a dot (".")
**    --ignore <CSG>   ignore files matching patterns from the argument
**    --rel-paths      Display pathnames relative to the current working
**                     directory.
**
** See also: changes, clean, status
*/
void extra_cmd(void){
  Stmt q;
  const char *zIgnoreFlag = find_option("ignore",0,1);
  unsigned scanFlags = find_option("dotfiles",0,0)!=0 ? SCAN_ALL : 0;
  int cwdRelative = 0;
  Glob *pIgnore;
  Blob rewrittenPathname;
  const char *zPathname, *zDisplayName;

  if( find_option("temp",0,0)!=0 ) scanFlags |= SCAN_TEMP;
  capture_case_sensitive_option();
  db_must_be_within_tree();
  cwdRelative = determine_cwd_relative_option();
  if( zIgnoreFlag==0 ){
    zIgnoreFlag = db_get("ignore-glob", 0);
  }
  pIgnore = glob_create(zIgnoreFlag);
  locate_unmanaged_files(g.argc-2, g.argv+2, scanFlags, pIgnore, 0);
  glob_free(pIgnore);
  db_prepare(&q,
      "SELECT x FROM sfile"
      " WHERE x NOT IN (%s)"
      " ORDER BY 1",
      fossil_all_reserved_names(0)
  );
  db_multi_exec("DELETE FROM sfile WHERE x IN (SELECT pathname FROM vfile)");
  blob_zero(&rewrittenPathname);
  while( db_step(&q)==SQLITE_ROW ){
    zDisplayName = zPathname = db_column_text(&q, 0);
    if( cwdRelative ) {
      char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
      file_relative_name(zFullName, &rewrittenPathname, 0);
      free(zFullName);
      zDisplayName = blob_str(&rewrittenPathname);
      if( zDisplayName[0]=='.' && zDisplayName[1]=='/' ){
        zDisplayName += 2;  /* no unnecessary ./ prefix */
      }
    }
    fossil_print("%s\n", zDisplayName);
  }
  blob_reset(&rewrittenPathname);
  db_finalize(&q);
}

/*
** COMMAND: clean
** Usage: %fossil clean ?OPTIONS? ?PATH1 ...?
**
** Delete all "extra" files in the source tree.  "Extra" files are
** files that are not officially part of the checkout. This operation
** cannot be undone. If paths are specified, only the directories or
** files specified will be considered for cleaning.
**
** You will be prompted before removing each eligible file unless the
** --force flag is in use or it matches the --clean option.  The
** GLOBPATTERN specified by the "ignore-glob" setting is used if the
** --ignore option is omitted, the same with "clean-glob" and --clean
** as well as "keep-glob" and --keep.  If you are sure you wish to
** remove all "extra" files except the ones specified with --ignore
** and --keep, you can specify the optional -f|--force flag and no
** prompts will be issued.  If a file matches both --keep and --clean,
** --keep takes precedence.
**
** Files and subdirectories whose names begin with "." are
** normally kept.  They are handled if the "--dotfiles" option
** is used.
**
** Options:
**    --allckouts      Check for empty directories within any checkouts
**                     that may be nested within the current one.  This
**                     option should be used with great care because the
**                     empty-dirs setting (and other applicable settings)
**                     belonging to the other repositories, if any, will
**                     not be checked.
**    --case-sensitive <BOOL> override case-sensitive setting
**    --dirsonly       Only remove empty directories.  No files will
**                     be removed.  Using this option will automatically
**                     enable the --emptydirs option as well.
**    --dotfiles       Include files beginning with a dot (".").
**    --emptydirs      Remove any empty directories that are not
**                     explicitly exempted via the empty-dirs setting
**                     or another applicable setting or command line
**                     argument.  Matching files, if any, are removed
**                     prior to checking for any empty directories;
**                     therefore, directories that contain only files
**                     that were removed will be removed as well.
**    -f|--force       Remove files without prompting.
**    --clean <CSG>    Never prompt for files matching this
**                     comma separated list of glob patterns.
**    --ignore <CSG>   Ignore files matching patterns from the
**                     comma separated list of glob patterns.
**    --keep <CSG>     Keep files matching this comma separated
**                     list of glob patterns.
**    -n|--dry-run     If given, display instead of run actions.
**    --temp           Remove only Fossil-generated temporary files.
**    -v|--verbose     Show all files as they are removed.
**
** See also: addremove, extra, status
*/
void clean_cmd(void){
  int allFileFlag, allDirFlag, dryRunFlag, verboseFlag;
  int emptyDirsFlag, dirsOnlyFlag;
  unsigned scanFlags = 0;
  const char *zIgnoreFlag, *zKeepFlag, *zCleanFlag;
  Glob *pIgnore, *pKeep, *pClean;
  int nRoot;

  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("test",0,0)!=0; /* deprecated */
  }
  if( !dryRunFlag ){
    dryRunFlag = find_option("whatif",0,0)!=0;
  }
  allFileFlag = allDirFlag = find_option("force","f",0)!=0;
  dirsOnlyFlag = find_option("dirsonly",0,0)!=0;
  emptyDirsFlag = find_option("emptydirs","d",0)!=0 || dirsOnlyFlag;
  if( find_option("dotfiles",0,0)!=0 ) scanFlags |= SCAN_ALL;
  if( find_option("temp",0,0)!=0 ) scanFlags |= SCAN_TEMP;
  if( find_option("allckouts",0,0)!=0 ) scanFlags |= SCAN_NESTED;
  zIgnoreFlag = find_option("ignore",0,1);
  verboseFlag = find_option("verbose","v",0)!=0;
  zKeepFlag = find_option("keep",0,1);
  zCleanFlag = find_option("clean",0,1);
  capture_case_sensitive_option();
  db_must_be_within_tree();
  if( zIgnoreFlag==0 ){
    zIgnoreFlag = db_get("ignore-glob", 0);
  }
  if( zKeepFlag==0 ){
    zKeepFlag = db_get("keep-glob", 0);
  }
  if( zCleanFlag==0 ){
    zCleanFlag = db_get("clean-glob", 0);
  }
  verify_all_options();
  pIgnore = glob_create(zIgnoreFlag);
  pKeep = glob_create(zKeepFlag);
  pClean = glob_create(zCleanFlag);
  nRoot = (int)strlen(g.zLocalRoot);
  if( !dirsOnlyFlag ){
    Stmt q;
    Blob repo;
    locate_unmanaged_files(g.argc-2, g.argv+2, scanFlags, pIgnore, pKeep);
    db_prepare(&q,
        "SELECT %Q || x FROM sfile"
        " WHERE x NOT IN (%s)"
        " ORDER BY 1",
        g.zLocalRoot, fossil_all_reserved_names(0)
    );
    if( file_tree_name(g.zRepositoryName, &repo, 0) ){
      db_multi_exec("DELETE FROM sfile WHERE x=%B", &repo);
    }
    db_multi_exec("DELETE FROM sfile WHERE x IN (SELECT pathname FROM vfile)");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      if( !allFileFlag && !dryRunFlag && !glob_match(pClean, zName+nRoot) ){
        Blob ans;
        char cReply;
        char *prompt = mprintf("Remove unmanaged file \"%s\" (a=all/y/N)? ",
                               zName+nRoot);
        blob_zero(&ans);
        prompt_user(prompt, &ans);
        cReply = blob_str(&ans)[0];
        if( cReply=='a' || cReply=='A' ){
          allFileFlag = 1;
        }else if( cReply!='y' && cReply!='Y' ){
          blob_reset(&ans);
          continue;
        }
        blob_reset(&ans);
      }
      if ( dryRunFlag || file_delete(zName)==0 ){
        if( verboseFlag || dryRunFlag ){
          fossil_print("Removed unmanaged file: %s\n", zName+nRoot);
        }
      }else if( verboseFlag ){
        fossil_print("Could not remove file: %s\n", zName+nRoot);
      }
    }
    db_finalize(&q);
  }
  if( emptyDirsFlag ){
    Glob *pEmptyDirs = glob_create(db_get("empty-dirs", 0));
    Stmt q;
    Blob root;
    blob_init(&root, g.zLocalRoot, nRoot - 1);
    vfile_dir_scan(&root, blob_size(&root), scanFlags, pIgnore, pKeep,
                   pEmptyDirs);
    blob_reset(&root);
    db_prepare(&q,
        "SELECT %Q || x FROM dscan_temp"
        " WHERE x NOT IN (%s) AND y = 0"
        " ORDER BY 1 DESC",
        g.zLocalRoot, fossil_all_reserved_names(0)
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      if( !allDirFlag && !dryRunFlag && !glob_match(pClean, zName+nRoot) ){
        Blob ans;
        char cReply;
        char *prompt = mprintf("Remove empty directory \"%s\" (a=all/y/N)? ",
                               zName+nRoot);
        blob_zero(&ans);
        prompt_user(prompt, &ans);
        cReply = blob_str(&ans)[0];
        if( cReply=='a' || cReply=='A' ){
          allDirFlag = 1;
        }else if( cReply!='y' && cReply!='Y' ){
          blob_reset(&ans);
          continue;
        }
        blob_reset(&ans);
      }
      if ( dryRunFlag || file_rmdir(zName)==0 ){
        if( verboseFlag || dryRunFlag ){
          fossil_print("Removed unmanaged directory: %s\n", zName+nRoot);
        }
      }else if( verboseFlag ){
        fossil_print("Could not remove directory: %s\n", zName+nRoot);
      }
    }
    db_finalize(&q);
    glob_free(pEmptyDirs);
  }
  glob_free(pClean);
  glob_free(pKeep);
  glob_free(pIgnore);
}

/*
** Prompt the user for a check-in or stash comment (given in pPrompt),
** gather the response, then return the response in pComment.
**
** Lines of the prompt that begin with # are discarded.  Excess whitespace
** is removed from the reply.
**
** Appropriate encoding translations are made on windows.
*/
void prompt_for_user_comment(Blob *pComment, Blob *pPrompt){
  const char *zEditor;
  char *zCmd;
  char *zFile;
  Blob reply, line;
  char *zComment;
  int i;

  zEditor = db_get("editor", 0);
  if( zEditor==0 ){
    zEditor = fossil_getenv("VISUAL");
  }
  if( zEditor==0 ){
    zEditor = fossil_getenv("EDITOR");
  }
#if defined(_WIN32) || defined(__CYGWIN__)
  if( zEditor==0 ){
    zEditor = mprintf("%s\\notepad.exe", fossil_getenv("SYSTEMROOT"));
#if defined(__CYGWIN__)
    zEditor = fossil_utf8_to_filename(zEditor);
    blob_add_cr(pPrompt);
#endif
  }
#endif
  if( zEditor==0 ){
    blob_append(pPrompt,
       "#\n"
       "# Since no default text editor is set using EDITOR or VISUAL\n"
       "# environment variables or the \"fossil set editor\" command,\n"
       "# and because no comment was specified using the \"-m\" or \"-M\"\n"
       "# command-line options, you will need to enter the comment below.\n"
       "# Type \".\" on a line by itself when you are done:\n", -1);
    zFile = mprintf("-");
  }else{
    Blob fname;
    blob_zero(&fname);
    file_relative_name(g.zLocalRoot, &fname, 1);
    zFile = db_text(0, "SELECT '%qci-comment-' || hex(randomblob(6)) || '.txt'",
                    blob_str(&fname));
    blob_reset(&fname);
  }
#if defined(_WIN32)
  blob_add_cr(pPrompt);
#endif
  blob_write_to_file(pPrompt, zFile);
  if( zEditor ){
    zCmd = mprintf("%s \"%s\"", zEditor, zFile);
    fossil_print("%s\n", zCmd);
    if( fossil_system(zCmd) ){
      fossil_fatal("editor aborted: \"%s\"", zCmd);
    }

    blob_read_from_file(&reply, zFile);
  }else{
    char zIn[300];
    blob_zero(&reply);
    while( fgets(zIn, sizeof(zIn), stdin)!=0 ){
      if( zIn[0]=='.' && (zIn[1]==0 || zIn[1]=='\r' || zIn[1]=='\n') ){
        break;
      }
      blob_append(&reply, zIn, -1);
    }
  }
  blob_to_utf8_no_bom(&reply, 1);
  blob_to_lf_only(&reply);
  file_delete(zFile);
  free(zFile);
  blob_zero(pComment);
  while( blob_line(&reply, &line) ){
    int i, n;
    char *z;
    n = blob_size(&line);
    z = blob_buffer(&line);
    for(i=0; i<n && fossil_isspace(z[i]);  i++){}
    if( i<n && z[i]=='#' ) continue;
    if( i<n || blob_size(pComment)>0 ){
      blob_appendf(pComment, "%b", &line);
    }
  }
  blob_reset(&reply);
  zComment = blob_str(pComment);
  i = strlen(zComment);
  while( i>0 && fossil_isspace(zComment[i-1]) ){ i--; }
  blob_resize(pComment, i);
}

/*
** Prepare a commit comment.  Let the user modify it using the
** editor specified in the global_config table or either
** the VISUAL or EDITOR environment variable.
**
** Store the final commit comment in pComment.  pComment is assumed
** to be uninitialized - any prior content is overwritten.
**
** zInit is the text of the most recent failed attempt to check in
** this same change.  Use zInit to reinitialize the check-in comment
** so that the user does not have to retype.
**
** zBranch is the name of a new branch that this check-in is forced into.
** zBranch might be NULL or an empty string if no forcing occurs.
**
** parent_rid is the recordid of the parent check-in.
*/
static void prepare_commit_comment(
  Blob *pComment,
  char *zInit,
  CheckinInfo *p,
  int parent_rid
){
  Blob prompt;
#if defined(_WIN32) || defined(__CYGWIN__)
  int bomSize;
  const unsigned char *bom = get_utf8_bom(&bomSize);
  blob_init(&prompt, (const char *) bom, bomSize);
  if( zInit && zInit[0]) {
    blob_append(&prompt, zInit, -1);
  }
#else
  blob_init(&prompt, zInit, -1);
#endif
  blob_append(&prompt,
    "\n"
    "# Enter commit message for this check-in. Lines beginning with # are ignored.\n"
    "#\n", -1
  );
  blob_appendf(&prompt, "# user: %s\n", p->zUserOvrd ? p->zUserOvrd : g.zLogin);
  if( p->zBranch && p->zBranch[0] ){
    blob_appendf(&prompt, "# tags: %s\n#\n", p->zBranch);
  }else{
    char *zTags = info_tags_of_checkin(parent_rid, 1);
    if( zTags )  blob_appendf(&prompt, "# tags: %z\n#\n", zTags);
  }
  status_report(&prompt, "# ", 1, 0);
  if( g.markPrivate ){
    blob_append(&prompt,
      "# PRIVATE BRANCH: This check-in will be private and will not sync to\n"
      "# repositories.\n"
      "#\n", -1
    );
  }
  prompt_for_user_comment(pComment, &prompt);
  blob_reset(&prompt);
}

/*
** Populate the Global.aCommitFile[] based on the command line arguments
** to a [commit] command. Global.aCommitFile is an array of integers
** sized at (N+1), where N is the number of arguments passed to [commit].
** The contents are the [id] values from the vfile table corresponding
** to the filenames passed as arguments.
**
** The last element of aCommitFile[] is always 0 - indicating the end
** of the array.
**
** If there were no arguments passed to [commit], aCommitFile is not
** allocated and remains NULL. Other parts of the code interpret this
** to mean "all files".
**
** Returns 1 if there was a warning, 0 otherwise.
*/
int select_commit_files(void){
  int result = 0;
  assert( g.aCommitFile==0 );
  if( g.argc>2 ){
    int ii, jj=0;
    Blob fname;
    Stmt q;
    const char *zCollate;
    Bag toCommit;

    zCollate = filename_collation();
    blob_zero(&fname);
    bag_init(&toCommit);
    for(ii=2; ii<g.argc; ii++){
      int cnt = 0;
      file_tree_name(g.argv[ii], &fname, 1);
      if( fossil_strcmp(blob_str(&fname),".")==0 ){
        bag_clear(&toCommit);
        return result;
      }
      db_prepare(&q,
        "SELECT id FROM vfile WHERE pathname=%Q %s"
        " OR (pathname>'%q/' %s AND pathname<'%q0' %s)",
        blob_str(&fname), zCollate, blob_str(&fname),
        zCollate, blob_str(&fname), zCollate);
      while( db_step(&q)==SQLITE_ROW ){
        cnt++;
        bag_insert(&toCommit, db_column_int(&q, 0));
      }
      db_finalize(&q);
      if( cnt==0 ){
        fossil_warning("fossil knows nothing about: %s", g.argv[ii]);
        result = 1;
      }
      blob_reset(&fname);
    }
    g.aCommitFile = fossil_malloc( (bag_count(&toCommit)+1) * sizeof(g.aCommitFile[0]) );
    for(ii=bag_first(&toCommit); ii>0; ii=bag_next(&toCommit, ii)){
      g.aCommitFile[jj++] = ii;
    }
    g.aCommitFile[jj] = 0;
    bag_clear(&toCommit);
  }
  return result;
}

/*
** Make sure the current check-in with timestamp zDate is younger than its
** ancestor identified rid and zUuid.  Throw a fatal error if not.
*/
static void checkin_verify_younger(
  int rid,              /* The record ID of the ancestor */
  const char *zUuid,    /* The artifact ID of the ancestor */
  const char *zDate     /* Date & time of the current check-in */
){
#ifndef FOSSIL_ALLOW_OUT_OF_ORDER_DATES
  int b;
  b = db_exists(
    "SELECT 1 FROM event"
    " WHERE datetime(mtime)>=%Q"
    "   AND type='ci' AND objid=%d",
    zDate, rid
  );
  if( b ){
    fossil_fatal("ancestor check-in [%.10s] (%s) is not older (clock skew?)"
                 " Use --allow-older to override.", zUuid, zDate);
  }
#endif
}

/*
** zDate should be a valid date string.  Convert this string into the
** format YYYY-MM-DDTHH:MM:SS.  If the string is not a valid date,
** print a fatal error and quit.
*/
char *date_in_standard_format(const char *zInputDate){
  char *zDate;
  if( g.perm.Setup && fossil_strcmp(zInputDate,"now")==0 ){
    zInputDate = PD("date_override","now");
  }
  zDate = db_text(0, "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%f',%Q)",
                  zInputDate);
  if( zDate[0]==0 ){
    fossil_fatal(
      "unrecognized date format (%s): use \"YYYY-MM-DD HH:MM:SS.SSS\"",
      zInputDate
    );
  }
  return zDate;
}

/*
** COMMAND: test-date-format
**
** Usage: %fossil test-date-format DATE-STRING...
**
** Convert the DATE-STRING into the standard format used in artifacts
** and display the result.
*/
void test_date_format(void){
  int i;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  for(i=2; i<g.argc; i++){
    fossil_print("%s -> %s\n", g.argv[i], date_in_standard_format(g.argv[i]));
  }
}

#if INTERFACE
/*
** The following structure holds some of the information needed to construct a
** check-in manifest.
*/
struct CheckinInfo {
  Blob *pComment;             /* Check-in comment text */
  const char *zMimetype;      /* Mimetype of check-in command.  May be NULL */
  int verifyDate;             /* Verify that child is younger */
  int closeFlag;              /* Close the branch being committed */
  Blob *pCksum;               /* Repository checksum.  May be 0 */
  const char *zDateOvrd;      /* Date override.  If 0 then use 'now' */
  const char *zUserOvrd;      /* User override.  If 0 then use g.zLogin */
  const char *zBranch;        /* Branch name.  May be 0 */
  const char *zColor;         /* One-time background color.  May be 0 */
  const char *zBrClr;         /* Persistent branch color.  May be 0 */
  const char **azTag;         /* Tags to apply to this check-in */
};
#endif /* INTERFACE */

/*
** Create a manifest.
*/
static void create_manifest(
  Blob *pOut,                 /* Write the manifest here */
  const char *zBaselineUuid,  /* UUID of baseline, or zero */
  Manifest *pBaseline,        /* Make it a delta manifest if not zero */
  int vid,                    /* BLOB.id for the parent check-in */
  CheckinInfo *p,             /* Information about the check-in */
  int *pnFBcard               /* OUT: Number of generated B- and F-cards */
){
  char *zDate;                /* Date of the check-in */
  char *zParentUuid;          /* UUID of parent check-in */
  Blob filename;              /* A single filename */
  int nBasename;              /* Size of base filename */
  Stmt q;                     /* Various queries */
  Blob mcksum;                /* Manifest checksum */
  ManifestFile *pFile;        /* File from the baseline */
  int nFBcard = 0;            /* Number of B-cards and F-cards */
  int i;                      /* Loop counter */
  const char *zColor;         /* Modified value of p->zColor */

  assert( pBaseline==0 || pBaseline->zBaseline==0 );
  assert( pBaseline==0 || zBaselineUuid!=0 );
  blob_zero(pOut);
  zParentUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d AND "
    "EXISTS(SELECT 1 FROM event WHERE event.type='ci' and event.objid=%d)",
    vid, vid);
  if( !zParentUuid ){
    fossil_fatal("Could not find a valid check-in for RID %d. "
                 "Possible checkout/repo mismatch.", vid);
  }
  if( pBaseline ){
    blob_appendf(pOut, "B %s\n", zBaselineUuid);
    manifest_file_rewind(pBaseline);
    pFile = manifest_file_next(pBaseline, 0);
    nFBcard++;
  }else{
    pFile = 0;
  }
  if( blob_size(p->pComment)!=0 ){
    blob_appendf(pOut, "C %F\n", blob_str(p->pComment));
  }else{
    blob_append(pOut, "C (no\\scomment)\n", 16);
  }
  zDate = date_in_standard_format(p->zDateOvrd ? p->zDateOvrd : "now");
  blob_appendf(pOut, "D %s\n", zDate);
  zDate[10] = ' ';
  db_prepare(&q,
    "SELECT pathname, uuid, origname, blob.rid, isexe, islink,"
    "       is_selected(vfile.id)"
    "  FROM vfile JOIN blob ON vfile.mrid=blob.rid"
    " WHERE (NOT deleted OR NOT is_selected(vfile.id))"
    "   AND vfile.vid=%d"
    " ORDER BY if_selected(vfile.id, pathname, origname)",
    vid);
  blob_zero(&filename);
  blob_appendf(&filename, "%s", g.zLocalRoot);
  nBasename = blob_size(&filename);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zOrig = db_column_text(&q, 2);
    int frid = db_column_int(&q, 3);
    int isExe = db_column_int(&q, 4);
    int isLink = db_column_int(&q, 5);
    int isSelected = db_column_int(&q, 6);
    const char *zPerm;
    int cmp;

    blob_resize(&filename, nBasename);
    blob_append(&filename, zName, -1);

#if !defined(_WIN32)
    /* For unix, extract the "executable" and "symlink" permissions
    ** directly from the filesystem.  On windows, permissions are
    ** unchanged from the original.  However, only do this if the file
    ** itself is actually selected to be part of this check-in.
    */
    if( isSelected ){
      int mPerm;

      mPerm = file_wd_perm(blob_str(&filename));
      isExe = ( mPerm==PERM_EXE );
      isLink = ( mPerm==PERM_LNK );
    }
#endif
    if( isExe ){
      zPerm = " x";
    }else if( isLink ){
      zPerm = " l"; /* note: symlinks don't have executable bit on unix */
    }else{
      zPerm = "";
    }
    if( !g.markPrivate ) content_make_public(frid);
    while( pFile && fossil_strcmp(pFile->zName,zName)<0 ){
      blob_appendf(pOut, "F %F\n", pFile->zName);
      pFile = manifest_file_next(pBaseline, 0);
      nFBcard++;
    }
    cmp = 1;
    if( pFile==0
      || (cmp = fossil_strcmp(pFile->zName,zName))!=0
      || fossil_strcmp(pFile->zUuid, zUuid)!=0
    ){
      if( zOrig && !isSelected ){ zName = zOrig; zOrig = 0; }
      if( zOrig==0 || fossil_strcmp(zOrig,zName)==0 ){
        blob_appendf(pOut, "F %F %s%s\n", zName, zUuid, zPerm);
      }else{
        if( zPerm[0]==0 ){ zPerm = " w"; }
        blob_appendf(pOut, "F %F %s%s %F\n", zName, zUuid, zPerm, zOrig);
      }
      nFBcard++;
    }
    if( cmp==0 ) pFile = manifest_file_next(pBaseline,0);
  }
  blob_reset(&filename);
  db_finalize(&q);
  while( pFile ){
    blob_appendf(pOut, "F %F\n", pFile->zName);
    pFile = manifest_file_next(pBaseline, 0);
    nFBcard++;
  }
  if( p->zMimetype && p->zMimetype[0] ){
    blob_appendf(pOut, "N %F\n", p->zMimetype);
  }
  if( zParentUuid ){
    blob_appendf(pOut, "P %s", zParentUuid);
    if( p->verifyDate ) checkin_verify_younger(vid, zParentUuid, zDate);
    free(zParentUuid);
    db_prepare(&q, "SELECT merge FROM vmerge WHERE id=0 OR id<-2");
    while( db_step(&q)==SQLITE_ROW ){
      char *zMergeUuid;
      int mid = db_column_int(&q, 0);
      if( (!g.markPrivate && content_is_private(mid)) || (mid == vid) ) continue;
      zMergeUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", mid);
      if( zMergeUuid ){
        blob_appendf(pOut, " %s", zMergeUuid);
        if( p->verifyDate ) checkin_verify_younger(mid, zMergeUuid, zDate);
        free(zMergeUuid);
      }
    }
    db_finalize(&q);
    blob_appendf(pOut, "\n");
  }
  free(zDate);

  db_prepare(&q,
    "SELECT CASE vmerge.id WHEN -1 THEN '+' ELSE '-' END || blob.uuid, merge"
    "  FROM vmerge, blob"
    " WHERE (vmerge.id=-1 OR vmerge.id=-2)"
    "   AND blob.rid=vmerge.merge"
    " ORDER BY 1");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zCherrypickUuid = db_column_text(&q, 0);
    int mid = db_column_int(&q, 1);
    if( mid != vid ){
      blob_appendf(pOut, "Q %s\n", zCherrypickUuid);
    }
  }
  db_finalize(&q);

  if( p->pCksum ) blob_appendf(pOut, "R %b\n", p->pCksum);
  zColor = p->zColor;
  if( p->zBranch && p->zBranch[0] ){
    /* Set tags for the new branch */
    if( p->zBrClr && p->zBrClr[0] ){
      zColor = 0;
      blob_appendf(pOut, "T *bgcolor * %F\n", p->zBrClr);
    }
    blob_appendf(pOut, "T *branch * %F\n", p->zBranch);
    blob_appendf(pOut, "T *sym-%F *\n", p->zBranch);
  }
  if( zColor && zColor[0] ){
    /* One-time background color */
    blob_appendf(pOut, "T +bgcolor * %F\n", zColor);
  }
  if( p->closeFlag ){
    blob_appendf(pOut, "T +closed *\n");
  }
  db_prepare(&q, "SELECT uuid,merge FROM vmerge JOIN blob ON merge=rid"
                 " WHERE id=-4 ORDER BY 1");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zIntegrateUuid = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    if( is_a_leaf(rid) && !db_exists("SELECT 1 FROM tagxref "
        " WHERE tagid=%d AND rid=%d AND tagtype>0", TAG_CLOSED, rid)){
      blob_appendf(pOut, "T +closed %s\n", zIntegrateUuid);
    }
  }
  db_finalize(&q);

  if( p->azTag ){
    for(i=0; p->azTag[i]; i++){
      /* Add a symbolic tag to this check-in.  The tag names have already
      ** been sorted and converted using the %F format */
      assert( i==0 || strcmp(p->azTag[i-1], p->azTag[i])<=0 );
      blob_appendf(pOut, "T +sym-%s *\n", p->azTag[i]);
    }
  }
  if( p->zBranch && p->zBranch[0] ){
    /* For a new branch, cancel all prior propagating tags */
    db_prepare(&q,
        "SELECT tagname FROM tagxref, tag"
        " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
        "   AND tagtype==2 AND tagname GLOB 'sym-*'"
        "   AND tagname!='sym-'||%Q"
        " ORDER BY tagname",
        vid, p->zBranch);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zBrTag = db_column_text(&q, 0);
      blob_appendf(pOut, "T -%F *\n", zBrTag);
    }
    db_finalize(&q);
  }
  blob_appendf(pOut, "U %F\n", p->zUserOvrd ? p->zUserOvrd : g.zLogin);
  md5sum_blob(pOut, &mcksum);
  blob_appendf(pOut, "Z %b\n", &mcksum);
  if( pnFBcard ) *pnFBcard = nFBcard;
}

/*
** Issue a warning and give the user an opportunity to abandon out
** if a Unicode (UTF-16) byte-order-mark (BOM) or a \r\n line ending
** is seen in a text file.
**
** Return 1 if the user pressed 'c'. In that case, the file will have
** been converted to UTF-8 (if it was UTF-16) with NL line-endings,
** and the original file will have been renamed to "<filename>-original".
*/
static int commit_warning(
  Blob *p,              /* The content of the file being committed. */
  int crnlOk,           /* Non-zero if CR/NL warnings should be disabled. */
  int binOk,            /* Non-zero if binary warnings should be disabled. */
  int encodingOk,       /* Non-zero if encoding warnings should be disabled. */
  const char *zFilename /* The full name of the file being committed. */
){
  int bReverse;           /* UTF-16 byte order is reversed? */
  int fUnicode;           /* return value of could_be_utf16() */
  int fBinary;            /* does the blob content appear to be binary? */
  int lookFlags;          /* output flags from looks_like_utf8/utf16() */
  int fHasAnyCr;          /* the blob contains one or more CR chars */
  int fHasLoneCrOnly;     /* all detected line endings are CR only */
  int fHasCrLfOnly;       /* all detected line endings are CR/LF pairs */
  char *zMsg;             /* Warning message */
  Blob fname;             /* Relative pathname of the file */
  static int allOk = 0;   /* Set to true to disable this routine */

  if( allOk ) return 0;
  fUnicode = could_be_utf16(p, &bReverse);
  if( fUnicode ){
    lookFlags = looks_like_utf16(p, bReverse, LOOK_NUL);
  }else{
    lookFlags = looks_like_utf8(p, LOOK_NUL);
  }
  fHasAnyCr = (lookFlags & LOOK_CR);
  fBinary = (lookFlags & LOOK_BINARY);
  fHasLoneCrOnly = ((lookFlags & LOOK_EOL) == LOOK_LONE_CR);
  fHasCrLfOnly = ((lookFlags & LOOK_EOL) == LOOK_CRLF);
  if( fUnicode || fHasAnyCr || fBinary ){
    const char *zWarning;
    const char *zDisable;
    const char *zConvert = "c=convert/";
    Blob ans;
    char cReply;

    if( fBinary ){
      int fHasNul = (lookFlags & LOOK_NUL); /* contains NUL chars? */
      int fHasLong = (lookFlags & LOOK_LONG); /* overly long line? */
      if( binOk ){
        return 0; /* We don't want binary warnings for this file. */
      }
      if( !fHasNul && fHasLong ){
        zWarning = "long lines";
        zConvert = ""; /* We cannot convert binary files. */
      }else{
        zWarning = "binary data";
        zConvert = ""; /* We cannot convert binary files. */
      }
      zDisable = "\"binary-glob\" setting";
    }else if( fUnicode && fHasAnyCr ){
      if( crnlOk && encodingOk ){
        return 0; /* We don't want CR/NL and Unicode warnings for this file. */
      }
      if( fHasLoneCrOnly ){
        zWarning = "CR line endings and Unicode";
      }else if( fHasCrLfOnly ){
        zWarning = "CR/NL line endings and Unicode";
      }else{
        zWarning = "mixed line endings and Unicode";
      }
      zDisable = "\"crnl-glob\" and \"encoding-glob\" settings";
    }else if( fHasAnyCr ){
      if( crnlOk ){
        return 0; /* We don't want CR/NL warnings for this file. */
      }
      if( fHasLoneCrOnly ){
        zWarning = "CR line endings";
      }else if( fHasCrLfOnly ){
        zWarning = "CR/NL line endings";
      }else{
        zWarning = "mixed line endings";
      }
      zDisable = "\"crnl-glob\" setting";
    }else{
      if( encodingOk ){
        return 0; /* We don't want encoding warnings for this file. */
      }
      zWarning = "Unicode";
#if !defined(_WIN32) && !defined(__CYGWIN__)
      zConvert = ""; /* On Unix, we cannot easily convert Unicode files. */
#endif
      zDisable = "\"encoding-glob\" setting";
    }
    file_relative_name(zFilename, &fname, 0);
    blob_zero(&ans);
    zMsg = mprintf(
         "%s contains %s. Use --no-warnings or the %s to disable this warning.\n"
         "Commit anyhow (a=all/%sy/N)? ",
         blob_str(&fname), zWarning, zDisable, zConvert);
    prompt_user(zMsg, &ans);
    fossil_free(zMsg);
    cReply = blob_str(&ans)[0];
    if( cReply=='a' || cReply=='A' ){
      allOk = 1;
    }else if( *zConvert && (cReply=='c' || cReply=='C') ){
      char *zOrig = file_newname(zFilename, "original", 1);
      FILE *f;
      blob_write_to_file(p, zOrig);
      fossil_free(zOrig);
      f = fossil_fopen(zFilename, "wb");
      if( fUnicode ) {
        int bomSize;
        const unsigned char *bom = get_utf8_bom(&bomSize);
        fwrite(bom, 1, bomSize, f);
        blob_to_utf8_no_bom(p, 0);
      }
      if( fHasAnyCr ){
        blob_to_lf_only(p);
      }
      fwrite(blob_buffer(p), 1, blob_size(p), f);
      fclose(f);
      return 1;
    }else if( cReply!='y' && cReply!='Y' ){
      fossil_fatal("Abandoning commit due to %s in %s",
                   zWarning, blob_str(&fname));
    }
    blob_reset(&ans);
    blob_reset(&fname);
  }
  return 0;
}

/*
** qsort() comparison routine for an array of pointers to strings.
*/
static int tagCmp(const void *a, const void *b){
  char **pA = (char**)a;
  char **pB = (char**)b;
  return fossil_strcmp(pA[0], pB[0]);
}

/*
** COMMAND: ci*
** COMMAND: commit
**
** Usage: %fossil commit ?OPTIONS? ?FILE...?
**
** Create a new version containing all of the changes in the current
** checkout.  You will be prompted to enter a check-in comment unless
** the comment has been specified on the command-line using "-m" or a
** file containing the comment using -M.  The editor defined in the
** "editor" fossil option (see %fossil help set) will be used, or from
** the "VISUAL" or "EDITOR" environment variables (in that order) if
** no editor is set.
**
** All files that have changed will be committed unless some subset of
** files is specified on the command line.
**
** The --branch option followed by a branch name causes the new
** check-in to be placed in a newly-created branch with the name
** passed to the --branch option.
**
** Use the --branchcolor option followed by a color name (ex:
** '#ffc0c0') to specify the background color of entries in the new
** branch when shown in the web timeline interface.  The use of
** the --branchcolor option is not recommend.  Instead, let Fossil
** choose the branch color automatically.
**
** The --bgcolor option works like --branchcolor but only sets the
** background color for a single check-in.  Subsequent check-ins revert
** to the default color.
**
** A check-in is not permitted to fork unless the --allow-fork option
** appears.  An empty check-in (i.e. with nothing changed) is not
** allowed unless the --allow-empty option appears.  A check-in may not
** be older than its ancestor unless the --allow-older option appears.
** If any of files in the check-in appear to contain unresolved merge
** conflicts, the check-in will not be allowed unless the
** --allow-conflict option is present.  In addition, the entire
** check-in process may be aborted if a file contains content that
** appears to be binary, Unicode text, or text with CR/NL line endings
** unless the interactive user chooses to proceed.  If there is no
** interactive user or these warnings should be skipped for some other
** reason, the --no-warnings option may be used.  A check-in is not
** allowed against a closed leaf.
**
** The --private option creates a private check-in that is never synced.
** Children of private check-ins are automatically private.
**
** the --tag option applies the symbolic tag name to the check-in.
**
** Options:
**    --allow-conflict           allow unresolved merge conflicts
**    --allow-empty              allow a commit with no changes
**    --allow-fork               allow the commit to fork
**    --allow-older              allow a commit older than its ancestor
**    --baseline                 use a baseline manifest in the commit process
**    --bgcolor COLOR            apply COLOR to this one check-in only
**    --branch NEW-BRANCH-NAME   check in to this new branch
**    --branchcolor COLOR        apply given COLOR to the branch
**    --close                    close the branch being committed
**    --delta                    use a delta manifest in the commit process
**    -m|--comment COMMENT-TEXT  use COMMENT-TEXT as commit comment
**    -M|--message-file FILE     read the commit comment from given file
**    --mimetype MIMETYPE        mimetype of check-in comment
**    -n|--dry-run               If given, display instead of run actions
**    --no-warnings              omit all warnings about file contents
**    --nosign                   do not attempt to sign this commit with gpg
**    --private                  do not sync changes and their descendants
**    --tag TAG-NAME             assign given tag TAG-NAME to the checkin
**
** See also: branch, changes, checkout, extra, sync
*/
void commit_cmd(void){
  int hasChanges;        /* True if unsaved changes exist */
  int vid;               /* blob-id of parent version */
  int nrid;              /* blob-id of a modified file */
  int nvid;              /* Blob-id of the new check-in */
  Blob comment;          /* Check-in comment */
  const char *zComment;  /* Check-in comment */
  Stmt q;                /* Various queries */
  char *zUuid;           /* UUID of the new check-in */
  int noSign = 0;        /* True to omit signing the manifest using GPG */
  int isAMerge = 0;      /* True if checking in a merge */
  int noWarningFlag = 0; /* True if skipping all warnings */
  int forceFlag = 0;     /* Undocumented: Disables all checks */
  int forceDelta = 0;    /* Force a delta-manifest */
  int forceBaseline = 0; /* Force a baseline-manifest */
  int allowConflict = 0; /* Allow unresolve merge conflicts */
  int allowEmpty = 0;    /* Allow a commit with no changes */
  int allowFork = 0;     /* Allow the commit to fork */
  int allowOlder = 0;    /* Allow a commit older than its ancestor */
  char *zManifestFile;   /* Name of the manifest file */
  int useCksum;          /* True if checksums should be computed and verified */
  int outputManifest;    /* True to output "manifest" and "manifest.uuid" */
  int dryRunFlag;        /* True for a test run.  Debugging only */
  CheckinInfo sCiInfo;   /* Information about this check-in */
  const char *zComFile;  /* Read commit message from this file */
  int nTag = 0;          /* Number of --tag arguments */
  const char *zTag;      /* A single --tag argument */
  Blob manifest;         /* Manifest in baseline form */
  Blob muuid;            /* Manifest uuid */
  Blob cksum1, cksum2;   /* Before and after commit checksums */
  Blob cksum1b;          /* Checksum recorded in the manifest */
  int szD;               /* Size of the delta manifest */
  int szB;               /* Size of the baseline manifest */
  int nConflict = 0;     /* Number of unresolved merge conflicts */
  int abortCommit = 0;
  Blob ans;
  char cReply;

  memset(&sCiInfo, 0, sizeof(sCiInfo));
  url_proxy_options();
  noSign = find_option("nosign",0,0)!=0;
  forceDelta = find_option("delta",0,0)!=0;
  forceBaseline = find_option("baseline",0,0)!=0;
  if( forceDelta && forceBaseline ){
    fossil_fatal("cannot use --delta and --baseline together");
  }
  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("test",0,0)!=0; /* deprecated */
  }
  zComment = find_option("comment","m",1);
  forceFlag = find_option("force", "f", 0)!=0;
  allowConflict = find_option("allow-conflict",0,0)!=0;
  allowEmpty = find_option("allow-empty",0,0)!=0;
  allowFork = find_option("allow-fork",0,0)!=0;
  allowOlder = find_option("allow-older",0,0)!=0;
  noWarningFlag = find_option("no-warnings", 0, 0)!=0;
  sCiInfo.zBranch = find_option("branch","b",1);
  sCiInfo.zColor = find_option("bgcolor",0,1);
  sCiInfo.zBrClr = find_option("branchcolor",0,1);
  sCiInfo.closeFlag = find_option("close",0,0)!=0;
  sCiInfo.zMimetype = find_option("mimetype",0,1);
  while( (zTag = find_option("tag",0,1))!=0 ){
    if( zTag[0]==0 ) continue;
    sCiInfo.azTag = fossil_realloc((void*)sCiInfo.azTag, sizeof(char*)*(nTag+2));
    sCiInfo.azTag[nTag++] = zTag;
    sCiInfo.azTag[nTag] = 0;
  }
  zComFile = find_option("message-file", "M", 1);
  if( find_option("private",0,0) ){
    g.markPrivate = 1;
    if( sCiInfo.zBranch==0 ) sCiInfo.zBranch = "private";
    if( sCiInfo.zBrClr==0 && sCiInfo.zColor==0 ){
      sCiInfo.zBrClr = "#fec084";  /* Orange */
    }
  }
  sCiInfo.zDateOvrd = find_option("date-override",0,1);
  sCiInfo.zUserOvrd = find_option("user-override",0,1);
  db_must_be_within_tree();
  noSign = db_get_boolean("omitsign", 0)|noSign;
  if( db_get_boolean("clearsign", 0)==0 ){ noSign = 1; }
  useCksum = db_get_boolean("repo-cksum", 1);
  outputManifest = db_get_boolean("manifest", 0);
  verify_all_options();

  /* Escape special characters in tags and put all tags in sorted order */
  if( nTag ){
    int i;
    for(i=0; i<nTag; i++) sCiInfo.azTag[i] = mprintf("%F", sCiInfo.azTag[i]);
    qsort((void*)sCiInfo.azTag, nTag, sizeof(sCiInfo.azTag[0]), tagCmp);
  }

  /* So that older versions of Fossil (that do not understand delta-
  ** manifest) can continue to use this repository, do not create a new
  ** delta-manifest unless this repository already contains one or more
  ** delta-manifests, or unless the delta-manifest is explicitly requested
  ** by the --delta option.
  */
  if( !forceDelta && !db_get_boolean("seen-delta-manifest",0) ){
    forceBaseline = 1;
  }

  /* Get the ID of the parent manifest artifact */
  vid = db_lget_int("checkout", 0);
  if( content_is_private(vid) ){
    g.markPrivate = 1;
  }

  /*
  ** Autosync if autosync is enabled and this is not a private check-in.
  */
  if( !g.markPrivate ){
    if( autosync(SYNC_PULL) ){
      blob_zero(&ans);
      prompt_user("continue in spite of sync failure (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      if( cReply!='y' && cReply!='Y' ){
        fossil_exit(1);
      }
    }
  }

  /* Require confirmation to continue with the check-in if there is
  ** clock skew
  */
  if( g.clockSkewSeen ){
    blob_zero(&ans);
    prompt_user("continue in spite of time skew (y/N)? ", &ans);
    cReply = blob_str(&ans)[0];
    if( cReply!='y' && cReply!='Y' ){
      fossil_exit(1);
    }
  }

  /* There are two ways this command may be executed. If there are
  ** no arguments following the word "commit", then all modified files
  ** in the checked out directory are committed. If one or more arguments
  ** follows "commit", then only those files are committed.
  **
  ** After the following function call has returned, the Global.aCommitFile[]
  ** array is allocated to contain the "id" field from the vfile table
  ** for each file to be committed. Or, if aCommitFile is NULL, all files
  ** should be committed.
  */
  if( select_commit_files() ){
    blob_zero(&ans);
    prompt_user("continue (y/N)? ", &ans);
    cReply = blob_str(&ans)[0];
    if( cReply!='y' && cReply!='Y' ) fossil_exit(1);;
  }
  isAMerge = db_exists("SELECT 1 FROM vmerge WHERE id=0 OR id<-2");
  if( g.aCommitFile && isAMerge ){
    fossil_fatal("cannot do a partial commit of a merge");
  }

  /* Doing "fossil mv fileA fileB; fossil add fileA; fossil commit fileA"
  ** will generate a manifest that has two fileA entries, which is illegal.
  ** When you think about it, the sequence above makes no sense.  So detect
  ** it and disallow it.  Ticket [0ff64b0a5fc8].
  */
  if( g.aCommitFile ){
    db_prepare(&q,
       "SELECT v1.pathname, v2.pathname"
       "  FROM vfile AS v1, vfile AS v2"
       " WHERE is_selected(v1.id)"
       "   AND v2.origname IS NOT NULL"
       "   AND v2.origname=v1.pathname"
       "   AND NOT is_selected(v2.id)");
    if( db_step(&q)==SQLITE_ROW ){
      const char *zFrom = db_column_text(&q, 0);
      const char *zTo = db_column_text(&q, 1);
      fossil_fatal("cannot do a partial commit of '%s' without '%s' because "
                   "'%s' was renamed to '%s'", zFrom, zTo, zFrom, zTo);
    }
    db_finalize(&q);
  }

  user_select();
  /*
  ** Check that the user exists.
  */
  if( !db_exists("SELECT 1 FROM user WHERE login=%Q", g.zLogin) ){
    fossil_fatal("no such user: %s", g.zLogin);
  }

  hasChanges = unsaved_changes();
  db_begin_transaction();
  db_record_repository_filename(0);
  if( hasChanges==0 && !isAMerge && !allowEmpty && !forceFlag ){
    fossil_fatal("nothing has changed; use --allow-empty to override");
  }

  /* If none of the files that were named on the command line have
  ** been modified, bail out now unless the --allow-empty or --force
  ** flags is used.
  */
  if( g.aCommitFile
   && !allowEmpty
   && !forceFlag
   && !db_exists(
        "SELECT 1 FROM vfile "
        " WHERE is_selected(id)"
        "   AND (chnged OR deleted OR rid=0 OR pathname!=origname)")
  ){
    fossil_fatal("none of the selected files have changed; use "
                 "--allow-empty to override.");
  }

  /*
  ** Do not allow a commit that will cause a fork unless the --allow-fork
  ** or --force flags is used, or unless this is a private check-in.
  */
  if( sCiInfo.zBranch==0 && allowFork==0 && forceFlag==0
    && g.markPrivate==0 && !is_a_leaf(vid)
  ){
    fossil_fatal("would fork.  \"update\" first or use --allow-fork.");
  }

  /*
  ** Do not allow a commit against a closed leaf
  */
  if( db_exists("SELECT 1 FROM tagxref"
                " WHERE tagid=%d AND rid=%d AND tagtype>0",
                TAG_CLOSED, vid) ){
    fossil_fatal("cannot commit against a closed leaf");
  }

  if( useCksum ) vfile_aggregate_checksum_disk(vid, &cksum1);
  if( zComment ){
    blob_zero(&comment);
    blob_append(&comment, zComment, -1);
  }else if( zComFile ){
    blob_zero(&comment);
    blob_read_from_file(&comment, zComFile);
    blob_to_utf8_no_bom(&comment, 1);
  }else if(dryRunFlag){
    blob_zero(&comment);
  }else{
    char *zInit = db_text(0, "SELECT value FROM vvar WHERE name='ci-comment'");
    prepare_commit_comment(&comment, zInit, &sCiInfo, vid);
    if( zInit && zInit[0] && fossil_strcmp(zInit, blob_str(&comment))==0 ){
      blob_zero(&ans);
      prompt_user("unchanged check-in comment.  continue (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      if( cReply!='y' && cReply!='Y' ) fossil_exit(1);;
    }
    free(zInit);
  }
  if( blob_size(&comment)==0 ){
    if( !dryRunFlag ){
      blob_zero(&ans);
      prompt_user("empty check-in comment.  continue (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      if( cReply!='y' && cReply!='Y' ){
        fossil_exit(1);
      }
    }
  }else{
    db_multi_exec("REPLACE INTO vvar VALUES('ci-comment',%B)", &comment);
    db_end_transaction(0);
    db_begin_transaction();
  }

  /* Step 1: Insert records for all modified files into the blob
  ** table. If there were arguments passed to this command, only
  ** the identified files are inserted (if they have been modified).
  */
  db_prepare(&q,
    "SELECT id, %Q || pathname, mrid, %s, chnged, %s, %s FROM vfile "
    "WHERE chnged==1 AND NOT deleted AND is_selected(id)",
    g.zLocalRoot,
    glob_expr("pathname", db_get("crnl-glob","")),
    glob_expr("pathname", db_get("binary-glob","")),
    glob_expr("pathname", db_get("encoding-glob",""))
  );
  while( db_step(&q)==SQLITE_ROW ){
    int id, rid;
    const char *zFullname;
    Blob content;
    int crnlOk, binOk, encodingOk, chnged;

    id = db_column_int(&q, 0);
    zFullname = db_column_text(&q, 1);
    rid = db_column_int(&q, 2);
    crnlOk = db_column_int(&q, 3);
    chnged = db_column_int(&q, 4);
    binOk = db_column_int(&q, 5);
    encodingOk = db_column_int(&q, 6);

    blob_zero(&content);
    if( file_wd_islink(zFullname) ){
      /* Instead of file content, put link destination path */
      blob_read_link(&content, zFullname);
    }else{
      blob_read_from_file(&content, zFullname);
    }
    /* Do not emit any warnings when they are disabled. */
    if( !noWarningFlag ){
      abortCommit |= commit_warning(&content, crnlOk, binOk,
                                    encodingOk, zFullname);
    }
    if( chnged==1 && contains_merge_marker(&content) ){
      Blob fname; /* Relative pathname of the file */

      nConflict++;
      file_relative_name(zFullname, &fname, 0);
      fossil_print("possible unresolved merge conflict in %s\n",
                   blob_str(&fname));
      blob_reset(&fname);
    }
    nrid = content_put(&content);
    blob_reset(&content);
    if( rid>0 ){
      content_deltify(rid, nrid, 0);
    }
    db_multi_exec("UPDATE vfile SET mrid=%d, rid=%d WHERE id=%d", nrid,nrid,id);
    db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nrid);
  }
  db_finalize(&q);
  if( nConflict && !allowConflict ){
    fossil_fatal("abort due to unresolved merge conflicts; "
                 "use --allow-conflict to override");
  }else if( abortCommit ){
    fossil_fatal("one or more files were converted on your request; "
                 "please re-test before committing");
  }

  /* Create the new manifest */
  sCiInfo.pComment = &comment;
  sCiInfo.pCksum =  useCksum ? &cksum1 : 0;
  sCiInfo.verifyDate = !allowOlder && !forceFlag;
  if( forceDelta ){
    blob_zero(&manifest);
  }else{
    create_manifest(&manifest, 0, 0, vid, &sCiInfo, &szB);
  }

  /* See if a delta-manifest would be more appropriate */
  if( !forceBaseline ){
    const char *zBaselineUuid;
    Manifest *pParent;
    Manifest *pBaseline;
    pParent = manifest_get(vid, CFTYPE_MANIFEST, 0);
    if( pParent && pParent->zBaseline ){
      zBaselineUuid = pParent->zBaseline;
      pBaseline = manifest_get_by_name(zBaselineUuid, 0);
    }else{
      zBaselineUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
      pBaseline = pParent;
    }
    if( pBaseline ){
      Blob delta;
      create_manifest(&delta, zBaselineUuid, pBaseline, vid, &sCiInfo, &szD);
      /*
      ** At this point, two manifests have been constructed, either of
      ** which would work for this checkin.  The first manifest (held
      ** in the "manifest" variable) is a baseline manifest and the second
      ** (held in variable named "delta") is a delta manifest.  The
      ** question now is: which manifest should we use?
      **
      ** Let B be the number of F-cards in the baseline manifest and
      ** let D be the number of F-cards in the delta manifest, plus one for
      ** the B-card.  (B is held in the szB variable and D is held in the
      ** szD variable.)  Assume that all delta manifests adds X new F-cards.
      ** Then to minimize the total number of F- and B-cards in the repository,
      ** we should use the delta manifest if and only if:
      **
      **      D*D < B*X - X*X
      **
      ** X is an unknown here, but for most repositories, we will not be
      ** far wrong if we assume X=3.
      */
      if( forceDelta || (szD*szD)<(szB*3-9) ){
        blob_reset(&manifest);
        manifest = delta;
      }else{
        blob_reset(&delta);
      }
    }else if( forceDelta ){
      fossil_fatal("unable to find a baseline-manifest for the delta");
    }
  }
  if( !noSign && !g.markPrivate && clearsign(&manifest, &manifest) ){
    blob_zero(&ans);
    prompt_user("unable to sign manifest.  continue (y/N)? ", &ans);
    cReply = blob_str(&ans)[0];
    if( cReply!='y' && cReply!='Y' ){
      fossil_exit(1);
    }
  }

  /* If the -n|--dry-run option is specified, output the manifest file
  ** and rollback the transaction.
  */
  if( dryRunFlag ){
    blob_write_to_file(&manifest, "");
  }
  if( outputManifest ){
    zManifestFile = mprintf("%smanifest", g.zLocalRoot);
    blob_write_to_file(&manifest, zManifestFile);
    blob_reset(&manifest);
    blob_read_from_file(&manifest, zManifestFile);
    free(zManifestFile);
  }

  nvid = content_put(&manifest);
  if( nvid==0 ){
    fossil_fatal("trouble committing manifest: %s", g.zErrMsg);
  }
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nvid);
  if( manifest_crosslink(nvid, &manifest,
                         dryRunFlag ? MC_NONE : MC_PERMIT_HOOKS)==0 ){
    fossil_fatal("%s\n", g.zErrMsg);
  }
  assert( blob_is_reset(&manifest) );
  content_deltify(vid, nvid, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", nvid);

  db_prepare(&q, "SELECT uuid,merge FROM vmerge JOIN blob ON merge=rid"
                 " WHERE id=-4");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zIntegrateUuid = db_column_text(&q, 0);
    if( is_a_leaf(db_column_int(&q, 1)) ){
      fossil_print("Closed: %s\n", zIntegrateUuid);
    }else{
      fossil_print("Not_Closed: %s (not a leaf any more)\n", zIntegrateUuid);
    }
  }
  db_finalize(&q);

  fossil_print("New_Version: %s\n", zUuid);
  if( outputManifest ){
    zManifestFile = mprintf("%smanifest.uuid", g.zLocalRoot);
    blob_zero(&muuid);
    blob_appendf(&muuid, "%s\n", zUuid);
    blob_write_to_file(&muuid, zManifestFile);
    free(zManifestFile);
    blob_reset(&muuid);
  }


  /* Update the vfile and vmerge tables */
  db_multi_exec(
    "DELETE FROM vfile WHERE (vid!=%d OR deleted) AND is_selected(id);"
    "DELETE FROM vmerge;"
    "UPDATE vfile SET vid=%d;"
    "UPDATE vfile SET rid=mrid, chnged=0, deleted=0, origname=NULL"
    " WHERE is_selected(id);"
    , vid, nvid
  );
  db_lset_int("checkout", nvid);

  if( useCksum ){
    /* Verify that the repository checksum matches the expected checksum
    ** calculated before the checkin started (and stored as the R record
    ** of the manifest file).
    */
    vfile_aggregate_checksum_repository(nvid, &cksum2);
    if( blob_compare(&cksum1, &cksum2) ){
      vfile_compare_repository_to_disk(nvid);
      fossil_fatal("working checkout does not match what would have ended "
                   "up in the repository:  %b versus %b",
                   &cksum1, &cksum2);
    }

    /* Verify that the manifest checksum matches the expected checksum */
    vfile_aggregate_checksum_manifest(nvid, &cksum2, &cksum1b);
    if( blob_compare(&cksum1, &cksum1b) ){
      fossil_fatal("manifest checksum self-test failed: "
                   "%b versus %b", &cksum1, &cksum1b);
    }
    if( blob_compare(&cksum1, &cksum2) ){
      fossil_fatal(
         "working checkout does not match manifest after commit: "
         "%b versus %b", &cksum1, &cksum2);
    }

    /* Verify that the commit did not modify any disk images. */
    vfile_aggregate_checksum_disk(nvid, &cksum2);
    if( blob_compare(&cksum1, &cksum2) ){
      fossil_fatal("working checkout before and after commit does not match");
    }
  }

  /* Clear the undo/redo stack */
  undo_reset();

  /* Commit */
  db_multi_exec("DELETE FROM vvar WHERE name='ci-comment'");
  db_multi_exec("PRAGMA %s.application_id=252006673;", db_name("repository"));
  db_multi_exec("PRAGMA %s.application_id=252006674;", db_name("localdb"));
  if( dryRunFlag ){
    db_end_transaction(1);
    exit(1);
  }
  db_end_transaction(0);

  if( !g.markPrivate ){
    autosync(SYNC_PUSH|SYNC_PULL);
  }
  if( count_nonbranch_children(vid)>1 ){
    fossil_print("**** warning: a fork has occurred *****\n");
  }
}
