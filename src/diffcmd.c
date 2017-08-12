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
** This file contains code used to implement the "diff" command
*/
#include "config.h"
#include "diffcmd.h"
#include <assert.h>

/*
** Use the right null device for the platform.
*/
#if defined(_WIN32)
#  define NULL_DEVICE "NUL"
#else
#  define NULL_DEVICE "/dev/null"
#endif

/*
** Used when the name for the diff is unknown.
*/
#define DIFF_NO_NAME  "(unknown)"

/*
** Use the "exec-rel-paths" setting and the --exec-abs-paths and
** --exec-rel-paths command line options to determine whether
** certain external commands are executed using relative paths.
*/
static int determine_exec_relative_option(int force){
  static int relativePaths = -1;
  if( force || relativePaths==-1 ){
    int relPathOption = find_option("exec-rel-paths", 0, 0)!=0;
    int absPathOption = find_option("exec-abs-paths", 0, 0)!=0;
#if defined(FOSSIL_ENABLE_EXEC_REL_PATHS)
    relativePaths = db_get_boolean("exec-rel-paths", 1);
#else
    relativePaths = db_get_boolean("exec-rel-paths", 0);
#endif
    if( relPathOption ){ relativePaths = 1; }
    if( absPathOption ){ relativePaths = 0; }
  }
  return relativePaths;
}

#if INTERFACE
/*
** An array of FileDirList objects describe the files and directories listed
** on the command line of a "diff" command.  Only those objects listed are
** actually diffed.
*/
struct FileDirList {
  int nUsed;       /* Number of times each entry is used */
  int nName;       /* Length of the entry */
  char *zName;     /* Text of the entry */
};
#endif

/*
** Return true if zFile is a file named on the azInclude[] list or is
** a file in a directory named on the azInclude[] list.
**
** if azInclude is NULL, then always include zFile.
*/
static int file_dir_match(FileDirList *p, const char *zFile){
  if( p==0 || strcmp(p->zName,".")==0 ) return 1;
  if( filenames_are_case_sensitive() ){
    while( p->zName ){
      if( strcmp(zFile, p->zName)==0
       || (strncmp(zFile, p->zName, p->nName)==0
           && zFile[p->nName]=='/')
      ){
        break;
      }
      p++;
    }
  }else{
    while( p->zName ){
      if( fossil_stricmp(zFile, p->zName)==0
       || (fossil_strnicmp(zFile, p->zName, p->nName)==0
           && zFile[p->nName]=='/')
      ){
        break;
      }
      p++;
    }
  }
  if( p->zName ){
    p->nUsed++;
    return 1;
  }
  return 0;
}

/*
** Print the "Index:" message that patches wants to see at the top of a diff.
*/
void diff_print_index(const char *zFile, u64 diffFlags){
  if( (diffFlags & (DIFF_SIDEBYSIDE|DIFF_BRIEF|DIFF_NUMSTAT))==0 ){
    char *z = mprintf("Index: %s\n%.66c\n", zFile, '=');
    fossil_print("%s", z);
    fossil_free(z);
  }
}

/*
** Print the +++/--- filename lines for a diff operation.
*/
void diff_print_filenames(const char *zLeft, const char *zRight, u64 diffFlags){
  char *z = 0;
  if( diffFlags & DIFF_BRIEF ){
    /* no-op */
  }else if( diffFlags & DIFF_SIDEBYSIDE ){
    int w = diff_width(diffFlags);
    int n1 = strlen(zLeft);
    int n2 = strlen(zRight);
    int x;
    if( n1==n2 && fossil_strcmp(zLeft,zRight)==0 ){
      if( n1>w*2 ) n1 = w*2;
      x = w*2+17 - (n1+2);
      z = mprintf("%.*c %.*s %.*c\n",
                 x/2, '=', n1, zLeft, (x+1)/2, '=');
    }else{
      if( w<20 ) w = 20;
      if( n1>w-10 ) n1 = w - 10;
      if( n2>w-10 ) n2 = w - 10;
      z = mprintf("%.*c %.*s %.*c versus %.*c %.*s %.*c\n",
                  (w-n1+10)/2, '=', n1, zLeft, (w-n1+1)/2, '=',
                  (w-n2)/2, '=', n2, zRight, (w-n2+1)/2, '=');
    }
  }else{
    z = mprintf("--- %s\n+++ %s\n", zLeft, zRight);
  }
  fossil_print("%s", z);
  fossil_free(z);
}

