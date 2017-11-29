/*
** Copyright (c) 2006,2007 D. Richard Hipp
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
** This file contains code to implement the basic web page look and feel.
**
*/
#include "VERSION.h"
#include "config.h"
#include "style.h"


/*
** Elements of the submenu are collected into the following
** structure and displayed below the main menu.
**
** Populate these structure with calls to
**
**      style_submenu_element()
**      style_submenu_entry()
**      style_submenu_checkbox()
**      style_submenu_binary()
**      style_submenu_multichoice()
**      style_submenu_sql()
**
** prior to calling style_footer().  The style_footer() routine
** will generate the appropriate HTML text just below the main
** menu.
*/
static struct Submenu {
  const char *zLabel;        /* Button label */
  const char *zLink;         /* Jump to this link when button is pressed */
} aSubmenu[30];
static int nSubmenu = 0;     /* Number of buttons */
static struct SubmenuCtrl {
  const char *zName;           /* Form query parameter */
  const char *zLabel;          /* Label.  Might be NULL for FF_MULTI */
  unsigned char eType;         /* FF_ENTRY, FF_MULTI, FF_BINARY */
  unsigned char eVisible;      /* STYLE_NORMAL, STYLE_VISIBLE, .... */
  short int iSize;             /* Width for FF_ENTRY.  Count for FF_MULTI */
  const char *const *azChoice; /* value/display pairs for FF_MULTI */
  const char *zFalse;          /* FF_BINARY label when false */
  const char *zJS;             /* Javascript to run on toggle */
} aSubmenuCtrl[20];
static int nSubmenuCtrl = 0;
#define FF_ENTRY    1          /* Text entry box */
#define FF_MULTI    2          /* Combobox.  Multiple choices. */
#define FF_BINARY   3          /* Control binary query parameter */
#define FF_CHECKBOX 4          /* Check-box with JS */
#define FF_JSBUTTON 5          /* Run JS when clicked */

#if INTERFACE
#define STYLE_NORMAL   0       /* Normal display of control */
#define STYLE_DISABLED 1       /* Control is disabled */
#define STYLE_CLUTTER  2       /* Only visible in "Advanced" display */
#define STYLE_BASIC    4       /* Only visible in "Basic" display */
#endif /* INTERFACE */

/*
** Remember that the header has been generated.  The footer is omitted
** if an error occurs before the header.
*/
static int headerHasBeenGenerated = 0;

/*
** remember, if a sidebox was used
*/
static int sideboxUsed = 0;

/*
** Ad-unit styles.
*/
static unsigned adUnitFlags = 0;


/*
** List of hyperlinks and forms that need to be resolved by javascript in
** the footer.
*/
char **aHref = 0;
int nHref = 0;
int nHrefAlloc = 0;
char **aFormAction = 0;
int nFormAction = 0;

/*
** Generate and return a anchor tag like this:
**
**        <a href="URL">
**  or    <a id="ID">
**
** The form of the anchor tag is determined by the g.javascriptHyperlink
** variable.  The href="URL" form is used if g.javascriptHyperlink is false.
** If g.javascriptHyperlink is true then the
** id="ID" form is used and javascript is generated in the footer to cause
** href values to be inserted after the page has loaded.  If
** g.perm.History is false, then the <a id="ID"> form is still
** generated but the javascript is not generated so the links never
** activate.
**
** If the user lacks the Hyperlink (h) property and the "auto-hyperlink"
** setting is true, then g.perm.Hyperlink is changed from 0 to 1 and
** g.javascriptHyperlink is set to 1.  The g.javascriptHyperlink defaults
** to 0 and only changes to one if the user lacks the Hyperlink (h) property
** and the "auto-hyperlink" setting is enabled.
**
** Filling in the href="URL" using javascript is a defense against bots.
**
** The name of this routine is deliberately kept short so that can be
** easily used within @-lines.  Example:
**
**      @ %z(href("%R/artifact/%s",zUuid))%h(zFN)</a>
**
** Note %z format.  The string returned by this function is always
** obtained from fossil_malloc() so rendering it with %z will reclaim
** that memory space.
**
** There are two versions of this routine: href() does a plain hyperlink
** and xhref() adds extra attribute text.
**
** g.perm.Hyperlink is true if the user has the Hyperlink (h) property.
** Most logged in users should have this property, since we can assume
** that a logged in user is not a bot.  Only "nobody" lacks g.perm.Hyperlink,
** typically.
*/
char *xhref(const char *zExtra, const char *zFormat, ...){
  char *zUrl;
  va_list ap;
  va_start(ap, zFormat);
  zUrl = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.perm.Hyperlink && !g.javascriptHyperlink ){
    char *zHUrl = mprintf("<a %s href=\"%h\">", zExtra, zUrl);
    fossil_free(zUrl);
    return zHUrl;
  }
  if( nHref>=nHrefAlloc ){
    nHrefAlloc = nHrefAlloc*2 + 10;
    aHref = fossil_realloc(aHref, nHrefAlloc*sizeof(aHref[0]));
  }
  aHref[nHref++] = zUrl;
  return mprintf("<a %s id='a%d' href='%R/honeypot'>", zExtra, nHref);
}
char *href(const char *zFormat, ...){
  char *zUrl;
  va_list ap;
  va_start(ap, zFormat);
  zUrl = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.perm.Hyperlink && !g.javascriptHyperlink ){
    char *zHUrl = mprintf("<a href=\"%h\">", zUrl);
    fossil_free(zUrl);
    return zHUrl;
  }
  if( nHref>=nHrefAlloc ){
    nHrefAlloc = nHrefAlloc*2 + 10;
    aHref = fossil_realloc(aHref, nHrefAlloc*sizeof(aHref[0]));
  }
  aHref[nHref++] = zUrl;
  return mprintf("<a id='a%d' href='%R/honeypot'>", nHref);
}

