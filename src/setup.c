/*
** Copyright (c) 2007 D. Richard Hipp
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
** Implementation of the Setup page
*/
#include <assert.h>
#include "config.h"
#include "setup.h"

/*
** The table of web pages supported by this application is generated
** automatically by the "mkindex" program and written into a file
** named "page_index.h".  We include that file here to get access
** to the table.
*/
#include "page_index.h"

/*
** Output a single entry for a menu generated using an HTML table.
** If zLink is not NULL or an empty string, then it is the page that
** the menu entry will hyperlink to.  If zLink is NULL or "", then
** the menu entry has no hyperlink - it is disabled.
*/
void setup_menu_entry(
  const char *zTitle,
  const char *zLink,
  const char *zDesc
){
  @ <tr><td valign="top" align="right">
  if( zLink && zLink[0] ){
    @ <a href="%s(zLink)">%h(zTitle)</a>
  }else{
    @ %h(zTitle)
  }
  @ </td><td width="5"></td><td valign="top">%h(zDesc)</td></tr>
}

/*
** WEBPAGE: /setup
*/
void setup_page(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }

  style_header("Server Administration");

  /* Make sure the header contains <base href="...">.   Issue a warning
  ** if it does not. */
  if( !cgi_header_contains("<base href=") ){
    @ <p class="generalError"><b>Configuration Error:</b> Please add
    @ <tt>&lt;base href="$baseurl/$current_page"&gt;</tt> after
    @ <tt>&lt;head&gt;</tt> in the <a href="setup_header">HTML header</a>!</p>
  }

  @ <table border="0" cellspacing="7">
  setup_menu_entry("Users", "setup_ulist",
    "Grant privileges to individual users.");
  setup_menu_entry("Access", "setup_access",
    "Control access settings.");
  setup_menu_entry("Configuration", "setup_config",
    "Configure the WWW components of the repository");
  setup_menu_entry("Settings", "setup_settings",
    "Web interface to the \"fossil settings\" command");
  setup_menu_entry("Timeline", "setup_timeline",
    "Timeline display preferences");
  setup_menu_entry("Login-Group", "setup_login_group",
    "Manage single sign-on between this repository and others"
    " on the same server");
  setup_menu_entry("Tickets", "tktsetup",
    "Configure the trouble-ticketing system for this repository");
  setup_menu_entry("Transfers", "xfersetup",
    "Configure the transfer system for this repository");
  setup_menu_entry("Skins", "setup_skin",
    "Select from a menu of prepackaged \"skins\" for the web interface");
  setup_menu_entry("CSS", "setup_editcss",
    "Edit the Cascading Style Sheet used by all pages of this repository");
  setup_menu_entry("Header", "setup_header",
    "Edit HTML text inserted at the top of every page");
  setup_menu_entry("Footer", "setup_footer",
    "Edit HTML text inserted at the bottom of every page");
  setup_menu_entry("Moderation", "setup_modreq",
    "Enable/Disable requiring moderator approval of Wiki and/or Ticket"
    " changes and attachments.");
  setup_menu_entry("Ad-Unit", "setup_adunit",
    "Edit HTML text for an ad unit inserted after the menu bar");
  setup_menu_entry("Logo", "setup_logo",
    "Change the logo and background images for the server");
  setup_menu_entry("Shunned", "shun",
    "Show artifacts that are shunned by this repository");
  setup_menu_entry("Log", "rcvfromlist",
    "A record of received artifacts and their sources");
  setup_menu_entry("User-Log", "access_log",
    "A record of login attempts");
  setup_menu_entry("Stats", "stat",
    "Display repository statistics");
  setup_menu_entry("SQL", "admin_sql",
    "Enter raw SQL commands");
  setup_menu_entry("TH1", "admin_th1",
    "Enter raw TH1 commands");
  @ </table>

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
  int prevLevel = 0;

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed();
    return;
  }

  style_submenu_element("Add", "Add User", "setup_uedit");
  style_header("User List");
  @ <table class="usetupLayoutTable">
  @ <tr><td class="usetupColumnLayout">
  @ <span class="note">Users:</span>
  @ <table class="usetupUserList">
  prevLevel = 0;
  db_prepare(&s,
     "SELECT uid, login, cap, info, 1 FROM user"
     " WHERE login IN ('anonymous','nobody','developer','reader') "
     " UNION ALL "
     "SELECT uid, login, cap, info, 2 FROM user"
     " WHERE login NOT IN ('anonymous','nobody','developer','reader') "
     "ORDER BY 5, 2"
  );
  while( db_step(&s)==SQLITE_ROW ){
    int iLevel = db_column_int(&s, 4);
    const char *zCap = db_column_text(&s, 2);
    const char *zLogin = db_column_text(&s, 1);
    if( iLevel>prevLevel ){
      if( prevLevel>0 ){
        @ <tr><td colspan="3"><hr></td></tr>
      }
      if( iLevel==1 ){
        @ <tr>
        @   <th class="usetupListUser"
        @    style="text-align: right;padding-right: 20px;">Category</th>
        @   <th class="usetupListCap"
        @    style="text-align: center;padding-right: 15px;">Capabilities</th>
        @   <th class="usetupListCon"
        @    style="text-align: left;">Notes</th>
        @ </tr>
      }else{
        @ <tr>
        @   <th class="usetupListUser"
        @    style="text-align: right;padding-right: 20px;">User&nbsp;ID</th>
        @   <th class="usetupListCap"
        @    style="text-align: center;padding-right: 15px;">Capabilities</th>
        @   <th class="usetupListCon"
        @    style="text-align: left;">Contact&nbsp;Info</th>
        @ </tr>
      }
      prevLevel = iLevel;
    }
    @ <tr>
    @ <td class="usetupListUser"
    @     style="text-align: right;padding-right: 20px;white-space:nowrap;">
    if( g.perm.Admin && (zCap[0]!='s' || g.perm.Setup) ){
      @ <a href="setup_uedit?id=%d(db_column_int(&s,0))">
    }
    @ %h(zLogin)
    if( g.perm.Admin ){
      @ </a>
    }
    @ </td>
    @ <td class="usetupListCap" style="text-align: center;padding-right: 15px;">%s(zCap)</td>
    @ <td  class="usetupListCon"  style="text-align: left;">%h(db_column_text(&s,3))</td>
    @ </tr>
  }
  @ </table>
  @ </td><td class="usetupColumnLayout">
  @ <span class="note">Notes:</span>
  @ <ol>
  @ <li><p>The permission flags are as follows:</p>
  @ <table>
     @ <tr><td valign="top"><b>a</b></td>
     @   <td><i>Admin:</i> Create and delete users</td></tr>
     @ <tr><td valign="top"><b>b</b></td>
     @   <td><i>Attach:</i> Add attachments to wiki or tickets</td></tr>
     @ <tr><td valign="top"><b>c</b></td>
     @   <td><i>Append-Tkt:</i> Append to tickets</td></tr>
     @ <tr><td valign="top"><b>d</b></td>
     @   <td><i>Delete:</i> Delete wiki and tickets</td></tr>
     @ <tr><td valign="top"><b>e</b></td>
     @   <td><i>Email:</i> View sensitive data such as EMail addresses</td></tr>
     @ <tr><td valign="top"><b>f</b></td>
     @   <td><i>New-Wiki:</i> Create new wiki pages</td></tr>
     @ <tr><td valign="top"><b>g</b></td>
     @   <td><i>Clone:</i> Clone the repository</td></tr>
     @ <tr><td valign="top"><b>h</b></td>
     @   <td><i>Hyperlinks:</i> Show hyperlinks to detailed
     @   repository history</td></tr>
     @ <tr><td valign="top"><b>i</b></td>
     @   <td><i>Check-In:</i> Commit new versions in the repository</td></tr>
     @ <tr><td valign="top"><b>j</b></td>
     @   <td><i>Read-Wiki:</i> View wiki pages</td></tr>
     @ <tr><td valign="top"><b>k</b></td>
     @   <td><i>Write-Wiki:</i> Edit wiki pages</td></tr>
     @ <tr><td valign="top"><b>l</b></td>
     @   <td><i>Mod-Wiki:</i> Moderator for wiki pages</td></tr>
     @ <tr><td valign="top"><b>m</b></td>
     @   <td><i>Append-Wiki:</i> Append to wiki pages</td></tr>
     @ <tr><td valign="top"><b>n</b></td>
     @   <td><i>New-Tkt:</i> Create new tickets</td></tr>
     @ <tr><td valign="top"><b>o</b></td>
     @   <td><i>Check-Out:</i> Check out versions</td></tr>
     @ <tr><td valign="top"><b>p</b></td>
     @   <td><i>Password:</i> Change your own password</td></tr>
     @ <tr><td valign="top"><b>q</b></td>
     @   <td><i>Mod-Tkt:</i> Moderator for tickets</td></tr>
     @ <tr><td valign="top"><b>r</b></td>
     @   <td><i>Read-Tkt:</i> View tickets</td></tr>
     @ <tr><td valign="top"><b>s</b></td>
     @   <td><i>Setup/Super-user:</i> Setup and configure this website</td></tr>
     @ <tr><td valign="top"><b>t</b></td>
     @   <td><i>Tkt-Report:</i> Create new bug summary reports</td></tr>
     @ <tr><td valign="top"><b>u</b></td>
     @   <td><i>Reader:</i> Inherit privileges of
     @   user <tt>reader</tt></td></tr>
     @ <tr><td valign="top"><b>v</b></td>
     @   <td><i>Developer:</i> Inherit privileges of
     @   user <tt>developer</tt></td></tr>
     @ <tr><td valign="top"><b>w</b></td>
     @   <td><i>Write-Tkt:</i> Edit tickets</td></tr>
     @ <tr><td valign="top"><b>x</b></td>
     @   <td><i>Private:</i> Push and/or pull private branches</td></tr>
     @ <tr><td valign="top"><b>z</b></td>
     @   <td><i>Zip download:</i> Download a baseline via the
     @   <tt>/zip</tt> URL even without
     @    check<span class="capability">o</span>ut
     @    and <span class="capability">h</span>istory permissions</td></tr>
  @ </table>
  @ </li>
  @
  @ <li><p>
  @ Every user, logged in or not, inherits the privileges of
  @ <span class="usertype">nobody</span>.
  @ </p></li>
  @
  @ <li><p>
  @ Any human can login as <span class="usertype">anonymous</span> since the
  @ password is clearly displayed on the login page for them to type. The
  @ purpose of requiring anonymous to log in is to prevent access by spiders.
  @ Every logged-in user inherits the combined privileges of
  @ <span class="usertype">anonymous</span> and
  @ <span class="usertype">nobody</span>.
  @ </p></li>
  @
  @ <li><p>
  @ Users with privilege <span class="capability">v</span> inherit the combined
  @ privileges of <span class="usertype">developer</span>,
  @ <span class="usertype">anonymous</span>, and
  @ <span class="usertype">nobody</span>.
  @ </p></li>
  @
  @ </ol>
  @ </td></tr></table>
  style_footer();
}

