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
  const struct Submenu *B = (const struct Submenu*)B;
  return strcmp(A->zLabel, B->zLabel);
}

/*
** Draw the header.
*/
void style_header(const char *zTitle){
  const char *zLogInOut = "Logout";
  login_check_credentials();
  @ <html>
  @ <head>
  @ <title>%s(zTitle)</title>
  @ <link rel="alternate" type="application/rss+xml" title="RSS Feed" href="%s(g.zBaseURL)/timeline.rss">
  @ <link rel="stylesheet" href="%s(g.zBaseURL)/style.css" type="text/css" media="screen">
  @ </head>
  @ <body>
  @ <div id="page-title">%s(zTitle)</div>
  @ <div id="login-status">
  if( g.zLogin==0 ){
    @ not logged in
    zLogInOut = "Login";
  }else{
    @ logged in as %h(g.zLogin)
  }
  @ </div>
  @ <div id="main-menu">
  @ <a href="%s(g.zBaseURL)/index">Home</a>
  if( g.okRead ){
    @ | <a href="%s(g.zBaseURL)/leaves">Leaves</a>
    @ | <a href="%s(g.zBaseURL)/timeline">Timeline</a>
  }
  if( g.okRdWiki ){
    @ | <a href="%s(g.zBaseURL)/wiki">Wiki</a>
  }
#if 0
  @ | <font color="#888888">Search</font>
  @ | <font color="#888888">Ticket</font>
  @ | <font color="#888888">Reports</font>
#endif
  if( g.okSetup ){
    @ | <a href="%s(g.zBaseURL)/setup">Setup</a>
  }
  if( !g.noPswd ){
    @ | <a href="%s(g.zBaseURL)/login">%s(zLogInOut)</a>
  }
  @ </div>
  if( nSubmenu>0 ){
    @ <div id="sub-menu">
    int i;
    qsort(aSubmenu, nSubmenu, sizeof(aSubmenu[0]), submenuCompare);
    for(i=0; i<nSubmenu; i++){
      struct Submenu *p = &aSubmenu[i];
      char *zTail = i<nSubmenu-1 ? " | " : "";
      if( p->zLink==0 ){
        @ <span class="label">%h(p->zLabel)</span>
        @ <span class="tail">%s(zTail)</span>
      }else{
        @ <a class="label" href="%T(p->zLink)">%h(p->zLabel)</a>
        @ <span class="tail">%s(zTail)</span>
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
** WEBPAGE: index
** WEBPAGE: home
** WEBPAGE: not_found
*/
void page_index(void){
  char *zHome = db_get("homepage", 0);
  if( zHome ){
    g.zExtra = zHome;
    g.okRdWiki = 1;
    wiki_page();
  }else{
    style_header("Main Title Page");
    @ No homepage configured for this server
    style_footer();
  }
}

/*
** TODO: COPIED FROM WIKI.C... BAD
*/
/*
** Create a fake replicate of the "vfile" table as a TEMP table
** using the manifest identified by manid.
*/
static void style_create_fake_vfile(int manid){
  static const char zVfileDef[] = 
    @ CREATE TEMP TABLE vfile(
    @   id INTEGER PRIMARY KEY,     -- ID of the checked out file
    @   vid INTEGER REFERENCES blob, -- The version this file is part of.
    @   chnged INT DEFAULT 0,       -- 0:unchnged 1:edited 2:m-chng 3:m-add
    @   deleted BOOLEAN DEFAULT 0,  -- True if deleted 
    @   rid INTEGER,                -- Originally from this repository record
    @   mrid INTEGER,               -- Based on this record due to a merge
    @   pathname TEXT,              -- Full pathname
    @   UNIQUE(pathname,vid)
    @ );
    ;
  db_multi_exec(zVfileDef);
  load_vfile_from_rid(manid);
}


/*
** WEBPAGE: style.css
*/
void page_style_css(void){
  Stmt q;
  int id = 0;
  int rid = 0;
  int chnged = 0;
  char *zPathname = 0;
  char *z;
  
  cgi_set_content_type("text/css");

  login_check_credentials();
  if( !g.localOpen ){
    int headid = db_int(0,
       "SELECT cid FROM plink ORDER BY mtime DESC LIMIT 1"
    );
    style_create_fake_vfile(headid);
  }
  
  db_prepare(&q,
     "SELECT id, rid, chnged, pathname FROM vfile"
     " WHERE (pathname='style.css' OR pathname LIKE '%%/style.css')"
     "   AND NOT deleted"
  );
  if( db_step(&q)==SQLITE_ROW ){
    id = db_column_int(&q, 0);
    rid = db_column_int(&q, 1);
    chnged = db_column_int(&q, 2);
    if( chnged || rid==0 ){
      zPathname = db_column_malloc(&q, 3);
    }
  }
  db_finalize(&q);
  if( id ){
    Blob src;
    blob_zero(&src);
    if( zPathname ){
      zPathname = mprintf("%s/%z", g.zLocalRoot, zPathname);
      blob_read_from_file(&src, zPathname);
      free(zPathname);
    }else{
      content_get(rid, &src);
    }

    z = blob_str(&src);
    @ %s(z)
  }else{
    /* No CSS file found, use our own */
    /*
    ** Selector order: tags, ids, classes, other
    ** Content order: margin, borders, padding, fonts, colors, other 
    ** Note: Once things are finialize a bit we can collapse this and
    **       make it much smaller, if necessary. Right now, it's verbose
    **       but easy to edit.
    */
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
  }
}

/*
** WEBPAGE: test_env
*/
void page_test_env(void){
  style_header("Environment Test");
  cgi_print_all();
  style_footer();
}
