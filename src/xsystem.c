/*
** Copyright (c) 2025 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@sqlite.org
**
*******************************************************************************
**
** This file contains code used to implement "fossil system ..." command.
**
** Fossil is frequently used by people familiar with Unix but who must
** sometimes also work on Windows systems.  The "fossil sys ..." command
** provides a few work-arounds for command unix command-line utilities to
** help make development on Windows more habitable for long-time unix
** users.  The commands provided here are normally cheap substitutes to
** their more feature-reach unix counterparts.  But they are sufficient to
** get the job done.
**
** This source code file is called "xsystem.c" with the 'x' up front because
** if it were called "system.c", then makeheaders would generate a "system.h"
** header file, and that might be confused with an actual system header
** file.
*/
#include "config.h"
#include "xsystem.h"
#include "qrf.h"
#include <time.h>


/* Date and time */
void xsystem_date(int argc, char **argv){
  (void)argc;
  (void)argv;
  fossil_print("%z = ", cgi_iso8601_datestamp());
  fossil_print("%z\n", cgi_rfc822_datestamp(time(0)));
}

/* Present working diretory */
void xsystem_pwd(int argc, char **argv){
  char *zPwd = file_getcwd(0, 0);
  fossil_print("%z\n", zPwd);
}

/* Implement "stty size" */
void xsystem_stty(int argc, char **argv){
  TerminalSize ts;
  if( argc!=2 || strcmp(argv[1],"size")!=0 ){
    fossil_print("ERROR: only \"stty size\" is supported\n");
  }else{
    terminal_get_size(&ts);
    fossil_print("%d %d\n", ts.nLines, ts.nColumns);
  }
}

/* Show where an executable is located on PATH */
void xsystem_which(int argc, char **argv){
  int ePrint = 1;
  int i;
  for(i=1; i<argc; i++){
    const char *z = argv[i];
    if( z[0]!='-' ){
      fossil_app_on_path(z, ePrint);
    }else{
      if( z[1]=='-' && z[2]!=0 ) z++;
      if( fossil_strcmp(z,"-a")==0 ){
        ePrint = 2;
      }else
      {
        fossil_fatal("unknown option \"%s\"", argv[i]);
      }
    }
  }
}

/*
** Bit values for the mFlags paramater to "ls"
*/
#define LS_LONG         0x001   /* -l  Long format - one object per line */
#define LS_REVERSE      0x002   /* -r  Reverse the sort order */
#define LS_MTIME        0x004   /* -t  Sort by mtime, newest first */
#define LS_SIZE         0x008   /* -S  Sort by size, largest first */
#define LS_COMMA        0x010   /* -m  Comma-separated list */
#define LS_DIRONLY      0x020   /* -d  Show just directory name, not content */
#define LS_ALL          0x040   /* -a  Show all entries */
#define LS_COLOR        0x080   /*     Colorize the output */
#define LS_COLUMNS      0x100   /* -C  Split column output */

/* xWrite() callback from QRF
*/
static int xsystem_write(void *NotUsed, const char *zText, sqlite3_int64 n){
  fossil_puts(zText, 0, (int)n);
  return SQLITE_OK;
}

