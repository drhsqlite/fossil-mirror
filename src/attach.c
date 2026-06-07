/*
** Copyright (c) 2010 D. Richard Hipp
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
** This file contains code for dealing with attachments.
*/
#include "config.h"
#include "attach.h"
#include <assert.h>

/*
** Given a presumedly legal attachment target name, this guesses the
** target type and returns one of CFTYPE_FORUM, CFTYPE_WIKI,
** CFTYPE_TICKET, or CFTYPE_EVENT. Returns 0 if it cannot distinguish
** the target type.
**
** zTarget is an attachment target name: wiki page name, tech-note ID,
** ticket ID, or forumpost hash.
**
** If bFull is true then it requires zTarget to be a full ID for
** tech-notes and tickets, otherwise such IDs may be prefixes. If
** bFull is false then tech-notes and tickets will perform a prefix
** match, but it is up to the caller to provide enough of a prefix to
** rule out ambiguity[^1]. When called repeatedly, this routine can
** run a bit faster and more efficiently if bFull is true, but some
** historical use cases call for prefix matches.
**
** Wiki page names always require an exact match.
**
** Forum posts are a special case:
**
** - They ignore the bFull flag. That is, they will do prefix matches
**   but will not match an ambiguous prefix.
**
** - It is up to the caller to, if needed, resolve zTarget using
**   forumpost_head_rid2() to resolve the RID of the earliest version
**   of the post, as that is the only one which attachments should
**   target.
**
** [^1]: Historically (from the perspective of 2026-06) attachment
** target lookups have used GLOB prefix matching but have taken no
** measures to ensure that the prefix is unambiguous. Ergo we do the
** same here. It is assumed that the caller passes enough of a prefix
** to be unambiguous and that's worked out fine so far.
*/
int attachment_target_type(const char *zTarget, int bFull){
  if( !zTarget || !zTarget[0] || strlen(zTarget)>64/*vs. abuse*/ ){
    return 0;
  }
  if( symbolic_name_to_rid(zTarget, "f")>0 ){
    /* Check forum posts first because they are the most likely target
    ** as of 2026. We should arguably use something more
    ** specialized/efficient than symbolic_name_to_rid(). */
    return CFTYPE_FORUM;
  }
  if( bFull ){
    static Stmt q = empty_Stmt_m;
    int rc = 0;
    if( !q.pStmt ){
      db_static_prepare(
        &q,
        "SELECT CASE "
        /* Ordered by presumed likelihood of attachments. */
        "WHEN (SELECT 1 FROM tag WHERE tagname='tkt-'||:tgt) THEN %d\n"
        "WHEN (SELECT 1 FROM tag WHERE tagname='wiki-'||:tgt) THEN %d\n"
        "WHEN (SELECT 1 FROM tag WHERE tagname='event-'||:tgt) THEN %d\n"
        "ELSE 0 END",
        CFTYPE_TICKET, CFTYPE_WIKI, CFTYPE_EVENT
      );
    }
    db_bind_text(&q, ":tgt", zTarget);
    if( SQLITE_ROW==db_step(&q) ){
      rc = db_column_int(&q, 0);
    }
    db_reset(&q);
    return rc;
  }else{
    return db_int(
      0,
      "SELECT CASE "
      "WHEN (SELECT tagid FROM tag WHERE tagname GLOB 'tkt-%q*')"
      "  THEN %d\n"
      "WHEN (SELECT tagid FROM tag WHERE tagname='wiki-%q')"
      "  THEN %d\n"
      "WHEN (SELECT tagid FROM tag WHERE tagname GLOB 'event-%q*')"
      "  THEN %d\n"
      "ELSE 0 END",
      zTarget, CFTYPE_TICKET,
      zTarget, CFTYPE_WIKI,
      zTarget, CFTYPE_EVENT
    );
  }
}

/*
** Given an attachment target name, returns the target's blob.rid.
** zTarget and bFull work as described for attachment_target_type().
**
** For forum posts, this always returns the RID of the first version
** of the post, as attachments should always target that instance.
*/
int attachment_target_rid(const char *zTarget, int bFull){
  int rid = 0;
  const int eType = attachment_target_type(zTarget, bFull);
  switch(eType){
    case CFTYPE_TICKET:
    case CFTYPE_EVENT:{
      const char *zTagPrefix = (eType==CFTYPE_EVENT) ? "event" : "tkt";
      rid = db_int(
        0, "SELECT b.rid FROM blob b, tag t, tagxref x\n"
        "WHERE tagname %s '%s-%q%s'\n"
        "AND x.tagtype>0\n"
        "AND x.tagid=t.tagid\n"
        "AND x.rid=b.rid\n"
        "ORDER BY x.mtime DESC",
        bFull ? "=" : "GLOB"/*safe-for-%s*/,
        zTagPrefix/*safe-for-%s*/,
        zTarget,
        bFull ? "" : "*"/*safe-for-%s*/
      );
      break;
    }
    case CFTYPE_FORUM:
      rid = db_int(
        0, "SELECT f.fpid FROM forumpost f, blob b\n"
        "WHERE f.fpid=b.rid\n"
        "AND b.uuid %s '%q%s'",
        bFull ? "=" : "GLOB"/*safe-for-%s*/,
        zTarget,
        bFull ? "" : "*"/*safe-for-%s*/
      );
      if( rid>0 ){
        rid = forumpost_head_rid(rid);
      }
      break;
    case CFTYPE_WIKI:
      rid = db_int(
        0, "SELECT b.rid FROM blob b, tag t, tagxref x\n"
        "WHERE tagname='wiki-%q'\n"
        "AND x.tagtype>0\n"
        "AND x.tagid=t.tagid\n"
        "AND x.rid=b.rid\n"
        "ORDER BY x.mtime DESC",
        zTarget
      );
      break;
    default:
      break;
  }
  return rid;
}

/*
** For a given aritfact ID and type (from the CFTYPE_xyz enum),
** returns true if the current user could hypothetically apply and
** attachment to it, else returns 0.
**
** The rid is currently only relevant when eArtifactType is
** CFTYPE_FORUM.  For forum posts, it checks precisely the rid given,
** not the head RID, to keep non-admins from attaching files to
** threads which have since been taken over by another user (this
** happens when an admin edits another user's post).
*/
int attach_user_may(int rid, int eArtifactType){
  if( g.perm.Admin ) return 1;
  if( !login_is_individual() ) return 0;
  switch(eArtifactType){
    case CFTYPE_FORUM:
      return forumpost_is_owner(rid, 0);
    case CFTYPE_WIKI:
      return g.perm.ApndWiki && g.perm.Attach;
    case CFTYPE_TICKET:
      return g.perm.ApndTkt && g.perm.Attach;
    case CFTYPE_EVENT:
      return g.perm.Write && g.perm.ApndWiki && g.perm.Attach;
    default:
      return 0;
  }
}

/*
** Emits a single-button FORM which invokes
** /attachadd with target=$zTarget.
*/
void attach_render_attachadd_button(const char *zTarget){
  /* This could be changed from POST to GET, and arguably should so
  ** that the target=X part becomes part of the resulting URL. */
  @ <form method="post" action="%R/attachadd">\
  @ <input type="hidden" name="target" value="%T(zTarget)">\
  @ <input type="submit" value="Attach...">
  @ </form>\
}

