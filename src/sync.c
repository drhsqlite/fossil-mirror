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
** This file contains code used to push, pull, and sync a repository
*/
#include "config.h"
#include "sync.h"
#include <assert.h>

/*
** Explain what type of sync operation is about to occur
*/
static void sync_explain(unsigned syncFlags){
  if( g.url.isAlias ){
    const char *url;
    if( g.url.useProxy ){
      url = g.url.proxyUrlCanonical;
    }else{
      url = g.url.canonical;
    }
    if( (syncFlags & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL) ){
      fossil_print("Sync with %s\n", url);
    }else if( syncFlags & SYNC_PUSH ){
      fossil_print("Push to %s\n", url);
    }else if( syncFlags & SYNC_PULL ){
      fossil_print("Pull from %s\n", url);
    }
  }
}


/*
** Call client_sync() one or more times in order to complete a
** sync operation.  Usually, client_sync() is called only once, though
** is can be called multiple times if the SYNC_ALLURL flags is set.
*/
static int client_sync_all_urls(
  unsigned syncFlags,      /* Mask of SYNC_* flags */
  unsigned configRcvMask,  /* Receive these configuration items */
  unsigned configSendMask, /* Send these configuration items */
  const char *zAltPCode    /* Alternative project code (usually NULL) */
){
  int nErr = 0;            /* Number of errors seen */
  int nOther;              /* Number of extra remote URLs */
  char **azOther;          /* Text of extra remote URLs */
  int i;                   /* Loop counter */
  int iEnd;                /* Loop termination point */
  int nextIEnd;            /* Loop termination point for next pass */
  int iPass;               /* Which pass through the remotes.  0 or 1 */
  int nPass;               /* Number of passes to make.  1 or 2 */
  Stmt q;                  /* An SQL statement */
  UrlData baseUrl;         /* Saved parse of the default remote */

  sync_explain(syncFlags);
  if( (syncFlags & SYNC_ALLURL)==0 ){
    /* Common-case:  Only sync with the remote identified by g.url */
    nErr = client_sync(syncFlags, configRcvMask, configSendMask, zAltPCode, 0);
    if( nErr==0 ) url_remember();
    return nErr;
  }

  /* If we reach this point, it means we want to sync with all remotes */
  memset(&baseUrl, 0, sizeof(baseUrl));
  url_move_parse(&baseUrl, &g.url);
  nOther = 0;
  azOther = 0;
  db_prepare(&q,
    "SELECT substr(name,10) FROM config"
    " WHERE name glob 'sync-url:*'"
    "   AND value<>(SELECT value FROM config WHERE name='last-sync-url')"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUrl = db_column_text(&q, 0);
    azOther = fossil_realloc(azOther, sizeof(*azOther)*(nOther+1));
    azOther[nOther++] = fossil_strdup(zUrl);
  }
  db_finalize(&q);
  iEnd = nOther+1;
  nextIEnd = 0;
  nPass = 1 + ((syncFlags & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL));
  for(iPass=0; iPass<nPass; iPass++){
    for(i=0; i<iEnd; i++){
      int rc;
      int nRcvd;
      if( i==0 ){
        url_move_parse(&g.url, &baseUrl);  /* Load canonical URL */
      }else{
        /* Load an auxiliary remote URL */
        url_parse(azOther[i-1],
                  URL_PROMPT_PW|URL_ASK_REMEMBER_PW|URL_USE_CONFIG);
      }
      if( i>0 || iPass>0 ) sync_explain(syncFlags);
      rc = client_sync(syncFlags, configRcvMask, configSendMask,
                       zAltPCode, &nRcvd);
      if( nRcvd>0 ){
        /* If new artifacts were received, we want to repeat all prior
        ** remotes on the second pass */
        nextIEnd = i;
      }
      nErr += rc;
      if( rc==0 && iPass==0 ){
        if( i==0 ){
          url_remember();
        }else if( (g.url.flags & URL_REMEMBER_PW)!=0 ){
          char *zKey = mprintf("sync-pw:%s", azOther[i-1]);
          char *zPw = obscure(g.url.passwd);
          if( zPw && zPw[0] ){
            db_set(zKey/*works-like:""*/, zPw, 0);
          }
          fossil_free(zPw);
          fossil_free(zKey);
        }
      }
      if( i==0 ){
        url_move_parse(&baseUrl, &g.url); /* Don't forget canonical URL */
      }else{
        url_unparse(&g.url);  /* Delete auxiliary URL parses */
      }
    }
    iEnd = nextIEnd;
  }
  for(i=0; i<nOther; i++){
    fossil_free(azOther[i]);
    azOther[i] = 0;
  }
  fossil_free(azOther);
  url_move_parse(&g.url, &baseUrl);  /* Restore the canonical URL parse */
  return nErr;
}


