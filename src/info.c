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
** This file contains code to implement the "info" command.  The
** "info" command gives command-line access to information about
** the current tree, or a particular artifact or check-in.
*/
#include "config.h"
#include "info.h"
#include <assert.h>

/*
** Return a string (in memory obtained from malloc) holding a
** comma-separated list of tags that apply to check-in with
** record-id rid.  If the "propagatingOnly" flag is true, then only
** show branch tags (tags that propagate to children).
**
** Return NULL if there are no such tags.
*/
char *info_tags_of_checkin(int rid, int propagatingOnly){
  char *zTags;
  zTags = db_text(0, "SELECT group_concat(substr(tagname, 5), ', ')"
                     "  FROM tagxref, tag"
                     " WHERE tagxref.rid=%d AND tagxref.tagtype>%d"
                     "   AND tag.tagid=tagxref.tagid"
                     "   AND tag.tagname GLOB 'sym-*'",
                     rid, propagatingOnly!=0);
  return zTags;
}


/*
** Print common information about a particular record.
**
**     *  The UUID
**     *  The record ID
**     *  mtime and ctime
**     *  who signed it
**
*/
void show_common_info(
  int rid,                   /* The rid for the check-in to display info for */
  const char *zUuidName,     /* Name of the UUID */
  int showComment,           /* True to show the check-in comment */
  int showFamily             /* True to show parents and children */
){
  Stmt q;
  char *zComment = 0;
  char *zTags;
  char *zDate;
  char *zUuid;
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( zUuid ){
    zDate = db_text(0,
      "SELECT datetime(mtime) || ' UTC' FROM event WHERE objid=%d",
      rid
    );
         /* 01234567890123 */
    fossil_print("%-13s %.40s %s\n", zUuidName, zUuid, zDate ? zDate : "");
    free(zDate);
    if( showComment ){
      zComment = db_text(0,
        "SELECT coalesce(ecomment,comment) || "
        "       ' (user: ' || coalesce(euser,user,'?') || ')' "
        "  FROM event WHERE objid=%d",
        rid
      );
    }
    free(zUuid);
  }
  if( showFamily ){
    db_prepare(&q, "SELECT uuid, pid, isprim FROM plink JOIN blob ON pid=rid "
                   " WHERE cid=%d"
                   " ORDER BY isprim DESC, mtime DESC /*sort*/", rid);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zUuid = db_column_text(&q, 0);
      const char *zType = db_column_int(&q, 2) ? "parent:" : "merged-from:";
      zDate = db_text("",
        "SELECT datetime(mtime) || ' UTC' FROM event WHERE objid=%d",
        db_column_int(&q, 1)
      );
      fossil_print("%-13s %.40s %s\n", zType, zUuid, zDate);
      free(zDate);
    }
    db_finalize(&q);
    db_prepare(&q, "SELECT uuid, cid, isprim FROM plink JOIN blob ON cid=rid "
                   " WHERE pid=%d"
                   " ORDER BY isprim DESC, mtime DESC /*sort*/", rid);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zUuid = db_column_text(&q, 0);
      const char *zType = db_column_int(&q, 2) ? "child:" : "merged-into:";
      zDate = db_text("",
        "SELECT datetime(mtime) || ' UTC' FROM event WHERE objid=%d",
        db_column_int(&q, 1)
      );
      fossil_print("%-13s %.40s %s\n", zType, zUuid, zDate);
      free(zDate);
    }
    db_finalize(&q);
  }
  zTags = info_tags_of_checkin(rid, 0);
  if( zTags && zTags[0] ){
    fossil_print("tags:         %s\n", zTags);
  }
  free(zTags);
  if( zComment ){
    fossil_print("comment:      ");
    comment_print(zComment, 0, 14, -1, g.comFmtFlags);
    free(zComment);
  }
}

/*
** Print information about the URLs used to access a repository and
** checkouts in a repository.
*/
static void extraRepoInfo(void){
  Stmt s;
  db_prepare(&s, "SELECT substr(name,7), date(mtime,'unixepoch')"
                 "  FROM config"
                 " WHERE name GLOB 'ckout:*' ORDER BY mtime DESC");
  while( db_step(&s)==SQLITE_ROW ){
    const char *zName;
    const char *zCkout = db_column_text(&s, 0);
    if( !vfile_top_of_checkout(zCkout) ) continue;
    if( g.localOpen ){
      if( fossil_strcmp(zCkout, g.zLocalRoot)==0 ) continue;
      zName = "alt-root:";
    }else{
      zName = "check-out:";
    }
    fossil_print("%-11s   %-54s %s\n", zName, zCkout,
                 db_column_text(&s, 1));
  }
  db_finalize(&s);
  db_prepare(&s, "SELECT substr(name,9), date(mtime,'unixepoch')"
                 "  FROM config"
                 " WHERE name GLOB 'baseurl:*' ORDER BY mtime DESC");
  while( db_step(&s)==SQLITE_ROW ){
    fossil_print("access-url:   %-54s %s\n", db_column_text(&s, 0),
                 db_column_text(&s, 1));
  }
  db_finalize(&s);
}

/*
** Show the parent project, if any
*/
static void showParentProject(void){
  const char *zParentCode;
  zParentCode = db_get("parent-project-code",0);
  if( zParentCode ){
    fossil_print("derived-from: %s %s\n", zParentCode,
                 db_get("parent-project-name",""));
  }
}


/*
** COMMAND: info
**
** Usage: %fossil info ?VERSION | REPOSITORY_FILENAME? ?OPTIONS?
**
** With no arguments, provide information about the current tree.
** If an argument is specified, provide information about the object
** in the repository of the current tree that the argument refers
** to.  Or if the argument is the name of a repository, show
** information about that repository.
**
** If the argument is a repository name, then the --verbose option shows
** known the check-out locations for that repository and all URLs used
** to access the repository.  The --verbose is (currently) a no-op if
** the argument is the name of a object within the repository.
**
** Use the "finfo" command to get information about a specific
** file in a checkout.
**
** Options:
**
**    -R|--repository FILE       Extract info from repository FILE
**    -v|--verbose               Show extra information about repositories
**
** See also: annotate, artifact, finfo, timeline
*/
void info_cmd(void){
  i64 fsize;
  int verboseFlag = find_option("verbose","v",0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("detail","l",0)!=0; /* deprecated */
  }

  if( g.argc==3 && (fsize = file_size(g.argv[2]))>0 && (fsize&0x1ff)==0 ){
    db_open_config(0, 0);
    db_open_repository(g.argv[2]);
    db_record_repository_filename(g.argv[2]);
    fossil_print("project-name: %s\n", db_get("project-name", "<unnamed>"));
    fossil_print("project-code: %s\n", db_get("project-code", "<none>"));
    showParentProject();
    extraRepoInfo();
    return;
  }
  db_find_and_open_repository(0,0);
  verify_all_options();
  if( g.argc==2 ){
    int vid;
         /* 012345678901234 */
    db_record_repository_filename(0);
    fossil_print("project-name: %s\n", db_get("project-name", "<unnamed>"));
    if( g.localOpen ){
      fossil_print("repository:   %s\n", db_repository_filename());
      fossil_print("local-root:   %s\n", g.zLocalRoot);
    }
    if( verboseFlag ) extraRepoInfo();
    if( g.zConfigDbName ){
      fossil_print("config-db:    %s\n", g.zConfigDbName);
    }
    fossil_print("project-code: %s\n", db_get("project-code", ""));
    showParentProject();
    vid = g.localOpen ? db_lget_int("checkout", 0) : 0;
    if( vid ){
      show_common_info(vid, "checkout:", 1, 1);
    }
    fossil_print("check-ins:    %d\n",
             db_int(-1, "SELECT count(*) FROM event WHERE type='ci' /*scan*/"));
  }else{
    int rid;
    rid = name_to_rid(g.argv[2]);
    if( rid==0 ){
      fossil_fatal("no such object: %s", g.argv[2]);
    }
    show_common_info(rid, "uuid:", 1, 1);
  }
}

/*
** Show information about all tags on a given check-in.
*/
static void showTags(int rid){
  Stmt q;
  int cnt = 0;
  db_prepare(&q,
    "SELECT tag.tagid, tagname, "
    "       (SELECT uuid FROM blob WHERE rid=tagxref.srcid AND rid!=%d),"
    "       value, datetime(tagxref.mtime,toLocal()), tagtype,"
    "       (SELECT uuid FROM blob WHERE rid=tagxref.origid AND rid!=%d)"
    "  FROM tagxref JOIN tag ON tagxref.tagid=tag.tagid"
    " WHERE tagxref.rid=%d"
    " ORDER BY tagname /*sort*/", rid, rid, rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTagname = db_column_text(&q, 1);
    const char *zSrcUuid = db_column_text(&q, 2);
    const char *zValue = db_column_text(&q, 3);
    const char *zDate = db_column_text(&q, 4);
    int tagtype = db_column_int(&q, 5);
    const char *zOrigUuid = db_column_text(&q, 6);
    cnt++;
    if( cnt==1 ){
      @ <div class="section">Tags And Properties</div>
      @ <ul>
    }
    @ <li>
    if( tagtype==0 ){
      @ <span class="infoTagCancelled">%h(zTagname)</span> cancelled
    }else if( zValue ){
      @ <span class="infoTag">%h(zTagname)=%h(zValue)</span>
    }else {
      @ <span class="infoTag">%h(zTagname)</span>
    }
    if( tagtype==2 ){
      if( zOrigUuid && zOrigUuid[0] ){
        @ inherited from
        hyperlink_to_uuid(zOrigUuid);
      }else{
        @ propagates to descendants
      }
#if 0
      if( zValue && fossil_strcmp(zTagname,"branch")==0 ){
        @ &nbsp;&nbsp;
        @ %z(href("%R/timeline?r=%T",zValue))branch timeline</a>
      }
#endif
    }
    if( zSrcUuid && zSrcUuid[0] ){
      if( tagtype==0 ){
        @ by
      }else{
        @ added by
      }
      hyperlink_to_uuid(zSrcUuid);
      @ on
      hyperlink_to_date(zDate,0);
    }
    @ </li>
  }
  db_finalize(&q);
  if( cnt ){
    @ </ul>
  }
}

/*
** Show the context graph (immediate parents and children) for
** check-in rid.
*/
void render_checkin_context(int rid, int parentsOnly){
  Blob sql;
  Stmt q;
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_www(), -1);
  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
     "DELETE FROM ok;"
     "INSERT INTO ok VALUES(%d);"
     "INSERT OR IGNORE INTO ok SELECT pid FROM plink WHERE cid=%d;",
     rid, rid
  );
  if( !parentsOnly ){
    db_multi_exec(
      "INSERT OR IGNORE INTO ok SELECT cid FROM plink WHERE pid=%d;", rid
    );
  }
  blob_append_sql(&sql, " AND event.objid IN ok ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_sql_text(&sql));
  www_print_timeline(&q, TIMELINE_DISJOINT|TIMELINE_GRAPH, 0, 0, rid, 0);
  db_finalize(&q);
}

/*
** Show a graph all wiki, tickets, and check-ins that refer to object zUuid.
**
** If zLabel is not NULL and the graph is not empty, then output zLabel as
** a prefix to the graph.
*/
void render_backlink_graph(const char *zUuid, const char *zLabel){
  Blob sql;
  Stmt q;
  char *zGlob;
  zGlob = mprintf("%.5s*", zUuid);
  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
     "DELETE FROM ok;"
     "INSERT OR IGNORE INTO ok"
     " SELECT srcid FROM backlink"
     "  WHERE target GLOB %Q"
     "    AND %Q GLOB (target || '*');",
     zGlob, zUuid
  );
  if( !db_exists("SELECT 1 FROM ok") ) return;
  if( zLabel ) cgi_printf("%s", zLabel);
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_www(), -1);
  blob_append_sql(&sql, " AND event.objid IN ok ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_sql_text(&sql));
  www_print_timeline(&q, TIMELINE_DISJOINT|TIMELINE_GRAPH, 0, 0, 0, 0);
  db_finalize(&q);
}

/*
** WEBPAGE: test-backlinks
**
** Show a timeline of all check-ins and other events that have entries
** in the backlink table.  This is used for testing the rendering
** of the "References" section of the /info page.
*/
void backlink_timeline_page(void){
  Blob sql;
  Stmt q;

  login_check_credentials();
  if( !g.perm.Read || !g.perm.RdTkt || !g.perm.RdWiki ){
    login_needed(g.anon.Read && g.anon.RdTkt && g.anon.RdWiki);
    return;
  }
  style_header("Backlink Timeline (Internal Testing Use)");
  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
     "DELETE FROM ok;"
     "INSERT OR IGNORE INTO ok"
     " SELECT blob.rid FROM backlink, blob"
     "  WHERE blob.uuid BETWEEN backlink.target AND (backlink.target||'x')"
  );
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_www(), -1);
  blob_append_sql(&sql, " AND event.objid IN ok ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_sql_text(&sql));
  www_print_timeline(&q, TIMELINE_DISJOINT|TIMELINE_GRAPH, 0, 0, 0, 0);
  db_finalize(&q);
  style_footer();
}


/*
** Append the difference between artifacts to the output
*/
static void append_diff(
  const char *zFrom,    /* Diff from this artifact */
  const char *zTo,      /*  ... to this artifact */
  u64 diffFlags,        /* Diff formatting flags */
  ReCompiled *pRe       /* Only show change matching this regex */
){
  int fromid;
  int toid;
  Blob from, to, out;
  if( zFrom ){
    fromid = uuid_to_rid(zFrom, 0);
    content_get(fromid, &from);
  }else{
    blob_zero(&from);
  }
  if( zTo ){
    toid = uuid_to_rid(zTo, 0);
    content_get(toid, &to);
  }else{
    blob_zero(&to);
  }
  blob_zero(&out);
  if( diffFlags & DIFF_SIDEBYSIDE ){
    text_diff(&from, &to, &out, pRe, diffFlags | DIFF_HTML | DIFF_NOTTOOBIG);
    @ %s(blob_str(&out))
  }else{
    text_diff(&from, &to, &out, pRe,
           diffFlags | DIFF_LINENO | DIFF_HTML | DIFF_NOTTOOBIG);
    @ <pre class="udiff">
    @ %s(blob_str(&out))
    @ </pre>
  }
  blob_reset(&from);
  blob_reset(&to);
  blob_reset(&out);
}

