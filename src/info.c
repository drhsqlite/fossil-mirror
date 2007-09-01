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
** the current tree, or a particular file or version.
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
    int rid = name_to_rid(g.argv[2]);
    if( rid==0 ){
      fossil_panic("no such object: %s\n", g.argv[2]);
    }
    show_common_info(rid, "uuid:", 1);
  }
}

/*
** Show information about descendents of a version.  Do this recursively
** to a depth of N.  Return true if descendents are shown and false if not.
*/
static int showDescendents(int pid, int depth, const char *zTitle){
  Stmt q;
  int cnt = 0;
  db_prepare(&q,
    "SELECT plink.cid, blob.uuid, datetime(plink.mtime, 'localtime'),"
    "       event.user, event.comment"
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
        @ <h2>%s(zTitle)</h2>
      }
      @ <ul>
    }
    @ <li>
    hyperlink_to_uuid(zUuid);
    @ %s(zCom) (by %s(zUser) on %s(zDate))
    if( depth ){
      n = showDescendents(cid, depth-1, 0);
    }else{
      n = db_int(0, "SELECT 1 FROM plink WHERE pid=%d", cid);
    }
    if( n==0 ){
      db_multi_exec("DELETE FROM leaves WHERE rid=%d", cid);
      @ <b>leaf</b>
    }
  }
  if( cnt ){
    @ </ul>
  }
  return cnt;
}

/*
** Show information about ancestors of a version.  Do this recursively
** to a depth of N.  Return true if ancestors are shown and false if not.
*/
static void showAncestors(int pid, int depth, const char *zTitle){
  Stmt q;
  int cnt = 0;
  db_prepare(&q,
    "SELECT plink.pid, blob.uuid, datetime(event.mtime, 'localtime'),"
    "       event.user, event.comment"
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
        @ <h2>%s(zTitle)</h2>
      }
      @ <ul>
    }
    @ <li>
    hyperlink_to_uuid(zUuid);
    @ %s(zCom) (by %s(zUser) on %s(zDate))
    if( depth ){
      showAncestors(cid, depth-1, 0);
    }
  }
  if( cnt ){
    @ </ul>
  }
}


/*
** Show information about versions mentioned in the "leaves" table.
*/
static void showLeaves(void){
  Stmt q;
  int cnt = 0;
  db_prepare(&q,
    "SELECT blob.uuid, datetime(event.mtime, 'localtime'),"
    "       event.user, event.comment"
    "  FROM leaves, plink, blob, event"
    " WHERE plink.cid=leaves.rid"
    "   AND blob.rid=leaves.rid"
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
      @ <h2>Leaves</h2>
      @ <ul>
    }
    @ <li>
    hyperlink_to_uuid(zUuid);
    @ %s(zCom) (by %s(zUser) on %s(zDate))
  }
  if( cnt ){
    @ </ul>
  }
}


/*
** WEBPAGE: vinfo
**
** Return information about a version.  The version number is contained
** in g.zExtra.
*/
void vinfo_page(void){
  Stmt q;
  int rid;
  int isLeaf;

  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  style_header("Version Information");
  rid = name_to_rid(g.zExtra);
  if( rid==0 ){
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
    @ <h2>Version %s(zUuid)</h2>
    @ <ul>
    @ <li><b>Date:</b> %s(db_column_text(&q, 1))</li>
    @ <li><b>User:</b> %s(db_column_text(&q, 2))</li>
    @ <li><b>Comment:</b> %s(db_column_text(&q, 3))</li>
    @ <li><a href="%s(g.zBaseURL)/vdiff/%d(rid)">diff</a></li>
    @ <li><a href="%s(g.zBaseURL)/zip/%s(zUuid).zip">ZIP archive</a></li>
    @ <li><a href="%s(g.zBaseURL)/fview/%d(rid)">manifest</a></li>
    if( g.okSetup ){
      @ <li><b>Record ID:</b> %d(rid)</li>
    }
    @ </ul>
  }
  db_finalize(&q);
  @ <p><h2>Changes:</h2>
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
    @ <a href="%s(g.zBaseURL)/finfo/%T(zName)">%h(zName)</a></li>
  }
  @ </ul>
  compute_leaves(rid);
  showDescendents(rid, 2, "Descendents");
  showLeaves();
  showAncestors(rid, 2, "Ancestors");
  style_footer();
}

