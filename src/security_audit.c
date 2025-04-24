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
** Parse the content-security-policy
** into separate fields, and return a pointer to a null-terminated
** array of pointers to strings, one entry for each field.  Or return
** a NULL pointer if no CSP could be located in the header.
**
** Memory to hold the returned array and of the strings is obtained from
** a single memory allocation, which the caller should free to avoid a
** memory leak.
*/
static char **parse_content_security_policy(void){
  char **azCSP = 0;
  int nCSP = 0;
  char *zAll;
  char *zCopy;
  int nAll = 0;
  int jj;
  int nSemi;

  zAll = style_csp(0);
  nAll = (int)strlen(zAll);
  for(jj=nSemi=0; jj<nAll; jj++){ if( zAll[jj]==';' ) nSemi++; }
  azCSP = fossil_malloc( nAll+1+(nSemi+2)*sizeof(char*) );
  zCopy = (char*)&azCSP[nSemi+2];
  memcpy(zCopy,zAll,nAll);
  zCopy[nAll] = 0;
  while( fossil_isspace(zCopy[0]) || zCopy[0]==';' ){ zCopy++; }
  azCSP[0] = zCopy;
  nCSP = 1;
  for(jj=0; zCopy[jj]; jj++){
    if( zCopy[jj]==';' ){
      int k;
      for(k=jj-1; k>0 && fossil_isspace(zCopy[k]); k--){ zCopy[k] = 0; }
      zCopy[jj] = 0;
      while( jj+1<nAll
         && (fossil_isspace(zCopy[jj+1]) || zCopy[jj+1]==';')
      ){
        jj++;
      }
      assert( nCSP<nSemi+1 );
      azCSP[nCSP++] = zCopy+jj;
    }
  }
  assert( nCSP<=nSemi+2 );
  azCSP[nCSP] = 0;
  fossil_free(zAll);
  return azCSP;
}

