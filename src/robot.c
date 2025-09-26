/*
** Copyright (c) 2025 D. Richard Hipp
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
** This file contains code that attempts to prevent robots and
** especially bot-nets from consume excess CPU and bandwidth when
** Fossil is run as a service.
*/
#include "config.h"
#include "robot.h"
#include <assert.h>
#include <time.h>

/*
** The name of the cookie used to demonstrate that the client has been
** tested and is believed to be operated by a human, not by a robot.
*/
#if INTERFACE
#define ROBOT_COOKIE  "fossil-client-ok"
#endif

/*
** Values computed only once and then cached.
*/
static struct RobotCache {
  unsigned int h1, h2;       /* Proof-of-work hash values */
  unsigned int resultCache;  /* 0: unknown.  1: human  2: might-be-robot */
} robot = { 0, 0, 0 };

/*
** Allowed values for robot.resultCache
*/
#define KNOWN_NOT_ROBOT  1
#define MIGHT_BE_ROBOT   2

/*
** Compute two hashes, robot.h1 and robot.h2, that are used as
** part of determining whether or not the HTTP client is a robot.
** These hashes are based on current time, client IP address,
** and User-Agent.  robot.h1 is for the current time slot and
** robot.h2 is the previous.
**
** The hashes are integer values between 100,000,000 and 999,999,999
** inclusive.
*/
static void robot_pow_hash(void){
  const char *az[2], *z;
  sqlite3_int64 tm;
  unsigned int h1, h2, k;

  if( robot.h1 ) return;   /* Already computed */

  /* Construct a proof-of-work value based on the IP address of the
  ** sender and the sender's user-agent string.  The current time also
  ** affects the pow value, so actually compute two values, one for the
  ** current 900-second interval and one for the previous.  Either can
  ** match.  The pow-value is an integer between 100,000,000 and
  ** 999,999,999.
  */
  az[0] = P("REMOTE_ADDR");
  az[1] = P("HTTP_USER_AGENT");
  tm = time(0);
  h1 = (unsigned)(tm/900)&0xffffffff;
  h2 = h1 - 1;
  for(k=0; k<2; k++){
    z = az[k];
    if( z==0 ) continue;
    while( *z ){
      h1 = (h1 + *(unsigned char*)z)*0x9e3779b1;
      h2 = (h2 + *(unsigned char*)z)*0x9e3779b1;
      z++;
    }
  }
  robot.h1 = (h1 % 900000000) + 100000000;
  robot.h2 = (h2 % 900000000) + 100000000;
}