/*
** Return true if zPw is a valid password string.  A valid
** password string is:
**
**  (1)  A zero-length string, or
**  (2)  a string that contains a character other than '*'.
*/
static int isValidPwString(const char *zPw){
  if( zPw==0 ) return 0;
  if( zPw[0]==0 ) return 1;
  while( zPw[0]=='*' ){ zPw++; }
  return zPw[0]!=0;
}

/*
** WEBPAGE: /setup_uedit
*/
void user_edit(void){
  const char *zId, *zLogin, *zInfo, *zCap, *zPw;
  const char *zGroup;
  const char *zOldLogin;
  int doWrite;
  int uid, i;
  int higherUser = 0;  /* True if user being edited is SETUP and the */
                       /* user doing the editing is ADMIN.  Disallow editing */
  char *inherit[128];
  int a[128];
  char *oa[128];

  /* Must have ADMIN privileges to access this page
  */
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(); return; }

  /* Check to see if an ADMIN user is trying to edit a SETUP account.
  ** Don't allow that.
  */
  zId = PD("id", "0");
  uid = atoi(zId);
  if( zId && !g.perm.Setup && uid>0 ){
    char *zOldCaps;
    zOldCaps = db_text(0, "SELECT cap FROM user WHERE uid=%d",uid);
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
    char c;
    char zCap[50], zNm[4];
    zNm[0] = 'a';
    zNm[2] = 0;
    for(i=0, c='a'; c<='z'; c++){
      zNm[1] = c;
      a[c&0x7f] = (c!='s' || g.perm.Setup) && P(zNm)!=0;
      if( a[c&0x7f] ) zCap[i++] = c;
    }

    zCap[i] = 0;
    zPw = P("pw");
    zLogin = P("login");
    if( strlen(zLogin)==0 ){
      style_header("User Creation Error");
      @ <span class="loginError">Empty login not allowed.</span>
      @
      @ <p><a href="setup_uedit?id=%d(uid)">[Bummer]</a></p>
      style_footer();
      return;
    }
    if( isValidPwString(zPw) ){
      zPw = sha1_shared_secret(zPw, zLogin, 0);
    }else{
      zPw = db_text(0, "SELECT pw FROM user WHERE uid=%d", uid);
    }
    zOldLogin = db_text(0, "SELECT login FROM user WHERE uid=%d", uid);
    if( uid>0 &&
        db_exists("SELECT 1 FROM user WHERE login=%Q AND uid!=%d", zLogin, uid)
    ){
      style_header("User Creation Error");
      @ <span class="loginError">Login "%h(zLogin)" is already used by
      @ a different user.</span>
      @
      @ <p><a href="setup_uedit?id=%d(uid)">[Bummer]</a></p>
      style_footer();
      return;
    }
    login_verify_csrf_secret();
    db_multi_exec(
       "REPLACE INTO user(uid,login,info,pw,cap,mtime) "
       "VALUES(nullif(%d,0),%Q,%Q,%Q,'%s',now())",
      uid, P("login"), P("info"), zPw, zCap
    );
    if( atoi(PD("all","0"))>0 ){
      Blob sql;
      char *zErr = 0;
      blob_zero(&sql);
      if( zOldLogin==0 ){
        blob_appendf(&sql,
          "INSERT INTO user(login)"
          "  SELECT %Q WHERE NOT EXISTS(SELECT 1 FROM user WHERE login=%Q);",
          zLogin, zLogin
        );
        zOldLogin = zLogin;
      }
      blob_appendf(&sql,
        "UPDATE user SET login=%Q,"
        "  pw=coalesce(shared_secret(%Q,%Q,"
                "(SELECT value FROM config WHERE name='project-code')),pw),"
        "  info=%Q,"
        "  cap=%Q,"
        "  mtime=now()"
        " WHERE login=%Q;",
        zLogin, P("pw"), zLogin, P("info"), zCap,
        zOldLogin
      );
      login_group_sql(blob_str(&sql), "<li> ", " </li>\n", &zErr);
      blob_reset(&sql);
      if( zErr ){
        style_header("User Change Error");
        @ <span class="loginError">%s(zErr)</span>
        @
        @ <p><a href="setup_uedit?id=%d(uid)">[Bummer]</a></p>
        style_footer();
        return;
      }
    }
    cgi_redirect("setup_ulist");
    return;
  }

  /* Load the existing information about the user, if any
  */
  zLogin = "";
  zInfo = "";
  zCap = "";
  zPw = "";
  for(i='a'; i<='z'; i++) oa[i] = "";
  if( uid ){
    zLogin = db_text("", "SELECT login FROM user WHERE uid=%d", uid);
    zInfo = db_text("", "SELECT info FROM user WHERE uid=%d", uid);
    zCap = db_text("", "SELECT cap FROM user WHERE uid=%d", uid);
    zPw = db_text("", "SELECT pw FROM user WHERE uid=%d", uid);
    for(i=0; zCap[i]; i++){
      char c = zCap[i];
      if( c>='a' && c<='z' ) oa[c&0x7f] = " checked=\"checked\"";
    }
  }

  /* figure out inherited permissions */
  memset(inherit, 0, sizeof(inherit));
  if( fossil_strcmp(zLogin, "developer") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='developer'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
         "<span class=\"ueditInheritDeveloper\">&bull;</span>";
    }
    free(z2);
  }
  if( fossil_strcmp(zLogin, "reader") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='reader'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
          "<span class=\"ueditInheritReader\">&bull;</span>";
    }
    free(z2);
  }
  if( fossil_strcmp(zLogin, "anonymous") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='anonymous'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
           "<span class=\"ueditInheritAnonymous\">&bull;</span>";
    }
    free(z2);
  }
  if( fossil_strcmp(zLogin, "nobody") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='nobody'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
           "<span class=\"ueditInheritNobody\">&bull;</span>";
    }
    free(z2);
  }

  /* Begin generating the page
  */
  style_submenu_element("Cancel", "Cancel", "setup_ulist");
  if( uid ){
    style_header(mprintf("Edit User %h", zLogin));
  }else{
    style_header("Add A New User");
  }
  @ <div class="ueditCapBox">
  @ <form action="%s(g.zPath)" method="post"><div>
  login_insert_csrf_secret();
  @ <table>
  @ <tr>
  @   <td class="usetupEditLabel">User ID:</td>
  if( uid ){
    @   <td>%d(uid) <input type="hidden" name="id" value="%d(uid)" /></td>
  }else{
    @   <td>(new user)<input type="hidden" name="id" value="0" /></td>
  }
  @ </tr>
  @ <tr>
  @   <td class="usetupEditLabel">Login:</td>
  @   <td><input type="text" name="login" value="%h(zLogin)" /></td>
  @ </tr>
  @ <tr>
  @   <td class="usetupEditLabel">Contact&nbsp;Info:</td>
  @   <td><input type="text" name="info" size="40" value="%h(zInfo)" /></td>
  @ </tr>
  @ <tr>
  @   <td class="usetupEditLabel">Capabilities:</td>
  @   <td>
