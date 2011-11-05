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
** Interfaces to register the scripting language extensions.
*/
int register_tcl(Jim_Interp *interp, void *pContext); /* th_tcl.c */

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
static long enableOutput = 1;

/*
** TH command:     enable_output BOOLEAN
**
** Enable or disable the puts and hputs commands.
*/
static int enableOutputCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  if( argc!=2 ){
    Jim_WrongNumArgs(interp, 1, argv, "BOOLEAN");
    return JIM_ERR;
  }
  return Jim_GetLong(interp, argv[1], &enableOutput);
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
    if( encode ) free((char*)z);
  }
}

static void sendTextObj(Jim_Obj *objPtr, int encode)
{
  sendText(Jim_String(objPtr), Jim_Length(objPtr), encode);
}

/*
** TH command:     puts STRING
**
** Output STRING as HTML
*/
static int putsCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  if( argc!=2 ){
    Jim_WrongNumArgs(interp, 1, argv, "STRING");
    return JIM_ERR;
  }
  sendText(Jim_String(argv[1]), -1, 1);
  return JIM_OK;
}

/*
** TH command:     html STRING
**
** Output STRING unchanged
*/
static int htmlCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  if( argc!=2 ){
    Jim_WrongNumArgs(interp, 1, argv, "STRING");
    return JIM_ERR;
  }
  sendText(Jim_String(argv[1]), -1, 0);
  return JIM_OK;
}

/*
** TH command:      wiki STRING
**
** Render the input string as wiki.
*/
static int wikiCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  if( argc!=2 ){
    Jim_WrongNumArgs(interp, 1, argv, "STRING");
    return JIM_ERR;
  }
  if( enableOutput ){
    Blob src;
    blob_init(&src, Jim_String(argv[1]), Jim_Length(argv[1]));
    wiki_convert(&src, 0, WIKI_INLINE);
    blob_reset(&src);
  }
  return JIM_OK;
}

/*
** TH command:      htmlize STRING
**
** Escape all characters of STRING which have special meaning in HTML.
** Return a new string result.
*/
static int htmlizeCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  char *zOut;
  if( argc!=2 ){
    Jim_WrongNumArgs(interp, 1, argv, "STRING");
    return JIM_ERR;
  }
  zOut = htmlize(Jim_String(argv[1]), Jim_Length(argv[1]));
  Jim_SetResultString(interp, zOut, -1);
  free(zOut);
  return JIM_OK;
}

/*
** TH command:      date
**
** Return a string which is the current time and date.  If the
** -local option is used, the date appears using localtime instead
** of UTC.
*/
static int dateCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  char *zOut;
  if( argc>=2 && Jim_CompareStringImmediate(interp, argv[1], "-local")) {
    zOut = db_text("??", "SELECT datetime('now','localtime')");
  }else{
    zOut = db_text("??", "SELECT datetime('now')");
  }
  Jim_SetResultString(interp, zOut, -1);
  free(zOut);
  return JIM_OK;
}

/*
** TH command:     hascap STRING
**
** Return true if the user has all of the capabilities listed in STRING.
*/
static int hascapCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  int rc;
  const char *str;
  int len;
  if( argc!=2 ){
    Jim_WrongNumArgs(interp, 1, argv, "STRING");
    return JIM_ERR;
  }
  str = Jim_GetString(argv[1], &len);
  rc = login_has_capability(str, len);
  if( g.thTrace ){
    Th_Trace("[hascap %#h] => %d<br />\n", len, str, rc);
  }
  Jim_SetResultInt(interp, rc);
  return JIM_OK;
}

