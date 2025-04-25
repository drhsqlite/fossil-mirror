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
** This file contains code used to check-out versions of the project
** from the local repository.
*/
#include "config.h"
#include "add.h"
#include <assert.h>
#include <dirent.h>
#include "cygsup.h"

/*
** This routine returns the names of files in a working check-out that
** are created by Fossil itself, and hence should not be added, deleted,
** or merge, and should be omitted from "clean" and "extras" lists.
**
** Return the N-th name.  The first name has N==0.  When all names have
** been used, return 0.
*/
const char *fossil_reserved_name(int N, int omitRepo){
  /* Possible names of the local per-check-out database file and
  ** its associated journals
  */
  static const char *const azName[] = {
     "_FOSSIL_",
     "_FOSSIL_-journal",
     "_FOSSIL_-wal",
     "_FOSSIL_-shm",
     ".fslckout",
     ".fslckout-journal",
     ".fslckout-wal",
     ".fslckout-shm",

     /* The use of ".fos" as the name of the check-out database is
     ** deprecated.  Use ".fslckout" instead.  At some point, the following
     ** entries should be removed.  2012-02-04 */
     ".fos",
     ".fos-journal",
     ".fos-wal",
     ".fos-shm",
  };

  /* Possible names of auxiliary files generated when the "manifest" property
  ** is used
  */
  static const struct {
    const char *fname;
    int flg;
  }aManifestflags[] = {
    { "manifest",      MFESTFLG_RAW },
    { "manifest.uuid", MFESTFLG_UUID },
    { "manifest.tags", MFESTFLG_TAGS }
  };
  static const char *azManifests[3];

  /*
  ** Names of repository files, if they exist in the check-out.
  */
  static const char *azRepo[4] = { 0, 0, 0, 0 };

  /* Cached setting "manifest" */
  static int cachedManifest = -1;
  static int numManifests;

  if( cachedManifest == -1 ){
    int i;
    Blob repo;
    cachedManifest = db_get_manifest_setting(0);
    numManifests = 0;
    for(i=0; i<count(aManifestflags); i++){
      if( cachedManifest&aManifestflags[i].flg ) {
        azManifests[numManifests++] = aManifestflags[i].fname;
      }
    }
    blob_zero(&repo);
    if( file_tree_name(g.zRepositoryName, &repo, 0, 0) ){
      const char *zRepo = blob_str(&repo);
      azRepo[0] = zRepo;
      azRepo[1] = mprintf("%s-journal", zRepo);
      azRepo[2] = mprintf("%s-wal", zRepo);
      azRepo[3] = mprintf("%s-shm", zRepo);
    }
  }

  if( N<0 ) return 0;
  if( N<count(azName) ) return azName[N];
  N -= count(azName);
  if( cachedManifest ){
    if( N<numManifests ) return azManifests[N];
    N -= numManifests;
  }
  if( !omitRepo && N<count(azRepo) ) return azRepo[N];
  return 0;
}

/*
** Return a list of all reserved filenames as an SQL list.
*/
const char *fossil_all_reserved_names(int omitRepo){
  static char *zAll = 0;
  if( zAll==0 ){
    Blob x;
    int i;
    const char *z;
    blob_zero(&x);
    for(i=0; (z = fossil_reserved_name(i, omitRepo))!=0; i++){
      if( i>0 ) blob_append(&x, ",", 1);
      blob_appendf(&x, "'%q'", z);
    }
    zAll = blob_str(&x);
  }
  return zAll;
}

/*
** COMMAND: test-reserved-names
**
** Usage: %fossil test-reserved-names [-omitrepo]
**
** Show all reserved filenames for the current check-out.
*/
void test_reserved_names(void){
  int i;
  const char *z;
  int omitRepo = find_option("omitrepo",0,0)!=0;

  /* We should be done with options.. */
  verify_all_options();

  db_must_be_within_tree();
  for(i=0; (z = fossil_reserved_name(i, omitRepo))!=0; i++){
    fossil_print("%3d: %s\n", i, z);
  }
  fossil_print("ALL: (%s)\n", fossil_all_reserved_names(omitRepo));
}

/*
** Add a single file named zName to the VFILE table with vid.
**
** Omit any file whose name is pOmit.
*/
static int add_one_file(
  const char *zPath,   /* Tree-name of file to add. */
  int vid              /* Add to this VFILE */
){
  int doSkip = 0;
  if( !file_is_simple_pathname(zPath, 1) ){
    fossil_warning("filename contains illegal characters: %s", zPath);
    return 0;
  }
  if( db_exists("SELECT 1 FROM vfile"
                " WHERE pathname=%Q %s", zPath, filename_collation()) ){
    db_multi_exec("UPDATE vfile SET deleted=0"
                  " WHERE pathname=%Q %s AND deleted",
                  zPath, filename_collation());
  }else{
    char *zFullname = mprintf("%s%s", g.zLocalRoot, zPath);
    int isExe = file_isexe(zFullname, RepoFILE);
    int isLink = file_islink(0);
    if( file_nondir_objects_on_path(g.zLocalRoot, zFullname) ){
      /* Do not add unsafe files to the vfile */
      doSkip = 1;
    }else{
      db_multi_exec(
        "INSERT INTO vfile(vid,deleted,rid,mrid,pathname,isexe,islink,mhash)"
        "VALUES(%d,0,0,0,%Q,%d,%d,NULL)",
        vid, zPath, isExe, isLink);
    }
    fossil_free(zFullname);
  }
  if( db_changes() && !doSkip ){
    fossil_print("ADDED  %s\n", zPath);
    return 1;
  }else{
    fossil_print("SKIP   %s\n", zPath);
    return 0;
  }
}

