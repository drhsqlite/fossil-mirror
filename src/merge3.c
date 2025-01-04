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
** This module implements a 3-way merge
*/
#include "config.h"
#include "merge3.h"

#if 0
#define DEBUG(X)  X
#define ISDEBUG 1
#else
#define DEBUG(X)
#define ISDEBUG 0
#endif

/* The minimum of two integers */
#ifndef min
#  define min(A,B)  (A<B?A:B)
#endif

/*
** Compare N lines of text from pV1 and pV2.  If the lines
** are the same, return true.  Return false if one or more of the N
** lines are different.
**
** The cursors on both pV1 and pV2 is unchanged by this comparison.
*/
static int sameLines(Blob *pV1, Blob *pV2, int N){
  char *z1, *z2;
  int i;
  char c;

  if( N==0 ) return 1;
  z1 = &blob_buffer(pV1)[blob_tell(pV1)];
  z2 = &blob_buffer(pV2)[blob_tell(pV2)];
  for(i=0; (c=z1[i])==z2[i]; i++){
    if( c=='\n' || c==0 ){
      N--;
      if( N==0 || c==0 ) return 1;
    }
  }
  return 0;
}

/*
** Look at the next edit triple in both aC1 and aC2.  (An "edit triple" is
** three integers describing the number of copies, deletes, and inserts in
** moving from the original to the edited copy of the file.) If the three
** integers of the edit triples describe an identical edit, then return 1.
** If the edits are different, return 0.
*/
static int sameEdit(
  int *aC1,      /* Array of edit integers for file 1 */
  int *aC2,      /* Array of edit integers for file 2 */
  Blob *pV1,     /* Text of file 1 */
  Blob *pV2      /* Text of file 2 */
){
  if( aC1[0]!=aC2[0] ) return 0;
  if( aC1[1]!=aC2[1] ) return 0;
  if( aC1[2]!=aC2[2] ) return 0;
  if( sameLines(pV1, pV2, aC1[2]) ) return 1;
  return 0;
}

/*
** Text of boundary markers for merge conflicts.
*/
static const char *const mergeMarker[] = {
 /*123456789 123456789 123456789 123456789 123456789 123456789 123456789*/
  "<<<<<<< BEGIN MERGE CONFLICT: local copy shown first <<<<<<<<<<<<",
  "####### SUGGESTED CONFLICT RESOLUTION follows ###################",
  "||||||| COMMON ANCESTOR content follows |||||||||||||||||||||||||",
  "======= MERGED IN content follows ===============================",
  ">>>>>>> END MERGE CONFLICT >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
};

/*
** Return true if the input blob contains any CR/LF pairs on the first
** ten lines. This should be enough to detect files that use mainly CR/LF
** line endings without causing a performance impact for LF only files.
*/
int contains_crlf(Blob *p){
  int i;
  int j = 0;
  const int maxL = 10;             /* Max lines to check */
  const char *z = blob_buffer(p);
  int n = blob_size(p)+1;
  for(i=1; i<n; ){
    if( z[i-1]=='\r' && z[i]=='\n' ) return 1;
    while( i<n && z[i]!='\n' ){ i++; }
    j++;
    if( j>maxL ) return 0;
  }
  return 0;
}

/*
** Ensure that the text in pBlob ends with a new line.
** If useCrLf is true adds "\r\n" otherwise '\n'.
*/
void ensure_line_end(Blob *pBlob, int useCrLf){
  if( pBlob->nUsed<=0 ) return;
  if( pBlob->aData[pBlob->nUsed-1]!='\n' ){
    if( useCrLf ) blob_append_char(pBlob, '\r');
    blob_append_char(pBlob, '\n');
  }
}

/*
** Write out one of the four merge-marks.
*/
void append_merge_mark(Blob *pOut, int iMark, int ln, int useCrLf){
  ensure_line_end(pOut, useCrLf);
  blob_append(pOut, mergeMarker[iMark], -1);
  if( ln>0 ) blob_appendf(pOut, " (line %d)", ln);
  ensure_line_end(pOut, useCrLf);
}

#if INTERFACE
/*
** This is an abstract class for constructing a merge.
** Subclasses of this object format the merge output in different ways.
**
** To subclass, create an instance of the MergeBuilder object and fill
** in appropriate method implementations.
*/
struct MergeBuilder {
  void (*xStart)(MergeBuilder*);
  void (*xSame)(MergeBuilder*, unsigned int);
  void (*xChngV1)(MergeBuilder*, unsigned int, unsigned int);
  void (*xChngV2)(MergeBuilder*, unsigned int, unsigned int);
  void (*xChngBoth)(MergeBuilder*, unsigned int, unsigned int);
  void (*xConflict)(MergeBuilder*, unsigned int, unsigned int, unsigned int);
  void (*xEnd)(MergeBuilder*);
  void (*xDestroy)(MergeBuilder*);
  const char *zPivot;        /* Label or name for the pivot */
  const char *zV1;           /* Label or name for the V1 file */
  const char *zV2;           /* Label or name for the V2 file */
  const char *zOut;          /* Label or name for the output */
  Blob *pPivot;              /* The common ancestor */
  Blob *pV1;                 /* First variant (local copy) */
  Blob *pV2;                 /* Second variant (merged in) */
  Blob *pOut;                /* Write merge results here */
  int useCrLf;               /* Use CRLF line endings */
  int nContext;              /* Size of unchanged line boundaries */
  unsigned int mxPivot;      /* Number of lines in the pivot */
  unsigned int mxV1;         /* Number of lines in V1 */
  unsigned int mxV2;         /* Number of lines in V2 */
  unsigned int lnPivot;      /* Lines read from pivot */
  unsigned int lnV1;         /* Lines read from v1 */
  unsigned int lnV2;         /* Lines read from v2 */
  unsigned int lnOut;        /* Lines written to out */
  unsigned int nConflict;    /* Number of conflicts seen */
  u64 diffFlags;             /* Flags for difference engine */
};
#endif /* INTERFACE */


