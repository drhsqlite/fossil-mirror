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
** Compute an appropriate Anti-CSRF token into g.zCsrfToken[].
*/
static void login_create_csrf_secret(const char *zSeed){
  unsigned char zResult[20];
  unsigned int i;

  sha1sum_binary(zSeed, zResult);
  for(i=0; i<sizeof(g.zCsrfToken)-1; i++){
    g.zCsrfToken[i] = "abcdefghijklmnopqrstuvwxyz"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                      "0123456789-/"[zResult[i]%64];
  }
  g.zCsrfToken[i] = 0;
}

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
const char *login_cookie_path(void){
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
char *login_cookie_name(void){
  static char *zCookieName = 0;
  if( zCookieName==0 ){
    zCookieName = db_text(0,
       "SELECT 'fossil-' || substr(value,1,16)"
       "  FROM config"
       " WHERE name IN ('project-code','login-group-code')"
       " ORDER BY name /*sort*/"
    );
  }
  return zCookieName;
}

/*
** Redirect to the page specified by the "g" query parameter.
** Or if there is no "g" query parameter, redirect to the homepage.
*/
NORETURN void login_redirect_to_g(void){
  const char *zGoto = P("g");
  if( zGoto ){
    cgi_redirectf("%R/%s",zGoto);
  }else if( (zGoto = P("fossil-goto"))!=0 && zGoto[0]!=0 ){
    cgi_set_cookie("fossil-goto","",0,1);
    cgi_redirect(zGoto);
  }else{
    fossil_redirect_home();
  }
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
**
** The zCS parameter is the "captcha seed" used for a specific
** anonymous login request.
*/
int login_is_valid_anonymous(
  const char *zUsername,  /* The username.  Must be "anonymous" */
  const char *zPassword,  /* The supplied password */
  const char *zCS         /* The captcha seed value */
){
  const char *zPw;        /* The correct password shown in the captcha */
  int uid;                /* The user ID of anonymous */
  int n = 0;              /* Counter of captcha-secrets */

  if( zUsername==0 ) return 0;
  else if( zPassword==0 ) return 0;
  else if( zCS==0 ) return 0;
  else if( fossil_strcmp(zUsername,"anonymous")!=0 ) return 0;
  else if( anon_cookie_lifespan()==0 ) return 0;
  while( 1/*exit-by-break*/ ){
    zPw = captcha_decode((unsigned int)atoi(zCS), n);
    if( zPw==0 ) return 0;
    if( fossil_stricmp(zPw, zPassword)==0 ) break;
    n++;
  }
  uid = db_int(0, "SELECT uid FROM user WHERE login='anonymous'"
                  " AND octet_length(pw)>0 AND octet_length(cap)>0");
  return uid;
}

/*
** Make sure the accesslog table exists.  Create it if it does not
*/
void create_accesslog_table(void){
  if( !db_table_exists("repository","accesslog") ){
    db_unprotect(PROTECT_READONLY);
    db_multi_exec(
      "CREATE TABLE IF NOT EXISTS repository.accesslog("
      "  uname TEXT,"
      "  ipaddr TEXT,"
      "  success BOOLEAN,"
      "  mtime TIMESTAMP"
      ");"
    );
    db_protect_pop();
  }
}

/*
** Make a record of a login attempt, if login record keeping is enabled.
*/
static void record_login_attempt(
  const char *zUsername,     /* Name of user logging in */
  const char *zIpAddr,       /* IP address from which they logged in */
  int bSuccess               /* True if the attempt was a success */
){
  db_unprotect(PROTECT_READONLY);
  if( db_get_boolean("access-log", 0) ){
    create_accesslog_table();
    db_multi_exec(
      "INSERT INTO accesslog(uname,ipaddr,success,mtime)"
      "VALUES(%Q,%Q,%d,julianday('now'));",
      zUsername, zIpAddr, bSuccess
    );
  }
  if( bSuccess ){
    alert_user_contact(zUsername);
  }
  db_protect_pop();
}

/*
** Searches for the user ID matching the given name and password.
** On success it returns a positive value. On error it returns 0.
** On serious (DB-level) error it will probably exit.
**
** zUsername uses double indirection because we may re-point *zUsername
** at a C string allocated with fossil_strdup() if you pass an email
** address instead and we find that address in the user table's info
** field, which is expected to contain a string of the form "Human Name
** <human@example.com>".  In that case, *zUsername will point to that
** user's actual login name on return, causing a leak unless the caller
** is diligent enough to check whether its pointer was re-pointed.
**
** zPassword may be either the plain-text form or the encrypted
** form of the user's password.
*/
int login_search_uid(const char **pzUsername, const char *zPasswd){
  char *zSha1Pw = sha1_shared_secret(zPasswd, *pzUsername, 0);
  int uid = db_int(0,
    "SELECT uid FROM user"
    " WHERE login=%Q"
    "   AND octet_length(cap)>0 AND octet_length(pw)>0"
    "   AND login NOT IN ('anonymous','nobody','developer','reader')"
    "   AND (pw=%Q OR (length(pw)<>40 AND pw=%Q))"
    "   AND (info NOT LIKE '%%expires 20%%'"
    "      OR substr(info,instr(lower(info),'expires')+8,10)>datetime('now'))",
    *pzUsername, zSha1Pw, zPasswd
  );

  /* If we did not find a login on the first attempt, and the username
  ** looks like an email address, then perhaps the user entered their
  ** email address instead of their login.  Try again to match the user
  ** against email addresses contained in the "info" field.
  */
  if( uid==0 && strchr(*pzUsername,'@')!=0 ){
    Stmt q;
    db_prepare(&q,
      "SELECT login FROM user"
      " WHERE find_emailaddr(info)=%Q"
      "   AND instr(login,'@')==0",
      *pzUsername
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zLogin = db_column_text(&q,0);
      if( (uid = login_search_uid(&zLogin, zPasswd) ) != 0 ){
        *pzUsername = fossil_strdup(zLogin);
        break;
      }
    }
    db_finalize(&q);
  }
  free(zSha1Pw);
  return uid;
}

/*
** Generates a login cookie value for a non-anonymous user.
**
** The zHash parameter must be a random value which must be
** subsequently stored in user.cookie for later validation.
**
** The returned memory should be free()d after use.
*/
char *login_gen_user_cookie_value(const char *zUsername, const char *zHash){
  char *zProjCode = db_get("project-code",NULL);
  char *zCode = abbreviated_project_code(zProjCode);
  free(zProjCode);
  assert((zUsername && *zUsername) && "Invalid user data.");
  return mprintf("%s/%z/%s", zHash, zCode, zUsername);
}

/*
** Generates a login cookie for NON-ANONYMOUS users.  Note that this
** function "could" figure out the uid by itself but it currently
** doesn't because the code which calls this already has the uid.
**
** This function also updates the user.cookie, user.ipaddr,
** and user.cexpire fields for the given user.
**
** If zDest is not NULL then the generated cookie is copied to
** *zDdest and ownership is transfered to the caller (who should
** eventually pass it to free()).
**
** If bSessionCookie is true, the cookie will be a session cookie,
** else a persistent cookie. If it's a session cookie, the
** [user].[cexpire] and [user].[cookie] entries will be modified as if
** it were a persistent cookie because doing so is necessary for
** fossil's own "is this cookie still valid?" checks to work.
*/
void login_set_user_cookie(
  const char *zUsername,  /* User's name */
  int uid,                /* User's ID */
  char **zDest,           /* Optional: store generated cookie value. */
  int bSessionCookie      /* True for session-only cookie */
){
  const char *zCookieName = login_cookie_name();
  const char *zExpire = db_get("cookie-expire","8766");
  const int expires = atoi(zExpire)*3600;
  char *zHash = 0;
  char *zCookie;
  const char *zIpAddr = PD("REMOTE_ADDR","nil"); /* IP address of user */

  assert((zUsername && *zUsername) && (uid > 0) && "Invalid user data.");
  zHash = db_text(0,
      "SELECT cookie FROM user"
      " WHERE uid=%d"
      "   AND cexpire>julianday('now')"
      "   AND length(cookie)>30",
      uid);
  if( zHash==0 ) zHash = db_text(0, "SELECT hex(randomblob(25))");
  zCookie = login_gen_user_cookie_value(zUsername, zHash);
  cgi_set_cookie(zCookieName, zCookie, login_cookie_path(),
                 bSessionCookie ? 0 : expires);
  record_login_attempt(zUsername, zIpAddr, 1);
  db_unprotect(PROTECT_USER);
  db_multi_exec("UPDATE user SET cookie=%Q,"
                "  cexpire=julianday('now')+%d/86400.0 WHERE uid=%d",
                zHash, expires, uid);
  db_protect_pop();
  fossil_free(zHash);
  if( zDest ){
    *zDest = zCookie;
  }else{
    free(zCookie);
  }
}

/*
** SETTING: anon-cookie-lifespan      width=10 default=480
** The number of minutes for which an anonymous login cookie is
** valid.  Anonymous logins are prohibited if this value is zero.
*/


/*
** The default lifetime of an anoymous cookie, in minutes.
*/
#define ANONYMOUS_COOKIE_LIFESPAN (8*60)

/*
** Return the lifetime of an anonymous cookie, in minutes.
*/
int anon_cookie_lifespan(void){
  static int lifespan = -1;
  if( lifespan<0 ){
    lifespan = db_get_int("anon-cookie-lifespan", ANONYMOUS_COOKIE_LIFESPAN);
    if( lifespan<0 ) lifespan = 0;
  }
  return lifespan;
}

/* Sets a cookie for an anonymous user login, which looks like this:
**
**    HASH/TIME/anonymous
**
** Where HASH is the sha1sum of TIME/USERAGENT/SECRET, in which SECRET
** is captcha-secret and USERAGENT is the HTTP_USER_AGENT value.
**
** If zCookieDest is not NULL then the generated cookie is assigned to
** *zCookieDest and the caller must eventually free() it.
**
** If bSessionCookie is true, the cookie will be a session cookie.
**
** Search for tag-20250817a to find the code that recognizes this cookie.
*/
void login_set_anon_cookie(char **zCookieDest, int bSessionCookie){
  char *zNow;                  /* Current time (julian day number) */
  char *zCookie;               /* The login cookie */
  const char *zUserAgent;      /* The user agent */
  const char *zCookieName;     /* Name of the login cookie */
  Blob b;                      /* Blob used during cookie construction */
  int expires = bSessionCookie ? 0 : anon_cookie_lifespan();
  zCookieName = login_cookie_name();
  zNow = db_text("0", "SELECT julianday('now')");
  assert( zCookieName && zNow );
  blob_init(&b, zNow, -1);
  zUserAgent = PD("HTTP_USER_AGENT","nil");
  blob_appendf(&b, "/%s/%z", zUserAgent, captcha_secret(0));
  sha1sum_blob(&b, &b);
  zCookie = mprintf("%s/%s/anonymous", blob_buffer(&b), zNow);
  blob_reset(&b);
  cgi_set_cookie(zCookieName, zCookie, login_cookie_path(), expires);
  if( zCookieDest ){
    *zCookieDest = zCookie;
  }else{
    free(zCookie);
  }
  fossil_free(zNow);
}

/*
** "Unsets" the login cookie (insofar as cookies can be unset) and
** clears the current user's (g.userUid) login information from the
** user table. Sets: user.cookie, user.ipaddr, user.cexpire.
**
** We could/should arguably clear out g.userUid and g.perm here, but
** we don't currently do not.
**
** This is a no-op if g.userUid is 0.
*/
void login_clear_login_data(){
  if(!g.userUid){
    return;
  }else{
    const char *cookie = login_cookie_name();
    /* To logout, change the cookie value to an empty string */
    cgi_set_cookie(cookie, "",
                   login_cookie_path(), -86400);
    db_unprotect(PROTECT_USER);
    db_multi_exec("UPDATE user SET cookie=NULL, ipaddr=NULL, "
                  "  cexpire=0 WHERE uid=%d"
                  "  AND login NOT IN ('anonymous','nobody',"
                  "  'developer','reader')", g.userUid);
    db_protect_pop();
    cgi_replace_parameter(cookie, NULL);
    cgi_replace_parameter("anon", NULL);
  }
}

/*
** Look at the HTTP_USER_AGENT parameter and try to determine if the user agent
** is a manually operated browser or a bot.  When in doubt, assume a bot.
** Return true if we believe the agent is a real person.
*/
static int isHuman(const char *zAgent){
  if( zAgent==0 ) return 0;  /* If no UserAgent, then probably a bot */
  if( strstr(zAgent, "bot")!=0 ) return 0;
  if( strstr(zAgent, "spider")!=0 ) return 0;
  if( strstr(zAgent, "crawl")!=0 ) return 0;
  /* If a URI appears in the User-Agent, it is probably a bot */
  if( strstr(zAgent, "http")!=0 ) return 0;
  if( strncmp(zAgent, "Mozilla/", 8)==0 ){
    if( atoi(&zAgent[8])<4 ) return 0;  /* Many bots advertise as Mozilla/3 */

    /* Google AI Robot, maybe? */
    if( strstr(zAgent, "GoogleOther)")!=0 ) return 0;

    /* 2016-05-30:  A pernicious spider that likes to walk Fossil timelines has
    ** been detected on the SQLite website.  The spider changes its user-agent
    ** string frequently, but it always seems to include the following text:
    */
    if( strstr(zAgent, "Safari/537.36Mozilla/5.0")!=0 ) return 0;

    if( sqlite3_strglob("*Firefox/[1-9]*", zAgent)==0 ) return 1;
    if( sqlite3_strglob("*Chrome/[1-9]*", zAgent)==0 ) return 1;
    if( sqlite3_strglob("*(compatible;?MSIE?[1789]*", zAgent)==0 ) return 1;
    if( sqlite3_strglob("*Trident/[1-9]*;?rv:[1-9]*", zAgent)==0 ){
      return 1; /* IE11+ */
    }
    if( sqlite3_strglob("*AppleWebKit/[1-9]*(KHTML*", zAgent)==0 ) return 1;
    if( sqlite3_strglob("*PaleMoon/[1-9]*", zAgent)==0 ) return 1;
    return 0;
  }
  if( strncmp(zAgent, "Opera/", 6)==0 ) return 1;
  if( strncmp(zAgent, "Safari/", 7)==0 ) return 1;
  if( strncmp(zAgent, "Lynx/", 5)==0 ) return 1;
  if( strncmp(zAgent, "NetSurf/", 8)==0 ) return 1;
  return 0;
}

/*
** Make a guess at whether or not the requestor is a mobile device or
** a desktop device (narrow screen vs. wide screen) based the HTTP_USER_AGENT
** parameter.  Return true for mobile and false for desktop.
**
** Caution:  This is only a guess.
**
** Algorithm derived from https://developer.mozilla.org/en-US/docs/Web/
** HTTP/Browser_detection_using_the_user_agent#mobile_device_detection on
** 2021-03-01
*/
int user_agent_is_likely_mobile(void){
  const char *zAgent = P("HTTP_USER_AGENT");
  if( zAgent==0 ) return 0;
  if( strstr(zAgent,"Mobi")!=0 ) return 1;
  return 0;
}

/*
** COMMAND: test-ishuman
**
** Read lines of text from standard input.  Interpret each line of text
** as a User-Agent string from an HTTP header.  Label each line as HUMAN
** or ROBOT.
*/
void test_ishuman(void){
  char zLine[3000];
  while( fgets(zLine, sizeof(zLine), stdin) ){
    fossil_print("%s %s", isHuman(zLine) ? "HUMAN" : "ROBOT", zLine);
  }
}

/*
** SQL function for constant time comparison of two values.
** Sets result to 0 if two values are equal.
*/
static void constant_time_cmp_function(
 sqlite3_context *context,
 int argc,
 sqlite3_value **argv
){
  const unsigned char *buf1, *buf2;
  int len, i;
  unsigned char rc = 0;

  assert( argc==2 );
  len = sqlite3_value_bytes(argv[0]);
  if( len==0 || len!=sqlite3_value_bytes(argv[1]) ){
    rc = 1;
  }else{
    buf1 = sqlite3_value_text(argv[0]);
    buf2 = sqlite3_value_text(argv[1]);
    for( i=0; i<len; i++ ){
      rc = rc | (buf1[i] ^ buf2[i]);
    }
  }
  sqlite3_result_int(context, rc);
}

/*
** Return true if the current page was reached by a redirect from the /login
** page.
*/
int referred_from_login(void){
  const char *zReferer = P("HTTP_REFERER");
  char *zPattern;
  int rc;
  if( zReferer==0 ) return 0;
  zPattern = mprintf("%s/login*", g.zBaseURL);
  rc = sqlite3_strglob(zPattern, zReferer)==0;
  fossil_free(zPattern);
  return rc;
}

/*
** Return true if users are allowed to reset their own passwords.
*/
int login_self_password_reset_available(void){
  if( !db_get_boolean("self-pw-reset",0) ) return 0;
  if( !alert_tables_exist() ) return 0;
  return 1;
}

/*
** Return TRUE if self-registration is available.  If the zNeeded
** argument is not NULL, then only return true if self-registration is
** available and any of the capabilities named in zNeeded are available
** to self-registered users.
*/
int login_self_register_available(const char *zNeeded){
  CapabilityString *pCap;
  int rc;
  if( !db_get_boolean("self-register",0) ) return 0;
  if( zNeeded==0 ) return 1;
  pCap = capability_add(0, db_get("default-perms", "u"));
  capability_expand(pCap);
  rc = capability_has_any(pCap, zNeeded);
  capability_free(pCap);
  return rc;
}

/*
** There used to be a page named "my" that was designed to show information
** about a specific user.  The "my" page was linked from the "Logged in as USER"
** line on the title bar.  The "my" page was never completed so it is now
** removed.  Use this page as a placeholder in older installations.
**
** WEBPAGE: login
** WEBPAGE: logout
** WEBPAGE: my
**
** The login/logout page.  Parameters:
**
**    g=URL             Jump back to this URL after login completes
**    anon              The g=URL is not accessible by "nobody" but is
**                      accessible by "anonymous"
*/
void login_page(void){
  const char *zUsername, *zPasswd;
  const char *zNew1, *zNew2;
  const char *zAnonPw = 0;
  const char *zGoto = P("g");
  int anonFlag;                /* Login as "anonymous" would be useful */
  char *zErrMsg = "";
  int uid;                     /* User id logged in user */
  char *zSha1Pw;
  const char *zIpAddr;         /* IP address of requestor */
  const int noAnon = P("noanon")!=0;
  int rememberMe;              /* If true, use persistent cookie, else
                                  session cookie. Toggled per
                                  checkbox. */

  if( P("pwreset")!=0 && login_self_password_reset_available() ){
    /* If the "Reset Password" button in the form was pressed, render
    ** the Request Password Reset page in place of this one. */
    login_reqpwreset_page();
    return;
  }

  /* If the "anon" query parameter is 1 or 2, that means rework the web-page
  ** to make it a more user-friendly captcha.  Extraneous text and boxes
  ** are omitted.  The user has just the captcha image and an entry box
  ** and a "Verify" button.  Underneath is the same login page for user
  ** "anonymous", just displayed in an easier to digest format for one-time
  ** visitors.
  **
  ** anon=1 is advisory and only has effect if there is not some other login
  ** cookie.  anon=2 means always show the captcha. 
  */
  anonFlag = anon_cookie_lifespan()>0 ? atoi(PD("anon","0")) : 0;
  if( anonFlag==2 ){
    g.zLogin = 0;
  }else{
    login_check_credentials();
    if( g.zLogin!=0 ) anonFlag = 0;
  }

  fossil_redirect_to_https_if_needed(1);
  sqlite3_create_function(g.db, "constant_time_cmp", 2, SQLITE_UTF8, 0,
                  constant_time_cmp_function, 0, 0);
  zUsername = P("u");
  zPasswd = P("p");

  /* Handle log-out requests */
  if( P("out") && cgi_csrf_safe(2) ){
    login_clear_login_data();
    login_redirect_to_g();
    return;
  }

  /* Redirect for create-new-account requests */
  if( P("self") ){
    cgi_redirectf("%R/register");
    return;
  }

  /* Deal with password-change requests */
  if( g.perm.Password && zPasswd
   && (zNew1 = P("n1"))!=0 && (zNew2 = P("n2"))!=0
   && cgi_csrf_safe(2)
  ){
    /* If there is not a "real" login, we cannot change any password. */
    if( g.zLogin ){
      /* The user requests a password change */
      zSha1Pw = sha1_shared_secret(zPasswd, g.zLogin, 0);
      if( db_int(1, "SELECT 0 FROM user"
                    " WHERE uid=%d"
                    " AND (constant_time_cmp(pw,%Q)=0"
                    "      OR constant_time_cmp(pw,%Q)=0)",
                    g.userUid, zSha1Pw, zPasswd) ){
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
        int rc;

        /* vvvvvvv---  tag-20230106-1 ----vvvvvv
        **
        ** Replicate changes made below to tag-20230106-2
        */
        admin_log("password change for user %s", g.zLogin);
        db_unprotect(PROTECT_USER);
        db_multi_exec(
           "UPDATE user SET pw=%Q WHERE uid=%d", zNewPw, g.userUid
        );
        zChngPw = mprintf(
           "UPDATE user"
           "   SET pw=shared_secret(%Q,%Q,"
           "        (SELECT value FROM config WHERE name='project-code'))"
           " WHERE login=%Q",
           zNew1, g.zLogin, g.zLogin
        );
        fossil_free(zNewPw);
        rc = login_group_sql(zChngPw, "<p>", "</p>\n", &zErr);
        db_protect_pop();
        /*
        ** ^^^^^^^^---  tag-20230106-1 ----^^^^^^^^^
        **
        ** Replicate changes above to tag-20230106-2
        */

        if( rc ){
          zErrMsg = mprintf("<span class=\"loginError\">%s</span>", zErr);
          fossil_free(zErr);
        }else{
          login_redirect_to_g();
          return;
        }
      }
    }else{
      zErrMsg =
         @ <p><span class="loginError">
         @ The password cannot be changed for this type of login.
         @ The password is unchanged.
         @ </span></p>
      ;
    }
  }
  zIpAddr = PD("REMOTE_ADDR","nil");   /* Complete IP address for logging */
  uid = login_is_valid_anonymous(zUsername, zPasswd, P("cs"));
  if(zUsername==0){
    /* Initial login page hit. */
    rememberMe = 0;
  }else{
    rememberMe = P("remember")!=0;
  }
  if( uid>0 ){
    login_set_anon_cookie(NULL, rememberMe?0:1);
    record_login_attempt("anonymous", zIpAddr, 1);
    login_redirect_to_g();
  }
  if( zUsername!=0 && zPasswd!=0 && zPasswd[0]!=0 ){
    /* Attempting to log in as a user other than anonymous.
    */
    uid = login_search_uid(&zUsername, zPasswd);
    if( uid<=0 ){
      sleep(1);
      zErrMsg =
         @ <p><span class="loginError">
         @ You entered an unknown user or an incorrect password.
         @ </span></p>
      ;
      record_login_attempt(zUsername, zIpAddr, 0);
      cgi_set_status(401, "Unauthorized");
    }else{
      /* Non-anonymous login is successful.  Set a cookie of the form:
      **
      **    HASH/PROJECT/LOGIN
      **
      ** where HASH is a random hex number, PROJECT is either project
      ** code prefix, and LOGIN is the user name.
      */
      login_set_user_cookie(zUsername, uid, NULL, rememberMe?0:1);
      login_redirect_to_g();
    }
  }
  style_set_current_feature("login");
  style_header("Login/Logout");
  if( anonFlag==2 ) g.zLogin = 0;
  style_adunit_config(ADUNIT_OFF);
  @ %s(zErrMsg)
  if( zGoto && !noAnon ){
    char *zAbbrev = fossil_strdup(zGoto);
    int i;
    for(i=0; zAbbrev[i] && zAbbrev[i]!='?'; i++){}
    zAbbrev[i] = 0;
    if( g.zLogin ){
      @ <p>Use a different login with greater privilege than <b>%h(g.zLogin)</b>
      @ to access <b>%h(zAbbrev)</b>.
    }else if( anonFlag ){
      @ <p><b>Verify that you are human by typing in the 8-character text
      @ password shown below.</b></p>
    }else{
      @ <p>Login as a named user to access page <b>%h(zAbbrev)</b>.
    }
    fossil_free(zAbbrev);
  }
  if( g.sslNotAvailable==0
   && strncmp(g.zBaseURL,"https:",6)!=0
   && db_get_boolean("https-login",0)
  ){
    form_begin(0, "https:%s/login", g.zBaseURL+5);
  }else{
    form_begin(0, "%R/login");
  }
  if( zGoto ){
    @ <input type="hidden" name="g" value="%h(zGoto)">
  }
  if( anonFlag ){
    @ <input type="hidden" name="anon" value="1">
    @ <input type="hidden" name="u" value="anonymous">
  }
  if( g.zLogin ){
    @ <p>Currently logged in as <b>%h(g.zLogin)</b>.
    @ <input type="submit" name="out" value="Logout" autofocus></p>
    @ </form>
  }else{
    unsigned int uSeed = captcha_seed();
    if( g.zLogin==0 && (anonFlag || zGoto==0) && anon_cookie_lifespan()>0 ){
      zAnonPw = db_text(0, "SELECT pw FROM user"
                           " WHERE login='anonymous'"
                           "   AND cap!=''");
    }else{
      zAnonPw = 0;
    }
    @ <table class="login_out">
    if( P("HTTPS")==0 && !anonFlag ){
      @ <tr><td class="form_label">Warning:</td>
      @ <td><span class='securityWarning'>
      @ Login information, including the password,
      @ will be sent in the clear over an unencrypted connection.
      if( !g.sslNotAvailable ){
        @ Consider logging in at
        @ <a href='%s(g.zHttpsURL)'>%h(g.zHttpsURL)</a> instead.
      }
      @ </span></td></tr>
    }
    if( !anonFlag ){
      @ <tr>
      @   <td class="form_label" id="userlabel1">User ID:</td>
      @   <td><input type="text" id="u" aria-labelledby="userlabel1" name="u" \
      @ size="30" value="" autofocus></td>
      @ </tr>
    }
    @ <tr>
    @  <td class="form_label" id="pswdlabel">Password:</td>
    @  <td><input aria-labelledby="pswdlabel" type="password" id="p" \
    @ name="p" value="" size="30"%s(anonFlag ? " autofocus" : "")>
    if( anonFlag ){
      @ </td></tr>
      @ <tr>
      @  <td></td><td>\
      captcha_speakit_button(uSeed, "Read the password out loud");
    }else if( zAnonPw && !noAnon ){
      captcha_speakit_button(uSeed, "Speak password for \"anonymous\"");
    }
    @ </td>
    @ </tr>
    if( !anonFlag ){
      @ <tr>
      @   <td></td>
      @   <td><input type="checkbox" name="remember" value="1" \
      @ id="remember-me" %s(rememberMe ? "checked=\"checked\"" : "")>
      @   <label for="remember-me">Remember me?</label></td>
      @ </tr>
      @ <tr>
      @   <td></td>
      @   <td><input type="submit" name="in" value="Login">
      @ </tr>
    }else{
      @ <tr>
      @   <td></td>
      @   <td><input type="submit" name="in" value="Verify that I am human">
      @ </tr>
    }
    if( !anonFlag && !noAnon && login_self_register_available(0) ){
      @ <tr>
      @   <td></td>
      @   <td><input type="submit" name="self" value="Create A New Account">
      @ </tr>
    }
    if( !anonFlag && login_self_password_reset_available() ){
      @ <tr>
      @   <td></td>
      @   <td><input type="submit" name="pwreset" value="Reset My Password">
      @ </tr>
    }
    @ </table>
    if( zAnonPw && !noAnon ){
      const char *zDecoded = captcha_decode(uSeed, 0);
      int bAutoCaptcha = db_get_boolean("auto-captcha", 0);
      char *zCaptcha = captcha_render(zDecoded);

      @ <p><input type="hidden" name="cs" value="%u(uSeed)">
      if( !anonFlag ){
        @ Visitors may enter <b>anonymous</b> as the user-ID with
        @ the 8-character hexadecimal password shown below:</p>
      }
      @ <div class="captcha"><table class="captcha"><tr><td>\
      @ <pre class="captcha">
      @ %h(zCaptcha)
      @ </pre></td></tr></table>
      if( bAutoCaptcha && !anonFlag ) {
         @ <input type="button" value="Fill out captcha" id='autofillButton' \
         @ data-af='%s(zDecoded)'>
         builtin_request_js("login.js");
      }
      @ </div>
      free(zCaptcha);
    }
    @ </form>
  }
  if( login_is_individual() && !anonFlag ){
    if( g.perm.EmailAlert && alert_enabled() ){
      @ <hr>
      @ <p>Configure <a href="%R/alerts">Email Alerts</a>
      @ for user <b>%h(g.zLogin)</b></p>
    }
    if( db_table_exists("repository","forumpost") ){
      @ <hr><p>
      @ <a href="%R/timeline?ss=v&y=f&vfx&u=%t(g.zLogin)">Forum
      @ post timeline</a> for user <b>%h(g.zLogin)</b></p>
    }
  }
  if( !anonFlag ){
    @ <hr><p>
    @ Select your preferred <a href="%R/skins">site skin</a>.
    @ </p>
    @ <hr><p>
    @ Manage your <a href="%R/cookies">cookies</a> or your
    @ <a href="%R/tokens">access tokens</a>.</p>
  }
  if( login_is_individual() ){
    if( g.perm.Password ){
      char *zRPW = fossil_random_password(12);
      @ <hr>
      @ <p>Change Password for user <b>%h(g.zLogin)</b>:</p>
      form_begin(0, "%R/login");
      @ <table>
      @ <tr><td class="form_label" id="oldpw">Old Password:</td>
      @ <td><input aria-labelledby="oldpw" type="password" name="p" \
      @ size="30"/></td></tr>
      @ <tr><td class="form_label" id="newpw">New Password:</td>
      @ <td><input aria-labelledby="newpw" type="password" name="n1" \
      @ size="30"> Suggestion: %z(zRPW)</td></tr>
      @ <tr><td class="form_label" id="reppw">Repeat New Password:</td>
      @ <td><input aria-labledby="reppw" type="password" name="n2" \
      @ size="30"></td></tr>
      @ <tr><td></td>
      @ <td><input type="submit" value="Change Password"></td></tr>
      @ </table>
      @ </form>
    }
  }
  style_finish_page();
}

/*
** Construct an appropriate URL suffix for the /resetpw page.  The
** suffix will be of the form:
**
**     UID-TIMESTAMP-HASH
**
** Where UID and TIMESTAMP are the parameters to this function, and HASH
** is constructed from information that is unique to the user in question
** and which is not publicly available.  In particular, the HASH includes
** the existing user password.  Thus, in order to construct a URL that can
** change a password, an attacker must know the current password, in which
** case the attacker does not need to construct the URL in order to take
** over the account.
**
** Return a pointer to the resulting string in memory obtained
** from fossil_malloc().
*/
char *login_resetpw_suffix(int uid, i64 timestamp){
  char *zHash;
  char *zInnerSql;
  char *zResult;
  extern int sqlite3_shathree_init(sqlite3*,char**,const sqlite3_api_routines*);
  if( timestamp<=0 ){ timestamp = time(0); }
  sqlite3_shathree_init(g.db, 0, 0);
  if( db_table_exists("repository","subscriber") ){
    zInnerSql = mprintf(
      "SELECT %lld, login, pw, cookie, user.mtime, user.info, subscriberCode"
      "  FROM user LEFT JOIN subscriber ON suname=login"
      " WHERE uid=%d", timestamp, uid);
  }else{
    zInnerSql = mprintf(
      "SELECT %lld, login, pw, cookie, user.mtime, user.info"
      "  FROM user WHERE uid=%d", timestamp, uid);
  }
  zHash = db_text(0, "SELECT lower(hex(sha3_query(%Q)))", zInnerSql);
  fossil_free(zInnerSql);
  zResult = mprintf("%x-%llx-%s", uid, timestamp, zHash);
  if( strlen(zHash)<64 || strlen(zResult)<70 ){
    /* This should never happen, but if it does, we don't want it to lead
    ** to a security breach. */
    fossil_panic("insecure password reset hash generated\n");
  }
  fossil_free(zHash);
  return zResult;
}

/*
** Check to see if the "name" query parameter is a valid resetpw suffix
** for a user whose password we are allowed to reset.  If it is, then return
** the positive integer UID for that user.  If the query parameter is not
** valid, return 0.
*/
static int login_resetpw_suffix_is_valid(const char *zName){
  int i, j;
  int uid;
  i64 timestamp;
  i64 now;
  char *zHash;
  if( zName==0 || strlen(zName)<70 ) goto not_valid_suffix;
  for(i=0; fossil_isxdigit(zName[i]); i++){}
  if( i<1 || zName[i]!='-' ) goto not_valid_suffix;
  for(j=i+1; fossil_isxdigit(zName[j]); j++){}
  if( j<=i+1 || zName[j]!='-' ) goto not_valid_suffix;
  uid = strtol(zName, 0, 16);
  if( uid<=0 ) goto not_valid_suffix;
  if( !db_exists("SELECT 1 FROM user WHERE uid=%d", uid) ){
    goto not_valid_suffix;
  }
  timestamp = strtoll(&zName[i+1], 0, 16);
  now = time(0);
  if( timestamp+3600 <= now ) goto not_valid_suffix;
  zHash = login_resetpw_suffix(uid,timestamp);
  if( fossil_strcmp(zHash, zName)!=0 ){
    fossil_free(zHash);
    goto not_valid_suffix;
  }
  fossil_free(zHash);
  return uid;

not_valid_suffix:
  return 0;
}

/*
** COMMAND: test-resetpw-url
** Usage: fossil test-resetpw-url UID
**
** Generate and verify a /resetpw URL for user UID.
**
** This command is intended for unit testing the login_resetpw_suffix()
** and login_resetpw_suffix_is_valid() functions.
*/
void test_resetpw_url(void){
  char *zSuffix;
  int uid;
  int xuid;
  char *zLogin;
  int i;
  db_find_and_open_repository(0, 0);
  verify_all_options();
  if( g.argc<3 ){
    usage("UID ...");
  }
  for(i=2; i<g.argc; i++){
    uid = atoi(g.argv[i]);
    zSuffix = login_resetpw_suffix(uid, 0);
    xuid = login_resetpw_suffix_is_valid(zSuffix);
    if( xuid>0 ){
      zLogin = db_text(0, "SELECT login FROM user WHERE uid=%d", xuid);
    }else{
      zLogin = 0;
    }
    fossil_print("/resetpw/%s   %d (%s)\n",
                 zSuffix, xuid, zLogin ? zLogin : "???");
    fossil_free(zSuffix);
    fossil_free(zLogin);
  }
}

/*
** WEBPAGE: resetpw
**
** The URL format must be like this:
**
**      /resetpw/UID-TIMESTAMP-HASH
**
** Where UID is the uid of the user whose password is to be reset,
** TIMESTAMP is the unix timestamp when the request was made, and
** HASH is a hash based on UID, TIMESTAMP, and other information that
** is unavailable to an attacher.
**
** With no other arguments, a form is present which allows the user to
** enter a new password.  When the SUBMIT button is pressed, a POST request
** back to the same URL that will change the password.
*/
void login_resetpw(void){
  const char *zName;
  int uid;
  char *zRPW;
  const char *zNew1, *zNew2;

  style_set_current_feature("resetpw");
  style_header("Reset Password");
  style_adunit_config(ADUNIT_OFF);
  zName = PD("name","");
  uid = login_resetpw_suffix_is_valid(zName);
  if( uid==0 ){
    @ <p><span class="loginError">
    @ This password-reset URL is invalid, probably because it has expired.
    @ Password-reset URLs have a short lifespan.
    @ </span></p>
    style_finish_page();
    sleep(1);  /* Introduce a small delay on an invalid suffix as an
               ** extra defense against search attacks */
    return;
  }
  fossil_redirect_to_https_if_needed(1);
  login_set_uid(uid, 0);
  if( g.perm.Setup || g.perm.Admin || !g.perm.Password || g.zLogin==0 ){
    @ <p><span class="loginError">
    @ Cannot change the password for user <b>%h(g.zLogin)</b>.
    @ </span></p>
    style_finish_page();
    return;
  }
  if( (zNew1 = P("n1"))!=0 && (zNew2 = P("n2"))!=0 ){
    if( fossil_strcmp(zNew1,zNew2)!=0 ){
      @ <p><span class="loginError">
      @ The two copies of your new passwords do not match.
      @ Try again.
      @ </span></p>
    }else{
      char *zNewPw = sha1_shared_secret(zNew1, g.zLogin, 0);
      char *zChngPw;
      char *zErr;
      int rc;

      /* vvvvvvv---  tag-20230106-2 ----vvvvvv
      **
      ** Replicate changes made below to tag-20230106-1
      */
      admin_log("password change for user %s", g.zLogin);
      db_unprotect(PROTECT_USER);
      db_multi_exec(
         "UPDATE user SET pw=%Q WHERE uid=%d", zNewPw, g.userUid
      );
      zChngPw = mprintf(
         "UPDATE user"
         "   SET pw=shared_secret(%Q,%Q,"
         "        (SELECT value FROM config WHERE name='project-code'))"
         " WHERE login=%Q",
         zNew1, g.zLogin, g.zLogin
      );
      fossil_free(zNewPw);
      rc = login_group_sql(zChngPw, "<p>", "</p>\n", &zErr);
      db_protect_pop();
      /*
      ** ^^^^^^^^---  tag-20230106-2 ----^^^^^^^^^
      **
      ** Replicate changes above to tag-20230106-1
      */

      if( rc ){
        @ <p><span class='loginError'>
        @ %s(zErr);
        @ </span></p>
        fossil_free(zErr);
      }else{
        @ <p>Password changed successfully.  Go to the
        @ <a href="%R/login?u=%t(g.zLogin)">Login</a> page and log in
        @ using the new password to continue.
        @ </p>
        style_finish_page();
        return;
      }
    }
  }
  zRPW = fossil_random_password(12);
  @ <p>Change Password for user <b>%h(g.zLogin)</b>:</p>
  form_begin(0, "%R/resetpw");
  @ <input type='hidden' name='name' value='%h(zName)'>
  @ <table>
  @ <tr><td class="form_label" id="newpw">New Password:</td>
  @ <td><input aria-labelledby="newpw" type="password" name="n1" \
  @ size="30"> Suggestion: %z(zRPW)</td></tr>
  @ <tr><td class="form_label" id="reppw">Repeat New Password:</td>
  @ <td><input aria-labledby="reppw" type="password" name="n2" \
  @ size="30"></td></tr>
  @ <tr><td></td>
  @ <td><input type="submit" value="Change Password"></td></tr>
  @ </table>
  @ </form>
  style_finish_page();
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
  const char *zHash            /* HASH from login cookie HASH/CODE/LOGIN */
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

  rc = sqlite3_open_v2(
       zOtherRepo, &pOther,
       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
       g.zVfsName
  );
  if( rc==SQLITE_OK ){
    sqlite3_create_function(pOther,"now",0,SQLITE_UTF8,0,db_now_function,0,0);
    sqlite3_create_function(pOther, "constant_time_cmp", 2, SQLITE_UTF8, 0,
                  constant_time_cmp_function, 0, 0);
    sqlite3_busy_timeout(pOther, 5000);
    zSQL = mprintf(
      "SELECT cexpire FROM user"
      " WHERE login=%Q"
      "   AND octet_length(cap)>0"
      "   AND octet_length(pw)>0"
      "   AND cexpire>julianday('now')"
      "   AND constant_time_cmp(cookie,%Q)=0",
      zLogin, zHash
    );
    pStmt = 0;
    rc = sqlite3_prepare_v2(pOther, zSQL, -1, &pStmt, 0);
    if( rc==SQLITE_OK && sqlite3_step(pStmt)==SQLITE_ROW ){
      db_unprotect(PROTECT_USER);
      db_multi_exec(
        "UPDATE user SET cookie=%Q, cexpire=%.17g"
        " WHERE login=%Q",
        zHash,
        sqlite3_column_double(pStmt, 0), zLogin
      );
      db_protect_pop();
      nXfer++;
    }
    sqlite3_finalize(pStmt);
  }
  sqlite3_close(pOther);
  fossil_free(zOtherRepo);
  return nXfer;
}

/*
** Return TRUE if zLogin is one of the special usernames
*/
int login_is_special(const char *zLogin){
  if( fossil_strcmp(zLogin, "anonymous")==0 ) return 1;
  if( fossil_strcmp(zLogin, "nobody")==0 ) return 1;
  if( fossil_strcmp(zLogin, "developer")==0 ) return 1;
  if( fossil_strcmp(zLogin, "reader")==0 ) return 1;
  return 0;
}

/*
** Lookup the uid for a non-built-in user with zLogin and zCookie.
** Return 0 if not found.
**
** Note that this only searches for logged-in entries with matching
** zCookie (db: user.cookie) entries.
*/
static int login_find_user(
  const char *zLogin,            /* User name */
  const char *zCookie            /* Login cookie value */
){
  int uid;
  if( login_is_special(zLogin) ) return 0;
  uid = db_int(0,
    "SELECT uid FROM user"
    " WHERE login=%Q"
    "   AND cexpire>julianday('now')"
    "   AND octet_length(cap)>0"
    "   AND octet_length(pw)>0"
    "   AND constant_time_cmp(cookie,%Q)=0",
    zLogin, zCookie
  );
  return uid;
}

/*
** Attempt to use Basic Authentication to establish the user.  Return the
** (non-zero) uid if successful.  Return 0 if it does not work.
*/
static int login_basic_authentication(const char *zIpAddr){
  const char *zAuth = PD("HTTP_AUTHORIZATION", 0);
  int i;
  int uid = 0;
  int nDecode = 0;
  char *zDecode = 0;
  const char *zUsername = 0;
  const char *zPasswd = 0;

  if( zAuth==0 ) return 0;             /* Fail: No Authentication: header */
  while( fossil_isspace(zAuth[0]) ) zAuth++;  /* Skip leading whitespace */
  if( strncmp(zAuth, "Basic ", 6)!=0 ){
    return 0;  /* Fail: Not Basic Authentication */
  }

  /* Parse out the username and password, separated by a ":" */
  zAuth += 6;
  while( fossil_isspace(zAuth[0]) ) zAuth++;
  zDecode = decode64(zAuth, &nDecode);

  for(i=0; zDecode[i] && zDecode[i]!=':'; i++){}
  if( zDecode[i] ){
    zDecode[i] = 0;
    zUsername = zDecode;
    zPasswd = &zDecode[i+1];

    /* Attempting to log in as the user provided by HTTP
    ** basic auth
    */
    uid = login_search_uid(&zUsername, zPasswd);
    if( uid>0 ){
      record_login_attempt(zUsername, zIpAddr, 1);
    }else{
      record_login_attempt(zUsername, zIpAddr, 0);

      /* The user attempted to login specifically with HTTP basic
      ** auth, but provided invalid credentials. Inform them of
      ** the failed login attempt via 401.
      */
      cgi_set_status(401, "Unauthorized");
      cgi_reply();
      fossil_exit(0);
    }
  }
  fossil_free(zDecode);
  return uid;
}

/*
** When this routine is called, we know that the request does not
** have a login on the present repository.  This routine checks to
** see if their login cookie might be for another member of the
** login-group.
**
** If this repository is not a part of any login group, then this
** routine always returns false.
**
** If this repository is part of a login group, and the login cookie
** appears to be well-formed, then return true.  That might be a
** false-positive, as we don't actually check to see if the login
** cookie is valid for some other repository.  But false-positives
** are ok.  This routine is used for robot defense only.
*/
int login_cookie_wellformed(void){
  const char *zCookie;
  int n;
  zCookie = P(login_cookie_name());
  if( zCookie==0 ){
    return 0;
  }
  if( !db_exists("SELECT 1 FROM config WHERE name='login-group-code'") ){
    return 0;
  }
  for(n=0; fossil_isXdigit(zCookie[n]); n++){}
  return n>48 && zCookie[n]=='/' && zCookie[n+1]!=0;
}

/*
** This routine examines the login cookie to see if it exists and
** is valid.  If the login cookie checks out, it then sets global
** variables appropriately.
**
**    g.userUid      Database USER.UID value.  Might be -1 for "nobody"
**    g.zLogin       Database USER.LOGIN value.  NULL for user "nobody"
**    g.perm         Permissions granted to this user
**    g.anon         Permissions that would be available to anonymous
**    g.isRobot      True if the client is known to be a spider or robot
**    g.perm         Populated based on user account's capabilities
**
*/
void login_check_credentials(void){
  int uid = 0;                  /* User id */
  const char *zCookie;          /* Text of the login cookie */
  const char *zIpAddr;          /* Raw IP address of the requestor */
  const char *zCap = 0;         /* Capability string */
  const char *zLogin = 0;       /* Login user for credentials */

  /* Only run this check once.  */
  if( g.userUid!=0 ) return;

  sqlite3_create_function(g.db, "constant_time_cmp", 2, SQLITE_UTF8, 0,
                  constant_time_cmp_function, 0, 0);

  /* If the HTTP connection is coming over 127.0.0.1 and if
  ** local login is disabled and if we are using HTTP and not HTTPS,
  ** then there is no need to check user credentials.
  **
  ** This feature allows the "fossil ui" command to give the user
  ** full access rights without having to log in.
  */
  zIpAddr = PD("REMOTE_ADDR","nil");
  if( ( cgi_is_loopback(zIpAddr)
       || (g.fSshClient & CGI_SSH_CLIENT)!=0 )
   && g.useLocalauth
   && db_get_boolean("localauth",0)==0
   && P("HTTPS")==0
  ){
    char *zSeed;
    if( g.localOpen ) zLogin = db_lget("default-user",0);
    if( zLogin!=0 ){
      uid = db_int(0, "SELECT uid FROM user WHERE login=%Q", zLogin);
    }else{
      uid = db_int(0, "SELECT uid FROM user WHERE cap LIKE '%%s%%'");
    }
    g.zLogin = db_text("?", "SELECT login FROM user WHERE uid=%d", uid);
    zCap = "sxy";
    g.noPswd = 1;
    g.isRobot = 0;
    zSeed = db_text("??", "SELECT uid||quote(login)||quote(pw)||quote(cookie)"
                          "  FROM user WHERE uid=%d", uid);
    login_create_csrf_secret(zSeed);
    fossil_free(zSeed);
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
    }else if( fossil_strcmp(zUser, "anonymous")==0
           && anon_cookie_lifespan()>0 ){
      /* Cookies of the form "HASH/TIME/anonymous".  The TIME must
      ** not be more than ANONYMOUS_COOKIE_LIFESPAN seconds ago and
      ** the sha1 hash of TIME/USERAGENT/SECRET must match HASH. USERAGENT
      ** is the HTTP_USER_AGENT of the client and SECRET is the
      ** "captcha-secret" value in the repository.  See tag-20250817a
      ** for the code the creates this cookie.
      */
      double rTime = atof(zArg);
      const char *zUserAgent = PD("HTTP_USER_AGENT","nil");
      Blob b;
      char *zSecret;
      int n = 0;

      do{
        blob_zero(&b);
        zSecret = captcha_secret(n++);
        if( zSecret==0 ) break;
        blob_appendf(&b, "%s/%s/%s", zArg, zUserAgent, zSecret);
        sha1sum_blob(&b, &b);
        if( fossil_strcmp(zHash, blob_str(&b))==0 ){
          uid = db_int(0,
              "SELECT uid FROM user WHERE login='anonymous'"
              " AND octet_length(cap)>0"
              " AND octet_length(pw)>0"
              " AND %.17g>julianday('now')",
              rTime+anon_cookie_lifespan()/1440.0
          );
        }
      }while( uid==0 );
      blob_reset(&b);
    }else{
      /* Cookies of the form "HASH/CODE/USER".  Search first in the
      ** local user table, then the user table for project CODE if we
      ** are part of a login-group.
      */
      uid = login_find_user(zUser, zHash);
      if( uid==0 && login_transfer_credentials(zUser,zArg,zHash) ){
        uid = login_find_user(zUser, zHash);
        if( uid ){
          record_login_attempt(zUser, zIpAddr, 1);
        }else{
          /* The login cookie is a valid login for project CODE, but no
          ** user named USER exists on this repository.  Cannot login as
          ** USER, but at least give them "anonymous" login. */
          uid = db_int(0, "SELECT uid FROM user WHERE login='anonymous'"
                          " AND octet_length(cap)>0"
                          " AND octet_length(pw)>0");
        }
      }
    }
    login_create_csrf_secret(zHash);
  }

  /* If no user found and the REMOTE_USER environment variable is set,
  ** then accept the value of REMOTE_USER as the user.
  */
  if( uid==0 ){
    const char *zRemoteUser = P("REMOTE_USER");
    if( zRemoteUser && db_get_boolean("remote_user_ok",0) ){
      uid = db_int(0, "SELECT uid FROM user WHERE login=%Q"
                      " AND octet_length(cap)>0 AND octet_length(pw)>0",
                      zRemoteUser);
    }
  }

  /* If the request didn't provide a login cookie or the login cookie didn't
  ** match a known valid user, check the HTTP "Authorization" header and
  ** see if those credentials are valid for a known user.
  */
  if( uid==0 && db_get_boolean("http_authentication_ok",0) ){
    uid = login_basic_authentication(zIpAddr);
  }

  /* Check for magic query parameters "resid" (for the username) and
  ** "token" for the password.  Both values (if they exist) will be
  ** obfuscated.
  */
  if( uid==0 ){
    char *zUsr, *zPW;
    if( (zUsr = unobscure(P("resid")))!=0
     && (zPW = unobscure(P("token")))!=0
    ){
      char *zSha1Pw = sha1_shared_secret(zPW, zUsr, 0);
      uid = db_int(0, "SELECT uid FROM user"
                      " WHERE login=%Q"
                      " AND (constant_time_cmp(pw,%Q)=0"
                      "      OR constant_time_cmp(pw,%Q)=0)",
                      zUsr, zSha1Pw, zPW);
      fossil_free(zSha1Pw);
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
    login_create_csrf_secret("none");
  }

  login_set_uid(uid, zCap);

  /* Maybe restrict access by robots */
  if( g.zLogin==0 && robot_restrict(g.zPath) ){
    cgi_reply();
    fossil_exit(0);
  }
}

/*
** Set the current logged in user to be uid.  zCap is precomputed
** (override) capabilities.  If zCap==0, then look up the capabilities
** in the USER table.
*/
int login_set_uid(int uid, const char *zCap){
  const char *zPublicPages = 0; /* GLOB patterns of public pages */

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
  if( PB("isrobot") ){
    g.isRobot = 1;
  }else if( g.zLogin==0 ){
    g.isRobot = !isHuman(P("HTTP_USER_AGENT"));
  }else{
    g.isRobot = 0;
  }

  /* Set the capabilities */
  login_replace_capabilities(zCap, 0);

  /* The auto-hyperlink setting allows hyperlinks to be displayed for users
  ** who do not have the "h" permission as long as their UserAgent string
  ** makes it appear that they are human.  Check to see if auto-hyperlink is
  ** enabled for this repository and make appropriate adjustments to the
  ** permission flags if it is.  This should be done before the permissions
  ** are (potentially) copied to the anonymous permission set; otherwise,
  ** those will be out-of-sync.
  */
  if( zCap[0] && !g.perm.Hyperlink && !g.isRobot ){
    int autoLink = db_get_int("auto-hyperlink",1);
    if( autoLink==1 ){
      g.jsHref = 1;
      g.perm.Hyperlink = 1;
    }else if( autoLink==2 ){
      g.perm.Hyperlink = 1;
    }
  }

  /*
  ** At this point, the capabilities for the logged in user are not going
  ** to be modified anymore; therefore, we can copy them over to the ones
  ** for the anonymous user.
  **
  ** WARNING: In the future, please do not add code after this point that
  **          modifies the capabilities for the logged in user.
  */
  login_set_anon_nobody_capabilities();

  /* If the public-pages glob pattern is defined and REQUEST_URI matches
  ** one of the globs in public-pages, then also add in all default-perms
  ** permissions.
  */
  zPublicPages = db_get("public-pages",0);
  if( zPublicPages!=0 ){
    const char *zUri = PD("REQUEST_URI","");
    zUri += (int)strlen(g.zTop);
    if( glob_multi_match(zPublicPages, zUri) ){
      login_set_capabilities(db_get("default-perms", "u"), 0);
    }
  }
  return g.zLogin!=0;
}

/*
** Memory of settings
*/
static int login_anon_once = 1;

/*
** Add to g.perm the default privileges of users "nobody" and/or "anonymous"
** as appropriate for the user g.zLogin.
**
** This routine also sets up g.anon to be either a copy of g.perm for
** all logged in uses, or the privileges that would be available to "anonymous"
** if g.zLogin==0 (meaning that the user is "nobody").
*/
void login_set_anon_nobody_capabilities(void){
  if( login_anon_once ){
    const char *zCap;
    /* All users get privileges from "nobody" */
    zCap = db_text("", "SELECT cap FROM user WHERE login = 'nobody'");
    login_set_capabilities(zCap, 0);
    zCap = db_text("", "SELECT cap FROM user WHERE login = 'anonymous'");
    if( g.zLogin && fossil_strcmp(g.zLogin, "nobody")!=0 ){
      /* All logged-in users inherit privileges from "anonymous" */
      login_set_capabilities(zCap, 0);
      g.anon = g.perm;
    }else{
      /* Record the privileges of anonymous in g.anon */
      g.anon = g.perm;
      login_set_capabilities(zCap, LOGIN_ANON);
    }
    login_anon_once = 0;
  }
}

/*
** Flags passed into the 2nd argument of login_set/replace_capabilities().
*/
#if INTERFACE
#define LOGIN_IGNORE_UV  0x01         /* Ignore "u" and "v" */
#define LOGIN_ANON       0x02         /* Use g.anon instead of g.perm */
#endif

/*
** Adds all capability flags in zCap to g.perm or g.anon.
*/
void login_set_capabilities(const char *zCap, unsigned flags){
  int i;
  FossilUserPerms *p = (flags & LOGIN_ANON) ? &g.anon : &g.perm;
  if(NULL==zCap){
    return;
  }
  for(i=0; zCap[i]; i++){
    switch( zCap[i] ){
      case 's':   p->Setup = 1; /* Fall thru into Admin */
      case 'a':   p->Admin = p->RdTkt = p->WrTkt = p->Zip =
                             p->RdWiki = p->WrWiki = p->NewWiki =
                             p->ApndWiki = p->Hyperlink = p->Clone =
                             p->NewTkt = p->Password = p->RdAddr =
                             p->TktFmt = p->Attach = p->ApndTkt =
                             p->ModWiki = p->ModTkt =
                             p->RdForum = p->WrForum = p->ModForum =
                             p->WrTForum = p->AdminForum = p->Chat =
                             p->EmailAlert = p->Announce = p->Debug = 1;
                             /* Fall thru into Read/Write */
      case 'i':   p->Read = p->Write = 1;                      break;
      case 'o':   p->Read = 1;                                 break;
      case 'z':   p->Zip = 1;                                  break;

      case 'h':   p->Hyperlink = 1;                            break;
      case 'g':   p->Clone = 1;                                break;
      case 'p':   p->Password = 1;                             break;

      case 'j':   p->RdWiki = 1;                               break;
      case 'k':   p->WrWiki = p->RdWiki = p->ApndWiki =1;      break;
      case 'm':   p->ApndWiki = 1;                             break;
      case 'f':   p->NewWiki = 1;                              break;
      case 'l':   p->ModWiki = 1;                              break;

      case 'e':   p->RdAddr = 1;                               break;
      case 'r':   p->RdTkt = 1;                                break;
      case 'n':   p->NewTkt = 1;                               break;
      case 'w':   p->WrTkt = p->RdTkt = p->NewTkt =
                  p->ApndTkt = 1;                              break;
      case 'c':   p->ApndTkt = 1;                              break;
      case 'q':   p->ModTkt = 1;                               break;
      case 't':   p->TktFmt = 1;                               break;
      case 'b':   p->Attach = 1;                               break;
      case 'x':   p->Private = 1;                              break;
      case 'y':   p->WrUnver = 1;                              break;

      case '6':   p->AdminForum = 1;
      case '5':   p->ModForum = 1;
      case '4':   p->WrTForum = 1;
      case '3':   p->WrForum = 1;
      case '2':   p->RdForum = 1;                              break;

      case '7':   p->EmailAlert = 1;                           break;
      case 'A':   p->Announce = 1;                             break;
      case 'C':   p->Chat = 1;                                 break;
      case 'D':   p->Debug = 1;                                break;

      /* The "u" privilege recursively
      ** inherits all privileges of the user named "reader" */
      case 'u': {
        if( p->XReader==0 ){
          const char *zUser;
          p->XReader = 1;
          zUser = db_text("", "SELECT cap FROM user WHERE login='reader'");
          login_set_capabilities(zUser, flags);
        }
        break;
      }

      /* The "v" privilege recursively
      ** inherits all privileges of the user named "developer" */
      case 'v': {
        if( p->XDeveloper==0 ){
          const char *zDev;
          p->XDeveloper = 1;
          zDev = db_text("", "SELECT cap FROM user WHERE login='developer'");
          login_set_capabilities(zDev, flags);
        }
        break;
      }
    }
  }
}

/*
** Zeroes out g.perm and calls login_set_capabilities(zCap,flags).
*/
void login_replace_capabilities(const char *zCap, unsigned flags){
  memset(&g.perm, 0, sizeof(g.perm));
  login_set_capabilities(zCap, flags);
  login_anon_once = 1;
}

/*
** If the current login lacks any of the capabilities listed in
** the input, then return 0.  If all capabilities are present, then
** return 1.
**
** As a special case, the 'L' pseudo-capability ID means "is logged
** in" and will return true for any non-guest user.
*/
int login_has_capability(const char *zCap, int nCap, u32 flgs){
  int i;
  int rc = 1;
  FossilUserPerms *p = (flgs & LOGIN_ANON) ? &g.anon : &g.perm;
  if( nCap<0 ) nCap = strlen(zCap);
  for(i=0; i<nCap && rc && zCap[i]; i++){
    switch( zCap[i] ){
      case 'a':  rc = p->Admin;     break;
      case 'b':  rc = p->Attach;    break;
      case 'c':  rc = p->ApndTkt;   break;
      /* d unused: see comment in capabilities.c */
      case 'e':  rc = p->RdAddr;    break;
      case 'f':  rc = p->NewWiki;   break;
      case 'g':  rc = p->Clone;     break;
      case 'h':  rc = p->Hyperlink; break;
      case 'i':  rc = p->Write;     break;
      case 'j':  rc = p->RdWiki;    break;
      case 'k':  rc = p->WrWiki;    break;
      case 'l':  rc = p->ModWiki;   break;
      case 'm':  rc = p->ApndWiki;  break;
      case 'n':  rc = p->NewTkt;    break;
      case 'o':  rc = p->Read;      break;
      case 'p':  rc = p->Password;  break;
      case 'q':  rc = p->ModTkt;    break;
      case 'r':  rc = p->RdTkt;     break;
      case 's':  rc = p->Setup;     break;
      case 't':  rc = p->TktFmt;    break;
      /* case 'u': READER    */
      /* case 'v': DEVELOPER */
      case 'w':  rc = p->WrTkt;     break;
      case 'x':  rc = p->Private;   break;
      case 'y':  rc = p->WrUnver;   break;
      case 'z':  rc = p->Zip;       break;
      case '2':  rc = p->RdForum;   break;
      case '3':  rc = p->WrForum;   break;
      case '4':  rc = p->WrTForum;  break;
      case '5':  rc = p->ModForum;  break;
      case '6':  rc = p->AdminForum;break;
      case '7':  rc = p->EmailAlert;break;
      case 'A':  rc = p->Announce;  break;
      case 'C':  rc = p->Chat;      break;
      case 'D':  rc = p->Debug;     break;
      case 'L':  rc = g.zLogin && *g.zLogin; break;
      /* Mainenance reminder: '@' should not be used because
         it would semantically collide with the @ in the
         capexpr TH1 command. */
      default:   rc = 0;            break;
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
  memset( &g.perm, 0, sizeof(g.perm) );

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
  login_set_capabilities(zCap, 0);
  login_anon_once = 1;
  login_set_anon_nobody_capabilities();
}

/*
** Return true if the user is "nobody"
*/
int login_is_nobody(void){
  return g.zLogin==0 || g.zLogin[0]==0 || fossil_strcmp(g.zLogin,"nobody")==0;
}

/*
** Return true if the user is a specific individual, not "nobody" or
** "anonymous".
*/
int login_is_individual(void){
  return g.zLogin!=0 && g.zLogin[0]!=0 && fossil_strcmp(g.zLogin,"nobody")!=0
           && fossil_strcmp(g.zLogin,"anonymous")!=0;
}

/*
** Return the login name.  If no login name is specified, return "nobody".
*/
const char *login_name(void){
  return (g.zLogin && g.zLogin[0]) ? g.zLogin : "nobody";
}

/*
** Call this routine when the credential check fails.  It causes
** a redirect to the "login" page.
*/
void login_needed(int anonOk){
#ifdef FOSSIL_ENABLE_JSON
  if(g.json.isJsonMode){
    json_err( FSL_JSON_E_DENIED, NULL, 1 );
    fossil_exit(0);
    /* NOTREACHED */
    assert(0);
  }else
#endif /* FOSSIL_ENABLE_JSON */
  {
    const char *zQS = P("QUERY_STRING");
    const char *zPathInfo = PD("PATH_INFO","");
    Blob redir;
    blob_init(&redir, 0, 0);
    if( zPathInfo[0]=='/' ) zPathInfo++; /* skip leading slash */
    if( fossil_wants_https(1) ){
      blob_appendf(&redir, "%s/login?g=%T", g.zHttpsURL, zPathInfo);
    }else{
      blob_appendf(&redir, "%R/login?g=%T", zPathInfo);
    }
    if( zQS && zQS[0] ){
      blob_appendf(&redir, "%%3f%T", zQS);
    }
    if( anonOk ) blob_append(&redir, "&anon=1", 7);
    cgi_redirect(blob_str(&redir));
    /* NOTREACHED */
    assert(0);
  }
}

/*
** Call this routine if the user lacks g.perm.Hyperlink permission.  If
** the anonymous user has Hyperlink permission, then paint a mesage
** to inform the user that much more information is available by
** logging in as anonymous.
*/
void login_anonymous_available(void){
  if( !g.perm.Hyperlink && g.anon.Hyperlink && anon_cookie_lifespan()>0 ){
    const char *zUrl = PD("PATH_INFO", "");
    @ <p>Many <span class="disabled">hyperlinks are disabled.</span><br>
    @ Use <a href="%R/login?anon=1&amp;g=%T(zUrl)">anonymous login</a>
    @ to enable hyperlinks.</p>
  }
}

/*
** While rendering a form, call this routine to add the Anti-CSRF token
** as a hidden element of the form.
*/
void login_insert_csrf_secret(void){
  @ <input type="hidden" name="csrf" value="%s(g.zCsrfToken)">
}

/*
** Check to see if the candidate username zUserID is already used.
** Return 1 if it is already in use.  Return 0 if the name is
** available for a self-registeration.
*/
static int login_self_choosen_userid_already_exists(const char *zUserID){
  int rc = db_exists(
    "SELECT 1 FROM user WHERE login=%Q "
    "UNION ALL "
    "SELECT 1 FROM event WHERE user=%Q OR euser=%Q",
    zUserID, zUserID, zUserID
  );
  return rc;
}

/*
** zEMail is an email address.  (Example:  "xyz@gmail.com".)  This routine
** searches for a user or subscriber that has that email address.  If the
** email address is used no-where in the system, return 0.  If the email
** address is assigned to a particular user return the UID for that user.
** If the email address is used, but not by a particular user, return -1.
*/
static int email_address_in_use(const char *zEMail){
  int uid;
  uid = db_int(0,
    "SELECT uid FROM user"
    " WHERE info LIKE '%%<%q>%%'", zEMail);
  if( uid>0 ){
    if( db_exists("SELECT 1 FROM user WHERE uid=%d AND ("
                  "   cap GLOB '*[as]*' OR"
                  "   find_emailaddr(info)<>%Q COLLATE nocase)",
                  uid, zEMail) ){
      uid = -1;
    }
  }
  if( uid==0 && alert_tables_exist() ){
    uid = db_int(0,
      "SELECT user.uid FROM subscriber JOIN user ON login=suname"
      " WHERE semail=%Q AND sverified", zEMail);
    if( uid ){
      if( db_exists("SELECT 1 FROM user WHERE uid=%d AND "
                    "   cap GLOB '*[as]*'",
                    uid) ){
        uid = -1;
      }
    }
  }
  return uid;
}

/*
** COMMAND: test-email-used
** Usage:  fossil test-email-used EMAIL ...
**
** Given a list of email addresses, show the UID and LOGIN associated
** with each one.
*/
void test_email_used(void){
  int i;
  db_find_and_open_repository(0, 0);
  verify_all_options();
  if( g.argc<3 ){
    usage("EMAIL ...");
  }
  for(i=2; i<g.argc; i++){
    const char *zEMail = g.argv[i];
    int uid = email_address_in_use(zEMail);
    if( uid==0 ){
      fossil_print("%s:  not used\n", zEMail);
    }else if( uid<0 ){
      fossil_print("%s:  used but no password reset is available\n", zEMail);
    }else{
      char *zLogin = db_text(0, "SELECT login FROM user WHERE uid=%d", uid);
      fossil_print("%s:  UID %d (%s)\n", zEMail, uid, zLogin);
      fossil_free(zLogin);
    }
  }
}


/*
** Check an email address and confirm that it is valid for self-registration.
** The email address is known already to be well-formed.  Return true
** if the email address is on the allowed list.
**
** The default behavior is that any valid email address is accepted.
** But if the "auth-sub-email" setting exists and is not empty, then
** it is a comma-separated list of GLOB patterns for email addresses
** that are authorized to self-register.
*/
int authorized_subscription_email(const char *zEAddr){
  char *zGlob = db_get("auth-sub-email",0);
  char *zAddr;
  int rc;

  if( zGlob==0 || zGlob[0]==0 ) return 1;
  zGlob = fossil_strtolwr(fossil_strdup(zGlob));
  zAddr = fossil_strtolwr(fossil_strdup(zEAddr));
  rc = glob_multi_match(zGlob, zAddr);
  fossil_free(zGlob);
  fossil_free(zAddr);
  return rc!=0;
}

/*
** WEBPAGE: register
**
** Page to allow users to self-register.  The "self-register" setting
** must be enabled for this page to operate.
*/
void register_page(void){
  const char *zUserID, *zPasswd, *zConfirm, *zEAddr;
  const char *zDName;
  unsigned int uSeed;
  const char *zDecoded;
  int iErrLine = -1;
  const char *zErr = 0;
  int uid = 0;              /* User id with the same email */
  int captchaIsCorrect = 0; /* True on a correct captcha */
  char *zCaptcha = "";      /* Value of the captcha text */
  char *zPerms;             /* Permissions for the default user */
  int canDoAlerts = 0;      /* True if receiving email alerts is possible */
  int doAlerts = 0;         /* True if subscription is wanted too */

  if( !db_get_boolean("self-register", 0) ){
    style_header("Registration not possible");
    @ <p>This project does not allow user self-registration. Please contact the
    @ project administrator to obtain an account.</p>
    style_finish_page();
    return;
  }
  if( P("pwreset")!=0 && login_self_password_reset_available() ){
    /* The "Request Password Reset" button was pressed, so render the
    ** "Request Password Reset" page instead of this one. */
    login_reqpwreset_page();
    return;
  }
  zPerms = db_get("default-perms", "u");
  login_check_credentials();

  /* Prompt the user for email alerts if this repository is configured for
  ** email alerts and if the default permissions include "7" */
  canDoAlerts = alert_tables_exist() && (db_int(0,
    "SELECT fullcap(%Q) GLOB '*7*'", zPerms
  ) || db_get_boolean("selfreg-verify",0));
  doAlerts = canDoAlerts && atoi(PD("alerts","1"))!=0;

  zUserID = PDT("u","");
  zPasswd = PDT("p","");
  zConfirm = PDT("cp","");
  zEAddr = PDT("ea","");
  zDName = PDT("dn","");

  /* Verify user imputs */
  if( P("new")==0 || !cgi_csrf_safe(2) ){
    /* This is not a valid form submission.  Fall through into
    ** the form display */
  }else if( (captchaIsCorrect = captcha_is_correct(1))==0 ){
    iErrLine = 6;
    zErr = "Incorrect CAPTCHA";
  }else if( strlen(zUserID)<6 ){
    iErrLine = 1;
    zErr = "User ID too short. Must be at least 6 characters.";
  }else if( sqlite3_strglob("*[^-a-zA-Z0-9_.]*",zUserID)==0 ){
    iErrLine = 1;
    zErr = "User ID may not contain spaces or special characters.";
  }else if( sqlite3_strlike("anonymous%", zUserID, 0)==0
         || sqlite3_strlike("nobody%", zUserID, 0)==0
         || sqlite3_strlike("reader%", zUserID, 0)==0
         || sqlite3_strlike("developer%", zUserID, 0)==0
  ){
    iErrLine = 1;
    zErr = "This User ID is reserved. Choose something different.";
  }else if( zDName[0]==0 ){
    iErrLine = 2;
    zErr = "Required";
  }else if( zEAddr[0]==0 ){
    iErrLine = 3;
    zErr = "Required";
  }else if( email_address_is_valid(zEAddr,0)==0 ){
    iErrLine = 3;
    zErr = "Not a valid email address";
  }else if( authorized_subscription_email(zEAddr)==0 ){
    iErrLine = 3;
    zErr = "Not an authorized email address";
  }else if( strlen(zPasswd)<6 ){
    iErrLine = 4;
    zErr = "Password must be at least 6 characters long";
  }else if( fossil_strcmp(zPasswd,zConfirm)!=0 ){
    iErrLine = 5;
    zErr = "Passwords do not match";
  }else if( (uid = email_address_in_use(zEAddr))!=0 ){
    iErrLine = 3;
    zErr = "This email address is already associated with a user";
  }else if( login_self_choosen_userid_already_exists(zUserID) ){
    iErrLine = 1;
    zErr = "This User ID is already taken. Choose something different.";
  }else{
    /* If all of the tests above have passed, that means that the submitted
    ** form contains valid data and we can proceed to create the new login */
    Blob sql;
    int uid;
    char *zPass = sha1_shared_secret(zPasswd, zUserID, 0);
    const char *zStartPerms = zPerms;
    if( db_get_boolean("selfreg-verify",0) ){
      /* If email verification is required for self-registration, initalize
      ** the new user capabilities to just "7" (Sign up for email).  The
      ** full "default-perms" permissions will be added when they click
      ** the verification link on the email they are sent. */
      zStartPerms = "7";
    }
    blob_init(&sql, 0, 0);
    blob_append_sql(&sql,
       "INSERT INTO user(login,pw,cap,info,mtime)\n"
       "VALUES(%Q,%Q,%Q,"
       "'%q <%q>\nself-register from ip %q on '||datetime('now'),now())",
       zUserID, zPass, zStartPerms, zDName, zEAddr, g.zIpAddr);
    fossil_free(zPass);
    db_unprotect(PROTECT_USER);
    db_multi_exec("%s", blob_sql_text(&sql));
    db_protect_pop();
    uid = db_int(0, "SELECT uid FROM user WHERE login=%Q", zUserID);
    login_set_user_cookie(zUserID, uid, NULL, 0);
    if( doAlerts ){
      /* Also make the new user a subscriber. */
      Blob hdr, body;
      AlertSender *pSender;
      const char *zCode;  /* New subscriber code (in hex) */
      const char *zGoto = P("g");
      int nsub = 0;
      char ssub[20];
      CapabilityString *pCap;
      pCap = capability_add(0, zPerms);
      capability_expand(pCap);
      ssub[nsub++] = 'a';
      if( capability_has_any(pCap,"o") ) ssub[nsub++] = 'c';
      if( capability_has_any(pCap,"2") ) ssub[nsub++] = 'f';
      if( capability_has_any(pCap,"r") ) ssub[nsub++] = 't';
      if( capability_has_any(pCap,"j") ) ssub[nsub++] = 'w';
      ssub[nsub] = 0;
      capability_free(pCap);
      /* Also add the user to the subscriber table. */
      zCode = db_text(0,
        "INSERT INTO subscriber(semail,suname,"
        "  sverified,sdonotcall,sdigest,ssub,sctime,mtime,smip,lastContact)"
        " VALUES(%Q,%Q,%d,0,%d,%Q,now(),now(),%Q,now()/86400)"
        " ON CONFLICT(semail) DO UPDATE"
        "   SET suname=excluded.suname"
        " RETURNING hex(subscriberCode);",
        /* semail */    zEAddr,
        /* suname */    zUserID,
        /* sverified */ 0,
        /* sdigest */   0,
        /* ssub */      ssub,
        /* smip */      g.zIpAddr
      );
      if( db_exists("SELECT 1 FROM subscriber WHERE semail=%Q"
                    "  AND sverified", zEAddr) ){
        /* This the case where the user was formerly a verified subscriber
        ** and here they have also registered as a user as well.  It is
        ** not necessary to repeat the verfication step */
        login_redirect_to_g();
      }
      /* A verification email */
      pSender = alert_sender_new(0,0);
      blob_init(&hdr,0,0);
      blob_init(&body,0,0);
      blob_appendf(&hdr, "To: <%s>\n", zEAddr);
      blob_appendf(&hdr, "Subject: Subscription verification\n");
      alert_append_confirmation_message(&body, zCode);
      alert_send(pSender, &hdr, &body, 0);
      style_header("Email Verification");
      if( pSender->zErr ){
        @ <h1>Internal Error</h1>
        @ <p>The following internal error was encountered while trying
        @ to send the confirmation email:
        @ <blockquote><pre>
        @ %h(pSender->zErr)
        @ </pre></blockquote>
      }else{
        @ <p>An email has been sent to "%h(zEAddr)". That email contains a
        @ hyperlink that you must click to activate your account.</p>
      }
      alert_sender_free(pSender);
      if( zGoto ){
        @ <p><a href='%h(zGoto)'>Continue</a>
      }
      style_finish_page();
      return;
    }
    login_redirect_to_g();
  }

  /* Prepare the captcha. */
  if( captchaIsCorrect ){
    uSeed = strtoul(P("captchaseed"),0,10);
  }else{
    uSeed = captcha_seed();
  }
  zDecoded = captcha_decode(uSeed, 0);
  zCaptcha = captcha_render(zDecoded);

  style_header("Register");
  /* Print out the registration form. */
  g.perm.Hyperlink = 1;  /* Artificially enable hyperlinks */
  form_begin(0, "%R/register");
  if( P("g") ){
    @ <input type="hidden" name="g" value="%h(P("g"))">
  }
  @ <p><input type="hidden" name="captchaseed" value="%u(uSeed)">
  @ <table class="login_out">
  @ <tr>
  @   <td class="form_label" align="right" id="uid">User ID:</td>
  @   <td><input aria-labelledby="uid" type="text" name="u" \
  @ value="%h(zUserID)" size="30" autofocus></td>
  @
  if( iErrLine==1 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
  }
  @ <tr>
  @   <td class="form_label" align="right" id="dpyname">Display Name:</td>
  @   <td><input aria-labelledby="dpyname" type="text" name="dn" \
  @ value="%h(zDName)" size="30"></td>
  @ </tr>
  if( iErrLine==2 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
  }
  @ </tr>
  @ <tr>
  @   <td class="form_label" align="right" id="emaddr">Email Address:</td>
  @   <td><input aria-labelledby="emaddr" type="text" name="ea" \
  @ value="%h(zEAddr)" size="30"></td>
  @ </tr>
  if( iErrLine==3 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span>
    if( uid>0 && login_self_password_reset_available() ){
      @ <br>
      @ <input type="submit" name="pwreset" \
      @ value="Request Password Reset For %h(zEAddr)">
    }
    @ </td></tr>
  }
  if( canDoAlerts ){
    int a = atoi(PD("alerts","1"));
    @ <tr>
    @   <td class="form_label" align="right" id="emalrt">Email&nbsp;Alerts?</td>
    @   <td><select aria-labelledby="emalrt" size='1' name='alerts'>
    @       <option value="1" %s(a?"selected":"")>Yes</option>
    @       <option value="0" %s(!a?"selected":"")>No</option>
    @   </select></td></tr>
  }
  @ <tr>
  @   <td class="form_label" align="right" id="pswd">Password:</td>
  @   <td><input aria-labelledby="pswd" type="password" name="p" \
  @ value="%h(zPasswd)" size="30"> \
  if( zPasswd[0]==0 ){
    char *zRPW = fossil_random_password(12);
    @ Password suggestion: %z(zRPW)</td>
  }else{
    @ </td>
  }
  @ <tr>
  if( iErrLine==4 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
  }
  @ <tr>
  @   <td class="form_label" align="right" id="pwcfrm">Confirm:</td>
  @   <td><input aria-labelledby="pwcfrm" type="password" name="cp" \
  @ value="%h(zConfirm)" size="30"></td>
  @ </tr>
  if( iErrLine==5 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
  }
  @ <tr>
  @   <td class="form_label" align="right" id="cptcha">Captcha:</td>
  @   <td><input type="text" name="captcha" aria-labelledby="cptcha" \
  @ value="%h(captchaIsCorrect?zDecoded:"")" size="30">
  captcha_speakit_button(uSeed, "Speak the captcha text");
  @   </td>
  @ </tr>
  if( iErrLine==6 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
  }
  @ <tr><td></td>
  @ <td><input type="submit" name="new" value="Register"></td></tr>
  @ </table>
  @ <div class="captcha"><table class="captcha"><tr><td><pre class="captcha">
  @ %h(zCaptcha)
  @ </pre>
  @ Enter this 8-letter code in the "Captcha" box above.
  @ </td></tr></table></div>
  @ </form>
  style_finish_page();

  free(zCaptcha);
}

/*
** WEBPAGE: reqpwreset
**
** A web page to request a password reset.
**
** A form is presented where the user can enter their email address
** and a captcha.  If the email address entered corresponds to a known
** users, an email is sent to that address that contains a link to the
** /resetpw page that allows the users to enter a new password.
**
** This page is only available if the self-pw-reset property is enabled
** and email notifications are configured and operating.  Password resets
** are not available to users with Admin or Setup privilege.
*/
void login_reqpwreset_page(void){
  const char *zEAddr;
  const char *zDecoded;
  unsigned int uSeed;
  int iErrLine = -1;
  const char *zErr = 0;
  int uid = 0;              /* User id with the email zEAddr */
  int captchaIsCorrect = 0; /* True on a correct captcha */
  char *zCaptcha = "";      /* Value of the captcha text */

  if( !login_self_password_reset_available() ){
    style_header("Password reset not possible");
    @ <p>This project does not allow users to reset their own passwords.
    @ If you need a password reset, you will have to negotiate that directly
    @ with the project administrator.
    style_finish_page();
    return;
  }
  zEAddr = PDT("ea","");

  /* Verify user imputs */
  if( !cgi_csrf_safe(1) || P("reqpwreset")==0 ){
    /* This is the initial display of the form.  No processing or error
    ** checking is to be done. Fall through into the form display
    **
    ** cgi_csrf_safe():  Nothing interesting happens on this page without
    ** a valid captcha solution, so we only need to check referrer and that
    ** the request is a POST.
    */
  }else if( (captchaIsCorrect = captcha_is_correct(1))==0 ){
    iErrLine = 2;
    zErr = "Incorrect CAPTCHA";
  }else if( zEAddr[0]==0 ){
    iErrLine = 1;
    zErr = "Required";
  }else if( email_address_is_valid(zEAddr,0)==0 ){
    iErrLine = 1;
    zErr = "Not a valid email address";
  }else if( authorized_subscription_email(zEAddr)==0 ){
    iErrLine = 1;
    zErr = "Not an authorized email address";
  }else if( (uid = email_address_in_use(zEAddr))<=0 ){
    iErrLine = 1;
    zErr = "This email address is not associated with a user who has "
           "password reset privileges.";
  }else if( login_set_uid(uid,0)==0 || g.perm.Admin || g.perm.Setup
            || !g.perm.Password ){
    iErrLine = 1;
    zErr = "This email address is not associated with a user who has "
           "password reset privileges.";
  }else{

    /* If all of the tests above have passed, that means that the submitted
    ** form contains valid data and we can proceed to issue the password
    ** reset email. */
    Blob hdr, body;
    AlertSender *pSender;
    char *zUrl = login_resetpw_suffix(uid, 0);
    pSender = alert_sender_new(0,0);
    blob_init(&hdr,0,0);
    blob_init(&body,0,0);
    blob_appendf(&hdr, "To: <%s>\n", zEAddr);
    blob_appendf(&hdr, "Subject: Password reset for %s\n", g.zBaseURL);
    blob_appendf(&body,
      "Someone has requested to reset the password for user \"%s\"\n",
      g.zLogin);
    blob_appendf(&body, "at %s.\n\n", g.zBaseURL);
    blob_appendf(&body,
       "If you did not request this password reset, ignore\n"
       "this email\n\n");
    blob_appendf(&body,
       "To reset the password, visit the following link:\n\n"
       "    %s/resetpw/%s\n\n", g.zBaseURL, zUrl);
    fossil_free(zUrl);
    alert_send(pSender, &hdr, &body, 0);
    style_header("Email Verification");
    if( pSender->zErr ){
      @ <h1>Internal Error</h1>
      @ <p>The following internal error was encountered while trying
      @ to send the confirmation email:
      @ <blockquote><pre>
      @ %h(pSender->zErr)
      @ </pre></blockquote>
    }else{
      @ <p>An email containing a hyperlink that can be used to reset
      @ your password has been sent to "%h(zEAddr)".</p>
    }
    alert_sender_free(pSender);
    style_finish_page();
    return;
  }

  /* Prepare the captcha. */
  if( captchaIsCorrect ){
    uSeed = strtoul(P("captchaseed"),0,10);
  }else{
    uSeed = captcha_seed();
  }
  zDecoded = captcha_decode(uSeed, 0);
  zCaptcha = captcha_render(zDecoded);

  style_header("Request Password Reset");
  /* Print out the registration form. */
  g.perm.Hyperlink = 1;  /* Artificially enable hyperlinks */
  form_begin(0, "%R/reqpwreset");
  @ <p><input type="hidden" name="captchaseed" value="%u(uSeed)">
  @ <p><input type="hidden" name="reqpwreset" value="1">
  @ <table class="login_out">
  @ <tr>
  @   <td class="form_label" align="right" id="emaddr">Email Address:</td>
  @   <td><input aria-labelledby="emaddr" type="text" name="ea" \
  @ value="%h(zEAddr)" size="30"></td>
  @ </tr>
  if( iErrLine==1 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
  }
  @ <tr>
  @   <td class="form_label" align="right" id="cptcha">Captcha:</td>
  @   <td><input type="text" name="captcha" aria-labelledby="cptcha" \
  @ value="%h(captchaIsCorrect?zDecoded:"")" size="30">
  captcha_speakit_button(uSeed, "Speak the captcha text");
  @   </td>
  @ </tr>
  if( iErrLine==2 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
  }
  @ <tr><td></td>
  @ <td><input type="submit" name="new" value="Request Password Reset"/>\
  @ </td></tr>
  @ </table>
  @ <div class="captcha"><table class="captcha"><tr><td><pre class="captcha">
  @ %h(zCaptcha)
  @ </pre>
  @ Enter this 8-letter code in the "Captcha" box above.
  @ </td></tr></table></div>
  @ </form>
  style_finish_page();
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
    if( file_size(zRepoName, ExtFILE)<0 ){
      /* Silently remove non-existent repositories from the login group. */
      const char *zLabel = db_column_text(&q, 0);
      db_unprotect(PROTECT_CONFIG);
      db_multi_exec(
         "DELETE FROM config WHERE name GLOB 'peer-*-%q'",
         &zLabel[10]
      );
      db_protect_pop();
      continue;
    }
    rc = sqlite3_open_v2(
         zRepoName, &pPeer,
         SQLITE_OPEN_READWRITE,
         g.zVfsName
    );
    if( rc!=SQLITE_OK ){
      blob_appendf(&err, "%s%s: %s%s", zPrefix, zRepoName,
                   sqlite3_errmsg(pPeer), zSuffix);
      nErr++;
      sqlite3_close(pPeer);
      continue;
    }
    sqlite3_create_function(pPeer, "shared_secret", 3, SQLITE_UTF8,
                            0, sha1_shared_secret_sql_function, 0, 0);
    sqlite3_create_function(pPeer, "now", 0,SQLITE_UTF8,0,db_now_function,0,0);
    sqlite3_busy_timeout(pPeer, 5000);
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
  int bPwRequired,           /* True if the login,password is required */
  const char *zLogin,        /* Login name for the other repo */
  const char *zPassword,     /* Password to prove we are authorized to join */
  const char *zNewName,      /* Name of new login group if making a new one */
  char **pzErrMsg            /* Leave an error message here */
){
  Blob fullName;             /* Blob for finding full pathnames */
  sqlite3 *pOther;           /* The other repository */
  int rc;                    /* Return code from sqlite3 functions */
  char *zOtherProjCode;      /* Project code for pOther */
  char *zSelfRepo;           /* Name of our repository */
  char *zSelfLabel;          /* Project-name for our repository */
  char *zSelfProjCode;       /* Our project-code */
  char *zSql;                /* SQL to run on all peers */
  const char *zSelf;         /* The ATTACH name of our repository */

  *pzErrMsg = 0;   /* Default to no errors */
  zSelf = "repository";

  /* Get the full pathname of the other repository */
  file_canonical_name(zRepo, &fullName, 0);
  zRepo = fossil_strdup(blob_str(&fullName));
  blob_reset(&fullName);

  /* Get the full pathname for our repository.  Also the project code
  ** and project name for ourself. */
  file_canonical_name(g.zRepositoryName, &fullName, 0);
  zSelfRepo = fossil_strdup(blob_str(&fullName));
  blob_reset(&fullName);
  zSelfProjCode = db_get("project-code", "unknown");
  zSelfLabel = db_get("project-name", 0);
  if( zSelfLabel==0 ){
    zSelfLabel = zSelfProjCode;
  }

  /* Make sure we are not trying to join ourselves */
  if( fossil_strcmp(zRepo, zSelfRepo)==0 ){
    *pzErrMsg = mprintf("The \"other\" repository is the same as this one.");
    return;
  }

  /* Make sure the other repository is a valid Fossil database */
  if( file_size(zRepo, ExtFILE)<0 ){
    *pzErrMsg = mprintf("repository file \"%s\" does not exist", zRepo);
    return;
  }
  rc = sqlite3_open_v2(
       zRepo, &pOther,
       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
       g.zVfsName
  );
  if( rc!=SQLITE_OK ){
    *pzErrMsg = fossil_strdup(sqlite3_errmsg(pOther));
  }else{
    rc = sqlite3_exec(pOther, "SELECT count(*) FROM user", 0, 0, pzErrMsg);
  }
  sqlite3_close(pOther);
  if( rc ) return;

  /* Attach the other repository.  Make sure the username/password is
  ** valid and has Setup permission.
  */
  db_attach(zRepo, "other");
  zOtherProjCode = db_text("x", "SELECT value FROM other.config"
                                " WHERE name='project-code'");
  if( bPwRequired ){
    char *zPwHash;             /* Password hash on pOther */
    zPwHash = sha1_shared_secret(zPassword, zLogin, zOtherProjCode);
    if( !db_exists(
      "SELECT 1 FROM other.user"
      " WHERE login=%Q AND cap GLOB '*s*'"
      "   AND (pw=%Q OR pw=%Q)",
      zLogin, zPassword, zPwHash)
    ){
      db_detach("other");
      *pzErrMsg = "The supplied username/password does not correspond to a"
                  " user Setup permission on the other repository.";
      return;
    }
  }

  /* Create all the necessary CONFIG table entries on both the
  ** other repository and on our own repository.
  */
  zSelfProjCode = abbreviated_project_code(zSelfProjCode);
  zOtherProjCode = abbreviated_project_code(zOtherProjCode);
  db_begin_transaction();
  db_unprotect(PROTECT_CONFIG);
  db_multi_exec(
    "DELETE FROM \"%w\".config WHERE name GLOB 'peer-*';"
    "INSERT INTO \"%w\".config(name,value) VALUES('peer-repo-%q',%Q);"
    "INSERT INTO \"%w\".config(name,value) "
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
    "REPLACE INTO \"%w\".config(name,value)"
    "  SELECT name, value FROM other.config"
    "   WHERE name GLOB 'peer-*' OR name GLOB 'login-group-*'",
    zSelf
  );
  db_protect_pop();
  db_end_transaction(0);
  db_multi_exec("DETACH other");

  /* Propagate the changes to all other members of the login-group */
  zSql = mprintf(
    "BEGIN;"
    "REPLACE INTO config(name,value,mtime) VALUES('peer-name-%q',%Q,now());"
    "REPLACE INTO config(name,value,mtime) VALUES('peer-repo-%q',%Q,now());"
    "COMMIT;",
    zSelfProjCode, zSelfLabel, zSelfProjCode, zSelfRepo
  );
  db_unprotect(PROTECT_CONFIG);
  login_group_sql(zSql, "<li> ", "</li>", pzErrMsg);
  db_protect_pop();
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
  db_unprotect(PROTECT_CONFIG);
  login_group_sql(zSql, "<li> ", "</li>", pzErrMsg);
  fossil_free(zSql);
  db_multi_exec(
    "DELETE FROM config "
    " WHERE name GLOB 'peer-*'"
    "    OR name GLOB 'login-group-*';"
  );
  db_protect_pop();
}

/*
** COMMAND: login-group*
**
** Usage: %fossil login-group ?SUBCOMMAND? ?OPTIONS?
**
** Run various subcommands to manage login-group related settings of the open
** repository or of the repository identified by the -R or --repository option.
**
** >  fossil login-group ?-R REPO?
**
**     Show the login-group to which REPO, or if invoked from within a check-out
**     the repository on which the current check-out is based, belongs.
**
** >  fossil login-group join ?-R REPO? ?--name NAME? REPO2
**
**     This command will either: (1) add the repository on which the current
**     check-out is based, or the repository REPO specified with -R, to the
**     login group where REPO2 is a member, in which case the optional --name
**     argument is not required; or (2) create a new login group between the
**     repository on which the current check-out is based, or the repository
**     REPO specified with -R, and REPO2, in which case the new group NAME is
**     determined by the mandatory --name option. In both cases, the specified
**     repositories will first leave any group in which they are currently a
**     member before joining the new login group.
**
** >  fossil login-group leave ?-R REPO?
**
**     Take the repository REPO, or if invoked from within a check-out the
**     repository on which the current check-out is based, out of whatever
**     login group it is a member.
**
** About Login Groups:
**
** A login-group is a set of repositories that share user credentials.
** If a user is logged into one member of the group, then that user can
** access any other group member as long as they have an entry in the USER
** table of that member.  If a user changes their password using web
** interface, their password is also automatically changed in every other
** member of the login group.
*/
void login_group_command(void){
  const char *zLGName;
  const char *zCmd;
  int nCmd;
  Stmt q;
  db_find_and_open_repository(0, 0);
  if( g.argc>2 ){
    zCmd = g.argv[2];
    nCmd = (int)strlen(zCmd);
    if( strncmp(zCmd,"join",nCmd)==0 && nCmd>=1 ){
      const char *zNewName = find_option("name",0,1);
      const char *zOther = 0;
      char *zErr = 0;
      verify_all_options();
      if( g.argc!=4 ){
        fossil_fatal("unexpected argument count for \"login-group join\"");
      }
      zOther = g.argv[3];
      login_group_leave(&zErr);
      sqlite3_free(zErr);
      zErr = 0;
      login_group_join(zOther,0,0,0,zNewName,&zErr);
      if( zErr ){
        fossil_fatal("%s", zErr);
      }
    }else if( strncmp(zCmd,"leave",nCmd)==0 && nCmd>=1 ){
      verify_all_options();
      if( g.argc!=3 ){
        fossil_fatal("unknown extra arguments to \"login-group leave\"");
      }
      zLGName = login_group_name();
      if( zLGName ){
        char *zErr = 0;
        fossil_print("Leaving login-group \"%s\"\n", zLGName);
        login_group_leave(&zErr);
        if( zErr ) fossil_fatal("Oops: %s", zErr);
        return;
      }
    }else{
      fossil_fatal("unknown command \"%s\" - should be \"join\" or \"leave\"",
                   zCmd);
    }
  }
  /* Show the current login group information */
  zLGName = login_group_name();
  if( zLGName==0 ){
    fossil_print("Not currently a part of any login-group\n");
    return;
  }
  fossil_print("Now part of login-group \"%s\" with:\n", zLGName);
  db_prepare(&q, "SELECT value FROM config WHERE name LIKE 'peer-repo-%%'");
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("  %s\n", db_column_text(&q,0));
  }
  db_finalize(&q);

}