/*
** WEBPAGE: attachlist
** List attachments.
**
**    tkt=HASH
**    page=WIKIPAGE
**    technote=HASH
**    forumpost=HASH
**
** At most one of technote=, tkt=, forumpost=, or page= may be supplied.
**
** If none are given, all attachments are listed.  If one is given, only
** attachments for the designated technote, ticket or wiki page are shown.
**
** HASH may be just a prefix of the relevant forum post, technical
** note, or ticket artifact hash, in which case all attachments of all
** technical notes or tickets with the prefix will be listed. Forum
** posts, on the other hand, require a unique hash or hash prefix.
*/
void attachlist_page(void){
  const char *zPage = P("page");
  const char *zTkt = P("tkt");
  const char *zTechNote = P("technote");
  const char *zForumPost = P("forumpost");
  char *zLink = 0;
  Blob sql;
  Stmt q;

  if( zPage && zTkt ) zTkt = 0;
  login_check_credentials();
  style_set_current_feature("attach");
  blob_zero(&sql);
  blob_append_sql(&sql,
     "SELECT datetime(mtime,toLocal()), src, target, filename,"
     "       comment, user,"
     "       (SELECT uuid FROM blob WHERE rid=attachid), attachid"
     "  FROM attachment"
  );
  if( zForumPost ){
    int fnid;
    if( g.perm.RdForum==0 ){ login_needed(g.anon.RdForum); return; }
    style_header("Attachments To Forum post %S", zForumPost);
    fnid = forumpost_head_rid2(zForumPost);
    if( fnid<=0 ){
      webpage_error("Invalid forum post ID: %h", zForumPost);
    }
    blob_append_sql(&sql, " WHERE target="
                    "(SELECT uuid FROM blob WHERE rid=%d)", fnid);
    zLink = mprintf("forum post <a href='%R/forumpost/%t'>%#h</a>",
                    zForumPost, hash_digits(0), zForumPost);
  }else if( zPage ){
    if( g.perm.RdWiki==0 ){ login_needed(g.anon.RdWiki); return; }
    style_header("Attachments To Wiki page %h", zPage);
    blob_append_sql(&sql, " WHERE target=%Q", zPage);
    zLink = mprintf("wiki page <a href='%R/wiki?name=%t'>%h</a>",
                    zPage, zPage);
  }else if( zTkt ){
    if( g.perm.RdTkt==0 ){ login_needed(g.anon.RdTkt); return; }
    style_header("Attachments To Ticket %S", zTkt);
    blob_append_sql(&sql, " WHERE target GLOB '%q*'", zTkt);
    zLink = mprintf("ticket <a href='%R/tktview?name=%t'>%#h</a>",
                    zTkt, hash_digits(0), zTkt);
  }else if( zTechNote ){
    if( g.perm.RdWiki==0 ){ login_needed(g.anon.RdWiki); return; }
    style_header("Attachments To Tech Note %S", zTechNote);
    blob_append_sql(&sql, " WHERE target GLOB '%q*'",
                    zTechNote);
    zLink = mprintf("tech-note <a href='%R/technote?name=%t'>%#h</a>",
                    zTechNote, hash_digits(0), zTechNote);
  }else{
    if( g.perm.RdTkt==0 && g.perm.RdWiki==0 ){
      login_needed(g.anon.RdTkt || g.anon.RdWiki);
      return;
    }
    style_header("All Attachments");
  }
  blob_append_sql(&sql, " ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_sql_text(&sql));

  if( zLink ){
    @ <h2>Attachments for %s(zLink)</h2>
    fossil_free(zLink);
    zLink = 0;
  }

  @ <ol>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate;
    const char *zSrc;
    const char *zTarget;
    const char *zFilename;
    const char *zComment;
    const char *zUser;
    const char *zUuid;
    const char *zDispUser;
    const int attachid = db_column_int(&q, 7);
    int type;
    int i;
    int bDeleted;
    char *zUrlTail = 0;

    if( moderation_pending(attachid)
        && !moderation_user_could(attachid, 1, 0) ){
      /* Elide entries which are currently pending moderation unless
      ** the user would be able to moderate the entry themselves. */
      continue;
    }

    zDate = db_column_text(&q, 0);
    zSrc = db_column_text(&q, 1);
    zTarget = db_column_text(&q, 2);
    zFilename = db_column_text(&q, 3);
    zComment = db_column_text(&q, 4);
    zUser = db_column_text(&q, 5);
    zUuid = db_column_text(&q, 6);
    zDispUser = zUser && zUser[0] ? zUser : "anonymous";
    for(i=0; zFilename[i]; i++){
      if( zFilename[i]=='/' && zFilename[i+1]!=0 ){
        zFilename = &zFilename[i+1];
        i = -1;
      }
    }
    bDeleted = 0==zSrc || 0==zSrc[0];
    type = attachment_target_type(zTarget, 1);
    switch( type ){
      case CFTYPE_TICKET:
        zUrlTail = mprintf("tkt=%s&file=%t", zTarget, zFilename);
        break;
      case CFTYPE_EVENT:
        zUrlTail = mprintf("technote=%s&file=%t", zTarget, zFilename);
        break;
      case CFTYPE_FORUM:
        zUrlTail = mprintf("forumpost=%t&file=%t", zTarget, zFilename);
        break;
      case CFTYPE_WIKI:
        zUrlTail = mprintf("page=%t&file=%t", zTarget, zFilename);
        break;
    }
    @ <li><p>
    if( bDeleted ){
      @ <s>\
    }
    @ Attachment %z(href("%R/ainfo/%!S",zUuid))%S(zUuid)</a>\
    moderation_pending_www(attachid);
    @ <br>\
    @ <a href="%R/attachview?%s(zUrlTail)">%h(zFilename)</a>
    @ [<a href="%R/attachdownload/%t(zFilename)?%s(zUrlTail)">download</a>]\
    if( bDeleted ){
      @ </s>
    }
    @ <br>
    if( zComment ) while( fossil_isspace(zComment[0]) ) zComment++;
    if( zComment && zComment[0] ){
      /* FIXME (2026-06-05): Honor the N-card (comment mimetype). %W
      ** (historically used here) assumes fossil-wiki and the
      ** fileformat.wiki doc has always claimed that it defaults to
      ** text/plain. /ainfo assumes it is plain text. */
      @ %h(zComment)<br>
    }
    if( zForumPost==0 && zPage==0 && zTkt==0 && zTechNote==0 ){
      if( bDeleted ){
        zSrc = "Deleted from";
      }else {
        zSrc = "Added to";
      }
      switch( type ){
        case CFTYPE_TICKET:
          @ %s(zSrc) ticket <a href="%R/tktview?name=%s(zTarget)">
          @ %S(zTarget)</a>
          break;
        case CFTYPE_EVENT:
          @ %s(zSrc) tech note <a href="%R/technote/%s(zTarget)">
          @ %S(zTarget)</a>
          break;
        case CFTYPE_WIKI:
          @ %s(zSrc) wiki page <a href="%R/wiki?name=%t(zTarget)">
          @ %h(zTarget)</a>
          break;
        case CFTYPE_FORUM:
          @ %s(zSrc) forum post <a href="%R/forumpost/%s(zTarget)">
          @ %h(zTarget)</a>
          break;
        default:
          @ <span class='error'>%s(zSrc) cannot determine target type
          @ of %h(zTarget)</span>
        break;
      }
    }else{
      if( zSrc==0 || zSrc[0]==0 ){
        @ Deleted
      }else {
        @ Added
      }
    }
    @ by %h(zDispUser) on
    hyperlink_to_date(zDate, ".");
    free(zUrlTail);
  }
  db_finalize(&q);
  @ </ol>
  style_finish_page();
  return;
}

/*
** WEBPAGE: attachdownload
** WEBPAGE: attachimage
** WEBPAGE: attachview
**
** Download or display an attachment.
**
** Query parameters:
**
**    tkt=HASH
**    page=WIKIPAGE
**    technote=HASH
**    forumpost=HASH
**    file=FILENAME
**    attachid=ID
**
*/
void attachview_page(void){
  const char *zPage = P("page");
  const char *zTkt = P("tkt");
  const char *zTechNote = P("technote");
  const char *zForumPost = P("forumpost");
  const char *zFile = P("file");
  const char *zTarget = 0;
  int attachid = atoi(PD("attachid","0"));
  char *zUUID = 0;

  if( zFile==0 ) fossil_redirect_home();
  login_check_credentials();
  style_set_current_feature("attach");
  if( zForumPost ){
    int fnid;
    if( g.perm.RdForum==0 ){ login_needed(g.anon.RdForum); return; }
    /* Forum attachments are always tied to the post's initial version */
    fnid = forumpost_head_rid2(zForumPost);
    if( fnid>0 ) zTarget = rid_to_uuid(fnid);
  }else if( zPage ){
    if( g.perm.RdWiki==0 ){ login_needed(g.anon.RdWiki); return; }
    zTarget = zPage;
  }else if( zTkt ){
    if( g.perm.RdTkt==0 ){ login_needed(g.anon.RdTkt); return; }
    zTarget = zTkt;
  }else if( zTechNote ){
    if( g.perm.RdWiki==0 ){ login_needed(g.anon.RdWiki); return; }
    zTarget = zTechNote;
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
    style_finish_page();
    return;
  }else if( zUUID[0]=='x' ){
    style_header("Missing");
    @ Attachment has been deleted
    style_finish_page();
    return;
  }else{
    g.perm.Read = 1;
    cgi_replace_parameter("name",zUUID);
    if( fossil_strcmp(g.zPath,"attachview")==0 ){
      artifact_page();
    }else{
      cgi_replace_parameter("m", mimetype_from_name(zFile));
      rawartifact_page();
    }
  }
}

