/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used render and control ticket entry
** and display pages.
*/
#if 0
#include "config.h"
#include "tkt.h"
#include <assert.h>

/*
** Flags to indicate what kind of ticket string is being generated.
** A bitmask of these is associated with each verb in order to indicate
** which verbs go on which pages.
*/
#define M_NEW  0x01
#define M_EDIT 0x02
#define M_VIEW 0x04

/*
** The Subscript interpreter used to parse the ticket configure
** and to render ticket screens.
*/
static struct Subscript *pInterp = 0;

/*
** The list of database fields in the ticket table.
** This is the user-defined list in the configuration file.
** Add the "tkt_" prefix to all of these names in the real table.
** The real table also contains some addition fields not found
** here.
*/
static int nField = 0;
static Blob fieldList;
static char **azField = 0;
static char **azValue = 0;
static unsigned char *aChanged = 0;

/*
** Compare two entries in azField for sorting purposes
*/
static int nameCmpr(void *a, void *b){
  return strcmp((char*)a, (char*)b);
}

/*
** Subscript command:      LIST setfields
**
** Parse up the list and populate the nField and azField variables.
*/
static int setFieldsCmd(struct Subscript *p, void *pNotUsed){
  if( SbS_RequireStack(p, 1) ) return 1;
  if( nField==0 ){
    char *zFieldList;
    int nFieldList, i;
    Blob field;
    blob_zero(&fieldList);
    zFieldList = SbS_StackValue(p, 0, &nFieldList);
    blob_appendf(&fieldList, zFieldList, nFieldList);
    while( blob_token(&fieldList, &field) ){
      nField++;
    }
    azField = malloc( sizeof(azField[0])*nField*2 + nField );
    if( azField ){
      azValue = &azField[nField];
      aChanged = (unsigned char*)&azValue[nField];
      blob_rewind(&fieldList);
      i = 0;
      while( blob_token(&fieldList, &field) ){
        azField[i] = blob_terminate(&field);
        azValue[i] = 0;
        aChanged[i] = 0;
      }
    }
    qsort(azField, nField, sizeof(azField[0]), nameCmpr);
  }
  SbS_Pop(p, 1);
  return 0;
}

/*
** Find the text of the field whose name is the Nth element down
** on the Subscript stack.  0 means the top of the stack.
**
** First check for a value for this field as passed in via
** CGI parameter.  If not found, then use the value from the
** database.
*/
static const char *field_value(int N){
  const char *zFName;
  int nFName;
  char *zName;
  int i;
  const char *zValue;
  
  zFName = SbS_StackValue(pInterp, N, &nFName);
  if( zField==0 ){
    return 0;
  }
  zName = mprintf("%.*s", nFName, zFName);
  zValue = P(zName);
  if( zValue==0 ){
    for(i=0; i<nField; i++){
      if( strcmp(azField[i], zName)==0 ){
        zValue = azValue[i];
        break;
      }
    }
  }
  free(zName);
  return zValue;
}

/*
** Fill in the azValue[] array with the contents of the ticket
** table for the entry determined by the "name" CGI parameter.
*/
static void fetchOriginalValues(void){
  Blob sql;
  Stmt q;
  int i;
  char *zSep = "SELECT ";
  blob_zero(&sql);
  for(i=0; i<nField; i++){
    blob_appendf(&sql, "%stkt_%s", zSep, azField[i]);
    zSep = ", ";
  }
  blob_appendf(" FROM ticket WHERE uuid=%Q", PD("name",""));
  db_prepare(&q, "%b", &sql);
  if( db_step(&q)==SQLITE_ROW ){
    for(i=0; i<nField; i++){
      azValue[i] = db_column_malloc(&q, i);
    }
  }
  db_finalize(&q);
}

/*
** Subscript command:      INTEGER not INTEGER
*/
static int notCmd(struct Subscript *p, void *pNotUsed){
  int n;
  if( SbS_RequireStack(p, 1) ) return 1;
  n = SbS_StackValueInt(p, 0);
  SbS_Pop(p, 1);
  SbS_PushInt(p, !n);
  return 0;
}

/*
** Subscript command:      INTEGER INTEGER max INTEGER
*/
static int maxCmd(struct Subscript *p, void *pNotUsed){
  int a, b;
  if( SbS_RequireStack(p, 2) ) return 1;
  a = SbS_StackValueInt(p, 0);
  b = SbS_StackValueInt(p, 1);
  SbS_Pop(p, 2);
  SbS_PushInt(p, a>b ? a : b);
  return 0;
}