/*
** Return true if the HTTP client has not demonstrated that it is
** human interactive.  Return false is the HTTP client might be
** a non-interactive robot.
**
** For this routine, any of the following is considered proof that
** the HTTP client is not a robot:
**
**   1.   There is a valid login, including "anonymous".  User "nobody"
**        is not a valid login, but every other user is.
**
**   2.   There exists a ROBOT_COOKIE with the correct proof-of-work
**        value.
**
**   3.   There exists a proof=VALUE query parameter where VALUE is
**        a correct proof-of-work value.
**
**   4.   There exists a valid token=VALUE query parameter.
**
** After being run once, this routine caches its findings and
** returns very quickly on subsequent invocations.
*/
int client_might_be_a_robot(void){
  const char *z;

  /* Only do this computation once, then cache the results for future
  ** use */
  if( robot.resultCache ){
    return robot.resultCache==MIGHT_BE_ROBOT;
  }

  /* Condition 1:  Is there a valid login?
  */
  if( g.userUid==0 ){
    login_check_credentials();
  }
  if( g.zLogin!=0 ){
    robot.resultCache = KNOWN_NOT_ROBOT;
    return 0;
  }

  /* Condition 2:  If there is already a proof-of-work cookie
  ** with a correct value, then the user agent has been authenticated.
  */
  z = P(ROBOT_COOKIE);
  if( z ){
    unsigned h = atoi(z);
    robot_pow_hash();
    if( (h==robot.h1 || h==robot.h2) && !cgi_is_qp(ROBOT_COOKIE) ){
      robot.resultCache = KNOWN_NOT_ROBOT;
      return 0;
    }
  }

  /* Condition 3:  There is a "proof=VALUE" query parameter with a valid
  ** VALUE attached.  If this is the case, also set the robot cookie
  ** so that future requests will hit condition 2 above.
  */
  z = P("proof");
  if( z ){
    unsigned h = atoi(z);
    robot_pow_hash();
    if( h==robot.h1 || h==robot.h2 ){
      cgi_set_cookie(ROBOT_COOKIE,z,"/",900);
      robot.resultCache = KNOWN_NOT_ROBOT;
      return 0;
    }
    cgi_tag_query_parameter("proof");
  }

  /* Condition 4:  If there is a "token=VALUE" query parameter with a
  ** valid VALUE argument, then assume that the request is coming from
  ** either an interactive human session, or an authorized robot that we
  ** want to treat as human.  All it through and also set the robot cookie.
  */
  z = P("token");
  if( z!=0 ){
    if( db_exists("SELECT 1 FROM config"
                  " WHERE name='token-%q'"
                  "   AND json_valid(value,6)"
                  "   AND value->>'user' IS NOT NULL", z)
    ){
      char *zVal;
      robot_pow_hash();
      zVal = mprintf("%u", robot.h1);
      cgi_set_cookie(ROBOT_COOKIE,zVal,"/",900);
      fossil_free(zVal);
      robot.resultCache = KNOWN_NOT_ROBOT;
      return 0;                /* There is a valid token= query parameter */
    }
    cgi_tag_query_parameter("token");
  }

  /* We have no proof that the request is coming from an interactive
  ** human session, so assume the request comes from a robot.
  */
  robot.resultCache = MIGHT_BE_ROBOT;
  return 1;
}

/*
** Rewrite the current page with content that attempts
** to prove that the client is not a robot.
*/
static void ask_for_proof_that_client_is_not_robot(void){
  unsigned p1, p2, p3, p4, p5, k2, k3;
  int k;

  /* Ask the client to present proof-of-work */
  cgi_reset_content();
  cgi_set_content_type("text/html");
  style_header("Browser Verification");
  @ <h1 id="x1">Checking to see if you are a robot<span id="x2"></span></h1>
  @ <form method="GET" id="x6"><p>
  @ <span id="x3" style="visibility:hidden;">\
  @ Press <input type="submit" id="x5" value="Ok" focus> to continue</span>
  @ <span id="x7" style="visibility:hidden;">You appear to be a robot.</span>\
  @ </p>
  cgi_tag_query_parameter("name");
  cgi_query_parameters_to_hidden();
  @ <input id="x4" type="hidden" name="proof" value="0">
  @ </form>
  @ <script nonce='%s(style_nonce())'>
  @ function aaa(x){return document.getElementById(x);}\
  @ function bbb(h,a){\
  @ aaa("x4").value=h;\
  @ if((a%%75)==0){\
  @ aaa("x2").textContent=aaa("x2").textContent+".";\
  @ }var z;\
  @ if(a>0){\
  @ setTimeout(bbb,1,h+a,a-1);\
  @ }else if((z=window.getComputedStyle(document.body).zIndex)==='0'||z===0){\
  @ aaa("x3").style.visibility="visible";\
  @ aaa("x2").textContent="";\
  @ aaa("x1").textContent="All clear";\
  @ aaa("x6").onsubmit=function(){aaa("x3").style.visibility="hidden";};\
  @ aaa("x5").focus();\
  @ }else{\
  @ aaa("x7").style.visibility="visible";\
  @ aaa("x2").textContent="";\
  @ aaa("x3").style.display="none";\
  @ aaa("x1").textContent="Access Denied";\
  @ }\
  @ }\
  robot_pow_hash();
  k = 400 + robot.h2%299;
  k2 = (robot.h2/299)%99 + 973;
  k3 = (robot.h2/(299*99))%99 + 811;
  p1 = (k*k + k)/2;
  p2 = robot.h1-p1;
  p3 = p2%k2;
  p4 = (p2/k2)%k3;
  p5 = p2/(k2*k3);
  @ function ccc(a,b,c){return (a*%u(k3)+b)*%u(k2)+c;}\
  @ window.addEventListener('load',function(){\
  @ bbb(ccc(%u(p5),%u(p4),%u(p3)),%u(k));},false);
  /* Prevent successfully completed robot checks from reappearing and force
  ** incomplete checks to start over when navigating back and forward. More
  ** information: <https://stackoverflow.com/a/43043658>. */
  @ window.addEventListener('pageshow',function(e){if(e.persisted)\
    @ window.location.reload();});
  @ </script>
  style_finish_page();
}

