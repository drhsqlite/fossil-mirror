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
** Usage: %fossil clone ?OPTIONS? URI ?FILENAME?
**
** Make a clone of a repository specified by URI in the local
** file named FILENAME.  If FILENAME is omitted, then an appropriate
** filename is deduced from last element of the path in the URL.
**
** URI may be one of the following forms ([...] denotes optional elements):
**
**  * HTTP/HTTPS protocol:
**
**      http[s]://[userid[:password]@]host[:port][/path]
**
**  * SSH protocol:
**
**      ssh://[userid@]host[:port]/path/to/repo.fossil[?fossil=path/fossil.exe]
**
**  * Filesystem:
**
**      [file://]path/to/repo.fossil
**
** For ssh and filesystem, path must have an extra leading
** '/' to use an absolute path.
**
** Use %HH escapes for special characters in the userid and
** password.  For example "%40" in place of "@", "%2f" in place
** of "/", and "%3a" in place of ":".
**
** Note that in Fossil (in contrast to some other DVCSes) a repository
** is distinct from a check-out.  Cloning a repository is not the same thing
** as opening a repository.  This command always clones the repository.  This
** command might also open the repository, but only if the --no-open option
** is omitted and either the --workdir option is included or the FILENAME
** argument is omitted.  Use the separate [[open]] command to open a
** repository that was previously cloned and already exists on the
** local machine.
**
** By default, the current login name is used to create the default
** admin user for the new clone. This can be overridden using
** the -A|--admin-user parameter.
**
** Options:
**    -A|--admin-user USERNAME   Make USERNAME the administrator
**    -B|--httpauth USER:PASS    Add HTTP Basic Authorization to requests
**    --nested                   Allow opening a repository inside an opened
**                               check-out
**    --nocompress               Omit extra delta compression
**    --no-open                  Clone only.  Do not open a check-out.
**    --once                     Don't remember the URI.
**    --private                  Also clone private branches
**    --proxy PROXY              Use the specified HTTP proxy
**    --save-http-password       Remember the HTTP password without asking
**    -c|--ssh-command SSH       Use SSH as the "ssh" command
**    --ssl-identity FILENAME    Use the SSL identity if requested by the server
**    --transport-command CMD    Use CMD to move messages to the server and back
**    -u|--unversioned           Also sync unversioned content
**    -v|--verbose               Show more statistics in output
**    --workdir DIR              Also open a check-out in DIR
**    --xverbose                 Extra debugging output
**
** See also: [[init]], [[open]]
*/
void clone_cmd(void){
  char *zPassword;
  const char *zDefaultUser;   /* Optional name of the default user */
  const char *zHttpAuth;      /* HTTP Authorization user:pass information */
  int nErr = 0;
  int urlFlags = URL_PROMPT_PW | URL_REMEMBER;
  int syncFlags = SYNC_CLONE;
  int noCompress = find_option("nocompress",0,0)!=0;
  int noOpen = find_option("no-open",0,0)!=0;
  int allowNested = find_option("nested",0,0)!=0; /* Used by open */
  const char *zRepo = 0;      /* Name of the new local repository file */
  const char *zWorkDir = 0;   /* Open in this directory, if not zero */


  /* Also clone private branches */
  if( find_option("private",0,0)!=0 ) syncFlags |= SYNC_PRIVATE;
  if( find_option("once",0,0)!=0) urlFlags &= ~URL_REMEMBER;
  if( find_option("save-http-password",0,0)!=0 ){
    urlFlags &= ~URL_PROMPT_PW;
    urlFlags |= URL_REMEMBER_PW;
  }
  if( find_option("verbose","v",0)!=0) syncFlags |= SYNC_VERBOSE;
  if( find_option("xverbose",0,0)!=0) syncFlags |= SYNC_XVERBOSE;
  if( find_option("unversioned","u",0)!=0 ){
    syncFlags |= SYNC_UNVERSIONED;
    if( syncFlags & SYNC_VERBOSE ){
      syncFlags |= SYNC_UV_TRACE;
    }
  }
  zHttpAuth = find_option("httpauth","B",1);
  zDefaultUser = find_option("admin-user","A",1);
  zWorkDir = find_option("workdir", 0, 1);
  clone_ssh_find_options();
  url_proxy_options();
  g.zHttpCmd = find_option("transport-command",0,1);

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc < 3 ){
    usage("?OPTIONS? FILE-OR-URL ?NEW-REPOSITORY?");
  }
  db_open_config(0, 0);
  if( g.argc==4 ){
    zRepo = g.argv[3];
  }else{
    char *zBase = url_to_repo_basename(g.argv[2]);
    if( zBase==0 ){
      fossil_fatal(
        "unable to guess a repository name from the url \"%s\".\n"
        "give the repository filename as an additional argument.",
        g.argv[2]);
    }
    zRepo = mprintf("./%s.fossil", zBase);
    if( zWorkDir==0 ){
      zWorkDir = mprintf("./%s", zBase);
    }
    fossil_free(zBase);
  }
  if( -1 != file_size(zRepo, ExtFILE) ){
    fossil_fatal("file already exists: %s", zRepo);
  }
  /* Fail before clone if open will fail because inside an open check-out */
  if( zWorkDir!=0 && zWorkDir[0]!=0 && !noOpen ){
    if( db_open_local_v2(0, allowNested) ){
      fossil_fatal("there is already an open tree at %s", g.zLocalRoot);
    }
  }
  url_parse(g.argv[2], urlFlags);
  if( zDefaultUser==0 && g.url.user!=0 ) zDefaultUser = g.url.user;
  if( g.url.isFile ){
    file_copy(g.url.name, zRepo);
    db_close(1);
    db_open_repository(zRepo);
    db_open_config(1,0);
    db_record_repository_filename(zRepo);
    url_remember();
    if( !(syncFlags & SYNC_PRIVATE) ) delete_private_content();
    shun_artifacts();
    db_create_default_users(1, zDefaultUser);
    if( zDefaultUser ){
      g.zLogin = zDefaultUser;
    }else{
      g.zLogin = db_text(0, "SELECT login FROM user WHERE cap LIKE '%%s%%'");
    }
    fossil_print("Repository cloned into %s\n", zRepo);
  }else{
    db_close_config();
    db_create_repository(zRepo);
    db_open_repository(zRepo);
    db_open_config(0,0);
    db_begin_transaction();
    db_record_repository_filename(zRepo);
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
      db_unprotect(PROTECT_ALL);
      db_set("ssl-identity", blob_str(&fn), 0);
      db_protect_pop();
      blob_reset(&fn);
    }
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "REPLACE INTO config(name,value,mtime)"
      " VALUES('server-code', lower(hex(randomblob(20))), now());"
      "DELETE FROM config WHERE name='project-code';"
    );
    db_protect_pop();
    url_enable_proxy(0);
    clone_ssh_db_set_options();
    url_get_password_if_needed();
    g.xlinkClusterOnly = 1;
    nErr = client_sync(syncFlags,CONFIGSET_ALL,0,0,0);
    g.xlinkClusterOnly = 0;
    verify_cancel();
    db_end_transaction(0);
    db_close(1);
    if( nErr ){
      file_delete(zRepo);
      if( g.fHttpTrace ){
        fossil_fatal(
          "server returned an error - clone aborted\n\n%s",
          http_last_trace_reply()
        );
      }else{
        fossil_fatal(
          "server returned an error - clone aborted\n"
          "Rerun using --httptrace for more detail"
        );
      }
    }
    db_open_repository(zRepo);
  }
  db_begin_transaction();
  if( db_exists("SELECT 1 FROM delta WHERE srcId IN phantom") ){
    fossil_fatal("there are unresolved deltas -"
                 " the clone is probably incomplete and unusable.");
  }
  fossil_print("Rebuilding repository meta-data...\n");
  rebuild_db(1, 0);
  if( !noCompress ){
    int nDelta = 0;
    i64 nByte;
    fossil_print("Extra delta compression... "); fflush(stdout);
    nByte = extra_deltification(&nDelta);
    if( nDelta==1 ){
      fossil_print("1 delta saves %,lld bytes\n", nByte);
    }else if( nDelta>1 ){
      fossil_print("%d deltas save %,lld bytes\n", nDelta, nByte);
    }else{
      fossil_print("none found\n");
    }
  }
  db_end_transaction(0);
  fossil_print("Vacuuming the database... "); fflush(stdout);
  if( db_int(0, "PRAGMA page_count")>1000
   && db_int(0, "PRAGMA page_size")<8192 ){
     db_multi_exec("PRAGMA page_size=8192;");
  }
  db_unprotect(PROTECT_ALL);
  db_multi_exec("VACUUM");
  db_protect_pop();
  fossil_print("\nproject-id: %s\n", db_get("project-code", 0));
  fossil_print("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  fossil_print("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
  hash_user_password(g.zLogin);
  if( zWorkDir!=0 && zWorkDir[0]!=0 && !noOpen ){
    Blob cmd;
    fossil_print("opening the new %s repository in directory %s...\n",
       zRepo, zWorkDir);
    blob_init(&cmd, 0, 0);
    blob_append_escaped_arg(&cmd, g.nameOfExe, 1);
    blob_append(&cmd, " open ", -1);
    blob_append_escaped_arg(&cmd, zRepo, 1);
    blob_append(&cmd, " --nosync --workdir ", -1);
    blob_append_escaped_arg(&cmd, zWorkDir, 1);
    if( allowNested ){
      blob_append(&cmd, " --nested", -1);
    }
    fossil_system(blob_str(&cmd));
    blob_reset(&cmd);
  }
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
  if( zHttpAuth && zHttpAuth[0] ){
    g.zHttpAuth = mprintf("%s", zHttpAuth);
  }
  if( fRemember ){
    if( g.zHttpAuth && g.zHttpAuth[0] ){
      set_httpauth(g.zHttpAuth);
    }else if( zUrl && zUrl[0] ){
      db_unset_mprintf(0, "http-auth:%s", g.url.canonical);
    }else{
      g.zHttpAuth = get_httpauth();
    }
  }else if( g.zHttpAuth==0 && zUrl==0 ){
    g.zHttpAuth = get_httpauth();
  }
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
  db_set_mprintf(obscure(zHttpAuth), 0, "http-auth:%s", g.url.canonical);
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
    db_unprotect(PROTECT_ALL);
    db_set("ssh-command", g.zSshCmd, 0);
    db_protect_pop();
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
  cgi_check_for_malice();
  style_header("Download Page");
  if( !g.perm.Zip ){
    @ <p>Bummer.  You do not have permission to download.
    if( g.zLogin==0 || g.zLogin[0]==0 ){
      @ Maybe it would work better if you
      @ %z(href("%R/login"))logged in</a>.
    }else{
      @ Contact the site administrator and ask them to give
      @ you "Download Zip" privileges.
    }
  }else{
    const char *zDLTag = db_get("download-tag","trunk");
    const char *zNm = db_get("short-project-name","download");
    char *zUrl = href("%R/zip/%t/%t.zip", zDLTag, zNm);
    @ <p>ZIP Archive: %z(zUrl)%h(zNm).zip</a>
    zUrl = href("%R/tarball/%t/%t.tar.gz", zDLTag, zNm);
    @ <p>Tarball: %z(zUrl)%h(zNm).tar.gz</a>
    if( g.zLogin!=0 ){
      zUrl = href("%R/sqlar/%t/%t.sqlar", zDLTag, zNm);
      @ <p>SQLite Archive: %z(zUrl)%h(zNm).sqlar</a>
    }
  }
  if( !g.perm.Clone ){
    @ <p>You are not authorized to clone this repository.
    if( g.zLogin==0 || g.zLogin[0]==0 ){
      @ Maybe you would be able to clone if you
      @ %z(href("%R/login"))logged in</a>.
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
  style_finish_page();
}
