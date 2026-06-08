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
** Default to using Markdown markup
*/
#define DEFAULT_FORUM_MIMETYPE  "text/x-markdown"

#if INTERFACE
/*
** Each instance of the following object represents a single message -
** either the initial post, an edit to a post, a reply, or an edit to
** a reply.
*/
struct ForumPost {
  int fpid;              /* rid for this post */
  int sid;               /* Serial ID number */
  int rev;               /* Revision number */
  char *zUuid;           /* Artifact hash */
  char *zDisplayName;    /* Name of user who wrote this post */
  double rDate;          /* Date for this post */
  ForumPost *pIrt;       /* This post replies to pIrt */
  ForumPost *pEditHead;  /* Original, unedited post */
  ForumPost *pEditTail;  /* Most recent edit for this post */
  ForumPost *pEditNext;  /* This post is edited by pEditNext */
  ForumPost *pEditPrev;  /* This post is an edit of pEditPrev */
  ForumPost *pNext;      /* Next in chronological order */
  ForumPost *pPrev;      /* Previous in chronological order */
  ForumPost *pDisplay;   /* Next in display order */
  int nEdit;             /* Number of edits to this post */
  int nIndent;           /* Number of levels of indentation for this post */
  int iClosed;           /* True if forum_rid_is_tagged("closed") */
};

/*
** A single instance of the following tracks all entries for a thread.
*/
struct ForumThread {
  ForumPost *pFirst;     /* First post in chronological order */
  ForumPost *pLast;      /* Last post in chronological order */
  ForumPost *pDisplay;   /* Entries in display order */
  ForumPost *pTail;      /* Last on the display list */
  int mxIndent;          /* Maximum indentation level */
  int nArtifact;         /* Number of forum artifacts in this thread */
};

/*
** A single entry from the forum-statuses setting.
*/
struct ForumStatus {
  char *zLabel;  /* Label for the UI */
  char *zValue;  /* status=X tag value */
};

/*
** A list of ForumStatus objects.
*/
struct ForumStatusList {
  struct ForumStatus *aStatus; /* List of statuses */
  unsigned int n;              /* Number of entries */
};

/*
** Information passed into the status_match() SQL function
** via the sqlite3_user_data() mechanism, and used by status_match()
** to determine whether or not a particular forum thread should
** be displayed.
*/
struct ForumStatusMatch {
  const ForumStatusList *pFses;  /* Parsed forum-statuses setting */
  int eStatusTag;                /* tagid for the "status" property */
  unsigned int iMatch;           /* Match this status value */
};
#endif /* INTERFACE */


/*
** Returns a high-level representation of the forum-statuses setting.
** This is a singleton, cached across calls.
 */
const ForumStatusList * forum_statuses(void){
  static ForumStatusList fses = {0,0};
  static int once = 0;
  while( !once ){
    ++once;
    /* Read `forum-statuses` setting and transform it into the
    ** fses object.
    **
    ** Maybe: if it's empty, synthesize a length-1 list from
    ** {value:"default",label:"Default",...}.  It's expected that
    ** usage may be slightly simplified if we always have a non-empty
    ** list. A length-1 list is, for purposes of the UI, identical to
    ** an empty one - status selection/filtering makes no sense if
    ** there's only one choice. */
    db_multi_exec(
      "CREATE TEMP TABLE IF NOT EXISTS forumstatus("
      " ord INTEGER PRIMARY KEY, label, value"
      ");"
      "DELETE FROM forumstatus;"
      "INSERT INTO forumstatus(label,value)"
      "  WITH setting(v) AS ("
      "    SELECT value v FROM config WHERE name='forum-statuses'"
      "  ),"
      "  room(r) AS ("
      "    SELECT e.value FROM setting s, jsonb_each(s.v) e"
      "    WHERE json_valid(s.v, 0x02)"
      "  )"
      "  SELECT r->>'label', r->>'value'"
      "  FROM room;"
    );
    fses.n = (unsigned)db_int(0, "SELECT count(*) FROM forumstatus");
    if( fses.n ){
      int i = 0;
      Stmt q;
      db_prepare(&q,"SELECT label, value FROM forumstatus"
                 "   ORDER BY ord");
      fses.aStatus = fossil_malloc(sizeof(fses.aStatus[0]) * fses.n);
      while( SQLITE_ROW==db_step(&q) ){
        ForumStatus * fs = &fses.aStatus[i++];
        fs->zLabel = fossil_strdup(db_column_text(&q, 0));
        fs->zValue = fossil_strdup(db_column_text(&q, 1));
      }
      db_finalize(&q);
    }
  }
  return &fses;
}

/*
** Search for a ForumStatus object by its tag value. If a match is
** found, the corresponding object is returned. If no match is found
** then (A) if bFirst is false then 0 is returned, else (B) the first
** entry in the list is returned, noting that the list may be empty,
** in which case 0 is returned.
*/
static const ForumStatus * forum_status_by_value(
  const char *z, int bFirst
){
  const ForumStatusList * const fses = forum_statuses();
  const ForumStatus * fs0 = 0;
  unsigned int i;
  if( !fses->n ) return 0;
  for( i = 0; i < fses->n; ++i ){
    const ForumStatus * fs = &fses->aStatus[i];
    if( 0==fossil_strcmp(z, fs->zValue) ){
      return fs;
    }else if( !fs0 ){
      fs0 = fs;
    }
  }
  return bFirst ? fs0 : 0;
}

/*
** COMMAND: test-forum-statuses
*/
void test_forum_statuses_cmd(void){
  const ForumStatusList * fses;
  unsigned i;
  db_find_and_open_repository(0,0);
  fses = forum_statuses();
  for(i = 0; i < fses->n; ++i ){
    const ForumStatus * fs = &fses->aStatus[i];
    fossil_print("Status: %!j %!j\n", fs->zValue, fs->zLabel);
    assert( fs==forum_status_by_value(fs->zValue, 0) );
  }
  fossil_print("Total statuses: %u\n", i);
}

/*
** Return true if the forum post with the given rid has been
** subsequently edited.
*/
int forum_rid_has_been_edited(int rid){
  static Stmt q;
  int res;
  db_static_prepare(&q,
     "SELECT 1 FROM forumpost A, forumpost B"
     " WHERE A.fpid=$rid AND B.froot=A.froot AND B.fprev=$rid"
  );
  db_bind_int(&q, "$rid", rid);
  res = db_step(&q)==SQLITE_ROW;
  db_reset(&q);
  return res;
}

/*
** Given a valid forumpost.fpid value, this function returns the
** initial forumpost.fpid in the chain of edits for that forum post,
** or rid if no prior versions are found.
*/
int forumpost_head_rid(int rid){
  static Stmt q = empty_Stmt_m;
  int rcRid = rid;
  if( !q.pStmt ){
    db_static_prepare(&q,
       "SELECT fprev FROM forumpost"
       " WHERE fpid=:rid AND fprev IS NOT NULL"
    );
  }
  db_bind_int(&q, ":rid", rid);
  while( SQLITE_ROW==db_step(&q) ){
    rcRid = db_column_int(&q, 0);
    db_reset(&q);
    db_bind_int(&q, ":rid", rcRid);
  }
  db_reset(&q);
  return rcRid;
}

/*
** Works like forumpost_head_rid() but expects zUuid to be an
** unambiguous forum post name. It may be a hash prefix, so long as
** it's unambiguous. Returns the rid of the head post, -1 if the name
** is ambiguous, and 0 if the name cannot be resolved as a forum post.
*/
int forumpost_head_rid2(const char *zUuid){
  const int fpid = symbolic_name_to_rid(zUuid, "f");
  return fpid>0
    ? forumpost_head_rid(fpid)
    : fpid;
}

/*
** Given a forum post RID and user name, returns true if zUserName
** matches the event.(euser,user) field for a formpost entry with the
** matching RID. Returns false if no match is found. If zUserName is
** 0 then login_name() is used.
*/
int forumpost_is_owner(int rid, const char *zUserName){
  static Stmt q;
  int rc;
  if( !q.pStmt ){
    db_static_prepare(
      &q, "SELECT 1 FROM event"
      " WHERE type='f' AND objid=$rid"
      " AND coalesce(euser,user)=$user"
    );
  }
  db_bind_int(&q, "$rid", rid);
  db_bind_text(&q, "$user", zUserName ? zUserName : login_name());
  rc = SQLITE_ROW==db_step(&q);
  db_reset(&q);
  return rc;
}

/*
** Returns true if p, or any parent of p, has a non-zero iClosed
** value.  Returns 0 if !p. For an edited chain of post, the tag is
** checked on the pEditHead entry, to simplify subsequent unlocking of
** the post.
**
** If bCheckIrt is true then p's thread in-response-to parents are
** checked (recursively) for closure, else only p is checked.
*/
static int forumpost_is_closed(
  ForumThread *pThread,          /* Thread that the post is a member of */
  ForumPost *p,                  /* the forum post */
  int bCheckIrt                  /* True to check In-Reply-To posts */
){
  int mx = pThread->nArtifact+1;
  while( p && (mx--)>0 ){
    if( p->pEditHead ) p = p->pEditHead;
    if( p->iClosed || !bCheckIrt ) return p->iClosed;
    p = p->pIrt;
  }
  return 0;
}

/*
** Given a forum post RID, this function returns true if that post has
** (or inherits) an active tag named zTagName. If bCheckIrt is true
** then the post to which the given post responds is also checked
** (recursively), else they are not. When checking in-response-to
** posts, the first one which is closed ends the search.
**
** This function checks _exactly_ the given rid, whereas forum post
** closure/re-opening is always applied to the head of an edit chain
** so that we get consistent implied locking behavior for later
** versions and responses to arbitrary versions in the chain. Even so,
** the "closed" tag is applied as a propagating tag so will apply to
** all edits in a given chain.
**
** The return value is one of:
**
** - 0 if no matching tag is found.
**
** - The tagxref.rowid of the tagxref entry for the closure if rid is
**   the forum post to which the closure applies.
**
** - (-tagxref.rowid) if the given rid inherits the tag from an IRT
**   forum post.
*/
static int forum_rid_is_tagged(int rid, const char *zTagName, int bCheckIrt){
  static Stmt qIrt = empty_Stmt_m;
  int rc = 0, i = 0;
  /* TODO: this can probably be turned into a CTE by someone with
  ** superior SQL-fu. */
  for( ; rid; i++ ){
    rc = rid_has_active_tag_name(rid, zTagName);
    if( rc || !bCheckIrt ) break;
    else if( !qIrt.pStmt ) {
      db_static_prepare(&qIrt,
        "SELECT firt FROM forumpost "
        "WHERE fpid=$fpid ORDER BY fmtime DESC"
      );
    }
    db_bind_int(&qIrt, "$fpid", rid);
    rid = SQLITE_ROW==db_step(&qIrt) ? db_column_int(&qIrt, 0) : 0;
    db_reset(&qIrt);
  }
  return i ? -rc : rc;
}

/* True if moderation of forum posts performs the same operation
** on its attachments. */
#define FORUMPOST_MOD_ATTACHMENTS 1
#if FORUMPOST_MOD_ATTACHMENTS
/*
** Internal helper for moderation_forumpost_...().
*/
static void forumpost_prep_pending_attachids(Stmt *q, int fpid){
  db_prepare(
    q,
    "SELECT attachid FROM attachment "
    "WHERE target=("
    "  SELECT uuid FROM blob WHERE rid=%d"
    ") and attachid in ("
    "  SELECT objid FROM modreq"
    ")",
    forumpost_head_rid(fpid)
  );
}
#endif

/*
** Approve the given forum post RID and any pending-approval
** attachments associated with its initial version.
*/
static void moderation_forumpost_approve(int fpid){
#if !FORUMPOST_MOD_ATTACHMENTS
  moderation_approve('f', fpid);
#else
  /* Also approve any pending attachments */
  Stmt q;
  moderation_approve('f', fpid);
  forumpost_prep_pending_attachids(&q, fpid);
  while( SQLITE_ROW==db_step(&q) ){
    moderation_approve('a', db_column_int(&q, 0));
  }
  db_finalize(&q);
#endif
}

/*
** Disapprove the given forum post and any pending-moderation
** attachments on its initial version.
*/
static void moderation_forumpost_disapprove(int fpid){
#if !FORUMPOST_MOD_ATTACHMENTS
  moderation_disapprove(fpid);
#else
  /* Also disapprove any pending attachments */
  Stmt q;
  moderation_disapprove(fpid);
  forumpost_prep_pending_attachids(&q, fpid);
  while( SQLITE_ROW==db_step(&q) ){
    moderation_disapprove(db_column_int(&q, 0));
  }
  db_finalize(&q);
#endif
}
#undef FORUMPOST_MOD_ATTACHMENTS

