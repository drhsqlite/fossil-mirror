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

#if INTERFACE

#define GR_MAX_RAIL   40      /* Max number of "rails" to display */

/* The graph appears vertically beside a timeline.  Each row in the
** timeline corresponds to a row in the graph.  GraphRow.idx is 0 for
** the top-most row and increases moving down.  Hence (in the absence of
** time skew) parents have a larger index than their children.
*/
struct GraphRow {
  int rid;                    /* The rid for the check-in */
  i8 nParent;                 /* Number of parents */
  int *aParent;               /* Array of parents.  0 element is primary .*/
  char *zBranch;              /* Branch name */
  char *zBgClr;               /* Background Color */
  char zUuid[41];             /* Check-in for file ID */

  GraphRow *pNext;            /* Next row down in the list of all rows */
  GraphRow *pPrev;            /* Previous row */

  int idx;                    /* Row index.  First is 1.  0 used for "none" */
  int idxTop;                 /* Direct descendent highest up on the graph */
  GraphRow *pChild;           /* Child immediately above this node */
  u8 isDup;                   /* True if this is duplicate of a prior entry */
  u8 isLeaf;                  /* True if this is a leaf node */
  u8 timeWarp;                /* Child is earlier in time */
  u8 bDescender;              /* True if riser from bottom of graph to here. */
  i8 iRail;                   /* Which rail this check-in appears on. 0-based.*/
  i8 mergeOut;                /* Merge out to this rail.  -1 if no merge-out */
  u8 mergeIn[GR_MAX_RAIL];    /* Merge in from non-zero rails */
  int aiRiser[GR_MAX_RAIL];   /* Risers from this node to a higher row. */
  int mergeUpto;              /* Draw the mergeOut rail up to this level */
  u64 mergeDown;              /* Draw merge lines up from bottom of graph */

  u64 railInUse;              /* Mask of occupied rails at this row */
};

/* Context while building a graph
*/
struct GraphContext {
  int nErr;                  /* Number of errors encountered */
  int mxRail;                /* Number of rails required to render the graph */
  GraphRow *pFirst;          /* First row in the list */
  GraphRow *pLast;           /* Last row in the list */
  int nBranch;               /* Number of distinct branches */
  char **azBranch;           /* Names of the branches */
  int nRow;                  /* Number of rows */
  int nHash;                 /* Number of slots in apHash[] */
  GraphRow **apHash;         /* Hash table of GraphRow objects.  Key: rid */
};

#endif

/* The N-th bit */
#define BIT(N)  (((u64)1)<<(N))

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
static GraphRow *hashFind(GraphContext *p, int rid){
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
  p->azBranch[p->nBranch-1] = mprintf("%s", zBranch);
  return p->azBranch[p->nBranch-1];
}

/*
** Add a new row to the graph context.  Rows are added from top to bottom.
*/
int graph_add_row(
  GraphContext *p,     /* The context to which the row is added */
  int rid,             /* RID for the check-in */
  int nParent,         /* Number of parents */
  int *aParent,        /* Array of parents */
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
  nByte += sizeof(pRow->aParent[0])*nParent;
  pRow = (GraphRow*)safeMalloc( nByte );
  pRow->aParent = (int*)&pRow[1];
  pRow->rid = rid;
  pRow->nParent = nParent;
  pRow->zBranch = persistBranchName(p, zBranch);
  if( zUuid==0 ) zUuid = "";
  sqlite3_snprintf(sizeof(pRow->zUuid), pRow->zUuid, "%s", zUuid);
  pRow->isLeaf = isLeaf;
  memset(pRow->aiRiser, -1, sizeof(pRow->aiRiser));
  if( zBgClr==0 ) zBgClr = "";
  pRow->zBgClr = persistBranchName(p, zBgClr);
  memcpy(pRow->aParent, aParent, sizeof(aParent[0])*nParent);
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
  int iNearto              /* Find rail nearest to this rail */
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
  for(i=0; i<32; i++){
    if( (inUseMask & BIT(i))==0 ){
      int dist;
      if( iNearto<=0 ){
        return i;
      }
      dist = i - iNearto;
      if( dist<0 ) dist = -dist;
      if( dist<iBestDist ){
        iBestDist = dist;
        iBest = i;
      }
    }
  }
  if( iBestDist>1000 ) p->nErr++;
  if( iBest>p->mxRail ) p->mxRail = iBest;
  return iBest;
}