/************************* Generic MergeBuilder ******************************/
/* These are generic methods for MergeBuilder.  They just output debugging
** information.  But some of them are useful as base methods for other useful
** implementations of MergeBuilder.
*/

/* xStart() and xEnd() are called to generate header and fotter information
** in the output.  This is a no-op in the generic implementation.
*/
static void dbgStartEnd(MergeBuilder *p){  (void)p; }

/* The next N lines of PIVOT are unchanged in both V1 and V2
*/
static void dbgSame(MergeBuilder *p, unsigned int N){
  blob_appendf(p->pOut, 
     "COPY %u from BASELINE(%u..%u) or V1(%u..%u) or V2(%u..%u)\n",
     N, p->lnPivot+1, p->lnPivot+N, p->lnV1+1, p->lnV1+N,
     p->lnV2+1, p->lnV2+N);
  p->lnPivot += N;
  p->lnV1 += N;
  p->lnV2 += N;
}

/* The next nPivot lines of the PIVOT are changed into nV1 lines by V1
*/
static void dbgChngV1(MergeBuilder *p, unsigned int nPivot, unsigned int nV1){
  blob_appendf(p->pOut, "COPY %u from V1(%u..%u)\n",
               nV1, p->lnV1+1, p->lnV1+nV1);
  p->lnPivot += nPivot;
  p->lnV2 += nPivot;
  p->lnV1 += nV1;
}

/* The next nPivot lines of the PIVOT are changed into nV2 lines by V2
*/
static void dbgChngV2(MergeBuilder *p, unsigned int nPivot, unsigned int nV2){
  blob_appendf(p->pOut, "COPY %u lines FROM V2(%u..%u)\n",
               nV2, p->lnV2+1, p->lnV2+nV2);
  p->lnPivot += nPivot;
  p->lnV1 += nPivot;
  p->lnV2 += nV2;
}

/* The next nPivot lines of the PIVOT are changed into nV lines from V1 and
** V2, which should be the same.  In other words, the same change is found
** in both V1 and V2.
*/
static void dbgChngBoth(MergeBuilder *p, unsigned int nPivot, unsigned int nV){
  blob_appendf(p->pOut, "COPY %u lines from V1(%u..%u) or V2(%u..%u)\n",
               nV, p->lnV1+1, p->lnV1+nV, p->lnV2+1, p->lnV2+nV);
  p->lnPivot += nPivot;
  p->lnV1 += nV;
  p->lnV2 += nV;
}

/* V1 and V2 have different and overlapping changes.  The next nPivot lines
** of the PIVOT are converted into nV1 lines of V1 and nV2 lines of V2.
*/
static void dbgConflict(
  MergeBuilder *p,
  unsigned int nPivot,
  unsigned int nV1,
  unsigned int nV2
){
  blob_appendf(p->pOut, 
   "CONFLICT %u,%u,%u BASELINE(%u..%u) versus V1(%u..%u) versus V2(%u..%u)\n",
       nPivot, nV1, nV2,
       p->lnPivot+1, p->lnPivot+nPivot,
       p->lnV1+1, p->lnV1+nV1,
       p->lnV2+1, p->lnV2+nV2);
  p->lnV1 += nV1;
  p->lnPivot += nPivot;
  p->lnV2 += nV2;
}

/* Generic destructor for the MergeBuilder object
*/
static void dbgDestroy(MergeBuilder *p){
  memset(p, 0, sizeof(*p));
}

/* Generic initializer for a MergeBuilder object
*/
static void mergebuilder_init(MergeBuilder *p){
  memset(p, 0, sizeof(*p));
  p->xStart = dbgStartEnd;
  p->xSame = dbgSame;
  p->xChngV1 = dbgChngV1;
  p->xChngV2 = dbgChngV2;
  p->xChngBoth = dbgChngBoth;
  p->xConflict = dbgConflict;
  p->xEnd = dbgStartEnd;
  p->xDestroy = dbgDestroy;
}

/************************* MergeBuilderToken ********************************/
/* This version of MergeBuilder actually performs a merge on file that
** are broken up into tokens instead of lines, and puts the result in pOut.
*/
static void tokenSame(MergeBuilder *p, unsigned int N){
  blob_append(p->pOut, p->pPivot->aData+p->pPivot->iCursor, N);
  p->pPivot->iCursor += N;
  p->pV1->iCursor += N;
  p->pV2->iCursor += N;
}
static void tokenChngV1(MergeBuilder *p, unsigned int nPivot, unsigned nV1){
  blob_append(p->pOut, p->pV1->aData+p->pV1->iCursor, nV1);
  p->pPivot->iCursor += nPivot;
  p->pV1->iCursor += nV1;
  p->pV2->iCursor += nPivot;
}
static void tokenChngV2(MergeBuilder *p, unsigned int nPivot, unsigned nV2){
  blob_append(p->pOut, p->pV2->aData+p->pV2->iCursor, nV2);
  p->pPivot->iCursor += nPivot;
  p->pV1->iCursor += nPivot;
  p->pV2->iCursor += nV2;
}
static void tokenChngBoth(MergeBuilder *p, unsigned int nPivot, unsigned nV){
  blob_append(p->pOut, p->pV2->aData+p->pV2->iCursor, nV);
  p->pPivot->iCursor += nPivot;
  p->pV1->iCursor += nV;
  p->pV2->iCursor += nV;
}
static void tokenConflict(
  MergeBuilder *p,
  unsigned int nPivot,
  unsigned int nV1,
  unsigned int nV2
){
  /* For a token-merge conflict, use the text from the merge-in */
  blob_append(p->pOut, p->pV2->aData+p->pV2->iCursor, nV2);
  p->pPivot->iCursor += nPivot;
  p->pV1->iCursor += nV1;
  p->pV2->iCursor += nV2;
}
static void mergebuilder_init_token(MergeBuilder *p){
  mergebuilder_init(p);
  p->xSame = tokenSame;
  p->xChngV1 = tokenChngV1;
  p->xChngV2 = tokenChngV2;
  p->xChngBoth = tokenChngBoth;
  p->xConflict = tokenConflict;
  p->diffFlags = DIFF_BY_TOKEN;
}

