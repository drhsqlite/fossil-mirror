/*
** Copyright (c) 2010 D. Richard Hipp
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
** This file contains code to do formatting of event messages:
**
**     Milestones
**     Blog posts
**     New articles
**     Process checkpoints
**     Announcements
*/
#include <assert.h>
#include <ctype.h>
#include "config.h"
#include "event.h"

/*
** Output a hyperlink to an event given its tagid.
*/
void hyperlink_to_event_tagid(int tagid){
  char *zEventId;
  zEventId = db_text(0, "SELECT substr(tagname, 7) FROM tag WHERE tagid=%d",
                     tagid);
  @ [%z(href("%R/event/%s",zEventId))%S(zEventId)</a>]
  free(zEventId);
}

/*
** WEBPAGE: event
** URL: /event
** PARAMETERS:
**
**  name=EVENTID      // Identify the event to display EVENTID must be complete
**  aid=ARTIFACTID    // Which specific version of the event.  Optional.
**  v=BOOLEAN         // Show details if TRUE.  Default is FALSE.  Optional.
**
** Display an existing event identified by EVENTID
*/
void event_page(void){
  int rid = 0;             /* rid of the event artifact */
  char *zUuid;             /* UUID corresponding to rid */
  const char *zEventId;    /* Event identifier */
  const char *zVerbose;    /* Value of verbose option */
  char *zETime;            /* Time of the event */
  char *zATime;            /* Time the artifact was created */
  int specRid;             /* rid specified by aid= parameter */
  int prevRid, nextRid;    /* Previous or next edits of this event */
  Manifest *pEvent;        /* Parsed event artifact */
  Blob fullbody;           /* Complete content of the event body */
  Blob title;              /* Title extracted from the event body */
  Blob tail;               /* Event body that comes after the title */
  Stmt q1;                 /* Query to search for the event */
  int verboseFlag;         /* True to show details */


  /* wiki-read privilege is needed in order to read events.
  */
  login_check_credentials();
  if( !g.perm.RdWiki ){
    login_needed();
    return;
  }

  zEventId = P("name");
  if( zEventId==0 ){ fossil_redirect_home(); return; }
  zUuid = (char*)P("aid");
  specRid = zUuid ? uuid_to_rid(zUuid, 0) : 0;
  rid = nextRid = prevRid = 0;
  db_prepare(&q1,
     "SELECT rid FROM tagxref"
     " WHERE tagid=(SELECT tagid FROM tag WHERE tagname GLOB 'event-%q*')"
     " ORDER BY mtime DESC",
     zEventId
  );
  while( db_step(&q1)==SQLITE_ROW ){
    nextRid = rid;
    rid = db_column_int(&q1, 0);
    if( specRid==0 || specRid==rid ){
      if( db_step(&q1)==SQLITE_ROW ){
        prevRid = db_column_int(&q1, 0);
      }
      break;
    }
  }
  db_finalize(&q1);
  if( rid==0 || (specRid!=0 && specRid!=rid) ){
    style_header("No Such Event");
    @ Cannot locate specified event
    style_footer();
    return;
  }
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  zVerbose = P("v");
  if( !zVerbose ){
    zVerbose = P("verbose");
  }
  if( !zVerbose ){
    zVerbose = P("detail"); /* deprecated */
  }
  verboseFlag = (zVerbose!=0) && (*zVerbose!=0) && !is_false(zVerbose);

  /* Extract the event content.
  */
  pEvent = manifest_get(rid, CFTYPE_EVENT);
  if( pEvent==0 ){
    fossil_panic("Object #%d is not an event", rid);
  }
  blob_init(&fullbody, pEvent->zWiki, -1);
  if( wiki_find_title(&fullbody, &title, &tail) ){
    style_header(blob_str(&title));
  }else{
    style_header("Event %S", zEventId);
    tail = fullbody;
  }
  if( g.perm.WrWiki && g.perm.Write && nextRid==0 ){
    style_submenu_element("Edit", "Edit", "%s/eventedit?name=%s",
                          g.zTop, zEventId);
  }
  zETime = db_text(0, "SELECT datetime(%.17g)", pEvent->rEventDate);
  style_submenu_element("Context", "Context", "%s/timeline?c=%T",
                        g.zTop, zETime);
  if( g.perm.Hyperlink ){
    if( verboseFlag ){
      style_submenu_element("Plain", "Plain", "%s/event?name=%s&aid=%s",
                            g.zTop, zEventId, zUuid);
      if( nextRid ){
        char *zNext;
        zNext = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", nextRid);
        style_submenu_element("Next", "Next",
                              "%s/event?name=%s&aid=%s&v",
                              g.zTop, zEventId, zNext);
        free(zNext);
      }
      if( prevRid ){
        char *zPrev;
        zPrev = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", prevRid);
        style_submenu_element("Prev", "Prev",
                              "%s/event?name=%s&aid=%s&v",
                              g.zTop, zEventId, zPrev);
        free(zPrev);
      }
    }else{
      style_submenu_element("Detail", "Detail",
                            "%s/event?name=%s&aid=%s&v",
                            g.zTop, zEventId, zUuid);
    }
  }

  if( verboseFlag && g.perm.Hyperlink ){
    int i;
    const char *zClr = 0;
    Blob comment;

    zATime = db_text(0, "SELECT datetime(%.17g)", pEvent->rDate);
    @ <p>Event [%z(href("%R/artifact/%s",zUuid))%S(zUuid)</a>] at
    @ [%z(href("%R/timeline?c=%T",zETime))%s(zETime)</a>]
    @ entered by user <b>%h(pEvent->zUser)</b> on
    @ [%z(href("%R/timeline?c=%T",zATime))%s(zATime)</a>]:</p>
    @ <blockquote>
    for(i=0; i<pEvent->nTag; i++){
      if( fossil_strcmp(pEvent->aTag[i].zName,"+bgcolor")==0 ){
        zClr = pEvent->aTag[i].zValue;
      }
    }
    if( zClr && zClr[0]==0 ) zClr = 0;
    if( zClr ){
      @ <div style="background-color: %h(zClr);">
    }else{
      @ <div>
    }
    blob_init(&comment, pEvent->zComment, -1);
    wiki_convert(&comment, 0, WIKI_INLINE);
    blob_reset(&comment);
    @ </div>
    @ </blockquote><hr />
  }

  wiki_convert(&tail, 0, 0);
  style_footer();
  manifest_destroy(pEvent);
}

