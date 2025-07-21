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
** This file contains code used to create new branches within a repository.
*/
#include "config.h"
#include "branch.h"
#include <assert.h>

/*
** Return true if zBr is the branch name associated with check-in with
** blob.uuid value of zUuid
*/
int branch_includes_uuid(const char *zBr, const char *zUuid){
  return db_exists(
    "SELECT 1 FROM tagxref, blob"
    " WHERE blob.uuid=%Q AND tagxref.rid=blob.rid"
    "   AND tagxref.value=%Q AND tagxref.tagtype>0"
    "   AND tagxref.tagid=%d",
    zUuid, zBr, TAG_BRANCH
  );
}

/*
** If RID refers to a check-in, return the name of the branch for that
** check-in.
**
** Space to hold the returned value is obtained from fossil_malloc()
** and should be freed by the caller.
*/
char *branch_of_rid(int rid){
  char *zBr = 0;
  static Stmt q;
  db_static_prepare(&q,
      "SELECT value FROM tagxref"
      " WHERE rid=$rid AND tagid=%d"
      " AND tagtype>0", TAG_BRANCH);
  db_bind_int(&q, "$rid", rid);
  if( db_step(&q)==SQLITE_ROW ){
    zBr = fossil_strdup(db_column_text(&q,0));
  }
  db_reset(&q);
  if( zBr==0 ){
    static char *zMain = 0;
    if( zMain==0 ) zMain = db_get("main-branch",0);
    zBr = fossil_strdup(zMain);
  }
  return zBr;
}

/*
**  fossil branch new    NAME  BASIS ?OPTIONS?
**  argv0  argv1  argv2  argv3 argv4
*/
void branch_new(void){
  int rootid;            /* RID of the root check-in - what we branch off of */
  int brid;              /* RID of the branch check-in */
  int noSign;            /* True if the branch is unsigned */
  int i;                 /* Loop counter */
  char *zUuid;           /* Artifact ID of origin */
  Stmt q;                /* Generic query */
  const char *zBranch;   /* Name of the new branch */
  char *zDate;           /* Date that branch was created */
  char *zComment;        /* Check-in comment for the new branch */
  const char *zColor;    /* Color of the new branch */
  Blob branch;           /* manifest for the new branch */
  Manifest *pParent;     /* Parsed parent manifest */
  Blob mcksum;           /* Self-checksum on the manifest */
  const char *zDateOvrd; /* Override date string */
  const char *zUserOvrd; /* Override user name */
  int isPrivate = 0;     /* True if the branch should be private */

  noSign = find_option("nosign","",0)!=0;
  if( find_option("nosync",0,0) ) g.fNoSync = 1;
  zColor = find_option("bgcolor","c",1);
  isPrivate = find_option("private",0,0)!=0;
  zDateOvrd = find_option("date-override",0,1);
  zUserOvrd = find_option("user-override",0,1);
  verify_all_options();
  if( g.argc<5 ){
    usage("new BRANCH-NAME BASIS ?OPTIONS?");
  }
  db_find_and_open_repository(0, 0);
  noSign = db_get_boolean("omitsign", 0)|noSign;
  if( db_get_boolean("clearsign", 0)==0 ){ noSign = 1; }

  /* fossil branch new name */
  zBranch = g.argv[3];
  if( zBranch==0 || zBranch[0]==0 ){
    fossil_fatal("branch name cannot be empty");
  }
  if( branch_is_open(zBranch) ){
    fossil_fatal("an open branch named \"%s\" already exists", zBranch);
  }

  user_select();
  db_begin_transaction();
  rootid = name_to_typed_rid(g.argv[4], "ci");
  if( rootid==0 ){
    fossil_fatal("unable to locate check-in off of which to branch");
  }

  pParent = manifest_get(rootid, CFTYPE_MANIFEST, 0);
  if( pParent==0 ){
    fossil_fatal("%s is not a valid check-in", g.argv[4]);
  }

  /* Create a manifest for the new branch */
  blob_zero(&branch);
  if( pParent->zBaseline ){
    blob_appendf(&branch, "B %s\n", pParent->zBaseline);
  }
  zComment = mprintf("Create new branch named \"%h\"", zBranch);
  blob_appendf(&branch, "C %F\n", zComment);
  zDate = date_in_standard_format(zDateOvrd ? zDateOvrd : "now");
  blob_appendf(&branch, "D %s\n", zDate);

  /* Copy all of the content from the parent into the branch */
  for(i=0; i<pParent->nFile; ++i){
    blob_appendf(&branch, "F %F", pParent->aFile[i].zName);
    if( pParent->aFile[i].zUuid ){
      blob_appendf(&branch, " %s", pParent->aFile[i].zUuid);
      if( pParent->aFile[i].zPerm && pParent->aFile[i].zPerm[0] ){
        blob_appendf(&branch, " %s", pParent->aFile[i].zPerm);
      }
    }
    blob_append(&branch, "\n", 1);
  }
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rootid);
  blob_appendf(&branch, "P %s\n", zUuid);
  if( pParent->zRepoCksum ){
    blob_appendf(&branch, "R %s\n", pParent->zRepoCksum);
  }
  manifest_destroy(pParent);

  /* Add the symbolic branch name and the "branch" tag to identify
  ** this as a new branch */
  if( content_is_private(rootid) ) isPrivate = 1;
  if( zColor!=0 ){
    blob_appendf(&branch, "T *bgcolor * %F\n", zColor);
  }
  blob_appendf(&branch, "T *branch * %F\n", zBranch);
  blob_appendf(&branch, "T *sym-%F *\n", zBranch);
  if( isPrivate ){
    noSign = 1;
  }

  /* Cancel all other symbolic tags */
  db_prepare(&q,
      "SELECT tagname FROM tagxref, tag"
      " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
      "   AND tagtype>0 AND tagname GLOB 'sym-*'"
      " ORDER BY tagname",
      rootid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTag = db_column_text(&q, 0);
    blob_appendf(&branch, "T -%F *\n", zTag);
  }
  db_finalize(&q);

  blob_appendf(&branch, "U %F\n", zUserOvrd ? zUserOvrd : login_name());
  md5sum_blob(&branch, &mcksum);
  blob_appendf(&branch, "Z %b\n", &mcksum);
  if( !noSign && clearsign(&branch, &branch) ){
    Blob ans;
    char cReply;
    prompt_user("unable to sign manifest.  continue (y/N)? ", &ans);
    cReply = blob_str(&ans)[0];
    if( cReply!='y' && cReply!='Y'){
      db_end_transaction(1);
      fossil_exit(1);
    }
  }

  brid = content_put_ex(&branch, 0, 0, 0, isPrivate);
  if( brid==0 ){
    fossil_fatal("trouble committing manifest: %s", g.zErrMsg);
  }
  db_add_unsent(brid);
  if( manifest_crosslink(brid, &branch, MC_PERMIT_HOOKS)==0 ){
    fossil_fatal("%s", g.zErrMsg);
  }
  assert( blob_is_reset(&branch) );
  content_deltify(rootid, &brid, 1, 0);
  zUuid = rid_to_uuid(brid);
  fossil_print("New branch: %s\n", zUuid);
  if( g.argc==3 ){
    fossil_print(
      "\n"
      "Note: the local check-out has not been updated to the new\n"
      "      branch.  To begin working on the new branch, do this:\n"
      "\n"
      "      %s update %s\n",
      g.argv[0], zBranch
    );
  }


  /* Commit */
  db_end_transaction(0);

  /* Do an autosync push, if requested */
  if( !isPrivate ) autosync_loop(SYNC_PUSH, 0, "branch");
}

