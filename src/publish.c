/*
** Copyright (c) 2014 D. Richard Hipp
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
** This file contains code used to implement the "publish" and
** "unpublished" commands.
*/
#include "config.h"
#include "publish.h"
#include <assert.h>

/*
** COMMAND: unpublished
**
** Usage: %fossil unpublished ?OPTIONS?
**
** Show a list of unpublished or "private" artifacts.  Unpublished artifacts
** will never push and hence will not be shared with collaborators.
**
** By default, this command only shows unpublished check-ins.  To show
** all unpublished artifacts, use the --all command-line option.
**
** OPTIONS:
**     --all                   Show all artifacts, not just check-ins
*/
void unpublished_cmd(void){
  int bAll = find_option("all",0,0)!=0;

  db_find_and_open_repository(0,0);
  verify_all_options();
  if( bAll ){
    describe_artifacts_to_stdout("IN private", 0);
  }else{
    describe_artifacts_to_stdout(
      "IN (SELECT rid FROM private CROSS JOIN event"
                   " WHERE private.rid=event.objid"
                   "   AND event.type='ci')", 0);
  }
}

/*
** COMMAND: publish
**
** Usage: %fossil publish ?--only? TAGS...
**
** Cause artifacts identified by TAGS... to be published (made non-private).
** This can be used (for example) to convert a private branch into a public
** branch, or to publish a bundle that was imported privately.
**
** If any of TAGS names a branch, then all check-ins on the most recent
** instance of that branch are included, not just the most recent check-in.
**
** If any of TAGS name check-ins then all files and tags associated with
** those check-ins are also published automatically.  Except if the --only
** option is used, then only the specific artifacts identified by TAGS
** are published.
**
** If a TAG is already public, this command is a harmless no-op.
*/
void publish_cmd(void){
  int bOnly = find_option("only",0,0)!=0;
  int bTest = find_option("test",0,0)!=0;  /* Undocumented --test option */
  int bExclusive = find_option("exclusive",0,0)!=0;  /* undocumented */
  int i;

  db_find_and_open_repository(0,0);
  verify_all_options();
  if( g.argc<3 ) usage("?--only? TAGS...");
  db_begin_transaction();
  db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY);");
  for(i=2; i<g.argc; i++){
    int rid = name_to_rid(g.argv[i]);
    if( db_exists("SELECT 1 FROM tagxref"
                  " WHERE rid=%d AND tagid=%d"
                  "   AND tagtype>0 AND value=%Q",
                  rid,TAG_BRANCH,g.argv[i]) ){
      rid = start_of_branch(rid, 1);
      compute_descendants(rid, 1000000000);
    }else{
      db_multi_exec("INSERT OR IGNORE INTO ok VALUES(%d)", rid);
    }
  }
  if( !bOnly ){
    find_checkin_associates("ok", bExclusive);
  }
  if( bTest ){
    /* If the --test option is used, then do not actually publish any
    ** artifacts.  Instead, just list the artifact information on standard
    ** output.  The --test option is useful for verifying correct operation
    ** of the logic that figures out which artifacts to publish, such as
    ** the find_checkin_associates() routine
    */
    describe_artifacts_to_stdout("IN ok", 0);
  }else{
    /* Standard behavior is simply to remove the published documents from
    ** the PRIVATE table */
    db_multi_exec(
      "DELETE FROM ok WHERE rid NOT IN private;"
      "DELETE FROM private WHERE rid IN ok;"
      "INSERT OR IGNORE INTO unsent SELECT rid FROM ok;"
      "INSERT OR IGNORE INTO unclustered SELECT rid FROM ok;"
    );
  }
  db_end_transaction(0);
}