/*
** Save an attachment control artifact into the repository
*/
static void attach_put(
  Blob *pAttach,     /* Text of the Attachment record */
  int attachRid,     /* RID for the file that is being attached */
  int needMod        /* True if the attachment is subject to moderation */
){
  int rid;
  if( needMod ){
    rid = content_put_ex(pAttach, 0, 0, 0, 1);
    moderation_table_create();
    db_multi_exec(
      "INSERT INTO modreq(objid,attachRid) VALUES(%d,%d);",
      rid, attachRid
    );
  }else{
    rid = content_put(pAttach);
    db_add_unsent(rid);
    db_multi_exec("INSERT OR IGNORE INTO unclustered VALUES(%d);", rid);
  }
  manifest_crosslink(rid, pAttach, MC_NONE);
}


/*
** Commit a new attachment into the repository
*/
void attach_commit(
  const char *zName,                   /* The filename of the attachment */
  const char *zTarget,                 /* The artifact hash to attach to */
  const char *aContent,                /* The content of the attachment */
  int         szContent,               /* The length of the attachment */
  int         needModerator,           /* Moderate the attachment? */
  const char *zComment                 /* The comment for the attachment */
){
    Blob content;
    Blob manifest;
    Blob cksum;
    char *zUUID;
    char *zDate;
    int rid;
    int i, n;
    int addCompress = 0;
    Manifest *pManifest;

    db_begin_transaction();
    blob_init(&content, aContent, szContent);
    pManifest = manifest_parse(&content, 0, 0);
    manifest_destroy(pManifest);
    blob_init(&content, aContent, szContent);
    if( pManifest ){
      blob_compress(&content, &content);
      addCompress = 1;
    }
    rid = content_put_ex(&content, 0, 0, 0, needModerator);
    zUUID = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    blob_zero(&manifest);
    for(i=n=0; zName[i]; i++){
      if( zName[i]=='/' || zName[i]=='\\' ) n = i+1;
    }
    zName += n;
    if( zName[0]==0 ) zName = "unknown";
    blob_appendf(&manifest, "A %F%s %F %s\n",
                 zName, addCompress ? ".gz" : "", zTarget, zUUID);
    if( zComment!=0 && zComment[0]!=0 ){
      while( fossil_isspace(zComment[0]) ) zComment++;
      n = strlen(zComment);
      while( n>0 && fossil_isspace(zComment[n-1]) ){ n--; }
      if( n>0 ){
        blob_appendf(&manifest, "C %#F\n", n, zComment);
      }
    }
    zDate = date_in_standard_format("now");
    blob_appendf(&manifest, "D %z\n", zDate);
    blob_appendf(&manifest, "U %F\n", login_name());
    md5sum_blob(&manifest, &cksum);
    blob_appendf(&manifest, "Z %b\n", &cksum);
    attach_put(&manifest, rid, needModerator);
    assert( blob_is_reset(&manifest) );
    db_end_transaction(0);
}

/*
** Renders the "legacy" (static) /attachadd form. One of the first
** four arguments must be non-NULL and the other three must be NULL.
** zComment may be NULL, as may zFrom. See the call sites for more
** context.
*/
static void attach_render_legacy_form(const char *zForumPost,
                                      const char *zTechNote,
                                      const char *zTicket,
                                      const char *zWikiPage,
                                      const char *zComment,
                                      const char *zFrom){
  form_begin("enctype='multipart/form-data'", "%R/attachadd");
  @ <div>\
  @ File to Attach:
  @ <input type="file" name="f" size="60"><br>
  @ Description:<br>
  @ <textarea name="comment" cols="80" rows="5" wrap="virtual"\
  @ >%h(zComment)</textarea><br>
  if( zForumPost ){
    @ <input type="hidden" name="forumpost" value="%h(zForumPost)">\
  }else if( zTicket ){
    @ <input type="hidden" name="tkt" value="%h(zTicket)">\
  }else if( zTechNote ){
    @ <input type="hidden" name="technote" value="%h(zTechNote)">\
  }else if( zWikiPage ){
    @ <input type="hidden" name="page" value="%h(zWikiPage)">\
  }
  @ <input type="hidden" name="from" value="%h(zFrom)">\
  @ <input type="submit" name="ok" value="Add Attachment">\
  @ <input type="submit" name="cancel" value="Cancel">\
  @ </div>
  captcha_generate(0);
  login_insert_csrf_secret();
  @ </form>
}

/*
** WEBPAGE: attachadd
** Add a new attachment.
**
**    tkt=HASH
**    page=WIKIPAGE
**    technote=HASH
**    forumpost=HASH
**    from=URL
**
** Adds a POSTed file attachment to the given target.
**
** Or the "version 2" interface:
**
**    target=ATTACHMENT_TARGET
**
** Behaves as documented for attachaddV2_page().
*/
void attachadd_page(void){
  const char *zPage;
  const char *zForumPost;
  const char *zTkt;
  const char *zTechNote;
  const char *aContent;
  const char *zName;
  const char *zComment;
  const char *zTarget;
  const char *zFrom;       /* Origin page - redirect here after saving */
  char *zTo = 0;           /* Optionally redirect here after saving */
  char *zTargetType = 0;
  char *zExtraFree = 0;
  int szContent;
  int goodCaptcha = 1;
  int szLimit = 0;

  if( P("target")!=0 ){
    attachaddV2_page();
    return;
  }
  zPage = P("page");
  zForumPost = P("forumpost");
  zTkt = P("tkt");
  zTechNote = P("technote");
  zFrom = P("from");
  aContent = P("f");
  zName = PD("f:filename","unknown");
  zComment = PD("comment", "");
  szContent = atoi(PD("f:bytes","0"));

  if( zFrom==0 ) zFrom = mprintf("%R/home");
  if( P("cancel") ) cgi_redirect(zFrom);
  if( (!!zPage + !!zTkt + !!zTechNote + !!zForumPost)!=1 ){
    webpage_error("Requires exactly one one: page=X, tkt=X, forumpost=X,"
                  " technote=X, or target=X");
  }
  login_check_credentials();
  if( zForumPost ){
    int fpid;
    if( g.perm.AttachForum==0 ){
      login_needed(g.anon.AttachForum);
      return;
    }
    fpid = forumpost_head_rid2(zForumPost);
    if( fpid<=0 ){
      webpage_error("Invalid forum post ID: %h", zForumPost);
    }else if( !g.perm.Admin && !forumpost_is_owner(fpid, 0) ){
      webpage_error("Only admins can attach files to other users' "
                   "forum posts.");
    }
    zTarget = zExtraFree = rid_to_uuid(fpid);
    zTargetType = mprintf("Forum post <a href=\"%R/forumpost/%S\">%h</a>",
                          zTarget, zForumPost);
    zTo = zFrom ? 0 : mprintf("%R/forumpost/%S", zTarget);
  }else if( zPage ){
    if( g.perm.ApndWiki==0 || g.perm.Attach==0 ){
      login_needed(g.anon.ApndWiki && g.anon.Attach);
      return;
    }
    if( !db_exists("SELECT 1 FROM tag WHERE tagname='wiki-%q'", zPage) ){
      fossil_redirect_home();
    }
    zTarget = zPage;
    zTargetType = mprintf("Wiki Page <a href=\"%R/wiki?name=%t\">%h</a>",
                           zPage, zPage);
    zTo = zFrom ? 0 : mprintf("%R/wiki?name=%T", zTarget);
  }else if ( zTechNote ){
    if( g.perm.Write==0 || g.perm.ApndWiki==0 || g.perm.Attach==0 ){
      login_needed(g.anon.Write && g.anon.ApndWiki && g.anon.Attach);
      return;
    }
    if( !db_exists("SELECT 1 FROM tag WHERE tagname='event-%q'", zTechNote) ){
      zTechNote = db_text(0, "SELECT substr(tagname,7) FROM tag"
                             " WHERE tagname GLOB 'event-%q*'", zTechNote);
      if( zTechNote==0) fossil_redirect_home();
    }
    zTarget = zTechNote;
    zTargetType = mprintf("Tech Note <a href=\"%R/technote/%s\">%S</a>",
                           zTechNote, zTechNote);
    zTo = zFrom ? 0 : mprintf("%R/technote/%S", zTarget);
  }else{
    assert( zTkt );
    if( g.perm.ApndTkt==0 || g.perm.Attach==0 ){
      login_needed(g.anon.ApndTkt && g.anon.Attach);
      return;
    }
    if( !db_exists("SELECT 1 FROM tag WHERE tagname='tkt-%q'", zTkt) ){
      zTkt = db_text(0, "SELECT substr(tagname,5) FROM tag"
                        " WHERE tagname GLOB 'tkt-%q*'", zTkt);
      if( zTkt==0 ) fossil_redirect_home();
    }
    zTarget = zTkt;
    zTargetType = mprintf("Ticket <a href=\"%R/tktview/%S\">%S</a>",
                          zTkt, zTkt);
    zTo = zFrom ? 0 : mprintf("%R/tktview/%S", zTarget);
  }
  szLimit = db_get_int("attachment-size-limit", 0);
  if( szContent<0 || (szLimit && szContent>szLimit) ){
    /* This check must be done late so that zTargetType is set up. */
    @ <p class="generalError">Attachment %h(zName) is too large.
    @ <a href="%R/help/attachment-size-limit">Limit</a> is
    @ %d(szLimit ? szLimit : 0x7fffffff) bytes</p>
    /* Fall through and render form. */
   }else if( P("ok")
             && cgi_csrf_safe(2)
             && szContent>0
             && (goodCaptcha = captcha_is_correct(0)) ){
    int needModerator = (zForumPost!=0 && forum_need_moderation()) ||
                        (zTkt!=0 && ticket_need_moderation(0)) ||
                        (zPage!=0 && wiki_need_moderation(0));
    attach_commit(zName, zTarget, aContent, szContent, needModerator, zComment);
    cgi_redirect(zTo ? zTo : zFrom);
  }

  style_set_current_feature("attach");
  style_header("Add Attachment");
  if( !goodCaptcha ){
    @ <p class="generalError">Error: Incorrect security code.</p>
  }
  @ <h2>Add Attachment To %s(zTargetType)</h2>
  attach_render_legacy_form(zForumPost, zTechNote, zTechNote, zPage,
                            zComment, zFrom);
  builtin_fossil_js_bundle_or("attach", NULL);
  style_finish_page();
  fossil_free(zTargetType);
  fossil_free(zExtraFree);
}

