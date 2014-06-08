/*
** Copyright (c) 2008 D. Richard Hipp
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
** This file contains an interface between the TH scripting language
** (an independent project) and fossil.
*/
#include "config.h"
#include "th_main.h"
#include "sqlite3.h"

#if INTERFACE
/*
** Flag parameters to the Th_FossilInit() routine used to control the
** interpreter creation and initialization process.
*/
#define TH_INIT_NONE        ((u32)0x00000000) /* No flags. */
#define TH_INIT_NEED_CONFIG ((u32)0x00000001) /* Open configuration first? */
#define TH_INIT_FORCE_TCL   ((u32)0x00000002) /* Force Tcl to be enabled? */
#define TH_INIT_FORCE_RESET ((u32)0x00000004) /* Force TH commands re-added? */
#define TH_INIT_FORCE_SETUP ((u32)0x00000008) /* Force eval of setup script? */
#define TH_INIT_DEFAULT     (TH_INIT_NONE)    /* Default flags. */
#define TH_INIT_HOOK        (TH_INIT_NEED_CONFIG | TH_INIT_FORCE_SETUP)
#endif

#ifdef FOSSIL_ENABLE_TH1_HOOKS
/*
** These are the "well-known" TH1 error messages that occur when no hook is
** registered to be called prior to executing a command or processing a web
** page, respectively.  If one of these errors is seen, it will not be sent
** or displayed to the remote user or local interactive user, respectively.
*/
#define NO_COMMAND_HOOK_ERROR "no such command:  command_hook"
#define NO_WEBPAGE_HOOK_ERROR "no such command:  webpage_hook"
#endif

/*
** Global variable counting the number of outstanding calls to malloc()
** made by the th1 implementation. This is used to catch memory leaks
** in the interpreter. Obviously, it also means th1 is not threadsafe.
*/
static int nOutstandingMalloc = 0;

/*
** Implementations of malloc() and free() to pass to the interpreter.
*/
static void *xMalloc(unsigned int n){
  void *p = fossil_malloc(n);
  if( p ){
    nOutstandingMalloc++;
  }
  return p;
}
static void xFree(void *p){
  if( p ){
    nOutstandingMalloc--;
  }
  free(p);
}
static Th_Vtab vtab = { xMalloc, xFree };

/*
** Returns the number of outstanding TH1 memory allocations.
*/
int Th_GetOutstandingMalloc(){
  return nOutstandingMalloc;
}

/*
** Generate a TH1 trace message if debugging is enabled.
*/
void Th_Trace(const char *zFormat, ...){
  va_list ap;
  va_start(ap, zFormat);
  blob_vappendf(&g.thLog, zFormat, ap);
  va_end(ap);
}

/*
** Checks if the TH1 trace log needs to be enabled.  If so, prepares
** it for use.
*/
void Th_InitTraceLog(){
  g.thTrace = find_option("th-trace", 0, 0)!=0;
  if( g.thTrace ){
    blob_zero(&g.thLog);
  }
}

/*
** Prints the entire contents of the TH1 trace log to the standard
** output channel.
*/
void Th_PrintTraceLog(){
  if( g.thTrace ){
    fossil_print("\n------------------ BEGIN TRACE LOG ------------------\n");
    fossil_print("%s", blob_str(&g.thLog));
    fossil_print("\n------------------- END TRACE LOG -------------------\n");
  }
}

/*
** TH command:      httpize STRING
**
** Escape all characters of STRING which have special meaning in URI
** components. Return a new string result.
*/
static int httpizeCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  char *zOut;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "httpize STRING");
  }
  zOut = httpize((char*)argv[1], argl[1]);
  Th_SetResult(interp, zOut, -1);
  free(zOut);
  return TH_OK;
}

/*
** True if output is enabled.  False if disabled.
*/
static int enableOutput = 1;

/*
** TH command:     enable_output BOOLEAN
**
** Enable or disable the puts and hputs commands.
*/
static int enableOutputCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  int rc;
  if( argc<2 || argc>3 ){
    return Th_WrongNumArgs(interp, "enable_output [LABEL] BOOLEAN");
  }
  rc = Th_ToInt(interp, argv[argc-1], argl[argc-1], &enableOutput);
  if( g.thTrace ){
    Th_Trace("enable_output {%.*s} -> %d<br>\n", argl[1],argv[1],enableOutput);
  }
  return rc;
}

/*
** Return a name for a TH1 return code.
*/
const char *Th_ReturnCodeName(int rc, int nullIfOk){
  static char zRc[32];
  switch( rc ){
    case TH_OK:       return nullIfOk ? 0 : "TH_OK";
    case TH_ERROR:    return "TH_ERROR";
    case TH_BREAK:    return "TH_BREAK";
    case TH_RETURN:   return "TH_RETURN";
    case TH_CONTINUE: return "TH_CONTINUE";
    default: {
      sqlite3_snprintf(sizeof(zRc),zRc,"return code %d",rc);
    }
  }
  return zRc;
}

/*
** Send text to the appropriate output:  Either to the console
** or to the CGI reply buffer.  Escape all characters with special
** meaning to HTML if the encode parameter is true.
*/
static void sendText(const char *z, int n, int encode){
  if( enableOutput && n ){
    if( n<0 ) n = strlen(z);
    if( encode ){
      z = htmlize(z, n);
      n = strlen(z);
    }
    if( g.cgiOutput ){
      cgi_append_content(z, n);
    }else{
      fwrite(z, 1, n, stdout);
      fflush(stdout);
    }
    if( encode ) free((char*)z);
  }
}

