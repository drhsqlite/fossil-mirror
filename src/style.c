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
** structure and displayed below the main menu by style_header().
**
** Populate this structure with calls to style_submenu_element()
** prior to calling style_header().
*/
static struct Submenu {
  const char *zLabel;
  const char *zTitle;
  const char *zLink;
} aSubmenu[30];
static int nSubmenu = 0;

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
  if( sqlite3_strglob("*Opera Mini/[1-9]*", P("HTTP_USER_AGENT"))==0 ){
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
  const char *zTitle,
  const char *zLink,
  ...
){
  va_list ap;
  assert( nSubmenu < sizeof(aSubmenu)/sizeof(aSubmenu[0]) );
  aSubmenu[nSubmenu].zLabel = zLabel;
  aSubmenu[nSubmenu].zTitle = zTitle;
  va_start(ap, zLink);
  aSubmenu[nSubmenu].zLink = vmprintf(zLink, ap);
  va_end(ap);
  nSubmenu++;
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
  char *zMtime = db_get_mtime(zConfigName, 0, 0);
  char *zUrl = mprintf("%s/%s/%s%.5s", g.zTop, zPageName, zMtime,
                       MANIFEST_UUID);
  char *zVarName = mprintf("%s_url", zVarPrefix);
  Th_Store(zVarName, zUrl);
  free(zMtime);
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
  const char *zHeader = db_get("header", (char*)zDefaultHeader);
  login_check_credentials();

  va_start(ap, zTitleFormat);
  zTitle = vmprintf(zTitleFormat, ap);
  va_end(ap);

  cgi_destination(CGI_HEADER);

  @ <!DOCTYPE html>

  if( g.thTrace ) Th_Trace("BEGIN_HEADER<br />\n", -1);

  /* Generate the header up through the main menu */
  Th_Store("project_name", db_get("project-name","Unnamed Fossil Project"));
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
  @ if(/^#/.test(x)) x = x.substr(1);
  @ var e = document.getElementById(x);
  @ if(!e) throw new Error("Expecting element with ID "+x);
  @ else return e;}
  @ </script>
}

/*
** Append ad unit text if appropriate.
*/
static void style_ad_unit(void){
  const char *zAd;
  if( g.perm.Admin && db_get_boolean("adunit-omit-if-admin",0) ){
    return;
  }
  if( !login_is_nobody()
   && fossil_strcmp(g.zLogin,"anonymous")!=0
   && db_get_boolean("adunit-omit-if-user",0)
  ){
    return;
  }
  zAd = db_get("adunit", 0);
  if( zAd ) cgi_append_content(zAd, -1);
}

