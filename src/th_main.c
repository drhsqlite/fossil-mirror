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
#ifndef INTERFACE
#include "blob.h"
#endif
#ifdef TH_ENABLE_SQLITE
#include "sqlite3.h"
#endif

/*#include "th_main.h"*/
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
  fossil_free(p);
}

static void *xRealloc(void * p, unsigned int n){
  if(0 == n){
    xFree(p);
    return NULL;
  }else if(NULL == p){
    return xMalloc(n);
  }else{
    return fossil_realloc(p, n)
      /* FIXME: try to find some reasonable nOutstandingMalloc
         heuristics, e.g. if !p then ++, if !n then --, etc.
      */;
  }
}

static Th_Vtab vtab = { xRealloc, {
    NULL,
    NULL,
    1
  }
};

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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                           "BOOLEAN");
  }else{
    int rc = Th_ToInt(interp, argv[1], argl[1], &enableOutput);
    vtab.out.enabled = enableOutput;
    return rc;
  }
}

int Th_output_f_cgi_content( char const * zData, int nData, void * pState ){
  cgi_append_content(zData, nData);
  return nData;
}


/*
** Send text to the appropriate output:  Either to the console
** or to the CGI reply buffer.
*/
static void sendText(Th_Interp *pInterp, const char *z, int n, int encode){
  if(NULL == pInterp){
    pInterp = g.interp;
  }
  assert( NULL != pInterp );
  if( enableOutput && n ){
    if( n<0 ) n = strlen(z);
    if( encode ){
      z = htmlize(z, n);
      n = strlen(z);
    }
    Th_output( pInterp, z, n );
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "STRING ...STRING_N");
  }
  for( i = 1; i < argc; ++i ){
    if(sepLen && (i>1)){
      sendText(interp, fmt->sep, sepLen, 0);
    }
    sendText(interp, (char const*)argv[i], argl[i], fmt->escapeHtml);
  }
  if(fmt->eol){
    sendText(interp, fmt->eol, strlen(fmt->eol), 0);
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "STRING");
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "STRING");
  }
  zOut = htmlize((char*)argv[1], argl[1]);
  Th_SetResult(interp, zOut, -1);
  free(zOut);
  return TH_OK;
}

#if 0
/* i'm not sure we need this */
/*
** TH command:      render STRING
**
** Render the input string as TH1.
*/
static int renderCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  if( argc<2 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "STRING ?STRING...?");
  }else{
    Th_Ob_Man * man = Th_ob_manager(interp);
    Blob * b = NULL;
    Blob buf = empty_blob;
    int rc, i;
    /*FIXME: assert(NULL != man && man->interp==interp);*/
    man->interp = interp;
    /* Combine all inputs into one buffer so that we can use that to
       embed TH1 tags across argument boundaries.

       FIX:E optimize away buf for the 1-arg case.
     */
    for( i = 1; TH_OK==rc && i < argc; ++i ){
      char const * str = argv[i];
      blob_append( &buf, str, argl[i] );
      /*rc = Th_Render( str, Th_Render_Flags_NO_DOLLAR_DEREF );*/
    }
    rc = Th_ob_push( man, &b );
    if(rc){
      blob_reset( &buf );
      return rc;
    }
    rc = Th_Render( buf.aData, Th_Render_Flags_DEFAULT );
    blob_reset(&buf);
    b = Th_ob_pop( man );
    if(TH_OK==rc){
      Th_SetResult( interp, b->aData, b->nUsed );
    }
    blob_reset( b );
    Th_Free( interp, b );
    return rc;
  }
}/* renderCmd() */
#endif

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
    return Th_WrongNumArgs2(interp,
                           argv[0], argl[0],
                           "STRING");
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "STRING");
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "STRING");
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "NAME TEXT-LIST NUMLINES");
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
    sendText(interp, z, -1, 0);
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
      sendText(interp, z, -1, 0);
      free(z);
    }
    sendText(interp, "</select>", -1, 0);
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "STRING MAX MIN");
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "?BOOLEAN?");
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


#ifdef TH_ENABLE_ARGV
extern const char *find_option(const char *zLong,
                               const char *zShort,
                               int hasArg) /* from main.c */;
