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
  noSign = db_get_boolean("omitsign", 0)|noSign;
  if( db_get_boolean("clearsign", 0)==0 ){ noSign = 1; }

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
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", brid);
  if( manifest_crosslink(brid, &branch, MC_PERMIT_HOOKS)==0 ){
    fossil_fatal("%s", g.zErrMsg);
  }
  assert( blob_is_reset(&branch) );
  content_deltify(rootid, &brid, 1, 0);
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
  if( !isPrivate ) autosync_loop(SYNC_PUSH, db_get_int("autosync-tries",1),0);
}

#if INTERFACE
/*
** Allows bits in the mBplqFlags parameter to branch_prepare_list_query().
*/
#define BRL_CLOSED_ONLY      0x001 /* Show only closed branches */
#define BRL_OPEN_ONLY        0x002 /* Show only open branches */
#define BRL_BOTH             0x003 /* Show both open and closed branches */
#define BRL_OPEN_CLOSED_MASK 0x003
#define BRL_MTIME            0x004 /* Include lastest check-in time */
#define BRL_ORDERBY_MTIME    0x008 /* Sort by MTIME. (otherwise sort by name)*/

#endif /* INTERFACE */

/*
** Prepare a query that will list branches.
**
** If (which<0) then the query pulls only closed branches. If
** (which>0) then the query pulls all (closed and opened)
** branches. Else the query pulls currently-opened branches.
*/
void branch_prepare_list_query(Stmt *pQuery, int brFlags){
  switch( brFlags & BRL_OPEN_CLOSED_MASK ){
    case BRL_CLOSED_ONLY: {
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
      break;
    }
    case BRL_BOTH: {
      db_prepare(pQuery,
        "SELECT DISTINCT value FROM tagxref"
        " WHERE tagid=%d AND value NOT NULL"
        "   AND rid IN leaf"
        " ORDER BY value COLLATE nocase /*sort*/",
        TAG_BRANCH
      );
      break;
    }
    case BRL_OPEN_ONLY: {
      db_prepare(pQuery,
        "SELECT DISTINCT value FROM tagxref"
        " WHERE tagid=%d AND value NOT NULL"
        "   AND rid IN leaf"
        "   AND NOT %z"
        " ORDER BY value COLLATE nocase /*sort*/",
        TAG_BRANCH, leaf_is_closed_sql("tagxref.rid")
      );
      break;
    }
  }
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
** COMMAND: branch
**
** Usage: %fossil branch SUBCOMMAND ... ?OPTIONS?
**
** Run various subcommands to manage branches of the open repository or
** of the repository identified by the -R or --repository option.
**
**    fossil branch new BRANCH-NAME BASIS ?OPTIONS?
**
**        Create a new branch BRANCH-NAME off of check-in BASIS.
**        Supported options for this subcommand include:
**        --private             branch is private (i.e., remains local)
**        --bgcolor COLOR       use COLOR instead of automatic background
**        --nosign              do not sign contents on this branch
**        --date-override DATE  DATE to use instead of 'now'
**        --user-override USER  USER to use instead of the current default
**
**        DATE may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
**        year-month-day form, it may be truncated, the "T" may be
**        replaced by a space, and it may also name a timezone offset
**        from UTC as "-HH:MM" (westward) or "+HH:MM" (eastward).
**        Either no timezone suffix or "Z" means UTC.
**
**    fossil branch list|ls ?-a|--all|-c|--closed?
**
**        List all branches.  Use -a or --all to list all branches and
**        -c or --closed to list all closed branches.  The default is to
**        show only open branches.
**
**    fossil branch info BRANCH-NAME
**
**        Print information about a branch
**
** Options:
**    -R|--repository FILE       Run commands on repository FILE
*/
void branch_cmd(void){
  int n;
  const char *zCmd = "list";
  db_find_and_open_repository(0, 0);
  if( g.argc>=3 ) zCmd = g.argv[2];
  n = strlen(zCmd);
  if( strncmp(zCmd,"new",n)==0 ){
    branch_new();
  }else if( (strncmp(zCmd,"list",n)==0)||(strncmp(zCmd, "ls", n)==0) ){
    Stmt q;
    int vid;
    char *zCurrent = 0;
    int brFlags = BRL_OPEN_ONLY;
    if( find_option("all","a",0)!=0 ) brFlags = BRL_BOTH;
    if( find_option("closed","c",0)!=0 ) brFlags = BRL_CLOSED_ONLY;

    if( g.localOpen ){
      vid = db_lget_int("checkout", 0);
      zCurrent = db_text(0, "SELECT value FROM tagxref"
                            " WHERE rid=%d AND tagid=%d", vid, TAG_BRANCH);
    }
    branch_prepare_list_query(&q, brFlags);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zBr = db_column_text(&q, 0);
      int isCur = zCurrent!=0 && fossil_strcmp(zCurrent,zBr)==0;
      fossil_print("%s%s\n", (isCur ? "* " : "  "), zBr);
    }
    db_finalize(&q);
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
  }else{
    fossil_fatal("branch subcommand should be one of: "
                 "info list ls new");
  }
}

