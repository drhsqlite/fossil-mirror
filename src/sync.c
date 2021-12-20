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

/*
** Explain what type of sync operation is about to occur
*/
static void sync_explain(unsigned syncFlags){
  if( g.url.isAlias ){
    if( (syncFlags & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL) ){
      fossil_print("Sync with %s\n", g.url.canonical);
    }else if( syncFlags & SYNC_PUSH ){
      fossil_print("Push to %s\n", g.url.canonical);
    }else if( syncFlags & SYNC_PULL ){
      fossil_print("Pull from %s\n", g.url.canonical);
    }
  }
}


/*
** Call client_sync() one or more times in order to complete a
** sync operation.  Usually, client_sync() is called only once, though
** is can be called multiple times if the SYNC_ALLURL flags is set.
*/
static int client_sync_all_urls(
  unsigned syncFlags,      /* Mask of SYNC_* flags */
  unsigned configRcvMask,  /* Receive these configuration items */
  unsigned configSendMask, /* Send these configuration items */
  const char *zAltPCode    /* Alternative project code (usually NULL) */
){
  int nErr;
  int nOther;
  char **azOther;
  int i;
  Stmt q;

  sync_explain(syncFlags);
  nErr = client_sync(syncFlags, configRcvMask, configSendMask, zAltPCode);
  if( nErr==0 ) url_remember();
  if( (syncFlags & SYNC_ALLURL)==0 ) return nErr;
  nOther = 0;
  azOther = 0;
  db_prepare(&q,
    "SELECT substr(name,10) FROM config"
    " WHERE name glob 'sync-url:*'"
    "   AND value<>(SELECT value FROM config WHERE name='last-sync-url')"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUrl = db_column_text(&q, 0);
    azOther = fossil_realloc(azOther, sizeof(*azOther)*(nOther+1));
    azOther[nOther++] = fossil_strdup(zUrl);
  }
  db_finalize(&q);
  for(i=0; i<nOther; i++){
    int rc;
    url_unparse(&g.url);
    url_parse(azOther[i], URL_PROMPT_PW|URL_ASK_REMEMBER_PW);
    sync_explain(syncFlags);
    rc = client_sync(syncFlags, configRcvMask, configSendMask, zAltPCode);
    nErr += rc;
    if( (g.url.flags & URL_REMEMBER_PW)!=0 && rc==0 ){
      char *zKey = mprintf("sync-pw:%s", azOther[i]);
      char *zPw = obscure(g.url.passwd);
      if( zPw && zPw[0] ){
        db_set(zKey/*works-like:""*/, zPw, 0);
      }
      fossil_free(zPw);
      fossil_free(zKey);
    }
    fossil_free(azOther[i]);
    azOther[i] = 0;
  }
  fossil_free(azOther);
  return nErr;
}

/*
** Modify the URL to remove the username/password.
*/
static void remove_url_username(char *z){
  int i, j;
  for(i=0; z[i] && z[i]!='/'; i++){}
  if( z[i+1]!='/' ) return;
  i += 2;
  for(j=i; z[j] && z[j]!='@'; j++){}
  if( z[j]==0 ) return;
  j++;
  do{
    z[i++] = z[j];
  }while( z[j++]!=0 );
}

/*
** Make a new entry, or update an existing entry, in the SYNCLOG table.
**
** For an ordinary push/pull, zType is NULL.  But it may also be a string
** describing non-standard operations.  For example zType might be "git"
** when doing a "fossil git export", or zType might be "import" when doing
** a "fossil pull --from-parent-project".
**
** Usernames are stripped from the zFrom and zTo URLs
*/
void sync_log_entry(
  const char *zFrom,        /* Content comes from this machine */
  const char *zTo,          /* Content goes to this machine */
  i64 iTime,                /* Transfer time, or 0 for "now" */  
  const char *zType         /* Type of sync.  NULL for normal */
){
  char *zFree1 = 0;
  char *zFree2 = 0;
  schema_synclog();
  if( sqlite3_strglob("http*://*@*", zFrom)==0 ){
    zFree1 = fossil_strdup(zFrom);
    remove_url_username(zFree1);
    zFrom = zFree1;
  }
  if( sqlite3_strglob("http*://*@*", zTo)==0 ){
    zFree2 = fossil_strdup(zTo);
    remove_url_username(zFree2);
    zTo = zFree2;
  }
  if( iTime<=0 ){
    db_multi_exec(
      "INSERT INTO repository.synclog(sfrom,sto,stime,stype)"
      " VALUES(%Q,%Q,unixepoch(),%Q)"
      " ON CONFLICT DO UPDATE SET stime=unixepoch()",
      zFrom, zTo, zType
    );
  }else{
    db_multi_exec(
      "INSERT INTO repository.synclog(sfrom,sto,stime,stype)"
      " VALUES(%Q,%Q,%lld,%Q)"
      " ON CONFLICT DO UPDATE SET stime=%lld WHERE stime<%lld",
      zFrom, zTo, iTime, zType, iTime, iTime
    );
  }
  fossil_free(zFree1);
  fossil_free(zFree2);
}


