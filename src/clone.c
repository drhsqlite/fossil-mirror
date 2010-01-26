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
** This file contains code used to clone a repository
*/
#include "config.h"
#include "clone.h"
#include <assert.h>



/*
** COMMAND: clone
**
** Usage: %fossil clone URL FILENAME
**
** Make a clone of a repository specified by URL in the local
** file named FILENAME.  
*/
void clone_cmd(void){
  char *zPassword;
  url_proxy_options();
  if( g.argc!=4 ){
    usage("FILE-OR-URL NEW-REPOSITORY");
  }
  db_open_config(0);
  if( file_size(g.argv[3])>0 ){
    fossil_panic("file already exists: %s", g.argv[3]);
  }
  url_parse(g.argv[2]);
  if( g.urlIsFile ){
    file_copy(g.urlName, g.argv[3]);
    db_close();
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
      db_create_default_users(1,0);
    }
    printf("Repository cloned into %s\n", g.argv[3]);
  }else{
    db_create_repository(g.argv[3]);
    db_open_repository(g.argv[3]);
    db_begin_transaction();
    db_record_repository_filename(g.argv[3]);
    db_initial_setup(0, 0, 0);
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
    client_sync(0,0,1,CONFIGSET_ALL,0);
    g.xlinkClusterOnly = 0;
    verify_cancel();
    db_end_transaction(0);
    db_close();
    db_open_repository(g.argv[3]);
  }
  db_begin_transaction();
  printf("Rebuilding repository meta-data...\n");
  rebuild_db(0, 1);
  printf("project-id: %s\n", db_get("project-code", 0));
  printf("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  printf("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
  db_end_transaction(0);
}
