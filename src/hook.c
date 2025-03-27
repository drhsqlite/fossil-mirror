/*
** Copyright (c) 2020 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** This file implements "hooks" - external programs that can be run
** when various events occur on a Fossil repository.
**
** Hooks are stored in the following CONFIG variables:
**
**     hooks               A JSON-array of JSON objects.  Each object describes
**                         a single hook.  Example:
**                         {
**                            "type": "after-receive",  // type of hook
**                            "cmd": "command-to-run",  // command to run
**                            "seq": 50                 // run in this order
**                         }
**
**     hook-last-rcvid     The last rcvid for which post-receive hooks were
**                         run.
**
**     hook-embargo        Do not run hooks again before this julianday.
**
** For "after-receive" hooks, a list of the received artifacts is sent
** into the command via standard input.  Each line of input begins with
** the hash of the artifact and continues with a description of the
** interpretation of the artifact.
*/
#include "config.h"
#include "hook.h"

/*
** SETTING: hooks sensitive width=40 block-text
** The "hooks" setting contains JSON that describes all defined
** hooks.  The value is an array of objects.  Each object describes
** a single hook.  Example:
**
**
**    {
**    "type": "after-receive",  // type of hook
**    "cmd": "command-to-run",  // command to run
**    "seq": 50                 // run in this order
**    }
*/
/*
** List of valid hook types:
*/
static const char *azType[] = {
   "after-receive",
   "before-commit",
   "disabled",
};

/*
** Return true if zType is a valid hook type.
*/
static int is_valid_hook_type(const char *zType){
  int i;
  for(i=0; i<count(azType); i++){
    if( strcmp(azType[i],zType)==0  ) return 1;
  }
  return 0;
}

/*
** Throw an error if zType is not a valid hook type
*/
static void validate_type(const char *zType){
  int i;
  char *zMsg;
  if( is_valid_hook_type(zType) ) return;
  zMsg = mprintf("\"%s\" is not a valid hook type - should be one of:", zType);
  for(i=0; i<count(azType); i++){
    zMsg = mprintf("%z %s", zMsg, azType[i]);
  }
  fossil_fatal("%s", zMsg);
}

/*
** Translate a hook command string into its executable format by
** converting every %-substitutions as follows:
**
**     %F     ->    Name of the fossil executable
**     %R     ->    Name of the repository
**     %A     ->    Auxiliary information filename (might be empty string)
**
** The returned string is obtained from fossil_malloc() and should
** be freed by the caller.
*/
static char *hook_subst(
  const char *zCmd,
  const char *zAuxFilename   /* Name of auxiliary information file */
){
  Blob r;
  int i;
  blob_init(&r, 0, 0);
  if( zCmd==0 ) return 0;
  while( zCmd[0] ){
    for(i=0; zCmd[i] && zCmd[i]!='%'; i++){}
    blob_append(&r, zCmd, i);
    if( zCmd[i]==0 ) break;
    if( zCmd[i+1]=='F' ){
      blob_append(&r, g.nameOfExe, -1);
      zCmd += i+2;
    }else if( zCmd[i+1]=='R' ){
      blob_append(&r, g.zRepositoryName, -1);
      zCmd += i+2;
    }else if( zCmd[i+1]=='A' ){
      if( zAuxFilename ) blob_append(&r, zAuxFilename, -1);
      zCmd += i+2;
    }else{
      blob_append(&r, zCmd+i, 1);
      zCmd += i+1;
    }
  }
  blob_str(&r);
  return r.aData;
}

/*
** Record the fact that new artifacts are expected within N seconds
** (N is normally a small number) and so post-receive hooks should
** probably be deferred until after the new artifacts arrive.
**
** If N==0, then there is no expectation of new artifacts arriving
** soon and so post-receive hooks can be run without delay.
*/
void hook_expecting_more_artifacts(int N){
  if( !db_is_writeable("repository") ){
    /* No-op */
  }else if( N>0 ){
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "REPLACE INTO config(name,value,mtime)"
      "VALUES('hook-embargo',now()+%d,now())",
      N
    );
    db_protect_pop();
  }else{
    db_unset("hook-embargo",0);
  }
}


