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
**     *  The artifact hash
**     *  The record ID
**     *  mtime and ctime
**     *  who signed it
**
*/
void show_common_info(
  int rid,                   /* The rid for the check-in to display info for */
  const char *zRecDesc,      /* Brief record description; e.g. "check-out:" */
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
    fossil_print("%-13s %.40s %s\n", zRecDesc, zUuid, zDate ? zDate : "");
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
    comment_print(zComment, 0, 14, -1, get_comment_format());
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
** all known check-out locations for that repository and all URLs used
** to access the repository.  The --verbose is (currently) a no-op if
** the argument is the name of an object within the repository.
**
** Use the "finfo" command to get information about a specific
** file in a check-out.
**
** Options:
**    -R|--repository REPO       Extract info from repository REPO
**    -v|--verbose               Show extra information about repositories
**
** See also: [[annotate]], [[artifact]], [[finfo]], [[timeline]]
*/
void info_cmd(void){
  i64 fsize;
  int verboseFlag = find_option("verbose","v",0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("detail","l",0)!=0; /* deprecated */
  }

  if( g.argc==3
   && file_isfile(g.argv[2], ExtFILE)
   && (fsize = file_size(g.argv[2], ExtFILE))>0
   && (fsize&0x1ff)==0
  ){
    db_open_config(0, 0);
    db_open_repository(g.argv[2]);
    db_record_repository_filename(g.argv[2]);
    fossil_print("project-name: %s\n", db_get("project-name", "<unnamed>"));
    fossil_print("project-code: %s\n", db_get("project-code", "<none>"));
    showParentProject();
    extraRepoInfo();
    return;
  }
  db_find_and_open_repository(OPEN_OK_NOT_FOUND,0);
  verify_all_options();
  if( g.argc==2 ){
    int vid;
    if( g.repositoryOpen ){
      db_record_repository_filename(0);
      fossil_print("project-name: %s\n", db_get("project-name", "<unnamed>"));
    }else{
      db_open_config(0,1);
    }
    if( g.localOpen ){
      fossil_print("repository:   %s\n", db_repository_filename());
      fossil_print("local-root:   %s\n", g.zLocalRoot);
    }
    if( verboseFlag && g.repositoryOpen ){
      extraRepoInfo();
    }
    if( g.zConfigDbName ){
      fossil_print("config-db:    %s\n", g.zConfigDbName);
    }
    if( g.repositoryOpen ){
      fossil_print("project-code: %s\n", db_get("project-code", ""));
      showParentProject();
      vid = g.localOpen ? db_lget_int("checkout", 0) : 0;
      if( vid ){
        show_common_info(vid, "checkout:", 1, 1);
      }
      fossil_print("check-ins:    %d\n",
             db_int(-1, "SELECT count(*) FROM event WHERE type='ci' /*scan*/"));
    }
    if( verboseFlag || !g.repositoryOpen ){
      Blob vx;
      char *z;
      fossil_version_blob(&vx, 0);
      z = strstr(blob_str(&vx), "version");
      if( z ){
        z += 8;
      }else{
        z = blob_str(&vx);
      }
      fossil_print("fossil:       %z\n", file_fullexename(g.nameOfExe));
      fossil_print("version:      %s", z);
      blob_reset(&vx);
    }
  }else if( g.repositoryOpen ){
    int rid;
    rid = name_to_rid(g.argv[2]);
    if( rid==0 ){
      fossil_fatal("no such object: %s", g.argv[2]);
    }
    show_common_info(rid, "hash:", 1, 1);
  }else{
    fossil_fatal("Could not find or open a Fossil repository");
  }
}

/*
** Show the context graph (immediate parents and children) for
** check-in rid and rid2
*/
void render_checkin_context(int rid, int rid2, int parentsOnly, int mFlags){
  Blob sql;
  Stmt q;
  int rx[2];
  int i, n;
  rx[0] = rid;
  rx[1] = rid2;
  n = rid2 ? 2 : 1;
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_www(), -1);

  db_multi_exec(
    "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
    "DELETE FROM ok;"
  );
  for(i=0; i<n; i++){
    db_multi_exec(
      "INSERT OR IGNORE INTO ok VALUES(%d);"
      "INSERT OR IGNORE INTO ok SELECT pid FROM plink WHERE cid=%d;",
      rx[i], rx[i]
    );
  }
  if( !parentsOnly ){
    for(i=0; i<n; i++){
      db_multi_exec(
        "INSERT OR IGNORE INTO ok SELECT cid FROM plink WHERE pid=%d;", rx[i]
      );
      if( db_table_exists("repository","cherrypick") ){
        db_multi_exec(
          "INSERT OR IGNORE INTO ok "
          "  SELECT parentid FROM cherrypick WHERE childid=%d;"
          "INSERT OR IGNORE INTO ok "
          "  SELECT childid FROM cherrypick WHERE parentid=%d;",
          rx[i], rx[i]
        );
      }
    }
  }
  blob_append_sql(&sql, " AND event.objid IN ok ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_sql_text(&sql));
  www_print_timeline(&q,
         mFlags
         |TIMELINE_GRAPH
         |TIMELINE_FILLGAPS
         |TIMELINE_NOSCROLL
         |TIMELINE_XMERGE
         |TIMELINE_CHPICK,
       0, 0, 0, rid, rid2, 0);
  db_finalize(&q);
  blob_reset(&sql);
}


/*
** Append the difference between artifacts to the output
*/
static void append_diff(
  const char *zFrom,    /* Diff from this artifact */
  const char *zTo,      /*  ... to this artifact */
  DiffConfig *pCfg      /* The diff configuration */
){
  int fromid;
  int toid;
  Blob from, to;
  if( zFrom ){
    fromid = uuid_to_rid(zFrom, 0);
    content_get(fromid, &from);
    pCfg->zLeftHash = zFrom;
  }else{
    blob_zero(&from);
    pCfg->zLeftHash = 0;
  }
  if( zTo ){
    toid = uuid_to_rid(zTo, 0);
    content_get(toid, &to);
  }else{
    blob_zero(&to);
  }
  if( pCfg->diffFlags & DIFF_SIDEBYSIDE ){
    pCfg->diffFlags |= DIFF_HTML | DIFF_NOTTOOBIG;
  }else{
    pCfg->diffFlags |= DIFF_LINENO | DIFF_HTML | DIFF_NOTTOOBIG;
  }
  text_diff(&from, &to, cgi_output_blob(), pCfg);
  pCfg->zLeftHash = 0;
  blob_reset(&from);
  blob_reset(&to);
}

/*
** Write a line of web-page output that shows changes that have occurred
** to a file between two check-ins.
*/
static void append_file_change_line(
  const char *zCkin,    /* The check-in on which the change occurs */
  const char *zName,    /* Name of the file that has changed */
  const char *zOld,     /* blob.uuid before change.  NULL for added files */
  const char *zNew,     /* blob.uuid after change.  NULL for deletes */
  const char *zOldName, /* Prior name.  NULL if no name change. */
  DiffConfig *pCfg,     /* Flags for text_diff() or NULL to omit all */
  int mperm             /* executable or symlink permission for zNew */
){
  @ <div class='file-change-line'><span>
    /* Maintenance reminder: the extra level of SPAN is for
    ** arranging new elements via JS. */
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
    @ </span></div>
    if( pCfg ){
      append_diff(zOld, zNew, pCfg);
    }
  }else{
    if( zOld && zNew ){
      if( fossil_strcmp(zOld, zNew)!=0 ){
        if( zOldName!=0 && fossil_strcmp(zName,zOldName)!=0 ){
          @ Renamed and modified
          @ %z(href("%R/finfo?name=%T&m=%!S&ci=%!S",zOldName,zOld,zCkin))\
          @ %h(zOldName)</a>
          @ %z(href("%R/artifact/%!S",zOld))[%S(zOld)]</a>
          @ to %z(href("%R/finfo?name=%T&m=%!S&ci=%!S",zName,zNew,zCkin))\
          @ %h(zName)</a>
          @ %z(href("%R/artifact/%!S",zNew))[%S(zNew)]</a>.
        }else{
          @ Modified %z(href("%R/finfo?name=%T&m=%!S&ci=%!S",zName,zNew,zCkin))\
          @ %h(zName)</a>
          @ from %z(href("%R/artifact/%!S",zOld))[%S(zOld)]</a>
          @ to %z(href("%R/artifact/%!S",zNew))[%S(zNew)]</a>.
        }
      }else if( zOldName!=0 && fossil_strcmp(zName,zOldName)!=0 ){
        @ Name change
        @ from %z(href("%R/finfo?name=%T&m=%!S&ci=%!S",zOldName,zOld,zCkin))\
        @ %h(zOldName)</a>
        @ to %z(href("%R/finfo?name=%T&m=%!S&ci=%!S",zName,zNew,zCkin))\
        @ %h(zName)</a>.
      }else{
        @ %z(href("%R/finfo?name=%T&m=%!S&ci=%!S",zName,zNew,zCkin))\
        @ %h(zName)</a> became
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
      @ Deleted %z(href("%R/finfo?name=%T&m=%!S&ci=%!S",zName,zOld,zCkin))\
      @ %h(zName)</a> version %z(href("%R/artifact/%!S",zOld))[%S(zOld)]</a>.
    }else{
      @ Added %z(href("%R/finfo?name=%T&m=%!S&ci=%!S",zName,zNew,zCkin))\
      @ %h(zName)</a> version %z(href("%R/artifact/%!S",zNew))[%S(zNew)]</a>.
    }
    if( zOld && zNew && fossil_strcmp(zOld,zNew)!=0 ){
      if( pCfg ){
        @ </span></div>
        append_diff(zOld, zNew, pCfg);
      }else{
        @ %z(href("%R/fdiff?v1=%!S&v2=%!S",zOld,zNew))[diff]</a>
        @ </span></div>
      }
    }else{
      @ </span></div>
    }
  }
}

/*
** Generate javascript to enhance HTML diffs.
*/
void append_diff_javascript(int diffType){
  if( diffType==0 ) return;
  builtin_fossil_js_bundle_or("diff", NULL);
}

/*
** Construct an appropriate diffFlag for text_diff() based on query
** parameters and the to boolean arguments.
*/
DiffConfig *construct_diff_flags(int diffType, DiffConfig *pCfg){
  u64 diffFlags = 0;  /* Zero means do not show any diff */
  if( diffType>0 ){
    int x;
    if( diffType==2 ) diffFlags = DIFF_SIDEBYSIDE;
    if( P_NoBot("w") )  diffFlags |= DIFF_IGNORE_ALLWS;
    if( PD_NoBot("noopt",0)!=0 ) diffFlags |= DIFF_NOOPT;
    diffFlags |= DIFF_STRIP_EOLCR;
    diff_config_init(pCfg, diffFlags);

    /* "dc" query parameter determines lines of context */
    x = atoi(PD("dc","7"));
    if( x>0 ) pCfg->nContext = x;

    /* The "noopt" parameter disables diff optimization */
    return pCfg;
  }else{
    diff_config_init(pCfg, 0);
    return 0;
  }
}

/*
** WEBPAGE: ci_tags
** URL:    /ci_tags?name=ARTIFACTID
**
** Show all tags and properties for a given check-in.
**
** This information used to be part of the main /ci page, but it is of
** marginal usefulness.  Better to factor it out into a sub-screen.
*/
void ci_tags_page(void){
  const char *zHash;
  int rid;
  Stmt q;
  int cnt = 0;
  Blob sql;
  char const *zType;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  rid = name_to_rid_www("name");
  if( rid==0 ){
    style_header("Check-in Information Error");
    @ No such object: %h(PD("name",""))
    style_finish_page();
    return;
  }
  cgi_check_for_malice();
  zHash = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  style_header("Tags and Properties");
  zType = whatis_rid_type_label(rid);
  if(!zType) zType = "Artifact";
  @ <h1>Tags and Properties for %s(zType)  \
  @ %z(href("%R/ci/%!S",zHash))%S(zHash)</a></h1>
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
        hyperlink_to_version(zOrigUuid);
      }else{
        @ propagates to descendants
      }
    }
    if( zSrcUuid && zSrcUuid[0] ){
      if( tagtype==0 ){
        @ by
      }else{
        @ added by
      }
      hyperlink_to_version(zSrcUuid);
      @ on
      hyperlink_to_date(zDate,0);
    }
    @ </li>
  }
  db_finalize(&q);
  if( cnt ){
    @ </ul>
  }
  @ <div class="section">Context</div>
  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
     "DELETE FROM ok;"
     "INSERT INTO ok VALUES(%d);"
     "INSERT OR IGNORE INTO ok "
     " SELECT tagxref.srcid"
     "   FROM tagxref JOIN tag ON tagxref.tagid=tag.tagid"
     "  WHERE tagxref.rid=%d;"
     "INSERT OR IGNORE INTO ok "
     " SELECT tagxref.origid"
     "   FROM tagxref JOIN tag ON tagxref.tagid=tag.tagid"
     "  WHERE tagxref.rid=%d;",
     rid, rid, rid
  );
#if 0
  db_multi_exec(
    "SELECT tag.tagid, tagname, "
    "       (SELECT uuid FROM blob WHERE rid=tagxref.srcid AND rid!=%d),"
    "       value, datetime(tagxref.mtime,toLocal()), tagtype,"
    "       (SELECT uuid FROM blob WHERE rid=tagxref.origid AND rid!=%d)"
    "  FROM tagxref JOIN tag ON tagxref.tagid=tag.tagid"
    " WHERE tagxref.rid=%d"
    " ORDER BY tagname /*sort*/", rid, rid, rid
  );
#endif
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_www(), -1);
  blob_append_sql(&sql, " AND event.objid IN ok ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_sql_text(&sql));
  www_print_timeline(&q, TIMELINE_DISJOINT|TIMELINE_GRAPH|TIMELINE_NOSCROLL,
                     0, 0, 0, rid, 0, 0);
  db_finalize(&q);
  style_finish_page();
}

