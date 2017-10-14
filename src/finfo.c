/*
** Copyright (c) 2009 D. Richard Hipp
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
** This file contains code to implement the "finfo" command.
*/
#include "config.h"
#include "finfo.h"

/*
** COMMAND: finfo
**
** Usage: %fossil finfo ?OPTIONS? FILENAME
**
** Print the complete change history for a single file going backwards
** in time.  The default mode is -l.
**
** For the -l|--log mode: If "-b|--brief" is specified one line per revision
** is printed, otherwise the full comment is printed.  The "-n|--limit N"
** and "--offset P" options limits the output to the first N changes
** after skipping P changes.
**
** In the -s mode prints the status as <status> <revision>.  This is
** a quick status and does not check for up-to-date-ness of the file.
**
** In the -p mode, there's an optional flag "-r|--revision REVISION".
** The specified version (or the latest checked out version) is printed
** to stdout.  The -p mode is another form of the "cat" command.
**
** Options:
**   -b|--brief           display a brief (one line / revision) summary
**   --case-sensitive B   Enable or disable case-sensitive filenames.  B is a
**                        boolean: "yes", "no", "true", "false", etc.
**   -l|--log             select log mode (the default)
**   -n|--limit N         Display the first N changes (default unlimited).
**                        N<=0 means no limit.
**   --offset P           skip P changes
**   -p|--print           select print mode
**   -r|--revision R      print the given revision (or ckout, if none is given)
**                        to stdout (only in print mode)
**   -s|--status          select status mode (print a status indicator for FILE)
**   -W|--width <num>     Width of lines (default is to auto-detect). Must be
**                        >22 or 0 (= no limit, resulting in a single line per
**                        entry).
**
** See also: artifact, cat, descendants, info, leaves
*/
void finfo_cmd(void){
  db_must_be_within_tree();
  if( find_option("status","s",0) ){
    Stmt q;
    Blob line;
    Blob fname;
    int vid;

    /* We should be done with options.. */
    verify_all_options();

    if( g.argc!=3 ) usage("-s|--status FILENAME");
    vid = db_lget_int("checkout", 0);
    if( vid==0 ){
      fossil_fatal("no checkout to finfo files in");
    }
    vfile_check_signature(vid, CKSIG_ENOTFILE);
    file_tree_name(g.argv[2], &fname, 0, 1);
    db_prepare(&q,
        "SELECT pathname, deleted, rid, chnged, coalesce(origname!=pathname,0)"
        "  FROM vfile WHERE vfile.pathname=%B %s",
        &fname, filename_collation());
    blob_zero(&line);
    if( db_step(&q)==SQLITE_ROW ) {
      Blob uuid;
      int isDeleted = db_column_int(&q, 1);
      int isNew = db_column_int(&q,2) == 0;
      int chnged = db_column_int(&q,3);
      int renamed = db_column_int(&q,4);

      blob_zero(&uuid);
      db_blob(&uuid,
           "SELECT uuid FROM blob, mlink, vfile WHERE "
           "blob.rid = mlink.mid AND mlink.fid = vfile.rid AND "
           "vfile.pathname=%B %s",
           &fname, filename_collation()
      );
      if( isNew ){
        blob_appendf(&line, "new");
      }else if( isDeleted ){
        blob_appendf(&line, "deleted");
      }else if( renamed ){
        blob_appendf(&line, "renamed");
      }else if( chnged ){
        blob_appendf(&line, "edited");
      }else{
        blob_appendf(&line, "unchanged");
      }
      blob_appendf(&line, " ");
      blob_appendf(&line, " %10.10s", blob_str(&uuid));
      blob_reset(&uuid);
    }else{
      blob_appendf(&line, "unknown 0000000000");
    }
    db_finalize(&q);
    fossil_print("%s\n", blob_str(&line));
    blob_reset(&fname);
    blob_reset(&line);
  }else if( find_option("print","p",0) ){
    Blob record;
    Blob fname;
    const char *zRevision = find_option("revision", "r", 1);

    /* We should be done with options.. */
    verify_all_options();

    file_tree_name(g.argv[2], &fname, 0, 1);
    if( zRevision ){
      historical_blob(zRevision, blob_str(&fname), &record, 1);
    }else{
      int rid = db_int(0, "SELECT rid FROM vfile WHERE pathname=%B %s",
                       &fname, filename_collation());
      if( rid==0 ){
        fossil_fatal("no history for file: %b", &fname);
      }
      content_get(rid, &record);
    }
    blob_write_to_file(&record, "-");
    blob_reset(&record);
    blob_reset(&fname);
  }else{
    Blob line;
    Stmt q;
    Blob fname;
    int rid;
    const char *zFilename;
    const char *zLimit;
    const char *zWidth;
    const char *zOffset;
    int iLimit, iOffset, iBrief, iWidth;

    if( find_option("log","l",0) ){
      /* this is the default, no-op */
    }
    zLimit = find_option("limit","n",1);
    zWidth = find_option("width","W",1);
    iLimit = zLimit ? atoi(zLimit) : -1;
    zOffset = find_option("offset",0,1);
    iOffset = zOffset ? atoi(zOffset) : 0;
    iBrief = (find_option("brief","b",0) == 0);
    if( iLimit==0 ){
      iLimit = -1;
    }
    if( zWidth ){
      iWidth = atoi(zWidth);
      if( (iWidth!=0) && (iWidth<=22) ){
        fossil_fatal("-W|--width value must be >22 or 0");
      }
    }else{
      iWidth = -1;
    }

    /* We should be done with options.. */
    verify_all_options();

    if( g.argc!=3 ){
      usage("?-l|--log? ?-b|--brief? FILENAME");
    }
    file_tree_name(g.argv[2], &fname, 0, 1);
    rid = db_int(0, "SELECT rid FROM vfile WHERE pathname=%B %s",
                 &fname, filename_collation());
    if( rid==0 ){
      fossil_fatal("no history for file: %b", &fname);
    }
    zFilename = blob_str(&fname);
    db_prepare(&q,
        "SELECT DISTINCT b.uuid, ci.uuid, date(event.mtime,toLocal()),"
        "       coalesce(event.ecomment, event.comment),"
        "       coalesce(event.euser, event.user),"
        "       (SELECT value FROM tagxref WHERE tagid=%d AND tagtype>0"
                                " AND tagxref.rid=mlink.mid)" /* Tags */
        "  FROM mlink, blob b, event, blob ci, filename"
        " WHERE filename.name=%Q %s"
        "   AND mlink.fnid=filename.fnid"
        "   AND b.rid=mlink.fid"
        "   AND event.objid=mlink.mid"
        "   AND event.objid=ci.rid"
        " ORDER BY event.mtime DESC LIMIT %d OFFSET %d",
        TAG_BRANCH, zFilename, filename_collation(),
        iLimit, iOffset
    );
    blob_zero(&line);
    if( iBrief ){
      fossil_print("History of %s\n", blob_str(&fname));
    }
    while( db_step(&q)==SQLITE_ROW ){
      const char *zFileUuid = db_column_text(&q, 0);
      const char *zCiUuid = db_column_text(&q,1);
      const char *zDate = db_column_text(&q, 2);
      const char *zCom = db_column_text(&q, 3);
      const char *zUser = db_column_text(&q, 4);
      const char *zBr = db_column_text(&q, 5);
      char *zOut;
      if( zBr==0 ) zBr = "trunk";
      if( iBrief ){
        fossil_print("%s ", zDate);
        zOut = mprintf(
           "[%S] %s (user: %s, artifact: [%S], branch: %s)",
           zCiUuid, zCom, zUser, zFileUuid, zBr);
        comment_print(zOut, zCom, 11, iWidth, g.comFmtFlags);
        fossil_free(zOut);
      }else{
        blob_reset(&line);
        blob_appendf(&line, "%S ", zCiUuid);
        blob_appendf(&line, "%.10s ", zDate);
        blob_appendf(&line, "%8.8s ", zUser);
        blob_appendf(&line, "%8.8s ", zBr);
        blob_appendf(&line,"%-39.39s", zCom );
        comment_print(blob_str(&line), zCom, 0, iWidth, g.comFmtFlags);
      }
    }
    db_finalize(&q);
    blob_reset(&fname);
  }
}