/*
** If the repository is configured for autosyncing, then do an
** autosync.  Bits of the "flags" parameter determine details of behavior:
**
**   SYNC_PULL           Pull content from the server to the local repo
**   SYNC_PUSH           Push content from local up to the server
**   SYNC_CKIN_LOCK      Take a check-in lock on the current check-out.
**   SYNC_VERBOSE        Extra output
**
** Return the number of errors.
**
** The autosync setting can be a boolean or "pullonly".  No autosync
** is attempted if the autosync setting is off, and only auto-pull is
** attempted if autosync is set to "pullonly".  The check-in lock is
** not acquired unless autosync is set to "on".
**
** If dont-push setting is true, that is the same as having autosync
** set to pullonly.
*/
static int autosync(int flags, const char *zSubsys){
  const char *zAutosync;
  int rc;
  int configSync = 0;       /* configuration changes transferred */
  if( g.fNoSync ){
    return 0;
  }
  zAutosync = db_get_for_subsystem("autosync", zSubsys);
  if( zAutosync==0 ) zAutosync = "on";  /* defend against misconfig */
  if( is_false(zAutosync) ) return 0;
  if( db_get_boolean("dont-push",0)
   || sqlite3_strglob("*pull*", zAutosync)==0
  ){
    flags &= ~SYNC_CKIN_LOCK;
    if( flags & SYNC_PUSH ) return 0;
  }
  if( find_option("verbose","v",0)!=0 ) flags |= SYNC_VERBOSE;
  url_parse(0, URL_REMEMBER|URL_USE_CONFIG);
  if( g.url.protocol==0 ) return 0;
  if( g.url.user!=0 && g.url.passwd==0 ){
    g.url.passwd = unobscure(db_get("last-sync-pw", 0));
    g.url.flags |= URL_PROMPT_PW;
    url_prompt_for_password();
  }
  g.zHttpAuth = get_httpauth();
  if( sqlite3_strglob("*all*", zAutosync)==0 ){
    rc = client_sync_all_urls(flags|SYNC_ALLURL, configSync, 0, 0);
  }else{
    url_remember();
    sync_explain(flags);
    url_enable_proxy("via proxy: ");
    rc = client_sync(flags, configSync, 0, 0, 0);
  }
  return rc;
}