/*
** Render a web-page diff of the changes in the working check-out
*/
static void ckout_normal_diff(int vid){
  int diffType;               /* 0: no diff,  1: unified,  2: side-by-side */
  DiffConfig DCfg,*pCfg;      /* Diff details */
  const char *zW;             /* The "w" query parameter */
  int nChng;                  /* Number of changes */
  Stmt q;

  diffType = preferred_diff_type();
  pCfg = construct_diff_flags(diffType, &DCfg);
  nChng = db_int(0, "SELECT count(*) FROM vfile"
                    " WHERE vid=%d AND (deleted OR chnged OR rid==0)", vid);
  if( nChng==0 ){
    @ <p>No uncommitted changes</p>
    return;
  }
  db_prepare(&q,
       /*   0         1        2        3       4    5       6 */
    "SELECT pathname, deleted, chnged , rid==0, rid, islink, uuid"
    "  FROM vfile LEFT JOIN blob USING(rid)"
    " WHERE vid=%d"
    "   AND (deleted OR chnged OR rid==0)"
    " ORDER BY pathname /*scan*/",
    vid
  );
  if( DCfg.diffFlags & DIFF_SIDEBYSIDE ){
    DCfg.diffFlags |= DIFF_HTML | DIFF_NOTTOOBIG;
  }else{
    DCfg.diffFlags |= DIFF_LINENO | DIFF_HTML | DIFF_NOTTOOBIG;
  }
  @ <div class="sectionmenu info-changes-menu">
  zW = (DCfg.diffFlags&DIFF_IGNORE_ALLWS)?"&w":"";
  if( diffType!=1 ){
    @ %z(chref("button","%R?diff=1%s",zW))Unified&nbsp;Diff</a>
  }
  if( diffType!=2 ){
    @ %z(chref("button","%R?diff=2%s",zW))Side-by-Side&nbsp;Diff</a>
  }
  if( diffType!=0 ){
    if( *zW ){
      @ %z(chref("button","%R?diff=%d",diffType))\
      @ Show&nbsp;Whitespace&nbsp;Changes</a>
    }else{
      @ %z(chref("button","%R?diff=%d&w",diffType))Ignore&nbsp;Whitespace</a>
    }
  }
  @ </div>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTreename = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3);
    int srcid = db_column_int(&q, 4);
    int isLink = db_column_int(&q, 5);
    const char *zUuid = db_column_text(&q, 6);
    int showDiff = 1;

    DCfg.diffFlags &= (~DIFF_FILE_MASK);
    @ <div class='file-change-line'><span>
    if( isDeleted ){
      @ DELETED %h(zTreename)
      DCfg.diffFlags |= DIFF_FILE_DELETED;
      showDiff = 0;
    }else if( file_access(zTreename, F_OK) ){
      @ MISSING %h(zTreename)
      showDiff = 0;
    }else if( isNew ){
      @ ADDED %h(zTreename)
      DCfg.diffFlags |= DIFF_FILE_ADDED;
      srcid = 0;
      showDiff = 0;
    }else if( isChnged==3 ){
      @ ADDED_BY_MERGE %h(zTreename)
      DCfg.diffFlags |= DIFF_FILE_ADDED;
      srcid = 0;
      showDiff = 0;
    }else if( isChnged==5 ){
      @ ADDED_BY_INTEGRATE %h(zTreename)
      DCfg.diffFlags |= DIFF_FILE_ADDED;
      srcid = 0;
      showDiff = 0;
    }else{
      @ CHANGED %h(zTreename)
    }
    @ </span></div>
    if( showDiff && pCfg ){
      Blob old, new;
      if( !isLink != !file_islink(zTreename) ){
        @ %s(DIFF_CANNOT_COMPUTE_SYMLINK)
        continue;
      }
      if( srcid>0 ){
        content_get(srcid, &old);
        pCfg->zLeftHash = zUuid;
      }else{
        blob_zero(&old);
        pCfg->zLeftHash = 0;
      }
      blob_read_from_file(&new, zTreename, ExtFILE);
      text_diff(&old, &new, cgi_output_blob(), pCfg);
      blob_reset(&old);
      blob_reset(&new);
    }
  }
  db_finalize(&q);
  append_diff_javascript(diffType);
}

/*
** Render a web-page diff of the changes in the working check-out to
** an external reference.
*/
static void ckout_external_base_diff(int vid, const char *zExBase){
  int diffType;               /* 0: no diff,  1: unified,  2: side-by-side */
  DiffConfig DCfg,*pCfg;      /* Diff details */
  const char *zW;             /* The "w" query parameter */
  Stmt q;

  diffType = preferred_diff_type();
  pCfg = construct_diff_flags(diffType, &DCfg);
  db_prepare(&q,
    "SELECT pathname FROM vfile WHERE vid=%d ORDER BY pathname", vid
  );
  if( DCfg.diffFlags & DIFF_SIDEBYSIDE ){
    DCfg.diffFlags |= DIFF_HTML | DIFF_NOTTOOBIG;
  }else{
    DCfg.diffFlags |= DIFF_LINENO | DIFF_HTML | DIFF_NOTTOOBIG;
  }
  @ <div class="sectionmenu info-changes-menu">
  zW = (DCfg.diffFlags&DIFF_IGNORE_ALLWS)?"&w":"";
  if( diffType!=1 ){
    @ %z(chref("button","%R?diff=1&exbase=%h%s",zExBase,zW))\
    @ Unified&nbsp;Diff</a>
  }
  if( diffType!=2 ){
    @ %z(chref("button","%R?diff=2&exbase=%h%s",zExBase,zW))\
    @ Side-by-Side&nbsp;Diff</a>
  }
  if( diffType!=0 ){
    if( *zW ){
      @ %z(chref("button","%R?diff=%d&exbase=%h",diffType,zExBase))\
      @ Show&nbsp;Whitespace&nbsp;Changes</a>
    }else{
      @ %z(chref("button","%R?diff=%d&exbase=%h&w",diffType,zExBase))\
      @ Ignore&nbsp;Whitespace</a>
    }
  }
  @ </div>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFile;  /* Name of file in the repository */
    char *zLhs;         /* Full name of left-hand side file */
    char *zRhs;         /* Full name of right-hand side file */
    Blob rhs;           /* Full text of RHS */
    Blob lhs;           /* Full text of LHS */

    zFile = db_column_text(&q,0);
    zLhs = mprintf("%s/%s", zExBase, zFile);
    zRhs = mprintf("%s%s", g.zLocalRoot, zFile);
    if( file_size(zLhs, ExtFILE)<0 ){
      @ <div class='file-change-line'><span>
      @ Missing from external baseline: %h(zFile)
      @ </span></div>
    }else{
      blob_read_from_file(&lhs, zLhs, ExtFILE);
      blob_read_from_file(&rhs, zRhs, ExtFILE);
      if( blob_size(&lhs)!=blob_size(&rhs)
       || memcmp(blob_buffer(&lhs), blob_buffer(&rhs), blob_size(&lhs))!=0
      ){
        @ <div class='file-change-line'><span>
        @ Changes to %h(zFile)
        @ </span></div>
        if( pCfg ){
          char *zFullFN;
          char *zHexFN;
          zFullFN = file_canonical_name_dup(zLhs);
          zHexFN = mprintf("x%H", zFullFN);
          fossil_free(zFullFN);
          pCfg->zLeftHash = zHexFN;
          text_diff(&lhs, &rhs, cgi_output_blob(), pCfg);
          pCfg->zLeftHash = 0;
          fossil_free(zHexFN);
        }
      }
      blob_reset(&lhs);
      blob_reset(&rhs);
    }
    fossil_free(zLhs);
    fossil_free(zRhs);
  }
  db_finalize(&q);
  append_diff_javascript(diffType);
}

/*
** WEBPAGE: ckout
**
** Show information about the current checkout.  This page only functions
** if the web server is run on a loopback interface (in other words, was
** started using "fossil ui" or similar) from within an open check-out.
**
** If the "exbase=PATH" query parameter is provided, then the diff shown
** uses the files in PATH as the baseline.  This is the same as using
** the "--from PATH" argument to the "fossil diff" command-line.  In fact,
** when using "fossil ui --from PATH", the --from argument becomes the value
** of the exbase query parameter for the start page.  Note that if PATH
** is a pure hexadecimal string, it is decoded first before being used as
** the pathname.  Real pathnames should contain at least one directory
** separator character.
**
** Other query parameters related to diffs are also accepted.
*/
void ckout_page(void){
  int vid;
  const char *zHome;          /* Home directory */
  int nHome;
  const char *zExBase;
  char *zHostname;
  char *zCwd;

  if( !cgi_is_loopback(g.zIpAddr) || !db_open_local(0) ){
    cgi_redirectf("%R/home");
    return;
  }
  file_chdir(g.zLocalRoot, 0);
  vid = db_lget_int("checkout", 0);
  db_unprotect(PROTECT_ALL);
  vfile_check_signature(vid, CKSIG_ENOTFILE);
  db_protect_pop();
  style_set_current_feature("vinfo");
  zHostname = fossil_hostname();
  zCwd = file_getcwd(0,0);
  zHome = fossil_getenv("HOME");
  if( zHome ){
    nHome = (int)strlen(zHome);
    if( strncmp(zCwd, zHome, nHome)==0 && zCwd[nHome]=='/' ){
      zCwd = mprintf("~%s", zCwd+nHome);
    }
  }else{
    nHome = 0;
  }
  if( zHostname ){
    style_header("Checkout Status: %h on %h", zCwd, zHostname);
  }else{
    style_header("Checkout Status: %h", zCwd);
  }
  render_checkin_context(vid, 0, 0, 0);
  @ <hr>
  zExBase = P("exbase");
  if( zExBase && zExBase[0] ){
    char *zPath = decode16_dup(zExBase);
    char *zCBase = file_canonical_name_dup(zPath?zPath:zExBase);
    if( nHome && strncmp(zCBase, zHome, nHome)==0 && zCBase[nHome]=='/' ){
      @ <p>Using external baseline: ~%h(zCBase+nHome)</p>
    }else{
      @ <p>Using external baseline: %h(zCBase)</p>
    }
    ckout_external_base_diff(vid, zCBase);
    fossil_free(zCBase);
    fossil_free(zPath);
  }else{
    ckout_normal_diff(vid);
  }
  style_finish_page();
}

