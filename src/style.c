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
  return strcmp(A->zLabel, B->zLabel);
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
  cgi_printf("%s",
     "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\""
     " \"http://www.x3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">");
  
  if( g.thTrace ) Th_Trace("BEGIN_HEADER<br />\n", -1);

  /* Generate the header up through the main menu */
  Th_Store("project_name", db_get("project-name","Unnamed Fossil Project"));
  Th_Store("title", zTitle);
  Th_Store("baseurl", g.zBaseURL);
  Th_Store("index_page", db_get("index-page","/home"));
  Th_Store("current_page", g.zPath);
  Th_Store("manifest_version", MANIFEST_VERSION);
  Th_Store("manifest_date", MANIFEST_DATE);
  if( g.zLogin ){
    Th_Store("login", g.zLogin);
  }
  if( g.thTrace ) Th_Trace("BEGIN_HEADER_SCRIPT<br />\n", -1);
  Th_Render(zHeader);
  if( g.thTrace ) Th_Trace("END_HEADER<br />\n", -1);
  Th_Unstore("title");   /* Avoid collisions with ticket field names */
  cgi_destination(CGI_BODY);
  g.cgiOutput = 1;
  headerHasBeenGenerated = 1;
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
        @ <a class="label" href="%s(p->zLink)">%h(p->zLabel)</a>
      }
    }
    @ </div>
  }
  @ <div class="content">
  cgi_destination(CGI_BODY);

  /* Put the footer at the bottom of the page.
  */
  @ </div><br clear="both"/>
  zFooter = db_get("footer", (char*)zDefaultFooter);
  if( g.thTrace ) Th_Trace("BEGIN_FOOTER<br />\n", -1);
  Th_Render(zFooter);
  if( g.thTrace ) Th_Trace("END_FOOTER<br />\n", -1);
  
  /* Render trace log if TH1 tracing is enabled. */
  if( g.thTrace ){
    cgi_append_content("<font color=\"red\"><hr>\n", -1);
    cgi_append_content(blob_str(&g.thLog), blob_size(&g.thLog));
    cgi_append_content("</font>\n", -1);
  }
}