/*
** If the repository is configured for autosyncing, then do an
** autosync.  Bits of the "flags" parameter determine details of behavior:
**
**   SYNC_PULL           Pull content from the server to the local repo
**   SYNC_PUSH           Push content from local up to the server
**   SYNC_CKIN_LOCK      Take a check-in lock on the current checkout.
**   SYNC_VERBOSE        Extra output
**
** Return the number of errors.
**
** The autosync setting can be a boolean or "pullonly".  No autosync
** is attempted if the autosync setting is off, and only auto-pull is
** attempted if autosync is set to "pullonly".  The check-in lock is
** not acquired unless autosync is set to "on".
**
** If dont-push setting is true, that is the same as having autosync
** set to pullonly.
*/
int autosync(int flags){
  const char *zAutosync;
  int rc;
  int configSync = 0;       /* configuration changes transferred */
  if( g.fNoSync ){
    return 0;
  }
  zAutosync = db_get("autosync", 0);
  if( zAutosync==0 ) zAutosync = "on";  /* defend against misconfig */
  if( is_false(zAutosync) ) return 0;
  if( db_get_boolean("dont-push",0) 
   || sqlite3_strglob("*pull*", zAutosync)==0
  ){
    flags &= ~SYNC_CKIN_LOCK;
    if( flags & SYNC_PUSH ) return 0;
  }
  if( find_option("verbose","v",0)!=0 ) flags |= SYNC_VERBOSE;
  url_parse(0, URL_REMEMBER);
  if( g.url.protocol==0 ) return 0;
  if( g.url.user!=0 && g.url.passwd==0 ){
    g.url.passwd = unobscure(db_get("last-sync-pw", 0));
    g.url.flags |= URL_PROMPT_PW;
    url_prompt_for_password();
  }
  g.zHttpAuth = get_httpauth();
  if( sqlite3_strglob("*all*", zAutosync)==0 ){
    rc = client_sync_all_urls(flags|SYNC_ALLURL, configSync, 0, 0);
  }else{
    url_remember();
    sync_explain(flags);
    url_enable_proxy("via proxy: ");
    rc = client_sync(flags, configSync, 0, 0);
  }
  return rc;
}

/*
** This routine will try a number of times to perform autosync with a
** 0.5 second sleep between attempts.
**
** Return zero on success and non-zero on a failure.  If failure occurs
** and doPrompt flag is true, ask the user if they want to continue, and
** if they answer "yes" then return zero in spite of the failure.
*/
int autosync_loop(int flags, int nTries, int doPrompt){
  int n = 0;
  int rc = 0;
  if( (flags & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL)
   && db_get_boolean("uv-sync",0)
  ){
    flags |= SYNC_UNVERSIONED;
  }
  while( (n==0 || n<nTries) && (rc=autosync(flags)) ){
    if( rc ){
      if( ++n<nTries ){
        fossil_warning("Autosync failed, making another attempt.");
        sqlite3_sleep(500);
      }else{
        fossil_warning("Autosync failed.");
      }
    }
  }
  if( rc && doPrompt ){
    Blob ans;
    char cReply;
    prompt_user("continue in spite of sync failure (y/N)? ", &ans);
    cReply = blob_str(&ans)[0];
    if( cReply=='y' || cReply=='Y' ) rc = 0;
    blob_reset(&ans);
  }
  return rc;
}

