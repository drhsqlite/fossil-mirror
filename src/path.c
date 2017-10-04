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
** directed acyclic graph (DAG) of check-ins.
*/
#include "config.h"
#include "path.h"
#include <assert.h>

#if INTERFACE
/* Nodes for the paths through the DAG.
*/
struct PathNode {
  int rid;                 /* ID for this node */
  u8 fromIsParent;         /* True if pFrom is the parent of rid */
  u8 isPrim;               /* True if primary side of common ancestor */
  u8 isHidden;             /* Abbreviate output in "fossil bisect ls" */
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
  memset(p, 0, sizeof(*p));
  p->rid = rid;
  p->fromIsParent = isParent;
  p->pFrom = pFrom;
  p->u.pPeer = path.pCurrent;
  path.pCurrent = p;
  p->pAll = path.pAll;
  path.pAll = p;
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
  memset(&path, 0, sizeof(path));
}

/*
** Construct the path from path.pStart to path.pEnd in the u.pTo fields.
*/
static void path_reverse_path(void){
  PathNode *p;
  assert( path.pEnd!=0 );
  for(p=path.pEnd; p && p->pFrom; p = p->pFrom){
    p->pFrom->u.pTo = p;
  }
  path.pEnd->u.pTo = 0;
  assert( p==path.pStart );
}

/*
** Compute the shortest path from iFrom to iTo
**
** If directOnly is true, then use only the "primary" links from parent to
** child.  In other words, ignore merges.
**
** Return a pointer to the beginning of the path (the iFrom node).
** Elements of the path can be traversed by following the PathNode.u.pTo
** pointer chain.
**
** Return NULL if no path is found.
*/
PathNode *path_shortest(
  int iFrom,          /* Path starts here */
  int iTo,            /* Path ends here */
  int directOnly,     /* No merge links if true */
  int oneWayOnly      /* Parent->child only if true */
){
  Stmt s;
  PathNode *pPrev;
  PathNode *p;

  path_reset();
  path.pStart = path_new_node(iFrom, 0, 0);
  if( iTo==iFrom ){
    path.pEnd = path.pStart;
    return path.pStart;
  }
  if( oneWayOnly && directOnly ){
    db_prepare(&s,
        "SELECT cid, 1 FROM plink WHERE pid=:pid AND isprim"
    );
  }else if( oneWayOnly ){
    db_prepare(&s,
        "SELECT cid, 1 FROM plink WHERE pid=:pid "
    );
  }else if( directOnly ){
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
          path.pEnd = p;
          path_reverse_path();
          return path.pStart;
        }
      }
      db_reset(&s);
      pPrev = pPrev->u.pPeer;
    }
  }
  db_finalize(&s);
  path_reset();
  return 0;
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
** Compute the shortest path between two check-ins and then transfer
** that path into the "ancestor" table.  This is a utility used by
** both /annotate and /finfo.  See also: compute_direct_ancestors().
*/
void path_shortest_stored_in_ancestor_table(
  int origid,     /* RID for check-in at start of the path */
  int cid         /* RID for check-in at the end of the path */
){
  PathNode *pPath;
  int gen = 0;
  Stmt ins;
  pPath = path_shortest(cid, origid, 1, 0);
  db_multi_exec(
    "CREATE TEMP TABLE IF NOT EXISTS ancestor("
    "  rid INT UNIQUE,"
    "  generation INTEGER PRIMARY KEY"
    ");"
    "DELETE FROM ancestor;"
  );
  db_prepare(&ins, "INSERT INTO ancestor(rid, generation) VALUES(:rid,:gen)");
  while( pPath ){
    db_bind_int(&ins, ":rid", pPath->rid);
    db_bind_int(&ins, ":gen", ++gen);
    db_step(&ins);
    db_reset(&ins);
    pPath = pPath->u.pTo;
  }
  db_finalize(&ins);
  path_reset();
}

