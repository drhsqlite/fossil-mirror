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
    @ <a href="%s(g.zBaseURL)/info/%s(zUuid)">[%s(zShortUuid)]</a>
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
** should return these columns:
**
**    0.  rid
**    1.  UUID
**    2.  Date/Time
**    3.  Comment string
**    4.  User
**    5.  Number of non-merge children
**    6.  Number of parents
**    7.  True if is a leaf
**    8.  background color
**    9.  type ("ci", "w")
*/
void www_print_timeline(
  Stmt *pQuery,
  int *pFirstEvent,
  int *pLastEvent,
  int (*xCallback)(int, Blob*),
  Blob *pArg
 ){
  int cnt = 0;
  int wikiFlags;
  int mxWikiLen;
  Blob comment;
  char zPrevDate[20];
  zPrevDate[0] = 0;

  mxWikiLen = db_get_int("timeline-max-comment", 0);
  if( db_get_boolean("timeline-block-markup", 0) ){
    wikiFlags = WIKI_INLINE;
  }else{
    wikiFlags = WIKI_INLINE | WIKI_NOBLOCK;
  }

  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS seen(rid INTEGER PRIMARY KEY);"
     "DELETE FROM seen;"
  );
  @ <table cellspacing=0 border=0 cellpadding=0>
  blob_zero(&comment);
  while( db_step(pQuery)==SQLITE_ROW ){
    int rid = db_column_int(pQuery, 0);
    const char *zUuid = db_column_text(pQuery, 1);
    int nPChild = db_column_int(pQuery, 5);
    int nParent = db_column_int(pQuery, 6);
    int isLeaf = db_column_int(pQuery, 7);
    const char *zBgClr = db_column_text(pQuery, 8);
    const char *zDate = db_column_text(pQuery, 2);
    const char *zType = db_column_text(pQuery, 9);
    const char *zUser = db_column_text(pQuery, 4);
    if( cnt==0 && pFirstEvent ){
      *pFirstEvent = rid;
    }
    cnt++;
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
      @   <div class="divider">%s(zPrevDate)</div>
      @ </td></tr>
    }
    @ <tr>
    @ <td valign="top">%s(&zDate[11])</td>
    @ <td width="20" align="center" valign="top">
    @ <font id="m%d(rid)" size="+1" color="white">*</font></td>
    if( zBgClr && zBgClr[0] ){
      @ <td valign="top" align="left" bgcolor="%h(zBgClr)">
    }else{
      @ <td valign="top" align="left">
    }
    if( zType[0]=='c' ){
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
    }else{
      hyperlink_to_uuid(zUuid);
    }
    db_column_blob(pQuery, 3, &comment);
    if( mxWikiLen>0 && blob_size(&comment)>mxWikiLen ){
      Blob truncated;
      blob_zero(&truncated);
      blob_append(&truncated, blob_buffer(&comment), mxWikiLen);
      blob_append(&truncated, "...", 3);
      wiki_convert(&truncated, 0, wikiFlags);
      blob_reset(&truncated);
    }else{
      wiki_convert(&comment, 0, wikiFlags);
    }
    blob_reset(&comment);
    @ (by %h(zUser))</td></tr>
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
** Create a temporary table suitable for storing timeline data.
*/
static void timeline_temp_table(void){
  static const char zSql[] = 
    @ CREATE TEMP TABLE IF NOT EXISTS timeline(
    @   rid INTEGER PRIMARY KEY,
    @   uuid TEXT,
    @   timestamp TEXT,
    @   comment TEXT,
    @   user TEXT,
    @   nchild INTEGER,
    @   nparent INTEGER,
    @   isleaf BOOLEAN,
    @   bgcolor TEXT,
    @   etype TEXT
    @ )
  ;
  db_multi_exec(zSql);
}

/*
** Return a pointer to a constant string that forms the basis
** for a timeline query for the WWW interface.
*/
const char *timeline_query_for_www(void){
  static const char zBaseSql[] =
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   datetime(event.mtime,'localtime') AS timestamp,
    @   coalesce(ecomment, comment),
    @   coalesce(euser, user),
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim=1),
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid),
    @   NOT EXISTS (SELECT 1 FROM plink WHERE pid=blob.rid),
    @   coalesce(bgcolor, brbgcolor),
    @   event.type
    @  FROM event JOIN blob 
    @ WHERE blob.rid=event.objid
  ;
  return zBaseSql;
}

