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

/* includes needed to catch interrupts */
#ifdef _WIN32
# include <windows.h>
#else
# include <signal.h>
#endif

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
** Print details about the compared versions - possibly the working directory
** or the undo buffer.  For check-ins, show hash and commit time.
**
** This is intended primarily to go into the "header garbage" that is ignored
** by patch(1).
**
** zFrom and zTo are interpreted as symbolic version names, unless they
** start with '(', in which case they are printed directly.
*/
void diff_print_versions(const char *zFrom, const char *zTo, DiffConfig *pCfg){
  if( (pCfg->diffFlags & (DIFF_SIDEBYSIDE|DIFF_BRIEF|DIFF_NUMSTAT|
        DIFF_HTML|DIFF_WEBPAGE|DIFF_BROWSER|DIFF_JSON|DIFF_TCL))==0 ){
    fossil_print("Fossil-Diff-From:  %s\n",
      zFrom[0]=='(' ? zFrom : mprintf("%S %s",
        rid_to_uuid(symbolic_name_to_rid(zFrom, "ci")),
        db_text("","SELECT datetime(%f)||' UTC'",
          symbolic_name_to_mtime(zFrom, 0, 0))));
    fossil_print("Fossil-Diff-To:    %s\n",
      zTo[0]=='(' ? zTo : mprintf("%S %s",
        rid_to_uuid(symbolic_name_to_rid(zTo, "ci")),
        db_text("","SELECT datetime(%f)||' UTC'",
          symbolic_name_to_mtime(zTo, 0, 1))));
    fossil_print("%.66c\n", '-');
  }
}

/*
** Print the "Index:" message that patches wants to see at the top of a diff.
*/
void diff_print_index(const char *zFile, DiffConfig *pCfg, Blob *pOut){
  if( (pCfg->diffFlags &
          (DIFF_SIDEBYSIDE|DIFF_BRIEF|DIFF_NUMSTAT|DIFF_JSON|
           DIFF_WEBPAGE|DIFF_TCL))==0
  ){
    blob_appendf(pOut, "Index: %s\n%.66c\n", zFile, '=');
  }
}

/*
** Print the +++/--- filename lines or whatever filename information
** is appropriate for the output format.
**
*/
void diff_print_filenames(
  const char *zLeft,      /* Name of the left file */
  const char *zRight,     /* Name of the right file */
  DiffConfig *pCfg,       /* Diff configuration */
  Blob *pOut              /* Write to this blob, or stdout of this is NULL */
){
  u64 diffFlags = pCfg->diffFlags;
  /* Standardize on /dev/null, regardless of platform. */
  if( pCfg->diffFlags & DIFF_FILE_ADDED ) zLeft = "/dev/null";
  if( pCfg->diffFlags & DIFF_FILE_DELETED ) zRight = "/dev/null";
  if( pCfg->azLabel[0] ) zLeft = pCfg->azLabel[0];
  if( pCfg->azLabel[1] ) zRight = pCfg->azLabel[1];
  if( diffFlags & (DIFF_BRIEF|DIFF_RAW) ){
    /* no-op */
  }else if( diffFlags & DIFF_DEBUG ){
    blob_appendf(pOut, "FILE-LEFT   %s\nFILE-RIGHT  %s\n", zLeft, zRight);
  }else if( diffFlags & DIFF_WEBPAGE ){
    if( fossil_strcmp(zLeft,zRight)==0 ){
      blob_appendf(pOut,"<h1>%h</h1>\n", zLeft);
    }else{
      blob_appendf(pOut,"<h1>%h &lrarr; %h</h1>\n", zLeft, zRight);
    }
  }else if( diffFlags & (DIFF_TCL|DIFF_JSON) ){
    if( diffFlags & DIFF_TCL ){
      blob_append(pOut, "FILE ", 5);
      blob_append_tcl_literal(pOut, zLeft, (int)strlen(zLeft));
      blob_append_char(pOut, ' ');
      blob_append_tcl_literal(pOut, zRight, (int)strlen(zRight));
      blob_append_char(pOut, '\n');
    }else{
      if( pOut ) blob_trim(pOut);
      blob_append(pOut, (pCfg->nFile==0 ? "[{" : ",\n{"), -1);
      pCfg->nFile++;
      blob_append(pOut, "\n  \"leftname\":", -1);
      blob_append_json_literal(pOut, zLeft, (int)strlen(zLeft));
      blob_append(pOut, ",\n  \"rightname\":", -1);
      blob_append_json_literal(pOut, zRight, (int)strlen(zRight));
      blob_append(pOut, ",\n  \"diff\":\n", -1);
    }
  }else if( diffFlags & DIFF_SIDEBYSIDE ){
    int w = diff_width(pCfg);
    int n1 = strlen(zLeft);
    int n2 = strlen(zRight);
    int x;
    if( n1==n2 && fossil_strcmp(zLeft,zRight)==0 ){
      if( n1>w*2 ) n1 = w*2;
      x = w*2+17 - (n1+2);
      blob_appendf(pOut, "%.*c %.*s %.*c\n",
                   x/2, '=', n1, zLeft, (x+1)/2, '=');
    }else{
      if( w<20 ) w = 20;
      if( n1>w-10 ) n1 = w - 10;
      if( n2>w-10 ) n2 = w - 10;
      blob_appendf(pOut, "%.*c %.*s %.*c versus %.*c %.*s %.*c\n",
                         (w-n1+10)/2, '=', n1, zLeft, (w-n1+1)/2, '=',
                         (w-n2)/2, '=', n2, zRight, (w-n2+1)/2, '=');
    }
  }else{
    blob_appendf(pOut, "--- %s\n+++ %s\n", zLeft, zRight);
  }
}


