/*
** Copyright (c) 2011 D. Richard Hipp
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
** This file contains code used to trace paths of through the
** directed acyclic graph (DAG) of checkins.
*/
#include "config.h"
#include "path.h"
#include <assert.h>

#if INTERFACE
/* Nodes for the paths through the DAG.
*/
struct PathNode {
  int rid;                 /* ID for this node */
  int fromIsParent;        /* True if pFrom is the parent of rid */
  PathNode *pFrom;         /* Node we came from */
  union {
    PathNode *pPeer;       /* List of nodes of the same generation */
    PathNode *pTo;         /* Next on path from beginning to end */
  } u;
  PathNode *pAll;        /* List of all nodes */
};
#endif

/*
** Local variables for this module
*/
static struct {
  PathNode *pCurrent;   /* Current generation of nodes */
  PathNode *pAll;       /* All nodes */
  Bag seen;             /* Nodes seen before */
  int nStep;            /* Number of steps from first to last */
  PathNode *pStart;     /* Earliest node */
  PathNode *pEnd;       /* Most recent */
} path;

/*
** Return the first (last) element of the computed path.
*/
PathNode *path_first(void){ return path.pStart; }
PathNode *path_last(void){ return path.pEnd; }

/*
** Return the number of steps in the computed path.
*/
int path_length(void){ return path.nStep; }

/*
** Create a new node
*/
static PathNode *path_new_node(int rid, PathNode *pFrom, int isParent){
  PathNode *p;

  p = fossil_malloc( sizeof(*p) );
  p->rid = rid;
  p->fromIsParent = isParent;
  p->pFrom = pFrom;
  p->u.pPeer = path.pCurrent;
  path.pCurrent = p;
  p->pAll = path.pAll;
  path.pAll = p;
  path.pEnd = p;
  bag_insert(&path.seen, rid);
  return p;
}

/*
** Reset memory used by the shortest path algorithm.
*/
void path_reset(void){
  PathNode *p;
  while( path.pAll ){
    p = path.pAll;
    path.pAll = p->pAll;
    fossil_free(p);
  }
  bag_clear(&path.seen);
  path.pCurrent = 0;
  path.pAll = 0;
  path.pEnd = 0;
  path.nStep = 0;
}

/*
** Compute the shortest path from iFrom to iTo
**
** If directOnly is true, then use only the "primary" links from parent to
** child.  In other words, ignore merges.
*/
PathNode *path_shortest(int iFrom, int iTo, int directOnly){
  Stmt s;
  PathNode *pPrev;
  PathNode *p;

  path_reset();
  path.pStart = path_new_node(iFrom, 0, 0);
  if( iTo==iFrom ) return path.pStart;
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
  while( path.pCurrent ){
    path.nStep++;
    pPrev = path.pCurrent;
    path.pCurrent = 0;
    while( pPrev ){
      db_bind_int(&s, ":pid", pPrev->rid);
      while( db_step(&s)==SQLITE_ROW ){
        int cid = db_column_int(&s, 0);
        int isParent = db_column_int(&s, 1);
        if( bag_find(&path.seen, cid) ) continue;
        p = path_new_node(cid, pPrev, isParent);
        if( cid==iTo ){
          db_finalize(&s);
          return p;
        }
      }
      db_reset(&s);
      pPrev = pPrev->u.pPeer;
    }
  }
  path_reset();
  return 0;
}

/*
** Construct the path from path.pStart to path.pEnd in the u.pTo fields.
*/
PathNode *path_reverse_path(void){
  PathNode *p;
  for(p=path.pEnd; p && p->pFrom; p = p->pFrom){
    p->pFrom->u.pTo = p;
  }
  path.pEnd->u.pTo = 0;
  assert( p==path.pStart );
  return p;
}

/*
** Find the mid-point of the path.  If the path contains fewer than
** 2 steps, return 0.
*/
PathNode *path_midpoint(void){
  PathNode *p;
  int i;
  if( path.nStep<2 ) return 0;
  for(p=path.pEnd, i=0; p && i<path.nStep/2; p=p->pFrom, i++){}
  return p;
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
  PathNode *p;
  int n;
  int directOnly;

  db_find_and_open_repository(0,0);
  directOnly = find_option("no-merge",0,0)!=0;
  if( g.argc!=4 ) usage("VERSION1 VERSION2");
  iFrom = name_to_rid(g.argv[2]);
  iTo = name_to_rid(g.argv[3]);
  p = path_shortest(iFrom, iTo, directOnly);
  if( p==0 ){
    fossil_fatal("no path from %s to %s", g.argv[1], g.argv[2]);
  }
  path_reverse_path();
  for(n=1, p=path.pStart; p; p=p->u.pTo, n++){
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
** This routine really has nothing to do with pathion.  It is located
** in this path.c module in order to leverage some of the path
** infrastructure.
*/
void find_filename_changes(
  int iFrom,
  int iTo,
  int *pnChng,
  int **aiChng
){
  PathNode *p;           /* For looping over path from iFrom to iTo */
  NameChange *pAll = 0;    /* List of all name changes seen so far */
  NameChange *pChng;       /* For looping through the name change list */
  int nChng = 0;           /* Number of files whose names have changed */
  int *aChng;              /* Two integers per name change */
  int i;                   /* Loop counter */
  Stmt q1;                 /* Query of name changes */

  *pnChng = 0;
  *aiChng = 0;
  path_reset();
  p = path_shortest(iFrom, iTo, 0);
  if( p==0 ) return;
  path_reverse_path();
  db_prepare(&q1,
     "SELECT pfnid, fnid FROM mlink WHERE mid=:mid AND pfnid>0"
  );
  for(p=path.pStart; p; p=p->u.pTo){
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
