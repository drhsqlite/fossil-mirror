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
** This file contains code to implement the timeline web page
**
*/
#include "config.h"
#include "timeline.h"

/*
** Generate a hyperlink to a version.
*/
void hyperlink_to_uuid(const char *zUuid){
  char zShortUuid[UUID_SIZE+1];
  sprintf(zShortUuid, "%.10s", zUuid);
  if( g.okHistory ){
    @ <a href="%s(g.zBaseURL)/vinfo/%s(zUuid)">[%s(zShortUuid)]</a>
  }else{
    @ <b>[%s(zShortUuid)]</b>
  }
}

/*
** Generate a hyperlink to a diff between two versions.
*/
void hyperlink_to_diff(const char *zV1, const char *zV2){
  if( g.okHistory ){
    if( zV2==0 ){
      @ <a href="%s(g.zBaseURL)/diff?v2=%s(zV1)">[diff]</a>
    }else{
      @ <a href="%s(g.zBaseURL)/diff?v1=%s(zV1)&v2=%s(zV2)">[diff]</a>
    }
  }
}

/*
** Output a timeline in the web format given a query.  The query
** should return 4 columns:
**
**    0.  UUID
**    1.  Date/Time
**    2.  Comment string
**    3.  User
*/
void www_print_timeline(Stmt *pQuery){
  char zPrevDate[20];
  zPrevDate[0] = 0;
  @ <table cellspacing=0 border=0 cellpadding=0>
  while( db_step(pQuery)==SQLITE_ROW ){
    const char *zDate = db_column_text(pQuery, 1);
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
    hyperlink_to_uuid(db_column_text(pQuery,0));
    @ %h(db_column_text(pQuery,2)) (by %h(db_column_text(pQuery,3)))</td>
  }
  @ </table>
}



/*
** WEBPAGE: timeline
*/
void page_timeline(void){
  Stmt q;

  /* To view the timeline, must have permission to read project data.
  */
  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }

  style_header("Timeline");
  db_prepare(&q,
    "SELECT uuid, datetime(event.mtime,'localtime'), comment, user"
    "  FROM event, blob"
    " WHERE event.type='ci' AND blob.rid=event.objid"
    " ORDER BY event.mtime DESC"
  );
  www_print_timeline(&q);
  db_finalize(&q);
  style_footer();
}
/*
** The input query q selects various records.  Print a human-readable
** summary of those records.
**
** Limit the number of entries printed to nLine.
*/
void print_timeline(Stmt *q, int mxLine){
  int nLine = 0;
  char zPrevDate[20];
  zPrevDate[0] = 0;

  while( db_step(q)==SQLITE_ROW && nLine<=mxLine ){
    const char *zId = db_column_text(q, 0);
    const char *zDate = db_column_text(q, 1);
    const char *zCom = db_column_text(q, 2);
    char zUuid[UUID_SIZE+1];

    sprintf(zUuid, "%.10s", zId);
    if( memcmp(zDate, zPrevDate, 10) ){
      printf("=== %.10s ===\n", zDate);
      memcpy(zPrevDate, zDate, 10);
      nLine++;
    }
    if( zCom==0 ) zCom = "";
    printf("%.5s [%.10s] ", &zDate[11], zUuid);
    nLine += comment_print(zCom, 19, 79);
  }
}


/*
** COMMAND: timeline
**
** The timeline command works very much like the timeline webpage, but
** shows much less data and has fewer configuration options.  It is
** intended as a convenient shortcut for the common case of seeing
** recent changes.
*/
void timeline_cmd(void){
  Stmt q;
  db_must_be_within_tree();
  db_prepare(&q,
    "SELECT uuid, datetime(event.mtime,'localtime'),"
    "       comment || ' (by ' || user || ')'"
    "  FROM event, blob"
    " WHERE event.type='ci' AND blob.rid=event.objid"
    " ORDER BY event.mtime DESC"
  );
  print_timeline(&q, 20);
  db_finalize(&q);
}