#define B(x) inherit[x]
  @ <table border=0><tr><td valign="top">
  if( g.perm.Setup ){
    @  <label><input type="checkbox" name="as"%s(oa['s']) />%s(B('s'))Setup
    @  </label><br />
  }
  @  <label><input type="checkbox" name="aa"%s(oa['a']) />%s(B('a'))Admin
  @  </label><br />
  @  <label><input type="checkbox" name="ad"%s(oa['d']) />%s(B('d'))Delete
  @  </label><br />
  @  <label><input type="checkbox" name="ae"%s(oa['e']) />%s(B('e'))Email
  @  </label><br />
  @  <label><input type="checkbox" name="ap"%s(oa['p']) />%s(B('p'))Password
  @  </label><br />
  @  <label><input type="checkbox" name="ai"%s(oa['i']) />%s(B('i'))Check-In
  @  </label><br />
  @  <label><input type="checkbox" name="ao"%s(oa['o']) />%s(B('o'))Check-Out
  @  </label><br />
  @  <label><input type="checkbox" name="ah"%s(oa['h']) />%s(B('h'))Hyperlinks
  @  </label><br />
  @  <label><input type="checkbox" name="ab"%s(oa['b']) />%s(B('b'))Attachments
  @  </label><br />
  @ </td><td><td width="40"></td><td valign="top">
  @  <label><input type="checkbox" name="au"%s(oa['u']) />%s(B('u'))Reader
  @  </label><br />
  @  <label><input type="checkbox" name="av"%s(oa['v']) />%s(B('v'))Developer
  @  </label><br />
  @  <label><input type="checkbox" name="ag"%s(oa['g']) />%s(B('g'))Clone
  @  </label><br />
  @  <label><input type="checkbox" name="aj"%s(oa['j']) />%s(B('j'))Read Wiki
  @  </label><br />
  @  <label><input type="checkbox" name="af"%s(oa['f']) />%s(B('f'))New Wiki
  @  </label><br />
  @  <label><input type="checkbox" name="am"%s(oa['m']) />%s(B('m'))Append Wiki
  @  </label><br />
  @  <label><input type="checkbox" name="ak"%s(oa['k']) />%s(B('k'))Write Wiki
  @  </label><br />
  @  <label><input type="checkbox" name="al"%s(oa['l']) />%s(B('l'))Moderate
  @  Wiki</label><br />
  @ </td><td><td width="40"></td><td valign="top">
  @  <label><input type="checkbox" name="ar"%s(oa['r']) />%s(B('r'))Read Ticket
  @  </label><br />
  @  <label><input type="checkbox" name="an"%s(oa['n']) />%s(B('n'))New Tickets
  @  </label><br />
  @  <label><input type="checkbox" name="ac"%s(oa['c']) />%s(B('c'))Append
  @  To Ticket </label><br />
  @  <label><input type="checkbox" name="aw"%s(oa['w']) />%s(B('w'))Write
  @  Tickets </label><br />
  @  <label><input type="checkbox" name="aq"%s(oa['q']) />%s(B('q'))Moderate
  @  Tickets </label><br />
  @  <label><input type="checkbox" name="at"%s(oa['t']) />%s(B('t'))Ticket
  @  Report </label><br />
  @  <label><input type="checkbox" name="ax"%s(oa['x']) />%s(B('x'))Private
  @  </label><br />
  @  <label><input type="checkbox" name="az"%s(oa['z']) />%s(B('z'))Download
  @  Zip </label>
  @ </td></tr></table>
  @   </td>
  @ </tr>
  @ <tr>
  @   <td align="right">Password:</td>
  if( zPw[0] ){
    /* Obscure the password for all users */
    @   <td><input type="password" name="pw" value="**********" /></td>
  }else{
    /* Show an empty password as an empty input field */
    @   <td><input type="password" name="pw" value="" /></td>
  }
  @ </tr>
  zGroup = login_group_name();
  if( zGroup ){
    @ <tr>
    @ <td valign="top" align="right">Scope:</td>
    @ <td valign="top">
    @ <input type="radio" name="all" checked value="0">
    @ Apply changes to this repository only.<br />
    @ <input type="radio" name="all" value="1">
    @ Apply changes to all repositories in the "<b>%h(zGroup)</b>"
    @ login group.</td></tr>
  }
  if( !higherUser ){
    @ <tr>
    @   <td>&nbsp;</td>
    @   <td><input type="submit" name="submit" value="Apply Changes" /></td>
    @ </tr>
  }
  @ </table>
  @ </div></form>
  @ </div>
  @ <h2>Privileges And Capabilities:</h2>
  @ <ul>
  if( higherUser ){
    @ <li><p class=missingPriv">
    @ User %h(zLogin) has Setup privileges and you only have Admin privileges
    @ so you are not permitted to make changes to %h(zLogin).
    @ </p></li>
    @
  }
  @ <li><p>
  @ The <span class="capability">Setup</span> user can make arbitrary
  @ configuration changes. An <span class="usertype">Admin</span> user
  @ can add other users and change user privileges
  @ and reset user passwords.  Both automatically get all other privileges
  @ listed below.  Use these two settings with discretion.
  @ </p></li>
  @
  @ <li><p>
  @ The "<span class="ueditInheritNobody"><big>&bull;</big></span>" mark
  @ indicates the privileges of <span class="usertype">nobody</span> that
  @ are available to all users regardless of whether or not they are logged in.
  @ </p></li>
  @
  @ <li><p>
  @ The "<span class="ueditInheritAnonymous"><big>&bull;</big></span>" mark
  @ indicates the privileges of <span class="usertype">anonymous</span> that
  @ are inherited by all logged-in users.
  @ </p></li>
  @
  @ <li><p>
  @ The "<span class="ueditInheritDeveloper"><big>&bull;</big></span>" mark
  @ indicates the privileges of <span class="usertype">developer</span> that
  @ are inherited by all users with the
  @ <span class="capability">Developer</span> privilege.
  @ </p></li>
  @
  @ <li><p>
  @ The "<span class="ueditInheritReader"><big>&bull;</big></span>" mark
  @ indicates the privileges of <span class="usertype">reader</span> that
  @ are inherited by all users with the <span class="capability">Reader</span>
  @ privilege.
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="capability">Delete</span> privilege give the user the
  @ ability to erase wiki, tickets, and attachments that have been added
  @ by anonymous users.  This capability is intended for deletion of spam.
  @ The delete capability is only in effect for 24 hours after the item
  @ is first posted.  The <span class="usertype">Setup</span> user can
  @ delete anything at any time.
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="capability">Hyperlinks</span> privilege allows a user
  @ to see most hyperlinks. This is recommended ON for most logged-in users
  @ but OFF for user "nobody" to avoid problems with spiders trying to walk
  @ every diff and annotation of every historical check-in and file.
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="capability">Zip</span> privilege allows a user to
  @ see the "download as ZIP"
  @ hyperlink and permits access to the <tt>/zip</tt> page.  This allows
  @ users to download ZIP archives without granting other rights like
  @ <span class="capability">Read</span> or
  @ <span class="capability">Hyperlink</span>.  The "z" privilege is recommended
  @ for user <span class="usertype">nobody</span> so that automatic package
  @ downloaders can obtain the sources without going through the login
  @ procedure.
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="capability">Check-in</span> privilege allows remote
  @ users to "push". The <span class="capability">Check-out</span> privilege
  @ allows remote users to "pull". The <span class="capability">Clone</span>
  @ privilege allows remote users to "clone".
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="capability">Read Wiki</span>,
  @ <span class="capability">New Wiki</span>,
  @ <span class="capability">Append Wiki</span>, and
  @ <b>Write Wiki</b> privileges control access to wiki pages.  The
  @ <span class="capability">Read Ticket</span>,
  @ <span class="capability">New Ticket</span>,
  @ <span class="capability">Append Ticket</span>, and
  @ <span class="capability">Write Ticket</span> privileges control access
  @ to trouble tickets.
  @ The <span class="capability">Ticket Report</span> privilege allows
  @ the user to create or edit ticket report formats.
  @ </p></li>
  @
  @ <li><p>
  @ Users with the <span class="capability">Password</span> privilege
  @ are allowed to change their own password.  Recommended ON for most
  @ users but OFF for special users <span class="usertype">developer</span>,
  @ <span class="usertype">anonymous</span>,
  @ and <span class="usertype">nobody</span>.
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="capability">EMail</span> privilege allows the display of
  @ sensitive information such as the email address of users and contact
  @ information on tickets. Recommended OFF for
  @ <span class="usertype">anonymous</span> and for
  @ <span class="usertype">nobody</span> but ON for
  @ <span class="usertype">developer</span>.
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="capability">Attachment</span> privilege is needed in
  @ order to add attachments to tickets or wiki.  Write privilege on the
  @ ticket or wiki is also required.
  @ </p></li>
  @
  @ <li><p>
  @ Login is prohibited if the password is an empty string.
  @ </p></li>
  @ </ul>
  @
  @ <h2>Special Logins</h2>
  @
  @ <ul>
  @ <li><p>
  @ No login is required for user <span class="usertype">nobody</span>. The
  @ capabilities of the <span class="usertype">nobody</span> user are
  @ inherited by all users, regardless of whether or not they are logged in.
  @ To disable universal access to the repository, make sure no user named
  @ <span class="usertype">nobody</span> exists or that the
  @ <span class="usertype">nobody</span> user has no capabilities
  @ enabled. The password for <span class="usertype">nobody</span> is ignore.
  @ To avoid problems with spiders overloading the server, it is recommended
  @ that the <span class="capability">h</span> (Hyperlinks) capability be
  @ turned off for the <span class="usertype">nobody</span> user.
  @ </p></li>
  @
  @ <li><p>
  @ Login is required for user <span class="usertype">anonymous</span> but the
  @ password is displayed on the login screen beside the password entry box
  @ so anybody who can read should be able to login as anonymous.
  @ On the other hand, spiders and web-crawlers will typically not
  @ be able to login.  Set the capabilities of the
  @ <span class="usertype">anonymous</span>
  @ user to things that you want any human to be able to do, but not any
  @ spider.  Every other logged-in user inherits the privileges of
  @ <span class="usertype">anonymous</span>.
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="usertype">developer</span> user is intended as a template
  @ for trusted users with check-in privileges. When adding new trusted users,
  @ simply select the <span class="capability">developer</span> privilege to
  @ cause the new user to inherit all privileges of the
  @ <span class="usertype">developer</span>
  @ user.  Similarly, the <span class="usertype">reader</span> user is a
  @ template for users who are allowed more access than
  @ <span class="usertype">anonymous</span>,
  @ but less than a <span class="usertype">developer</span>.
  @ </p></li>
  @ </ul>
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
  const char *zQ = P(zQParm);
  int iVal = db_get_boolean(zVar, dfltVal);
  if( zQ==0 && P("submit") ){
    zQ = "off";
  }
  if( zQ ){
    int iQ = fossil_strcmp(zQ,"on")==0 || atoi(zQ);
    if( iQ!=iVal ){
      login_verify_csrf_secret();
      db_set(zVar, iQ ? "1" : "0", 0);
      iVal = iQ;
    }
  }
  if( iVal ){
    @ <input type="checkbox" name="%s(zQParm)" checked="checked" />
    @ <b>%s(zLabel)</b>
  }else{
    @ <input type="checkbox" name="%s(zQParm)" /> <b>%s(zLabel)</b>
  }
}

