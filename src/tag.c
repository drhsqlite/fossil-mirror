/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to manage tags
*/
#include "config.h"
#include "tag.h"
#include <assert.h>

/*
** Propagate the tag given by tagid to the children of pid.
**
** This routine assumes that tagid is a tag that should be
** propagated and that the tag is already present in pid.
**
** If addFlag is true then the tag is added.  If false, then the
** tag is removed.
*/
void tag_propagate(
  int pid,             /* Propagate the tag to children of this node */
  int tagid,           /* Tag to propagate */
  int addFlag,         /* True to add the tag. False to delete it. */
  const char *zValue,  /* Value of the tag.  Might be NULL */
  double mtime         /* Timestamp on the tag */
){
  PQueue queue;
  Stmt s, ins;
  pqueue_init(&queue);
  pqueue_insert(&queue, pid, 0.0);
  db_prepare(&s, 
     "SELECT cid, plink.mtime,"
     "       coalesce(srcid=0 AND tagxref.mtime<:mtime, %d) AS doit"
     "  FROM plink LEFT JOIN tagxref ON cid=rid AND tagid=%d"
     " WHERE pid=:pid AND isprim",
     addFlag, tagid
  );
  db_bind_double(&s, ":mtime", mtime);
  if( addFlag ){
    db_prepare(&ins,
       "REPLACE INTO tagxref(tagid, addFlag, srcid, value, mtime, rid)"
       "VALUES(%d,1,0,%Q,:mtime,:rid)",
       tagid, zValue
    );
    db_bind_double(&ins, ":mtime", mtime);
  }else{
    db_prepare(&ins,
       "DELETE FROM tagxref WHERE tagid=%d AND rid=:rid", tagid
    );
  }
  while( (pid = pqueue_extract(&queue))!=0 ){
    db_bind_int(&s, ":pid", pid);
    while( db_step(&s)==SQLITE_ROW ){
      int doit = db_column_int(&s, 2);
      if( doit ){
        int cid = db_column_int(&s, 0);
        double mtime = db_column_double(&s, 1);
        pqueue_insert(&queue, cid, mtime);
        db_bind_int(&ins, ":rid", cid);
        db_step(&ins);
        db_reset(&ins);
      }
    }
    db_reset(&s);
  }
  pqueue_clear(&queue);
  db_finalize(&ins);
  db_finalize(&s);
}

/*
** Propagate all propagatable tags in pid to its children.
*/
void tag_propagate_all(int pid){
  Stmt q;
  db_prepare(&q,
     "SELECT tagid, addflag, mtime, value FROM tagxref"
     " WHERE rid=%d"
     "   AND (SELECT tagname FROM tag WHERE tagid=tagxref.tagid) LIKE 'br%'",
     pid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int tagid = db_column_int(&q, 0);
    int addflag = db_column_int(&q, 1);
    double mtime = db_column_double(&q, 2);
    const char *zValue = db_column_text(&q, 3);
    tag_propagate(pid, tagid, addflag, zValue, mtime);
  }
  db_finalize(&q);
}

/*
** Get a tagid for the given TAG.  Create a new tag if necessary
** if createFlag is 1.
*/
int tag_findid(const char *zTag, int createFlag){
  int id;
  id = db_int(0, "SELECT tagid FROM tag WHERE tagname=%Q", zTag);
  if( id==0 && createFlag ){
    db_multi_exec("INSERT INTO tag(tagname) VALUES(%Q)", zTag);
    id = db_last_insert_rowid();
  }
  return id;
}

/*
** Insert a tag into the database.
*/
void tag_insert(
  const char *zTag,        /* Name of the tag (w/o the "+" or "-" prefix */
  int addFlag,             /* True to add.  False to remove */
  const char *zValue,      /* Value if the tag is really a property */
  int srcId,               /* Artifact that contains this tag */
  double mtime,            /* Timestamp.  Use default if <=0.0 */
  int rid                  /* Artifact to which the tag is to attached */
){
  Stmt s;
  int tagid = tag_findid(zTag, 1);
  if( mtime<=0.0 ){
    mtime = db_double(0.0, "SELECT julianday('now')");
  }
  db_prepare(&s, 
    "REPLACE INTO tagxref(tagid,addFlag,srcId,value,mtime,rid)"
    " VALUES(%d,%d,%d,%Q,:mtime,%d)",
    tagid, addFlag, srcId, zValue, rid
  );
  db_bind_double(&s, ":mtime", mtime);
  db_step(&s);
  db_finalize(&s);
  if( strncmp(zTag, "br", 2)==0 ){
    tag_propagate(rid, tagid, 1, zValue, mtime);
  }
}


/*
** COMMAND: test-addtag
** %fossil test-addtag TAGNAME UUID ?VALUE?
**
** Add a tag to the rebuildable tables of the local repository.
** No tag artifact is created so the new tag is erased the next
** time the repository is rebuilt.  This routine is for testing
** use only.
*/
void addtag_cmd(void){
  const char *zTag;
  const char *zValue;
  int rid;
  db_must_be_within_tree();
  if( g.argc!=4 && g.argc!=5 ){
    usage("TAGNAME UUID ?VALUE?");
  }
  zTag = g.argv[2];
  rid = name_to_rid(g.argv[3]);
  if( rid==0 ){
    fossil_fatal("no such object: %s", g.argv[3]);
  }
  zValue = g.argc==5 ? g.argv[4] : 0;
  db_begin_transaction();
  tag_insert(zTag, 1, zValue, -1, 0.0, rid);
  db_end_transaction(0); 
}
/*
** COMMAND: test-deltag
** %fossil test-deltag TAGNAME UUID
**
** Cancel a tag to the rebuildable tables of the local repository.
** No tag artifact is created so the cancellation is undone the next
** time the repository is rebuilt.  This routine is for testing
** use only.
*/
void deltag_cmd(void){
  const char *zTag;
  int rid;
  db_must_be_within_tree();
  if( g.argc!=4 ){
    usage("TAGNAME UUID");
  }
  zTag = g.argv[2];
  rid = name_to_rid(g.argv[3]);
  if( rid==0 ){
    fossil_fatal("no such object: %s", g.argv[3]);
  }
  db_begin_transaction();
  tag_insert(zTag, 0, 0, -1, 0.0, rid);
  db_end_transaction(0); 
}