/*
** Show the difference between two files, one in memory and one on disk.
**
** The difference is the set of edits needed to transform pFile1 into
** zFile2.  The content of pFile1 is in memory.  zFile2 exists on disk.
**
** If fSwapDiff is 1, show the set of edits to transform zFile2 into pFile1
** instead of the opposite.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
void diff_file(
  Blob *pFile1,             /* In memory content to compare from */
  int isBin1,               /* Does the 'from' content appear to be binary */
  const char *zFile2,       /* On disk content to compare to */
  const char *zName,        /* Display name of the file */
  const char *zDiffCmd,     /* Command for comparison */
  const char *zBinGlob,     /* Treat file names matching this as binary */
  int fIncludeBinary,       /* Include binary files for external diff */
  u64 diffFlags,            /* Flags to control the diff */
  int fSwapDiff             /* Diff from Zfile2 to Pfile1 */
){
  if( zDiffCmd==0 ){
    Blob out;                 /* Diff output text */
    Blob file2;               /* Content of zFile2 */
    const char *zName2;       /* Name of zFile2 for display */

    /* Read content of zFile2 into memory */
    blob_zero(&file2);
    if( file_wd_size(zFile2)<0 ){
      zName2 = NULL_DEVICE;
    }else{
      if( file_wd_islink(0) ){
        blob_read_link(&file2, zFile2);
      }else{
        blob_read_from_file(&file2, zFile2);
      }
      zName2 = zName;
    }

    /* Compute and output the differences */
    if( diffFlags & DIFF_BRIEF ){
      if( blob_compare(pFile1, &file2) ){
        fossil_print("CHANGED  %s\n", zName);
      }
    }else{
      blob_zero(&out);
      if( fSwapDiff ){
        text_diff(&file2, pFile1, &out, 0, diffFlags);
      }else{
        text_diff(pFile1, &file2, &out, 0, diffFlags);
      }
      if( blob_size(&out) ){
        if( diffFlags & DIFF_NUMSTAT ){
          fossil_print("%s %s\n", blob_str(&out), zName);
        }else{
          diff_print_filenames(zName, zName2, diffFlags);
          fossil_print("%s\n", blob_str(&out));
        }
      }
      blob_reset(&out);
    }

    /* Release memory resources */
    blob_reset(&file2);
  }else{
    int cnt = 0;
    Blob nameFile1;    /* Name of temporary file to old pFile1 content */
    Blob cmd;          /* Text of command to run */

    if( !fIncludeBinary ){
      Blob file2;
      if( isBin1 ){
        fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
        return;
      }
      if( zBinGlob ){
        Glob *pBinary = glob_create(zBinGlob);
        if( glob_match(pBinary, zName) ){
          fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
          glob_free(pBinary);
          return;
        }
        glob_free(pBinary);
      }
      blob_zero(&file2);
      if( file_wd_size(zFile2)>=0 ){
        if( file_wd_islink(0) ){
          blob_read_link(&file2, zFile2);
        }else{
          blob_read_from_file(&file2, zFile2);
        }
      }
      if( looks_like_binary(&file2) ){
        fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
        blob_reset(&file2);
        return;
      }
      blob_reset(&file2);
    }

    /* Construct a temporary file to hold pFile1 based on the name of
    ** zFile2 */
    blob_zero(&nameFile1);
    do{
      blob_reset(&nameFile1);
      blob_appendf(&nameFile1, "%s~%d", zFile2, cnt++);
    }while( file_access(blob_str(&nameFile1),F_OK)==0 );
    blob_write_to_file(pFile1, blob_str(&nameFile1));

    /* Construct the external diff command */
    blob_zero(&cmd);
    blob_append(&cmd, zDiffCmd, -1);
    if( fSwapDiff ){
      blob_append_escaped_arg(&cmd, zFile2);
      blob_append_escaped_arg(&cmd, blob_str(&nameFile1));
    }else{
      blob_append_escaped_arg(&cmd, blob_str(&nameFile1));
      blob_append_escaped_arg(&cmd, zFile2);
    }

    /* Run the external diff command */
    fossil_system(blob_str(&cmd));

    /* Delete the temporary file and clean up memory used */
    file_delete(blob_str(&nameFile1));
    blob_reset(&nameFile1);
    blob_reset(&cmd);
  }
}

