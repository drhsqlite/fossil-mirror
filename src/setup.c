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
  if( !g.okSetup ){
    login_needed();
  }

  style_header("Server Administration");
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
  setup_menu_entry("Skins", "setup_skin",
    "Select from a menu of prepackaged \"skins\" for the web interface");
  setup_menu_entry("CSS", "setup_editcss",
    "Edit the Cascading Style Sheet used by all pages of this repository");
  setup_menu_entry("Header", "setup_header",
    "Edit HTML text inserted at the top of every page");
  setup_menu_entry("Footer", "setup_footer",
    "Edit HTML text inserted at the bottom of every page");
  setup_menu_entry("Logo", "setup_logo",
    "Change the logo image for the server");
  setup_menu_entry("Shunned", "shun",
    "Show artifacts that are shunned by this repository");
  setup_menu_entry("Log", "rcvfromlist",
    "A record of received artifacts and their sources");
  setup_menu_entry("User-Log", "access_log",
    "A record of login attempts");
  setup_menu_entry("Stats", "stat",
    "Display repository statistics");
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

  login_check_credentials();
  if( !g.okAdmin ){
    login_needed();
    return;
  }

  style_submenu_element("Add", "Add User", "setup_uedit");
  style_header("User List");
  @ <table class="usetupLayoutTable">
  @ <tr><td class="usetupColumnLayout">
  @ <span class="note">Users:</span>
  @ <table class="usetupUserList">
  @ <tr>
  @   <th class="usetupListUser" style="text-align: right;padding-right: 20px;">User&nbsp;ID</th>
  @   <th class="usetupListCap" style="text-align: center;padding-right: 15px;">Capabilities</th>
  @   <th class="usetupListCon"  style="text-align: left;">Contact&nbsp;Info</th>
  @ </tr>
  db_prepare(&s, "SELECT uid, login, cap, info FROM user ORDER BY login");
  while( db_step(&s)==SQLITE_ROW ){
    const char *zCap = db_column_text(&s, 2);
    @ <tr>
    @ <td class="usetupListUser" style="text-align: right;padding-right: 20px;white-space:nowrap;">
    if( g.okAdmin && (zCap[0]!='s' || g.okSetup) ){
      @ <a href="setup_uedit?id=%d(db_column_int(&s,0))">
    }
    @ %h(db_column_text(&s,1))
    if( g.okAdmin ){
      @ </a>
    }
    @ </td>
    @ <td class="usetupListCap" style="text-align: center;padding-right: 15px;">%s(zCap)</td>
    @ <td  class="usetupListCon"  style="text-align: left;">%s(db_column_text(&s,3))</td>
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
     @ <tr><td valign="top"><b>m</b></td>
     @   <td><i>Append-Wiki:</i> Append to wiki pages</td></tr>
     @ <tr><td valign="top"><b>n</b></td>
     @   <td><i>New-Tkt:</i> Create new tickets</td></tr>
     @ <tr><td valign="top"><b>o</b></td>
     @   <td><i>Check-Out:</i> Check out versions</td></tr>
     @ <tr><td valign="top"><b>p</b></td>
     @   <td><i>Password:</i> Change your own password</td></tr>
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
  char *oaa, *oas, *oar, *oaw, *oan, *oai, *oaj, *oao, *oap;
  char *oak, *oad, *oac, *oaf, *oam, *oah, *oag, *oae;
  char *oat, *oau, *oav, *oab, *oax, *oaz;
  const char *zGroup;
  const char *zOldLogin;
  const char *inherit[128];
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
    char zCap[50];
    int i = 0;
    int aa = P("aa")!=0;
    int ab = P("ab")!=0;
    int ad = P("ad")!=0;
    int ae = P("ae")!=0;
    int ai = P("ai")!=0;
    int aj = P("aj")!=0;
    int ak = P("ak")!=0;
    int an = P("an")!=0;
    int ao = P("ao")!=0;
    int ap = P("ap")!=0;
    int ar = P("ar")!=0;
    int as = g.okSetup && P("as")!=0;
    int aw = P("aw")!=0;
    int ac = P("ac")!=0;
    int af = P("af")!=0;
    int am = P("am")!=0;
    int ah = P("ah")!=0;
    int ag = P("ag")!=0;
    int at = P("at")!=0;
    int au = P("au")!=0;
    int av = P("av")!=0;
    int ax = P("ax")!=0;
    int az = P("az")!=0;
    if( aa ){ zCap[i++] = 'a'; }
    if( ab ){ zCap[i++] = 'b'; }
    if( ac ){ zCap[i++] = 'c'; }
    if( ad ){ zCap[i++] = 'd'; }
    if( ae ){ zCap[i++] = 'e'; }
    if( af ){ zCap[i++] = 'f'; }
    if( ah ){ zCap[i++] = 'h'; }
    if( ag ){ zCap[i++] = 'g'; }
    if( ai ){ zCap[i++] = 'i'; }
    if( aj ){ zCap[i++] = 'j'; }
    if( ak ){ zCap[i++] = 'k'; }
    if( am ){ zCap[i++] = 'm'; }
    if( an ){ zCap[i++] = 'n'; }
    if( ao ){ zCap[i++] = 'o'; }
    if( ap ){ zCap[i++] = 'p'; }
    if( ar ){ zCap[i++] = 'r'; }
    if( as ){ zCap[i++] = 's'; }
    if( at ){ zCap[i++] = 't'; }
    if( au ){ zCap[i++] = 'u'; }
    if( av ){ zCap[i++] = 'v'; }
    if( aw ){ zCap[i++] = 'w'; }
    if( ax ){ zCap[i++] = 'x'; }
    if( az ){ zCap[i++] = 'z'; }

    zCap[i] = 0;
    zPw = P("pw");
    zLogin = P("login");
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
       "REPLACE INTO user(uid,login,info,pw,cap) "
       "VALUES(nullif(%d,0),%Q,%Q,%Q,'%s')",
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
        "  cap=%Q"
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
  oaa = oab = oac = oad = oae = oaf = oag = oah = oai = oaj = oak = oam =
        oan = oao = oap = oar = oas = oat = oau = oav = oaw = oax = oaz = "";
  if( uid ){
    zLogin = db_text("", "SELECT login FROM user WHERE uid=%d", uid);
    zInfo = db_text("", "SELECT info FROM user WHERE uid=%d", uid);
    zCap = db_text("", "SELECT cap FROM user WHERE uid=%d", uid);
    zPw = db_text("", "SELECT pw FROM user WHERE uid=%d", uid);
    if( strchr(zCap, 'a') ) oaa = " checked=\"checked\"";
    if( strchr(zCap, 'b') ) oab = " checked=\"checked\"";
    if( strchr(zCap, 'c') ) oac = " checked=\"checked\"";
    if( strchr(zCap, 'd') ) oad = " checked=\"checked\"";
    if( strchr(zCap, 'e') ) oae = " checked=\"checked\"";
    if( strchr(zCap, 'f') ) oaf = " checked=\"checked\"";
    if( strchr(zCap, 'g') ) oag = " checked=\"checked\"";
    if( strchr(zCap, 'h') ) oah = " checked=\"checked\"";
    if( strchr(zCap, 'i') ) oai = " checked=\"checked\"";
    if( strchr(zCap, 'j') ) oaj = " checked=\"checked\"";
    if( strchr(zCap, 'k') ) oak = " checked=\"checked\"";
    if( strchr(zCap, 'm') ) oam = " checked=\"checked\"";
    if( strchr(zCap, 'n') ) oan = " checked=\"checked\"";
    if( strchr(zCap, 'o') ) oao = " checked=\"checked\"";
    if( strchr(zCap, 'p') ) oap = " checked=\"checked\"";
    if( strchr(zCap, 'r') ) oar = " checked=\"checked\"";
    if( strchr(zCap, 's') ) oas = " checked=\"checked\"";
    if( strchr(zCap, 't') ) oat = " checked=\"checked\"";
    if( strchr(zCap, 'u') ) oau = " checked=\"checked\"";
    if( strchr(zCap, 'v') ) oav = " checked=\"checked\"";
    if( strchr(zCap, 'w') ) oaw = " checked=\"checked\"";
    if( strchr(zCap, 'x') ) oax = " checked=\"checked\"";
    if( strchr(zCap, 'z') ) oaz = " checked=\"checked\"";
  }

  /* figure out inherited permissions */
  memset(inherit, 0, sizeof(inherit));
  if( strcmp(zLogin, "developer") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='developer'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
         "<span class=\"ueditInheritDeveloper\">&bull;</span>";
    }
    free(z2);
  }
  if( strcmp(zLogin, "reader") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='reader'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
          "<span class=\"ueditInheritReader\">&bull;</span>";
    }
    free(z2);
  }
  if( strcmp(zLogin, "anonymous") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='anonymous'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
           "<span class=\"ueditInheritAnonymous\">&bull;</span>";
    }
    free(z2);
  }
  if( strcmp(zLogin, "nobody") ){
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
  if( g.okSetup ){
    @    <input type="checkbox" name="as"%s(oas) />%s(B('s'))Setup<br />
  }
  @    <input type="checkbox" name="aa"%s(oaa) />%s(B('a'))Admin<br />
  @    <input type="checkbox" name="ad"%s(oad) />%s(B('d'))Delete<br />
  @    <input type="checkbox" name="ae"%s(oae) />%s(B('e'))Email<br />
  @    <input type="checkbox" name="ap"%s(oap) />%s(B('p'))Password<br />
  @    <input type="checkbox" name="ai"%s(oai) />%s(B('i'))Check-In<br />
  @    <input type="checkbox" name="ao"%s(oao) />%s(B('o'))Check-Out<br />
  @    <input type="checkbox" name="ah"%s(oah) />%s(B('h'))History<br />
  @    <input type="checkbox" name="au"%s(oau) />%s(B('u'))Reader<br />
  @    <input type="checkbox" name="av"%s(oav) />%s(B('v'))Developer<br />
  @    <input type="checkbox" name="ag"%s(oag) />%s(B('g'))Clone<br />
  @    <input type="checkbox" name="aj"%s(oaj) />%s(B('j'))Read Wiki<br />
  @    <input type="checkbox" name="af"%s(oaf) />%s(B('f'))New Wiki<br />
  @    <input type="checkbox" name="am"%s(oam) />%s(B('m'))Append Wiki<br />
  @    <input type="checkbox" name="ak"%s(oak) />%s(B('k'))Write Wiki<br />
  @    <input type="checkbox" name="ab"%s(oab) />%s(B('b'))Attachments<br />
  @    <input type="checkbox" name="ar"%s(oar) />%s(B('r'))Read Ticket<br />
  @    <input type="checkbox" name="an"%s(oan) />%s(B('n'))New Ticket<br />
  @    <input type="checkbox" name="ac"%s(oac) />%s(B('c'))Append Ticket<br />
  @    <input type="checkbox" name="aw"%s(oaw) />%s(B('w'))Write Ticket<br />
  @    <input type="checkbox" name="at"%s(oat) />%s(B('t'))Ticket Report<br />
  @    <input type="checkbox" name="ax"%s(oax) />%s(B('x'))Private<br />
  @    <input type="checkbox" name="az"%s(oaz) />%s(B('z'))Download Zip
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
  @ The <span class="capability">History</span> privilege allows a user
  @ to see most hyperlinks. This is recommended ON for most logged-in users
  @ but OFF for user "nobody" to avoid problems with spiders trying to walk
  @ every historical version of every baseline and file.
  @ </p></li>
  @
  @ <li><p>
  @ The <span class="capability">Zip</span> privilege allows a user to
  @ see the "download as ZIP"
  @ hyperlink and permits access to the <tt>/zip</tt> page.  This allows
  @ users to download ZIP archives without granting other rights like
  @ <span class="capability">Read</span> or
  @ <span class="capability">History</span>.  This privilege is recommended for
  @ user <span class="usertype">nobody</span> so that automatic package
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
  @ that the <span class="capability">h</span> (History) capability be turned 
  @ off for the <span class="usertype">nobody</span> user.
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
    int iQ = strcmp(zQ,"on")==0 || atoi(zQ);
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
  if( zQ && strcmp(zQ,zVal)!=0 ){
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
  if( zQ && strcmp(zQ,z)!=0 ){
    login_verify_csrf_secret();
    db_set(zVar, zQ, 0);
    z = zQ;
  }
  if( rows>0 && cols>0 ){
    @ <textarea name="%s(zQP)" rows="%d(rows)" cols="%d(cols)">%h(z)</textarea>
    if (zLabel && *zLabel)
      @ <span class="textareaLabel">%s(zLabel)</span>
  }
}


/*
** WEBPAGE: setup_access
*/
void setup_access(void){
  login_check_credentials();
  if( !g.okSetup ){
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
  @ <hr />
  onoff_attribute("Allow REMOTE_USER authentication",
     "remote_user_ok", "remote_user_ok", 0);
  @ <p>When enabled, if the REMOTE_USER environment variable is set to the
  @ login name of a valid user and no other login credentials are available,
  @ then the REMOTE_USER is accepted as an authenticated user.
  @ </p>

  @ <hr />
  entry_attribute("Login expiration time", 6, "cookie-expire", "cex", "8766");
  @ <p>The number of hours for which a login is valid.  This must be a
  @ positive number.  The default is 8760 hours which is approximately equal
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
  @ <p>Permissions given to users that register themselves using the HTTP UI
  @ or are registered by the administrator using the command line interface.
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
  if( !g.okSetup ){
    login_needed();
  }
  file_canonical_name(g.zRepositoryName, &fullName);
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
    @ <blockquote><table broder="0">
    @
    @ <tr><td align="right"><b>Repository filename in group to join:</b></td>
    @ <td width="5"></td><td>
    @ <input type="text" size="50" value="%h(zRepo)" name="repo"></td></tr>
    @
    @ <td align="right"><b>Login on the above repo:</b></td>
    @ <td width="5"></td><td>
    @ <input type="text" size="20" value="%h(zLogin)" name="login"></td></tr>
    @
    @ <td align="right"><b>Password:</b></td>
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
    @ </table>
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
  login_check_credentials();
  if( !g.okSetup ){
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
  onoff_attribute("Use Universal Coordinated Time (UTC)",
                  "timeline-utc", "utc", 1);
  @ <p>Show times as UTC (also sometimes called Greenwich Mean Time (GMT) or
  @ Zulu) instead of in local time.</p>

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
  if( !g.okSetup ){
    login_needed();
  }

  style_header("Settings");
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
      @ <br />
    }
  }
  @ </td><td style="width: 30;"></td><td valign="top">
  for(pSet=ctrlSettings; pSet->name!=0; pSet++){
    if( pSet->width!=0 ){
      entry_attribute(pSet->name, /*pSet->width*/ 40, pSet->name,
                      pSet->var!=0 ? pSet->var : pSet->name,
                      (char*)pSet->def);
      @ <br />
    }
  }
  @ </td></tr></table>
  @ <p><input type="submit"  name="submit" value="Apply Changes" /></p>
  @ </div></form>
  @ <hr /><p>
  @ These settings work in the same way, as the <kbd>set</kbd> commandline:<br />
  @ </p><pre>%s(zHelp_setting_cmd)</pre>
  db_end_transaction(0);
  style_footer();
}

/*
** WEBPAGE: setup_config
*/
void setup_config(void){
  login_check_credentials();
  if( !g.okSetup ){
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
  textarea_attribute("Project Description", 5, 60,
                     "project-description", "pd", "");
  @ <p>Describe your project. This will be used in page headers for search
  @ engines as well as a short RSS description.</p>
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
  if( !g.okSetup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("clear")!=0 ){
    db_multi_exec("DELETE FROM config WHERE name='css'");
    cgi_replace_parameter("css", zDefaultCSS);
    db_end_transaction(0);
    cgi_redirect("setup_editcss");
  }else{
    textarea_attribute(0, 0, 0, "css", "css", zDefaultCSS);
  }
  if( P("submit")!=0 ){
    db_end_transaction(0);
    cgi_redirect("setup_editcss");
  }
  style_header("Edit CSS");
  @ <form action="%s(g.zTop)/setup_editcss" method="post"><div>
  login_insert_csrf_secret();
  @ Edit the CSS below:<br />
  textarea_attribute("", 40, 80, "css", "css", zDefaultCSS);
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
  if( !g.okSetup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("clear")!=0 ){
    db_multi_exec("DELETE FROM config WHERE name='header'");
    cgi_replace_parameter("header", zDefaultHeader);
  }else{
    textarea_attribute(0, 0, 0, "header", "header", zDefaultHeader);
  }
  style_header("Edit Page Header");
  @ <form action="%s(g.zTop)/setup_header" method="post"><div>
  login_insert_csrf_secret();
  @ <p>Edit HTML text with embedded TH1 (a TCL dialect) that will be used to
  @ generate the beginning of every page through start of the main
  @ menu.</p>
  textarea_attribute("", 40, 80, "header", "header", zDefaultHeader);
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
  if( !g.okSetup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("clear")!=0 ){
    db_multi_exec("DELETE FROM config WHERE name='footer'");
    cgi_replace_parameter("footer", zDefaultFooter);
  }else{
    textarea_attribute(0, 0, 0, "footer", "footer", zDefaultFooter);
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
** WEBPAGE: setup_logo
*/
void setup_logo(void){
  const char *zMime = db_get("logo-mimetype","image/gif");
  const char *aImg = P("im");
  int szImg = atoi(PD("im:bytes","0"));
  if( szImg>0 ){
    zMime = PD("im:mimetype","image/gif");
  }
  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }
  db_begin_transaction();
  if( P("set")!=0 && zMime && zMime[0] && szImg>0 ){
    Blob img;
    Stmt ins;
    blob_init(&img, aImg, szImg);
    db_prepare(&ins,
        "REPLACE INTO config(name,value,mtime)"
        " VALUES('logo-image',:bytes,now())"
    );
    db_bind_blob(&ins, ":bytes", &img);
    db_step(&ins);
    db_finalize(&ins);
    db_multi_exec(
       "REPLACE INTO config(name,value,mtime) VALUES('logo-mimetype',%Q,now())",
       zMime
    );
    db_end_transaction(0);
    cgi_redirect("setup_logo");
  }else if( P("clr")!=0 ){
    db_multi_exec(
       "DELETE FROM config WHERE name GLOB 'logo-*'"
    );
    db_end_transaction(0);
    cgi_redirect("setup_logo");
  }
  style_header("Edit Project Logo");
  @ <p>The current project logo has a MIME-Type of <b>%h(zMime)</b> and looks
  @ like this:</p>
  @ <blockquote><p><img src="%s(g.zTop)/logo" alt="logo" /></p></blockquote>
  @
  @ <p>The logo is accessible to all users at this URL:
  @ <a href="%s(g.zBaseURL)/logo">%s(g.zBaseURL)/logo</a>.
  @ The logo may or may not appear on each
  @ page depending on the <a href="setup_editcss">CSS</a> and
  @ <a href="setup_header">header setup</a>.</p>
  @
  @ <form action="%s(g.zTop)/setup_logo" method="post"
  @  enctype="multipart/form-data"><div>
  @ <p>To set a new logo image, select a file to use as the logo using
  @ the entry box below and then press the "Change Logo" button.</p>
  login_insert_csrf_secret();
  @ Logo Image file:
  @ <input type="file" name="im" size="60" accept="image/*" /><br />
  @ <input type="submit" name="set" value="Change Logo" />
  @ <input type="submit" name="clr" value="Revert To Default" />
  @ </div></form>
  @
  @ <p><span class="note">Note:</span>  Your browser has probably cached the
  @ logo image, so you will probably need to press the Reload button on your
  @ browser after changing the logo to provoke your browser to reload the new
  @ logo image. </p>
  style_footer();
  db_end_transaction(0);
}
