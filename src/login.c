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
** This file contains code for generating the login and logout screens.
**
** Notes:
**
** There are four special-case user-ids:  "anonymous", "nobody",
** "developer" and "reader".
**
** The capabilities of the nobody user are available to anyone,
** regardless of whether or not they are logged in.  The capabilities
** of anonymous are only available after logging in, but the login
** screen displays the password for the anonymous login, so this
** should not prevent a human user from doing so.  The capabilities
** of developer and reader are inherited by any user that has the
** "v" and "u" capabilities, respectively.
**
** The nobody user has capabilities that you want spiders to have.
** The anonymous user has capabilities that you want people without
** logins to have.
**
** Of course, a sophisticated spider could easily circumvent the
** anonymous login requirement and walk the website.  But that is
** not really the point.  The anonymous login keeps search-engine
** crawlers and site download tools like wget from walking change
** logs and downloading diffs of very version of the archive that
** has ever existed, and things like that.
*/
#include "config.h"
#include "login.h"
#if defined(_WIN32)  
#  include <windows.h>           /* for Sleep */
#  if defined(__MINGW32__) || defined(_MSC_VER)
#    define sleep Sleep            /* windows does not have sleep, but Sleep */
#  endif
#endif
#include <time.h>

/*
** Return the login-group name.  Or return 0 if this repository is
** not a member of a login-group.
*/
const char *login_group_name(void){
  static const char *zGroup = 0;
  static int once = 1;
  if( once ){
    zGroup = db_get("login-group-name", 0);
    once = 0;
  }
  return zGroup;
}

/*
** Return a path appropriate for setting a cookie.
**
** The path is g.zTop for single-repo cookies.  It is "/" for
** cookies of a login-group.
*/
static const char *login_cookie_path(void){
  if( login_group_name()==0 ){
    return g.zTop;
  }else{
    return "/";
  }
}

/*
** Return the name of the login cookie.
**
** The login cookie name is always of the form:  fossil-XXXXXXXXXXXXXXXX
** where the Xs are the first 16 characters of the login-group-code or
** of the project-code if we are not a member of any login-group.
*/
static char *login_cookie_name(void){
  static char *zCookieName = 0;
  if( zCookieName==0 ){
    zCookieName = db_text(0,
       "SELECT 'fossil-' || substr(value,1,16)"
       "  FROM config"
       " WHERE name IN ('project-code','login-group-code')"
       " ORDER BY name;"
    );
  }
  return zCookieName;
}

/*
** Redirect to the page specified by the "g" query parameter.
** Or if there is no "g" query parameter, redirect to the homepage.
*/
static void redirect_to_g(void){
  const char *zGoto = P("g");
  if( zGoto ){
    cgi_redirect(zGoto);
  }else{
    fossil_redirect_home();
  }
}

/*
** The IP address of the client is stored as part of login cookies.
** But some clients are behind firewalls that shift the IP address 
** with each HTTP request.  To allow such (broken) clients to log in, 
** extract just a prefix of the IP address.  
*/
static char *ipPrefix(const char *zIP){
  int i, j; 
  for(i=j=0; zIP[i]; i++){
    if( zIP[i]=='.' ){
      j++;
      if( j==2 ) break;
    }
  }
  return mprintf("%.*s", i, zIP);
}

/*
** Return an abbreviated project code.  The abbreviation is the first
** 16 characters of the project code.
**
** Memory is obtained from malloc.
*/
static char *abbreviated_project_code(const char *zFullCode){
  return mprintf("%.16s", zFullCode);
}


/*
** Check to see if the anonymous login is valid.  If it is valid, return
** the userid of the anonymous user.
*/
static int isValidAnonymousLogin(
  const char *zUsername,  /* The username.  Must be "anonymous" */
  const char *zPassword   /* The supplied password */
){
  const char *zCS;        /* The captcha seed value */
  const char *zPw;        /* The correct password shown in the captcha */
  int uid;                /* The user ID of anonymous */

  if( zUsername==0 ) return 0;
  if( zPassword==0 ) return 0;
  if( strcmp(zUsername,"anonymous")!=0 ) return 0;
  zCS = P("cs");   /* The "cs" parameter is the "captcha seed" */
  if( zCS==0 ) return 0;
  zPw = captcha_decode((unsigned int)atoi(zCS));
  if( fossil_stricmp(zPw, zPassword)!=0 ) return 0;
  uid = db_int(0, "SELECT uid FROM user WHERE login='anonymous'"
                  " AND length(pw)>0 AND length(cap)>0");
  return uid;
}

/*
** Make sure the accesslog table exists.  Create it if it does not
*/
void create_accesslog_table(void){
  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS %s.accesslog("
    "  uname TEXT,"
    "  ipaddr TEXT,"
    "  success BOOLEAN,"
    "  mtime TIMESTAMP"
    ");", db_name("repository")
  );
}

/*
** Make a record of a login attempt, if login record keeping is enabled.
*/
static void record_login_attempt(
  const char *zUsername,     /* Name of user logging in */
  const char *zIpAddr,       /* IP address from which they logged in */
  int bSuccess               /* True if the attempt was a success */
){
  if( !db_get_boolean("access-log", 0) ) return;
  create_accesslog_table();
  db_multi_exec(
    "INSERT INTO accesslog(uname,ipaddr,success,mtime)"
    "VALUES(%Q,%Q,%d,julianday('now'));",
    zUsername, zIpAddr, bSuccess
  );
}