/*
** Create a TEMP table named "tmp_brlist" with 7 columns:
**
**      name           Name of the branch
**      mtime          Time of last check-in on this branch
**      isclosed       True if the branch is closed
**      mergeto        Another branch this branch was merged into
**      nckin          Number of checkins on this branch
**      ckin           Hash of the last check-in on this branch
**      isprivate      True if the branch is private
**      bgclr          Background color for this branch
*/
static const char createBrlistQuery[] =
@ CREATE TEMP TABLE IF NOT EXISTS tmp_brlist AS
@ SELECT
@   tagxref.value AS name,
@   max(event.mtime) AS mtime,
@   EXISTS(SELECT 1 FROM tagxref AS tx
@           WHERE tx.rid=tagxref.rid
@             AND tx.tagid=(SELECT tagid FROM tag WHERE tagname='closed')
@             AND tx.tagtype>0) AS isclosed,
@   (SELECT tagxref.value
@      FROM plink CROSS JOIN tagxref
@    WHERE plink.pid=event.objid
@       AND tagxref.rid=plink.cid
@      AND tagxref.tagid=(SELECT tagid FROM tag WHERE tagname='branch')
@      AND tagtype>0) AS mergeto,
@   count(*) AS nckin,
@   (SELECT uuid FROM blob WHERE rid=tagxref.rid) AS ckin,
@   event.bgcolor AS bgclr,
@   EXISTS(SELECT 1 FROM private WHERE rid=tagxref.rid) AS isprivate
@  FROM tagxref, tag, event
@ WHERE tagxref.tagid=tag.tagid
@   AND tagxref.tagtype>0
@   AND tag.tagname='branch'
@   AND event.objid=tagxref.rid
@ GROUP BY 1;
;

/* Call this routine to create the TEMP table */
static void brlist_create_temp_table(void){
  db_exec_sql(createBrlistQuery);
}


#if INTERFACE
/*
** Allows bits in the mBplqFlags parameter to branch_prepare_list_query().
*/
#define BRL_CLOSED_ONLY      0x001 /* Show only closed branches */
#define BRL_OPEN_ONLY        0x002 /* Show only open branches */
#define BRL_BOTH             0x003 /* Show both open and closed branches */
#define BRL_OPEN_CLOSED_MASK 0x003
#define BRL_ORDERBY_MTIME    0x004 /* Sort by MTIME. (otherwise sort by name)*/
#define BRL_REVERSE          0x008 /* Reverse the sort order */
#define BRL_PRIVATE          0x010 /* Show only private branches */
#define BRL_MERGED           0x020 /* Show only merged branches */
#define BRL_UNMERGED         0x040 /* Show only unmerged branches */
#define BRL_LIST_USERS       0x080 /* Populate list of users participating */

#endif /* INTERFACE */

