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

#define GR_MAX_PARENT 10      /* Most parents for any one node */
#define GR_MAX_RAIL   32      /* Max number of "rails" to display */

/* The graph appears vertically beside a timeline.  Each row in the
** timeline corresponds to a row in the graph.
*/
struct GraphRow {
  int rid;                    /* The rid for the check-in */
  int nParent;                /* Number of parents */
  int aParent[GR_MAX_PARENT]; /* Array of parents.  0 element is primary .*/
  char *zBranch;              /* Branch name */
  char *zBgClr;               /* Background Color */

  GraphRow *pNext;            /* Next row down in the list of all rows */
  GraphRow *pPrev;            /* Previous row */
  
  int idx;                    /* Row index.  First is 1.  0 used for "none" */
  int idxTop;                 /* Direct descendent highest up on the graph */
  GraphRow *pChild;           /* Child immediately above this node */
  u8 isDup;                   /* True if this is duplicate of a prior entry */
  int iRail;                  /* Which rail this check-in appears on. 0-based.*/
  int aiRaiser[GR_MAX_RAIL];  /* Raisers from this node to a higher row. */
  int bDescender;             /* Raiser from bottom of graph to here. */
  u32 mergeIn;                /* Merge in from other rails */
  int mergeOut;               /* Merge out to this rail */
  int mergeUpto;              /* Draw the merge rail up to this level */