/*
** WEBPAGE: login
** WEBPAGE: logout
** WEBPAGE: my
**
** Generate the login page.
**
** There used to be a page named "my" that was designed to show information
** about a specific user.  The "my" page was linked from the "Logged in as USER"
** line on the title bar.  The "my" page was never completed so it is now
** removed.  Use this page as a placeholder in older installations.
*/
void login_page(void){
  const char *zUsername, *zPasswd;
  const char *zNew1, *zNew2;
  const char *zAnonPw = 0;
  int anonFlag;
  char *zErrMsg = "";
  int uid;                     /* User id loged in user */
  char *zSha1Pw;
  const char *zIpAddr;         /* IP address of requestor */
  char *zRemoteAddr;           /* Abbreviated IP address of requestor */

  login_check_credentials();
  zUsername = P("u");
  zPasswd = P("p");
  anonFlag = P("anon")!=0;
  if( P("out")!=0 ){
    /* To logout, change the cookie value to an empty string */
    const char *zCookieName = login_cookie_name();
    cgi_set_cookie(zCookieName, "", login_cookie_path(), -86400);
    redirect_to_g();
  }
  if( g.okPassword && zPasswd && (zNew1 = P("n1"))!=0 && (zNew2 = P("n2"))!=0 ){
    /* The user requests a password change */
    zSha1Pw = sha1_shared_secret(zPasswd, g.zLogin, 0);
    if( db_int(1, "SELECT 0 FROM user"
                  " WHERE uid=%d AND (pw=%Q OR pw=%Q)", 
                  g.userUid, zPasswd, zSha1Pw) ){
      sleep(1);
      zErrMsg = 
         @ <p><span class="loginError">
         @ You entered an incorrect old password while attempting to change
         @ your password.  Your password is unchanged.
         @ </span></p>
      ;
    }else if( fossil_strcmp(zNew1,zNew2)!=0 ){
      zErrMsg = 
         @ <p><span class="loginError">
         @ The two copies of your new passwords do not match.
         @ Your password is unchanged.
         @ </span></p>
      ;
    }else{
      char *zNewPw = sha1_shared_secret(zNew1, g.zLogin, 0);
      char *zChngPw;
      char *zErr;
      db_multi_exec(
         "UPDATE user SET pw=%Q WHERE uid=%d", zNewPw, g.userUid
      );
      fossil_free(zNewPw);
      zChngPw = mprintf(
         "UPDATE user"
         "   SET pw=shared_secret(%Q,%Q,"
         "        (SELECT value FROM config WHERE name='project-code'))"
         " WHERE login=%Q",
         zNew1, g.zLogin, g.zLogin
      );
      if( login_group_sql(zChngPw, "<p>", "</p>\n", &zErr) ){
        zErrMsg = mprintf("<span class=\"loginError\">%s</span>", zErr);
        fossil_free(zErr);
      }else{
        redirect_to_g();
        return;
      }
    }
  }
  zIpAddr = PD("REMOTE_ADDR","nil");   /* Complete IP address for logging */
  zRemoteAddr = ipPrefix(zIpAddr);     /* Abbreviated IP address */
  uid = isValidAnonymousLogin(zUsername, zPasswd);
  if( uid>0 ){
    /* Successful login as anonymous.  Set a cookie that looks like
    ** this:
    **
    **    HASH/TIME/anonymous
    **
    ** Where HASH is the sha1sum of TIME/IPADDR/SECRET, in which IPADDR
    ** is the abbreviated IP address and SECRET is captcha-secret.
    */
    char *zNow;                  /* Current time (julian day number) */
    char *zCookie;               /* The login cookie */
    const char *zCookieName;     /* Name of the login cookie */
    Blob b;                      /* Blob used during cookie construction */

    zCookieName = login_cookie_name();
    zNow = db_text("0", "SELECT julianday('now')");
    blob_init(&b, zNow, -1);
    blob_appendf(&b, "/%s/%s", zRemoteAddr, db_get("captcha-secret",""));
    sha1sum_blob(&b, &b);
    zCookie = sqlite3_mprintf("%s/%s/anonymous", blob_buffer(&b), zNow);
    blob_reset(&b);
    free(zNow);
    cgi_set_cookie(zCookieName, zCookie, login_cookie_path(), 6*3600);
    record_login_attempt("anonymous", zIpAddr, 1);
    redirect_to_g();
  }
  if( zUsername!=0 && zPasswd!=0 && zPasswd[0]!=0 ){
    /* Attempting to log in as a user other than anonymous.
    */
    zSha1Pw = sha1_shared_secret(zPasswd, zUsername, 0);
    uid = db_int(0,
        "SELECT uid FROM user"
        " WHERE login=%Q"
        "   AND length(cap)>0 AND length(pw)>0"
        "   AND login NOT IN ('anonymous','nobody','developer','reader')"
        "   AND (pw=%Q OR pw=%Q)",
        zUsername, zPasswd, zSha1Pw
    );
    if( uid<=0 ){
      sleep(1);
      zErrMsg = 
         @ <p><span class="loginError">
         @ You entered an unknown user or an incorrect password.
         @ </span></p>
      ;
      record_login_attempt(zUsername, zIpAddr, 0);
    }else{
      /* Non-anonymous login is successful.  Set a cookie of the form:
      **
      **    HASH/PROJECT/LOGIN
      **
      ** where HASH is a random hex number, PROJECT is either project
      ** code prefix, and LOGIN is the user name.
      */
      char *zCookie;
      const char *zCookieName = login_cookie_name();
      const char *zExpire = db_get("cookie-expire","8766");
      int expires = atoi(zExpire)*3600;
      char *zCode = abbreviated_project_code(db_get("project-code",""));
      char *zHash;
  
      zHash = db_text(0, "SELECT hex(randomblob(25))");
      zCookie = mprintf("%s/%s/%s", zHash, zCode, zUsername);
      cgi_set_cookie(zCookieName, zCookie, login_cookie_path(), expires);
      record_login_attempt(zUsername, zIpAddr, 1);
      db_multi_exec(
        "UPDATE user SET cookie=%Q, ipaddr=%Q, "
        "  cexpire=julianday('now')+%d/86400.0 WHERE uid=%d",
        zHash, zRemoteAddr, expires, uid
      );
      redirect_to_g();
    }
  }
  style_header("Login/Logout");
  @ %s(zErrMsg)
  @ <form action="login" method="post">
  if( P("g") ){
    @ <input type="hidden" name="g" value="%h(P("g"))" />
  }
  @ <table class="login_out">
  @ <tr>
  @   <td class="login_out_label">User ID:</td>
  if( anonFlag ){
    @ <td><input type="text" id="u" name="u" value="anonymous" size="30" /></td>
  }else{
    @ <td><input type="text" id="u" name="u" value="" size="30" /></td>
  }
  @ </tr>
  @ <tr>
  @  <td class="login_out_label">Password:</td>
  @   <td><input type="password" id="p" name="p" value="" size="30" /></td>
  @ </tr>
  if( g.zLogin==0 ){
    zAnonPw = db_text(0, "SELECT pw FROM user"
                         " WHERE login='anonymous'"
                         "   AND cap!=''");
  }
  @ <tr>
  @   <td></td>
  @   <td><input type="submit" name="in" value="Login"
  @        onClick="chngAction(this.form)" /></td>
  @ </tr>
  @ </table>
  @ <script type="text/JavaScript">
  @   document.getElementById('u').focus()
  @   function chngAction(form){
  if( g.sslNotAvailable==0
   && memcmp(g.zBaseURL,"https:",6)!=0
   && db_get_boolean("https-login",0)
  ){
     char *zSSL = mprintf("https:%s", &g.zBaseURL[5]);
     @  if( form.u.value!="anonymous" ){
     @     form.action = "%h(zSSL)/login";
     @  }
  }
  @ }
  @ </script>
  if( g.zLogin==0 ){
    @ <p>Enter
  }else{
    @ <p>You are currently logged in as <b>%h(g.zLogin)</b></p>
    @ <p>To change your login to a different user, enter
  }
  @ your user-id and password at the left and press the
  @ "Login" button.  Your user name will be stored in a browser cookie.
  @ You must configure your web browser to accept cookies in order for
  @ the login to take.</p>
  if( db_get_boolean("self-register", 0) ){
    @ <p>If you do not have an account, you can 
    @ <a href="%s(g.zTop)/register?g=%T(P("G"))">create one</a>.
  }
  if( zAnonPw ){
    unsigned int uSeed = captcha_seed();
    char const *zDecoded = captcha_decode(uSeed);
    int bAutoCaptcha = db_get_boolean("auto-captcha", 1);
    char *zCaptcha = captcha_render(zDecoded);

    @ <p><input type="hidden" name="cs" value="%u(uSeed)" />
    @ Visitors may enter <b>anonymous</b> as the user-ID with
    @ the 8-character hexadecimal password shown below:</p>
    @ <div class="captcha"><table class="captcha"><tr><td><pre>
    @ %s(zCaptcha)
    @ </pre></td></tr></table>
    if( bAutoCaptcha ) {
        @ <input type="button" value="Fill out captcha"
        @  onclick="document.getElementById('u').value='anonymous';
        @           document.getElementById('p').value='%s(zDecoded)';" />
    }
    @ </div>
    free(zCaptcha);
  }
  if( g.zLogin ){
    @ <hr />
    @ <p>To log off the system (and delete your login cookie)
    @  press the following button:<br />
    @ <input type="submit" name="out" value="Logout" /></p>
  }
  @ </form>
  if( g.okPassword ){
    @ <hr />
    @ <p>To change your password, enter your old password and your
    @ new password twice below then press the "Change Password"
    @ button.</p>
    @ <form action="login" method="post">
    @ <table>
    @ <tr><td class="login_out_label">Old Password:</td>
    @ <td><input type="password" name="p" size="30" /></td></tr>
    @ <tr><td class="login_out_label">New Password:</td>
    @ <td><input type="password" name="n1" size="30" /></td></tr>
    @ <tr><td class="login_out_label">Repeat New Password:</td>
    @ <td><input type="password" name="n2" size="30" /></td></tr>
    @ <tr><td></td>
    @ <td><input type="submit" value="Change Password" /></td></tr>
    @ </table>
    @ </form>
  }
  style_footer();
}

