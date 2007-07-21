/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to implement "diff" operators.
*/
#include "config.h"
#include "diff.h"
#include <assert.h>

/*
** Information about each line of a file being diffed.
*/
typedef struct DLine DLine;
struct DLine {
  const char *z;    /* The text of the line */
  unsigned int h;   /* Hash of the line */
};

/*
** Break a blob into lines by converting each \n into a \000 and
** creating pointers to the beginning of each line.
*/
static DLine *break_into_lines(char *z, int *pnLine){
  int nLine, i, j;
  unsigned int h;
  DLine *a;
  for(i=0, nLine=1; z[i]; i++){
    if( z[i]=='\n' ) nLine++;
  }
  a = malloc( nLine*sizeof(a[0]) );
  if( a==0 ) fossil_panic("out of memory");
  a[0].z = z;
  for(i=0, j=0, h=0; z[i]; i++){
    if( z[i]=='\n' ){
      a[j].h = h;
      j++;
      a[j].z = &z[i+1];
      z[i] = 0;
      h = 0;
    }else{
      h = h ^ (h<<2) ^ z[i];
    }
  }
  a[j].h = h;
  *pnLine = j+1;
  return a;
}

/*
** Return true if two DLine elements are identical.
*/
static int same_dline(DLine *pA, DLine *pB){
  return pA->h==pB->h && strcmp(pA->z,pB->z)==0;
}

/*
** Generate a unified diff of two blobs.  The text of the original
** two blobs is destroyed by the diffing process.
*/
void unified_diff(Blob *pA, Blob *pB, int nContext, Blob *pOut){
  DLine *pDA, *pDB, *A, *B;
  int nA, nB, nAp1;
  int x, y;
  int cnt;
  int i, iStart;
  int *m;

  /* Break the two files being diffed into individual lines.
  ** Compute hashes on each line for fast comparison.
  */
  pDA = break_into_lines(blob_str(pA), &nA);
  pDB = break_into_lines(blob_str(pB), &nB);

  /* Remove common prefix and suffix to help reduce the value
  ** of N in the O(N^2) minimum edit distance algorithm.
  */
  for(i=0; i<nA && i<nB && same_dline(&pDA[i],&pDB[i]); i++){}
  i -= nContext;
  if( i<0 ) i = 0;
  iStart = i;
  A = &pDA[iStart];
  B = &pDB[iStart];
  nA -= iStart;
  nB -= iStart;
  for(i=1; i<nA && i<nB && same_dline(&A[nA-i],&B[nB-i]); i++){}
  i -= nContext;
  if( i<1 ) i = 1;
  i--;
  nA -= i;
  nB -= i;
  
  /* Create the matrix used for the minimum edit distance
  ** calculation.
  */
  nAp1 = nA + 1;
  m = malloc( sizeof(m[0])*(nB+1)*nAp1 );
# define M(X,Y) m[(Y)*nAp1+(X)]


  /* Find the minimum edit distance using Wagner's algorithm.
  */
  for(x=0; x<=nA; x++){
    M(x,0) = x;
  }
  for(y=0; y<=nB; y++){
    M(0,y) = y;
  }
  for(x=1; x<=nA; x++){
    for(y=1; y<=nB; y++){
      int e = M(x-1,y) + 1;
      if( e>M(x,y-1)+1 ){
        e = M(x,y-1)+1;
      }
      if( e<=M(x-1,y-1) ){
        M(x,y) = e;
      }else if( same_dline(&A[x-1], &B[y-1]) ){
        M(x,y) = M(x-1,y-1);
      }else{
        M(x,y) = e;
      }
    }
  }

  /* Walk backwards through the Wagner algorithm matrix to determine
  ** the specific edits that give the minimum edit distance.  Mark our
  ** path through the matrix with -1.
  */
  x = nA;
  y = nB;
  while( x>0 || y>0 ){
    int v = M(x,y);
    M(x,y) = -1;
    if( x==0 ){
      y--;
    }else if( y==0 ){
      x--;
    }else if( M(x,y-1)+1==v ){
      y--;
    }else if( M(x-1,y)+1==v ){
      x--;
    }else{
      x--;
      y--;
    }
  }

#if 0
for(y=0; y<=nB; y++){
  for(x=0; x<=nA; x++){
    printf(" %2d", M(x,y));
  }
  printf("\n");
}
#endif

  x = y = 0;
  cnt = nContext;
  while( x<nA || y<nB ){
    int t1, t2;
    if( (t1 = M(x+1,y))<0 || (t2 = M(x,y+1))<0 ){
      if( cnt>=nContext ){
        blob_appendf(pOut, "@@ -%d +%d @@\n", 
            x-nContext+iStart+2, y-nContext+iStart+2);
        for(i=x-nContext+1; i<x; i++){
          if( i<0 ) continue;
          blob_appendf(pOut, " %s\n", A[i].z);
        }
      }
    }
    if( t1<0 ){
      blob_appendf(pOut, "-%s\n", A[x].z);
      x++;
      cnt = 0;
    }else if( t2<0 ){
      blob_appendf(pOut, "+%s\n", B[y].z);
      y++;
      cnt = 0;
    }else{
      if( M(x+1,y+1)==(-1) && cnt<nContext ){
        blob_appendf(pOut, " %s\n", A[x].z);
      }
      cnt++;
      x++;
      y++;
    }
  }

  /* Cleanup allocationed memory */
  free(m);
  free(pDA);
  free(pDB);
}

/*
** COMMAND: test-diff
*/
void test_diff_cmd(void){
  Blob a, b, out;
  if( g.argc!=4 ) usage("FILE1 FILE2");
  blob_read_from_file(&a, g.argv[2]);
  blob_read_from_file(&b, g.argv[3]);
  blob_zero(&out);
  unified_diff(&a, &b, 4, &out);
  blob_reset(&a);
  blob_reset(&b);
  printf("%s", blob_str(&out));
  blob_reset(&out);
}
/*
** COMMAND: test-uuid-diff
*/
void test_uuiddiff_cmd(void){
  Blob a, b, out;
  int ridA, ridB;
  if( g.argc!=4 ) usage("UUID2 UUID1");
  db_must_be_within_tree();
  ridA = name_to_rid(g.argv[2]);
  content_get(ridA, &a);
  ridB = name_to_rid(g.argv[3]);
  content_get(ridB, &b);
  blob_zero(&out);
  unified_diff(&a, &b, 4, &out);
  blob_reset(&a);
  blob_reset(&b);
  printf("%s", blob_str(&out));
  blob_reset(&out);
}
