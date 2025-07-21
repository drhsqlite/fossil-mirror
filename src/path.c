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
#include <math.h>

#if INTERFACE
/* Nodes for the paths through the DAG.
*/
struct PathNode {
  int rid;                 /* ID for this node */
  u8 fromIsParent;         /* True if pFrom is the parent of rid */
  u8 isPrim;               /* True if primary side of common ancestor */
  u8 isHidden;             /* Abbreviate output in "fossil bisect ls" */
  char *zBranch;           /* Branch name for this node.  Might be NULL */
  double mtime;            /* Date/time of this check-in */
  PathNode *pFrom;         /* Node we came from */
  union {
    double rCost;          /* Cost of getting to this node from pStart */
    PathNode *pTo;         /* Next on path from beginning to end */
  } u;
  PathNode *pAll;          /* List of all nodes */
};
#endif

/*
** Local variables for this module
*/
static struct {
  PQueue pending;       /* Nodes pending review for inclusion in the graph */
  PathNode *pAll;       /* All nodes */
  int nStep;            /* Number of steps from first to last */
  int nNotHidden;       /* Number of steps not counting hidden nodes */
  int brCost;           /* Extra cost for moving to a different branch */
  int revCost;          /* Extra cost for changing directions */
  PathNode *pStart;     /* Earliest node */
  PathNode *pEnd;       /* Most recent */
} path;
static int path_debug = 0;  /* Flag to enable debugging */

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
** Return the number of non-hidden steps in the computed path.
*/
int path_length_not_hidden(void){ return path.nNotHidden; }

/*
** Used for debugging only.
**
** Given a RID, return the ISO date/time string and branch for the
** corresponding check-in.  Memory is held locally and is overwritten
** with each call.
*/
char *path_rid_desc(int rid){
  static Stmt q;
  static char *zDesc = 0;
  db_static_prepare(&q, 
    "SELECT concat(strftime('%%Y%%m%%d%%H%%M',event.mtime),'/',value)"
    "  FROM event, tagxref"
    " WHERE event.objid=:rid"
    "   AND tagxref.rid=:rid"
    "   AND tagxref.tagid=%d"
    "   AND tagxref.tagtype>0",
    TAG_BRANCH
  );
  fossil_free(zDesc);
  db_bind_int(&q, ":rid", rid);
  if( db_step(&q)==SQLITE_ROW ){
    zDesc = fossil_strdup(db_column_text(&q,0));
  }
  db_reset(&q);
  return zDesc ? zDesc : "???";
}

/*
** Create a new node and insert it into the path.pending queue.
*/
static PathNode *path_new_node(int rid, PathNode *pFrom, int isParent){
  PathNode *p;

  p = fossil_malloc( sizeof(*p) );
  memset(p, 0, sizeof(*p));
  p->pAll = path.pAll;
  path.pAll = p;
  p->rid = rid;
  p->fromIsParent = isParent;
  p->pFrom = pFrom;
  p->u.rCost = pFrom ? pFrom->u.rCost : 0.0;
  if( path.brCost ){
    p->zBranch = branch_of_rid(rid);
    p->mtime = mtime_of_rid(rid, 0.0);
    if( pFrom ){
      p->u.rCost +=  fabs(pFrom->mtime - p->mtime);
      if( fossil_strcmp(p->zBranch, pFrom->zBranch)!=0 ){
        p->u.rCost += path.brCost;
      }
    }
  }else{
    /* When brCost==0, we try to minimize the number of nodes
    ** along the path.  The cost is just the number of nodes back
    ** to the start.  We do not need to know the branch name nor
    ** the mtime */
    p->u.rCost += 1.0;
  }
  if( path_debug ){
    fossil_print("PUSH %-50s cost = %g\n", path_rid_desc(p->rid), p->u.rCost);
  }
  pqueuex_insert_ptr(&path.pending, (void*)p, p->u.rCost);
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
    fossil_free(p->zBranch);
    fossil_free(p);
  }
  pqueuex_clear(&path.pending);
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
  int oneWayOnly,     /* Parent->child only if true */
  Bag *pHidden,       /* Hidden nodes */
  int branchCost      /* Add extra cost to changing branches */
){
  Stmt s;
  Bag seen;
  PathNode *p;

  path_reset();
  path.brCost = branchCost;
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
        "SELECT pid, 0 FROM plink WHERE :back AND cid=:pid AND isprim"
    );
  }else{
    db_prepare(&s,
        "SELECT cid, 1 FROM plink WHERE pid=:pid "
        "UNION ALL "
        "SELECT pid, 0 FROM plink WHERE :back AND cid=:pid"
    );
  }
  bag_init(&seen);
  while( (p = pqueuex_extract_ptr(&path.pending))!=0 ){
    if( path_debug ){
      printf("PULL %s %g\n", path_rid_desc(p->rid), p->u.rCost);
    }
    if( p->rid==iTo ){
      db_finalize(&s);
      path.pEnd = p;
      path_reverse_path();
      for(p=path.pStart->u.pTo; p; p=p->u.pTo ){
        if( !p->isHidden ) path.nNotHidden++;
      }
      return path.pStart;
    }
    if( bag_find(&seen, p->rid) ) continue;
    bag_insert(&seen, p->rid);
    db_bind_int(&s, ":pid", p->rid);
    if( !oneWayOnly ) db_bind_int(&s, ":back", !p->fromIsParent);
    while( db_step(&s)==SQLITE_ROW ){
      int cid = db_column_int(&s, 0);
      int isParent = db_column_int(&s, 1);
      PathNode *pNew;
      if( bag_find(&seen, cid) ) continue;
      pNew = path_new_node(cid, p, isParent);
      if( pHidden && bag_find(pHidden,cid) ) pNew->isHidden = 1;
    }
    db_reset(&s);
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
  if( path.nNotHidden<2 ) return 0;
  for(p=path.pEnd, i=0; p && (p->isHidden || i<path.nNotHidden/2); p=p->pFrom){
    if( !p->isHidden ) i++;
  }
  return p;
}

