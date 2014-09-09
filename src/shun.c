/*
** Copyright (c) 2008 D. Richard Hipp
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
** This file contains code used to manage SHUN table of the repository
*/
#include "config.h"
#include "shun.h"
#include <assert.h>

/*
** Return true if the given artifact ID should be shunned.
*/
int uuid_is_shunned(const char *zUuid){
  static Stmt q;
  int rc;
  if( zUuid==0 || zUuid[0]==0 ) return 0;
  db_static_prepare(&q, "SELECT 1 FROM shun WHERE uuid=:uuid");
  db_bind_text(&q, ":uuid", zUuid);
  rc = db_step(&q);
  db_reset(&q);
  return rc==SQLITE_ROW;
}

/*
** WEBPAGE: shun
*/
void shun_page(void){
  Stmt q;
  int cnt = 0;
  const char *zUuid = P("uuid");
  const char *zShun = P("shun");
  const char *zAccept = P("accept");
  int nUuid;
  char *zCanonical = 0;

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed();
  }
  if( P("rebuild") ){
    db_close(1);
    db_open_repository(g.zRepositoryName);
    db_begin_transaction();
    rebuild_db(0, 0, 0);
    db_end_transaction(0);
  }
  if( zUuid ){
    char *p;
    int i = 0;
    int j = 0;
    zCanonical = fossil_malloc(strlen(zUuid)+2);
    while( zUuid[i] ){
      if( fossil_isspace(zUuid[i]) ){
        if( j && zCanonical[j-1] ){
          zCanonical[j] = 0;
          j++;
        }
      }else{
        zCanonical[j] = zUuid[i];
        j++;
      }
      i++;
    }
    zCanonical[j+1] = zCanonical[j] = 0;
    p = zCanonical;
    while( *p ){
      nUuid = strlen(p);
      if( nUuid!=UUID_SIZE || !validate16(p, nUuid) ){
        @ <p class="generalError">Error: Bad artifact IDs.</p>
        fossil_free(zCanonical);
        zCanonical = 0;
        break;
      }else{
        canonical16(p, UUID_SIZE);
        p += UUID_SIZE+1;
      }
    }
    zUuid = zCanonical;
  }
  style_header("Shunned Artifacts");
  if( zUuid && P("sub") ){
    const char *p = zUuid;
    int allExist = 1;
    login_verify_csrf_secret();
    while( *p ){
      db_multi_exec("DELETE FROM shun WHERE uuid='%s'", p);
      if( !db_exists("SELECT 1 FROM blob WHERE uuid='%s'", p) ){
        allExist = 0;
      }
      p += UUID_SIZE+1;
    }
    if( allExist ){
      @ <p class="noMoreShun">Artifact(s)<br />
      for( p = zUuid ; *p ; p += UUID_SIZE+1 ){
        @ <a href="%s(g.zTop)/artifact/%s(p)">%s(p)</a><br />
      }
      @ are no longer being shunned.</p>
    }else{
      @ <p class="noMoreShun">Artifact(s)<br />
      for( p = zUuid ; *p ; p += UUID_SIZE+1 ){
        @ %s(p)<br />
      }
      @ will no longer be shunned.  But they may not exist in the repository.
      @ It may be necessary to rebuild the repository using the
      @ <b>fossil rebuild</b> command-line before the artifact content
      @ can pulled in from other repositories.</p>
    }
  }
  if( zUuid && P("add") ){
    const char *p = zUuid;
    int rid, tagid;
    login_verify_csrf_secret();
    while( *p ){
      db_multi_exec(
        "INSERT OR IGNORE INTO shun(uuid,mtime)"
        " VALUES('%s', now())", p);
      db_multi_exec("DELETE FROM attachment WHERE src=%Q", p);
      rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q", p);
      if( rid ){
        db_multi_exec("DELETE FROM event WHERE objid=%d", rid);
      }
      tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname='tkt-%q'", p);
      if( tagid ){
        db_multi_exec("DELETE FROM ticket WHERE tkt_uuid=%Q", p);
        db_multi_exec("DELETE FROM tag WHERE tagid=%d", tagid);
        db_multi_exec("DELETE FROM tagxref WHERE tagid=%d", tagid);
      }
      p += UUID_SIZE+1;
    }
    @ <p class="shunned">Artifact(s)<br />
    for( p = zUuid ; *p ; p += UUID_SIZE+1 ){
      @ <a href="%s(g.zTop)/artifact/%s(p)">%s(p)</a><br />
    }
    @ have been shunned.  They will no longer be pushed.
    @ They will be removed from the repository the next time the repository
    @ is rebuilt using the <b>fossil rebuild</b> command-line</p>
  }
  @ <p>A shunned artifact will not be pushed nor accepted in a pull and the
  @ artifact content will be purged from the repository the next time the
  @ repository is rebuilt.  A list of shunned artifacts can be seen at the
  @ bottom of this page.</p>
  @
  @ <a name="addshun"></a>
  @ <p>To shun artifacts, enter their artifact IDs (the 40-character SHA1
  @ hash of the artifacts) in the
  @ following box and press the "Shun" button.  This will cause the artifacts
  @ to be removed from the repository and will prevent the artifacts from being
  @ readded to the repository by subsequent sync operation.</p>
  @
  @ <p>Note that you must enter the full 40-character artifact IDs, not
  @ an abbreviation or a symbolic tag.</p>
  @
  @ <p>Warning:  Shunning should only be used to remove inappropriate content
  @ from the repository.  Inappropriate content includes such things as
  @ spam added to Wiki, files that violate copyright or patent agreements,
  @ or artifacts that by design or accident interfere with the processing
  @ of the repository.  Do not shun artifacts merely to remove them from
  @ sight - set the "hidden" tag on such artifacts instead.</p>
  @
  @ <blockquote>
  @ <form method="post" action="%s(g.zTop)/%s(g.zPath)"><div>
  login_insert_csrf_secret();
  @ <textarea class="fullsize-text" cols="50" rows="3" name="uuid">
  if( zShun ){
    @ %h(zShun)
  }
  @ </textarea>
  @ <input type="submit" name="add" value="Shun" />
  @ </div></form>
  @ </blockquote>
  @
  @ <a name="delshun"></a>
  @ <p>Enter the UUIDs of previously shunned artifacts to cause them to be
  @ accepted again in the repository.  The artifacts content is not
  @ restored because the content is unknown.  The only change is that
  @ the formerly shunned artifacts will be accepted on subsequent sync
  @ operations.</p>
  @
  @ <blockquote>
  @ <form method="post" action="%s(g.zTop)/%s(g.zPath)"><div>
  login_insert_csrf_secret();
  @ <textarea class="fullsize-text" cols="50" rows="3" name="uuid">
  if( zAccept ){
    @ %h(zAccept)
  }
  @ </textarea>
  @ <input type="submit" name="sub" value="Accept" />
  @ </div></form>
  @ </blockquote>
  @
  @ <p>Press the Rebuild button below to rebuild the repository.  The
  @ content of newly shunned artifacts is not purged until the repository
  @ is rebuilt.  On larger repositories, the rebuild may take minute or
  @ two, so be patient after pressing the button.</p>
  @
  @ <blockquote>
  @ <form method="post" action="%s(g.zTop)/%s(g.zPath)"><div>
  login_insert_csrf_secret();
  @ <input type="submit" name="rebuild" value="Rebuild" />
  @ </div></form>
  @ </blockquote>
  @
  @ <hr /><p>Shunned Artifacts:</p>
  @ <blockquote><p>
  db_prepare(&q,
     "SELECT uuid, EXISTS(SELECT 1 FROM blob WHERE blob.uuid=shun.uuid)"
     "  FROM shun ORDER BY uuid");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    int stillExists = db_column_int(&q, 1);
    cnt++;
    if( stillExists ){
      @ <b><a href="%s(g.zTop)/artifact/%s(zUuid)">%s(zUuid)</a></b><br />
    }else{
      @ <b>%s(zUuid)</b><br />
    }
  }
  if( cnt==0 ){
    @ <i>no artifacts are shunned on this server</i>
  }
  db_finalize(&q);
  @ </p></blockquote>
  style_footer();
  fossil_free(zCanonical);
}

