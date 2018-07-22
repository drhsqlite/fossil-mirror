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
** WEBPAGE: forumnew
**
** Start a new forum thread.
*/
void forumnew_page(void){
  style_header("Pending");
  @ TBD...
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