/*
** Applies or cancels a tag named zTagName on the given forum RID via
** addition of a new control artifact into the repository. In order to
** provide consistent behavior, it always acts on the first version of
** the given forum post, walking the forumpost.fprev values to find
** the head of the chain.
**
** If addTag is true then a propagating tag is added, except as noted
** below, with the given optional zValue string as the tag's
** value. If addTag is false then any matching active tag on frid is
** cancelled, except as noted below. zValue is ignored if it is NULL
** or starts with a NUL byte, or if addTag is false.
**
** This function only adds a tag if forum_rid_is_tagged() indicates
** that frid's head is not tagged. If a parent post is already tagged,
** no tag is added. Similarly, it will only remove a tag from a post
** which has its own tag, and will not remove an inherited one from a
** parent post.
**
** If addTag is true and frid is already tagged, this is a
** no-op. Likewise, if addTag is false and frid is not tagged
** (not accounting for an inherited closed tag), this is a no-op.
**
** If bCheckIrt is true then the forum post IRT hierarchy is searched
** for the tag, otherwise only the given RID is checked.
**
** Returns a positive value (a new tag.tagid value) if it actually
** creates a new tag, else 0. On error it returns a negative alue
** and g.zErrMsg "should" contain details.
**
** If it returns true then state from previously-loaded posts may be
** invalidated if they refer to the amended post or a response to it.
** e.g. if zTagName is "closed" then ForumPost::iClosed values may be
** stale.
**
** Sidebars:
**
** - Unless the caller has a transaction open, via
**   db_begin_transaction(), there is a very tiny race condition
**   window during which the caller's idea of whether or not the forum
**   post is tagged may differ from the current repository state.
**
** - This routine assumes that frid really does refer to a forum post.
**
** - This routine assumes that frid is not private or pending
**   moderation.
**
** - The applied tag is propagating so so that "closed" tags can
**   account for how edits of posts are handled. This differs from
**   closure of a branch, where a non-propagating tag is used.
*/
static int forumpost_tag(int frid, int addTag,
                         const char *zTagName,
                         const char *zValue){
  Blob artifact = BLOB_INITIALIZER;  /* Output artifact */
  Blob cksum = BLOB_INITIALIZER;     /* Z-card */
  int iTagged;                       /* true if frid is already tagged */
  int trid;                          /* RID of new control artifact */
  char *zUuid;                       /* UUID of head version of post */

  db_begin_transaction();
  frid = forumpost_head_rid(frid);
  iTagged = forum_rid_is_tagged(frid, zTagName, 0);
  if( !addTag && !iTagged ){
    /* Nothing to do. We never tag an IRT-inherited post via this
    ** function. */
    db_end_transaction(0);
    return 0;
  }
  if( !addTag || (zValue && !zValue[0]) ){
    zValue = 0;
  }
  if( addTag && iTagged ){
    char *zOld = 0;
    int cmp;
    rid_has_tag2(iTagged, zTagName, &zOld);
    cmp = fossil_strcmp(zOld, zValue);
    fossil_free(zOld);
    if( 0==cmp ){
      /* Same value - leave it as is. */
      db_end_transaction(0);
      return 0;
    }
  }
  zUuid = rid_to_uuid(frid);
  blob_appendf(&artifact, "D %z\n", date_in_standard_format( "now" ));
  blob_appendf(&artifact, "T %c%s %s%s%F\n",
               addTag ? '*' : '-', zTagName,
               zUuid, zValue ? " " : "", zValue ? zValue : "");
  blob_appendf(&artifact, "U %F\n", login_name());
  md5sum_blob(&artifact, &cksum);
  blob_appendf(&artifact, "Z %b\n", &cksum);
  blob_reset(&cksum);
  trid = content_put_ex(&artifact, 0, 0, 0, 0);
  if( trid==0 ){
    return -1;
  }
  if( manifest_crosslink(trid, &artifact, MC_NONE)==0 ){
    return -2;
  }
  assert( blob_is_reset(&artifact) );
  db_add_unsent(trid);
  admin_log("Tag forum post %S with %c%s",
            zUuid, addTag ? '*' : '-', zTagName);
  fossil_free(zUuid);
  db_end_transaction(0);
  return trid;
}

/*
** Returns true if the forum-close-policy setting is true, else false,
** caching the result for subsequent calls.
*/
static int forumpost_close_policy(void){
  static int closePolicy = -99;

  if( closePolicy==-99 ){
    closePolicy = db_get_boolean("forum-close-policy",0)>0;
  }
  return closePolicy;
}

/*
** Returns 1 if the current user is an admin, -1 if the current user
** is a forum moderator and the forum-close-policy setting is true,
** else returns 0. The value is cached for subsequent calls.
**
** This policy also determines whether non-admin forum moderators
** may delete forum attachments.
*/
int forumpost_may_close(void){
  static int permClose = -99;
  if( permClose!=-99 ){
    return permClose;
  }else if( g.perm.Admin ){
    return permClose = 1;
  }else if( g.perm.ModForum ){
    return permClose = forumpost_close_policy()>0 ? -1 : 0;
  }else{
    return permClose = 0;
  }
}

/*
** Emits a warning that the current forum post is CLOSED and can only
** be edited or responded to by an administrator. */
static void forumpost_error_closed(void){
  @ <div class='error'>This (sub)thread is CLOSED and can only be
  @ edited or replied to by an admin user.</div>
}

/*
** Delete a complete ForumThread and all its entries.
*/
static void forumthread_delete(ForumThread *pThread){
  ForumPost *pPost, *pNext;
  for(pPost=pThread->pFirst; pPost; pPost = pNext){
    pNext = pPost->pNext;
    fossil_free(pPost->zUuid);
    fossil_free(pPost->zDisplayName);
    fossil_free(pPost);
  }
  fossil_free(pThread);
}

/*
** Search a ForumPost list forwards looking for the post with fpid
*/
static ForumPost *forumpost_forward(ForumPost *p, int fpid){
  while( p && p->fpid!=fpid ) p = p->pNext;
  return p;
}

/*
** Search backwards for a ForumPost
*/
static ForumPost *forumpost_backward(ForumPost *p, int fpid){
  while( p && p->fpid!=fpid ) p = p->pPrev;
  return p;
}

/*
** Add a post to the display list
*/
static void forumpost_add_to_display(ForumThread *pThread, ForumPost *p){
  if( pThread->pDisplay==0 ){
    pThread->pDisplay = p;
  }else{
    pThread->pTail->pDisplay = p;
  }
  pThread->pTail = p;
}

/*
** Extend the display list for pThread by adding all entries that
** reference fpid.  The first such post will be no earlier then
** post "p".
*/
static void forumthread_display_order(
  ForumThread *pThread,    /* The complete thread */
  ForumPost *pBase         /* Add replies to this post */
){
  ForumPost *p;
  ForumPost *pPrev = 0;
  ForumPost *pBaseIrt;
  for(p=pBase->pNext; p; p=p->pNext){
    if( !p->pEditPrev && p->pIrt ){
      pBaseIrt = p->pIrt->pEditHead ? p->pIrt->pEditHead : p->pIrt;
      if( pBaseIrt==pBase ){
        if( pPrev ){
          pPrev->nIndent = pBase->nIndent + 1;
          forumpost_add_to_display(pThread, pPrev);
          forumthread_display_order(pThread, pPrev);
        }
        pPrev = p;
      }
    }
  }
  if( pPrev ){
    pPrev->nIndent = pBase->nIndent + 1;
    if( pPrev->nIndent>pThread->mxIndent ) pThread->mxIndent = pPrev->nIndent;
    forumpost_add_to_display(pThread, pPrev);
    forumthread_display_order(pThread, pPrev);
  }
}

/*
** Construct a ForumThread object given the root record id.
*/
static ForumThread *forumthread_create(int froot, int computeHierarchy){
  ForumThread *pThread;
  ForumPost *pPost;
  ForumPost *p;
  Stmt q;
  int sid = 1;
  int firt, fprev;
  pThread = fossil_malloc( sizeof(*pThread) );
  memset(pThread, 0, sizeof(*pThread));
  db_prepare(&q,
     "SELECT fpid, firt, fprev, (SELECT uuid FROM blob WHERE rid=fpid), fmtime"
     "  FROM forumpost"
     " WHERE froot=%d ORDER BY fmtime",
     froot
  );
  while( db_step(&q)==SQLITE_ROW ){
    pPost = fossil_malloc( sizeof(*pPost) );
    memset(pPost, 0, sizeof(*pPost));
    pPost->fpid = db_column_int(&q, 0);
    firt = db_column_int(&q, 1);
    fprev = db_column_int(&q, 2);
    pPost->zUuid = fossil_strdup(db_column_text(&q,3));
    pPost->rDate = db_column_double(&q,4);
    if( !fprev ) pPost->sid = sid++;
    pPost->pPrev = pThread->pLast;
    pPost->pNext = 0;
    if( pThread->pLast==0 ){
      pThread->pFirst = pPost;
    }else{
      pThread->pLast->pNext = pPost;
    }
    pThread->pLast = pPost;
    pThread->nArtifact++;

    /* Find the in-reply-to post.  Default to the topic post if the replied-to
    ** post cannot be found. */
    if( firt ){
      pPost->pIrt = pThread->pFirst;
      for(p=pThread->pFirst; p; p=p->pNext){
        if( p->fpid==firt ){
          pPost->pIrt = p;
          break;
        }
      }
    }

    /* Maintain the linked list of post edits. */
    if( fprev ){
      p = forumpost_backward(pPost->pPrev, fprev);
      p->pEditNext = pPost;
      pPost->sid = p->sid;
      pPost->rev = p->rev+1;
      pPost->nEdit = p->nEdit+1;
      pPost->pEditPrev = p;
      pPost->pEditHead = p->pEditHead ? p->pEditHead : p;
      for(; p; p=p->pEditPrev ){
        p->nEdit = pPost->nEdit;
        p->pEditTail = pPost;
      }
    }
    pPost->iClosed = forum_rid_is_tagged(pPost->pEditHead
                                         ? pPost->pEditHead->fpid
                                         : pPost->fpid, "closed", 1);
  }
  db_finalize(&q);

  if( computeHierarchy ){
    /* Compute the hierarchical display order */
    pPost = pThread->pFirst;
    pPost->nIndent = 1;
    pThread->mxIndent = 1;
    forumpost_add_to_display(pThread, pPost);
    forumthread_display_order(pThread, pPost);
  }

  /* Return the result */
  return pThread;
}

/*
** List all forum threads to standard output.
*/
static void forum_thread_list(void){
  Stmt q;
  db_prepare(&q,
    " SELECT"
    "  datetime(max(fmtime)),"
    "  sum(fprev IS NULL),"
    "  froot"
    " FROM forumpost"
    " GROUP BY froot"
    " ORDER BY 1;"
  );
  fossil_print("    id  cnt    most recent post\n");
  fossil_print("------ ---- -------------------\n");
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%6d %4d %s\n",
      db_column_int(&q, 2),
      db_column_int(&q, 1),
      db_column_text(&q, 0)
    );
  }
  db_finalize(&q);
}

/*
** COMMAND: test-forumthread
**
** Usage: %fossil test-forumthread [THREADID]
**
** Display a summary of all messages on a thread THREADID.  If the
** THREADID argument is omitted, then show a list of all threads.
**
** This command is intended for testing an analysis only.
*/
void forumthread_cmd(void){
  int fpid;
  int froot;
  const char *zName;
  ForumThread *pThread;
  ForumPost *p;

  db_find_and_open_repository(0,0);
  verify_all_options();
  if( g.argc==2 ){
    forum_thread_list();
    return;
  }
  if( g.argc!=3 ) usage("THREADID");
  zName = g.argv[2];
  fpid = symbolic_name_to_rid(zName, "f");
  if( fpid<=0 ){
    fpid = db_int(0, "SELECT rid FROM blob WHERE rid=%d", atoi(zName));
  }
  if( fpid<=0 ){
    fossil_fatal("unknown or ambiguous forum id: \"%s\"", zName);
  }
  froot = db_int(0, "SELECT froot FROM forumpost WHERE fpid=%d", fpid);
  if( froot==0 ){
    fossil_fatal("Not a forum post: \"%s\"", zName);
  }
  fossil_print("fpid  = %d\n", fpid);
  fossil_print("froot = %d\n", froot);
  pThread = forumthread_create(froot, 1);
  fossil_print("count = %d\n", pThread->nArtifact);
  fossil_print("Chronological:\n");
  fossil_print(
/* 0         1         2         3         4         5         6         7    */
/*  123456789 123456789 123456789 123456789 123456789 123456789 123456789 123 */
  " sid  rev  closed      fpid      pIrt pEditPrev pEditTail hash\n");
  for(p=pThread->pFirst; p; p=p->pNext){
    fossil_print("%4d %4d %7d %9d %9d %9d %9d %8.8s\n",
       p->sid, p->rev,
       p->iClosed,
       p->fpid, p->pIrt ? p->pIrt->fpid : 0,
       p->pEditPrev ? p->pEditPrev->fpid : 0,
       p->pEditTail ? p->pEditTail->fpid : 0, p->zUuid);
  }
  fossil_print("\nDisplay\n");
  for(p=pThread->pDisplay; p; p=p->pDisplay){
    fossil_print("%*s", (p->nIndent-1)*3, "");
    if( p->pEditTail ){
      fossil_print("%d->%d", p->fpid, p->pEditTail->fpid);
    }else{
      fossil_print("%d", p->fpid);
    }
    if( p->iClosed ){
      fossil_print(" [closed%s]", p->iClosed<0 ? " via parent" : "");
    }
    fossil_print("\n");
  }
  forumthread_delete(pThread);
}

/*
** WEBPAGE:  forumthreadhashlist
**
** Usage:  /forumthreadhashlist/HASH-OF-ROOT
**
** This page (accessibly only to admins) shows a list of all artifacts
** associated with a single forum thread.  An admin might copy/paste this
** list into the /shun page in order to shun an entire thread.
*/
void forumthreadhashlist(void){
  int fpid;
  int froot;
  const char *zName = P("name");
  ForumThread *pThread;
  ForumPost *p;
  char *fuuid;
  Stmt q;

  login_check_credentials();
  if( !g.perm.Admin ){
    return;
  }
  if( zName==0 ){
    webpage_error("Missing \"name=\" query parameter");
  }
  fpid = symbolic_name_to_rid(zName, "f");
  if( fpid<=0 ){
    if( fpid==0 ){
      webpage_notfound_error("Unknown forum id: \"%s\"", zName);
    }else{
      ambiguous_page();
    }
    return;
  }
  froot = db_int(0, "SELECT froot FROM forumpost WHERE fpid=%d", fpid);
  if( froot==0 ){
    webpage_notfound_error("Not a forum post: \"%s\"", zName);
  }
  fuuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", froot);
  style_set_current_feature("forum");
  style_header("Artifacts Of Forum Thread");
  @ <h2>
  @ Artifacts associated with the forum thread
  @ <a href="%R/forumthread/%S(fuuid)">%S(fuuid)</a>:</h2>
  @ <pre>
  pThread = forumthread_create(froot, 1);
  for(p=pThread->pFirst; p; p=p->pNext){
    @ %h(p->zUuid)
  }
  forumthread_delete(pThread);
  @ </pre>
  @ <hr>
  @ <h2>Related FORUMPOST Table Content</h2>
  @ <table border="1" cellpadding="4" cellspacing="0">
  @ <tr><th>fpid<th>froot<th>fprev<th>firt<th>fmtime
  db_prepare(&q, "SELECT fpid, froot, fprev, firt, datetime(fmtime)"
                 "  FROM forumpost"
                 " WHERE froot=%d"
                 " ORDER BY fmtime", froot);
  while( db_step(&q)==SQLITE_ROW ){
    @ <tr><td>%d(db_column_int(&q,0))\
    @ <td>%d(db_column_int(&q,1))\
    @ <td>%d(db_column_int(&q,2))\
    @ <td>%d(db_column_int(&q,3))\
    @ <td>%h(db_column_text(&q,4))</tr>
  }
  @ </table>
  db_finalize(&q);
  style_finish_page();
}