/*
** Generate <form method="post" action=ARG>.  The ARG value is inserted
** by javascript.
*/
void form_begin(const char *zOtherArgs, const char *zAction, ...){
  char *zLink;
  va_list ap;
  if( zOtherArgs==0 ) zOtherArgs = "";
  va_start(ap, zAction);
  zLink = vmprintf(zAction, ap);
  va_end(ap);
  if( g.perm.Hyperlink && !g.javascriptHyperlink ){
    @ <form method="POST" action="%z(zLink)" %s(zOtherArgs)>
  }else{
    int n;
    aFormAction = fossil_realloc(aFormAction, (nFormAction+1)*sizeof(char*));
    aFormAction[nFormAction++] = zLink;
    n = nFormAction;
    @ <form id="form%d(n)" method="POST" action='%R/login' %s(zOtherArgs)>
  }
}

/*
** Generate javascript that will set the href= attribute on all anchors.
*/
void style_resolve_href(void){
  int i;
  int nDelay = db_get_int("auto-hyperlink-delay",10);
  if( !g.perm.Hyperlink ) return;
  if( nHref==0 && nFormAction==0 ) return;
  @ <script>
  @ function setAllHrefs(){
  if( g.javascriptHyperlink ){
    for(i=0; i<nHref; i++){
      @ gebi("a%d(i+1)").href="%s(aHref[i])";
    }
  }
  for(i=0; i<nFormAction; i++){
    @ gebi("form%d(i+1)").action="%s(aFormAction[i])";
  }
  @ }
  if( sqlite3_strglob("*Opera Mini/[1-9]*", PD("HTTP_USER_AGENT",""))==0 ){
    /* Special case for Opera Mini, which executes JS server-side */
    @ var isOperaMini = Object.prototype.toString.call(window.operamini)
    @                   === "[object OperaMini]";
    @ if( isOperaMini ){
    @   setTimeout("setAllHrefs();",%d(nDelay));
    @ }
  }else if( db_get_boolean("auto-hyperlink-ishuman",0) && g.isHuman ){
    /* Active hyperlinks after a delay */
    @ setTimeout("setAllHrefs();",%d(nDelay));
  }else if( db_get_boolean("auto-hyperlink-mouseover",0) ){
    /* Require mouse movement before starting the teim that will
    ** activating hyperlinks */
    @ document.getElementsByTagName("body")[0].onmousemove=function(){
    @   setTimeout("setAllHrefs();",%d(nDelay));
    @   this.onmousemove = null;
    @ }
  }else{
    /* Active hyperlinks after a delay */
    @ setTimeout("setAllHrefs();",%d(nDelay));
  }
  @ </script>
}