/*
** This routine will try a number of times to perform autosync with a
** 0.5 second sleep between attempts.  The number of attempts is determined
** by the "autosync-tries" setting, which defaults to 1.
**
** Return zero on success and non-zero on a failure.  If failure occurs
** and doPrompt flag is true, ask the user if they want to continue, and
** if they answer "yes" then return zero in spite of the failure.
*/
int autosync_loop(int flags, int doPrompt, const char *zSubsystem){
  int n = 0;
  int rc = 0;
  int nTries = db_get_int("autosync-tries", 1);
  if( (flags & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL)
   && db_get_boolean("uv-sync",0)
  ){
    flags |= SYNC_UNVERSIONED;
  }
  if( nTries<1 ) nTries = 1;
  while( (n==0 || n<nTries) && (rc=autosync(flags, zSubsystem)) ){
    if( rc ){
      if( ++n<nTries ){
        fossil_warning("Autosync failed, making another attempt.");
        sqlite3_sleep(500);
      }else{
        fossil_warning("Autosync failed.");
      }
    }
  }
  if( rc && doPrompt ){
    Blob ans;
    char cReply;
    prompt_user("continue in spite of sync failure (y/N)? ", &ans);
    cReply = blob_str(&ans)[0];
    if( cReply=='y' || cReply=='Y' ) rc = 0;
    blob_reset(&ans);
  }
  return rc;
}

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
static void process_sync_args(
  unsigned *pConfigFlags,      /* Write configuration flags here */
  unsigned *pSyncFlags,        /* Write sync flags here */
  int uvOnly,                  /* Special handling flags for UV sync */
  unsigned urlOmitFlags        /* Omit these URL flags */
){
  const char *zUrl = 0;
  const char *zHttpAuth = 0;
  unsigned configSync = 0;
  unsigned urlFlags = URL_REMEMBER | URL_PROMPT_PW;
  int urlOptional = 0;
  if( find_option("autourl",0,0)!=0 ){
    urlOptional = 1;
    urlFlags = 0;
  }
  zHttpAuth = find_option("httpauth","B",1);
  if( find_option("once",0,0)!=0 ) urlFlags &= ~URL_REMEMBER;
  if( (*pSyncFlags) & SYNC_FROMPARENT ) urlFlags |= URL_USE_PARENT;
  if( !uvOnly ){
    if( find_option("private",0,0)!=0 ){
      *pSyncFlags |= SYNC_PRIVATE;
    }
    /* The --verily option to sync, push, and pull forces extra igot cards
    ** to be exchanged.  This can overcome malfunctions in the sync protocol.
    */
    if( find_option("verily",0,0)!=0 ){
      *pSyncFlags |= SYNC_RESYNC;
    }
  }
  if( find_option("private",0,0)!=0 ){
    *pSyncFlags |= SYNC_PRIVATE;
  }
  if( find_option("verbose","v",0)!=0 ){
    *pSyncFlags |= SYNC_VERBOSE;
    if( find_option("verbose","v",0)!=0 ){
      *pSyncFlags |= SYNC_XVERBOSE;
    }
  }
  if( find_option("no-http-compression",0,0)!=0 ){
    *pSyncFlags |= SYNC_NOHTTPCOMPRESS;
  }
  if( find_option("all",0,0)!=0 ){
    *pSyncFlags |= SYNC_ALLURL;
  }

  /* Undocumented option to cause links transitive links to other
  ** repositories to be shared */
  if( ((*pSyncFlags) & SYNC_PULL)!=0
   && find_option("share-links",0,0)!=0
  ){
    *pSyncFlags |= SYNC_SHARE_LINKS;
  }

  /* Option:  --transport-command COMMAND
  **
  ** Causes COMMAND to be run with three arguments in order to talk
  ** to the server.
  **
  **       COMMAND URL PAYLOAD REPLY
  **
  ** URL is the server name.  PAYLOAD is the name of a temporary file
  ** that will contain the xfer-protocol payload to send to the server.
  ** REPLY is a temporary filename in which COMMAND should write the
  ** content of the reply from the server.
  **
  ** CMD is reponsible for HTTP redirects.  The following Fossil command
  ** can be used for CMD to achieve a working sync:
  **
  **      fossil test-httpmsg --xfer
  */
  g.zHttpCmd = find_option("transport-command",0,1);

  url_proxy_options();
  clone_ssh_find_options();
  if( !uvOnly ) db_find_and_open_repository(0, 0);
  db_open_config(0, 1);
  if( g.argc==2 ){
    if( db_get_boolean("auto-shun",0) ) configSync = CONFIGSET_SHUN;
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
    if( (*pSyncFlags) & SYNC_ALLURL ){
      fossil_fatal("cannot use both the --all option and specific URL \"%s\"",
          zUrl);
    }
  }
  if( ((*pSyncFlags) & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL)
   && db_get_boolean("uv-sync",0)
  ){
    *pSyncFlags |= SYNC_UNVERSIONED;
  }
  urlFlags &= ~urlOmitFlags;
  if( urlFlags & URL_REMEMBER ){
    clone_ssh_db_set_options();
  }
  url_parse(zUrl, urlFlags|URL_USE_CONFIG);
  remember_or_get_http_auth(zHttpAuth, urlFlags & URL_REMEMBER, zUrl);
  if( g.url.protocol==0 ){
    if( urlOptional ) fossil_exit(0);
    usage("URL");
  }
  user_select();
  url_enable_proxy("via proxy: ");
  *pConfigFlags |= configSync;
  if( (*pSyncFlags & SYNC_ALLURL)==0 && zUrl==0 ){
    const char *zAutosync = db_get_for_subsystem("autosync", "sync");
    if( sqlite3_strglob("*all*", zAutosync)==0 ){
      *pSyncFlags |= SYNC_ALLURL;
    }
  }
}