/*
** Returns true if the current user is authorized to set forum post
** fpid's status.
*/
static int forum_may_set_status(int fpid){
  if( moderation_pending(fpid) ) return 0;
  return g.perm.Admin
    || g.perm.ModForum
    || (login_is_individual()
        && forumpost_is_owner(fpid, 0));
}

/*
** If the current user is authorized to set fp's status then this
** renders a mini-form for setting the status then redirecting back to
** the post. Else it may emit a status label or no output.
*/
static void forum_render_status_selection( const ForumPost *fp ){
  const ForumStatusList * const fss = forum_statuses();
  if( fss->n>1 ){
    const ForumPost * pHead = fp->pEditHead ? fp->pEditHead : fp;
    int i;
    char * zCurrent = 0;
    const ForumStatus * sCurrent = 0;
    rid_has_tag2(pHead->fpid, "status", &zCurrent);
    for( i = 0; i < fss->n; ++i ){
      const ForumStatus * const fs = &fss->aStatus[i];
      if( 0==fossil_strcmp(zCurrent, fs->zValue) ){
        sCurrent = fs;
        break;
      }
    }
    if( !sCurrent ) sCurrent = &fss->aStatus[0];
    assert( sCurrent );
    @ <fieldset class='forum-status-selection'>\
    @ <legend>Status \
    @ <span class='help-buttonlet'>\
    @ Moderators and the post's owner may change \
    @ the status of this thread. See \
    @ <a href='%R/help/forum-statuses' target='_new'>\
    @ /help/forum-statuses</a></span>\
    @ </legend>\
    if( forum_may_set_status(fp->fpid) ){
      @ <form method="post" action='%R/forumpost_status'>
      login_insert_csrf_secret();
      @ <input type='hidden' name='fpid' value='%s(fp->zUuid)' />
      @ <select name='status' data-fpid='%s(fp->zUuid)'\
      @ data-initial-value='%h(zCurrent ? zCurrent : "")'>
      for( i = 0; i < fss->n; ++i ){
        const ForumStatus * const fs = &fss->aStatus[i];
        @ <option value='%h(fs->zValue)'\
        @ %s(sCurrent==fs ? " selected" : "")>\
        @ %h(fs->zLabel)</option>
      }
      @ </select>
      @ <input type='button' class='submit action-status' disabled
      @ value='Change' />
      /* ^^^ This must be <input>, not <button>, or else tapping it
      ** will unconditionally submit. */
      @ </form>
      /* Form is activated in fossil.page.forumpost.js */
    }else{
      @ <button disabled>Status: %h(sCurrent->zLabel)</button>
    }
    @ </fieldset>
    fossil_free(zCurrent);
  }
}

/*
** Render a forum post for display
*/
void forum_render(
  const char *zTitle,         /* The title.  Might be NULL for no title */
  const char *zMimetype,      /* Mimetype of the message */
  const char *zContent,       /* Content of the message */
  const char *zClass,         /* Put in a <div> if not NULL */
  int bScroll                 /* Large message content scrolls if true */
){
  if( zClass ){
    @ <div class='%s(zClass)'>
  }
  if( zTitle ){
    if( zTitle[0] ){
      @ <h1>%h(zTitle)</h1>
    }else{
      @ <h1><i>Deleted</i></h1>
    }
  }
  if( zContent && zContent[0] ){
    Blob x;
    const int isFossilWiki = zMimetype==0
      || fossil_strcmp(zMimetype, "text/x-fossil-wiki")==0;
    if( bScroll ){
      @ <div class='forumPostBody'>
    }else{
      @ <div class='forumPostFullBody'>
    }
    blob_init(&x, 0, 0);
    blob_append(&x, zContent, -1);
    safe_html_context(DOCSRC_FORUM);
    if( isFossilWiki ){
      /* Markdown and plain-text rendering add a wrapper DIV resp. PRE
      ** element around the post, and some CSS relies on its existence
      ** in order to handle expansion/collapse of the post. Fossil
      ** Wiki rendering does not do so, so we must wrap those manually
      ** here. */
      @ <div class='fossilWiki'>
    }
    wiki_render_by_mimetype(&x, zMimetype);
    if( isFossilWiki ){
      @ </div>
    }
    blob_reset(&x);
    @ </div>
  }else{
    @ <i>Deleted</i>
  }
  if( zClass ){
    @ </div>
  }
}

/*
** Compute a display name from a login name.
**
** If the input login is found in the USER table, then check the USER.INFO
** field to see if it has display-name followed by an email address.
** If it does, that becomes the new display name.  If not, let the display
** name just be the login.
**
** Space to hold the returned name is obtained from fossil_strdup() or
** mprintf() and should be freed by the caller.
**
** HTML markup within the reply has been property escaped.  Hyperlinks
** may have been added.  The result is safe for use with %s.
*/
static char *display_name_from_login(const char *zLogin){
  static Stmt q;
  char *zResult;
  db_static_prepare(&q,
     "SELECT display_name(info) FROM user WHERE login=$login"
  );
  db_bind_text(&q, "$login", zLogin);
  if( db_step(&q)==SQLITE_ROW && db_column_type(&q,0)==SQLITE_TEXT ){
    const char *zDisplay = db_column_text(&q,0);
    if( fossil_strcmp(zDisplay,zLogin)==0 ){
      zResult = mprintf("%z%h</a>",
                  href("%R/timeline?ss=v&y=f&vfx&u=%t",zLogin),zLogin);
    }else{
      zResult = mprintf("%s (%z%h</a>)", zDisplay,
                  href("%R/timeline?ss=v&y=f&vfx&u=%t",zLogin),zLogin);
    }
  }else{
    zResult = mprintf("%z%h</a>",
                href("%R/timeline?ss=v&y=f&vfx&u=%t",zLogin),zLogin);
  }
  db_reset(&q);
  return zResult;
}

/*
** Compute and return the display name for a ForumPost.  If
** pManifest is not NULL, then it is a Manifest object for the post.
** if pManifest is NULL, this routine has to fetch and parse the
** Manifest object for itself.
**
** Memory to hold the display name is attached to p->zDisplayName
** and will be freed together with the ForumPost object p when it
** is freed.
**
** The returned text has had all HTML markup escaped and is safe for
** use within %s.
*/
static char *forum_post_display_name(ForumPost *p, Manifest *pManifest){
  Manifest *pToFree = 0;
  if( p->zDisplayName ) return p->zDisplayName;
  if( pManifest==0 ){
    pManifest = pToFree = manifest_get(p->fpid, CFTYPE_FORUM, 0);
    if( pManifest==0 ) return "(unknown)";
  }
  p->zDisplayName = display_name_from_login(pManifest->zUser);
  if( pToFree ) manifest_destroy(pToFree);
  if( p->zDisplayName==0 ) return "(unknown)";
  return p->zDisplayName;
}

/*
** Renders the attachment list for the given forum post.
** Emits no output if there are no attachments.
*/
static void forum_render_attachment_list(const char *zUuid){
#if 1
    attachment_list(zUuid, "&#x1f4ce; Attachments", 0
                    | ATTACHLIST_SIZE
                    | ATTACHLIST_HIDE_UNAPPROVED
                    | ATTACHLIST_DETAILS_CLOSED
                    | ATTACHLIST_HIDE_EMPTY);
#else
    char * zLbl = mprintf("<a href='%R/attachlist?forumpost=%!S'>"
                          "Attachments</a>:", zUuid);
    attachment_list(zUuid, zLbl,
                    ATTACHLIST_HRULE_ABOVE
                    | ATTACHLIST_SIZE
                    | ATTACHLIST_HIDE_UNAPPROVED
                    | ATTACHLIST_HIDE_EMPTY);
    fossil_free(zLbl);
#endif
}

/*
** Renders the attachment list for p or (if not NULL) pEditHead.
*/
static void forum_render_attachment_list2(ForumPost *p){
  if( p->pEditHead ) p = p->pEditHead;
  forum_render_attachment_list(p->zUuid);
}

/* Flags for use with forum_display_post() */
#define FDISPLAY_RAW         0x01 /* omit the border */
#define FDISPLAY_UNFORMATTED 0x02 /* leave the post unformatted */
#define FDISPLAY_HISTORY     0x04 /* Showing edit history */
#define FDISPLAY_SELECTED    0x08 /* This is the selected post */

