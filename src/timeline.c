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

#define TIMELINE_FMT_ONELINE \
    "%h %c"
#define TIMELINE_FMT_MEDIUM \
    "Commit:   %h%nDate:     %d%nAuthor:   %a%nComment:  %c"
#define TIMELINE_FMT_FULL \
    "Commit:   %H%nDate:     %d%nAuthor:   %a%nComment:  %c%n"\
    "Branch:   %b%nTags:     %t%nPhase:    %p"
/*
** Add an appropriate tag to the output if "rid" is unpublished (private)
*/
#define UNPUB_TAG "<em>(unpublished)</em>"
void tag_private_status(int rid){
  if( content_is_private(rid) ){
    cgi_printf(" %s", UNPUB_TAG);
  }
}

/*
** Generate a hyperlink to a version.
*/
void hyperlink_to_version(const char *zVerHash){
  if( g.perm.Hyperlink ){
    @ %z(chref("timelineHistLink","%R/info/%!S",zVerHash))[%S(zVerHash)]</a>
  }else{
    @ <span class="timelineHistDsp">[%S(zVerHash)]</span>
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
      @ %z(href("%R/timeline?c=%T&u=%T&y=a",zD,zU))%h(zU)</a>%s(zSuf)
    }else{
      @ %z(href("%R/timeline?u=%T&y=a",zU))%h(zU)</a>%s(zSuf)
    }
  }else{
    @ %s(zU)
  }
}

/*
** Allowed flags for the tmFlags argument to www_print_timeline
*/
#if INTERFACE
#define TIMELINE_ARTID    0x0000001 /* Show artifact IDs on non-check-in lines*/
#define TIMELINE_LEAFONLY 0x0000002 /* Show "Leaf" but not "Merge", "Fork" etc*/
#define TIMELINE_BRIEF    0x0000004 /* Combine adjacent elements of same obj */
#define TIMELINE_GRAPH    0x0000008 /* Compute a graph */
#define TIMELINE_DISJOINT 0x0000010 /* Elements are not contiguous */
#define TIMELINE_FCHANGES 0x0000020 /* Detail file changes */
#define TIMELINE_BRCOLOR  0x0000040 /* Background color by branch name */
#define TIMELINE_UCOLOR   0x0000080 /* Background color by user */
#define TIMELINE_FRENAMES 0x0000100 /* Detail only file name changes */
#define TIMELINE_UNHIDE   0x0000200 /* Unhide check-ins with "hidden" tag */
#define TIMELINE_SHOWRID  0x0000400 /* Show RID values in addition to hashes */
#define TIMELINE_BISECT   0x0000800 /* Show supplemental bisect information */
#define TIMELINE_COMPACT  0x0001000 /* Use the "compact" view style */
#define TIMELINE_VERBOSE  0x0002000 /* Use the "detailed" view style */
#define TIMELINE_MODERN   0x0004000 /* Use the "modern" view style */
#define TIMELINE_COLUMNAR 0x0008000 /* Use the "columns" view style */
#define TIMELINE_CLASSIC  0x0010000 /* Use the "classic" view style */
#define TIMELINE_VIEWS    0x001f000 /* Mask for all of the view styles */
#define TIMELINE_NOSCROLL 0x0100000 /* Don't scroll to the selection */
#define TIMELINE_FILEDIFF 0x0200000 /* Show File differences, not ckin diffs */
#define TIMELINE_CHPICK   0x0400000 /* Show cherrypick merges */
#define TIMELINE_FILLGAPS 0x0800000 /* Dotted lines for missing nodes */
#define TIMELINE_XMERGE   0x1000000 /* Omit merges from off-graph nodes */
#define TIMELINE_NOTKT    0x2000000 /* Omit extra ticket classes */
#define TIMELINE_FORUMTXT 0x4000000 /* Render all forum messages */
#define TIMELINE_REFS     0x8000000 /* Output intended for References tab */
#define TIMELINE_DELTA   0x10000000 /* Background color shows delta manifests */
#define TIMELINE_NOCOLOR 0x20000000 /* No colors except for highlights */
#endif

/*
** Return a new timelineTable id.
*/
int timeline_tableid(void){
  static int id = 0;
  return id++;
}

/*
** Return true if the checking identified by "rid" has a valid "closed"
** tag.
*/
static int has_closed_tag(int rid){
  static Stmt q;
  int res = 0;
  db_static_prepare(&q,
     "SELECT 1 FROM tagxref WHERE rid=$rid AND tagid=%d AND tagtype>0",
     TAG_CLOSED);
  db_bind_int(&q, "$rid", rid);
  res = db_step(&q)==SQLITE_ROW;
  db_reset(&q);
  return res;
}

/*
** Return the text of the unformatted
** forum post given by the RID in the argument.
*/
static void forum_post_content_function(
 sqlite3_context *context,
 int argc,
 sqlite3_value **argv
){
  int rid = sqlite3_value_int(argv[0]);
  Manifest *pPost = manifest_get(rid, CFTYPE_FORUM, 0);
  if( pPost ){
    sqlite3_result_text(context, pPost->zWiki, -1, SQLITE_TRANSIENT);
    manifest_destroy(pPost);
  }
}


