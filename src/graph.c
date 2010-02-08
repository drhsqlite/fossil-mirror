/*
** Copyright (c) 2010 D. Richard Hipp
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
** This file contains code to compute a revision history graph.
*/
#include "config.h"
#include "graph.h"
#include <assert.h>

#if INTERFACE

#define GR_MAX_PARENT 10
#define GR_MAX_RAIL   32

/* The graph appears vertically beside a timeline.  Each row in the
** timeline corresponds to a row in the graph.
*/
struct GraphRow {
  int rid;                    /* The rid for the check-in */
  int isLeaf;                 /* True if the check-in is an open leaf */
  int nParent;                /* Number of parents */
  int aParent[GR_MAX_PARENT]; /* Array of parents.  0 element is primary .*/
  char *zBranch;              /* Branch name */

  GraphRow *pNext;            /* Next row down in the list of all rows */
  GraphRow *pPrev;            /* Previous row */
  
  int idx;                    /* Row index.  First is 1.  0 used for "none" */
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
  int nErr;             /* Number of errors encountered */
  int mxRail;           /* Number of rails required to render the graph */
  GraphRow *pFirst;     /* First row in the list */
  GraphRow *pLast;      /* Last row in the list */
  int nBranch;          /* Number of distinct branches */
  char **azBranch;      /* Names of the branches */
  int railMap[GR_MAX_RAIL];  /* Rail order mapping */
};

#endif

/*
** Malloc for zeroed space.  Panic if unable to provide the
** requested space.
*/
void *safeMalloc(int nByte){
  void *p = malloc(nByte);
  if( p==0 ) fossil_panic("out of memory");
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
  free(p);
}

/*
** Return the canonical pointer for a given branch name.
** Multiple calls to this routine with equivalent strings
** will return the same pointer.
*/
static char *persistBranchName(GraphContext *p, const char *zBranch){
  int i;
  for(i=0; i<p->nBranch; i++){
    if( strcmp(zBranch, p->azBranch[i])==0 ) return p->azBranch[i];
  }
  p->nBranch++;
  p->azBranch = realloc(p->azBranch, sizeof(char*)*p->nBranch);
  if( p->azBranch==0 ) fossil_panic("out of memory");
  p->azBranch[p->nBranch-1] = mprintf("%s", zBranch);
  return p->azBranch[p->nBranch-1];
}

/*
** Add a new row t the graph context.  Rows are added from top to bottom.
*/
void graph_add_row(
  GraphContext *p,     /* The context to which the row is added */
  int rid,             /* RID for the check-in */
  int isLeaf,          /* True if the check-in is an leaf */
  int nParent,         /* Number of parents */
  int *aParent,        /* Array of parents */
  const char *zBranch  /* Branch for this check-in */
){
  GraphRow *pRow;

  if( p->nErr ) return;
  if( nParent>GR_MAX_PARENT ){ p->nErr++; return; }
  pRow = (GraphRow*)safeMalloc( sizeof(GraphRow) );
  pRow->rid = rid;
  pRow->isLeaf = isLeaf;
  pRow->nParent = nParent;
  pRow->zBranch = persistBranchName(p, zBranch);
  memcpy(pRow->aParent, aParent, sizeof(aParent[0])*nParent);
  if( p->pFirst==0 ){
    p->pFirst = pRow;
  }else{
    p->pLast->pNext = pRow;
  }
  p->pLast = pRow;
}

/*
** Return the index of a rail currently not in use for any row between
** top and bottom, inclusive.  
*/
static int findFreeRail(GraphContext *p, int top, int btm, u32 inUseMask){
  GraphRow *pRow;
  int i;
  for(pRow=p->pFirst; pRow && pRow->idx<top; pRow=pRow->pNext){}
  while( pRow && pRow->idx<=btm ){
    inUseMask |= pRow->railInUse;
    pRow = pRow->pNext;
  }
  for(i=0; i<32; i++){
    if( (inUseMask & (1<<i))==0 ) return i;
  }
  p->nErr++;
  return 0;
}

