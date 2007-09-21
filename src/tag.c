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
  int pid,         /* Propagate the tag to children of this node */
  int tagid,       /* Tag to propagate */
  int addFlag,     /* True to add the tag. False to delete it. */
  double mtime     /* Timestamp on the tag */
){
  PQueue queue;
  Stmt s, ins;
  pqueue_init(&queue);
  pqueue_insert(&queue, pid, 0.0);
  db_prepare(&s, 
     "SELECT cid, mtime, coalesce(srcid=0 AND mtime<:mtime, %d) AS doit"
     "  FROM plink LEFT JOIN tagxref ON cid=rid AND tagid=%d"
     " WHERE pid=:pid AND isprim",
     addFlag, tagid
  );
  db_bind_double(&s, ":mtime", mtime);
  if( addFlag ){
    db_prepare(&ins,
       "REPLACE INTO tagxref(tagid, addFlag, srcid, mtime, rid)"
       "VALUES(%d,1,0,:mtime,:rid)",
       tagid
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
     "SELECT tagid, addflag, mtime FROM tagxref"
     " WHERE rid=%d"
     "   AND (SELECT tagname FROM tag WHERE tagid=tagxref.tagid) LIKE 'br%'",
     pid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int tagid = db_column_int(&q, 0);
    int addflag = db_column_int(&q, 1);
    double mtime = db_column_double(&q, 2);
    tag_propagate(pid, tagid, addflag, mtime);
  }
  db_finalize(&q);
}