/*
** Attempt to find login credentials for user zLogin on a peer repository
** with project code zCode.  Transfer those credentials to the local 
** repository.
**
** Return true if a transfer was made and false if not.
*/
static int login_transfer_credentials(
  const char *zLogin,          /* Login we are looking for */
  const char *zCode,           /* Project code of peer repository */
  const char *zHash,           /* HASH from login cookie HASH/CODE/LOGIN */
  const char *zRemoteAddr      /* Request comes from here */
){
  sqlite3 *pOther = 0;         /* The other repository */
  sqlite3_stmt *pStmt;         /* Query against the other repository */
  char *zSQL;                  /* SQL of the query against other repo */
  char *zOtherRepo;            /* Filename of the other repository */
  int rc;                      /* Result code from SQLite library functions */
  int nXfer = 0;               /* Number of credentials transferred */

  zOtherRepo = db_text(0, 
       "SELECT value FROM config WHERE name='peer-repo-%q'",
       zCode
  );
  if( zOtherRepo==0 ) return 0;  /* No such peer repository */

  rc = sqlite3_open(zOtherRepo, &pOther);
  if( rc==SQLITE_OK ){
    zSQL = mprintf(
      "SELECT cexpire FROM user"
      " WHERE cookie=%Q"
      "   AND ipaddr=%Q"
      "   AND login=%Q"
      "   AND length(cap)>0"
      "   AND length(pw)>0"
      "   AND cexpire>julianday('now')",
      zHash, zRemoteAddr, zLogin
    );
    pStmt = 0;
    rc = sqlite3_prepare_v2(pOther, zSQL, -1, &pStmt, 0);
    if( rc==SQLITE_OK && sqlite3_step(pStmt)==SQLITE_ROW ){
      db_multi_exec(
        "UPDATE user SET cookie=%Q, ipaddr=%Q, cexpire=%.17g"
        " WHERE login=%Q",
        zHash, zRemoteAddr,
        sqlite3_column_double(pStmt, 0), zLogin
      );
      nXfer++;
    }
    sqlite3_finalize(pStmt);
  }
  sqlite3_close(pOther);
  fossil_free(zOtherRepo);
  return nXfer;
}

