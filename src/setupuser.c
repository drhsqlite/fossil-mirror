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
**
** Note:  Do not confuse "Users" with "Subscribers".  Code to deal with
** subscribers is over in the "alerts.c" source file.
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
**   ubg              Color backgrounds by username hash
*/
void setup_ulist(void){
  Stmt s;
  double rNow;
  const char *zWith = P("with");
  int bUnusedOnly = P("unused")!=0;
  int bUbg = P("ubg")!=0;
  int bHaveAlerts;

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  bHaveAlerts = alert_tables_exist();
  style_submenu_element("Add", "setup_uedit");
  style_submenu_element("Log", "access_log");
  style_submenu_element("Help", "setup_ulist_notes");
  if( bHaveAlerts ){
    style_submenu_element("Subscribers", "subscribers");
  }
  style_set_current_feature("setup");
  style_header("User List");
  if( (zWith==0 || zWith[0]==0) && !bUnusedOnly ){
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
    @ </tbody></table>
    @ <div class='section'>Users</div>
  }else{
    style_submenu_element("All Users", "setup_ulist");
    if( bUnusedOnly ){
      @ <div class='section'>Unused logins</div>
    }else if( zWith ){
      if( zWith[1]==0 ){
        @ <div class='section'>Users with capability "%h(zWith)"</div>
      }else{
        @ <div class='section'>Users with any capability in "%h(zWith)"</div>
      }
    }
  }
  if( !bUnusedOnly ){
    style_submenu_element("Unused", "setup_ulist?unused");
  }
  @ <table border=1 cellpadding=2 cellspacing=0 class='userTable sortable' \
  @  data-column-types='ktxKTKt' data-init-sort='4'>
  @ <thead><tr>
  @ <th>Login Name<th>Caps<th>Info<th>Date<th>Expire<th>Last Login\
  @ <th>Alerts</tr></thead>
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
  if( !db_table_exists("repository","subscriber") ){
    db_multi_exec(
      "CREATE TEMP TABLE subscriber(suname PRIMARY KEY, ssub, subscriberId)"
      "WITHOUT ROWID;"
    );
  }
  if( bUnusedOnly ){
    zWith = mprintf(
        " AND login NOT IN ("
        "SELECT user FROM event WHERE user NOT NULL "
        "UNION ALL SELECT euser FROM event WHERE euser NOT NULL%s)"
        " AND uid NOT IN (SELECT uid FROM rcvfrom)",
        bHaveAlerts ?
          " UNION ALL SELECT suname FROM subscriber WHERE suname NOT NULL":"");
  }else if( zWith && zWith[0] ){
    zWith = mprintf(" AND fullcap(cap) GLOB '*[%q]*'", zWith);
  }else{
    zWith = "";
  }
  db_prepare(&s,
      /*0-4*/"SELECT uid, login, cap, info, date(user.mtime,'unixepoch'),"
      /* 5 */"lower(login) AS sortkey, "
      /* 6 */"CASE WHEN info LIKE '%%expires 20%%'"
             "    THEN substr(info,instr(lower(info),'expires')+8,10)"
             "    END AS exp,"
      /* 7 */"atime,"
      /* 8 */"user.mtime AS sorttime,"
      /*9-11*/"%s"
             " FROM user LEFT JOIN lastAccess ON login=uname"
             "            LEFT JOIN subscriber ON login=suname"
             " WHERE login NOT IN ('anonymous','nobody','developer','reader') %s"
             " ORDER BY sorttime DESC",
             bHaveAlerts
             ? "subscriber.ssub, subscriber.subscriberId, subscriber.semail"
             : "null, null, null",
             zWith/*safe-for-%s*/
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
    const char *zSub;
    int sid = db_column_int(&s,10);
    sqlite3_int64 sorttime = db_column_int64(&s, 8);
    if( rATime>0.0 ){
      zAge = human_readable_age(rNow - rATime);
    }
    if( bUbg ){
      @ <tr style='background-color: %h(user_color(zLogin));'>
    }else{
      @ <tr>
    }
    @ <td data-sortkey='%h(zSortKey)'>\
    @ <a href='setup_uedit?id=%d(uid)'>%h(zLogin)</a>
    @ <td>%h(zCap)
    @ <td>%h(zInfo)
    @ <td data-sortkey='%09llx(sorttime)'>%h(zDate?zDate:"")
    @ <td>%h(zExp?zExp:"")
    @ <td data-sortkey='%f(rATime)' style='white-space:nowrap'>%s(zAge?zAge:"")
    if( db_column_type(&s,9)==SQLITE_NULL ){
      @ <td>
    }else if( (zSub = db_column_text(&s,9))==0 || zSub[0]==0 ){
      @ <td><a href="%R/alerts?sid=%d(sid)"><i>off</i></a>
    }else{
      const char *zEmail = db_column_text(&s, 11);
      char * zAt = zEmail ? mprintf(" &rarr; %h", zEmail) : mprintf("");
      @ <td><a href="%R/alerts?sid=%d(sid)">%h(zSub)</a>  %z(zAt)
    }

    @ </tr>
    fossil_free(zAge);
  }
  @ </tbody></table>
  db_finalize(&s);
  style_table_sorter();
  style_finish_page();
}

