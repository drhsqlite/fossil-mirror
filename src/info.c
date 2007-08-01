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
static int showDescendents(int pid, int depth){
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
      @ <ul>
    }
    @ <li>
    hyperlink_to_uuid(zUuid);
    @ %s(zCom) (by %s(zUser) on %s(zDate))
    if( depth ){
      n = showDescendents(cid, depth-1);
    }else{
      n = db_int(0, "SELECT 1 FROM plink WHERE pid=%d", cid);
    }
    if( n==0 ){
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
static int showAncestors(int pid, int depth){
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
  @ <ul>
  while( db_step(&q)==SQLITE_ROW ){
    int cid = db_column_int(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    const char *zCom = db_column_text(&q, 4);
    cnt++;
    @ <li>
    hyperlink_to_uuid(zUuid);
    @ %s(zCom) (by %s(zUser) on %s(zDate))
    if( depth ){
      showAncestors(cid, depth-1);
    }
  }
  @ </ul>
  return cnt;
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
  int n;

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
    @ </ul>
  }
  db_finalize(&q);
  @ <p><h2>Descendents:</h2>
  n = showDescendents(rid, 2);
  if( n==0 ){
    @ <ul>None.  This is a leaf node.</ul>
  }
  @ <p><h2>Ancestors:</h2>
  n = showAncestors(rid, 2);
  if( n==0 ){
    @ <ul>None.  This is the root of the tree.</ul>
  }
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
    "SELECT blob.uuid, datetime(event.mtime,'localtime'),"
    "       event.comment, event.user"
    "  FROM mlink, blob, event"
    " WHERE mlink.fnid=(SELECT fnid FROM filename WHERE name=%Q)"
    "   AND blob.rid=mlink.mid"
    "   AND event.objid=mlink.mid"
    " ORDER BY event.mtime DESC",
    g.zExtra
  );
  @ <h2>History of %h(g.zExtra)</h2>
  @ <table cellspacing=0 border=0 cellpadding=0>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 1);
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
    hyperlink_to_uuid(db_column_text(&q,0));
    @ %h(db_column_text(&q,2)) (by %h(db_column_text(&q,3)))</td>
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
  int rid, i;
  Stmt q;
  Manifest m;
  Blob mfile, file;
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



#if 0
/*
** WEB PAGE: diff
**
** Display the difference between two files determined by the v1 and v2
** query parameters.  If only v2 is given compute v1 as the parent of v2.
** If v2 has no parent, then show the complete text of v2.
*/
void diff_page(void){
  const char *zV1 = P("v1");
  const char *zV2 = P("v2");
  int vid1, vid2;
  Blob out;
  Record *p1, *p2;

  if( zV2==0 ){
    cgi_redirect("index");
  }
  vid2 = uuid_to_rid(zV2, 0);
  p2 = record_from_rid(vid2);
  style_header("File Diff");
  if( zV1==0 ){
    zV1 = db_text(0, 
       "SELECT uuid FROM record WHERE rid="
       "  (SELECT a FROM link WHERE typecode='P' AND b=%d)", vid2);
  }
  if( zV1==0 ){
    @ <p>Content of
    hyperlink_to_uuid(zV2);
    @ </p>
    @ <pre>
    @ %h(blob_str(record_get_content(p2)))
    @ </pre>
  }else{
    vid1 = uuid_to_rid(zV1, 0);
    p1 = record_from_rid(vid1);
    blob_zero(&out);
    unified_diff(record_get_content(p1), record_get_content(p2), 4, &out);
    @ <p>Differences between
    hyperlink_to_uuid(zV1);
    @ and
    hyperlink_to_uuid(zV2);
    @ </p>
    @ <pre>
    @ %h(blob_str(&out))
    @ </pre>
    record_destroy(p1);
    blob_reset(&out);
  }
  record_destroy(p2);
  style_footer();
}
#endif
