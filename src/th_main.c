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

#ifdef TH_ENABLE_QUERY
#ifndef INTERFACE
#include "sqlite3.h"
#endif
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
  fossil_free(p);
}

/*
** Default Th_Vtab::xRealloc() implementation.
*/
static void *xRealloc(void * p, unsigned int n){
  assert(n>=0 && "Invalid memory (re/de)allocation size.");
  if(0 == n){
    xFree(p);
    return NULL;
  }else if(NULL == p){
    return xMalloc(n);
  }else{
    return fossil_realloc(p, n)
      /* In theory nOutstandingMalloc doesn't need to be updated here
         unless xRealloc() is sorely misused.
      */;
  }
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
** True if output is enabled.  False if disabled.
**
** We "could" replace this with Th_OutputEnable() and friends, but
** there is a functional difference: this particular flag prohibits
** some extra escaping which would happen (but be discared, unused) if
** relied solely on that API. Also, because that API only works on the
** current Vtab_Output handler, relying soly on that handling would
** introduce incompatible behaviour with the historical enable_output
** command.
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
    return rc;
  }
}

/*
** Th_Output_f() impl which sends all output to cgi_append_content().
*/
static int Th_Output_f_cgi_content( char const * zData, int nData, void * pState ){
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
    Th_Output( pInterp, z, n );
    if( encode ) fossil_free((char*)z);
  }
}

/*
** Internal state for the putsCmd() function, allowing it to be used
** as the basis for multiple implementations with slightly different
** behaviours based on the context. An instance of this type must be
** set as the Context parameter for any putsCmd()-based script command
** binding.
*/
struct PutsCmdData {
  char escapeHtml;    /* If true, htmlize all output. */
  char const * sep;   /* Optional NUL-terminated separator to output
                         between arguments. May be NULL. */
  char const * eol;   /* Optional NUL-terminated end-of-line separator,
                         output after the final argument. May be NULL. */
};
typedef struct PutsCmdData PutsCmdData;

/*
** TH command:     puts STRING
** TH command:     html STRING
**
** Output STRING as HTML (html) or unchanged (puts).
**
** pConvert MUST be a (PutsCmdData [const]*). It is not modified by
** this function.
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
/* This is not yet needed, but something like it may become useful for
   custom page/command support, for rendering snippets/templates. */
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
    Th_Ob_Manager * man = Th_Ob_GetManager(interp);
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
    rc = Th_Ob_Push( man, &b );
    if(rc){
      blob_reset( &buf );
      return rc;
    }
    rc = Th_Render( buf.aData, Th_Render_Flags_DEFAULT );
    blob_reset(&buf);
    b = Th_Ob_Pop( man );
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
/*
** TH command:
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
** TH command:
**
** argv at Index
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
** TH command:
**
** argv getstr longName ??shortName? ?defaultValue??
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
** TH command:
**
** argv getbool longName ??shortName? ?defaultValue??
**
** Works just like argv getstr but treats any empty value or one
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
** TH command:
**
** argv getint longName ?shortName? ?defaultValue?
**
** Works like argv getstr but returns the value as an integer
** (throwing an error if the argument cannot be converted).
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

/*
** TH command:
**
** argv subcommand
**
** This is the top-level dispatching function.
*/
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
  Th_RegisterCommands( interp, aCommand );
}

#endif
/* end TH_ENABLE_ARGV */

#ifdef TH_ENABLE_QUERY

/*
** Adds the given prepared statement to the interpreter. Returns the
** statement's opaque identifier (a positive value). Ownerships of
** pStmt is transfered to interp and it must be cleaned up by the
** client by calling Th_query_FinalizeStmt(), passing it the value returned
** by this function.
**
** If interp is destroyed before all statements are finalized,
** it will finalize them but may emit a warning message.
*/
static int Th_query_AddStmt(Th_Interp *interp, sqlite3_stmt * pStmt);