/*
** Write a line of web-page output that shows changes that have occurred
** to a file between two check-ins.
*/
static void append_file_change_line(
  const char *zName,    /* Name of the file that has changed */
  const char *zOld,     /* blob.uuid before change.  NULL for added files */
  const char *zNew,     /* blob.uuid after change.  NULL for deletes */
  const char *zOldName, /* Prior name.  NULL if no name change. */
  u64 diffFlags,        /* Flags for text_diff().  Zero to omit diffs */
  ReCompiled *pRe,      /* Only show diffs that match this regex, if not NULL */
  int mperm             /* executable or symlink permission for zNew */
){
  @ <p>
  if( !g.perm.Hyperlink ){
    if( zNew==0 ){
      @ Deleted %h(zName).
    }else if( zOld==0 ){
      @ Added %h(zName).
    }else if( zOldName!=0 && fossil_strcmp(zName,zOldName)!=0 ){
      @ Name change from %h(zOldName) to %h(zName).
    }else if( fossil_strcmp(zNew, zOld)==0 ){
      if( mperm==PERM_EXE ){
        @ %h(zName) became executable.
      }else if( mperm==PERM_LNK ){
        @ %h(zName) became a symlink.
      }else{
        @ %h(zName) became a regular file.
      }
    }else{
      @ Changes to %h(zName).
    }
    if( diffFlags ){
      append_diff(zOld, zNew, diffFlags, pRe);
    }
  }else{
    if( zOld && zNew ){
      if( fossil_strcmp(zOld, zNew)!=0 ){
        @ Modified %z(href("%R/finfo?name=%T",zName))%h(zName)</a>
        @ from %z(href("%R/artifact/%!S",zOld))[%S(zOld)]</a>
        @ to %z(href("%R/artifact/%!S",zNew))[%S(zNew)]</a>.
      }else if( zOldName!=0 && fossil_strcmp(zName,zOldName)!=0 ){
        @ Name change
        @ from %z(href("%R/finfo?name=%T",zOldName))%h(zOldName)</a>
        @ to %z(href("%R/finfo?name=%T",zName))%h(zName)</a>.
      }else{
        @ %z(href("%R/finfo?name=%T",zName))%h(zName)</a> became
        if( mperm==PERM_EXE ){
          @ executable with contents
        }else if( mperm==PERM_LNK ){
          @ a symlink with target
        }else{
          @ a regular file with contents
        }
        @ %z(href("%R/artifact/%!S",zNew))[%S(zNew)]</a>.
      }
    }else if( zOld ){
      @ Deleted %z(href("%R/finfo?name=%T",zName))%h(zName)</a>
      @ version %z(href("%R/artifact/%!S",zOld))[%S(zOld)]</a>.
    }else{
      @ Added %z(href("%R/finfo?name=%T",zName))%h(zName)</a>
      @ version %z(href("%R/artifact/%!S",zNew))[%S(zNew)]</a>.
    }
    if( diffFlags ){
      append_diff(zOld, zNew, diffFlags, pRe);
    }else if( zOld && zNew && fossil_strcmp(zOld,zNew)!=0 ){
      @ &nbsp;&nbsp;
      @ %z(href("%R/fdiff?v1=%!S&v2=%!S&sbs=1",zOld,zNew))[diff]</a>
    }
  }
  @ </p>
}

/*
** Generate javascript to enhance HTML diffs.
*/
void append_diff_javascript(int sideBySide){
  if( !sideBySide ) return;
  @ <script>(function(){
  @ var SCROLL_LEN = 25;
  @ function initSbsDiff(diff){
  @   var txtCols = diff.querySelectorAll('.difftxtcol');
  @   var txtPres = diff.querySelectorAll('.difftxtcol pre');
  @   var width = Math.max(txtPres[0].scrollWidth, txtPres[1].scrollWidth);
  @   for(var i=0; i<2; i++){
  @     txtPres[i].style.width = width + 'px';
  @     txtCols[i].onscroll = function(e){
  @       txtCols[0].scrollLeft = txtCols[1].scrollLeft = this.scrollLeft;
  @     };
  @   }
  @   diff.tabIndex = 0;
  @   diff.onkeydown = function(e){
  @     e = e || event;
  @     var len = {37: -SCROLL_LEN, 39: SCROLL_LEN}[e.keyCode];
  @     if( !len ) return;
  @     txtCols[0].scrollLeft += len;
  @     return false;
  @   };
  @ }
  @
  @ var diffs = document.querySelectorAll('.sbsdiffcols');
  @ for(var i=0; i<diffs.length; i++){
  @   initSbsDiff(diffs[i]);
  @ }
  @ }())</script>
}

/*
** Construct an appropriate diffFlag for text_diff() based on query
** parameters and the to boolean arguments.
*/
u64 construct_diff_flags(int verboseFlag, int sideBySide){
  u64 diffFlags = 0;  /* Zero means do not show any diff */
  if( verboseFlag!=0 ){
    int x;
    if( sideBySide ){
      diffFlags = DIFF_SIDEBYSIDE;

      /* "dw" query parameter determines width of each column */
      x = atoi(PD("dw","80"))*(DIFF_CONTEXT_MASK+1);
      if( x<0 || x>DIFF_WIDTH_MASK ) x = DIFF_WIDTH_MASK;
      diffFlags += x;
    }

    if( P("w") ){
      diffFlags |= DIFF_IGNORE_ALLWS;
    }
    /* "dc" query parameter determines lines of context */
    x = atoi(PD("dc","7"));
    if( x<0 || x>DIFF_CONTEXT_MASK ) x = DIFF_CONTEXT_MASK;
    diffFlags += x;

    /* The "noopt" parameter disables diff optimization */
    if( PD("noopt",0)!=0 ) diffFlags |= DIFF_NOOPT;
    diffFlags |= DIFF_STRIP_EOLCR;
  }
  return diffFlags;
}

/*
** WEBPAGE: vinfo
** WEBPAGE: ci
** URL:  /ci?name=ARTIFACTID
** URL:  /vinfo?name=ARTIFACTID
**
** Display information about a particular check-in.
**
** We also jump here from /info if the name is a check-in
**
** If the /ci page is used (instead of /vinfo or /info) then the
** default behavior is to show unified diffs of all file changes.
** With /vinfo and /info, only a list of the changed files are
** shown, without diffs.  This behavior is inverted if the
** "show-version-diffs" setting is turned on.
*/
void ci_page(void){
  Stmt q1, q2, q3;
  int rid;
  int isLeaf;
  int verboseFlag;     /* True to show diffs */
  int sideBySide;      /* True for side-by-side diffs */
  u64 diffFlags;       /* Flag parameter for text_diff() */
  const char *zName;   /* Name of the check-in to be displayed */
  const char *zUuid;   /* UUID of zName */
  const char *zParent; /* UUID of the parent check-in (if any) */
  const char *zRe;     /* regex parameter */
  ReCompiled *pRe = 0; /* regex */
  const char *zW;      /* URL param for ignoring whitespace */
  const char *zPage = "vinfo";  /* Page that shows diffs */
  const char *zPageHide = "ci"; /* Page that hides diffs */

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  zName = P("name");
  rid = name_to_rid_www("name");
  if( rid==0 ){
    style_header("Check-in Information Error");
    @ No such object: %h(g.argv[2])
    style_footer();
    return;
  }
  zRe = P("regex");
  if( zRe ) re_compile(&pRe, zRe, 0);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  zParent = db_text(0,
    "SELECT uuid FROM plink, blob"
    " WHERE plink.cid=%d AND blob.rid=plink.pid AND plink.isprim",
    rid
  );
  isLeaf = is_a_leaf(rid);
  db_prepare(&q1,
     "SELECT uuid, datetime(mtime,toLocal()), user, comment,"
     "       datetime(omtime,toLocal()), mtime"
     "  FROM blob, event"
     " WHERE blob.rid=%d"
     "   AND event.objid=%d",
     rid, rid
  );
  sideBySide = !is_false(PD("sbs","1"));
  if( db_step(&q1)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q1, 0);
    int nUuid = db_column_bytes(&q1, 0);
    char *zEUser, *zEComment;
    const char *zUser;
    const char *zComment;
    const char *zDate;
    const char *zOrigDate;

    style_header("Check-in [%S]", zUuid);
    login_anonymous_available();
    zEUser = db_text(0,
                   "SELECT value FROM tagxref"
                   " WHERE tagid=%d AND rid=%d AND tagtype>0",
                    TAG_USER, rid);
    zEComment = db_text(0,
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_COMMENT, rid);
    zUser = db_column_text(&q1, 2);
    zComment = db_column_text(&q1, 3);
    zDate = db_column_text(&q1,1);
    zOrigDate = db_column_text(&q1, 4);
    @ <div class="section">Overview</div>
    @ <table class="label-value">
    @ <tr><th>%s(hname_alg(nUuid)):</th><td>%s(zUuid)
    if( g.perm.Setup ){
      @ (Record ID: %d(rid))
    }
    @ </td></tr>
    @ <tr><th>Date:</th><td>
    hyperlink_to_date(zDate, "</td></tr>");
    if( zOrigDate && fossil_strcmp(zDate, zOrigDate)!=0 ){
      @ <tr><th>Original&nbsp;Date:</th><td>
      hyperlink_to_date(zOrigDate, "</td></tr>");
    }
    if( zEUser ){
      @ <tr><th>Edited&nbsp;User:</th><td>
      hyperlink_to_user(zEUser,zDate,"</td></tr>");
      @ <tr><th>Original&nbsp;User:</th><td>
      hyperlink_to_user(zUser,zDate,"</td></tr>");
    }else{
      @ <tr><th>User:</th><td>
      hyperlink_to_user(zUser,zDate,"</td></tr>");
    }
    if( zEComment ){
      @ <tr><th>Edited&nbsp;Comment:</th>
      @     <td class="infoComment">%!W(zEComment)</td></tr>
      @ <tr><th>Original&nbsp;Comment:</th>
      @     <td class="infoComment">%!W(zComment)</td></tr>
    }else{
      @ <tr><th>Comment:</th><td class="infoComment">%!W(zComment)</td></tr>
    }
    if( g.perm.Admin ){
      db_prepare(&q2,
         "SELECT rcvfrom.ipaddr, user.login, datetime(rcvfrom.mtime)"
         "  FROM blob JOIN rcvfrom USING(rcvid) LEFT JOIN user USING(uid)"
         " WHERE blob.rid=%d",
         rid
      );
      if( db_step(&q2)==SQLITE_ROW ){
        const char *zIpAddr = db_column_text(&q2, 0);
        const char *zUser = db_column_text(&q2, 1);
        const char *zDate = db_column_text(&q2, 2);
        if( zUser==0 || zUser[0]==0 ) zUser = "unknown";
        @ <tr><th>Received&nbsp;From:</th>
        @ <td>%h(zUser) @ %h(zIpAddr) on %s(zDate)</td></tr>
      }
      db_finalize(&q2);
    }
    if( g.perm.Hyperlink ){
      char *zPJ = db_get("short-project-name", 0);
      Blob projName;
      int jj;
      if( zPJ==0 ) zPJ = db_get("project-name", "unnamed");
      blob_zero(&projName);
      blob_append(&projName, zPJ, -1);
      blob_trim(&projName);
      zPJ = blob_str(&projName);
      for(jj=0; zPJ[jj]; jj++){
        if( (zPJ[jj]>0 && zPJ[jj]<' ') || strchr("\"*/:<>?\\|", zPJ[jj]) ){
          zPJ[jj] = '_';
        }
      }
      @ <tr><th>Timelines:</th><td>
      @   %z(href("%R/timeline?f=%!S&unhide",zUuid))family</a>
      if( zParent ){
        @ | %z(href("%R/timeline?p=%!S&unhide",zUuid))ancestors</a>
      }
      if( !isLeaf ){
        @ | %z(href("%R/timeline?d=%!S&unhide",zUuid))descendants</a>
      }
      if( zParent && !isLeaf ){
        @ | %z(href("%R/timeline?dp=%!S&unhide",zUuid))both</a>
      }
      db_prepare(&q2,"SELECT substr(tag.tagname,5) FROM tagxref, tag "
                     " WHERE rid=%d AND tagtype>0 "
                     "   AND tag.tagid=tagxref.tagid "
                     "   AND +tag.tagname GLOB 'sym-*'", rid);
      while( db_step(&q2)==SQLITE_ROW ){
        const char *zTagName = db_column_text(&q2, 0);
        @  | %z(href("%R/timeline?r=%T&unhide",zTagName))%h(zTagName)</a>
      }
      db_finalize(&q2);


      /* The Download: line */
      if( g.anon.Zip ){
        char *zUrl = mprintf("%R/tarball/%t-%S.tar.gz?uuid=%s",
                             zPJ, zUuid, zUuid);
        @ </td></tr>
        @ <tr><th>Downloads:</th><td>
        @ %z(href("%s",zUrl))Tarball</a>
        @ | %z(href("%R/zip/%t-%S.zip?uuid=%!S",zPJ,zUuid,zUuid))
        @         ZIP archive</a>
        fossil_free(zUrl);
      }
      @ </td></tr>
      @ <tr><th>Other&nbsp;Links:</th>
      @   <td>
      @     %z(href("%R/tree?ci=%!S",zUuid))files</a>
      @   | %z(href("%R/fileage?name=%!S",zUuid))file ages</a>
      @   | %z(href("%R/tree?nofiles&type=tree&ci=%!S",zUuid))folders</a>
      @   | %z(href("%R/artifact/%!S",zUuid))manifest</a>
      if( g.perm.Admin ){
        @   | %z(href("%R/mlink?ci=%!S",zUuid))mlink table</a>
      }
      if( g.anon.Write ){
        @   | %z(href("%R/ci_edit?r=%!S",zUuid))edit</a>
      }
      @   </td>
      @ </tr>
      blob_reset(&projName);
    }
    @ </table>
  }else{
    style_header("Check-in Information");
    login_anonymous_available();
  }
  db_finalize(&q1);
  render_backlink_graph(zUuid, "<div class=\"section\">References</div>\n");
  showTags(rid);
  @ <div class="section">Context</div>
  render_checkin_context(rid, 0);
  @ <div class="section">Changes</div>
  @ <div class="sectionmenu">
  verboseFlag = g.zPath[0]!='c';
  if( db_get_boolean("show-version-diffs", 0)==0 ){
    verboseFlag = !verboseFlag;
    zPage = "ci";
    zPageHide = "vinfo";
  }
  diffFlags = construct_diff_flags(verboseFlag, sideBySide);
  zW = (diffFlags&DIFF_IGNORE_ALLWS)?"&w":"";
  if( verboseFlag ){
    @ %z(xhref("class='button'","%R/%s/%T",zPageHide,zName))
    @ Hide&nbsp;Diffs</a>
    if( sideBySide ){
      @ %z(xhref("class='button'","%R/%s/%T?sbs=0%s",zPage,zName,zW))
      @ Unified&nbsp;Diffs</a>
    }else{
      @ %z(xhref("class='button'","%R/%s/%T?sbs=1%s",zPage,zName,zW))
      @ Side-by-Side&nbsp;Diffs</a>
    }
    if( *zW ){
      @ %z(xhref("class='button'","%R/%s/%T?sbs=%d",zPage,zName,sideBySide))
      @ Show&nbsp;Whitespace&nbsp;Changes</a>
    }else{
      @ %z(xhref("class='button'","%R/%s/%T?sbs=%d&w",zPage,zName,sideBySide))
      @ Ignore&nbsp;Whitespace</a>
    }
  }else{
    @ %z(xhref("class='button'","%R/%s/%T?sbs=0",zPage,zName))
    @ Show&nbsp;Unified&nbsp;Diffs</a>
    @ %z(xhref("class='button'","%R/%s/%T?sbs=1",zPage,zName))
    @ Show&nbsp;Side-by-Side&nbsp;Diffs</a>
  }
  if( zParent ){
    @ %z(xhref("class='button'","%R/vpatch?from=%!S&to=%!S",zParent,zUuid))
    @ Patch</a>
  }
  if( g.perm.Admin ){
    @ %z(xhref("class='button'","%R/mlink?ci=%!S",zUuid))MLink Table</a>
  }
  @</div>
  if( pRe ){
    @ <p><b>Only differences that match regular expression "%h(zRe)"
    @ are shown.</b></p>
  }
  db_prepare(&q3,
    "SELECT name,"
    "       mperm,"
    "       (SELECT uuid FROM blob WHERE rid=mlink.pid),"
    "       (SELECT uuid FROM blob WHERE rid=mlink.fid),"
    "       (SELECT name FROM filename WHERE filename.fnid=mlink.pfnid)"
    "  FROM mlink JOIN filename ON filename.fnid=mlink.fnid"
    " WHERE mlink.mid=%d AND NOT mlink.isaux"
    "   AND (mlink.fid>0"
           " OR mlink.fnid NOT IN (SELECT pfnid FROM mlink WHERE mid=%d))"
    " ORDER BY name /*sort*/",
    rid, rid
  );
  while( db_step(&q3)==SQLITE_ROW ){
    const char *zName = db_column_text(&q3,0);
    int mperm = db_column_int(&q3, 1);
    const char *zOld = db_column_text(&q3,2);
    const char *zNew = db_column_text(&q3,3);
    const char *zOldName = db_column_text(&q3, 4);
    append_file_change_line(zName, zOld, zNew, zOldName, diffFlags,pRe,mperm);
  }
  db_finalize(&q3);
  append_diff_javascript(sideBySide);
  style_footer();
}

