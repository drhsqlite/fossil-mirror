/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
#define DIFF_WEBPAGE   (((u64)0x00040)<<32) /* Complete webpage */
#define DIFF_BROWSER   (((u64)0x00080)<<32) /* The --browser option */
#define DIFF_JSON      (((u64)0x00100)<<32) /* JSON output */
#define DIFF_DEBUG     (((u64)0x00200)<<32) /* Debugging diff output */
#define DIFF_RAW       (((u64)0x00400)<<32) /* Raw triples - for debugging */
#define DIFF_TCL       (((u64)0x00800)<<32) /* For the --tk option */

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
** Maximum length of a line in a text file, in bytes.  (2**15 = 32768 bytes)
*/
#define LENGTH_MASK_SZ  15
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
  const char *z;          /* The text of the line */
  u64 h;                  /* Hash of the line */
  unsigned short indent;  /* Indent of the line. Only !=0 with -w/-Z option */
  unsigned short n;       /* number of bytes */
  unsigned int iNext;     /* 1+(Index of next line with same the same hash) */

  /* an array of DLine elements serves two purposes.  The fields
  ** above are one per line of input text.  But each entry is also
  ** a bucket in a hash table, as follows: */
  unsigned int iHash;     /* 1+(first entry in the hash chain) */
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
  int (*xDiffer)(const DLine*,const DLine*); /* comparison function */
};

/* <span> text for change coloration
*/
static const char zClassRm[]   = "<span class=\"diffrm\">";
static const char zClassAdd[]  = "<span class=\"diffadd\">";
static const char zClassChng[] = "<span class=\"diffchng\">";

/*
** Count the number of lines in the input string.  Include the last line
** in the count even if it lacks the \n terminator.  If an empty string
** is specified, the number of lines is zero.  For the purposes of this
** function, a string is considered empty if it contains no characters
** -OR- it contains only NUL characters.
**
** Returns true if the input seems to be plain text input, else false.
** If it returns false, pnLine is not modified, else it is set to the
** number of lines in z.
*/
int count_lines(
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
  u64 h, h2;
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
          h = (h^c)*9000000000000000041LL;
        }
      }
      k -= numws;
    }else{
      int k2 = k & ~0x7;
      u64 m;
      for(h=0, x=s; x<k2; x += 8){
        memcpy(&m, z+x, 8);
        h = (h^m)*9000000000000000041LL;
      }
      m = 0;
      memcpy(&m, z+x, k-k2);
      h ^= m;
    }
    a[i].indent = s;
    a[i].h = h = ((h%281474976710597LL)<<LENGTH_MASK_SZ) | (k-s);
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
** Return zero if two DLine elements are identical.
*/
static int same_dline(const DLine *pA, const DLine *pB){
  if( pA->h!=pB->h ) return 1;
  return memcmp(pA->z,pB->z, pA->h&LENGTH_MASK);
}

/*
** Return zero if two DLine elements are identical, ignoring
** all whitespace. The indent field of pA/pB already points
** to the first non-space character in the string.
*/

static int same_dline_ignore_allws(const DLine *pA, const DLine *pB){
  int a = pA->indent, b = pB->indent;
  if( pA->h==pB->h ){
    while( a<pA->n || b<pB->n ){
      if( a<pA->n && b<pB->n && pA->z[a++] != pB->z[b++] ) return 1;
      while( a<pA->n && fossil_isspace(pA->z[a])) ++a;
      while( b<pB->n && fossil_isspace(pB->z[b])) ++b;
    }
    return pA->n-a != pB->n-b;
  }
  return 1;
}

/*
** Return true if the regular expression *pRe matches any of the
** N dlines
*/
static int re_dline_match(
  ReCompiled *pRe,      /* The regular expression to be matched */
  const DLine *aDLine,  /* First of N DLines to compare against */
  int N                 /* Number of DLines to check */
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
      blob_append(pOut, zClassAdd, -1);
    }else if( cPrefix=='-' ){
      blob_append(pOut, zClassRm, -1);
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
  if( html ) blob_append(pOut, "<pre class=\"udiff\">\n", -1);
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
        na ? a+skip+1 : a+skip, na,
        nb ? b+skip+1 : b+skip, nb);
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
  if( html ) blob_append(pOut, "</pre>\n", -1);
}

/*
** Limits for the intra-line diffing.
*/
#define SBS_MXN  8  /* Maximum number of change regions per line of text */
#define SBS_CSN  8  /* Maximum number of change spans across a change region */

/*
** Status of a single output line
*/
typedef struct SbsLine SbsLine;
struct SbsLine {
  Blob *apCols[5];         /* Array of pointers to output columns */
  int width;               /* Maximum width of a column in the output */
  unsigned char escHtml;   /* True to escape html characters */
  struct SbsMark {
    int iStart;              /* Write zTag prior to character iStart */
    int iEnd;                /* Write </span> prior to character iEnd */
    const char *zTag;        /* A <span> tag for coloration */
  } a[SBS_MXN];            /* Change regions */
  int n;                   /* Number of change regions used */
  ReCompiled *pRe;         /* Only colorize matching lines, if not NULL */
};