/*
** Draw the footer at the bottom of the page.
*/
void style_footer(void){
  const char *zFooter;

  if( !headerHasBeenGenerated ) return;

  /* Go back and put the submenu at the top of the page.  We delay the
  ** creation of the submenu until the end so that we can add elements
  ** to the submenu while generating page text.
  */
  cgi_destination(CGI_HEADER);
  if( nSubmenu>0 ){
    int i;
    @ <div class="submenu">
    qsort(aSubmenu, nSubmenu, sizeof(aSubmenu[0]), submenuCompare);
    for(i=0; i<nSubmenu; i++){
      struct Submenu *p = &aSubmenu[i];
      if( p->zLink==0 ){
        @ <span class="label">%h(p->zLabel)</span>
      }else{
        @ <a class="label" href="%h(p->zLink)">%h(p->zLabel)</a>
      }
    }
    @ </div>
  }
  style_ad_unit();
  @ <div class="content">
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

  zFooter = db_get("footer", (char*)zDefaultFooter);
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

/* @-comment: // */
/*
** The default page header.
*/
const char zDefaultHeader[] =
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
@ <div class="header">
@   <div class="logo">
@     <img src="$logo_image_url" alt="logo" />
@   </div>
@   <div class="title"><small>$<project_name></small><br />$<title></div>
@   <div class="status"><th1>
@      if {[info exists login]} {
@        puts "Logged in as $login"
@      } else {
@        puts "Not logged in"
@      }
@   </th1></div>
@ </div>
@ <div class="mainmenu">
@ <th1>
@ html "<a href='$home$index_page'>Home</a>\n"
@ if {[anycap jor]} {
@   html "<a href='$home/timeline'>Timeline</a>\n"
@ }
@ if {[hascap oh]} {
@   html "<a href='$home/tree?ci=tip'>Files</a>\n"
@ }
@ if {[hascap o]} {
@   html "<a href='$home/brlist'>Branches</a>\n"
@   html "<a href='$home/taglist'>Tags</a>\n"
@ }
@ if {[hascap r]} {
@   html "<a href='$home/reportlist'>Tickets</a>\n"
@ }
@ if {[hascap j]} {
@   html "<a href='$home/wiki'>Wiki</a>\n"
@ }
@ if {[hascap s]} {
@   html "<a href='$home/setup'>Admin</a>\n"
@ } elseif {[hascap a]} {
@   html "<a href='$home/setup_ulist'>Users</a>\n"
@ }
@ if {[info exists login]} {
@   html "<a href='$home/login'>Logout</a>\n"
@ } else {
@   html "<a href='$home/login'>Login</a>\n"
@ }
@ </th1></div>
;

/*
** The default page footer
*/
const char zDefaultFooter[] =
@ <div class="footer">
@ This page was generated in about
@ <th1>puts [expr {([utime]+[stime]+1000)/1000*0.001}]</th1>s by
@ Fossil version $manifest_version $manifest_date
@ </div>
@ </body></html>
;

/*
** The default Cascading Style Sheet.
** It's assembled by different strings for each class.
** The default css contains all definitions.
** The style sheet, send to the client only contains the ones,
** not defined in the user defined css.
*/
const char zDefaultCSS[] =
@ /* General settings for the entire page */
@ body {
@   margin: 0ex 1ex;
@   padding: 0px;
@   background-color: white;
@   font-family: sans-serif;
@ }
@
@ /* The project logo in the upper left-hand corner of each page */
@ div.logo {
@   display: table-cell;
@   text-align: center;
@   vertical-align: bottom;
@   font-weight: bold;
@   color: #558195;
@   min-width: 200px;
@   white-space: nowrap;
@ }
@
@ /* The page title centered at the top of each page */
@ div.title {
@   display: table-cell;
@   font-size: 2em;
@   font-weight: bold;
@   text-align: center;
@   padding: 0 0 0 1em;
@   color: #558195;
@   vertical-align: bottom;
@   width: 100%;
@ }
@
@ /* The login status message in the top right-hand corner */
@ div.status {
@   display: table-cell;
@   text-align: right;
@   vertical-align: bottom;
@   color: #558195;
@   font-size: 0.8em;
@   font-weight: bold;
@   min-width: 200px;
@   white-space: nowrap;
@ }
@
@ /* The header across the top of the page */
@ div.header {
@   display: table;
@   width: 100%;
@ }
@
@ /* The main menu bar that appears at the top of the page beneath
@ ** the header */
@ div.mainmenu {
@   padding: 5px 10px 5px 10px;
@   font-size: 0.9em;
@   font-weight: bold;
@   text-align: center;
@   letter-spacing: 1px;
@   background-color: #558195;
@   border-top-left-radius: 8px;
@   border-top-right-radius: 8px;
@   color: white;
@ }
@
@ /* The submenu bar that *sometimes* appears below the main menu */
@ div.submenu, div.sectionmenu {
@   padding: 3px 10px 3px 0px;
@   font-size: 0.9em;
@   text-align: center;
@   background-color: #456878;
@   color: white;
@ }
@ div.mainmenu a, div.mainmenu a:visited, div.submenu a, div.submenu a:visited,
@ div.sectionmenu>a.button:link, div.sectionmenu>a.button:visited {
@   padding: 3px 10px 3px 10px;
@   color: white;
@   text-decoration: none;
@ }
@ div.mainmenu a:hover, div.submenu a:hover, div.sectionmenu>a.button:hover {
@   color: #558195;
@   background-color: white;
@ }
@
@ /* All page content from the bottom of the menu or submenu down to
@ ** the footer */
@ div.content {
@   padding: 0ex 1ex 1ex 1ex;
@   border: solid #aaa;
@   border-width: 1px;
@ }
@
@ /* Some pages have section dividers */
@ div.section {
@   margin-bottom: 0px;
@   margin-top: 1em;
@   padding: 1px 1px 1px 1px;
@   font-size: 1.2em;
@   font-weight: bold;
@   background-color: #558195;
@   color: white;
@   white-space: nowrap;
@ }
@
@ /* The "Date" that occurs on the left hand side of timelines */
@ div.divider {
@   background: #a1c4d4;
@   border: 2px #558195 solid;
@   font-size: 1em; font-weight: normal;
@   padding: .25em;
@   margin: .2em 0 .2em 0;
@   float: left;
@   clear: left;
@   white-space: nowrap;
@ }
@
@ /* The footer at the very bottom of the page */
@ div.footer {
@   clear: both;
@   font-size: 0.8em;
@   padding: 5px 10px 5px 10px;
@   text-align: right;
@   background-color: #558195;
@   border-bottom-left-radius: 8px;
@   border-bottom-right-radius: 8px;
@   color: white;
@ }
@
@ /* Hyperlink colors in the footer */
@ div.footer a { color: white; }
@ div.footer a:link { color: white; }
@ div.footer a:visited { color: white; }
@ div.footer a:hover { background-color: white; color: #558195; }
@
@ /* verbatim blocks */
@ pre.verbatim {
@   background-color: #f5f5f5;
@   padding: 0.5em;
@   white-space: pre-wrap;
@}
;


/* The following table contains bits of default CSS that must
** be included if they are not found in the application-defined
** CSS.
*/
const struct strctCssDefaults {
  const char *elementClass;  /* Name of element needed */
  const char *comment;       /* Comment text */
  const char *value;         /* CSS text */
} cssDefaultList[] = {
  { "",
    "",
    zDefaultCSS
  },
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
  },
  { "td.timelineTableCell",
    "the format for the timeline data cells",
    @   vertical-align: top;
    @   text-align: left;
  },
  { "tr.timelineCurrent td.timelineTableCell",
    "the format for the timeline data cell of the current checkout",
    @   padding: .1em .2em;
    @   border: 1px dashed #446979;
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
    "the format for the grap placeholder cells in timelines",
    @ width: 20px;
    @ text-align: left;
    @ vertical-align: top;
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
  { "ul.browser",
    "format for the list in the file browser",
    @   margin-left: 0.5em;
    @   padding-left: 0.5em;
    @   white-space: nowrap;
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
    @   position: relative;
    @   z-index: 1;
    @   display: table-cell;
    @   min-height: 16px;
    @   padding-left: 21px;
    @   background-image: url(data:image/gif;base64,R0lGODlhEAAQAJEAAP\/\/\/yEhIf\/\/\/wAAACH5BAEHAAIALAAAAAAQABAAAAIvlIKpxqcfmgOUvoaqDSCxrEEfF14GqFXImJZsu73wepJzVMNxrtNTj3NATMKhpwAAOw==);
    @   background-position: center left;
    @   background-repeat: no-repeat;
  },
  { "div.filetreeline",
    "line of a file tree",
    @   display: table;
    @   width: 100%;
    @   white-space: nowrap;
  },
  { ".filetree .dir > div.filetreeline > a",
    "tree-view directory links",
    @   background-image: url(data:image/gif;base64,R0lGODlhEAAQAJEAAP/WVCIiIv\/\/\/wAAACH5BAEHAAIALAAAAAAQABAAAAInlI9pwa3XYniCgQtkrAFfLXkiFo1jaXpo+jUs6b5Z/K4siDu5RPUFADs=);
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
    "format for user color input on checkin edit page",
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
  { "#canvas", "timeline graph node colors",
    @ color: black;
    @ background-color: white;
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
    @ word-break: break-all;
    @ word-wrap: break-word;
    @ max-width: 50%;
  },
  { ".brlist table",  "The list of branches",
    @ border-spacing: 0;
  },
  { ".brlist table th",  "Branch list table headers",
    @ text-align: left;
    @ padding: 0px 1em 0.5ex 0px;
  },
  { ".brlist table td",  "Branch list table headers",
    @ padding: 0px 2em 0px 0px;
  },
  { "th.sort:after",
    "General styles for sortable column marker",
    @ margin-left: .4em;
    @ cursor: pointer;
    @ text-shadow: 0 0 0 #000; /* Makes arrow darker */
  },
  { "th.sort.asc:after",
    "Ascending sort column marker",
    @ content: '\2193';
  },
  { "th.sort.desc:after",
    "Descending sort column marker",
    @ content: '\2191';
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

  for( i=0; cssDefaultList[i].elementClass; i++ ){
    if( cssDefaultList[i].elementClass[0] ){
      cgi_printf("/* %s */\n%s {\n%s\n}\n\n",
                 cssDefaultList[i].comment,
                 cssDefaultList[i].elementClass,
                 cssDefaultList[i].value
                );
    }else{
      cgi_printf("%s",
                 cssDefaultList[i].value
                );
    }
  }
}

/*
** WEBPAGE: style.css
*/
void page_style_css(void){
  Blob css;
  int i;

  cgi_set_content_type("text/css");
  blob_init(&css, db_get("css",(char*)zDefaultCSS), -1);

  /* add special missing definitions */
  for(i=1; cssDefaultList[i].elementClass; i++){
    if( strstr(blob_str(&css), cssDefaultList[i].elementClass)==0 ){
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
*/
void page_test_env(void){
  char c;
  int i;
  int showAll;
  char zCap[30];
  static const char *const azCgiVars[] = {
    "COMSPEC", "DOCUMENT_ROOT", "GATEWAY_INTERFACE",
    "HTTP_ACCEPT", "HTTP_ACCEPT_CHARSET", "HTTP_ACCEPT_ENCODING",
    "HTTP_ACCEPT_LANGUAGE", "HTTP_CONNECTION", "HTTP_HOST",
    "HTTP_USER_AGENT", "HTTP_REFERER", "PATH_INFO", "PATH_TRANSLATED",
    "QUERY_STRING", "REMOTE_ADDR", "REMOTE_PORT", "REQUEST_METHOD",
    "REQUEST_URI", "SCRIPT_FILENAME", "SCRIPT_NAME", "SERVER_PROTOCOL",
  };

  login_check_credentials();
  if( !g.perm.Admin && !g.perm.Setup && !db_get_boolean("test_env_enable",0) ){
    login_needed();
    return;
  }
  for(i=0; i<count(azCgiVars); i++) (void)P(azCgiVars[i]);
  style_header("Environment Test");
  showAll = atoi(PD("showall","0"));
  if( !showAll ){
    style_submenu_element("Show Cookies", "Show Cookies",
                          "%s/test_env?showall=1", g.zTop);
  }else{
    style_submenu_element("Hide Cookies", "Hide Cookies",
                          "%s/test_env", g.zTop);
  }
#if !defined(_WIN32)
  @ uid=%d(getuid()), gid=%d(getgid())<br />
#endif
  @ g.zBaseURL = %h(g.zBaseURL)<br />
  @ g.zHttpsURL = %h(g.zHttpsURL)<br />
  @ g.zTop = %h(g.zTop)<br />
  for(i=0, c='a'; c<='z'; c++){
    if( login_has_capability(&c, 1) ) zCap[i++] = c;
  }
  zCap[i] = 0;
  @ g.userUid = %d(g.userUid)<br />
  @ g.zLogin = %h(g.zLogin)<br />
  @ g.isHuman = %d(g.isHuman)<br />
  @ capabilities = %s(zCap)<br />
  @ g.zRepositoryName = %h(g.zRepositoryName)<br />
  @ load_average() = %f(load_average())<br />
  @ <hr>
  P("HTTP_USER_AGENT");
  cgi_print_all(showAll);
  if( showAll && blob_size(&g.httpHeader)>0 ){
    @ <hr>
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
** This page is a honeypot for spiders and bots.
**
** WEBPAGE: honeypot
*/
void honeypot_page(void){
  cgi_set_status(403, "Forbidden");
  @ <p>Please enable javascript or log in to see this content</p>
}
