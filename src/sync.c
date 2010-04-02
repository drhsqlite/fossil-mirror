/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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
  zPw = db_get("last-sync-pw", 0);
  url_parse(zUrl);
  if( g.urlUser!=0 && g.urlPasswd==0 ){
    g.urlPasswd = mprintf("%s", zPw);
  }
  printf("Autosync:  %s\n", g.urlCanonical);
  url_enable_proxy("via proxy: ");
  client_sync((flags & AUTOSYNC_PUSH)!=0, 1, 0, 0, 0);
}

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
void process_sync_args(void){
  const char *zUrl = 0;
  const char *zPw = 0;
  int urlOptional = find_option("autourl",0,0)!=0;
  g.dontKeepUrl = find_option("once",0,0)!=0;
  url_proxy_options();
  db_find_and_open_repository(1);
  db_open_config(0);
  if( g.argc==2 ){
    zUrl = db_get("last-sync-url", 0);
    zPw = db_get("last-sync-pw", 0);
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
  }
  if( zUrl==0 ){
    if( urlOptional ) exit(0);
    usage("URL");
  }
  url_parse(zUrl);
  if( !g.dontKeepUrl ){
    db_set("last-sync-url", g.urlCanonical, 0);
    if( g.urlPasswd ) db_set("last-sync-pw", g.urlPasswd, 0);
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
** See also: clone, push, sync, remote-url
*/
void pull_cmd(void){
  process_sync_args();
  client_sync(0,1,0,0,0);
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
** See also: clone, pull, sync, remote-url
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
** See also:  clone, push, pull, remote-url
*/
void sync_cmd(void){
  process_sync_args();
  client_sync(1,1,0,0,0);
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
        db_set("last-sync-pw", g.urlPasswd, 0);
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