/*
** WEBPAGE: winfo
** URL:  /winfo?name=UUID
**
** Display information about a wiki page.
*/
void winfo_page(void){
  int rid;
  Manifest *pWiki;
  char *zUuid;
  char *zDate;
  Blob wiki;
  int modPending;
  const char *zModAction;

  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  rid = name_to_rid_www("name");
  if( rid==0 || (pWiki = manifest_get(rid, CFTYPE_WIKI, 0))==0 ){
    style_header("Wiki Page Information Error");
    @ No such object: %h(P("name"))
    style_footer();
    return;
  }
  if( g.perm.ModWiki && (zModAction = P("modaction"))!=0 ){
    if( strcmp(zModAction,"delete")==0 ){
      moderation_disapprove(rid);
      /*
      ** Next, check if the wiki page still exists; if not, we cannot
      ** redirect to it.
      */
      if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                    " WHERE rid=%d AND tagname LIKE 'wiki-%%'", rid) ){
        cgi_redirectf("%R/wiki?name=%T", pWiki->zWikiTitle);
        /*NOTREACHED*/
      }else{
        cgi_redirectf("%R/modreq");
        /*NOTREACHED*/
      }
    }
    if( strcmp(zModAction,"approve")==0 ){
      moderation_approve(rid);
    }
  }
  style_header("Update of \"%h\"", pWiki->zWikiTitle);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  zDate = db_text(0, "SELECT datetime(%.17g)", pWiki->rDate);
  style_submenu_element("Raw", "artifact/%s", zUuid);
  style_submenu_element("History", "whistory?name=%t", pWiki->zWikiTitle);
  style_submenu_element("Page", "wiki?name=%t", pWiki->zWikiTitle);
  login_anonymous_available();
  @ <div class="section">Overview</div>
  @ <p><table class="label-value">
  @ <tr><th>Artifact&nbsp;ID:</th>
  @ <td>%z(href("%R/artifact/%!S",zUuid))%s(zUuid)</a>
  if( g.perm.Setup ){
    @ (%d(rid))
  }
  modPending = moderation_pending(rid);
  if( modPending ){
    @ <span class="modpending">*** Awaiting Moderator Approval ***</span>
  }
  @ </td></tr>
  @ <tr><th>Page&nbsp;Name:</th><td>%h(pWiki->zWikiTitle)</td></tr>
  @ <tr><th>Date:</th><td>
  hyperlink_to_date(zDate, "</td></tr>");
  @ <tr><th>Original&nbsp;User:</th><td>
  hyperlink_to_user(pWiki->zUser, zDate, "</td></tr>");
  if( pWiki->zMimetype ){
    @ <tr><th>Mimetype:</th><td>%h(pWiki->zMimetype)</td></tr>
  }
  if( pWiki->nParent>0 ){
    int i;
    @ <tr><th>Parent%s(pWiki->nParent==1?"":"s"):</th><td>
    for(i=0; i<pWiki->nParent; i++){
      char *zParent = pWiki->azParent[i];
      @ %z(href("info/%!S",zParent))%s(zParent)</a>
    }
    @ </td></tr>
  }
  @ </table>

  if( g.perm.ModWiki && modPending ){
    @ <div class="section">Moderation</div>
    @ <blockquote>
    @ <form method="POST" action="%R/winfo/%s(zUuid)">
    @ <label><input type="radio" name="modaction" value="delete">
    @ Delete this change</label><br />
    @ <label><input type="radio" name="modaction" value="approve">
    @ Approve this change</label><br />
    @ <input type="submit" value="Submit">
    @ </form>
    @ </blockquote>
  }


  @ <div class="section">Content</div>
  blob_init(&wiki, pWiki->zWiki, -1);
  wiki_render_by_mimetype(&wiki, pWiki->zMimetype);
  blob_reset(&wiki);
  manifest_destroy(pWiki);
  style_footer();
}

/*
** Show a webpage error message
*/
void webpage_error(const char *zFormat, ...){
  va_list ap;
  const char *z;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  style_header("URL Error");
  @ <h1>Error</h1>
  @ <p>%h(z)</p>
  style_footer();
}

/*
** Find an check-in based on query parameter zParam and parse its
** manifest.  Return the number of errors.
*/
static Manifest *vdiff_parse_manifest(const char *zParam, int *pRid){
  int rid;

  *pRid = rid = name_to_rid_www(zParam);
  if( rid==0 ){
    const char *z = P(zParam);
    if( z==0 || z[0]==0 ){
      webpage_error("Missing \"%s\" query parameter.", zParam);
    }else{
      webpage_error("No such artifact: \"%s\"", z);
    }
    return 0;
  }
  if( !is_a_version(rid) ){
    webpage_error("Artifact %s is not a check-in.", P(zParam));
    return 0;
  }
  return manifest_get(rid, CFTYPE_MANIFEST, 0);
}

/*
** Output a description of a check-in
*/
static void checkin_description(int rid){
  Stmt q;
  db_prepare(&q,
    "SELECT datetime(mtime), coalesce(euser,user),"
    "       coalesce(ecomment,comment), uuid,"
    "      (SELECT group_concat(substr(tagname,5), ', ') FROM tag, tagxref"
    "        WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid"
    "          AND tagxref.rid=blob.rid AND tagxref.tagtype>0)"
    "  FROM event, blob"
    " WHERE event.objid=%d AND type='ci'"
    "   AND blob.rid=%d",
    rid, rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 0);
    const char *zUser = db_column_text(&q, 1);
    const char *zUuid = db_column_text(&q, 3);
    const char *zTagList = db_column_text(&q, 4);
    Blob comment;
    int wikiFlags = WIKI_INLINE|WIKI_NOBADLINKS;
    if( db_get_boolean("timeline-block-markup", 0)==0 ){
      wikiFlags |= WIKI_NOBLOCK;
    }
    hyperlink_to_uuid(zUuid);
    blob_zero(&comment);
    db_column_blob(&q, 2, &comment);
    wiki_convert(&comment, 0, wikiFlags);
    blob_reset(&comment);
    @ (user:
    hyperlink_to_user(zUser,zDate,",");
    if( zTagList && zTagList[0] && g.perm.Hyperlink ){
      int i;
      const char *z = zTagList;
      Blob links;
      blob_zero(&links);
      while( z && z[0] ){
        for(i=0; z[i] && (z[i]!=',' || z[i+1]!=' '); i++){}
        blob_appendf(&links,
              "%z%#h</a>%.2s",
              href("%R/timeline?r=%#t&nd&c=%t",i,z,zDate), i,z, &z[i]
        );
        if( z[i]==0 ) break;
        z += i+2;
      }
      @ tags: %s(blob_str(&links)),
      blob_reset(&links);
    }else{
      @ tags: %h(zTagList),
    }
    @ date:
    hyperlink_to_date(zDate, ")");
    tag_private_status(rid);
  }
  db_finalize(&q);
}


/*
** WEBPAGE: vdiff
** URL: /vdiff?from=TAG&to=TAG
**
** Show the difference between two check-ins identified by the from= and
** to= query parameters.
**
** Query parameters:
**
**   from=TAG        Left side of the comparison
**   to=TAG          Right side of the comparison
**   branch=TAG      Show all changes on a particular branch
**   v=BOOLEAN       Default true.  If false, only list files that have changed
**   sbs=BOOLEAN     Side-by-side diff if true.  Unified diff if false
**   glob=STRING     only diff files matching this glob
**   dc=N            show N lines of context around each diff
**   w=BOOLEAN       ignore whitespace when computing diffs
**   nohdr           omit the description at the top of the page
**
**
** Show all differences between two check-ins.
*/
void vdiff_page(void){
  int ridFrom, ridTo;
  int verboseFlag;
  int sideBySide;
  u64 diffFlags = 0;
  Manifest *pFrom, *pTo;
  ManifestFile *pFileFrom, *pFileTo;
  const char *zBranch;
  const char *zFrom;
  const char *zTo;
  const char *zRe;
  const char *zW;
  const char *zVerbose;
  const char *zGlob;
  ReCompiled *pRe = 0;
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  login_anonymous_available();
  zRe = P("regex");
  if( zRe ) re_compile(&pRe, zRe, 0);
  zBranch = P("branch");
  if( zBranch && zBranch[0] ){
    cgi_replace_parameter("from", mprintf("root:%s", zBranch));
    cgi_replace_parameter("to", zBranch);
  }
  pTo = vdiff_parse_manifest("to", &ridTo);
  if( pTo==0 ) return;
  pFrom = vdiff_parse_manifest("from", &ridFrom);
  if( pFrom==0 ) return;
  sideBySide = !is_false(PD("sbs","1"));
  zVerbose = P("v");
  if( !zVerbose ){
    zVerbose = P("verbose");
  }
  if( !zVerbose ){
    zVerbose = P("detail"); /* deprecated */
  }
  verboseFlag = (zVerbose!=0) && !is_false(zVerbose);
  if( !verboseFlag && sideBySide ) verboseFlag = 1;
  zGlob = P("glob");
  zFrom = P("from");
  zTo = P("to");
  if(zGlob && !*zGlob){
    zGlob = NULL;
  }
  diffFlags = construct_diff_flags(verboseFlag, sideBySide);
  zW = (diffFlags&DIFF_IGNORE_ALLWS)?"&w":"";
  style_submenu_element("Path", "%R/timeline?me=%T&you=%T", zFrom, zTo);
  if( sideBySide || verboseFlag ){
    style_submenu_element("Hide Diff", "%R/vdiff?from=%T&to=%T&sbs=0%s%T%s",
                          zFrom, zTo,
                          zGlob ? "&glob=" : "", zGlob ? zGlob : "", zW);
  }
  if( !sideBySide ){
    style_submenu_element("Side-by-Side Diff",
                          "%R/vdiff?from=%T&to=%T&sbs=1%s%T%s",
                          zFrom, zTo,
                          zGlob ? "&glob=" : "", zGlob ? zGlob : "", zW);
  }
  if( sideBySide || !verboseFlag ) {
    style_submenu_element("Unified Diff",
                          "%R/vdiff?from=%T&to=%T&sbs=0&v%s%T%s",
                          zFrom, zTo,
                          zGlob ? "&glob=" : "", zGlob ? zGlob : "", zW);
  }
  style_submenu_element("Invert",
                        "%R/vdiff?from=%T&to=%T&sbs=%d%s%s%T%s", zTo, zFrom,
                        sideBySide, (verboseFlag && !sideBySide)?"&v":"",
                        zGlob ? "&glob=" : "", zGlob ? zGlob : "", zW);
  if( zGlob ){
    style_submenu_element("Clear glob",
                          "%R/vdiff?from=%T&to=%T&sbs=%d%s%s", zFrom, zTo,
                          sideBySide, (verboseFlag && !sideBySide)?"&v":"", zW);
  }else{
    style_submenu_element("Patch", "%R/vpatch?from=%T&to=%T%s", zFrom, zTo, zW);
  }
  if( sideBySide || verboseFlag ){
    style_submenu_checkbox("w", "Ignore Whitespace", 0, 0);
  }
  style_header("Check-in Differences");
  if( P("nohdr")==0 ){
    @ <h2>Difference From:</h2><blockquote>
    checkin_description(ridFrom);
    @ </blockquote><h2>To:</h2><blockquote>
    checkin_description(ridTo);
    @ </blockquote>
    if( pRe ){
      @ <p><b>Only differences that match regular expression "%h(zRe)"
      @ are shown.</b></p>
    }
    if( zGlob ){
      @ <p><b>Only files matching the glob "%h(zGlob)" are shown.</b></p>
    }
    @<hr /><p>
  }

  manifest_file_rewind(pFrom);
  pFileFrom = manifest_file_next(pFrom, 0);
  manifest_file_rewind(pTo);
  pFileTo = manifest_file_next(pTo, 0);
  while( pFileFrom || pFileTo ){
    int cmp;
    if( pFileFrom==0 ){
      cmp = +1;
    }else if( pFileTo==0 ){
      cmp = -1;
    }else{
      cmp = fossil_strcmp(pFileFrom->zName, pFileTo->zName);
    }
    if( cmp<0 ){
      if( !zGlob || sqlite3_strglob(zGlob, pFileFrom->zName)==0 ){
        append_file_change_line(pFileFrom->zName,
                                pFileFrom->zUuid, 0, 0, diffFlags, pRe, 0);
      }
      pFileFrom = manifest_file_next(pFrom, 0);
    }else if( cmp>0 ){
      if( !zGlob || sqlite3_strglob(zGlob, pFileTo->zName)==0 ){
        append_file_change_line(pFileTo->zName,
                                0, pFileTo->zUuid, 0, diffFlags, pRe,
                                manifest_file_mperm(pFileTo));
      }
      pFileTo = manifest_file_next(pTo, 0);
    }else if( fossil_strcmp(pFileFrom->zUuid, pFileTo->zUuid)==0 ){
      pFileFrom = manifest_file_next(pFrom, 0);
      pFileTo = manifest_file_next(pTo, 0);
    }else{
      if(!zGlob || (sqlite3_strglob(zGlob, pFileFrom->zName)==0
                || sqlite3_strglob(zGlob, pFileTo->zName)==0) ){
        append_file_change_line(pFileFrom->zName,
                                pFileFrom->zUuid,
                                pFileTo->zUuid, 0, diffFlags, pRe,
                                manifest_file_mperm(pFileTo));
      }
      pFileFrom = manifest_file_next(pFrom, 0);
      pFileTo = manifest_file_next(pTo, 0);
    }
  }
  manifest_destroy(pFrom);
  manifest_destroy(pTo);
  append_diff_javascript(sideBySide);
  style_footer();
}

