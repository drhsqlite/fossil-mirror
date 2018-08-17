/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** Setup pages associated with user management.  The code in this
** file was formerly part of the "setup.c" module, but has been broken
** out into its own module to improve maintainability.
*/
#include "config.h"
#include <assert.h>
#include "setupuser.h"

/*
** WEBPAGE: setup_ulist
**
** Show a list of users.  Clicking on any user jumps to the edit
** screen for that user.  Requires Admin privileges.
**
** Query parameters:
**
**   with=CAP         Only show users that have one or more capabilities in CAP.
*/
void setup_ulist(void){
  Stmt s;
  double rNow;
  const char *zWith = P("with");

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }

  if( zWith==0 || zWith[0]==0 ){
    style_submenu_element("Add", "setup_uedit");
    style_submenu_element("Log", "access_log");
    style_submenu_element("Help", "setup_ulist_notes");
    style_header("User List");
    @ <table border=1 cellpadding=2 cellspacing=0 class='userTable'>
    @ <thead><tr>
    @   <th>Category
    @   <th>Capabilities (<a href='%R/setup_ucap_list'>key</a>)
    @   <th>Info <th>Last Change</tr></thead>
    @ <tbody>
    db_prepare(&s,
       "SELECT uid, login, cap, date(mtime,'unixepoch')"
       "  FROM user"
       " WHERE login IN ('anonymous','nobody','developer','reader')"
       " ORDER BY login"
    );
    while( db_step(&s)==SQLITE_ROW ){
      int uid = db_column_int(&s, 0);
      const char *zLogin = db_column_text(&s, 1);
      const char *zCap = db_column_text(&s, 2);
      const char *zDate = db_column_text(&s, 4);
      @ <tr>
      @ <td><a href='setup_uedit?id=%d(uid)'>%h(zLogin)</a>
      @ <td>%h(zCap)

      if( fossil_strcmp(zLogin,"anonymous")==0 ){
        @ <td>All logged-in users
      }else if( fossil_strcmp(zLogin,"developer")==0 ){
        @ <td>Users with '<b>v</b>' capability
      }else if( fossil_strcmp(zLogin,"nobody")==0 ){
        @ <td>All users without login
      }else if( fossil_strcmp(zLogin,"reader")==0 ){
        @ <td>Users with '<b>u</b>' capability
      }else{
        @ <td>
      }
      if( zDate && zDate[0] ){
        @ <td>%h(zDate)
      }else{
        @ <td>
      }
      @ </tr>
    }
    db_finalize(&s);
  }else{
    style_header("Users With Capabilities \"%h\"", zWith);
  }
  @ </tbody></table>
  @ <div class='section'>Users</div>
  @ <table border=1 cellpadding=2 cellspacing=0 class='userTable sortable' \
  @  data-column-types='ktxTTK' data-init-sort='2'>
  @ <thead><tr>
  @ <th>Login Name<th>Caps<th>Info<th>Date<th>Expire<th>Last Login</tr></thead>
  @ <tbody>
  db_multi_exec(
    "CREATE TEMP TABLE lastAccess(uname TEXT PRIMARY KEY, atime REAL)"
    "WITHOUT ROWID;"
  );
  if( db_table_exists("repository","accesslog") ){
    db_multi_exec(
      "INSERT INTO lastAccess(uname, atime)"
      " SELECT uname, max(mtime) FROM ("
      "    SELECT uname, mtime FROM accesslog WHERE success"
      "    UNION ALL"
      "    SELECT login AS uname, rcvfrom.mtime AS mtime"
      "      FROM rcvfrom JOIN user USING(uid))"
      " GROUP BY 1;"
    );
  }
  if( zWith && zWith[0] ){
    zWith = mprintf(" AND fullcap(cap) GLOB '*[%q]*'", zWith);
  }else{
    zWith = "";
  }
  db_prepare(&s,
     "SELECT uid, login, cap, info, date(mtime,'unixepoch'),"
     "       lower(login) AS sortkey, "
     "       CASE WHEN info LIKE '%%expires 20%%'"
             "    THEN substr(info,instr(lower(info),'expires')+8,10)"
             "    END AS exp,"
             "atime"
     "  FROM user LEFT JOIN lastAccess ON login=uname"
     " WHERE login NOT IN ('anonymous','nobody','developer','reader') %s"
     " ORDER BY sortkey", zWith/*safe-for-%s*/
  );
  rNow = db_double(0.0, "SELECT julianday('now');");
  while( db_step(&s)==SQLITE_ROW ){
    int uid = db_column_int(&s, 0);
    const char *zLogin = db_column_text(&s, 1);
    const char *zCap = db_column_text(&s, 2);
    const char *zInfo = db_column_text(&s, 3);
    const char *zDate = db_column_text(&s, 4);
    const char *zSortKey = db_column_text(&s,5);
    const char *zExp = db_column_text(&s,6);
    double rATime = db_column_double(&s,7);
    char *zAge = 0;
    if( rATime>0.0 ){
      zAge = human_readable_age(rNow - rATime);
    }
    @ <tr>
    @ <td data-sortkey='%h(zSortKey)'>\
    @ <a href='setup_uedit?id=%d(uid)'>%h(zLogin)</a>
    @ <td>%h(zCap)
    @ <td>%h(zInfo)
    @ <td>%h(zDate?zDate:"")
    @ <td>%h(zExp?zExp:"")
    @ <td data-sortkey='%f(rATime)' style='white-space:nowrap'>%s(zAge?zAge:"")
    @ </tr>
    fossil_free(zAge);
  }
  @ </tbody></table>
  db_finalize(&s);
  style_table_sorter();
  style_footer();
}

