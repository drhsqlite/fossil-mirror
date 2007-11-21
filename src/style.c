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
** Draw the header.
*/
void style_header(const char *zTitle){
  const char *zLogInOut = "Login";
  const char *zHeader = db_get("header", zDefaultHeader);  
  struct Subscript *p;
  login_check_credentials();

  /* Generate the header up through the main menu */
  p = SbS_Create();
  SbS_Store(p, "title", zTitle, 0);
  SbS_Store(p, "baseurl", g.zBaseURL, 0);
  if( g.zLogin ){
    SbS_Store(p, "login", g.zLogin, 0);
    zLogInOut = "Logout";
  }
  SbS_Render(p, zHeader);
  SbS_Destroy(p);

  /* Generate the main menu and the submenu (if any) */
  @ <div id="main-menu">
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
    @ <div id="sub-menu">
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
  @ <div id="page">
  g.cgiPanic = 1;
}

/*
** Draw the footer at the bottom of the page.
*/
void style_footer(void){
  /* end the <div id="page"> from style_header() */
  @ </div>
  @ <div id="style-footer">
  @ Fossil version %s(MANIFEST_VERSION) %s(MANIFEST_DATE)
  @ </div>
}

/*
** The default page header.
*/
const char zDefaultHeader[] = 
@ <html>
@ <head>
@ <title>Edit CSS</title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="[baseurl puts]/timeline.rss">
@ <link rel="stylesheet" href="[baseurl puts]/style.css" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div id="page-title">[title html]</div>
@ <div id="login-status">
@ [/login exists enable_output]
@ logged in as [0 /login get html]
@ [/login exists not enable_output]
@ not logged in
@ [1 enable_output]
@ </div>
;

/*
** The default Cascading Style Sheet.
**
** Selector order: tags, ids, classes, other
** Content order: margin, borders, padding, fonts, colors, other
** Note: Once things are finialize a bit we can collapse this and
**       make it much smaller, if necessary. Right now, it's verbose
**       but easy to edit.
*/
const char zDefaultCSS[] = 
@ body {
@   margin: 0px;
@   padding: 0px;
@   background-color: white;
@ }
@ #page-title {
@   padding: 10px 10px 10px 10px;
@   font-size: 1.8em;
@   font-weight: bold;
@   background-color: #6a7ec7;
@   color: #0a1e67;
@ }
@ #login-status {
@   padding: 0px 10px 10px 0px;
@   font-size: 0.9em;
@   text-align: right;
@   background-color: #6a7ec7;
@   color: white;
@   position: absolute;
@   top: 10;
@   right: 0;
@ }
@ #main-menu {
@   padding: 5px 10px 5px 10px;
@   font-size: 0.9em;
@   font-weight: bold;
@   text-align: center;
@   letter-spacing: 1px;
@   background-color: #414f84;
@   color: white;
@ }
@ #sub-menu {
@   padding: 3px 10px 3px 0px;
@   font-size: 0.9em;
@   text-align: center;
@   background-color: #414f84;
@   color: white;
@ }
@ #main-menu a, #main-menu a:visited, #sub-menu a, #sub-menu a:visited {
@   padding: 3px 10px 3px 10px;
@   color: white;
@ }
@ #main-menu a:hover, #sub-menu a:hover {
@   color: #414f84;
@   background-color: white;
@ }
@ #page {
@   padding: 10px 20px 10px 20px;
@ }
@ #style-footer {
@   font-size: 0.8em;
@   margin-top: 12px;
@   padding: 5px 10px 5px 10px;
@   text-align: right;
@   background-color: #414f84;
@   color: white;
@ }
@ table.label-value th {
@   text-align: right;
@   vertical-align: top;
@ }
@ div.section-title {
@   margin-bottom: 0px;
@   padding: 1px 1px 1px 1px;
@   font-size: 1.2em;
@   font-weight: bold;
@   background-color: #6a7ec7;
@   color: #0a1e67;
@ }
;

/*
** WEBPAGE: style.css
*/
void page_style_css(void){
  char *zCSS = 0;

  cgi_set_content_type("text/css");
  zCSS = db_get("css",0);
  if( zCSS ){
    cgi_append_content(zCSS, -1);
  }else{
    cgi_append_content(zDefaultCSS, -1);
  }
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