/*
** WEBPAGE: attachadd_ajax_post   hidden
**
** Used by attachadd V2 to handle attachments via POST requests with:
**
**     target=ATTACHMENT_TARGET
**     file1..fileN=FILE_OBJECTS
**     dryrun=0|1
**
**  Each posted file in the set file1..fileN gets attached to the
**  given target, permissions permitting. If dryrun>0 then the change
**  is rolled back instead of committed. target=X must refer to a full
**  target ID, not a prefix.
**
**  Responds with JSON: an empty object on success and
**  {error:"message"} on error. The on-success response structure is
**  subject to amendment.
*/
void attachadd_ajax_post(void){
  const char *zTarget;
  char *zExtraFree = 0;
  int eTgtType = 0;
  int bNeedsModeration = 0;
  int i;
  int goodCaptcha = 1;
  int szLimit;                 /* attachment-max-size setting */
  int bRollback = 0;           /* Roll back if true. */
  char aKeyPrefix[20];         /* Buffer for key "file%d" */
  char aKeySize[30];           /* Buffer for key "file%d:bytes" */
  char aKeyName[30];           /* Buffer for key "file%d:filename" */
  char aKeyDesc[30];           /* Buffer for key "file%d_desc" */

  if( ! ajax_route_bootstrap(0, 1) ){
    return;
  }else if( !(goodCaptcha = captcha_is_correct(0)) ){
    goto ajax_post_403;
  }else if( !cgi_csrf_safe(2) ){
    ajax_route_error(403, "Invalid CSRF signature.");
    return;
  }
  db_begin_transaction();
  zTarget = P("target");
  eTgtType = attachment_target_type(zTarget, 1);
  CX("{");
  switch( eTgtType ){
    default:
    case 0:
      ajax_route_error(400, "Invalid attachment target.");
      db_rollback_transaction();
      return;
    case CFTYPE_FORUM:{
      int fpid;
      if( g.perm.AttachForum==0 ){
        goto ajax_post_403;
      }
      fpid = forumpost_head_rid2(zTarget);
      if( fpid<=0 ){
        goto ajax_post_404;
      }else if( !g.perm.Admin && !forumpost_is_owner(fpid, 0) ){
        ajax_route_error(403, "Only admins can attach files to "
                         "other users' forum posts.");
        db_rollback_transaction();
        return;
      }
      zTarget = zExtraFree = rid_to_uuid(fpid);
      bNeedsModeration = forum_need_moderation();
      break;
    }
    case CFTYPE_EVENT:{
      if( g.perm.Write==0 || g.perm.ApndWiki==0 || g.perm.Attach==0 ){
        goto ajax_post_403;
      }
      if( !db_exists("SELECT 1 FROM tag WHERE tagname='event-%q'",
                     zTarget) ){
        zTarget = zExtraFree =
          db_text(0, "SELECT substr(tagname,7) FROM tag"
                  " WHERE tagname GLOB 'event-%q*'", zTarget);
        if( zTarget==0){
          goto ajax_post_404;
        }
      }
      bNeedsModeration = 0;
      break;
    }
    case CFTYPE_TICKET:{
      if( g.perm.ApndTkt==0 || g.perm.Attach==0 ){
        goto ajax_post_403;
      }
      if( !db_exists("SELECT 1 FROM tag WHERE tagname='tkt-%q'",
                     zTarget) ){
        zTarget = db_text(0, "SELECT substr(tagname,5) FROM tag"
                       " WHERE tagname GLOB 'tkt-%q*'", zTarget);
        if( zTarget==0 ){
          goto ajax_post_404;
        }
      }
      bNeedsModeration = ticket_need_moderation(0);
      break;
    }
    case CFTYPE_WIKI:{
      if( g.perm.ApndWiki==0 || g.perm.Attach==0 ){
        goto ajax_post_403;
      }
      if( !db_exists("SELECT 1 FROM tag WHERE tagname='wiki-%q'",
                     zTarget) ){
        goto ajax_post_404;
      }
      bNeedsModeration = wiki_need_moderation(0);
      break;
    }
  }

  szLimit = db_get_int("attachment-size-limit", 0);
  for(i = 1; !bRollback; ++i){
    /* Look for P("fileN"), where N=1..n */
    const char *zContent;
    int szContent;
    sqlite3_snprintf(sizeof(aKeyPrefix), aKeyPrefix, "file%d", i);
    zContent = P(aKeyPrefix);
    if( !zContent ){
      /* End of the list. */
      break;
    }
    sqlite3_snprintf(sizeof(aKeySize), aKeySize, "%s:bytes",
                     aKeyPrefix);
    szContent = atoi(PD(aKeySize,"-1"));
    if( szContent<=0 ){
      bRollback = 1;
      ajax_route_error(400,"Invalid file size: %d", szContent);
      break;
    }else if( szLimit>0 && szContent>szLimit ){
      bRollback = 1;
      ajax_route_error(400, "File size limit is %d bytes.", szLimit);
      break;
    }else{
      sqlite3_snprintf(sizeof(aKeyName), aKeyName, "%s:filename",
                       aKeyPrefix);
      sqlite3_snprintf(sizeof(aKeyDesc), aKeyDesc, "%s_desc",
                       aKeyPrefix);
      attach_commit(P(aKeyName), zTarget, zContent, szContent,
                    bNeedsModeration, P(aKeyDesc));
    }
  }
  fossil_free(zExtraFree);
  if( !bRollback ){
    CX("}");
    if( atoi(PD("dryrun","0"))>0 ){
      bRollback = 1;
    }
  }
  db_end_transaction(bRollback);
  return;
ajax_post_403:
  if( db_transaction_nesting_depth()>0 ){
    db_rollback_transaction();
  }
  ajax_route_error(403, "Permission denied.");
  return;
ajax_post_404:
  if( db_transaction_nesting_depth()>0 ){
    db_rollback_transaction();
  }
  ajax_route_error(404, "Target not found.");
  return;
}