/*
** Prepare a query that will list branches.
**
** If the BRL_ORDERBY_MTIME flag is set and nLimitMRU ("Limit Most Recently Used
** style") is a non-zero number, the result is limited to nLimitMRU entries, and
** the BRL_REVERSE flag is applied in an outer query after processing the limit,
** so that it's possible to generate short lists with the most recently modified
** branches sorted chronologically in either direction, as does the "branch lsh"
** command.
** For other cases, the outer query is also generated, but works as a no-op. The
** code to build the outer query is marked with *//* OUTER QUERY *//* comments.
*/
void branch_prepare_list_query(
  Stmt *pQuery,
  int brFlags,
  const char *zBrNameGlob,
  int nLimitMRU,
  const char *zUser
){
  Blob sql;
  blob_init(&sql, 0, 0);
  brlist_create_temp_table();
  /* Ignore nLimitMRU if no chronological sort requested. */
  if( (brFlags & BRL_ORDERBY_MTIME)==0 ) nLimitMRU = 0;
  /* Negative values for nLimitMRU also mean "no limit". */
  if( nLimitMRU<0 ) nLimitMRU = 0;
  /* OUTER QUERY */
  blob_append_sql(&sql,"SELECT name, isprivate, mergeto,");
  if( brFlags & BRL_LIST_USERS ){
    blob_append_sql(&sql,
      " (SELECT group_concat(user) FROM ("
      "     SELECT DISTINCT * FROM ("
      "         SELECT coalesce(euser,user) AS user"
      "           FROM event"
      "          WHERE type='ci' AND objid IN ("
      "             SELECT rid FROM tagxref WHERE value=name)"
      "          ORDER BY 1)))"
    );
  }else{
    blob_append_sql(&sql, " NULL");
  }
  blob_append_sql(&sql," FROM (");
  /* INNER QUERY */
  switch( brFlags & BRL_OPEN_CLOSED_MASK ){
    case BRL_CLOSED_ONLY: {
      blob_append_sql(&sql,
        "SELECT name, isprivate, mtime, mergeto FROM tmp_brlist WHERE isclosed"
      );
      break;
    }
    case BRL_BOTH: {
      blob_append_sql(&sql,
        "SELECT name, isprivate, mtime, mergeto FROM tmp_brlist WHERE 1"
      );
      break;
    }
    case BRL_OPEN_ONLY: {
      blob_append_sql(&sql,
        "SELECT name, isprivate, mtime, mergeto FROM tmp_brlist "
        "  WHERE NOT isclosed"
      );
      break;
    }
  }
  if( brFlags & BRL_PRIVATE ) blob_append_sql(&sql, " AND isprivate");
  if( brFlags & BRL_MERGED ) blob_append_sql(&sql, " AND mergeto IS NOT NULL");
  if( zBrNameGlob ) blob_append_sql(&sql, " AND (name GLOB %Q)", zBrNameGlob);
  if( zUser && zUser[0] ) blob_append_sql(&sql,
    " AND EXISTS (SELECT 1 FROM event WHERE type='ci' AND (user=%Q OR euser=%Q)"
    "      AND objid in (SELECT rid FROM tagxref WHERE value=tmp_brlist.name))",
    zUser, zUser
  );
  if( brFlags & BRL_ORDERBY_MTIME ){
    blob_append_sql(&sql, " ORDER BY -mtime");
  }else{
    blob_append_sql(&sql, " ORDER BY name COLLATE nocase");
  }
  if( brFlags & BRL_REVERSE && !nLimitMRU ){
    blob_append_sql(&sql," DESC");
  }
  if( nLimitMRU ){
    blob_append_sql(&sql," LIMIT %d",nLimitMRU);
  }
  blob_append_sql(&sql,")"); /* OUTER QUERY */
  if( brFlags & BRL_REVERSE && nLimitMRU ){
    blob_append_sql(&sql," ORDER BY mtime"); /* OUTER QUERY */
  }
  db_prepare_blob(pQuery, &sql);
  blob_reset(&sql);
}

/*
** If the branch named in the argument is open, return a RID for one of
** the open leaves of that branch.  If the branch does not exists or is
** closed, return 0.
*/
int branch_is_open(const char *zBrName){
  return db_int(0,
    "SELECT rid FROM tagxref AS ox"
    " WHERE tagid=%d"
    "   AND tagtype=2"
    "   AND value=%Q"
    "   AND rid IN leaf"
    "   AND NOT EXISTS(SELECT 1 FROM tagxref AS ix"
                      " WHERE tagid=%d"
                      "   AND tagtype=1"
                      "   AND ox.rid=ix.rid)",
    TAG_BRANCH, zBrName, TAG_CLOSED
  );
}

/*
** Internal helper for branch_cmd_close() and friends. Adds a row to
** the to the brcmdtag TEMP table, initializing that table if needed,
** holding a pending tag for the given blob.rid (which is assumed to
** be valid). zTag must be a fully-formed tag name, including the
** (+,-,*) prefix character.
**
*/
static void branch_cmd_tag_add(int rid, const char *zTag){
  static int once = 0;
  assert(zTag && ('+'==zTag[0] || '-'==zTag[0] || '*'==zTag[0]));
  if(0==once++){
    db_multi_exec("CREATE TEMP TABLE brcmdtag("
                  "rid INTEGER UNIQUE ON CONFLICT IGNORE,"
                  "tag TEXT NOT NULL"
                  ")");
  }
  db_multi_exec("INSERT INTO brcmdtag(rid,tag) VALUES(%d,%Q)",
                rid, zTag);
}