static void sendError(const char *z, int n, int forceCgi){
  int savedEnable = enableOutput;
  enableOutput = 1;
  if( forceCgi || g.cgiOutput ){
    sendText("<hr><p class=\"thmainError\">", -1, 0);
  }
  sendText("ERROR: ", -1, 0);
  sendText((char*)z, n, 1);
  sendText(forceCgi || g.cgiOutput ? "</p>" : "\n", -1, 0);
  enableOutput = savedEnable;
}

/*
** TH command:     puts STRING
** TH command:     html STRING
**
** Output STRING escaped for HTML (html) or unchanged (puts).  
*/
static int putsCmd(
  Th_Interp *interp, 
  void *pConvert, 
  int argc, 
  const char **argv, 
  int *argl
){
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "puts STRING");
  }
  sendText((char*)argv[1], argl[1], *(unsigned int*)pConvert);
  return TH_OK;
}

/*
** TH command:      wiki STRING
**
** Render the input string as wiki.
*/
static int wikiCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  int flags = WIKI_INLINE | WIKI_NOBADLINKS | *(unsigned int*)p;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "wiki STRING");
  }
  if( enableOutput ){
    Blob src;
    blob_init(&src, (char*)argv[1], argl[1]);
    wiki_convert(&src, 0, flags);
    blob_reset(&src);
  }
  return TH_OK;
}

/*
** TH command:      htmlize STRING
**
** Escape all characters of STRING which have special meaning in HTML.
** Return a new string result.
*/
static int htmlizeCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  char *zOut;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "htmlize STRING");
  }
  zOut = htmlize((char*)argv[1], argl[1]);
  Th_SetResult(interp, zOut, -1);
  free(zOut);
  return TH_OK;
}

/*
** TH command:      date
**
** Return a string which is the current time and date.  If the
** -local option is used, the date appears using localtime instead
** of UTC.
*/
static int dateCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  char *zOut;
  if( argc>=2 && argl[1]==6 && memcmp(argv[1],"-local",6)==0 ){
    zOut = db_text("??", "SELECT datetime('now'%s)", timeline_utc());
  }else{
    zOut = db_text("??", "SELECT datetime('now')");
  }
  Th_SetResult(interp, zOut, -1);
  free(zOut);
  return TH_OK;
}

/*
** TH command:     hascap STRING...
**
** Return true if the user has all of the capabilities listed in STRING.
*/
static int hascapCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  int rc = 0, i;
  if( argc<2 ){
    return Th_WrongNumArgs(interp, "hascap STRING ...");
  }
  for(i=1; i<argc && rc==0; i++){
    rc = login_has_capability((char*)argv[i],argl[i]);
  }
  if( g.thTrace ){
    Th_Trace("[hascap %#h] => %d<br />\n", argl[1], argv[1], rc);
  }
  Th_SetResultInt(interp, rc);
  return TH_OK;
}

/*
** TH command:     hasfeature STRING
**
** Return true if the fossil binary has the given compile-time feature
** enabled. The set of features includes:
**
** "ssl"             = FOSSIL_ENABLE_SSL
** "tcl"             = FOSSIL_ENABLE_TCL
** "useTclStubs"     = USE_TCL_STUBS
** "tclStubs"        = FOSSIL_ENABLE_TCL_STUBS
** "tclPrivateStubs" = FOSSIL_ENABLE_TCL_PRIVATE_STUBS
** "json"            = FOSSIL_ENABLE_JSON
** "markdown"        = FOSSIL_ENABLE_MARKDOWN
**
*/
static int hasfeatureCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  int rc = 0;
  char const * zArg;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "hasfeature STRING");
  }
  zArg = (char const*)argv[1];
  if(NULL==zArg){
    /* placeholder for following ifdefs... */
  }
#if defined(FOSSIL_ENABLE_SSL)
  else if( 0 == fossil_strnicmp( zArg, "ssl\0", 4 ) ){
    rc = 1;
  }
#endif
#if defined(FOSSIL_ENABLE_TCL)
  else if( 0 == fossil_strnicmp( zArg, "tcl\0", 4 ) ){
    rc = 1;
  }
#endif
#if defined(USE_TCL_STUBS)
  else if( 0 == fossil_strnicmp( zArg, "useTclStubs\0", 12 ) ){
    rc = 1;
  }
#endif
#if defined(FOSSIL_ENABLE_TCL_STUBS)
  else if( 0 == fossil_strnicmp( zArg, "tclStubs\0", 9 ) ){
    rc = 1;
  }
#endif
#if defined(FOSSIL_ENABLE_TCL_PRIVATE_STUBS)
  else if( 0 == fossil_strnicmp( zArg, "tclPrivateStubs\0", 16 ) ){
    rc = 1;
  }
#endif
#if defined(FOSSIL_ENABLE_JSON)
  else if( 0 == fossil_strnicmp( zArg, "json\0", 5 ) ){
    rc = 1;
  }
#endif
  else if( 0 == fossil_strnicmp( zArg, "markdown\0", 9 ) ){
    rc = 1;
  }
  if( g.thTrace ){
    Th_Trace("[hasfeature %#h] => %d<br />\n", argl[1], zArg, rc);
  }
  Th_SetResultInt(interp, rc);
  return TH_OK;
}


/*
** TH command:     tclReady
**
** Return true if the fossil binary has the Tcl integration feature
** enabled and it is currently available for use by TH1 scripts.
**
*/
static int tclReadyCmd(
  Th_Interp *interp,
  void *p,
  int argc,
  const char **argv,
  int *argl
){
  int rc = 0;
  if( argc!=1 ){
    return Th_WrongNumArgs(interp, "tclReady");
  }
#if defined(FOSSIL_ENABLE_TCL)
  if( g.tcl.interp ){
    rc = 1;
  }
#endif
  if( g.thTrace ){
    Th_Trace("[tclReady] => %d<br />\n", rc);
  }
  Th_SetResultInt(interp, rc);
  return TH_OK;
}