/*
** WEBPAGE: vinfo
** WEBPAGE: ci
** URL:  /ci/ARTIFACTID
**  OR:  /ci?name=ARTIFACTID
**
** Display information about a particular check-in.  The exact
** same information is shown on the /info page if the name query
** parameter to /info describes a check-in.
**
** The ARTIFACTID can be a unique prefix for the HASH of the check-in,
** or a tag or branch name that identifies the check-in.
*/
void ci_page(void){
  Stmt q1, q2, q3;
  int rid;
  int isLeaf;
  int diffType;        /* 0: no diff,  1: unified,  2: side-by-side */
  const char *zName;   /* Name of the check-in to be displayed */
  const char *zUuid;   /* Hash of zName, found via blob.uuid */
  const char *zParent; /* Hash of the parent check-in (if any) */
  const char *zRe;     /* regex parameter */
  ReCompiled *pRe = 0; /* regex */
  const char *zW;               /* URL param for ignoring whitespace */
  const char *zPage = "vinfo";  /* Page that shows diffs */
  const char *zBrName;          /* Branch name */
  DiffConfig DCfg,*pCfg;        /* Type of diff */

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_set_current_feature("vinfo");
  zName = P("name");
  rid = name_to_rid_www("name");
  if( rid==0 ){
    style_header("Check-in Information Error");
    @ No such object: %h(zName)
    style_finish_page();
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
  isLeaf = !db_exists("SELECT 1 FROM plink WHERE pid=%d", rid);
  db_prepare(&q1,
     "SELECT uuid, datetime(mtime,toLocal(),'subsec'), user, comment,"
     "       datetime(omtime,toLocal(),'subsec'), mtime"
     "  FROM blob, event"
     " WHERE blob.rid=%d"
     "   AND event.objid=%d",
     rid, rid
  );
  zBrName = branch_of_rid(rid);

  diffType = preferred_diff_type();
  cgi_check_for_malice();
  if( db_step(&q1)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q1, 0);
    int nUuid = db_column_bytes(&q1, 0);
    char *zEUser, *zEComment;
    const char *zUser;
    const char *zOrigUser;
    const char *zComment;
    const char *zDate;
    const char *zOrigDate;
    int okWiki = 0;
    Blob wiki_read_links = BLOB_INITIALIZER;
    Blob wiki_add_links = BLOB_INITIALIZER;

    Th_StoreUnsafe("current_checkin", zName);
    style_header("Check-in [%S]", zUuid);
    login_anonymous_available();
    zEUser = db_text(0,
                   "SELECT value FROM tagxref"
                   " WHERE tagid=%d AND rid=%d AND tagtype>0",
                    TAG_USER, rid);
    zEComment = db_text(0,
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_COMMENT, rid);
    zOrigUser = db_column_text(&q1, 2);
    zUser = zEUser ? zEUser : zOrigUser;
    zComment = db_column_text(&q1, 3);
    zDate = db_column_text(&q1,1);
    zOrigDate = db_column_text(&q1, 4);
    if( zOrigDate==0 ) zOrigDate = zDate;
    @ <div class="section accordion">Overview</div>
    @ <div class="accordion_panel">
    @ <table class="label-value">
    @ <tr><th>Comment:</th><td class="infoComment">\
    @ %!W(zEComment?zEComment:zComment)</td></tr>

    /* The Download: line */
    if( g.perm.Zip  ){
      char *zPJ = db_get("short-project-name", 0);
      char *zUrl;
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
      zUrl = mprintf("%R/tarball/%S/%t-%S.tar.gz", zUuid, zPJ, zUuid);
      @ <tr><th>Downloads:</th><td>
      @ %z(href("%s",zUrl))Tarball</a>
      @ | %z(href("%R/zip/%S/%t-%S.zip",zUuid, zPJ,zUuid))ZIP archive</a>
      if( g.zLogin!=0 ){
        @ | %z(href("%R/sqlar/%S/%t-%S.sqlar",zUuid,zPJ,zUuid))\
        @ SQL archive</a></td></tr>
      }
      fossil_free(zUrl);
      blob_reset(&projName);
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
      if( fossil_strcmp(zTagName,zBrName)==0 ){
        cgi_printf(" | ");
        style_copy_button(1, "name-br", 0, 0, "%z%h</a>",
          href("%R/timeline?r=%T&unhide",zTagName), zTagName);
        cgi_printf("\n");
        if( wiki_tagid2("branch",zTagName)!=0 ){
          blob_appendf(&wiki_read_links, " | %z%h</a>",
              href("%R/%s?name=branch/%h",
                   (g.perm.Write && g.perm.WrWiki)
                   ? "wikiedit" : "wiki",
                   zTagName), zTagName);
        }else if( g.perm.Write && g.perm.WrWiki ){
          blob_appendf(&wiki_add_links, " | %z%h</a>",
              href("%R/wikiedit?name=branch/%h",zTagName), zTagName);
        }
      }else{
        @  | %z(href("%R/timeline?t=%T&unhide",zTagName))%h(zTagName)</a>
        if( wiki_tagid2("tag",zTagName)!=0 ){
          blob_appendf(&wiki_read_links, " | %z%h</a>",
              href("%R/wiki?name=tag/%h",zTagName), zTagName);
        }else if( g.perm.Write && g.perm.WrWiki ){
          blob_appendf(&wiki_add_links, " | %z%h</a>",
              href("%R/wikiedit?name=tag/%h",zTagName), zTagName);
        }
      }
    }
    db_finalize(&q2);
    @ </td></tr>

    @ <tr><th>Files:</th>
    @   <td>
    @     %z(href("%R/tree?ci=%!S",zUuid))files</a>
    @   | %z(href("%R/fileage?name=%!S",zUuid))file ages</a>
    @   | %z(href("%R/tree?nofiles&type=tree&ci=%!S",zUuid))folders</a>
    @   </td>
    @ </tr>

    @ <tr><th>%s(hname_alg(nUuid)):</th><td>
    style_copy_button(1, "hash-ci", 0, 2, "%.32s<wbr>%s", zUuid, zUuid+32);
    if( g.perm.Setup ){
      @  (Record ID: %d(rid))
    }
    @ </td></tr>
    @ <tr><th>User&nbsp;&amp;&nbsp;Date:</th><td>
    hyperlink_to_user(zUser,zDate," on ");
    hyperlink_to_date(zDate, "</td></tr>");
    if( zEComment ){
      @ <tr><th>Original&nbsp;Comment:</th>
      @     <td class="infoComment">%!W(zComment)</td></tr>
    }
    if( fossil_strcmp(zDate, zOrigDate)!=0
     || fossil_strcmp(zOrigUser, zUser)!=0
    ){
      @ <tr><th>Original&nbsp;User&nbsp;&amp;&nbsp;Date:</th><td>
      hyperlink_to_user(zOrigUser,zOrigDate," on ");
      hyperlink_to_date(zOrigDate, "</td></tr>");
    }
    if( g.perm.Admin ){
      db_prepare(&q2,
         "SELECT rcvfrom.ipaddr, user.login, datetime(rcvfrom.mtime),"
               " blob.rcvid"
         "  FROM blob JOIN rcvfrom USING(rcvid) LEFT JOIN user USING(uid)"
         " WHERE blob.rid=%d",
         rid
      );
      if( db_step(&q2)==SQLITE_ROW ){
        const char *zIpAddr = db_column_text(&q2, 0);
        const char *zUser = db_column_text(&q2, 1);
        const char *zDate = db_column_text(&q2, 2);
        int rcvid = db_column_int(&q2,3);
        if( zUser==0 || zUser[0]==0 ) zUser = "unknown";
        @ <tr><th>Received&nbsp;From:</th>
        @ <td>%h(zUser) @ %h(zIpAddr) on %s(zDate) \
        @ (<a href="%R/rcvfrom?rcvid=%d(rcvid)">Rcvid %d(rcvid)</a>)</td></tr>
      }
      db_finalize(&q2);
    }

    /* Only show links to edit wiki pages if the users can read wiki
    ** and if the wiki pages already exist */
    if( g.perm.WrWiki
     && g.perm.RdWiki
     && g.perm.Write
     && ((okWiki = wiki_tagid2("checkin",zUuid))!=0 ||
                 blob_size(&wiki_read_links)>0)
     && db_get_boolean("wiki-about",1)
    ){
      const char *zLinks = blob_str(&wiki_read_links);
      @ <tr><th>Edit&nbsp;Wiki:</th><td>\
      if( okWiki ){
        @ %z(href("%R/wikiedit?name=checkin/%s",zUuid))this check-in</a>\
      }else if( zLinks[0] ){
        zLinks += 3;
      }
      @ %s(zLinks)</td></tr>
    }

    /* Only show links to create new wiki pages if the users can write wiki
    ** and if the wiki pages do not already exist */
    if( g.perm.WrWiki
     && g.perm.RdWiki
     && g.perm.Write
     && (blob_size(&wiki_add_links)>0 || !okWiki)
     && db_get_boolean("wiki-about",1)
    ){
      const char *zLinks = blob_str(&wiki_add_links);
      @ <tr><th>Add&nbsp;Wiki:</th><td>\
      if( !okWiki ){
        @ %z(href("%R/wikiedit?name=checkin/%s",zUuid))this check-in</a>\
      }else if( zLinks[0] ){
        zLinks += 3;
      }
      @ %s(zLinks)</td></tr>
    }

    if( g.perm.Hyperlink ){
      @ <tr><th>Other&nbsp;Links:</th>
      @   <td>
      if( fossil_strcmp(zBrName, db_get("main-branch",0))!=0 ){
        @ %z(href("%R/vdiff?branch=%!S", zUuid))branch diff</a> |
      }
      @ %z(href("%R/artifact/%!S",zUuid))manifest</a>
      @ | %z(href("%R/ci_tags/%!S",zUuid))tags</a>
      if( g.perm.Admin ){
        @   | %z(href("%R/mlink?ci=%!S",zUuid))mlink table</a>
      }
      if( g.anon.Write ){
        @   | %z(href("%R/ci_edit?r=%!S",zUuid))edit</a>
      }
      @   </td>
      @ </tr>
    }
    @ </table>
    blob_reset(&wiki_read_links);
    blob_reset(&wiki_add_links);
  }else{
    style_header("Check-in Information");
    login_anonymous_available();
  }
  db_finalize(&q1);
  @ </div>
  builtin_request_js("accordion.js");
  if( !PB("nowiki") ){
    wiki_render_associated("checkin", zUuid, 0);
  }
  render_backlink_graph(zUuid,
       "<div class=\"section accordion\">References</div>\n");
  @ <div class="section accordion">Context</div><div class="accordion_panel">
  render_checkin_context(rid, 0, 0, 0);
  @ </div><div class="section accordion">Changes</div>
  @ <div class="accordion_panel">
  @ <div class="sectionmenu info-changes-menu">
  /* ^^^ .info-changes-menu is used by diff scroll sync */
  pCfg = construct_diff_flags(diffType, &DCfg);
  DCfg.pRe = pRe;
  zW = (DCfg.diffFlags&DIFF_IGNORE_ALLWS)?"&w":"";
  if( diffType!=1 ){
    @ %z(chref("button","%R/%s/%T?diff=1%s",zPage,zName,zW))\
    @ Unified&nbsp;Diff</a>
  }
  if( diffType!=2 ){
    @ %z(chref("button","%R/%s/%T?diff=2%s",zPage,zName,zW))\
    @ Side-by-Side&nbsp;Diff</a>
  }
  if( diffType!=0 ){
    if( *zW ){
      @ %z(chref("button","%R/%s/%T?diff=%d",zPage,zName,diffType))
      @ Show&nbsp;Whitespace&nbsp;Changes</a>
    }else{
      @ %z(chref("button","%R/%s/%T?diff=%d&w",zPage,zName,diffType))
      @ Ignore&nbsp;Whitespace</a>
    }
  }
  if( zParent ){
    @ %z(chref("button","%R/vpatch?from=%!S&to=%!S",zParent,zUuid))
    @ Patch</a>
  }
  if( g.perm.Admin ){
    @ %z(chref("button","%R/mlink?ci=%!S",zUuid))MLink Table</a>
  }
  @ </div>
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
    append_file_change_line(zUuid, zName, zOld, zNew, zOldName,
                            pCfg,mperm);
  }
  db_finalize(&q3);
  @ </div>
  append_diff_javascript(diffType);
  style_finish_page();
}

/*
** WEBPAGE: winfo
** URL:  /winfo?name=HASH
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
  int tagid;
  int ridNext;

  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  style_set_current_feature("winfo");
  rid = name_to_rid_www("name");
  if( rid==0 || (pWiki = manifest_get(rid, CFTYPE_WIKI, 0))==0 ){
    style_header("Wiki Page Information Error");
    @ No such object: %h(P("name"))
    style_finish_page();
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
      moderation_approve('w', rid);
    }
  }
  cgi_check_for_malice();
  style_header("Update of \"%h\"", pWiki->zWikiTitle);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  zDate = db_text(0, "SELECT datetime(%.17g,toLocal())", pWiki->rDate);
  style_submenu_element("Raw", "%R/artifact/%s", zUuid);
  style_submenu_element("History", "%R/whistory?name=%t", pWiki->zWikiTitle);
  style_submenu_element("Page", "%R/wiki?name=%t", pWiki->zWikiTitle);
  login_anonymous_available();
  @ <div class="section">Overview</div>
  @ <p><table class="label-value">
  @ <tr><th>Artifact&nbsp;ID:</th>
  @ <td>%z(href("%R/artifact/%!S",zUuid))%s(zUuid)</a>
  if( g.perm.Setup ){
    @ (%d(rid))
  }
  modPending = moderation_pending_www(rid);
  @ </td></tr>
  @ <tr><th>Page&nbsp;Name:</th>\
  @ <td>%z(href("%R/whistory?name=%h",pWiki->zWikiTitle))\
  @ %h(pWiki->zWikiTitle)</a></td></tr>
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
      @ %z(href("%R/info/%!S",zParent))%s(zParent)</a>
      @ %z(href("%R/wdiff?id=%!S&pid=%!S",zUuid,zParent))(diff)</a>
    }
    @ </td></tr>
  }
  tagid = wiki_tagid(pWiki->zWikiTitle);
  if( tagid>0 && (ridNext = wiki_next(tagid, pWiki->rDate))>0 ){
    char *zId = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", ridNext);
    @ <tr><th>Next</th>
    @ <td>%z(href("%R/info/%!S",zId))%s(zId)</a></td>
  }
  @ </table>

  if( g.perm.ModWiki && modPending ){
    @ <div class="section">Moderation</div>
    @ <blockquote>
    @ <form method="POST" action="%R/winfo/%s(zUuid)">
    @ <label><input type="radio" name="modaction" value="delete">
    @ Delete this change</label><br>
    @ <label><input type="radio" name="modaction" value="approve">
    @ Approve this change</label><br>
    @ <input type="submit" value="Submit">
    @ </form>
    @ </blockquote>
  }


  @ <div class="section">Content</div>
  blob_init(&wiki, pWiki->zWiki, -1);
  safe_html_context(DOCSRC_WIKI);
  wiki_render_by_mimetype(&wiki, pWiki->zMimetype);
  blob_reset(&wiki);
  manifest_destroy(pWiki);
  document_emit_js();
  style_finish_page();
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
**   diff=INTEGER    0: none, 1: unified, 2: side-by-side
**   glob=STRING     only diff files matching this glob
**   dc=N            show N lines of context around each diff
**   w=BOOLEAN       ignore whitespace when computing diffs
**   nohdr           omit the description at the top of the page
**   nc              omit branch coloration from the header graph
**   inv             "Invert".  Exchange the roles of from= and to=
**
** Show all differences between two check-ins.
*/
void vdiff_page(void){
  int ridFrom, ridTo;
  int diffType = 0;        /* 0: none, 1: unified, 2: side-by-side */
  Manifest *pFrom, *pTo;
  ManifestFile *pFileFrom, *pFileTo;
  const char *zBranch;
  const char *zFrom;
  const char *zTo;
  const char *zRe;
  const char *zGlob;
  Glob * pGlob = 0;
  char *zMergeOrigin = 0;
  ReCompiled *pRe = 0;
  DiffConfig DCfg, *pCfg = 0;
  int graphFlags = 0;
  Blob qp;                /* non-glob= query parameters for generated links */
  Blob qpGlob;            /* glob= query parameter for generated links */
  int bInvert = PB("inv");

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  login_anonymous_available();
  fossil_nice_default();
  blob_init(&qp, 0, 0);
  blob_init(&qpGlob, 0, 0);
  diffType = preferred_diff_type();
  zRe = P("regex");
  if( zRe ) re_compile(&pRe, zRe, 0);
  zBranch = P("branch");
  if( zBranch && zBranch[0]==0 ) zBranch = 0;
  if( zBranch ){
    blob_appendf(&qp, "branch=%T", zBranch);
    zMergeOrigin = mprintf("merge-in:%s", zBranch);
    cgi_replace_parameter("from", zMergeOrigin);
    cgi_replace_parameter("to", zBranch);
  }else{
    if( bInvert ){
      blob_appendf(&qp, "to=%T&from=%T",PD("from",""),PD("to",""));
    }else{
      blob_appendf(&qp, "from=%T&to=%T",PD("from",""),PD("to",""));
    }
  }
  pTo = vdiff_parse_manifest("to", &ridTo);
  if( pTo==0 ) return;
  pFrom = vdiff_parse_manifest("from", &ridFrom);
  if( pFrom==0 ) return;
  zGlob = P("glob");
  /*
  ** Maintenace reminder: we explicitly do _not_ use P_NoBot()
  ** for "from" and "to" because those args can contain legitimate
  ** strings which may trigger the looks-like SQL checks, e.g.
  **   from=merge-in:OR-clause-improvement
  **   to=OR-clause-improvement
  */
  zFrom = P("from");
  zTo = P("to");
  if( bInvert ){
    Manifest *pTemp = pTo;
    const char *zTemp = zTo;
    pTo = pFrom;
    pFrom = pTemp;
    zTo = zFrom;
    zFrom = zTemp;
  }
  if( zGlob ){
    if( !*zGlob ){
      zGlob = NULL;
    }else{
      blob_appendf(&qpGlob, "&glob=%T", zGlob);
      pGlob = glob_create(zGlob);
    }
  }
  if( PB("nc") ){
    graphFlags |= TIMELINE_NOCOLOR;
    blob_appendf(&qp, "&nc");
  }
  pCfg = construct_diff_flags(diffType, &DCfg);
  if( DCfg.diffFlags & DIFF_IGNORE_ALLWS ){
    blob_appendf(&qp, "&w");
  }
  cgi_check_for_malice();
  style_set_current_feature("vdiff");
  if( zBranch==0 ){
    style_submenu_element("Path", "%R/timeline?me=%T&you=%T", zFrom, zTo);
  }
  if( diffType!=2 ){
    style_submenu_element("Side-by-Side Diff", "%R/vdiff?diff=2&%b%b", &qp,
                          &qpGlob);
  }
  if( diffType!=1 ) {
    style_submenu_element("Unified Diff", "%R/vdiff?diff=1&%b%b", &qp, &qpGlob);
  }
  if( zBranch==0 ){
    style_submenu_element("Invert","%R/vdiff?diff=%d&inv&%b%b", diffType,
                          &qp, &qpGlob);
  }
  if( zGlob ){
    style_submenu_element("Clear glob", "%R/vdiff?diff=%d&%b", diffType, &qp);
  }else{
    style_submenu_element("Patch", "%R/vpatch?from=%T&to=%T%s", zFrom, zTo,
           (DCfg.diffFlags & DIFF_IGNORE_ALLWS)?"&w":"");
  }
  if( diffType!=0 ){
    style_submenu_checkbox("w", "Ignore Whitespace", 0, 0);
  }
  if( zBranch ){
    style_header("Changes On Branch %h", zBranch);
  }else{
    style_header("Check-in Differences");
  }
  if( P("nohdr")==0 ){
    if( zBranch ){
      char *zRealBranch = branch_of_rid(ridTo);
      char *zToUuid = rid_to_uuid(ridTo);
      char *zFromUuid = rid_to_uuid(ridFrom);
      @ <h2>Changes In Branch \
      @ %z(href("%R/timeline?r=%T",zRealBranch))%h(zRealBranch)</a>
      if( ridTo != symbolic_name_to_rid(zRealBranch,"ci") ){
        @ Through %z(href("%R/info/%!S",zToUuid))[%S(zToUuid)]</a>
      }
      @ Excluding Merge-Ins</h2>
      @ <p>This is equivalent to a diff from
      @ <span class='timelineSelected'>\
      @ %z(href("%R/info/%!S",zFromUuid))%S(zFromUuid)</a></span>
      @ to <span class='timelineSelected timelineSecondary'>\
      @ %z(href("%R/info/%!S",zToUuid))%S(zToUuid)</a></span></p>
    }else{
      @ <h2>Difference From <span class='timelineSelected'>\
      @ %z(href("%R/info/%h",zFrom))%h(zFrom)</a></span>
      @ To <span class='timelineSelected timelineSecondary'>\
      @ %z(href("%R/info/%h",zTo))%h(zTo)</a></span></h2>
    }
    render_checkin_context(ridFrom, ridTo, 0, graphFlags);
    if( pRe ){
      @ <p><b>Only differences that match regular expression "%h(zRe)"
      @ are shown.</b></p>
    }
    if( zGlob ){
      @ <p><b>Only files matching the glob "%h(zGlob)" are shown.</b></p>
    }
    @<hr><p>
  }
  blob_reset(&qp);
  blob_reset(&qpGlob);

  manifest_file_rewind(pFrom);
  pFileFrom = manifest_file_next(pFrom, 0);
  manifest_file_rewind(pTo);
  pFileTo = manifest_file_next(pTo, 0);
  DCfg.pRe = pRe;
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
      if( !pGlob || glob_match(pGlob, pFileFrom->zName) ){
        append_file_change_line(zFrom, pFileFrom->zName,
                                pFileFrom->zUuid, 0, 0, pCfg, 0);
      }
      pFileFrom = manifest_file_next(pFrom, 0);
    }else if( cmp>0 ){
      if( !pGlob || glob_match(pGlob, pFileTo->zName) ){
        append_file_change_line(zTo, pFileTo->zName,
                                0, pFileTo->zUuid, 0, pCfg,
                                manifest_file_mperm(pFileTo));
      }
      pFileTo = manifest_file_next(pTo, 0);
    }else if( fossil_strcmp(pFileFrom->zUuid, pFileTo->zUuid)==0 ){
      pFileFrom = manifest_file_next(pFrom, 0);
      pFileTo = manifest_file_next(pTo, 0);
    }else{
      if(!pGlob || (glob_match(pGlob, pFileFrom->zName)
                    || glob_match(pGlob, pFileTo->zName)) ){
        append_file_change_line(zFrom, pFileFrom->zName,
                                pFileFrom->zUuid,
                                pFileTo->zUuid, 0, pCfg,
                                manifest_file_mperm(pFileTo));
      }
      pFileFrom = manifest_file_next(pFrom, 0);
      pFileTo = manifest_file_next(pTo, 0);
    }
  }
  glob_free(pGlob);
  manifest_destroy(pFrom);
  manifest_destroy(pTo);
  append_diff_javascript(diffType);
  style_finish_page();
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
#define OBJTYPE_FORUM      0x0200