/*
** Internal helper for branch_cmd_close() and friends. Creates and
** saves a control artifact of tag changes stored via
** branch_cmd_tag_add(). Fails fatally on error, returns 0 if it saves
** an artifact, and a negative value if it does not save anything
** because no tags were queued up. A positive return value is reserved
** for potential future semantics.
**
** This function asserts that a transaction is underway and it ends
** the transaction, committing or rolling back, as appropriate.
*/
static int branch_cmd_tag_finalize(int fDryRun /* roll back if true */,
                                   int fVerbose /* output extra info */,
                                   const char *zDateOvrd /* --date-override */,
                                   const char *zUserOvrd /* --user-override */){
  int nTags = 0;
  Stmt q = empty_Stmt;
  Blob manifest = empty_blob;
  int doRollback = fDryRun!=0;

  assert(db_transaction_nesting_depth() > 0);
  if(!db_table_exists("temp","brcmdtag")){
    fossil_warning("No tags added - nothing to do.");
    db_end_transaction(1);
    return -1;
  }
  db_prepare(&q, "SELECT b.uuid, t.tag "
             "FROM blob b, brcmdtag t "
             "WHERE b.rid=t.rid "
             "ORDER BY t.tag, b.uuid");
  blob_appendf(&manifest, "D %z\n",
               date_in_standard_format( zDateOvrd ? zDateOvrd : "now"));
  while(SQLITE_ROW==db_step(&q)){
    const char * zHash = db_column_text(&q, 0);
    const char * zTag = db_column_text(&q, 1);
    blob_appendf(&manifest, "T %s %s\n", zTag, zHash);
    ++nTags;
  }
  if(!nTags){
    fossil_warning("No tags added - nothing to do.");
    db_end_transaction(1);
    blob_reset(&manifest);
    return -1;
  }
  user_select();
  blob_appendf(&manifest, "U %F\n", zUserOvrd ? zUserOvrd : login_name());
  { /* Z-card and save artifact */
    int newRid;
    Blob cksum = empty_blob;
    md5sum_blob(&manifest, &cksum);
    blob_appendf(&manifest, "Z %b\n", &cksum);
    blob_reset(&cksum);
    if(fDryRun && fVerbose){
      fossil_print("Dry-run mode: will roll back new artifact:\n%b",
                   &manifest);
      /* Run through the saving steps, though, noting that doing so
      ** will clear out &manifest, which is why we output it here
      ** instead of after saving. */
    }
    newRid = content_put(&manifest);
    if(0==newRid){
      fossil_fatal("Problem saving new artifact: %s\n%b",
                   g.zErrMsg, &manifest);
    }else if(manifest_crosslink(newRid, &manifest, 0)==0){
      fossil_fatal("Crosslinking error: %s", g.zErrMsg);
    }
    fossil_print("Saved new control artifact %z (RID %d).\n",
                 rid_to_uuid(newRid), newRid);
    db_add_unsent(newRid);
    if(fDryRun){
      fossil_print("Dry-run mode: rolling back new artifact.\n");
      assert(0!=doRollback);
    }
  }
  db_multi_exec("DROP TABLE brcmdtag");
  blob_reset(&manifest);
  db_end_transaction(doRollback);
  return 0;
}

/*
** Internal helper for branch_cmd_close() and friends. zName is a
** symbolic check-in name. Returns the blob.rid of the check-in or fails
** fatally if the name does not resolve unambiguously.  If zUuid is
** not NULL, *zUuid is set to the resolved blob.uuid and must be freed
** by the caller via fossil_free().
*/
static int branch_resolve_name(char const *zName, char **zUuid){
  const int rid = name_to_uuid2(zName, "ci", zUuid);
  if(0==rid){
    fossil_fatal("Cannot resolve name: %s", zName);
  }else if(rid<0){
    fossil_fatal("Ambiguous name: %s", zName);
  }
  return rid;
}

/*
** Implementation of (branch hide/unhide) subcommands. nStartAtArg is
** the g.argv index to start reading branch/check-in names. fHide is
** true for hiding, false for unhiding. Fails fatally on error.
*/
static void branch_cmd_hide(int nStartAtArg, int fHide){
  int argPos = nStartAtArg;    /* g.argv pos with first branch/ci name */
  char * zUuid = 0;            /* Resolved branch UUID. */
  const int fVerbose = find_option("verbose","v",0)!=0;
  const int fDryRun = find_option("dry-run","n",0)!=0;
  const char *zDateOvrd = find_option("date-override",0,1);
  const char *zUserOvrd = find_option("user-override",0,1);

  verify_all_options();
  db_begin_transaction();
  for( ; argPos < g.argc; fossil_free(zUuid), ++argPos ){
    const char * zName = g.argv[argPos];
    const int rid = branch_resolve_name(zName, &zUuid);
    const int isHidden = rid_has_tag(rid, TAG_HIDDEN);
    /* Potential TODO: check for existing 'hidden' flag and skip this
    ** entry if it already has (if fHide) or does not have (if !fHide)
    ** that tag. FWIW, /ci_edit does not do so. */
    if(fHide && isHidden){
      fossil_warning("Skipping hidden check-in %s: %s.", zName, zUuid);
      continue;
    }else if(!fHide && !isHidden){
      fossil_warning("Skipping non-hidden check-in %s: %s.", zName, zUuid);
      continue;
    }
    branch_cmd_tag_add(rid, fHide ? "*hidden" : "-hidden");
    if(fVerbose!=0){
      fossil_print("%s check-in [%s] %s\n",
                   fHide ? "Hiding" : "Unhiding",
                   zName, zUuid);
    }
  }
  branch_cmd_tag_finalize(fDryRun, fVerbose, zDateOvrd, zUserOvrd);
}