/*
** TH command:     anycap STRING
**
** Return true if the user has any one of the capabilities listed in STRING.
*/
static int anycapCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  int rc = 0;
  int i;
  const char *str;
  int len;
  if( argc!=2 ){
    Jim_WrongNumArgs(interp, 1, argv, "STRING");
    return JIM_ERR;
  }
  str = Jim_GetString(argv[1], &len);
  for(i=0; rc==0 && i<len; i++){
    rc = login_has_capability(&str[i],1);
  }
  if( g.thTrace ){
    Th_Trace("[hascap %#h] => %d<br />\n", len, str, rc);
  }
  Jim_SetResultInt(interp, rc);
  return JIM_OK;
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
static int comboboxCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  if( argc!=4 ){
    Jim_WrongNumArgs(interp, 1, argv, "NAME TEXT-LIST NUMLINES");
    return JIM_ERR;
  }
  if( enableOutput ){
    long height;
    char *z, *zH;
    int nElem;
    int i;
    Jim_Obj *objPtr;
    Jim_Obj *varObjPtr;

    if( Jim_GetLong(interp, argv[3], &height) ) return JIM_ERR;
    nElem = Jim_ListLength(interp, argv[2]);

    varObjPtr = Jim_GetVariable(g.interp, argv[1], JIM_NONE);
    z = mprintf("<select name=\"%z\" size=\"%d\">", 
                 htmlize(Jim_String(varObjPtr), Jim_Length(varObjPtr)), height);
    sendText(z, -1, 0);
    free(z);
    for(i=0; i<nElem; i++){
      Jim_ListIndex(interp, argv[2], i, &objPtr, JIM_NONE);
      zH = htmlize(Jim_String(objPtr), Jim_Length(objPtr));
      if( varObjPtr && Jim_StringEqObj(varObjPtr, objPtr)) {
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
  }
  return JIM_OK;
}

/*
** TH1 command:     linecount STRING MAX MIN
**
** Return one more than the number of \n characters in STRING.  But
** never return less than MIN or more than MAX.
*/
static int linecntCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  const char *z;
  int size, n, i;
  jim_wide iMin, iMax;
  if( argc!=4 ){
    Jim_WrongNumArgs(interp, 1, argv, "STRING MAX MIN");
    return JIM_ERR;
  }
  if( Jim_GetWide(interp, argv[2], &iMax) ) return JIM_ERR;
  if( Jim_GetWide(interp, argv[3], &iMin) ) return JIM_ERR;
  z = Jim_GetString(argv[1], &size);
  for(n=1, i=0; i<size; i++){
    if( z[i]=='\n' ){
      n++;
      if( n>=iMax ) break;
    }
  }
  if( n<iMin ) n = iMin;
  if( n>iMax ) n = iMax;
  Jim_SetResultInt(interp, n);
  return JIM_OK;
}

/*
** TH1 command:     repository ?BOOLEAN?
**
** Return the fully qualified file name of the open repository or an empty
** string if one is not currently open.  Optionally, it will attempt to open
** the repository if the boolean argument is non-zero.
*/
static int repositoryCmd(Jim_Interp *interp, int argc, Jim_Obj *const *argv)
{
  long openRepository;

  if( argc!=1 && argc!=2 ){
    Jim_WrongNumArgs(interp, 1, argv, "BOOLEAN");
    return JIM_ERR;
  }
  if( argc==2 ){
    if( Jim_GetLong(interp, argv[1], &openRepository) != JIM_OK){
      return JIM_ERR;
    }
    if( openRepository ) db_find_and_open_repository(OPEN_OK_NOT_FOUND, 0);
  }
  Jim_SetResultString(interp, g.zRepositoryName, -1);
  return JIM_OK;
}