/*
** TH Syntax:
**
** argv len
**
** Returns the number of command-line arguments.
*/
static int argvArgcCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  Th_SetResultInt( interp, g.argc );
  return TH_OK;
}



/*
** TH Syntax:
**
** argv_getat Index
**
** Returns the raw argument at the given index, throwing if
** out of bounds.
*/
static int argvGetAtCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  char const * zVal;
  int pos = 0;
  if( argc != 2 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "Index");
  }
  if( TH_OK != Th_ToInt(interp, argv[1], argl[1], &pos) ){
    return TH_ERROR;
  }
  if( pos < 0 || pos >= g.argc ){
    Th_ErrorMessage(interp, "Argument out of range:", argv[1], argl[1]);
    return TH_ERROR;
  }
  if( 0 == pos ){/*special case*/
    zVal = fossil_nameofexe();
  }else{
    zVal = (pos>0 && pos<g.argc) ? g.argv[pos] : 0;
  }
  Th_SetResult( interp, zVal, zVal ? strlen(zVal) : 0 );
  return TH_OK;  
}


/*
** TH Syntax:
**
** argv_getstr longName ??shortName? ?defaultValue??
**
** Functions more or less like Fossil's find_option().
** If the given argument is found then its value is returned,
** else defaultValue is returned. If that is not set
** and the option is not found, an error is thrown.
** If defaultValue is provided, shortName must also be provided
** but it may be empty. For example:
**
** set foo [argv_getstr foo "" "hi, world"]
**
** ACHTUNG: find_option() removes any entries it finds from
** g.argv, such that future calls to find_option() will not
** find the same option.
*/
static int argvFindOptionStringCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  enum { BufLen = 100 };
  char zLong[BufLen] = {0};
  char zShort[BufLen] = {0};
  char aBuf[BufLen] = {0};
  int hasArg;
  char const * zVal = NULL;
  char const * zDefault = NULL;
  int check;
  if( 1 < argc ){
    assert( argl[1] < BufLen );
    check = snprintf( zLong, BufLen, "%s", argv[1] );
    assert( check <= BufLen );
  }
  if( (2 < argc) && (0 < argl[2]) ){
    assert( argl[2] < BufLen );
    check = snprintf( zShort, BufLen, "%s", argv[2] );
    assert( check <= BufLen );
  }
  if( 3 < argc){
    zDefault = argv[3];
  }

  if(0 == zLong[0]){
    return Th_WrongNumArgs2(interp,
                           argv[0], argl[0],
                           "longName ?shortName? ?defaultVal?");
  }
  if(g.cgiOutput){
      zVal = cgi_parameter( zLong, NULL );
      if( !zVal && zShort[0] ){
          zVal = cgi_parameter( zShort, NULL );
      }
  }else{
      zVal = find_option( zLong, zShort[0] ? zShort : NULL, 1 );
  }
  if(!zVal){
    zVal = zDefault;
    if(!zVal){
      Th_ErrorMessage(interp, "Option not found and no default provided:", zLong, -1);
      return TH_ERROR;
    }
  }
  Th_SetResult( interp, zVal, zVal ? strlen(zVal) : 0 );
  return TH_OK;  
}

