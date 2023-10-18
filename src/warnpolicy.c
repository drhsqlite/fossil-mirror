/*
** Copyright (c) 2023 Preben Guldberg
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
** This file contains code implementing a warning policy for different events.
*/
#include "config.h"
#include "warnpolicy.h"

/*
** SETTING: warning-policy      width=40 block-text propagating default={}
** Policy for showing warnings under certain conditions.
**
** The policy is a JSON object where the following names are recognised:
**
**   commit:        Used when committing.  A list of objects with names in
**                  (message, branch, except-branch, users, except-users).
**   merge:         Used when merging.  A List of objects with names in
**                  (message, branch, except-branch, from, except-from,
**                  users, except-users, unpublished).
**   match-style:   If "regexp", patterns use REGEXP, otherwise GLOB.
**
** Meaning of names used in lists above:
**
**   message: MESSAGE          Required: Message to show.
**   branch: PATTERN           Apply branch match PATTERN (default any).
**   except-branch: PATTERN    Exclude when in a branch matching PATTERN.
**   from: PATTERN             Apply if merging from PATTERN (default any).
**   except-from: PATTERN      Exclude when merging from PATTERN.
**   unpublished: true         If true, only show when merging from a private
**                             branch into a public branch.
**   users: LIST               Show only for users in LIST (default any).
**   except-users: LIST        Users in LIST will not be shown the messages.
** 
** Example:
**
**   {
**     "commit": [
**       { "message": "Release pending, proceed with caution.",
**         "branch": "trunk",
**         "except-users": [ "owner", "admin" ] }
**     ],
**     "merge": [
**       { "message": "Please use 'fossil publish' before merging private to public",
**         "except-branch": "rebased-branch-*",
**         "unpublished": true },
**       { "message": "Updates to release branches should be merged from rc.",
**         "branch": "release-*",
**         "except-from": "rc-*" }
**     ]
**   }
*/ 

/*
** Fetch the match-style for warning-policy.
*/
static const char *warning_policy_match_style(void){
  int isRegexp = db_int(0,
      "SELECT 1"
      "  FROM config"
      " WHERE name='warning-policy'"
      "   AND json_error_position(value)=0"
      "   AND value->>'match-style'='regexp'");
  return isRegexp ? "REGEXP" : "GLOB";
}

/*
** Common part of issuing warnings.
*/
static int print_policy_warnings(Blob *pSql){
  Stmt q;
  int nWarnings = 0;

  db_prepare(&q, "%s)", blob_sql_text(pSql));
  while( db_step(&q)==SQLITE_ROW ){
    if( nWarnings==0 ) fossil_warning("Policy warnings:");
    fossil_warning("    %s", db_column_text(&q, 0));
    nWarnings++;
  }
  db_finalize(&q);
  return nWarnings;
}

/*
** Print commit specific warnings from the warning-policy.
*/
int issue_commit_warnings(
  const char *zBranch   /* The branch we are committing to */
){
    Blob sql = empty_blob;
  const char *zMatch;
  int nWarnings = 0;

  assert(zBranch!=0);
  if( g.zLogin==0) user_select();
  zMatch = warning_policy_match_style();
  blob_append_sql(&sql,
      "WITH list AS ("
      "   SELECT value AS elm"
      "     FROM json_each(("
      "        SELECT json_extract(value, '$.commit')"
      "          FROM config"
      "         WHERE name='warning-policy' AND json_error_position(value)=0)))"
      " SELECT elm->>'message' FROM list"
      "  WHERE ("
      "        (elm->>'branch' IS NULL"
      "         OR %Q %S elm->>'branch')"
      "    AND (elm->>'except-branch' IS NULL"
      "         OR NOT %Q %S elm->>'except-branch')"
      "    AND (elm->>'users' IS NULL"
      "         OR %Q IN (SELECT value FROM json_each(elm->>'users')))"
      "    AND NOT %Q IN (SELECT value FROM json_each(elm->>'except-users')"
      "        )",
      zBranch, zMatch, zBranch, zMatch, g.zLogin, g.zLogin
  );
  nWarnings = print_policy_warnings(&sql);
  blob_reset(&sql);
  return nWarnings;
}

