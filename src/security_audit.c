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
** Return TRUE if any of the capability letters in zTest are found
** in the capability string zCap.
*/
static int hasAnyCap(const char *zCap, const char *zTest){
  while( zTest[0] ){
    if( strchr(zCap, zTest[0]) ) return 1;
    zTest++;
  }
  return 0;
}


/*
** WEBPAGE: secaudit0
**
** Run a security audit of the current Fossil setup.
** This page requires administrator access
*/
void secaudit0_page(void){
  const char *zAnonCap;      /* Capabilities of user "anonymous" and "nobody" */
  const char *zPubPages;     /* GLOB pattern for public pages */

  login_check_credentials();
  if( !g.perm.Setup && !g.perm.Admin ){
    login_needed(0);
    return;
  }
  style_header("Security Audit");
  @ <ol>

  /* Step 1:  Determine if the repository is public or private.  "Public"
  ** means that any anonymous user on the internet can access all content.
  ** "Private" repos require (non-anonymous) login to access all content,
  ** though some content may be accessible anonymously.
  */
  zAnonCap = db_text("", "SELECT group_concat(coalesce(cap,'')) FROM user"
                         " WHERE login IN ('anonymous','nobody')");
  zPubPages = db_get("public-pages",0);
  if( hasAnyCap(zAnonCap,"as") ){
    @ <li><p>This repository is <big><b>Wildly INSECURE</b></big> because
    @ it grants administrator privileges to anonymous users.  You
    @ should <a href="takeitprivate">take this repository private</a>
    @ immediately!  Or, at least remove the Setup and Admin privileges
    @ for users "anonymous" and "login" on the
    @ <a href="setup_ulist">User Configuration</a> page.
  }else if( hasAnyCap(zAnonCap,"y") ){
    @ <li><p>This repository is <big><b>INSECURE</b></big> because
    @ it allows anonymous users to push unversioned files.
    @ <p>Fix this by <a href="takeitprivate">taking the repository private</a>
    @ or by removing the "y" permission from users "anonymous" and
    @ "nobody" on the <a href="setup_ulist">User Configuration</a> page.
  }else if( hasAnyCap(zAnonCap,"goz") ){
    @ <li><p>This repository is <big><b>PUBLIC</b></big>. All
    @ checked-in content can be accessed by anonymous passers-by on the
    @ internet.  <a href="takeitprivate">Take it private</a>.<p>
  }else if( !hasAnyCap(zAnonCap, "jry") && (zPubPages==0 || zPubPages[0]==0) ){
    @ <li><p>This repository is <big><b>Completely PRIVATE</b></big>.
    @ A valid login and password is required to access any content.
  }else{
    @ <li><p>This repository is <big><b>Mostly PRIVATE</b></big>.
    @ A valid login and password is usually required, however some
    @ content can be accessed anonymously:
    @ <ul>
    if( hasAnyCap(zAnonCap,"j") ){
      @ <li> Wiki pages
    }
    if( hasAnyCap(zAnonCap,"r") ){
      @ <li> Tickets
    }
    if( zPubPages && zPubPages[0] ){
      Glob *pGlob = glob_create(zPubPages);
      int i;
      @ <li> URLs that matches any of these GLOB patterns:
      @ <ul>
      for(i=0; i<pGlob->nPattern; i++){
        @ <li> %h(pGlob->azPattern[i])
      }
      @ </ul>
    }
    @ </ul>
    if( zPubPages && zPubPages[0] ){
      @ <p>Change GLOB patterns exceptions using the "Public pages" setting
      @ on the <a href="setup_access">Access Settings</a> page.</p>
    }
  }

  /* Make sure the HTTPS is required for login, so that the password
  ** does not go across the internet in the clear.
  */
  if( db_get_boolean("redirect-to-https",0)==0 ){
    @ <li><p><b>WARNING:</b>
    @ Login passwords can be sent over an unencrypted connection.
    @ <p>Fix this by activating the "Redirect to HTTPS on the Login page"
    @ setting on the <a href="setup_access">Access Control</a> page.
  }

  /* Anonymous users should not be able to harvest email addresses 
  ** from tickets.
  */
  if( hasAnyCap(zAnonCap, "e") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can view email addresses and other personally
    @ identifiable information on tickets.
    @ <p>Fix this by removing the "Email" privilege from users
    @ "anonymous" and "nobody" on the 
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* Anonymous users probably should not be allowed to push content
  ** to the repository.
  */
  if( hasAnyCap(zAnonCap, "i") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can push new check-ins into the repository.
    @ <p>Fix this by removing the "Check-in" privilege from users
    @ "anonymous" and "nobody" on the 
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* Anonymous users probably should not be allowed act as moderators
  ** for wiki or tickets.
  */
  if( hasAnyCap(zAnonCap, "lq") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can act as moderators for wiki and/or tickets.
    @ This defeats the whole purpose of moderation.
    @ <p>Fix this by removing the "Mod-Wiki" and "Mod-Tkt"
    @ privilege from users "anonymous" and "nobody" on the 
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* Anonymous users probably should not be allowed to delete
  ** wiki or tickets.
  */
  if( hasAnyCap(zAnonCap, "d") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can delete wiki and tickets.
    @ <p>Fix this by removing the "Delete"
    @ privilege from users "anonymous" and "nobody" on the 
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* If anonymous users are allowed to create new Wiki, then
  ** wiki moderation should be activated to pervent spam.
  */
  if( hasAnyCap(zAnonCap, "fk") ){
    if( db_get_boolean("modreq-wiki",0)==0 ){
      @ <li><p><b>WARNING:</b>
      @ Anonymous users can create or edit wiki without moderation.
      @ This can result in robots inserting lots of wiki spam into
      @ repository.
      @ <p>Fix this by removing the "New-Wiki" and "Write-Wiki"
      @ privileges from users "anonymous" and "nobody" on the 
      @ <a href="setup_ulist">User Configuration</a> page or
      @ by enabling wiki moderation on the
      @ <a href="setup_modreq">Moderation Setup</a> page.
    }else{
      @ <li><p>
      @ Anonymous users can create or edit wiki, but moderator
      @ approval is required before the edits become permanent.
    }
  }

  @ </ol>  
  style_footer();
}

/*
** WEBPAGE: takeitprivate
**
** Disable anonymous access to this website
*/
void takeitprivate_page(void){
  login_check_credentials();
  if( !g.perm.Setup && !g.perm.Admin ){
    login_needed(0);
    return;
  }
  if( P("cancel") ){
    /* User pressed the cancel button.  Go back */
    cgi_redirect("secaudit0");
  }
  if( P("apply") ){
    db_multi_exec(
      "UPDATE user SET cap=''"
      " WHERE login IN ('nobody','anonymous');"
      "DELETE FROM config WHERE name='public-pages';"
    );
    cgi_redirect("secaudit0");
  }
  style_header("Make This Website Private");
  @ <p>Click the "Make It Private" button below to disable all
  @ anonymous access to this repository.  A valid login and password
  @ will be required to access this repository after clicking that
  @ button.</p>
  @
  @ <p>Click the "Cancel" button to leave things as they are.</p>
  @
  @ <form action="%s(g.zPath)" method="post">
  @ <input type="submit" name="apply" value="Make It Private">
  @ <input type="submit" name="cancel" value="Cancel">
  @ </form>

  style_footer();
}