/* Helper function for xsystem_ls():  Make entries in the LS table
** for every file or directory zName.
**
** If zName is a directory, load all files contained within that directory.
** If zName is just a file, load only that file.
*/
static void xsystem_ls_insert(
  sqlite3_stmt *pStmt,
  const char *zName,
  int mFlags
){
  char *aList[2];
  char **azList;
  int nList;
  int i;
  const char *zPrefix;
  switch( file_isdir(zName, ExtFILE) ){
    case 1: {  /* A directory */
      if( (mFlags & LS_DIRONLY)==0 ){
        azList = 0;
        nList = file_directory_list(zName, 0, (mFlags & LS_ALL)==0, 0, &azList);
        zPrefix = fossil_strcmp(zName,".") ? zName : 0;
        break;
      }
    }
    case 2: {  /* A file */
      aList[0] = (char*)zName;
      aList[1] = 0;
      azList = aList;
      nList = 1;
      zPrefix = 0;
      break;
    }
    default: {  /* Does not exist */
      return;
    }
  }
  for(i=0; i<nList; i++){
    char *zFile = zPrefix ? mprintf("%s/%s",zPrefix,azList[i]) : azList[i];
    int mode = file_mode(zFile, ExtFILE);
    sqlite3_int64 sz = file_size(zFile, ExtFILE);
    sqlite3_int64 mtime = file_mtime(zFile, ExtFILE);
    sqlite3_bind_text(pStmt, 1, azList[i], -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(pStmt, 2, mtime);
    sqlite3_bind_int64(pStmt, 3, sz);
    sqlite3_bind_int(pStmt, 4, mode);
    sqlite3_bind_int64(pStmt, 5, strlen(zFile));
        /* TODO:  wcwidth()------^^^^^^ */
    sqlite3_step(pStmt);
    sqlite3_reset(pStmt);
    if( zPrefix ) fossil_free(zFile);
  }
  if( azList!=aList ){
    file_directory_list_free(azList);
  }
}

/*
** Return arguments to ORDER BY that will correctly sort the entires.
*/
static const char *xsystem_ls_orderby(int mFlags){
  static const char *zSortTypes[] = {
    "fn COLLATE NOCASE",
    "mtime DESC",
    "size DESC",
    "fn COLLATE NOCASE DESC",
    "mtime",
    "size"
  };
  int i = 0;
  if( mFlags & LS_MTIME ) i = 1;
  if( mFlags & LS_SIZE )  i = 2;
  if( mFlags & LS_REVERSE ) i += 3;
  return zSortTypes[i];
}

/*
** color(fn,mode)
**
** SQL function to colorize a filename based on its mode.
*/
static void colorNameFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zName = (const char*)sqlite3_value_text(argv[0]);
  int iMode = sqlite3_value_int(argv[1]);
  sqlite3_str *pOut;
  if( zName==0 ) return;
  pOut = sqlite3_str_new(0);
#ifdef _WIN32
  if( sqlite3_strlike("%.exe",zName,0)==0 ) iMode |= 0111;
#endif
  if( iMode & 040000 ){
    /* A directory */
    sqlite3_str_appendall(pOut, "\033[1;34m");
  }else if( iMode & 0100 ){
    /* Executable */
    sqlite3_str_appendall(pOut, "\033[1;32m");
  }
  sqlite3_str_appendall(pOut, zName);
  if( (iMode & 040100)!=0 ){
    sqlite3_str_appendall(pOut, "\033[0m");
  }
  sqlite3_result_text(context, sqlite3_str_value(pOut), -1, SQLITE_TRANSIENT);
  sqlite3_str_free(pOut);
}
/* Alternative implementation that does *not* introduce color */
static void nocolorNameFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  sqlite3_result_value(context, argv[0]);
}