/*
** Add all files in the sfile temp table.
**
** Automatically exclude the repository file and any other files
** with reserved names. Also exclude files that are beneath an
** existing symlink.
*/
static int add_files_in_sfile(int vid){
  const char *zRepo;        /* Name of the repository database file */
  int nAdd = 0;             /* Number of files added */
  int i;                    /* Loop counter */
  const char *zReserved;    /* Name of a reserved file */
  Blob repoName;            /* Treename of the repository */
  Stmt loop;                /* SQL to loop over all files to add */
  int (*xCmp)(const char*,const char*);

  if( !file_tree_name(g.zRepositoryName, &repoName, 0, 0) ){
    blob_zero(&repoName);
    zRepo = "";
  }else{
    zRepo = blob_str(&repoName);
  }
  if( filenames_are_case_sensitive() ){
    xCmp = fossil_strcmp;
  }else{
    xCmp = fossil_stricmp;
  }
  db_prepare(&loop,
     "SELECT pathname FROM sfile"
     " WHERE pathname NOT IN ("
       "SELECT sfile.pathname FROM vfile, sfile"
       " WHERE vfile.islink"
       "   AND NOT vfile.deleted"
       "   AND sfile.pathname>(vfile.pathname||'/')"
       "   AND sfile.pathname<(vfile.pathname||'0'))"
     " ORDER BY pathname");
  while( db_step(&loop)==SQLITE_ROW ){
    const char *zToAdd = db_column_text(&loop, 0);
    if( fossil_strcmp(zToAdd, zRepo)==0 ) continue;
    if( strchr(zToAdd,'/') ){
      if( file_is_reserved_name(zToAdd, -1) ) continue;
    }else{
      for(i=0; (zReserved = fossil_reserved_name(i, 0))!=0; i++){
        if( xCmp(zToAdd, zReserved)==0 ) break;
      }
      if( zReserved ) continue;
    }
    nAdd += add_one_file(zToAdd, vid);
  }
  db_finalize(&loop);
  blob_reset(&repoName);
  return nAdd;
}

/*
** Resets the ADDED/DELETED state of a check-out, such that all
** newly-added (but not yet committed) files are no longer added and
** newly-removed (but not yet committed) files are no longer
** removed. If bIsAdd is true, it operates on the "add" state, else it
** operates on the "rm" state.
**
** If bDryRun is true it outputs what it would have done, but does not
** actually do it. In this case it rolls back the transaction it
** starts (so don't start a transaction before calling this).
**
** If bVerbose is true it outputs the name of each reset entry.
**
** This is intended to be called only in the context of the
** add/rm/addremove commands, after a call to verify_all_options().
**
** Un-added files are not modified but any un-rm'd files which are
** missing from the check-out are restored from the repo. un-rm'd files
** which exist in the check-out are left as-is, rather than restoring
** them using vfile_to_disk(), to avoid overwriting any local changes
** made to those files.
*/
static void addremove_reset(int bIsAdd, int bDryRun, int bVerbose){
  int nReset = 0; /* # of entries which get reset */
  Stmt stmt;      /* vfile loop query */

  db_begin_transaction();
  db_prepare(&stmt, "SELECT id, pathname FROM vfile "
                    "WHERE %s ORDER BY pathname",
                    bIsAdd==0 ? "deleted<>0" : "rid=0"/*safe-for-%s*/);
  while( db_step(&stmt)==SQLITE_ROW ){
    /* This loop exists only so we can restore the contents of un-rm'd
    ** files and support verbose mode. All manipulation of vfile's
    ** contents happens after the loop. For the ADD case in non-verbose
    ** mode we "could" skip this loop entirely.
    */
    int const id = db_column_int(&stmt, 0);
    char const * zPathname = db_column_text(&stmt, 1);
    Blob relName = empty_blob;
    if(bIsAdd==0 || bVerbose!=0){
      /* Make filename relative... */
      char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
      file_relative_name(zFullName, &relName, 0);
      fossil_free(zFullName);
    }
    if(bIsAdd==0){
      /* Restore contents of missing un-rm'd files. We don't do this
      ** unconditionally because we might cause data loss if a file
      ** is modified, rm'd, then un-rm'd.
      */
      ++nReset;
      if(!file_isfile_or_link(blob_str(&relName))){
        if(bDryRun==0){
          vfile_to_disk(0, id, 0, 0);
          if(bVerbose){
            fossil_print("Restored missing file: %b\n", &relName);
          }
        }else{
          fossil_print("Dry-run: not restoring missing file: %b\n", &relName);
        }
      }
      if(bVerbose){
        fossil_print("Un-removed: %b\n", &relName);
      }
    }else{
      /* un-add... */
      ++nReset;
      if(bVerbose){
        fossil_print("Un-added: %b\n", &relName);
      }
    }
    blob_reset(&relName);
  }
  db_finalize(&stmt);
  if(nReset>0){
    if(bIsAdd==0){
      if(bDryRun==0){
        db_exec_sql("UPDATE vfile SET deleted=0 WHERE deleted<>0");
      }
      fossil_print("Un-removed %d file(s).\n", nReset);
    }else{
      if(bDryRun==0){
        db_exec_sql("DELETE FROM vfile WHERE rid=0");
      }
      fossil_print("Un-added %d file(s).\n", nReset);
    }
  }
  db_end_transaction(bDryRun ? 1 : 0);
}