/*
** This routine processes the command-line argument for push, pull,
** and sync.  If a command-line argument is given, that is the URL
** of a server to sync against.  If no argument is given, use the
** most recently synced URL.  Remember the current URL for next time.
*/
static void process_sync_args(
  unsigned *pConfigFlags,      /* Write configuration flags here */
  unsigned *pSyncFlags,        /* Write sync flags here */
  int uvOnly,                  /* Special handling flags for UV sync */
  unsigned urlOmitFlags        /* Omit these URL flags */
){
  const char *zUrl = 0;
  const char *zHttpAuth = 0;
  unsigned configSync = 0;
  unsigned urlFlags = URL_REMEMBER | URL_PROMPT_PW;
  int urlOptional = 0;
  if( find_option("autourl",0,0)!=0 ){
    urlOptional = 1;
    urlFlags = 0;
  }
  zHttpAuth = find_option("httpauth","B",1);
  if( find_option("once",0,0)!=0 ) urlFlags &= ~URL_REMEMBER;
  if( (*pSyncFlags) & SYNC_FROMPARENT ) urlFlags &= ~URL_REMEMBER;
  if( !uvOnly ){
    if( find_option("private",0,0)!=0 ){
      *pSyncFlags |= SYNC_PRIVATE;
    }
    /* The --verily option to sync, push, and pull forces extra igot cards
    ** to be exchanged.  This can overcome malfunctions in the sync protocol.
    */
    if( find_option("verily",0,0)!=0 ){
      *pSyncFlags |= SYNC_RESYNC;
    }
  }
  if( find_option("private",0,0)!=0 ){
    *pSyncFlags |= SYNC_PRIVATE;
  }
  if( find_option("verbose","v",0)!=0 ){
    *pSyncFlags |= SYNC_VERBOSE;
  }
  if( find_option("no-http-compression",0,0)!=0 ){
    *pSyncFlags |= SYNC_NOHTTPCOMPRESS;
  }
  if( find_option("all",0,0)!=0 ){
    *pSyncFlags |= SYNC_ALLURL;
  }
  if( find_option("synclog",0,0)!=0 ){
    *pSyncFlags |= SYNC_PUSH_SYNCLOG;
  }
  url_proxy_options();
  clone_ssh_find_options();
  if( !uvOnly ) db_find_and_open_repository(0, 0);
  db_open_config(0, 1);
  if( g.argc==2 ){
    if( db_get_boolean("auto-shun",0) ) configSync = CONFIGSET_SHUN;
  }else if( g.argc==3 ){
    zUrl = g.argv[2];
    if( (*pSyncFlags) & SYNC_ALLURL ){
      fossil_fatal("cannot use both the --all option and specific URL \"%s\"",
          zUrl);
    }
  }
  if( ((*pSyncFlags) & (SYNC_PUSH|SYNC_PULL))==(SYNC_PUSH|SYNC_PULL)
   && db_get_boolean("uv-sync",0)
  ){
    *pSyncFlags |= SYNC_UNVERSIONED;
  }
  urlFlags &= ~urlOmitFlags;
  if( urlFlags & URL_REMEMBER ){
    clone_ssh_db_set_options();
  }
  url_parse(zUrl, urlFlags);
  remember_or_get_http_auth(zHttpAuth, urlFlags & URL_REMEMBER, zUrl);
  if( g.url.protocol==0 ){
    if( urlOptional ) fossil_exit(0);
    usage("URL");
  }
  user_select();
  url_enable_proxy("via proxy: ");
  *pConfigFlags |= configSync;
}


/*
** COMMAND: pull
**
** Usage: %fossil pull ?URL? ?options?
**
** Pull all sharable changes from a remote repository into the local
** repository.  Sharable changes include public check-ins, edits to
** wiki pages, tickets, tech-notes, and forum posts.  Add
** the --private option to pull private branches.  Use the
** "configuration pull" command to pull website configuration details.
**
** If URL is not specified, then the URL from the most recent clone, push,
** pull, remote, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**
**   --all                      Pull from all remotes, not just the default
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --from-parent-project      Pull content from the parent project
**   --ipv4                     Use only IPv4, not IPv6
**   --no-http-compression      Do not compress HTTP traffic
**   --once                     Do not remember URL for subsequent syncs
**   --private                  Pull private branches too
**   --project-code CODE        Use CODE as the project code
**   --proxy PROXY              Use the specified HTTP proxy
**   -R|--repository REPO       Local repository to pull into
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   --synclog                  Push local synclog information to the server
**   -v|--verbose               Additional (debugging) output
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: [[clone]], [[config]], [[push]], [[remote]], [[sync]]
*/
void pull_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PULL;
  unsigned urlOmitFlags = 0;
  const char *zAltPCode = find_option("project-code",0,1);
  if( find_option("from-parent-project",0,0)!=0 ){
    syncFlags |= SYNC_FROMPARENT;
  }
  if( zAltPCode ) urlOmitFlags = URL_REMEMBER;
  process_sync_args(&configFlags, &syncFlags, 0, urlOmitFlags);

  /* We should be done with options.. */
  verify_all_options();

  client_sync_all_urls(syncFlags, configFlags, 0, zAltPCode);
}