/*
** Add a new element to the submenu
*/
void style_submenu_element(
  const char *zLabel,
  const char *zLink,
  ...
){
  va_list ap;
  assert( nSubmenu < count(aSubmenu) );
  aSubmenu[nSubmenu].zLabel = zLabel;
  va_start(ap, zLink);
  aSubmenu[nSubmenu].zLink = vmprintf(zLink, ap);
  va_end(ap);
  nSubmenu++;
}
void style_submenu_entry(
  const char *zName,       /* Query parameter name */
  const char *zLabel,      /* Label before the entry box */
  int iSize,               /* Size of the entry box */
  int eVisible             /* Visible or disabled */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zName = zName;
  aSubmenuCtrl[nSubmenuCtrl].zLabel = zLabel;
  aSubmenuCtrl[nSubmenuCtrl].iSize = iSize;
  aSubmenuCtrl[nSubmenuCtrl].eVisible = eVisible;
  aSubmenuCtrl[nSubmenuCtrl].eType = FF_ENTRY;
  nSubmenuCtrl++;
}
void style_submenu_checkbox(
  const char *zName,       /* Query parameter name */
  const char *zLabel,      /* Label to display after the checkbox */
  int eVisible,            /* Visible or disabled */
  const char *zJS          /* Optional javascript to run on toggle */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zName = zName;
  aSubmenuCtrl[nSubmenuCtrl].zLabel = zLabel;
  aSubmenuCtrl[nSubmenuCtrl].eVisible = eVisible;
  aSubmenuCtrl[nSubmenuCtrl].zJS = zJS;
  aSubmenuCtrl[nSubmenuCtrl].eType = FF_CHECKBOX;
  nSubmenuCtrl++;
}
void style_submenu_jsbutton(
  const char *zLabel,      /* Label to display on the submenu */
  int eVisible,            /* Visible or disabled */
  const char *zJS          /* Javascript to run when clicked */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zLabel = zLabel;
  aSubmenuCtrl[nSubmenuCtrl].eVisible = eVisible;
  aSubmenuCtrl[nSubmenuCtrl].zJS = zJS;
  aSubmenuCtrl[nSubmenuCtrl].eType = FF_JSBUTTON;
  nSubmenuCtrl++;
}
void style_submenu_binary(
  const char *zName,       /* Query parameter name */
  const char *zTrue,       /* Label to show when parameter is true */
  const char *zFalse,      /* Label to show when the parameter is false */
  int eVisible             /* Visible or disabled */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zName = zName;
  aSubmenuCtrl[nSubmenuCtrl].zLabel = zTrue;
  aSubmenuCtrl[nSubmenuCtrl].zFalse = zFalse;
  aSubmenuCtrl[nSubmenuCtrl].eVisible = eVisible;
  aSubmenuCtrl[nSubmenuCtrl].eType = FF_BINARY;
  nSubmenuCtrl++;
}
void style_submenu_multichoice(
  const char *zName,           /* Query parameter name */
  int nChoice,                 /* Number of options */
  const char *const *azChoice, /* value/display pairs.  2*nChoice entries */
  int eVisible                 /* Visible or disabled */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zName = zName;
  aSubmenuCtrl[nSubmenuCtrl].iSize = nChoice;
  aSubmenuCtrl[nSubmenuCtrl].azChoice = azChoice;
  aSubmenuCtrl[nSubmenuCtrl].eVisible = eVisible;
  aSubmenuCtrl[nSubmenuCtrl].eType = FF_MULTI;
  nSubmenuCtrl++;
}
void style_submenu_sql(
  const char *zName,       /* Query parameter name */
  const char *zLabel,      /* Label on the control */
  const char *zFormat,     /* Format string for SQL command for choices */
  ...                      /* Arguments to the format string */
){
  Stmt q;
  int n = 0;
  int nAlloc = 0;
  char **az = 0;
  va_list ap;

  va_start(ap, zFormat);
  db_vprepare(&q, 0, zFormat, ap);
  va_end(ap);
  while( SQLITE_ROW==db_step(&q) ){
    if( n+2>=nAlloc ){
      nAlloc += nAlloc + 20;
      az = fossil_realloc(az, sizeof(char*)*nAlloc);
    }
    az[n++] = fossil_strdup(db_column_text(&q,0));
    az[n++] = fossil_strdup(db_column_text(&q,1));
  }
  db_finalize(&q);
  if( n>0 ){
    aSubmenuCtrl[nSubmenuCtrl].zName = zName;
    aSubmenuCtrl[nSubmenuCtrl].zLabel = zLabel;
    aSubmenuCtrl[nSubmenuCtrl].iSize = n/2;
    aSubmenuCtrl[nSubmenuCtrl].azChoice = (const char *const *)az;
    aSubmenuCtrl[nSubmenuCtrl].eVisible = STYLE_NORMAL;
    aSubmenuCtrl[nSubmenuCtrl].eType = FF_MULTI;
    nSubmenuCtrl++;
  }
}


