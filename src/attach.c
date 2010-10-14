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
** This file contains code for dealing with attachments.
*/
#include "config.h"
#include "attach.h"
#include <assert.h>

/*
** WEBPAGE: attachlist
**
**    tkt=TICKETUUID
**    page=WIKIPAGE
**
** List attachments.
** Either one of tkt= or page= are supplied or neither.  If neither
** are given, all attachments are listed.  If one is given, only
** attachments for the designated ticket or wiki page are shown.
** TICKETUUID must be complete 
*/
void attachlist_page(void){
  const char *zPage = P("page");
  const char *zTkt = P("tkt");
  Blob sql;
  Stmt q;

  if( zPage && zTkt ) zTkt = 0;
  login_check_credentials();
  blob_zero(&sql);
  blob_append(&sql,
     "SELECT datetime(mtime,'localtime'), src, target, filename, comment, user"
     "  FROM attachment",
     -1
  );
  if( zPage ){
    if( g.okRdWiki==0 ) login_needed();
    style_header("Attachments To %h", zPage);
    blob_appendf(&sql, " WHERE target=%Q", zPage);
  }else if( zTkt ){
    if( g.okRdTkt==0 ) login_needed();
    style_header("Attachments To Ticket %.10s", zTkt);
    blob_appendf(&sql, " WHERE target GLOB '%q*'", zTkt);
  }else{
    if( g.okRdTkt==0 && g.okRdWiki==0 ) login_needed();
    style_header("All Attachments");
  }
  blob_appendf(&sql, " ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_str(&sql));
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 0);
    const char *zSrc = db_column_text(&q, 1);
    const char *zTarget = db_column_text(&q, 2);
    const char *zFilename = db_column_text(&q, 3);
    const char *zComment = db_column_text(&q, 4);
    const char *zUser = db_column_text(&q, 5);
    int i;
    char *zUrlTail;
    for(i=0; zFilename[i]; i++){
      if( zFilename[i]=='/' && zFilename[i+1]!=0 ){ 
        zFilename = &zFilename[i+1];
        i = -1;
      }
    }
    if( strlen(zTarget)==UUID_SIZE && validate16(zTarget,UUID_SIZE) ){
      zUrlTail = mprintf("tkt=%s&amp;file=%t", zTarget, zFilename);
    }else{
      zUrlTail = mprintf("page=%t&amp;file=%t", zTarget, zFilename);
    }
    @
    @ <p><a href="/attachview?%s(zUrlTail)">%h(zFilename)</a>
    @ [<a href="/attachdownload/%t(zFilename)?%s(zUrlTail)">download</a>]<br />
    if( zComment ) while( fossil_isspace(zComment[0]) ) zComment++;
    if( zComment && zComment[0] ){
      @ %w(zComment)<br />
    }
    if( zPage==0 && zTkt==0 ){
      if( zSrc==0 || zSrc[0]==0 ){
        zSrc = "Deleted from";
      }else {
        zSrc = "Added to";
      }
      if( strlen(zTarget)==UUID_SIZE && validate16(zTarget, UUID_SIZE) ){
        @ %s(zSrc) ticket <a href="%s(g.zTop)/tktview?name=%s(zTarget)">
        @ %S(zTarget)</a>
      }else{
        @ %s(zSrc) wiki page <a href="%s(g.zTop)/wiki?name=%t(zTarget)">
        @ %h(zTarget)</a>
      }
    }else{
      if( zSrc==0 || zSrc[0]==0 ){
        @ Deleted
      }else {
        @ Added
      }
    }
    @ by %h(zUser) on
    hyperlink_to_date(zDate, ".");
    free(zUrlTail);
  }
  db_finalize(&q);
  style_footer();
  return;
}