#if INTERFACE
/*
** Possible return values from object_description()
*/
#define OBJTYPE_CHECKIN    0x0001
#define OBJTYPE_CONTENT    0x0002
#define OBJTYPE_WIKI       0x0004
#define OBJTYPE_TICKET     0x0008
#define OBJTYPE_ATTACHMENT 0x0010
#define OBJTYPE_EVENT      0x0020
#define OBJTYPE_TAG        0x0040
#define OBJTYPE_SYMLINK    0x0080
#define OBJTYPE_EXE        0x0100

/*
** Possible flags for the second parameter to
** object_description()
*/
#define OBJDESC_DETAIL      0x0001   /* more detail */
#endif

/*
** Write a description of an object to the www reply.
**
** If the object is a file then mention:
**
**     * It's artifact ID
**     * All its filenames
**     * The check-in it was part of, with times and users
**
** If the object is a manifest, then mention:
**
**     * It's artifact ID
**     * date of check-in
**     * Comment & user
*/
int object_description(
  int rid,                 /* The artifact ID */
  u32 objdescFlags,        /* Flags to control display */
  Blob *pDownloadName      /* Fill with an appropriate download name */
){
  Stmt q;
  int cnt = 0;
  int nWiki = 0;
  int objType = 0;
  char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  int showDetail = (objdescFlags & OBJDESC_DETAIL)!=0;
  char *prevName = 0;

  db_prepare(&q,
    "SELECT filename.name, datetime(event.mtime,toLocal()),"
    "       coalesce(event.ecomment,event.comment),"
    "       coalesce(event.euser,event.user),"
    "       b.uuid, mlink.mperm,"
    "       coalesce((SELECT value FROM tagxref"
                    "  WHERE tagid=%d AND tagtype>0 AND rid=mlink.mid),'trunk'),"
    "       a.size"
    "  FROM mlink, filename, event, blob a, blob b"
    " WHERE filename.fnid=mlink.fnid"
    "   AND event.objid=mlink.mid"
    "   AND a.rid=mlink.fid"
    "   AND b.rid=mlink.mid"
    "   AND mlink.fid=%d"
    "   ORDER BY filename.name, event.mtime /*sort*/",
    TAG_BRANCH, rid
  );
  @ <ul>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zCom = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    const char *zVers = db_column_text(&q, 4);
    int mPerm = db_column_int(&q, 5);
    const char *zBr = db_column_text(&q, 6);
    int szFile = db_column_int(&q,7);
    int sameFilename = prevName!=0 && fossil_strcmp(zName,prevName)==0;
    if( sameFilename && !showDetail ){
      if( cnt==1 ){
        @ %z(href("%R/whatis/%!S",zUuid))[more...]</a>
      }
      cnt++;
      continue;
    }
    if( !sameFilename ){
      if( prevName && showDetail ) {
        @ </ul>
      }
      if( mPerm==PERM_LNK ){
        @ <li>Symbolic link
        objType |= OBJTYPE_SYMLINK;
      }else if( mPerm==PERM_EXE ){
        @ <li>Executable file
        objType |= OBJTYPE_EXE;
      }else{
        @ <li>File
      }
      objType |= OBJTYPE_CONTENT;
      @ %z(href("%R/finfo?name=%T",zName))%h(zName)</a>
      tag_private_status(rid);
      if( showDetail ){
        @ <ul>
      }
      prevName = fossil_strdup(zName);
    }
    if( showDetail ){
      @ <li>
      hyperlink_to_date(zDate,"");
      @ &mdash; part of check-in
      hyperlink_to_uuid(zVers);
    }else{
      @ &mdash; part of check-in
      hyperlink_to_uuid(zVers);
      @ at
      hyperlink_to_date(zDate,"");
    }
    if( zBr && zBr[0] ){
      @ on branch %z(href("%R/timeline?r=%T",zBr))%h(zBr)</a>
    }
    @ &mdash; %!W(zCom) (user:
    hyperlink_to_user(zUser,zDate,",");
    @ size: %d(szFile))
    if( g.perm.Hyperlink ){
      @ %z(href("%R/finfo?name=%T&ci=%!S",zName,zVers))[ancestry]</a>
      @ %z(href("%R/annotate?filename=%T&checkin=%!S",zName,zVers))
      @ [annotate]</a>
      @ %z(href("%R/blame?filename=%T&checkin=%!S",zName,zVers))
      @ [blame]</a>
    }
    cnt++;
    if( pDownloadName && blob_size(pDownloadName)==0 ){
      blob_append(pDownloadName, zName, -1);
    }
  }
  if( prevName && showDetail ){
    @ </ul>
  }
  @ </ul>
  free(prevName);
  db_finalize(&q);
  db_prepare(&q,
    "SELECT substr(tagname, 6, 10000), datetime(event.mtime, toLocal()),"
    "       coalesce(event.euser, event.user)"
    "  FROM tagxref, tag, event"
    " WHERE tagxref.rid=%d"
    "   AND tag.tagid=tagxref.tagid"
    "   AND tag.tagname LIKE 'wiki-%%'"
    "   AND event.objid=tagxref.rid",
    rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPagename = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zUser = db_column_text(&q, 2);
    if( cnt>0 ){
      @ Also wiki page
    }else{
      @ Wiki page
    }
    objType |= OBJTYPE_WIKI;
    @ [%z(href("%R/wiki?name=%t",zPagename))%h(zPagename)</a>] by
    hyperlink_to_user(zUser,zDate," on");
    hyperlink_to_date(zDate,".");
    nWiki++;
    cnt++;
    if( pDownloadName && blob_size(pDownloadName)==0 ){
      blob_appendf(pDownloadName, "%s.txt", zPagename);
    }
  }
  db_finalize(&q);
  if( nWiki==0 ){
    db_prepare(&q,
      "SELECT datetime(mtime, toLocal()), user, comment, type, uuid, tagid"
      "  FROM event, blob"
      " WHERE event.objid=%d"
      "   AND blob.rid=%d",
      rid, rid
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zDate = db_column_text(&q, 0);
      const char *zUser = db_column_text(&q, 1);
      const char *zCom = db_column_text(&q, 2);
      const char *zType = db_column_text(&q, 3);
      const char *zUuid = db_column_text(&q, 4);
      int eventTagId = db_column_int(&q, 5);
      if( cnt>0 ){
        @ Also
      }
      if( zType[0]=='w' ){
        @ Wiki edit
        objType |= OBJTYPE_WIKI;
      }else if( zType[0]=='t' ){
        @ Ticket change
        objType |= OBJTYPE_TICKET;
      }else if( zType[0]=='c' ){
        @ Manifest of check-in
        objType |= OBJTYPE_CHECKIN;
      }else if( zType[0]=='e' ){
        if( eventTagId != 0) {
          @ Instance of technote
          objType |= OBJTYPE_EVENT;
          hyperlink_to_event_tagid(db_column_int(&q, 5));
        }else{
          @ Attachment to technote
        }
      }else{
        @ Tag referencing
      }
      if( zType[0]!='e' || eventTagId == 0){
        hyperlink_to_uuid(zUuid);
      }
      @ - %!W(zCom) by
      hyperlink_to_user(zUser,zDate," on");
      hyperlink_to_date(zDate, ".");
      if( pDownloadName && blob_size(pDownloadName)==0 ){
        blob_appendf(pDownloadName, "%S.txt", zUuid);
      }
      tag_private_status(rid);
      cnt++;
    }
    db_finalize(&q);
  }
  db_prepare(&q,
    "SELECT target, filename, datetime(mtime, toLocal()), user, src"
    "  FROM attachment"
    " WHERE src=(SELECT uuid FROM blob WHERE rid=%d)"
    " ORDER BY mtime DESC /*sort*/",
    rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTarget = db_column_text(&q, 0);
    int nTarget = db_column_bytes(&q, 0);
    const char *zFilename = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    /* const char *zSrc = db_column_text(&q, 4); */
    if( cnt>0 ){
      @ Also attachment "%h(zFilename)" to
    }else{
      @ Attachment "%h(zFilename)" to
    }
    objType |= OBJTYPE_ATTACHMENT;
    if( nTarget==UUID_SIZE && validate16(zTarget,UUID_SIZE) ){
      if ( db_exists("SELECT 1 FROM tag WHERE tagname='tkt-%q'",
            zTarget)
      ){
        if( g.perm.Hyperlink && g.anon.RdTkt ){
          @ ticket [%z(href("%R/tktview?name=%!S",zTarget))%S(zTarget)</a>]
        }else{
          @ ticket [%S(zTarget)]
        }
      }else if( db_exists("SELECT 1 FROM tag WHERE tagname='event-%q'",
            zTarget)
      ){
        if( g.perm.Hyperlink && g.anon.RdWiki ){
          @ tech note [%z(href("%R/technote/%h",zTarget))%S(zTarget)</a>]
        }else{
          @ tech note [%S(zTarget)]
        }
      }else{
        if( g.perm.Hyperlink && g.anon.RdWiki ){
          @ wiki page [%z(href("%R/wiki?name=%t",zTarget))%h(zTarget)</a>]
        }else{
          @ wiki page [%h(zTarget)]
        }
      }
    }else{
      if( g.perm.Hyperlink && g.anon.RdWiki ){
        @ wiki page [%z(href("%R/wiki?name=%t",zTarget))%h(zTarget)</a>]
      }else{
        @ wiki page [%h(zTarget)]
      }
    }
    @ added by
    hyperlink_to_user(zUser,zDate," on");
    hyperlink_to_date(zDate,".");
    cnt++;
    if( pDownloadName && blob_size(pDownloadName)==0 ){
      blob_append(pDownloadName, zFilename, -1);
    }
    tag_private_status(rid);
  }
  db_finalize(&q);
  if( db_exists("SELECT 1 FROM tagxref WHERE rid=%d AND tagid=%d",
                rid, TAG_CLUSTER) ){
    @ Cluster
    cnt++;
  }
  if( cnt==0 ){
    @ Unrecognized artifact
    if( pDownloadName && blob_size(pDownloadName)==0 ){
      blob_appendf(pDownloadName, "%S.txt", zUuid);
    }
    tag_private_status(rid);
  }
  return objType;
}


/*
** WEBPAGE: fdiff
** URL: fdiff?v1=UUID&v2=UUID
**
** Two arguments, v1 and v2, identify the artifacts to be diffed.
** Show diff side by side unless sbs is 0.  Generate plain text if
** "patch" is present, otherwise generate "pretty" HTML.
**
** Alternative URL:  fdiff?from=filename1&to=filename2&ci=checkin
**
** If the "from" and "to" query parameters are both present, then they are
** the names of two files within the check-in "ci" that are diffed.  If the
** "ci" parameter is omitted, then the most recent check-in ("tip") is
** used.
**
** Additional parameters:
**
**      dc=N             Show N lines of context around each diff
**      patch            Use the patch diff format
**      regex=REGEX      Only show differences that match REGEX
**      sbs=BOOLEAN      Turn side-by-side diffs on and off (default: on)
**      verbose=BOOLEAN  Show more detail when describing artifacts
**      w=BOOLEAN        Ignore whitespace
*/
void diff_page(void){
  int v1, v2;
  int isPatch = P("patch")!=0;
  int sideBySide = PB("sbs");
  int verbose = PB("verbose");
  char *zV1;
  char *zV2;
  const char *zRe;
  ReCompiled *pRe = 0;
  u64 diffFlags;
  u32 objdescFlags = 0;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( P("from") && P("to") ){
    v1 = artifact_from_ci_and_filename(0, "from");
    v2 = artifact_from_ci_and_filename(0, "to");
  }else{
    Stmt q;
    v1 = name_to_rid_www("v1");
    v2 = name_to_rid_www("v2");

    /* If the two file versions being compared both have the same
    ** filename, then offer an "Annotate" link that constructs an
    ** annotation between those version. */
    db_prepare(&q,
      "SELECT (SELECT substr(uuid,1,20) FROM blob WHERE rid=a.mid),"
      "       (SELECT substr(uuid,1,20) FROM blob WHERE rid=b.mid),"
      "       (SELECT name FROM filename WHERE filename.fnid=a.fnid)"
      "  FROM mlink a, event ea, mlink b, event eb"
      " WHERE a.fid=%d"
      "   AND b.fid=%d"
      "   AND a.fnid=b.fnid"
      "   AND a.fid!=a.pid"
      "   AND b.fid!=b.pid"
      "   AND ea.objid=a.mid"
      "   AND eb.objid=b.mid"
      " ORDER BY ea.mtime ASC, eb.mtime ASC",
      v1, v2
    );
    if( db_step(&q)==SQLITE_ROW ){
      const char *zCkin = db_column_text(&q, 0);
      const char *zOrig = db_column_text(&q, 1);
      const char *zFN = db_column_text(&q, 2);
      style_submenu_element("Annotate",
        "%R/annotate?origin=%s&checkin=%s&filename=%T",
        zOrig, zCkin, zFN);
    }
    db_finalize(&q);
  }
  if( v1==0 || v2==0 ) fossil_redirect_home();
  zRe = P("regex");
  if( zRe ) re_compile(&pRe, zRe, 0);
  if( verbose ) objdescFlags |= OBJDESC_DETAIL;
  if( isPatch ){
    Blob c1, c2, *pOut;
    pOut = cgi_output_blob();
    cgi_set_content_type("text/plain");
    diffFlags = 4;
    content_get(v1, &c1);
    content_get(v2, &c2);
    text_diff(&c1, &c2, pOut, pRe, diffFlags);
    blob_reset(&c1);
    blob_reset(&c2);
    return;
  }

  zV1 = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", v1);
  zV2 = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", v2);
  diffFlags = construct_diff_flags(1, sideBySide) | DIFF_HTML;

  style_header("Diff");
  style_submenu_checkbox("w", "Ignore Whitespace", 0, 0);
  style_submenu_checkbox("sbs", "Side-by-Side Diff", 0, 0);
  style_submenu_checkbox("verbose", "Verbose", 0, 0);
  style_submenu_element("Patch", "%s/fdiff?v1=%T&v2=%T&patch",
                        g.zTop, P("v1"), P("v2"));

  if( P("smhdr")!=0 ){
    @ <h2>Differences From Artifact
    @ %z(href("%R/artifact/%!S",zV1))[%S(zV1)]</a> To
    @ %z(href("%R/artifact/%!S",zV2))[%S(zV2)]</a>.</h2>
  }else{
    @ <h2>Differences From
    @ Artifact %z(href("%R/artifact/%!S",zV1))[%S(zV1)]</a>:</h2>
    object_description(v1, objdescFlags, 0);
    @ <h2>To Artifact %z(href("%R/artifact/%!S",zV2))[%S(zV2)]</a>:</h2>
    object_description(v2, objdescFlags, 0);
  }
  if( pRe ){
    @ <b>Only differences that match regular expression "%h(zRe)"
    @ are shown.</b>
  }
  @ <hr />
  append_diff(zV1, zV2, diffFlags, pRe);
  append_diff_javascript(sideBySide);
  style_footer();
}