/*
** Compare two submenu items for sorting purposes
*/
static int submenuCompare(const void *a, const void *b){
  const struct Submenu *A = (const struct Submenu*)a;
  const struct Submenu *B = (const struct Submenu*)b;
  return fossil_strcmp(A->zLabel, B->zLabel);
}

/* Use this for the $current_page variable if it is not NULL.  If it is
** NULL then use g.zPath.
*/
static char *local_zCurrentPage = 0;

/*
** Set the desired $current_page to something other than g.zPath
*/
void style_set_current_page(const char *zFormat, ...){
  fossil_free(local_zCurrentPage);
  if( zFormat==0 ){
    local_zCurrentPage = 0;
  }else{
    va_list ap;
    va_start(ap, zFormat);
    local_zCurrentPage = vmprintf(zFormat, ap);
    va_end(ap);
  }
}

/*
** Create a TH1 variable containing the URL for the specified config resource.
** The resulting variable name will be of the form $[zVarPrefix]_url.
*/
static void url_var(
  const char *zVarPrefix,
  const char *zConfigName,
  const char *zPageName
){
  char *zVarName = mprintf("%s_url", zVarPrefix);
  char *zUrl = mprintf("%R/%s?id=%x", zPageName,
                       skin_id(zConfigName));
  Th_Store(zVarName, zUrl);
  free(zUrl);
  free(zVarName);
}

/*
** Create a TH1 variable containing the URL for the specified config image.
** The resulting variable name will be of the form $[zImageName]_image_url.
*/
static void image_url_var(const char *zImageName){
  char *zVarPrefix = mprintf("%s_image", zImageName);
  char *zConfigName = mprintf("%s-image", zImageName);
  url_var(zVarPrefix, zConfigName, zImageName);
  free(zVarPrefix);
  free(zConfigName);
}

/*
** Default HTML page header text through <body>.  If the repository-specific
** header template lacks a <body> tag, then all of the following is
** prepended.
*/
static char zDfltHeader[] = 
@ <html>
@ <head>
@ <base href="$baseurl/$current_page" />
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$home/timeline.rss" />
@ <link rel="stylesheet" href="$stylesheet_url" type="text/css"
@       media="screen" />
@ </head>
@ <body>
;

/*
** Draw the header.
*/
void style_header(const char *zTitleFormat, ...){
  va_list ap;
  char *zTitle;
  const char *zHeader = skin_get("header");
  login_check_credentials();

  va_start(ap, zTitleFormat);
  zTitle = vmprintf(zTitleFormat, ap);
  va_end(ap);

  cgi_destination(CGI_HEADER);

  @ <!DOCTYPE html>

  if( g.thTrace ) Th_Trace("BEGIN_HEADER<br />\n", -1);

  /* Generate the header up through the main menu */
  Th_Store("project_name", db_get("project-name","Unnamed Fossil Project"));
  Th_Store("project_description", db_get("project-description",""));
  Th_Store("title", zTitle);
  Th_Store("baseurl", g.zBaseURL);
  Th_Store("secureurl", login_wants_https_redirect()? g.zHttpsURL: g.zBaseURL);
  Th_Store("home", g.zTop);
  Th_Store("index_page", db_get("index-page","/home"));
  if( local_zCurrentPage==0 ) style_set_current_page("%T", g.zPath);
  Th_Store("current_page", local_zCurrentPage);
  Th_Store("csrf_token", g.zCsrfToken);
  Th_Store("release_version", RELEASE_VERSION);
  Th_Store("manifest_version", MANIFEST_VERSION);
  Th_Store("manifest_date", MANIFEST_DATE);
  Th_Store("compiler_name", COMPILER_NAME);
  url_var("stylesheet", "css", "style.css");
  image_url_var("logo");
  image_url_var("background");
  if( !login_is_nobody() ){
    Th_Store("login", g.zLogin);
  }
  if( sqlite3_strlike("%<body>%", zHeader, 0)!=0 ){
    Th_Render(zDfltHeader);
  }
  if( g.thTrace ) Th_Trace("BEGIN_HEADER_SCRIPT<br />\n", -1);
  Th_Render(zHeader);
  if( g.thTrace ) Th_Trace("END_HEADER<br />\n", -1);
  Th_Unstore("title");   /* Avoid collisions with ticket field names */
  cgi_destination(CGI_BODY);
  g.cgiOutput = 1;
  headerHasBeenGenerated = 1;
  sideboxUsed = 0;

  /* Make the gebi(x) function available as an almost-alias for
  ** document.getElementById(x) (except that it throws an error
  ** if the element is not found).
  **
  ** Maintenance note: this function must of course be available
  ** before it is called. It "should" go in the HEAD so that client
  ** HEAD code can make use of it, but because the client can replace
  ** the HEAD, and some fossil pages rely on gebi(), we put it here.
  */
  @ <script>
  @ function gebi(x){
  @ if(x.substr(0,1)=='#') x = x.substr(1);
  @ var e = document.getElementById(x);
  @ if(!e) throw new Error('Expecting element with ID '+x);
  @ else return e;}
  @ </script>
}

