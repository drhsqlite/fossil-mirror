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
** Render a forum post for display
*/
void forum_render(const char *zMimetype, const char *zContent){
  Blob x;
  blob_init(&x, zContent, -1);
  wiki_render_by_mimetype(&x, zMimetype);
  blob_reset(&x);
}

/*
** Display all posts in a forum thread in chronological order
*/
static void forum_thread_chronological(int froot){
  Stmt q;
  int i = 0;
  db_prepare(&q,
      "SELECT fpid, fprev, firt, uuid, datetime(fmtime,'unixepoch')\n"
      " FROM forumpost, blob\n"
      " WHERE froot=%d AND rid=fpid\n"
      " ORDER BY fmtime", froot);
  while( db_step(&q)==SQLITE_ROW ){
    int fpid = db_column_int(&q, 0);
    int fprev = db_column_int(&q, 1);
    int firt = db_column_int(&q, 2);
    const char *zUuid = db_column_text(&q, 3);
    const char *zDate = db_column_text(&q, 4);
    Manifest *pPost = manifest_get(fpid, CFTYPE_FORUM, 0);
    if( pPost==0 ) continue;
    if( i>0 ){
      @ <hr>
    }
    i++;
    if( pPost->zThreadTitle ){
      @ <h1>%h(pPost->zThreadTitle)</h1>
    }
    @ <p>By %h(pPost->zUser) on %h(zDate)
    if( fprev ){
      @ edit of %d(fprev) %h(pPost->azParent[0])
    }
    if( firt ){
      @ in reply to %d(firt) %h(pPost->zInReplyTo)
    }
    forum_render(pPost->zMimetype, pPost->zWiki);
    if( g.perm.WrForum ){
      int sameUser = login_is_individual()
                     && fossil_strcmp(pPost->zUser, g.zLogin)==0;
      int isPrivate = content_is_private(fpid);
      @ <p><form action="%R/forumedit" method="POST">
      @ <input type="hidden" name="fpid" value="%s(zUuid)">
      if( !isPrivate ){
        /* Reply and Edit are only available if the post has already
        ** been approved */
        @ <input type="submit" name="reply" value="Reply">
        if( g.perm.Admin || sameUser ){
          @ <input type="submit" name="edit" value="Edit">
          @ <input type="submit" name="nullout" value="Delete">
        }
      }else if( g.perm.ModForum ){
        /* Provide moderators with moderation buttons for posts that
        ** are pending moderation */
        @ <input type="submit" name="approve" value="Approve">
        @ <input type="submit" name="reject" value="Reject">
      }else if( sameUser ){
        /* A post that is pending moderation can be deleted by the
        ** person who originally submitted the post */
        @ <input type="submit" name="reject" value="Delete">
      }
      @ </form></p>
    }
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
**
** Return true if a redirect occurs.
*/
static int forum_post(
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
    int nrid = wiki_put(&x, 0, forum_need_moderation());
    cgi_redirectf("%R/forumthread/%S", rid_to_uuid(nrid));
    return 1;
  }

forum_post_error:
  blob_reset(&x);
  return 0;
}

/*
** Paint the form elements for entering a Forum post
*/
static void forum_entry_widget(
  const char *zTitle,
  const char *zMimetype,
  const char *zContent
){
  if( zTitle ){
    @ Title: <input type="input" name="title" value="%h(zTitle)" size="50"><br>
  }
  @ Markup style:
  mimetype_option_menu(zMimetype);
  @ <br><textarea name="content" class="wikiedit" cols="80" \
  @ rows="25" wrap="virtual">%h(zContent)</textarea><br>
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
  const char *zTitle = PDT("title","");
  const char *zMimetype = PD("mimetype","text/x-fossil-wiki");
  const char *zContent = PDT("content","");
  login_check_credentials();
  if( !g.perm.WrForum ){
    login_needed(g.anon.WrForum);
    return;
  }
  if( P("submit") ){
    if( forum_post(zTitle, 0, 0, 0, zMimetype, zContent) ) return;
  }
  if( P("preview") ){
    @ <h1>%h(zTitle)</h1>
    forum_render(zMimetype, zContent);
    @ <hr>
  }
  style_header("New Forum Thread");
  @ <form action="%R/%s(g.zPath)" method="POST">
  forum_entry_widget(zTitle, zMimetype, zContent);
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
  int fpid;
  Manifest *pPost;

  login_check_credentials();
  if( !g.perm.WrForum ){
    login_needed(g.anon.WrForum);
    return;
  }
  fpid = symbolic_name_to_rid(PD("fpid",""), "f");
  if( fpid<=0 || (pPost = manifest_get(fpid, CFTYPE_FORUM, 0))==0 ){
    webpage_error("Missing or invalid fpid query parameter");
    return;
  }
  if( g.perm.ModForum ){
    if( P("approve") ){
      webpage_not_yet_implemented();
      return;
    }
    if( P("reject") ){
      webpage_not_yet_implemented();
      return;
    }
  }
  if( P("submitdryrun") ){
    cgi_set_parameter_nocopy("dryrun","1",1);
    cgi_set_parameter_nocopy("submit","1",1);
  }
  if( P("submit") && cgi_csrf_safe(1) ){
    int done = 1;
    const char *zMimetype = PD("mimetype","text/x-fossil-wiki");
    const char *zContent = PDT("content","");
    if( P("reply") ){
      done = forum_post(0, fpid, 0, 0, zMimetype, zContent);
    }else if( P("edit") ){
      done = forum_post(0, 0, fpid, 0, zMimetype, zContent);
    }else{
      webpage_error("Need one of 'edit' or 'reply' query parameters");
    }
    if( done ) return;
  }
  if( P("edit") ){
    /* Provide an edit to the fpid post */
    webpage_not_yet_implemented();
    return;
  }else{
    const char *zMimetype = PD("mimetype","text/x-fossil-wiki");
    const char *zContent = PDT("content","");
    style_header("Forum Reply");
    @ <h1>Replying To:</h1>
    forum_render(pPost->zMimetype, pPost->zWiki);
    if( P("preview") ){
      @ <h1>Preview:</h1>
      forum_render(zMimetype,zContent);
    }
    @ <h1>Enter A Reply:</h1>
    @ <form action="%R/forumedit" method="POST">
    @ <input type="hidden" name="fpid" value="%h(P("fpid"))">
    @ <input type="hidden" name="reply" value="1">
    forum_entry_widget(0, zMimetype, zContent);
    @ <input type="submit" name="preview" value="Preview">
    if( P("preview") ){
      @ <input type="submit" name="submit" value="Submit">
      if( g.perm.Setup ){
        @ <input type="submit" name="submitdryrun" value="Dry Run">
      }
    }
    @ </form>
  }
  style_footer();
}