/*
** Fill the Blob pOut with text that describes all artifacts
** received after zBaseRcvid up to and including zNewRcvid.
** Except, never include more than one days worth of changes.
**
** If zBaseRcvid is NULL, then use the "hook-last-rcvid" setting.
** If zNewRcvid is NULL, use the last available rcvid.
*/
void hook_changes(Blob *pOut, const char *zBaseRcvid, const char *zNewRcvid){
  char *zWhere;
  Stmt q;
  if( zBaseRcvid==0 ){
    zBaseRcvid = db_get("hook-last-rcvid","0");
  }
  if( zNewRcvid==0 ){
    zNewRcvid = db_text("0","SELECT max(rcvid) FROM rcvfrom");
  }

  /* Adjust the baseline rcvid to omit change that are more than
  ** 24 hours older than the most recent change.
  */
  zBaseRcvid = db_text(0,
     "SELECT min(rcvid) FROM rcvfrom"
     " WHERE rcvid>=%d"
     "   AND mtime>=(SELECT mtime FROM rcvfrom WHERE rcvid=%d)-1.0",
     atoi(zBaseRcvid), atoi(zNewRcvid)
  );

  zWhere = mprintf("IN (SELECT rid FROM blob WHERE rcvid>%d AND rcvid<=%d)",
                   atoi(zBaseRcvid), atoi(zNewRcvid));
  describe_artifacts(zWhere);
  fossil_free(zWhere);
  db_prepare(&q, "SELECT uuid, summary FROM description");
  while( db_step(&q)==SQLITE_ROW ){
    blob_appendf(pOut, "%s %s\n", db_column_text(&q,0), db_column_text(&q,1));
  }
  db_finalize(&q);
}