/*
** Implementation of (branch close|reopen) subcommands. nStartAtArg is
** the g.argv index to start reading branch/check-in names. The given
** checkins are closed if fClose is true, else their "closed" tag (if
** any) is cancelled. Fails fatally on error.
*/
static void branch_cmd_close(int nStartAtArg, int fClose){
  int argPos = nStartAtArg;    /* g.argv pos with first branch name */
  char * zUuid = 0;            /* Resolved branch UUID. */
  const int fVerbose = find_option("verbose","v",0)!=0;
  const int fDryRun = find_option("dry-run","n",0)!=0;
  const char *zDateOvrd = find_option("date-override",0,1);
  const char *zUserOvrd = find_option("user-override",0,1);

  verify_all_options();
  db_begin_transaction();
  for( ; argPos < g.argc; fossil_free(zUuid), ++argPos ){
    const char * zName = g.argv[argPos];
    const int rid = branch_resolve_name(zName, &zUuid);
    const int isClosed = leaf_is_closed(rid);
    if(!is_a_leaf(rid)){
      /* This behaviour is different from /ci_edit closing, where
      ** is_a_leaf() adds a "+" tag and !is_a_leaf() adds a "*"
      ** tag. We might want to change this to match for consistency's
      ** sake, but it currently seems unnecessary to close/re-open a
      ** non-leaf. */
      fossil_warning("Skipping non-leaf [%s] %s", zName, zUuid);
      continue;
    }else if(fClose && isClosed){
      fossil_warning("Skipping closed leaf [%s] %s", zName, zUuid);
      continue;
    }else if(!fClose && !isClosed){
      fossil_warning("Skipping non-closed leaf [%s] %s", zName, zUuid);
      continue;
    }
    branch_cmd_tag_add(rid, fClose ? "+closed" : "-closed");
    if(fVerbose!=0){
      fossil_print("%s branch [%s] %s\n",
                   fClose ? "Closing" : "Re-opening",
                   zName, zUuid);
    }
  }
  branch_cmd_tag_finalize(fDryRun, fVerbose, zDateOvrd, zUserOvrd);
}

