/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to check-in versions of the project
** from the local repository.
*/
#include "config.h"
#include "checkin.h"
#include <assert.h>

/*
** Change filter options.
*/
enum {
  /* Zero-based bit indexes. */
  CB_EDITED , CB_UPDATED , CB_CHANGED, CB_MISSING  , CB_ADDED, CB_DELETED,
  CB_RENAMED, CB_CONFLICT, CB_META   , CB_UNCHANGED, CB_EXTRA, CB_MERGE  ,
  CB_RELPATH, CB_CLASSIFY, CB_MTIME  , CB_SIZE     , CB_FATAL, CB_COMMENT,

  /* Bitmask values. */
  C_EDITED    = 1 << CB_EDITED,     /* Edited, merged, and conflicted files. */
  C_UPDATED   = 1 << CB_UPDATED,    /* Files updated by merge/integrate. */
  C_CHANGED   = 1 << CB_CHANGED,    /* Treated the same as the above two. */
  C_MISSING   = 1 << CB_MISSING,    /* Missing and non- files. */
  C_ADDED     = 1 << CB_ADDED,      /* Added files. */
  C_DELETED   = 1 << CB_DELETED,    /* Deleted files. */
  C_RENAMED   = 1 << CB_RENAMED,    /* Renamed files. */
  C_CONFLICT  = 1 << CB_CONFLICT,   /* Files having merge conflicts. */
  C_META      = 1 << CB_META,       /* Files with metadata changes. */
  C_UNCHANGED = 1 << CB_UNCHANGED,  /* Unchanged files. */
  C_EXTRA     = 1 << CB_EXTRA,      /* Unmanaged files. */
  C_MERGE     = 1 << CB_MERGE,      /* Merge contributors. */
  C_FILTER    = C_EDITED  | C_UPDATED | C_CHANGED  | C_MISSING | C_ADDED
              | C_DELETED | C_RENAMED | C_CONFLICT | C_META    | C_UNCHANGED
              | C_EXTRA   | C_MERGE,                /* All filter bits. */
  C_ALL       = C_FILTER & ~(C_EXTRA     | C_MERGE),/* All managed files. */
  C_DIFFER    = C_FILTER & ~(C_UNCHANGED | C_MERGE),/* All differences. */
  C_RELPATH   = 1 << CB_RELPATH,    /* Show relative paths. */
  C_CLASSIFY  = 1 << CB_CLASSIFY,   /* Show file change types. */
  C_DEFAULT   = (C_ALL & ~C_UNCHANGED) | C_MERGE | C_CLASSIFY,
  C_MTIME     = 1 << CB_MTIME,      /* Show file modification time. */
  C_SIZE      = 1 << CB_SIZE,       /* Show file size in bytes. */
  C_FATAL     = 1 << CB_FATAL,      /* Fail on MISSING/NOT_A_FILE. */
  C_COMMENT   = 1 << CB_COMMENT,    /* Precede each line with "# ". */
};

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
  Glob *pIgnore       /* Do not add files that match this GLOB */
){
  Blob name;   /* Name of a candidate file or directory */
  char *zName; /* Name of a candidate file or directory */
  int isDir;   /* 1 for a directory, 0 if doesn't exist, 2 for anything else */
  int i;       /* Loop counter */
  int nRoot;   /* length of g.zLocalRoot */

  db_multi_exec("CREATE TEMP TABLE sfile(pathname TEXT PRIMARY KEY %s,"
                " mtime INTEGER, size INTEGER)", filename_collation());
  nRoot = (int)strlen(g.zLocalRoot);
  if( argc==0 ){
    blob_init(&name, g.zLocalRoot, nRoot - 1);
    vfile_scan(&name, blob_size(&name), scanFlags, pIgnore, 0, RepoFILE);
    blob_reset(&name);
  }else{
    for(i=0; i<argc; i++){
      file_canonical_name(argv[i], &name, 0);
      zName = blob_str(&name);
      isDir = file_isdir(zName, RepoFILE);
      if( isDir==1 ){
        vfile_scan(&name, nRoot-1, scanFlags, pIgnore, 0, RepoFILE);
      }else if( isDir==0 ){
        fossil_warning("not found: %s", &zName[nRoot]);
      }else if( file_access(zName, R_OK) ){
        fossil_fatal("cannot open %s", &zName[nRoot]);
      }else{
        db_multi_exec(
           "INSERT OR IGNORE INTO sfile(pathname) VALUES(%Q)",
           &zName[nRoot]
        );
      }
      blob_reset(&name);
    }
  }
}

/*
** Generate text describing all changes.
**
** We assume that vfile_check_signature has been run.
*/
static void status_report(
  Blob *report,          /* Append the status report here */
  unsigned flags         /* Filter and other configuration flags */
){
  Stmt q;
  int nErr = 0;
  Blob rewrittenPathname;
  Blob sql = BLOB_INITIALIZER, where = BLOB_INITIALIZER;
  const char *zName;
  int i;

  /* Skip the file report if no files are requested at all. */
  if( !(flags & (C_ALL | C_EXTRA)) ){
     goto skipFiles;
  }

  /* Assemble the path-limiting WHERE clause, if any. */
  blob_zero(&where);
  for(i=2; i<g.argc; i++){
    Blob fname;
    file_tree_name(g.argv[i], &fname, 0, 1);
    zName = blob_str(&fname);
    if( fossil_strcmp(zName, ".")==0 ){
      blob_reset(&where);
      break;
    }
    blob_append_sql(&where,
      " %s (pathname=%Q %s) "
      "OR (pathname>'%q/' %s AND pathname<'%q0' %s)",
      (blob_size(&where)>0) ? "OR" : "AND", zName,
      filename_collation(), zName, filename_collation(),
      zName, filename_collation()
    );
  }

  /* Obtain the list of managed files if appropriate. */
  blob_zero(&sql);
  if( flags & C_ALL ){
    /* Start with a list of all managed files. */
    blob_append_sql(&sql,
      "SELECT pathname, %s as mtime, %s as size, deleted, chnged, rid,"
      "       coalesce(origname!=pathname,0) AS renamed, 1 AS managed"
      "  FROM vfile LEFT JOIN blob USING (rid)"
      " WHERE is_selected(id)%s",
      flags & C_MTIME ? "datetime(checkin_mtime(:vid, rid), "
                        "'unixepoch', toLocal())" : "''" /*safe-for-%s*/,
      flags & C_SIZE ? "coalesce(blob.size, 0)" : "0" /*safe-for-%s*/,
      blob_sql_text(&where));

    /* Exclude unchanged files unless requested. */
    if( !(flags & C_UNCHANGED) ){
      blob_append_sql(&sql,
          " AND (chnged OR deleted OR rid=0 OR pathname!=origname)");
    }
  }

  /* If C_EXTRA, add unmanaged files to the query result too. */
  if( flags & C_EXTRA ){
    if( blob_size(&sql) ){
      blob_append_sql(&sql, " UNION ALL");
    }
    blob_append_sql(&sql,
      " SELECT pathname, %s, %s, 0, 0, 0, 0, 0"
      " FROM sfile WHERE pathname NOT IN (%s)%s",
      flags & C_MTIME ? "datetime(mtime, 'unixepoch', toLocal())" : "''",
      flags & C_SIZE ? "size" : "0",
      fossil_all_reserved_names(0), blob_sql_text(&where));
  }
  blob_reset(&where);

  /* Pre-create the "ok" temporary table so the checkin_mtime() SQL function
   * does not lead to SQLITE_ABORT_ROLLBACK during execution of the OP_OpenRead
   * SQLite opcode.  checkin_mtime() calls mtime_of_manifest_file() which
   * creates a temporary table if it doesn't already exist, thus invalidating
   * the prepared statement in the middle of its execution. */
  db_multi_exec("CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY)");

  /* Append an ORDER BY clause then compile the query. */
  blob_append_sql(&sql, " ORDER BY pathname");
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);

  /* Bind the checkout version ID to the query if needed. */
  if( (flags & C_ALL) && (flags & C_MTIME) ){
    db_bind_int(&q, ":vid", db_lget_int("checkout", 0));
  }

  /* Execute the query and assemble the report. */
  blob_zero(&rewrittenPathname);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q, 0);
    const char *zClass = 0;
    int isManaged = db_column_int(&q, 7);
    const char *zMtime = db_column_text(&q, 1);
    int size = db_column_int(&q, 2);
    int isDeleted = db_column_int(&q, 3);
    int isChnged = db_column_int(&q, 4);
    int isNew = isManaged && !db_column_int(&q, 5);
    int isRenamed = db_column_int(&q, 6);
    char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    int isMissing = !file_isfile_or_link(zFullName);

    /* Determine the file change classification, if any. */
    if( isDeleted ){
      if( flags & C_DELETED ){
        zClass = "DELETED";
      }
    }else if( isMissing ){
      if( file_access(zFullName, F_OK)==0 ){
        if( flags & C_MISSING ){
          zClass = "NOT_A_FILE";
        }
        if( flags & C_FATAL ){
          fossil_warning("not a file: %s", zFullName);
          nErr++;
        }
      }else{
        if( flags & C_MISSING ){
          zClass = "MISSING";
        }
        if( flags & C_FATAL ){
          fossil_warning("missing file: %s", zFullName);
          nErr++;
        }
      }
    }else if( isNew ){
      if( flags & C_ADDED ){
        zClass = "ADDED";
      }
    }else if( (flags & (C_UPDATED | C_CHANGED)) && isChnged==2 ){
      zClass = "UPDATED_BY_MERGE";
    }else if( (flags & C_ADDED) && isChnged==3 ){
      zClass = "ADDED_BY_MERGE";
    }else if( (flags & (C_UPDATED | C_CHANGED)) && isChnged==4 ){
      zClass = "UPDATED_BY_INTEGRATE";
    }else if( (flags & C_ADDED) && isChnged==5 ){
      zClass = "ADDED_BY_INTEGRATE";
    }else if( (flags & C_META) && isChnged==6 ){
      zClass = "EXECUTABLE";
    }else if( (flags & C_META) && isChnged==7 ){
      zClass = "SYMLINK";
    }else if( (flags & C_META) && isChnged==8 ){
      zClass = "UNEXEC";
    }else if( (flags & C_META) && isChnged==9 ){
      zClass = "UNLINK";
    }else if( (flags & C_CONFLICT) && isChnged && !file_islink(zFullName)
           && file_contains_merge_marker(zFullName) ){
      zClass = "CONFLICT";
    }else if( (flags & (C_EDITED | C_CHANGED)) && isChnged
           && (isChnged<2 || isChnged>9) ){
      zClass = "EDITED";
    }else if( (flags & C_RENAMED) && isRenamed ){
      zClass = "RENAMED";
    }else if( (flags & C_UNCHANGED) && isManaged && !isNew
                                    && !isChnged && !isRenamed ){
      zClass = "UNCHANGED";
    }else if( (flags & C_EXTRA) && !isManaged ){
      zClass = "EXTRA";
    }

    /* Only report files for which a change classification was determined. */
    if( zClass ){
      if( flags & C_COMMENT ){
        blob_append(report, "# ", 2);
      }
      if( flags & C_CLASSIFY ){
        blob_appendf(report, "%-10s ", zClass);
      }
      if( flags & C_MTIME ){
        blob_append(report, zMtime, -1);
        blob_append(report, "  ", 2);
      }
      if( flags & C_SIZE ){
        blob_appendf(report, "%7d ", size);
      }
      if( flags & C_RELPATH ){
        /* If C_RELPATH, display paths relative to current directory. */
        const char *zDisplayName;
        file_relative_name(zFullName, &rewrittenPathname, 0);
        zDisplayName = blob_str(&rewrittenPathname);
        if( zDisplayName[0]=='.' && zDisplayName[1]=='/' ){
          zDisplayName += 2;  /* no unnecessary ./ prefix */
        }
        blob_append(report, zDisplayName, -1);
      }else{
        /* If not C_RELPATH, display paths relative to project root. */
        blob_append(report, zPathname, -1);
      }
      blob_append(report, "\n", 1);
    }
    free(zFullName);
  }
  blob_reset(&rewrittenPathname);
  db_finalize(&q);

  /* If C_MERGE, put merge contributors at the end of the report. */
