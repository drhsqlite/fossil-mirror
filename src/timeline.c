/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
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
#include <time.h>
#include "config.h"
#include "timeline.h"

/*
** Shorten a UUID so that is the minimum length needed to contain
** at least one digit in the range 'a'..'f'.  The minimum length is 10.
*/
static void shorten_uuid(char *zDest, const char *zSrc){
  int i;
  for(i=0; i<10 && zSrc[i]<='9'; i++){}
  memcpy(zDest, zSrc, 10);
  if( i==10 && zSrc[i] ){
    do{
      zDest[i] = zSrc[i];
      i++;
    }while( zSrc[i-1]<='9' );
  }else{
    i = 10;
  }
  zDest[i] = 0;
}


/*
** Generate a hyperlink to a version.
*/
void hyperlink_to_uuid(const char *zUuid){
  char zShortUuid[UUID_SIZE+1];
  shorten_uuid(zShortUuid, zUuid);
  if( g.okHistory ){
    @ <a class="timelineHistLink" href="%s(g.zBaseURL)/info/%s(zShortUuid)">
    @ [%s(zShortUuid)]</a>
  }else{
    @ <span class="timelineHistDsp">[%s(zShortUuid)]</span>
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
  shorten_uuid(zShortUuid, zUuid);
  if( g.okHistory ){
    @ <a onmouseover='%s(zIn)("m%d(id)")' onmouseout='%s(zOut)("m%d(id)")'
    @    href="%s(g.zBaseURL)/vinfo/%s(zShortUuid)">[%s(zShortUuid)]</a>
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
      @ <a href="%s(g.zBaseURL)/diff?v1=%s(zV1)&amp;v2=%s(zV2)">[diff]</a>
    }
  }
}

/*
** Generate a hyperlink to a date & time.
*/
void hyperlink_to_date(const char *zDate, const char *zSuffix){
  if( zSuffix==0 ) zSuffix = "";
  if( g.okHistory ){
    @ <a href="%s(g.zTop)/timeline?c=%T(zDate)">%s(zDate)</a>%s(zSuffix)
  }else{
    @ %s(zDate)%s(zSuffix)
  }
}

/*
** Generate a hyperlink to a user.  This will link to a timeline showing
** events by that user.  If the date+time is specified, then the timeline
** is centered on that date+time.
*/
void hyperlink_to_user(const char *zU, const char *zD, const char *zSuf){
  if( zSuf==0 ) zSuf = "";
  if( g.okHistory ){
    if( zD && zD[0] ){
      @ <a href="%s(g.zTop)/timeline?c=%T(zD)&amp;u=%T(zU)">%h(zU)</a>%s(zSuf)
    }else{
      @ <a href="%s(g.zTop)/timeline?u=%T(zU)">%h(zU)</a>%s(zSuf)
    }
  }else{
    @ %s(zU)
  }
}

/*
** Count the number of primary non-branch children for the given check-in.
**
** A primary child is one where the parent is the primary parent, not
** a merge parent.
**
** A non-branch child is one which is on the same branch as the parent.
*/
int count_nonbranch_children(int pid){
  int nNonBranch;
  static const char zSql[] = 
    @ SELECT count(*) FROM plink
    @  WHERE pid=%d AND isprim
    @    AND coalesce((SELECT value FROM tagxref
    @                   WHERE tagid=%d AND rid=plink.pid), 'trunk')
    @       =coalesce((SELECT value FROM tagxref
    @                   WHERE tagid=%d AND rid=plink.cid), 'trunk')
  ;
  nNonBranch = db_int(0, zSql, pid, TAG_BRANCH, TAG_BRANCH);
  return nNonBranch;
}

/*
** Allowed flags for the tmFlags argument to www_print_timeline
*/
#if INTERFACE
#define TIMELINE_ARTID    0x0001  /* Show artifact IDs on non-check-in lines */
#define TIMELINE_LEAFONLY 0x0002  /* Show "Leaf", but not "Merge", "Fork" etc */
#define TIMELINE_BRIEF    0x0004  /* Combine adjacent elements of same object */
#define TIMELINE_GRAPH    0x0008  /* Compute a graph */
#define TIMELINE_DISJOINT 0x0010  /* Elements are not contiguous */
#endif