/*
** Begin a side-box on the right-hand side of a page.  The title and
** the width of the box are given as arguments.  The width is usually
** a percentage of total screen width.
*/
void style_sidebox_begin(const char *zTitle, const char *zWidth){
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
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$baseurl/timeline.rss" />
@ <link rel="stylesheet" href="$baseurl/style.css?default" type="text/css"
@       media="screen" />
@ </head>
@ <body>
@ <div class="header">
@   <div class="logo">
@     <img src="$baseurl/logo" alt="logo">
@   </div>
@   <div class="title"><small>$<project_name></small><br />$<title></div>
@   <div class="status"><nobr><th1>
@      if {[info exists login]} {
@        puts "Logged in as $login"
@      } else {
@        puts "Not logged in"
@      }
@   </th1></nobr></div>
@ </div>
@ <div class="mainmenu"><th1>
@ html "<a href='$baseurl$index_page'>Home</a> "
@ if {[anycap jor]} {
@   html "<a href='$baseurl/timeline'>Timeline</a> "
@ }
@ if {[hascap oh]} {
@   html "<a href='$baseurl/dir?ci=tip'>Files</a> "
@ }
@ if {[hascap o]} {
@   html "<a href='$baseurl/leaves'>Leaves</a> "
@   html "<a href='$baseurl/brlist'>Branches</a> "
@   html "<a href='$baseurl/taglist'>Tags</a> "
@ }
@ if {[hascap r]} {
@   html "<a href='$baseurl/reportlist'>Tickets</a> "
@ }
@ if {[hascap j]} {
@   html "<a href='$baseurl/wiki'>Wiki</a> "
@ }
@ if {[hascap s]} {
@   html "<a href='$baseurl/setup'>Admin</a> "
@ } elseif {[hascap a]} {
@   html "<a href='$baseurl/setup_ulist'>Users</a> "
@ }
@ if {[info exists login]} {
@   html "<a href='$baseurl/login'>Logout</a> "
@ } else {
@   html "<a href='$baseurl/login'>Login</a> "
@ }
@ </th1></div>
;

/*
** The default page footer
*/
const char zDefaultFooter[] = 
@ <div class="footer">
@ Fossil version $manifest_version $manifest_date
@ </div>
@ </body></html>
;

/*
** The default Cascading Style Sheet.
** It's assembled by different strings for each class.
** The default css conatains all definitions.
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
@   color: white;
@ }
@
@ /* The submenu bar that *sometimes* appears below the main menu */
@ div.submenu {
@   padding: 3px 10px 3px 0px;
@   font-size: 0.9em;
@   text-align: center;
@   background-color: #456878;
@   color: white;
@ }
@ div.mainmenu a, div.mainmenu a:visited, div.submenu a, div.submenu a:visited {
@   padding: 3px 10px 3px 10px;
@   color: white;
@   text-decoration: none;
@ }
@ div.mainmenu a:hover, div.submenu a:hover {
@   color: #558195;
@   background-color: white;
@ }
@
@ /* All page content from the bottom of the menu or submenu down to
@ ** the footer */
@ div.content {
@   padding: 0ex 1ex 0ex 2ex;
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
@ }
@
@ /* The footer at the very bottom of the page */
@ div.footer {
@   font-size: 0.8em;
@   margin-top: 12px;
@   padding: 5px 10px 5px 10px;
@   text-align: right;
@   background-color: #558195;
@   color: white;
@ }
@
@ /* Hyperlink colors in the footer */
@ div.footer a { color: white; }
@ div.footer a:link { color: white; }
@ div.footer a:visited { color: white; }
@ div.footer a:hover { background-color: white; color: #558195; }
@ 
@ /* <verbatim> blocks */
@ pre.verbatim {
@    background-color: #f5f5f5;
@    padding: 0.5em;
@}
@
;
const char zTableLabelValueCSS[] = 
@ /* The label/value pairs on (for example) the ci page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }
@
;
const char zDivSidebox[] =
@ /* The nomenclature sidebox for branches,.. */
@ div.sidebox {
@   float: right;
@   border-width: medium;
@   border-style: double;
@   margin: 10;
@ }
@
;
const char zDivSideboxTitle[] =
@ /* The nomenclature title in sideboxes for branches,.. */
@ div.sideboxTitle {
@   display: inline;
@   font-weight: bold;
@ }
@
;
const char zDivSideboxDescribed[] =
@ /* The defined element in sideboxes for branches,.. */
@ div.sideboxDescribed {
@   display: inline;
@   font-weight: bold;
@ }
@
;
const char zSpanDisabled[] =
@ /* The defined element in sideboxes for branches,.. */
@ span.disabled {
@   color: red;
@ }
@
;
const char zSpanTimelineSuppressed[] =
@ /* The suppressed duplicates lines in timeline, .. */
@ span.timelineDisabled {
@   font-style: italic;
@   font-size: small;
@ }
@
;
const char zTableTimelineTable[] =
@ /* the format for the timeline data table */
@ table.timelineTable {
@   cellspacing: 0;
@   border: 0;
@   cellpadding: 0
@ }
@
;
const char zTdTimelineTableCell[] =
@ /* the format for the timeline data cells */
@ td.timelineTableCell {
@   valign: top;
@   align: left;
@ }
@
;
const char zSpanTimelineLeaf[] =
@ /* the format for the timeline leaf marks */
@ span.timelineLeaf {
@   font-weight: bold;
@ }
@
;
const char zATimelineHistLink[] =
@ /* the format for the timeline version links */
@ a.timelineHistLink {
@ }
@
;
const char zSpanTimelineHistDsp[] =
@ /* the format for the timeline version display(no history permission!) */
@ span.timelineHistDsp {
@   font-weight: bold;
@ }
@
;
const char zTdTimelineTime[] =
@ /* the format for the timeline time display */
@ td.timelineTime {
@   vertical-align: top;
@   text-align: right;
@ }
@
;
const char zATagLink[] =
@ /* the format for the tag links */
@ a.tagLink {
@ }
@
;
const char zSpanTagDsp[] =
@ /* the format for the tag display(no history permission!) */
@ span.tagDsp {
@   font-weight: bold;
@ }
@
;
const char zSpanWikiError[] =
@ /* the format for wiki errors */
@ span.wikiError {
@   font-weight: bold;
@   color: red;
@ }
@
;
const struct strctCssDefaults {
  char const * const name;
  char const * const value;
} cssDefaultList[] = {
  { "",                      zDefaultCSS             },
  { "table.label-value",     zTableLabelValueCSS     },
  { "div.sidebox",           zDivSidebox             },
  { "div.sideboxTitle",      zDivSideboxTitle        },
  { "div.sideboxDescribed",  zDivSideboxDescribed    },
  { "span.disabled",         zSpanDisabled           },
  { "span.timelineDisabled", zSpanTimelineSuppressed },
  { "table.timelineTable",   zTableTimelineTable     },
  { "td.timelineTableCell",  zTdTimelineTableCell    },
  { "span.timelineLeaf",     zSpanTimelineLeaf       },
  { "a.timelineHistLink",    zATimelineHistLink      },
  { "span.timelineHistDsp",  zSpanTimelineHistDsp    },
  { "td.timelineTime",       zTdTimelineTime         },
  { "a.tagLink",             zATagLink               },
  { "span.tagDsp",           zSpanTagDsp             },
  { "span.wikiError",        zSpanWikiError          },
  { 0,                       0                       }
};

void cgi_append_default_css(void) {
  enum cssDefaultItems i;

  for (i=0;cssDefaultList[i].name;i++)
    cgi_printf(cssDefaultList[i].value);
}

/*
** WEBPAGE: style.css
*/
void page_style_css(void){
  const char *zCSS    = 0;
  const char *zCSSdef = 0;
  enum cssDefaultItems i;

  cgi_set_content_type("text/css");
  zCSS = db_get("css",(char*)zDefaultCSS);
  /* append user defined css */
  cgi_append_content(zCSS, -1);
  /* add special missing definitions */
  for (i=1;cssDefaultList[i].name;i++)
    if (!strstr(zCSS,cssDefaultList[i].name))
      cgi_append_content(cssDefaultList[i].value, -1);
  g.isConst = 1;
}

/*
** WEBPAGE: test_env
*/
void page_test_env(void){
  style_header("Environment Test");
#if !defined(_WIN32)
  @ uid=%d(getuid()), gid=%d(getgid())<br>
#endif
  @ g.zBaseURL = %h(g.zBaseURL)<br>
  @ g.zTop = %h(g.zTop)<br>
  cgi_print_all();
  style_footer();
}