/*
** Output a timeline in the web format given a query.  The query
** should return these columns:
**
**    0.  rid
**    1.  artifact hash
**    2.  Date/Time
**    3.  Comment string
**    4.  User
**    5.  True if is a leaf
**    6.  background color
**    7.  type ("ci", "w", "t", "e", "g", "f", "div")
**    8.  list of symbolic tags.
**    9.  tagid for ticket or wiki or event
**   10.  Short comment to user for repeated tickets and wiki
*/
void www_print_timeline(
  Stmt *pQuery,            /* Query to implement the timeline */
  int tmFlags,             /* Flags controlling display behavior */
  const char *zThisUser,   /* Suppress links to this user */
  const char *zThisTag,    /* Suppress links to this tag */
  Matcher *pLeftBranch,    /* Comparison function to use for zLeftBranch */
  int selectedRid,         /* Highlight the line with this RID value or zero */
  int secondRid,           /* Secondary highlight (or zero) */
  void (*xExtra)(int)      /* Routine to call on each line of display */
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
  int pendingEndTr = 0;       /* True if a </td></tr> is needed */
  int vid = 0;                /* Current check-out version */
  int dateFormat = 0;         /* 0: HH:MM (default) */
  int bCommentGitStyle = 0;   /* Only show comments through first blank line */
  const char *zStyle;         /* Sub-name for classes for the style */
  const char *zDateFmt;
  int iTableId = timeline_tableid();
  int bTimestampLinksToInfo;  /* True if timestamp hyperlinks go to the /info
                              ** page rather than the /timeline page */

  if( cgi_is_loopback(g.zIpAddr) && db_open_local(0) ){
    vid = db_lget_int("checkout", 0);
  }
  zPrevDate[0] = 0;
  mxWikiLen = db_get_int("timeline-max-comment", 0);
  dateFormat = db_get_int("timeline-date-format", 0);
/*
** SETTING: timeline-truncate-at-blank  boolean default=off
**
** If enabled, check-in comments displayed on the timeline are truncated
** at the first blank line of the comment text.  The comment text after
** the first blank line is only seen in the /info or similar pages that
** show details about the check-in.
*/
  bCommentGitStyle = db_get_int("timeline-truncate-at-blank", 0);
/*
** SETTING: timeline-tslink-info       boolean default=off
**
** The hyperlink on the timestamp associated with each timeline entry,
** on the far left-hand side of the screen, normally targets another
** /timeline page that shows the entry in context.  However, if this
** option is turned on, that hyperlink targets the /info page showing
** the details of the entry.
*/
  bTimestampLinksToInfo = db_get_boolean("timeline-tslink-info", 0);
  if( (tmFlags & TIMELINE_VIEWS)==0 ){
    tmFlags |= timeline_ss_cookie();
  }
  if( tmFlags & TIMELINE_COLUMNAR ){
    zStyle = "Columnar";
  }else if( tmFlags & TIMELINE_COMPACT ){
    zStyle = "Compact";
  }else if( tmFlags & TIMELINE_VERBOSE ){
    zStyle = "Verbose";
  }else if( tmFlags & TIMELINE_CLASSIC ){
    zStyle = "Classic";
  }else{
    zStyle = "Modern";
  }
  zDateFmt = P("datefmt");
  if( zDateFmt ) dateFormat = atoi(zDateFmt);
  if( tmFlags & TIMELINE_GRAPH ){
    pGraph = graph_init();
  }
  if( (tmFlags & TIMELINE_CHPICK)!=0
   && !db_table_exists("repository","cherrypick")
  ){
    tmFlags &= ~TIMELINE_CHPICK;
  }
  @ <table id="timelineTable%d(iTableId)" class="timelineTable"> \
  @ <!-- tmFlags: 0x%x(tmFlags) -->
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
    char *zBr = 0;            /* Branch */
    int commentColumn = 3;    /* Column containing comment text */
    int modPending;           /* Pending moderation */
    char *zDateLink;          /* URL for the link on the timestamp */
    int drawDetailEllipsis;   /* True to show ellipsis in place of detail */
    int gidx = 0;             /* Graph row identifier */
    int isSelectedOrCurrent = 0;  /* True if current row is selected */
    const char *zExtraClass = "";
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
      pendingEndTr = 0;
    }
    if( fossil_strcmp(zType,"div")==0 ){
      if( !prevWasDivider ){
        @ <tr><td colspan="3"><hr class="timelineMarker"></td></tr>
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
        @ <tr class="timelineDateRow"><td>
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
      @ <tr class="timelineSelected">
      isSelectedOrCurrent = 1;
    }else if( rid==secondRid ){
      @ <tr class="timelineSelected timelineSecondary">
      isSelectedOrCurrent = 1;
    }else if( rid==vid ){
      @ <tr class="timelineCurrent">
      isSelectedOrCurrent = 1;
    }else {
      @ <tr>
    }
    if( zType[0]=='t' && tagid && (tmFlags & TIMELINE_NOTKT)==0 ){
      char *zTktid = db_text(0, "SELECT substr(tagname,5) FROM tag"
                                " WHERE tagid=%d", tagid);
      if( zTktid ){
        int isClosed = 0;
        if( is_ticket(zTktid, &isClosed) && isClosed ){
          zExtraClass = " tktTlClosed";
        }else{
          zExtraClass = " tktTlOpen";
        }
        fossil_free(zTktid);
      }
    }
    if( zType[0]=='e' && tagid ){
      if( bTimestampLinksToInfo ){
        char *zId;
        zId = db_text(0, "SELECT substr(tagname, 7) FROM tag WHERE tagid=%d",
                          tagid);
        zDateLink = href("%R/technote/%s",zId);
        free(zId);
      }else{
        zDateLink = href("%R/timeline?c=%t&y=a",zDate);
      }
    }else if( zUuid ){
      if( bTimestampLinksToInfo ){
        zDateLink = chref("timelineHistLink", "%R/info/%!S", zUuid);
      }else{
        zDateLink = chref("timelineHistLink", "%R/timeline?c=%!S&y=a", zUuid);
      }
    }else{
      zDateLink = mprintf("<a>");
    }
    @ <td class="timelineTime">%z(zDateLink)%s(zTime)</a></td>
    @ <td class="timelineGraph">
    if( tmFlags & (TIMELINE_UCOLOR|TIMELINE_DELTA|TIMELINE_NOCOLOR) ){
      /* Don't use the requested background color.  Use the background color
      ** override from query parameters instead. */
      if( tmFlags & TIMELINE_UCOLOR ){
        zBgClr = zUser ? user_color(zUser) : 0;
      }else if( tmFlags & TIMELINE_NOCOLOR ){
        zBgClr = 0;
      }else if( zType[0]=='c' ){
        static Stmt qdelta;
        db_static_prepare(&qdelta, "SELECT baseid IS NULL FROM plink"
                                   " WHERE cid=:rid");
        db_bind_int(&qdelta, ":rid", rid);
        if( db_step(&qdelta)!=SQLITE_ROW ){
          zBgClr = 0; /* Not a check-in */
        }else if( db_column_int(&qdelta, 0) ){
          zBgClr = hash_color("b");  /* baseline manifest */
        }else{
          zBgClr = hash_color("f");  /* delta manifest */
        }
        db_reset(&qdelta);
      }
    }else{
      /* Make sure the user-specified background color is reasonable */
      zBgClr = reasonable_bg_color(zBgClr, 0);
    }
    if( zType[0]=='c'
    && (pGraph || zBgClr==0 || (tmFlags & (TIMELINE_BRCOLOR|TIMELINE_DELTA))!=0)
    ){
      zBr = branch_of_rid(rid);
      if( zBgClr==0 || (tmFlags & TIMELINE_BRCOLOR)!=0 ){
        /* If no background color is specified, use a color based on the
        ** branch name */
        if( tmFlags & (TIMELINE_DELTA|TIMELINE_NOCOLOR) ){
        }else if( zBr==0 || strcmp(zBr,"trunk")==0 ){
          zBgClr = 0;
        }else{
          zBgClr = hash_color(zBr);
        }
      }
    }
    if( zType[0]=='c' && pGraph ){
      int nParent = 0;
      int nCherrypick = 0;
      GraphRowId aParent[GR_MAX_RAIL];
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
      if( (tmFlags & TIMELINE_CHPICK)!=0 && nParent>0 ){
        static Stmt qcherrypick;
        db_static_prepare(&qcherrypick,
          "SELECT parentid FROM cherrypick"
          " WHERE childid=:rid AND parentid NOT IN phantom"
        );
        db_bind_int(&qcherrypick, ":rid", rid);
        while( db_step(&qcherrypick)==SQLITE_ROW && nParent<count(aParent) ){
          aParent[nParent++] = db_column_int(&qcherrypick, 0);
          nCherrypick++;
        }
        db_reset(&qcherrypick);
      }
      gidx = graph_add_row(pGraph, rid, nParent, nCherrypick, aParent,
                           zBr, zBgClr, zUuid,
                           isLeaf ? isLeaf + 2 * has_closed_tag(rid) : 0);
      @ <div id="m%d(gidx)" class="tl-nodemark"></div>
    }else if( zType[0]=='e' && pGraph && zBgClr && zBgClr[0] ){
      /* For technotes, make a graph node with nParent==(-1).  This will
      ** not actually draw anything on the graph, but it will set the
      ** background color of the timeline entry */
      gidx = graph_add_row(pGraph, rid, -1, 0, 0, zBr, zBgClr, zUuid, 0);
      @ <div id="m%d(gidx)" class="tl-nodemark"></div>
    }
    fossil_free(zBr);
    @</td>
    if( !isSelectedOrCurrent ){
      @ <td class="timeline%s(zStyle)Cell%s(zExtraClass)" id='mc%d(gidx)'>
    }else{
      @ <td class="timeline%s(zStyle)Cell%s(zExtraClass)">
    }
    if( pGraph ){
      if( zType[0]=='e' ){
        @ <b>Note:</b>
      }else if( zType[0]!='c' ){
        @ &bull;
      }
    }
    if( modPending ){
      @ <span class="modpending">(Awaiting Moderator Approval)</span>
    }
    if( (tmFlags & TIMELINE_BISECT)!=0 && zType[0]=='c' ){
      static Stmt bisectQuery;
      db_static_prepare(&bisectQuery,
          "SELECT seq, stat FROM bilog WHERE rid=:rid AND seq");
      db_bind_int(&bisectQuery, ":rid", rid);
      if( db_step(&bisectQuery)==SQLITE_ROW ){
        @ <b>%s(db_column_text(&bisectQuery,1))</b>
        @ (%d(db_column_int(&bisectQuery,0)))
      }
      db_reset(&bisectQuery);
    }
    drawDetailEllipsis = (tmFlags & (TIMELINE_COMPACT))!=0;
    db_column_blob(pQuery, commentColumn, &comment);
    if( tmFlags & TIMELINE_COMPACT ){
      @ <span class='timelineCompactComment' data-id='%d(rid)'>
    }else{
      @ <span class='timeline%s(zStyle)Comment'>
    }
    if( (tmFlags & TIMELINE_CLASSIC)!=0 ){
      if( zType[0]=='c' ){
        hyperlink_to_version(zUuid);
        if( isLeaf ){
          if( has_closed_tag(rid) ){
            @ <span class="timelineLeaf">Closed-Leaf:</span>
          }else{
            @ <span class="timelineLeaf">Leaf:</span>
          }
        }
      }else if( zType[0]=='e' && tagid ){
        hyperlink_to_event_tagid(tagid<0?-tagid:tagid);
      }else if( (tmFlags & TIMELINE_ARTID)!=0 ){
        hyperlink_to_version(zUuid);
      }
      if( tmFlags & TIMELINE_SHOWRID ){
        int srcId = delta_source_rid(rid);
        if( srcId ){
          @ (%z(href("%R/deltachain/%d",rid))%d(rid)&larr;%d(srcId)</a>)
        }else{
          @ (%z(href("%R/deltachain/%d",rid))%d(rid)</a>)
        }
      }
    }
    if( zType[0]!='c' ){
      /* Comments for anything other than a check-in are generated by
      ** "fossil rebuild" and expect to be rendered as text/x-fossil-wiki */
      if( zType[0]=='w' ){
        const char *zCom = blob_str(&comment);
        /* Except, the comments generated by "fossil rebuild" for a wiki
        ** page edit consist of a single character '-', '+', or ':' (to
        ** indicate "deleted", "added", or "edited") followed by the
        ** raw wiki page name.  We have to generate an appropriate
        ** comment on-the-fly
        */
        wiki_hyperlink_override(zUuid);
        if( zCom[0]=='-' ){
          @ Deleted wiki page "%z(href("%R/whistory?name=%t",zCom+1))\
          @ %h(zCom+1)</a>"
        }else if( (tmFlags & TIMELINE_REFS)!=0
               && (zCom[0]=='+' || zCom[0]==':') ){
          @ Wiki page "%z(href("%R/wiki?name=%t",zCom+1))%h(zCom+1)</a>"
        }else if( zCom[0]=='+' ){
          @ Added wiki page "%z(href("%R/wiki?name=%t",zCom+1))%h(zCom+1)</a>"
        }else if( zCom[0]==':' ){
          @ %z(href("%R/wdiff?id=%!S",zUuid))Changes</a> to wiki page
          @ "%z(href("%R/wiki?name=%t",zCom+1))%h(zCom+1)</a>"
        }else{
          /* Assume this is an attachment message. It _might_ also
          ** be a legacy-format wiki log entry, in which case it
          ** will simply be rendered in the older format. */
          wiki_convert(&comment, 0, WIKI_INLINE);
        }
        wiki_hyperlink_override(0);
      }else{
        wiki_convert(&comment, 0, WIKI_INLINE);
      }
    }else{
      if( bCommentGitStyle ){
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
        cgi_printf("%W",z);
      }else if( mxWikiLen>0 && (int)blob_size(&comment)>mxWikiLen ){
        Blob truncated;
        blob_zero(&truncated);
        blob_append(&truncated, blob_buffer(&comment), mxWikiLen);
        blob_append(&truncated, "...", 3);
        @ %W(blob_str(&truncated))
        blob_reset(&truncated);
        drawDetailEllipsis = 0;
      }else{
        cgi_printf("%W",blob_str(&comment));
      }
    }
 
    if( zType[0]=='c' && strcmp(zUuid, MANIFEST_UUID)==0 ){
      /* This will only ever happen when Fossil is drawing a timeline for
      ** its own self-host repository.  If the timeline shows the specific
      ** check-in corresponding to the current executable, then tag that
      ** check-in with "This is me!". */
      @ <b>&larr; This is me!</b>
    }

    @ </span>
    blob_reset(&comment);

    /* Generate extra information and hyperlinks to follow the comment.
    ** Example:  "(check-in: [abcdefg], user: drh, tags: trunk)"
    */
    if( drawDetailEllipsis ){
      @ <span class='timelineEllipsis' id='ellipsis-%d(rid)' \
      @ data-id='%d(rid)'>...</span>
    }
    if( tmFlags & TIMELINE_COLUMNAR ){
      if( !isSelectedOrCurrent ){
        @ <td class="timelineDetailCell%s(zExtraClass)" id='md%d(gidx)'>
      }else{
        @ <td class="timelineDetailCell%s(zExtraClass)">
      }
    }
    if( tmFlags & TIMELINE_COMPACT ){
      cgi_printf("<span class='clutter' id='detail-%d'>",rid);
    }
    cgi_printf("<span class='timeline%sDetail'>", zStyle);
    if( (tmFlags & (TIMELINE_CLASSIC|TIMELINE_VERBOSE|TIMELINE_COMPACT))!=0 ){
      cgi_printf("(");
    }

    if( (tmFlags & TIMELINE_CLASSIC)==0 ){
      if( zType[0]=='c' ){
        if( isLeaf ){
          if( has_closed_tag(rid) ){
            @ <span class='timelineLeaf'>Closed-Leaf</span>
          }else{
            @ <span class='timelineLeaf'>Leaf</span>
          }
        }
        cgi_printf("check-in:&nbsp;%z%S</a> ",href("%R/info/%!S",zUuid),zUuid);
      }else if( zType[0]=='e' && tagid ){
        cgi_printf("technote:&nbsp;");
        hyperlink_to_event_tagid(tagid<0?-tagid:tagid);
      }else{
        cgi_printf("artifact:&nbsp;%z%S</a> ",href("%R/info/%!S",zUuid),zUuid);
      }
    }else if( zType[0]=='g' || zType[0]=='w' || zType[0]=='t'
              || zType[0]=='n' || zType[0]=='f'){
      cgi_printf("artifact:&nbsp;%z%S</a> ",href("%R/info/%!S",zUuid),zUuid);
    }

    if( g.perm.Hyperlink && fossil_strcmp(zDispUser, zThisUser)!=0 ){
      char *zLink;
      if( zType[0]!='f' || (tmFlags & TIMELINE_FORUMTXT)==0 ){
        zLink = mprintf("%R/timeline?u=%h&c=%t&y=a", zDispUser, zDate);
      }else{
        zLink = mprintf("%R/timeline?u=%h&c=%t&y=a&vfx", zDispUser, zDate);
      }
      cgi_printf("user:&nbsp;%z%h</a>", href("%z",zLink), zDispUser);
    }else{
      cgi_printf("user:&nbsp;%h", zDispUser);
    }

    /* Generate the "tags: TAGLIST" at the end of the comment, together
    ** with hyperlinks to the tag list.
    */
    if( zTagList && zTagList[0]==0 ) zTagList = 0;
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
                  href("%R/timeline?r=%#t&c=%t",i,z,zDate), i,z, &z[i]
            );
          }else{
            blob_appendf(&links, "%#h", i+2, z);
          }
          if( z[i]==0 ) break;
          z += i+2;
        }
        cgi_printf(" tags:&nbsp;%s", blob_str(&links));
        blob_reset(&links);
      }else{
        cgi_printf(" tags:&nbsp;%h", zTagList);
      }
    }

    if( tmFlags & TIMELINE_SHOWRID ){
      int srcId = delta_source_rid(rid);
      if( srcId ){
        cgi_printf(" id:&nbsp;%z%d&larr;%d</a>",
                   href("%R/deltachain/%d",rid), rid, srcId);
      }else{
        cgi_printf(" id:&nbsp;%z%d</a>",
                   href("%R/deltachain/%d",rid), rid);
      }
    }
    tag_private_status(rid);
    if( xExtra ){
      xExtra(rid);
    }
    /* End timelineDetail */
    if( (tmFlags & (TIMELINE_CLASSIC|TIMELINE_VERBOSE|TIMELINE_COMPACT))!=0 ){
      cgi_printf(")");
    }
    if( tmFlags & TIMELINE_COMPACT ){
      @ </span></span>
    }else{
      @ </span>
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
        char *zId;
        if( !inUl ){
          @ <ul class="filelist">
          inUl = 1;
        }
        if( tmFlags & TIMELINE_SHOWRID ){
          int srcId = delta_source_rid(fid);
          if( srcId ){
            zId = mprintf(" (%z%d&larr;%d</a>) ",
                          href("%R/deltachain/%d", fid), fid, srcId);
          }else{
            zId = mprintf(" (%z%d</a>) ",
                          href("%R/deltachain/%d", fid), fid);
          }
        }else{
          zId = fossil_strdup("");
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
          @ %z(href("%R/fdiff?v1=%!S&v2=%!S",zOld,zNew))[diff]</a></li>
        }
        fossil_free(zA);
        fossil_free(zId);
      }
      db_reset(&fchngQuery);
      if( inUl ){
        @ </ul>
      }
    }

    /* Show the complete text of forum messages */
    if( (tmFlags & (TIMELINE_FORUMTXT))!=0
     && zType[0]=='f' && g.perm.Hyperlink
     && (!content_is_private(rid) || g.perm.ModForum)
    ){
      Manifest *pPost = manifest_get(rid, CFTYPE_FORUM, 0);
      if( pPost ){
        const char *zClass = "forumTimeline";
        if( forum_rid_has_been_edited(rid) ){
          zClass = "forumTimeline forumObs";
        }
        forum_render(0, pPost->zMimetype, pPost->zWiki, zClass, 1);
        manifest_destroy(pPost);
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
    graph_finish(pGraph, pLeftBranch, tmFlags);
    if( pGraph->nErr ){
      graph_free(pGraph);
      pGraph = 0;
    }else{
      @ <tr class="timelineBottom" id="btm-%d(iTableId)">\
      @ <td></td><td></td><td></td></tr>
    }
  }
  @ </table>
  if( fchngQueryInit ) db_finalize(&fchngQuery);
  timeline_output_graph_javascript(pGraph, tmFlags, iTableId);
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
    if( mx>t ){
      for(i=0; i<3; i++){
        x[i] = x[i]>=mx-t ? x[i] - (mx-t) : 0;
      }
    }
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
  int tmFlags,              /* Flags that control rendering */
  int iTableId              /* Which graph is this for */
){
  if( pGraph && pGraph->nErr==0 ){
    GraphRow *pRow;
    int i;
    char cSep;
    int iRailPitch;      /* Pixels between consecutive rails */
    int showArrowheads;  /* True to draw arrowheads.  False to omit. */
    int circleNodes;     /* True for circle nodes.  False for square nodes */
    int colorGraph;      /* Use colors for graph lines */
    int iTopRow;         /* Index of the top row of the graph */
    int fileDiff;        /* True for file diff.  False for check-in diff */
    int omitDescenders;  /* True to omit descenders */
    int scrollToSelect;  /* True to scroll to the selection */
    int dwellTimeout;    /* Milliseconds to wait for tooltips to show */
    int closeTimeout;    /* Milliseconds to wait for tooltips to close */
    u8 *aiMap;           /* The rail map */

    iRailPitch = atoi(PD("railpitch","0"));
    showArrowheads = skin_detail_boolean("timeline-arrowheads");
    circleNodes = skin_detail_boolean("timeline-circle-nodes");
    colorGraph = skin_detail_boolean("timeline-color-graph-lines");
    iTopRow = pGraph->pFirst ? pGraph->pFirst->idx : 0;
    omitDescenders = (tmFlags & TIMELINE_DISJOINT)!=0;
    fileDiff = (tmFlags & TIMELINE_FILEDIFF)!=0;
    scrollToSelect = (tmFlags & TIMELINE_NOSCROLL)==0;
    dwellTimeout = atoi(db_get("timeline-dwelltime","100"));
    closeTimeout = atoi(db_get("timeline-closetime","250"));
    @ <script id='timeline-data-%d(iTableId)' type='application/json'>{
    @   "iTableId": %d(iTableId),
    @   "circleNodes": %d(circleNodes),
    @   "showArrowheads": %d(showArrowheads),
    @   "iRailPitch": %d(iRailPitch),
    @   "colorGraph": %d(colorGraph),
    @   "nomo": %d(PB("nomo")),
    @   "iTopRow": %d(iTopRow),
    @   "omitDescenders": %d(omitDescenders),
    @   "fileDiff": %d(fileDiff),
    @   "scrollToSelect": %d(scrollToSelect),
    @   "nrail": %d(pGraph->mxRail+1),
    @   "baseUrl": "%R",
    @   "dwellTimeout": %d(dwellTimeout),
    @   "closeTimeout": %d(closeTimeout),
    @   "hashDigits": %d(hash_digits(1)),
    @   "bottomRowId": "btm-%d(iTableId)",
    if( pGraph->nRow==0 ){
      @   "rowinfo": null
    }else{
      @   "rowinfo": [
    }

    /* the rowinfo[] array contains all the information needed to generate
    ** the graph.  Each entry contains information for a single row:
    **
    **   id:  The id of the <div> element for the row. This is an integer.
    **        to get an actual id, prepend "m" to the integer.  The top node
    **        is iTopRow and numbers increase moving down the timeline.
    **   bg:  The background color for this row
    **    r:  The "rail" that the node for this row sits on.  The left-most
    **        rail is 0 and the number increases to the right.
    **    d:  If exists and true then there is a "descender" - an arrow
    **        coming from the bottom of the page or further down on the page
    **        straight up to this node.
    **   mo:  "merge-out".  If it exists, this is the rail position
    **        for the upward portion of a merge arrow.  The merge arrow goes as
    **        a solid normal merge line up to the row identified by "mu" and
    **        then as a dashed cherrypick merge line up further to "cu".
    **        If this value is omitted if there are no merge children.
    **   mu:  The id of the row which is the top of the merge-out arrow.
    **        Only exists if "mo" exists.
    **   cu:  Extend the mu merge arrow up to this row as a cherrypick
    **        merge line, if this value exists.
    **    u:  Draw a thick child-line out of the top of this node and up to
    **        the node with an id equal to this value.  0 if it is straight to
    **        the top of the page or just up a little ways, -1 if there is
    **        no thick-line riser (if the node is a leaf).
    **   sb:  Draw a dotted child-line out of the top of this node up to the
    **        node with the id equal to the value.  This is like "u" except
    **        that the line is dotted instead of solid and has no arrow.
    **        Mnemonic: "Same Branch".
    **    f:  0x01: a leaf node, 0x02: a closed leaf node.
    **   au:  An array of integers that define thick-line risers for branches.
    **        The integers are in pairs.  For each pair, the first integer is
    **        is the rail on which the riser should run and the second integer
    **        is the id of the node upto which the riser should run. If there
    **        are no risers, this array does not exist.
    **   mi:  "merge-in".  An array of integer rail positions from which
    **        merge arrows should be drawn into this node.  If the value is
    **        negative, then the rail position is -1-mi[] and a thin merge-arrow
    **        descender is drawn to the bottom of the screen. This array is
    **        omitted if there are no inbound merges.
    **   ci:  "cherrypick-in". Like "mi" except for cherrypick merges.
    **        omitted if there are no cherrypick merges.
    **    h:  The artifact hash of the object being graphed
    *    br:  The branch to which the artifact belongs
    */
    aiMap = pGraph->aiRailMap;
    for(pRow=pGraph->pFirst; pRow; pRow=pRow->pNext){
      int k = 0;
      cgi_printf("{\"id\":%d,",     pRow->idx);
      cgi_printf("\"bg\":\"%s\",",  pRow->zBgClr);
      cgi_printf("\"r\":%d,",       pRow->iRail>=0 ? aiMap[pRow->iRail] : -1);
      if( pRow->bDescender ){
        cgi_printf("\"d\":%d,",       pRow->bDescender);
      }
      if( pRow->mergeOut>=0 ){
        cgi_printf("\"mo\":%d,",      aiMap[pRow->mergeOut]);
        if( pRow->mergeUpto==0 ) pRow->mergeUpto = pRow->idx;
        cgi_printf("\"mu\":%d,",      pRow->mergeUpto);
        if( pRow->cherrypickUpto>0 && pRow->cherrypickUpto<=pRow->mergeUpto ){
          cgi_printf("\"cu\":%d,",    pRow->cherrypickUpto);
        }
      }
      if( pRow->isStepParent ){
        cgi_printf("\"sb\":%d,",      pRow->aiRiser[pRow->iRail]);
      }else{
        cgi_printf("\"u\":%d,",       pRow->aiRiser[pRow->iRail]);
      }
      k = 0;
      if( pRow->isLeaf ) k |= 1;
      if( pRow->isLeaf & 2) k |= 2;
      cgi_printf("\"f\":%d,",k);
      for(i=k=0; i<GR_MAX_RAIL; i++){
        if( i==pRow->iRail ) continue;
        if( pRow->aiRiser[i]>0 ){
          if( k==0 ){
            cgi_printf("\"au\":");
            cSep = '[';
          }
          k++;
          cgi_printf("%c%d,%d", cSep, aiMap[i], pRow->aiRiser[i]);
          cSep = ',';
        }
      }
      if( k ){
        cgi_printf("],");
      }
      if( colorGraph && pRow->zBgClr[0]=='#' ){
        cgi_printf("\"fg\":\"%s\",", bg_to_fg(pRow->zBgClr));
      }
      /* mi */
      for(i=k=0; i<GR_MAX_RAIL; i++){
        if( pRow->mergeIn[i]==1 ){
          int mi = aiMap[i];
          if( (pRow->mergeDown >> i) & 1 ) mi = -1-mi;
          if( k==0 ){
            cgi_printf("\"mi\":");
            cSep = '[';
          }
          k++;
          cgi_printf("%c%d", cSep, mi);
          cSep = ',';
        }
      }
      if( k ) cgi_printf("],");
      /* ci */
      for(i=k=0; i<GR_MAX_RAIL; i++){
        if( pRow->mergeIn[i]==2 ){
          int mi = aiMap[i];
          if( (pRow->cherrypickDown >> i) & 1 ) mi = -mi;
          if( k==0 ){
            cgi_printf("\"ci\":");
            cSep = '[';
          }
          k++;
          cgi_printf("%c%d", cSep, mi);
          cSep = ',';
        }
      }
      if( k ) cgi_printf("],");
      cgi_printf("\"br\":\"%j\",", pRow->zBranch ? pRow->zBranch : "");
      cgi_printf("\"h\":\"%!S\"}%s",
                 pRow->zUuid, pRow->pNext ? ",\n" : "]\n");
    }
    @ }</script>
    builtin_request_js("graph.js");
    builtin_request_js("copybtn.js"); /* Required by graph.js */
    graph_free(pGraph);
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
          href("%R/finfo?name=%t&m=%!S", zFN, zUuid), zFN);
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
  static const char *az[16];
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
      az[i++] = "n";
      az[i++] = "New Tickets";
    }
    if( g.perm.RdWiki ){
      az[i++] = "w";
      az[i++] = "Wiki";
    }
    if( g.perm.RdForum ){
      az[i++] = "f";
      az[i++] = "Forum";
    }
    assert( i<=count(az) );
  }
  if( i>2 ){
    style_submenu_multichoice("y", i/2, az, isDisabled);
  }
}