/*
** SETTING: robot-restrict                width=40 block-text
** The VALUE of this setting is a list of GLOB patterns that match
** pages for which complex HTTP requests from unauthenicated clients
** should be disallowed.  "Unauthenticated" means the user is "nobody".
** The recommended value for this setting is:
**
**     timelineX,diff,annotate,zip,fileage,file,finfo,reports
**
** The "diff" tag covers all diffing pages such as /vdiff, /fdiff, and
** /vpatch.  The "annotate" tag also covers /blame and /praise.  "zip"
** also covers /tarball and /sqlar.  If a tag has an "X" character appended,
** then it only applies if query parameters are such that the page is
** particularly difficult to compute. In all other case, the tag should
** exactly match the page name.
**
** Change this setting "off" to disable all robot restrictions.
*/
/*
** SETTING: robot-exception              width=40 block-text
**
** The value of this setting should be a regular expression.
** If it matches the REQUEST_URI without the SCRIPT_NAME prefix
** matches this regular expression, then the request is an exception
** to anti-robot defenses and should be allowed through.  For
** example, to allow robots to download tarballs or ZIP archives
** for named versions and releases, you could use an expression like
** this:
**
**     ^/(tarball|zip)\\b*\\b(version-|release)\\b
**
** This setting can hold multiple regular expressions, one
** regular expression per line.  The input URL is exempted from
** anti-robot defenses if any of the multiple regular expressions
** matches.
*/

/*
** Return the default restriction GLOB
*/
const char *robot_restrict_default(void){
  return "timelineX,diff,annotate,zip,fileage,file,finfo,reports";
}

/*
** Return true if zTag matches one of the tags in the robot-restrict
** setting.
*/
int robot_restrict_has_tag(const char *zTag){
  static const char *zGlob = 0;
  if( zGlob==0 ){
    zGlob = db_get("robot-restrict",robot_restrict_default());
    if( zGlob==0 ) zGlob = "";
  }
  if( zGlob[0]==0 || fossil_strcmp(zGlob, "off")==0 ){
    return 0;
  }
  return glob_multi_match(zGlob,zTag);
}