/*
** Show the difference between two files, both in memory.
**
** The difference is the set of edits needed to transform pFile1 into
** pFile2.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
void diff_file_mem(
  Blob *pFile1,             /* In memory content to compare from */
  Blob *pFile2,             /* In memory content to compare to */
  int isBin1,               /* Does the 'from' content appear to be binary */
  int isBin2,               /* Does the 'to' content appear to be binary */
  const char *zName,        /* Display name of the file */
  const char *zDiffCmd,     /* Command for comparison */
  const char *zBinGlob,     /* Treat file names matching this as binary */
  int fIncludeBinary,       /* Include binary files for external diff */
  u64 diffFlags             /* Diff flags */
){
  if( diffFlags & DIFF_BRIEF ) return;
  if( zDiffCmd==0 ){
    Blob out;      /* Diff output text */

    blob_zero(&out);
    text_diff(pFile1, pFile2, &out, 0, diffFlags);
    if( diffFlags & DIFF_NUMSTAT ){
      fossil_print("%s %s\n", blob_str(&out), zName);
    }else{
      diff_print_filenames(zName, zName, diffFlags);
      fossil_print("%s\n", blob_str(&out));
    }

    /* Release memory resources */
    blob_reset(&out);
  }else{
    Blob cmd;
    Blob temp1;
    Blob temp2;
    Blob prefix1;
    Blob prefix2;

    if( !fIncludeBinary ){
      if( isBin1 || isBin2 ){
        fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
        return;
      }
      if( zBinGlob ){
        Glob *pBinary = glob_create(zBinGlob);
        if( glob_match(pBinary, zName) ){
          fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
          glob_free(pBinary);
          return;
        }
        glob_free(pBinary);
      }
    }

    /* Construct a prefix for the temporary file names */
    blob_zero(&prefix1);
    blob_zero(&prefix2);
    blob_appendf(&prefix1, "%s-v1", zName);
    blob_appendf(&prefix2, "%s-v2", zName);

    /* Construct a temporary file names */
    file_tempname(&temp1, blob_str(&prefix1));
    file_tempname(&temp2, blob_str(&prefix2));
    blob_write_to_file(pFile1, blob_str(&temp1));
    blob_write_to_file(pFile2, blob_str(&temp2));

    /* Construct the external diff command */
    blob_zero(&cmd);
    blob_append(&cmd, zDiffCmd, -1);
    blob_append_escaped_arg(&cmd, blob_str(&temp1));
    blob_append_escaped_arg(&cmd, blob_str(&temp2));

    /* Run the external diff command */
    fossil_system(blob_str(&cmd));

    /* Delete the temporary file and clean up memory used */
    file_delete(blob_str(&temp1));
    file_delete(blob_str(&temp2));

    blob_reset(&prefix1);
    blob_reset(&prefix2);
    blob_reset(&temp1);
    blob_reset(&temp2);
    blob_reset(&cmd);
  }
}