/*
** COMMAND: pull
**
** Usage: %fossil pull ?URL? ?options?
**
** Pull all sharable changes from a remote repository into the local
** repository.  Sharable changes include public check-ins, edits to
** wiki pages, tickets, tech-notes, and forum posts.  Add
** the --private option to pull private branches.  Use the
** "configuration pull" command to pull website configuration details.
**
** If URL is not specified, then the URL from the most recent clone, push,
** pull, remote, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**   --all                      Pull from all remotes, not just the default
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --from-parent-project      Pull content from the parent project
**   --ipv4                     Use only IPv4, not IPv6
**   --no-http-compression      Do not compress HTTP traffic
**   --once                     Do not remember URL for subsequent syncs
**   --private                  Pull private branches too
**   --project-code CODE        Use CODE as the project code
**   --proxy PROXY              Use the specified HTTP proxy
**   -R|--repository REPO       Local repository to pull into
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   --transport-command CMD    Use external command CMD to move messages
**                              between client and server
**   -v|--verbose               Additional (debugging) output - use twice to
**                              also trace network traffic.
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: [[clone]], [[config]], [[push]], [[remote]], [[sync]]
*/
void pull_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PULL;
  unsigned urlOmitFlags = 0;
  const char *zAltPCode = find_option("project-code",0,1);
  if( find_option("from-parent-project",0,0)!=0 ){
    syncFlags |= SYNC_FROMPARENT;
  }
  if( zAltPCode ) urlOmitFlags = URL_REMEMBER;
  process_sync_args(&configFlags, &syncFlags, 0, urlOmitFlags);

  /* We should be done with options.. */
  verify_all_options();

  client_sync_all_urls(syncFlags, configFlags, 0, zAltPCode);
}

/*
** COMMAND: push
**
** Usage: %fossil push ?URL? ?options?
**
** Push all sharable changes from the local repository to a remote
** repository.  Sharable changes include public check-ins, edits to
** wiki pages, tickets, tech-notes, and forum posts.  Use
** --private to also push private branches.  Use the "configuration
** push" command to push website configuration details.
**
** If URL is not specified, then the URL from the most recent clone, push,
** pull, remote, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**   --all                      Push to all remotes, not just the default
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --ipv4                     Use only IPv4, not IPv6
**   --no-http-compression      Do not compress HTTP traffic
**   --once                     Do not remember URL for subsequent syncs
**   --proxy PROXY              Use the specified HTTP proxy
**   --private                  Push private branches too
**   -R|--repository REPO       Local repository to push from
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   --transport-command CMD    Use external command CMD to communicate with
**                              the server
**   -v|--verbose               Additional (debugging) output - use twice for
**                              network debugging
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: [[clone]], [[config]], [[pull]], [[remote]], [[sync]]
*/
void push_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PUSH;
  process_sync_args(&configFlags, &syncFlags, 0, 0);

  /* We should be done with options.. */
  verify_all_options();

  if( db_get_boolean("dont-push",0) ){
    fossil_fatal("pushing is prohibited: the 'dont-push' option is set");
  }
  client_sync_all_urls(syncFlags, 0, 0, 0);
}


