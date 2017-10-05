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
  unsigned char isDisabled;    /* True if this control is grayed out */
  short int iSize;             /* Width for FF_ENTRY.  Count for FF_MULTI */
  const char *const *azChoice; /* value/display pairs for FF_MULTI */
  const char *zFalse;          /* FF_BINARY label when false */
  const char *zJS;             /* Javascript to run on toggle */
} aSubmenuCtrl[20];
static int nSubmenuCtrl = 0;
#define FF_ENTRY    1
#define FF_MULTI    2
#define FF_BINARY   3
#define FF_CHECKBOX 4

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
  int isDisabled           /* True if disabled */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zName = zName;
  aSubmenuCtrl[nSubmenuCtrl].zLabel = zLabel;
  aSubmenuCtrl[nSubmenuCtrl].iSize = iSize;
  aSubmenuCtrl[nSubmenuCtrl].isDisabled = isDisabled;
  aSubmenuCtrl[nSubmenuCtrl].eType = FF_ENTRY;
  nSubmenuCtrl++;
}
void style_submenu_checkbox(
  const char *zName,       /* Query parameter name */
  const char *zLabel,      /* Label to display after the checkbox */
  int isDisabled,          /* True if disabled */
  const char *zJS          /* Optional javascript to run on toggle */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zName = zName;
  aSubmenuCtrl[nSubmenuCtrl].zLabel = zLabel;
  aSubmenuCtrl[nSubmenuCtrl].isDisabled = isDisabled;
  aSubmenuCtrl[nSubmenuCtrl].zJS = zJS;
  aSubmenuCtrl[nSubmenuCtrl].eType = FF_CHECKBOX;
  nSubmenuCtrl++;
}
void style_submenu_binary(
  const char *zName,       /* Query parameter name */
  const char *zTrue,       /* Label to show when parameter is true */
  const char *zFalse,      /* Label to show when the parameter is false */
  int isDisabled           /* True if this control is disabled */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zName = zName;
  aSubmenuCtrl[nSubmenuCtrl].zLabel = zTrue;
  aSubmenuCtrl[nSubmenuCtrl].zFalse = zFalse;
  aSubmenuCtrl[nSubmenuCtrl].isDisabled = isDisabled;
  aSubmenuCtrl[nSubmenuCtrl].eType = FF_BINARY;
  nSubmenuCtrl++;
}
void style_submenu_multichoice(
  const char *zName,       /* Query parameter name */
  int nChoice,             /* Number of options */
  const char *const *azChoice,/* value/display pairs.  2*nChoice entries */
  int isDisabled           /* True if this control is disabled */
){
  assert( nSubmenuCtrl < count(aSubmenuCtrl) );
  aSubmenuCtrl[nSubmenuCtrl].zName = zName;
  aSubmenuCtrl[nSubmenuCtrl].iSize = nChoice;
  aSubmenuCtrl[nSubmenuCtrl].azChoice = azChoice;
  aSubmenuCtrl[nSubmenuCtrl].isDisabled = isDisabled;
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
    aSubmenuCtrl[nSubmenuCtrl].isDisabled = 0;
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
      const char *zDisabled = " disabled";
      if( !aSubmenuCtrl[i].isDisabled ){
        zDisabled = "";
        cgi_tag_query_parameter(zQPN);
      }
      switch( aSubmenuCtrl[i].eType ){
        case FF_ENTRY:
          @ <span class='submenuctrl'>\
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
          break;
        }
        case FF_BINARY: {
          int isTrue = PB(zQPN);
          @ <select class='submenuctrl' size='1' name='%s(zQPN)' \
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
          @ <label class='submenuctrl submenuckbox'>\
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


/* The following table contains bits of default CSS that must
** be included if they are not found in the application-defined
** CSS.
*/
const struct strctCssDefaults {
  const char *elementClass;  /* Name of element needed */
  const char *comment;       /* Comment text */
  const char *value;         /* CSS text */
} cssDefaultList[] = {
  { "div.sidebox",
    "The nomenclature sidebox for branches,..",
    @   float: right;
    @   background-color: white;
    @   border-width: medium;
    @   border-style: double;
    @   margin: 10px;
  },
  { "div.sideboxTitle",
    "The nomenclature title in sideboxes for branches,..",
    @   display: inline;
    @   font-weight: bold;
  },
  { "div.sideboxDescribed",
    "The defined element in sideboxes for branches,..",
    @   display: inline;
    @   font-weight: bold;
  },
  { "span.disabled",
    "The defined element in sideboxes for branches,..",
    @   color: red;
  },
  { "span.timelineDisabled",
    "The suppressed duplicates lines in timeline, ..",
    @   font-style: italic;
    @   font-size: small;
  },
  { "table.timelineTable",
    "the format for the timeline data table",
    @   border: 0;
    @   border-collapse: collapse;
  },
  { "td.timelineTableCell",
    "the format for the timeline data cells",
    @   vertical-align: top;
    @   text-align: left;
  },
  { "tr.timelineCurrent",
    "the format for the timeline data cell of the current checkout",
    @   padding: .1em .2em;
    @   border: 1px dashed #446979;
    @   box-shadow: 1px 1px 4px rgba(0, 0, 0, 0.5);
  },
  { "tr.timelineSelected",
    "The row in the timeline table that contains the entry of interest",
    @   padding: .1em .2em;
    @   border: 2px solid lightgray;
    @   background-color: #ffc;
    @   box-shadow: 4px 4px 2px rgba(0, 0, 0, 0.5);
  },
  { "tr.timelineSpacer",
    "An extra row inserted to give vertical space between two rows",
    @   height: 1ex;
  },
  { "span.timelineLeaf",
    "the format for the timeline leaf marks",
    @   font-weight: bold;
  },
  { "a.timelineHistLink",
    "the format for the timeline version links",
    @
  },
  { "span.timelineHistDsp",
    "the format for the timeline version display(no history permission!)",
    @   font-weight: bold;
  },
  { "td.timelineTime",
    "the format for the timeline time display",
    @   vertical-align: top;
    @   text-align: right;
    @   white-space: nowrap;
  },
  { "td.timelineGraph",
    "the format for the graph placeholder cells in timelines",
    @ width: 20px;
    @ text-align: left;
    @ vertical-align: top;
  },
  { ".tl-canvas",
    "timeline graph canvas",
    @   margin: 0 6px 0 10px;
  },
  { ".tl-rail",
    "maximum rail spacing",
    @   width: 18px;
  },
  { ".tl-mergeoffset",
    "maximum spacing between merge risers and primary child risers",
    @   width: 2px;
  },
  { ".tl-nodemark",
    "adjusts the vertical position of graph nodes",
    @   margin-top: 5px;
  },
  { ".tl-node",
    "commit node",
    @   width: 10px;
    @   height: 10px;
    @   border: 1px solid #000;
    @   background: #fff;
    @   cursor: pointer;
  },
  { ".tl-node.leaf:after",
    "leaf commit marker",
    @   content: '';
    @   position: absolute;
    @   top: 3px;
    @   left: 3px;
    @   width: 4px;
    @   height: 4px;
    @   background: #000;
  },
  { ".tl-node.sel:after",
    "selected commit node marker",
    @   content: '';
    @   position: absolute;
    @   top: 2px;
    @   left: 2px;
    @   width: 6px;
    @   height: 6px;
    @   background: red;
  },
  { ".tl-arrow",
    "arrow",
    @   width: 0;
    @   height: 0;
    @   transform: scale(.999);
    @   border: 0 solid transparent;
  },
  { ".tl-arrow.u",
    "up arrow",
    @   margin-top: -1px;
    @   border-width: 0 3px;
    @   border-bottom: 7px solid #000;
  },
  { ".tl-arrow.u.sm",
    "small up arrow",
    @   border-bottom: 5px solid #000;
  },
  { ".tl-line",
    "line",
    @   background: #000;
    @   width: 2px;
  },
  { ".tl-arrow.merge",
    "merge arrow",
    @   height: 1px;
    @   border-width: 2px 0;
  },
  { ".tl-arrow.merge.l",
    "left merge arrow",
    @   border-right: 3px solid #000;
  },
  { ".tl-arrow.merge.r",
    "right merge arrow",
    @   border-left: 3px solid #000;
  },
  { ".tl-line.merge",
    "merge line",
    @   width: 1px;
  },
  { ".tl-arrow.warp",
    "timewarp arrow",
    @   margin-left: 1px;
    @   border-width: 3px 0;
    @   border-left: 7px solid #600000;
  },
  { ".tl-line.warp",
    "timewarp line",
    @   background: #600000;
  },
  { "a.tagLink",
    "the format for the tag links",
    @
  },
  { "span.tagDsp",
    "the format for the tag display(no history permission!)",
    @   font-weight: bold;
  },
  { "span.wikiError",
    "the format for wiki errors",
    @   font-weight: bold;
    @   color: red;
  },
  { "span.infoTagCancelled",
    "the format for fixed/canceled tags,..",
    @   font-weight: bold;
    @   text-decoration: line-through;
  },
  { "span.infoTag",
    "the format for tags,..",
    @   font-weight: bold;
  },
  { "span.wikiTagCancelled",
    "the format for fixed/cancelled tags,.. on wiki pages",
    @   text-decoration: line-through;
  },
  { "table.browser",
    "format for the file display table",
    @ /* the format for wiki errors */
    @   width: 100%;
    @   border: 0;
  },
  { "td.browser",
    "format for cells in the file browser",
    @   width: 24%;
    @   vertical-align: top;
  },
  { ".filetree",
    "tree-view file browser",
    @   margin: 1em 0;
    @   line-height: 1.5;
  },
  {
    ".filetree > ul",
    "tree-view top-level list",
    @   display: inline-block;
  },
  { ".filetree ul",
    "tree-view lists",
    @   margin: 0;
    @   padding: 0;
    @   list-style: none;
  },
  { ".filetree ul.collapsed",
    "tree-view collapsed list",
    @   display: none;
  },
  { ".filetree ul ul",
    "tree-view lists below the root",
    @   position: relative;
    @   margin: 0 0 0 21px;
  },
  { ".filetree li",
    "tree-view lists items",
    @   position: relative;
    @   margin: 0;
    @   padding: 0;
  },
  { ".filetree li li:before",
    "tree-view node lines",
    @   content: '';
    @   position: absolute;
    @   top: -.8em;
    @   left: -14px;
    @   width: 14px;
    @   height: 1.5em;
    @   border-left: 2px solid #aaa;
    @   border-bottom: 2px solid #aaa;
  },
  { ".filetree li > ul:before",
    "tree-view directory lines",
    @   content: '';
    @   position: absolute;
    @   top: -1.5em;
    @   bottom: 0;
    @   left: -35px;
    @   border-left: 2px solid #aaa;
  },
  { ".filetree li.last > ul:before",
    "hide lines for last-child directories",
    @   display: none;
  },
  { ".filetree a",
    "tree-view links",
    "  position: relative;\n"
    "  z-index: 1;\n"
    "  display: table-cell;\n"
    "  min-height: 16px;\n"
    "  padding-left: 21px;\n"
    "  background-image: url(data:image/gif;base64,R0lGODlhEAAQAJEAAP"
    "\\/\\/\\/yEhIf\\/\\/\\/wAAACH5BAEHAAIALAAAAAAQABAAAAIvlIKpxqcfmg"
    "OUvoaqDSCxrEEfF14GqFXImJZsu73wepJzVMNxrtNTj3NATMKhpwAAOw==);\n"
    "  background-position: center left;\n"
    "  background-repeat: no-repeat;\n"
  },
  { "ul.browser",
    "list of files in the 'flat-view' file browser",
    @   list-style-type: none;
    @   padding: 10px;
    @   margin: 0px;
    @   white-space: nowrap;
  },
  { "ul.browser li.file",
    "List element in the 'flat-view' file browser for a file",
    "  background-image: url(data:image/gif;base64,R0lGODlhEAAQAJEAAP"
    "\\/\\/\\/yEhIf\\/\\/\\/wAAACH5BAEHAAIALAAAAAAQABAAAAIvlIKpxqcfm"
    "gOUvoaqDSCxrEEfF14GqFXImJZsu73wepJzVMNxrtNTj3NATMKhpwAAOw==);\n"
    "  background-repeat: no-repeat;\n"
    "  background-position: 0px center;\n"
    "  padding-left: 20px;\n"
    "  padding-top: 2px;\n"
  },
  { "ul.browser li.dir",
    "List element in the 'flat-view file browser for a directory",
    "  background-image: url(data:image/gif;base64,R0lGODlhEAAQAJEAAP/WVCIi"
    "Iv\\/\\/\\/wAAACH5BAEHAAIALAAAAAAQABAAAAInlI9pwa3XYniCgQtkrAFfLXkiFo1jaX"
    "po+jUs6b5Z/K4siDu5RPUFADs=);\n"
    "  background-repeat: no-repeat;\n"
    "  background-position: 0px center;\n"
    "  padding-left: 20px;\n"
    "  padding-top: 2px;\n"
  },
  { "div.filetreeline",
    "line of a file tree",
    @   display: table;
    @   width: 100%;
    @   white-space: nowrap;
  },
  { ".filetree .dir > div.filetreeline > a",
    "tree-view directory links",
    "  background-image: url(data:image/gif;base64,R0lGODlhEAAQAJEAAP/WVCIi"
    "Iv\\/\\/\\/wAAACH5BAEHAAIALAAAAAAQABAAAAInlI9pwa3XYniCgQtkrAFfLXkiFo1jaXp"
    "o+jUs6b5Z/K4siDu5RPUFADs=);\n"
  },
  { "div.filetreeage",
    "Last change floating display on the right",
    @  display: table-cell;
    @  padding-left: 3em;
    @  text-align: right;
  },
  { "div.filetreeline:hover",
    "Highlight the line of a file tree",
    @  background-color: #eee;
  },
  { "table.login_out",
    "table format for login/out label/input table",
    @   text-align: left;
    @   margin-right: 10px;
    @   margin-left: 10px;
    @   margin-top: 10px;
  },
  { "div.captcha",
    "captcha display options",
    @   text-align: center;
    @   padding: 1ex;
  },
  { "table.captcha",
    "format for the layout table, used for the captcha display",
    @   margin: auto;
    @   padding: 10px;
    @   border-width: 4px;
    @   border-style: double;
    @   border-color: black;
  },
  { "td.login_out_label",
    "format for the label cells in the login/out table",
    @   text-align: center;
  },
  { "span.loginError",
    "format for login error messages",
    @   color: red;
  },
  { "span.note",
    "format for leading text for notes",
    @   font-weight: bold;
  },
  { "span.textareaLabel",
    "format for textarea labels",
    @   font-weight: bold;
  },
  { "table.usetupLayoutTable",
    "format for the user setup layout table",
    @   outline-style: none;
    @   padding: 0;
    @   margin: 25px;
  },
  { "td.usetupColumnLayout",
    "format of the columns on the user setup list page",
    @   vertical-align: top
  },
  { "table.usetupUserList",
    "format for the user list table on the user setup page",
    @   outline-style: double;
    @   outline-width: 1px;
    @   padding: 10px;
  },
  { "th.usetupListUser",
    "format for table header user in user list on user setup page",
    @   text-align: right;
    @   padding-right: 20px;
  },
  { "th.usetupListCap",
    "format for table header capabilities in user list on user setup page",
    @   text-align: center;
    @   padding-right: 15px;
  },
  { "th.usetupListCon",
    "format for table header contact info in user list on user setup page",
    @   text-align: left;
  },
  { "td.usetupListUser",
    "format for table cell user in user list on user setup page",
    @   text-align: right;
    @   padding-right: 20px;
    @   white-space:nowrap;
  },
  { "td.usetupListCap",
    "format for table cell capabilities in user list on user setup page",
    @   text-align: center;
    @   padding-right: 15px;
  },
  { "td.usetupListCon",
    "format for table cell contact info in user list on user setup page",
    @   text-align: left
  },
  { "div.ueditCapBox",
    "layout definition for the capabilities box on the user edit detail page",
    @   float: left;
    @   margin-right: 20px;
    @   margin-bottom: 20px;
  },
  { "td.usetupEditLabel",
    "format of the label cells in the detailed user edit page",
    @   text-align: right;
    @   vertical-align: top;
    @   white-space: nowrap;
  },
  { "span.ueditInheritNobody",
    "color for capabilities, inherited by nobody",
    @   color: green;
    @   padding: .2em;
  },
  { "span.ueditInheritDeveloper",
    "color for capabilities, inherited by developer",
    @   color: red;
    @   padding: .2em;
  },
  { "span.ueditInheritReader",
    "color for capabilities, inherited by reader",
    @   color: black;
    @   padding: .2em;
  },
  { "span.ueditInheritAnonymous",
    "color for capabilities, inherited by anonymous",
    @   color: blue;
    @   padding: .2em;
  },
  { "span.capability",
    "format for capabilities, mentioned on the user edit page",
    @   font-weight: bold;
  },
  { "span.usertype",
    "format for different user types, mentioned on the user edit page",
    @   font-weight: bold;
  },
  { "span.usertype:before",
    "leading text for user types, mentioned on the user edit page",
    @   content:"'";
  },
  { "span.usertype:after",
    "trailing text for user types, mentioned on the user edit page",
    @   content:"'";
  },
  { "div.selectedText",
    "selected lines of text within a linenumbered artifact display",
    @   font-weight: bold;
    @   color: blue;
    @   background-color: #d5d5ff;
    @   border: 1px blue solid;
  },
  { "p.missingPriv",
    "format for missing privileges note on user setup page",
    @  color: blue;
  },
  { "span.wikiruleHead",
    "format for leading text in wikirules definitions",
    @   font-weight: bold;
  },
  { "td.tktDspLabel",
    "format for labels on ticket display page",
    @   text-align: right;
  },
  { "td.tktDspValue",
    "format for values on ticket display page",
    @   text-align: left;
    @   vertical-align: top;
    @   background-color: #d0d0d0;
  },
  { "span.tktError",
    "format for ticket error messages",
    @   color: red;
    @   font-weight: bold;
  },
  { "table.rpteditex",
    "format for example tables on the report edit page",
    @   float: right;
    @   margin: 0;
    @   padding: 0;
    @   width: 125px;
    @   text-align: center;
    @   border-collapse: collapse;
    @   border-spacing: 0;
  },
  { "table.report",
    "Ticket report table formatting",
    @   border-collapse:collapse;
    @   border: 1px solid #999;
    @   margin: 1em 0 1em 0;
    @   cursor: pointer;
  },
  { "td.rpteditex",
    "format for example table cells on the report edit page",
    @   border-width: thin;
    @   border-color: #000000;
    @   border-style: solid;
  },
  { "input.checkinUserColor",
    "format for user color input on check-in edit page",
    @ /* no special definitions, class defined, to enable color pickers, f.e.:
    @ **  add the color picker found at http:jscolor.com  as java script include
    @ **  to the header and configure the java script file with
    @ **   1. use as bindClass :checkinUserColor
    @ **   2. change the default hash adding behaviour to ON
    @ ** or change the class defition of element identified by id="clrcust"
    @ ** to a standard jscolor definition with java script in the footer. */
  },
  { "div.endContent",
    "format for end of content area, to be used to clear page flow.",
    @   clear: both;
  },
  { "p.generalError",
    "format for general errors",
    @   color: red;
  },
  { "p.tktsetupError",
    "format for tktsetup errors",
    @   color: red;
    @   font-weight: bold;
  },
  { "p.xfersetupError",
    "format for xfersetup errors",
    @   color: red;
    @   font-weight: bold;
  },
  { "p.thmainError",
    "format for th script errors",
    @   color: red;
    @   font-weight: bold;
  },
  { "span.thTrace",
    "format for th script trace messages",
    @   color: red;
  },
  { "p.reportError",
    "format for report configuration errors",
    @   color: red;
    @   font-weight: bold;
  },
  { "blockquote.reportError",
    "format for report configuration errors",
    @   color: red;
    @   font-weight: bold;
  },
  { "p.noMoreShun",
    "format for artifact lines, no longer shunned",
    @   color: blue;
  },
  { "p.shunned",
    "format for artifact lines beeing shunned",
    @   color: blue;
  },
  { "span.brokenlink",
    "a broken hyperlink",
    @   color: red;
  },
  { "ul.filelist",
    "List of files in a timeline",
    @   margin-top: 3px;
    @   line-height: 100%;
  },
  { "ul.filelist li",
    "List of files in a timeline",
    @   padding-top: 1px;
  },
  { "table.sbsdiffcols",
    "side-by-side diff display (column-based)",
    @   width: 90%;
    @   border-spacing: 0;
    @   font-size: xx-small;
  },
  { "table.sbsdiffcols td",
    "sbs diff table cell",
    @   padding: 0;
    @   vertical-align: top;
  },
  { "table.sbsdiffcols pre",
    "sbs diff pre block",
    @   margin: 0;
    @   padding: 0;
    @   border: 0;
    @   font-size: inherit;
    @   background: inherit;
    @   color: inherit;
  },
  { "div.difflncol",
    "diff line number column",
    @   padding-right: 1em;
    @   text-align: right;
    @   color: #a0a0a0;
  },
  { "div.difftxtcol",
    "diff text column",
    @   width: 45em;
    @   overflow-x: auto;
  },
  { "div.diffmkrcol",
    "diff marker column",
    @   padding: 0 1em;
  },
  { "span.diffchng",
    "changes in a diff",
    @   background-color: #c0c0ff;
  },
  { "span.diffadd",
    "added code in a diff",
    @   background-color: #c0ffc0;
  },
  { "span.diffrm",
    "deleted in a diff",
    @   background-color: #ffc8c8;
  },
  { "span.diffhr",
    "suppressed lines in a diff",
    @   display: inline-block;
    @   margin: .5em 0 1em;
    @   color: #0000ff;
  },
  { "span.diffln",
    "line numbers in a diff",
    @   color: #a0a0a0;
  },
  { "span.modpending",
    "Moderation Pending message on timeline",
    @   color: #b03800;
    @   font-style: italic;
  },
  { "pre.th1result",
    "format for th1 script results",
    @   white-space: pre-wrap;
    @   word-wrap: break-word;
  },
  { "pre.th1error",
    "format for th1 script errors",
    @   white-space: pre-wrap;
    @   word-wrap: break-word;
    @   color: red;
  },
  { "table.label-value th",
    "The label/value pairs on (for example) the ci page",
    @   vertical-align: top;
    @   text-align: right;
    @   padding: 0.2ex 2ex;
  },
  { ".statistics-report-graph-line",
    "for the /reports views",
    @   background-color: #446979;
  },
  { ".statistics-report-table-events th",
    "",
    @   padding: 0 1em 0 1em;
  },
  { ".statistics-report-table-events td",
    "",
    @   padding: 0.1em 1em 0.1em 1em;
  },
  { ".statistics-report-row-year",
    "",
    @   text-align: left;
  },
  { ".statistics-report-week-number-label",
    "for the /stats_report views",
    @ text-align: right;
    @ font-size: 0.8em;
  },
  { ".statistics-report-week-of-year-list",
    "for the /stats_report views",
    @ font-size: 0.8em;
  },
  { "tr.row0",
    "even table row color",
    @ /* use default */
  },
  { "tr.row1",
    "odd table row color",
    @ /* Use default */
  },
  { "#usetupEditCapability",
    "format for capabilities string, mentioned on the user edit page",
    @ font-weight: bold;
  },
  { "table.adminLogTable",
    "Class for the /admin_log table",
    @ text-align: left;
  },
  { ".adminLogTable .adminTime",
    "Class for the /admin_log table",
    @ text-align: left;
    @ vertical-align: top;
    @ white-space: nowrap;
  },
  { ".fileage table",
    "The fileage table",
    @ border-spacing: 0;
  },
  { ".fileage tr:hover",
    "Mouse-over effects for the file-age table",
    @ background-color: #eee;
  },
  { ".fileage td",
    "fileage table cells",
    @ vertical-align: top;
    @ text-align: left;
    @ border-top: 1px solid #ddd;
    @ padding-top: 3px;
  },
  { ".fileage td:first-child",
    "fileage first column (the age)",
    @ white-space: nowrap;
  },
  { ".fileage td:nth-child(2)",
    "fileage second column (the filename)",
    @ padding-left: 1em;
    @ padding-right: 1em;
  },
  { ".fileage td:nth-child(3)",
    "fileage third column (the check-in comment)",
    @ word-wrap: break-word;
    @ max-width: 50%;
  },
  { ".brlist table",  "The list of branches",
    @ border-spacing: 0;
  },
  { ".brlist table th",  "Branch list table headers",
    @ text-align: left;
    @ padding: 0px 1em 0.5ex 0px;
    @ vertical-align: bottom;
  },
  { ".brlist table td",  "Branch list table headers",
    @ padding: 0px 2em 0px 0px;
    @ white-space: nowrap;
  },
  { "th.sort:after",
    "General styles for sortable column marker",
    @ margin-left: .4em;
    @ cursor: pointer;
    @ text-shadow: 0 0 0 #000; /* Makes arrow darker */
  },
  { "th.sort.none:after",
    "None sort column marker",
    @ content: '\2666';
  },
  { "th.sort.asc:after",
    "Ascending sort column marker",
    @ content: '\2193';
  },
  { "th.sort.desc:after",
    "Descending sort column marker",
    @ content: '\2191';
  },
  { "span.snippet>mark",
    "Search markup",
    @ background-color: inherit;
    @ font-weight: bold;
  },
  { "div.searchForm",
    "Container for the search terms entry box",
    @ text-align: center;
  },
  { "p.searchEmpty",
    "Message explaining that there are no search results",
    @ font-style: italic;
  },
  { 0,
    0,
    0
  }
};

/*
** Append all of the default CSS to the CGI output.
*/
void cgi_append_default_css(void) {
  int i;

  cgi_printf("%s", builtin_text("skins/default/css.txt"));
  for( i=0; cssDefaultList[i].elementClass; i++ ){
    if( cssDefaultList[i].elementClass[0] ){
      cgi_printf("/* %s */\n%s {\n%s\n}\n\n",
                 cssDefaultList[i].comment,
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

  cgi_set_content_type("text/css");
  blob_init(&css,skin_get("css"),-1);

  /* add special missing definitions */
  for(i=1; cssDefaultList[i].elementClass; i++){
    char *z = blob_str(&css);
    if( !containsSelector(z, cssDefaultList[i].elementClass) ){
      blob_appendf(&css, "/* %s */\n%s {\n%s}\n",
          cssDefaultList[i].comment,
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
