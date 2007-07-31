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

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
static void process_sync_args(void){
  const char *zUrl = 0;
  db_find_and_open_repository();
  if( g.argc==2 ){
    zUrl = db_get("last-sync-url", 0);
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
  }
  if( zUrl==0 ){
    usage("URL");
  }
  url_parse(zUrl);
  if( g.urlIsFile ){
    fossil_fatal("network sync only");
  }
  db_set("last-sync-url", zUrl);
  user_select();
  if( g.argc==2 ){
    if( g.urlPort!=80 ){
      printf("Server:    http://%s:%d%s\n", g.urlName, g.urlPort, g.urlPath);
    }else{
      printf("Server:    http://%s%s\n", g.urlName, g.urlPath);
    }
  }
}

/*
** COMMAND: pull
**
** Pull changes in a remote repository into the local repository
*/
void pull_cmd(void){
  process_sync_args();
  client_sync(0,1,0);
}

/*
** COMMAND: push
**
** Push changes in the local repository over into a remote repository
*/
void push_cmd(void){
  process_sync_args();
  client_sync(1,0,0);
}


/*
** COMMAND: sync
**
** Synchronize the local repository with a remote repository
*/
void sync_cmd(void){
  process_sync_args();
  client_sync(1,1,0);
}
