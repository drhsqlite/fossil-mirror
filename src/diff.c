/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to compute a "diff" between two
** text files.
*/
#include "config.h"
#include "diff.h"
#include <assert.h>


/*
** Maximum length of a line in a text file.  (8192)
*/
#define LENGTH_MASK_SZ  13
#define LENGTH_MASK     ((1<<LENGTH_MASK_SZ)-1)

/*
** Information about each line of a file being diffed.
**
** The lower LENGTH_MASK_SZ bits of the hash (DLine.h) are the length
** of the line.  If any line is longer than LENGTH_MASK characters,
** the file is considered binary.
*/
typedef struct DLine DLine;
struct DLine {
  const char *z;        /* The text of the line */
  unsigned int h;       /* Hash of the line */
  unsigned int iNext;   /* 1+(Index of next line with same the same hash) */

  /* an array of DLine elements services two purposes.  The fields
  ** above are one per line of input text.  But each entry is also
  ** a bucket in a hash table, as follows: */
  unsigned int iHash;   /* 1+(first entry in the hash chain) */
};

/*
** A context for running a diff.
*/
typedef struct DContext DContext;
struct DContext {
  int *aEdit;        /* Array of copy/delete/insert triples */
  int nEdit;         /* Number of integers (3x num of triples) in aEdit[] */
  int nEditAlloc;    /* Space allocated for aEdit[] */
  DLine *aFrom;      /* File on left side of the diff */
  int nFrom;         /* Number of lines in aFrom[] */
  DLine *aTo;        /* File on right side of the diff */
  int nTo;           /* Number of lines in aTo[] */
};

