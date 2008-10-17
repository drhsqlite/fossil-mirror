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
  if( g.fNoSync ){
    return;
  }
  if( db_get_boolean("autosync", 0)==0 ){
    return;
  }
  zUrl = db_get("last-sync-url", 0);
  if( zUrl==0 ){
    return;  /* No default server */
  }
  url_parse(zUrl);
  if( g.urlIsFile ){
    return;  /* Network sync only */
  }
  if( g.urlPort!=80 ){
    printf("Autosync:  http://%s:%d%s\n", g.urlName, g.urlPort, g.urlPath);
  }else{
    printf("Autosync:  http://%s%s\n", g.urlName, g.urlPath);
  }
  url_enable_proxy("via proxy: ");
  client_sync((flags & AUTOSYNC_PUSH)!=0, 1, 0, 0);
}

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
void process_sync_args(void){
  const char *zUrl = 0;
  int urlOptional = find_option("autourl",0,0)!=0;
  url_proxy_options();
  db_find_and_open_repository(1);
  if( g.argc==2 ){
    zUrl = db_get("last-sync-url", 0);
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
  }
  if( zUrl==0 ){
    if( urlOptional ) exit(0);
    usage("URL");
  }
  url_parse(zUrl);
  if( g.urlIsFile ){
    fossil_fatal("network sync only");
  }
  db_set("last-sync-url", zUrl, 0);
  user_select();
  if( g.argc==2 ){
    if( g.urlPort!=80 ){
      printf("Server:    http://%s:%d%s\n", g.urlName, g.urlPort, g.urlPath);
    }else{
      printf("Server:    http://%s%s\n", g.urlName, g.urlPath);
    }
  }
  url_enable_proxy("via proxy: ");
}

/*
** COMMAND: pull
**
** Usage: %fossil pull ?URL? ?-R|--respository REPOSITORY?
**
** Pull changes in a remote repository into the local repository.
** The repository is identified by the -R or --repository option.
** If there is no such option then the open repository is used.
** The URL of the remote server is specified on the command line
** If no URL is specified then the URL used by the most recent
** "pull", "push", or "sync" command is used.
**
** The URL is of the following form:
**
**      http://USER@HOST:PORT/PATH
**
** The "USER@" and ":PORT" substrings are optional.
** The "USER" substring specifies the login user.  You will be
** prompted for the password on the command-line.  The PORT
** specifies the TCP port of the server.  The default port is
** 80.
*/
void pull_cmd(void){
  process_sync_args();
  client_sync(0,1,0,0);
}

/*
** COMMAND: push
**
** Usage: %fossil push ?URL? ?-R|--repository REPOSITORY?
**
** Push changes in the local repository over into a remote repository.
** See the "pull" command for additional information.
*/
void push_cmd(void){
  process_sync_args();
  client_sync(1,0,0,0);
}


/*
** COMMAND: sync
**
** Usage: %fossil sync ?URL? ?-R|--repository REPOSITORY?
**
** Synchronize the local repository with a remote repository.  This is
** the equivalent of running both "push" and "pull" at the same time.
** See the "pull" command for additional information.
*/
void sync_cmd(void){
  process_sync_args();
  client_sync(1,1,0,0);
}