static const char brlistQuery[] =
@ SELECT
@   tagxref.value,
@   max(event.mtime),
@   EXISTS(SELECT 1 FROM tagxref AS tx
@           WHERE tx.rid=tagxref.rid
@             AND tx.tagid=(SELECT tagid FROM tag WHERE tagname='closed')
@             AND tx.tagtype>0),
@   (SELECT tagxref.value
@      FROM plink CROSS JOIN tagxref
@    WHERE plink.pid=event.objid
@       AND tagxref.rid=plink.cid
@      AND tagxref.tagid=(SELECT tagid FROM tag WHERE tagname='branch')
@      AND tagtype>0),
@   count(*),
@   (SELECT uuid FROM blob WHERE rid=tagxref.rid),
@   event.bgcolor
@  FROM tagxref, tag, event
@ WHERE tagxref.tagid=tag.tagid
@   AND tagxref.tagtype>0
@   AND tag.tagname='branch'
@   AND event.objid=tagxref.rid
@ GROUP BY 1
@ ORDER BY 2 DESC;
;

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
  style_header("Branches");
  style_adunit_config(ADUNIT_RIGHT_OK);
  style_submenu_checkbox("colors", "Use Branch Colors", 0, 0);
  login_anonymous_available();

  db_prepare(&q, brlistQuery/*works-like:""*/);
  rNow = db_double(0.0, "SELECT julianday('now')");
  @ <div class="brlist"><table id="branchlisttable">
  @ <thead><tr>
  @ <th>Branch Name</th>
  @ <th>Age</th>
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
    if( zBgClr == 0 ){
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
    @ <td>%z(href("%R/timeline?n=100&r=%T",zBranch))%h(zBranch)</a></td>
    @ <td data-sortkey="%016llx(-iMtime)">%s(zAge)</td>
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
  output_table_sorting_javascript("branchlisttable","tkNtt",2);
  style_footer();
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
**     open        Show only open branches (default behavior)
**     colortest   Show all branches with automatic color
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
  if( colorTest ){
    showClosed = 0;
    showAll = 1;
  }
  if( showAll ) brFlags = BRL_BOTH;
  if( showClosed ) brFlags = BRL_CLOSED_ONLY;

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

  branch_prepare_list_query(&q, brFlags);
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
      @ <li>%z(href("%R/timeline?r=%T&n=200",zBr))%h(zBr)</a></li>
    }
  }
  if( cnt ){
    @ </ul>
  }
  db_finalize(&q);
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
    @ %z(href("%R/timeline?r=%T&n=200",zTagName))[timeline]</a>
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
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }

  style_header("Branches");
  style_submenu_element("List", "brlist");
  login_anonymous_available();
  @ <h2>The initial check-in for each branch:</h2>
  db_prepare(&q,
    "%s AND blob.rid IN (SELECT rid FROM tagxref"
    "                     WHERE tagtype>0 AND tagid=%d AND srcid!=0)"
    " ORDER BY event.mtime DESC",
    timeline_query_for_www(), TAG_BRANCH
  );
  www_print_timeline(&q, 0, 0, 0, 0, brtimeline_extra);
  db_finalize(&q);
  style_footer();
}
