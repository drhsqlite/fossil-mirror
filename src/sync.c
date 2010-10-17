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
*/
void autosync(int flags){
  const char *zUrl;
  const char *zAutosync;
  const char *zPw;
  int configSync = 0;       /* configuration changes transferred */
  if( g.fNoSync ){
    return;
  }
  zAutosync = db_get("autosync", 0);
  if( zAutosync ){
    if( (flags & AUTOSYNC_PUSH)!=0 && memcmp(zAutosync,"pull",4)==0 ){
      return;   /* Do not auto-push when autosync=pullonly */
    }
    if( is_false(zAutosync) ){
      return;   /* Autosync is completely off */
    }
  }else{
    /* Autosync defaults on.  To make it default off, "return" here. */
  }
  zUrl = db_get("last-sync-url", 0);
  if( zUrl==0 ){
    return;  /* No default server */
  }
  zPw = unobscure(db_get("last-sync-pw", 0));
  url_parse(zUrl);
  if( g.urlUser!=0 && g.urlPasswd==0 ){
    g.urlPasswd = mprintf("%s", zPw);
  }
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
  printf("Autosync:  %s\n", g.urlCanonical);
  url_enable_proxy("via proxy: ");
  client_sync((flags & AUTOSYNC_PUSH)!=0, 1, 0, configSync, 0);
}

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
static int process_sync_args(void){
  const char *zUrl = 0;
  const char *zPw = 0;
  int configSync = 0;
  int urlOptional = find_option("autourl",0,0)!=0;
  g.dontKeepUrl = find_option("once",0,0)!=0;
  url_proxy_options();
  db_find_and_open_repository(1);
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
  if( !g.dontKeepUrl ){
    db_set("last-sync-url", g.urlCanonical, 0);
    if( g.urlPasswd ) db_set("last-sync-pw", obscure(g.urlPasswd), 0);
  }
  if( g.urlUser!=0 && g.urlPasswd==0 ){
    if( zPw==0 ){
      url_prompt_for_password();
    }else{
      g.urlPasswd = mprintf("%s", zPw);
    }
  }
  user_select();
  if( g.argc==2 ){
    printf("Server:    %s\n", g.urlCanonical);
  }
  url_enable_proxy("via proxy: ");
  return configSync;
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
** <a>clone</a>, <a>push</a>, <a>pull</a>, <a>remote-url</a>, or <a>sync</a> command
** is used.
**
** The URL specified normally becomes the new "remote-url" used for
** subsequent push, pull, and sync operations.  However, the "--once"
** command-line option makes the URL a one-time-use URL that is not
** saved.
**
** See also: <a>clone</a>, <a>push</a>, <a>sync</a>, <a>remote-url</a>
*/
void pull_cmd(void){
  int syncFlags = process_sync_args();
  client_sync(0,1,0,syncFlags,0);
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
** <a>clone</a>, <a>push</a>, <a>pull</a>, <a>remote-url</a>, or <a>sync</a> command
** is used.
**
** The URL specified normally becomes the new "remote-url" used for
** subsequent <a>push</a>, <a>pull</a>, and <a>sync</a> operations.  However,
** the "--once" command-line option makes the URL a one-time-use URL
** that is not saved.
**
** If configured (<a>setting</a> push-hook-..), the push hook command will be executed
** after pushing files to the server.
**
** See also: <a>clone</a>, <a>pull</a>, <a>sync</a>, <a>remote-url</a>
*/
void push_cmd(void){
  process_sync_args();
  client_sync(1,0,0,0,0);
}


/*
** COMMAND: sync
**
** Usage: %fossil sync ?URL? ?options?
**
** Synchronize the local repository with a remote repository.  This is
** the equivalent of running both <a>push</a> and <a>pull</a> at the same
** time. Use the "-R REPO" or "--repository REPO" command-line options
** to specify an alternative repository file.
**
** If a user-id and password are required, specify them as follows:
**
**     http://userid:password@www.domain.com:1234/path
**
** If the URL is not specified, then the URL from the most recent
** successful <a>clone</a>, <a>push</a>, <a>pull</a>, <a>remote-url</a>, or <a>sync</a>
** command is used.
**
** The URL specified normally becomes the new "remote-url" used for
** subsequent <a>push</a>, <a>pull</a>, and <a>sync</a> operations.  However,
** the "--once" command-line option makes the URL a one-time-use URL
** that is not saved.
**
** See also:  <a>clone</a>, <a>push</a>, <a>pull</a>, <a>remote-url</a>
*/
void sync_cmd(void){
  int syncFlags = process_sync_args();
  client_sync(1,1,0,syncFlags,0);
}

/*
** COMMAND: remote-url
**
** Usage: %fossil remote-url ?URL|off?
**
** Query and/or change the default server URL used by the <a>pull</a>,
** <a>push</a>, and <a>sync</a> commands.
**
** The remote-url is set automatically by a <a>clone</a> command or by any
** <a>sync</a>, <a>push</a>, or <a>pull</a> command that specifies an explicit
** URL. The default remote-url is used by auto-syncing and by
** <a>sync</a>, <a>push</a>, <a>pull</a> that omit the server URL.
**
** See also: <a>clone</a>, <a>push</a>, <a>pull</a>, <a>sync</a>
*/
void remote_url_cmd(void){
  char *zUrl;
  db_find_and_open_repository(1);
  if( g.argc!=2 && g.argc!=3 ){
    usage("remote-url ?URL|off?");
  }
  if( g.argc==3 ){
    if( strcmp(g.argv[2],"off")==0 ){
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