/*
** COMMAND: hook*
**
** Usage: %fossil hook COMMAND ...
**
** Commands include:
**
** > fossil hook add --command COMMAND --type TYPE --sequence NUMBER
**
**        Create a new hook.  The --command and --type arguments are
**        required.  --sequence is optional.
**
** > fossil hook delete ID ...
**
**        Delete one or more hooks by their IDs.  ID can be "all"
**        to delete all hooks.  Caution:  There is no "undo" for
**        this operation.  Deleted hooks are permanently lost.
**
** > fossil hook edit --command COMMAND --type TYPE --sequence NUMBER ID ...
**
**        Make changes to one or more existing hooks.  The ID argument
**        is either a hook-id, or a list of hook-ids, or the keyword
**        "all".  For example, to disable hook number 2, use:
**
**            fossil hook edit --type disabled 2
**
** > fossil hook list
**
**        Show all current hooks
**
** > fossil hook status
**
**        Print the values of CONFIG table entries that are relevant to
**        hook processing.  Used for debugging.
**
** > fossil hook test [OPTIONS] ID
**
**        Run the hook script given by ID for testing purposes.
**        Options:
**
**            --dry-run          Print the script on stdout rather than run it
**            --base-rcvid N     Pretend that the hook-last-rcvid value is N
**            --new-rcvid M      Pretend that the last rcvid value is M
**            --aux-file NAME    NAME is substituted for %A in the script
**
**        The --base-rcvid and --new-rcvid options are silently ignored if
**        the hook type is not "after-receive".  The default values for
**        --base-rcvid and --new-rcvid cause the last receive to be processed.
*/
void hook_cmd(void){
  const char *zCmd;
  int nCmd;
  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    usage("SUBCOMMAND ...");
  }
  zCmd = g.argv[2];
  nCmd = (int)strlen(zCmd);
  if( strncmp(zCmd, "add", nCmd)==0 ){
    const char *zCmd = find_option("command",0,1);
    const char *zType = find_option("type",0,1);
    const char *zSeq = find_option("sequence",0,1);
    int nSeq;
    verify_all_options();
    if( zCmd==0 || zType==0 ){
      fossil_fatal("the --command and --type options are required");
    }
    validate_type(zType);
    nSeq = zSeq ? atoi(zSeq) : 10;
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
       "INSERT OR IGNORE INTO config(name,value) VALUES('hooks','[]');\n"
       "UPDATE config"
       "  SET value=json_insert("
       "     CASE WHEN json_valid(value) THEN value ELSE '[]' END,'$[#]',"
       "     json_object('cmd',%Q,'type',%Q,'seq',%d)),"
       "      mtime=now()"
       " WHERE name='hooks';",
       zCmd, zType, nSeq
    );
    db_protect_pop();
    db_commit_transaction();
  }else
  if( strncmp(zCmd, "edit", nCmd)==0 ){
    const char *zCmd = find_option("command",0,1);
    const char *zType = find_option("type",0,1);
    const char *zSeq = find_option("sequence",0,1);
    int nSeq;
    int i;
    verify_all_options();
    if( zCmd==0 && zType==0 && zSeq==0 ){
      fossil_fatal("at least one of --command, --type, or --sequence"
                   " is required");
    }
    if( zType ) validate_type(zType);
    nSeq = zSeq ? atoi(zSeq) : 10;
    if( g.argc<4 ) usage("delete ID ...");
    db_begin_write();
    for(i=3; i<g.argc; i++){
      Blob sql;
      int id;
      if( sqlite3_strglob("*[^0-9]*", g.argv[i])==0 ){
        fossil_fatal("not a valid ID: \"%s\"", g.argv[i]);
      }
      id = atoi(g.argv[i]);
      blob_init(&sql, 0, 0);
      blob_append_sql(&sql, "UPDATE config SET mtime=now(), value="
        "json_replace(CASE WHEN json_valid(value) THEN value ELSE '[]' END");
      if( zCmd ){
        blob_append_sql(&sql, ",'$[%d].cmd',%Q", id, zCmd);
      }
      if( zType ){
        blob_append_sql(&sql, ",'$[%d].type',%Q", id, zType);
      }
      if( zSeq ){
        blob_append_sql(&sql, ",'$[%d].seq',%d", id, nSeq);
      }
      blob_append_sql(&sql,") WHERE name='hooks';");
      db_unprotect(PROTECT_CONFIG);
      db_multi_exec("%s", blob_sql_text(&sql));
      db_protect_pop();
      blob_reset(&sql);
    }
    db_commit_transaction();
  }else
  if( strncmp(zCmd, "delete", nCmd)==0 ){
    int i;
    verify_all_options();
    if( g.argc<4 ) usage("delete ID ...");
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
       "INSERT OR IGNORE INTO config(name,value) VALUES('hooks','[]');\n"
    );
    for(i=3; i<g.argc; i++){
      const char *zId = g.argv[i];
      if( strcmp(zId,"all")==0 ){
        db_unprotect(PROTECT_ALL);
        db_set("hooks","[]", 0);
        db_protect_pop();
        break;
      }
      if( sqlite3_strglob("*[^0-9]*", g.argv[i])==0 ){
        fossil_fatal("not a valid ID: \"%s\"", g.argv[i]);
      }
      db_multi_exec(
        "UPDATE config"
        "  SET value=json_remove("
        "     CASE WHEN json_valid(value) THEN value ELSE '[]' END,'$[%d]'),"
        "      mtime=now()"
        " WHERE name='hooks';",
        atoi(zId)
      );
    }
    db_protect_pop();
    db_commit_transaction();
  }else
  if( strncmp(zCmd, "list", nCmd)==0 ){
    Stmt q;
    int n = 0;
    verify_all_options();
    db_prepare(&q,
      "SELECT jx.key,"
      "       jx.value->>'seq',"
      "       jx.value->>'cmd',"
      "       jx.value->>'type'"
      "  FROM config, json_each(config.value) AS jx"
      " WHERE config.name='hooks' AND json_valid(config.value)"
    );
    while( db_step(&q)==SQLITE_ROW ){
      if( n++ ) fossil_print("\n");
      fossil_print("%3d: type = %s\n",
        db_column_int(&q,0), db_column_text(&q,3));
      fossil_print("     command = %s\n", db_column_text(&q,2));
      fossil_print("     sequence = %d\n", db_column_int(&q,1));
    }
    db_finalize(&q);
  }else
  if( strncmp(zCmd, "status", nCmd)==0 ){
    Stmt q;
    db_prepare(&q,
      "SELECT name, quote(value) FROM config WHERE name IN"
      "('hooks','hook-embargo','hook-last-rcvid') ORDER BY name"
    );
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%s: %s\n", db_column_text(&q,0), db_column_text(&q,1));
    }
    db_finalize(&q);
  }else
  if( strncmp(zCmd, "test", nCmd)==0 ){
    Stmt q;
    int id;
    int bDryRun = find_option("dry-run", "n", 0)!=0;
    const char *zOrigRcvid = find_option("base-rcvid",0,1);
    const char *zNewRcvid = find_option("new-rcvid",0,1);
    const char *zAuxFilename = find_option("aux-file",0,1);
    verify_all_options();
    if( g.argc<4 ) usage("test ID");
    id = atoi(g.argv[3]);
    if( zOrigRcvid==0 ){
      zOrigRcvid = db_text(0, "SELECT max(rcvid)-1 FROM rcvfrom");
    }
    db_prepare(&q,
      "SELECT value->>'$[%d].cmd', value->>'$[%d].type'=='after-receive'"
      "  FROM config"
      " WHERE name='hooks' AND json_valid(value)",
      id, id
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zCmd = db_column_text(&q,0);
      char *zCmd2 = hook_subst(zCmd, zAuxFilename);
      int needOut = db_column_int(&q,1);
      Blob out;
      if( zCmd2==0 ) continue;
      blob_init(&out,0,0);
      if( needOut ) hook_changes(&out, zOrigRcvid, zNewRcvid);
      if( bDryRun ){
        fossil_print("%s\n", zCmd2);
        if( needOut ){
          fossil_print("%s", blob_str(&out));
        }
      }else if( needOut ){
        int fdFromChild;
        FILE *toChild;
        int pidChild;
        if( popen2(zCmd2, &fdFromChild, &toChild, &pidChild, 0)==0 ){
          if( toChild ){
            fwrite(blob_buffer(&out),1,blob_size(&out),toChild);
          }
          pclose2(fdFromChild, toChild, pidChild);
        }
      }else{
        fossil_system(zCmd2);
      }
      fossil_free(zCmd2);
      blob_reset(&out);
    }
    db_finalize(&q);
  }else
  {
     fossil_fatal("unknown command \"%s\" - should be one of: "
                  "add delete edit list test", zCmd);
  }
}