/*
** Check the request URI to see if it matches one of the URI
** exceptions listed in the robot-exception setting.  Return true
** if it does.  Return false if it does not.
**
** For the purposes of this routine, the "request URI" means
** the REQUEST_URI value with the SCRIPT_NAME prefix removed and
** with QUERY_STRING appended with a "?" separator if QUERY_STRING
** is not empty.
**
** If the robot-exception setting does not exist or is an empty
** string, then return false.
*/
int robot_exception(void){
  const char *zRE = db_get("robot-exception",0);
  const char *zQS;    /* QUERY_STRING */
  const char *zURI;   /* REQUEST_URI */
  const char *zSN;    /* SCRIPT_NAME */
  const char *zNL;    /* Next newline character */
  char *zRequest;     /* REQUEST_URL w/o SCRIPT_NAME prefix + QUERY_STRING */
  int nRequest;       /* Length of zRequest in bytes */
  size_t nURI, nSN;   /* Length of zURI and zSN */
  int bMatch = 0;     /* True if there is a match */

  if( zRE==0 ) return 0;
  if( zRE[0]==0 ) return 0;
  zURI = PD("REQUEST_URI","");
  nURI = strlen(zURI);
  zSN = PD("SCRIPT_NAME","");
  nSN = strlen(zSN);
  if( nSN<=nURI ) zURI += nSN;
  zQS = P("QUERY_STRING");
  if( zQS && zQS[0] ){
    zRequest = mprintf("%s?%s", zURI, zQS);
  }else{
    zRequest = fossil_strdup(zURI);
  }
  nRequest = (int)strlen(zRequest);
  while( zRE[0] && bMatch==0 ){
    char *z;
    const char *zErr;
    size_t n;
    ReCompiled *pRe;
    zNL = strchr(zRE,'\n');
    if( zNL ){
      n = (size_t)(zNL - zRE)+1;
      while( zNL>zRE && fossil_isspace(zNL[0]) ) zNL--;
      if( zNL==zRE ){
        zRE += n;
        continue;
      }
    }else{
      n = strlen(zRE);
    }
    z = mprintf("%.*s", (int)(zNL - zRE)+1, zRE);
    zRE += n;
    zErr = fossil_re_compile(&pRe, z, 0);
    if( zErr ){
      fossil_warning("robot-exception error \"%s\" in expression \"%s\"\n",
                     zErr, z);
      fossil_free(z);
      continue;
    }
    fossil_free(z);
    bMatch = re_match(pRe, (const unsigned char*)zRequest, nRequest);
    re_free(pRe);
  }
  fossil_free(zRequest);
  return bMatch;
}

/*
** Check to see if the page named in the argument is on the
** robot-restrict list.  If it is on the list and if the user
** is "nobody" then bring up a captcha to test to make sure that
** client is not a robot.
**
** This routine returns true if a captcha was rendered and if subsequent
** page generation should be aborted.  It returns false if the page
** should not be restricted and should be rendered normally.
*/
int robot_restrict(const char *zTag){
  if( robot.resultCache==KNOWN_NOT_ROBOT ) return 0;
  if( !robot_restrict_has_tag(zTag) ) return 0;
  if( !client_might_be_a_robot() ) return 0;
  if( robot_exception() ){
    robot.resultCache = KNOWN_NOT_ROBOT;
    return 0;
  }

  /* Generate the proof-of-work captcha */
  ask_for_proof_that_client_is_not_robot();
  return 1;
}

/*
** WEBPAGE: test-robotck
**
** Run the robot_restrict() function using the value of the "name="
** query parameter as an argument.  Used for testing the robot_restrict()
** logic.
**
** Whenever this page is successfully rendered (when it doesn't go to
** the captcha) it deletes the proof-of-work cookie.  So reloading the
** page will reset the cookie and restart the verification.
*/
void robot_restrict_test_page(void){
  const char *zName = P("name");
  const char *zP1 = P("proof");
  const char *zP2 = P(ROBOT_COOKIE);
  const char *z;
  if( zName==0 || zName[0]==0 ) zName = g.zPath;
  login_check_credentials();
  if( g.zLogin==0 ){ login_needed(1); return; }
  g.zLogin = 0;
  if( robot_restrict(zName) ) return;
  style_set_current_feature("test");
  style_header("robot_restrict() test");
  @ <h1>Captcha passed</h1>
  @
  @ <p>
  if( zP1 && zP1[0] ){
     @ proof=%h(zP1)<br>
  }
  if( zP2 && zP2[0] ){
    @ %h(ROBOT_COOKIE)=%h(zP2)<br>
    cgi_set_cookie(ROBOT_COOKIE,"",0,-1);
  }
  if( g.perm.Admin ){
    z = db_get("robot-restrict",robot_restrict_default());
    if( z && z[0] ){
      @ robot-restrict=%h(z)</br>
    }
    @ robot.h1=%u(robot.h1)<br>
    @ robot.h2=%u(robot.h2)<br>
    switch( robot.resultCache ){
      case MIGHT_BE_ROBOT: {
        @ robot.resultCache=MIGHT_BE_ROBOT<br>
        break;
      }
      case KNOWN_NOT_ROBOT: {
        @ robot.resultCache=KNOWN_NOT_ROBOT<br>
        break;
      }
      default: {
        @ robot.resultCache=OTHER (%d(robot.resultCache))<br>
        break;
      }
    }
  }
  @ </p>
  @ <p><a href="%R/test-robotck/%h(zName)">Retry</a>
  style_finish_page();
}