/*
** WEBPAGE: raw
** URL: /raw?name=ARTIFACTID&m=TYPE
** URL: /raw?ci=BRANCH&filename=NAME
**
** Return the uninterpreted content of an artifact.  Used primarily
** to view artifacts that are images.
*/
void rawartifact_page(void){
  int rid = 0;
  char *zUuid;
  const char *zMime;
  Blob content;

  if( P("ci") && P("filename") ){
    rid = artifact_from_ci_and_filename(0, 0);
  }
  if( rid==0 ){
    rid = name_to_rid_www("name");
  }
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( rid==0 ) fossil_redirect_home();
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( fossil_strcmp(P("name"), zUuid)==0 && login_is_nobody() ){
    g.isConst = 1;
  }
  free(zUuid);
  zMime = P("m");
  if( zMime==0 ){
    char *zFName = db_text(0, "SELECT filename.name FROM mlink, filename"
                              " WHERE mlink.fid=%d"
                              "   AND filename.fnid=mlink.fnid", rid);
    if( !zFName ){
      /* Look also at the attachment table */
      zFName = db_text(0, "SELECT attachment.filename FROM attachment, blob"
                          " WHERE blob.rid=%d"
                          "   AND attachment.src=blob.uuid", rid);
    }
    if( zFName ) zMime = mimetype_from_name(zFName);
    if( zMime==0 ) zMime = "application/x-fossil-artifact";
  }
  content_get(rid, &content);
  cgi_set_content_type(zMime);
  cgi_set_content(&content);
}

/*
** Render a hex dump of a file.
*/
static void hexdump(Blob *pBlob){
  const unsigned char *x;
  int n, i, j, k;
  char zLine[100];
  static const char zHex[] = "0123456789abcdef";

  x = (const unsigned char*)blob_buffer(pBlob);
  n = blob_size(pBlob);
  for(i=0; i<n; i+=16){
    j = 0;
    zLine[0] = zHex[(i>>24)&0xf];
    zLine[1] = zHex[(i>>16)&0xf];
    zLine[2] = zHex[(i>>8)&0xf];
    zLine[3] = zHex[i&0xf];
    zLine[4] = ':';
    sqlite3_snprintf(sizeof(zLine), zLine, "%04x: ", i);
    for(j=0; j<16; j++){
      k = 5+j*3;
      zLine[k] = ' ';
      if( i+j<n ){
        unsigned char c = x[i+j];
        zLine[k+1] = zHex[c>>4];
        zLine[k+2] = zHex[c&0xf];
      }else{
        zLine[k+1] = ' ';
        zLine[k+2] = ' ';
      }
    }
    zLine[53] = ' ';
    zLine[54] = ' ';
    for(j=0; j<16; j++){
      k = j+55;
      if( i+j<n ){
        unsigned char c = x[i+j];
        if( c>=0x20 && c<=0x7e ){
          zLine[k] = c;
        }else{
          zLine[k] = '.';
        }
      }else{
        zLine[k] = 0;
      }
    }
    zLine[71] = 0;
    @ %h(zLine)
  }
}

/*
** WEBPAGE: hexdump
** URL: /hexdump?name=ARTIFACTID
**
** Show the complete content of a file identified by ARTIFACTID
** as preformatted text.
**
** Other parameters:
**
**     verbose              Show more detail when describing the object
*/
void hexdump_page(void){
  int rid;
  Blob content;
  Blob downloadName;
  char *zUuid;
  u32 objdescFlags = 0;

  rid = name_to_rid_www("name");
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( rid==0 ) fossil_redirect_home();
  if( g.perm.Admin ){
    const char *zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
    if( db_exists("SELECT 1 FROM shun WHERE uuid=%Q", zUuid) ){
      style_submenu_element("Unshun", "%s/shun?accept=%s&sub=1#delshun",
            g.zTop, zUuid);
    }else{
      style_submenu_element("Shun", "%s/shun?shun=%s#addshun", g.zTop, zUuid);
    }
  }
  style_header("Hex Artifact Content");
  zUuid = db_text("?","SELECT uuid FROM blob WHERE rid=%d", rid);
  if( g.perm.Setup ){
    @ <h2>Artifact %s(zUuid) (%d(rid)):</h2>
  }else{
    @ <h2>Artifact %s(zUuid):</h2>
  }
  blob_zero(&downloadName);
  if( P("verbose")!=0 ) objdescFlags |= OBJDESC_DETAIL;
  object_description(rid, objdescFlags, &downloadName);
  style_submenu_element("Download", "%s/raw/%T?name=%s",
        g.zTop, blob_str(&downloadName), zUuid);
  @ <hr />
  content_get(rid, &content);
  @ <blockquote><pre>
  hexdump(&content);
  @ </pre></blockquote>
  style_footer();
}

/*
** Look for "ci" and "filename" query parameters.  If found, try to
** use them to extract the record ID of an artifact for the file.
**
** Also look for "fn" as an alias for "filename".  If either "filename"
** or "fn" is present but "ci" is missing, use "tip" as a default value
** for "ci".
**
** If zNameParam is not NULL, this use that parameter as the filename
** rather than "fn" or "filename".
**
** If pUrl is not NULL, then record the "ci" and "filename" values in
** pUrl.
**
** At least one of pUrl or zNameParam must be NULL.
*/
int artifact_from_ci_and_filename(HQuery *pUrl, const char *zNameParam){
  const char *zFilename;
  const char *zCI;
  int cirid;
  Manifest *pManifest;
  ManifestFile *pFile;

  if( zNameParam ){
    zFilename = P(zNameParam);
  }else{
    zFilename = P("filename");
    if( zFilename==0 ){
      zFilename = P("fn");
    }
  }
  if( zFilename==0 ) return 0;

  zCI = P("ci");
  cirid = name_to_typed_rid(zCI ? zCI : "tip", "ci");
  if( cirid<=0 ) return 0;
  pManifest = manifest_get(cirid, CFTYPE_MANIFEST, 0);
  if( pManifest==0 ) return 0;
  manifest_file_rewind(pManifest);
  while( (pFile = manifest_file_next(pManifest,0))!=0 ){
    if( fossil_strcmp(zFilename, pFile->zName)==0 ){
      int rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q", pFile->zUuid);
      manifest_destroy(pManifest);
      if( pUrl ){
        assert( zNameParam==0 );
        url_add_parameter(pUrl, "fn", zFilename);
        if( zCI ) url_add_parameter(pUrl, "ci", zCI);
      }
      return rid;
    }
  }
  manifest_destroy(pManifest);
  return 0;
}

/*
** The "z" argument is a string that contains the text of a source code
** file.  This routine appends that text to the HTTP reply with line numbering.
**
** zLn is the ?ln= parameter for the HTTP query.  If there is an argument,
** then highlight that line number and scroll to it once the page loads.
** If there are two line numbers, highlight the range of lines.
** Multiple ranges can be highlighed by adding additional line numbers
** separated by a non-digit character (also not one of [-,.]).
*/
void output_text_with_line_numbers(
  const char *z,
  const char *zLn
){
  int iStart, iEnd;    /* Start and end of region to highlight */
  int n = 0;           /* Current line number */
  int i = 0;           /* Loop index */
  int iTop = 0;        /* Scroll so that this line is on top of screen. */
  Stmt q;

  iStart = iEnd = atoi(zLn);
  db_multi_exec(
    "CREATE TEMP TABLE lnos(iStart INTEGER PRIMARY KEY, iEnd INTEGER)");
  if( iStart>0 ){
    do{
      while( fossil_isdigit(zLn[i]) ) i++;
      if( zLn[i]==',' || zLn[i]=='-' || zLn[i]=='.' ){
        i++;
        while( zLn[i]=='.' ){ i++; }
        iEnd = atoi(&zLn[i]);
        while( fossil_isdigit(zLn[i]) ) i++;
      }
      while( fossil_isdigit(zLn[i]) ) i++;
      if( iEnd<iStart ) iEnd = iStart;
      db_multi_exec(
        "INSERT OR REPLACE INTO lnos VALUES(%d,%d)", iStart, iEnd
      );
      iStart = iEnd = atoi(&zLn[i++]);
    }while( zLn[i] && iStart && iEnd );
  }
  db_prepare(&q, "SELECT min(iStart), iEnd FROM lnos");
  if( db_step(&q)==SQLITE_ROW ){
    iStart = db_column_int(&q, 0);
    iEnd = db_column_int(&q, 1);
    iTop = iStart - 15 + (iEnd-iStart)/4;
    if( iTop>iStart - 2 ) iTop = iStart-2;
  }
  db_finalize(&q);
  @ <pre>
  while( z[0] ){
    n++;
    db_prepare(&q,
      "SELECT min(iStart), max(iEnd) FROM lnos"
      " WHERE iStart <= %d AND iEnd >= %d", n, n);
    if( db_step(&q)==SQLITE_ROW ){
      iStart = db_column_int(&q, 0);
      iEnd = db_column_int(&q, 1);
    }
    db_finalize(&q);
    for(i=0; z[i] && z[i]!='\n'; i++){}
    if( n==iTop ) cgi_append_content("<span id=\"topln\">", -1);
    if( n==iStart ){
      cgi_append_content("<div class=\"selectedText\">",-1);
    }
    cgi_printf("%6d  ", n);
    if( i>0 ){
      char *zHtml = htmlize(z, i);
      cgi_append_content(zHtml, -1);
      fossil_free(zHtml);
    }
    if( n==iTop ) cgi_append_content("</span>", -1);
    if( n==iEnd ) cgi_append_content("</div>", -1);
    else cgi_append_content("\n", 1);
    z += i;
    if( z[0]=='\n' ) z++;
  }
  if( n<iEnd ) cgi_printf("</div>");
  @ </pre>
  if( db_int(0, "SELECT EXISTS(SELECT 1 FROM lnos)") ){
    @ <script>gebi('topln').scrollIntoView(true);</script>
  }
}