/*
** TH command:     anycap STRING
**
** Return true if the user has any one of the capabilities listed in STRING.
*/
static int anycapCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  int rc = 0;
  int i;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "anycap STRING");
  }
  for(i=0; rc==0 && i<argl[1]; i++){
    rc = login_has_capability((char*)&argv[1][i],1);
  }
  if( g.thTrace ){
    Th_Trace("[hascap %#h] => %d<br />\n", argl[1], argv[1], rc);
  }
  Th_SetResultInt(interp, rc);
  return TH_OK;
}

/*
** TH1 command:  combobox NAME TEXT-LIST NUMLINES
**
** Generate an HTML combobox.  NAME is both the name of the
** CGI parameter and the name of a variable that contains the
** currently selected value.  TEXT-LIST is a list of possible
** values for the combobox.  NUMLINES is 1 for a true combobox.
** If NUMLINES is greater than one then the display is a listbox
** with the number of lines given.
*/
static int comboboxCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "combobox NAME TEXT-LIST NUMLINES");
  }
  if( enableOutput ){
    int height;
    Blob name;
    int nValue;
    const char *zValue;
    char *z, *zH;
    int nElem;
    int *aszElem;
    char **azElem;
    int i;

    if( Th_ToInt(interp, argv[3], argl[3], &height) ) return TH_ERROR;
    Th_SplitList(interp, argv[2], argl[2], &azElem, &aszElem, &nElem);
    blob_init(&name, (char*)argv[1], argl[1]);
    zValue = Th_Fetch(blob_str(&name), &nValue);
    zH = htmlize(blob_buffer(&name), blob_size(&name));
    z = mprintf("<select id=\"%s\" name=\"%s\" size=\"%d\">", zH, zH, height);
    free(zH);
    sendText(z, -1, 0);
    free(z);
    blob_reset(&name);
    for(i=0; i<nElem; i++){
      zH = htmlize((char*)azElem[i], aszElem[i]);
      if( zValue && aszElem[i]==nValue 
             && memcmp(zValue, azElem[i], nValue)==0 ){
        z = mprintf("<option value=\"%s\" selected=\"selected\">%s</option>",
                     zH, zH);
      }else{
        z = mprintf("<option value=\"%s\">%s</option>", zH, zH);
      }
      free(zH);
      sendText(z, -1, 0);
      free(z);
    }
    sendText("</select>", -1, 0);
    Th_Free(interp, azElem);
  }
  return TH_OK;
}

/*
** TH1 command:     linecount STRING MAX MIN
**
** Return one more than the number of \n characters in STRING.  But
** never return less than MIN or more than MAX.
*/
static int linecntCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  const char *z;
  int size, n, i;
  int iMin, iMax;
  if( argc!=4 ){
    return Th_WrongNumArgs(interp, "linecount STRING MAX MIN");
  }
  if( Th_ToInt(interp, argv[2], argl[2], &iMax) ) return TH_ERROR;
  if( Th_ToInt(interp, argv[3], argl[3], &iMin) ) return TH_ERROR;
  z = argv[1];
  size = argl[1];
  for(n=1, i=0; i<size; i++){
    if( z[i]=='\n' ){
      n++;
      if( n>=iMax ) break;
    }
  }
  if( n<iMin ) n = iMin;
  if( n>iMax ) n = iMax;
  Th_SetResultInt(interp, n);
  return TH_OK;
}

/*
** TH1 command:     repository ?BOOLEAN?
**
** Return the fully qualified file name of the open repository or an empty
** string if one is not currently open.  Optionally, it will attempt to open
** the repository if the boolean argument is non-zero.
*/
static int repositoryCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  if( argc!=1 && argc!=2 ){
    return Th_WrongNumArgs(interp, "repository ?BOOLEAN?");
  }
  if( argc==2 ){
    int openRepository = 0;
    if( Th_ToInt(interp, argv[1], argl[1], &openRepository) ){
      return TH_ERROR;
    }
    if( openRepository ) db_find_and_open_repository(OPEN_OK_NOT_FOUND, 0);
  }
  Th_SetResult(interp, g.zRepositoryName, -1);
  return TH_OK;
}

#ifdef _WIN32
# include <windows.h>
#else
# include <sys/time.h>
# include <sys/resource.h>
#endif

/*
** Get user and kernel times in microseconds.
*/
static void getCpuTimes(sqlite3_uint64 *piUser, sqlite3_uint64 *piKernel){
#ifdef _WIN32
  FILETIME not_used;
  FILETIME kernel_time;
  FILETIME user_time;
  GetProcessTimes(GetCurrentProcess(), &not_used, &not_used,
                  &kernel_time, &user_time);
  if( piUser ){
     *piUser = ((((sqlite3_uint64)user_time.dwHighDateTime)<<32) +
                         (sqlite3_uint64)user_time.dwLowDateTime + 5)/10;
  }
  if( piKernel ){
     *piKernel = ((((sqlite3_uint64)kernel_time.dwHighDateTime)<<32) +
                         (sqlite3_uint64)kernel_time.dwLowDateTime + 5)/10;
  }
#else
  struct rusage s;
  getrusage(RUSAGE_SELF, &s);
  if( piUser ){
    *piUser = ((sqlite3_uint64)s.ru_utime.tv_sec)*1000000 + s.ru_utime.tv_usec;
  }
  if( piKernel ){
    *piKernel = 
              ((sqlite3_uint64)s.ru_stime.tv_sec)*1000000 + s.ru_stime.tv_usec;
  }
#endif
}

