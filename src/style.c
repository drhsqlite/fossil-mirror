/*
** Copyright (c) 2006 D. Richard Hipp
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
  @ <body bgcolor="white">
  @ <hr size="1">
  @ <table border="0" cellpadding="0" cellspacing="0" width="100%%">
  @ <tr><td valign="top" align="left">
  @ <big><big><b>%s(zTitle)</b></big></big><br>
  if( g.zLogin==0 ){
    @ <small>not logged in</small>
    zLogInOut = "Login";
  }else{
    @ <small>logged in as %h(g.zLogin)</small>
  }
  @ </td><td valign="top" align="right">
  @ <a href="%s(g.zBaseURL)/index">Home</a>
  if( g.okRead ){
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
  if( nSubmenu>0 ){
    int i;
    @ <br>
    qsort(aSubmenu, nSubmenu, sizeof(aSubmenu[0]), submenuCompare);
    for(i=0; i<nSubmenu; i++){
      struct Submenu *p = &aSubmenu[i];
      char *zTail = i<nSubmenu-1 ? " | " : "";
      if( p->zLink==0 ){
        @ <font color="#888888">%h(p->zLabel)</font> %s(zTail)
      }else{
        @ <a href="%T(p->zLink)">%h(p->zLabel)</a> %s(zTail)
      }
    }
  }
  @ </td></tr></table>
  @ <hr size="1">
  g.cgiPanic = 1;
}

/*
** Draw the footer at the bottom of the page.
*/
void style_footer(void){
}

/*
** WEBPAGE: index
** WEBPAGE: home
** WEBPAGE: not_found
*/
void page_index(void){
  style_header("Main Title Page");
  @ This will become the title page
  style_footer();
}

/*
** WEBPAGE: test_env
*/
void page_test_env(void){
  style_header("Environment Test");
  cgi_print_all();
  style_footer();
}