#if INTERFACE
/* Allowed parameters for style_adunit() */
#define ADUNIT_OFF        0x0001       /* Do not allow ads on this page */
#define ADUNIT_RIGHT_OK   0x0002       /* Right-side vertical ads ok here */
#endif

/*
** Various page implementations can invoke this interface to let the
** style manager know what kinds of ads are appropriate for this page.
*/
void style_adunit_config(unsigned int mFlags){
  adUnitFlags = mFlags;
}

/*
** Return the text of an ad-unit, if one should be rendered.  Return
** NULL if no ad-unit is desired.
**
** The *pAdFlag value might be set to ADUNIT_RIGHT_OK if this is
** a right-hand vertical ad.
*/
static const char *style_adunit_text(unsigned int *pAdFlag){
  const char *zAd = 0;
  *pAdFlag = 0;
  if( adUnitFlags & ADUNIT_OFF ) return 0;  /* Disallow ads on this page */
  if( db_get_boolean("adunit-disable",0) ) return 0;
  if( g.perm.Admin && db_get_boolean("adunit-omit-if-admin",0) ){
    return 0;
  }
  if( !login_is_nobody()
   && fossil_strcmp(g.zLogin,"anonymous")!=0
   && db_get_boolean("adunit-omit-if-user",0)
  ){
    return 0;
  }
  if( (adUnitFlags & ADUNIT_RIGHT_OK)!=0
   && !fossil_all_whitespace(zAd = db_get("adunit-right", 0))
   && !cgi_body_contains("<table")
  ){
    *pAdFlag = ADUNIT_RIGHT_OK;
    return zAd;
  }else if( !fossil_all_whitespace(zAd = db_get("adunit",0)) ){
    return zAd;
  }
  return 0;
}