/*
** Assign all children of node pBottom to the same rail as pBottom.
*/
static void assignChildrenToRail(GraphRow *pBottom){
  int iRail = pBottom->iRail;
  GraphRow *pCurrent;
  GraphRow *pPrior;
  u64 mask = ((u64)1)<<iRail;

  pBottom->railInUse |= mask;
  pPrior = pBottom;
  for(pCurrent=pBottom->pChild; pCurrent; pCurrent=pCurrent->pChild){
    assert( pPrior->idx > pCurrent->idx );
    assert( pCurrent->iRail<0 );
    pCurrent->iRail = iRail;
    pCurrent->railInUse |= mask;
    pPrior->aiRiser[iRail] = pCurrent->idx;
    while( pPrior->idx > pCurrent->idx ){
      pPrior->railInUse |= mask;
      pPrior = pPrior->pPrev;
      assert( pPrior!=0 );
    }
  }
}

/*
** Create a merge-arrow riser going from pParent up to pChild.
*/
static void createMergeRiser(
  GraphContext *p,
  GraphRow *pParent,
  GraphRow *pChild
){
  int u;
  u64 mask;
  GraphRow *pLoop;

  if( pParent->mergeOut<0 ){
    u = pParent->aiRiser[pParent->iRail];
    if( u>=0 && u<pChild->idx ){
      /* The thick arrow up to the next primary child of pDesc goes
      ** further up than the thin merge arrow riser, so draw them both
      ** on the same rail. */
      pParent->mergeOut = pParent->iRail;
      pParent->mergeUpto = pChild->idx;
    }else{
      /* The thin merge arrow riser is taller than the thick primary
      ** child riser, so use separate rails. */
      int iTarget = pParent->iRail;
      pParent->mergeOut = findFreeRail(p, pChild->idx, pParent->idx-1, iTarget);
      pParent->mergeUpto = pChild->idx;
      mask = BIT(pParent->mergeOut);
      for(pLoop=pChild->pNext; pLoop && pLoop->rid!=pParent->rid;
           pLoop=pLoop->pNext){
        pLoop->railInUse |= mask;
      }
    }
  }
  pChild->mergeIn[pParent->mergeOut] = 1;
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
    while( p->mxRail<GR_MAX_RAIL && pRow->mergeDown>(BIT(p->mxRail+1)-1) ){
      p->mxRail++;
    }
  }
}

