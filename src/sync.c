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
    if( (flags & SYNC_PUSH)!=0 && memcmp(zAutosync,"pull",4)==0 ){
      return 0;   /* Do not auto-push when autosync=pullonly */
    }
    if( is_false(zAutosync) ){
      return 0;   /* Autosync is completely off */
    }
  }else{
    /* Autosync defaults on.  To make it default off, "return" here. */
  }
  url_parse(0, URL_REMEMBER);
  if( g.urlProtocol==0 ) return 0;  
  if( g.urlUser!=0 && g.urlPasswd==0 ){
    g.urlPasswd = unobscure(db_get("last-sync-pw", 0));
    g.urlFlags |= URL_PROMPT_PW;
    url_prompt_for_password();
  }
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
  fossil_print("Autosync:  %s\n", g.urlCanonical);
  url_enable_proxy("via proxy: ");
  rc = client_sync(flags, configSync, 0);
  if( rc ) fossil_warning("Autosync failed");
  return rc;
}

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
static void process_sync_args(unsigned *pConfigFlags, unsigned *pSyncFlags){
  const char *zUrl = 0;
  unsigned configSync = 0;
  unsigned urlFlags = URL_REMEMBER | URL_PROMPT_PW;
  int urlOptional = 0;
  if( find_option("autourl",0,0)!=0 ){
    urlOptional = 1;
    urlFlags = 0;
  }
  if( find_option("once",0,0)!=0 ) urlFlags &= ~URL_REMEMBER;
  if( find_option("private",0,0)!=0 ){
    *pSyncFlags |= SYNC_PRIVATE;
  }
  if( find_option("verbose","v",0)!=0 ){
    *pSyncFlags |= SYNC_VERBOSE;
  }
  /* The --verily option to sync, push, and pull forces extra igot cards
  ** to be exchanged.  This can overcome malfunctions in the sync protocol.
  */
  if( find_option("verily",0,0)!=0 ){
    *pSyncFlags |= SYNC_RESYNC;
  }
  url_proxy_options();
  clone_ssh_find_options();
  db_find_and_open_repository(0, 0);
  db_open_config(0);
  if( g.argc==2 ){
    if( db_get_boolean("auto-shun",1) ) configSync = CONFIGSET_SHUN;
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
  }
  if( urlFlags & URL_REMEMBER ){
    clone_ssh_db_set_options();
  }
  url_parse(zUrl, urlFlags);
  if( g.urlProtocol==0 ){
    if( urlOptional ) fossil_exit(0);
    usage("URL");
  }
  urlFlags = g.urlFlags;
  user_select();
  g.urlFlags = urlFlags;
  if( g.argc==2 ){
    if( ((*pSyncFlags) & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL) ){
      fossil_print("Sync with %s\n", g.urlCanonical);
    }else if( (*pSyncFlags) & SYNC_PUSH ){
      fossil_print("Push to %s\n", g.urlCanonical);
    }else if( (*pSyncFlags) & SYNC_PULL ){
      fossil_print("Pull from %s\n", g.urlCanonical);
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
** Pull changes from a remote repository into the local repository.
** Use the "-R REPO" or "--repository REPO" command-line options
** to specify an alternative repository file.
**
** See clone usage for possible URL formats.
**
** If the URL is not specified, then the URL from the most recent
** clone, push, pull, remote-url, or sync command is used.
**
** The URL specified normally becomes the new "remote-url" used for
** subsequent push, pull, and sync operations.  However, the "--once"
** command-line option makes the URL a one-time-use URL that is not
** saved.
**
** Use the --private option to pull private branches from the
** remote repository.
**
** See also: clone, push, sync, remote-url
*/
void pull_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PULL;
  process_sync_args(&configFlags, &syncFlags);
  client_sync(syncFlags, configFlags, 0);
}

/*
** COMMAND: push
**
** Usage: %fossil push ?URL? ?options?
**
** Push changes in the local repository over into a remote repository.
** Use the "-R REPO" or "--repository REPO" command-line options
** to specify an alternative repository file.
**
** See clone usage for possible URL formats.
**
** If the URL is not specified, then the URL from the most recent
** clone, push, pull, remote-url, or sync command is used.
**
** The URL specified normally becomes the new "remote-url" used for
** subsequent push, pull, and sync operations.  However, the "--once"
** command-line option makes the URL a one-time-use URL that is not
** saved.
**
** Use the --private option to push private branches to the
** remote repository.
**
** See also: clone, pull, sync, remote-url
*/
void push_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PUSH;
  process_sync_args(&configFlags, &syncFlags);
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
** Synchronize the local repository with a remote repository.  This is
** the equivalent of running both "push" and "pull" at the same time.
** Use the "-R REPO" or "--repository REPO" command-line options
** to specify an alternative repository file.
**
** See clone usage for possible URL formats.
**
** If the URL is not specified, then the URL from the most recent
** successful clone, push, pull, remote-url, or sync command is used.
**
** The URL specified normally becomes the new "remote-url" used for
** subsequent push, pull, and sync operations.  However, the "--once"
** command-line option makes the URL a one-time-use URL that is not
** saved.
**
** Use the --private option to sync private branches with the
** remote repository.
**
** See also:  clone, push, pull, remote-url
*/
void sync_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PUSH|SYNC_PULL;
  process_sync_args(&configFlags, &syncFlags);
  if( db_get_boolean("dont-push",0) ) syncFlags &= ~SYNC_PUSH;
  client_sync(syncFlags, configFlags, 0);
  if( (syncFlags & SYNC_PUSH)==0 ){
    fossil_warning("pull only: the 'dont-push' option is set");
  }
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
** See clone usage for possible URL formats.
**
** See also: clone, push, pull, sync
*/
void remote_url_cmd(void){
  char *zUrl;
  db_find_and_open_repository(0, 0);
  if( g.argc!=2 && g.argc!=3 ){
    usage("remote-url ?URL|off?");
  }
  if( g.argc==3 ){
    db_unset("last-sync-url", 0);
    db_unset("last-sync-pw", 0);
    if( is_false(g.argv[2]) ) return;
    url_parse(g.argv[2], URL_REMEMBER|URL_PROMPT_PW);
  }
  zUrl = db_get("last-sync-url", 0);
  if( zUrl==0 ){
    fossil_print("off\n");
    return;
  }else{
    url_parse(zUrl, 0);
    fossil_print("%s\n", g.urlCanonical);
  }
}
