/*
** Copyright (c) 2008 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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
** Return true if the given UUID should be shunned.
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
  int nUuid;
  char zCanonical[UUID_SIZE+1];

  login_check_credentials();
  if( !g.okAdmin ){
    login_needed();
  }
  if( P("rebuild") ){
    db_begin_transaction();
    rebuild_db(0,0);
    db_end_transaction(0);
  }
  if( zUuid ){
    nUuid = strlen(zUuid);
    if( nUuid!=40 || !validate16(zUuid, nUuid) ){
      zUuid = 0;
    }else{
      memcpy(zCanonical, zUuid, UUID_SIZE+1);
      canonical16(zCanonical, UUID_SIZE);
      zUuid = zCanonical;
    }
  }
  style_header("Shunned Artifacts");
  if( zUuid && P("sub") ){
    login_verify_csrf_secret();
    db_multi_exec("DELETE FROM shun WHERE uuid='%s'", zUuid);
    if( db_exists("SELECT 1 FROM blob WHERE uuid='%s'", zUuid) ){
      @ <p><font color="blue">Artifact 
      @ <a href="%s(g.zBaseURL)/artifact/%s(zUuid)">%s(zUuid)</a> is no
      @ longer being shunned.</font></p>
    }else{
      @ <p><font color="blue">Artifact %s(zUuid)</a> will no longer
      @ be shunned.  But it does not exist in the repository.  It
      @ may be necessary to rebuild the repository using the
      @ <b>fossil rebuild</b> command-line before the artifact content
      @ can pulled in from other respositories.</font></p>
    }
  }
  if( zUuid && P("add") ){
    login_verify_csrf_secret();
    db_multi_exec("INSERT OR IGNORE INTO shun VALUES('%s')", zUuid);
    @ <p><font color="blue">Artifact
    @ <a href="%s(g.zBaseURL)/artifact/%s(zUuid)">%s(zUuid)</a> has been
    @ shunned.  It will no longer be pushed.
    @ It will be removed from the repository the next time the respository
    @ is rebuilt using the <b>fossil rebuild</b> command-line</font></p>
  }
  @ <p>The artifacts listed below have been shunned by this repository.
  @ This means that the artifacts will not be transmitted on a push nor
  @ recieved on a pull.  These artifacts are banned from the respository.</p>
  @ <blockquote>
  db_prepare(&q, 
     "SELECT uuid, EXISTS(SELECT 1 FROM blob WHERE blob.uuid=shun.uuid)"
     "  FROM shun ORDER BY uuid");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    int stillExists = db_column_int(&q, 1);
    cnt++;
    if( stillExists ){
      @ <b><a href="%s(g.zBaseURL)/artifact/%s(zUuid)">%s(zUuid)</a></b><br>
    }else{
      @ <b>%s(zUuid)</b><br>
    }
  }
  if( cnt==0 ){
    @ <i>no artifacts are shunned on this server</i>
  }
  db_finalize(&q);
  @ </blockquote>
  @ <hr>
  @ <a name="addshun"></a>
  @ <p>To shun an artifact, enter its UUID in the
  @ following box and press the "Shun" button.  This will cause the artifact
  @ to be removed from the repository and will prevent the artifact from being
  @ readded to the repository by subsequent sync operation.</p>
  @
  @ <p>Warning:  Shunning should only be used to remove inappropriate content
  @ from the repository.  Inappropriate content includes such things as
  @ spam added to Wiki, files that violate copyright or patent agreements,
  @ or artifacts that by design or accident interfere with the processing
  @ of the repository.  Do not shun artifacts merely to remove them from
  @ sight - set the "hidden" tag on such artifacts instead.</p>
  @ 
  @ <blockquote>
  @ <form method="POST" action="%s(g.zBaseURL)/%s(g.zPath)">
  login_insert_csrf_secret();
  @ <input type="text" name="uuid" value="%h(PD("shun",""))" size="50">
  @ <input type="submit" name="add" value="Shun">
  @ </form>
  @ </blockquote>
  @
  @ <p>Enter the UUID of a previous shunned artifact to cause it to be
  @ accepted again in the repository.  The artifact content is not
  @ restored because the content is unknown.  The only change is that
  @ the formerly shunned artifact will be accepted on subsequent sync
  @ operations.</p>
  @
  @ <blockquote>
  @ <form method="POST" action="%s(g.zBaseURL)/%s(g.zPath)">
  login_insert_csrf_secret();
  @ <input type="text" name="uuid" size="50">
  @ <input type="submit" name="sub" value="Accept">
  @ </form>
  @ </blockquote>
  @
  @ <p>Press the button below to rebuild the respository.  The rebuild
  @ may take several seconds, so be patient after pressing the button.</p>
  @
  @ <blockquote>
  @ <form method="POST" action="%s(g.zBaseURL)/%s(g.zPath)">
  login_insert_csrf_secret();
  @ <input type="submit" name="rebuild" value="Rebuild">
  @ </form>
  @ </blockquote>
  @  
  style_footer();
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
  );
}
