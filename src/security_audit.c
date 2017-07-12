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
  char *z;
  int n;

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
    @ checked-in content can be accessed by anonymous users.
    @ <a href="takeitprivate">Take it private</a>.<p>
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
      @ <li> URLs that match any of these GLOB patterns:
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

  /* Administrative privilege should only be provided to
  ** specific individuals, not to entire classes of people.
  ** And not too many people should have administrator privilege.
  */
  z = db_text(0, "SELECT group_concat(login,' AND ') FROM user"
                 " WHERE cap GLOB '*[as]*'"
                 "   AND login in ('anonymous','nobody','reader','developer')");
  if( z && z[0] ){
    @ <li><p>
    @ Adminstrative privilege is granted to an entire class of users
    @ (%h(z)).  Ideally, the Write-Unver privilege should only be
    @ granted to specific individuals.
  }
  n = db_int(0,"SELECT count(*) FROM user WHERE cap GLOB '*[as]*'");
  if( n==0 ){
    @ <li><p>
    @ No users have administrator privilege.
  }else{
    z = db_text(0,
      "SELECT group_concat("
                 "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),"
             "', ')"
      " FROM user"
      " WHERE cap GLOB '*[as]*'"
    );
    @ <li><p>
    @ Users with administrator privilege are: %s(z)
    fossil_free(z);
    if( n>3 ){
      @ <p><b>Caution</b>:
      @ Administrator privilege is granted to
      @ <a href='setup_ulist?with=as'>%d(n) users</a>.
      @ Ideally, administator privilege ('s' or 'a') should only
      @ be granted to one or two users.
    }
  }

  /* The push-unversioned privilege should only be provided to
  ** specific individuals, not to entire classes of people.
  ** And no too many people should have this privilege.
  */
  z = db_text(0,
    "SELECT group_concat("
                 "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),"
             "' and ')"
    " FROM user"
    " WHERE cap GLOB '*y*'"
    "   AND login in ('anonymous','nobody','reader','developer')"
  );
  if( z && z[0] ){
    @ <li><p>
    @ The "Write-Unver" privilege is granted to an entire class of users
    @ (%s(z)).  Ideally, the Write-Unver privilege should only be
    @ granted to specific individuals.
    fossil_free(z);
  }
  n = db_int(0,"SELECT count(*) FROM user WHERE cap GLOB '*y*'");
  if( n>0 ){
    z = db_text(0,
       "SELECT group_concat("
          "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),', ')"
       " FROM user WHERE cap GLOB '*y*'"
    );
    @ <li><p>
    @ Users with "Write-Unver" privilege: %s(z)
    fossil_free(z);
    if( n>3 ){
      @ <p><b>Caution:</b>
      @ The "Write-Unver" privilege ('y') is granted to an excessive
      @ number of users (%d(n)).
      @ Ideally, the Write-Unver privilege should only
      @ be granted to one or two users.
    }
  }

  /* Notify if REMOTE_USER or HTTP_AUTHENTICATION is used for login.
  */
  if( db_get_boolean("remote_user_ok", 0) ){
    @ <li><p>
    @ This repository trusts that the REMOTE_USER environment variable set
    @ up by the webserver contains the name of an authenticated user.
    @ Fossil's built-in authentication mechanism is bypassed.
    @ <p>Fix this by deactivating the "Allow REMOTE_USER authentication"
    @ checkbox on the <a href="setup_access">Access Control</a> page.
  }
  if( db_get_boolean("http_authentication_ok", 0) ){
    @ <li><p>
    @ This repository trusts that the HTTP_AUTHENITICATION environment
    @ variable set up by the webserver contains the name of an
    @ authenticated user.
    @ Fossil's built-in authentication mechanism is bypassed.
    @ <p>Fix this by deactivating the "Allow HTTP_AUTHENTICATION authentication"
    @ checkbox on the <a href="setup_access">Access Control</a> page.
  }

  /* Logging should be turned on
  */
  if( db_get_boolean("access-log",0)==0 ){
    @ <li><p>
    @ The <a href="access_log">User Log</a> is disabled.  The user log
    @ keeps a record of successful and unsucessful login attempts and is
    @ useful for security monitoring.
  }
  if( db_get_boolean("admin-log",0)==0 ){
    @ <li><p>
    @ The <a href="admin_log">Administrative Log</a> is disabled.
    @ The administrative log provides a record of configuration changes
    @ and is useful for security monitoring.
  }

#if !defined(_WIN32) && !defined(FOSSIL_OMIT_LOAD_AVERAGE)
  /* Make sure that the load-average limiter is armed and working */
  if( load_average()==0.0 ){
    @ <li><p>
    @ Unable to get the system load average.  This can prevent Fossil
    @ from throttling expensive operations during peak demand.
    @ <p>If running in a chroot jail on Linux, verify that the /proc
    @ filesystem is mounted within the jail, so that the load average
    @ can be obtained from the /proc/loadavg file.
  }else {
    double r = atof(db_get("max-loadavg", "0"));
    if( r<=0.0 ){
      @ <li><p>
      @ Load average limiting is turned off.  This can cause the server
      @ to bog down if many requests for expensive services (such as
      @ large diffs or tarballs) arrive at about the same time.
      @ <p>To fix this, set the "Server Load Average Limit" on the
      @ <a href="setup_access">Access Control</a> page to approximately
      @ the number of available cores on your server, or maybe just a little
      @ less.
    }else if( r>=8.0 ){
      @ <li><p>
      @ The "Server Load Average Limit" on the
      @ <a href="setup_access">Access Control</a> page is set to %g(r),
      @ which seems high.  Is this server really a %d((int)r)-core machine?
    }
  }
#endif


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