/*
** TH1 command:     utime
**
** Return the number of microseconds of CPU time consumed by the current
** process in user space.
*/
static int utimeCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_uint64 x;
  char zUTime[50];
  getCpuTimes(&x, 0);
  sqlite3_snprintf(sizeof(zUTime), zUTime, "%llu", x);
  Th_SetResult(interp, zUTime, -1);
  return TH_OK;
}

/*
** TH1 command:     stime
**
** Return the number of microseconds of CPU time consumed by the current
** process in system space.
*/
static int stimeCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_uint64 x;
  char zUTime[50];
  getCpuTimes(0, &x);
  sqlite3_snprintf(sizeof(zUTime), zUTime, "%llu", x);
  Th_SetResult(interp, zUTime, -1);
  return TH_OK;
}


/*
** TH1 command:     randhex  N
**
** Return N*2 random hexadecimal digits with N<50.  If N is omitted, 
** use a value of 10.
*/
static int randhexCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  int n;
  unsigned char aRand[50];
  unsigned char zOut[100];
  if( argc!=1 && argc!=2 ){
    return Th_WrongNumArgs(interp, "repository ?BOOLEAN?");
  }
  if( argc==2 ){
    if( Th_ToInt(interp, argv[1], argl[1], &n) ){
      return TH_ERROR;
    }
    if( n<1 ) n = 1;
    if( n>sizeof(aRand) ) n = sizeof(aRand);
  }else{
    n = 10;
  }
  sqlite3_randomness(n, aRand);
  encode16(aRand, zOut, n);
  Th_SetResult(interp, (const char *)zOut, -1);
  return TH_OK;
}

/*
** TH1 command:     query SQL CODE
**
** Run the SQL query given by the SQL argument.  For each row in the result
** set, run CODE.
**
** In SQL, parameters such as $var are filled in using the value of variable
** "var".  Result values are stored in variables with the column name prior
** to each invocation of CODE.
*/
static int queryCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt *pStmt;
  int rc;
  const char *zSql;
  int nSql;
  const char *zTail;
  int n, i;
  int res = TH_OK;
  int nVar;
  char *zErr = 0;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "query SQL CODE");
  }
  if( g.db==0 ){
    Th_ErrorMessage(interp, "database is not open", 0, 0);
    return TH_ERROR;
  }
  zSql = argv[1];
  nSql = argl[1];
  while( res==TH_OK && nSql>0 ){
    zErr = 0;
    sqlite3_set_authorizer(g.db, report_query_authorizer, (void*)&zErr);
    rc = sqlite3_prepare_v2(g.db, argv[1], argl[1], &pStmt, &zTail);
    sqlite3_set_authorizer(g.db, 0, 0);
    if( rc!=0 || zErr!=0 ){
      Th_ErrorMessage(interp, "SQL error: ",
                      zErr ? zErr : sqlite3_errmsg(g.db), -1);
      return TH_ERROR;
    }
    n = (int)(zTail - zSql);
    zSql += n;
    nSql -= n;
    if( pStmt==0 ) continue;
    nVar = sqlite3_bind_parameter_count(pStmt);
    for(i=1; i<=nVar; i++){
      const char *zVar = sqlite3_bind_parameter_name(pStmt, i);
      int szVar = zVar ? th_strlen(zVar) : 0;
      if( szVar>1 && zVar[0]=='$'
       && Th_GetVar(interp, zVar+1, szVar-1)==TH_OK ){
        int nVal;
        const char *zVal = Th_GetResult(interp, &nVal);
        sqlite3_bind_text(pStmt, i, zVal, nVal, SQLITE_TRANSIENT);
      }
    }
    while( res==TH_OK && sqlite3_step(pStmt)==SQLITE_ROW ){
      int nCol = sqlite3_column_count(pStmt);
      for(i=0; i<nCol; i++){
        const char *zCol = sqlite3_column_name(pStmt, i);
        int szCol = th_strlen(zCol);
        const char *zVal = (const char*)sqlite3_column_text(pStmt, i);
        int szVal = sqlite3_column_bytes(pStmt, i);
        Th_SetVar(interp, zCol, szCol, zVal, szVal);
      }
      res = Th_Eval(interp, 0, argv[2], argl[2]);
      if( res==TH_BREAK || res==TH_CONTINUE ) res = TH_OK;
    }
    rc = sqlite3_finalize(pStmt);
    if( rc!=SQLITE_OK ){
      Th_ErrorMessage(interp, "SQL error: ", sqlite3_errmsg(g.db), -1);
      return TH_ERROR;
    }
  } 
  return res;
}

/*
** TH1 command:     setting name
**
** Gets and returns the value of the specified Fossil setting.
*/
#define SETTING_WRONGNUMARGS "setting ?-strict? ?--? name"
static int settingCmd(
  Th_Interp *interp,
  void *p,
  int argc,
  const char **argv,
  int *argl
){
  int rc;
  int strict = 0;
  int nArg = 1;
  char *zValue;
  if( argc<2 || argc>4 ){
    return Th_WrongNumArgs(interp, SETTING_WRONGNUMARGS);
  }
  if( fossil_strcmp(argv[nArg], "-strict")==0 ){
    strict = 1; nArg++;
  }
  if( fossil_strcmp(argv[nArg], "--")==0 ) nArg++;
  if( nArg+1!=argc ){
    return Th_WrongNumArgs(interp, SETTING_WRONGNUMARGS);
  }
  zValue = db_get(argv[nArg], 0);
  if( zValue!=0 ){
    Th_SetResult(interp, zValue, -1);
    rc = TH_OK;
  }else if( strict ){
    Th_ErrorMessage(interp, "no value for setting \"", argv[nArg], -1);
    rc = TH_ERROR;
  }else{
    Th_SetResult(interp, 0, 0);
    rc = TH_OK;
  }
  if( g.thTrace ){
    Th_Trace("[setting %s%#h] => %d<br />\n", strict ? "strict " : "",
             argl[nArg], argv[nArg], rc);
  }
  return rc;
}