/*
** COMMAND: branch
**
** Usage: %fossil branch SUBCOMMAND ... ?OPTIONS?
**
** Run various subcommands to manage branches of the open repository or
** of the repository identified by the -R or --repository option.
**
** > fossil branch close|reopen ?OPTIONS? BRANCH-NAME ?...BRANCH-NAMES?
**
**       Adds or cancels the "closed" tag to one or more branches.
**       It accepts arbitrary unambiguous symbolic names but
**       will only resolve check-in names and skips any which resolve
**       to non-leaf check-ins.
**
**       Options:
**         -n|--dry-run          Do not commit changes, but dump artifact
**                               to stdout
**         -v|--verbose          Output more information
**         --date-override DATE  DATE to use instead of 'now'
**         --user-override USER  USER to use instead of the current default
**
** > fossil branch current
**
**        Print the name of the branch for the current check-out
**
** > fossil branch hide|unhide ?OPTIONS? BRANCH-NAME ?...BRANCH-NAMES?
**
**       Adds or cancels the "hidden" tag for the specified branches or
**       or check-in IDs. Accepts the same options as the close
**       subcommand.
**
** > fossil branch info BRANCH-NAME
**
**        Print information about a branch
**
** > fossil branch list|ls ?OPTIONS? ?GLOB?
** > fossil branch lsh ?OPTIONS? ?LIMIT?
**
**        List all branches.
**
**        Options:
**          -a|--all         List all branches.  Default show only open branches
**          -c|--closed      List closed branches
**          -m|--merged      List branches merged into the current branch
**          -M|--unmerged    List branches not merged into the current branch
**          -p               List only private branches
**          -r               Reverse the sort order
**          -t               Show recently changed branches first
**          --self           List only branches where you participate
**          --username USER  List only branches where USER participates
**          --users N        List up to N users participating
**
**        The current branch is marked with an asterisk.  Private branches are
**        marked with a hash sign.
**
**        If GLOB is given, show only branches matching the pattern.
**
**        The "lsh" variant of this subcommand shows recently changed branches,
**        and accepts an optional LIMIT argument (defaults to 5) to cap output,
**        but no GLOB argument.  All other options are supported, with -t being
**        an implied no-op.
**
** > fossil branch new BRANCH-NAME BASIS ?OPTIONS?
**
**        Create a new branch BRANCH-NAME off of check-in BASIS.
**
**        This command is available for people who want to create a branch
**        in advance.  But  the use of this command is discouraged.  The
**        preferred idiom in Fossil is to create new branches at the point
**        of need, using the "--branch NAME" option to the "fossil commit"
**        command.
**
**        Options:
**          --private             Branch is private (i.e., remains local)
**          --bgcolor COLOR       Use COLOR instead of automatic background
**          --nosign              Do not sign the manifest for the check-in
**                                that creates this branch
**          --nosync              Do not auto-sync prior to creating the branch
**          --date-override DATE  DATE to use instead of 'now'
**          --user-override USER  USER to use instead of the current default
**
** Options:
**    -R|--repository REPO       Run commands on repository REPO
*/
void branch_cmd(void){
  int n;
  const char *zCmd = "list";
  db_find_and_open_repository(0, 0);
  if( g.argc>=3 ) zCmd = g.argv[2];
  n = strlen(zCmd);
  if( strncmp(zCmd,"current",n)==0 ){
    if( !g.localOpen ){
      fossil_fatal("not within an open check-out");
    }else{
      int vid = db_lget_int("checkout", 0);
      char *zCurrent = db_text(0, "SELECT value FROM tagxref"
                            " WHERE rid=%d AND tagid=%d", vid, TAG_BRANCH);
      fossil_print("%s\n", zCurrent);
      fossil_free(zCurrent);
    }
  }else if( strncmp(zCmd,"info",n)==0 ){
    int i;
    for(i=3; i<g.argc; i++){
      const char *zBrName = g.argv[i];
      int rid = branch_is_open(zBrName);
      if( rid==0 ){
        fossil_print("%s: not an open branch\n", zBrName);
      }else{
        const char *zUuid = db_text(0,"SELECT uuid FROM blob WHERE rid=%d",rid);
        const char *zDate = db_text(0,
          "SELECT datetime(mtime,toLocal()) FROM event"
          " WHERE objid=%d", rid);
        fossil_print("%s: open as of %s on %.16s\n", zBrName, zDate, zUuid);
      }
    }
  }else if( strncmp(zCmd,"list",n)==0 ||
            strncmp(zCmd, "ls", n)==0 ||
            strcmp(zCmd, "lsh")==0 ){
    Stmt q;
    Blob txt = empty_blob;
    int vid;
    char *zCurrent = 0;
    const char *zBrNameGlob = 0;
    const char *zUser = find_option("username",0,1);
    const char *zUsersOpt = find_option("users",0,1);
    int nUsers = zUsersOpt ? atoi(zUsersOpt) : 0;
    int nLimit = 0;
    int brFlags = BRL_OPEN_ONLY;
    if( find_option("all","a",0)!=0 ) brFlags = BRL_BOTH;
    if( find_option("closed","c",0)!=0 ) brFlags = BRL_CLOSED_ONLY;
    if( find_option("t",0,0)!=0 ) brFlags |= BRL_ORDERBY_MTIME;
    if( find_option("r",0,0)!=0 ) brFlags |= BRL_REVERSE;
    if( find_option("p",0,0)!=0 ) brFlags |= BRL_PRIVATE;
    if( find_option("merged","m",0)!=0 ) brFlags |= BRL_MERGED;
    if( find_option("unmerged","M",0)!=0 ) brFlags |= BRL_UNMERGED;
    if( find_option("self",0,0)!=0 ){
      if( zUser ){
        fossil_fatal("flags --username and --self are mutually exclusive");
      }
      user_select();
      zUser = login_name();
    }
    verify_all_options();

    if ( (brFlags & BRL_MERGED) && (brFlags & BRL_UNMERGED) ){
      fossil_fatal("flags --merged and --unmerged are mutually exclusive");
    }
    if( zUsersOpt ){
      if( nUsers <= 0) fossil_fatal("With --users, N must be positive");
      brFlags |= BRL_LIST_USERS;
    }
    if( strcmp(zCmd, "lsh")==0 ){
      nLimit = 5;
      if( g.argc>4 || (g.argc==4 && (nLimit = atoi(g.argv[3]))==0) ){
        fossil_fatal("the lsh subcommand allows one optional numeric argument");
      }
      brFlags |= BRL_ORDERBY_MTIME;
    }else{
      if( g.argc >= 4 ) zBrNameGlob = g.argv[3];
    }

    if( g.localOpen ){
      vid = db_lget_int("checkout", 0);
      zCurrent = db_text(0, "SELECT value FROM tagxref"
                            " WHERE rid=%d AND tagid=%d", vid, TAG_BRANCH);
    }
    branch_prepare_list_query(&q, brFlags, zBrNameGlob, nLimit, zUser);
    blob_init(&txt, 0, 0);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zBr = db_column_text(&q, 0);
      int isPriv = db_column_int(&q, 1)==1;
      const char *zMergeTo = db_column_text(&q, 2);
      int isCur = zCurrent!=0 && fossil_strcmp(zCurrent,zBr)==0;
      const char *zUsers = db_column_text(&q, 3);
      if( (brFlags & BRL_MERGED) && fossil_strcmp(zCurrent,zMergeTo)!=0 ){
        continue;
      }
      if( (brFlags & BRL_UNMERGED) && (fossil_strcmp(zCurrent,zMergeTo)==0
          || isCur) ){
        continue;
      }
      blob_appendf(&txt, "%s%s%s",
        ( (brFlags & BRL_PRIVATE) ? " " : ( isPriv ? "#" : " ") ),
        (isCur ? "* " : "  "), zBr);
      if( nUsers ){
        char c;
        const char *cp;
        const char *pComma = 0;
        int commas = 0;
        for( cp = zUsers; ( c = *cp ) != 0; cp++ ){
          if( c == ',' ){
            commas++;
            if( commas == nUsers ) pComma = cp;
          }
        }
        if( pComma ){
          blob_appendf(&txt, " (%.*s,... %i more)",
            pComma - zUsers, zUsers, commas + 1 - nUsers);
        }else{
          blob_appendf(&txt, " (%s)", zUsers);
        }
      }
      fossil_print("%s\n", blob_str(&txt));
      blob_reset(&txt);
    }
    db_finalize(&q);
  }else if( strncmp(zCmd,"new",n)==0 ){
    branch_new();
  }else if( strncmp(zCmd,"close",5)==0 ){
    if(g.argc<4){
      usage("close branch-name(s)...");
    }
    branch_cmd_close(3, 1);
  }else if( strncmp(zCmd,"reopen",6)==0 ){
    if(g.argc<4){
      usage("reopen branch-name(s)...");
    }
    branch_cmd_close(3, 0);
  }else if( strncmp(zCmd,"hide",4)==0 ){
    if(g.argc<4){
      usage("hide branch-name(s)...");
    }
    branch_cmd_hide(3,1);
  }else if( strncmp(zCmd,"unhide",6)==0 ){
    if(g.argc<4){
      usage("unhide branch-name(s)...");
    }
    branch_cmd_hide(3,0);
  }else{
    fossil_fatal("branch subcommand should be one of: "
                 "close current hide info list ls lsh new reopen unhide");
  }
}

