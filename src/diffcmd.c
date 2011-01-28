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
** Diff option flags
*/
#define DIFF_NEWFILE  0x01    /* Treat non-existing fails as empty files */
#define DIFF_NOEOLWS  0x02    /* Ignore whitespace at the end of lines */

/*
** Print the "Index:" message that patch wants to see at the top of a diff.
*/
void diff_print_index(const char *zFile){
  printf("Index: %s\n======================================="
         "============================\n", zFile);
}

/*
** Show the difference between two files, one in memory and one on disk.
**
** The difference is the set of edits needed to transform pFile1 into
** zFile2.  The content of pFile1 is in memory.  zFile2 exists on disk.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
*/
void diff_file(
  Blob *pFile1,             /* In memory content to compare from */
  const char *zFile2,       /* On disk content to compare to */
  const char *zName,        /* Display name of the file */
  const char *zDiffCmd,     /* Command for comparison */
  int ignoreEolWs           /* Ignore whitespace at end of line */
){
  if( zDiffCmd==0 ){
    Blob out;                 /* Diff output text */
    Blob file2;               /* Content of zFile2 */
    const char *zName2;       /* Name of zFile2 for display */

    /* Read content of zFile2 into memory */
    blob_zero(&file2);
    if( file_size(zFile2)<0 ){
      zName2 = "/dev/null";
    }else{
      if( file_islink(zFile2) ){
        blob_read_link(&file2, zFile2);
      }else{
        blob_read_from_file(&file2, zFile2);
      }
      zName2 = zName;
    }

    /* Compute and output the differences */
    blob_zero(&out);
    text_diff(pFile1, &file2, &out, 5, ignoreEolWs);
    printf("--- %s\n+++ %s\n", zName, zName2);
    printf("%s\n", blob_str(&out));

    /* Release memory resources */
    blob_reset(&file2);
    blob_reset(&out);
  }else{
    int cnt = 0;
    Blob nameFile1;    /* Name of temporary file to old pFile1 content */
    Blob cmd;          /* Text of command to run */

    /* Construct a temporary file to hold pFile1 based on the name of
    ** zFile2 */
    blob_zero(&nameFile1);
    do{
      blob_reset(&nameFile1);
      blob_appendf(&nameFile1, "%s~%d", zFile2, cnt++);
    }while( access(blob_str(&nameFile1),0)==0 );
    blob_write_to_file(pFile1, blob_str(&nameFile1));

    /* Construct the external diff command */
    blob_zero(&cmd);
    blob_appendf(&cmd, "%s ", zDiffCmd);
    shell_escape(&cmd, blob_str(&nameFile1));
    blob_append(&cmd, " ", 1);
    shell_escape(&cmd, zFile2);

    /* Run the external diff command */
    fossil_system(blob_str(&cmd));

    /* Delete the temporary file and clean up memory used */
    unlink(blob_str(&nameFile1));
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
*/
void diff_file_mem(
  Blob *pFile1,             /* In memory content to compare from */
  Blob *pFile2,             /* In memory content to compare to */
  const char *zName,        /* Display name of the file */
  const char *zDiffCmd,     /* Command for comparison */
  int ignoreEolWs           /* Ignore whitespace at end of lines */
){
  if( zDiffCmd==0 ){
    Blob out;      /* Diff output text */

    blob_zero(&out);
    text_diff(pFile1, pFile2, &out, 5, ignoreEolWs);
    printf("--- %s\n+++ %s\n", zName, zName);
    printf("%s\n", blob_str(&out));

    /* Release memory resources */
    blob_reset(&out);
  }else{
    Blob cmd;
    char zTemp1[300];
    char zTemp2[300];

    /* Construct a temporary file names */
    file_tempname(sizeof(zTemp1), zTemp1);
    file_tempname(sizeof(zTemp2), zTemp2);
    blob_write_to_file(pFile1, zTemp1);
    blob_write_to_file(pFile2, zTemp2);

    /* Construct the external diff command */
    blob_zero(&cmd);
    blob_appendf(&cmd, "%s ", zDiffCmd);
    shell_escape(&cmd, zTemp1);
    blob_append(&cmd, " ", 1);
    shell_escape(&cmd, zTemp2);

    /* Run the external diff command */
    fossil_system(blob_str(&cmd));

    /* Delete the temporary file and clean up memory used */
    unlink(zTemp1);
    unlink(zTemp2);
    blob_reset(&cmd);
  }
}

/*
** Do a diff against a single file named in g.argv[2] from version zFrom
** against the same file on disk.
*/
static void diff_one_against_disk(
  const char *zFrom,        /* Name of file */
  const char *zDiffCmd,     /* Use this "diff" command */
  int ignoreEolWs           /* Ignore whitespace changes at end of lines */
){
  Blob fname;
  Blob content;
  int isLink;
  file_tree_name(g.argv[2], &fname, 1);
  historical_version_of_file(zFrom, blob_str(&fname), &content, &isLink, 0);
  if( !isLink != !file_islink(zFrom) ){
    printf("cannot compute difference between symlink and regular file\n");
  }else{
    diff_file(&content, g.argv[2], g.argv[2], zDiffCmd, ignoreEolWs);
  }
  blob_reset(&content);
  blob_reset(&fname);
}

/*
** Run a diff between the version zFrom and files on disk.  zFrom might
** be NULL which means to simply show the difference between the edited
** files on disk and the check-out on which they are based.
*/
static void diff_all_against_disk(
  const char *zFrom,        /* Version to difference from */
  const char *zDiffCmd,     /* Use this diff command.  NULL for built-in */
  int diffFlags             /* Flags controlling diff output */
){
  int vid;
  Blob sql;
  Stmt q;
  int ignoreEolWs;          /* Ignore end-of-line whitespace */
  int asNewFile;            /* Treat non-existant files as empty files */

  ignoreEolWs = (diffFlags & DIFF_NOEOLWS)!=0;
  asNewFile = (diffFlags & DIFF_NEWFILE)!=0;
  vid = db_lget_int("checkout", 0);
  vfile_check_signature(vid, 1, 0);
  blob_zero(&sql);
  db_begin_transaction();
  if( zFrom ){
    int rid = name_to_rid(zFrom);
    if( !is_a_version(rid) ){
      fossil_fatal("no such check-in: %s", zFrom);
    }
    load_vfile_from_rid(rid);
    blob_appendf(&sql,
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
      " ORDER BY 1",
      rid, vid, rid, vid, vid, rid
    );
  }else{
    blob_appendf(&sql,
      "SELECT pathname, deleted, chnged , rid==0, rid, islink"
      "  FROM vfile"
      " WHERE vid=%d"
      "   AND (deleted OR chnged OR rid==0)"
      " ORDER BY pathname",
      vid
    );
  }
  db_prepare(&q, blob_str(&sql));
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3);
    int srcid = db_column_int(&q, 4);
    int isLink = db_column_int(&q, 5);
    char *zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    char *zToFree = zFullName;
    int showDiff = 1;
    if( isDeleted ){
      printf("DELETED  %s\n", zPathname);
      if( !asNewFile ){ showDiff = 0; zFullName = "/dev/null"; }
    }else if( access(zFullName, 0) ){
      printf("MISSING  %s\n", zPathname);
      if( !asNewFile ){ showDiff = 0; }
    }else if( isNew ){
      printf("ADDED    %s\n", zPathname);
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }else if( isChnged==3 ){
      printf("ADDED_BY_MERGE %s\n", zPathname);
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }
    if( showDiff ){
      if( !isLink != !file_islink(zFullName) ){
        diff_print_index(zPathname);
        printf("--- %s\n+++ %s\n", zPathname, zPathname);
        printf("cannot compute difference between symlink and regular file\n");
        continue;
      }
      Blob content;
      if( srcid>0 ){
        content_get(srcid, &content);
      }else{
        blob_zero(&content);
      }
      diff_print_index(zPathname);
      diff_file(&content, zFullName, zPathname, zDiffCmd, ignoreEolWs);
      blob_reset(&content);
    }
    free(zToFree);
  }
  db_finalize(&q);
  db_end_transaction(1);  /* ROLLBACK */
}

