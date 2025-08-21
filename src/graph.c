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
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code to compute a revision history graph.
*/
#include "config.h"
#include "graph.h"
#include <assert.h>

/* Notes:
**
** The graph is laid out in 1 or more "rails".  A "rail" is a vertical
** band in the graph in which one can place nodes or arrows connecting
** nodes.  There can be between 1 and GR_MAX_RAIL rails.  If the graph
** is too complex to be displayed in GR_MAX_RAIL rails, it is omitted.
**
** A "riser" is the thick line that comes out of the top of a node and
** goes up to the next node on the branch, or to the top of the screen.
** A "descender" is a thick line that comes out of the bottom of a node
** and proceeds down to the bottom of the page.
**
** A "merge riser" is a thin line going up out of a node to indicate a
** merge or cherrypick.  (Cherrypicks are drawn with thin dashed lines.
** Merges are drawn with thin solid lines.)  A "merge riser" might go
** stright up out of the top of a leaf node, but for non-leaves, they
** go horizontally to their assigned rail first, then up.
**
** Invoke graph_init() to create a new GraphContext object.  Then
** call graph_add_row() to add nodes, one by one, to the graph.
** Nodes must be added in display order, from top to bottom.
** Then invoke graph_render() to run the layout algorithm.  The
** layout algorithm computes which rail each nodes sit on, and
** the rails used for merge arrows.
*/

#if INTERFACE

/*
** The type of integer identifiers for rows of the graph.
**
** For a normal /timeline graph, the identifiers are never that big
** an an ordinary 32-bit int will work fine.  But for the /finfo page,
** the identifier is a combination of the BLOB.RID and the FILENAME.FNID
** values, and so it can become quite large for repos that have both many
** check-ins and many files.  For this reason, we make the identifier
** a 64-bit integer, to dramatically reduce the risk of an overflow.
*/
typedef sqlite3_int64 GraphRowId;

#define GR_MAX_RAIL   64      /* Max number of "rails" to display */

/* The graph appears vertically beside a timeline.  Each row in the
** timeline corresponds to a row in the graph.  GraphRow.idx is 0 for
** the top-most row and increases moving down.  Hence (in the absence of
** time skew) parents have a larger index than their children.
**
** The nParent field is -1 for entires that do not participate in the graph
** but which are included just so that we can capture their background color.
*/
struct GraphRow {
  GraphRowId rid;             /* The rid for the check-in */
  i8 nParent;                 /* Number of parents. */
  i8 nCherrypick;             /* Subset of aParent that are cherrypicks */
  i8 nNonCherrypick;          /* Number of non-cherrypick parents */
  u8 nMergeChild;             /* Number of merge children */
  GraphRowId *aParent;        /* Array of parents.  0 element is primary .*/
  char *zBranch;              /* Branch name */
  char *zBgClr;               /* Background Color */
  char zUuid[HNAME_MAX+1];    /* Check-in for file ID */

  GraphRow *pNext;            /* Next row down in the list of all rows */
  GraphRow *pPrev;            /* Previous row */

  int idx;                    /* Row index.  Top row is smallest. */
  int idxTop;                 /* Direct descendant highest up on the graph */
  GraphRow *pChild;           /* Child immediately above this node */
  u8 isDup;                   /* True if this is duplicate of a prior entry */
  u8 isLeaf;                  /* True if this is a leaf node */
  u8 isStepParent;            /* pChild is actually a step-child. The thick
                              ** arrow up to the child is dashed, not solid */
  u8 hasNormalOutMerge;       /* Is parent of at laest 1 non-cherrypick merge */
  u8 timeWarp;                /* Child is earlier in time */
  u8 bDescender;              /* True if riser from bottom of graph to here. */
  u8 selfUp;                  /* Space above this node but belonging */
  i8 iRail;                   /* Which rail this check-in appears on. 0-based.*/
  i8 mergeOut;                /* Merge out to this rail.  -1 if no merge-out */
  u8 mergeIn[GR_MAX_RAIL];    /* Merge in from non-zero rails */
  int aiRiser[GR_MAX_RAIL];   /* Risers from this node to a higher row. */
  int mergeUpto;              /* Draw the mergeOut rail up to this level */
  int cherrypickUpto;         /* Continue the mergeOut rail up to here */
  u64 mergeDown;              /* Draw merge lines up from bottom of graph */
  u64 cherrypickDown;         /* Draw cherrypick lines up from bottom */
  u64 railInUse;              /* Mask of occupied rails at this row */
};

/* Context while building a graph
*/
struct GraphContext {
  int nErr;                  /* Number of errors encountered */
  int mxRail;                /* Number of rails required to render the graph */
  GraphRow *pFirst;          /* First row in the list.  Top row of graph. */
  GraphRow *pLast;           /* Last row in the list. Bottom row of graph. */
  int nBranch;               /* Number of distinct branches */
  char **azBranch;           /* Names of the branches */
  int nRow;                  /* Number of rows */
  int nHash;                 /* Number of slots in apHash[] */
  u8 hasOffsetMergeRiser;    /* Merge arrow from leaf goes up on a different
                             ** rail that the node */
  u8 bOverfull;              /* Unable to allocate sufficient rails */
  u64 mergeRail;             /* Rails used for merge lines */
  GraphRow **apHash;         /* Hash table of GraphRow objects.  Key: rid */
  u8 aiRailMap[GR_MAX_RAIL+1]; /* Mapping of rails to actually columns */
};