/*
** Draw the footer at the bottom of the page.
*/
void style_footer(void){
  const char *zFooter;
  const char *zAd = 0;
  unsigned int mAdFlags = 0;

  if( !headerHasBeenGenerated ) return;

  /* Go back and put the submenu at the top of the page.  We delay the
  ** creation of the submenu until the end so that we can add elements
  ** to the submenu while generating page text.
  */
  cgi_destination(CGI_HEADER);
  if( nSubmenu+nSubmenuCtrl>0 ){
    int i;
    if( nSubmenuCtrl ){
      cgi_printf("<form id='f01' method='GET' action='%R/%s'>", g.zPath);
    }
    @ <div class="submenu">
    if( nSubmenu>0 ){
      qsort(aSubmenu, nSubmenu, sizeof(aSubmenu[0]), submenuCompare);
      for(i=0; i<nSubmenu; i++){
        struct Submenu *p = &aSubmenu[i];
        if( p->zLink==0 ){
          @ <span class="label">%h(p->zLabel)</span>
        }else{
          @ <a class="label" href="%h(p->zLink)">%h(p->zLabel)</a>
        }
      }
    }
    for(i=0; i<nSubmenuCtrl; i++){
      const char *zQPN = aSubmenuCtrl[i].zName;
      const char *zDisabled = "";
      const char *zXtraClass = "";
      if( aSubmenuCtrl[i].eVisible & STYLE_DISABLED ){
        zDisabled = " disabled";
      }else if( zQPN ){
        cgi_tag_query_parameter(zQPN);
      }
      if( aSubmenuCtrl[i].eVisible & STYLE_CLUTTER ){
        zXtraClass = " clutter";
      }
      if( aSubmenuCtrl[i].eVisible & STYLE_BASIC ){
        zXtraClass = " anticlutter";
      }
      switch( aSubmenuCtrl[i].eType ){
        case FF_ENTRY:
          @ <span class='submenuctrl%s(zXtraClass)'>\
          @ &nbsp;%h(aSubmenuCtrl[i].zLabel)\
          @ <input type='text' name='%s(zQPN)' value='%h(PD(zQPN, ""))' \
          if( aSubmenuCtrl[i].iSize<0 ){
            @ size='%d(-aSubmenuCtrl[i].iSize)' \
          }else if( aSubmenuCtrl[i].iSize>0 ){
            @ size='%d(aSubmenuCtrl[i].iSize)' \
            @ maxlength='%d(aSubmenuCtrl[i].iSize)' \
          }
          @ onchange='gebi("f01").submit();'%s(zDisabled)></span>
          break;
        case FF_MULTI: {
          int j;
          const char *zVal = P(zQPN);
          if( zXtraClass[0] ){
            @ <span class='%s(zXtraClass+1)'>
          }
          if( aSubmenuCtrl[i].zLabel ){
            @ &nbsp;%h(aSubmenuCtrl[i].zLabel)\
          }
          @ <select class='submenuctrl' size='1' name='%s(zQPN)' \
          @ onchange='gebi("f01").submit();'%s(zDisabled)>
          for(j=0; j<aSubmenuCtrl[i].iSize*2; j+=2){
            const char *zQPV = aSubmenuCtrl[i].azChoice[j];
            @ <option value='%h(zQPV)'\
            if( fossil_strcmp(zVal, zQPV)==0 ){
              @  selected\
            }
            @ >%h(aSubmenuCtrl[i].azChoice[j+1])</option>
          }
          @ </select>
          if( zXtraClass[0] ){
            @ </span>
          }
          break;
        }
        case FF_BINARY: {
          int isTrue = PB(zQPN);
          @ <select class='submenuctrl%s(zXtraClass)' size='1' name='%s(zQPN)' \
          @ onchange='gebi("f01").submit();'%s(zDisabled)>
          @ <option value='1'\
          if( isTrue ){
            @  selected\
          }
          @ >%h(aSubmenuCtrl[i].zLabel)</option>
          @ <option value='0'\
          if( !isTrue ){
            @  selected\
          }
          @ >%h(aSubmenuCtrl[i].zFalse)</option>
          @ </select>
          break;
        }
        case FF_CHECKBOX: {
          @ <label class='submenuctrl submenuckbox%s(zXtraClass)'>\
          @ <input type='checkbox' name='%s(zQPN)' \
          if( PB(zQPN) ){
            @ checked \
          }
          if( aSubmenuCtrl[i].zJS ){
            @ onchange='%s(aSubmenuCtrl[i].zJS)'%s(zDisabled)>\
          }else{
            @ onchange='gebi("f01").submit();'%s(zDisabled)>\
          }
          @ %h(aSubmenuCtrl[i].zLabel)</label>
          break;
        }
        case FF_JSBUTTON: {
          @ <a class="label%s(zXtraClass)" \
          @  onclick='%s(aSubmenuCtrl[i].zJS)'%s(zDisabled)>\
          @ %s(aSubmenuCtrl[i].zLabel)</a>
          break;
        }
      }
    }
    @ </div>
    if( nSubmenuCtrl ){
      cgi_query_parameters_to_hidden();
      cgi_tag_query_parameter(0);
      @ </form>
    }
  }

  zAd = style_adunit_text(&mAdFlags);
  if( (mAdFlags & ADUNIT_RIGHT_OK)!=0  ){
    @ <div class="content adunit_right_container">
    @ <div class="adunit_right">
    cgi_append_content(zAd, -1);
    @ </div>
  }else{
    if( zAd ){
      @ <div class="adunit_banner">
      cgi_append_content(zAd, -1);
      @ </div>
    }
    @ <div class="content">
  }
  cgi_destination(CGI_BODY);

  if( sideboxUsed ){
    /* Put the footer at the bottom of the page.
    ** the additional clear/both is needed to extend the content
    ** part to the end of an optional sidebox.
    */
    @ <div class="endContent"></div>
  }
  @ </div>

  /* Set the href= field on hyperlinks.  Do this before the footer since
  ** the footer will be generating </html> */
  style_resolve_href();

  zFooter = skin_get("footer");
  if( g.thTrace ) Th_Trace("BEGIN_FOOTER<br />\n", -1);
  Th_Render(zFooter);
  if( g.thTrace ) Th_Trace("END_FOOTER<br />\n", -1);

  /* Render trace log if TH1 tracing is enabled. */
  if( g.thTrace ){
    cgi_append_content("<span class=\"thTrace\"><hr />\n", -1);
    cgi_append_content(blob_str(&g.thLog), blob_size(&g.thLog));
    cgi_append_content("</span>\n", -1);
  }

  /* Add document end mark if it was not in the footer */
  if( sqlite3_strlike("%</body>%", zFooter, 0)!=0 ){
    @ </body></html>
  }
}