/*
** Attempt to do a low-level merge on a conflict.  The conflict is
** described by the first four parameters, which are the same as the
** arguments to the xConflict method of the MergeBuilder object.
** This routine attempts to resolve the conflict by looking at
** elements of the conflict region that are finer grain than complete
** lines of text.
**
** The result is written into Blob pOut.  pOut is initialized by this
** routine.
*/
int merge_try_to_resolve_conflict(
  MergeBuilder *pMB,     /* MergeBuilder that encounter conflict */
  unsigned int nPivot,   /* Lines of conflict in the pivot */
  unsigned int nV1,      /* Lines of conflict in V1 */
  unsigned int nV2,      /* Lines of conflict in V2 */
  Blob *pOut             /* Write resolution text here */
){
  int nConflict;
  MergeBuilder mb;
  Blob pv, v1, v2;
  mergebuilder_init_token(&mb);
  blob_extract_lines(pMB->pPivot, nPivot, &pv);
  blob_extract_lines(pMB->pV1, nV1, &v1);
  blob_extract_lines(pMB->pV2, nV2, &v2);
  blob_zero(pOut);
  blob_materialize(&pv);
  blob_materialize(&v1);
  blob_materialize(&v2);
  mb.pPivot = &pv;
  mb.pV1 = &v1;
  mb.pV2 = &v2;
  mb.pOut = pOut;
  nConflict = merge_three_blobs(&mb);
  blob_reset(&pv);
  blob_reset(&v1);
  blob_reset(&v2);
  /* mb has not allocated any resources, so we do not need to invoke
  ** the xDestroy method. */
  blob_add_final_newline(pOut);
  return nConflict;
}


/************************* MergeBuilderText **********************************/
/* This version of MergeBuilder actually performs a merge on file and puts
** the result in pOut
*/
static void txtStart(MergeBuilder *p){
  /* If both pV1 and pV2 start with a UTF-8 byte-order-mark (BOM),
  ** keep it in the output. This should be secure enough not to cause
  ** unintended changes to the merged file and consistent with what
  ** users are using in their source files.
  */
  if( starts_with_utf8_bom(p->pV1, 0) && starts_with_utf8_bom(p->pV2, 0) ){
    blob_append(p->pOut, (char*)get_utf8_bom(0), -1);
  }
  if( contains_crlf(p->pV1) && contains_crlf(p->pV2) ){
    p->useCrLf = 1;
  }
}
static void txtSame(MergeBuilder *p, unsigned int N){
  blob_copy_lines(p->pOut, p->pPivot, N);  p->lnPivot += N;
  blob_copy_lines(0, p->pV1, N);           p->lnV1 += N;
  blob_copy_lines(0, p->pV2, N);           p->lnV2 += N;
}
static void txtChngV1(MergeBuilder *p, unsigned int nPivot, unsigned int nV1){
  blob_copy_lines(0, p->pPivot, nPivot);   p->lnPivot += nPivot;
  blob_copy_lines(0, p->pV2, nPivot);      p->lnV2 += nPivot;
  blob_copy_lines(p->pOut, p->pV1, nV1);   p->lnV1 += nV1;
}
static void txtChngV2(MergeBuilder *p, unsigned int nPivot, unsigned int nV2){
  blob_copy_lines(0, p->pPivot, nPivot);   p->lnPivot += nPivot;
  blob_copy_lines(0, p->pV1, nPivot);      p->lnV1 += nPivot;
  blob_copy_lines(p->pOut, p->pV2, nV2);   p->lnV2 += nV2;
}
static void txtChngBoth(MergeBuilder *p, unsigned int nPivot, unsigned int nV){
  blob_copy_lines(0, p->pPivot, nPivot);   p->lnPivot += nPivot;
  blob_copy_lines(0, p->pV1, nV);          p->lnV1 += nV;
  blob_copy_lines(p->pOut, p->pV2, nV);    p->lnV2 += nV;
}
static void txtConflict(
  MergeBuilder *p,
  unsigned int nPivot,
  unsigned int nV1,
  unsigned int nV2
){
  int nRes;   /* Lines in the computed conflict resolution */
  Blob res;   /* Text of the conflict resolution */
  
  merge_try_to_resolve_conflict(p, nPivot, nV1, nV2, &res);
  nRes = blob_linecount(&res);

  append_merge_mark(p->pOut, 0, p->lnV1+1, p->useCrLf);
  blob_copy_lines(p->pOut, p->pV1, nV1);         p->lnV1 += nV1;

  if( nRes>0 ){
    append_merge_mark(p->pOut, 1, 0, p->useCrLf);
    blob_copy_lines(p->pOut, &res, nRes);
  }
  blob_reset(&res);

  append_merge_mark(p->pOut, 2, p->lnPivot+1, p->useCrLf);
  blob_copy_lines(p->pOut, p->pPivot, nPivot);   p->lnPivot += nPivot;

  append_merge_mark(p->pOut, 3, p->lnV2+1, p->useCrLf);
  blob_copy_lines(p->pOut, p->pV2, nV2);         p->lnV2 += nV2;

  append_merge_mark(p->pOut, 4, -1, p->useCrLf);
}
static void mergebuilder_init_text(MergeBuilder *p){
  mergebuilder_init(p);
  p->xStart = txtStart;
  p->xSame = txtSame;
  p->xChngV1 = txtChngV1;
  p->xChngV2 = txtChngV2;
  p->xChngBoth = txtChngBoth;
  p->xConflict = txtConflict;
}

