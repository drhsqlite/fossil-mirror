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
  int nParent;                /* Number of parents */
  int aParent[GR_MAX_PARENT]; /* Array of parents.  0 element is primary .*/
  char *zBranch;              /* Branch name */
  char *zBgClr;               /* Background Color */

  GraphRow *pNext;            /* Next row down in the list of all rows */
  GraphRow *pPrev;            /* Previous row */
  
  int idx;                    /* Row index.  First is 1.  0 used for "none" */
  u8 isLeaf;                  /* True if no direct child nodes */
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
  int nErr;             /* Number of errors encountered */
  int mxRail;           /* Number of rails required to render the graph */
  GraphRow *pFirst;     /* First row in the list */
  GraphRow *pLast;      /* Last row in the list */
  int nBranch;          /* Number of distinct branches */
  char **azBranch;      /* Names of the branches */
  int nRow;                  /* Number of rows */
  int railMap[GR_MAX_RAIL];  /* Rail order mapping */
  int nHash;                 /* Number of slots in apHash[] */
  GraphRow **apHash;         /* Hash table of rows */
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
  free(p->apHash);
  free(p);
}

/*
** Insert a row into the hash table.  If there is already another
** row with the same rid, overwrite the prior entry if the overwrite
** flag is set.
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
** Note: also used for background color names.
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
  pRow->idx = p->nRow;
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
** Compute the complete graph
*/
void graph_finish(GraphContext *p, int omitDescenders){
  GraphRow *pRow, *pDesc, *pDup, *pLoop;
  int i;
  u32 mask;
  u32 inUse;
  int hasDup = 0;    /* True if one or more isDup entries */

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

  /* Purge merge-parents that are out-of-graph
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    for(i=1; i<pRow->nParent; i++){
      if( hashFind(p, pRow->aParent[i])==0 ){
        pRow->aParent[i] = pRow->aParent[--pRow->nParent];
        i--;
      }
    }
  }

  /* Figure out which nodes have no direct children (children on
  ** the same rail).  Mark such nodes as isLeaf.
  */
  memset(p->apHash, 0, sizeof(p->apHash[0])*p->nHash);
  for(pRow=p->pLast; pRow; pRow=pRow->pPrev) pRow->isLeaf = 1;
  for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
    GraphRow *pParent;
    hashInsert(p, pRow, 0);
    if( !pRow->isDup
     && pRow->nParent>0 
     && (pParent = hashFind(p, pRow->aParent[0]))!=0
     && pRow->zBranch==pParent->zBranch
    ){
      pParent->isLeaf = 0;
    }
  }

  /* Identify rows where the primary parent is off screen.  Assign
  ** each to a rail and draw descenders to the bottom of the screen.
  */
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->nParent==0 || hashFind(p,pRow->aParent[0])==0 ){
      if( omitDescenders ){
        pRow->iRail = findFreeRail(p, pRow->idx, pRow->idx, 0, 0);
      }else{
        pRow->iRail = ++p->mxRail;
      }
      mask = 1<<(pRow->iRail);
      if( omitDescenders ){
        pRow->railInUse |= mask;
        if( pRow->pNext ) pRow->pNext->railInUse |= mask;
      }else{
        pRow->bDescender = pRow->nParent>0;
        for(pDesc=pRow; pDesc; pDesc=pDesc->pNext){
          pDesc->railInUse |= mask;
        }
      }
    }
  }

  /* Assign rails to all rows that are still unassigned.
  ** The first primary child of a row goes on the same rail as
  ** that row.
  */
  inUse = (1<<(p->mxRail+1))-1;
  for(pRow=p->pLast; pRow; pRow=pRow->pPrev){
    int parentRid;
    if( pRow->iRail>=0 ) continue;
    if( pRow->isDup ){
      pRow->iRail = findFreeRail(p, pRow->idx, pRow->idx, inUse, 0);
      pDesc = pRow;
    }else{
      assert( pRow->nParent>0 );
      parentRid = pRow->aParent[0];
      pDesc = hashFind(p, parentRid);
      if( pDesc==0 ){
        /* Time skew */
        pRow->iRail = ++p->mxRail;
        pRow->railInUse = 1<<pRow->iRail;
        continue;
      }
      if( pDesc->aiRaiser[pDesc->iRail]==0 && pDesc->zBranch==pRow->zBranch ){
        pRow->iRail = pDesc->iRail;
      }else{
        pRow->iRail = findFreeRail(p, 0, pDesc->idx, inUse, pDesc->iRail);
      }
      pDesc->aiRaiser[pRow->iRail] = pRow->idx;
    }
    mask = 1<<pRow->iRail;
    if( pRow->isLeaf ){
      inUse &= ~mask;
    }else{
      inUse |= mask;
    }
    for(pLoop=pRow; pLoop && pLoop!=pDesc; pLoop=pLoop->pNext){
      pLoop->railInUse |= mask;
    }
    pDesc->railInUse |= mask;
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
        for(pDesc=pRow->pNext; pDesc && pDesc->rid!=parentRid;
             pDesc=pDesc->pNext){
          pDesc->railInUse |= mask;
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
  for(i=0; i<GR_MAX_RAIL; i++) p->railMap[i] = i;
  p->mxRail = 0;
  for(pRow=p->pFirst; pRow; pRow=pRow->pNext){
    if( pRow->iRail>p->mxRail ) p->mxRail = pRow->iRail;
    if( pRow->mergeOut>p->mxRail ) p->mxRail = pRow->mergeOut;
  }
}