/*
** Proxy for /attachadd?target=X
**
** Lists attachments for, and can add them to, a target artifact.
**
**    target=TKT_HASH|WIKIPAGE_NAME|TECHNOTE_HASH|FORUMPOST_HASH
**    from=ORIGINATING_URL
**
**  Works like /attachadd but uses a JS-based interactive attachment
**  selector.
**
**  from=X tells it where to redirect to when it's done.
**
** This page requires a post-2018-ish JS-capable browser.
*/
void attachaddV2_page(void){
  const char *zFrom = P("from");
  const char *zTarget = P("target");
  char *zTo = 0;
  char *zTargetType = 0;
  char *zExtraFree = 0;
  int eTgtType = 0;
  int goodCaptcha = 1;
  char const * noJsArgs[] = {0,0,0,0}; /* Args for noscript form */

  if( P("cancel") ) cgi_redirect(zFrom);
  if( 0==zTarget ){
    webpage_error("Requires target=X");
  }
  login_check_credentials();
  eTgtType = attachment_target_type(zTarget, 1);
  switch( eTgtType ){
    default:
    case 0:
      webpage_error("Cannot resolve target=%h.", zTarget);
      break;
    case CFTYPE_FORUM:{
      int fpid;
      if( g.perm.AttachForum==0 ){
        login_needed(g.anon.AttachForum);
        return;
      }
      fpid = forumpost_head_rid2(zTarget);
      if( fpid<=0 ){
        webpage_error("Invalid forum post ID: %h", zTarget);
      }else if( !g.perm.Admin && !forumpost_is_owner(fpid, 0) ){
        webpage_error("Only admins can attach files to other users' "
                      "forum posts.");
      }
      zTarget = zExtraFree = rid_to_uuid(fpid);
      noJsArgs[0] = zTarget;
      zTargetType = mprintf(
        "Forum post <a href=\"%R/forumpost/%S\">%.16h</a>",
        zTarget, zTarget
      );
      zTo = mprintf("%R/forumpost/%S", zTarget);
      break;
    }
    case CFTYPE_EVENT:{
      if( g.perm.Write==0 || g.perm.ApndWiki==0 || g.perm.Attach==0 ){
        login_needed(g.anon.Write && g.anon.ApndWiki && g.anon.Attach);
        return;
      }
      if( !db_exists("SELECT 1 FROM tag WHERE tagname='event-%q'",
                     zTarget) ){
        zTarget = db_text(0, "SELECT substr(tagname,7) FROM tag"
                            " WHERE tagname GLOB 'event-%q*'",
                          zTarget);
        if( zTarget==0) fossil_redirect_home();
      }
      zTo = zFrom ? 0 : mprintf("%R/technote?name=%T", zTarget);
      zTargetType = mprintf("Tech-note <a href=\"%R/technote/%s\">%S</a>",
                            zTarget, zTarget);
      noJsArgs[1] = zTarget;
      break;
    }
    case CFTYPE_TICKET:{
      if( g.perm.ApndTkt==0 || g.perm.Attach==0 ){
        login_needed(g.anon.ApndTkt && g.anon.Attach);
        return;
      }
      if( !db_exists("SELECT 1 FROM tag WHERE tagname='tkt-%q'",
                     zTarget) ){
        zTarget = db_text(0, "SELECT substr(tagname,5) FROM tag"
                       " WHERE tagname GLOB 'tkt-%q*'", zTarget);
        if( zTarget==0 ) fossil_redirect_home();
      }
      zTo = zFrom ? 0 : mprintf("%R/tktview/%t", zTarget);
      zTargetType = mprintf("Ticket <a href=\"%R/tktview/%s\">%S</a>",
                            zTarget, zTarget);
      noJsArgs[2] = zTarget;
      break;
    }
    case CFTYPE_WIKI:{
      if( g.perm.ApndWiki==0 || g.perm.Attach==0 ){
        login_needed(g.anon.ApndWiki && g.anon.Attach);
        return;
      }
      if( !db_exists("SELECT 1 FROM tag WHERE tagname='wiki-%q'",
                     zTarget) ){
        fossil_redirect_home();
      }
      zTo = zFrom ? 0 : mprintf("%R/wiki?name=%T", zTarget);
      zTargetType = mprintf(
        "Wiki page <a href=\"%R/wiki?name=%h\">%h</a>",
        zTarget, zTarget
      );
      noJsArgs[3] = zTarget;
      break;
    }
  }

  db_begin_transaction();

  style_set_current_feature("attach");
  style_header("Add Attachment");
  if( !goodCaptcha ){
    @ <p class="generalError">Error: Incorrect security code.</p>
  }
  @ <h2>Attachments for %s(zTargetType)</h2>
  if(1){
    /* noscript fallback is completely untested */
    @ <noscript>
    attach_render_legacy_form(noJsArgs[0], noJsArgs[1], noJsArgs[2],
                              noJsArgs[3], 0,
                              zFrom ? zFrom : mprintf("%R/home"));
    @ </noscript>
  }
  attachment_list(zTarget, NULL,
                  ATTACHLIST_SIZE | ATTACHLIST_HIDE_UNAPPROVED);
  @ <div id='attachadd-form-wrapper' class='hidden'>
  /* fossil.attach.js populates this DIV with the attachment widget
  ** and imports these hidden fields. */
  @ <input type="hidden" name="target" value="%h(zTarget)">
  if( zFrom ){
    @ <input type="hidden" name="from" value="%h(zFrom)">
  }
  if( zTo ){
    @ <input type="hidden" name="to" value="%h(zTo)">
  }
  captcha_generate(0);
  login_insert_csrf_secret();
  @ </div>
  builtin_fossil_js_bundle_or("attach", NULL);
  db_end_transaction(0);
  style_finish_page();
  fossil_free(zTargetType);
  fossil_free(zExtraFree);
}