/************************* MergeBuilderTcl **********************************/
/* Generate merge output formatted for reading by a TCL script.
**
** The output consists of lines of text, each with 4 tokens.  The tokens
** represent the content for one line from baseline, v1, v2, and output
** respectively.  The first character of each token provides auxiliary
** information:
**
**     .     This line is omitted.
**     N     Name of the file.
**     T     Literal text follows that should have a \n terminator.
**     R     Literal text follows that needs a \r\n terminator.
**     X     Merge conflict.
**     Z     Literal text without a line terminator.
**     S     Skipped lines.  Followed by number of lines to skip.
**     1     Text is a copy of token 1
**     2     Use data from data-token 2
**     3     Use data from data-token 3
*/

/* Write text that goes into the interior of a double-quoted string in TCL */
static void tclWriteQuotedText(Blob *pOut, const char *zIn, int nIn){
  int j;
  for(j=0; j<nIn; j++){
    char c = zIn[j];
    if( c=='\\' ){
      blob_append(pOut, "\\\\", 2);
    }else if( c=='"' ){
      blob_append(pOut, "\\\"", 2);
    }else if( c<' ' || c>0x7e ){
      char z[5];
      z[0] = '\\';
      z[1] = "01234567"[(c>>6)&0x3];
      z[2] = "01234567"[(c>>3)&0x7];
      z[3] = "01234567"[c&0x7];
      z[4] = 0;
      blob_append(pOut, z, 4);
    }else{
      blob_append_char(pOut, c);
    }
  }
}

/* Copy one line of text from pIn and append to pOut, encoded as TCL */
static void tclLineOfText(Blob *pOut, Blob *pIn, char cType){
  int i, k;
  for(i=pIn->iCursor; i<pIn->nUsed && pIn->aData[i]!='\n'; i++){}
  if( i==pIn->nUsed ){
    k = i;
  }else if( i>pIn->iCursor && pIn->aData[i-1]=='\r' ){
    k = i-1;
    i++;
  }else{
    k = i;
    i++;
  }
  blob_append_char(pOut, '"');
  blob_append_char(pOut, cType);
  tclWriteQuotedText(pOut, pIn->aData+pIn->iCursor, k-pIn->iCursor);
  pIn->iCursor = i;
  blob_append_char(pOut, '"');
}
static void tclStart(MergeBuilder *p){
  Blob *pOut = p->pOut;
  blob_append(pOut, "\"N", 2);
  tclWriteQuotedText(pOut, p->zPivot, (int)strlen(p->zPivot));
  blob_append(pOut, "\" \"N", 4);
  tclWriteQuotedText(pOut, p->zV1, (int)strlen(p->zV1));
  blob_append(pOut, "\" \"N", 4);
  tclWriteQuotedText(pOut, p->zV2, (int)strlen(p->zV2));
  blob_append(pOut, "\" \"N", 4);
  if( p->zOut ){
    tclWriteQuotedText(pOut, p->zOut, (int)strlen(p->zOut));
  }else{
    blob_append(pOut, "(Merge Result)", -1);
  }
  blob_append(pOut, "\"\n", 2);
}
static void tclSame(MergeBuilder *p, unsigned int N){
  int i = 0;
  int nSkip;

  if( p->lnPivot>=2 || p->lnV1>2 || p->lnV2>2 ){
    while( i<N && i<p->nContext ){
      tclLineOfText(p->pOut, p->pPivot, 'T');
      blob_append(p->pOut, " 1 1 1\n", 7);
      i++;
    }
    nSkip = N - p->nContext*2;
  }else{
    nSkip = N - p->nContext;
  }
  if( nSkip>0 ){
    blob_appendf(p->pOut, "\"S%d %d %d %d\" . . .\n",
                 nSkip, nSkip, nSkip, nSkip);
    blob_copy_lines(0, p->pPivot, nSkip);
    i += nSkip;
  }

  p->lnPivot += N;
  p->lnV1 += N;
  p->lnV2 += N;

  if( p->lnPivot<p->mxPivot || p->lnV1<p->mxV1 || p->lnV2<p->mxV2 ){
    while( i<N ){
      tclLineOfText(p->pOut, p->pPivot, 'T');
      blob_append(p->pOut, " 1 1 1\n", 7);
      i++;
    }
  }

  blob_copy_lines(0, p->pV1, N);
  blob_copy_lines(0, p->pV2, N);
}
static void tclChngV1(MergeBuilder *p, unsigned int nPivot, unsigned int nV1){
  int i;
  for(i=0; i<nPivot && i<nV1; i++){
    tclLineOfText(p->pOut, p->pPivot, 'T');
    blob_append_char(p->pOut, ' ');
    tclLineOfText(p->pOut, p->pV1, 'T');
    blob_append(p->pOut, " 1 2\n", 5);
  }
  while( i<nPivot ){
    tclLineOfText(p->pOut, p->pPivot, 'T');
    blob_append(p->pOut, " . 1 .\n", 7);
    i++;
  }
  while( i<nV1 ){
    blob_append(p->pOut, ". ", 2);
    tclLineOfText(p->pOut, p->pV1, 'T');
    blob_append(p->pOut, " . 2\n", 5);
    i++;
  }
  p->lnPivot += nPivot;
  p->lnV1 += nV1;
  p->lnV2 += nPivot;
  blob_copy_lines(0, p->pV2, nPivot);
}
static void tclChngV2(MergeBuilder *p, unsigned int nPivot, unsigned int nV2){
  int i;
  for(i=0; i<nPivot && i<nV2; i++){
    tclLineOfText(p->pOut, p->pPivot, 'T');
    blob_append(p->pOut, " 1 ", 3);
    tclLineOfText(p->pOut, p->pV2, 'T');
    blob_append(p->pOut, " 3\n", 3);
  }
  while( i<nPivot ){
    tclLineOfText(p->pOut, p->pPivot, 'T');
    blob_append(p->pOut, " 1 . .\n", 7);
    i++;
  }
  while( i<nV2 ){
    blob_append(p->pOut, ". . ", 4);
    tclLineOfText(p->pOut, p->pV2, 'T');
    blob_append(p->pOut, " 3\n", 3);
    i++;
  }
  p->lnPivot += nPivot;
  p->lnV1 += nPivot;
  p->lnV2 += nV2;
  blob_copy_lines(0, p->pV1, nPivot);
}
static void tclChngBoth(MergeBuilder *p, unsigned int nPivot, unsigned int nV){
  int i;
  for(i=0; i<nPivot && i<nV; i++){
    tclLineOfText(p->pOut, p->pPivot, 'T');
    blob_append_char(p->pOut, ' ');
    tclLineOfText(p->pOut, p->pV1, 'T');
    blob_append(p->pOut, " 2 2\n", 5);
  }
  while( i<nPivot ){
    tclLineOfText(p->pOut, p->pPivot, 'T');
    blob_append(p->pOut, " . . .\n", 7);
    i++;
  }
  while( i<nV ){
    blob_append(p->pOut, ". ", 2);
    tclLineOfText(p->pOut, p->pV1, 'T');
    blob_append(p->pOut, " 2 2\n", 5);
    i++;
  }
  p->lnPivot += nPivot;
  p->lnV1 += nV;
  p->lnV2 += nV;
  blob_copy_lines(0, p->pV2, nV);
}
static void tclConflict(
  MergeBuilder *p,
  unsigned int nPivot,
  unsigned int nV1,
  unsigned int nV2
){
  int mx = nPivot;
  int i;
  int nRes;
  Blob res;
  
  merge_try_to_resolve_conflict(p, nPivot, nV1, nV2, &res);
  nRes = blob_linecount(&res);
  if( nV1>mx ) mx = nV1;
  if( nV2>mx ) mx = nV2;
  if( nRes>mx ) mx = nRes;
  if( nRes>0 ){
    blob_appendf(p->pOut, "\"S0 0 0 %d\" . . .\n", nV2+2);
  }
  for(i=0; i<mx; i++){
    if( i<nPivot ){
      tclLineOfText(p->pOut, p->pPivot, 'X');
    }else{
      blob_append_char(p->pOut, '.');
    }
    blob_append_char(p->pOut, ' ');
    if( i<nV1 ){
      tclLineOfText(p->pOut, p->pV1, 'X');
    }else{
      blob_append_char(p->pOut, '.');
    }
    blob_append_char(p->pOut, ' ');
    if( i<nV2 ){
      tclLineOfText(p->pOut, p->pV2, 'X');
    }else{
      blob_append_char(p->pOut, '.');
    }
    if( i<nRes ){
      blob_append_char(p->pOut, ' ');
      tclLineOfText(p->pOut, &res, 'X');
      blob_append_char(p->pOut, '\n');
    }else{
      blob_append(p->pOut, " .\n", 3);
    }
    if( i==mx-1 ){
      blob_appendf(p->pOut, "\"S0 0 0 %d\" . . .\n", nPivot+nV1+3);
    }
  }
  blob_reset(&res);
  p->lnPivot += nPivot;
  p->lnV1 += nV1;
  p->lnV2 += nV2;
}
void mergebuilder_init_tcl(MergeBuilder *p){
  mergebuilder_init(p);
  p->xStart = tclStart;
  p->xSame = tclSame;
  p->xChngV1 = tclChngV1;
  p->xChngV2 = tclChngV2;
  p->xChngBoth = tclChngBoth;
  p->xConflict = tclConflict;
}
/*****************************************************************************/