/*
** Show ls output information for content in the LS table
*/
static void xsystem_ls_render(
  sqlite3 *db,
  int mFlags
){
  sqlite3_stmt *pStmt;
  if( mFlags & LS_COLOR ){
    sqlite3_create_function(db, "color",2,SQLITE_UTF8,0,colorNameFunc,0,0);
  }else{
    sqlite3_create_function(db, "color",2,SQLITE_UTF8,0,nocolorNameFunc,0,0);
  }
  if( (mFlags & LS_LONG)!=0 ){
    /* Long mode */
    char *zSql;
    int szSz = 8;
    sqlite3_prepare_v2(db, "SELECT length(max(size)) FROM ls", -1, &pStmt, 0);
    if( sqlite3_step(pStmt)==SQLITE_ROW ){
      szSz = sqlite3_column_int(pStmt, 0);
    }
    sqlite3_finalize(pStmt);
    pStmt = 0;
    zSql = mprintf(
         "SELECT mode, size, datetime(mtime,'unixepoch'), color(fn,mode)"
         " FROM ls ORDER BY %s",
         xsystem_ls_orderby(mFlags));
    sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
    while( sqlite3_step(pStmt)==SQLITE_ROW ){
      char zMode[12];
      const char *zName = (const char*)sqlite3_column_text(pStmt, 3);
      int mode = sqlite3_column_int(pStmt, 0);
#ifdef _WIN32
      memcpy(zMode, "-rw-", 5);
      if( mode & 040000 ){
        zMode[0] = 'd';
        zMode[3] = 'x';
      }else if( sqlite3_strlike("%.EXE",zName,0)==0 ){
        zMode[3] = 'x';
      }
#else
      memcpy(zMode, "----------", 11);
      if( mode & 040000 ) zMode[0] = 'd';
      if( mode & 0400 ) zMode[1] = 'r';
      if( mode & 0200 ) zMode[2] = 'w';
      if( mode & 0100 ) zMode[3] = 'x';
      if( mode & 0040 ) zMode[4] = 'r';
      if( mode & 0020 ) zMode[5] = 'w';
      if( mode & 0010 ) zMode[6] = 'x';
      if( mode & 0004 ) zMode[7] = 'r';
      if( mode & 0002 ) zMode[8] = 'w';
      if( mode & 0001 ) zMode[9] = 'x';
#endif
      fossil_print("%s %*lld %s %s\n",
         zMode,
         szSz,
         sqlite3_column_int64(pStmt, 1),
         sqlite3_column_text(pStmt, 2),
         zName);
    }
    sqlite3_finalize(pStmt);
  }else if( (mFlags & LS_COMMA)!=0 ){
    /* Comma-separate list */
    int mx = terminal_get_width(80);
    int sumW = 0;
    char *zSql;
    zSql = mprintf("SELECT color(fn,mode), dlen FROM ls ORDER BY %s",
                   xsystem_ls_orderby(mFlags));
    sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
    while( sqlite3_step(pStmt)==SQLITE_ROW ){
      const char *z = (const char*)sqlite3_column_text(pStmt, 0);
      int w = sqlite3_column_int(pStmt, 1);
      if( sumW==0 ){
        fossil_print("%s", z);
        sumW = w;
      }else if( sumW + w + 2 >= mx ){
        fossil_print("\n%s", z);
        sumW = w;
      }else{
        fossil_print(", %s", z);
        sumW += w+2;
      }
    }
    fossil_free(zSql);
    sqlite3_finalize(pStmt);
    if( sumW>0 ) fossil_print("\n");
  }else{
    /* Column mode with just filenames */
    sqlite3_qrf_spec spec;
    char *zSql;
    memset(&spec, 0, sizeof(spec));
    spec.iVersion = 1;
    spec.xWrite = xsystem_write;
    spec.eStyle = QRF_STYLE_Column;
    spec.bTitles = QRF_No;
    spec.eEsc = QRF_No;
    if( mFlags & LS_COLUMNS ){
      spec.nScreenWidth = terminal_get_width(80);
      spec.bSplitColumn = QRF_Yes;
    }
    zSql = mprintf("SELECT color(fn,mode) FROM ls ORDER BY %s",
                   xsystem_ls_orderby(mFlags));
    sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
    fossil_free(zSql);
    sqlite3_format_query_result(pStmt, &spec, 0);
    sqlite3_finalize(pStmt);
  }
  sqlite3_exec(db, "DELETE FROM ls;", 0, 0, 0);
}

/* List files "ls"
** Options:
**
**    -a            Show files that begin with "."
**    -C            List by colums
**    --color=WHEN  Colorize output?
**    -d            Show just directory names, not content
**    -l            Long listing
**    -m            Comma-separated list
**    -r            Reverse sort
**    -S            Sort by size, largest first
**    -t            Sort by mtime, newest first
*/
void xsystem_ls(int argc, char **argv){
  int i, rc;
  sqlite3 *db;
  sqlite3_stmt *pStmt = 0;
  int mFlags = 0;
  int nFile = 0;
  int nDir = 0;
  int bAutoColor = 1;
  int needBlankLine = 0;
  rc = sqlite3_open(":memory:", &db);
  if( rc || db==0 ){
    fossil_fatal("Cannot open in-memory database");
  }
  sqlite3_exec(db, "CREATE TABLE ls(fn,mtime,size,mode,dlen);", 0,0,0);
  rc = sqlite3_prepare_v2(db, "INSERT INTO ls VALUES(?1,?2,?3,?4,?5)",
                          -1, &pStmt, 0);
  if( rc || db==0 ){
    fossil_fatal("Cannot prepare INSERT statement");
  }
  for(i=1; i<argc; i++){
    const char *z = argv[i];
    if( z[0]=='-' ){
      if( z[1]=='-' ){
        if( strncmp(z,"--color",7)==0 ){
          if( z[7]==0 || strcmp(&z[7],"=always")==0 ){
            mFlags |= LS_COLOR;
          }else if( strcmp(&z[7],"=never")==0 ){
            bAutoColor = 0;
          }
        }else{
          fossil_fatal("unknown option: %s", z);
        }
      }else{
        int k;
        for(k=1; z[k]; k++){
          switch( z[k] ){
            case 'a':   mFlags |= LS_ALL;      break;
            case 'd':   mFlags |= LS_DIRONLY;  break;
            case 'l':   mFlags |= LS_LONG;     break;
            case 'm':   mFlags |= LS_COMMA;    break;
            case 'r':   mFlags |= LS_REVERSE;  break;
            case 'S':   mFlags |= LS_SIZE;     break;
            case 't':   mFlags |= LS_MTIME;    break;
            case 'C':   mFlags |= LS_COLUMNS;  break;
            default: {
              fossil_fatal("unknown option: -%c", z[k]);
            }
          }
        }
      }
    }else{
      if( (mFlags & LS_DIRONLY)==0 && file_isdir(z, ExtFILE)==1 ){
        nDir++;
      }else{
        nFile++;
        xsystem_ls_insert(pStmt, z, mFlags);
      }
    }
  }
  if( fossil_isatty(1) ){
    if( bAutoColor ) mFlags |= LS_COLOR;
    mFlags |= LS_COLUMNS;
  }
  if( nFile>0 ){
    xsystem_ls_render(db, mFlags);
    needBlankLine = 1;
  }else if( nDir==0 ){
    xsystem_ls_insert(pStmt, ".", mFlags);
    xsystem_ls_render(db, mFlags);
  }
  if( nDir>0 ){
    for(i=1; i<argc; i++){
      const char *z = argv[i];
      if( z[0]=='-' ) continue;
      if( file_isdir(z, ExtFILE)!=1 ) continue;
      if( needBlankLine ){
        fossil_print("\n");
        needBlankLine = 0;
      }
      fossil_print("%s:\n", z);
      xsystem_ls_insert(pStmt, z, mFlags);
      xsystem_ls_render(db, mFlags);
    }
  }
  sqlite3_finalize(pStmt);
  sqlite3_close(db);
}