/*
** COMMAND: push
**
** Usage: %fossil push ?URL? ?options?
**
** Push all sharable changes from the local repository to a remote
** repository.  Sharable changes include public check-ins, edits to
** wiki pages, tickets, tech-notes, and forum posts.  Use
** --private to also push private branches.  Use the "configuration
** push" command to push website configuration details.
**
** If URL is not specified, then the URL from the most recent clone, push,
** pull, remote, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**
**   --all                      Push to all remotes, not just the default
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --ipv4                     Use only IPv4, not IPv6
**   --no-http-compression      Do not compress HTTP traffic
**   --once                     Do not remember URL for subsequent syncs
**   --proxy PROXY              Use the specified HTTP proxy
**   --private                  Push private branches too
**   -R|--repository REPO       Local repository to push from
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   --synclog                  Push local synclog information to the server
**   -v|--verbose               Additional (debugging) output
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: [[clone]], [[config]], [[pull]], [[remote]], [[sync]]
*/
void push_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PUSH;
  process_sync_args(&configFlags, &syncFlags, 0, 0);

  /* We should be done with options.. */
  verify_all_options();

  if( db_get_boolean("dont-push",0) ){
    fossil_fatal("pushing is prohibited: the 'dont-push' option is set");
  }
  client_sync_all_urls(syncFlags, 0, 0, 0);
}


/*
** COMMAND: sync
**
** Usage: %fossil sync ?URL? ?options?
**
** Synchronize all sharable changes between the local repository and a
** remote repository.  Sharable changes include public check-ins and
** edits to wiki pages, tickets, forum posts, and technical notes.
**
** If URL is not specified, then the URL from the most recent clone, push,
** pull, remote, or sync command is used.  See "fossil help clone" for
** details on the URL formats.
**
** Options:
**
**   --all                      Sync with all remotes, not just the default
**   -B|--httpauth USER:PASS    Credentials for the simple HTTP auth protocol,
**                              if required by the remote website
**   --ipv4                     Use only IPv4, not IPv6
**   --no-http-compression      Do not compress HTTP traffic
**   --once                     Do not remember URL for subsequent syncs
**   --proxy PROXY              Use the specified HTTP proxy
**   --private                  Sync private branches too
**   -R|--repository REPO       Local repository to sync with
**   --ssl-identity FILE        Local SSL credentials, if requested by remote
**   --ssh-command SSH          Use SSH as the "ssh" command
**   --synclog                  Push local synclog information to the server
**   -u|--unversioned           Also sync unversioned content
**   -v|--verbose               Additional (debugging) output
**   --verily                   Exchange extra information with the remote
**                              to ensure no content is overlooked
**
** See also: [[clone]], [[pull]], [[push]], [[remote]]
*/
void sync_cmd(void){
  unsigned configFlags = 0;
  unsigned syncFlags = SYNC_PUSH|SYNC_PULL;
  if( find_option("unversioned","u",0)!=0 ){
    syncFlags |= SYNC_UNVERSIONED;
  }
  process_sync_args(&configFlags, &syncFlags, 0, 0);

  /* We should be done with options.. */
  verify_all_options();

  if( db_get_boolean("dont-push",0) ) syncFlags &= ~SYNC_PUSH;
  if( (syncFlags & SYNC_PUSH)==0 ){
    fossil_warning("pull only: the 'dont-push' option is set");
  }
  client_sync_all_urls(syncFlags, configFlags, 0, 0);
}

/*
** Handle the "fossil unversioned sync" and "fossil unversioned revert"
** commands.
*/
void sync_unversioned(unsigned syncFlags){
  unsigned configFlags = 0;
  (void)find_option("uv-noop",0,0);
  process_sync_args(&configFlags, &syncFlags, 1, 0);
  verify_all_options();
  client_sync(syncFlags, 0, 0, 0);
}