/*
** A description of zero or more (up to SBS_CSN) areas of commonality
** between two lines of text.
*/
typedef struct ChangeSpan ChangeSpan;
struct ChangeSpan {
  int n;            /* Number of change spans */
  struct Span {
    int iStart1;    /* Byte offset to start of change on the left */
    int iLen1;      /* Length of left change span in bytes */
    int iStart2;    /* Byte offset to start of change span on the right */
    int iLen2;      /* Length of right change span in bytes */
    int isMin;      /* True if this span is known to have no useful subdivs */
  } a[SBS_CSN];     /* Array of change spans, sorted order */
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
  int colorize = p->escHtml && p->n>0;
  if( colorize && p->pRe && re_dline_match(p->pRe, pLine, 1)==0 ){
    colorize = 0;
  }
  for(i=k=0; (p->escHtml || k<w) && i<n; i++, k++){
    char c = zIn[i];
    if( colorize ){
      if( i==p->a[0].iStart ){
        int x = strlen(p->a[0].zTag);
        blob_append(pCol, p->a[0].zTag, x);
        needEndSpan = 1;
      }else if( i==p->a[0].iEnd ){
        blob_append(pCol, "</span>", 7);
        needEndSpan = 0;
        if( p->n>1 ){
          p->n--;
          memmove(p->a, p->a+1, sizeof(p->a[0])*p->n);
        }else{
          colorize = 0;
        }
      }
    }
    if( c>'>' ){
      blob_append_char(pCol, c);
      if( (c&0xc0)==0x80 ) k--;
    }else if( c=='\t' && !p->escHtml ){
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
      blob_append_char(pCol, c);
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
  int i, j, k;               /* Loop counters */
  int lenBest = 0;           /* Match length to beat */

  for(i=0; i<nA-lenBest; i++){
    unsigned char cA = zA[i];
    if( (cA&0xc0)==0x80 ) continue;
    for(j=0; j<nB-lenBest; j++ ){
      if( zB[j]==cA ){
        for(k=1; j+k<nB && i+k<nA && zB[j+k]==zA[i+k]; k++){}
        while( (zB[j+k]&0xc0)==0x80 ){ k--; }
        if( k>lenBest ){
          lenBest = k;
          aLCS[0] = i;
          aLCS[1] = i+k;
          aLCS[2] = j;
          aLCS[3] = j+k;
        }
      }
    }
  }
  return lenBest>0;
}

/*
** Find the smallest spans that are different between two text strings that
** are known to be different on both ends.
*/
static int textChangeSpans(
  const char *zLeft,  int nA,       /* String on the left */
  const char *zRight,  int nB,      /* String on the right */
  ChangeSpan *p                     /* Write results here */
){
  p->n = 1;
  p->a[0].iStart1 = 0;
  p->a[0].iLen1 = nA;
  p->a[0].iStart2 = 0;
  p->a[0].iLen2 = nB;
  p->a[0].isMin = 0;
  while( p->n<SBS_CSN ){
    int mxi = -1;
    int mxLen = -1;
    int x, i;
    int aLCS[4];
    struct Span *a, *b;
    for(i=0; i<p->n; i++){
      if( p->a[i].isMin ) continue;
      x = p->a[i].iLen1;
      if( p->a[i].iLen2<x ) x = p->a[i].iLen2;
      if( x>mxLen ){
        mxLen = x;
        mxi = i;
      }
    }
    if( mxLen<6 ) break;
    x = textLCS(zLeft + p->a[mxi].iStart1, p->a[mxi].iLen1,
                zRight + p->a[mxi].iStart2, p->a[mxi].iLen2, aLCS);
    if( x==0 ){
      p->a[mxi].isMin = 1;
      continue;
    }
    a = p->a+mxi;
    b = a+1;
    if( mxi<p->n-1 ){
      memmove(b+1, b, sizeof(*b)*(p->n-mxi-1));
    }
    p->n++;
    b->iStart1 = a->iStart1 + aLCS[1];
    b->iLen1 = a->iLen1 - aLCS[1];
    a->iLen1 = aLCS[0];
    b->iStart2 = a->iStart2 + aLCS[3];
    b->iLen2 = a->iLen2 - aLCS[3];
    a->iLen2 = aLCS[2];
    b->isMin = 0;
  }
  return p->n;
}

/*
** Try to shift a[0].iStart as far as possible to the left.
*/
static void sbsShiftLeft(SbsLine *p, const char *z){
  int i, j;
  while( (i=p->a[0].iStart)>0 && z[i-1]==z[i] ){
    for(j=i+1; j<p->a[0].iEnd && z[j-1]==z[j]; j++){}
    if( j<p->a[0].iEnd ) break;
    p->a[0].iStart--;
    p->a[0].iEnd--;
  }
}

/*
** Simplify the diff-marks in a single line.
**
**    *  Remove any null (zero-length) diff marks.
**    *  Make sure all changes are at character boundaries for
**       multi-byte characters.
*/
static void sbsSimplifyLine(SbsLine *p, const char *z){
  int i, j;

  /* Remove zero-length spans */
  for(i=j=0; i<p->n; i++){
    if( p->a[i].iStart<p->a[i].iEnd ){
      if( j<i ) memcpy(p->a+j, p->a+i, sizeof(p->a[0]));
      j++;
    }
  }
  p->n = j;

  /* Align all spans to a character boundary */
  for(i=0; i<p->n; i++){
    struct SbsMark *a = p->a+i;
    while( a->iStart>0 && (z[a->iStart]&0xc0)==0x80 ) a->iStart--;
    while( (z[a->iEnd]&0xc0)==0x80 ) a->iEnd++;
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
  ChangeSpan CSpan;    /* Set of changes on a line */

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
    p->a[0].iStart = p->a[0].iEnd = -1;
    p->n = 0;
    sbsWriteText(p, pLeft, SBS_TXTA);
    if( nLeft==nRight && zLeft[nLeft]==zRight[nRight] ){
      sbsWriteMarker(p, "   ", "");
    }else{
      sbsWriteMarker(p, " | ", "|");
    }
    sbsWriteLineno(p, lnRight, SBS_LNB);
    p->a[0].iStart = nPrefix;
    p->a[0].iEnd = nRight - nSuffix;
    p->n = 1;
    p->a[0].zTag = zClassAdd;
    sbsWriteText(p, pRight, SBS_TXTB);
    return;
  }

  /* A single chunk of text deleted from the left */
  if( nPrefix+nSuffix==nRight ){
    /* Text deleted from the left */
    sbsWriteLineno(p, lnLeft, SBS_LNA);
    p->a[0].iStart = nPrefix;
    p->a[0].iEnd = nLeft - nSuffix;
    p->a[0].zTag = zClassRm;
    p->n = 1;
    sbsWriteText(p, pLeft, SBS_TXTA);
    sbsWriteMarker(p, " | ", "|");
    sbsWriteLineno(p, lnRight, SBS_LNB);
    p->a[0].iStart = p->a[0].iEnd = -1;
    p->n = 0;
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
   && textChangeSpans(&zLeft[nPrefix], nLeftDiff,
                      &zRight[nPrefix], nRightDiff, &CSpan)>1
  ){
    int i, j;
    sbsWriteLineno(p, lnLeft, SBS_LNA);
    for(i=j=0; i<CSpan.n; i++){
      if( CSpan.a[i].iLen1==0 ) continue;
      p->a[j].iStart = nPrefix + CSpan.a[i].iStart1;
      p->a[j].iEnd = p->a[j].iStart + CSpan.a[i].iLen1;
      if( CSpan.a[i].iLen2==0 ){
        if( i==0 ) sbsShiftLeft(p, zLeft);
        p->a[j].zTag = zClassRm;
      }else{
        p->a[j].zTag = zClassChng;
      }
      j++;
    }
    p->n = j;
    sbsSimplifyLine(p, zLeft);
    sbsWriteText(p, pLeft, SBS_TXTA);
    sbsWriteMarker(p, " | ", "|");
    sbsWriteLineno(p, lnRight, SBS_LNB);
    for(i=j=0; i<CSpan.n; i++){
      if( CSpan.a[i].iLen2==0 ) continue;
      p->a[j].iStart = nPrefix + CSpan.a[i].iStart2;
      p->a[j].iEnd = p->a[j].iStart + CSpan.a[i].iLen2;
      if( CSpan.a[i].iLen1==0 ){
        if( i==0 ) sbsShiftLeft(p, zRight);
        p->a[j].zTag = zClassAdd;
      }else{
        p->a[j].zTag = zClassChng;
      }
      j++;
    }
    p->n = j;
    sbsSimplifyLine(p, zRight);
    sbsWriteText(p, pRight, SBS_TXTB);
    return;
  }

  /* If all else fails, show a single big change between left and right */
  sbsWriteLineno(p, lnLeft, SBS_LNA);
  p->a[0].iStart = nPrefix;
  p->a[0].iEnd = nLeft - nSuffix;
  p->a[0].zTag = zClassChng;
  p->n = 1;
  sbsWriteText(p, pLeft, SBS_TXTA);
  sbsWriteMarker(p, " | ", "|");
  sbsWriteLineno(p, lnRight, SBS_LNB);
  p->a[0].iStart = nPrefix;
  p->a[0].iEnd = nRight - nSuffix;
  p->a[0].zTag = zClassChng;
  p->n = 1;
  sbsWriteText(p, pRight, SBS_TXTB);
}

/*
** Given two lines of text, pFrom and pTo, compute a set of changes
** between those two lines, for enhanced display purposes.
**
** The result is written into the ChangeSpan object given by the
** third parameter.
*/
static void oneLineChange(
  const DLine *pLeft,  /* Left line of the change */
  const DLine *pRight, /* Right line of the change */
  ChangeSpan *p        /* OUTPUT: Write the results here */
){
  int nLeft;           /* Length of left line in bytes */
  int nRight;          /* Length of right line in bytes */
  int nShort;          /* Shortest of left and right */
  int nPrefix;         /* Length of common prefix */
  int nSuffix;         /* Length of common suffix */
  int nCommon;         /* Total byte length of suffix and prefix */
  const char *zLeft;   /* Text of the left line */
  const char *zRight;  /* Text of the right line */
  int nLeftDiff;       /* nLeft - nPrefix - nSuffix */
  int nRightDiff;      /* nRight - nPrefix - nSuffix */

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
    while( nSuffix<nShort
           && zLeft[nLeft-nSuffix-1]==zRight[nRight-nSuffix-1] ){
      nSuffix++;
    }
    if( nSuffix<nShort ){
      while( nSuffix>0 && (zLeft[nLeft-nSuffix]&0xc0)==0x80 ) nSuffix--;
    }
    if( nSuffix==nLeft || nSuffix==nRight ) nPrefix = 0;
  }
  nCommon = nPrefix + nSuffix;

  /* If the prefix and suffix overlap, that means that we are dealing with
  ** a pure insertion or deletion of text that can have multiple alignments.
  ** Try to find an alignment to begins and ends on whitespace, or on
  ** punctuation, rather than in the middle of a name or number.
  */
  if( nCommon > nShort ){
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
    nCommon = nPrefix + nSuffix;
  }

  /* A single chunk of text inserted */
  if( nCommon==nLeft ){
    p->n = 1;
    p->a[0].iStart1 = nPrefix;
    p->a[0].iLen1 = 0;
    p->a[0].iStart2 = nPrefix;
    p->a[0].iLen2 = nRight - nCommon;
    return;
  }

  /* A single chunk of text deleted */
  if( nCommon==nRight ){
    p->n = 1;
    p->a[0].iStart1 = nPrefix;
    p->a[0].iLen1 = nLeft - nCommon;
    p->a[0].iStart2 = nPrefix;
    p->a[0].iLen2 = 0;
    return;
  }

  /* At this point we know that there is a chunk of text that has
  ** changed between the left and the right.  Check to see if there
  ** is a large unchanged section in the middle of that changed block.
  */
  nLeftDiff = nLeft - nCommon;
  nRightDiff = nRight - nCommon;
  if( nLeftDiff >= 4
   && nRightDiff >= 4
   && textChangeSpans(&zLeft[nPrefix], nLeftDiff,
                      &zRight[nPrefix], nRightDiff, p)>1
  ){
    int i;
    for(i=0; i<p->n; i++){
      p->a[i].iStart1 += nPrefix;
      p->a[i].iStart2 += nPrefix;
    }
    return;
  }

  /* If all else fails, show a single big change between left and right */
  p->n = 1;
  p->a[0].iStart1 = nPrefix;
  p->a[0].iLen1 = nLeft - nCommon;
  p->a[0].iStart2 = nPrefix;
  p->a[0].iLen2 = nRight - nCommon;
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
static int match_dline(const DLine *pA, const DLine *pB){
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
** Algorithm:  Wagner's minimum edit-distance algorithm, modified by
** adding a cost to each match based on how well the two rows match
** each other.  Insertion and deletion costs are 50.  Match costs
** are between 0 and 100 where 0 is a perfect match 100 is a complete
** mismatch.
*/
static unsigned char *diffBlockAlignment(
   const DLine *aLeft, int nLeft,     /* Text on the left */
   const DLine *aRight, int nRight,   /* Text on the right */
   u64 diffFlags                      /* Flags passed into the original diff */
){
  int i, j, k;                 /* Loop counters */
  int *a;                      /* One row of the Wagner matrix */
  int *pToFree;                /* Space that needs to be freed */
  unsigned char *aM;           /* Wagner result matrix */
  int nMatch, iMatch;          /* Number of matching lines and match score */
  int mnLen;                   /* MIN(nLeft, nRight) */
  int mxLen;                   /* MAX(nLeft, nRight) */
  int aBuf[100];               /* Stack space for a[] if nRight not to big */

  if( nLeft==0 ){
    aM = fossil_malloc( nLeft + nRight + 2 );
    memset(aM, 2, nRight);
    return aM;
  }
  if( nRight==0 ){
    aM = fossil_malloc( nLeft + nRight + 2 );
    memset(aM, 1, nLeft);
    return aM;
  }

  /* This algorithm is O(N**2).  So if N is too big, bail out with a
  ** simple (but stupid and ugly) result that doesn't take too long. */
  mnLen = nLeft<nRight ? nLeft : nRight;
  if( nLeft*nRight>100000 && (diffFlags & DIFF_SLOW_SBS)==0 ){
    aM = fossil_malloc( nLeft + nRight + 2 );
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
  aM = fossil_malloc( (nLeft+1)*(nRight+1) );


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
  s.a[0].iStart = -1;
  s.a[1].iStart = 0;
  s.a[0].iEnd = -1;
  s.n = 0;
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
      s.a[0].iStart = s.a[0].iEnd = -1;
      s.n = 1;
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

      alignment = diffBlockAlignment(&A[a], ma, &B[b], mb, diffFlags);
      for(j=0; ma+mb>0; j++){
        if( alignment[j]==1 ){
          /* Delete one line from the left */
          sbsWriteLineno(&s, a, SBS_LNA);
          s.a[0].iStart = 0;
          s.a[0].zTag = zClassRm;
          s.a[0].iEnd = LENGTH(&A[a]);
          s.n = 1;
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
          s.a[0].iStart = 0;
          s.a[0].zTag = zClassAdd;
          s.a[0].iEnd = LENGTH(&B[b]);
          s.n = 1;
          sbsWriteText(&s, &B[b], SBS_TXTB);
          assert( mb>0 );
          mb--;
          b++;
        }else{
          /* Delete from the left and insert on the right */
          sbsWriteLineno(&s, a, SBS_LNA);
          s.a[0].iStart = 0;
          s.a[0].zTag = zClassRm;
          s.a[0].iEnd = LENGTH(&A[a]);
          s.n = 1;
          sbsWriteText(&s, &A[a], SBS_TXTA);
          sbsWriteMarker(&s, " | ", "|");
          sbsWriteLineno(&s, b, SBS_LNB);
          s.a[0].iStart = 0;
          s.a[0].zTag = zClassAdd;
          s.a[0].iEnd = LENGTH(&B[b]);
          s.n = 1;
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
          s.a[0].iStart = s.a[0].iEnd = -1;
          s.n = 0;
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
      s.a[0].iStart = s.a[0].iEnd = -1;
      s.n = 0;
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
** This is an abstract superclass for an object that accepts difference
** lines and formats them for display.  Subclasses of this object format
** the diff output in different ways.
**
** To subclass, create an instance of the DiffBuilder object and fill
** in appropriate method implementations.
*/ 
typedef struct DiffBuilder DiffBuilder;
struct DiffBuilder {
  void (*xSkip)(DiffBuilder*, unsigned int, int);
  void (*xCommon)(DiffBuilder*,const DLine*);
  void (*xInsert)(DiffBuilder*,const DLine*);
  void (*xDelete)(DiffBuilder*,const DLine*);
  void (*xReplace)(DiffBuilder*,const DLine*, const DLine*);
  void (*xEdit)(DiffBuilder*,const DLine*,const DLine*);
  void (*xEnd)(DiffBuilder*);
  unsigned int lnLeft;              /* Lines seen on the left (delete) side */
  unsigned int lnRight;             /* Lines seen on the right (insert) side */
  unsigned int nPending;            /* Number of pending lines */
  int eState;                       /* State of the output */
  Blob *pOut;                       /* Output blob */
  Blob aCol[5];                     /* Holding blobs */
};

/************************* DiffBuilderDebug ********************************/
/* This version of DiffBuilder is used for debugging the diff and diff
** diff formatter logic.  It is accessed using the (undocumented) --debug
** option to the diff command.  The output is human-readable text that
** describes the various method calls that are invoked agains the DiffBuilder
** object.
*/
static void dfdebugSkip(DiffBuilder *p, unsigned int n, int isFinal){
  blob_appendf(p->pOut, "SKIP %d (%d..%d left and %d..%d right)%s\n",
                n, p->lnLeft+1, p->lnLeft+n, p->lnRight+1, p->lnRight+n,
                isFinal ? " FINAL" : "");
  p->lnLeft += n;
  p->lnRight += n;
}
static void dfdebugCommon(DiffBuilder *p, const DLine *pLine){
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut, "COMMON  %8u %8u %.*s\n",
      p->lnLeft, p->lnRight, (int)pLine->n, pLine->z);
}
static void dfdebugInsert(DiffBuilder *p, const DLine *pLine){
  p->lnRight++;
  blob_appendf(p->pOut, "INSERT           %8d %.*s\n",
      p->lnRight, (int)pLine->n, pLine->z);
}
static void dfdebugDelete(DiffBuilder *p, const DLine *pLine){
  p->lnLeft++;
  blob_appendf(p->pOut, "DELETE   %8u          %.*s\n",
      p->lnLeft, (int)pLine->n, pLine->z);
}
static void dfdebugReplace(DiffBuilder *p, const DLine *pX, const DLine *pY){
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut, "REPLACE %8u          %.*s\n",
      p->lnLeft, (int)pX->n, pX->z);
  blob_appendf(p->pOut, "            %8u %.*s\n",
      p->lnRight, (int)pY->n, pY->z);
}
static void dfdebugEdit(DiffBuilder *p, const DLine *pX, const DLine *pY){
  int i, j;
  int x;
  ChangeSpan span;
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut, "EDIT     %8u          %.*s\n",
               p->lnLeft, (int)pX->n, pX->z);
  oneLineChange(pX, pY, &span);
  for(i=x=0; i<span.n; i++){
    int ofst = span.a[i].iStart1;
    int len = span.a[i].iLen1;
    if( len ){
      if( x==0 ){ blob_appendf(p->pOut, "%*s", 25, ""); }
      while( ofst > x ){
        if( (pX->z[x]&0xc0)!=0x80 ) blob_append_char(p->pOut, ' ');
        x++;
      }
      for(j=0; j<len; j++, x++){
        if( (pX->z[x]&0xc0)!=0x80 ) blob_append_char(p->pOut, '^');
      }
    }
  }
  if( x ) blob_append_char(p->pOut, '\n');
  blob_appendf(p->pOut, "                %8u %.*s\n",
               p->lnRight, (int)pY->n, pY->z);
  for(i=x=0; i<span.n; i++){
    int ofst = span.a[i].iStart2;
    int len = span.a[i].iLen2;
    if( len ){
      if( x==0 ){ blob_appendf(p->pOut, "%*s", 25, ""); }
      while( ofst > x ){
        if( (pY->z[x]&0xc0)!=0x80 ) blob_append_char(p->pOut, ' ');
        x++;
      }
      for(j=0; j<len; j++, x++){
        if( (pY->z[x]&0xc0)!=0x80 ) blob_append_char(p->pOut, '^');
      }
    }
  }
  if( x ) blob_append_char(p->pOut, '\n');
}
static void dfdebugEnd(DiffBuilder *p){
  blob_appendf(p->pOut, "END with %u lines left and %u lines right\n",
     p->lnLeft, p->lnRight);
  fossil_free(p);
}
static DiffBuilder *dfdebugNew(Blob *pOut){
  DiffBuilder *p = fossil_malloc(sizeof(*p));
  p->xSkip = dfdebugSkip;
  p->xCommon = dfdebugCommon;
  p->xInsert = dfdebugInsert;
  p->xDelete = dfdebugDelete;
  p->xReplace = dfdebugReplace;
  p->xEdit = dfdebugEdit;
  p->xEnd = dfdebugEnd;
  p->lnLeft = p->lnRight = 0;
  p->pOut = pOut;
  return p;
}

/************************* DiffBuilderTcl ********************************/
/*
** This formatter outputs a description of the diff formatted as TCL, for
** use by the --tk option to "diff".   See also the "diff.tcl" file.  The
** output can be viewed directly using the --tcl option.
**
** There is one line per method call:
**
**     SKIP n                      -- Skip "n" lines of input
**     COM string                  -- "string" is an unchanged context line
**     INS string                  -- "string" is in the right file only
**     DEL string                  -- "string" is in the left file only
**     EDIT string ....            -- Complex edit between left and right
**
** The EDIT verb will be followed by 3*N or 3*N+1 strings.  The triples
** each show:
**
**     1.  Common text
**     2.  Text from the left side
**     3.  Text on the right that replaces (2) from the left
**
** For inserted text (2) will be an empty string.  For deleted text, (3)
** will be an empty string.  (1) might be empty for the first triple if
** the line begins with an edit.  After all triples, there might be one
** additional string which is a common suffix.
*/
static void dftclSkip(DiffBuilder *p, unsigned int n, int isFinal){
  blob_appendf(p->pOut, "SKIP %u\n", n);
}
static void dftclCommon(DiffBuilder *p, const DLine *pLine){
  blob_appendf(p->pOut, "COM ");
  blob_append_string_literal(p->pOut, pLine->z, pLine->n);
  blob_append_char(p->pOut, '\n');
}
static void dftclInsert(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "INS ", -1);
  blob_append_string_literal(p->pOut, pLine->z, pLine->n);
  blob_append_char(p->pOut, '\n');
}
static void dftclDelete(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "DEL ", -1);
  blob_append_string_literal(p->pOut, pLine->z, pLine->n);
  blob_append_char(p->pOut, '\n');
}
static void dftclReplace(DiffBuilder *p, const DLine *pX, const DLine *pY){
  blob_append(p->pOut, "EDIT ", -1);
  blob_append_string_literal(p->pOut, pX->z, pX->n);
  blob_append_char(p->pOut, ' ');
  blob_append_string_literal(p->pOut, pY->z, pY->n);
  blob_append_char(p->pOut, '\n');
}
static void dftclEdit(DiffBuilder *p, const DLine *pX, const DLine *pY){
  int i, x;
  ChangeSpan span;
  blob_append(p->pOut, "EDIT", 4);
  oneLineChange(pX, pY, &span);
  for(i=x=0; i<span.n; i++){
    blob_append_char(p->pOut, ' ');
    blob_append_string_literal(p->pOut, pX->z + x, span.a[i].iStart1 - x);
    x = span.a[i].iStart1;
    blob_append_char(p->pOut, ' ');
    blob_append_string_literal(p->pOut, pX->z + x, span.a[i].iLen1);
    x += span.a[i].iLen1;
    blob_append_char(p->pOut, ' ');
    blob_append_string_literal(p->pOut, 
                         pY->z + span.a[i].iStart2, span.a[i].iLen2);
  }
  if( x<pX->n ){
    blob_append_char(p->pOut, ' ');
    blob_append_string_literal(p->pOut, pX->z + x, pX->n - x);
  }
  blob_append_char(p->pOut, '\n');
}
static void dftclEnd(DiffBuilder *p){
  fossil_free(p);
}
static DiffBuilder *dftclNew(Blob *pOut){
  DiffBuilder *p = fossil_malloc(sizeof(*p));
  p->xSkip = dftclSkip;
  p->xCommon = dftclCommon;
  p->xInsert = dftclInsert;
  p->xDelete = dftclDelete;
  p->xReplace = dftclReplace;
  p->xEdit = dftclEdit;
  p->xEnd = dftclEnd;
  p->pOut = pOut;
  return p;
}

/************************* DiffBuilderJson ********************************/
/*
** This formatter generates a JSON array that describes the difference.
**
** The Json array consists of integer opcodes with each opcode followed
** by zero or more arguments:
**
**   Syntax        Mnemonic    Description
**   -----------   --------    --------------------------
**   0             END         This is the end of the diff
**   1  INTEGER    SKIP        Skip N lines from both files
**   2  STRING     COMMON      The line show by STRING is in both files
**   3  STRING     INSERT      The line STRING is in only the right file
**   4  STRING     DELETE      The STRING line is in only the left file
**   5  SUBARRAY   EDIT        One line is different on left and right.
**
** The SUBARRAY is an array of 3*N+1 strings with N>=0.  The triples
** represent common-text, left-text, and right-text.  The last string
** in SUBARRAY is the common-suffix.  Any string can be empty if it does
** not apply.
*/
static void dfjsonSkip(DiffBuilder *p, unsigned int n, int isFinal){
  blob_appendf(p->pOut, "1,%u,\n", n);
}
static void dfjsonCommon(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "2,",2);
  blob_append_string_literal(p->pOut, pLine->z, (int)pLine->n);
  blob_append(p->pOut, ",\n",2);
}
static void dfjsonInsert(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "3,",2);
  blob_append_string_literal(p->pOut, pLine->z, (int)pLine->n);
  blob_append(p->pOut, ",\n",2);
}
static void dfjsonDelete(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "4,",2);
  blob_append_string_literal(p->pOut, pLine->z, (int)pLine->n);
  blob_append(p->pOut, ",\n",2);
}
static void dfjsonReplace(DiffBuilder *p, const DLine *pX, const DLine *pY){
  blob_append(p->pOut, "5,[\"\",",-1);
  blob_append_string_literal(p->pOut, pX->z, (int)pX->n);
  blob_append(p->pOut, ",",1);
  blob_append_string_literal(p->pOut, pY->z, (int)pY->n);
  blob_append(p->pOut, ",\"\"],\n",-1);
}
static void dfjsonEdit(DiffBuilder *p, const DLine *pX, const DLine *pY){
  int i, x;
  ChangeSpan span;
  blob_append(p->pOut, "5,[", 3);
  oneLineChange(pX, pY, &span);
  for(i=x=0; i<span.n; i++){
    blob_append_string_literal(p->pOut, pX->z + x, span.a[i].iStart1 - x);
    x = span.a[i].iStart1;
    blob_append_char(p->pOut, ',');
    blob_append_string_literal(p->pOut, pX->z + x, span.a[i].iLen1);
    x += span.a[i].iLen1;
    blob_append_char(p->pOut, ',');
    blob_append_string_literal(p->pOut, 
                         pY->z + span.a[i].iStart2, span.a[i].iLen2);
  }
  blob_append_char(p->pOut, ',');
  blob_append_string_literal(p->pOut, pX->z + x, pX->n - x);
  blob_append(p->pOut, "],\n",3);
}
static void dfjsonEnd(DiffBuilder *p){
  blob_append(p->pOut, "0]", 2);
  fossil_free(p);
}
static DiffBuilder *dfjsonNew(Blob *pOut){
  DiffBuilder *p = fossil_malloc(sizeof(*p));
  p->xSkip = dfjsonSkip;
  p->xCommon = dfjsonCommon;
  p->xInsert = dfjsonInsert;
  p->xDelete = dfjsonDelete;
  p->xReplace = dfjsonReplace;
  p->xEdit = dfjsonEdit;
  p->xEnd = dfjsonEnd;
  p->lnLeft = p->lnRight = 0;
  p->pOut = pOut;
  blob_append_char(pOut, '[');
  return p;
}