/*
** TH Syntax:
**
** argv_getbool longName ??shortName? ?defaultValue??
**
** Works just like argv_getstr but treats any empty value or one
** starting with the digit '0' as a boolean false.
**
** Returns the result as an integer 0 (false) or 1 (true).
*/
static int argvFindOptionBoolCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  /* FIXME: refactor to re-use the code from getstr */
  enum { BufLen = 100 };
  char zLong[BufLen] = {0};
  char zShort[BufLen] = {0};
  char aBuf[BufLen] = {0};
  int hasArg;
  char const * zVal = NULL;
  char const * zDefault = NULL;
  int val;
  int rc;
  int check;
  if( 1 < argc ){
    assert( argl[1] < BufLen );
    check = snprintf( zLong, BufLen, "%s", argv[1] );
    assert( check <= BufLen );
  }
  if( (2 < argc) && (0 < argl[2]) ){
    assert( argl[2] < BufLen );
    check = snprintf( zShort, BufLen, "%s", argv[2] );
    assert( check <= BufLen );
  }
  if( 3 < argc){
    zDefault = argv[3];
  }

  if(0 == zLong[0]){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                           "longName ?shortName? ?defaultVal?");
  }
  if(g.cgiOutput){
      zVal = cgi_parameter( zLong, NULL );
      if( !zVal && zShort[0] ){
          zVal = cgi_parameter( zShort, NULL );
      }
  }else{
      zVal = find_option( zLong, zShort[0] ? zShort : NULL, 0 );
  }
  if(zVal && !*zVal){
    zVal = "1";
  }
  if(!zVal){
    zVal = zDefault;
    if(!zVal){
      Th_ErrorMessage(interp, "Option not found and no default provided:", zLong, -1);
      return TH_ERROR;
    }
  }
  if( !*zVal ){
    zVal = "0";
  }
  zVal = (zVal && *zVal && (*zVal!='0')) ? zVal : 0;
  Th_SetResultInt( interp, zVal ? 1 : 0 );
  return TH_OK;
}

/*
** TH Syntax:
**
** argv_getint longName ?shortName? ?defaultValue?
*/
static int argvFindOptionIntCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  /* FIXME: refactor to re-use the code from getstr */
  enum { BufLen = 100 };
  char zLong[BufLen] = {0};
  char zShort[BufLen] = {0};
  char aBuf[BufLen] = {0};
  int hasArg;
  char const * zVal = NULL;
  char const * zDefault = NULL;
  int val = 0;
  int check;
  if( 1 < argc ){
    assert( argl[1] < BufLen );
    check = snprintf( zLong, BufLen, "%s", argv[1] );
    assert( check <= BufLen );
  }
  if( (2 < argc) && (0 < argl[2]) ){
    assert( argl[2] < BufLen );
    check = snprintf( zShort, BufLen, "%s", argv[2] );
    assert( check <= BufLen );
  }
  if( 3 < argc){
    zDefault = argv[3];
  }

  if(0 == zLong[0]){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                           "longName ?shortName? ?defaultVal?");
  }
  if(g.cgiOutput){
      zVal = cgi_parameter( zLong, NULL );
      if( !zVal && zShort[0] ){
          zVal = cgi_parameter( zShort, NULL );
      }
  }else{
      zVal = find_option( zLong, zShort[0] ? zShort : NULL, 1 );
  }
  if(!zVal){
    zVal = zDefault;
    if(!zVal){
      Th_ErrorMessage(interp, "Option not found and no default provided:", zLong, -1);
      return TH_ERROR;
    }
  }
  Th_ToInt(interp, zVal, strlen(zVal), &val);
  Th_SetResultInt( interp, val );
  return TH_OK;  
}

static int argvTopLevelCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  static Th_SubCommand aSub[] = {
    {"len",      argvArgcCmd},
    {"at",       argvGetAtCmd},
    {"getstr",   argvFindOptionStringCmd},
    {"getbool",  argvFindOptionBoolCmd},
    {"getint",   argvFindOptionIntCmd},
    {0, 0}
  };
  Th_CallSubCommand2( interp, ctx, argc, argv, argl, aSub );
}

int th_register_argv(Th_Interp *interp){
  static Th_Command_Reg aCommand[] = {
    {"argv",            argvTopLevelCmd, 0 },
    {0, 0, 0}
  };
  Th_register_commands( interp, aCommand );
}

#endif
/* end TH_ENABLE_ARGV */

#ifdef TH_ENABLE_SQLITE

/*
** Adds the given prepared statement to the interpreter. Returns the
** statements opaque identifier (a positive value). Ownerships of
** pStmt is transfered to interp and it must be cleaned up by the
** client by calling Th_FinalizeStmt(), passing it the value returned
** by this function.
**
** If interp is destroyed before all statements are finalized,
** it will finalize them but may emit a warning message.
*/
static int Th_AddStmt(Th_Interp *interp, sqlite3_stmt * pStmt);

/*
** Expects stmtId to be a statement identifier returned by
** Th_AddStmt(). On success, finalizes the statement and returns 0.
** On error (statement not found) non-0 is returned. After this
** call, some subsequent call to Th_AddStmt() may return the
** same statement ID.
*/
static int Th_FinalizeStmt(Th_Interp *interp, int stmtId);

