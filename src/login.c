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
    int n = strlen(g.zTop);
    zCookieName = malloc( n*2+16 );
                      /* 0123456789 12345 */
    strcpy(zCookieName, "fossil_login_");
    encode16((unsigned char*)g.zTop, (unsigned char*)&zCookieName[13], n);
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
  if( strcasecmp(zPw, zPassword)!=0 ) return 0;
  uid = db_int(0, "SELECT uid FROM user WHERE login='anonymous'"
                  " AND length(pw)>0 AND length(cap)>0");
  return uid;
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
         @ <p><font color="red">
         @ You entered an incorrect old password while attempting to change
         @ your password.  Your password is unchanged.
         @ </font></p>
      ;
    }else if( strcmp(zNew1,zNew2)!=0 ){
      zErrMsg = 
         @ <p><font color="red">
         @ The two copies of your new passwords do not match.
         @ Your password is unchanged.
         @ </font></p>
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
  uid = isValidAnonymousLogin(zUsername, zPasswd);
  if( uid>0 ){
    char *zNow;                  /* Current time (julian day number) */
    const char *zIpAddr;         /* IP address of requestor */
    char *zCookie;               /* The login cookie */
    const char *zCookieName;     /* Name of the login cookie */
    Blob b;                      /* Blob used during cookie construction */

    zIpAddr = PD("REMOTE_ADDR","nil");
    zCookieName = login_cookie_name();
    zNow = db_text("0", "SELECT julianday('now')");
    blob_init(&b, zNow, -1);
    blob_appendf(&b, "/%z/%s", ipPrefix(zIpAddr), db_get("captcha-secret",""));
    sha1sum_blob(&b, &b);
    zCookie = sqlite3_mprintf("anon/%s/%s", zNow, blob_buffer(&b));
    blob_reset(&b);
    free(zNow);
    cgi_set_cookie(zCookieName, zCookie, 0, 6*3600);
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
         @ <p><font color="red">
         @ You entered an unknown user or an incorrect password.
         @ </font></p>
      ;
    }else{
      char *zCookie;
      const char *zCookieName = login_cookie_name();
      const char *zExpire = db_get("cookie-expire","8766");
      int expires = atoi(zExpire)*3600;
      const char *zIpAddr = PD("REMOTE_ADDR","nil");
 
      zCookie = db_text(0, "SELECT '%d/' || hex(randomblob(25))", uid);
      cgi_set_cookie(zCookieName, zCookie, 0, expires);
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
  @ <table align="left" hspace="10">
  @ <tr>
  @   <td align="right">User ID:</td>
  if( anonFlag ){
    @   <td><input type="text" id="u" name="u" value="anonymous" size=30 /></td>
  }else{
    @   <td><input type="text" id="u" name="u" value="" size=30 /></td>
  }
  @ </tr>
  @ <tr>
  @  <td align="right">Password:</td>
  @   <td><input type="password" id="p" name="p" value="" size=30 /></td>
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
  @ <script  type="text/JavaScript">document.getElementById('u').focus()</script>
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
  if( zAnonPw ){
    unsigned int uSeed = captcha_seed();
    char const *zDecoded = captcha_decode(uSeed);
    int bAutoCaptcha = db_get_boolean("auto-captcha", 1);
    char *zCaptcha = captcha_render(zDecoded);

    @ <input type="hidden" name="cs" value="%u(uSeed)" />
    @ <p>Visitors may enter <b>anonymous</b> as the user-ID with
    @ the 8-character hexadecimal password shown below:</p>
    @ <center><table border="1" cellpadding="10"><tr><td><pre>
    @ %s(zCaptcha)
    @ </pre></td></tr></table>
    if( bAutoCaptcha ) {
        @ <input type="button" value="Fill out captcha"
        @  onclick="document.getElementById('u').value='anonymous';
        @           document.getElementById('p').value='%s(zDecoded)';" />
    }
    @ </center>
    free(zCaptcha);
  }
  if( g.zLogin ){
    @ <br><hr>
    @ <p>To log off the system (and delete your login cookie)
    @  press the following button:<br>
    @ <input type="submit" name="out" value="Logout" /></p>
  }
  @ </form>
  if( g.okPassword ){
    @ <br><hr>
    @ <p>To change your password, enter your old password and your
    @ new password twice below then press the "Change Password"
    @ button.</p>
    @ <form action="login" method="POST">
    @ <table>
    @ <tr><td align="right">Old Password:</td>
    @ <td><input type="password" name="p" size=30 /></td></tr>
    @ <tr><td align="right">New Password:</td>
    @ <td><input type="password" name="n1" size=30 /></td></tr>
    @ <tr><td align="right">Repeat New Password:</td>
    @ <td><input type="password" name="n2" size=30 /></td></tr>
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
    strcpy(g.zCsrfToken, "localhost");
  }

  /* Check the login cookie to see if it matches a known valid user.
  */
  if( uid==0 && (zCookie = P(login_cookie_name()))!=0 ){
    if( isdigit(zCookie[0]) ){
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
    strcpy(g.zCsrfToken, "none");
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
  if( g.zLogin && strcmp(g.zLogin,"nobody")==0 ){
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
    if( strcmp(g.zLogin, "anonymous")!=0 ){
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
  const char *zCsrf;            /* The CSRF secret */
  if( g.okCsrf ) return;
  if( (zCsrf = P("csrf"))!=0 && strcmp(zCsrf, g.zCsrfToken)==0 ){
    g.okCsrf = 1;
    return;
  }
  fossil_fatal("Cross-site request forgery attempt");
}