/*
** Run a diff between the version zFrom and files on disk.  zFrom might
** be NULL which means to simply show the difference between the edited
** files on disk and the check-out on which they are based.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
static void diff_against_disk(
  const char *zFrom,        /* Version to difference from */
  const char *zDiffCmd,     /* Use this diff command.  NULL for built-in */
  const char *zBinGlob,     /* Treat file names matching this as binary */
  int fIncludeBinary,       /* Treat file names matching this as binary */
  u64 diffFlags,            /* Flags controlling diff output */
  FileDirList *pFileDir     /* Which files to diff */
){
  int vid;
  Blob sql;
  Stmt q;
  int asNewFile;            /* Treat non-existant files as empty files */
  int isNumStat;            /* True for --numstat */

  asNewFile = (diffFlags & (DIFF_VERBOSE|DIFF_NUMSTAT))!=0;
  isNumStat = (diffFlags & DIFF_NUMSTAT)!=0;
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid, CKSIG_ENOTFILE);
  blob_zero(&sql);
  db_begin_transaction();
  if( zFrom ){
    int rid = name_to_typed_rid(zFrom, "ci");
    if( !is_a_version(rid) ){
      fossil_fatal("no such check-in: %s", zFrom);
    }
    load_vfile_from_rid(rid);
    blob_append_sql(&sql,
      "SELECT v2.pathname, v2.deleted, v2.chnged, v2.rid==0, v1.rid, v1.islink"
      "  FROM vfile v1, vfile v2 "
      " WHERE v1.pathname=v2.pathname AND v1.vid=%d AND v2.vid=%d"
      "   AND (v2.deleted OR v2.chnged OR v1.mrid!=v2.rid)"
      "UNION "
      "SELECT pathname, 1, 0, 0, 0, islink"
      "  FROM vfile v1"
      " WHERE v1.vid=%d"
      "   AND NOT EXISTS(SELECT 1 FROM vfile v2"
                        " WHERE v2.vid=%d AND v2.pathname=v1.pathname)"
      "UNION "
      "SELECT pathname, 0, 0, 1, 0, islink"
      "  FROM vfile v2"
      " WHERE v2.vid=%d"
      "   AND NOT EXISTS(SELECT 1 FROM vfile v1"
                        " WHERE v1.vid=%d AND v1.pathname=v2.pathname)"
      " ORDER BY 1 /*scan*/",
      rid, vid, rid, vid, vid, rid
    );
  }else{
    blob_append_sql(&sql,
      "SELECT pathname, deleted, chnged , rid==0, rid, islink"
      "  FROM vfile"
      " WHERE vid=%d"
      "   AND (deleted OR chnged OR rid==0)"
      " ORDER BY pathname /*scan*/",
      vid
    );
  }
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3);
    int srcid = db_column_int(&q, 4);
    int isLink = db_column_int(&q, 5);
    const char *zFullName;
    int showDiff = 1;
    Blob fname;

    if( !file_dir_match(pFileDir, zPathname) ) continue;
    if( determine_exec_relative_option(0) ){
      blob_zero(&fname);
      file_relative_name(zPathname, &fname, 1);
    }else{
      blob_set(&fname, g.zLocalRoot);
      blob_append(&fname, zPathname, -1);
    }
    zFullName = blob_str(&fname);
    if( isDeleted ){
      if( !isNumStat ){ fossil_print("DELETED  %s\n", zPathname); }
      if( !asNewFile ){ showDiff = 0; zFullName = NULL_DEVICE; }
    }else if( file_access(zFullName, F_OK) ){
      if( !isNumStat ){ fossil_print("MISSING  %s\n", zPathname); }
      if( !asNewFile ){ showDiff = 0; }
    }else if( isNew ){
      if( !isNumStat ){ fossil_print("ADDED    %s\n", zPathname); }
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }else if( isChnged==3 ){
      if( !isNumStat ){ fossil_print("ADDED_BY_MERGE %s\n", zPathname); }
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }else if( isChnged==5 ){
      if( !isNumStat ){ fossil_print("ADDED_BY_INTEGRATE %s\n", zPathname); }
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }
    if( showDiff ){
      Blob content;
      int isBin;
      if( !isLink != !file_wd_islink(zFullName) ){
        diff_print_index(zPathname, diffFlags);
        diff_print_filenames(zPathname, zPathname, diffFlags);
        fossil_print("%s",DIFF_CANNOT_COMPUTE_SYMLINK);
        continue;
      }
      if( srcid>0 ){
        content_get(srcid, &content);
      }else{
        blob_zero(&content);
      }
      isBin = fIncludeBinary ? 0 : looks_like_binary(&content);
      diff_print_index(zPathname, diffFlags);
      diff_file(&content, isBin, zFullName, zPathname, zDiffCmd,
                zBinGlob, fIncludeBinary, diffFlags, 0);
      blob_reset(&content);
    }
    blob_reset(&fname);
  }
  db_finalize(&q);
  db_end_transaction(1);  /* ROLLBACK */
}