/*
** COMMAND: add
**
** Usage: %fossil add ?OPTIONS? FILE1 ?FILE2 ...?
**
** Make arrangements to add one or more files or directories to the
** current check-out at the next [[commit]].
**
** When adding files or directories recursively, filenames that begin
** with "." are excluded by default.  To include such files, add
** the "--dotfiles" option to the command-line.
**
** The --ignore and --clean options are comma-separated lists of glob patterns
** for files to be excluded.  Example:  '*.o,*.obj,*.exe'  If the --ignore
** option does not appear on the command line then the "ignore-glob" setting
** is used.  If the --clean option does not appear on the command line then
** the "clean-glob" setting is used.
**
** When attempting to explicitly add files on the commandline, and if those
** match "ignore-glob", a confirmation is asked first. This can be prevented
** using the -f|--force option.
**
** The --case-sensitive option determines whether or not filenames should
** be treated case sensitive or not. If the option is not given, the default
** depends on the global setting, or the operating system default, if not set.
**
** Options:
**    --case-sensitive BOOL   Override the case-sensitive setting
**    --dotfiles              Include files beginning with a dot (".")
**    -f|--force              Add files without prompting
**    --ignore CSG            Ignore unmanaged files matching patterns from
**                            the Comma Separated Glob (CSG) pattern list
**    --clean CSG             Also ignore files matching patterns from
**                            the Comma Separated Glob (CSG) list
**    --reset                 Reset the ADDED state of a check-out, such
**                            that all newly-added (but not yet committed)
**                            files are no longer added. No flags other
**                            than --verbose and --dry-run may be used
**                            with --reset.
**    --allow-reserved        Permit filenames which are reserved on
**                            Windows platforms. Such files cannot be
**                            checked out on Windows, so use with care.
**
** The following options are only valid with --reset:
**    -v|--verbose            Output information about each --reset file
**    -n|--dry-run            Display instead of run actions
**
** See also: [[addremove]], [[rm]]
*/
void add_cmd(void){
  int i;                     /* Loop counter */
  int vid;                   /* Currently checked-out version */
  int nRoot;                 /* Full path characters in g.zLocalRoot */
  const char *zCleanFlag;    /* The --clean option or clean-glob setting */
  const char *zIgnoreFlag;   /* The --ignore option or ignore-glob setting */
  Glob *pIgnore, *pClean;    /* Ignore everything matching the glob patterns */
  unsigned scanFlags = 0;    /* Flags passed to vfile_scan() */
  int forceFlag;
  int allowReservedFlag = 0; /* --allow-reserved flag */

  if(0!=find_option("reset",0,0)){
    int const verboseFlag = find_option("verbose","v",0)!=0;
    int const dryRunFlag = find_option("dry-run","n",0)!=0;
    db_must_be_within_tree();
    verify_all_options();
    addremove_reset(1, dryRunFlag, verboseFlag);
    return;
  }

  zCleanFlag = find_option("clean",0,1);
  zIgnoreFlag = find_option("ignore",0,1);
  forceFlag = find_option("force","f",0)!=0;
  if( find_option("dotfiles",0,0)!=0 ) scanFlags |= SCAN_ALL;
  allowReservedFlag = find_option("allow-reserved",0,0)!=0;

  /* We should be done with options.. */
  verify_all_options();

  db_must_be_within_tree();
  if( zCleanFlag==0 ){
    zCleanFlag = db_get("clean-glob", 0);
  }
  if( zIgnoreFlag==0 ){
    zIgnoreFlag = db_get("ignore-glob", 0);
  }
  if( db_get_boolean("dotfiles", 0) ) scanFlags |= SCAN_ALL;
  vid = db_lget_int("checkout",0);
  db_begin_transaction();
  db_multi_exec("CREATE TEMP TABLE sfile(pathname TEXT PRIMARY KEY %s)",
                filename_collation());
  pClean = glob_create(zCleanFlag);
  pIgnore = glob_create(zIgnoreFlag);
  nRoot = strlen(g.zLocalRoot);

  /* Load the names of all files that are to be added into sfile temp table */
  for(i=2; i<g.argc; i++){
    char *zName;
    int isDir;
    Blob fullName = empty_blob;

    /* file_tree_name() throws a fatal error if g.argv[i] is outside of the
    ** check-out. */
    file_tree_name(g.argv[i], &fullName, 0, 1);
    blob_reset(&fullName);
    file_canonical_name(g.argv[i], &fullName, 0);
    zName = blob_str(&fullName);
    isDir = file_isdir(zName, RepoFILE);
    if( isDir==1 ){
      vfile_scan(&fullName, nRoot-1, scanFlags, pClean, pIgnore, RepoFILE);
    }else if( isDir==0 ){
      fossil_warning("not found: %s", zName);
    }else{
      char *zTreeName = file_case_preferred_name(g.zLocalRoot,&zName[nRoot]);
      if( !forceFlag && glob_match(pIgnore, zTreeName) ){
        Blob ans;
        char cReply;
        char *prompt = mprintf("file \"%s\" matches \"ignore-glob\".  "
                               "Add it (a=all/y/N)? ", zTreeName);
        prompt_user(prompt, &ans);
        fossil_free(prompt);
        cReply = blob_str(&ans)[0];
        blob_reset(&ans);
        if( cReply=='a' || cReply=='A' ){
          forceFlag = 1;
        }else if( cReply!='y' && cReply!='Y' ){
          blob_reset(&fullName);
          continue;
        }
      }
      db_multi_exec(
         "INSERT OR IGNORE INTO sfile(pathname) VALUES(%Q)",
         zTreeName
      );
      fossil_free(zTreeName);
    }
    blob_reset(&fullName);
  }
  glob_free(pIgnore);
  glob_free(pClean);

  /** Check for Windows-reserved names and warn or exit, as
   ** appopriate. Note that the 'add' internal machinery already
   ** _silently_ skips over any names for which
   ** file_is_reserved_name() returns true or which is in the
   ** fossil_reserved_name() list. We do not need to warn for those,
   ** as they're outright verboten. */
  if(db_exists("SELECT 1 FROM sfile WHERE win_reserved(pathname)")){
    int reservedCount = 0;
    Stmt q = empty_Stmt;
    db_prepare(&q,"SELECT pathname FROM sfile "
                  "WHERE win_reserved(pathname)");
    while( db_step(&q)==SQLITE_ROW ){
      const char * zName = db_column_text(&q, 0);
      ++reservedCount;
      if(allowReservedFlag){
        fossil_warning("WARNING: Windows-reserved "
                       "filename: %s", zName);
      }else{
        fossil_warning("ERROR: Windows-reserved filename: %s", zName);
      }
    }
    db_finalize(&q);
    if(allowReservedFlag==0){
      fossil_fatal("ERROR: %d Windows-reserved filename(s) added. "
                   "Use --allow-reserved to permit such names.",
                   reservedCount);
    }
  }
  add_files_in_sfile(vid);
  db_end_transaction(0);
}

