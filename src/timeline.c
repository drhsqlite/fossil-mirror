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
void www_print_timeline(Stmt *pQuery, char *zLastDate){
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
    if( zLastDate ){
      strcpy(zLastDate, zDate);
    }
  }
  @ </table>
}



/*
** WEBPAGE: timeline
*/
void page_timeline(void){
  Stmt q;
  char *zSQL;
  char zDate[100];
  const char *zStart = P("d");
  int nEntry = atoi(PD("n","25"));

  /* To view the timeline, must have permission to read project data.
  */
  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }

  style_header("Timeline");
  if( !g.okHistory &&
      db_exists("SELECT 1 FROM user"
                " WHERE login='anonymous'"
                "   AND cap LIKE '%%h%%'") ){
    @ <p><b>Note:</b> You will be able to access <u>much</u> more
    @ historical information if <a href="%s(g.zBaseURL)/login">login</a>.</p>
  }
  zSQL = mprintf(
    "SELECT uuid, datetime(event.mtime,'localtime'), comment, user"
    "  FROM event, blob"
    " WHERE event.type='ci' AND blob.rid=event.objid"
  );
  if( zStart ){
    while( isspace(zStart[0]) ){ zStart++; }
    if( zStart[0] ){
      zSQL = mprintf("%z AND event.mtime<=julianday(%Q, 'localtime')",
                      zSQL, zStart);
    }
  }
  zSQL = mprintf("%z ORDER BY event.mtime DESC LIMIT %d", zSQL, nEntry);
  db_prepare(&q, zSQL);
  free(zSQL);
  zDate[0] = 0;
  www_print_timeline(&q, zDate);
  db_finalize(&q);
  if( zStart==0 ){
    zStart = zDate;
  }
  @ <hr>
  @ <form method="GET" action="%s(g.zBaseURL)/timeline">
  @ Start Date:
  @ <input type="text" size="30" value="%h(zStart)" name="d">
  @ Number Of Entries:  
  @ <input type="text" size="4" value="%d(nEntry)" name="n">
  @ <br><input type="submit" value="Submit">
  @ </form>
  @ <form method="GET" action="%s(g.zBaseURL)/timeline">
  @ <input type="hidden" value="%h(zDate)" name="d">
  @ <input type="hidden" value="%d(nEntry)" name="n">
  @ <input type="submit" value="Next %d(nEntry) Rows">
  @ </form>
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
** Usage: %fossil timeline ?DATETIME? ?-n|--count N?
**
** Print a summary of activity going backwards in date and time
** specified or from the current date and time if no arguments
** are given.  Show as many as N (default 20) check-ins.
**
** The date and time should be in the ISO8601 format.  For
** examples: "2007-08-18 07:21:21".  The time may be omitted.
** Times are according to the local timezone.
*/
void timeline_cmd(void){
  Stmt q;
  int n;
  char *zCount;
  char *zDate;
  db_find_and_open_repository();
  zCount = find_option("n","count",1);
  if( zCount ){
    n = atoi(zCount);
  }else{
    n = 20;
  }
  if( g.argc!=2 && g.argc!=3 ){
    usage("YYYY-MM-DDtHH:MM:SS");
  }
  if( g.argc==3 ){
    zDate = g.argv[2];
  }else{
    zDate = "now";
  }
  db_prepare(&q,
    "SELECT uuid, datetime(event.mtime,'localtime'),"
    "       comment || ' (by ' || user || ')'"
    "  FROM event, blob"
    " WHERE event.type='ci' AND blob.rid=event.objid"
    "   AND event.mtime<=(SELECT julianday(%Q,'utc'))"
    " ORDER BY event.mtime DESC", zDate
  );
  print_timeline(&q, n);
  db_finalize(&q);
}