/*
** Run a diff between the undo buffer and files on disk.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
static void diff_against_undo(
  const char *zDiffCmd,     /* Use this diff command.  NULL for built-in */
  const char *zBinGlob,     /* Treat file names matching this as binary */
  int fIncludeBinary,       /* Treat file names matching this as binary */
  u64 diffFlags,            /* Flags controlling diff output */
  FileDirList *pFileDir     /* List of files and directories to diff */
){
  Stmt q;
  Blob content;
  db_prepare(&q, "SELECT pathname, content FROM undo");
  blob_init(&content, 0, 0);
  while( db_step(&q)==SQLITE_ROW ){
    char *zFullName;
    const char *zFile = (const char*)db_column_text(&q, 0);
    if( !file_dir_match(pFileDir, zFile) ) continue;
    zFullName = mprintf("%s%s", g.zLocalRoot, zFile);
    db_column_blob(&q, 1, &content);
    diff_file(&content, 0, zFullName, zFile,
              zDiffCmd, zBinGlob, fIncludeBinary, diffFlags, 0);
    fossil_free(zFullName);
    blob_reset(&content);
  }
  db_finalize(&q);
}

/*
** Show the difference between two files identified by ManifestFile
** entries.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
static void diff_manifest_entry(
  struct ManifestFile *pFrom,
  struct ManifestFile *pTo,
  const char *zDiffCmd,
  const char *zBinGlob,
  int fIncludeBinary,
  u64 diffFlags
){
  Blob f1, f2;
  int isBin1, isBin2;
  int rid;
  const char *zName;
  if( pFrom ){
    zName = pFrom->zName;
  }else if( pTo ){
    zName = pTo->zName;
  }else{
    zName = DIFF_NO_NAME;
  }
  if( diffFlags & DIFF_BRIEF ) return;
  diff_print_index(zName, diffFlags);
  if( pFrom ){
    rid = uuid_to_rid(pFrom->zUuid, 0);
    content_get(rid, &f1);
  }else{
    blob_zero(&f1);
  }
  if( pTo ){
    rid = uuid_to_rid(pTo->zUuid, 0);
    content_get(rid, &f2);
  }else{
    blob_zero(&f2);
  }
  isBin1 = fIncludeBinary ? 0 : looks_like_binary(&f1);
  isBin2 = fIncludeBinary ? 0 : looks_like_binary(&f2);
  diff_file_mem(&f1, &f2, isBin1, isBin2, zName, zDiffCmd,
                zBinGlob, fIncludeBinary, diffFlags);
  blob_reset(&f1);
  blob_reset(&f2);
}

/*
** Output the differences between two check-ins.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
static void diff_two_versions(
  const char *zFrom,
  const char *zTo,
  const char *zDiffCmd,
  const char *zBinGlob,
  int fIncludeBinary,
  u64 diffFlags,
  FileDirList *pFileDir
){
  Manifest *pFrom, *pTo;
  ManifestFile *pFromFile, *pToFile;
  int asNewFlag = (diffFlags & (DIFF_VERBOSE|DIFF_NUMSTAT))!=0 ? 1 : 0;

  pFrom = manifest_get_by_name(zFrom, 0);
  manifest_file_rewind(pFrom);
  pFromFile = manifest_file_next(pFrom,0);
  pTo = manifest_get_by_name(zTo, 0);
  manifest_file_rewind(pTo);
  pToFile = manifest_file_next(pTo,0);

  while( pFromFile || pToFile ){
    int cmp;
    if( pFromFile==0 ){
      cmp = +1;
    }else if( pToFile==0 ){
      cmp = -1;
    }else{
      cmp = fossil_strcmp(pFromFile->zName, pToFile->zName);
    }
    if( cmp<0 ){
      if( file_dir_match(pFileDir, pFromFile->zName) ){
        if( (diffFlags & DIFF_NUMSTAT)==0 ){
          fossil_print("DELETED %s\n", pFromFile->zName);
        }
        if( asNewFlag ){
          diff_manifest_entry(pFromFile, 0, zDiffCmd, zBinGlob,
                              fIncludeBinary, diffFlags);
        }
      }
      pFromFile = manifest_file_next(pFrom,0);
    }else if( cmp>0 ){
      if( file_dir_match(pFileDir, pToFile->zName) ){
        if( (diffFlags & DIFF_NUMSTAT)==0 ){
          fossil_print("ADDED   %s\n", pToFile->zName);
        }
        if( asNewFlag ){
          diff_manifest_entry(0, pToFile, zDiffCmd, zBinGlob,
                              fIncludeBinary, diffFlags);
        }
      }
      pToFile = manifest_file_next(pTo,0);
    }else if( fossil_strcmp(pFromFile->zUuid, pToFile->zUuid)==0 ){
      /* No changes */
      (void)file_dir_match(pFileDir, pFromFile->zName); /* Record name usage */
      pFromFile = manifest_file_next(pFrom,0);
      pToFile = manifest_file_next(pTo,0);
    }else{
      if( file_dir_match(pFileDir, pToFile->zName) ){
        if( diffFlags & DIFF_BRIEF ){
          fossil_print("CHANGED %s\n", pFromFile->zName);
        }else{
          diff_manifest_entry(pFromFile, pToFile, zDiffCmd, zBinGlob,
                              fIncludeBinary, diffFlags);
        }
      }
      pFromFile = manifest_file_next(pFrom,0);
      pToFile = manifest_file_next(pTo,0);
    }
  }
  manifest_destroy(pFrom);
  manifest_destroy(pTo);
}

