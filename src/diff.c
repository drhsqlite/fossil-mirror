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
#include <errno.h>


#if INTERFACE
/*
** Flag parameters to the text_diff() routine used to control the formatting
** of the diff output.
*/
#define DIFF_IGNORE_EOLWS      0x00000001 /* Ignore end-of-line whitespace */
#define DIFF_IGNORE_ALLWS      0x00000003 /* Ignore all whitespace */
#define DIFF_SIDEBYSIDE        0x00000004 /* Generate a side-by-side diff */
#define DIFF_VERBOSE           0x00000008 /* Missing shown as empty files */
#define DIFF_BRIEF             0x00000010 /* Show filenames only */
#define DIFF_HTML              0x00000020 /* Render for HTML */
#define DIFF_LINENO            0x00000040 /* Show line numbers */
#define DIFF_NUMSTAT           0x00000080 /* Show line count of changes */
#define DIFF_NOOPT             0x00000100 /* Suppress optimizations (debug) */
#define DIFF_INVERT            0x00000200 /* Invert the diff (debug) */
#define DIFF_CONTEXT_EX        0x00000400 /* Use context even if zero */
#define DIFF_NOTTOOBIG         0x00000800 /* Only display if not too big */
#define DIFF_STRIP_EOLCR       0x00001000 /* Strip trailing CR */
#define DIFF_SLOW_SBS          0x00002000 /* Better but slower side-by-side */
#define DIFF_WEBPAGE           0x00004000 /* Complete webpage */
#define DIFF_BROWSER           0x00008000 /* The --browser option */
#define DIFF_JSON              0x00010000 /* JSON output */
#define DIFF_DEBUG             0x00020000 /* Debugging diff output */
#define DIFF_RAW               0x00040000 /* Raw triples - for debugging */
#define DIFF_TCL               0x00080000 /* For the --tk option */
#define DIFF_INCBINARY         0x00100000 /* The --diff-binary option */
#define DIFF_SHOW_VERS         0x00200000 /* Show compared versions */
#define DIFF_DARKMODE          0x00400000 /* Use dark mode for HTML */
#define DIFF_BY_TOKEN          0x01000000 /* Split on tokens, not lines */

/*
** Per file information that may influence output.
*/
#define DIFF_FILE_ADDED        0x40000000 /* Added or rename destination */
#define DIFF_FILE_DELETED      0x80000000 /* Deleted or rename source */
#define DIFF_FILE_MASK         0xc0000000 /* Used for clearing file flags */

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

/*
** An instance of this object describes the formatting and processing
** details desired of a "diff" operation.
**
** Conceptually, this object is as an encoding of the command-line options
** for the "fossil diff" command.  That is not a precise description, though,
** because not all diff operations are started from the command-line.  But
** the idea is sound.
**
** Information encoded by this object includes but is not limited to:
**
**    *   The desired output format (unified vs. side-by-side,
**        TCL, JSON, HTML vs. plain-text).
**
**    *   Number of lines of context surrounding each difference block
**
**    *   Width of output columns for text side-by-side diffop
*/
struct DiffConfig {
  u64 diffFlags;           /* Diff flags */
  int nContext;            /* Number of lines of context */
  int wColumn;             /* Column width in -y mode */
  u32 nFile;               /* Number of files diffed so far */
  const char *zDiffCmd;    /* External diff command to use instead of builtin */
  const char *zBinGlob;    /* GLOB pattern for binary files */
  ReCompiled *pRe;         /* Show only changes matching this pattern */
  const char *zLeftHash;   /* HASH-id of the left file */
  const char *azLabel[2];  /* Optional labels for left and right files */
};

#endif /* INTERFACE */

/*
** Initialize memory for a DiffConfig based on just a diffFlags integer.
*/
DiffConfig *diff_config_init(DiffConfig *pCfg, u64 diffFlags){
  memset(pCfg, 0, sizeof(*pCfg));
  pCfg->diffFlags = diffFlags;
  return pCfg;
}

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
  unsigned short indent;  /* Index of first non-space */
  unsigned short n;       /* number of bytes */
  unsigned short nw;      /* number of bytes without leading/trailing space */
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
** Number of diff chunks generated
*/
static int nChunk = 0;

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
  int (*xDiffer)(const DLine *,const DLine *); /* comparison function */
};

/* Fast isspace for use by diff */
static const char diffIsSpace[] = {
  0, 0, 0, 0, 0, 0, 0, 0,   1, 1, 1, 0, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  1, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,

  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};
#define diff_isspace(X)  (diffIsSpace[(unsigned char)(X)])

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
    if( diffFlags & DIFF_IGNORE_EOLWS ){
      while( k>0 && diff_isspace(z[k-1]) ){ k--; }
    }
    if( (diffFlags & DIFF_IGNORE_ALLWS)==DIFF_IGNORE_ALLWS ){
      int numws = 0;
      for(s=0; s<k && z[s]<=' '; s++){}
      a[i].indent = s;
      a[i].nw = k - s;
      for(h=0, x=s; x<k; x++){
        char c = z[x];
        if( diff_isspace(c) ){
          ++numws;
        }else{
          h = (h^c)*9000000000000000041LL;
        }
      }
      k -= numws;
    }else{
      int k2 = k & ~0x7;
      u64 m;
      for(h=x=s=0; x<k2; x += 8){
        memcpy(&m, z+x, 8);
        h = (h^m)*9000000000000000041LL;
      }
      m = 0;
      memcpy(&m, z+x, k-k2);
      h ^= m;
    }
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
** Character classes for the purpose of tokenization.
**
**    1 - alphanumeric
**    2 - whitespace
**    3 - punctuation
*/
static char aTCharClass[256] = {
  2, 2, 2, 2,  2, 2, 2, 2,   2, 2, 2, 2,  2, 2, 2, 2, 
  2, 2, 2, 2,  2, 2, 2, 2,   2, 2, 2, 2,  2, 2, 2, 2, 
  2, 3, 3, 3,  3, 3, 3, 3,   3, 3, 3, 3,  3, 3, 3, 3,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 3, 3, 3,  3, 3, 3, 3,

  3, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 3,  3, 3, 3, 3,
  3, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 3,  3, 3, 3, 3,

  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,

  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1,
  1, 1, 1, 1,  1, 1, 1, 1,   1, 1, 1, 1,  1, 1, 1, 1
};

/*
** Count the number of tokens in the given string.
*/
static int count_tokens(const unsigned char *p, int n){
  int nToken = 0;
  int iPrev = 0;
  int i;
  for(i=0; i<n; i++){
    char x = aTCharClass[p[i]];
    if( x!=iPrev ){
      iPrev = x;
      nToken++;
    }
  }
  return nToken;
}

/*
** Return an array of DLine objects containing a pointer to the
** start of each token and a hash of that token.  The lower
** bits of the hash store the length of each token.
**
** This is like break_into_lines() except that it works with tokens
** instead of lines.  A token is:
**
**     *  A contiguous sequence of alphanumeric characters.
**     *  A contiguous sequence of whitespace
**     *  A contiguous sequence of punctuation characters.
**
** Return 0 if the file is binary or contains a line that is
** too long.
*/
static DLine *break_into_tokens(
  const char *z,
  int n,
  int *pnToken,
  u64 diffFlags
){
  int nToken, i, k;
  u64 h, h2;
  DLine *a;
  unsigned char *p = (unsigned char*)z;

  nToken = count_tokens(p, n);
  a = fossil_malloc( sizeof(a[0])*(nToken+1) );
  memset(a, 0, sizeof(a[0])*(nToken+1));
  if( n==0 ){
    *pnToken = 0;
    return a;
  }
  i = 0;
  while( n>0 ){
    char x = aTCharClass[*p];
    h = 0xcbf29ce484222325LL;
    for(k=1; k<n && aTCharClass[p[k]]==x; k++){
      h ^= p[k];
      h *= 0x100000001b3LL;
    }
    a[i].z = (char*)p;
    a[i].n = k;
    a[i].h = h = ((h%281474976710597LL)<<LENGTH_MASK_SZ) | k;
    h2 = h % nToken;
    a[i].iNext = a[h2].iHash;
    a[h2].iHash = i+1;
    p += k; n -= k;
    i++;
  };
  assert( i==nToken );

  /* Return results */
  *pnToken = nToken;
  return a;
}

/*
** Return zero if two DLine elements are identical.
*/
static int compare_dline(const DLine *pA, const DLine *pB){
  if( pA->h!=pB->h ) return 1;
  return memcmp(pA->z,pB->z, pA->h&LENGTH_MASK);
}