/*
** Output the differences between two versions of a single file.
** zFrom and zTo are the check-ins containing the two file versions.
** The filename is contained in g.argv[2].
*/
static void diff_one_two_versions(
  const char *zFrom,
  const char *zTo,
  const char *zDiffCmd,
  int ignoreEolWs
){
  char *zName;
  Blob fname;
  Blob v1, v2;
  int isLink1, isLink2;
  file_tree_name(g.argv[2], &fname, 1);
  zName = blob_str(&fname);
  historical_version_of_file(zFrom, zName, &v1, &isLink1, 0);
  historical_version_of_file(zTo, zName, &v2, &isLink2, 0);
  if( isLink1 != isLink2 ){
    printf("--- %s\n+++ %s\n", zName, zName);
    printf("cannot compute difference between symlink and regular file\n");
  }else{
    diff_file_mem(&v1, &v2, zName, zDiffCmd, ignoreEolWs);
  }
  blob_reset(&v1);
  blob_reset(&v2);
  blob_reset(&fname);
}

/*
** Show the difference between two files identified by ManifestFile
** entries.
*/
static void diff_manifest_entry(
  struct ManifestFile *pFrom,
  struct ManifestFile *pTo,
  const char *zDiffCmd,
  int ignoreEolWs
){
  Blob f1, f2;
  int rid;
  const char *zName =  pFrom ? pFrom->zName : pTo->zName;
  diff_print_index(zName);
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
  diff_file_mem(&f1, &f2, zName, zDiffCmd, ignoreEolWs);
  blob_reset(&f1);
  blob_reset(&f2);
}

