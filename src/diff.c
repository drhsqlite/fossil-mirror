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


#if INTERFACE
/*
** Flag parameters to the text_diff() routine used to control the formatting
** of the diff output.
*/
#define DIFF_CONTEXT_MASK ((u64)0x0000ffff) /* Lines of context. Default if 0 */
#define DIFF_WIDTH_MASK   ((u64)0x00ff0000) /* side-by-side column width */
#define DIFF_IGNORE_EOLWS ((u64)0x01000000) /* Ignore end-of-line whitespace */
#define DIFF_IGNORE_ALLWS ((u64)0x03000000) /* Ignore all whitespace */
#define DIFF_SIDEBYSIDE   ((u64)0x04000000) /* Generate a side-by-side diff */
#define DIFF_VERBOSE      ((u64)0x08000000) /* Missing shown as empty files */
#define DIFF_BRIEF        ((u64)0x10000000) /* Show filenames only */
#define DIFF_HTML         ((u64)0x20000000) /* Render for HTML */
#define DIFF_LINENO       ((u64)0x40000000) /* Show line numbers */
#define DIFF_NUMSTAT      ((u64)0x80000000) /* Show line count of changes */
#define DIFF_NOOPT        (((u64)0x01)<<32) /* Suppress optimizations (debug) */
#define DIFF_INVERT       (((u64)0x02)<<32) /* Invert the diff (debug) */
#define DIFF_CONTEXT_EX   (((u64)0x04)<<32) /* Use context even if zero */
#define DIFF_NOTTOOBIG    (((u64)0x08)<<32) /* Only display if not too big */
#define DIFF_STRIP_EOLCR  (((u64)0x10)<<32) /* Strip trailing CR */
#define DIFF_SLOW_SBS     (((u64)0x20)<<32) /* Better but slower side-by-side */

/*
** These error messages are shared in multiple locations.  They are defined
** here for consistency.
*/
#define DIFF_CANNOT_COMPUTE_BINARY \
    "cannot compute difference between binary files\n"

#define DIFF_CANNOT_COMPUTE_SYMLINK \
    "cannot compute difference between symlink and regular file\n"

#define DIFF_TOO_MANY_CHANGES \
    "more than 10,000 changes\n"

#define DIFF_WHITESPACE_ONLY \
    "whitespace changes only\n"

/*
** Maximum length of a line in a text file, in bytes.  (2**13 = 8192 bytes)
*/
#define LENGTH_MASK_SZ  13
#define LENGTH_MASK     ((1<<LENGTH_MASK_SZ)-1)

#endif /* INTERFACE */

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
  unsigned short indent;  /* Indent of the line. Only !=0 with -w/-Z option */
  unsigned short n;     /* number of bytes */
  unsigned int iNext;   /* 1+(Index of next line with same the same hash) */

  /* an array of DLine elements serves two purposes.  The fields
  ** above are one per line of input text.  But each entry is also
  ** a bucket in a hash table, as follows: */
  unsigned int iHash;   /* 1+(first entry in the hash chain) */
};

/*
** Length of a dline
*/
#define LENGTH(X)   ((X)->n)

/*
** A context for running a raw diff.
**
** The aEdit[] array describes the raw diff.  Each triple of integers in
** aEdit[] means:
**
**   (1) COPY:   Number of lines aFrom and aTo have in common
**   (2) DELETE: Number of lines found only in aFrom
**   (3) INSERT: Number of lines found only in aTo
**
** The triples repeat until all lines of both aFrom and aTo are accounted
** for.
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
  int (*same_fn)(const DLine*,const DLine*); /* comparison function */
};

/*
** Count the number of lines in the input string.  Include the last line
** in the count even if it lacks the \n terminator.  If an empty string
** is specified, the number of lines is zero.  For the purposes of this
** function, a string is considered empty if it contains no characters
** -OR- it contains only NUL characters.
*/
static int count_lines(
  const char *z,
  int n,
  int *pnLine
){
  int nLine;
  const char *zNL, *z2;
  for(nLine=0, z2=z; (zNL = strchr(z2,'\n'))!=0; z2=zNL+1, nLine++){}
  if( z2[0]!='\0' ){
    nLine++;
    do{ z2++; }while( z2[0]!='\0' );
  }
  if( n!=(int)(z2-z) ) return 0;
  if( pnLine ) *pnLine = nLine;
  return 1;
}

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
**
** Profiling show that in most cases this routine consumes the bulk of
** the CPU time on a diff.
*/
static DLine *break_into_lines(
  const char *z,
  int n,
  int *pnLine,
  u64 diffFlags
){
  int nLine, i, k, nn, s, x;
  unsigned int h, h2;
  DLine *a;
  const char *zNL;

  if( count_lines(z, n, &nLine)==0 ){
    return 0;
  }
  assert( nLine>0 || z[0]=='\0' );
  a = fossil_malloc( sizeof(a[0])*nLine );
  memset(a, 0, sizeof(a[0])*nLine);
  if( nLine==0 ){
    *pnLine = 0;
    return a;
  }
  i = 0;
  do{
    zNL = strchr(z,'\n');
    if( zNL==0 ) zNL = z+n;
    nn = (int)(zNL - z);
    if( nn>LENGTH_MASK ){
      fossil_free(a);
      return 0;
    }
    a[i].z = z;
    k = nn;
    if( diffFlags & DIFF_STRIP_EOLCR ){
      if( k>0 && z[k-1]=='\r' ){ k--; }
    }
    a[i].n = k;
    s = 0;
    if( diffFlags & DIFF_IGNORE_EOLWS ){
      while( k>0 && fossil_isspace(z[k-1]) ){ k--; }
    }
    if( (diffFlags & DIFF_IGNORE_ALLWS)==DIFF_IGNORE_ALLWS ){
      int numws = 0;
      while( s<k && fossil_isspace(z[s]) ){ s++; }
      for(h=0, x=s; x<k; x++){
        char c = z[x];
        if( fossil_isspace(c) ){
          ++numws;
        }else{
          h += c;
          h *= 0x9e3779b1;
        }
      }
      k -= numws;
    }else{
      for(h=0, x=s; x<k; x++){
        h += z[x];
        h *= 0x9e3779b1;
      }
    }
    a[i].indent = s;
    a[i].h = h = (h<<LENGTH_MASK_SZ) | (k-s);
    h2 = h % nLine;
    a[i].iNext = a[h2].iHash;
    a[h2].iHash = i+1;
    z += nn+1; n -= nn+1;
    i++;
  }while( zNL[0]!='\0' && zNL[1]!='\0' );
  assert( i==nLine );

  /* Return results */
  *pnLine = nLine;
  return a;
}

/*
** Return true if two DLine elements are identical.
*/
static int same_dline(const DLine *pA, const DLine *pB){
  return pA->h==pB->h && memcmp(pA->z,pB->z, pA->h&LENGTH_MASK)==0;
}

/*
** Return true if two DLine elements are identical, ignoring
** all whitespace. The indent field of pA/pB already points
** to the first non-space character in the string.
*/

static int same_dline_ignore_allws(const DLine *pA, const DLine *pB){
  int a = pA->indent, b = pB->indent;
  if( pA->h==pB->h ){
    while( a<pA->n || b<pB->n ){
      if( a<pA->n && b<pB->n && pA->z[a++] != pB->z[b++] ) return 0;
      while( a<pA->n && fossil_isspace(pA->z[a])) ++a;
      while( b<pB->n && fossil_isspace(pB->z[b])) ++b;
    }
    return pA->n-a == pB->n-b;
  }
  return 0;
}

/*
** Return true if the regular expression *pRe matches any of the
** N dlines
*/
static int re_dline_match(
  ReCompiled *pRe,    /* The regular expression to be matched */
  DLine *aDLine,      /* First of N DLines to compare against */
  int N               /* Number of DLines to check */
){
  while( N-- ){
    if( re_match(pRe, (const unsigned char *)aDLine->z, LENGTH(aDLine)) ){
      return 1;
    }
    aDLine++;
  }
  return 0;
}

/*
** Append a single line of context-diff output to pOut.
*/
static void appendDiffLine(
  Blob *pOut,         /* Where to write the line of output */
  char cPrefix,       /* One of " ", "+",  or "-" */
  DLine *pLine,       /* The line to be output */
  int html,           /* True if generating HTML.  False for plain text */
  ReCompiled *pRe     /* Colorize only if line matches this Regex */
){
  blob_append(pOut, &cPrefix, 1);
  if( html ){
    if( pRe && re_dline_match(pRe, pLine, 1)==0 ){
      cPrefix = ' ';
    }else if( cPrefix=='+' ){
      blob_append(pOut, "<span class=\"diffadd\">", -1);
    }else if( cPrefix=='-' ){
      blob_append(pOut, "<span class=\"diffrm\">", -1);
    }
    htmlize_to_blob(pOut, pLine->z, pLine->n);
    if( cPrefix!=' ' ){
      blob_append(pOut, "</span>", -1);
    }
  }else{
    blob_append(pOut, pLine->z, pLine->n);
  }
  blob_append(pOut, "\n", 1);
}

/*
** Add two line numbers to the beginning of an output line for a context
** diff.  One or the other of the two numbers might be zero, which means
** to leave that number field blank.  The "html" parameter means to format
** the output for HTML.
*/
static void appendDiffLineno(Blob *pOut, int lnA, int lnB, int html){
  if( html ) blob_append(pOut, "<span class=\"diffln\">", -1);
  if( lnA>0 ){
    blob_appendf(pOut, "%6d ", lnA);
  }else{
    blob_append(pOut, "       ", 7);
  }
  if( lnB>0 ){
    blob_appendf(pOut, "%6d  ", lnB);
  }else{
    blob_append(pOut, "        ", 8);
  }
  if( html ) blob_append(pOut, "</span>", -1);
}