/*
** WEBPAGE: eventedit
** URL: /eventedit?name=EVENTID
**
** Edit an event.  If name is omitted, create a new event.
*/
void eventedit_page(void){
  char *zTag;
  int rid = 0;
  Blob event;
  const char *zEventId;
  char *zHtmlPageName;
  int n;
  const char *z;
  char *zBody = (char*)P("w");
  char *zETime = (char*)P("t");
  const char *zComment = P("c");
  const char *zTags = P("g");
  const char *zClr;

  if( zBody ){
    zBody = mprintf("%s", zBody);
  }
  login_check_credentials();
  zEventId = P("name");
  if( zEventId==0 ){
    zEventId = db_text(0, "SELECT lower(hex(randomblob(20)))");
  }else{
    int nEventId = strlen(zEventId);
    if( nEventId!=40 || !validate16(zEventId, 40) ){
      fossil_redirect_home();
      return;
    }
  }
  zTag = mprintf("event-%s", zEventId);
  rid = db_int(0,
    "SELECT rid FROM tagxref"
    " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
    " ORDER BY mtime DESC", zTag
  );
  free(zTag);

  /* Need both check-in and wiki-write or wiki-create privileges in order
  ** to edit/create an event.
  */
  if( !g.perm.Write || (rid && !g.perm.WrWiki) || (!rid && !g.perm.NewWiki) ){
    login_needed();
    return;
  }

  /* Figure out the color */
  if( rid ){
    zClr = db_text("", "SELECT bgcolor  FROM event WHERE objid=%d", rid);
  }else{
    zClr = "";
  }
  zClr = PD("clr",zClr);
  if( fossil_strcmp(zClr,"##")==0 ) zClr = PD("cclr","");


  /* If editing an existing event, extract the key fields to use as
  ** a starting point for the edit.
  */
  if( rid && (zBody==0 || zETime==0 || zComment==0 || zTags==0) ){
    Manifest *pEvent;
    pEvent = manifest_get(rid, CFTYPE_EVENT);
    if( pEvent && pEvent->type==CFTYPE_EVENT ){
      if( zBody==0 ) zBody = pEvent->zWiki;
      if( zETime==0 ){
        zETime = db_text(0, "SELECT datetime(%.17g)", pEvent->rEventDate);
      }
      if( zComment==0 ) zComment = pEvent->zComment;
    }
    if( zTags==0 ){
      zTags = db_text(0,
        "SELECT group_concat(substr(tagname,5),', ')"
        "  FROM tagxref, tag"
        " WHERE tagxref.rid=%d"
        "   AND tagxref.tagid=tag.tagid"
        "   AND tag.tagname GLOB 'sym-*'",
        rid
      );
    }
  }
  zETime = db_text(0, "SELECT coalesce(datetime(%Q),datetime('now'))", zETime);
  if( P("submit")!=0 && (zBody!=0 && zComment!=0) ){
    char *zDate;
    Blob cksum;
    int nrid;
    blob_zero(&event);
    db_begin_transaction();
    login_verify_csrf_secret();
    blob_appendf(&event, "C %F\n", zComment);
    zDate = date_in_standard_format("now");
    blob_appendf(&event, "D %s\n", zDate);
    free(zDate);
    zETime[10] = 'T';
    blob_appendf(&event, "E %s %s\n", zETime, zEventId);
    zETime[10] = ' ';
    if( rid ){
      char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      blob_appendf(&event, "P %s\n", zUuid);
      free(zUuid);
    }
    if( zClr && zClr[0] ){
      blob_appendf(&event, "T +bgcolor * %F\n", zClr);
    }
    if( zTags && zTags[0] ){
      Blob tags, one;
      int i, j;
      Stmt q;
      char *zBlob;

      /* Load the tags string into a blob */
      blob_zero(&tags);
      blob_append(&tags, zTags, -1);

      /* Collapse all sequences of whitespace and "," characters into
      ** a single space character */
      zBlob = blob_str(&tags);
      for(i=j=0; zBlob[i]; i++, j++){
        if( fossil_isspace(zBlob[i]) || zBlob[i]==',' ){
          while( fossil_isspace(zBlob[i+1]) ){ i++; }
          zBlob[j] = ' ';
        }else{
          zBlob[j] = zBlob[i];
        }
      }
      blob_resize(&tags, j);

      /* Parse out each tag and load it into a temporary table for sorting */
      db_multi_exec("CREATE TEMP TABLE newtags(x);");
      while( blob_token(&tags, &one) ){
        db_multi_exec("INSERT INTO newtags VALUES(%B)", &one);
      }
      blob_reset(&tags);

      /* Extract the tags in sorted order and make an entry in the
      ** artifact for each. */
      db_prepare(&q, "SELECT x FROM newtags ORDER BY x");
      while( db_step(&q)==SQLITE_ROW ){
        blob_appendf(&event, "T +sym-%F *\n", db_column_text(&q, 0));
      }
      db_finalize(&q);
    }
    if( g.zLogin ){
      blob_appendf(&event, "U %F\n", g.zLogin);
    }
    blob_appendf(&event, "W %d\n%s\n", strlen(zBody), zBody);
    md5sum_blob(&event, &cksum);
    blob_appendf(&event, "Z %b\n", &cksum);
    blob_reset(&cksum);
    nrid = content_put(&event);
    db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nrid);
    manifest_crosslink(nrid, &event);
    assert( blob_is_reset(&event) );
    content_deltify(rid, nrid, 0);
    db_end_transaction(0);
    cgi_redirectf("event?name=%T", zEventId);
  }
  if( P("cancel")!=0 ){
    cgi_redirectf("event?name=%T", zEventId);
    return;
  }
  if( zBody==0 ){
    zBody = mprintf("<i>Event Text</i>");
  }
  zHtmlPageName = mprintf("Edit Event %S", zEventId);
  style_header(zHtmlPageName);
  if( P("preview")!=0 ){
    Blob title, tail, com;
    @ <p><b>Timeline comment preview:</b></p>
    @ <blockquote>
    @ <table border="0">
    if( zClr && zClr[0] ){
      @ <tr><td style="background-color: %h(zClr);">
    }else{
      @ <tr><td>
    }
    blob_zero(&com);
    blob_append(&com, zComment, -1);
    wiki_convert(&com, 0, WIKI_INLINE|WIKI_NOBADLINKS);
    @ </td></tr></table>
    @ </blockquote>
    @ <p><b>Page content preview:</b><p>
    @ <blockquote>
    blob_zero(&event);
    blob_append(&event, zBody, -1);
    if( wiki_find_title(&event, &title, &tail) ){
      @ <h2 align="center">%h(blob_str(&title))</h2>
      wiki_convert(&tail, 0, 0);
    }else{
      wiki_convert(&event, 0, 0);
    }
    @ </blockquote><hr />
    blob_reset(&event);
  }
  for(n=2, z=zBody; z[0]; z++){
    if( z[0]=='\n' ) n++;
  }
  if( n<20 ) n = 20;
  if( n>40 ) n = 40;
  @ <form method="post" action="%s(g.zTop)/eventedit"><div>
  login_insert_csrf_secret();
  @ <input type="hidden" name="name" value="%h(zEventId)" />
  @ <table border="0" cellspacing="10">

  @ <tr><td align="right" valign="top"><b>Event&nbsp;Time:</b></td>
  @ <td valign="top">
  @   <input type="text" name="t" size="25" value="%h(zETime)" />
  @ </td></tr>

  @ <tr><td align="right" valign="top"><b>Timeline&nbsp;Comment:</b></td>
  @ <td valign="top">
  @ <textarea name="c" class="eventedit" cols="80"
  @  rows="3" wrap="virtual">%h(zComment)</textarea>
  @ </td></tr>

  @ <tr><td align="right" valign="top"><b>Background&nbsp;Color:</b></td>
  @ <td valign="top">
  render_color_chooser(0, zClr, 0, "clr", "cclr");
  @ </td></tr>

  @ <tr><td align="right" valign="top"><b>Tags:</b></td>
  @ <td valign="top">
  @   <input type="text" name="g" size="40" value="%h(zTags)" />
  @ </td></tr>

  @ <tr><td align="right" valign="top"><b>Page&nbsp;Content:</b></td>
  @ <td valign="top">
  @ <textarea name="w" class="eventedit" cols="80"
  @  rows="%d(n)" wrap="virtual">%h(zBody)</textarea>
  @ </td></tr>

  @ <tr><td colspan="2">
  @ <input type="submit" name="preview" value="Preview Your Changes" />
  @ <input type="submit" name="submit" value="Apply These Changes" />
  @ <input type="submit" name="cancel" value="Cancel" />
  @ </td></tr></table>
  @ </div></form>
  style_footer();
}