/*
** Draw a riser from pRow to the top of the graph
*/
static void riser_to_top(GraphRow *pRow){
  u64 mask = BIT(pRow->iRail);
  pRow->aiRiser[pRow->iRail] = 0;
  while( pRow ){
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
*/
void graph_finish(GraphContext *p, int omitDescenders){
  GraphRow *pRow, *pDesc, *pDup, *pLoop, *pParent;
  int i, j;
  u64 mask;
  int hasDup = 0;      /* True if one or more isDup entries */
  const char *zTrunk;

  /* If mergeRiserFrom[X]==Y that means rail X holds a merge riser
  ** coming up from the bottom of the graph from off-screen check-in Y
  ** where Y is the RID.  There is no riser on rail X if mergeRiserFrom[X]==0.
  */
  int mergeRiserFrom[GR_MAX_RAIL];

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
  if( omitDescenders ){
    for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
      for(i=1; i<pRow->nParent; i++){
        if( hashFind(p, pRow->aParent[i])==0 ){
          pRow->aParent[i] = pRow->aParent[--pRow->nParent];
          i--;
        }
      }
    }
  }

  /* If the primary parent is in a different branch, but there are
  ** other parents in the same branch, reorder the parents to make
  ** the parent from the same branch the primary parent.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->isDup ) continue;
    if( pRow->nParent<2 ) continue;                    /* Not a fork */
    pParent = hashFind(p, pRow->aParent[0]);
    if( pParent==0 ) continue;                         /* Parent off-screen */
    if( pParent->zBranch==pRow->zBranch ) continue;    /* Same branch */
    for(i=1; i<pRow->nParent; i++){
      pParent = hashFind(p, pRow->aParent[i]);
      if( pParent && pParent->zBranch==pRow->zBranch ){
        int t = pRow->aParent[0];
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
    if( pRow->nParent==0 ) continue;                   /* Root node */
    pParent = hashFind(p, pRow->aParent[0]);
    if( pParent==0 ) continue;                         /* Parent off-screen */
    if( pParent->zBranch!=pRow->zBranch ) continue;    /* Different branch */
    if( pParent->idx <= pRow->idx ){
       pParent->timeWarp = 1;
       continue;                                       /* Time-warp */
    }
    if( pRow->idxTop < pParent->idxTop ){
      pParent->pChild = pRow;
      pParent->idxTop = pRow->idxTop;
    }
  }

  /* Identify rows where the primary parent is off screen.  Assign
  ** each to a rail and draw descenders to the bottom of the screen.
  **
  ** Strive to put the "trunk" branch on far left.
  */
  zTrunk = persistBranchName(p, "trunk");
  for(i=0; i<2; i++){
    for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
      if( pRow->isDup ) continue;
      if( i==0 ){
        if( pRow->zBranch!=zTrunk ) continue;
      }else {
        if( pRow->iRail>=0 ) continue;
      }
      if( pRow->nParent==0 || hashFind(p,pRow->aParent[0])==0 ){
        if( omitDescenders ){
          pRow->iRail = findFreeRail(p, pRow->idxTop, pRow->idx, 0);
        }else{
          pRow->iRail = ++p->mxRail;
        }
        if( p->mxRail>=GR_MAX_RAIL ) return;
        mask = BIT(pRow->iRail);
        if( !omitDescenders ){
          pRow->bDescender = pRow->nParent>0;
          for(pLoop=pRow; pLoop; pLoop=pLoop->pNext){
            pLoop->railInUse |= mask;
          }
        }
        assignChildrenToRail(pRow);
      }
    }
  }

  /* Assign rails to all rows that are still unassigned.
  */
  for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
    int parentRid;

    if( pRow->iRail>=0 ){
      if( pRow->pChild==0 && !pRow->timeWarp ){
        if( !omitDescenders && count_nonbranch_children(pRow->rid)!=0 ){
          riser_to_top(pRow);
        }
      }
      continue;
    }
    if( pRow->isDup ){
      continue;
    }else{
      assert( pRow->nParent>0 );
      parentRid = pRow->aParent[0];
      pParent = hashFind(p, parentRid);
      if( pParent==0 ){
        pRow->iRail = ++p->mxRail;
        if( p->mxRail>=GR_MAX_RAIL ) return;
        pRow->railInUse = BIT(pRow->iRail);
        continue;
      }
      if( pParent->idx>pRow->idx ){
        /* Common case:  Child occurs after parent and is above the
        ** parent in the timeline */
        pRow->iRail = findFreeRail(p, 0, pParent->idx, pParent->iRail);
        if( p->mxRail>=GR_MAX_RAIL ) return;
        pParent->aiRiser[pRow->iRail] = pRow->idx;
      }else{
        /* Timewarp case:  Child occurs earlier in time than parent and
        ** appears below the parent in the timeline. */
        int iDownRail = ++p->mxRail;
        if( iDownRail<1 ) iDownRail = ++p->mxRail;
        pRow->iRail = ++p->mxRail;
        if( p->mxRail>=GR_MAX_RAIL ) return;
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
      assignChildrenToRail(pRow);
    }else if( !omitDescenders && count_nonbranch_children(pRow->rid)!=0 ){
      riser_to_top(pRow);
    }
    if( pParent ){
      for(pLoop=pParent->pPrev; pLoop && pLoop!=pRow; pLoop=pLoop->pPrev){
        pLoop->railInUse |= mask;
      }
    }
  }

  /*
  ** Insert merge rails and merge arrows
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    for(i=1; i<pRow->nParent; i++){
      int parentRid = pRow->aParent[i];
      pDesc = hashFind(p, parentRid);
      if( pDesc==0 ){
        /* Merge from a node that is off-screen */
        int iMrail = -1;
        for(j=0; j<GR_MAX_RAIL; j++){
          if( mergeRiserFrom[j]==parentRid ){
            iMrail = j;
            break;
          }
        }
        if( iMrail==-1 ){
          iMrail = findFreeRail(p, pRow->idx, p->nRow, 0);
          if( p->mxRail>=GR_MAX_RAIL ) return;
          mergeRiserFrom[iMrail] = parentRid;
        }
        mask = BIT(iMrail);
        pRow->mergeIn[iMrail] = 1;
        pRow->mergeDown |= mask;
        for(pLoop=pRow->pNext; pLoop; pLoop=pLoop->pNext){
          pLoop->railInUse |= mask;
        }
      }else{
        /* Merge from an on-screen node */
        createMergeRiser(p, pDesc, pRow);
        if( p->mxRail>=GR_MAX_RAIL ) return;
      }
    }
  }

  /*
  ** Insert merge rails from primaries to duplicates.
  */
  if( hasDup ){
    int dupRail;
    int mxRail;
    find_max_rail(p);
    mxRail = p->mxRail;
    dupRail = mxRail+1;
    if( p->mxRail>=GR_MAX_RAIL ) return;
    for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
      if( !pRow->isDup ) continue;
      pRow->iRail = dupRail;
      pDesc = hashFind(p, pRow->rid);
      assert( pDesc!=0 && pDesc!=pRow );
      createMergeRiser(p, pDesc, pRow);
      if( pDesc->mergeOut>mxRail ) mxRail = pDesc->mergeOut;
    }
    if( dupRail<=mxRail ){
      dupRail = mxRail+1;
      for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
        if( pRow->isDup ) pRow->iRail = dupRail;
      }
    }
    if( mxRail>=GR_MAX_RAIL ) return;
  }

  /*
  ** Find the maximum rail number.
  */
  find_max_rail(p);
  p->nErr = 0;
}