/*
** Output a timeline in the web format given a query.  The query
** should return these columns:
**
**    0.  rid
**    1.  UUID
**    2.  Date/Time
**    3.  Comment string
**    4.  User
**    5.  True if is a leaf
**    6.  background color
**    7.  type ("ci", "w", "t")
**    8.  list of symbolic tags.
**    9.  tagid for ticket or wiki or event
**   10.  Short comment to user for repeated tickets and wiki
*/
void www_print_timeline(
  Stmt *pQuery,          /* Query to implement the timeline */
  int tmFlags,           /* Flags controlling display behavior */
  void (*xExtra)(int)    /* Routine to call on each line of display */
){
  int wikiFlags;
  int mxWikiLen;
  Blob comment;
  int prevTagid = 0;
  int suppressCnt = 0;
  char zPrevDate[20];
  GraphContext *pGraph = 0;

  zPrevDate[0] = 0;
  mxWikiLen = db_get_int("timeline-max-comment", 0);
  if( db_get_boolean("timeline-block-markup", 0) ){
    wikiFlags = WIKI_INLINE;
  }else{
    wikiFlags = WIKI_INLINE | WIKI_NOBLOCK;
  }
  if( tmFlags & TIMELINE_GRAPH ){
    pGraph = graph_init();
    /* style is not moved to css, because this is
    ** a technical div for the timeline graph
    */
    @ <div id="canvas" style="position:relative;width:1px;height:1px;"></div>
  }

  @ <table id="timelineTable" class="timelineTable">
  blob_zero(&comment);
  while( db_step(pQuery)==SQLITE_ROW ){
    int rid = db_column_int(pQuery, 0);
    const char *zUuid = db_column_text(pQuery, 1);
    int isLeaf = db_column_int(pQuery, 5);
    const char *zBgClr = db_column_text(pQuery, 6);
    const char *zDate = db_column_text(pQuery, 2);
    const char *zType = db_column_text(pQuery, 7);
    const char *zUser = db_column_text(pQuery, 4);
    const char *zTagList = db_column_text(pQuery, 8);
    int tagid = db_column_int(pQuery, 9);
    int commentColumn = 3;    /* Column containing comment text */
    char zTime[8];
    if( tagid ){
      if( tagid==prevTagid ){
        if( tmFlags & TIMELINE_BRIEF ){
          suppressCnt++;
          continue;
        }else{
          commentColumn = 10;
        }
      }
    }
    prevTagid = tagid;
    if( suppressCnt ){
      @ <tr><td /><td /><td>
      @ <span class="timelineDisabled">... %d(suppressCnt) similar
      @ event%s(suppressCnt>1?"s":"") omitted.</span></td></tr>
      suppressCnt = 0;
    }
    if( strcmp(zType,"div")==0 ){
      @ <tr><td colspan="3"><hr /></td></tr>
      continue;
    }
    if( memcmp(zDate, zPrevDate, 10) ){
      sprintf(zPrevDate, "%.10s", zDate);
      @ <tr><td>
      @   <div class="divider">%s(zPrevDate)</div>
      @ </td></tr>
    }
    memcpy(zTime, &zDate[11], 5);
    zTime[5] = 0;
    @ <tr>
    @ <td class="timelineTime">%s(zTime)</td>
    @ <td class="timelineGraph">
    if( pGraph && zType[0]=='c' ){
      int nParent = 0;
      int aParent[32];
      const char *zBr;
      int gidx;
      static Stmt qparent;
      static Stmt qbranch;
      db_static_prepare(&qparent,
        "SELECT pid FROM plink WHERE cid=:rid ORDER BY isprim DESC /*sort*/"
      );
      db_static_prepare(&qbranch,
        "SELECT value FROM tagxref WHERE tagid=%d AND tagtype>0 AND rid=:rid",
        TAG_BRANCH
      );
      db_bind_int(&qparent, ":rid", rid);
      while( db_step(&qparent)==SQLITE_ROW && nParent<32 ){
        aParent[nParent++] = db_column_int(&qparent, 0);
      }
      db_reset(&qparent);
      db_bind_int(&qbranch, ":rid", rid);
      if( db_step(&qbranch)==SQLITE_ROW ){
        zBr = db_column_text(&qbranch, 0);
      }else{
        zBr = "trunk";
      }
      gidx = graph_add_row(pGraph, rid, nParent, aParent, zBr, zBgClr);
      db_reset(&qbranch);
      @ <div id="m%d(gidx)"></div>
    }
    @</td>
    if( zBgClr && zBgClr[0] ){
      @ <td class="timelineTableCell" style="background-color: %h(zBgClr);">
    }else{
      @ <td class="timelineTableCell">
    }
    if( zType[0]=='c' ){
      hyperlink_to_uuid(zUuid);
      if( isLeaf ){
        if( db_exists("SELECT 1 FROM tagxref"
                      " WHERE rid=%d AND tagid=%d AND tagtype>0",
                      rid, TAG_CLOSED) ){
          @ <span class="timelineLeaf">Closed-Leaf:</span>
        }else{
          @ <span class="timelineLeaf">Leaf:</span>
        }
      }
    }else if( zType[0]=='e' && tagid ){
      hyperlink_to_event_tagid(tagid);
    }else if( (tmFlags & TIMELINE_ARTID)!=0 ){
      hyperlink_to_uuid(zUuid);
    }
    db_column_blob(pQuery, commentColumn, &comment);
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
    if( zTagList && zTagList[0] ){
      @ (user: %h(zUser), tags: %h(zTagList))
    }else{
      @ (user: %h(zUser))
    }
    if( xExtra ){
      xExtra(rid);
    }
    @ </td></tr>
  }
  if( suppressCnt ){
    @ <tr><td /><td /><td>
    @ <span class="timelineDisabled">... %d(suppressCnt) similar
    @ event%s(suppressCnt>1?"s":"") omitted.</span></td></tr>
    suppressCnt = 0;
  }
  if( pGraph ){
    graph_finish(pGraph, (tmFlags & TIMELINE_DISJOINT)!=0);
    if( pGraph->nErr ){
      graph_free(pGraph);
      pGraph = 0;
    }else{
      /* style is not moved to css, because this is
      ** a technical div for the timeline graph
      */
      @ <tr><td /><td>
      @ <div id="grbtm" style="width:%d(pGraph->mxRail*20+30)px;"></div>
      @ </td></tr>
    }
  }
  @ </table>
  timeline_output_graph_javascript(pGraph);
}

