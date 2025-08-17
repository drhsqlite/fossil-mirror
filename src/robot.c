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
** Rewrite the current page with a robot squelch captcha and return 1.
**
** Or, if valid proof-of-work is present as either a query parameter or
** as a cookie, then return 0.
*/
static int robot_proofofwork(void){
  sqlite3_int64 tm;
  unsigned h1, h2;
  int k;
  const char *z;
  const char *az[2];

  /* Construct a proof-of-work value based on the IP address of the
  ** sender and the sender's user-agent string.  The current time also
  ** affects the pow value, so actually compute two values, one for the
  ** current 900-second interval and one for the previous.  Either can
  ** match.  The pow-value is an integer between 100,000,000 and
  ** 999,999,999. */
  az[0] = P("REMOTE_ADDR");
  az[1] = P("HTTP_USER_AGENT");
  tm = time(0);
  h1 = (unsigned)((tm&0xffffffff) / 900);
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
  h1 = (h1 % 900000000) + 100000000;
  h2 = (h2 % 900000000) + 100000000;

  /* If there is already a proof-of-work cookie with this value
  ** that means that the user agent has already authenticated.
  */
  z = P(ROBOT_COOKIE);
  if( z
   && (atoi(z)==h1 || atoi(z)==h2)
   && !cgi_is_qp(ROBOT_COOKIE) ){
    return 0;
  }

  /* Check for a proof query parameter.  If found, that means that
  ** the captcha has just now passed, so set the proof-of-work cookie
  ** in addition to letting the request through.
  */
  z = P("proof");
  if( z
   && (atoi(z)==h1 || atoi(z)==h2)
  ){
    cgi_set_cookie(ROBOT_COOKIE,z,"/",900);
    return 0;
  }
  cgi_tag_query_parameter("proof");

  /* Ask the client to present proof-of-work */
  cgi_reset_content();
  cgi_set_content_type("text/html");
  style_header("Browser Verification");
  @ <h1 id="x1">Checking to see if you are a robot<span id="x2"></span></h1>
  @ <form method="GET" id="x6"><p>
  @ <span id="x3" style="visibility:hidden;">\
  @ Press <input type="submit" id="x5" value="Ok" focus> to continue</span>
  @ <span id="x7" style="visibility:hidden;">You appear to be a robot.</span></p>
  cgi_query_parameters_to_hidden();
  @ <input id="x4" type="hidden" name="proof" value="0">
  @ </form>
  @ <script nonce='%s(style_nonce())'>
  @ window.addEventListener('load',function(){
  @ function aaa(x){return document.getElementById(x);}
  @ function bbb(h,a){\
  @ aaa("x4").value=h;\
  @ if((a%%75)==0){\
  @ aaa("x2").textContent=aaa("x2").textContent+".";\
  @ }
  @ if(a>0){\
  @ setTimeout(bbb,1,h+a,a-1);\
  @ }else if(window.getComputedStyle(document.body).zIndex==='0'){\
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
  @ }
  k = 400 + h2%299;
  h2 = (k*k + k)/2;
  @ setTimeout(function(){bbb(%u(h1-h2),%u(k));},10);
  @ }, false);
  @ </script>
  style_finish_page();
  return 1;
}

/*
** SETTING: robot-restrict                width=40 block-text
** The VALUE of this setting is a list of GLOB patterns that match
** pages for which complex HTTP requests from unauthenicated clients
** should be disallowed.  "Unauthenticated" means the user is "nobody".
** The recommended value for this setting is:
**
**     timelineX,diff,annotate,zip,fileage,file
**
** The "diff" tag covers all diffing pages such as /vdiff, /fdiff, and
** /vpatch.  The "annotate" tag also covers /blame and /praise.  "zip"
** also covers /tarball and /sqlar.  If a tag has an "X" character appended,
** then it only applies if query parameters are such that the page is
** particularly difficult to compute.
**
** In all other case, the tag should exactly match the page name.
*/

/*
** Return the default restriction GLOB
*/
const char *robot_restrict_default(void){
  return "timelineX,diff,annotate,zip,fileage,file";
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
int robot_restrict(const char *zPage){
  const char *zGlob;
  const char *zToken;
  static int bKnownPass = 0;
  if( g.zLogin ) return 0;    /* Logged in users always get through */
  if( bKnownPass ) return 0;  /* Already known to pass robot restrictions */
  zGlob = db_get("robot-restrict",robot_restrict_default());
  if( zGlob==0 || zGlob[0]==0 ){ bKnownPass = 1;  return 0; }
  if( !glob_multi_match(zGlob, zPage) ) return 0;
  zToken = P("token");
  if( zToken!=0
   && db_exists("SELECT 1 FROM config"
                " WHERE name='token-%q'"
                "   AND json_valid(value,6)"
                "   AND value->>'user' IS NOT NULL", zToken)
  ){
    bKnownPass = 1;
    return 0;                /* There is a valid token= query parameter */
  }
  if( robot_proofofwork() ){
    /* A captcha was generated.  Abort this page.  A redirect will occur
    ** if the captcha passes. */
    return 1;
  }
  bKnownPass = 1;
  return 0;
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
  z = db_get("robot-restrict",robot_restrict_default());
  if( z && z[0] ){
    @ robot-restrict=%h(z)</br>
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
