/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code to implement the "info" command.  The
** "info" command gives command-line access to information about
** the current tree, or a particular artifact or baseline.
*/
#include "config.h"
#include "info.h"
#include <assert.h>


/*
** Print common information about a particular record.
**
**     *  The UUID
**     *  The record ID
**     *  mtime and ctime
**     *  who signed it
*/
void show_common_info(int rid, const char *zUuidName, int showComment){
  Stmt q;
  char *zComment = 0;
  db_prepare(&q,
    "SELECT uuid"
    "  FROM blob WHERE rid=%d", rid
  );
  if( db_step(&q)==SQLITE_ROW ){
         /* 01234567890123 */
    printf("%-13s %s\n", zUuidName, db_column_text(&q, 0));
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid FROM plink JOIN blob ON pid=rid "
                 " WHERE cid=%d", rid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    printf("parent:       %s\n", zUuid);
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid FROM plink JOIN blob ON cid=rid "
                 " WHERE pid=%d", rid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    printf("child:        %s\n", zUuid);
  }
  db_finalize(&q);
  if( zComment ){
    printf("comment:\n%s\n", zComment);
    free(zComment);
  }
}


/*
** COMMAND: info
**
** Usage: %fossil info ?UUID?
**
** With no arguments, provide information about the current tree.
** If an argument is given, provide information about the record
** that the argument refers to.
*/
void info_cmd(void){
  if( g.argc!=2 && g.argc!=3 ){
    usage("?FILEID|UUID?");
  }
  db_must_be_within_tree();
  if( g.argc==2 ){
    int vid;
         /* 012345678901234 */
    printf("repository:   %s\n", db_lget("repository", ""));
    printf("local-root:   %s\n", g.zLocalRoot);
    printf("project-code: %s\n", db_get("project-code", ""));
    printf("server-code:  %s\n", db_get("server-code", ""));
    vid = db_lget_int("checkout", 0);
    if( vid==0 ){
      printf("checkout:     nil\n");
    }else{
      show_common_info(vid, "checkout:", 1);
    }
  }else{
    int rid;
    rid = name_to_rid(g.argv[2]);
    if( rid==0 ){
      fossil_panic("no such object: %s\n", g.argv[2]);
    }
    show_common_info(rid, "uuid:", 1);
  }
}