/*
** The aC[] array contains triples of integers.  Within each triple, the
** elements are:
**
**   (0)  The number of lines to copy
**   (1)  The number of lines to delete
**   (2)  The number of liens to insert
**
** Suppose we want to advance over sz lines of the original file.  This routine
** returns true if that advance would land us on a copy operation.  It
** returns false if the advance would end on a delete.
*/
static int ends_with_copy(int *aC, int sz){
  while( sz>0 && (aC[0]>0 || aC[1]>0 || aC[2]>0) ){
    if( aC[0]>=sz ) return 1;
    sz -= aC[0];
    if( aC[1]>sz ) return 0;
    sz -= aC[1];
    aC += 3;
  }
  return 1;
}

/*
** aC[] is an "edit triple" for changes from A to B.  Advance through
** this triple to determine the number of lines to bypass on B in order
** to match an advance of sz lines on A.
*/
static int skip_conflict(
  int *aC,             /* Array of integer triples describing the edit */
  int i,               /* Index in aC[] of current location */
  int sz,              /* Lines of A that have been skipped */
  unsigned int *pLn    /* OUT: Lines of B to skip to keep aligment with A */
){
  *pLn = 0;
  while( sz>0 ){
    if( aC[i]==0 && aC[i+1]==0 && aC[i+2]==0 ) break;
    if( aC[i]>=sz ){
      aC[i] -= sz;
      *pLn += sz;
      break;
    }
    *pLn += aC[i];
    *pLn += aC[i+2];
    sz -= aC[i] + aC[i+1];
    i += 3;
  }
  return i;
}