/*
** Internal state for the "query" API.
*/
struct Th_Query {
  sqlite3_stmt ** aStmt; /* Array of statement handles. */
  int nStmt;             /* number of entries in aStmt. */
  int colCmdIndex;       /* column index argument. Set by some top-level dispatchers
                            for their subcommands.
                         */
};
/*
** Internal key for use with Th_Data_Add().
*/
#define Th_Query_KEY "Th_Query"
typedef struct Th_Query Th_Query;

/*
** Returns the Th_Query object associated with the given interpreter,
** or 0 if there is not one.
*/
static Th_Query * Th_query_manager( Th_Interp * interp ){
  void * p = Th_GetData( interp, Th_Query_KEY );
  return p ? (Th_Query*)p : NULL;
}

static int Th_query_AddStmt(Th_Interp *interp, sqlite3_stmt * pStmt){
  Th_Query * sq = Th_query_manager(interp);
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


/*
** Expects stmtId to be a statement identifier returned by
** Th_query_AddStmt(). On success, finalizes the statement and returns 0.
** On error (statement not found) non-0 is returned. After this
** call, some subsequent call to Th_query_AddStmt() may return the
** same statement ID.
*/
static int Th_query_FinalizeStmt(Th_Interp *interp, int stmtId){
  Th_Query * sq = Th_query_manager(interp);
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

/*
** Works like Th_query_FinalizeStmt() but takes a statement pointer, which
** must have been Th_query_AddStmt()'d to the given interpreter.
*/
static int Th_query_FinalizeStmt2(Th_Interp *interp, sqlite3_stmt * pSt){
  Th_Query * sq = Th_query_manager(interp);
  int i = 0;
  sqlite3_stmt * st = NULL;
  int rc = 0;
  for( ; i < sq->nStmt; ++i ){
    st = sq->aStmt[i];
    if(st == pSt) break;
  }
  if( st == pSt ){
    assert( i>=0 && i<sq->nStmt );
    sq->aStmt[i] = NULL;
    sqlite3_finalize(st);
    return 0;
  }else{
    return 1;
  }
}


/*
** Fetches the statement with the given ID, as returned by
** Th_query_AddStmt(). Returns NULL if stmtId does not refer (or no longer
** refers) to a statement added via Th_query_AddStmt().
*/
static sqlite3_stmt * Th_query_GetStmt(Th_Interp *interp, int stmtId){
  Th_Query * sq = Th_query_manager(interp);
  return (!sq || (stmtId<1) || (stmtId > sq->nStmt))
    ? NULL
    : sq->aStmt[stmtId-1];
}


/*
** Th_GCEntry finalizer which requires that p be a (Th_Query*).
*/
static void finalizerSqlite( Th_Interp * interp, void * p ){
  Th_Query * sq = (Th_Query *)p;
  int i;
  sqlite3_stmt * st = NULL;
  if(!sq) {
    fossil_warning("Got a finalizer call for a NULL Th_Query.");
    return;
  }
  for( i = 0; i < sq->nStmt; ++i ){
    st = sq->aStmt[i];
    if(NULL != st){
      fossil_warning("Auto-finalizing unfinalized "
                     "statement id #%d: %s",
                     i+1, sqlite3_sql(st));
      Th_query_FinalizeStmt( interp, i+1 );
    }
  }
  Th_Free(interp, sq->aStmt);
  Th_Free(interp, sq);
}


/*
** TH command:
**
** query prepare SQL
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
  rc = Th_query_AddStmt( interp, pStmt );
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
    pStmt = Th_query_GetStmt( interp, rc );
    if(NULL==pStmt){
      Th_ErrorMessage(interp, "no such statement handle:", arg, -1);
    }
  }
  return pStmt;

}

/*
** TH command:
**
** query finalize stmtId
** query stmtId finalize 
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
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 1 : 2;
  char * zSql;
  int stId = 0;
  char const * arg;
  int rc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle");
  }
  if(!pStmt){
    arg = argv[1];
    pStmt = queryStmtHandle(interp, arg, argl[1], &stId);
    if(!pStmt){
      Th_ErrorMessage(interp, "Not a valid statement handle argument.", NULL, 0);
      return TH_ERROR;
    }
  }
  assert( NULL != pStmt );
  rc = Th_query_FinalizeStmt2( interp, pStmt );
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
** modifies neither pStmt nor pIndex.
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
  stmt = *pStmt ? *pStmt : queryStmtHandle(interp, argv[1], argl[1], NULL);
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
** TH command:
**
** query step stmtId
** query stmtId step
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
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 1 : 2;
  int rc = 0;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle");
  }
  if(!pStmt && 0 != queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, NULL)){
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
** TH command:
**
** query StmtId reset
** query reset StmtId
**
** Equivalent to sqlite3_reset().
*/
static int queryResetCmd(
  Th_Interp *interp,
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int const rc = sqlite3_reset(pStmt);
  if(rc){
    Th_ErrorMessage(interp, "Reset of statement failed.", NULL, 0);
    return TH_ERROR;
  }else{
    return TH_OK;
  }
}


/*
** TH command:
**
** query col string stmtId Index
** query stmtId col string Index
** query stmtId col Index string
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 2 : 3;
  char const * val;
  int valLen;
  if( index >= 0 ) --requireArgc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  }else if(index<0){
    Th_ToInt(interp, argv[1], argl[1], &index);
  }
  if(index < 0){
    return TH_ERROR;
  }
  val = sqlite3_column_text( pStmt, index );
  valLen = val ? sqlite3_column_bytes( pStmt, index ) : 0;
  Th_SetResult( interp, val, valLen );
  return TH_OK;
}

/*
** TH command:
**
** query col int stmtId Index
** query stmtId col int Index
** query stmtId col Index int
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 2 : 3;
  int rc = 0;
  if( index >= 0 ) --requireArgc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  }else if(index<0){
    Th_ToInt(interp, argv[1], argl[1], &index);
  }
  if(index < 0){
    return TH_ERROR;
  }
  Th_SetResultInt( interp, sqlite3_column_int( pStmt, index ) );
  return TH_OK;
}

/*
** TH command:
**
** query col double stmtId Index
** query stmtId col double Index
** query stmtId col Index double
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 2 : 3;
  double rc = 0;
  if( index >= 0 ) --requireArgc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  }else if(index<0){
    Th_ToInt(interp, argv[1], argl[1], &index);
  }
  if(index < 0){
    return TH_ERROR;
  }
  Th_SetResultDouble( interp, sqlite3_column_double( pStmt, index ) );
  return TH_OK;
}

/*
** TH command:
**
** query col isnull stmtId Index
** query stmtId col isnull Index
** query stmtId col Index isnull
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 2 : 3;
  if( index >= 0 ) --requireArgc;
  double rc = 0;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  }else if(index<0){
    Th_ToInt(interp, argv[1], argl[1], &index);
  }
  if(index < 0){
    return TH_ERROR;
  }
  Th_SetResultInt( interp,
                   SQLITE_NULL==sqlite3_column_type( pStmt, index )
                   ? 1 : 0);
  return TH_OK;
}

/*
** TH command:
**
** query col type stmtId Index
** query stmtId col type Index
** query stmtId col Index type
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 2 : 3;
  if( index >= 0 ) --requireArgc;
  double rc = 0;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  }else if(index<0){
    Th_ToInt( interp, argv[1], argl[1], &index );
  }
  if(index < 0){
    return TH_ERROR;
  }
  Th_SetResultInt( interp, sqlite3_column_type( pStmt, index ) );
  return TH_OK;
}

/*
** TH command:
**
** query col count stmtId
** query stmtId col count
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
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 1 : 2;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                           argv[0], argl[0],
                           "StmtHandle");
  }
  if(!pStmt){
    pStmt = queryStmtHandle(interp, argv[1], argl[1], NULL);
    if( NULL == pStmt ){
      return TH_ERROR;
    }
  }
  rc = sqlite3_column_count( pStmt );
  Th_SetResultInt( interp, rc );
  return TH_OK;
}

/*
** TH command:
**
** query col name stmtId Index
** query stmtId col name Index
** query stmtId col Index name 
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 2 : 3;
  char const * val;
  int rc = 0;
  if( index >= 0 ) --requireArgc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  }else if(index<0){
    Th_ToInt( interp, argv[1], argl[1], &index );
  }
  if(index < 0){
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

/*
** TH command:
**
** query col time stmtId Index format
** query stmtId col name Index format
** query stmtId col Index name format
**
** Returns the result column name at the given 0-based index.
*/
static int queryColTimeCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)ctx;
  int minArgs = pStmt ? 3 : 4;
  int argPos;
  char const * val;
  char * fval;
  int i, rc = 0;
  char const * fmt;
  Blob sql = empty_blob;
  if( index >= 0 ) --minArgs;
  if( argc<minArgs ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index Format");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
    argPos = 3;
  }else if(index<0){
    Th_ToInt( interp, argv[1], argl[1], &index );
    argPos = 2;
  }else{
    argPos = 1;
  }
  if(index < 0){
    return TH_ERROR;
  }
  val = sqlite3_column_text( pStmt, index );
  fmt = argv[argPos++];
  assert(NULL!=pStmt);
  blob_appendf(&sql,"SELECT strftime(%Q,%Q",
               fmt, val);
  if(argc>argPos){
    for(i = argPos; i < argc; ++i ){
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
** TH command:
**
**  query strftime TimeVal ?Modifiers...?
**
** Acts as a proxy to sqlite3's strftime() SQL function.
*/
static int queryStrftimeCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
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
** TH command:
**
** query bind null stmtId Index
** query stmtId bind null Index
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 2 : 3;
  if( index > 0 ) --requireArgc;
  int rc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
  }else if(index<1){
    Th_ToInt( interp, argv[1], argl[1], &index );
  }
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
** TH command:
**
** query bind string stmtId Index Value
** query stmtId bind string Index Value
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 3 : 4;
  int rc;
  int argPos;
  if( index > 0 ) --requireArgc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index Value");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
    argPos = 3;
  }else if(index<1){
    Th_ToInt( interp, argv[1], argl[1], &index );
    argPos = 2;
  }else{
    argPos = 1;
  }
  if(index < 1){
    return TH_ERROR;
  }
  rc = sqlite3_bind_text( pStmt, index, argv[argPos], argl[argPos], SQLITE_TRANSIENT );
  if(rc){
    return queryReportDbErr( interp );
  }
  Th_SetResultInt( interp, 0 );
  return TH_OK;
}