/*
** Show information about descendants of a baseline.  Do this recursively
** to a depth of N.  Return true if descendants are shown and false if not.
*/
static int showDescendants(int pid, int depth, const char *zTitle){
  Stmt q;
  int cnt = 0;
  db_prepare(&q,
    "SELECT plink.cid, blob.uuid, datetime(plink.mtime, 'localtime'),"
    "       coalesce(event.euser,event.user),"
    "       coalesce(event.ecomment,event.comment)"
    "  FROM plink, blob, event"
    " WHERE plink.pid=%d"
    "   AND blob.rid=plink.cid"
    "   AND event.objid=plink.cid"
    " ORDER BY plink.mtime ASC",
    pid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int n;
    int cid = db_column_int(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    const char *zCom = db_column_text(&q, 4);
    cnt++;
    if( cnt==1 ){
      if( zTitle ){
        @ <div class="section">%s(zTitle)</div>
      }
      @ <ul>
    }
    @ <li>
    hyperlink_to_uuid(zUuid);
    @ %w(zCom) (by %s(zUser) on %s(zDate))
    if( depth ){
      n = showDescendants(cid, depth-1, 0);
    }else{
      n = db_int(0, "SELECT 1 FROM plink WHERE pid=%d", cid);
    }
    if( n==0 ){
      db_multi_exec("DELETE FROM leaves WHERE rid=%d", cid);
      @ <b>leaf</b>
    }
  }
  db_finalize(&q);
  if( cnt ){
    @ </ul>
  }
  return cnt;
}

/*
** Show information about ancestors of a baseline.  Do this recursively
** to a depth of N.  Return true if ancestors are shown and false if not.
*/
static void showAncestors(int pid, int depth, const char *zTitle){
  Stmt q;
  int cnt = 0;
  db_prepare(&q,
    "SELECT plink.pid, blob.uuid, datetime(event.mtime, 'localtime'),"
    "       coalesce(event.euser,event.user),"
    "       coalesce(event.ecomment,event.comment)"
    "  FROM plink, blob, event"
    " WHERE plink.cid=%d"
    "   AND blob.rid=plink.pid"
    "   AND event.objid=plink.pid"
    " ORDER BY event.mtime DESC",
    pid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int cid = db_column_int(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    const char *zCom = db_column_text(&q, 4);
    cnt++;
    if( cnt==1 ){
      if( zTitle ){
        @ <div class="section">%s(zTitle)</div>
      }
      @ <ul>
    }
    @ <li>
    hyperlink_to_uuid(zUuid);
    @ %w(zCom) (by %s(zUser) on %s(zDate))
    if( depth ){
      showAncestors(cid, depth-1, 0);
    }
  }
  db_finalize(&q);
  if( cnt ){
    @ </ul>
  }
}


/*
** Show information about baselines mentioned in the "leaves" table.
*/
static void showLeaves(void){
  Stmt q;
  int cnt = 0;
  db_prepare(&q,
    "SELECT blob.uuid, datetime(event.mtime, 'localtime'),"
    "       coalesce(event.euser, event.user),"
    "       coalesce(event.ecomment,event.comment)"
    "  FROM leaves, blob, event"
    " WHERE blob.rid=leaves.rid"
    "   AND event.objid=leaves.rid"
    " ORDER BY event.mtime DESC"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zUser = db_column_text(&q, 2);
    const char *zCom = db_column_text(&q, 3);
    cnt++;
    if( cnt==1 ){
      @ <div class="section">Leaves</div>
      @ <ul>
    }
    @ <li>
    hyperlink_to_uuid(zUuid);
    @ %w(zCom) (by %s(zUser) on %s(zDate))
  }
  db_finalize(&q);
  if( cnt ){
    @ </ul>
  }
}

/*
** Show information about all tags on a given node.
*/
static void showTags(int rid, const char *zNotGlob){
  Stmt q;
  int cnt = 0;
  db_prepare(&q,
    "SELECT tag.tagid, tagname, srcid, blob.uuid, value,"
    "       datetime(tagxref.mtime,'localtime'), tagtype"
    "  FROM tagxref JOIN tag ON tagxref.tagid=tag.tagid"
    "       LEFT JOIN blob ON blob.rid=tagxref.srcid"
    " WHERE tagxref.rid=%d AND tagname NOT GLOB '%s'"
    " ORDER BY tagname", rid, zNotGlob
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTagname = db_column_text(&q, 1);
    int srcid = db_column_int(&q, 2);
    const char *zUuid = db_column_text(&q, 3);
    const char *zValue = db_column_text(&q, 4);
    const char *zDate = db_column_text(&q, 5);
    int tagtype = db_column_int(&q, 6);
    cnt++;
    if( cnt==1 ){
      @ <div class="section">Tags And Properties</div>
      @ <ul>
    }
    @ <li>
    @ <b>%h(zTagname)</b>
    if( zValue ){
      @ = %h(zValue)<i>
    }else if( tagtype==0 ){
      @ <i>Cancelled
    }else{
      @ <i>
    }
    if( srcid==0 ){
      @ Inherited
    }else if( zUuid ){
      @ From
      hyperlink_to_uuid(zUuid);
    }
    @ on %s(zDate)</i>
  }
  db_finalize(&q);
  if( cnt ){
    @ </ul>
  }
}


/*
** WEBPAGE: vinfo
** URL:  /vinfo?name=RID|UUID
**
** Return information about a baseline
*/
void vinfo_page(void){
  Stmt q;
  int rid;
  int isLeaf;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  rid = name_to_rid(PD("name","0"));
  if( rid==0 ){
    style_header("Baseline Information Error");
    @ No such object: %h(g.argv[2])
    style_footer();
    return;
  }
  isLeaf = !db_exists("SELECT 1 FROM plink WHERE pid=%d", rid);
  db_prepare(&q, 
     "SELECT uuid, datetime(mtime, 'localtime'), user, comment"
     "  FROM blob, event"
     " WHERE blob.rid=%d"
     "   AND event.objid=%d",
     rid, rid
  );
  if( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    char *zTitle = mprintf("Baseline [%.10s]", zUuid);
    char *zEUser, *zEComment;
    const char *zUser;
    const char *zComment;
    style_header(zTitle);
    login_anonymous_available();
    free(zTitle);
    zEUser = db_text(0,
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                    TAG_USER, rid);
    zEComment = db_text(0, 
                   "SELECT value FROM tagxref WHERE tagid=%d AND rid=%d",
                   TAG_COMMENT, rid);
    zUser = db_column_text(&q, 2);
    zComment = db_column_text(&q, 3);
    @ <div class="section">Overview</div>
    @ <p><table class="label-value">
    @ <tr><th>SHA1&nbsp;Hash:</th><td>%s(zUuid)</td></tr>
    @ <tr><th>Date:</th><td>%s(db_column_text(&q, 1))</td></tr>
    if( g.okSetup ){
      @ <tr><th>Record ID:</th><td>%d(rid)</td></tr>
    }
    if( zEUser ){
      @ <tr><th>Edited&nbsp;User:</td><td>%h(zEUser)</td></tr>
      @ <tr><th>Original&nbsp;User:</th><td>%h(zUser)</td></tr>
    }else{
      @ <tr><th>User:</td><td>%h(zUser)</td></tr>
    }
    if( zEComment ){
      @ <tr><th>Edited&nbsp;Comment:</th><td>%w(zEComment)</td></tr>
      @ <tr><th>Original&nbsp;Comment:</th><td>%w(zComment)</td></tr>
    }else{
      @ <tr><th>Comment:</th><td>%w(zComment)</td></tr>
    }
    @ </td></tr>
    if( g.okHistory ){
      char *zShortUuid = mprintf("%.10s", zUuid);
      const char *zProjName = db_get("project-name", "unnamed");
      @ <tr><th>Timelines:</th><td>
      @    <a href="%s(g.zBaseURL)/timeline?p=%d(rid)">ancestors</a>
      @    | <a href="%s(g.zBaseURL)/timeline?d=%d(rid)">descendants</a>
      @    | <a href="%s(g.zBaseURL)/timeline?d=%d(rid)&p=%d(rid)">both</a>
      @ </td></tr>
      @ <tr><th>Commands:</th>
      @   <td>
      @     <a href="%s(g.zBaseURL)/vdiff/%d(rid)">diff</a>
      @     | <a href="%s(g.zBaseURL)/zip/%s(zProjName)-%s(zShortUuid).zip?rid=%s(zUuid)">
      @         ZIP archive</a>
      @     | <a href="%s(g.zBaseURL)/artifact/%d(rid)">manifest</a>
      if( g.okWrite ){
        @     | <a href="%s(g.zBaseURL)/vedit?r=%d(rid)">edit</a>
      }
      @   </td>
      @ </tr>
      free(zShortUuid);
    }
    @ </table></p>
  }else{
    style_header("Baseline Information");
    login_anonymous_available();
  }
  db_finalize(&q);
  showTags(rid, "");
  @ <div class="section">Files Changed</div>
  @ <ul>
  db_prepare(&q, 
     "SELECT name, pid, fid"
     "  FROM mlink, filename"
     " WHERE mid=%d"
     "   AND filename.fnid=mlink.fnid",
     rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    int pid = db_column_int(&q, 1);
    int fid = db_column_int(&q, 2);
    @ <li>
    if( pid && fid ){
      @ <b>Modified:</b>
    }else if( fid ){
      @ <b>Added:</b>
    }else{
      @ <b>Deleted:</b>
    }
    if( g.okHistory ){
      @ <a href="%s(g.zBaseURL)/finfo?name=%T(zName)">%h(zName)</a></li>
    }else{
      @ %h(zName)</li>
    }
  }
  @ </ul>
  compute_leaves(rid);
  showDescendants(rid, 2, "Descendants");
  showLeaves();
  showAncestors(rid, 2, "Ancestors");
  style_footer();
}

/*
** WEBPAGE: winfo
** URL:  /winfo?name=RID
**
** Return information about a wiki page.
*/
void winfo_page(void){
  Stmt q;
  int rid;

  login_check_credentials();
  if( !g.okRdWiki ){ login_needed(); return; }
  rid = name_to_rid(PD("name","0"));
  if( rid==0 ){
    style_header("Wiki Page Information Error");
    @ No such object: %h(g.argv[2])
    style_footer();
    return;
  }
  db_prepare(&q, 
     "SELECT substr(tagname, 6, 1000), uuid,"
     "       datetime(event.mtime, 'localtime'), user"
     "  FROM tagxref, tag, blob, event"
     " WHERE tagxref.rid=%d"
     "   AND tag.tagid=tagxref.tagid"
     "   AND tag.tagname LIKE 'wiki-%%'"
     "   AND blob.rid=%d"
     "   AND event.objid=%d",
     rid, rid, rid
  );
  if( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    char *zTitle = mprintf("Wiki Page %s", zName);
    style_header(zTitle);
    free(zTitle);
    login_anonymous_available();
    @ <div class="section">Overview</div>
    @ <p><table class="label-value">
    @ <tr><th>Version:</th><td>%s(zUuid)</td></tr>
    @ <tr><th>Date:</th><td>%s(db_column_text(&q, 2))</td></tr>
    if( g.okSetup ){
      @ <tr><th>Record ID:</th><td>%d(rid)</td></tr>
    }
    @ <tr><th>Original&nbsp;User:</th><td>%s(db_column_text(&q, 3))</td></tr>
    if( g.okHistory ){
      @ <tr><th>Commands:</th>
      @   <td>
      /* @     <a href="%s(g.zBaseURL)/wdiff/%d(rid)">diff</a> | */
      @     <a href="%s(g.zBaseURL)/whistory?name=%t(zName)">history</a>
      @     | <a href="%s(g.zBaseURL)/artifact/%d(rid)">raw-text</a>
      @   </td>
      @ </tr>
    }
    @ </table></p>
  }else{
    style_header("Wiki Information");
    rid = 0;
  }
  db_finalize(&q);
  showTags(rid, "wiki-*");
  if( rid ){
    Blob content;
    Manifest m;
    memset(&m, 0, sizeof(m));
    blob_zero(&m.content);
    content_get(rid, &content);
    manifest_parse(&m, &content);
    if( m.type==CFTYPE_WIKI ){
      Blob wiki;
      blob_init(&wiki, m.zWiki, -1);
      @ <div class="section">Content</div>
      wiki_convert(&wiki, 0, 0);
      blob_reset(&wiki);
    }
    manifest_clear(&m);
  }
  style_footer();
}

/*
** WEBPAGE: finfo
** URL: /finfo?name=FILENAME
**
** Show the complete change history for a single file. 
*/
void finfo_page(void){
  Stmt q;
  const char *zFilename;
  char zPrevDate[20];
  Blob title;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  style_header("File History");
  login_anonymous_available();

  zPrevDate[0] = 0;
  zFilename = PD("name","");
  db_prepare(&q,
    "SELECT a.uuid, substr(b.uuid,1,10), datetime(event.mtime,'localtime'),"
    "       coalesce(event.ecomment, event.comment),"
    "       coalesce(event.euser, event.user),"
    "       mlink.pid, mlink.fid, mlink.mid, mlink.fnid"
    "  FROM mlink, blob a, blob b, event"
    " WHERE mlink.fnid=(SELECT fnid FROM filename WHERE name=%Q)"
    "   AND a.rid=mlink.mid"
    "   AND b.rid=mlink.fid"
    "   AND event.objid=mlink.mid"
    " ORDER BY event.mtime DESC",
    zFilename
  );
  blob_zero(&title);
  blob_appendf(&title, "History of ");
  hyperlinked_path(zFilename, &title);
  @ <h2>%b(&title)</h2>
  blob_reset(&title);
  @ <table cellspacing=0 border=0 cellpadding=0>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zVers = db_column_text(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zCom = db_column_text(&q, 3);
    const char *zUser = db_column_text(&q, 4);
    int fpid = db_column_int(&q, 5);
    int frid = db_column_int(&q, 6);
    int mid = db_column_int(&q, 7);
    int fnid = db_column_int(&q, 8);
    if( memcmp(zDate, zPrevDate, 10) ){
      sprintf(zPrevDate, "%.10s", zDate);
      @ <tr><td colspan=3>
      @ <table cellpadding=2 border=0>
      @ <tr><td bgcolor="#a0b5f4" class="border1">
      @ <table cellpadding=2 cellspacing=0 border=0><tr>
      @ <td bgcolor="#d0d9f4" class="bkgnd1">%s(zPrevDate)</td>
      @ </tr></table>
      @ </td></tr></table>
      @ </td></tr>
    }
    @ <tr><td valign="top">%s(&zDate[11])</td>
    @ <td width="20"></td>
    @ <td valign="top" align="left">
    hyperlink_to_uuid(zVers);
    @ %h(zCom) (By: %h(zUser))
    @ Id: %s(zUuid)/%d(frid)
    if( g.okHistory ){
      @ <a href="%s(g.zBaseURL)/artifact/%d(frid)">[view]</a>
      if( fpid ){
        @ <a href="%s(g.zBaseURL)/fdiff?v1=%d(fpid)&amp;v2=%d(frid)">[diff]</a>
      }
      @ <a href="%s(g.zBaseURL)/annotate?mid=%d(mid)&amp;fnid=%d(fnid)">
      @ [annotate]</a>
      @ </td>
    }
  }
  db_finalize(&q);
  @ </table>
  style_footer();
}


/*
** Append the difference between two RIDs to the output
*/
static void append_diff(int fromid, int toid){
  Blob from, to, out;
  content_get(fromid, &from);
  content_get(toid, &to);
  blob_zero(&out);
  text_diff(&from, &to, &out, 5);
  @ %h(blob_str(&out))
  blob_reset(&from);
  blob_reset(&to);
  blob_reset(&out);  
}

/*
** WEBPAGE: vdiff
** URL: /vdiff?name=RID
**
** Show all differences for a particular check-in.
*/
void vdiff_page(void){
  int rid;
  Stmt q;
  char *zUuid;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  style_header("Baseline Changes");
  login_anonymous_available();

  rid = name_to_rid(PD("name",""));
  if( rid==0 ){
    fossil_redirect_home();
  }
  db_prepare(&q,
     "SELECT pid, fid, name"
     "  FROM mlink, filename"
     " WHERE mlink.mid=%d"
     "   AND filename.fnid=mlink.fnid"
     " ORDER BY name",
     rid
  );
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  @ <h2>All Changes In Baseline
  hyperlink_to_uuid(zUuid);
  @ </h2>
  while( db_step(&q)==SQLITE_ROW ){
    int pid = db_column_int(&q,0);
    int fid = db_column_int(&q,1);
    const char *zName = db_column_text(&q,2);
    @ <p><a href="%s(g.zBaseURL)/finfo?name=%T(zName)">%h(zName)</a></p>
    @ <blockquote><pre>
    append_diff(pid, fid);
    @ </pre></blockquote>
  }
  db_finalize(&q);
  style_footer();
}


/*
** Write a description of an object to the www reply.
**
** If the object is a file then mention:
**
**     * It's uuid
**     * All its filenames
**     * The baselines it was checked-in on, with times and users
**
** If the object is a manifest, then mention:
**
**     * It's uuid
**     * date of check-in
**     * Comment & user
*/
static void object_description(int rid, int linkToView){
  Stmt q;
  int cnt = 0;
  int nWiki = 0;
  db_prepare(&q,
    "SELECT filename.name, datetime(event.mtime), substr(a.uuid,1,10),"
    "       coalesce(event.ecomment,event.comment),"
    "       coalesce(event.euser,event.user),"
    "       b.uuid"
    "  FROM mlink, filename, event, blob a, blob b"
    " WHERE filename.fnid=mlink.fnid"
    "   AND event.objid=mlink.mid"
    "   AND a.rid=mlink.fid"
    "   AND b.rid=mlink.mid"
    "   AND mlink.fid=%d",
    rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zFuuid = db_column_text(&q, 2);
    const char *zCom = db_column_text(&q, 3);
    const char *zUser = db_column_text(&q, 4);
    const char *zVers = db_column_text(&q, 5);
    @ File <a href="%s(g.zBaseURL)/finfo?name=%T(zName)">%h(zName)</a>
    @ uuid %s(zFuuid) part of check-in
    hyperlink_to_uuid(zVers);
    @ %w(zCom) by %h(zUser) on %s(zDate)
    cnt++;
  }
  db_finalize(&q);
  db_prepare(&q, 
    "SELECT substr(tagname, 6, 10000), datetime(event.mtime),"
    "       coalesce(event.euser, event.user), uuid"
    "  FROM tagxref, tag, event, blob"
    " WHERE tagxref.rid=%d"
    "   AND tag.tagid=tagxref.tagid" 
    "   AND tag.tagname LIKE 'wiki-%%'"
    "   AND event.objid=tagxref.rid"
    "   AND blob.rid=tagxref.rid",
    rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPagename = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zUser = db_column_text(&q, 2);
    const char *zUuid = db_column_text(&q, 3);
    @ Wiki page
    @ [<a href="%s(g.zBaseURL)/wiki?name=%t(zPagename)">%h(zPagename)</a>]
    @ uuid %s(zUuid) by %h(zUser) on %s(zDate)
    nWiki++;
    cnt++;
  }
  db_finalize(&q);
  if( nWiki==0 ){
    db_prepare(&q,
      "SELECT datetime(mtime), user, comment, uuid, type"
      "  FROM event, blob"
      " WHERE event.objid=%d"
      "   AND blob.rid=%d",
      rid, rid
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zDate = db_column_text(&q, 0);
      const char *zUuid = db_column_text(&q, 3);
      const char *zUser = db_column_text(&q, 1);
      const char *zCom = db_column_text(&q, 2);
      const char *zType = db_column_text(&q, 4);
      if( zType[0]=='w' ){
        @ Wiki edit
      }else if( zType[0]=='t' ){
        @ Ticket change
      }else if( zType[0]=='c' ){
        @ Manifest of baseline
      }else{
        @ Control file referencing
      }
      hyperlink_to_uuid(zUuid);
      @ %w(zCom) by %h(zUser) on %s(zDate)
      cnt++;
    }
    db_finalize(&q);
  }
  if( cnt==0 ){
    char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    @ Control file %s(zUuid).
  }else if( linkToView ){
    @ <a href="%s(g.zBaseURL)/artifact/%d(rid)">[view]</a>
  }
}

/*
** WEBPAGE: fdiff
**
** Two arguments, v1 and v2, are integers.  Show the difference between
** the two records.
*/
void diff_page(void){
  int v1 = name_to_rid(PD("v1","0"));
  int v2 = name_to_rid(PD("v2","0"));
  Blob c1, c2, diff;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  style_header("Diff");
  @ <h2>Differences From:</h2>
  @ <blockquote>
  object_description(v1, 1);
  @ </blockquote>
  @ <h2>To:</h2>
  @ <blockquote>
  object_description(v2, 1);
  @ </blockquote>
  @ <hr>
  @ <blockquote><pre>
  content_get(v1, &c1);
  content_get(v2, &c2);
  blob_zero(&diff);
  text_diff(&c1, &c2, &diff, 4);
  blob_reset(&c1);
  blob_reset(&c2);
  @ %h(blob_str(&diff))
  @ </pre></blockquote>
  blob_reset(&diff);
  style_footer();
}

/*
** WEBPAGE: artifact
** URL: /artifact?name=UUID
** 
** Show the complete content of a file identified by UUID
** as preformatted text.
*/
void artifact_page(void){
  int rid;
  Blob content;

  rid = name_to_rid(PD("name","0"));
  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  if( rid==0 ){ cgi_redirect("/home"); }
  if( g.okAdmin ){
    const char *zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
    if( db_exists("SELECT 1 FROM shun WHERE uuid='%s'", zUuid) ){
      style_submenu_element("Unshun","Unshun", "%s/shun?uuid=%s&sub=1",
            g.zTop, zUuid);
    }else{
      style_submenu_element("Shun","Shun", "%s/shun?shun=%s#addshun",
            g.zTop, zUuid);
    }
  }
  style_header("Artifact Content");
  @ <h2>Content Of:</h2>
  @ <blockquote>
  object_description(rid, 0);
  @ </blockquote>
  @ <hr>
  @ <blockquote><pre>
  content_get(rid, &content);
  @ %h(blob_str(&content))
  @ </pre></blockquote>
  blob_reset(&content);
  style_footer();
}

/*
** Return TRUE if the given BLOB contains a newline character.
*/
static int contains_newline(Blob *p){
  const char *z = blob_str(p);
  while( *z ){
    if( *z=='\n' ) return 1;
    z++;
  }
  return 0;
}

/*
** WEBPAGE: tinfo
** URL: /tinfo?name=UUID
**
** Show the details of a ticket change control artifact.
*/
void tinfo_page(void){
  int rid;
  Blob content;
  char *zDate;
  int i;
  const char *zUuid;
  char zTktName[20];
  const char *z;
  Manifest m;

  login_check_credentials();
  if( !g.okRdTkt ){ login_needed(); return; }
  rid = name_to_rid(PD("name","0"));
  if( rid==0 ){ fossil_redirect_home(); }
  zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( g.okAdmin ){
    if( db_exists("SELECT 1 FROM shun WHERE uuid='%s'", zUuid) ){
      style_submenu_element("Unshun","Unshun", "%s/shun?uuid=%s&sub=1",
            g.zTop, zUuid);
    }else{
      style_submenu_element("Shun","Shun", "%s/shun?shun=%s#addshun",
            g.zTop, zUuid);
    }
  }
  content_get(rid, &content);
  if( manifest_parse(&m, &content)==0 ){
    fossil_redirect_home();
  }
  if( m.type!=CFTYPE_TICKET ){
    fossil_redirect_home();
  }
  style_header("Ticket Change Details");
  zDate = db_text(0, "SELECT datetime(%.12f)", m.rDate);
  memcpy(zTktName, m.zTicketUuid, 10);
  zTktName[10] = 0;
  @ <h2>Changes to ticket <a href="%s(m.zTicketUuid)">%s(zTktName)</a></h2>
  @
  @ <p>By %h(m.zUser) on %s(zDate).  See also:
  @ <a href="%s(g.zTop)/artifact/%T(zUuid)">artifact content</a>, and
  @ <a href="%s(g.zTop)/tkthistory/%s(m.zTicketUuid)">ticket history</a>
  @ </p>
  @
  @ <ol>
  free(zDate);
  for(i=0; i<m.nField; i++){
    Blob val;
    z = m.aField[i].zName;
    blob_set(&val, m.aField[i].zValue);
    if( z[0]=='+' ){
      @ <li><p>Appended to %h(&z[1]):</p><blockquote>
      wiki_convert(&val, 0, 0);
      @ </blockquote></li>
    }else if( blob_size(&val)<=50 && contains_newline(&val) ){
      @ <li><p>Change %h(z) to:</p><blockquote>
      wiki_convert(&val, 0, 0);
      @ </blockquote></li>
    }else{
      @ <li><p>Change %h(z) to "%h(blob_str(&val))"</p></li>
    }
    blob_reset(&val);
  }
  manifest_clear(&m);
  @ </ol>
  style_footer();
}


/*
** WEBPAGE: info
** URL: info/UUID
**
** The argument is a UUID which might be a baseline or a file or
** a ticket changes or a wiki editor or something else. 
**
** Figure out what the UUID is and jump to it.
*/
void info_page(void){
  const char *zName;
  int rid, nName;
  
  zName = P("name");
  if( zName==0 ) fossil_redirect_home();
  nName = strlen(zName);
  if( nName<4 || nName>UUID_SIZE || !validate16(zName, nName) ){
    fossil_redirect_home();
  }
  if( db_exists("SELECT 1 FROM ticket WHERE tkt_uuid GLOB '%s*'", zName) ){
    tktview_page();
    return;
  }
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid GLOB '%s*'", zName);
  if( rid==0 ){
    style_header("Broken Link");
    @ <p>No such object: %h(zName)</p>
    style_footer();
    return;
  }
  if( db_exists("SELECT 1 FROM mlink WHERE mid=%d", rid) ){
    vinfo_page();
  }else
  if( db_exists("SELECT 1 FROM mlink WHERE fid=%d", rid) ){
    finfo_page();
  }else
  if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                " WHERE rid=%d AND tagname LIKE 'wiki-%%'", rid) ){
    winfo_page();
  }else
  if( db_exists("SELECT 1 FROM tagxref JOIN tag USING(tagid)"
                " WHERE rid=%d AND tagname LIKE 'tkt-%%'", rid) ){
    tinfo_page();
  }else
  {
    artifact_page();
  }
}