/*
** COMMAND: cat
**
** Usage: %fossil cat FILENAME ... ?OPTIONS?
**
** Print on standard output the content of one or more files as they exist
** in the repository.  The version currently checked out is shown by default.
** Other versions may be specified using the -r option.
**
** Options:
**    -R|--repository FILE       Extract artifacts from repository FILE
**    -r VERSION                 The specific check-in containing the file
**
** See also: finfo
*/
void cat_cmd(void){
  int i;
  Blob content, fname;
  const char *zRev;
  db_find_and_open_repository(0, 0);
  zRev = find_option("r","r",1);

  /* We should be done with options.. */
  verify_all_options();

  for(i=2; i<g.argc; i++){
    file_tree_name(g.argv[i], &fname, 0, 1);
    blob_zero(&content);
    historical_blob(zRev, blob_str(&fname), &content, 1);
    blob_write_to_file(&content, "-");
    blob_reset(&fname);
    blob_reset(&content);
  }
}

/* Values for the debug= query parameter to finfo */
#define FINFO_DEBUG_MLINK  0x01

/*
** WEBPAGE: finfo
** URL: /finfo?name=FILENAME
**
** Show the change history for a single file.
**
** Additional query parameters:
**
**    a=DATETIME Only show changes after DATETIME
**    b=DATETIME Only show changes before DATETIME
**    n=NUM      Show the first NUM changes only
**    brbg       Background color by branch name
**    ubg        Background color by user name
**    ci=UUID    Ancestors of a particular check-in
**    orig=UUID  If both ci and orig are supplied, only show those
**                 changes on a direct path from orig to ci.
**    showid     Show RID values for debugging
**
** DATETIME may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
** year-month-day form, it may be truncated, and it may also name a
** timezone offset from UTC as "-HH:MM" (westward) or "+HH:MM"
** (eastward). Either no timezone suffix or "Z" means UTC.
*/
void finfo_page(void){
  Stmt q;
  const char *zFilename;
  char zPrevDate[20];
  const char *zA;
  const char *zB;
  int n;
  int baseCheckin;
  int origCheckin = 0;
  int fnid;
  Blob title;
  Blob sql;
  HQuery url;
  GraphContext *pGraph;
  int brBg = P("brbg")!=0;
  int uBg = P("ubg")!=0;
  int fDebug = atoi(PD("debug","0"));
  int fShowId = P("showid")!=0;
  Stmt qparent;
  int iTableId = timeline_tableid();

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_header("File History");
  login_anonymous_available();
  url_initialize(&url, "finfo");
  if( brBg ) url_add_parameter(&url, "brbg", 0);
  if( uBg ) url_add_parameter(&url, "ubg", 0);
  baseCheckin = name_to_rid_www("ci");
  zPrevDate[0] = 0;
  zFilename = PD("name","");
  fnid = db_int(0, "SELECT fnid FROM filename WHERE name=%Q", zFilename);
  if( fnid==0 ){
    @ No such file: %h(zFilename)
    style_footer();
    return;
  }
  if( g.perm.Admin ){
    style_submenu_element("MLink Table", "%R/mlink?name=%t", zFilename);
  }
  if( baseCheckin ){
    if( P("orig")!=0 ){
      origCheckin = name_to_typed_rid(P("orig"),"ci");
      path_shortest_stored_in_ancestor_table(origCheckin, baseCheckin);
    }else{
      compute_direct_ancestors(baseCheckin);
    }
  }
  url_add_parameter(&url, "name", zFilename);
  blob_zero(&sql);
  blob_append_sql(&sql,
    "SELECT"
    " datetime(min(event.mtime),toLocal()),"         /* Date of change */
    " coalesce(event.ecomment, event.comment),"      /* Check-in comment */
    " coalesce(event.euser, event.user),"            /* User who made chng */
    " mlink.pid,"                                    /* Parent file rid */
    " mlink.fid,"                                    /* File rid */
    " (SELECT uuid FROM blob WHERE rid=mlink.pid),"  /* Parent file uuid */
    " blob.uuid,"                                    /* Current file uuid */
    " (SELECT uuid FROM blob WHERE rid=mlink.mid),"  /* Check-in uuid */
    " event.bgcolor,"                                /* Background color */
    " (SELECT value FROM tagxref WHERE tagid=%d AND tagtype>0"
                                " AND tagxref.rid=mlink.mid)," /* Branchname */
    " mlink.mid,"                                    /* check-in ID */
    " mlink.pfnid,"                                  /* Previous filename */
    " blob.size"                                     /* File size */
    "  FROM mlink, event, blob"
    " WHERE mlink.fnid=%d"
    "   AND event.objid=mlink.mid"
    "   AND mlink.fid=blob.rid",
    TAG_BRANCH, fnid
  );
  if( (zA = P("a"))!=0 ){
    blob_append_sql(&sql, " AND event.mtime>=julianday('%q')", zA);
    url_add_parameter(&url, "a", zA);
  }
  if( (zB = P("b"))!=0 ){
    blob_append_sql(&sql, " AND event.mtime<=julianday('%q')", zB);
    url_add_parameter(&url, "b", zB);
  }
  if( baseCheckin ){
    blob_append_sql(&sql,
      " AND mlink.mid IN (SELECT rid FROM ancestor)"
      " GROUP BY mlink.fid"
    );
  }else{
    /* We only want each version of a file to appear on the graph once,
    ** at its earliest appearance.  All the other times that it gets merged
    ** into this or that branch can be ignored.  An exception is for when
    ** files are deleted (when they have mlink.fid==0).  If the same file
    ** is deleted in multiple places, we want to show each deletion, so
    ** use a "fake fid" which is derived from the parent-fid for grouping.
    ** The same fake-fid must be used on the graph.
    */
    blob_append_sql(&sql,
      " GROUP BY"
      "   CASE WHEN mlink.fid>0 THEN mlink.fid ELSE mlink.pid+1000000000 END"
    );
  }
  blob_append_sql(&sql, " ORDER BY event.mtime DESC /*sort*/");
  if( (n = atoi(PD("n","0")))>0 ){
    blob_append_sql(&sql, " LIMIT %d", n);
    url_add_parameter(&url, "n", P("n"));
  }
  db_prepare(&q, "%s", blob_sql_text(&sql));
  if( P("showsql")!=0 ){
    @ <p>SQL: %h(blob_str(&sql))</p>
  }
  blob_reset(&sql);
  blob_zero(&title);
  if( baseCheckin ){
    char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", baseCheckin);
    char *zLink = href("%R/info/%!S", zUuid);
    if( origCheckin ){
      blob_appendf(&title, "Changes to file ");
    }else if( n>0 ){
      blob_appendf(&title, "First %d ancestors of file ", n);
    }else{
      blob_appendf(&title, "Ancestors of file ");
    }
    blob_appendf(&title,"<a href='%R/finfo?name=%T'>%h</a>",
                 zFilename, zFilename);
    if( fShowId ) blob_appendf(&title, " (%d)", fnid);
    blob_append(&title, origCheckin ? " between " : " from ", -1);
    blob_appendf(&title, "check-in %z%S</a>", zLink, zUuid);
    if( fShowId ) blob_appendf(&title, " (%d)", baseCheckin);
    fossil_free(zUuid);
    if( origCheckin ){
      zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", origCheckin);
      zLink = href("%R/info/%!S", zUuid);
      blob_appendf(&title, " and check-in %z%S</a>", zLink, zUuid);
      fossil_free(zUuid);
    }
  }else{
    blob_appendf(&title, "History of ");
    hyperlinked_path(zFilename, &title, 0, "tree", "");
    if( fShowId ) blob_appendf(&title, " (%d)", fnid);
  }
  @ <h2>%b(&title)</h2>
  blob_reset(&title);
  pGraph = graph_init();
  @ <table id="timelineTable%d(iTableId)" class="timelineTable">
  if( baseCheckin ){
    db_prepare(&qparent,
      "SELECT DISTINCT pid FROM mlink"
      " WHERE fid=:fid AND mid=:mid AND pid>0 AND fnid=:fnid"
      "   AND pmid IN (SELECT rid FROM ancestor)"
      " ORDER BY isaux /*sort*/"
    );
  }else{
    db_prepare(&qparent,
      "SELECT DISTINCT pid FROM mlink"
      " WHERE fid=:fid AND mid=:mid AND pid>0 AND fnid=:fnid"
      " ORDER BY isaux /*sort*/"
    );
  }
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 0);
    const char *zCom = db_column_text(&q, 1);
    const char *zUser = db_column_text(&q, 2);
    int fpid = db_column_int(&q, 3);
    int frid = db_column_int(&q, 4);
    const char *zPUuid = db_column_text(&q, 5);
    const char *zUuid = db_column_text(&q, 6);
    const char *zCkin = db_column_text(&q,7);
    const char *zBgClr = db_column_text(&q, 8);
    const char *zBr = db_column_text(&q, 9);
    int fmid = db_column_int(&q, 10);
    int pfnid = db_column_int(&q, 11);
    int szFile = db_column_int(&q, 12);
    int gidx;
    char zTime[10];
    int nParent = 0;
    int aParent[GR_MAX_RAIL];

    db_bind_int(&qparent, ":fid", frid);
    db_bind_int(&qparent, ":mid", fmid);
    db_bind_int(&qparent, ":fnid", fnid);
    while( db_step(&qparent)==SQLITE_ROW && nParent<count(aParent) ){
      aParent[nParent] = db_column_int(&qparent, 0);
      nParent++;
    }
    db_reset(&qparent);
    if( zBr==0 ) zBr = "trunk";
    if( uBg ){
      zBgClr = hash_color(zUser);
    }else if( brBg || zBgClr==0 || zBgClr[0]==0 ){
      zBgClr = strcmp(zBr,"trunk")==0 ? "" : hash_color(zBr);
    }
    gidx = graph_add_row(pGraph, frid>0 ? frid : fpid+1000000000,
                         nParent, aParent, zBr, zBgClr,
                         zUuid, 0);
    if( strncmp(zDate, zPrevDate, 10) ){
      sqlite3_snprintf(sizeof(zPrevDate), zPrevDate, "%.10s", zDate);
      @ <tr><td>
      @   <div class="divider timelineDate">%s(zPrevDate)</div>
      @ </td><td></td><td></td></tr>
    }
    memcpy(zTime, &zDate[11], 5);
    zTime[5] = 0;
    @ <tr><td class="timelineTime">
    @ %z(href("%R/timeline?c=%t",zDate))%s(zTime)</a></td>
    @ <td class="timelineGraph"><div id="m%d(gidx)" class="tl-nodemark"></div>
    @ </td>
    if( zBgClr && zBgClr[0] ){
      @ <td class="timelineTableCell" style="background-color: %h(zBgClr);">
    }else{
      @ <td class="timelineTableCell">
    }
    if( zUuid ){
      if( origCheckin==0 ){
        if( nParent==0 ){
          @ <b>Added</b>
        }else if( pfnid ){
          char *zPrevName = db_text(0,"SELECT name FROM filename WHERE fnid=%d",
                                    pfnid);
          @ <b>Renamed</b> from
          @ %z(href("%R/finfo?name=%t", zPrevName))%h(zPrevName)</a>
        }
      }
      @ %z(href("%R/artifact/%!S",zUuid))[%S(zUuid)]</a>
      if( fShowId ){
        int srcId = delta_source_rid(frid);
        if( srcId>0 ){
          @ (%d(frid)&larr;%d(srcId))
        }else{
          @ (%d(frid))
        }
      }
      @ part of check-in
    }else{
      char *zNewName;
      zNewName = db_text(0,
        "SELECT name FROM filename WHERE fnid = "
        "   (SELECT fnid FROM mlink"
        "     WHERE mid=%d"
        "       AND pfnid IN (SELECT fnid FROM filename WHERE name=%Q))",
        fmid, zFilename);
      if( zNewName ){
        @ <b>Renamed</b> to
        @ %z(href("%R/finfo?name=%t",zNewName))%h(zNewName)</a> by check-in
        fossil_free(zNewName);
      }else{
        @ <b>Deleted</b> by check-in
      }
    }
    hyperlink_to_uuid(zCkin);
    if( fShowId ){
      @ (%d(fmid))
    }
    @ %W(zCom) (user:
    hyperlink_to_user(zUser, zDate, ",");
    @ branch: %z(href("%R/timeline?t=%T&n=200",zBr))%h(zBr)</a>,
    @ size: %d(szFile))
    if( g.perm.Hyperlink && zUuid ){
      const char *z = zFilename;
      @ %z(href("%R/annotate?filename=%h&checkin=%s",z,zCkin))
      @ [annotate]</a>
      @ %z(href("%R/blame?filename=%h&checkin=%s",z,zCkin))
      @ [blame]</a>
      @ %z(href("%R/timeline?n=200&uf=%!S",zUuid))[check-ins&nbsp;using]</a>
      if( fpid>0 ){
        @ %z(href("%R/fdiff?sbs=1&v1=%!S&v2=%!S",zPUuid,zUuid))[diff]</a>
      }
    }
    if( fDebug & FINFO_DEBUG_MLINK ){
      int ii;
      char *zAncLink;
      @ <br />fid=%d(frid) pid=%d(fpid) mid=%d(fmid)
      if( nParent>0 ){
        @ parents=%d(aParent[0])
        for(ii=1; ii<nParent; ii++){
          @ %d(aParent[ii])
        }
      }
      zAncLink = href("%R/finfo?name=%T&ci=%!S&debug=1",zFilename,zCkin);
      @ %z(zAncLink)[ancestry]</a>
    }
    tag_private_status(frid);
    @ </td></tr>
  }
  db_finalize(&q);
  db_finalize(&qparent);
  if( pGraph ){
    graph_finish(pGraph, 1);
    if( pGraph->nErr ){
      graph_free(pGraph);
      pGraph = 0;
    }else{
      @ <tr class="timelineBottom"><td></td><td></td><td></td></tr>
    }
  }
  @ </table>
  timeline_output_graph_javascript(pGraph, 0, iTableId, 1);
  style_footer();
}

