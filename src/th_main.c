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
** Generate a TH1 trace message if debugging is enabled.
*/
void Th_Trace(const char *zFormat, ...){
  va_list ap;
  va_start(ap, zFormat);
  blob_vappendf(&g.thLog, zFormat, ap);
  va_end(ap);
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
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "enable_output BOOLEAN");
  }
  return Th_ToInt(interp, argv[1], argl[1], &enableOutput);
}

/*
** Send text to the appropriate output:  Either to the console
** or to the CGI reply buffer.
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
    if( encode ) fossil_free((char*)z);
  }
}

struct PutsCmdData {
  char escapeHtml;
  char const * sep;
  char const * eol;
};
typedef struct PutsCmdData PutsCmdData;

/*
** TH command:     puts STRING
** TH command:     html STRING
**
** Output STRING as HTML (html) or unchanged (puts).  
*/
static int putsCmd(
  Th_Interp *interp, 
  void *pConvert, 
  int argc, 
  const char **argv, 
  int *argl
){
  PutsCmdData const * fmt = (PutsCmdData const *)pConvert;
  const int sepLen = fmt->sep ? strlen(fmt->sep) : 0;
  int i;
  if( argc<2 ){
    return Th_WrongNumArgs(interp, "puts STRING ...STRING_N");
  }
  for( i = 1; i < argc; ++i ){
    if(sepLen && (i>1)){
      sendText(fmt->sep, sepLen, 0);
    }
    sendText((char const*)argv[i], argl[i], fmt->escapeHtml);
  }
  if(fmt->eol){
    sendText(fmt->eol, strlen(fmt->eol), 0);
  }
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
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "wiki STRING");
  }
  if( enableOutput ){
    Blob src;
    blob_init(&src, (char*)argv[1], argl[1]);
    wiki_convert(&src, 0, WIKI_INLINE);
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
    zOut = db_text("??", "SELECT datetime('now','localtime')");
  }else{
    zOut = db_text("??", "SELECT datetime('now')");
  }
  Th_SetResult(interp, zOut, -1);
  free(zOut);
  return TH_OK;
}