/*
** Default header texts for diff with --webpage
*/
static const char zWebpageHdr[] =
@ <!DOCTYPE html>
@ <html>
@ <head>
@ <meta charset="UTF-8">
@ <style>
@ body {
@    background-color: white;
@ }
@ h1 {
@   font-size: 150%;
@ }
@
@ table.diff {
@   width: 100%;
@   border-spacing: 0;
@   border: 1px solid black;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ table.diff td {
@   vertical-align: top;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ table.diff pre {
@   margin: 0 0 0 0;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.diffln {
@   width: fit-content;
@   text-align: right;
@   padding: 0 1em 0 0;
@ }
@ td.difflne {
@   padding-bottom: 0.4em;
@ }
@ td.diffsep {
@   width: fit-content;
@   padding: 0 0.3em 0 1em;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.diffsep pre {
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.difftxt pre {
@   overflow-x: auto;
@ }
@ td.diffln ins {
@   background-color: #a0e4b2;
@   text-decoration: none;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.diffln del {
@   background-color: #ffc0c0;
@   text-decoration: none;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.difftxt del {
@   background-color: #ffe8e8;
@   text-decoration: none;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.difftxt del > del {
@   background-color: #ffc0c0;
@   text-decoration: none;
@   font-weight: bold;
@ }
@ td.difftxt del > del.edit {
@   background-color: #c0c0ff;
@   text-decoration: none;
@   font-weight: bold;
@ }
@ td.difftxt ins {
@   background-color: #dafbe1;
@   text-decoration: none;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.difftxt ins > ins {
@   background-color: #a0e4b2;
@   text-decoration: none;
@   font-weight: bold;
@ }
@ td.difftxt ins > ins.edit {
@   background-color: #c0c0ff;
@   text-decoration: none;
@   font-weight: bold;
@ }
@ @media (prefers-color-scheme: dark) {
@   body {
@     background-color: #353535;
@     color: #ffffff;
@   }
@   td.diffln ins {
@     background-color: #559855;
@     color: #000000;
@   }
@   td.diffln del {
@     background-color: #cc5555;
@     color: #000000;
@   }
@   td.difftxt del {
@     background-color: #f9cfcf;
@     color: #000000;
@   }
@     td.difftxt del > del {
@     background-color: #cc5555;
@     color: #000000;
@   }
@   td.difftxt ins {
@     background-color: #a2dbb2;
@     color: #000000;
@   }
@   td.difftxt ins > ins {
@     background-color: #559855;
@   }
@ }
@
@ </style>
@ </head>
@ <body>
;
static const char zWebpageHdrDark[] =
@ <!DOCTYPE html>
@ <html>
@ <head>
@ <meta charset="UTF-8">
@ <style>
@ body {
@    background-color: #353535;
@    color: #ffffff;
@ }
@ h1 {
@   font-size: 150%;
@ }
@
@ table.diff {
@   width: 100%;
@   border-spacing: 0;
@   border: 1px solid black;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ table.diff td {
@   vertical-align: top;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ table.diff pre {
@   margin: 0 0 0 0;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.diffln {
@   width: fit-content;
@   text-align: right;
@   padding: 0 1em 0 0;
@ }
@ td.difflne {
@   padding-bottom: 0.4em;
@ }
@ td.diffsep {
@   width: fit-content;
@   padding: 0 0.3em 0 1em;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.diffsep pre {
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.difftxt pre {
@   overflow-x: auto;
@ }
@ td.diffln ins {
@   background-color: #559855;
@   color: #000000;
@   text-decoration: none;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.diffln del {
@   background-color: #cc5555;
@   color: #000000;
@   text-decoration: none;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.difftxt del {
@   background-color: #f9cfcf;
@   color: #000000;
@   text-decoration: none;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.difftxt del > del {
@   background-color: #cc5555;
@   color: #000000;
@   text-decoration: none;
@   font-weight: bold;
@ }
@ td.difftxt del > del.edit {
@   background-color: #c0c0ff;
@   text-decoration: none;
@   font-weight: bold;
@ }
@ td.difftxt ins {
@   background-color: #a2dbb2;
@   color: #000000;
@   text-decoration: none;
@   line-height: inherit;
@   font-size: inherit;
@ }
@ td.difftxt ins > ins {
@   background-color: #559855;
@   text-decoration: none;
@   font-weight: bold;
@ }
@ td.difftxt ins > ins.edit {
@   background-color: #c0c0ff;
@   text-decoration: none;
@   font-weight: bold;
@ }
@
@ </style>
@ </head>
@ <body>
;
const char zWebpageEnd[] =
@ </body>
@ </html>
;

/*
** State variables used by the --browser option for diff.  These must
** be static variables, not elements of DiffConfig, since they are
** used by the interrupt handler.
*/
static char *tempDiffFilename;  /* File holding the diff HTML */
static FILE *diffOut;           /* Open to write into tempDiffFilename */

/* Amount of delay (in milliseconds) between launching the
** web browser and deleting the temporary file used by --browser
*/
#ifndef FOSSIL_BROWSER_DIFF_DELAY
# define FOSSIL_BROWSER_DIFF_DELAY 5000  /* 5 seconds by default */
#endif

/*
** If we catch a single while writing the temporary file for the --browser
** diff output, then delete the temporary file and exit.
*/
static void diff_www_interrupt(int NotUsed){
  (void)NotUsed;
  if( diffOut ) fclose(diffOut);
  if( tempDiffFilename ) file_delete(tempDiffFilename);
  exit(1);
}
#ifdef _WIN32
static BOOL WINAPI diff_console_ctrl_handler(DWORD dwCtrlType){
  if( dwCtrlType==CTRL_C_EVENT ) diff_www_interrupt(0);
  return FALSE;
}
#endif


/*
** Do preliminary setup and output before computing a diff.
**
** For --browser, redirect stdout to a temporary file that will
** hold the result.  Make arrangements to delete that temporary
** file if the diff is interrupted.
**
** For --browser and --webpage, output the HTML header.
*/
void diff_begin(DiffConfig *pCfg){
  if( (pCfg->diffFlags & DIFF_BROWSER)!=0 ){
    tempDiffFilename = fossil_temp_filename();
    tempDiffFilename = sqlite3_mprintf("%z.html", tempDiffFilename);
    diffOut = fossil_freopen(tempDiffFilename,"wb",stdout);
    if( diffOut==0 ){
      fossil_fatal("unable to create temporary file \"%s\"",
                   tempDiffFilename);
    }
#ifndef _WIN32
    signal(SIGINT, diff_www_interrupt);
#else
    SetConsoleCtrlHandler(diff_console_ctrl_handler, TRUE);
#endif
  }
  if( (pCfg->diffFlags & DIFF_WEBPAGE)!=0 ){
    fossil_print("%s",(pCfg->diffFlags & DIFF_DARKMODE)!=0 ? zWebpageHdrDark :
                                                             zWebpageHdr);
    fflush(stdout);
  }
}

/* Do any final output required by a diff and complete the diff
** process.
**
** For --browser and --webpage, output any javascript required by
** the diff.  (Currently JS is only needed for side-by-side diffs).
**
** For --browser, close the connection to the temporary file, then
** launch a web browser to view the file.  After a delay
** of FOSSIL_BROWSER_DIFF_DELAY milliseconds, delete the temp file.
*/
void diff_end(DiffConfig *pCfg, int nErr){
  if( (pCfg->diffFlags & DIFF_WEBPAGE)!=0 ){
    if( pCfg->diffFlags & DIFF_SIDEBYSIDE ){
      const unsigned char *zJs = builtin_file("diff.js", 0);
      fossil_print("<script>\n%s</script>\n", zJs);
    }
    fossil_print("%s", zWebpageEnd);
  }
  if( (pCfg->diffFlags & DIFF_BROWSER)!=0 && nErr==0 ){
    char *zCmd = mprintf("%s %$", fossil_web_browser(), tempDiffFilename);
    fclose(diffOut);
    diffOut = fossil_freopen(NULL_DEVICE, "wb", stdout);
    fossil_system(zCmd);
    fossil_free(zCmd);
    diffOut = 0;
    sqlite3_sleep(FOSSIL_BROWSER_DIFF_DELAY);
    file_delete(tempDiffFilename);
    sqlite3_free(tempDiffFilename);
    tempDiffFilename = 0;
  }
  if( (pCfg->diffFlags & DIFF_JSON)!=0 && pCfg->nFile>0 ){
    fossil_print("]\n");
  }
}

/*
** Show the difference between two files, one in memory and one on disk.
**
** The difference is the set of edits needed to transform pFile1 into
** zFile2.  The content of pFile1 is in memory.  zFile2 exists on disk.
*/
void diff_file(
  Blob *pFile1,             /* In memory content to compare from */
  const char *zFile2,       /* On disk content to compare to */
  const char *zName,        /* Display name of the file */
  DiffConfig *pCfg,         /* Flags to control the diff */
  Blob *pOut            /* Blob to store diff output */
){
  if( pCfg->zDiffCmd==0 ){
    Blob out;                 /* Diff output text */
    Blob file2;               /* Content of zFile2 */
    const char *zName2;       /* Name of zFile2 for display */

    /* Read content of zFile2 into memory */
    blob_zero(&file2);
    if( pCfg->diffFlags & DIFF_FILE_DELETED || file_size(zFile2, ExtFILE)<0 ){
      zName2 = NULL_DEVICE;
    }else{
      blob_read_from_file(&file2, zFile2, ExtFILE);
      zName2 = zName;
    }

    /* Compute and output the differences */
    if( (pCfg->diffFlags & DIFF_BRIEF) && !(pCfg->diffFlags & DIFF_NUMSTAT) ){
      if( blob_compare(pFile1, &file2) ){
        fossil_print("CHANGED  %s\n", zName);
      }
    }else{
      blob_zero(&out);
      text_diff(pFile1, &file2, &out, pCfg);
      if( blob_size(&out) ){
        if( pCfg->diffFlags & DIFF_NUMSTAT ){
          if( !(pCfg->diffFlags & DIFF_BRIEF) ){
            blob_appendf(pOut, "%s %s\n", blob_str(&out), zName);
          }
        }else{
          diff_print_filenames(zName, zName2, pCfg, pOut);
          blob_appendf(pOut, "%s\n", blob_str(&out));
        }
      }
      blob_reset(&out);
    }

    /* Release memory resources */
    blob_reset(&file2);
  }else{
    Blob nameFile1;    /* Name of temporary file to old pFile1 content */
    Blob cmd;          /* Text of command to run */
    int useTempfile = 1;

    if( (pCfg->diffFlags & DIFF_INCBINARY)==0 ){
      Blob file2;
      if( looks_like_binary(pFile1) ){
        fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
        return;
      }
      if( pCfg->zBinGlob ){
        Glob *pBinary = glob_create(pCfg->zBinGlob);
        if( glob_match(pBinary, zName) ){
          fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
          glob_free(pBinary);
          return;
        }
        glob_free(pBinary);
      }
      blob_zero(&file2);
      if( file_size(zFile2, ExtFILE)>=0 ){
        blob_read_from_file(&file2, zFile2, ExtFILE);
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
    file_tempname(&nameFile1, zFile2, "orig");
#if !defined(_WIN32)
    /* On Unix, use /dev/null for added or deleted files. */
    if( pCfg->diffFlags & DIFF_FILE_ADDED ){
      blob_init(&nameFile1, NULL_DEVICE, -1);
      useTempfile = 0;
    }else if( pCfg->diffFlags & DIFF_FILE_DELETED ){
      zFile2 = NULL_DEVICE;
    }
#endif
    if( useTempfile ) blob_write_to_file(pFile1, blob_str(&nameFile1));

    /* Construct the external diff command */
    blob_zero(&cmd);
    blob_append(&cmd, pCfg->zDiffCmd, -1);
    if( pCfg->diffFlags & DIFF_INVERT ){
      blob_append_escaped_arg(&cmd, zFile2, 1);
      blob_append_escaped_arg(&cmd, blob_str(&nameFile1), 1);
    }else{
      blob_append_escaped_arg(&cmd, blob_str(&nameFile1), 1);
      blob_append_escaped_arg(&cmd, zFile2, 1);
    }

    /* Run the external diff command */
    fossil_system(blob_str(&cmd));

    /* Delete the temporary file and clean up memory used */
    if( useTempfile ) file_delete(blob_str(&nameFile1));
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
  const char *zName,        /* Display name of the file */
  DiffConfig *pCfg          /* Diff flags */
){
  if( (pCfg->diffFlags & DIFF_BRIEF) && !(pCfg->diffFlags & DIFF_NUMSTAT) ){
    return;
  }
  if( pCfg->zDiffCmd==0 ){
    Blob out;      /* Diff output text */

    blob_zero(&out);
    text_diff(pFile1, pFile2, &out, pCfg);
    if( pCfg->diffFlags & DIFF_NUMSTAT ){
      if( !(pCfg->diffFlags & DIFF_BRIEF) ){
        fossil_print("%s %s\n", blob_str(&out), zName);
      }
    }else{
      diff_print_filenames(zName, zName, pCfg, 0);
      fossil_print("%s\n", blob_str(&out));
    }

    /* Release memory resources */
    blob_reset(&out);
  }else{
    Blob cmd;
    Blob temp1;
    Blob temp2;
    int useTempfile1 = 1;
    int useTempfile2 = 1;

    if( (pCfg->diffFlags & DIFF_INCBINARY)==0 ){
      if( looks_like_binary(pFile1) || looks_like_binary(pFile2) ){
        fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
        return;
      }
      if( pCfg->zBinGlob ){
        Glob *pBinary = glob_create(pCfg->zBinGlob);
        if( glob_match(pBinary, zName) ){
          fossil_print("%s",DIFF_CANNOT_COMPUTE_BINARY);
          glob_free(pBinary);
          return;
        }
        glob_free(pBinary);
      }
    }

    /* Construct temporary file names */
    file_tempname(&temp1, zName, "before");
    file_tempname(&temp2, zName, "after");
#if !defined(_WIN32)
    /* On Unix, use /dev/null for added or deleted files. */
    if( pCfg->diffFlags & DIFF_FILE_ADDED ){
      useTempfile1 = 0;
      blob_init(&temp1, NULL_DEVICE, -1);
    }else if( pCfg->diffFlags & DIFF_FILE_DELETED ){
      useTempfile2 = 0;
      blob_init(&temp2, NULL_DEVICE, -1);
    }
#endif
    if( useTempfile1 ) blob_write_to_file(pFile1, blob_str(&temp1));
    if( useTempfile2 ) blob_write_to_file(pFile2, blob_str(&temp2));

    /* Construct the external diff command */
    blob_zero(&cmd);
    blob_append(&cmd, pCfg->zDiffCmd, -1);
    blob_append_escaped_arg(&cmd, blob_str(&temp1), 1);
    blob_append_escaped_arg(&cmd, blob_str(&temp2), 1);

    /* Run the external diff command */
    fossil_system(blob_str(&cmd));

    /* Delete the temporary file and clean up memory used */
    if( useTempfile1 ) file_delete(blob_str(&temp1));
    if( useTempfile2 ) file_delete(blob_str(&temp2));

    blob_reset(&temp1);
    blob_reset(&temp2);
    blob_reset(&cmd);
  }
}

/*
** Return true if the disk file is identical to the Blob.  Return zero
** if the files differ in any way.
*/
static int file_same_as_blob(Blob *blob, const char *zDiskFile){
  Blob file;
  int rc = 0;
  if( blob_size(blob)!=file_size(zDiskFile, ExtFILE) ) return 0;
  blob_zero(&file);
  blob_read_from_file(&file, zDiskFile, ExtFILE);
  if( blob_size(&file)!=blob_size(blob) ){
    rc = 0;
  }else{
    rc = memcmp(blob_buffer(&file), blob_buffer(blob), blob_size(&file))==0;
  }
  blob_reset(&file);
  return rc;
}

/*
** Run a diff between the version zFrom and files on disk in the current
** working checkout.  zFrom might be NULL which means to simply show the
** difference between the edited files on disk and the check-out on which
** they are based.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
void diff_version_to_checkout(
  const char *zFrom,        /* Version to difference from */
  DiffConfig *pCfg,         /* Flags controlling diff output */
  FileDirList *pFileDir,    /* Which files to diff */
  Blob *pOut                /* Blob to output diff instead of stdout */
){
  int vid;
  Blob sql;
  Stmt q;
  int asNewFile;            /* Treat non-existant files as empty files */
  int isNumStat;            /* True for --numstat */

  asNewFile = (pCfg->diffFlags & (DIFF_VERBOSE|DIFF_NUMSTAT|DIFF_HTML))!=0;
  isNumStat = (pCfg->diffFlags & (DIFF_NUMSTAT|DIFF_TCL|DIFF_HTML))!=0;
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
  if( (pCfg->diffFlags & DIFF_SHOW_VERS)!=0 ){
    diff_print_versions(zFrom ? zFrom : db_lget("checkout-hash", 0),
      "(workdir)", pCfg);
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
    pCfg->diffFlags &= (~DIFF_FILE_MASK);
    if( isDeleted ){
      if( !isNumStat ){ fossil_print("DELETED  %s\n", zPathname); }
      pCfg->diffFlags |= DIFF_FILE_DELETED;
      if( !asNewFile ){ showDiff = 0; zFullName = NULL_DEVICE; }
    }else if( file_access(zFullName, F_OK) ){
      if( !isNumStat ){ fossil_print("MISSING  %s\n", zPathname); }
      if( !asNewFile ){ showDiff = 0; }
    }else if( isNew ){
      if( !isNumStat ){ fossil_print("ADDED    %s\n", zPathname); }
      pCfg->diffFlags |= DIFF_FILE_ADDED;
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }else if( isChnged==3 ){
      if( !isNumStat ){ fossil_print("ADDED_BY_MERGE %s\n", zPathname); }
      pCfg->diffFlags |= DIFF_FILE_ADDED;
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }else if( isChnged==5 ){
      if( !isNumStat ){ fossil_print("ADDED_BY_INTEGRATE %s\n", zPathname); }
      pCfg->diffFlags |= DIFF_FILE_ADDED;
      srcid = 0;
      if( !asNewFile ){ showDiff = 0; }
    }
    if( showDiff ){
      Blob content;
      if( !isLink != !file_islink(zFullName) ){
        diff_print_index(zPathname, pCfg, 0);
        diff_print_filenames(zPathname, zPathname, pCfg, 0);
        fossil_print("%s",DIFF_CANNOT_COMPUTE_SYMLINK);
        continue;
      }
      if( srcid>0 ){
        content_get(srcid, &content);
      }else{
        blob_zero(&content);
      }
      if( isChnged==0
       || pCfg->diffFlags & DIFF_FILE_DELETED
       || !file_same_as_blob(&content, zFullName)
      ){
        diff_print_index(zPathname, pCfg, pOut);
        diff_file(&content, zFullName, zPathname, pCfg, pOut);
      }
      blob_reset(&content);
    }
    blob_reset(&fname);
  }
  db_finalize(&q);
  db_end_transaction(1);  /* ROLLBACK */
}

/*
** Run a diff from the undo buffer to files on disk.
**
** Use the internal diff logic if zDiffCmd is NULL.  Otherwise call the
** command zDiffCmd to do the diffing.
**
** When using an external diff program, zBinGlob contains the GLOB patterns
** for file names to treat as binary.  If fIncludeBinary is zero, these files
** will be skipped in addition to files that may contain binary content.
*/
static void diff_undo_to_checkout(
  DiffConfig *pCfg,         /* Flags controlling diff output */
  FileDirList *pFileDir     /* List of files and directories to diff */
){
  Stmt q;
  Blob content;
  db_prepare(&q, "SELECT pathname, content FROM undo");
  blob_init(&content, 0, 0);
  if( (pCfg->diffFlags & DIFF_SHOW_VERS)!=0 ){
    diff_print_versions("(undo)", "(workdir)", pCfg);
  }
  while( db_step(&q)==SQLITE_ROW ){
    char *zFullName;
    const char *zFile = (const char*)db_column_text(&q, 0);
    if( !file_dir_match(pFileDir, zFile) ) continue;
    zFullName = mprintf("%s%s", g.zLocalRoot, zFile);
    db_column_blob(&q, 1, &content);
    diff_file(&content, zFullName, zFile, pCfg, 0);
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
  DiffConfig *pCfg
){
  Blob f1, f2;
  int rid;
  const char *zName;
  if( pFrom ){
    zName = pFrom->zName;
  }else if( pTo ){
    zName = pTo->zName;
  }else{
    zName = DIFF_NO_NAME;
  }
  if( (pCfg->diffFlags & DIFF_BRIEF) && !(pCfg->diffFlags & DIFF_NUMSTAT) ){
    return;
  }
  diff_print_index(zName, pCfg, 0);
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
  diff_file_mem(&f1, &f2, zName, pCfg);
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
  DiffConfig *pCfg,
  FileDirList *pFileDir
){
  Manifest *pFrom, *pTo;
  ManifestFile *pFromFile, *pToFile;
  int asNewFlag = (pCfg->diffFlags & (DIFF_VERBOSE|DIFF_NUMSTAT))!=0 ? 1 : 0;

  pFrom = manifest_get_by_name(zFrom, 0);
  manifest_file_rewind(pFrom);
  pFromFile = manifest_file_next(pFrom,0);
  pTo = manifest_get_by_name(zTo, 0);
  manifest_file_rewind(pTo);
  pToFile = manifest_file_next(pTo,0);
  if( (pCfg->diffFlags & DIFF_SHOW_VERS)!=0 ){
    diff_print_versions(zFrom, zTo, pCfg);
  }
  while( pFromFile || pToFile ){
    int cmp;
    if( pFromFile==0 ){
      cmp = +1;
    }else if( pToFile==0 ){
      cmp = -1;
    }else{
      cmp = fossil_strcmp(pFromFile->zName, pToFile->zName);
    }
    pCfg->diffFlags &= (~DIFF_FILE_MASK);
    if( cmp<0 ){
      if( file_dir_match(pFileDir, pFromFile->zName) ){
        if( (pCfg->diffFlags & (DIFF_NUMSTAT|DIFF_HTML))==0 ){
          fossil_print("DELETED %s\n", pFromFile->zName);
        }
        pCfg->diffFlags |= DIFF_FILE_DELETED;
        if( asNewFlag ){
          diff_manifest_entry(pFromFile, 0, pCfg);
        }
      }
      pFromFile = manifest_file_next(pFrom,0);
    }else if( cmp>0 ){
      if( file_dir_match(pFileDir, pToFile->zName) ){
        if( (pCfg->diffFlags &
             (DIFF_NUMSTAT|DIFF_HTML|DIFF_TCL|DIFF_JSON))==0 ){
          fossil_print("ADDED   %s\n", pToFile->zName);
        }
        pCfg->diffFlags |= DIFF_FILE_ADDED;
        if( asNewFlag ){
          diff_manifest_entry(0, pToFile, pCfg);
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
        if((pCfg->diffFlags & DIFF_BRIEF) && !(pCfg->diffFlags & DIFF_NUMSTAT)){
          fossil_print("CHANGED %s\n", pFromFile->zName);
        }else{
          diff_manifest_entry(pFromFile, pToFile, pCfg);
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
** Compute the difference from an external tree of files to the current
** working checkout with its edits.
**
** To put it another way:  Every managed file in the current working
** checkout is compared to the file with same name under zExternBase.  The
** zExternBase files are on the left and the files in the current working
** directory are on the right.
*/
void diff_externbase_to_checkout(
  const char *zExternBase,   /* Remote tree to use as the baseline */
  DiffConfig *pCfg,          /* Diff settings */
  FileDirList *pFileDir      /* Only look at these files */
){
  int vid;
  Stmt q;

  vid = db_lget_int("checkout",0);
  if( file_isdir(zExternBase, ExtFILE)!=1 ){
    fossil_fatal("\"%s\" is not a directory", zExternBase);
  }
  db_prepare(&q,
    "SELECT pathname FROM vfile WHERE vid=%d ORDER BY pathname",
    vid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFile;  /* Name of file in the repository */
    char *zLhs;         /* Full name of left-hand side file */
    char *zRhs;         /* Full name of right-hand side file */
    Blob rhs;           /* Full text of RHS */
    Blob lhs;           /* Full text of LHS */

    zFile = db_column_text(&q,0);
    if( !file_dir_match(pFileDir, zFile) ) continue;
    zLhs = mprintf("%s/%s", zExternBase, zFile);
    zRhs = mprintf("%s%s", g.zLocalRoot, zFile);
    if( file_size(zLhs, ExtFILE)<0 ){
      blob_zero(&lhs);
    }else{
      blob_read_from_file(&lhs, zLhs, ExtFILE);
    }
    blob_read_from_file(&rhs, zRhs, ExtFILE);
    if( blob_size(&lhs)!=blob_size(&rhs)
     || memcmp(blob_buffer(&lhs), blob_buffer(&rhs), blob_size(&lhs))!=0
    ){
      diff_print_index(zFile, pCfg, 0);
      diff_file_mem(&lhs, &rhs, zFile, pCfg);
    }
    blob_reset(&lhs);
    blob_reset(&rhs);
    fossil_free(zLhs);
    fossil_free(zRhs);
  }
  db_finalize(&q);
}


/*
** Return the name of the external diff command, or return NULL if
** no external diff command is defined.
*/
const char *diff_command_external(int guiDiff){
  const char *zName;
  zName = guiDiff ? "gdiff-command" : "diff-command";
  return db_get(zName, 0);
}

/*
** Return true if it reasonable to run "diff -tk" for "gdiff".
**
** Details: Return true if all of the following are true:
**
**     (1)   The isGDiff flags is true
**     (2)   The "gdiff-command" setting is undefined
**     (3)   There is a "tclsh" on PATH
**     (4)   There is a "wish" on PATH
*/
int gdiff_using_tk(int isGdiff){
  if( isGdiff
   && db_get("gdiff-command","")[0]==0
   && fossil_app_on_path("tclsh",0)
   && fossil_app_on_path("wish",0)
  ){
    return 1;
  }
  return 0;
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
  const char *zTclsh;
  int bDebug = find_option("tkdebug",0,0)!=0;
  int bDarkMode = find_option("dark",0,0)!=0;
  (void)find_option("debug",0,0);
  blob_zero(&script);
  /* Caution:  When this routine is called from the merge-info command,
  ** the --tcl argument requires an argument.  But merge-info does not
  ** use -i, so we can take -i as that argument.  This routine needs to
  ** always have -i after --tcl.
  **                                                CAUTION!
  **                                                vvvvvvv            */
  blob_appendf(&script, "set fossilcmd {| \"%/\" %s -tcl -i -v",
               g.nameOfExe, zSubCmd);
  find_option("tcl",0,0);
  find_option("html",0,0);
  find_option("side-by-side","y",0);
  find_option("internal","i",0);
  find_option("verbose","v",0);
  zTclsh = find_option("tclsh",0,1);
  if( zTclsh==0 ){
    zTclsh = db_get("tclsh",0);
  }
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
  blob_appendf(&script, "}\nset darkmode %d\n", bDarkMode);
  blob_appendf(&script, "set debug %d\n", bDebug);
  blob_appendf(&script, "%s", builtin_file("diff.tcl", 0));
  if( zTempFile ){
    blob_write_to_file(&script, zTempFile);
    fossil_print("To see diff, run: %s \"%s\"\n", zTclsh, zTempFile);
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
    zCmd = mprintf("%$ %$", zTclsh, zTempFile);
    fossil_system(zCmd);
    file_delete(zTempFile);
    fossil_free(zCmd);
  }
  blob_reset(&script);
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
** specified (as they exist on disk) and that same file as it was checked-
** out.  Or if the FILE arguments are omitted, show all unsaved changes
** currently in the working check-out.  The "gdiff" variant means to
** use a GUI diff.
**
** The default output format is a "unified patch" (the same as the
** output of "diff -u" on most unix systems).  Many alternative formats
** are available.  A few of the more useful alternatives:
**
**    --tk              Pop up a Tcl/Tk-based GUI to show the diff
**    --by              Show a side-by-side diff in the default web browser
**    -b                Show a linear diff in the default web browser
**    -y                Show a text side-by-side diff
**    --webpage         Format output as HTML
**    --webpage -y      HTML output in the side-by-side format
**
** The "--from VERSION" option is used to specify the source check-in
** for the diff operation.  If not specified, the source check-in is the
** base check-in for the current check-out. Similarly, the "--to VERSION"
** option specifies the check-in from which the second version of the file
** or files is taken.  If there is no "--to" option then the (possibly edited)
** files in the current check-out are used.  The "--checkin VERSION" option
** shows the changes made by check-in VERSION relative to its primary parent.
** The "--branch BRANCHNAME" shows all the changes on the branch BRANCHNAME.
**
** With the "--from VERSION" option, if VERSION is actually a directory name
** (not a tag or check-in hash) then the files under that directory are used
** as the baseline for the diff.
**
** The "-i" command-line option forces the use of Fossil's own internal
** diff logic rather than any external diff program that might be configured
** using the "setting" command.  If no external diff program is configured,
** then the "-i" option is a no-op.  The "-i" option converts "gdiff" into
** "diff".
**
** The "--diff-binary" option enables or disables the inclusion of binary files
** when using an external diff program.
**
** The "--binary" option causes files matching the glob PATTERN to be treated
** as binary when considering if they should be used with the external diff
** program.  This option overrides the "binary-glob" setting.
**
** These command show differences between managed files. Use the "fossil xdiff"
** command to see differences in unmanaged files.
**
** Options:
**   --binary PATTERN            Treat files that match the glob PATTERN
**                               as binary
**   --branch BRANCH             Show diff of all changes on BRANCH
**   --brief                     Show filenames only
**   -b|--browser                Show the diff output in a web-browser
**   --by                        Shorthand for "--browser -y"
**   -ci|--checkin VERSION       Show diff of all changes in VERSION
**   --command PROG              External diff program. Overrides "diff-command"
**   -c|--context N              Show N lines of context around each change,
**                               with negative N meaning show all content
**   --dark                      Use dark mode for the Tcl/Tk-based GUI and HTML
**   --diff-binary BOOL          Include binary files with external commands
**   --exec-abs-paths            Force absolute path names on external commands
**   --exec-rel-paths            Force relative path names on external commands
**   -r|--from VERSION           Use VERSION as the baseline for the diff, or
**                               if VERSION is a directory name, use files in
**                               that directory as the baseline.
**   -w|--ignore-all-space       Ignore white space when comparing lines
**   -i|--internal               Use internal diff logic
**   --invert                    Invert the diff
**   --json                      Output formatted as JSON
**   -n|--linenum                Show line numbers
**   -N|--new-file               Alias for --verbose
**   --numstat                   Show the number of added and deleted lines per
**                               file, omitting the diff. When combined with
**                                 --brief, show only the total row.
**   -y|--side-by-side           Side-by-side diff
**   --strip-trailing-cr         Strip trailing CR
**   --tcl                       Tcl-formatted output used internally by --tk
**   --tclsh PATH                Tcl/Tk shell used for --tk (default: "tclsh")
**   --tk                        Launch a Tcl/Tk GUI for display
**   --to VERSION                Select VERSION as target for the diff
**   --undo                      Use the undo buffer as the baseline
**   --unified                   Unified diff
**   -v|--verbose                Output complete text of added or deleted files
**   -h|--versions               Show compared versions in the diff header
**   --webpage                   Format output as a stand-alone HTML webpage
**   -W|--width N                Width of lines in side-by-side diff
**   -Z|--ignore-trailing-space  Ignore changes to end-of-line whitespace
*/
void diff_cmd(void){
  int isGDiff;               /* True for gdiff.  False for normal diff */
  const char *zFrom;         /* Source version number */
  const char *zTo;           /* Target version number */
  const char *zCheckin;      /* Check-in version number */
  const char *zBranch;       /* Branch to diff */
  int againstUndo = 0;       /* Diff against files in the undo buffer */
  FileDirList *pFileDir = 0; /* Restrict the diff to these files */
  DiffConfig DCfg;           /* Diff configuration object */
  int bFromIsDir = 0;        /* True if zFrom is a directory name */

  isGDiff = g.argv[1][0]=='g';
  if( find_option("tk",0,0)!=0|| has_option("tclsh") ){
    diff_tk("diff", 2);
    return;
  }
  zFrom = find_option("from", "r", 1);
  zTo = find_option("to", 0, 1);
  zCheckin = find_option("checkin", "ci", 1);
  zBranch = find_option("branch", 0, 1);
  againstUndo = find_option("undo",0,0)!=0;
  if( againstUndo && (zFrom!=0 || zTo!=0 || zCheckin!=0 || zBranch!=0) ){
    fossil_fatal("cannot use --undo together with --from, --to, --checkin,"
                 " or --branch");
  }
  if( zBranch ){
    if( zTo || zFrom || zCheckin ){
      fossil_fatal("cannot use --from, --to, or --checkin with --branch");
    }
    zTo = zBranch;
    zFrom = mprintf("root:%s", zBranch);
    zBranch = 0;
  }
  if( zCheckin!=0 && (zFrom!=0 || zTo!=0) ){
    fossil_fatal("cannot use --checkin together with --from or --to");
  }
  if( 0==zCheckin ){
    if( zTo==0 || againstUndo ){
      db_must_be_within_tree();
    }else if( zFrom==0 ){
      fossil_fatal("must use --from if --to is present");
    }else{
      db_find_and_open_repository(0, 0);
    }
  }else{
    db_find_and_open_repository(0, 0);
  }
  if( gdiff_using_tk(isGDiff) ){
    restore_option("--from", zFrom, 1);
    restore_option("--to", zTo, 1);
    restore_option("--checkin", zCheckin, 1);
    restore_option("--branch", zBranch, 1);
    if( againstUndo ) restore_option("--undo", 0, 0);
    diff_tk("diff", 2);
    return;
  }
  determine_exec_relative_option(1);
  if( zFrom!=file_tail(zFrom)
   && file_isdir(zFrom, ExtFILE)==1
   && !db_exists("SELECT 1 FROM tag WHERE tagname='sym-%q'", zFrom)
  ){
    bFromIsDir = 1;
    if( zTo ){
      fossil_fatal("cannot use --to together with \"--from PATH\"");
    }
  }
  diff_options(&DCfg, isGDiff, 0);
  verify_all_options();
  g.diffCnt[0] = g.diffCnt[1] = g.diffCnt[2] = 0;
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
  if( DCfg.diffFlags & DIFF_NUMSTAT ){
    fossil_print("%10s %10s\n", "INSERTED", "DELETED");
  }
  if( zCheckin!=0 ){
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
  diff_begin(&DCfg);
  if( bFromIsDir ){
    diff_externbase_to_checkout(zFrom, &DCfg, pFileDir);
  }else if( againstUndo ){
    if( db_lget_int("undo_available",0)==0 ){
      fossil_print("No undo or redo is available\n");
      return;
    }
    diff_undo_to_checkout(&DCfg, pFileDir);
  }else if( zTo==0 ){
    diff_version_to_checkout(zFrom, &DCfg, pFileDir, 0);
  }else{
    diff_two_versions(zFrom, zTo, &DCfg, pFileDir);
  }
  if( pFileDir ){
    int i;
    for(i=0; pFileDir[i].zName; i++){
      if( pFileDir[i].nUsed==0
       && strcmp(pFileDir[0].zName,".")!=0
       && !file_isdir(g.argv[i+2], ExtFILE)
      ){
        fossil_fatal("not found: '%s'", g.argv[i+2]);
      }
      fossil_free(pFileDir[i].zName);
    }
    fossil_free(pFileDir);
  }
  diff_end(&DCfg, 0);
  if ( DCfg.diffFlags & DIFF_NUMSTAT ){
    fossil_print("%10d %10d TOTAL over %d changed file%s\n",
           g.diffCnt[1], g.diffCnt[2], g.diffCnt[0], g.diffCnt[0]!=1 ? "s": "");
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
  DiffConfig DCfg;
  cgi_check_for_malice();
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( zFrom==0 || zTo==0 ) fossil_redirect_home();
  if( robot_restrict("diff") ) return;

  fossil_nice_default();
  cgi_set_content_type("text/plain");
  diff_config_init(&DCfg, DIFF_VERBOSE);
  diff_two_versions(zFrom, zTo, &DCfg, 0);
}