/*
** Lookup the uid for a user with zLogin and zCookie and zRemoteAddr.
** Return 0 if not found.
*/
static int login_find_user(
  const char *zLogin,            /* User name */
  const char *zCookie,           /* Login cookie value */
  const char *zRemoteAddr        /* Abbreviated IP address for valid login */
){
  int uid;
  if( fossil_strcmp(zLogin, "anonymous")==0 ) return 0;
  if( fossil_strcmp(zLogin, "nobody")==0 ) return 0;
  if( fossil_strcmp(zLogin, "developer")==0 ) return 0;
  if( fossil_strcmp(zLogin, "reader")==0 ) return 0;
  uid = db_int(0, 
    "SELECT uid FROM user"
    " WHERE login=%Q"
    "   AND cookie=%Q"
    "   AND ipaddr=%Q"
    "   AND cexpire>julianday('now')"
    "   AND length(cap)>0"
    "   AND length(pw)>0",
    zLogin, zCookie, zRemoteAddr
  );
  return uid;
}

/*
** This routine examines the login cookie to see if it exists and
** and is valid.  If the login cookie checks out, it then sets 
** global variables appropriately.  Global variables set include
** g.userUid and g.zLogin and of the g.okRead family of permission
** booleans.
**
*/
void login_check_credentials(void){
  int uid = 0;                  /* User id */
  const char *zCookie;          /* Text of the login cookie */
  const char *zIpAddr;          /* Raw IP address of the requestor */
  char *zRemoteAddr;            /* Abbreviated IP address of the requestor */
  const char *zCap = 0;         /* Capability string */

  /* Only run this check once.  */
  if( g.userUid!=0 ) return;

  /* If the HTTP connection is coming over 127.0.0.1 and if
  ** local login is disabled and if we are using HTTP and not HTTPS, 
  ** then there is no need to check user credentials.
  **
  ** This feature allows the "fossil ui" command to give the user
  ** full access rights without having to log in.
  */
  zRemoteAddr = ipPrefix(zIpAddr = PD("REMOTE_ADDR","nil"));
  if( strcmp(zIpAddr, "127.0.0.1")==0
   && g.useLocalauth
   && db_get_int("localauth",0)==0
   && P("HTTPS")==0
  ){
    uid = db_int(0, "SELECT uid FROM user WHERE cap LIKE '%%s%%'");
    g.zLogin = db_text("?", "SELECT login FROM user WHERE uid=%d", uid);
    zCap = "sx";
    g.noPswd = 1;
    sqlite3_snprintf(sizeof(g.zCsrfToken), g.zCsrfToken, "localhost");
  }

  /* Check the login cookie to see if it matches a known valid user.
  */
  if( uid==0 && (zCookie = P(login_cookie_name()))!=0 ){
    /* Parse the cookie value up into HASH/ARG/USER */
    char *zHash = fossil_strdup(zCookie);
    char *zArg = 0;
    char *zUser = 0;
    int i, c;
    for(i=0; (c = zHash[i])!=0; i++){
      if( c=='/' ){
        zHash[i++] = 0;
        if( zArg==0 ){
          zArg = &zHash[i];
        }else{
          zUser = &zHash[i];
          break;
        }
      }
    }
    if( zUser==0 ){
      /* Invalid cookie */
    }else if( strcmp(zUser, "anonymous")==0 ){
      /* Cookies of the form "HASH/TIME/anonymous".  The TIME must not be
      ** too old and the sha1 hash of TIME/IPADDR/SECRET must match HASH.
      ** SECRET is the "captcha-secret" value in the repository.
      */
      double rTime = atof(zArg);
      Blob b;
      blob_zero(&b);
      blob_appendf(&b, "%s/%s/%s", 
                   zArg, zRemoteAddr, db_get("captcha-secret",""));
      sha1sum_blob(&b, &b);
      if( fossil_strcmp(zHash, blob_str(&b))==0 ){
        uid = db_int(0, 
            "SELECT uid FROM user WHERE login='anonymous'"
            " AND length(cap)>0"
            " AND length(pw)>0"
            " AND %.17g+0.25>julianday('now')",
            rTime
        );
      }
      blob_reset(&b);
    }else{
      /* Cookies of the form "HASH/CODE/USER".  Search first in the
      ** local user table, then the user table for project CODE if we
      ** are part of a login-group.
      */
      uid = login_find_user(zUser, zHash, zRemoteAddr);
      if( uid==0 && login_transfer_credentials(zUser,zArg,zHash,zRemoteAddr) ){
        uid = login_find_user(zUser, zHash, zRemoteAddr);
        if( uid ) record_login_attempt(zUser, zIpAddr, 1);
      }
    }
    sqlite3_snprintf(sizeof(g.zCsrfToken), g.zCsrfToken, "%.10s", zHash);
  }

  /* If no user found and the REMOTE_USER environment variable is set,
  ** the accept the value of REMOTE_USER as the user.
  */
  if( uid==0 ){
    const char *zRemoteUser = P("REMOTE_USER");
    if( zRemoteUser && db_get_boolean("remote_user_ok",0) ){
      uid = db_int(0, "SELECT uid FROM user WHERE login=%Q"
                      " AND length(cap)>0 AND length(pw)>0", zRemoteUser);
    }
  }

  /* If no user found yet, try to log in as "nobody" */
  if( uid==0 ){
    uid = db_int(0, "SELECT uid FROM user WHERE login='nobody'");
    if( uid==0 ){
      /* If there is no user "nobody", then make one up - with no privileges */
      uid = -1;
      zCap = "";
    }
    sqlite3_snprintf(sizeof(g.zCsrfToken), g.zCsrfToken, "none");
  }

  /* At this point, we know that uid!=0.  Find the privileges associated
  ** with user uid.
  */
  assert( uid!=0 );
  if( zCap==0 ){
    Stmt s;
    db_prepare(&s, "SELECT login, cap FROM user WHERE uid=%d", uid);
    if( db_step(&s)==SQLITE_ROW ){
      g.zLogin = db_column_malloc(&s, 0);
      zCap = db_column_malloc(&s, 1);
    }
    db_finalize(&s);
    if( zCap==0 ){
      zCap = "";
    }
  }
  if( g.fHttpTrace && g.zLogin ){
    fprintf(stderr, "# login: [%s] with capabilities [%s]\n", g.zLogin, zCap);
  }

  /* Set the global variables recording the userid and login.  The
  ** "nobody" user is a special case in that g.zLogin==0.
  */
  g.userUid = uid;
  if( fossil_strcmp(g.zLogin,"nobody")==0 ){
    g.zLogin = 0;
  }

  /* Set the capabilities */
  login_set_capabilities(zCap);
  login_set_anon_nobody_capabilities();
}

