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
  
  style_footer();
  login_check_credentials();
  if( !g.okWrite || g.isAnon ){
    login_needed();
    return;
  }

  style_submenu_element("Add", "Add User", "setup_uedit");
  style_header();
  @ <h2>List Of Users</h2>
  @ <table cellspacing=0 cellpadding=0 border=0>
  @ <tr>
  @   <th align="right"><nobr>User ID</nobr></th>
  @   <th>&nbsp;&nbsp;&nbsp;Capabilities&nbsp;&nbsp;&nbsp;</th>
  @   <th><nobr>Contact Info</nobr></th>
  @ </tr>
  db_prepare(&s, "SELECT uid, login, cap, info FROM user ORDER BY login");
  while( db_step(&s)==SQLITE_ROW ){
    @ <tr>
    @ <td align="right">
    if( g.okAdmin ){
      @ <a href="setup_uedit?id=%d(db_column_int(&s,0))">
    }
    @ <nobr>%h(db_column_text(&s,1))</nobr>
    if( g.okAdmin ){
      @ </a>
    }
    @ </td>
    @ <td align="center">%s(db_column_text(&s,2))</td>
    @ <td align="center">%s(db_column_text(&s,3))</td>
    @ </tr>
  }
  @ </table>
  @ <p><hr>
  @ <b>Notes:</b>
  @ <ol>
  @ <li><p>The permission flags are as follows:</p>
  @ <table>
  @ <tr><td>a</td><td width="10"></td>
  @     <td>Admin: Create or delete users and ticket report formats</td></tr>
  @ <tr><td>d</td><td></td>
  @     <td>Delete: Erase anonymous wiki, tickets, and attachments</td></tr>
  @ <tr><td>i</td><td></td>
  @     <td>Check-in: Add new code to the repository</td></tr>
  @ <tr><td>j</td><td></td><td>Read-Wiki: View wiki pages</td></tr>
  @ <tr><td>k</td><td></td><td>Wiki: Create or modify wiki pages</td></tr>
  @ <tr><td>n</td><td></td><td>New: Create new tickets</td></tr>
  @ <tr><td>o</td><td></td>
  @     <td>Check-out: Read code out of the repository</td></tr>
  @ <tr><td>p</td><td></td><td>Password: Change password</td></tr>
  @ <tr><td>q</td><td></td><td>Query: Create or edit report formats</td></tr>
  @ <tr><td>r</td><td></td><td>Read: View tickets and change histories</td></tr>
  @ <tr><td>s</td><td></td><td>Setup: Change CVSTrac options</td></tr>
  @ <tr><td>w</td><td></td><td>Write: Edit tickets</td></tr>
  @ </table>
  @ </p></li>
  @
  @ <li><p>
  @ If a user named "<b>anonymous</b>" exists, then anyone can access
  @ the server without having to log in.  The permissions on the
  @ anonymous user determine the access rights for anyone who is not
  @ logged in.
  @ </p></li>
  @
  @ </ol>
  style_footer();
}