/*
** WEBPAGE: vedit
** URL:  vedit?r=RID&c=NEWCOMMENT&u=NEWUSER
**
** Present a dialog for updating properties of a baseline:
**
**     *  The check-in user
**     *  The check-in comment
**     *  The background color.
*/
void vedit_page(void){
  int rid;
  const char *zComment;
  const char *zNewComment;
  const char *zUser;
  const char *zNewUser;
  char *zUuid;
  Blob comment;
  
  login_check_credentials();
  if( !g.okWrite ){ login_needed(); return; }
  rid = atoi(PD("r","0"));
  zComment = db_text(0, "SELECT coalesce(ecomment,comment)"
                        "  FROM event WHERE objid=%d", rid);
  if( zComment==0 ) fossil_redirect_home();
  zNewComment = PD("c",zComment);
  zUser = db_text(0, "SELECT coalesce(euser,user)"
                     "  FROM event WHERE objid=%d", rid);
  if( zUser==0 ) fossil_redirect_home();
  zNewUser = PD("u",zUser);
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( P("cancel") ){
    cgi_redirectf("vinfo?name=%d", rid);
  }
  if( P("apply") ){
    Blob ctrl;
    char *zDate;
    int nChng = 0;

    blob_zero(&ctrl);
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10] = 'T';
    blob_appendf(&ctrl, "D %s\n", zDate);
    if( strcmp(zComment,zNewComment)!=0 ){
      nChng++;
      blob_appendf(&ctrl, "T +comment %s %F\n", zUuid, zNewComment);
    }
    if( strcmp(zUser,zNewUser)!=0 ){
      nChng++;
      blob_appendf(&ctrl, "T +user %s %F\n", zUuid, zNewUser);
    }
    if( nChng>0 ){
      int nrid;
      Blob cksum;
      blob_appendf(&ctrl, "U %F\n", g.zLogin);
      md5sum_blob(&ctrl, &cksum);
      blob_appendf(&ctrl, "Z %b\n", &cksum);
      db_begin_transaction();
      nrid = content_put(&ctrl, 0, 0);
      manifest_crosslink(nrid, &ctrl);
      db_end_transaction(0);
    }
    cgi_redirectf("vinfo?name=%d", rid);
  }
  blob_zero(&comment);
  blob_append(&comment, zNewComment, -1);
  zUuid[10] = 0;
  style_header("Edit Baseline [%s]", zUuid);
  @ <p>Make changes to the User and Comment for baseline 
  @ [<a href="vinfo?name=%d(rid)">%s(zUuid)</a>] then press the
  @ "Apply Changes" button.</p>
  @ <form action="%s(g.zBaseURL)/vedit" method="POST">
  @ <input type="hidden" name="r" value="%d(rid)">
  @ <p>
  @ <b>User:</b> <input type="text" name="u" size="20" value="%h(zNewUser)">
  @ </p>
  @ <p><b>Comment:</b></b><br />
  wiki_convert(&comment, 0, WIKI_INLINE);
  @ <br /><textarea name="c" rows="10" cols="80">%h(zNewComment)</textarea></p>
  @ <blockquote>
  @ <input type="submit" name="preview" value="Preview">
  @ <input type="submit" name="apply" value="Apply Changes">
  @ <input type="submit" name="cancel" value="Cancel">
  @ </blockquote>
  @ </form>
  style_footer();
}