/*
** This function adds a file to list of files to delete from disk after
** the other actions required for the parent operation have completed
** successfully.  The first time it is called for the current process,
** it creates a temporary table named "fremove", to keep track of these
** files.
*/
static void add_file_to_remove(
  const char *zOldName /* The old name of the file on disk. */
){
  static int tableCreated = 0;
  Blob fullOldName;
  if( !tableCreated ){
    db_multi_exec("CREATE TEMP TABLE fremove(x TEXT PRIMARY KEY %s)",
                  filename_collation());
    tableCreated = 1;
  }
  file_tree_name(zOldName, &fullOldName, 1, 1);
  db_multi_exec("INSERT INTO fremove VALUES('%q');", blob_str(&fullOldName));
  blob_reset(&fullOldName);
}

/*
** This function deletes files from the check-out, using the file names
** contained in the temporary table "fremove".  The temporary table is
** created on demand by the add_file_to_remove() function.
**
** If dryRunFlag is non-zero, no files will be removed; however, their
** names will still be output.
**
** The temporary table "fremove" is dropped after being processed.
*/
static void process_files_to_remove(
  int dryRunFlag /* Zero to actually operate on the file-system. */
){
  Stmt remove;
  if( db_table_exists("temp", "fremove") ){
    db_prepare(&remove, "SELECT x FROM fremove ORDER BY x;");
    while( db_step(&remove)==SQLITE_ROW ){
      const char *zOldName = db_column_text(&remove, 0);
      if( !dryRunFlag ){
        file_delete(zOldName);
      }
      fossil_print("DELETED_FILE %s\n", zOldName);
    }
    db_finalize(&remove);
    db_multi_exec("DROP TABLE fremove;");
  }
}