/*
** Memory of settings
*/
static int login_anon_once = 1;

/*
** Add the default privileges of users "nobody" and "anonymous" as appropriate
** for the user g.zLogin.
*/
void login_set_anon_nobody_capabilities(void){
  if( g.zLogin && login_anon_once ){
    const char *zCap;
    /* All logged-in users inherit privileges from "nobody" */
    zCap = db_text("", "SELECT cap FROM user WHERE login = 'nobody'");
    login_set_capabilities(zCap);
    if( fossil_strcmp(g.zLogin, "nobody")!=0 ){
      /* All logged-in users inherit privileges from "anonymous" */
      zCap = db_text("", "SELECT cap FROM user WHERE login = 'anonymous'");
      login_set_capabilities(zCap);
    }
    login_anon_once = 0;
  }
}

/*
** Set the global capability flags based on a capability string.
*/
void login_set_capabilities(const char *zCap){
  static char *zDev = 0;
  static char *zUser = 0;
  int i;
  for(i=0; zCap[i]; i++){
    switch( zCap[i] ){
      case 's':   g.okSetup = 1;  /* Fall thru into Admin */
      case 'a':   g.okAdmin = g.okRdTkt = g.okWrTkt = g.okZip =
                              g.okRdWiki = g.okWrWiki = g.okNewWiki =
                              g.okApndWiki = g.okHistory = g.okClone = 
                              g.okNewTkt = g.okPassword = g.okRdAddr =
                              g.okTktFmt = g.okAttach = g.okApndTkt = 1;
                              /* Fall thru into Read/Write */
      case 'i':   g.okRead = g.okWrite = 1;                     break;
      case 'o':   g.okRead = 1;                                 break;
      case 'z':   g.okZip = 1;                                  break;

      case 'd':   g.okDelete = 1;                               break;
      case 'h':   g.okHistory = 1;                              break;
      case 'g':   g.okClone = 1;                                break;
      case 'p':   g.okPassword = 1;                             break;

      case 'j':   g.okRdWiki = 1;                               break;
      case 'k':   g.okWrWiki = g.okRdWiki = g.okApndWiki =1;    break;
      case 'm':   g.okApndWiki = 1;                             break;
      case 'f':   g.okNewWiki = 1;                              break;

      case 'e':   g.okRdAddr = 1;                               break;
      case 'r':   g.okRdTkt = 1;                                break;
      case 'n':   g.okNewTkt = 1;                               break;
      case 'w':   g.okWrTkt = g.okRdTkt = g.okNewTkt = 
                  g.okApndTkt = 1;                              break;
      case 'c':   g.okApndTkt = 1;                              break;
      case 't':   g.okTktFmt = 1;                               break;
      case 'b':   g.okAttach = 1;                               break;
      case 'x':   g.okPrivate = 1;                              break;

      /* The "u" privileges is a little different.  It recursively 
      ** inherits all privileges of the user named "reader" */
      case 'u': {
        if( zUser==0 ){
          zUser = db_text("", "SELECT cap FROM user WHERE login='reader'");
          login_set_capabilities(zUser);
        }
        break;
      }

      /* The "v" privileges is a little different.  It recursively 
      ** inherits all privileges of the user named "developer" */
      case 'v': {
        if( zDev==0 ){
          zDev = db_text("", "SELECT cap FROM user WHERE login='developer'");
          login_set_capabilities(zDev);
        }
        break;
      }
    }
  }
}

/*
** If the current login lacks any of the capabilities listed in
** the input, then return 0.  If all capabilities are present, then
** return 1.
*/
int login_has_capability(const char *zCap, int nCap){
  int i;
  int rc = 1;
  if( nCap<0 ) nCap = strlen(zCap);
  for(i=0; i<nCap && rc && zCap[i]; i++){
    switch( zCap[i] ){
      case 'a':  rc = g.okAdmin;     break;
      case 'b':  rc = g.okAttach;    break;
      case 'c':  rc = g.okApndTkt;   break;
      case 'd':  rc = g.okDelete;    break;
      case 'e':  rc = g.okRdAddr;    break;
      case 'f':  rc = g.okNewWiki;   break;
      case 'g':  rc = g.okClone;     break;
      case 'h':  rc = g.okHistory;   break;
      case 'i':  rc = g.okWrite;     break;
      case 'j':  rc = g.okRdWiki;    break;
      case 'k':  rc = g.okWrWiki;    break;
      /* case 'l': */
      case 'm':  rc = g.okApndWiki;  break;
      case 'n':  rc = g.okNewTkt;    break;
      case 'o':  rc = g.okRead;      break;
      case 'p':  rc = g.okPassword;  break;
      /* case 'q': */
      case 'r':  rc = g.okRdTkt;     break;
      case 's':  rc = g.okSetup;     break;
      case 't':  rc = g.okTktFmt;    break;
      /* case 'u': READER    */
      /* case 'v': DEVELOPER */
      case 'w':  rc = g.okWrTkt;     break;
      case 'x':  rc = g.okPrivate;   break;
      /* case 'y': */
      case 'z':  rc = g.okZip;       break;
      default:   rc = 0;             break;
    }
  }
  return rc;
}