/*
** COMMAND: test-shortest-path
**
** Usage: %fossil test-shortest-path ?--no-merge? VERSION1 VERSION2
**
** Report the shortest path between two check-ins.  If the --no-merge flag
** is used, follow only direct parent-child paths and omit merge links.
*/
void shortest_path_test_cmd(void){
  int iFrom;
  int iTo;
  PathNode *p;
  int n;
  int directOnly;
  int oneWay;

  db_find_and_open_repository(0,0);
  directOnly = find_option("no-merge",0,0)!=0;
  oneWay = find_option("one-way",0,0)!=0;
  if( g.argc!=4 ) usage("VERSION1 VERSION2");
  iFrom = name_to_rid(g.argv[2]);
  iTo = name_to_rid(g.argv[3]);
  p = path_shortest(iFrom, iTo, directOnly, oneWay);
  if( p==0 ){
    fossil_fatal("no path from %s to %s", g.argv[1], g.argv[2]);
  }
  for(n=1, p=path.pStart; p; p=p->u.pTo, n++){
    char *z;
    z = db_text(0,
      "SELECT substr(uuid,1,12) || ' ' || datetime(mtime)"
      "  FROM blob, event"
      " WHERE blob.rid=%d AND event.objid=%d AND event.type='ci'",
      p->rid, p->rid);
    fossil_print("%4d: %5d %s", n, p->rid, z);
    fossil_free(z);
    if( p->u.pTo ){
      fossil_print(" is a %s of\n",
                   p->u.pTo->fromIsParent ? "parent" : "child");
    }else{
      fossil_print("\n");
    }
  }
}

/*
** Find the closest common ancestor of two nodes.  "Closest" means the
** fewest number of arcs.
*/
int path_common_ancestor(int iMe, int iYou){
  Stmt s;
  PathNode *pPrev;
  PathNode *p;
  Bag me, you;

  if( iMe==iYou ) return iMe;
  if( iMe==0 || iYou==0 ) return 0;
  path_reset();
  path.pStart = path_new_node(iMe, 0, 0);
  path.pStart->isPrim = 1;
  path.pEnd = path_new_node(iYou, 0, 0);
  db_prepare(&s, "SELECT pid FROM plink WHERE cid=:cid");
  bag_init(&me);
  bag_insert(&me, iMe);
  bag_init(&you);
  bag_insert(&you, iYou);
  while( path.pCurrent ){
    pPrev = path.pCurrent;
    path.pCurrent = 0;
    while( pPrev ){
      db_bind_int(&s, ":cid", pPrev->rid);
      while( db_step(&s)==SQLITE_ROW ){
        int pid = db_column_int(&s, 0);
        if( bag_find(pPrev->isPrim ? &you : &me, pid) ){
          /* pid is the common ancestor */
          PathNode *pNext;
          for(p=path.pAll; p && p->rid!=pid; p=p->pAll){}
          assert( p!=0 );
          pNext = p;
          while( pNext ){
            pNext = p->pFrom;
            p->pFrom = pPrev;
            pPrev = p;
            p = pNext;
          }
          if( pPrev==path.pStart ) path.pStart = path.pEnd;
          path.pEnd = pPrev;
          path_reverse_path();
          db_finalize(&s);
          return pid;
        }else if( bag_find(&path.seen, pid) ){
          /* pid is just an alternative path on one of the legs */
          continue;
        }
        p = path_new_node(pid, pPrev, 0);
        p->isPrim = pPrev->isPrim;
        bag_insert(pPrev->isPrim ? &me : &you, pid);
      }
      db_reset(&s);
      pPrev = pPrev->u.pPeer;
    }
  }
  db_finalize(&s);
  path_reset();
  return 0;
}

