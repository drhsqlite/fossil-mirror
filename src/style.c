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
** prior to calling style_finish_page().  The style_finish_page() routine
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
** Submenu disable flag
*/
static int submenuEnable = 1;

/*
** Disable content-security-policy.
** Warning:  Do not disable the CSP without careful consideration!
*/
static int disableCSP = 0;

/*
** Flags for various javascript files needed prior to </body>
*/
static int needHrefJs = 0;      /* href.js */

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
  if( !g.perm.Hyperlink ) return fossil_strdup("");
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
  if( !g.perm.Hyperlink ) return fossil_strdup("");
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
  if( !g.perm.Hyperlink ) return fossil_strdup("");
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
** Disable or enable the submenu
*/
void style_submenu_enable(int onOff){
  submenuEnable = onOff;
}


/*
** Compare two submenu items for sorting purposes
*/
static int submenuCompare(const void *a, const void *b){
  const struct Submenu *A = (const struct Submenu*)a;
  const struct Submenu *B = (const struct Submenu*)b;
  return fossil_strcmp(A->zLabel, B->zLabel);
}

/* Use this for the $current_page variable if it is not NULL.  If it
** is NULL then use g.zPath.
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
** Create a TH1 variable containing the URL for the specified config
** resource. The resulting variable name will be of the form
** $[zVarPrefix]_url.
*/
static void url_var(
  const char *zVarPrefix,
  const char *zConfigName,
  const char *zPageName
){
  char *zVarName = mprintf("%s_url", zVarPrefix);
  char *zUrl = 0;              /* stylesheet URL */
  int hasBuiltin = 0;          /* true for built-in page-specific CSS */

  if(0==strcmp("css",zConfigName)){
    /* Account for page-specific CSS, appending a /{{g.zPath}} to the
    ** url only if we have a corresponding built-in page-specific CSS
    ** file. Do not append it to all pages because we would
    ** effectively cache-bust all pages which do not have
    ** page-specific CSS. */
    char * zBuiltin = mprintf("style.%s.css", g.zPath);
    hasBuiltin = builtin_file(zBuiltin,0)!=0;
    fossil_free(zBuiltin);
  }
  zUrl = mprintf("%R/%s%s%s?id=%x", zPageName,
                 hasBuiltin ? "/" : "", hasBuiltin ? g.zPath : "",
                 skin_id(zConfigName));
  Th_Store(zVarName, zUrl);
  fossil_free(zUrl);
  fossil_free(zVarName);
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
  builtin_request_js("copybtn.js");
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
  const char *zFormat;
  Blob csp;
  char *zNonce;
  char *zCsp;
  int i;
  if( disableCSP ) return fossil_strdup("");
  zFormat = db_get("default-csp","");
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
  /* No whitespace other than actual space characters allowed in the CSP
  ** string.  See https://fossil-scm.org/forum/forumpost/d29e3af43c */
  for(i=0; zCsp[i]; i++){ if( fossil_isspace(zCsp[i]) ) zCsp[i] = ' '; }
  if( toHeader ){
    cgi_printf_header("Content-Security-Policy: %s\r\n", zCsp);
  }
  return zCsp;
}

/*
** Disable content security policy for the current page.
** WARNING:  Do not do this lightly!
**
** This routine must be called before the CSP is sued by 
** style_header().
*/
void style_disable_csp(void){
  disableCSP = 1;
}

/*
** Default HTML page header text through <body>.  If the repository-specific
** header template lacks a <body> tag, then all of the following is
** prepended.
*/
static const char zDfltHeader[] = 
@ <html>
@ <head>
@ <base href="$baseurl/$current_page" />
@ <meta charset="UTF-8">
@ <meta http-equiv="Content-Security-Policy" content="$default_csp" />
@ <meta name="viewport" content="width=device-width, initial-scale=1.0">
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed" \
@  href="$home/timeline.rss" />
@ <link rel="stylesheet" href="$stylesheet_url" type="text/css" />
@ </head>
@ <body class="$current_feature">
;

/*
** Returns the default page header.
*/
const char *get_default_header(){
  return zDfltHeader;
}