/*
** WEBPAGE: setup_ulist_notes
**
** A documentation page showing notes about user configuration.  This
** information used to be a side-bar on the user list page, but has been
** factored out for improved presentation.
*/
void setup_ulist_notes(void){
  style_set_current_feature("setup");
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
  capabilities_table(CAPCLASS_ALL);
  @ </li>
  @ </ol>
  style_finish_page();
}

/*
** WEBPAGE: setup_ucap_list
**
** A documentation page showing the meaning of the various user capabilities
** code letters.
*/
void setup_ucap_list(void){
  style_set_current_feature("setup");
  style_header("User Capability Codes");
  @ <h1>All capabilities</h1>
  capabilities_table(CAPCLASS_ALL);
  @ <h1>Capabilities associated with checked-in content</h1>
  capabilities_table(CAPCLASS_CODE);
  @ <h1>Capabilities associated with data transfer and sync</h1>
  capabilities_table(CAPCLASS_DATA);
  @ <h1>Capabilities associated with the forum</h1>
  capabilities_table(CAPCLASS_FORUM);
  @ <h1>Capabilities associated with tickets</h1>
  capabilities_table(CAPCLASS_TKT);
  @ <h1>Capabilities associated with wiki</h1>
  capabilities_table(CAPCLASS_WIKI);
  @ <h1>Administrative capabilities</h1>
  capabilities_table(CAPCLASS_SUPER);
  @ <h1>Miscellaneous capabilities</h1>
  capabilities_table(CAPCLASS_OTHER);
  style_finish_page();
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
** Return true if user capability strings zOrig and zNew materially
** differ, taking into account that they may be sorted in an arbitary
** order. This does not take inherited permissions into
** account. Either argument may be NULL. A NULL and an empty string
** are considered equivalent here. e.g. "abc" and "cab" are equivalent
** for this purpose, but "aCb" and "acb" are not.
*/
static int userCapsChanged(const char *zOrig, const char *zNew){
  if( !zOrig ){
    return zNew ? (0!=*zNew) : 0;
  }else if( !zNew ){
    return 0!=*zOrig;
  }else if( 0==fossil_strcmp(zOrig, zNew) ){
    return 0;
  }else{
    /* We don't know that zOrig and zNew are sorted equivalently.  The
    ** following steps will compare strings which contain all the same
    ** capabilities letters as equivalent, regardless of the letters'
    ** order in their strings. */
    char aOrig[128]; /* table of zOrig bytes */
    int nOrig = 0, nNew = 0;

    memset( &aOrig[0], 0, sizeof(aOrig) );
    for( ; *zOrig; ++zOrig, ++nOrig ){
      if( 0==(*zOrig & 0x80) ){
        aOrig[(int)*zOrig] = 1;
      }
    }
    for( ; *zNew; ++zNew, ++nNew ){
      if( 0==(*zNew & 0x80) && !aOrig[(int)*zNew] ){
        return 1;
      }
    }
    return nOrig!=nNew;
  }
}

/*
** COMMAND: test-user-caps-changed
**
** Usage: %fossil test-user-caps-changed caps1 caps2
**
*/
void test_user_caps_changed(void){

  char const * zOld = g.argc>2 ? g.argv[2] : NULL;
  char const * zNew = g.argc>3 ? g.argv[3] : NULL;
  fossil_print("Has changes? = %d\n",
               userCapsChanged( zOld, zNew ));
}

/*
** Sends notification of user permission elevation changes to all
** subscribers with a "u" subscription. This is a no-op if alerts are
** not enabled.
**
** These subscriptions differ from most, in that:
**
** - They currently lack an "unsubscribe" link.
**
** - Only an admin can assign this subscription, but if a non-admin
**   edits their subscriptions after an admin assigns them this one,
**   this particular one will be lost.  "Feature or bug?" is unclear,
**   but it would be odd for a non-admin to be assigned this
**   capability.
*/
static void alert_user_cap_change(const char *zLogin,   /*Affected user*/
                                 int uid,              /*[user].uid*/
                                 int bIsNew,           /*true if new user*/
                                 const char *zOrigCaps,/*Old caps*/
                                 const char *zNewCaps  /*New caps*/){
  Blob hdr, body;
  Stmt q;
  int nBody;
  AlertSender *pSender;
  char *zSubname;
  char *zURL;
  char * zSubject;

  if( !alert_enabled() ) return;
  zSubject = bIsNew
    ? mprintf("New user created: [%q]", zLogin)
    : mprintf("User [%q] capabilities changed", zLogin);
  zURL = db_get("email-url",0);
  zSubname = db_get("email-subname", "[Fossil Repo]");
  blob_init(&body, 0, 0);
  blob_init(&hdr, 0, 0);
  if( bIsNew ){
    blob_appendf(&body, "User [%q] was created with "
                 "permissions [%q] by user [%q].\n",
                 zLogin, zNewCaps, g.zLogin);
  } else {
    blob_appendf(&body, "Permissions for user [%q] where changed "
                 "from [%q] to [%q] by user [%q].\n",
                 zLogin, zOrigCaps, zNewCaps, g.zLogin);
  }
  if( zURL ){
    blob_appendf(&body, "\nUser editor: %s/setup_uedit?uid=%d\n", zURL, uid);
  }
  nBody = blob_size(&body);
  pSender = alert_sender_new(0, 0);
  db_prepare(&q,
        "SELECT semail, hex(subscriberCode)"
        "  FROM subscriber, user "
        " WHERE sverified AND NOT sdonotcall"
        "   AND suname=login"
        "   AND ssub GLOB '*u*'");
  while( !pSender->zErr && db_step(&q)==SQLITE_ROW ){
    const char *zTo = db_column_text(&q, 0);
    blob_truncate(&hdr, 0);
    blob_appendf(&hdr, "To: <%s>\r\nSubject: %s %s\r\n",
                 zTo, zSubname, zSubject);
    if( zURL ){
      const char *zCode = db_column_text(&q, 1);
      blob_truncate(&body, nBody);
      blob_appendf(&body,"\n-- \nSubscription info: %s/alerts/%s\n",
                   zURL, zCode);
    }
    alert_send(pSender, &hdr, &body, 0);
  }
  db_finalize(&q);
  alert_sender_free(pSender);
  fossil_free(zURL);
  fossil_free(zSubname);
  fossil_free(zSubject);
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
  int uid, i;
  char *zOldCaps = 0;        /* Capabilities before edit */
  char *zDeleteVerify = 0;   /* Delete user verification text */
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
  if( uid>0 ){
    zOldCaps = db_text("", "SELECT cap FROM user WHERE uid=%d",uid);
    if( zId && !g.perm.Setup ){
      higherUser = zOldCaps && strchr(zOldCaps,'s');
    }
  }

  if( P("can") ){
    /* User pressed the cancel button */
    cgi_redirect(cgi_referer("setup_ulist"));
    return;
  }

  /* Check for requests to delete the user */
  if( P("delete") && cgi_csrf_safe(2) ){
    int n;
    if( P("verifydelete") ){
      /* Verified delete user request */
      db_unprotect(PROTECT_USER);
      if( alert_tables_exist() ){
        /* Also delete any subscriptions associated with this user */
        db_multi_exec("DELETE FROM subscriber WHERE suname="
                      "(SELECT login FROM user WHERE uid=%d)", uid);
      }
      db_multi_exec("DELETE FROM user WHERE uid=%d", uid);
      db_protect_pop();
      moderation_disapprove_for_missing_users();
      admin_log("Deleted user [%s] (uid %d).",
                PD("login","???")/*safe-for-%s*/, uid);
      cgi_redirect(cgi_referer("setup_ulist"));
      return;
    }
    n = db_int(0, "SELECT count(*) FROM event"
                  " WHERE user=%Q AND objid NOT IN private",
                  P("login"));
    if( n==0 ){
      zDeleteVerify = mprintf("Check this box and press \"Delete User\" again");
    }else{
      zDeleteVerify = mprintf(
        "User \"%s\" has %d or more artifacts in the block-chain. "
        "Delete anyhow?",
        P("login")/*safe-for-%s*/, n);
    }
  }

  style_set_current_feature("setup");

  /* If we have all the necessary information, write the new or
  ** modified user record.  After writing the user record, redirect
  ** to the page that displays a list of users.
  */
  if( !cgi_all("login","info","pw","apply") ){
    /* need all of the above properties to make a change.  Since one or
    ** more are missing, no-op */
  }else if( higherUser ){
    /* An Admin (a) user cannot edit a Superuser (s) */
  }else if( zDeleteVerify!=0 ){
    /* Need to verify a delete request */
  }else if( !cgi_csrf_safe(2) ){
    /* This might be a cross-site request forgery, so ignore it */
  }else{
    /* We have all the information we need to make the change to the user */
    char c;
    int bCapsChanged = 0 /* 1 if user's permissions changed */;
    const int bIsNew = uid<=0;
    char aCap[70], zNm[4];
    zNm[0] = 'a';
    zNm[2] = 0;
    for(i=0, c='a'; c<='z'; c++){
      zNm[1] = c;
      a[c&0x7f] = ((c!='s' && c!='y') || g.perm.Setup) && P(zNm)!=0;
      if( a[c&0x7f] ) aCap[i++] = c;
    }
    for(c='0'; c<='9'; c++){
      zNm[1] = c;
      a[c&0x7f] = P(zNm)!=0;
      if( a[c&0x7f] ) aCap[i++] = c;
    }
    for(c='A'; c<='Z'; c++){
      zNm[1] = c;
      a[c&0x7f] = P(zNm)!=0;
      if( a[c&0x7f] ) aCap[i++] = c;
    }

    aCap[i] = 0;
    bCapsChanged = bIsNew || userCapsChanged(zOldCaps, &aCap[0]);
    zPw = P("pw");
    zLogin = P("login");
    if( strlen(zLogin)==0 ){
      const char *zRef = cgi_referer("setup_ulist");
      style_header("User Creation Error");
      @ <span class="loginError">Empty login not allowed.</span>
      @
      @ <p><a href="setup_uedit?id=%d(uid)&referer=%T(zRef)">
      @ [Bummer]</a></p>
      style_finish_page();
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
      style_finish_page();
      return;
    }
    cgi_csrf_verify();
    db_unprotect(PROTECT_USER);
    uid = db_int(0,
                 "REPLACE INTO user(uid,login,info,pw,cap,mtime) "
                 "VALUES(nullif(%d,0),%Q,%Q,%Q,%Q,now()) "
                 "RETURNING uid",
                 uid, zLogin, P("info"), zPw, &aCap[0]);
    assert( uid>0 );
    if( zOldLogin && fossil_strcmp(zLogin, zOldLogin)!=0 ){
      if( alert_tables_exist() ){
        /* Rename matching subscriber entry, else the user cannot
           re-subscribe with their same email address. */
        db_multi_exec("UPDATE subscriber SET suname=%Q WHERE suname=%Q",
                      zLogin, zOldLogin);
      }
      admin_log( "Renamed user [%q] to [%q].", zOldLogin, zLogin );
    }
    db_protect_pop();
    setup_incr_cfgcnt();
    admin_log( "%s user [%q] with capabilities [%q].",
               bIsNew ? "Added" : "Updated",
               zLogin, &aCap[0] );
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
#if 0
      /* Problem: when renaming a user we need to update the subcriber
      ** names to match but we cannot know from here if each member of
      ** the login group has the subscriber tables, so we cannot blindly
      ** include this SQL. */
      else if( fossil_strcmp(zLogin, zOldLogin)!=0
               && alert_tables_exist() ){
        /* Rename matching subscriber entry, else the user cannot
           re-subscribe with their same email address. */
        blob_appendf(&sql,
                     "UPDATE subscriber SET suname=%Q WHERE suname=%Q;",
                     zLogin, zOldLogin);
      }
#endif
      blob_appendf(&sql,
        "UPDATE user SET login=%Q,"
        "  pw=coalesce(shared_secret(%Q,%Q,"
                "(SELECT value FROM config WHERE name='project-code')),pw),"
        "  info=%Q,"
        "  cap=%Q,"
        "  mtime=now()"
        " WHERE login=%Q;",
        zLogin, P("pw"), zLogin, P("info"), &aCap[0],
        zOldLogin
      );
      db_unprotect(PROTECT_USER);
      login_group_sql(blob_str(&sql), "<li> ", " </li>\n", &zErr);
      db_protect_pop();
      blob_reset(&sql);
      admin_log( "Updated user [%q] in all login groups "
                 "with capabilities [%q].",
                 zLogin, &aCap[0] );
      if( zErr ){
        const char *zRef = cgi_referer("setup_ulist");
        style_header("User Change Error");
        admin_log( "Error updating user '%q': %s'.", zLogin, zErr );
        @ <span class="loginError">%h(zErr)</span>
        @
        @ <p><a href="setup_uedit?id=%d(uid)&referer=%T(zRef)">
        @ [Bummer]</a></p>
        style_finish_page();
        if( bCapsChanged ){
          /* It's possible that caps were updated locally even if
          ** login group updates failed. */
          alert_user_cap_change(zLogin, uid, bIsNew, zOldCaps, &aCap[0]);
        }
        return;
      }
    }
    if( bCapsChanged ){
      alert_user_cap_change(zLogin, uid, bIsNew, zOldCaps, &aCap[0]);
    }
    cgi_redirect(cgi_referer("setup_ulist"));
    return;
  }

  /* Load the existing information about the user, if any
  */
  zLogin = "";
  zInfo = "";
  zCap = zOldCaps;
  zPw = "";
  for(i='a'; i<='z'; i++) oa[i] = "";
  for(i='0'; i<='9'; i++) oa[i] = "";
  for(i='A'; i<='Z'; i++) oa[i] = "";
  if( uid ){
    assert( zCap );
    zLogin = db_text("", "SELECT login FROM user WHERE uid=%d", uid);
    zInfo = db_text("", "SELECT info FROM user WHERE uid=%d", uid);
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
    if( !login_is_special(zLogin) ){
      style_submenu_element("Access Log", "%R/access_log?u=%t", zLogin);
      style_submenu_element("Timeline","%R/timeline?u=%t", zLogin);
    }
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
  @ <table width="100%%">
  @ <tr>
  @   <td class="usetupEditLabel" id="suuid">User ID:</td>
  if( uid ){
    @   <td>%d(uid) <input aria-labelledby="suuid" type="hidden" \
    @   name="id" value="%d(uid)"/>\
    @ </td>
  }else{
    @   <td>(new user)<input aria-labelledby="suuid" type="hidden" name="id" \
    @ value="0"></td>
  }
  @ </tr>
  @ <tr>
  @   <td class="usetupEditLabel" id="sulgn">Login:</td>
  if( login_is_special(zLogin) ){
    @    <td><b>%h(zLogin)</b></td>
  }else{
    @   <td><input aria-labelledby="sulgn" type="text" name="login" \
    @ value="%h(zLogin)">
    if( alert_tables_exist() ){
      int sid;
      sid = db_int(0, "SELECT subscriberId FROM subscriber"
                      " WHERE suname=%Q", zLogin);
      if( sid>0 ){
        @ &nbsp;&nbsp;<a href="%R/alerts?sid=%d(sid)">\
        @ (subscription info for %h(zLogin))</a>\
      }
    }
    @ </td></tr>
    @ <tr>
    @   <td class="usetupEditLabel" id="sucnfo">Contact&nbsp;Info:</td>
    @   <td><textarea aria-labelledby="sucnfo" name="info" cols="40" \
    @ rows="2">%h(zInfo)</textarea></td>
  }
  @ </tr>
  @ <tr>
  @   <td class="usetupEditLabel">Capabilities:</td>
  @   <td width="100%%">
#define B(x) inherit[x]
  @ <div class="columns" style="column-width:13em;">
  @ <ul style="list-style-type: none;">
  if( g.perm.Setup ){
    @  <li><label><input type="checkbox" name="as"%s(oa['s'])>
    @  Setup%s(B('s'))</label>
  }
  @  <li><label><input type="checkbox" name="aa"%s(oa['a'])>
  @  Admin%s(B('a'))</label>
  @  <li><label><input type="checkbox" name="au"%s(oa['u'])>
  @  Reader%s(B('u'))</label>
  @  <li><label><input type="checkbox" name="av"%s(oa['v'])>
  @  Developer%s(B('v'))</label>
#if 0  /* Not Used */
  @  <li><label><input type="checkbox" name="ad"%s(oa['d'])>
  @  Delete%s(B('d'))</label>
#endif
  @  <li><label><input type="checkbox" name="ae"%s(oa['e'])>
  @  View-PII%s(B('e'))</label>
  @  <li><label><input type="checkbox" name="ap"%s(oa['p'])>
  @  Password%s(B('p'))</label>
  @  <li><label><input type="checkbox" name="ai"%s(oa['i'])>
  @  Check-In%s(B('i'))</label>
  @  <li><label><input type="checkbox" name="ao"%s(oa['o'])>
  @  Check-Out%s(B('o'))</label>
  @  <li><label><input type="checkbox" name="ah"%s(oa['h'])>
  @  Hyperlinks%s(B('h'))</label>
  @  <li><label><input type="checkbox" name="ab"%s(oa['b'])>
  @  Attachments%s(B('b'))</label>
  @  <li><label><input type="checkbox" name="ag"%s(oa['g'])>
  @  Clone%s(B('g'))</label>
  @  <li><label><input type="checkbox" name="aj"%s(oa['j'])>
  @  Read Wiki%s(B('j'))</label>
  @  <li><label><input type="checkbox" name="af"%s(oa['f'])>
  @  New Wiki%s(B('f'))</label>
  @  <li><label><input type="checkbox" name="am"%s(oa['m'])>
  @  Append Wiki%s(B('m'))</label>
  @  <li><label><input type="checkbox" name="ak"%s(oa['k'])>
  @  Write Wiki%s(B('k'))</label>
  @  <li><label><input type="checkbox" name="al"%s(oa['l'])>
  @  Moderate Wiki%s(B('l'))</label>
  @  <li><label><input type="checkbox" name="ar"%s(oa['r'])>
  @  Read Ticket%s(B('r'))</label>
  @  <li><label><input type="checkbox" name="an"%s(oa['n'])>
  @  New Tickets%s(B('n'))</label>
  @  <li><label><input type="checkbox" name="ac"%s(oa['c'])>
  @  Append To Ticket%s(B('c'))</label>
  @  <li><label><input type="checkbox" name="aw"%s(oa['w'])>
  @  Write Tickets%s(B('w'))</label>
  @  <li><label><input type="checkbox" name="aq"%s(oa['q'])>
  @  Moderate Tickets%s(B('q'))</label>
  @  <li><label><input type="checkbox" name="at"%s(oa['t'])>
  @  Ticket Report%s(B('t'))</label>
  @  <li><label><input type="checkbox" name="ax"%s(oa['x'])>
  @  Private%s(B('x'))</label>
  @  <li><label><input type="checkbox" name="ay"%s(oa['y'])>
  @  Write Unversioned%s(B('y'))</label>
  @  <li><label><input type="checkbox" name="az"%s(oa['z'])>
  @  Download Zip%s(B('z'))</label>
  @  <li><label><input type="checkbox" name="a2"%s(oa['2'])>
  @  Read Forum%s(B('2'))</label>
  @  <li><label><input type="checkbox" name="a3"%s(oa['3'])>
  @  Write Forum%s(B('3'))</label>
  @  <li><label><input type="checkbox" name="a4"%s(oa['4'])>
  @  WriteTrusted Forum%s(B('4'))</label>
  @  <li><label><input type="checkbox" name="a5"%s(oa['5'])>
  @  Moderate Forum%s(B('5'))</label>
  @  <li><label><input type="checkbox" name="a6"%s(oa['6'])>
  @  Supervise Forum%s(B('6'))</label>
  @  <li><label><input type="checkbox" name="a7"%s(oa['7'])>
  @  Email Alerts%s(B('7'))</label>
  @  <li><label><input type="checkbox" name="aA"%s(oa['A'])>
  @  Send Announcements%s(B('A'))</label>
  @  <li><label><input type="checkbox" name="aC"%s(oa['C'])>
  @  Chatroom%s(B('C'))</label>
  @  <li><label><input type="checkbox" name="aD"%s(oa['D'])>
  @  Enable Debug%s(B('D'))</label>
  @ </ul></div>
  @   </td>
  @ </tr>
  @ <tr>
  @   <td class="usetupEditLabel">Selected Cap:</td>
  @   <td>
  @     <span id="usetupEditCapability">(missing JS?)</span>
  @     <a href="%R/setup_ucap_list">(key)</a>
  @   </td>
  @ </tr>
  if( !login_is_special(zLogin) ){
    @ <tr>
    @   <td align="right" id="supw">Password:</td>
    if( zPw[0] ){
      /* Obscure the password for all users */
      @   <td><input aria-labelledby="supw" type="password" autocomplete="off" \
      @   name="pw" value="**********">
      @   (Leave unchanged to retain password)</td>
    }else{
      /* Show an empty password as an empty input field */
      char *zRPW = fossil_random_password(12);
      @   <td><input aria-labelledby="supw" type="password" name="pw" \
      @   autocomplete="off" value=""> Password suggestion: %z(zRPW)</td>
    }
    @ </tr>
  }
  zGroup = login_group_name();
  if( zGroup ){
    @ <tr>
    @ <td valign="top" align="right">Scope:</td>
    @ <td valign="top">
    @ <input type="radio" name="all" checked value="0">
    @ Apply changes to this repository only.<br>
    @ <input type="radio" name="all" value="1">
    @ Apply changes to all repositories in the "<b>%h(zGroup)</b>"
    @ login group.</td></tr>
  }
  if( !higherUser ){
    if( zDeleteVerify ){
      @ <tr>
      @   <td valign="top" align="right">Verify:</td>
      @   <td><label><input type="checkbox" name="verifydelete">\
      @   Confirm Delete \
      @   <span class="loginError">&larr; %h(zDeleteVerify)</span>
      @   </label></td>
      @ <tr>
    }
    @ <tr>
    @   <td>&nbsp;</td>
    @   <td><input type="submit" name="apply" value="Apply Changes">
    if( !login_is_special(zLogin) ){
      @   <input type="submit" name="delete" value="Delete User">
    }
    @   <input type="submit" name="can" value="Cancel"></td>
    @ </tr>
  }
  @ </table>
  @ </div></form>
  @ </div>
  builtin_request_js("useredit.js");
  @ <hr>
  @ <h1>Notes On Privileges And Capabilities:</h1>
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
  style_finish_page();
}

/*
** WEBPAGE: setup_uinfo
**
** Detailed information about a user account, available to administrators
** only.
**
**    u=UID
**    l=LOGIN
*/
void setup_uinfo_page(void){
  Stmt q;
  Blob sql;
  const char *zLogin;
  int uid;

  /* Must have ADMIN privileges to access this page
  */
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(0); return; }
  style_set_current_feature("setup");
  zLogin = P("l");
  uid = atoi(PD("u","0"));
  if( zLogin==0 && uid==0 ){
    uid = db_int(1,"SELECT uid FROM user");
  }
  blob_init(&sql, 0, 0);
  blob_append_sql(&sql,
    "SELECT "
       /*  0 */ "uid,"
       /*  1 */ "login,"
       /*  2 */ "cap,"
       /*  3 */ "cookie,"
       /*  4 */ "datetime(cexpire),"
       /*  5 */ "info,"
       /*  6 */ "datetime(user.mtime,'unixepoch'),"
  );
  if( db_table_exists("repository","subscriber") ){
    blob_append_sql(&sql,
      /*  7 */ "subscriberId,"
      /*  8 */ "semail,"
      /*  9 */ "sverified,"
      /* 10 */ "date(lastContact+2440587.5)"
      " FROM user LEFT JOIN subscriber ON suname=login"
    );
  }else{
    blob_append_sql(&sql,
      /*  7 */ "NULL,"
      /*  8 */ "NULL,"
      /*  9 */ "NULL,"
      /* 10 */ "NULL"
      " FROM user"
    );
  }
  if( zLogin!=0 ){
    blob_append_sql(&sql, " WHERE login=%Q", zLogin);
  }else{
    blob_append_sql(&sql, " WHERE uid=%d", uid);
  }
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_zero(&sql);
  if( db_step(&q)!=SQLITE_ROW ){
    style_header("No Such User");
    if( zLogin ){
      @ <p>Cannot find any information on user %h(zLogin).
    }else{
      @ <p>Cannot find any information on userid %d(uid).
    }
    style_finish_page();
    db_finalize(&q);
    return;
  }
  style_header("User %h", db_column_text(&q,1));
  @ <table class="label-value">
  @ <tr><th>uid:</th><td>%d(db_column_int(&q,0))
  @  (<a href="%R/setup_uedit?id=%d(db_column_int(&q,0))">edit</a>)</td></tr>
  @ <tr><th>login:</th><td>%h(db_column_text(&q,1))</td></tr>
  @ <tr><th>capabilities:</th><td>%h(db_column_text(&q,2))</td></tr>
  @ <tr><th valign="top">info:</th>
  @ <td valign="top"><span style='white-space:pre-line;'>\
  @ %h(db_column_text(&q,5))</span></td></tr>
  @ <tr><th>user.mtime:</th><td>%h(db_column_text(&q,6))</td></tr>
  if( db_column_type(&q,7)!=SQLITE_NULL ){
    @ <tr><th>subscriberId:</th><td>%d(db_column_int(&q,7))
    @  (<a href="%R/alerts?sid=%d(db_column_int(&q,7))">edit</a>)</td></tr>
    @ <tr><th>semail:</th><td>%h(db_column_text(&q,8))</td></tr>
    @ <tr><th>verified:</th><td>%s(db_column_int(&q,9)?"yes":"no")</td></th>
    @ <tr><th>lastContact:</th><td>%h(db_column_text(&q,10))</td></tr>
  }
  @ </table>
  db_finalize(&q);
  style_finish_page();
}