/*
** Do a three-way merge.  Initialize pOut to contain the result.
**
** The merge is an edit against pV2.  Both pV1 and pV2 have a
** common origin at pPivot.  Apply the changes of pPivot ==> pV1
** to pV2.
**
** The return is 0 upon complete success. If any input file is binary,
** -1 is returned and pOut is unmodified.  If there are merge
** conflicts, the merge proceeds as best as it can and the number
** of conflicts is returns
*/
int merge_three_blobs(MergeBuilder *p){
  int *aC1;              /* Changes from pPivot to pV1 */
  int *aC2;              /* Changes from pPivot to pV2 */
  int i1, i2;            /* Index into aC1[] and aC2[] */
  int nCpy, nDel, nIns;  /* Number of lines to copy, delete, or insert */
  int limit1, limit2;    /* Sizes of aC1[] and aC2[] */
  int nConflict = 0;     /* Number of merge conflicts seen so far */
  DiffConfig DCfg;

  /* Compute the edits that occur from pPivot => pV1 (into aC1)
  ** and pPivot => pV2 (into aC2).  Each of the aC1 and aC2 arrays is
  ** an array of integer triples.  Within each triple, the first integer
  ** is the number of lines of text to copy directly from the pivot,
  ** the second integer is the number of lines of text to omit from the
  ** pivot, and the third integer is the number of lines of text that are
  ** inserted.  The edit array ends with a triple of 0,0,0.
  */
  diff_config_init(&DCfg, 0);
  DCfg.diffFlags = p->diffFlags;
  aC1 = text_diff(p->pPivot, p->pV1, 0, &DCfg);
  aC2 = text_diff(p->pPivot, p->pV2, 0, &DCfg);
  if( aC1==0 || aC2==0 ){
    free(aC1);
    free(aC2);
    return -1;
  }

  blob_rewind(p->pV1);        /* Rewind inputs:  Needed to reconstruct output */
  blob_rewind(p->pV2);
  blob_rewind(p->pPivot);

  /* Determine the length of the aC1[] and aC2[] change vectors */
  p->mxPivot = 0;
  p->mxV1 = 0;
  for(i1=0; aC1[i1] || aC1[i1+1] || aC1[i1+2]; i1+=3){
    p->mxPivot += aC1[i1] + aC1[i1+1];
    p->mxV1 += aC1[i1] + aC1[i1+2];
  }
  limit1 = i1;
  p->mxV2 = 0;
  for(i2=0; aC2[i2] || aC2[i2+1] || aC2[i2+2]; i2+=3){
    p->mxV2 += aC2[i2] + aC2[i2+2];
  }
  limit2 = i2;

  /* Output header text and do any other required initialization */
  p->xStart(p);

  /* Loop over the two edit vectors and use them to compute merged text
  ** which is written into pOut.  i1 and i2 are multiples of 3 which are
  ** indices into aC1[] and aC2[] to the edit triple currently being
  ** processed
  */
  i1 = i2 = 0;
  while( i1<limit1 && i2<limit2 ){
    if( aC1[i1]>0 && aC2[i2]>0 ){
      /* Output text that is unchanged in both V1 and V2 */
      nCpy = min(aC1[i1], aC2[i2]);
      p->xSame(p, nCpy);
      aC1[i1] -= nCpy;
      aC2[i2] -= nCpy;
    }else
    if( aC1[i1] >= aC2[i2+1] && aC1[i1]>0 && aC2[i2+1]+aC2[i2+2]>0 ){
      /* Output edits to V2 that occurs within unchanged regions of V1 */
      nDel = aC2[i2+1];
      nIns = aC2[i2+2];
      p->xChngV2(p, nDel, nIns);
      aC1[i1] -= nDel;
      i2 += 3;
    }else
    if( aC2[i2] >= aC1[i1+1] && aC2[i2]>0 && aC1[i1+1]+aC1[i1+2]>0 ){
      /* Output edits to V1 that occur within unchanged regions of V2 */
      nDel = aC1[i1+1];
      nIns = aC1[i1+2];
      p->xChngV1(p, nDel, nIns);
      aC2[i2] -= nDel;
      i1 += 3;
    }else
    if( sameEdit(&aC1[i1], &aC2[i2], p->pV1, p->pV2) ){
      /* Output edits that are identical in both V1 and V2. */
      assert( aC1[i1]==0 );
      nDel = aC1[i1+1];
      nIns = aC1[i1+2];
      p->xChngBoth(p, nDel, nIns);
      i1 += 3;
      i2 += 3;
    }else
    {
      /* We have found a region where different edits to V1 and V2 overlap.
      ** This is a merge conflict.  Find the size of the conflict, then
      ** output both possible edits separated by distinctive marks.
      */
      unsigned int sz = 1;    /* Size of the conflict in the pivot, in lines */
      unsigned int nV1, nV2;  /* Size of conflict in V1 and V2, in lines */
      nConflict++;
      while( !ends_with_copy(&aC1[i1], sz) || !ends_with_copy(&aC2[i2], sz) ){
        sz++;
      }
      i1 = skip_conflict(aC1, i1, sz, &nV1);
      i2 = skip_conflict(aC2, i2, sz, &nV2);
      p->xConflict(p, sz, nV1, nV2);
    }
 
    /* If we are finished with an edit triple, advance to the next
    ** triple.
    */
    if( i1<limit1 && aC1[i1]==0 && aC1[i1+1]==0 && aC1[i1+2]==0 ) i1+=3;
    if( i2<limit2 && aC2[i2]==0 && aC2[i2+1]==0 && aC2[i2+2]==0 ) i2+=3;
  }

  /* When one of the two edit vectors reaches its end, there might still
  ** be an insert in the other edit vector.  Output this remaining
  ** insert.
  */
  if( i1<limit1 && aC1[i1+2]>0 ){
    p->xChngV1(p, 0, aC1[i1+2]);
  }else if( i2<limit2 && aC2[i2+2]>0 ){
    p->xChngV2(p, 0, aC2[i2+2]);
  }

  /* Output footer text */
  p->xEnd(p);

  free(aC1);
  free(aC2);
  return nConflict;
}

/*
** Return true if the input string contains a merge marker on a line by
** itself.
*/
int contains_merge_marker(Blob *p){
  int i, j;
  int len = (int)strlen(mergeMarker[0]);
  const char *z = blob_buffer(p);
  int n = blob_size(p) - len + 1;
  assert( len==(int)strlen(mergeMarker[1]) );
  assert( len==(int)strlen(mergeMarker[2]) );
  assert( len==(int)strlen(mergeMarker[3]) );
  assert( len==(int)strlen(mergeMarker[4]) );
  assert( count(mergeMarker)==5 );
  for(i=0; i<n; ){
    for(j=0; j<4; j++){
      if( (memcmp(&z[i], mergeMarker[j], len)==0) ){
        return 1;
      }
    }
    while( i<n && z[i]!='\n' ){ i++; }
    while( i<n && (z[i]=='\n' || z[i]=='\r') ){ i++; }
  }
  return 0;
}