/*
** WEBPAGE: mlink
** URL: /mlink?name=FILENAME
** URL: /mlink?ci=NAME
**
** Show all MLINK table entries for a particular file, or for
** a particular check-in.
**
** This screen is intended for use by Fossil developers to help
** in debugging Fossil itself.  Ordinary Fossil users are not 
** expected to know what the MLINK table is or why it is important.
**
** To avoid confusing ordinary users, this page is only available
** to adminstrators.
*/
void mlink_page(void){
  const char *zFName = P("name");
  const char *zCI = P("ci");
  Stmt q;

  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(g.anon.Admin); return; }
  style_header("MLINK Table");
  if( zFName==0 && zCI==0 ){
    @ <span class='generalError'>
    @ Requires either a name= or ci= query parameter
    @ </span>
  }else if( zFName ){
    int fnid = db_int(0,"SELECT fnid FROM filename WHERE name=%Q",zFName);
    if( fnid<=0 ) fossil_fatal("no such file: \"%s\"", zFName);
    db_prepare(&q,
       "SELECT"
       /* 0 */ "  datetime(event.mtime,toLocal()),"
       /* 1 */ "  (SELECT uuid FROM blob WHERE rid=mlink.mid),"
       /* 2 */ "  (SELECT uuid FROM blob WHERE rid=mlink.pmid),"
       /* 3 */ "  isaux,"
       /* 4 */ "  (SELECT uuid FROM blob WHERE rid=mlink.fid),"
       /* 5 */ "  (SELECT uuid FROM blob WHERE rid=mlink.pid),"
       /* 6 */ "  mlink.pid,"
       /* 7 */ "  mperm,"
       /* 8 */ "  (SELECT name FROM filename WHERE fnid=mlink.pfnid)"
       "  FROM mlink, event"
       " WHERE mlink.fnid=%d"
       "   AND event.objid=mlink.mid"
       " ORDER BY 1 DESC",
       fnid
    );
    @ <h1>MLINK table for file
    @ <a href='%R/finfo?name=%t(zFName)'>%h(zFName)</a></h1>
    @ <div class='brlist'>
    @ <table id='mlinktable'>
    @ <thead><tr>
    @ <th>Date</th>
    @ <th>Check-in</th>
    @ <th>Parent<br>Check-in</th>
    @ <th>Merge?</th>
    @ <th>New</th>
    @ <th>Old</th>
    @ <th>Exe<br>Bit?</th>
    @ <th>Prior<br>Name</th>
    @ </tr></thead>
    @ <tbody>
    while( db_step(&q)==SQLITE_ROW ){
      const char *zDate = db_column_text(&q,0);
      const char *zCkin = db_column_text(&q,1);
      const char *zParent = db_column_text(&q,2);
      int isMerge = db_column_int(&q,3);
      const char *zFid = db_column_text(&q,4);
      const char *zPid = db_column_text(&q,5);
      int isExe = db_column_int(&q,7);
      const char *zPrior = db_column_text(&q,8);
      @ <tr>
      @ <td><a href='%R/timeline?c=%!S(zCkin)'>%s(zDate)</a></td>
      @ <td><a href='%R/info/%!S(zCkin)'>%S(zCkin)</a></td>
      if( zParent ){
        @ <td><a href='%R/info/%!S(zParent)'>%S(zParent)</a></td>
      }else{
        @ <td><i>(New)</i></td>
      }
      @ <td align='center'>%s(isMerge?"&#x2713;":"")</td>
      if( zFid ){
        @ <td><a href='%R/info/%!S(zFid)'>%S(zFid)</a></td>
      }else{
        @ <td><i>(Deleted)</i></td>
      }
      if( zPid ){
        @ <td><a href='%R/info/%!S(zPid)'>%S(zPid)</a>
      }else if( db_column_int(&q,6)<0 ){
        @ <td><i>(Added by merge)</i></td>
      }else{
        @ <td><i>(New)</i></td>
      }
      @ <td align='center'>%s(isExe?"&#x2713;":"")</td>
      if( zPrior ){
        @ <td><a href='%R/finfo?name=%t(zPrior)'>%h(zPrior)</a></td>
      }else{
        @ <td></td>
      }
      @ </tr>
    }
    db_finalize(&q);
    @ </tbody>
    @ </table>
    @ </div>
    output_table_sorting_javascript("mlinktable","tttxtttt",1);
  }else{
    int mid = name_to_rid_www("ci");
    db_prepare(&q,
       "SELECT"
       /* 0 */ "  (SELECT name FROM filename WHERE fnid=mlink.fnid),"
       /* 1 */ "  (SELECT uuid FROM blob WHERE rid=mlink.fid),"
       /* 2 */ "  pid,"
       /* 3 */ "  (SELECT uuid FROM blob WHERE rid=mlink.pid),"
       /* 4 */ "  (SELECT name FROM filename WHERE fnid=mlink.pfnid),"
       /* 5 */ "  (SELECT uuid FROM blob WHERE rid=mlink.pmid),"
       /* 6 */ "  mperm,"
       /* 7 */ "  isaux"
       "  FROM mlink WHERE mid=%d ORDER BY 1",
       mid
    );
    @ <h1>MLINK table for check-in %h(zCI)</h1>
    render_checkin_context(mid, 1);
    @ <hr />
    @ <div class='brlist'>
    @ <table id='mlinktable'>
    @ <thead><tr>
    @ <th>File</th>
    @ <th>Parent<br>Check-in</th>
    @ <th>Merge?</th>
    @ <th>New</th>
    @ <th>Old</th>
    @ <th>Exe<br>Bit?</th>
    @ <th>Prior<br>Name</th>
    @ </tr></thead>
    @ <tbody>
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q,0);
      const char *zFid = db_column_text(&q,1);
      const char *zPid = db_column_text(&q,3);
      const char *zPrior = db_column_text(&q,4);
      const char *zParent = db_column_text(&q,5);
      int isExec = db_column_int(&q,6);
      int isAux = db_column_int(&q,7);
      @ <tr>
      @ <td><a href='%R/finfo?name=%t(zName)'>%h(zName)</a></td>
      if( zParent ){
        @ <td><a href='%R/info/%!S(zParent)'>%S(zParent)</a></td>
      }else{
        @ <td><i>(New)</i></td>
      }
      @ <td align='center'>%s(isAux?"&#x2713;":"")</td>
      if( zFid ){
        @ <td><a href='%R/info/%!S(zFid)'>%S(zFid)</a></td>
      }else{
        @ <td><i>(Deleted)</i></td>
      }
      if( zPid ){
        @ <td><a href='%R/info/%!S(zPid)'>%S(zPid)</a>
      }else if( db_column_int(&q,2)<0 ){
        @ <td><i>(Added by merge)</i></td>
      }else{
        @ <td><i>(New)</i></td>
      }
      @ <td align='center'>%s(isExec?"&#x2713;":"")</td>
      if( zPrior ){
        @ <td><a href='%R/finfo?name=%t(zPrior)'>%h(zPrior)</a></td>
      }else{
        @ <td></td>
      }
      @ </tr>
    }
    db_finalize(&q);
    @ </tbody>
    @ </table>
    @ </div>
    output_table_sorting_javascript("mlinktable","ttxtttt",1);
  }
  style_footer();
}