/*
** Remove from the BLOB table all artifacts that are in the SHUN table.
*/
void shun_artifacts(void){
  Stmt q;
  db_multi_exec(
     "CREATE TEMP TABLE toshun(rid INTEGER PRIMARY KEY);"
     "INSERT INTO toshun SELECT rid FROM blob, shun WHERE blob.uuid=shun.uuid;"
  );
  db_prepare(&q,
     "SELECT rid FROM delta WHERE srcid IN toshun"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int srcid = db_column_int(&q, 0);
    content_undelta(srcid);
  }
  db_finalize(&q);
  db_multi_exec(
     "DELETE FROM delta WHERE rid IN toshun;"
     "DELETE FROM blob WHERE rid IN toshun;"
     "DROP TABLE toshun;"
     "DELETE FROM private "
     " WHERE NOT EXISTS (SELECT 1 FROM blob WHERE rid=private.rid);"
  );
}

/*
** WEBPAGE: rcvfromlist
**
** Show a listing of RCVFROM table entries.
*/
void rcvfromlist_page(void){
  int ofst = atoi(PD("ofst","0"));
  int cnt;
  Stmt q;

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed();
  }
  style_header("Content Sources");
  if( ofst>0 ){
    style_submenu_element("Newer", "Newer", "rcvfromlist?ofst=%d",
                           ofst>30 ? ofst-30 : 0);
  }
  db_prepare(&q,
    "SELECT rcvid, login, datetime(rcvfrom.mtime), rcvfrom.ipaddr"
    "  FROM rcvfrom LEFT JOIN user USING(uid)"
    " ORDER BY rcvid DESC LIMIT 31 OFFSET %d",
    ofst
  );
  @ <p>Whenever new artifacts are added to the repository, either by
  @ push or using the web interface, an entry is made in the RCVFROM table
  @ to record the source of that artifact.  This log facilitates
  @ finding and fixing attempts to inject illicit content into the
  @ repository.</p>
  @
  @ <p>Click on the "rcvid" to show a list of specific artifacts received
  @ by a transaction.  After identifying illicit artifacts, remove them
  @ using the "Shun" feature.</p>
  @
  @ <table cellpadding="0" cellspacing="0" border="0">
  @ <tr><th style="padding-right: 15px;text-align: right;">rcvid</th>
  @     <th style="padding-right: 15px;text-align: left;">Date</th>
  @     <th style="padding-right: 15px;text-align: left;">User</th>
  @     <th style="text-align: left;">IP&nbsp;Address</th></tr>
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    int rcvid = db_column_int(&q, 0);
    const char *zUser = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zIpAddr = db_column_text(&q, 3);
    if( cnt==30 ){
      style_submenu_element("Older", "Older",
         "rcvfromlist?ofst=%d", ofst+30);
    }else{
      cnt++;
      @ <tr>
      @ <td style="padding-right: 15px;text-align: right;"><a href="rcvfrom?rcvid=%d(rcvid)">%d(rcvid)</a></td>
      @ <td style="padding-right: 15px;text-align: left;">%s(zDate)</td>
      @ <td style="padding-right: 15px;text-align: left;">%h(zUser)</td>
      @ <td style="text-align: left;">%s(zIpAddr)</td>
      @ </tr>
    }
  }
  db_finalize(&q);
  @ </table>
  style_footer();
}