/*
** WEBPAGE: ainfo
** URL: /ainfo?name=ARTIFACTID
**
**    name=ATTACHMENT_ARTIFACT_UUID
**
** Show the details of an attachment artifact.
*/
void ainfo_page(void){
  int rid;                       /* RID for the control artifact */
  int ridSrc;                    /* RID for the attached file */
  char *zDate;                   /* Date attached */
  const char *zUuid;             /* Hash of the control artifact */
  Manifest *pAttach;             /* Parse of the control artifact */
  const char *zTarget;           /* Wiki, ticket or tech note attached to */
  const char *zSrc;              /* Hash of the attached file */
  const char *zName;             /* Name of the attached file */
  const char *zDesc;             /* Description of the attached file */
  const char *zWikiName = 0;     /* Wiki page name when attached to Wiki */
  const char *zTNUuid = 0;       /* Tech Note ID when attached to tech note */
  const char *zTktUuid = 0;      /* Ticket ID when attached to a ticket */
  const char *zForumPost = 0;    /* Forum UID when attached to forum post */
  int modPending;                /* True if awaiting moderation */
  const char *zModAction;        /* Moderation action or NULL */
  int isModerator;               /* TRUE if user is the moderator */
  const char *zMime;             /* MIME Type */
  Blob attach;                   /* Content of the attachment */
  int fShowContent = 0;          /* True to emit the content */
  int bUserIsOwner = 0;          /* True if pAttach->zUser is login_name() */
  int showDelMenu = 0;           /* True to enable delete option */
  const char *zLn = P("ln");

  login_check_credentials();
  if( !g.perm.RdTkt && !g.perm.RdWiki ){
    login_needed(g.anon.RdTkt || g.anon.RdWiki);
    return;
  }
  rid = name_to_rid_www("name");
  if( rid==0 ){ fossil_redirect_home(); }
  zUuid = rid_to_uuid(rid);
  pAttach = manifest_get(rid, CFTYPE_ATTACHMENT, 0);
  if( pAttach==0 ) fossil_redirect_home();
  bUserIsOwner =
    0==fossil_strcmp(pAttach->zUser, login_name())
    && login_is_individual();
  zTarget = pAttach->zAttachTarget;
  zSrc = pAttach->zAttachSrc;
  ridSrc = db_int(0,"SELECT rid FROM blob WHERE uuid='%q'", zSrc);
  zName = pAttach->zAttachName;
  zDesc = pAttach->zComment;
  zMime = mimetype_from_name(zName);
  fShowContent = zMime ? strncmp(zMime,"text/", 5)==0 : 0;
  if( db_int(0,"SELECT 1 FROM event WHERE objid=%d and type='f'", rid) ){
    if( !g.perm.RdForum ){ login_needed(g.anon.RdForum); return; }
    showDelMenu = g.perm.Admin || bUserIsOwner;
    zForumPost = zTarget;
  }else if( validate16(zTarget, strlen(zTarget))
   && db_exists("SELECT 1 FROM ticket WHERE tkt_uuid='%q'", zTarget)
  ){
    if( !g.perm.RdTkt ){ login_needed(g.anon.RdTkt); return; }
    zTktUuid = zTarget;
    showDelMenu = g.perm.WrTkt;
  }else if( db_exists("SELECT 1 FROM tag WHERE tagname='wiki-%q'",
                      zTarget) ){
    if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
    zWikiName = zTarget;
    showDelMenu = g.perm.WrWiki;
  }else if( db_exists("SELECT 1 FROM tag WHERE tagname='event-%q'",
                      zTarget) ){
    if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
    zTNUuid = zTarget;
    showDelMenu = g.perm.Write && g.perm.WrWiki;
  }
  if( showDelMenu ){
    style_submenu_element("Delete", "%R/ainfo/%s?del", zUuid);
  }
  zDate = db_text(0, "SELECT datetime(%.12f)", pAttach->rDate);

  if( P("confirm")
   && cgi_csrf_safe(2)
   && ((zForumPost
        && ((bUserIsOwner && g.perm.AttachForum) ||
            forumpost_may_close())) ||
       (zTktUuid && g.perm.WrTkt) ||
       (zWikiName && g.perm.WrWiki) ||
       (zTNUuid && g.perm.Write && g.perm.WrWiki))
  ){
    /* Delete attachment. */
    int i, n, rid;
    char *zNewDate;
    Blob manifest;
    Blob cksum;
    const char *zFile = zName;

    if( !bUserIsOwner ){
      if( zForumPost ? !forumpost_may_close() : !g.perm.Admin ){
        webpage_error(
          "Only admins can delete other users' attachments."
        );
      }
    }
    db_begin_transaction();
    blob_zero(&manifest);
    for(i=n=0; zFile[i]; i++){
      if( zFile[i]=='/' || zFile[i]=='\\' ) n = i;
    }
    zFile += n;
    if( zFile[0]==0 ) zFile = "unknown";
    blob_appendf(&manifest, "A %F %F\n", zFile, zTarget);
    zNewDate = date_in_standard_format("now");
    blob_appendf(&manifest, "D %s\n", zNewDate);
    blob_appendf(&manifest, "U %F\n", login_name());
    md5sum_blob(&manifest, &cksum);
    blob_appendf(&manifest, "Z %b\n", &cksum);
    rid = content_put(&manifest);
    manifest_crosslink(rid, &manifest, MC_NONE);
    db_end_transaction(0);
    @ <p>The attachment below has been deleted.</p>
    fossil_free(zNewDate);
  }

  if( P("del")
      && ((zForumPost && (bUserIsOwner || forumpost_may_close()))
          || (zTktUuid && g.perm.WrTkt)
          || (zWikiName && g.perm.WrWiki)
          || (zTNUuid && g.perm.Write && g.perm.WrWiki))
  ){
    form_begin(0, "%R/ainfo/%!S", zUuid);
    @ <p>Confirm you want to delete the attachment shown below.
    @ <input type="submit" name="confirm" value="Confirm">
    login_insert_csrf_secret();
    @ </form>
  }

  isModerator = g.perm.Admin ||
    (zForumPost && g.perm.ModForum) ||
    (zTktUuid && g.perm.ModTkt) ||
    (zWikiName && g.perm.ModWiki);
  zModAction = P("modaction");
  if( zModAction!=0 && cgi_csrf_safe(2) ){
    if( strcmp(zModAction,"delete")==0 ){
      if( isModerator || bUserIsOwner ){
        moderation_disapprove(rid);
      }
      if( zForumPost ){
        cgi_redirectf("%R/forumpost/%!S", zForumPost);
      }else if( zTktUuid ){
        cgi_redirectf("%R/tktview/%!S", zTktUuid);
      }else if( zWikiName ) {
        cgi_redirectf("%R/wiki?name=%t", zWikiName);
      }
      /* zTNUuid is intentionally unhandled. Tech note attachments
      ** don't go through moderation. */
      return;
    }
    if( isModerator && strcmp(zModAction,"approve")==0 ){
      moderation_approve('a', rid);
    }
  }
  style_set_current_feature("attach");
  style_header("Attachment Details");
  style_submenu_element("Raw", "%R/artifact/%s", zUuid);
  if(fShowContent){
    style_submenu_element("Line Numbers", "%R/ainfo/%s%s", zUuid,
                          ((zLn&&*zLn) ? "" : "?ln=0"));
  }

  @ <div class="section">Overview</div>
  @ <p><table class="label-value">
  @ <tr><th>Artifact&nbsp;ID:</th>
  @ <td>%z(href("%R/artifact/%!S",zUuid))%s(zUuid)</a>
  if( g.perm.Setup ){
    @ (%d(rid))
  }
  modPending = moderation_pending_www(rid);
  if( zForumPost ){
    @ <tr><th>Forum&nbsp;Post:</th>
    @ <td>%z(href("%R/forumpost/%s",zForumPost))%h(zForumPost)</a>\
    @ </td></tr>
  }else if( zTktUuid ){
    @ <tr><th>Ticket:</th>
    @ <td>%z(href("%R/tktview/%s",zTktUuid))%s(zTktUuid)</a></td></tr>
  }else if( zTNUuid ){
    @ <tr><th>Tech Note:</th>
    @ <td>%z(href("%R/technote/%s",zTNUuid))%s(zTNUuid)</a></td></tr>
  }else if( zWikiName ){
    @ <tr><th>Wiki&nbsp;Page:</th>
    @ <td>%z(href("%R/wiki?name=%t",zWikiName))%h(zWikiName)</a>\
    @ </td></tr>
  }
  @ <tr><th>Date:</th><td>
  hyperlink_to_date(zDate, "</td></tr>");
  @ <tr><th>User:</th><td>
  hyperlink_to_user(pAttach->zUser, zDate, "</td></tr>");
  @ <tr><th>Artifact&nbsp;Attached:</th>
  @ <td>%z(href("%R/artifact/%s",zSrc))%s(zSrc)</a>
  if( g.perm.Setup ){
    @ (%d(ridSrc))
  }
  @ <tr><th>Filename:</th><td>%h(zName)</td></tr>
  if( g.perm.Setup ){
    @ <tr><th>MIME-Type:</th><td>%h(zMime)</td></tr>
  }
  @ <tr><th valign="top">Description:</th>\
  /* FIXME (2026-06-05): Honor the N-card (comment mimetype). */
  @ <td valign="top">%h(zDesc)</td></tr>
  @ </table>

  if( modPending && (isModerator || bUserIsOwner) ){
    @ <div class="section">Moderation</div>
    @ <blockquote>
    form_begin(0, "%R/ainfo/%s", zUuid);
    @ <label><input type="radio" name="modaction" value="delete">
    @ Delete this attachment</label><br>
    if( isModerator ){
#if 0
      /* TODO/FIXME (2026-06-03): only allow approval of an attachment
      ** if its target has been approved. Without this, we can end up
      ** with stale attachments which refer to rejected targets. We
      ** need a type-specific RID/UUID here, which requires
      ** refactoring above to get it. */
      const int tgtid = 0;
      if( moderation_pending(tgtid) ){
        @ <label><input type="radio" name="modaction" \
        @ disabled value="approve">
        @ <span class='modpending'>Cannot approve:
        @ target is pending moderation</span>\
        @ </label><br>
      }else
#else
     {
        @ <label><input type="radio" name="modaction" value="approve">
        @ Approve this attachment</label><br>
      }
#endif
    }
    @ <input type="submit" value="Submit">
    login_insert_csrf_secret();
    @ </form>
    @ </blockquote>
  }

  @ <div class="section">Content:</div>
  blob_zero(&attach);
  if( modPending && !moderation_user_could(rid, 1, 0) ){
    @ <p><span class="modpending">Content is awaiting moderator \
    @ approval.</span></p>
  }else{
    @ <blockquote>
    if( fShowContent ){
      const char *z;
      content_get(ridSrc, &attach);
      blob_to_utf8_no_bom(&attach, 0);
      z = blob_str(&attach);
      if( zLn ){
        output_text_with_line_numbers(z, blob_size(&attach),
                                      zName, zLn, 1);
      }else{
        @ <pre>
        @ %h(z)
        @ </pre>
      }
    }else if( strncmp(zMime, "image/", 6)==0 ){
      int sz = db_int(0, "SELECT size FROM blob WHERE rid=%d", ridSrc);
      @ <i>(file is %d(sz) bytes of image data)</i><br>
      @ <img src="%R/raw/%s(zSrc)?m=%s(zMime)"></img>
      style_submenu_element("Image", "%R/raw/%s?m=%s", zSrc, zMime);
    }else{
      int sz = db_int(0, "SELECT size FROM blob WHERE rid=%d", ridSrc);
      @ <i>(file is %d(sz) bytes of binary data)</i>
    }
    @ </blockquote>
 }
  manifest_destroy(pAttach);
  blob_reset(&attach);
  style_finish_page();
}