/*
** TH1 command:     regexp ?-nocase? ?--? exp string
**
** Checks the string against the specified regular expression and returns
** non-zero if it matches.  If the regular expression is invalid or cannot
** be compiled, an error will be generated.
*/
#define REGEXP_WRONGNUMARGS "regexp ?-nocase? ?--? exp string"
static int regexpCmd(
  Th_Interp *interp,
  void *p,
  int argc,
  const char **argv,
  int *argl
){
  int rc;
  int noCase = 0;
  int nArg = 1;
  ReCompiled *pRe = 0;
  const char *zErr;
  if( argc<3 || argc>5 ){
    return Th_WrongNumArgs(interp, REGEXP_WRONGNUMARGS);
  }
  if( fossil_strcmp(argv[nArg], "-nocase")==0 ){
    noCase = 1; nArg++;
  }
  if( fossil_strcmp(argv[nArg], "--")==0 ) nArg++;
  if( nArg+2!=argc ){
    return Th_WrongNumArgs(interp, REGEXP_WRONGNUMARGS);
  }
  zErr = re_compile(&pRe, argv[nArg], noCase);
  if( !zErr ){
    Th_SetResultInt(interp, re_match(pRe,
        (const unsigned char *)argv[nArg+1], argl[nArg+1]));
    rc = TH_OK;
  }else{
    Th_SetResult(interp, zErr, -1);
    rc = TH_ERROR;
  }
  re_free(pRe);
  return rc;
}

/*
** TH command:      http ?-asynchronous? ?--? url ?payload?
**
** Perform an HTTP or HTTPS request for the specified URL.  If a
** payload is present, it will be interpreted as text/plain and
** the POST method will be used; otherwise, the GET method will
** be used.  Upon success, if the -asynchronous option is used, an
** empty string is returned as the result; otherwise, the response
** from the server is returned as the result.  Synchronous requests
** are not currently implemented.
*/
#define HTTP_WRONGNUMARGS "http ?-asynchronous? ?--? url ?payload?"
static int httpCmd(
  Th_Interp *interp,
  void *p,
  int argc,
  const char **argv,
  int *argl
){
  int nArg = 1;
  int fAsynchronous = 0;
  const char *zType, *zRegexp;
  Blob payload;
  ReCompiled *pRe = 0;
  UrlData urlData;

  if( argc<2 || argc>5 ){
    return Th_WrongNumArgs(interp, HTTP_WRONGNUMARGS);
  }
  if( fossil_strnicmp(argv[nArg], "-asynchronous", argl[nArg])==0 ){
    fAsynchronous = 1; nArg++;
  }
  if( fossil_strcmp(argv[nArg], "--")==0 ) nArg++;
  if( nArg+1!=argc && nArg+2!=argc ){
    return Th_WrongNumArgs(interp, REGEXP_WRONGNUMARGS);
  }
  memset(&urlData, '\0', sizeof(urlData));
  url_parse_local(argv[nArg], 0, &urlData);
  if( urlData.isSsh || urlData.isFile ){
    Th_ErrorMessage(interp, "url must be http:// or https://", 0, 0);
    return TH_ERROR;
  }
  zRegexp = db_get("th1-uri-regexp", 0);
  if( zRegexp && zRegexp[0] ){
    const char *zErr = re_compile(&pRe, zRegexp, 0);
    if( zErr ){
      Th_SetResult(interp, zErr, -1);
      return TH_ERROR;
    }
  }
  if( !pRe || !re_match(pRe, (const unsigned char *)urlData.canonical, -1) ){
    Th_SetResult(interp, "url not allowed", -1);
    re_free(pRe);
    return TH_ERROR;
  }
  re_free(pRe);
  blob_zero(&payload);
  if( nArg+2==argc ){
    blob_append(&payload, argv[nArg+1], argl[nArg+1]);
    zType = "POST";
  }else{
    zType = "GET";
  }
  if( fAsynchronous ){
    const char *zSep, *zParams;
    Blob hdr;
    zParams = strrchr(argv[nArg], '?');
    if( strlen(urlData.path)>0 && zParams!=argv[nArg] ){
      zSep = "";
    }else{
      zSep = "/";
    }
    blob_zero(&hdr);
    blob_appendf(&hdr, "%s %s%s%s HTTP/1.0\r\n",
                 zType, zSep, urlData.path, zParams ? zParams : "");
    if( urlData.proxyAuth ){
      blob_appendf(&hdr, "Proxy-Authorization: %s\r\n", urlData.proxyAuth);
    }
    if( urlData.passwd && urlData.user && urlData.passwd[0]=='#' ){
      char *zCredentials = mprintf("%s:%s", urlData.user, &urlData.passwd[1]);
      char *zEncoded = encode64(zCredentials, -1);
      blob_appendf(&hdr, "Authorization: Basic %s\r\n", zEncoded);
      fossil_free(zEncoded);
      fossil_free(zCredentials);
    }
    blob_appendf(&hdr, "Host: %s\r\n"
        "User-Agent: %s\r\n", urlData.hostname, get_user_agent());
    if( zType[0]=='P' ){
      blob_appendf(&hdr, "Content-Type: application/x-www-form-urlencoded\r\n"
          "Content-Length: %d\r\n\r\n", blob_size(&payload));
    }else{
      blob_appendf(&hdr, "\r\n");
    }
    if( transport_open(&urlData) ){
      Th_ErrorMessage(interp, transport_errmsg(&urlData), 0, 0);
      blob_reset(&hdr);
      blob_reset(&payload);
      return TH_ERROR;
    }
    transport_send(&urlData, &hdr);
    transport_send(&urlData, &payload);
    blob_reset(&hdr);
    blob_reset(&payload);
    transport_close(&urlData);
    Th_SetResult(interp, 0, 0); /* NOTE: Asynchronous, no results. */
    return TH_OK;
  }else{
    Th_ErrorMessage(interp,
        "synchronous requests are not yet implemented", 0, 0);
    blob_reset(&payload);
    return TH_ERROR;
  }
}