/*
** COMMAND: rm
** COMMAND: delete
** COMMAND: forget*
**
** Usage: %fossil rm|delete|forget FILE1 ?FILE2 ...?
**
** Remove one or more files or directories from the repository.
**
** The 'rm' and 'delete' commands do NOT normally remove the files from
** disk.  They just mark the files as no longer being part of the project.
** In other words, future changes to the named files will not be versioned.
** However, the default behavior of this command may be overridden via the
** command line options listed below and/or the 'mv-rm-files' setting.
**
** The 'forget' command never removes files from disk, even when the command
** line options and/or the 'mv-rm-files' setting would otherwise require it
** to do so.
**
** WARNING: If the "--hard" option is specified -OR- the "mv-rm-files"
**          setting is non-zero, files WILL BE removed from disk as well.
**          This does NOT apply to the 'forget' command.
**
** Options:
**   --soft                  Skip removing files from the check-out.
**                           This supersedes the --hard option.
**   --hard                  Remove files from the check-out
**   --case-sensitive BOOL   Override the case-sensitive setting
**   -n|--dry-run            If given, display instead of run actions.
**   --reset                 Reset the DELETED state of a check-out, such
**                           that all newly-rm'd (but not yet committed)
**                           files are no longer removed. No flags other
**                           than --verbose or --dry-run may be used with
**                           --reset.
**   -v|--verbose            Outputs information about each --reset file.
**                           Only usable with --reset.
**
** See also: [[addremove]], [[add]]
*/
void delete_cmd(void){
  int i;
  int removeFiles;
  int dryRunFlag = find_option("dry-run","n",0)!=0;
  int softFlag;
  int hardFlag;
  Stmt loop;

  if(0!=find_option("reset",0,0)){
    int const verboseFlag = find_option("verbose","v",0)!=0;
    db_must_be_within_tree();
    verify_all_options();
    addremove_reset(0, dryRunFlag, verboseFlag);
    return;
  }

  softFlag = find_option("soft",0,0)!=0;
  hardFlag = find_option("hard",0,0)!=0;

  /* We should be done with options.. */
  verify_all_options();

  db_must_be_within_tree();
  db_begin_transaction();
  if( g.argv[1][0]=='f' ){ /* i.e. "forget" */
    removeFiles = 0;
  }else if( softFlag ){
    removeFiles = 0;
  }else if( hardFlag ){
    removeFiles = 1;
  }else{
    removeFiles = db_get_boolean("mv-rm-files",0);
  }
  db_multi_exec("CREATE TEMP TABLE sfile(pathname TEXT PRIMARY KEY %s)",
                filename_collation());
  for(i=2; i<g.argc; i++){
    Blob treeName;
    char *zTreeName;

    file_tree_name(g.argv[i], &treeName, 0, 1);
    zTreeName = blob_str(&treeName);
    db_multi_exec(
       "INSERT OR IGNORE INTO sfile"
       " SELECT pathname FROM vfile"
       "  WHERE (pathname=%Q %s"
       "     OR (pathname>'%q/' %s AND pathname<'%q0' %s))"
       "    AND NOT deleted",
       zTreeName, filename_collation(), zTreeName,
       filename_collation(), zTreeName, filename_collation()
    );
    blob_reset(&treeName);
  }

  db_prepare(&loop, "SELECT pathname FROM sfile");
  while( db_step(&loop)==SQLITE_ROW ){
    fossil_print("DELETED %s\n", db_column_text(&loop, 0));
    if( removeFiles ) add_file_to_remove(db_column_text(&loop, 0));
  }
  db_finalize(&loop);
  if( !dryRunFlag ){
    db_multi_exec(
      "UPDATE vfile SET deleted=1 WHERE pathname IN sfile;"
      "DELETE FROM vfile WHERE rid=0 AND deleted;"
    );
  }
  db_end_transaction(0);
  if( removeFiles ) process_files_to_remove(dryRunFlag);
}

/*
** Capture the command-line --case-sensitive option.
*/
static const char *zCaseSensitive = 0;
void capture_case_sensitive_option(void){
  if( zCaseSensitive==0 ){
    zCaseSensitive = find_option("case-sensitive",0,1);
  }
}

/*
** This routine determines if files should be case-sensitive or not.
** In other words, this routine determines if two filenames that
** differ only in case should be considered the same name or not.
**
** The case-sensitive setting determines the default value.  If
** the case-sensitive setting is undefined, then case sensitivity
** defaults off for Cygwin, Mac and Windows and on for all other unix.
** If case-sensitivity is enabled in the windows kernel, the Cygwin port
** of fossil.exe can detect that, and modifies the default to 'on'.
**
** The "--case-sensitive BOOL" command-line option overrides any
** setting.
*/
int filenames_are_case_sensitive(void){
  static int caseSensitive;
  static int once = 1;

  if( once ){
    once = 0;
    if( zCaseSensitive ){
      caseSensitive = is_truth(zCaseSensitive);
    }else{
#if defined(_WIN32) || defined(__DARWIN__) || defined(__APPLE__)
      caseSensitive = 0;  /* Mac and Windows */
#elif defined(__CYGWIN__)
      /* Cygwin can be configured to be case-sensitive, check this. */
      void *hKey;
      int value = 1, length = sizeof(int);
      caseSensitive = 0;  /* Cygwin default */
      if( (RegOpenKeyExW((void *)0x80000002, L"SYSTEM\\CurrentControlSet\\"
          "Control\\Session Manager\\kernel", 0, 1, (void *)&hKey)
          == 0) && (RegQueryValueExW(hKey, L"obcaseinsensitive",
          0, NULL, (void *)&value, (void *)&length) == 0) && !value ){
        caseSensitive = 1;
      }
#else
      caseSensitive = 1;  /* Unix */
#endif
      caseSensitive = db_get_boolean("case-sensitive",caseSensitive);
    }
    if( !caseSensitive && g.localOpen ){
      db_multi_exec(
         "CREATE INDEX IF NOT EXISTS localdb.vfile_nocase"
         "  ON vfile(pathname COLLATE nocase)"
      );
    }
  }
  return caseSensitive;
}