/*
** Fetches the statement with the given ID, as returned by
** Th_AddStmt(). Returns NULL if stmtId does not refer (or no longer
** refers) to a statement added via Th_AddStmt().
*/
static sqlite3_stmt * Th_GetStmt(Th_Interp *interp, int stmtId);


struct Th_Sqlite {
  sqlite3_stmt ** aStmt;
  int nStmt;
};
#define Th_Sqlite_KEY "Th_Sqlite"
typedef struct Th_Sqlite Th_Sqlite;

static Th_Sqlite * Th_sqlite_manager( Th_Interp * interp ){
  void * p = Th_Data_Get( interp, Th_Sqlite_KEY );
  return p ? (Th_Sqlite*)p : NULL;
}

static int Th_AddStmt(Th_Interp *interp, sqlite3_stmt * pStmt){
  Th_Sqlite * sq = Th_sqlite_manager(interp);
  int i, x;
  sqlite3_stmt * s;
  sqlite3_stmt ** list = sq->aStmt;
  for( i = 0; i < sq->nStmt; ++i ){
    s = list[i];
    if(NULL==s){
      list[i] = pStmt;
      return i+1;
    }
  }
  x = (sq->nStmt + 1) * 2;
  list = (sqlite3_stmt**)fossil_realloc( list, sizeof(sqlite3_stmt*)*x );
  for( i = sq->nStmt; i < x; ++i ){
    list[i] = NULL;
  }
  list[sq->nStmt] = pStmt;
  x = sq->nStmt;
  sq->nStmt = i;
  sq->aStmt = list;
  return x + 1;
}


static int Th_FinalizeStmt(Th_Interp *interp, int stmtId){
  Th_Sqlite * sq = Th_sqlite_manager(interp);
  sqlite3_stmt * st;
  int rc = 0;
  assert( stmtId>0 && stmtId<=sq->nStmt );
  st = sq->aStmt[stmtId-1];
  if(NULL != st){
    sq->aStmt[stmtId-1] = NULL;
    sqlite3_finalize(st);
    return 0;
  }else{
    return 1;
  }
}

static sqlite3_stmt * Th_GetStmt(Th_Interp *interp, int stmtId){
  Th_Sqlite * sq = Th_sqlite_manager(interp);
  return ((stmtId<1) || (stmtId > sq->nStmt))
    ? NULL
    : sq->aStmt[stmtId-1];
}


static void finalizerSqlite( Th_Interp * interp, void * p ){
  Th_Sqlite * sq = (Th_Sqlite *)p;
  int i;
  sqlite3_stmt * st = NULL;
  if(!sq) {
    fossil_warning("Got a finalizer call for a NULL Th_Sqlite.");
    return;
  }
  for( i = 0; i < sq->nStmt; ++i ){
    st = sq->aStmt[i];
    if(NULL != st){
      fossil_warning("Auto-finalizing unfinalized "
                     "statement id #%d: %s",
                     i+1, sqlite3_sql(st));
      Th_FinalizeStmt( interp, i+1 );
    }
  }
  Th_Free(interp, sq->aStmt);
  Th_Free(interp, sq);
}


/*
** TH Syntax:
**
** query_prepare SQL
**
** Returns an opaque statement identifier.
*/
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "STRING");
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

/*
** Tries to convert arg, which must be argLen bytes long, to a
** statement handle id and, in turn, to a sqlite3_stmt. On success
** (the argument references a prepared statement) it returns the
** handle and stmtId (if not NULL) is assigned to the integer value of
** arg. On error NULL is returned and stmtId might be modified (if not
** NULL). If stmtId is unmodified after an error then it is not a
** number, else it is a number but does not reference an opened
** statement.
*/
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

/*
** TH Syntax:
**
** query_finalize stmtId
**
** sqlite3_finalize()s the given statement.
*/
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle");
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

/*
** Reports the current sqlite3_errmsg() via TH and returns TH_ERROR.
*/
static int queryReportDbErr( Th_Interp * interp ){
  char const * msg = sqlite3_errmsg( g.db );
  Th_ErrorMessage(interp, "db error:", msg, -1);
  return TH_ERROR;
}