/*
** WEBPAGE: tokens
**
** Allow users to create, delete, and view their access token.
**
** The access token is a string TOKEN which if included in a query
** parameter like "token=TOKEN" authenticates a request as coming
** from an authorized agent.  This can be used, for example, by
** script to access content without running into problems with
** robot defenses.
*/
void tokens_page(void){
  char *zMyToken;

  login_check_credentials();
  style_set_current_feature("tokens");
  style_header("Access Tokens");
  if( g.zLogin==0 || fossil_strcmp(g.zLogin,"anonymous")==0 ){
    @ User "%h(g.zLogin?g.zLogin:"anonymous")" is not allowed to
    @ own or use access tokens.
    style_finish_page();
    return;
  }
  if( g.perm.Admin && P("del")!=0 ){
    const char *zDel = P("del");
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "DELETE FROM config WHERE name='token-%q'",
      zDel);
    db_protect_pop();
  }
  zMyToken = db_text(0,
    "SELECT substr(name,7) FROM config"
    " WHERE name GLOB 'token-*'"
    "   AND json_valid(value,6)"
    "   AND value->>'user' = %Q",
    g.zLogin
  );
  if( zMyToken==0 && P("new") ){
    sqlite3_uint64 r;
    sqlite3_randomness(sizeof(r),&r);
    zMyToken = mprintf("%016llx", r);
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "INSERT INTO config(name,value,mtime)"
      "VALUES('token-%q','{user:%!j}',now())",
      zMyToken, g.zLogin
    );
    db_protect_pop();
  }else if( zMyToken!=0 && P("selfdel")
         && fossil_strcmp(zMyToken,P("selfdel"))==0 ){
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "DELETE FROM config WHERE name='token-%q'",
      zMyToken);
    db_protect_pop();
    zMyToken = 0;
  }
  if( zMyToken==0 ){
    @ <p>You do not currently have an access token.
    @ <a href="%R/tokens?new=true">Create one</a>
  }else{
    @ <p>Your access token is "%h(zMyToken)". 
    @ <p>Use this token as the value of the token= query parameter
    @ to bypass robot defenses on unauthenticated queries to this
    @ server (%R).  Do not misuse your token.  Keep it confidential.
    @ If you misuse your token, or if somebody else steals your token
    @ and misuses, that can result in loss of access privileges to this
    @ server.
    @ <p><a href="%R/tokens?selfdel=%h(zMyToken)">Delete my token</a>
  }
  if( g.perm.Admin ){
    int nTok = 0;
    Stmt s;
    db_prepare(&s, 
      "SELECT substr(name,7), value->>'user', datetime(mtime,'unixepoch')"
      "  FROM config"
      " WHERE name GLOB 'token-*'"
      "   AND json_valid(value,6)"
    );
    while( db_step(&s)==SQLITE_ROW ){
      if( nTok==0 ){
        @ <hr>
        @ <p>All tokens</p>
        @ <table border="1" cellpadding="5" cellspacing="0">
        @ <tr><th>User <th>Token  <th>Date <th> &nbsp;</tr>
      }
      nTok++;
      @ <tr><td>%h(db_column_text(&s,1))
      @ <td>%h(db_column_text(&s,0))
      @ <td>%h(db_column_text(&s,2))
      @ <td><a href="%R/tokens?del=%h(db_column_text(&s,0))">delete</a>
      @ </tr>
    }
    db_finalize(&s);
    if( nTok==0 ){
      @ <hr>
      @ <p>There are access tokens defined for this repository.
    }else{
      @ </table>
    }
  }
  style_finish_page();
}
