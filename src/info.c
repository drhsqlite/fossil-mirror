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
    "SELECT plink.cid, blob.uuid, datetime(plink.mtime),"
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
    "SELECT plink.pid, blob.uuid, datetime(event.mtime),"
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
    int n;
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
  int cid, pid, n;

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
     "SELECT uuid, datetime(mtime), user, comment"
     "  FROM blob, event"
     " WHERE blob.rid=%d"
     "   AND event.objid=%d",
     rid, rid
  );
  if( db_step(&q)==SQLITE_ROW ){
    @ <h2>Version %s(db_column_text(&q,0))</h2>
    @ <ul>
    @ <li><b>Date:</b> %s(db_column_text(&q, 1))</li>
    @ <li><b>User:</b> %s(db_column_text(&q, 2))</li>
    @ <li><b>Comment:</b> %s(db_column_text(&q, 3))</li>
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
    @ %h(zName)</li>
  }
  @ </ul>
  style_footer();
}


#if 0
/*
** WEB PAGE: vinfo
**
** Return information about a version.  The version number is contained
** in g.zExtra.
*/
void vinfo_page(void){
  Stmt q;
  int rid;
  char cType;
  char *zType;

  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  style_header("Version Information");
  rid = name_to_rid(g.zExtra);
  if( rid==0 ){
    @ No such object: %h(g.argv[2])
    style_footer();
    return;
  }
  db_row_to_table("SELECT "
    "  blob.uuid               AS \"UUID\""
    ", datetime(rcvfrom.mtime) AS \"Created\""
    ", rcvfrom.uid             AS \"User Id\""
    ", blob.size               AS \"Size\""
    "FROM blob, rcvfrom "
    "WHERE rid=%d", rid
  );
  style_footer();
  return;

  db_prepare(&q,
    "SELECT "
      "uuid, "                                         /* 0 */
      "datetime(mtime,'unixepoch'),"                   /* 1 */
      "datetime(ctime,'unixepoch'),"                   /* 2 */
      "uid, size, cksum, branch, comment, type"        /* 3..8 */
    "FROM record WHERE rid=%d", rid
  );
  if( db_step(&q)==SQLITE_ROW ){
    const char *z;
    const char *zSignedBy = db_text("unknown",
                                     "SELECT login FROM repuser WHERE uid=%d",
                                     db_column_int(&q, 3));
    cType = db_column_text(&q,8)[0];
    switch( cType ){
      case 'f':  zType = "file";        break;
      case 'v':  zType = "version";     break;
      case 'c':  zType = "control";     break;
      case 'w':  zType = "wiki";        break;
      case 'a':  zType = "attachment";  break;
      case 't':  zType = "ticket";      break;
    }
    @ <table border="0" cellpadding="0" cellspacing="0">
    @ <tr><td align="right">%s(zType)&nbsp;UUID:</td><td width="10"></td>
    @ <td>%s(db_column_text(&q,0))</td></tr>
    z = db_column_text(&q, 7);
    if( z ){
      @ <tr><td align="right" valign="top">comment:</td><td></td>
      @ <td valign="top">%h(z)</td></tr>
    }
    @ <tr><td align="right">created:</td><td></td>
    @ <td>%s(db_column_text(&q,2))</td></tr>
    @ <tr><td align="right">received:</td><td></td>
    @ <td>%s(db_column_text(&q,1))</td></tr>
    @ <tr><td align="right">signed&nbsp;by:</td><td></td>
    @ <td>%h(zSignedBy)</td></tr>
    z = db_column_text(&q, 4);
    if( z && z[0] && (z[0]!='0' || z[1]!=0) ){
      @ <tr><td align="right">size:</td><td></td>
      @ <td>%s(z)</td></tr>
    }
    z = db_column_text(&q, 5);
    if( z ){
      @ <tr><td align="right">MD5&nbsp;checksum:</td><td></td>
      @ <td>%s(z)</td></tr>
    }
    z = db_column_text(&q, 6);
    if( z ){
      @ <tr><td align="right">branch:</td><td></td>
      @ <td>%h(z)</td></tr>
    }
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid, typecode FROM link JOIN record ON a=rid "
                 " WHERE b=%d", rid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zType = db_column_text(&q, 1);
    const char *zUuid = db_column_text(&q, 0);
    if( zType[0]=='P' ){
      @ <tr><td align="right">parent:</td><td></td><td>
      hyperlink_to_uuid(zUuid);
      if( cType=='f' || cType=='w' ){
        hyperlink_to_diff(zUuid, g.zExtra);
      }
      @ </td></tr>
    }else if( zType[0]=='M' ){
      @ <tr><td align="right">merge&nbsp;parent:</td><td></td><td>
      hyperlink_to_uuid(zUuid);
      if( cType=='f' || cType=='w' ){
        hyperlink_to_diff(zUuid, g.zExtra);
      }
      @ </td></tr>
    }
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid, typecode FROM link JOIN record ON b=rid "
                 " WHERE a=%d ORDER BY typecode DESC", rid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zType = db_column_text(&q, 1);
    const char *zUuid = db_column_text(&q, 0);
    if( zType[0]=='P' ){
      @ <tr><td align="right">child:</td><td></td><td>
      hyperlink_to_uuid(zUuid);
      if( cType=='f' || cType=='w' ){
        hyperlink_to_diff(g.zExtra, zUuid);
      }
      @ </td></tr>
    }else if( zType[0]=='M' ){
      @ <tr><td align="right">merge&nbsp;child:</td><td></td><td>
      hyperlink_to_uuid(zUuid);
      if( cType=='f' || cType=='w' ){
        hyperlink_to_diff(g.zExtra, zUuid);
      }
      @ </td></tr>
    }
  }
  db_finalize(&q);
  if( cType=='v' ){
    db_prepare(&q, "SELECT uuid, typecode, name "
                   " FROM link, record, fname"
                   " WHERE a=%d AND typecode IN ('D','E','I')"
                   "   AND b=record.rid AND fname.fnid=record.fnid"
                   " ORDER BY name", rid);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zUuid = db_column_text(&q, 0);
      const char *zType = db_column_text(&q, 1);
      const char *zName = db_column_text(&q, 2);
      if( zType[0]=='D' ){
        @ <tr><td align="right">deleted&nbsp;file:</td><td></td><td>
        hyperlink_to_uuid(zUuid);
      }else if( zType[0]=='E' ){
        @ <tr><td align="right">changed&nbsp;file:</td><td></td><td>
        hyperlink_to_uuid(zUuid);
        hyperlink_to_diff(zUuid, 0);
      }else if( zType[0]=='I' ){
        @ <tr><td align="right">added&nbsp;file:</td><td></td><td>
        hyperlink_to_uuid(zUuid);
      }
      @ &nbsp;&nbsp;%h(zName)</td></tr>
    }
    db_finalize(&q);
  }else if( cType=='f' ){
    db_prepare(&q, "SELECT uuid"
                   " FROM link, record"
                   " WHERE b=%d AND typecode IN ('E','I')"
                   "   AND a=record.rid", rid);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zUuid = db_column_text(&q, 0);
      @ <tr><td align="right">associated&nbsp;version:</td><td></td><td>
      hyperlink_to_uuid(zUuid);
      @ </td></tr>
    }
    db_finalize(&q);
  }
  style_footer();
}
#endif

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