/*
** TH command:     hascap STRING
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
  int rc;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "hascap STRING");
  }
  rc = login_has_capability((char*)argv[1],argl[1]);
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
** "json" = FOSSIL_ENABLE_JSON
** "tcl" = FOSSIL_ENABLE_TCL
** "ssl" = FOSSIL_ENABLE_SSL
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
#if defined(FOSSIL_ENABLE_JSON)
  else if( 0 == fossil_strnicmp( zArg, "json", 4 ) ){
    rc = 1;
  }
#endif
#if defined(FOSSIL_ENABLE_SSL)
  else if( 0 == fossil_strnicmp( zArg, "ssl", 3 ) ){
    rc = 1;
  }
#endif
#if defined(FOSSIL_ENABLE_TCL)
  else if( 0 == fossil_strnicmp( zArg, "tcl", 3 ) ){
    rc = 1;
  }
#endif
  if( g.thTrace ){
    Th_Trace("[hasfeature %#h] => %d<br />\n", argl[1], zArg, rc);
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
    z = mprintf("<select name=\"%z\" size=\"%d\">", 
                 htmlize(blob_buffer(&name), blob_size(&name)), height);
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
  int openRepository;

  if( argc!=1 && argc!=2 ){
    return Th_WrongNumArgs(interp, "repository ?BOOLEAN?");
  }
  if( argc==2 ){
    if( Th_ToInt(interp, argv[1], argl[1], &openRepository) ){
      return TH_ERROR;
    }
    if( openRepository ) db_find_and_open_repository(OPEN_OK_NOT_FOUND, 0);
  }
  Th_SetResult(interp, g.zRepositoryName, -1);
  return TH_OK;
}

#ifdef TH_USE_SQLITE
static int queryPrepareCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  char const * zSql;
  sqlite3_stmt * pStmt = NULL;
  int rc;
  char const * errMsg = NULL;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "query_prepare STRING");
  }
  zSql = argv[1];
  rc = sqlite3_prepare( g.db, zSql, strlen(zSql), &pStmt, NULL );
  if(SQLITE_OK==rc){
    if(sqlite3_column_count( pStmt ) < 1){
      errMsg = "Only SELECT-like queries are supported.";
      rc = SQLITE_ERROR;
      sqlite3_finalize( pStmt );
      pStmt = NULL;
    }
  }else{
    errMsg = sqlite3_errmsg( g.db );
  }
  if(SQLITE_OK!=rc){
    assert(NULL != errMsg);
    assert(NULL == pStmt);
    Th_ErrorMessage(interp, "error preparing SQL:", errMsg, -1);
    return TH_ERROR;
  }
  rc = Th_AddStmt( interp, pStmt );
  assert( rc >= 0 && "AddStmt failed.");
  Th_SetResultInt( interp, rc );
  return TH_OK;
}

static sqlite3_stmt * queryStmtHandle(Th_Interp *interp, char const * arg, int argLen, int * stmtId ){
  int rc = 0;
  sqlite3_stmt * pStmt = NULL;
  if( 0 == Th_ToInt( interp, arg, argLen, &rc ) ){
    if(stmtId){
      *stmtId = rc;
    }
    pStmt = Th_GetStmt( interp, rc );
    if(NULL==pStmt){
      Th_ErrorMessage(interp, "no such statement handle:", arg, -1);
    }
  }
  return pStmt;

}

static int queryFinalizeCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  char * zSql;
  sqlite3_stmt * pStmt = NULL;
  int rc = 0;
  char const * arg;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "query_finalize StmtHandle");
  }
  arg = argv[1];
  pStmt = queryStmtHandle(interp, arg, argl[1], &rc);
  if( rc < 1 ){
    return TH_ERROR;
  }
  assert( NULL != pStmt );
  rc = Th_FinalizeStmt( interp, rc );
  Th_SetResultInt( interp, rc );
  return TH_OK;
}

static void queryReportDbErr( Th_Interp * interp, int rc ){
  char const * msg = sqlite3_errmsg( g.db );
  Th_ErrorMessage(interp, "db error:", msg, -1);
}

static int queryStmtIndexArgs(
  Th_Interp * interp,
  int argc,
  char const ** argv,
  int *argl,
  sqlite3_stmt ** pStmt,
  int * pIndex ){
  int index = 0;
  sqlite3_stmt * stmt;
  if( !pIndex ){
    if(argc<2){
      return Th_WrongNumArgs(interp, "StmtHandle");
    }
  }else{
    if( argc<3 ){
      return Th_WrongNumArgs(interp, "StmtHandle, Index");
    }
    if( 0 != Th_ToInt( interp, argv[2], argl[2], &index ) ){
      return TH_ERROR;
    }
  }
  stmt = queryStmtHandle(interp, argv[1], argl[1], NULL);
  if( NULL == stmt ){
    return TH_ERROR;
  }else{
    *pStmt = stmt;
    if( pIndex ){
      *pIndex = index;
    }
    return 0;
  }
}

static int queryStepCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  int rc = 0;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "query_step StmtHandle");
  }
  if(0 != queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, NULL)){
    return TH_ERROR;
  }
  rc = sqlite3_step( pStmt );
  switch(rc){
    case SQLITE_ROW:
      rc = 1;
      break;
    case SQLITE_DONE:
      rc = 0;
      break;
    default:
      queryReportDbErr( interp, rc );
      return TH_ERROR;
  }
  Th_SetResultInt( interp, rc );
  return TH_OK;
}

static int queryColStringCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  char const * val;
  int index;
  int valLen;
  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "query_col_string StmtHandle Index");
  }
  if(0 != queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index)){
    return TH_ERROR;
  }
  val = sqlite3_column_text( pStmt, index );
  valLen = val ? sqlite3_column_bytes( pStmt, index ) : 0;
  Th_SetResult( interp, val, valLen );
  return TH_OK;
}

static int queryColIntCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  int rc = 0;
  int index = 0;
  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "query_col_int StmtHandle Index");
  }
  if(0 != queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index)){
    return TH_ERROR;
  }
  Th_SetResultInt( interp, sqlite3_column_int( pStmt, index ) );
  return TH_OK;
}

static int queryColDoubleCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  double rc = 0;
  int index = -1;
  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "query_col_double StmtHandle Index");
  }
  if(0 != queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index)){
    return TH_ERROR;
  }
  Th_SetResultDouble( interp, sqlite3_column_double( pStmt, index ) );
  return TH_OK;
}


static int queryColCountCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  int rc;
  sqlite3_stmt * pStmt = NULL;
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "query_col_count StmtHandle");
  }
  pStmt = queryStmtHandle(interp, argv[1], argl[1], NULL);
  if( NULL == pStmt ){
    return TH_ERROR;
  }
  rc = sqlite3_column_count( pStmt );
  Th_SetResultInt( interp, rc );
  return TH_OK;
}

static int queryColNameCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  char const * val;
  int index;
  int rc = 0;
  int valLen;
  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "query_col_name StmtHandle Index");
  }
  pStmt = queryStmtHandle(interp, argv[1], argl[1], &rc);
  if( rc < 1 ){
    return TH_ERROR;
  }
  if( 0 != Th_ToInt( interp, argv[2], argl[2], &index ) ){
    return TH_ERROR;
  }
  val = sqlite3_column_name( pStmt, index );
  if(NULL==val){
    Th_ErrorMessage(interp, "Column index out of bounds(?):", argv[2], -1);
    return TH_ERROR;
  }else{
    Th_SetResult( interp, val, strlen( val ) );
    return TH_OK;
  }
}

#endif
/* end TH_USE_SQLITE */