/*
** Display a single post in a forum thread.
*/
static void forum_display_post(
  ForumThread *pThread, /* The thread that this post is a member of */
  ForumPost *p,         /* Forum post to display */
  int iIndentScale,     /* Indent scale factor */
  int flags,            /* From the FDISPLAY_... enum */
  char *zQuery          /* Common query string */
){
  char *zPosterName;    /* Name of user who originally made this post */
  char *zEditorName;    /* Name of user who provided the current edit */
  char *zDate;          /* The time/date string */
  char *zDateZulu;      /* The date/time string in Zulul time */
  char *zHist;          /* History query string */
  Manifest *pManifest;  /* Manifest comprising the current post */
  int bPrivate;         /* True for posts awaiting moderation */
  int bSameUser;        /* True if author is also the reader */
  int iIndent;          /* Indent level */
  int iClosed;          /* True if (sub)thread is closed */
  const int bRaw = flags & FDISPLAY_RAW;
  const int bUnf = flags & FDISPLAY_UNFORMATTED;
  const int bHist = flags & FDISPLAY_HISTORY;
  const int bSelect = flags & FDISPLAY_SELECTED;
  const char *zMimetype;/* Formatting MIME type */

  /* Get the manifest for the post.  Abort if not found (e.g. shunned). */
  pManifest = manifest_get(p->fpid, CFTYPE_FORUM, 0);
  if( !pManifest ) return;
  iClosed = forumpost_is_closed(pThread, p, 1);
  bPrivate = content_is_private(p->fpid);
  bSameUser = login_is_individual()
    && fossil_strcmp(pManifest->zUser, g.zLogin)==0;
  /* When not in raw mode, create the border around the post. */
  if( !bRaw ){
    /* Open the <div> enclosing the post. Set the class string to mark the post
    ** as selected and/or obsolete. */
    iIndent = (p->pEditHead ? p->pEditHead->nIndent : p->nIndent)-1;
    @ <div id='forum%d(p->fpid)' class='forumTime\
    @ %s(bSelect ? " forumSel" : "")\
    @ %s(iClosed ? " forumClosed" : "")\
    @ %s(p->pEditTail ? " forumObs" : "")' \
    if( iIndent && iIndentScale ){
      @ style='margin-left:%d(iIndent*iIndentScale)ex;' \
    }
    /* These data-X fields are used by the JS editor. */
    if( p->pIrt ){
      @ data-firt="%s(p->pIrt->zUuid)" \
    }
    if( p->pEditHead ){
      @ data-fedithead="%s(p->pEditHead->zUuid)" \
    }
    @ data-fpid="%s(p->zUuid)">\

    /* If this is the first post (or an edit thereof), emit the thread title. */
    if( pManifest->zThreadTitle ){
      @ <h1>%h(pManifest->zThreadTitle)</h1>
    }

    /* Begin emitting the header line.  The forum of the title
    ** varies depending on whether:
    **    *  The post is unedited
    **    *  The post was last edited by the original author
    **    *  The post was last edited by a different person
    */
    if( !p->pEditHead ){
      zPosterName = forum_post_display_name(p, pManifest);
      zEditorName = zPosterName;
    }
    zDate = db_text(0, "SELECT datetime(%.17g,toLocal())", p->rDate);
    zDateZulu = db_text(0,
          "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%SZ',%.17g)",
          p->rDate);
    if( p->pEditPrev ){
      zPosterName = forum_post_display_name(p->pEditHead, 0);
      zEditorName = forum_post_display_name(p, pManifest);
      zHist = bHist ? "" : zQuery[0]==0 ? "?hist" : "&hist";
      @ <h3 class='forumPostHdr'>(%d(p->sid)\
      @ .%0*d(fossil_num_digits(p->nEdit))(p->rev))
      if( fossil_strcmp(zPosterName, zEditorName)==0 ){
        @ By %s(zPosterName) on \
        style_copy_button(1, 0, 0, 0, zDateZulu, "%h", zDate);
        @ &#32;edited from \
        @ %z(href("%R/forumpost/%S%s%s",p->pEditPrev->zUuid,zQuery,zHist))\
        @ %d(p->sid).%0*d(fossil_num_digits(p->nEdit))(p->pEditPrev->rev)</a>
      }else{
        @ Originally by %s(zPosterName) \
        @ with edits by %s(zEditorName) on \
        style_copy_button(1, 0, 0, 0, zDateZulu, "%h", zDate);
        @ &#32;from \
        @ %z(href("%R/forumpost/%S%s%s",p->pEditPrev->zUuid,zQuery,zHist))\
        @ %d(p->sid).%0*d(fossil_num_digits(p->nEdit))(p->pEditPrev->rev)</a>
      }
    }else{
      zPosterName = forum_post_display_name(p, pManifest);
      @ <h3 class='forumPostHdr'>(%d(p->sid))
      @ By %s(zPosterName) on \
      style_copy_button(1, 0, 0, 0, zDateZulu, "%h", zDate);
      cgi_append_content(" ", 1);
    }
    fossil_free(zDate);
    fossil_free(zDateZulu);


    /* If debugging is enabled, link to the artifact page. */
    if( g.perm.Debug ){
      @ <span class="debug">\
      @ <a href="%R/artifact/%h(p->zUuid)">(artifact-%d(p->fpid))</a></span>
    }

    /* If this is a reply, refer back to the parent post. */
    if( p->pIrt ){
      @ in reply to %z(href("%R/forumpost/%S%s",p->pIrt->zUuid,zQuery))\
      @ %d(p->pIrt->sid)\
      if( p->pIrt->nEdit ){
        @ .%0*d(fossil_num_digits(p->pIrt->nEdit))(p->pIrt->rev)\
      }
      @ </a>
    }

    /* If this post was later edited, refer forward to the next edit. */
    if( p->pEditNext ){
      @ updated by %z(href("%R/forumpost/%S%s",p->pEditNext->zUuid,zQuery))\
      @ %d(p->pEditNext->sid)\
      @ .%0*d(fossil_num_digits(p->nEdit))(p->pEditNext->rev)</a>
    }

    /* Provide a link to select the individual post. */
    if( !bSelect ){
      @ %z(href("%R/forumpost/%!S%s",p->zUuid,zQuery))[link]</a>
    }

    /* Provide a link to the raw source code. */
    if( !bUnf ){
      @ %z(href("%R/forumpost/%!S?raw",p->zUuid))[source]</a>
    }
    @ </h3>

    if( bPrivate && (bSameUser || g.perm.Admin || g.perm.ModForum) ){
      moderation_pending_www(p->fpid);
    }
  }/*!bRaw*/

  /* Check if this post is approved, also if it's by the current user.
     Render the post if the user is able to see it. */
  if( bPrivate && !g.perm.ModForum && !bSameUser ){
    @ <p><span class="modpending">Awaiting Moderator Approval</span></p>
  }else{
    if( bRaw || bUnf || p->pEditTail ){
      zMimetype = "text/plain";
    }else{
      zMimetype = pManifest->zMimetype;
    }
    forum_render(0, zMimetype, pManifest->zWiki, 0, !bRaw);
  }

  /* When not in raw mode, finish creating the border around the post. */
  if( !bRaw ){
    int bBrBeforeAttach = 0;  /* Layout kludge for Attach button */
    forum_render_attachment_list2(p);
    /* If the user is able to write to the forum and if this post has not been
    ** edited, create a form with various interaction buttons. */
    if( g.perm.WrForum && !p->pEditTail ){
      @ <div class="forumpost-single-controls">\
      @ <form action="%R/forumedit" method="POST">
      @ <input type="hidden" name="fpid" value="%s(p->zUuid)">
      if( !bPrivate ){
        /* Reply and Edit are only available if the post has been
        ** approved.  Closed threads can only be edited or replied to
        ** if forumpost_may_close() is true but a user may delete
        ** their own posts even if they are closed. */
        if( forumpost_may_close() || !iClosed ){
          @ <input type="submit" name="reply" value="Reply">
          if( g.perm.Admin || (bSameUser && !iClosed) ){
            @ <input type="submit" name="edit" value="Edit">
          }
          if( g.perm.Admin || bSameUser ){
            @ <input type="submit" name="nullout" value="Delete">
          }
        }
      }else if( g.perm.ModForum ){
        /* Allow moderators to approve or reject pending posts.  Also allow
        ** forum supervisors to mark non-special users as trusted and therefore
        ** able to post unmoderated. */
        @ <input type="submit" name="approve" value="Approve">
        @ <input type="submit" name="reject" value="Reject">
        if( g.perm.AdminForum && !login_is_special(pManifest->zUser) ){
          @ <br><label><input type="checkbox" name="trust">
          @ Trust user "%h(pManifest->zUser)" so that future posts by \
          @ "%h(pManifest->zUser)" do not require moderation.
          @ </label>
          @ <input type="hidden" name="trustuser" value="%h(pManifest->zUser)">
          bBrBeforeAttach = 1 /* slightly unmangle the layout */;
        }
      }else if( bSameUser ){
        /* Allow users to delete (reject) their own pending posts. */
        @ <input type="submit" name="reject" value="Delete">
      }
      login_insert_csrf_secret();
      @ </form>

      if( bSelect ){
        const ForumPost *pHead = p->pEditHead ? p->pEditHead : p;
        if( !bPrivate && forumpost_may_close() && iClosed>=0 ){
          @ <form method="post" \
          @  action='%R/forumpost_%s(iClosed > 0 ? "reopen" : "close")'>
          login_insert_csrf_secret();
          @ <input type="hidden" name="fpid" value="%s(p->zUuid)" />
          if( moderation_pending(p->fpid)==0 ){
            @ <input type="button" value='%s(iClosed ? "Re-open" : "Close")' \
            @ class='submit hidden \
            @ %s(iClosed ? "action-reopen" : "action-close")'/>
            /* ^^^ activated by fossil.page.forumpost.js */
          }
          @ </form>
        }
        if( attach_user_may(p/*not pHead*/->fpid, CFTYPE_FORUM) ){
          /* When an admin edits someone else's post, the admin
          ** effectively takes over ownership of it (and we currently
          ** have no way of passing it back). Because of this, we
          ** check the ownership of `p` instead of `pHead`. */
          if( bBrBeforeAttach ){
            @ <br>
          }
          attach_render_attachadd_button(pHead->zUuid);
        }
      }
      @ </div>
    }
    if( !p->pIrt && (flags & FDISPLAY_SELECTED)){
      forum_render_status_selection(p);
    }
    @ </div>
  }/*!bRaw*/

  /* Clean up. */
  manifest_destroy(pManifest);
}

/*
** Possible display modes for forum_display_thread().
*/
enum {
  FD_RAW,     /* Like FD_SINGLE, but additionally omit the border, force
              ** unformatted mode, and inhibit history mode */
  FD_SINGLE,  /* Render a single post and (optionally) its edit history */
  FD_CHRONO,  /* Render all posts in chronological order */
  FD_HIER,    /* Render all posts in an indented hierarchy */
};

/*
** Display a forum thread.  If mode is FD_RAW or FD_SINGLE, display only a
** single post from the thread and (optionally) its edit history.
*/
static void forum_display_thread(
  int froot,            /* Forum thread root post ID */
  int fpid,             /* Selected forum post ID, or 0 if none selected */
  int mode,             /* Forum display mode, one of the FD_* enumerations */
  int autoMode,         /* mode was selected automatically */
  int bUnf,             /* True if rendering unformatted */
  int bHist             /* True if showing edit history, ignored for FD_RAW */
){
  ForumThread *pThread; /* Thread structure */
  ForumPost *pSelect;   /* Currently selected post, or NULL if none */
  ForumPost *p;         /* Post iterator pointer */
  char zQuery[30];      /* Common query string */
  int iIndentScale = 4; /* Indent scale factor, measured in "ex" units */
  int sid;              /* Comparison serial ID */
  int i;

  /* In raw mode, force unformatted display and disable history. */
  if( mode == FD_RAW ){
    bUnf = 1;
    bHist = 0;
  }

  /* Thread together the posts and (optionally) compute the hierarchy. */
  pThread = forumthread_create(froot, mode==FD_HIER);

  /* Compute the appropriate indent scaling. */
  if( mode==FD_HIER ){
    iIndentScale = 4;
    while( iIndentScale>1 && iIndentScale*pThread->mxIndent>25 ){
      iIndentScale--;
    }
  }else{
    iIndentScale = 0;
  }

  /* Find the selected post, or (depending on parameters) its latest edit. */
  pSelect = fpid ? forumpost_forward(pThread->pFirst, fpid) : 0;
  if( !bHist && mode!=FD_RAW && pSelect && pSelect->pEditTail ){
    pSelect = pSelect->pEditTail;
  }

  /* When displaying only a single post, abort if no post was selected or the
  ** selected forum post does not exist in the thread.  Otherwise proceed to
  ** display the entire thread without marking any posts as selected. */
  if( !pSelect && (mode==FD_RAW || mode==FD_SINGLE) ){
    return;
  }

  /* Create the common query string to append to nearly all post links. */
  i = 0;
  if( !autoMode ){
    char m = 'a';
    switch( mode ){
      case FD_RAW:     m = 'r';  break;
      case FD_CHRONO:  m = 'c';  break;
      case FD_HIER:    m = 'h';  break;
      case FD_SINGLE:  m = 's';  break;
    }
    zQuery[i++] = '?';
    zQuery[i++] = 't';
    zQuery[i++] = '=';
    zQuery[i++] = m;
  }
  if( bUnf ){
    zQuery[i] =  i==0 ? '?' : '&'; i++;
    zQuery[i++] = 'u';
    zQuery[i++] = 'n';
    zQuery[i++] = 'f';
  }
  if( bHist ){
    zQuery[i] = i==0 ? '?' : '&'; i++;
    zQuery[i++] = 'h';
    zQuery[i++] = 'i';
    zQuery[i++] = 's';
    zQuery[i++] = 't';
  }
  assert( i<(int)sizeof(zQuery) );
  zQuery[i] = 0;
  assert( zQuery[0]==0 || zQuery[0]=='?' );

  /* Identify which post to display first.  If history is shown, start with the
  ** original, unedited post.  Otherwise advance to the post's latest edit.  */
  if( mode==FD_RAW || mode==FD_SINGLE ){
    p = pSelect;
    if( bHist && p->pEditHead ) p = p->pEditHead;
  }else{
    p = mode==FD_CHRONO ? pThread->pFirst : pThread->pDisplay;
    if( !bHist && p->pEditTail ) p = p->pEditTail;
  }

  /* Display the appropriate subset of posts in sequence. */
  while( p ){
    /* Display the post. */
    forum_display_post(
      pThread, p, iIndentScale,
      (mode==FD_RAW ? FDISPLAY_RAW : 0) |
      (bUnf ? FDISPLAY_UNFORMATTED : 0) |
      (bHist ? FDISPLAY_HISTORY : 0) |
      (p==pSelect ? FDISPLAY_SELECTED : 0),
      zQuery
    );

    /* Advance to the next post in the thread. */
    if( mode==FD_CHRONO ){
      /* Chronological mode: display posts (optionally including edits) in their
      ** original commit order. */
      if( bHist ){
        p = p->pNext;
      }else{
        sid = p->sid;
        if( p->pEditHead ) p = p->pEditHead;
        do p = p->pNext; while( p && p->sid<=sid );
        if( p && p->pEditTail ) p = p->pEditTail;
      }
    }else if( bHist && p->pEditNext ){
      /* Hierarchical and single mode: display each post's edits in sequence. */
      p = p->pEditNext;
    }else if( mode==FD_HIER ){
      /* Hierarchical mode: after displaying with each post (optionally
      ** including edits), go to the next post in computed display order. */
      p = p->pEditHead ? p->pEditHead->pDisplay : p->pDisplay;
      if( !bHist && p && p->pEditTail ) p = p->pEditTail;
    }else{
      /* Single and raw mode: terminate after displaying the selected post and
      ** (optionally) its edits. */
      break;
    }
  }

  /* Undocumented "threadtable" query parameter causes thread table to be
  ** displayed for debugging purposes. */
  if( PB("threadtable") ){
    @ <hr>
    @ <table border="1" cellpadding="3" cellspacing="0">
    @ <tr><th>sid<th>rev<th>fpid<th>pIrt<th>pEditHead<th>pEditTail\
    @ <th>pEditNext<th>pEditPrev<th>pDisplay<th>hash
    for(p=pThread->pFirst; p; p=p->pNext){
      @ <tr><td>%d(p->sid)<td>%d(p->rev)<td>%d(p->fpid)\
      @ <td>%d(p->pIrt ? p->pIrt->fpid : 0)\
      @ <td>%d(p->pEditHead ? p->pEditHead->fpid : 0)\
      @ <td>%d(p->pEditTail ? p->pEditTail->fpid : 0)\
      @ <td>%d(p->pEditNext ? p->pEditNext->fpid : 0)\
      @ <td>%d(p->pEditPrev ? p->pEditPrev->fpid : 0)\
      @ <td>%d(p->pDisplay ? p->pDisplay->fpid : 0)\
      @ <td>%S(p->zUuid)</tr>
    }
    @ </table>
  }

  /* Clean up. */
  forumthread_delete(pThread);
}

/*
** Emit Forum Javascript which applies (or optionally can apply)
** to all forum-related pages. It does not include page-specific
** code (e.g. "forum.js").
*/
static void forum_emit_js(void){
  builtin_fossil_js_bundle_or("copybutton", "pikchr", "confirmer",
                              "attach", "tabs", "storage",
                              "popupwidget", NULL);
  builtin_request_js("fossil.page.forumpost.js");
}

/*
** WEBPAGE: forumpost
**
** Show a single forum posting. The posting is shown in context with
** its entire thread.  The selected posting is enclosed within
** <div class='forumSel'>...</div>.  Javascript is used to move the
** selected posting into view after the page loads.
**
** Query parameters:
**
**   name=X        REQUIRED.  The hash of the post to display.
**   t=a           Automatic display mode, i.e. hierarchical for
**                 desktop and chronological for mobile.  This is the
**                 default if the "t" query parameter is omitted.
**   t=c           Show posts in the order they were written.
**   t=h           Show posts using hierarchical indenting.
**   t=s           Show only the post specified by "name=X".
**   t=r           Alias for "t=c&unf&hist".
**   t=y           Alias for "t=s&unf&hist".
**   raw           Alias for "t=s&unf".  Additionally, omit the border
**                 around the post, and ignore "t" and "hist".
**   unf           Show the original, unformatted source text.
**   hist          Show edit history in addition to current posts.
*/
void forumpost_page(void){
  forumthread_page();
}

