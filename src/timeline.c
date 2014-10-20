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
#include "config.h"
#include <string.h>
#include <time.h>
#include "timeline.h"

/*
** Generate a hyperlink to a version.
*/
void hyperlink_to_uuid(const char *zUuid){
  if( g.perm.Hyperlink ){
    @ %z(xhref("class='timelineHistLink'","%R/info/%s",zUuid))[%S(zUuid)]</a>
  }else{
    @ <span class="timelineHistDsp">[%S(zUuid)]</span>
  }
}

/*
** Generate a hyperlink to a date & time.
*/
void hyperlink_to_date(const char *zDate, const char *zSuffix){
  if( zSuffix==0 ) zSuffix = "";
  if( g.perm.Hyperlink ){
    @ %z(href("%R/timeline?c=%T",zDate))%s(zDate)</a>%s(zSuffix)
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
  if( zU==0 || zU[0]==0 ) zU = "anonymous";
  if( zSuf==0 ) zSuf = "";
  if( g.perm.Hyperlink ){
    if( zD && zD[0] ){
      @ %z(href("%R/timeline?c=%T&u=%T",zD,zU))%h(zU)</a>%s(zSuf)
    }else{
      @ %z(href("%R/timeline?u=%T",zU))%h(zU)</a>%s(zSuf)
    }
  }else{
    @ %s(zU)
  }
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
#define TIMELINE_FCHANGES 0x0020  /* Detail file changes */
#define TIMELINE_BRCOLOR  0x0040  /* Background color by branch name */
#define TIMELINE_UCOLOR   0x0080  /* Background color by user */
#define TIMELINE_FRENAMES 0x0100  /* Detail only file name changes */
#define TIMELINE_UNHIDE   0x0200  /* Unhide check-ins with "hidden" tag */
#endif

/*
** Hash a string and use the hash to determine a background color.
*/
char *hash_color(const char *z){
  int i;                       /* Loop counter */
  unsigned int h = 0;          /* Hash on the branch name */
  int r, g, b;                 /* Values for red, green, and blue */
  int h1, h2, h3, h4;          /* Elements of the hash value */
  int mx, mn;                  /* Components of HSV */
  static char zColor[10];      /* The resulting color */
  static int ix[2] = {0,0};    /* Color chooser parameters */

  if( ix[0]==0 ){
    if( db_get_boolean("white-foreground", 0) ){
      ix[0] = 140;
      ix[1] = 40;
    }else{
      ix[0] = 216;
      ix[1] = 16;
    }
  }
  for(i=0; z[i]; i++ ){
    h = (h<<11) ^ (h<<1) ^ (h>>3) ^ z[i];
  }
  h1 = h % 6;  h /= 6;
  h3 = h % 30; h /= 30;
  h4 = h % 40; h /= 40;
  mx = ix[0] - h3;
  mn = mx - h4 - ix[1];
  h2 = (h%(mx - mn)) + mn;
  switch( h1 ){
    case 0:  r = mx; g = h2, b = mn;  break;
    case 1:  r = h2; g = mx, b = mn;  break;
    case 2:  r = mn; g = mx, b = h2;  break;
    case 3:  r = mn; g = h2, b = mx;  break;
    case 4:  r = h2; g = mn, b = mx;  break;
    default: r = mx; g = mn, b = h2;  break;
  }
  sqlite3_snprintf(8, zColor, "#%02x%02x%02x", r,g,b);
  return zColor;
}

/*
** COMMAND:  test-hash-color
**
** Usage: %fossil test-hash-color TAG ...
**
** Print out the color names associated with each tag.  Used for
** testing the hash_color() function.
*/
void test_hash_color(void){
  int i;
  for(i=2; i<g.argc; i++){
    fossil_print("%20s: %s\n", g.argv[i], hash_color(g.argv[i]));
  }
}

/*
** WEBPAGE:  hash-color-test
**
** Print out the color names associated with each tag.  Used for
** testing the hash_color() function.
*/
void test_hash_color_page(void){
  const char *zBr;
  char zNm[10];
  int i, cnt;
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }

  style_header("Hash Color Test");
  for(i=cnt=0; i<10; i++){
    sqlite3_snprintf(sizeof(zNm),zNm,"b%d",i);
    zBr = P(zNm);
    if( zBr && zBr[0] ){
      @ <p style='border:1px solid;background-color:%s(hash_color(zBr));'>
      @ %h(zBr) - %s(hash_color(zBr)) -
      @ Omnes nos quasi oves erravimus unusquisque in viam
      @ suam declinavit.</p>
      cnt++;
    }
  }
  if( cnt ){
    @ <hr>
  }
  @ <form method="post" action="%s(g.zTop)/hash-color-test">
  @ <p>Enter candidate branch names below and see them displayed in their
  @ default background colors above.</p>
  for(i=0; i<10; i++){
    sqlite3_snprintf(sizeof(zNm),zNm,"b%d",i);
    zBr = P(zNm);
    @ <input type="text" size="30" name='%s(zNm)' value='%h(PD(zNm,""))'><br />
  }
  @ <input type="submit">
  @ </form>
  style_footer();
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
**    5.  True if is a leaf
**    6.  background color
**    7.  type ("ci", "w", "t", "e", "g", "div")
**    8.  list of symbolic tags.
**    9.  tagid for ticket or wiki or event
**   10.  Short comment to user for repeated tickets and wiki
*/
void www_print_timeline(
  Stmt *pQuery,          /* Query to implement the timeline */
  int tmFlags,           /* Flags controlling display behavior */
  const char *zThisUser, /* Suppress links to this user */
  const char *zThisTag,  /* Suppress links to this tag */
  void (*xExtra)(int)    /* Routine to call on each line of display */
){
  int mxWikiLen;
  Blob comment;
  int prevTagid = 0;
  int suppressCnt = 0;
  char zPrevDate[20];
  GraphContext *pGraph = 0;
  int prevWasDivider = 0;     /* True if previous output row was <hr> */
  int fchngQueryInit = 0;     /* True if fchngQuery is initialized */
  Stmt fchngQuery;            /* Query for file changes on check-ins */
  static Stmt qbranch;
  int pendingEndTr = 0;       /* True if a </td></tr> is needed */
  int vid = 0;                /* Current checkout version */
  int dateFormat = 0;         /* 0: HH:MM  1: HH:MM:SS
                                 2: YYYY-MM-DD HH:MM
                                 3: YYMMDD HH:MM */

  if( fossil_strcmp(g.zIpAddr, "127.0.0.1")==0 && db_open_local(0) ){
    vid = db_lget_int("checkout", 0);
  }
  zPrevDate[0] = 0;
  mxWikiLen = db_get_int("timeline-max-comment", 0);
  dateFormat = db_get_int("timeline-date-format", 0);
  if( tmFlags & TIMELINE_GRAPH ){
    pGraph = graph_init();
    /* style is not moved to css, because this is
    ** a technical div for the timeline graph
    */
    @ <div id="canvas" style="position:relative;height:0px;width:0px;"
    @  onclick="clickOnGraph(event)"></div>
  }
  db_static_prepare(&qbranch,
    "SELECT value FROM tagxref WHERE tagid=%d AND tagtype>0 AND rid=:rid",
    TAG_BRANCH
  );

  @ <table id="timelineTable" class="timelineTable"
  @  onclick="clickOnGraph(event)">
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
    const char *zDispUser = zUser && zUser[0] ? zUser : "anonymous";
    const char *zBr = 0;      /* Branch */
    int commentColumn = 3;    /* Column containing comment text */
    int modPending;           /* Pending moderation */
    char zTime[20];

    if( zDate==0 ) zDate = "YYYY-MM-DD HH:MM:SS";  /* Something wrong with the repo */
    modPending =  moderation_pending(rid);
    if( tagid ){
      if( modPending ) tagid = -tagid;
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
      @ <span class="timelineDisabled">... %d(suppressCnt) similar
      @ event%s(suppressCnt>1?"s":"") omitted.</span>
      suppressCnt = 0;
    }
    if( pendingEndTr ){
      @ </td></tr>
      pendingEndTr = 0;
    }
    if( fossil_strcmp(zType,"div")==0 ){
      if( !prevWasDivider ){
        @ <tr><td colspan="3"><hr /></td></tr>
      }
      prevWasDivider = 1;
      continue;
    }
    prevWasDivider = 0;
    if( dateFormat<2 ){
      if( fossil_strnicmp(zDate, zPrevDate, 10) ){
        sqlite3_snprintf(sizeof(zPrevDate), zPrevDate, "%.10s", zDate);
        @ <tr><td>
        @   <div class="divider timelineDate">%s(zPrevDate)</div>
        @ </td><td></td><td></td></tr>
      }
      memcpy(zTime, &zDate[11], 5+dateFormat*3);
      zTime[5+dateFormat*3] = 0;
    }else if(3==dateFormat){
      /* YYMMDD HH:MM */
      int pos = 0;
      zTime[pos++] = zDate[2]; zTime[pos++] = zDate[3]; /* YY */
      zTime[pos++] = zDate[5]; zTime[pos++] = zDate[6]; /* MM */
      zTime[pos++] = zDate[8]; zTime[pos++] = zDate[9]; /* DD */
      zTime[pos++] = ' ';
      zTime[pos++] = zDate[11]; zTime[pos++] = zDate[12]; /* HH */
      zTime[pos++] = ':';
      zTime[pos++] = zDate[14]; zTime[pos++] = zDate[15]; /* MM */
      zTime[pos++] = 0;
    }else{
      /* YYYY-MM-DD HH:MM */
      sqlite3_snprintf(sizeof(zTime), zTime, "%.16s", zDate);
    }
    if( rid == vid ){
      @ <tr class="timelineCurrent">
    }else {
      @ <tr>
    }
    @ <td class="timelineTime">%s(zTime)</td>
    @ <td class="timelineGraph">
    if( tmFlags & TIMELINE_UCOLOR )  zBgClr = zUser ? hash_color(zUser) : 0;
    if( zType[0]=='c'
    && (pGraph || zBgClr==0 || (tmFlags & TIMELINE_BRCOLOR)!=0)
    ){
      db_reset(&qbranch);
      db_bind_int(&qbranch, ":rid", rid);
      if( db_step(&qbranch)==SQLITE_ROW ){
        zBr = db_column_text(&qbranch, 0);
      }else{
        zBr = "trunk";
      }
      if( zBgClr==0 || (tmFlags & TIMELINE_BRCOLOR)!=0 ){
        if( zBr==0 || strcmp(zBr,"trunk")==0 ){
          zBgClr = 0;
        }else{
          zBgClr = hash_color(zBr);
        }
      }
    }
    if( zType[0]=='c' && (pGraph || (tmFlags & TIMELINE_BRCOLOR)!=0) ){
      int nParent = 0;
      int aParent[32];
      int gidx;
      static Stmt qparent;
      db_static_prepare(&qparent,
        "SELECT pid FROM plink"
        " WHERE cid=:rid AND pid NOT IN phantom"
        " ORDER BY isprim DESC /*sort*/"
      );
      db_bind_int(&qparent, ":rid", rid);
      while( db_step(&qparent)==SQLITE_ROW && nParent<32 ){
        aParent[nParent++] = db_column_int(&qparent, 0);
      }
      db_reset(&qparent);
      gidx = graph_add_row(pGraph, rid, nParent, aParent, zBr, zBgClr,
                           zUuid, isLeaf);
      db_reset(&qbranch);
      @ <div id="m%d(gidx)"></div>
    }
    @</td>
    if( zBgClr && zBgClr[0] ){
      @ <td class="timelineTableCell" style="background-color: %h(zBgClr);">
    }else{
      @ <td class="timelineTableCell">
    }
    if( pGraph && zType[0]!='c' ){
      @ &bull;
    }
    if( modPending ){
      @ <span class="modpending">(Awaiting Moderator Approval)</span>
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
      hyperlink_to_event_tagid(tagid<0?-tagid:tagid);
    }else if( (tmFlags & TIMELINE_ARTID)!=0 ){
      hyperlink_to_uuid(zUuid);
    }
    db_column_blob(pQuery, commentColumn, &comment);
    if( zType[0]!='c' ){
      /* Comments for anything other than a check-in are generated by
      ** "fossil rebuild" and expect to be rendered as text/x-fossil-wiki */
      wiki_convert(&comment, 0, WIKI_INLINE);
    }else if( mxWikiLen>0 && blob_size(&comment)>mxWikiLen ){
      Blob truncated;
      blob_zero(&truncated);
      blob_append(&truncated, blob_buffer(&comment), mxWikiLen);
      blob_append(&truncated, "...", 3);
      @ <span class="timelineComment">%W(blob_str(&truncated))</span>
      blob_reset(&truncated);
    }else{
      @ <span class="timelineComment">%W(blob_str(&comment))</span>
    }
    blob_reset(&comment);

    /* Generate the "user: USERNAME" at the end of the comment, together
    ** with a hyperlink to another timeline for that user.
    */
    if( zTagList && zTagList[0]==0 ) zTagList = 0;
    if( g.perm.Hyperlink && fossil_strcmp(zDispUser, zThisUser)!=0 ){
      char *zLink = mprintf("%R/timeline?u=%h&c=%t&nd", zDispUser, zDate);
      @ (user: %z(href("%z",zLink))%h(zDispUser)</a>%s(zTagList?",":"\051")
    }else{
      @ (user: %h(zDispUser)%s(zTagList?",":"\051")
    }

    /* Generate a "detail" link for tags. */
    if( (zType[0]=='g' || zType[0]=='w' || zType[0]=='t') && g.perm.Hyperlink ){
      @ [%z(href("%R/info/%s",zUuid))details</a>]
    }

    /* Generate the "tags: TAGLIST" at the end of the comment, together
    ** with hyperlinks to the tag list.
    */
    if( zTagList ){
      if( g.perm.Hyperlink ){
        int i;
        const char *z = zTagList;
        Blob links;
        blob_zero(&links);
        while( z && z[0] ){
          for(i=0; z[i] && (z[i]!=',' || z[i+1]!=' '); i++){}
          if( zThisTag==0 || memcmp(z, zThisTag, i)!=0 || zThisTag[i]!=0 ){
            blob_appendf(&links,
                  "%z%#h</a>%.2s",
                  href("%R/timeline?r=%#t&nd&c=%t",i,z,zDate), i,z, &z[i]
            );
          }else{
            blob_appendf(&links, "%#h", i+2, z);
          }
          if( z[i]==0 ) break;
          z += i+2;
        }
        @ tags: %s(blob_str(&links)))
        blob_reset(&links);
      }else{
        @ tags: %h(zTagList))
      }
    }


    /* Generate extra hyperlinks at the end of the comment */
    if( xExtra ){
      xExtra(rid);
    }

    /* Generate the file-change list if requested */
    if( (tmFlags & (TIMELINE_FCHANGES|TIMELINE_FRENAMES))!=0
     && zType[0]=='c' && g.perm.Hyperlink
    ){
      int inUl = 0;
      if( !fchngQueryInit ){
        db_prepare(&fchngQuery,
          "SELECT (pid==0) AS isnew,"
          "       (fid==0) AS isdel,"
          "       (SELECT name FROM filename WHERE fnid=mlink.fnid) AS name,"
          "       (SELECT uuid FROM blob WHERE rid=fid),"
          "       (SELECT uuid FROM blob WHERE rid=pid),"
          "       (SELECT name FROM filename WHERE fnid=mlink.pfnid) AS oldnm"
          "  FROM mlink"
          " WHERE mid=:mid AND (pid!=fid OR pfnid>0)"
          "   AND (fid>0 OR"
               "   fnid NOT IN (SELECT pfnid FROM mlink WHERE mid=:mid))"
          " ORDER BY 3 /*sort*/"
        );
        fchngQueryInit = 1;
      }
      db_bind_int(&fchngQuery, ":mid", rid);
      while( db_step(&fchngQuery)==SQLITE_ROW ){
        const char *zFilename = db_column_text(&fchngQuery, 2);
        int isNew = db_column_int(&fchngQuery, 0);
        int isDel = db_column_int(&fchngQuery, 1);
        const char *zOldName = db_column_text(&fchngQuery, 5);
        const char *zOld = db_column_text(&fchngQuery, 4);
        const char *zNew = db_column_text(&fchngQuery, 3);
        if( !inUl ){
          @ <ul class="filelist">
          inUl = 1;
        }
        if( (tmFlags & TIMELINE_FRENAMES)!=0 ){
          if( !isNew && !isDel && zOldName!=0 ){
            @ <li> %h(zOldName) &rarr; %h(zFilename)
          }
          continue;
        }
        if( isNew ){
          @ <li> %h(zFilename) (new file) &nbsp;
          @ %z(href("%R/artifact/%s",zNew))[view]</a></li>
        }else if( isDel ){
          @ <li> %h(zFilename) (deleted)</li>
        }else if( fossil_strcmp(zOld,zNew)==0 && zOldName!=0 ){
          @ <li> %h(zOldName) &rarr; %h(zFilename)
          @ %z(href("%R/artifact/%s",zNew))[view]</a></li>
        }else{
          if( zOldName!=0 ){
            @ <li> %h(zOldName) &rarr; %h(zFilename)
          }else{
            @ <li> %h(zFilename) &nbsp;
          }
          @ %z(href("%R/fdiff?sbs=1&v1=%s&v2=%s",zOld,zNew))[diff]</a></li>
        }
      }
      db_reset(&fchngQuery);
      if( inUl ){
        @ </ul>
      }
    }
    pendingEndTr = 1;
  }
  if( suppressCnt ){
    @ <span class="timelineDisabled">... %d(suppressCnt) similar
    @ event%s(suppressCnt>1?"s":"") omitted.</span>
    suppressCnt = 0;
  }
  if( pendingEndTr ){
    @ </td></tr>
  }
  if( pGraph ){
    graph_finish(pGraph, (tmFlags & TIMELINE_DISJOINT)!=0);
    if( pGraph->nErr ){
      graph_free(pGraph);
      pGraph = 0;
    }else{
      int w;
      /* style is not moved to css, because this is
      ** a technical div for the timeline graph
      */
      w = (pGraph->mxRail+1)*pGraph->iRailPitch + 10;
      @ <tr><td></td><td>
      @ <div id="grbtm" style="width:%d(w)px;"></div>
      @ </td><td></td></tr>
    }
  }
  @ </table>
  if( fchngQueryInit ) db_finalize(&fchngQuery);
  timeline_output_graph_javascript(pGraph, (tmFlags & TIMELINE_DISJOINT)!=0, 0);
}

/*
** Generate all of the necessary javascript to generate a timeline
** graph.
*/
void timeline_output_graph_javascript(
  GraphContext *pGraph,     /* The graph to be displayed */
  int omitDescenders,       /* True to omit descenders */
  int fileDiff              /* True for file diff.  False for check-in diff */
){
  if( pGraph && pGraph->nErr==0 && pGraph->nRow>0 ){
    GraphRow *pRow;
    int i;
    char cSep;

    @ <script>
    @ var railPitch=%d(pGraph->iRailPitch);

    /* the rowinfo[] array contains all the information needed to generate
    ** the graph.  Each entry contains information for a single row:
    **
    **   id:  The id of the <div> element for the row. This is an integer.
    **        to get an actual id, prepend "m" to the integer.  The top node
    **        is 1 and numbers increase moving down the timeline.
    **   bg:  The background color for this row
    **    r:  The "rail" that the node for this row sits on.  The left-most
    **        rail is 0 and the number increases to the right.
    **    d:  True if there is a "descender" - an arrow coming from the bottom
    **        of the page straight up to this node.
    **   mo:  "merge-out".  If non-zero, this is one more than the x-coordinate
    **        for the upward portion of a merge arrow.  The merge arrow goes up
    **        to the row identified by mu:.  If this value is zero then
    **        node has no merge children and no merge-out line is drawn.
    **   mu:  The id of the row which is the top of the merge-out arrow.
    **    u:  Draw a thick child-line out of the top of this node and up to
    **        the node with an id equal to this value.  0 if there is no
    **        thick-line riser.
    **    f:  0x01: a leaf node.
    **   au:  An array of integers that define thick-line risers for branches.
    **        The integers are in pairs.  For each pair, the first integer is
    **        is the rail on which the riser should run and the second integer
    **        is the id of the node upto which the riser should run.
    **   mi:  "merge-in".  An array of integer x-coordinates from which
    **        merge arrows should be drawn into this node.  If the value is
    **        negative, then the x-coordinate is the absolute value of mi[]
    **        and a thin merge-arrow descender is drawn to the bottom of
    **        the screen.
    **    h:  The SHA1 hash of the object being graphed
    */
    cgi_printf("var rowinfo = [\n");
    for(pRow=pGraph->pFirst; pRow; pRow=pRow->pNext){
      int mo = pRow->mergeOut;
      if( mo<0 ){
        mo = 0;
      }else{
        mo = (mo/4)*pGraph->iRailPitch - 3 + 4*(mo&3);
      }
      cgi_printf("{id:%d,bg:\"%s\",r:%d,d:%d,mo:%d,mu:%d,u:%d,f:%d,au:",
        pRow->idx,                      /* id */
        pRow->zBgClr,                   /* bg */
        pRow->iRail,                    /* r */
        pRow->bDescender,               /* d */
        mo,                             /* mo */
        pRow->mergeUpto,                /* mu */
        pRow->aiRiser[pRow->iRail],     /* u */
        pRow->isLeaf ? 1 : 0            /* f */
      );
      /* u */
      cSep = '[';
      for(i=0; i<GR_MAX_RAIL; i++){
        if( i==pRow->iRail ) continue;
        if( pRow->aiRiser[i]>0 ){
          cgi_printf("%c%d,%d", cSep, i, pRow->aiRiser[i]);
          cSep = ',';
        }
      }
      if( cSep=='[' ) cgi_printf("[");
      cgi_printf("],mi:");
      /* mi */
      cSep = '[';
      for(i=0; i<GR_MAX_RAIL; i++){
        if( pRow->mergeIn[i] ){
          int mi = i*pGraph->iRailPitch - 8 + 4*pRow->mergeIn[i];
          if( pRow->mergeDown & (1<<i) ) mi = -mi;
          cgi_printf("%c%d", cSep, mi);
          cSep = ',';
        }
      }
      if( cSep=='[' ) cgi_printf("[");
      cgi_printf("],h:\"%s\"}%s", pRow->zUuid, pRow->pNext ? ",\n" : "];\n");
    }
    cgi_printf("var nrail = %d\n", pGraph->mxRail+1);
    graph_free(pGraph);
    @ var canvasDiv = gebi("canvas");
    @ var canvasStyle = window.getComputedStyle && window.getComputedStyle(canvasDiv,null);
    @ var lineColor = (canvasStyle && canvasStyle.getPropertyValue('color')) || 'black';
    @ var bgColor = (canvasStyle && canvasStyle.getPropertyValue('background-color')) || 'white';
    @ if( bgColor=='transparent' ) bgColor = 'white';
    @ var boxColor = lineColor;
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
    @   return n;
    @ }
    @ function absoluteY(id){
    @   var obj = gebi(id);
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
    @   var obj = gebi(id);
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
    @   drawBox(lineColor,x,y0,x+1,y1);
    @   if( y0+10>=y1 ){
    @     drawBox(lineColor,x-1,y0+1,x+2,y0+2);
    @     drawBox(lineColor,x-2,y0+3,x+3,y0+4);
    @   }else{
    @     drawBox(lineColor,x-1,y0+2,x+2,y0+4);
    @     drawBox(lineColor,x-2,y0+5,x+3,y0+7);
    @   }
    @ }
    @ function drawThinArrow(y,xFrom,xTo){
    @   if( xFrom<xTo ){
    @     drawBox(lineColor,xFrom,y,xTo,y);
    @     drawBox(lineColor,xTo-3,y-1,xTo-2,y+1);
    @     drawBox(lineColor,xTo-4,y-2,xTo-4,y+2);
    @   }else{
    @     drawBox(lineColor,xTo,y,xFrom,y);
    @     drawBox(lineColor,xTo+2,y-1,xTo+3,y+1);
    @     drawBox(lineColor,xTo+4,y-2,xTo+4,y+2);
    @   }
    @ }
    @ function drawThinLine(x0,y0,x1,y1){
    @   drawBox(lineColor,x0,y0,x1,y1);
    @ }
    @ function drawNodeBox(color,x0,y0,x1,y1){
    @   drawBox(color,x0,y0,x1,y1).style.cursor = "pointer";
    @ }
    @ function drawNode(p, left, btm){
    @   drawNodeBox(boxColor,p.x-5,p.y-5,p.x+6,p.y+6);
    @   drawNodeBox(p.bg||bgColor,p.x-4,p.y-4,p.x+5,p.y+5);
    @   if( p.u>0 ) drawUpArrow(p.x, rowinfo[p.u-1].y+6, p.y-5);
    @   if( p.f&1 ) drawNodeBox(boxColor,p.x-1,p.y-1,p.x+2,p.y+2);
    if( !omitDescenders ){
      @   if( p.u==0 ) drawUpArrow(p.x, 0, p.y-5);
      @   if( p.d ) drawUpArrow(p.x, p.y+6, btm);
    }
    @   if( p.mo>0 ){
    @     var x1 = p.mo + left - 1;
    @     var y1 = p.y-3;
    @     var x0 = x1>p.x ? p.x+7 : p.x-6;
    @     var u = rowinfo[p.mu-1];
    @     var y0 = u.y+5;
    @     if( x1>=p.x-5 && x1<=p.x+5 ){
    @       y1 = p.y-5;
    @     }else{
    @       drawThinLine(x0,y1,x1,y1);
    @     }
    @     drawThinLine(x1,y0,x1,y1);
    @   }
    @   var n = p.au.length;
    @   for(var i=0; i<n; i+=2){
    @     var x1 = p.au[i]*railPitch + left;
    @     var x0 = x1>p.x ? p.x+7 : p.x-6;
    @     var u = rowinfo[p.au[i+1]-1];
    @     if(u.id<p.id){
    @       drawBox(lineColor,x0,p.y,x1,p.y+1);
    @       drawUpArrow(x1, u.y+6, p.y);
    @     }else{
    @       drawBox("#600000",x0,p.y,x1,p.y+1);
    @       drawBox("#600000",x1-1,p.y,x1,u.y+1);
    @       drawBox("#600000",x1,u.y,u.x-6,u.y+1);
    @       drawBox("#600000",u.x-9,u.y-1,u.x-8,u.y+2);
    @       drawBox("#600000",u.x-11,u.y-2,u.x-10,u.y+3);
    @     }
    @   }
    @   for(var j in p.mi){
    @     var y0 = p.y+5;
    @     var mx = p.mi[j];
    @     if( mx<0 ){
    @       mx = left-mx;
    @       drawThinLine(mx,y0,mx,btm);
    @     }else{
    @       mx += left;
    @     }
    @     if( mx>p.x ){
    @       drawThinArrow(y0,mx,p.x+6);
    @     }else{
    @       drawThinArrow(y0,mx,p.x-5);
    @     }
    @   }
    @ }
    @ var selBox = null;
    @ var selRow = null;
    @ function renderGraph(){
    @   var canvasDiv = gebi("canvas");
    @   while( canvasDiv.hasChildNodes() ){
    @     canvasDiv.removeChild(canvasDiv.firstChild);
    @   }
    @   var canvasY = absoluteY("timelineTable");
    @   var left = absoluteX("m"+rowinfo[0].id) - absoluteX("canvas") + 15;
    @   for(var i in rowinfo){
    @     rowinfo[i].y = absoluteY("m"+rowinfo[i].id) + 10 - canvasY;
    @     rowinfo[i].x = left + rowinfo[i].r*railPitch;
    @   }
    @   var btm = absoluteY("grbtm") + 10 - canvasY;
    @   for(var i in rowinfo){
    @     drawNode(rowinfo[i], left, btm);
    @   }
    @   if( selRow!=null ) clickOnRow(selRow);
    @ }
    @ function clickOnGraph(event){
    @   var x=event.clientX-absoluteX("canvas");
    @   var y=event.clientY-absoluteY("canvas");
    @   if(window.pageXOffset!=null){
    @     x += window.pageXOffset;
    @     y += window.pageYOffset;
    @   }else{
    @     var d = window.document.documentElement;
    @     if(document.compatMode!="CSS1Compat") d = d.body;
    @     x += d.scrollLeft;
    @     y += d.scrollTop;
    @   }
    if( P("clicktest")!=0 ){
      @ alert("click at "+x+","+y)
    }
    @   for(var i in rowinfo){
    @     p = rowinfo[i];
    @     if( p.y<y-11 ) continue;
    @     if( p.y>y+9 ) break;
    @     if( p.x>x-11 && p.x<x+9 ){
    @       clickOnRow(p);
    @       break;
    @     }
    @   }
    @ }
    @ function clickOnRow(p){
    @   if( selRow==null ){
    @     selBox = drawBox("red",p.x-2,p.y-2,p.x+3,p.y+3);
    @     selRow = p;
    @   }else if( selRow==p ){
    @     var canvasDiv = gebi("canvas");
    @     canvasDiv.removeChild(selBox);
    @     selBox = null;
    @     selRow = null;
    @   }else{
    if( fileDiff ){
      @     location.href="%R/fdiff?v1="+selRow.h+"&v2="+p.h+"&sbs=1";
    }else{
      if( db_get_boolean("show-version-diffs", 0)==0 ){
        @     location.href="%R/vdiff?from="+selRow.h+"&to="+p.h+"&sbs=0";
      }else{
        @     location.href="%R/vdiff?from="+selRow.h+"&to="+p.h+"&sbs=1";
      }
    }
    @   }
    @ }
    @ var lastId = "m"+rowinfo[rowinfo.length-1].id;
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
  db_multi_exec("%s", zSql/*safe-for-%s*/);
}

/*
** Return a pointer to a constant string that forms the basis
** for a timeline query for the WWW interface.
*/
const char *timeline_query_for_www(void){
  static const char *zBase = 0;
  static const char zBaseSql[] =
    @ SELECT
    @   blob.rid AS blobRid,
    @   uuid AS uuid,
    @   datetime(event.mtime%s) AS timestamp,
    @   coalesce(ecomment, comment) AS comment,
    @   coalesce(euser, user) AS user,
    @   blob.rid IN leaf AS leaf,
    @   bgcolor AS bgColor,
    @   event.type AS eventType,
    @   (SELECT group_concat(substr(tagname,5), ', ') FROM tag, tagxref
    @     WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @       AND tagxref.rid=blob.rid AND tagxref.tagtype>0) AS tags,
    @   tagid AS tagid,
    @   brief AS brief,
    @   event.mtime AS mtime
    @  FROM event CROSS JOIN blob
    @ WHERE blob.rid=event.objid
  ;
  if( zBase==0 ){
    zBase = mprintf(zBaseSql /*works-like: "%s"*/, timeline_utc());
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
** Convert a symbolic name used as an argument to the a=, b=, or c=
** query parameters of timeline into a julianday mtime value.
*/
double symbolic_name_to_mtime(const char *z){
  double mtime;
  int rid;
  if( z==0 ) return -1.0;
  if( fossil_isdate(z) ){
    mtime = db_double(0.0, "SELECT julianday(%Q,'utc')", z);
    if( mtime>0.0 ) return mtime;
  }
  rid = symbolic_name_to_rid(z, "ci");
  if( rid==0 ) return -1.0;
  mtime = db_double(0.0, "SELECT mtime FROM event WHERE objid=%d", rid);
  return mtime;
}

/*
** The value of one second in julianday notation
*/
#define ONE_SECOND (1.0/86400.0)

/*
** zDate is a localtime date.  Insert records into the
** "timeline" table to cause <hr> to be inserted before and after
** entries of that date.  If zDate==NULL then put dividers around
** the event identified by rid.
*/
static void timeline_add_dividers(double rDate, int rid){
  char *zToDel = 0;
  if( rDate==0 ){
    rDate = db_double(0.0, "SELECT mtime FROM event WHERE objid=%d", rid);
  }
  db_multi_exec(
    "INSERT INTO timeline(rid,sortby,etype)"
    "VALUES(-1,%.16g,'div')",
    rDate-ONE_SECOND
  );
  db_multi_exec(
    "INSERT INTO timeline(rid,sortby,etype)"
    "VALUES(-2,%.17g,'div')",
    rDate+ONE_SECOND
  );
  fossil_free(zToDel);
}

/*
** Return all possible names for file zUuid.
*/
char *names_of_file(const char *zUuid){
  Stmt q;
  Blob out;
  const char *zSep = "";
  db_prepare(&q,
    "SELECT DISTINCT filename.name FROM mlink, filename"
    " WHERE mlink.fid=(SELECT rid FROM blob WHERE uuid=%Q)"
    "   AND filename.fnid=mlink.fnid",
    zUuid
  );
  blob_zero(&out);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFN = db_column_text(&q, 0);
    blob_appendf(&out, "%s%z%h</a>", zSep,
          href("%R/finfo?name=%t", zFN), zFN);
    zSep = " or ";
  }
  db_finalize(&q);
  return blob_str(&out);
}


/*
** WEBPAGE: timeline
**
** Query parameters:
**
**    a=TIMEORTAG    after this event
**    b=TIMEORTAG    before this event
**    c=TIMEORTAG    "circa" this event
**    n=COUNT        max number of events in output
**    p=UUID         artifact and up to COUNT parents and ancestors
**    d=UUID         artifact and up to COUNT descendants
**    dp=UUID        The same as d=UUID&p=UUID
**    t=TAGID        show only check-ins with the given tagid
**    r=TAGID        show check-ins related to tagid
**    u=USER         only if belonging to this user
**    y=TYPE         'ci', 'w', 't', 'e', or (default) 'all'
**    s=TEXT         string search (comment and brief)
**    ng             Suppress the graph if present
**    nd             Suppress "divider" lines
**    v              Show details of files changed
**    f=UUID         Show family (immediate parents and children) of UUID
**    from=UUID      Path from...
**    to=UUID          ... to this
**    nomerge          ... avoid merge links on the path
**    shortest         ... show only the shortest path
**    uf=FUUID       Show only checkins that use given file version
**    brbg           Background color from branch name
**    ubg            Background color from user
**    namechng       Show only checkins that filename changes
**    ym=YYYY-MM     Shown only events for the given year/month.
**
** p= and d= can appear individually or together.  If either p= or d=
** appear, then u=, y=, a=, and b= are ignored.
**
** If a= and b= appear, only a= is used.  If neither appear, the most
** recent events are chosen.
**
** If n= is missing, the default count is 20.
*/
void page_timeline(void){
  Stmt q;                            /* Query used to generate the timeline */
  Blob sql;                          /* text of SQL used to generate timeline */
  Blob desc;                         /* Description of the timeline */
  int nEntry = atoi(PD("n","20"));   /* Max number of entries on timeline */
  int p_rid = name_to_typed_rid(P("p"),"ci");  /* artifact p and its parents */
  int d_rid = name_to_typed_rid(P("d"),"ci");  /* artifact d and descendants */
  int f_rid = name_to_typed_rid(P("f"),"ci");  /* artifact f and close family */
  const char *zUser = P("u");        /* All entries by this user if not NULL */
  const char *zType = PD("y","all"); /* Type of events.  All if NULL */
  const char *zAfter = P("a");       /* Events after this time */
  const char *zBefore = P("b");      /* Events before this time */
  const char *zCirca = P("c");       /* Events near this time */
  const char *zTagName = P("t");     /* Show events with this tag */
  const char *zBrName = P("r");      /* Show events related to this tag */
  const char *zSearch = P("s");      /* Search string */
  const char *zUses = P("uf");       /* Only show checkins hold this file */
  const char *zYearMonth = P("ym");  /* Show checkins for the given YYYY-MM */
  const char *zYearWeek = P("yw");   /* Show checkins for the given YYYY-WW (weak-of-year) */
  int useDividers = P("nd")==0;      /* Show dividers if "nd" is missing */
  int renameOnly = P("namechng")!=0; /* Show only checkins that rename files */
  int tagid;                         /* Tag ID */
  int tmFlags;                       /* Timeline flags */
  const char *zThisTag = 0;          /* Suppress links to this tag */
  const char *zThisUser = 0;         /* Suppress links to this user */
  HQuery url;                        /* URL for various branch links */
  int from_rid = name_to_typed_rid(P("from"),"ci"); /* from= for paths */
  int to_rid = name_to_typed_rid(P("to"),"ci");    /* to= for path timelines */
  int noMerge = P("shortest")==0;           /* Follow merge links if shorter */
  int me_rid = name_to_typed_rid(P("me"),"ci");  /* me= for common ancestory */
  int you_rid = name_to_typed_rid(P("you"),"ci");/* you= for common ancst */
  int pd_rid;
  double rBefore, rAfter, rCirca;     /* Boundary times */

  /* To view the timeline, must have permission to read project data.
  */
  pd_rid = name_to_typed_rid(P("dp"),"ci");
  if( pd_rid ){
    p_rid = d_rid = pd_rid;
  }
  login_check_credentials();
  if( !g.perm.Read && !g.perm.RdTkt && !g.perm.RdWiki ){
    login_needed();
    return;
  }
  url_initialize(&url, "timeline");
  if( zTagName && g.perm.Read ){
    tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname='sym-%q'", zTagName);
    zThisTag = zTagName;
  }else if( zBrName && g.perm.Read ){
    tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname='sym-%q'",zBrName);
    zThisTag = zBrName;
  }else{
    tagid = 0;
  }
  if( zType[0]=='a' ){
    tmFlags = TIMELINE_BRIEF | TIMELINE_GRAPH;
  }else{
    tmFlags = TIMELINE_GRAPH;
  }
  url_add_parameter(&url, "n", mprintf("%d", nEntry));
  if( P("ng")!=0 || zSearch!=0 ){
    tmFlags &= ~TIMELINE_GRAPH;
    url_add_parameter(&url, "ng", 0);
  }
  if( P("brbg")!=0 ){
    tmFlags |= TIMELINE_BRCOLOR;
    url_add_parameter(&url, "brbg", 0);
  }
  if( P("unhide")!=0 ){
    tmFlags |= TIMELINE_UNHIDE;
    url_add_parameter(&url, "unhide", 0);
  }
  if( P("ubg")!=0 ){
    tmFlags |= TIMELINE_UCOLOR;
    url_add_parameter(&url, "ubg", 0);
  }
  if( zUses!=0 ){
    int ufid = db_int(0, "SELECT rid FROM blob WHERE uuid GLOB '%q*'", zUses);
    if( ufid ){
      zUses = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", ufid);
      url_add_parameter(&url, "uf", zUses);
      db_multi_exec("CREATE TEMP TABLE usesfile(rid INTEGER PRIMARY KEY)");
      compute_uses_file("usesfile", ufid, 0);
      zType = "ci";
    }else{
      zUses = 0;
    }
  }
  if( renameOnly ){
    db_multi_exec(
      "CREATE TEMP TABLE rnfile(rid INTEGER PRIMARY KEY);"
      "INSERT OR IGNORE INTO rnfile"
      "  SELECT mid FROM mlink WHERE pfnid>0 AND pfnid!=fnid;"
    );
  }

  style_header("Timeline");
  login_anonymous_available();
  timeline_temp_table();
  blob_zero(&sql);
  blob_zero(&desc);
  blob_append(&sql, "INSERT OR IGNORE INTO timeline ", -1);
  blob_append(&sql, timeline_query_for_www(), -1);
  if( P("fc")!=0 || P("v")!=0 || P("detail")!=0 ){
    tmFlags |= TIMELINE_FCHANGES;
    url_add_parameter(&url, "v", 0);
  }
  if( (tmFlags & TIMELINE_UNHIDE)==0 ){
    blob_append_sql(&sql,
      " AND NOT EXISTS(SELECT 1 FROM tagxref"
      "     WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)",
      TAG_HIDDEN
    );
  }
  if( !useDividers ) url_add_parameter(&url, "nd", 0);
  if( ((from_rid && to_rid) || (me_rid && you_rid)) && g.perm.Read ){
    /* If from= and to= are present, display all nodes on a path connecting
    ** the two */
    PathNode *p = 0;
    const char *zFrom = 0;
    const char *zTo = 0;

    if( from_rid && to_rid ){
      p = path_shortest(from_rid, to_rid, noMerge, 0);
      zFrom = P("from");
      zTo = P("to");
    }else{
      if( path_common_ancestor(me_rid, you_rid) ){
        p = path_first();
      }
      zFrom = P("me");
      zTo = P("you");
    }
    blob_append(&sql, " AND event.objid IN (0", -1);
    while( p ){
      blob_append_sql(&sql, ",%d", p->rid);
      p = p->u.pTo;
    }
    blob_append(&sql, ")", -1);
    path_reset();
    blob_append(&desc, "All nodes on the path from ", -1);
    blob_appendf(&desc, "%z[%h]</a>", href("%R/info/%h", zFrom), zFrom);
    blob_append(&desc, " to ", -1);
    blob_appendf(&desc, "%z[%h]</a>", href("%R/info/%h",zTo), zTo);
    tmFlags |= TIMELINE_DISJOINT;
    db_multi_exec("%s", blob_sql_text(&sql));
  }else if( (p_rid || d_rid) && g.perm.Read ){
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
    blob_append_sql(&sql, " AND event.objid IN ok");
    nd = 0;
    if( d_rid ){
      compute_descendants(d_rid, nEntry+1);
      nd = db_int(0, "SELECT count(*)-1 FROM ok");
      if( nd>=0 ) db_multi_exec("%s", blob_sql_text(&sql));
      if( nd>0 ) blob_appendf(&desc, "%d descendant%s", nd,(1==nd)?"":"s");
      if( useDividers ) timeline_add_dividers(0, d_rid);
      db_multi_exec("DELETE FROM ok");
    }
    if( p_rid ){
      compute_ancestors(p_rid, nEntry+1, 0);
      np = db_int(0, "SELECT count(*)-1 FROM ok");
      if( np>0 ){
        if( nd>0 ) blob_appendf(&desc, " and ");
        blob_appendf(&desc, "%d ancestors", np);
        db_multi_exec("%s", blob_sql_text(&sql));
      }
      if( d_rid==0 && useDividers ) timeline_add_dividers(0, p_rid);
    }
    blob_appendf(&desc, " of %z[%S]</a>",
                   href("%R/info/%s", zUuid), zUuid);
    if( p_rid ){
      url_add_parameter(&url, "p", zUuid);
    }
    if( d_rid ){
      if( p_rid ){
        /* If both p= and d= are set, we don't have the uuid of d yet. */
        zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", d_rid);
      }
      url_add_parameter(&url, "d", zUuid);
    }
    if( nEntry>20 ){
      timeline_submenu(&url, "20 Entries", "n", "20", 0);
    }
    if( nEntry<200 ){
      timeline_submenu(&url, "200 Entries", "n", "200", 0);
    }
    if( tmFlags & TIMELINE_FCHANGES ){
      timeline_submenu(&url, "Hide Files", "v", 0, 0);
    }else{
      timeline_submenu(&url, "Show Files", "v", "", 0);
    }
    if( (tmFlags & TIMELINE_UNHIDE)==0 ){
      timeline_submenu(&url, "Unhide", "unhide", "", 0);
    }
  }else if( f_rid && g.perm.Read ){
    /* If f= is present, ignore all other parameters other than n= */
    char *zUuid;
    db_multi_exec(
       "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
       "INSERT INTO ok VALUES(%d);"
       "INSERT OR IGNORE INTO ok SELECT pid FROM plink WHERE cid=%d;"
       "INSERT OR IGNORE INTO ok SELECT cid FROM plink WHERE pid=%d;",
       f_rid, f_rid, f_rid
    );
    blob_append_sql(&sql, " AND event.objid IN ok");
    db_multi_exec("%s", blob_sql_text(&sql));
    if( useDividers ) timeline_add_dividers(0, f_rid);
    blob_appendf(&desc, "Parents and children of check-in ");
    zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", f_rid);
    blob_appendf(&desc, "%z[%S]</a>", href("%R/info/%s", zUuid), zUuid);
    tmFlags |= TIMELINE_DISJOINT;
    url_add_parameter(&url, "f", zUuid);
    if( tmFlags & TIMELINE_FCHANGES ){
      timeline_submenu(&url, "Hide Files", "v", 0, 0);
    }else{
      timeline_submenu(&url, "Show Files", "v", "", 0);
    }
    if( (tmFlags & TIMELINE_UNHIDE)==0 ){
      timeline_submenu(&url, "Unhide", "unhide", "", 0);
    }
  }else{
    /* Otherwise, a timeline based on a span of time */
    int n;
    const char *zEType = "timeline item";
    char *zDate;
    if( zUses ){
      blob_append_sql(&sql, " AND event.objid IN usesfile ");
    }
    if( renameOnly ){
      blob_append_sql(&sql, " AND event.objid IN rnfile ");
    }
    if( zYearMonth ){
      blob_append_sql(&sql, " AND %Q=strftime('%%Y-%%m',event.mtime) ",
                   zYearMonth);
    }
    else if( zYearWeek ){
      blob_append_sql(&sql, " AND %Q=strftime('%%Y-%%W',event.mtime) ",
                   zYearWeek);
    }
    if( tagid>0 ){
      blob_append_sql(&sql,
        "AND (EXISTS(SELECT 1 FROM tagxref"
                    " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)", tagid);

      if( zBrName ){
        url_add_parameter(&url, "r", zBrName);
        /* The next two blob_appendf() calls add SQL that causes checkins that
        ** are not part of the branch which are parents or children of the
        ** branch to be included in the report.  This related check-ins are
        ** useful in helping to visualize what has happened on a quiescent
        ** branch that is infrequently merged with a much more activate branch.
        */
        blob_append_sql(&sql,
          " OR EXISTS(SELECT 1 FROM plink CROSS JOIN tagxref ON rid=cid"
                     " WHERE tagid=%d AND tagtype>0 AND pid=blob.rid)",
           tagid
        );
        if( (tmFlags & TIMELINE_UNHIDE)==0 ){
          blob_append_sql(&sql,
            " AND NOT EXISTS(SELECT 1 FROM plink JOIN tagxref ON rid=cid"
                       " WHERE tagid=%d AND tagtype>0 AND pid=blob.rid)",
            TAG_HIDDEN
          );
        }
        if( P("mionly")==0 ){
          blob_append_sql(&sql,
            " OR EXISTS(SELECT 1 FROM plink CROSS JOIN tagxref ON rid=pid"
                       " WHERE tagid=%d AND tagtype>0 AND cid=blob.rid)",
            tagid
          );
          if( (tmFlags & TIMELINE_UNHIDE)==0 ){
            blob_append_sql(&sql, 
              " AND NOT EXISTS(SELECT 1 FROM plink JOIN tagxref ON rid=pid"
              " WHERE tagid=%d AND tagtype>0 AND cid=blob.rid)",
              TAG_HIDDEN
            );
          }
        }else{
          url_add_parameter(&url, "mionly", "1");
        }
      }else{
        url_add_parameter(&url, "t", zTagName);
      }
      blob_append_sql(&sql, ")");
    }
    if( (zType[0]=='w' && !g.perm.RdWiki)
     || (zType[0]=='t' && !g.perm.RdTkt)
     || (zType[0]=='e' && !g.perm.RdWiki)
     || (zType[0]=='c' && !g.perm.Read)
     || (zType[0]=='g' && !g.perm.Read)
    ){
      zType = "all";
    }
    if( zType[0]=='a' ){
      if( !g.perm.Read || !g.perm.RdWiki || !g.perm.RdTkt ){
        char cSep = '(';
        blob_append_sql(&sql, " AND event.type IN ");
        if( g.perm.Read ){
          blob_append_sql(&sql, "%c'ci','g'", cSep);
          cSep = ',';
        }
        if( g.perm.RdWiki ){
          blob_append_sql(&sql, "%c'w','e'", cSep);
          cSep = ',';
        }
        if( g.perm.RdTkt ){
          blob_append_sql(&sql, "%c't'", cSep);
          cSep = ',';
        }
        blob_append_sql(&sql, ")");
      }
    }else{ /* zType!="all" */
      blob_append_sql(&sql, " AND event.type=%Q", zType);
      url_add_parameter(&url, "y", zType);
      if( zType[0]=='c' ){
        zEType = "checkin";
      }else if( zType[0]=='w' ){
        zEType = "wiki edit";
      }else if( zType[0]=='t' ){
        zEType = "ticket change";
      }else if( zType[0]=='e' ){
        zEType = "event";
      }else if( zType[0]=='g' ){
        zEType = "tag";
      }
    }
    if( zUser ){
      blob_append_sql(&sql, " AND (event.user=%Q OR event.euser=%Q)",
                   zUser, zUser);
      url_add_parameter(&url, "u", zUser);
      zThisUser = zUser;
    }
    if( zSearch ){
      blob_append_sql(&sql,
        " AND (event.comment LIKE '%%%q%%' OR event.brief LIKE '%%%q%%')",
        zSearch, zSearch);
      url_add_parameter(&url, "s", zSearch);
    }
    rBefore = symbolic_name_to_mtime(zBefore);
    rAfter = symbolic_name_to_mtime(zAfter);
    rCirca = symbolic_name_to_mtime(zCirca);
    if( rAfter>0.0 ){
      if( rBefore>0.0 ){
        blob_append_sql(&sql,
           " AND event.mtime>=%.17g AND event.mtime<=%.17g"
           " ORDER BY event.mtime ASC", rAfter-ONE_SECOND, rBefore+ONE_SECOND);
        url_add_parameter(&url, "a", zAfter);
        url_add_parameter(&url, "b", zBefore);
        nEntry = 1000000;
      }else{
        blob_append_sql(&sql,
           " AND event.mtime>=%.17g  ORDER BY event.mtime ASC",
           rAfter-ONE_SECOND);
        url_add_parameter(&url, "a", zAfter);
      }
    }else if( rBefore>0.0 ){
      blob_append_sql(&sql,
         " AND event.mtime<=%.17g ORDER BY event.mtime DESC",
         rBefore+ONE_SECOND);
      url_add_parameter(&url, "b", zBefore);
    }else if( rCirca>0.0 ){
      Blob sql2;
      blob_init(&sql2, blob_sql_text(&sql), -1);
      blob_append_sql(&sql2,
          " AND event.mtime<=%f ORDER BY event.mtime DESC LIMIT %d",
          rCirca, (nEntry+1)/2
      );
      db_multi_exec("%s", blob_sql_text(&sql2));
      blob_reset(&sql2);
      blob_append_sql(&sql,
          " AND event.mtime>=%f ORDER BY event.mtime ASC",
          rCirca
      );
      nEntry -= (nEntry+1)/2;
      if( useDividers ) timeline_add_dividers(rCirca, 0);
      url_add_parameter(&url, "c", zCirca);
    }else{
      blob_append_sql(&sql, " ORDER BY event.mtime DESC");
    }
    blob_append_sql(&sql, " LIMIT %d", nEntry);
    db_multi_exec("%s", blob_sql_text(&sql));

    n = db_int(0, "SELECT count(*) FROM timeline WHERE etype!='div' /*scan*/");
    if( zYearMonth ){
      blob_appendf(&desc, "%s events for %h", zEType, zYearMonth);
    }else if( zYearWeek ){
      blob_appendf(&desc, "%s events for year/week %h", zEType, zYearWeek);
    }else if( zAfter==0 && zBefore==0 && zCirca==0 ){
      blob_appendf(&desc, "%d most recent %ss", n, zEType);
    }else{
      blob_appendf(&desc, "%d %ss", n, zEType);
    }
    if( zUses ){
      char *zFilenames = names_of_file(zUses);
      blob_appendf(&desc, " using file %s version %z%S</a>", zFilenames,
                   href("%R/artifact/%s",zUses), zUses);
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( renameOnly ){
      blob_appendf(&desc, " that contain filename changes");
      tmFlags |= TIMELINE_DISJOINT|TIMELINE_FRENAMES;
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
    if( rAfter>0.0 ){
      if( rBefore>0.0 ){
        blob_appendf(&desc, " occurring between %h and %h.<br>",
                     zAfter, zBefore);
      }else{
        blob_appendf(&desc, " occurring on or after %h.<br />", zAfter);
      }
    }else if( rBefore>0.0 ){
      blob_appendf(&desc, " occurring on or before %h.<br />", zBefore);
    }else if( rCirca>0.0 ){
      blob_appendf(&desc, " occurring around %h.<br />", zCirca);
    }
    if( zSearch ){
      blob_appendf(&desc, " matching \"%h\"", zSearch);
    }
    if( g.perm.Hyperlink ){
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
        if( zType[0]!='w' && g.perm.RdWiki ){
          timeline_submenu(&url, "Wiki Only", "y", "w", 0);
        }
        if( zType[0]!='c' && g.perm.Read ){
          timeline_submenu(&url, "Checkins Only", "y", "ci", 0);
        }
        if( zType[0]!='t' && g.perm.RdTkt ){
          timeline_submenu(&url, "Tickets Only", "y", "t", 0);
        }
        if( zType[0]!='e' && g.perm.RdWiki ){
          timeline_submenu(&url, "Events Only", "y", "e", 0);
        }
        if( zType[0]!='g' && g.perm.Read ){
          timeline_submenu(&url, "Tags Only", "y", "g", 0);
        }
      }
      if( nEntry>20 ){
        timeline_submenu(&url, "20 Entries", "n", "20", 0);
      }
      if( nEntry<200 ){
        timeline_submenu(&url, "200 Entries", "n", "200", 0);
      }
      if( zType[0]=='a' || zType[0]=='c' ){
        if( tmFlags & TIMELINE_FCHANGES ){
          timeline_submenu(&url, "Hide Files", "v", 0, 0);
        }else{
          timeline_submenu(&url, "Show Files", "v", "", 0);
        }
        if( (tmFlags & TIMELINE_UNHIDE)==0 ){
          timeline_submenu(&url, "Unhide", "unhide", "", 0);
        }
      }
    }
  }
  if( P("showsql") ){
    @ <blockquote>%h(blob_sql_text(&sql))</blockquote>
  }
  blob_zero(&sql);
  db_prepare(&q, "SELECT * FROM timeline ORDER BY sortby DESC /*scan*/");
  @ <h2>%b(&desc)</h2>
  blob_reset(&desc);
  www_print_timeline(&q, tmFlags, zThisUser, zThisTag, 0);
  db_finalize(&q);
  style_footer();
}

/*
** The input query q selects various records.  Print a human-readable
** summary of those records.
**
** Limit number of lines or entries printed to nLimit.  If nLimit is zero
** there is no limit.  If nLimit is greater than zero, limit the number of
** complete entries printed.  If nLimit is less than zero, attempt to limit
** the number of lines printed (this is basically the legacy behavior).
** The line limit, if used, is approximate because it is only checked on a
** per-entry basis.  If verbose mode, the file name details are considered
** to be part of the entry.
**
** The query should return these columns:
**
**    0.  rid
**    1.  uuid
**    2.  Date/Time
**    3.  Comment string and user
**    4.  Number of non-merge children
**    5.  Number of parents
**    6.  mtime
**    7.  branch
*/
void print_timeline(Stmt *q, int nLimit, int width, int verboseFlag){
  int nAbsLimit = (nLimit >= 0) ? nLimit : -nLimit;
  int nLine = 0;
  int nEntry = 0;
  char zPrevDate[20];
  const char *zCurrentUuid = 0;
  int fchngQueryInit = 0;     /* True if fchngQuery is initialized */
  Stmt fchngQuery;            /* Query for file changes on check-ins */
  int rc;

  zPrevDate[0] = 0;
  if( g.localOpen ){
    int rid = db_lget_int("checkout", 0);
    zCurrentUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  }

  while( (rc=db_step(q))==SQLITE_ROW ){
    int rid = db_column_int(q, 0);
    const char *zId = db_column_text(q, 1);
    const char *zDate = db_column_text(q, 2);
    const char *zCom = db_column_text(q, 3);
    int nChild = db_column_int(q, 4);
    int nParent = db_column_int(q, 5);
    char *zFree = 0;
    int n = 0;
    char zPrefix[80];

    if( nAbsLimit!=0 ){
      if( nLimit<0 && nLine>=nAbsLimit ){
        fossil_print("--- line limit (%d) reached ---\n", nAbsLimit);
        break; /* line count limit hit, stop. */
      }else if( nEntry>=nAbsLimit ){
        fossil_print("--- entry limit (%d) reached ---\n", nAbsLimit);
        break; /* entry count limit hit, stop. */
      }
    }
    if( fossil_strnicmp(zDate, zPrevDate, 10) ){
      fossil_print("=== %.10s ===\n", zDate);
      memcpy(zPrevDate, zDate, 10);
      nLine++; /* record another line */
    }
    if( zCom==0 ) zCom = "";
    fossil_print("%.8s ", &zDate[11]);
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
    if( fossil_strcmp(zCurrentUuid,zId)==0 ){
      sqlite3_snprintf(sizeof(zPrefix)-n, &zPrefix[n], "*CURRENT* ");
      n += strlen(zPrefix);
    }
    zFree = mprintf("[%S] %s%s", zId, zPrefix, zCom);
    /* record another X lines */
    nLine += comment_print(zFree, zCom, 9, width, g.comFmtFlags);
    fossil_free(zFree);

    if(verboseFlag){
      if( !fchngQueryInit ){
        db_prepare(&fchngQuery,
           "SELECT (pid==0) AS isnew,"
           "       (fid==0) AS isdel,"
           "       (SELECT name FROM filename WHERE fnid=mlink.fnid) AS name,"
           "       (SELECT uuid FROM blob WHERE rid=fid),"
           "       (SELECT uuid FROM blob WHERE rid=pid)"
           "  FROM mlink"
           " WHERE mid=:mid AND pid!=fid"
           " ORDER BY 3 /*sort*/"
        );
        fchngQueryInit = 1;
      }
      db_bind_int(&fchngQuery, ":mid", rid);
      while( db_step(&fchngQuery)==SQLITE_ROW ){
        const char *zFilename = db_column_text(&fchngQuery, 2);
        int isNew = db_column_int(&fchngQuery, 0);
        int isDel = db_column_int(&fchngQuery, 1);
        if( isNew ){
          fossil_print("   ADDED %s\n",zFilename);
        }else if( isDel ){
          fossil_print("   DELETED %s\n",zFilename);
        }else{
          fossil_print("   EDITED %s\n", zFilename);
        }
        nLine++; /* record another line */
      }
      db_reset(&fchngQuery);
    }
    nEntry++; /* record another complete entry */
  }
  if( rc==SQLITE_DONE ){
    /* Did the underlying query actually have all entries? */
    if( nAbsLimit==0 ){
      fossil_print("+++ end of timeline (%d) +++\n", nEntry);
    }else{
      fossil_print("+++ no more data (%d) +++\n", nEntry);
    }
  }
  if( fchngQueryInit ) db_finalize(&fchngQuery);
}

/*
** Return a pointer to a static string that forms the basis for
** a timeline query for display on a TTY.
*/
const char *timeline_query_for_tty(void){
  static const char zBaseSql[] =
    @ SELECT
    @   blob.rid AS rid,
    @   uuid,
    @   datetime(event.mtime%s) AS mDateTime,
    @   coalesce(ecomment,comment)
    @     || ' (user: ' || coalesce(euser,user,'?')
    @     || (SELECT case when length(x)>0 then ' tags: ' || x else '' end
    @           FROM (SELECT group_concat(substr(tagname,5), ', ') AS x
    @                   FROM tag, tagxref
    @                  WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @                    AND tagxref.rid=blob.rid AND tagxref.tagtype>0))
    @     || ')' as comment,
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim)
    @        AS primPlinkCount,
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid) AS plinkCount,
    @   event.mtime AS mtime,
    @   tagxref.value AS branch
    @ FROM tag CROSS JOIN event CROSS JOIN blob
    @      LEFT JOIN tagxref ON tagxref.tagid=tag.tagid
    @   AND tagxref.tagtype>0
    @   AND tagxref.rid=blob.rid
    @ WHERE blob.rid=event.objid
    @   AND tag.tagname='branch'
  ;
  return mprintf(zBaseSql /*works-like: "%s"*/, timeline_utc());
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
** Usage: %fossil timeline ?WHEN? ?BASELINE|DATETIME? ?OPTIONS?
**
** Print a summary of activity going backwards in date and time
** specified or from the current date and time if no arguments
** are given.  The WHEN argument can be any unique abbreviation
** of one of these keywords:
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
** Options:
**   -n|--limit N         Output the first N entries (default 20 lines).
**                        N=0 means no limit.
**   --offset P           skip P changes
**   -t|--type TYPE       Output items from the given types only, such as:
**                            ci = file commits only
**                            e  = events only
**                            t  = tickets only
**                            w  = wiki commits only
**   -v|--verbose         Output the list of files changed by each commit
**                        and the type of each change (edited, deleted,
**                        etc.) after the checkin comment.
**   -W|--width <num>     Width of lines (default is to auto-detect). Must be
**                        >20 or 0 (= no limit, resulting in a single line per
**                        entry).
**   -R REPO_FILE         Specifies the repository db to use. Default is
**                        the current checkout's repository.
*/
void timeline_cmd(void){
  Stmt q;
  int n, k, width, i;
  const char *zLimit;
  const char *zWidth;
  const char *zOffset;
  const char *zType;
  char *zOrigin;
  char *zDate;
  Blob sql;
  int objid = 0;
  Blob uuid;
  int mode = 0 ;       /* 0:none  1: before  2:after  3:children  4:parents */
  int verboseFlag = 0 ;
  int iOffset;
  const char *zFilePattern = 0;
  Blob treeName;

  verboseFlag = find_option("verbose","v", 0)!=0;
  if( !verboseFlag){
    verboseFlag = find_option("showfiles","f", 0)!=0; /* deprecated */
  }
  db_find_and_open_repository(0, 0);
  zLimit = find_option("limit","n",1);
  zWidth = find_option("width","W",1);
  zType = find_option("type","t",1);
  if( !zLimit ){
    zLimit = find_option("count",0,1);
  }
  if( zLimit ){
    n = atoi(zLimit);
  }else{
    n = -20;
  }
  if( zWidth ){
    width = atoi(zWidth);
    if( (width!=0) && (width<=20) ){
      fossil_fatal("-W|--width value must be >20 or 0");
    }
  }else{
    width = -1;
  }
  zOffset = find_option("offset",0,1);
  iOffset = zOffset ? atoi(zOffset) : 0;

  /* We should be done with options.. */
  verify_all_options();

  zOrigin = "now";
  zFilePattern = 0;
  for(i=2; i<g.argc; i++){
    char *zArg = g.argv[i];
    k = strlen(zArg);
    if( mode==0 ){
      if( strncmp(zArg,"before",k)==0 ){
        mode = 1;
      }else if( strncmp(zArg,"after",k)==0 && k>1 ){
        mode = 2;
      }else if( strncmp(zArg,"descendants",k)==0 ){
        mode = 3;
      }else if( strncmp(zArg,"children",k)==0 ){
        mode = 3;
      }else if( strncmp(zArg,"ancestors",k)==0 && k>1 ){
        mode = 4;
      }else if( strncmp(zArg,"parents",k)==0 ){
        mode = 4;
      }
      if( mode ){
        if( i<g.argc-1 ) zOrigin = g.argv[++i];
        continue;
      }
    }
    if( zFilePattern==0 ){
      zFilePattern = zArg;
    }else{
      usage("?WHEN? ?CHECKIN|DATETIME? ?FILE? ?OPTIONS?");
    }
  }
  k = strlen(zOrigin);
  blob_zero(&uuid);
  blob_append(&uuid, zOrigin, -1);
  if( fossil_strcmp(zOrigin, "now")==0 ){
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
  }else if( name_to_uuid(&uuid, 0, "*")==0 ){
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
  
  if( zFilePattern ){
    if( zType==0 ){
      /* When zFilePattern is specified and type is not specified, only show
       * file checkins */
      zType="ci";
    }
    file_tree_name(zFilePattern, &treeName, 1);
    if( fossil_strcmp(blob_str(&treeName), ".")==0 ){
      /* When zTreeName refers to g.zLocalRoot, it's like not specifying
       * zFilePattern. */
      zFilePattern = 0;
    }
  }

  if( mode==0 ) mode = 1;
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_tty(), -1);
  blob_append_sql(&sql, "  AND event.mtime %s %s",
     (mode==1 || mode==4) ? "<=" : ">=",
     zDate /*safe-for-%s*/
  );

  if( mode==3 || mode==4 ){
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    if( mode==3 ){
      compute_descendants(objid, n);
    }else{
      compute_ancestors(objid, n, 0);
    }
    blob_append_sql(&sql, "\n  AND blob.rid IN ok");
  }
  if( zType && (zType[0]!='a') ){
    blob_append_sql(&sql, "\n  AND event.type=%Q ", zType);
  }
  if( zFilePattern ){
    blob_append(&sql,
       "\n  AND EXISTS(SELECT 1 FROM mlink\n"
         "              WHERE mlink.mid=event.objid\n"
         "                AND mlink.fnid IN ", -1);
    if( filenames_are_case_sensitive() ){
      blob_append_sql(&sql,
        "(SELECT fnid FROM filename"
        " WHERE name=%Q"
        " OR name GLOB '%q/*')",
        blob_str(&treeName), blob_str(&treeName));
    }else{
      blob_append_sql(&sql,
        "(SELECT fnid FROM filename"
        " WHERE name=%Q COLLATE nocase"
        " OR lower(name) GLOB lower('%q/*'))",
        blob_str(&treeName), blob_str(&treeName));
    }
    blob_append(&sql, ")", -1);
  }
  blob_append_sql(&sql, "\nORDER BY event.mtime DESC");
  if( iOffset>0 ){
    /* Don't handle LIMIT here, otherwise print_timeline()
     * will not determine the end-marker correctly! */
    blob_append_sql(&sql, "\n LIMIT -1 OFFSET %d", iOffset);
  }
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  print_timeline(&q, n, width, verboseFlag);
  db_finalize(&q);
}

/*
** Return one of two things:
**
**   ",'localtime'"  if the timeline-utc property is set to 0.
**
**   ""              (empty string) otherwise.
*/
const char *timeline_utc(){
  if( g.fTimeFormat==0 ){
    if( db_get_int("timeline-utc", 1) ){
      g.fTimeFormat = 1;
    }else{
      g.fTimeFormat = 2;
    }
  }
  if( g.fTimeFormat==1 ){
    return "";
  }else{
    return ",'localtime'";
  }
}


/*
** COMMAND: test-timewarp-list
**
** Usage: %fossil test-timewarp-list ?-v|---verbose?
**
** Display all instances of child checkins that appear earlier in time
** than their parent.  If the -v|--verbose option is provided, both the
** parent and child checking and their times are shown.
*/
void test_timewarp_cmd(void){
  Stmt q;
  int verboseFlag;

  db_find_and_open_repository(0, 0);
  verboseFlag = find_option("verbose", "v", 0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("detail", 0, 0)!=0; /* deprecated */
  }
  db_prepare(&q,
     "SELECT (SELECT uuid FROM blob WHERE rid=p.cid),"
     "       (SELECT uuid FROM blob WHERE rid=c.cid),"
     "       datetime(p.mtime), datetime(c.mtime)"
     "  FROM plink p, plink c"
     " WHERE p.cid=c.pid  AND p.mtime>c.mtime"
  );
  while( db_step(&q)==SQLITE_ROW ){
    if( !verboseFlag ){
      fossil_print("%s\n", db_column_text(&q, 1));
    }else{
      fossil_print("%.14s -> %.14s   %s -> %s\n",
         db_column_text(&q, 0),
         db_column_text(&q, 1),
         db_column_text(&q, 2),
         db_column_text(&q, 3));
    }
  }
  db_finalize(&q);
}

/*
** WEBPAGE: test_timewarps
*/
void test_timewarp_page(void){
  Stmt q;

  login_check_credentials();
  if( !g.perm.Read || !g.perm.Hyperlink ){ login_needed(); return; }
  style_header("Instances of timewarp");
  @ <ul>
  db_prepare(&q,
     "SELECT blob.uuid "
     "  FROM plink p, plink c, blob"
     " WHERE p.cid=c.pid  AND p.mtime>c.mtime"
     "   AND blob.rid=c.cid"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    @ <li>
    @ <a href="%s(g.zTop)/timeline?p=%s(zUuid)&amp;d=%s(zUuid)&amp;unhide">%S(zUuid)</a>
  }
  db_finalize(&q);
  style_footer();
}


/*
** Used by stats_report_xxxxx() to remember which type of events
** to show. Populated by stats_report_init_view() and holds the
** return value of that function.
*/
static int statsReportType = 0;

/*
** Set by stats_report_init_view() to one of the y=XXXX values
** accepted by /timeline?y=XXXX.
*/
static const char *statsReportTimelineYFlag = NULL;

/*
** Creates a TEMP VIEW named v_reports which is a wrapper around the
** EVENT table filtered on event.type. It looks for the request
** parameter 'type' (reminder: we "should" use 'y' for consistency
** with /timeline, but /reports uses 'y' for the year) and expects it
** to contain one of the conventional values from event.type or the
** value "all", which is treated as equivalent to "*".  By default (if
** no 'y' is specified), "*" is assumed (that is also the default for
** invalid/unknown filter values). That 'y' filter is the one used for
** the event list. Note that a filter of "*" or "all" is equivalent to
** querying against the full event table. The view, however, adds an
** abstraction level to simplify the implementation code for the
** various /reports pages.
**
** Returns one of: 'c', 'w', 'g', 't', 'e', representing the type of
** filter it applies, or '*' if no filter is applied (i.e. if "all" is
** used).
*/
static int stats_report_init_view(){
  const char *zType = PD("type","*");  /* analog to /timeline?y=... */
  const char *zRealType = NULL;        /* normalized form of zType */
  int rc = 0;                          /* result code */
  assert( !statsReportType && "Must not be called more than once." );
  switch( (zType && *zType) ? *zType : 0 ){
    case 'c':
    case 'C':
      zRealType = "ci";
      rc = *zRealType;
      break;
    case 'e':
    case 'E':
      zRealType = "e";
      rc = *zRealType;
      break;
    case 'g':
    case 'G':
      zRealType = "g";
      rc = *zRealType;
      break;
    case 't':
    case 'T':
      zRealType = "t";
      rc = *zRealType;
      break;
    case 'w':
    case 'W':
      zRealType = "w";
      rc = *zRealType;
      break;
    default:
      rc = '*';
      break;
  }
  assert(0 != rc);
  if(zRealType){
    statsReportTimelineYFlag = zRealType;
    db_multi_exec("CREATE TEMP VIEW v_reports AS "
                  "SELECT * FROM event WHERE type GLOB %Q",
                  zRealType);
  }else{
    statsReportTimelineYFlag = "a";
    db_multi_exec("CREATE TEMP VIEW v_reports AS "
                  "SELECT * FROM event");
  }
  return statsReportType = rc;
}

/*
** Returns a string suitable (for a given value of suitable) for
** use in a label with the header of the /reports pages, dependent
** on the 'type' flag. See stats_report_init_view().
** The returned bytes are static.
*/
static const char *stats_report_label_for_type(){
  assert( statsReportType && "Must call stats_report_init_view() first." );
  switch( statsReportType ){
    case 'c':
      return "checkins";
    case 'e':
      return "events";
    case 'w':
      return "wiki changes";
    case 't':
      return "ticket changes";
    case 'g':
      return "tag changes";
    default:
      return "all types";
  }
}

/*
** A helper for the /reports family of pages which prints out a menu
** of links for the various type=XXX flags. zCurrentViewName must be
** the name/value of the 'view' parameter which is in effect at the
** time this is called. e.g. if called from the 'byuser' view then
** zCurrentViewName must be "byuser". Any URL parameters which need to
** be added to the generated URLs should be passed in zParam. The
** caller is expected to have already encoded any zParam in the %T or
** %t encoding.  */
static void stats_report_event_types_menu(const char *zCurrentViewName,
                                          const char *zParam){
  char *zTop;
  if(zParam && !*zParam){
    zParam = NULL;
  }
  zTop = mprintf("%s/reports?view=%s%s%s", g.zTop, zCurrentViewName,
                 zParam ? "&" : "", zParam);
  cgi_printf("<div>");
  cgi_printf("<span>Types:</span> ");
  if('*' == statsReportType){
    cgi_printf(" <strong>all</strong>", zTop);
  }else{
    cgi_printf(" <a href='%s'>all</a>", zTop);
  }
  if('c' == statsReportType){
    cgi_printf(" <strong>checkins</strong>", zTop);
  }else{
    cgi_printf(" <a href='%s&type=ci'>checkins</a>", zTop);
  }
  if('e' == statsReportType){
    cgi_printf(" <strong>events</strong>", zTop);
  }else{
    cgi_printf(" <a href='%s&type=e'>events</a>", zTop);
  }
  if( 't' == statsReportType ){
    cgi_printf(" <strong>tickets</strong>", zTop);
  }else{
    cgi_printf(" <a href='%s&type=t'>tickets</a>", zTop);
  }
  if( 'g' == statsReportType ){
    cgi_printf(" <strong>tags</strong>", zTop);
  }else{
    cgi_printf(" <a href='%s&type=g'>tags</a>", zTop);
  }
  if( 'w' == statsReportType ){
    cgi_printf(" <strong>wiki</strong>", zTop);
  }else{
    cgi_printf(" <a href='%s&type=w'>wiki</a>", zTop);
  }
  fossil_free(zTop);
  cgi_printf("</div>");
}


/*
** Helper for stats_report_by_month_year(), which generates a list of
** week numbers. zTimeframe should be either a timeframe in the form YYYY
** or YYYY-MM.
*/
static void stats_report_output_week_links(const char *zTimeframe){
  Stmt stWeek = empty_Stmt;
  char yearPart[5] = {0,0,0,0,0};
  memcpy(yearPart, zTimeframe, 4);
  db_prepare(&stWeek,
             "SELECT DISTINCT strftime('%%W',mtime) AS wk, "
             "count(*) AS n, "
             "substr(date(mtime),1,%d) AS ym "
             "FROM v_reports "
             "WHERE ym=%Q AND mtime < current_timestamp "
             "GROUP BY wk ORDER BY wk",
             strlen(zTimeframe),
             zTimeframe);
  while( SQLITE_ROW == db_step(&stWeek) ){
    const char *zWeek = db_column_text(&stWeek,0);
    const int nCount = db_column_int(&stWeek,1);
    cgi_printf("<a href='%s/timeline?"
               "yw=%t-%t&n=%d&y=%s'>%s</a>",
               g.zTop, yearPart, zWeek,
               nCount, statsReportTimelineYFlag, zWeek);
  }
  db_finalize(&stWeek);
}

/*
** Implements the "byyear" and "bymonth" reports for /reports.
** If includeMonth is true then it generates the "bymonth" report,
** else the "byyear" report. If zUserName is not NULL and not empty
** then the report is restricted to events created by the named user
** account.
*/
static void stats_report_by_month_year(char includeMonth,
                                       char includeWeeks,
                                       const char *zUserName){
  Stmt query = empty_Stmt;
  int nRowNumber = 0;                /* current TR number */
  int nEventTotal = 0;               /* Total event count */
  int rowClass = 0;                  /* counter for alternating
                                        row colors */
  Blob sql = empty_blob;             /* SQL */
  const char *zTimeLabel = includeMonth ? "Year/Month" : "Year";
  char zPrevYear[5] = {0};           /* For keeping track of when
                                        we change years while looping */
  int nEventsPerYear = 0;            /* Total event count for the
                                        current year */
  char showYearTotal = 0;            /* Flag telling us when to show
                                        the per-year event totals */
  Blob header = empty_blob;          /* Page header text */
  int nMaxEvents  = 1;               /* for calculating length of graph
                                        bars. */
  int iterations = 0;                /* number of weeks/months we iterate
                                        over */
  stats_report_init_view();
  stats_report_event_types_menu( includeMonth ? "bymonth" : "byyear", NULL );
  blob_appendf(&header, "Timeline Events (%s) by year%s",
               stats_report_label_for_type(),
               (includeMonth ? "/month" : ""));
  blob_append_sql(&sql,
               "SELECT substr(date(mtime),1,%d) AS timeframe, "
               "count(*) AS eventCount "
               "FROM v_reports ",
               includeMonth ? 7 : 4);
  if(zUserName&&*zUserName){
    blob_append_sql(&sql, " WHERE user=%Q ", zUserName);
    blob_appendf(&header," for user %q", zUserName);
  }
  blob_append(&sql,
              " GROUP BY timeframe"
              " ORDER BY timeframe DESC",
              -1);
  db_prepare(&query, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  @ <h1>%b(&header)</h1>
  @ <table class='statistics-report-table-events' border='0' cellpadding='2'
  @  cellspacing='0' id='statsTable'>
  @ <thead>
  @ <th>%s(zTimeLabel)</th>
  @ <th>Events</th>
  @ <th width='90%%'><!-- relative commits graph --></th>
  @ </thead><tbody>
  blob_reset(&header);
  /*
     Run the query twice. The first time we calculate the maximum
     number of events for a given row. Maybe someone with better SQL
     Fu can re-implement this with a single query.
  */
  while( SQLITE_ROW == db_step(&query) ){
    const int nCount = db_column_int(&query, 1);
    if(nCount>nMaxEvents){
      nMaxEvents = nCount;
    }
    ++iterations;
  }
  db_reset(&query);
  while( SQLITE_ROW == db_step(&query) ){
    const char *zTimeframe = db_column_text(&query, 0);
    const int nCount = db_column_int(&query, 1);
    int nSize = nCount
      ? (int)(100 * nCount / nMaxEvents)
      : 1;
    showYearTotal = 0;
    if(!nSize) nSize = 1;
    if(includeMonth){
      /* For Month/year view, add a separator for each distinct year. */
      if(!*zPrevYear ||
         (0!=fossil_strncmp(zPrevYear,zTimeframe,4))){
        showYearTotal = *zPrevYear;
        if(showYearTotal){
          rowClass = ++nRowNumber % 2;
          @ <tr class='row%d(rowClass)'>
          @ <td></td>
          @ <td colspan='2'>Yearly total: %d(nEventsPerYear)</td>
          @</tr>
        }
        nEventsPerYear = 0;
        memcpy(zPrevYear,zTimeframe,4);
        rowClass = ++nRowNumber % 2;
        @ <tr class='row%d(rowClass)'>
        @ <th colspan='3' class='statistics-report-row-year'>%s(zPrevYear)</th>
        @ </tr>
     }
   }
   rowClass = ++nRowNumber % 2;
   nEventTotal += nCount;
   nEventsPerYear += nCount;
   @<tr class='row%d(rowClass)'>
   @ <td>
    if(includeMonth){
      cgi_printf("<a href='%s/timeline?"
                 "ym=%t&n=%d&y=%s",
                 g.zTop, zTimeframe, nCount,
                 statsReportTimelineYFlag );
      /* Reminder: n=nCount is not actually correct for bymonth unless
         that was the only user who caused events.
      */
      if( zUserName && *zUserName ){
        cgi_printf("&u=%t", zUserName);
      }
      cgi_printf("' target='_new'>%s</a>",zTimeframe);
    }else {
      cgi_printf("<a href='?view=byweek&y=%s&type=%c",
                 zTimeframe, (char)statsReportType);
      if(zUserName && *zUserName){
        cgi_printf("&u=%t", zUserName);
      }
      cgi_printf("'>%s</a>", zTimeframe);
    }
    @ </td><td>%d(nCount)</td>
    @ <td>
    @ <div class='statistics-report-graph-line'
    @  style='width:%d(nSize)%%;'>&nbsp;</div>
    @ </td>
    @</tr>
    if(includeWeeks){
      /* This part works fine for months but it terribly slow (4.5s on my PC),
         so it's only shown for by-year for now. Suggestions/patches for
         a better/faster layout are welcomed. */
      @ <tr class='row%d(rowClass)'>
      @ <td colspan='2' class='statistics-report-week-number-label'>Week #:</td>
      @ <td class='statistics-report-week-of-year-list'>
      stats_report_output_week_links(zTimeframe);
      @ </td></tr>
    }

    /*
      Potential improvement: calculate the min/max event counts and
      use percent-based graph bars.
    */
  }
  db_finalize(&query);
  if(includeMonth && !showYearTotal && *zPrevYear){
    /* Add final year total separator. */
    rowClass = ++nRowNumber % 2;
    @ <tr class='row%d(rowClass)'>
    @ <td></td>
    @ <td colspan='2'>Yearly total: %d(nEventsPerYear)</td>
    @</tr>
  }
  @ </tbody></table>
  if(nEventTotal){
    const char *zAvgLabel = includeMonth ? "month" : "year";
    int nAvg = iterations ? (nEventTotal/iterations) : 0;
    @ <br><div>Total events: %d(nEventTotal)
    @ <br>Average per active %s(zAvgLabel): %d(nAvg)
    @ </div>
  }
  if( !includeMonth ){
    output_table_sorting_javascript("statsTable","tnx");
  }
}

/*
** Implements the "byuser" view for /reports.
*/
static void stats_report_by_user(){
  Stmt query = empty_Stmt;
  int nRowNumber = 0;                /* current TR number */
  int nEventTotal = 0;               /* Total event count */
  int rowClass = 0;                  /* counter for alternating
                                        row colors */
  int nMaxEvents = 1;                /* max number of events for
                                        all rows. */
  stats_report_init_view();
  stats_report_event_types_menu("byuser", NULL);
  db_prepare(&query,
               "SELECT user, "
               "COUNT(*) AS eventCount "
               "FROM v_reports "
               "GROUP BY user ORDER BY eventCount DESC");
  @ <h1>Timeline Events
  @ (%s(stats_report_label_for_type())) by User</h1>
  @ <table class='statistics-report-table-events' border='0'
  @ cellpadding='2' cellspacing='0' id='statsTable'>
  @ <thead><tr>
  @ <th>User</th>
  @ <th>Events</th>
  @ <th width='90%%'><!-- relative commits graph --></th>
  @ </tr></thead><tbody>
  while( SQLITE_ROW == db_step(&query) ){
    const int nCount = db_column_int(&query, 1);
    if(nCount>nMaxEvents){
      nMaxEvents = nCount;
    }
  }
  db_reset(&query);
  while( SQLITE_ROW == db_step(&query) ){
    const char *zUser = db_column_text(&query, 0);
    const int nCount = db_column_int(&query, 1);
    int nSize = nCount
      ? (int)(100 * nCount / nMaxEvents)
      : 0;
    if(!nCount) continue /* arguable! Possible? */;
    else if(!nSize) nSize = 1;
    rowClass = ++nRowNumber % 2;
    nEventTotal += nCount;
    @<tr class='row%d(rowClass)'>
    @ <td>
    @ <a href="?view=bymonth&user=%h(zUser)&type=%c((char)statsReportType)">%h(zUser)</a>
    @ </td><td>%d(nCount)</td>
    @ <td>
    @ <div class='statistics-report-graph-line'
    @  style='width:%d(nSize)%%;'>&nbsp;</div>
    @ </td>
    @</tr>
    /*
      Potential improvement: calculate the min/max event counts and
      use percent-based graph bars.
    */
  }
  @ </tbody></table>
  db_finalize(&query);
  output_table_sorting_javascript("statsTable","tnx");
}

/*
** Implements the "byweekday" view for /reports.
*/
static void stats_report_day_of_week(){
  Stmt query = empty_Stmt;
  int nRowNumber = 0;                /* current TR number */
  int nEventTotal = 0;               /* Total event count */
  int rowClass = 0;                  /* counter for alternating
                                        row colors */
  int nMaxEvents = 1;                /* max number of events for
                                        all rows. */
  static const char *const daysOfWeek[] = {
  "Monday", "Tuesday", "Wednesday", "Thursday",
  "Friday", "Saturday", "Sunday"
  };

  stats_report_init_view();
  stats_report_event_types_menu("byweekday", NULL);
  db_prepare(&query,
               "SELECT cast(mtime %% 7 AS INTEGER) dow, "
               "COUNT(*) AS eventCount "
               "FROM v_reports "
               "GROUP BY dow ORDER BY dow");
  @ <h1>Timeline Events
  @ (%s(stats_report_label_for_type())) by Day of the Week</h1>
  @ <table class='statistics-report-table-events' border='0'
  @ cellpadding='2' cellspacing='0' id='statsTable'>
  @ <thead><tr>
  @ <th>DoW</th>
  @ <th>Day</th>
  @ <th>Events</th>
  @ <th width='90%%'><!-- relative commits graph --></th>
  @ </tr></thead><tbody>
  while( SQLITE_ROW == db_step(&query) ){
    const int nCount = db_column_int(&query, 1);
    if(nCount>nMaxEvents){
      nMaxEvents = nCount;
    }
  }
  db_reset(&query);
  while( SQLITE_ROW == db_step(&query) ){
    const int dayNum =db_column_int(&query, 0);
    const int nCount = db_column_int(&query, 1);
    int nSize = nCount
      ? (int)(100 * nCount / nMaxEvents)
      : 0;
    if(!nCount) continue /* arguable! Possible? */;
    else if(!nSize) nSize = 1;
    rowClass = ++nRowNumber % 2;
    nEventTotal += nCount;
    @<tr class='row%d(rowClass)'>
    @ <td>%d(dayNum)</td>
    @ <td>%s(daysOfWeek[dayNum])</td>
    @ <td>%d(nCount)</td>
    @ <td>
    @ <div class='statistics-report-graph-line'
    @  style='width:%d(nSize)%%;'>&nbsp;</div>
    @ </td>
    @</tr>
  }
  @ </tbody></table>
  db_finalize(&query);
  output_table_sorting_javascript("statsTable","ntnx");
}


/*
** Helper for stats_report_by_month_year(), which generates a list of
** week numbers. zTimeframe should be either a timeframe in the form YYYY
** or YYYY-MM.
*/
static void stats_report_year_weeks(const char *zUserName){
  const char *zYear = P("y");
  int nYear = zYear ? strlen(zYear) : 0;
  int i = 0;
  Stmt qYears = empty_Stmt;
  char *zDefaultYear = NULL;
  Blob sql = empty_blob;
  int nMaxEvents = 1;                /* max number of events for
                                        all rows. */
  int iterations = 0;                /* # of active time periods. */
  stats_report_init_view();
  if(4==nYear){
    Blob urlParams = empty_blob;
    blob_appendf(&urlParams, "y=%T", zYear);
    stats_report_event_types_menu("byweek", blob_str(&urlParams));
    blob_reset(&urlParams);
  }else{
    stats_report_event_types_menu("byweek", NULL);
  }
  blob_append(&sql,
              "SELECT DISTINCT substr(date(mtime),1,4) AS y "
              "FROM v_reports WHERE 1 ", -1);
  if(zUserName&&*zUserName){
    blob_append_sql(&sql,"AND user=%Q ", zUserName);
  }
  blob_append(&sql,"GROUP BY y ORDER BY y", -1);
  db_prepare(&qYears, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  cgi_printf("Select year: ");
  while( SQLITE_ROW == db_step(&qYears) ){
    const char *zT = db_column_text(&qYears, 0);
    if( i++ ){
      cgi_printf(" ");
    }
    cgi_printf("<a href='?view=byweek&y=%s&type=%c", zT,
               (char)statsReportType);
    if(zUserName && *zUserName){
      cgi_printf("&user=%t",zUserName);
    }
    cgi_printf("'>%s</a>",zT);
  }
  db_finalize(&qYears);
  cgi_printf("<br/>");
  if(!zYear || !*zYear){
    zDefaultYear = db_text("????", "SELECT strftime('%%Y')");
    zYear = zDefaultYear;
    nYear = 4;
  }
  if(4 == nYear){
    Stmt stWeek = empty_Stmt;
    int rowCount = 0;
    int total = 0;
    Blob header = empty_blob;
    blob_appendf(&header, "Timeline events (%s) for the calendar weeks "
                 "of %h", stats_report_label_for_type(),
                 zYear);
    blob_append_sql(&sql,
                 "SELECT DISTINCT strftime('%%%%W',mtime) AS wk, "
                 "count(*) AS n "
                 "FROM v_reports "
                 "WHERE %Q=substr(date(mtime),1,4) "
                 "AND mtime < current_timestamp ",
                 zYear);
    if(zUserName&&*zUserName){
      blob_append_sql(&sql, " AND user=%Q ", zUserName);
      blob_appendf(&header," for user %h", zUserName);
    }
    blob_append_sql(&sql, "GROUP BY wk ORDER BY wk DESC");
    cgi_printf("<h1>%h</h1>", blob_str(&header));
    blob_reset(&header);
    cgi_printf("<table class='statistics-report-table-events' "
               "border='0' cellpadding='2' width='100%%' "
               "cellspacing='0' id='statsTable'>");
    cgi_printf("<thead><tr>"
               "<th>Week</th>"
               "<th>Events</th>"
               "<th width='90%%'><!-- relative commits graph --></th>"
               "</tr></thead>"
               "<tbody>");
    db_prepare(&stWeek, "%s", blob_sql_text(&sql));
    blob_reset(&sql);
    while( SQLITE_ROW == db_step(&stWeek) ){
      const int nCount = db_column_int(&stWeek, 1);
      if(nCount>nMaxEvents){
        nMaxEvents = nCount;
      }
      ++iterations;
    }
    db_reset(&stWeek);
    while( SQLITE_ROW == db_step(&stWeek) ){
      const char *zWeek = db_column_text(&stWeek,0);
      const int nCount = db_column_int(&stWeek,1);
      int nSize = nCount
        ? (int)(100 * nCount / nMaxEvents)
        : 0;
      if(!nSize) nSize = 1;
      total += nCount;
      cgi_printf("<tr class='row%d'>", ++rowCount % 2 );
      cgi_printf("<td><a href='%s/timeline?yw=%t-%s&n=%d&y=%s",
                 g.zTop, zYear, zWeek, nCount,
                 statsReportTimelineYFlag);
      if(zUserName && *zUserName){
        cgi_printf("&u=%t",zUserName);
      }
      cgi_printf("'>%s</a></td>",zWeek);

      cgi_printf("<td>%d</td>",nCount);
      cgi_printf("<td>");
      if(nCount){
        cgi_printf("<div class='statistics-report-graph-line'"
                   "style='width:%d%%;'>&nbsp;</div>",
                   nSize);
      }
      cgi_printf("</td></tr>");
    }
    db_finalize(&stWeek);
    free(zDefaultYear);
    cgi_printf("</tbody></table>");
    if(total){
      int nAvg = iterations ? (total/iterations) : 0;
      cgi_printf("<br><div>Total events: %d<br>"
                 "Average per active week: %d</div>",
                 total, nAvg);
    }
    output_table_sorting_javascript("statsTable","tnx");
  }
}

/*
** WEBPAGE: reports
**
** Shows activity reports for the repository.
**
** Query Parameters:
**
**   view=REPORT_NAME  Valid values: bymonth, byyear, byuser
**   user=NAME         Restricts statistics to the given user
**   type=TYPE         Restricts the report to a specific event type:
**                     ci (checkin), w (wiki), t (ticket), g (tag)
**                     Defaulting to all event types.
**
** The view-specific query parameters include:
**
** view=byweek:
**
**   y=YYYY            The year to report (default is the server's
**                     current year).
*/
void stats_report_page(){
  HQuery url;                        /* URL for various branch links */
  const char *zView = P("view");    /* Which view/report to show. */
  const char *zUserName = P("user");

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }
  if(!zUserName) zUserName = P("u");
  url_initialize(&url, "reports");
  if(zUserName && *zUserName){
    url_add_parameter(&url,"user", zUserName);
    timeline_submenu(&url, "(Remove User Flag)", "view", zView, "user");
  }
  timeline_submenu(&url, "By Year", "view", "byyear", 0);
  timeline_submenu(&url, "By Month", "view", "bymonth", 0);
  timeline_submenu(&url, "By Week", "view", "byweek", 0);
  timeline_submenu(&url, "By Weekday", "view", "byweekday", 0);
  timeline_submenu(&url, "By User", "view", "byuser", "user");
  url_reset(&url);
  style_header("Activity Reports");
  if(0==fossil_strcmp(zView,"byyear")){
    stats_report_by_month_year(0, 0, zUserName);
  }else if(0==fossil_strcmp(zView,"bymonth")){
    stats_report_by_month_year(1, 0, zUserName);
  }else if(0==fossil_strcmp(zView,"byweek")){
    stats_report_year_weeks(zUserName);
  }else if(0==fossil_strcmp(zView,"byuser")){
    stats_report_by_user();
  }else if(0==fossil_strcmp(zView,"byweekday")){
    stats_report_day_of_week();
  }else{
    @ <h1>Select a report to show:</h1>
    @ <ul>
    @ <li><a href='?view=byyear'>Events by year</a></li>
    @ <li><a href='?view=bymonth'>Events by month</a></li>
    @ <li><a href='?view=byweek'>Events by calendar week</a></li>
    @ <li><a href='?view=byweekday'>Events by day of the week</a></li>
    @ <li><a href='?view=byuser'>Events by user</a></li>
    @ </ul>
  }

  style_footer();
}