/*
** Return an array of DLine objects containing a pointer to the
** start of each line and a hash of that line.  The lower 
** bits of the hash store the length of each line.
**
** Trailing whitespace is removed from each line.  2010-08-20:  Not any
** more.  If trailing whitespace is ignored, the "patch" command gets
** confused by the diff output.  Ticket [a9f7b23c2e376af5b0e5b]
**
** Return 0 if the file is binary or contains a line that is
** too long.
*/
static DLine *break_into_lines(const char *z, int n, int *pnLine, int ignoreWS){
  int nLine, i, j, k, x;
  unsigned int h, h2;
  DLine *a;

  /* Count the number of lines.  Allocate space to hold
  ** the returned array.
  */
  for(i=j=0, nLine=1; i<n; i++, j++){
    int c = z[i];
    if( c==0 ){
      return 0;
    }
    if( c=='\n' && z[i+1]!=0 ){
      nLine++;
      if( j>LENGTH_MASK ){
        return 0;
      }
      j = 0;
    }
  }
  if( j>LENGTH_MASK ){
    return 0;
  }
  a = fossil_malloc( nLine*sizeof(a[0]) );
  memset(a, 0, nLine*sizeof(a[0]) );
  if( n==0 ){
    *pnLine = 0;
    return a;
  }

  /* Fill in the array */
  for(i=0; i<nLine; i++){
    a[i].z = z;
    for(j=0; z[j] && z[j]!='\n'; j++){}
    k = j;
    while( ignoreWS && k>0 && fossil_isspace(z[k-1]) ){ k--; }
    for(h=0, x=0; x<k; x++){
      h = h ^ (h<<2) ^ z[x];
    }
    a[i].h = h = (h<<LENGTH_MASK_SZ) | k;;
    h2 = h % nLine;
    a[i].iNext = a[h2].iHash;
    a[h2].iHash = i+1;
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
  return pA->h==pB->h && memcmp(pA->z,pB->z,pA->h & LENGTH_MASK)==0;
}

/*
** Append a single line of "diff" output to pOut.
*/
static void appendDiffLine(Blob *pOut, char *zPrefix, DLine *pLine){
  blob_append(pOut, zPrefix, 1);
  blob_append(pOut, pLine->z, pLine->h & LENGTH_MASK);
  blob_append(pOut, "\n", 1);
}

/*
** Expand the size of aEdit[] array to hold nEdit elements.
*/
static void expandEdit(DContext *p, int nEdit){
  p->aEdit = fossil_realloc(p->aEdit, nEdit*sizeof(int));
  p->nEditAlloc = nEdit;
}

/*
** Append a new COPY/DELETE/INSERT triple.
*/
static void appendTriple(DContext *p, int nCopy, int nDel, int nIns){
  /* printf("APPEND %d/%d/%d\n", nCopy, nDel, nIns); */
  if( p->nEdit>=3 ){
    if( p->aEdit[p->nEdit-1]==0 ){
      if( p->aEdit[p->nEdit-2]==0 ){
        p->aEdit[p->nEdit-3] += nCopy;
        p->aEdit[p->nEdit-2] += nDel;
        p->aEdit[p->nEdit-1] += nIns;
        return;
      }
      if( nCopy==0 ){
        p->aEdit[p->nEdit-2] += nDel;
        p->aEdit[p->nEdit-1] += nIns;
        return;
      }
    }
    if( nCopy==0 && nDel==0 ){
      p->aEdit[p->nEdit-1] += nIns;
      return;
    }
  }  
  if( p->nEdit+3>p->nEditAlloc ){
    expandEdit(p, p->nEdit*2 + 15);
    if( p->aEdit==0 ) return;
  }
  p->aEdit[p->nEdit++] = nCopy;
  p->aEdit[p->nEdit++] = nDel;
  p->aEdit[p->nEdit++] = nIns;
}


/*
** Given a diff context in which the aEdit[] array has been filled
** in, compute a context diff into pOut.
*/
static void contextDiff(DContext *p, Blob *pOut, int nContext){
  DLine *A;     /* Left side of the diff */
  DLine *B;     /* Right side of the diff */  
  int a = 0;    /* Index of next line in A[] */
  int b = 0;    /* Index of next line in B[] */
  int *R;       /* Array of COPY/DELETE/INSERT triples */
  int r;        /* Index into R[] */
  int nr;       /* Number of COPY/DELETE/INSERT triples to process */
  int mxr;      /* Maximum value for r */
  int na, nb;   /* Number of lines shown from A and B */
  int i, j;     /* Loop counters */
  int m;        /* Number of lines to output */
  int skip;     /* Number of lines to skip */

  A = p->aFrom;
  B = p->aTo;
  R = p->aEdit;
  mxr = p->nEdit;
  while( mxr>2 && R[mxr-1]==0 && R[mxr-2]==0 ){ mxr -= 3; }
  for(r=0; r<mxr; r += 3*nr){
    /* Figure out how many triples to show in a single block */
    for(nr=1; R[r+nr*3]>0 && R[r+nr*3]<nContext*2; nr++){}
    /* printf("r=%d nr=%d\n", r, nr); */

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
    if( R[r+nr*3]>nContext ){
      na += nContext;
      nb += nContext;
    }else{
      na += R[r+nr*3];
      nb += R[r+nr*3];
    }
    for(i=1; i<nr; i++){
      na += R[r+i*3];
      nb += R[r+i*3];
    }
    /*
     * If the patch changes an empty file or results in an empty file,
     * the block header must use 0,0 as position indicator and not 1,0.
     * Otherwise, patch would be confused and may reject the diff.
     */
    blob_appendf(pOut,"@@ -%d,%d +%d,%d @@\n",
      na ? a+skip+1 : 0, na,
      nb ? b+skip+1 : 0, nb);

    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    for(j=0; j<m; j++){
      appendDiffLine(pOut, " ", &A[a+j]);
    }
    a += m;
    b += m;

    /* Show the differences */
    for(i=0; i<nr; i++){
      m = R[r+i*3+1];
      for(j=0; j<m; j++){
        appendDiffLine(pOut, "-", &A[a+j]);
      }
      a += m;
      m = R[r+i*3+2];
      for(j=0; j<m; j++){
        appendDiffLine(pOut, "+", &B[b+j]);
      }
      b += m;
      if( i<nr-1 ){
        m = R[r+i*3+3];
        for(j=0; j<m; j++){
          appendDiffLine(pOut, " ", &B[b+j]);
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
      appendDiffLine(pOut, " ", &B[b+j]);
    }
  }
}

/*
** Compute the optimal longest common subsequence (LCS) using an
** exhaustive search.  This version of the LCS is only used for
** shorter input strings since runtime is O(N*N) where N is the
** input string length.
*/
static void optimalLCS(
  DContext *p,               /* Two files being compared */
  int iS1, int iE1,          /* Range of lines in p->aFrom[] */
  int iS2, int iE2,          /* Range of lines in p->aTo[] */
  int *piSX, int *piEX,      /* Write p->aFrom[] common segment here */
  int *piSY, int *piEY       /* Write p->aTo[] common segment here */
){
  int mxLength = 0;          /* Length of longest common subsequence */
  int i, j;                  /* Loop counters */
  int k;                     /* Length of a candidate subsequence */
  int iSXb = iS1;            /* Best match so far */
  int iSYb = iS2;            /* Best match so far */

  for(i=iS1; i<iE1-mxLength; i++){
    for(j=iS2; j<iE2-mxLength; j++){
      if( !same_dline(&p->aFrom[i], &p->aTo[j]) ) continue;
      if( mxLength && !same_dline(&p->aFrom[i+mxLength], &p->aTo[j+mxLength]) ){
        continue;
      }
      k = 1;
      while( i+k<iE1 && j+k<iE2 && same_dline(&p->aFrom[i+k],&p->aTo[j+k]) ){
        k++;
      }
      if( k>mxLength ){
        iSXb = i;
        iSYb = j;
        mxLength = k;
      }
    }
  }
  *piSX = iSXb;
  *piEX = iSXb + mxLength;
  *piSY = iSYb;
  *piEY = iSYb + mxLength;
}

/*
** Compare two blocks of text on lines iS1 through iE1-1 of the aFrom[]
** file and lines iS2 through iE2-1 of the aTo[] file.  Locate a sequence
** of lines in these two blocks that are exactly the same.  Return
** the bounds of the matching sequence.
**
** If there are two or more possible answers of the same length, the
** returned sequence should be the one closest to the center of the
** input range.
**
** Ideally, the common sequence should be the longest possible common
** sequence.  However, an exact computation of LCS is O(N*N) which is
** way too slow for larger files.  So this routine uses an O(N)
** heuristic approximation based on hashing that usually works about 
** as well.  But if the O(N) algorithm doesn't get a good solution
** and N is not too large, we fall back to an exact solution by
** calling optimalLCS().
*/
static void longestCommonSequence(
  DContext *p,               /* Two files being compared */
  int iS1, int iE1,          /* Range of lines in p->aFrom[] */
  int iS2, int iE2,          /* Range of lines in p->aTo[] */
  int *piSX, int *piEX,      /* Write p->aFrom[] common segment here */
  int *piSY, int *piEY       /* Write p->aTo[] common segment here */
){
  double bestScore = -1e30;  /* Best score seen so far */
  int i, j;                  /* Loop counters */
  int iSX, iSY, iEX, iEY;    /* Current match */
  double score;              /* Current score */
  int skew;                  /* How lopsided is the match */
  int dist;                  /* Distance of match from center */
  int mid;                   /* Center of the span */
  int iSXb, iSYb, iEXb, iEYb;   /* Best match so far */
  int iSXp, iSYp, iEXp, iEYp;   /* Previous match */


  iSXb = iSXp = iS1;
  iEXb = iEXp = iS1;
  iSYb = iSYp = iS2;
  iEYb = iEYp = iS2;
  mid = (iE1 + iS1)/2;
  for(i=iS1; i<iE1; i++){
    int limit = 0;
    j = p->aTo[p->aFrom[i].h % p->nTo].iHash;
    while( j>0 
      && (j-1<iS2 || j>=iE2 || !same_dline(&p->aFrom[i], &p->aTo[j-1]))
    ){
      if( limit++ > 10 ){
        j = 0;
        break;
      }
      j = p->aTo[j-1].iNext;
    }
    if( j==0 ) continue;
    assert( i>=iSXb && i>=iSXp );
    if( i<iEXb && j>=iSYb && j<iEYb ) continue;
    if( i<iEXp && j>=iSYp && j<iEYp ) continue;
    iSX = i;
    iSY = j-1;
    while( iSX>iS1 && iSY>iS2 && same_dline(&p->aFrom[iSX-1],&p->aTo[iSY-1]) ){
      iSX--;
      iSY--;
    }
    iEX = i+1;
    iEY = j;
    while( iEX<iE1 && iEY<iE2 && same_dline(&p->aFrom[iEX],&p->aTo[iEY]) ){
      iEX++;
      iEY++;
    }
    skew = (iSX-iS1) - (iSY-iS2);
    if( skew<0 ) skew = -skew;
    dist = (iSX+iEX)/2 - mid;
    if( dist<0 ) dist = -dist;
    score = (iEX - iSX) - 0.05*skew - 0.05*dist;
    if( score>bestScore ){
      bestScore = score;
      iSXb = iSX;
      iSYb = iSY;
      iEXb = iEX;
      iEYb = iEY;
    }else{
      iSXp = iSX;
      iSYp = iSY;
      iEXp = iEX;
      iEYp = iEY;
    }
  }
  if( iSXb==iEXb && (iE1-iS1)*(iE2-iS2)<400 ){
    /* If no common sequence is found using the hashing heuristic and
    ** the input is not too big, use the expensive exact solution */
    optimalLCS(p, iS1, iE1, iS2, iE2, piSX, piEX, piSY, piEY);
  }else{
    *piSX = iSXb;
    *piSY = iSYb;
    *piEX = iEXb;
    *piEY = iEYb;
  }
  /* printf("LCS(%d..%d/%d..%d) = %d..%d/%d..%d\n", 
     iS1, iE1, iS2, iE2, *piSX, *piEX, *piSY, *piEY);  */
}

/*
** Do a single step in the difference.  Compute a sequence of
** copy/delete/insert steps that will convert lines iS1 through iE1-1 of
** the input into lines iS2 through iE2-1 of the output and write
** that sequence into the difference context.
**
** The algorithm is to find a block of common text near the middle
** of the two segments being diffed.  Then recursively compute
** differences on the blocks before and after that common segment.
** Special cases apply if either input segment is empty or if the
** two segments have no text in common.
*/
static void diff_step(DContext *p, int iS1, int iE1, int iS2, int iE2){
  int iSX, iEX, iSY, iEY;

  if( iE1<=iS1 ){
    /* The first segment is empty */
    if( iE2>iS2 ){
      appendTriple(p, 0, 0, iE2-iS2);
    }
    return;
  }
  if( iE2<=iS2 ){
    /* The second segment is empty */
    appendTriple(p, 0, iE1-iS1, 0);
    return;
  }

  /* Find the longest matching segment between the two sequences */
  longestCommonSequence(p, iS1, iE1, iS2, iE2, &iSX, &iEX, &iSY, &iEY);

  if( iEX>iSX ){
    /* A common segement has been found.
    ** Recursively diff either side of the matching segment */
    diff_step(p, iS1, iSX, iS2, iSY);
    if( iEX>iSX ){
      appendTriple(p, iEX - iSX, 0, 0);
    }
    diff_step(p, iEX, iE1, iEY, iE2);
  }else{
    /* The two segments have nothing in common.  Delete the first then
    ** insert the second. */
    appendTriple(p, 0, iE1-iS1, iE2-iS2);
  }
}

/*
** Compute the differences between two files already loaded into
** the DContext structure.
**
** A divide and conquer technique is used.  We look for a large
** block of common text that is in the middle of both files.  Then
** compute the difference on those parts of the file before and
** after the common block.  This technique is fast, but it does
** not necessarily generate the minimum difference set.  On the
** other hand, we do not need a minimum difference set, only one
** that makes sense to human readers, which this algorithm does.
**
** Any common text at the beginning and end of the two files is
** removed before starting the divide-and-conquer algorithm.
*/
static void diff_all(DContext *p){
  int mnE, iS, iE1, iE2;

  /* Carve off the common header and footer */
  iE1 = p->nFrom;
  iE2 = p->nTo;
  while( iE1>0 && iE2>0 && same_dline(&p->aFrom[iE1-1], &p->aTo[iE2-1]) ){
    iE1--;
    iE2--;
  }
  mnE = iE1<iE2 ? iE1 : iE2;
  for(iS=0; iS<mnE && same_dline(&p->aFrom[iS],&p->aTo[iS]); iS++){}

  /* do the difference */
  if( iS>0 ){
    appendTriple(p, iS, 0, 0);
  }
  diff_step(p, iS, iE1, iS, iE2);
  if( iE1<p->nFrom ){
    appendTriple(p, p->nFrom - iE1, 0, 0);
  }

  /* Terminate the COPY/DELETE/INSERT triples with three zeros */
  expandEdit(p, p->nEdit+3);
  if( p->aEdit ){
    p->aEdit[p->nEdit++] = 0;
    p->aEdit[p->nEdit++] = 0;
    p->aEdit[p->nEdit++] = 0;
  }
}

/*
** Generate a report of the differences between files pA and pB.
** If pOut is not NULL then a unified diff is appended there.  It
** is assumed that pOut has already been initialized.  If pOut is
** NULL, then a pointer to an array of integers is returned.  
** The integers come in triples.  For each triple,
** the elements are the number of lines copied, the number of
** lines deleted, and the number of lines inserted.  The vector
** is terminated by a triple of all zeros.
**
** This diff utility does not work on binary files.  If a binary
** file is encountered, 0 is returned and pOut is written with
** text "cannot compute difference between binary files".
*/
int *text_diff(
  Blob *pA_Blob,   /* FROM file */
  Blob *pB_Blob,   /* TO file */
  Blob *pOut,      /* Write unified diff here if not NULL */
  int nContext,    /* Amount of context to unified diff */
  int ignoreEolWs  /* Ignore whitespace at the end of lines */
){
  DContext c;
 
  /* Prepare the input files */
  memset(&c, 0, sizeof(c));
  c.aFrom = break_into_lines(blob_str(pA_Blob), blob_size(pA_Blob),
                             &c.nFrom, ignoreEolWs);
  c.aTo = break_into_lines(blob_str(pB_Blob), blob_size(pB_Blob),
                           &c.nTo, ignoreEolWs);
  if( c.aFrom==0 || c.aTo==0 ){
    free(c.aFrom);
    free(c.aTo);
    if( pOut ){
      blob_appendf(pOut, "cannot compute difference between binary files\n");
    }
    return 0;
  }

  /* Compute the difference */
  diff_all(&c);

  if( pOut ){
    /* Compute a context diff if requested */
    contextDiff(&c, pOut, nContext);
    free(c.aFrom);
    free(c.aTo);
    free(c.aEdit);
    return 0;
  }else{
    /* If a context diff is not requested, then return the
    ** array of COPY/DELETE/INSERT triples.
    */
    free(c.aFrom);
    free(c.aTo);
    return c.aEdit;
  }
}

/*
** Copy a line with a limit. Used for side-by-side diffs to enforce a maximum
** line length limit.
*/
static char *copylimline(char *out, DLine *dl, int lim){
  int len;
  len = dl->h & LENGTH_MASK;
  if( lim && len > lim ){
    memcpy(out, dl->z, lim-3);
    strcpy(&out[lim-3], "...");
  }else{
    memcpy(out, dl->z, len);
    out[len] = '\0';
  }
  return out;
}

/*
** Output table body of a side-by-side diff. Prior to the call, the caller
** should have output:
**   <table class="sbsdiff">
**   <tr><th colspan="2" class="diffhdr">Old title</th><th/>
**   <th colspan="2" class="diffhdr">New title</th></tr>
**
** And after the call, it should output:
**   </table>
**
** Some good reference diffs in the fossil repository for testing:
** /vdiff?from=080d27a&to=4b0f813&detail=1
** /vdiff?from=636804745b&to=c1d78e0556&detail=1
** /vdiff?from=c0b6c28d29&to=25169506b7&detail=1
** /vdiff?from=e3d022dffa&to=48bcfbd47b&detail=1
*/
int html_sbsdiff(
  Blob *pA_Blob,   /* FROM file */
  Blob *pB_Blob,   /* TO file */
  int nContext,    /* Amount of context to unified diff */
  int ignoreEolWs  /* Ignore whitespace at the end of lines */
){
  DContext c;
  int i;
  int iFrom, iTo;
  char *linebuf;
  int collim=0; /* Currently not settable; allows a column limit for diffs */
  int allowExp=0; /* Currently not settable; (dis)allow expansion of rows */

  /* Prepare the input files */
  memset(&c, 0, sizeof(c));
  c.aFrom = break_into_lines(blob_str(pA_Blob), blob_size(pA_Blob),
                             &c.nFrom, ignoreEolWs);
  c.aTo = break_into_lines(blob_str(pB_Blob), blob_size(pB_Blob),
                           &c.nTo, ignoreEolWs);
  if( c.aFrom==0 || c.aTo==0 ){
    free(c.aFrom);
    free(c.aTo);
    /* Note: This would be generated within a table. */
    @ <p class="generalError" style="white-space: nowrap">cannot compute
    @ difference between binary files</p>
    return 0;
  }

  collim = collim < 4 ? 0 : collim;

  /* Compute the difference */
  diff_all(&c);

  linebuf = fossil_malloc(LENGTH_MASK+1);
  if( !linebuf ){
    free(c.aFrom);
    free(c.aTo);
    free(c.aEdit);
    return 0;
  }

  iFrom=iTo=0;
  i=0;
  while( i<c.nEdit ){
    int j;
    /* Copied lines */
    for( j=0; j<c.aEdit[i]; j++){
      int len;
      int dist;

      /* Hide lines which are copied and are further away from block boundaries
      ** than nConext lines. For each block with hidden lines, show a row
      ** notifying the user about the hidden rows.
      */
      if( j<nContext || j>c.aEdit[i]-nContext-1 ){
        @ <tr>
      }else if( j==nContext && j<c.aEdit[i]-nContext-1 ){
        @ <tr>
        @ <td class="meta" colspan="5" style="white-space: nowrap;">
        @ %d(c.aEdit[i]-2*nContext) hidden lines</td>
        @ </tr>
        if( !allowExp )
           continue;
        @ <tr style="display:none;">
      }else{
        if( !allowExp )
           continue;
        @ <tr style="display:none;">
      }

      copylimline(linebuf, &c.aFrom[iFrom+j], collim);
      @ <td class="lineno">%d(iFrom+j+1)</td><td>%h(linebuf)</td>

      @ <td> </td>

      copylimline(linebuf, &c.aTo[iTo+j], collim);
      @ <td class="lineno">%d(iTo+j+1)</td><td>%h(linebuf)</td>

      @ </tr>
    }
    iFrom+=c.aEdit[i];
    iTo+=c.aEdit[i];

    if( c.aEdit[i+1]!=0 && c.aEdit[i+2]!=0 ){
      int lim;
      lim = c.aEdit[i+1] > c.aEdit[i+2] ? c.aEdit[i+1] : c.aEdit[i+2];

      /* Assume changed lines */
      for( j=0; j<lim; j++ ){
        int len;
        @ <tr>

        if( j<c.aEdit[i+1] ){
          copylimline(linebuf, &c.aFrom[iFrom+j], collim);
          @ <td class="changed lineno">%d(iFrom+j+1)</td>
          @ <td class="changed">%h(linebuf)</td>
        }else{
          @ <td colspan="2"/>
        }

        @ <td class="changed">|</td>

        if( j<c.aEdit[i+2] ){
          copylimline(linebuf, &c.aTo[iTo+j], collim);
          @ <td class="changed lineno">%d(iTo+j+1)</td>
          @ <td class="changed">%h(linebuf)</td>
        }else{
          @ <td colspan="2"/>
        }

        @ </tr>
      }
      iFrom+=c.aEdit[i+1];
      iTo+=c.aEdit[i+2];
    }else{

      /* Process deleted lines */
      for( j=0; j<c.aEdit[i+1]; j++ ){
        int len;
        @ <tr>

        copylimline(linebuf, &c.aFrom[iFrom+j], collim);
        @ <td class="removed lineno">%d(iFrom+j+1)</td>
        @ <td class="removed">%h(linebuf)</td>

        @ <td>&lt;</td>

        @ <td colspan="2"/>

        @ </tr>
      }
      iFrom+=c.aEdit[i+1];

      /* Process inserted lines */
      for( j=0; j<c.aEdit[i+2]; j++ ){
        int len;
        @ <tr>
        @ <td colspan="2"/>

        @ <td>&gt;</td>

        copylimline(linebuf, &c.aTo[iTo+j], collim);
        @ <td class="added lineno">%d(iTo+j+1)</td>
        @ <td class="added">%h(linebuf)</td>

        @ </tr>
      }
      iTo+=c.aEdit[i+2];
    }

    i+=3;
  }

  free(linebuf);
  free(c.aFrom);
  free(c.aTo);
  free(c.aEdit);
  return 1;
}


/*
** COMMAND: test-rawdiff
*/
void test_rawdiff_cmd(void){
  Blob a, b;
  int r;
  int i;
  int *R;
  if( g.argc<4 ) usage("FILE1 FILE2 ...");
  blob_read_from_file(&a, g.argv[2]);
  for(i=3; i<g.argc; i++){
    if( i>3 ) fossil_print("-------------------------------\n");
    blob_read_from_file(&b, g.argv[i]);
    R = text_diff(&a, &b, 0, 0, 0);
    for(r=0; R[r] || R[r+1] || R[r+2]; r += 3){
      fossil_print(" copy %4d  delete %4d  insert %4d\n", R[r], R[r+1], R[r+2]);
    }
    /* free(R); */
    blob_reset(&b);
  }
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
  text_diff(&a, &b, &out, 3, 0);
  blob_write_to_file(&out, "-");
}

/**************************************************************************
** The basic difference engine is above.  What follows is the annotation
** engine.  Both are in the same file since they share many components.
*/

/*
** The status of an annotation operation is recorded by an instance
** of the following structure.
*/
typedef struct Annotator Annotator;
struct Annotator {
  DContext c;       /* The diff-engine context */
  struct AnnLine {  /* Lines of the original files... */
    const char *z;       /* The text of the line */
    short int n;         /* Number of bytes (omitting trailing space and \n) */
    short int iLevel;    /* Level at which tag was set */
    const char *zSrc;    /* Tag showing origin of this line */
  } *aOrig;
  int nOrig;        /* Number of elements in aOrig[] */
  int nNoSrc;       /* Number of entries where aOrig[].zSrc==NULL */
  int iLevel;       /* Current level */
  int nVers;        /* Number of versions analyzed */
  char **azVers;    /* Names of versions analyzed */
};

/*
** Initialize the annotation process by specifying the file that is
** to be annotated.  The annotator takes control of the input Blob and
** will release it when it is finished with it.
*/
static int annotation_start(Annotator *p, Blob *pInput){
  int i;

  memset(p, 0, sizeof(*p));
  p->c.aTo = break_into_lines(blob_str(pInput), blob_size(pInput),&p->c.nTo,1);
  if( p->c.aTo==0 ){
    return 1;
  }
  p->aOrig = fossil_malloc( sizeof(p->aOrig[0])*p->c.nTo );
  for(i=0; i<p->c.nTo; i++){
    p->aOrig[i].z = p->c.aTo[i].z;
    p->aOrig[i].n = p->c.aTo[i].h & LENGTH_MASK;
    p->aOrig[i].zSrc = 0;
  }
  p->nOrig = p->c.nTo;
  return 0;
}

/*
** The input pParent is the next most recent ancestor of the file
** being annotated.  Do another step of the annotation.  Return true
** if additional annotation is required.  zPName is the tag to insert
** on each line of the file being annotated that was contributed by
** pParent.  Memory to hold zPName is leaked.
*/
static int annotation_step(Annotator *p, Blob *pParent, char *zPName){
  int i, j;
  int lnTo;
  int iPrevLevel;
  int iThisLevel;

  /* Prepare the parent file to be diffed */
  p->c.aFrom = break_into_lines(blob_str(pParent), blob_size(pParent),
                                &p->c.nFrom, 1);
  if( p->c.aFrom==0 ){
    return 1;
  }

  /* Compute the differences going from pParent to the file being
  ** annotated. */
  diff_all(&p->c);

  /* Where new lines are inserted on this difference, record the
  ** zPName as the source of the new line.
  */
  iPrevLevel = p->iLevel;
  p->iLevel++;
  iThisLevel = p->iLevel;
  for(i=lnTo=0; i<p->c.nEdit; i+=3){
    struct AnnLine *x = &p->aOrig[lnTo];
    for(j=0; j<p->c.aEdit[i]; j++, lnTo++, x++){
      if( x->zSrc==0 || x->iLevel==iPrevLevel ){
         x->zSrc = zPName;
         x->iLevel = iThisLevel;
      }
    }
    lnTo += p->c.aEdit[i+2];
  }

  /* Clear out the diff results */
  free(p->c.aEdit);
  p->c.aEdit = 0;
  p->c.nEdit = 0;
  p->c.nEditAlloc = 0;

  /* Clear out the from file */
  free(p->c.aFrom);    

  /* Return no errors */
  return 0;
}


/*
** COMMAND: test-annotate-step
*/
void test_annotate_step_cmd(void){
  Blob orig, b;
  Annotator x;
  int i;

  if( g.argc<4 ) usage("RID1 RID2 ...");
  db_must_be_within_tree();
  blob_zero(&b);
  content_get(name_to_rid(g.argv[2]), &orig);
  if( annotation_start(&x, &orig) ){
    fossil_fatal("binary file");
  }
  for(i=3; i<g.argc; i++){
    blob_zero(&b);
    content_get(name_to_rid(g.argv[i]), &b);
    if( annotation_step(&x, &b, g.argv[i-1]) ){
      fossil_fatal("binary file");
    }
  }
  for(i=0; i<x.nOrig; i++){
    const char *zSrc = x.aOrig[i].zSrc;
    if( zSrc==0 ) zSrc = g.argv[g.argc-1];
    fossil_print("%10s: %.*s\n", zSrc, x.aOrig[i].n, x.aOrig[i].z);
  }
}

/* Annotation flags */
#define ANN_FILE_VERS  0x001  /* Show file version rather than commit version */

/*
** Compute a complete annotation on a file.  The file is identified
** by its filename number (filename.fnid) and the baseline in which
** it was checked in (mlink.mid).
*/
static void annotate_file(
  Annotator *p,        /* The annotator */
  int fnid,            /* The name of the file to be annotated */
  int mid,             /* Use the version of the file in this check-in */
  int webLabel,        /* Use web-style annotations if true */
  int iLimit,          /* Limit the number of levels if greater than zero */
  int annFlags         /* Flags to alter the annotation */
){
  Blob toAnnotate;     /* Text of the final (mid) version of the file */
  Blob step;           /* Text of previous revision */
  int rid;             /* Artifact ID of the file being annotated */
  char *zLabel;        /* Label to apply to a line */
  Stmt q;              /* Query returning all ancestor versions */

  /* Initialize the annotation */
  rid = db_int(0, "SELECT fid FROM mlink WHERE mid=%d AND fnid=%d",mid,fnid);
  if( rid==0 ){
    fossil_panic("file #%d is unchanged in manifest #%d", fnid, mid);
  }
  if( !content_get(rid, &toAnnotate) ){
    fossil_panic("unable to retrieve content of artifact #%d", rid);
  }
  db_multi_exec("CREATE TEMP TABLE ok(rid INTEGER PRIMARY KEY)");
  if( iLimit<=0 ) iLimit = 1000000000;
  compute_direct_ancestors(mid, iLimit);
  annotation_start(p, &toAnnotate);

  db_prepare(&q, 
    "SELECT mlink.fid,"
    "       (SELECT uuid FROM blob WHERE rid=mlink.%s),"
    "       date(event.mtime), "
    "       coalesce(event.euser,event.user) "
    "  FROM ancestor, mlink, event"
    " WHERE mlink.fnid=%d"
    "   AND mlink.mid=ancestor.rid"
    "   AND event.objid=ancestor.rid"
    " ORDER BY ancestor.generation ASC"
    " LIMIT %d",
    (annFlags & ANN_FILE_VERS)!=0 ? "fid" : "mid",
    fnid,
    iLimit>0 ? iLimit : 10000000
  );
  while( db_step(&q)==SQLITE_ROW ){
    int pid = db_column_int(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    if( webLabel ){
      zLabel = mprintf(
          "<a href='%s/info/%s' target='infowindow'>%.10s</a> %s %9.9s", 
          g.zTop, zUuid, zUuid, zDate, zUser
      );
    }else{
      zLabel = mprintf("%.10s %s %9.9s", zUuid, zDate, zUser);
    }
    p->nVers++;
    p->azVers = fossil_realloc(p->azVers, p->nVers*sizeof(p->azVers[0]) );
    p->azVers[p->nVers-1] = zLabel;
    content_get(pid, &step);
    annotation_step(p, &step, zLabel);
    blob_reset(&step);
  }
  db_finalize(&q);
}

/*
** WEBPAGE: annotate
**
** Query parameters:
**
**    checkin=ID          The manifest ID at which to start the annotation
**    filename=FILENAME   The filename.
*/
void annotation_page(void){
  int mid;
  int fnid;
  int i;
  int iLimit;
  int annFlags = 0;
  Annotator ann;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }
  mid = name_to_typed_rid(PD("checkin","0"),"ci");
  fnid = db_int(0, "SELECT fnid FROM filename WHERE name=%Q", P("filename"));
  if( mid==0 || fnid==0 ){ fossil_redirect_home(); }
  iLimit = atoi(PD("limit","-1"));
  if( !db_exists("SELECT 1 FROM mlink WHERE mid=%d AND fnid=%d",mid,fnid) ){
    fossil_redirect_home();
  }
  style_header("File Annotation");
  if( P("filevers") ) annFlags |= ANN_FILE_VERS;
  annotate_file(&ann, fnid, mid, g.perm.History, iLimit, annFlags);
  if( P("log") ){
    int i;
    @ <h2>Versions analyzed:</h2>
    @ <ol>
    for(i=0; i<ann.nVers; i++){
      @ <li><tt>%s(ann.azVers[i])</tt></li>
    }
    @ </ol>
    @ <hr>
    @ <h2>Annotation:</h2>
  }
  @ <pre>
  for(i=0; i<ann.nOrig; i++){
    ((char*)ann.aOrig[i].z)[ann.aOrig[i].n] = 0;
    @ %s(ann.aOrig[i].zSrc): %h(ann.aOrig[i].z)
  }
  @ </pre>
  style_footer();
}

/*
** COMMAND: annotate
**
** %fossil annotate ?OPTIONS? FILENAME
**
** Output the text of a file with markings to show when each line of
** the file was last modified.
**
** Options:
**   --limit N       Only look backwards in time by N versions
**   --log           List all versions analyzed
**   --filevers      Show file version numbers rather than check-in versions
*/
void annotate_cmd(void){
  int fnid;         /* Filename ID */
  int fid;          /* File instance ID */
  int mid;          /* Manifest where file was checked in */
  Blob treename;    /* FILENAME translated to canonical form */
  char *zFilename;  /* Cannonical filename */
  Annotator ann;    /* The annotation of the file */
  int i;            /* Loop counter */
  const char *zLimit; /* The value to the --limit option */
  int iLimit;       /* How far back in time to look */
  int showLog;      /* True to show the log */
  int fileVers;     /* Show file version instead of check-in versions */
  int annFlags = 0; /* Flags to control annotation properties */

  zLimit = find_option("limit",0,1);
  if( zLimit==0 || zLimit[0]==0 ) zLimit = "-1";
  iLimit = atoi(zLimit);
  showLog = find_option("log",0,0)!=0;
  fileVers = find_option("filevers",0,0)!=0;
  db_must_be_within_tree();
  if (g.argc<3) {
    usage("FILENAME");
  }
  file_tree_name(g.argv[2], &treename, 1);
  zFilename = blob_str(&treename);
  fnid = db_int(0, "SELECT fnid FROM filename WHERE name=%Q", zFilename);
  if( fnid==0 ){
    fossil_fatal("no such file: %s", zFilename);
  }
  fid = db_int(0, "SELECT rid FROM vfile WHERE pathname=%Q", zFilename);
  if( fid==0 ){
    fossil_fatal("not part of current checkout: %s", zFilename);
  }
  mid = db_int(0, "SELECT mid FROM mlink WHERE fid=%d AND fnid=%d", fid, fnid);
  if( mid==0 ){
    fossil_panic("unable to find manifest");
  }
  if( fileVers ) annFlags |= ANN_FILE_VERS;
  annotate_file(&ann, fnid, mid, 0, iLimit, annFlags);
  if( showLog ){
    for(i=0; i<ann.nVers; i++){
      printf("version %3d: %s\n", i+1, ann.azVers[i]);
    }
    printf("---------------------------------------------------\n");
  }
  for(i=0; i<ann.nOrig; i++){
    fossil_print("%s: %.*s\n", 
                 ann.aOrig[i].zSrc, ann.aOrig[i].n, ann.aOrig[i].z);
  }
}