/*
** Return the default value for the "ss" cookie or query parameter.
** The "ss" cookie determines the graph style.  See the
** timeline_view_styles[] global constant for a list of choices.
*/
const char *timeline_default_ss(void){
  static const char *zSs = 0;
  if( zSs==0 ) zSs = db_get("timeline-default-style","m");
  return zSs;
}

/*
** Convert the current "ss" display preferences cookie into an
** appropriate TIMELINE_* flag
*/
int timeline_ss_cookie(void){
  int tmFlags;
  const char *v = cookie_value("ss",0);
  if( v==0 ) v = timeline_default_ss();
  switch( v[0] ){
    case 'c':  tmFlags = TIMELINE_COMPACT;  break;
    case 'v':  tmFlags = TIMELINE_VERBOSE;  break;
    case 'j':  tmFlags = TIMELINE_COLUMNAR; break;
    case 'x':  tmFlags = TIMELINE_CLASSIC;  break;
    default:   tmFlags = TIMELINE_MODERN;   break;
  }
  return tmFlags;
}

/* Available timeline display styles, together with their y= query
** parameter names.
*/
const char *const timeline_view_styles[] = {
  "m", "Modern View",
  "j", "Columnar View",
  "c", "Compact View",
  "v", "Verbose View",
  "x", "Classic View",
};
#if INTERFACE
# define N_TIMELINE_VIEW_STYLE 5
#endif

/*
** Add the select/option box to the timeline submenu that is used to
** set the ss= parameter that determines the viewing mode.
**
** Return the TIMELINE_* value appropriate for the view-style.
*/
int timeline_ss_submenu(void){
  cookie_link_parameter("ss","ss",timeline_default_ss());
  style_submenu_multichoice("ss",
              N_TIMELINE_VIEW_STYLE,
              timeline_view_styles, 0);
  return timeline_ss_cookie();
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
      "SELECT mlink.mid FROM mlink, filename\n"
      " WHERE mlink.fnid=filename.fnid\n"
      "   AND %s)",
      glob_expr("filename.name", mprintf("\"%s\"", zChng)));
}
static void addFileGlobDescription(
  const char *zChng,        /* The filename GLOB list */
  Blob *pDescription        /* Result description */
){
  if( zChng==0 || zChng[0]==0 ) return;
  blob_appendf(pDescription, " that include changes to files matching '%h'",
               zChng);
}

/*
** Similar to fossil_expand_datetime()
**
** Add missing "-" characters into a date/time.  Examples:
**
**       20190419  =>  2019-04-19
**       201904    =>  2019-04
*/
const char *timeline_expand_datetime(const char *zIn, int *pbZulu){
  static char zEDate[16];
  int n = (int)strlen(zIn);
  int i, j;

  /* These forms are recognized:
  **
  **   (1)  YYYYMMDD
  **   (2)  YYYYMM
  **   (3)  YYYYWW
  */
  if( n && (zIn[n-1]=='Z' || zIn[n-1]=='z') ){
    n--;
    if( pbZulu ) *pbZulu = 1;
  }else{
    if( pbZulu ) *pbZulu = 0;
  }
  if( n!=8 && n!=6 ) return zIn;

  /* Every character must be a digit */
  for(i=0; i<n && fossil_isdigit(zIn[i]); i++){}
  if( i!=n ) return zIn;

  /* Expand the date */
  for(i=j=0; i<n; i++){
    if( j==4 || j==7 ) zEDate[j++] = '-';
    zEDate[j++] = zIn[i];
  }
  zEDate[j] = 0;

  /* It looks like this may be a date.  Return it with punctuation added. */
  return zEDate;
}

/*
** Check to see if the argument is a date-span for the ymd= query
** parameter.  A valid date-span is of the form:
**
**       0123456789 123456  <-- index
**       YYYYMMDD-YYYYMMDD
**
** with an optional "Z" timeline modifier at the end.  Return true if
** the input is a valid date space and false if not.
*/
static int timeline_is_datespan(const char *zDay){
  size_t n = strlen(zDay);
  int i, d, m;
  
  if( n<17 || n>18 ) return 0;
  if( n==18 ){
    if( zDay[17]!='Z' && zDay[17]!='z' ) return 0;
    n--;
  }
  if( zDay[8]!='-' ) return 0;
  for(i=0; i<17 && (fossil_isdigit(zDay[i]) || i==8); i++){}
  if( i!=17 ) return 0;
  i = atoi(zDay);
  d = i%100;
  if( d<1 || d>31 ) return 0;
  m = (i/100)%100;
  if( m<1 || m>12 ) return 0;
  i = atoi(zDay+9);
  d = i%100;
  if( d<1 || d>31 ) return 0;
  m = (i/100)%100;
  if( m<1 || m>12 ) return 0;
  return 1;
}

/*
** Find the first check-in encountered with a particular tag
** when moving either forwards are backwards in time from a
** particular starting point (iFrom).  Return the rid of that
** first check-in.  If there are no check-ins in the decendent
** or ancestor set of check-in iFrom that match the tag, then
** return 0.
*/
static int timeline_endpoint(
  int iFrom,         /* Starting point */
  const char *zEnd,  /* Tag we are searching for */   
  int bForward       /* 1: forwards in time (descendants) 0: backwards */
){
  int tagId;
  int endId = 0;
  Stmt q;
  int ans = 0;

  tagId = db_int(0, "SELECT tagid FROM tag WHERE tagname='sym-%q'", zEnd);
  if( tagId==0 ){
    endId = symbolic_name_to_rid(zEnd, "ci");
    if( endId==0 ) return 0;
  }
  db_pause_dml_log();
  if( bForward ){
    if( tagId ){
      db_prepare(&q,
        "WITH RECURSIVE dx(id,mtime) AS ("
        "  SELECT %d, event.mtime FROM event WHERE objid=%d"
        "  UNION"
        "  SELECT plink.cid, plink.mtime"
        "    FROM dx, plink"
        "   WHERE plink.pid=dx.id"
        "     AND plink.mtime<=(SELECT max(event.mtime) FROM tagxref, event"
                               " WHERE tagxref.tagid=%d AND tagxref.tagtype>0"
                               " AND event.objid=tagxref.rid)"
        "   ORDER BY plink.mtime)"
        "SELECT id FROM dx, tagxref"
        " WHERE tagid=%d AND tagtype>0 AND rid=id"
        " ORDER BY dx.mtime LIMIT 1",
        iFrom, iFrom, tagId, tagId
      );
    }else{
      db_prepare(&q,
        "WITH RECURSIVE dx(id,mtime) AS ("
        "  SELECT %d, event.mtime FROM event WHERE objid=%d"
        "  UNION"
        "  SELECT plink.cid, plink.mtime"
        "    FROM dx, plink"
        "   WHERE plink.pid=dx.id"
        "     AND plink.mtime<=(SELECT mtime FROM event WHERE objid=%d)"
        "   ORDER BY plink.mtime)"
        "SELECT id FROM dx WHERE id=%d",
        iFrom, iFrom, endId, endId
      );
    }
  }else{
    if( tagId ){
      db_prepare(&q,
        "WITH RECURSIVE dx(id,mtime) AS ("
        "  SELECT %d, event.mtime FROM event WHERE objid=%d"
        "  UNION"
        "  SELECT plink.pid, event.mtime"
        "    FROM dx, plink, event"
        "   WHERE plink.cid=dx.id AND event.objid=plink.pid"
        "     AND event.mtime>=(SELECT min(event.mtime) FROM tagxref, event"
                               " WHERE tagxref.tagid=%d AND tagxref.tagtype>0"
                               " AND event.objid=tagxref.rid)"
        "   ORDER BY event.mtime DESC)"
        "SELECT id FROM dx, tagxref"
        " WHERE tagid=%d AND tagtype>0 AND rid=id"
        " ORDER BY dx.mtime DESC LIMIT 1",
        iFrom, iFrom, tagId, tagId
      );
    }else{
      db_prepare(&q,
        "WITH RECURSIVE dx(id,mtime) AS ("
        "  SELECT %d, event.mtime FROM event WHERE objid=%d"
        "  UNION"
        "  SELECT plink.pid, event.mtime"
        "    FROM dx, plink, event"
        "   WHERE plink.cid=dx.id AND event.objid=plink.pid"
        "     AND event.mtime>=(SELECT mtime FROM event WHERE objid=%d)"
        "   ORDER BY event.mtime DESC)"
        "SELECT id FROM dx WHERE id=%d",
        iFrom, iFrom, endId, endId
      );
    }
  }
  if( db_step(&q)==SQLITE_ROW ){
    ans = db_column_int(&q, 0);
  }
  db_finalize(&q);
  db_unpause_dml_log();
  return ans;
}

/*
** Add to the (temp) table zTab, RID values for every check-in
** identifier found on the zExtra string.  Check-in names can be separated
** by commas or by whitespace.
*/
static void add_extra_rids(const char *zTab, const char *zExtra){
  int ii;
  int rid;
  int cnt;
  Blob sql;
  char *zX;
  char *zToDel;
  if( zExtra==0 ) return;
  cnt = 0;
  blob_init(&sql, 0, 0);
  zX = zToDel = fossil_strdup(zExtra);
  blob_append_sql(&sql, "INSERT OR IGNORE INTO \"%w\" VALUES", zTab);
  while( zX[0] ){
    char c;
    if( zX[0]==',' || zX[0]==' ' ){ zX++; continue; }
    for(ii=1; zX[ii] && zX[ii]!=',' && zX[ii]!=' '; ii++){}
    c = zX[ii];
    zX[ii] = 0;
    rid = name_to_rid(zX);
    if( rid>0 ){
      if( (cnt%10)==4 ){
        blob_append_sql(&sql,",\n ");
      }else if( cnt>0 ){
        blob_append_sql(&sql,",");
      }
      blob_append_sql(&sql, "(%d)", rid);
      cnt++;
    }
    zX[ii] = c;
    zX += ii;
  }
  if( cnt ) db_exec_sql(blob_sql_text(&sql));
  blob_reset(&sql);
  fossil_free(zToDel);
}

/*
** COMMAND: test-endpoint
**
** Usage: fossil test-endpoint BASE TAG ?OPTIONS?
**
** Show the first check-in with TAG that is a descendant or ancestor
** of BASE.  The first descendant checkin is shown by default.  Use
** the --backto to see the first ancestor checkin.
**
** Options:
**
**      --backto            Show ancestor.  Others defaults to descendants.
*/
void timeline_test_endpoint(void){
  int bForward = find_option("backto",0,0)==0;
  int from_rid;
  int ans;
  db_find_and_open_repository(0, 0);
  verify_all_options();
  if( g.argc!=4 ){
    usage("BASE-CHECKIN TAG ?--backto?");
  }
  from_rid = symbolic_name_to_rid(g.argv[2],"ci");
  ans = timeline_endpoint(from_rid, g.argv[3], bForward);
  if( ans ){
    fossil_print("Result: %d (%S)\n", ans, rid_to_uuid(ans));
  }else{
    fossil_print("No path found\n");
  }
}


