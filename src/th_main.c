/*
** Copyright (c) 2008 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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
** A pointer to the single TH interpreter used within fossil.
*/
static Th_Interp *interp = 0;


/*
** Implementations of malloc() and free() to pass to the interpreter.
*/
static void *xMalloc(unsigned int n){
  void *p = malloc(n);
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
  const unsigned char **argv, 
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
    if( g.cgiPanic ){
      cgi_append_content(z, n);
    }else{
      fwrite(z, 1, n, stdout);
    }
    if( encode ) free((char*)z);
  }
}

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
  const unsigned char **argv, 
  int *argl
){
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "puts STRING");
  }
  sendText((char*)argv[1], argl[1], pConvert!=0);
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
  const unsigned char **argv, 
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
** TH command:     hascap STRING
**
** Return true if the user has all of the capabilities listed in STRING.
*/
static int hascapCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const unsigned char **argv, 
  int *argl
){
  if( argc!=2 ){
    return Th_WrongNumArgs(interp, "hascap STRING");
  }
  Th_SetResultInt(interp, login_has_capability((char*)argv[1],argl[1]));
  return TH_OK;
}

/*
** Make sure the interpreter has been initialized.
*/
static void initializeInterp(void){
  static struct _Command {
    const char *zName;
    Th_CommandProc xProc;
    void *pContext;
  } aCommand[] = {
    {"enable_output", enableOutputCmd,      0},
    {"hascap",        hascapCmd,            0},
    {"html",          putsCmd,              0},
    {"puts",          putsCmd,       (void*)1},
    {"wiki",          wikiCmd,              0},
  };
  if( interp==0 ){
    int i;
    interp = Th_CreateInterp(&vtab);
    th_register_language(interp);       /* Basic scripting commands. */
    for(i=0; i<sizeof(aCommand)/sizeof(aCommand[0]); i++){
      Th_CreateCommand(interp, aCommand[i].zName, aCommand[i].xProc,
                       aCommand[i].pContext, 0);
    }
  }
}

/*
** Initialize a variable in the interpreter.
*/
void Th_InitVar(const char *zName, const char *zValue){
  initializeInterp();
  Th_SetVar(interp, (uchar*)zName, -1, (uchar*)zValue, strlen(zValue));
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
  if( z[0]==':' && z[1]==':' && isalpha(z[2]) ){
    z += 3;
    i += 3;
  }else if( isalpha(z[0]) ){
    z ++;
    i += 1;
  }else{
    return 0;
  }
  while( isalnum(z[0]) || z[0]=='_' ){
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
  uchar *zResult;
  initializeInterp();
  while( z[i] ){
    if( z[i]=='$' && (n = validVarName(&z[i+1]))>0 ){
      const char *zVar;
      int nVar;
      sendText(z, i, 0);
      if( z[i+1]=='<' ){
        /* Variables of the form $<aaa> */
        zVar = &z[i+2];
        nVar = n-2;
      }else{
        /* Variables of the form $aaa */
        zVar = &z[i+1];
        nVar = n;
      }
      rc = Th_GetVar(interp, (uchar*)zVar, nVar);
      z += i+1+n;
      i = 0;
      zResult = (uchar*)Th_GetResult(interp, &n);
      sendText((char*)zResult, n, n>nVar);
    }else if( z[i]=='<' && isBeginScriptTag(&z[i]) ){
      sendText(z, i, 0);
      z += i+5;
      for(i=0; z[i] && (z[i]!='<' || !isEndScriptTag(&z[i])); i++){}
      rc = Th_Eval(interp, 0, (const uchar*)z, i);
      if( rc!=SBS_OK ) break;
      z += i;
      if( z[0] ){ z += 6; }
      i = 0;
    }else{
      i++;
    }
  }
  if( rc==TH_ERROR ){
    sendText("<hr><p><font color=\"red\"><b>ERROR: ", -1, 0);
    zResult = (uchar*)Th_GetResult(interp, &n);
    sendText((char*)zResult, n, 1);
    sendText("</b></font></p>", -1, 0);
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
  blob_zero(&in);
  blob_read_from_file(&in, g.argv[2]);
  Th_Render(blob_str(&in));
}