/*
** Change the login to zUser.
*/
void login_as_user(const char *zUser){
  char *zCap = "";   /* New capabilities */

  /* Turn off all capabilities from prior logins */
  g.okSetup = 0;
  g.okAdmin = 0;
  g.okDelete = 0;
  g.okPassword = 0;
  g.okQuery = 0;
  g.okWrite = 0;
  g.okRead = 0;
  g.okHistory = 0;
  g.okClone = 0;
  g.okRdWiki = 0;
  g.okNewWiki = 0;
  g.okApndWiki = 0;
  g.okWrWiki = 0;
  g.okRdTkt = 0;
  g.okNewTkt = 0;
  g.okApndTkt = 0;
  g.okWrTkt = 0;
  g.okAttach = 0;
  g.okTktFmt = 0;
  g.okRdAddr = 0;
  g.okZip = 0;
  g.okPrivate = 0;

  /* Set the global variables recording the userid and login.  The
  ** "nobody" user is a special case in that g.zLogin==0.
  */
  g.userUid = db_int(0, "SELECT uid FROM user WHERE login=%Q", zUser);
  if( g.userUid==0 ){
    zUser = 0;
    g.userUid = db_int(0, "SELECT uid FROM user WHERE login='nobody'");
  }
  if( g.userUid ){
    zCap = db_text("", "SELECT cap FROM user WHERE uid=%d", g.userUid);
  }
  if( fossil_strcmp(zUser,"nobody")==0 ) zUser = 0;
  g.zLogin = fossil_strdup(zUser);

  /* Set the capabilities */
  login_set_capabilities(zCap);
  login_anon_once = 1;
  login_set_anon_nobody_capabilities();
}

/*
** Call this routine when the credential check fails.  It causes
** a redirect to the "login" page.
*/
void login_needed(void){
  const char *zUrl = PD("REQUEST_URI", "index");
  cgi_redirect(mprintf("login?g=%T", zUrl));
  /* NOTREACHED */
  assert(0);
}

/*
** Call this routine if the user lacks okHistory permission.  If
** the anonymous user has okHistory permission, then paint a mesage
** to inform the user that much more information is available by
** logging in as anonymous.
*/
void login_anonymous_available(void){
  if( !g.okHistory &&
      db_exists("SELECT 1 FROM user"
                " WHERE login='anonymous'"
                "   AND cap LIKE '%%h%%'") ){
    const char *zUrl = PD("REQUEST_URI", "index");
    @ <p>Many <span class="disabled">hyperlinks are disabled.</span><br />
    @ Use <a href="%s(g.zTop)/login?anon=1&amp;g=%T(zUrl)">anonymous login</a>
    @ to enable hyperlinks.</p>
  }
}

/*
** While rendering a form, call this routine to add the Anti-CSRF token
** as a hidden element of the form.
*/
void login_insert_csrf_secret(void){
  @ <input type="hidden" name="csrf" value="%s(g.zCsrfToken)" />
}

/*
** Before using the results of a form, first call this routine to verify
** that ths Anti-CSRF token is present and is valid.  If the Anti-CSRF token
** is missing or is incorrect, that indicates a cross-site scripting attach
** so emits an error message and abort.
*/
void login_verify_csrf_secret(void){
  if( g.okCsrf ) return;
  if( fossil_strcmp(P("csrf"), g.zCsrfToken)==0 ){
    g.okCsrf = 1;
    return;
  }
  fossil_fatal("Cross-site request forgery attempt");
}

/*
** WEBPAGE: register
**
** Generate the register page.
**
*/
void register_page(void){
  const char *zUsername, *zPasswd, *zConfirm, *zContact, *zCS, *zPw, *zCap;
  unsigned int uSeed;
  char const *zDecoded;
  char *zCaptcha;
  if( !db_get_boolean("self-register", 0) ){
    style_header("Registration not possible");
    @ <p>This project does not allow user self-registration. Please contact the
    @ project administrator to obtain an account.</p>
    style_footer();
    return;
  }

  style_header("Register");
  zUsername = P("u");
  zPasswd = P("p");
  zConfirm = P("cp");
  zContact = P("c");
  zCap = P("cap");
  zCS = P("cs"); /* Captcha Secret */

  /* Try to make any sense from user input. */
  if( P("new") ){
    if( zCS==0 ) fossil_redirect_home();  /* Forged request */
    zPw = captcha_decode((unsigned int)atoi(zCS));
    if( !(zUsername && zPasswd && zConfirm && zContact) ){
      @ <p><span class="loginError">
      @ All fields are obligatory.
      @ </span></p>
    }else if( strlen(zPasswd) < 6){
      @ <p><span class="loginError">
      @ Password too weak.
      @ </span></p>
    }else if( strcmp(zPasswd,zConfirm)!=0 ){
      @ <p><span class="loginError">
      @ The two copies of your new passwords do not match.
      @ </span></p>
    }else if( fossil_stricmp(zPw, zCap)!=0 ){
      @ <p><span class="loginError">
      @ Captcha text invalid.
      @ </span></p>
    }else{
      /* This almost is stupid copy-paste of code from user.c:user_cmd(). */
      Blob passwd, login, caps, contact;

      blob_init(&login, zUsername, -1);
      blob_init(&contact, zContact, -1);
      blob_init(&caps, db_get("default-perms", "u"), -1);
      blob_init(&passwd, zPasswd, -1);

      if( db_exists("SELECT 1 FROM user WHERE login=%B", &login) ){
        /* Here lies the reason I don't use zErrMsg - it would not substitute
         * this %s(zUsername), or at least I don't know how to force it to.*/
        @ <p><span class="loginError">
        @ %s(zUsername) already exists.
        @ </span></p>
      }else{
        char *zPw = sha1_shared_secret(blob_str(&passwd), blob_str(&login), 0);
        int uid;
        char *zCookie;
        const char *zCookieName;
        const char *zExpire;
        int expires;
        const char *zIpAddr;
        db_multi_exec(
            "INSERT INTO user(login,pw,cap,info)"
            "VALUES(%B,%Q,%B,%B)",
            &login, zPw, &caps, &contact
            );
        free(zPw);

        /* The user is registered, now just log him in. */
        uid = db_int(0, "SELECT uid FROM user WHERE login=%Q", zUsername);
        zCookieName = login_cookie_name();
        zExpire = db_get("cookie-expire","8766");
        expires = atoi(zExpire)*3600;
        zIpAddr = PD("REMOTE_ADDR","nil");

        zCookie = db_text(0, "SELECT '%d/' || hex(randomblob(25))", uid);
        cgi_set_cookie(zCookieName, zCookie, login_cookie_path(), expires);
        record_login_attempt(zUsername, zIpAddr, 1);
        db_multi_exec(
            "UPDATE user SET cookie=%Q, ipaddr=%Q, "
            "  cexpire=julianday('now')+%d/86400.0 WHERE uid=%d",
            zCookie, ipPrefix(zIpAddr), expires, uid
        );
        redirect_to_g();

      }
    }
  }

  /* Prepare the captcha. */
  uSeed = captcha_seed();
  zDecoded = captcha_decode(uSeed);
  zCaptcha = captcha_render(zDecoded);

  /* Print out the registration form. */
  @ <form action="register" method="post">
  if( P("g") ){
    @ <input type="hidden" name="g" value="%h(P("g"))" />
  }
  @ <p><input type="hidden" name="cs" value="%u(uSeed)" />
  @ <table class="login_out">
  @ <tr>
  @   <td class="login_out_label" align="right">User ID:</td>
  @   <td><input type="text" id="u" name="u" value="" size="30" /></td>
  @ </tr>
  @ <tr>
  @   <td class="login_out_label" align="right">Password:</td>
  @   <td><input type="password" id="p" name="p" value="" size="30" /></td>
  @ </tr>
  @ <tr>
  @   <td class="login_out_label" align="right">Confirm password:</td>
  @   <td><input type="password" id="cp" name="cp" value="" size="30" /></td>
  @ </tr>
  @ <tr>
  @   <td class="login_out_label" align="right">Contact info:</td>
  @   <td><input type="text" id="c" name="c" value="" size="30" /></td>
  @ </tr>
  @ <tr>
  @   <td class="login_out_label" align="right">Captcha text (below):</td>
  @   <td><input type="text" id="cap" name="cap" value="" size="30" /></td>
  @ </tr>
  @ <tr><td></td>
  @ <td><input type="submit" name="new" value="Register" /></td></tr>
  @ </table>
  @ <div class="captcha"><table class="captcha"><tr><td><pre>
  @ %s(zCaptcha)
  @ </pre></td></tr></table>
  @ </form>
  style_footer();

  free(zCaptcha);
}