/*
** WEBPAGE: artifact
** WEBPAGE: file
** WEBPAGE: whatis
**
** Typical usage:
**
**    /artifact/HASH
**    /whatis/HASH
**    /file/NAME
**
** Additional query parameters:
**
**   ln              - show line numbers
**   ln=N            - highlight line number N
**   ln=M-N          - highlight lines M through N inclusive
**   ln=M-N+Y-Z      - highlight lines M through N and Y through Z (inclusive)
**   verbose         - show more detail in the description
**   download        - redirect to the download (artifact page only)
**   name=SHA1HASH   - Provide the SHA1HASH as a query parameter
**   filename=NAME   - Show information for content file NAME
**   fn=NAME         - "fn" is shorthand for "filename"
**   ci=VERSION      - The specific check-in to use for "filename=".
**
** The /artifact page show the complete content of a file
** identified by HASH as preformatted text.  The
** /whatis page shows only a description of the file.  The /file
** page shows the most recent version of the file or directory
** called NAME, or a list of the top-level directory if NAME is
** omitted.
*/
void artifact_page(void){
  int rid = 0;
  Blob content;
  const char *zMime;
  Blob downloadName;
  int renderAsWiki = 0;
  int renderAsHtml = 0;
  int objType;
  int asText;
  const char *zUuid;
  u32 objdescFlags = 0;
  int descOnly = fossil_strcmp(g.zPath,"whatis")==0;
  int isFile = fossil_strcmp(g.zPath,"file")==0;
  const char *zLn = P("ln");
  const char *zName = P("name");
  HQuery url;

  url_initialize(&url, g.zPath);
  rid = artifact_from_ci_and_filename(&url, 0);
  if( rid==0 ){
    url_add_parameter(&url, "name", zName);
    if( isFile ){
      /* Do a top-level directory listing in /file mode if no argument
      ** specified */
      if( zName==0 || zName[0]==0 ){
        if( P("ci")==0 ) cgi_set_query_parameter("ci","tip");
        page_tree();
        return;
      }
      /* Look for a single file with the given name */
      rid = db_int(0,
         "SELECT fid FROM filename, mlink, event"
         " WHERE name=%Q"
         "   AND mlink.fnid=filename.fnid"
         "   AND event.objid=mlink.mid"
         " ORDER BY event.mtime DESC LIMIT 1",
         zName
      );
      /* If no file called NAME exists, instead look for a directory
      ** with that name, and do a directory listing */
      if( rid==0 ){
        int nName = (int)strlen(zName);
        if( nName && zName[nName-1]=='/' ) nName--;
        if( db_exists(
           "SELECT 1 FROM filename"
           " WHERE name GLOB '%.*q/*' AND substr(name,1,%d)=='%.*q/';",
           nName, zName, nName+1, nName, zName
        ) ){
          if( P("ci")==0 ) cgi_set_query_parameter("ci","tip");
          page_tree();
          return;
        }
      }
      /* If no file or directory called NAME: issue an error */
      if( rid==0 ){
        style_header("No such file");
        @ File '%h(zName)' does not exist in this repository.
        style_footer();
        return;
      }
    }else{
      rid = name_to_rid_www("name");
    }
  }

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( rid==0 ){
    style_header("No such artifact");
    @ Artifact '%h(zName)' does not exist in this repository.
    style_footer();
    return;
  }
  if( descOnly || P("verbose")!=0 ){
    url_add_parameter(&url, "verbose", "1");
    objdescFlags |= OBJDESC_DETAIL;
  }
  zUuid = db_text("?", "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( isFile ){
    @ <h2>Latest version of file '%h(zName)':</h2>
    style_submenu_element("Artifact", "%R/artifact/%S", zUuid);
  }else if( g.perm.Setup ){
    @ <h2>Artifact %s(zUuid) (%d(rid)):</h2>
  }else{
    @ <h2>Artifact %s(zUuid):</h2>
  }
  blob_zero(&downloadName);
  objType = object_description(rid, objdescFlags, &downloadName);
  if( !descOnly && P("download")!=0 ){
    cgi_redirectf("%R/raw/%T?name=%s", blob_str(&downloadName),
          db_text("?", "SELECT uuid FROM blob WHERE rid=%d", rid));
    /*NOTREACHED*/
  }
  if( g.perm.Admin ){
    const char *zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
    if( db_exists("SELECT 1 FROM shun WHERE uuid=%Q", zUuid) ){
      style_submenu_element("Unshun", "%s/shun?accept=%s&sub=1#accshun",
            g.zTop, zUuid);
    }else{
      style_submenu_element("Shun", "%s/shun?shun=%s#addshun", g.zTop, zUuid);
    }
  }
  style_header("%s", isFile ? "File Content" :
                     descOnly ? "Artifact Description" : "Artifact Content");
  if( g.perm.Admin ){
    Stmt q;
    db_prepare(&q,
      "SELECT coalesce(user.login,rcvfrom.uid),"
      "       datetime(rcvfrom.mtime,toLocal()), rcvfrom.ipaddr"
      "  FROM blob, rcvfrom LEFT JOIN user ON user.uid=rcvfrom.uid"
      " WHERE blob.rid=%d"
      "   AND rcvfrom.rcvid=blob.rcvid;", rid);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zUser = db_column_text(&q,0);
      const char *zDate = db_column_text(&q,1);
      const char *zIp = db_column_text(&q,2);
      @ <p>Received on %s(zDate) from %h(zUser) at %h(zIp).</p>
    }
    db_finalize(&q);
  }
  style_submenu_element("Download", "%R/raw/%T?name=%s",
                         blob_str(&downloadName), zUuid);
  if( db_exists("SELECT 1 FROM mlink WHERE fid=%d", rid) ){
    style_submenu_element("Check-ins Using", "%R/timeline?n=200&uf=%s", zUuid);
  }
  asText = P("txt")!=0;
  zMime = mimetype_from_name(blob_str(&downloadName));
  if( zMime ){
    if( fossil_strcmp(zMime, "text/html")==0 ){
      if( asText ){
        style_submenu_element("Html", "%s", url_render(&url, "txt", 0, 0, 0));
      }else{
        renderAsHtml = 1;
        style_submenu_element("Text", "%s", url_render(&url, "txt", "1", 0, 0));
      }
    }else if( fossil_strcmp(zMime, "text/x-fossil-wiki")==0
           || fossil_strcmp(zMime, "text/x-markdown")==0 ){
      if( asText ){
        style_submenu_element("Wiki", "%s", url_render(&url, "txt", 0, 0, 0));
      }else{
        renderAsWiki = 1;
        style_submenu_element("Text", "%s", url_render(&url, "txt", "1", 0, 0));
      }
    }
  }
  if( (objType & (OBJTYPE_WIKI|OBJTYPE_TICKET))!=0 ){
    style_submenu_element("Parsed", "%R/info/%s", zUuid);
  }
  if( descOnly ){
    style_submenu_element("Content", "%R/artifact/%s", zUuid);
  }else{
    if( zLn==0 || atoi(zLn)==0 ){
      style_submenu_checkbox("ln", "Line Numbers", 0, 0);
    }
    @ <hr />
    content_get(rid, &content);
    if( renderAsWiki ){
      wiki_render_by_mimetype(&content, zMime);
    }else if( renderAsHtml ){
      @ <iframe src="%R/raw/%T(blob_str(&downloadName))?name=%s(zUuid)"
      @ width="100%%" frameborder="0" marginwidth="0" marginheight="0"
      @ sandbox="allow-same-origin"
      @ onload="this.height=this.contentDocument.documentElement.scrollHeight;">
      @ </iframe>
    }else{
      style_submenu_element("Hex", "%s/hexdump?name=%s", g.zTop, zUuid);
      blob_to_utf8_no_bom(&content, 0);
      zMime = mimetype_from_content(&content);
      @ <blockquote>
      if( zMime==0 ){
        const char *z;
        z = blob_str(&content);
        if( zLn ){
          output_text_with_line_numbers(z, zLn);
        }else{
          @ <pre>
          @ %h(z)
          @ </pre>
        }
      }else if( strncmp(zMime, "image/", 6)==0 ){
        @ <i>(file is %d(blob_size(&content)) bytes of image data)</i><br />
        @ <img src="%R/raw/%s(zUuid)?m=%s(zMime)" />
        style_submenu_element("Image", "%R/raw/%s?m=%s", zUuid, zMime);
      }else{
        @ <i>(file is %d(blob_size(&content)) bytes of binary data)</i>
      }
      @ </blockquote>
    }
  }
  style_footer();
}

/*
** WEBPAGE: tinfo
** URL: /tinfo?name=ARTIFACTID
**
** Show the details of a ticket change control artifact.
*/
void tinfo_page(void){
  int rid;
  char *zDate;
  const char *zUuid;
  char zTktName[UUID_SIZE+1];
  Manifest *pTktChng;
  int modPending;
  const char *zModAction;
  char *zTktTitle;
  login_check_credentials();
  if( !g.perm.RdTkt ){ login_needed(g.anon.RdTkt); return; }
  rid = name_to_rid_www("name");
  if( rid==0 ){ fossil_redirect_home(); }
  zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( g.perm.Admin ){
    if( db_exists("SELECT 1 FROM shun WHERE uuid=%Q", zUuid) ){
      style_submenu_element("Unshun", "%s/shun?accept=%s&sub=1#accshun",
            g.zTop, zUuid);
    }else{
      style_submenu_element("Shun", "%s/shun?shun=%s#addshun", g.zTop, zUuid);
    }
  }
  pTktChng = manifest_get(rid, CFTYPE_TICKET, 0);
  if( pTktChng==0 ) fossil_redirect_home();
  zDate = db_text(0, "SELECT datetime(%.12f)", pTktChng->rDate);
  memcpy(zTktName, pTktChng->zTicketUuid, UUID_SIZE+1);
  if( g.perm.ModTkt && (zModAction = P("modaction"))!=0 ){
    if( strcmp(zModAction,"delete")==0 ){
      moderation_disapprove(rid);
      /*
      ** Next, check if the ticket still exists; if not, we cannot
      ** redirect to it.
      */
      if( db_exists("SELECT 1 FROM ticket WHERE tkt_uuid GLOB '%q*'",
                    zTktName) ){
        cgi_redirectf("%R/tktview/%s", zTktName);
        /*NOTREACHED*/
      }else{
        cgi_redirectf("%R/modreq");
        /*NOTREACHED*/
      }
    }
    if( strcmp(zModAction,"approve")==0 ){
      moderation_approve(rid);
    }
  }
  zTktTitle = db_table_has_column("repository", "ticket", "title" )
      ? db_text("(No title)", "SELECT title FROM ticket WHERE tkt_uuid=%Q", zTktName)
      : 0;
  style_header("Ticket Change Details");
  style_submenu_element("Raw", "%R/artifact/%s", zUuid);
  style_submenu_element("History", "%R/tkthistory/%s", zTktName);
  style_submenu_element("Page", "%R/tktview/%t", zTktName);
  style_submenu_element("Timeline", "%R/tkttimeline/%t", zTktName);
  if( P("plaintext") ){
    style_submenu_element("Formatted", "%R/info/%s", zUuid);
  }else{
    style_submenu_element("Plaintext", "%R/info/%s?plaintext", zUuid);
  }

  @ <div class="section">Overview</div>
  @ <p><table class="label-value">
  @ <tr><th>Artifact&nbsp;ID:</th>
  @ <td>%z(href("%R/artifact/%!S",zUuid))%s(zUuid)</a>
  if( g.perm.Setup ){
    @ (%d(rid))
  }
  modPending = moderation_pending(rid);
  if( modPending ){
    @ <span class="modpending">*** Awaiting Moderator Approval ***</span>
  }
  @ <tr><th>Ticket:</th>
  @ <td>%z(href("%R/tktview/%s",zTktName))%s(zTktName)</a>
  if( zTktTitle ){
        @<br />%h(zTktTitle)
  }
  @</td></tr>
  @ <tr><th>Date:</th><td>
  hyperlink_to_date(zDate, "</td></tr>");
  @ <tr><th>User:</th><td>
  hyperlink_to_user(pTktChng->zUser, zDate, "</td></tr>");
  @ </table>
  free(zDate);
  free(zTktTitle);

  if( g.perm.ModTkt && modPending ){
    @ <div class="section">Moderation</div>
    @ <blockquote>
    @ <form method="POST" action="%R/tinfo/%s(zUuid)">
    @ <label><input type="radio" name="modaction" value="delete">
    @ Delete this change</label><br />
    @ <label><input type="radio" name="modaction" value="approve">
    @ Approve this change</label><br />
    @ <input type="submit" value="Submit">
    @ </form>
    @ </blockquote>
  }

  @ <div class="section">Changes</div>
  @ <p>
  ticket_output_change_artifact(pTktChng, 0);
  manifest_destroy(pTktChng);
  style_footer();
}


/*
** WEBPAGE: info
** URL: info/ARTIFACTID
**
** The argument is a artifact ID which might be a check-in or a file or
** a ticket changes or a wiki edit or something else.
**
** Figure out what the artifact ID is and display it appropriately.
*/
void info_page(void){
  const char *zName;
  Blob uuid;
  int rid;
  int rc;
  int nLen;

  zName = P("name");
  if( zName==0 ) fossil_redirect_home();
  nLen = strlen(zName);
  blob_set(&uuid, zName);
  if( name_collisions(zName) ){
    cgi_set_parameter("src","info");
    ambiguous_page();
    return;
  }
  rc = name_to_uuid(&uuid, -1, "*");
  if( rc==1 ){
    if( validate16(zName, nLen) ){
      if( db_exists("SELECT 1 FROM ticket WHERE tkt_uuid GLOB '%q*'", zName) ){
        tktview_page();
        return;
      }
      if( db_exists("SELECT 1 FROM tag"
                    " WHERE tagname GLOB 'event-%q*'", zName) ){
        event_page();
        return;
      }
    }
    style_header("No Such Object");
    @ <p>No such object: %h(zName)</p>
    if( nLen<4 ){
      @ <p>Object name should be no less than 4 characters.  Ten or more
      @ characters are recommended.</p>
    }
    style_footer();
    return;
  }else if( rc==2 ){
    cgi_set_parameter("src","info");
    ambiguous_page();
    return;
  }
  zName = blob_str(&uuid);
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q", zName);
  if( rid==0 ){
    style_header("Broken Link");
    @ <p>No such object: %h(zName)</p>
    style_footer();
    return;
  }
  if( db_exists("SELECT 1 FROM mlink WHERE mid=%d", rid) ){
    ci_page();
  }else
  if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                " WHERE rid=%d AND tagname LIKE 'wiki-%%'", rid) ){
    winfo_page();
  }else
  if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                " WHERE rid=%d AND tagname LIKE 'tkt-%%'", rid) ){
    tinfo_page();
  }else
  if( db_exists("SELECT 1 FROM plink WHERE cid=%d", rid) ){
    ci_page();
  }else
  if( db_exists("SELECT 1 FROM plink WHERE pid=%d", rid) ){
    ci_page();
  }else
  if( db_exists("SELECT 1 FROM attachment WHERE attachid=%d", rid) ){
    ainfo_page();
  }else
  {
    artifact_page();
  }
}

/*
** Generate HTML that will present the user with a selection of
** potential background colors for timeline entries.
*/
void render_color_chooser(
  int fPropagate,             /* Default value for propagation */
  const char *zDefaultColor,  /* The current default color */
  const char *zIdPropagate,   /* ID of form element checkbox.  NULL for none */
  const char *zId,            /* The ID of the form element */
  const char *zIdCustom       /* ID of text box for custom color */
){
  static const struct SampleColors {
     const char *zCName;
     const char *zColor;
  } aColor[] = {
     { "(none)",  "" },
     { "#f2dcdc", 0 },
     { "#bde5d6", 0 },
     { "#a0a0a0", 0 },
     { "#b0b0b0", 0 },
     { "#c0c0c0", 0 },
     { "#d0d0d0", 0 },
     { "#e0e0e0", 0 },

     { "#c0fff0", 0 },
     { "#c0f0ff", 0 },
     { "#d0c0ff", 0 },
     { "#ffc0ff", 0 },
     { "#ffc0d0", 0 },
     { "#fff0c0", 0 },
     { "#f0ffc0", 0 },
     { "#c0ffc0", 0 },

     { "#a8d3c0", 0 },
     { "#a8c7d3", 0 },
     { "#aaa8d3", 0 },
     { "#cba8d3", 0 },
     { "#d3a8bc", 0 },
     { "#d3b5a8", 0 },
     { "#d1d3a8", 0 },
     { "#b1d3a8", 0 },

     { "#8eb2a1", 0 },
     { "#8ea7b2", 0 },
     { "#8f8eb2", 0 },
     { "#ab8eb2", 0 },
     { "#b28e9e", 0 },
     { "#b2988e", 0 },
     { "#b0b28e", 0 },
     { "#95b28e", 0 },

     { "#80d6b0", 0 },
     { "#80bbd6", 0 },
     { "#8680d6", 0 },
     { "#c680d6", 0 },
     { "#d680a6", 0 },
     { "#d69b80", 0 },
     { "#d1d680", 0 },
     { "#91d680", 0 },


     { "custom",  "##" },
  };
  int nColor = count(aColor)-1;
  int stdClrFound = 0;
  int i;

  if( zIdPropagate ){
    @ <div><label>
    if( fPropagate ){
      @ <input type="checkbox" name="%s(zIdPropagate)" checked="checked" />
    }else{
      @ <input type="checkbox" name="%s(zIdPropagate)" />
    }
    @ Propagate color to descendants</label></div>
  }
  @ <table border="0" cellpadding="0" cellspacing="1" class="colorpicker">
  @ <tr>
  for(i=0; i<nColor; i++){
    const char *zClr = aColor[i].zColor;
    if( zClr==0 ) zClr = aColor[i].zCName;
    if( zClr[0] ){
      @ <td style="background-color: %h(zClr);">
    }else{
      @ <td>
    }
    @ <label>
    if( fossil_strcmp(zDefaultColor, zClr)==0 ){
      @ <input type="radio" name="%s(zId)" value="%h(zClr)"
      @  checked="checked" />
      stdClrFound=1;
    }else{
      @ <input type="radio" name="%s(zId)" value="%h(zClr)" />
    }
    @ %h(aColor[i].zCName)</label></td>
    if( (i%8)==7 && i+1<nColor ){
      @ </tr><tr>
    }
  }
  @ </tr><tr>
  if( stdClrFound ){
    @ <td colspan="6"><label>
    @ <input type="radio" name="%s(zId)" value="%h(aColor[nColor].zColor)"
    @  onclick="gebi('%s(zIdCustom)').select();" />
  }else{
    @ <td style="background-color: %h(zDefaultColor);" colspan="6"><label>
    @ <input type="radio" name="%s(zId)" value="%h(aColor[nColor].zColor)"
    @  checked="checked" onclick="gebi('%s(zIdCustom)').select();" />
  }
  @ %h(aColor[i].zCName)</label>&nbsp;
  @ <input type="text" name="%s(zIdCustom)"
  @  id="%s(zIdCustom)" class="checkinUserColor"
  @  value="%h(stdClrFound?"":zDefaultColor)"
  @  onfocus="this.form.elements['%s(zId)'][%d(nColor)].checked = true;"
  @  onload="this.blur();"
  @  onblur="this.parentElement.style.backgroundColor = this.value ? ('#'+this.value.replace('#','')) : '';" />
  @ </td>
  @ </tr>
  @ </table>
}