/*
** COMMAND: test-ancestor-path
**
** Usage: %fossil test-ancestor-path VERSION1 VERSION2
**
** Report the path from VERSION1 to VERSION2 through their most recent
** common ancestor.
*/
void ancestor_path_test_cmd(void){
  int iFrom;
  int iTo;
  int iPivot;
  PathNode *p;
  int n;

  db_find_and_open_repository(0,0);
  if( g.argc!=4 ) usage("VERSION1 VERSION2");
  iFrom = name_to_rid(g.argv[2]);
  iTo = name_to_rid(g.argv[3]);
  iPivot = path_common_ancestor(iFrom, iTo);
  for(n=1, p=path.pStart; p; p=p->u.pTo, n++){
    char *z;
    z = db_text(0,
      "SELECT substr(uuid,1,12) || ' ' || datetime(mtime)"
      "  FROM blob, event"
      " WHERE blob.rid=%d AND event.objid=%d AND event.type='ci'",
      p->rid, p->rid);
    fossil_print("%4d: %5d %s", n, p->rid, z);
    fossil_free(z);
    if( p->rid==iFrom ) fossil_print(" VERSION1");
    if( p->rid==iTo ) fossil_print(" VERSION2");
    if( p->rid==iPivot ) fossil_print(" PIVOT");
    fossil_print("\n");
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
** Compute all file name changes that occur going from check-in iFrom
** to check-in iTo.
**
** The number of name changes is written into *pnChng.  For each name
** change, two integers are allocated for *piChng.  The first is the
** filename.fnid for the original name as seen in check-in iFrom and
** the second is for new name as it is used in check-in iTo.
**
** Space to hold *piChng is obtained from fossil_malloc() and should
** be released by the caller.
**
** This routine really has nothing to do with path.  It is located
** in this path.c module in order to leverage some of the path
** infrastructure.
*/
void find_filename_changes(
  int iFrom,               /* Ancestor check-in */
  int iTo,                 /* Recent check-in */
  int revOk,               /* Ok to move backwards (child->parent) if true */
  int *pnChng,             /* Number of name changes along the path */
  int **aiChng,            /* Name changes */
  const char *zDebug       /* Generate trace output if no NULL */
){
  PathNode *p;             /* For looping over path from iFrom to iTo */
  NameChange *pAll = 0;    /* List of all name changes seen so far */
  NameChange *pChng;       /* For looping through the name change list */
  int nChng = 0;           /* Number of files whose names have changed */
  int *aChng;              /* Two integers per name change */
  int i;                   /* Loop counter */
  Stmt q1;                 /* Query of name changes */

  *pnChng = 0;
  *aiChng = 0;
  if(0==iFrom){
    fossil_fatal("Invalid 'from' RID: 0");
  }else if(0==iTo){
    fossil_fatal("Invalid 'to' RID: 0");
  }
  if( iFrom==iTo ) return;
  path_reset();
  p = path_shortest(iFrom, iTo, 1, revOk==0);
  if( p==0 ) return;
  path_reverse_path();
  db_prepare(&q1,
     "SELECT pfnid, fnid FROM mlink"
     " WHERE mid=:mid AND (pfnid>0 OR fid==0)"
     " ORDER BY pfnid"
  );
  for(p=path.pStart; p; p=p->u.pTo){
    int fnid, pfnid;
    if( !p->fromIsParent && (p->u.pTo==0 || p->u.pTo->fromIsParent) ){
      /* Skip nodes where the parent is not on the path */
      continue;
    }
    db_bind_int(&q1, ":mid", p->rid);
    while( db_step(&q1)==SQLITE_ROW ){
      fnid = db_column_int(&q1, 1);
      pfnid = db_column_int(&q1, 0);
      if( pfnid==0 ){
        pfnid = fnid;
        fnid = 0;
      }
      if( !p->fromIsParent ){
        int t = fnid;
        fnid = pfnid;
        pfnid = t;
      }
      if( zDebug ){
        fossil_print("%s at %d%s %.10z: %d[%z] -> %d[%z]\n",
           zDebug, p->rid, p->fromIsParent ? ">" : "<",
           db_text(0, "SELECT uuid FROM blob WHERE rid=%d", p->rid),
           pfnid,
           db_text(0, "SELECT name FROM filename WHERE fnid=%d", pfnid),
           fnid,
           db_text(0, "SELECT name FROM filename WHERE fnid=%d", fnid));
      }
      for(pChng=pAll; pChng; pChng=pChng->pNext){
        if( pChng->curName==pfnid ){
          pChng->newName = fnid;
          break;
        }
      }
      if( pChng==0 && fnid>0 ){
        pChng = fossil_malloc( sizeof(*pChng) );
        pChng->pNext = pAll;
        pAll = pChng;
        pChng->origName = pfnid;
        pChng->curName = pfnid;
        pChng->newName = fnid;
        nChng++;
      }
    }
    for(pChng=pAll; pChng; pChng=pChng->pNext){
      pChng->curName = pChng->newName;
    }
    db_reset(&q1);
  }
  db_finalize(&q1);
  if( nChng ){
    aChng = *aiChng = fossil_malloc( nChng*2*sizeof(int) );
    for(pChng=pAll, i=0; pChng; pChng=pChng->pNext){
      if( pChng->newName==0 ) continue;
      if( pChng->origName==0 ) continue;
      aChng[i] = pChng->origName;
      aChng[i+1] = pChng->newName;
      if( zDebug ){
        fossil_print("%s summary %d[%z] -> %d[%z]\n",
           zDebug,
           aChng[i],
           db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i]),
           aChng[i+1],
           db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i+1]));
      }
      i += 2;
    }
    *pnChng = i/2;
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
** Usage: %fossil test-name-changes [--debug] VERSION1 VERSION2
**
** Show all filename changes that occur going from VERSION1 to VERSION2
*/
void test_name_change(void){
  int iFrom;
  int iTo;
  int *aChng;
  int nChng;
  int i;
  const char *zDebug = 0;
  int revOk = 0;

  db_find_and_open_repository(0,0);
  zDebug = find_option("debug",0,0)!=0 ? "debug" : 0;
  revOk = find_option("bidirectional",0,0)!=0;
  if( g.argc<4 ) usage("VERSION1 VERSION2");
  while( g.argc>=4 ){
    iFrom = name_to_rid(g.argv[2]);
    iTo = name_to_rid(g.argv[3]);
    find_filename_changes(iFrom, iTo, revOk, &nChng, &aChng, zDebug);
    fossil_print("------ Changes for (%d) %s -> (%d) %s\n",
                 iFrom, g.argv[2], iTo, g.argv[3]);
    for(i=0; i<nChng; i++){
      char *zFrom, *zTo;

      zFrom = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2]);
      zTo = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2+1]);
      fossil_print("[%s] -> [%s]\n", zFrom, zTo);
      fossil_free(zFrom);
      fossil_free(zTo);
    }
    fossil_free(aChng);
    g.argv += 2;
    g.argc -= 2;
  }
}