/*
** Generate an entry box for an attribute.
*/
void entry_attribute(
  const char *zLabel,   /* The text label on the entry box */
  int width,            /* Width of the entry box */
  const char *zVar,     /* The corresponding row in the VAR table */
  const char *zQParm,   /* The query parameter */
  char *zDflt     /* Default value if VAR table entry does not exist */
){
  const char *zVal = db_get(zVar, zDflt);
  const char *zQ = P(zQParm);
  if( zQ && fossil_strcmp(zQ,zVal)!=0 ){
    login_verify_csrf_secret();
    db_set(zVar, zQ, 0);
    zVal = zQ;
  }
  @ <input type="text" name="%s(zQParm)" value="%h(zVal)" size="%d(width)" />
  @ <b>%s(zLabel)</b>
}

/*
** Generate a text box for an attribute.
*/
static void textarea_attribute(
  const char *zLabel,   /* The text label on the textarea */
  int rows,             /* Rows in the textarea */
  int cols,             /* Columns in the textarea */
  const char *zVar,     /* The corresponding row in the VAR table */
  const char *zQP,      /* The query parameter */
  const char *zDflt     /* Default value if VAR table entry does not exist */
){
  const char *z = db_get(zVar, (char*)zDflt);
  const char *zQ = P(zQP);
  if( zQ && fossil_strcmp(zQ,z)!=0 ){
    login_verify_csrf_secret();
    db_set(zVar, zQ, 0);
    z = zQ;
  }
  if( rows>0 && cols>0 ){
    @ <textarea id="id%s(zQP)" name="%s(zQP)" rows="%d(rows)"
    @ cols="%d(cols)">%h(z)</textarea>
    if( zLabel && *zLabel ){
      @ <span class="textareaLabel">%s(zLabel)</span>
    }
  }
}