/*
** This is the new-style branch-list page that shows the branch names
** together with their ages (time of last check-in) and whether or not
** they are closed or merged to another branch.
**
** Control jumps to this routine from brlist_page() (the /brlist handler)
** if there are no query parameters.
*/
static void new_brlist_page(void){
  Stmt q;
  double rNow;
  int show_colors = PB("colors");
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_set_current_feature("branch");
  style_header("Branches");
  style_adunit_config(ADUNIT_RIGHT_OK);
  style_submenu_checkbox("colors", "Use Branch Colors", 0, 0);
  login_anonymous_available();

  brlist_create_temp_table();
  db_prepare(&q, "SELECT * FROM tmp_brlist ORDER BY mtime DESC");
  rNow = db_double(0.0, "SELECT julianday('now')");
  @ <script id="brlist-data" type="application/json">\
  @ {"timelineUrl":"%R/timeline"}</script>
  @ <div class="brlist">
  @ <table class='sortable' data-column-types='tkNtt' data-init-sort='2'>
  @ <thead><tr>
  @ <th>Branch Name</th>
  @ <th>Last Change</th>
  @ <th>Check-ins</th>
  @ <th>Status</th>
  @ <th>Resolution</th>
  @ </tr></thead><tbody>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zBranch = db_column_text(&q, 0);
    double rMtime = db_column_double(&q, 1);
    int isClosed = db_column_int(&q, 2);
    const char *zMergeTo = db_column_text(&q, 3);
    int nCkin = db_column_int(&q, 4);
    const char *zLastCkin = db_column_text(&q, 5);
    const char *zBgClr = db_column_text(&q, 6);
    char *zAge = human_readable_age(rNow - rMtime);
    sqlite3_int64 iMtime = (sqlite3_int64)(rMtime*86400.0);
    if( zMergeTo && zMergeTo[0]==0 ) zMergeTo = 0;
    if( zBgClr ) zBgClr = reasonable_bg_color(zBgClr, 0);
    if( zBgClr==0 ){
      if( zBranch==0 || strcmp(zBranch,"trunk")==0 ){
        zBgClr = 0;
      }else{
        zBgClr = hash_color(zBranch);
      }
    }
    if( zBgClr && zBgClr[0] && show_colors ){
      @ <tr style="background-color:%s(zBgClr)">
    }else{
      @ <tr>
    }
    @ <td>%z(href("%R/timeline?r=%T",zBranch))%h(zBranch)</a><input
    @  type="checkbox" disabled="disabled"/></td>
    @ <td data-sortkey="%016llx(iMtime)">%s(zAge)</td>
    @ <td>%d(nCkin)</td>
    fossil_free(zAge);
    @ <td>%s(isClosed?"closed":"")</td>
    if( zMergeTo ){
      @ <td>merged into
      @ %z(href("%R/timeline?f=%!S",zLastCkin))%h(zMergeTo)</a></td>
    }else{
      @ <td></td>
    }
    @ </tr>
  }
  @ </tbody></table></div>
  db_finalize(&q);
  builtin_request_js("fossil.page.brlist.js");
  style_table_sorter();
  style_finish_page();
}