/*
** WEBPAGE: /setup_uedit
*/
void user_edit(void){
  const char *zId, *zLogin, *zInfo, *zCap;
  char *oaa, *oas, *oar, *oaw, *oan, *oai, *oaj, *oao, *oap ;
  char *oak, *oad, *oaq;
  int doWrite;
  int uid;
  int higherUser = 0;  /* True if user being edited is SETUP and the */
                       /* user doing the editing is ADMIN.  Disallow editing */

  /* Must have ADMIN privleges to access this page
  */
  login_check_credentials();
  if( !g.okAdmin ){ login_needed(); return; }

  /* Check to see if an ADMIN user is trying to edit a SETUP account.
  ** Don't allow that.
  */
  zId = PD("id", "0");
  uid = atoi(zId);
  if( zId && !g.okSetup && uid>0 ){
    char *zOldCaps;
    zOldCaps = db_text(0, "SELECT caps FROM user WHERE uid=%d",uid);
    higherUser = zOldCaps && strchr(zOldCaps,'s');
  }

  if( P("can") ){
    cgi_redirect("setup_ulist");
    return;
  }

  /* If we have all the necessary information, write the new or
  ** modified user record.  After writing the user record, redirect
  ** to the page that displays a list of users.
  */
  doWrite = cgi_all("login","info","pw") && !higherUser;
  if( doWrite ){
    const char *zPw;
    const char *zLogin;
    char zCap[20];
    int i = 0;
    int aa = P("aa")!=0;
    int ad = P("ad")!=0;
    int ai = P("ai")!=0;
    int aj = P("aj")!=0;
    int ak = P("ak")!=0;
    int an = P("an")!=0;
    int ao = P("ao")!=0;
    int ap = P("ap")!=0;
    int aq = P("aq")!=0;
    int ar = P("ar")!=0;
    int as = g.okSetup && P("as")!=0;
    int aw = P("aw")!=0;
    if( as ) aa = 1;
    if( aa ) ai = aw = ap = 1;
    if( aw ) an = ar = 1;
    if( ai ) ao = 1;
    if( ak ) aj = 1;
    if( aa ){ zCap[i++] = 'a'; }
    if( ad ){ zCap[i++] = 'd'; }
    if( ai ){ zCap[i++] = 'i'; }
    if( aj ){ zCap[i++] = 'j'; }
    if( ak ){ zCap[i++] = 'k'; }
    if( an ){ zCap[i++] = 'n'; }
    if( ao ){ zCap[i++] = 'o'; }
    if( ap ){ zCap[i++] = 'p'; }
    if( aq ){ zCap[i++] = 'q'; }
    if( ar ){ zCap[i++] = 'r'; }
    if( as ){ zCap[i++] = 's'; }
    if( aw ){ zCap[i++] = 'w'; }

    zCap[i] = 0;
    zPw = P("pw");
    if( zPw==0 || zPw[0]==0 ){
      zPw = db_text(0, "SELECT pw FROM user WHERE uid=%d", uid);
    }
    zLogin = P("login");
    if( uid>0 && 
        db_exists("SELECT 1 FROM user WHERE login=%Q AND uid!=%d", zLogin, uid)
    ){
      style_header();
      @ <font color="red">Login "%h(zLogin)" is already used by a different
      @ user.</font>
      @
      @ <p><a href="setup_uedit?id=%d(uid))>[Bummer]</a></p>
      style_footer();
      return;
    }
    db_multi_exec(
       "REPLACE INTO user(uid,login,info,pw,cap) "
       "VALUES(nullif(%d,0),%Q,%Q,%Q,'%s')",
      uid, P("login"), P("info"), zPw, zCap
    );
    cgi_redirect("setup_ulist");
    return;
  }

  /* Load the existing information about the user, if any
  */
  zLogin = "";
  zInfo = "";
  zCap = "";
  oaa = oad = oai = oaj = oak = oan = oao = oap = oaq = oar = oas = oaw = "";
  if( uid ){
    zLogin = db_text("", "SELECT login FROM user WHERE uid=%d", uid);
    zInfo = db_text("", "SELECT info FROM user WHERE uid=%d", uid);
    zCap = db_text("", "SELECT cap FROM user WHERE uid=%d", uid);
    if( strchr(zCap, 'a') ) oaa = " checked";
    if( strchr(zCap, 'd') ) oad = " checked";
    if( strchr(zCap, 'i') ) oai = " checked";
    if( strchr(zCap, 'j') ) oaj = " checked";
    if( strchr(zCap, 'k') ) oak = " checked";
    if( strchr(zCap, 'n') ) oan = " checked";
    if( strchr(zCap, 'o') ) oao = " checked";
    if( strchr(zCap, 'p') ) oap = " checked";
    if( strchr(zCap, 'q') ) oaq = " checked";
    if( strchr(zCap, 'r') ) oar = " checked";
    if( strchr(zCap, 's') ) oas = " checked";
    if( strchr(zCap, 'w') ) oaw = " checked";
  }

  /* Begin generating the page
  */
  style_submenu_element("Cancel", "Cancel", "setup_ulist");
  style_header();
  if( uid ){
    @ <h2>Edit User %h(zLogin)</h2>
  }else{
    @ <h2>Add A New User</h2>
  }
  @ <table align="left" hspace="20" vspace="10"><tr><td>
  @ <form action="%s(g.zPath)" method="POST">
  @ <table>
  @ <tr>
  @   <td align="right"><nobr>User ID:</nobr></td>
  if( uid ){
    @   <td>%d(uid) <input type="hidden" name="id" value="%d(uid)"></td>
  }else{
    @   <td>(new user)<input type="hidden" name="id" value=0></td>
  }
  @ </tr>
  @ <tr>
  @   <td align="right"><nobr>Login:</nobr></td>
  @   <td><input type="text" name="login" value="%h(zLogin)"></td>
  @ </tr>
  @ <tr>
  @   <td align="right"><nobr>Contact&nbsp;Info:</nobr></td>
  @   <td><input type="text" name="info" size=40 value="%h(zInfo)"></td>
  @ </tr>
  @ <tr>
  @   <td align="right" valign="top">Capabilities:</td>
  @   <td>
  @     <input type="checkbox" name="aa"%s(oaa)>Admin</input><br>
  @     <input type="checkbox" name="ad"%s(oad)>Delete</input><br>
  @     <input type="checkbox" name="ai"%s(oai)>Check-In</input><br>
  @     <input type="checkbox" name="aj"%s(oaj)>Read Wiki</input><br>
  @     <input type="checkbox" name="ak"%s(oak)>Write Wiki</input><br>
  @     <input type="checkbox" name="an"%s(oan)>New Tkt</input><br>
  @     <input type="checkbox" name="ao"%s(oao)>Check-Out</input><br>
  @     <input type="checkbox" name="ap"%s(oap)>Password</input><br>
  @     <input type="checkbox" name="aq"%s(oaq)>Query</input><br>
  @     <input type="checkbox" name="ar"%s(oar)>Read</input><br>
  if( g.okSetup ){
    @     <input type="checkbox" name="as"%s(oas)>Setup</input><br>
  }
  @     <input type="checkbox" name="aw"%s(oaw)>Write</input>
  @   </td>
  @ </tr>
  @ <tr>
  @   <td align="right">Password:</td>
  @   <td><input type="password" name="pw" value=""></td>
  @ </tr>
  if( !higherUser ){
    @ <tr>
    @   <td>&nbsp</td>
    @   <td><input type="submit" name="submit" value="Apply Changes">
    @ </tr>
  }
  @ </table></td></tr></table>
  @ <p><b>Notes:</b></p>
  @ <ol>
  if( higherUser ){
    @ <li><p>
    @ User %h(zId) has Setup privileges and you only have Admin privileges
    @ so you are not permitted to make changes to %h(zId).
    @ </p></li>
    @
  }
  @ <li><p>
  @ The <b>Read</b> and <b>Write</b> privileges give the user the ability
  @ to read and write tickets.  The <b>New Tkt</b> capability means that
  @ the user is able to create new tickets.
  @ </p></li>
  @
  @ <li><p>
  @ The <b>Delete</b> privilege give the user the ability to erase
  @ wiki, tickets, and atttachments that have been added by anonymous
  @ users.  This capability is intended for deletion of spam.
  @ </p></li>
  @
  @ <li><p>
  @ The <b>Query</b> privilege allows the user to create or edit
  @ report formats by specifying appropriate SQL.  Users can run 
  @ existing reports without the Query privilege.
  @ </p></li>
  @
  @ <li><p>
  @ An <b>Admin</b> user can add other users, create new ticket report
  @ formats, and change system defaults.  But only the <b>Setup</b> user
  @ is able to change the repository to
  @ which this program is linked.
  @ </p></li>
  @
  if( zId==0 || strcmp(zId,"anonymous")==0 ){
    @ <li><p>
    @ No login is required for user "<b>anonymous</b>".  The capabilities
    @ of this user are available to anyone without supplying a username or
    @ password.  To disable anonymous access, make sure there is no user
    @ with an ID of <b>anonymous</b>.
    @ </p></li>
    @
    @ <li><p>
    @ The password for the "<b>anonymous</b>" user is used for anonymous
    @ access.  The recommended value for the anonymous password
    @ is "anonymous".
    @ </p></li>
  }
  @ </form>
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
