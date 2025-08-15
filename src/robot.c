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
** SETTING: robot-squelch                width=10 default=200
** The VALUE of is an integer between 0 and 1000 that determines how
** readily Fossil will squelch requests from robots.  A value of 0
** means "never squelch requests".  A value of 1000 means "always
** squelch requests from user 'nobody'".  For values greater than 0
** and less than 1000, the decision to squelch is based on a variety
** of heuristics, but is more likely to occur the larger the number.
*/

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
** WEBPAGE functions can invoke this routine with an argument
** that is between 0 and 1000.  Based on that argument, and on
** other factors, this routine decides whether or not to squelch
** the request.  "Squelch" in this context, means to require the
** client to show proof-of-work before the request is processed.
** The idea here is to prevent server overload due to excess robot
** traffic.  If a robot (or any client application really) wants us
** to spend a lot of CPU computing some result for it, then it needs
** to first demonstrate good faith by doing some make-work for us.
**
** This routine returns true for a squelch and false if the original
** request should go through.
**
** The input parameter is an estimate of how much CPU time
** and bandwidth is needed to compute a response.  The higher the
** value of this parameter, the more likely this routine is to squelch
** the page.  A value of zero means "never squelch".  A value of
** 1000 means always squelch if the user is "nobody".
**
** Squelching only happens if the user is "nobody".  If the request
** comes from any other user, including user "anonymous", the request
** is never squelched.
*/
int robot_squelch(int n){
  const char *zToken;
  int iSquelch;
  assert( n>=0 && n<=1000 );
  if( g.zLogin ) return 0;   /* Logged in users always get through */
  if( n==0 ) return 0;       /* Squelch is completely disabled */
  zToken = P("token");
  if( zToken!=0
   && db_exists("SELECT 1 FROM config WHERE name='token-%q'", zToken)
  ){
    return 0;                /* There is a valid token= query parameter */
  }
  iSquelch = db_get_int("robot-squelch",200);
  if( iSquelch<=0 ) return 0;
  if( n+iSquelch>=1000 && robot_proofofwork() ){
    return 1;
  }
  return 0;
}