/*
** Internal helper for fetching statement handle and index parameters.
** The first 4 args should be the args passed to the TH1 callback.
** pStmt must be a pointer to a NULL pointer. pIndex may be NULL or
** a pointer to store the statement index argument in. If pIndex is
** NULL then argc is asserted to be at least 2, else it must be at
** least 3.
**
** On success it returns 0, sets *pStmt to the referenced statement
** handle, and pIndex (if not NULL) to the integer value of argv[2]
** argument. On error it reports the error via TH, returns non-0, and
** modifies neither pStmt not pIndex.
*/
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
      return Th_WrongNumArgs2(interp,
                              argv[0], argl[0],
                              "StmtHandle");
    }
  }else{
    if( argc<3 ){
      return Th_WrongNumArgs2(interp,
                              argv[0], argl[0],
                              "StmtHandle Index");
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

/*
** TH Syntax:
**
** query_step stmtId
**
** Steps the given statement handle. Returns 0 at the end of the set,
** a positive value if it fetches a row, and throws on error.
*/
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle");
  }
  if(0 != queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, NULL)){
    return TH_ERROR;
  }
  assert(NULL != pStmt);
  rc = sqlite3_step( pStmt );
  switch(rc){
    case SQLITE_ROW:
      rc = 1;
      break;
    case SQLITE_DONE:
      rc = 0;
      break;
    default:
      return queryReportDbErr( interp );
  }
  Th_SetResultInt( interp, rc );
  return TH_OK;
}

/*
** TH Syntax:
**
** query_col_string stmtId Index
**
** Returns the result column value at the given 0-based index.
*/
static int queryColStringCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  char const * val;
  int index = -1;
  int valLen;
  if( argc!=3 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 0){
    return TH_ERROR;
  }
  val = sqlite3_column_text( pStmt, index );
  valLen = val ? sqlite3_column_bytes( pStmt, index ) : 0;
  Th_SetResult( interp, val, valLen );
  return TH_OK;
}

/*
** TH Syntax:
**
** query_col_int stmtId Index
**
** Returns the result column value at the given 0-based index.
*/
static int queryColIntCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  int rc = 0;
  int index = -1;
  if( argc!=3 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 0){
    return TH_ERROR;
  }
  Th_SetResultInt( interp, sqlite3_column_int( pStmt, index ) );
  return TH_OK;
}

/*
** TH Syntax:
**
** query_col_double stmtId Index
**
** Returns the result column value at the given 0-based index.
*/
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 0){
    return TH_ERROR;
  }
  Th_SetResultDouble( interp, sqlite3_column_double( pStmt, index ) );
  return TH_OK;
}

/*
** TH Syntax:
**
** query_col_is_null stmtId Index
**
** Returns non-0 if the given 0-based result column index contains
** an SQL NULL value, else returns 0.
*/
static int queryColIsNullCmd(
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 0){
    return TH_ERROR;
  }
  Th_SetResultInt( interp,
                   SQLITE_NULL==sqlite3_column_type( pStmt, index )
                   ? 1 : 0);
  return TH_OK;
}

/*
** TH Syntax:
**
** query_col_type stmtId Index
**
** Returns the sqlite type identifier for the given 0-based result
** column index. The values are available in TH as $SQLITE_NULL,
** $SQLITE_INTEGER, etc.
*/
static int queryColTypeCmd(
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
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 0){
    return TH_ERROR;
  }
  Th_SetResultInt( interp, sqlite3_column_type( pStmt, index ) );
  return TH_OK;
}

/*
** TH Syntax:
**
** query_col_count stmtId
**
** Returns the number of result columns in the query.
*/
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
    return Th_WrongNumArgs2(interp,
                           argv[0], argl[0],
                           "StmtHandle");
  }
  pStmt = queryStmtHandle(interp, argv[1], argl[1], NULL);
  if( NULL == pStmt ){
    return TH_ERROR;
  }
  rc = sqlite3_column_count( pStmt );
  Th_SetResultInt( interp, rc );
  return TH_OK;
}

