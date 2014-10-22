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
** Print the "Index:" message that patches wants to see at the top of a diff.
*/
void diff_print_index(const char *zFile, u64 diffFlags){
  if( (diffFlags & (DIFF_SIDEBYSIDE|DIFF_BRIEF))==0 ){
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
  u64 diffFlags             /* Flags to control the diff */
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
      if( file_wd_islink(zFile2) ){
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
      text_diff(pFile1, &file2, &out, 0, diffFlags);
      if( blob_size(&out) ){
        diff_print_filenames(zName, zName2, diffFlags);
        fossil_print("%s\n", blob_str(&out));
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
        if( file_wd_islink(zFile2) ){
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
    blob_appendf(&cmd, "%s ", zDiffCmd);
    shell_escape(&cmd, blob_str(&nameFile1));
    blob_append(&cmd, " ", 1);
    shell_escape(&cmd, zFile2);

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
    diff_print_filenames(zName, zName, diffFlags);
    fossil_print("%s\n", blob_str(&out));

    /* Release memory resources */
    blob_reset(&out);
  }else{
    Blob cmd;
    char zTemp1[300];
    char zTemp2[300];

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
    file_delete(zTemp1);
    file_delete(zTemp2);
    blob_reset(&cmd);
  }
}

/*
** Do a diff against a single file named in zFileTreeName from version zFrom
** against the same file on disk.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
static void diff_one_against_disk(
  const char *zFrom,        /* Name of file */
  const char *zDiffCmd,     /* Use this "diff" command */
  const char *zBinGlob,     /* Treat file names matching this as binary */
  int fIncludeBinary,       /* Include binary files for external diff */
  u64 diffFlags,            /* Diff control flags */
  const char *zFileTreeName
){
  Blob fname;
  Blob content;
  int isLink;
  int isBin;
  file_tree_name(zFileTreeName, &fname, 1);
  historical_version_of_file(zFrom, blob_str(&fname), &content, &isLink, 0,
                             fIncludeBinary ? 0 : &isBin, 0);
  if( !isLink != !file_wd_islink(zFrom) ){
    fossil_print("%s",DIFF_CANNOT_COMPUTE_SYMLINK);
  }else{
    diff_file(&content, isBin, zFileTreeName, zFileTreeName,
              zDiffCmd, zBinGlob, fIncludeBinary, diffFlags);
  }
  blob_reset(&content);
  blob_reset(&fname);
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
static void diff_all_against_disk(
  const char *zFrom,        /* Version to difference from */
  const char *zDiffCmd,     /* Use this diff command.  NULL for built-in */
  const char *zBinGlob,     /* Treat file names matching this as binary */
  int fIncludeBinary,       /* Treat file names matching this as binary */
  u64 diffFlags             /* Flags controlling diff output */
){
  int vid;
  Blob sql;
  Stmt q;
  int asNewFile;            /* Treat non-existant files as empty files */

  asNewFile = (diffFlags & DIFF_VERBOSE)!=0;
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
      " ORDER BY 1",
      rid, vid, rid, vid, vid, rid
    );
  }else{
    blob_append_sql(&sql,
      "SELECT pathname, deleted, chnged , rid==0, rid, islink"
      "  FROM vfile"
      " WHERE vid=%d"
      "   AND (deleted OR chnged OR rid==0)"
      " ORDER BY pathname",
      vid
    );
  }
  db_prepare(&q, "%s", blob_sql_text(&sql));
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3);
    int srcid = db_column_int(&q, 4);
    int isLink = db_column_int(&q, 5);
    char *zToFree = mprintf("%s%s", g.zLocalRoot, zPathname);
    const char *zFullName = zToFree;
    int showDiff = 1;
    if( isDeleted ){
      fossil_print("DELETED  %s\n", zPathname);
      if( !asNewFile ){ showDiff = 0; zFullName = NULL_DEVICE; }
    }else if( file_access(zFullName, F_OK) ){
      fossil_print("MISSING  %s\n", zPathname);
      if( !asNewFile ){ showDiff = 0; }
    }else if( isNew ){
      fossil_print("ADDED    %s\n", zPathname);
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }else if( isChnged==3 ){
      fossil_print("ADDED_BY_MERGE %s\n", zPathname);
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
                zBinGlob, fIncludeBinary, diffFlags);
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
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
static void diff_one_two_versions(
  const char *zFrom,
  const char *zTo,
  const char *zDiffCmd,
  const char *zBinGlob,
  int fIncludeBinary,
  u64 diffFlags,
  const char *zFileTreeName
){
  char *zName;
  Blob fname;
  Blob v1, v2;
  int isLink1, isLink2;
  int isBin1, isBin2;
  if( diffFlags & DIFF_BRIEF ) return;
  file_tree_name(zFileTreeName, &fname, 1);
  zName = blob_str(&fname);
  historical_version_of_file(zFrom, zName, &v1, &isLink1, 0,
                             fIncludeBinary ? 0 : &isBin1, 0);
  historical_version_of_file(zTo, zName, &v2, &isLink2, 0,
                             fIncludeBinary ? 0 : &isBin2, 0);
  if( isLink1 != isLink2 ){
    diff_print_filenames(zName, zName, diffFlags);
    fossil_print("%s",DIFF_CANNOT_COMPUTE_SYMLINK);
  }else{
    diff_file_mem(&v1, &v2, isBin1, isBin2, zName, zDiffCmd,
                  zBinGlob, fIncludeBinary, diffFlags);
  }
  blob_reset(&v1);
  blob_reset(&v2);
  blob_reset(&fname);
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
static void diff_all_two_versions(
  const char *zFrom,
  const char *zTo,
  const char *zDiffCmd,
  const char *zBinGlob,
  int fIncludeBinary,
  u64 diffFlags
){
  Manifest *pFrom, *pTo;
  ManifestFile *pFromFile, *pToFile;
  int asNewFlag = (diffFlags & DIFF_VERBOSE)!=0 ? 1 : 0;

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
      fossil_print("DELETED %s\n", pFromFile->zName);
      if( asNewFlag ){
        diff_manifest_entry(pFromFile, 0, zDiffCmd, zBinGlob,
                            fIncludeBinary, diffFlags);
      }
      pFromFile = manifest_file_next(pFrom,0);
    }else if( cmp>0 ){
      fossil_print("ADDED   %s\n", pToFile->zName);
      if( asNewFlag ){
        diff_manifest_entry(0, pToFile, zDiffCmd, zBinGlob,
                            fIncludeBinary, diffFlags);
      }
      pToFile = manifest_file_next(pTo,0);
    }else if( fossil_strcmp(pFromFile->zUuid, pToFile->zUuid)==0 ){
      /* No changes */
      pFromFile = manifest_file_next(pFrom,0);
      pToFile = manifest_file_next(pTo,0);
    }else{
      if( diffFlags & DIFF_BRIEF ){
        fossil_print("CHANGED %s\n", pFromFile->zName);
      }else{
        diff_manifest_entry(pFromFile, pToFile, zDiffCmd, zBinGlob,
                            fIncludeBinary, diffFlags);
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
  char *zDefault;
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

/* A Tcl/Tk script used to render diff output.
*/
static const char zDiffScript[] =
@ set prog {
@ package require Tk
@
@ array set CFG {
@   TITLE      {Fossil Diff}
@   LN_COL_BG  #dddddd
@   LN_COL_FG  #444444
@   TXT_COL_BG #ffffff
@   TXT_COL_FG #000000
@   MKR_COL_BG #444444
@   MKR_COL_FG #dddddd
@   CHNG_BG    #d0d0ff
@   ADD_BG     #c0ffc0
@   RM_BG      #ffc0c0
@   HR_FG      #888888
@   HR_PAD_TOP 4
@   HR_PAD_BTM 8
@   FN_BG      #444444
@   FN_FG      #ffffff
@   FN_PAD     5
@   ERR_FG     #ee0000
@   PADX       5
@   WIDTH      80
@   HEIGHT     45
@   LB_HEIGHT  25
@ }
@
@ if {![namespace exists ttk]} {
@   interp alias {} ::ttk::scrollbar {} ::scrollbar
@   interp alias {} ::ttk::menubutton {} ::menubutton
@ }
@
@ proc dehtml {x} {
@   set x [regsub -all {<[^>]*>} $x {}]
@   return [string map {&amp; & &lt; < &gt; > &#39; ' &quot; \"} $x]
@ }
@
@ proc cols {} {
@   return [list .lnA .txtA .mkr .lnB .txtB]
@ }
@
@ proc colType {c} {
@   regexp {[a-z]+} $c type
@   return $type
@ }
@
@ proc getLine {difftxt N iivar} {
@   upvar $iivar ii
@   if {$ii>=$N} {return -1}
@   set x [lindex $difftxt $ii]
@   incr ii
@   return $x
@ }
@
@ proc readDiffs {fossilcmd} {
@   global difftxt
@   if {![info exists difftxt]} {
@     set in [open $fossilcmd r]
@     fconfigure $in -encoding utf-8
@     set difftxt [split [read $in] \n]
@     close $in
@   }
@   set N [llength $difftxt]
@   set ii 0
@   set nDiffs 0
@   array set widths {txt 0 ln 0 mkr 0}
@   while {[set line [getLine $difftxt $N ii]] != -1} {
@     set fn2 {}
@     if {![regexp {^=+ (.*?) =+ versus =+ (.*?) =+$} $line all fn fn2]
@      && ![regexp {^=+ (.*?) =+$} $line all fn]
@     } {
@       continue
@     }
@     set errMsg ""
@     set line [getLine $difftxt $N ii]
@     if {[string compare -length 6 $line "<table"]
@      && ![regexp {<p[^>]*>(.+)} $line - errMsg]} {
@       continue
@     }
@     incr nDiffs
@     set idx [expr {$nDiffs > 1 ? [.txtA index end] : "1.0"}]
@     .wfiles.lb insert end $fn
@
@     foreach c [cols] {
@       if {$nDiffs > 1} {
@         $c insert end \n -
@       }
@       if {[colType $c] eq "txt"} {
@         $c insert end $fn\n fn
@         if {$fn2!=""} {set fn $fn2}
@       } else {
@         $c insert end \n fn
@       }
@       $c insert end \n -
@
@       if {$errMsg ne ""} continue
@       while {[getLine $difftxt $N ii] ne "<pre>"} continue
@       set type [colType $c]
@       set str {}
@       while {[set line [getLine $difftxt $N ii]] ne "</pre>"} {
@         set len [string length [dehtml $line]]
@         if {$len > $widths($type)} {
@           set widths($type) $len
@         }
@         append str $line\n
@       }
@
@       set re {<span class="diff([a-z]+)">([^<]*)</span>}
@       # Use \r as separator since it can't appear in the diff output (it gets
@       # converted to a space).
@       set str [regsub -all $re $str "\r\\1\r\\2\r"]
@       foreach {pre class mid} [split $str \r] {
@         if {$class ne ""} {
@           $c insert end [dehtml $pre] - [dehtml $mid] [list $class -]
@         } else {
@           $c insert end [dehtml $pre] -
@         }
@       }
@     }
@
@     if {$errMsg ne ""} {
@       foreach c {.txtA .txtB} {$c insert end [string trim $errMsg] err}
@       foreach c [cols] {$c insert end \n -}
@     }
@   }
@
@   foreach c [cols] {
@     set type [colType $c]
@     if {$type ne "txt"} {
@       $c config -width $widths($type)
@     }
@     $c config -state disabled
@   }
@   if {$nDiffs <= [.wfiles.lb cget -height]} {
@     .wfiles.lb config -height $nDiffs
@     grid remove .wfiles.sb
@   }
@
@   return $nDiffs
@ }
@
@ proc viewDiff {idx} {
@   .txtA yview $idx
@   .txtA xview moveto 0
@ }
@
@ proc cycleDiffs {{reverse 0}} {
@   if {$reverse} {
@     set range [.txtA tag prevrange fn @0,0 1.0]
@     if {$range eq ""} {
@       viewDiff {fn.last -1c}
@     } else {
@       viewDiff [lindex $range 0]
@     }
@   } else {
@     set range [.txtA tag nextrange fn {@0,0 +1c} end]
@     if {$range eq "" || [lindex [.txtA yview] 1] == 1} {
@       viewDiff fn.first
@     } else {
@       viewDiff [lindex $range 0]
@     }
@   }
@ }
@
@ proc prev_next_diff { prev_next } {
@   set range [.txtA tag nextrange active 1.0 end]
@   if {$prev_next eq "prev"} {
@     set idx0 [lindex $range 0]
@     if {$idx0 eq ""} {set idx0 end}
@     if {[.txtA compare $idx0 > @0,[winfo height .txtA]]} {
@       set idx0 [.txtA index @0,[winfo height .txtA]]
@     }
@     set idx ""
@     foreach tag [list add rm chng fn] {
@       foreach w [list  .txtA .txtB] {
@         lassign [$w tag prevrange $tag $idx0 1.0] a b
@         if { $idx eq "" || ($a ne "" && [$w compare $a > $idx]) } {
@           set idx $a
@           set idx_end $b
@           set tagB $tag
@           set wB $w
@         }
@       }
@     }
@     if {$idx ne ""} {
@       while 1 {
@         lassign [$wB tag prevrange $tagB $idx 1.0] a b
@         if {$b ne "" && [$wB compare $b == "$idx - 1 l lineend"]} {
@           set idx $a
@         } else {
@           break
@         }
@       }
@     }
@   } else {
@     set idx0 [lindex $range 1]
@     if { $idx0 eq "" } { set idx0 1.0 }
@     if { [.txtA compare $idx0 < @0,0] } {
@       set idx0 [.txtA index @0,0]
@     }
@     set idx ""
@     foreach tag [list add rm chng fn] {
@       foreach w [list  .txtA .txtB] {
@         lassign [$w tag nextrange $tag $idx0 end] a b
@         if { $idx eq "" || ($a ne "" && [$w compare $a < $idx]) } {
@           set idx $a
@           set idx_end $b
@           set tagB $tag
@           set wB $w
@         }
@       }
@     }
@     if { $idx ne "" } {
@       while 1 {
@         lassign [$wB tag nextrange $tagB $idx_end end] a b
@         if { $a ne "" && [$wB compare $a == "$idx_end + 1 l linestart"] } {
@           set idx_end $b
@         } else {
@           break
@         }
@       }
@     }
@   }
@   if { $idx eq "" } {
@     bell
@     return
@   }
@   set idx [.txtA index "$idx linestart"]
@   if { $tagB ne "fn" } {
@     set idx_end [.txtA index "$idx_end +1l linestart"]
@   }
@   .txtA tag remove active 1.0 end
@   .txtA tag add active $idx $idx_end
@   .txtA tag configure active -borderwidth 2 -relief raised\
@                      -background #eeeeee -foreground black
@   if { $tagB ne "fn" } {
@     .txtA tag lower active
@   } else {
@     .txtA tag raise active
@   }
@   .txtA see 1.0
@   .txtA see $idx
@ }
@
@ proc searchText {} {
@   set rangeA [.txtA tag nextrange search 1.0 end]
@   set rangeB [.txtB tag nextrange search 1.0 end]
@   set idx0 [lindex $rangeA 1]
@   if { $idx0 eq "" } { set idx0 [lindex $rangeB 1] }
@   if { $idx0 eq "" } { set idx0 1.0 }
@   set word [.bb.search get]
@   if { [.txtA compare $idx0 < @0,0] } {
@     set idx0 [.txtA index @0,0]
@   }
@   if { [info exists ::this_does_not_find] } {
@     if { $::this_does_not_find eq  [list $idx0 $word] } {
@       set idx0 1.0
@     }
@     unset ::this_does_not_find
@   }
@   set idx ""
@   foreach w [list  .txtA .txtB] {
@     foreach regexp [list 0 1] {
@       switch $regexp {
@         0 { set rexFlag "-exact" }
@         1 { set rexFlag "-regexp" }
@       }
@       set err [catch {
@         $w search -nocase $rexFlag -count count $word $idx0 end 
@       } idx_i]
@       if {!$err && $idx_i ne ""
@            && ($idx eq "" || [$w compare $idx_i < $idx])} {
@         set idx $idx_i
@         set countB $count
@         set wB $w
@       }
@     }
@   }
@   .txtA  tag remove search 1.0 end
@   .txtB  tag remove search 1.0 end
@   if { $idx eq "" } {
@     bell
@     set ::this_does_not_find [list $idx0 $word]
@     return
@   }
@   set idx_end [$wB index "$idx + $countB c"]
@   $wB tag add search $idx $idx_end
@   $wB tag configure search -borderwidth 2 -relief raised\
@                     -background orange -foreground black
@   $wB tag raise search
@   $wB see 1.0
@   $wB see $idx
@ }
@ 
@ proc reopen { action } {
@   if { ![regexp {[|]\s*(.*)} $::fossilcmd {} cmdList] } { return }
@   set f [lindex $cmdList 0]
@   set args_with_arg \
@      [list binary branch context c diff-binary from r to W width]
@   set skip_args [list html internal i side-by-side y tk]
@   lassign "" argsDict files
@   for { set i 2 } { $i < [llength $cmdList] } { incr i } {
@     if { [string match "-*" [lindex $cmdList $i]] } {
@       set n [string trimleft [lindex $cmdList $i] "-"]
@       if { $n in $args_with_arg } {
@         dict set argsDict $n [lindex $cmdList $i+1]
@         incr i
@       } elseif { $n ni $skip_args } {
@         dict set argsDict $n 1
@       }
@     } else {
@       lappend files [lindex $cmdList $i]
@     }
@   }
@   switch $action {
@     togglewhitespace {
@       if { [dict exists $argsDict w]
@            || [dict exists $argsDict ignore-all-space] } {
@         dict unset argsDict w
@         dict unset argsDict ignore-all-space
@       } else {
@         dict set argsDict w 1
@       }
@     }
@     onefile {
@       set range [.txtA tag nextrange fn "@0,0" "@0,[winfo height .txtA] +1l"]
@       if { $range eq "" } { return }
@       set file [string trim [.txtA get {*}$range]]
@       set files [list $file]
@       regexp -line {local-root:\s+(.*)} [exec $f info] {} dir
@       cd $dir
@     }
@     allfiles {
@       set files ""
@     }
@     prev -
@     next {
@       set widget [focus]
@       if { $widget eq ".txtA" } {
@         set from_to from
@         if { ![dict exists $argsDict from] } {
@           dict set argsDict from current
@         }
@       } elseif { $widget eq ".txtB" } {
@         set from_to to
@         if { ![dict exists $argsDict to] } {
@           dict set argsDict to ckout
@         }
@       } else {
@         tk_messageBox -message "Click on one of the panes to select it"
@         return
@       }
@       lassign "" parent child current tag
@       set err [catch { exec $f info [dict get $argsDict $from_to] } info]
@       if { $err } {
@         if { [dict get $argsDict $from_to] eq "ckout" } {
@           set err [catch { exec $f info } info]
@           if { !$err } { regexp {checkout:\s+(\S+)} $info {} parent }
@         } else {
@           bell
@           return
@         }
@       } else {
@         regexp {uuid:\s+(\S+)\s+(\S+)} $info {} current date
@         regexp {parent:\s+(\S+)} $info {} parent
@         regexp {child:\s+(\S+)} $info {} child
@       }
@       if { [llength $files] == 1 } {
@         set file [lindex $files 0]
@         set err [catch { exec $f finfo -b -limit 100 $file } info]
@         if { $err } {
@           bell
@           return
@         }
@         if { $current eq "" } {
@           if { $action eq "prev" } {
@             regexp {^\S+} $info tag
@           }
@         } else {
@           set current [string range $current 0 9]
@           set prev ""
@           set found 0
@           foreach line [split $info \n] {
@             regexp {(\S+)\s+(\S+)} $line {} currentL dateL
@             if { $found } {
@               set tag $currentL
@               break
@             } elseif { $currentL eq $current || $dateL < $date } {
@               if { $action eq "next" } {
@                 set tag $prev
@                 break
@               }
@               set found 1
@             }
@             set prev $currentL  
@           }
@         }
@       } else {
@         if { $action eq "prev" } {
@           set tag $parent
@         } else {
@           set tag $child
@         }
@       }
@       if { $tag eq "" && $action eq "prev" } {
@         bell
@         return
@       }
@       if { $tag ne "" } {
@         dict set argsDict $from_to $tag
@       } else {
@         dict unset argsDict $from_to
@       }
@       if { $from_to eq "to" && ![dict exists $argsDict from] } {
@         dict set argsDict from current
@       }
@     }
@   }
@
@   set f_args ""
@   dict for "n v" $argsDict {
@     if { $n in $args_with_arg } {
@       lappend f_args -$n $v
@     } else {
@       lappend f_args -$n
@     }
@   }
@   lappend f_args {*}$files
@
@   # note: trying to put two contiguous "-" gives an error
@   exec $f diff -tk {*}$f_args &
@   exit        
@ }
@
@ proc fossil_ui {} {
@   if { ![regexp {[|]\s*(.*)} $::fossilcmd {} cmdList] } { return }
@   set f [lindex $cmdList 0]
@   exec $f ui &
@ }
@
@ proc searchToggle {} {
@   set err [catch { pack info .bb.search }]
@   if { $err } {
@     pack .bb.search -side left -padx 5 -after .bb.files
@     tk::TabToWindow .bb.search
@   } else {
@     .txtA  tag remove search 1.0 end
@     .txtB  tag remove search 1.0 end
@     pack  forget .bb.search
@     focus .
@   }
@ }
@
@ proc xvis {col} {
@   set view [$col xview]
@   return [expr {[lindex $view 1]-[lindex $view 0]}]
@ }
@
@ proc scroll-x {args} {
@   set c .txt[expr {[xvis .txtA] < [xvis .txtB] ? "A" : "B"}]
@   eval $c xview $args
@ }
@
@ interp alias {} scroll-y {} .txtA yview
@
@ proc noop {args} {}
@
@ proc enableSync {axis} {
@   update idletasks
@   interp alias {} sync-$axis {}
@   rename _sync-$axis sync-$axis
@ }
@
@ proc disableSync {axis} {
@   rename sync-$axis _sync-$axis
@   interp alias {} sync-$axis {} noop
@ }
@
@ proc sync-x {col first last} {
@   disableSync x
@   $col xview moveto [expr {$first*[xvis $col]/($last-$first)}]
@   foreach side {A B} {
@     set sb .sbx$side
@     set xview [.txt$side xview]
@     if {[lindex $xview 0] > 0 || [lindex $xview 1] < 1} {
@       grid $sb
@       eval $sb set $xview
@     } else {
@       grid remove $sb
@     }
@   }
@   enableSync x
@ }
@
@ proc sync-y {first last} {
@   disableSync y
@   foreach c [cols] {
@     $c yview moveto $first
@   }
@   if {$first > 0 || $last < 1} {
@     grid .sby
@     .sby set $first $last
@   } else {
@     grid remove .sby
@   }
@   enableSync y
@ }
@
@ wm withdraw .
@ wm title . $CFG(TITLE)
@ wm iconname . $CFG(TITLE)
@ bind . <q> exit
@ bind . <Destroy> {after 0 exit}
@ bind . <Tab> {cycleDiffs; break}
@ bind . <<PrevWindow>> {cycleDiffs 1; break}
@ bind . <Return> {
@   event generate .bb.files <1>
@   event generate .bb.files <ButtonRelease-1>
@   break
@ }
@ foreach {key axis args} {
@   Up    y {scroll -5 units}
@   Down  y {scroll 5 units}
@   Left  x {scroll -5 units}
@   Right x {scroll 5 units}
@   Prior y {scroll -1 page}
@   Next  y {scroll 1 page}
@   Home  y {moveto 0}
@   End   y {moveto 1}
@ } {
@   bind . <$key> "scroll-$axis $args; break"
@   bind . <Shift-$key> continue
@ }
@
@ frame .bb
@ ::ttk::menubutton .bb.files -text "Files"
@ toplevel .wfiles
@ wm withdraw .wfiles
@ update idletasks
@ wm transient .wfiles .
@ wm overrideredirect .wfiles 1
@ listbox .wfiles.lb -width 0 -height $CFG(LB_HEIGHT) -activestyle none \
@   -yscroll {.wfiles.sb set}
@ ::ttk::scrollbar .wfiles.sb -command {.wfiles.lb yview}
@ grid .wfiles.lb .wfiles.sb -sticky ns
@ bind .bb.files <1> {
@   set x [winfo rootx %W]
@   set y [expr {[winfo rooty %W]+[winfo height %W]}]
@   wm geometry .wfiles +$x+$y
@   wm deiconify .wfiles
@   focus .wfiles.lb
@ }
@ bind .wfiles <FocusOut> {wm withdraw .wfiles}
@ bind .wfiles <Escape> {focus .}
@ foreach evt {1 Return} {
@   bind .wfiles.lb <$evt> {
@     catch {
@       set idx [lindex [.txtA tag ranges fn] [expr {[%W curselection]*2}]]
@       viewDiff $idx
@     }
@     focus .
@     break
@   }
@ }
@ bind .wfiles.lb <Motion> {
@   %W selection clear 0 end
@   %W selection set @%x,%y
@ }
@
@ foreach {side syncCol} {A .txtB B .txtA} {
@   set ln .ln$side
@   text $ln
@   $ln tag config - -justify right
@
@   set txt .txt$side
@   text $txt -width $CFG(WIDTH) -height $CFG(HEIGHT) -wrap none \
@     -xscroll "sync-x $syncCol"
@   catch {$txt config -tabstyle wordprocessor} ;# Required for Tk>=8.5
@   foreach tag {add rm chng} {
@     $txt tag config $tag -background $CFG([string toupper $tag]_BG)
@     $txt tag lower $tag
@   }
@   $txt tag config fn -background $CFG(FN_BG) -foreground $CFG(FN_FG) \
@     -justify center
@   $txt tag config err -foreground $CFG(ERR_FG)
@ }
@ text .mkr
@
@ foreach c [cols] {
@   set keyPrefix [string toupper [colType $c]]_COL_
@   if {[tk windowingsystem] eq "win32"} {$c config -font {courier 9}}
@   $c config -bg $CFG(${keyPrefix}BG) -fg $CFG(${keyPrefix}FG) -borderwidth 0 \
@     -padx $CFG(PADX) -yscroll sync-y
@   $c tag config hr -spacing1 $CFG(HR_PAD_TOP) -spacing3 $CFG(HR_PAD_BTM) \
@      -foreground $CFG(HR_FG)
@   $c tag config fn -spacing1 $CFG(FN_PAD) -spacing3 $CFG(FN_PAD)
@   bindtags $c ". $c Text all"
@   bind $c <1> {focus %W}
@ }
@
@ ::ttk::scrollbar .sby -command {.txtA yview} -orient vertical
@ ::ttk::scrollbar .sbxA -command {.txtA xview} -orient horizontal
@ ::ttk::scrollbar .sbxB -command {.txtB xview} -orient horizontal
@ frame .spacer
@
@ if {[readDiffs $fossilcmd] == 0} {
@   tk_messageBox -type ok -title $CFG(TITLE) -message "No changes"
@   #exit
@ }
@ update idletasks
@
@ proc saveDiff {} {
@   set fn [tk_getSaveFile]
@   if {$fn==""} return
@   set out [open $fn wb]
@   puts $out "#!/usr/bin/tclsh\n#\n# Run this script using 'tclsh' or 'wish'"
@   puts $out "# to see the graphical diff.\n#"
@   puts $out "set fossilcmd {}"
@   puts $out "set prog [list $::prog]"
@   puts $out "set difftxt \173"
@   foreach e $::difftxt {puts $out [list $e]}
@   puts $out "\175"
@   puts $out "eval \$prog"
@   close $out
@ }
@ proc invertDiff {} {
@   global CFG
@   array set x [grid info .txtA]
@   if {$x(-column)==1} {
@     grid config .lnB -column 0
@     grid config .txtB -column 1
@     .txtB tag config add -background $CFG(RM_BG)
@     grid config .lnA -column 3
@     grid config .txtA -column 4
@     .txtA tag config rm -background $CFG(ADD_BG)
@   } else {
@     grid config .lnA -column 0
@     grid config .txtA -column 1
@     .txtA tag config rm -background $CFG(RM_BG)
@     grid config .lnB -column 3
@     grid config .txtB -column 4
@     .txtB tag config add -background $CFG(ADD_BG)
@   }
@   .mkr config -state normal
@   set clt [.mkr search -all < 1.0 end]
@   set cgt [.mkr search -all > 1.0 end]
@   foreach c $clt {.mkr replace $c "$c +1 chars" >}
@   foreach c $cgt {.mkr replace $c "$c +1 chars" <}
@   .mkr config -state disabled
@ }
@ proc bind_key_do { cmd } {
@   if { [focus] eq ".bb.search" } { return -code continue }
@   uplevel #0 $cmd
@   return -code break
@ }
@ ::ttk::menubutton .bb.actions -text "Actions" -menu  .bb.actions.m
@ menu .bb.actions.m -tearoff 0
@ .bb.actions.m add command -label "Go to previous diff" -acc "p" -command "prev_next_diff prev"
@ .bb.actions.m add command -label "Go to next diff" -acc "n" -command "prev_next_diff next"
@ .bb.actions.m add separator
@ .bb.actions.m add command -label "Search" -acc "f" -command "searchToggle;"
@ .bb.actions.m add command -label "Toggle whitespace" -acc "w" -command "reopen togglewhitespace"
@ .bb.actions.m add separator
@ .bb.actions.m add command -label "View one file" -acc "1" -command "reopen onefile"
@ .bb.actions.m add command -label "View all files" -acc "a" -command "reopen allfiles"
@ .bb.actions.m add separator
@ .bb.actions.m add command -label "Older version" -acc "Shift-P" -command "reopen prev"
@ .bb.actions.m add command -label "Newer version" -acc "Shift-N" -command "reopen next"
@ .bb.actions.m add command -label "Fossil ui" -acc "u" -command "fossil_ui"
@ ::ttk::button .bb.quit -text {Quit} -command exit
@ ::ttk::button .bb.invert -text {Invert} -command invertDiff
@ ::ttk::button .bb.save -text {Save As...} -command saveDiff
@ ::ttk::entry .bb.search -width 12
@ 
@ bind  .bb.search <Return> "searchText; break"
@ bind  .bb.search <Escape> "searchToggle; break"
@
@ bind  . <Key-f> [list bind_key_do "searchToggle"]
@ bind  . <Key-w> [list bind_key_do "reopen togglewhitespace"]
@ bind  . <Key-1> [list bind_key_do "reopen onefile"]
@ bind  . <Key-a> [list bind_key_do "reopen allfiles"]
@ bind  . <Key-P> [list bind_key_do "reopen prev"]
@ bind  . <Key-N> [list bind_key_do "reopen next"]  
@ bind  . <Key-u> [list bind_key_do "fossil_ui"]  
@
@ lassign [list "(current)" "(ckout)"] from to
@ if { [regexp {[|]\s*(.*)} $::fossilcmd {} cmdList] } {
@   set f [lindex $cmdList 0]
@   if { [regexp {([-][-]?from|-r)\s+(\S+)} [join $cmdList " "] {} {} from] } {
@     set err [catch { exec $f info $from } info]
@     if { !$err } {
@       regexp {uuid:\s+(\S+)\s+(\S+)\s+(\S+)} $info {} from date time
@       set from "\[[string range $from 0 9]\] $date $time"
@     }
@   }
@   if { [regexp {([-][-]?to)\s+(\S+)} [join $cmdList " "] {} {} to] } {
@     set err [catch { exec $f info $to } info]
@     if { !$err } {
@       regexp {uuid:\s+(\S+)\s+(\S+)\s+(\S+)} $info {} to date time
@       set to "\[[string range $to 0 9]\] $date $time"
@     }
@   }
@ }
@    
@ ttk::label .bb.from -text $from
@ ttk::label .bb.to -text $to
@    
@ pack .bb.from -side left -padx "2 25"
@ pack .bb.quit .bb.invert -side left
@ if {$fossilcmd!=""} {pack .bb.save -side left}
@ pack .bb.files -side left
@ pack .bb.actions -side left
@ pack .bb.to -side left -padx "25 2"
@ grid rowconfigure . 1 -weight 1
@ grid columnconfigure . 1 -weight 1
@ grid columnconfigure . 4 -weight 1
@ grid .bb -row 0 -columnspan 7
@ eval grid [cols] -row 1 -sticky nsew
@ grid .sby -row 1 -column 5 -sticky ns
@ grid .sbxA -row 2 -columnspan 2 -sticky ew
@ grid .spacer -row 2 -column 2
@ grid .sbxB -row 2 -column 3 -columnspan 2 -sticky ew
@
@ .spacer config -height [winfo height .sbxA]
@ wm deiconify .
@ }
@ eval $prog
;

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
  blob_appendf(&script, "}\n%s", zDiffScript);
  if( zTempFile ){
    blob_write_to_file(&script, zTempFile);
    fossil_print("To see diff, run: tclsh \"%s\"\n", zTempFile);
  }else{
#if defined(FOSSIL_ENABLE_TCL)
    Th_FossilInit(TH_INIT_DEFAULT);
    if( evaluateTclWithEvents(g.interp, &g.tcl, blob_str(&script),
                              blob_size(&script), 1, 0)==TCL_OK ){
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
  if( is_truth(find_option("diff-binary", 0, 1)) ){
    return 1;
  }
  if( db_get_boolean("diff-binary", 1) ){
    return 1;
  }
  return 0;
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
** out.  Or if the FILE arguments are omitted, show the unsaved changed
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
**   --context|-c N             Use N lines of context
**   --diff-binary BOOL         Include binary files when using external commands
**   --from|-r VERSION          select VERSION as source for the diff
**   --internal|-i              use internal diff logic
**   --side-by-side|-y          side-by-side diff
**   --strip-trailing-cr        Strip trailing CR
**   --tk                       Launch a Tcl/Tk GUI for display
**   --to VERSION               select VERSION as target for the diff
**   --unified                  unified diff
**   -v|--verbose               output complete text of added or deleted files
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
  const char *zBranch;       /* Branch to diff */
  const char *zDiffCmd = 0;  /* External diff command. NULL for internal diff */
  const char *zBinGlob = 0;  /* Treat file names matching this as binary */
  int fIncludeBinary = 0;    /* Include binary files for external diff */
  u64 diffFlags = 0;         /* Flags to control the DIFF */
  int f;

  if( find_option("tk",0,0)!=0 ){
    diff_tk("diff", 2);
    return;
  }
  isGDiff = g.argv[1][0]=='g';
  isInternDiff = find_option("internal","i",0)!=0;
  zFrom = find_option("from", "r", 1);
  zTo = find_option("to", 0, 1);
  zBranch = find_option("branch", 0, 1);
  diffFlags = diff_options();
  verboseFlag = find_option("verbose","v",0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("new-file","N",0)!=0; /* deprecated */
  }
  if( verboseFlag ) diffFlags |= DIFF_VERBOSE;
  if( zBranch ){
    if( zTo || zFrom ){
      fossil_fatal("cannot use --from or --to with --branch");
    }
    zTo = zBranch;
    zFrom = mprintf("root:%s", zBranch);
  }
  if( zTo==0 ){
    db_must_be_within_tree();
    if( !isInternDiff ){
      zDiffCmd = diff_command_external(isGDiff);
    }
    zBinGlob = diff_get_binary_glob();
    fIncludeBinary = diff_include_binary_files();
    verify_all_options();
    if( g.argc>=3 ){
      for(f=2; f<g.argc; ++f){
        diff_one_against_disk(zFrom, zDiffCmd, zBinGlob, fIncludeBinary,
                              diffFlags, g.argv[f]);
      }
    }else{
      diff_all_against_disk(zFrom, zDiffCmd, zBinGlob, fIncludeBinary,
                            diffFlags);
    }
  }else if( zFrom==0 ){
    fossil_fatal("must use --from if --to is present");
  }else{
    db_find_and_open_repository(0, 0);
    if( !isInternDiff ){
      zDiffCmd = diff_command_external(isGDiff);
    }
    zBinGlob = diff_get_binary_glob();
    fIncludeBinary = diff_include_binary_files();
    verify_all_options();
    if( g.argc>=3 ){
      for(f=2; f<g.argc; ++f){
        diff_one_two_versions(zFrom, zTo, zDiffCmd, zBinGlob, fIncludeBinary,
                              diffFlags, g.argv[f]);
      }
    }else{
      diff_all_two_versions(zFrom, zTo, zDiffCmd, zBinGlob, fIncludeBinary,
                            diffFlags);
    }
  }
}

/*
** WEBPAGE: vpatch
** URL vpatch?from=UUID&to=UUID
*/
void vpatch_page(void){
  const char *zFrom = P("from");
  const char *zTo = P("to");
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }
  if( zFrom==0 || zTo==0 ) fossil_redirect_home();

  cgi_set_content_type("text/plain");
  diff_all_two_versions(zFrom, zTo, 0, 0, 0, DIFF_VERBOSE);
}
