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
** Add a new element to the submenu
*/
void style_submenu_element(
  const char *zLabel,
  const char *zTitle,
  const char *zLink
){
  assert( nSubmenu < sizeof(aSubmenu)/sizeof(aSubmenu[0]) );
  aSubmenu[nSubmenu].zLabel = zLabel;
  aSubmenu[nSubmenu].zTitle = zTitle;
  aSubmenu[nSubmenu].zLink = zLink;
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
** The Subscript interpreter used to render header and footer.
*/
static struct Subscript *pInterp;

/*
** Draw the header.
*/
void style_header(const char *zTitle){
  const char *zLogInOut = "Login";
  const char *zHeader = db_get("header", (char*)zDefaultHeader);  
  login_check_credentials();

  /* Generate the header up through the main menu */
  pInterp = SbS_Create();
  SbS_Store(pInterp, "project_name",
                     db_get("project-name","Unnamed Fossil Project"), 0);
  SbS_Store(pInterp, "title", zTitle, 0);
  SbS_Store(pInterp, "baseurl", g.zBaseURL, 0);
  SbS_Store(pInterp, "manifest_version", MANIFEST_VERSION, 0);
  SbS_Store(pInterp, "manifest_date", MANIFEST_DATE, 0);
  if( g.zLogin ){
    SbS_Store(pInterp, "login", g.zLogin, 0);
    zLogInOut = "Logout";
  }
  SbS_Render(pInterp, zHeader);

  /* Generate the main menu and the submenu (if any) */
  @ <div class="mainmenu">
  @ <a href="%s(g.zBaseURL)/home">Home</a>
  if( g.okRead ){
    @ <a href="%s(g.zBaseURL)/leaves">Leaves</a>
    @ <a href="%s(g.zBaseURL)/timeline">Timeline</a>
  }
  if( g.okRdWiki ){
    @ <a href="%s(g.zBaseURL)/wiki">Wiki</a>
  }
#if 0
  @ <font color="#888888">Search</font>
  @ <font color="#888888">Ticket</font>
  @ <font color="#888888">Reports</font>
#endif
  if( g.okSetup ){
    @ <a href="%s(g.zBaseURL)/setup">Setup</a>
  }
  if( !g.noPswd ){
    @ <a href="%s(g.zBaseURL)/login">%s(zLogInOut)</a>
  }
  @ </div>
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
  g.cgiPanic = 1;
}

/*
** Draw the footer at the bottom of the page.
*/
void style_footer(void){
  const char *zFooter = db_get("footer", (char*)zDefaultFooter);
  @ </div>
  SbS_Render(pInterp, zFooter);
  SbS_Destroy(pInterp);
}

/* @-comment: // */
/*
** The default page header.
*/
const char zDefaultHeader[] = 
@ <html>
@ <head>
@ <title>[project_name html]: [title html]</title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="[baseurl puts]/timeline.rss">
@ <link rel="stylesheet" href="[baseurl puts]/style.css" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
@   <div class="logo">
@     <!-- <img src="logo.gif" alt="logo"><br></br> -->
@     <nobr>[project_name html]</nobr>
@   </div>
@   <div class="title">[title html]</div>
@   <div class="status"><nobr>
@     [/login exists enable_output]     Logged in as [0 /login get html]
@     [/login exists not enable_output] Not logged in
@     [1 enable_output]
@   </nobr></div>
@ </div>
;

/*
** The default page footer
*/
const char zDefaultFooter[] = 
@ <div class="footer">
@ Fossil version [manifest_version puts] [manifest_date puts]
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
  @ g.zBaseURL = %h(g.zBaseURL)<br>
  @ g.zTop = %h(g.zTop)<br>
  cgi_print_all();
  style_footer();
}