/*
** Return true if the named file contains an unresolved merge marker line.
*/
int file_contains_merge_marker(const char *zFullpath){
  Blob file;
  int rc;
  blob_read_from_file(&file, zFullpath, ExtFILE);
  rc = contains_merge_marker(&file);
  blob_reset(&file);
  return rc;
}

/*
** Show merge output in a Tcl/Tk window, in response to the --tk option
** to the "merge" or "3-way-merge" command.
**
** If fossil has direct access to a Tcl interpreter (either loaded
** dynamically through stubs or linked in statically), we can use it
** directly. Otherwise:
** (1) Write the Tcl/Tk script used for rendering into a temp file.
** (2) Invoke "tclsh" on the temp file using fossil_system().
** (3) Delete the temp file.
*/
void merge_tk(const char *zSubCmd, int firstArg){
  int i;
  Blob script;
  const char *zTempFile = 0;
  char *zCmd;
  const char *zTclsh;
  const char *zCnt;
  int bDarkMode = find_option("dark",0,0)!=0;
  int nContext;
  zCnt = find_option("context", "c", 1);
  if( zCnt==0 ){
    nContext = 6;
  }else{
    nContext = atoi(zCnt);
    if( nContext<0 ) nContext = 0xfffffff;
  }
  blob_zero(&script);
  blob_appendf(&script, "set ncontext %d\n", nContext);
  blob_appendf(&script, "set fossilcmd {| \"%/\" %s -tcl",
               g.nameOfExe, zSubCmd);
  find_option("tcl",0,0);
  find_option("debug",0,0);
  zTclsh = find_option("tclsh",0,1);
  if( zTclsh==0 ){
    zTclsh = db_get("tclsh",0);
  }
  /* The undocumented --script FILENAME option causes the Tk script to
  ** be written into the FILENAME instead of being run.  This is used
  ** for testing and debugging. */
  zTempFile = find_option("script",0,1);
  verify_all_options();

  if( (g.argc - firstArg)!=3 ){
    fossil_fatal("Requires 3 filename arguments");
  }

  for(i=firstArg; i<g.argc; i++){
    const char *z = g.argv[i];
    if( sqlite3_strglob("*}*",z) ){
      blob_appendf(&script, " {%/}", z);
    }else{
      int j;
      blob_append(&script, " ", 1);
      for(j=0; z[j]; j++) blob_appendf(&script, "\\%03o", (unsigned char)z[j]);
    }
  }
  blob_appendf(&script, "}\nset darkmode %d\n", bDarkMode);
  blob_appendf(&script, "%s", builtin_file("merge.tcl", 0));
  if( zTempFile ){
    blob_write_to_file(&script, zTempFile);
    fossil_print("To see the merge, run: %s \"%s\"\n", zTclsh, zTempFile);
  }else{
#if defined(FOSSIL_ENABLE_TCL)
    Th_FossilInit(TH_INIT_DEFAULT);
    if( evaluateTclWithEvents(g.interp, &g.tcl, blob_str(&script),
                              blob_size(&script), 1, 1, 0)==TCL_OK ){
      blob_reset(&script);
      return;
    }
    /*
     * If evaluation of the Tcl script fails, the reason may be that Tk
     * could not be found by the loaded Tcl, or that Tcl cannot be loaded
     * dynamically (e.g. x64 Tcl with x86 Fossil).  Therefore, fallback
     * to using the external "tclsh", if available.
     */
#endif
    zTempFile = write_blob_to_temp_file(&script);
    zCmd = mprintf("%$ %$", zTclsh, zTempFile);
    fossil_system(zCmd);
    file_delete(zTempFile);
    fossil_free(zCmd);
  }
  blob_reset(&script);
}


/*
** COMMAND: 3-way-merge*
**
** Usage: %fossil 3-way-merge BASELINE V1 V2 [MERGED]
**
** Inputs are files BASELINE, V1, and V2.  The file MERGED is generated
** as output.  If no MERGED file is specified, output is sent to
** stdout.
**
** BASELINE is a common ancestor of two files V1 and V2 that have diverging
** edits.  The generated output file MERGED is the combination of all
** changes in both V1 and V2.
**
** This command has no effect on the Fossil repository.  It is a utility
** command made available for the convenience of users.  This command can
** be used, for example, to help import changes from an upstream project.
**
** Suppose an upstream project has a file named "Xup.c" which is imported
** with modifications to the local project as "Xlocal.c".  Suppose further
** that the "Xbase.c" is an exact copy of the last imported "Xup.c".
** Then to import the latest "Xup.c" while preserving all the local changes:
**
**      fossil 3-way-merge Xbase.c Xlocal.c Xup.c Xlocal.c
**      cp Xup.c Xbase.c
**      # Verify that everything still works
**      fossil commit
**
*/
void merge_3way_cmd(void){
  MergeBuilder s;
  int nConflict;
  Blob pivot, v1, v2, out;
  int noWarn = 0;
  const char *zCnt;

  if( find_option("tk", 0, 0)!=0 ){
    merge_tk("3-way-merge", 2);
    return;
  }
  mergebuilder_init_text(&s);
  if( find_option("debug", 0, 0) ){
    mergebuilder_init(&s);
  }
  if( find_option("tcl", 0, 0) ){
    mergebuilder_init_tcl(&s);
    noWarn = 1;
  }
  zCnt = find_option("context", "c", 1);
  if( zCnt ){
    s.nContext = atoi(zCnt);
    if( s.nContext<0 ) s.nContext = 0xfffffff;
  }else{
    s.nContext = 6;
  }
  blob_zero(&pivot); s.pPivot = &pivot;
  blob_zero(&v1);    s.pV1 = &v1;
  blob_zero(&v2);    s.pV2 = &v2;
  blob_zero(&out);   s.pOut = &out;

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=6 && g.argc!=5 ){
    usage("[OPTIONS] PIVOT V1 V2 [MERGED]");
  }
  s.zPivot = file_tail(g.argv[2]);
  s.zV1 = file_tail(g.argv[3]);
  s.zV2 = file_tail(g.argv[4]);
  if( blob_read_from_file(s.pPivot, g.argv[2], ExtFILE)<0 ){
    fossil_fatal("cannot read %s", g.argv[2]);
  }
  if( blob_read_from_file(s.pV1, g.argv[3], ExtFILE)<0 ){
    fossil_fatal("cannot read %s", g.argv[3]);
  }
  if( blob_read_from_file(s.pV2, g.argv[4], ExtFILE)<0 ){
    fossil_fatal("cannot read %s", g.argv[4]);
  }
  nConflict = merge_three_blobs(&s);
  if( g.argc==6 ){
    s.zOut = file_tail(g.argv[5]);
    blob_write_to_file(s.pOut, g.argv[5]);
  }else{
    s.zOut = "(Merge Result)";
    blob_write_to_file(s.pOut, "-");
  }
  s.xDestroy(&s);
  blob_reset(&pivot);
  blob_reset(&v1);
  blob_reset(&v2);
  blob_reset(&out);
  if( nConflict>0 && !noWarn ){
    fossil_warning("WARNING: %d merge conflicts", nConflict);
  }
}