skipFiles:
  if( flags & C_MERGE ){
    db_prepare(&q, "SELECT mhash, id FROM vmerge WHERE id<=0" );
    while( db_step(&q)==SQLITE_ROW ){
      if( flags & C_COMMENT ){
        blob_append(report, "# ", 2);
      }
      if( flags & C_CLASSIFY ){
        const char *zClass;
        switch( db_column_int(&q, 1) ){
          case -1: zClass = "CHERRYPICK" ; break;
          case -2: zClass = "BACKOUT"    ; break;
          case -4: zClass = "INTEGRATE"  ; break;
          default: zClass = "MERGED_WITH"; break;
        }
        blob_appendf(report, "%-10s ", zClass);
      }
      blob_append(report, db_column_text(&q, 0), -1);
      blob_append(report, "\n", 1);
    }
    db_finalize(&q);
  }
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
** COMMAND: status
**
** Usage: %fossil changes|status ?OPTIONS? ?PATHS ...?
**
** Report the change status of files in the current checkout.  If one or
** more PATHS are specified, only changes among the named files and
** directories are reported.  Directories are searched recursively.
**
** The status command is similar to the changes command, except it lacks
** several of the options supported by changes and it has its own header
** and footer information.  The header information is a subset of that
** shown by the info command, and the footer shows if there are any forks.
** Change type classification is always enabled for the status command.
**
** Each line of output is the name of a changed file, with paths shown
** according to the "relative-paths" setting, unless overridden by the
** --abs-paths or --rel-paths options.
**
** By default, all changed files are selected for display.  This behavior
** can be overridden by using one or more filter options (listed below),
** in which case only files with the specified change type(s) are shown.
** As a special case, the --no-merge option does not inhibit this default.
** This default shows exactly the set of changes that would be checked
** in by the commit command.
**
** If no filter options are used, or if the --merge option is used, the
** artifact hash of each merge contributor check-in version is displayed at
** the end of the report.  The --no-merge option is useful to display the
** default set of changed files without the merge contributors.
**
** If change type classification is enabled, each output line starts with
** a code describing the file's change type, e.g. EDITED or RENAMED.  It
** is enabled by default unless exactly one change type is selected.  For
** the purposes of determining the default, --changed counts as selecting
** one change type.  The default can be overridden by the --classify or
** --no-classify options.
**
** --edited and --updated produce disjoint sets.  --updated shows a file
** only when it is identical to that of its merge contributor, and the
** change type classification is UPDATED_BY_MERGE or UPDATED_BY_INTEGRATE.
** If the file had to be merged with any other changes, it is considered
** to be merged or conflicted and therefore will be shown by --edited, not
** --updated, with types EDITED or CONFLICT.  The --changed option can be
** used to display the union of --edited and --updated.
**
** --differ is so named because it lists all the differences between the
** checked-out version and the checkout directory.  In addition to the
** default changes (excluding --merge), it lists extra files which (if
** ignore-glob is set correctly) may be worth adding.  Prior to doing a
** commit, it is good practice to check --differ to see not only which
** changes would be committed but also if any files should be added.
**
** If both --merge and --no-merge are used, --no-merge has priority.  The
** same is true of --classify and --no-classify.
**
** The "fossil changes --extra" command is equivalent to "fossil extras".
**
** General options:
**    --abs-paths       Display absolute pathnames.
**    --rel-paths       Display pathnames relative to the current working
**                      directory.
**    --hash            Verify file status using hashing rather than
**                      relying on file mtimes.
**    --case-sensitive <BOOL>  Override case-sensitive setting.
**    --dotfiles        Include unmanaged files beginning with a dot.
**    --ignore <CSG>    Ignore unmanaged files matching CSG glob patterns.
**
** Options specific to the changes command:
**    --header          Identify the repository if report is non-empty.
**    -v|--verbose      Say "(none)" if the change report is empty.
**    --classify        Start each line with the file's change type.
**    --no-classify     Do not print file change types.
**
** Filter options:
**    --edited          Display edited, merged, and conflicted files.
**    --updated         Display files updated by merge/integrate.
**    --changed         Combination of the above two options.
**    --missing         Display missing files.
**    --added           Display added files.
**    --deleted         Display deleted files.
**    --renamed         Display renamed files.
**    --conflict        Display files having merge conflicts.
**    --meta            Display files with metadata changes.
**    --unchanged       Display unchanged files.
**    --all             Display all managed files, i.e. all of the above.
**    --extra           Display unmanaged files.
**    --differ          Display modified and extra files.
**    --merge           Display merge contributors.
**    --no-merge        Do not display merge contributors.
**
** See also: extras, ls
*/
void status_cmd(void){
  /* Affirmative and negative flag option tables. */
  static const struct {
    const char *option; /* Flag name. */
    unsigned mask;      /* Flag bits. */
  } flagDefs[] = {
    {"edited"  , C_EDITED  }, {"updated"    , C_UPDATED  },
    {"changed" , C_CHANGED }, {"missing"    , C_MISSING  },
    {"added"   , C_ADDED   }, {"deleted"    , C_DELETED  },
    {"renamed" , C_RENAMED }, {"conflict"   , C_CONFLICT },
    {"meta"    , C_META    }, {"unchanged"  , C_UNCHANGED},
    {"all"     , C_ALL     }, {"extra"      , C_EXTRA    },
    {"differ"  , C_DIFFER  }, {"merge"      , C_MERGE    },
    {"classify", C_CLASSIFY},
  }, noFlagDefs[] = {
    {"no-merge", C_MERGE   }, {"no-classify", C_CLASSIFY },
  };

  Blob report = BLOB_INITIALIZER;
  enum {CHANGES, STATUS} command = *g.argv[1]=='s' ? STATUS : CHANGES;
  /* --sha1sum is an undocumented alias for --hash for backwards compatiblity */
  int useHash = find_option("hash",0,0)!=0 || find_option("sha1sum",0,0)!=0;
  int showHdr = command==CHANGES && find_option("header", 0, 0);
  int verboseFlag = command==CHANGES && find_option("verbose", "v", 0);
  const char *zIgnoreFlag = find_option("ignore", 0, 1);
  unsigned scanFlags = 0;
  unsigned flags = 0;
  int vid, i;

  fossil_pledge("stdio rpath wpath cpath fattr id flock tty chown");

  /* Load affirmative flag options. */
  for( i=0; i<count(flagDefs); ++i ){
    if( (command==CHANGES || !(flagDefs[i].mask & C_CLASSIFY))
     && find_option(flagDefs[i].option, 0, 0) ){
      flags |= flagDefs[i].mask;
    }
  }

  /* If no filter options are specified, enable defaults. */
  if( !(flags & C_FILTER) ){
    flags |= C_DEFAULT;
  }

  /* If more than one filter is enabled, enable classification.  This is tricky.
   * Having one filter means flags masked by C_FILTER is a power of two.  If a
   * number masked by one less than itself is zero, it's either zero or a power
   * of two.  It's already known to not be zero because of the above defaults.
   * Unlike --all, --changed is a single filter, i.e. it sets only one bit.
   * Also force classification for the status command. */
  if( command==STATUS || (flags & (flags-1) & C_FILTER) ){
    flags |= C_CLASSIFY;
  }

  /* Negative flag options override defaults applied above. */
  for( i=0; i<count(noFlagDefs); ++i ){
    if( (command==CHANGES || !(noFlagDefs[i].mask & C_CLASSIFY))
     && find_option(noFlagDefs[i].option, 0, 0) ){
      flags &= ~noFlagDefs[i].mask;
    }
  }

  /* Confirm current working directory is within checkout. */
  db_must_be_within_tree();

  /* Get checkout version. l*/
  vid = db_lget_int("checkout", 0);

  /* Relative path flag determination is done by a shared function. */
  if( determine_cwd_relative_option() ){
    flags |= C_RELPATH;
  }

  /* If --ignore is not specified, use the ignore-glob setting. */
  if( !zIgnoreFlag ){
    zIgnoreFlag = db_get("ignore-glob", 0);
  }

  /* Get the --dotfiles argument, or read it from the dotfiles setting. */
  if( find_option("dotfiles", 0, 0) || db_get_boolean("dotfiles", 0) ){
    scanFlags = SCAN_ALL;
  }

  /* We should be done with options. */
  verify_all_options();

  /* Check for changed files. */
  vfile_check_signature(vid, useHash ? CKSIG_HASH : 0);

  /* Search for unmanaged files if requested. */
  if( flags & C_EXTRA ){
    Glob *pIgnore = glob_create(zIgnoreFlag);
    locate_unmanaged_files(g.argc-2, g.argv+2, scanFlags, pIgnore);
    glob_free(pIgnore);
  }

  /* The status command prints general information before the change list. */
  if( command==STATUS ){
    fossil_print("repository:   %s\n", db_repository_filename());
    fossil_print("local-root:   %s\n", g.zLocalRoot);
    if( g.zConfigDbName ){
      fossil_print("config-db:    %s\n", g.zConfigDbName);
    }
    if( vid ){
      show_common_info(vid, "checkout:", 1, 1);
    }
    db_record_repository_filename(0);
  }

  /* Find and print all requested changes. */
  blob_zero(&report);
  status_report(&report, flags);
  if( blob_size(&report) ){
    if( showHdr ){
      fossil_print(
        "Changes for %s at %s:\n", db_get("project-name", "<unnamed>"),
        g.zLocalRoot);
    }
    blob_write_to_file(&report, "-");
  }else if( verboseFlag ){
    fossil_print("  (none)\n");
  }
  blob_reset(&report);

  /* The status command ends with warnings about ambiguous leaves (forks). */
  if( command==STATUS ){
    leaf_ambiguity_warning(vid, vid);
  }
}

/*
** Take care of -r version of ls command
*/
static void ls_cmd_rev(
  const char *zRev,  /* Revision string given */
  int verboseFlag,   /* Verbose flag given */
  int showAge,       /* Age flag given */
  int timeOrder      /* Order by time flag given */
){
  Stmt q;
  char *zOrderBy = "pathname COLLATE nocase";
  char *zName;
  Blob where;
  int rid;
  int i;

  /* Handle given file names */
  blob_zero(&where);
  for(i=2; i<g.argc; i++){
    Blob fname;
    file_tree_name(g.argv[i], &fname, 0, 1);
    zName = blob_str(&fname);
    if( fossil_strcmp(zName, ".")==0 ){
      blob_reset(&where);
      break;
    }
    blob_append_sql(&where,
      " %s (pathname=%Q %s) "
      "OR (pathname>'%q/' %s AND pathname<'%q0' %s)",
      (blob_size(&where)>0) ? "OR" : "AND (", zName,
      filename_collation(), zName, filename_collation(),
      zName, filename_collation()
    );
  }
  if( blob_size(&where)>0 ){
    blob_append_sql(&where, ")");
  }

  rid = symbolic_name_to_rid(zRev, "ci");
  if( rid==0 ){
    fossil_fatal("not a valid check-in: %s", zRev);
  }

  if( timeOrder ){
    zOrderBy = "mtime DESC";
  }

  compute_fileage(rid,0);
  db_prepare(&q,
    "SELECT datetime(fileage.mtime, toLocal()), fileage.pathname,\n"
    "       blob.size\n"
    "  FROM fileage, blob\n"
    " WHERE blob.rid=fileage.fid %s\n"
    " ORDER BY %s;", blob_sql_text(&where), zOrderBy /*safe-for-%s*/
  );
  blob_reset(&where);

  while( db_step(&q)==SQLITE_ROW ){
    const char *zTime = db_column_text(&q,0);
    const char *zFile = db_column_text(&q,1);
    int size = db_column_int(&q,2);
    if( verboseFlag ){
      fossil_print("%s  %7d  %s\n", zTime, size, zFile);
    }else if( showAge ){
      fossil_print("%s  %s\n", zTime, zFile);
    }else{
      fossil_print("%s\n", zFile);
    }
  }
  db_finalize(&q);
}

