/*
** Copyright (c) 2007 D. Richard Hipp
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
** Implementation of the Setup page
*/
#include <assert.h>
#include "config.h"
#include "setup.h"


/*
** Output a single entry for a menu generated using an HTML table.
** If zLink is not NULL or an empty string, then it is the page that
** the menu entry will hyperlink to.  If zLink is NULL or "", then
** the menu entry has no hyperlink - it is disabled.
*/
static void menu_entry(
  const char *zTitle,
  const char *zLink,
  const char *zDesc
){
  @ <dt>
  if( zLink && zLink[0] ){
    @ <a href="%s(zLink)">%h(zTitle)</a>
  }else{
    @ %h(zTitle)
  }
  @ </dt>
  @ <dd>%h(zDesc)</dd>
}

/*
** WEBPAGE: /setup
*/
void setup_page(void){
  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }

  style_header();

  @ <dl id="setup">
  menu_entry("Users", "setup_ulist",
    "Grant privileges to individual users.");
  menu_entry("Access", "setup_access",
    "Control access settings.");
  @ </dl>

  style_footer();
}

/*
** WEBPAGE: setup_ulist
**
** Show a list of users.  Clicking on any user jumps to the edit
** screen for that user.
*/
void setup_ulist(void){
  Stmt s;
  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }

  style_header();

  @ <table border="0" cellpadding="0" cellspacing="0">
  db_prepare(&s, "SELECT uid, login, cap FROM repuser ORDER BY login");
  while( db_step(&s)==SQLITE_ROW ){
    @ <tr><td><a href="%s(g.zBaseURL)/setup_uedit?uid=%d(db_column_int(&s,0))">
    @ %h(db_column_text(&s,1))</a></td><td width="10"></td>
    @ <td>%h(db_column_text(&s,2))</td></tr>
  }
  db_finalize(&s);
  @ </table>
  
  style_footer();
}

/*
** WEBPAGE: setup_uedit
**
** Edit the user with REPUSER.UID equal to the "u" query parameter.
*/
void setup_uedit(void){
  int uid;

  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }
  uid = atoi(PD("u","0"));
  if( uid<=0 ){
    cgi_redirect("setup_ulist");
    assert(0);
  }
  style_header();
  style_footer(); 
}

/*
** Generate a checkbox for an attribute.
*/
static void onoff_attribute(
  const char *zLabel,   /* The text label on the checkbox */
  const char *zVar,     /* The corresponding row in the VAR table */
  const char *zQParm,   /* The query parameter */
  int dfltVal           /* Default value if VAR table entry does not exist */
){
  const char *zVal = db_get(zVar, 0);
  const char *zQ = P(zQParm);
  int iVal;
  if( zVal ){
    iVal = atoi(zVal);
  }else{
    iVal = dfltVal;
  }
  if( zQ==0 && P("submit") ){
    zQ = "off";
  }
  if( zQ ){
    int iQ = strcmp(zQ,"on")==0 || atoi(zQ);
    if( iQ!=iVal ){
      db_set(zVar, iQ ? "1" : "0");
      iVal = iQ;
    }
  }
  if( iVal ){
    @ <input type="checkbox" name="%s(zQParm)" checked>%s(zLabel)</input>
  }else{
    @ <input type="checkbox" name="%s(zQParm)">%s(zLabel)</input>
  }
}

/*
** Generate an entry box for an attribute.
*/
static void entry_attribute(
  const char *zLabel,   /* The text label on the entry box */
  int width,            /* Width of the entry box */
  const char *zVar,     /* The corresponding row in the VAR table */
  const char *zQParm,   /* The query parameter */
  const char *zDflt     /* Default value if VAR table entry does not exist */
){
  const char *zVal = db_get(zVar, zDflt);
  const char *zQ = P(zQParm);
  if( zQ && strcmp(zQ,zVal)!=0 ){
    db_set(zVar, zQ);
    zVal = zQ;
  }
  @ <input type="text" name="%s(zQParm)" value="%h(zVal)" size="%d(width)">
  @ %s(zLabel)
}



/*
** WEBPAGE: setup_access
*/
void setup_access(void){
  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }

  style_header();
  db_begin_transaction();
  @ <form action="%s(g.zBaseURL)/setup_access" method="GET">

  @ <hr>
  onoff_attribute("Require password for local access",
     "authenticate-localhost", "localauth", 1);
  @ <p>When enabled, the password sign-in is required for
  @ web access coming from 127.0.0.1.  When disabled, web access
  @ from 127.0.0.1 is allows without any login - the user id is selected
  @ from the ~/.fossil database. Password login is always required
  @ for incoming web connections on internet addresses other than
  @ 127.0.0.1.</p></li>

  @ <hr>
  entry_attribute("Login expiration time", 6, "cookie-expire", "cex", "8766");
  @ <p>The number of hours for which a login is valid.  This must be a
  @ positive number.  The default is 8760 hours which is approximately equal
  @ to a year.</p>
   
  @ <hr>
  onoff_attribute("Allow anonymous signup", "anon-signup", "asu", 0);
  @ <p>Allow users to create their own accounts</p>
   
  @ <hr>
  @ <p><input type="submit"  name="submit" value="Apply Changes"></p>
  @ </form>
  db_end_transaction(0);
  style_footer();
}