/* Query to extract all rename operations */
static const char zRenameQuery[] =
@ SELECT
@     datetime(event.mtime),
@     F.name AS old_name,
@     T.name AS new_name,
@     blob.uuid
@   FROM mlink, filename F, filename T, event, blob
@  WHERE coalesce(mlink.pfnid,0)!=0 AND mlink.pfnid!=mlink.fnid
@    AND F.fnid=mlink.pfnid
@    AND T.fnid=mlink.fnid
@    AND event.objid=mlink.mid
@    AND event.type='ci'
@    AND blob.rid=mlink.mid
@  ORDER BY 1 DESC, 2;
;

/*
** WEBPAGE: test-rename-list
**
** Print a list of all file rename operations throughout history.
** This page is intended for for testing purposes only and may change
** or be discontinued without notice.
*/
void test_rename_list_page(void){
  Stmt q;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_header("List Of File Name Changes");
  @ <h3>NB: Experimental Page</h3>
  @ <table border="1" width="100%%">
  @ <tr><th>Date &amp; Time</th>
  @ <th>Old Name</th>
  @ <th>New Name</th>
  @ <th>Check-in</th></tr>
  db_prepare(&q, "%s", zRenameQuery/*safe-for-%s*/);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 0);
    const char *zOld = db_column_text(&q, 1);
    const char *zNew = db_column_text(&q, 2);
    const char *zUuid = db_column_text(&q, 3);
    @ <tr>
    @ <td>%z(href("%R/timeline?c=%t",zDate))%s(zDate)</a></td>
    @ <td>%z(href("%R/finfo?name=%t",zOld))%h(zOld)</a></td>
    @ <td>%z(href("%R/finfo?name=%t",zNew))%h(zNew)</a></td>
    @ <td>%z(href("%R/info/%!S",zUuid))%S(zUuid)</a></td></tr>
  }
  @ </table>
  db_finalize(&q);
  style_footer();
}