/*
** TH Syntax:
**
** query_col_count stmtId Index
**
** Returns the result column name at the given 0-based index.
*/
static int queryColNameCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  char const * val;
  int rc = 0;
  int index = -1;
  if( argc!=3 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 0){
    assert(NULL==pStmt);
    return TH_ERROR;
  }
  assert(NULL!=pStmt);
  val = sqlite3_column_name( pStmt, index );
  if(NULL==val){
    Th_ErrorMessage(interp, "Column index out of bounds(?):", argv[2], -1);
    return TH_ERROR;
  }else{
    Th_SetResult( interp, val, strlen( val ) );
    return TH_OK;
  }
}

static int queryColTimeCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  char const * val;
  char * fval;
  int i, rc = 0;
  int index = -1;
  char const * fmt;
  Blob sql = empty_blob;
  if( argc<4 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index Format");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 0){
    assert(NULL==pStmt);
    return TH_ERROR;
  }
  val = sqlite3_column_text( pStmt, index );
  fmt = argv[3];
  assert(NULL!=pStmt);
  blob_appendf(&sql,"SELECT strftime(%Q,%Q",
               fmt, val);
  if(argc>4){
    for(i = 4; i < argc; ++i ){
      blob_appendf(&sql, ",%Q", argv[i]);
    }
  }
  blob_append(&sql, ")", 1);
  fval = db_text(NULL,"%s", sql.aData);
  
  blob_reset(&sql);
  Th_SetResult( interp, fval, fval ? strlen(fval) : 0 );
  fossil_free(fval);
  return 0;
}

static int queryStrftimeCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  char const * val;
  char * fval;
  int i, rc = 0;
  int index = -1;
  char const * fmt;
  Blob sql = empty_blob;
  if( argc<3 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "Format Value ?Modifiers...?");
  }
  fmt = argv[1];
  val = argv[2];
  blob_appendf(&sql,"SELECT strftime(%Q,%Q",
               fmt, val);
  if(argc>3){
    for(i = 3; i < argc; ++i ){
      blob_appendf(&sql, ",%Q", argv[i]);
    }
  }
  blob_append(&sql, ")", 1);
  fval = db_text(NULL,"%s", sql.aData);
  blob_reset(&sql);
  Th_SetResult( interp, fval, fval ? strlen(fval) : 0 );
  fossil_free(fval);
  return 0;
}


/*
** TH Syntax:
**
** query_bind_null stmtId Index
**
** Binds a value to the given 1-based parameter index.
*/
static int queryBindNullCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  int rc;
  int index = 0;
  if( argc!=3 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 1){
    return TH_ERROR;
  }
  rc = sqlite3_bind_null( pStmt, index );
  if(rc){
    return queryReportDbErr( interp );
  }
  Th_SetResultInt( interp, 0 );
  return TH_OK;
}


/*
** TH Syntax:
**
** query_bind_string stmtId Index Value
**
** Binds a value to the given 1-based parameter index.
*/
static int queryBindStringCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  int rc;
  int index = 0;
  if( argc!=4 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index Value");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 1){
    return TH_ERROR;
  }
  rc = sqlite3_bind_text( pStmt, index, argv[3], argl[3], SQLITE_TRANSIENT );
  if(rc){
    return queryReportDbErr( interp );
  }
  Th_SetResultInt( interp, 0 );
  return TH_OK;
}

/*
** TH Syntax:
**
** query_bind_int stmtId Index Value
**
** Binds a value to the given 1-based parameter index.
*/
static int queryBindIntCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  int rc;
  int val;
  int index = 0;
  if( argc!=4 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index Value");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 1){
    return TH_ERROR;
  }
  if( 0 != Th_ToInt( interp, argv[3], argl[3], &val ) ){
    return TH_ERROR;
  }

  rc = sqlite3_bind_int( pStmt, index, val );
  if(rc){
    return queryReportDbErr( interp );
  }
  Th_SetResultInt( interp, 0 );
  return TH_OK;
}