/*
** The backoffice calls this routine to run the after-receive hooks.
*/
int hook_backoffice(void){
  Stmt q;
  const char *zLastRcvid = 0;
  char *zNewRcvid = 0;
  Blob chng;
  int cnt = 0;
  db_begin_write();
  if( !db_exists("SELECT 1 FROM config WHERE name='hooks'") ){
    goto hook_backoffice_done;  /* No hooks */
  }
  if( db_int(0, "SELECT now()<value+0 FROM config"
                " WHERE name='hook-embargo'") ){
    goto hook_backoffice_done;  /* Within the embargo window */
  }
  zLastRcvid = db_get("hook-last-rcvid","0");
  zNewRcvid = db_text("0","SELECT max(rcvid) FROM rcvfrom");
  if( atoi(zLastRcvid)>=atoi(zNewRcvid) ){
    goto hook_backoffice_done;  /* no new content */
  }
  blob_init(&chng, 0, 0);
  db_prepare(&q,
      "SELECT jx.value->>'cmd'"
      "  FROM config, json_each(config.value) AS jx"
      " WHERE config.name='hooks' AND json_valid(config.value)"
      "   AND jx.value->>'type'='after-receive'"
      " ORDER BY jx.value->>'seq';"
  );
  while( db_step(&q)==SQLITE_ROW ){
    char *zCmd;
    int fdFromChild;
    FILE *toChild;
    int childPid;
    if( cnt==0 ){
      hook_changes(&chng, zLastRcvid, 0);
    }
    zCmd = hook_subst(db_column_text(&q,0), 0);
    if( popen2(zCmd, &fdFromChild, &toChild, &childPid, 0)==0 ){
      if( toChild ){
        fwrite(blob_buffer(&chng),1,blob_size(&chng),toChild);
      }
      pclose2(fdFromChild, toChild, childPid);
    }
    fossil_free(zCmd);
    cnt++;
  }
  db_finalize(&q);
  db_set("hook-last-rcvid", zNewRcvid, 0);
  blob_reset(&chng);
hook_backoffice_done:
  db_commit_transaction();
  return cnt;
}

/*
** Return true if one or more hooks of type zType exit.
*/
int hook_exists(const char *zType){
  return db_exists(
      "SELECT 1"
      "  FROM config, json_each(config.value) AS jx"
      " WHERE config.name='hooks' AND json_valid(config.value)"
      "   AND jx.value->>'type'=%Q;",
      zType
  );
}

/*
** Run all hooks of type zType.  Use zAuxFile as the auxiliary information
** file.
**
** If any hook returns non-zero, then stop running and return non-zero.
** Return zero only if all hooks return zero.
*/
int hook_run(const char *zType, const char *zAuxFile, int traceFlag){
  Stmt q;
  int rc = 0;
  if( !db_exists("SELECT 1 FROM config WHERE name='hooks'") ){
    return 0;
  }
  db_prepare(&q,
      "SELECT jx.value->>'cmd' "
      "  FROM config, json_each(config.value) AS jx"
      " WHERE config.name='hooks' AND json_valid(config.value)"
      "   AND jx.value->>'type'==%Q"
      " ORDER BY jx.value->'seq';",
      zType
  );
  while( db_step(&q)==SQLITE_ROW ){
    char *zCmd;
    zCmd = hook_subst(db_column_text(&q,0), zAuxFile);
    if( traceFlag ){
      fossil_print("%s hook: %s\n", zType, zCmd);
    }
    rc = fossil_system(zCmd);
    fossil_free(zCmd);
    if( rc ){
      break;
    }
  }
  db_finalize(&q);
  return rc;
}