/*
** The default TCL list that defines the main menu.
*/
static const char zDfltMainMenu[] = 
@ Home      /home        *              {}
@ Timeline  /timeline    {o r j}        {}
@ Files     /dir?ci=tip  oh             desktoponly
@ Branches  /brlist      o              wideonly
@ Tags      /taglist     o              wideonly
@ Forum     /forum       {@2 3 4 5 6}   wideonly
@ Chat      /chat        C              wideonly
@ Tickets   /ticket      r              wideonly
@ Wiki      /wiki        j              wideonly
@ Setup     /setup       s              {}
;

/*
** Return the default menu
*/
const char *style_default_mainmenu(void){
  return zDfltMainMenu;
}

/*
** Given a URL path, extract the first element as a "feature" name,
** used as the <body class="FEATURE"> value by default, though
** later-running code may override this, typically to group multiple
** Fossil UI URLs into a single "feature" so you can have per-feature
** CSS rules.
**
** For example, "body.forum div.markdown blockquote" targets only
** block quotes made in forum posts, leaving other Markdown quotes
** alone.  Because feature class "forum" groups /forummain, /forumpost,
** and /forume2, it works across all renderings of Markdown to HTML
** within the Fossil forum feature.
*/
static const char* feature_from_page_path(const char *zPath){
  const char* zSlash = strchr(zPath, '/');
  if (zSlash) {
    return fossil_strndup(zPath, zSlash - zPath);
  } else {
    return zPath;
  }
}

/*
** Override the value of the TH1 variable current_feature, its default
** set by feature_from_page_path().  We do not call this from
** style_init_th1_vars() because that uses Th_MaybeStore() instead to
** allow webpage implementations to call this before style_header()
** to override that "maybe" default with something better.
*/
void style_set_current_feature(const char* zFeature){
  Th_Store("current_feature", zFeature);
}

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
  Th_Store("mainmenu", db_get("mainmenu", style_default_mainmenu()));
  url_var("stylesheet", "css", "style.css");
  image_url_var("logo");
  image_url_var("background");
  if( !login_is_nobody() ){
    Th_Store("login", g.zLogin);
  }
  Th_MaybeStore("current_feature", feature_from_page_path(local_zCurrentPage) );
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
  builtin_request_js("sorttable.js");
}

