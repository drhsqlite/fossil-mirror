/*
** Copyright (c) 2006,2007 D. Richard Hipp
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
  g.cgiPanic = 1;
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
  @ </div>
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
  @ <table width="%s(zWidth)" align="right" border="1" cellpadding=5
  @  vspace=5 hspace=5>
  @ <tr><td>
  @ <b>%h(zTitle)</b>
}

/* End the side-box
*/
void style_sidebox_end(void){
  @ </td></tr></table>
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
@       href="$baseurl/timeline.rss">
@ <link rel="stylesheet" href="$baseurl/style.css" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
@   <div class="logo">
@     <!-- <img src="logo.gif" alt="logo"><br></br> -->
@     <nobr>$<project_name></nobr>
@   </div>
@   <div class="title">$<title></div>
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
@ if {[hascap h]} {
@   html "<a href='$baseurl/dir'>Files</a> "
@ }
@ if {[hascap o]} {
@   html "<a href='$baseurl/leaves'>Leaves</a> "
@   html "<a href='$baseurl/timeline'>Timeline</a> "
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
*/
const char zDefaultCSS[] = 
@ /* General settings for the entire page */
@ body {
@   margin: 0ex 1ex;
@   padding: 0px;
@   background-color: white;
@   font-family: "sans serif";
@ }
@
@ /* The project logo in the upper left-hand corner of each page */
@ div.logo {
@   display: table-cell;
@   text-align: center;
@   vertical-align: bottom;
@   font-weight: bold;
@   color: #558195;
@ }
@
@ /* The page title centered at the top of each page */
@ div.title {
@   display: table-cell;
@   font-size: 2em;
@   font-weight: bold;
@   text-align: center;
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
@ /* Make the links in the footer less ugly... */
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
@ /* The label/value pairs on (for example) the ci page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }
@
@ /* For marking important UI elements which shouldn't be
@    lightly dismissed. I mainly use it to mark "not yet
@    implemented" parts of a page. Whether or not to have
@    a 'border' attribute set is arguable. */
@ .achtung {
@   color: #ff0000;
@   background: #ffff00;
@   border: 1px solid #ff0000;
@ }
@
@ div.miniform {
@     font-size: smaller;
@     margin: 8px;
@ } 
;

/*
** WEBPAGE: style.css
*/
void page_style_css(void){
  char *zCSS = 0;

  cgi_set_content_type("text/css");
  zCSS = db_get("css",(char*)zDefaultCSS);
  cgi_append_content(zCSS, -1);
}

/*
** WEBPAGE: test_env
*/
void page_test_env(void){
  style_header("Environment Test");
#if !defined(__MINGW32__)
  @ uid=%d(getuid()), gid=%d(getgid())<br>
#endif
  @ g.zBaseURL = %h(g.zBaseURL)<br>
  @ g.zTop = %h(g.zTop)<br>
  cgi_print_all();
  style_footer();
}