/*
** Generate all of the necessary javascript to generate a timeline
** graph.
*/
void timeline_output_graph_javascript(GraphContext *pGraph){
  if( pGraph && pGraph->nErr==0 ){
    GraphRow *pRow;
    int i;
    char cSep;
    @ <script  type="text/JavaScript">
    @ /* <![CDATA[ */
    cgi_printf("var rowinfo = [\n");
    for(pRow=pGraph->pFirst; pRow; pRow=pRow->pNext){
      cgi_printf("{id:\"m%d\",bg:\"%s\",r:%d,d:%d,mo:%d,mu:%d,u:%d,au:",
        pRow->idx,
        pRow->zBgClr,
        pRow->iRail,
        pRow->bDescender,
        pRow->mergeOut,
        pRow->mergeUpto,
        pRow->aiRaiser[pRow->iRail]
      );
      cSep = '[';
      for(i=0; i<GR_MAX_RAIL; i++){
        if( i==pRow->iRail ) continue;
        if( pRow->aiRaiser[i]>0 ){
          cgi_printf("%c%d,%d", cSep, i, pRow->aiRaiser[i]);
          cSep = ',';
        }
      }
      if( cSep=='[' ) cgi_printf("[");
      cgi_printf("],mi:");
      cSep = '[';
      for(i=0; i<GR_MAX_RAIL; i++){
        if( pRow->mergeIn & (1<<i) ){
          cgi_printf("%c%d", cSep, i);
          cSep = ',';
        }
      }
      if( cSep=='[' ) cgi_printf("[");
      cgi_printf("]}%s", pRow->pNext ? ",\n" : "];\n");
    }
    cgi_printf("var nrail = %d\n", pGraph->mxRail+1);
    graph_free(pGraph);
    @ var canvasDiv = document.getElementById("canvas");
    @ var realCanvas = null;
    @ function drawBox(color,x0,y0,x1,y1){
    @   var n = document.createElement("div");
    @   if( x0>x1 ){ var t=x0; x0=x1; x1=t; }
    @   if( y0>y1 ){ var t=y0; y0=y1; y1=t; }
    @   var w = x1-x0+1;
    @   var h = y1-y0+1;
    @   n.style.position = "absolute";
    @   n.style.overflow = "hidden";
    @   n.style.left = x0+"px";
    @   n.style.top = y0+"px";
    @   n.style.width = w+"px";
    @   n.style.height = h+"px";
    @   n.style.backgroundColor = color;
    @   canvasDiv.appendChild(n);
    @ }
    @ function absoluteY(id){
    @   var obj = document.getElementById(id);
    @   if( !obj ) return;
    @   var top = 0;
    @   if( obj.offsetParent ){
    @     do{
    @       top += obj.offsetTop;
    @     }while( obj = obj.offsetParent );
    @   }
    @   return top;
    @ }
    @ function absoluteX(id){
    @   var obj = document.getElementById(id);
    @   if( !obj ) return;
    @   var left = 0;
    @   if( obj.offsetParent ){
    @     do{
    @       left += obj.offsetLeft;
    @     }while( obj = obj.offsetParent );
    @   }
    @   return left;
    @ }
    @ function drawUpArrow(x,y0,y1){
    @   drawBox("black",x,y0,x+1,y1);
    @   if( y0+8>=y1 ){
    @     drawBox("black",x-1,y0+1,x+2,y0+2);
    @     drawBox("black",x-2,y0+3,x+3,y0+4);
    @   }else{
    @     drawBox("black",x-1,y0+2,x+2,y0+4);
    @     drawBox("black",x-2,y0+5,x+3,y0+7);
    @   }
    @ }
    @ function drawThinArrow(y,xFrom,xTo){
    @   if( xFrom<xTo ){
    @     drawBox("black",xFrom,y,xTo,y);
    @     drawBox("black",xTo-4,y-1,xTo-2,y+1);
    @     if( xTo>xFrom-8 ) drawBox("black",xTo-6,y-2,xTo-5,y+2);
    @   }else{
    @     drawBox("black",xTo,y,xFrom,y);
    @     drawBox("black",xTo+2,y-1,xTo+4,y+1);
    @     if( xTo+8<xFrom ) drawBox("black",xTo+5,y-2,xTo+6,y+2);
    @   }
    @ }
    @ function drawThinLine(x0,y0,x1,y1){
    @   drawBox("black",x0,y0,x1,y1);
    @ }
    @ function drawNode(p, left, btm){
    @   drawBox("black",p.x-5,p.y-5,p.x+6,p.y+6);
    @   drawBox(p.bg,p.x-4,p.y-4,p.x+5,p.y+5);
    @   if( p.u>0 ){
    @     var u = rowinfo[p.u-1];
    @     drawUpArrow(p.x, u.y+6, p.y-5);
    @   }
    @   if( p.d ){
    @     drawUpArrow(p.x, p.y+6, btm);
    @   } 
    @   if( p.mo>=0 ){
    @     var x1 = p.mo*20 + left;
    @     var y1 = p.y-3;
    @     var x0 = x1>p.x ? p.x+7 : p.x-6;
    @     var u = rowinfo[p.mu-1];
    @     var y0 = u.y+5;
    @     drawThinLine(x0,y1,x1,y1);
    @     drawThinLine(x1,y0,x1,y1);
    @   }
    @   var n = p.au.length;
    @   for(var i=0; i<n; i+=2){
    @     var x1 = p.au[i]*20 + left;
    @     var x0 = x1>p.x ? p.x+7 : p.x-6;
    @     drawBox("black",x0,p.y,x1,p.y+1);
    @     var u = rowinfo[p.au[i+1]-1];
    @     drawUpArrow(x1, u.y+6, p.y);
    @   }
    @   for(var j in p.mi){
    @     var y0 = p.y+5;
    @     var mx = p.mi[j]*20 + left;
    @     if( mx>p.x ){
    @       drawThinArrow(y0,mx,p.x+5);
    @     }else{
    @       drawThinArrow(y0,mx,p.x-5);
    @     }
    @   }
    @ }
    @ function renderGraph(){
    @   var canvasDiv = document.getElementById("canvas");
    @   while( canvasDiv.hasChildNodes() ){
    @     canvasDiv.removeChild(canvasDiv.firstChild);
    @   }
    @   var canvasY = absoluteY("timelineTable");
    @   var left = absoluteX(rowinfo[0].id) - absoluteX("canvas") + 15;
    @   var width = nrail*20;
    @   for(var i in rowinfo){
    @     rowinfo[i].y = absoluteY(rowinfo[i].id) + 10 - canvasY;
    @     rowinfo[i].x = left + rowinfo[i].r*20;
    @   }
    @   var btm = absoluteY("grbtm") + 10 - canvasY;
    @   if( btm<32768 ){
    @     canvasDiv.innerHTML = '<canvas id="timeline-canvas" '+
    @        'style="position:absolute;left:'+(left-5)+'px;"' +
    @        ' width="'+width+'" height="'+btm+'"><'+'/canvas>';
    @     realCanvas = document.getElementById('timeline-canvas');
    @   }else{
    @     realCanvas = 0;
    @   }
    @   var context;
    @   if( realCanvas && realCanvas.getContext
    @        && (context = realCanvas.getContext('2d'))) {
    @     drawBox = function(color,x0,y0,x1,y1) {
    @       if( y0>32767 || y1>32767 ) return;
    @       if( x0>x1 ){ var t=x0; x0=x1; x1=t; }
    @       if( y0>y1 ){ var t=y0; y0=y1; y1=t; }
    @       if(isNaN(x0) || isNaN(y0) || isNaN(x1) || isNaN(y1)) return;
    @       context.fillStyle = color;
    @       context.fillRect(x0-left+5,y0,x1-x0+1,y1-y0+1);
    @     };
    @   }
    @   for(var i in rowinfo){
    @     drawNode(rowinfo[i], left, btm);
    @   }
    @ }
    @ var lastId = rowinfo[rowinfo.length-1].id;
    @ var lastY = 0;
    @ function checkHeight(){
    @   var h = absoluteY(lastId);
    @   if( h!=lastY ){
    @     renderGraph();
    @     lastY = h;
    @   }
    @   setTimeout("checkHeight();", 1000);
    @ }
    @ checkHeight();
    @ /* ]]> */
    @ </script>
  }
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
    @   isleaf BOOLEAN,
    @   bgcolor TEXT,
    @   etype TEXT,
    @   taglist TEXT,
    @   tagid INTEGER,
    @   short TEXT,
    @   sortby REAL
    @ )
  ;
  db_multi_exec(zSql);
}