  u32 railInUse;              /* Mask of occupied rails */
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
** Destroy a GraphContext;
*/
void graph_free(GraphContext *p){
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
    if( strcmp(zBranch, p->azBranch[i])==0 ) return p->azBranch[i];
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
  const char *zBgClr   /* Background color. NULL or "" for white. */
){
  GraphRow *pRow;

  if( p->nErr ) return 0;
  if( nParent>GR_MAX_PARENT ){ p->nErr++; return 0; }
  pRow = (GraphRow*)safeMalloc( sizeof(GraphRow) );
  pRow->rid = rid;
  pRow->nParent = nParent;
  pRow->zBranch = persistBranchName(p, zBranch);
  if( zBgClr==0 || zBgClr[0]==0 ) zBgClr = "white";
  pRow->zBgClr = persistBranchName(p, zBgClr);
  memcpy(pRow->aParent, aParent, sizeof(aParent[0])*nParent);
  if( p->pFirst==0 ){
    p->pFirst = pRow;
  }else{
    p->pLast->pNext = pRow;
  }
  p->pLast = pRow;
  p->nRow++;
  pRow->idx = pRow->idxTop = p->nRow;
  return pRow->idx;
}

/*
** Return the index of a rail currently not in use for any row between
** top and bottom, inclusive.  
*/
static int findFreeRail(
  GraphContext *p,         /* The graph context */
  int top, int btm,        /* Span of rows for which the rail is needed */
  u32 inUseMask,           /* Mask or rails already in use */
  int iNearto              /* Find rail nearest to this rail */
){
  GraphRow *pRow;
  int i;
  int iBest = 0;
  int iBestDist = 9999;
  for(pRow=p->pFirst; pRow && pRow->idx<top; pRow=pRow->pNext){}
  while( pRow && pRow->idx<=btm ){
    inUseMask |= pRow->railInUse;
    pRow = pRow->pNext;
  }
  for(i=0; i<32; i++){
    if( (inUseMask & (1<<i))==0 ){
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
  return iBest;
}

/*
** Assign all children of node pBottom to the same rail as pBottom.
*/
static void assignChildrenToRail(GraphRow *pBottom){
  int iRail = pBottom->iRail;
  GraphRow *pCurrent;
  GraphRow *pPrior;
  u32 mask = 1<<iRail;

  pBottom->iRail = iRail;
  pBottom->railInUse |= mask;
  pPrior = pBottom;
  for(pCurrent=pBottom->pChild; pCurrent; pCurrent=pCurrent->pChild){
    assert( pPrior->idx > pCurrent->idx );
    assert( pCurrent->iRail<0 );
    pCurrent->iRail = iRail;
    pCurrent->railInUse |= mask;
    pPrior->aiRaiser[iRail] = pCurrent->idx;
    while( pPrior->idx > pCurrent->idx ){
      pPrior->railInUse |= mask;
      pPrior = pPrior->pPrev;
      assert( pPrior!=0 );
    }
    if( pCurrent->pPrev ){
      pCurrent->pPrev->railInUse |= mask;
    }
  }
}


/*
** Compute the complete graph
*/
void graph_finish(GraphContext *p, int omitDescenders){
  GraphRow *pRow, *pDesc, *pDup, *pLoop, *pParent;
  int i;
  u32 mask;
  u32 inUse;
  int hasDup = 0;    /* True if one or more isDup entries */
  const char *zTrunk;

  if( p==0 || p->pFirst==0 || p->nErr ) return;

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

  /* Purge merge-parents that are out-of-graph.
  **
  ** Each node has one primary parent and zero or more "merge" parents.
  ** A merge parent is a prior checkin from which changes were merged into
  ** the current check-in.  If a merge parent is not in the visible section
  ** of this graph, then no arrows will be drawn for it, so remove it from
  ** the aParent[] array.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    for(i=1; i<pRow->nParent; i++){
      if( hashFind(p, pRow->aParent[i])==0 ){
        pRow->aParent[i] = pRow->aParent[--pRow->nParent];
        i--;
      }
    }
  }

  /* Find the pChild pointer for each node. 
  **
  ** The pChild points to node directly above on the same rail.
  ** The pChild must be in the same branch.  Leaf nodes have a NULL
  ** pChild.
  **
  ** In the case of a fork, choose the pChild that results in the
  ** longest rail.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->isDup ) continue;
    if( pRow->nParent==0 ) continue;
    pParent = hashFind(p, pRow->aParent[0]);
    if( pParent==0 ) continue;
    if( pParent->zBranch!=pRow->zBranch ) continue;
    if( pParent->idx <= pRow->idx ) continue;
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
      if( i==0 ){
        if( pRow->zBranch!=zTrunk ) continue;
      }else {
        if( pRow->iRail>=0 ) continue;
      }
      if( pRow->nParent==0 || hashFind(p,pRow->aParent[0])==0 ){
        if( omitDescenders ){
          pRow->iRail = findFreeRail(p, pRow->idxTop, pRow->idx, 0, 0);
        }else{
          pRow->iRail = ++p->mxRail;
        }
        mask = 1<<(pRow->iRail);
        if( omitDescenders ){
          if( pRow->pNext ) pRow->pNext->railInUse |= mask;
        }else{
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
  inUse = (1<<(p->mxRail+1))-1;
  for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
    int parentRid;

    if( pRow->iRail>=0 ){
      if( pRow->pChild==0 ) inUse &= ~(1<<pRow->iRail);
      continue;
    }
    if( pRow->isDup ){
      pRow->iRail = findFreeRail(p, pRow->idx, pRow->idx, inUse, 0);
      pDesc = pRow;
      pParent = 0;
    }else{
      assert( pRow->nParent>0 );
      parentRid = pRow->aParent[0];
      pParent = hashFind(p, parentRid);
      if( pParent==0 ){
        /* Time skew */
        pRow->iRail = ++p->mxRail;
        pRow->railInUse = 1<<pRow->iRail;
        continue;
      }
      pRow->iRail = findFreeRail(p, 0, pParent->idx, inUse, pParent->iRail);
      pParent->aiRaiser[pRow->iRail] = pRow->idx;
    }
    mask = 1<<pRow->iRail;
    if( pRow->pPrev ) pRow->pPrev->railInUse |= mask;
    if( pRow->pNext ) pRow->pNext->railInUse |= mask;
    if( pRow->pChild==0 ){
      inUse &= ~mask;
    }else{
      inUse |= mask;
      assignChildrenToRail(pRow);
    }
    if( pParent ){
      for(pLoop=pParent; pLoop && pLoop!=pRow; pLoop=pLoop->pPrev){
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
      if( pDesc==0 ) continue;
      if( pDesc->mergeOut<0 ){
        int iTarget = (pRow->iRail + pDesc->iRail)/2;
        pDesc->mergeOut = findFreeRail(p, pRow->idx, pDesc->idx, 0, iTarget);
        pDesc->mergeUpto = pRow->idx;
        mask = 1<<pDesc->mergeOut;
        pDesc->railInUse |= mask;
        for(pLoop=pRow->pNext; pLoop && pLoop->rid!=parentRid;
             pLoop=pLoop->pNext){
          pLoop->railInUse |= mask;
        }
      }
      pRow->mergeIn |= 1<<pDesc->mergeOut;
    }
  }

  /*
  ** Insert merge rails from primaries to duplicates. 
  */
  if( hasDup ){
    for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
      if( !pRow->isDup ) continue;
      pDesc = hashFind(p, pRow->rid);
      assert( pDesc!=0 && pDesc!=pRow );
      if( pDesc->mergeOut<0 ){
        int iTarget = (pRow->iRail + pDesc->iRail)/2;
        pDesc->mergeOut = findFreeRail(p, pRow->idx, pDesc->idx, 0, iTarget);
        pDesc->mergeUpto = pRow->idx;
        mask = 1<<pDesc->mergeOut;
        pDesc->railInUse |= mask;
        for(pLoop=pRow->pNext; pLoop && pLoop!=pDesc; pLoop=pLoop->pNext){
          pLoop->railInUse |= mask;
        }
      }
      pRow->mergeIn |= 1<<pDesc->mergeOut;
    }
  }

  /*
  ** Find the maximum rail number.
  */
  p->mxRail = 0;
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->iRail>p->mxRail ) p->mxRail = pRow->iRail;
    if( pRow->mergeOut>p->mxRail ) p->mxRail = pRow->mergeOut;
  }
}