/*
** Return the name of the external diff command, or return NULL if
** no external diff command is defined.
*/
const char *diff_command_external(int guiDiff){
  const char *zDefault;
  const char *zName;

  if( guiDiff ){
#if defined(_WIN32)
    zDefault = "WinDiff.exe";
#else
    zDefault = 0;
#endif
    zName = "gdiff-command";
  }else{
    zDefault = 0;
    zName = "diff-command";
  }
  return db_get(zName, zDefault);
}

/*
** Show diff output in a Tcl/Tk window, in response to the --tk option
** to the diff command.
**
** If fossil has direct access to a Tcl interpreter (either loaded
** dynamically through stubs or linked in statically), we can use it
** directly. Otherwise:
** (1) Write the Tcl/Tk script used for rendering into a temp file.
** (2) Invoke "tclsh" on the temp file using fossil_system().
** (3) Delete the temp file.
*/
void diff_tk(const char *zSubCmd, int firstArg){
  int i;
  Blob script;
  const char *zTempFile = 0;
  char *zCmd;
  blob_zero(&script);
  blob_appendf(&script, "set fossilcmd {| \"%/\" %s --html -y -i -v",
               g.nameOfExe, zSubCmd);
  find_option("html",0,0);
  find_option("side-by-side","y",0);
  find_option("internal","i",0);
  find_option("verbose","v",0);
  /* The undocumented --script FILENAME option causes the Tk script to
  ** be written into the FILENAME instead of being run.  This is used
  ** for testing and debugging. */
  zTempFile = find_option("script",0,1);
  for(i=firstArg; i<g.argc; i++){
    const char *z = g.argv[i];
    if( sqlite3_strglob("*}*",z) ){
      blob_appendf(&script, " {%/}", z);
    }else{
      int j;
      blob_append(&script, " ", 1);
      for(j=0; z[j]; j++) blob_appendf(&script, "\\%03o", (unsigned char)z[j]);
    }
  }
  blob_appendf(&script, "}\n%s", builtin_file("diff.tcl", 0));
  if( zTempFile ){
    blob_write_to_file(&script, zTempFile);
    fossil_print("To see diff, run: tclsh \"%s\"\n", zTempFile);
  }else{
#if defined(FOSSIL_ENABLE_TCL)
    Th_FossilInit(TH_INIT_DEFAULT);
    if( evaluateTclWithEvents(g.interp, &g.tcl, blob_str(&script),
                              blob_size(&script), 1, 1, 0)==TCL_OK ){
      blob_reset(&script);
      return;
    }
    /*
     * If evaluation of the Tcl script fails, the reason may be that Tk
     * could not be found by the loaded Tcl, or that Tcl cannot be loaded
     * dynamically (e.g. x64 Tcl with x86 Fossil).  Therefore, fallback
     * to using the external "tclsh", if available.
     */
#endif
    zTempFile = write_blob_to_temp_file(&script);
    zCmd = mprintf("tclsh \"%s\"", zTempFile);
    fossil_system(zCmd);
    file_delete(zTempFile);
    fossil_free(zCmd);
  }
  blob_reset(&script);
}

/*
** Returns non-zero if files that may be binary should be used with external
** diff programs.
*/
int diff_include_binary_files(void){
  const char* zArgIncludeBinary = find_option("diff-binary", 0, 1);

  /* Command line argument have priority on settings */
  if( zArgIncludeBinary ){
    return is_truth(zArgIncludeBinary);
  }else{
    return db_get_boolean("diff-binary", 1);
  }
}