/*
** Return a pointer to a constant string that forms the basis
** for a timeline query for the WWW interface.
*/
const char *timeline_query_for_www(void){
  static char *zBase = 0;
  static const char zBaseSql[] =
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   datetime(event.mtime,'localtime') AS timestamp,
    @   coalesce(ecomment, comment),
    @   coalesce(euser, user),
    @   NOT EXISTS(SELECT 1 FROM plink
    @               WHERE pid=blob.rid
    @                AND coalesce((SELECT value FROM tagxref
    @                              WHERE tagid=%d AND rid=plink.pid), 'trunk')
    @                  = coalesce((SELECT value FROM tagxref
    @                              WHERE tagid=%d AND rid=plink.cid), 'trunk')),
    @   bgcolor,
    @   event.type,
    @   (SELECT group_concat(substr(tagname,5), ', ') FROM tag, tagxref
    @     WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @       AND tagxref.rid=blob.rid AND tagxref.tagtype>0),
    @   tagid,
    @   brief,
    @   event.mtime
    @  FROM event JOIN blob 
    @ WHERE blob.rid=event.objid
  ;
  if( zBase==0 ){
    zBase = mprintf(zBaseSql, TAG_BRANCH, TAG_BRANCH);
  }
  return zBase;
}

/*
** Generate a submenu element with a single parameter change.
*/
static void timeline_submenu(
  HQuery *pUrl,            /* Base URL */
  const char *zMenuName,   /* Submenu name */
  const char *zParam,      /* Parameter value to add or change */
  const char *zValue,      /* Value of the new parameter */
  const char *zRemove      /* Parameter to omit */
){
  style_submenu_element(zMenuName, zMenuName, "%s",
                        url_render(pUrl, zParam, zValue, zRemove, 0));
}


/*
** zDate is a localtime date.  Insert records into the
** "timeline" table to cause <hr> to be inserted before and after
** entries of that date.
*/
static void timeline_add_dividers(const char *zDate){
  db_multi_exec(
    "INSERT INTO timeline(rid,sortby,etype)"
    "VALUES(-1,julianday(%Q,'utc')-5.0e-6,'div')",
    zDate
  );
  db_multi_exec(
    "INSERT INTO timeline(rid,sortby,etype)"
    "VALUES(-2,julianday(%Q,'utc')+5.0e-6,'div')",
     zDate
  );
}