/*
** WEBPAGE: finfo
**
** Show the complete change history for a single file.  The name
** of the file is in g.zExtra
*/
void finfo_page(void){
  Stmt q;
  char zPrevDate[20];
  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  style_header("File History");

  zPrevDate[0] = 0;
  db_prepare(&q,
    "SELECT a.uuid, substr(b.uuid,1,10), datetime(event.mtime,'localtime'),"
    "       event.comment, event.user, mlink.pid, mlink.fid"
    "  FROM mlink, blob a, blob b, event"
    " WHERE mlink.fnid=(SELECT fnid FROM filename WHERE name=%Q)"
    "   AND a.rid=mlink.mid"
    "   AND b.rid=mlink.fid"
    "   AND event.objid=mlink.mid"
    " ORDER BY event.mtime DESC",
    g.zExtra
  );
  @ <h2>History of %h(g.zExtra)</h2>
  @ <table cellspacing=0 border=0 cellpadding=0>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zVers = db_column_text(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zCom = db_column_text(&q, 3);
    const char *zUser = db_column_text(&q, 4);
    int fpid = db_column_int(&q, 5);
    int frid = db_column_int(&q, 6);
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
    @ <a href="%s(g.zBaseURL)/fview/%d(frid)">[view]</a>
    @ <a href="%s(g.zBaseURL)/fdiff?v1=%d(fpid)&amp;v2=%d(frid)">[diff]</a>
    @ </td>
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
  unified_diff(&from, &to, 5, &out);
  @ %h(blob_str(&out))
  blob_reset(&from);
  blob_reset(&to);
  blob_reset(&out);  
}

/*
** WEBPAGE: vdiff
**
** Show all differences for a particular check-in specified by g.zExtra
*/
void vdiff_page(void){
  int rid;
  Stmt q;
  char *zUuid;

  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  style_header("Version Diff");

  rid = name_to_rid(g.zExtra);
  if( rid==0 ){
    cgi_redirect("index");
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
  @ <h2>All Changes In Version
  hyperlink_to_uuid(zUuid);
  @ </h2>
  while( db_step(&q)==SQLITE_ROW ){
    int pid = db_column_int(&q,0);
    int fid = db_column_int(&q,1);
    const char *zName = db_column_text(&q,2);
    @ <p><a href="%s(g.zBaseURL)/finfo/%T(zName)">%h(zName)</a></p>
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
**     * The versions it was checked-in on, with times and users
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
  db_prepare(&q,
    "SELECT filename.name, datetime(event.mtime), substr(a.uuid,1,10),"
    "       event.comment, event.user, b.uuid"
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
    @ File <a href="%s(g.zBaseURL)/finfo/%T(zName)">%h(zName)</a>
    @ uuid %s(zFuuid) part of check-in
    hyperlink_to_uuid(zVers);
    @ %s(zCom) by %s(zUser) on %s(zDate).
    cnt++;
  }
  db_finalize(&q);
  db_prepare(&q,
    "SELECT datetime(mtime), user, comment, uuid"
    "  FROM event, blob"
    " WHERE event.objid=%d"
    "   AND blob.rid=%d",
    rid, rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 0);
    const char *zUuid = db_column_text(&q, 3);
    const char *zCom = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 1);
    @ Version
    hyperlink_to_uuid(zUuid);
    @ %s(zCom) by %s(zUser) on %s(zDate).
    cnt++;
  }
  db_finalize(&q);
  if( cnt==0 ){
    @ Empty file
  }else if( linkToView ){
    @ <a href="%s(g.zBaseURL)/fview/%d(rid)">[view]</a>
  }
}

/*
** WEBPAGE: fdiff
**
** Two arguments, v1 and v2, are integers.  Show the difference between
** the two records.
*/
void diff_page(void){
  int v1 = atoi(PD("v1","0"));
  int v2 = atoi(PD("v2","0"));
  Blob c1, c2, diff;

  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
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
  unified_diff(&c1, &c2, 4, &diff);
  blob_reset(&c1);
  blob_reset(&c2);
  @ %h(blob_str(&diff))
  @ </pre></blockquote>
  blob_reset(&diff);
  style_footer();
}

/*
** WEBPAGE: fview
** URL: /fview/UUID
** 
** Show the complete content of a file identified by UUID
** as preformatted text.
*/
void fview_page(void){
  int rid;
  Blob content;

  rid = name_to_rid(g.zExtra);
  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  style_header("File Content");
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
