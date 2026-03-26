/*
** Copyright (c) 2014 D. Richard Hipp
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
** This file contains code to check the host load-average and abort
** CPU-intensive operations if the load-average is too high.
*/
#include "config.h"
#include "loadctrl.h"
#include <assert.h>

/*
** Return the load average for the host processor
*/
double load_average(void){
#if !defined(_WIN32) && !defined(FOSSIL_OMIT_LOAD_AVERAGE)
  double a[3];
  if( getloadavg(a, 3)>0 ){
    return a[0]>=0.000001 ? a[0] : 0.000001;
  }
#endif
  return 0.0;
}

/*
** COMMAND: test-loadavg
**
** %fossil test-loadavg
**
** Print the load average on the host machine.
*/
void loadavg_test_cmd(void){
  fossil_print("load-average: %f\n", load_average());
}

/*
** WEBPAGE:  test-overload
**
** Generate the response that would normally be shown only when
** service is denied due to an overload condition.  This is for
** testing of the overload warning page.
*/
void overload_page(void){
  double mxLoad = atof(db_get("max-loadavg", "0.0"));
  style_set_current_feature("test");
  style_header("Server Overload");
  @ <h2>The server load is currently too high.
  @ Please try again later.</h2>
  @ <p>Current load average: %f(load_average())<br>
  @ Load average limit: %f(mxLoad)<br>
  @ URL: %h(g.zBaseURL)%h(P("PATH_INFO"))<br>
  @ Timestamp: %h(db_text("","SELECT datetime()"))Z</p>
  style_finish_page();
}

/*
** Abort the current page request if the load average of the host
** computer is too high. Admin and Setup users are exempt from this
** restriction.
*/
void load_control(void){
  double mxLoad = atof(db_get("max-loadavg", "0.0"));
#if 1
  /* Disable this block only to test load restrictions */
  if( mxLoad<=0.0 || mxLoad>=load_average() ) return;

  login_check_credentials();
  if(g.perm.Admin || g.perm.Setup){
    return;
  }
#endif
  overload_page();
  cgi_set_status(503,"Server Overload");
  cgi_reply();
  exit(0);
}
