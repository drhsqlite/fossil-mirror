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
**
** This file also contains logic used to compute the closure of filename
** changes that have occurred across multiple check-ins.
*/
#include "config.h"
#include "bisect.h"
#include <assert.h>

/* Nodes for the shortest path algorithm.
*/
typedef struct BisectNode BisectNode;
struct BisectNode {
  int rid;                 /* ID for this node */
  int fromIsParent;        /* True if pFrom is the parent of rid */
  BisectNode *pFrom;       /* Node we came from */
  union {
    BisectNode *pPeer;       /* List of nodes of the same generation */
    BisectNode *pTo;         /* Next on path from beginning to end */
  } u;
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
  BisectNode *pStart;     /* Earliest node (bad) */
  BisectNode *pEnd;       /* Most recent (good) */
} bisect;

/*
** Create a new node
*/
static BisectNode *bisect_new_node(int rid, BisectNode *pFrom, int isParent){
  BisectNode *p;

  p = fossil_malloc( sizeof(*p) );
  p->rid = rid;
  p->fromIsParent = isParent;
  p->pFrom = pFrom;
  p->u.pPeer = bisect.pCurrent;
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
**
** If directOnly is true, then use only the "primary" links from parent to
** child.  In other words, ignore merges.
*/
static BisectNode *bisect_shortest_path(int iFrom, int iTo, int directOnly){
  Stmt s;
  BisectNode *pPrev;
  BisectNode *p;

  bisect_reset();
  bisect.pStart = bisect_new_node(iFrom, 0, 0);
  if( iTo==iFrom ) return bisect.pStart;
  if( directOnly ){
    db_prepare(&s, 
        "SELECT cid, 1 FROM plink WHERE pid=:pid AND isprim "
        "UNION ALL "
        "SELECT pid, 0 FROM plink WHERE cid=:pid AND isprim"
    );
  }else{
    db_prepare(&s, 
        "SELECT cid, 1 FROM plink WHERE pid=:pid "
        "UNION ALL "
        "SELECT pid, 0 FROM plink WHERE cid=:pid"
    );
  }
  while( bisect.pCurrent ){
    bisect.nStep++;
    pPrev = bisect.pCurrent;
    bisect.pCurrent = 0;
    while( pPrev ){
      db_bind_int(&s, ":pid", pPrev->rid);
      while( db_step(&s)==SQLITE_ROW ){
        int cid = db_column_int(&s, 0);
        int isParent = db_column_int(&s, 1);
        if( bag_find(&bisect.seen, cid) ) continue;
        p = bisect_new_node(cid, pPrev, isParent);
        if( cid==iTo ){
          db_finalize(&s);
          return p;
        }
      }
      db_reset(&s);
      pPrev = pPrev->u.pPeer;
    }
  }
  bisect_reset();
  return 0;
}

/*
** Construct the path from bisect.pStart to bisect.pEnd in the u.pTo fields.
*/
static void bisect_reverse_path(void){
  BisectNode *p;
  for(p=bisect.pEnd; p && p->pFrom; p = p->pFrom){
    p->pFrom->u.pTo = p;
  }
  bisect.pEnd->u.pTo = 0;
  assert( p==bisect.pStart );
}

/*
** COMMAND:  test-shortest-path
**
** Usage: %fossil test-shortest-path ?--no-merge? VERSION1 VERSION2
**
** Report the shortest path between two checkins.  If the --no-merge flag
** is used, follow only direct parent-child paths and omit merge links.
*/
void shortest_path_test_cmd(void){
  int iFrom;
  int iTo;
  BisectNode *p;
  int n;
  int directOnly;

  db_find_and_open_repository(0,0);
  directOnly = find_option("no-merge",0,0)!=0;
  if( g.argc!=4 ) usage("VERSION1 VERSION2");
  iFrom = name_to_rid(g.argv[2]);
  iTo = name_to_rid(g.argv[3]);
  p = bisect_shortest_path(iFrom, iTo, directOnly);
  if( p==0 ){
    fossil_fatal("no path from %s to %s", g.argv[1], g.argv[2]);
  }
  bisect_reverse_path();
  for(n=1, p=bisect.pStart; p; p=p->u.pTo, n++){
    char *z;
    z = db_text(0,
      "SELECT substr(uuid,1,12) || ' ' || datetime(mtime)"
      "  FROM blob, event"
      " WHERE blob.rid=%d AND event.objid=%d AND event.type='ci'",
      p->rid, p->rid);
    printf("%4d: %s", n, z);
    fossil_free(z);
    if( p->u.pTo ){
      printf(" is a %s of\n", p->u.pTo->fromIsParent ? "parent" : "child");
    }else{
      printf("\n");
    }
  }
}

/*
** Find the shortest path between bad and good.
*/
static BisectNode *bisect_path(void){
  BisectNode *p;
  bisect.bad = db_lget_int("bisect-bad", 0);
  if( bisect.bad==0 ){
    bisect.bad = db_int(0, "SELECT cid FROM plink ORDER BY mtime DESC LIMIT 1");
    db_lset_int("bisect-bad", bisect.bad);
  }
  bisect.good = db_lget_int("bisect-good", 0);
  if( bisect.good==0 ){
    bisect.good = db_int(0,"SELECT pid FROM plink ORDER BY mtime LIMIT 1");
    db_lset_int("bisect-good", bisect.good);
  }
  p = bisect_shortest_path(bisect.good, bisect.bad, 0);
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
  if( n==0 ) zCmd = "-";
  if( memcmp(zCmd, "bad", n)==0 ){
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
  }else if( memcmp(zCmd, "next", n)==0 ){
    BisectNode *p;
    int n;
    bisect_path();
    if( bisect.nStep<2 ){
      fossil_fatal("bisect is done - there are no more intermediate versions");
    }
    for(p=bisect.pEnd, n=0; p && n<bisect.nStep/2; p=p->pFrom, n++){}
    g.argv[1] = "update";
    g.argv[2] = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", p->rid);
    g.argc = 3;
    g.fNoSync = 1;
    update_cmd();
  }else if( memcmp(zCmd, "reset", n)==0 ){
    db_multi_exec(
      "REPLACE INTO vvar(name, value) "
      "  SELECT 'bisect-good', pid FROM plink ORDER BY mtime LIMIT 1;"
      "REPLACE INTO vvar(name, value) "
      "  SELECT 'bisect-bad', cid FROM plink ORDER BY mtime DESC LIMIT 1;"
    );
  }else if( memcmp(zCmd, "vlist", n)==0 ){
    BisectNode *p;
    int vid = db_lget_int("checkout", 0);
    int n;
    Stmt s;
    bisect_path();
    db_prepare(&s, "SELECT substr(blob.uuid,1,20) || ' ' || "
                   "       datetime(event.mtime) FROM blob, event"
                   " WHERE blob.rid=:rid AND event.objid=:rid"
                   "   AND event.type='ci'");
    for(p=bisect.pEnd, n=0; p; p=p->pFrom, n++){
      const char *z;
      db_bind_int(&s, ":rid", p->rid);
      if( db_step(&s)==SQLITE_ROW ){
        z = db_column_text(&s, 0);
        printf("%s", z);
        if( p->rid==bisect.good ) printf(" GOOD");
        if( p->rid==bisect.bad ) printf(" BAD");
        if( p->rid==vid ) printf(" CURRENT");
        if( bisect.nStep>1 && n==bisect.nStep/2 ) printf(" NEXT");
        printf("\n");
      }
      db_reset(&s);
    }
    db_finalize(&s);
  }else{
    usage("bad|good|next|reset|vlist ...");
  }
}

/*
** A record of a file rename operation.
*/
typedef struct NameChange NameChange;
struct NameChange {
  int origName;        /* Original name of file */
  int curName;         /* Current name of the file */
  int newName;         /* Name of file in next version */
  NameChange *pNext;   /* List of all name changes */
};

/*
** Compute all file name changes that occur going from checkin iFrom
** to checkin iTo.
**
** The number of name changes is written into *pnChng.  For each name
** change, two integers are allocated for *piChng.  The first is the 
** filename.fnid for the original name and the second is for new name.
** Space to hold *piChng is obtained from fossil_malloc() and should
** be released by the caller.
**
** This routine really has nothing to do with bisection.  It is located
** in this bisect.c module in order to leverage some of the bisect
** infrastructure.
*/
void find_filename_changes(
  int iFrom,
  int iTo,
  int *pnChng,
  int **aiChng
){
  BisectNode *p;           /* For looping over path from iFrom to iTo */
  NameChange *pAll = 0;    /* List of all name changes seen so far */
  NameChange *pChng;       /* For looping through the name change list */
  int nChng = 0;           /* Number of files whose names have changed */
  int *aChng;              /* Two integers per name change */
  int i;                   /* Loop counter */
  Stmt q1;                 /* Query of name changes */

  *pnChng = 0;
  *aiChng = 0;
  bisect_reset();
  p = bisect_shortest_path(iFrom, iTo, 1);
  if( p==0 ) return;
  bisect_reverse_path();
  db_prepare(&q1,
     "SELECT pfnid, fnid FROM mlink WHERE mid=:mid AND pfnid>0"
  );
  for(p=bisect.pStart; p; p=p->u.pTo){
    int fnid, pfnid;
    if( !p->fromIsParent && (p->u.pTo==0 || p->u.pTo->fromIsParent) ){
      /* Skip nodes where the parent is not on the path */
      continue;
    }
    db_bind_int(&q1, ":mid", p->rid);
    while( db_step(&q1)==SQLITE_ROW ){
      if( p->fromIsParent ){
        fnid = db_column_int(&q1, 1);
        pfnid = db_column_int(&q1, 0);
      }else{
        fnid = db_column_int(&q1, 0);
        pfnid = db_column_int(&q1, 1);
      }
      for(pChng=pAll; pChng; pChng=pChng->pNext){
        if( pChng->curName==pfnid ){
          pChng->newName = fnid;
          break;
        }
      }
      if( pChng==0 ){
        pChng = fossil_malloc( sizeof(*pChng) );
        pChng->pNext = pAll;
        pAll = pChng;
        pChng->origName = pfnid;
        pChng->curName = pfnid;
        pChng->newName = fnid;
        nChng++;
      }
    }
    for(pChng=pAll; pChng; pChng=pChng->pNext) pChng->curName = pChng->newName;
    db_reset(&q1);
  }
  db_finalize(&q1);
  if( nChng ){
    *pnChng = nChng;
    aChng = *aiChng = fossil_malloc( nChng*2*sizeof(int) );
    for(pChng=pAll, i=0; pChng; pChng=pChng->pNext, i+=2){
      aChng[i] = pChng->origName;
      aChng[i+1] = pChng->newName;
    }
    while( pAll ){
      pChng = pAll;
      pAll = pAll->pNext;
      fossil_free(pChng);
    }
  }
}

/*
** COMMAND: test-name-changes
**
** Usage: %fossil test-name-changes VERSION1 VERSION2
**
** Show all filename changes that occur going from VERSION1 to VERSION2
*/
void test_name_change(void){
  int iFrom;
  int iTo;
  int *aChng;
  int nChng;
  int i;

  db_find_and_open_repository(0,0);
  if( g.argc!=4 ) usage("VERSION1 VERSION2");
  iFrom = name_to_rid(g.argv[2]);
  iTo = name_to_rid(g.argv[3]);
  find_filename_changes(iFrom, iTo, &nChng, &aChng);
  for(i=0; i<nChng; i++){
    char *zFrom, *zTo;

    zFrom = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2]);
    zTo = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2+1]);
    printf("[%s] -> [%s]\n", zFrom, zTo);
    fossil_free(zFrom);
    fossil_free(zTo);
  }
  fossil_free(aChng);
}