#if INTERFACE
/*
** Flags for use with attachment_list(). ATTACHLIST_HRULE_ABOVE
** must have a value of 1 for historical call compatibility.
*/
#define ATTACHLIST_HRULE_ABOVE     0x01 /* Insert <hr> above header */
#define ATTACHLIST_TARGET_BLANK    0x02 /* use target=_blank for links */
#define ATTACHLIST_SIZE            0x04 /* add size */
#define ATTACHLIST_HIDE_UNAPPROVED 0x08 /* Hide pending-moderation files */
#define ATTACHLIST_DETAILS_CLOSED  0x10 /* Wrap in a closed DETAILS element */
#define ATTACHLIST_DETAILS_OPEN    0x20 /* Wrap in an open DETAILS element */
#endif

/*
** Output HTML to show a list of attachments.
*/
void attachment_list(
  const char *zTarget,   /* Object that things are attached to */
  const char *zHeader,   /* Header to display with attachments */
  const int flags        /* ATTACHLIST_... flags */
){
  int cnt = 0;
  char szBuf[36] = {0};  /* scratchpad for attachment size value */
  const char *zLinkTgt = (ATTACHLIST_TARGET_BLANK & flags)
    ? " target=\"_blank\"" : "";
  const int bUseDetail = flags &
    (ATTACHLIST_DETAILS_CLOSED | ATTACHLIST_DETAILS_OPEN);
  Stmt q;

  db_prepare(&q,
     "SELECT datetime(mtime,toLocal()), a.filename, a.user,"
     "       b1.uuid, a.src, a.target, a.attachid, b2.size\n"
     " FROM attachment a, blob b1, blob b2\n"
     " WHERE a.isLatest\n"
     " AND a.src IS NOT NULL\n"
     " AND a.target=%Q\n"
     " AND b1.rid=a.attachid\n"
     " AND b2.uuid=a.src\n"
     " ORDER BY mtime DESC",
     zTarget
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 0);
    const char *zFile = db_column_text(&q, 1);
    const char *zUser = db_column_text(&q, 2);
    const char *zUuid = db_column_text(&q, 3);
    const char *zSrc = db_column_text(&q, 4);
    const char *zTarget = db_column_text(&q, 5);
    const char *zDispUser = zUser && zUser[0] ? zUser : "anonymous";
    const char *zTypeArg = 0; /* URL arg name for /attachdownload */
    const int aid = db_column_int(&q, 6);
    const int sz = db_column_int(&q, 7);
    if( (flags & ATTACHLIST_HIDE_UNAPPROVED)
        && moderation_pending(aid)
        && !moderation_user_could(aid, 1, 0) ){
      continue;
    }
    if( cnt==0 ){
      if( bUseDetail ){
        @ <details class='attachlist'
        if( ATTACHLIST_DETAILS_OPEN & flags ){
          @ open
        }
        @ >
      }else{
        @ <section class='attachlist'>
      }
      if( flags & ATTACHLIST_HRULE_ABOVE ){
        @ <hr>
      }
      if( bUseDetail ){
        @ <summary>%s(zHeader)</summary>
      }else{
        @ %s(zHeader)
      }
      @ <ul>
    }
    cnt++;
    switch( attachment_target_type(zTarget, 1) ){
      case CFTYPE_TICKET: zTypeArg = "tkt"; break;
      case CFTYPE_FORUM:  zTypeArg = "forumpost"; break;
      case CFTYPE_EVENT:  zTypeArg = "technote"; break;
      case CFTYPE_WIKI:
      default:            zTypeArg = "page"; break;
    }
    @ <li>
    @ <a href="%R/artifact/%!S(zSrc)"%s(zLinkTgt)>%h(zFile)</a>
    if( flags & ATTACHLIST_SIZE ){
      sqlite3_snprintf(sizeof(szBuf), szBuf, " %d bytes", sz);
    }
    @ [<a href="%R/attachdownload/%t(zFile)?%s(zTypeArg)=%t(zTarget)\
    @&file=%t(zFile)%s(zLinkTgt)">download</a>%s(szBuf)]
    @ added by %h(zDispUser) on
    hyperlink_to_date(zDate, ".");
    @ [<a href="%R/ainfo/%!S(zUuid)"%s(zLinkTgt)>details</a>]
    moderation_pending_www(aid);
    @ </li>
  }
  if( cnt ){
    @ </ul>
    if( bUseDetail ){
      @ </details>
    }else{
      @ </section>
    }
  }
  db_finalize(&q);
}

/*
** COMMAND: attachment*
**
** Usage: %fossil attachment add ?PAGENAME? FILENAME ?OPTIONS?
**
** Add an attachment to an existing wiki page or tech note.
**
** Options:
**    -t|--technote DATETIME      Specifies the timestamp of
**                                the technote to which the attachment
**                                is to be made. The attachment will be
**                                to the most recently modified tech note
**                                with the specified timestamp.
**    -t|--technote TECHNOTE-ID   Specifies the technote to be
**                                updated by its technote id
**
** One of PAGENAME, DATETIME or TECHNOTE-ID must be specified.
**
** DATETIME may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
** year-month-day form, it may be truncated, the "T" may be replaced by
** a space, and it may also name a timezone offset from UTC as "-HH:MM"
** (westward) or "+HH:MM" (eastward). Either no timezone suffix or "Z"
** means UTC.
*/
void attachment_cmd(void){
  int n;
  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    goto attachment_cmd_usage;
  }
  n = strlen(g.argv[2]);
  if( n==0 ){
    goto attachment_cmd_usage;
  }

  if( strncmp(g.argv[2],"add",n)==0 ){
    const char *zPageName = 0;    /* Name of the wiki page to attach to */
    const char *zFile;            /* Name of the file to be attached */
    const char *zETime;           /* The name of the technote to attach to */
    Manifest *pWiki = 0;          /* Parsed wiki page content */
    char *zBody = 0;              /* Wiki page content */
    int rid;
    const char *zTarget;          /* Target of the attachment */
    Blob content;                 /* The content of the attachment */
    zETime = find_option("technote","t",1);
    if( !zETime ){
      if( g.argc!=5 ){
        usage("add PAGENAME FILENAME");
      }
      zPageName = g.argv[3];
      rid = db_int(0, "SELECT x.rid FROM tag t, tagxref x"
        " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q'"
        " ORDER BY x.mtime DESC LIMIT 1",
        zPageName
      );
      if( (pWiki = manifest_get(rid, CFTYPE_WIKI, 0))!=0 ){
        zBody = pWiki->zWiki;
      }
      if( zBody==0 ){
        fossil_fatal("wiki page [%s] not found",zPageName);
      }
      zTarget = zPageName;
      zFile = g.argv[4];
    }else{
      if( g.argc!=4 ){
        usage("add FILENAME --technote DATETIME|TECHNOTE-ID");
      }
      rid = wiki_technote_to_rid(zETime);
      if( rid<0 ){
        fossil_fatal("ambiguous tech note id: %s", zETime);
      }
      if( (pWiki = manifest_get(rid, CFTYPE_EVENT, 0))!=0 ){
        zBody = pWiki->zWiki;
      }
      if( zBody==0 ){
        fossil_fatal("technote [%s] not found",zETime);
      }
      zTarget = db_text(0,
        "SELECT substr(tagname,7) FROM tag "
        "  WHERE tagid=(SELECT tagid FROM event WHERE objid='%d')",
        rid
      );
      zFile = g.argv[3];
    }
    blob_read_from_file(&content, zFile, ExtFILE);
    user_select();
    attach_commit(
      zFile,                   /* The filename of the attachment */
      zTarget,                 /* The artifact hash to attach to */
      blob_buffer(&content),   /* The content of the attachment */
      blob_size(&content),     /* The length of the attachment */
      0,                       /* No need to moderate the attachment */
      ""                       /* Empty attachment comment */
    );
    if( !zETime ){
      fossil_print("Attached %s to wiki page %s.\n", zFile, zPageName);
    }else{
      fossil_print("Attached %s to tech note %s.\n", zFile, zETime);
    }
  }else{
    goto attachment_cmd_usage;
  }
  return;