/*
** Make sure the interpreter has been initialized.  Initialize it if
** it has not been already.
**
** The interpreter is stored in the g.interp global variable.
*/
void Th_FossilInit(void){
  static struct _Command {
    const char *zName;
    Jim_CmdProc xProc;
  } aCommand[] = {
    {"anycap",        anycapCmd,            },
    {"combobox",      comboboxCmd,          },
    {"enable_output", enableOutputCmd,      },
    {"linecount",     linecntCmd,           },
    {"hascap",        hascapCmd,            },
    {"htmlize",       htmlizeCmd,           },
    {"date",          dateCmd,              },
    {"html",          htmlCmd,              },
    {"puts",          putsCmd,              },
    {"wiki",          wikiCmd,              },
    {"repository",    repositoryCmd,        },
  };
  if( g.interp==0 ){
    int i;
    /* Create and initialize the interpreter */
    g.interp = Jim_CreateInterp();
    Jim_RegisterCoreCommands(g.interp);

    /* Register static extensions */
    Jim_InitStaticExtensions(g.interp);

#ifdef FOSSIL_ENABLE_TCL
    if( getenv("FOSSIL_ENABLE_TCL")!=0 || db_get_boolean("tcl", 0) ){
      register_tcl(g.interp, &g.tcl);  /* Tcl integration commands. */
    }
#endif
    for(i=0; i<sizeof(aCommand)/sizeof(aCommand[0]); i++){
      Jim_CreateCommand(g.interp, aCommand[i].zName, aCommand[i].xProc, NULL,
          NULL);
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
    Jim_SetVariableStrWithStr(g.interp, zName, zValue);
  }
}

/*
** Unset a variable.
*/
void Th_Unstore(const char *zName){
  if( g.interp ){
    Jim_Obj *nameObjPtr = Jim_NewStringObj(g.interp, zName, -1);
    Jim_UnsetVariable(g.interp, nameObjPtr, JIM_NONE);
    Jim_FreeNewObj(g.interp, nameObjPtr);
  }
}

/*
** Retrieve a string value (variable) from the interpreter.  If no such
** variable exists, return NULL.
*/
const char *Th_Fetch(const char *zName){
  Th_FossilInit();

  Jim_Obj *objPtr = Jim_GetVariableStr(g.interp, zName, JIM_NONE);

  return objPtr ? Jim_String(objPtr) : NULL;
}

/**
 * Like Th_Fetch() except the variable name may not be null terminated.
 * Instead, the length of the name is supplied as 'namelen'.
 */
const char *Th_GetVar(Jim_Interp *interp, const char *name, int namelen){
    Jim_Obj *nameObjPtr, *varObjPtr;

    nameObjPtr = Jim_NewStringObj(interp, name, namelen);
    Jim_IncrRefCount(nameObjPtr);
    varObjPtr = Jim_GetVariable(interp, nameObjPtr, 0);
    Jim_DecrRefCount(interp, nameObjPtr);

    return varObjPtr ? Jim_String(varObjPtr) : NULL;
}

/*
** Return true if the string begins with the TH1 begin-script
** tag:  <th1>.
*/
static int isBeginScriptTag(const char *z){
  /* XXX: Should we also allow <tcl>? */
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
  /* XXX: Should we also allow </tcl>? */
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
  int rc = JIM_OK;
  const char *zResult;
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
      zResult = Th_GetVar(g.interp, zVar, nVar);
      z += i+1+n;
      i = 0;
      if (zResult) {
        sendText(zResult, -1, encode);
      }
    }else if( z[i]=='<' && isBeginScriptTag(&z[i]) ){
      Jim_Obj *objPtr;
      sendText(z, i, 0);
      z += i+5;
      for(i=0; z[i] && (z[i]!='<' || !isEndScriptTag(&z[i])); i++){}
      /* XXX: Would be nice to record the source location in case of error */
      objPtr = Jim_NewStringObj(g.interp, z, i);
      rc = Jim_EvalObj(g.interp, objPtr);
      if( rc!=JIM_OK ) break;
      z += i;
      if( z[0] ){ z += 6; }
      i = 0;
    }else{
      i++;
    }
  }
  if( rc==JIM_ERR ){
    sendText("<hr><p class=\"thmainError\">ERROR: ", -1, 0);
    sendTextObj(Jim_GetResult(g.interp), 1);
    sendText("</p>", -1, 0);
  }else{
    sendText(z, i, 0);
  }
  return rc;
}

/*
** COMMAND: test-script-render
*/
void test_script_render(void){
  Blob in;
  if( g.argc<3 ){
    usage("FILE");
  }
  db_open_config(0); /* Needed for "tcl" setting. */
  blob_zero(&in);
  blob_read_from_file(&in, g.argv[2]);
  Th_Render(blob_str(&in));
}