/*
** Run SQL on the repository database for every repository in our
** login group.  The SQL is run in a separate database connection.
**
** Any members of the login group whose repository database file
** cannot be found is silently removed from the group.
**
** Error messages accumulate and are returned in *pzErrorMsg.  The
** memory used to hold these messages should be freed using
** fossil_free() if one desired to avoid a memory leak.  The
** zPrefix and zSuffix strings surround each error message.
**
** Return the number of errors.
*/
int login_group_sql(
  const char *zSql,        /* The SQL to run */
  const char *zPrefix,     /* Prefix to each error message */
  const char *zSuffix,     /* Suffix to each error message */
  char **pzErrorMsg        /* Write error message here, if not NULL */
){
  sqlite3 *pPeer;          /* Connection to another database */
  int nErr = 0;            /* Number of errors seen so far */
  int rc;                  /* Result code from subroutine calls */
  char *zErr;              /* SQLite error text */
  char *zSelfCode;         /* Project code for ourself */
  Blob err;                /* Accumulate errors here */
  Stmt q;                  /* Query of all peer-* entries in CONFIG */

  if( zPrefix==0 ) zPrefix = "";
  if( zSuffix==0 ) zSuffix = "";
  if( pzErrorMsg ) *pzErrorMsg = 0;
  zSelfCode = abbreviated_project_code(db_get("project-code", "x"));
  blob_zero(&err);
  db_prepare(&q, 
    "SELECT name, value FROM config"
    " WHERE name GLOB 'peer-repo-*'"
    "   AND name <> 'peer-repo-%q'"
    " ORDER BY +value",
    zSelfCode
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zRepoName = db_column_text(&q, 1);
    if( file_size(zRepoName)<0 ){
      /* Silently remove non-existant repositories from the login group. */
      const char *zLabel = db_column_text(&q, 0);
      db_multi_exec(
         "DELETE FROM config WHERE name GLOB 'peer-*-%q'",
         &zLabel[10]
      );
      continue;
    }
    rc = sqlite3_open_v2(zRepoName, &pPeer, SQLITE_OPEN_READWRITE, 0);
    if( rc!=SQLITE_OK ){
      blob_appendf(&err, "%s%s: %s%s", zPrefix, zRepoName,
                   sqlite3_errmsg(pPeer), zSuffix);
      nErr++;
      sqlite3_close(pPeer);
      continue;
    }
    sqlite3_create_function(pPeer, "shared_secret", 3, SQLITE_UTF8,
                            0, sha1_shared_secret_sql_function, 0, 0);
    zErr = 0;
    rc = sqlite3_exec(pPeer, zSql, 0, 0, &zErr);
    if( zErr ){
      blob_appendf(&err, "%s%s: %s%s", zPrefix, zRepoName, zErr, zSuffix);
      sqlite3_free(zErr);
      nErr++;
    }else if( rc!=SQLITE_OK ){
      blob_appendf(&err, "%s%s: %s%s", zPrefix, zRepoName,
                   sqlite3_errmsg(pPeer), zSuffix);
      nErr++;
    }
    sqlite3_close(pPeer);
  }
  db_finalize(&q);
  if( pzErrorMsg && blob_size(&err)>0 ){
    *pzErrorMsg = fossil_strdup(blob_str(&err));
  }
  blob_reset(&err);
  fossil_free(zSelfCode);
  return nErr;
}