/*
** COMMAND: remote
** COMMAND: remote-url*
**
** Usage: %fossil remote ?SUBCOMMAND ...?
**
** View or modify the set of remote repository sync URLs used as the
** target in any command that uses the sync protocol: "sync", "push",
** and "pull", plus all other commands that trigger Fossil's autosync
** feature.  (Collectively, "sync operations".)
**
** See "fossil help clone" for the format of these sync URLs.
**
** Fossil implicitly sets the default remote sync URL from the initial
** "clone" or "open URL" command for a repository, then may subsequently
** change it when given a URL in commands that take a sync URL, except
** when given the --once flag.  Fossil uses this new sync URL as its
** default when not explicitly given one in subsequent sync operations.
**
** Named remotes added by "remote add" allow use of those names in place
** of a sync URL in any command that takes one.
**
** The full name of this command is "remote-url", but we anticipate no
** future collision from use of its shortened form "remote".
**
** > fossil remote
**
**     With no arguments, this command shows the current default remote
**     URL.  If there is no default, it shows "off".
**
** > fossil remote add NAME URL
**
**     Add a new named URL to the set of remote sync URLs for use in
**     place of a sync URL in commands that take one.
**
** > fossil remote delete NAME
**
**     Delete a sync URL previously added by the "add" subcommand.
**
** > fossil remote list|ls
**
**     Show all remote repository sync URLs.
**
** > fossil remote off
**
**     Forget the default sync URL, disabling autosync.  Combined with
**     named sync URLs, it allows canceling this "airplane mode" with
**     "fossil remote NAME" to select a previously-set named URL.
**
**     To disable use of the default remote without forgetting its URL,
**     say "fossil set autosync 0" instead.
**
** > fossil remote scrub
**
**     Forget any saved passwords for remote repositories, but continue
**     to remember the URLs themselves.  You will be prompted for the
**     password the next time it is needed.
**
** > fossil remote REF
**
**     Make REF the new default URL, replacing the prior default.
**     REF may be a URL or a NAME from a prior "add".
*/
void remote_url_cmd(void){
  char *zUrl, *zArg;
  int nArg;
  db_find_and_open_repository(0, 0);

  /* We should be done with options.. */
  verify_all_options();

  /* 2021-10-25: A note about data structures.
  **
  ** The remote URLs are stored in the CONFIG table.  The URL is stored
  ** separately from the password.  The password is obscured using the
  ** obscure() function.
  **
  ** Originally, Fossil only preserved a single remote URL.  That URL
  ** is stored in "last-sync-url" and the password in "last-sync-pw".  The
  ** ability to have multiple remotes was added later so these names
  ** were retained for backwards compatibility.  The other remotes are
  ** stored in "sync-url:NAME" and "sync-pw:NAME" where NAME is the name
  ** of the remote.
  **
  ** The last-sync-url is called "default" for the display list.
  **
  ** The last-sync-url might be duplicated into one of the sync-url:NAME
  ** entries.  Thus, when doing a "fossil sync --all" or an autosync with
  ** autosync=all, each sync-url:NAME entry is checked to see if it is the
  ** same as last-sync-url and if it is then that entry is skipped.
  */ 

  if( g.argc==2 ){
    /* "fossil remote" with no arguments:  Show the last sync URL. */
    zUrl = db_get("last-sync-url", 0);
    if( zUrl==0 ){
      fossil_print("off\n");
    }else{
      url_parse(zUrl, 0);
      fossil_print("%s\n", g.url.canonical);
    }
    return;
  }
  zArg = g.argv[2];
  nArg = (int)strlen(zArg);
  if( strcmp(zArg,"off")==0 ){
    /* fossil remote off
    ** Forget the last-sync-URL and its password
    */
    if( g.argc!=3 ) usage("off");
remote_delete_default:
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "DELETE FROM config WHERE name GLOB 'last-sync-*';"
    );
    db_protect_pop();
    return;
  }
  if( strncmp(zArg, "list", nArg)==0 || strcmp(zArg,"ls")==0 ){
    Stmt q;
    if( g.argc!=3 ) usage("list");
    db_prepare(&q,
      "SELECT 'default', value FROM config WHERE name='last-sync-url'"
      " UNION ALL "
      "SELECT substr(name,10), value FROM config"
      " WHERE name GLOB 'sync-url:*'"
      " ORDER BY 1"
    );
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%-18s %s\n", db_column_text(&q,0), db_column_text(&q,1));
    }
    db_finalize(&q);
    return;
  }
  if( strcmp(zArg, "add")==0 ){
    char *zName;
    char *zUrl;
    UrlData x;
    if( g.argc!=5 ) usage("add NAME URL");
    memset(&x, 0, sizeof(x));
    zName = g.argv[3];
    zUrl = g.argv[4];
    if( strcmp(zName,"default")==0 ) goto remote_add_default;
    url_parse_local(zUrl, URL_PROMPT_PW, &x);
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
       "REPLACE INTO config(name, value, mtime)"
       " VALUES('sync-url:%q',%Q,now())",
       zName, x.canonical
    );
    db_multi_exec(
       "REPLACE INTO config(name, value, mtime)"
       " VALUES('sync-pw:%q',obscure(%Q),now())",
       zName, x.passwd
    );
    db_protect_pop();
    db_commit_transaction();
    return;
  }
  if( strncmp(zArg, "delete", nArg)==0 ){
    char *zName;
    if( g.argc!=4 ) usage("delete NAME");
    zName = g.argv[3];
    if( strcmp(zName,"default")==0 ) goto remote_delete_default;
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec("DELETE FROM config WHERE name glob 'sync-url:%q'", zName);
    db_multi_exec("DELETE FROM config WHERE name glob 'sync-pw:%q'", zName);
    db_protect_pop();
    db_commit_transaction();
    return;
  }
  if( strncmp(zArg, "scrub", nArg)==0 ){
    if( g.argc!=3 ) usage("scrub");
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec("DELETE FROM config WHERE name glob 'sync-pw:*'");
    db_multi_exec("DELETE FROM config WHERE name = 'last-sync-pw'");
    db_protect_pop();
    db_commit_transaction();
    return;
  }
  if( strncmp(zArg, "config-data", nArg)==0 ){
    /* Undocumented command:  "fossil remote config-data"
    **
    ** Show the CONFIG table entries that relate to remembering remote URLs
    */
    Stmt q;
    int n;
    n = db_int(13,
       "SELECT max(length(name))"
       "  FROM config"
       " WHERE name GLOB 'sync-*:*' OR name GLOB 'last-sync-*'"
    );
    db_prepare(&q,
       "SELECT name,"
       "       CASE WHEN name LIKE '%%sync-pw%%'"
                  " THEN printf('%%.*c',length(value),'*') ELSE value END"
       "  FROM config"
       " WHERE name GLOB 'sync-*:*' OR name GLOB 'last-sync-*'"
       " ORDER BY name LIKE '%%sync-pw%%', name"
    );
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%-*s  %s\n",
        n, db_column_text(&q,0),
        db_column_text(&q,1)
      );
    }
    db_finalize(&q);
    return;
  }
  if( sqlite3_strlike("http://%",zArg,0)==0
   || sqlite3_strlike("https://%",zArg,0)==0
   || sqlite3_strlike("ssh:%",zArg,0)==0
   || sqlite3_strlike("file:%",zArg,0)==0
   || db_exists("SELECT 1 FROM config WHERE name='sync-url:%q'",zArg)
  ){
remote_add_default:
    db_unset("last-sync-url", 0);
    db_unset("last-sync-pw", 0);
    url_parse(g.argv[2], URL_REMEMBER|URL_PROMPT_PW|URL_ASK_REMEMBER_PW);
    url_remember();
    return;
  }
  fossil_fatal("unknown command \"%s\" - should be a URL or one of: "
               "add delete list off", zArg);
}