/************************* DiffBuilderUnified********************************/
/* This formatter generates a unified diff for HTML.
**
** The result is a <table> with four columns.  The four columns hold:
**
**     1.   The line numbers for the first file.
**     2.   The line numbers for the second file.
**     3.   The "diff mark":  "+" or "-" or just a space
**     4.   Text of the line
**
** Inserted lines are marked with <ins> and deleted lines are marked
** with <del>.  The whole line is marked this way, not just the part that
** changed.  The part that change has an additional nested <ins> or <del>.
** The CSS needs to be set up such that a single <ins> or <del> gives a
** light background and a nested <ins> or <del> gives a darker background.
** Additional attributes (like bold font) might also be added to nested
** <ins> and <del> since those are the characters that have actually
** changed.
**
** Accumulator strategy:
**
**    *   Delete line numbers are output directly to p->pOut
**    *   Insert line numbers accumulate in p->aCol[0].
**    *   Separator marks accumulate in p->aCol[1].
**    *   Change text accumulates in p->aCol[2].
**    *   Pending insert line numbers go into p->aCol[3].
**    *   Pending insert text goes into p->aCol[4].
**
** eState is 1 if text has an open <del>
*/
static void dfunifiedFinishDelete(DiffBuilder *p){
  if( p->eState==0 ) return;
  blob_append(p->pOut, "</del>", 6);
  blob_append(&p->aCol[2], "</del>", 6);
  p->eState = 0;
}
static void dfunifiedFinishInsert(DiffBuilder *p){
  unsigned int i;
  if( p->nPending==0 ) return;
  dfunifiedFinishDelete(p);

  /* Blank lines for delete line numbers for each inserted line */
  for(i=0; i<p->nPending; i++) blob_append_char(p->pOut, '\n');

  /* Insert line numbers */
  blob_append(&p->aCol[0], "<ins>", 5);
  blob_append_xfer(&p->aCol[0], &p->aCol[3]);
  blob_append(&p->aCol[0], "</ins>", 6);

  /* "+" marks for the separator on inserted lines */
  for(i=0; i<p->nPending; i++) blob_append(&p->aCol[1], "+\n", 2);

  /* Text of the inserted lines */
  blob_append(&p->aCol[2], "<ins>", 5);
  blob_append_xfer(&p->aCol[2], &p->aCol[4]);
  blob_append(&p->aCol[2], "</ins>", 6);
  
  p->nPending = 0;
}
static void dfunifiedFinishRow(DiffBuilder *p){
  dfunifiedFinishDelete(p);
  dfunifiedFinishInsert(p);
  if( blob_size(&p->aCol[0])==0 ) return;
  blob_append(p->pOut, "</pre></td><td class=\"diffln difflnr\"><pre>\n", -1);
  blob_append_xfer(p->pOut, &p->aCol[0]);
  blob_append(p->pOut, "</pre></td><td class=\"diffsep\"><pre>\n", -1);
  blob_append_xfer(p->pOut, &p->aCol[1]);
  blob_append(p->pOut, "</pre></td><td class=\"difftxt difftxtu\"><pre>\n",-1);
  blob_append_xfer(p->pOut, &p->aCol[2]);
  blob_append(p->pOut, "</pre></td></tr>\n", -1);
}
static void dfunifiedStartRow(DiffBuilder *p){
  if( blob_size(&p->aCol[0])>0 ) return;
  blob_append(p->pOut,"<tr><td class=\"diffln difflnl\"><pre>\n", -1);
  p->eState = 0;
  p->nPending = 0;
}
static void dfunifiedSkip(DiffBuilder *p, unsigned int n, int isFinal){
  dfunifiedFinishRow(p);
  if( (p->lnLeft || p->lnRight) && !isFinal ){
    blob_append(p->pOut, "<tr><td class=\"diffln difflne\">"
                         "&#xfe19;</td><td></td><td></td></tr>\n", -1);
  }
  p->lnLeft += n;
  p->lnRight += n;
}
static void dfunifiedCommon(DiffBuilder *p, const DLine *pLine){
  dfunifiedStartRow(p);
  dfunifiedFinishDelete(p);
  dfunifiedFinishInsert(p);
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  blob_appendf(&p->aCol[0],"%d\n",p->lnRight);
  blob_append_char(&p->aCol[1], '\n');
  htmlize_to_blob(&p->aCol[2], pLine->z, (int)pLine->n);
  blob_append_char(&p->aCol[2], '\n');
}
static void dfunifiedInsert(DiffBuilder *p, const DLine *pLine){
  dfunifiedStartRow(p);
  p->lnRight++;
  blob_appendf(&p->aCol[3],"%d\n", p->lnRight);
  blob_append(&p->aCol[4], "<ins>", 5);
  htmlize_to_blob(&p->aCol[4], pLine->z, (int)pLine->n);
  blob_append(&p->aCol[4], "</ins>\n", 7);
  p->nPending++;
}
static void dfunifiedDelete(DiffBuilder *p, const DLine *pLine){
  dfunifiedStartRow(p);
  dfunifiedFinishInsert(p);
  if( p->eState==0 ){
    dfunifiedFinishInsert(p);
    blob_append(p->pOut, "<del>", 5);
    blob_append(&p->aCol[2], "<del>", 5);
    p->eState = 1;
  }
  p->lnLeft++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  blob_append_char(&p->aCol[0],'\n');
  blob_append(&p->aCol[1],"-\n",2);
  blob_append(&p->aCol[2], "<del>", 5);
  htmlize_to_blob(&p->aCol[2], pLine->z, (int)pLine->n);
  blob_append(&p->aCol[2], "</del>\n", 7);
}
static void dfunifiedReplace(DiffBuilder *p, const DLine *pX, const DLine *pY){
  dfunifiedStartRow(p);
  if( p->eState==0 ){
    dfunifiedFinishInsert(p);
    blob_append(p->pOut, "<del>", 5);
    blob_append(&p->aCol[2], "<del>", 5);
    p->eState = 1;
  }
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  blob_append_char(&p->aCol[0], '\n');
  blob_append(&p->aCol[1], "-\n", 2);

  htmlize_to_blob(&p->aCol[2], pX->z,  pX->n);
  blob_append_char(&p->aCol[2], '\n');

  blob_appendf(&p->aCol[3],"%d\n", p->lnRight);

  htmlize_to_blob(&p->aCol[4], pY->z,  pY->n);
  blob_append_char(&p->aCol[4], '\n');
  p->nPending++;
}
static void dfunifiedEdit(DiffBuilder *p, const DLine *pX, const DLine *pY){
  int i;
  int x;
  ChangeSpan span;
  oneLineChange(pX, pY, &span);
  dfunifiedStartRow(p);
  if( p->eState==0 ){
    dfunifiedFinishInsert(p);
    blob_append(p->pOut, "<del>", 5);
    blob_append(&p->aCol[2], "<del>", 5);
    p->eState = 1;
  }
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  blob_append_char(&p->aCol[0], '\n');
  blob_append(&p->aCol[1], "-\n", 2);

  for(i=x=0; i<span.n; i++){
    int ofst = span.a[i].iStart1;
    int len = span.a[i].iLen1;
    if( len ){
      htmlize_to_blob(&p->aCol[2], pX->z+x, ofst - x);
      x = ofst;
      blob_append(&p->aCol[2], "<del>", 5);
      htmlize_to_blob(&p->aCol[2], pX->z+x, len);
      x += len;
      blob_append(&p->aCol[2], "</del>", 6);
    }
  }
  htmlize_to_blob(&p->aCol[2], pX->z+x,  pX->n - x);
  blob_append_char(&p->aCol[2], '\n');

  blob_appendf(&p->aCol[3],"%d\n", p->lnRight);
  for(i=x=0; i<span.n; i++){
    int ofst = span.a[i].iStart2;
    int len = span.a[i].iLen2;
    if( len ){
      htmlize_to_blob(&p->aCol[4], pY->z+x, ofst - x);
      x = ofst;
      blob_append(&p->aCol[4], "<ins>", 5);
      htmlize_to_blob(&p->aCol[4], pY->z+x, len);
      x += len;
      blob_append(&p->aCol[4], "</ins>", 6);
    }
  }
  htmlize_to_blob(&p->aCol[4], pY->z+x,  pY->n - x);
  blob_append_char(&p->aCol[4], '\n');
  p->nPending++;
}
static void dfunifiedEnd(DiffBuilder *p){
  dfunifiedFinishRow(p);
  blob_append(p->pOut, "</table>\n",-1);
  fossil_free(p);
}
static DiffBuilder *dfunifiedNew(Blob *pOut){
  DiffBuilder *p = fossil_malloc(sizeof(*p));
  p->xSkip = dfunifiedSkip;
  p->xCommon = dfunifiedCommon;
  p->xInsert = dfunifiedInsert;
  p->xDelete = dfunifiedDelete;
  p->xReplace = dfunifiedReplace;
  p->xEdit = dfunifiedEdit;
  p->xEnd = dfunifiedEnd;
  p->lnLeft = p->lnRight = 0;
  p->eState = 0;
  p->nPending = 0;
  p->pOut = pOut;
  blob_append(pOut, "<table class=\"diff udiff\">\n", -1);
  blob_init(&p->aCol[0], 0, 0);
  blob_init(&p->aCol[1], 0, 0);
  blob_init(&p->aCol[2], 0, 0);
  blob_init(&p->aCol[3], 0, 0);
  blob_init(&p->aCol[4], 0, 0);
  return p;
}