/*
** TH command:
**
** query bind int stmtId Index Value
** query stmtId bind int Index Value
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 3 : 4;
  int rc;
  int argPos;
  int val;
  if( index > 0 ) --requireArgc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index Value");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
    argPos = 3;
  }else if(index<1){
    Th_ToInt( interp, argv[1], argl[1], &index );
    argPos = 2;
  }else{
    argPos = 1;
  }
  if(index < 1){
    return TH_ERROR;
  }
  if( 0 != Th_ToInt( interp, argv[argPos], argl[argPos], &val ) ){
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
** TH command:
**
** query bind double stmtId Index Value
** query stmtId bind double Index Value
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
  Th_Query * sq = Th_query_manager(interp);
  int index = sq->colCmdIndex;
  sqlite3_stmt * pStmt = (sqlite3_stmt*)p;
  int requireArgc = pStmt ? 3 : 4;
  int rc;
  int argPos;
  double val;
  if( index > 0 ) --requireArgc;
  if( argc!=requireArgc ){
    return Th_WrongNumArgs2(interp,
                            argv[0], argl[0],
                            "StmtHandle Index Value");
  }
  if(!pStmt){
    queryStmtIndexArgs(interp, argc, argv, argl, &pStmt, &index);
    argPos = 3;
  }else if(index<1){
    Th_ToInt( interp, argv[1], argl[1], &index );
    argPos = 2;
  }else{
    argPos = 1;
  }
  if(index < 1){
    return TH_ERROR;
  }
  if( 0 != Th_ToDouble( interp, argv[argPos], argl[argPos], &val ) ){
    return TH_ERROR;
  }

  rc = sqlite3_bind_double( pStmt, index, val );
  if(rc){
    return queryReportDbErr( interp );
  }
  Th_SetResultInt( interp, 0 );
  return TH_OK;
}

/*
** TH command:
**
** bind subcommand StmtId...
** bind StmtId subcommand...
**
** This is the top-level dispatcher for the "bind" family of commands.
*/
static int queryBindTopLevelCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  int colIndex = -1;
  static Th_SubCommand aSub[] = {
    {"int",    queryBindIntCmd},
    {"double", queryBindDoubleCmd},
    {"null",   queryBindNullCmd},
    {"string", queryBindStringCmd},
    {0, 0}
  };
  Th_Query * sq = Th_query_manager(interp);
  assert(NULL != sq);
  if( 1 == argc ){
      Th_WrongNumArgs2( interp, argv[0], argl[0],
                        "subcommand: int|double|null|string");
      return TH_ERROR;
  }else if( 0 == Th_TryInt(interp,argv[1], argl[1], &colIndex) ){
    if(colIndex <0){
      Th_ErrorMessage( interp, "Invalid column index.", NULL, 0);
      return TH_ERROR;
    }
    ++argv;
    ++argl;
    --argc;
  }
  sq->colCmdIndex = colIndex;
  Th_CallSubCommand2( interp, ctx, argc, argv, argl, aSub );

}