/*
** Available system commands.
*/
typedef struct XSysCmd XSysCmd;
static struct XSysCmd {
  const char *zName;
  void (*xFunc)(int,char**);
  const char *zHelp;
} aXSysCmd[] = {
  { "date", xsystem_date,
    "\n"
    "Show the current system time and date\n"
  },
  { "ls", xsystem_ls,
    "[OPTIONS] [PATH] ...\n"
    "Options:\n"
    "   -a   Show files that begin with '.'\n"
    "   -C   Split columns\n"
    "   -d   Show just directory names, not content\n"
    "   -l   Long listing\n"
    "   -m   Comma-separated list\n"
    "   -r   Reverse sort order\n"
    "   -S   Sort by size, largest first\n"
    "   -t   Sort by mtime, newest first\n"
    "   --color[=WHEN]  Colorize output?\n"
  },
  { "pwd", xsystem_pwd,
    "\n"
    "Show the Present Working Directory name\n"
  },
  { "stty", xsystem_stty,
    "\n"
    "Show the size of the TTY\n"
  },
  { "which", xsystem_which,
    "EXE ...\n"
    "Show the location on PATH of executables EXE\n"
    "Options:\n"
    "   -a     Show all path locations rather than just the first\n"
  },
};

/*
** COMMAND: system
**
** Usage: %fossil system COMMAND ARGS...
**
** Often abbreviated as just "fossil sys", this command provides primative,
** low-level unix-like commands for use on systems that lack those commands
** natively.
**
** Type "fossil sys help" for a list of available commands.
**
** Type "fossil sys help COMMAND" for detailed help on a particular
** command.
*/
void xsystem_cmd(void){
  int i;
  const char *zCmd;
  int bHelp = 0;
  if( g.argc<=2 || (g.argc==3 && fossil_strcmp(g.argv[2],"help")==0) ){
    fossil_print("Available commands:\n");
    for(i=0; i<count(aXSysCmd); i++){
      if( (i%4)==3 || i==count(aXSysCmd)-1 ){
        fossil_print("  %s\n", aXSysCmd[i].zName);
      }else{
        fossil_print("  %-12s", aXSysCmd[i].zName);
      }
    }
    return;
  }
  zCmd = g.argv[2];
  if( fossil_strcmp(zCmd, "help")==0 ){
    bHelp = 1;
    zCmd = g.argv[3]; 
  }
  for(i=0; i<count(aXSysCmd); i++){
    if( fossil_strcmp(zCmd,aXSysCmd[i].zName)==0 ){
      if( !bHelp ){
        aXSysCmd[i].xFunc(g.argc-2, g.argv+2);
      }else{
        fossil_print("Usage: fossil system %s %s", zCmd, aXSysCmd[i].zHelp);
      }
      return;
    }
  }
  fossil_fatal("Unknown system command \"%s\"."
          " Use \"%s system help\" for a list of available commands",
          zCmd, g.argv[0]);
}