/*
** WEBPAGE: forumthread
**
** Show all forum messages associated with a particular message thread.
** The result is basically the same as /forumpost except that none of
** the postings in the thread are selected.
**
** Query parameters:
**
**   name=X        REQUIRED.  The hash of any post of the thread.
**   t=a           Automatic display mode, i.e. hierarchical for
**                 desktop and chronological for mobile.  This is the
**                 default if the "t" query parameter is omitted.
**   t=c           Show posts in the order they were written.
**   t=h           Show posts using hierarchical indenting.
**   unf           Show the original, unformatted source text.
**   hist          Show edit history in addition to current posts.
*/
void forumthread_page(void){
  int fpid;
  int froot;
  char *zThreadTitle;
  const char *zName = P("name");
  const char *zMode = PD("t","a");
  int bRaw = PB("raw");
  int bUnf = PB("unf");
  int bHist = PB("hist");
  int mode = 0;
  int autoMode = 0;
  login_check_credentials();
  if( !g.perm.RdForum ){
    login_needed(g.anon.RdForum);
    return;
  }
  if( zName==0 ){
    webpage_error("Missing \"name=\" query parameter");
  }
  cgi_check_for_malice();
  fpid = symbolic_name_to_rid(zName, "f");
  if( fpid<=0 ){
    if( fpid==0 ){
      webpage_notfound_error("Unknown forum id: \"%s\"", zName);
    }else{
      ambiguous_page();
    }
    return;
  }
  froot = db_int(0, "SELECT froot FROM forumpost WHERE fpid=%d", fpid);
  if( froot==0 ){
    webpage_notfound_error("Not a forum post: \"%s\"", zName);
  }

  /* Decode the mode parameters. */
  if( bRaw ){
    mode = FD_RAW;
    bUnf = 1;
    bHist = 0;
    cgi_replace_query_parameter("unf", "on");
    cgi_delete_query_parameter("hist");
    cgi_delete_query_parameter("raw");
  }else{
    switch( *zMode ){
      case 'a': mode = cgi_from_mobile() ? FD_CHRONO : FD_HIER;
                autoMode=1; break;
      case 'c': mode = FD_CHRONO; break;
      case 'h': mode = FD_HIER; break;
      case 's': mode = FD_SINGLE; break;
      case 'r': mode = FD_CHRONO; break;
      case 'y': mode = FD_SINGLE; break;
      default: webpage_error("Invalid thread mode: \"%s\"", zMode);
    }
    if( *zMode=='r' || *zMode=='y') {
      bUnf = 1;
      bHist = 1;
      cgi_replace_query_parameter("t", mode==FD_CHRONO ? "c" : "s");
      cgi_replace_query_parameter("unf", "on");
      cgi_replace_query_parameter("hist", "on");
    }
  }

  /* Define the page header. */
  zThreadTitle = db_text("",
    "SELECT"
    " substr(event.comment,instr(event.comment,':')+2)"
    " FROM forumpost, event"
    " WHERE event.objid=forumpost.fpid"
    "   AND forumpost.fpid=%d;",
    fpid
  );
  style_set_current_feature("forum");
  style_header("%s%s", zThreadTitle, *zThreadTitle ? "" : "Forum");
  fossil_free(zThreadTitle);
  if( mode!=FD_CHRONO ){
    style_submenu_element("Chronological", "%R/%s/%s?t=c%s%s", g.zPath, zName,
        bUnf ? "&unf" : "", bHist ? "&hist" : "");
  }
  if( mode!=FD_HIER ){
    style_submenu_element("Hierarchical", "%R/%s/%s?t=h%s%s", g.zPath, zName,
        bUnf ? "&unf" : "", bHist ? "&hist" : "");
  }
  style_submenu_checkbox("unf", "Unformatted", 0, 0);
  style_submenu_checkbox("hist", "History", 0, 0);
  if( g.perm.Admin ){
    style_submenu_element("Artifacts", "%R/forumthreadhashlist/%t", zName);
  }

  /* Display the thread. */
  if( fossil_strcmp(g.zPath,"forumthread")==0 ) fpid = 0;
  forum_display_thread(froot, fpid, mode, autoMode, bUnf, bHist);

  /* Emit Forum Javascript. */
  builtin_request_js("forum.js");
  forum_emit_js();

  /* Emit the page style. */
  style_finish_page();
}

/*
** Return true if a forum post should be moderated.
*/
int forum_need_moderation(void){
  if( P("domod") ) return 1;
  if( g.perm.WrTForum ) return 0;
  if( g.perm.ModForum ) return 0;
  return 1;
}

/* Flags for use with forum_post() */
#define FPOST_NO_ALERT 1 /* do not send any alerts */
#define FPOST_DRYRUN   2 /* do not save the artifact */