/*
** TH command:
**
** query col subcommand ...
** query StmtId col subcommand ...
**
** This is the top-level dispatcher for the col subcommands.
*/
static int queryColTopLevelCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  int colIndex = -1;
  static Th_SubCommand aSub[] = {
    {"count",   queryColCountCmd},
    {"is_null", queryColIsNullCmd},
    {"isnull",  queryColIsNullCmd},
    {"name",    queryColNameCmd},
    {"double",  queryColDoubleCmd},
    {"int",     queryColIntCmd},
    {"string",  queryColStringCmd},
    {"time",    queryColTimeCmd},
    {"type",    queryColTypeCmd},
    {0, 0}
  };
  static Th_SubCommand aSubWithIndex[] = {
    /*
      This subset is coded to accept the column index
      either before the subcommand name or after it.
      If called like (bind StmtId subcommand) then
      only these commands will be checked.
    */
    {"is_null", queryColIsNullCmd},
    {"isnull",  queryColIsNullCmd},
    {"name",    queryColNameCmd},
    {"double",  queryColDoubleCmd},
    {"int",     queryColIntCmd},
    {"string",  queryColStringCmd},
    {"time",    queryColTimeCmd},
    {"type",    queryColTypeCmd},
    {0, 0}
  };
  Th_Query * sq = Th_query_manager(interp);
  assert(NULL != sq);
  if( 1 == argc ){
      Th_WrongNumArgs2( interp, argv[0], argl[0],
                        "subcommand: "
                        "count|is_null|isnull|name|"
                        "double|int|string|time|type");
      return TH_ERROR;
  }else if( 0 == Th_TryInt(interp,argv[1], argl[1], &colIndex) ){
    if(colIndex <0){
      Th_ErrorMessage( interp, "Invalid column index.", NULL, 0);
      return TH_ERROR;
    }
    ++argv;
    ++argl;
    --argc;
  }
  sq->colCmdIndex = colIndex;
  Th_CallSubCommand2( interp, ctx, argc, argv, argl,
                      (colIndex<0) ? aSub : aSubWithIndex );
}