/*
** Return one of two things:
**
**   ""                 (empty string) if filenames are case sensitive
**
**   "COLLATE nocase"   if filenames are not case sensitive.
*/
const char *filename_collation(void){
  return filenames_are_case_sensitive() ? "" : "COLLATE nocase";
}

/*
** COMMAND: addremove
**
** Usage: %fossil addremove ?OPTIONS?
**
** Do all necessary "[[add]]" and "[[rm]]" commands to synchronize the
** repository with the content of the working check-out:
**
**  *  All files in the check-out but not in the repository (that is,
**     all files displayed using the "extras" command) are added as
**     if by the "[[add]]" command.
**
**  *  All files in the repository but missing from the check-out (that is,
**     all files that show as MISSING with the "status" command) are
**     removed as if by the "[[rm]]" command.
**
** Note that this command does not "commit", as that is a separate step.
**
** Files and directories whose names begin with "." are ignored unless
** the --dotfiles option is used.
**
** The --ignore option overrides the "ignore-glob" setting, as do the
** --case-sensitive option with the "case-sensitive" setting and the
** --clean option with the "clean-glob" setting. See the documentation
** on the "settings" command for further information.
**
** The -n|--dry-run option shows what would happen without actually doing
** anything.
**
** This command can be used to track third party software.
**
** Options:
**   --case-sensitive BOOL   Override the case-sensitive setting
**   --dotfiles              Include files beginning with a dot (".")
**   --ignore CSG            Ignore unmanaged files matching patterns from
**                           the Comma Separated Glob (CSG) list
**   --clean CSG             Also ignore files matching patterns from
**                           the Comma Separated Glob (CSG) list
**   -n|--dry-run            If given, display instead of run actions
**   --reset                 Reset the ADDED/DELETED state of a check-out,
**                           such that all newly-added (but not yet committed)
**                           files are no longer added and all newly-removed
**                           (but not yet committed) files are no longer
**                           removed. No flags other than --verbose and
**                           --dry-run may be used with --reset.
**   -v|--verbose            Outputs information about each --reset file.
**                           Only usable with --reset.
**
** See also: [[add]], [[rm]]
*/
void addremove_cmd(void){
  Blob path;
  const char *zCleanFlag;
  const char *zIgnoreFlag;
  unsigned scanFlags;
  int dryRunFlag = find_option("dry-run","n",0)!=0;
  int n;
  Stmt q;
  int vid;
  int nAdd = 0;
  int nDelete = 0;
  Glob *pIgnore, *pClean;

  if( !dryRunFlag ){
    dryRunFlag = find_option("test",0,0)!=0; /* deprecated */
  }

  if(0!=find_option("reset",0,0)){
    int const verboseFlag = find_option("verbose","v",0)!=0;
    db_must_be_within_tree();
    verify_all_options();
    addremove_reset(0, dryRunFlag, verboseFlag);
    addremove_reset(1, dryRunFlag, verboseFlag);
    return;
  }

  zCleanFlag = find_option("clean",0,1);
  zIgnoreFlag = find_option("ignore",0,1);
  scanFlags = find_option("dotfiles",0,0)!=0 ? SCAN_ALL : 0;

  /* We should be done with options.. */
  verify_all_options();

  /* Fail if unprocessed arguments are present, in case user expect the
  ** addremove command to accept a list of file or directory.
  */
  if( g.argc>2 ){
    fossil_fatal(
        "%s: Can only work on the entire check-out, no arguments supported.",
        g.argv[1]);
  }
  db_must_be_within_tree();
  if( zCleanFlag==0 ){
    zCleanFlag = db_get("clean-glob", 0);
  }
  if( zIgnoreFlag==0 ){
    zIgnoreFlag = db_get("ignore-glob", 0);
  }
  if( db_get_boolean("dotfiles", 0) ) scanFlags |= SCAN_ALL;
  vid = db_lget_int("checkout",0);
  db_begin_transaction();

  /* step 1:
  ** Populate the temp table "sfile" with the names of all unmanaged
  ** files currently in the check-out, except for files that match the
  ** --ignore or ignore-glob patterns and dot-files.  Then add all of
  ** the files in the sfile temp table to the set of managed files.
  */
  db_multi_exec("CREATE TEMP TABLE sfile(pathname TEXT PRIMARY KEY %s)",
                filename_collation());
  n = strlen(g.zLocalRoot);
  blob_init(&path, g.zLocalRoot, n-1);
  /* now we read the complete file structure into a temp table */
  pClean = glob_create(zCleanFlag);
  pIgnore = glob_create(zIgnoreFlag);
  vfile_scan(&path, blob_size(&path), scanFlags, pClean, pIgnore, RepoFILE);
  glob_free(pIgnore);
  glob_free(pClean);
  nAdd = add_files_in_sfile(vid);

  /* step 2: search for missing files */
  db_prepare(&q,
      "SELECT pathname, %Q || pathname, deleted FROM vfile"
      " WHERE NOT deleted"
      " ORDER BY 1",
      g.zLocalRoot
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFile;
    const char *zPath;

    zFile = db_column_text(&q, 0);
    zPath = db_column_text(&q, 1);
    if( !file_isfile_or_link(zPath) ){
      if( !dryRunFlag ){
        db_multi_exec("UPDATE vfile SET deleted=1 WHERE pathname=%Q", zFile);
      }
      fossil_print("DELETED  %s\n", zFile);
      nDelete++;
    }
  }
  db_finalize(&q);
  /* show command summary */
  fossil_print("added %d files, deleted %d files\n", nAdd, nDelete);
  if(dryRunFlag!=0){
    fossil_print("Dry-run mode: no changes were made.\n");
  }
  db_end_transaction(dryRunFlag);
}