#endif

/* The N-th bit */
#define BIT(N)  (((u64)1)<<(N))

/*
** Number of rows before and after a node with a riser or descender
** that goes off-screen before we can reuse that rail.
*/
#define RISER_MARGIN 4


/*
** Malloc for zeroed space.  Panic if unable to provide the
** requested space.
*/
void *safeMalloc(int nByte){
  void *p = fossil_malloc(nByte);
  memset(p, 0, nByte);
  return p;
}

/*
** Create and initialize a GraphContext
*/
GraphContext *graph_init(void){
  return (GraphContext*)safeMalloc( sizeof(GraphContext) );
}

/*
** Clear all content from a graph
*/
static void graph_clear(GraphContext *p){
  int i;
  GraphRow *pRow;
  while( p->pFirst ){
    pRow = p->pFirst;
    p->pFirst = pRow->pNext;
    free(pRow);
  }
  for(i=0; i<p->nBranch; i++) free(p->azBranch[i]);
  free(p->azBranch);
  free(p->apHash);
  memset(p, 0, sizeof(*p));
  p->nErr = 1;
}

/*
** Destroy a GraphContext;
*/
void graph_free(GraphContext *p){
  graph_clear(p);
  free(p);
}

/*
** Insert a row into the hash table.  pRow->rid is the key.  Keys must
** be unique.  If there is already another row with the same rid,
** overwrite the prior entry if and only if the overwrite flag is set.
*/
static void hashInsert(GraphContext *p, GraphRow *pRow, int overwrite){
  int h;
  h = pRow->rid % p->nHash;
  while( p->apHash[h] && p->apHash[h]->rid!=pRow->rid ){
    h++;
    if( h>=p->nHash ) h = 0;
  }
  if( p->apHash[h]==0 || overwrite ){
    p->apHash[h] = pRow;
  }
}

/*
** Look up the row with rid.
*/
static GraphRow *hashFind(GraphContext *p, GraphRowId rid){
  int h = rid % p->nHash;
  while( p->apHash[h] && p->apHash[h]->rid!=rid ){
    h++;
    if( h>=p->nHash ) h = 0;
  }
  return p->apHash[h];
}

/*
** Return the canonical pointer for a given branch name.
** Multiple calls to this routine with equivalent strings
** will return the same pointer.
**
** The returned value is a pointer to a (readonly) string that
** has the useful property that strings can be checked for
** equality by comparing pointers.
**
** Note: also used for background color names.
*/
static char *persistBranchName(GraphContext *p, const char *zBranch){
  int i;
  for(i=0; i<p->nBranch; i++){
    if( fossil_strcmp(zBranch, p->azBranch[i])==0 ) return p->azBranch[i];
  }
  p->nBranch++;
  p->azBranch = fossil_realloc(p->azBranch, sizeof(char*)*p->nBranch);
  p->azBranch[p->nBranch-1] = fossil_strdup(zBranch);
  return p->azBranch[p->nBranch-1];
}

/*
** Add a new row to the graph context.  Rows are added from top to bottom.
*/
int graph_add_row(
  GraphContext *p,     /* The context to which the row is added */
  GraphRowId rid,      /* RID for the check-in */
  int nParent,         /* Number of parents */
  int nCherrypick,     /* How many of aParent[] are actually cherrypicks */
  GraphRowId *aParent, /* Array of parents */
  const char *zBranch, /* Branch for this check-in */
  const char *zBgClr,  /* Background color. NULL or "" for white. */
  const char *zUuid,   /* hash name of the object being graphed */
  int isLeaf           /* True if this row is a leaf */
){
  GraphRow *pRow;
  int nByte;
  static int nRow = 0;

  if( p->nErr ) return 0;
  nByte = sizeof(GraphRow);
  if( nParent>0 ) nByte += sizeof(pRow->aParent[0])*nParent;
  pRow = (GraphRow*)safeMalloc( nByte );
  pRow->aParent = nParent>0 ? (GraphRowId*)&pRow[1] : 0;
  pRow->rid = rid;
  if( nCherrypick>=nParent ){
    nCherrypick = nParent-1; /* Safety. Should never happen. */
  }
  pRow->nParent = nParent;
  pRow->nCherrypick = nCherrypick;
  pRow->nNonCherrypick = nParent - nCherrypick;
  pRow->zBranch = persistBranchName(p, zBranch);
  if( zUuid==0 ) zUuid = "";
  sqlite3_snprintf(sizeof(pRow->zUuid), pRow->zUuid, "%s", zUuid);
  pRow->isLeaf = isLeaf;
  memset(pRow->aiRiser, -1, sizeof(pRow->aiRiser));
  if( zBgClr==0 ) zBgClr = "";
  pRow->zBgClr = persistBranchName(p, zBgClr);
  if( nParent>0 ) memcpy(pRow->aParent, aParent, sizeof(aParent[0])*nParent);
  if( p->pFirst==0 ){
    p->pFirst = pRow;
  }else{
    p->pLast->pNext = pRow;
  }
  p->pLast = pRow;
  p->nRow++;
  pRow->idx = pRow->idxTop = ++nRow;
  return pRow->idx;
}

