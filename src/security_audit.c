/*
** Copyright (c) 2017 D. Richard Hipp
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
** This file implements various web pages use for running a security audit
** of a Fossil configuration.
*/
#include "config.h"
#include <assert.h>
#include "security_audit.h"


/*
** WEBPAGE: secaudit0
**
** Run a security audit of the current Fossil setup.
** This page requires administrator access
*/
void secaudit0_page(void){
  login_check_credentials();
  if( !g.perm.Setup && !g.perm.Admin ){
    login_needed(0);
    return;
  }
  style_header("Security Audit");
  @ Under Construction...
  style_footer();
}