/*
** Generate code to load all required javascript files.
*/
static void style_load_all_js_files(void){
  if( needHrefJs && g.perm.Hyperlink ){
    int nDelay = db_get_int("auto-hyperlink-delay",0);
    int bMouseover = db_get_boolean("auto-hyperlink-mouseover",0);
    @ <script id='href-data' type='application/json'>\
    @ {"delay":%d(nDelay),"mouseover":%d(bMouseover)}</script>
  }
  @ <script nonce="%h(style_nonce())">/* style.c:%d(__LINE__) */
  @ function debugMsg(msg){
  @ var n = document.getElementById("debugMsg");
  @ if(n){n.textContent=msg;}
  @ }
  if( needHrefJs && g.perm.Hyperlink ){
    @ /* href.js */
    cgi_append_content(builtin_text("href.js"),-1);
  }
  if( blob_size(&blobOnLoad)>0 ){
    @ window.onload = function(){
    cgi_append_content(blob_buffer(&blobOnLoad), blob_size(&blobOnLoad));
    cgi_append_content("\n}\n", -1);
  }
  @ </script>
  builtin_fulfill_js_requests();
}

/*
** Invoke this routine after all of the content for a webpage has been
** generated.  This routine should be called once for every webpage, at
** or near the end of page generation.  This routine does the following:
**
**   *  Populates the header of the page, including setting up appropriate
**      submenu elements.  The header generation is deferred until this point
**      so that we know that all style_submenu_element() and similar have
**      been received.
**
**   *  Finalizes the page content.
**
**   *  Appends the footer.
*/
void style_finish_page(){
  const char *zFooter;
  const char *zAd = 0;
  unsigned int mAdFlags = 0;

  if( !headerHasBeenGenerated ) return;

  /* Go back and put the submenu at the top of the page.  We delay the
  ** creation of the submenu until the end so that we can add elements
  ** to the submenu while generating page text.
  */
  cgi_destination(CGI_HEADER);
  if( submenuEnable && nSubmenu+nSubmenuCtrl>0 ){
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
      builtin_request_js("menu.js");
    }
  }

  zAd = style_adunit_text(&mAdFlags);
  if( (mAdFlags & ADUNIT_RIGHT_OK)!=0  ){
    @ <div class="content adunit_right_container">
    @ <div class="adunit_right">
    cgi_append_content(zAd, -1);
    @ </div>
  }else if( zAd ){
    @ <div class="adunit_banner">
    cgi_append_content(zAd, -1);
    @ </div>
  }

  @ <div class="content"><span id="debugMsg"></span>
  cgi_destination(CGI_BODY);

  if( sideboxUsed ){
    @ <div class="endContent"></div>
  }
  @ </div>

  /* Put the footer at the bottom of the page. */
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
**
** Note that as of 2020-05-28, the default rules are always emitted,
** so the containsSelector() logic is no longer applied when emitting
** style.css. It is unclear whether this test command is now obsolete
** or whether it may still serve a purpose.
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
** If one of the "name" or "page" URL parameters (in that order)
** is set then this function looks for page/page group-specific
** CSS and (if found) appends it to pOut, else it is a no-op.
*/
static void page_style_css_append_page_style(Blob *pOut){
  const char *zPage = PD("name",P("page"));
  char * zFile;
  int nFile = 0;
  const char *zBuiltin;

  if(zPage==0 || zPage[0]==0){
    return;
  }
  zFile = mprintf("style.%s.css", zPage);
  zBuiltin = (const char *)builtin_file(zFile, &nFile);
  if(nFile>0){
    blob_appendf(pOut,
      "\n/***********************************************************\n"
      "** Start of page-specific CSS for page %s...\n"
      "***********************************************************/\n",
      zPage);
    blob_append(pOut, zBuiltin, nFile);
    blob_appendf(pOut,
      "\n/***********************************************************\n"
      "** End of page-specific CSS for page %s.\n"
      "***********************************************************/\n",
      zPage);
    fossil_free(zFile);
    return;
  }
  /* Potential TODO: check for aliases/page groups. e.g. group all
  ** /forumXYZ CSS into one file, all /setupXYZ into another, etc. As
  ** of this writing, doing so would only shave a few kb from
  ** default.css. */
  fossil_free(zFile);
}

/*
** WEBPAGE: style.css
**
** Return the style sheet.
*/
void page_style_css(void){
  Blob css = empty_blob;
  int i;
  const char * zDefaults;

  cgi_set_content_type("text/css");
  etag_check(0, 0);
  /* Emit all default rules... */
  zDefaults = (const char*)builtin_file("default.css", &i);
  blob_append(&css, zDefaults, i);
  /* Page-specific CSS, if any... */
  page_style_css_append_page_style(&css);
  blob_append(&css,
     "\n/***********************************************************\n"
     "** All CSS which follows is supplied by the repository \"skin\".\n"
     "***********************************************************/\n",
     -1);
  blob_append(&css,skin_get("css"),-1);
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
  style_set_current_feature(zFormat[0]==0 ? "test" : "error");
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
#ifndef _WIN32
    @ RSS = %.2f(fossil_rss()/1000000.0) MB</br />
#endif
    @ cgi_csrf_safe(0) = %d(cgi_csrf_safe(0))<br />
    @ fossil_exe_id() = %h(fossil_exe_id())<br />
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
  if( zErr && zErr[0] ){
    style_finish_page();
    cgi_reply();
    fossil_exit(1);
  }else{
    style_finish_page();
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

/*
** Issue a 404 Not Found error for a webpage
*/
void webpage_notfound_error(const char *zFormat, ...){
  char *zMsg;
  va_list ap;
  if( zFormat ){
    va_start(ap, zFormat);
    zMsg = vmprintf(zFormat, ap);
    va_end(ap);
  }else{
    zMsg = "Not Found";
  }
  style_set_current_feature("enotfound");
  style_header("Not Found");
  @ <p>%h(zMsg)</p>
  cgi_set_status(404, "Not Found");
  style_finish_page();
}

#if INTERFACE
# define webpage_assert(T) if(!(T)){webpage_assert_page(__FILE__,__LINE__,#T);}
#endif

/*
** Returns a pseudo-random input field ID, for use in associating an
** ID-less input field with a label. The memory is owned by the
** caller.
*/
static char * style_next_input_id(){
  static int inputID = 0;
  ++inputID;
  return mprintf("input-id-%d", inputID);
}

/*
** Outputs a labeled checkbox element. zWrapperId is an optional ID
** value for the containing element (see below). zFieldName is the
** form element name. zLabel is the label for the checkbox. zValue is
** the optional value for the checkbox. zTip is an optional tooltip,
** which gets set as the "title" attribute of the outermost
** element. If isChecked is true, the checkbox gets the "checked"
** attribute set, else it is not.
**
** Resulting structure:
**
** <div class='input-with-label' title={{zTip}} id={{zWrapperId}}>
**   <input type='checkbox' name={{zFieldName}} value={{zValue}}
**          id='A RANDOM VALUE'
**          {{isChecked ? " checked : ""}}/>
**   <label for='ID OF THE INPUT FIELD'>{{zLabel}}</label>
** </div>
**
** zLabel, and zValue are required. zFieldName, zWrapperId, and zTip
** are may be NULL or empty.
**
** Be sure that the input-with-label CSS class is defined sensibly, in
** particular, having its display:inline-block is useful for alignment
** purposes.
*/
void style_labeled_checkbox(const char * zWrapperId,
                            const char *zFieldName, const char * zLabel,
                            const char * zValue, int isChecked,
                            const char * zTip){
  char * zLabelID = style_next_input_id();
  CX("<div class='input-with-label'");
  if(zTip && *zTip){
    CX(" title='%h'", zTip);
  }
  if(zWrapperId && *zWrapperId){
    CX(" id='%s'",zWrapperId);
  }
  CX("><input type='checkbox' id='%s' ", zLabelID);
  if(zFieldName && *zFieldName){
    CX("name='%s' ",zFieldName);
  }
  CX("value='%T'%s/>",
     zValue ? zValue : "", isChecked ? " checked" : "");
  CX("<label for='%s'>%h</label></div>", zLabelID, zLabel);
  fossil_free(zLabelID);
}

/*
** Outputs a SELECT list from a compile-time list of integers.
** The vargs must be a list of (const char *, int) pairs, terminated
** with a single NULL. Each pair is interpreted as...
**
** If the (const char *) is NULL, it is the end of the list, else
** a new OPTION entry is created. If the string is empty, the
** label and value of the OPTION is the integer part of the pair.
** If the string is not empty, it becomes the label and the integer
** the value. If that value == selectedValue then that OPTION
** element gets the 'selected' attribute.
**
** Note that the pairs are not in (int, const char *) order because
** there is no well-known integer value which we can definitively use
** as a list terminator.
**
** zWrapperId is an optional ID value for the containing element (see
** below).
**
** zFieldName is the value of the form element's name attribute. Note
** that fossil prefers underscores over '-' for separators in form
** element names.
**
** zLabel is an optional string to use as a "label" for the element
** (see below).
**
** zTooltip is an optional value for the SELECT's title attribute.
**
** The structure of the emitted HTML is:
**
** <div class='input-with-label' title={{zToolTip}} id={{zWrapperId}}>
**   <label for='SELECT ELEMENT ID'>{{zLabel}}</label>
**   <select id='RANDOM ID' name={{zFieldName}}>...</select>
** </div>
**
** Example:
**
** style_select_list_int("my-grapes", "my_grapes", "Grapes",
**                      "Select the number of grapes",
**                       atoi(PD("my_field","0")),
**                       "", 1, "2", 2, "Three", 3,
**                       NULL);
** 
*/
void style_select_list_int(const char * zWrapperId,
                           const char *zFieldName, const char * zLabel,
                           const char * zToolTip, int selectedVal,
                           ... ){
  char * zLabelID = style_next_input_id();
  va_list vargs;

  va_start(vargs,selectedVal);
  CX("<div class='input-with-label'");
  if(zToolTip && *zToolTip){
    CX(" title='%h'",zToolTip);
  }
  if(zWrapperId && *zWrapperId){
    CX(" id='%s'",zWrapperId);
  }
  CX(">");
  if(zLabel && *zLabel){
    CX("<label for='%s'>%h</label>", zLabelID, zLabel);
  }
  CX("<select name='%s' id='%s'>",zFieldName, zLabelID);
  while(1){
    const char * zOption = va_arg(vargs,char *);
    int v;
    if(NULL==zOption){
      break;
    }
    v = va_arg(vargs,int);
    CX("<option value='%d'%s>",
         v, v==selectedVal ? " selected" : "");
    if(*zOption){
      CX("%s", zOption);
    }else{
      CX("%d",v);
    }
    CX("</option>\n");
  }
  CX("</select>\n");
  CX("</div>\n");
  va_end(vargs);
  fossil_free(zLabelID);
}

/*
** The C-string counterpart of style_select_list_int(), this variant
** differs only in that its variadic arguments are C-strings in pairs
** of (optionLabel, optionValue). If a given optionLabel is an empty
** string, the corresponding optionValue is used as its label. If any
** given value matches zSelectedVal, that option gets preselected. If
** no options match zSelectedVal then the first entry is selected by
** default.
**
** Any of (zWrapperId, zTooltip, zSelectedVal) may be NULL or empty.
**
** Example:
**
** style_select_list_str("my-grapes", "my_grapes", "Grapes",
**                      "Select the number of grapes",
**                       P("my_field"),
**                       "1", "One", "2", "Two", "", "3",
**                       NULL);
*/
void style_select_list_str(const char * zWrapperId,
                           const char *zFieldName, const char * zLabel,
                           const char * zToolTip, char const * zSelectedVal,
                           ... ){
  char * zLabelID = style_next_input_id();
  va_list vargs;

  va_start(vargs,zSelectedVal);
  if(!zSelectedVal){
    zSelectedVal = __FILE__/*some string we'll never match*/;
  }
  CX("<div class='input-with-label'");
  if(zToolTip && *zToolTip){
    CX(" title='%h'",zToolTip);
  }
  if(zWrapperId && *zWrapperId){
    CX(" id='%s'",zWrapperId);
  }
  CX(">");
  if(zLabel && *zLabel){
    CX("<label for='%s'>%h</label>", zLabelID, zLabel);
  }
  CX("<select name='%s' id='%s'>",zFieldName, zLabelID);
  while(1){
    const char * zLabel = va_arg(vargs,char *);
    const char * zVal;
    if(NULL==zLabel){
      break;
    }
    zVal = va_arg(vargs,char *);
    CX("<option value='%T'%s>",
       zVal, 0==fossil_strcmp(zVal, zSelectedVal) ? " selected" : "");
    if(*zLabel){
      CX("%s", zLabel);
    }else{
      CX("%h",zVal);
    }
    CX("</option>\n");
  }
  CX("</select>\n");
  CX("</div>\n");
  va_end(vargs);
  fossil_free(zLabelID);
}

/*
** Generate a <script> with an appropriate nonce.
**
** zOrigin and iLine are the source code filename and line number
** that generated this request.
*/
void style_script_begin(const char *zOrigin, int iLine){
  const char *z;
  for(z=zOrigin; z[0]!=0; z++){
    if( z[0]=='/' || z[0]=='\\' ){
      zOrigin = z+1;
    }
  }
  CX("<script nonce='%s'>/* %s:%d */\n", style_nonce(), zOrigin, iLine);
}

/* Generate the closing </script> tag 
*/
void style_script_end(void){
  CX("</script>\n");
}

/*
** Emits a NOSCRIPT tag with an error message stating that JS is
** required for the current page. This "should" be called near the top
** of pages which *require* JS. The inner DIV has the CSS class
** 'error' and can be styled via a (noscript > .error) CSS selector.
*/
void style_emit_noscript_for_js_page(void){
  CX("<noscript><div class='error'>"
     "This page requires JavaScript (ES2015, a.k.a. ES6, or newer)."
     "</div></noscript>");
}