/*
** Possible flags for the second parameter to
** object_description()
*/
#define OBJDESC_DETAIL      0x0001   /* Show more detail */
#define OBJDESC_BASE        0x0002   /* Set <base> using this object */
#endif

/*
** Write a description of an object to the www reply.
*/
int object_description(
  int rid,                 /* The artifact ID for the object to describe */
  u32 objdescFlags,        /* Flags to control display */
  const char *zFileName,   /* For file objects, use this name.  Can be NULL */
  Blob *pDownloadName      /* Fill with a good download name.  Can be NULL */
){
  Stmt q;
  int cnt = 0;
  int nWiki = 0;
  int objType = 0;
  char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  int showDetail = (objdescFlags & OBJDESC_DETAIL)!=0;
  char *prevName = 0;
  int bNeedBase = (objdescFlags & OBJDESC_BASE)!=0;

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
    if( zFileName && fossil_strcmp(zName,zFileName)!=0 ) continue;
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
        if( bNeedBase ){
          bNeedBase = 0;
          style_set_current_page("doc/%S/%s",zVers,zName);
        }
      }
      objType |= OBJTYPE_CONTENT;
      @ %z(href("%R/finfo?name=%T&ci=%!S&m=%!S",zName,zVers,zUuid))\
      @ %h(zName)</a>
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
      hyperlink_to_version(zVers);
    }else{
      @ &mdash; part of check-in
      hyperlink_to_version(zVers);
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
      @ %z(href("%R/annotate?filename=%T&checkin=%!S",zName,zVers))
      @ [annotate]</a>
      @ %z(href("%R/blame?filename=%T&checkin=%!S",zName,zVers))
      @ [blame]</a>
      @ %z(href("%R/timeline?uf=%!S",zUuid))[check-ins&nbsp;using]</a>
      if( fileedit_is_editable(zName) ){
        @ %z(href("%R/fileedit?filename=%T&checkin=%!S",zName,zVers))[edit]</a>
      }
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
      }else if( zType[0]=='f' ){
        objType |= OBJTYPE_FORUM;
        @ Forum post
      }else{
        @ Tag referencing
      }
      if( zType[0]!='e' || eventTagId == 0){
        hyperlink_to_version(zUuid);
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
    if( fossil_is_artifact_hash(zTarget) ){
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
    @ Cluster %z(href("%R/info/%S",zUuid))%S(zUuid)</a>.
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
** SETTING: preferred-diff-type         width=16 default=0
**
** The preferred-diff-type setting determines the preferred diff format
** for web pages if the format is not otherwise specified, for example
** by a query parameter or cookie.  Allowed values:
**
**    1    Unified diff
**    2    Side-by-side diff
**
** If this setting is omitted or has a value of 0 or less, then it
** is ignored.
*/
/*
** Return the preferred diff type.
**
**    0 = No diff at all.
**    1 = unified diff
**    2 = side-by-side diff
**
** To determine the preferred diff type, the following values are
** consulted in the order shown.  The first available source wins.
**
**    *  The "diff" query parameter
**    *  The "diff" field of the user display cookie
**    *  The "preferred-diff-type" setting
**    *  1 for mobile and 2 for desktop, based on the UserAgent
*/
int preferred_diff_type(void){
  int dflt;
  static char zDflt[2]
    /*static b/c cookie_link_parameter() does not copy it!*/;
  dflt = db_get_int("preferred-diff-type",-99);
  if( dflt<=0 ) dflt = user_agent_is_likely_mobile() ? 1 : 2;
  zDflt[0] = dflt + '0';
  zDflt[1] = 0;
  cookie_link_parameter("diff","diff", zDflt);
  return atoi(PD_NoBot("diff",zDflt));
}


/*
** WEBPAGE: fdiff
** URL: fdiff?v1=HASH&v2=HASH
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
  int diffType;          /* 0: none, 1: unified,  2: side-by-side */
  char *zV1;
  char *zV2;
  const char *zRe;
  ReCompiled *pRe = 0;
  u32 objdescFlags = 0;
  int verbose = PB("verbose");
  DiffConfig DCfg;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  diff_config_init(&DCfg, 0);
  diffType = preferred_diff_type();
  if( P("from") && P("to") ){
    v1 = artifact_from_ci_and_filename("from");
    v2 = artifact_from_ci_and_filename("to");
    if( v1==0 || v2==0 ) fossil_redirect_home();
  }else{
    Stmt q;
    v1 = name_to_rid_www("v1");
    v2 = name_to_rid_www("v2");
    if( v1==0 || v2==0 ) fossil_redirect_home();

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
  zRe = P("regex");
  cgi_check_for_malice();
  if( zRe ) re_compile(&pRe, zRe, 0);
  if( verbose ) objdescFlags |= OBJDESC_DETAIL;
  if( isPatch ){
    Blob c1, c2, *pOut;
    DiffConfig DCfg;
    pOut = cgi_output_blob();
    cgi_set_content_type("text/plain");
    DCfg.diffFlags = DIFF_VERBOSE;
    content_get(v1, &c1);
    content_get(v2, &c2);
    DCfg.pRe = pRe;
    text_diff(&c1, &c2, pOut, &DCfg);
    blob_reset(&c1);
    blob_reset(&c2);
    return;
  }

  zV1 = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", v1);
  zV2 = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", v2);
  construct_diff_flags(diffType, &DCfg);
  DCfg.diffFlags |= DIFF_HTML;

  style_set_current_feature("fdiff");
  style_header("Diff");
  style_submenu_checkbox("w", "Ignore Whitespace", 0, 0);
  if( diffType==2 ){
    style_submenu_element("Unified Diff", "%R/fdiff?v1=%T&v2=%T&diff=1",
                           P("v1"), P("v2"));
  }else{
    style_submenu_element("Side-by-side Diff", "%R/fdiff?v1=%T&v2=%T&diff=2",
                           P("v1"), P("v2"));
  }
  style_submenu_checkbox("verbose", "Verbose", 0, 0);
  style_submenu_element("Patch", "%R/fdiff?v1=%T&v2=%T&patch",
                        P("v1"), P("v2"));

  if( P("smhdr")!=0 ){
    @ <h2>Differences From Artifact
    @ %z(href("%R/artifact/%!S",zV1))[%S(zV1)]</a> To
    @ %z(href("%R/artifact/%!S",zV2))[%S(zV2)]</a>.</h2>
  }else{
    @ <h2>Differences From
    @ Artifact %z(href("%R/artifact/%!S",zV1))[%S(zV1)]</a>:</h2>
    object_description(v1, objdescFlags,0, 0);
    @ <h2>To Artifact %z(href("%R/artifact/%!S",zV2))[%S(zV2)]</a>:</h2>
    object_description(v2, objdescFlags,0, 0);
  }
  if( pRe ){
    @ <b>Only differences that match regular expression "%h(zRe)"
    @ are shown.</b>
    DCfg.pRe = pRe;
  }
  @ <hr>
  append_diff(zV1, zV2, &DCfg);
  append_diff_javascript(diffType);
  style_finish_page();
}

/*
** WEBPAGE: raw
** URL: /raw/ARTIFACTID
** URL: /raw?ci=BRANCH&filename=NAME
**
** Additional query parameters:
**
**    m=MIMETYPE       The mimetype is MIMETYPE
**    at=FILENAME      Content-disposition; attachment; filename=FILENAME;
**
** Return the uninterpreted content of an artifact.  Used primarily
** to view artifacts that are images.
*/
void rawartifact_page(void){
  int rid = 0;
  char *zUuid;

  (void)P("at")/*for cgi_check_for_malice()*/;
  (void)P("m");
  if( P("ci") ){
    rid = artifact_from_ci_and_filename(0);
  }
  if( rid==0 ){
    rid = name_to_rid_www("name");
  }
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  cgi_check_for_malice();
  if( rid==0 ) fossil_redirect_home();
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  etag_check(ETAG_HASH, zUuid);
  if( fossil_strcmp(P("name"), zUuid)==0 && login_is_nobody() ){
    g.isConst = 1;
  }
  free(zUuid);
  deliver_artifact(rid, P("m"));
}


/*
** WEBPAGE: secureraw
** URL: /secureraw/HASH?m=TYPE
**
** Return the uninterpreted content of an artifact.  This is similar
** to /raw except in this case the only way to specify the artifact
** is by the full-length SHA1 or SHA3 hash.  Abbreviations are not
** accepted.
*/
void secure_rawartifact_page(void){
  int rid = 0;
  const char *zName = PD("name", "");

  (void)P("at")/*for cgi_check_for_malice()*/;
  (void)P("m");
  cgi_check_for_malice();
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q", zName);
  if( rid==0 ){
    cgi_set_status(404, "Not Found");
    @ Unknown artifact: "%h(zName)"
    return;
  }
  g.isConst = 1;
  deliver_artifact(rid, P("m"));
}


/*
** WEBPAGE: jchunk hidden
** URL: /jchunk/HASH?from=N&to=M
**
** Return lines of text from a file as a JSON array - one entry in the
** array for each line of text.
**
** The HASH is normally a sha1 or sha3 hash that identifies an artifact
** in the BLOB table of the database.  However, if HASH starts with an "x"
** and is followed by valid hexadecimal, and if we are running in a
** "fossil ui" situation (locally and with privilege), then decode the hex
** into a filename and read the file content from that name.
**
** **Warning:**  This is an internal-use-only interface that is subject to
** change at any moment.  External application should not use this interface
** since the application will break when this interface changes, and this
** interface will undoubtedly change.
**
** This page is intended to be used in an XHR from javascript on a
** diff page, to return unseen context to fill in additional context
** when the user clicks on the appropriate button. The response is
** always in JSON form and errors are reported as documented for
** ajax_route_error().
*/
void jchunk_page(void){
  int rid = 0;
  const char *zName = PD("name", "");
  int nName = (int)(strlen(zName)&0x7fffffff);
  int iFrom = atoi(PD("from","0"));
  int iTo = atoi(PD("to","0"));
  int ln;
  int go = 1;
  const char *zSep;
  Blob content;
  Blob line;
  Blob *pOut;

  if(0){
    ajax_route_error(400, "Just testing client-side error handling.");
    return;
  }

  login_check_credentials();
  cgi_check_for_malice();
  if( !g.perm.Read ){
    ajax_route_error(403, "Access requires Read permissions.");
    return;
  }
  if( iFrom<1 || iTo<iFrom ){
    ajax_route_error(500, "Invalid line range from=%d, to=%d.",
                     iFrom, iTo);
    return;
  }
  if( zName[0]=='x'
   && ((nName-1)&1)==0
   && validate16(&zName[1],nName-1)
   && g.perm.Admin
   && cgi_is_loopback(g.zIpAddr)
   && db_open_local(0)
  ){
    /* Treat the HASH as a hex-encoded filename */
    int n = (nName-1)/2;
    char *zFN = fossil_malloc(n+1);
    decode16((const u8*)&zName[1], (u8*)zFN, nName-1);
    zFN[n] = 0;
    if( file_size(zFN, ExtFILE)<0 ){
      blob_zero(&content);
    }else{
      blob_read_from_file(&content, zFN, ExtFILE);
    }
    fossil_free(zFN);
  }else{
    /* Treat the HASH as an artifact hash matching BLOB.UUID */
#if 1
    /* Re-enable this block once this code is integrated somewhere into
       the UI. */
    rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q", zName);
    if( rid==0 ){
      ajax_route_error(404, "Unknown artifact: %h", zName);
      return;
    }
#else
    /* This impl is only to simplify "manual" testing via the JS
       console. */
    rid = symbolic_name_to_rid(zName, "*");
    if( rid==0 ){
      ajax_route_error(404, "Unknown artifact: %h", zName);
      return;
    }else if( rid<0 ){
      ajax_route_error(418, "Ambiguous artifact name: %h", zName);
      return;
    }
#endif
    content_get(rid, &content);
  }
  g.isConst = 1;
  cgi_set_content_type("application/json");
  ln = 0;
  while( go && ln<iFrom ){
    go = blob_line(&content, &line);
    ln++;
  }
  pOut = cgi_output_blob();
  blob_append(pOut, "[\n", 2);
  zSep = 0;
  while( go && ln<=iTo ){
    if( zSep ) blob_append(pOut, zSep, 2);
    blob_trim(&line);
    blob_append_json_literal(pOut, blob_buffer(&line), blob_size(&line));
    zSep = ",\n";
    go = blob_line(&content, &line);
    ln++;
  }
  blob_appendf(pOut,"]\n");
  blob_reset(&content);
}

/*
** Generate a verbatim artifact as the result of an HTTP request.
** If zMime is not NULL, use it as the mimetype.  If zMime is
** NULL, guess at the mimetype based on the filename
** associated with the artifact.
*/
void deliver_artifact(int rid, const char *zMime){
  Blob content;
  const char *zAttachName = P("at");
  if( zMime==0 ){
    char *zFN = (char*)zAttachName;
    if( zFN==0 ){
      zFN = db_text(0, "SELECT filename.name FROM mlink, filename"
                       " WHERE mlink.fid=%d"
                       "   AND filename.fnid=mlink.fnid", rid);
    }
    if( zFN==0 ){
      /* Look also at the attachment table */
      zFN = db_text(0, "SELECT attachment.filename FROM attachment, blob"
                       " WHERE blob.rid=%d"
                       "   AND attachment.src=blob.uuid", rid);
    }
    if( zFN ){
      zMime = mimetype_from_name(zFN);
    }
    if( zMime==0 ){
      zMime = "application/x-fossil-artifact";
    }
  }
  content_get(rid, &content);
  fossil_free(style_csp(1));
  cgi_set_content_type(zMime);
  if( zAttachName ){
    cgi_content_disposition_filename(zAttachName);
  }
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
    cgi_append_content(zLine, 55);
    for(j=k=0; j<16; j++){
      if( i+j<n ){
        unsigned char c = x[i+j];
        if( c>'>' && c<=0x7e ){
          zLine[k++] = c;
        }else if( c=='>' ){
          zLine[k++] = '&';
          zLine[k++] = 'g';
          zLine[k++] = 't';
          zLine[k++] = ';';
        }else if( c=='<' ){
          zLine[k++] = '&';
          zLine[k++] = 'l';
          zLine[k++] = 't';
          zLine[k++] = ';';
        }else if( c=='&' ){
          zLine[k++] = '&';
          zLine[k++] = 'a';
          zLine[k++] = 'm';
          zLine[k++] = 'p';
          zLine[k++] = ';';
        }else if( c>=' ' ){
          zLine[k++] = c;
        }else{
          zLine[k++] = '.';
        }
      }else{
        break;
      }
    }
    zLine[k++] = '\n';
    cgi_append_content(zLine, k);
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
  cgi_check_for_malice();
  if( g.perm.Admin ){
    const char *zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
    if( db_exists("SELECT 1 FROM shun WHERE uuid=%Q", zUuid) ){
      style_submenu_element("Unshun", "%R/shun?accept=%s&sub=1#delshun", zUuid);
    }else{
      style_submenu_element("Shun", "%R/shun?shun=%s#addshun", zUuid);
    }
  }
  style_header("Hex Artifact Content");
  zUuid = db_text("?","SELECT uuid FROM blob WHERE rid=%d", rid);
  etag_check(ETAG_HASH, zUuid);
  @ <h2>Artifact
  style_copy_button(1, "hash-ar", 0, 2, "%s", zUuid);
  if( g.perm.Setup ){
    @  (%d(rid)):</h2>
  }else{
    @ :</h2>
  }
  blob_zero(&downloadName);
  if( P("verbose")!=0 ) objdescFlags |= OBJDESC_DETAIL;
  object_description(rid, objdescFlags, 0, &downloadName);
  style_submenu_element("Download", "%R/raw/%s?at=%T",
                        zUuid, file_tail(blob_str(&downloadName)));
  @ <hr>
  content_get(rid, &content);
  if( !g.isHuman ){
    /* Prevent robots from running hexdump on megabyte-sized source files
    ** and there by eating up lots of CPU time and bandwidth.  There is
    ** no good reason for a robot to need a hexdump. */
    @ <p>A hex dump of this file is not available.
    @  Please download the raw binary file and generate a hex dump yourself.</p>
  }else{
    @ <blockquote><pre>
    hexdump(&content);
    @ </pre></blockquote>
  }
  style_finish_page();
}

/*
** Look for "ci" and "filename" query parameters.  If found, try to
** use them to extract the record ID of an artifact for the file.
**
** Also look for "fn" and "name" as an aliases for "filename".  If any
** "filename" or "fn" or "name" are present but "ci" is missing, then
** use "tip" as the default value for "ci".
**
** If zNameParam is not NULL, then use that parameter as the filename
** rather than "fn" or "filename" or "name".  the zNameParam is used
** for the from= and to= query parameters of /fdiff.
*/
int artifact_from_ci_and_filename(const char *zNameParam){
  const char *zFilename;
  const char *zCI;
  int cirid;
  Manifest *pManifest;
  ManifestFile *pFile;
  int rid = 0;

  if( zNameParam ){
    zFilename = P(zNameParam);
  }else{
    zFilename = P("filename");
    if( zFilename==0 ){
      zFilename = P("fn");
    }
    if( zFilename==0 ){
      zFilename = P("name");
    }
  }
  if( zFilename==0 ) return 0;

  zCI = PD("ci", "tip");
  cirid = name_to_typed_rid(zCI, "ci");
  if( cirid<=0 ) return 0;
  pManifest = manifest_get(cirid, CFTYPE_MANIFEST, 0);
  if( pManifest==0 ) return 0;
  manifest_file_rewind(pManifest);
  while( (pFile = manifest_file_next(pManifest,0))!=0 ){
    if( fossil_strcmp(zFilename, pFile->zName)==0 ){
      rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q", pFile->zUuid);
      break;
    }
  }
  manifest_destroy(pManifest);
  return rid;
}

/*
** The "z" argument is a string that contains the text of a source
** code file and nZ is its length in bytes. This routine appends that
** text to the HTTP reply with line numbering.
**
** zName is the content's file name, if any (it may be NULL). If that
** name contains a '.' then the part after the final '.' is used as
** the X part of a "language-X" CSS class on the generated CODE block.
**
** zLn is the ?ln= parameter for the HTTP query.  If there is an argument,
** then highlight that line number and scroll to it once the page loads.
** If there are two line numbers, highlight the range of lines.
** Multiple ranges can be highlighed by adding additional line numbers
** separated by a non-digit character (also not one of [-,.]).
**
** If includeJS is true then the JS code associated with line
** numbering is also emitted, else it is not. If this routine is
** called multiple times in a single app run, the JS is emitted only
** once. Note that when using this routine to emit Ajax responses, the
** JS should be not be included, as it will not get imported properly
** into the response's rendering.
*/
void output_text_with_line_numbers(
  const char *z,
  int nZ,
  const char *zName,
  const char *zLn,
  int includeJS
){
  int iStart, iEnd;    /* Start and end of region to highlight */
  int n = 0;           /* Current line number */
  int i = 0;           /* Loop index */
  int iTop = 0;        /* Scroll so that this line is on top of screen. */
  int nLine = 0;       /* content line count */
  int nSpans = 0;      /* number of distinct zLn spans */
  const char *zExt = file_extension(zName);
  static int emittedJS = 0; /* emitted shared JS yet? */
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
      ++nSpans;
      iStart = iEnd = atoi(&zLn[i++]);
    }while( zLn[i] && iStart && iEnd );
  }
  /*cgi_printf("<!-- ln span count=%d -->", nSpans);*/
  cgi_append_content("<table class='numbered-lines'><tbody>"
                     "<tr><td class='line-numbers'><pre>", -1);
  iStart = iEnd = 0;
  count_lines(z, nZ, &nLine);
  for( n=1 ; n<=nLine; ++n ){
    const char * zAttr = "";
    const char * zId = "";
    if(nSpans>0 && iEnd==0){/*Grab the next range of zLn marking*/
      db_prepare(&q, "SELECT iStart, iEnd FROM lnos "
                 "WHERE iStart >= %d ORDER BY iStart", n);
      if( db_step(&q)==SQLITE_ROW ){
        iStart = db_column_int(&q, 0);
        iEnd = db_column_int(&q, 1);
        if(!iTop){
          iTop = iStart - 15 + (iEnd-iStart)/4;
          if( iTop>iStart - 2 ) iTop = iStart-2;
        }
      }else{
        /* Note that overlapping multi-spans, e.g. 10-15+12-20,
           can cause us to miss a row. */
        iStart = iEnd = 0;
      }
      db_finalize(&q);
      --nSpans;
      /*cgi_printf("<!-- iStart=%d, iEnd=%d -->", iStart, iEnd);*/
    }
    if(n==iTop) {
      zId = " id='scrollToMe'";
    }
    if(n==iStart){/*Figure out which CSS class(es) this line needs...*/
      if(n==iEnd){
        zAttr = " class='selected-line start end'";
        iEnd = 0;
      }else{
        zAttr = " class='selected-line start'";
      }
      iStart = 0;
    }else if(n==iEnd){
      zAttr = " class='selected-line end'";
      iEnd = 0;
    }else if( n>iStart && n<iEnd ){
      zAttr = " class='selected-line'";
    }
    cgi_printf("<span%s%s>%6d</span>\n", zId, zAttr, n)
      /* ^^^ explicit \n is necessary for text-mode browsers. */;
  }
  cgi_append_content("</pre></td><td class='file-content'><pre>",-1);
  if(zExt && *zExt){
    cgi_printf("<code class='language-%h'>",zExt);
  }else{
    cgi_append_content("<code>", -1);
  }
  cgi_printf("%z", htmlize(z, nZ));
  CX("</code></pre></td></tr></tbody></table>\n");
  if(includeJS && !emittedJS){
    emittedJS = 1;
    if( db_int(0, "SELECT EXISTS(SELECT 1 FROM lnos)") ){
      builtin_request_js("scroll.js");
    }
    builtin_fossil_js_bundle_or("numbered-lines", NULL);
  }
}