/*
** Begin a side-box on the right-hand side of a page.  The title and
** the width of the box are given as arguments.  The width is usually
** a percentage of total screen width.
*/
void style_sidebox_begin(const char *zTitle, const char *zWidth){
  sideboxUsed = 1;
  @ <div class="sidebox" style="width:%s(zWidth)">
  @ <div class="sideboxTitle">%h(zTitle)</div>
}

/* End the side-box
*/
void style_sidebox_end(void){
  @ </div>
}

/*
** Insert the cssDefaultList[] table, generated from default_css.txt
** using the mkcss.c program.
*/
#include "default_css.h"

/*
** Append all of the default CSS to the CGI output.
*/
void cgi_append_default_css(void) {
  int i;

  cgi_printf("%s", builtin_text("skins/default/css.txt"));
  for( i=0; cssDefaultList[i].elementClass; i++ ){
    if( cssDefaultList[i].elementClass[0] ){
      cgi_printf("%s {\n%s\n}\n\n",
                 cssDefaultList[i].elementClass,
                 cssDefaultList[i].value
                );
    }
  }
}

/*
** Search string zCss for zSelector.
**
** Return true if found.  Return false if not found
*/
static int containsSelector(const char *zCss, const char *zSelector){
  const char *z;
  int n;
  int selectorLen = (int)strlen(zSelector);

  for(z=zCss; *z; z+=selectorLen){
    z = strstr(z, zSelector);
    if( z==0 ) return 0;
    if( z!=zCss ){
      for( n=-1; z+n!=zCss && fossil_isspace(z[n]); n--);
      if( z+n!=zCss && z[n]!=',' && z[n]!= '}' && z[n]!='/' ) continue;
    }
    for( n=selectorLen; z[n] && fossil_isspace(z[n]); n++ );
    if( z[n]==',' || z[n]=='{' || z[n]=='/' ) return 1;
  }
  return 0;
}

/*
** COMMAND: test-contains-selector
**
** Usage: %fossil test-contains-selector FILENAME SELECTOR
**
** Determine if the CSS stylesheet FILENAME contains SELECTOR.
*/
void contains_selector_cmd(void){
  int found;
  char *zSelector;
  Blob css;
  if( g.argc!=4 ) usage("FILENAME SELECTOR");
  blob_read_from_file(&css, g.argv[2]);
  zSelector = g.argv[3];
  found = containsSelector(blob_str(&css), zSelector);
  fossil_print("%s %s\n", zSelector, found ? "found" : "not found");
  blob_reset(&css);
}


/*
** WEBPAGE: style.css
**
** Return the style sheet.
*/
void page_style_css(void){
  Blob css;
  int i;
  int isInit = 0;

  cgi_set_content_type("text/css");
  blob_init(&css,skin_get("css"),-1);

  /* add special missing definitions */
  for(i=1; cssDefaultList[i].elementClass; i++){
    char *z = blob_str(&css);
    if( !containsSelector(z, cssDefaultList[i].elementClass) ){
      if( !isInit ){
        isInit = 1;
        blob_append(&css,
          "/*** All the follows is supplemental CSS automatically generated"
          " by Fossil ***/\n", -1);
      }
      blob_appendf(&css, "%s {\n%s}\n",
          cssDefaultList[i].elementClass,
          cssDefaultList[i].value);
    }
  }

  /* Process through TH1 in order to give an opportunity to substitute
  ** variables such as $baseurl.
  */
  Th_Store("baseurl", g.zBaseURL);
  Th_Store("secureurl", login_wants_https_redirect()? g.zHttpsURL: g.zBaseURL);
  Th_Store("home", g.zTop);
  image_url_var("logo");
  image_url_var("background");
  Th_Render(blob_str(&css));

  /* Tell CGI that the content returned by this page is considered cacheable */
  g.isConst = 1;
}