/*
** Make sure the interpreter has been initialized.  Initialize it if
** it has not been already.
**
** The interpreter is stored in the g.interp global variable.
*/
void Th_FossilInit(u32 flags){
  int wasInit = 0;
  int needConfig = flags & TH_INIT_NEED_CONFIG;
  int forceReset = flags & TH_INIT_FORCE_RESET;
  int forceTcl = flags & TH_INIT_FORCE_TCL;
  int forceSetup = flags & TH_INIT_FORCE_SETUP;
  static unsigned int aFlags[] = { 0, 1, WIKI_LINKSONLY };
  static struct _Command {
    const char *zName;
    Th_CommandProc xProc;
    void *pContext;
  } aCommand[] = {
    {"anycap",        anycapCmd,            0},
    {"combobox",      comboboxCmd,          0},
    {"date",          dateCmd,              0},
    {"decorate",      wikiCmd,              (void*)&aFlags[2]},
    {"enable_output", enableOutputCmd,      0},
    {"httpize",       httpizeCmd,           0},
    {"hascap",        hascapCmd,            0},
    {"hasfeature",    hasfeatureCmd,        0},
    {"html",          putsCmd,              (void*)&aFlags[0]},
    {"htmlize",       htmlizeCmd,           0},
    {"http",          httpCmd,              0},
    {"linecount",     linecntCmd,           0},
    {"puts",          putsCmd,              (void*)&aFlags[1]},
    {"query",         queryCmd,             0},
    {"randhex",       randhexCmd,           0},
    {"regexp",        regexpCmd,            0},
    {"repository",    repositoryCmd,        0},
    {"setting",       settingCmd,           0},
    {"tclReady",      tclReadyCmd,          0},
    {"stime",         stimeCmd,             0},
    {"utime",         utimeCmd,             0},
    {"wiki",          wikiCmd,              (void*)&aFlags[0]},
    {0, 0, 0}
  };
  if( needConfig ){
    /*
    ** This function uses several settings which may be defined in the
    ** repository and/or the global configuration.  Since the caller
    ** passed a non-zero value for the needConfig parameter, make sure
    ** the necessary database connections are open prior to continuing.
    */
    db_find_and_open_repository(OPEN_ANY_SCHEMA | OPEN_OK_NOT_FOUND, 0);
    db_open_config(0);
  }
  if( forceReset || forceTcl || g.interp==0 ){
    int created = 0;
    int i;
    if( g.interp==0 ){
      g.interp = Th_CreateInterp(&vtab);
      created = 1;
    }
    if( forceReset || created ){
      th_register_language(g.interp);     /* Basic scripting commands. */
    }
#ifdef FOSSIL_ENABLE_TCL
    if( forceTcl || fossil_getenv("TH1_ENABLE_TCL")!=0 ||
        db_get_boolean("tcl", 0) ){
      if( !g.tcl.setup ){
        g.tcl.setup = db_get("tcl-setup", 0); /* Grab Tcl setup script. */
      }
      th_register_tcl(g.interp, &g.tcl);  /* Tcl integration commands. */
    }
#endif
    for(i=0; i<sizeof(aCommand)/sizeof(aCommand[0]); i++){
      if ( !aCommand[i].zName || !aCommand[i].xProc ) continue;
      Th_CreateCommand(g.interp, aCommand[i].zName, aCommand[i].xProc,
                       aCommand[i].pContext, 0);
    }
  }else{
    wasInit = 1;
  }
  if( forceSetup || !wasInit ){
    int rc = TH_OK;
    if( !g.th1Setup ){
      g.th1Setup = db_get("th1-setup", 0); /* Grab TH1 setup script. */
    }
    if( g.th1Setup ){
      rc = Th_Eval(g.interp, 0, g.th1Setup, -1);
      if( rc==TH_ERROR ){
        int nResult = 0;
        char *zResult = (char*)Th_GetResult(g.interp, &nResult);
        sendError(zResult, nResult, 0);
      }
    }
    if( g.thTrace ){
      Th_Trace("th1-setup {%h} => %h<br />\n", g.th1Setup,
               Th_ReturnCodeName(rc, 0));
    }
  }
}

/*
** Store a string value in a variable in the interpreter.
*/
void Th_Store(const char *zName, const char *zValue){
  Th_FossilInit(TH_INIT_DEFAULT);
  if( zValue ){
    if( g.thTrace ){
      Th_Trace("set %h {%h}<br />\n", zName, zValue);
    }
    Th_SetVar(g.interp, zName, -1, zValue, strlen(zValue));
  }
}

/*
** Store an integer value in a variable in the interpreter.
*/
void Th_StoreInt(const char *zName, int iValue){
  Blob value;
  char *zValue;
  Th_FossilInit(TH_INIT_DEFAULT);
  blob_zero(&value);
  blob_appendf(&value, "%d", iValue);
  zValue = blob_str(&value);
  if( g.thTrace ){
    Th_Trace("set %h {%h}<br />\n", zName, zValue);
  }
  Th_SetVar(g.interp, zName, -1, zValue, strlen(zValue));
  blob_reset(&value);
}

/*
** Unset a variable.
*/
void Th_Unstore(const char *zName){
  if( g.interp ){
    Th_UnsetVar(g.interp, (char*)zName, -1);
  }
}