/*
** COMMAND: test-line-numbers
**
** Usage: %fossil test-line-numbers FILE ?LN-SPEC?
**
*/
void cmd_test_line_numbers(void){
  Blob content = empty_blob;
  const char * zLn = "";
  const char * zFilename = 0;

  if(g.argc < 3){
    usage("FILE");
  }else if(g.argc>3){
    zLn = g.argv[3];
  }
  db_find_and_open_repository(0,0);
  zFilename = g.argv[2];
  fossil_print("%s %s\n", zFilename, zLn);

  blob_read_from_file(&content, zFilename, ExtFILE);
  output_text_with_line_numbers(blob_str(&content), blob_size(&content),
                                zFilename, zLn, 0);
  blob_reset(&content);
  fossil_print("%b\n", cgi_output_blob());
}

/*
** WEBPAGE: artifact
** WEBPAGE: file
** WEBPAGE: whatis
** WEBPAGE: docfile
**
** Typical usage:
**
**    /artifact/HASH
**    /whatis/HASH
**    /file/NAME
**    /docfile/NAME
**
** Additional query parameters:
**
**   ln              - show line numbers
**   ln=N            - highlight line number N
**   ln=M-N          - highlight lines M through N inclusive
**   ln=M-N+Y-Z      - highlight lines M through N and Y through Z (inclusive)
**   verbose         - show more detail in the description
**   brief           - show just the document, not the metadata.  The
**                     /docfile page is an alias for /file?brief
**   download        - redirect to the download (artifact page only)
**   name=NAME       - filename or hash as a query parameter
**   filename=NAME   - alternative spelling for "name="
**   fn=NAME         - alternative spelling for "name="
**   ci=VERSION      - The specific check-in to use with "name=" to
**                     identify the file.
**   txt             - Force display of unformatted source text
**   hash            - Output only the hash of the artifact
**
** The /artifact page show the complete content of a file
** identified by HASH.  The /whatis page shows only a description
** of how the artifact is used.  The /file page shows the most recent
** version of the file or directory called NAME, or a list of the
** top-level directory if NAME is omitted.
**
** For /artifact and /whatis, the name= query parameter can refer to
** either the name of a file, or an artifact hash.  If the ci= query
** parameter is also present, then name= must refer to a file name.
** If ci= is omitted, then the hash interpretation is preferred but
** if name= cannot be understood as a hash, a default "tip" value is
** used for ci=.
**
** For /file, name= can only be interpreted as a filename.  As before,
** a default value of "tip" is used for ci= if ci= is omitted.
*/
void artifact_page(void){
  int rid = 0;
  Blob content;
  const char *zMime;
  Blob downloadName;
  Blob uuid;
  int renderAsWiki = 0;
  int renderAsHtml = 0;
  int renderAsSvg = 0;
  int objType;
  int asText;
  const char *zUuid = 0;
  u32 objdescFlags = OBJDESC_BASE;
  int descOnly = fossil_strcmp(g.zPath,"whatis")==0;
  int hashOnly = P("hash")!=0;
  int docOnly = P("brief")!=0;
  int isFile = fossil_strcmp(g.zPath,"file")==0;
  const char *zLn = P("ln");
  const char *zName = P("name");
  const char *zCI = P("ci");
  HQuery url;
  char *zCIUuid = 0;
  int isSymbolicCI = 0;  /* ci= exists and is a symbolic name, not a hash */
  int isBranchCI = 0;    /* ci= refers to a branch name */
  char *zHeader = 0;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  cgi_check_for_malice();
  style_set_current_feature("artifact");
  if( fossil_strcmp(g.zPath, "docfile")==0 ){
    isFile = 1;
    docOnly = 1;
  }

  /* Capture and normalize the name= and ci= query parameters */
  if( zName==0 ){
    zName = P("filename");
    if( zName==0 ){
      zName = P("fn");
    }
  }
  if( zCI && strlen(zCI)==0 ){ zCI = 0; }
  if( zCI
   && name_to_uuid2(zCI, "ci", &zCIUuid)
   && sqlite3_strnicmp(zCIUuid, zCI, strlen(zCI))!=0
  ){
    isSymbolicCI = 1;
    isBranchCI = branch_includes_uuid(zCI, zCIUuid);
  }

  /* The name= query parameter (or at least one of its alternative
  ** spellings) is required.  Except for /file, show a top-level
  ** directory listing if name= is omitted.
  */
  if( zName==0 ){
    if( isFile ){
      if( P("ci")==0 ) cgi_set_query_parameter("ci","tip");
      page_tree();
      return;
    }
    style_header("Missing name= query parameter");
    @ The name= query parameter is missing
    style_finish_page();
    return;
  }

  url_initialize(&url, g.zPath);
  url_add_parameter(&url, "name", zName);
  url_add_parameter(&url, "ci", zCI);     /* no-op if zCI is NULL */

  if( zCI==0 && !isFile ){
    /* If there is no ci= query parameter, then prefer to interpret
    ** name= as a hash for /artifact and /whatis.  But not for /file.
    ** For /file, a name= without a ci= will prefer to use the default
    ** "tip" value for ci=. */
    rid = name_to_rid(zName);
  }
  if( rid==0 ){
    rid = artifact_from_ci_and_filename(0);
  }

  if( rid==0 ){  /* Artifact not found */
    if( isFile ){
      Stmt q;
      /* For /file, also check to see if name= refers to a directory,
      ** and if so, do a listing for that directory */
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
      /* No directory found, look for an historic version of the file
      ** that was subsequently deleted. */
      db_prepare(&q,
        "SELECT fid, uuid FROM mlink, filename, event, blob"
        " WHERE filename.name=%Q"
        "   AND mlink.fnid=filename.fnid AND mlink.fid>0"
        "   AND event.objid=mlink.mid"
        "   AND blob.rid=mlink.mid"
        " ORDER BY event.mtime DESC",
        zName
      );
      if( db_step(&q)==SQLITE_ROW ){
        rid = db_column_int(&q, 0);
        zCI = fossil_strdup(db_column_text(&q, 1));
        zCIUuid = fossil_strdup(zCI);
        url_add_parameter(&url, "ci", zCI);
      }
      db_finalize(&q);
      if( rid==0 ){
        style_header("No such file");
        @ File '%h(zName)' does not exist in this repository.
      }
    }else{
      style_header("No such artifact");
      @ Artifact '%h(zName)' does not exist in this repository.
    }
    if( rid==0 ){
      style_finish_page();
      return;
    }
  }

  if( descOnly || P("verbose")!=0 ){
    url_add_parameter(&url, "verbose", "1");
    objdescFlags |= OBJDESC_DETAIL;
  }
  zUuid = db_text("?", "SELECT uuid FROM blob WHERE rid=%d", rid);
  etag_check(ETAG_HASH, zUuid);

  if( descOnly && hashOnly ){
    blob_set(&uuid, zUuid);
    cgi_set_content_type("text/plain");
    cgi_set_content(&uuid);
    return;
  }

  asText = P("txt")!=0;
  if( isFile ){
    if( docOnly ){
      /* No header */
    }else if( zCI==0 || fossil_strcmp(zCI,"tip")==0 ){
      zCI = "tip";
      @ <h2>File %z(href("%R/finfo?name=%T&m&ci=tip",zName))%h(zName)</a>
      @ from the %z(href("%R/info/tip"))latest check-in</a></h2>
    }else{
      const char *zPath;
      Blob path;
      blob_zero(&path);
      hyperlinked_path(zName, &path, zCI, "dir", "", LINKPATH_FINFO);
      zPath = blob_str(&path);
      @ <h2>File %s(zPath) artifact \
      style_copy_button(1,"hash-fid",0,0,"%z%S</a> ",
           href("%R/info/%s",zUuid),zUuid);
      if( isBranchCI ){
        @ on branch %z(href("%R/timeline?r=%T",zCI))%h(zCI)</a></h2>
      }else if( isSymbolicCI ){
        @ part of check-in %z(href("%R/info/%!S",zCIUuid))%s(zCI)</a></h2>
      }else{
        @ part of check-in %z(href("%R/info/%!S",zCIUuid))%S(zCIUuid)</a></h2>
      }
      blob_reset(&path);
    }
    zMime = mimetype_from_name(zName);
    if( !docOnly ){
      style_submenu_element("Artifact", "%R/artifact/%S", zUuid);
      style_submenu_element("Annotate", "%R/annotate?filename=%T&checkin=%T",
                            zName, zCI);
      style_submenu_element("Blame", "%R/blame?filename=%T&checkin=%T",
                            zName, zCI);
      style_submenu_element("Doc", "%R/doc/%T/%T", zCI, zName);
    }
    blob_init(&downloadName, zName, -1);
    objType = OBJTYPE_CONTENT;
  }else{
    @ <h2>Artifact
    style_copy_button(1, "hash-ar", 0, 2, "%s", zUuid);
    if( g.perm.Setup ){
      @  (%d(rid)):</h2>
    }else{
      @ :</h2>
    }
    blob_zero(&downloadName);
    if( asText ) objdescFlags &= ~OBJDESC_BASE;
    objType = object_description(rid, objdescFlags,
                                (isFile?zName:0), &downloadName);
    zMime = mimetype_from_name(blob_str(&downloadName));
  }
  if( !descOnly && P("download")!=0 ){
    cgi_redirectf("%R/raw/%s?at=%T",
          db_text("x", "SELECT uuid FROM blob WHERE rid=%d", rid),
          file_tail(blob_str(&downloadName)));
    /*NOTREACHED*/
  }
  if( g.perm.Admin && !docOnly ){
    const char *zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
    if( db_exists("SELECT 1 FROM shun WHERE uuid=%Q", zUuid) ){
      style_submenu_element("Unshun", "%R/shun?accept=%s&sub=1#accshun", zUuid);
    }else{
      style_submenu_element("Shun", "%R/shun?shun=%s#addshun",zUuid);
    }
  }

  if( isFile ){
    if( isSymbolicCI ){
      zHeader = mprintf("%s at %s", file_tail(zName), zCI);
      style_set_current_page("doc/%t/%T", zCI, zName);
    }else if( zCIUuid && zCIUuid[0] ){
      zHeader = mprintf("%s at [%S]", file_tail(zName), zCIUuid);
      style_set_current_page("doc/%S/%T", zCIUuid, zName);
    }else{
      zHeader = mprintf("%s", file_tail(zName));
      style_set_current_page("doc/tip/%T", zName);
    }
  }else if( descOnly ){
    zHeader = mprintf("Artifact Description [%S]", zUuid);
  }else{
    zHeader = mprintf("Artifact [%S]", zUuid);
  }
  style_header("%s", zHeader);
  fossil_free(zCIUuid);
  fossil_free(zHeader);
  if( !isFile && g.perm.Admin ){
    Stmt q;
    db_prepare(&q,
      "SELECT coalesce(user.login,rcvfrom.uid),"
      "       datetime(rcvfrom.mtime,toLocal()),"
      "       coalesce(rcvfrom.ipaddr,'unknown')"
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
  if( !docOnly ){
    style_submenu_element("Download", "%R/raw/%s?at=%T",zUuid,file_tail(zName));
    if( db_exists("SELECT 1 FROM mlink WHERE fid=%d", rid) ){
      style_submenu_element("Check-ins Using", "%R/timeline?uf=%s", zUuid);
    }
  }
  if( zMime ){
    if( fossil_strcmp(zMime, "text/html")==0 ){
      if( asText ){
        style_submenu_element("Html", "%s", url_render(&url, "txt", 0, 0, 0));
      }else{
        renderAsHtml = 1;
        if( !docOnly ){
          style_submenu_element("Text", "%s", url_render(&url, "txt","1",0,0));
        }
      }
    }else if( fossil_strcmp(zMime, "text/x-fossil-wiki")==0
           || fossil_strcmp(zMime, "text/x-markdown")==0
           || fossil_strcmp(zMime, "text/x-pikchr")==0 ){
      if( asText ){
        style_submenu_element(zMime[7]=='p' ? "Pikchr" : "Wiki",
                              "%s", url_render(&url, "txt", 0, 0, 0));
      }else{
        renderAsWiki = 1;
        if( !docOnly ){
          style_submenu_element("Text", "%s", url_render(&url, "txt","1",0,0));
        }
      }
    }else if( fossil_strcmp(zMime, "image/svg+xml")==0 ){
      if( asText ){
        style_submenu_element("Svg", "%s", url_render(&url, "txt", 0, 0, 0));
      }else{
        renderAsSvg = 1;
        if( !docOnly ){
          style_submenu_element("Text", "%s", url_render(&url, "txt","1",0,0));
        }
      }
    }
    if( !docOnly && fileedit_is_editable(zName) ){
      style_submenu_element("Edit",
                            "%R/fileedit?filename=%T&checkin=%!S",
                            zName, zCI);
    }
  }
  if( (objType & (OBJTYPE_WIKI|OBJTYPE_TICKET))!=0 ){
    style_submenu_element("Parsed", "%R/info/%s", zUuid);
  }
  if( descOnly ){
    style_submenu_element("Content", "%R/artifact/%s", zUuid);
  }else{
    if( !docOnly || !isFile ){
      @ <hr>
    }
    content_get(rid, &content);
    if( renderAsWiki ){
      safe_html_context(DOCSRC_FILE);
      wiki_render_by_mimetype(&content, zMime);
      document_emit_js();
    }else if( renderAsHtml ){
      @ <iframe src="%R/raw/%s(zUuid)"
      @ width="100%%" frameborder="0" marginwidth="0" marginheight="0"
      @ sandbox="allow-same-origin" id="ifm1">
      @ </iframe>
      @ <script nonce="%h(style_nonce())">/* info.c:%d(__LINE__) */
      @ document.getElementById("ifm1").addEventListener("load",
      @   function(){
      @     this.height=this.contentDocument.documentElement.scrollHeight + 75;
      @   }
      @ );
      @ </script>
    }else if( renderAsSvg ){
      @ <object type="image/svg+xml" data="%R/raw/%s(zUuid)"></object>
    }else{
      const char *zContentMime;
      style_submenu_element("Hex", "%R/hexdump?name=%s", zUuid);
      if( zLn==0 || atoi(zLn)==0 ){
        style_submenu_checkbox("ln", "Line Numbers", 0, 0);
      }
      blob_to_utf8_no_bom(&content, 0);
      zContentMime = mimetype_from_content(&content);
      if( zMime==0 ) zMime = zContentMime;
      @ <blockquote class="file-content">
      if( zContentMime==0 ){
        const char *z, *zFileName, *zExt;
        z = blob_str(&content);
        zFileName = db_text(0,
         "SELECT name FROM mlink, filename"
         " WHERE filename.fnid=mlink.fnid"
         "   AND mlink.fid=%d",
         rid);
        zExt = zFileName ? file_extension(zFileName) : 0;
        if( zLn ){
          output_text_with_line_numbers(z, blob_size(&content),
                                        zFileName, zLn, 1);
        }else if( zExt && zExt[0] ){
          @ <pre>
          @ <code class="language-%s(zExt)">%h(z)</code>
          @ </pre>
        }else{
          @ <pre>
          @ %h(z)
          @ </pre>
        }
      }else if( strncmp(zMime, "image/", 6)==0 ){
        @ <p>(file is %d(blob_size(&content)) bytes of image data)</i></p>
        @ <p><img src="%R/raw/%s(zUuid)?m=%s(zMime)"></p>
        style_submenu_element("Image", "%R/raw/%s?m=%s", zUuid, zMime);
      }else if( strncmp(zMime, "audio/", 6)==0 ){
        @ <p>(file is %d(blob_size(&content)) bytes of sound data)</i></p>
        @ <audio controls src="%R/raw/%s(zUuid)?m=%s(zMime)">
        @ (Not supported by this browser)
        @ </audio>
      }else{
        @ <i>(file is %d(blob_size(&content)) bytes of binary data)</i>
      }
      @ </blockquote>
    }
  }
  style_finish_page();
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
  char zTktName[HNAME_MAX+1];
  Manifest *pTktChng;
  int modPending;
  const char *zModAction;
  char *zTktTitle;
  login_check_credentials();
  if( !g.perm.RdTkt ){ login_needed(g.anon.RdTkt); return; }
  rid = name_to_rid_www("name");
  if( rid==0 ){ fossil_redirect_home(); }
  cgi_check_for_malice();
  zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( g.perm.Admin ){
    if( db_exists("SELECT 1 FROM shun WHERE uuid=%Q", zUuid) ){
      style_submenu_element("Unshun", "%R/shun?accept=%s&sub=1#accshun", zUuid);
    }else{
      style_submenu_element("Shun", "%R/shun?shun=%s#addshun", zUuid);
    }
  }
  pTktChng = manifest_get(rid, CFTYPE_TICKET, 0);
  if( pTktChng==0 ) fossil_redirect_home();
  zDate = db_text(0, "SELECT datetime(%.12f,toLocal())", pTktChng->rDate);
  sqlite3_snprintf(sizeof(zTktName), zTktName, "%s", pTktChng->zTicketUuid);
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
      moderation_approve('t', rid);
    }
  }
  zTktTitle = db_table_has_column("repository", "ticket", "title" )
      ? db_text("(No title)",
                "SELECT title FROM ticket WHERE tkt_uuid=%Q", zTktName)
      : 0;
  style_set_current_feature("tinfo");
  style_header("Ticket Change Details");
  style_submenu_element("Raw", "%R/artifact/%s", zUuid);
  style_submenu_element("History", "%R/tkthistory/%s#%S", zTktName,zUuid);
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
  modPending = moderation_pending_www(rid);
  @ <tr><th>Ticket:</th>
  @ <td>%z(href("%R/tktview/%s",zTktName))%s(zTktName)</a>
  if( zTktTitle ){
        @<br>%h(zTktTitle)
  }
  @</td></tr>
  @ <tr><th>User&nbsp;&amp;&nbsp;Date:</th><td>
  hyperlink_to_user(pTktChng->zUser, zDate, " on ");
  hyperlink_to_date(zDate, "</td></tr>");
  @ </table>
  free(zDate);
  free(zTktTitle);

  if( g.perm.ModTkt && modPending ){
    @ <div class="section">Moderation</div>
    @ <blockquote>
    @ <form method="POST" action="%R/tinfo/%s(zUuid)">
    @ <label><input type="radio" name="modaction" value="delete">
    @ Delete this change</label><br>
    @ <label><input type="radio" name="modaction" value="approve">
    @ Approve this change</label><br>
    @ <input type="submit" value="Submit">
    @ </form>
    @ </blockquote>
  }

  @ <div class="section">Changes</div>
  @ <p>
  ticket_output_change_artifact(pTktChng, 0, 1, 0);
  manifest_destroy(pTktChng);
  style_finish_page();
}