/*
** WEBPAGE: attachdownload
** WEBPAGE: attachimage
** WEBPAGE: attachview
**
**    tkt=TICKETUUID
**    page=WIKIPAGE
**    file=FILENAME
**    attachid=ID
**
** List attachments.
*/
void attachview_page(void){
  const char *zPage = P("page");
  const char *zTkt = P("tkt");
  const char *zFile = P("file");
  const char *zTarget = 0;
  int attachid = atoi(PD("attachid","0"));
  char *zUUID;

  if( zPage && zTkt ) zTkt = 0;
  if( zFile==0 ) fossil_redirect_home();
  login_check_credentials();
  if( zPage ){
    if( g.okRdWiki==0 ) login_needed();
    zTarget = zPage;
  }else if( zTkt ){
    if( g.okRdTkt==0 ) login_needed();
    zTarget = zTkt;
  }else{
    fossil_redirect_home();
  }
  if( attachid>0 ){
    zUUID = db_text(0,
       "SELECT coalesce(src,'x') FROM attachment"
       " WHERE target=%Q AND attachid=%d",
       zTarget, attachid
    );
  }else{
    zUUID = db_text(0,
       "SELECT coalesce(src,'x') FROM attachment"
       " WHERE target=%Q AND filename=%Q"
       " ORDER BY mtime DESC LIMIT 1",
       zTarget, zFile
    );
  }
  if( zUUID==0 || zUUID[0]==0 ){
    style_header("No Such Attachment");
    @ No such attachment....
    style_footer();
    return;
  }else if( zUUID[0]=='x' ){
    style_header("Missing");
    @ Attachment has been deleted
    style_footer();
    return;
  }
  g.okRead = 1;
  cgi_replace_parameter("name",zUUID);
  if( strcmp(g.zPath,"attachview")==0 ){
    artifact_page();
  }else{
    cgi_replace_parameter("m", mimetype_from_name(zFile));
    rawartifact_page();
  }
}


/*
** WEBPAGE: attachadd
**
**    tkt=TICKETUUID
**    page=WIKIPAGE
**    from=URL
**
** Add a new attachment.
*/
void attachadd_page(void){
  const char *zPage = P("page");
  const char *zTkt = P("tkt");
  const char *zFrom = P("from");
  const char *aContent = P("f");
  const char *zName = PD("f:filename","unknown");
  const char *zTarget;
  const char *zTargetType;
  int szContent = atoi(PD("f:bytes","0"));

  if( P("cancel") ) cgi_redirect(zFrom);
  if( zPage && zTkt ) fossil_redirect_home();
  if( zPage==0 && zTkt==0 ) fossil_redirect_home();
  login_check_credentials();
  if( zPage ){
    if( g.okApndWiki==0 || g.okAttach==0 ) login_needed();
    if( !db_exists("SELECT 1 FROM tag WHERE tagname='wiki-%q'", zPage) ){
      fossil_redirect_home();
    }
    zTarget = zPage;
    zTargetType = mprintf("Wiki Page <a href=\"%s/wiki?name=%h\">%h</a>",
                           g.zTop, zPage, zPage);
  }else{
    if( g.okApndTkt==0 || g.okAttach==0 ) login_needed();
    if( !db_exists("SELECT 1 FROM tag WHERE tagname='tkt-%q'", zTkt) ){
      zTkt = db_text(0, "SELECT substr(tagname,5) FROM tag" 
                        " WHERE tagname GLOB 'tkt-%q*'", zTkt);
      if( zTkt==0 ) fossil_redirect_home();
    }
    zTarget = zTkt;
    zTargetType = mprintf("Ticket <a href=\"%s/tktview/%S\">%S</a>",
                          g.zTop, zTkt, zTkt);
  }
  if( zFrom==0 ) zFrom = mprintf("%s/home", g.zTop);
  if( P("cancel") ){
    cgi_redirect(zFrom);
  }
  if( P("ok") && szContent>0 ){
    Blob content;
    Blob manifest;
    Blob cksum;
    char *zUUID;
    const char *zComment;
    char *zDate;
    int rid;
    int i, n;

    db_begin_transaction();
    blob_init(&content, aContent, szContent);
    rid = content_put(&content, 0, 0);
    zUUID = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    blob_zero(&manifest);
    for(i=n=0; zName[i]; i++){
      if( zName[i]=='/' || zName[i]=='\\' ) n = i;
    }
    zName += n;
    if( zName[0]==0 ) zName = "unknown";
    blob_appendf(&manifest, "A %F %F %s\n", zName, zTarget, zUUID);
    zComment = PD("comment", "");
    while( fossil_isspace(zComment[0]) ) zComment++;
    n = strlen(zComment);
    while( n>0 && fossil_isspace(zComment[n-1]) ){ n--; }
    if( n>0 ){
      blob_appendf(&manifest, "C %F\n", zComment);
    }
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10] = 'T';
    blob_appendf(&manifest, "D %s\n", zDate);
    blob_appendf(&manifest, "U %F\n", g.zLogin ? g.zLogin : "nobody");
    md5sum_blob(&manifest, &cksum);
    blob_appendf(&manifest, "Z %b\n", &cksum);
    rid = content_put(&manifest, 0, 0);
    manifest_crosslink(rid, &manifest);
    db_end_transaction(0);
    cgi_redirect(zFrom);
  }
  style_header("Add Attachment");
  @ <h2>Add Attachment To %s(zTargetType)</h2>
  @ <form action="%s(g.zBaseURL)/attachadd" method="post"
  @  enctype="multipart/form-data"><div>
  @ File to Attach:
  @ <input type="file" name="f" size="60" /><br />
  @ Description:<br />
  @ <textarea name="comment" cols="80" rows="5" wrap="virtual"></textarea><br />
  if( zTkt ){
    @ <input type="hidden" name="tkt" value="%h(zTkt)" />
  }else{
    @ <input type="hidden" name="page" value="%h(zPage)" />
  }
  @ <input type="hidden" name="from" value="%h(zFrom)" />
  @ <input type="submit" name="ok" value="Add Attachment" />
  @ <input type="submit" name="cancel" value="Cancel" />
  @ </div></form>
  style_footer();
}


