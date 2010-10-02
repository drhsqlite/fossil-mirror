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
** WEBPAGE: event
** URL: /event?name=EVENTID
**
** Display an existing event identified by EVENTID
*/
void event_page(void){
  char *zTag;
  int rid = 0;
  const char *zEventId;
  char *zETime;
  Manifest m;
  Blob content;
  Blob fullbody;
  Blob title;
  Blob tail;


  /* wiki-read privilege is needed in order to read events.
  */
  login_check_credentials();
  if( !g.okRdWiki ){
    login_needed();
    return;
  }

  zEventId = PD("name","nil");
  zTag = mprintf("event-%s", zEventId);
  rid = db_int(0, 
    "SELECT rid FROM tagxref"
    " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
    " ORDER BY mtime DESC", zTag
  );
  free(zTag);
  if( rid==0 ){
    style_header("No Such Event");
    @ Unknown event: %h(zEventId)
    style_footer();
    return;
  }

  /* Extract the event content.
  */
  memset(&m, 0, sizeof(m));
  blob_zero(&m.content);
  content_get(rid, &content);
  manifest_parse(&m, &content);
  if( m.type!=CFTYPE_EVENT ){
    fossil_panic("Object #%d is not an event", rid);
  }
  zETime = db_text(0, "SELECT datetime(%.17g)", m.rEventDate);
  blob_init(&fullbody, m.zWiki, -1);
  if( wiki_find_title(&fullbody, &title, &tail) ){
    style_header(blob_str(&title));
  }else{
    style_header("Event %s", zEventId);
    tail = fullbody;
  }
  wiki_convert(&tail, 0, 0);
  style_footer();
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
  char *zComment = (char*)P("c");

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
  if( !g.okWrite || (rid && !g.okWrWiki) || (!rid && !g.okNewWiki) ){
    login_needed();
    return;
  }

  /* If editing an existing event, extract the key fields to use as
  ** a starting point for the edit.
  */
  if( rid && (zBody==0 || zETime==0 || zComment==0) ){
    Manifest m;
    Blob content;
    memset(&m, 0, sizeof(m));
    blob_zero(&m.content);
    content_get(rid, &content);
    manifest_parse(&m, &content);
    if( m.type==CFTYPE_EVENT ){
      if( zBody==0 ) zBody = m.zWiki;
      if( zETime==0 ){
        zETime = db_text(0, "SELECT datetime(%.17g)", m.rEventDate);
      }
      if( zComment==0 ) zComment = m.zComment;
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
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10] = 'T';
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
    if( g.zLogin ){
      blob_appendf(&event, "U %F\n", g.zLogin);
    }
    blob_appendf(&event, "W %d\n%s\n", strlen(zBody), zBody);
    md5sum_blob(&event, &cksum);
    blob_appendf(&event, "Z %b\n", &cksum);
    blob_reset(&cksum);
    nrid = content_put(&event, 0, 0);
    db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nrid);
    manifest_crosslink(nrid, &event);
    blob_reset(&event);
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
    blob_zero(&event);
    blob_append(&event, zBody, -1);
    @ Page content:<hr />
    wiki_convert(&event, 0, 0);
    @ <hr />
    blob_reset(&event);
  }
  for(n=2, z=zBody; z[0]; z++){
    if( z[0]=='\n' ) n++;
  }
  if( n<20 ) n = 20;
  if( n>40 ) n = 40;
  @ <form method="post" action="%s(g.zBaseURL)/eventedit"><div>
  login_insert_csrf_secret();
  @ <input type="hidden" name="name" value="%h(zEventId)" />
  @ <p>Event time:
  @ <input type="text" name="t" size="25" value="%h(zETime)" /></p>
  @ <p>Summary of the event as it appears on a timeline:<br />
  @ <textarea name="c" class="eventedit" cols="80" 
  @  rows="3" wrap="virtual">%h(zComment)</textarea></p>
  @ <p>Full content of the event.  Put the page title in
  @ &lt;title&gt;...&lt;/title&gt; markup at the beginning:<br />
  @ <textarea name="w" class="eventedit" cols="80" 
  @  rows="%d(n)" wrap="virtual">%h(zBody)</textarea></p>
  @ </p><input type="submit" name="preview" value="Preview Your Changes" />
  @ <input type="submit" name="submit" value="Apply These Changes" />
  @ <input type="submit" name="cancel" value="Cancel" /></p>
  @ </div></form>
  style_footer();
}
