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
** The schema for the tables that manage the forum, if forum is
** enabled.
*/
static const char zForumInit[] = 
@ CREATE TABLE repository.forumpost(
@   mpostid INTEGER PRIMARY KEY,  -- unique id for each post
@   mthreadid INTEGER,            -- thread to which this post belongs
@   uname TEXT,                   -- name of user
@   mtime REAL,                   -- julian day number
@   mstatus TEXT,                 -- status.  ('mod','ok')
@   mimetype TEXT,                -- Mimetype for mbody
@   ipaddr TEXT,                  -- IP address of post origin
@   inreplyto INT,                -- Parent posting
@   mbody TEXT                    -- Content of the post
@ );
@ CREATE INDEX repository.forumpost_x1 ON
@   forumpost(threadid,inreplyto,mtime);
@ CREATE INDEX repository.forumpost_x2 ON
@   forumpost(mtime) WHERE mstatus='mod';
@ CREATE TABLE repository.forumthread(
@   mthreadid INTEGER PRIMARY KEY,
@   mtitle TEXT,                  -- Title or subject line
@   mtime REAL,                   -- Most recent update
@   npost INT                     -- Number of posts on this thread
@ );
;

/*
** Create the forum tables in the schema if they do not already
** exist.
*/
static void forum_verify_schema(void){
  if( !db_table_exists("repository","forumpost") ){
    db_multi_exec(zForumInit /*works-like:""*/);
  }
}

/*
** WEBPAGE: forum
** URL: /forum
** Query parameters:
**
**    thread=N           Show posts from thread N
**    item=N             Show post N and its replies
**    
*/
void forum_page(void){
  int threadId = 0;
  int itemId;
  Stmt q;
  int i;

  login_check_credentials();
  if( !g.perm.RdForum ){ login_needed(g.anon.RdForum); return; }
  forum_verify_schema();
  itemId = atoi(PD("item","0"));
  if( itemId>0 ){
    threadId = db_int(0, "SELECT mthreadid FROM forumpost WHERE mpostid=%d",
                      itemId);
  }
  if( threadId==0 ) threadId = atoi(PD("thread","0"));
  if( threadId>0 ){
    
  }
  i = 0;
  db_prepare(&q,
    "SELECT mtitle, npost, mthreadid FROM forumthread"
    " WHERE inreplyto IS NULL ORDER BY mtime DESC LIMIT 40"
  );
  style_header("Recent Forum Threads");
  while( db_step(&q)==SQLITE_OK ){
    int n = db_column_int(&q,1);
    int threadid = db_column_int(&q,2);
    const char *zTitle = db_column_text(&q,0);
    if( i==0 ){
      @ <ol>
    }
    @ <li>
    @ %z(href("%R/forum?thread=%d",threadid))%h(zTitle)</a><br>
    @ %d(n) post%s(n==1?"":"s")</li>
  }
  if( i ){
    @ </ol>
  }
  style_footer();
}
