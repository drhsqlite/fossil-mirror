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
** The value of one second in julianday notation
*/
#define ONE_SECOND (1.0/86400.0)

/*
** timeline mode options
*/
#define TIMELINE_MODE_NONE      0
#define TIMELINE_MODE_BEFORE    1
#define TIMELINE_MODE_AFTER     2
#define TIMELINE_MODE_CHILDREN  3
#define TIMELINE_MODE_PARENTS   4

/*
** Add an appropriate tag to the output if "rid" is unpublished (private)
*/
#define UNPUB_TAG "<em>(unpublished)</em>"
void tag_private_status(int rid){
  if( content_is_private(rid) ){
    cgi_printf("%s", UNPUB_TAG);
  }
}

/*
** Generate a hyperlink to a version.
*/
void hyperlink_to_uuid(const char *zUuid){
  if( g.perm.Hyperlink ){
    @ %z(xhref("class='timelineHistLink'","%R/info/%!S",zUuid))[%S(zUuid)]</a>
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
#define TIMELINE_SHOWRID  0x0400  /* Show RID values in addition to UUIDs */
#define TIMELINE_BISECT   0x0800  /* Show supplimental bisect information */
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
    if( skin_detail_boolean("white-foreground") ){
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
** COMMAND: test-hash-color
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
** WEBPAGE: hash-color-test
**
** Print out the color names associated with each tag.  Used for
** testing the hash_color() function.
*/
void test_hash_color_page(void){
  const char *zBr;
  char zNm[10];
  int i, cnt;
  login_check_credentials();

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
    @ <hr />
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
** Return a new timelineTable id.
*/
int timeline_tableid(void){
  static int id = 0;
  return id++;
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
  int selectedRid,       /* Highlight the line with this RID value */
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
  int dateFormat = 0;         /* 0: HH:MM (default) */
  int bCommentGitStyle = 0;   /* Only show comments through first blank line */
  const char *zDateFmt;
  int iTableId = timeline_tableid();

  if( fossil_strcmp(g.zIpAddr, "127.0.0.1")==0 && db_open_local(0) ){
    vid = db_lget_int("checkout", 0);
  }
  zPrevDate[0] = 0;
  mxWikiLen = db_get_int("timeline-max-comment", 0);
  dateFormat = db_get_int("timeline-date-format", 0);
  bCommentGitStyle = db_get_int("timeline-truncate-at-blank", 0);
  zDateFmt = P("datefmt");
  if( zDateFmt ) dateFormat = atoi(zDateFmt);
  if( tmFlags & TIMELINE_GRAPH ){
    pGraph = graph_init();
  }
  db_static_prepare(&qbranch,
    "SELECT value FROM tagxref WHERE tagid=%d AND tagtype>0 AND rid=:rid",
    TAG_BRANCH
  );

  @ <table id="timelineTable%d(iTableId)" class="timelineTable">
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
    char *zDateLink;          /* URL for the link on the timestamp */
    char zTime[20];

    if( zDate==0 ){
      zDate = "YYYY-MM-DD HH:MM:SS";  /* Something wrong with the repo */
    }
    modPending = moderation_pending(rid);
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
      if( pendingEndTr>1 ){
        @ <tr class="timelineSpacer"></tr>
      }
      pendingEndTr = 0;
    }
    if( fossil_strcmp(zType,"div")==0 ){
      if( !prevWasDivider ){
        @ <tr><td colspan="3"><hr class="timelineMarker" /></td></tr>
      }
      prevWasDivider = 1;
      continue;
    }
    prevWasDivider = 0;
    /* Date format codes:
    **   (0)  HH:MM
    **   (1)  HH:MM:SS
    **   (2)  YYYY-MM-DD HH:MM
    **   (3)  YYMMDD HH:MM
    **   (4)  (off)
    */
    if( dateFormat<2 ){
      if( fossil_strnicmp(zDate, zPrevDate, 10) ){
        sqlite3_snprintf(sizeof(zPrevDate), zPrevDate, "%.10s", zDate);
        @ <tr><td>
        @   <div class="divider timelineDate">%s(zPrevDate)</div>
        @ </td><td></td><td></td></tr>
      }
      memcpy(zTime, &zDate[11], 5+dateFormat*3);
      zTime[5+dateFormat*3] = 0;
    }else if( 2==dateFormat ){
      /* YYYY-MM-DD HH:MM */
      sqlite3_snprintf(sizeof(zTime), zTime, "%.16s", zDate);
    }else if( 3==dateFormat ){
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
      zTime[0] = 0;
    }
    pendingEndTr = 1;
    if( rid==selectedRid ){
      @ <tr class="timelineSpacer"></tr>
      @ <tr class="timelineSelected">
      pendingEndTr = 2;
    }else if( rid==vid ){
      @ <tr class="timelineCurrent">
    }else {
      @ <tr>
    }
    zDateLink = href("%R/timeline?c=%!S&unhide", zUuid);
    @ <td class="timelineTime">%z(zDateLink)%s(zTime)</a></td>
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
      int aParent[GR_MAX_RAIL];
      int gidx;
      static Stmt qparent;
      db_static_prepare(&qparent,
        "SELECT pid FROM plink"
        " WHERE cid=:rid AND pid NOT IN phantom"
        " ORDER BY isprim DESC /*sort*/"
      );
      db_bind_int(&qparent, ":rid", rid);
      while( db_step(&qparent)==SQLITE_ROW && nParent<count(aParent) ){
        aParent[nParent++] = db_column_int(&qparent, 0);
      }
      db_reset(&qparent);
      gidx = graph_add_row(pGraph, rid, nParent, aParent, zBr, zBgClr,
                           zUuid, isLeaf);
      db_reset(&qbranch);
      @ <div id="m%d(gidx)" class="tl-nodemark"></div>
    }
    @</td>
    if( zBgClr && zBgClr[0] && rid!=selectedRid ){
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
      if( tmFlags & TIMELINE_BISECT ){
        static Stmt bisectQuery;
        db_prepare(&bisectQuery, "SELECT seq, stat FROM bilog WHERE rid=:rid");
        db_bind_int(&bisectQuery, ":rid", rid);
        if( db_step(&bisectQuery)==SQLITE_ROW ){
          @ <b>%s(db_column_text(&bisectQuery,1))</b>
          @ (%d(db_column_int(&bisectQuery,0)))
        }
        db_reset(&bisectQuery);
      }
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
    if( tmFlags & TIMELINE_SHOWRID ){
      int srcId = delta_source_rid(rid);
      if( srcId ){
        @ (%d(rid)&larr;%d(srcId))
      }else{
        @ (%d(rid))
      }
    }
    db_column_blob(pQuery, commentColumn, &comment);
    if( zType[0]!='c' ){
      /* Comments for anything other than a check-in are generated by
      ** "fossil rebuild" and expect to be rendered as text/x-fossil-wiki */
      wiki_convert(&comment, 0, WIKI_INLINE);
    }else if( bCommentGitStyle ){
      /* Truncate comment at first blank line */
      int ii, jj;
      int n = blob_size(&comment);
      char *z = blob_str(&comment);
      for(ii=0; ii<n; ii++){
        if( z[ii]=='\n' ){
          for(jj=ii+1; jj<n && z[jj]!='\n' && fossil_isspace(z[jj]); jj++){}
          if( z[jj]=='\n' ) break;
        }
      }
      z[ii] = 0;
      @ <span class="timelineComment">%W(z)</span>
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
      char *zLink = mprintf("%R/timeline?u=%h&c=%t&nd&n=200", zDispUser, zDate);
      @ (user: %z(href("%z",zLink))%h(zDispUser)</a>%s(zTagList?",":"\051")
    }else{
      @ (user: %h(zDispUser)%s(zTagList?",":"\051")
    }

    /* Generate a "detail" link for tags. */
    if( (zType[0]=='g' || zType[0]=='w' || zType[0]=='t') && g.perm.Hyperlink ){
      @ [%z(href("%R/info/%!S",zUuid))details</a>]
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
                  href("%R/timeline?r=%#t&nd&c=%t&n=200",i,z,zDate), i,z, &z[i]
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
    tag_private_status(rid);

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
          "SELECT pid,"
          "       fid,"
          "       (SELECT name FROM filename WHERE fnid=mlink.fnid) AS name,"
          "       (SELECT uuid FROM blob WHERE rid=fid),"
          "       (SELECT uuid FROM blob WHERE rid=pid),"
          "       (SELECT name FROM filename WHERE fnid=mlink.pfnid) AS oldnm"
          "  FROM mlink"
          " WHERE mid=:mid AND (pid!=fid OR pfnid>0)"
          "   AND (fid>0 OR"
               "   fnid NOT IN (SELECT pfnid FROM mlink WHERE mid=:mid))"
          "   AND NOT mlink.isaux"
          " ORDER BY 3 /*sort*/"
        );
        fchngQueryInit = 1;
      }
      db_bind_int(&fchngQuery, ":mid", rid);
      while( db_step(&fchngQuery)==SQLITE_ROW ){
        const char *zFilename = db_column_text(&fchngQuery, 2);
        int isNew = db_column_int(&fchngQuery, 0)<=0;
        int isMergeNew = db_column_int(&fchngQuery, 0)<0;
        int fid = db_column_int(&fchngQuery, 1);
        int isDel = fid==0;
        const char *zOldName = db_column_text(&fchngQuery, 5);
        const char *zOld = db_column_text(&fchngQuery, 4);
        const char *zNew = db_column_text(&fchngQuery, 3);
        const char *zUnpub = "";
        char *zA;
        char zId[40];
        if( !inUl ){
          @ <ul class="filelist">
          inUl = 1;
        }
        if( tmFlags & TIMELINE_SHOWRID ){
          int srcId = delta_source_rid(fid);
          if( srcId ){
            sqlite3_snprintf(sizeof(zId), zId, " (%d&larr;%d) ", fid, srcId);
          }else{
            sqlite3_snprintf(sizeof(zId), zId, " (%d) ", fid);
          }
        }else{
          zId[0] = 0;
        }
        if( (tmFlags & TIMELINE_FRENAMES)!=0 ){
          if( !isNew && !isDel && zOldName!=0 ){
            @ <li> %h(zOldName) &rarr; %h(zFilename)%s(zId)
          }
          continue;
        }
        zA = href("%R/artifact/%!S",fid?zNew:zOld);
        if( content_is_private(fid) ){
          zUnpub = UNPUB_TAG;
        }
        if( isNew ){
          @ <li> %s(zA)%h(zFilename)</a>%s(zId) %s(zUnpub)
          if( isMergeNew ){
            @ (added by merge)
          }else{
            @ (new file)
          }
          @ &nbsp; %z(href("%R/artifact/%!S",zNew))[view]</a></li>
        }else if( isDel ){
          @ <li> %s(zA)%h(zFilename)</a> (deleted)</li>
        }else if( fossil_strcmp(zOld,zNew)==0 && zOldName!=0 ){
          @ <li> %h(zOldName) &rarr; %s(zA)%h(zFilename)</a>%s(zId)
          @ %s(zUnpub) %z(href("%R/artifact/%!S",zNew))[view]</a></li>
        }else{
          if( zOldName!=0 ){
            @ <li>%h(zOldName) &rarr; %s(zA)%h(zFilename)%s(zId)</a> %s(zUnpub)
          }else{
            @ <li>%s(zA)%h(zFilename)</a>%s(zId) &nbsp; %s(zUnpub)
          }
          @ %z(href("%R/fdiff?sbs=1&v1=%!S&v2=%!S",zOld,zNew))[diff]</a></li>
        }
        fossil_free(zA);
      }
      db_reset(&fchngQuery);
      if( inUl ){
        @ </ul>
      }
    }
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
      @ <tr class="timelineBottom"><td></td><td></td><td></td></tr>
    }
  }
  @ </table>
  if( fchngQueryInit ) db_finalize(&fchngQuery);
  timeline_output_graph_javascript(pGraph, (tmFlags & TIMELINE_DISJOINT)!=0, iTableId, 0);
}

/*
** Change the RGB background color given in the argument in a foreground
** color with the same hue.
*/
static const char *bg_to_fg(const char *zIn){
  int i;
  unsigned int x[3];
  unsigned int mx = 0;
  static int whiteFg = -1;
  static char zRes[10];
  if( strlen(zIn)!=7 || zIn[0]!='#' ) return zIn;
  zIn++;
  for(i=0; i<3; i++){
    x[i] = hex_digit_value(zIn[0])*16 + hex_digit_value(zIn[1]);
    zIn += 2;
    if( x[i]>mx ) mx = x[i];
  }
  if( whiteFg<0 ) whiteFg = skin_detail_boolean("white-foreground");
  if( whiteFg ){
    /* Make the color lighter */
    static const unsigned int t = 215;
    if( mx<t ) for(i=0; i<3; i++) x[i] += t - mx;
  }else{
    /* Make the color darker */
    static const unsigned int t = 128;
    if( mx>t ) for(i=0; i<3; i++) x[i] -= mx - t;
  }
  sqlite3_snprintf(sizeof(zRes),zRes,"#%02x%02x%02x",x[0],x[1],x[2]);
  return zRes;
}

/*
** Generate all of the necessary javascript to generate a timeline
** graph.
*/
void timeline_output_graph_javascript(
  GraphContext *pGraph,     /* The graph to be displayed */
  int omitDescenders,       /* True to omit descenders */
  int iTableId,             /* Identifier for the timelineTable */
  int fileDiff              /* True for file diff.  False for check-in diff */
){
  if( pGraph && pGraph->nErr==0 && pGraph->nRow>0 ){
    GraphRow *pRow;
    int i;
    char cSep;
    int iRailPitch;      /* Pixels between consecutive rails */
    int showArrowheads;  /* True to draw arrowheads.  False to omit. */
    int circleNodes;     /* True for circle nodes.  False for square nodes */
    int colorGraph;      /* Use colors for graph lines */
    int iTopRow;         /* Index of the top row of the graph */

    iRailPitch = atoi(PD("railpitch","0"));
    showArrowheads = skin_detail_boolean("timeline-arrowheads");
    circleNodes = skin_detail_boolean("timeline-circle-nodes");
    colorGraph = skin_detail_boolean("timeline-color-graph-lines");

    @ <script>(function(){
    @ "use strict";
    @ var css = "";
    if( circleNodes ){
      @ css += ".tl-node, .tl-node:after { border-radius: 50%%; }";
    }
    if( !showArrowheads ){
      @ css += ".tl-arrow.u { display: none; }";
    }
    @ if( css!=="" ){
    @   var style = document.createElement("style");
    @   style.textContent = css;
    @   document.querySelector("head").appendChild(style);
    @ }
    /* the rowinfo[] array contains all the information needed to generate
    ** the graph.  Each entry contains information for a single row:
    **
    **   id:  The id of the <div> element for the row. This is an integer.
    **        to get an actual id, prepend "m" to the integer.  The top node
    **        is iTopRow and numbers increase moving down the timeline.
    **   bg:  The background color for this row
    **    r:  The "rail" that the node for this row sits on.  The left-most
    **        rail is 0 and the number increases to the right.
    **    d:  True if there is a "descender" - an arrow coming from the bottom
    **        of the page straight up to this node.
    **   mo:  "merge-out".  If non-negative, this is the rail position
    **        for the upward portion of a merge arrow.  The merge arrow goes up
    **        to the row identified by mu:.  If this value is negative then
    **        node has no merge children and no merge-out line is drawn.
    **   mu:  The id of the row which is the top of the merge-out arrow.
    **    u:  Draw a thick child-line out of the top of this node and up to
    **        the node with an id equal to this value.  0 if it is straight to
    **        the top of the page, -1 if there is no thick-line riser.
    **    f:  0x01: a leaf node.
    **   au:  An array of integers that define thick-line risers for branches.
    **        The integers are in pairs.  For each pair, the first integer is
    **        is the rail on which the riser should run and the second integer
    **        is the id of the node upto which the riser should run.
    **   mi:  "merge-in".  An array of integer rail positions from which
    **        merge arrows should be drawn into this node.  If the value is
    **        negative, then the rail position is the absolute value of mi[]
    **        and a thin merge-arrow descender is drawn to the bottom of
    **        the screen.
    **    h:  The artifact hash of the object being graphed
    */
    iTopRow = pGraph->pFirst ? pGraph->pFirst->idx : 0;
    cgi_printf("var rowinfo = [\n");
    for(pRow=pGraph->pFirst; pRow; pRow=pRow->pNext){
      cgi_printf("{id:%d,bg:\"%s\",r:%d,d:%d,mo:%d,mu:%d,u:%d,f:%d,au:",
        pRow->idx,                      /* id */
        pRow->zBgClr,                   /* bg */
        pRow->iRail,                    /* r */
        pRow->bDescender,               /* d */
        pRow->mergeOut,                 /* mo */
        pRow->mergeUpto,                /* mu */
        pRow->aiRiser[pRow->iRail],     /* u */
        pRow->isLeaf ? 1 : 0            /* f */
      );
      /* au */
      cSep = '[';
      for(i=0; i<GR_MAX_RAIL; i++){
        if( i==pRow->iRail ) continue;
        if( pRow->aiRiser[i]>0 ){
          cgi_printf("%c%d,%d", cSep, i, pRow->aiRiser[i]);
          cSep = ',';
        }
      }
      if( cSep=='[' ) cgi_printf("[");
      cgi_printf("],");
      if( colorGraph && pRow->zBgClr[0]=='#' ){
        cgi_printf("fg:\"%s\",", bg_to_fg(pRow->zBgClr));
      }
      /* mi */
      cgi_printf("mi:");
      cSep = '[';
      for(i=0; i<GR_MAX_RAIL; i++){
        if( pRow->mergeIn[i] ){
          int mi = i;
          if( (pRow->mergeDown >> i) & 1 ) mi = -mi;
          cgi_printf("%c%d", cSep, mi);
          cSep = ',';
        }
      }
      if( cSep=='[' ) cgi_printf("[");
      cgi_printf("],h:\"%!S\"}%s", pRow->zUuid, pRow->pNext ? ",\n" : "];\n");
    }
    cgi_printf("var nrail = %d\n", pGraph->mxRail+1);
    graph_free(pGraph);
    @ var canvasDiv;
    @ var railPitch;
    @ var mergeOffset;
    @ var node, arrow, arrowSmall, line, mArrow, mLine, wArrow, wLine;
    @ function initGraph(){
    @   var parent = gebi("timelineTable%d(iTableId)").rows[0].cells[1];
    @   parent.style.verticalAlign = "top";
    @   canvasDiv = document.createElement("div");
    @   canvasDiv.className = "tl-canvas";
    @   canvasDiv.style.position = "absolute";
    @   parent.appendChild(canvasDiv);
    @
    @   var elems = {};
    @   var elemClasses = [
    @     "rail", "mergeoffset", "node", "arrow u", "arrow u sm", "line",
    @     "arrow merge r", "line merge", "arrow warp", "line warp"
    @   ];
    @   for( var i=0; i<elemClasses.length; i++ ){
    @     var cls = elemClasses[i];
    @     var elem = document.createElement("div");
    @     elem.className = "tl-" + cls;
    @     if( cls.indexOf("line")==0 ) elem.className += " v";
    @     canvasDiv.appendChild(elem);
    @     var k = cls.replace(/\s/g, "_");
    @     var r = elem.getBoundingClientRect();
    @     var w = Math.round(r.right - r.left);
    @     var h = Math.round(r.bottom - r.top);
    @     elems[k] = {w: w, h: h, cls: cls};
    @   }
    @   node = elems.node;
    @   arrow = elems.arrow_u;
    @   arrowSmall = elems.arrow_u_sm;
    @   line = elems.line;
    @   mArrow = elems.arrow_merge_r;
    @   mLine = elems.line_merge;
    @   wArrow = elems.arrow_warp;
    @   wLine = elems.line_warp;
    @
    @   var minRailPitch = Math.ceil((node.w+line.w)/2 + mArrow.w + 1);
    if( iRailPitch ){
      @   railPitch = %d(iRailPitch);
    }else{
      @   railPitch = elems.rail.w;
      @   railPitch -= Math.floor((nrail-1)*(railPitch-minRailPitch)/21);
    }
    @   railPitch = Math.max(railPitch, minRailPitch);
    @
    if( PB("nomo") ){
      @   mergeOffset = 0;
    }else{
      @   mergeOffset = railPitch-minRailPitch-mLine.w;
      @   mergeOffset = Math.min(mergeOffset, elems.mergeoffset.w);
      @   mergeOffset = mergeOffset>0 ? mergeOffset + line.w/2 : 0;
    }
    @
    @   var canvasWidth = (nrail-1)*railPitch + node.w;
    @   canvasDiv.style.width = canvasWidth + "px";
    @   canvasDiv.style.position = "relative";
    @ }
    @ function drawBox(cls,color,x0,y0,x1,y1){
    @   var n = document.createElement("div");
    @   x0 = Math.floor(x0);
    @   y0 = Math.floor(y0);
    @   x1 = x1 || x1===0 ? Math.floor(x1) : x0;
    @   y1 = y1 || y1===0 ? Math.floor(y1) : y0;
    @   if( x0>x1 ){ var t=x0; x0=x1; x1=t; }
    @   if( y0>y1 ){ var t=y0; y0=y1; y1=t; }
    @   var w = x1-x0;
    @   var h = y1-y0;
    @   n.style.position = "absolute";
    @   n.style.left = x0+"px";
    @   n.style.top = y0+"px";
    @   if( w ) n.style.width = w+"px";
    @   if( h ) n.style.height = h+"px";
    @   if( color ) n.style.backgroundColor = color;
    @   n.className = "tl-"+cls;
    @   canvasDiv.appendChild(n);
    @   return n;
    @ }
    @ function absoluteY(obj){
    @   var top = 0;
    @   if( obj.offsetParent ){
    @     do{
    @       top += obj.offsetTop;
    @     }while( obj = obj.offsetParent );
    @   }
    @   return top;
    @ }
    @ function miLineY(p){
    @   return p.y + node.h - mLine.w - 1;
    @ }
    @ function drawLine(elem,color,x0,y0,x1,y1){
    @   var cls = elem.cls + " ";
    @   if( x1===null ){
    @     x1 = x0+elem.w;
    @     cls += "v";
    @   }else{
    @     y1 = y0+elem.w;
    @     cls += "h";
    @   }
    @   drawBox(cls,color,x0,y0,x1,y1);
    @ }
    @ function drawUpArrow(from,to,color){
    @   var y = to.y + node.h;
    @   var arrowSpace = from.y - y + (!from.id || from.r!=to.r ? node.h/2 : 0);
    @   var arw = arrowSpace < arrow.h*1.5 ? arrowSmall : arrow;
    @   var x = to.x + (node.w-line.w)/2;
    @   var y0 = from.y + node.h/2;
    @   var y1 = Math.ceil(to.y + node.h + arw.h/2);
    @   drawLine(line,color,x,y0,null,y1);
    @   x = to.x + (node.w-arw.w)/2;
    @   var n = drawBox(arw.cls,null,x,y);
    @   n.style.borderBottomColor = color;
    @ }
    @ function drawMergeLine(x0,y0,x1,y1){
    @   drawLine(mLine,null,x0,y0,x1,y1);
    @ }
    @ function drawMergeArrow(p,rail){
    @   var x0 = rail*railPitch + node.w/2;
    @   if( rail in mergeLines ){
    @     x0 += mergeLines[rail];
    @     if( p.r<rail ) x0 += mLine.w;
    @   }else{
    @     x0 += (p.r<rail ? -1 : 1)*line.w/2;
    @   }
    @   var x1 = mArrow.w ? mArrow.w/2 : -node.w/2;
    @   x1 = p.x + (p.r<rail ? node.w + Math.ceil(x1) : -x1);
    @   var y = miLineY(p);
    @   drawMergeLine(x0,y,x1,null);
    @   var x = p.x + (p.r<rail ? node.w : -mArrow.w);
    @   var cls = "arrow merge " + (p.r<rail ? "l" : "r");
    @   drawBox(cls,null,x,y+(mLine.w-mArrow.h)/2);
    @ }
    @ function drawNode(p, btm){
    @   if( p.u>0 ) drawUpArrow(p,rowinfo[p.u-%d(iTopRow)],p.fg);
    @   var cls = node.cls;
    @   if( p.mi.length ) cls += " merge";
    @   if( p.f&1 ) cls += " leaf";
    @   var n = drawBox(cls,p.bg,p.x,p.y);
    @   n.id = "tln"+p.id;
    @   n.onclick = clickOnNode;
    @   n.style.zIndex = 10;
    if( !omitDescenders ){
      @   if( p.u==0 ) drawUpArrow(p,{x: p.x, y: -node.h},p.fg);
      @   if( p.d ) drawUpArrow({x: p.x, y: btm-node.h/2},p,p.fg);
    }
    @   if( p.mo>=0 ){
    @     var x0 = p.x + node.w/2;
    @     var x1 = p.mo*railPitch + node.w/2;
    @     var u = rowinfo[p.mu-%d(iTopRow)];
    @     var y1 = miLineY(u);
    @     if( p.u<0 || p.mo!=p.r ){
    @       x1 += mergeLines[p.mo] = -mLine.w/2;
    @       var y0 = p.y+2;
    @       if( p.r!=p.mo ) drawMergeLine(x0,y0,x1+(x0<x1 ? mLine.w : 0),null);
    @       drawMergeLine(x1,y0+mLine.w,null,y1);
    @     }else if( mergeOffset ){
    @       mergeLines[p.mo] = u.r<p.r ? -mergeOffset-mLine.w : mergeOffset;
    @       x1 += mergeLines[p.mo];
    @       drawMergeLine(x1,p.y+node.h/2,null,y1);
    @     }else{
    @       delete mergeLines[p.mo];
    @     }
    @   }
    @   for( var i=0; i<p.au.length; i+=2 ){
    @     var rail = p.au[i];
    @     var x0 = p.x + node.w/2;
    @     var x1 = rail*railPitch + (node.w-line.w)/2;
    @     if( x0<x1 ){
    @       x0 = Math.ceil(x0);
    @       x1 += line.w;
    @     }
    @     var y0 = p.y + (node.h-line.w)/2;
    @     var u = rowinfo[p.au[i+1]-%d(iTopRow)];
    @     if( u.id<p.id ){
    @       drawLine(line,u.fg,x0,y0,x1,null);
    @       drawUpArrow(p,u,u.fg);
    @     }else{
    @       var y1 = u.y + (node.h-line.w)/2;
    @       drawLine(wLine,u.fg,x0,y0,x1,null);
    @       drawLine(wLine,u.fg,x1-line.w,y0,null,y1+line.w);
    @       drawLine(wLine,u.fg,x1,y1,u.x-wArrow.w/2,null);
    @       var x = u.x-wArrow.w;
    @       var y = u.y+(node.h-wArrow.h)/2;
    @       var n = drawBox(wArrow.cls,null,x,y);
    @       if( u.fg ) n.style.borderLeftColor = u.fg;
    @     }
    @   }
    @   for( var i=0; i<p.mi.length; i++ ){
    @     var rail = p.mi[i];
    @     if( rail<0 ){
    @       rail = -rail;
    @       mergeLines[rail] = -mLine.w/2;
    @       var x = rail*railPitch + (node.w-mLine.w)/2;
    @       drawMergeLine(x,miLineY(p),null,btm);
    @     }
    @     drawMergeArrow(p,rail);
    @   }
    @ }
    @ var mergeLines;
    @ function renderGraph(){
    @   mergeLines = {};
    @   canvasDiv.innerHTML = "";
    @   var canvasY = absoluteY(canvasDiv);
    @   for( var i=0; i<rowinfo.length; i++ ){
    @     rowinfo[i].y = absoluteY(gebi("m"+rowinfo[i].id)) - canvasY;
    @     rowinfo[i].x = rowinfo[i].r*railPitch;
    @   }
    @   var tlBtm = document.querySelector(".timelineBottom");
    @   if( tlBtm.offsetHeight<node.h ){
    @     tlBtm.style.height = node.h + "px";
    @   }
    @   var btm = absoluteY(tlBtm) - canvasY + tlBtm.offsetHeight;
    @   for( var i=rowinfo.length-1; i>=0; i-- ){
    @     drawNode(rowinfo[i], btm);
    @   }
    @ }
    @ var selRow;
    @ function clickOnNode(){
    @   var p = rowinfo[parseInt(this.id.match(/\d+$/)[0], 10)-%d(iTopRow)];
    @   if( !selRow ){
    @     selRow = p;
    @     this.className += " sel";
    @     canvasDiv.className += " sel";
    @   }else if( selRow==p ){
    @     selRow = null;
    @     this.className = this.className.replace(" sel", "");
    @     canvasDiv.className = canvasDiv.className.replace(" sel", "");
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
    @ var lastRow = gebi("m"+rowinfo[rowinfo.length-1].id);
    @ var lastY = 0;
    @ function checkHeight(){
    @   var h = absoluteY(lastRow);
    @   if( h!=lastY ){
    @     renderGraph();
    @     lastY = h;
    @   }
    @   setTimeout(checkHeight, 1000);
    @ }
    @ initGraph();
    @ checkHeight();
    @ }())</script>
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
  static const char zBase[] =
    @ SELECT
    @   blob.rid AS blobRid,
    @   uuid AS uuid,
    @   datetime(event.mtime,toLocal()) AS timestamp,
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
  style_submenu_element(zMenuName, "%s",
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
    mtime = db_double(0.0, "SELECT julianday(%Q,fromLocal())", z);
    if( mtime>0.0 ) return mtime;
  }
  rid = symbolic_name_to_rid(z, "*");
  if( rid ){
    mtime = db_double(0.0, "SELECT mtime FROM event WHERE objid=%d", rid);
  }else{
    mtime = db_double(-1.0,
        "SELECT max(event.mtime) FROM event, tag, tagxref"
        " WHERE tag.tagname GLOB 'event-%q*'"
        "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype"
        "   AND event.objid=tagxref.rid",
        z
    );
  }
  return mtime;
}

/*
** zDate is a localtime date.  Insert records into the
** "timeline" table to cause <hr> to be inserted on zDate.
*/
static int timeline_add_divider(double rDate){
  int rid = db_int(-1,
    "SELECT rid FROM timeline ORDER BY abs(sortby-%.16g) LIMIT 1", rDate
  );
  if( rid>0 ) return rid;
  db_multi_exec(
    "INSERT INTO timeline(rid,sortby,etype) VALUES(-1,%.16g,'div')",
    rDate
  );
  return -1;
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
** Add the select/option box to the timeline submenu that is used to
** set the y= parameter that determines which elements to display
** on the timeline.
*/
static void timeline_y_submenu(int isDisabled){
  static int i = 0;
  static const char *az[12];
  if( i==0 ){
    az[0] = "all";
    az[1] = "Any Type";
    i = 2;
    if( g.perm.Read ){
      az[i++] = "ci";
      az[i++] = "Check-ins";
      az[i++] = "g";
      az[i++] = "Tags";
    }
    if( g.perm.RdWiki ){
      az[i++] = "e";
      az[i++] = "Tech Notes";
    }
    if( g.perm.RdTkt ){
      az[i++] = "t";
      az[i++] = "Tickets";
    }
    if( g.perm.RdWiki ){
      az[i++] = "w";
      az[i++] = "Wiki";
    }
    assert( i<=count(az) );
  }
  if( i>2 ){
    style_submenu_multichoice("y", i/2, az, isDisabled);
  }
}

/*
** If the zChng string is not NULL, then it should be a comma-separated
** list of glob patterns for filenames.  Add an term to the WHERE clause
** for the SQL statement under construction that excludes any check-in that
** does not modify one or more files matching the globs.
*/
static void addFileGlobExclusion(
  const char *zChng,        /* The filename GLOB list */
  Blob *pSql                /* The SELECT statement under construction */
){
  if( zChng==0 || zChng[0]==0 ) return;
  blob_append_sql(pSql," AND event.objid IN ("
      "SELECT mlink.mid FROM mlink, filename"
      " WHERE mlink.fnid=filename.fnid AND %s)",
      glob_expr("filename.name", zChng));
}
static void addFileGlobDescription(
  const char *zChng,        /* The filename GLOB list */
  Blob *pDescription        /* Result description */
){
  if( zChng==0 || zChng[0]==0 ) return;
  blob_appendf(pDescription, " that include changes to files matching %Q",
               zChng);
}

/*
** Tag match expression type code.
*/
typedef enum {
  MS_EXACT,   /* Matches a single tag by exact string comparison. */
  MS_GLOB,    /* Matches tags against a list of GLOB patterns. */
  MS_LIKE,    /* Matches tags against a list of LIKE patterns. */
  MS_REGEXP   /* Matches tags against a list of regular expressions. */
} MatchStyle;

/*
** Quote a tag string by surrounding it with double quotes and preceding
** internal double quotes and backslashes with backslashes.
*/
static const char *tagQuote(
   int len,         /* Maximum length of zTag, or negative for unlimited */
   const char *zTag /* Tag string */
){
  Blob blob = BLOB_INITIALIZER;
  int i, j;
  blob_zero(&blob);
  blob_append(&blob, "\"", 1);
  for( i=j=0; zTag[j] && (len<0 || j<len); ++j ){
    if( zTag[j]=='\"' || zTag[j]=='\\' ){
      if( j>i ){
        blob_append(&blob, zTag+i, j-i);
      }
      blob_append(&blob, "\\", 1);
      i = j;
    }
  }
  if( j>i ){
    blob_append(&blob, zTag+i, j-i);
  }
  blob_append(&blob, "\"", 1);
  return blob_str(&blob);
}

/*
** Construct the tag match SQL expression.
**
** This function is adapted from glob_expr() to support the MS_EXACT, MS_GLOB,
** MS_LIKE, and MS_REGEXP match styles.  For MS_EXACT, the returned expression
** checks for integer match against the tag ID which is looked up directly by
** this function.  For the other modes, the returned SQL expression performs
** string comparisons against the tag names, so it is necessary to join against
** the tag table to access the "tagname" column.
**
** Each pattern is adjusted to to start with "sym-" and be anchored at end.
**
** In MS_REGEXP mode, backslash can be used to protect delimiter characters.
** The backslashes are not removed from the regular expression.
**
** In addition to assembling and returning an SQL expression, this function
** makes an English-language description of the patterns being matched, suitable
** for display in the web interface.
**
** If any errors arise during processing, *zError is set to an error message.
** Otherwise it is set to NULL.
*/
static const char *tagMatchExpression(
  MatchStyle matchStyle,        /* Match style code */
  const char *zTag,             /* Tag name, match pattern, or pattern list */
  const char **zDesc,           /* Output expression description string */
  const char **zError           /* Output error string */
){
  Blob expr = BLOB_INITIALIZER; /* SQL expression string assembly buffer */
  Blob desc = BLOB_INITIALIZER; /* English description of match patterns */
  Blob err = BLOB_INITIALIZER;  /* Error text assembly buffer */
  const char *zStart;           /* Text at start of expression */
  const char *zDelimiter;       /* Text between expression terms */
  const char *zEnd;             /* Text at end of expression */
  const char *zPrefix;          /* Text before each match pattern */
  const char *zSuffix;          /* Text after each match pattern */
  const char *zIntro;           /* Text introducing pattern description */
  const char *zPattern = 0;     /* Previous quoted pattern */
  const char *zFail = 0;        /* Current failure message or NULL if okay */
  const char *zOr = " or ";     /* Text before final quoted pattern */
  char cDel;                    /* Input delimiter character */
  int i;                        /* Input match pattern length counter */

  /* Optimize exact matches by looking up the ID in advance to create a simple
   * numeric comparison.  Bypass the remainder of this function. */
  if( matchStyle==MS_EXACT ){
    *zDesc = tagQuote(-1, zTag);
    return mprintf("(tagid=%d)", db_int(-1,
        "SELECT tagid FROM tag WHERE tagname='sym-%q'", zTag));
  }

  /* Decide pattern prefix and suffix strings according to match style. */
  if( matchStyle==MS_GLOB ){
    zStart = "(";
    zDelimiter = " OR ";
    zEnd = ")";
    zPrefix = "tagname GLOB 'sym-";
    zSuffix = "'";
    zIntro = "glob pattern ";
  }else if( matchStyle==MS_LIKE ){
    zStart = "(";
    zDelimiter = " OR ";
    zEnd = ")";
    zPrefix = "tagname LIKE 'sym-";
    zSuffix = "'";
    zIntro = "SQL LIKE pattern ";
  }else/* if( matchStyle==MS_REGEXP )*/{
    zStart = "(tagname REGEXP '^sym-(";
    zDelimiter = "|";
    zEnd = ")$')";
    zPrefix = "";
    zSuffix = "";
    zIntro = "regular expression ";
  }

  /* Convert the list of matches into an SQL expression and text description. */
  blob_zero(&expr);
  blob_zero(&desc);
  blob_zero(&err);
  while( 1 ){
    /* Skip leading delimiters. */
    for( ; fossil_isspace(*zTag) || *zTag==','; ++zTag );

    /* Next non-delimiter character determines quoting. */
    if( !*zTag ){
      /* Terminate loop at end of string. */
      break;
    }else if( *zTag=='\'' || *zTag=='"' ){
      /* If word is quoted, prepare to stop at end quote. */
      cDel = *zTag;
      ++zTag;
    }else{
      /* If word is not quoted, prepare to stop at delimiter. */
      cDel = ',';
    }

    /* Find the next delimiter character or end of string. */
    for( i=0; zTag[i] && zTag[i]!=cDel; ++i ){
      /* If delimiter is comma, also recognize spaces as delimiters. */
      if( cDel==',' && fossil_isspace(zTag[i]) ){
        break;
      }

      /* In regexp mode, ignore delimiters following backslashes. */
      if( matchStyle==MS_REGEXP && zTag[i]=='\\' && zTag[i+1] ){
        ++i;
      }
    }

    /* Check for regular expression syntax errors. */
    if( matchStyle==MS_REGEXP ){
      ReCompiled *regexp;
      char *zTagDup = fossil_strndup(zTag, i);
      zFail = re_compile(&regexp, zTagDup, 0);
      re_free(regexp);
      fossil_free(zTagDup);
    }

    /* Process success and error results. */
    if( !zFail ){
      /* Incorporate the match word into the output expression.  %q is used to
       * protect against SQL injection attacks by replacing ' with ''. */
      blob_appendf(&expr, "%s%s%#q%s", blob_size(&expr) ? zDelimiter : zStart,
          zPrefix, i, zTag, zSuffix);

      /* Build up the description string. */
      if( !blob_size(&desc) ){
        /* First tag: start with intro followed by first quoted tag. */
        blob_append(&desc, zIntro, -1);
        blob_append(&desc, tagQuote(i, zTag), -1);
      }else{
        if( zPattern ){
          /* Third and subsequent tags: append comma then previous tag. */
          blob_append(&desc, ", ", 2);
          blob_append(&desc, zPattern, -1);
          zOr = ", or ";
        }

        /* Second and subsequent tags: store quoted tag for next iteration. */
        zPattern = tagQuote(i, zTag);
      }
    }else{
      /* On error, skip the match word and build up the error message buffer. */
      if( !blob_size(&err) ){
        blob_append(&err, "Error: ", 7);
      }else{
        blob_append(&err, ", ", 2);
      }
      blob_appendf(&err, "(%s%s: %s)", zIntro, tagQuote(i, zTag), zFail);
    }

    /* Advance past all consumed input characters. */
    zTag += i;
    if( cDel!=',' && *zTag==cDel ){
      ++zTag;
    }
  }

  /* Finalize and extract the pattern description. */
  if( zPattern ){
    blob_append(&desc, zOr, -1);
    blob_append(&desc, zPattern, -1);
  }
  *zDesc = blob_str(&desc);

  /* Finalize and extract the error text. */
  *zError = blob_size(&err) ? blob_str(&err) : 0;

  /* Finalize and extract the SQL expression. */
  if( blob_size(&expr) ){
    blob_append(&expr, zEnd, -1);
    return blob_str(&expr);
  }

  /* If execution reaches this point, the pattern was empty.  Return NULL. */
  return 0;
}

/*
** WEBPAGE: timeline
**
** Query parameters:
**
**    a=TIMEORTAG    After this event
**    b=TIMEORTAG    Before this event
**    c=TIMEORTAG    "Circa" this event
**    m=TIMEORTAG    Mark this event
**    n=COUNT        Suggested number of events in output
**    p=CHECKIN      Parents and ancestors of CHECKIN
**    d=CHECKIN      Descendants of CHECIN
**    dp=CHECKIN     The same as d=CHECKIN&p=CHECKIN
**    t=TAG          Show only check-ins with the given TAG
**    r=TAG          Show check-ins related to TAG, equivalent to t=TAG&rel
**    rel            Show related check-ins as well as those matching t=TAG
**    mionly         Limit rel to show ancestors but not descendants
**    ms=STYLE       Set tag match style to EXACT, GLOB, LIKE, REGEXP
**    u=USER         Only show items associated with USER
**    y=TYPE         'ci', 'w', 't', 'e', or (default) 'all'
**    ng             No Graph.
**    nd             Do not highlight the focus check-in
**    v              Show details of files changed
**    f=CHECKIN      Show family (immediate parents and children) of CHECKIN
**    from=CHECKIN   Path from...
**    to=CHECKIN       ... to this
**    shortest         ... show only the shortest path
**    uf=FILE_HASH   Show only check-ins that contain the given file version
**    chng=GLOBLIST  Show only check-ins that involve changes to a file whose
**                     name matches one of the comma-separate GLOBLIST
**    brbg           Background color from branch name
**    ubg            Background color from user
**    namechng       Show only check-ins that have filename changes
**    forks          Show only forks and their children
**    ym=YYYY-MM     Show only events for the given year/month
**    yw=YYYY-WW     Show only events for the given week of the given year
**    ymd=YYYY-MM-DD Show only events on the given day
**    datefmt=N      Override the date format
**    bisect         Show the check-ins that are in the current bisect
**    showid         Show RIDs
**    showsql        Show the SQL text
**
** p= and d= can appear individually or together.  If either p= or d=
** appear, then u=, y=, a=, and b= are ignored.
**
** If both a= and b= appear then both upper and lower bounds are honored.
*/
void page_timeline(void){
  Stmt q;                            /* Query used to generate the timeline */
  Blob sql;                          /* text of SQL used to generate timeline */
  Blob desc;                         /* Description of the timeline */
  int nEntry;                        /* Max number of entries on timeline */
  int p_rid = name_to_typed_rid(P("p"),"ci");  /* artifact p and its parents */
  int d_rid = name_to_typed_rid(P("d"),"ci");  /* artifact d and descendants */
  int f_rid = name_to_typed_rid(P("f"),"ci");  /* artifact f and close family */
  const char *zUser = P("u");        /* All entries by this user if not NULL */
  const char *zType = PD("y","all"); /* Type of events.  All if NULL */
  const char *zAfter = P("a");       /* Events after this time */
  const char *zBefore = P("b");      /* Events before this time */
  const char *zCirca = P("c");       /* Events near this time */
  const char *zMark = P("m");        /* Mark this event or an event this time */
  const char *zTagName = P("t");     /* Show events with this tag */
  const char *zBrName = P("r");      /* Equivalent to t=TAG&rel */
  int related = PB("rel");           /* Show events related to zTagName */
  const char *zMatchStyle = P("ms"); /* Tag/branch match style string */
  MatchStyle matchStyle = MS_EXACT;  /* Match style code */
  const char *zMatchDesc = 0;        /* Tag match expression description text */
  const char *zError = 0;            /* Tag match error string */
  const char *zTagSql = 0;           /* Tag/branch match SQL expression */
  const char *zSearch = P("s");      /* Search string */
  const char *zUses = P("uf");       /* Only show check-ins hold this file */
  const char *zYearMonth = P("ym");  /* Show check-ins for the given YYYY-MM */
  const char *zYearWeek = P("yw");   /* Check-ins for YYYY-WW (week-of-year) */
  const char *zDay = P("ymd");       /* Check-ins for the day YYYY-MM-DD */
  const char *zChng = P("chng");     /* List of GLOBs for files that changed */
  int useDividers = P("nd")==0;      /* Show dividers if "nd" is missing */
  int renameOnly = P("namechng")!=0; /* Show only check-ins that rename files */
  int forkOnly = PB("forks");        /* Show only forks and their children */
  int bisectOnly = PB("bisect");     /* Show the check-ins of the bisect */
  int tmFlags = 0;                   /* Timeline flags */
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
  const char *z;
  char *zOlderButton = 0;             /* URL for Older button at the bottom */
  int selectedRid = -9999999;         /* Show a highlight on this RID */
  int disableY = 0;                   /* Disable type selector on submenu */

  /* Set number of rows to display */
  z = P("n");
  if( z ){
    if( fossil_strcmp(z,"all")==0 ){
      nEntry = 0;
    }else{
      nEntry = atoi(z);
      if( nEntry<=0 ){
        cgi_replace_query_parameter("n","10");
        nEntry = 10;
      }
    }
  }else if( zCirca ){
    cgi_replace_query_parameter("n","11");
    nEntry = 11;
  }else{
    cgi_replace_query_parameter("n","50");
    nEntry = 50;
  }

  /* To view the timeline, must have permission to read project data.
  */
  pd_rid = name_to_typed_rid(P("dp"),"ci");
  if( pd_rid ){
    p_rid = d_rid = pd_rid;
  }
  login_check_credentials();
  if( (!g.perm.Read && !g.perm.RdTkt && !g.perm.RdWiki)
   || (bisectOnly && !g.perm.Setup)
  ){
    login_needed(g.anon.Read && g.anon.RdTkt && g.anon.RdWiki);
    return;
  }
  url_initialize(&url, "timeline");
  cgi_query_parameters_to_url(&url);

  /* Convert r=TAG to t=TAG&rel. */
  if( zBrName && !related ){
    cgi_delete_query_parameter("r");
    cgi_set_query_parameter("t", zBrName);
    cgi_set_query_parameter("rel", "1");
    zTagName = zBrName;
    related = 1;
  }

  /* Ignore empty tag query strings. */
  if( zTagName && !*zTagName ){
    zTagName = 0;
  }

  /* Finish preliminary processing of tag match queries. */
  if( zTagName ){
    /* Interpet the tag style string. */
    if( fossil_stricmp(zMatchStyle, "glob")==0 ){
      matchStyle = MS_GLOB;
    }else if( fossil_stricmp(zMatchStyle, "like")==0 ){
      matchStyle = MS_LIKE;
    }else if( fossil_stricmp(zMatchStyle, "regexp")==0 ){
      matchStyle = MS_REGEXP;
    }else{
      /* For exact maching, inhibit links to the selected tag. */
      zThisTag = zTagName;
    }

    /* Display a checkbox to enable/disable display of related check-ins. */
    style_submenu_checkbox("rel", "Related", 0, 0);

    /* Construct the tag match expression. */
    zTagSql = tagMatchExpression(matchStyle, zTagName, &zMatchDesc, &zError);
  }

  if( zMark && zMark[0]==0 ){
    if( zAfter ) zMark = zAfter;
    if( zBefore ) zMark = zBefore;
    if( zCirca ) zMark = zCirca;
  }
  if( (zTagSql && db_int(0,"SELECT count(*) "
      "FROM tagxref NATURAL JOIN tag WHERE %s",zTagSql/*safe-for-%s*/)<=nEntry)
  ){
    nEntry = -1;
    zCirca = 0;
  }
  if( zType[0]=='a' ){
    tmFlags |= TIMELINE_BRIEF | TIMELINE_GRAPH;
  }else{
    tmFlags |= TIMELINE_GRAPH;
  }
  if( PB("ng") || zSearch!=0 ){
    tmFlags &= ~TIMELINE_GRAPH;
  }
  if( PB("brbg") ){
    tmFlags |= TIMELINE_BRCOLOR;
  }
  if( PB("unhide") ){
    tmFlags |= TIMELINE_UNHIDE;
  }
  if( PB("ubg") ){
    tmFlags |= TIMELINE_UCOLOR;
  }
  if( zUses!=0 ){
    int ufid = db_int(0, "SELECT rid FROM blob WHERE uuid GLOB '%q*'", zUses);
    if( ufid ){
      zUses = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", ufid);
      db_multi_exec("CREATE TEMP TABLE usesfile(rid INTEGER PRIMARY KEY)");
      compute_uses_file("usesfile", ufid, 0);
      zType = "ci";
      disableY = 1;
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
    disableY = 1;
  }
  if( forkOnly ){
    db_multi_exec(
      "CREATE TEMP TABLE rnfork(rid INTEGER PRIMARY KEY);\n"
      "INSERT OR IGNORE INTO rnfork(rid)\n"
      "  SELECT pid FROM plink\n"
      "   WHERE (SELECT value FROM tagxref WHERE tagid=%d AND rid=cid)=="
      "           (SELECT value FROM tagxref WHERE tagid=%d AND rid=pid)\n"
      "   GROUP BY pid"
      "   HAVING count(*)>1;\n"
      "INSERT OR IGNORE INTO rnfork(rid)"
      "  SELECT cid FROM plink\n"
      "   WHERE (SELECT value FROM tagxref WHERE tagid=%d AND rid=cid)=="
      "           (SELECT value FROM tagxref WHERE tagid=%d AND rid=pid)\n"
      "     AND pid IN rnfork;",
      TAG_BRANCH, TAG_BRANCH, TAG_BRANCH, TAG_BRANCH
    );
    tmFlags |= TIMELINE_UNHIDE;
    zType = "ci";
    disableY = 1;
  }
  if( bisectOnly
   && fossil_strcmp(g.zIpAddr,"127.0.0.1")==0
   && db_open_local(0)
  ){
    int iCurrent = db_lget_int("checkout",0);
    bisect_create_bilog_table(iCurrent);
    tmFlags |= TIMELINE_UNHIDE | TIMELINE_BISECT;
    zType = "ci";
    disableY = 1;
  }else{
    bisectOnly = 0;
  }

  style_header("Timeline");
  login_anonymous_available();
  timeline_temp_table();
  blob_zero(&sql);
  blob_zero(&desc);
  blob_append(&sql, "INSERT OR IGNORE INTO timeline ", -1);
  blob_append(&sql, timeline_query_for_www(), -1);
  if( PB("fc") || PB("v") || PB("detail") ){
    tmFlags |= TIMELINE_FCHANGES;
  }
  if( (tmFlags & TIMELINE_UNHIDE)==0 ){
    blob_append_sql(&sql,
      " AND NOT EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)\n",
      TAG_HIDDEN
    );
  }
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
    addFileGlobExclusion(zChng, &sql);
    tmFlags |= TIMELINE_DISJOINT;
    db_multi_exec("%s", blob_sql_text(&sql));
    style_submenu_checkbox("v", "Files", zType[0]!='a' && zType[0]!='c', 0);
    blob_appendf(&desc, "%d check-ins going from ",
                 db_int(0, "SELECT count(*) FROM timeline"));
    blob_appendf(&desc, "%z[%h]</a>", href("%R/info/%h", zFrom), zFrom);
    blob_append(&desc, " to ", -1);
    blob_appendf(&desc, "%z[%h]</a>", href("%R/info/%h",zTo), zTo);
    addFileGlobDescription(zChng, &desc);
  }else if( (p_rid || d_rid) && g.perm.Read ){
    /* If p= or d= is present, ignore all other parameters other than n= */
    char *zUuid;
    int np, nd;

    tmFlags |= TIMELINE_DISJOINT;
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
      if( useDividers ) selectedRid = d_rid;
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
      if( useDividers ) selectedRid = p_rid;
    }
    blob_appendf(&desc, " of %z[%S]</a>",
                   href("%R/info/%!S", zUuid), zUuid);
    if( d_rid ){
      if( p_rid ){
        /* If both p= and d= are set, we don't have the uuid of d yet. */
        zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", d_rid);
      }
    }
    style_submenu_checkbox("v", "Files", zType[0]!='a' && zType[0]!='c', 0);
    style_submenu_entry("n","Max:",4,0);
    timeline_y_submenu(1);
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
    if( useDividers ) selectedRid = f_rid;
    blob_appendf(&desc, "Parents and children of check-in ");
    zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", f_rid);
    blob_appendf(&desc, "%z[%S]</a>", href("%R/info/%!S", zUuid), zUuid);
    tmFlags |= TIMELINE_DISJOINT;
    style_submenu_checkbox("unhide", "Unhide", 0, 0);
    style_submenu_checkbox("v", "Files", zType[0]!='a' && zType[0]!='c', 0);
  }else{
    /* Otherwise, a timeline based on a span of time */
    int n;
    const char *zEType = "timeline item";
    char *zDate;
    Blob cond;
    blob_zero(&cond);
    if( zChng && *zChng ){
      addFileGlobExclusion(zChng, &cond);
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( zUses ){
      blob_append_sql(&cond, " AND event.objid IN usesfile ");
    }
    if( renameOnly ){
      blob_append_sql(&cond, " AND event.objid IN rnfile ");
    }
    if( forkOnly ){
      blob_append_sql(&cond, " AND event.objid IN rnfork ");
    }
    if( bisectOnly ){
      blob_append_sql(&cond, " AND event.objid IN (SELECT rid FROM bilog) ");
    }
    if( zYearMonth ){
      blob_append_sql(&cond, " AND %Q=strftime('%%Y-%%m',event.mtime) ",
                   zYearMonth);
    }
    else if( zYearWeek ){
      blob_append_sql(&cond, " AND %Q=strftime('%%Y-%%W',event.mtime) ",
                   zYearWeek);
    }
    else if( zDay ){
      blob_append_sql(&cond, " AND %Q=strftime('%%Y-%%m-%%d',event.mtime) ",
                   zDay);
    }
    if( zTagSql ){
      blob_append_sql(&cond,
        " AND (EXISTS(SELECT 1 FROM tagxref NATURAL JOIN tag"
        " WHERE %s AND tagtype>0 AND rid=blob.rid)\n", zTagSql/*safe-for-%s*/);

      if( related ){
        /* The next two blob_appendf() calls add SQL that causes check-ins that
        ** are not part of the branch which are parents or children of the
        ** branch to be included in the report.  This related check-ins are
        ** useful in helping to visualize what has happened on a quiescent
        ** branch that is infrequently merged with a much more activate branch.
        */
        blob_append_sql(&cond,
          " OR EXISTS(SELECT 1 FROM plink CROSS JOIN tagxref ON rid=cid"
          " NATURAL JOIN tag WHERE %s AND tagtype>0 AND pid=blob.rid)\n",
           zTagSql/*safe-for-%s*/
        );
        if( (tmFlags & TIMELINE_UNHIDE)==0 ){
          blob_append_sql(&cond,
            " AND NOT EXISTS(SELECT 1 FROM plink JOIN tagxref ON rid=cid"
                       " WHERE tagid=%d AND tagtype>0 AND pid=blob.rid)\n",
            TAG_HIDDEN
          );
        }
        if( P("mionly")==0 ){
          blob_append_sql(&cond,
            " OR EXISTS(SELECT 1 FROM plink CROSS JOIN tagxref ON rid=pid"
            " NATURAL JOIN tag WHERE %s AND tagtype>0 AND cid=blob.rid)\n",
            zTagSql/*safe-for-%s*/
          );
          if( (tmFlags & TIMELINE_UNHIDE)==0 ){
            blob_append_sql(&cond,
              " AND NOT EXISTS(SELECT 1 FROM plink JOIN tagxref ON rid=pid"
              " WHERE tagid=%d AND tagtype>0 AND cid=blob.rid)\n",
              TAG_HIDDEN
            );
          }
        }
      }
      blob_append_sql(&cond, ")");
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
        blob_append_sql(&cond, " AND event.type IN ");
        if( g.perm.Read ){
          blob_append_sql(&cond, "%c'ci','g'", cSep);
          cSep = ',';
        }
        if( g.perm.RdWiki ){
          blob_append_sql(&cond, "%c'w','e'", cSep);
          cSep = ',';
        }
        if( g.perm.RdTkt ){
          blob_append_sql(&cond, "%c't'", cSep);
          cSep = ',';
        }
        blob_append_sql(&cond, ")");
      }
    }else{ /* zType!="all" */
      blob_append_sql(&cond, " AND event.type=%Q", zType);
      if( zType[0]=='c' ){
        zEType = "check-in";
      }else if( zType[0]=='w' ){
        zEType = "wiki edit";
      }else if( zType[0]=='t' ){
        zEType = "ticket change";
      }else if( zType[0]=='e' ){
        zEType = "technical note";
      }else if( zType[0]=='g' ){
        zEType = "tag";
      }
    }
    if( zUser ){
      int n = db_int(0,"SELECT count(*) FROM event"
                       " WHERE user=%Q OR euser=%Q", zUser, zUser);
      if( n<=nEntry ){
        zCirca = zBefore = zAfter = 0;
        nEntry = -1;
      }
      blob_append_sql(&cond, " AND (event.user=%Q OR event.euser=%Q)",
                   zUser, zUser);
      zThisUser = zUser;
    }
    if( zSearch ){
      blob_append_sql(&cond,
        " AND (event.comment LIKE '%%%q%%' OR event.brief LIKE '%%%q%%')",
        zSearch, zSearch);
    }
    rBefore = symbolic_name_to_mtime(zBefore);
    rAfter = symbolic_name_to_mtime(zAfter);
    rCirca = symbolic_name_to_mtime(zCirca);
    blob_append_sql(&sql, "%s", blob_sql_text(&cond));
    if( rAfter>0.0 ){
      if( rBefore>0.0 ){
        blob_append_sql(&sql,
           " AND event.mtime>=%.17g AND event.mtime<=%.17g"
           " ORDER BY event.mtime ASC", rAfter-ONE_SECOND, rBefore+ONE_SECOND);
        nEntry = -1;
      }else{
        blob_append_sql(&sql,
           " AND event.mtime>=%.17g  ORDER BY event.mtime ASC",
           rAfter-ONE_SECOND);
      }
      zCirca = 0;
      url_add_parameter(&url, "c", 0);
    }else if( rBefore>0.0 ){
      blob_append_sql(&sql,
         " AND event.mtime<=%.17g ORDER BY event.mtime DESC",
         rBefore+ONE_SECOND);
      zCirca = 0;
      url_add_parameter(&url, "c", 0);
    }else if( rCirca>0.0 ){
      Blob sql2;
      blob_init(&sql2, blob_sql_text(&sql), -1);
      blob_append_sql(&sql2,
          " AND event.mtime<=%f ORDER BY event.mtime DESC", rCirca);
      if( nEntry>0 ){
        blob_append_sql(&sql2," LIMIT %d", (nEntry+1)/2);
        nEntry -= (nEntry+1)/2;
      }
      if( PB("showsql") ){
         @ <pre>%h(blob_sql_text(&sql2))</pre>
      }
      db_multi_exec("%s", blob_sql_text(&sql2));
      blob_reset(&sql2);
      blob_append_sql(&sql,
          " AND event.mtime>=%f ORDER BY event.mtime ASC",
          rCirca
      );
      if( zMark==0 ) zMark = zCirca;
    }else{
      blob_append_sql(&sql, " ORDER BY event.mtime DESC");
    }
    if( nEntry>0 ) blob_append_sql(&sql, " LIMIT %d", nEntry);
    db_multi_exec("%s", blob_sql_text(&sql));

    n = db_int(0, "SELECT count(*) FROM timeline WHERE etype!='div' /*scan*/");
    if( zYearMonth ){
      blob_appendf(&desc, "%s events for %h", zEType, zYearMonth);
    }else if( zYearWeek ){
      blob_appendf(&desc, "%s events for year/week %h", zEType, zYearWeek);
    }else if( zDay ){
      blob_appendf(&desc, "%s events occurring on %h", zEType, zDay);
    }else if( zBefore==0 && zCirca==0 && n>=nEntry && nEntry>0 ){
      blob_appendf(&desc, "%d most recent %ss", n, zEType);
    }else{
      blob_appendf(&desc, "%d %ss", n, zEType);
    }
    if( zUses ){
      char *zFilenames = names_of_file(zUses);
      blob_appendf(&desc, " using file %s version %z%S</a>", zFilenames,
                   href("%R/artifact/%!S",zUses), zUses);
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( renameOnly ){
      blob_appendf(&desc, " that contain filename changes");
      tmFlags |= TIMELINE_DISJOINT|TIMELINE_FRENAMES;
    }
    if( forkOnly ){
      blob_appendf(&desc, " associated with forks");
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( bisectOnly ){
      blob_appendf(&desc, " in the most recent bisect");
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( zUser ){
      blob_appendf(&desc, " by user %h", zUser);
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( zTagSql ){
      if( matchStyle==MS_EXACT ){
        if( related ){
          blob_appendf(&desc, " related to %h", zMatchDesc);
        }else{
          blob_appendf(&desc, " tagged with %h", zMatchDesc);
        }
      }else{
        if( related ){
          blob_appendf(&desc, " related to tags matching %h", zMatchDesc);
        }else{
          blob_appendf(&desc, " with tags matching %h", zMatchDesc);
        }
      }
      tmFlags |= TIMELINE_DISJOINT;
    }
    addFileGlobDescription(zChng, &desc);
    if( rAfter>0.0 ){
      if( rBefore>0.0 ){
        blob_appendf(&desc, " occurring between %h and %h.<br />",
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
      static const char *const azMatchStyles[] = {
        "exact", "Exact", "glob", "Glob", "like", "Like", "regexp", "Regexp"
      };
      double rDate;
      zDate = db_text(0, "SELECT min(timestamp) FROM timeline /*scan*/");
      if( (!zDate || !zDate[0]) && ( zAfter || zBefore ) ){
        zDate = mprintf("%s", (zAfter ? zAfter : zBefore));
      }
      if( zDate ){
        rDate = symbolic_name_to_mtime(zDate);
        if( db_int(0,
            "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
            " WHERE blob.rid=event.objid AND mtime<=%.17g%s)",
            rDate-ONE_SECOND, blob_sql_text(&cond))
        ){
          timeline_submenu(&url, "Older", "b", zDate, "a");
          zOlderButton = fossil_strdup(url_render(&url, "b", zDate, "a", 0));
        }
        free(zDate);
      }
      zDate = db_text(0, "SELECT max(timestamp) FROM timeline /*scan*/");
      if( (!zDate || !zDate[0]) && ( zAfter || zBefore ) ){
        zDate = mprintf("%s", (zBefore ? zBefore : zAfter));
      }
      if( zDate ){
        rDate = symbolic_name_to_mtime(zDate);
        if( db_int(0,
            "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
            " WHERE blob.rid=event.objid AND mtime>=%.17g%s)",
            rDate+ONE_SECOND, blob_sql_text(&cond))
        ){
          timeline_submenu(&url, "Newer", "a", zDate, "b");
        }
        free(zDate);
      }
      if( zType[0]=='a' || zType[0]=='c' ){
        style_submenu_checkbox("unhide", "Unhide", 0, 0);
      }
      style_submenu_checkbox("v", "Files", zType[0]!='a' && zType[0]!='c', 0);
      style_submenu_entry("n","Max:",4,0);
      timeline_y_submenu(disableY);
      style_submenu_entry("t", "Tag Filter:", -8, 0);
      style_submenu_multichoice("ms", count(azMatchStyles)/2, azMatchStyles, 0);
    }
    blob_zero(&cond);
  }
  if( PB("showsql") ){
    @ <pre>%h(blob_sql_text(&sql))</pre>
  }
  if( search_restrict(SRCH_CKIN)!=0 ){
    style_submenu_element("Search", "%R/search?y=c");
  }
  if( PB("showid") ) tmFlags |= TIMELINE_SHOWRID;
  if( useDividers && zMark && zMark[0] ){
    double r = symbolic_name_to_mtime(zMark);
    if( r>0.0 ) selectedRid = timeline_add_divider(r);
  }
  blob_zero(&sql);
  db_prepare(&q, "SELECT * FROM timeline ORDER BY sortby DESC /*scan*/");
  @ <h2>%b(&desc)</h2>
  blob_reset(&desc);

  /* Report any errors. */
  if( zError ){
    @ <p class="generalError">%h(zError)</p>
  }

  www_print_timeline(&q, tmFlags, zThisUser, zThisTag, selectedRid, 0);
  db_finalize(&q);
  if( zOlderButton ){
    @ %z(xhref("class='button'","%z",zOlderButton))Older</a>
  }
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
      n += strlen(zPrefix+n);
    }
    if( content_is_private(rid) ){
      sqlite3_snprintf(sizeof(zPrefix)-n, &zPrefix[n], "*UNPUBLISHED* ");
      n += strlen(zPrefix+n);
    }
    zFree = mprintf("[%S] %s%s", zId, zPrefix, zCom);
    /* record another X lines */
    nLine += comment_print(zFree, zCom, 9, width, g.comFmtFlags);
    fossil_free(zFree);

    if(verboseFlag){
      if( !fchngQueryInit ){
        db_prepare(&fchngQuery,
           "SELECT (pid<=0) AS isnew,"
           "       (fid==0) AS isdel,"
           "       (SELECT name FROM filename WHERE fnid=mlink.fnid) AS name,"
           "       (SELECT uuid FROM blob WHERE rid=fid),"
           "       (SELECT uuid FROM blob WHERE rid=pid)"
           "  FROM mlink"
           " WHERE mid=:mid AND pid!=fid AND NOT mlink.isaux"
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
    @   datetime(event.mtime,toLocal()) AS mDateTime,
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
** Return true if the input string can be converted to a julianday.
*/
static int fossil_is_julianday(const char *zDate){
  return db_int(0, "SELECT EXISTS (SELECT julianday(%Q) AS jd WHERE jd IS NOT NULL)", zDate);
}

/*
** COMMAND: timeline
**
** Usage: %fossil timeline ?WHEN? ?CHECKIN|DATETIME? ?OPTIONS?
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
** The CHECKIN can be any unique prefix of 4 characters or more. You
** can also say "current" for the current version.
**
** DATETIME may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
** year-month-day form, it may be truncated, the "T" may be replaced by
** a space, and it may also name a timezone offset from UTC as "-HH:MM"
** (westward) or "+HH:MM" (eastward). Either no timezone suffix or "Z"
** means UTC.
**
**
** Options:
**   -n|--limit N         If N is positive, output the first N entries.  If
**                        N is negative, output the first -N lines.  If N is
**                        zero, no limit.  Default is -20 meaning 20 lines.
**   -p|--path PATH       Output items affecting PATH only.
**                        PATH can be a file or a sub directory.
**   --offset P           skip P changes
**   -t|--type TYPE       Output items from the given types only, such as:
**                            ci = file commits only
**                            e  = technical notes only
**                            t  = tickets only
**                            w  = wiki commits only
**   -v|--verbose         Output the list of files changed by each commit
**                        and the type of each change (edited, deleted,
**                        etc.) after the check-in comment.
**   -W|--width <num>     Width of lines (default is to auto-detect). Must be
**                        >20 or 0 (= no limit, resulting in a single line per
**                        entry).
**   -R REPO_FILE         Specifies the repository db to use. Default is
**                        the current checkout's repository.
*/
void timeline_cmd(void){
  Stmt q;
  int n, k, width;
  const char *zLimit;
  const char *zWidth;
  const char *zOffset;
  const char *zType;
  char *zOrigin;
  char *zDate;
  Blob sql;
  int objid = 0;
  Blob uuid;
  int mode = TIMELINE_MODE_NONE;
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
  zFilePattern = find_option("path","p",1);

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

  if( g.argc>=4 ){
    k = strlen(g.argv[2]);
    if( strncmp(g.argv[2],"before",k)==0 ){
      mode = TIMELINE_MODE_BEFORE;
    }else if( strncmp(g.argv[2],"after",k)==0 && k>1 ){
      mode = TIMELINE_MODE_AFTER;
    }else if( strncmp(g.argv[2],"descendants",k)==0 ){
      mode = TIMELINE_MODE_CHILDREN;
    }else if( strncmp(g.argv[2],"children",k)==0 ){
      mode = TIMELINE_MODE_CHILDREN;
    }else if( strncmp(g.argv[2],"ancestors",k)==0 && k>1 ){
      mode = TIMELINE_MODE_PARENTS;
    }else if( strncmp(g.argv[2],"parents",k)==0 ){
      mode = TIMELINE_MODE_PARENTS;
    }else if(!zType && !zLimit){
      usage("?WHEN? ?CHECKIN|DATETIME? ?-n|--limit #? ?-t|--type TYPE? "
            "?-W|--width WIDTH? ?-p|--path PATH");
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
  if( fossil_strcmp(zOrigin, "now")==0 ){
    if( mode==TIMELINE_MODE_CHILDREN || mode==TIMELINE_MODE_PARENTS ){
      fossil_fatal("cannot compute descendants or ancestors of a date");
    }
    zDate = mprintf("(SELECT datetime('now'))");
  }else if( strncmp(zOrigin, "current", k)==0 ){
    if( !g.localOpen ){
      fossil_fatal("must be within a local checkout to use 'current'");
    }
    objid = db_lget_int("checkout",0);
    zDate = mprintf("(SELECT mtime FROM plink WHERE cid=%d)", objid);
  }else if( fossil_is_julianday(zOrigin) ){
    const char *zShift = "";
    if( mode==TIMELINE_MODE_CHILDREN || mode==TIMELINE_MODE_PARENTS ){
      fossil_fatal("cannot compute descendants or ancestors of a date");
    }
    if( mode==TIMELINE_MODE_NONE ){
      if( isIsoDate(zOrigin) ) zShift = ",'+1 day'";
    }
    zDate = mprintf("(SELECT julianday(%Q%s, fromLocal()))", zOrigin, zShift);
  }else if( name_to_uuid(&uuid, 0, "*")==0 ){
    objid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &uuid);
    zDate = mprintf("(SELECT mtime FROM event WHERE objid=%d)", objid);
  }else{
    fossil_fatal("unknown check-in or invalid date: %s", zOrigin);
  }

  if( zFilePattern ){
    if( zType==0 ){
      /* When zFilePattern is specified and type is not specified, only show
       * file check-ins */
      zType="ci";
    }
    file_tree_name(zFilePattern, &treeName, 0, 1);
    if( fossil_strcmp(blob_str(&treeName), ".")==0 ){
      /* When zTreeName refers to g.zLocalRoot, it's like not specifying
       * zFilePattern. */
      zFilePattern = 0;
    }
  }

  if( mode==TIMELINE_MODE_NONE ) mode = TIMELINE_MODE_BEFORE;
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_tty(), -1);
  blob_append_sql(&sql, "\n  AND event.mtime %s %s",
     ( mode==TIMELINE_MODE_BEFORE ||
       mode==TIMELINE_MODE_PARENTS ) ? "<=" : ">=", zDate /*safe-for-%s*/
  );

  /* When zFilePattern is specified, compute complete ancestry;
   * limit later at print_timeline() */
  if( mode==TIMELINE_MODE_CHILDREN || mode==TIMELINE_MODE_PARENTS ){
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    if( mode==TIMELINE_MODE_CHILDREN ){
      compute_descendants(objid, (zFilePattern ? 0 : n));
    }else{
      compute_ancestors(objid, (zFilePattern ? 0 : n), 0);
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
** COMMAND: test-timewarp-list
**
** Usage: %fossil test-timewarp-list ?-v|---verbose?
**
** Display all instances of child check-ins that appear earlier in time
** than their parent.  If the -v|--verbose option is provided, both the
** parent and child check-ins and their times are shown.
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
** WEBPAGE: timewarps
**
** Show all check-ins that are "timewarps".  A timewarp is a
** check-in that occurs before its parent, according to the
** timestamp information on the check-in.  This can only actually
** happen, of course, if a users system clock is set incorrectly.
*/
void test_timewarp_page(void){
  Stmt q;
  int cnt = 0;

  login_check_credentials();
  if( !g.perm.Read || !g.perm.Hyperlink ){
    login_needed(g.anon.Read && g.anon.Hyperlink);
    return;
  }
  style_header("Instances of timewarp");
  db_prepare(&q,
     "SELECT blob.uuid, "
     "       date(ce.mtime),"
     "       pe.mtime>ce.mtime,"
     "       coalesce(ce.euser,ce.user)"
     "  FROM plink p, plink c, blob, event pe, event ce"
     " WHERE p.cid=c.pid  AND p.mtime>c.mtime"
     "   AND blob.rid=c.cid"
     "   AND pe.objid=p.cid"
     "   AND ce.objid=c.cid"
     " ORDER BY 2 DESC"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zCkin = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zStatus = db_column_int(&q,2) ? "Open"
                                 : "Resolved by editing date";
    const char *zUser = db_column_text(&q, 3);
    char *zHref = href("%R/timeline?c=%S", zCkin);
    if( cnt==0 ){
      @ <div class="brlist"><table id="timewarptable">
      @ <thead><tr>
      @ <th>Check-in</th>
      @ <th>Date</th>
      @ <th>User</th>
      @ <th>Status</th>
      @ </tr></thead><tbody>
    }
    @ <tr>
    @ <td>%s(zHref)%S(zCkin)</a></td>
    @ <td>%s(zHref)%s(zDate)</a></td>
    @ <td>%h(zUser)</td>
    @ <td>%s(zStatus)</td>
    @ </tr>
    fossil_free(zHref);
    cnt++;
  }
  db_finalize(&q);
  if( cnt==0 ){
    @ <p>No timewarps in this repository</p>
  }else{
    @ </tbody></table></div>
    output_table_sorting_javascript("timewarptable","tttt",2);
  }
  style_footer();
}