/*
** Retrieve a string value from the interpreter.  If no such
** variable exists, return NULL.
*/
char *Th_Fetch(const char *zName, int *pSize){
  int rc;
  Th_FossilInit(TH_INIT_DEFAULT);
  rc = Th_GetVar(g.interp, (char*)zName, -1);
  if( rc==TH_OK ){
    return (char*)Th_GetResult(g.interp, pSize);
  }else{
    return 0;
  }
}

/*
** Return true if the string begins with the TH1 begin-script
** tag:  <th1>.
*/
static int isBeginScriptTag(const char *z){
  return z[0]=='<'
      && (z[1]=='t' || z[1]=='T')
      && (z[2]=='h' || z[2]=='H')
      && z[3]=='1'
      && z[4]=='>';
}

/*
** Return true if the string begins with the TH1 end-script
** tag:  </th1>.
*/
static int isEndScriptTag(const char *z){
  return z[0]=='<'
      && z[1]=='/'
      && (z[2]=='t' || z[2]=='T')
      && (z[3]=='h' || z[3]=='H')
      && z[4]=='1'
      && z[5]=='>';
}

/*
** If string z[0...] contains a valid variable name, return
** the number of characters in that name.  Otherwise, return 0.
*/
static int validVarName(const char *z){
  int i = 0;
  int inBracket = 0;
  if( z[0]=='<' ){
    inBracket = 1;
    z++;
  }
  if( z[0]==':' && z[1]==':' && fossil_isalpha(z[2]) ){
    z += 3;
    i += 3;
  }else if( fossil_isalpha(z[0]) ){
    z ++;
    i += 1;
  }else{
    return 0;
  }
  while( fossil_isalnum(z[0]) || z[0]=='_' ){
    z++;
    i++;
  }
  if( inBracket ){
    if( z[0]!='>' ) return 0;
    i += 2;
  }
  return i;
}

#ifdef FOSSIL_ENABLE_TH1_HOOKS
/*
** This function is called by Fossil just prior to dispatching a command.
** Returning a value other than TH_OK from this function (i.e. via an
** evaluated script raising an error or calling [break]/[continue]) will
** cause the actual command execution to be skipped.
*/
int Th_CommandHook(
  const char *zName,
  char cmdFlags
){
  int rc = TH_OK;
  Th_FossilInit(TH_INIT_HOOK);
  Th_Store("cmd_name", zName);
  Th_StoreInt("cmd_flags", cmdFlags);
  rc = Th_Eval(g.interp, 0, "command_hook", -1);
  if( rc==TH_ERROR ){
    int nResult = 0;
    char *zResult = (char*)Th_GetResult(g.interp, &nResult);
    /*
    ** Make sure that the TH1 script error was not caused by a "missing"
    ** command hook handler as that is not actually an error condition.
    */
    if( memcmp(zResult, NO_COMMAND_HOOK_ERROR, nResult)!=0 ){
      sendError(zResult, nResult, 0);
    }
  }
  /*
  ** If the script returned TH_ERROR (e.g. the "command_hook" TH1 command does
  ** not exist because commands are not being hooked), return TH_OK because we
  ** do not want to skip executing essential commands unless the called command
  ** (i.e. "command_hook") explicitly forbids this by successfully returning
  ** TH_BREAK or TH_CONTINUE.
  */
  if( g.thTrace ){
    Th_Trace("[command_hook {%h}] => %h<br />\n", zName,
             Th_ReturnCodeName(rc, 0));
  }
  return (rc != TH_ERROR) ? rc : TH_OK;
}

/*
** This function is called by Fossil just after dispatching a command.
** Returning a value other than TH_OK from this function (i.e. via an
** evaluated script raising an error or calling [break]/[continue]) may
** cause an error message to be displayed to the local interactive user.
** Currently, TH1 error messages generated by this function are ignored.
*/
int Th_CommandNotify(
  const char *zName,
  char cmdFlags
){
  int rc;
  Th_FossilInit(TH_INIT_HOOK);
  Th_Store("cmd_name", zName);
  Th_StoreInt("cmd_flags", cmdFlags);
  rc = Th_Eval(g.interp, 0, "command_notify", -1);
  if( g.thTrace ){
    Th_Trace("[command_notify {%h}] => %h<br />\n", zName,
             Th_ReturnCodeName(rc, 0));
  }
  return rc;
}

/*
** This function is called by Fossil just prior to processing a web page.
** Returning a value other than TH_OK from this function (i.e. via an
** evaluated script raising an error or calling [break]/[continue]) will
** cause the actual web page processing to be skipped.
*/
int Th_WebpageHook(
  const char *zName,
  char cmdFlags
){
  int rc = TH_OK;
  Th_FossilInit(TH_INIT_HOOK);
  Th_Store("web_name", zName);
  Th_StoreInt("web_flags", cmdFlags);
  rc = Th_Eval(g.interp, 0, "webpage_hook", -1);
  if( rc==TH_ERROR ){
    int nResult = 0;
    char *zResult = (char*)Th_GetResult(g.interp, &nResult);
    /*
    ** Make sure that the TH1 script error was not caused by a "missing"
    ** webpage hook handler as that is not actually an error condition.
    */
    if( memcmp(zResult, NO_WEBPAGE_HOOK_ERROR, nResult)!=0 ){
      sendError(zResult, nResult, 1);
    }
  }
  /*
  ** If the script returned TH_ERROR (e.g. the "webpage_hook" TH1 command does
  ** not exist because commands are not being hooked), return TH_OK because we
  ** do not want to skip processing essential web pages unless the called
  ** command (i.e. "webpage_hook") explicitly forbids this by successfully
  ** returning TH_BREAK or TH_CONTINUE.
  */
  if( g.thTrace ){
    Th_Trace("[webpage_hook {%h}] => %h<br />\n", zName,
             Th_ReturnCodeName(rc, 0));
  }
  return (rc != TH_ERROR) ? rc : TH_OK;
}