/*
** Return a flags value for use with the final argument to
** forum_post(), extracted from the CGI environment.
*/
static int forum_post_flags(void){
  int iPostFlags = 0;
  if( g.perm.Debug && P("fpsilent")!=0 ){
    iPostFlags |= FPOST_NO_ALERT;
  }
  if( P("dryrun")!=0 ){
    iPostFlags |= FPOST_DRYRUN;
  }
  return iPostFlags;
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
  const char *zContent,        /* Content */
  int iFlags                   /* FPOST_xyz flag values */
){
  char *zDate;
  char *zI;
  char *zG;
  int iBasis;
  Blob x, cksum, formatCheck, errMsg;
  Manifest *pPost;
  int nContent = zContent ? (int)strlen(zContent) : 0;

  schema_forum();
  if( !g.perm.Admin && (iEdit || iInReplyTo)
      && forum_rid_is_tagged(iEdit ? iEdit : iInReplyTo, "closed", 1) ){
    forumpost_error_closed();
    return 0;
  }
  if( iEdit==0 && fossil_all_whitespace(zContent) ){
    return 0;
  }
  if( iInReplyTo==0 && iEdit>0 ){
    iBasis = iEdit;
    iInReplyTo = db_int(0, "SELECT firt FROM forumpost WHERE fpid=%d", iEdit);
  }else{
    iBasis = iInReplyTo;
  }
  webpage_assert( (zTitle==0)+(iInReplyTo==0)==1 );
  blob_init(&x, 0, 0);
  zDate = date_in_standard_format("now");
  blob_appendf(&x, "D %s\n", zDate);
  fossil_free(zDate);
  zG = db_text(0,
     "SELECT uuid FROM blob, forumpost"
     " WHERE blob.rid==forumpost.froot"
     "   AND forumpost.fpid=%d", iBasis);
  if( zG ){
    blob_appendf(&x, "G %s\n", zG);
    fossil_free(zG);
  }
  if( zTitle ){
    blob_appendf(&x, "H %F\n", zTitle);
  }
  zI = rid_to_uuid(iInReplyTo);
  if( zI ){
    blob_appendf(&x, "I %s\n", zI);
    fossil_free(zI);
  }
  if( zMimetype!=0
      && zMimetype[0]!=0
      && fossil_strcmp(zMimetype,"text/x-fossil-wiki")!=0 ){
    blob_appendf(&x, "N %F\n", zMimetype);
  }
  if( iEdit>0 ){
    char *zP = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", iEdit);
    if( zP==0 ) webpage_error("missing edit artifact %d", iEdit);
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
  blob_appendf(&x, "W %d\n%s\n", nContent, zContent);
  md5sum_blob(&x, &cksum);
  blob_appendf(&x, "Z %b\n", &cksum);
  blob_reset(&cksum);

  /* Verify that the artifact we are creating is well-formed */
  blob_init(&formatCheck, 0, 0);
  blob_init(&errMsg, 0, 0);
  blob_copy(&formatCheck, &x);
  pPost = manifest_parse(&formatCheck, 0, &errMsg);
  if( pPost==0 ){
    webpage_error("malformed forum post artifact - %s", blob_str(&errMsg));
  }
  webpage_assert( pPost->type==CFTYPE_FORUM );
  manifest_destroy(pPost);

  if( (iFlags & FPOST_DRYRUN)!=0 ){
    @ <div class='debug'>
    @ This is the artifact that would have been generated:
    @ <pre>%h(blob_str(&x))</pre>
    @ </div>
    blob_reset(&x);
    return 0;
  }else{
    int nrid;
    db_begin_transaction();
    nrid = wiki_put(&x, iEdit>0 ? iEdit : 0, forum_need_moderation());
    blob_reset(&x);
    if( (iFlags & FPOST_NO_ALERT)!=0 ){
      alert_unqueue('f', nrid);
    }
    db_commit_transaction();
    cgi_redirectf("%R/forumpost/%S", rid_to_uuid(nrid));
    return 1;
  }
}

/*
** Paint the form elements for entering a Forum post
*/
static void forum_post_widget(
  const char *zTitle,
  const char *zMimetype,
  const char *zContent
){
  if( zTitle ){
    @ Title: <input type="input" name="title" value="%h(zTitle)" size="50"
    @ maxlength="125"><br>
  }
  @ %z(href("%R/markup_help"))Markup style</a>:
  mimetype_option_menu(zMimetype, "mimetype");
  @ <div class="forum-editor-widget">
  @ <textarea aria-label="Content:" name="content" class="wikiedit" \
  @ cols="80" rows="25" wrap="virtual">%h(zContent)</textarea></div>
}

/*
** If PD("fpid") refers to a forum post, its rid is returned, else
** this function emits an error does not does return.
*/
static int forum_validate_fpid_param(void){
  const char *zFpid = PD("fpid","");
  int fpid = symbolic_name_to_rid(zFpid, "f");
  if( fpid<=0 ){
    webpage_error("Missing or invalid fpid parameter.");
  }
  return fpid;
}

/*
** Internal helper for /forumpost_XYZ internal pages which tag/untag
** posts.
*/
static void forumpost_action_helper(const char *zTag, const char *zVal,
                                    int addTag, int validFpid){
  if( !cgi_csrf_safe(2) ){
    webpage_error("CSRF validation failed");
  }else{
    const int fpid = validFpid>0 ? validFpid : forum_validate_fpid_param();
    if( fpid>0 ){
      if( forumpost_tag(fpid, addTag, zTag, zVal) < 0 ){
        webpage_error("Tagging artifact failed: %s", g.zErrMsg);
      }else{
        cgi_redirectf("%R/forumpost/%S",P("fpid"));
      }
    }
  }
}

/*
** WEBPAGE: forumpost_close hidden
** WEBPAGE: forumpost_reopen hidden
**
**   fpid=X        Hash of the post to be edited.  REQUIRED
**
** Closes or re-opens the given forum post, within the bounds of the
** API for forumpost_tag(). After (perhaps) modifying the "closed"
** status of the given thread, it redirects to that post's thread
** view. Requires admin privileges.
*/
void forum_page_close(void){
  login_check_credentials();
  if( forumpost_may_close()==0 ){
    login_needed(g.anon.Admin);
  }else{
    const int bIsAdd = sqlite3_strglob("*_close*", g.zPath)==0;
    forumpost_action_helper("closed", 0, bIsAdd, 0);
  }
}

/*
** WEBPAGE: forumpost_status hidden
**
**   fpid=X        Hash of the post to be edited.  REQUIRED
**   status=Y      New status value. REQUIRED
**
** Updates the current status=Y tag on the first version of
** the forum post X. Requires forum_may_set_status() permissions.
*/
void forum_page_status(void){
  int fpid;
  login_check_credentials();
  fpid = forum_validate_fpid_param();
  if(forum_may_set_status(fpid)){
    const char *zStatus = PD("status",0);
    if( !zStatus || !zStatus[0] ){
      webpage_error("Missing required status.");
    }
    forumpost_action_helper("status", zStatus, 1, fpid);
  }else{
    webpage_error("You lack permissions to change this post's status.");
  }
}

/*
** WEBPAGE: forumnew
** WEBPAGE: forumedit
**
** Start a new thread on the forum or reply to an existing thread.
** But first prompt to see if the user would like to log in.
*/
void forum_page_init(void){
  int isEdit;
  char *zGoto;

  login_check_credentials();
  if( !g.perm.WrForum ){
    login_needed(g.anon.WrForum);
    return;
  }
  if( sqlite3_strglob("*edit*", g.zPath)==0 ){
    zGoto = mprintf("forume2?fpid=%S",PD("fpid",""));
    isEdit = 1;
  }else{
    zGoto = mprintf("forume1");
    isEdit = 0;
  }
  if( login_is_individual() ){
    if( isEdit ){
      forumedit_page();
    }else{
      forumnew_page();
    }
    return;
  }
  style_set_current_feature("forum");
  style_header("%h As Anonymous?", isEdit ? "Reply" : "Post");
  @ <p>You are not logged in.
  @ <p><table border="0" cellpadding="10">
  @ <tr><td>
  @ <form action="%s(zGoto)" method="POST">
  @ <input type="submit" value="Remain Anonymous">
  @ </form>
  @ <td>Post to the forum anonymously
  if( login_self_register_available(0) ){
    @ <tr><td>
    @ <form action="%R/register" method="POST">
    @ <input type="hidden" name="g" value="%s(zGoto)">
    @ <input type="submit" value="Create An Account">
    @ </form>
    @ <td>Create a new account and post using that new account
  }
  @ <tr><td>
  @ <form action="%R/login" method="POST">
  @ <input type="hidden" name="g" value="%s(zGoto)">
  @ <input type="hidden" name="noanon" value="1">
  @ <input type="submit" value="Login">
  @ </form>
  @ <td>Log into an existing account
  @ </table>
  forum_emit_js();
  style_finish_page();
  fossil_free(zGoto);
}

/*
** Write the "From: USER" line on the webpage.
*/
static void forum_from_line(void){
  if( login_is_nobody() ){
    @ From: anonymous<br>
  }else{
    @ From: %h(login_name())<br>
  }
}

static void forum_render_debug_options(void){
  if( g.perm.Debug ){
    /* Give extra control over the post to users with the special
     * Debug capability, which includes Admin and Setup users */
    @ <div class="debug">
    @ <label><input type="checkbox" name="dryrun" %s(PCK("dryrun"))> \
    @ Dry run</label>
    @ <br><label><input type="checkbox" name="domod" %s(PCK("domod"))> \
    @ Require moderator approval</label>
    @ <br><label><input type="checkbox" name="showqp" %s(PCK("showqp"))> \
    @ Show query parameters</label>
    @ <br><label><input type="checkbox" name="fpsilent" %s(PCK("fpsilent"))> \
    @ Do not send notification emails</label>
    @ </div>
  }
}

/*
** If the user has AttachForum permissions, emit a notice that
** attachments may be added after saving. If p is not NULL,
** also emit its list of attachments.
*/
static void forum_render_attachment_notice(void){
  if( g.perm.AttachForum ){
    @ <div>You will be able to attach files to this post after saving
    @ it.</div>
  }
}

/*
** WEBPAGE: forume1 hidden
**
** Start a new forum thread.
*/
void forumnew_page(void){
  const char *zTitle = PDT("title","");
  const char *zMimetype = PD("mimetype",DEFAULT_FORUM_MIMETYPE);
  const char *zContent = PDT("content","");
  const int bLegacy = PB("legacy"); /* True for legacy HTML form */

  login_check_credentials();
  if( !g.perm.WrForum ){
    login_needed(g.anon.WrForum);
    return;
  }
  if( P("submit") && cgi_csrf_safe(2) ){
    if( forum_post(zTitle, 0, 0, 0, zMimetype, zContent,
                   forum_post_flags()) ) return;
  }
  if( P("preview") && !fossil_all_whitespace(zContent) ){
    @ <h1>Preview:</h1>
    forum_render(zTitle, zMimetype, zContent, "forumEdit", 1);
  }
  style_set_current_feature("forum");
  style_header("New Forum Thread");

  if( !bLegacy ){
    @ <noscript>
  }
  @ <form action="%R/forume1" method="POST">
  @ <h1>New Thread:</h1>
  forum_from_line();
  forum_post_widget(zTitle, zMimetype, zContent);
  @ <input type="submit" name="preview" value="Preview">
  @ <input type="hidden" name="legacy" value="1">
  if( P("preview") && !fossil_all_whitespace(zContent) ){
    @ <input type="submit" name="submit" value="Submit">
  }else{
    @ <input type="submit" name="submit" value="Submit" disabled>
  }
  forum_render_debug_options();
  login_insert_csrf_secret();
  @ </form>
  forum_render_attachment_notice();
  if( !bLegacy ){
    @ </noscript>
    /* When JS is disabled the block above will work.
       When it's enabled, the above won't do anything and
       JS will render the editor form in the next element. */
    @ <div hidden id='forumnew-placeholder'>
    @ <input type='hidden' name='title' value='%h(zTitle)'>
    login_insert_csrf_secret();
    captcha_generate_for_js(0);
    @ </div>
  }
  forum_emit_js();
  style_finish_page();
}

/*
** WEBPAGE: forume2 hidden
**
** Edit an existing forum message.
** Query parameters:
**
**   fpid=X        Hash of the post to be edited.  REQUIRED
*/
void forumedit_page(void){
  int fpid;
  int froot;
  Manifest *pPost = 0;
  Manifest *pRootPost = 0;
  const char *zMimetype = 0;
  const char *zContent = 0;
  const char *zTitle = 0;
  char *zDate = 0;
  const char *zFpid = PD("fpid","");
  const int bLegacy = 1 ? 1 : PB("legacy"); /* True for legacy HTML form */
  int isCsrfSafe;
  int isDelete = 0;
  int iClosed = 0;
  int bSameUser;        /* True if author is also the reader */
  int bPreview;         /* True in preview mode. */
  int bPrivate;         /* True if post is private (not yet moderated) */
  int bReply;           /* True if replying to a post */

  if( !bLegacy ){
    forumedit_page_v2();
    return;
  }

  login_check_credentials();
  if( !g.perm.WrForum ){
    login_needed(g.anon.WrForum);
    return;
  }
  fpid = symbolic_name_to_rid(zFpid, "f");
  if( fpid<=0 || (pPost = manifest_get(fpid, CFTYPE_FORUM, 0))==0 ){
    webpage_error("Missing or invalid fpid query parameter");
  }
  froot = db_int(0, "SELECT froot FROM forumpost WHERE fpid=%d", fpid);
  if( (froot==0 || (pRootPost = manifest_get(froot, CFTYPE_FORUM, 0))==0)
   && P("reject")==0
  ){
    webpage_error("fpid does not appear to be a forum post: \"%d\"", fpid);
  }
  if( P("cancel") ){
    cgi_redirectf("%R/forumpost/%S",zFpid);
    return;
  }
  bPreview = P("preview")!=0;
  bReply = P("reply")!=0;
  iClosed = forum_rid_is_tagged(fpid, "closed", 1);
  isCsrfSafe = cgi_csrf_safe(2);
  bPrivate = content_is_private(fpid);
  bSameUser = login_is_individual()
    && fossil_strcmp(pPost->zUser, g.zLogin)==0;
  if( isCsrfSafe && (g.perm.ModForum || (bPrivate && bSameUser)) ){
    if( g.perm.ModForum && P("approve") ){
      const char *zUserToTrust;
      moderation_forumpost_approve(fpid);
      if( g.perm.AdminForum
       && PB("trust")
       && (zUserToTrust = P("trustuser"))!=0
      ){
        db_unprotect(PROTECT_USER);
        db_multi_exec("UPDATE user SET cap=cap||'4' "
                      "WHERE login=%Q AND cap NOT GLOB '*4*'",
                      zUserToTrust);
        db_protect_pop();
      }
      cgi_redirectf("%R/forumpost/%S",P("fpid"));
      return;
    }
    if( P("reject") ){
      char *zParent =
        db_text(0,
          "SELECT uuid FROM forumpost, blob"
          " WHERE forumpost.fpid=%d AND blob.rid=forumpost.firt",
          fpid
        );
      moderation_forumpost_disapprove(fpid);
      if( zParent ){
        cgi_redirectf("%R/forumpost/%S",zParent);
      }else{
        cgi_redirectf("%R/forum");
      }
      return;
    }
  }
  style_set_current_feature("forum");
  isDelete = P("nullout")!=0;
  if( P("submit")
   && isCsrfSafe
   && (zContent = PDT("content",""))!=0
   && (isDelete || !fossil_all_whitespace(zContent))
  ){
    int done = 1;
    const char *zMimetype = PD("mimetype",DEFAULT_FORUM_MIMETYPE);
    if( bReply ){
      done = forum_post(0, fpid, 0, 0, zMimetype, zContent,
                        forum_post_flags());
    }else if( P("edit") || isDelete ){
      done = forum_post(P("title"), 0, fpid, 0, zMimetype, zContent,
                        forum_post_flags());
    }else{
      webpage_error("Missing 'reply' query parameter");
    }
    if( done ) return;
  }
  if( isDelete ){
    zMimetype = "text/x-fossil-wiki";
    zContent = "";
    if( pPost->zThreadTitle ) zTitle = "";
    style_header("Delete %s", zTitle ? "Post" : "Reply");
    @ <h1>Original Post:</h1>
    forum_render(pPost->zThreadTitle, pPost->zMimetype, pPost->zWiki,
                 "forumEdit", 1);
    @ <h1>Change Into:</h1>
    forum_render(zTitle, zMimetype, zContent,"forumEdit", 1);
    @ <form action="%R/forume2" method="POST">
    login_insert_csrf_secret();
    @ <input type="hidden" name="fpid" value="%h(P("fpid"))">
    @ <input type="hidden" name="nullout" value="1">
    @ <input type="hidden" name="mimetype" value="%h(zMimetype)">
    @ <input type="hidden" name="content" value="%h(zContent)">
    if( zTitle ){
      @ <input aria-label="Title" type="hidden" name="title" value="%h(zTitle)">
    }
  }else if( P("edit") ){
    /* Provide an edit to the fpid post */
    zMimetype = P("mimetype");
    zContent = PT("content");
    zTitle = P("title");
    if( zContent==0 ) zContent = fossil_strdup(pPost->zWiki);
    if( zMimetype==0 ) zMimetype = fossil_strdup(pPost->zMimetype);
    if( zTitle==0 && pPost->zThreadTitle!=0 ){
      zTitle = fossil_strdup(pPost->zThreadTitle);
    }
    style_header("Edit %s", zTitle ? "Post" : "Reply");
    @ <h2>Original Post:</h2>
    forum_render(pPost->zThreadTitle, pPost->zMimetype, pPost->zWiki,
                 "forumEdit", 1);
    if( bPreview ){
      @ <h2>Preview of Edited Post:</h2>
      forum_render(zTitle, zMimetype, zContent,"forumEdit", 1);
    }
    @ <h2>Revised Message:</h2>
    @ <form action="%R/forume2" method="POST">
    login_insert_csrf_secret();
    @ <input type="hidden" name="fpid" value="%h(P("fpid"))">
    @ <input type="hidden" name="edit" value="1">
    forum_from_line();
    forum_post_widget(zTitle, zMimetype, zContent);
  }else{
    /* Reply */
    char *zDisplayName;
    zMimetype = PD("mimetype",DEFAULT_FORUM_MIMETYPE);
    zContent = PDT("content","");
    style_header("Reply");
    @ <h2>Replying to
    @ <a href="%R/forumpost/%!S(zFpid)" target="_blank">%S(zFpid)</a>
    if( pRootPost->zThreadTitle ){
      @ in thread
      @ <span class="forumPostReplyTitle">%h(pRootPost->zThreadTitle)</span>
    }
    @ </h2>
    zDate = db_text(0, "SELECT datetime(%.17g,toLocal())", pPost->rDate);
    zDisplayName = display_name_from_login(pPost->zUser);
    @ <h3 class='forumPostHdr'>By %s(zDisplayName) on %h(zDate)</h3>
    fossil_free(zDisplayName);
    fossil_free(zDate);
    forum_render(0, pPost->zMimetype, pPost->zWiki, "forumEdit", 1);
    if( bPreview && !fossil_all_whitespace(zContent) ){
      @ <h2>Preview:</h2>
      forum_render(0, zMimetype,zContent, "forumEdit", 1);
    }
    @ <h2>Enter Reply:</h2>
    @ <form action="%R/forume2" method="POST">
    @ <input type="hidden" name="fpid" value="%h(P("fpid"))">
    @ <input type="hidden" name="reply" value="1">
    forum_from_line();
    forum_post_widget(0, zMimetype, zContent);
  }
  if( !isDelete ){
    @ <input type="submit" name="preview" value="Preview">
  }
  @ <input type="hidden" name="legacy" value="1">
  @ <input type="submit" name="cancel" value="Cancel">
  if( isDelete || (bPreview && !fossil_all_whitespace(zContent)) ){
    if( !iClosed || g.perm.Admin ) {
      @ <input type="submit" name="submit" value="Submit">
    }
  }
  forum_render_debug_options();
  login_insert_csrf_secret();
  @ </form>
  if( !bReply ){
    forum_render_attachment_list(rid_to_uuid(fpid));
  }
  forum_render_attachment_notice();
  forum_emit_js();
  style_finish_page();
}

/*
** WEBPAGE: forume2_v2 hidden
**
** A work in progress.
*/
void forumedit_page_v2(void){
  const char *zFpid = PD("fpid","");

  login_check_credentials();
  if( !g.perm.WrForum ){
    login_needed(g.anon.WrForum);
    return;
  }
  style_set_current_feature("forum");
  style_header("Edit Forum Post");
  (void)zFpid;
  @ Much to do here.
  @ <div hidden id='forumedit-placeholder'>
#if 0
  @ <input type='hidden' name='title' value='%h(zTitle)'>
#endif
  login_insert_csrf_secret();
  captcha_generate_for_js(0);
  @ </div>
  forum_emit_js();
  style_finish_page();

}

/*
** SETTING: forum-close-policy    boolean default=off
** If true, forum moderators may close/re-open forum posts, and reply
** to closed posts. If false, only administrators may do so. Note that
** this only affects the forum web UI, not post-closing tags which
** arrive via the command-line or from synchronization with a remote.
** This policy also determines whether moderators may delete forum
** attachments.
*/
/*
** SETTING: forum-title          width=20 default=Forum
** This is the name or "title" of the Forum for this repository.  The
** default is just "Forum".  But in some setups, admins might want to
** change it to "Developer Forum" or "User Forum" or whatever other name
** seems more appropriate for the particular usage.
**
** SETTING: attachment-size-limit    width=16
** The maximum number of bytes for an attachment. The default (or 0) is
** unlimited but a limit may be imposed by the web server or a proxy.
**
** SETTING: forum-statuses        width=40 block-text
** This JSON5-formatted value defines an array of objects describing
** the available statuses of forum posts. Each entry of the array must
** be an object in the form {label:"X",value:"Y"}.
** The label is used in the UI and value becomes the value of the
** "status" tag on forum posts. Any forum post which has a status
** value which does not appear in this list is treated as if it had
** the first value from this list. If this setting is empty, is
** ill-formed JSON, or has only a single entry then the forum will
** lack the capability of setting and filtering by status.
*/

/*
** WEBPAGE: setup_forum
**
** Forum configuration and metrics.
*/
void forum_setup(void){
  /* boolean config settings specific to the forum. */
  static const char *azForumSettings[] =  {
    "forum-close-policy",
    "forum-title",
  };

  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(g.anon.Setup);
    return;
  }
  style_set_current_feature("forum");
  style_header("Forum Setup");

  @ <h2>Metrics</h2>
  {
    int nPosts = db_int(0, "SELECT COUNT(*) FROM event WHERE type='f'");
    @ <p><a href='%R/forum'>Forum posts</a>:
    @ <a href='%R/timeline?y=f'>%d(nPosts)</a></p>
  }

  @ <h2>Supervisors</h2>
  {
    Stmt q = empty_Stmt;
    db_prepare(&q, "SELECT uid, login, cap FROM user "
                   "WHERE cap GLOB '*[as6]*' ORDER BY login");
    @ <table class='bordered'>
    @ <thead><tr><th>User</th><th>Capabilities</th></tr></thead>
    @ <tbody>
    while( SQLITE_ROW==db_step(&q) ){
      const int iUid = db_column_int(&q, 0);
      const char *zUser = db_column_text(&q, 1);
      const char *zCap = db_column_text(&q, 2);
      @ <tr>
      @ <td><a href='%R/setup_uedit?id=%d(iUid)'>%h(zUser)</a></td>
      @ <td>(%h(zCap))</td>
      @ </tr>
    }
    db_finalize(&q);
    @</tbody></table>
  }

  @ <h2>Moderators</h2>
  if( db_int(0, "SELECT count(*) FROM user "
                " WHERE cap GLOB '*5*' AND cap NOT GLOB '*[as6]*'")==0 ){
      @ <p>No non-supervisor moderators
  }else{
    Stmt q = empty_Stmt;
    db_prepare(&q, "SELECT uid, login, cap FROM user "
               "WHERE cap GLOB '*5*' AND cap NOT GLOB '*[as6]*'"
               " ORDER BY login");
    @ <table class='bordered'>
    @ <thead><tr><th>User</th><th>Capabilities</th></tr></thead>
    @ <tbody>
    while( SQLITE_ROW==db_step(&q) ){
      const int iUid = db_column_int(&q, 0);
      const char *zUser = db_column_text(&q, 1);
      const char *zCap = db_column_text(&q, 2);
      @ <tr>
      @ <td><a href='%R/setup_uedit?id=%d(iUid)'>%h(zUser)</a></td>
      @ <td>(%h(zCap))</td>
      @ </tr>
    }
    db_finalize(&q);
    @ </tbody></table>
  }

  @ <h2>Settings</h2>
  if( P("submit") && cgi_csrf_safe(2) ){
    int i = 0;
    db_begin_transaction();
    for(i=0; i<ArraySize(azForumSettings); i++){
      char zQP[4];
      const char *z;
      const Setting *pSetting = setting_find(azForumSettings[i]);
      if( pSetting==0 ) continue;
      zQP[0] = 'a'+i;
      zQP[1] = zQP[0];
      zQP[2] = 0;
      z = P(zQP);
      if( z==0 || z[0]==0 ) continue;
      db_set(pSetting->name/*works-like:"x"*/, z, 0);
    }
    db_end_transaction(0);
    @ <p><em>Settings saved.</em></p>
  }
  {
    int i = 0;
    @ <form action="%R/setup_forum" method="post">
    login_insert_csrf_secret();
    @ <table class='forum-settings-list'><tbody>
    for(i=0; i<ArraySize(azForumSettings); i++){
      char zQP[4];
      const Setting *pSetting = setting_find(azForumSettings[i]);
      if( pSetting==0 ) continue;
      zQP[0] = 'a'+i;
      zQP[1] = zQP[0];
      zQP[2] = 0;
      if( pSetting->width==0 ){
        /* Boolean setting */
        @ <tr><td align="right">
        @ <a href='%R/help/%h(pSetting->name)'>%h(pSetting->name)</a>:
        @ </td><td>
        onoff_attribute("", zQP, pSetting->name/*works-like:"x"*/, 0, 0);
        @ </td></tr>
      }else{
        /* Text value setting */
        @ <tr><td align="right">
        @ <a href='%R/help/%h(pSetting->name)'>%h(pSetting->name)</a>:
        @ </td><td>
        entry_attribute("", 25, pSetting->name, zQP/*works-like:""*/,
                        pSetting->def, 0);
        @ </td></tr>
      }
    }
    @ </tbody></table>
    @ <input type='submit' name='submit' value='Apply changes'>
    @ </form>
  }

  style_finish_page();
}