/*
** Do a comment comparison.
**
** +  Leading and trailing whitespace are ignored.
** +  \r\n characters compare equal to \n
**
** Return true if equal and false if not equal.
*/
static int comment_compare(const char *zA, const char *zB){
  if( zA==0 ) zA = "";
  if( zB==0 ) zB = "";
  while( fossil_isspace(zA[0]) ) zA++;
  while( fossil_isspace(zB[0]) ) zB++;
  while( zA[0] && zB[0] ){
    if( zA[0]==zB[0] ){ zA++; zB++; continue; }
    if( zA[0]=='\r' && zA[1]=='\n' && zB[0]=='\n' ){
      zA += 2;
      zB++;
      continue;
    }
    if( zB[0]=='\r' && zB[1]=='\n' && zA[0]=='\n' ){
      zB += 2;
      zA++;
      continue;
    }
    return 0;
  }
  while( fossil_isspace(zB[0]) ) zB++;
  while( fossil_isspace(zA[0]) ) zA++;
  return zA[0]==0 && zB[0]==0;
}

/*
** The following methods operate on the newtags temporary table
** that is used to collect various changes to be added to a control
** artifact for a check-in edit.
*/
static void init_newtags(void){
  db_multi_exec("CREATE TEMP TABLE newtags(tag UNIQUE, prefix, value)");
}

static void change_special(
  const char *zName,    /* Name of the special tag */
  const char *zOp,      /* Operation prefix (e.g. +,-,*) */
  const char *zValue    /* Value of the tag */
){
  db_multi_exec("REPLACE INTO newtags VALUES(%Q,'%q',%Q)", zName, zOp, zValue);
}

static void change_sym_tag(const char *zTag, const char *zOp){
  db_multi_exec("REPLACE INTO newtags VALUES('sym-%q',%Q,NULL)", zTag, zOp);
}

static void cancel_special(const char *zTag){
  change_special(zTag,"-",0);
}

static void add_color(const char *zNewColor, int fPropagateColor){
  change_special("bgcolor",fPropagateColor ? "*" : "+", zNewColor);
}

static void cancel_color(void){
  change_special("bgcolor","-",0);
}

static void add_comment(const char *zNewComment){
  change_special("comment","+",zNewComment);
}

static void add_date(const char *zNewDate){
  change_special("date","+",zNewDate);
}

static void add_user(const char *zNewUser){
  change_special("user","+",zNewUser);
}

static void add_tag(const char *zNewTag){
  change_sym_tag(zNewTag,"+");
}

static void cancel_tag(int rid, const char *zCancelTag){
  if( db_exists("SELECT 1 FROM tagxref, tag"
                " WHERE tagxref.rid=%d AND tagtype>0"
                "   AND tagxref.tagid=tag.tagid AND tagname='sym-%q'",
                rid, zCancelTag)
  ) change_sym_tag(zCancelTag,"-");
}

static void hide_branch(void){
  change_special("hidden","*",0);
}

static void close_leaf(int rid){
  change_special("closed",is_a_leaf(rid)?"+":"*",0);
}

static void change_branch(int rid, const char *zNewBranch){
  db_multi_exec(
    "REPLACE INTO newtags "
    " SELECT tagname, '-', NULL FROM tagxref, tag"
    "  WHERE tagxref.rid=%d AND tagtype==2"
    "    AND tagname GLOB 'sym-*'"
    "    AND tag.tagid=tagxref.tagid",
    rid
  );
  change_special("branch","*",zNewBranch);
  change_sym_tag(zNewBranch,"*");
}

/*
** The apply_newtags method is called after all newtags have been added
** and the control artifact is completed and then written to the DB.
*/
static void apply_newtags(Blob *ctrl, int rid, const char *zUuid){
  Stmt q;
  int nChng = 0;

  db_prepare(&q, "SELECT tag, prefix, value FROM newtags"
                 " ORDER BY prefix || tag");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTag = db_column_text(&q, 0);
    const char *zPrefix = db_column_text(&q, 1);
    const char *zValue = db_column_text(&q, 2);
    nChng++;
    if( zValue ){
      blob_appendf(ctrl, "T %s%F %s %F\n", zPrefix, zTag, zUuid, zValue);
    }else{
      blob_appendf(ctrl, "T %s%F %s\n", zPrefix, zTag, zUuid);
    }
  }
  db_finalize(&q);
  if( nChng>0 ){
    int nrid;
    Blob cksum;
    blob_appendf(ctrl, "U %F\n", login_name());
    md5sum_blob(ctrl, &cksum);
    blob_appendf(ctrl, "Z %b\n", &cksum);
    db_begin_transaction();
    g.markPrivate = content_is_private(rid);
    nrid = content_put(ctrl);
    manifest_crosslink(nrid, ctrl, MC_PERMIT_HOOKS);
    assert( blob_is_reset(ctrl) );
    db_end_transaction(0);
  }
}

/*
** This method checks that the date can be parsed.
** Returns 1 if datetime() can validate, 0 otherwise.
*/
int is_datetime(const char* zDate){
  return db_int(0, "SELECT datetime(%Q) NOT NULL", zDate);
}

/*
** WEBPAGE: ci_edit
** URL:  /ci_edit?r=RID&c=NEWCOMMENT&u=NEWUSER
**
** Present a dialog for updating properties of a check-in.
**
**     *  The check-in user
**     *  The check-in comment
**     *  The check-in time and date
**     *  The background color.
**     *  Add and remove tags
*/
void ci_edit_page(void){
  int rid;
  const char *zComment;         /* Current comment on the check-in */
  const char *zNewComment;      /* Revised check-in comment */
  const char *zUser;            /* Current user for the check-in */
  const char *zNewUser;         /* Revised user */
  const char *zDate;            /* Current date of the check-in */
  const char *zNewDate;         /* Revised check-in date */
  const char *zColor;
  const char *zNewColor;
  const char *zNewTagFlag;
  const char *zNewTag;
  const char *zNewBrFlag;
  const char *zNewBranch;
  const char *zCloseFlag;
  const char *zHideFlag;
  int fPropagateColor;          /* True if color propagates before edit */
  int fNewPropagateColor;       /* True if color propagates after edit */
  int fHasHidden = 0;           /* True if hidden tag already set */
  int fHasClosed = 0;           /* True if closed tag already set */
  const char *zChngTime = 0;     /* Value of chngtime= query param, if any */
  char *zUuid;
  Blob comment;
  char *zBranchName = 0;
  Stmt q;

  login_check_credentials();
  if( !g.perm.Write ){ login_needed(g.anon.Write); return; }
  rid = name_to_typed_rid(P("r"), "ci");
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  zComment = db_text(0, "SELECT coalesce(ecomment,comment)"
                        "  FROM event WHERE objid=%d", rid);
  if( zComment==0 ) fossil_redirect_home();
  if( P("cancel") ){
    cgi_redirectf("ci?name=%s", zUuid);
  }
  if( g.perm.Setup ) zChngTime = P("chngtime");
  zNewComment = PD("c",zComment);
  zUser = db_text(0, "SELECT coalesce(euser,user)"
                     "  FROM event WHERE objid=%d", rid);
  if( zUser==0 ) fossil_redirect_home();
  zNewUser = PDT("u",zUser);
  zDate = db_text(0, "SELECT datetime(mtime)"
                     "  FROM event WHERE objid=%d", rid);
  if( zDate==0 ) fossil_redirect_home();
  zNewDate = PDT("dt",zDate);
  zColor = db_text("", "SELECT bgcolor"
                        "  FROM event WHERE objid=%d", rid);
  zNewColor = PDT("clr",zColor);
  if( fossil_strcmp(zNewColor,"##")==0 ){
    zNewColor = PT("clrcust");
  }
  fPropagateColor = db_int(0, "SELECT tagtype FROM tagxref"
                              " WHERE rid=%d AND tagid=%d",
                              rid, TAG_BGCOLOR)==2;
  fNewPropagateColor = P("clr")!=0 ? P("pclr")!=0 : fPropagateColor;
  zNewTagFlag = P("newtag") ? " checked" : "";
  zNewTag = PDT("tagname","");
  zNewBrFlag = P("newbr") ? " checked" : "";
  zNewBranch = PDT("brname","");
  zCloseFlag = P("close") ? " checked" : "";
  zHideFlag = P("hide") ? " checked" : "";
  if( P("apply") ){
    Blob ctrl;
    char *zNow;

    login_verify_csrf_secret();
    blob_zero(&ctrl);
    zNow = date_in_standard_format(zChngTime ? zChngTime : "now");
    blob_appendf(&ctrl, "D %s\n", zNow);
    init_newtags();
    if( zNewColor[0]
     && (fPropagateColor!=fNewPropagateColor
             || fossil_strcmp(zColor,zNewColor)!=0)
    ) add_color(zNewColor,fNewPropagateColor);
    if( zNewColor[0]==0 && zColor[0]!=0 ) cancel_color();
    if( comment_compare(zComment,zNewComment)==0 ) add_comment(zNewComment);
    if( fossil_strcmp(zDate,zNewDate)!=0 ) add_date(zNewDate);
    if( fossil_strcmp(zUser,zNewUser)!=0 ) add_user(zNewUser);
    db_prepare(&q,
       "SELECT tag.tagid, tagname FROM tagxref, tag"
       " WHERE tagxref.rid=%d AND tagtype>0 AND tagxref.tagid=tag.tagid",
       rid
    );
    while( db_step(&q)==SQLITE_ROW ){
      int tagid = db_column_int(&q, 0);
      const char *zTag = db_column_text(&q, 1);
      char zLabel[30];
      sqlite3_snprintf(sizeof(zLabel), zLabel, "c%d", tagid);
      if( P(zLabel) ) cancel_special(zTag);
    }
    db_finalize(&q);
    if( zHideFlag[0] ) hide_branch();
    if( zCloseFlag[0] ) close_leaf(rid);
    if( zNewTagFlag[0] && zNewTag[0] ) add_tag(zNewTag);
    if( zNewBrFlag[0] && zNewBranch[0] ) change_branch(rid,zNewBranch);
    apply_newtags(&ctrl, rid, zUuid);
    cgi_redirectf("ci?name=%s", zUuid);
  }
  blob_zero(&comment);
  blob_append(&comment, zNewComment, -1);
  zUuid[10] = 0;
  style_header("Edit Check-in [%s]", zUuid);
  /*
  ** chgcbn/chgbn: Handle change of (checkbox for) branch name in
  ** remaining of form.
  */
  @ <script>
  @ function chgcbn(checked, branch){
  @   val = gebi('brname').value.trim();
  @   if( !val || !checked ) val = branch;
  @   if( checked ) gebi('brname').select();
  @   gebi('hbranch').textContent = val;
  @   cidbrid = document.getElementById('cbranch');
  @   if( cidbrid ) cidbrid.textContent = val;
  @ }
  @ function chgbn(val, branch){
  @   if( !val ) val = branch;
  @   gebi('newbr').checked = (val!=branch);
  @   gebi('hbranch').textContent = val;
  @   cidbrid = document.getElementById('cbranch');
  @   if( cidbrid ) cidbrid.textContent = val;
  @ }
  @ </script>
  if( P("preview") ){
    Blob suffix;
    int nTag = 0;
    @ <b>Preview:</b>
    @ <blockquote>
    @ <table border=0>
    if( zNewColor && zNewColor[0] ){
      @ <tr><td style="background-color: %h(zNewColor);">
    }else{
      @ <tr><td>
    }
    @ %!W(blob_str(&comment))
    blob_zero(&suffix);
    blob_appendf(&suffix, "(user: %h", zNewUser);
    db_prepare(&q, "SELECT substr(tagname,5) FROM tagxref, tag"
                   " WHERE tagname GLOB 'sym-*' AND tagxref.rid=%d"
                   "   AND tagtype>1 AND tag.tagid=tagxref.tagid",
                   rid);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zTag = db_column_text(&q, 0);
      if( nTag==0 ){
        blob_appendf(&suffix, ", tags: %h", zTag);
      }else{
        blob_appendf(&suffix, ", %h", zTag);
      }
      nTag++;
    }
    db_finalize(&q);
    blob_appendf(&suffix, ")");
    @ %s(blob_str(&suffix))
    @ </td></tr></table>
    if( zChngTime ){
      @ <p>The timestamp on the tag used to make the changes above
      @ will be overridden as: %s(date_in_standard_format(zChngTime))</p>
    }
    @ </blockquote>
    @ <hr />
    blob_reset(&suffix);
  }
  @ <p>Make changes to attributes of check-in
  @ [%z(href("%R/ci/%!S",zUuid))%s(zUuid)</a>]:</p>
  form_begin(0, "%R/ci_edit");
  login_insert_csrf_secret();
  @ <div><input type="hidden" name="r" value="%s(zUuid)" />
  @ <table border="0" cellspacing="10">

  @ <tr><th align="right" valign="top">User:</th>
  @ <td valign="top">
  @   <input type="text" name="u" size="20" value="%h(zNewUser)" />
  @ </td></tr>

  @ <tr><th align="right" valign="top">Comment:</th>
  @ <td valign="top">
  @ <textarea name="c" rows="10" cols="80">%h(zNewComment)</textarea>
  @ </td></tr>

  @ <tr><th align="right" valign="top">Check-in Time:</th>
  @ <td valign="top">
  @   <input type="text" name="dt" size="20" value="%h(zNewDate)" />
  @ </td></tr>

  if( zChngTime ){
    @ <tr><th align="right" valign="top">Timestamp of this change:</th>
    @ <td valign="top">
    @   <input type="text" name="chngtime" size="20" value="%h(zChngTime)" />
    @ </td></tr>
  }

  @ <tr><th align="right" valign="top">Background Color:</th>
  @ <td valign="top">
  render_color_chooser(fNewPropagateColor, zNewColor, "pclr", "clr", "clrcust");
  @ </td></tr>

  @ <tr><th align="right" valign="top">Tags:</th>
  @ <td valign="top">
  @ <label><input type="checkbox" id="newtag" name="newtag"%s(zNewTagFlag) />
  @ Add the following new tag name to this check-in:</label>
  @ <input type="text" style="width:15;" name="tagname" value="%h(zNewTag)"
  @ onkeyup="gebi('newtag').checked=!!this.value" />
  zBranchName = db_text(0, "SELECT value FROM tagxref, tag"
     " WHERE tagxref.rid=%d AND tagtype>0 AND tagxref.tagid=tag.tagid"
     " AND tagxref.tagid=%d", rid, TAG_BRANCH);
  db_prepare(&q,
     "SELECT tag.tagid, tagname, tagxref.value FROM tagxref, tag"
     " WHERE tagxref.rid=%d AND tagtype>0 AND tagxref.tagid=tag.tagid"
     " ORDER BY CASE WHEN tagname GLOB 'sym-*' THEN substr(tagname,5)"
     "               ELSE tagname END /*sort*/",
     rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int tagid = db_column_int(&q, 0);
    const char *zTagName = db_column_text(&q, 1);
    int isSpecialTag = fossil_strncmp(zTagName, "sym-", 4)!=0;
    char zLabel[30];

    if( tagid == TAG_CLOSED ){
      fHasClosed = 1;
    }else if( (tagid == TAG_COMMENT) || (tagid == TAG_BRANCH) ){
      continue;
    }else if( tagid==TAG_HIDDEN ){
      fHasHidden = 1;
    }else if( !isSpecialTag && zTagName &&
        fossil_strcmp(&zTagName[4], zBranchName)==0){
      continue;
    }
    sqlite3_snprintf(sizeof(zLabel), zLabel, "c%d", tagid);
    @ <br /><label>
    if( P(zLabel) ){
      @ <input type="checkbox" name="c%d(tagid)" checked="checked" />
    }else{
      @ <input type="checkbox" name="c%d(tagid)" />
    }
    if( isSpecialTag ){
      @ Cancel special tag <b>%h(zTagName)</b></label>
    }else{
      @ Cancel tag <b>%h(&zTagName[4])</b></label>
    }
  }
  db_finalize(&q);
  @ </td></tr>

  if( !zBranchName ){
    zBranchName = db_get("main-branch", "trunk");
  }
  if( !zNewBranch || !zNewBranch[0]){
    zNewBranch = zBranchName;
  }
  @ <tr><th align="right" valign="top">Branching:</th>
  @ <td valign="top">
  @ <label><input id="newbr" type="checkbox" name="newbr"%s(zNewBrFlag)
  @ onchange="chgcbn(this.checked,'%h(zBranchName)')" />
  @ Make this check-in the start of a new branch named:</label>
  @ <input id="brname" type="text" style="width:15;" name="brname"
  @ value="%h(zNewBranch)"
  @ onkeyup="chgbn(this.value.trim(),'%h(zBranchName)')" /></td></tr>
  if( !fHasHidden ){
    @ <tr><th align="right" valign="top">Branch Hiding:</th>
    @ <td valign="top">
    @ <label><input type="checkbox" id="hidebr" name="hide"%s(zHideFlag) />
    @ Hide branch
    @ <span style="font-weight:bold" id="hbranch">%h(zBranchName)</span>
    @ from the timeline starting from this check-in</label>
    @ </td></tr>
  }
  if( !fHasClosed ){
    if( is_a_leaf(rid) ){
      @ <tr><th align="right" valign="top">Leaf Closure:</th>
      @ <td valign="top">
      @ <label><input type="checkbox" name="close"%s(zCloseFlag) />
      @ Mark this leaf as "closed" so that it no longer appears on the
      @ "leaves" page and is no longer labeled as a "<b>Leaf</b>"</label>
      @ </td></tr>
    }else if( zBranchName ){
      @ <tr><th align="right" valign="top">Branch Closure:</th>
      @ <td valign="top">
      @ <label><input type="checkbox" name="close"%s(zCloseFlag) />
      @ Mark branch
      @ <span style="font-weight:bold" id="cbranch">%h(zBranchName)</span>
      @ as "closed".</label>
      @ </td></tr>
    }
  }
  if( zBranchName ) fossil_free(zBranchName);


  @ <tr><td colspan="2">
  @ <input type="submit" name="preview" value="Preview" />
  @ <input type="submit" name="apply" value="Apply Changes" />
  @ <input type="submit" name="cancel" value="Cancel" />
  @ </td></tr>
  @ </table>
  @ </div></form>
  style_footer();
}

