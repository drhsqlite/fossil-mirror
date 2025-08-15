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
** Rewrite the current page with a robot squelch captcha.
*/
static void robot_send_captcha(void){
  /* Actually, for now, redirect to /login?anon&g=...
  ** We can work on a more efficient implementation later.
  */
  login_needed(1);
}


/*
** WEBPAGE functions can invoke this routine with an argument
** that is between 0 and 1000.  Based on that argument, and on
** other factors, this routine decides whether or not to squelch
** the request.  "Squelch" in this context, means paint a captcha
** rather than complete the original request.  The idea here is to
** prevent server overload due to excess robot traffic.
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
  if( n+iSquelch>=1000 ){
    robot_send_captcha();
    return 1;
  }
  return 0;
}