/*
** Output the differences between two check-ins.
*/
static void diff_all_two_versions(
  const char *zFrom,
  const char *zTo,
  const char *zDiffCmd,
  int diffFlags
){
  Manifest *pFrom, *pTo;
  ManifestFile *pFromFile, *pToFile;
  int ignoreEolWs = (diffFlags & DIFF_NOEOLWS)!=0 ? 1 : 0;
  int asNewFlag = (diffFlags & DIFF_NEWFILE)!=0 ? 1 : 0;

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
      printf("DELETED %s\n", pFromFile->zName);
      if( asNewFlag ){
        diff_manifest_entry(pFromFile, 0, zDiffCmd, ignoreEolWs);
      }
      pFromFile = manifest_file_next(pFrom,0);
    }else if( cmp>0 ){
      printf("ADDED   %s\n", pToFile->zName);
      if( asNewFlag ){
        diff_manifest_entry(0, pToFile, zDiffCmd, ignoreEolWs);
      }
      pToFile = manifest_file_next(pTo,0);
    }else if( fossil_strcmp(pFromFile->zUuid, pToFile->zUuid)==0 ){
      /* No changes */
      pFromFile = manifest_file_next(pFrom,0);
      pToFile = manifest_file_next(pTo,0);
    }else{
      printf("CHANGED %s\n", pFromFile->zName);
      diff_manifest_entry(pFromFile, pToFile, zDiffCmd, ignoreEolWs);
      pFromFile = manifest_file_next(pFrom,0);
      pToFile = manifest_file_next(pTo,0);
    }
  }
  manifest_destroy(pFrom);
  manifest_destroy(pTo);
}

/*
** COMMAND: diff
** COMMAND: gdiff
**
** Usage: %fossil diff|gdiff ?options? ?FILE?
**
** Show the difference between the current version of FILE (as it
** exists on disk) and that same file as it was checked out.  Or
** if the FILE argument is omitted, show the unsaved changed currently
** in the working check-out.
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
** The "-i" command-line option forces the use of the internal diff logic
** rather than any external diff program that might be configured using
** the "setting" command.  If no external diff program is configured, then
** the "-i" option is a no-op.  The "-i" option converts "gdiff" into "diff".
**
** The "-N" or "--new-file" option causes the complete text of added or
** deleted files to be displayed.
*/
void diff_cmd(void){
  int isGDiff;               /* True for gdiff.  False for normal diff */
  int isInternDiff;          /* True for internal diff */
  int hasNFlag;              /* True if -N or --new-file flag is used */
  const char *zFrom;         /* Source version number */
  const char *zTo;           /* Target version number */
  const char *zDiffCmd = 0;  /* External diff command. NULL for internal diff */
  int diffFlags = 0;         /* Flags to control the DIFF */

  isGDiff = g.argv[1][0]=='g';
  isInternDiff = find_option("internal","i",0)!=0;
  zFrom = find_option("from", "r", 1);
  zTo = find_option("to", 0, 1);
  hasNFlag = find_option("new-file","N",0)!=0;


  if( hasNFlag ) diffFlags |= DIFF_NEWFILE;
  if( zTo==0 ){
    db_must_be_within_tree();
    verify_all_options();
    if( !isInternDiff ){
      zDiffCmd = db_get(isGDiff ? "gdiff-command" : "diff-command", 0);
    }
    if( g.argc==3 ){
      diff_one_against_disk(zFrom, zDiffCmd, 0);
    }else{
      diff_all_against_disk(zFrom, zDiffCmd, diffFlags);
    }
  }else if( zFrom==0 ){
    fossil_fatal("must use --from if --to is present");
  }else{
    db_find_and_open_repository(0, 0);
    verify_all_options();
    if( !isInternDiff ){
      zDiffCmd = db_get(isGDiff ? "gdiff-command" : "diff-command", 0);
    }
    if( g.argc==3 ){
      diff_one_two_versions(zFrom, zTo, zDiffCmd, 0);
    }else{
      diff_all_two_versions(zFrom, zTo, zDiffCmd, diffFlags);
    }
  }
}