/*
** WEBPAGE: timeline
**
** Query parameters:
**
**    a=TIMESTAMP    after this date
**    b=TIMESTAMP    before this date.
**    c=TIMESTAMP    "circa" this date.
**    n=COUNT        number of events in output
**    p=RID          artifact RID and up to COUNT parents and ancestors
**    d=RID          artifact RID and up to COUNT descendants
**    t=TAGID        show only check-ins with the given tagid
**    r=TAGID        show check-ins related to tagid
**    u=USER         only if belonging to this user
**    y=TYPE         'ci', 'w', 't', 'e'
**    s=TEXT         string search (comment and brief)
**    ng             Suppress the graph if present
**    f=RID          Show family (immediate parents and children) of RID
**
** p= and d= can appear individually or together.  If either p= or d=
** appear, then u=, y=, a=, and b= are ignored.
**
** If a= and b= appear, only a= is used.  If neither appear, the most
** recent events are choosen.
**
** If n= is missing, the default count is 20.
*/
void page_timeline(void){
  Stmt q;                            /* Query used to generate the timeline */
  Blob sql;                          /* text of SQL used to generate timeline */
  Blob desc;                         /* Description of the timeline */
  int nEntry = atoi(PD("n","20"));   /* Max number of entries on timeline */
  int p_rid = name_to_rid(P("p"));   /* artifact p and its parents */
  int d_rid = name_to_rid(P("d"));   /* artifact d and its descendants */
  int f_rid = name_to_rid(P("f"));   /* artifact f and immediate family */
  const char *zUser = P("u");        /* All entries by this user if not NULL */
  const char *zType = PD("y","all"); /* Type of events.  All if NULL */
  const char *zAfter = P("a");       /* Events after this time */
  const char *zBefore = P("b");      /* Events before this time */
  const char *zCirca = P("c");       /* Events near this time */
  const char *zTagName = P("t");     /* Show events with this tag */
  const char *zBrName = P("r");      /* Show events related to this tag */
  const char *zSearch = P("s");      /* Search string */
  HQuery url;                        /* URL for various branch links */
  int tagid;                         /* Tag ID */
  int tmFlags;                       /* Timeline flags */

  /* To view the timeline, must have permission to read project data.
  */
  login_check_credentials();
  if( !g.okRead && !g.okRdTkt && !g.okRdWiki ){ login_needed(); return; }
  if( zTagName && g.okRead ){
    tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname='sym-%q'", zTagName);
  }else if( zBrName && g.okRead ){
    tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname='sym-%q'",zBrName);
  }else{
    tagid = 0;
  }
  if( zType[0]=='a' ){
    tmFlags = TIMELINE_BRIEF | TIMELINE_GRAPH;
  }else{
    tmFlags = TIMELINE_GRAPH;
  }
  if( P("ng")!=0 || zSearch!=0 ){
    tmFlags &= ~TIMELINE_GRAPH;
  }

  style_header("Timeline");
  login_anonymous_available();
  timeline_temp_table();
  blob_zero(&sql);
  blob_zero(&desc);
  blob_append(&sql, "INSERT OR IGNORE INTO timeline ", -1);
  blob_append(&sql, timeline_query_for_www(), -1);
  if( (p_rid || d_rid) && g.okRead ){
    /* If p= or d= is present, ignore all other parameters other than n= */
    char *zUuid;
    int np, nd;

    if( p_rid && d_rid ){
      if( p_rid!=d_rid ) p_rid = d_rid;
      if( P("n")==0 ) nEntry = 10;
    }
    db_multi_exec(
       "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY)"
    );
    zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d",
                         p_rid ? p_rid : d_rid);
    blob_appendf(&sql, " AND event.objid IN ok");
    nd = 0;
    if( d_rid ){
      compute_descendants(d_rid, nEntry+1);
      nd = db_int(0, "SELECT count(*)-1 FROM ok");
      if( nd>=0 ){
        db_multi_exec("%s", blob_str(&sql));
        blob_appendf(&desc, "%d descendant%s", nd,(1==nd)?"":"s");
      }
      timeline_add_dividers(
        db_text("1","SELECT datetime(mtime,'localtime') FROM event"
                    " WHERE objid=%d", d_rid)
      );
      db_multi_exec("DELETE FROM ok");
    }
    if( p_rid ){
      compute_ancestors(p_rid, nEntry+1);
      np = db_int(0, "SELECT count(*)-1 FROM ok");
      if( np>0 ){
        if( nd>0 ) blob_appendf(&desc, " and ");
        blob_appendf(&desc, "%d ancestors", np);
        db_multi_exec("%s", blob_str(&sql));
      }
      if( d_rid==0 ){
        timeline_add_dividers(  
          db_text("1","SELECT datetime(mtime,'localtime') FROM event"
                      " WHERE objid=%d", p_rid)
        );
      }
    }
    if( g.okHistory ){
      blob_appendf(&desc, " of <a href='%s/info/%s'>[%.10s]</a>",
                   g.zBaseURL, zUuid, zUuid);
    }else{
      blob_appendf(&desc, " of check-in [%.10s]", zUuid);
    }
  }else if( f_rid && g.okRead ){
    /* If f= is present, ignore all other parameters other than n= */
    char *zUuid;
    db_multi_exec(
       "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
       "INSERT INTO ok VALUES(%d);"
       "INSERT OR IGNORE INTO ok SELECT pid FROM plink WHERE cid=%d;"
       "INSERT OR IGNORE INTO ok SELECT cid FROM plink WHERE pid=%d;",
       f_rid, f_rid, f_rid
    );
    blob_appendf(&sql, " AND event.objid IN ok");
    db_multi_exec("%s", blob_str(&sql));
    timeline_add_dividers(
      db_text("1","SELECT datetime(mtime,'localtime') FROM event"
                  " WHERE objid=%d", f_rid)
    );
    blob_appendf(&desc, "Parents and children of check-in ");
    zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", f_rid);
    if( g.okHistory ){
      blob_appendf(&desc, "<a href='%s/info/%s'>[%.10s]</a>",
                   g.zBaseURL, zUuid, zUuid);
    }else{
      blob_appendf(&desc, "[%.10s]", zUuid);
    }
  }else{
    int n;
    const char *zEType = "timeline item";
    char *zDate;
    char *zNEntry = mprintf("%d", nEntry);
    url_initialize(&url, "timeline");
    url_add_parameter(&url, "n", zNEntry);
    if( tagid>0 ){
      if( zType[0]!='e' ) zType = "ci";
      blob_appendf(&sql,
        "AND (EXISTS(SELECT 1 FROM tagxref"
                    " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)", tagid);

      if( zBrName && zType[0]=='c' ){
        /* The next two blob_appendf() calls add SQL that causes checkins that
        ** are not part of the branch which are parents or childen of the branch
        ** to be included in the report.  This related check-ins are useful
        ** in helping to visualize what has happened on a quiescent branch 
        ** that is infrequently merged with a much more activate branch.
        */
        url_add_parameter(&url, "r", zBrName);
        blob_appendf(&sql,
          " OR EXISTS(SELECT 1 FROM plink JOIN tagxref ON rid=cid"
                     " WHERE tagid=%d AND tagtype>0 AND pid=blob.rid)", tagid);
        blob_appendf(&sql,
          " OR EXISTS(SELECT 1 FROM plink JOIN tagxref ON rid=pid"
                     " WHERE tagid=%d AND tagtype>0 AND cid=blob.rid)", tagid);
      }else{
        url_add_parameter(&url, "t", zTagName);
      }
      blob_appendf(&sql, ")");
    }
    if( (zType[0]=='w' && !g.okRdWiki)
     || (zType[0]=='t' && !g.okRdTkt)
     || (zType[0]=='e' && !g.okRdWiki)
     || (zType[0]=='c' && !g.okRead)
    ){
      zType = "all";
    }
    if( zType[0]=='a' ){
      if( !g.okRead || !g.okRdWiki || !g.okRdTkt ){
        char cSep = '(';
        blob_appendf(&sql, " AND event.type IN ");
        if( g.okRead ){
          blob_appendf(&sql, "%c'ci'", cSep);
          cSep = ',';
        }
        if( g.okRdWiki ){
          blob_appendf(&sql, "%c'w','e'", cSep);
          cSep = ',';
        }
        if( g.okRdTkt ){
          blob_appendf(&sql, "%c't'", cSep);
          cSep = ',';
        }
        blob_appendf(&sql, ")");
      }
    }else{ /* zType!="all" */
      blob_appendf(&sql, " AND event.type=%Q", zType);
      url_add_parameter(&url, "y", zType);
      if( zType[0]=='c' ){
        zEType = "checkin";
      }else if( zType[0]=='w' ){
        zEType = "wiki edit";
      }else if( zType[0]=='t' ){
        zEType = "ticket change";
      }else if( zType[0]=='e' ){
        zEType = "event";
      }
    }
    if( zUser ){
      blob_appendf(&sql, " AND event.user=%Q", zUser);
      url_add_parameter(&url, "u", zUser);
    }
    if ( zSearch ){
      blob_appendf(&sql,
        " AND (event.comment LIKE '%%%q%%' OR event.brief LIKE '%%%q%%')",
        zSearch, zSearch);
      url_add_parameter(&url, "s", zSearch);
    }
    if( zAfter ){
      while( fossil_isspace(zAfter[0]) ){ zAfter++; }
      if( zAfter[0] ){
        blob_appendf(&sql, 
           " AND event.mtime>=(SELECT julianday(%Q, 'utc'))"
           " ORDER BY event.mtime ASC", zAfter);
        url_add_parameter(&url, "a", zAfter);
        zBefore = 0;
      }else{
        zAfter = 0;
      }
    }else if( zBefore ){
      while( fossil_isspace(zBefore[0]) ){ zBefore++; }
      if( zBefore[0] ){
        blob_appendf(&sql, 
           " AND event.mtime<=(SELECT julianday(%Q, 'utc'))"
           " ORDER BY event.mtime DESC", zBefore);
        url_add_parameter(&url, "b", zBefore);
       }else{
        zBefore = 0;
      }
    }else if( zCirca ){
      while( fossil_isspace(zCirca[0]) ){ zCirca++; }
      if( zCirca[0] ){
        double rCirca = db_double(0.0, "SELECT julianday(%Q, 'utc')", zCirca);
        Blob sql2;
        blob_init(&sql2, blob_str(&sql), -1);
        blob_appendf(&sql2,
            " AND event.mtime<=%f ORDER BY event.mtime DESC LIMIT %d",
            rCirca, (nEntry+1)/2
        );
        db_multi_exec("%s", blob_str(&sql2));
        blob_reset(&sql2);
        blob_appendf(&sql,
            " AND event.mtime>=%f ORDER BY event.mtime ASC",
            rCirca
        );
        nEntry -= (nEntry+1)/2;
        timeline_add_dividers(zCirca);
        url_add_parameter(&url, "c", zCirca);
      }else{
        zCirca = 0;
      }
    }else{
      blob_appendf(&sql, " ORDER BY event.mtime DESC");
    }
    blob_appendf(&sql, " LIMIT %d", nEntry);
    db_multi_exec("%s", blob_str(&sql));

    n = db_int(0, "SELECT count(*) FROM timeline /*scan*/");
    if( n<nEntry && zAfter ){
      cgi_redirect(url_render(&url, "a", 0, "b", 0));
    }
    if( zAfter==0 && zBefore==0 && zCirca==0 ){
      blob_appendf(&desc, "%d most recent %ss", n, zEType);
    }else{
      blob_appendf(&desc, "%d %ss", n, zEType);
    }
    if( zUser ){
      blob_appendf(&desc, " by user %h", zUser);
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( zTagName ){
      blob_appendf(&desc, " tagged with \"%h\"", zTagName);
      tmFlags |= TIMELINE_DISJOINT;
    }else if( zBrName ){
      blob_appendf(&desc, " related to \"%h\"", zBrName);
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( zAfter ){
      blob_appendf(&desc, " occurring on or after %h.<br />", zAfter);
    }else if( zBefore ){
      blob_appendf(&desc, " occurring on or before %h.<br />", zBefore);
    }else if( zCirca ){
      blob_appendf(&desc, " occurring around %h.<br />", zCirca);
    }
    if( zSearch ){
      blob_appendf(&desc, " matching \"%h\"", zSearch);
    }
    if( g.okHistory ){
      if( zAfter || n==nEntry ){
        zDate = db_text(0, "SELECT min(timestamp) FROM timeline /*scan*/");
        timeline_submenu(&url, "Older", "b", zDate, "a");
        free(zDate);
      }
      if( zBefore || (zAfter && n==nEntry) ){
        zDate = db_text(0, "SELECT max(timestamp) FROM timeline /*scan*/");
        timeline_submenu(&url, "Newer", "a", zDate, "b");
        free(zDate);
      }else if( tagid==0 ){
        if( zType[0]!='a' ){
          timeline_submenu(&url, "All Types", "y", "all", 0);
        }
        if( zType[0]!='w' && g.okRdWiki ){
          timeline_submenu(&url, "Wiki Only", "y", "w", 0);
        }
        if( zType[0]!='c' && g.okRead ){
          timeline_submenu(&url, "Checkins Only", "y", "ci", 0);
        }
        if( zType[0]!='t' && g.okRdTkt ){
          timeline_submenu(&url, "Tickets Only", "y", "t", 0);
        }
        if( zType[0]!='e' && g.okRdWiki ){
          timeline_submenu(&url, "Events Only", "y", "e", 0);
        }
      }
      if( nEntry>20 ){
        timeline_submenu(&url, "20 Entries", "n", "20", 0);
      }
      if( nEntry<200 ){
        timeline_submenu(&url, "200 Entries", "n", "200", 0);
      }
    }
  }
  if( P("showsql") ){
    @ <blockquote>%h(blob_str(&sql))</blockquote>
  }
  blob_zero(&sql);
  db_prepare(&q, "SELECT * FROM timeline ORDER BY sortby DESC /*scan*/");
  @ <h2>%b(&desc)</h2>
  blob_reset(&desc);
  www_print_timeline(&q, tmFlags, 0);
  db_finalize(&q);
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
  zPrevDate[0] = 0;

  if( g.localOpen ){
    int rid = db_lget_int("checkout", 0);
    zCurrentUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  }

  while( db_step(q)==SQLITE_ROW && nLine<=mxLine ){
    int rid = db_column_int(q, 0);
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
      const char *zBrType;
      if( count_nonbranch_children(rid)>1 ){
        zBrType = "*FORK* ";
      }else{
        zBrType = "*BRANCH* ";
      }
      sqlite3_snprintf(sizeof(zPrefix)-n, &zPrefix[n], zBrType);
      n = strlen(zPrefix);
    }
    if( zCurrentUuid && strcmp(zCurrentUuid,zId)==0 ){
      sqlite3_snprintf(sizeof(zPrefix)-n, &zPrefix[n], "*CURRENT* ");
      n += strlen(zPrefix);
    }
    zFree = sqlite3_mprintf("[%.10s] %s%s", zUuid, zPrefix, zCom);
    nLine += comment_print(zFree, 9, 79);
    sqlite3_free(zFree);
  }
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
    @   coalesce(ecomment,comment)
    @     || ' (user: ' || coalesce(euser,user,'?')
    @     || (SELECT case when length(x)>0 then ' tags: ' || x else '' end
    @           FROM (SELECT group_concat(substr(tagname,5), ', ') AS x
    @                   FROM tag, tagxref
    @                  WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @                    AND tagxref.rid=blob.rid AND tagxref.tagtype>0))
    @     || ')',
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid),
    @   event.mtime
    @ FROM event, blob
    @ WHERE blob.rid=event.objid
  ;
  return zBaseSql;
}