/*
** Rename a single file.
**
** The original name of the file is zOrig.  The new filename is zNew.
*/
static void mv_one_file(
  int vid,
  const char *zOrig,
  const char *zNew,
  int dryRunFlag,
  int moveFiles
){
  int x = db_int(-1, "SELECT deleted FROM vfile WHERE pathname=%Q %s",
                         zNew, filename_collation());
  if( x>=0 ){
    if( x==0 ){
      if( !filenames_are_case_sensitive() && fossil_stricmp(zOrig,zNew)==0 ){
        /* Case change only */
      }else{
        fossil_fatal("cannot rename '%s' to '%s' since another file named '%s'"
                     " is currently under management", zOrig, zNew, zNew);
      }
    }else{
      fossil_fatal("cannot rename '%s' to '%s' since the delete of '%s' has "
                   "not yet been committed", zOrig, zNew, zNew);
    }
  }
  if( moveFiles ){
    if( file_size(zNew, ExtFILE) != -1 ){
      fossil_fatal("cannot rename '%s' to '%s' on disk since another file"
        " named '%s' already exists", zOrig, zNew, zNew);      
    }
  }
  fossil_print("RENAME %s %s\n", zOrig, zNew);
  if( !dryRunFlag ){
    db_multi_exec(
      "UPDATE vfile SET pathname='%q' WHERE pathname='%q' %s AND vid=%d",
      zNew, zOrig, filename_collation(), vid
    );
  }
}

/*
** This function adds a file to list of files to move on disk after the
** other actions required for the parent operation have completed
** successfully.  The first time it is called for the current process,
** it creates a temporary table named "fmove", to keep track of these
** files.
*/
static void add_file_to_move(
  const char *zOldName, /* The old name of the file on disk. */
  const char *zNewName  /* The new name of the file on disk. */
){
  static int tableCreated = 0;
  Blob fullOldName;
  Blob fullNewName;
  char *zOld, *zNew;
  if( !tableCreated ){
    db_multi_exec("CREATE TEMP TABLE fmove(x TEXT PRIMARY KEY %s, y TEXT %s)",
                  filename_collation(), filename_collation());
    tableCreated = 1;
  }
  file_tree_name(zOldName, &fullOldName, 1, 1);
  zOld = blob_str(&fullOldName);
  file_tree_name(zNewName, &fullNewName, 1, 1);
  zNew = blob_str(&fullNewName);
  if( filenames_are_case_sensitive() || fossil_stricmp(zOld,zNew)!=0 ){
    db_multi_exec("INSERT INTO fmove VALUES('%q','%q');", zOld, zNew);
  }
  blob_reset(&fullNewName);
  blob_reset(&fullOldName);
}

/*
** This function moves files within the check-out, using the file names
** contained in the temporary table "fmove".  The temporary table is
** created on demand by the add_file_to_move() function.
**
** If dryRunFlag is non-zero, no files will be moved; however, their
** names will still be output.
**
** The temporary table "fmove" is dropped after being processed.
*/
static void process_files_to_move(
  int dryRunFlag /* Zero to actually operate on the file-system. */
){
  Stmt move;
  if( db_table_exists("temp", "fmove") ){
    db_prepare(&move, "SELECT x, y FROM fmove ORDER BY x;");
    while( db_step(&move)==SQLITE_ROW ){
      const char *zOldName = db_column_text(&move, 0);
      const char *zNewName = db_column_text(&move, 1);
      if( !dryRunFlag ){
        int isOldDir = file_isdir(zOldName, RepoFILE);
        if( isOldDir==1 ){
          int isNewDir = file_isdir(zNewName, RepoFILE);
          if( isNewDir==0 ){
            file_rename(zOldName, zNewName, isOldDir, isNewDir);
          }
        }else{
          if( file_islink(zOldName) ){
            symlink_copy(zOldName, zNewName);
          }else{
            file_copy(zOldName, zNewName);
          }
          file_delete(zOldName);
        }
      }
      fossil_print("MOVED_FILE %s\n", zOldName);
    }
    db_finalize(&move);
    db_multi_exec("DROP TABLE fmove;");
  }
}