/************************* DiffBuilderSplit  ******************************/
/* This formatter creates a side-by-side diff in HTML.  The output is a
** <table> with 5 columns:
**
**    1.  Line numbers for the first file.
**    2.  Text for the first file.
**    3.  The difference mark.  "<", ">", "|" or blank
**    4.  Line numbers for the second file.
**    5.  Text for the second file.
**
** The <ins> and <del> strategy is the same as for unified diff above.
** In fact, the same CSS can be used for both.
**
** Accumulator strategy:
**
**    *   Left line numbers are output directly to p->pOut
**    *   Left text accumulates in p->aCol[0].
**    *   Edit marks accumulates in p->aCol[1].
**    *   Right line numbers accumulate in p->aCol[2].
**    *   Right text accumulates in p->aCol[3].
**
** eState:
**    0   In common block
**    1   Have <del> on the left
**    2   Have <ins> on the right
**    3   Have <del> on left and <ins> on the right
*/
static void dfsplitChangeState(DiffBuilder *p, int newState){
  if( p->eState == newState ) return;
  if( (p->eState&1)==0 && (newState & 1)!=0 ){
    blob_append(p->pOut, "<del>", 5);
    blob_append(&p->aCol[0], "<del>", 5);
    p->eState |= 1;
  }else if( (p->eState&1)!=0 && (newState & 1)==0 ){
    blob_append(p->pOut, "</del>", 6);
    blob_append(&p->aCol[0], "</del>", 6);
    p->eState &= ~1;
  }
  if( (p->eState&2)==0 && (newState & 2)!=0 ){
    blob_append(&p->aCol[2], "<ins>", 5);
    blob_append(&p->aCol[3], "<ins>", 5);
    p->eState |= 2;
  }else if( (p->eState&2)!=0 && (newState & 2)==0 ){
    blob_append(&p->aCol[2], "</ins>", 6);
    blob_append(&p->aCol[3], "</ins>", 6);
    p->eState &= ~2;
  }
}
static void dfsplitFinishRow(DiffBuilder *p){
  if( blob_size(&p->aCol[0])==0 ) return;
  dfsplitChangeState(p, 0);
  blob_append(p->pOut, "</pre></td><td class=\"difftxt difftxtl\"><pre>\n",-1);
  blob_append_xfer(p->pOut, &p->aCol[0]);
  blob_append(p->pOut, "</pre></td><td class=\"diffsep\"><pre>\n", -1);
  blob_append_xfer(p->pOut, &p->aCol[1]);
  blob_append(p->pOut, "</pre></td><td class=\"diffln difflnr\"><pre>\n",-1);
  blob_append_xfer(p->pOut, &p->aCol[2]);
  blob_append(p->pOut, "</pre></td><td class=\"difftxt difftxtr\"><pre>\n",-1);
  blob_append_xfer(p->pOut, &p->aCol[3]);
  blob_append(p->pOut, "</pre></td></tr>\n", -1);
}
static void dfsplitStartRow(DiffBuilder *p){
  if( blob_size(&p->aCol[0])>0 ) return;
  blob_append(p->pOut,"<tr><td class=\"diffln difflnl\"><pre>\n", -1);
  p->eState = 0;
}
static void dfsplitSkip(DiffBuilder *p, unsigned int n, int isFinal){
  dfsplitFinishRow(p);
  if( (p->lnLeft || p->lnRight) && !isFinal ){
    blob_append(p->pOut, 
       "<tr><td class=\"diffln difflnl difflne\">&#xfe19;</td>"
       "<td></td><td></td>"
       "<td class=\"diffln difflnr difflne\">&#xfe19;</td>"
       "<td/td></tr>\n", -1);
  }
  p->lnLeft += n;
  p->lnRight += n;
}
static void dfsplitCommon(DiffBuilder *p, const DLine *pLine){
  dfsplitStartRow(p);
  dfsplitChangeState(p, 0);
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  htmlize_to_blob(&p->aCol[0], pLine->z, (int)pLine->n);
  blob_append_char(&p->aCol[0], '\n');
  blob_append_char(&p->aCol[1], '\n');
  blob_appendf(&p->aCol[2],"%d\n",p->lnRight);
  htmlize_to_blob(&p->aCol[3], pLine->z, (int)pLine->n);
  blob_append_char(&p->aCol[3], '\n');
}
static void dfsplitInsert(DiffBuilder *p, const DLine *pLine){
  dfsplitStartRow(p);
  dfsplitChangeState(p, 2);
  p->lnRight++;
  blob_append_char(p->pOut, '\n');
  blob_append_char(&p->aCol[0], '\n');
  blob_append(&p->aCol[1], "&gt;\n", -1);
  blob_appendf(&p->aCol[2],"%d\n", p->lnRight);
  blob_append(&p->aCol[3], "<ins>", 5);
  htmlize_to_blob(&p->aCol[3], pLine->z, (int)pLine->n);
  blob_append(&p->aCol[3], "</ins>\n", 7);
}
static void dfsplitDelete(DiffBuilder *p, const DLine *pLine){
  dfsplitStartRow(p);
  dfsplitChangeState(p, 1);
  p->lnLeft++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  blob_append(&p->aCol[0], "<del>", 5);
  htmlize_to_blob(&p->aCol[0], pLine->z, (int)pLine->n);
  blob_append(&p->aCol[0], "</del>\n", 7);
  blob_append(&p->aCol[1], "&lt;\n", -1);
  blob_append_char(&p->aCol[2],'\n');
  blob_append_char(&p->aCol[3],'\n');
}
static void dfsplitReplace(DiffBuilder *p, const DLine *pX, const DLine *pY){
  dfsplitStartRow(p);
  dfsplitChangeState(p, 3);
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  htmlize_to_blob(&p->aCol[0], pX->z,  pX->n);
  blob_append_char(&p->aCol[0], '\n');

  blob_append(&p->aCol[1], "|\n", 2);

  blob_appendf(&p->aCol[2],"%d\n", p->lnRight);

  htmlize_to_blob(&p->aCol[3], pY->z,  pY->n);
  blob_append_char(&p->aCol[3], '\n');
}
static void dfsplitEdit(DiffBuilder *p, const DLine *pX, const DLine *pY){
  int i;
  int x;
   ChangeSpan span;
  oneLineChange(pX, pY, &span);
  dfsplitStartRow(p);
  dfsplitChangeState(p, 3);
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  for(i=x=0; i<span.n; i++){
    int ofst = span.a[i].iStart1;
    int len = span.a[i].iLen1;
    if( len ){
      htmlize_to_blob(&p->aCol[0], pX->z+x, ofst - x);
      x = ofst;
      blob_append(&p->aCol[0], "<del>", 5);
      htmlize_to_blob(&p->aCol[0], pX->z+x, len);
      x += len;
      blob_append(&p->aCol[0], "</del>", 6);
    }
  }
  htmlize_to_blob(&p->aCol[0], pX->z+x,  pX->n - x);
  blob_append_char(&p->aCol[0], '\n');

  blob_append(&p->aCol[1], "|\n", 2);

  blob_appendf(&p->aCol[2],"%d\n", p->lnRight);
  for(i=x=0; i<span.n; i++){
    int ofst = span.a[i].iStart2;
    int len = span.a[i].iLen2;
    if( len ){
      htmlize_to_blob(&p->aCol[3], pY->z+x, ofst - x);
      x = ofst;
      blob_append(&p->aCol[3], "<ins>", 5);
      htmlize_to_blob(&p->aCol[3], pY->z+x, len);
      x += len;
      blob_append(&p->aCol[3], "</ins>", 6);
    }
  }
  htmlize_to_blob(&p->aCol[3], pY->z+x,  pY->n - x);
  blob_append_char(&p->aCol[3], '\n');
}
static void dfsplitEnd(DiffBuilder *p){
  dfsplitFinishRow(p);
  blob_append(p->pOut, "</table>\n",-1);
  fossil_free(p);
}
static DiffBuilder *dfsplitNew(Blob *pOut){
  DiffBuilder *p = fossil_malloc(sizeof(*p));
  p->xSkip = dfsplitSkip;
  p->xCommon = dfsplitCommon;
  p->xInsert = dfsplitInsert;
  p->xDelete = dfsplitDelete;
  p->xReplace = dfsplitReplace;
  p->xEdit = dfsplitEdit;
  p->xEnd = dfsplitEnd;
  p->lnLeft = p->lnRight = 0;
  p->eState = 0;
  p->pOut = pOut;
  blob_append(pOut, "<table class=\"diff splitdiff\">\n", -1);
  blob_init(&p->aCol[0], 0, 0);
  blob_init(&p->aCol[1], 0, 0);
  blob_init(&p->aCol[2], 0, 0);
  blob_init(&p->aCol[3], 0, 0);
  blob_init(&p->aCol[4], 0, 0);
  return p;
}
/****************************************************************************/