/*
** WEBPAGE: ntimeline
**
** Query parameters:
**
**    a=TIMESTAMP    after this date
**    b=TIMESTAMP    before this date.
**    n=COUNT        number of events in output
**    p=RID          artifact RID and up to COUNT parents and ancestors
**    d=RID          artifact RID and up to COUNT descendents
**    u=USER         only if belonging to this user
**    y=TYPE         'ci', 'w', 'tkt'
**
** p= and d= can appear individually or together.  If either p= or d=
** appear, then u=, y=, a=, and b= are ignored.
**
** If a= and b= appear, only a= is used.  If neither appear, the most
** recent events are choosen.
**
** If n= is missing, the default count is 20.
*/
void page_ntimeline(void){
  Stmt q;                            /* Query used to generate the timeline */
  Blob sql;                          /* text of SQL used to generate timeline */
  Blob desc;                         /* Description of the timeline */
  int nEntry = atoi(PD("n","20"));   /* Max number of entries on timeline */
  int p_rid = atoi(PD("p","0"));     /* artifact p and its parents */
  int d_rid = atoi(PD("d","0"));     /* artifact d and its descendents */
  const char *zUser = P("u");        /* All entries by this user if not NULL */
  const char *zType = P("y");        /* Type of events.  All if NULL */
  const char *zAfter = P("a");       /* Events after this time */
  const char *zBefore = P("b");      /* Events before this time */
  Blob scriptInit;

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
    @ historical information if you <a href="%s(g.zTop)/login">login</a>.</p>
  }
  timeline_temp_table();
  blob_zero(&sql);
  blob_zero(&desc);
  blob_append(&sql, "INSERT OR IGNORE INTO timeline ", -1);
  blob_append(&sql, timeline_query_for_www(), -1);
  if( p_rid || d_rid ){
    /* If p= or d= is present, ignore all other parameters other than n= */
    char *zUuid;
    int np, nd;

    if( p_rid && d_rid && p_rid!=d_rid ) p_rid = d_rid;
    db_multi_exec(
       "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY)"
    );
    zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d",
                         p_rid ? p_rid : d_rid);
    blob_appendf(&sql, " AND event.objid IN ok");
    nd = 0;
    if( d_rid ){
      compute_descendents(d_rid, nEntry);
      nd = db_int(0, "SELECT count(*)-1 FROM ok");
      if( nd>0 ){
        db_multi_exec("%s", blob_str(&sql));
        blob_appendf(&desc, "%d descendents", nd);
      }
      db_multi_exec("DELETE FROM ok");
    }
    if( p_rid ){
      compute_ancestors(p_rid, nEntry);
      np = db_int(0, "SELECT count(*)-1 FROM ok");
      if( np>0 ){
        if( nd>0 ) blob_appendf(&desc, " and ");
        blob_appendf(&desc, "%d ancestors", np);
        db_multi_exec("%s", blob_str(&sql));
      }
    }
    blob_appendf(&desc, " of <a href='%s/info/%s'>[%.10s]</a>",
                 g.zBaseURL, zUuid, zUuid);
    db_prepare(&q, "SELECT * FROM timeline ORDER BY timestamp DESC");
  }else{
    const char *zEType = "event";
    if( zType ){
      blob_appendf(&sql, " AND event.type=%Q", zType);
      if( zType[0]=='c' ){
        zEType = "checkin";
      }else if( zType[0]=='w' ){
        zEType = "wiki edit";
      }else if( zType[0]=='t' ){
        zEType = "ticket change";
      }
    }
    blob_appendf(&desc, "Timeline of up to %d %s", nEntry, zEType);
    if( zUser ){
      blob_appendf(&sql, " AND event.user=%Q", zUser);
      blob_appendf(&desc, " by user %h", zUser);
    }
    if( zAfter ){
      while( isspace(zAfter[0]) ){ zAfter++; }
      if( zAfter[0] ){
        blob_appendf(&sql, 
           " AND event.mtime>=(SELECT julianday(%Q, 'utc'))"
           " ORDER BY event.mtime ASC", zAfter);
        blob_appendf(&desc, " occurring on or after %h", zAfter);
        zBefore = 0;
      }
    }else if( zBefore ){
      while( isspace(zBefore[0]) ){ zBefore++; }
      if( zBefore[0] ){
        blob_appendf(&sql, 
           " AND event.mtime<=(SELECT julianday(%Q, 'utc'))"
           " ORDER BY event.mtime DESC", zBefore);
        blob_appendf(&desc, " occurring on or before %h", zBefore);
      }
    }else{
      blob_appendf(&sql, " ORDER BY event.mtime DESC");
    }
    blob_appendf(&sql, " LIMIT %d", nEntry);
    db_multi_exec("%s", blob_str(&sql));
  }
  blob_zero(&sql);
  db_prepare(&q, "SELECT * FROM timeline ORDER BY timestamp DESC");
  @ <h2>%b(&desc)</h2>
  blob_reset(&desc);
  blob_zero(&scriptInit);
  www_print_timeline(&q, 0, 0, save_parentage_javascript, &scriptInit);
  db_finalize(&q);
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
  style_footer();
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
**    y=TYPE         show only TYPE ('ci' or 'w')       dflt: nil
**    s              show the SQL                       dflt: nil
*/
void page_timeline(void){
  Stmt q;           
  Blob sql;                    /* text of SQL used to generate timeline */
  char *zSQL;                  /* Rendered copy of sql */
  Blob scriptInit;
  char zDate[100];
  const char *zStart = P("d");       /* Starting date */
  int nEntry = atoi(PD("n","20"));   /* Max number of entries on timeline */
  const char *zUser = P("u");        /* All entries by this user if not NULL */
  int objid = atoi(PD("e","0"));     /* Entries related to this event */
  int relatedEvents = P("r")!=0;     /* Must be directly related to of objid */
  int afterFlag = P("a")!=0;         /* After objid if true */
  const char *zType = P("y");        /* Type of events.  All if NULL */
  int firstEvent;              /* First event displayed */
  int lastEvent;               /* Last event displayed */
  Blob desc;                   /* Human readable description of the timeline */
  const char *zEType;          /* Human readable event type */

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
    @ historical information if you <a href="%s(g.zTop)/login">login</a>.</p>
  }
  blob_zero(&sql);
  blob_zero(&desc);
  blob_append(&sql, timeline_query_for_www(), -1);
  zEType = "events";
  if( zType ){
    blob_appendf(&sql, " AND event.type=%Q", zType);
    if( zType[0]=='c' ){
      zEType = "checkins";
    }else if( zType[0]=='w' ){
      zEType = "wiki edits";
    }
  }
  blob_appendf(&desc, "Timeline of up to %d %s", nEntry, zEType);
  if( zUser ){
    blob_appendf(&sql, " AND event.user=%Q", zUser);
    blob_appendf(&desc, " by user %h", zUser);
  }
  if( objid ){
    char *z = db_text(0, "SELECT datetime(event.mtime, 'localtime') FROM event"
                         " WHERE objid=%d", objid);
    if( z ){
      zStart = z;
    }
  }
  if( zStart ){
    while( isspace(zStart[0]) ){ zStart++; }
    if( zStart[0] ){
      blob_appendf(&sql, 
         " AND event.mtime %s (SELECT julianday(%Q, 'utc'))",
                          afterFlag ? ">=" : "<=", zStart);
      blob_appendf(&desc, " occurring on or %s %h",
          afterFlag ? "after": "before",
          zStart);
    }
  }
  if( relatedEvents && objid ){
    char *zUuid;
    db_multi_exec(
       "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY)"
    );
    zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", objid);
    if( afterFlag ){
      compute_descendents(objid, nEntry);
      blob_appendf(&desc,
         " and decended from <a href='%s/vinfo/%d'>[%.10s]</a>",
         g.zBaseURL, objid, zUuid);
    }else{
      compute_ancestors(objid, nEntry);
      blob_appendf(&desc,
         " and a ancestor of <a href='%s/vinfo/%d'>[%.10s]</a>",
         g.zBaseURL, objid, zUuid);
    }
    blob_append(&sql, " AND event.objid IN ok", -1);
  }
  if( afterFlag ){
    blob_appendf(&sql, " ORDER BY event.mtime ASC LIMIT %d",
                 nEntry);
  }else{
    blob_appendf(&sql, " ORDER BY event.mtime DESC LIMIT %d",
                 nEntry);
  }
  zSQL = blob_str(&sql);
  if( afterFlag ){
    zSQL = mprintf("SELECT * FROM (%s) ORDER BY timestamp DESC", zSQL);
  }
  db_prepare(&q, zSQL);
  if( P("s")!=0 ){
    @ <hr><p>%h(zSQL)</p><hr>
  }
  @ <h2>%b(&desc)</h2>
  blob_reset(&desc);
  blob_zero(&sql);
  if( afterFlag ){
    free(zSQL);
  }
  zDate[0] = 0;
  blob_zero(&scriptInit);
  zDate[0] = 0;
  www_print_timeline(&q, &firstEvent, &lastEvent,
                     save_parentage_javascript, &scriptInit);
  db_finalize(&q);
  /* @ <p>firstEvent=%d(firstEvent) lastEvent=%d(lastEvent)</p> */
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
  @ <table><tr><td>
  @ <form method="GET" action="%s(g.zBaseURL)/timeline">
  @ <input type="hidden" value="%d(lastEvent)" name="e">
  @ <input type="hidden" value="%d(nEntry)" name="n">
  @ <input type="submit" value="Next %d(nEntry) Rows">
  @ </form></td><td>
  @ <form method="GET" action="%s(g.zBaseURL)/timeline">
  @ <input type="hidden" value="%d(firstEvent)" name="e">
  @ <input type="hidden" value="%d(nEntry)" name="n">
  @ <input type="hidden" value="1" name="a">
  @ <input type="submit" value="Previous %d(nEntry) Rows">
  @ </form></td></tr></table>
  style_footer();
}