/*
** WEBPAGE: brlist
** Show a list of branches.  With no query parameters, a sortable table
** is used to show all branches.  If query parameters are present a
** fixed bullet list is shown.
**
** Query parameters:
**
**     all         Show all branches
**     closed      Show only closed branches
**     open        Show only open branches
**     colortest   Show all branches with automatic color
**
** When there are no query parameters, a new-style /brlist page shows
** all branches in a sortable table.  The new-style /brlist page is
** preferred and is the default.
*/
void brlist_page(void){
  Stmt q;
  int cnt;
  int showClosed = P("closed")!=0;
  int showAll = P("all")!=0;
  int showOpen = P("open")!=0;
  int colorTest = P("colortest")!=0;
  int brFlags = BRL_OPEN_ONLY;

  if( showClosed==0 && showAll==0 && showOpen==0 && colorTest==0 ){
    new_brlist_page();
    return;
  }
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  cgi_check_for_malice();
  if( colorTest ){
    showClosed = 0;
    showAll = 1;
  }
  if( showAll ) brFlags = BRL_BOTH;
  if( showClosed ) brFlags = BRL_CLOSED_ONLY;

  style_set_current_feature("branch");
  style_header("%s", showClosed ? "Closed Branches" :
                        showAll ? "All Branches" : "Open Branches");
  style_submenu_element("Timeline", "brtimeline");
  if( showClosed ){
    style_submenu_element("All", "brlist?all");
    style_submenu_element("Open", "brlist?open");
  }else if( showAll ){
    style_submenu_element("Closed", "brlist?closed");
    style_submenu_element("Open", "brlist");
  }else{
    style_submenu_element("All", "brlist?all");
    style_submenu_element("Closed", "brlist?closed");
  }
  if( !colorTest ){
    style_submenu_element("Color-Test", "brlist?colortest");
  }else{
    style_submenu_element("All", "brlist?all");
  }
  login_anonymous_available();
#if 0
  style_sidebox_begin("Nomenclature:", "33%");
  @ <ol>
  @ <li> An <div class="sideboxDescribed">%z(href("brlist"))
  @ open branch</a></div> is a branch that has one or more
  @ <div class="sideboxDescribed">%z(href("leaves"))open leaves.</a></div>
  @ The presence of open leaves presumably means
  @ that the branch is still being extended with new check-ins.</li>
  @ <li> A <div class="sideboxDescribed">%z(href("brlist?closed"))
  @ closed branch</a></div> is a branch with only
  @ <div class="sideboxDescribed">%z(href("leaves?closed"))
  @ closed leaves</a></div>.
  @ Closed branches are fixed and do not change (unless they are first
  @ reopened).</li>
  @ </ol>
  style_sidebox_end();
#endif

  branch_prepare_list_query(&q, brFlags, 0, 0, 0);
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zBr = db_column_text(&q, 0);
    if( cnt==0 ){
      if( colorTest ){
        @ <h2>Default background colors for all branches:</h2>
      }else if( showClosed ){
        @ <h2>Closed Branches:</h2>
      }else if( showAll ){
        @ <h2>All Branches:</h2>
      }else{
        @ <h2>Open Branches:</h2>
      }
      @ <ul>
      cnt++;
    }
    if( colorTest ){
      const char *zColor = hash_color(zBr);
      @ <li><span style="background-color: %s(zColor)">
      @ %h(zBr) &rarr; %s(zColor)</span></li>
    }else{
      @ <li>%z(href("%R/timeline?r=%T",zBr))%h(zBr)</a></li>
    }
  }
  if( cnt ){
    @ </ul>
  }
  db_finalize(&q);
  style_finish_page();
}

/*
** This routine is called while for each check-in that is rendered by
** the timeline of a "brlist" page.  Add some additional hyperlinks
** to the end of the line.
*/
static void brtimeline_extra(int rid){
  Stmt q;
  if( !g.perm.Hyperlink ) return;
  db_prepare(&q,
    "SELECT substr(tagname,5) FROM tagxref, tag"
    " WHERE tagxref.rid=%d"
    "   AND tagxref.tagid=tag.tagid"
    "   AND tagxref.tagtype>0"
    "   AND tag.tagname GLOB 'sym-*'",
    rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTagName = db_column_text(&q, 0);
    @  %z(href("%R/timeline?r=%T",zTagName))[timeline]</a>
  }
  db_finalize(&q);
}

/*
** WEBPAGE: brtimeline
**
** Show a timeline of all branches
**
** Query parameters:
**
**     ng            No graph
**     nohidden      Hide check-ins with "hidden" tag
**     onlyhidden    Show only check-ins with "hidden" tag
**     brbg          Background color by branch name
**     ubg           Background color by user name
*/
void brtimeline_page(void){
  Blob sql = empty_blob;
  Stmt q;
  int tmFlags;                            /* Timeline display flags */
  int fNoHidden = PB("nohidden")!=0;      /* The "nohidden" query parameter */
  int fOnlyHidden = PB("onlyhidden")!=0;  /* The "onlyhidden" query parameter */

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }

  style_set_current_feature("branch");
  style_header("Branches");
  style_submenu_element("List", "brlist");
  login_anonymous_available();
  timeline_ss_submenu();
  cgi_check_for_malice();
  @ <h2>The initial check-in for each branch:</h2>
  blob_append(&sql, timeline_query_for_www(), -1);
  blob_append_sql(&sql,
    "AND blob.rid IN (SELECT rid FROM tagxref"
    "                  WHERE tagtype>0 AND tagid=%d AND srcid!=0)", TAG_BRANCH);
  if( fNoHidden || fOnlyHidden ){
    const char* zUnaryOp = fNoHidden ? "NOT" : "";
    blob_append_sql(&sql,
      " AND %s EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)\n",
      zUnaryOp/*safe-for-%s*/, TAG_HIDDEN);
  }
  db_prepare(&q, "%s ORDER BY event.mtime DESC", blob_sql_text(&sql));
  blob_reset(&sql);
  /* Always specify TIMELINE_DISJOINT, or graph_finish() may fail because of too
  ** many descenders to (off-screen) parents. */
  tmFlags = TIMELINE_DISJOINT | TIMELINE_NOSCROLL;
  if( PB("ng")==0 ) tmFlags |= TIMELINE_GRAPH;
  if( PB("brbg")!=0 ) tmFlags |= TIMELINE_BRCOLOR;
  if( PB("ubg")!=0 ) tmFlags |= TIMELINE_UCOLOR;
  www_print_timeline(&q, tmFlags, 0, 0, 0, 0, 0, brtimeline_extra);
  db_finalize(&q);
  style_finish_page();
}