/*
** Print merge specific warnings from the warning-policy.
*/
int issue_merge_warnings(
  const char *zBranch,  /* The branch we are merging into */
  const char *zFrom,    /* The branch we are merging from */
  int historyLoss       /* Merging a private branch into a public branch */
){
  Blob sql = empty_blob;
  const char *zMatch;
  int nWarnings = 0;

  assert(zBranch!=0);
  assert(zFrom!=0);
  if( g.zLogin==0) user_select();
  zMatch = warning_policy_match_style();
  blob_append_sql(&sql,
      "WITH list AS ("
      "   SELECT value AS elm"
      "     FROM json_each(("
      "        SELECT json_extract(value, '$.merge')"
      "          FROM config"
      "         WHERE name='warning-policy' AND json_error_position(value)=0)))"
      " SELECT elm->>'message' FROM list"
      "  WHERE ("
      "        (elm->>'branch' IS NULL"
      "         OR %Q %S elm->>'branch')"
      "    AND (elm->>'except-branch' IS NULL"
      "         OR NOT %Q %S elm->>'except-branch')"
      "    AND (elm->>'from' IS NULL"
      "         OR %Q %S elm->>'from')"
      "    AND (elm->>'except-from' IS NULL"
      "         OR NOT %Q %S elm->>'except-from')"
      "    AND (elm->>'users' IS NULL"
      "         OR %Q IN (SELECT value FROM json_each(elm->>'users')))"
      "    AND NOT %Q IN (SELECT value FROM json_each(elm->>'except-users'))",
      zBranch, zMatch, zBranch, zMatch,
      zFrom, zMatch, zFrom, zMatch,
      g.zLogin, g.zLogin
  );
  if( !historyLoss ){
    blob_append_sql(&sql,
        " AND (elm->>'unpublished' IS NULL"
        "      OR NOT elm->>'unpublished')"
    );
  }
  nWarnings = print_policy_warnings(&sql);
  blob_reset(&sql);
  return nWarnings;
}

/*
** COMMAND: test-warning-policy
** Usage:  %fossil test-warning-policy EVENT ?OPTIONS?
**
** Test what messages would be shown for a specific scenario.
** Use the global -U|--user option to test for a specific user.
**
** Options:
**   --json JSON   
**
** Options for "commit" event:
**   -b|--branch BRANCH   Test commit to BRANCH.
** 
** Options for "merge" event:
**   -b|--branch BRANCH   Test merge to BRANCH.
**   -f|--from BRANCH     Test merge from BRANCH.
**   -u|--unpublished     Test merging from a private to a public branch.
*/
void test_warning_policy_cmd(void){
  const char *zEvent;
  const char *zJSON;

  if( g.argc<3 ) fossil_fatal("EVENT is required");
  zEvent = g.argv[2];
  db_must_be_within_tree();

  zJSON = find_option("json", 0, 1);
  if( zJSON ){
    db_begin_transaction();
    db_set("warning-policy", zJSON, 0);
  }

  switch( db_int(-1,
      "SELECT json_error_position(value)=0 FROM config WHERE name='warning-policy'")
  ){
  case -1:  fossil_fatal("The warning-policy setting is not set");
  case 0:   fossil_fatal("The warning-policy setting is not valid JSON");
  default:  break;
  }

  if( fossil_strcmp(zEvent, "commit")==0 ){
    const char *zBranch = find_option("branch", "b", 1);
    if( zBranch==0 ) fossil_fatal("%s: missing --branch option", zEvent);
    verify_all_options();
    issue_commit_warnings(zBranch);
  }else if( fossil_strcmp(zEvent, "merge")==0 ){
    const char *zBranch = find_option("branch", "b", 1);
    const char *zFrom = find_option("from", "f", 1);
    int historyLoss = find_option("unpublished", "u", 0)!=0;
    if( zBranch==0 ) fossil_fatal("%s: missing --branch option", zEvent);
    if( zFrom==0 ) fossil_fatal("%s: missing --from option", zEvent);
    verify_all_options();
    issue_merge_warnings(zBranch, zFrom, historyLoss);
  }else{
    fossil_fatal("Unknown POLICY: %s", g.argv[2]);
  }

  if( zJSON ) db_end_transaction(1);
}