/*
** aSubst is an array of string pairs.  The first element of each pair is
** a string that begins with %.  The second element is a replacement for that
** string.
**
** This routine makes a copy of zInput into memory obtained from malloc and
** performance all applicable substitutions on that string.
*/
char *string_subst(const char *zInput, int nSubst, const char **azSubst){
  Blob x;
  int i, j;
  blob_zero(&x);
  while( zInput[0] ){
    for(i=0; zInput[i] && zInput[i]!='%'; i++){}
    if( i>0 ){
      blob_append(&x, zInput, i);
      zInput += i;
    }
    if( zInput[0]==0 ) break;
    for(j=0; j<nSubst; j+=2){
      int n = strlen(azSubst[j]);
      if( strncmp(zInput, azSubst[j], n)==0 ){
        blob_append(&x, azSubst[j+1], -1);
        zInput += n;
        break;
      }
    }
    if( j>=nSubst ){
      blob_append(&x, "%", 1);
      zInput++;
    }
  }
  return blob_str(&x);
}

#if INTERFACE
/*
** Flags to the 3-way merger
*/
#define MERGE_DRYRUN          0x0001
/*
** The MERGE_KEEP_FILES flag specifies that merge_3way() should retain
** its temporary files on error. By default they are removed after the
** merge, regardless of success or failure.
*/
#define MERGE_KEEP_FILES      0x0002
#endif


/*
** This routine is a wrapper around merge_three_blobs() with the following
** enhancements:
**
**    (1) If the merge-command is defined, then use the external merging
**        program specified instead of the built-in blob-merge to do the
**        merging.  Panic if the external merger fails.
**        ** Not currently implemented **
**
**    (2) If gmerge-command is defined and there are merge conflicts in
**        merge_three_blobs() then invoke the external graphical merger 
**        to resolve the conflicts.
**
**    (3) If a merge conflict occurs and gmerge-command is not defined,
**        then write the pivot, original, and merge-in files to the
**        filesystem.
*/
int merge_3way(
  Blob *pPivot,       /* Common ancestor (older) */
  const char *zV1,    /* Name of file for version merging into (mine) */
  Blob *pV2,          /* Version merging from (yours) */
  Blob *pOut,         /* Output written here */
  unsigned mergeFlags /* Flags that control operation */
){
  Blob v1;               /* Content of zV1 */
  int rc;                /* Return code of subroutines and this routine */
  const char *zGMerge;   /* Name of the gmerge command */
  MergeBuilder s;        /* The merge state */

  mergebuilder_init_text(&s);
  s.pPivot = pPivot;
  s.pV1 = &v1;
  s.pV2 = pV2;
  blob_zero(pOut);
  s.pOut = pOut;
  blob_read_from_file(s.pV1, zV1, ExtFILE);
  rc = merge_three_blobs(&s);
  zGMerge = rc<=0 ? 0 : db_get("gmerge-command", 0);
  if( (mergeFlags & MERGE_DRYRUN)==0
      && ((zGMerge!=0 && zGMerge[0]!=0)
          || (rc!=0 && (mergeFlags & MERGE_KEEP_FILES)!=0)) ){
    char *zPivot;       /* Name of the pivot file */
    char *zOrig;        /* Name of the original content file */
    char *zOther;       /* Name of the merge file */

    zPivot = file_newname(zV1, "baseline", 1);
    blob_write_to_file(s.pPivot, zPivot);
    zOrig = file_newname(zV1, "original", 1);
    blob_write_to_file(s.pV1, zOrig);
    zOther = file_newname(zV1, "merge", 1);
    blob_write_to_file(s.pV2, zOther);
    if( rc>0 ){
      if( zGMerge && zGMerge[0] ){
        char *zOut;     /* Temporary output file */
        char *zCmd;     /* Command to invoke */
        const char *azSubst[8];  /* Strings to be substituted */
        zOut = file_newname(zV1, "output", 1);
        azSubst[0] = "%baseline";  azSubst[1] = zPivot;
        azSubst[2] = "%original";  azSubst[3] = zOrig;
        azSubst[4] = "%merge";     azSubst[5] = zOther;
        azSubst[6] = "%output";    azSubst[7] = zOut;
        zCmd = string_subst(zGMerge, 8, azSubst);
        printf("%s\n", zCmd); fflush(stdout);
        fossil_system(zCmd);
        if( file_size(zOut, RepoFILE)>=0 ){
          blob_read_from_file(pOut, zOut, ExtFILE);
          file_delete(zOut);
        }
        fossil_free(zCmd);
        fossil_free(zOut);
      }
    }
    if( (mergeFlags & MERGE_KEEP_FILES)==0 ){
      file_delete(zPivot);
      file_delete(zOrig);
      file_delete(zOther);
    }
    fossil_free(zPivot);
    fossil_free(zOrig);
    fossil_free(zOther);
  }
  blob_reset(&v1);
  s.xDestroy(&s);
  return rc;
}