/*
** WEBPAGE: timeline
**
** Query parameters:
**
**    a=TIMEORTAG     Show events after TIMEORTAG.
**    b=TIMEORTAG     Show events before TIMEORTAG.
**    c=TIMEORTAG     Show events that happen "circa" TIMEORTAG
**    cf=FILEHASH     Show events around the time of the first use of
**                    the file with FILEHASH.
**    m=TIMEORTAG     Highlight the event at TIMEORTAG, or the closest available
**                    event if TIMEORTAG is not part of the timeline.  If
**                    the t= or r= is used, the m event is added to the timeline
**                    if it isn't there already.
**    x=LIST          Show check-ins in the comma- or space-separated LIST
**                    in addition to check-ins specified by other parameters.
**    sel1=TIMEORTAG  Highlight the check-in at TIMEORTAG if it is part of
**                    the timeline.  Similar to m= except TIMEORTAG must
**                    match a check-in that is already in the timeline.
**    sel2=TIMEORTAG  Like sel1= but use the secondary highlight.
**    n=COUNT         Maximum number of events. "all" for no limit
**    n1=COUNT        Same as "n" but doesn't set the display-preference cookie
**                       Use "n1=COUNT" for a one-time display change
**    p=CHECKIN       Parents and ancestors of CHECKIN
**                       bt=PRIOR   ... going back to PRIOR
**                       p2=CKIN2   ... use CKIN2 if CHECKIN is not found
**    d=CHECKIN       Children and descendants of CHECKIN
**                       d2=CKIN2        ... Use CKIN2 if CHECKIN is not found
**                       ft=DESCENDANT   ... going forward to DESCENDANT
**    dp=CHECKIN      Same as 'd=CHECKIN&p=CHECKIN'
**    dp2=CKIN2       Same as 'd2=CKIN2&p2=CKIN2'
**    df=CHECKIN      Same as 'd=CHECKIN&n1=all&nd'.  Mnemonic: "Derived From"
**    bt=CHECKIN      "Back To".  Show ancenstors going back to CHECKIN
**                       p=CX       ... from CX back to time of CHECKIN
**                       from=CX    ... path from CX back to CHECKIN
**    ft=CHECKIN      "Forward To":  Show decendents forward to CHECKIN
**                       d=CX       ... from CX up to the time of CHECKIN
**                       from=CX    ... path from CX up to CHECKIN
**    t=TAG           Show only check-ins with the given TAG
**    r=TAG           Same as 't=TAG&rel'.  Mnemonic: "Related"
**    tl=TAGLIST      Same as 't=TAGLIST&ms=brlist'.  Mnemonic: "Tag List"
**    rl=TAGLIST      Same as 'r=TAGLIST&ms=brlist'.  Mnemonic: "Related List"
**    ml=TAGLIST      Same as 'tl=TAGLIST&mionly'.  Mnemonic: "Merge-in List"
**    sl=TAGLIST      "Sort List". Draw TAGLIST branches ordered left to right.
**    rel             Show related check-ins as well as those matching t=TAG
**    mionly          Show related parents but not related children.
**    nowiki          Do not show wiki associated with branch or tag
**    ms=MATCHSTYLE   Set tag name match algorithm.  One of "exact", "glob",
**                    "like", or "regexp".
**    u=USER          Only show items associated with USER
**    y=TYPE          'ci', 'w', 't', 'n', 'e', 'f', or 'all'.
**    ss=VIEWSTYLE    c: "Compact", v: "Verbose", m: "Modern", j: "Columnar",
*                     x: "Classic".
**    advm            Use the "Advanced" or "Busy" menu design.
**    ng              No Graph.
**    ncp             Omit cherrypick merges
**    nd              Do not highlight the focus check-in
**    nsm             Omit the submenu
**    nc              Omit all graph colors other than highlights
**    v               Show details of files changed
**    vfx             Show complete text of forum messages
**    f=CHECKIN       Family (immediate parents and children) of CHECKIN
**    from=CHECKIN    Path through common ancestor from CHECKIN...
**                       to=CHECKIN   ... to this
**                       to2=CHECKIN  ... backup name if to= doesn't resolve
**                       shortest     ... pick path with least number of nodes
**                       rel          ... also show related checkins
**                       min          ... hide long sequences along same branch
**                       bt=PRIOR     ... path from CHECKIN back to PRIOR
**                       ft=LATER     ... path from CHECKIN forward to LATER
**    me=CHECKIN      Most direct path from CHECKIN...
**                       you=CHECKIN  ... to this
**                       rel          ... also show related checkins
**    uf=FILE_HASH    Show only check-ins that contain the given file version
**                    All qualifying check-ins are shown unless there is
**                    also an n= or n1= query parameter.
**    chng=GLOBLIST   Show only check-ins that involve changes to a file whose
**                    name matches one of the comma-separate GLOBLIST
**    brbg            Background color determined by branch name
**    ubg             Background color determined by user
**    deltabg         Background color red for delta manifests or green
**                    for baseline manifests
**    namechng        Show only check-ins that have filename changes
**    forks           Show only forks and their children
**    cherrypicks     Show all cherrypicks
**    ym=YYYYMM       Show only events for the given year/month
**    yw=YYYYWW       Show only events for the given week of the given year
**    yw=YYYYMMDD     Show events for the week that includes the given day
**    ymd=YYYYMMDD    Show only events on the given day. The use "ymd=now"
**                    to see all changes for the current week.  Add "z" at end
**                    to divide days at UTC instead of localtime days.
**                    Use ymd=YYYYMMDD-YYYYMMDD (with optional "z") for a range.
**    year=YYYY       Show only events on the given year. The use "year=0"
**                    to see all changes for the current year.
**    days=N          Show events over the previous N days
**    datefmt=N       Override the date format:  0=HH:MM, 1=HH:MM:SS,
**                    2=YYYY-MM-DD HH:MM:SS, 3=YYMMDD HH:MM, and 4 means "off".
**    bisect          Show the check-ins that are in the current bisect
**    oldestfirst     Show events oldest first.
**    showid          Show RIDs
**    showsql         Show the SQL used to generate the report
**
** p= and d= can appear individually or together.  If either p= or d=
** appear, then u=, y=, a=, and b= are ignored.
**
** If both a= and b= appear then both upper and lower bounds are honored.
**
** When multiple time-related filters are used, e.g. ym, yw, and ymd,
** which one(s) is/are applied is unspecified and may change between
** fossil versions.
**
** CHECKIN or TIMEORTAG can be a check-in hash prefix, or a tag, or the
** name of a branch.
*/
void page_timeline(void){
  Stmt q;                            /* Query used to generate the timeline */
  Blob sql;                          /* text of SQL used to generate timeline */
  Blob desc;                         /* Description of the timeline */
  int nEntry;                        /* Max number of entries on timeline */
  int p_rid;                         /* artifact p and its parents */
  int d_rid;                         /* artifact d and descendants */
  int f_rid;                         /* artifact f and close family */
  const char *zUser = P("u");        /* All entries by this user if not NULL */
  const char *zType;                 /* Type of events to display */
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
  char *zYearWeekStart = 0;          /* YYYY-MM-DD for start of YYYY-WW */
  const char *zDay = P("ymd");       /* Check-ins for the day YYYY-MM-DD */
  const char *zYear = P("year");     /* Events for the year YYYY */
  const char *zNDays = P("days");    /* Show events over the previous N days */
  int nDays = 0;                     /* Numeric value for zNDays */
  const char *zChng = P("chng");     /* List of GLOBs for files that changed */
  int useDividers = P("nd")==0;      /* Show dividers if "nd" is missing */
  int renameOnly = P("namechng")!=0; /* Show only check-ins that rename files */
  int forkOnly = PB("forks");        /* Show only forks and their children */
  int bisectLocal = PB("bisect");    /* Show the check-ins of the bisect */
  const char *zBisect = P("bid");    /* Bisect description */
  int cpOnly = PB("cherrypicks");    /* Show all cherrypick checkins */
  int tmFlags = 0;                   /* Timeline flags */
  const char *zThisTag = 0;          /* Suppress links to this tag */
  const char *zThisUser = 0;         /* Suppress links to this user */
  HQuery url;                        /* URL for various branch links */
  int from_rid = name_to_typed_rid(P("from"),"ci"); /* from= for paths */
  const char *zTo2 = 0;
  int to_rid = name_choice("to","to2",&zTo2);    /* to= for path timelines */
  int bShort = P("shortest")!=0;                 /* shortest possible path */
  int me_rid = name_to_typed_rid(P("me"),"ci");  /* me= for common ancestory */
  int you_rid = name_to_typed_rid(P("you"),"ci");/* you= for common ancst */
  int pd_rid;
  const char *zDPNameP, *zDPNameD;    /* Value of p=, d=, or dp= params */
  double rBefore, rAfter, rCirca;     /* Boundary times */
  const char *z;
  char *zOlderButton = 0;             /* URL for Older button at the bottom */
  char *zOlderButtonLabel = 0;        /* Label for the Older Button */
  char *zNewerButton = 0;             /* URL for Newer button at the top */
  char *zNewerButtonLabel = 0;        /* Label for the Newer button */
  int selectedRid = 0;                /* Show a highlight on this RID */
  int secondaryRid = 0;               /* Show secondary highlight */
  int disableY = 0;                   /* Disable type selector on submenu */
  int advancedMenu = 0;               /* Use the advanced menu design */
  char *zPlural;                      /* Ending for plural forms */
  int showCherrypicks = 1;            /* True to show cherrypick merges */
  int haveParameterN;                 /* True if n= query parameter present */
  int from_to_mode = 0;               /* 0: from,to. 1: from,ft 2: from,bt */
  int showSql = PB("showsql");        /* True to show the SQL */
  Blob allSql;                        /* Copy of all SQL text */
  int bMin = P("min")!=0;             /* True if "min" query parameter used */

  login_check_credentials();
  url_initialize(&url, "timeline");
  cgi_query_parameters_to_url(&url);
  blob_init(&allSql, 0, 0);

  /* The "mionly" query parameter is like "rel", but shows merge-ins only */
  if( P("mionly")!=0 ) related = 2;

  (void)P_NoBot("ss")
    /* "ss" is processed via the udc but at least one spider likes to
    ** try to SQL inject via this argument, so let's catch that. */;

  /* Set number of rows to display */
  z = P("n");
  if( z!=0 ){
    haveParameterN = 1;
    cookie_write_parameter("n","n",0);
  }else{
    const char *z2;
    haveParameterN = 0;
    cookie_read_parameter("n","n");
    z = P("n");
    if( z==0 ){
      z = db_get("timeline-default-length",0);
    }
    cgi_replace_query_parameter("n",fossil_strdup(z));
    cookie_write_parameter("n","n",0);
    z2 = P("n1");
    if( z2 ){
      haveParameterN = 2;
      z = z2;
    }
  }
  if( z ){
    if( fossil_strcmp(z,"all")==0 ){
      nEntry = 0;
    }else{
      nEntry = atoi(z);
      if( nEntry<=0 ){
        z = "50";
        nEntry = 50;
      }
    }
  }else{
    nEntry = 50;
  }

  /* Query parameters d=, p=, and f= and variants */
  p_rid = name_choice("p","p2", &zDPNameP);
  d_rid = name_choice("d","d2", &zDPNameD);
  z = P("f");
  f_rid = z ? name_to_typed_rid(z,"ci") : 0;
  z = P("df");
  if( z && (d_rid = name_to_typed_rid(z,"ci"))!=0 ){
    nEntry = 0;
    useDividers = 0;
    cgi_replace_query_parameter("d",fossil_strdup(z));
    zDPNameD = zDPNameP = z;
  }

  /* Undocumented query parameter to set JS mode */
  builtin_set_js_delivery_mode(P("jsmode"),1);

  secondaryRid = name_to_typed_rid(P("sel2"),"ci");
  selectedRid = name_to_typed_rid(P("sel1"),"ci");
  if( from_rid!=0 && to_rid!=0 ){
    if( selectedRid==0 ) selectedRid = from_rid;
    if( secondaryRid==0 ) secondaryRid = to_rid;
  }
  tmFlags |= timeline_ss_submenu();
  cookie_link_parameter("advm","advm","0");
  advancedMenu = atoi(PD("advm","0"));

  /* Omit all cherry-pick merge lines if the "ncp" query parameter is
  ** present or if this repository lacks a "cherrypick" table. */
  if( PB("ncp") || !db_table_exists("repository","cherrypick") ){
    showCherrypicks = 0;
  }

  /* To view the timeline, must have permission to read project data.
  */
  pd_rid = name_choice("dp","dp2",&zDPNameP);
  if( pd_rid ){
    p_rid = d_rid = pd_rid;
    zDPNameD = zDPNameP;
  }
  if( (!g.perm.Read && !g.perm.RdTkt && !g.perm.RdWiki && !g.perm.RdForum)
   || (bisectLocal && !g.perm.Setup)
  ){
    login_needed(g.anon.Read && g.anon.RdTkt && g.anon.RdWiki);
    return;
  }
  if( !bisectLocal ){
    etag_check(ETAG_QUERY|ETAG_COOKIE|ETAG_DATA|ETAG_CONFIG, 0);
  }
  cookie_read_parameter("y","y");
  zType = P("y");
  if( zType==0 ){
    zType = g.perm.Read ? "ci" : "all";
    cgi_set_parameter("y", zType);
  }
  if( zType[0]=='a' ||
      ( g.perm.Read && zType[0]=='c' ) ||
      ( g.perm.RdTkt && (zType[0]=='t' || zType[0]=='n') ) ||
      ( g.perm.RdWiki && (zType[0]=='w' || zType[0]=='e') ) ||
      ( g.perm.RdForum && zType[0]=='f' )
    ){
    cookie_write_parameter("y","y",zType);
  }

  /* Convert the cf=FILEHASH query parameter into a c=CHECKINHASH value */
  if( P("cf")!=0 ){
    zCirca = db_text(0,
      "SELECT (SELECT uuid FROM blob WHERE rid=mlink.mid)"
      "  FROM mlink, event"
      " WHERE mlink.fid=(SELECT rid FROM blob WHERE uuid LIKE '%q%%')"
      "   AND event.objid=mlink.mid"
      " ORDER BY event.mtime LIMIT 1",
      P("cf")
    );
  }

  /* Check for tl=TAGLIST and rl=TAGLIST which are abbreviations for
  ** t=TAGLIST&ms=brlist and r=TAGLIST&ms=brlist repectively. */
  if( zBrName==0 && zTagName==0 ){
    const char *z;
    const char *zPattern = 0;
    if( (z = P("tl"))!=0 ){
      zPattern = zTagName = z;
    }else if( (z = P("rl"))!=0 ){
      zPattern = zBrName = z;
      if( related==0 ) related = 1;
    }else if( (z = P("ml"))!=0 ){
      zPattern = zBrName = z;
      if( related==0 ) related = 2;
    }
    if( zPattern!=0 && zMatchStyle==0 ){
      /* If there was no ms= query parameter, set the match style to
      ** "glob" if the pattern appears to contain GLOB character, or
      ** "brlist" if it does not. */
      if( strpbrk(zPattern,"*[?") ){
        zMatchStyle = "glob";
      }else{
        zMatchStyle = "brlist";
      }
    }
  }

  /* Convert r=TAG to t=TAG&rel in order to populate the UI style widgets. */
  if( zBrName ){
    cgi_delete_query_parameter("r");
    cgi_set_query_parameter("t", zBrName);  (void)P("t");
    cgi_set_query_parameter("rel", "1");
    zTagName = zBrName;
    if( related==0 ) related = 1;
    zType = "ci";
  }

  /* Ignore empty tag query strings. */
  if( zTagName && !*zTagName ){
    zTagName = 0;
  }

  /* Finish preliminary processing of tag match queries. */
  matchStyle = match_style(zMatchStyle, MS_EXACT);
  if( zTagName ){
    zType = "ci";
    if( matchStyle==MS_EXACT ){
      /* For exact maching, inhibit links to the selected tag. */
      zThisTag = zTagName;
      Th_StoreUnsafe("current_checkin", zTagName);
    }

    /* Display a checkbox to enable/disable display of related check-ins. */
    if( advancedMenu ){
      style_submenu_checkbox("rel", "Related", 0, 0);
    }

    /* Construct the tag match expression. */
    zTagSql = match_tag_sqlexpr(matchStyle, zTagName, &zMatchDesc, &zError);
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
    tmFlags |= TIMELINE_BRIEF | TIMELINE_GRAPH | TIMELINE_CHPICK;
  }else{
    tmFlags |= TIMELINE_GRAPH | TIMELINE_CHPICK;
  }
  if( related ){
    tmFlags |= TIMELINE_FILLGAPS | TIMELINE_XMERGE;
    tmFlags &= ~TIMELINE_DISJOINT;
  }
  if( PB("ncp") ){
    tmFlags &= ~TIMELINE_CHPICK;
  }
  if( PB("ng") || zSearch!=0 ){
    tmFlags &= ~(TIMELINE_GRAPH|TIMELINE_CHPICK);
  }
  if( PB("nsm") ){
    style_submenu_enable(0);
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
  if( PB("deltabg") ){
    tmFlags |= TIMELINE_DELTA;
  }
  if( PB("nc") ){
    tmFlags &= ~(TIMELINE_DELTA|TIMELINE_BRCOLOR|TIMELINE_UCOLOR);
    tmFlags |= TIMELINE_NOCOLOR;
  }
  if( showSql ) db_append_dml_to_blob(&allSql);
  if( zUses!=0 ){
    int ufid = db_int(0, "SELECT rid FROM blob WHERE uuid GLOB '%q*'", zUses);
    if( ufid ){
      zUses = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", ufid);
      db_multi_exec("CREATE TEMP TABLE usesfile(rid INTEGER PRIMARY KEY)");
      compute_uses_file("usesfile", ufid, 0);
      zType = "ci";
      disableY = 1;
      if( !haveParameterN ) nEntry = 0;
    }else{
      zUses = 0;
    }
  }
  if( renameOnly ){
    db_multi_exec(
      "CREATE TEMP TABLE rnfile(rid INTEGER PRIMARY KEY);"
      "INSERT OR IGNORE INTO rnfile"
      " SELECT mid FROM mlink WHERE pfnid>0 AND pfnid!=fnid;"
    );
    disableY = 1;
  }
  if( forkOnly ){
    db_multi_exec(
      "CREATE TEMP TABLE rnfork(rid INTEGER PRIMARY KEY);\n"
      "INSERT OR IGNORE INTO rnfork(rid)\n"
      "  SELECT pid FROM plink\n"
      "   WHERE (SELECT value FROM tagxref WHERE tagid=%d AND rid=cid)==\n"
      "         (SELECT value FROM tagxref WHERE tagid=%d AND rid=pid)\n"
      "   GROUP BY pid\n"
      "   HAVING count(*)>1;\n"
      "INSERT OR IGNORE INTO rnfork(rid)\n"
      "  SELECT cid FROM plink\n"
      "   WHERE (SELECT value FROM tagxref WHERE tagid=%d AND rid=cid)==\n"
      "         (SELECT value FROM tagxref WHERE tagid=%d AND rid=pid)\n"
      "   GROUP BY cid\n"
      "   HAVING count(*)>1;\n",
      TAG_BRANCH, TAG_BRANCH, TAG_BRANCH, TAG_BRANCH
    );
    db_multi_exec(
      "INSERT OR IGNORE INTO rnfork(rid)\n"
      "  SELECT cid FROM plink\n"
      "   WHERE pid IN rnfork\n"
      "     AND (SELECT value FROM tagxref WHERE tagid=%d AND rid=cid)==\n"
      "         (SELECT value FROM tagxref WHERE tagid=%d AND rid=pid)\n"
      "  UNION\n"
      "  SELECT pid FROM plink\n"
      "   WHERE cid IN rnfork\n"
      "     AND (SELECT value FROM tagxref WHERE tagid=%d AND rid=cid)==\n"
      "         (SELECT value FROM tagxref WHERE tagid=%d AND rid=pid)\n",
      TAG_BRANCH, TAG_BRANCH, TAG_BRANCH, TAG_BRANCH
    );
    tmFlags |= TIMELINE_UNHIDE;
    zType = "ci";
    disableY = 1;
  }
  if( bisectLocal && cgi_is_loopback(g.zIpAddr) && db_open_local(0) ){
    int iCurrent = db_lget_int("checkout",0);
    char *zPerm = bisect_permalink();
    bisect_create_bilog_table(iCurrent, 0, 1);
    tmFlags |= TIMELINE_UNHIDE | TIMELINE_BISECT | TIMELINE_FILLGAPS;
    zType = "ci";
    disableY = 1;
    style_submenu_element("Permalink", "%R/timeline?bid=%z", zPerm);
  }else{
    bisectLocal = 0;
  }
  if( zBisect!=0 && bisect_create_bilog_table(0, zBisect, 1) ){
    tmFlags |= TIMELINE_UNHIDE | TIMELINE_BISECT | TIMELINE_FILLGAPS;
    zType = "ci";
    disableY = 1;
  }else{
    zBisect = 0;
  }

  style_header("Timeline");
  if( advancedMenu ){
    style_submenu_element("Help", "%R/help?cmd=/timeline");
  }
  login_anonymous_available();
  timeline_temp_table();
  blob_zero(&sql);
  blob_zero(&desc);
  blob_append(&sql, "INSERT OR IGNORE INTO timeline ", -1);
  blob_append(&sql, timeline_query_for_www(), -1);
  if( PB("fc") || PB("v") || PB("detail") ){
    tmFlags |= TIMELINE_FCHANGES;
  }
  if( PB("vfx") ){
    tmFlags |= TIMELINE_FORUMTXT;
  }
  if( (tmFlags & TIMELINE_UNHIDE)==0 ){
    blob_append_sql(&sql,
      " AND NOT EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)\n",
      TAG_HIDDEN
    );
  }
  if( from_rid && !to_rid && (P("ft")!=0 || P("bt")!=0) ){
    const char *zTo = P("ft");
    if( zTo ){
      from_to_mode = 1;
      to_rid = timeline_endpoint(from_rid, zTo, 1);
    }else{
      from_to_mode = 2;
      zTo = P("bt");
      to_rid = timeline_endpoint(from_rid, zTo, 0);
    }
    if( to_rid ){
      cgi_replace_parameter("to", zTo);
      if( selectedRid==0 ) selectedRid = from_rid;
      if( secondaryRid==0 ) secondaryRid = to_rid;
    }else{
      to_rid = from_rid;
      blob_appendf(&desc, "There is no path from %h %s to %h.<br>Instead: ",
                   P("from"), from_to_mode==1 ? "forward" : "back", zTo);
     }
  }
  if( ((from_rid && to_rid) || (me_rid && you_rid)) && g.perm.Read ){
    /* If from= and to= are present, display all nodes on a path connecting
    ** the two */
    PathNode *p = 0;
    const char *zFrom = 0;
    const char *zTo = 0;
    Blob ins;
    int nNodeOnPath = 0;
    int commonAncs = 0;    /* Common ancestors of me_rid and you_rid. */
    int earlierRid = 0, laterRid = 0;
    int cost = bShort ? 0 : 1;
    int nSkip = 0;

    if( from_rid && to_rid ){
      if( from_to_mode==0 ){
        p = path_shortest(from_rid, to_rid, 0, 0, 0, cost);
      }else if( from_to_mode==1 ){
        p = path_shortest(from_rid, to_rid, 0, 1, 0, cost);
        earlierRid = commonAncs = from_rid;
        laterRid = to_rid;
      }else{
        p = path_shortest(to_rid, from_rid, 0, 1, 0, cost);
        earlierRid = commonAncs = to_rid;
        laterRid = from_rid;
      }
      zFrom = P("from");
      zTo = zTo2 ? zTo2 : P("to");
    }else{
      commonAncs = path_common_ancestor(me_rid, you_rid);
      if( commonAncs!=0 ){
        p = path_first();
      }
      if( commonAncs==you_rid ){
        zFrom = P("you");
        zTo = P("me");
        earlierRid = you_rid;
        laterRid = me_rid;
      }else{
        zFrom = P("me");
        zTo = P("you");
        earlierRid = me_rid;
        laterRid = you_rid;
      }
    }
    blob_init(&ins, 0, 0);
    db_multi_exec(
      "CREATE TEMP TABLE IF NOT EXISTS pathnode(x INTEGER PRIMARY KEY);"
    );
    if( p ){
      int cnt = 4;
      blob_init(&ins, 0, 0);
      blob_append_sql(&ins, "INSERT INTO pathnode(x) VALUES(%d)", p->rid);
      if( p->u.pTo==0 ) bMin = 0;
      for(p=p->u.pTo; p; p=p->u.pTo){
        if( bMin
         && p->u.pTo!=0
         && fossil_strcmp(path_branch(p->pFrom),path_branch(p))==0
         && fossil_strcmp(path_branch(p),path_branch(p->u.pTo))==0
        ){
          nSkip++;
        }else if( cnt==8 ){
          blob_append_sql(&ins, ",\n  (%d)", p->rid);
          cnt = 0;
        }else{
          cnt++;
          blob_append_sql(&ins, ",(%d)", p->rid);
        }
      }
    }
    path_reset();
    db_multi_exec("%s", blob_str(&ins)/*safe-for-%s*/);
    blob_reset(&ins);
    if( related ){
      db_multi_exec(
        "CREATE TEMP TABLE IF NOT EXISTS related(x INTEGER PRIMARY KEY);"
        "INSERT OR IGNORE INTO related(x)"
        " SELECT pid FROM plink WHERE cid IN pathnode AND NOT isprim;"
      );
      if( related==1 ){
        db_multi_exec(
          "INSERT OR IGNORE INTO related(x)"
          " SELECT cid FROM plink WHERE pid IN pathnode;"
        );
      }
      if( showCherrypicks ){
        db_multi_exec(
          "INSERT OR IGNORE INTO related(x)"
          " SELECT parentid FROM cherrypick WHERE childid IN pathnode;"
        );
        if( related==1 ){
          db_multi_exec(
            "INSERT OR IGNORE INTO related(x)"
            " SELECT childid FROM cherrypick WHERE parentid IN pathnode;"
          );
        }
      }
      if( earlierRid && laterRid && commonAncs==earlierRid ){
        /* On a query with me=XXX, you=YYY, and rel, omit all nodes that
        ** are not ancestors of either XXX or YYY, as those nodes tend to
        ** be extraneous */
        db_multi_exec(
          "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY)"
        );
        compute_ancestors(laterRid, 0, 0, earlierRid);
        db_multi_exec(
          "DELETE FROM related WHERE x NOT IN ok;"
        );
      }
      db_multi_exec("INSERT OR IGNORE INTO pathnode SELECT x FROM related");
    }
    add_extra_rids("pathnode",P("x"));
    blob_append_sql(&sql, " AND event.objid IN pathnode");
    if( zChng && zChng[0] ){
      db_multi_exec(
        "DELETE FROM pathnode\n"
        " WHERE NOT EXISTS(SELECT 1 FROM mlink, filename\n"
        "                   WHERE mlink.mid=x\n"
        "                     AND mlink.fnid=filename.fnid\n"
        "                     AND %s)",
        glob_expr("filename.name", zChng)
      );
    }
    tmFlags |= TIMELINE_XMERGE | TIMELINE_FILLGAPS;
    db_multi_exec("%s", blob_sql_text(&sql));
    if( advancedMenu ){
      style_submenu_checkbox("v", "Files", (zType[0]!='a' && zType[0]!='c'),0);
    }
    nNodeOnPath = db_int(0, "SELECT count(*) FROM temp.pathnode");
    if( nNodeOnPath==1 && from_to_mode>0 ){
      blob_appendf(&desc,"Check-in ");
    }else if( bMin ){
      blob_appendf(&desc, "%d of %d check-ins along the path from ",
                   nNodeOnPath, nNodeOnPath+nSkip);
    }else{
      blob_appendf(&desc, "%d check-ins going from ", nNodeOnPath);
    }
    if( from_rid==selectedRid ){
      blob_appendf(&desc, "<span class='timelineSelected'>");
    }
    blob_appendf(&desc, "%z%h</a>", href("%R/info/%h", zFrom), zFrom);
    if( from_rid==selectedRid ) blob_appendf(&desc, "</span>");
    if( nNodeOnPath==1 && from_to_mode>0 ){
      blob_appendf(&desc, " only");
    }else{
      blob_append(&desc, " to ", -1);
      if( to_rid==secondaryRid ){
        blob_appendf(&desc,"<span class='timelineSelected timelineSecondary'>");
      }
      blob_appendf(&desc, "%z%h</a>", href("%R/info/%h",zTo), zTo);
      if( to_rid==secondaryRid )  blob_appendf(&desc, "</span>");
      if( related ){
        int nRelated = db_int(0, "SELECT count(*) FROM timeline") - nNodeOnPath;
        if( nRelated>0 ){
          blob_appendf(&desc, " and %d related check-in%s", nRelated,
                       nRelated>1 ? "s" : "");
        }
      }
    }
    addFileGlobDescription(zChng, &desc);
  }else if( (p_rid || d_rid) && g.perm.Read && zTagSql==0 ){
    /* If either p= or d= or both are present, ignore all other parameters
    ** other than n=, ft=, and bt= */
    const char *zBaseName = 0;
    int np = 0, nd;
    const char *zBackTo = 0;
    const char *zFwdTo = 0;
    int ridBackTo = 0;
    int ridFwdTo = 0;
    int bBackAdded = 0;       /* True if the zBackTo node was added */
    int bFwdAdded = 0;        /* True if the zBackTo node was added */
    int bSeparateDandP = 0;   /* p_rid & d_rid both exist and are distinct */

    tmFlags |= TIMELINE_XMERGE | TIMELINE_FILLGAPS;
    if( p_rid && d_rid && p_rid!=d_rid ){
      bSeparateDandP = 1;
      db_multi_exec(
        "CREATE TEMP TABLE IF NOT EXISTS ok_d(rid INTEGER PRIMARY KEY)"
      );
    }else{
      zBaseName = p_rid ? zDPNameP : zDPNameD;
    }
    db_multi_exec(
      "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY)"
    );
    add_extra_rids("ok", P("x"));
    blob_append_sql(&sql, " AND event.objid IN ok");
    nd = 0;
    if( d_rid ){
      double rStopTime = 9e99;
      zFwdTo = P("ft");
      if( zFwdTo && bSeparateDandP ){
        if( zError==0 ){
          zError = "Cannot use the ft= query parameter when both p= and d= "
                   "are used and have distinct values.";
        }
        zFwdTo = 0;
      }        
      if( zFwdTo ){
        double rStartDate = mtime_of_rid(d_rid, 0.0);
        ridFwdTo = first_checkin_with_tag_after_date(zFwdTo, rStartDate);
        if( ridFwdTo==0 ){
          ridFwdTo = name_to_typed_rid(zBackTo,"ci");
        }
        if( ridFwdTo ){
          if( !haveParameterN ) nEntry = 0;
          rStopTime = mtime_of_rid(ridFwdTo, 9e99);
        }
      }else if( bSeparateDandP ){
        rStopTime = mtime_of_rid(p_rid, 9e99);
        nEntry = 0;
      }
      if( rStopTime<9e99 ){
        rStopTime += 5.8e-6;  /* Round up by 1/2 second */
      }
      db_multi_exec(
        "WITH RECURSIVE dx(rid,mtime) AS (\n"
        "  SELECT %d, 0\n"
        "  UNION\n"
        "  SELECT plink.cid, plink.mtime FROM dx, plink\n"
        "   WHERE plink.pid=dx.rid\n"
        "     AND plink.mtime<=%.*g\n"
        "   ORDER BY 2\n"
        ")\n"
        "INSERT OR IGNORE INTO ok SELECT rid FROM dx LIMIT %d",
        d_rid, rStopTime<8e99 ? 17 : 2, rStopTime, nEntry<=0 ? -1 : nEntry+1
      );
      if( ridFwdTo && !db_exists("SELECT 1 FROM ok WHERE rid=%d",ridFwdTo) ){
        db_multi_exec("INSERT OR IGNORE INTO ok VALUES(%d)", ridFwdTo);
        bFwdAdded = 1;
      }
      if( bSeparateDandP ){
        db_multi_exec(
           "INSERT INTO ok_d SELECT rid FROM ok;"
           "DELETE FROM ok;"
        );
      }else{
        nd = db_int(0, "SELECT count(*)-1 FROM ok");
        if( nd>=0 ) db_multi_exec("%s", blob_sql_text(&sql));
        if( nd>0 || p_rid==0 ){
          blob_appendf(&desc, "%d descendant%s", nd,(1==nd)?"":"s");
        }
        if( useDividers && !selectedRid ) selectedRid = d_rid;
        db_multi_exec("DELETE FROM ok");
      }
    }
    if( p_rid ){
      zBackTo = P("bt");
      if( zBackTo && bSeparateDandP ){
        if( zError==0 ){
          zError = "Cannot use the bt= query parameter when both p= and d= "
                   "are used and have distinct values.";
        }
        zBackTo = 0;
      }        
      if( zBackTo ){
        double rDateLimit = mtime_of_rid(p_rid, 0.0);
        ridBackTo = last_checkin_with_tag_before_date(zBackTo, rDateLimit);
        if( ridBackTo==0 ){
          ridBackTo = name_to_typed_rid(zBackTo,"ci");
        }
        if( ridBackTo && !haveParameterN ) nEntry = 0;
      }else if( bSeparateDandP ){
        ridBackTo = d_rid;
        nEntry = 0;
      }
      compute_ancestors(p_rid, nEntry==0 ? 0 : nEntry+1, 0, ridBackTo);
      if( ridBackTo && !db_exists("SELECT 1 FROM ok WHERE rid=%d",ridBackTo) ){
        db_multi_exec("INSERT OR IGNORE INTO ok VALUES(%d)", ridBackTo);
        bBackAdded = 1;
      }
      if( bSeparateDandP ){
        db_multi_exec("DELETE FROM ok WHERE rid NOT IN ok_d;");
        db_multi_exec("%s", blob_sql_text(&sql));
      }else{
        np = db_int(0, "SELECT count(*)-1 FROM ok");
        if( np>0 || nd==0 ){
          if( nd>0 ) blob_appendf(&desc, " and ");
          blob_appendf(&desc, "%d ancestor%s", np, (1==np)?"":"s");
          db_multi_exec("%s", blob_sql_text(&sql));
        }
        if( useDividers && !selectedRid ) selectedRid = p_rid;
      }
    }
    if( bSeparateDandP ){
      int n = db_int(0, "SELECT count(*) FROM ok");
      blob_reset(&desc);
      blob_appendf(&desc,
          "%d check-ins that are both ancestors of %z%h</a>"
          " and descendants of %z%h</a>",
          n,
          href("%R/info?name=%h",zDPNameP),zDPNameP,
          href("%R/info?name=%h",zDPNameD),zDPNameD
      );
      ridBackTo = 0;
      ridFwdTo = 0;
    }else{
      blob_appendf(&desc, " of %z%h</a>",
                   href("%R/info?name=%h", zBaseName), zBaseName);
    }
    if( ridBackTo ){
      if( np==0 ){
        blob_reset(&desc);
        blob_appendf(&desc,
                    "Check-in %z%h</a> only (%z%h</a> does not precede it)",
                     href("%R/info?name=%h",zBaseName), zBaseName,
                     href("%R/info?name=%h",zBackTo), zBackTo);
      }else{
        blob_appendf(&desc, " back to %z%h</a>%s",
                     href("%R/info?name=%h",zBackTo), zBackTo,
                     bBackAdded ? " (not a direct anscestor)" : "");
        if( ridFwdTo && zFwdTo ){
          blob_appendf(&desc, " and up to %z%h</a>%s",
                     href("%R/info?name=%h",zFwdTo), zFwdTo,
                     bFwdAdded ? " (not a direct descendant)" : "");
        }
      }
    }else if( ridFwdTo ){
      if( nd==0 ){
        blob_reset(&desc);
        blob_appendf(&desc,
                    "Check-in %z%h</a> only (%z%h</a> does not follow it)",
                     href("%R/info?name=%h",zBaseName), zBaseName,
                     href("%R/info?name=%h",zFwdTo), zFwdTo);
      }else{
        blob_appendf(&desc, " up to %z%h</a>%s",
                     href("%R/info?name=%h",zFwdTo), zFwdTo,
                     bFwdAdded ? " (not a direct descendant)":"");
      }
    }
    if( advancedMenu ){
      style_submenu_checkbox("v", "Files", (zType[0]!='a' && zType[0]!='c'),0);
    }
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
    if( showCherrypicks ){
      db_multi_exec(
         "INSERT OR IGNORE INTO ok SELECT parentid FROM cherrypick"
         " WHERE childid=%d;"
         "INSERT OR IGNORE INTO ok SELECT childid FROM cherrypick"
         " WHERE parentid=%d;",
         f_rid, f_rid
      );
    }
    blob_append_sql(&sql, " AND event.objid IN ok");
    db_multi_exec("%s", blob_sql_text(&sql));
    if( useDividers && !selectedRid ) selectedRid = f_rid;
    blob_appendf(&desc, "Parents and children of check-in ");
    zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", f_rid);
    blob_appendf(&desc, "%z[%S]</a>", href("%R/info/%!S", zUuid), zUuid);
    tmFlags |= TIMELINE_XMERGE;
    if( advancedMenu ){
      style_submenu_checkbox("unhide", "Unhide", 0, 0);
      style_submenu_checkbox("v", "Files", (zType[0]!='a' && zType[0]!='c'),0);
    }
  }else{
    /* Otherwise, a timeline based on a span of time */
    int n;
    const char *zEType = "event";
    char *zDate;
    Blob cond;
    blob_zero(&cond);
    tmFlags |= TIMELINE_FILLGAPS;
    if( zChng && *zChng ){
      addFileGlobExclusion(zChng, &cond);
      tmFlags |= TIMELINE_XMERGE;
    }
    if( zUses ){
      blob_append_sql(&cond, " AND event.objid IN usesfile\n");
    }
    if( renameOnly ){
      blob_append_sql(&cond, " AND event.objid IN rnfile\n");
    }
    if( forkOnly ){
      blob_append_sql(&cond, " AND event.objid IN rnfork\n");
    }
    if( cpOnly && showCherrypicks ){
      db_multi_exec(
        "CREATE TEMP TABLE IF NOT EXISTS cpnodes(rid INTEGER PRIMARY KEY);"
        "INSERT OR IGNORE INTO cpnodes SELECT childid FROM cherrypick;"
        "INSERT OR IGNORE INTO cpnodes SELECT parentid FROM cherrypick;"
      );
      blob_append_sql(&cond, " AND event.objid IN cpnodes\n");
    }
    if( bisectLocal || zBisect!=0 ){
      blob_append_sql(&cond, " AND event.objid IN (SELECT rid FROM bilog)\n");
    }
    if( zYearMonth ){
      char *zNext;
      int bZulu = 0;
      const char *zTZMod;
      zYearMonth = timeline_expand_datetime(zYearMonth, &bZulu);
      zYearMonth = mprintf("%.7s", zYearMonth);
      if( db_int(0,"SELECT julianday('%q-01') IS NULL", zYearMonth) ){
        zYearMonth = db_text(0, "SELECT strftime('%%Y-%%m','now');");
      }
      zTZMod = (bZulu==0 && fossil_ui_localtime()) ? "utc" : "+00:00";
      if( db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid"
          "   AND mtime>=julianday('%q-01',%Q)%s)",
          zYearMonth, zTZMod, blob_sql_text(&cond))
      ){
        zNext = db_text(0, "SELECT strftime('%%Y%%m%q','%q-01','+1 month');",
                        &"Z"[!bZulu], zYearMonth);
        zNewerButton = fossil_strdup(url_render(&url, "ym", zNext, 0, 0));
        zNewerButtonLabel = "Following month";
        fossil_free(zNext);
      }
      if( db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid"
          "   AND mtime<julianday('%q-01',%Q)%s)",
          zYearMonth, zTZMod, blob_sql_text(&cond))
      ){
        zNext = db_text(0, "SELECT strftime('%%Y%%m%q','%q-01','-1 month');",
                        &"Z"[!bZulu], zYearMonth);
        zOlderButton = fossil_strdup(url_render(&url, "ym", zNext, 0, 0));
        zOlderButtonLabel = "Previous month";
        fossil_free(zNext);
      }
      blob_append_sql(&cond,
         " AND event.mtime>=julianday('%q-01',%Q)"
         " AND event.mtime<julianday('%q-01',%Q,'+1 month')\n",
         zYearMonth, zTZMod, zYearMonth, zTZMod);
      nEntry = -1;
      /* Adjust the zYearMonth for the title */
      zYearMonth = mprintf("%z-01%s", zYearMonth, &"Z"[!bZulu]);
    }
    else if( zYearWeek ){
      char *z, *zNext;
      int bZulu = 0;
      const char *zTZMod;
      zYearWeek = timeline_expand_datetime(zYearWeek, &bZulu);
      z = db_text(0, "SELECT strftime('%%Y-%%W',%Q)", zYearWeek);
      if( z && z[0] ){
        zYearWeekStart = db_text(0, "SELECT date(%Q,'-6 days','weekday 1')",
                                 zYearWeek);
        zYearWeek = z;
      }else{
        if( strlen(zYearWeek)==7 ){
          zYearWeekStart = db_text(0,
             "SELECT date('%.4q-01-01','%+d days','weekday 1')",
             zYearWeek, atoi(zYearWeek+5)*7-6);
        }else{
          zYearWeekStart = 0;
        }
        if( zYearWeekStart==0 || zYearWeekStart[0]==0 ){
          zYearWeekStart = db_text(0,
             "SELECT date('now','-6 days','weekday 1');");
          zYearWeek = db_text(0,
             "SELECT strftime('%%Y-%%W','now','-6 days','weekday 1')");
        }
      }
      zTZMod = (bZulu==0 && fossil_ui_localtime()) ? "utc" : "+00:00";
      if( db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid"
          "   AND mtime>=julianday(%Q,%Q)%s)",
          zYearWeekStart, zTZMod, blob_sql_text(&cond))
      ){
        zNext = db_text(0, "SELECT strftime('%%Y%%W%q',%Q,'+7 day');",
                        &"Z"[!bZulu], zYearWeekStart);
        zNewerButton = fossil_strdup(url_render(&url, "yw", zNext, 0, 0));
        zNewerButtonLabel = "Following week";
        fossil_free(zNext);
      }
      if( db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid"
          "   AND mtime<julianday(%Q,%Q)%s)",
          zYearWeekStart, zTZMod, blob_sql_text(&cond))
      ){
        zNext = db_text(0, "SELECT strftime('%%Y%%W%q',%Q,'-7 days');",
                        &"Z"[!bZulu], zYearWeekStart);
        zOlderButton = fossil_strdup(url_render(&url, "yw", zNext, 0, 0));
        zOlderButtonLabel = "Previous week";
        fossil_free(zNext);
      }
      blob_append_sql(&cond,
        " AND event.mtime>=julianday(%Q,%Q)"
        " AND event.mtime<julianday(%Q,%Q,'+7 days')\n",
        zYearWeekStart, zTZMod, zYearWeekStart, zTZMod);
      nEntry = -1;
      if( fossil_ui_localtime() && bZulu ){
        zYearWeekStart = mprintf("%zZ", zYearWeekStart);
      }
    }
    else if( zDay && timeline_is_datespan(zDay) ){
      char *zNext;
      char *zStart, *zEnd;
      int nDay;
      int bZulu = 0;
      const char *zTZMod;
      zEnd = db_text(0, "SELECT date(%Q)",
                     timeline_expand_datetime(zDay+9, &bZulu));
      zStart = db_text(0, "SELECT date('%.4q-%.2q-%.2q')",
                        zDay, zDay+4, zDay+6);
      nDay = db_int(0, "SELECT julianday(%Q)-julianday(%Q)", zEnd, zStart);
      if( nDay==0 ){
        zDay = &zDay[9];
        goto single_ymd;
      }
      if( nDay<0 ){
        char *zTemp = zEnd;
        zEnd = zStart;
        zStart = zTemp;
        nDay = 1 - nDay;
      }else{
        nDay += 1;
      }
      zTZMod = (bZulu==0 && fossil_ui_localtime()) ? "utc" : "+00:00";
      if( nDay>0 && db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid"
          "   AND mtime>=julianday(%Q,'1 day',%Q)%s)",
          zEnd, zTZMod, blob_sql_text(&cond))
      ){
        zNext = db_text(0,
           "SELECT strftime('%%Y%%m%%d-',%Q,'%d days')||"
                  "strftime('%%Y%%m%%d%q',%Q,'%d day');",
                  zStart, nDay, &"Z"[!bZulu], zEnd, nDay);
        zNewerButton = fossil_strdup(url_render(&url, "ymd", zNext, 0, 0));
        zNewerButtonLabel = mprintf("Following %d days", nDay);
        fossil_free(zNext);
      }
      if( nDay>1 && db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid"
          "   AND mtime<julianday(%Q,'-1 day',%Q)%s)",
          zStart, zTZMod, blob_sql_text(&cond))
      ){
        zNext = db_text(0,
           "SELECT strftime('%%Y%%m%%d-',%Q,'%d days')||"
                  "strftime('%%Y%%m%%d%q',%Q,'%d day');",
                  zStart, -nDay, &"Z"[!bZulu], zEnd, -nDay);
        zOlderButton = fossil_strdup(url_render(&url, "ymd", zNext, 0, 0));
        zOlderButtonLabel = mprintf("Previous %d days", nDay);
        fossil_free(zNext);
      }
      blob_append_sql(&cond,
        " AND event.mtime>=julianday(%Q,%Q)"
        " AND event.mtime<julianday(%Q,%Q,'+1 day')\n",
        zStart, zTZMod, zEnd, zTZMod);
      nEntry = -1;
      
      if( fossil_ui_localtime() && bZulu ){
        zDay = mprintf("%d days between %zZ and %zZ", nDay, zStart, zEnd);
      }else{
        zDay = mprintf("%d days between %z and %z", nDay, zStart, zEnd);
      }
    }
    else if( zDay ){
      char *zNext;
      int bZulu = 0;
      const char *zTZMod;
    single_ymd:
      bZulu = 0;
      zDay = timeline_expand_datetime(zDay, &bZulu);
      zDay = db_text(0, "SELECT date(%Q)", zDay);
      if( zDay==0 || zDay[0]==0 ){
        zDay = db_text(0, "SELECT date('now')");
      }
      zTZMod = (bZulu==0 && fossil_ui_localtime()) ? "utc" : "+00:00";
      if( db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid"
          "   AND mtime>=julianday(%Q,'+1 day',%Q)%s)",
          zDay, zTZMod, blob_sql_text(&cond))
      ){
        zNext = db_text(0,"SELECT strftime('%%Y%%m%%d%q',%Q,'+1 day');",
                        &"Z"[!bZulu], zDay);
        zNewerButton = fossil_strdup(url_render(&url, "ymd", zNext, 0, 0));
        zNewerButtonLabel = "Following day";
        fossil_free(zNext);
      }
      if( db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid"
          "   AND mtime<julianday(%Q,'-1 day',%Q)%s)",
          zDay, zTZMod, blob_sql_text(&cond))
      ){
        zNext = db_text(0,"SELECT strftime('%%Y%%m%%d%q',%Q,'-1 day');",
                        &"Z"[!bZulu], zDay);
        zOlderButton = fossil_strdup(url_render(&url, "ymd", zNext, 0, 0));
        zOlderButtonLabel = "Previous day";
        fossil_free(zNext);
      }
      blob_append_sql(&cond,
        " AND event.mtime>=julianday(%Q,%Q)"
        " AND event.mtime<julianday(%Q,%Q,'+1 day')\n",
        zDay, zTZMod, zDay, zTZMod);
      nEntry = -1;
      if( fossil_ui_localtime() && bZulu ){
        zDay = mprintf("%zZ", zDay); /* Add Z suffix to day for the title */
      }
    }
    else if( zNDays ){
      nDays = atoi(zNDays);
      if( nDays<1 ) nDays = 1;
      blob_append_sql(&cond, " AND event.mtime>=julianday('now','-%d days') ",
                      nDays);
      nEntry = -1;
    }
    else if( zYear &&
             ((4==strlen(zYear) && atoi(zYear)>1900)
              || (1==strlen(zYear) && 0==atoi(zYear)))){
      int year = atoi(zYear);
      char *zNext = 0;
      if(0==year){/*use current year*/
        Stmt qy;
        db_prepare(&qy, "SELECT strftime('%%Y','now')");
        db_step(&qy);
        year = db_column_int(&qy, 0);
        zYear = fossil_strdup(db_column_text(&qy, 0));
        db_finalize(&qy);
      }else{
        zNext = mprintf("%d", year+1);
        if( db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid AND strftime('%%Y',mtime)=%Q %s)",
          zNext, blob_sql_text(&cond))
        ){
          zNewerButton = fossil_strdup(url_render(&url, "year", zNext, 0, 0));
          zNewerButtonLabel = "Following year";
        }
        fossil_free(zNext);
      }
      zNext = mprintf("%d", year-1);
      if( db_int(0,
          "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
          " WHERE blob.rid=event.objid AND strftime('%%Y',mtime)=%Q %s)",
          zNext, blob_sql_text(&cond))
      ){
        zOlderButton = fossil_strdup(url_render(&url, "year", zNext, 0, 0));
        zOlderButtonLabel = "Previous year";
      }
      fossil_free(zNext);
      blob_append_sql(&cond, " AND %Q=strftime('%%Y',event.mtime) ",
                      zYear);
      nEntry = -1;
    }
    if( zTagSql ){
      db_multi_exec(
        "CREATE TEMP TABLE selected_nodes(rid INTEGER PRIMARY KEY);"
        "INSERT OR IGNORE INTO selected_nodes\n"
        "  SELECT tagxref.rid FROM tagxref NATURAL JOIN tag\n"
        "   WHERE tagtype>0\n"
        "     AND %s", zTagSql/*safe-for-%s*/
      );
      if( zMark ){
        /* If the t=release option is used with m=UUID, then also
        ** include the UUID check-in in the display list */
        int ridMark = name_to_rid(zMark);
        db_multi_exec(
          "INSERT OR IGNORE INTO selected_nodes(rid) VALUES(%d)", ridMark);
      }
      add_extra_rids("selected_nodes",P("x"));
      if( related==0 ){
        blob_append_sql(&cond, " AND blob.rid IN selected_nodes");
      }else{
        db_multi_exec(
          "CREATE TEMP TABLE related_nodes(rid INTEGER PRIMARY KEY);"
          "INSERT INTO related_nodes SELECT rid FROM selected_nodes;"
        );
        blob_append_sql(&cond, " AND blob.rid IN related_nodes");
        /* The next two blob_appendf() calls add SQL that causes check-ins that
        ** are not part of the branch which are parents or children of the
        ** branch to be included in the report.  These related check-ins are
        ** useful in helping to visualize what has happened on a quiescent
        ** branch that is infrequently merged with a much more activate branch.
        */
        db_multi_exec(
          "INSERT OR IGNORE INTO related_nodes\n"
          "  SELECT pid FROM selected_nodes CROSS JOIN plink\n"
          "  WHERE selected_nodes.rid=plink.cid;"
        );
        if( related==1 ){
          db_multi_exec(
            "INSERT OR IGNORE INTO related_nodes\n"
            "  SELECT cid FROM selected_nodes CROSS JOIN plink\n"
            "  WHERE selected_nodes.rid=plink.pid;"
          );
          if( showCherrypicks ){
            db_multi_exec(
              "INSERT OR IGNORE INTO related_nodes\n"
              "  SELECT childid FROM selected_nodes CROSS JOIN cherrypick\n"
              "  WHERE selected_nodes.rid=cherrypick.parentid;"
            );
          }
        }
        if( showCherrypicks ){
          db_multi_exec(
            "INSERT OR IGNORE INTO related_nodes\n"
            "  SELECT parentid FROM selected_nodes CROSS JOIN cherrypick\n"
            "  WHERE selected_nodes.rid=cherrypick.childid;"
          );
        }
        if( (tmFlags & TIMELINE_UNHIDE)==0 ){
          db_multi_exec(
            "DELETE FROM related_nodes\n"
            " WHERE rid IN (SELECT related_nodes.rid\n"
            "                 FROM related_nodes, tagxref\n"
            "                WHERE tagid=%d AND tagtype>0\n"
            "                  AND tagxref.rid=related_nodes.rid)",
            TAG_HIDDEN
          );
        }
      }
    }
    if( (zType[0]=='w' && !g.perm.RdWiki)
     || (zType[0]=='t' && !g.perm.RdTkt)
     || (zType[0]=='n' && !g.perm.RdTkt)
     || (zType[0]=='e' && !g.perm.RdWiki)
     || (zType[0]=='c' && !g.perm.Read)
     || (zType[0]=='g' && !g.perm.Read)
     || (zType[0]=='f' && !g.perm.RdForum)
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
        if( g.perm.RdForum ){
          blob_append_sql(&cond, "%c'f'", cSep);
          cSep = ',';
        }
        blob_append_sql(&cond, ")");
      }
    }else{ /* zType!="all" */
      if( zType[0]=='n' ){
        blob_append_sql(&cond,
            " AND event.type='t' AND event.comment GLOB 'New ticket*'");
      }else{
        blob_append_sql(&cond, " AND event.type=%Q", zType);
      }
      if( zType[0]=='c' ){
        zEType = "check-in";
      }else if( zType[0]=='w' ){
        zEType = "wiki";
      }else if( zType[0]=='t' ){
        zEType = "ticket change";
      }else if( zType[0]=='n' ){
        zEType = "new ticket";
      }else if( zType[0]=='e' ){
        zEType = "technical note";
      }else if( zType[0]=='g' ){
        zEType = "tag";
      }else if( zType[0]=='f' ){
        zEType = "forum post";
      }
    }
    if( zUser ){
      int n = db_int(0,"SELECT count(*) FROM event"
                       " WHERE user=%Q OR euser=%Q", zUser, zUser);
      if( n<=nEntry ){
        nEntry = -1;
      }
      blob_append_sql(&cond, " AND (event.user=%Q OR event.euser=%Q)",
                   zUser, zUser);
      zThisUser = zUser;
    }
    if( zSearch ){
      if( tmFlags & TIMELINE_FORUMTXT ){
        sqlite3_create_function(g.db, "forum_post_content", 1, SQLITE_UTF8,
                 0, forum_post_content_function, 0, 0);
        blob_append_sql(&cond,
          " AND (event.comment LIKE '%%%q%%'"
               " OR event.brief LIKE '%%%q%%'"
               " OR (event.type=='f' AND"
                     " forum_post_content(event.objid) LIKE '%%%q%%'))",
          zSearch, zSearch, zSearch);
      }else{
        blob_append_sql(&cond,
          " AND (event.comment LIKE '%%%q%%' OR event.brief LIKE '%%%q%%')",
          zSearch, zSearch);
      }
    }
    rBefore = symbolic_name_to_mtime(zBefore, &zBefore, 1);
    rAfter = symbolic_name_to_mtime(zAfter, &zAfter, 0);
    rCirca = symbolic_name_to_mtime(zCirca, &zCirca, 0);
    blob_append_sql(&sql, "%s", blob_sql_text(&cond));
    if( rAfter>0.0 ){
      if( rBefore>0.0 ){
        blob_append_sql(&sql,
           " AND event.mtime>=%.17g AND event.mtime<=%.17g\n"
           " ORDER BY event.mtime ASC", rAfter-ONE_SECOND, rBefore+ONE_SECOND);
        nEntry = -1;
      }else{
        blob_append_sql(&sql,
           " AND event.mtime>=%.17g\n ORDER BY event.mtime ASC",
           rAfter-ONE_SECOND);
      }
      zCirca = 0;
      url_add_parameter(&url, "c", 0);
    }else if( rBefore>0.0 ){
      blob_append_sql(&sql,
         " AND event.mtime<=%.17g\n ORDER BY event.mtime DESC",
         rBefore+ONE_SECOND);
      zCirca = 0;
      url_add_parameter(&url, "c", 0);
    }else if( rCirca>0.0 ){
      Blob sql2;
      blob_init(&sql2, blob_sql_text(&sql), -1);
      blob_append_sql(&sql2,
          " AND event.mtime>=%f\n ORDER BY event.mtime ASC", rCirca);
      if( nEntry>0 ){
        blob_append_sql(&sql2," LIMIT %d", (nEntry+1)/2);
      }
      db_multi_exec("%s", blob_sql_text(&sql2));
      if( nEntry>0 ){
        nEntry -= db_int(0,"select count(*) from timeline");
        if( nEntry<=0 ) nEntry = 1;
      }
      blob_reset(&sql2);
      blob_append_sql(&sql,
          " AND event.mtime<=%f\n ORDER BY event.mtime DESC",
          rCirca
      );
      if( zMark==0 ) zMark = zCirca;
    }else{
      blob_append_sql(&sql, " ORDER BY event.mtime DESC");
    }
    if( nEntry>0 ) blob_append_sql(&sql, " LIMIT %d", nEntry);
    db_multi_exec("%s", blob_sql_text(&sql));

    n = db_int(0, "SELECT count(*) FROM timeline WHERE etype!='div' /*scan*/");
    zPlural = n==1 ? "" : "s";
    if( zYearMonth ){
      blob_appendf(&desc, "%d %s%s for the month beginning %h",
                   n, zEType, zPlural, zYearMonth);
    }else if( zYearWeek ){
      blob_appendf(&desc, "%d %s%s for week %h beginning on %h",
                   n, zEType, zPlural, zYearWeek, zYearWeekStart);
    }else if( zDay ){
      blob_appendf(&desc, "%d %s%s occurring on %h", n, zEType, zPlural, zDay);
    }else if( zNDays ){
      blob_appendf(&desc, "%d %s%s within the past %d day%s",
                          n, zEType, zPlural, nDays, nDays>1 ? "s" : "");
    }else if( zBefore==0 && zCirca==0 && n>=nEntry && nEntry>0 ){
      blob_appendf(&desc, "%d most recent %s%s", n, zEType, zPlural);
    }else{
      blob_appendf(&desc, "%d %s%s", n, zEType, zPlural);
    }
    if( zUses ){
      char *zFilenames = names_of_file(zUses);
      blob_appendf(&desc, " using file %s version %z%S</a>", zFilenames,
                   href("%R/artifact/%!S",zUses), zUses);
      tmFlags |= TIMELINE_XMERGE | TIMELINE_FILLGAPS;
    }
    if( renameOnly ){
      blob_appendf(&desc, " that contain filename changes");
      tmFlags |= TIMELINE_XMERGE | TIMELINE_FILLGAPS;
    }
    if( forkOnly ){
      blob_appendf(&desc, " associated with forks");
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( bisectLocal || zBisect!=0 ){
      blob_appendf(&desc, " in a bisect");
      tmFlags |= TIMELINE_DISJOINT;
    }
    if( cpOnly && showCherrypicks ){
      blob_appendf(&desc, " that participate in a cherrypick merge");
      tmFlags |= TIMELINE_CHPICK|TIMELINE_DISJOINT;
    }
    if( zUser ){
      if( g.perm.Admin ){
        blob_appendf(&desc, " by user <a href='%R/setup_uinfo?l=%h'>%h</a>",
                     zUser, zUser);
      }else{
        blob_appendf(&desc, " by user %h", zUser);
      }
      tmFlags |= TIMELINE_XMERGE | TIMELINE_FILLGAPS;
    }
    if( zTagSql ){
      if( matchStyle==MS_EXACT || matchStyle==MS_BRLIST ){
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
      if( zMark ){
        blob_appendf(&desc," plus check-in \"%h\"", zMark);
      }
      tmFlags |= TIMELINE_XMERGE | TIMELINE_FILLGAPS;
    }
    addFileGlobDescription(zChng, &desc);
    if( rAfter>0.0 ){
      if( rBefore>0.0 ){
        blob_appendf(&desc, " occurring between %h and %h.<br>",
                     zAfter, zBefore);
      }else{
        blob_appendf(&desc, " occurring on or after %h.<br>", zAfter);
      }
    }else if( rBefore>0.0 ){
      blob_appendf(&desc, " occurring on or before %h.<br>", zBefore);
    }else if( rCirca>0.0 ){
      blob_appendf(&desc, " occurring around %h.<br>", zCirca);
    }
    if( zSearch ){
      blob_appendf(&desc, " matching \"%h\"", zSearch);
    }
    if( g.perm.Hyperlink ){
      static const char *const azMatchStyles[] = {
        "exact", "Exact", "glob", "Glob", "like", "Like", "regexp", "Regexp",
        "brlist", "List"
      };
      double rDate;
      zDate = db_text(0, "SELECT min(timestamp) FROM timeline /*scan*/");
      if( (!zDate || !zDate[0]) && ( zAfter || zBefore ) ){
        zDate = mprintf("%s", (zAfter ? zAfter : zBefore));
      }
      if( zDate ){
        rDate = symbolic_name_to_mtime(zDate, 0, 0);
        if( db_int(0,
            "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
            " WHERE blob.rid=event.objid AND mtime<=%.17g%s)",
            rDate-ONE_SECOND, blob_sql_text(&cond))
        ){
          zOlderButton = fossil_strdup(url_render(&url, "b", zDate, "a", 0));
          zOlderButtonLabel = "More";
        }
        free(zDate);
      }
      zDate = db_text(0, "SELECT max(timestamp) FROM timeline /*scan*/");
      if( (!zDate || !zDate[0]) && ( zAfter || zBefore ) ){
        zDate = mprintf("%s", (zBefore ? zBefore : zAfter));
      }
      if( zDate ){
        rDate = symbolic_name_to_mtime(zDate, 0, 0);
        if( db_int(0,
            "SELECT EXISTS (SELECT 1 FROM event CROSS JOIN blob"
            " WHERE blob.rid=event.objid AND mtime>=%.17g%s)",
            rDate+ONE_SECOND, blob_sql_text(&cond))
        ){
          zNewerButton = fossil_strdup(url_render(&url, "a", zDate, "b", 0));
          zNewerButtonLabel = "More";
        }
        free(zDate);
      }
      if( advancedMenu ){
        if( zType[0]=='a' || zType[0]=='c' ){
          style_submenu_checkbox("unhide", "Unhide", 0, 0);
        }
        style_submenu_checkbox("v", "Files",(zType[0]!='a' && zType[0]!='c'),0);
      }
      style_submenu_entry("n","Max:",4,0);
      timeline_y_submenu(disableY);
      if( advancedMenu ){
        style_submenu_entry("t", "Tag Filter:", -8, 0);
        style_submenu_multichoice("ms", count(azMatchStyles)/2,azMatchStyles,0);
      }
    }
    blob_zero(&cond);
  }
  if( showSql ){
    db_append_dml_to_blob(0);
    @ <pre>%h(blob_str(&allSql))</pre>
    blob_reset(&allSql);
  }
  if( search_restrict(SRCH_CKIN)!=0 ){
    style_submenu_element("Search", "%R/search?y=c");
  }
  if( advancedMenu ){
    style_submenu_element("Basic", "%s",
        url_render(&url, "advm", "0", "udc", "1"));
  }else{
    style_submenu_element("Advanced", "%s",
        url_render(&url, "advm", "1", "udc", "1"));
  }
  if( PB("showid") ) tmFlags |= TIMELINE_SHOWRID;
  if( useDividers && zMark && zMark[0] ){
    double r = symbolic_name_to_mtime(zMark, 0, 0);
    if( r>0.0 && !selectedRid ) selectedRid = timeline_add_divider(r);
  }
  blob_zero(&sql);
  if( PB("oldestfirst") ){
    db_prepare(&q, "SELECT * FROM timeline ORDER BY sortby ASC /*scan*/");
  }else{
    db_prepare(&q, "SELECT * FROM timeline ORDER BY sortby DESC /*scan*/");
  }
  if( fossil_islower(desc.aData[0]) ){
    desc.aData[0] = fossil_toupper(desc.aData[0]);
  }
  if( zBrName ){
    if( !PB("nowiki")
     && wiki_render_associated("branch", zBrName, WIKIASSOC_ALL)
    ){
      @ <div class="section">%b(&desc)</div>
    } else{
      @ <h2>%b(&desc)</h2>
    }
    style_submenu_element("Diff", "%R/vdiff?branch=%T", zBrName);
  }else
  if( zTagName
   && matchStyle==MS_EXACT
   && zBrName==0
   && !PB("nowiki")
   && wiki_render_associated("tag", zTagName, WIKIASSOC_ALL)
  ){
    @ <div class="section">%b(&desc)</div>
  } else{
    @ <h2>%b(&desc)</h2>
  }
  blob_reset(&desc);

  /* Report any errors. */
  if( zError ){
    @ <p class="generalError">%h(zError)</p>
  }

  if( zNewerButton ){
    @ %z(chref("button","%s",zNewerButton))%h(zNewerButtonLabel)\
    @ &nbsp;&uarr;</a>
  }
  cgi_check_for_malice();
  {
    Matcher *pLeftBranch;
    const char *zPattern = P("sl");
    if( zPattern!=0 ){
      MatchStyle ms;
      if( zMatchStyle!=0 ){
        ms = matchStyle;
      }else{
        ms = strpbrk(zPattern,"*[?")!=0 ? MS_GLOB : MS_BRLIST;
      }
      pLeftBranch = match_create(ms,zPattern);
    }else{
      pLeftBranch = match_create(matchStyle, zBrName?zBrName:zTagName);
    }
    www_print_timeline(&q, tmFlags, zThisUser, zThisTag, pLeftBranch,
                       selectedRid, secondaryRid, 0);
    match_free(pLeftBranch);
  }
  db_finalize(&q);
  if( zOlderButton ){
    @ %z(chref("button","%s",zOlderButton))%h(zOlderButtonLabel)\
    @ &nbsp;&darr;</a>
  }
  document_emit_js(/*handles pikchrs rendered above*/);
  blob_reset(&sql);
  blob_reset(&desc);
  style_finish_page();
}