/*
** WEBPAGE: setup_access
*/
void setup_access(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }

  style_header("Access Control Settings");
  db_begin_transaction();
  @ <form action="%s(g.zTop)/setup_access" method="post"><div>
  login_insert_csrf_secret();
  @ <hr />
  onoff_attribute("Require password for local access",
     "localauth", "localauth", 0);
  @ <p>When enabled, the password sign-in is always required for
  @ web access.  When disabled, unrestricted web access from 127.0.0.1
  @ is allowed for the <a href="%s(g.zTop)/help/ui">fossil ui</a> command or
  @ from the <a href="%s(g.zTop)/help/server">fossil server</a>,
  @ <a href="%s(g.zTop)/help/http">fossil http</a> commands when the
  @ "--localauth" command line options is used, or from the
  @ <a href="%s(g.zTop)/help/cgi">fossil cgi</a> if a line containing
  @ the word "localauth" appears in the CGI script.
  @
  @ <p>A password is always required if any one or more
  @ of the following are true:
  @ <ol>
  @ <li> This button is checked
  @ <li> The inbound TCP/IP connection is not from 127.0.0.1
  @ <li> The server is started using either of the
  @ <a href="%s(g.zTop)/help/server">fossil server</a> or
  @ <a href="%s(g.zTop)/help/server">fossil http</a> commands
  @ without the "--localauth" option.
  @ <li> The server is started from CGI without the "localauth" keyword
  @ in the CGI script.
  @ </ol>
  @
  @ <hr />
  onoff_attribute("Enable /test_env",
     "test_env_enable", "test_env_enable", 0);
  @ <p>When enabled, the %h(g.zBaseURL)/test_env URL is available to all
  @ users.  When disabled (the default) only users Admin and Setup can visit
  @ the /test_env page.
  @ </p>
  @
  @ <hr />
  onoff_attribute("Allow REMOTE_USER authentication",
     "remote_user_ok", "remote_user_ok", 0);
  @ <p>When enabled, if the REMOTE_USER environment variable is set to the
  @ login name of a valid user and no other login credentials are available,
  @ then the REMOTE_USER is accepted as an authenticated user.
  @ </p>
  @
  @ <hr />
  entry_attribute("IP address terms used in login cookie", 3,
                  "ip-prefix-terms", "ipt", "2");
  @ <p>The number of octets of of the IP address used in the login cookie.
  @ Set to zero to omit the IP address from the login cookie.  A value of
  @ 2 is recommended.
  @ </p>
  @
  @ <hr />
  entry_attribute("Login expiration time", 6, "cookie-expire", "cex", "8766");
  @ <p>The number of hours for which a login is valid.  This must be a
  @ positive number.  The default is 8766 hours which is approximately equal
  @ to a year.</p>

  @ <hr />
  entry_attribute("Download packet limit", 10, "max-download", "mxdwn",
                  "5000000");
  @ <p>Fossil tries to limit out-bound sync, clone, and pull packets
  @ to this many bytes, uncompressed.  If the client requires more data
  @ than this, then the client will issue multiple HTTP requests.
  @ Values below 1 million are not recommended.  5 million is a
  @ reasonable number.</p>

  @ <hr />
  entry_attribute("Download time limit", 11, "max-download-time", "mxdwnt",
                  "30");

  @ <p>Fossil tries to spend less than this many seconds gathering
  @ the out-bound data of sync, clone, and pull packets.
  @ If the client request takes longer, a partial reply is given similar
  @ to the download packet limit. 30s is a reasonable default.</p>

  @ <hr />
  onoff_attribute(
      "Enable hyperlinks for \"nobody\" based on User-Agent and Javascript",
      "auto-hyperlink", "autohyperlink", 1);
  @ <p>Enable hyperlinks (the equivalent of the "h" permission) for all users
  @ including user "nobody", as long as (1) the User-Agent string in the
  @ HTTP header indicates that the request is coming from an actual human
  @ being and not a a robot or spider and (2) the user agent is able to
  @ run Javascript in order to set the href= attribute of hyperlinks.  Bots
  @ and spiders can forge a User-Agent string that makes them seem to be a
  @ normal browser and they can run javascript just like browsers.  But most 
  @ bots do not go to that much trouble so this is normally an effective defense.</p>
  @
  @ <p>You do not normally want a bot to walk your entire repository because
  @ if it does, your server will end up computing diffs and annotations for
  @ every historical version of every file and creating ZIPs and tarballs of
  @ every historical check-in, which can use a lot of CPU and bandwidth
  @ even for relatively small projects.</p>
  @ 
  @ <p>Additional parameters that control this behavior:</p>
  @ <blockquote>
  onoff_attribute("Require mouse movement before enabling hyperlinks",
                  "auto-hyperlink-mouseover", "ahmo", 0);
  @ <br>
  entry_attribute("Delay before enabling hyperlinks (milliseconds)", 5,
                  "auto-hyperlink-delay", "ah-delay", "0");
  @ </blockquote>
  @ <p>Hyperlinks for user "nobody" are normally enabled as soon as the page
  @ finishes loading.  But the first check-box below can be set to require mouse
  @ movement before enabling the links. One can also set a delay prior to enabling
  @ links by enter a positive number of milliseconds in the entry box above.</p>

  @ <hr />
  onoff_attribute("Require a CAPTCHA if not logged in",
                  "require-captcha", "reqcapt", 1);
  @ <p>Require a CAPTCHA for edit operations (appending, creating, or
  @ editing wiki or tickets or adding attachments to wiki or tickets)
  @ for users who are not logged in.</p>

  @ <hr />
  entry_attribute("Public pages", 30, "public-pages",
                  "pubpage", "");
  @ <p>A comma-separated list of glob patterns for pages that are accessible
  @ without needing a login and using the privileges given by the
  @ "Default privileges" setting below.  Example use case: Set this field
  @ to "/doc/trunk/www/*" to give anonymous users read-only permission to the
  @ latest version of the embedded documentation in the www/ folder without
  @ allowing them to see the rest of the source code.
  @ </p>

  @ <hr />
  onoff_attribute("Allow users to register themselves",
                  "self-register", "selfregister", 0);
  @ <p>Allow users to register themselves through the HTTP UI.
  @ The registration form always requires filling in a CAPTCHA
  @ (<em>auto-captcha</em> setting is ignored). Still, bear in mind that anyone
  @ can register under any user name. This option is useful for public projects
  @ where you do not want everyone in any ticket discussion to be named
  @ "Anonymous".</p>

  @ <hr />
  entry_attribute("Default privileges", 10, "default-perms",
                  "defaultperms", "u");
  @ <p>Permissions given to users that... <ul><li>register themselves using
  @ the self-registration procedure (if enabled), or <li>access "public"
  @ pages identified by the public-pages glob pattern above, or <li>
  @ are users newly created by the administrator.</ul>
  @ </p>

  @ <hr />
  onoff_attribute("Show javascript button to fill in CAPTCHA",
                  "auto-captcha", "autocaptcha", 0);
  @ <p>When enabled, a button appears on the login screen for user
  @ "anonymous" that will automatically fill in the CAPTCHA password.
  @ This is less secure than forcing the user to do it manually, but is
  @ probably secure enough and it is certainly more convenient for
  @ anonymous users.</p>

  @ <hr />
  @ <p><input type="submit"  name="submit" value="Apply Changes" /></p>
  @ </div></form>
  db_end_transaction(0);
  style_footer();
}

/*
** WEBPAGE: setup_login_group
*/
void setup_login_group(void){
  const char *zGroup;
  char *zErrMsg = 0;
  Blob fullName;
  char *zSelfRepo;
  const char *zRepo = PD("repo", "");
  const char *zLogin = PD("login", "");
  const char *zPw = PD("pw", "");
  const char *zNewName = PD("newname", "New Login Group");

  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  file_canonical_name(g.zRepositoryName, &fullName, 0);
  zSelfRepo = mprintf(blob_str(&fullName));
  blob_reset(&fullName);
  if( P("join")!=0 ){
    login_group_join(zRepo, zLogin, zPw, zNewName, &zErrMsg);
  }else if( P("leave") ){
    login_group_leave(&zErrMsg);
  }
  style_header("Login Group Configuration");
  if( zErrMsg ){
    @ <p class="generalError">%s(zErrMsg)</p>
  }
  zGroup = login_group_name();
  if( zGroup==0 ){
    @ <p>This repository (in the file named "%h(zSelfRepo)")
    @ is not currently part of any login-group.
    @ To join a login group, fill out the form below.</p>
    @
    @ <form action="%s(g.zTop)/setup_login_group" method="post"><div>
    login_insert_csrf_secret();
    @ <blockquote><table border="0">
    @
    @ <tr><td align="right"><b>Repository filename in group to join:</b></td>
    @ <td width="5"></td><td>
    @ <input type="text" size="50" value="%h(zRepo)" name="repo"></td></tr>
    @
    @ <tr><td align="right"><b>Login on the above repo:</b></td>
    @ <td width="5"></td><td>
    @ <input type="text" size="20" value="%h(zLogin)" name="login"></td></tr>
    @
    @ <tr><td align="right"><b>Password:</b></td>
    @ <td width="5"></td><td>
    @ <input type="password" size="20" name="pw"></td></tr>
    @
    @ <tr><td align="right"><b>Name of login-group:</b></td>
    @ <td width="5"></td><td>
    @ <input type="text" size="30" value="%h(zNewName)" name="newname">
    @ (only used if creating a new login-group).</td></tr>
    @
    @ <tr><td colspan="3" align="center">
    @ <input type="submit" value="Join" name="join"></td></tr>
    @ </table></blockquote></div></form>
  }else{
    Stmt q;
    int n = 0;
    @ <p>This repository (in the file "%h(zSelfRepo)")
    @ is currently part of the "<b>%h(zGroup)</b>" login group.
    @ Other repositories in that group are:</p>
    @ <table border="0" cellspacing="4">
    @ <tr><td colspan="2"><th align="left">Project Name<td>
    @ <th align="left">Repository File</tr>
    db_prepare(&q,
       "SELECT value,"
       "       (SELECT value FROM config"
       "         WHERE name=('peer-name-' || substr(x.name,11)))"
       "  FROM config AS x"
       " WHERE name GLOB 'peer-repo-*'"
       " ORDER BY value"
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zRepo = db_column_text(&q, 0);
      const char *zTitle = db_column_text(&q, 1);
      n++;
      @ <tr><td align="right">%d(n).</td><td width="4">
      @ <td>%h(zTitle)<td width="10"><td>%h(zRepo)</tr>
    }
    db_finalize(&q);
    @ </table>
    @
    @ <p><form action="%s(g.zTop)/setup_login_group" method="post"><div>
    login_insert_csrf_secret();
    @ To leave this login group press
    @ <input type="submit" value="Leave Login Group" name="leave">
    @ </form></p>
  }
  style_footer();
}

