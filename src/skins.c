/*
** Copyright (c) 2009 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License as published by the Free Software Foundation; either
** version 2 of the License, or (at your option) any later version.
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
** Implementation of the Setup page for "skins".
*/
#include <assert.h>
#include "config.h"
#include "skins.h"

/* @-comment: // */
/*
** A black-and-white theme with the project title in a bar across the top
** and no logo image.
*/
static const char zBuiltinSkin1[] = 
@ REPLACE INTO config VALUES('css','/* General settings for the entire page */
@ body {
@   margin: 0ex 1ex;
@   padding: 0px;
@   background-color: white;
@   font-family: "sans serif";
@ }
@ 
@ /* The project logo in the upper left-hand corner of each page */
@ div.logo {
@   display: table-row;
@   text-align: center;
@   /* vertical-align: bottom;*/
@   font-size: 2em;
@   font-weight: bold;
@   background-color: #707070;
@   color: #ffffff;
@ }
@ 
@ /* The page title centered at the top of each page */
@ div.title {
@   display: table-cell;
@   font-size: 1.5em;
@   font-weight: bold;
@   text-align: left;
@   padding: 0 0 0 10px;
@   color: #404040;
@   vertical-align: bottom;
@   width: 100%;
@ }
@ 
@ /* The login status message in the top right-hand corner */
@ div.status {
@   display: table-cell;
@   text-align: right;
@   vertical-align: bottom;
@   color: #404040;
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
@   background-color: #404040;
@   color: white;
@ }
@ 
@ /* The submenu bar that *sometimes* appears below the main menu */
@ div.submenu {
@   padding: 3px 10px 3px 0px;
@   font-size: 0.9em;
@   text-align: center;
@   background-color: #606060;
@   color: white;
@ }
@ div.mainmenu a, div.mainmenu a:visited, div.submenu a, div.submenu a:visited {
@   padding: 3px 10px 3px 10px;
@   color: white;
@   text-decoration: none;
@ }
@ div.mainmenu a:hover, div.submenu a:hover {
@   color: #404040;
@   background-color: white;
@ }
@ 
@ /* All page content from the bottom of the menu or submenu down to
@ ** the footer */
@ div.content {
@   padding: 0ex 0ex 0ex 0ex;
@ }
@ /* Hyperlink colors */
@ div.content a { color: #604000; }
@ div.content a:link { color: #604000;}
@ div.content a:visited { color: #600000; }
@ 
@ /* Some pages have section dividers */
@ div.section {
@   margin-bottom: 0px;
@   margin-top: 1em;
@   padding: 1px 1px 1px 1px;
@   font-size: 1.2em;
@   font-weight: bold;
@   background-color: #404040;
@   color: white;
@ }
@ 
@ /* The "Date" that occurs on the left hand side of timelines */
@ div.divider {
@   background: #a0a0a0;
@   border: 2px #505050 solid;
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
@   background-color: #404040;
@   color: white;
@ }
@ 
@ /* The label/value pairs on (for example) the vinfo page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }');
@ REPLACE INTO config VALUES('header','<html>
@ <head>
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$baseurl/timeline.rss">
@ <link rel="stylesheet" href="$baseurl/style.css?blackwhite" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
@   <div class="logo">
@     <nobr>$<project_name></nobr>
@   </div>
@ </div>
@ <div class="header">
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
@ html "<a href=''$baseurl$index_page''>Home</a> "
@ if {[anycap jor]} {
@   html "<a href=''$baseurl/timeline''>Timeline</a> "
@ }
@ if {[hascap oh]} {
@   html "<a href=''$baseurl/dir''>Files</a> "
@ }
@ if {[hascap o]} {
@   html "<a href=''$baseurl/leaves''>Leaves</a> "
@   html "<a href=''$baseurl/brlist''>Branches</a> "
@   html "<a href=''$baseurl/taglist''>Tags</a> "
@ }
@ if {[hascap r]} {
@   html "<a href=''$baseurl/reportlist''>Tickets</a> "
@ }
@ if {[hascap j]} {
@   html "<a href=''$baseurl/wiki''>Wiki</a> "
@ }
@ if {[hascap s]} {
@   html "<a href=''$baseurl/setup''>Admin</a> "
@ } elseif {[hascap a]} {
@   html "<a href=''$baseurl/setup_ulist''>Users</a> "
@ }
@ if {[info exists login]} {
@   html "<a href=''$baseurl/login''>Logout</a> "
@ } else {
@   html "<a href=''$baseurl/login''>Login</a> "
@ }
@ </th1></div>
@ ');
@ REPLACE INTO config VALUES('footer','<div class="footer">
@ Fossil version $manifest_version $manifest_date 
@ </div>
@ </body></html>
@ ');
;

/*
** A tan theme with the project title above the user identification
** and no logo image.
*/
static const char zBuiltinSkin2[] = 
@ REPLACE INTO config VALUES('css','/* General settings for the entire page */
@ body {
@   margin: 0ex 0ex;
@   padding: 0px;
@   background-color: #fef3bc;
@   font-family: sans-serif;
@ }
@ 
@ /* The project logo in the upper left-hand corner of each page */
@ div.logo {
@   display: inline;
@   text-align: center;
@   vertical-align: bottom;
@   font-weight: bold;
@   font-size: 2.5em;
@   color: #a09048;
@ }
@ 
@ /* The page title centered at the top of each page */
@ div.title {
@   display: table-cell;
@   font-size: 2em;
@   font-weight: bold;
@   text-align: left;
@   padding: 0 0 0 5px;
@   color: #a09048;
@   vertical-align: bottom;
@   width: 100%;
@ }
@ 
@ /* The login status message in the top right-hand corner */
@ div.status {
@   display: table-cell;
@   text-align: right;
@   vertical-align: bottom;
@   color: #a09048;
@   padding: 5px 5px 0 0;
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
@   background-color: #a09048;
@   color: black;
@ }
@ 
@ /* The submenu bar that *sometimes* appears below the main menu */
@ div.submenu {
@   padding: 3px 10px 3px 0px;
@   font-size: 0.9em;
@   text-align: center;
@   background-color: #c0af58;
@   color: white;
@ }
@ div.mainmenu a, div.mainmenu a:visited, div.submenu a, div.submenu a:visited {
@   padding: 3px 10px 3px 10px;
@   color: white;
@   text-decoration: none;
@ }
@ div.mainmenu a:hover, div.submenu a:hover {
@   color: #a09048;
@   background-color: white;
@ }
@ 
@ /* All page content from the bottom of the menu or submenu down to
@ ** the footer */
@ div.content {
@   padding: 1ex 5px;
@ }
@ div.content a { color: #706532; }
@ div.content a:link { color: #706532; }
@ div.content a:visited { color: #704032; }
@ div.content a:hover { background-color: white; color: #706532; }
@ 
@ /* Some pages have section dividers */
@ div.section {
@   margin-bottom: 0px;
@   margin-top: 1em;
@   padding: 3px 3px 0 3px;
@   font-size: 1.2em;
@   font-weight: bold;
@   background-color: #a09048;
@   color: white;
@ }
@ 
@ /* The "Date" that occurs on the left hand side of timelines */
@ div.divider {
@   background: #e1d498;
@   border: 2px #a09048 solid;
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
@   background-color: #a09048;
@   color: white;
@ }
@ 
@ /* Hyperlink colors */
@ div.footer a { color: white; }
@ div.footer a:link { color: white; }
@ div.footer a:visited { color: white; }
@ div.footer a:hover { background-color: white; color: #558195; }
@ 
@ /* <verbatim> blocks */
@ pre.verbatim {
@    background-color: #f5f5f5;
@    padding: 0.5em;
@ }
@ 
@ /* The label/value pairs on (for example) the ci page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }
@ ');
@ REPLACE INTO config VALUES('header','<html>
@ <head>
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$baseurl/timeline.rss">
@ <link rel="stylesheet" href="$baseurl/style.css?tan" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
@   <div class="title">$<title></div>
@   <div class="status">
@     <div class="logo"><nobr>$<project_name></nobr></div><br/>
@     <nobr><th1>
@      if {[info exists login]} {
@        puts "Logged in as $login"
@      } else {
@        puts "Not logged in"
@      }
@   </th1></nobr></div>
@ </div>
@ <div class="mainmenu"><th1>
@ html "<a href=''$baseurl$index_page''>Home</a> "
@ if {[anycap jor]} {
@   html "<a href=''$baseurl/timeline''>Timeline</a> "
@ }
@ if {[hascap oh]} {
@   html "<a href=''$baseurl/dir''>Files</a> "
@ }
@ if {[hascap o]} {
@   html "<a href=''$baseurl/leaves''>Leaves</a> "
@   html "<a href=''$baseurl/brlist''>Branches</a> "
@   html "<a href=''$baseurl/taglist''>Tags</a> "
@ }
@ if {[hascap r]} {
@   html "<a href=''$baseurl/reportlist''>Tickets</a> "
@ }
@ if {[hascap j]} {
@   html "<a href=''$baseurl/wiki''>Wiki</a> "
@ }
@ if {[hascap s]} {
@   html "<a href=''$baseurl/setup''>Admin</a> "
@ } elseif {[hascap a]} {
@   html "<a href=''$baseurl/setup_ulist''>Users</a> "
@ }
@ if {[info exists login]} {
@   html "<a href=''$baseurl/login''>Logout</a> "
@ } else {
@   html "<a href=''$baseurl/login''>Login</a> "
@ }
@ </th1></div>
@ ');
@ REPLACE INTO config VALUES('footer','<div class="footer">
@ Fossil version $manifest_version $manifest_date
@ </div>
@ </body></html>
@ ');
;

/*
** Black letters on a white or cream background with the main menu
** stuck on the left-hand side.
*/
static const char zBuiltinSkin3[] = 
@ REPLACE INTO config VALUES('css','/* General settings for the entire page */
@ body {
@     margin:0px 0px 0px 0px;
@     padding:0px;
@     font-family:verdana, arial, helvetica, "sans serif";
@     color:#333;
@     background-color:white;
@ }
@ 
@ /* consistent colours */
@ h2 {
@   color: #333;
@ }
@ h3 {
@   color: #333;
@ }
@ 
@ /* The project logo in the upper left-hand corner of each page */
@ div.logo {
@   display: table-cell;
@   text-align: left;
@   vertical-align: bottom;
@   font-weight: bold;
@   color: #333;
@ }
@ 
@ /* The page title centered at the top of each page */
@ div.title {
@   display: table-cell;
@   font-size: 2em;
@   font-weight: bold;
@   text-align: center;
@   color: #333;
@   vertical-align: bottom;
@   width: 100%;
@ }
@ 
@ /* The login status message in the top right-hand corner */
@ div.status {
@   display: table-cell;
@   padding-right: 10px;
@   text-align: right;
@   vertical-align: bottom;
@   padding-bottom: 5px;
@   color: #333;
@   font-size: 0.8em;
@   font-weight: bold;
@ }
@ 
@ /* The header across the top of the page */
@ div.header {
@     margin:10px 0px 10px 0px;
@     padding:1px 0px 0px 20px;
@     border-style:solid;
@     border-color:black;
@     border-width:1px 0px;
@     background-color:#eee;
@ }
@ 
@ /* The main menu bar that appears at the top left of the page beneath
@ ** the header. Width must be co-ordinated with the container below */
@ div.mainmenu {
@   float: left;
@   margin-left: 10px;
@   margin-right: 10px;
@   font-size: 0.9em;
@   font-weight: bold;
@   padding:5px;
@   background-color:#eee;
@   border:1px solid #999;
@   width:8em;
@ }
@ 
@ /* Main menu is now a list */
@ div.mainmenu ul {
@   padding: 0;
@   list-style:none;
@ }
@ div.mainmenu a, div.mainmenu a:visited{
@   padding: 1px 10px 1px 10px;
@   color: #333;
@   text-decoration: none;
@ }
@ div.mainmenu a:hover {
@   color: #eee;
@   background-color: #333;
@ }
@ 
@ /* Container for the sub-menu and content so they don''t spread
@ ** out underneath the main menu */
@ #container {
@   padding-left: 9em;
@ }
@ 
@ /* The submenu bar that *sometimes* appears below the main menu */
@ div.submenu {
@   padding: 3px 10px 3px 10px;
@   font-size: 0.9em;
@   text-align: center;
@   border:1px solid #999;
@   border-width:1px 0px;
@   background-color: #eee;
@   color: #333;
@ }
@ div.submenu a, div.submenu a:visited {
@   padding: 3px 10px 3px 10px;
@   color: #333;
@   text-decoration: none;
@ }
@ div.submenu a:hover {
@   color: #eee;
@   background-color: #333;
@ }
@ 
@ /* All page content from the bottom of the menu or submenu down to
@ ** the footer */
@ div.content {
@   float right;
@   padding: 2ex 1ex 0ex 2ex;
@ }
@ 
@ /* Some pages have section dividers */
@ div.section {
@   margin-bottom: 0px;
@   margin-top: 1em;
@   padding: 1px 1px 1px 1px;
@   font-size: 1.2em;
@   font-weight: bold;
@   border-style:solid;
@   border-color:#999;
@   border-width:1px 0px;
@   background-color: #eee;
@   color: #333;
@ }
@ 
@ /* The "Date" that occurs on the left hand side of timelines */
@ div.divider {
@   background: #eee;
@   border: 2px #999 solid;
@   font-size: 1em; font-weight: normal;
@   padding: .25em;
@   margin: .2em 0 .2em 0;
@   float: left;
@   clear: left;
@   color: #333
@ }
@ 
@ /* The footer at the very bottom of the page */
@ div.footer {
@   font-size: 0.8em;
@   margin-top: 12px;
@   padding: 5px 10px 5px 10px;
@   text-align: right;
@   background-color: #eee;
@   color: #555;
@ }
@ 
@ /* <verbatim> blocks */
@ pre.verbatim {
@    background-color: #f5f5f5;
@    padding: 0.5em;
@ }
@ 
@ /* The label/value pairs on (for example) the ci page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }');
@ REPLACE INTO config VALUES('header','<html>
@ <head>
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$baseurl/timeline.rss">
@ <link rel="stylesheet" href="$baseurl/style.css?black2" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
@   <div class="logo">
@     <!-- <img src="$baseurl/logo" alt="logo"> -->
@     <br><nobr>$<project_name></nobr>
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
@ <div class="mainmenu"><ul><th1>
@ html "<li><a href=''$baseurl$index_page''>Home</a></li>"
@ if {[anycap jor]} {
@   html "<li><a href=''$baseurl/timeline''>Timeline</a></li>"
@ }
@ if {[hascap oh]} {
@   html "<li><a href=''$baseurl/dir''>Files</a></li>"
@ }
@ if {[hascap o]} {
@   html "<li><a href=''$baseurl/leaves''>Leaves</a></li>"
@   html "<li><a href=''$baseurl/brlist''>Branches</a></li>"
@   html "<li><a href=''$baseurl/taglist''>Tags</a></li>"
@ }
@ if {[hascap r]} {
@   html "<li><a href=''$baseurl/reportlist''>Tickets</a></li>"
@ }
@ if {[hascap j]} {
@   html "<li><a href=''$baseurl/wiki''>Wiki</a></li>"
@ }
@ if {[hascap s]} {
@   html "<li><a href=''$baseurl/setup''>Admin</a></li>"
@ } elseif {[hascap a]} {
@   html "<li><a href=''$baseurl/setup_ulist''>Users</a></li>"
@ }
@ if {[info exists login]} {
@   html "<li><a href=''$baseurl/login''>Logout</a></li>"
@ } else {
@   html "<li><a href=''$baseurl/login''>Login</a></li>"
@ }
@ </th1></ul></div>
@ <div id="container">
@ ');
@ REPLACE INTO config VALUES('footer','</div>
@ <div class="footer">
@ Fossil version $manifest_version $manifest_date
@ </div>
@ </body></html>
@ ');
;
/*
** An array of available built-in skins.
*/
static struct BuiltinSkin {
  const char *zName;
  const char *zValue;
} aBuiltinSkin[] = {
  { "Default",                     0 /* Filled in at runtime */ },
  { "Plain Gray, No Logo",         zBuiltinSkin1                },
  { "Khaki, No Logo",              zBuiltinSkin2                },
  { "Black & White, Menu on Left", zBuiltinSkin3                },
};

/*
** For a skin named zSkinName, compute the name of the CONFIG table
** entry where that skin is stored and return it.
**
** Return NULL if zSkinName is NULL or an empty string.
**
** If ifExists is true, and the named skin does not exist, return NULL.
*/
static char *skinVarName(const char *zSkinName, int ifExists){
  char *z;
  if( zSkinName==0 || zSkinName[0]==0 ) return 0;
  z = mprintf("skin:%s", zSkinName);
  if( ifExists && !db_exists("SELECT 1 FROM config WHERE name=%Q", z) ){
    free(z);
    z = 0;
  }
  return z;
}

/*
** Construct and return a string that represents the current skin if
** useDefault==0 or a string for the default skin if useDefault==1.
**
** Memory to hold the returned string is obtained from malloc.
*/
static char *getSkin(int useDefault){
  Blob val;
  blob_zero(&val);
  blob_appendf(&val, "REPLACE INTO config VALUES('css',%Q);\n",
     useDefault ? zDefaultCSS : db_get("css", (char*)zDefaultCSS)
  );
  blob_appendf(&val, "REPLACE INTO config VALUES('header',%Q);\n",
     useDefault ? zDefaultHeader : db_get("header", (char*)zDefaultHeader)
  );
  blob_appendf(&val, "REPLACE INTO config VALUES('footer',%Q);\n",
     useDefault ? zDefaultFooter : db_get("footer", (char*)zDefaultFooter)
  );
  return blob_str(&val);
}

/*
** Construct the default skin string and fill in the corresponding
** entry in aBuildinSkin[]
*/
static void setDefaultSkin(void){
  aBuiltinSkin[0].zValue = getSkin(1);
}

/*
** WEBPAGE: setup_skin
*/
void setup_skin(void){
  const char *z;
  char *zName;
  char *zErr = 0;
  const char *zCurrent;  /* Current skin */
  int i;                 /* Loop counter */
  Stmt q;

  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }
  db_begin_transaction();

  /* Process requests to delete a user-defined skin */
  if( P("del1") && (zName = skinVarName(P("sn"), 1))!=0 ){
    style_header("Confirm Custom Skin Delete");
    @ <form action="%s(g.zBaseURL)/setup_skin" method="POST">
    @ <p>Deletion of a custom skin is a permanent action that cannot
    @ be undone.  Please confirm that this is what you want to do:</p>
    @ <input type="hidden" name="sn" value="%h(P("sn"))">
    @ <input type="submit" name="del2" value="Confirm - Delete The Skin">
    @ <input type="submit" name="cancel" value="Cancel - Do Not Delete">
    login_insert_csrf_secret();
    @ </form>
    style_footer();
    return;
  }
  if( P("del2")!=0 && (zName = skinVarName(P("sn"), 1))!=0 ){
    db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
  }

  setDefaultSkin();
  zCurrent = getSkin(0);

  if( P("save")!=0 && (zName = skinVarName(P("save"),0))!=0 ){
    if( db_exists("SELECT 1 FROM config WHERE name=%Q", zName)
          || strcmp(zName, "Default")==0 ){
      zErr = mprintf("Skin name \"%h\" already exists. "
                     "Choose a different name.", P("sn"));
    }else{
      db_multi_exec("INSERT INTO config VALUES(%Q,%Q)",
         zName, zCurrent
      );
    }
  }

  /* The user pressed the "Use This Skin" button. */
  if( P("load") && (z = P("sn"))!=0 && z[0] ){
    int seen = 0;
    for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
      if( strcmp(aBuiltinSkin[i].zValue, zCurrent)==0 ){
        seen = 1;
        break;
      }
    }
    if( !seen ){
      seen = db_exists("SELECT 1 FROM config WHERE name GLOB 'skin:*'"
                       " AND value=%Q", zCurrent);
    }
    if( !seen ){
      db_multi_exec(
        "INSERT INTO config VALUES("
        "  strftime('skin:Backup On %%Y-%%m-%%d %%H:%%M:%%S'),"
        "  %Q)", zCurrent
      );
    }
    seen = 0;
    for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
      if( strcmp(aBuiltinSkin[i].zName, z)==0 ){
        seen = 1;
        zCurrent = aBuiltinSkin[i].zValue;
        db_multi_exec("%s", zCurrent);
        break;
      }
    }
    if( !seen ){
      zName = skinVarName(z,0);
      zCurrent = db_get(zName, 0);
      db_multi_exec("%s", zCurrent);
    }
  }

  style_header("Skins");
  @ <p>A "skin" is a combination of
  @ <a href="setup_editcss">CSS</a>, 
  @ <a href="setup_header">Header</a>, and 
  @ <a href="setup_footer">Footer</a> that determines the look and feel
  @ of the web interface.</p>
  @
  @ <h2>Available Skins:</h2>
  @ <ol>
  for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
    z = aBuiltinSkin[i].zName;
    if( strcmp(aBuiltinSkin[i].zValue, zCurrent)==0 ){
      @ <li><p>%h(z).&nbsp;&nbsp; <b>Currently In Use</b></p>
    }else{
      @ <li><form action="%s(g.zBaseURL)/setup_skin" method="POST">
      @ %h(z).&nbsp;&nbsp; 
      @ <input type="hidden" name="sn" value="%h(z)">
      @ <input type="submit" name="load" value="Use This Skin">
      @ </form></li>
    }
  }
  db_prepare(&q,
     "SELECT substr(name, 6), value FROM config"
     " WHERE name GLOB 'skin:*'"
     " ORDER BY name"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zN = db_column_text(&q, 0);
    const char *zV = db_column_text(&q, 1);
    if( strcmp(zV, zCurrent)==0 ){
      @ <li><p>%h(zN).&nbsp;&nbsp;  <b>Currently In Use</b></p>
    }else{
      @ <li><form action="%s(g.zBaseURL)/setup_skin" method="POST">
      @ %h(zN).&nbsp;&nbsp; 
      @ <input type="hidden" name="sn" value="%h(zN)">
      @ <input type="submit" name="load" value="Use This Skin">
      @ <input type="submit" name="del1" value="Delete This Skin">
      @ </form></li>
    }
  }
  db_finalize(&q);
  @ </ol>
  style_footer();
  db_end_transaction(0);
}
