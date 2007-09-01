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
#include <string.h>
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
void hyperlink_to_uuid_with_mouseover(
  const char *zUuid,   /* The UUID to display */
  const char *zIn,     /* Javascript proc for mouseover */
  const char *zOut,    /* Javascript proc for mouseout */
  int id               /* Argument to javascript procs */
){
  char zShortUuid[UUID_SIZE+1];
  sprintf(zShortUuid, "%.10s", zUuid);
  if( g.okHistory ){
    @ <a onmouseover='%s(zIn)("m%d(id)")' onmouseout='%s(zOut)("m%d(id)")'
    @    href="%s(g.zBaseURL)/vinfo/%s(zUuid)">[%s(zShortUuid)]</a>
  }else{
    @ <b onmouseover='%s(zIn)("m%d(id)")' onmouseout='%s(zOut)("m%d(id)")'>
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
  int *pFirstEvent,
  int *pLastEvent,
  int (*xCallback)(int, Blob*),
  Blob *pArg
 ){
  char zPrevDate[20];
  zPrevDate[0] = 0;
  int cnt = 0;
  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS seen(rid INTEGER PRIMARY KEY);"
     "DELETE FROM seen;"
  );
  @ <table cellspacing=0 border=0 cellpadding=0>
  while( db_step(pQuery)==SQLITE_ROW ){
    int rid = db_column_int(pQuery, 0);
    const char *zUuid = db_column_text(pQuery, 1);
    int nPChild = db_column_int(pQuery, 5);
    int nParent = db_column_int(pQuery, 6);
    int isLeaf = db_column_int(pQuery, 7);
    const char *zDate = db_column_text(pQuery, 2);
    if( cnt==0 && pFirstEvent ){
      *pFirstEvent = rid;
    }
    if( pLastEvent ){
      *pLastEvent = rid;
    }
    db_multi_exec("INSERT OR IGNORE INTO seen VALUES(%d)", rid);
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
    @ <tr>
    @ <td valign="top">%s(&zDate[11])</td>
    @ <td width="20" align="center" valign="top">
    @ <font id="m%d(rid)" size="+1" color="white">*</font></td>
    @ <td valign="top" align="left">
    hyperlink_to_uuid_with_mouseover(zUuid, "xin", "xout", rid);
    if( nParent>1 ){
      @ <b>Merge</b> 
    }
    if( nPChild>1 ){
      @ <b>Fork</b>
    }
    if( isLeaf ){
      @ <b>Leaf</b>
    }
    @ %h(db_column_text(pQuery,3))
    @ (by %h(db_column_text(pQuery,4)))</td></tr>
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
**
** Query parameters:
**
**    d=STARTDATE    date in iso8601 notation.          dflt: newest event
**    n=INTEGER      number of events to show.          dflt: 25
**    e=INTEGER      starting event id.                 dflt: nil
**    u=NAME         show only events from user.        dflt: nil
**    a              show events after and including.   dflt: false
**    r              show only related events.          dflt: false
*/
void page_timeline(void){
  Stmt q;
  char *zSQL;
  Blob scriptInit;
  char zDate[100];
  const char *zStart = P("d");
  int nEntry = atoi(PD("n","20"));
  const char *zUser = P("u");
  int objid = atoi(PD("e","0"));
  int relatedEvents = P("r")!=0;
  int afterFlag = P("a")!=0;
  int firstEvent;
  int lastEvent;

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
  if( zUser ){
    zSQL = mprintf("%z AND event.user=%Q", zSQL, zUser);
  }
  if( objid ){
    char *z = db_text(0, "SELECT datetime(event.mtime) FROM event"
                         " WHERE objid=%d", objid);
    if( z ){
      zStart = z;
    }
  }
  if( zStart ){
    while( isspace(zStart[0]) ){ zStart++; }
    if( zStart[0] ){
      zSQL = mprintf("%z AND event.mtime %s julianday(%Q, 'localtime')",
                      zSQL, afterFlag ? ">=" : "<=", zStart);
    }
  }
  if( relatedEvents && objid ){
    db_multi_exec(
       "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY)"
    );
    if( afterFlag ){
      compute_descendents(objid, nEntry);
    }else{
      compute_ancestors(objid, nEntry);
    }
    zSQL = mprintf("%z AND event.objid IN ok", zSQL);
  }
  zSQL = mprintf("%z ORDER BY event.mtime DESC LIMIT %d", zSQL, nEntry);
  db_prepare(&q, zSQL);
  free(zSQL);
  zDate[0] = 0;
  blob_zero(&scriptInit);
  zDate[0] = 0;
  www_print_timeline(&q, &firstEvent, &lastEvent,
                     save_parentage_javascript, &scriptInit);
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
  @ setall("#ffffff");
  @ function setone(id, clr){
  @   if( parentof[id]==null ) return 0;
  @   var w = document.getElementById(id);
  @   if( w.style.color==clr ){
  @     return 0
  @   }else{
  @     w.style.color = clr
  @     return 1
  @   }
  @ }
  @ function xin(id) {
  @   setall("#ffffff");
  @   setone(id,"#ff0000");
  @   set_children(id, "#b0b0b0");
  @   set_parents(id, "#b0b0b0");
  @   for(var x in parentof[id]){
  @     var pid = parentof[id][x]
  @     var w = document.getElementById(pid);
  @     if( w!=null ){
  @       w.style.color = "#000000";
  @     }
  @   }
  @   for(var x in childof[id]){
  @     var cid = childof[id][x]
  @     var w = document.getElementById(cid);
  @     if( w!=null ){
  @       w.style.color = "#000000";
  @     }
  @   }
  @ }
  @ function xout(id) {
  @   /* setall("#000000"); */
  @ }
  @ function set_parents(id, clr){
  @   var plist = parentof[id];
  @   if( plist==null ) return;
  @   for(var x in plist){
  @     var pid = plist[x];
  @     if( setone(pid,clr)==1 ){
  @       set_parents(pid,clr);
  @     }
  @   }
  @ }
  @ function set_children(id,clr){
  @   var clist = childof[id];
  @   if( clist==null ) return;
  @   for(var x in clist){
  @     var cid = clist[x];
  @     if( setone(cid,clr)==1 ){
  @       set_children(cid,clr);
  @     }
  @   }
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
** Usage: %fossil timeline ?WHEN? ?UUID|DATETIME? ?-n|--count N?
**
** Print a summary of activity going backwards in date and time
** specified or from the current date and time if no arguments
** are given.  Show as many as N (default 20) check-ins.  The
** WHEN argument can be any unique abbreviation of one of these
** keywords:
**
**     before
**     after
**     descendents | children
**     ancestors | parents
**
** The UUID can be any unique prefix of 4 characters or more.
** The DATETIME should be in the ISO8601 format.  For
** examples: "2007-08-18 07:21:21".  You can also say "current"
** for the current version or "now" for the current time.
*/
void timeline_cmd(void){
  Stmt q;
  int n, k;
  const char *zCount;
  char *zOrigin;
  char *zDate;
  char *zSQL;
  int objid = 0;
  Blob uuid;
  int mode = 1 ;       /* 1: before  2:after  3:children  4:parents */
  db_find_and_open_repository();
  zCount = find_option("n","count",1);
  if( zCount ){
    n = atoi(zCount);
  }else{
    n = 20;
  }
  if( g.argc==4 ){
    k = strlen(g.argv[2]);
    if( strncmp(g.argv[2],"before",k)==0 ){
      mode = 1;
    }else if( strncmp(g.argv[2],"after",k)==0 && k>1 ){
      mode = 2;
    }else if( strncmp(g.argv[2],"descendents",k)==0 ){
      mode = 3;
    }else if( strncmp(g.argv[2],"children",k)==0 ){
      mode = 3;
    }else if( strncmp(g.argv[2],"ancestors",k)==0 && k>1 ){
      mode = 4;
    }else if( strncmp(g.argv[2],"parents",k)==0 ){
      mode = 4;
    }else{
      usage("?WHEN? ?UUID|DATETIME?");
    }
    zOrigin = g.argv[3];
  }else if( g.argc==3 ){
    zOrigin = g.argv[2];
  }else{
    zOrigin = "now";
  }
  k = strlen(zOrigin);
  blob_zero(&uuid);
  blob_append(&uuid, zOrigin, -1);
  if( strcmp(zOrigin, "now")==0 ){
    if( mode==3 || mode==4 ){
      fossil_fatal("cannot compute descendents or ancestors of a date");
    }
    zDate = mprintf("(SELECT julianday('now','utc'))");
  }else if( strncmp(zOrigin, "current", k)==0 ){
    objid = db_lget_int("checkout",0);
    zDate = mprintf("(SELECT mtime FROM plink WHERE cid=%d)", objid);
  }else if( name_to_uuid(&uuid, 0)==0 ){
    objid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &uuid);
    zDate = mprintf("(SELECT mtime FROM plink WHERE cid=%d)", objid);
  }else{
    if( mode==3 || mode==4 ){
      fossil_fatal("cannot compute descendents or ancestors of a date");
    }
    zDate = mprintf("(SELECT julianday(%Q, 'utc'))", zOrigin);
  }
  zSQL = mprintf(
    "SELECT blob.rid, uuid, datetime(event.mtime,'localtime'),"
    "       comment || ' (by ' || user || ')',"
    "       (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),"
    "       (SELECT count(*) FROM plink WHERE cid=blob.rid)"
    "  FROM event, blob"
    " WHERE event.type='ci' AND blob.rid=event.objid"
    "   AND event.mtime %s %s",
    (mode==1 || mode==4) ? "<=" : ">=", zDate
  );
  if( mode==3 || mode==4 ){
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    if( mode==3 ){
      compute_descendents(objid, n);
    }else{
      compute_ancestors(objid, n);
    }
    zSQL = mprintf("%z AND blob.rid IN ok", zSQL);
  }
  zSQL = mprintf("%z ORDER BY event.mtime DESC", zSQL);
  db_prepare(&q, zSQL);
  print_timeline(&q, n);
  db_finalize(&q);
}