/*
** WEBPAGE: setup_timeline
*/
void setup_timeline(void){
  double tmDiff;
  char zTmDiff[20];
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }

  style_header("Timeline Display Preferences");
  db_begin_transaction();
  @ <form action="%s(g.zTop)/setup_timeline" method="post"><div>
  login_insert_csrf_secret();

  @ <hr />
  onoff_attribute("Allow block-markup in timeline",
                  "timeline-block-markup", "tbm", 0);
  @ <p>In timeline displays, check-in comments can be displayed with or
  @ without block markup (paragraphs, tables, etc.)</p>

  @ <hr />
  onoff_attribute("Plaintext comments on timelines",
                  "timeline-plaintext", "tpt", 0);
  @ <p>In timeline displays, check-in comments are displayed literally,
  @ without any wiki or HTML interpretation.</p>

  @ <hr />
  onoff_attribute("Use Universal Coordinated Time (UTC)",
                  "timeline-utc", "utc", 1);
  @ <p>Show times as UTC (also sometimes called Greenwich Mean Time (GMT) or
  @ Zulu) instead of in local time.  On this server, local time is currently
  g.fTimeFormat = 2;
  tmDiff = db_double(0.0, "SELECT julianday('now')");
  tmDiff = db_double(0.0,
        "SELECT (julianday(%.17g,'localtime')-julianday(%.17g))*24.0",
        tmDiff, tmDiff);
  sqlite3_snprintf(sizeof(zTmDiff), zTmDiff, "%.1f", tmDiff);
  if( strcmp(zTmDiff, "0.0")==0 ){
    @ the same as UTC and so this setting will make no difference in
    @ the display.</p>
  }else if( tmDiff<0.0 ){
    sqlite3_snprintf(sizeof(zTmDiff), zTmDiff, "%.1f", -tmDiff);
    @ %s(zTmDiff) hours behind UTC.</p>
  }else{
    @ %s(zTmDiff) hours ahead of UTC.</p>
  }

  @ <hr />
  onoff_attribute("Show version differences by default",
                  "show-version-diffs", "vdiff", 0);
  @ <p>On the version-information pages linked from the timeline can either
  @ show complete diffs of all file changes, or can just list the names of
  @ the files that have changed.  Users can get to either page by
  @ clicking.  This setting selects the default.</p>

  @ <hr />
  entry_attribute("Max timeline comment length", 6,
                  "timeline-max-comment", "tmc", "0");
  @ <p>The maximum length of a comment to be displayed in a timeline.
  @ "0" there is no length limit.</p>

  @ <hr />
  @ <p><input type="submit"  name="submit" value="Apply Changes" /></p>
  @ </div></form>
  db_end_transaction(0);
  style_footer();
}

/*
** WEBPAGE: setup_settings
*/
void setup_settings(void){
  struct stControlSettings const *pSet;

  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }

  style_header("Settings");
  db_open_local(0);
  db_begin_transaction();
  @ <p>This page provides a simple interface to the "fossil setting" command.
  @ See the "fossil help setting" output below for further information on
  @ the meaning of each setting.</p><hr />
  @ <form action="%s(g.zTop)/setup_settings" method="post"><div>
  @ <table border="0"><tr><td valign="top">
  login_insert_csrf_secret();
  for(pSet=ctrlSettings; pSet->name!=0; pSet++){
    if( pSet->width==0 ){
      onoff_attribute(pSet->name, pSet->name,
                      pSet->var!=0 ? pSet->var : pSet->name,
                      is_truth(pSet->def));
      if( pSet->versionable ){
        @  (v)<br />
      } else {
        @ <br />
      }
    }
  }
  @ </td><td style="width:50px;"></td><td valign="top">
  for(pSet=ctrlSettings; pSet->name!=0; pSet++){
    if( pSet->width!=0 && !pSet->versionable){
      entry_attribute(pSet->name, /*pSet->width*/ 25, pSet->name,
                      pSet->var!=0 ? pSet->var : pSet->name,
                      (char*)pSet->def);
      @ <br />
    }
  }
  @ </td><td style="width:50px;"></td><td valign="top">
  for(pSet=ctrlSettings; pSet->name!=0; pSet++){
    if( pSet->width!=0 && pSet->versionable){
      @<b>%s(pSet->name)</b> (v)<br />
      textarea_attribute("", /*rows*/ 3, /*cols*/ 20, pSet->name,
                      pSet->var!=0 ? pSet->var : pSet->name,
                      (char*)pSet->def);
      @<br />
    }
  }
  @ </td></tr></table>
  @ <p><input type="submit"  name="submit" value="Apply Changes" /></p>
  @ </div></form>
  @ <p>Settings marked with (v) are 'versionable' and will be overridden
  @ by the contents of files named <tt>.fossil-settings/PROPERTY</tt>.</p>
  @ <hr /><p>
  @ These settings work in the same way, as the <kbd>set</kbd>
  @ commandline:<br />
  @ </p><pre>%s(zHelp_setting_cmd)</pre>
  db_end_transaction(0);
  style_footer();
}

/*
** WEBPAGE: setup_config
*/
void setup_config(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }

  style_header("WWW Configuration");
  db_begin_transaction();
  @ <form action="%s(g.zTop)/setup_config" method="post"><div>
  login_insert_csrf_secret();
  @ <hr />
  entry_attribute("Project Name", 60, "project-name", "pn", "");
  @ <p>Give your project a name so visitors know what this site is about.
  @ The project name will also be used as the RSS feed title.</p>
  @ <hr />
  textarea_attribute("Project Description", 3, 80,
                     "project-description", "pd", "");
  @ <p>Describe your project. This will be used in page headers for search
  @ engines as well as a short RSS description.</p>
  @ <hr />
  onoff_attribute("Enable WYSIWYG Wiki Editing",
                  "wysiwyg-wiki", "wysiwyg-wiki", 0);
  @ <p>Enable what-you-see-is-what-you-get (WYSIWYG) editing of wiki pages.
  @ The WYSIWYG editor generates HTML instead of markup, which makes
  @ subsequent manual editing more difficult.</p>
  @ <hr />
  entry_attribute("Index Page", 60, "index-page", "idxpg", "/home");
  @ <p>Enter the pathname of the page to display when the "Home" menu
  @ option is selected and when no pathname is
  @ specified in the URL.  For example, if you visit the url:</p>
  @
  @ <blockquote><p>%h(g.zBaseURL)</p></blockquote>
  @
  @ <p>And you have specified an index page of "/home" the above will
  @ automatically redirect to:</p>
  @
  @ <blockquote><p>%h(g.zBaseURL)/home</p></blockquote>
  @
  @ <p>The default "/home" page displays a Wiki page with the same name
  @ as the Project Name specified above.  Some sites prefer to redirect
  @ to a documentation page (ex: "/doc/tip/index.wiki") or to "/timeline".</p>
  @
  @ <p>Note:  To avoid a redirect loop or other problems, this entry must
  @ begin with "/" and it must specify a valid page.  For example,
  @ "<b>/home</b>" will work but "<b>home</b>" will not, since it omits the
  @ leading "/".</p>
  @ <hr />
  onoff_attribute("Use HTML as wiki markup language",
    "wiki-use-html", "wiki-use-html", 0);
  @ <p>Use HTML as the wiki markup language. Wiki links will still be parsed
  @ but all other wiki formatting will be ignored. This option is helpful
  @ if you have chosen to use a rich HTML editor for wiki markup such as
  @ TinyMCE.</p>
  @ <p><strong>CAUTION:</strong> when
  @ enabling, <i>all</i> HTML tags and attributes are accepted in the wiki.
  @ No sanitization is done. This means that it is very possible for malicious
  @ users to inject dangerous HTML, CSS and JavaScript code into your wiki.</p>
  @ <p>This should <strong>only</strong> be enabled when wiki editing is limited
  @ to trusted users. It should <strong>not</strong> be used on a publically
  @ editable wiki.</p>
  @ <hr />
  @ <p><input type="submit"  name="submit" value="Apply Changes" /></p>
  @ </div></form>
  db_end_transaction(0);
  style_footer();
}

/*
** WEBPAGE: setup_editcss
*/
void setup_editcss(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("clear")!=0 ){
    db_multi_exec("DELETE FROM config WHERE name='css'");
    cgi_replace_parameter("css", zDefaultCSS);
    db_end_transaction(0);
    cgi_redirect("setup_editcss");
  }
  if( P("submit")!=0 ){
    textarea_attribute(0, 0, 0, "css", "css", zDefaultCSS);
    db_end_transaction(0);
    cgi_redirect("setup_editcss");
  }
  style_header("Edit CSS");
  @ <form action="%s(g.zTop)/setup_editcss" method="post"><div>
  login_insert_csrf_secret();
  @ Edit the CSS below:<br />
  textarea_attribute("", 35, 80, "css", "css", zDefaultCSS);
  @ <br />
  @ <input type="submit" name="submit" value="Apply Changes" />
  @ <input type="submit" name="clear" value="Revert To Default" />
  @ </div></form>
  @ <p><span class="note">Note:</span> Press your browser Reload button after
  @ modifying the CSS in order to pull in the modified CSS file.</p>
  @ <hr />
  @ The default CSS is shown below for reference.  Other examples
  @ of CSS files can be seen on the <a href="setup_skin">skins page</a>.
  @ See also the <a href="setup_header">header</a> and
  @ <a href="setup_footer">footer</a> editing screens.
  @ <blockquote><pre>
  cgi_append_default_css();
  @ </pre></blockquote>
  style_footer();
  db_end_transaction(0);
}

