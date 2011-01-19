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
** There are two special-case user-ids: "anonymous" and "nobody".
** The capabilities of the nobody user are available to anyone,
** regardless of whether or not they are logged in.  The capabilities
** of anonymous are only available after logging in, but the login
** screen displays the password for the anonymous login, so this
** should not prevent a human user from doing so.
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
** Return the name of the login cookie
*/
static char *login_cookie_name(void){
  static char *zCookieName = 0;
  if( zCookieName==0 ){
    unsigned int h = 0;
    const char *z = g.zBaseURL;
    while( *z ){ h = (h<<3) ^ (h>>26) ^ *(z++); }
    zCookieName = mprintf("fossil_login_%08x", h);
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
** The IP address of the client is stored as part of the anonymous
** login cookie for additional security.  But some clients are behind
** firewalls that shift the IP address with each HTTP request.  To
** allow such (broken) clients to log in, extract just a prefix of the
** IP address.  
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
** Make a record of a login attempt, if login record keeping is enabled.
*/
static void record_login_attempt(
  const char *zUsername,     /* Name of user logging in */
  const char *zIpAddr,       /* IP address from which they logged in */
  int bSuccess               /* True if the attempt was a success */
){
  if( !db_get_boolean("access-log", 0) ) return;
  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS %s.accesslog("
    "  uname TEXT,"
    "  ipaddr TEXT,"
    "  success BOOLEAN,"
    "  mtime TIMESTAMP"
    ");"
    "INSERT INTO accesslog(uname,ipaddr,success,mtime)"
    "VALUES(%Q,%Q,%d,julianday('now'));",
    db_name("repository"), zUsername, zIpAddr, bSuccess
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

  login_check_credentials();
  zUsername = P("u");
  zPasswd = P("p");
  anonFlag = P("anon")!=0;
  if( P("out")!=0 ){
    const char *zCookieName = login_cookie_name();
    cgi_set_cookie(zCookieName, "", 0, -86400);
    redirect_to_g();
  }
  if( g.okPassword && zPasswd && (zNew1 = P("n1"))!=0 && (zNew2 = P("n2"))!=0 ){
    zSha1Pw = sha1_shared_secret(zPasswd, g.zLogin);
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
      char *zNewPw = sha1_shared_secret(zNew1, g.zLogin);
      db_multi_exec(
         "UPDATE user SET pw=%Q WHERE uid=%d", zNewPw, g.userUid
      );
      redirect_to_g();
      return;
    }
  }
  zIpAddr = PD("REMOTE_ADDR","nil");
  uid = isValidAnonymousLogin(zUsername, zPasswd);
  if( uid>0 ){
    char *zNow;                  /* Current time (julian day number) */
    char *zCookie;               /* The login cookie */
    const char *zCookieName;     /* Name of the login cookie */
    Blob b;                      /* Blob used during cookie construction */

    zCookieName = login_cookie_name();
    zNow = db_text("0", "SELECT julianday('now')");
    blob_init(&b, zNow, -1);
    blob_appendf(&b, "/%z/%s", ipPrefix(zIpAddr), db_get("captcha-secret",""));
    sha1sum_blob(&b, &b);
    zCookie = sqlite3_mprintf("anon/%s/%s", zNow, blob_buffer(&b));
    blob_reset(&b);
    free(zNow);
    cgi_set_cookie(zCookieName, zCookie, 0, 6*3600);
    record_login_attempt("anonyous", zIpAddr, 1);
    redirect_to_g();
  }
  if( zUsername!=0 && zPasswd!=0 && zPasswd[0]!=0 ){
    zSha1Pw = sha1_shared_secret(zPasswd, zUsername);
    uid = db_int(0,
        "SELECT uid FROM user"
        " WHERE login=%Q"
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
      char *zCookie;
      const char *zCookieName = login_cookie_name();
      const char *zExpire = db_get("cookie-expire","8766");
      int expires = atoi(zExpire)*3600;
      const char *zIpAddr = PD("REMOTE_ADDR","nil");
 
      zCookie = db_text(0, "SELECT '%d/' || hex(randomblob(25))", uid);
      cgi_set_cookie(zCookieName, zCookie, 0, expires);
      record_login_attempt(zUsername, zIpAddr, 1);
      db_multi_exec(
        "UPDATE user SET cookie=%Q, ipaddr=%Q, "
        "  cexpire=julianday('now')+%d/86400.0 WHERE uid=%d",
        zCookie, zIpAddr, expires, uid
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
  @   <td><input type="submit" name="in" value="Login" /></td>
  @ </tr>
  @ </table>
  @ <script type="text/JavaScript">document.getElementById('u').focus()</script>
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
** This routine examines the login cookie to see if it exists and
** and is valid.  If the login cookie checks out, it then sets 
** g.zUserUuid appropriately.
**
*/
void login_check_credentials(void){
  int uid = 0;                  /* User id */
  const char *zCookie;          /* Text of the login cookie */
  const char *zRemoteAddr;      /* IP address of the requestor */
  const char *zCap = 0;         /* Capability string */

  /* Only run this check once.  */
  if( g.userUid!=0 ) return;


  /* If the HTTP connection is coming over 127.0.0.1 and if
  ** local login is disabled and if we are using HTTP and not HTTPS, 
  ** then there is no need to check user credentials.
  **
  */
  zRemoteAddr = PD("REMOTE_ADDR","nil");
  if( strcmp(zRemoteAddr, "127.0.0.1")==0
   && db_get_int("localauth",0)==0
   && P("HTTPS")==0
  ){
    uid = db_int(0, "SELECT uid FROM user WHERE cap LIKE '%%s%%'");
    g.zLogin = db_text("?", "SELECT login FROM user WHERE uid=%d", uid);
    zCap = "s";
    g.noPswd = 1;
    sqlite3_snprintf(sizeof(g.zCsrfToken), g.zCsrfToken, "localhost");
  }

  /* Check the login cookie to see if it matches a known valid user.
  */
  if( uid==0 && (zCookie = P(login_cookie_name()))!=0 ){
    if( fossil_isdigit(zCookie[0]) ){
      /* Cookies of the form "uid/randomness".  There must be a
      ** corresponding entry in the user table. */
      uid = db_int(0, 
            "SELECT uid FROM user"
            " WHERE uid=%d"
            "   AND cookie=%Q"
            "   AND ipaddr=%Q"
            "   AND cexpire>julianday('now')",
            atoi(zCookie), zCookie, zRemoteAddr
         );
    }else if( memcmp(zCookie,"anon/",5)==0 ){
      /* Cookies of the form "anon/TIME/HASH".  The TIME must not be
      ** too old and the sha1 hash of TIME+IPADDR+SECRET must match HASH.
      ** SECRET is the "captcha-secret" value in the repository.
      */
      double rTime;
      int i;
      Blob b;
      rTime = atof(&zCookie[5]);
      for(i=5; zCookie[i] && zCookie[i]!='/'; i++){}
      blob_init(&b, &zCookie[5], i-5);
      if( zCookie[i]=='/' ){ i++; }
      blob_append(&b, "/", 1);
      blob_appendf(&b, "%z/%s", ipPrefix(zRemoteAddr),
                   db_get("captcha-secret",""));
      sha1sum_blob(&b, &b);
      uid = db_int(0, 
          "SELECT uid FROM user WHERE login='anonymous'"
          " AND length(cap)>0"
          " AND length(pw)>0"
          " AND %f+0.25>julianday('now')"
          " AND %Q=%Q",
          rTime, &zCookie[i], blob_buffer(&b)
      );
      blob_reset(&b);
    }
    sqlite3_snprintf(sizeof(g.zCsrfToken), g.zCsrfToken, "%.10s", zCookie);
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
** Add the default privileges of users "nobody" and "anonymous" as appropriate
** for the user g.zLogin.
*/
void login_set_anon_nobody_capabilities(void){
  static int once = 1;
  if( g.zLogin && once ){
    const char *zCap;
    /* All logged-in users inherit privileges from "nobody" */
    zCap = db_text("", "SELECT cap FROM user WHERE login = 'nobody'");
    login_set_capabilities(zCap);
    if( fossil_strcmp(g.zLogin, "nobody")!=0 ){
      /* All logged-in users inherit privileges from "anonymous" */
      zCap = db_text("", "SELECT cap FROM user WHERE login = 'anonymous'");
      login_set_capabilities(zCap);
    }
    once = 0;
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
      /* case 'x': */
      /* case 'y': */
      case 'z':  rc = g.okZip;       break;
      default:   rc = 0;             break;
    }
  }
  return rc;
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
        char *zPw = sha1_shared_secret(blob_str(&passwd), blob_str(&login));
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
        cgi_set_cookie(zCookieName, zCookie, 0, expires);
        record_login_attempt(zUsername, zIpAddr, 1);
        db_multi_exec(
            "UPDATE user SET cookie=%Q, ipaddr=%Q, "
            "  cexpire=julianday('now')+%d/86400.0 WHERE uid=%d",
            zCookie, zIpAddr, expires, uid
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