/*
** Return the index of a rail currently not in use for any row between
** top and bottom, inclusive.
*/
static int findFreeRail(
  GraphContext *p,         /* The graph context */
  int top, int btm,        /* Span of rows for which the rail is needed */
  int iNearto,             /* Find rail nearest to this rail */
  int bMergeRail           /* This rail will be used for a merge line */
){
  GraphRow *pRow;
  int i;
  int iBest = 0;
  int iBestDist = 9999;
  u64 inUseMask = 0;
  for(pRow=p->pFirst; pRow && pRow->idx<top; pRow=pRow->pNext){}
  while( pRow && pRow->idx<=btm ){
    inUseMask |= pRow->railInUse;
    pRow = pRow->pNext;
  }

  /* First look for a match that honors bMergeRail */
  for(i=0; i<=p->mxRail; i++){
    u64 m = BIT(i);
    int dist;
    if( inUseMask & m ) continue;
    if( (bMergeRail!=0) != ((p->mergeRail & m)!=0) ) continue;
    if( iNearto<=0 ){
      iBest = i;
      iBestDist = 1;
      break;
    }
    dist = i - iNearto;
    if( dist<0 ) dist = -dist;
    if( dist<iBestDist ){
      iBestDist = dist;
      iBest = i;
    }
  }

  /* If no match, consider all possible rails */
  if( iBestDist>1000 ){
    for(i=0; i<=p->mxRail+1; i++){
      int dist;
      if( inUseMask & BIT(i) ) continue;
      if( iNearto<=0 ){
        iBest = i;
        iBestDist = 1;
        break;
      }
      dist = i - iNearto;
      if( dist<0 ) dist = -dist;
      if( dist<iBestDist ){
        iBestDist = dist;
        iBest = i;
      }
    }
  }
  if( iBestDist>1000 ){
    p->bOverfull = 1;
    iBest = GR_MAX_RAIL;
  }
  if( iBest>GR_MAX_RAIL ){
    p->bOverfull = 1;
    iBest = GR_MAX_RAIL;
  }
  if( iBest>p->mxRail ) p->mxRail = iBest;
  if( bMergeRail ) p->mergeRail |= BIT(iBest);
  return iBest;
}

/*
** Assign all children of node pBottom to the same rail as pBottom.
*/
static void assignChildrenToRail(GraphRow *pBottom, u32 tmFlags){
  int iRail = pBottom->iRail;
  GraphRow *pCurrent;
  GraphRow *pPrior;
  u64 mask = ((u64)1)<<iRail;

  pBottom->railInUse |= mask;
  pPrior = pBottom;
  for(pCurrent=pBottom->pChild; pCurrent; pCurrent=pCurrent->pChild){
    assert( pPrior->idx > pCurrent->idx );
    assert( pCurrent->iRail<0 );
    if( pPrior->timeWarp ) break;
    pCurrent->iRail = iRail;
    pCurrent->railInUse |= mask;
    pPrior->aiRiser[iRail] = pCurrent->idx;
    while( pPrior->idx > pCurrent->idx ){
      pPrior->railInUse |= mask;
      pPrior = pPrior->pPrev;
      assert( pPrior!=0 );
    }
  }
  /* Mask of additional rows for the riser to infinity */
  if( !pPrior->isLeaf && (tmFlags & TIMELINE_DISJOINT)==0 ){
    int n = RISER_MARGIN;
    GraphRow *p;
    pPrior->selfUp = 0;
    for(p=pPrior; p && (n--)>0; p=p->pPrev){
      pPrior->selfUp++;
      p->railInUse |= mask;
    }
  }
}


/*
** Check to see if rail iRail is clear from pBottom up to and including
** pTop.
*/
static int railIsClear(GraphRow *pBottom, int iTop, int iRail){
  u64 m = BIT(iRail);
  while( pBottom && pBottom->idx>=iTop ){
    if( pBottom->railInUse & m ) return 0;
    pBottom = pBottom->pPrev;
  }
  return 1;
}

