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
  unsigned char eType;         /* FF_ENTRY, FF_MULTI, FF_CHECKBOX */
  unsigned char eVisible;      /* STYLE_NORMAL or STYLE_DISABLED */
  short int iSize;             /* Width for FF_ENTRY.  Count for FF_MULTI */
  const char *const *azChoice; /* value/display pairs for FF_MULTI */
  const char *zFalse;          /* FF_BINARY label when false */
  const char *zJS;             /* Javascript to run on toggle */
} aSubmenuCtrl[20];
static int nSubmenuCtrl = 0;
#define FF_ENTRY    1          /* Text entry box */
#define FF_MULTI    2          /* Combobox.  Multiple choices. */
#define FF_BINARY   3          /* Control for binary query parameter */
#define FF_CHECKBOX 4          /* Check-box */

#if INTERFACE
#define STYLE_NORMAL   0       /* Normal display of control */
#define STYLE_DISABLED 1       /* Control is disabled */
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
** Flags for various javascript files needed prior to </body>
*/
static int needHrefJs = 0;      /* href.js */
static int needSortJs = 0;      /* sorttable.js */
static int needGraphJs = 0;     /* graph.js */
static int needCopyBtnJs = 0;   /* copybtn.js */

/*
** Extra JS added to the end of the file.
*/
static Blob blobOnLoad = BLOB_INITIALIZER;

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
** There are three versions of this routine:
**
**    (1)   href() does a plain hyperlink
**    (2)   xhref() adds extra attribute text
**    (3)   chref() adds a class name
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
    char *zHUrl;
    if( zExtra ){
      zHUrl = mprintf("<a %s href=\"%h\">", zExtra, zUrl);
    }else{
      zHUrl = mprintf("<a href=\"%h\">", zUrl);
    }
    fossil_free(zUrl);
    return zHUrl;
  }
  needHrefJs = 1;
  if( zExtra==0 ){
    return mprintf("<a data-href='%z' href='%R/honeypot'>", zUrl);
  }else{
    return mprintf("<a %s data-href='%z' href='%R/honeypot'>",
                   zExtra, zUrl);
  }
}
char *chref(const char *zExtra, const char *zFormat, ...){
  char *zUrl;
  va_list ap;
  va_start(ap, zFormat);
  zUrl = vmprintf(zFormat, ap);
  va_end(ap);
  if( g.perm.Hyperlink && !g.javascriptHyperlink ){
    char *zHUrl = mprintf("<a class=\"%s\" href=\"%h\">", zExtra, zUrl);
    fossil_free(zUrl);
    return zHUrl;
  }
  needHrefJs = 1;
  return mprintf("<a class='%s' data-href='%z' href='%R/honeypot'>",
                 zExtra, zUrl);
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
  needHrefJs = 1;
  return mprintf("<a data-href='%s' href='%R/honeypot'>",
                  zUrl);
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
    needHrefJs = 1;
    @ <form method="POST" data-action='%s(zLink)' action='%R/login' \
    @ %s(zOtherArgs)>
  }
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
** Output TEXT with a click-to-copy button next to it. Loads the copybtn.js
** Javascript module, and generates HTML elements with the following IDs:
**
**    TARGETID:       The <span> wrapper around TEXT.
**    copy-TARGETID:  The <span> for the copy button.
**
** If the FLIPPED argument is non-zero, the copy button is displayed after TEXT.
**
** The COPYLENGTH argument defines the length of the substring of TEXT copied to
** clipboard:
**
**    <= 0:   No limit (default if the argument is omitted).
**    >= 3:   Truncate TEXT after COPYLENGTH (single-byte) characters.
**       1:   Use the "hash-digits" setting as the limit.
**       2:   Use the length appropriate for URLs as the limit (defined at
**            compile-time by FOSSIL_HASH_DIGITS_URL, defaults to 16).
*/
char *style_copy_button(
  int bOutputCGI,         /* Don't return result, but send to cgi_printf(). */
  const char *zTargetId,  /* The TARGETID argument. */
  int bFlipped,           /* The FLIPPED argument. */
  int cchLength,          /* The COPYLENGTH argument. */
  const char *zTextFmt,   /* Formatting of the TEXT argument (htmlized). */
  ...                     /* Formatting parameters of the TEXT argument. */
){
  va_list ap;
  char *zText;
  char *zResult = 0;
  va_start(ap,zTextFmt);
  zText = vmprintf(zTextFmt/*works-like:?*/,ap);
  va_end(ap);
  if( cchLength==1 ) cchLength = hash_digits(0);
  else if( cchLength==2 ) cchLength = hash_digits(1);
  if( !bFlipped ){
    const char *zBtnFmt =
      "<span class=\"nobr\">"
      "<span "
      "class=\"copy-button\" "
      "id=\"copy-%h\" "
      "data-copytarget=\"%h\" "
      "data-copylength=\"%d\">"
      "</span>"
      "<span id=\"%h\">"
      "%s"
      "</span>"
      "</span>";
    if( bOutputCGI ){
      cgi_printf(
                  zBtnFmt/*works-like:"%h%h%d%h%s"*/,
                  zTargetId,zTargetId,cchLength,zTargetId,zText);
    }else{
      zResult = mprintf(
                  zBtnFmt/*works-like:"%h%h%d%h%s"*/,
                  zTargetId,zTargetId,cchLength,zTargetId,zText);
    }
  }else{
    const char *zBtnFmt =
      "<span class=\"nobr\">"
      "<span id=\"%h\">"
      "%s"
      "</span>"
      "<span "
      "class=\"copy-button copy-button-flipped\" "
      "id=\"copy-%h\" "
      "data-copytarget=\"%h\" "
      "data-copylength=\"%d\">"
      "</span>"
      "</span>";
    if( bOutputCGI ){
      cgi_printf(
                  zBtnFmt/*works-like:"%h%s%h%h%d"*/,
                  zTargetId,zText,zTargetId,zTargetId,cchLength);
    }else{
      zResult = mprintf(
                  zBtnFmt/*works-like:"%h%s%h%h%d"*/,
                  zTargetId,zText,zTargetId,zTargetId,cchLength);
    }
  }
  free(zText);
  style_copybutton_control();
  return zResult;
}