/*
** TH command:
**
** query subcommand ...
** query StmtId subcommand ...
**
** This is the top-level dispatcher for the query subcommand.
*/
static int queryTopLevelCmd(
  Th_Interp *interp,
  void *ctx, 
  int argc, 
  const char **argv, 
  int *argl
){
  int stmtId = 0;
  sqlite3_stmt * pStmt = NULL;
  static Th_SubCommand aSubAll[] = {
    {"bind",        queryBindTopLevelCmd},
    {"col",         queryColTopLevelCmd},
    {"finalize",    queryFinalizeCmd},
    {"prepare",     queryPrepareCmd},
    {"reset",       queryResetCmd},
    {"step",        queryStepCmd},
    {"strftime",    queryStrftimeCmd},
    {0, 0}
  };
  static Th_SubCommand aSubWithStmt[] = {
    /* This subset is coded to deal with being supplied a statement
       via pStmt or via one of their args. When called like (query
       StmtId ...) only these subcommands will be checked.*/
    {"bind",        queryBindTopLevelCmd},
    {"col",         queryColTopLevelCmd},
    {"step",        queryStepCmd},
    {"finalize",    queryFinalizeCmd},
    {"reset",       queryResetCmd},
    {0, 0}
  };


  assert( NULL != Th_query_manager(interp) );
  if( 1 == argc ){
      Th_WrongNumArgs2( interp, argv[0], argl[0],
                        "subcommand: bind|col|finalize|prepare|reset|step|strftime");
      return TH_ERROR;
  }else if( 0 == Th_TryInt(interp,argv[1], argl[1], &stmtId) ){
    ++argv;
    ++argl;
    --argc;
    pStmt = Th_query_GetStmt( interp, stmtId );
  }

  Th_CallSubCommand2( interp, pStmt, argc, argv, argl,
                      pStmt ? aSubWithStmt : aSubAll );
}