/*
** WEBPAGE: setup_header
*/
void setup_header(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("clear")!=0 ){
    db_multi_exec("DELETE FROM config WHERE name='header'");
    cgi_replace_parameter("header", zDefaultHeader);
  }else if( P("submit")!=0 ){
    textarea_attribute(0, 0, 0, "header", "header", zDefaultHeader);
  }else if( P("fixbase")!=0 ){
    const char *z = db_get("header", (char*)zDefaultHeader);
    char *zHead = strstr(z, "<head>");
    if( strstr(z, "<base href=")==0 && zHead!=0 ){
      char *zNew;
      char *zTail = &zHead[6];
      while( fossil_isspace(zTail[0]) ) zTail++;
      zNew = mprintf("%.*s\n<base href=\"$baseurl/$current_page\" />\n%s",
                     zHead+6-z, z, zTail);
      cgi_replace_parameter("header", zNew);
      db_set("header", zNew, 0);
    }
  }

  style_header("Edit Page Header");
  @ <form action="%R/setup_header" method="post"><div>

  /* Make sure the header contains <base href="...">.   Issue a warning
  ** if it does not. */
  if( !cgi_header_contains("<base href=") ){
    @ <p class="generalError">Please add
    @ <tt>&lt;base href="$baseurl/$current_page"&gt;</tt> after
    @ <tt>&lt;head&gt;</tt> in the header!
    @ <input type="submit" name="fixbase" value="Add &lt;base&gt; Now"></p>
  }

  login_insert_csrf_secret();
  @ <p>Edit HTML text with embedded TH1 (a TCL dialect) that will be used to
  @ generate the beginning of every page through start of the main
  @ menu.</p>
  textarea_attribute("", 35, 80, "header", "header", zDefaultHeader);
  @ <br />
  @ <input type="submit" name="submit" value="Apply Changes" />
  @ <input type="submit" name="clear" value="Revert To Default" />
  @ </div></form>
  @ <hr />
  @ The default header is shown below for reference.  Other examples
  @ of headers can be seen on the <a href="setup_skin">skins page</a>.
  @ See also the <a href="setup_editcss">CSS</a> and
  @ <a href="setup_footer">footer</a> editing screeens.
  @ <blockquote><pre>
  @ %h(zDefaultHeader)
  @ </pre></blockquote>
  style_footer();
  db_end_transaction(0);
}

/*
** WEBPAGE: setup_footer
*/
void setup_footer(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("clear")!=0 ){
    db_multi_exec("DELETE FROM config WHERE name='footer'");
    cgi_replace_parameter("footer", zDefaultFooter);
  }

  style_header("Edit Page Footer");
  @ <form action="%s(g.zTop)/setup_footer" method="post"><div>
  login_insert_csrf_secret();
  @ <p>Edit HTML text with embedded TH1 (a TCL dialect) that will be used to
  @ generate the end of every page.</p>
  textarea_attribute("", 20, 80, "footer", "footer", zDefaultFooter);
  @ <br />
  @ <input type="submit" name="submit" value="Apply Changes" />
  @ <input type="submit" name="clear" value="Revert To Default" />
  @ </div></form>
  @ <hr />
  @ The default footer is shown below for reference.  Other examples
  @ of footers can be seen on the <a href="setup_skin">skins page</a>.
  @ See also the <a href="setup_editcss">CSS</a> and
  @ <a href="setup_header">header</a> editing screens.
  @ <blockquote><pre>
  @ %h(zDefaultFooter)
  @ </pre></blockquote>
  style_footer();
  db_end_transaction(0);
}

/*
** WEBPAGE: setup_modreq
*/
void setup_modreq(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }

  style_header("Moderator For Wiki And Tickets");
  db_begin_transaction();
  @ <form action="%R/setup_modreq" method="post"><div>
  login_insert_csrf_secret();
  @ <hr />
  onoff_attribute("Moderate ticket changes",
     "modreq-tkt", "modreq-tkt", 0);
  @ <p>When enabled, any change to tickets is subject to the approval
  @ a ticket moderator - a user with the "q" or Mod-Tkt privilege.
  @ Ticket changes enter the system and are shown locally, but are not
  @ synced until they are approved.  The moderator has the option to
  @ delete the change rather than approve it.  Ticket changes made by
  @ a user who hwas the Mod-Tkt privilege are never subject to
  @ moderation.
  @
  @ <hr />
  onoff_attribute("Moderate wiki changes",
     "modreq-wiki", "modreq-wiki", 0);
  @ <p>When enabled, any change to wiki is subject to the approval
  @ a ticket moderator - a user with the "l" or Mod-Wiki privilege.
  @ Wiki changes enter the system and are shown locally, but are not
  @ synced until they are approved.  The moderator has the option to
  @ delete the change rather than approve it.  Wiki changes made by
  @ a user who has the Mod-Wiki privilege are never subject to
  @ moderation.
  @ </p>

  @ <hr />
  @ <p><input type="submit"  name="submit" value="Apply Changes" /></p>
  @ </div></form>
  db_end_transaction(0);
  style_footer();

}

/*
** WEBPAGE: setup_adunit
*/
void setup_adunit(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("clear")!=0 ){
    db_multi_exec("DELETE FROM config WHERE name GLOB 'adunit*'");
    cgi_replace_parameter("adunit","");
  }

  style_header("Edit Ad Unit");
  @ <form action="%s(g.zTop)/setup_adunit" method="post"><div>
  login_insert_csrf_secret();
  @ <p>Edit HTML text for an ad unit that will be inserted after the
  @ menu bar and above the content of every page.</p>
  textarea_attribute("", 20, 80, "adunit", "adunit", "");
  @ <br />
  onoff_attribute("Omit ads to administrator",
     "adunit-omit-if-admin", "oia", 0);
  @ <br />
  onoff_attribute("Omit ads to logged-in users",
     "adunit-omit-if-user", "oiu", 0);
  @ <br />
  @ <input type="submit" name="submit" value="Apply Changes" />
  @ <input type="submit" name="clear" value="Delete Ad-Unit" />
  @ </div></form>
  style_footer();
  db_end_transaction(0);
}