/*
** rid is a cluster.  Paint a page that contains detailed information
** about that cluster.
*/
static void cluster_info(int rid, const char *zName){
  Manifest *pCluster;
  int i;
  Blob where = BLOB_INITIALIZER;
  Blob unks = BLOB_INITIALIZER;
  Stmt q;
  char *zSha1Bg;
  char *zSha3Bg;
  int badRid = 0;
  int rcvid;
  int hashClr = PB("hclr");
  const char *zDate;

  pCluster = manifest_get(rid, CFTYPE_CLUSTER, 0);
  if( pCluster==0 ){
    artifact_page();
    return;
  }  
  style_header("Cluster %S", zName);
  rcvid = db_int(0, "SELECT rcvid FROM blob WHERE rid=%d", rid);
  if( rcvid==0 ){
    zDate = 0;
  }else{
    zDate = db_text(0, "SELECT datetime(mtime) FROM rcvfrom WHERE rcvid=%d",
                    rcvid);
  }
  @ <p>Artifact %z(href("%R/artifact/%h",zName))%S(zName)</a> is a cluster
  @ with %d(pCluster->nCChild) entries
  if( g.perm.Admin ){
    @ received <a href="%R/rcvfrom?rcvid=%d(rcvid)">%h(zDate)</a>:
  }else{
    @ received %h(zDate):
  }
  blob_appendf(&where,"IN(0");
  for(i=0; i<pCluster->nCChild; i++){
    int rid = fast_uuid_to_rid(pCluster->azCChild[i]);
    if( rid ){
      blob_appendf(&where,",%d", rid);
    }else{
      if( blob_size(&unks)>0 ) blob_append_char(&unks, ',');
      badRid++;
      blob_append_sql(&unks,"(%d,%Q)",-badRid,pCluster->azCChild[i]);
    }
  }
  blob_append_char(&where,')');
  describe_artifacts(blob_str(&where));
  blob_reset(&where);
  if( badRid>0 ){
    db_multi_exec(
      "WITH unks(rx,hx) AS (VALUES %s)\n"
      "INSERT INTO description(rid,uuid,type,summary) "
      "  SELECT rx, hx, 'phantom', '' FROM unks;",
      blob_sql_text(&unks)
    );
  }
  blob_reset(&unks);
  db_prepare(&q,
    "SELECT rid, uuid, summary, isPrivate, type='phantom', rcvid, ref"
    "  FROM description ORDER BY uuid"
  );
  if( skin_detail_boolean("white-foreground") ){
    zSha1Bg = "#714417";
    zSha3Bg = "#177117";
  }else{
    zSha1Bg = "#ebffb0";
    zSha3Bg = "#b0ffb0";
  }
  @ <table cellpadding="2" cellspacing="0" border="1">
  if( g.perm.Admin ){
    @ <tr><th>RID<th>Hash<th>Rcvid<th>Description<th>Ref<th>Remarks
  }else{
    @ <tr><th>RID<th>Hash<th>Description<th>Ref<th>Remarks
  }
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q,0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDesc = db_column_text(&q, 2);
    int isPriv = db_column_int(&q,3);
    int isPhantom = db_column_int(&q,4);
    const char *zRef = db_column_text(&q,6);
    if( isPriv && !isPhantom && !g.perm.Private && !g.perm.Admin ){
      /* Don't show private artifacts to users without Private (x) permission */
      continue;
    }
    if( rid<=0 ){
      @ <tr><td>&nbsp;</td>
    }else if( hashClr ){
      const char *zClr = db_column_bytes(&q,1)>40 ? zSha3Bg : zSha1Bg;
      @ <tr style='background-color:%s(zClr);'><td align="right">%d(rid)</td>
    }else{
      @ <tr><td align="right">%d(rid)</td>
    }
    if( rid<=0 ){
      @ <td>&nbsp;%S(zUuid)&nbsp;</td>
    }else{
      @ <td>&nbsp;%z(href("%R/info/%!S",zUuid))%S(zUuid)</a>&nbsp;</td>
    }
    if( g.perm.Admin ){
      int rcvid = db_column_int(&q,5);
      if( rcvid<=0 ){
        @ <td>&nbsp;
      }else{
        @ <td><a href='%R/rcvfrom?rcvid=%d(rcvid)'>%d(rcvid)</a>
      }
    }
    @ <td align="left">%h(zDesc)</td>
    if( zRef && zRef[0] ){
      @ <td>%z(href("%R/info/%!S",zRef))%S(zRef)</a>
    }else{
      @ <td>&nbsp;
    }
    if( isPriv || isPhantom ){
      if( isPriv==0 ){
        @ <td>phantom</td>
      }else if( isPhantom==0 ){
        @ <td>private</td>
      }else{
        @ <td>private,phantom</td>
      }
    }else{
      @ <td>&nbsp;
    }
    @ </tr>
  }
  @ </table>
  db_finalize(&q);
  style_finish_page();
}