/*
** Prepare an ammended commit comment.  Let the user modify it using the
** editor specified in the global_config table or either
** the VISUAL or EDITOR environment variable.
**
** Store the final commit comment in pComment.  pComment is assumed
** to be uninitialized - any prior content is overwritten.
**
** Use zInit to initialize the check-in comment so that the user does
** not have to retype.
*/
static void prepare_amend_comment(
  Blob *pComment,
  const char *zInit,
  const char *zUuid
){
  Blob prompt;
#if defined(_WIN32) || defined(__CYGWIN__)
  int bomSize;
  const unsigned char *bom = get_utf8_bom(&bomSize);
  blob_init(&prompt, (const char *) bom, bomSize);
  if( zInit && zInit[0]){
    blob_append(&prompt, zInit, -1);
  }
#else
  blob_init(&prompt, zInit, -1);
#endif
  blob_append(&prompt, "\n# Enter a new comment for check-in ", -1);
  if( zUuid && zUuid[0] ){
    blob_append(&prompt, zUuid, -1);
  }
  blob_append(&prompt, ".\n# Lines beginning with a # are ignored.\n", -1);
  prompt_for_user_comment(pComment, &prompt);
  blob_reset(&prompt);
}

#define AMEND_USAGE_STMT "UUID OPTION ?OPTION ...?"
/*
** COMMAND: amend
**
** Usage: %fossil amend UUID OPTION ?OPTION ...?
**
** Amend the tags on check-in UUID to change how it displays in the timeline.
**
** Options:
**
**    --author USER           Make USER the author for check-in
**    -m|--comment COMMENT    Make COMMENT the check-in comment
**    -M|--message-file FILE  Read the amended comment from FILE
**    -e|--edit-comment       Launch editor to revise comment
**    --date DATETIME         Make DATETIME the check-in time
**    --bgcolor COLOR         Apply COLOR to this check-in
**    --branchcolor COLOR     Apply and propagate COLOR to the branch
**    --tag TAG               Add new TAG to this check-in
**    --cancel TAG            Cancel TAG from this check-in
**    --branch NAME           Make this check-in the start of branch NAME
**    --hide                  Hide branch starting from this check-in
**    --close                 Mark this "leaf" as closed
**
** DATETIME may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
** year-month-day form, it may be truncated, the "T" may be replaced by
** a space, and it may also name a timezone offset from UTC as "-HH:MM"
** (westward) or "+HH:MM" (eastward). Either no timezone suffix or "Z"
** means UTC.
*/
void ci_amend_cmd(void){
  int rid;
  const char *zComment;         /* Current comment on the check-in */
  const char *zNewComment;      /* Revised check-in comment */
  const char *zComFile;         /* Filename from which to read comment */
  const char *zUser;            /* Current user for the check-in */
  const char *zNewUser;         /* Revised user */
  const char *zDate;            /* Current date of the check-in */
  const char *zNewDate;         /* Revised check-in date */
  const char *zColor;
  const char *zNewColor;
  const char *zNewBrColor;
  const char *zNewBranch;
  const char **pzNewTags = 0;
  const char **pzCancelTags = 0;
  int fClose;                   /* True if leaf should be closed */
  int fHide;                    /* True if branch should be hidden */
  int fPropagateColor;          /* True if color propagates before amend */
  int fNewPropagateColor = 0;   /* True if color propagates after amend */
  int fHasHidden = 0;           /* True if hidden tag already set */
  int fHasClosed = 0;           /* True if closed tag already set */
  int fEditComment;             /* True if editor to be used for comment */
  const char *zChngTime;        /* The change time on the control artifact */
  const char *zUuid;
  Blob ctrl;
  Blob comment;
  char *zNow;
  int nTags, nCancels;
  int i;
  Stmt q;

  if( g.argc==3 ) usage(AMEND_USAGE_STMT);
  fEditComment = find_option("edit-comment","e",0)!=0;
  zNewComment = find_option("comment","m",1);
  zComFile = find_option("message-file","M",1);
  zNewBranch = find_option("branch",0,1);
  zNewColor = find_option("bgcolor",0,1);
  zNewBrColor = find_option("branchcolor",0,1);
  if( zNewBrColor ){
    zNewColor = zNewBrColor;
    fNewPropagateColor = 1;
  }
  zNewDate = find_option("date",0,1);
  zNewUser = find_option("author",0,1);
  pzNewTags = find_repeatable_option("tag",0,&nTags);
  pzCancelTags = find_repeatable_option("cancel",0,&nCancels);
  fClose = find_option("close",0,0)!=0;
  fHide = find_option("hide",0,0)!=0;
  zChngTime = find_option("chngtime",0,1);
  db_find_and_open_repository(0,0);
  user_select();
  verify_all_options();
  if( g.argc<3 || g.argc>=4 ) usage(AMEND_USAGE_STMT);
  rid = name_to_typed_rid(g.argv[2], "ci");
  if( rid==0 && !is_a_version(rid) ) fossil_fatal("no such check-in");
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( zUuid==0 ) fossil_fatal("Unable to find UUID");
  zComment = db_text(0, "SELECT coalesce(ecomment,comment)"
                        "  FROM event WHERE objid=%d", rid);
  zUser = db_text(0, "SELECT coalesce(euser,user)"
                     "  FROM event WHERE objid=%d", rid);
  zDate = db_text(0, "SELECT datetime(mtime)"
                     "  FROM event WHERE objid=%d", rid);
  zColor = db_text("", "SELECT bgcolor"
                        "  FROM event WHERE objid=%d", rid);
  fPropagateColor = db_int(0, "SELECT tagtype FROM tagxref"
                              " WHERE rid=%d AND tagid=%d",
                              rid, TAG_BGCOLOR)==2;
  fNewPropagateColor = zNewColor && zNewColor[0]
                        ? fNewPropagateColor : fPropagateColor;
  db_prepare(&q,
     "SELECT tag.tagid FROM tagxref, tag"
     " WHERE tagxref.rid=%d AND tagtype>0 AND tagxref.tagid=tag.tagid",
     rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int tagid = db_column_int(&q, 0);

    if( tagid == TAG_CLOSED ){
      fHasClosed = 1;
    }else if( tagid==TAG_HIDDEN ){
      fHasHidden = 1;
    }else{
      continue;
    }
  }
  db_finalize(&q);
  blob_zero(&ctrl);
  zNow = date_in_standard_format(zChngTime && zChngTime[0] ? zChngTime : "now");
  blob_appendf(&ctrl, "D %s\n", zNow);
  init_newtags();
  if( zNewColor && zNewColor[0]
      && (fPropagateColor!=fNewPropagateColor
            || fossil_strcmp(zColor,zNewColor)!=0)
  ){
    add_color(
      mprintf("%s%s", (zNewColor[0]!='#' &&
        validate16(zNewColor,strlen(zNewColor)) &&
        (strlen(zNewColor)==6 || strlen(zNewColor)==3)) ? "#" : "",
        zNewColor
      ),
      fNewPropagateColor
    );
  }
  if( (zNewColor!=0 && zNewColor[0]==0) && (zColor && zColor[0] ) ){
    cancel_color();
  }
  if( fEditComment ){
    prepare_amend_comment(&comment, zComment, zUuid);
    zNewComment = blob_str(&comment);
  }else if( zComFile ){
    blob_zero(&comment);
    blob_read_from_file(&comment, zComFile);
    blob_to_utf8_no_bom(&comment, 1);
    zNewComment = blob_str(&comment);
  }
  if( zNewComment && zNewComment[0]
      && comment_compare(zComment,zNewComment)==0 ) add_comment(zNewComment);
  if( zNewDate && zNewDate[0] && fossil_strcmp(zDate,zNewDate)!=0 ){
    if( is_datetime(zNewDate) ){
      add_date(zNewDate);
    }else{
      fossil_fatal("Unsupported date format, use YYYY-MM-DD HH:MM:SS");
    }
  }
  if( zNewUser && zNewUser[0] && fossil_strcmp(zUser,zNewUser)!=0 ){
    add_user(zNewUser);
  }
  if( pzNewTags!=0 ){
    for(i=0; i<nTags; i++){
      if( pzNewTags[i] && pzNewTags[i][0] ) add_tag(pzNewTags[i]);
    }
    fossil_free((void *)pzNewTags);
  }
  if( pzCancelTags!=0 ){
    for(i=0; i<nCancels; i++){
      if( pzCancelTags[i] && pzCancelTags[i][0] )
        cancel_tag(rid,pzCancelTags[i]);
    }
    fossil_free((void *)pzCancelTags);
  }
  if( fHide && !fHasHidden ) hide_branch();
  if( fClose && !fHasClosed ) close_leaf(rid);
  if( zNewBranch && zNewBranch[0] ) change_branch(rid,zNewBranch);
  apply_newtags(&ctrl, rid, zUuid);
  show_common_info(rid, "uuid:", 1, 0);
}