/*
** Make sure the interpreter has been initialized.  Initialize it if
** it has not been already.
**
** The interpreter is stored in the g.interp global variable.
*/
void Th_FossilInit(void){
  static PutsCmdData puts_Html = {0, 0, 0};
  static PutsCmdData puts_Normal = {1, 0, 0};
  static PutsCmdData puts_Ext = {1, " ", "\n"};
  static struct _Command {
    const char *zName;
    Th_CommandProc xProc;
    void *pContext;
  } aCommand[] = {
    {"anycap",        anycapCmd,            0},
    {"combobox",      comboboxCmd,          0},
    {"enable_output", enableOutputCmd,      0},
    {"linecount",     linecntCmd,           0},
    {"hascap",        hascapCmd,            0},
    {"hasfeature",    hasfeatureCmd,        0},
    {"htmlize",       htmlizeCmd,           0},
    {"date",          dateCmd,              0},
    {"html",          putsCmd,     &puts_Html},
    {"puts",          putsCmd,   &puts_Normal},
    {"putsl",         putsCmd,      &puts_Ext},
#ifdef TH_USE_SQLITE
    {"query_col_count",   queryColCountCmd,  0},
    {"query_col_name",    queryColNameCmd,   0},
    {"query_col_string",  queryColStringCmd, 0},
    {"query_col_int",     queryColIntCmd,    0},
    {"query_col_double",  queryColDoubleCmd, 0},
    {"query_finalize",    queryFinalizeCmd,  0},
    {"query_prepare",     queryPrepareCmd,   0},
    {"query_step",        queryStepCmd,      0},
#endif
    {"wiki",          wikiCmd,              0},
    {"repository",    repositoryCmd,        0},
    {0, 0, 0}
  };
  if( g.interp==0 ){
    int i;
    g.interp = Th_CreateInterp(&vtab);
    th_register_language(g.interp);       /* Basic scripting commands. */
#ifdef FOSSIL_ENABLE_TCL
    if( getenv("TH1_ENABLE_TCL")!=0 || db_get_boolean("tcl", 0) ){
      th_register_tcl(g.interp, &g.tcl);  /* Tcl integration commands. */
    }
#endif
    for(i=0; i<sizeof(aCommand)/sizeof(aCommand[0]); i++){
      if ( !aCommand[i].zName || !aCommand[i].xProc ) continue;
      Th_CreateCommand(g.interp, aCommand[i].zName, aCommand[i].xProc,
                       aCommand[i].pContext, 0);
    }
  }
}

/*
** Store a string value in a variable in the interpreter.
*/
void Th_Store(const char *zName, const char *zValue){
  Th_FossilInit();
  if( zValue ){
    if( g.thTrace ){
      Th_Trace("set %h {%h}<br />\n", zName, zValue);
    }
    Th_SetVar(g.interp, zName, -1, zValue, strlen(zValue));
  }
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
  Th_FossilInit();
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
  Th_FossilInit();
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
    sendText("<hr><p class=\"thmainError\">ERROR: ", -1, 0);
    zResult = (char*)Th_GetResult(g.interp, &n);
    sendText((char*)zResult, n, 1);
    sendText("</p>", -1, 0);
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
  if( g.argc<3 ){
    usage("FILE");
  }
  db_open_config(0); /* Needed for global "tcl" setting. */
  blob_zero(&in);
  db_find_and_open_repository(OPEN_ANY_SCHEMA,0) /* for query_xxx tests. */;
  blob_read_from_file(&in, g.argv[2]);
  Th_Render(blob_str(&in));
}