/*
** COMMAND: backup*
**
** Usage: %fossil backup ?OPTIONS? FILE|DIRECTORY
**
** Make a backup of the repository into the named file or into the named
** directory.  This backup is guaranteed to be consistent even if there are
** concurrent changes taking place on the repository.  In other words, it
** is safe to run "fossil backup" on a repository that is in active use.
**
** Only the main repository database is backed up by this command.  The
** open checkout file (if any) is not saved.  Nor is the global configuration
** database.
**
** Options:
**
**    --overwrite              OK to overwrite an existing file
**    -R NAME                  Filename of the repository to backup
*/
void backup_cmd(void){
  char *zDest;
  int bOverwrite = 0;
  char *zFullName;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  bOverwrite = find_option("overwrite",0,0)!=0;
  verify_all_options();
  if( g.argc!=3 ){
    usage("FILE|DIRECTORY");
  }
  zDest = g.argv[2];
  if( file_isdir(zDest, ExtFILE)==1 ){
    zDest = mprintf("%s/%s", zDest, file_tail(g.zRepositoryName));
  }
  if( file_isfile(zDest, ExtFILE) ){
    if( bOverwrite ){
      if( file_delete(zDest) ){
        fossil_fatal("unable to delete old copy of \"%s\"", zDest);
      }
    }else{
      fossil_fatal("backup \"%s\" already exists", zDest);
    }
  }
  db_unprotect(PROTECT_ALL);
  db_multi_exec("VACUUM repository INTO %Q", zDest);
  zFullName = file_canonical_name_dup(zDest);
  sync_log_entry("this", zFullName, 0, "backup");
  fossil_free(zFullName);
}