/*
** COMMAND: sync
**
** Usage: %fossil sync ?REMOTE? ?options?
**
** Synchronize all sharable changes between the local repository and a
** remote repository, with the remote provided as a URL or a
** configured remote name (see the [[remote]] command).  Sharable
** changes include public check-ins and edits to wiki pages, tickets,
** forum posts, and technical notes.
**
** If REMOTE is not specified, then the URL from the most recent clone, push,
** pull, remote, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**   --all                      Sync with all remotes, not just the default
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --ipv4                     Use only IPv4, not IPv6
**   --no-http-compression      Do not compress HTTP traffic
**   --once                     Do not remember URL for subsequent syncs
**   --proxy PROXY              Use the specified HTTP proxy
**   --private                  Sync private branches too
**   -R|--repository REPO       Local repository to sync with
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   --transport-command CMD    Use external command CMD to move message
**                              between the client and the server
**   -u|--unversioned           Also sync unversioned content
**   -v|--verbose               Additional (debugging) output - use twice to
**                              get network debug info
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: [[clone]], [[pull]], [[push]], [[remote]]
*/
void sync_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PUSH|SYNC_PULL;
  if( find_option("unversioned","u",0)!=0 ){
    syncFlags |= SYNC_UNVERSIONED;
  }
  process_sync_args(&configFlags, &syncFlags, 0, 0);

  /* We should be done with options.. */
  verify_all_options();

  if( db_get_boolean("dont-push",0) ) syncFlags &= ~SYNC_PUSH;
  if( (syncFlags & SYNC_PUSH)==0 ){
    fossil_warning("pull only: the 'dont-push' option is set");
  }
  client_sync_all_urls(syncFlags, configFlags, 0, 0);
}

/*
** Handle the "fossil unversioned sync" and "fossil unversioned revert"
** commands.
*/
void sync_unversioned(unsigned syncFlags){
  unsigned configFlags = 0;
  (void)find_option("uv-noop",0,0);
  process_sync_args(&configFlags, &syncFlags, 1, 0);
  verify_all_options();
  client_sync(syncFlags, 0, 0, 0, 0);
}

