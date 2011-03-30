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

#if INTERFACE
/*
** Flags used to determine which direction(s) an autosync goes in.
*/
#define AUTOSYNC_PUSH  1
#define AUTOSYNC_PULL  2

#endif /* INTERFACE */

/*
** If the respository is configured for autosyncing, then do an
** autosync.  This will be a pull if the argument is true or a push
** if the argument is false.
**
** Return the number of errors.
*/
int autosync(int flags){
  const char *zUrl;
  const char *zAutosync;
  const char *zPw;
  int rc;
  int configSync = 0;       /* configuration changes transferred */
  if( g.fNoSync ){
    return 0;
  }
  zAutosync = db_get("autosync", 0);
  if( zAutosync ){
    if( (flags & AUTOSYNC_PUSH)!=0 && memcmp(zAutosync,"pull",4)==0 ){
      return 0;   /* Do not auto-push when autosync=pullonly */
    }
    if( is_false(zAutosync) ){
      return 0;   /* Autosync is completely off */
    }
  }else{
    /* Autosync defaults on.  To make it default off, "return" here. */
  }
  zUrl = db_get("last-sync-url", 0);
  if( zUrl==0 ){
    return 0;  /* No default server */
  }
  zPw = unobscure(db_get("last-sync-pw", 0));
  url_parse(zUrl);
  if( g.urlUser!=0 && g.urlPasswd==0 ){
    g.urlPasswd = mprintf("%s", zPw);
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
  printf("Autosync:  %s\n", g.urlCanonical);
  url_enable_proxy("via proxy: ");
  rc = client_sync((flags & AUTOSYNC_PUSH)!=0, 1, 0, 0, configSync, 0);
  if( rc ) fossil_warning("Autosync failed");
  return rc;
}

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
static void process_sync_args(int *pConfigSync, int *pPrivate){
  const char *zUrl = 0;
  const char *zPw = 0;
  int configSync = 0;
  int urlOptional = find_option("autourl",0,0)!=0;
  g.dontKeepUrl = find_option("once",0,0)!=0;
  *pPrivate = find_option("private",0,0)!=0;
  g.urlCertGroup = find_option("certgroup",0,1);
  url_proxy_options();
  db_find_and_open_repository(0, 0);
  db_open_config(0);
  if( g.argc==2 ){
    zUrl = db_get("last-sync-url", 0);
    zPw = unobscure(db_get("last-sync-pw", 0));
    if( db_get_boolean("auto-sync",1) ) configSync = CONFIGSET_SHUN;
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
  }
  if( zUrl==0 ){
    if( urlOptional ) fossil_exit(0);
    usage("URL");
  }
  url_parse(zUrl);
  if( g.urlUser!=0 && g.urlPasswd==0 ){
    if( zPw==0 ){
      url_prompt_for_password();
    }else{
      g.urlPasswd = mprintf("%s", zPw);
    }
  }
  if( !g.dontKeepUrl ){
    db_set("last-sync-url", g.urlCanonical, 0);
    if( g.urlPasswd ) db_set("last-sync-pw", obscure(g.urlPasswd), 0);
  }
  user_select();
  if( g.argc==2 ){
    printf("Server:    %s\n", g.urlCanonical);
  }
  url_enable_proxy("via proxy: ");
  *pConfigSync = configSync;
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
** Use the "--certgroup NAME" option to specify the name of the
** certificate/key bundle to use for https connections. If this option
** is not specified, a cached value associated with the URL will be
** used if it exists.
**
** See also: cert, clone, push, sync, remote-url
*/
void pull_cmd(void){
  int syncFlags;
  int bPrivate;
  process_sync_args(&syncFlags, &bPrivate);
  client_sync(0,1,0,bPrivate,syncFlags,0);
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
** Use the "--certgroup NAME" option to specify the name of the
** certificate/key bundle to use for https connections. If this option
** is not specified, a cached value associated with the URL will be
** used if it exists.
**
** See also: cert, clone, pull, sync, remote-url
*/
void push_cmd(void){
  int syncFlags;
  int bPrivate;
  process_sync_args(&syncFlags, &bPrivate);
  client_sync(1,0,0,bPrivate,0,0);
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
** If a user-id and password are required, specify them as follows:
**
**     http://userid:password@www.domain.com:1234/path
**
** If the URL is not specified, then the URL from the most recent successful
** clone, push, pull, remote-url, or sync command is used.
**
** The URL specified normally becomes the new "remote-url" used for
** subsequent push, pull, and sync operations.  However, the "--once"
** command-line option makes the URL a one-time-use URL that is not
** saved.
**
** Use the --private option to sync private branches with the
** remote repository.
**
** Use the "--certgroup NAME" option to specify the name of the
** certificate/key bundle to use for https connections. If this option
** is not specified, a cached value associated with the URL will be
** used if it exists.
**
** See also: cert, clone, push, pull, remote-url
*/
void sync_cmd(void){
  int syncFlags;
  int bPrivate;
  process_sync_args(&syncFlags, &bPrivate);
  client_sync(1,1,0,bPrivate,syncFlags,0);
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
** See also: clone, push, pull, sync
*/
void remote_url_cmd(void){
  char *zUrl;
  db_find_and_open_repository(0, 0);
  if( g.argc!=2 && g.argc!=3 ){
    usage("remote-url ?URL|off?");
  }
  if( g.argc==3 ){
    if( fossil_strcmp(g.argv[2],"off")==0 ){
      db_unset("last-sync-url", 0);
      db_unset("last-sync-pw", 0);
    }else{
      url_parse(g.argv[2]);
      if( g.urlUser && g.urlPasswd==0 ){
        url_prompt_for_password();
      }
      db_set("last-sync-url", g.urlCanonical, 0);
      if( g.urlPasswd ){
        db_set("last-sync-pw", obscure(g.urlPasswd), 0);
      }else{
        db_unset("last-sync-pw", 0);
      }
    }
  }
  zUrl = db_get("last-sync-url", 0);
  if( zUrl==0 ){
    printf("off\n");
    return;
  }else{
    url_parse(zUrl);
    printf("%s\n", g.urlCanonical);
  }
}