/*
** Create a merge-arrow riser going from pParent up to pChild.
*/
static void createMergeRiser(
  GraphContext *p,
  GraphRow *pParent,    /* Lower node from which the merge line begins */
  GraphRow *pChild,     /* Upper node at which the merge line ends */
  int isCherrypick      /* True for a cherry-pick merge */
){
  int u;
  u64 mask;
  GraphRow *pLoop;

  if( pParent->mergeOut<0 ){
    u = pParent->aiRiser[pParent->iRail];
    if( u<0 && railIsClear(pParent->pPrev, pChild->idx, pParent->iRail) ){
      /* pParent is a leaf and the merge-line can be drawn straight up.*/
      pParent->mergeOut = pParent->iRail;
      mask = BIT(pParent->iRail);
      for(pLoop=pChild->pNext; pLoop && pLoop->rid!=pParent->rid;
           pLoop=pLoop->pNext){
        pLoop->railInUse |= mask;
      }
    }else if( u>0 && u<pChild->idx ){
      /* The thick arrow up to the next primary child of pDesc goes
      ** further up than the thin merge arrow riser, so draw them both
      ** on the same rail. */
      pParent->mergeOut = pParent->iRail;
    }else if( pParent->idx - pChild->idx < pParent->selfUp ){
      pParent->mergeOut = pParent->iRail;
    }else{
      /* The thin merge arrow riser is taller than the thick primary
      ** child riser, so use separate rails. */
      int iTarget = pParent->iRail;
      if( u<0 ) p->hasOffsetMergeRiser = 1;
      pParent->mergeOut = findFreeRail(p, pChild->idx, pParent->idx-1,
                                       iTarget, 1);
      mask = BIT(pParent->mergeOut);
      for(pLoop=pChild->pNext; pLoop && pLoop->rid!=pParent->rid;
           pLoop=pLoop->pNext){
        pLoop->railInUse |= mask;
      }
    }
  }
  if( isCherrypick ){
    if( pParent->cherrypickUpto==0 || pParent->cherrypickUpto > pChild->idx ){
      pParent->cherrypickUpto = pChild->idx;
    }
  }else{
    pParent->hasNormalOutMerge = 1;
    if( pParent->mergeUpto==0 || pParent->mergeUpto > pChild->idx ){
      pParent->mergeUpto = pChild->idx;
    }
  }
  pChild->mergeIn[pParent->mergeOut] = isCherrypick ? 2 : 1;
}

/*
** Compute the maximum rail number.
*/
static void find_max_rail(GraphContext *p){
  GraphRow *pRow;
  p->mxRail = 0;
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->iRail>p->mxRail ) p->mxRail = pRow->iRail;
    if( pRow->mergeOut>p->mxRail ) p->mxRail = pRow->mergeOut;
    while( p->mxRail<GR_MAX_RAIL
        && (pRow->mergeDown|pRow->cherrypickDown)>(BIT(p->mxRail+1)-1)
    ){
      p->mxRail++;
    }
  }
}

/*
** Draw a riser from pRow upward to indicate that it is going
** to a node that is off the graph to the top.
*/
static void riser_to_top(GraphRow *pRow){
  u64 mask = BIT(pRow->iRail);
  int n = RISER_MARGIN;
  pRow->aiRiser[pRow->iRail] = 0;
  while( pRow && (n--)>0 ){
    pRow->railInUse |= mask;
    pRow = pRow->pPrev;
  }
}