/*
** Return true if the input string is a date in the ISO 8601 format:
** YYYY-MM-DD.
*/
static int isIsoDate(const char *z){
  return strlen(z)==10
      && z[4]=='-'
      && z[7]=='-'
      && fossil_isdigit(z[0])
      && fossil_isdigit(z[5]);
}

/*
** COMMAND: timeline
**
** Usage: %fossil timeline ?WHEN? ?BASELINE|DATETIME? ?-n N? ?-t TYPE?
**
** Print a summary of activity going backwards in date and time
** specified or from the current date and time if no arguments
** are given.  Show as many as N (default 20) check-ins.  The
** WHEN argument can be any unique abbreviation of one of these
** keywords:
**
**     before
**     after
**     descendants | children
**     ancestors | parents
**
** The BASELINE can be any unique prefix of 4 characters or more.
** The DATETIME should be in the ISO8601 format.  For
** examples: "2007-08-18 07:21:21".  You can also say "current"
** for the current version or "now" for the current time.
**
** The optional TYPE argument may any types supported by the /timeline
** page. For example:
**
**     w  = wiki commits only
**     ci = file commits only
**     t  = tickets only
*/
void timeline_cmd(void){
  Stmt q;
  int n, k;
  const char *zCount;
  const char *zType;
  char *zOrigin;
  char *zDate;
  Blob sql;
  int objid = 0;
  Blob uuid;
  int mode = 0 ;       /* 0:none  1: before  2:after  3:children  4:parents */
  db_find_and_open_repository(1);
  zCount = find_option("count","n",1);
  zType = find_option("type","t",1);
  if( zCount ){
    n = atoi(zCount);
  }else{
    n = 20;
  }
  if( g.argc>=4 ){
    k = strlen(g.argv[2]);
    if( strncmp(g.argv[2],"before",k)==0 ){
      mode = 1;
    }else if( strncmp(g.argv[2],"after",k)==0 && k>1 ){
      mode = 2;
    }else if( strncmp(g.argv[2],"descendants",k)==0 ){
      mode = 3;
    }else if( strncmp(g.argv[2],"children",k)==0 ){
      mode = 3;
    }else if( strncmp(g.argv[2],"ancestors",k)==0 && k>1 ){
      mode = 4;
    }else if( strncmp(g.argv[2],"parents",k)==0 ){
      mode = 4;
    }else if(!zType && !zCount){
      usage("?WHEN? ?BASELINE|DATETIME? ?-n|--count N? ?-t TYPE?");
    }
    if( '-' != *g.argv[3] ){
	zOrigin = g.argv[3];
    }else{
	zOrigin = "now";
    }
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
      fossil_fatal("cannot compute descendants or ancestors of a date");
    }
    zDate = mprintf("(SELECT datetime('now'))");
  }else if( strncmp(zOrigin, "current", k)==0 ){
    if( !g.localOpen ){
      fossil_fatal("must be within a local checkout to use 'current'");
    }
    objid = db_lget_int("checkout",0);
    zDate = mprintf("(SELECT mtime FROM plink WHERE cid=%d)", objid);
  }else if( name_to_uuid(&uuid, 0)==0 ){
    objid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &uuid);
    zDate = mprintf("(SELECT mtime FROM plink WHERE cid=%d)", objid);
  }else{
    const char *zShift = "";
    if( mode==3 || mode==4 ){
      fossil_fatal("cannot compute descendants or ancestors of a date");
    }
    if( mode==0 ){
      if( isIsoDate(zOrigin) ) zShift = ",'+1 day'";
    }
    zDate = mprintf("(SELECT julianday(%Q%s, 'utc'))", zOrigin, zShift);
  }
  if( mode==0 ) mode = 1;
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_tty(), -1);
  blob_appendf(&sql, "  AND event.mtime %s %s",
     (mode==1 || mode==4) ? "<=" : ">=",
     zDate
  );

  if( mode==3 || mode==4 ){
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    if( mode==3 ){
      compute_descendants(objid, n);
    }else{
      compute_ancestors(objid, n);
    }
    blob_appendf(&sql, " AND blob.rid IN ok");
  }
  if( zType && (zType[0]!='a') ){
    blob_appendf(&sql, " AND event.type=%Q ", zType);
  }

  blob_appendf(&sql, " ORDER BY event.mtime DESC");
  db_prepare(&q, blob_str(&sql));
  blob_reset(&sql);
  print_timeline(&q, n);
  db_finalize(&q);
}

/*
** This is a version of the "localtime()" function from the standard
** C library.  It converts a unix timestamp (seconds since 1970) into
** a broken-out local time structure.
**
** This modified version of localtime() works like the library localtime()
** by default.  Except if the timeline-utc property is set, this routine
** uses gmttime() instead.  Thus by setting the timeline-utc property, we
** can get all localtimes to be displayed at UTC time.
*/
struct tm *fossil_localtime(const time_t *clock){
  if( g.fTimeFormat==0 ){
    if( db_get_int("timeline-utc", 1) ){
      g.fTimeFormat = 1;
    }else{
      g.fTimeFormat = 2;
    }
  }
  if( g.fTimeFormat==1 ){
    return gmtime(clock);
  }else{
    return localtime(clock);
  }
}