/*
** WEBPAGE: setup_logo
*/
void setup_logo(void){
  const char *zLogoMime = db_get("logo-mimetype","image/gif");
  const char *aLogoImg = P("logoim");
  int szLogoImg = atoi(PD("logoim:bytes","0"));
  const char *zBgMime = db_get("background-mimetype","image/gif");
  const char *aBgImg = P("bgim");
  int szBgImg = atoi(PD("bgim:bytes","0"));
  if( szLogoImg>0 ){
    zLogoMime = PD("logoim:mimetype","image/gif");
  }
  if( szBgImg>0 ){
    zBgMime = PD("bgim:mimetype","image/gif");
  }
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("setlogo")!=0 && zLogoMime && zLogoMime[0] && szLogoImg>0 ){
    Blob img;
    Stmt ins;
    blob_init(&img, aLogoImg, szLogoImg);
    db_prepare(&ins,
        "REPLACE INTO config(name,value,mtime)"
        " VALUES('logo-image',:bytes,now())"
    );
    db_bind_blob(&ins, ":bytes", &img);
    db_step(&ins);
    db_finalize(&ins);
    db_multi_exec(
       "REPLACE INTO config(name,value,mtime) VALUES('logo-mimetype',%Q,now())",
       zLogoMime
    );
    db_end_transaction(0);
    cgi_redirect("setup_logo");
  }else if( P("clrlogo")!=0 ){
    db_multi_exec(
       "DELETE FROM config WHERE name IN "
           "('logo-image','logo-mimetype')"
    );
    db_end_transaction(0);
    cgi_redirect("setup_logo");
  }else if( P("setbg")!=0 && zBgMime && zBgMime[0] && szBgImg>0 ){
    Blob img;
    Stmt ins;
    blob_init(&img, aBgImg, szBgImg);
    db_prepare(&ins,
        "REPLACE INTO config(name,value,mtime)"
        " VALUES('background-image',:bytes,now())"
    );
    db_bind_blob(&ins, ":bytes", &img);
    db_step(&ins);
    db_finalize(&ins);
    db_multi_exec(
       "REPLACE INTO config(name,value,mtime)"
       " VALUES('background-mimetype',%Q,now())",
       zBgMime
    );
    db_end_transaction(0);
    cgi_redirect("setup_logo");
  }else if( P("clrbg")!=0 ){
    db_multi_exec(
       "DELETE FROM config WHERE name IN "
           "('background-image','background-mimetype')"
    );
    db_end_transaction(0);
    cgi_redirect("setup_logo");
  }
  style_header("Edit Project Logo And Background");
  @ <p>The current project logo has a MIME-Type of <b>%h(zLogoMime)</b>
  @ and looks like this:</p>
  @ <blockquote><p><img src="%s(g.zTop)/logo" alt="logo" border="1" />
  @ </p></blockquote>
  @
  @ <form action="%s(g.zTop)/setup_logo" method="post"
  @  enctype="multipart/form-data"><div>
  @ <p>The logo is accessible to all users at this URL:
  @ <a href="%s(g.zBaseURL)/logo">%s(g.zBaseURL)/logo</a>.
  @ The logo may or may not appear on each
  @ page depending on the <a href="setup_editcss">CSS</a> and
  @ <a href="setup_header">header setup</a>.
  @ To change the logo image, use the following form:</p>
  login_insert_csrf_secret();
  @ Logo Image file:
  @ <input type="file" name="logoim" size="60" accept="image/*" />
  @ <p align="center">
  @ <input type="submit" name="setlogo" value="Change Logo" />
  @ <input type="submit" name="clrlogo" value="Revert To Default" /></p>
  @ </div></form>
  @ <hr />
  @
  @ <p>The current background image has a MIME-Type of <b>%h(zBgMime)</b>
  @ and looks like this:</p>
  @ <blockquote><p><img src="%s(g.zTop)/background" alt="background" border=1 />
  @ </p></blockquote>
  @
  @ <form action="%s(g.zTop)/setup_logo" method="post"
  @  enctype="multipart/form-data"><div>
  @ <p>The background image is accessible to all users at this URL:
  @ <a href="%s(g.zBaseURL)/background">%s(g.zBaseURL)/background</a>.
  @ The background image may or may not appear on each
  @ page depending on the <a href="setup_editcss">CSS</a> and
  @ <a href="setup_header">header setup</a>.
  @ To change the background image, use the following form:</p>
  login_insert_csrf_secret();
  @ Background image file:
  @ <input type="file" name="bgim" size="60" accept="image/*" />
  @ <p align="center">
  @ <input type="submit" name="setbg" value="Change Background" />
  @ <input type="submit" name="clrbg" value="Revert To Default" /></p>
  @ </div></form>
  @ <hr />
  @
  @ <p><span class="note">Note:</span>  Your browser has probably cached these
  @ images, so you may need to press the Reload button before changes will
  @ take effect. </p>
  style_footer();
  db_end_transaction(0);
}

/*
** Prevent the RAW SQL feature from being used to ATTACH a different
** database and query it.
**
** Actually, the RAW SQL feature only does a single statement per request.
** So it is not possible to ATTACH and then do a separate query.  This
** routine is not strictly necessary, therefore.  But it does not hurt
** to be paranoid.
*/
int raw_sql_query_authorizer(
  void *pError,
  int code,
  const char *zArg1,
  const char *zArg2,
  const char *zArg3,
  const char *zArg4
){
  if( code==SQLITE_ATTACH ){
    return SQLITE_DENY;
  }
  return SQLITE_OK;
}


/*
** WEBPAGE: admin_sql
**
** Run raw SQL commands against the database file using the web interface.
*/
void sql_page(void){
  const char *zQ = P("q");
  int go = P("go")!=0;
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();
  style_header("Raw SQL Commands");
  @ <p><b>Caution:</b> There are no restrictions on the SQL that can be
  @ run by this page.  You can do serious and irrepairable damage to the
  @ repository.  Proceed with extreme caution.</p>
  @
  @ <p>Only a the first statement in the entry box will be run.
  @ Any subsequent statements will be silently ignored.</p>
  @
  @ <p>Database names:<ul><li>repository &rarr; %s(db_name("repository"))
  if( g.zConfigDbName ){
    @ <li>config &rarr; %s(db_name("configdb"))
  }
  if( g.localOpen ){
    @ <li>local-checkout &rarr; %s(db_name("localdb"))
  }
  @ </ul></p>
  @
  @ <form method="post" action="%s(g.zTop)/admin_sql">
  login_insert_csrf_secret();
  @ SQL:<br />
  @ <textarea name="q" rows="5" cols="80">%h(zQ)</textarea><br />
  @ <input type="submit" name="go" value="Run SQL">
  @ <input type="submit" name="schema" value="Show Schema">
  @ <input type="submit" name="tablelist" value="List Tables">
  @ </form>
  if( P("schema") ){
    zQ = sqlite3_mprintf(
            "SELECT sql FROM %s.sqlite_master WHERE sql IS NOT NULL",
            db_name("repository"));
    go = 1;
  }else if( P("tablelist") ){
    zQ = sqlite3_mprintf(
            "SELECT name FROM %s.sqlite_master WHERE type='table'"
            " ORDER BY name",
            db_name("repository"));
    go = 1;
  }
  if( go ){
    sqlite3_stmt *pStmt;
    int rc;
    const char *zTail;
    int nCol;
    int nRow = 0;
    int i;
    @ <hr />
    login_verify_csrf_secret();
    sqlite3_set_authorizer(g.db, raw_sql_query_authorizer, 0);
    rc = sqlite3_prepare_v2(g.db, zQ, -1, &pStmt, &zTail);
    if( rc!=SQLITE_OK ){
      @ <div class="generalError">%h(sqlite3_errmsg(g.db))</div>
      sqlite3_finalize(pStmt);
    }else if( pStmt==0 ){
      /* No-op */
    }else if( (nCol = sqlite3_column_count(pStmt))==0 ){
      sqlite3_step(pStmt);
      rc = sqlite3_finalize(pStmt);
      if( rc ){
        @ <div class="generalError">%h(sqlite3_errmsg(g.db))</div>
      }
    }else{
      @ <table border=1>
      while( sqlite3_step(pStmt)==SQLITE_ROW ){
        if( nRow==0 ){
          @ <tr>
          for(i=0; i<nCol; i++){
            @ <th>%h(sqlite3_column_name(pStmt, i))</th>
          }
          @ </tr>
        }
        nRow++;
        @ <tr>
        for(i=0; i<nCol; i++){
          switch( sqlite3_column_type(pStmt, i) ){
            case SQLITE_INTEGER:
            case SQLITE_FLOAT: {
               @ <td align="right" valign="top">
               @ %s(sqlite3_column_text(pStmt, i))</td>
               break;
            }
            case SQLITE_NULL: {
               @ <td valign="top" align="center"><i>NULL</i></td>
               break;
            }
            case SQLITE_TEXT: {
               const char *zText = (const char*)sqlite3_column_text(pStmt, i);
               @ <td align="left" valign="top"
               @ style="white-space:pre;">%h(zText)</td>
               break;
            }
            case SQLITE_BLOB: {
               @ <td valign="top" align="center">
               @ <i>%d(sqlite3_column_bytes(pStmt, i))-byte BLOB</i></td>
               break;
            }
          }
        }
        @ </tr>
      }
      sqlite3_finalize(pStmt);
      @ </table>
    }
  }
  style_footer();
}


/*
** WEBPAGE: admin_th1
**
** Run raw TH1 commands using the web interface.  If Tcl integration was
** enabled at compile-time and the "tcl" setting is enabled, Tcl commands
** may be run as well.
*/
void th1_page(void){
  const char *zQ = P("q");
  int go = P("go")!=0;
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed();
  }
  db_begin_transaction();
  style_header("Raw TH1 Commands");
  @ <p><b>Caution:</b> There are no restrictions on the TH1 that can be
  @ run by this page.  If Tcl integration was enabled at compile-time and
  @ the "tcl" setting is enabled, Tcl commands may be run as well.</p>
  @
  @ <form method="post" action="%s(g.zTop)/admin_th1">
  login_insert_csrf_secret();
  @ TH1:<br />
  @ <textarea name="q" rows="5" cols="80">%h(zQ)</textarea><br />
  @ <input type="submit" name="go" value="Run TH1">
  @ </form>
  if( go ){
    const char *zR;
    int rc;
    int n;
    @ <hr />
    login_verify_csrf_secret();
    rc = Th_Eval(g.interp, 0, zQ, -1);
    zR = Th_GetResult(g.interp, &n);
    if( rc==TH_OK ){
      @ <pre class="th1result">%h(zR)</pre>
    }else{
      @ <pre class="th1error">%h(zR)</pre>
    }
  }
  style_footer();
}