/*
** Given a raw diff p[] in which the p->aEdit[] array has been filled
** in, compute a context diff into pOut.
*/
static void contextDiff(
  DContext *p,      /* The difference */
  Blob *pOut,       /* Output a context diff to here */
  ReCompiled *pRe,  /* Only show changes that match this regex */
  u64 diffFlags     /* Flags controlling the diff format */
){
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
  static int nChunk = 0;  /* Number of diff chunks seen so far */
  int nContext;    /* Number of lines of context */
  int showLn;      /* Show line numbers */
  int html;        /* Render as HTML */
  int showDivider = 0;  /* True to show the divider between diff blocks */

  nContext = diff_context_lines(diffFlags);
  showLn = (diffFlags & DIFF_LINENO)!=0;
  html = (diffFlags & DIFF_HTML)!=0;
  A = p->aFrom;
  B = p->aTo;
  R = p->aEdit;
  mxr = p->nEdit;
  while( mxr>2 && R[mxr-1]==0 && R[mxr-2]==0 ){ mxr -= 3; }
  for(r=0; r<mxr; r += 3*nr){
    /* Figure out how many triples to show in a single block */
    for(nr=1; R[r+nr*3]>0 && R[r+nr*3]<nContext*2; nr++){}
    /* printf("r=%d nr=%d\n", r, nr); */

    /* If there is a regex, skip this block (generate no diff output)
    ** if the regex matches or does not match both insert and delete.
    ** Only display the block if one side matches but the other side does
    ** not.
    */
    if( pRe ){
      int hideBlock = 1;
      int xa = a, xb = b;
      for(i=0; hideBlock && i<nr; i++){
        int c1, c2;
        xa += R[r+i*3];
        xb += R[r+i*3];
        c1 = re_dline_match(pRe, &A[xa], R[r+i*3+1]);
        c2 = re_dline_match(pRe, &B[xb], R[r+i*3+2]);
        hideBlock = c1==c2;
        xa += R[r+i*3+1];
        xb += R[r+i*3+2];
      }
      if( hideBlock ){
        a = xa;
        b = xb;
        continue;
      }
    }

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

    /* Show the header for this block, or if we are doing a modified
    ** context diff that contains line numbers, show the separator from
    ** the previous block.
    */
    nChunk++;
    if( showLn ){
      if( !showDivider ){
        /* Do not show a top divider */
        showDivider = 1;
      }else if( html ){
        blob_appendf(pOut, "<span class=\"diffhr\">%.80c</span>\n", '.');
      }else{
        blob_appendf(pOut, "%.80c\n", '.');
      }
      if( html ) blob_appendf(pOut, "<span id=\"chunk%d\"></span>", nChunk);
    }else{
      if( html ) blob_appendf(pOut, "<span class=\"diffln\">");
      /*
       * If the patch changes an empty file or results in an empty file,
       * the block header must use 0,0 as position indicator and not 1,0.
       * Otherwise, patch would be confused and may reject the diff.
       */
      blob_appendf(pOut,"@@ -%d,%d +%d,%d @@",
        na ? a+skip+1 : 0, na,
        nb ? b+skip+1 : 0, nb);
      if( html ) blob_appendf(pOut, "</span>");
      blob_append(pOut, "\n", 1);
    }

    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    for(j=0; j<m; j++){
      if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1, html);
      appendDiffLine(pOut, ' ', &A[a+j], html, 0);
    }
    a += m;
    b += m;

    /* Show the differences */
    for(i=0; i<nr; i++){
      m = R[r+i*3+1];
      for(j=0; j<m; j++){
        if( showLn ) appendDiffLineno(pOut, a+j+1, 0, html);
        appendDiffLine(pOut, '-', &A[a+j], html, pRe);
      }
      a += m;
      m = R[r+i*3+2];
      for(j=0; j<m; j++){
        if( showLn ) appendDiffLineno(pOut, 0, b+j+1, html);
        appendDiffLine(pOut, '+', &B[b+j], html, pRe);
      }
      b += m;
      if( i<nr-1 ){
        m = R[r+i*3+3];
        for(j=0; j<m; j++){
          if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1, html);
          appendDiffLine(pOut, ' ', &A[a+j], html, 0);
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
      if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1, html);
      appendDiffLine(pOut, ' ', &A[a+j], html, 0);
    }
  }
}

/*
** Status of a single output line
*/
typedef struct SbsLine SbsLine;
struct SbsLine {
  Blob *apCols[5];         /* Array of pointers to output columns */
  int width;               /* Maximum width of a column in the output */
  unsigned char escHtml;   /* True to escape html characters */
  int iStart;              /* Write zStart prior to character iStart */
  const char *zStart;      /* A <span> tag */
  int iEnd;                /* Write </span> prior to character iEnd */
  int iStart2;             /* Write zStart2 prior to character iStart2 */
  const char *zStart2;     /* A <span> tag */
  int iEnd2;               /* Write </span> prior to character iEnd2 */
  ReCompiled *pRe;         /* Only colorize matching lines, if not NULL */
};

/*
** Column indices for SbsLine.apCols[]
*/
#define SBS_LNA  0     /* Left line number */
#define SBS_TXTA 1     /* Left text */
#define SBS_MKR  2     /* Middle separator column */
#define SBS_LNB  3     /* Right line number */
#define SBS_TXTB 4     /* Right text */

/*
** Append newlines to all columns.
*/
static void sbsWriteNewlines(SbsLine *p){
  int i;
  for( i=p->escHtml ? SBS_LNA : SBS_TXTB; i<=SBS_TXTB; i++ ){
    blob_append(p->apCols[i], "\n", 1);
  }
}

/*
** Append n spaces to the column.
*/
static void sbsWriteSpace(SbsLine *p, int n, int col){
  blob_appendf(p->apCols[col], "%*s", n, "");
}

/*
** Write the text of pLine into column iCol of p.
**
** If outputting HTML, write the full line.  Otherwise, only write the
** width characters.  Translate tabs into spaces.  Add newlines if col
** is SBS_TXTB.  Translate HTML characters if escHtml is true.  Pad the
** rendering to width bytes if col is SBS_TXTA and escHtml is false.
**
** This comment contains multibyte unicode characters (ü, Æ, ð) in order
** to test the ability of the diff code to handle such characters.
*/
static void sbsWriteText(SbsLine *p, DLine *pLine, int col){
  Blob *pCol = p->apCols[col];
  int n = pLine->n;
  int i;   /* Number of input characters consumed */
  int k;   /* Cursor position */
  int needEndSpan = 0;
  const char *zIn = pLine->z;
  int w = p->width;
  int colorize = p->escHtml;
  if( colorize && p->pRe && re_dline_match(p->pRe, pLine, 1)==0 ){
    colorize = 0;
  }
  for(i=k=0; (p->escHtml || k<w) && i<n; i++, k++){
    char c = zIn[i];
    if( colorize ){
      if( i==p->iStart ){
        int x = strlen(p->zStart);
        blob_append(pCol, p->zStart, x);
        needEndSpan = 1;
        if( p->iStart2 ){
          p->iStart = p->iStart2;
          p->zStart = p->zStart2;
          p->iStart2 = 0;
        }
      }else if( i==p->iEnd ){
        blob_append(pCol, "</span>", 7);
        needEndSpan = 0;
        if( p->iEnd2 ){
          p->iEnd = p->iEnd2;
          p->iEnd2 = 0;
        }
      }
    }
    if( c=='\t' && !p->escHtml ){
      blob_append(pCol, " ", 1);
      while( (k&7)!=7 && (p->escHtml || k<w) ){
        blob_append(pCol, " ", 1);
        k++;
      }
    }else if( c=='\r' || c=='\f' ){
      blob_append(pCol, " ", 1);
    }else if( c=='<' && p->escHtml ){
      blob_append(pCol, "&lt;", 4);
    }else if( c=='&' && p->escHtml ){
      blob_append(pCol, "&amp;", 5);
    }else if( c=='>' && p->escHtml ){
      blob_append(pCol, "&gt;", 4);
    }else if( c=='"' && p->escHtml ){
      blob_append(pCol, "&quot;", 6);
    }else{
      blob_append(pCol, &zIn[i], 1);
      if( (c&0xc0)==0x80 ) k--;
    }
  }
  if( needEndSpan ){
    blob_append(pCol, "</span>", 7);
  }
  if( col==SBS_TXTB ){
    sbsWriteNewlines(p);
  }else if( !p->escHtml ){
    sbsWriteSpace(p, w-k, SBS_TXTA);
  }
}

/*
** Append a column to the final output blob.
*/
static void sbsWriteColumn(Blob *pOut, Blob *pCol, int col){
  blob_appendf(pOut,
    "<td><div class=\"diff%scol\">\n"
    "<pre>\n"
    "%s"
    "</pre>\n"
    "</div></td>\n",
    (col % 3) ? (col == SBS_MKR ? "mkr" : "txt") : "ln",
    blob_str(pCol)
  );
}

/*
** Append a separator line to column iCol
*/
static void sbsWriteSep(SbsLine *p, int len, int col){
  char ch = '.';
  if( len<1 ){
    len = 1;
    ch = ' ';
  }
  blob_appendf(p->apCols[col], "<span class=\"diffhr\">%.*c</span>\n", len, ch);
}

/*
** Append the appropriate marker into the center column of the diff.
*/
static void sbsWriteMarker(SbsLine *p, const char *zTxt, const char *zHtml){
  blob_append(p->apCols[SBS_MKR], p->escHtml ? zHtml : zTxt, -1);
}

/*
** Append a line number to the column.
*/
static void sbsWriteLineno(SbsLine *p, int ln, int col){
  if( p->escHtml ){
    blob_appendf(p->apCols[col], "%d", ln+1);
  }else{
    char zLn[7];
    sqlite3_snprintf(7, zLn, "%5d ", ln+1);
    blob_appendf(p->apCols[col], "%s ", zLn);
  }
}

/*
** The two text segments zLeft and zRight are known to be different on
** both ends, but they might have  a common segment in the middle.  If
** they do not have a common segment, return 0.  If they do have a large
** common segment, return 1 and before doing so set:
**
**   aLCS[0] = start of the common segment in zLeft
**   aLCS[1] = end of the common segment in zLeft
**   aLCS[2] = start of the common segment in zLeft
**   aLCS[3] = end of the common segment in zLeft
**
** This computation is for display purposes only and does not have to be
** optimal or exact.
*/
static int textLCS(
  const char *zLeft,  int nA,       /* String on the left */
  const char *zRight,  int nB,      /* String on the right */
  int *aLCS                         /* Identify bounds of LCS here */
){
  const unsigned char *zA = (const unsigned char*)zLeft;    /* left string */
  const unsigned char *zB = (const unsigned char*)zRight;   /* right string */
  int nt;                    /* Number of target points */
  int ti[3];                 /* Index for start of each 4-byte target */
  unsigned int target[3];    /* 4-byte alignment targets */
  unsigned int probe;        /* probe to compare against target */
  int iAS, iAE, iBS, iBE;    /* Range of common segment */
  int i, j;                  /* Loop counters */
  int rc = 0;                /* Result code.  1 for success */

  if( nA<6 || nB<6 ) return 0;
  memset(aLCS, 0, sizeof(int)*4);
  ti[0] = i = nB/2-2;
  target[0] = (zB[i]<<24) | (zB[i+1]<<16) | (zB[i+2]<<8) | zB[i+3];
  probe = 0;
  if( nB<16 ){
    nt = 1;
  }else{
    ti[1] = i = nB/4-2;
    target[1] = (zB[i]<<24) | (zB[i+1]<<16) | (zB[i+2]<<8) | zB[i+3];
    ti[2] = i = (nB*3)/4-2;
    target[2] = (zB[i]<<24) | (zB[i+1]<<16) | (zB[i+2]<<8) | zB[i+3];
    nt = 3;
  }
  probe = (zA[0]<<16) | (zA[1]<<8) | zA[2];
  for(i=3; i<nA; i++){
    probe = (probe<<8) | zA[i];
    for(j=0; j<nt; j++){
      if( probe==target[j] ){
        iAS = i-3;
        iAE = i+1;
        iBS = ti[j];
        iBE = ti[j]+4;
        while( iAE<nA && iBE<nB && zA[iAE]==zB[iBE] ){ iAE++; iBE++; }
        while( iAS>0 && iBS>0 && zA[iAS-1]==zB[iBS-1] ){ iAS--; iBS--; }
        if( iAE-iAS > aLCS[1] - aLCS[0] ){
          aLCS[0] = iAS;
          aLCS[1] = iAE;
          aLCS[2] = iBS;
          aLCS[3] = iBE;
          rc = 1;
        }
      }
    }
  }
  return rc;
}