/*
** If the forum-statuses setting is active and has 2 or more entries,
** this adds a submenu for selecting the status filter, else it emits
** nothing.
*/
static void forum_status_submenu(void){
  const ForumStatusList * const fss = forum_statuses();
  static int i = 0;
  static const char **az;
  if( i==0 && fss->n>1 ){
    unsigned j;
    az = fossil_malloc(sizeof(az[0]) * ((1 + fss->n) * 2));
    az[i++] = "*";
    az[i++] = "Any status";
    for( j = 0; j < fss->n; ++j ){
      const ForumStatus * fs = &fss->aStatus[j];
      /* Potential TODO: skip any entries for which there are no
      ** forum posts with a status=${fs->zValue} tag. */
      az[i++] = fs->zValue;
      az[i++] = fs->zLabel;
    }
    //assert( i==(1+fss->n)*2 );
  }
  if( i ){
    cookie_link_parameter("status","forumStatus","*");
    style_submenu_multichoice("status", i/2, az, 0);
  }
}

/*
** Transient SQL Function:    status_match(FROOT)
**
** Return true if the forum thread identified by FROOT should be included
** in a list of threads.  Used to implement the status=NAME query parameter
** on /forum.
**
** The result of this routine depends on the content of the
** ForumStatusMatch *pMData object that is available via sqlite3_user_data().
**
**    *   If pMData==NULL, always return true.  This means that no
**        filtering of threads is being done.  This is the common case.
**
**    *   If FROOT contains a status property value that matches
**        pMData->iMatch, return true.
**
**    *   if pMData->iMatch==0 (meaning we want to match the default
**        status value) and if the FROOT thread contains a status that
**        is not on the list of statuses or if FROOT has no statue
**        property at all, then return true.  In other words, a forum
**        thread with no status property or an unknown status property
**        is treated as if it had the default status.
**
**    *   Otherwise, return false.
*/
static void forum_status_match(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  static Stmt q;
  ForumStatusMatch *pMData = sqlite3_user_data(context);
  int i;

  if( pMData==0 ){
    sqlite3_result_int(context, 1);
    return;
  }
  db_static_prepare(&q,
    "SELECT value FROM tagxref\n"
    " WHERE tagid=%d\n"
    "   AND tagtype>=1\n"
    "   AND rid=:rid\n"
    " ORDER BY mtime DESC LIMIT 1",
    pMData->eStatusTag
  );
  db_bind_int(&q, ":rid", sqlite3_value_int(argv[0]));
  if( db_step(&q)==SQLITE_ROW ){
    const char *zValue = (const char*)db_column_text(&q,0);
    const ForumStatusList *pFses = pMData->pFses;
    if( zValue==0 ){
      i = 0;
    }else{
      for(i=0; i<pFses->n; i++){
        if( fossil_strcmp(pFses->aStatus[i].zValue,zValue)==0 ) break;
      }
    }
    if( i>=pMData->pFses->n ) i = 0;
  }else{
    i = 0;
  }
  db_reset(&q);
  sqlite3_result_int(context, i==pMData->iMatch);
}

/*
** WEBPAGE: forummain
** WEBPAGE: forum
**
** The main page for the forum feature.  Show a list of recent forum
** threads.  Also show a search box at the top if search is enabled,
** and a button for creating a new thread, if enabled.
**
** Query parameters:
**
**    n=N             The number of threads to show on each page
**    x=X             Skip the first X threads
**    s=Y             Search for term Y.
*/
void forum_main_page(void){
  Stmt q;
  int iLimit = 0, iOfst, iCnt;
  int srchFlags;
  const int isSearch = P("s")!=0;
  const char *zStatusFilter;
  char const *zLimit = 0;    /* Value of the n= query parameter */
  int eStatusTag = 0;        /* tagid for the "status" property */
  int bHasStatus = 0;        /* True if forum-statuses setting exists */
  int bFilter = 0;           /* True if status=NAME query parameter */
  int bHasForum = 0;         /* True if forumpost table exists */
  const ForumStatusList *pFstat = forum_statuses();
  ForumStatusMatch sFSM;     /* Aux data to status_match() SQL function */

  login_check_credentials();
  srchFlags = search_restrict(SRCH_FORUM);
  if( !g.perm.RdForum ){
    login_needed(g.anon.RdForum);
    return;
  }
  cgi_check_for_malice();
  bHasForum = db_table_exists("repository","forumpost");
  if( bHasForum ){
    eStatusTag = db_int(0, "SELECT tagid FROM tag WHERE tagname='status'");
    bHasStatus = pFstat->n>1;
  }
  style_set_current_feature("forum");
  style_header("%s%s", db_get("forum-title","Forum"),
                       isSearch ? " Search Results" : "");
  style_submenu_element("Timeline", "%R/timeline?ss=v&y=f&vfx");
  if( g.perm.WrForum ){
    style_submenu_element("New Thread","%R/forumnew");
  }else{
    style_submenu_element("New Thread","%R/login");
  }
  if( g.perm.ModForum && moderation_needed() ){
    style_submenu_element("Moderation Requests", "%R/modreq");
  }
  if( (srchFlags & SRCH_FORUM)!=0 ){
    if( search_screen(SRCH_FORUM, 0) ){
      style_submenu_element("Recent Threads","%R/forum");
      style_finish_page();
      return;
    }
  }
  cookie_read_parameter("n","forum-n");
  zLimit = P("n");
  if( zLimit!=0 ){
    iLimit = atoi(zLimit);
    if( iLimit>=0 && P("udc")!=0 ){
      cookie_write_parameter("n","forum-n",0);
    }
  }
  if( iLimit<=0 ){
    cgi_replace_query_parameter("n", fossil_strdup("25"))
      /*for the sake of Max, below*/;
    iLimit = 25;
  }
  style_submenu_entry("n","Max:",4,0);
  forum_status_submenu();
  zStatusFilter = P("status") /*must be after forum_status_submenu()!*/;
  iOfst = atoi(PD("x","0"));
  iCnt = 0;
  if( zStatusFilter ){
    if( zStatusFilter[0]==0 || 0==fossil_strcmp("*",zStatusFilter) ){
      zStatusFilter = 0;
    }else{
      bFilter = bHasStatus;
    }
  }
  if( bHasForum ){
    Stmt qStat = empty_Stmt;     /* Query to get status information */
    if( bHasStatus ){
      /* The qStat query runs once for each output row generate by the
      ** q query.  It determines the value and label of the status for
      ** the row with froot=:rowid
      */
      db_prepare(&qStat,
        "SELECT tagxref.value, forumstatus.label\n"
        " FROM forumstatus, tagxref\n"
        " WHERE tagid=%d AND tagtype>=1\n"
        "   AND forumstatus.value=tagxref.value\n"
        "   AND rid=:rid\n"
        " ORDER BY mtime DESC",
        eStatusTag
      );
    }

    /* Create the status_match() SQL function that will determine
    ** whether or not each thread in the "q" query below is eligible
    ** for display
    */
    if( bFilter ){
      sFSM.pFses = pFstat;
      sFSM.eStatusTag = eStatusTag;
      for(sFSM.iMatch=0; sFSM.iMatch<pFstat->n; sFSM.iMatch++){
        if( 0==fossil_strcmp(zStatusFilter,
                             pFstat->aStatus[sFSM.iMatch].zValue) ){
          break;
        }
      }
      sqlite3_create_function(g.db,"status_match",1,SQLITE_UTF8,(void*)&sFSM,
                              forum_status_match, 0, 0);
    }else{
      sqlite3_create_function(g.db,"status_match",1,SQLITE_UTF8,0,
                              forum_status_match, 0, 0);
    }

    db_prepare(&q,
      "WITH thread(root,endtime,lastrid) AS (\n"
      "  SELECT\n"
      "    froot,\n"
      "    max(fmtime),\n"
      "    fpid\n"
      "  FROM forumpost\n"
      "  WHERE %s/*ModForum*/\n"
      "  GROUP BY froot\n"
      "  HAVING status_match(froot)\n"
      "  ORDER BY 2 DESC\n"
      "  LIMIT %d OFFSET %d\n"
      ")\n"
      "SELECT\n"
      "  julianday('now') - thread.endtime,\n"                  /* 0 */
      "  thread.endtime - "
        "(SELECT fmtime FROM forumpost WHERE fpid=root),\n"     /* 1 */
      "  (SELECT sum(fprev IS NULL) FROM forumpost"
         " WHERE froot=root),\n"                                /* 2 */
      "  blob.uuid,\n"                                          /* 3 */
      "  substr(event.comment,instr(event.comment,':')+1),\n"   /* 4 */
      "  thread.lastrid,\n"                                     /* 5 */
      "  thread.root\n"                                         /* 6 */
      " FROM thread, blob, event\n"
      " WHERE blob.rid=thread.lastrid\n"
      "   AND event.objid=thread.lastrid\n"
      " ORDER BY 1;",
      g.perm.ModForum ? "true" : "fpid NOT IN private" /*safe-for-%s*/,
      iLimit+1, iOfst
    );
    while( db_step(&q)==SQLITE_ROW ){
      char *zAge;
      int nMsg;
      const char *zUuid;
      const char *zTitle;
      const char *zStatus;
      const char *zStatusLbl;
      const int bShowStatus = bHasStatus && !zStatusFilter;
      const int nCols = bShowStatus ? 4 : 3;

      if( qStat.pStmt ){
        /* Determine the status value for this row */
        db_reset(&qStat);
        db_bind_int(&qStat, ":rid", db_column_int(&q,6));
        if( db_step(&qStat)==SQLITE_ROW ){
          zStatus = db_column_text(&qStat, 0);
          zStatusLbl = db_column_text(&qStat, 1);
        }else{
          zStatus = pFstat->aStatus[0].zValue;
          zStatusLbl = pFstat->aStatus[0].zLabel;
        }
      }else{
        zStatus = zStatusLbl = NULL;
      }
      zAge = human_readable_age(db_column_double(&q,0));
      nMsg = db_column_int(&q, 2);
      zUuid = db_column_text(&q, 3);
      zTitle = db_column_text(&q, 4);
      if( iCnt==0 ){
        char *zTail = bFilter ? mprintf(" with status=%Q", zStatusFilter): 0;
        if( iOfst>0 ){
          @ <h1>Threads at least %s(zAge) old%h(zTail ? zTail : "")</h1>
        }else{
          @ <h1>Most recent threads%h(zTail ? zTail : "")</h1>
        }
        fossil_free(zTail);
        @ <div class='forumPosts fileage'><table width="100%%">
        if( iOfst>0 ){
          if( iOfst>iLimit ){
            @ <tr><td colspan="%d(nCols)">\
            @ <a href='%R/forum?x=%d(iOfst-iLimit)&n=%d(iLimit) \
            if( bFilter ){
              @ &status=%T(zStatusFilter)\
            }
            @ '>&uarr; Newer...</a></td></tr>
          }else{
            @ <tr><td colspan="%d(nCols)">\
            @ <a href='%R/forum?n=%d(iLimit)\
            if( bFilter ){
              @ &status=%T(zStatusFilter) \
            }
            @ '>&uarr; Newer...</a></td></tr>
          }
        }
      }
      iCnt++;
      if( iCnt>iLimit ){
        @ <tr><td colspan="%d(nCols)">\
        @ <a href='%R/forum?x=%d(iOfst+iLimit)&n=%d(iLimit) \
        if( bFilter ){
          @ &status=%T(zStatusFilter)\
        }
        @ '>&darr; Older...</a></td></tr>
        fossil_free(zAge);
        break;
      }
      @ <tr \
      if( bHasStatus ){
        @ data-status="%h(zStatus)"\
      }
      @ ><td>%h(zAge) ago</td>
      @ <td class='subject'>%z(href("%R/forumpost/%S",zUuid))%h(zTitle)</a>\
      @ </td><td>\
      if( g.perm.ModForum && moderation_pending(db_column_int(&q,5)) ){
        @ <span class="modpending">\
        @ Awaiting Moderator Approval</span><br>
      }
      if( nMsg<2 ){
        @ no replies\
      }else{
        char *zDuration = human_readable_age(db_column_double(&q,1));
        @ %d(nMsg) posts spanning %h(zDuration)\
        fossil_free(zDuration);
      }
      @ </td>\
      if( bShowStatus ){
        @ <td class='status'>%h(zStatusLbl)</td>\
      }
      if( qStat.pStmt ){
        db_reset(&qStat);
      }
      @</tr>
      fossil_free(zAge);
    }
    db_finalize(&q);
    if( qStat.pStmt ) db_finalize(&qStat);
    sqlite3_create_function(g.db,"status_match",1,SQLITE_UTF8,0,0,0,0);
  }
  if( iCnt>0 ){
    @ </table></div>
  }else{
    @ <h1>No forum posts found</h1>
  }
  if( bHasStatus ){
    /* We need a JS-side kludge to avoid passing on the x=N
    ** URL arg when the status selection list is activated. */
    forum_emit_js();
  }
  style_finish_page();
}

