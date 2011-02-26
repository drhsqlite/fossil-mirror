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
** This file contains code used to clone a repository
*/
#include "config.h"
#include "clone.h"
#include <assert.h>



/*
** COMMAND: clone
**
** Usage: %fossil clone ?OPTIONS? URL FILENAME
**
** Make a clone of a repository specified by URL in the local
** file named FILENAME.  
**
** By default, your current login name is used to create the default
** admin user. This can be overridden using the -A|--admin-user
** parameter.
**
** Options:
**
**    --admin-user|-A USERNAME    Make USERNAME the administrator
**    --private                   Also clone private branches 
**
*/
void clone_cmd(void){
  char *zPassword;
  const char *zDefaultUser;   /* Optional name of the default user */
  int nErr = 0;
  int bPrivate;               /* Also clone private branches */

  bPrivate = find_option("private",0,0)!=0;
  url_proxy_options();
  if( g.argc < 4 ){
    usage("?OPTIONS? FILE-OR-URL NEW-REPOSITORY");
  }
  db_open_config(0);
  if( file_size(g.argv[3])>0 ){
    fossil_panic("file already exists: %s", g.argv[3]);
  }

  zDefaultUser = find_option("admin-user","A",1);

  url_parse(g.argv[2]);
  if( g.urlIsFile ){
    file_copy(g.urlName, g.argv[3]);
    db_close(1);
    db_open_repository(g.argv[3]);
    db_record_repository_filename(g.argv[3]);
    db_multi_exec(
      "REPLACE INTO config(name,value)"
      " VALUES('server-code', lower(hex(randomblob(20))));"
      "REPLACE INTO config(name,value)"
      " VALUES('last-sync-url', '%q');",
      g.urlCanonical
    );
    db_multi_exec(
       "DELETE FROM blob WHERE rid IN private;"
       "DELETE FROM delta wHERE rid IN private;"
       "DELETE FROM private;"
    );
    shun_artifacts();
    g.zLogin = db_text(0, "SELECT login FROM user WHERE cap LIKE '%%s%%'");
    if( g.zLogin==0 ){
      db_create_default_users(1,zDefaultUser);
    }
    printf("Repository cloned into %s\n", g.argv[3]);
  }else{
    db_create_repository(g.argv[3]);
    db_open_repository(g.argv[3]);
    db_begin_transaction();
    db_record_repository_filename(g.argv[3]);
    db_initial_setup(0, zDefaultUser, 0);
    user_select();
    db_set("content-schema", CONTENT_SCHEMA, 0);
    db_set("aux-schema", AUX_SCHEMA, 0);
    db_set("last-sync-url", g.argv[2], 0);
    db_multi_exec(
      "REPLACE INTO config(name,value)"
      " VALUES('server-code', lower(hex(randomblob(20))));"
    );
    url_enable_proxy(0);
    g.xlinkClusterOnly = 1;
    nErr = client_sync(0,0,1,bPrivate,CONFIGSET_ALL,0);
    g.xlinkClusterOnly = 0;
    verify_cancel();
    db_end_transaction(0);
    db_close(1);
    if( nErr ){
      unlink(g.argv[3]);
      fossil_fatal("server returned an error - clone aborted");
    }
    db_open_repository(g.argv[3]);
  }
  db_begin_transaction();
  printf("Rebuilding repository meta-data...\n");
  rebuild_db(0, 1, 0);
  printf("project-id: %s\n", db_get("project-code", 0));
  printf("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  printf("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
  db_end_transaction(0);
}