/*
** WEBPAGE: setup_ulist_notes
**
** A documentation page showing notes about user configuration.  This
** information used to be a side-bar on the user list page, but has been
** factored out for improved presentation.
*/
void setup_ulist_notes(void){
  style_header("User Configuration Notes");
  @ <h1>User Configuration Notes:</h1>
  @ <ol>
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
  @ Users with privilege <span class="capability">u</span> inherit the combined
  @ privileges of <span class="usertype">reader</span>,
  @ <span class="usertype">anonymous</span>, and
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
  @ <li><p>The permission flags are as follows:</p>
  capabilities_table();
  @ </li>
  @ </ol>
  style_footer();
}

/*
** WEBPAGE: setup_ucap_list
**
** A documentation page showing the meaning of the various user capabilities
** code letters.
*/
void setup_ucap_list(void){
  style_header("User Capability Codes");
  capabilities_table();
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
** WEBPAGE: setup_uedit
**
** Edit information about a user or create a new user.
** Requires Admin privileges.
*/
void user_edit(void){
  const char *zId, *zLogin, *zInfo, *zCap, *zPw;
  const char *zGroup;
  const char *zOldLogin;
  int doWrite;
  int uid, i;
  int higherUser = 0;  /* True if user being edited is SETUP and the */
                       /* user doing the editing is ADMIN.  Disallow editing */
  const char *inherit[128];
  int a[128];
  const char *oa[128];

  /* Must have ADMIN privileges to access this page
  */
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(0); return; }

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
    /* User pressed the cancel button */
    cgi_redirect(cgi_referer("setup_ulist"));
    return;
  }

  /* If we have all the necessary information, write the new or
  ** modified user record.  After writing the user record, redirect
  ** to the page that displays a list of users.
  */
  doWrite = cgi_all("login","info","pw") && !higherUser && cgi_csrf_safe(1);
  if( doWrite ){
    char c;
    char zCap[70], zNm[4];
    zNm[0] = 'a';
    zNm[2] = 0;
    for(i=0, c='a'; c<='z'; c++){
      zNm[1] = c;
      a[c&0x7f] = (c!='s' || g.perm.Setup) && P(zNm)!=0;
      if( a[c&0x7f] ) zCap[i++] = c;
    }
    for(c='0'; c<='9'; c++){
      zNm[1] = c;
      a[c&0x7f] = P(zNm)!=0;
      if( a[c&0x7f] ) zCap[i++] = c;
    }
    for(c='A'; c<='Z'; c++){
      zNm[1] = c;
      a[c&0x7f] = P(zNm)!=0;
      if( a[c&0x7f] ) zCap[i++] = c;
    }

    zCap[i] = 0;
    zPw = P("pw");
    zLogin = P("login");
    if( strlen(zLogin)==0 ){
      const char *zRef = cgi_referer("setup_ulist");
      style_header("User Creation Error");
      @ <span class="loginError">Empty login not allowed.</span>
      @
      @ <p><a href="setup_uedit?id=%d(uid)&referer=%T(zRef)">
      @ [Bummer]</a></p>
      style_footer();
      return;
    }
    if( isValidPwString(zPw) ){
      zPw = sha1_shared_secret(zPw, zLogin, 0);
    }else{
      zPw = db_text(0, "SELECT pw FROM user WHERE uid=%d", uid);
    }
    zOldLogin = db_text(0, "SELECT login FROM user WHERE uid=%d", uid);
    if( db_exists("SELECT 1 FROM user WHERE login=%Q AND uid!=%d",zLogin,uid) ){
      const char *zRef = cgi_referer("setup_ulist");
      style_header("User Creation Error");
      @ <span class="loginError">Login "%h(zLogin)" is already used by
      @ a different user.</span>
      @
      @ <p><a href="setup_uedit?id=%d(uid)&referer=%T(zRef)">
      @ [Bummer]</a></p>
      style_footer();
      return;
    }
    login_verify_csrf_secret();
    db_multi_exec(
       "REPLACE INTO user(uid,login,info,pw,cap,mtime) "
       "VALUES(nullif(%d,0),%Q,%Q,%Q,%Q,now())",
      uid, zLogin, P("info"), zPw, zCap
    );
    setup_incr_cfgcnt();
    admin_log( "Updated user [%q] with capabilities [%q].",
               zLogin, zCap );
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
      admin_log( "Updated user [%q] in all login groups "
                 "with capabilities [%q].",
                 zLogin, zCap );
      if( zErr ){
        const char *zRef = cgi_referer("setup_ulist");
        style_header("User Change Error");
        admin_log( "Error updating user '%q': %s'.", zLogin, zErr );
        @ <span class="loginError">%h(zErr)</span>
        @
        @ <p><a href="setup_uedit?id=%d(uid)&referer=%T(zRef)">
        @ [Bummer]</a></p>
        style_footer();
        return;
      }
    }
    cgi_redirect(cgi_referer("setup_ulist"));
    return;
  }

  /* Load the existing information about the user, if any
  */
  zLogin = "";
  zInfo = "";
  zCap = "";
  zPw = "";
  for(i='a'; i<='z'; i++) oa[i] = "";
  for(i='0'; i<='9'; i++) oa[i] = "";
  for(i='A'; i<='Z'; i++) oa[i] = "";
  if( uid ){
    zLogin = db_text("", "SELECT login FROM user WHERE uid=%d", uid);
    zInfo = db_text("", "SELECT info FROM user WHERE uid=%d", uid);
    zCap = db_text("", "SELECT cap FROM user WHERE uid=%d", uid);
    zPw = db_text("", "SELECT pw FROM user WHERE uid=%d", uid);
    for(i=0; zCap[i]; i++){
      char c = zCap[i];
      if( (c>='a' && c<='z') || (c>='0' && c<='9') || (c>='A' && c<='Z') ){
        oa[c&0x7f] = " checked=\"checked\"";
      }
    }
  }

  /* figure out inherited permissions */
  memset((char *)inherit, 0, sizeof(inherit));
  if( fossil_strcmp(zLogin, "developer") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='developer'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
         "<span class=\"ueditInheritDeveloper\"><sub>[D]</sub></span>";
    }
    free(z2);
  }
  if( fossil_strcmp(zLogin, "reader") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='reader'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
          "<span class=\"ueditInheritReader\"><sub>[R]</sub></span>";
    }
    free(z2);
  }
  if( fossil_strcmp(zLogin, "anonymous") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='anonymous'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
           "<span class=\"ueditInheritAnonymous\"><sub>[A]</sub></span>";
    }
    free(z2);
  }
  if( fossil_strcmp(zLogin, "nobody") ){
    char *z1, *z2;
    z1 = z2 = db_text(0,"SELECT cap FROM user WHERE login='nobody'");
    while( z1 && *z1 ){
      inherit[0x7f & *(z1++)] =
           "<span class=\"ueditInheritNobody\"><sub>[N]</sub></span>";
    }
    free(z2);
  }

  /* Begin generating the page
  */
  style_submenu_element("Cancel", "%s", cgi_referer("setup_ulist"));
  if( uid ){
    style_header("Edit User %h", zLogin);
    style_submenu_element("Access Log", "%R/access_log?u=%t", zLogin);
  }else{
    style_header("Add A New User");
  }
  @ <div class="ueditCapBox">
  @ <form action="%s(g.zPath)" method="post"><div>
  login_insert_csrf_secret();
  if( login_is_special(zLogin) ){
    @ <input type="hidden" name="login" value="%s(zLogin)">
    @ <input type="hidden" name="info" value="">
    @ <input type="hidden" name="pw" value="*">
  }
  @ <input type="hidden" name="referer" value="%h(cgi_referer("setup_ulist"))">
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
  if( login_is_special(zLogin) ){
    @    <td><b>%h(zLogin)</b></td>
  }else{
    @   <td><input type="text" name="login" value="%h(zLogin)" /></td>
    @ </tr>
    @ <tr>
    @   <td class="usetupEditLabel">Contact&nbsp;Info:</td>
    @   <td><textarea name="info" cols="40" rows="2">%h(zInfo)</textarea></td>
  }
  @ </tr>
  @ <tr>
  @   <td class="usetupEditLabel">Capabilities:</td>
  @   <td>
