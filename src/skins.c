/*
** Copyright (c) 2009 D. Richard Hipp
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
** Implementation of the Setup page for "skins".
*/
#include <assert.h>
#include "config.h"
#include "skins.h"

/* @-comment: ## */
/*
** A black-and-white theme with the project title in a bar across the top
** and no logo image.
*/
static const char zBuiltinSkin1[] =
@ REPLACE INTO config(name,mtime,value)
@ VALUES('css',now(),'/* General settings for the entire page */
@ body {
@   margin: 0ex 1ex;
@   padding: 0px;
@   background-color: white;
@   font-family: sans-serif;
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
@   min-width: 200px;
@   white-space: nowrap;
@ }
@
@ /* The page title centered at the top of each page */
@ div.title {
@   display: table-cell;
@   font-size: 1.5em;
@   font-weight: bold;
@   text-align: center;
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
@   background-color: #404040;
@   color: white;
@ }
@
@ /* The submenu bar that *sometimes* appears below the main menu */
@ div.submenu, div.sectionmenu {
@   padding: 3px 10px 3px 0px;
@   font-size: 0.9em;
@   text-align: center;
@   background-color: #606060;
@   color: white;
@ }
@ div.mainmenu a, div.mainmenu a:visited, div.submenu a, div.submenu a:visited,
@ div.sectionmenu>a.button:link, div.sectionmenu>a.button:visited {
@   padding: 3px 10px 3px 10px;
@   color: white;
@   text-decoration: none;
@ }
@ div.mainmenu a:hover, div.submenu a:hover, div.sectionmenu>a.button:hover {
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
@   white-space: nowrap;
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
@   white-space: nowrap;
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
@ REPLACE INTO config(name,mtime,value) VALUES('header',now(),'<html>
@ <head>
@ <base href="$baseurl/$current_page" />
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$home/timeline.rss">
@ <link rel="stylesheet" href="$home/style.css?blackwhite" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
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
@ html "<a href=''$home$index_page''>Home</a>\n"
@ if {[anycap jor]} {
@   html "<a href=''$home/timeline''>Timeline</a>\n"
@ }
@ if {[hascap oh]} {
@   html "<a href=''$home/dir?ci=tip''>Files</a>\n"
@ }
@ if {[hascap o]} {
@   html "<a href=''$home/brlist''>Branches</a>\n"
@   html "<a href=''$home/taglist''>Tags</a>\n"
@ }
@ if {[hascap r]} {
@   html "<a href=''$home/reportlist''>Tickets</a>\n"
@ }
@ if {[hascap j]} {
@   html "<a href=''$home/wiki''>Wiki</a>\n"
@ }
@ if {[hascap s]} {
@   html "<a href=''$home/setup''>Admin</a>\n"
@ } elseif {[hascap a]} {
@   html "<a href=''$home/setup_ulist''>Users</a>\n"
@ }
@ if {[info exists login]} {
@   html "<a href=''$home/login''>Logout</a>\n"
@ } else {
@   html "<a href=''$home/login''>Login</a>\n"
@ }
@ </th1></div>
@ ');
@ REPLACE INTO config(name,mtime,value)
@ VALUES('footer',now(),'<div class="footer">
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
@ REPLACE INTO config(name,mtime,value)
@ VALUES('css',now(),'/* General settings for the entire page */
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
@   white-space: nowrap;
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
@   background-color: #a09048;
@   color: black;
@ }
@
@ /* The submenu bar that *sometimes* appears below the main menu */
@ div.submenu, div.sectionmenu {
@   padding: 3px 10px 3px 0px;
@   font-size: 0.9em;
@   text-align: center;
@   background-color: #c0af58;
@   color: white;
@ }
@ div.mainmenu a, div.mainmenu a:visited, div.submenu a, div.submenu a:visited,
@ div.sectionmenu>a.button:link, div.sectionmenu>a.button:visited {
@   padding: 3px 10px 3px 10px;
@   color: white;
@   text-decoration: none;
@ }
@ div.mainmenu a:hover, div.submenu a:hover, div.sectionmenu>a.button:hover {
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
@   white-space: nowrap;
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
@   white-space: nowrap;
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
@   background-color: #f5f5f5;
@   padding: 0.5em;
@   white-space: pre-wrap;
@ }
@
@ /* The label/value pairs on (for example) the ci page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }');
@ REPLACE INTO config(name,mtime,value) VALUES('header',now(),'<html>
@ <head>
@ <base href="$baseurl/$current_page" />
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$home/timeline.rss">
@ <link rel="stylesheet" href="$home/style.css?tan" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
@   <div class="title">$<title></div>
@   <div class="status">
@     <div class="logo">$<project_name></div><br/>
@     <th1>
@      if {[info exists login]} {
@        puts "Logged in as $login"
@      } else {
@        puts "Not logged in"
@      }
@   </th1></div>
@ </div>
@ <div class="mainmenu">
@ <th1>
@ html "<a href=''$home$index_page''>Home</a>\n"
@ if {[anycap jor]} {
@   html "<a href=''$home/timeline''>Timeline</a>\n"
@ }
@ if {[hascap oh]} {
@   html "<a href=''$home/dir?ci=tip''>Files</a>\n"
@ }
@ if {[hascap o]} {
@   html "<a href=''$home/brlist''>Branches</a>\n"
@   html "<a href=''$home/taglist''>Tags</a>\n"
@ }
@ if {[hascap r]} {
@   html "<a href=''$home/reportlist''>Tickets</a>\n"
@ }
@ if {[hascap j]} {
@   html "<a href=''$home/wiki''>Wiki</a>\n"
@ }
@ if {[hascap s]} {
@   html "<a href=''$home/setup''>Admin</a>\n"
@ } elseif {[hascap a]} {
@   html "<a href=''$home/setup_ulist''>Users</a>\n"
@ }
@ if {[info exists login]} {
@   html "<a href=''$home/login''>Logout</a>\n"
@ } else {
@   html "<a href=''$home/login''>Login</a>\n"
@ }
@ </th1></div>
@ ');
@ REPLACE INTO config(name,mtime,value)
@ VALUES('footer',now(),'<div class="footer">
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
@ REPLACE INTO config(name,mtime,value)
@ VALUES('css',now(),'/* General settings for the entire page */
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
@   white-space: nowrap;
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
@   white-space: nowrap;
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
@ div.submenu, div.sectionmenu {
@   padding: 3px 10px 3px 10px;
@   font-size: 0.9em;
@   text-align: center;
@   border:1px solid #999;
@   border-width:1px 0px;
@   background-color: #eee;
@   color: #333;
@ }
@ div.submenu a, div.submenu a:visited, div.sectionmenu>a.button:link,
@ div.sectionmenu>a.button:visited {
@   padding: 3px 10px 3px 10px;
@   color: #333;
@   text-decoration: none;
@ }
@ div.submenu a:hover, div.sectionmenu>a.button:hover {
@   color: #eee;
@   background-color: #333;
@ }
@
@ /* All page content from the bottom of the menu or submenu down to
@ ** the footer */
@ div.content {
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
@   white-space: nowrap;
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
@   color: #333;
@   white-space: nowrap;
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
@   background-color: #f5f5f5;
@   padding: 0.5em;
@   white-space: pre-wrap;
@ }
@
@ /* The label/value pairs on (for example) the ci page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }');
@ REPLACE INTO config(name,mtime,value) VALUES('header',now(),'<html>
@ <head>
@ <base href="$baseurl/$current_page" />
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$home/timeline.rss">
@ <link rel="stylesheet" href="$home/style.css?black2" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
@   <div class="logo">
@     <img src="$home/logo" alt="logo">
@     <br />$<project_name>
@   </div>
@   <div class="title">$<title></div>
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
@ html "<a href=''$home$index_page''>Home</a>\n"
@ if {[anycap jor]} {
@   html "<a href=''$home/timeline''>Timeline</a>\n"
@ }
@ if {[hascap oh]} {
@   html "<a href=''$home/dir?ci=tip''>Files</a>\n"
@ }
@ if {[hascap o]} {
@   html "<a href=''$home/brlist''>Branches</a>\n"
@   html "<a href=''$home/taglist''>Tags</a>\n"
@ }
@ if {[hascap r]} {
@   html "<a href=''$home/reportlist''>Tickets</a>\n"
@ }
@ if {[hascap j]} {
@   html "<a href=''$home/wiki''>Wiki</a>\n"
@ }
@ if {[hascap s]} {
@   html "<a href=''$home/setup''>Admin</a>\n"
@ } elseif {[hascap a]} {
@   html "<a href=''$home/setup_ulist''>Users</a>\n"
@ }
@ if {[info exists login]} {
@   html "<a href=''$home/login''>Logout</a>\n"
@ } else {
@   html "<a href=''$home/login''>Login</a>\n"
@ }
@ </th1></ul></div>
@ <div id="container">
@ ');
@ REPLACE INTO config(name,mtime,value) VALUES('footer',now(),'</div>
@ <div class="footer">
@ Fossil version $manifest_version $manifest_date
@ </div>
@ </body></html>
@ ');
;


/*
** Shadow boxes and rounded corners.
*/
static const char zBuiltinSkin4[] =
@ REPLACE INTO config(name,mtime,value)
@ VALUES('css',now(),'/* General settings for the entire page */
@ html {
@   min-height: 100%;
@ }
@ body {
@   margin: 0ex 1ex;
@   padding: 0px;
@   background-color: white;
@   color: #333;
@   font-family: Verdana, sans-serif;
@   font-size: 0.8em;
@ }
@
@ /* The project logo in the upper left-hand corner of each page */
@ div.logo {
@   display: table-cell;
@   text-align: right;
@   vertical-align: bottom;
@   font-weight: normal;
@   white-space: nowrap;
@ }
@
@ /* Widths */
@ div.header, div.mainmenu, div.submenu, div.content, div.footer {
@   max-width: 900px;
@   margin: auto;
@   padding: 3px 20px 3px 20px;
@   clear: both;
@ }
@
@ /* The page title at the top of each page */
@ div.title {
@   display: table-cell;
@   padding-left: 10px;
@   font-size: 2em;
@   margin: 10px 0 10px -20px;
@   vertical-align: bottom;
@   text-align: left;
@   width: 80%;
@   font-family: Verdana, sans-serif;
@   font-weight: bold;
@   color: #558195;
@   text-shadow: 0px 2px 2px #999999;
@ }
@
@ /* The login status message in the top right-hand corner */
@ div.status {
@   display: table-cell;
@   text-align: right;
@   vertical-align: bottom;
@   color: #333;
@   margin-right: -20px;
@   white-space: nowrap;
@ }
@
@ /* The main menu bar that appears at the top of the page beneath
@  ** the header */
@ div.mainmenu {
@   text-align: center;
@   color: white;
@   border-top-left-radius: 5px;
@   border-top-right-radius: 5px;
@   vertical-align: middle;
@   padding-top: 8px;
@   padding-bottom: 8px;
@   background-color: #446979;
@   box-shadow: 0px 3px 4px #333333;
@ }
@
@ /* The submenu bar that *sometimes* appears below the main menu */
@ div.submenu {
@   padding-top:10px;
@   padding-bottom:0;
@   text-align: right;
@   color: #000;
@   background-color: #fff;
@   height: 1.5em;
@   vertical-align:middle;
@   box-shadow: 0px 3px 4px #999;
@ }
@ div.mainmenu a, div.mainmenu a:visited {
@   padding: 3px 10px 3px 10px;
@   color: white;
@   text-decoration: none;
@ }
@ div.submenu a, div.submenu a:visited, a.button,
@ div.sectionmenu>a.button:link, div.sectinmenu>a.button:visited {
@   padding: 2px 8px;
@   color: #000;
@   font-family: Arial;
@   text-decoration: none;
@   margin:auto;
@   border-radius: 5px;
@   background-color: #e0e0e0;
@   text-shadow: 0px -1px 0px #eee;
@   border: 1px solid #000;
@ }
@
@ div.mainmenu a:hover {
@   color: #000;
@   background-color: white;
@ }
@
@ div.submenu a:hover, div.sectionmenu>a.button:hover {
@   background-color: #c0c0c0;
@ }
@
@ /* All page content from the bottom of the menu or submenu down to
@  ** the footer */
@ div.content {
@   background-color: #fff;
@   box-shadow: 0px 3px 4px #999;
@   border-bottom-right-radius: 5px;
@   border-bottom-left-radius: 5px;
@   padding-bottom: 1em;
@   min-height:40%;
@ }
@
@
@ /* Some pages have section dividers */
@ div.section {
@   margin-bottom: 0.5em;
@   margin-top: 1em;
@   margin-right: auto;
@   padding: 1px 1px 1px 1px;
@   font-size: 1.2em;
@   font-weight: bold;
@   text-align: center;
@   color: white;
@   border-radius: 5px;
@   background-color: #446979;
@   box-shadow: 0px 3px 4px #333333;
@   white-space: nowrap;
@ }
@
@ /* The "Date" that occurs on the left hand side of timelines */
@ div.divider {
@   font-size: 1.2em;
@   font-family: Georgia, serif;
@   font-weight: bold;
@   margin-top: 1em;
@   white-space: nowrap;
@ }
@
@ /* The footer at the very bottom of the page */
@ div.footer {
@   font-size: 0.9em;
@   text-align: right;
@   margin-bottom: 1em;
@   color: #666;
@ }
@
@ /* Hyperlink colors in the footer */
@ div.footer a { color: white; }
@ div.footer a:link { color: white; }
@ div.footer a:visited { color: white; }
@ div.footer a:hover { background-color: white; color: #558195; }
@
@ /* <verbatim> blocks */
@ pre.verbatim, blockquote pre {
@   font-family: Dejavu Sans Mono, Monaco, Lucida Console, monospace;
@   background-color: #f3f3f3;
@   padding: 0.5em;
@   white-space: pre-wrap;
@ }
@
@ blockquote pre {
@   border: 1px #000 dashed;
@ }
@
@ /* The label/value pairs on (for example) the ci page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }
@
@ table.report tr th {
@   padding: 3px 5px;
@   text-transform: capitalize;
@   cursor: pointer;
@ }
@
@ table.report tr td {
@   padding: 3px 5px;
@   cursor: pointer;
@ }
@
@ textarea {
@   font-size: 1em;
@ }');
@ REPLACE INTO config(name,mtime,value) VALUES('header',now(),'<html>
@ <head>
@ <base href="$baseurl/$current_page" />
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$home/timeline.rss">
@ <link rel="stylesheet" href="$home/style.css?black2" type="text/css"
@       media="screen">
@ </head>
@ <body>
@ <div class="header">
@   <div class="logo">
@     <img src="$home/logo" alt="logo">
@     <br />$<project_name>
@   </div>
@   <div class="title">$<title></div>
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
@ html "<a href=''$home$index_page''>Home</a>\n"
@ if {[anycap jor]} {
@   html "<a href=''$home/timeline''>Timeline</a>\n"
@ }
@ if {[hascap oh]} {
@   html "<a href=''$home/dir?ci=tip''>Files</a>\n"
@ }
@ if {[hascap o]} {
@   html "<a href=''$home/brlist''>Branches</a>\n"
@   html "<a href=''$home/taglist''>Tags</a>\n"
@ }
@ if {[hascap r]} {
@   html "<a href=''$home/reportlist''>Tickets</a>\n"
@ }
@ if {[hascap j]} {
@   html "<a href=''$home/wiki''>Wiki</a>\n"
@ }
@ if {[hascap s]} {
@   html "<a href=''$home/setup''>Admin</a>\n"
@ } elseif {[hascap a]} {
@   html "<a href=''$home/setup_ulist''>Users</a>\n"
@ }
@ if {[info exists login]} {
@   html "<a href=''$home/login''>Logout</a>\n"
@ } else {
@   html "<a href=''$home/login''>Login</a>\n"
@ }
@ </th1></div>
@ <div id="container">
@ ');
@ REPLACE INTO config(name,mtime,value) VALUES('footer',now(),'</div>
@ <div class="footer">
@ Fossil version $manifest_version $manifest_date
@ </div>
@ </body></html>
@ ');
;


/*
** This skin is intended to be almost identical to the default one, with the
** following changes to the header and footer:
**
** 1. The logo image in the header has been modified to be a hyperlink to the
**    root of the web site containing the repository using the same scheme
**    (i.e. HTTP or HTTPS) as the base URL for the repository.  The header
**    contains a TH1 script block to help accomplish these tasks.
**
** 2. The Fossil version information in the footer has been augmented with
**    hyperlinks to the corresponding points on the timeline in the official
**    Fossil repository.  Additionally, if the Tcl integration feature is
**    enabled, the loaded version of Tcl is included, with a hyperlink to the
**    official Tcl/Tk web site.  The footer also contains a TH1 script block
**    to help accomplish these tasks.
*/
static const char zBuiltinSkin5[] =
@ REPLACE INTO config(name,mtime,value)
@ VALUES('css',now(),'/* General settings for the entire page */
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
@ }
@
@ /* The label/value pairs on (for example) the ci page */
@ table.label-value th {
@   vertical-align: top;
@   text-align: right;
@   padding: 0.2ex 2ex;
@ }');
@ REPLACE INTO config(name,mtime,value) VALUES('header',now(),'<html>
@ <head>
@ <base href="$baseurl/$current_page" />
@ <title>$<project_name>: $<title></title>
@ <link rel="alternate" type="application/rss+xml" title="RSS Feed"
@       href="$home/timeline.rss" />
@ <link rel="stylesheet" href="$home/style.css?enhanced" type="text/css"
@       media="screen" />
@ </head>
@ <body>
@ <div class="header">
@   <div class="logo">
@     <th1>
@     ##
@     ## NOTE: The purpose of this procedure is to take the base URL of the
@     ##       Fossil project and return the root of the entire web site using
@     ##       the same URI scheme as the base URL (e.g. http or https).
@     ##
@     proc getLogoUrl { baseurl } {
@       set idx(first) [string first // $baseurl]
@       if {$idx(first) != -1} {
@         ##
@         ## NOTE: Skip second slash.
@         ##
@         set idx(first+1) [expr {$idx(first) + 2}]
@         ##
@         ## NOTE: (part 1) The [string first] command does NOT actually
@         ##       the optional startIndex argument as specified in the
@         ##       TH1 support manual; therefore, we fake it by using the
@         ##       [string range] command and then adding the necessary
@         ##       offset to the resulting index manually (below).  In Tcl,
@         ##       we could use the following instead:
@         ##
@         ##       set idx(next) [string first / $baseurl $idx(first+1)]
@         ##
@         set idx(nextRange) [string range $baseurl $idx(first+1) end]
@         set idx(next) [string first / $idx(nextRange)]
@         if {$idx(next) != -1} {
@           ##
@           ## NOTE: (part 2) Add the necessary offset to the result of the
@           ##       search for the next slash (i.e. the one after the initial
@           ##       search for the two slashes).
@           ##
@           set idx(next) [expr {$idx(next) + $idx(first+1)}]
@           ##
@           ## NOTE: Back up one character from the next slash.
@           ##
@           set idx(next-1) [expr {$idx(next) - 1}]
@           ##
@           ## NOTE: Extract the URI scheme and host from the base URL.
@           ##
@           set scheme [string range $baseurl 0 $idx(first)]
@           set host [string range $baseurl $idx(first+1) $idx(next-1)]
@           ##
@           ## NOTE: Try to stay in SSL mode if we are there now.
@           ##
@           if {[string compare $scheme http:/] == 0} {
@             set scheme http://
@           } else {
@             set scheme https://
@           }
@           set logourl $scheme$host/
@         } else {
@           set logourl $baseurl
@         }
@       } else {
@         set logourl $baseurl
@       }
@       return $logourl
@     }
@     set logourl [getLogoUrl $baseurl]
@     </th1>
@     <a href="$logourl">
@       <img src="$baseurl/logo" border="0" alt="$project_name">
@     </a>
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
@ html "<a href=''$home$index_page''>Home</a>\n"
@ if {[anycap jor]} {
@   html "<a href=''$home/timeline''>Timeline</a>\n"
@ }
@ if {[hascap oh]} {
@   html "<a href=''$home/dir?ci=tip''>Files</a>\n"
@ }
@ if {[hascap o]} {
@   html "<a href=''$home/brlist''>Branches</a>\n"
@   html "<a href=''$home/taglist''>Tags</a>\n"
@ }
@ if {[hascap r]} {
@   html "<a href=''$home/reportlist''>Tickets</a>\n"
@ }
@ if {[hascap j]} {
@   html "<a href=''$home/wiki''>Wiki</a>\n"
@ }
@ if {[hascap s]} {
@   html "<a href=''$home/setup''>Admin</a>\n"
@ } elseif {[hascap a]} {
@   html "<a href=''$home/setup_ulist''>Users</a>\n"
@ }
@ if {[info exists login]} {
@   html "<a href=''$home/login''>Logout</a>\n"
@ } else {
@   html "<a href=''$home/login''>Login</a>\n"
@ }
@ </th1></div>
@ ');
@ REPLACE INTO config(name,mtime,value)
@ VALUES('footer',now(),'<div class="footer">
@   <th1>
@   proc getTclVersion {} {
@     if {[catch {tclEval info patchlevel} tclVersion] == 0} {
@       return "<a href=\"http://www.tcl.tk/\">Tcl</a> version $tclVersion"
@     }
@     return ""
@   }
@   proc getVersion { version } {
@     set length [string length $version]
@     return [string range $version 1 [expr {$length - 2}]]
@   }
@   set version [getVersion $manifest_version]
@   set tclVersion [getTclVersion]
@   set fossilUrl http://www.fossil-scm.org
@   </th1>
@   This page was generated in about
@   <th1>puts [expr {([utime]+[stime]+1000)/1000*0.001}]</th1>s by
@   <a href="$fossilUrl/">Fossil</a>
@   version $release_version $tclVersion
@   <a href="$fossilUrl/index.html/info/$version">$manifest_version</a>
@   <a href="$fossilUrl/fossil/timeline?c=$manifest_date&amp;y=ci">$manifest_date</a>
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
  { "Shadow boxes & Rounded Corners", zBuiltinSkin4             },
  { "Enhanced Default",            zBuiltinSkin5                },
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
  blob_appendf(&val,
     "REPLACE INTO config(name,value,mtime) VALUES('css',%Q,now());\n",
     useDefault ? zDefaultCSS : db_get("css", (char*)zDefaultCSS)
  );
  blob_appendf(&val,
     "REPLACE INTO config(name,value,mtime) VALUES('header',%Q,now());\n",
     useDefault ? zDefaultHeader : db_get("header", (char*)zDefaultHeader)
  );
  blob_appendf(&val,
     "REPLACE INTO config(name,value,mtime) VALUES('footer',%Q,now());\n",
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
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();

  /* Process requests to delete a user-defined skin */
  if( P("del1") && (zName = skinVarName(P("sn"), 1))!=0 ){
    style_header("Confirm Custom Skin Delete");
    @ <form action="%s(g.zTop)/setup_skin" method="post"><div>
    @ <p>Deletion of a custom skin is a permanent action that cannot
    @ be undone.  Please confirm that this is what you want to do:</p>
    @ <input type="hidden" name="sn" value="%h(P("sn"))" />
    @ <input type="submit" name="del2" value="Confirm - Delete The Skin" />
    @ <input type="submit" name="cancel" value="Cancel - Do Not Delete" />
    login_insert_csrf_secret();
    @ </div></form>
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
          || fossil_strcmp(zName, "Default")==0 ){
      zErr = mprintf("Skin name \"%h\" already exists. "
                     "Choose a different name.", P("sn"));
    }else{
      db_multi_exec("INSERT INTO config(name,value,mtime) VALUES(%Q,%Q,now())",
         zName, zCurrent
      );
    }
  }

  /* The user pressed the "Use This Skin" button. */
  if( P("load") && (z = P("sn"))!=0 && z[0] ){
    int seen = 0;
    for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
      if( fossil_strcmp(aBuiltinSkin[i].zValue, zCurrent)==0 ){
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
        "INSERT INTO config(name,value,mtime) VALUES("
        "  strftime('skin:Backup On %%Y-%%m-%%d %%H:%%M:%%S'),"
        "  %Q,now())", zCurrent
      );
    }
    seen = 0;
    for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
      if( fossil_strcmp(aBuiltinSkin[i].zName, z)==0 ){
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
  if( zErr ){
    @ <p><font color="red">%h(zErr)</font></p>
  }
  @ <p>A "skin" is a combination of
  @ <a href="setup_editcss">CSS</a>,
  @ <a href="setup_header">Header</a>,
  @ <a href="setup_footer">Footer</a>, and
  @ <a href="setup_logo">Logo</a> that determines the look and feel
  @ of the web interface.</p>
  @
  @ <h2>Available Skins:</h2>
  @ <ol>
  for(i=0; i<sizeof(aBuiltinSkin)/sizeof(aBuiltinSkin[0]); i++){
    z = aBuiltinSkin[i].zName;
    if( fossil_strcmp(aBuiltinSkin[i].zValue, zCurrent)==0 ){
      @ <li><p>%h(z).&nbsp;&nbsp; <b>Currently In Use</b></p>
    }else{
      @ <li><form action="%s(g.zTop)/setup_skin" method="post"><div>
      @ %h(z).&nbsp;&nbsp;
      @ <input type="hidden" name="sn" value="%h(z)" />
      @ <input type="submit" name="load" value="Use This Skin" />
      @ </div></form></li>
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
    if( fossil_strcmp(zV, zCurrent)==0 ){
      @ <li><p>%h(zN).&nbsp;&nbsp;  <b>Currently In Use</b></p>
    }else{
      @ <li><form action="%s(g.zTop)/setup_skin" method="post">
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