/*
** WEBPAGE: secaudit0
**
** Run a security audit of the current Fossil setup, looking
** for configuration problems that might allow unauthorized
** access to the repository.
**
** This page requires administrator access.  It is usually
** accessed using the Admin/Security-Audit menu option
** from any of the default skins.
*/
void secaudit0_page(void){
  const char *zAnonCap;      /* Capabilities of user "anonymous" and "nobody" */
  const char *zDevCap;       /* Capabilities of user group "developer" */
  const char *zReadCap;      /* Capabilities of user group "reader" */
  const char *zPubPages;     /* GLOB pattern for public pages */
  const char *zSelfCap;      /* Capabilities of self-registered users */
  int hasSelfReg = 0;        /* True if able to self-register */
  const char *zPublicUrl;    /* Canonical access URL */
  const char *zVulnReport;   /* The vuln-report setting */
  Blob cmd;
  char *z;
  int n, i;
  CapabilityString *pCap;
  char **azCSP;              /* Parsed content security policy */

  login_check_credentials();
  if( !g.perm.Admin ){
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
  zAnonCap = db_text("", "SELECT fullcap(NULL)");
  zDevCap  = db_text("", "SELECT fullcap('v')");
  zReadCap = db_text("", "SELECT fullcap('u')");
  zPubPages = db_get("public-pages",0);
  hasSelfReg = db_get_boolean("self-register",0);
  pCap = capability_add(0, db_get("default-perms","u"));
  capability_expand(pCap);
  zSelfCap = capability_string(pCap);
  capability_free(pCap);
  if( hasAnyCap(zAnonCap,"as") ){
    @ <li><p>This repository is <big><b>Wildly INSECURE</b></big> because
    @ it grants administrator privileges to anonymous users.  You
    @ should <a href="takeitprivate">take this repository private</a>
    @ immediately!  Or, at least remove the Setup and Admin privileges
    @ for users "anonymous" and "login" on the
    @ <a href="setup_ulist">User Configuration</a> page.
  }else if( hasAnyCap(zSelfCap,"as") && hasSelfReg ){
    @ <li><p>This repository is <big><b>Wildly INSECURE</b></big> because
    @ it grants administrator privileges to self-registered users.  You
    @ should <a href="takeitprivate">take this repository private</a>
    @ and/or disable self-registration
    @ immediately!  Or, at least remove the Setup and Admin privileges
    @ from the default permissions for new users.
  }else if( hasAnyCap(zAnonCap,"y") ){
    @ <li><p>This repository is <big><b>INSECURE</b></big> because
    @ it allows anonymous users to push unversioned files.
    @ Fix this by <a href="takeitprivate">taking the repository private</a>
    @ or by removing the "y" permission from users "anonymous" and
    @ "nobody" on the <a href="setup_ulist">User Configuration</a> page.
  }else if( hasAnyCap(zSelfCap,"y") ){
    @ <li><p>This repository is <big><b>INSECURE</b></big> because
    @ it allows self-registered users to push unversioned files.
    @ Fix this by <a href="takeitprivate">taking the repository private</a>
    @ or by removing the "y" permission from the default permissions or
    @ by disabling self-registration.
  }else if( hasAnyCap(zAnonCap,"goz") ){
    @ <li><p>This repository is <big><b>PUBLIC</b></big>. All
    @ checked-in content can be accessed by anonymous users.
    @ <a href="takeitprivate">Take it private</a>.<p>
  }else if( hasAnyCap(zSelfCap,"goz") && hasSelfReg ){
    @ <li><p>This repository is <big><b>PUBLIC</b></big> because all
    @ checked-in content can be accessed by self-registered users.
    @ This repostory would be private if you disabled self-registration.</p>
  }else if( !hasAnyCap(zAnonCap, "jrwy234567")
         && (!hasSelfReg || !hasAnyCap(zSelfCap, "jrwy234567"))
         && (zPubPages==0 || zPubPages[0]==0) ){
    @ <li><p>This repository is <big><b>Completely PRIVATE</b></big>.
    @ A valid login and password is required to access any content.
  }else{
    @ <li><p>This repository is <big><b>Mostly PRIVATE</b></big>.
    @ A valid login and password is usually required, however some
    @ content can be accessed either anonymously or by self-registered
    @ users:
    @ <ul>
    if( hasSelfReg ){
      if( hasAnyCap(zAnonCap,"j") || hasAnyCap(zSelfCap,"j") ){
        @ <li> Wiki pages
      }
      if( hasAnyCap(zAnonCap,"r") || hasAnyCap(zSelfCap,"r") ){
        @ <li> Tickets
      }
      if( hasAnyCap(zAnonCap,"234567") || hasAnyCap(zSelfCap,"234567") ){
        @ <li> Forum posts
      }
    }
    if( zPubPages && zPubPages[0] ){
      Glob *pGlob = glob_create(zPubPages);
      int i;
      @ <li> "Public Pages" are URLs that match any of these GLOB patterns:
      @ <p><ul>
      for(i=0; i<pGlob->nPattern; i++){
        @ <li> %h(pGlob->azPattern[i])
      }
      @ </ul>
      @ <p>Anoymous users are vested with capabilities "%h(zSelfCap)" on
      @ public pages. See the "Public Pages" entry in the
      @ "User capability summary" below.
    }
    @ </ul>
    if( zPubPages && zPubPages[0] ){
      @ <p>Change GLOB patterns exceptions using the "Public pages" setting
      @ on the <a href="setup_access">Access Settings</a> page.</p>
    }
  }

  zPublicUrl = public_url();
  if( zPublicUrl!=0 ){
    int nOther = db_int(0, "SELECT count(*) FROM config"
                           " WHERE name GLOB 'baseurl:*'"
                           " AND name<>'baseurl:%q'", zPublicUrl);
    @ <li><p>The <a href="setup_config#eurl">canonical URL</a> for this
    @ repository is <a href="%s(zPublicUrl)">%h(zPublicUrl)</a>.
    if( nOther==1 ){
      @ This is also <a href="urllist?urlonly">1 other URL</a> that has
      @ been used to access this repository.
    }else if( nOther>=2 ){
      @ There are also
      @ <a href="urllist?all&urlonly">%d(nOther) other URLs</a> that have
      @ been used to access this repository.
    }
  }else{
    int nUrl = db_int(0, "SELECT count(*) FROM config"
                         " WHERE name GLOB 'baseurl:*'");
    @ <li><p>This repository does not have a
    @ <a href="setup_config#eurl">canonical access URL</a>.
    if( nUrl==1 ){
      @ There is
      @ <a href="urllist?urlonly">1 non-canonical URL</a>
      @ that has been used to access this repository.
    }else if( nUrl>=2 ){
      @ There are
      @ <a href="urllist?all&urlonly">%d(nUrl) non-canonical URLs</a>
      @ that have been used to access this repository.
    }
  }

  /* Make sure the HTTPS is required for login, at least, so that the
  ** password does not go across the Internet in the clear.
  */
  if( db_get_int("redirect-to-https",0)==0 ){
    @ <li><p><b>WARNING:</b>
    @ Sensitive material such as login passwords can be sent over an
    @ unencrypted connection.
    @ Fix this by changing the "Redirect to HTTPS" setting on the
    @ <a href="setup_access">Access Control</a> page. If you were using
    @ the old "Redirect to HTTPS on Login Page" setting, switch to the
    @ new setting: it has a more secure implementation.
  }

#ifdef FOSSIL_ENABLE_TH1_DOCS
  /* The use of embedded TH1 is dangerous.  Warn if it is possible.
  */
  if( !Th_AreDocsEnabled() ){
    @ <li><p>
    @ This server is compiled with -DFOSSIL_ENABLE_TH1_DOCS. TH1 docs
    @ are disabled for this particular repository, so you are safe for
    @ now.  However, to prevent future problems caused by accidentally
    @ enabling TH1 docs in the future, it is recommended that you
    @ recompile Fossil without the -DFOSSIL_ENABLE_TH1_DOCS flag.</p>
  }else{
    @ <li><p><b>DANGER:</b>
    @ This server is compiled with -DFOSSIL_ENABLE_TH1_DOCS and TH1 docs
    @ are enabled for this repository.  Anyone who can check-in or push
    @ to this repository can create a malicious TH1 script and then cause
    @ that script to be run on the server. This is a serious security concern.
    @ TH1 docs should only be enabled for repositories with a very limited
    @ number of trusted committers, and the repository should be monitored
    @ closely to ensure no hostile content sneaks in.  If a bad TH1 script
    @ does make it into the repository, the only want to prevent it from
    @ being run is to shun it.</p>
    @
    @ <p>Disable TH1 docs by recompiling Fossil without the
    @ -DFOSSIL_ENABLE_TH1_DOCS flag, and/or clear the th1-docs setting
    @ and ensure that the TH1_ENABLE_DOCS environment variable does not
    @ exist in the environment.</p>
  }
#endif

#if FOSSIL_ENABLE_TCL
  @ <li><p>
  if( db_get_boolean("tcl",0) ){
    #ifdef FOSSIL_ENABLE_TH1_DOCS
      if( Th_AreDocsEnabled() ){
        @ <b>DANGER:</b>
      }else{
        @ <b>WARNING:</b>
      }
    #else
      @ <b>WARNING:</b>
    #endif
    @ This server is compiled with -DFOSSIL_ENABLE_TCL and Tcl integration
    @ is enabled for this repository.  Anyone who can execute malicious
    @ TH1 script on that server can also execute arbitrary Tcl script
    @ under the identity of the operating system process of that server.
    @ This is a serious security concern.</p>
    @
    @ <p>Disable Tcl integration by recompiling Fossil without the
    @ -DFOSSIL_ENABLE_TCL flag, and/or clear the 'tcl' setting.</p>
  }else{
    @ This server is compiled with -DFOSSIL_ENABLE_TCL. Tcl integration
    @ is disabled for this particular repository, so you are safe for
    @ now.  However, to prevent potential problems caused by accidentally
    @ enabling Tcl integration in the future, it is recommended that you
    @ recompile Fossil without the -DFOSSIL_ENABLE_TCL flag.</p>
  }
#endif

  /* Anonymous users should not be able to harvest email addresses
  ** from tickets.
  */
  if( hasAnyCap(zAnonCap, "e") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can view email addresses and other personally
    @ identifiable information on tickets.
    @ Fix this by removing the "Email" privilege
    @ (<a href="setup_ucap_list">capability "e"</a>) from users
    @ "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* Anonymous users probably should not be allowed to push content
  ** to the repository.
  */
  if( hasAnyCap(zAnonCap, "i") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can push new check-ins into the repository.
    @ Fix this by removing the "Check-in" privilege
    @ (<a href="setup_ucap_list">capability</a> "i") from users
    @ "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page.
  }

  /* Anonymous users probably should not be allowed act as moderators
  ** for wiki or tickets.
  */
  if( hasAnyCap(zAnonCap, "lq5") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can act as moderators for wiki, tickets, or
    @ forum posts. This defeats the whole purpose of moderation.
    @ Fix this by removing the "Mod-Wiki", "Mod-Tkt", and "Mod-Forum"
    @ privileges (<a href="%R/setup_ucap_list">capabilities</a> "fq5")
    @ from users "anonymous" and "nobody"
    @ on the <a href="setup_ulist">User Configuration</a> page.
  }

  /* Check to see if any TH1 scripts are configured to run on a sync
  */
  if( db_exists("SELECT 1 FROM config WHERE name GLOB 'xfer-*-script'"
                " AND length(value)>0") ){
    @ <li><p><b>WARNING:</b>
    @ TH1 scripts might be configured to run on any sync, push, pull, or
    @ clone operation.  See the the <a href="%R/xfersetup">/xfersetup</a>
    @ page for more information.  These TH1 scripts are a potential
    @ security concern and so should be carefully audited by a human.
  }

  /* The strict-manifest-syntax setting should be on. */
  if( db_get_boolean("strict-manifest-syntax",1)==0 ){
    @ <li><p><b>WARNING:</b>
    @ The "strict-manifest-syntax"  flag is off.  This is a security
    @ risk.  Turn this setting on (its default) to protect the users
    @ of this repository.
  }

  zVulnReport = db_get("vuln-report","log");
  if( fossil_strcmp(zVulnReport,"block")!=0
   && fossil_strcmp(zVulnReport,"fatal")!=0
  ){
    @ <li><p><b>WARNING:</b>
    @ The <a href="%R/help?cmd=vuln-report">vuln-report setting</a>
    @ has a value of "%h(zVulnReport)". This disables defenses against
    @ XSS or SQL-injection vulnerabilities caused by coding errors in
    @ custom TH1 scripts.  For the best security, change
    @ the value of the vuln-report setting to "block" or "fatal".
  }

  /* Obsolete:  */
  if( hasAnyCap(zAnonCap, "d") ||
      hasAnyCap(zDevCap,  "d") ||
      hasAnyCap(zReadCap, "d") ){
    @ <li><p><b>WARNING:</b>
    @ One or more users has the <a
    @ href="https://fossil-scm.org/forum/forumpost/43c78f4bef">obsolete</a>
    @ "d" capability. You should remove it using the
    @ <a href="setup_ulist">User Configuration</a> page in case we
    @ ever reuse the letter for another purpose.
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
      @ Fix this by removing the "New-Wiki" and "Write-Wiki"
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

  /* Anonymous users should not be able to create trusted forum
  ** posts.
  */
  if( hasAnyCap(zAnonCap, "456") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can create forum posts that are
    @ accepted into the permanent record without moderation.
    @ This can result in robots generating spam on forum posts.
    @ Fix this by removing the "WriteTrusted-Forum" privilege
    @ (<a href="setup_ucap_list">capabilities</a> "456") from
    @ users "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page or
  }

  /* Anonymous users should not be able to send announcements.
  */
  if( hasAnyCap(zAnonCap, "A") ){
    @ <li><p><b>WARNING:</b>
    @ Anonymous users can send announcements to anybody who is signed
    @ up to receive announcements.  This can result in spam.
    @ Fix this by removing the "Announce" privilege
    @ (<a href="setup_ucap_list">capability</a> "A") from
    @ users "anonymous" and "nobody" on the
    @ <a href="setup_ulist">User Configuration</a> page or
  }

  /* Administrative privilege should only be provided to
  ** specific individuals, not to entire classes of people.
  ** And not too many people should have administrator privilege.
  */
  z = db_text(0,
    "SELECT group_concat("
                 "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),"
             "' and ')"
    " FROM user"
    " WHERE cap GLOB '*[as]*'"
    "   AND login in ('anonymous','nobody','reader','developer')"
  );
  if( z && z[0] ){
    @ <li><p><b>WARNING:</b>
    @ Administrative privilege ('a' or 's')
    @ is granted to an entire class of users: %s(z).
    @ Administrative privilege should only be
    @ granted to specific individuals.
  }
  n = db_int(0,"SELECT count(*) FROM user WHERE fullcap(cap) GLOB '*[as]*'");
  if( n==0 ){
    @ <li><p>
    @ No users have administrator privilege.
  }else{
    z = db_text(0,
      "SELECT group_concat("
                 "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),"
             "', ')"
      " FROM user"
      " WHERE fullcap(cap) GLOB '*[as]*'"
    );
    @ <li><p>
    @ Users with administrator privilege are: %s(z)
    fossil_free(z);
    if( n>3 ){
      @ <li><p><b>WARNING:</b>
      @ Administrator privilege is granted to
      @ <a href='setup_ulist?with=as'>%d(n) users</a>.
      @ Ideally, administrator privilege ('s' or 'a') should only
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
    @ <li><p><b>WARNING:</b>
    @ The "Write-Unver" privilege is granted to an entire class of users: %s(z).
    @ The Write-Unver privilege should only be granted to specific individuals.
    fossil_free(z);
  }
  n = db_int(0,"SELECT count(*) FROM user WHERE cap GLOB '*y*'");
  if( n>0 ){
    z = db_text(0,
       "SELECT group_concat("
          "printf('<a href=''setup_uedit?id=%%d''>%%s</a>',uid,login),', ')"
       " FROM user WHERE fullcap(cap) GLOB '*y*'"
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

  /* Providing hyperlink capability to user "nobody" can lead to robots
  ** making excessive requests resulting in DoS
  */
  if( db_exists("SELECT 1 FROM user WHERE login='nobody' AND cap GLOB '*h*'") ){
    int nobodyId = db_int(0,"SELECT uid FROM user WHERE login='nobody'");
    int anonId = db_int(0,
      "SELECT uid FROM user WHERE login='anonymous' AND cap NOT GLOB '*h*'");
    @ <li><p>
    @ User "nobody" has "Hyperlink" privilege ('h') which can lead to
    @ robots walking a nearly endless progression of pages on public-facing
    @ repositories, causing excessive server load and possible DoS.
    @ Suggested remediation:
    @ <ol type="a">
    @ <li>Remove the 'h' privilege from the
    @     <a href="%R/setup_uedit?id=%d(nobodyId)">'nobody' user</a> so that
    @     robots cannot see hyperlinks.
    @ <li>Activate <a href="%R/setup_robot">autohyperlink</a> so that
    @     human readers can still see hyperlinks even if they are not logged in.
    @     Set the delay to at least 50 milliseconds and require a mouse
    @     event for maximum robot defense.
    if( anonId>0 ){
      @ <li>Perhaps set the 'h' privilege on the
      @     <a href="%R/setup_uedit?id=%d(anonId)">'anonymous' user</a> so
      @     that humans that have javascript disabled in their browsers can
      @     still see hyperlinks if they will log in as "anonymous".
    }
    @ </ol>
  }

  /* Notify if REMOTE_USER or HTTP_AUTHENTICATION is used for login.
  */
  if( db_get_boolean("remote_user_ok", 0) ){
    @ <li><p><b>Caution:</b>
    @ This repository trusts that the REMOTE_USER environment variable set
    @ up by the webserver contains the name of an authenticated user.
    @ Fossil's built-in authentication mechanism is bypassed.
    @ Fix this by deactivating the "Allow REMOTE_USER authentication"
    @ checkbox on the <a href="setup_access">Access Control</a> page.
  }
  if( db_get_boolean("http_authentication_ok", 0) ){
    @ <li><p><b>Caution:</b>
    @ This repository trusts that the HTTP_AUTHENTICATION environment
    @ variable set up by the webserver contains the name of an
    @ authenticated user.
    @ Fossil's built-in authentication mechanism is bypassed.
    @ Fix this by deactivating the "Allow HTTP_AUTHENTICATION authentication"
    @ checkbox on the <a href="setup_access">Access Control</a> page.
  }

  /* Logging should be turned on
  */
  if( db_get_boolean("access-log",1)==0 ){
    @ <li><p>
    @ The <a href="access_log">User Log</a> is disabled.  The user log
    @ keeps a record of successful and unsuccessful login attempts and is
    @ useful for security monitoring.
  }
  if( db_get_boolean("admin-log",1)==0 ){
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
    @ If running in a chroot jail on Linux, verify that the /proc
    @ filesystem is mounted within the jail, so that the load average
    @ can be obtained from the /proc/loadavg file.
  }else {
    double r = atof(db_get("max-loadavg", "0.0"));
    if( r<=0.0 ){
      @ <li><p>
      @ Load average limiting is turned off.  This can cause the server
      @ to bog down if many requests for expensive services (such as
      @ large diffs or tarballs) arrive at about the same time.
      @ To fix this, set the
      @ <a href='%R/setup_access#slal'>"Server Load Average Limit"</a> on the
      @ <a href='%R/setup_access'>Access Control</a> page to the approximate
      @ the number of available cores on your server, or maybe just a little
      @ less.
    }else if( r>=8.0 ){
      @ <li><p>
      @ The <a href='%R/setup_access#slal'>"Server Load Average Limit"</a> on
      @ the <a href="setup_access">Access Control</a> page is set to %g(r),
      @ which seems high.  Is this server really a %d((int)r)-core machine?
    }
  }
#endif

  if( g.zErrlog==0 || fossil_strcmp(g.zErrlog,"-")==0 ){
    @ <li><p>
    @ The server error log is disabled.
    @ To set up an error log,
    if( fossil_strcmp(g.zCmdName, "cgi")==0 ){
      @ make an entry like "errorlog: <i>FILENAME</i>" in the
      @ CGI script at %h(P("SCRIPT_FILENAME")).
    }else{
      @ add the "--errorlog <i>FILENAME</i>" option to the
      @ "%h(g.argv[0]) %h(g.zCmdName)" command that launched this server.
    }
  }else{
    FILE *pTest = fossil_fopen(g.zErrlog,"a");
    if( pTest==0 ){
      @ <li><p>
      @ <b>Error:</b>
      @ There is an error log at "%h(g.zErrlog)" but that file is not
      @ writable and so no logging will occur.
    }else{
      fclose(pTest);
      @ <li><p>
      @ The error log at "<a href='%R/errorlog'>%h(g.zErrlog)</a>" is
      @ %,lld(file_size(g.zErrlog, ExtFILE)) bytes in size.
    }
  }

  if( g.zExtRoot ){
    int nFile;
    int nCgi;
    ext_files();
    nFile = db_int(0, "SELECT count(*) FROM sfile");
    nCgi = nFile==0 ? 0 : db_int(0,"SELECT count(*) FROM sfile WHERE isexe");
    @ <li><p> CGI Extensions are enabled with a document root
    @ at <a href='%R/extfilelist'>%h(g.zExtRoot)</a> holding
    @ %d(nCgi) CGIs and %d(nFile-nCgi) static content and data files.
  }

  if( fileedit_glob()!=0 ){
    @ <li><p><a href='%R/fileedit'>Online File Editing</a> is enabled
    @ for this repository.  Clear the
    @ <a href='%R/setup_settings'>"fileedit-glob" setting</a> to
    @ disable online editing.</p>
  }

  @ <li><p> User capability summary:
  capability_summary();


  azCSP = parse_content_security_policy();
  if( azCSP==0 ){
    @ <li><p> WARNING: No Content Security Policy (CSP) is specified in the
    @ header. Though not required, a strong CSP is recommended. Fossil will
    @ automatically insert an appropriate CSP if you let it generate the
    @ HTML <tt>&lt;head&gt;</tt> element by omitting <tt>&lt;body&gt;</tt>
    @ from the header configuration in your customized skin.
    @
  }else{
    int ii;
    @ <li><p> Content Security Policy:
    @ <ol type="a">
    for(ii=0; azCSP[ii]; ii++){
      @ <li>%h(azCSP[ii])
    }
    @ </ol>
  }
  fossil_free(azCSP);

  if( alert_enabled() ){
    @ <li><p> Email alert configuration summary:
    @ <table class="label-value">
    stats_for_email();
    @ </table>
  }else{
    @ <li><p> Email alerts are disabled
  }

  n = db_int(0,"SELECT count(*) FROM ("
               "SELECT rid FROM phantom EXCEPT SELECT rid FROM private)");
  if( n>0 ){
    @ <li><p>\
    @ There exists public phantom artifacts in this repository, shown below.
    @ Phantom artifacts are artifacts whose hash name is referenced by some
    @ other artifact but whose content is unknown.  Some phantoms are marked
    @ private and those are ignored.  But public phantoms cause unnecessary
    @ sync traffic and might represent malicious attempts to corrupt the
    @ repository structure.
    @ </p><p>
    @ To suppress unnecessary sync traffic caused by phantoms, add the RID
    @ of each phantom to the "private" table.  Example:
    @ <blockquote><pre>
    @    INSERT INTO private SELECT rid FROM blob WHERE content IS NULL;
    @ </pre></blockquote>
    @ </p>
    table_of_public_phantoms();
    @ </li>
  }

  @ <li><p>Robot Defenses:
  @ <ol type="a">
  switch( db_get_int("auto-hyperlink",1) ){
    default:
       @ <li> No auto-enable of hyperlinks.
       break;
    case 1:
       @ <li> Hyperlinks auto-enabled based on UserAgent and Javascript.
       break;
    case 2:
       @ <li> Hyperlinks auto-enabled based on UserAgent only.
       break;
  }
  z = db_get("max-loadavg",0);
  if( z && atof(z)>0.0 ){
    @ <li> Maximum load average for expensive requests: %h(z);
  }else{
    @ <li> No limits on the load average
  }
  z = db_get("robot-restrict",0);
  if( z==0 ){
    @ <li> No complex-request constraints on robots
  }else{
    @ <li> Complex requests limited for pages matching: %h(z)
  }
  @ </ol>

  blob_init(&cmd, 0, 0);
  for(i=0; g.argvOrig[i]!=0; i++){
    blob_append_escaped_arg(&cmd, g.argvOrig[i], 0);
  }
  @ <li><p>
  if( g.zCgiFile ){
    Blob fullname;
    blob_init(&fullname, 0, 0);
    file_canonical_name(g.zCgiFile, &fullname, 0);
    @ The CGI control file for this page is "%h(blob_str(&fullname))".
  }
  @ The command that generated this page:
  @ <blockquote>
  @ <tt>%h(blob_str(&cmd))</tt>
  @ </blockquote></li>
  blob_zero(&cmd);

  @ </ol>
  style_finish_page();
}

/*
** WEBPAGE: takeitprivate
**
** Disable anonymous access to this website
*/
void takeitprivate_page(void){
  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  if( P("cancel") ){
    /* User pressed the cancel button.  Go back */
    cgi_redirect("secaudit0");
  }
  if( P("apply") ){
    db_unprotect(PROTECT_ALL);
    db_multi_exec(
      "UPDATE user SET cap=''"
      " WHERE login IN ('nobody','anonymous');"
      "DELETE FROM config WHERE name='public-pages';"
    );
    db_set("self-register","0",0);
    db_protect_pop();
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

  style_finish_page();
}

/*
** Output a message explaining that no error log is available.
*/
static void no_error_log_available(void){
  @ <p>No error log is configured.
  if( g.zCgiFile==0 ){
    @ To create an error log, add the "--errorlog FILENAME"
    @ command-line option to the command that launches the Fossil server.
  }else{
    Blob fullname;
    blob_init(&fullname, 0, 0);
    file_canonical_name(g.zCgiFile, &fullname, 0);
    @ To create an error log, edit the CGI control file
    @ named "%h(blob_str(&fullname))" to add a line like this:
    @ <blockquote><pre>
    @ errorlog: <i>FILENAME</i>
    @ </pre></blockquote>
    blob_reset(&fullname);
  }
}

/*
** WEBPAGE: errorlog
**
** Show the content of the error log.  Only the administrator can view
** this page.
**
**    y=0x001          Show only hack attempts
**    y=0x002          Show only panics and assertion faults
**    y=0x004          Show hung backoffice processes
**    y=0x008          Show POST requests from a different origin
**    y=0x010          Show SQLITE_AUTH and similar
**    y=0x020          Show SMTP error reports
**    y=0x040          Show TH1 vulnerability reports
**    y=0x800          Show other uncategorized messages
**
** If y is omitted or is zero, a count of the various message types is
** shown.
*/
void errorlog_page(void){
  i64 szFile;
  FILE *in;
  char *zLog;
  const char *zType = P("y");
  static const int eAllTypes = 0x87f;
  long eType = 0;
  int bOutput = 0;
  int prevWasTime = 0;
  int nHack = 0;
  int nPanic = 0;
  int nOther = 0;
  int nHang = 0;
  int nXPost = 0;
  int nAuth = 0;
  int nSmtp = 0;
  int nVuln = 0;
  char z[10000];
  char zTime[10000];

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  if( zType ){
    eType = strtol(zType,0,0) & eAllTypes;
  }
  style_header("Server Error Log");
  style_submenu_element("Test", "%R/test-warning");
  style_submenu_element("Refresh", "%R/errorlog");
  style_submenu_element("Download", "%R/errorlog?download");
  style_submenu_element("Truncate", "%R/errorlog?truncate");
  style_submenu_element("Log-Menu", "%R/setup-logmenu");
  if( eType ){
    style_submenu_element("Summary", "%R/errorlog");
  }

  if( g.zErrlog==0 || fossil_strcmp(g.zErrlog,"-")==0 ){
    no_error_log_available();
    style_finish_page();
    return;
  }
  if( P("truncate1") && cgi_csrf_safe(2) ){
    fclose(fopen(g.zErrlog,"w"));
  }
  if( P("download") ){
    Blob log;
    blob_read_from_file(&log, g.zErrlog, ExtFILE);
    cgi_set_content_type("text/plain");
    cgi_set_content(&log);
    return;
  }
  szFile = file_size(g.zErrlog, ExtFILE);
  if( P("truncate") ){
    @ <form action="%R/errorlog" method="POST">
    login_insert_csrf_secret();
    @ <p>Confirm that you want to truncate the %,lld(szFile)-byte error log:
    @ <input type="submit" name="truncate1" value="Confirm">
    @ <input type="submit" name="cancel" value="Cancel">
    @ </form>
    style_finish_page();
    return;
  }
  zLog = file_canonical_name_dup(g.zErrlog);
  @ <p>The server error log at "%h(zLog)" is %,lld(szFile) bytes in size.
  fossil_free(zLog);
  in = fossil_fopen(g.zErrlog, "rb");
  if( in==0 ){
    @ <p class='generalError'>Unable to open that file for reading!</p>
    style_finish_page();
    return;
  }
  if( eType==0 ){
    /* will do a summary */
  }else if( (eType&eAllTypes)!=eAllTypes ){
    @ Only the following types of messages displayed:
    @ <ul>
    if( eType & 0x01 ){
      @ <li>Hack attempts
    }
    if( eType & 0x02 ){
      @ <li>Panics and assertion faults
    }
    if( eType & 0x04 ){
      @ <li>Hung backoffice processes
    }
    if( eType & 0x08 ){
      @ <li>POST requests from different origin
    }
    if( eType & 0x10 ){
      @ <li>SQLITE_AUTH and similar errors
    }
    if( eType & 0x20 ){
      @ <li>SMTP malfunctions
    }
    if( eType & 0x40 ){
      @ <li>TH1 vulnerabilities
    }
    if( eType & 0x800 ){
      @ <li>Other uncategorized messages
    }
    @ </ul>
  }
  @ <hr>
  if( eType ){
    @ <pre>
  }
  while( fgets(z, sizeof(z), in) ){
    if( prevWasTime ){
      if( strncmp(z,"possible hack attempt - 418 ", 27)==0 ){
        bOutput = (eType & 0x01)!=0;
        nHack++;
      }else
      if( (strncmp(z,"panic: ", 7)==0 || strstr(z," assertion fault ")!=0) ){
        bOutput = (eType & 0x02)!=0;
        nPanic++;
      }else
      if( strncmp(z,"SMTP:", 5)==0 ){
        bOutput = (eType & 0x20)!=0;
        nSmtp++;
      }else
      if( sqlite3_strglob("warning: backoffice process * still *",z)==0 ){
        bOutput = (eType & 0x04)!=0;
        nHang++;
      }else
      if( sqlite3_strglob("warning: POST from different origin*",z)==0 ){
        bOutput = (eType & 0x08)!=0;
        nXPost++;
      }else
      if( sqlite3_strglob("SECURITY: authorizer blocks*",z)==0
       || sqlite3_strglob("warning: SQLITE_AUTH*",z)==0
      ){
        bOutput = (eType & 0x10)!=0;
        nAuth++;
      }else
      if( strncmp(z,"possible", 8)==0 && strstr(z,"tainted")!=0 ){
        bOutput = (eType & 0x40)!=0;
        nVuln++;
      }else
      {
        bOutput = (eType & 0x800)!=0;
        nOther++;
      }
      if( bOutput ){
        @ %h(zTime)\
      }
    }
    if( strncmp(z, "--------", 8)==0 ){
      size_t n = strlen(z);
      memcpy(zTime, z, n+1);
      prevWasTime = 1;
      bOutput = 0;
    }else{
      prevWasTime = 0;
    }
    if( bOutput && eType ){
      @ %h(z)\
    }
  }
  fclose(in);
  if( eType ){
    @ </pre>
  }
  if( eType==0 ){
    int nNonHack = nPanic + nHang + nAuth + nSmtp + nVuln + nOther;
    int nTotal = nNonHack + nHack + nXPost;
    @ <p><table border="a" cellspacing="0" cellpadding="5">
    if( nPanic>0 ){
      @ <tr><td align="right">%d(nPanic)</td>
      @     <td><a href="./errorlog?y=2">Panics</a></td>
    }
    if( nVuln>0 ){
      @ <tr><td align="right">%d(nVuln)</td>
      @     <td><a href="./errorlog?y=64">TH1 Vulnerabilities</a></td>
    }
    if( nHack>0 ){
      @ <tr><td align="right">%d(nHack)</td>
      @     <td><a href="./errorlog?y=1">Hack Attempts</a></td>
    }
    if( nHang>0 ){
      @ <tr><td align="right">%d(nHang)</td>
      @     <td><a href="./errorlog?y=4">Hung Backoffice</a></td>
    }
    if( nXPost>0 ){
      @ <tr><td align="right">%d(nXPost)</td>
      @     <td><a href="./errorlog?y=8">POSTs from different origin</a></td>
    }
    if( nAuth>0 ){
      @ <tr><td align="right">%d(nAuth)</td>
      @     <td><a href="./errorlog?y=16">SQLITE_AUTH and similar</a></td>
    }
    if( nSmtp>0 ){
      @ <tr><td align="right">%d(nSmtp)</td>
      @     <td><a href="./errorlog?y=32">SMTP faults</a></td>
    }
    if( nOther>0 ){
      @ <tr><td align="right">%d(nOther)</td>
      @     <td><a href="./errorlog?y=2048">Other</a></td>
    }
    @ <tr><td align="right">%d(nTotal)</td>
    if( nTotal>0 ){
      @     <td><a href="./errorlog?y=4095">All Messages</a></td>
    }else{
      @     <td>All Messages</td>
    }
    @ </table>
  }
  style_finish_page();
}