/*
** Try to shift iStart as far as possible to the left.
*/
static void sbsShiftLeft(SbsLine *p, const char *z){
  int i, j;
  while( (i=p->iStart)>0 && z[i-1]==z[i] ){
    for(j=i+1; j<p->iEnd && z[j-1]==z[j]; j++){}
    if( j<p->iEnd ) break;
    p->iStart--;
    p->iEnd--;
  }
}

/*
** Simplify iStart and iStart2:
**
**    *  If iStart is a null-change then move iStart2 into iStart
**    *  Make sure any null-changes are in canonoical form.
**    *  Make sure all changes are at character boundaries for
**       multi-byte characters.
*/
static void sbsSimplifyLine(SbsLine *p, const char *z){
  if( p->iStart2==p->iEnd2 ){
    p->iStart2 = p->iEnd2 = 0;
  }else if( p->iStart2 ){
    while( p->iStart2>0 && (z[p->iStart2]&0xc0)==0x80 ) p->iStart2--;
    while( (z[p->iEnd2]&0xc0)==0x80 ) p->iEnd2++;
  }
  if( p->iStart==p->iEnd ){
    p->iStart = p->iStart2;
    p->iEnd = p->iEnd2;
    p->zStart = p->zStart2;
    p->iStart2 = 0;
    p->iEnd2 = 0;
  }
  if( p->iStart==p->iEnd ){
    p->iStart = p->iEnd = -1;
  }else if( p->iStart>0 ){
    while( p->iStart>0 && (z[p->iStart]&0xc0)==0x80 ) p->iStart--;
    while( (z[p->iEnd]&0xc0)==0x80 ) p->iEnd++;
  }
}

/*
** Write out lines that have been edited.  Adjust the highlight to cover
** only those parts of the line that actually changed.
*/
static void sbsWriteLineChange(
  SbsLine *p,          /* The SBS output line */
  DLine *pLeft,        /* Left line of the change */
  int lnLeft,          /* Line number for the left line */
  DLine *pRight,       /* Right line of the change */
  int lnRight          /* Line number of the right line */
){
  int nLeft;           /* Length of left line in bytes */
  int nRight;          /* Length of right line in bytes */
  int nShort;          /* Shortest of left and right */
  int nPrefix;         /* Length of common prefix */
  int nSuffix;         /* Length of common suffix */
  const char *zLeft;   /* Text of the left line */
  const char *zRight;  /* Text of the right line */
  int nLeftDiff;       /* nLeft - nPrefix - nSuffix */
  int nRightDiff;      /* nRight - nPrefix - nSuffix */
  int aLCS[4];         /* Bounds of common middle segment */
  static const char zClassRm[]   = "<span class=\"diffrm\">";
  static const char zClassAdd[]  = "<span class=\"diffadd\">";
  static const char zClassChng[] = "<span class=\"diffchng\">";

  nLeft = pLeft->n;
  zLeft = pLeft->z;
  nRight = pRight->n;
  zRight = pRight->z;
  nShort = nLeft<nRight ? nLeft : nRight;

  nPrefix = 0;
  while( nPrefix<nShort && zLeft[nPrefix]==zRight[nPrefix] ){
    nPrefix++;
  }
  if( nPrefix<nShort ){
    while( nPrefix>0 && (zLeft[nPrefix]&0xc0)==0x80 ) nPrefix--;
  }
  nSuffix = 0;
  if( nPrefix<nShort ){
    while( nSuffix<nShort && zLeft[nLeft-nSuffix-1]==zRight[nRight-nSuffix-1] ){
      nSuffix++;
    }
    if( nSuffix<nShort ){
      while( nSuffix>0 && (zLeft[nLeft-nSuffix]&0xc0)==0x80 ) nSuffix--;
    }
    if( nSuffix==nLeft || nSuffix==nRight ) nPrefix = 0;
  }

  /* If the prefix and suffix overlap, that means that we are dealing with
  ** a pure insertion or deletion of text that can have multiple alignments.
  ** Try to find an alignment to begins and ends on whitespace, or on
  ** punctuation, rather than in the middle of a name or number.
  */
  if( nPrefix+nSuffix > nShort ){
    int iBest = -1;
    int iBestVal = -1;
    int i;
    int nLong = nLeft<nRight ? nRight : nLeft;
    int nGap = nLong - nShort;
    for(i=nShort-nSuffix; i<=nPrefix; i++){
       int iVal = 0;
       char c = zLeft[i];
       if( fossil_isspace(c) ){
         iVal += 5;
       }else if( !fossil_isalnum(c) ){
         iVal += 2;
       }
       c = zLeft[i+nGap-1];
       if( fossil_isspace(c) ){
         iVal += 5;
       }else if( !fossil_isalnum(c) ){
         iVal += 2;
       }
       if( iVal>iBestVal ){
         iBestVal = iVal;
         iBest = i;
       }
    }
    nPrefix = iBest;
    nSuffix = nShort - nPrefix;
  }

  /* A single chunk of text inserted on the right */
  if( nPrefix+nSuffix==nLeft ){
    sbsWriteLineno(p, lnLeft, SBS_LNA);
    p->iStart2 = p->iEnd2 = 0;
    p->iStart = p->iEnd = -1;
    sbsWriteText(p, pLeft, SBS_TXTA);
    if( nLeft==nRight && zLeft[nLeft]==zRight[nRight] ){
      sbsWriteMarker(p, "   ", "");
    }else{
      sbsWriteMarker(p, " | ", "|");
    }
    sbsWriteLineno(p, lnRight, SBS_LNB);
    p->iStart = nPrefix;
    p->iEnd = nRight - nSuffix;
    p->zStart = zClassAdd;
    sbsWriteText(p, pRight, SBS_TXTB);
    return;
  }

  /* A single chunk of text deleted from the left */
  if( nPrefix+nSuffix==nRight ){
    /* Text deleted from the left */
    sbsWriteLineno(p, lnLeft, SBS_LNA);
    p->iStart2 = p->iEnd2 = 0;
    p->iStart = nPrefix;
    p->iEnd = nLeft - nSuffix;
    p->zStart = zClassRm;
    sbsWriteText(p, pLeft, SBS_TXTA);
    sbsWriteMarker(p, " | ", "|");
    sbsWriteLineno(p, lnRight, SBS_LNB);
    p->iStart = p->iEnd = -1;
    sbsWriteText(p, pRight, SBS_TXTB);
    return;
  }

  /* At this point we know that there is a chunk of text that has
  ** changed between the left and the right.  Check to see if there
  ** is a large unchanged section in the middle of that changed block.
  */
  nLeftDiff = nLeft - nSuffix - nPrefix;
  nRightDiff = nRight - nSuffix - nPrefix;
  if( p->escHtml
   && nLeftDiff >= 6
   && nRightDiff >= 6
   && textLCS(&zLeft[nPrefix], nLeftDiff, &zRight[nPrefix], nRightDiff, aLCS)
  ){
    sbsWriteLineno(p, lnLeft, SBS_LNA);
    p->iStart = nPrefix;
    p->iEnd = nPrefix + aLCS[0];
    if( aLCS[2]==0 ){
      sbsShiftLeft(p, pLeft->z);
      p->zStart = zClassRm;
    }else{
      p->zStart = zClassChng;
    }
    p->iStart2 = nPrefix + aLCS[1];
    p->iEnd2 = nLeft - nSuffix;
    p->zStart2 = aLCS[3]==nRightDiff ? zClassRm : zClassChng;
    sbsSimplifyLine(p, zLeft);
    sbsWriteText(p, pLeft, SBS_TXTA);
    sbsWriteMarker(p, " | ", "|");
    sbsWriteLineno(p, lnRight, SBS_LNB);
    p->iStart = nPrefix;
    p->iEnd = nPrefix + aLCS[2];
    if( aLCS[0]==0 ){
      sbsShiftLeft(p, pRight->z);
      p->zStart = zClassAdd;
    }else{
      p->zStart = zClassChng;
    }
    p->iStart2 = nPrefix + aLCS[3];
    p->iEnd2 = nRight - nSuffix;
    p->zStart2 = aLCS[1]==nLeftDiff ? zClassAdd : zClassChng;
    sbsSimplifyLine(p, zRight);
    sbsWriteText(p, pRight, SBS_TXTB);
    return;
  }

  /* If all else fails, show a single big change between left and right */
  sbsWriteLineno(p, lnLeft, SBS_LNA);
  p->iStart2 = p->iEnd2 = 0;
  p->iStart = nPrefix;
  p->iEnd = nLeft - nSuffix;
  p->zStart = zClassChng;
  sbsWriteText(p, pLeft, SBS_TXTA);
  sbsWriteMarker(p, " | ", "|");
  sbsWriteLineno(p, lnRight, SBS_LNB);
  p->iEnd = nRight - nSuffix;
  sbsWriteText(p, pRight, SBS_TXTB);
}

/*
** Minimum of two values
*/
static int minInt(int a, int b){ return a<b ? a : b; }

