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
    int x;
    if( n1>w*2 ) n1 = w*2;
    x = w*2+17 - (n1+2);
    z = mprintf("%.*c %.*s %.*c\n",
                x/2, '=', n1, zLeft, (x+1)/2, '=');
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
  int eType1,               /* Does the 'from' content appear to be text */
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
    int eType2 = 0;

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
    if( !fIncludeBinary ){
      looks_like_text(eType2, &file2);
    }
    /* Compute and output the differences */
    if( diffFlags & DIFF_BRIEF ){
      if( blob_compare(pFile1, &file2) ){
        fossil_print("CHANGED  %s\n", zName);
      }
    }else if( eType1!=eType2 ){
      fossil_print(DIFF_CANNOT_COMPUTE_ENCODING);
    }else{
      blob_to_utf8_no_bom(pFile1, 2);
      blob_to_utf8_no_bom(&file2, 2);
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
      int eType2;
      if( eType1!=1 ){
        fossil_print(DIFF_CANNOT_COMPUTE_BINARY);
        return;
      }
      if( zBinGlob ){
        Glob *pBinary = glob_create(zBinGlob);
        if( glob_match(pBinary, zName) ){
          fossil_print(DIFF_CANNOT_COMPUTE_BINARY);
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
      looks_like_text(eType2, &file2);
      if( (eType2&3)!=1 ){
        fossil_print(DIFF_CANNOT_COMPUTE_BINARY);
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
    }while( file_access(blob_str(&nameFile1),0)==0 );
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
  int eType,                /* Does the content appear to be text */
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
    blob_to_utf8_no_bom(pFile1, 2);
    blob_to_utf8_no_bom(pFile2, 2);
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
      if( eType==0 ){
        fossil_print(DIFF_CANNOT_COMPUTE_BINARY);
        return;
      }
      if( zBinGlob ){
        Glob *pBinary = glob_create(zBinGlob);
        if( glob_match(pBinary, zName) ){
          fossil_print(DIFF_CANNOT_COMPUTE_BINARY);
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
  int eType = 0;
  file_tree_name(zFileTreeName, &fname, 1);
  historical_version_of_file(zFrom, blob_str(&fname), &content, &isLink, 0,
                             fIncludeBinary ? 0 : &eType, 0);
  if( !isLink != !file_wd_islink(zFrom) ){
    fossil_print(DIFF_CANNOT_COMPUTE_SYMLINK);
  }else{
    diff_file(&content, eType, zFileTreeName, zFileTreeName,
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

  asNewFile = (diffFlags & DIFF_NEWFILE)!=0;
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
      fossil_print("DELETED  %s\n", zPathname);
      if( !asNewFile ){ showDiff = 0; zFullName = NULL_DEVICE; }
    }else if( file_access(zFullName, 0) ){
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
      int eType = 0;
      if( !isLink != !file_wd_islink(zFullName) ){
        diff_print_index(zPathname, diffFlags);
        diff_print_filenames(zPathname, zPathname, diffFlags);
        fossil_print(DIFF_CANNOT_COMPUTE_SYMLINK);
        continue;
      }
      if( srcid>0 ){
        content_get(srcid, &content);
      }else{
        blob_zero(&content);
      }
      if( !fIncludeBinary ){
        looks_like_text(eType, &content);
      }
      diff_print_index(zPathname, diffFlags);
      diff_file(&content, eType, zFullName, zPathname, zDiffCmd,
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
  int eType = 0, eType2 = 0;
  if( diffFlags & DIFF_BRIEF ) return;
  file_tree_name(zFileTreeName, &fname, 1);
  zName = blob_str(&fname);
  historical_version_of_file(zFrom, zName, &v1, &isLink1, 0,
                             fIncludeBinary ? 0 : &eType, 0);
  historical_version_of_file(zTo, zName, &v2, &isLink2, 0,
                             fIncludeBinary ? 0 : &eType2, 0);
  if( isLink1 != isLink2 ){
    diff_print_filenames(zName, zName, diffFlags);
    fossil_print(DIFF_CANNOT_COMPUTE_SYMLINK);
  }else if( eType!=eType2 ){
    diff_print_filenames(zName, zName, diffFlags);
    fossil_print(DIFF_CANNOT_COMPUTE_ENCODING);
  }else{
    diff_file_mem(&v1, &v2, eType, zName, zDiffCmd,
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
  int eType = 0, eType2 = 0;
  int rid;
  const char *zName =  pFrom ? pFrom->zName : pTo->zName;
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
  if ( !fIncludeBinary ){
    looks_like_text(eType, &f1);
    looks_like_text(eType2, &f2);
  }
  if( eType!=eType2 ){
    diff_print_filenames(zName, zName, diffFlags);
    fossil_print(DIFF_CANNOT_COMPUTE_ENCODING);
  }else{
    diff_file_mem(&f1, &f2, eType, zName, zDiffCmd,
                  zBinGlob, fIncludeBinary, diffFlags);
  }
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
@ package require Tk
@ wm withdraw .
@ wm title . {Fossil Diff}
@ wm iconname . {Fossil Diff}
@ set body {}
@ set mx 80          ;# Length of the longest line of text
@ set nLine 0        ;# Number of lines of text
@ text .t -width 180 -yscroll {.sb set}
@ if {$tcl_platform(platform)=="windows"} {.t config -font {courier 9}}
@ .t tag config ln -foreground gray
@ .t tag config chng -background {#d0d0ff}
@ .t tag config add -background {#c0ffc0}
@ .t tag config rm -background {#ffc0c0}
@ proc dehtml {x} {
@   return [string map {&amp; & &lt; < &gt; > &#39; ' &quot; \"} $x]
@ }
@ # puts $cmd
@ set in [open $cmd r]
@ while {![eof $in]} {
@   set line [gets $in]
@   if {[regexp {^<a name="chunk.*"></a>} $line]} continue
@   if {[regexp {^===} $line]} {
@     set n [string length $line]
@     if {$n>$mx} {set mx $n}
@   }
@   incr nLine
@   while {[regexp {^(.*?)<span class="diff([a-z]+)">(.*?)</span>(.*)$} $line \
@             all pre class mid tail]} {
@     .t insert end [dehtml $pre] {} [dehtml $mid] $class
@     set line $tail
@   }
@   .t insert end [dehtml $line]\n {}
@ }
@ close $in
@ if {$mx>250} {set mx 250}      ;# Limit window width to 200 characters
@ if {$nLine>55} {set nLine 55}  ;# Limit window height to 55 lines
@ .t config -height $nLine -width $mx
@ pack .t -side left -fill both -expand 1
@ scrollbar .sb -command {.t yview} -orient vertical
@ pack .sb -side left -fill y
@ wm deiconify .
;

/*
** Show diff output in a Tcl/Tk window, in response to the --tk option
** to the diff command.
** 
** Steps:
** (1) Write the Tcl/Tk script used for rendering into a temp file.
** (2) Invoke "wish" on the temp file using fossil_system().
** (3) Delete the temp file.
*/
void diff_tk(const char *zSubCmd, int firstArg){
  int i;
  Blob script;
  char *zTempFile;
  char *zCmd;
  blob_zero(&script);
  blob_appendf(&script, "set cmd {| \"%/\" %s --html -y -i",
               g.nameOfExe, zSubCmd);
  for(i=firstArg; i<g.argc; i++){
    const char *z = g.argv[i];
    if( z[0]=='-' ){
      if( strglob("*-html",z) ) continue;
      if( strglob("*-y",z) ) continue;
      if( strglob("*-i",z) ) continue;
    }
    blob_append(&script, " ", 1);
    shell_escape(&script, z);
  }
  blob_appendf(&script, "}\n%s", zDiffScript);
  zTempFile = write_blob_to_temp_file(&script);
  zCmd = mprintf("tclsh \"%s\"", zTempFile);
  fossil_system(zCmd);
  file_delete(zTempFile);
  fossil_free(zCmd);
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
**   --branch BRANCH     Show diff of all changes on BRANCH
**   --brief             Show filenames only
**   --context|-c N      Use N lines of context 
**   --from|-r VERSION   select VERSION as source for the diff
**   -i                  use internal diff logic
**   --new-file|-N       output complete text of added or deleted files
**   --tk                Launch a Tcl/Tk GUI for display
**   --to VERSION        select VERSION as target for the diff
**   --side-by-side|-y   side-by-side diff
**   --unified           unified diff
**   --width|-W N        Width of lines in side-by-side diff 
**   --diff-binary BOOL  Include binary files when using external commands
**   --binary PATTERN    Treat files that match the glob PATTERN as binary
*/
void diff_cmd(void){
  int isGDiff;               /* True for gdiff.  False for normal diff */
  int isInternDiff;          /* True for internal diff */
  int hasNFlag;              /* True if -N or --new-file flag is used */
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
  hasNFlag = find_option("new-file","N",0)!=0;
  if( hasNFlag ) diffFlags |= DIFF_NEWFILE;

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
  diff_all_two_versions(zFrom, zTo, 0, 0, 0, DIFF_NEWFILE);
}