/*
** Return a random nonce that is stored in static space.  For a particular
** run, the same nonce is always returned.
*/
char *style_nonce(void){
  static char zNonce[52];
  if( zNonce[0]==0 ){
    unsigned char zSeed[24];
    sqlite3_randomness(24, zSeed);
    encode16(zSeed,(unsigned char*)zNonce,24);
  }
  return zNonce;
}

/*
** Return the default Content Security Policy (CSP) string.
** If the toHeader argument is true, then also add the
** CSP to the HTTP reply header.
**
** The CSP comes from the "default-csp" setting if it exists and
** is non-empty.  If that setting is an empty string, then the following
** default is used instead:
**
**     default-src 'self' data:;
**     script-src 'self' 'nonce-$nonce';
**     style-src 'self' 'unsafe-inline';
**
** The text '$nonce' is replaced by style_nonce() if and whereever it
** occurs in the input string.
**
** The string returned is obtained from fossil_malloc() and
** should be released by the caller.
*/
char *style_csp(int toHeader){
  static const char zBackupCSP[] = 
   "default-src 'self' data:; "
   "script-src 'self' 'nonce-$nonce'; "
   "style-src 'self' 'unsafe-inline'";
  const char *zFormat = db_get("default-csp","");
  Blob csp;
  char *zNonce;
  char *zCsp;
  if( zFormat[0]==0 ){
    zFormat = zBackupCSP;
  }
  blob_init(&csp, 0, 0);
  while( zFormat[0] && (zNonce = strstr(zFormat,"$nonce"))!=0 ){
    blob_append(&csp, zFormat, (int)(zNonce - zFormat));
    blob_append(&csp, style_nonce(), -1);
    zFormat = zNonce + 6;
  }
  blob_append(&csp, zFormat, -1);
  zCsp = blob_str(&csp);
  if( toHeader ){
    cgi_printf_header("Content-Security-Policy: %s\r\n", zCsp);
  }
  return zCsp;
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
@ <meta http-equiv="Content-Security-Policy" content="$default_csp" />
@ <meta name="viewport" content="width=device-width, initial-scale=1.0">
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed" \
@  href="$home/timeline.rss" />
@ <link rel="stylesheet" href="$stylesheet_url" type="text/css" />
@ </head>
@ <body>
;

/*
** Initialize all the default TH1 variables
*/
static void style_init_th1_vars(const char *zTitle){
  const char *zNonce = style_nonce();
  char *zDfltCsp;

  zDfltCsp = style_csp(1);
  /*
  ** Do not overwrite the TH1 variable "default_csp" if it exists, as this
  ** allows it to be properly overridden via the TH1 setup script (i.e. it
  ** is evaluated before the header is rendered).
  */
  Th_MaybeStore("default_csp", zDfltCsp);
  fossil_free(zDfltCsp);
  Th_Store("nonce", zNonce);
  Th_Store("project_name", db_get("project-name","Unnamed Fossil Project"));
  Th_Store("project_description", db_get("project-description",""));
  if( zTitle ) Th_Store("title", zTitle);
  Th_Store("baseurl", g.zBaseURL);
  Th_Store("secureurl", fossil_wants_https(1)? g.zHttpsURL: g.zBaseURL);
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
  style_init_th1_vars(zTitle);
  if( sqlite3_strlike("%<body%", zHeader, 0)!=0 ){
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
  if( g.perm.Debug && P("showqp") ){
    @ <div class="debug">
    cgi_print_all(0, 0);
    @ </div>
  }
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
** Indicate that the table-sorting javascript is needed.
*/
void style_table_sorter(void){
  needSortJs = 1;
}

/*
** Indicate that the timeline graph javascript is needed.
*/
void style_graph_generator(void){
  needGraphJs = 1;
}

/*
** Indicate that the copy button javascript is needed.
*/
void style_copybutton_control(void){
  needCopyBtnJs = 1;
}

/*
** Generate code to load a single javascript file
*/
void style_load_one_js_file(const char *zFile){
  @ <script src='%R/builtin/%s(zFile)?id=%S(MANIFEST_UUID)'></script>
}

/*
** All extra JS files to load.
*/
static const char *azJsToLoad[4];
static int nJsToLoad = 0;

/*
** Register a new JS file to load at the end of the document.
*/
void style_load_js(const char *zName){
  int i;
  for(i=0; i<nJsToLoad; i++){
    if( fossil_strcmp(zName, azJsToLoad[i])==0 ) return;
  }
  if( nJsToLoad>=sizeof(azJsToLoad)/sizeof(azJsToLoad[0]) ){
    fossil_panic("too many JS files");
  }
  azJsToLoad[nJsToLoad++] = zName;
}

/*
** Generate code to load all required javascript files.
*/
static void style_load_all_js_files(void){
  int i;
  if( needHrefJs ){
    int nDelay = db_get_int("auto-hyperlink-delay",0);
    int bMouseover = db_get_boolean("auto-hyperlink-mouseover",0);
    @ <script id='href-data' type='application/json'>\
    @ {"delay":%d(nDelay),"mouseover":%d(bMouseover)}</script>
  }
  @ <script nonce="%h(style_nonce())">
  @ function debugMsg(msg){
  @ var n = document.getElementById("debugMsg");
  @ if(n){n.textContent=msg;}
  @ }
  if( needHrefJs ){
    cgi_append_content(builtin_text("href.js"),-1);
  }
  if( needSortJs ){
    cgi_append_content(builtin_text("sorttable.js"),-1);
  }
  if( needGraphJs ){
    cgi_append_content(builtin_text("graph.js"),-1);
  }
  if( needCopyBtnJs ){
    cgi_append_content(builtin_text("copybtn.js"),-1);
  }
  for(i=0; i<nJsToLoad; i++){
    cgi_append_content(builtin_text(azJsToLoad[i]),-1);
  }
  if( blob_size(&blobOnLoad)>0 ){
    @ window.onload = function(){
    cgi_append_content(blob_buffer(&blobOnLoad), blob_size(&blobOnLoad));
    cgi_append_content("\n}\n", -1);
  }
  @ </script>
}

/*
** Extra JS to run after all content is loaded.
*/
void style_js_onload(const char *zFormat, ...){
  va_list ap;
  va_start(ap, zFormat);
  blob_vappendf(&blobOnLoad, zFormat, ap);
  va_end(ap);
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
      @ <form id='f01' method='GET' action='%R/%s(g.zPath)'>
      @ <input type='hidden' name='udc' value='1'>
      cgi_tag_query_parameter("udc");
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
          @ id='submenuctrl-%d(i)'%s(zDisabled)></span>
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
          @ id='submenuctrl-%d(i)'%s(zDisabled)>
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
          @ <select class='submenuctrl%s(zXtraClass)' size='1' \
          @ name='%s(zQPN)' id='submenuctrl-%d(i)'%s(zDisabled)>
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
          @ <input type='checkbox' name='%s(zQPN)' id='submenuctrl-%d(i)' \
          if( PB(zQPN) ){
            @ checked \
          }
          if( aSubmenuCtrl[i].zJS ){
            @ data-ctrl='%s(aSubmenuCtrl[i].zJS)'%s(zDisabled)>\
          }else{
            @ %s(zDisabled)>\
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
      style_load_one_js_file("menu.js");
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
    @ <div class="content"><span id="debugMsg"></span>
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



  zFooter = skin_get("footer");
  if( sqlite3_strlike("%</body>%", zFooter, 0)==0 ){
    style_load_all_js_files();
  }
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
    style_load_all_js_files();
    @ </body>
    @ </html>
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
  blob_read_from_file(&css, g.argv[2], ExtFILE);
  zSelector = g.argv[3];
  found = containsSelector(blob_str(&css), zSelector);
  fossil_print("%s %s\n", zSelector, found ? "found" : "not found");
  blob_reset(&css);
}

/*
** WEBPAGE: script.js
**
** Return the "Javascript" content for the current skin (if there is any)
*/
void page_script_js(void){
  const char *zScript = skin_get("js");
  if( P("test") ){
    /* Render the script as plain-text for testing purposes, if the "test"
    ** query parameter is present */
    cgi_set_content_type("text/plain");
  }else{
    /* Default behavior is to return javascript */
    cgi_set_content_type("application/javascript");
  }
  style_init_th1_vars(0);
  Th_Render(zScript?zScript:"");
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
          "\n/***********************************************************\n"
          "** All CSS above is supplied by the repository \"skin\".\n"
          "** That which follows is generated automatically by Fossil\n"
          "** to fill in needed selectors that are missing from the\n"
          "** \"skin\" CSS.\n"
          "***********************************************************/\n",
          -1);
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
  Th_Store("secureurl", fossil_wants_https(1)? g.zHttpsURL: g.zBaseURL);
  Th_Store("home", g.zTop);
  image_url_var("logo");
  image_url_var("background");
  Th_Render(blob_str(&css));

  /* Tell CGI that the content returned by this page is considered cacheable */
  g.isConst = 1;
}

/*
** WEBPAGE: builtin
** URL:  builtin/FILENAME
**
** Return the built-in text given by FILENAME.  This is used internally 
** by many Fossil web pages to load built-in javascript files.
**
** If the id= query parameter is present, then Fossil assumes that the
** result is immutable and sets a very large cache retention time (1 year).
*/
void page_builtin_text(void){
  Blob out;
  const char *zName = P("name");
  const char *zTxt = 0;
  const char *zId = P("id");
  int nId;
  if( zName ) zTxt = builtin_text(zName);
  if( zTxt==0 ){
    cgi_set_status(404, "Not Found");
    @ File "%h(zName)" not found
    return;
  }
  if( sqlite3_strglob("*.js", zName)==0 ){
    cgi_set_content_type("application/javascript");
  }else{
    cgi_set_content_type("text/plain");
  }
  if( zId && (nId = (int)strlen(zId))>=8 && strncmp(zId,MANIFEST_UUID,nId)==0 ){
    g.isConst = 1;
  }else{
    etag_check(0,0);
  }
  blob_init(&out, zTxt, -1);
  cgi_set_content(&out);
}

/*
** All possible capabilities
*/
static const char allCap[] = 
  "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKL";

/*
** Compute the current login capabilities
*/
static char *find_capabilities(char *zCap){
  int i, j;
  char c;
  for(i=j=0; (c = allCap[j])!=0; j++){
    if( login_has_capability(&c, 1, 0) ) zCap[i++] = c;
  }
  zCap[i] = 0;
  return zCap;
}

/*
** Compute the current login capabilities that were
** contributed by Anonymous
*/
static char *find_anon_capabilities(char *zCap){
  int i, j;
  char c;
  for(i=j=0; (c = allCap[j])!=0; j++){
    if( login_has_capability(&c, 1, LOGIN_ANON)
      && !login_has_capability(&c, 1, 0) ) zCap[i++] = c;
  }
  zCap[i] = 0;
  return zCap;
}

/*
** WEBPAGE: test_env
**
** Display CGI-variables and other aspects of the run-time
** environment, for debugging and trouble-shooting purposes.
*/
void page_test_env(void){
  webpage_error("");
}

/*
** WEBPAGE: honeypot
** This page is a honeypot for spiders and bots.
*/
void honeypot_page(void){
  cgi_set_status(403, "Forbidden");
  @ <p>Please enable javascript or log in to see this content</p>
}

/*
** Webpages that encounter an error due to missing or incorrect
** query parameters can jump to this routine to render an error
** message screen.
**
** For administators, or if the test_env_enable setting is true, then
** details of the request environment are displayed.  Otherwise, just
** the error message is shown.
**
** If zFormat is an empty string, then this is the /test_env page.
*/
void webpage_error(const char *zFormat, ...){
  int showAll;
  char *zErr = 0;
  int isAuth = 0;
  char zCap[100];

  login_check_credentials();
  if( g.perm.Admin || g.perm.Setup  || db_get_boolean("test_env_enable",0) ){
    isAuth = 1;
  }
  cgi_load_environment();
  if( zFormat[0] ){
    va_list ap;
    va_start(ap, zFormat);
    zErr = vmprintf(zFormat, ap);
    va_end(ap);
    style_header("Bad Request");
    @ <h1>/%h(g.zPath): %h(zErr)</h1>
    showAll = 0;
    cgi_set_status(500, "Bad Request");
  }else if( !isAuth ){
    login_needed(0);
    return;
  }else{
    style_header("Environment Test");
    showAll = PB("showall");
    style_submenu_checkbox("showall", "Cookies", 0, 0);
    style_submenu_element("Stats", "%R/stat");
  }

  if( isAuth ){
  #if !defined(_WIN32)
    @ uid=%d(getuid()), gid=%d(getgid())<br />
  #endif
    @ g.zBaseURL = %h(g.zBaseURL)<br />
    @ g.zHttpsURL = %h(g.zHttpsURL)<br />
    @ g.zTop = %h(g.zTop)<br />
    @ g.zPath = %h(g.zPath)<br />
    @ g.userUid = %d(g.userUid)<br />
    @ g.zLogin = %h(g.zLogin)<br />
    @ g.isHuman = %d(g.isHuman)<br />
    if( g.nRequest ){
      @ g.nRequest = %d(g.nRequest)<br />
    }
    if( g.nPendingRequest>1 ){
      @ g.nPendingRequest = %d(g.nPendingRequest)<br />
    }
    @ capabilities = %s(find_capabilities(zCap))<br />
    if( zCap[0] ){
      @ anonymous-adds = %s(find_anon_capabilities(zCap))<br />
    }
    @ g.zRepositoryName = %h(g.zRepositoryName)<br />
    @ load_average() = %f(load_average())<br />
    @ cgi_csrf_safe(0) = %d(cgi_csrf_safe(0))<br />
    @ <hr />
    P("HTTP_USER_AGENT");
    cgi_print_all(showAll, 0);
    if( showAll && blob_size(&g.httpHeader)>0 ){
      @ <hr />
      @ <pre>
      @ %h(blob_str(&g.httpHeader))
      @ </pre>
    }
  }
  style_footer();
  if( zErr ){
    cgi_reply();
    fossil_exit(1);
  }
}

/*
** Generate a Not Yet Implemented error page.
*/
void webpage_not_yet_implemented(void){
  webpage_error("Not yet implemented");
}

/*
** Generate a webpage for a webpage_assert().
*/
void webpage_assert_page(const char *zFile, int iLine, const char *zExpr){
  fossil_warning("assertion fault at %s:%d - %s", zFile, iLine, zExpr);
  cgi_reset_content();
  webpage_error("assertion fault at %s:%d - %s", zFile, iLine, zExpr);
}

#if INTERFACE
# define webpage_assert(T) if(!(T)){webpage_assert_page(__FILE__,__LINE__,#T);}
#endif
