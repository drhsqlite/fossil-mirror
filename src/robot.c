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
  z = P("fossil-proofofwork");
  if( z
   && (atoi(z)==h1 || atoi(z)==h2) 
   && !cgi_is_qp("fossil-proofofwork") ){
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
    cgi_set_cookie("fossil-proofofwork",z,"/",900);
    return 0;
  }
  cgi_tag_query_parameter("proof");

  /* Ask the client to present proof-of-work */
  cgi_reset_content();
  cgi_set_content_type("text/html");
  style_header("Captcha");
  @ <h1>Prove That You Are Human</h1>
  @ <form method="GET">
  @ <p>Press the button below</p><p>
  cgi_query_parameters_to_hidden();
  @ <input id="vx" type="hidden" name="proof" value="0">
  @ <input id="cx" type="submit" value="Wait..." disabled>
  @ </form>
  @ <script nonce='%s(style_nonce())'>
  @ function Nhtot1520(x){return document.getElementById(x);}
  @ function Aoxlxzajv(h){\
  @ Nhtot1520("vx").value=h;\
  @ Nhtot1520("cx").value="Ok";\
  @ Nhtot1520("cx").disabled=false;\
  @ }
  @ function Vhcnyarsm(h,a){\
  @ if(a>0){setTimeout(Vhcnyarsm,1,h+a,a-1);}else{Aoxlxzajv(h);}\
  @ }
  k = 200 + h2%99;
  h2 = (k*k + k)/2;
  @ setTimeout(function(){Vhcnyarsm(%u(h1-h2),%u(k));},10);
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
  if( g.zLogin ) return 0;   /* Logged in users always get through */
  zGlob = db_get("robot-restrict",robot_restrict_default());
  if( zGlob==0 || zGlob[0]==0 ) return 0;
  if( !glob_multi_match(zGlob, zPage) ) return 0;
  zToken = P("token");
  if( zToken!=0
   && db_exists("SELECT 1 FROM config WHERE name='token-%q'", zToken)
  ){
    return 0;                /* There is a valid token= query parameter */
  }
  if( robot_proofofwork() ){
    return 1;
  }
  return 0;
}