/*
** The input query q selects various records.  Print a human-readable
** summary of those records.
**
** Limit the number of entries printed to nLine.
** 
** The query should return these columns:
**
**    0.  rid
**    1.  uuid
**    2.  Date/Time
**    3.  Comment string and user
**    4.  Number of non-merge children
**    5.  Number of parents
*/
void print_timeline(Stmt *q, int mxLine){
  int nLine = 0;
  char zPrevDate[20];
  const char *zCurrentUuid=0;
  Stmt currentQ;
  int rid = db_lget_int("checkout", 0);
  zPrevDate[0] = 0;

  db_prepare(&currentQ,
    "SELECT uuid"
    "  FROM blob WHERE rid=%d", rid
  );
  if( db_step(&currentQ)==SQLITE_ROW ){
    zCurrentUuid = db_column_text(&currentQ, 0);
  }

  while( db_step(q)==SQLITE_ROW && nLine<=mxLine ){
    const char *zId = db_column_text(q, 1);
    const char *zDate = db_column_text(q, 2);
    const char *zCom = db_column_text(q, 3);
    int nChild = db_column_int(q, 4);
    int nParent = db_column_int(q, 5);
    char *zFree = 0;
    int n = 0;
    char zPrefix[80];
    char zUuid[UUID_SIZE+1];
    
    sprintf(zUuid, "%.10s", zId);
    if( memcmp(zDate, zPrevDate, 10) ){
      printf("=== %.10s ===\n", zDate);
      memcpy(zPrevDate, zDate, 10);
      nLine++;
    }
    if( zCom==0 ) zCom = "";
    printf("%.8s ", &zDate[11]);
    zPrefix[0] = 0;
    if( nParent>1 ){
      sqlite3_snprintf(sizeof(zPrefix), zPrefix, "*MERGE* ");
      n = strlen(zPrefix);
    }
    if( nChild>1 ){
      sqlite3_snprintf(sizeof(zPrefix)-n, &zPrefix[n], "*FORK* ");
      n = strlen(zPrefix);
    }
    if( strcmp(zCurrentUuid,zId)==0 ){
      sqlite3_snprintf(sizeof(zPrefix)-n, &zPrefix[n], "*CURRENT* ");
      n += strlen(zPrefix);
    }
    zFree = sqlite3_mprintf("[%.10s] %s%s", zUuid, zPrefix, zCom);
    nLine += comment_print(zFree, 9, 79);
    sqlite3_free(zFree);
  }
  db_finalize(&currentQ);
}

/*
** Return a pointer to a static string that forms the basis for
** a timeline query for display on a TTY.
*/
const char *timeline_query_for_tty(void){
  static const char zBaseSql[] = 
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   datetime(event.mtime,'localtime'),
    @   coalesce(ecomment,comment) || ' (by ' || coalesce(euser,user,'?') ||')',
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid)
    @ FROM event, blob
    @ WHERE blob.rid=event.objid
  ;
  return zBaseSql;
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
    zDate = mprintf("(SELECT datetime('now'))");
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
  zSQL = mprintf("%s AND event.mtime %s %s",
     timeline_query_for_tty(),
     (mode==1 || mode==4) ? "<=" : ">=",
     zDate
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