/*
** Returns the GLOB pattern for file names that should be treated as binary
** by the diff subsystem, if any.
*/
const char *diff_get_binary_glob(void){
  const char *zBinGlob = find_option("binary", 0, 1);
  if( zBinGlob==0 ) zBinGlob = db_get("binary-glob",0);
  return zBinGlob;
}

/*
** COMMAND: diff
** COMMAND: gdiff
**
** Usage: %fossil diff|gdiff ?OPTIONS? ?FILE1? ?FILE2 ...?
**
** Show the difference between the current version of each of the FILEs
** specified (as they exist on disk) and that same file as it was checked
** out.  Or if the FILE arguments are omitted, show the unsaved changes
** currently in the working check-out.
**
** If the "--from VERSION" or "-r VERSION" option is used it specifies
** the source check-in for the diff operation.  If not specified, the
** source check-in is the base check-in for the current check-out.
**
** If the "--to VERSION" option appears, it specifies the check-in from
** which the second version of the file or files is taken.  If there is
** no "--to" option then the (possibly edited) files in the current check-out
** are used.
**
** The "--checkin VERSION" option shows the changes made by
** check-in VERSION relative to its primary parent.
**
** The "-i" command-line option forces the use of the internal diff logic
** rather than any external diff program that might be configured using
** the "setting" command.  If no external diff program is configured, then
** the "-i" option is a no-op.  The "-i" option converts "gdiff" into "diff".
**
** The "-N" or "--new-file" option causes the complete text of added or
** deleted files to be displayed.
**
** The "--diff-binary" option enables or disables the inclusion of binary files
** when using an external diff program.
**
** The "--binary" option causes files matching the glob PATTERN to be treated
** as binary when considering if they should be used with external diff program.
** This option overrides the "binary-glob" setting.
**
** Options:
**   --binary PATTERN           Treat files that match the glob PATTERN as binary
**   --branch BRANCH            Show diff of all changes on BRANCH
**   --brief                    Show filenames only
**   --checkin VERSION          Show diff of all changes in VERSION
**   --command PROG             External diff program - overrides "diff-command"
**   --context|-c N             Use N lines of context
**   --diff-binary BOOL         Include binary files when using external commands
**   --exec-abs-paths           Force absolute path names with external commands.
**   --exec-rel-paths           Force relative path names with external commands.
**   --from|-r VERSION          Select VERSION as source for the diff
**   --internal|-i              Use internal diff logic
**   --numstat                  Show only the number of lines delete and added
**   --side-by-side|-y          Side-by-side diff
**   --strip-trailing-cr        Strip trailing CR
**   --tk                       Launch a Tcl/Tk GUI for display
**   --to VERSION               Select VERSION as target for the diff
**   --undo                     Diff against the "undo" buffer
**   --unified                  Unified diff
**   -v|--verbose               Output complete text of added or deleted files
**   -w|--ignore-all-space      Ignore white space when comparing lines
**   -W|--width <num>           Width of lines in side-by-side diff
**   -Z|--ignore-trailing-space Ignore changes to end-of-line whitespace
*/
void diff_cmd(void){
  int isGDiff;               /* True for gdiff.  False for normal diff */
  int isInternDiff;          /* True for internal diff */
  int verboseFlag;           /* True if -v or --verbose flag is used */
  const char *zFrom;         /* Source version number */
  const char *zTo;           /* Target version number */
  const char *zCheckin;      /* Check-in version number */
  const char *zBranch;       /* Branch to diff */
  const char *zDiffCmd = 0;  /* External diff command. NULL for internal diff */
  const char *zBinGlob = 0;  /* Treat file names matching this as binary */
  int fIncludeBinary = 0;    /* Include binary files for external diff */
  int againstUndo = 0;       /* Diff against files in the undo buffer */
  u64 diffFlags = 0;         /* Flags to control the DIFF */
  FileDirList *pFileDir = 0; /* Restrict the diff to these files */

  if( find_option("tk",0,0)!=0 ){
    diff_tk("diff", 2);
    return;
  }
  isGDiff = g.argv[1][0]=='g';
  isInternDiff = find_option("internal","i",0)!=0;
  zFrom = find_option("from", "r", 1);
  zTo = find_option("to", 0, 1);
  zCheckin = find_option("checkin", 0, 1);
  zBranch = find_option("branch", 0, 1);
  againstUndo = find_option("undo",0,0)!=0;
  diffFlags = diff_options();
  verboseFlag = find_option("verbose","v",0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("new-file","N",0)!=0; /* deprecated */
  }
  if( verboseFlag ) diffFlags |= DIFF_VERBOSE;
  if( againstUndo && ( zFrom!=0 || zTo!=0 || zCheckin!=0 || zBranch!=0) ){
    fossil_fatal("cannot use --undo together with --from, --to, --checkin,"
                 " or --branch");
  }
  if( zBranch ){
    if( zTo || zFrom || zCheckin ){
      fossil_fatal("cannot use --from, --to, or --checkin with --branch");
    }
    zTo = zBranch;
    zFrom = mprintf("root:%s", zBranch);
  }
  if( zCheckin!=0 && ( zFrom!=0 || zTo!=0 ) ){
    fossil_fatal("cannot use --checkin together with --from or --to");
  }
  if( zTo==0 || againstUndo ){
    db_must_be_within_tree();
  }else if( zFrom==0 ){
    fossil_fatal("must use --from if --to is present");
  }else{
    db_find_and_open_repository(0, 0);
  }
  if( !isInternDiff ){
    zDiffCmd = find_option("command", 0, 1);
    if( zDiffCmd==0 ) zDiffCmd = diff_command_external(isGDiff);
  }
  zBinGlob = diff_get_binary_glob();
  fIncludeBinary = diff_include_binary_files();
  determine_exec_relative_option(1);
  verify_all_options();
  if( g.argc>=3 ){
    int i;
    Blob fname;
    pFileDir = fossil_malloc( sizeof(*pFileDir) * (g.argc-1) );
    memset(pFileDir, 0, sizeof(*pFileDir) * (g.argc-1));
    for(i=2; i<g.argc; i++){
      file_tree_name(g.argv[i], &fname, 0, 1);
      pFileDir[i-2].zName = fossil_strdup(blob_str(&fname));
      if( strcmp(pFileDir[i-2].zName,".")==0 ){
        pFileDir[0].zName[0] = '.';
        pFileDir[0].zName[1] = 0;
        break;
      }
      pFileDir[i-2].nName = blob_size(&fname);
      pFileDir[i-2].nUsed = 0;
      blob_reset(&fname);
    }
  }
  if ( zCheckin!=0 ){
    int ridTo = name_to_typed_rid(zCheckin, "ci");
    zTo = zCheckin;
    zFrom = db_text(0,
      "SELECT uuid FROM blob, plink"
      " WHERE plink.cid=%d AND plink.isprim AND plink.pid=blob.rid",
      ridTo);
    if( zFrom==0 ){
      fossil_fatal("check-in %s has no parent", zTo);
    }
  }
  if( againstUndo ){
    if( db_lget_int("undo_available",0)==0 ){
      fossil_print("No undo or redo is available\n");
      return;
    }
    diff_against_undo(zDiffCmd, zBinGlob, fIncludeBinary,
                      diffFlags, pFileDir);
  }else if( zTo==0 ){
    diff_against_disk(zFrom, zDiffCmd, zBinGlob, fIncludeBinary,
                      diffFlags, pFileDir);
  }else{
    diff_two_versions(zFrom, zTo, zDiffCmd, zBinGlob, fIncludeBinary,
                      diffFlags, pFileDir);
  }
  if( pFileDir ){
    int i;
    for(i=0; pFileDir[i].zName; i++){
      if( pFileDir[i].nUsed==0
       && strcmp(pFileDir[0].zName,".")!=0
       && !file_wd_isdir(g.argv[i+2])
      ){
        fossil_fatal("not found: '%s'", g.argv[i+2]);
      }
      fossil_free(pFileDir[i].zName);
    }
    fossil_free(pFileDir);
  }
}

/*
** WEBPAGE: vpatch
** URL: /vpatch?from=FROM&to=TO
**
** Show a patch that goes from check-in FROM to check-in TO.
*/
void vpatch_page(void){
  const char *zFrom = P("from");
  const char *zTo = P("to");
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( zFrom==0 || zTo==0 ) fossil_redirect_home();

  cgi_set_content_type("text/plain");
  diff_two_versions(zFrom, zTo, 0, 0, 0, DIFF_VERBOSE, 0);
}