#define B(x) inherit[x]
  @ <table border=0><tr><td valign="top">
  if( g.perm.Setup ){
    @  <label><input type="checkbox" name="as"%s(oa['s']) />
    @  Setup%s(B('s'))</label><br />
  }
  @  <label><input type="checkbox" name="aa"%s(oa['a']) />
  @  Admin%s(B('a'))</label><br />
  @  <label><input type="checkbox" name="au"%s(oa['u']) />
  @  Reader%s(B('u'))</label><br>
  @  <label><input type="checkbox" name="av"%s(oa['v']) />
  @  Developer%s(B('v'))</label><br />
  @  <label><input type="checkbox" name="ad"%s(oa['d']) />
  @  Delete%s(B('d'))</label><br />
  @  <label><input type="checkbox" name="ae"%s(oa['e']) />
  @  View-PII%s(B('e'))</label><br />
  @  <label><input type="checkbox" name="ap"%s(oa['p']) />
  @  Password%s(B('p'))</label><br />
  @  <label><input type="checkbox" name="ai"%s(oa['i']) />
  @  Check-In%s(B('i'))</label><br />
  @  <label><input type="checkbox" name="ao"%s(oa['o']) />
  @  Check-Out%s(B('o'))</label><br />
  @  <label><input type="checkbox" name="ah"%s(oa['h']) />
  @  Hyperlinks%s(B('h'))</label><br />
  @  <label><input type="checkbox" name="ab"%s(oa['b']) />
  @  Attachments%s(B('b'))</label><br>
  @  <label><input type="checkbox" name="ag"%s(oa['g']) />
  @  Clone%s(B('g'))</label><br />

  @ </td><td><td width="40"></td><td valign="top">
  @  <label><input type="checkbox" name="aj"%s(oa['j']) />
  @  Read Wiki%s(B('j'))</label><br>
  @  <label><input type="checkbox" name="af"%s(oa['f']) />
  @  New Wiki%s(B('f'))</label><br />
  @  <label><input type="checkbox" name="am"%s(oa['m']) />
  @  Append Wiki%s(B('m'))</label><br />
  @  <label><input type="checkbox" name="ak"%s(oa['k']) />
  @  Write Wiki%s(B('k'))</label><br />
  @  <label><input type="checkbox" name="al"%s(oa['l']) />
  @  Moderate Wiki%s(B('l'))</label><br />
  @  <label><input type="checkbox" name="ar"%s(oa['r']) />
  @  Read Ticket%s(B('r'))</label><br />
  @  <label><input type="checkbox" name="an"%s(oa['n']) />
  @  New Tickets%s(B('n'))</label><br />
  @  <label><input type="checkbox" name="ac"%s(oa['c']) />
  @  Append To Ticket%s(B('c'))</label><br>
  @  <label><input type="checkbox" name="aw"%s(oa['w']) />
  @  Write Tickets%s(B('w'))</label><br />
  @  <label><input type="checkbox" name="aq"%s(oa['q']) />
  @  Moderate Tickets%s(B('q'))</label><br>
  @  <label><input type="checkbox" name="at"%s(oa['t']) />
  @  Ticket Report%s(B('t'))</label><br />
  @  <label><input type="checkbox" name="ax"%s(oa['x']) />
  @  Private%s(B('x'))</label>

  @ </td><td><td width="40"></td><td valign="top">
  @  <label><input type="checkbox" name="ay"%s(oa['y']) />
  @  Write Unversioned%s(B('y'))</label><br />
  @  <label><input type="checkbox" name="az"%s(oa['z']) />
  @  Download Zip%s(B('z'))</label><br />
  @  <label><input type="checkbox" name="a2"%s(oa['2']) />
  @  Read Forum%s(B('2'))</label><br />
  @  <label><input type="checkbox" name="a3"%s(oa['3']) />
  @  Write Forum%s(B('3'))</label><br />
  @  <label><input type="checkbox" name="a4"%s(oa['4']) />
  @  WriteTrusted Forum%s(B('4'))</label><br>
  @  <label><input type="checkbox" name="a5"%s(oa['5']) />
  @  Moderate Forum%s(B('5'))</label><br>
  @  <label><input type="checkbox" name="a6"%s(oa['6']) />
  @  Supervise Forum%s(B('6'))</label><br>
  @  <label><input type="checkbox" name="a7"%s(oa['7']) />
  @  Email Alerts%s(B('7'))</label><br>
  @  <label><input type="checkbox" name="aA"%s(oa['A']) />
  @  Send Announcements%s(B('A'))</label><br>
  @  <label><input type="checkbox" name="aD"%s(oa['D']) />
  @  Enable Debug%s(B('D'))</label>
  @ </td></tr>
  @ </table>
  @   </td>
  @ </tr>
  @ <tr>
  @   <td class="usetupEditLabel">Selected Cap.:</td>
  @   <td>
  @     <span id="usetupEditCapability">(missing JS?)</span>
  @     <a href="%R/setup_ucap_list">(key)</a>
  @   </td>
  @ </tr>
  if( !login_is_special(zLogin) ){
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
  }
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
  style_load_one_js_file("useredit.js");
  @ <h2>Privileges And Capabilities:</h2>
  @ <ul>
  if( higherUser ){
    @ <li><p class="missingPriv">
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
  @ The "<span class="ueditInheritNobody"><sub>N</sub></span>" subscript suffix
  @ indicates the privileges of <span class="usertype">nobody</span> that
  @ are available to all users regardless of whether or not they are logged in.
  @ </p></li>
  @
  @ <li><p>
  @ The "<span class="ueditInheritAnonymous"><sub>A</sub></span>"
  @ subscript suffix
  @ indicates the privileges of <span class="usertype">anonymous</span> that
  @ are inherited by all logged-in users.
  @ </p></li>
  @
  @ <li><p>
  @ The "<span class="ueditInheritDeveloper"><sub>D</sub></span>"
  @ subscript suffix indicates the privileges of 
  @ <span class="usertype">developer</span> that
  @ are inherited by all users with the
  @ <span class="capability">Developer</span> privilege.
  @ </p></li>
  @
  @ <li><p>
  @ The "<span class="ueditInheritReader"><sub>R</sub></span>" subscript suffix
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
  @ The <span class="capability">View-PII</span> privilege allows the display
  @ of personally-identifiable information information such as the
  @ email address of users and contact
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
  @ To disable universal access to the repository, make sure that the
  @ <span class="usertype">nobody</span> user has no capabilities
  @ enabled. The password for <span class="usertype">nobody</span> is ignored.
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