/*
** WEBPAGE: rcvfrom
**
** Show a single RCVFROM table entry.
*/
void rcvfrom_page(void){
  int rcvid = atoi(PD("rcvid","0"));
  Stmt q;

  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed();
  }
  style_header("Content Source %d", rcvid);
  db_prepare(&q,
    "SELECT login, datetime(rcvfrom.mtime), rcvfrom.ipaddr"
    "  FROM rcvfrom LEFT JOIN user USING(uid)"
    " WHERE rcvid=%d",
    rcvid
  );
  @ <table cellspacing="15" cellpadding="0" border="0">
  @ <tr><th valign="top" align="right">rcvid:</th>
  @ <td valign="top">%d(rcvid)</td></tr>
  if( db_step(&q)==SQLITE_ROW ){
    const char *zUser = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zIpAddr = db_column_text(&q, 2);
    @ <tr><th valign="top" align="right">User:</th>
    @ <td valign="top">%s(zUser)</td></tr>
    @ <tr><th valign="top" align="right">Date:</th>
    @ <td valign="top">%s(zDate)</td></tr>
    @ <tr><th valign="top" align="right">IP&nbsp;Address:</th>
    @ <td valign="top">%s(zIpAddr)</td></tr>
  }
  db_finalize(&q);
  db_prepare(&q,
    "SELECT rid, uuid, size FROM blob WHERE rcvid=%d", rcvid
  );
  @ <tr><th valign="top" align="right">Artifacts:</th>
  @ <td valign="top">
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    int size = db_column_int(&q, 2);
    @ <a href="%s(g.zTop)/info/%s(zUuid)">%s(zUuid)</a>
    @ (rid: %d(rid), size: %d(size))<br />
  }
  @ </td></tr>
  @ </table>
  db_finalize(&q);
  style_footer();
}
