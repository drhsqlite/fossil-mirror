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
** Usage: %fossil clone ?OPTIONS? URI FILENAME
**
** Make a clone of a repository specified by URI in the local
** file named FILENAME.
**
** URI may be one of the following form: ([...] mean optional)
**   HTTP/HTTPS protocol:
**     http[s]://[userid[:password]@]host[:port][/path]
**
**   SSH protocol:
**     ssh://[userid@]host[:port]/path/to/repo.fossil\\
**     [?fossil=path/to/fossil.exe]
**
**   Filesystem:
**     [file://]path/to/repo.fossil
**
** Note 1: For ssh and filesystem, path must have an extra leading
**         '/' to use an absolute path.
**
** Note 2: Use %HH escapes for special characters in the userid and
**         password.  For example "%40" in place of "@", "%2f" in place
**         of "/", and "%3a" in place of ":".
**
** By default, your current login name is used to create the default
** admin user. This can be overridden using the -A|--admin-user
** parameter.
**
** Options:
**    --admin-user|-A USERNAME   Make USERNAME the administrator
**    --once                     Don't remember the URI.
**    --private                  Also clone private branches
**    --ssl-identity FILENAME    Use the SSL identity if requested by the server
**    --ssh-command|-c SSH       Use SSH as the "ssh" command
**    --httpauth|-B USER:PASS    Add HTTP Basic Authorization to requests
**    -u|--unversioned           Also sync unversioned content
**    -v|--verbose               Show more statistics in output
**
** See also: init
*/
void clone_cmd(void){
  char *zPassword;
  const char *zDefaultUser;   /* Optional name of the default user */
  const char *zHttpAuth;      /* HTTP Authorization user:pass information */
  int nErr = 0;
  int urlFlags = URL_PROMPT_PW | URL_REMEMBER;
  int syncFlags = SYNC_CLONE;

  /* Also clone private branches */
  if( find_option("private",0,0)!=0 ) syncFlags |= SYNC_PRIVATE;
  if( find_option("once",0,0)!=0) urlFlags &= ~URL_REMEMBER;
  if( find_option("verbose","v",0)!=0) syncFlags |= SYNC_VERBOSE;
  if( find_option("unversioned","u",0)!=0 ) syncFlags |= SYNC_UNVERSIONED;
  zHttpAuth = find_option("httpauth","B",1);
  zDefaultUser = find_option("admin-user","A",1);
  clone_ssh_find_options();
  url_proxy_options();

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc < 4 ){
    usage("?OPTIONS? FILE-OR-URL NEW-REPOSITORY");
  }
  db_open_config(0, 0);
  if( -1 != file_size(g.argv[3]) ){
    fossil_fatal("file already exists: %s", g.argv[3]);
  }

  url_parse(g.argv[2], urlFlags);
  if( zDefaultUser==0 && g.url.user!=0 ) zDefaultUser = g.url.user;
  if( g.url.isFile ){
    file_copy(g.url.name, g.argv[3]);
    db_close(1);
    db_open_repository(g.argv[3]);
    db_record_repository_filename(g.argv[3]);
    url_remember();
    if( !(syncFlags & SYNC_PRIVATE) ) delete_private_content();
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
    db_initial_setup(0, 0, zDefaultUser);
    user_select();
    db_set("content-schema", CONTENT_SCHEMA, 0);
    db_set("aux-schema", AUX_SCHEMA_MAX, 0);
    db_set("rebuilt", get_version(), 0);
    db_unset("hash-policy", 0);
    remember_or_get_http_auth(zHttpAuth, urlFlags & URL_REMEMBER, g.argv[2]);
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
      "DELETE FROM config WHERE name='project-code';"
    );
    url_enable_proxy(0);
    clone_ssh_db_set_options();
    url_get_password_if_needed();
    g.xlinkClusterOnly = 1;
    nErr = client_sync(syncFlags,CONFIGSET_ALL,0);
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
  fossil_print("Extra delta compression... "); fflush(stdout);
  extra_deltification();
  db_end_transaction(0);
  fossil_print("\nVacuuming the database... "); fflush(stdout);
  if( db_int(0, "PRAGMA page_count")>1000
   && db_int(0, "PRAGMA page_size")<8192 ){
     db_multi_exec("PRAGMA page_size=8192;");
  }
  db_multi_exec("VACUUM");
  fossil_print("\nproject-id: %s\n", db_get("project-code", 0));
  fossil_print("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  fossil_print("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
}

/*
** If user chooses to use HTTP Authentication over unencrypted HTTP,
** remember decision.  Otherwise, if the URL is being changed and no
** preference has been indicated, err on the safe side and revert the
** decision. Set the global preference if the URL is not being changed.
*/
void remember_or_get_http_auth(
  const char *zHttpAuth,  /* Credentials in the form "user:password" */
  int fRemember,          /* True to remember credentials for later reuse */
  const char *zUrl        /* URL for which these credentials apply */
){
  char *zKey = mprintf("http-auth:%s", g.url.canonical);
  if( zHttpAuth && zHttpAuth[0] ){
    g.zHttpAuth = mprintf("%s", zHttpAuth);
  }
  if( fRemember ){
    if( g.zHttpAuth && g.zHttpAuth[0] ){
      set_httpauth(g.zHttpAuth);
    }else if( zUrl && zUrl[0] ){
      db_unset(zKey, 0);
    }else{
      g.zHttpAuth = get_httpauth();
    }
  }else if( g.zHttpAuth==0 && zUrl==0 ){
    g.zHttpAuth = get_httpauth();
  }
  free(zKey);
}

/*
** Get the HTTP Authorization preference from db.
*/
char *get_httpauth(void){
  char *zKey = mprintf("http-auth:%s", g.url.canonical);
  char * rc = unobscure(db_get(zKey, 0));
  free(zKey);
  return rc;
}

/*
** Set the HTTP Authorization preference in db.
*/
void set_httpauth(const char *zHttpAuth){
  char *zKey = mprintf("http-auth:%s", g.url.canonical);
  db_set(zKey, obscure(zHttpAuth), 0);
  free(zKey);
}

/*
** Look for SSH clone command line options and setup in globals.
*/
void clone_ssh_find_options(void){
  const char *zSshCmd;        /* SSH command string */

  zSshCmd = find_option("ssh-command","c",1);
  if( zSshCmd && zSshCmd[0] ){
    g.zSshCmd = mprintf("%s", zSshCmd);
  }
}

/*
** Set SSH options discovered in global variables (set from command line
** options).
*/
void clone_ssh_db_set_options(void){
  if( g.zSshCmd && g.zSshCmd[0] ){
    db_set("ssh-command", g.zSshCmd, 0);
  }
}

/*
** WEBPAGE: download
**
** Provide a simple page that enables newbies to download the latest tarball or
** ZIP archive, and provides instructions on how to clone.
*/
void download_page(void){
  login_check_credentials();
  style_header("Download Page");
  if( !g.perm.Zip ){
    @ <p>Bummer.  You do not have permission to download.
    if( g.zLogin==0 || g.zLogin[0]==0 ){
      @ Maybe it would work better if you
      @ <a href="../login">logged in</a>.
    }else{
      @ Contact the site administrator and ask them to give
      @ you "Download Zip" privileges.
    }
  }else{
    const char *zDLTag = db_get("download-tag","trunk");
    const char *zNm = db_get("short-project-name","download");
    char *zUrl = href("%R/zip/%t.zip?uuid=%t", zNm, zDLTag);
    @ <p>ZIP Archive: %z(zUrl)%h(zNm).zip</a>
    zUrl = href("%R/tarball/%t.tar.gz?uuid=%t", zNm, zDLTag);
    @ <p>Tarball: %z(zUrl)%h(zNm).tar.gz</a>
  }
  if( !g.perm.Clone ){
    @ <p>You are not authorized to clone this repository.
    if( g.zLogin==0 || g.zLogin[0]==0 ){
      @ Maybe you would be able to clone if you
      @ <a href="../login">logged in</a>.
    }else{
      @ Contact the site administrator and ask them to give
      @ you "Clone" privileges in order to clone.
    }
  }else{
    const char *zNm = db_get("short-project-name","clone");
    @ <p>Clone the repository using this command:
    @ <blockquote><pre>
    @ fossil  clone  %s(g.zBaseURL)  %h(zNm).fossil
    @ </pre></blockquote>
  }
  style_footer();
}
