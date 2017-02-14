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
** If the repository is configured for autosyncing, then do an
** autosync.  This will be a pull if the argument is true or a push
** if the argument is false.
**
** Return the number of errors.
*/
int autosync(int flags){
  const char *zAutosync;
  int rc;
  int configSync = 0;       /* configuration changes transferred */
  if( g.fNoSync ){
    return 0;
  }
  if( flags==SYNC_PUSH && db_get_boolean("dont-push",0) ){
    return 0;
  }
  zAutosync = db_get("autosync", 0);
  if( zAutosync ){
    if( (flags & SYNC_PUSH)!=0 && fossil_strncmp(zAutosync,"pull",4)==0 ){
      return 0;   /* Do not auto-push when autosync=pullonly */
    }
    if( is_false(zAutosync) ){
      return 0;   /* Autosync is completely off */
    }
  }else{
    /* Autosync defaults on.  To make it default off, "return" here. */
  }
  url_parse(0, URL_REMEMBER);
  if( g.url.protocol==0 ) return 0;
  if( g.url.user!=0 && g.url.passwd==0 ){
    g.url.passwd = unobscure(db_get("last-sync-pw", 0));
    g.url.flags |= URL_PROMPT_PW;
    url_prompt_for_password();
  }
  g.zHttpAuth = get_httpauth();
  url_remember();
#if 0 /* Disabled for now */
  if( (flags & AUTOSYNC_PULL)!=0 && db_get_boolean("auto-shun",1) ){
    /* When doing an automatic pull, also automatically pull shuns from
    ** the server if pull_shuns is enabled.
    **
    ** TODO:  What happens if the shun list gets really big?
    ** Maybe the shunning list should only be pulled on every 10th
    ** autosync, or something?
    */
    configSync = CONFIGSET_SHUN;
  }
#endif
  if( find_option("verbose","v",0)!=0 ) flags |= SYNC_VERBOSE;
  fossil_print("Autosync:  %s\n", g.url.canonical);
  url_enable_proxy("via proxy: ");
  rc = client_sync(flags, configSync, 0);
  return rc;
}

/*
** This routine will try a number of times to perform autosync with a
** 0.5 second sleep between attempts.
**
** Return zero on success and non-zero on a failure.  If failure occurs
** and doPrompt flag is true, ask the user if they want to continue, and
** if they answer "yes" then return zero in spite of the failure.
*/
int autosync_loop(int flags, int nTries, int doPrompt){
  int n = 0;
  int rc = 0;
  if( (flags & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL)
   && db_get_boolean("uv-sync",0)
  ){
    flags |= SYNC_UNVERSIONED;
  }
  while( (n==0 || n<nTries) && (rc=autosync(flags)) ){
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
  int uvOnly                   /* Special handling flags for UV sync */
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
  if( (*pSyncFlags) & SYNC_FROMPARENT ) urlFlags &= ~URL_REMEMBER;
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
  }
  url_proxy_options();
  clone_ssh_find_options();
  if( !uvOnly ) db_find_and_open_repository(0, 0);
  db_open_config(0, 0);
  if( g.argc==2 ){
    if( db_get_boolean("auto-shun",1) ) configSync = CONFIGSET_SHUN;
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
  }
  if( ((*pSyncFlags) & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL)
   && db_get_boolean("uv-sync",0)
  ){
    *pSyncFlags |= SYNC_UNVERSIONED;
  }
  if( urlFlags & URL_REMEMBER ){
    clone_ssh_db_set_options();
  }
  url_parse(zUrl, urlFlags);
  remember_or_get_http_auth(zHttpAuth, urlFlags & URL_REMEMBER, zUrl);
  url_remember();
  if( g.url.protocol==0 ){
    if( urlOptional ) fossil_exit(0);
    usage("URL");
  }
  user_select();
  if( g.argc==2 ){
    if( ((*pSyncFlags) & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL) ){
      fossil_print("Sync with %s\n", g.url.canonical);
    }else if( (*pSyncFlags) & SYNC_PUSH ){
      fossil_print("Push to %s\n", g.url.canonical);
    }else if( (*pSyncFlags) & SYNC_PULL ){
      fossil_print("Pull from %s\n", g.url.canonical);
    }
  }
  url_enable_proxy("via proxy: ");
  *pConfigFlags |= configSync;
}