/*
** COMMAND: synclog
**
** Usage: %fossil synclog
**
** Show other repositories with which this repository has pushed or pulled,
** together with the time since the most recent push or pull.
*/
void synclog_cmd(void){
  Stmt q;
  int cnt;
  const int nIndent = 2;
  db_find_and_open_repository(0,0);
  db_prepare(&q,
    "WITH allpull(xfrom,xto,xtime) AS MATERIALIZED (\n"
    "  SELECT sfrom, sto, max(stime) FROM synclog GROUP BY 1\n"
    "),\n"
    "pull(level, url, mtime, ex) AS (\n"
    "  SELECT 0, xfrom, xtime, '|this|' || xfrom || '|'\n"
    "    FROM allpull WHERE xto='this'\n"
    "  UNION\n"
    "  SELECT level+1, xfrom, xtime, ex || xfrom || '|'\n"
    "    FROM pull, allpull\n"
    "   WHERE xto=url\n"
    "     AND ex NOT GLOB ('*|' || xfrom || '|*')\n"
    "   ORDER BY 1 DESC, 3 DESC\n"
    ")\n"
    "SELECT level, url, julianday() - julianday(mtime,'auto') FROM pull"
  );
  cnt = 0;
  fossil_print("PULL:\n");
  while( db_step(&q)==SQLITE_ROW ){
    int iLevel = (db_column_int(&q,0)+1)*nIndent;
    const char *zUrl = db_column_text(&q,1);
    double rTimeAgo = db_column_double(&q,2);
    if( rTimeAgo*86400.0<=2.0 ){
      fossil_print("%.*c%s (current)\n", iLevel, ' ', zUrl);
    }else{
      char *zAgo = human_readable_age(rTimeAgo);
      fossil_print("%.*c%s (%z ago)\n", iLevel, ' ', zUrl, zAgo);
    }
    cnt++;
  }
  db_finalize(&q);
  if( cnt==0 ){
    fossil_print("  (none)\n");
  }
  db_prepare(&q,
    "WITH allpush(xfrom,xto,xtime) AS MATERIALIZED (\n"
    "  SELECT sfrom, sto, max(stime) FROM synclog GROUP BY 2\n"
    "),\n"
    "push(level, url, mtime, ex) AS (\n"
    "  SELECT 0, xto, xtime, '|this|' || xto || '|'\n"
    "    FROM allpush WHERE xfrom='this'\n"
    "  UNION\n"
    "  SELECT level+1, xto, xtime, ex || xto || '|'\n"
    "    FROM push, allpush\n"
    "   WHERE xfrom=url\n"
    "     AND ex NOT GLOB ('*|' || xto || '|*')\n"
    "   ORDER BY 1 DESC, 3 DESC\n"
    ")\n"
    "SELECT level, url, julianday() - julianday(mtime,'auto') FROM push"
  );
  cnt = 0;
  fossil_print("PUSH:\n");
  while( db_step(&q)==SQLITE_ROW ){
    int iLevel = (db_column_int(&q,0)+1)*nIndent;
    const char *zUrl = db_column_text(&q,1);
    double rTimeAgo = db_column_double(&q,2);
    if( rTimeAgo*86400.0<=2.0 ){
      fossil_print("%.*c%s (current)\n", iLevel, ' ', zUrl);
    }else{
      char *zAgo = human_readable_age(rTimeAgo);
      fossil_print("%.*c%s (%z ago)\n", iLevel, ' ', zUrl, zAgo);
    }
    cnt++;
  }
  db_finalize(&q);
  if( cnt==0 ){
    fossil_print("  (none)\n");
  }
}