/*
** Subscript command:      INTEGER INTEGER and INTEGER
*/
static int andCmd(struct Subscript *p, void *pNotUsed){
  int a, b;
  if( SbS_RequireStack(p, 2) ) return 1;
  a = SbS_StackValueInt(p, 0);
  b = SbS_StackValueInt(p, 1);
  SbS_Pop(p, 2);
  SbS_PushInt(p, a && b);
  return 0;
}

/*
** Subscript command:      FIELD wikiview
*/
static int wikiViewCmd(struct Subscript *p, void *pNotUsed){
  if( SbS_RequireStack(p, 2) ) return 1;
  
  SbS_Pop(p, 1);
  return 0;
}


/*
** Create an Subscript interpreter appropriate for processing
** Ticket pages.
*/
static void tkt_screen_init(int flags){
  char *zConfig;
  int i;
  static const struct {
     const char *zName;
     int (*xVerb);
     int mask;
  } aVerb[] = {
    { "not",              notCmd,                M_NEW|M_EDIT|M_VIEW },
    { "max",              maxCmd,                M_NEW|M_EDIT|M_VIEW },
    { "and",              andCmd,                M_NEW|M_EDIT|M_VIEW },
    { "wikiview",         wikiViewCmd,           M_NEW|M_EDIT|M_VIEW },
    { "textview",         textViewCmd,           M_NEW|M_EDIT|M_VIEW },
    { "linecount",        lineCountCmd,          M_NEW|M_EDIT|M_VIEW },
    { "cgiparam",         cgiParamCmd,           M_NEW|M_EDIT|M_VIEW },
    { "enable_output",    enableOutputCmd,       M_NEW|M_EDIT|M_VIEW },
    { "is_anon",          isAnonCmd,             M_NEW|M_EDIT|M_VIEW },
    { "ok_wrtkt",         okWrTktCmd,            M_NEW|M_EDIT|M_VIEW },
    { "default_value",    dfltValueCmd,          M_NEW               },
    { "textedit",         textEditCmd,           M_NEW|M_EDIT        },
    { "combobox",         comboBoxCmd,           M_NEW|M_EDIT        },
    { "multilineedit",    multiLineEditCmd,      M_NEW|M_EDIT        },
    { "multilineappend",  multiAppendCmd,              M_EDIT        },
    { "auxbutton",        auxButtonCmd,          M_NEW|M_EDIT        },
    { "submitbutton",     submitButtonCmd,       M_NEW|M_EDIT        },
  };

  pInterp = SbS_Create();
  SbS_AddVerb(pInterp, "setfields", setFieldsCmd, 0);
  zConfig = db_get("ticket-config","");
  SbS_Eval(pInter, zConfig, -1);
  for(i=0; i<sizeof(aVerb)/sizeof(aVerb[0]); i++){
    if( flags & aVerb[i].mask ){
      SbS_AddVerb(pInterp, aVerb[i].zName, aVerb[i].xVerb, 0);
    }
  }
  /* Extract appropriate template */
  return pInterp;
}

/*
** PAGE: tktnew
*/
void tktnew_page(void){
  struct Subscript *pInterp;
  const char *zPage;
  int nPage;

  tkt_screen_init(M_NEW);
  if( P("submit")!=0 ){
    // * Construct the ticket artifact
    //    + Prefix
    //    + Field/Value pairs in sorted order
    //    + Suffix
    // * Register the artifact
    // * Update the ticket table
    // * redirect to the ticket viewer
  }
  style_header("New Ticket");
  @ This will become a page for entering new tickets.
  style_footer();
}

/*
** PAGE: tktview
** URL: tktview?name=UUID
**
*/
void tktedit_page(void){
  struct Subscript *pInterp;
  const char *zPage;
  int nPage;

  tkt_screen_init(M_VIEW);
  style_header("View Ticket");
  @ This will become a page for entering new tickets.
  style_footer();
}

/*
** PAGE: tktedit
** URL: tktedit?name=UUID
**
*/
void tktedit_page(void){
  struct Subscript *pInterp;
  const char *zPage;
  int nPage;

  tkt_screen_init(M_EDIT);
  if( P("submit") ){
    // * Construct ticket change artifact
    //   +  Prefix
    //   +  Modified field/value pairs in sorted order
    //   +  Suffix
    // * Register the artifact
    // * Update the ticket table
    // * redirect to the ticket viewer
  }
  style_header("Edit Ticket");
  @ This will become a page for entering new tickets.
  style_footer();
}
#endif