/*
** Compute the complete graph
*/
void graph_finish(GraphContext *p){
  GraphRow *pRow, *pDesc;
  Bag allRids;
  int i;
  int nRow;
  u32 mask;
  u32 inUse;

  if( p==0 || p->pFirst==0 || p->nErr ) return;

  /* Initialize all rows */
  bag_init(&allRids);
  nRow = 0;
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->pNext ) pRow->pNext->pPrev = pRow;
    pRow->idx = ++nRow;
    pRow->iRail = -1;
    pRow->mergeOut = -1;
    bag_insert(&allRids, pRow->rid);
  }
  p->mxRail = -1;

  /* Purge merge-parents that are out-of-graph
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    for(i=1; i<pRow->nParent; i++){
      if( !bag_find(&allRids, pRow->aParent[i]) ){
        pRow->aParent[i] = pRow->aParent[--pRow->nParent];
        i--;
      }
    }
  }

  /* Identify rows where the primary parent is off screen.  Assign
  ** each to a rail and draw descenders to the bottom of the screen.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->nParent==0 || !bag_find(&allRids,pRow->aParent[0]) ){
      pRow->iRail = ++p->mxRail;
      pRow->bDescender = pRow->nParent>0;
      mask = 1<<(pRow->iRail);
      for(pDesc=pRow; pDesc; pDesc=pDesc->pNext){
        pDesc->railInUse |= mask;
      }
    }
  }

  /* Assign rails to all rows that are still unassigned.
  ** The first primary child of a row goes on the same rail as
  ** that row.
  */
  inUse = 0;
  for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
    int parentRid;
    if( pRow->iRail>=0 ) continue;
    assert( pRow->nParent>0 );
    parentRid = pRow->aParent[0];
    assert( bag_find(&allRids, parentRid) );
    for(pDesc=pRow->pNext; pDesc && pDesc->rid!=parentRid; pDesc=pDesc->pNext){}
    assert( pDesc!=0 );
    if( pDesc->aiRaiser[pDesc->iRail]==0 && pDesc->zBranch==pRow->zBranch ){
      pRow->iRail = pDesc->iRail;
    }else{
      pRow->iRail = findFreeRail(p, 0, pDesc->idx, inUse);
    }
    pDesc->aiRaiser[pRow->iRail] = pRow->idx;
    mask = 1<<pRow->iRail;
    if( pRow->isLeaf ){
      inUse &= ~mask;
    }else{
      inUse |= mask;
    }
    for(pDesc = pRow; ; pDesc=pDesc->pNext){
      assert( pDesc!=0 );
      pDesc->railInUse |= mask;
      if( pDesc->rid==parentRid ) break;
    }
  }

  /*
  ** Insert merge rails and merge arrows
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    for(i=1; i<pRow->nParent; i++){
      int parentRid = pRow->aParent[i];
      for(pDesc=pRow->pNext; pDesc && pDesc->rid!=parentRid;
          pDesc=pDesc->pNext){}
      if( pDesc==0 ) continue;
      if( pDesc->mergeOut<0 ){
        pDesc->mergeOut = findFreeRail(p, pRow->idx, pDesc->idx, 0);
        pDesc->mergeUpto = pRow->idx;
      }
      pRow->mergeIn |= 1<<pDesc->mergeOut;
    }
  }

  /*
  ** Sort the rail numbers
  */
#if 0
  p->mxRail = -1;
  mask = 0;
  for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
    if( (mask & (1<<pRow->iRail))==0 ){
      p->railMap[pRow->iRail] = ++p->mxRail;
      mask |= 1<<pRow->iRail;
    }
    if( pRow->mergeOut>=0 && (mask & (1<<pRow->mergeOut))==0 ){
      p->railMap[pRow->mergeOut] = ++p->mxRail;
      mask |= 1<<pRow->mergeOut;
    }
  }
#else
  for(i=0; i<GR_MAX_RAIL; i++) p->railMap[i] = i;
  p->mxRail = 0;
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->iRail>p->mxRail ) p->mxRail = pRow->iRail;
    if( pRow->mergeOut>p->mxRail ) p->mxRail = pRow->mergeOut;
  }
#endif
}
