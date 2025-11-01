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
      azList = 0;
      nList = file_directory_list(zName, 0, (mFlags & 0x08)==0, 0, &azList);
      zPrefix = fossil_strcmp(zName,".") ? zName : 0;
      break;
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
    sqlite3_bind_text(pStmt, 1, zFile, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(pStmt, 2, mtime);
    sqlite3_bind_int64(pStmt, 3, sz);
    sqlite3_bind_int(pStmt, 4, mode);
    sqlite3_step(pStmt);
    sqlite3_reset(pStmt);
    if( zPrefix ) fossil_free(zFile);
  }
  if( azList!=aList ){
    file_directory_list_free(azList);
  }
}

/*
** Show ls output information for content in the LS table
*/
static void xsystem_ls_render(
  sqlite3 *db,
  int mFlags
){
  sqlite3_stmt *pStmt;
  int bDesc = (mFlags & 0x02)!=0;
  if( mFlags & 0x04 ) bDesc = !bDesc;
  if( (mFlags & 0x01)!=0 ){
    /* Long mode */
    char *zSql;
    zSql = mprintf(
         "SELECT mode, size, strftime('%%Y-%%m-%%d %%H:%%M',"
                "mtime,'unixepoch'), fn"
         " FROM ls ORDER BY %s %s",
         (mFlags & 0x04)!=0 ? "mtime" : "fn",
         bDesc ? "DESC" : "ASC");
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
      fossil_print("%s %12lld %s %s\n",
         zMode,
         sqlite3_column_int64(pStmt, 1),
         sqlite3_column_text(pStmt, 2),
         zName);
    }
    sqlite3_finalize(pStmt);
  }else{
    /* Column mode with just filenames */
    int nCol, mxWidth, iRow, nSp, nRow;
    char *zSql;
    char *zOrderBy;
    sqlite3_prepare_v2(db, "SELECT max(length(fn)),count(*) FROM ls",-1,
                           &pStmt,0);
    if( sqlite3_step(pStmt)==SQLITE_ROW ){
      mxWidth = sqlite3_column_int(pStmt,0);
      nCol = (terminal_get_width(80)+1)/(mxWidth+2);
      if( nCol<1 ) nCol = 1;
      nRow = (sqlite3_column_int(pStmt,1)+nCol-1)/nCol;
    }else{
      nCol = 1;
      mxWidth = 100;
      nRow = 2000000;
    }
    sqlite3_finalize(pStmt);
    zOrderBy = mprintf("%s %s", (mFlags & 0x04)!=0 ? "mtime": "fn",
                                         bDesc ? "DESC" : "ASC");
    zSql = mprintf("WITH sfn(ii,fn,mtime) AS "
                   "(SELECT row_number()OVER(ORDER BY %s)-1,fn,mtime FROM ls)"
                   "SELECT ii/%d,ii%%%d, fn FROM sfn ORDER BY 2,1",
                   zOrderBy, nRow, nRow);
    fossil_free(zOrderBy);
    sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
    nSp = 0;
    iRow = -1;
    while( sqlite3_step(pStmt)==SQLITE_ROW ){
      const char *zFN = (const char*)sqlite3_column_text(pStmt, 2);
      int thisRow = sqlite3_column_int(pStmt,1);
      if( iRow!=thisRow ){
        if( iRow>=0 ) fossil_print("\n");
        iRow = thisRow;
      }else{
        if( nSp ) fossil_print("%*s",nSp,"");
      }
      fossil_print("%s", zFN);
      nSp = mxWidth - (int)strlen(zFN) + 2;
    }
    fossil_print("\n");
    sqlite3_finalize(pStmt);
  }
  sqlite3_exec(db, "DELETE FROM ls;", 0, 0, 0);
}

/* List files "ls"
** Options:
**
**    -a            Show files that begin with "."
**    -l            Long listing
**    -r            Reverse sort
**    -t            Sort by mtime
*/
void xsystem_ls(int argc, char **argv){
  int i, rc;
  sqlite3 *db;
  sqlite3_stmt *pStmt = 0;
  int mFlags = 0;
  int nFile = 0;
  int nDir = 0;
  int needBlankLine = 0;
  rc = sqlite3_open(":memory:", &db);
  if( rc || db==0 ){
    fossil_fatal("Cannot open in-memory database");
  }
  sqlite3_exec(db, "CREATE TABLE ls(fn,mtime,size,mode);", 0,0,0);
  rc = sqlite3_prepare_v2(db, "INSERT INTO ls VALUES(?1,?2,?3,?4)",
                          -1, &pStmt, 0);
  if( rc || db==0 ){
    fossil_fatal("Cannot prepare INSERT statement");
  }
  for(i=1; i<argc; i++){
    const char *z = argv[i];
    if( z[0]=='-' ){
      int k;
      for(k=1; z[k]; k++){
        if( z[k]=='l' ){
          mFlags |= 0x01;
        }else if( z[k]=='r' ){
          mFlags |= 0x02;
        }else if( z[k]=='t' ){
          mFlags |= 0x04;
        }else if( z[k]=='a' ){
          mFlags |= 0x08;
        }else{
          fossil_fatal("unknown option: -%c", z[k]);
        }
      }
    }else{
      if( file_isdir(z, ExtFILE)==1 ){
        nDir++;
      }else{
        nFile++;
        xsystem_ls_insert(pStmt, z, mFlags);
      }
    }
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
    "  -l     Long format\n"
    "  -r     Reverse sort order\n"
    "  -t     Sort by mtime\n"
  },
  { "pwd", xsystem_pwd,
    "\n"
    "Show the Present Working Directory name\n"
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