/*
** Return zero if two DLine elements are identical, ignoring
** all whitespace. The indent field of pA/pB already points
** to the first non-space character in the string.
*/
static int compare_dline_ignore_allws(const DLine *pA, const DLine *pB){
  if( pA->h==pB->h ){
    int a, b;
    if( memcmp(pA->z, pB->z, pA->h&LENGTH_MASK)==0 ) return 0;
    a = pA->indent;
    b = pB->indent;
    while( a<pA->n || b<pB->n ){
      if( a<pA->n && b<pB->n && pA->z[a++] != pB->z[b++] ) return 1;
      while( a<pA->n && diff_isspace(pA->z[a])) ++a;
      while( b<pB->n && diff_isspace(pB->z[b])) ++b;
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
  const DLine *pLine  /* The line to be output */
){
  blob_append_char(pOut, cPrefix);
  blob_append(pOut, pLine->z, pLine->n);
  blob_append_char(pOut, '\n');
}

/*
** Add two line numbers to the beginning of an output line for a context
** diff.  One or the other of the two numbers might be zero, which means
** to leave that number field blank.
*/
static void appendDiffLineno(Blob *pOut, int lnA, int lnB){
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
}

/*
** Output a patch-style text diff.
*/
static void contextDiff(
  DContext *p,      /* The difference */
  Blob *pOut,       /* Output a context diff to here */
  DiffConfig *pCfg  /* Configuration options */
){
  const DLine *A;   /* Left side of the diff */
  const DLine *B;   /* Right side of the diff */
  int a = 0;        /* Index of next line in A[] */
  int b = 0;        /* Index of next line in B[] */
  int *R;       /* Array of COPY/DELETE/INSERT triples */
  int r;        /* Index into R[] */
  int nr;       /* Number of COPY/DELETE/INSERT triples to process */
  int mxr;      /* Maximum value for r */
  int na, nb;   /* Number of lines shown from A and B */
  int i, j;     /* Loop counters */
  int m;        /* Number of lines to output */
  int skip;     /* Number of lines to skip */
  int nContext;    /* Number of lines of context */
  int showLn;      /* Show line numbers */
  int showDivider = 0;  /* True to show the divider between diff blocks */

  nContext = diff_context_lines(pCfg);
  showLn = (pCfg->diffFlags & DIFF_LINENO)!=0;
  A = p->aFrom;
  B = p->aTo;
  R = p->aEdit;
  mxr = p->nEdit;
  while( mxr>2 && R[mxr-1]==0 && R[mxr-2]==0 ){ mxr -= 3; }
  for(r=0; r<mxr; r += 3*nr){
    /* Figure out how many triples to show in a single block */
    for(nr=1; 3*nr<mxr && R[r+nr*3]>0 && R[r+nr*3]<(int)nContext*2; nr++){}
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

    /* Show the header for this block, or if we are doing a modified
    ** context diff that contains line numbers, show the separator from
    ** the previous block.
    */
    nChunk++;
    if( showLn ){
      if( !showDivider ){
        /* Do not show a top divider */
        showDivider = 1;
      }else{
        blob_appendf(pOut, "%.80c\n", '.');
      }
    }else{
      /*
       * If the patch changes an empty file or results in an empty file,
       * the block header must use 0,0 as position indicator and not 1,0.
       * Otherwise, patch would be confused and may reject the diff.
       */
      blob_appendf(pOut,"@@ -%d,%d +%d,%d @@",
        na ? a+skip+1 : a+skip, na,
        nb ? b+skip+1 : b+skip, nb);
      blob_append(pOut, "\n", 1);
    }

    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    for(j=0; j<m; j++){
      if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1);
      appendDiffLine(pOut, ' ', &A[a+j]);
    }
    a += m;
    b += m;

    /* Show the differences */
    for(i=0; i<nr; i++){
      m = R[r+i*3+1];
      for(j=0; j<m; j++){
        if( showLn ) appendDiffLineno(pOut, a+j+1, 0);
        appendDiffLine(pOut, '-', &A[a+j]);
      }
      a += m;
      m = R[r+i*3+2];
      for(j=0; j<m; j++){
        if( showLn ) appendDiffLineno(pOut, 0, b+j+1);
        appendDiffLine(pOut, '+', &B[b+j]);
      }
      b += m;
      if( i<nr-1 ){
        m = R[r+i*3+3];
        for(j=0; j<m; j++){
          if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1);
          appendDiffLine(pOut, ' ', &A[a+j]);
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
      if( showLn ) appendDiffLineno(pOut, a+j+1, b+j+1);
      appendDiffLine(pOut, ' ', &A[a+j]);
    }
  }
}

#define MX_CSN  8  /* Maximum number of change spans across a change region */

/*
** A description of zero or more (up to MX_CSN) areas of difference
** between two lines of text.
*/
typedef struct LineChange LineChange;
struct LineChange {
  int n;            /* Number of change spans */
  struct Span {
    int iStart1;    /* Byte offset to start of a change on the left */
    int iLen1;      /* Length of the left change in bytes */
    int iStart2;    /* Byte offset to start of a change on the right */
    int iLen2;      /* Length of the change on the right in bytes */
    int isMin;      /* True if this change is known to have no useful subdivs */
  } a[MX_CSN];     /* Array of change spans, sorted order */
};

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
static int textLineChanges(
  const char *zLeft,  int nA,       /* String on the left */
  const char *zRight,  int nB,      /* String on the right */
  LineChange *p                     /* Write results here */
){
  p->n = 1;
  p->a[0].iStart1 = 0;
  p->a[0].iLen1 = nA;
  p->a[0].iStart2 = 0;
  p->a[0].iLen2 = nB;
  p->a[0].isMin = 0;
  while( p->n<MX_CSN-1 ){
    int mxi = -1;
    int mxLen = -1;
    int x, i;
    int aLCS[4];
    struct Span *a, *b;
    memset(aLCS, 0, sizeof(aLCS));
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
** Return true if the string starts with n spaces
*/
static int allSpaces(const char *z, int n){
  int i;
  for(i=0; i<n && diff_isspace(z[i]); i++){}
  return i==n;
}

/*
** Try to improve the human-readability of the LineChange p.
**
** (1)  If the first change span shows a change of indentation, try to
**      move that indentation change to the left margin.
**
** (2)  Try to shift changes so that they begin or end with a space.
*/
static void improveReadability(
  const char *zA,  /* Left line of the change */
  const char *zB,  /* Right line of the change */
  LineChange *p    /* The LineChange to be adjusted */
){
  int j, n, len;
  if( p->n<1 ) return;

  /* (1) Attempt to move indentation changes to the left margin */
  if( p->a[0].iLen1==0
   && (len = p->a[0].iLen2)>0
   && (j = p->a[0].iStart2)>0
   && zB[0]==zB[j]
   && allSpaces(zB, j)
  ){
    for(n=1; n<len && n<j && zB[j]==zB[j+n]; n++){}
    if( n<len ){
      memmove(&p->a[1], &p->a[0], sizeof(p->a[0])*p->n);
      p->n++;
      p->a[0] = p->a[1];
      p->a[1].iStart2 += n;
      p->a[1].iLen2 -= n;
      p->a[0].iLen2 = n;
    }
    p->a[0].iStart1 = 0;
    p->a[0].iStart2 = 0;
  }else
  if( p->a[0].iLen2==0
   && (len = p->a[0].iLen1)>0
   && (j = p->a[0].iStart1)>0
   && zA[0]==zA[j]
   && allSpaces(zA, j)
  ){
    for(n=1; n<len && n<j && zA[j]==zA[j+n]; n++){}
    if( n<len ){
      memmove(&p->a[1], &p->a[0], sizeof(p->a[0])*p->n);
      p->n++;
      p->a[0] = p->a[1];
      p->a[1].iStart1 += n;
      p->a[1].iLen1 -= n;
      p->a[0].iLen1 = n;
    }
    p->a[0].iStart1 = 0;
    p->a[0].iStart2 = 0;
  }

  /* (2) Try to shift changes so that they begin or end with a
  ** space.  (TBD) */
}


/*
** Given two lines of text, pFrom and pTo, compute a set of changes
** between those two lines, for enhanced display purposes.
**
** The result is written into the LineChange object given by the
** third parameter.
*/
static void oneLineChange(
  const DLine *pLeft,  /* Left line of the change */
  const DLine *pRight, /* Right line of the change */
  LineChange *p        /* OUTPUT: Write the results here */
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
       if( diff_isspace(c) ){
         iVal += 5;
       }else if( !fossil_isalnum(c) ){
         iVal += 2;
       }
       c = zLeft[i+nGap-1];
       if( diff_isspace(c) ){
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
    improveReadability(zLeft, zRight, p);
    return;
  }

  /* A single chunk of text deleted */
  if( nCommon==nRight ){
    p->n = 1;
    p->a[0].iStart1 = nPrefix;
    p->a[0].iLen1 = nLeft - nCommon;
    p->a[0].iStart2 = nPrefix;
    p->a[0].iLen2 = 0;
    improveReadability(zLeft, zRight, p);
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
   && textLineChanges(&zLeft[nPrefix], nLeftDiff,
                      &zRight[nPrefix], nRightDiff, p)>1
  ){
    int i;
    for(i=0; i<p->n; i++){
      p->a[i].iStart1 += nPrefix;
      p->a[i].iStart2 += nPrefix;
    }
    improveReadability(zLeft, zRight, p);
    return;
  }

  /* If all else fails, show a single big change between left and right */
  p->n = 1;
  p->a[0].iStart1 = nPrefix;
  p->a[0].iLen1 = nLeft - nCommon;
  p->a[0].iStart2 = nPrefix;
  p->a[0].iLen2 = nRight - nCommon;
  improveReadability(zLeft, zRight, p);
}

/*
** COMMAND: test-line-diff
** Usage: %fossil% test-line-diff STRING1 STRING2
**
** Show the differences between the two strings.  Used for testing
** the oneLineChange() routine in the diff logic.
*/
void test_line_diff(void){
  DLine a, b;
  LineChange chng;
  int i, j, x;
  if( g.argc!=4 ) usage("STRING1 STRING2");
  a.z = g.argv[2];
  a.n = (int)strlen(a.z);
  b.z = g.argv[3];
  b.n = (int)strlen(b.z);
  oneLineChange(&a, &b, &chng);
  fossil_print("left:  [%s]\n", a.z);
  for(i=x=0; i<chng.n; i++){
    int ofst = chng.a[i].iStart1;
    int len = chng.a[i].iLen1;
    if( len ){
      if( x==0 ){ fossil_print("%*s", 8, ""); }
      while( ofst > x ){
        if( (a.z[x]&0xc0)!=0x80 ) fossil_print(" ");
        x++;
      }
      for(j=0; j<len; j++, x++){
        if( (a.z[x]&0xc0)!=0x80 ) fossil_print("%d",i);
      }
    }
  }
  if( x ) fossil_print("\n");
  fossil_print("right: [%s]\n", b.z);
  for(i=x=0; i<chng.n; i++){
    int ofst = chng.a[i].iStart2;
    int len = chng.a[i].iLen2;
    if( len ){
      if( x==0 ){ fossil_print("%*s", 8, ""); }
      while( ofst > x ){
        if( (b.z[x]&0xc0)!=0x80 ) fossil_print(" ");
        x++;
      }
      for(j=0; j<len; j++, x++){
        if( (b.z[x]&0xc0)!=0x80 ) fossil_print("%d",i);
      }
    }
  }
  if( x ) fossil_print("\n");
}

/*
** Minimum of two values
*/
static int minInt(int a, int b){ return a<b ? a : b; }



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
  void (*xReplace)(DiffBuilder*,const DLine*,const DLine*);
  void (*xEdit)(DiffBuilder*,const DLine*,const DLine*);
  void (*xEnd)(DiffBuilder*);
  unsigned int lnLeft;              /* Lines seen on the left (delete) side */
  unsigned int lnRight;             /* Lines seen on the right (insert) side */
  unsigned int nPending;            /* Number of pending lines */
  int eState;                       /* State of the output */
  int width;                        /* Display width */
  Blob *pOut;                       /* Output blob */
  Blob aCol[5];                     /* Holding blobs */
  DiffConfig *pCfg;                 /* Configuration information */
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
  blob_appendf(p->pOut, "DELETE  %8u          %.*s\n",
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
  LineChange chng;
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut, "EDIT    %8u          %.*s\n",
               p->lnLeft, (int)pX->n, pX->z);
  oneLineChange(pX, pY, &chng);
  for(i=x=0; i<chng.n; i++){
    int ofst = chng.a[i].iStart1;
    int len = chng.a[i].iLen1;
    if( len ){
      char c = '0' + i;
      if( x==0 ){ blob_appendf(p->pOut, "%*s", 26, ""); }
      while( ofst > x ){
        if( (pX->z[x]&0xc0)!=0x80 ) blob_append_char(p->pOut, ' ');
        x++;
      }
      for(j=0; j<len; j++, x++){
        if( (pX->z[x]&0xc0)!=0x80 ) blob_append_char(p->pOut, c);
      }
    }
  }
  if( x ) blob_append_char(p->pOut, '\n');
  blob_appendf(p->pOut, "                 %8u %.*s\n",
               p->lnRight, (int)pY->n, pY->z);
  for(i=x=0; i<chng.n; i++){
    int ofst = chng.a[i].iStart2;
    int len = chng.a[i].iLen2;
    if( len ){
      char c = '0' + i;
      if( x==0 ){ blob_appendf(p->pOut, "%*s", 26, ""); }
      while( ofst > x ){
        if( (pY->z[x]&0xc0)!=0x80 ) blob_append_char(p->pOut, ' ');
        x++;
      }
      for(j=0; j<len; j++, x++){
        if( (pY->z[x]&0xc0)!=0x80 ) blob_append_char(p->pOut, c);
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
  blob_append_tcl_literal(p->pOut, pLine->z, pLine->n);
  blob_append_char(p->pOut, '\n');
}
static void dftclInsert(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "INS ", -1);
  blob_append_tcl_literal(p->pOut, pLine->z, pLine->n);
  blob_append_char(p->pOut, '\n');
}
static void dftclDelete(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "DEL ", -1);
  blob_append_tcl_literal(p->pOut, pLine->z, pLine->n);
  blob_append_char(p->pOut, '\n');
}
static void dftclReplace(DiffBuilder *p, const DLine *pX, const DLine *pY){
  blob_append(p->pOut, "EDIT \"\" ", -1);
  blob_append_tcl_literal(p->pOut, pX->z, pX->n);
  blob_append_char(p->pOut, ' ');
  blob_append_tcl_literal(p->pOut, pY->z, pY->n);
  blob_append_char(p->pOut, '\n');
}
static void dftclEdit(DiffBuilder *p, const DLine *pX, const DLine *pY){
  int i, x;
  LineChange chng;
  blob_append(p->pOut, "EDIT", 4);
  oneLineChange(pX, pY, &chng);
  for(i=x=0; i<chng.n; i++){
    blob_append_char(p->pOut, ' ');
    blob_append_tcl_literal(p->pOut, pX->z + x, chng.a[i].iStart1 - x);
    x = chng.a[i].iStart1;
    blob_append_char(p->pOut, ' ');
    blob_append_tcl_literal(p->pOut, pX->z + x, chng.a[i].iLen1);
    x += chng.a[i].iLen1;
    blob_append_char(p->pOut, ' ');
    blob_append_tcl_literal(p->pOut,
                         pY->z + chng.a[i].iStart2, chng.a[i].iLen2);
  }
  if( x<pX->n ){
    blob_append_char(p->pOut, ' ');
    blob_append_tcl_literal(p->pOut, pX->z + x, pX->n - x);
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
**   2  STRING     COMMON      The line shown by STRING is in both files
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
  blob_append_json_literal(p->pOut, pLine->z, (int)pLine->n);
  blob_append(p->pOut, ",\n",2);
}
static void dfjsonInsert(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "3,",2);
  blob_append_json_literal(p->pOut, pLine->z, (int)pLine->n);
  blob_append(p->pOut, ",\n",2);
}
static void dfjsonDelete(DiffBuilder *p, const DLine *pLine){
  blob_append(p->pOut, "4,",2);
  blob_append_json_literal(p->pOut, pLine->z, (int)pLine->n);
  blob_append(p->pOut, ",\n",2);
}
static void dfjsonReplace(DiffBuilder *p, const DLine *pX, const DLine *pY){
  blob_append(p->pOut, "5,[\"\",",-1);
  blob_append_json_literal(p->pOut, pX->z, (int)pX->n);
  blob_append(p->pOut, ",",1);
  blob_append_json_literal(p->pOut, pY->z, (int)pY->n);
  blob_append(p->pOut, ",\"\"],\n",-1);
}
static void dfjsonEdit(DiffBuilder *p, const DLine *pX, const DLine *pY){
  int i, x;
  LineChange chng;
  blob_append(p->pOut, "5,[", 3);
  oneLineChange(pX, pY, &chng);
  for(i=x=0; i<chng.n; i++){
    if(i>0){
      blob_append_char(p->pOut, ',');
    }
    blob_append_json_literal(p->pOut, pX->z + x, chng.a[i].iStart1 - x);
    x = chng.a[i].iStart1;
    blob_append_char(p->pOut, ',');
    blob_append_json_literal(p->pOut, pX->z + x, chng.a[i].iLen1);
    x += chng.a[i].iLen1;
    blob_append_char(p->pOut, ',');
    blob_append_json_literal(p->pOut,
                         pY->z + chng.a[i].iStart2, chng.a[i].iLen2);
  }
  blob_append_char(p->pOut, ',');
  blob_append_json_literal(p->pOut, pX->z + x, pX->n - x);
  blob_append(p->pOut, "],\n",3);
}
static void dfjsonEnd(DiffBuilder *p){
  blob_append(p->pOut, "0]}", 3);
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
  blob_appendf(p->pOut,"<tr id=\"chunk%d\" class=\"diffchunk\">"
                       "<td class=\"diffln difflnl\"><pre>\n", ++nChunk);
}
static void dfunifiedSkip(DiffBuilder *p, unsigned int n, int isFinal){
  dfunifiedFinishRow(p);
  if( p->pCfg && p->pCfg->zLeftHash ){
    blob_appendf(p->pOut,
       "<tr class=\"diffskip\" data-startln=\"%d\" data-endln=\"%d\""
       " id=\"skip%xh%xi%x\">\n",
       p->lnLeft+1, p->lnLeft+n,
       nChunk, p->lnLeft, n);
  }else{
    blob_append(p->pOut, "<tr>", 4);
  }
  blob_append(p->pOut, "<td class=\"diffln difflne\">"
                       "&#xfe19;</td><td></td><td></td></tr>\n", -1);
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
  LineChange chng;
  oneLineChange(pX, pY, &chng);
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

  for(i=x=0; i<chng.n; i++){
    int ofst = chng.a[i].iStart1;
    int len = chng.a[i].iLen1;
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
  for(i=x=0; i<chng.n; i++){
    int ofst = chng.a[i].iStart2;
    int len = chng.a[i].iLen2;
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
static DiffBuilder *dfunifiedNew(Blob *pOut, DiffConfig *pCfg){
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
  if( pCfg->zLeftHash ){
    blob_appendf(pOut, "<table class=\"diff udiff\" data-lefthash=\"%s\">\n",
                pCfg->zLeftHash);
  }else{
    blob_append(pOut, "<table class=\"diff udiff\">\n", -1);
  }
  blob_init(&p->aCol[0], 0, 0);
  blob_init(&p->aCol[1], 0, 0);
  blob_init(&p->aCol[2], 0, 0);
  blob_init(&p->aCol[3], 0, 0);
  blob_init(&p->aCol[4], 0, 0);
  p->pCfg = pCfg;
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
  blob_appendf(p->pOut,"<tr id=\"chunk%d\" class=\"diffchunk\">"
                       "<td class=\"diffln difflnl\"><pre>\n", ++nChunk);
  p->eState = 0;
}
static void dfsplitSkip(DiffBuilder *p, unsigned int n, int isFinal){
  dfsplitFinishRow(p);
  if( p->pCfg && p->pCfg->zLeftHash ){
    blob_appendf(p->pOut,
       "<tr class=\"diffskip\" data-startln=\"%d\" data-endln=\"%d\""
       " id=\"skip%xh%xi%x\">\n",
       p->lnLeft+1, p->lnLeft+n,
       nChunk,p->lnLeft,n);
  }else{
    blob_append(p->pOut, "<tr>", 4);
  }
  blob_append(p->pOut,
       "<td class=\"diffln difflnl difflne\">&#xfe19;</td>"
       "<td></td><td></td>"
       "<td class=\"diffln difflnr difflne\">&#xfe19;</td>"
       "<td/td></tr>\n", -1);
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
  LineChange chng;
  oneLineChange(pX, pY, &chng);
  dfsplitStartRow(p);
  dfsplitChangeState(p, 3);
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%d\n", p->lnLeft);
  for(i=x=0; i<chng.n; i++){
    int ofst = chng.a[i].iStart1;
    int len = chng.a[i].iLen1;
    if( len ){
      htmlize_to_blob(&p->aCol[0], pX->z+x, ofst - x);
      x = ofst;
      if( chng.a[i].iLen2 ){
        blob_append(&p->aCol[0], "<del class='edit'>", -1);
      }else{
        blob_append(&p->aCol[0], "<del>", 5);
      }
      htmlize_to_blob(&p->aCol[0], pX->z+x, len);
      x += len;
      blob_append(&p->aCol[0], "</del>", 6);
    }
  }
  htmlize_to_blob(&p->aCol[0], pX->z+x,  pX->n - x);
  blob_append_char(&p->aCol[0], '\n');

  blob_append(&p->aCol[1], "|\n", 2);

  blob_appendf(&p->aCol[2],"%d\n", p->lnRight);
  for(i=x=0; i<chng.n; i++){
    int ofst = chng.a[i].iStart2;
    int len = chng.a[i].iLen2;
    if( len ){
      htmlize_to_blob(&p->aCol[3], pY->z+x, ofst - x);
      x = ofst;
      if( chng.a[i].iLen1 ){
        blob_append(&p->aCol[3], "<ins class='edit'>", -1);
      }else{
        blob_append(&p->aCol[3], "<ins>", 5);
      }
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
static DiffBuilder *dfsplitNew(Blob *pOut, DiffConfig *pCfg){
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
  if( pCfg->zLeftHash ){
    blob_appendf(pOut,
      "<table class=\"diff splitdiff\" data-lefthash=\"%s\">\n",
      pCfg->zLeftHash);
  }else{
    blob_append(pOut, "<table class=\"diff splitdiff\">\n", -1);
  }
  blob_init(&p->aCol[0], 0, 0);
  blob_init(&p->aCol[1], 0, 0);
  blob_init(&p->aCol[2], 0, 0);
  blob_init(&p->aCol[3], 0, 0);
  blob_init(&p->aCol[4], 0, 0);
  p->pCfg = pCfg;
  return p;
}

/************************* DiffBuilderSbs  ******************************/
/* This formatter creates a side-by-side diff in text.
*/
static void dfsbsSkip(DiffBuilder *p, unsigned int n, int isFinal){
  if( (p->lnLeft || p->lnRight) && !isFinal ){
    blob_appendf(p->pOut, "%.*c\n", p->width*2 + 16, '.');
  }
  p->lnLeft += n;
  p->lnRight += n;
}

/*
** Append at least iMin characters (not bytes) and at most iMax characters
** from pX onto the into of p.
**
** This comment contains multibyte unicode characters (ü, Æ, ð) in order
** to test the ability of the diff code to handle such characters.
*/
static void sbs_append_chars(Blob *p, int iMin, int iMax, const DLine *pX){
  int i;
  const char *z = pX->z;
  for(i=0; i<iMax && i<pX->n; i++){
    char c = z[i];
    blob_append_char(p, c);
    if( (c&0xc0)==0x80 ){ iMin++; iMax++; }
  }
  while( i<iMin ){
    blob_append_char(p, ' ');
    i++;
  }
}

static void dfsbsCommon(DiffBuilder *p, const DLine *pLine){
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%6u ", p->lnLeft);
  sbs_append_chars(p->pOut, p->width, p->width, pLine);
  blob_appendf(p->pOut,"   %6u ", p->lnRight);
  sbs_append_chars(p->pOut, 0, p->width, pLine);
  blob_append_char(p->pOut, '\n');
}
static void dfsbsInsert(DiffBuilder *p, const DLine *pLine){
  p->lnRight++;
  blob_appendf(p->pOut,"%6s %*s > %6u ",
     "", p->width, "", p->lnRight);
  sbs_append_chars(p->pOut, 0, p->width, pLine);
  blob_append_char(p->pOut, '\n');
}
static void dfsbsDelete(DiffBuilder *p, const DLine *pLine){
  p->lnLeft++;
  blob_appendf(p->pOut,"%6u ", p->lnLeft);
  sbs_append_chars(p->pOut, p->width, p->width, pLine);
  blob_append(p->pOut," <\n", 3);
}
static void dfsbsEdit(DiffBuilder *p, const DLine *pX, const DLine *pY){
  p->lnLeft++;
  p->lnRight++;
  blob_appendf(p->pOut,"%6u ", p->lnLeft);
  sbs_append_chars(p->pOut, p->width, p->width, pX);
  blob_appendf(p->pOut, " | %6u ", p->lnRight);
  sbs_append_chars(p->pOut, 0, p->width, pY);
  blob_append_char(p->pOut, '\n');
}
static void dfsbsEnd(DiffBuilder *p){
  fossil_free(p);
}
static DiffBuilder *dfsbsNew(Blob *pOut, DiffConfig *pCfg){
  DiffBuilder *p = fossil_malloc(sizeof(*p));
  p->xSkip = dfsbsSkip;
  p->xCommon = dfsbsCommon;
  p->xInsert = dfsbsInsert;
  p->xDelete = dfsbsDelete;
  p->xReplace = dfsbsEdit;
  p->xEdit = dfsbsEdit;
  p->xEnd = dfsbsEnd;
  p->lnLeft = p->lnRight = 0;
  p->width = diff_width(pCfg);
  p->pOut = pOut;
  return p;
}
/****************************************************************************/
/*
** Return the number between 0 and 100 that is smaller the closer pA and
** pB match.  Return 0 for a perfect match.  Return 100 if pA and pB are
** completely different.
**
** The current algorithm is as follows:
**
** (1) Remove leading and trailing whitespace.
** (2) Truncate both strings to at most 250 characters
** (3) If the two strings have a common prefix, measure that prefix
** (4) Find the length of the longest common subsequence that is
**     at least 150% longer than the common prefix.
** (5) Longer common subsequences yield lower scores.
*/
static int match_dline(DLine *pA, DLine *pB){
  const char *zA;            /* Left string */
  const char *zB;            /* right string */
  int nA;                    /* Bytes in zA[] */
  int nB;                    /* Bytes in zB[] */
  int nMin;
  int nPrefix;
  int avg;                   /* Average length of A and B */
  int i, j, k;               /* Loop counters */
  int best = 0;              /* Longest match found so far */
  int score;                 /* Final score.  0..100 */
  unsigned char c;           /* Character being examined */
  unsigned char aFirst[256]; /* aFirst[X] = index in zB[] of first char X */
  unsigned char aNext[252];  /* aNext[i] = index in zB[] of next zB[i] char */

  zA = pA->z;
  if( pA->nw==0 && pA->n ){
    for(i=0; i<pA->n && diff_isspace(zA[i]); i++){}
    pA->indent = i;
    for(j=pA->n-1; j>i && diff_isspace(zA[j]); j--){}
    pA->nw = j - i + 1;
  }
  zA += pA->indent;
  nA = pA->nw;

  zB = pB->z;
  if( pB->nw==0 && pB->n ){
    for(i=0; i<pB->n && diff_isspace(zB[i]); i++){}
    pB->indent = i;
    for(j=pB->n-1; j>i && diff_isspace(zB[j]); j--){}
    pB->nw = j - i + 1;
  }
  zB += pB->indent;
  nB = pB->nw;

  if( nA>250 ) nA = 250;
  if( nB>250 ) nB = 250;
  avg = (nA+nB)/2;
  if( avg==0 ) return 0;
  nMin = nA;
  if( nB<nMin ) nMin = nB;
  if( nMin==0 ) return 68;
  for(nPrefix=0; nPrefix<nMin && zA[nPrefix]==zB[nPrefix]; nPrefix++){}
  best = 0;
  if( nPrefix>5 && nPrefix>nMin/2 ){
    best = nPrefix*3/2;
    if( best>=avg - 2 ) best = avg - 2;
  }
  if( nA==nB && memcmp(zA, zB, nA)==0 ) return 0;
  memset(aFirst, 0xff, sizeof(aFirst));
  zA--; zB--;   /* Make both zA[] and zB[] 1-indexed */
  for(i=nB; i>0; i--){
    c = (unsigned char)zB[i];
    aNext[i] = aFirst[c];
    aFirst[c] = i;
  }
  for(i=1; i<=nA-best; i++){
    c = (unsigned char)zA[i];
    for(j=aFirst[c]; j<nB-best && memcmp(&zA[i],&zB[j],best)==0; j = aNext[j]){
      int limit = minInt(nA-i, nB-j);
      for(k=best; k<=limit && zA[k+i]==zB[k+j]; k++){}
      if( k>best ) best = k;
    }
  }
  score = 5 + ((best>=avg) ? 0 : (avg - best)*95/avg);

#if 0
  fprintf(stderr, "A: [%.*s]\nB: [%.*s]\nbest=%d avg=%d score=%d\n",
  nA, zA+1, nB, zB+1, best, avg, score);
#endif

  /* Return the result */
  return score;
}

/*
** COMMAND:  test-line-match
** Usage: %fossil test-line-match STRING1 STRING2
**
** Return a score from 0 to 100 that is how similar STRING1 is to
** STRING2.  Smaller numbers mean more similar.  0 is an exact match.
**
** This command is used to test to match_dline() function in the
** internal Fossil diff logic.
*/
void test_dline_match(void){
  DLine a, b;
  int x;
  if( g.argc!=4 ) usage("STRING1 STRING2");
  a.z = g.argv[2];
  a.n = (int)strlen(a.z);
  b.z = g.argv[3];
  b.n = (int)strlen(b.z);
  x = match_dline(&a, &b);
  fossil_print("%d\n", x);
}

/* Forward declarations for recursion */
static unsigned char *diffBlockAlignment(
  DLine *aLeft, int nLeft,     /* Text on the left */
  DLine *aRight, int nRight,   /* Text on the right */
  DiffConfig *pCfg,            /* Configuration options */
  int *pNResult                /* OUTPUT: Bytes of result */
);
static void longestCommonSequence(
  DContext *p,               /* Two files being compared */
  int iS1, int iE1,          /* Range of lines in p->aFrom[] */
  int iS2, int iE2,          /* Range of lines in p->aTo[] */
  int *piSX, int *piEX,      /* Write p->aFrom[] common segment here */
  int *piSY, int *piEY       /* Write p->aTo[] common segment here */
);

/*
** Make a copy of a list of nLine DLine objects from one array to
** another.  Hash the new array to ignore whitespace.
*/
static void diffDLineXfer(
  DLine *aTo,
  const DLine *aFrom,
  int nLine
){
  int i, j, k;
  u64 h, h2;
  for(i=0; i<nLine; i++) aTo[i].iHash = 0;
  for(i=0; i<nLine; i++){
    const char *z = aFrom[i].z;
    int n = aFrom[i].n;
    for(j=0; j<n && diff_isspace(z[j]); j++){}
    aTo[i].z = &z[j];
    for(k=aFrom[i].n; k>j && diff_isspace(z[k-1]); k--){}
    aTo[i].n = n = k-j;
    aTo[i].indent = 0;
    aTo[i].nw = 0;
    for(h=0; j<k; j++){
      char c = z[j];
      if( !diff_isspace(c) ){
        h = (h^c)*9000000000000000041LL;
      }
    }
    aTo[i].h = h = ((h%281474976710597LL)<<LENGTH_MASK_SZ) | n;
    h2 = h % nLine;
    aTo[i].iNext = aTo[h2].iHash;
    aTo[h2].iHash = i+1;
  }
}


/*
** For a difficult diff-block alignment that was originally for
** the default consider-all-whitespace algorithm, try to find the
** longest common subsequence between the two blocks that involves
** only whitespace changes.
*/
static unsigned char *diffBlockAlignmentIgnoreSpace(
  DLine *aLeft, int nLeft,     /* Text on the left */
  DLine *aRight, int nRight,   /* Text on the right */
  DiffConfig *pCfg,            /* Configuration options */
  int *pNResult                /* OUTPUT: Bytes of result */
){
  DContext dc;
  int iSX, iEX;                /* Start and end of LCS on the left */
  int iSY, iEY;                /* Start and end of the LCS on the right */
  unsigned char *a1, *a2;
  int n1, n2, nLCS;

  dc.aEdit = 0;
  dc.nEdit = 0;
  dc.nEditAlloc = 0;
  dc.nFrom = nLeft;
  dc.nTo = nRight;
  dc.xDiffer = compare_dline_ignore_allws;
  dc.aFrom = fossil_malloc( sizeof(DLine)*(nLeft+nRight) );
  dc.aTo = &dc.aFrom[dc.nFrom];
  diffDLineXfer(dc.aFrom, aLeft, nLeft);
  diffDLineXfer(dc.aTo, aRight, nRight);
  longestCommonSequence(&dc,0,nLeft,0,nRight,&iSX,&iEX,&iSY,&iEY);
  fossil_free(dc.aFrom);
  nLCS = iEX - iSX;
  if( nLCS<5 ) return 0;   /* No good LCS was found */

  if( pCfg->diffFlags & DIFF_DEBUG ){
     fossil_print("   LCS size=%d\n"
                  "     [%.*s]\n"
                  "     [%.*s]\n",
                  nLCS, aLeft[iSX].n, aLeft[iSX].z,
                  aLeft[iEX-1].n, aLeft[iEX-1].z);
  }

  a1 = diffBlockAlignment(aLeft,iSX,aRight,iSY,pCfg,&n1);
  a2 = diffBlockAlignment(aLeft+iEX, nLeft-iEX,
                          aRight+iEY, nRight-iEY,
                          pCfg, &n2);
  a1 = fossil_realloc(a1, n1+nLCS+n2);
  memcpy(a1+n1+nLCS,a2,n2);
  memset(a1+n1,3,nLCS);
  fossil_free(a2);
  *pNResult = n1+n2+nLCS;
  return a1;
}


/*
** This is a helper route for diffBlockAlignment().  In this case,
** a very large block is encountered that might be too expensive to
** use the O(N*N) Wagner edit distance algorithm.  So instead, this
** block implements a less-precise but faster O(N*logN) divide-and-conquer
** approach.
*/
static unsigned char *diffBlockAlignmentDivideAndConquer(
  DLine *aLeft, int nLeft,     /* Text on the left */
  DLine *aRight, int nRight,   /* Text on the right */
  DiffConfig *pCfg,            /* Configuration options */
  int *pNResult                /* OUTPUT: Bytes of result */
){
  DLine *aSmall;               /* The smaller of aLeft and aRight */
  DLine *aBig;                 /* The larger of aLeft and aRight */
  int nSmall, nBig;            /* Size of aSmall and aBig.  nSmall<=nBig */
  int iDivSmall, iDivBig;      /* Divider point for aSmall and aBig */
  int iDivLeft, iDivRight;     /* Divider point for aLeft and aRight */
  unsigned char *a1, *a2;      /* Results of the alignments on two halves */
  int n1, n2;                  /* Number of entries in a1 and a2 */
  int score, bestScore;        /* Score and best score seen so far */
  int i;                       /* Loop counter */

  if( nLeft>nRight ){
    aSmall = aRight;
    nSmall = nRight;
    aBig = aLeft;
    nBig = nLeft;
  }else{
    aSmall = aLeft;
    nSmall = nLeft;
    aBig = aRight;
    nBig = nRight;
  }
  iDivBig = nBig/2;
  iDivSmall = nSmall/2;

  if( pCfg->diffFlags & DIFF_DEBUG ){
    fossil_print("  Divide at [%.*s]\n",
                 aBig[iDivBig].n, aBig[iDivBig].z);
  }

  bestScore = 10000;
  for(i=0; i<nSmall; i++){
    score = match_dline(aBig+iDivBig, aSmall+i) + abs(i-nSmall/2)*2;
    if( score<bestScore ){
      bestScore = score;
      iDivSmall = i;
    }
  }
  if( aSmall==aRight ){
    iDivRight = iDivSmall;
    iDivLeft = iDivBig;
  }else{
    iDivRight = iDivBig;
    iDivLeft = iDivSmall;
  }
  a1 = diffBlockAlignment(aLeft,iDivLeft,aRight,iDivRight,pCfg,&n1);
  a2 = diffBlockAlignment(aLeft+iDivLeft, nLeft-iDivLeft,
                          aRight+iDivRight, nRight-iDivRight,
                          pCfg, &n2);
  a1 = fossil_realloc(a1, n1+n2 );
  memcpy(a1+n1,a2,n2);
  fossil_free(a2);
  *pNResult = n1+n2;
  return a1;
}


/*
** The threshold at which diffBlockAlignment transitions from the
** O(N*N) Wagner minimum-edit-distance algorithm to a less process
** O(NlogN) divide-and-conquer approach.
*/
#define DIFF_ALIGN_MX  1225

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
** The length of the returned array will be at most nLeft+nRight bytes.
** If the first bytes is 4,  that means we could not compute reasonable
** alignment between the two blocks.
**
** Algorithm:  Wagner's minimum edit-distance algorithm, modified by
** adding a cost to each match based on how well the two rows match
** each other.  Insertion and deletion costs are 50.  Match costs
** are between 0 and 100 where 0 is a perfect match 100 is a complete
** mismatch.
*/
static unsigned char *diffBlockAlignment(
  DLine *aLeft, int nLeft,     /* Text on the left */
  DLine *aRight, int nRight,   /* Text on the right */
  DiffConfig *pCfg,            /* Configuration options */
  int *pNResult                /* OUTPUT: Bytes of result */
){
  int i, j, k;                 /* Loop counters */
  int *a;                      /* One row of the Wagner matrix */
  int *pToFree;                /* Space that needs to be freed */
  unsigned char *aM;           /* Wagner result matrix */
  int aBuf[100];               /* Stack space for a[] if nRight not to big */

  if( nLeft==0 ){
    aM = fossil_malloc( nRight + 2 );
    memset(aM, 2, nRight);
    *pNResult = nRight;
    return aM;
  }
  if( nRight==0 ){
    aM = fossil_malloc( nLeft + 2 );
    memset(aM, 1, nLeft);
    *pNResult = nLeft;
    return aM;
  }

  if( pCfg->diffFlags & DIFF_DEBUG ){
    fossil_print("BlockAlignment:\n   [%.*s] + %d\n   [%.*s] + %d\n",
                 aLeft[0].n, aLeft[0].z, nLeft,
                 aRight[0].n, aRight[0].z, nRight);
  }

  /* For large alignments, try to use alternative algorithms that are
  ** faster than the O(N*N) Wagner edit distance.
  */
  if( (i64)nLeft*(i64)nRight>DIFF_ALIGN_MX
   && (pCfg->diffFlags & DIFF_SLOW_SBS)==0
  ){
    if( (pCfg->diffFlags & DIFF_IGNORE_ALLWS)==0 ){
      unsigned char *aRes;
      aRes = diffBlockAlignmentIgnoreSpace(
                 aLeft, nLeft,aRight, nRight,pCfg,pNResult);
      if( aRes ) return aRes;
    }
    return diffBlockAlignmentDivideAndConquer(
                 aLeft, nLeft,aRight, nRight,pCfg,pNResult);
  }

  /* If we reach this point, we will be doing an O(N*N) Wagner minimum
  ** edit distance to compute the alignment.
  */
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
        if( (score<=90 || (i<j+1 && i>j-1)) && m>p+score ){
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
  while( i+j>0 ){
    unsigned char c = aM[k];
    if( c>=3 ){
      assert( i>0 && j>0 );
      i--;
      j--;
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
  *pNResult = i;

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
static int smallGap(const int *R, int ma, int mb){
  int m = R[3];
  ma += R[4] + m;
  mb += R[5] + m;
  if( ma*mb>DIFF_ALIGN_MX ) return 0;
  return m<=2 || m<=(R[1]+R[2]+R[4]+R[5])/8;
}

/*
** Format a diff using a DiffBuilder object
*/
static void formatDiff(
  DContext *p,           /* The computed diff */
  DiffConfig *pCfg,      /* Configuration options */
  DiffBuilder *pBuilder  /* The formatter object */
){
  DLine *A;              /* Left side of the diff */
  DLine *B;              /* Right side of the diff */
  unsigned int a = 0;    /* Index of next line in A[] */
  unsigned int b = 0;    /* Index of next line in B[] */
  const int *R;          /* Array of COPY/DELETE/INSERT triples */
  unsigned int r;        /* Index into R[] */
  unsigned int nr;       /* Number of COPY/DELETE/INSERT triples to process */
  unsigned int mxr;      /* Maximum value for r */
  unsigned int i, j;     /* Loop counters */
  unsigned int m, ma, mb;/* Number of lines to output */
  signed int skip = 0;   /* Number of lines to skip */
  unsigned int nContext; /* Lines of context above and below each change */

  nContext = diff_context_lines(pCfg);
  A = p->aFrom;
  B = p->aTo;
  R = p->aEdit;
  mxr = p->nEdit;
  while( mxr>2 && R[mxr-1]==0 && R[mxr-2]==0 ){ mxr -= 3; }

  for(r=0; r<mxr; r += 3*nr){
    /* Figure out how many triples to show in a single block */
    for(nr=1; 3*nr<mxr && R[r+nr*3]>0 && R[r+nr*3]<(int)nContext*2; nr++){}

    /* If there is a regex, skip this block (generate no diff output)
    ** if the regex matches or does not match both insert and delete.
    ** Only display the block if one side matches but the other side does
    ** not.
    */
    if( pCfg->pRe ){
      int hideBlock = 1;
      int xa = a, xb = b;
      for(i=0; hideBlock && i<nr; i++){
        int c1, c2;
        xa += R[r+i*3];
        xb += R[r+i*3];
        c1 = re_dline_match(pCfg->pRe, &A[xa], R[r+i*3+1]);
        c2 = re_dline_match(pCfg->pRe, &B[xb], R[r+i*3+2]);
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
    if( R[r]>(int)nContext ){
      skip = R[r] - nContext;
    }else{
      skip = 0;
    }
    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    if( r ) skip -= nContext;
    if( skip>0 ){
      if( skip<(int)nContext ){
        /* If the amount to skip is less that the context band, then
        ** go ahead and show the skip band as it is not worth eliding */
        for(j=0; (int)j<skip; j++){
          pBuilder->xCommon(pBuilder, &A[a+j-skip]);
        }
      }else{
        pBuilder->xSkip(pBuilder, skip, 0);
      }
    }
    for(j=0; j<m; j++){
      pBuilder->xCommon(pBuilder, &A[a+j]);
    }
    a += m;
    b += m;

    /* Show the differences */
    for(i=0; i<nr; i++){
      int nAlign;
      unsigned char *alignment;
      ma = R[r+i*3+1];   /* Lines on left but not on right */
      mb = R[r+i*3+2];   /* Lines on right but not on left */

      /* Try merging the current block with subsequent blocks, if the
      ** subsequent blocks are nearby and there result isn't too big.
      */
      while( i<nr-1 && smallGap(&R[r+i*3],ma,mb) ){
        i++;
        m = R[r+i*3];
        ma += R[r+i*3+1] + m;
        mb += R[r+i*3+2] + m;
      }

      /* Try to find an alignment for the lines within this one block */
      alignment = diffBlockAlignment(&A[a], ma, &B[b], mb, pCfg, &nAlign);

      for(j=0; ma+mb>0; j++){
        assert( (int)j<nAlign );
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
            if( p->xDiffer(&A[a], &B[b])==0 ){
              pBuilder->xCommon(pBuilder, &A[a]);
            }else{
              pBuilder->xEdit(pBuilder, &A[a], &B[b]);
            }
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
      assert( nAlign==(int)j );
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
  if( R[r]>(int)nContext ){
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
  int mid;                   /* Center of the chng */
  int iSXb, iSYb, iEXb, iEYb;   /* Best match so far */
  int iSXp, iSYp, iEXp, iEYp;   /* Previous match */
  sqlite3_int64 bestScore;      /* Best score so far */
  sqlite3_int64 score;          /* Score for current candidate LCS */
  int span;                     /* combined width of the input sequences */
  int cutoff = 4;            /* Max hash chain entries to follow */
  int nextCutoff = -1;       /* Value of cutoff for next iteration */

  span = (iE1 - iS1) + (iE2 - iS2);
  bestScore = -9223300000*(sqlite3_int64)1000000000;
  score = 0;
  iSXb = iSXp = iS1;
  iEXb = iEXp = iS1;
  iSYb = iSYp = iS2;
  iEYb = iEYp = iS2;
  mid = (iE1 + iS1)/2;
  do{
    nextCutoff = 0;
    for(i=iS1; i<iE1; i++){
      int limit = 0;
      j = p->aTo[p->aFrom[i].h % p->nTo].iHash;
      while( j>0
        && (j-1<iS2 || j>=iE2 || p->xDiffer(&p->aFrom[i], &p->aTo[j-1]))
      ){
        if( limit++ > cutoff ){
          j = 0;
          nextCutoff = cutoff*4;
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
  }while( iSXb==iEXb && nextCutoff && (cutoff=nextCutoff)<=64 );
  if( iSXb==iEXb && (sqlite3_int64)(iE1-iS1)*(iE2-iS2)<2500 ){
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
** A common subsequence between p->aFrom and p->aTo has been found.
** This routine tries to judge if the subsequence really is a valid
** match or rather is just an artifact of an indentation change.
**
** Return non-zero if the subsequence is valid.  Return zero if the
** subsequence seems likely to be an editing artifact and should be
** ignored.
**
** This routine is a heuristic optimization intended to give more
** intuitive diff results following an indentation change it code that
** is formatted similarly to C/C++, Javascript, Go, TCL, and similar
** languages that use {...} for nesting.  A correct diff is computed
** even if this routine always returns true (non-zero).  But sometimes
** a more intuitive diff can result if this routine returns false.
**
** The subsequences consists of the rows iSX through iEX-1 (inclusive)
** in p->aFrom[].  The total sequences is iS1 through iE1-1 (inclusive)
** of p->aFrom[].
**
** Example where this heuristic is useful, see the diff at
** https://www.sqlite.org/src/fdiff?v1=0e79dd15cbdb4f48&v2=33955a6fd874dd97
**
** See also discussion at https://fossil-scm.org/forum/forumpost/9ba3284295
**
** ALGORITHM (subject to change and refinement):
**
**    1.  If the subsequence is larger than 1/7th of the original span,
**        then consider it valid.  --> return 1
**
**    2.  If no lines of the subsequence contains more than one
**        non-whitespace character,  --> return 0
**
**    3.  If any line of the subsequence contains more than one non-whitespace
**        character and is unique across the entire sequence after ignoring
**        leading and trailing whitespace   --> return 1
**
**    4.  Otherwise, it is potentially an artifact of an indentation
**        change. --> return 0
*/
static int likelyNotIndentChngArtifact(
  DContext *p,     /* The complete diff context */
  int iS1,         /* Start of the main segment */
  int iSX,         /* Start of the subsequence */
  int iEX,         /* First row past the end of the subsequence */
  int iE1          /* First row past the end of the main segment */
){
  int i, j, n;

  /* Rule (1) */
  if( (iEX-iSX)*7 >= (iE1-iS1) ) return 1;

  /* Compute DLine.indent and DLine.nw for all lines of the subsequence.
  ** If no lines contain more than one non-whitespace character return
  ** 0 because the subsequence could be due to an indentation change.
  ** Rule (2).
  */
  n = 0;
  for(i=iSX; i<iEX; i++){
    DLine *pA = &p->aFrom[i];
    if( pA->nw==0 && pA->n ){
      const char *zA = pA->z;
      const int nn = pA->n;
      int ii, jj;
      for(ii=0; ii<nn && diff_isspace(zA[ii]); ii++){}
      pA->indent = ii;
      for(jj=nn-1; jj>ii && diff_isspace(zA[jj]); jj--){}
      pA->nw = jj - ii + 1;
    }
    if( pA->nw>1 ) n++;
  }
  if( n==0 ) return 0;

  /* Compute DLine.indent and DLine.nw for the entire sequence */
  for(i=iS1; i<iE1; i++){
    DLine *pA;
    if( i==iSX ){
      i = iEX;
      if( i>=iE1 ) break;
    }
    pA = &p->aFrom[i];
    if( pA->nw==0 && pA->n ){
      const char *zA = pA->z;
      const int nn = pA->n;
      int ii, jj;
      for(ii=0; ii<nn && diff_isspace(zA[ii]); ii++){}
      pA->indent = ii;
      for(jj=nn-1; jj>ii && diff_isspace(zA[jj]); jj--){}
      pA->nw = jj - ii + 1;
    }
  }

  /* Check to see if any subsequence line that has more than one
  ** non-whitespace character is unique across the entire sequence.
  ** Rule (3)
  */
  for(i=iSX; i<iEX; i++){
    const char *z = p->aFrom[i].z + p->aFrom[i].indent;
    const int nw = p->aFrom[i].nw;
    if( nw<=1 ) continue;
    for(j=iS1; j<iSX; j++){
      if( p->aFrom[j].nw!=nw ) continue;
      if( memcmp(p->aFrom[j].z+p->aFrom[j].indent,z,nw)==0 ) break;
    }
    if( j<iSX ) continue;
    for(j=iEX; j<iE1; j++){
      if( p->aFrom[j].nw!=nw ) continue;
      if( memcmp(p->aFrom[j].z+p->aFrom[j].indent,z,nw)==0 ) break;
    }
    if( j>=iE1 ) break;
  }
  return i<iEX;
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

  if( iEX>iSX+5
   || (iEX>iSX && likelyNotIndentChngArtifact(p,iS1,iSX,iEX,iE1) )
  ){
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
** appropriate default if no context width is specified. If pCfg is
** NULL then the compile-time default is used (which gets propagated
** to JS-side state by certain pages).
*/
int diff_context_lines(DiffConfig *pCfg){
  const int dflt = 5;
  if(pCfg!=0){
    int n = pCfg->nContext;
    if( n==0 && (pCfg->diffFlags & DIFF_CONTEXT_EX)==0 ) n = dflt;
    return n<0 ? 0x7ffffff : n;
  }else{
    return dflt;
  }
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
int diff_width(DiffConfig *pCfg){
  int w = pCfg->wColumn;
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
** The return vector is terminated by a triple of all zeros.  The caller
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
  DiffConfig *pCfg /* Configuration options */
){
  int ignoreWs; /* Ignore whitespace */
  DContext c;

  if( pCfg->diffFlags & DIFF_INVERT ){
    Blob *pTemp = pA_Blob;
    pA_Blob = pB_Blob;
    pB_Blob = pTemp;
  }
  ignoreWs = (pCfg->diffFlags & DIFF_IGNORE_ALLWS)!=0;
  blob_to_utf8_no_bom(pA_Blob, 0);
  blob_to_utf8_no_bom(pB_Blob, 0);

  /* Prepare the input files */
  memset(&c, 0, sizeof(c));
  if( (pCfg->diffFlags & DIFF_IGNORE_ALLWS)==DIFF_IGNORE_ALLWS ){
    c.xDiffer = compare_dline_ignore_allws;
  }else{
    c.xDiffer = compare_dline;
  }
  if( pCfg->diffFlags & DIFF_BY_TOKEN ){
    c.aFrom = break_into_tokens(blob_str(pA_Blob), blob_size(pA_Blob),
                               &c.nFrom, pCfg->diffFlags);
    c.aTo = break_into_tokens(blob_str(pB_Blob), blob_size(pB_Blob),
                             &c.nTo, pCfg->diffFlags);
  }else{
    c.aFrom = break_into_lines(blob_str(pA_Blob), blob_size(pA_Blob),
                               &c.nFrom, pCfg->diffFlags);
    c.aTo = break_into_lines(blob_str(pB_Blob), blob_size(pB_Blob),
                             &c.nTo, pCfg->diffFlags);
  }
  if( c.aFrom==0 || c.aTo==0 ){
    fossil_free(c.aFrom);
    fossil_free(c.aTo);
    if( pOut ){
      diff_errmsg(pOut, DIFF_CANNOT_COMPUTE_BINARY, pCfg->diffFlags);
    }
    return 0;
  }

  /* Compute the difference */
  diff_all(&c);
  if( ignoreWs && c.nEdit==6 && c.aEdit[1]==0 && c.aEdit[2]==0 ){
    fossil_free(c.aFrom);
    fossil_free(c.aTo);
    fossil_free(c.aEdit);
    if( pOut ) diff_errmsg(pOut, DIFF_WHITESPACE_ONLY, pCfg->diffFlags);
    return 0;
  }
  if( (pCfg->diffFlags & DIFF_NOTTOOBIG)!=0 ){
    int i, m, n;
    int *a = c.aEdit;
    int mx = c.nEdit;
    for(i=m=n=0; i<mx; i+=3){ m += a[i]; n += a[i+1]+a[i+2]; }
    if( n>10000 ){
      fossil_free(c.aFrom);
      fossil_free(c.aTo);
      fossil_free(c.aEdit);
      if( pOut ) diff_errmsg(pOut, DIFF_TOO_MANY_CHANGES, pCfg->diffFlags);
      return 0;
    }
  }
  if( (pCfg->diffFlags & DIFF_NOOPT)==0 ){
    diff_optimize(&c);
  }
  if( (pCfg->diffFlags & DIFF_BY_TOKEN)!=0 ){
    /* Convert token counts into byte counts. */
    int i;
    int iA = 0;
    int iB = 0;
    for(i=0; c.aEdit[i] || c.aEdit[i+1] || c.aEdit[i+2]; i+=3){
      int k, sum;
      for(k=0, sum=0; k<c.aEdit[i]; k++) sum += c.aFrom[iA++].n;
      iB += c.aEdit[i];
      c.aEdit[i] = sum;
      for(k=0, sum=0; k<c.aEdit[i+1]; k++) sum += c.aFrom[iA++].n;
      c.aEdit[i+1] = sum;
      for(k=0, sum=0; k<c.aEdit[i+2]; k++) sum += c.aTo[iB++].n;
      c.aEdit[i+2] = sum;
    }
  }

  if( pOut ){
    if( pCfg->diffFlags & DIFF_NUMSTAT ){
      int nDel = 0, nIns = 0, i;
      for(i=0; c.aEdit[i] || c.aEdit[i+1] || c.aEdit[i+2]; i+=3){
        nDel += c.aEdit[i+1];
        nIns += c.aEdit[i+2];
      }
      g.diffCnt[1] += nIns;
      g.diffCnt[2] += nDel;
      if( nIns+nDel ){
        g.diffCnt[0]++;
        if( !(pCfg->diffFlags & DIFF_BRIEF) ){
          blob_appendf(pOut, "%10d %10d", nIns, nDel);
        }
      }
    }else if( pCfg->diffFlags & (DIFF_RAW|DIFF_BY_TOKEN) ){
      const int *R = c.aEdit;
      unsigned int r;
      for(r=0; R[r] || R[r+1] || R[r+2]; r += 3){
        blob_appendf(pOut, " copy %6d  delete %6d  insert %6d\n",
                     R[r], R[r+1], R[r+2]);
      }
    }else if( pCfg->diffFlags & DIFF_JSON ){
      DiffBuilder *pBuilder = dfjsonNew(pOut);
      formatDiff(&c, pCfg, pBuilder);
      blob_append_char(pOut, '\n');
    }else if( pCfg->diffFlags & DIFF_TCL ){
      DiffBuilder *pBuilder = dftclNew(pOut);
      formatDiff(&c, pCfg, pBuilder);
    }else if( pCfg->diffFlags & DIFF_SIDEBYSIDE ){
      DiffBuilder *pBuilder;
      if( pCfg->diffFlags & DIFF_HTML ){
        pBuilder = dfsplitNew(pOut, pCfg);
      }else{
        pBuilder = dfsbsNew(pOut, pCfg);
      }
      formatDiff(&c, pCfg, pBuilder);
    }else if( pCfg->diffFlags & DIFF_DEBUG ){
      DiffBuilder *pBuilder = dfdebugNew(pOut);
      formatDiff(&c, pCfg, pBuilder);
    }else if( pCfg->diffFlags & DIFF_HTML ){
      DiffBuilder *pBuilder = dfunifiedNew(pOut, pCfg);
      formatDiff(&c, pCfg, pBuilder);
    }else{
      contextDiff(&c, pOut, pCfg);
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
** Initialize the DiffConfig object using command-line options.
**
** Process diff-related command-line options and return an appropriate
** "diffFlags" integer.
**
**   -b|--browser                 Show the diff output in a web-browser
**   --brief                      Show filenames only        DIFF_BRIEF
**   --by                         Shorthand for "--browser -y"
**   -c|--context N               N lines of context.        nContext
**   --dark                       Use dark mode for Tcl/Tk and HTML output
**   --html                       Format for HTML            DIFF_HTML
**   -i|--internal                Use built-in diff, not an external tool
**   --invert                     Invert the diff            DIFF_INVERT
**   --json                       Output formatted as JSON
**   --label NAME                 Column label.  Can be repeated once.
**   -n|--linenum                 Show line numbers          DIFF_LINENO
**   -N|--new-file                Alias for --verbose
**   --noopt                      Disable optimization       DIFF_NOOPT
**   --numstat                    Show change counts         DIFF_NUMSTAT
**   --strip-trailing-cr          Strip trailing CR          DIFF_STRIP_EOLCR
**   --tcl                        Tcl-formatted output used internally by --tk
**   --unified                    Unified diff.              ~DIFF_SIDEBYSIDE
**   -v|--verbose                 Show complete text of added or deleted files
**   -w|--ignore-all-space        Ignore all whitespaces     DIFF_IGNORE_ALLWS
**   --webpage                    Format output as a stand-alone HTML webpage
**   -W|--width N                 N character lines.         wColumn
**   -y|--side-by-side            Side-by-side diff.         DIFF_SIDEBYSIDE
**   -Z|--ignore-trailing-space   Ignore eol-whitespaces     DIFF_IGNORE_EOLWS
*/
void diff_options(DiffConfig *pCfg, int isGDiff, int bUnifiedTextOnly){
  u64 diffFlags = 0;
  const char *z;
  int f;

  memset(pCfg, 0, sizeof(*pCfg));
  if( find_option("ignore-trailing-space","Z",0)!=0 ){
    diffFlags = DIFF_IGNORE_EOLWS;
  }
  if( find_option("ignore-all-space","w",0)!=0 ){
    diffFlags = DIFF_IGNORE_ALLWS; /* stronger than DIFF_IGNORE_EOLWS */
  }
  if( find_option("strip-trailing-cr",0,0)!=0 ){
    diffFlags |= DIFF_STRIP_EOLCR;
  }
  if( !bUnifiedTextOnly ){
    if( find_option("side-by-side","y",0)!=0 ) diffFlags |= DIFF_SIDEBYSIDE;
    if( find_option("yy",0,0)!=0 ){
      diffFlags |= DIFF_SIDEBYSIDE | DIFF_SLOW_SBS;
    }
    if( find_option("html",0,0)!=0 ) diffFlags |= DIFF_HTML;
    if( find_option("unified",0,0)!=0 ) diffFlags &= ~DIFF_SIDEBYSIDE;
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
    if( find_option("bytoken",0,0)!=0 ){
      diffFlags = DIFF_RAW|DIFF_BY_TOKEN;
    }
  }
  if( (z = find_option("context","c",1))!=0 ){
    char *zEnd;
    f = (int)strtol(z, &zEnd, 10);
    if( zEnd[0]==0 && errno!=ERANGE ){
      pCfg->nContext = f;
      diffFlags |= DIFF_CONTEXT_EX;
    }
  }
  if( (z = find_option("width","W",1))!=0 && (f = atoi(z))>0 ){
    pCfg->wColumn = f;
  }
  pCfg->azLabel[0] = find_option("label",0,1);
  if( pCfg->azLabel[0] ){
    pCfg->azLabel[1] = find_option("label",0,1);
  }
  if( find_option("linenum","n",0)!=0 ) diffFlags |= DIFF_LINENO;
  if( find_option("noopt",0,0)!=0 ) diffFlags |= DIFF_NOOPT;
  if( find_option("numstat",0,0)!=0 ) diffFlags |= DIFF_NUMSTAT;
  if( find_option("versions","h",0)!=0 ) diffFlags |= DIFF_SHOW_VERS;
  if( find_option("dark",0,0)!=0 ) diffFlags |= DIFF_DARKMODE;
  if( find_option("invert",0,0)!=0 ) diffFlags |= DIFF_INVERT;
  if( find_option("brief",0,0)!=0 ) diffFlags |= DIFF_BRIEF;
  if( find_option("internal","i",0)==0
   && (diffFlags & (DIFF_HTML|DIFF_TCL|DIFF_DEBUG|DIFF_JSON))==0
  ){
    pCfg->zDiffCmd = find_option("command", 0, 1);
    if( pCfg->zDiffCmd==0 ) pCfg->zDiffCmd = diff_command_external(isGDiff);
    if( pCfg->zDiffCmd ){
      const char *zDiffBinary;
      pCfg->zBinGlob = diff_get_binary_glob();
      zDiffBinary = find_option("diff-binary", 0, 1);
      if( zDiffBinary ){
        if( is_truth(zDiffBinary) ) diffFlags |= DIFF_INCBINARY;
      }else if( db_get_boolean("diff-binary", 1) ){
        diffFlags |= DIFF_INCBINARY;
      }
    }else if( isGDiff) {
      /* No external gdiff command found, using --by */
      diffFlags |= DIFF_HTML|DIFF_WEBPAGE|DIFF_LINENO|DIFF_BROWSER
                     |DIFF_SIDEBYSIDE;
    }
  }
  if( find_option("verbose","v",0)!=0 ) diffFlags |= DIFF_VERBOSE;
  /* Deprecated, but retained for script compatibility. */
  else if( find_option("new-file","N",0)!=0 ) diffFlags |= DIFF_VERBOSE;

  pCfg->diffFlags = diffFlags;
}

/*
** COMMAND: test-diff
** COMMAND: xdiff
**
** Usage: %fossil xdiff [options] FILE1 FILE2
**
** Compute an "external diff" between two files. By "external diff" we mean
** a diff between two disk files that are not necessarily under management.
** In other words, this command provides a mechanism to use Fossil's file
** difference engine on arbitrary disk files.  See the "diff" command for
** computing differences between files that are under management.
**
** This command prints the differences between the two files FILE1 and FILE2.
** all of the usual diff formatting options (--tk, --by, -c N, etc.) apply.
** See the "diff" command for a full list of command-line options.
*/
void xdiff_cmd(void){
  Blob a, b, out;
  const char *zRe;           /* Regex filter for diff output */
  DiffConfig DCfg;

  if( find_option("tk",0,0)!=0 ){
    diff_tk("xdiff", 2);
    return;
  }
  find_option("i",0,0);
  find_option("v",0,0);
  diff_options(&DCfg, 0, 0);
  zRe = find_option("regexp","e",1);
  if( zRe ){
    const char *zErr = re_compile(&DCfg.pRe, zRe, 0);
    if( zErr ) fossil_fatal("regex error: %s", zErr);
  }
  verify_all_options();
  if( g.argc!=4 ) usage("FILE1 FILE2");
  blob_zero(&out);
  diff_begin(&DCfg);
  diff_print_filenames(g.argv[2], g.argv[3], &DCfg, &out);
  blob_read_from_file(&a, g.argv[2], ExtFILE);
  blob_read_from_file(&b, g.argv[3], ExtFILE);
  text_diff(&a, &b, &out, &DCfg);
  blob_write_to_file(&out, "-");
  diff_end(&DCfg, 0);
  re_free(DCfg.pRe);
}

/*
** COMMAND: fdiff
**
** Usage: %fossil fdiff [options] HASH1 HASH2
**
** Compute a diff between two artifacts in a repository (either a repository
** identified by the "-R FILENAME" option, or the repository that contains
** the working directory).
**
** All of the usual diff formatting options (--tk, --by, -c N, etc.) apply.
** See the "diff" command for a full list of command-line options.
*/
void fdiff_cmd(void){
  Blob a, b, out;
  const char *zRe;           /* Regex filter for diff output */
  int rid;
  DiffConfig DCfg;

  if( find_option("tk",0,0)!=0 ){
    diff_tk("fdiff", 2);
    return;
  }
  find_option("i",0,0);
  find_option("v",0,0);
  diff_options(&DCfg, 0, 0);
  zRe = find_option("regexp","e",1);
  if( zRe ){
    const char *zErr = re_compile(&DCfg.pRe, zRe, 0);
    if( zErr ) fossil_fatal("regex error: %s", zErr);
  }
  db_find_and_open_repository(0, 0);
  verify_all_options();
  if( g.argc!=4 ) usage("HASH1 HASH2");
  blob_zero(&out);
  diff_begin(&DCfg);
  diff_print_filenames(g.argv[2], g.argv[3], &DCfg, &out);
  rid = name_to_typed_rid(g.argv[2], 0);
  content_get(rid, &a);
  rid = name_to_typed_rid(g.argv[3], 0);
  content_get(rid, &b);
  text_diff(&a, &b, &out, &DCfg);
  blob_write_to_file(&out, "-");
  diff_end(&DCfg, 0);
  re_free(DCfg.pRe);
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
    p->c.xDiffer = compare_dline_ignore_allws;
  }else{
    p->c.xDiffer = compare_dline;
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
      mxTime =
        (sqlite3_int64)(current_time_in_milliseconds() + 1000.0*atof(zLimit));
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
      fossil_fatal("file %s does not exist in check-in %s",
                   zFilename, zRevision);
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
**    origin=ID           The origin check-in.  If unspecified, the root
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
  if( exclude_spiders(0) ) return;
  fossil_nice_default();
  zFilename = P("filename");
  zRevision = PD("checkin",0);
  zOrigin = P("origin");
  zLimit = P("limit");
  showLog = PB("log");
  fileVers = PB("filevers");
  ignoreWs = PB("w");
  if( ignoreWs ) annFlags |= DIFF_IGNORE_ALLWS;
  cgi_check_for_malice();

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
  @ <hr>
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
  zRevision = find_option("revision","r",1);
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
