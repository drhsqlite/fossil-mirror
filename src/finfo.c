/*
** Copyright (c) 2009 D. Richard Hipp
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
** This file contains code to implement the "finfo" command.
*/
#include "config.h"
#include "finfo.h"

/*
** COMMAND: finfo
** 
** Usage: %fossil finfo FILENAME
**
** Print the change history for a single file.
**
** The "--limit N" and "--offset P" options limits the output to the first
** N changes after skipping P changes.
*/
void finfo_cmd(void){
  Stmt q;
  int vid;
  Blob dest;
  const char *zFilename;
  const char *zLimit;
  const char *zOffset;
  int iLimit, iOffset;

  db_must_be_within_tree();
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_panic("no checkout to finfo files in");
  }
  zLimit = find_option("limit",0,1);
  iLimit = zLimit ? atoi(zLimit) : -1;
  zOffset = find_option("offset",0,1);
  iOffset = zOffset ? atoi(zOffset) : 0;
  if (g.argc<3) {
    usage("FILENAME");
  }
  file_tree_name(g.argv[2], &dest, 1);
  zFilename = blob_str(&dest);
  db_prepare(&q,
    "SELECT b.uuid, ci.uuid, date(event.mtime,'localtime'),"
    "       coalesce(event.ecomment, event.comment),"
    "       coalesce(event.euser, event.user)"
    "  FROM mlink, blob b, event, blob ci"
    " WHERE mlink.fnid=(SELECT fnid FROM filename WHERE name=%Q)"
    "   AND b.rid=mlink.fid"
    "   AND event.objid=mlink.mid"
    "   AND event.objid=ci.rid"
    " ORDER BY event.mtime DESC LIMIT %d OFFSET %d",
    zFilename, iLimit, iOffset
  );
 
  printf("History of %s\n", zFilename);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFileUuid = db_column_text(&q, 0);
    const char *zCiUuid = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zCom = db_column_text(&q, 3);
    const char *zUser = db_column_text(&q, 4);
    char *zOut;
    printf("%s ", zDate);
    zOut = sqlite3_mprintf("[%.10s] %s (user: %s, artifact: [%.10s])",
                            zCiUuid, zCom, zUser, zFileUuid);
    comment_print(zOut, 11, 79);
    sqlite3_free(zOut);
  }
  db_finalize(&q);
  blob_reset(&dest);
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
    "SELECT substr(b.uuid,1,10), datetime(event.mtime,'localtime'),"
    "       coalesce(event.ecomment, event.comment),"
    "       coalesce(event.euser, event.user),"
    "       mlink.pid, mlink.fid, mlink.mid, mlink.fnid, ci.uuid"
    "  FROM mlink, blob b, event, blob ci"
    " WHERE mlink.fnid=(SELECT fnid FROM filename WHERE name=%Q)"
    "   AND b.rid=mlink.fid"
    "   AND event.objid=mlink.mid"
    "   AND event.objid=ci.rid"
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
    const char *zUuid = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zCom = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    int fpid = db_column_int(&q, 4);
    int frid = db_column_int(&q, 5);
    int mid = db_column_int(&q, 6);
    int fnid = db_column_int(&q, 7);
    const char *zCkin = db_column_text(&q,8);
    char zShort[20];
    char zShortCkin[20];
    if( memcmp(zDate, zPrevDate, 10) ){
      sprintf(zPrevDate, "%.10s", zDate);
      @ <tr><td colspan=3>
      @   <div class="divider">%s(zPrevDate)</div>
      @ </td></tr>
    }
    @ <tr><td valign="top">%s(&zDate[11])</td>
    @ <td width="20"></td>
    @ <td valign="top" align="left">
    sqlite3_snprintf(sizeof(zShort), zShort, "%.10s", zUuid);
    sqlite3_snprintf(sizeof(zShortCkin), zShortCkin, "%.10s", zCkin);
    if( g.okHistory ){
      @ <a href="%s(g.zTop)/artifact/%s(zUuid)">[%s(zShort)]</a>
    }else{
      @ [%s(zShort)]
    }
    @ part of check-in
    hyperlink_to_uuid(zShortCkin);
    @ %h(zCom) (By: 
    hyperlink_to_user(zUser, zDate, " on");
    hyperlink_to_date(zDate, ")");
    if( g.okHistory ){
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
