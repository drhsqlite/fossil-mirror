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
** If there are public BLOBs that deltas from private BLOBs, then
** undeltify the public BLOBs so that the private BLOBs may be safely
** deleted.
*/
void fix_private_blob_dependencies(int showWarning){
  Bag toUndelta;
  Stmt q;
  int rid;

  /* Careful:  We are about to delete all BLOB entries that are private.
  ** So make sure that any no public BLOBs are deltas from a private BLOB.
  ** Otherwise after the deletion, we won't be able to recreate the public
  ** BLOBs.
  */
  db_prepare(&q,
    "SELECT "
    "   rid, (SELECT uuid FROM blob WHERE rid=delta.rid),"
    "   srcid, (SELECT uuid FROM blob WHERE rid=delta.srcid)"
    "  FROM delta"
    " WHERE srcid in private AND rid NOT IN private"
  );
  bag_init(&toUndelta);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    const char *zId = db_column_text(&q, 1);
    int srcid = db_column_int(&q, 2);
    const char *zSrc = db_column_text(&q, 3);
    if( showWarning ){
      fossil_warning(
        "public artifact %S (%d) is a delta from private artifact %S (%d)",
        zId, rid, zSrc, srcid
      );
    }
    bag_insert(&toUndelta, rid);
  }
  db_finalize(&q);
  while( (rid = bag_first(&toUndelta))>0 ){
    content_undelta(rid);
    bag_remove(&toUndelta, rid);
  }
  bag_clear(&toUndelta);
}

/*
** Delete all private content from a repository.
*/
void delete_private_content(void){
  fix_private_blob_dependencies(1);
  db_multi_exec(
    "DELETE FROM blob WHERE rid IN private;"
    "DELETE FROM delta WHERE rid IN private;"
    "DELETE FROM private;"
    "DROP TABLE IF EXISTS modreq;"
  );
}


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
**    --admin-user|-A USERNAME   Make USERNAME the administrator
**    --private                  Also clone private branches 
**    --ssl-identity=filename    Use the SSL identity if requested by the server
**    --ssh-fossil|-f /fossil    Use this path as remote fossil command
**    --ssh-command|-c 'command' Use this SSH command
**
** See also: init
*/
void clone_cmd(void){
  char *zPassword;
  const char *zDefaultUser;   /* Optional name of the default user */
  int nErr = 0;
  int bPrivate = 0;           /* Also clone private branches */

  if( find_option("private",0,0)!=0 ) bPrivate = SYNC_PRIVATE;
  clone_ssh_options();
  url_proxy_options();
  if( g.argc < 4 ){
    usage("?OPTIONS? FILE-OR-URL NEW-REPOSITORY");
  }
  db_open_config(0);
  if( file_size(g.argv[3])>0 ){
    fossil_panic("file already exists: %s", g.argv[3]);
  }

  zDefaultUser = find_option("admin-user","A",1);

  url_parse(g.argv[2], URL_PROMPT_PW|URL_ASK_REMEMBER_PW);
  if( g.urlIsFile ){
    file_copy(g.urlName, g.argv[3]);
    db_close(1);
    db_open_repository(g.argv[3]);
    db_record_repository_filename(g.argv[3]);
    url_remember();
    if( !bPrivate ) delete_private_content();
    shun_artifacts();
    db_create_default_users(1, zDefaultUser);
    if( zDefaultUser ){
      g.zLogin = zDefaultUser;
    }else{
      g.zLogin = db_text(0, "SELECT login FROM user WHERE cap LIKE '%%s%%'");
    }
    fossil_print("Repository cloned into %s\n", g.argv[3]);
  }else{
    db_create_repository(g.argv[3]);
    db_open_repository(g.argv[3]);
    db_begin_transaction();
    db_record_repository_filename(g.argv[3]);
    db_initial_setup(0, 0, zDefaultUser, 0);
    user_select();
    db_set("content-schema", CONTENT_SCHEMA, 0);
    db_set("aux-schema", AUX_SCHEMA, 0);
    url_remember();
    if( g.zSSLIdentity!=0 ){
      /* If the --ssl-identity option was specified, store it as a setting */
      Blob fn;
      blob_zero(&fn);
      file_canonical_name(g.zSSLIdentity, &fn, 0);
      db_set("ssl-identity", blob_str(&fn), 0);
      blob_reset(&fn);
    }
    db_multi_exec(
      "REPLACE INTO config(name,value,mtime)"
      " VALUES('server-code', lower(hex(randomblob(20))), now());"
    );
    url_enable_proxy(0);
    url_get_password_if_needed();
    g.xlinkClusterOnly = 1;
    nErr = client_sync(SYNC_CLONE | bPrivate,CONFIGSET_ALL,0);
    g.xlinkClusterOnly = 0;
    verify_cancel();
    db_end_transaction(0);
    db_close(1);
    if( nErr ){
      file_delete(g.argv[3]);
      fossil_fatal("server returned an error - clone aborted");
    }
    db_open_repository(g.argv[3]);
  }
  db_begin_transaction();
  fossil_print("Rebuilding repository meta-data...\n");
  rebuild_db(0, 1, 0);
  fossil_print("project-id: %s\n", db_get("project-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  fossil_print("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
  db_end_transaction(0);
}

/*
** Look for SSH clone command line options and setup in globals.
*/
void clone_ssh_options(void){
  const char *zSshFossilCmd;  /* Path to remote fossil command for SSH */
  const char *zSshCmd;        /* SSH command string */

  zSshFossilCmd = find_option("ssh-fossil","f",1);
  if( zSshFossilCmd && zSshFossilCmd[0] ){
    g.zSshFossilCmd = mprintf("%s", zSshFossilCmd);
  }
  zSshCmd = find_option("ssh-command","c",1);
  if( zSshCmd && zSshCmd[0] ){
    g.zSshCmd = mprintf("%s", zSshCmd);
  }
}

/*
** Set SSH options discovered in global variables (set from command line 
** options).  If not found, attempt to retrieve from database if present.
*/
void clone_ssh_db_options(void){
  if( g.zSshFossilCmd && g.zSshFossilCmd[0] ){
    db_set("ssh-fossil", g.zSshFossilCmd, 0);
  }else{
    g.zSshFossilCmd = db_get("ssh-fossil","fossil");
  }
  if( g.zSshCmd && g.zSshCmd[0] ){
    db_set("ssh-command", g.zSshCmd, 0);
  }
}