/*
** COMMAND: remote
** COMMAND: remote-url*
**
** Usage: %fossil remote ?SUBCOMMAND ...?
**
** View or modify the URLs of remote repositories used for syncing.
** The "default" remote is specially named by Fossil and corresponds to
** the URL used in the most recent "sync", "push", "pull", "clone", or
** similar command.  As such, the default remote can be updated by
** Fossil with each sync command.  Other named remotes are persistent.
**
** > fossil remote
**
**     With no arguments, this command shows the current default remote
**     URL.  If there is no default, it shows "off".
**
** > fossil remote add NAME URL
**
**     Add a new named URL. Afterwards, NAME can be used as a short
**     symbolic name for URL in contexts where a URL is required. The
**     URL argument can be "default" or a prior symbolic name to make
**     a copy of an existing URL under the new NAME. The "default"
**     remote cannot be defined with this subcommand; instead,
**     use 'fossil remote REF' as documented below.
**
** > fossil remote config-data
**
**     DEBUG USE ONLY - Show the name and value of every CONFIG table
**     entry in the repository that is associated with the remote URL store.
**     Passwords are obscured in the output.
**
** > fossil remote delete NAME
**
**     Delete a named URL previously created by the "add" subcommand.
**
** > fossil remote hyperlink ?FILENAME? ?LINENUM? ?LINENUM?
**
**     Print a URL that will access the current check-out on the remote
**     repository.  Or if the FILENAME argument is included, print the
**     URL to access that particular file within the current check-out.
**     If one or two linenumber arguments are provided after the filename,
**     then the URL is for the line or range of lines specified.
**
** > fossil remote list|ls
**
**     Show all remote repository URLs.
**
** > fossil remote off
**
**     Forget the default URL. This disables autosync.
**
**     This is a convenient way to enter "airplane mode".  To enter
**     airplane mode, first save the current default URL, then turn the
**     default off.  Perhaps like this:
**
**         fossil remote add main default
**         fossil remote off
**
**     To exit airplane mode and turn autosync back on again:
**
**         fossil remote main
**
** > fossil remote scrub
**
**     Forget any saved passwords for remote repositories, but continue
**     to remember the URLs themselves.  You will be prompted for the
**     password the next time it is needed.
**
** > fossil remote ui ?FILENAME? ?LINENUM? ?LINENUM?
**
**     Bring up a web browser pointing at the remote repository, and
**     specifically to the page that describes the current check-out
**     on that remote repository.  Or if FILENAME and/or LINENUM arguments
**     are provided, to the specific file and range of lines.  This
**     command is similar to "fossil remote hyperlink" except that instead
**     of printing the URL, it passes the URL off to the web browser.
**
** > fossil remote REF
**
**     Make REF the new default URL, replacing the prior default.
**     REF may be a URL or a NAME from a prior "add".
*/
void remote_url_cmd(void){
  char *zUrl, *zArg;
  int nArg;
  int showPw;
  db_find_and_open_repository(0, 0);
  showPw = find_option("show-passwords",0,0)!=0;

  /* We should be done with options.. */
  verify_all_options();

  /* 2021-10-25: A note about data structures.
  **
  ** The remote URLs are stored in the CONFIG table.  The URL is stored
  ** separately from the password.  The password is obscured using the
  ** obscure() function.
  **
  ** Originally, Fossil only preserved a single remote URL.  That URL
  ** is stored in "last-sync-url" and the password in "last-sync-pw".  The
  ** ability to have multiple remotes was added later so these names
  ** were retained for backwards compatibility.  The other remotes are
  ** stored in "sync-url:NAME" and "sync-pw:NAME" where NAME is the name
  ** of the remote.
  **
  ** The last-sync-url is called "default" for the display list.
  **
  ** The last-sync-url might be duplicated into one of the sync-url:NAME
  ** entries.  Thus, when doing a "fossil sync --all" or an autosync with
  ** autosync=all, each sync-url:NAME entry is checked to see if it is the
  ** same as last-sync-url and if it is then that entry is skipped.
  */

  if( g.argc==2 ){
    /* "fossil remote" with no arguments:  Show the last sync URL. */
    zUrl = db_get("last-sync-url", 0);
    if( zUrl==0 ){
      fossil_print("off\n");
    }else{
      url_parse(zUrl, 0);
      fossil_print("%s\n", g.url.canonical);
    }
    return;
  }
  zArg = g.argv[2];
  nArg = (int)strlen(zArg);
  if( strcmp(zArg,"off")==0 ){
    /* fossil remote off
    ** Forget the last-sync-URL and its password
    */
    if( g.argc!=3 ) usage("off");
remote_delete_default:
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "DELETE FROM config WHERE name GLOB 'last-sync-*';"
    );
    db_protect_pop();
    return;
  }
  if( strncmp(zArg, "list", nArg)==0 || strcmp(zArg,"ls")==0 ){
    Stmt q;
    if( g.argc!=3 ) usage("list");
    db_prepare(&q,
      "SELECT 'default', value FROM config WHERE name='last-sync-url'"
      " UNION ALL "
      "SELECT substr(name,10), value FROM config"
      " WHERE name GLOB 'sync-url:*'"
      " ORDER BY 1"
    );
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%-18s %s\n", db_column_text(&q,0), db_column_text(&q,1));
    }
    db_finalize(&q);
    return;
  }
  if( strcmp(zArg, "add")==0 ){
    char *zName;
    char *zUrl;
    UrlData x;
    if( g.argc!=5 ) usage("add NAME URL");
    memset(&x, 0, sizeof(x));
    zName = g.argv[3];
    zUrl = g.argv[4];
    if( strcmp(zName,"default")==0 ){
      fossil_fatal("update the \"default\" remote-url with 'fossil remote REF'"
          "\nsee 'fossil help remote' for complete usage information");
    }
    db_begin_write();
    if( fossil_strcmp(zUrl,"default")==0 ){
      x.canonical = db_get("last-sync-url",0);
      x.passwd = unobscure(db_get("last-sync-pw",0));
    }else{
      url_parse_local(zUrl, URL_PROMPT_PW|URL_USE_CONFIG, &x);
    }
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
       "REPLACE INTO config(name, value, mtime)"
       " VALUES('sync-url:%q',%Q,now())",
       zName, x.canonical
    );
    db_multi_exec(
       "REPLACE INTO config(name, value, mtime)"
       " VALUES('sync-pw:%q',obscure(%Q),now())",
       zName, x.passwd
    );
    db_protect_pop();
    db_commit_transaction();
    return;
  }
  if( strncmp(zArg, "delete", nArg)==0 ){
    char *zName;
    if( g.argc!=4 ) usage("delete NAME");
    zName = g.argv[3];
    if( strcmp(zName,"default")==0 ) goto remote_delete_default;
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec("DELETE FROM config WHERE name glob 'sync-url:%q'", zName);
    db_multi_exec("DELETE FROM config WHERE name glob 'sync-pw:%q'", zName);
    db_protect_pop();
    db_commit_transaction();
    return;
  }
  if( strncmp(zArg, "hyperlink", nArg)==0
   || (nArg==2 && strcmp(zArg, "ui")==0)
  ){
    char *zBase;
    char *zUuid;
    Blob fname;
    Blob url;
    char *zSubCmd = g.argv[2][0]=='u' ? "ui" : "hyperlink";
    if( !db_table_exists("localdb","vvar") ){
      fossil_fatal("the \"remote %s\" command only works from "
                   "within an open check-out", zSubCmd);
    }
    zUrl = db_get("last-sync-url", 0);
    if( zUrl==0 ){
      zUrl = "http://localhost:8080/";
    }
    url_parse(zUrl, 0);
    if( g.url.isFile ){
      url_parse("http://localhost:8080/", 0);
    }
    zBase = url_nouser(&g.url);
    blob_init(&url, 0, 0);
    if( g.argc==3 ){
      blob_appendf(&url, "%s/info/%!S",
        zBase,
        db_text("???",
          "SELECT uuid FROM blob, vvar"
          " WHERE blob.rid=0+vvar.value"
          "   AND vvar.name='checkout';"
        ));
    }else{
      blob_init(&fname, 0, 0);
      file_tree_name(g.argv[3], &fname, 0, 1);
      zUuid = db_text(0,
        "SELECT uuid FROM files_of_checkin"
        " WHERE checkinID=(SELECT value FROM vvar WHERE name='checkout')"
        "   AND filename=%Q",
        blob_str(&fname)
      );
      if( zUuid==0 ){
        fossil_fatal("not a managed file: \"%s\"", g.argv[3]);
      }
      blob_appendf(&url, "%s/info/%S",zBase,zUuid);
      if( g.argc>4 ){
        int ln1 = atoi(g.argv[4]);
        if( ln1<=0 || sqlite3_strglob("*[^0-9]*",g.argv[4])==0 ){
          fossil_fatal("\"%s\" is not a valid line number", g.argv[4]);
        }
        if( g.argc>5 ){
          int ln2 = atoi(g.argv[5]);
          if( ln2==0 || sqlite3_strglob("*[^0-9]*",g.argv[5])==0 ){
            fossil_fatal("\"%s\" is not a valid line number", g.argv[5]);
          }
          if( ln2<=ln1 ){
            fossil_fatal("second line number should be greater than the first");
          }
          blob_appendf(&url,"?ln=%d,%d", ln1, ln2);
        }else{
          blob_appendf(&url,"?ln=%d", ln1);
        }
      }
      if( g.argc>6 ){
        usage(mprintf("%s ?FILENAME? ?LINENUMBER? ?LINENUMBER?", zSubCmd));
      }
    }
    if( g.argv[2][0]=='u' ){
      char *zCmd;
      zCmd = mprintf("%s %!$ &", fossil_web_browser(), blob_str(&url));
      fossil_system(zCmd);
    }else{
      fossil_print("%s\n", blob_str(&url));
    }
    return;
  }
  if( strncmp(zArg, "scrub", nArg)==0 ){
    if( g.argc!=3 ) usage("scrub");
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec("DELETE FROM config WHERE name glob 'sync-pw:*'");
    db_multi_exec("DELETE FROM config WHERE name = 'last-sync-pw'");
    db_protect_pop();
    db_commit_transaction();
    return;
  }
  if( strncmp(zArg, "config-data", nArg)==0 ){
    /* Undocumented command:  "fossil remote config-data [-show-passwords]"
    **
    ** Show the CONFIG table entries that relate to remembering remote URLs
    */
    Stmt q;
    int n;
    sqlite3_create_function(g.db, "unobscure", 1, SQLITE_UTF8, &g.db,
                            db_obscure, 0, 0);
    n = db_int(13,
       "SELECT max(length(name))"
       "  FROM config"
       " WHERE name GLOB 'sync-*:*'"
          " OR name GLOB 'last-sync-*'"
          " OR name GLOB 'parent-project-*'"
    );
    db_prepare(&q,
      "SELECT name,"
      "  CASE WHEN name NOT LIKE '%%sync-pw%%' AND name<>'parent-project-pw'"
      "       THEN value"
      "       WHEN %d THEN unobscure(value)"
      "       ELSE printf('%%.*c',length(value)/2-1,'*') END"
      "  FROM config"
      " WHERE name GLOB 'sync-*:*'"
         " OR name GLOB 'last-sync-*'"
         " OR name GLOB 'parent-project-*'"
      " ORDER BY name LIKE '%%sync-pw%%' OR name='parent-project-pw', name",
      showPw
    );
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%-*s  %s\n",
        n, db_column_text(&q,0),
        db_column_text(&q,1)
      );
    }
    db_finalize(&q);
    return;
  }
  if( sqlite3_strlike("http://%",zArg,0)==0
   || sqlite3_strlike("https://%",zArg,0)==0
   || sqlite3_strlike("ssh:%",zArg,0)==0
   || sqlite3_strlike("file:%",zArg,0)==0
   || db_exists("SELECT 1 FROM config WHERE name='sync-url:%q'",zArg)
  ){
    db_unset("last-sync-url", 0);
    db_unset("last-sync-pw", 0);
    url_parse(g.argv[2], URL_REMEMBER|URL_PROMPT_PW|
                         URL_USE_CONFIG|URL_ASK_REMEMBER_PW);
    url_remember();
    return;
  }
  fossil_fatal("unknown command \"%s\" - should be a URL or one of: "
               "add delete hyperlink list off scrub", zArg);
}

