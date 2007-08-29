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
** Generate a hyperlink that invokes javascript to highlight
** a version on mouseover.
*/
void hyperlink_to_uuid_with_highlight(const char *zUuid, int id){
  char zShortUuid[UUID_SIZE+1];
  sprintf(zShortUuid, "%.10s", zUuid);
  if( g.okHistory ){
    @ <a onmouseover='hilite("m%d(id)")' onmouseout='unhilite("m%d(id)")'
    @    href="%s(g.zBaseURL)/vinfo/%s(zUuid)">[%s(zShortUuid)]</a>
  }else{
    @ <b onmouseover='hilite("m%d(id)")' onmouseout='unhilite("m%d(id)")'>
    @ [%s(zShortUuid)]</b>
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
**    0.  rid
**    1.  UUID
**    2.  Date/Time
**    3.  Comment string
**    4.  User
**    5.  Number of non-merge children
**    6.  Number of parents
**    7.  True if is a leaf
*/
void www_print_timeline(
  Stmt *pQuery,
  char *zLastDate,
  int (*xCallback)(int, Blob*),
  Blob *pArg
 ){
  char zPrevDate[20];
  zPrevDate[0] = 0;
  @ <table cellspacing=0 border=0 cellpadding=0>
  while( db_step(pQuery)==SQLITE_ROW ){
    int rid = db_column_int(pQuery, 0);
    int nPChild = db_column_int(pQuery, 5);
    int nParent = db_column_int(pQuery, 6);
    int isLeaf = db_column_int(pQuery, 7);
    const char *zDate = db_column_text(pQuery, 2);
    if( xCallback ){
      xCallback(rid, pArg);
    }
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
    @ <tr id="m%d(rid)" onmouseover='xin("m%d(rid)")'
    @     onmouseout='xout("m%d(rid)")'>
    @ <td valign="top">%s(&zDate[11])</td>
    @ <td width="20"></td>
    @ <td valign="top" align="left">
    hyperlink_to_uuid(db_column_text(pQuery,1));
    @ %h(db_column_text(pQuery,3))
    if( nParent>1 ){
      Stmt q;
      @ <b>Merge</b> from
      db_prepare(&q,
        "SELECT rid, uuid FROM plink, blob"
        " WHERE plink.cid=%d AND blob.rid=plink.pid AND plink.isprim=0",
        rid
      );
      while( db_step(&q)==SQLITE_ROW ){
        int mrid = db_column_int(&q, 0);
        const char *zUuid = db_column_text(&q, 1);
        hyperlink_to_uuid_with_highlight(zUuid, mrid);
      }
      db_finalize(&q);
    }
    if( nPChild>1 ){
      Stmt q;
      @ <b>Fork</b> to
      db_prepare(&q,
        "SELECT rid, uuid FROM plink, blob"
        " WHERE plink.pid=%d AND blob.rid=plink.cid AND plink.isprim>0",
        rid
      );
      while( db_step(&q)==SQLITE_ROW ){
        int frid = db_column_int(&q, 0);
        const char *zUuid = db_column_text(&q, 1);
        hyperlink_to_uuid_with_highlight(zUuid, frid);
      }
      db_finalize(&q);
    }
    if( isLeaf ){
      @ <b>Leaf</b>
    }
    @ (by %h(db_column_text(pQuery,4)))</td></tr>
    if( zLastDate ){
      strcpy(zLastDate, zDate);
    }
  }
  @ </table>
}

/*
** Generate javascript code that records the parents and children
** of the version rid.
*/
static int save_parentage_javascript(int rid, Blob *pOut){
  const char *zSep;
  Stmt q;

  db_prepare(&q, "SELECT pid FROM plink WHERE cid=%d", rid);
  zSep = "";
  blob_appendf(pOut, "parentof[\"m%d\"] = [", rid);
  while( db_step(&q)==SQLITE_ROW ){
    int pid = db_column_int(&q, 0);
    blob_appendf(pOut, "%s\"m%d\"", zSep, pid);
    zSep = ",";
  }
  db_finalize(&q);
  blob_appendf(pOut, "];\n");
  db_prepare(&q, "SELECT cid FROM plink WHERE pid=%d", rid);
  zSep = "";
  blob_appendf(pOut, "childof[\"m%d\"] = [", rid);
  while( db_step(&q)==SQLITE_ROW ){
    int pid = db_column_int(&q, 0);
    blob_appendf(pOut, "%s\"m%d\"", zSep, pid);
    zSep = ",";
  }
  db_finalize(&q);
  blob_appendf(pOut, "];\n");
  return 0;
}

/*
** WEBPAGE: timeline
*/
void page_timeline(void){
  Stmt q;
  char *zSQL;
  Blob scriptInit;
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
    "SELECT blob.rid, uuid, datetime(event.mtime,'localtime'), comment, user,"
    "       (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim=1),"
    "       (SELECT count(*) FROM plink WHERE cid=blob.rid),"
    "       NOT EXISTS (SELECT 1 FROM plink WHERE pid=blob.rid)"
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
  blob_zero(&scriptInit);
  www_print_timeline(&q, zDate, save_parentage_javascript, &scriptInit);
  db_finalize(&q);
  if( zStart==0 ){
    zStart = zDate;
  }
  @ <script>
  @ var parentof = new Object();
  @ var childof = new Object();
  cgi_append_content(blob_buffer(&scriptInit), blob_size(&scriptInit));
  blob_reset(&scriptInit);
  @ function setall(value){
  @   for(var x in parentof){
  @     setone(x,value);
  @   }
  @ }
  @ function setone(id, onoff){
  @   if( parentof[id]==null ) return 0;
  @   var w = document.getElementById(id);
  @   var clr = onoff==1 ? "#e0e0ff" : "#ffffff";
  @   if( w.backgroundColor==clr ){
  @     return 0
  @   }else{
  @     w.style.backgroundColor = clr
  @     return 1
  @   }
  @ }
  @ function xin(id) {
  @   setall(0);
  @   setone(id,1);
  @   set_children(id);
  @   set_parents(id);
  @ }
  @ function xout(id) {
  @   setall(0);
  @ }
  @ function set_parents(id){
  @   var plist = parentof[id];
  @   if( plist==null ) return;
  @   for(var x in plist){
  @     var pid = plist[x];
  @     if( setone(pid,1)==1 ){
  @       set_parents(pid);
  @     }
  @   }
  @ }
  @ function set_children(id){
  @   var clist = childof[id];
  @   if( clist==null ) return;
  @   for(var x in clist){
  @     var cid = clist[x];
  @     if( setone(cid,1)==1 ){
  @       set_children(cid);
  @     }
  @   }
  @ }
  @ function hilite(id) {
  @   var x = document.getElementById(id);
  @   x.style.color = "#ff0000";
  @ }
  @ function unhilite(id) {
  @   var x = document.getElementById(id);
  @   x.style.color = "#000000";
  @ }
  @ </script>
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
    const char *zId = db_column_text(q, 1);
    const char *zDate = db_column_text(q, 2);
    const char *zCom = db_column_text(q, 3);
    int nChild = db_column_int(q, 4);
    int nParent = db_column_int(q, 5);
    char *zFree = 0;
    char zUuid[UUID_SIZE+1];

    sprintf(zUuid, "%.10s", zId);
    if( memcmp(zDate, zPrevDate, 10) ){
      printf("=== %.10s ===\n", zDate);
      memcpy(zPrevDate, zDate, 10);
      nLine++;
    }
    if( zCom==0 ) zCom = "";
    printf("%.5s [%.10s] ", &zDate[11], zUuid);
    if( nChild>1 || nParent>1 ){
      int n = 0;
      char zPrefix[50];
      if( nParent>1 ){
        sqlite3_snprintf(sizeof(zPrefix), zPrefix, "*MERGE* ");
        n = strlen(zPrefix);
      }
      if( nChild>1 ){
        sqlite3_snprintf(sizeof(zPrefix)-n, &zPrefix[n], "*FORK* ");
        n = strlen(zPrefix);
      }
      zCom = zFree = sqlite3_mprintf("%s%s", zPrefix, zCom);
    }
    nLine += comment_print(zCom, 19, 79);
    sqlite3_free(zFree);
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
  const char *zCount;
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
    "SELECT blob.rid, uuid, datetime(event.mtime,'localtime'),"
    "       comment || ' (by ' || user || ')',"
    "       (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),"
    "       (SELECT count(*) FROM plink WHERE cid=blob.rid)"
    "  FROM event, blob"
    " WHERE event.type='ci' AND blob.rid=event.objid"
    "   AND event.mtime<=(SELECT julianday(%Q,'utc'))"
    " ORDER BY event.mtime DESC", zDate
  );
  print_timeline(&q, n);
  db_finalize(&q);
}