/*
** WEBPAGE: info
** URL: info/NAME
**
** The NAME argument is any valid artifact name: an artifact hash,
** a timestamp, a tag name, etc.
**
** Because NAME can match so many different things (commit artifacts,
** wiki pages, ticket comments, forum posts...) the format of the output
** page depends on the type of artifact that NAME matches.
*/
void info_page(void){
  const char *zName;
  Blob uuid;
  int rid;
  int rc;
  int nLen;

  zName = P("name");
  if( zName==0 ) fossil_redirect_home();
  cgi_check_for_malice();
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
        cgi_set_parameter_nocopy("tl","1",0);
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
    style_finish_page();
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
    style_finish_page();
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
  if( db_table_exists("repository","forumpost")
   && db_exists("SELECT 1 FROM forumpost WHERE fpid=%d", rid)
  ){
    forumthread_page();
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
  if( db_exists("SELECT 1 FROM tagxref WHERE rid=%d AND tagid=%d",
                rid, TAG_CLUSTER) ){
    cluster_info(rid, zName);
  }else
  {
    artifact_page();
  }
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
static void apply_newtags(
  Blob *ctrl,
  int rid,
  const char *zUuid,
  const char *zUserOvrd,  /* The user name on the control artifact */
  int fDryRun             /* Print control artifact, but make no changes */
){
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
    if( zUserOvrd && zUserOvrd[0] ){
      blob_appendf(ctrl, "U %F\n", zUserOvrd);
    }else{
      blob_appendf(ctrl, "U %F\n", login_name());
    }
    md5sum_blob(ctrl, &cksum);
    blob_appendf(ctrl, "Z %b\n", &cksum);
    if( fDryRun ){
      assert( g.isHTTP==0 ); /* Only print control artifact in console mode. */
      fossil_print("%s", blob_str(ctrl));
      blob_reset(ctrl);
    }else{
      db_begin_transaction();
      g.markPrivate = content_is_private(rid);
      nrid = content_put(ctrl);
      manifest_crosslink(nrid, ctrl, MC_PERMIT_HOOKS);
      db_end_transaction(0);
    }
    assert( blob_is_reset(ctrl) );
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
**
** Edit a check-in.  (Check-ins are immutable and do not really change.
** This page really creates supplemental tags that affect the display
** of the check-in.)
**
** Query parameters:
**
**     rid=INTEGER        Record ID of the check-in to edit (REQUIRED)
**
** POST parameters after pressing "Preview", "Cancel", or "Apply":
**
**     c=TEXT             New check-in comment
**     u=TEXT             New user name
**     newclr             Apply a background color
**     clr=TEXT           New background color (only if newclr)
**     pclr               Propagate new background color (only if newclr)
**     dt=TEXT            New check-in date/time (ISO8610 format)
**     newtag             Add a new tag to the check-in
**     tagname=TEXT       Name of the new tag to be added (only if newtag)
**     newbr              Put the check-in on a new branch
**     brname=TEXT        Name of the new branch (only if newbr)
**     close              Close this check-in
**     hide               Hide this check-in
**     cNNN               Cancel tag with tagid=NNN
**
**     cancel             Cancel the edit.  Return to the check-in view
**     preview            Show a preview of the edited check-in comment
**     apply              Apply changes
*/
void ci_edit_page(void){
  int rid;
  const char *zComment;         /* Current comment on the check-in */
  const char *zNewComment;      /* Revised check-in comment */
  const char *zUser;            /* Current user for the check-in */
  const char *zNewUser;         /* Revised user */
  const char *zDate;            /* Current date of the check-in */
  const char *zNewDate;         /* Revised check-in date */
  const char *zNewColorFlag;    /* "checked" if "Change color" is checked */
  const char *zColor;           /* Current background color */
  const char *zNewColor;        /* Revised background color */
  const char *zNewTagFlag;      /* "checked" if "Add tag" is checked */
  const char *zNewTag;          /* Name of the new tag */
  const char *zNewBrFlag;       /* "checked" if "New branch" is checked */
  const char *zNewBranch;       /* Name of the new branch */
  const char *zCloseFlag;       /* "checked" if "Close" is checked */
  const char *zHideFlag;        /* "checked" if "Hide" is checked */
  int fPropagateColor;          /* True if color propagates before edit */
  int fNewPropagateColor;       /* True if color propagates after edit */
  int fHasHidden = 0;           /* True if hidden tag already set */
  int fHasClosed = 0;           /* True if closed tag already set */
  const char *zChngTime = 0;    /* Value of chngtime= query param, if any */
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
    cgi_redirectf("%R/ci/%S", zUuid);
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
  fPropagateColor = db_int(0, "SELECT tagtype FROM tagxref"
                              " WHERE rid=%d AND tagid=%d",
                              rid, TAG_BGCOLOR)==2;
  fNewPropagateColor = P("clr")!=0 ? P("pclr")!=0 : fPropagateColor;
  zNewColorFlag = P("newclr") ? " checked" : "";
  zNewTagFlag = P("newtag") ? " checked" : "";
  zNewTag = PDT("tagname","");
  zNewBrFlag = P("newbr") ? " checked" : "";
  zNewBranch = PDT("brname","");
  zBranchName = branch_of_rid(rid);
  zCloseFlag = P("close") ? " checked" : "";
  zHideFlag = P("hide") ? " checked" : "";
  if( P("apply") && cgi_csrf_safe(2) ){
    Blob ctrl;
    char *zNow;

    blob_zero(&ctrl);
    zNow = date_in_standard_format(zChngTime ? zChngTime : "now");
    blob_appendf(&ctrl, "D %s\n", zNow);
    init_newtags();
    if( zNewColorFlag[0]
     && zNewColor[0]
     && (fPropagateColor!=fNewPropagateColor
             || fossil_strcmp(zColor,zNewColor)!=0)
    ){
      add_color(zNewColor,fNewPropagateColor);
    }
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
    apply_newtags(&ctrl, rid, zUuid, 0, 0);
    cgi_redirectf("%R/ci/%S", zUuid);
  }
  blob_zero(&comment);
  blob_append(&comment, zNewComment, -1);
  zUuid[10] = 0;
  style_header("Edit Check-in [%s]", zUuid);
  if( P("preview") ){
    Blob suffix;
    int nTag = 0;
    const char *zDplyBr;   /* Branch name used to determine BG color */
    if( zNewBrFlag[0] && zNewBranch[0] ){
      zDplyBr = zNewBranch;
    }else{
      zDplyBr = zBranchName;
    }
    @ <b>Preview:</b>
    @ <blockquote>
    @ <table border=0>
    if( zNewColorFlag[0] && zNewColor && zNewColor[0] ){
      @ <tr><td style="background-color:%h(reasonable_bg_color(zNewColor,0));">
    }else if( zColor[0] ){
      @ <tr><td style="background-color:%h(reasonable_bg_color(zColor,0));">
    }else if( zDplyBr && fossil_strcmp(zDplyBr,"trunk")!=0 ){
      @ <tr><td style="background-color:%h(hash_color(zDplyBr));">
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
    @ <hr>
    blob_reset(&suffix);
  }
  @ <p>Make changes to attributes of check-in
  @ [%z(href("%R/ci/%!S",zUuid))%s(zUuid)</a>]:</p>
  form_begin(0, "%R/ci_edit");
  @ <div><input type="hidden" name="r" value="%s(zUuid)">
  @ <table border="0" cellspacing="10">

  @ <tr><th align="right" valign="top">User:</th>
  @ <td valign="top">
  @   <input type="text" name="u" size="20" value="%h(zNewUser)">
  @ </td></tr>

  @ <tr><th align="right" valign="top">Comment:</th>
  @ <td valign="top">
  @ <textarea name="c" rows="10" cols="80">%h(zNewComment)</textarea>
  @ </td></tr>

  @ <tr><th align="right" valign="top">Check-in Time:</th>
  @ <td valign="top">
  @   <input type="text" name="dt" size="20" value="%h(zNewDate)">
  @ </td></tr>

  if( zChngTime ){
    @ <tr><th align="right" valign="top">Timestamp of this change:</th>
    @ <td valign="top">
    @   <input type="text" name="chngtime" size="20" value="%h(zChngTime)">
    @ </td></tr>
  }

  @ <tr><th align="right" valign="top">Background&nbsp;Color:</th>
  @ <td valign="top">
  @ <div><label><input type='checkbox' name='newclr'%s(zNewColorFlag)>
  @ Change background color: \
  @ <input type='color' name='clr'\
  @ value='%s(zNewColor[0]?zNewColor:"#808080")'></label></div>
  @ <div><label>
  if( fNewPropagateColor ){
    @ <input type="checkbox" name="pclr" checked="checked">
  }else{
    @ <input type="checkbox" name="pclr">
  }
  @ Propagate color to descendants</label></div>
  @ <div class='font-size-80'>Be aware that fixed background
  @ colors will not interact well with all available skins.
  @ It is recommended that Fossil be allowed to select these
  @ colors automatically so that it can take the skin's
  @ preferences into account.</div>
  @ </td></tr>

  @ <tr><th align="right" valign="top">Tags:</th>
  @ <td valign="top">
  @ <label><input type="checkbox" id="newtag" name="newtag"%s(zNewTagFlag)>
  @ Add the following new tag name to this check-in:</label>
  @ <input size="15" name="tagname" id="tagname" value="%h(zNewTag)">
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
    @ <br><label>
    if( P(zLabel) ){
      @ <input type="checkbox" name="c%d(tagid)" checked="checked">
    }else{
      @ <input type="checkbox" name="c%d(tagid)">
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
    zBranchName = db_get("main-branch", 0);
  }
  if( !zNewBranch || !zNewBranch[0]){
    zNewBranch = zBranchName;
  }
  @ <tr><th align="right" valign="top">Branching:</th>
  @ <td valign="top">
  @ <label><input id="newbr" type="checkbox" name="newbr" \
  @ data-branch='%h(zBranchName)'%s(zNewBrFlag)>
  @ Starting from this check-in, rename the branch to:</label>
  @ <input id="brname" type="text" style="width:15;" name="brname" \
  @ value="%h(zNewBranch)"></td></tr>
  if( !fHasHidden ){
    @ <tr><th align="right" valign="top">Branch Hiding:</th>
    @ <td valign="top">
    @ <label><input type="checkbox" id="hidebr" name="hide"%s(zHideFlag)>
    @ Hide branch
    @ <span style="font-weight:bold" id="hbranch">%h(zBranchName)</span>
    @ from the timeline starting from this check-in</label>
    @ </td></tr>
  }
  if( !fHasClosed ){
    if( is_a_leaf(rid) ){
      @ <tr><th align="right" valign="top">Leaf Closure:</th>
      @ <td valign="top">
      @ <label><input type="checkbox" name="close"%s(zCloseFlag)>
      @ Mark this leaf as "closed" so that it no longer appears on the
      @ "leaves" page and is no longer labeled as a "<b>Leaf</b>"</label>
      @ </td></tr>
    }else if( zBranchName ){
      @ <tr><th align="right" valign="top">Branch Closure:</th>
      @ <td valign="top">
      @ <label><input type="checkbox" name="close"%s(zCloseFlag)>
      @ Mark branch
      @ <span style="font-weight:bold" id="cbranch">%h(zBranchName)</span>
      @ as "closed".</label>
      @ </td></tr>
    }
  }
  if( zBranchName ) fossil_free(zBranchName);


  @ <tr><td colspan="2">
  @ <input type="submit" name="cancel" value="Cancel">
  @ <input type="submit" name="preview" value="Preview">
  if( P("preview") ){
    @ <input type="submit" name="apply" value="Apply Changes">
  }
  @ </td></tr>
  @ </table>
  @ </div></form>
  builtin_request_js("ci_edit.js");
  style_finish_page();
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

#define AMEND_USAGE_STMT "HASH OPTION ?OPTION ...?"
/*
** COMMAND: amend
**
** Usage: %fossil amend HASH OPTION ?OPTION ...?
**
** Amend the tags on check-in HASH to change how it displays in the timeline.
**
** Options:
**    --author USER              Make USER the author for check-in
**    --bgcolor COLOR            Apply COLOR to this check-in
**    --branch NAME              Rename branch of check-in to NAME
**    --branchcolor COLOR        Apply and propagate COLOR to the branch
**    --cancel TAG               Cancel TAG from this check-in
**    --close                    Mark this "leaf" as closed
**    --date DATETIME            Make DATETIME the check-in time
**    --date-override DATETIME   Set the change time on the control artifact
**    -e|--edit-comment          Launch editor to revise comment
**    --hide                     Hide branch starting from this check-in
**    -m|--comment COMMENT       Make COMMENT the check-in comment
**    -M|--message-file FILE     Read the amended comment from FILE
**    -n|--dry-run               Print control artifact, but make no changes
**    --no-verify-comment        Do not validate the check-in comment
**    --tag TAG                  Add new TAG to this check-in
**    --user-override USER       Set the user name on the control artifact
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
  int fDryRun;                  /* Print control artifact, make no changes */
  int noVerifyCom = 0;          /* Allow suspicious check-in comments */
  const char *zChngTime;        /* The change time on the control artifact */
  const char *zUserOvrd;        /* The user name on the control artifact */
  const char *zUuid;
  Blob ctrl;
  Blob comment;
  char *zNow;
  int nTags, nCancels;
  int i;
  Stmt q;
  int ckComFlgs;                /* Flags passed to verify_comment() */


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
  fDryRun = find_option("dry-run","n",0)!=0;
  zChngTime = find_option("date-override",0,1);
  if( zChngTime==0 ) zChngTime = find_option("chngtime",0,1);
  zUserOvrd = find_option("user-override",0,1);
  noVerifyCom = find_option("no-verify-comment",0,0)!=0;
  db_find_and_open_repository(0,0);
  user_select();
  verify_all_options();
  if( g.argc<3 || g.argc>=4 ) usage(AMEND_USAGE_STMT);
  rid = name_to_typed_rid(g.argv[2], "ci");
  if( rid==0 && !is_a_version(rid) ) fossil_fatal("no such check-in");
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( zUuid==0 ) fossil_fatal("Unable to find artifact hash");
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
  if( fEditComment || zNewComment || zComFile ){
    blob_init(&comment, 0, 0);

    /* Figure out how much comment verification is requested */
    if( noVerifyCom ){
      ckComFlgs = 0;
    }else{
      const char *zVerComs = db_get("verify-comments","on");
      if( is_false(zVerComs) ){
        ckComFlgs = 0;
      }else if( strcmp(zVerComs,"preview")==0 ){
        ckComFlgs = COMCK_PREVIEW | COMCK_MARKUP;
      }else{
        ckComFlgs = COMCK_MARKUP;
      }
    }
    if( fEditComment ){
      prepare_amend_comment(&comment, zComment, zUuid);
    }else if( zComFile ){
      blob_read_from_file(&comment, zComFile, ExtFILE);
      blob_to_utf8_no_bom(&comment, 1);
    }else if( zNewComment ){
      blob_init(&comment, zNewComment, -1);
    }
    if( blob_size(&comment)>0
     && comment_compare(zComment, blob_str(&comment))==0
    ){
      int rc;
      while( (rc = verify_comment(&comment, ckComFlgs))!=0 ){
        char cReply;
        Blob ans;
        if( !fEditComment ){
          fossil_fatal("Amend aborted; "
                       "use --no-verify-comment to override");
        }
        if( rc==COMCK_PREVIEW ){
          prompt_user("Continue, abort, or edit (C/a/e)? ", &ans);
        }else{
          prompt_user("Edit, abort, or continue (E/a/c)? ", &ans);
        }
        cReply = blob_str(&ans)[0];
        cReply = fossil_tolower(cReply);
        blob_reset(&ans);
        if( cReply=='a' ){
          fossil_fatal("Amend aborted.");
        }
        if( cReply=='e' || (cReply!='c' && rc!=COMCK_PREVIEW) ){
          char *zPrior = blob_materialize(&comment);
          blob_init(&comment, 0, 0);
          prepare_amend_comment(&comment, zPrior, zUuid);
          fossil_free(zPrior);
          continue;
        }else{
          break;
        }
      }
    }
    add_comment(blob_str(&comment));
  }
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
  apply_newtags(&ctrl, rid, zUuid, zUserOvrd, fDryRun);
  if( fDryRun==0 ){
    show_common_info(rid, "hash:", 1, 0);
  }
  if( g.localOpen ){
    manifest_to_disk(rid);
  }
}


/*
** COMMAND: test-symlink-list
**
** Show all symlinks that have been checked into a Fossil repository.
**
** This command does a linear scan through all check-ins and so might take
** several seconds on a large repository.
*/
void test_symlink_list_cmd(void){
  Stmt q;
  db_find_and_open_repository(0,0);
  add_content_sql_commands(g.db);
  db_prepare(&q,
     "SELECT min(date(e.mtime)),"
           " b.uuid,"
           " f.filename,"
           " content(f.uuid)"
     " FROM event AS e, blob AS b, files_of_checkin(b.uuid) AS f"
     " WHERE e.type='ci'"
     "   AND b.rid=e.objid"
     "   AND f.perm LIKE '%%l%%'"
     " GROUP BY 3, 4"
     " ORDER BY 1 DESC"
  );
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%s %.16s %s -> %s\n",
      db_column_text(&q,0),
      db_column_text(&q,1),
      db_column_text(&q,2),
      db_column_text(&q,3));
  }
  db_finalize(&q);
}

#if INTERFACE
/*
** Description of a check-in relative to an earlier, tagged check-in.
*/
typedef struct CommitDescr {
  char *zRelTagname;        /* Tag name on the relative check-in */
  int nCommitsSince;        /* Number of commits since then */
  char *zCommitHash;        /* Hash of the described check-in */
  int isDirty;              /* Working directory has uncommitted changes */
} CommitDescr;
#endif

/*
** Describe the check-in given by 'zName', and possibly matching 'matchGlob',
** relative to an earlier, tagged check-in. Use 'descr' for the output.
**
** Finds the closest ancestor (ignoring merge-ins) that has a non-propagating
** label tag and the number of steps backwards that we had to search in
** order to find that tag.  Tags applied to more than one check-in are ignored.
**
** Return values:
**       0: ok
**      -1: zName does not resolve to a commit
**      -2: zName resolves to more than a commit
**      -3: no ancestor commit with a fitting non-propagating tag found
*/
int describe_commit(
  const char *zName,       /* Name of the commit to be described */
  const char *matchGlob,   /* Glob pattern for the tag */
  CommitDescr *descr       /* Write the description here */
){
  int rid;             /* rid for zName */
  const char *zUuid;   /* Hash of rid */
  int nRet = 0;        /* Value to be returned */
  Stmt q;              /* Query for tagged ancestors */

  rid = symbolic_name_to_rid(zName, "ci"); /* only commits */

  if( rid<=0 ){
    /* Commit does not exist or is ambiguous */
    descr->zRelTagname = mprintf("");
    descr->nCommitsSince = -1;
    descr->zCommitHash = mprintf("");
    descr->isDirty = -1;
    return (rid-1);
  }

  zUuid = rid_to_uuid(rid);
  descr->zCommitHash = mprintf("%s", zUuid);
  descr->isDirty = unsaved_changes(0);

  db_multi_exec(
    "DROP TABLE IF EXISTS temp.singletonTag;"
    "CREATE TEMP TABLE singletonTag("
    "  rid INT,"
    "  tagname TEXT,"
    "  PRIMARY KEY (rid,tagname)"
    ") WITHOUT ROWID;"
    "INSERT OR IGNORE INTO singletonTag(rid, tagname)"
    "  SELECT min(rid),"
    "         substr(tagname,5)"
    "    FROM tag, tagxref"
    "   WHERE tag.tagid=tagxref.tagid"
    "     AND tagxref.tagtype=1"
    "     AND tagname GLOB 'sym-%q'"
    "   GROUP BY tagname"
    "  HAVING count(*)==1;",
    (matchGlob ? matchGlob : "*")
  );

  db_prepare(&q,
    "WITH RECURSIVE"
    "  ancestor(rid,mtime,tagname,n) AS ("
    "    SELECT %d, event.mtime, singletonTag.tagname, 0 "
    "      FROM event"
    "      LEFT JOIN singletonTag ON singletonTag.rid=event.objid"
    "     WHERE event.objid=%d"
    "     UNION ALL"
    "     SELECT plink.pid, event.mtime, singletonTag.tagname, n+1"
    "       FROM ancestor, plink, event"
    "       LEFT JOIN singletonTag ON singletonTag.rid=plink.pid"
    "      WHERE plink.cid=ancestor.rid"
    "        AND plink.isprim=1"
    "        AND event.objid=plink.pid"
    "        AND ancestor.tagname IS NULL"
    "      ORDER BY mtime DESC"
    "      LIMIT 100000"
    "  )"
    "SELECT tagname, n"
    "  FROM ancestor"
    " WHERE tagname IS NOT NULL"
    " ORDER BY n LIMIT 1;",
    rid, rid
  );

  if( db_step(&q)==SQLITE_ROW ){
    const char *lastTag = db_column_text(&q, 0);
    descr->zRelTagname = mprintf("%s", lastTag);
    descr->nCommitsSince = db_column_int(&q, 1);
    nRet = 0;
  }else{
    /* no ancestor commit with a fitting singleton tag found */
    descr->zRelTagname = mprintf("");
    descr->nCommitsSince = -1;
    nRet = -3;
  }

  db_finalize(&q);
  return nRet;
}

/*
** COMMAND: describe
**
** Usage: %fossil describe ?VERSION? ?OPTIONS?
**
** Provide a description of the given VERSION by showing a non-propagating
** tag of the youngest tagged ancestor, followed by the number of commits
** since that, and the short hash of VERSION.  Only tags applied to a single
** check-in are considered.
**
** If no VERSION is provided, describe the currently checked-out version.
**
** If VERSION and the found ancestor refer to the same commit, the last two
** components are omitted, unless --long is provided.  When no fitting tagged
** ancestor is found, show only the short hash of VERSION.
**
** Options:
**    --digits           Display so many hex digits of the hash
**                       (default: the larger of 6 and the 'hash-digit' setting)
**    -d|--dirty         Show whether there are changes to be committed
**    --long             Always show all three components
**    --match GLOB       Consider only non-propagating tags matching GLOB
*/
void describe_cmd(void){
  const char *zName;
  const char *zMatchGlob;
  const char *zDigits;
  int nDigits;
  int bDirtyFlag = 0;
  int bLongFlag = 0;
  CommitDescr descr;

  db_find_and_open_repository(0,0);
  bDirtyFlag = find_option("dirty","d",0)!=0;
  bLongFlag = find_option("long","",0)!=0;
  zMatchGlob = find_option("match", 0, 1);
  zDigits = find_option("digits", 0, 1);

  if ( !zDigits || ((nDigits=atoi(zDigits))==0) ){
    nDigits = hash_digits(0);
  }

  /* We should be done with options.. */
  verify_all_options();
  if( g.argc<3 ){
    zName = "current";
  }else{
    zName = g.argv[2];
  }

  if( bDirtyFlag ){
    if ( g.argc>=3 ) fossil_fatal("cannot use --dirty with specific check-in");
  }

  switch( describe_commit(zName, zMatchGlob, &descr) ){
    case -1:
      fossil_fatal("commit %s does not exist", zName);
      break;
    case -2:
      fossil_fatal("commit %s is ambiguous", zName);
      break;
    case -3:
      fossil_print("%.*s%s\n", nDigits, descr.zCommitHash,
                  bDirtyFlag ? (descr.isDirty ? "-dirty" : "") : "");
      break;
    case 0:
      if( descr.nCommitsSince==0 && !bLongFlag ){
        fossil_print("%s%s\n", descr.zRelTagname,
                    bDirtyFlag ? (descr.isDirty ? "-dirty" : "") : "");
      }else{
        fossil_print("%s-%d-%.*s%s\n", descr.zRelTagname,
                    descr.nCommitsSince, nDigits, descr.zCommitHash,
                    bDirtyFlag ? (descr.isDirty ? "-dirty" : "") : "");
      }
      break;
    default:
      fossil_fatal("cannot describe commit");
      break;
  }
}