/*
** Format a diff using a DiffBuilder object
*/
static void formatDiff(
  DContext *p,           /* The computed diff */
  ReCompiled *pRe,       /* Only show changes that match this regex */
  u64 diffFlags,         /* Flags controlling the diff */
  DiffBuilder *pBuilder  /* The formatter object */
){
  const DLine *A;        /* Left side of the diff */
  const DLine *B;        /* Right side of the diff */
  unsigned int a = 0;    /* Index of next line in A[] */
  unsigned int b = 0;    /* Index of next line in B[] */
  const int *R;          /* Array of COPY/DELETE/INSERT triples */
  unsigned int r;        /* Index into R[] */
  unsigned int nr;       /* Number of COPY/DELETE/INSERT triples to process */
  unsigned int mxr;      /* Maximum value for r */
  unsigned int na, nb;   /* Number of lines shown from A and B */
  unsigned int i, j;     /* Loop counters */
  unsigned int m, ma, mb;/* Number of lines to output */
  signed int skip = 0;   /* Number of lines to skip */
  unsigned int nContext; /* Lines of context above and below each change */

  nContext = diff_context_lines(diffFlags);
  A = p->aFrom;
  B = p->aTo;
  R = p->aEdit;
  mxr = p->nEdit;
  while( mxr>2 && R[mxr-1]==0 && R[mxr-2]==0 ){ mxr -= 3; }

  for(r=0; r<mxr; r += 3*nr){
    /* Figure out how many triples to show in a single block */
    for(nr=1; R[r+nr*3]>0 && R[r+nr*3]<nContext*2; nr++){}

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

    /* Figure out how many lines of A and B are to be displayed
    ** for this change block.
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

    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    if( r ) skip -= nContext;
    if( skip>0 ){
      pBuilder->xSkip(pBuilder, skip, 0);
    }
    for(j=0; j<m; j++){
      pBuilder->xCommon(pBuilder, &A[a+j]);
    }
    a += m;
    b += m;

    /* Show the differences */
    for(i=0; i<nr; i++){
      unsigned char *alignment;
      ma = R[r+i*3+1];   /* Lines on left but not on right */
      mb = R[r+i*3+2];   /* Lines on right but not on left */

      alignment = diffBlockAlignment(&A[a], ma, &B[b], mb, diffFlags);
      for(j=0; ma+mb>0; j++){
        switch( alignment[j] ){
          case 1: {
            /* Delete one line from the left */
            pBuilder->xDelete(pBuilder, &A[a]);
            ma--;
            a++;
            break;
          }
          case 2: {
            /* Insert one line on the right */
            pBuilder->xInsert(pBuilder, &B[b]);
            assert( mb>0 );
            mb--;
            b++;
            break;
          }
          case 3: {
            /* The left line is changed into the right line */
            pBuilder->xEdit(pBuilder, &A[a], &B[b]);
            assert( ma>0 && mb>0 );
            ma--;
            mb--;
            a++;
            b++;
            break;
          }
          case 4: {
            /* Delete from left then separately insert on the right */
            pBuilder->xReplace(pBuilder, &A[a], &B[b]);
            ma--;
            a++;
            mb--;
            b++;
            break;
          }
        }
      }
      fossil_free(alignment);
      if( i<nr-1 ){
        m = R[r+i*3+3];
        for(j=0; j<m; j++){
          pBuilder->xCommon(pBuilder, &A[a+j]);
        }
        b += m;
        a += m;
      }
    }

    /* Show the final common area */
    assert( nr==i );
    m = R[r+nr*3];
    if( m>nContext ) m = nContext;
    for(j=0; j<m && j<nContext; j++){
      pBuilder->xCommon(pBuilder, &A[a+j]);
    }
  }
  if( R[r]>nContext ){
    pBuilder->xSkip(pBuilder, R[r] - nContext, 1);
  }
  pBuilder->xEnd(pBuilder);
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
      if( p->xDiffer(&p->aFrom[i], &p->aTo[j]) ) continue;
      if( mxLength && p->xDiffer(&p->aFrom[i+mxLength], &p->aTo[j+mxLength]) ){
        continue;
      }
      k = 1;
      while( i+k<iE1 && j+k<iE2 && p->xDiffer(&p->aFrom[i+k],&p->aTo[j+k])==0 ){
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
      && (j-1<iS2 || j>=iE2 || p->xDiffer(&p->aFrom[i], &p->aTo[j-1]))
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
    for(k=0; k<n && p->xDiffer(pA,pB)==0; k++, pA--, pB--){}
    iSX -= k;
    iSY -= k;
    iEX = i+1;
    iEY = j;
    pA = &p->aFrom[iEX];
    pB = &p->aTo[iEY];
    n = minInt(iE1-iEX, iE2-iEY);
    for(k=0; k<n && p->xDiffer(pA,pB)==0; k++, pA++, pB++){}
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
  if( iSXb==iEXb && (sqlite3_int64)(iE1-iS1)*(iE2-iS2)<400 ){
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
  while( iE1>0 && iE2>0 && p->xDiffer(&p->aFrom[iE1-1], &p->aTo[iE2-1])==0 ){
    iE1--;
    iE2--;
  }
  mnE = iE1<iE2 ? iE1 : iE2;
  for(iS=0; iS<mnE && p->xDiffer(&p->aFrom[iS],&p->aTo[iS])==0; iS++){}

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
      if( p->xDiffer(pTop, pBtm) ) break;
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
      if( p->xDiffer(pTop, pBtm) ) break;
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
      if( p->xDiffer(pTop, pBtm) ) break;
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
      if( p->xDiffer(pTop, pBtm) ) break;
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
**
** Calculate the default automatically, based on terminal's current width:
**   term-width = 2*diff-col + diff-marker + 1
**   diff-col = lineno + lmargin + text-width + rmargin
**
**   text-width = (term-width - diff-marker - 1)/2 - lineno - lmargin - rmargin
*/
int diff_width(u64 diffFlags){
  int w = (diffFlags & DIFF_WIDTH_MASK)/(DIFF_CONTEXT_MASK+1);
  if( w==0 ){
    static struct {
      unsigned int lineno, lmargin, text, rmargin, marker;
    } sbsW = { 5, 2, 0, 0, 3 };
    const unsigned int wMin = 24, wMax = 132;
    unsigned int tw = terminal_get_width(80);
    unsigned int twMin =
      (wMin + sbsW.lineno + sbsW.lmargin + sbsW.rmargin)*2 + sbsW.marker + 1;
    unsigned int twMax =
      (wMax + sbsW.lineno + sbsW.lmargin + sbsW.rmargin)*2 + sbsW.marker + 1;

    if( tw<twMin ){
      tw = twMin;
    }else if( tw>twMax ){
      tw = twMax;
    }
    sbsW.text =
      (tw - sbsW.marker - 1)/2 - sbsW.lineno - sbsW.lmargin - sbsW.rmargin;
    w = sbsW.text;
  }
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
** Generate a report of the differences between files pA_Blob and pB_Blob.
**
** If pOut!=NULL then append text to pOut that will be the difference,
** formatted according to flags in diffFlags.  The pOut Blob must have
** already been initialized.
**
** If pOut==NULL then no formatting occurs.  Instead, this routine
** returns a pointer to an array of integers.  The integers come in
** triples.  The elements of each triple are:
**
**   1.  The number of lines to copy
**   2.  The number of lines to delete
**   3.  The number of lines to insert
**
** The return vector is terminated bin a triple of all zeros.  The caller
** should free the returned vector using fossil_free().
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
    c.xDiffer = same_dline_ignore_allws;
  }else{
    c.xDiffer = same_dline;
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
      g.diffCnt[1] += nIns;
      g.diffCnt[2] += nDel;
      if( nIns+nDel ){
        g.diffCnt[0]++;
        blob_appendf(pOut, "%10d %10d", nIns, nDel);
      }
    }else if( diffFlags & DIFF_RAW ){
      const int *R = c.aEdit;
      unsigned int r;
      for(r=0; R[r] || R[r+1] || R[r+2]; r += 3){
        blob_appendf(pOut, " copy %6d  delete %6d  insert %6d\n",
                     R[r], R[r+1], R[r+2]);
      }
    }else if( diffFlags & DIFF_JSON ){
      DiffBuilder *pBuilder = dfjsonNew(pOut);
      formatDiff(&c, pRe, diffFlags, pBuilder);
      blob_append_char(pOut, '\n');
    }else if( diffFlags & DIFF_TCL ){
      DiffBuilder *pBuilder = dftclNew(pOut);
      formatDiff(&c, pRe, diffFlags, pBuilder);
    }else if( diffFlags & DIFF_SIDEBYSIDE ){
      if( diffFlags & DIFF_HTML ){
        DiffBuilder *pBuilder = dfsplitNew(pOut);
        formatDiff(&c, pRe, diffFlags, pBuilder);
      }else{
        sbsDiff(&c, pOut, pRe, diffFlags);
      }
    }else if( diffFlags & DIFF_DEBUG ){
      DiffBuilder *pBuilder = dfdebugNew(pOut);
      formatDiff(&c, pRe, diffFlags, pBuilder);
    }else if( diffFlags & DIFF_HTML ){
      DiffBuilder *pBuilder = dfunifiedNew(pOut);
      formatDiff(&c, pRe, diffFlags, pBuilder);
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
  if( find_option("webpage",0,0)!=0 ){
    diffFlags |= DIFF_HTML|DIFF_WEBPAGE|DIFF_LINENO;
  }
  if( find_option("browser","b",0)!=0 ){
    diffFlags |= DIFF_HTML|DIFF_WEBPAGE|DIFF_LINENO|DIFF_BROWSER;
  }
  if( find_option("by",0,0)!=0 ){
    diffFlags |= DIFF_HTML|DIFF_WEBPAGE|DIFF_LINENO|DIFF_BROWSER
                   |DIFF_SIDEBYSIDE;
  }
  if( find_option("json",0,0)!=0 ){
    diffFlags |= DIFF_JSON;
  }
  if( find_option("tcl",0,0)!=0 ){
    diffFlags |= DIFF_TCL;
  }

  /* Undocumented and unsupported flags used for development
  ** debugging and analysis: */
  if( find_option("debug",0,0)!=0 ) diffFlags |= DIFF_DEBUG;
  if( find_option("raw",0,0)!=0 )   diffFlags |= DIFF_RAW;
  return diffFlags;
}

/*
** COMMAND: test-diff
** COMMAND: xdiff
**
** Usage: %fossil xdiff [options] FILE1 FILE2
**
** This is the "external diff" feature.  By "external" here we mean a diff
** applied to files that are not under version control.  See the "diff"
** command for computing differences between files that are under control.
**
** This command prints the differences between the two files FILE1 and FILE2.
** all of the usual diff command-line options apply.  See the "diff" command
** for a full list of command-line options.
**
** This command used to be called "test-diff".  The older "test-diff" spelling
** still works, for compatibility.
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
  blob_zero(&out);
  diff_begin(diffFlag);
  diff_print_filenames(g.argv[2], g.argv[3], diffFlag, &out);
  blob_read_from_file(&a, g.argv[2], ExtFILE);
  blob_read_from_file(&b, g.argv[3], ExtFILE);
  text_diff(&a, &b, &out, pRe, diffFlag);
  blob_write_to_file(&out, "-");
  diff_end(diffFlag, 0);
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
    p->c.xDiffer = same_dline_ignore_allws;
  }else{
    p->c.xDiffer = same_dline;
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
  int cid;               /* Selected check-in ID */
  int origid = 0;        /* The origin ID or zero */
  int rid;               /* Artifact ID of the file being annotated */
  int fnid;              /* Filename ID */
  Stmt q;                /* Query returning all ancestor versions */
  int cnt = 0;           /* Number of versions analyzed */
  int iLimit;            /* Maximum number of versions to analyze */
  sqlite3_int64 mxTime;  /* Halt at this time if not already complete */

  memset(p, 0, sizeof(*p));

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

  /* Get the artifact ID for the check-in begin analyzed */
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
  fnid = db_int(0, "SELECT fnid FROM filename WHERE name=%Q", zFilename);
  if( fnid==0 ){
    fossil_fatal("no such file: %Q", zFilename);
  }

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

  if( p->nVers==0 ){
    if( zRevision ){
      fossil_fatal("file %s does not exist in check-in %s", zFilename, zRevision);
    }else{
      fossil_fatal("no history for file: %s", zFilename);
    }
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
** version of the file shows the first time each line in the file was changed
** or removed by any subsequent check-in.
**
** Query parameters:
**
**    checkin=ID          The check-in at which to start the annotation
**    filename=FILENAME   The filename.
**    filevers=BOOLEAN    Show file versions rather than check-in versions
**    limit=LIMIT         Limit the amount of analysis.  LIMIT can be one of:
**                           none   No limit
**                           Xs     As much as can be computed in X seconds
**                           N      N versions
**    log=BOOLEAN         Show a log of versions analyzed
**    origin=ID           The origin checkin.  If unspecified, the root
**                        check-in over the entire repository is used.
**                        Specify "origin=trunk" or similar for a reverse
**                        annotation
**    w=BOOLEAN           Ignore whitespace
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
  style_set_current_feature("annotate");
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
  style_submenu_checkbox("log", "Log", 0, "toggle_annotation_log");
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
    zLink = href("%R/finfo?name=%t&from=%!S&to=%!S",zFilename,zCI,zOrigin);
  }else{
    zLink = href("%R/finfo?name=%t&from=%!S",zFilename,zCI);
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

  if( !ann.bMoreToDo ){
    assert( ann.origId==0 );  /* bMoreToDo always set for a point-to-point */
    @ <h2>Origin for each line in
    @ %z(href("%R/finfo?name=%h&from=%!S", zFilename, zCI))%h(zFilename)</a>
    @ from check-in %z(href("%R/info/%!S",zCI))%S(zCI)</a>:</h2>
  }else if( ann.origId>0 ){
    @ <h2>Lines of
    @ %z(href("%R/finfo?name=%h&from=%!S", zFilename, zCI))%h(zFilename)</a>
    @ from check-in %z(href("%R/info/%!S",zCI))%S(zCI)</a>
    @ that are changed by the sequence of edits moving toward
    @ check-in %z(href("%R/info/%!S",zOrigin))%S(zOrigin)</a>:</h2>
  }else{
    @ <h2>Lines added by the %d(ann.nVers) most recent ancestors of
    @ %z(href("%R/finfo?name=%h&from=%!S", zFilename, zCI))%h(zFilename)</a>
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
  style_finish_page();
}

/*
** COMMAND: annotate
** COMMAND: blame
** COMMAND: praise*
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
** of the file shows the first time each line in the file was changed or
** removed by any subsequent check-in.
**
** Options:
**   --filevers                  Show file version numbers rather than
**                               check-in versions
**   -r|--revision VERSION       The specific check-in containing the file
**   -l|--log                    List all versions analyzed
**   -n|--limit LIMIT            LIMIT can be one of:
**                                 N      Up to N versions
**                                 Xs     As much as possible in X seconds
**                                 none   No limit
**   -o|--origin VERSION         The origin check-in. By default this is the
**                               root of the repository. Set to "trunk" or
**                               similar for a reverse annotation.
**   -w|--ignore-all-space       Ignore white space when comparing lines
**   -Z|--ignore-trailing-space  Ignore whitespace at line end
**
** See also: [[info]], [[finfo]], [[timeline]]
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
  Blob treename;         /* Name of file to be annotated */
  char *zFilename;       /* Name of file to be annotated */

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
  file_tree_name(g.argv[2], &treename, 0, 1);
  zFilename = blob_str(&treename);
  annotate_file(&ann, zFilename, zRevision, zLimit, zOrig, annFlags);
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