/*
** TH Syntax:
**
** query_bind_double stmtId Index Value
**
** Binds a value to the given 1-based parameter index.
*/
static int queryBindDoubleCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = NULL;
  int rc;
  double val;
  int index = 0;
  if( argc!=4 ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index Value");
  }
  queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  if(index < 1){
    return TH_ERROR;
  }
  if( 0 != Th_ToDouble( interp, argv[3], argl[3], &val ) ){
    return TH_ERROR;
  }

  rc = sqlite3_bind_double( pStmt, index, val );
  if(rc){
    return queryReportDbErr( interp );
  }
  Th_SetResultInt( interp, 0 );
  return TH_OK;
}

static int queryBindTopLevelCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  static Th_SubCommand aSub[] = {
    {"int",    queryBindIntCmd},
    {"double", queryBindDoubleCmd},
    {"null",   queryBindNullCmd},
    {"string", queryBindStringCmd},
    {0, 0}
  };
  Th_CallSubCommand2( interp, ctx, argc, argv, argl, aSub );
}

static int queryColTopLevelCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  static Th_SubCommand aSub[] = {
    {"count",   queryColCountCmd},
    {"is_null", queryColIsNullCmd},
    {"name",    queryColNameCmd},
    {"double",  queryColDoubleCmd},
    {"int",     queryColIntCmd},
    {"string",  queryColStringCmd},
    {"time",    queryColTimeCmd},
    {"type",    queryColTypeCmd},
    {0, 0}
  };
  Th_CallSubCommand2( interp, ctx, argc, argv, argl, aSub );
}


static int queryTopLevelCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  Th_Sqlite * sq = Th_sqlite_manager(interp);
  static Th_SubCommand aSub[] = {
    {"bind",        queryBindTopLevelCmd},
    {"col",         queryColTopLevelCmd},
    {"step",        queryStepCmd},
    {"finalize",    queryFinalizeCmd},
    {"prepare",     queryPrepareCmd},
    {"strftime",    queryStrftimeCmd},
    {0, 0}
  };
  assert( NULL != sq );
  Th_CallSubCommand2( interp, sq, argc, argv, argl, aSub );
}


int th_register_query(Th_Interp *interp){
  enum { BufLen = 100 };
  char buf[BufLen];
  int i, l;
#define SET(K) l = snprintf(buf, BufLen, "%d", K);      \
  Th_SetVar( interp, #K, strlen(#K), buf, l );
  SET(SQLITE_BLOB);
  SET(SQLITE_DONE);
  SET(SQLITE_ERROR);
  SET(SQLITE_FLOAT);
  SET(SQLITE_INTEGER);
  SET(SQLITE_NULL);
  SET(SQLITE_OK);
  SET(SQLITE_ROW);
  SET(SQLITE_TEXT);
#undef SET
  int rc = TH_OK;
  static Th_Command_Reg aCommand[] = {
    {"query",             queryTopLevelCmd,  0},
    {0, 0, 0}
  };
  rc = Th_register_commands( interp, aCommand );
  if(TH_OK==rc){
    Th_Sqlite * sq = Th_Malloc(interp, sizeof(Th_Sqlite));
    if(!sq){
      rc = TH_ERROR;
    }else{
      assert( NULL == sq->aStmt );
      assert( 0 == sq->nStmt );
      Th_Data_Set( interp, Th_Sqlite_KEY, sq, finalizerSqlite );
      assert( sq == Th_sqlite_manager(interp) );
    }
  }
  return rc;
}

#endif
/* end TH_ENABLE_SQLITE */

int Th_register_commands( Th_Interp * interp,
                           Th_Command_Reg const * aCommand ){
  int i;
  int rc = TH_OK;
  for(i=0; (TH_OK==rc) && aCommand[i].zName; ++i){
    if ( !aCommand[i].zName ) break;
    else if( !aCommand[i].xProc ) continue;
    else{
      rc = Th_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc,
                            aCommand[i].pContext, 0);
    }
  }
  return rc;
}

