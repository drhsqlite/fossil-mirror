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
**  fossil branch new    BRANCH-NAME ?ORIGIN-CHECK-IN? ?-bgcolor COLOR?
**  argv0  argv1  argv2  argv3       argv4
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
  zColor = find_option("bgcolor","c",1);
  isPrivate = find_option("private",0,0)!=0;
  zDateOvrd = find_option("date-override",0,1);
  zUserOvrd = find_option("user-override",0,1);
  verify_all_options();
  if( g.argc<5 ){
    usage("new BRANCH-NAME CHECK-IN ?-bgcolor COLOR?");
  }
  db_find_and_open_repository(0, 0);
  noSign = db_get_int("omitsign", 0)|noSign;

  /* fossil branch new name */
  zBranch = g.argv[3];
  if( zBranch==0 || zBranch[0]==0 ){
    fossil_panic("branch name cannot be empty");
  }
  if( db_exists(
        "SELECT 1 FROM tagxref"
        " WHERE tagtype>0"
        "   AND tagid=(SELECT tagid FROM tag WHERE tagname='sym-%s')",
        zBranch)!=0 ){
    fossil_fatal("branch \"%s\" already exists", zBranch);
  }

  user_select();
  db_begin_transaction();
  rootid = name_to_rid(g.argv[4]);
  if( rootid==0 ){
    fossil_fatal("unable to locate check-in off of which to branch");
  }

  pParent = manifest_get(rootid, CFTYPE_MANIFEST);
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
  if( isPrivate && zColor==0 ) zColor = "#fec084";
  if( zColor!=0 ){
    blob_appendf(&branch, "T *bgcolor * %F\n", zColor);
  }
  blob_appendf(&branch, "T *branch * %F\n", zBranch);
  blob_appendf(&branch, "T *sym-%F *\n", zBranch);
  if( isPrivate ){
    blob_appendf(&branch, "T +private *\n");
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

  blob_appendf(&branch, "U %F\n", zUserOvrd ? zUserOvrd : g.zLogin);
  md5sum_blob(&branch, &mcksum);
  blob_appendf(&branch, "Z %b\n", &mcksum);
  if( !noSign && clearsign(&branch, &branch) ){
    Blob ans;
    blob_zero(&ans);
    prompt_user("unable to sign manifest.  continue (y/N)? ", &ans);
    if( blob_str(&ans)[0]!='y' ){
      db_end_transaction(1);
      fossil_exit(1);
    }
  }

  brid = content_put(&branch);
  if( brid==0 ){
    fossil_panic("trouble committing manifest: %s", g.zErrMsg);
  }
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", brid);
  if( manifest_crosslink(brid, &branch)==0 ){
    fossil_panic("unable to install new manifest");
  }
  assert( blob_is_reset(&branch) );
  content_deltify(rootid, brid, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", brid);
  fossil_print("New branch: %s\n", zUuid);
  if( g.argc==3 ){
    fossil_print(
      "\n"
      "Note: the local check-out has not been updated to the new\n"
      "      branch.  To begin working on the new branch, do this:\n"
      "\n"
      "      %s update %s\n",
      fossil_nameofexe(), zBranch
    );
  }


  /* Commit */
  db_end_transaction(0);

  /* Do an autosync push, if requested */
  autosync(AUTOSYNC_PUSH);
}

/*
** COMMAND: branch
**
** Usage: %fossil branch SUBCOMMAND ... ?-R|--repository FILE?
**
** Run various subcommands to manage branches of the open repository or
** of the repository identified by the -R or --repository option.
**
**    %fossil branch new BRANCH-NAME BASIS ?--bgcolor COLOR? ?--private?
**
**        Create a new branch BRANCH-NAME off of check-in BASIS.
**        You can optionally give the branch a default color.  The
**        --private option makes the branch private.
**
**    %fossil branch list
**    %fossil branch ls
**
**        List all branches
**
**
** SUMMARY:     fossil branch subcommand ...
** Subcommands: new branchname basis ?-R|-repository file? ?--bgcolor color?
**                  ?--private?
** Or:          list, ls
*/
void branch_cmd(void){
  int n;
  const char *zCmd = "list";
  db_find_and_open_repository(0, 0);
  if( g.argc<2 ){
    usage("new|list|ls ...");
  }
  if( g.argc>=3 ) zCmd = g.argv[2];
  n = strlen(zCmd);
  if( strncmp(zCmd,"new",n)==0 ){
    branch_new();
  }else if( (strncmp(zCmd,"list",n)==0)||(strncmp(zCmd, "ls", n)==0) ){
    Stmt q;
    int vid;
    char *zCurrent = 0;

    if( g.localOpen ){
      vid = db_lget_int("checkout", 0);
      zCurrent = db_text(0, "SELECT value FROM tagxref"
                            " WHERE rid=%d AND tagid=%d", vid, TAG_BRANCH);
    }
    db_prepare(&q,
      "SELECT DISTINCT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      "   AND rid IN leaf"
      "   AND NOT %z"
      " ORDER BY value /*sort*/",
      TAG_BRANCH, leaf_is_closed_sql("tagxref.rid")
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zBr = db_column_text(&q, 0);
      int isCur = zCurrent!=0 && fossil_strcmp(zCurrent,zBr)==0;
      fossil_print("%s%s\n", (isCur ? "* " : "  "), zBr);
    }
    db_finalize(&q);
  }else{
    fossil_panic("branch subcommand should be one of: "
                 "new list ls");
  }
}

/*
** WEBPAGE: brlist
**
** Show a timeline of all branches
*/
void brlist_page(void){
  Stmt q;
  int cnt;
  int showClosed = P("closed")!=0;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }

  style_header(showClosed ? "Closed Branches" : "Open Branches");
  style_submenu_element("Timeline", "Timeline", "brtimeline");
  if( showClosed ){
    style_submenu_element("Open","Open","brlist");
  }else{
    style_submenu_element("Closed","Closed","brlist?closed");
  }
  login_anonymous_available();
  style_sidebox_begin("Nomenclature:", "33%");
  @ <ol>
  @ <li> An <div class="sideboxDescribed"><a href="brlist">
  @ open branch</a></div> is a branch that has one or
  @ more <a href="leaves">open leaves.</a>
  @ The presence of open leaves presumably means
  @ that the branch is still being extended with new check-ins.</li>
  @ <li> A <div class="sideboxDescribed"><a href="brlist?closed">
  @ closed branch</a></div> is a branch with only
  @ <div class="sideboxDescribed"><a href="leaves?closed">
  @ closed leaves</a></div>.
  @ Closed branches are fixed and do not change (unless they are first
  @ reopened)</li>
  @ </ol>
  style_sidebox_end();

  cnt = 0;
  if( showClosed ){
    db_prepare(&q,
      "SELECT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL "
      "EXCEPT "
      "SELECT value FROM tagxref"
      " WHERE tagid=%d"
      "   AND rid IN leaf"
      "   AND NOT %z"
      " ORDER BY value /*sort*/",
      TAG_BRANCH, TAG_BRANCH, leaf_is_closed_sql("tagxref.rid")
    );
  }else{
    db_prepare(&q,
      "SELECT DISTINCT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      "   AND rid IN leaf"
      "   AND NOT %z"
      " ORDER BY value /*sort*/",
      TAG_BRANCH, leaf_is_closed_sql("tagxref.rid")
    );
  }
  while( db_step(&q)==SQLITE_ROW ){
    const char *zBr = db_column_text(&q, 0);
    if( cnt==0 ){
      if( showClosed ){
        @ <h2>Closed Branches:</h2>
      }else{
        @ <h2>Open Branches:</h2>
      }
      @ <ul>
      cnt++;
    }
    if( g.okHistory ){
      @ <li><a href="%s(g.zTop)/timeline?r=%T(zBr)">%h(zBr)</a></li>
    }else{
      @ <li><b>%h(zBr)</b></li>
    }
  }
  if( cnt ){
    @ </ul>
  }
  db_finalize(&q);
  @ <script  type="text/JavaScript">
  @ function xin(id){
  @ }
  @ function xout(id){
  @ }
  @ </script>
  style_footer();
}

/*
** This routine is called while for each check-in that is rendered by
** the timeline of a "brlist" page.  Add some additional hyperlinks
** to the end of the line.
*/
static void brtimeline_extra(int rid){
  Stmt q;
  if( !g.okHistory ) return;
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
    @ <a href="%s(g.zTop)/timeline?r=%T(zTagName)">[timeline]</a>
  }
  db_finalize(&q);
}

/*
** WEBPAGE: brtimeline
**
** Show a timeline of all branches
*/
void brtimeline_page(void){
  Stmt q;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }

  style_header("Branches");
  style_submenu_element("List", "List", "brlist");
  login_anonymous_available();
  @ <h2>The initial check-in for each branch:</h2>
  db_prepare(&q,
    "%s AND blob.rid IN (SELECT rid FROM tagxref"
    "                     WHERE tagtype>0 AND tagid=%d AND srcid!=0)"
    " ORDER BY event.mtime DESC",
    timeline_query_for_www(), TAG_BRANCH
  );
  www_print_timeline(&q, 0, 0, 0, brtimeline_extra);
  db_finalize(&q);
  @ <script  type="text/JavaScript">
  @ function xin(id){
  @ }
  @ function xout(id){
  @ }
  @ </script>
  style_footer();
}