/*
** WEBPAGE: test_env
**
** Display CGI-variables and other aspects of the run-time
** environment, for debugging and trouble-shooting purposes.
*/
void page_test_env(void){
  char c;
  int i;
  int showAll;
  char zCap[30];
  static const char *const azCgiVars[] = {
    "COMSPEC", "DOCUMENT_ROOT", "GATEWAY_INTERFACE",
    "HTTP_ACCEPT", "HTTP_ACCEPT_CHARSET", "HTTP_ACCEPT_ENCODING",
    "HTTP_ACCEPT_LANGUAGE", "HTTP_AUTHENICATION",
    "HTTP_CONNECTION", "HTTP_HOST",
    "HTTP_USER_AGENT", "HTTP_REFERER", "PATH_INFO", "PATH_TRANSLATED",
    "QUERY_STRING", "REMOTE_ADDR", "REMOTE_PORT",
    "REMOTE_USER", "REQUEST_METHOD",
    "REQUEST_URI", "SCRIPT_FILENAME", "SCRIPT_NAME", "SERVER_PROTOCOL",
    "HOME", "FOSSIL_HOME", "USERNAME", "USER", "FOSSIL_USER",
    "SQLITE_TMPDIR", "TMPDIR",
    "TEMP", "TMP", "FOSSIL_VFS",
    "FOSSIL_FORCE_TICKET_MODERATION", "FOSSIL_FORCE_WIKI_MODERATION",
    "FOSSIL_TCL_PATH", "TH1_DELETE_INTERP", "TH1_ENABLE_DOCS",
    "TH1_ENABLE_HOOKS", "TH1_ENABLE_TCL", "REMOTE_HOST"
  };

  login_check_credentials();
  if( !g.perm.Admin && !g.perm.Setup && !db_get_boolean("test_env_enable",0) ){
    login_needed(0);
    return;
  }
  for(i=0; i<count(azCgiVars); i++) (void)P(azCgiVars[i]);
  style_header("Environment Test");
  showAll = PB("showall");
  style_submenu_checkbox("showall", "Cookies", 0, 0);
  style_submenu_element("Stats", "%R/stat");

#if !defined(_WIN32)
  @ uid=%d(getuid()), gid=%d(getgid())<br />
#endif
  @ g.zBaseURL = %h(g.zBaseURL)<br />
  @ g.zHttpsURL = %h(g.zHttpsURL)<br />
  @ g.zTop = %h(g.zTop)<br />
  @ g.zPath = %h(g.zPath)<br />
  for(i=0, c='a'; c<='z'; c++){
    if( login_has_capability(&c, 1, 0) ) zCap[i++] = c;
  }
  zCap[i] = 0;
  @ g.userUid = %d(g.userUid)<br />
  @ g.zLogin = %h(g.zLogin)<br />
  @ g.isHuman = %d(g.isHuman)<br />
  @ capabilities = %s(zCap)<br />
  for(i=0, c='a'; c<='z'; c++){
    if( login_has_capability(&c, 1, LOGIN_ANON)
         && !login_has_capability(&c, 1, 0) ) zCap[i++] = c;
  }
  zCap[i] = 0;
  if( i>0 ){
    @ anonymous-adds = %s(zCap)<br />
  }
  @ g.zRepositoryName = %h(g.zRepositoryName)<br />
  @ load_average() = %f(load_average())<br />
  @ <hr />
  P("HTTP_USER_AGENT");
  cgi_print_all(showAll);
  if( showAll && blob_size(&g.httpHeader)>0 ){
    @ <hr />
    @ <pre>
    @ %h(blob_str(&g.httpHeader))
    @ </pre>
  }
  if( g.perm.Setup ){
    const char *zRedir = P("redirect");
    if( zRedir ) cgi_redirect(zRedir);
  }
  style_footer();
  if( g.perm.Admin && P("err") ) fossil_fatal("%s", P("err"));
}

/*
** WEBPAGE: honeypot
** This page is a honeypot for spiders and bots.
*/
void honeypot_page(void){
  cgi_set_status(403, "Forbidden");
  @ <p>Please enable javascript or log in to see this content</p>
}