/*
** Return the number between 0 and 100 that is smaller the closer pA and
** pB match.  Return 0 for a perfect match.  Return 100 if pA and pB are
** completely different.
**
** The current algorithm is as follows:
**
** (1) Remove leading and trailing whitespace.
** (2) Truncate both strings to at most 250 characters
** (3) Find the length of the longest common subsequence
** (4) Longer common subsequences yield lower scores.
*/
static int match_dline(DLine *pA, DLine *pB){
  const char *zA;            /* Left string */
  const char *zB;            /* right string */
  int nA;                    /* Bytes in zA[] */
  int nB;                    /* Bytes in zB[] */
  int avg;                   /* Average length of A and B */
  int i, j, k;               /* Loop counters */
  int best = 0;              /* Longest match found so far */
  int score;                 /* Final score.  0..100 */
  unsigned char c;           /* Character being examined */
  unsigned char aFirst[256]; /* aFirst[X] = index in zB[] of first char X */
  unsigned char aNext[252];  /* aNext[i] = index in zB[] of next zB[i] char */

  zA = pA->z;
  zB = pB->z;
  nA = pA->n;
  nB = pB->n;
  while( nA>0 && fossil_isspace(zA[0]) ){ nA--; zA++; }
  while( nA>0 && fossil_isspace(zA[nA-1]) ){ nA--; }
  while( nB>0 && fossil_isspace(zB[0]) ){ nB--; zB++; }
  while( nB>0 && fossil_isspace(zB[nB-1]) ){ nB--; }
  if( nA>250 ) nA = 250;
  if( nB>250 ) nB = 250;
  avg = (nA+nB)/2;
  if( avg==0 ) return 0;
  if( nA==nB && memcmp(zA, zB, nA)==0 ) return 0;
  memset(aFirst, 0xff, sizeof(aFirst));
  zA--; zB--;   /* Make both zA[] and zB[] 1-indexed */
  for(i=nB; i>0; i--){
    c = (unsigned char)zB[i];
    aNext[i] = aFirst[c];
    aFirst[c] = i;
  }
  best = 0;
  for(i=1; i<=nA-best; i++){
    c = (unsigned char)zA[i];
    for(j=aFirst[c]; j<nB-best && memcmp(&zA[i],&zB[j],best)==0; j = aNext[j]){
      int limit = minInt(nA-i, nB-j);
      for(k=best; k<=limit && zA[k+i]==zB[k+j]; k++){}
      if( k>best ) best = k;
    }
  }
  score = (best>avg) ? 0 : (avg - best)*100/avg;

#if 0
  fprintf(stderr, "A: [%.*s]\nB: [%.*s]\nbest=%d avg=%d score=%d\n",
  nA, zA+1, nB, zB+1, best, avg, score);
#endif

  /* Return the result */
  return score;
}

/*
** There is a change block in which nLeft lines of text on the left are
** converted into nRight lines of text on the right.  This routine computes
** how the lines on the left line up with the lines on the right.
**
** The return value is a buffer of unsigned characters, obtained from
** fossil_malloc().  (The caller needs to free the return value using
** fossil_free().)  Entries in the returned array have values as follows:
**
**    1.  Delete the next line of pLeft.
**    2.  Insert the next line of pRight.
**    3.  The next line of pLeft changes into the next line of pRight.
**    4.  Delete one line from pLeft and add one line to pRight.
**
** Values larger than three indicate better matches.
**
** The length of the returned array will be just large enough to cause
** all elements of pLeft and pRight to be consumed.
**
** Algorithm:  Wagner's minimum edit-distance algorithm, modified by
** adding a cost to each match based on how well the two rows match
** each other.  Insertion and deletion costs are 50.  Match costs
** are between 0 and 100 where 0 is a perfect match 100 is a complete
** mismatch.
*/
static unsigned char *sbsAlignment(
   DLine *aLeft, int nLeft,       /* Text on the left */
   DLine *aRight, int nRight,     /* Text on the right */
   u64 diffFlags                  /* Flags passed into the original diff */
){
  int i, j, k;                 /* Loop counters */
  int *a;                      /* One row of the Wagner matrix */
  int *pToFree;                /* Space that needs to be freed */
  unsigned char *aM;           /* Wagner result matrix */
  int nMatch, iMatch;          /* Number of matching lines and match score */
  int mnLen;                   /* MIN(nLeft, nRight) */
  int mxLen;                   /* MAX(nLeft, nRight) */
  int aBuf[100];               /* Stack space for a[] if nRight not to big */

  aM = fossil_malloc( (nLeft+1)*(nRight+1) );
  if( nLeft==0 ){
    memset(aM, 2, nRight);
    return aM;
  }
  if( nRight==0 ){
    memset(aM, 1, nLeft);
    return aM;
  }

  /* This algorithm is O(N**2).  So if N is too big, bail out with a
  ** simple (but stupid and ugly) result that doesn't take too long. */
  mnLen = nLeft<nRight ? nLeft : nRight;
  if( nLeft*nRight>100000 && (diffFlags & DIFF_SLOW_SBS)==0 ){
    memset(aM, 4, mnLen);
    if( nLeft>mnLen )  memset(aM+mnLen, 1, nLeft-mnLen);
    if( nRight>mnLen ) memset(aM+mnLen, 2, nRight-mnLen);
    return aM;
  }

  if( nRight < count(aBuf)-1 ){
    pToFree = 0;
    a = aBuf;
  }else{
    a = pToFree = fossil_malloc( sizeof(a[0])*(nRight+1) );
  }

  /* Compute the best alignment */
  for(i=0; i<=nRight; i++){
    aM[i] = 2;
    a[i] = i*50;
  }
  aM[0] = 0;
  for(j=1; j<=nLeft; j++){
    int p = a[0];
    a[0] = p+50;
    aM[j*(nRight+1)] = 1;
    for(i=1; i<=nRight; i++){
      int m = a[i-1]+50;
      int d = 2;
      if( m>a[i]+50 ){
        m = a[i]+50;
        d = 1;
      }
      if( m>p ){
        int score = match_dline(&aLeft[j-1], &aRight[i-1]);
        if( (score<=63 || (i<j+1 && i>j-1)) && m>p+score ){
          m = p+score;
          d = 3 | score*4;
        }
      }
      p = a[i];
      a[i] = m;
      aM[j*(nRight+1)+i] = d;
    }
  }

  /* Compute the lowest-cost path back through the matrix */
  i = nRight;
  j = nLeft;
  k = (nRight+1)*(nLeft+1)-1;
  nMatch = iMatch = 0;
  while( i+j>0 ){
    unsigned char c = aM[k];
    if( c>=3 ){
      assert( i>0 && j>0 );
      i--;
      j--;
      nMatch++;
      iMatch += (c>>2);
      aM[k] = 3;
    }else if( c==2 ){
      assert( i>0 );
      i--;
    }else{
      assert( j>0 );
      j--;
    }
    k--;
    aM[k] = aM[j*(nRight+1)+i];
  }
  k++;
  i = (nRight+1)*(nLeft+1) - k;
  memmove(aM, &aM[k], i);

  /* If:
  **   (1) the alignment is more than 25% longer than the longest side, and
  **   (2) the average match cost exceeds 15
  ** Then this is probably an alignment that will be difficult for humans
  ** to read.  So instead, just show all of the right side inserted followed
  ** by all of the left side deleted.
  **
  ** The coefficients for conditions (1) and (2) above are determined by
  ** experimentation.
  */
  mxLen = nLeft>nRight ? nLeft : nRight;
  if( i*4>mxLen*5 && (nMatch==0 || iMatch/nMatch>15) ){
    memset(aM, 4, mnLen);
    if( nLeft>mnLen )  memset(aM+mnLen, 1, nLeft-mnLen);
    if( nRight>mnLen ) memset(aM+mnLen, 2, nRight-mnLen);
  }

  /* Return the result */
  fossil_free(pToFree);
  return aM;
}

/*
** R[] is an array of six integer, two COPY/DELETE/INSERT triples for a
** pair of adjacent differences.  Return true if the gap between these
** two differences is so small that they should be rendered as a single
** edit.
*/
static int smallGap(int *R){
  return R[3]<=2 || R[3]<=(R[1]+R[2]+R[4]+R[5])/8;
}