/*
** The AJAX counterpart of forum_post().
**
** Returns the new artifact's RID on success, 0 if no changes were
** necessary (e.g. an empty new post or dry-run mode), and a negative
** value on error. If it returns a negative value then it will have
** populated the ajax response state with an error object.
**
** zTitle must be NULL if iInReplyTo>0 and must be non-empty if
** iInReplyTo==0.
**
** The caller must have started a transaction and must roll it back if
** this call returns <=0, noting that only the negative-value case is
** an error.
**
** Maintenance reminders:
**
** - iInReplyTo==0 && iEdit==0: new thread
** - iInReplyTo==0 && iEdit>0 : edit top post or response
** - iInReplyTo>0  && iEdit==0: new response
** - iInReplyTo>0  && iEdit>0 : edit response
*/
static int forum_post_ajax(
  const char *zTitle,          /* Title.  NULL for replies */
  int iInReplyTo,              /* Post replying to.  0 for new threads */
  int iEdit,                   /* Post being edited, or zero for a new post */
  const char *zUser,           /* Username.  NULL means use login name */
  const char *zMimetype,       /* Mimetype of content. */
  const char *zContent,        /* Content */
  int iFlags                   /* FPOST_xyz flag values */
){
  char *zI;
  char *zG;
  int iBasis;
  Blob x = BLOB_INITIALIZER,
    cksum = BLOB_INITIALIZER,
    formatCheck = BLOB_INITIALIZER,
    errMsg = BLOB_INITIALIZER;
  Manifest *pPost = 0;
  int nContent = zContent ? (int)strlen(zContent) : 0;
  int rc = 0;

  assert( db_transaction_nesting_depth()>0 );
  schema_forum();
  if( iEdit==0 && fossil_all_whitespace(zContent) ){
    return 0;
  }
  if( !g.perm.Admin && (iEdit || iInReplyTo)
      && forum_rid_is_tagged(iEdit ? iEdit : iInReplyTo, "closed", 1) ){
    return -ajax_route_error(400, "Thread is closed.");
  }
  if( 0==iInReplyTo && fossil_all_whitespace(zTitle) ){
    return -ajax_route_error(400, "Empty title is not permitted.");
  }

  if( zUser==0 ){
    if( login_is_nobody() ){
      zUser = "anonymous";
    }else{
      zUser = login_name();
    }
  }
  if( iEdit>0
      && !g.perm.Admin
      && !forumpost_is_owner(iEdit, zUser) ){
    return -ajax_route_error(
      403, "Only admins may edit other peoples' posts."
    );
  }
  if( iInReplyTo==0 && iEdit>0 ){
    iBasis = iEdit;
    iInReplyTo = db_int(0, "SELECT firt FROM forumpost WHERE fpid=%d",
                        iEdit);
  }else{
    iBasis = iInReplyTo;
    /* TODO (2026-06-008) If (iInReplyTo>0 && iEdit>0), validate that
    ** iInReplyTo is connected to iEdit properly, else we risk
    ** reparenting the new edit and having unrepredictable downstream
    ** side effects. */
  }
  if( 0!=zTitle && 0==zTitle[0] ) zTitle = 0;
  webpage_assert( (zTitle==0)+(iInReplyTo==0)==1 );
  blob_init(&x, 0, 0);
  blob_appendf(&x, "D %z\n", date_in_standard_format("now"));
  zG = db_text(
    0,
    "SELECT uuid FROM blob, forumpost"
    " WHERE blob.rid==forumpost.froot"
    "   AND forumpost.fpid=%d",
    iBasis
  );
  if( zG ){
    blob_appendf(&x, "G %z\n", zG);
  }
  if( zTitle ){
    blob_appendf(&x, "H %F\n", zTitle);
  }
  if( iInReplyTo>0 ){
    zI = rid_to_uuid(iInReplyTo);
    if( 0==zI ){
      rc = -ajax_route_error(404, "Missing in-reply-to artifact %d",
                             iInReplyTo);
      goto post_ajax_end;
    }
    blob_appendf(&x, "I %z\n", zI);
  }
  if( zMimetype!=0
      && zMimetype[0]!=0
      && fossil_strcmp(zMimetype,"text/x-fossil-wiki")!=0 ){
    blob_appendf(&x, "N %F\n", zMimetype);
  }
  if( iEdit>0 ){
    char *zP = rid_to_uuid(iEdit);
    if( zP==0 ){
      rc = -ajax_route_error(404, "Missing edit artifact %d", iEdit);
      goto post_ajax_end;
    }
    blob_appendf(&x, "P %z\n", zP);
  }

  blob_appendf(&x, "U %F\n", zUser);
  blob_appendf(&x, "W %d\n%s\n", nContent, zContent);
  md5sum_blob(&x, &cksum);
  blob_appendf(&x, "Z %b\n", &cksum);
  blob_reset(&cksum);

  /* Verify that the artifact we are creating is well-formed */
  blob_init(&formatCheck, 0, 0);
  blob_init(&errMsg, 0, 0);
  blob_copy(&formatCheck, &x);
  pPost = manifest_parse(&formatCheck, 0, &errMsg);
  if( pPost==0 ){
    ajax_route_error(500, "Malformed forum post artifact: %b", &errMsg);
    rc = -500;
    goto post_ajax_end;
  }
  webpage_assert( pPost->type==CFTYPE_FORUM );

  if( (iFlags & FPOST_DRYRUN)!=0 ){
    rc = 0;
  }else{
    int nrid;
    db_begin_transaction();
    nrid = wiki_put(&x, iEdit>0 ? iEdit : 0, forum_need_moderation());
    blob_reset(&x);
    if( (iFlags & FPOST_NO_ALERT)!=0 ){
      alert_unqueue('f', nrid);
    }
    rc = nrid;
    db_end_transaction(0);
  }
post_ajax_end:
  manifest_destroy(pPost);
  blob_reset(&x);
  blob_reset(&cksum);
  blob_reset(&formatCheck);
  return rc;
}
/*
** WEBPAGE: forumajax_save hidden
**
** WIP
**
** Response JSON:
**
** { uuid: hash, ...tbd }
*/
void forum_ajax_save_page(void){
  const char *zFpid;
  const char *zTitle;
  const char *zIrt;
  const char *zMimetype;
  const char *zContent;
  const char *zStatus;
  const int bHasAttachment = P("file1")!=0;
  Manifest *pPost = 0;
  char *zNewUuid = 0;
  int goodCaptcha = 1;
  int firt = 0;        /* In-reply-to rid or 0 */
  int fpid = 0;        /* Post rid being edited or 0 */
  int rc = 0;          /* Result code. */
  int nrid = 0;        /* New artifact rid. */
  int iPostFlags;      /* forum_post_flags() (after perms check) */
  int bRollback;       /* True = roll back. */

  if( !ajax_route_bootstrap(0, 1) ){
    return;
  }else if( !g.perm.WrForum
            || (bHasAttachment && !g.perm.AttachForum) ){
    ajax_route_error_forbidden();
    return;
  }else if( !ajax_check_csrf(2) ){
    ajax_route_error_csrf();
    return;
  }else if( 0==(goodCaptcha = captcha_is_correct(0)) ){
    ajax_route_error_captcha();
    return;
  }

  iPostFlags = forum_post_flags(/*must come after permissions init*/);
  bRollback = (FPOST_DRYRUN & iPostFlags);
  zFpid = P("fpid");
  zIrt = P("firt");
  zMimetype = P("mimetype");
  zContent = P("content");
  zStatus = P("status");
  db_begin_transaction();
  if( zFpid && zFpid[0] ){
    fpid = symbolic_name_to_rid(zFpid, "f");
    if( fpid<0 ){
      rc = -ajax_route_error(400, "Ambiguous forum ID.");
      goto ajax_save_end;
    }else if( 0==fpid
              || 0==(pPost = manifest_get(fpid, CFTYPE_FORUM, 0)) ){
      rc = -ajax_route_error(404, "Cannot resolve forum post ID.");
      goto ajax_save_end;
    }
  }
  /*
  ** Problem: if we derive firt from fpid/pPost then there's a race
  ** condition where the IRT post is edited between the time that this
  ** edit was initiated and when it is posted: the new edit's IRT will
  ** point to the edit which was made in the meantime, not the one the
  ** user intended to respond to. However, if we accept firt from the
  ** enviornment, we "really should" validate that it's actually in
  ** the current chain, to prohibit that malicious posts could move
  ** posts around.
  **
  ** forum_post_ajax() will, if fpid>0 && !firt, select fpid's current
  ** firt.
  */
  if( zIrt && zIrt[0] ){
    firt = symbolic_name_to_rid(zIrt, "f");
    if( firt<0 ){
      rc = -ajax_route_error(400, "Ambiguous in-reply-do ID.");
      goto ajax_save_end;
    }else if( 0==firt ){
      rc = -ajax_route_error(404, "Cannot resolve in-reply-do ID.");
      goto ajax_save_end;
    }
  }

  if( 0 ){
    rc = -ajax_route_error(400, "Save is TODO. "
                           "iPostFlags=%d debug=%d",
                           iPostFlags, g.perm.Debug);
    goto ajax_save_end;
  }

  zTitle = firt ? 0 : P("title");
  nrid = forum_post_ajax(zTitle, firt, fpid, 0, zMimetype,
                         zContent, iPostFlags);
  if( nrid<0 ){
    rc = nrid;
    goto ajax_save_end;
  }else if( nrid==0 ){
    if( 0==(FPOST_DRYRUN & iPostFlags) ){
      bRollback = 1;
      CX("{\"message\": \"No saving needed.\"}\n");
    }else{
      CX("{\"message\": \"Rolled back for dry-run.\","
         "\"iPostFlags\":%d}\n", iPostFlags);
    }
    goto ajax_save_end;
  }else{
    const int bNeedsModeration = forum_need_moderation();
    const int fpHead = forumpost_head_rid(nrid);
    assert( nrid>0 );
    assert( fpHead>0 );
    zNewUuid = rid_to_uuid(nrid);
    if( 0!=P("file1") ){
      /* Attachments */
      if( !g.perm.Admin && !g.perm.AttachForum ){
        rc = -ajax_route_error(403, "No permission no attach files.");
        goto ajax_save_end;
      }else{
        char *zRoot = (nrid==fpHead) ? 0 : rid_to_uuid(fpHead);
        const int atRc =
          attachments_ajax_from_POST(zRoot ? zRoot : zNewUuid,
                                     bNeedsModeration);
        fossil_free(zRoot);
        if( atRc<0 ){
          rc = atRc;
          goto ajax_save_end;
        }
        if( atRc>0
            && (iPostFlags & FPOST_NO_ALERT)!=0
            && db_table_exists("repository","pending_alert") ){
          /* Unqueue any alerts for these attachments. Recall that
          ** they're attached to the first version of the post, which
          ** means we actually risk cancelling _other_ pending
          ** notifications for attachments on this same post. C'est la
          ** vie.*/
          db_multi_exec(
            "WITH x(id) AS (\n"
            "  SELECT 'f%d'\n"
            "  UNION ALL\n"
            "  SELECT 'f'||a.attachid FROM blob b, attachment a\n"
            "    WHERE b.rid=%d\n"
            "    AND b.uuid=a.target\n"
            ") DELETE FROM pending_alert WHERE eventid IN x",
            fpHead, fpHead
          );
        }
      }
    }
    if( 0==bNeedsModeration
        /* ^^^ Do not allow a status tag on a pending-moderation post
        ** because it will introduce a reference to an artifact which
        ** will become a phantom if it is rejected by a moderator. */
        && zStatus!=0 && zStatus[0]!=0
        && forum_may_set_status(nrid)
        && forumpost_tag(nrid, 1, "status", zStatus)<0 ){
      rc = -ajax_route_error(500, "Tagging failed: %s", g.zErrMsg);
      goto ajax_save_end;
    }
  }

  assert( 0==rc );
  assert( zNewUuid );
  CX("{\"uuid\": %!j, \"dryrun\": %s, \"iPostFlags\":%d}\n",
     zNewUuid, bRollback ? "true" : "false", iPostFlags);

ajax_save_end:
  manifest_destroy(pPost);
  fossil_free(zNewUuid);
  db_end_transaction(rc || bRollback);
}
