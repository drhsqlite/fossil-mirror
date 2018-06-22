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
@   mpostid INTEGER PRIMARY KEY,  -- unique id for each post (local)
@   mposthash TEXT,               -- uuid for this post
@   mthreadid INTEGER,            -- thread to which this post belongs
@   uname TEXT,                   -- name of user
@   mtime REAL,                   -- julian day number
@   mstatus TEXT,                 -- status.  NULL=ok. 'mod'=pending moderation
@   mimetype TEXT,                -- Mimetype for mbody
@   ipaddr TEXT,                  -- IP address of post origin
@   inreplyto INT,                -- Parent posting
@   mbody TEXT                    -- Content of the post
@ );
@ CREATE INDEX repository.forumpost_x1 ON
@   forumpost(inreplyto,mtime);
@ CREATE TABLE repository.forumthread(
@   mthreadid INTEGER PRIMARY KEY,
@   mthreadhash TEXT,             -- uuid for this thread
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
**    item=N             Show post N and its replies
**    
*/
void forum_page(void){
  int itemId;
  Stmt q;
  int i;

  login_check_credentials();
  if( !g.perm.RdForum ){ login_needed(g.anon.RdForum); return; }
  forum_verify_schema();
  style_header("Forum");
  itemId = atoi(PD("item","0"));
  if( itemId>0 ){
    int iUp;
    style_submenu_element("Topics", "%R/forum");
    iUp = db_int(0, "SELECT inreplyto FROM forumpost WHERE mpostid=%d", itemId);
    if( iUp ){
      style_submenu_element("Parent", "%R/forum?item=%d", iUp);
    }
    double rNow = db_double(0.0, "SELECT julianday('now')");
    /* Show the post given by itemId and all its descendents */
    db_prepare(&q,
      "WITH RECURSIVE"
      " post(id,uname,mstat,mime,ipaddr,parent,mbody,depth,mtime) AS ("
      "    SELECT mpostid, uname, mstatus, mimetype, ipaddr, inreplyto, mbody,"
      "           0, mtime FROM forumpost WHERE mpostid=%d"
      "  UNION"
      "  SELECT f.mpostid, f.uname, f.mstatus, f.mimetype, f.ipaddr,"
      "         f.inreplyto, f.mbody, p.depth+1 AS xdepth, f.mtime AS xtime"
      "    FROM forumpost AS f, post AS p"
      "   WHERE f.inreplyto=p.id"
      "   ORDER BY xdepth DESC, xtime ASC"
      ") SELECT * FROM post;",
      itemId
    );
    while( db_step(&q)==SQLITE_ROW ){
      int id = db_column_int(&q, 0);
      const char *zUser = db_column_text(&q, 1);
      const char *zMime = db_column_text(&q, 3);
      int iDepth = db_column_int(&q, 7);
      double rMTime = db_column_double(&q, 8);
      char *zAge = db_timespan_name(rNow - rMTime);
      Blob body;
      @ <!-- Forum post %d(id) -->
      @ <table class="forum_post">
      @ <tr>
      @ <td class="forum_margin" width="%d(iDepth*40)" rowspan="2">
      @ <td><span class="forum_author">%h(zUser)</span>
      @ <span class="forum_age">%s(zAge) ago</span>
      sqlite3_free(zAge);
      if( g.perm.WrForum ){
        @ <span class="forum_buttons">
        if( g.perm.AdminForum || fossil_strcmp(g.zLogin, zUser)==0 ){
          @ <a href='%R/forumedit?item=%d(id)'>Edit</a>
        }
        @ <a href='%R/forumedit?replyto=%d(id)'>Reply</a>
        @ </span>
      }
      @ </tr>
      @ <tr><td><div class="forum_body">
      blob_init(&body, db_column_text(&q,6), db_column_bytes(&q,6));
      wiki_render_by_mimetype(&body, zMime);
      blob_reset(&body);
      @ </div></td></tr>
      @ </table>
    }
  }else{
    /* If we reach this point, that means the users wants a list of
    ** recent threads.
    */
    i = 0;
    db_prepare(&q,
      "SELECT a.mtitle, a.npost, b.mpostid"
      "  FROM forumthread AS a, forumpost AS b "
      " WHERE a.mthreadid=b.mthreadid"
      "   AND b.inreplyto IS NULL"
      " ORDER BY a.mtime DESC LIMIT 40"
    );
    if( g.perm.WrForum ){
      style_submenu_element("New", "%R/forumedit");
    }
    @ <h1>Recent Forum Threads</h1>
    while( db_step(&q)==SQLITE_ROW ){
      int n = db_column_int(&q,1);
      int itemid = db_column_int(&q,2);
      const char *zTitle = db_column_text(&q,0);
      if( (i++)==0 ){
        @ <ol>
      }
      @ <li><span class="forum_title">
      @ %z(href("%R/forum?item=%d",itemid))%h(zTitle)</a></span>
      @ <span class="forum_npost">%d(n) post%s(n==1?"":"s")</span></li>
    }
    if( i ){
      @ </ol>
    }
  }
  style_footer();
}

/*
** Use content in CGI parameters "s" (subject), "b" (body), and
** "mimetype" (mimetype) to create a new forum entry.
** Return the id of the new forum entry.
**
** If any problems occur, return 0 and set *pzErr to a description of
** the problem.
**
** Cases:
**
**    itemId==0 && parentId==0        Starting a new thread.
**    itemId==0 && parentId>0         New reply to parentId
**    itemId>0 && parentId==0         Edit existing post itemId
*/
static int forum_post(int itemId, int parentId, char **pzErr){
  const char *zSubject = 0;
  int threadId;
  double rNow = db_double(0.0, "SELECT julianday('now')");
  const char *zMime = wiki_filter_mimetypes(P("mimetype"));
  if( itemId==0 && parentId==0 ){
    /* Start a new thread.  Subject required. */
    sqlite3_uint64 r1, r2;
    zSubject = PT("s");
    if( zSubject==0 || zSubject[0]==0 ){
      *pzErr = "\"Subject\" required to start a new thread";
      return 0;
    }
    sqlite3_randomness(sizeof(r1), &r1);
    sqlite3_randomness(sizeof(r2), &r2);
    db_multi_exec(
      "INSERT INTO forumthread(mthreadhash, mtitle, mtime, npost)"
      "VALUES(lower(hex(randomblob(28))),%Q,%!.17g,1)",
      zSubject, rNow
    );
    threadId = db_last_insert_rowid();
  }else{
    threadId = db_int(0, "SELECT mthreadid FROM forumpost"
                         " WHERE mpostid=%d", itemId ? itemId : parentId);
  }
  if( itemId ){
    if( db_int(0, "SELECT inreplyto IS NULL FROM forumpost"
                  " WHERE mpostid=%d", itemId) ){
      db_multi_exec(
        "UPDATE forumthread SET mtitle=%Q WHERE mthreadid=%d",
        PT("s"), threadId
      );
    }
    db_multi_exec(
       "UPDATE forumpost SET"
       " mtime=%!.17g,"
       " mimetype=%Q,"
       " ipaddr=%Q,"
       " mbody=%Q"
       " WHERE mpostid=%d",
       rNow, PT("mimetype"), P("REMOTE_ADDR"), PT("b"), itemId
    );
  }else{
    db_multi_exec(
       "INSERT INTO forumpost(mposthash,mthreadid,uname,mtime,"
       "  mstatus,mimetype,ipaddr,inreplyto,mbody) VALUES"
       "  (lower(hex(randomblob(28))),%d,%Q,%!.17g,%Q,%Q,%Q,nullif(%d,0),%Q)",
       threadId,g.zLogin,rNow,NULL,zMime,P("REMOTE_ADDR"),parentId,P("b"));
    itemId = db_last_insert_rowid();
  }
  if( zSubject==0 ){
    db_multi_exec(
      "UPDATE forumthread SET mtime=%!.17g, npost=npost+1"
      " WHERE mthreadid=(SELECT mthreadid FROM forumpost WHERE mpostid=%d)",
      rNow, itemId
    );
  }
  return itemId;
}

/*
** WEBPAGE: forumedit
**
** Query parameters:
**
**    replyto=N      Enter a reply to forum item N
**    item=N         Edit item N
**    s=SUBJECT      Subject. New thread only. Omitted for replies
**    b=BODY         Body of the post
**    m=MIMETYPE     Mimetype for the body of the post
**    x              Submit changes
**    p              Preview changes
*/
void forum_edit_page(void){
  int itemId;
  int parentId;
  char *zErr = 0;
  login_check_credentials();
  const char *zMime;
  const char *zSub;
  if( !g.perm.WrForum ){ login_needed(g.anon.WrForum); return; }
  forum_verify_schema();
  itemId = atoi(PD("item","0"));
  parentId = atoi(PD("replyto","0"));
  if( P("cancel")!=0 ){
    cgi_redirectf("%R/forum?item=%d", itemId ? itemId : parentId);
    return;
  }
  if( P("x")!=0 && cgi_csrf_safe(1) ){
    itemId = forum_post(itemId,parentId,&zErr);
    if( itemId ){
      cgi_redirectf("%R/forum?item=%d",itemId);
      return;
    }
  }
  if( itemId && (P("mimetype")==0 || P("b")==0) ){
    Stmt q;
    db_prepare(&q, "SELECT mimetype, mbody FROM forumpost"
                   " WHERE mpostid=%d", itemId);
    if( db_step(&q)==SQLITE_ROW ){
      if( P("mimetype")==0 ){
        cgi_set_query_parameter("mimetype", db_column_text(&q, 0));
      }
      if( P("b")==0 ){
        cgi_set_query_parameter("b", db_column_text(&q, 1));
      }
    }
    db_finalize(&q);
  }
  zMime = wiki_filter_mimetypes(P("mimetype"));
  if( itemId>0 ){
    style_header("Edit Forum Post");
  }else if( parentId>0 ){
    style_header("Comment On Forum Post");
  }else{
    style_header("New Forum Thread");
  }
  @ <form action="%R/forumedit" method="POST">
  if( itemId ){
    @ <input type="hidden" name="item" value="%d(itemId)">
  }
  if( parentId ){
    @ <input type="hidden" name="replyto" value="%d(parentId)">
  }
  if( P("p") ){
    Blob x;
    @ <div class="forumpreview">
    if( P("s") ){
      @ <h1>%h(PT("s"))</h1>
    }
    @ <div class="forumpreviewbody">
    blob_init(&x, PT("b"), -1);
    wiki_render_by_mimetype(&x, PT("mimetype"));
    blob_reset(&x);
    @ </div>
    @ </div>
    @ <hr>
  }
  @ <table border="0" class="forumeditform"> 
  if( zErr ){
    @ <tr><td colspan="2">
    @ <span class='forumFormErr'>%h(zErr)</span>
  }
  if( (itemId==0 && parentId==0)
   || (itemId && db_int(0, "SELECT inreplyto IS NULL FROM forumpost"
                           " WHERE mpostid=%d", itemId))
  ){
    zSub = PT("s");
    if( zSub==0 && itemId ){
      zSub = db_text("",
         "SELECT mtitle FROM forumthread"
         " WHERE mthreadid=(SELECT mthreadid FROM forumpost"
                          "  WHERE mpostid=%d)", itemId);
    }
    @ <tr><td>Subject:</td>
    @ <td><input type='text' class='forumFormSubject' name='s' value='%h(zSub)'>
  }
  @ <tr><td>Markup:</td><td>
  mimetype_option_menu(zMime);
  @ <tr><td>Comment:</td><td>
  @ <textarea name="b" class="wikiedit" cols="80"\
  @  rows="20" wrap="virtual">%h(PD("b",""))</textarea></td>
  @ <tr><td></td><td>
  @ <input type="submit" name="p" value="Preview">
  if( P("p")!=0 ){
    @ <input type="submit" name="x" value="Submit">
  }
  @ <input type="submit" name="cancel" value="Cancel">
  @ </table>
  @ </form>
  style_footer();
}
