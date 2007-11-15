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
** This file contains code used to compute a "diff" between two
** text files.
*/
#include "config.h"
#include "diff2.h"
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
** Break a blob into lines by converting inserting \000 characters.
** Return an array of DLine objects containing a pointer to the
** start of each line and a hash of that line.
**
** Trailing whitespace is removed from each line.
*/
static DLine *break_into_lines(char *z, int *pnLine){
  int nLine, i, j, k, x;
  unsigned int h;
  DLine *a;

  /* Count the number of lines.  Allocate space to hold
  ** the returned array.
  */
  for(i=0, nLine=1; z[i]; i++){
    if( z[i]=='\n' ) nLine++;
  }
  a = malloc( nLine*sizeof(a[0]) );
  if( a==0 ) fossil_panic("out of memory");

  /* Fill in the array */
  for(i=0; i<nLine; i++){
    a[i].z = z;
    for(j=0; z[j] && z[j]!='\n'; j++){}
    for(k=j; k>0 && isspace(z[k-1]); k--){}
    z[k] = 0;
    for(h=0, x=0; x<k; x++){
      h = h ^ (h<<2) ^ z[x];
    }
    a[i].h = h;
    z += j+1;
  }

  /* Return results */
  *pnLine = nLine;
  return a;
}

/*
** Return true if two DLine elements are identical.
*/
static int same_dline(DLine *pA, DLine *pB){
  return pA->h==pB->h && strcmp(pA->z,pB->z)==0;
}