/*
** Translate a timeline entry into the printable format by
** converting every %-substitutions as follows:
**
**     %n  newline
**     %%  a raw %
**     %H  commit hash
**     %h  abbreviated commit hash
**     %a  author name
**     %d  date
**     %c  comment (\n, \t replaced by space, \r deleted)
**     %b  branch
**     %t  tags
**     %p  phase (zero or more of: *CURRENT*, *MERGE*, *FORK*,
**                                 *UNPUBLISHED*, *LEAF*, *BRANCH*)
**
** The returned string is obtained from fossil_malloc() and should
** be freed by the caller.
*/
static char *timeline_entry_subst(
  const char *zFormat,
  int *nLine,
  const char *zId,
  const char *zDate,
  const char *zUser,
  const char *zCom,
  const char *zBranch,
  const char *zTags,
  const char *zPhase
){
  Blob r, co;
  int i, j;
  blob_init(&r, 0, 0);
  blob_init(&co, 0, 0);

  if( 0==zCom ){
    zCom = "(NULL)";
  }

  /* Replace LF and tab with space, delete CR */
  while( zCom[0] ){
    for(j=0; zCom[j] && zCom[j]!='\r' && zCom[j]!='\n' && zCom[j]!='\t'; j++){}
    blob_append(&co, zCom, j);
    if( zCom[j]==0 ) break;
    if( zCom[j]!='\r')
      blob_append(&co, " ", 1);
    zCom += j+1;
  }
  blob_str(&co);

  *nLine = 1;
  while( zFormat[0] ){
    for(i=0; zFormat[i] && zFormat[i]!='%'; i++){}
    blob_append(&r, zFormat, i);
    if( zFormat[i]==0 ) break;
    if( zFormat[i+1]=='%' ){
      blob_append(&r, "%", 1);
      zFormat += i+2;
    }else if( zFormat[i+1]=='n' ){
      blob_append(&r, "\n", 1);
      *nLine += 1;
      zFormat += i+2;
    }else if( zFormat[i+1]=='H' ){
      blob_append(&r, zId, -1);
      zFormat += i+2;
    }else if( zFormat[i+1]=='h' ){
      char *zFree = 0;
      zFree = mprintf("%S", zId);
      blob_append(&r, zFree, -1);
      fossil_free(zFree);
      zFormat += i+2;
    }else if( zFormat[i+1]=='d' ){
      blob_append(&r, zDate, -1);
      zFormat += i+2;
    }else if( zFormat[i+1]=='a' ){
      blob_append(&r, zUser, -1);
      zFormat += i+2;
    }else if( zFormat[i+1]=='c' ){
      blob_append(&r, co.aData, -1);
      zFormat += i+2;
    }else if( zFormat[i+1]=='b' ){
      if( zBranch ) blob_append(&r, zBranch, -1);
      zFormat += i+2;
    }else if( zFormat[i+1]=='t' ){
      blob_append(&r, zTags, -1);
      zFormat += i+2;
    }else if( zFormat[i+1]=='p' ){
      blob_append(&r, zPhase, -1);
      zFormat += i+2;
    }else{
      blob_append(&r, zFormat+i, 1);
      zFormat += i+1;
    }
  }
  fossil_free(co.aData);
  blob_str(&r);
  return r.aData;
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
**    3.  Comment string, user, and tags
**    4.  Number of non-merge children
**    5.  Number of parents
**    6.  mtime
**    7.  branch
**    8.  event-type: 'ci', 'w', 't', 'f', and so forth.
**    9.  comment
**   10.  user
**   11.  tags
*/
void print_timeline(Stmt *q, int nLimit, int width, const char *zFormat,
                    int verboseFlag){
  int nAbsLimit = (nLimit >= 0) ? nLimit : -nLimit;
  int nLine = 0;
  int nEntry = 0;
  char zPrevDate[20];
  const char *zCurrentUuid = 0;
  int fchngQueryInit = 0;     /* True if fchngQuery is initialized */
  Stmt fchngQuery;            /* Query for file changes on check-ins */
  int rc;
  /* True: separate entries with a newline after file listing */
  int bVerboseNL = (zFormat &&
                    (fossil_strcmp(zFormat, TIMELINE_FMT_ONELINE)!=0));
  /* True: separate entries with a newline even with no file listing */
  int bNoVerboseNL = (zFormat &&
                      (fossil_strcmp(zFormat, TIMELINE_FMT_MEDIUM)==0 ||
                       fossil_strcmp(zFormat, TIMELINE_FMT_FULL)==0));

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
    const char *zBranch = db_column_text(q, 7);
    const char *zType = db_column_text(q, 8);
    const char *zComShort = db_column_text(q, 9);
    const char *zUserShort = db_column_text(q, 10);
    const char *zTags = db_column_text(q, 11);
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
    if( zFormat == 0 && fossil_strnicmp(zDate, zPrevDate, 10) ){
      fossil_print("=== %.10s ===\n", zDate);
      memcpy(zPrevDate, zDate, 10);
      nLine++; /* record another line */
    }
    if( zCom==0 ) zCom = "";
    if( zFormat == 0 )
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
    if( zType && zType[0]=='w'
     && (zCom[0]=='+' || zCom[0]=='-' || zCom[0]==':')
    ){
      /* Special processing for Wiki comments */
      if(!zComShort || !*zComShort){
        /* Shouldn't be possible, but just in case... */
        zComShort = " ";
      }
      if( zCom[0]=='+' ){
        zFree = mprintf("[%S] Add wiki page \"%s\" (user: %s)",
                        zId, zComShort+1, zUserShort);
      }else if( zCom[0]=='-' ){
        zFree = mprintf("[%S] Delete wiki page \"%s\" (user: %s)",
                        zId, zComShort+1, zUserShort);
      }else{
        zFree = mprintf("[%S] Edit to wiki page \"%s\" (user: %s)",
                        zId, zComShort+1, zUserShort);
      }
    }else{
      zFree = mprintf("[%S] %s%s", zId, zPrefix, zCom);
    }

    if( zFormat ){
      char *zEntry;
      int nEntryLine = 0;
      if( nChild==0 ){
        sqlite3_snprintf(sizeof(zPrefix)-n, &zPrefix[n], "*LEAF* ");
      }
      zEntry = timeline_entry_subst(zFormat, &nEntryLine, zId, zDate,
                                    zUserShort, zComShort, zBranch, zTags,
                                    zPrefix);
      nLine += nEntryLine;
      fossil_print("%s\n", zEntry);
      fossil_free(zEntry);
    }
    else{
      /* record another X lines */
      nLine += comment_print(zFree, zCom, 9, width, get_comment_format());
    }
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
      if( bVerboseNL ) fossil_print("\n");
    }else{
      if( bNoVerboseNL ) fossil_print("\n");
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
**    wiki_to_text(TEXT)
**
** Return a text rendering of Fossil-Wiki TEXT, intended for display
** on a timeline.  The timeline-plaintext and timeline-hard-newlines
** settings are considered when doing this rendering.
*/
static void wiki_to_text_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zIn, *zOut;
  int nIn, nOut;
  Blob in, html, txt;
  zIn = (const char*)sqlite3_value_text(argv[0]);
  if( zIn==0 ) return;
  nIn = sqlite3_value_bytes(argv[0]);
  blob_init(&in, zIn, nIn);
  blob_init(&html, 0, 0);
  wiki_convert(&in, &html, wiki_convert_flags(0));
  blob_reset(&in);
  blob_init(&txt, 0, 0);
  html_to_plaintext(blob_str(&html), &txt, 0);
  blob_reset(&html);
  nOut = blob_size(&txt);
  zOut = blob_str(&txt);
  while( fossil_isspace(zOut[0]) ){ zOut++; nOut--; }
  while( nOut>0 && fossil_isspace(zOut[nOut-1]) ){ nOut--; }
  sqlite3_result_text(context, zOut, nOut, SQLITE_TRANSIENT);
  blob_reset(&txt);
}

/*
** Return a pointer to a static string that forms the basis for
** a timeline query for display on a TTY.
*/
const char *timeline_query_for_tty(void){
  static int once = 0;
  static const char zBaseSql[] =
    @ SELECT
    @   blob.rid AS rid,
    @   uuid,
    @   datetime(event.mtime,toLocal()) AS mDateTime,
    @   wiki_to_text(coalesce(ecomment,comment))
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
    @   tagxref.value AS branch,
    @   event.type
    @   , coalesce(ecomment,comment) AS comment0
    @   , coalesce(euser,user,'?') AS user0
    @   , (SELECT case when length(x)>0 then x else '' end
    @         FROM (SELECT group_concat(substr(tagname,5), ', ') AS x
    @         FROM tag, tagxref
    @         WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @          AND tagxref.rid=blob.rid AND tagxref.tagtype>0)) AS tags
    @ FROM tag CROSS JOIN event CROSS JOIN blob
    @      LEFT JOIN tagxref ON tagxref.tagid=tag.tagid
    @   AND tagxref.tagtype>0
    @   AND tagxref.rid=blob.rid
    @ WHERE blob.rid=event.objid
    @   AND tag.tagname='branch'
  ;
  if( !once && g.db ){
    once = 1;
    sqlite3_create_function(g.db, "wiki_to_text", 1, SQLITE_UTF8, 0,
                            wiki_to_text_sqlfunc, 0, 0);
  }
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
  return db_int(0, "SELECT EXISTS (SELECT julianday(%Q) AS jd"
                   " WHERE jd IS NOT NULL)", zDate);
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
**   -b|--branch BRANCH   Show only items on the branch named BRANCH
**   -c|--current-branch  Show only items on the current branch
**   -F|--format          Entry format. Values "oneline", "medium", and "full"
**                        get mapped to the full options below. Otherwise a
**                        string which can contain these placeholders:
**                            %n  newline
**                            %%  a raw %
**                            %H  commit hash
**                            %h  abbreviated commit hash
**                            %a  author name
**                            %d  date
**                            %c  comment (NL, TAB replaced by space, LF erased)
**                            %b  branch
**                            %t  tags
**                            %p  phase: zero or more of *CURRENT*, *MERGE*,
**                                      *FORK*, *UNPUBLISHED*, *LEAF*, *BRANCH*
**   --oneline            Show only short hash and comment for each entry
**   --medium             Medium-verbose entry formatting
**   --full               Extra verbose entry formatting
**   -n|--limit N         If N is positive, output the first N entries.  If
**                        N is negative, output the first -N lines.  If N is
**                        zero, no limit.  Default is -20 meaning 20 lines.
**   --offset P           Skip P changes
**   -p|--path PATH       Output items affecting PATH only.
**                        PATH can be a file or a sub directory.
**   -R REPO_FILE         Specifies the repository db to use. Default is
**                        the current check-out's repository.
**   --sql                Show the SQL used to generate the timeline
**   -t|--type TYPE       Output items from the given types only, such as:
**                            ci = file commits only
**                            e  = technical notes only
**                            f  = forum posts only
**                            t  = tickets only
**                            w  = wiki commits only
**   -v|--verbose         Output the list of files changed by each commit
**                        and the type of each change (edited, deleted,
**                        etc.) after the check-in comment.
**   -W|--width N         Width of lines (default is to auto-detect). N must be
**                        either greater than 20 or it must be zero 0 to
**                        indicate no limit, resulting in a single line per
**                        entry.
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
  const char *zFormat = 0;
  const char *zBr = 0;
  Blob treeName;
  int showSql = 0;

  verboseFlag = find_option("verbose","v", 0)!=0;
  if( !verboseFlag){
    verboseFlag = find_option("showfiles","f", 0)!=0; /* deprecated */
  }
  db_find_and_open_repository(0, 0);
  zLimit = find_option("limit","n",1);
  zWidth = find_option("width","W",1);
  zType = find_option("type","t",1);
  zFilePattern = find_option("path","p",1);
  zFormat = find_option("format","F",1);
  zBr = find_option("branch","b",1);
  if( find_option("current-branch","c",0)!=0 ){
    if( !g.localOpen ){
      fossil_fatal("not within an open check-out");
    }else{
      int vid = db_lget_int("checkout", 0);
      zBr = db_text(0, "SELECT value FROM tagxref WHERE rid=%d AND tagid=%d",
                    vid, TAG_BRANCH);
    }
  }
  if( find_option("oneline",0,0)!= 0 || fossil_strcmp(zFormat,"oneline")==0 ){
    zFormat = TIMELINE_FMT_ONELINE;
  }
  if( find_option("medium",0,0)!= 0 || fossil_strcmp(zFormat,"medium")==0 ){
    zFormat = TIMELINE_FMT_MEDIUM;
  }
  if( find_option("full",0,0)!= 0 || fossil_strcmp(zFormat,"full")==0 ){
    zFormat = TIMELINE_FMT_FULL;
  }
  showSql = find_option("sql",0,0)!=0;

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
            "?-W|--width WIDTH? ?-p|--path PATH?");
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
      fossil_fatal("must be within a local check-out to use 'current'");
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
  if( mode==TIMELINE_MODE_AFTER ){
    /* Extra outer select to get older rows in reverse order */
    blob_append(&sql, "SELECT *\nFROM (", -1);
  }
  blob_append(&sql, timeline_query_for_tty(), -1);
  blob_append_sql(&sql, "\n  AND event.mtime %s %s",
     ( mode==TIMELINE_MODE_BEFORE ||
       mode==TIMELINE_MODE_PARENTS ) ? "<=" : ">=", zDate /*safe-for-%s*/
  );
  if( zType && (zType[0]!='a') ){
    blob_append_sql(&sql, "\n  AND event.type=%Q ", zType);
  }

  /* When zFilePattern is specified, compute complete ancestry;
   * limit later at print_timeline() */
  if( mode==TIMELINE_MODE_CHILDREN || mode==TIMELINE_MODE_PARENTS ){
    db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
    if( mode==TIMELINE_MODE_CHILDREN ){
      compute_descendants(objid, (zFilePattern ? 0 : n));
    }else{
      compute_ancestors(objid, (zFilePattern ? 0 : n), 0, 0);
    }
    blob_append_sql(&sql, "\n  AND blob.rid IN ok");
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
  if( zBr ){
    blob_append_sql(&sql,
      "\n  AND blob.rid IN (\n"                          /* Commits */
      "      SELECT rid FROM tagxref NATURAL JOIN tag\n"
      "        WHERE tagtype>0 AND tagname='sym-%q'\n"
      "      UNION\n"                                    /* Tags */
      "      SELECT srcid FROM tagxref WHERE origid IN (\n"
      "        SELECT rid FROM tagxref NATURAL JOIN tag\n"
      "          WHERE tagname='sym-%q')\n"
      "      UNION\n"                                    /* Branch wikis */
      "      SELECT objid FROM event WHERE comment LIKE '_branch/%q'\n"
      "      UNION\n"                                    /* Check-in wikis */
      "      SELECT e.objid FROM event e\n"
      "        INNER JOIN blob b ON b.uuid=substr(e.comment, 10)\n"
      "                          AND e.comment LIKE '_checkin/%%'\n"
      "        LEFT JOIN tagxref tx ON tx.rid=b.rid AND tx.tagid=%d\n"
      "          WHERE tx.value='%q'\n"
      ")\n"                                              /* No merge closures */
      "  AND (tagxref.value IS NULL OR tagxref.value='%q')",
      zBr, zBr, zBr, TAG_BRANCH, zBr, zBr);
  }
  
  if( mode==TIMELINE_MODE_AFTER ){
    /* Complete the above outer select. */
    blob_append_sql(&sql, 
        "\nORDER BY event.mtime LIMIT abs(%d)) t ORDER BY t.mDateTime DESC;", n);
  }else{
    blob_append_sql(&sql, "\nORDER BY event.mtime DESC");
  }
  if( iOffset>0 ){
    /* Don't handle LIMIT here, otherwise print_timeline()
     * will not determine the end-marker correctly! */
    blob_append_sql(&sql, "\n LIMIT -1 OFFSET %d", iOffset);
  }
  if( showSql ){
    fossil_print("%s\n", blob_str(&sql));
  }
  db_prepare_blob(&q, &sql);
  blob_reset(&sql);
  print_timeline(&q, n, width, zFormat, verboseFlag);
  db_finalize(&q);
}

/*
** WEBPAGE: thisdayinhistory
**
** Generate a vanity page that shows project activity for the current
** day of the year for various years in the history of the project.
**
** Query parameters:
**
**    today=DATE             Use DATE as today's date
*/
void thisdayinhistory_page(void){
  static int aYearsAgo[] = { 1, 2, 3, 4, 5, 10, 15, 20, 30, 40, 50, 75, 100 };
  const char *zToday;
  char *zStartOfProject;
  int i;
  Stmt q;
  char *z;
  int bZulu = 0;

  login_check_credentials();
  if( (!g.perm.Read && !g.perm.RdTkt && !g.perm.RdWiki && !g.perm.RdForum) ){
    login_needed(g.anon.Read && g.anon.RdTkt && g.anon.RdWiki);
    return;
  }
  style_set_current_feature("timeline");
  style_header("Today In History");
  zToday = (char*)P("today");
  if( zToday ){
    zToday = timeline_expand_datetime(zToday, &bZulu);
    if( !fossil_isdate(zToday) ) zToday = 0;
  }
  if( zToday==0 ){
    zToday = db_text(0, "SELECT date('now',toLocal())");
  }
  @ <h1>This Day In History For %h(zToday)</h1>
  z = db_text(0, "SELECT date(%Q,'-1 day')", zToday);
  style_submenu_element("Yesterday", "%R/thisdayinhistory?today=%t", z);
  z = db_text(0, "SELECT date(%Q,'+1 day')", zToday);
  style_submenu_element("Tomorrow", "%R/thisdayinhistory?today=%t", z);
  zStartOfProject = db_text(0,
      "SELECT datetime(min(mtime),toLocal(),'startofday') FROM event;"
  );
  timeline_temp_table();
  db_prepare(&q, "SELECT * FROM timeline ORDER BY sortby DESC /*scan*/");
  for(i=0; i<(int)(sizeof(aYearsAgo)/sizeof(aYearsAgo[0])); i++){
    int iAgo = aYearsAgo[i];
    char *zThis = db_text(0, "SELECT date(%Q,'-%d years')", zToday, iAgo);
    Blob sql;
    char *zId;
    if( strcmp(zThis, zStartOfProject)<0 ) break;
    blob_init(&sql, 0, 0);
    blob_append(&sql, "INSERT OR IGNORE INTO timeline ", -1);
    blob_append(&sql, timeline_query_for_www(), -1);
    blob_append_sql(&sql,
       " AND %Q=date(event.mtime,toLocal()) "
       " AND event.mtime BETWEEN julianday(%Q,'-1 day')"
             " AND julianday(%Q,'+2 days')",
       zThis, zThis, zThis
    );
    db_multi_exec("DELETE FROM timeline; %s;", blob_sql_text(&sql));
    blob_reset(&sql);
    if( db_int(0, "SELECT count(*) FROM timeline")==0 ){
      continue;
    }
    zId = db_text(0, "SELECT timestamp FROM timeline"
                     " ORDER BY sortby DESC LIMIT 1");
    @ <h2>%d(iAgo) Year%s(iAgo>1?"s":"") Ago
    @ <small>%z(href("%R/timeline?c=%t",zId))(more context)</a>\
    @ </small></h2>
    www_print_timeline(&q, TIMELINE_GRAPH, 0, 0, 0, 0, 0, 0);
  }
  db_finalize(&q);
  style_finish_page();
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
      style_table_sorter();
      @ <div class="brlist">
      @ <table class='sortable' data-column-types='tttt' data-init-sort='2'>
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
  }
  style_finish_page();
}