/*
** Attempt to join a login-group.
**
** If problems arise, leave an error message in *pzErrMsg.
*/
void login_group_join(
  const char *zRepo,         /* Repository file in the login group */
  const char *zLogin,        /* Login name for the other repo */
  const char *zPassword,     /* Password to prove we are authorized to join */
  const char *zNewName,      /* Name of new login group if making a new one */
  char **pzErrMsg            /* Leave an error message here */
){
  Blob fullName;             /* Blob for finding full pathnames */
  sqlite3 *pOther;           /* The other repository */
  int rc;                    /* Return code from sqlite3 functions */
  char *zOtherProjCode;      /* Project code for pOther */
  char *zPwHash;             /* Password hash on pOther */
  char *zSelfRepo;           /* Name of our repository */
  char *zSelfLabel;          /* Project-name for our repository */
  char *zSelfProjCode;       /* Our project-code */
  char *zSql;                /* SQL to run on all peers */
  const char *zSelf;         /* The ATTACH name of our repository */

  *pzErrMsg = 0;   /* Default to no errors */
  zSelf = db_name("repository");

  /* Get the full pathname of the other repository */  
  file_canonical_name(zRepo, &fullName);
  zRepo = mprintf(blob_str(&fullName));
  blob_reset(&fullName);

  /* Get the full pathname for our repository.  Also the project code
  ** and project name for ourself. */
  file_canonical_name(g.zRepositoryName, &fullName);
  zSelfRepo = mprintf(blob_str(&fullName));
  blob_reset(&fullName);
  zSelfProjCode = db_get("project-code", "unknown");
  zSelfLabel = db_get("project-name", 0);
  if( zSelfLabel==0 ){
    zSelfLabel = zSelfProjCode;
  }

  /* Make sure we are not trying to join ourselves */
  if( strcmp(zRepo, zSelfRepo)==0 ){
    *pzErrMsg = mprintf("The \"other\" repository is the same as this one.");
    return;
  }

  /* Make sure the other repository is a valid Fossil database */
  if( file_size(zRepo)<0 ){
    *pzErrMsg = mprintf("repository file \"%s\" does not exist", zRepo);
    return;
  }
  rc = sqlite3_open(zRepo, &pOther);
  if( rc!=SQLITE_OK ){
    *pzErrMsg = mprintf(sqlite3_errmsg(pOther));
  }else{
    rc = sqlite3_exec(pOther, "SELECT count(*) FROM user", 0, 0, pzErrMsg);
  }
  sqlite3_close(pOther);
  if( rc ) return;

  /* Attach the other respository.  Make sure the username/password is
  ** valid and has Setup permission.
  */
  db_multi_exec("ATTACH %Q AS other", zRepo);
  zOtherProjCode = db_text("x", "SELECT value FROM other.config"
                                " WHERE name='project-code'");
  zPwHash = sha1_shared_secret(zPassword, zLogin, zOtherProjCode);
  if( !db_exists(
    "SELECT 1 FROM other.user"
    " WHERE login=%Q AND cap GLOB '*s*'"
    "   AND (pw=%Q OR pw=%Q)",
    zLogin, zPassword, zPwHash)
  ){
    db_multi_exec("DETACH other");
    *pzErrMsg = "The supplied username/password does not correspond to a"
                " user Setup permission on the other repository.";
    return;
  }

  /* Create all the necessary CONFIG table entries on both the
  ** other repository and on our own repository.
  */
  zSelfProjCode = abbreviated_project_code(zSelfProjCode);
  zOtherProjCode = abbreviated_project_code(zOtherProjCode);
  db_begin_transaction();
  db_multi_exec(
    "DELETE FROM %s.config WHERE name GLOB 'peer-*';"
    "INSERT INTO %s.config(name,value) VALUES('peer-repo-%s',%Q);"
    "INSERT INTO %s.config(name,value) "
    "  SELECT 'peer-name-%q', value FROM other.config"
    "   WHERE name='project-name';",
    zSelf,
    zSelf, zOtherProjCode, zRepo,
    zSelf, zOtherProjCode
  );
  db_multi_exec(
    "INSERT OR IGNORE INTO other.config(name,value)"
    " VALUES('login-group-name',%Q);"
    "INSERT OR IGNORE INTO other.config(name,value)"
    " VALUES('login-group-code',lower(hex(randomblob(8))));",
    zNewName
  );
  db_multi_exec(
    "REPLACE INTO %s.config(name,value)"
    "  SELECT name, value FROM other.config"
    "   WHERE name GLOB 'peer-*' OR name GLOB 'login-group-*'",
    zSelf
  );
  db_end_transaction(0);
  db_multi_exec("DETACH other");

  /* Propagate the changes to all other members of the login-group */
  zSql = mprintf(
    "BEGIN;"
    "REPLACE INTO config(name, value) VALUES('peer-name-%q', %Q);"
    "REPLACE INTO config(name, value) VALUES('peer-repo-%q', %Q);"
    "COMMIT;",
    zSelfProjCode, zSelfLabel, zSelfProjCode, zSelfRepo
  );
  login_group_sql(zSql, "<li> ", "</li>", pzErrMsg);
  fossil_free(zSql);
}

/*
** Leave the login group that we are currently part of.
*/
void login_group_leave(char **pzErrMsg){
  char *zProjCode;
  char *zSql;

  *pzErrMsg = 0;
  zProjCode = abbreviated_project_code(db_get("project-code","x"));
  zSql = mprintf(
    "DELETE FROM config WHERE name GLOB 'peer-*-%q';"
    "DELETE FROM config"
    " WHERE name='login-group-name'"
    "   AND (SELECT count(*) FROM config WHERE name GLOB 'peer-*')==0;",
    zProjCode
  );
  fossil_free(zProjCode);
  login_group_sql(zSql, "<li> ", "</li>", pzErrMsg);
  fossil_free(zSql);
  db_multi_exec(
    "DELETE FROM config "
    " WHERE name GLOB 'peer-*'"
    "    OR name GLOB 'login-group-*';"
  );
}