/*
** COMMAND: pull
**
** Usage: %fossil pull ?URL? ?options?
**
** Pull all sharable changes from a remote repository into the local repository.
** Sharable changes include public check-ins, and wiki, ticket, and tech-note
** edits.  Add the --private option to pull private branches.  Use the
** "configuration pull" command to pull website configuration details.
**
** If URL is not specified, then the URL from the most recent clone, push,
** pull, remote-url, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --from-parent-project      Pull content from the parent project
**   --ipv4                     Use only IPv4, not IPv6
**   --once                     Do not remember URL for subsequent syncs
**   --proxy PROXY              Use the specified HTTP proxy
**   --private                  Pull private branches too
**   -R|--repository REPO       Local repository to pull into
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   -v|--verbose               Additional (debugging) output
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: clone, config pull, push, remote-url, sync
*/
void pull_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PULL;
  if( find_option("from-parent-project",0,0)!=0 ){
    syncFlags |= SYNC_FROMPARENT;
  }
  process_sync_args(&configFlags, &syncFlags, 0);

  /* We should be done with options.. */
  verify_all_options();

  client_sync(syncFlags, configFlags, 0);
}

/*
** COMMAND: push
**
** Usage: %fossil push ?URL? ?options?
**
** Push all sharable changes from the local repository to a remote repository.
** Sharable changes include public check-ins, and wiki, ticket, and tech-note
** edits.  Use --private to also push private branches.  Use the
** "configuration push" command to push website configuration details.
**
** If URL is not specified, then the URL from the most recent clone, push,
** pull, remote-url, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --ipv4                     Use only IPv4, not IPv6
**   --once                     Do not remember URL for subsequent syncs
**   --proxy PROXY              Use the specified HTTP proxy
**   --private                  Push private branches too
**   -R|--repository REPO       Local repository to push from
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   -v|--verbose               Additional (debugging) output
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: clone, config push, pull, remote-url, sync
*/
void push_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PUSH;
  process_sync_args(&configFlags, &syncFlags, 0);

  /* We should be done with options.. */
  verify_all_options();

  if( db_get_boolean("dont-push",0) ){
    fossil_fatal("pushing is prohibited: the 'dont-push' option is set");
  }
  client_sync(syncFlags, 0, 0);
}


/*
** COMMAND: sync
**
** Usage: %fossil sync ?URL? ?options?
**
** Synchronize all sharable changes between the local repository and a
** remote repository.  Sharable changes include public check-ins and
** edits to wiki pages, tickets, and technical notes.
**
** If URL is not specified, then the URL from the most recent clone, push,
** pull, remote-url, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --ipv4                     Use only IPv4, not IPv6
**   --once                     Do not remember URL for subsequent syncs
**   --proxy PROXY              Use the specified HTTP proxy
**   --private                  Sync private branches too
**   -R|--repository REPO       Local repository to sync with
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   -u|--unversioned           Also sync unversioned content
**   -v|--verbose               Additional (debugging) output
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: clone, pull, push, remote-url
*/
void sync_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PUSH|SYNC_PULL;
  if( find_option("unversioned","u",0)!=0 ){
    syncFlags |= SYNC_UNVERSIONED;
  }
  process_sync_args(&configFlags, &syncFlags, 0);

  /* We should be done with options.. */
  verify_all_options();

  if( db_get_boolean("dont-push",0) ) syncFlags &= ~SYNC_PUSH;
  client_sync(syncFlags, configFlags, 0);
  if( (syncFlags & SYNC_PUSH)==0 ){
    fossil_warning("pull only: the 'dont-push' option is set");
  }
}

/*
** Handle the "fossil unversioned sync" and "fossil unversioned revert"
** commands.
*/
void sync_unversioned(unsigned syncFlags){
  unsigned configFlags = 0;
  (void)find_option("uv-noop",0,0);
  process_sync_args(&configFlags, &syncFlags, 1);
  verify_all_options();
  client_sync(syncFlags, 0, 0);
}

/*
** COMMAND: remote-url
**
** Usage: %fossil remote-url ?URL|off?
**
** Query and/or change the default server URL used by the "pull", "push",
** and "sync" commands.
**
** The remote-url is set automatically by a "clone" command or by any
** "sync", "push", or "pull" command that specifies an explicit URL.
** The default remote-url is used by auto-syncing and by "sync", "push",
** "pull" that omit the server URL.
**
** See "fossil help clone" for further information about URL formats
**
** See also: clone, push, pull, sync
*/
void remote_url_cmd(void){
  char *zUrl;
  db_find_and_open_repository(0, 0);

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=2 && g.argc!=3 ){
    usage("?URL|off?");
  }
  if( g.argc==3 ){
    db_unset("last-sync-url", 0);
    db_unset("last-sync-pw", 0);
    db_unset("http-auth", 0);
    if( is_false(g.argv[2]) ) return;
    url_parse(g.argv[2], URL_REMEMBER|URL_PROMPT_PW|URL_ASK_REMEMBER_PW);
  }
  url_remember();
  zUrl = db_get("last-sync-url", 0);
  if( zUrl==0 ){
    fossil_print("off\n");
    return;
  }else{
    url_parse(zUrl, 0);
    fossil_print("%s\n", g.url.canonical);
  }
}
