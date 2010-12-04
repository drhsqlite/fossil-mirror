/*
** Copyright (c) 2010 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@sqlite.org
**
*******************************************************************************
**
** This file contains code used to implement the "bisect" command.
*/
#include "config.h"
#include "bisect.h"
#include <assert.h>

/* Nodes for the shortest path algorithm.
*/
typedef struct BisectNode BisectNode;
struct BisectNode {
  int rid;                 /* ID for this node */
  BisectNode *pFrom;       /* Node we came from */
  BisectNode *pPeer;       /* List of nodes of the same generation */
  BisectNode *pAll;        /* List of all nodes */
};

/*
** Local variables for this module
*/
static struct {
  BisectNode *pCurrent;   /* Current generation of nodes */
  BisectNode *pAll;       /* All nodes */
  Bag seen;               /* Nodes seen before */
  int bad;                /* The bad version */
  int good;               /* The good version */
  int nStep;              /* Number of steps from good to bad */
  BisectNode *pEnd;       /* Most recent (good) */
} bisect;

/*
** Create a new node
*/
static BisectNode *bisect_new_node(int rid, BisectNode *pFrom){
  BisectNode *p;

  p = fossil_malloc( sizeof(*p) );
  p->rid = rid;
  p->pFrom = pFrom;
  p->pPeer = bisect.pCurrent;
  bisect.pCurrent = p;
  p->pAll = bisect.pAll;
  bisect.pAll = p;
  bisect.pEnd = p;
  bag_insert(&bisect.seen, rid);
  return p;
}

/*
** Reset memory used by the shortest path algorithm.
*/
static void bisect_reset(void){
  BisectNode *p;
  while( bisect.pAll ){
    p = bisect.pAll;
    bisect.pAll = p->pAll;
    fossil_free(p);
  }
  bag_clear(&bisect.seen);
  bisect.pCurrent = 0;
  bisect.pAll = 0;
  bisect.pEnd = 0;
  bisect.nStep = 0;
}

/*
** Compute the shortest path from iFrom to iTo
*/
static BisectNode *bisect_shortest_path(int iFrom, int iTo){
  Stmt s;
  BisectNode *pStart;
  BisectNode *pPrev;
  BisectNode *p;

  bisect_reset();
  pStart = bisect_new_node(iFrom, 0);
  if( iTo==iFrom ) return pStart;
  db_prepare(&s, "SELECT cid FROM plink WHERE pid=:pid "
                 "UNION ALL SELECT pid FROM plink WHERE cid=:pid");
  while( bisect.pCurrent ){
    bisect.nStep++;
    pPrev = bisect.pCurrent;
    bisect.pCurrent = 0;
    while( pPrev ){
      db_bind_int(&s, ":pid", pPrev->rid);
      while( db_step(&s)==SQLITE_ROW ){
        int cid = db_column_int(&s, 0);
        if( bag_find(&bisect.seen, cid) ) continue;
        p = bisect_new_node(cid, pPrev);
        if( cid==iTo ){
          db_finalize(&s);
          return p;
        }
      }
      db_reset(&s);
      pPrev = pPrev->pPeer;
    }
  }
  bisect_reset();
  return 0;
}

/*
** Find the shortest path between bad and good.
*/
static BisectNode *bisect_path(void){
  BisectNode *p;
  bisect.bad = db_lget_int("bisect-bad", 0);
  if( bisect.bad==0 ){
    bisect.bad = db_int(0, "SELECT pid FROM plink ORDER BY mtime LIMIT 1");
    db_lset_int("bisect-bad", bisect.bad);
  }
  bisect.good = db_lget_int("bisect-good", 0);
  if( bisect.good==0 ){
    bisect.good = db_int(0,"SELECT cid FROM plink ORDER BY mtime DESC LIMIT 1");
    db_lset_int("bisect-good", bisect.good);
  }
  p = bisect_shortest_path(bisect.good, bisect.bad);
  if( p==0 ){
    char *zBad = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", bisect.bad);
    char *zGood = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", bisect.good);
    fossil_fatal("no path from good ([%S]) to bad ([%S]) or back",
                 zGood, zBad);
  }
  return p;
}



/*
** COMMAND: bisect
**
** Usage: %fossil bisect SUBCOMMAND ...
**
** Run various subcommands useful for searching for bugs.
**
**   fossil bisect bad ?VERSION?
**
**     Identify version VERSION as non-working.  If VERSION is omitted,
**     the current checkout is marked as non-working.
**
**   fossil bisect good ?VERSION?
**
**     Identify version VERSION as working.  If VERSION is omitted,
**     the current checkout is marked as working.
**
**   fossil bisect next
**
**     Update to the next version that is halfway between the working and
**     non-working versions.
**
**   fossil bisect reset
**
**     Reinitialize a bisect session.  This cancels prior bisect history
**     and allows a bisect session to start over from the beginning.
**
**   fossil bisect run SCRIPT
**
**     Automatically bisect between "bad" and "good" versions, running SCRIPT
**     at each step to determine if the version is bad or good.  SCRIPT should
**     exit with result code zero if the version is good, or non-zero if the
**     version is bad.
**
**   fossil bisect timeline
**
**     Show a timeline of all changes in between the identified "bad" and 
**     "good" versions.
**
**   fossil bisect vlist
**
**     List the versions in between "bad" and "good".
*/
void bisect_cmd(void){
  int n;
  const char *zCmd;
  db_must_be_within_tree();
  zCmd = g.argv[2];
  n = strlen(zCmd);
  if( n>=1 && memcmp(zCmd, "bad", n)==0 ){
    int ridBad;
    if( g.argc==3 ){
      ridBad = db_lget_int("checkout",0);
    }else{
      ridBad = name_to_rid(g.argv[3]);
    }
    db_lset_int("bisect-bad", ridBad);
  }else if( memcmp(zCmd, "good", n)==0 ){
    int ridGood;
    if( g.argc==3 ){
      ridGood = db_lget_int("checkout",0);
    }else{
      ridGood = name_to_rid(g.argv[3]);
    }
    db_lset_int("bisect-good", ridGood);
  }else if( n>=2 && memcmp(zCmd, "reset", n)==0 ){
    db_multi_exec(
      "REPLACE INTO vvar(name, value) "
      "  SELECT 'bisect-good', pid FROM plink ORDER BY mtime LIMIT 1;"
      "REPLACE INTO vvar(name, value) "
      "  SELECT 'bisect-bad', cid FROM plink ORDER BY mtime DESC LIMIT 1;"
    );
  }else if( n>=2 && memcmp(zCmd, "vlist", n)==0 ){
    BisectNode *p;
    Stmt s;
    bisect_path();
    db_prepare(&s, "SELECT substr(blob.uuid,1,20) || ' ' || "
                   "       datetime(event.mtime) FROM blob, event"
                   " WHERE blob.rid=:rid AND event.objid=:rid"
                   "   AND event.type='ci'");
    for(p=bisect.pEnd; p; p=p->pFrom){
      const char *z;
      db_bind_int(&s, ":rid", p->rid);
      if( db_step(&s)==SQLITE_ROW ){
        z = db_column_text(&s, 0);
        printf("%s", z);
        if( p->rid==bisect.good ) printf(" GOOD");
        if( p->rid==bisect.bad ) printf(" BAD");
        printf("\n");
      }
      db_reset(&s);
    }
    db_finalize(&s);
  }else{
    usage("bad|good|next|reset|run|timeline|vlist ...");
  }
}