/*
** Compute the complete graph
**
** When primary or merge parents are off-screen, normally a line is drawn
** from the node down to the bottom of the graph.  This line is called a
** "descender".  But if the omitDescenders flag is true, then lines down
** to the bottom of the screen are omitted.
**
** The tmFlags parameter is zero or more of the TIMELINE_* constants.
** Only the following are honored:
**
**       TIMELINE_DISJOINT:    Omit descenders
**       TIMELINE_FILLGAPS:    Use step-children
**       TIMELINE_XMERGE:      Omit off-graph merge lines
*/
void graph_finish(
  GraphContext *p,                /* The graph to be laid out */
  Matcher *pLeftBranch,           /* Compares true for left-most branch */
  u32 tmFlags                     /* TIMELINE flags */
){
  GraphRow *pRow, *pDesc, *pDup, *pLoop, *pParent;
  int i, j;
  u64 mask;
  int hasDup = 0;      /* True if one or more isDup entries */
  const char *zTrunk;
  u8 *aMap;            /* Copy of p->aiRailMap */
  int omitDescenders = (tmFlags & TIMELINE_DISJOINT)!=0;
  int nTimewarp = 0;
  int riserMargin = (tmFlags & TIMELINE_DISJOINT) ? 0 : RISER_MARGIN;

  /* If mergeRiserFrom[X]==Y that means rail X holds a merge riser
  ** coming up from the bottom of the graph from off-screen check-in Y
  ** where Y is the RID.  There is no riser on rail X if mergeRiserFrom[X]==0.
  */
  GraphRowId mergeRiserFrom[GR_MAX_RAIL];

  if( p==0 || p->pFirst==0 || p->nErr ) return;
  p->nErr = 1;   /* Assume an error until proven otherwise */

  /* Initialize all rows */
  p->nHash = p->nRow*2 + 1;
  p->apHash = safeMalloc( sizeof(p->apHash[0])*p->nHash );
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->pNext ) pRow->pNext->pPrev = pRow;
    pRow->iRail = -1;
    pRow->mergeOut = -1;
    if( (pDup = hashFind(p, pRow->rid))!=0 ){
      hasDup = 1;
      pDup->isDup = 1;
    }
    hashInsert(p, pRow, 1);
  }
  p->mxRail = -1;
  memset(mergeRiserFrom, 0, sizeof(mergeRiserFrom));

  /* Purge merge-parents that are out-of-graph if descenders are not
  ** drawn.
  **
  ** Each node has one primary parent and zero or more "merge" parents.
  ** A merge parent is a prior check-in from which changes were merged into
  ** the current check-in.  If a merge parent is not in the visible section
  ** of this graph, then no arrows will be drawn for it, so remove it from
  ** the aParent[] array.
  */
  if( (tmFlags & (TIMELINE_DISJOINT|TIMELINE_XMERGE))!=0 ){
    for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
      for(i=1; i<pRow->nParent; i++){
        GraphRow *pParent = hashFind(p, pRow->aParent[i]);
        if( pParent==0 ){
          memmove(pRow->aParent+i, pRow->aParent+i+1,
                  sizeof(pRow->aParent[0])*(pRow->nParent-i-1));
          pRow->nParent--;
          if( i<pRow->nNonCherrypick ){
            pRow->nNonCherrypick--;
          }else{
            pRow->nCherrypick--;
          }
          i--;
        }
      }
    }
  }

  /* Put the deepest (earliest) merge parent first in the list.
  ** An off-screen merge parent is considered deepest.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext ){
    if( pRow->nParent<=1 ) continue;
    for(i=1; i<pRow->nParent; i++){
      GraphRow *pParent = hashFind(p, pRow->aParent[i]);
      if( pParent ) pParent->nMergeChild++;
    }
    if( pRow->nCherrypick>1 ){
      int iBest = -1;
      int iDeepest = -1;
      for(i=pRow->nNonCherrypick; i<pRow->nParent; i++){
        GraphRow *pParent = hashFind(p, pRow->aParent[i]);
        if( pParent==0 ){
          iBest = i;
          break;
        }
        if( pParent->idx>iDeepest ){
          iDeepest = pParent->idx;
          iBest = i;
        }
      }
      i = pRow->nNonCherrypick;
      if( iBest>i ){
        GraphRowId x = pRow->aParent[i];
        pRow->aParent[i] = pRow->aParent[iBest];
        pRow->aParent[iBest] = x;
      }
    }
    if( pRow->nNonCherrypick>2 ){
      int iBest = -1;
      int iDeepest = -1;
      for(i=1; i<pRow->nNonCherrypick; i++){
        GraphRow *pParent = hashFind(p, pRow->aParent[i]);
        if( pParent==0 ){
          iBest = i;
          break;
        }
        if( pParent->idx>iDeepest ){
          iDeepest = pParent->idx;
          iBest = i;
        }
      }
      if( iBest>1 ){
        GraphRowId x = pRow->aParent[1];
        pRow->aParent[1] = pRow->aParent[iBest];
        pRow->aParent[iBest] = x;
      }
    }
  }

  /* If the primary parent is in a different branch, but there are
  ** other parents in the same branch, reorder the parents to make
  ** the parent from the same branch the primary parent.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->isDup ) continue;
    if( pRow->nNonCherrypick<2 ) continue;      /* Not a fork */
    pParent = hashFind(p, pRow->aParent[0]);
    if( pParent==0 ) continue;                         /* Parent off-screen */
    if( pParent->zBranch==pRow->zBranch ) continue;    /* Same branch */
    for(i=1; i<pRow->nNonCherrypick; i++){
      pParent = hashFind(p, pRow->aParent[i]);
      if( pParent && pParent->zBranch==pRow->zBranch ){
        GraphRowId t = pRow->aParent[0];
        pRow->aParent[0] = pRow->aParent[i];
        pRow->aParent[i] = t;
        break;
      }
    }
  }


  /* Find the pChild pointer for each node.
  **
  ** The pChild points to the node directly above on the same rail.
  ** The pChild must be in the same branch.  Leaf nodes have a NULL
  ** pChild.
  **
  ** In the case of a fork, choose the pChild that results in the
  ** longest rail.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->isDup ) continue;
    if( pRow->nParent<=0 ) continue;                   /* Root node */
    pParent = hashFind(p, pRow->aParent[0]);
    if( pParent==0 ) continue;                         /* Parent off-screen */
    if( pParent->zBranch!=pRow->zBranch ) continue;    /* Different branch */
    if( pParent->idx <= pRow->idx ){
      pParent->timeWarp = 1;
      nTimewarp++;
    }else if( pRow->idxTop < pParent->idxTop ){
      pParent->pChild = pRow;
      pParent->idxTop = pRow->idxTop;
    }
  }

  if( tmFlags & TIMELINE_FILLGAPS ){
    /* If a node has no pChild in the graph
    ** and there is a node higher up in the graph
    ** that is in the same branch and has no in-graph parent, then
    ** make the lower node a step-child of the upper node.  This will
    ** be represented on the graph by a thick dotted line without an arrowhead.
    */
    for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
      if( pRow->pChild ) continue;
      if( pRow->isLeaf ) continue;
      for(pLoop=pRow->pPrev; pLoop; pLoop=pLoop->pPrev){
        if( pLoop->nParent>0
         && pLoop->zBranch==pRow->zBranch
         && hashFind(p,pLoop->aParent[0])==0
        ){
          pRow->pChild = pLoop;
          pRow->isStepParent = 1;
          pLoop->aParent[0] = pRow->rid;
          break;
        }
      }
    }
  }

  /* Set the idxTop values for all entries.  The idxTop value is the
  ** "idx" value for the top entry in its stack of children.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    GraphRow *pChild = pRow->pChild;
    if( pChild && pRow->idxTop>pChild->idxTop ){
      pRow->idxTop = pChild->idxTop;
    }
  }

  /* Identify rows where the primary parent is off screen.  Assign
  ** each to a rail and draw descenders downward.
  **
  ** Strive to put the "trunk" branch on far left.
  */
  zTrunk = persistBranchName(p, "trunk");
  for(i=0; i<2; i++){
    for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
      if( i==0 && pRow->zBranch!=zTrunk ) continue;
      if( pRow->iRail>=0 ) continue;
      if( pRow->isDup ) continue;
      if( pRow->nParent<0 ) continue;
      if( pRow->nParent==0 || hashFind(p,pRow->aParent[0])==0 ){
        pRow->iRail = findFreeRail(p, pRow->idxTop, pRow->idx+riserMargin,0,0);
        /* if( p->mxRail>=GR_MAX_RAIL ) return; */
        mask = BIT(pRow->iRail);
        if( !omitDescenders ){
          int n = RISER_MARGIN;
          pRow->bDescender = pRow->nParent>0;
          for(pLoop=pRow; pLoop && (n--)>0; pLoop=pLoop->pNext){
            pLoop->railInUse |= mask;
          }
        }
        assignChildrenToRail(pRow, tmFlags);
      }
    }
  }

  /* Assign rails to all rows that are still unassigned.
  */
  for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
    GraphRowId parentRid;

    if( pRow->iRail>=0 ){
      if( pRow->pChild==0 && !pRow->timeWarp ){
        if( !omitDescenders && count_nonbranch_children(pRow->rid)!=0 ){
          riser_to_top(pRow);
        }
      }
      continue;
    }
    if( pRow->isDup || pRow->nParent<0 ){
      continue;
    }else{
      assert( pRow->nParent>0 );
      parentRid = pRow->aParent[0];
      pParent = hashFind(p, parentRid);
      if( pParent==0 ){
        pRow->iRail = ++p->mxRail;
        if( p->mxRail>=GR_MAX_RAIL ){
          pRow->iRail = p->mxRail = GR_MAX_RAIL;
          p->bOverfull = 1;
        }
        pRow->railInUse = BIT(pRow->iRail);
        continue;
      }
      if( pParent->idx>pRow->idx ){
        /* Common case:  Child occurs after parent and is above the
        ** parent in the timeline */
        pRow->iRail = findFreeRail(p, pRow->idxTop, pParent->idx,
                                   pParent->iRail, 0);
        /* if( p->mxRail>=GR_MAX_RAIL ) return; */
        pParent->aiRiser[pRow->iRail] = pRow->idx;
      }else{
        /* Timewarp case:  Child occurs earlier in time than parent and
        ** appears below the parent in the timeline. */
        int iDownRail = ++p->mxRail;
        if( iDownRail<1 ) iDownRail = ++p->mxRail;
        if( p->mxRail>GR_MAX_RAIL ){
          iDownRail = p->mxRail = GR_MAX_RAIL;
          p->bOverfull = 1;
        }
        pRow->iRail = ++p->mxRail;
        if( p->mxRail>=GR_MAX_RAIL ){
          pRow->iRail = p->mxRail = GR_MAX_RAIL;
          p->bOverfull = 1;
        }
        pRow->railInUse = BIT(pRow->iRail);
        pParent->aiRiser[iDownRail] = pRow->idx;
        mask = BIT(iDownRail);
        for(pLoop=p->pFirst; pLoop; pLoop=pLoop->pNext){
          pLoop->railInUse |= mask;
        }
      }
    }
    mask = BIT(pRow->iRail);
    pRow->railInUse |= mask;
    if( pRow->pChild ){
      assignChildrenToRail(pRow, tmFlags);
    }else if( !omitDescenders && count_nonbranch_children(pRow->rid)!=0 ){
      if( !pRow->timeWarp ) riser_to_top(pRow);
    }
    if( pParent ){
      if( pParent->idx>pRow->idx ){
        /* Common case:  Parent is below current row in the graph */
        for(pLoop=pParent->pPrev; pLoop && pLoop!=pRow; pLoop=pLoop->pPrev){
          pLoop->railInUse |= mask;
        }
      }else{
        /* Timewarp case: Parent is above current row in the graph */
        for(pLoop=pParent->pNext; pLoop && pLoop!=pRow; pLoop=pLoop->pNext){
          pLoop->railInUse |= mask;
        }
      }
    }
  }

  /*
  ** Insert merge rails and merge arrows
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    int iReuseIdx = -1;
    int iReuseRail = -1;
    int isCherrypick = 0;
    for(i=1; i<pRow->nParent; i++){
      GraphRowId parentRid = pRow->aParent[i];
      if( i==pRow->nNonCherrypick ){
        /* Because full merges are laid out before cherrypicks,
        ** it is ok to use a full-merge raiser for a cherrypick.
        ** See the graph on check-in 8ac66ef33b464d28 for example
        **    iReuseIdx = -1;
        **    iReuseRail = -1; */
        isCherrypick = 1;
      }
      pDesc = hashFind(p, parentRid);
      if( pDesc==0 ){
        int iMrail = -1;
        /* Merge from a node that is off-screen */
        if( iReuseIdx>=p->nRow+1 ){
          continue;  /* Suppress multiple off-screen merges */
        }
        for(j=0; j<GR_MAX_RAIL; j++){
          if( mergeRiserFrom[j]==parentRid ){
            iMrail = j;
            break;
          }
        }
        if( iMrail==-1 ){
          iMrail = findFreeRail(p, pRow->idx, p->pLast->idx, 0, 1);
          /*if( p->mxRail>=GR_MAX_RAIL ) return;*/
          mergeRiserFrom[iMrail] = parentRid;
        }
        iReuseIdx = p->nRow+1;
        iReuseRail = iMrail;
        mask = BIT(iMrail);
        if( i>=pRow->nNonCherrypick ){
          pRow->mergeIn[iMrail] = 2;
          pRow->cherrypickDown |= mask;
        }else{
          pRow->mergeIn[iMrail] = 1;
          pRow->mergeDown |= mask;
        }
        for(pLoop=pRow->pNext; pLoop; pLoop=pLoop->pNext){
          pLoop->railInUse |= mask;
        }
      }else{
        /* The merge parent node does exist on this graph */
        if( iReuseIdx>pDesc->idx
         && pDesc->nMergeChild==1
        ){
          /* Reuse an existing merge riser */
          pDesc->mergeOut = iReuseRail;
          if( isCherrypick ){
            pDesc->cherrypickUpto = pDesc->idx;
          }else{
            pDesc->hasNormalOutMerge = 1;
            pDesc->mergeUpto = pDesc->idx;
          }
        }else{
          /* Create a new merge for an on-screen node */
          createMergeRiser(p, pDesc, pRow, isCherrypick);
          /* if( p->mxRail>=GR_MAX_RAIL ) return; */
          if( iReuseIdx<0
           && pDesc->nMergeChild==1
           && (pDesc->iRail!=pDesc->mergeOut || pDesc->isLeaf)
          ){
            iReuseIdx = pDesc->idx;
            iReuseRail = pDesc->mergeOut;
          }
        }
      }
    }
  }

  /*
  ** Insert merge rails from primaries to duplicates.
  */
  if( hasDup && p->mxRail<GR_MAX_RAIL ){
    int dupRail;
    int mxRail;
    find_max_rail(p);
    mxRail = p->mxRail;
    dupRail = mxRail+1;
    /* if( p->mxRail>=GR_MAX_RAIL ) return; */
    for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
      if( !pRow->isDup ) continue;
      pRow->iRail = dupRail;
      pDesc = hashFind(p, pRow->rid);
      assert( pDesc!=0 && pDesc!=pRow );
      createMergeRiser(p, pDesc, pRow, 0);
      if( pDesc->mergeOut>mxRail ) mxRail = pDesc->mergeOut;
    }
    if( dupRail<=mxRail ){
      dupRail = mxRail+1;
      for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
        if( pRow->isDup ) pRow->iRail = dupRail;
      }
    }
    /* if( mxRail>=GR_MAX_RAIL ) return; */
  }

  /*
  ** Find the maximum rail number.
  */
  find_max_rail(p);

  /* If a leaf node has a merge riser going up on a different rail,
  ** try to move the rail of the node (and its ancestors) to be underneath
  ** the merge riser.  This is an optimization that improves the
  ** appearance of graph but is not strictly necessary.
  */
  if( nTimewarp==0 && p->hasOffsetMergeRiser ){
    for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
      GraphRow *pBottom;       /* Bottom row of a branch */
      GraphRow *pRoot;         /* Node off of which the branch diverges */
      int iFrom;               /* Proposed to move from this rail */
      int iTo;                 /* Move the branch to this rail */

      iFrom = pRow->iRail;
      if( pRow->aiRiser[iFrom]>=0 ) continue;  /* Not a leaf */
      if( pRow->mergeOut<0 ) continue;         /* No merge riser */
      if( pRow->mergeOut==iFrom ) continue;    /* Riser already aligned */
      iTo = pRow->mergeOut;

      /* Find the bottom (oldest) node in the branch */
      pBottom = 0;
      for(pLoop=pRow; pLoop; pLoop=pLoop->pNext){
        if( pLoop->idxTop==pRow->idx ) pBottom = pLoop;
      }
      if( pBottom==0 ) continue;  /* Not possible */

      /* Verify that the rail we want to shift into is clear */
      pLoop = pBottom;
      if( pLoop->pNext ) pLoop = pLoop->pNext;
      if( !railIsClear(pLoop, pRow->idx+1, iTo) ){
        /* Other nodes or risers are already using the space that
        ** we propose to move the pRow branch into. */
        continue;
      }

      /* Find the "root" of the branch.  The root is a different branch
      ** from which the pRow branch emerges.  There might not be a root
      ** if the pRow branch started off the bottom of the screen.
      */
      for(pRoot=pBottom->pNext; pRoot; pRoot=pRoot->pNext){
        if( pRoot->aiRiser[iFrom]==pBottom->idx ) break;
      }
      if( pRoot && pRoot->iRail==iTo ){
        /* The parent branch from which this branch emerges is on the
        ** same rail as pRow.  Do not shift as that would stack a child
        ** branch directly above its parent. */
        continue;
      }

      /* All clear.  Make the translation
      */
      for(pLoop=pRow; pLoop && pLoop->idx<=pBottom->idx; pLoop=pLoop->pNext){
        if( pLoop->iRail==iFrom ){
          pLoop->iRail = iTo;
          pLoop->aiRiser[iTo] = pLoop->aiRiser[iFrom];
          pLoop->aiRiser[iFrom] = -1;
        }
      }
      if( pRoot ){
        pRoot->aiRiser[iTo] = pRoot->aiRiser[iFrom];
        pRoot->aiRiser[iFrom] = -1;
      }
    }
  }

  /*
  ** Compute the rail mapping that tries to put the branch named
  ** pLeftBranch at the left margin.  Other branches that merge
  ** with pLeftBranch are to the right with merge rails in between.
  **
  ** aMap[X]=Y means that the X-th rail is drawn as the Y-th rail.
  **
  ** Do not move rails around if there are timewarps, because that can
  ** seriously mess up the display of timewarps.  Timewarps should be
  ** rare so this should not be a serious limitation to the algorithm.
  */
  aMap = p->aiRailMap;
  for(i=0; i<=p->mxRail; i++) aMap[i] = i; /* Set up a default mapping */
  if( nTimewarp==0 ){
    int kk;
    /* Priority bits:
    **
    **    0x04      The preferred branch
    **
    **    0x02      A merge rail - a rail that contains merge lines into
    **              the preferred branch.  Only applies if a preferred branch
    **              is defined.  This improves the display of r=BRANCH
    **              options to /timeline.
    **
    **    0x01      A rail that merges with the preferred branch
    */
    u16 aPriority[GR_MAX_RAIL];
    int mxMatch = 0;
    memset(aPriority, 0, (p->mxRail+1)*sizeof(aPriority[0]));
    if( pLeftBranch ){
      for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
        int iMatch = match_text(pLeftBranch, pRow->zBranch);
        if( iMatch>0 ){
          if( iMatch>10 ) iMatch = 10;
          aPriority[pRow->iRail] |= 1<<(iMatch+1);
          if( mxMatch<iMatch ) mxMatch = iMatch;
          for(i=0; i<=p->mxRail; i++){
            if( pRow->mergeIn[i] ) aPriority[i] |= 1;
          }
          if( pRow->mergeOut>=0 ) aPriority[pRow->mergeOut] |= 1;
        }
      }
      for(i=0; i<=p->mxRail; i++){
        if( p->mergeRail & BIT(i) ){
          aPriority[i] |= 2;
        }
      }
    }else{
      j = 1;
      aPriority[0] = 4;
      mxMatch = 1;
      for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
        if( pRow->iRail==0 ){
          for(i=0; i<=p->mxRail; i++){
            if( pRow->mergeIn[i] ) aPriority[i] |= 1;
          }
          if( pRow->mergeOut>=0 ) aPriority[pRow->mergeOut] |= 1;
        }
      }
    }

#if 0
    fprintf(stderr,"mergeRail: 0x%llx\n", p->mergeRail);
    fprintf(stderr,"Priority:");
    for(i=0; i<=p->mxRail; i++){
        fprintf(stderr," %x.%x",
                aPriority[i]/4, aPriority[i]&3);
    }
    fprintf(stderr,"\n");
#endif

    j = 0;
    for(kk=4; kk<=1<<(mxMatch+1); kk*=2){
      for(i=0; i<=p->mxRail; i++){
        if( aPriority[i]>=kk && aPriority[i]<kk*2 ){
          aMap[i] = j++;
        }
      }
    }
    for(i=p->mxRail; i>=0; i--){
      if( aPriority[i]==3 ) aMap[i] = j++;
    }
    for(i=0; i<=p->mxRail; i++){
      if( aPriority[i]==1 || aPriority[i]==2 ) aMap[i] = j++;
    }
    for(i=0; i<=p->mxRail; i++){
      if( aPriority[i]==0 ) aMap[i] = j++;
    }
    cgi_printf("<!-- aiRailMap =");
    for(i=0; i<=p->mxRail; i++) cgi_printf(" %d", aMap[i]);
    cgi_printf(" -->\n");
  }

  p->nErr = 0;
}