/*
** COMMAND: backup*
**
** Usage: %fossil backup ?OPTIONS? FILE|DIRECTORY
**
** Make a backup of the repository into the named file or into the named
** directory.  This backup is guaranteed to be consistent even if there are
** concurrent changes taking place on the repository.  In other words, it
** is safe to run "fossil backup" on a repository that is in active use.
**
** Only the main repository database is backed up by this command.  The
** open check-out file (if any) is not saved.  Nor is the global configuration
** database.
**
** Options:
**    --overwrite              OK to overwrite an existing file
**    -R NAME                  Filename of the repository to backup
*/
void backup_cmd(void){
  char *zDest;
  int bOverwrite = 0;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  bOverwrite = find_option("overwrite",0,0)!=0;
  verify_all_options();
  if( g.argc!=3 ){
    usage("FILE|DIRECTORY");
  }
  zDest = g.argv[2];
  if( file_isdir(zDest, ExtFILE)==1 ){
    zDest = mprintf("%s/%s", zDest, file_tail(g.zRepositoryName));
  }
  if( file_isfile(zDest, ExtFILE) ){
    if( bOverwrite ){
      if( file_delete(zDest) ){
        fossil_fatal("unable to delete old copy of \"%s\"", zDest);
      }
    }else{
      fossil_fatal("backup \"%s\" already exists", zDest);
    }
  }
  db_unprotect(PROTECT_ALL);
  db_multi_exec("VACUUM repository INTO %Q", zDest);
}