/*
** WEBPAGE: attachdelete
**
**    tkt=TICKETUUID
**    page=WIKIPAGE
**    file=FILENAME
**
** "Delete" an attachment.  Because objects in Fossil are immutable
** the attachment isn't really deleted.  Instead, we change the content
** of the attachment to NULL, which the system understands as being
** deleted.  Historical values of the attachment are preserved.
*/
void attachdel_page(void){
  const char *zPage = P("page");
  const char *zTkt = P("tkt");
  const char *zFile = P("file");
  const char *zFrom = P("from");
  const char *zTarget;

  if( zPage && zTkt ) fossil_redirect_home();
  if( zPage==0 && zTkt==0 ) fossil_redirect_home();
  if( zFile==0 ) fossil_redirect_home();
  login_check_credentials();
  if( zPage ){
    if( g.okWrWiki==0 || g.okAttach==0 ) login_needed();
    zTarget = zPage;
  }else{
    if( g.okWrTkt==0 || g.okAttach==0 ) login_needed();
    zTarget = zTkt;
  }
  if( zFrom==0 ) zFrom = mprintf("%s/home", g.zTop);
  if( P("cancel") ){
    cgi_redirect(zFrom);
  }
  if( P("confirm") ){
    int i, n, rid;
    char *zDate;
    Blob manifest;
    Blob cksum;

    db_begin_transaction();
    blob_zero(&manifest);
    for(i=n=0; zFile[i]; i++){
      if( zFile[i]=='/' || zFile[i]=='\\' ) n = i;
    }
    zFile += n;
    if( zFile[0]==0 ) zFile = "unknown";
    blob_appendf(&manifest, "A %F %F\n", zFile, zTarget);
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10] = 'T';
    blob_appendf(&manifest, "D %s\n", zDate);
    blob_appendf(&manifest, "U %F\n", g.zLogin ? g.zLogin : "nobody");
    md5sum_blob(&manifest, &cksum);
    blob_appendf(&manifest, "Z %b\n", &cksum);
    rid = content_put(&manifest, 0, 0);
    manifest_crosslink(rid, &manifest);
    db_end_transaction(0);
    cgi_redirect(zFrom);
  }    
  style_header("Delete Attachment");
  @ <form action="%s(g.zBaseURL)/attachdelete" method="post"><div>
  @ <p>Confirm that you want to delete the attachment named
  @ "%h(zFile)" on %s(zTkt?"ticket":"wiki page") %h(zTarget):<br /></p>
  if( zTkt ){
    @ <input type="hidden" name="tkt" value="%h(zTkt)" />
  }else{
    @ <input type="hidden" name="page" value="%h(zPage)" />
  }
  @ <input type="hidden" name="file" value="%h(zFile)" />
  @ <input type="hidden" name="from" value="%h(zFrom)" />
  @ <input type="submit" name="confirm" value="Delete" />
  @ <input type="submit" name="cancel" value="Cancel" />
  @ </div></form>
  style_footer();

}