/*
** COMMAND: ls
**
** Usage: %fossil ls ?OPTIONS? ?PATHS ...?
**
** List all files in the current checkout.  If PATHS is included, only the
** named files (or their children if directories) are shown.
**
** The ls command is essentially two related commands in one, depending on
** whether or not the -r option is given.  -r selects a specific check-in
** version to list, in which case -R can be used to select the repository.
** The fine behavior of the --age, -v, and -t options is altered by the -r
** option as well, as explained below.
**
** The --age option displays file commit times.  Like -r, --age has the
** side effect of making -t sort by commit time, not modification time.
**
** The -v option provides extra information about each file.  Without -r,
** -v displays the change status, in the manner of the changes command.
** With -r, -v shows the commit time and size of the checked-in files.
**
** The -t option changes the sort order.  Without -t, files are sorted by
** path and name (case insensitive sort if -r).  If neither --age nor -r
** are used, -t sorts by modification time, otherwise by commit time.
**
** Options:
**   --age                 Show when each file was committed.
**   -v|--verbose          Provide extra information about each file.
**   -t                    Sort output in time order.
**   -r VERSION            The specific check-in to list.
**   -R|--repository FILE  Extract info from repository FILE.
**
** See also: changes, extras, status
*/
void ls_cmd(void){
  int vid;
  Stmt q;
  int verboseFlag;
  int showAge;
  int timeOrder;
  char *zOrderBy = "pathname";
  Blob where;
  int i;
  const char *zName;
  const char *zRev;

  verboseFlag = find_option("verbose","v", 0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("l","l", 0)!=0; /* deprecated */
  }
  showAge = find_option("age",0,0)!=0;
  zRev = find_option("r","r",1);
  timeOrder = find_option("t","t",0)!=0;

  if( zRev!=0 ){
    db_find_and_open_repository(0, 0);
    verify_all_options();
    ls_cmd_rev(zRev,verboseFlag,showAge,timeOrder);
    return;
  }else if( find_option("R",0,1)!=0 ){
    fossil_fatal("the -r is required in addition to -R");
  }

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( timeOrder ){
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
    file_tree_name(g.argv[i], &fname, 0, 1);
    zName = blob_str(&fname);
    if( fossil_strcmp(zName, ".")==0 ){
      blob_reset(&where);
      break;
    }
    blob_append_sql(&where,
       " %s (pathname=%Q %s) "
       "OR (pathname>'%q/' %s AND pathname<'%q0' %s)",
       (blob_size(&where)>0) ? "OR" : "WHERE", zName,
       filename_collation(), zName, filename_collation(),
       zName, filename_collation()
    );
  }
  vfile_check_signature(vid, 0);
  if( showAge ){
    db_prepare(&q,
       "SELECT pathname, deleted, rid, chnged, coalesce(origname!=pathname,0),"
       "       datetime(checkin_mtime(%d,rid),'unixepoch',toLocal())"
       "  FROM vfile %s"
       " ORDER BY %s",
       vid, blob_sql_text(&where), zOrderBy /*safe-for-%s*/
    );
  }else{
    db_prepare(&q,
       "SELECT pathname, deleted, rid, chnged,"
       "       coalesce(origname!=pathname,0), islink"
       "  FROM vfile %s"
       " ORDER BY %s", blob_sql_text(&where), zOrderBy /*safe-for-%s*/
    );
  }
  blob_reset(&where);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isNew = db_column_int(&q,2)==0;
    int chnged = db_column_int(&q,3);
    int renamed = db_column_int(&q,4);
    int isLink = db_column_int(&q,5);
    char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    const char *type = "";
    if( verboseFlag ){
      if( isNew ){
        type = "ADDED      ";
      }else if( isDeleted ){
        type = "DELETED    ";
      }else if( !file_isfile_or_link(zFullName) ){
        if( file_access(zFullName, F_OK)==0 ){
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
        }else if( !isLink && file_contains_merge_marker(zFullName) ){
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
** COMMAND: extras
**
** Usage: %fossil extras ?OPTIONS? ?PATH1 ...?
**
** Print a list of all files in the source tree that are not part of the
** current checkout. See also the "clean" command. If paths are specified,
** only files in the given directories will be listed.
**
** Files and subdirectories whose names begin with "." are normally
** ignored but can be included by adding the --dotfiles option.
**
** Files whose names match any of the glob patterns in the "ignore-glob"
** setting are ignored. This setting can be overridden by the --ignore
** option, whose CSG argument is a comma-separated list of glob patterns.
**
** Pathnames are displayed according to the "relative-paths" setting,
** unless overridden by the --abs-paths or --rel-paths options.
**
** Options:
**    --abs-paths      Display absolute pathnames.
**    --case-sensitive <BOOL> override case-sensitive setting
**    --dotfiles       include files beginning with a dot (".")
**    --header         Identify the repository if there are extras
**    --ignore <CSG>   ignore files matching patterns from the argument
**    --rel-paths      Display pathnames relative to the current working
**                     directory.
**
** See also: changes, clean, status
*/
void extras_cmd(void){
  Blob report = BLOB_INITIALIZER;
  const char *zIgnoreFlag = find_option("ignore",0,1);
  unsigned scanFlags = find_option("dotfiles",0,0)!=0 ? SCAN_ALL : 0;
  unsigned flags = C_EXTRA;
  int showHdr = find_option("header",0,0)!=0;
  Glob *pIgnore;

  if( find_option("temp",0,0)!=0 ) scanFlags |= SCAN_TEMP;
  db_must_be_within_tree();

  if( determine_cwd_relative_option() ){
    flags |= C_RELPATH;
  }

  if( db_get_boolean("dotfiles", 0) ) scanFlags |= SCAN_ALL;

  /* We should be done with options.. */
  verify_all_options();

  if( zIgnoreFlag==0 ){
    zIgnoreFlag = db_get("ignore-glob", 0);
  }
  pIgnore = glob_create(zIgnoreFlag);
  /* Always consider symlinks. */
  g.allowSymlinks = db_allow_symlinks_by_default();
  locate_unmanaged_files(g.argc-2, g.argv+2, scanFlags, pIgnore);
  glob_free(pIgnore);

  blob_zero(&report);
  status_report(&report, flags);
  if( blob_size(&report) ){
    if( showHdr ){
      fossil_print("Extras for %s at %s:\n", db_get("project-name","<unnamed>"),
                   g.zLocalRoot);
    }
    blob_write_to_file(&report, "-");
  }
  blob_reset(&report);
}

/*
** COMMAND: clean
**
** Usage: %fossil clean ?OPTIONS? ?PATH ...?
**
** Delete all "extra" files in the source tree.  "Extra" files are files
** that are not officially part of the checkout.  If one or more PATH
** arguments appear, then only the files named, or files contained with
** directories named, will be removed.
**
** If the --prompt option is used, prompts are issued to confirm the
** permanent removal of each file.  Otherwise, files are backed up to the
** undo buffer prior to removal, and prompts are issued only for files
** whose removal cannot be undone due to their large size or due to
** --disable-undo being used.
**
** The --force option treats all prompts as having been answered yes,
** whereas --no-prompt treats them as having been answered no.
**
** Files matching any glob pattern specified by the --clean option are
** deleted without prompting, and the removal cannot be undone.
**
** No file that matches glob patterns specified by --ignore or --keep will
** ever be deleted.  Files and subdirectories whose names begin with "."
** are automatically ignored unless the --dotfiles option is used.
**
** The default values for --clean, --ignore, and --keep are determined by
** the (versionable) clean-glob, ignore-glob, and keep-glob settings.
**
** The --verily option ignores the keep-glob and ignore-glob settings and
** turns on --force, --emptydirs, --dotfiles, and --disable-undo.  Use the
** --verily option when you really want to clean up everything.  Extreme
** care should be exercised when using the --verily option.
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
**    --disable-undo   WARNING: This option disables use of the undo
**                     mechanism for this clean operation and should be
**                     used with extreme caution.
**    --dotfiles       Include files beginning with a dot (".").
**    --emptydirs      Remove any empty directories that are not
**                     explicitly exempted via the empty-dirs setting
**                     or another applicable setting or command line
**                     argument.  Matching files, if any, are removed
**                     prior to checking for any empty directories;
**                     therefore, directories that contain only files
**                     that were removed will be removed as well.
**    -f|--force       Remove files without prompting.
**    -i|--prompt      Prompt before removing each file.  This option
**                     implies the --disable-undo option.
**    -x|--verily      WARNING: Removes everything that is not a managed
**                     file or the repository itself.  This option
**                     implies the --force, --emptydirs, --dotfiles, and
**                     --disable-undo options.
**                     Furthermore, it completely disregards the keep-glob
**                     and ignore-glob settings.  However, it does honor
**                     the --ignore and --keep options.
**    --clean <CSG>    WARNING: Never prompt to delete any files matching
**                     this comma separated list of glob patterns.  Also,
**                     deletions of any files matching this pattern list
**                     cannot be undone.
**    --ignore <CSG>   Ignore files matching patterns from the
**                     comma separated list of glob patterns.
**    --keep <CSG>     Keep files matching this comma separated
**                     list of glob patterns.
**    -n|--dry-run     Delete nothing, but display what would have been
**                     deleted.
**    --no-prompt      This option disables prompting the user for input
**                     and assumes an answer of 'No' for every question.
**    --temp           Remove only Fossil-generated temporary files.
**    -v|--verbose     Show all files as they are removed.
**
** See also: addremove, extras, status
*/
void clean_cmd(void){
  int allFileFlag, allDirFlag, dryRunFlag, verboseFlag;
  int emptyDirsFlag, dirsOnlyFlag;
  int disableUndo, noPrompt;
  int alwaysPrompt = 0;
  unsigned scanFlags = 0;
  int verilyFlag = 0;
  const char *zIgnoreFlag, *zKeepFlag, *zCleanFlag;
  Glob *pIgnore, *pKeep, *pClean;
  int nRoot;

#ifndef UNDO_SIZE_LIMIT  /* TODO: Setting? */
#define UNDO_SIZE_LIMIT  (10*1024*1024) /* 10MiB */
#endif

  undo_capture_command_line();
  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("test",0,0)!=0; /* deprecated */
  }
  if( !dryRunFlag ){
    dryRunFlag = find_option("whatif",0,0)!=0;
  }
  disableUndo = find_option("disable-undo",0,0)!=0;
  noPrompt = find_option("no-prompt",0,0)!=0;
  alwaysPrompt = find_option("prompt","i",0)!=0;
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
  db_must_be_within_tree();
  if( find_option("verily","x",0)!=0 ){
    verilyFlag = allFileFlag = allDirFlag = 1;
    emptyDirsFlag = 1;
    disableUndo = 1;
    scanFlags |= SCAN_ALL;
    zCleanFlag = 0;
  }
  if( zIgnoreFlag==0 && !verilyFlag ){
    zIgnoreFlag = db_get("ignore-glob", 0);
  }
  if( zKeepFlag==0 && !verilyFlag ){
    zKeepFlag = db_get("keep-glob", 0);
  }
  if( zCleanFlag==0 && !verilyFlag ){
    zCleanFlag = db_get("clean-glob", 0);
  }
  if( db_get_boolean("dotfiles", 0) ) scanFlags |= SCAN_ALL;
  verify_all_options();
  pIgnore = glob_create(zIgnoreFlag);
  pKeep = glob_create(zKeepFlag);
  pClean = glob_create(zCleanFlag);
  nRoot = (int)strlen(g.zLocalRoot);
  /* Always consider symlinks. */
  g.allowSymlinks = db_allow_symlinks_by_default();
  if( !dirsOnlyFlag ){
    Stmt q;
    Blob repo;
    if( !dryRunFlag && !disableUndo ) undo_begin();
    locate_unmanaged_files(g.argc-2, g.argv+2, scanFlags, pIgnore);
    db_prepare(&q,
        "SELECT %Q || pathname FROM sfile"
        " WHERE pathname NOT IN (%s)"
        " ORDER BY 1",
        g.zLocalRoot, fossil_all_reserved_names(0)
    );
    if( file_tree_name(g.zRepositoryName, &repo, 0, 0) ){
      db_multi_exec("DELETE FROM sfile WHERE pathname=%B", &repo);
    }
    db_multi_exec("DELETE FROM sfile WHERE pathname IN"
                  " (SELECT pathname FROM vfile)");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      if( glob_match(pKeep, zName+nRoot) ){
        if( verboseFlag ){
          fossil_print("KEPT file \"%s\" not removed (due to --keep"
                       " or \"keep-glob\")\n", zName+nRoot);
        }
        continue;
      }
      if( !dryRunFlag && !glob_match(pClean, zName+nRoot) ){
        char *zPrompt = 0;
        char cReply;
        Blob ans = empty_blob;
        int undoRc = UNDO_NONE;
        if( alwaysPrompt ){
          zPrompt = mprintf("Remove unmanaged file \"%s\" (a=all/y/N)? ",
                            zName+nRoot);
          prompt_user(zPrompt, &ans);
          fossil_free(zPrompt);
          cReply = fossil_toupper(blob_str(&ans)[0]);
          blob_reset(&ans);
          if( cReply=='N' ) continue;
          if( cReply=='A' ){
            allFileFlag = 1;
            alwaysPrompt = 0;
          }else{
            undoRc = UNDO_SAVED_OK;
          }
        }else if( !disableUndo ){
          undoRc = undo_maybe_save(zName+nRoot, UNDO_SIZE_LIMIT);
        }
        if( undoRc!=UNDO_SAVED_OK ){
          if( allFileFlag ){
            cReply = 'Y';
          }else if( !noPrompt ){
            Blob ans;
            zPrompt = mprintf("\nWARNING: Deletion of this file will "
                              "not be undoable via the 'undo'\n"
                              "         command because %s.\n\n"
                              "Remove unmanaged file \"%s\" (a=all/y/N)? ",
                              undo_save_message(undoRc), zName+nRoot);
            prompt_user(zPrompt, &ans);
            fossil_free(zPrompt);
            cReply = blob_str(&ans)[0];
            blob_reset(&ans);
          }else{
            cReply = 'N';
          }
          if( cReply=='a' || cReply=='A' ){
            allFileFlag = 1;
          }else if( cReply!='y' && cReply!='Y' ){
            continue;
          }
        }
      }
      if( dryRunFlag || file_delete(zName)==0 ){
        if( verboseFlag || dryRunFlag ){
          fossil_print("Removed unmanaged file: %s\n", zName+nRoot);
        }
      }else{
        fossil_print("Could not remove file: %s\n", zName+nRoot);
      }
    }
    db_finalize(&q);
    if( !dryRunFlag && !disableUndo ) undo_finish();
  }
  if( emptyDirsFlag ){
    Glob *pEmptyDirs = glob_create(db_get("empty-dirs", 0));
    Stmt q;
    Blob root;
    blob_init(&root, g.zLocalRoot, nRoot - 1);
    vfile_dir_scan(&root, blob_size(&root), scanFlags, pIgnore,
                   pEmptyDirs, RepoFILE);
    blob_reset(&root);
    db_prepare(&q,
        "SELECT %Q || x FROM dscan_temp"
        " WHERE x NOT IN (%s) AND y = 0"
        " ORDER BY 1 DESC",
        g.zLocalRoot, fossil_all_reserved_names(0)
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      if( glob_match(pKeep, zName+nRoot) ){
        if( verboseFlag ){
          fossil_print("KEPT directory \"%s\" not removed (due to --keep"
                       " or \"keep-glob\")\n", zName+nRoot);
        }
        continue;
      }
      if( !allDirFlag && !dryRunFlag && !glob_match(pClean, zName+nRoot) ){
        char cReply;
        if( !noPrompt ){
          Blob ans;
          char *prompt = mprintf("Remove empty directory \"%s\" (a=all/y/N)? ",
                                 zName+nRoot);
          prompt_user(prompt, &ans);
          cReply = blob_str(&ans)[0];
          fossil_free(prompt);
          blob_reset(&ans);
        }else{
          cReply = 'N';
        }
        if( cReply=='a' || cReply=='A' ){
          allDirFlag = 1;
        }else if( cReply!='y' && cReply!='Y' ){
          continue;
        }
      }
      if( dryRunFlag || file_rmdir(zName)==0 ){
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
    zEditor = fossil_utf8_to_path(zEditor, 0);
    blob_add_cr(pPrompt);
#endif
  }
#endif
  if( zEditor==0 ){
    if( blob_size(pPrompt)>0 ){
      blob_append(pPrompt,
         "#\n"
         "# Since no default text editor is set using EDITOR or VISUAL\n"
         "# environment variables or the \"fossil set editor\" command,\n"
         "# and because no comment was specified using the \"-m\" or \"-M\"\n"
         "# command-line options, you will need to enter the comment below.\n"
         "# Type \".\" on a line by itself when you are done:\n", -1);
    }
    zFile = mprintf("-");
  }else{
    Blob fname;
    blob_zero(&fname);
    if( g.zLocalRoot!=0 ){
      file_relative_name(g.zLocalRoot, &fname, 1);
      zFile = db_text(0, "SELECT '%qci-comment-'||hex(randomblob(6))||'.txt'",
                      blob_str(&fname));
    }else{
      file_tempname(&fname, "ci-comment",0);
      zFile = mprintf("%s", blob_str(&fname));
    }
    blob_reset(&fname);
  }
#if defined(_WIN32)
  blob_add_cr(pPrompt);
#endif
  if( blob_size(pPrompt)>0 ) blob_write_to_file(pPrompt, zFile);
  if( zEditor ){
    zCmd = mprintf("%s \"%s\"", zEditor, zFile);
    fossil_print("%s\n", zCmd);
    if( fossil_system(zCmd) ){
      fossil_fatal("editor aborted: \"%s\"", zCmd);
    }

    blob_read_from_file(&reply, zFile, ExtFILE);
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
  if( zInit && zInit[0]){
    blob_append(&prompt, zInit, -1);
  }
#else
  blob_init(&prompt, zInit, -1);
#endif
  blob_append(&prompt,
    "\n"
    "# Enter a commit message for this check-in."
        " Lines beginning with # are ignored.\n"
    "#\n", -1
  );
  blob_appendf(&prompt, "# user: %s\n",
               p->zUserOvrd ? p->zUserOvrd : login_name());
  if( p->zBranch && p->zBranch[0] ){
    blob_appendf(&prompt, "# tags: %s\n#\n", p->zBranch);
  }else{
    char *zTags = info_tags_of_checkin(parent_rid, 1);
    if( zTags || p->azTag ){
      blob_append(&prompt, "# tags: ", 8);
      if(zTags){
        blob_appendf(&prompt, "%z%s", zTags, p->azTag ? ", " : "");
      }
      if(p->azTag){
        int i = 0;
        for( ; p->azTag[i]; ++i ){
          blob_appendf(&prompt, "%s%s", p->azTag[i],
                       p->azTag[i+1] ? ", " : "");
        }
      }
      blob_appendf(&prompt, "\n#\n");
    }
  }
  status_report(&prompt, C_DEFAULT | C_FATAL | C_COMMENT);
  if( g.markPrivate ){
    blob_append(&prompt,
      "# PRIVATE BRANCH: This check-in will be private and will not sync to\n"
      "# repositories.\n"
      "#\n", -1
    );
  }
  if( p->integrateFlag ){
    blob_append(&prompt,
      "#\n"
      "# All merged-in branches will be closed due to the --integrate flag\n"
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
    Bag toCommit;

    blob_zero(&fname);
    bag_init(&toCommit);
    for(ii=2; ii<g.argc; ii++){
      int cnt = 0;
      file_tree_name(g.argv[ii], &fname, 0, 1);
      if( fossil_strcmp(blob_str(&fname),".")==0 ){
        bag_clear(&toCommit);
        return result;
      }
      db_prepare(&q,
        "SELECT id FROM vfile WHERE pathname=%Q %s"
        " OR (pathname>'%q/' %s AND pathname<'%q0' %s)",
        blob_str(&fname), filename_collation(), blob_str(&fname),
        filename_collation(), blob_str(&fname), filename_collation());
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
    g.aCommitFile = fossil_malloc( (bag_count(&toCommit)+1) *
                                      sizeof(g.aCommitFile[0]) );
    for(ii=bag_first(&toCommit); ii>0; ii=bag_next(&toCommit, ii)){
      g.aCommitFile[jj++] = ii;
    }
    g.aCommitFile[jj] = 0;
    bag_clear(&toCommit);
  }
  return result;
}

/*
** Returns true if the checkin identified by the first parameter is
** older than the given (valid) date/time string, else returns false.
** Also returns true if rid does not refer to a checkin, but it is not
** intended to be used for that case.
*/
static int checkin_is_younger(
  int rid,              /* The record ID of the ancestor */
  const char *zDate     /* Date & time of the current check-in */
){
  return db_exists(
    "SELECT 1 FROM event"
    " WHERE datetime(mtime)>=%Q"
    "   AND type='ci' AND objid=%d",
    zDate, rid
  ) ? 0 : 1;
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
  if(checkin_is_younger(rid,zDate)==0){
    fossil_fatal("ancestor check-in [%S] (%s) is not older (clock skew?)"
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
  int integrateFlag;          /* Close merged-in branches */
  Blob *pCksum;               /* Repository checksum.  May be 0 */
  const char *zDateOvrd;      /* Date override.  If 0 then use 'now' */
  const char *zUserOvrd;      /* User override.  If 0 then use login_name() */
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
  char *zParentUuid = 0;      /* UUID of parent check-in */
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
  if( vid ){
    zParentUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d AND "
      "EXISTS(SELECT 1 FROM event WHERE event.type='ci' and event.objid=%d)",
      vid, vid);
    if( !zParentUuid ){
      fossil_fatal("Could not find a valid check-in for RID %d. "
                   "Possible checkout/repo mismatch.", vid);
    }
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

      mPerm = file_perm(blob_str(&filename), RepoFILE);
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
  if( vid ){
    blob_appendf(pOut, "P %s", zParentUuid);
    if( p->verifyDate ) checkin_verify_younger(vid, zParentUuid, zDate);
    free(zParentUuid);
    db_prepare(&q, "SELECT merge FROM vmerge WHERE id=0 OR id<-2");
    while( db_step(&q)==SQLITE_ROW ){
      char *zMergeUuid;
      int mid = db_column_int(&q, 0);
      if( (!g.markPrivate && content_is_private(mid)) || (mid == vid) ){
        continue;
      }
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
    "SELECT CASE vmerge.id WHEN -1 THEN '+' ELSE '-' END || mhash, merge"
    "  FROM vmerge"
    " WHERE (vmerge.id=-1 OR vmerge.id=-2)"
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
  db_prepare(&q, "SELECT mhash,merge FROM vmerge"
                 " WHERE id %s ORDER BY 1",
                 p->integrateFlag ? "IN(0,-4)" : "=(-4)");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zIntegrateUuid = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    if( is_a_leaf(rid) && !db_exists("SELECT 1 FROM tagxref "
        " WHERE tagid=%d AND rid=%d AND tagtype>0", TAG_CLOSED, rid)){
#if 0
      /* Make sure the check-in manifest of the resulting merge child does not
      ** include a +close tag referring to the leaf check-in on a private
      ** branch, so as not to generate a missing artifact reference on
      ** repository clones without that private branch.  The merge command
      ** should have dropped the --integrate option, at this point. */
      assert( !content_is_private(rid) );
#endif
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
  blob_appendf(pOut, "U %F\n", p->zUserOvrd ? p->zUserOvrd : login_name());
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
** been converted to UTF-8 (if it was UTF-16) with LF line-endings,
** and the original file will have been renamed to "<filename>-original".
*/
static int commit_warning(
  Blob *pContent,        /* The content of the file being committed. */
  int crlfOk,            /* Non-zero if CR/LF warnings should be disabled. */
  int binOk,             /* Non-zero if binary warnings should be disabled. */
  int encodingOk,        /* Non-zero if encoding warnings should be disabled. */
  int noPrompt,          /* 0 to always prompt, 1 for 'N', 2 for 'Y'. */
  const char *zFilename, /* The full name of the file being committed. */
  Blob *pReason          /* Reason for warning, if any (non-fatal only). */
){
  int bReverse;           /* UTF-16 byte order is reversed? */
  int fUnicode;           /* return value of could_be_utf16() */
  int fBinary;            /* does the blob content appear to be binary? */
  int lookFlags;          /* output flags from looks_like_utf8/utf16() */
  int fHasAnyCr;          /* the blob contains one or more CR chars */
  int fHasLoneCrOnly;     /* all detected line endings are CR only */
  int fHasCrLfOnly;       /* all detected line endings are CR/LF pairs */
  int fHasInvalidUtf8 = 0;/* contains invalid UTF-8 */
  char *zMsg;             /* Warning message */
  Blob fname;             /* Relative pathname of the file */
  static int allOk = 0;   /* Set to true to disable this routine */

  if( allOk ) return 0;
  fUnicode = could_be_utf16(pContent, &bReverse);
  if( fUnicode ){
    lookFlags = looks_like_utf16(pContent, bReverse, LOOK_NUL);
  }else{
    lookFlags = looks_like_utf8(pContent, LOOK_NUL);
    if( !(lookFlags & LOOK_BINARY) && invalid_utf8(pContent) ){
      fHasInvalidUtf8 = 1;
    }
  }
  fHasAnyCr = (lookFlags & LOOK_CR);
  fBinary = (lookFlags & LOOK_BINARY);
  fHasLoneCrOnly = ((lookFlags & LOOK_EOL) == LOOK_LONE_CR);
  fHasCrLfOnly = ((lookFlags & LOOK_EOL) == LOOK_CRLF);
  if( fUnicode || fHasAnyCr || fBinary || fHasInvalidUtf8 ){
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
        zConvert = ""; /* We cannot convert overlong lines. */
      }else{
        zWarning = "binary data";
        zConvert = ""; /* We cannot convert binary files. */
      }
      zDisable = "\"binary-glob\" setting";
    }else if( fUnicode && fHasAnyCr ){
      if( crlfOk && encodingOk ){
        return 0; /* We don't want CR/LF and Unicode warnings for this file. */
      }
      if( fHasLoneCrOnly ){
        zWarning = "CR line endings and Unicode";
      }else if( fHasCrLfOnly ){
        zWarning = "CR/LF line endings and Unicode";
      }else{
        zWarning = "mixed line endings and Unicode";
      }
      zDisable = "\"crlf-glob\" and \"encoding-glob\" settings";
    }else if( fHasInvalidUtf8 ){
      if( encodingOk ){
        return 0; /* We don't want encoding warnings for this file. */
      }
      zWarning = "invalid UTF-8";
      zDisable = "\"encoding-glob\" setting";
    }else if( fHasAnyCr ){
      if( crlfOk ){
        return 0; /* We don't want CR/LF warnings for this file. */
      }
      if( fHasLoneCrOnly ){
        zWarning = "CR line endings";
      }else if( fHasCrLfOnly ){
        zWarning = "CR/LF line endings";
      }else{
        zWarning = "mixed line endings";
      }
      zDisable = "\"crlf-glob\" setting";
    }else{
      if( encodingOk ){
        return 0; /* We don't want encoding warnings for this file. */
      }
      zWarning = "Unicode";
      zDisable = "\"encoding-glob\" setting";
    }
    file_relative_name(zFilename, &fname, 0);
    zMsg = mprintf(
         "%s contains %s. Use --no-warnings or the %s to"
                 " disable this warning.\n"
         "Commit anyhow (a=all/%sy/N)? ",
         blob_str(&fname), zWarning, zDisable, zConvert);
    if( noPrompt==0 ){
      prompt_user(zMsg, &ans);
      cReply = blob_str(&ans)[0];
      blob_reset(&ans);
    }else if( noPrompt==2 ){
      cReply = 'Y';
    }else{
      cReply = 'N';
    }
    fossil_free(zMsg);
    if( cReply=='a' || cReply=='A' ){
      allOk = 1;
    }else if( *zConvert && (cReply=='c' || cReply=='C') ){
      char *zOrig = file_newname(zFilename, "original", 1);
      FILE *f;
      blob_write_to_file(pContent, zOrig);
      fossil_free(zOrig);
      f = fossil_fopen(zFilename, "wb");
      if( f==0 ){
        fossil_warning("cannot open %s for writing", zFilename);
      }else{
        if( fUnicode ){
          int bomSize;
          const unsigned char *bom = get_utf8_bom(&bomSize);
          fwrite(bom, 1, bomSize, f);
          blob_to_utf8_no_bom(pContent, 0);
        }else if( fHasInvalidUtf8 ){
          blob_cp1252_to_utf8(pContent);
        }
        if( fHasAnyCr ){
          blob_to_lf_only(pContent);
        }
        fwrite(blob_buffer(pContent), 1, blob_size(pContent), f);
        fclose(f);
      }
      return 1;
    }else if( cReply!='y' && cReply!='Y' ){
      fossil_fatal("Abandoning commit due to %s in %s",
                   zWarning, blob_str(&fname));
    }else if( noPrompt==2 ){
      if( pReason ){
        blob_append(pReason, zWarning, -1);
      }
      return 1;
    }
    blob_reset(&fname);
  }
  return 0;
}

/*
** COMMAND: test-commit-warning
**
** Usage: %fossil test-commit-warning ?OPTIONS?
**
** Check each file in the checkout, including unmodified ones, using all
** the pre-commit checks.
**
** Options:
**    --no-settings     Do not consider any glob settings.
**    -v|--verbose      Show per-file results for all pre-commit checks.
**
** See also: commit, extras
*/
void test_commit_warning(void){
  int rc = 0;
  int noSettings;
  int verboseFlag;
  Stmt q;
  noSettings = find_option("no-settings",0,0)!=0;
  verboseFlag = find_option("verbose","v",0)!=0;
  verify_all_options();
  db_must_be_within_tree();
  db_prepare(&q,
      "SELECT %Q || pathname, pathname, %s, %s, %s FROM vfile"
      " WHERE NOT deleted",
      g.zLocalRoot,
      glob_expr("pathname", noSettings ? 0 : db_get("crlf-glob",
                                             db_get("crnl-glob",""))),
      glob_expr("pathname", noSettings ? 0 : db_get("binary-glob","")),
      glob_expr("pathname", noSettings ? 0 : db_get("encoding-glob",""))
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFullname;
    const char *zName;
    Blob content;
    Blob reason;
    int crlfOk, binOk, encodingOk;
    int fileRc;

    zFullname = db_column_text(&q, 0);
    zName = db_column_text(&q, 1);
    crlfOk = db_column_int(&q, 2);
    binOk = db_column_int(&q, 3);
    encodingOk = db_column_int(&q, 4);
    blob_zero(&content);
    blob_read_from_file(&content, zFullname, RepoFILE);
    blob_zero(&reason);
    fileRc = commit_warning(&content, crlfOk, binOk, encodingOk, 2,
                            zFullname, &reason);
    if( fileRc || verboseFlag ){
      fossil_print("%d\t%s\t%s\n", fileRc, zName, blob_str(&reason));
    }
    blob_reset(&reason);
    rc |= fileRc;
  }
  db_finalize(&q);
  fossil_print("%d\n", rc);
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
**    or: %fossil ci ?OPTIONS? ?FILE...?
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
** the --branchcolor option is not recommended.  Instead, let Fossil
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
** appears to be binary, Unicode text, or text with CR/LF line endings
** unless the interactive user chooses to proceed.  If there is no
** interactive user or these warnings should be skipped for some other
** reason, the --no-warnings option may be used.  A check-in is not
** allowed against a closed leaf.
**
** If a commit message is blank, you will be prompted:
** ("continue (y/N)?") to confirm you really want to commit with a
** blank commit message.  The default value is "N", do not commit.
**
** The --private option creates a private check-in that is never synced.
** Children of private check-ins are automatically private.
**
** The --tag option applies the symbolic tag name to the check-in.
**
** The --hash option detects edited files by computing each file's
** artifact hash rather than just checking for changes to its size or mtime.
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
**    --integrate                close all merged-in branches
**    -m|--comment COMMENT-TEXT  use COMMENT-TEXT as commit comment
**    -M|--message-file FILE     read the commit comment from given file
**    --mimetype MIMETYPE        mimetype of check-in comment
**    -n|--dry-run               If given, display instead of run actions
**    --no-prompt                This option disables prompting the user for
**                               input and assumes an answer of 'No' for every
**                               question.
**    --no-warnings              omit all warnings about file contents
**    --nosign                   do not attempt to sign this commit with gpg
**    --override-lock            allow a check-in even though parent is locked
**    --private                  do not sync changes and their descendants
**    --hash                     verify file status using hashing rather
**                               than relying on file mtimes
**    --tag TAG-NAME             assign given tag TAG-NAME to the check-in
**    --date-override DATETIME   DATE to use instead of 'now'
**    --user-override USER       USER to use instead of the current default
**
** DATETIME may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
** year-month-day form, it may be truncated, the "T" may be replaced by
** a space, and it may also name a timezone offset from UTC as "-HH:MM"
** (westward) or "+HH:MM" (eastward). Either no timezone suffix or "Z"
** means UTC.
**
** See also: branch, changes, checkout, extras, sync
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
  int useHash = 0;       /* True to verify file status using hashing */
  int noSign = 0;        /* True to omit signing the manifest using GPG */
  int privateFlag = 0;   /* True if the --private option is present */
  int privateParent = 0; /* True if the parent check-in is private */
  int isAMerge = 0;      /* True if checking in a merge */
  int noWarningFlag = 0; /* True if skipping all warnings */
  int noPrompt = 0;      /* True if skipping all prompts */
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
  ManifestFile *pFile;   /* File structure in the manifest */
  Manifest *pManifest;   /* Manifest structure */
  Blob manifest;         /* Manifest in baseline form */
  Blob muuid;            /* Manifest uuid */
  Blob cksum1, cksum2;   /* Before and after commit checksums */
  Blob cksum1b;          /* Checksum recorded in the manifest */
  int szD;               /* Size of the delta manifest */
  int szB;               /* Size of the baseline manifest */
  int nConflict = 0;     /* Number of unresolved merge conflicts */
  int abortCommit = 0;   /* Abort the commit due to text format conversions */
  Blob ans;              /* Answer to continuation prompts */
  char cReply;           /* First character of ans */
  int bRecheck = 0;      /* Repeat fork and closed-branch checks*/

  memset(&sCiInfo, 0, sizeof(sCiInfo));
  url_proxy_options();
  /* --sha1sum is an undocumented alias for --hash for backwards compatiblity */
  useHash = find_option("hash",0,0)!=0 || find_option("sha1sum",0,0)!=0;
  noSign = find_option("nosign",0,0)!=0;
  privateFlag = find_option("private",0,0)!=0;
  forceDelta = find_option("delta",0,0)!=0;
  forceBaseline = find_option("baseline",0,0)!=0;
  if( forceDelta ){
    if( forceBaseline ){
      fossil_fatal("cannot use --delta and --baseline together");
    }
    if( db_get_boolean("forbid-delta-manifests",0) ){
      fossil_fatal("delta manifests are prohibited in this repository");
    }
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
  if( find_option("override-lock",0,0)!=0 ) allowFork = 1;
  allowOlder = find_option("allow-older",0,0)!=0;
  noPrompt = find_option("no-prompt", 0, 0)!=0;
  noWarningFlag = find_option("no-warnings", 0, 0)!=0;
  sCiInfo.zBranch = find_option("branch","b",1);
  sCiInfo.zColor = find_option("bgcolor",0,1);
  sCiInfo.zBrClr = find_option("branchcolor",0,1);
  sCiInfo.closeFlag = find_option("close",0,0)!=0;
  sCiInfo.integrateFlag = find_option("integrate",0,0)!=0;
  sCiInfo.zMimetype = find_option("mimetype",0,1);
  while( (zTag = find_option("tag",0,1))!=0 ){
    if( zTag[0]==0 ) continue;
    sCiInfo.azTag = fossil_realloc((void*)sCiInfo.azTag,
                                    sizeof(char*)*(nTag+2));
    sCiInfo.azTag[nTag++] = zTag;
    sCiInfo.azTag[nTag] = 0;
  }
  zComFile = find_option("message-file", "M", 1);
  sCiInfo.zDateOvrd = find_option("date-override",0,1);
  sCiInfo.zUserOvrd = find_option("user-override",0,1);
  db_must_be_within_tree();
  noSign = db_get_boolean("omitsign", 0)|noSign;
  if( db_get_boolean("clearsign", 0)==0 ){ noSign = 1; }
  useCksum = db_get_boolean("repo-cksum", 1);
  outputManifest = db_get_manifest_setting();
  verify_all_options();

  /* Get the ID of the parent manifest artifact */
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    useCksum = 1;
    if( privateFlag==0 && sCiInfo.zBranch==0 ) {
      sCiInfo.zBranch=db_get("main-branch", 0);
    }
  }else{
    privateParent = content_is_private(vid);
  }

  /* Track the "private" status */
  g.markPrivate = privateFlag || privateParent;
  if( privateFlag && !privateParent ){
    /* Apply default branch name ("private") and color ("orange") if not
    ** specified otherwise on the command-line, and if the parent is not
    ** already private. */
    if( sCiInfo.zBranch==0 ) sCiInfo.zBranch = "private";
    if( sCiInfo.zBrClr==0 && sCiInfo.zColor==0 ) sCiInfo.zBrClr = "#fec084";
  }

  /* Do not allow the creation of a new branch using an existing open
  ** branch name unless the --force flag is used */
  if( sCiInfo.zBranch!=0
   && !forceFlag
   && fossil_strcmp(sCiInfo.zBranch,"private")!=0
   && branch_is_open(sCiInfo.zBranch)
  ){
    fossil_fatal("an open branch named \"%s\" already exists - use --force"
                 " to override", sCiInfo.zBranch);
  }

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
  if( !forceDelta
   && !db_get_boolean("seen-delta-manifest",0)
   && !db_get_boolean("forbid-delta-manifests",0)
  ){
    forceBaseline = 1;
  }

  /*
  ** Autosync if autosync is enabled and this is not a private check-in.
  */
  if( !g.markPrivate ){
    int syncFlags = SYNC_PULL;
    if( vid!=0 && !allowFork && !forceFlag ){
      syncFlags |= SYNC_CKIN_LOCK;
    }
    if( autosync_loop(syncFlags, db_get_int("autosync-tries", 1), 1) ){
      fossil_exit(1);
    }
  }

  /* Require confirmation to continue with the check-in if there is
  ** clock skew
  */
  if( g.clockSkewSeen ){
    if( !noPrompt ){
      prompt_user("continue in spite of time skew (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      blob_reset(&ans);
    }else{
      fossil_print("Abandoning commit due to time skew\n");
      cReply = 'N';
    }
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
    if( !noPrompt ){
      prompt_user("continue (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      blob_reset(&ans);
    }else{
      cReply = 'N';
    }
    if( cReply!='y' && cReply!='Y' ){
      fossil_exit(1);
    }
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

  hasChanges = unsaved_changes(useHash ? CKSIG_HASH : 0);
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

  /* This loop checks for potential forks and for check-ins against a
  ** closed branch.  The checks are repeated once after interactive
  ** check-in comment editing.
  */
  do{
    /*
    ** Do not allow a commit that will cause a fork unless the --allow-fork
    ** or --force flags is used, or unless this is a private check-in.
    ** The initial commit MUST have tags "trunk" and "sym-trunk".
    */
    if( sCiInfo.zBranch==0
     && allowFork==0
     && forceFlag==0
     && g.markPrivate==0
     && (vid==0 || !is_a_leaf(vid) || g.ckinLockFail)
    ){
      if( g.ckinLockFail ){
        fossil_fatal("Might fork due to a check-in race with user \"%s\"\n"
                     "Try \"update\" first, or --branch, or "
                     "use --override-lock",
                     g.ckinLockFail);
      }else{
        fossil_fatal("Would fork.  \"update\" first or use --branch or "
                     "--allow-fork.");
      }
    }
  
    /*
    ** Do not allow a commit against a closed leaf unless the commit
    ** ends up on a different branch.
    */
    if(
        /* parent check-in has the "closed" tag... */
       leaf_is_closed(vid)
        /* ... and the new check-in has no --branch option or the --branch
        ** option does not actually change the branch */
     && (sCiInfo.zBranch==0
         || db_exists("SELECT 1 FROM tagxref"
                      " WHERE tagid=%d AND rid=%d AND tagtype>0"
                      "   AND value=%Q", TAG_BRANCH, vid, sCiInfo.zBranch))
    ){
      fossil_fatal("cannot commit against a closed leaf");
    }

    /* Always exit the loop on the second pass */
    if( bRecheck ) break;
  
    /* Get the check-in comment.  This might involve prompting the
    ** user for the check-in comment, in which case we should resync
    ** to renew the check-in lock and repeat the checks for conflicts.
    */
    if( zComment ){
      blob_zero(&comment);
      blob_append(&comment, zComment, -1);
    }else if( zComFile ){
      blob_zero(&comment);
      blob_read_from_file(&comment, zComFile, ExtFILE);
      blob_to_utf8_no_bom(&comment, 1);
    }else if( dryRunFlag ){
      blob_zero(&comment);
    }else if( !noPrompt ){
      char *zInit = db_text(0,"SELECT value FROM vvar WHERE name='ci-comment'");
      prepare_commit_comment(&comment, zInit, &sCiInfo, vid);
      if( zInit && zInit[0] && fossil_strcmp(zInit, blob_str(&comment))==0 ){
        prompt_user("unchanged check-in comment.  continue (y/N)? ", &ans);
        cReply = blob_str(&ans)[0];
        blob_reset(&ans);
        if( cReply!='y' && cReply!='Y' ){
          fossil_exit(1);
        }
      }
      free(zInit);
      db_multi_exec("REPLACE INTO vvar VALUES('ci-comment',%B)", &comment);
      db_end_transaction(0);
      db_begin_transaction();
      if( !g.markPrivate && vid!=0 && !allowFork && !forceFlag ){
        /* Do another auto-pull, renewing the check-in lock.  Then set
        ** bRecheck so that we loop back above to verify that the check-in
        ** is still not against a closed branch and still won't fork. */
        int syncFlags = SYNC_PULL|SYNC_CKIN_LOCK;
        if( autosync_loop(syncFlags, db_get_int("autosync-tries", 1), 1) ){
          fossil_exit(1);
        }
        bRecheck = 1;
      }
    }
  }while( bRecheck );

  if( blob_size(&comment)==0 ){
    if( !dryRunFlag ){
      if( !noPrompt ){
        prompt_user("empty check-in comment.  continue (y/N)? ", &ans);
        cReply = blob_str(&ans)[0];
        blob_reset(&ans);
      }else{
        fossil_print("Abandoning commit due to empty check-in comment\n");
        cReply = 'N';
      }
      if( cReply!='y' && cReply!='Y' ){
        fossil_exit(1);
      }
    }
  }

  /*
  ** Step 1: Compute an aggregate MD5 checksum over the disk image
  ** of every file in vid.  The file names are part of the checksum.
  ** The resulting checksum is the same as is expected on the R-card
  ** of a manifest.
  */
  if( useCksum ) vfile_aggregate_checksum_disk(vid, &cksum1);

  /* Step 2: Insert records for all modified files into the blob
  ** table. If there were arguments passed to this command, only
  ** the identified files are inserted (if they have been modified).
  */
  db_prepare(&q,
    "SELECT id, %Q || pathname, mrid, %s, %s, %s FROM vfile "
    "WHERE chnged==1 AND NOT deleted AND is_selected(id)",
    g.zLocalRoot,
    glob_expr("pathname", db_get("crlf-glob",db_get("crnl-glob",""))),
    glob_expr("pathname", db_get("binary-glob","")),
    glob_expr("pathname", db_get("encoding-glob",""))
  );
  while( db_step(&q)==SQLITE_ROW ){
    int id, rid;
    const char *zFullname;
    Blob content;
    int crlfOk, binOk, encodingOk;

    id = db_column_int(&q, 0);
    zFullname = db_column_text(&q, 1);
    rid = db_column_int(&q, 2);
    crlfOk = db_column_int(&q, 3);
    binOk = db_column_int(&q, 4);
    encodingOk = db_column_int(&q, 5);

    blob_zero(&content);
    blob_read_from_file(&content, zFullname, RepoFILE);
    /* Do not emit any warnings when they are disabled. */
    if( !noWarningFlag ){
      abortCommit |= commit_warning(&content, crlfOk, binOk,
                                    encodingOk, noPrompt,
                                    zFullname, 0);
    }
    if( contains_merge_marker(&content) ){
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
      content_deltify(rid, &nrid, 1, 0);
    }
    db_multi_exec("UPDATE vfile SET mrid=%d, rid=%d, mhash=NULL WHERE id=%d",
                  nrid,nrid,id);
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
      ** which would work for this check-in.  The first manifest (held
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
    if( !noPrompt ){
      prompt_user("unable to sign manifest.  continue (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      blob_reset(&ans);
    }else{
      fossil_print("Abandoning commit due to manifest signing failure\n");
      cReply = 'N';
    }
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
  if( outputManifest & MFESTFLG_RAW ){
    zManifestFile = mprintf("%smanifest", g.zLocalRoot);
    blob_write_to_file(&manifest, zManifestFile);
    blob_reset(&manifest);
    blob_read_from_file(&manifest, zManifestFile, ExtFILE);
    free(zManifestFile);
  }

  nvid = content_put(&manifest);
  if( nvid==0 ){
    fossil_fatal("trouble committing manifest: %s", g.zErrMsg);
  }
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nvid);
  if( manifest_crosslink(nvid, &manifest,
                         dryRunFlag ? MC_NONE : MC_PERMIT_HOOKS)==0 ){
    fossil_fatal("%s", g.zErrMsg);
  }
  assert( blob_is_reset(&manifest) );
  content_deltify(vid, &nvid, 1, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", nvid);

  db_prepare(&q, "SELECT mhash,merge FROM vmerge WHERE id=-4");
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
  if( outputManifest & MFESTFLG_UUID ){
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
    "UPDATE vfile SET rid=mrid, mhash=NULL, chnged=0, deleted=0, origname=NULL"
    " WHERE is_selected(id);"
    , vid, nvid
  );
  db_set_checkout(nvid);

  /* Update the isexe and islink columns of the vfile table */
  db_prepare(&q,
    "UPDATE vfile SET isexe=:exec, islink=:link"
    " WHERE vid=:vid AND pathname=:path AND (isexe!=:exec OR islink!=:link)"
  );
  db_bind_int(&q, ":vid", nvid);
  pManifest = manifest_get(nvid, CFTYPE_MANIFEST, 0);
  manifest_file_rewind(pManifest);
  while( (pFile = manifest_file_next(pManifest, 0)) ){
    db_bind_int(&q, ":exec", pFile->zPerm && strstr(pFile->zPerm, "x"));
    db_bind_int(&q, ":link", pFile->zPerm && strstr(pFile->zPerm, "l"));
    db_bind_text(&q, ":path", pFile->zName);
    db_step(&q);
    db_reset(&q);
  }
  db_finalize(&q);
  manifest_destroy(pManifest);

  if( useCksum ){
    /* Verify that the repository checksum matches the expected checksum
    ** calculated before the check-in started (and stored as the R record
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
  db_multi_exec("PRAGMA repository.application_id=252006673;");
  db_multi_exec("PRAGMA localdb.application_id=252006674;");
  if( dryRunFlag ){
    leaf_ambiguity_warning(nvid,nvid);
    db_end_transaction(1);
    exit(1);
  }
  db_end_transaction(0);

  if( outputManifest & MFESTFLG_TAGS ){
    Blob tagslist;
    zManifestFile = mprintf("%smanifest.tags", g.zLocalRoot);
    blob_zero(&tagslist);
    get_checkin_taglist(nvid, &tagslist);
    blob_write_to_file(&tagslist, zManifestFile);
    blob_reset(&tagslist);
    free(zManifestFile);
  }

  if( !g.markPrivate ){
    int syncFlags = SYNC_PUSH | SYNC_PULL | SYNC_IFABLE;
    int nTries = db_get_int("autosync-tries",1);
    autosync_loop(syncFlags, nTries, 0);
  }
  if( count_nonbranch_children(vid)>1 ){
    fossil_print("**** warning: a fork has occurred *****\n");
  }else{
    leaf_ambiguity_warning(nvid,nvid);
  }
}

/*
** State for the "mini-checkin" infrastructure, which enables the
** ability to commit changes to a single file without a checkout
** db, e.g. for use via an HTTP request.
**
** Memory for all non-const (char *) members is owned by the
** CheckinMiniInfo instance and is freed by CheckinMiniInfo_cleanup().
*/
struct CheckinMiniInfo {
  Manifest * pParent;  /* parent checkin. Memory is owned by this
                          object. */
  char *zParentUuid;   /* Full UUID of pParent */
  char *zFilename;     /* Name of single file to commit. Must be
                          relative to the top of the repo. */
  Blob fileContent;    /* Content of file referred to by zFilename. */
  Blob fileHash;       /* Hash of this->fileContent, using the repo's
                          preferred hash method. */
  Blob comment;        /* Check-in comment text */
  char *zMimetype;     /* Mimetype of comment. May be NULL */
  char *zUser;         /* User name */
  char *zDate;         /* Optionally force this date string (anything
                          supported by date_in_standard_format()).
                          Maybe be NULL. */
  Blob *pMfOut;        /* If not NULL, checkin_mini() will write a
                          copy of the generated manifest here. This
                          memory is NOT owned by CheckinMiniInfo. */
  int filePerm;        /* Permissions (via file_perm()) of the input
                          file. We need to store this before calling
                          checkin_mini() because the real input file
                          name may differ from the repo-centric
                          this->zFilename, and checkin_mini() requires
                          the permissions of the original file. For
                          web commits, set this to PERM_REG or (when
                          editing executable scripts) PERM_EXE before
                          calling checkin_mini(). */
  int flags;           /* Bitmask of fossil_cimini_flags. */
};
typedef struct CheckinMiniInfo CheckinMiniInfo;

/*
** CheckinMiniInfo::flags values.
*/
enum fossil_cimini_flags {
CIMINI_NONE = 0,
/*
** Tells checkin_mini() to use dry-run mode.
*/
CIMINI_DRY_RUN = 1,
/*
** Tells checkin_mini() to allow forking from a non-leaf commit.
*/
CIMINI_ALLOW_FORK = 1<<1,
/*
** Tells checkin_mini() to dump its generated manifest to stdout.
*/
CIMINI_DUMP_MANIFEST = 1<<2,

/*
** By default, content containing what appears to be a merge conflict
** marker is not permitted. This flag relaxes that requirement.
*/
CIMINI_ALLOW_MERGE_MARKER = 1<<3,

/*
** By default mini-checkins are not allowed to be "older"
** than their parent. i.e. they may not have a timestamp
** which predates their parent. This flag bypasses that
** check.
*/
CIMINI_ALLOW_OLDER = 1<<4,

/*
** Indicates that the content of the newly-checked-in file is
** converted, if needed, to use the same EOL style as the previous
** version of that file. Only the in-memory/in-repo copies are
** affected, not the original file (if any).
*/
CIMINI_CONVERT_EOL_INHERIT = 1<<5,
/*
** Indicates that the input's EOLs should be converted to Unix-style.
*/
CIMINI_CONVERT_EOL_UNIX = 1<<6,
/*
** Indicates that the input's EOLs should be converted to Windows-style.
*/
CIMINI_CONVERT_EOL_WINDOWS = 1<<7,
/*
** A hint to checkin_mini() to "prefer" creation of a delta manifest.
** It may decide not to for various reasons.
*/
CIMINI_PREFER_DELTA = 1<<8,
/*
** A "stronger hint" to checkin_mini() to prefer creation of a delta
** manifest if it at all can. It will decide not to only if creation
** of a delta is not a realistic option. For this to work, it must be
** set together with the CIMINI_PREFER_DELTA flag, but the two cannot
** be combined in this enum.
**
** This option is ONLY INTENDED FOR TESTING, used in bypassing
** heuristics which may otherwise disable generation of a delta on the
** grounds of efficiency (e.g. not generating a delta if the parent
** non-delta only has a few F-cards).
**
** The forbid-delta-manifests repo config option trumps this.
*/
CIMINI_STRONGLY_PREFER_DELTA = 1<<9,
/*
** Tells checkin_mini() to permit the addition of a new file. Normally
** this is disabled because there are many cases where it could cause
** the inadvertent addition of a new file when an update to an
** existing was intended, as a side-effect of name-case differences.
*/
CIMINI_ALLOW_NEW_FILE = 1<<10
};

/*
** Initializes p to a known-valid default state.
*/
static void CheckinMiniInfo_init( CheckinMiniInfo * p ){
  memset(p, 0, sizeof(CheckinMiniInfo));
  p->flags = CIMINI_NONE;
  p->filePerm = -1;
  p->comment = p->fileContent = p->fileHash = empty_blob;
}

/*
** Frees all memory owned by p, but does not free p.
 */
static void CheckinMiniInfo_cleanup( CheckinMiniInfo * p ){
  blob_reset(&p->comment);
  blob_reset(&p->fileContent);
  blob_reset(&p->fileHash);
  if(p->pParent){
    manifest_destroy(p->pParent);
  }
  fossil_free(p->zFilename);
  fossil_free(p->zMimetype);
  fossil_free(p->zParentUuid);
  fossil_free(p->zUser);
  fossil_free(p->zDate);
  CheckinMiniInfo_init(p);
}

/*
** Internal helper which returns an F-card perms string suitable for
** writing into a manifest.
*/
static const char * mfile_permint_mstring(int perm){
  switch(perm){
    case PERM_EXE: return " x";
    case PERM_LNK: return " l";
    default: return "";
  }
}

/*
** Given a ManifestFile permission string (or NULL), it returns one of
** PERM_REG, PERM_EXE, or PERM_LNK.
*/
static int mfile_permstr_int(const char *zPerm){
  if(!zPerm || !*zPerm) return PERM_REG;
  else if(strstr(zPerm,"x")) return PERM_EXE;
  else if(strstr(zPerm,"l")) return PERM_LNK;
  else return PERM_REG/*???*/;
}

static const char * mfile_perm_mstring(const ManifestFile * p){
  return mfile_permint_mstring(manifest_file_mperm(p));
}

/*
** Internal helper for checkin_mini() and friends. Appends an F-card
** for p to pOut.
*/
static void checkin_mini_append_fcard(Blob *pOut, const ManifestFile *p){
  if(p->zUuid){
    assert(*p->zUuid);
    blob_appendf(pOut, "F %F %s%s", p->zName,
                 p->zUuid, mfile_perm_mstring(p));
    if(p->zPrior){
      assert(*p->zPrior);
      blob_appendf(pOut, " %F\n", p->zPrior);
    }else{
      blob_append(pOut, "\n", 1);
    }
  }else{
    /* File was removed from parent delta. */
    blob_appendf(pOut, "F %F\n", p->zName);
  }
}
/*
** Handles the F-card parts for create_manifest_mini().
**
** If asDelta is true, F-cards will be handled as for a delta
** manifest, and the caller MUST have added a B-card to pOut before
** calling this.
**
** Returns 1 on success, 0 on error, and writes any error message to
** pErr (if it's not NULL). The only non-immediately-fatal/panic error
** is if pCI->filePerm is PERM_LNK or pCI would update a PERM_LNK
** in-repo file.
*/
static int create_manifest_mini_fcards( Blob * pOut,
                                        CheckinMiniInfo * pCI,
                                        int asDelta,
                                        Blob * pErr){
  int wroteThisCard = 0;
  const ManifestFile * pFile;
  int (*fncmp)(char const *, char const *) =  /* filename comparator */
    filenames_are_case_sensitive()
    ? fossil_strcmp
    : fossil_stricmp;
#define mf_err(EXPR) if(pErr) blob_appendf EXPR; return 0
#define write_this_card(NAME) \
  blob_appendf(pOut, "F %F %b%s\n", (NAME), &pCI->fileHash, \
               mfile_permint_mstring(pCI->filePerm)); \
  wroteThisCard = 1

  assert(pCI->filePerm!=PERM_LNK && "This should have been validated before.");
  assert(pCI->filePerm==PERM_REG || pCI->filePerm==PERM_EXE);
  if(PERM_LNK==pCI->filePerm){
    goto err_no_symlink;
  }
  manifest_file_rewind(pCI->pParent);
  if(asDelta!=0 && (pCI->pParent->zBaseline==0
                    || pCI->pParent->nFile==0)){
    /* Parent is a baseline or a delta with no F-cards, so this is
    ** the simplest case: create a delta with a single F-card.
    */
    pFile = manifest_file_find(pCI->pParent, pCI->zFilename);
    if(pFile!=0 && manifest_file_mperm(pFile)==PERM_LNK){
      goto err_no_symlink;
    }
    write_this_card(pFile ? pFile->zName : pCI->zFilename);
    return 1;
  }
  while(1){
    int cmp;
    if(asDelta==0){
      pFile = manifest_file_next(pCI->pParent, 0);
    }else{
      /* Parent is a delta manifest with F-cards. Traversal of delta
      ** manifest file entries is normally done via
      ** manifest_file_next(), which takes into account the
      ** differences between the delta and its parent and returns
      ** F-cards from both. Each successive delta from the same
      ** baseline includes all F-card changes from the previous
      ** deltas, so we instead clone the parent's F-cards except for
      ** the one (if any) which matches the new file.
      */
      pFile = pCI->pParent->iFile < pCI->pParent->nFile
        ? &pCI->pParent->aFile[pCI->pParent->iFile++]
        : 0;
    }
    if(0==pFile) break;
    cmp = fncmp(pFile->zName, pCI->zFilename);
    if(cmp<0){
      checkin_mini_append_fcard(pOut,pFile);
    }else{
      if(cmp==0 || 0==wroteThisCard){
        assert(0==wroteThisCard);
        if(PERM_LNK==manifest_file_mperm(pFile)){
          goto err_no_symlink;
        }
        write_this_card(cmp==0 ? pFile->zName : pCI->zFilename);
      }
      if(cmp>0){
        assert(wroteThisCard!=0);
        checkin_mini_append_fcard(pOut,pFile);
      }
    }
  }
  if(wroteThisCard==0){
    write_this_card(pCI->zFilename);
  }
  return 1;
err_no_symlink:
  mf_err((pErr,"Cannot commit or overwrite symlinks "
          "via mini-checkin."));
  return 0;
#undef write_this_card
#undef mf_err
}

/*
** Creates a manifest file, written to pOut, from the state in the
** fully-populated and semantically valid pCI argument. pCI is not
** *semantically* modified but cannot be const because blob_str() may
** need to NUL-terminate any given blob.
**
** Returns true on success. On error, returns 0 and, if pErr is not
** NULL, writes an error message there.
**
** Intended only to be called via checkin_mini() or routines which
** have already completely vetted pCI.
*/
static int create_manifest_mini( Blob * pOut, CheckinMiniInfo * pCI,
                                 Blob * pErr){
  Blob zCard = empty_blob;     /* Z-card checksum */
  int asDelta = 0;
#define mf_err(EXPR) if(pErr) blob_appendf EXPR; return 0

  assert(blob_str(&pCI->fileHash));
  assert(pCI->pParent);
  assert(pCI->zFilename);
  assert(pCI->zUser);
  assert(pCI->zDate);

  /* Potential TODOs include...
  **
  ** - Maybe add support for tags. Those can be edited via /info page,
  **   and feel like YAGNI/feature creep for this purpose.
  */
  blob_zero(pOut);
  manifest_file_rewind(pCI->pParent) /* force load of baseline */;
  /* Determine whether we want to create a delta manifest... */
  if((CIMINI_PREFER_DELTA & pCI->flags)
     && ((CIMINI_STRONGLY_PREFER_DELTA & pCI->flags)
         || (pCI->pParent->pBaseline
             ? pCI->pParent->pBaseline
             : pCI->pParent)->nFile > 15
         /* 15 is arbitrary: don't create a delta when there is only a
         ** tiny gain for doing so. That heuristic is not *quite*
         ** right, in that when we're deriving from another delta, we
         ** really should compare the F-card count between it and its
         ** baseline, and create a delta if the baseline has (say)
         ** twice or more as many F-cards as the previous delta. */)
     && !db_get_boolean("forbid-delta-manifests",0)
     ){
    asDelta = 1;
    blob_appendf(pOut, "B %s\n",
                 pCI->pParent->zBaseline
                 ? pCI->pParent->zBaseline
                 : pCI->zParentUuid);
  }
  blob_reserve(pOut, 1024 *
               (asDelta ? 2 : pCI->pParent->nFile/11+1
                /* In the fossil core repo, each 12-ish F-cards (on
                ** average) take up roughly 1kb */));
  if(blob_size(&pCI->comment)!=0){
    blob_appendf(pOut, "C %F\n", blob_str(&pCI->comment));
  }else{
    blob_append(pOut, "C (no\\scomment)\n", 16);
  }
  blob_appendf(pOut, "D %z\n", pCI->zDate);
  if(create_manifest_mini_fcards(pOut,pCI,asDelta,pErr)==0){
    return 0;
  }
  if(pCI->zMimetype!=0 && pCI->zMimetype[0]!=0){
    blob_appendf(pOut, "N %F\n", pCI->zMimetype);
  }
  blob_appendf(pOut, "P %s\n", pCI->zParentUuid);
  blob_appendf(pOut, "U %F\n", pCI->zUser);
  md5sum_blob(pOut, &zCard);
  blob_appendf(pOut, "Z %b\n", &zCard);
  blob_reset(&zCard);
  return 1;
#undef mf_err
}

/*
** EXPERIMENTAL! Subject to change or removal at any time.
**
** A so-called "single-file/mini/web checkin" is a slimmed-down form
** of the checkin command which accepts only a single file and is
** intended to accept edits to a file via the web interface or from
** the CLI from outside of a checkout.
**
** Being fully non-interactive is a requirement for this function,
** thus it cannot perform autosync or similar activities.
**
** This routine uses the state from the given fully-populated pCI
** argument to add pCI->fileContent to the database, and create and
** save a manifest for that change. Ownership of pCI and its contents
** are unchanged.
**
** This function may may modify pCI as follows:
**
** - If one of Manifest pCI->pParent or pCI->zParentUuid are NULL,
**   then the other will be assigned based on its counterpart. Both
**   may not be NULL.
**
** - pCI->zDate is normalized to/replaced with a valid date/time
**   string. If its original value cannot be validated then
**   this function fails. If pCI->zDate is NULL, the current time
**   is used.
**
** - If the CIMINI_CONVERT_EOL_INHERIT flag is set,
**   pCI->fileContent appears to be plain text, and its line-ending
**   style differs from its previous version, it is converted to the
**   same EOL style as the previous version. If this is done, the
**   pCI->fileHash is re-computed. Note that only pCI->fileContent,
**   not the original file, is affected by the conversion.
**
** - If pCI->fileHash is empty, this routine populates it with the
**   repository's preferred hash algorithm.
**
** - pCI->comment may be converted to Unix-style newlines.
**
** pCI's ownership is not modified.
**
** This function validates several of the inputs and fails if any
** validation fails.
**
** On error, returns false (0) and, if pErr is not NULL, writes a
** diagnostic message there.
** 
** Returns true on success. If pRid is not NULL, the RID of the
** resulting manifest is written to *pRid.
**
** The checkin process is largely influenced by pCI->flags, and that
** must be populated before calling this. See the fossil_cimini_flags
** enum for the docs for each flag.
*/
static int checkin_mini(CheckinMiniInfo * pCI, int *pRid, Blob * pErr){
  Blob mf = empty_blob;             /* output manifest */
  int rid = 0, frid = 0;            /* various RIDs */
  int isPrivate;                    /* whether this is private content
                                       or not */
  ManifestFile * zFilePrev;         /* file entry from pCI->pParent */
  int prevFRid = 0;                 /* RID of file's prev. version */
#define ci_err(EXPR) if(pErr!=0){blob_appendf EXPR;} goto ci_error

  if(!(pCI->flags & CIMINI_DRY_RUN)){
    /* Until this feature is fully vetted, disallow it in the main
    ** fossil repo unless dry-run mode is being used. */
    char * zProjCode = db_get("project-code",0);
    assert(zProjCode);
    if(0==fossil_stricmp("CE59BB9F186226D80E49D1FA2DB29F935CCA0333",
                         zProjCode)){
      fossil_fatal("Never, ever run this in/on the core fossil repo "
                   "in non-dry-run mode until it's been well-vetted. "
                   "Use a temp/test repo.");
    }
    fossil_free(zProjCode);
  }
  db_begin_transaction();

  if(pCI->pParent==0 && pCI->zParentUuid){
    ci_err((pErr, "Cannot determine parent version."));
  }
  else if(pCI->pParent==0){
    pCI->pParent = manifest_get_by_name(pCI->zParentUuid, 0);
    if(pCI->pParent==0){
      ci_err((pErr,"Cannot load manifest for [%S].", pCI->zParentUuid));
    }
  }else if(pCI->zParentUuid==0){
    pCI->zParentUuid = rid_to_uuid(pCI->pParent->rid);
    assert(pCI->zParentUuid);
  }

  assert(pCI->pParent->rid>0);
  if(leaf_is_closed(pCI->pParent->rid)){
    ci_err((pErr,"Cannot commit to a closed leaf."));
    /* Remember that in order to override this we'd also need to
    ** cancel TAG_CLOSED on pCI->pParent. There would seem to be no
    ** reason we can't do that via the generated manifest, but the
    ** commit command does not offer that option, so mini-checkin
    ** probably shouldn't, either.
    */
  }
  if( !db_exists("SELECT 1 FROM user WHERE login=%Q", pCI->zUser) ){
    ci_err((pErr,"No such user: %s", pCI->zUser));
  }
  if(!(CIMINI_ALLOW_FORK & pCI->flags)
     && !is_a_leaf(pCI->pParent->rid)){
    ci_err((pErr,"Parent [%S] is not a leaf and forking is disabled.",
            pCI->zParentUuid));
  }
  if(!(CIMINI_ALLOW_MERGE_MARKER & pCI->flags)
     && contains_merge_marker(&pCI->fileContent)){
    ci_err((pErr,"Content appears to contain a merge conflict marker."));
  }
  if(!file_is_simple_pathname(pCI->zFilename, 1)){
    ci_err((pErr,"Invalid filename for use in a repository: %s",
            pCI->zFilename));
  }
  if(!(CIMINI_ALLOW_OLDER & pCI->flags)
     && !checkin_is_younger(pCI->pParent->rid, pCI->zDate)){
    ci_err((pErr,"Checkin time (%s) may not be older "
            "than its parent (%z).",
            pCI->zDate,
            db_text(0, "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%f',%lf)",
                    pCI->pParent->rDate)
            ));
  }
  {
    /*
    ** Normalize the timestamp. We don't use date_in_standard_format()
    ** because that has side-effects we don't want to trigger here.
    */
    char * zDVal = db_text(
         0, "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%f',%Q)",
         pCI->zDate ? pCI->zDate : "now");
    if(zDVal==0 || zDVal[0]==0){
      fossil_free(zDVal);
      ci_err((pErr,"Invalid timestamp string: %s", pCI->zDate));
    }
    fossil_free(pCI->zDate);
    pCI->zDate = zDVal;
  }
  { /* Confirm that only one EOL policy is in place. */
    int n = 0;
    if(CIMINI_CONVERT_EOL_INHERIT & pCI->flags) ++n;
    if(CIMINI_CONVERT_EOL_UNIX & pCI->flags) ++n;
    if(CIMINI_CONVERT_EOL_WINDOWS & pCI->flags) ++n;
    if(n>1){
      ci_err((pErr,"More than 1 EOL conversion policy was specified."));
    }
  }
  /* Potential TODOs include:
  **
  ** - Commit allows an empty checkin only with a flag, but we
  **   currently disallow it entirely. Conform with commit?
  **
  ** Non-TODOs:
  **
  ** - Check for a commit lock would require auto-sync, which this
  **   code cannot do if it's going to be run via a web page.
  */

  /*
  ** Confirm that pCI->zFilename can be found in pCI->pParent.  If
  ** not, fail unless the CIMINI_ALLOW_NEW_FILE flag is set. This is
  ** admittedly an artificial limitation, not strictly necessary. We
  ** do it to hopefully reduce the chance of an "oops" where file
  ** X/Y/z gets committed as X/Y/Z or X/y/z due to a typo or
  ** case-sensitivity mismatch between the user/repo/filesystem, or
  ** some such.
  */
  manifest_file_rewind(pCI->pParent);
  zFilePrev = manifest_file_find(pCI->pParent, pCI->zFilename);
  if(!(CIMINI_ALLOW_NEW_FILE & pCI->flags)
     && (!zFilePrev
         || !zFilePrev->zUuid/*was removed from parent delta manifest*/)
     ){
    ci_err((pErr,"File [%s] not found in manifest [%S]. "
            "Adding new files is currently not permitted.",
            pCI->zFilename, pCI->zParentUuid));
  }else if(zFilePrev
           && manifest_file_mperm(zFilePrev)==PERM_LNK){
    ci_err((pErr,"Cannot save a symlink via a mini-checkin."));
  }
  if(zFilePrev){
    prevFRid = fast_uuid_to_rid(zFilePrev->zUuid);
  }

  if(((CIMINI_CONVERT_EOL_INHERIT & pCI->flags)
      || (CIMINI_CONVERT_EOL_UNIX & pCI->flags)
      || (CIMINI_CONVERT_EOL_WINDOWS & pCI->flags))
     && blob_size(&pCI->fileContent)>0
     ){
    /* Confirm that the new content has the same EOL style as its
    ** predecessor and convert it, if needed, to the same style. Note
    ** that this inherently runs a risk of breaking content,
    ** e.g. string literals which contain embedded newlines. Note that
    ** HTML5 specifies that form-submitted TEXTAREA content gets
    ** normalized to CRLF-style:
    **
    ** https://html.spec.whatwg.org/multipage/form-elements.html#the-textarea-element
    **
    ** More performant/efficient would be to offer a flag which says
    ** which newline form to use, converting the new copy (if needed)
    ** without having to examine the original. Since the primary use
    ** case is a web interface, it would be easy to offer it as a
    ** checkbox there.
    */
    const int pseudoBinary = LOOK_LONG | LOOK_NUL;
    const int lookFlags = LOOK_CRLF | pseudoBinary;
    const int lookNew = looks_like_utf8( &pCI->fileContent, lookFlags );
    if(!(pseudoBinary & lookNew)){
      int rehash = 0;
      if(CIMINI_CONVERT_EOL_INHERIT & pCI->flags){
        Blob contentPrev = empty_blob;
        int lookOrig, nOrig;
        content_get(prevFRid, &contentPrev);
        lookOrig = looks_like_utf8(&contentPrev, lookFlags);
        nOrig = blob_size(&contentPrev);
        blob_reset(&contentPrev);
        if(nOrig>0 && lookOrig!=lookNew){
          /* If there is a newline-style mismatch, adjust the new
          ** content version to the previous style, then re-hash the
          ** content. Note that this means that what we insert is NOT
          ** what's in the filesystem.
          */
          if(!(lookOrig & LOOK_CRLF) && (lookNew & LOOK_CRLF)){
            /* Old has Unix-style, new has Windows-style. */
            blob_to_lf_only(&pCI->fileContent);
            rehash = 1;
          }else if((lookOrig & LOOK_CRLF) && !(lookNew & LOOK_CRLF)){
            /* Old has Windows-style, new has Unix-style. */
            blob_add_cr(&pCI->fileContent);
            rehash = 1;
          }
        }
      }else{
        const int oldSize = blob_size(&pCI->fileContent);
        if(CIMINI_CONVERT_EOL_UNIX & pCI->flags){
          blob_to_lf_only(&pCI->fileContent);
        }else{
          blob_add_cr(&pCI->fileContent);
          assert(CIMINI_CONVERT_EOL_WINDOWS & pCI->flags);
        }
        if(blob_size(&pCI->fileContent)!=oldSize){
          rehash = 1;
        }
      }
      if(rehash!=0){
        hname_hash(&pCI->fileContent, 0, &pCI->fileHash);
      }
    }
  }/* end EOL conversion */

  if(blob_size(&pCI->fileHash)==0){
    /* Hash the content if it's not done already... */
    hname_hash(&pCI->fileContent, 0, &pCI->fileHash);
    assert(blob_size(&pCI->fileHash)>0);
  }
  if(zFilePrev){
    /* Has this file been changed since its previous commit?  Note
    ** that we have to delay this check until after the potentially
    ** expensive EOL conversion. */
    assert(blob_size(&pCI->fileHash));
    if(0==fossil_strcmp(zFilePrev->zUuid, blob_str(&pCI->fileHash))
       && manifest_file_mperm(zFilePrev)==pCI->filePerm){
      ci_err((pErr,"File is unchanged. Not saving."));
    }
  }
#if 1
  /* Do we really want to normalize comment EOLs? Web-posting will
  ** submit them in CRLF format. */
  blob_to_lf_only(&pCI->comment);
#endif
  /* Create, save, deltify, and crosslink the manifest... */
  if(create_manifest_mini(&mf, pCI, pErr)==0){
    return 0;
  }
  isPrivate = content_is_private(pCI->pParent->rid);
  rid = content_put_ex(&mf, 0, 0, 0, isPrivate);
  if(pCI->flags & CIMINI_DUMP_MANIFEST){
    fossil_print("%b", &mf);
  }
  if(pCI->pMfOut!=0){
    /* Cross-linking clears mf, so we have to copy it,
    ** instead of taking over its memory. */
    blob_reset(pCI->pMfOut);
    blob_append(pCI->pMfOut, blob_buffer(&mf), blob_size(&mf));
  }
  content_deltify(rid, &pCI->pParent->rid, 1, 0);
  manifest_crosslink(rid, &mf, 0);
  blob_reset(&mf);
  /* Save and deltify the file content... */
  frid = content_put_ex(&pCI->fileContent, blob_str(&pCI->fileHash),
                        0, 0, isPrivate);
  if(zFilePrev!=0){
    assert(prevFRid>0);
    content_deltify(frid, &prevFRid, 1, 0);
  }
  db_end_transaction((CIMINI_DRY_RUN & pCI->flags) ? 1 : 0);
  if(pRid!=0){
    *pRid = rid;
  }
  return 1;
ci_error:
  assert(db_transaction_nesting_depth()>0);
  db_end_transaction(1);
  return 0;
#undef ci_err
}

/*
** COMMAND: test-ci-mini
**
** This is an on-going experiment, subject to change or removal at
** any time.
**
** Usage: %fossil ?OPTIONS? FILENAME
**
** where FILENAME is a repo-relative name as it would appear in the
** vfile table.
**
** Options:
**
**   --repository|-R REPO      The repository file to commit to.
**   --as FILENAME             The repository-side name of the input
**                             file, relative to the top of the
**                             repository. Default is the same as the
**                             input file name.
**   --comment|-m COMMENT      Required checkin comment.
**   --comment-file|-M FILE    Reads checkin comment from the given file.
**   --revision|-r VERSION     Commit from this version. Default is
**                             the checkout version (if available) or
**                             trunk (if used without a checkout).
**   --allow-fork              Allows the commit to be made against a
**                             non-leaf parent. Note that no autosync
**                             is performed beforehand.
**   --allow-merge-conflict    Allows checkin of a file even if it
**                             appears to contain a fossil merge conflict
**                             marker.
**   --user-override USER      USER to use instead of the current
**                             default.
**   --date-override DATETIME  DATE to use instead of 'now'.
**   --allow-older             Allow a commit to be older than its
**                             ancestor.
**   --convert-eol             Convert EOL style of the checkin to match
**                             the previous version's content. Does not
**                             modify the input file, only the checked-in
**                             content.
**   --delta                   Prefer to generate a delta manifest, if
**                             able. The forbid-delta-manifests repo
**                             config option trumps this, as do certain
**                             heuristics.
**   --allow-new-file          Allow addition of a new file this way.
**                             Disabled by default to avoid that case-
**                             sensitivity errors inadvertently lead to
**                             adding a new file where an update is
**                             intended.
**   --dump-manifest|-d        Dumps the generated manifest to stdout
**                             immediately after it's generated.
**   --save-manifest FILE      Saves the generated manifest to a file
**                             after successfully processing it.
**   --wet-run                 Disables the default dry-run mode.
**
** Example:
**
** %fossil test-ci-mini -R REPO -m ... -r foo --as src/myfile.c myfile.c
**
*/
void test_ci_mini_cmd(){
  CheckinMiniInfo cimi;       /* checkin state */
  int newRid = 0;                /* RID of new version */
  const char * zFilename;        /* argv[2] */
  const char * zComment;         /* -m comment */
  const char * zCommentFile;     /* -M FILE */
  const char * zAsFilename;      /* --as filename */
  const char * zRevision;        /* --revision|-r [=trunk|checkout] */
  const char * zUser;            /* --user-override */
  const char * zDate;            /* --date-override */
  char const * zManifestFile = 0;/* --save-manifest FILE */

  /* This function should perform only the minimal "business logic" it
  ** needs in order to fully/properly populate the CheckinMiniInfo and
  ** then pass it on to checkin_mini() to do most of the validation
  ** and work. The point of this is to avoid duplicate code when a web
  ** front-end is added for checkin_mini().
  */
  CheckinMiniInfo_init(&cimi);
  zComment = find_option("comment","m",1);
  zCommentFile = find_option("comment-file","M",1);
  zAsFilename = find_option("as",0,1);
  zRevision = find_option("revision","r",1);
  zUser = find_option("user-override",0,1);
  zDate = find_option("date-override",0,1);
  zManifestFile = find_option("save-manifest",0,1);
  if(find_option("wet-run",0,0)==0){
    cimi.flags |= CIMINI_DRY_RUN;
  }
  if(find_option("allow-fork",0,0)!=0){
    cimi.flags |= CIMINI_ALLOW_FORK;
  }
  if(find_option("dump-manifest","d",0)!=0){
    cimi.flags |= CIMINI_DUMP_MANIFEST;
  }
  if(find_option("allow-merge-conflict",0,0)!=0){
    cimi.flags |= CIMINI_ALLOW_MERGE_MARKER;
  }
  if(find_option("allow-older",0,0)!=0){
    cimi.flags |= CIMINI_ALLOW_OLDER;
  }
  if(find_option("convert-eol-prev",0,0)!=0){
    cimi.flags |= CIMINI_CONVERT_EOL_INHERIT;
  }
  if(find_option("delta",0,0)!=0){
    cimi.flags |= CIMINI_PREFER_DELTA;
  }
  if(find_option("delta2",0,0)!=0){
    /* Undocumented. For testing only. */
    cimi.flags |= CIMINI_PREFER_DELTA | CIMINI_STRONGLY_PREFER_DELTA;
  }
  if(find_option("allow-new-file",0,0)!=0){
    cimi.flags |= CIMINI_ALLOW_NEW_FILE;
  }
  db_find_and_open_repository(0, 0);
  verify_all_options();
  user_select();
  if(g.argc!=3){
    usage("INFILE");
  }
  if(zComment && zCommentFile){
    fossil_fatal("Only one of -m or -M, not both, may be used.");
  }else{
    if(zCommentFile && *zCommentFile){
      blob_read_from_file(&cimi.comment, zCommentFile, ExtFILE);
    }else if(zComment && *zComment){
      blob_append(&cimi.comment, zComment, -1);
    }
    if(!blob_size(&cimi.comment)){
      fossil_fatal("Non-empty checkin comment is required.");
    }
  }
  db_begin_transaction();
  zFilename = g.argv[2];
  cimi.zFilename = mprintf("%/", zAsFilename ? zAsFilename : zFilename);
  cimi.filePerm = file_perm(zFilename, ExtFILE);
  cimi.zUser = mprintf("%s", zUser ? zUser : login_name());
  if(zDate){
    cimi.zDate = mprintf("%s", zDate);
  }
  if(zRevision==0 || zRevision[0]==0){
    if(g.localOpen/*checkout*/){
      zRevision = db_lget("checkout-hash", 0)/*leak*/;
    }else{
      zRevision = "trunk";
    }
  }
  name_to_uuid2(zRevision, "ci", &cimi.zParentUuid);
  if(cimi.zParentUuid==0){
    fossil_fatal("Cannot determine version to commit to.");
  }
  blob_read_from_file(&cimi.fileContent, zFilename, ExtFILE);
  {
    Blob theManifest = empty_blob; /* --save-manifest target */
    Blob errMsg = empty_blob;
    int rc;
    if(zManifestFile){
      cimi.pMfOut = &theManifest;
    }
    rc = checkin_mini(&cimi, &newRid, &errMsg);
    if(rc){
      assert(blob_size(&errMsg)==0);
    }else{
      assert(blob_size(&errMsg));
      fossil_fatal("%b", &errMsg);
    }
    if(zManifestFile){
      fossil_print("Writing manifest to: %s\n", zManifestFile);
      assert(blob_size(&theManifest)>0);
      blob_write_to_file(&theManifest, zManifestFile);
      blob_reset(&theManifest);
    }
  }
  if(newRid!=0){
    fossil_print("New version%s: %z\n",
                 (cimi.flags & CIMINI_DRY_RUN) ? " (dry run)" : "",
                 rid_to_uuid(newRid));
  }
  db_end_transaction(0/*checkin_mini() will have triggered it to roll
                      ** back in dry-run mode, but we need access to
                      ** the transaction-written db state in this
                      ** routine.*/);
  if(!(cimi.flags & CIMINI_DRY_RUN) && newRid!=0 && g.localOpen!=0){
    fossil_warning("The checkout state is now out of sync "
                   "with regards to this commit. It needs to be "
                   "'update'd or 'close'd and re-'open'ed.");
  }
  CheckinMiniInfo_cleanup(&cimi);
}


/*
** Returns true if the given filename qualifies for online editing by
** the current user, else returns false.
**
** Editing requires that the user have the Write permission and that
** the filename match the glob defined by the fileedit-glob setting.
** A missing or empty value for that glob disables all editing.
*/
int fileedit_is_editable(const char *zFilename){
  static Glob * pGlobs = 0;
  static int once = 0;
  if(0==g.perm.Write || zFilename==0 || *zFilename==0
     || (once!=0 && pGlobs==0)){
    return 0;
  }else if(0==pGlobs){
    char * zGlobs = db_get("fileedit-glob",0);
    once = 1;
    if(0==zGlobs) return 0;
    pGlobs = glob_create(zGlobs);
    fossil_free(zGlobs);
    once = 1;
  }
  return glob_match(pGlobs, zFilename);
}

static void fileedit_emit_script(int phase){
  if(0==phase){
    fossil_print("<script nonce='%s'>", style_nonce());
  }else{
    fossil_print("</script>\n");
  }
}

#if 0
/*
** This function is for potential TODO features for /fileedit.
** It's been tested with that code but is not currently used
** by it.
*/
static void fileedit_emit_script_fetch(){
#define fp fossil_print
  fileedit_emit_script(0);
  fp("window.fossilFetch = function(path,opt){\n");
  fp("  if('function'===typeof opt){\n");
  fp("    opt={onload:opt};\n");
  fp("  }else{\n");
  fp("    opt=opt||{onload:function(r){console.debug('response:',r)}}\n");
  fp("  }\n");
  fp("  const url='%R/'+path, x=new XMLHttpRequest();\n");
  fp("  x.open(opt.method||'GET', url, true);\n");
  fp("  x.responseType=opt.responseType||'text';\n");
  fp("  if(opt.onload){\n");
  fp("    x.onload = function(e){\n");
  fp("      if(200!==this.status){\n");
  fp("        if(opt.onerror) opt.onerror(e);\n");
  fp("        return;\n");
  fp("      }\n");
  fp("      opt.onload(this.response);\n");
  fp("    }\n");
  fp("  }\n");
  fp("  x.send();");
  fp("};\n");
  fileedit_emit_script(1);
#undef fp
};
#endif /* fileedit_emit_script_fetch() */

/*
** Outputs a labeled checkbox element:
**
** <span class='input-with-label' title={{zTip}}>
**   <input type='checkbox' name={{zFieldName}} value={{zValue}}
**          {{isChecked ? " checked : ""}}/>
**   <span>{{zLabel}}</span>
** </span>
**
** zFieldName, zLabel, and zValue are required. zTip is optional.
*/
static void style_labeled_checkbox(const char *zFieldName,
                                   const char * zLabel,
                                   const char * zValue,
                                   const char * zTip,
                                   int isChecked){
  fossil_print("<span class='input-with-label'");
  if(zTip && *zTip){
    fossil_print(" title='%h'", zTip);
  }
  fossil_print("><input type='checkbox' name='%s' value='%T'%s/>",
               zFieldName,
               zValue ? zValue : "", isChecked ? " checked" : "");
  fossil_print("<span>%h</span></span>", zLabel);
}

/*
** WEBPAGE: fileedit
**
** EXPERIMENTAL and subject to change and removal at any time. The goal
** is to allow online edits of files.
**
** Query parameters:
**
**    file=FILE      Repo-relative path to the file.
**    r=VERSION      Checkin version
**
** Parameters intended to be passed in only via the editor's own form:
**
**    diff           If true, show diff from prev version.
**    preview        If true, preview (how depends on mimetype).
**    content        File content.
**    comment        Checkin comment.
**    n              Optional comment mimetype ("n" as in N-card).
**    
**
*/
void fileedit_page(){
  const char * zFilename = PD("file",P("name")); /* filename */
  const char * zRev = P("r");           /* checkin version */
  const char * zContent = P("content"); /* file content */
  const char * zComment = P("comment"); /* checkin comment */
  CheckinMiniInfo cimi;                 /* Checkin state */
  int submitMode = 0;                   /* See mapping below */
  char * zRevResolved = 0;              /* Resolved zRev */
  int vid, newVid = 0;                  /* checkin rid */
  char * zFileUuid = 0;                 /* File content UUID */
  int frid = 0;                         /* File content rid */
  Blob err = empty_blob;                /* Error report */
  const char * zFlagCheck = 0;          /* Temp url flag holder */
  Blob endScript = empty_blob;          /* Script code to run at the
                                           end. This content will be
                                           combined into a single JS
                                           function call, thus each
                                           entry must end with a
                                           semicolon. */
  Stmt stmt = empty_Stmt;
#define fail(EXPR) blob_appendf EXPR; goto end_footer

  login_check_credentials();
  if( !g.perm.Write ){
    login_needed(g.anon.Write);
    return;
  }
  db_begin_transaction();
  CheckinMiniInfo_init(&cimi);
  submitMode = atoi(PD("submit","0"))
    /* Submit modes: 0=initial request,
    ** 1=submit (save), 2=preview, 3=diff */;
  zFlagCheck = P("comment");
  if(zFlagCheck){
    cimi.zMimetype = mprintf("%s",zFlagCheck);
    zFlagCheck = 0;
  }
  cimi.zUser = mprintf("%s",g.zLogin);
  /*
  ** TODOs include, but are not limited to:
  **
  ** - Preview button + view
  **
  ** - Diff button + view
  **
  */
  style_header("File Editor");
  if(!zRev || !*zRev || !zFilename || !*zFilename){
    fail((&err,"Missing required URL parameters."));
  }
  if(0==fileedit_is_editable(zFilename)){
    fail((&err,"Filename <code>%h</code> is disallowed "
          "by the <code>fileedit-glob</code> repository "
          "setting.",
          zFilename));
  }
  vid = symbolic_name_to_rid(zRev, "ci");
  if(0==vid){
    fail((&err,"Could not resolve checkin version."));
  }

  /* Find the repo-side file entry or fail... */
  zRevResolved = rid_to_uuid(vid);
  db_prepare(&stmt, "SELECT uuid, perm FROM files_of_checkin "
             "WHERE filename=%Q %s AND checkinID=%d",
             zFilename, filename_collation(), vid);
  if(SQLITE_ROW==db_step(&stmt)){
    const char * zPerm = db_column_text(&stmt, 1);
    cimi.filePerm = mfile_permstr_int(zPerm);
    if(PERM_LNK==cimi.filePerm){
      fail((&err,"Editing symlinks is not permitted."));
    }
    zFileUuid = mprintf("%s",db_column_text(&stmt, 0));
  }
  db_finalize(&stmt);
  if(!zFileUuid){
    fail((&err,"Checkin [%S] does not contain file: "
          "<code>%h</code>", zRevResolved, zFilename));
  }
  frid = fast_uuid_to_rid(zFileUuid);
  assert(frid);

  /* Read file content from submit request or repo... */
  if(zContent==0){
    content_get(frid, &cimi.fileContent);
    zContent = blob_size(&cimi.fileContent)
      ? blob_str(&cimi.fileContent) : NULL;
  }else{
    blob_init(&cimi.fileContent,zContent,-1);
  }
  if(looks_like_binary(&cimi.fileContent)){
    fail((&err,"File appears to be binary. Cannot edit: "
          "<code>%h</code>",zFilename));
  }

  /* All set. Here we go... */
#define fp fossil_print
  /* ^^^ Appologies, Richard, but the @ form plays havoc with emacs */

  fp("<h1>Editing:</h1>");
  fp("<p class='hint'>");
  fp("File: <code>%h</code><br>"
     "Version: <code id='r-label'>%s</code><br>",
     zFilename, zRevResolved);
  fp("Permalink: <code>"
     "<a id='permalink' href='%R/fileedit?file=%T&r=%!S'>"
     "/fileedit?file=%T&r=%!S</a></code><br>"
     "(Clicking the permalink will reload the page and discard "
     "all edits!)",
     zFilename, zRevResolved, zFilename, zRevResolved);
  fp("</p>");
  fp("<p>This page is <em>far from complete</em>.</p>\n");
  
  fp("<form action='%R/fileedit' method='POST' "
     "class='fileedit-form'>\n");

  /******* Hidden fields *******/
  fp("<input type='hidden' name='r' value='%s'>", zRevResolved);
  fp("<input type='hidden' name='file' value='%T'>",
     zFilename);

  /******* Comment *******/
  fp("<h3>Checkin Comment</h3>\n");
  fp("<textarea name='comment' rows='3' cols='80'>");
  if(zComment && *zComment){
    fp("%h"/*%h? %s?*/, zComment);
  }
  fp("</textarea>\n");
  fp("<div class='hint'>Comments use the Fossil wiki markup "
     "syntax.</div>"/*TODO: radiobuttons for fossil/me/plain text*/);

  /******* Content *******/
  fp("<h3>File Content</h3>\n");
  fp("<textarea name='content' id='fileedit-content' "
     "rows='20' cols='80'>");
  fp("Loading...");
  fp("</textarea>\n");
  /******* Flags/options *******/
  fp("<fieldset class='fileedit-options'>"
     "<legend>Options</legend><div>"
     /* Chrome does not sanely lay out multiple
     ** fieldset children after the <legend>, so
     ** a containing div is necessary. */);
  /*
  ** TODO?: date-override date selection field. Maybe use
  ** an input[type=datetime-local].
  */
  if(0==submitMode || P("dry_run")!=0){
    cimi.flags |= CIMINI_DRY_RUN;
  }
  style_labeled_checkbox("dry_run", "Dry-run?", "1",
                         "In dry-run mode, do not really save.",
                         cimi.flags & CIMINI_DRY_RUN);
  if(P("allow_fork")!=0){
    cimi.flags |= CIMINI_ALLOW_FORK;
  }
  style_labeled_checkbox("allow_fork", "Allow fork?", "1",
                         "Allow saving to create a fork?",
                         cimi.flags & CIMINI_ALLOW_FORK);
  if(P("allow_older")!=0){
    cimi.flags |= CIMINI_ALLOW_OLDER;
  }
  style_labeled_checkbox("allow_older", "Allow older?", "1",
                         "Allow saving to a version which has "
                         "a newer timestamp?",
                         cimi.flags & CIMINI_ALLOW_OLDER);
  if(P("exec_bit")!=0){
    cimi.filePerm = PERM_EXE;
  }
  style_labeled_checkbox("exec_bit", "Executable?", "1",
                         "Set executable bit?",
                         PERM_EXE==cimi.filePerm);
  if(P("allow_merge_conflict")!=0){
    cimi.flags |= CIMINI_ALLOW_MERGE_MARKER;
  }
  style_labeled_checkbox("allow_merge_conflict",
                         "Allow merge conflict markers?", "1",
                         "Allow saving even if the content contains "
                         "what appear to be fossil merge conflict "
                         "markers?",
                         cimi.flags & CIMINI_ALLOW_MERGE_MARKER);
  {/* EOL conversion policy... */
    const int eolMode = submitMode==0 ? 0 : atoi(PD("eol","0"));
    switch(eolMode){
      case 1: cimi.flags |= CIMINI_CONVERT_EOL_UNIX; break;
      case 2: cimi.flags |= CIMINI_CONVERT_EOL_WINDOWS; break;
      default: cimi.flags |= CIMINI_CONVERT_EOL_INHERIT; break;
    }
    fp("<select name='eol' "
       "title='EOL conversion policy, noting that form-processing "
       "may implicitly change the line endings of the input.'>");
    fp("<option value='0'%s>Inherit EOLs</option>",
       (eolMode!=1 && eolMode!=2) ? " selected" : "");
    fp("<option value='1'%s/>Unix EOLs</option>",
       eolMode==1 ? " selected" : "");
    fp("<option value='2'%s>Windows EOLs</option>",
       eolMode==2 ? " selected" : "");
    fp("</select>");
  }
  if(P("prefer_delta")!=0){
    cimi.flags |= CIMINI_PREFER_DELTA;
  }
  style_labeled_checkbox("prefer_delta",
                         "Prefer delta manifest?", "1",
                         "Will create a delta manifest, instead of "
                         "baseline, if conditions are favorable to do "
                         "so. This option is only a suggestion.",
                         cimi.flags & CIMINI_PREFER_DELTA);

  fp("</div></fieldset>") /* end of checkboxes */;

  /******* Buttons *******/  
  fp("<fieldset class='fileedit-options'>"
     "<legend>Several buttons are TODO</legend><div>");
  fp("<button type='submit' name='submit' value='1'>"
     "Submit</button>");
  fp("<button type='submit' name='submit' value='2'>"
     "Preview (TODO)</button>");
  fp("<button type='submit' name='submit' value='3'>"
     "Diff (TODO)</button>");
  fp("</div></fieldset>");

  /******* End of form *******/    
  fp("</form>\n");

  {
    /* Populate the editor...
    **
    ** To avoid all escaping-related issues, we have to do this one of
    ** two ways:
    **
    ** 1) Fetch the content via AJAX. That only works if the content
    **    is already in the db, but not for edited versions.
    **
    ** 2) Store the content as JSON and feed it into the textarea
    **    using JavaScript.
    */
    char const * zQuoted = 0;
    if(blob_size(&cimi.fileContent)>0){
      db_prepare(&stmt, "SELECT json_quote(%B)", &cimi.fileContent);
      db_step(&stmt);
      zQuoted = db_column_text(&stmt,0);
    }
    blob_appendf(&endScript,
                 "/* populate editor form */\n"
                 "document.getElementById('fileedit-content')"
                 ".value=%s;", zQuoted ? zQuoted : "'';\n");
    if(stmt.pStmt){
      db_finalize(&stmt);
    }
  }

  if(1==submitMode/*save*/){
    Blob manifest = empty_blob;
    char * zNewUuid = 0;
    /*cimi.flags |= CIMINI_STRONGLY_PREFER_DELTA;*/
    if(zComment && *zComment){
      blob_append(&cimi.comment, zComment, -1);
    }else{
      fail((&err,"Empty comment is not permitted."));
    }
    cimi.pParent = manifest_get(vid, CFTYPE_MANIFEST, 0);
    assert(cimi.pParent && "We know vid is valid.");
    cimi.zFilename = mprintf("%s",zFilename);
    cimi.pMfOut = &manifest;
    checkin_mini(&cimi, &newVid, &err);
    if(newVid!=0){
      zNewUuid = rid_to_uuid(newVid);
      fp("<h3>Manifest%s: %S</h3><pre>"
         "<code class='fileedit-manifest'>%h</code>"
         "</pre>",
         (cimi.flags & CIMINI_DRY_RUN) ? " (dry run)" : "",
         zNewUuid, blob_str(&manifest));
      if(!(CIMINI_DRY_RUN & cimi.flags)){
        /* We need to update certain form fields and UI elements so
        ** they're not left pointing to the previous version. While
        ** we're at it, we'll re-enable dry-run mode for sanity's
        ** sake.
        */
        blob_appendf(&endScript,
                     "/* Toggle dry-run back on */\n"
                     "document.querySelector('input[type=checkbox]"
                     "[name=dry_run]').checked=true;\n");
        blob_appendf(&endScript,
                     "/* Update version number */\n"
                     "document.querySelector('input[name=r]')"
                     ".value=%Q;\n"
                     "document.querySelector('#r-label')"
                     ".innerText=%Q;\n",
                     zNewUuid, zNewUuid);
        blob_appendf(&endScript,
                     "/* Update permalink */\n"
                     "const urlFull='%R/fileedit?file=%T&r=%!S';\n"
                     "const urlShort='/fileedit?file=%T&r=%!S';\n"
                     "let link=document.querySelector('#permalink');\n"
                     "link.innerText=urlShort;\n"
                     "link.setAttribute('href',urlFull);\n",
                     zFilename, zNewUuid, zFilename, zNewUuid);
      }
      fossil_free(zNewUuid);
    }
    /* On error, the error message is in the err blob and will
    ** be emitted below. */
    blob_reset(&manifest);
  }else if(2==submitMode/*preview*/){
    /* TODO */
    fail((&err,"Preview mode is still TODO."));
  }else if(3==submitMode/*diff*/){
    fail((&err,"Diff mode is still TODO."));
  }else{
    /* Ignore invalid submitMode value */
    goto end_footer;
  }

end_footer:
  zContent = 0;
  fossil_free(zRevResolved);
  fossil_free(zFileUuid);
  if(stmt.pStmt){
    db_finalize(&stmt);
  }
  if(blob_size(&err)){
      fp("<div class='fileedit-error-report'>%s</div>",
         blob_str(&err));
  }
  blob_reset(&err);
  CheckinMiniInfo_cleanup(&cimi);
  if(blob_size(&endScript)>0){
    fileedit_emit_script(0);
    fp("(function(){\n");
    fp("try{\n%b\n}catch(e){console.error('Exception:',e)}\n",
       &endScript);
    fp("})();");
    fileedit_emit_script(1);
  }
  db_end_transaction(0/*noting that dry-run mode will have already
                      ** set this to rollback mode. */);
  style_footer();
#undef fp
}