/*
** Find the next most recent node on a path.
*/
PathNode *path_next(void){
  PathNode *p;
  p = path.pStart;
  if( p ) p = p->u.pTo;
  return p;
}

/*
** Return the branch for a path node.
**
** Storage space is managed by the path subsystem.  The returned value
** is valid until the path is reset.
*/
const char *path_branch(PathNode *p){
  if( p==0 ) return 0;
  if( p->zBranch==0 ) p->zBranch = branch_of_rid(p->rid);
  return p->zBranch;
}

/*
** Return an estimate of the number of comparisons remaining in order
** to bisect path.  This is based on the log2() of path.nStep.
*/
int path_search_depth(void){
  int i, j;
  for(i=0, j=1; j<path.nNotHidden; i++, j+=j){}
  return i;
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
  pPath = path_shortest(cid, origid, 1, 0, 0, 0);
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
** Usage: %fossil test-shortest-path [OPTIONS] VERSION1 VERSION2
**
** Report the shortest path between two check-ins.  Options:
**
**    --branch-cost N    Additional cost N for changing branches
**    --debug            Show debugging output
**    --one-way          One-way forwards in time, parent->child only
**    --no-merge         Follow only direct parent-child paths and omit
**                       merge links.
*/
void shortest_path_test_cmd(void){
  int iFrom;
  int iTo;
  PathNode *p;
  int n;
  int directOnly;
  int oneWay;
  const char *zBrCost;

  db_find_and_open_repository(0,0);
  directOnly = find_option("no-merge",0,0)!=0;
  oneWay = find_option("one-way",0,0)!=0;
  zBrCost = find_option("branch-cost",0,1);
  if( find_option("debug",0,0)!=0 ) path_debug = 1;
  if( g.argc!=4 ) usage("VERSION1 VERSION2");
  iFrom = name_to_rid(g.argv[2]);
  iTo = name_to_rid(g.argv[3]);
  p = path_shortest(iFrom, iTo, directOnly, oneWay, 0,
                    zBrCost ? atoi(zBrCost) : 0);
  if( p==0 ){
    fossil_fatal("no path from %s to %s", g.argv[1], g.argv[2]);
  }
  for(n=1, p=path.pStart; p; p=p->u.pTo, n++){
    fossil_print("%4d: %s\n", n, path_rid_desc(p->rid));
  }
  path_debug = 0;
}

/*
** Find the closest common ancestor of two nodes.  "Closest" means the
** fewest number of arcs.
*/
int path_common_ancestor(int iMe, int iYou){
  Stmt s;
  PathNode *pThis;
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
  while( (pThis = pqueuex_extract_ptr(&path.pending))!=0 ){
    db_bind_int(&s, ":cid", pThis->rid);
    while( db_step(&s)==SQLITE_ROW ){
      int pid = db_column_int(&s, 0);
      if( bag_find(pThis->isPrim ? &you : &me, pid) ){
        /* pid is the common ancestor */
        PathNode *pNext;
        for(p=path.pAll; p && p->rid!=pid; p=p->pAll){}
        assert( p!=0 );
        pNext = p;
        while( pNext ){
          pNext = p->pFrom;
          p->pFrom = pThis;
          pThis = p;
          p = pNext;
        }
        if( pThis==path.pStart ) path.pStart = path.pEnd;
        path.pEnd = pThis;
        path_reverse_path();
        db_finalize(&s);
        return pid;
      }else if( bag_find(pThis->isPrim ? &me : &you, pid) ){
        /* pid is just an alternative path to a node we've already visited */
        continue;
      }
      p = path_new_node(pid, pThis, 0);
      p->isPrim = pThis->isPrim;
      bag_insert(pThis->isPrim ? &me : &you, pid);
    }
    db_reset(&s);
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
  int revOK,               /* OK to move backwards (child->parent) if true */
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
  p = path_shortest(iFrom, iTo, 1, revOK==0, 0, 0);
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
    if( zDebug ){
      fossil_print("%s check-in %.16z %z rid %d\n",
         zDebug,
         db_text(0, "SELECT uuid FROM blob WHERE rid=%d", p->rid),
         db_text(0, "SELECT date(mtime) FROM event WHERE objid=%d", p->rid),
         p->rid
      );
    }
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
        fossil_print("%s %d[%z] -> %d[%z]\n",
           zDebug,
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
  path_reset();
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
  int revOK = 0;

  db_find_and_open_repository(0,0);
  zDebug = find_option("debug",0,0)!=0 ? "debug" : 0;
  revOK = find_option("bidirectional",0,0)!=0;
  if( g.argc<4 ) usage("VERSION1 VERSION2");
  while( g.argc>=4 ){
    iFrom = name_to_rid(g.argv[2]);
    iTo = name_to_rid(g.argv[3]);
    find_filename_changes(iFrom, iTo, revOK, &nChng, &aChng, zDebug);
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
@ CREATE TEMP TABLE renames AS
@ SELECT
@     datetime(event.mtime) AS date,
@     F.name AS old_name,
@     T.name AS new_name,
@     blob.uuid AS checkin
@   FROM mlink, filename F, filename T, event, blob
@  WHERE coalesce(mlink.pfnid,0)!=0 AND mlink.pfnid!=mlink.fnid
@    AND F.fnid=mlink.pfnid
@    AND T.fnid=mlink.fnid
@    AND event.objid=mlink.mid
@    AND event.type='ci'
@    AND blob.rid=mlink.mid;
;

/* Query to extract distinct rename operations */
static const char zDistinctRenameQuery[] =
@ CREATE TEMP TABLE renames AS
@ SELECT
@     min(datetime(event.mtime)) AS date,
@     F.name AS old_name,
@     T.name AS new_name,
@     blob.uuid AS checkin
@   FROM mlink, filename F, filename T, event, blob
@  WHERE coalesce(mlink.pfnid,0)!=0 AND mlink.pfnid!=mlink.fnid
@    AND F.fnid=mlink.pfnid
@    AND T.fnid=mlink.fnid
@    AND event.objid=mlink.mid
@    AND event.type='ci'
@    AND blob.rid=mlink.mid
@  GROUP BY 2, 3;
;

/*
** WEBPAGE: test-rename-list
**
** Print a list of all file rename operations throughout history.
** This page is intended for testing purposes only and may change
** or be discontinued without notice.
*/
void test_rename_list_page(void){
  Stmt q;
  int nRename;
  int nCheckin;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_set_current_feature("test");
  if( P("all")!=0 ){
    style_header("List Of All Filename Changes");
    db_multi_exec("%s", zRenameQuery/*safe-for-%s*/);
    style_submenu_element("Distinct", "%R/test-rename-list");
  }else{
    style_header("List Of Distinct Filename Changes");
    db_multi_exec("%s", zDistinctRenameQuery/*safe-for-%s*/);
    style_submenu_element("All", "%R/test-rename-list?all");
  }
  nRename = db_int(0, "SELECT count(*) FROM renames;");
  nCheckin = db_int(0, "SELECT count(DISTINCT checkin) FROM renames;");
  db_prepare(&q, "SELECT date, old_name, new_name, checkin FROM renames"
                 " ORDER BY date DESC, old_name ASC");
  @ <h1>%d(nRename) filename changes in %d(nCheckin) check-ins</h1>
  @ <table class='sortable' data-column-types='tttt' data-init-sort='1'\
  @  border="1" cellpadding="2" cellspacing="0">
  @ <thead><tr><th>Date &amp; Time</th>
  @ <th>Old Name</th>
  @ <th>New Name</th>
  @ <th>Check-in</th></tr></thead><tbody>
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
  @ </tbody></table>
  db_finalize(&q);
  style_table_sorter();
  style_finish_page();
}