/*
** Generate a report of the differences between files pA and pB.
** The line ending (\r\n versus \n) is ignored - the two line
** endings are considered to be equivalent.
**
** The return is a pointer to an array of integers that describes
** the difference.  Integers come in triples.  For each triple,
** the elements are the number of lines copied, the number of
** lines delete, and the number of lines inserted.  The vector
** is terminated by a triple of all zeros.
**
** The two blobs is destroyed ('\000' values are inserted)
** by the diffing process.  
**
** The core algorithm is a variation on the classic Wagner
** minimum edit distance with enhancements to reduce the runtime
** to be almost linear in the common case where the two files
** have a lot in common.  For additional information see
** Eugene W. Myers, "An O(ND) Difference Algorithm And Its
** Variations"
**
** Consider comparing strings A and B.  A=abcabba and B=cbabac
** We construct a "Wagner" matrix W with A along the X axis and 
** B along the Y axis:
**
**     c 6               *
**     a 5               *
**     b 4           * *
**     a 3         *
**     b 2       *
**   B c 1       *
**       0 * * * 
**         0 1 2 3 4 5 6 7
**           a b c a b b a
**           A
**
** (Note: we draw this Wagner matrix with the origin at the lower 
** left whereas Myers uses the origin at the upper left.  Otherwise,
** they are the same.)
**
** Let Y be the maximum y value or the number of characters in B.
** 6 in this example.  X is the maximum x value or the number of
** elements in A.  Here 7.
**
** Our goal is to find a path from (0,0) to (X,Y).  The path can
** use horizontal, vertical, or diagonal steps.  A diagonal from
** (x-1,y-1) to (x,y) is only allowed if A[x]==B[y].  A vertical
** steps corresponds to an insertion.  A horizontal step corresponds
** to a deletion.  We want to find the path with the fewest
** horizontal and vertical steps.
**
** Diagonal k consists of all points such that x-y==k.  Diagonal
** zero begins at the origin.  Diagonal 1 begins at (1,0).  
** Diagonal -1 begins at (0,1).  All diagonals move up and to the
** right at 45 degrees.  Diagonal number increase from upper left
** to lower right.
** 
** Myers matrix M is a lower right triangular matrix with indices d
** along the bottom and i vertically:
**
** 
**   i=4 |            +4  \
**     3 |         +3 +2   |
**     2 |      +2 +1  0   |- k values.   k = 2*i-d
**     1 |   +1  0 -1 -2   |
**     0 | 0 -1 -2 -3 -4  /
**       ---------------
**         0  1  2  3  4 = d
**
** Each element of the Myers matrix corresponds to a diagonal.
** The diagonal is k=2*i-d.  The diagonal values are shown
** in the template above.
**
** Each entry in M represents the end-point on a path from (0,0).
** The end-point is on diagonal k.  The value stored in M is
** q=x+y where (x,y) is the terminus of the path.  A path
** to M[d][i] will come through either M[d-1][i-1] or
** though M[d-1][i], whichever holds the largest value of q.
** If both elements hold the same value, the path comes
** through M[d-1][i-1].
**
** The value of d is the number of insertions and deletions
** made so far on the path.  M grows progressively.  So the
** size of the M matrix is proportional to d*d.  For the
** common case where A and B are similar, d will be small
** compared to X and Y so little memory is required.  The
** original Wagner algorithm requires X*Y memory, which for
** larger files (100K lines) is more memory than we have at
** hand.
*/
int *text_diff(Blob *pA_Blob, Blob *pB_Blob, Blob *pOut, int nContext){
  DLine *A, *B;    /* Files being compared */
  int X, Y;        /* Number of elements in A and B */
  int x, y;        /* Indices:  A[x] and B[y] */
  int szM = 0;     /* Number of rows and columns in M */
  int **M = 0;     /* Myers matrix */
  int i, d;        /* Indices on M.  M[d][i] */
  int k, q;        /* Diagonal number and distinct from (0,0) */
  int K, D;        /* The diagonal and d for the final solution */          
  int *R;          /* Result vector */
  int r;           /* Loop variables */
  int go = 1;      /* Outer loop control */

  /* Break the two files being diffed into individual lines.
  ** Compute hashes on each line for fast comparison.
  */
  A = break_into_lines(blob_str(pA_Blob), &X);
  B = break_into_lines(blob_str(pB_Blob), &Y);

  szM = 0;
  for(d=0; go; d++){
    if( szM<d+1 ){
      szM += szM + 10;
      M = realloc(M, sizeof(M[0])*szM);
      if( M==0 ){
        fossil_panic("out of memory");
      }
    }
    M[d] = malloc( sizeof(M[d][0])*(d+1) );
    if( M[d]==0 ){
      fossil_panic("out of memory");
    }
    for(i=0; i<=d; i++){
      k = 2*i - d;
      if( d==0 ){
        q = 0;
      }else if( i==0 ){
        q = M[d-1][0];
      }else if( M[d-1][i-1] < M[d-1][i] ){
        q = M[d-1][i];
      }else{
        q = M[d-1][i-1];
      }
      x = (k + q + 1)/2;
      y = x - k;
      while( x<X && y<Y && same_dline(&A[x],&B[y]) ){ x++; y++; }
      M[d][i] = x + y;
      if( x==X && y==Y ){
        go = 0;
        break;
      }
    }
  }

  /* Reuse M[] as follows:
  **
  **     M[d][1] = 1 if a line is inserted or 1 if a line is deleted.
  **     M[d][0] = number of lines copied at this step.
  **
  */
  D = d - 1;
  K = X - Y;
  for(d=D, i=(K+D)/2; d>0; d--){
    if( i==d || M[d-1][i-1] > M[d-1][i] ){
      M[d][0] = M[d][i] - M[d-1][i-1] - 1;
      M[d][1] = 0;
      i--;
    }else{
      M[d][0] = M[d][i] - M[d-1][i] - 1;
      M[d][1] = 1;
    }
  }

  /* Allocate the output vector
  */
  R = malloc( sizeof(R[0])*(D+2)*3 );
  if( R==0 ){
    fossil_fatal("out of memory");
  }

  /* Populate the output vector
  */
  d = r = 0;
  while( d<=D ){
    int n;
    R[r++] = M[d++][0]/2;   /* COPY */
    if( d>D ){
      R[r++] = 0;
      R[r++] = 0;
      break;
    }
    if( M[d][1]==0 ){
      n = 1;
      while( M[d][0]==0 && d<D && M[d+1][1]==0 ){
        d++;
        n++;
      }
      R[r++] = n;           /* DELETE */
      if( d==D || M[d][0]>0 ){
        R[r++] = 0;         /* INSERT */
        continue;
      }
      d++;
    }else{
      R[r++] = 0;           /* DELETE */
    }
    assert( M[d][1]==1 );
    n = 1;
    while( M[d][0]==0 && d<D && M[d+1][1]==1 ){
      d++;
      n++;
    }
    R[r++] = n;            /* INSERT */
  }
  R[r++] = 0;
  R[r++] = 0;
  R[r++] = 0;

  /* Free the Myers matrix */
  for(d=0; d<=D; d++){
    free(M[d]);
  }
  free(M);

  /* If pOut is defined, construct a unified diff into pOut and
  ** delete R
  */
  if( pOut ){
    int a = 0;    /* Index of next line in A[] */
    int b = 0;    /* Index of next line in B[] */
    int nr;       /* Number of COPY/DELETE/INSERT triples to process */
    int na, nb;   /* Number of lines shown from A and B */
    int i, j;     /* Loop counters */
    int m;        /* Number of lines to output */
    int skip;     /* Number of lines to skip */

    for(r=0; R[r+3]; r += 3*nr){
      /* Figure out how many triples to show in a single block */
      for(nr=1; R[r+nr*3]>0 && R[r+nr*3]<nContext*2; nr++){}

      /* For the current block comprising nr triples, figure out
      ** how many lines of A and B are to be displayed
      */
      if( R[r]>nContext ){
        na = nb = nContext;
        skip = R[r] - nContext;
      }else{
        na = nb = R[r];
        skip = 0;
      }
      for(i=0; i<nr; i++){
        na += R[r+i*3+1];
        nb += R[r+i*3+2];
      }
      if( R[r+i*3]>nContext ){
        na += nContext;
        nb += nContext;
      }else{
        na += R[r+i*3];
        nb += R[r+i*3];
      }
      for(i=1; i<nr; i++){
        na += R[r+i*3];
        nb += R[r+i*3];
      }
      blob_appendf(pOut,"@@ -%d,%d +%d,%d @@\n", a+skip+1, na, b+skip+1, nb);

      /* Show the initial common area */
      a += skip;
      b += skip;
      m = R[r] - skip;
      for(j=0; j<m; j++){
        blob_appendf(pOut," %s\n", A[a+j].z);
      }
      a += m;
      b += m;

      /* Show the differences */
      for(i=0; i<nr; i++){
        m = R[r+i*3+1];
        for(j=0; j<m; j++){
          blob_appendf(pOut,"-%s\n", A[a+j].z);
        }
        a += m;
        m = R[r+i*3+2];
        for(j=0; j<m; j++){
          blob_appendf(pOut,"+%s\n", B[b+j].z);
        }
        b += m;
        if( i<nr-1 ){
          m = R[r+i*3+3];
          for(j=0; j<m; j++){
            blob_appendf(pOut," %s\n", B[b+j].z);
          }
          b += m;
          a += m;
        }
      }

      /* Show the final common area */
      assert( nr==i );
      m = R[r+nr*3];
      if( m>nContext ) m = nContext;
      for(j=0; j<m; j++){
        blob_appendf(pOut," %s\n", B[b+j].z);
      }
    }
    free(R);
    R = 0;
  }

  /* We no longer need the A[] and B[] vectors */
  free(A);
  free(B);

  /* Return the result */
  return R;
}

/*
** COMMAND: test-rawdiff
*/
void test_rawdiff_cmd(void){
  Blob a, b;
  int r;
  int *R;
  if( g.argc!=4 ) usage("FILE1 FILE2");
  blob_read_from_file(&a, g.argv[2]);
  blob_read_from_file(&b, g.argv[3]);
  R = text_diff(&a, &b, 0, 0);
  for(r=0; R[r] || R[r+1] || R[r+2]; r += 3){
    printf(" copy %4d  delete %4d  insert %4d\n", R[r], R[r+1], R[r+2]);
  }
  free(R);
}

/*
** COMMAND: test-udiff
*/
void test_udiff_cmd(void){
  Blob a, b, out;
  if( g.argc!=4 ) usage("FILE1 FILE2");
  blob_read_from_file(&a, g.argv[2]);
  blob_read_from_file(&b, g.argv[3]);
  blob_zero(&out);
  text_diff(&a, &b, &out, 3);
  blob_write_to_file(&out, "-");
}
