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
  Blob parent;           /* root check-in manifest */
  Manifest mParent;      /* Parsed parent manifest */
  Blob mcksum;           /* Self-checksum on the manifest */
 
  noSign = find_option("nosign","",0)!=0;
  zColor = find_option("bgcolor","c",1);
  verify_all_options();
  if( g.argc<5 ){
    usage("new BRANCH-NAME CHECK-IN ?-bgcolor COLOR?");
  }
  db_find_and_open_repository(1);  
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

  /* Create a manifest for the new branch */
  blob_zero(&branch);
  zComment = mprintf("Create new branch named \"%h\"", zBranch);
  blob_appendf(&branch, "C %F\n", zComment);
  zDate = db_text(0, "SELECT datetime('now')");
  zDate[10] = 'T';
  blob_appendf(&branch, "D %s\n", zDate);

  /* Copy all of the content from the parent into the branch */
  content_get(rootid, &parent);
  manifest_parse(&mParent, &parent);
  if( mParent.type!=CFTYPE_MANIFEST ){
    fossil_fatal("%s is not a valid check-in", g.argv[4]);
  }
  for(i=0; i<mParent.nFile; ++i){
    if( mParent.aFile[i].zPerm[0] ){
      blob_appendf(&branch, "F %F %s %s\n",
                   mParent.aFile[i].zName,
                   mParent.aFile[i].zUuid,
                   mParent.aFile[i].zPerm);
    }else{
      blob_appendf(&branch, "F %F %s\n",
                   mParent.aFile[i].zName,
                   mParent.aFile[i].zUuid);
    }
  }
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rootid);
  blob_appendf(&branch, "P %s\n", zUuid);
  blob_appendf(&branch, "R %s\n", mParent.zRepoCksum);
  manifest_clear(&mParent);

  /* Add the symbolic branch name and the "branch" tag to identify
  ** this as a new branch */
  if( zColor!=0 ){
    blob_appendf(&branch, "T *bgcolor * %F\n", zColor);
  }
  blob_appendf(&branch, "T *branch * %F\n", zBranch);
  blob_appendf(&branch, "T *sym-%F *\n", zBranch);

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
  
  blob_appendf(&branch, "U %F\n", g.zLogin);
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

  brid = content_put(&branch, 0, 0);
  if( brid==0 ){
    fossil_panic("trouble committing manifest: %s", g.zErrMsg);
  }
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", brid);
  if( manifest_crosslink(brid, &branch)==0 ){
    fossil_panic("unable to install new manifest");
  }
  content_deltify(rootid, brid, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", brid);
  printf("New branch: %s\n", zUuid);
  if( g.argc==3 ){
    printf(
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
**    %fossil branch new BRANCH-NAME BASIS ?-bgcolor COLOR? 
**
**        Create a new branch BRANCH-NAME off of check-in BASIS.
**        You can optionally give the branch a default color.
**
**    %fossil branch list
**
**        List all branches
**
*/
void branch_cmd(void){
  int n;
  db_find_and_open_repository(1);
  if( g.argc<3 ){
    usage("new|list ...");
  }
  n = strlen(g.argv[2]);
  if( n>=2 && strncmp(g.argv[2],"new",n)==0 ){
    branch_new();
  }else if( n>=2 && strncmp(g.argv[2],"list",n)==0 ){
    Stmt q;
    db_prepare(&q,
      "%s"
      "   AND blob.rid IN (SELECT rid FROM tagxref"
      "                     WHERE tagid=%d AND tagtype==2 AND srcid!=0)"
      " ORDER BY event.mtime DESC",
      timeline_query_for_tty(), TAG_BRANCH
    );
    print_timeline(&q, 2000);
    db_finalize(&q);
  }else{
    fossil_panic("branch subcommand should be one of: "
                 "new list");
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
  compute_leaves(0, 1);
  style_sidebox_begin("Nomenclature:", "33%");
  @ <ol>
  @ <li> An <a href="brlist">open branch</a> is a branch that has one or
  @ more <a href="leaves">open leaves.</a>
  @ The presence of open leaves presumably means
  @ that the branch is still being extended with new check-ins.</li>
  @ <li> A <a href="brlist?closed">closed branch</a> is a branch with only
  @ <a href="leaves?closed">closed leaves</a>.
  @ Closed branches are fixed and do not change (unless they are first
  @ reopened)</li>
  @ </ol>
  style_sidebox_end();

  cnt = 0;
  if( !showClosed ){
    db_prepare(&q,
      "SELECT DISTINCT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      "   AND rid IN leaves"
      " ORDER BY value /*sort*/",
      TAG_BRANCH
    );
  }else{
    db_prepare(&q,
      "SELECT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      " EXCEPT "
      "SELECT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      "   AND rid IN leaves"
      " ORDER BY value /*sort*/",
      TAG_BRANCH, TAG_BRANCH
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
      @ <li><a href="%s(g.zBaseURL)/timeline?r=%T(zBr)">%h(zBr)</a></li>
    }else{
      @ <li><b>%h(zBr)</b></li>
    }
  }
  if( cnt ){
    @ </ul>
  }
  db_finalize(&q);
  @ </ul>
  @ <br clear="both">
  @ <script>
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
    @ <a href="%s(g.zBaseURL)/timeline?r=%T(zTagName)">[timeline]</a>
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
  www_print_timeline(&q, 0, brtimeline_extra);
  db_finalize(&q);
  @ <br clear="both">
  @ <script>
  @ function xin(id){
  @ }
  @ function xout(id){
  @ }
  @ </script>
  style_footer();
}