/*
** COMMAND: mv
** COMMAND: rename*
**
** Usage: %fossil mv|rename OLDNAME NEWNAME
**    or: %fossil mv|rename OLDNAME... DIR
**
** Move or rename one or more files or directories within the repository tree.
** You can either rename a file or directory or move it to another subdirectory.
**
** The 'mv' command does NOT normally rename or move the files on disk.
** This command merely records the fact that file names have changed so
** that appropriate notations can be made at the next [[commit]].
** However, the default behavior of this command may be overridden via
** command line options listed below and/or the 'mv-rm-files' setting.
**
** The 'rename' command never renames or moves files on disk, even when the
** command line options and/or the 'mv-rm-files' setting would otherwise
** require it to do so.
**
** WARNING: If the "--hard" option is specified -OR- the "mv-rm-files"
**          setting is non-zero, files WILL BE renamed or moved on disk
**          as well.  This does NOT apply to the 'rename' command.
**
** Options:
**   --soft                    Skip moving files within the check-out.
**                             This supersedes the --hard option.
**   --hard                    Move files within the check-out
**   --case-sensitive BOOL     Override the case-sensitive setting
**   -n|--dry-run              If given, display instead of run actions
**
** See also: [[changes]], [[status]]
*/
void mv_cmd(void){
  int i;
  int vid;
  int moveFiles;
  int dryRunFlag;
  int softFlag;
  int hardFlag;
  int origType;
  int destType;
  char *zDest;
  Blob dest;
  Stmt q;

  db_must_be_within_tree();
  dryRunFlag = find_option("dry-run","n",0)!=0;
  softFlag = find_option("soft",0,0)!=0;
  hardFlag = find_option("hard",0,0)!=0;

  /* We should be done with options.. */
  verify_all_options();

  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("no check-out in which to rename files");
  }
  if( g.argc<4 ){
    usage("OLDNAME NEWNAME");
  }
  zDest = file_case_preferred_name(".",g.argv[g.argc-1]);
  db_begin_transaction();
  if( g.argv[1][0]=='r' ){ /* i.e. "rename" */
    moveFiles = 0;
  }else if( softFlag ){
    moveFiles = 0;
  }else if( hardFlag ){
    moveFiles = 1;
  }else{
    moveFiles = db_get_boolean("mv-rm-files",0);
  }
  file_tree_name(zDest, &dest, 0, 1);
  db_multi_exec(
    "UPDATE vfile SET origname=pathname WHERE origname IS NULL;"
  );
  db_multi_exec(
    "CREATE TEMP TABLE mv(f TEXT UNIQUE ON CONFLICT IGNORE, t TEXT);"
  );
  if( g.argc!=4 ){
    origType = -1;
  }else{
    origType = (file_isdir(g.argv[2], RepoFILE) == 1);
  }
  destType = file_isdir(zDest, RepoFILE);
  if( origType==-1 && destType!=1 ){
    usage("OLDNAME NEWNAME");
  }else if( origType==1 && destType==2 ){
    fossil_fatal("cannot rename '%s' to '%s' since another file named"
                 " '%s' exists", g.argv[2], zDest, zDest);
  }else if( origType==0 && destType!=1 ){
    Blob orig;
    file_tree_name(g.argv[2], &orig, 0, 1);
    db_multi_exec(
      "INSERT INTO mv VALUES(%B,%B)", &orig, &dest
    );
  }else{
    if( blob_eq(&dest, ".") ){
      blob_reset(&dest);
    }else{
      blob_append(&dest, "/", 1);
    }
    for(i=2; i<g.argc-1; i++){
      Blob orig;
      char *zOrig;
      int nOrig;
      file_tree_name(g.argv[i], &orig, 0, 1);
      zOrig = blob_str(&orig);
      nOrig = blob_size(&orig);
      db_prepare(&q,
         "SELECT pathname FROM vfile"
         " WHERE vid=%d"
         "  AND (pathname='%q' %s OR (pathname>'%q/' %s AND pathname<'%q0' %s))"
         " ORDER BY 1",
         vid, zOrig, filename_collation(), zOrig, filename_collation(),
         zOrig, filename_collation()
      );
      while( db_step(&q)==SQLITE_ROW ){
        const char *zPath = db_column_text(&q, 0);
        int nPath = db_column_bytes(&q, 0);
        const char *zTail;
        if( nPath==nOrig ){
          zTail = file_tail(zPath);
        }else if( origType!=0 && destType==1 ){
          zTail = &zPath[nOrig-strlen(file_tail(zOrig))];
        }else{
          zTail = &zPath[nOrig+1];
        }
        db_multi_exec(
          "INSERT INTO mv VALUES('%q','%q%q')",
          zPath, blob_str(&dest), zTail
        );
      }
      db_finalize(&q);
    }
  }
  db_prepare(&q, "SELECT f, t FROM mv ORDER BY f");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFrom = db_column_text(&q, 0);
    const char *zTo = db_column_text(&q, 1);
    mv_one_file(vid, zFrom, zTo, dryRunFlag, moveFiles);
    if( moveFiles ) add_file_to_move(zFrom, zTo);
  }
  db_finalize(&q);
  undo_reset();
  db_end_transaction(0);
  if( moveFiles ) process_files_to_move(dryRunFlag);
  fossil_free(zDest);
}

/*
** Function for stash_apply to be able to restore a file and indicate
** newly ADDED state.
*/
int stash_add_files_in_sfile(int vid){
  return add_files_in_sfile(vid);
}
