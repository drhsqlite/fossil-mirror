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
**  fossil branch new    NAME BASIS ?OPTIONS?
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
  zColor = find_option("bgcolor","c",1);
  isPrivate = find_option("private",0,0)!=0;
  zDateOvrd = find_option("date-override",0,1);
  zUserOvrd = find_option("user-override",0,1);
  verify_all_options();
  if( g.argc<5 ){
    usage("new BRANCH-NAME BASIS ?OPTIONS?");
  }
  db_find_and_open_repository(0, 0);
  noSign = db_get_int("omitsign", 0)|noSign;

  /* fossil branch new name */
  zBranch = g.argv[3];
  if( zBranch==0 || zBranch[0]==0 ){
    fossil_fatal("branch name cannot be empty");
  }
  if( db_exists(
        "SELECT 1 FROM tagxref"
        " WHERE tagtype>0"
        "   AND tagid=(SELECT tagid FROM tag WHERE tagname='sym-%q')",
        zBranch)!=0 ){
    fossil_fatal("branch \"%s\" already exists", zBranch);
  }

  user_select();
  db_begin_transaction();
  rootid = name_to_typed_rid(g.argv[4], "ci");
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
    char cReply;
    blob_zero(&ans);
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
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", brid);
  if( run_common_script() || manifest_crosslink(brid, &branch)==0 ){
    fossil_fatal("%s\n", g.zErrMsg);
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
      g.argv[0], zBranch
    );
  }


  /* Commit */
  db_end_transaction(0);

  /* Do an autosync push, if requested */
  if( !isPrivate ) autosync(SYNC_PUSH);
}

/*
** Prepare a query that will list branches.
**
** If (which<0) then the query pulls only closed branches. If
** (which>0) then the query pulls all (closed and opened)
** branches. Else the query pulls currently-opened branches.
*/
void branch_prepare_list_query(Stmt *pQuery, int which ){
  if( which < 0 ){
    db_prepare(pQuery,
      "SELECT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL "
      "EXCEPT "
      "SELECT value FROM tagxref"
      " WHERE tagid=%d"
      "   AND rid IN leaf"
      "   AND NOT %z"
      " ORDER BY value COLLATE nocase /*sort*/",
      TAG_BRANCH, TAG_BRANCH, leaf_is_closed_sql("tagxref.rid")
    );
  }else if( which>0 ){
    db_prepare(pQuery,
      "SELECT DISTINCT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      "   AND rid IN leaf"
      " ORDER BY value COLLATE nocase /*sort*/",
      TAG_BRANCH
    );
  }else{
    db_prepare(pQuery,
      "SELECT DISTINCT value FROM tagxref"
      " WHERE tagid=%d AND value NOT NULL"
      "   AND rid IN leaf"
      "   AND NOT %z"
      " ORDER BY value COLLATE nocase /*sort*/",
      TAG_BRANCH, leaf_is_closed_sql("tagxref.rid")
    );
  }
}


/*
** COMMAND: branch
**
** Usage: %fossil branch SUBCOMMAND ... ?OPTIONS?
**
** Run various subcommands to manage branches of the open repository or
** of the repository identified by the -R or --repository option.
**
**    %fossil branch new BRANCH-NAME BASIS ?OPTIONS?
**
**        Create a new branch BRANCH-NAME off of check-in BASIS.
**        Supported options for this subcommand include:
**        --private             branch is private (i.e., remains local)
**        --bgcolor COLOR       use COLOR instead of automatic background
**        --nosign              do not sign contents on this branch
**        --date-override DATE  DATE to use instead of 'now'
**        --user-override USER  USER to use instead of the current default
**
**    %fossil branch list ?-a|--all|-c|--closed?
**    %fossil branch ls ?-a|--all|-c|--closed?
**
**        List all branches.  Use -a or --all to list all branches and
**        -c or --closed to list all closed branches.  The default is to
**        show only open branches.
**
** Options:
**    -R|--repository FILE       Run commands on repository FILE
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
    int showAll = find_option("all","a",0)!=0;
    int showClosed = find_option("closed","c",0)!=0;

    if( g.localOpen ){
      vid = db_lget_int("checkout", 0);
      zCurrent = db_text(0, "SELECT value FROM tagxref"
                            " WHERE rid=%d AND tagid=%d", vid, TAG_BRANCH);
    }
    branch_prepare_list_query(&q, showAll?1:(showClosed?-1:0));
    while( db_step(&q)==SQLITE_ROW ){
      const char *zBr = db_column_text(&q, 0);
      int isCur = zCurrent!=0 && fossil_strcmp(zCurrent,zBr)==0;
      fossil_print("%s%s\n", (isCur ? "* " : "  "), zBr);
    }
    db_finalize(&q);
  }else{
    fossil_fatal("branch subcommand should be one of: "
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
  int showAll = P("all")!=0;
  int colorTest = P("colortest")!=0;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }
  if( colorTest ){
    showClosed = 0;
    showAll = 1;
  }

  style_header(showClosed ? "Closed Branches" :
                  showAll ? "All Branches" : "Open Branches");
  style_submenu_element("Timeline", "Timeline", "brtimeline");
  if( showClosed ){
    style_submenu_element("All", "All", "brlist?all");
    style_submenu_element("Open","Open","brlist");
  }else if( showAll ){
    style_submenu_element("Closed", "Closed", "brlist?closed");
    style_submenu_element("Open","Open","brlist");
  }else{
    style_submenu_element("All", "All", "brlist?all");
    style_submenu_element("Closed","Closed","brlist?closed");
  }
  if( !colorTest ){
    style_submenu_element("Color-Test", "Color-Test", "brlist?colortest");
  }else{
    style_submenu_element("All", "All", "brlist?all");
  }
  login_anonymous_available();
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
  @ reopened)</li>
  @ </ol>
  style_sidebox_end();

  branch_prepare_list_query(&q, showAll?1:(showClosed?-1:0));
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zBr = db_column_text(&q, 0);
    if( cnt==0 ){
      if( colorTest ){
        @ <h2>Default background colors for all branches:</h2>
      }else if( showAll ){
        @ <h2>All Branches:</h2>
      }else if( showClosed ){
        @ <h2>Closed Branches:</h2>
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
    @ %z(href("%R/timeline?r=%T",zTagName))[timeline]</a>
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
  if( !g.perm.Read ){ login_needed(); return; }

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