/*
** Make sure the interpreter has been initialized.  Initialize it if
** it has not been already.
**
** The interpreter is stored in the g.interp global variable.
*/
void Th_FossilInit(void){
  static PutsCmdData puts_Html = {0, 0, 0};
  static PutsCmdData puts_Normal = {1, 0, 0};
  static Th_Command_Reg aCommand[] = {
    {"anycap",        anycapCmd,            0},
    {"combobox",      comboboxCmd,          0},
    {"date",          dateCmd,              0},
    {"enable_output", enableOutputCmd,      0},
    {"hascap",        hascapCmd,            0},
    {"hasfeature",    hasfeatureCmd,        0},
    {"html",          putsCmd,     &puts_Html},
    {"htmlize",       htmlizeCmd,           0},
    {"linecount",     linecntCmd,           0},
    {"puts",          putsCmd,   &puts_Normal},
#if 0
    {"render",        renderCmd,            0},
#endif
    {"repository",    repositoryCmd,        0},
    {"wiki",          wikiCmd,              0},

    {0, 0, 0}
  };
  if( g.interp==0 ){
    int i;
    if(g.cgiOutput){
      vtab.out.f = Th_output_f_cgi_content;
    }else{
      vtab.out.f = Th_output_f_FILE;
      vtab.out.pState = stdout;
    }
    vtab.out.enabled = enableOutput;
    g.interp = Th_CreateInterp(&vtab);
    th_register_language(g.interp);       /* Basic scripting commands. */
#ifdef FOSSIL_ENABLE_TCL
    if( getenv("TH1_ENABLE_TCL")!=0 || db_get_boolean("tcl", 0) ){
      th_register_tcl(g.interp, &g.tcl);  /* Tcl integration commands. */
    }
#endif
#ifdef TH_ENABLE_OUTBUF
    th_register_ob(g.interp);
#endif
#ifdef TH_ENABLE_SQLITE
    th_register_query(g.interp);
#endif
#ifdef TH_ENABLE_ARGV
    th_register_argv(g.interp);
#endif
    Th_register_commands( g.interp, aCommand );
    Th_Eval( g.interp, 0, "proc incr {name {step 1}} {\n"
             "upvar $name x\n"
             "set x [expr $x+$step]\n"
             "}", -1 );
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
**
** If flags does NOT contain the Th_Render_Flags_NO_DOLLAR_DEREF bit
** then TH1 variables are $aaa or $<aaa>.  The first form of variable
** is literal.  The second is run through htmlize before being
** inserted.
**
** This routine processes the template and writes the results
** via Th_output().
*/
int Th_Render(const char *z, int flags){
  int i = 0;
  int n;
  int rc = TH_OK;
  char const *zResult;
  char doDollar = !(flags & Th_Render_Flags_NO_DOLLAR_DEREF);
  Th_FossilInit();
  while( z[i] ){
    if( doDollar && z[i]=='$' && (n = validVarName(&z[i+1]))>0 ){
      const char *zVar;
      int nVar;
      int encode = 1;
      sendText(g.interp, z, i, 0);
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
      rc = Th_GetVar(g.interp, zVar, nVar);
      z += i+1+n;
      i = 0;
      zResult = Th_GetResult(g.interp, &n);
      sendText(g.interp, zResult, n, encode);
    }else if( z[i]=='<' && isBeginScriptTag(&z[i]) ){
      sendText(g.interp, z, i, 0);
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
    sendText(g.interp, "<hr><p class=\"thmainError\">ERROR: ", -1, 0);
    zResult = Th_GetResult(g.interp, &n);
    sendText(g.interp, zResult, n, 1);
    sendText(g.interp, "</p>", -1, 0);
  }else{
    sendText(g.interp, z, i, 0);
  }
  return rc;
}

/*
** COMMAND: test-th-render
** COMMAND: th1
**
** Processes a file provided on the command line as a TH1-capable
** script/page. Output is sent to stdout or the CGI output buffer, as
** appropriate. The input file is assumed to be text/wiki/HTML content
** which may contain TH1 tag blocks. Each block is executed in the
** same TH1 interpreter instance.
**
*/
void test_th_render(void){
  Blob in;
  if( g.argc<3 ){
    usage("FILE");
    assert(0 && "usage() does not return");
  }
  blob_zero(&in);
  db_open_config(0); /* Needed for global "tcl" setting. */
#ifdef TH_ENABLE_SQLITE
  db_find_and_open_repository(OPEN_ANY_SCHEMA,0)
    /* required for th1 query API. */;
#endif
  blob_read_from_file(&in, g.argv[2]);
  Th_Render(blob_str(&in), Th_Render_Flags_DEFAULT);
}
