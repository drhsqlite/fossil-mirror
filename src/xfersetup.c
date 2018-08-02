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
** This file contains code to implement the transfer configuration
** setup screens.
*/
#include "config.h"
#include "xfersetup.h"
#include <assert.h>

/*
** WEBPAGE: xfersetup
** Main sub-menu for configuring the transfer system.
*/
void xfersetup_page(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }

  style_header("Transfer Setup");

  @ <table class="xfersetup">
  setup_menu_entry("Common", "xfersetup_com",
    "Common TH1 code run before all transfer request processing.");
  setup_menu_entry("Push", "xfersetup_push",
    "Specific TH1 code to run after \"push\" transfer requests.");
  setup_menu_entry("Commit", "xfersetup_commit",
    "Specific TH1 code to run after processing a commit.");
  setup_menu_entry("Ticket", "xfersetup_ticket",
    "Specific TH1 code to run after processing a ticket change.");
  @ </table>

  url_parse(0, 0);
  if( g.url.protocol ){
    unsigned syncFlags;
    const char *zButton;
    char *zWarning;

    if( db_get_boolean("dont-push", 0) ){
      syncFlags = SYNC_PULL;
      zButton = "Pull";
      zWarning = 0;
    }else{
      syncFlags = SYNC_PUSH | SYNC_PULL;
      zButton = "Synchronize";
      zWarning = mprintf("WARNING: Pushing to \"%s\" is enabled.",
                         g.url.canonical);
    }
    @ <p>Press the <strong>%h(zButton)</strong> button below to
    @ synchronize with the <em>%h(g.url.canonical)</em> repository now.<br />
    @ This may be useful when testing the various transfer scripts.</p>
    @ <p>You can use the <code>http -async</code> command in your scripts, but
    @ make sure the <code>th1-uri-regexp</code> setting is set first.</p>
    if( zWarning ){
      @
      @ <big><b>%h(zWarning)</b></big>
      free(zWarning);
    }
    @
    @ <form method="post" action="%s(g.zTop)/%s(g.zPath)"><div>
    login_insert_csrf_secret();
    @ <input type="submit" name="sync" value="%h(zButton)" />
    @ </div></form>
    @
    if( P("sync") ){
      user_select();
      url_enable_proxy(0);
      @ <pre class="xfersetup">
      client_sync(syncFlags, 0, 0);
      @ </pre>
    }
  }

  style_footer();
}

/*
** Common implementation for the transfer setup editor pages.
*/
static void xfersetup_generic(
  const char *zTitle,           /* Page title */
  const char *zDbField,         /* Configuration field being edited */
  const char *zDfltValue,       /* Default text value */
  const char *zDesc,            /* Description of this field */
  char *(*xText)(const char*),  /* Validity test or NULL */
  void (*xRebuild)(void),       /* Run after successful update */
  int height                    /* Height of the edit box */
){
  const char *z;
  int isSubmit;

  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }
  if( P("setup") ){
    cgi_redirect("xfersetup");
  }
  isSubmit = P("submit")!=0;
  z = P("x");
  if( z==0 ){
    z = db_get(zDbField, zDfltValue);
  }
  style_header("Edit %s", zTitle);
  if( P("clear")!=0 ){
    login_verify_csrf_secret();
    db_unset(zDbField, 0);
    if( xRebuild ) xRebuild();
    z = zDfltValue;
  }else if( isSubmit ){
    char *zErr = 0;
    login_verify_csrf_secret();
    if( xText && (zErr = xText(z))!=0 ){
      @ <p class="xfersetupError">ERROR: %h(zErr)</p>
    }else{
      db_set(zDbField, z, 0);
      if( xRebuild ) xRebuild();
      cgi_redirect("xfersetup");
    }
  }
  @ <form action="%s(g.zTop)/%s(g.zPath)" method="post"><div>
  login_insert_csrf_secret();
  @ <p>%s(zDesc)</p>
  @ <textarea name="x" rows="%d(height)" cols="80">%h(z)</textarea>
  @ <p>
  @ <input type="submit" name="submit" value="Apply Changes" />
  @ <input type="submit" name="clear" value="Revert To Default" />
  @ <input type="submit" name="setup" value="Cancel" />
  @ </p>
  @ </div></form>
  if ( zDfltValue ){
    @ <hr />
    @ <h2>Default %s(zTitle)</h2>
    @ <blockquote><pre>
    @ %h(zDfltValue)
    @ </pre></blockquote>
  }
  style_footer();
}

static const char *zDefaultXferCommon = 0;

/*
** WEBPAGE: xfersetup_com
** View or edit the TH1 script that runs prior to receiving a
** transfer.
*/
void xfersetup_com_page(void){
  static const char zDesc[] =
  @ Enter TH1 script that initializes variables prior to running
  @ any of the transfer request scripts.
  ;
  xfersetup_generic(
    "Transfer Common Script",
    "xfer-common-script",
    zDefaultXferCommon,
    zDesc,
    0,
    0,
    30
  );
}

static const char *zDefaultXferPush = 0;

/*
** WEBPAGE: xfersetup_push
** View or edit the TH1 script that runs after receiving a "push".
*/
void xfersetup_push_page(void){
  static const char zDesc[] =
  @ Enter TH1 script that runs after processing <strong>push</strong>
  @ transfer requests.
  ;
  xfersetup_generic(
    "Transfer Push Script",
    "xfer-push-script",
    zDefaultXferPush,
    zDesc,
    0,
    0,
    30
  );
}

static const char *zDefaultXferCommit = 0;

/*
** WEBPAGE: xfersetup_commit
** View or edit the TH1 script that runs when a transfer commit
** is processed.
*/
void xfersetup_commit_page(void){
  static const char zDesc[] =
  @ Enter TH1 script that runs when a commit is processed.
  ;
  xfersetup_generic(
    "Transfer Commit Script",
    "xfer-commit-script",
    zDefaultXferCommit,
    zDesc,
    0,
    0,
    30
  );
}

static const char *zDefaultXferTicket = 0;

/*
** WEBPAGE: xfersetup_ticket
** View or edit the TH1 script that runs when a ticket change artifact
** is processed during a transfer.
*/
void xfersetup_ticket_page(void){
  static const char zDesc[] =
  @ Enter TH1 script that runs when a ticket change is processed.
  ;
  xfersetup_generic(
    "Transfer Ticket Script",
    "xfer-ticket-script",
    zDefaultXferTicket,
    zDesc,
    0,
    0,
    30
  );
}
