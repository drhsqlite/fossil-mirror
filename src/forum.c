/*
** Copyright (c) 2018 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** This file contains code used to generate the user forum.
*/
#include "config.h"
#include <assert.h>
#include "forum.h"

/*
** Display all posts in a forum thread in chronological order
*/
static void forum_thread_chronological(int froot){
  Stmt q;
  db_prepare(&q, "SELECT fpid FROM forumpost WHERE froot=%d"
                 " ORDER BY fmtime", froot);
  while( db_step(&q)==SQLITE_ROW ){
    int fpid = db_column_int(&q, 0);
    Manifest *pPost = manifest_get(fpid, CFTYPE_FORUM, 0);
    if( pPost==0 ) continue;
    manifest_destroy(pPost);
  }
  db_finalize(&q);
}

/*
** WEBPAGE: forumthread
**
** Show all forum messages associated with a particular message thread.
**
** Query parameters:
**
**   name=X        The hash of the first post of the thread.  REQUIRED
*/
void forumthread_page(void){
  int fpid;
  int froot;
  const char *zName = P("name");
  login_check_credentials();
  if( !g.perm.RdForum ){
    login_needed(g.anon.RdForum);
    return;
  }
  style_header("Forum");
  if( zName==0 ){
    @ <p class='generalError'>Missing name= query parameter</p>
    style_footer();
    return;
  }
  fpid = symbolic_name_to_rid(zName, "f");
  if( fpid<=0 ){
    @ <p class='generalError'>Unknown or ambiguous forum id in the "name="
    @ query parameter</p>
    style_footer();
    return;
  }
  froot = db_int(0, "SELECT froot FROM forumpost WHERE fpid=%d", fpid);
  if( froot==0 ){
    @ <p class='generalError'>Invalid forum id in the "name="
    @ query parameter</p>
    style_footer();
    return;
  }
  forum_thread_chronological(froot);
  style_footer();
}

/*
** Return true if a forum post should be moderated.
*/
static int forum_need_moderation(void){
  return !g.perm.WrTForum && !g.perm.ModForum && P("domod")==0;
}

/*
** Add a new Forum Post artifact to the repository.
*/
static void forum_post(
  const char *zTitle,          /* Title.  NULL for replies */
  int iInReplyTo,              /* Post replying to.  0 for new threads */
  int iEdit,                   /* Post being edited, or zero for a new post */
  const char *zUser,           /* Username.  NULL means use login name */
  const char *zMimetype,       /* Mimetype of content. */
  const char *zContent         /* Content */
){
  Blob x, cksum;
  char *zDate;
  schema_forum();
  blob_init(&x, 0, 0);
  zDate = date_in_standard_format("now");
  blob_appendf(&x, "D %s\n", zDate);
  fossil_free(zDate);
  if( zTitle ){
    blob_appendf(&x, "H %F\n", zTitle);
  }else{
    char *zG = db_text(0, 
       "SELECT uuid FROM blob, forumpost"
       " WHERE blob.rid==forumpost.froot"
       "   AND forumpost.fpid=%d", iInReplyTo);
    char *zI;
    if( zG==0 ) goto forum_post_error;
    blob_appendf(&x, "G %s\n", zG);
    fossil_free(zG);
    zI = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", iInReplyTo);
    if( zI==0 ) goto forum_post_error;
    blob_appendf(&x, "I %s\n", zI);
    fossil_free(zI);
  }
  if( fossil_strcmp(zMimetype,"text/x-fossil-wiki")!=0 ){
    blob_appendf(&x, "N %s\n", zMimetype);
  }
  if( iEdit>0 ){
    char *zP = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", iEdit);
    if( zP==0 ) goto forum_post_error;
    blob_appendf(&x, "P %s\n", zP);
    fossil_free(zP);
  }
  if( zUser==0 ){
    if( login_is_nobody() ){
      zUser = "anonymous";
    }else{
      zUser = login_name();
    }
  }
  blob_appendf(&x, "U %F\n", zUser);
  blob_appendf(&x, "W %d\n%s\n", strlen(zContent), zContent);
  md5sum_blob(&x, &cksum);
  blob_appendf(&x, "Z %b\n", &cksum);
  blob_reset(&cksum);
  if( P("dryrun") ){
    @ <pre>%h(blob_str(&x))</pre><hr>
  }else{
    wiki_put(&x, 0, forum_need_moderation());
    return;
  }

forum_post_error:
  blob_reset(&x);
}

/*
** Render a forum post for display
*/
void forum_render(const char *zMimetype, const char *zContent){
  Blob x;
  blob_init(&x, zContent, -1);
  wiki_render_by_mimetype(&x, zMimetype);
  blob_reset(&x);
}

/*
** WEBPAGE: forumnew
** WEBPAGE: test-forumnew
**
** Start a new forum thread.  The /test-forumnew works just like
** /forumnew except that it provides additional controls for testing
** and debugging.
*/
void forumnew_page(void){
  const char *zTitle = PDT("t","");
  const char *zMimetype = PD("mt","text/x-fossil-wiki");
  const char *zContent = PDT("x","");
  login_check_credentials();
  if( !g.perm.WrForum ){
    login_needed(g.anon.WrForum);
    return;
  }
  if( P("submit") ){
    forum_post(zTitle, 0, 0, 0, zMimetype, zContent);
  }
  if( P("preview") ){
    @ <h1>%h(zTitle)</h1>
    forum_render(zMimetype, zContent);
    @ <hr>
  }
  style_header("New Forum Thread");
  @ <form action="%R/%s(g.zPath)" method="POST">
  @ Title: <input type="input" name="t" value="%h(zTitle)" size="50"><br>
  @ Markup style:
  mimetype_option_menu(zMimetype);
  @ <br><textarea name="x" class="wikiedit" cols="80" \
  @ rows="25" wrap="virtual">%h(zContent)</textarea><br>
  @ <input type="submit" name="preview" value="Preview">
  if( P("preview") ){
    @ <input type="submit" name="submit" value="Submit">
  }else{
    @ <input type="submit" name="submit" value="Submit" disabled>
  }
  if( g.zPath[0]=='t' ){
    /* For the test-forumnew page add these extra debugging controls */
    @ <br><label><input type="checkbox" name="dryrun" %s(PCK("dryrun"))> \
    @ Dry run</label>
    @ <br><label><input type="checkbox" name="domod" %s(PCK("domod"))> \
    @ Require moderator approval</label>
  }
  @ </form>
  style_footer();
}

/*
** WEBPAGE: forumreply
**
** Reply to a forum message.
** Query parameters:
**
**   name=X        Hash of the post to reply to.  REQUIRED
*/
void forumreply_page(void){
  style_header("Pending");
  @ TBD...
  style_footer();
}

/*
** WEBPAGE: forumedit
**
** Edit an existing forum message.
** Query parameters:
**
**   name=X        Hash of the post to be editted.  REQUIRED
*/
void forumedit_page(void){
  style_header("Pending");
  @ TBD...
  style_footer();
}