/*
** This function is called by Fossil just after processing a web page.
** Returning a value other than TH_OK from this function (i.e. via an
** evaluated script raising an error or calling [break]/[continue]) may
** cause an error message to be displayed to the remote user.
** Currently, TH1 error messages generated by this function are ignored.
*/
int Th_WebpageNotify(
  const char *zName,
  char cmdFlags
){
  int rc;
  Th_FossilInit(TH_INIT_HOOK);
  Th_Store("web_name", zName);
  Th_StoreInt("web_flags", cmdFlags);
  rc = Th_Eval(g.interp, 0, "webpage_notify", -1);
  if( g.thTrace ){
    Th_Trace("[webpage_notify {%h}] => %h<br />\n", zName,
             Th_ReturnCodeName(rc, 0));
  }
  return rc;
}
#endif

/*
** The z[] input contains text mixed with TH1 scripts.
** The TH1 scripts are contained within <th1>...</th1>. 
** TH1 variables are $aaa or $<aaa>.  The first form of
** variable is literal.  The second is run through htmlize
** before being inserted.
**
** This routine processes the template and writes the results
** on either stdout or into CGI.
*/
int Th_Render(const char *z){
  int i = 0;
  int n;
  int rc = TH_OK;
  char *zResult;
  Th_FossilInit(TH_INIT_DEFAULT);
  while( z[i] ){
    if( z[i]=='$' && (n = validVarName(&z[i+1]))>0 ){
      const char *zVar;
      int nVar;
      int encode = 1;
      sendText(z, i, 0);
      if( z[i+1]=='<' ){
        /* Variables of the form $<aaa> are html escaped */
        zVar = &z[i+2];
        nVar = n-2;
      }else{
        /* Variables of the form $aaa are output raw */
        zVar = &z[i+1];
        nVar = n;
        encode = 0;
      }
      rc = Th_GetVar(g.interp, (char*)zVar, nVar);
      z += i+1+n;
      i = 0;
      zResult = (char*)Th_GetResult(g.interp, &n);
      sendText((char*)zResult, n, encode);
    }else if( z[i]=='<' && isBeginScriptTag(&z[i]) ){
      sendText(z, i, 0);
      z += i+5;
      for(i=0; z[i] && (z[i]!='<' || !isEndScriptTag(&z[i])); i++){}
      if( g.thTrace ){
        Th_Trace("eval {<pre>%#h</pre>}<br>", i, z);
      }
      rc = Th_Eval(g.interp, 0, (const char*)z, i);
      if( rc!=TH_OK ) break;
      z += i;
      if( z[0] ){ z += 6; }
      i = 0;
    }else{
      i++;
    }
  }
  if( rc==TH_ERROR ){
    zResult = (char*)Th_GetResult(g.interp, &n);
    sendError(zResult, n, 1);
  }else{
    sendText(z, i, 0);
  }
  return rc;
}

/*
** COMMAND: test-th-render
*/
void test_th_render(void){
  Blob in;
  Th_InitTraceLog();
  if( find_option("th-open-config", 0, 0)!=0 ){
    db_find_and_open_repository(OPEN_ANY_SCHEMA | OPEN_OK_NOT_FOUND, 0);
    db_open_config(0);
  }
  if( g.argc<3 ){
    usage("FILE");
  }
  blob_zero(&in);
  blob_read_from_file(&in, g.argv[2]);
  Th_Render(blob_str(&in));
  Th_PrintTraceLog();
}

/*
** COMMAND: test-th-eval
*/
void test_th_eval(void){
  int rc;
  const char *zRc;
  Th_InitTraceLog();
  if( find_option("th-open-config", 0, 0)!=0 ){
    db_find_and_open_repository(OPEN_ANY_SCHEMA | OPEN_OK_NOT_FOUND, 0);
    db_open_config(0);
  }
  if( g.argc!=3 ){
    usage("script");
  }
  Th_FossilInit(TH_INIT_DEFAULT);
  rc = Th_Eval(g.interp, 0, g.argv[2], -1);
  zRc = Th_ReturnCodeName(rc, 1);
  fossil_print("%s%s%s\n", zRc, zRc ? ": " : "", Th_GetResult(g.interp, 0));
  Th_PrintTraceLog();
}

#ifdef FOSSIL_ENABLE_TH1_HOOKS
/*
** COMMAND: test-th-hook
*/
void test_th_hook(void){
  int rc = TH_OK;
  int nResult = 0;
  char *zResult;
  if( g.argc<5 ){
    usage("TYPE NAME FLAGS");
  }
  if( fossil_stricmp(g.argv[2], "cmdhook")==0 ){
    rc = Th_CommandHook(g.argv[3], (char)atoi(g.argv[4]));
  }else if( fossil_stricmp(g.argv[2], "cmdnotify")==0 ){
    rc = Th_CommandNotify(g.argv[3], (char)atoi(g.argv[4]));
  }else if( fossil_stricmp(g.argv[2], "webhook")==0 ){
    rc = Th_WebpageHook(g.argv[3], (char)atoi(g.argv[4]));
  }else if( fossil_stricmp(g.argv[2], "webnotify")==0 ){
    rc = Th_WebpageNotify(g.argv[3], (char)atoi(g.argv[4]));
  }else{
    fossil_fatal("Unknown TH1 hook %s\n", g.argv[2]);
  }
  zResult = (char*)Th_GetResult(g.interp, &nResult);
  sendText("RESULT (", -1, 0);
  sendText(Th_ReturnCodeName(rc, 0), -1, 0);
  sendText("): ", -1, 0);
  sendText(zResult, nResult, 0);
  sendText("\n", -1, 0);
}
#endif