/*
** Registers the "query" API with the given interpreter. Returns TH_OK
** on success, TH_ERROR on error.
*/
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
  rc = Th_RegisterCommands( interp, aCommand );
  if(TH_OK==rc){
    Th_Query * sq = Th_Malloc(interp, sizeof(Th_Query));
    if(!sq){
      rc = TH_ERROR;
    }else{
      assert( NULL == sq->aStmt );
      assert( 0 == sq->nStmt );
      Th_SetData( interp, Th_Query_KEY, sq, finalizerSqlite );
      assert( sq == Th_query_manager(interp) );
    }
  }
  return rc;
}

#endif
/* end TH_ENABLE_QUERY */

/*
** Make sure the interpreter has been initialized.  Initialize it if
** it has not been already.
**
** The interpreter is stored in the g.interp global variable.
*/
void Th_FossilInit(void){
  /* The fossil-internal Th_Vtab instance. */
  static Th_Vtab vtab = { xRealloc, {/*out*/
    NULL /*write()*/,
    NULL/*dispose()*/,
    NULL/*pState*/,
    1/*enabled*/
    }
  };

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
      vtab.out.write = Th_Output_f_cgi_content;
    }else{
      vtab.out = Th_Vtab_OutputMethods_FILE;
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
#ifdef TH_ENABLE_OB
    th_register_ob(g.interp);
#endif
#ifdef TH_ENABLE_QUERY
    th_register_query(g.interp);
#endif
#ifdef TH_ENABLE_ARGV
    th_register_argv(g.interp);
#endif
    Th_RegisterCommands( g.interp, aCommand );
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
** via Th_Output().
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
** which may contain TH1 tag blocks and variables in the form $var or
** $<var>. Each block is executed in the same TH1 interpreter
** instance.
**
** ACHTUNG: not all of the $variables which are set in CGI mode
** are available via this (CLI) command.
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
#ifdef TH_ENABLE_QUERY
  db_find_and_open_repository(OPEN_ANY_SCHEMA,0)
    /* required for th1 query API. */;
#endif
  blob_read_from_file(&in, g.argv[2]);
  Th_Render(blob_str(&in), Th_Render_Flags_DEFAULT);
}