attachment_cmd_usage:
  usage("add ?PAGENAME? FILENAME [-t|--technote DATETIME ]");
}


/*
** COMMAND: test-list-attachments
**
** Usage: %fossil test-list-attachments ?-latest? ?TargetName(s)...?
**
** List attachments for one or more attachment targets. The target
** name arguments are glob prefixes for the attachment.target
** field. If no names are provided then a prefix of [a-zA-Z] is used,
** which will match most wiki page names and some ticket hashes.
**
** Options:
**    -latest    List only the latest version of a given attachment
**
*/
void test_list_attachments(void){
  Stmt q;
  int i;
  const int fLatest = find_option("latest", 0, 0) != 0;

  db_find_and_open_repository(0, 0);
  verify_all_options();
  db_prepare(&q,
     "SELECT datetime(mtime,toLocal()), src, target, filename,"
     "       comment, user "
     "  FROM attachment"
     "  WHERE target GLOB :tgtname ||'*'"
     "  AND (isLatest OR %d)"
     "  ORDER BY target, isLatest DESC, mtime DESC",
     !fLatest
  );
  if(g.argc<3){
    static char * argv[3] = {0,0,"[a-zA-Z]"};
    g.argc = 3;
    g.argv = argv;
  }
  for(i = 2; i < g.argc; ++i){
    const char *zPage = g.argv[i];
    db_bind_text(&q, ":tgtname", zPage);
    while(SQLITE_ROW == db_step(&q)){
      const char *zTime = db_column_text(&q, 0);
      const char *zSrc = db_column_text(&q, 1);
      const char *zTarget = db_column_text(&q, 2);
      const char *zName = db_column_text(&q, 3);
      printf("%-20s %s %.12s %s\n", zTarget, zTime, zSrc, zName);
    }
    db_reset(&q);
  }
  db_finalize(&q);
}

/*
** Renders the list of attachments for artifact pManifest as JSON to
** blob pOut. If pManifest->type is not one of (CFTYPE_TICKET,
** CFTYPE_FORUM, CFTYPE_EVENT, CFTYPE_WIKI) then it behaves as if the
** result set is empty.
**
** If there are no matching attachments then its behavior depends on
** emptyPolicy:
**
**  <0 = emit a JSON NULL
**   0 = emit no output
**  >0 = emit an empty JSON array
**
** If bLatestOnly is true then only the most recent entry for a given
** attachment is emitted, else all versions are emitted in descending
** mtime order.
**
** Returns the number of attachments.
**
** Output format:
**
** [{
**   "uuid": attachment artifact hash,
**   "src": hash of the attachment blob,
**   "target": wiki page name or ticket/event ID,
**   "filename": filename of attachment,
**   "mtime": ISO-8601 timestamp UTC,
**   "isLatest": true if this is the latest version of this file
**               else false,
** }, ...once per attachment]
**
*/
int attachments_to_json(const Manifest *pManifest,
                        Blob *pOut, int bLatestOnly,
                        int emptyPolicy){
  int i = 0;
  Stmt q = empty_Stmt;
  char *zToFree = 0;
  const char *zTgt = 0;
  switch(pManifest->type){
    case CFTYPE_FORUM:  zTgt = zToFree = rid_to_uuid(pManifest->rid);
      break;
    case CFTYPE_WIKI:   zTgt = pManifest->zWikiTitle;  break;
    case CFTYPE_EVENT:  zTgt = pManifest->zEventId;    break;
    case CFTYPE_TICKET: zTgt = pManifest->zTicketUuid; break;
    default:
      goto empty_result;
  }
  db_prepare(&q,
     "SELECT datetime(mtime), a.src, a.target, a.filename, a.isLatest,\n"
     "  b2.size, b1.uuid, a.user, a.comment\n"
     "  FROM attachment a, blob b1, blob b2\n"
     "  WHERE a.target=%Q\n"
     "  AND a.src IS NOT NULL\n"
     "  AND b1.rid=a.attachid\n"
     "  AND b2.uuid=a.src\n"
     "  AND (a.isLatest OR %d)\n"
     "  ORDER BY a.target, a.isLatest DESC, a.mtime DESC\n",
     zTgt, !bLatestOnly
  );
  while(SQLITE_ROW == db_step(&q)){
    const char *zTime = db_column_text(&q, 0);
    const char *zSrc = db_column_text(&q, 1);
    const char *zTarget = db_column_text(&q, 2);
    const char *zName = db_column_text(&q, 3);
    const int isLatest = db_column_int(&q, 4);
    const int sz = db_column_int(&q, 5);
    const char *zUuid = db_column_text(&q, 6);
    const char *zUser = db_column_text(&q, 7);
    const char *zComment = db_column_text(&q, 8);
    if(!i++){
      blob_append_char(pOut, '[');
    }else{
      blob_append_char(pOut, ',');
    }
    blob_appendf(
      pOut,
      "{\"uuid\": %!j, \"src\": %!j, \"target\": %!j, "
      "\"filename\": %!j, \"size\":%d, \"mtime\": %!j, "
      "\"isLatest\": %s, \"user\": %!j, \"comment\": ",
      zUuid, zSrc, zTarget,
      zName, sz, zTime, isLatest ? "true" : "false",
      zUser
    );
    if( zComment && zComment[0] ){
      blob_appendf(pOut, "%!j", zComment);
    }else{
      blob_append_literal(pOut, "null");
    }
    blob_append_char(pOut, '}');
  }
  fossil_free(zToFree);
  db_finalize(&q);
  if(!i){
  empty_result:
    if( emptyPolicy>0 ){
      blob_append_literal(pOut, "[]");
    }else if( emptyPolicy<0 ){
      blob_append_literal(pOut, "null");
    }
  }else{
    blob_append_char(pOut, ']');
  }
  return i;
}

/*
** COMMAND: test-attachment-target
**
** Usage: %fossil test-attachment-target TARGET_ID...
*/
void test_attachment_target_type_cmd(void){
  int i;
  verify_all_options();
  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    usage("test-attachment-target TARGET_ID");
    return;
  }
  for( i = 2; i < g.argc; ++i ){
    const char *zTarget = g.argv[i];
    const int rid = attachment_target_rid(zTarget, 0);
    const int type = attachment_target_type(zTarget, 0);
    const char *zType = "<invalid>";
    switch(type){
      case CFTYPE_EVENT:  zType = "technote"; break;
      case CFTYPE_FORUM:  zType = "forumpost"; break;
      case CFTYPE_TICKET: zType = "ticket"; break;
      case CFTYPE_WIKI:   zType = "wiki"; break;
    }
    fossil_print("%-20s = %-9s #%d %z\n",
                 zTarget, zType, rid,
                 rid>0 ? rid_to_uuid(rid) : 0);
  }
}


/*
** COMMAND: test-attachments-to-json
**
** Usage: %fossil test-attachments-to-json TARGET_ID...
**
** Options:
**    --old          List all versions of attachments. Default is to
**                   list only the latest.
**    --full         Require a full target ID, not a prefix.
**
** Emits a JSON array of attachments for the given attachment targets.
** The given IDs must be wiki page names, ticket hashes, tech-note
** hashes, or forum post hashes. By default it accepts hash prefixes
** but does no detection of ambiguity or cross-type prefix collisions
** so may emit curious results if given short, colliding IDs.
*/
void test_attachments_to_json_cmd(void){
  const int emptyPolicy = 1;
  const int bLatestOnly = find_option("old",0,0)==0;
  const int bFullId = find_option("full",0,0)!=0;
  int i;

  verify_all_options();
  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    usage("test-attachments-to-json TARGET_ID");
    return;
  }
  for( i = 2; i < g.argc; ++i ){
    const char *zTarget = g.argv[i];
    const int rid = attachment_target_rid(zTarget, bFullId);
    if( 0==rid ){
      fossil_print("** cannot resolve %s\n", zTarget);
    }else{
      Blob b = BLOB_INITIALIZER;
      Manifest *pManifest = manifest_get(rid, CFTYPE_ANY, NULL);
      assert( pManifest );
      attachments_to_json(pManifest, &b, bLatestOnly, emptyPolicy);
      fossil_print("Attachments for %s: ", zTarget);
      if( b.nUsed ){
        char *zPretty = db_text(0,"SELECT json_pretty(%B)", &b);
        fossil_print("%s\n", zPretty);
        fossil_free(zPretty);
      }else{
        fossil_print("none\n");
      }
      blob_reset(&b);
      manifest_destroy(pManifest);
    }
  }
}