/*
** Given a diff context in which the aEdit[] array has been filled
** in, compute a side-by-side diff into pOut.
*/
static void sbsDiff(
  DContext *p,       /* The computed diff */
  Blob *pOut,        /* Write the results here */
  ReCompiled *pRe,   /* Only show changes that match this regex */
  u64 diffFlags      /* Flags controlling the diff */
){
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
  int m, ma, mb;/* Number of lines to output */
  int skip;     /* Number of lines to skip */
  static int nChunk = 0; /* Number of chunks of diff output seen so far */
  SbsLine s;    /* Output line buffer */
  int nContext; /* Lines of context above and below each change */
  int showDivider = 0;  /* True to show the divider */
  Blob aCols[5]; /* Array of column blobs */

  memset(&s, 0, sizeof(s));
  s.width = diff_width(diffFlags);
  nContext = diff_context_lines(diffFlags);
  s.escHtml = (diffFlags & DIFF_HTML)!=0;
  if( s.escHtml ){
    for(i=SBS_LNA; i<=SBS_TXTB; i++){
      blob_zero(&aCols[i]);
      s.apCols[i] = &aCols[i];
    }
  }else{
    for(i=SBS_LNA; i<=SBS_TXTB; i++){
      s.apCols[i] = pOut;
    }
  }
  s.pRe = pRe;
  s.iStart = -1;
  s.iStart2 = 0;
  s.iEnd = -1;
  A = p->aFrom;
  B = p->aTo;
  R = p->aEdit;
  mxr = p->nEdit;
  while( mxr>2 && R[mxr-1]==0 && R[mxr-2]==0 ){ mxr -= 3; }

  for(r=0; r<mxr; r += 3*nr){
    /* Figure out how many triples to show in a single block */
    for(nr=1; R[r+nr*3]>0 && R[r+nr*3]<nContext*2; nr++){}
    /* printf("r=%d nr=%d\n", r, nr); */

    /* If there is a regex, skip this block (generate no diff output)
    ** if the regex matches or does not match both insert and delete.
    ** Only display the block if one side matches but the other side does
    ** not.
    */
    if( pRe ){
      int hideBlock = 1;
      int xa = a, xb = b;
      for(i=0; hideBlock && i<nr; i++){
        int c1, c2;
        xa += R[r+i*3];
        xb += R[r+i*3];
        c1 = re_dline_match(pRe, &A[xa], R[r+i*3+1]);
        c2 = re_dline_match(pRe, &B[xb], R[r+i*3+2]);
        hideBlock = c1==c2;
        xa += R[r+i*3+1];
        xb += R[r+i*3+2];
      }
      if( hideBlock ){
        a = xa;
        b = xb;
        continue;
      }
    }

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

    /* Draw the separator between blocks */
    if( showDivider ){
      if( s.escHtml ){
        char zLn[10];
        sqlite3_snprintf(sizeof(zLn), zLn, "%d", a+skip+1);
        sbsWriteSep(&s, strlen(zLn), SBS_LNA);
        sbsWriteSep(&s, s.width, SBS_TXTA);
        sbsWriteSep(&s, 0, SBS_MKR);
        sqlite3_snprintf(sizeof(zLn), zLn, "%d", b+skip+1);
        sbsWriteSep(&s, strlen(zLn), SBS_LNB);
        sbsWriteSep(&s, s.width, SBS_TXTB);
      }else{
        blob_appendf(pOut, "%.*c\n", s.width*2+16, '.');
      }
    }
    showDivider = 1;
    nChunk++;
    if( s.escHtml ){
      blob_appendf(s.apCols[SBS_LNA], "<span id=\"chunk%d\"></span>", nChunk);
    }

    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    for(j=0; j<m; j++){
      sbsWriteLineno(&s, a+j, SBS_LNA);
      s.iStart = s.iEnd = -1;
      sbsWriteText(&s, &A[a+j], SBS_TXTA);
      sbsWriteMarker(&s, "   ", "");
      sbsWriteLineno(&s, b+j, SBS_LNB);
      sbsWriteText(&s, &B[b+j], SBS_TXTB);
    }
    a += m;
    b += m;

    /* Show the differences */
    for(i=0; i<nr; i++){
      unsigned char *alignment;
      ma = R[r+i*3+1];   /* Lines on left but not on right */
      mb = R[r+i*3+2];   /* Lines on right but not on left */

      /* If the gap between the current diff and then next diff within the
      ** same block is not too great, then render them as if they are a
      ** single diff. */
      while( i<nr-1 && smallGap(&R[r+i*3]) ){
        i++;
        m = R[r+i*3];
        ma += R[r+i*3+1] + m;
        mb += R[r+i*3+2] + m;
      }

      alignment = sbsAlignment(&A[a], ma, &B[b], mb, diffFlags);
      for(j=0; ma+mb>0; j++){
        if( alignment[j]==1 ){
          /* Delete one line from the left */
          sbsWriteLineno(&s, a, SBS_LNA);
          s.iStart = 0;
          s.zStart = "<span class=\"diffrm\">";
          s.iEnd = LENGTH(&A[a]);
          sbsWriteText(&s, &A[a], SBS_TXTA);
          sbsWriteMarker(&s, " <", "&lt;");
          sbsWriteNewlines(&s);
          assert( ma>0 );
          ma--;
          a++;
        }else if( alignment[j]==3 ){
          /* The left line is changed into the right line */
          sbsWriteLineChange(&s, &A[a], a, &B[b], b);
          assert( ma>0 && mb>0 );
          ma--;
          mb--;
          a++;
          b++;
        }else if( alignment[j]==2 ){
          /* Insert one line on the right */
          if( !s.escHtml ){
            sbsWriteSpace(&s, s.width + 7, SBS_TXTA);
          }
          sbsWriteMarker(&s, " > ", "&gt;");
          sbsWriteLineno(&s, b, SBS_LNB);
          s.iStart = 0;
          s.zStart = "<span class=\"diffadd\">";
          s.iEnd = LENGTH(&B[b]);
          sbsWriteText(&s, &B[b], SBS_TXTB);
          assert( mb>0 );
          mb--;
          b++;
        }else{
          /* Delete from the left and insert on the right */
          sbsWriteLineno(&s, a, SBS_LNA);
          s.iStart = 0;
          s.zStart = "<span class=\"diffrm\">";
          s.iEnd = LENGTH(&A[a]);
          sbsWriteText(&s, &A[a], SBS_TXTA);
          sbsWriteMarker(&s, " | ", "|");
          sbsWriteLineno(&s, b, SBS_LNB);
          s.iStart = 0;
          s.zStart = "<span class=\"diffadd\">";
          s.iEnd = LENGTH(&B[b]);
          sbsWriteText(&s, &B[b], SBS_TXTB);
          ma--;
          mb--;
          a++;
          b++;
        }
      }
      fossil_free(alignment);
      if( i<nr-1 ){
        m = R[r+i*3+3];
        for(j=0; j<m; j++){
          sbsWriteLineno(&s, a+j, SBS_LNA);
          s.iStart = s.iEnd = -1;
          sbsWriteText(&s, &A[a+j], SBS_TXTA);
          sbsWriteMarker(&s, "   ", "");
          sbsWriteLineno(&s, b+j, SBS_LNB);
          sbsWriteText(&s, &B[b+j], SBS_TXTB);
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
      sbsWriteLineno(&s, a+j, SBS_LNA);
      s.iStart = s.iEnd = -1;
      sbsWriteText(&s, &A[a+j], SBS_TXTA);
      sbsWriteMarker(&s, "   ", "");
      sbsWriteLineno(&s, b+j, SBS_LNB);
      sbsWriteText(&s, &B[b+j], SBS_TXTB);
    }
  }

  if( s.escHtml && blob_size(s.apCols[SBS_LNA])>0 ){
    blob_append(pOut, "<table class=\"sbsdiffcols\"><tr>\n", -1);
    for(i=SBS_LNA; i<=SBS_TXTB; i++){
      sbsWriteColumn(pOut, s.apCols[i], i);
      blob_reset(s.apCols[i]);
    }
    blob_append(pOut, "</tr></table>\n", -1);
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
      if( !p->same_fn(&p->aFrom[i], &p->aTo[j]) ) continue;
      if( mxLength && !p->same_fn(&p->aFrom[i+mxLength], &p->aTo[j+mxLength]) ){
        continue;
      }
      k = 1;
      while( i+k<iE1 && j+k<iE2 && p->same_fn(&p->aFrom[i+k],&p->aTo[j+k]) ){
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
  int i, j, k;               /* Loop counters */
  int n;                     /* Loop limit */
  DLine *pA, *pB;            /* Pointers to lines */
  int iSX, iSY, iEX, iEY;    /* Current match */
  int skew = 0;              /* How lopsided is the match */
  int dist = 0;              /* Distance of match from center */
  int mid;                   /* Center of the span */
  int iSXb, iSYb, iEXb, iEYb;   /* Best match so far */
  int iSXp, iSYp, iEXp, iEYp;   /* Previous match */
  sqlite3_int64 bestScore;      /* Best score so far */
  sqlite3_int64 score;          /* Score for current candidate LCS */
  int span;                     /* combined width of the input sequences */

  span = (iE1 - iS1) + (iE2 - iS2);
  bestScore = -10000;
  score = 0;
  iSXb = iSXp = iS1;
  iEXb = iEXp = iS1;
  iSYb = iSYp = iS2;
  iEYb = iEYp = iS2;
  mid = (iE1 + iS1)/2;
  for(i=iS1; i<iE1; i++){
    int limit = 0;
    j = p->aTo[p->aFrom[i].h % p->nTo].iHash;
    while( j>0
      && (j-1<iS2 || j>=iE2 || !p->same_fn(&p->aFrom[i], &p->aTo[j-1]))
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
    pA = &p->aFrom[iSX-1];
    pB = &p->aTo[iSY-1];
    n = minInt(iSX-iS1, iSY-iS2);
    for(k=0; k<n && p->same_fn(pA,pB); k++, pA--, pB--){}
    iSX -= k;
    iSY -= k;
    iEX = i+1;
    iEY = j;
    pA = &p->aFrom[iEX];
    pB = &p->aTo[iEY];
    n = minInt(iE1-iEX, iE2-iEY);
    for(k=0; k<n && p->same_fn(pA,pB); k++, pA++, pB++){}
    iEX += k;
    iEY += k;
    skew = (iSX-iS1) - (iSY-iS2);
    if( skew<0 ) skew = -skew;
    dist = (iSX+iEX)/2 - mid;
    if( dist<0 ) dist = -dist;
    score = (iEX - iSX)*(sqlite3_int64)span - (skew + dist);
    if( score>bestScore ){
      bestScore = score;
      iSXb = iSX;
      iSYb = iSY;
      iEXb = iEX;
      iEYb = iEY;
    }else if( iEX>iEXp ){
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
}

/*
** Expand the size of aEdit[] array to hold at least nEdit elements.
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
    /* A common segment has been found.
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
  while( iE1>0 && iE2>0 && p->same_fn(&p->aFrom[iE1-1], &p->aTo[iE2-1]) ){
    iE1--;
    iE2--;
  }
  mnE = iE1<iE2 ? iE1 : iE2;
  for(iS=0; iS<mnE && p->same_fn(&p->aFrom[iS],&p->aTo[iS]); iS++){}

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
** Attempt to shift insertion or deletion blocks so that they begin and
** end on lines that are pure whitespace.  In other words, try to transform
** this:
**
**      int func1(int x){
**         return x*10;
**     +}
**     +
**     +int func2(int x){
**     +   return x*20;
**      }
**
**      int func3(int x){
**         return x/5;
**      }
**
** Into one of these:
**
**      int func1(int x){              int func1(int x){
**         return x*10;                   return x*10;
**      }                              }
**     +
**     +int func2(int x){             +int func2(int x){
**     +   return x*20;               +   return x*20;
**     +}                             +}
**                                    +
**      int func3(int x){              int func3(int x){
**         return x/5;                    return x/5;
**      }                              }
*/
static void diff_optimize(DContext *p){
  int r;       /* Index of current triple */
  int lnFrom;  /* Line number in p->aFrom */
  int lnTo;    /* Line number in p->aTo */
  int cpy, del, ins;

  lnFrom = lnTo = 0;
  for(r=0; r<p->nEdit; r += 3){
    cpy = p->aEdit[r];
    del = p->aEdit[r+1];
    ins = p->aEdit[r+2];
    lnFrom += cpy;
    lnTo += cpy;

    /* Shift insertions toward the beginning of the file */
    while( cpy>0 && del==0 && ins>0 ){
      DLine *pTop = &p->aFrom[lnFrom-1];  /* Line before start of insert */
      DLine *pBtm = &p->aTo[lnTo+ins-1];  /* Last line inserted */
      if( p->same_fn(pTop, pBtm)==0 ) break;
      if( LENGTH(pTop+1)+LENGTH(pBtm)<=LENGTH(pTop)+LENGTH(pBtm-1) ) break;
      lnFrom--;
      lnTo--;
      p->aEdit[r]--;
      p->aEdit[r+3]++;
      cpy--;
    }

    /* Shift insertions toward the end of the file */
    while( r+3<p->nEdit && p->aEdit[r+3]>0 && del==0 && ins>0 ){
      DLine *pTop = &p->aTo[lnTo];       /* First line inserted */
      DLine *pBtm = &p->aTo[lnTo+ins];   /* First line past end of insert */
      if( p->same_fn(pTop, pBtm)==0 ) break;
      if( LENGTH(pTop)+LENGTH(pBtm-1)<=LENGTH(pTop+1)+LENGTH(pBtm) ) break;
      lnFrom++;
      lnTo++;
      p->aEdit[r]++;
      p->aEdit[r+3]--;
      cpy++;
    }

    /* Shift deletions toward the beginning of the file */
    while( cpy>0 && del>0 && ins==0 ){
      DLine *pTop = &p->aFrom[lnFrom-1];     /* Line before start of delete */
      DLine *pBtm = &p->aFrom[lnFrom+del-1]; /* Last line deleted */
      if( p->same_fn(pTop, pBtm)==0 ) break;
      if( LENGTH(pTop+1)+LENGTH(pBtm)<=LENGTH(pTop)+LENGTH(pBtm-1) ) break;
      lnFrom--;
      lnTo--;
      p->aEdit[r]--;
      p->aEdit[r+3]++;
      cpy--;
    }

    /* Shift deletions toward the end of the file */
    while( r+3<p->nEdit && p->aEdit[r+3]>0 && del>0 && ins==0 ){
      DLine *pTop = &p->aFrom[lnFrom];     /* First line deleted */
      DLine *pBtm = &p->aFrom[lnFrom+del]; /* First line past end of delete */
      if( p->same_fn(pTop, pBtm)==0 ) break;
      if( LENGTH(pTop)+LENGTH(pBtm-1)<=LENGTH(pTop)+LENGTH(pBtm) ) break;
      lnFrom++;
      lnTo++;
      p->aEdit[r]++;
      p->aEdit[r+3]--;
      cpy++;
    }

    lnFrom += del;
    lnTo += ins;
  }
}

/*
** Extract the number of lines of context from diffFlags.  Supply an
** appropriate default if no context width is specified.
*/
int diff_context_lines(u64 diffFlags){
  int n = diffFlags & DIFF_CONTEXT_MASK;
  if( n==0 && (diffFlags & DIFF_CONTEXT_EX)==0 ) n = 5;
  return n;
}

/*
** Extract the width of columns for side-by-side diff.  Supply an
** appropriate default if no width is given.
*/
int diff_width(u64 diffFlags){
  int w = (diffFlags & DIFF_WIDTH_MASK)/(DIFF_CONTEXT_MASK+1);
  if( w==0 ) w = 80;
  return w;
}

/*
** Append the error message to pOut.
*/
void diff_errmsg(Blob *pOut, const char *msg, int diffFlags){
  if( diffFlags & DIFF_HTML ){
    blob_appendf(pOut, "<p class=\"generalError\">%s</p>", msg);
  }else{
    blob_append(pOut, msg, -1);
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
  Blob *pOut,      /* Write diff here if not NULL */
  ReCompiled *pRe, /* Only output changes where this Regexp matches */
  u64 diffFlags    /* DIFF_* flags defined above */
){
  int ignoreWs; /* Ignore whitespace */
  DContext c;

  if( diffFlags & DIFF_INVERT ){
    Blob *pTemp = pA_Blob;
    pA_Blob = pB_Blob;
    pB_Blob = pTemp;
  }
  ignoreWs = (diffFlags & DIFF_IGNORE_ALLWS)!=0;
  blob_to_utf8_no_bom(pA_Blob, 0);
  blob_to_utf8_no_bom(pB_Blob, 0);

  /* Prepare the input files */
  memset(&c, 0, sizeof(c));
  if( (diffFlags & DIFF_IGNORE_ALLWS)==DIFF_IGNORE_ALLWS ){
    c.same_fn = same_dline_ignore_allws;
  }else{
    c.same_fn = same_dline;
  }
  c.aFrom = break_into_lines(blob_str(pA_Blob), blob_size(pA_Blob),
                             &c.nFrom, diffFlags);
  c.aTo = break_into_lines(blob_str(pB_Blob), blob_size(pB_Blob),
                           &c.nTo, diffFlags);
  if( c.aFrom==0 || c.aTo==0 ){
    fossil_free(c.aFrom);
    fossil_free(c.aTo);
    if( pOut ){
      diff_errmsg(pOut, DIFF_CANNOT_COMPUTE_BINARY, diffFlags);
    }
    return 0;
  }

  /* Compute the difference */
  diff_all(&c);
  if( ignoreWs && c.nEdit==6 && c.aEdit[1]==0 && c.aEdit[2]==0 ){
    fossil_free(c.aFrom);
    fossil_free(c.aTo);
    fossil_free(c.aEdit);
    if( pOut ) diff_errmsg(pOut, DIFF_WHITESPACE_ONLY, diffFlags);
    return 0;
  }
  if( (diffFlags & DIFF_NOTTOOBIG)!=0 ){
    int i, m, n;
    int *a = c.aEdit;
    int mx = c.nEdit;
    for(i=m=n=0; i<mx; i+=3){ m += a[i]; n += a[i+1]+a[i+2]; }
    if( n>10000 ){
      fossil_free(c.aFrom);
      fossil_free(c.aTo);
      fossil_free(c.aEdit);
      if( pOut ) diff_errmsg(pOut, DIFF_TOO_MANY_CHANGES, diffFlags);
      return 0;
    }
  }
  if( (diffFlags & DIFF_NOOPT)==0 ){
    diff_optimize(&c);
  }

  if( pOut ){
    if( diffFlags & DIFF_NUMSTAT ){
      int nDel = 0, nIns = 0, i;
      for(i=0; c.aEdit[i] || c.aEdit[i+1] || c.aEdit[i+2]; i+=3){
        nDel += c.aEdit[i+1];
        nIns += c.aEdit[i+2];
      }
      blob_appendf(pOut, "%10d %10d", nIns, nDel);
    }else if( diffFlags & DIFF_SIDEBYSIDE ){
      sbsDiff(&c, pOut, pRe, diffFlags);
    }else{
      contextDiff(&c, pOut, pRe, diffFlags);
    }
    fossil_free(c.aFrom);
    fossil_free(c.aTo);
    fossil_free(c.aEdit);
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
** Process diff-related command-line options and return an appropriate
** "diffFlags" integer.
**
**   --brief                    Show filenames only    DIFF_BRIEF
**   -c|--context N             N lines of context.    DIFF_CONTEXT_MASK
**   --html                     Format for HTML        DIFF_HTML
**   --invert                   Invert the diff        DIFF_INVERT
**   -n|--linenum               Show line numbers      DIFF_LINENO
**   --noopt                    Disable optimization   DIFF_NOOPT
**   --numstat                  Show change counts     DIFF_NUMSTAT
**   --strip-trailing-cr        Strip trailing CR      DIFF_STRIP_EOLCR
**   --unified                  Unified diff.          ~DIFF_SIDEBYSIDE
**   -w|--ignore-all-space      Ignore all whitespaces DIFF_IGNORE_ALLWS
**   -W|--width N               N character lines.     DIFF_WIDTH_MASK
**   -y|--side-by-side          Side-by-side diff.     DIFF_SIDEBYSIDE
**   -Z|--ignore-trailing-space Ignore eol-whitespaces DIFF_IGNORE_EOLWS
*/
u64 diff_options(void){
  u64 diffFlags = 0;
  const char *z;
  int f;
  if( find_option("ignore-trailing-space","Z",0)!=0 ){
    diffFlags = DIFF_IGNORE_EOLWS;
  }
  if( find_option("ignore-all-space","w",0)!=0 ){
    diffFlags = DIFF_IGNORE_ALLWS; /* stronger than DIFF_IGNORE_EOLWS */
  }
  if( find_option("strip-trailing-cr",0,0)!=0 ){
    diffFlags |= DIFF_STRIP_EOLCR;
  }
  if( find_option("side-by-side","y",0)!=0 ) diffFlags |= DIFF_SIDEBYSIDE;
  if( find_option("yy",0,0)!=0 ){
    diffFlags |= DIFF_SIDEBYSIDE | DIFF_SLOW_SBS;
  }
  if( find_option("unified",0,0)!=0 ) diffFlags &= ~DIFF_SIDEBYSIDE;
  if( (z = find_option("context","c",1))!=0 && (f = atoi(z))>=0 ){
    if( f > DIFF_CONTEXT_MASK ) f = DIFF_CONTEXT_MASK;
    diffFlags |= f + DIFF_CONTEXT_EX;
  }
  if( (z = find_option("width","W",1))!=0 && (f = atoi(z))>0 ){
    f *= DIFF_CONTEXT_MASK+1;
    if( f > DIFF_WIDTH_MASK ) f = DIFF_CONTEXT_MASK;
    diffFlags |= f;
  }
  if( find_option("html",0,0)!=0 ) diffFlags |= DIFF_HTML;
  if( find_option("linenum","n",0)!=0 ) diffFlags |= DIFF_LINENO;
  if( find_option("noopt",0,0)!=0 ) diffFlags |= DIFF_NOOPT;
  if( find_option("numstat",0,0)!=0 ) diffFlags |= DIFF_NUMSTAT;
  if( find_option("invert",0,0)!=0 ) diffFlags |= DIFF_INVERT;
  if( find_option("brief",0,0)!=0 ) diffFlags |= DIFF_BRIEF;
  return diffFlags;
}

/*
** COMMAND: test-rawdiff
**
** Usage: %fossil test-rawdiff FILE1 FILE2
**
** Show a minimal sequence of Copy/Delete/Insert operations needed to convert
** FILE1 into FILE2.  This command is intended for use in testing and debugging
** the built-in difference engine of Fossil.
*/
void test_rawdiff_cmd(void){
  Blob a, b;
  int r;
  int i;
  int *R;
  u64 diffFlags = diff_options();
  if( g.argc<4 ) usage("FILE1 FILE2 ...");
  blob_read_from_file(&a, g.argv[2]);
  for(i=3; i<g.argc; i++){
    if( i>3 ) fossil_print("-------------------------------\n");
    blob_read_from_file(&b, g.argv[i]);
    R = text_diff(&a, &b, 0, 0, diffFlags);
    for(r=0; R[r] || R[r+1] || R[r+2]; r += 3){
      fossil_print(" copy %4d  delete %4d  insert %4d\n", R[r], R[r+1], R[r+2]);
    }
    /* free(R); */
    blob_reset(&b);
  }
}

/*
** COMMAND: test-diff
**
** Usage: %fossil [options] FILE1 FILE2
**
** Print the difference between two files.  The usual diff options apply.
*/
void test_diff_cmd(void){
  Blob a, b, out;
  u64 diffFlag;
  const char *zRe;           /* Regex filter for diff output */
  ReCompiled *pRe = 0;       /* Regex filter for diff output */

  if( find_option("tk",0,0)!=0 ){
    diff_tk("test-diff", 2);
    return;
  }
  find_option("i",0,0);
  find_option("v",0,0);
  zRe = find_option("regexp","e",1);
  if( zRe ){
    const char *zErr = re_compile(&pRe, zRe, 0);
    if( zErr ) fossil_fatal("regex error: %s", zErr);
  }
  diffFlag = diff_options();
  verify_all_options();
  if( g.argc!=4 ) usage("FILE1 FILE2");
  diff_print_filenames(g.argv[2], g.argv[3], diffFlag);
  blob_read_from_file(&a, g.argv[2]);
  blob_read_from_file(&b, g.argv[3]);
  blob_zero(&out);
  text_diff(&a, &b, &out, pRe, diffFlag);
  blob_write_to_file(&out, "-");
  re_free(pRe);
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
    short int n;         /* Number of bytes (omitting trailing \n) */
    short int iVers;     /* Level at which tag was set */
  } *aOrig;
  int nOrig;        /* Number of elements in aOrig[] */
  int nVers;        /* Number of versions analyzed */
  int bMoreToDo;    /* True if the limit was reached */
  int origId;       /* RID for the zOrigin version */
  int showId;       /* RID for the version being analyzed */
  struct AnnVers {
    const char *zFUuid;   /* File being analyzed */
    const char *zMUuid;   /* Check-in containing the file */
    const char *zDate;    /* Date of the check-in */
    const char *zBgColor; /* Suggested background color */
    const char *zUser;    /* Name of user who did the check-in */
    unsigned cnt;         /* Number of lines contributed by this check-in */
  } *aVers;         /* For each check-in analyzed */
  char **azVers;    /* Names of versions analyzed */
};

/*
** Initialize the annotation process by specifying the file that is
** to be annotated.  The annotator takes control of the input Blob and
** will release it when it is finished with it.
*/
static int annotation_start(Annotator *p, Blob *pInput, u64 diffFlags){
  int i;

  memset(p, 0, sizeof(*p));
  if( (diffFlags & DIFF_IGNORE_ALLWS)==DIFF_IGNORE_ALLWS ){
    p->c.same_fn = same_dline_ignore_allws;
  }else{
    p->c.same_fn = same_dline;
  }
  p->c.aTo = break_into_lines(blob_str(pInput), blob_size(pInput),&p->c.nTo,
                              diffFlags);
  if( p->c.aTo==0 ){
    return 1;
  }
  p->aOrig = fossil_malloc( sizeof(p->aOrig[0])*p->c.nTo );
  for(i=0; i<p->c.nTo; i++){
    p->aOrig[i].z = p->c.aTo[i].z;
    p->aOrig[i].n = p->c.aTo[i].n;
    p->aOrig[i].iVers = -1;
  }
  p->nOrig = p->c.nTo;
  return 0;
}

/*
** The input pParent is the next most recent ancestor of the file
** being annotated.  Do another step of the annotation.  Return true
** if additional annotation is required.
*/
static int annotation_step(
  Annotator *p,
  Blob *pParent,
  int iVers,
  u64 diffFlags
){
  int i, j;
  int lnTo;

  /* Prepare the parent file to be diffed */
  p->c.aFrom = break_into_lines(blob_str(pParent), blob_size(pParent),
                                &p->c.nFrom, diffFlags);
  if( p->c.aFrom==0 ){
    return 1;
  }

  /* Compute the differences going from pParent to the file being
  ** annotated. */
  diff_all(&p->c);

  /* Where new lines are inserted on this difference, record the
  ** iVers as the source of the new line.
  */
  for(i=lnTo=0; i<p->c.nEdit; i+=3){
    int nCopy = p->c.aEdit[i];
    int nIns = p->c.aEdit[i+2];
    lnTo += nCopy;
    for(j=0; j<nIns; j++, lnTo++){
      if( p->aOrig[lnTo].iVers<0 ){
        p->aOrig[lnTo].iVers = iVers;
      }
    }
  }

  /* Clear out the diff results */
  fossil_free(p->c.aEdit);
  p->c.aEdit = 0;
  p->c.nEdit = 0;
  p->c.nEditAlloc = 0;

  /* Clear out the from file */
  free(p->c.aFrom);

  /* Return no errors */
  return 0;
}

/* Return the current time as milliseconds since the Julian epoch */
static sqlite3_int64 current_time_in_milliseconds(void){
  static sqlite3_vfs *clockVfs = 0;
  sqlite3_int64 t;
  if( clockVfs==0 ) clockVfs = sqlite3_vfs_find(0);
  if( clockVfs->iVersion>=2 && clockVfs->xCurrentTimeInt64!=0 ){
    clockVfs->xCurrentTimeInt64(clockVfs, &t);
  }else{
    double r;
    clockVfs->xCurrentTime(clockVfs, &r);
    t = (sqlite3_int64)(r*86400000.0);
  }
  return t;
}

/*
** Compute a complete annotation on a file.  The file is identified by its
** filename and check-in name (NULL for current check-in).
*/
static void annotate_file(
  Annotator *p,          /* The annotator */
  const char *zFilename, /* The name of the file to be annotated */
  const char *zRevision, /* Use the version of the file in this check-in */
  const char *zLimit,    /* Limit the number of versions analyzed */
  const char *zOrigin,   /* The origin check-in, or NULL for root-of-tree */
  u64 annFlags           /* Flags to alter the annotation */
){
  Blob toAnnotate;       /* Text of the final (mid) version of the file */
  Blob step;             /* Text of previous revision */
  Blob treename;         /* FILENAME translated to canonical form */
  int cid;               /* Selected check-in ID */
  int origid = 0;        /* The origin ID or zero */
  int rid;               /* Artifact ID of the file being annotated */
  int fnid;              /* Filename ID */
  Stmt q;                /* Query returning all ancestor versions */
  int cnt = 0;           /* Number of versions analyzed */
  int iLimit;            /* Maximum number of versions to analyze */
  sqlite3_int64 mxTime;  /* Halt at this time if not already complete */

  if( zLimit ){
    if( strcmp(zLimit,"none")==0 ){
      iLimit = 0;
      mxTime = 0;
    }else if( sqlite3_strglob("*[0-9]s", zLimit)==0 ){
      iLimit = 0;
      mxTime = current_time_in_milliseconds() + 1000.0*atof(zLimit);
    }else{
      iLimit = atoi(zLimit);
      if( iLimit<=0 ) iLimit = 30;
      mxTime = 0;
    }
  }else{
    /* Default limit is as much as we can do in 1.000 seconds */
    iLimit = 0;
    mxTime = current_time_in_milliseconds()+1000;
  }
  db_begin_transaction();

  /* Get the artificate ID for the check-in begin analyzed */
  if( zRevision ){
    cid = name_to_typed_rid(zRevision, "ci");
  }else{
    db_must_be_within_tree();
    cid = db_lget_int("checkout", 0);
  }
  origid = zOrigin ? name_to_typed_rid(zOrigin, "ci") : 0;

  /* Compute all direct ancestors of the check-in being analyzed into
  ** the "ancestor" table. */
  if( origid ){
    path_shortest_stored_in_ancestor_table(origid, cid);
  }else{
    compute_direct_ancestors(cid);
  }

  /* Get filename ID */
  file_tree_name(zFilename, &treename, 0, 1);
  zFilename = blob_str(&treename);
  fnid = db_int(0, "SELECT fnid FROM filename WHERE name=%Q", zFilename);

  db_prepare(&q,
    "SELECT DISTINCT"
    "   (SELECT uuid FROM blob WHERE rid=mlink.fid),"
    "   (SELECT uuid FROM blob WHERE rid=mlink.mid),"
    "   date(event.mtime),"
    "   coalesce(event.euser,event.user),"
    "   mlink.fid"
    "  FROM mlink, event, ancestor"
    " WHERE mlink.fnid=%d"
    "   AND ancestor.rid=mlink.mid"
    "   AND event.objid=mlink.mid"
    "   AND mlink.mid!=mlink.pid"
    " ORDER BY ancestor.generation;",
    fnid
  );

  while( db_step(&q)==SQLITE_ROW ){
    if( cnt>=3 ){  /* Process at least 3 rows before imposing limits */
      if( (iLimit>0 && cnt>=iLimit)
       || (cnt>0 && mxTime>0 && current_time_in_milliseconds()>mxTime)
      ){
        p->bMoreToDo = 1;
        break;
      }
    }
    rid = db_column_int(&q, 4);
    if( cnt==0 ){
      if( !content_get(rid, &toAnnotate) ){
        fossil_fatal("unable to retrieve content of artifact #%d", rid);
      }
      blob_to_utf8_no_bom(&toAnnotate, 0);
      annotation_start(p, &toAnnotate, annFlags);
      p->bMoreToDo = origid!=0;
      p->origId = origid;
      p->showId = cid;
    }
    p->aVers = fossil_realloc(p->aVers, (p->nVers+1)*sizeof(p->aVers[0]));
    p->aVers[p->nVers].zFUuid = fossil_strdup(db_column_text(&q, 0));
    p->aVers[p->nVers].zMUuid = fossil_strdup(db_column_text(&q, 1));
    p->aVers[p->nVers].zDate = fossil_strdup(db_column_text(&q, 2));
    p->aVers[p->nVers].zUser = fossil_strdup(db_column_text(&q, 3));
    if( cnt>0 ){
      content_get(rid, &step);
      blob_to_utf8_no_bom(&step, 0);
      annotation_step(p, &step, p->nVers-1, annFlags);
      blob_reset(&step);
    }
    p->nVers++;
    cnt++;
  }
  db_finalize(&q);
  db_end_transaction(0);
}

/*
** Return a color from a gradient.
*/
unsigned gradient_color(unsigned c1, unsigned c2, int n, int i){
  unsigned c;   /* Result color */
  unsigned x1, x2;
  if( i==0 || n==0 ) return c1;
  else if(i>=n) return c2;
  x1 = (c1>>16)&0xff;
  x2 = (c2>>16)&0xff;
  c = (x1*(n-i) + x2*i)/n<<16 & 0xff0000;
  x1 = (c1>>8)&0xff;
  x2 = (c2>>8)&0xff;
  c |= (x1*(n-i) + x2*i)/n<<8 & 0xff00;
  x1 = c1&0xff;
  x2 = c2&0xff;
  c |= (x1*(n-i) + x2*i)/n & 0xff;
  return c;
}

/*
** WEBPAGE: annotate
** WEBPAGE: blame
** WEBPAGE: praise
**
** URL: /annotate?checkin=ID&filename=FILENAME
** URL: /blame?checkin=ID&filename=FILENAME
** URL: /praise?checkin=ID&filename=FILENAME
**
** Show the most recent change to each line of a text file.  /annotate shows
** the date of the changes and the check-in hash (with a link to the
** check-in).  /blame and /praise also show the user who made the check-in.
**
** Reverse Annotations:  Normally, these web pages look at versions of
** FILENAME moving backwards in time back toward the root check-in.  However,
** if the origin= query parameter is used to specify some future check-in
** (example: "origin=trunk") then these pages show changes moving towards
** that alternative origin.  Thus using "origin=trunk" on an historical
** version of the file shows the first time each line in the file was been
** changed in subsequent check-ins.
**
** Query parameters:
**
**    checkin=ID          The check-in at which to start the annotation
**    filename=FILENAME   The filename.
**    filevers=BOOLEAN    Show file versions rather than check-in versions
**    limit=LIMIT         Limit the amount of analysis:
**                           "none"  No limit
**                           "Xs"    As much as can be computed in X seconds
**                           "N"     N versions
**    log=BOOLEAN         Show a log of versions analyzed
**    origin=ID           The origin checkin.  If unspecified, the root
**                           check-in over the entire repository is used.
**                           Specify "origin=trunk" or similar for a reverse
**                           annotation
**    w=BOOLEAN           Ignore whitespace
**
*/
void annotation_page(void){
  int i;
  const char *zLimit;    /* Depth limit */
  u64 annFlags = DIFF_STRIP_EOLCR;
  int showLog;           /* True to display the log */
  int fileVers;          /* Show file version instead of check-in versions */
  int ignoreWs;          /* Ignore whitespace */
  const char *zFilename; /* Name of file to annotate */
  const char *zRevision; /* Name of check-in from which to start annotation */
  const char *zCI;       /* The check-in containing zFilename */
  const char *zOrigin;   /* The origin of the analysis */
  int szHash;            /* Number of characters in %S display */
  char *zLink;
  Annotator ann;
  HQuery url;
  struct AnnVers *p;
  unsigned clr1, clr2, clr;
  int bBlame = g.zPath[0]!='a';/* True for BLAME output.  False for ANNOTATE. */

  /* Gather query parameters */
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( exclude_spiders() ) return;
  load_control();
  zFilename = P("filename");
  zRevision = PD("checkin",0);
  zOrigin = P("origin");
  zLimit = P("limit");
  showLog = PB("log");
  fileVers = PB("filevers");
  ignoreWs = PB("w");
  if( ignoreWs ) annFlags |= DIFF_IGNORE_ALLWS;

  /* compute the annotation */
  annotate_file(&ann, zFilename, zRevision, zLimit, zOrigin, annFlags);
  zCI = ann.aVers[0].zMUuid;

  /* generate the web page */
  style_header("Annotation For %h", zFilename);
  if( bBlame ){
    url_initialize(&url, "blame");
  }else{
    url_initialize(&url, "annotate");
  }
  url_add_parameter(&url, "checkin", P("checkin"));
  url_add_parameter(&url, "filename", zFilename);
  if( zLimit ){
    url_add_parameter(&url, "limit", zLimit);
  }
  url_add_parameter(&url, "w", ignoreWs ? "1" : "0");
  url_add_parameter(&url, "log", showLog ? "1" : "0");
  url_add_parameter(&url, "filevers", fileVers ? "1" : "0");
  style_submenu_checkbox("w", "Ignore Whitespace", 0, 0);
  style_submenu_checkbox("log", "Log", 0, "toggle_annotation_log()");
  style_submenu_checkbox("filevers", "Link to Files", 0, 0);
  if( ann.bMoreToDo ){
    style_submenu_element("All Ancestors", "%s",
       url_render(&url, "limit", "none", 0, 0));
  }
  if( skin_detail_boolean("white-foreground") ){
    clr1 = 0xa04040;
    clr2 = 0x4059a0;
  }else{
    clr1 = 0xffb5b5;  /* Recent changes: red (hot) */
    clr2 = 0xb5e0ff;  /* Older changes: blue (cold) */
  }
  for(p=ann.aVers, i=0; i<ann.nVers; i++, p++){
    clr = gradient_color(clr1, clr2, ann.nVers-1, i);
    ann.aVers[i].zBgColor = mprintf("#%06x", clr);
  }

  @ <div id="annotation_log" style='display:%s(showLog?"block":"none");'>
  if( zOrigin ){
    zLink = href("%R/finfo?name=%t&ci=%!S&orig=%!S",zFilename,zCI,zOrigin);
  }else{
    zLink = href("%R/finfo?name=%t&ci=%!S",zFilename,zCI);
  }
  @ <h2>Versions of %z(zLink)%h(zFilename)</a> analyzed:</h2>
  @ <ol>
  for(p=ann.aVers, i=0; i<ann.nVers; i++, p++){
    @ <li><span style='background-color:%s(p->zBgColor);'>%s(p->zDate)
    @ check-in %z(href("%R/info/%!S",p->zMUuid))%S(p->zMUuid)</a>
    @ artifact %z(href("%R/artifact/%!S",p->zFUuid))%S(p->zFUuid)</a>
    @ </span>
  }
  @ </ol>
  @ <hr />
  @ </div>
  @ <script>
  @ function toggle_annotation_log(){
  @   var w = gebi("annotation_log");
  @   var x = document.forms["f01"].elements["log"].checked
  @   w.style.display = x ? "block" : "none";
  @ }
  @ </script>

  if( !ann.bMoreToDo ){
    assert( ann.origId==0 );  /* bMoreToDo always set for a point-to-point */
    @ <h2>Origin for each line in
    @ %z(href("%R/finfo?name=%h&ci=%!S", zFilename, zCI))%h(zFilename)</a>
    @ from check-in %z(href("%R/info/%!S",zCI))%S(zCI)</a>:</h2>
  }else if( ann.origId>0 ){
    @ <h2>Lines of
    @ %z(href("%R/finfo?name=%h&ci=%!S", zFilename, zCI))%h(zFilename)</a>
    @ from check-in %z(href("%R/info/%!S",zCI))%S(zCI)</a>
    @ that are changed by the sequence of edits moving toward
    @ check-in %z(href("%R/info/%!S",zOrigin))%S(zOrigin)</a>:</h2>
  }else{
    @ <h2>Lines added by the %d(ann.nVers) most recent ancestors of
    @ %z(href("%R/finfo?name=%h&ci=%!S", zFilename, zCI))%h(zFilename)</a>
    @ from check-in %z(href("%R/info/%!S",zCI))%S(zCI)</a>:</h2>
  }
  @ <pre>
  szHash = 10;
  for(i=0; i<ann.nOrig; i++){
    int iVers = ann.aOrig[i].iVers;
    char *z = (char*)ann.aOrig[i].z;
    int n = ann.aOrig[i].n;
    char zPrefix[300];
    z[n] = 0;
    if( iVers<0 && !ann.bMoreToDo ) iVers = ann.nVers-1;

    if( bBlame ){
      if( iVers>=0 ){
        struct AnnVers *p = ann.aVers+iVers;
        const char *zUuid = fileVers ? p->zFUuid : p->zMUuid;
        char *zLink = xhref("target='infowindow'", "%R/info/%!S", zUuid);
        sqlite3_snprintf(sizeof(zPrefix), zPrefix,
             "<span style='background-color:%s'>"
             "%s%.10s</a> %s</span> %13.13s:",
             p->zBgColor, zLink, zUuid, p->zDate, p->zUser);
        fossil_free(zLink);
      }else{
        sqlite3_snprintf(sizeof(zPrefix), zPrefix, "%*s", szHash+26, "");
      }
    }else{
      if( iVers>=0 ){
        struct AnnVers *p = ann.aVers+iVers;
        const char *zUuid = fileVers ? p->zFUuid : p->zMUuid;
        char *zLink = xhref("target='infowindow'", "%R/info/%!S", zUuid);
        sqlite3_snprintf(sizeof(zPrefix), zPrefix,
             "<span style='background-color:%s'>"
             "%s%.10s</a> %s</span> %4d:",
             p->zBgColor, zLink, zUuid, p->zDate, i+1);
        fossil_free(zLink);
      }else{
        sqlite3_snprintf(sizeof(zPrefix), zPrefix, "%*s%4d:",szHash+12,"",i+1);
      }
    }
    @ %s(zPrefix) %h(z)

  }
  @ </pre>
  style_footer();
}

/*
** COMMAND: annotate
** COMMAND: blame
** COMMAND: praise
**
** Usage: %fossil annotate|blame|praise ?OPTIONS? FILENAME
**
** Output the text of a file with markings to show when each line of the file
** was last modified.  The version currently checked out is shown by default.
** Other versions may be specified using the -r option.  The "annotate" command
** shows line numbers and omits the username.  The "blame" and "praise" commands
** show the user who made each check-in.
**
** Reverse Annotations:  Normally, these commands look at versions of
** FILENAME moving backwards in time back toward the root check-in, and
** thus the output shows the most recent change to each line.  However,
** if the -o|--origin option is used to specify some future check-in
** (example: "-o trunk") then these commands show changes moving towards
** that alternative origin.  Thus using "-o trunk" on an historical version
** of the file shows the first time each line in the file was been changed
** by subsequent check-ins.
**
** Options:
**   --filevers                  Show file version numbers rather than
**                               check-in versions
**   -r|--revision VERSION       The specific check-in containing the file
**   -l|--log                    List all versions analyzed
**   -n|--limit LIMIT            Limit the amount of analysis:
**                                 N      Up to N versions
**                                 Xs     As much as possible in X seconds
**                                 none   No limit
**   -o|--origin VERSION         The origin check-in. By default this is the
**                                 root of the repository. Set to "trunk" or
**                                 similar for a reverse annotation.
**   -w|--ignore-all-space       Ignore white space when comparing lines
**   -Z|--ignore-trailing-space  Ignore whitespace at line end
**
** See also: info, finfo, timeline
*/
void annotate_cmd(void){
  const char *zRevision; /* Revision name, or NULL for current check-in */
  Annotator ann;         /* The annotation of the file */
  int i;                 /* Loop counter */
  const char *zLimit;    /* The value to the -n|--limit option */
  const char *zOrig;     /* The value for -o|--origin */
  int showLog;           /* True to show the log */
  int fileVers;          /* Show file version instead of check-in versions */
  u64 annFlags = 0;      /* Flags to control annotation properties */
  int bBlame = 0;        /* True for BLAME output.  False for ANNOTATE. */
  int szHash;            /* Display size of a version hash */

  bBlame = g.argv[1][0]!='a';
  zRevision = find_option("r","revision",1);
  zLimit = find_option("limit","n",1);
  zOrig = find_option("origin","o",1);
  showLog = find_option("log","l",0)!=0;
  if( find_option("ignore-trailing-space","Z",0)!=0 ){
    annFlags = DIFF_IGNORE_EOLWS;
  }
  if( find_option("ignore-all-space","w",0)!=0 ){
    annFlags = DIFF_IGNORE_ALLWS; /* stronger than DIFF_IGNORE_EOLWS */
  }
  fileVers = find_option("filevers",0,0)!=0;
  db_must_be_within_tree();

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc<3 ) {
    usage("FILENAME");
  }

  annFlags |= DIFF_STRIP_EOLCR;
  annotate_file(&ann, g.argv[2], zRevision, zLimit, zOrig, annFlags);
  if( showLog ){
    struct AnnVers *p;
    for(p=ann.aVers, i=0; i<ann.nVers; i++, p++){
      fossil_print("version %3d: %s %S file %S\n",
                   i+1, p->zDate, p->zMUuid, p->zFUuid);
    }
    fossil_print("---------------------------------------------------\n");
  }
  szHash = length_of_S_display();
  for(i=0; i<ann.nOrig; i++){
    int iVers = ann.aOrig[i].iVers;
    char *z = (char*)ann.aOrig[i].z;
    int n = ann.aOrig[i].n;
    struct AnnVers *p;
    if( iVers<0 && !ann.bMoreToDo ) iVers = ann.nVers-1;
    if( bBlame ){
      if( iVers>=0 ){
        p = ann.aVers + iVers;
        fossil_print("%S %s %13.13s: %.*s\n",
             fileVers ? p->zFUuid : p->zMUuid, p->zDate, p->zUser, n, z);
      }else{
        fossil_print("%*s %.*s\n", szHash+26, "", n, z);
      }
    }else{
      if( iVers>=0 ){
        p = ann.aVers + iVers;
        fossil_print("%S %s %5d: %.*s\n",
             fileVers ? p->zFUuid : p->zMUuid, p->zDate, i+1, n, z);
      }else{
        fossil_print("%*s %5d: %.*s\n", szHash+11, "", i+1, n, z);
      }
    }
  }
}
