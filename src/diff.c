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
#define DIFF_SIDEBYSIDE   ((u64)0x02000000) /* Generate a side-by-side diff */
#define DIFF_NEWFILE      ((u64)0x04000000) /* Missing shown as empty files */
#define DIFF_BRIEF        ((u64)0x08000000) /* Show filenames only */
#define DIFF_INLINE       ((u64)0x00000000) /* Inline (not side-by-side) diff */
#define DIFF_HTML         ((u64)0x10000000) /* Render for HTML */
#define DIFF_LINENO       ((u64)0x20000000) /* Show line numbers */
#define DIFF_WS_WARNING   ((u64)0x40000000) /* Warn about whitespace */
#define DIFF_NOOPT        (((u64)0x01)<<32) /* Suppress optimizations (debug) */
#define DIFF_INVERT       (((u64)0x02)<<32) /* Invert the diff (debug) */
#define DIFF_CONTEXT_EX   (((u64)0x04)<<32) /* Use context even if zero */
#define DIFF_NOTTOOBIG    (((u64)0x08)<<32) /* Only display if not too big */

/*
** These error messages are shared in multiple locations.  They are defined
** here for consistency.
*/
#define DIFF_CANNOT_COMPUTE_BINARY \
    "cannot compute difference between binary files\n"

#define DIFF_CANNOT_COMPUTE_SYMLINK \
    "cannot compute difference between symlink and regular file\n"

#define DIFF_TOO_MANY_CHANGES_TXT \
    "more than 10,000 changes\n"

#define DIFF_TOO_MANY_CHANGES_HTML \
    "<p class='generalError'>More than 10,000 changes</p>\n"

/*
** This macro is designed to return non-zero if the specified blob contains
** data that MAY be binary in nature; otherwise, zero will be returned.
*/
#define looks_like_binary(blob) ((looks_like_utf8(blob)&LOOK_BINARY)!=LOOK_NONE)

/*
** Output flags for the looks_like_utf8() and looks_like_utf16() routines used
** to convey status information about the blob content.
*/
#define LOOK_NONE    ((int)0x00000000) /* Nothing special was found. */
#define LOOK_NUL     ((int)0x00000001) /* One or more NUL chars were found. */
#define LOOK_CR      ((int)0x00000002) /* One or more CR chars were found. */
#define LOOK_LONE_CR ((int)0x00000004) /* An unpaired CR char was found. */
#define LOOK_LF      ((int)0x00000008) /* One or more LF chars were found. */
#define LOOK_LONE_LF ((int)0x00000010) /* An unpaired CR char was found. */
#define LOOK_CRLF    ((int)0x00000020) /* One or more CR/LF pairs were found. */
#define LOOK_LONG    ((int)0x00000040) /* An over length line was found. */
#define LOOK_ODD     ((int)0x00000080) /* An odd number of bytes was found. */
#define LOOK_SHORT   ((int)0x00000100) /* Unable to perform full check. */
#define LOOK_INVALID ((int)0x00000200) /* Invalid sequence was found. */
#define LOOK_BINARY  (LOOK_NUL | LOOK_LONG | LOOK_SHORT) /* May be binary. */
#endif /* INTERFACE */

/*
** Maximum length of a line in a text file, in bytes.  (2**13 = 8192 bytes)
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

  /* an array of DLine elements serves two purposes.  The fields
  ** above are one per line of input text.  But each entry is also
  ** a bucket in a hash table, as follows: */
  unsigned int iHash;   /* 1+(first entry in the hash chain) */
};

/*
** Length of a dline
*/
#define LENGTH(X)   ((X)->h & LENGTH_MASK)

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
**
** Profiling show that in most cases this routine consumes the bulk of
** the CPU time on a diff.
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
    for(h=0, x=0; x<=k; x++){
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
** This function attempts to scan each logical line within the blob to
** determine the type of content it appears to contain.  The return value
** is a combination of one or more of the LOOK_XXX flags (see above):
**
** !LOOK_BINARY -- The content appears to consist entirely of text; however,
**                 the encoding may not be UTF-8.
**
** LOOK_BINARY -- The content appears to be binary because it contains one
**                or more embedded NUL characters or an extremely long line.
**                Since this function does not understand UTF-16, it may
**                falsely consider UTF-16 text to be binary.
**
** Additional flags (i.e. those other than the ones included in LOOK_BINARY)
** may be present in the result as well; however, they should not impact the
** determination of text versus binary content.
**
************************************ WARNING **********************************
**
** This function does not validate that the blob content is properly formed
** UTF-8.  It assumes that all code points are the same size.  It does not
** validate any code points.  It makes no attempt to detect if any [invalid]
** switches between UTF-8 and other encodings occur.
**
** The only code points that this function cares about are the NUL character,
** carriage-return, and line-feed.
**
** Whether or not this function examines the entire contents of the blob is
** officially unspecified.
**
************************************ WARNING **********************************
*/
int looks_like_utf8(const Blob *pContent){
  const unsigned char *z = (unsigned char *) blob_buffer(pContent);
  unsigned int n = blob_size(pContent);
  unsigned char c;
  int j, flags = LOOK_NONE;  /* Assume UTF-8 text, prove otherwise */

  if( n==0 ) return flags;  /* Empty file -> text */
  c = *z;
  if( c==0 ){
    flags |= LOOK_NUL;  /* NUL character in a file -> binary */
  }else if( c=='\r' ){
    flags |= LOOK_CR;
    if( n<=1 || z[1]!='\n' ){
      flags |= LOOK_LONE_CR;  /* More chars, next char is not LF */
    }
  }
  j = (c!='\n');
  if( !j ) flags |= (LOOK_LF | LOOK_LONE_LF);  /* Found LF as first char */
  while( --n>0 ){
    unsigned char c2 = c;
    c = *++z; ++j;
    if( c2>=0x80 ){
      /*
      ** Checks for proper UTF-8. It uses the method described in:
      **   http://en.wikipedia.org/wiki/UTF-8#Invalid_byte_sequences
      ** except for the "overlong form" which is not considered
      ** invalid: Some languages like Java and Tcl use it.
      */
      if( (c2<0xC0) || (c2>=0xF8) ){
   	    flags |= LOOK_INVALID;  /* Invalid 1-byte or >4-byte UTF-8, continue */
      }else do{
        /* Check if all continuation bytes >=0x80 and <0xC0 */
        if( n<2 || ((c&0xC0)!=0x80) ){
          flags |= LOOK_INVALID; /* Invalid continuation byte, continue */
          break;
        }else{
          /* prepare for checking remaining continuation bytes. */
          c2<<=1; --n; ++j;
          c = *++z;
          /* c and c2 are modified here, but we know for sure that c2>=0x80
           * always (otherwise the loop would not be entered in the first
           * place and it would not continue), and all skipped bytes are
           * valid continuation bytes >= 0x80; they can never be equal to 0,
           * '\n' or '\r'. Therefore, this does not disturb the remaining
           * checks, it just saves us work. */
        }
      }while( c2>=0xC0 );
    }
    if( c==0 ){
      flags |= LOOK_NUL;  /* NUL character in a file -> binary */
    }else if( c=='\n' ){
      flags |= LOOK_LF;
      if( c2=='\r' ){
        flags |= (LOOK_CR | LOOK_CRLF);  /* Found LF preceded by CR */
      }else{
        flags |= LOOK_LONE_LF;
      }
      if( j>LENGTH_MASK ){
        flags |= LOOK_LONG;  /* Very long line -> binary */
      }
      j = 0;
    }else if( c=='\r' ){
      flags |= LOOK_CR;
      if( n<=1 || z[1]!='\n' ){
        flags |= LOOK_LONE_CR;  /* More chars, next char is not LF */
      }
    }
  }
  if( c>=0x80 ){
    /* Last byte must be ASCII, there are no continuation bytes. */
    flags |= LOOK_INVALID;
  }
  if( j>LENGTH_MASK ){
    flags |= LOOK_LONG;  /* Very long line -> binary */
  }
  return flags;
}

/*
** Define the type needed to represent a Unicode (UTF-16) character.
*/
#ifndef WCHAR_T
#  ifdef _WIN32
#    define WCHAR_T wchar_t
#  else
#    define WCHAR_T unsigned short
#  endif
#endif

/*
** Maximum length of a line in a text file, in UTF-16 characters.  (4096)
** The number of bytes represented by this value cannot exceed LENGTH_MASK
** bytes, because that is the line buffer size used by the diff engine.
*/
#define UTF16_LENGTH_MASK_SZ   (LENGTH_MASK_SZ-(sizeof(WCHAR_T)-sizeof(char)))
#define UTF16_LENGTH_MASK      ((1<<UTF16_LENGTH_MASK_SZ)-1)

/*
** This macro is used to swap the byte order of a UTF-16 character in the
** looks_like_utf16() function.
*/
#define UTF16_SWAP(ch)         ((((ch) << 8) & 0xFF00) | (((ch) >> 8) & 0xFF))
#define UTF16_SWAP_IF(expr,ch) ((expr) ? UTF16_SWAP((ch)) : (ch))

/*
** This function attempts to scan each logical line within the blob to
** determine the type of content it appears to contain.  The return value
** is a combination of one or more of the LOOK_XXX flags (see above):
**
** !LOOK_BINARY -- The content appears to consist entirely of text; however,
**                 the encoding may not be UTF-16.
**
** LOOK_BINARY -- The content appears to be binary because it contains one
**                or more embedded NUL characters or an extremely long line.
**                Since this function does not understand UTF-8, it may
**                falsely consider UTF-8 text to be binary.
**
** Additional flags (i.e. those other than the ones included in LOOK_BINARY)
** may be present in the result as well; however, they should not impact the
** determination of text versus binary content.
**
************************************ WARNING **********************************
**
** This function does not validate that the blob content is properly formed
** UTF-16.  It assumes that all code points are the same size.  It does not
** validate any code points.  It makes no attempt to detect if any [invalid]
** switches between the UTF-16be and UTF-16le encodings occur.
**
** The only code points that this function cares about are the NUL character,
** carriage-return, and line-feed.
**
** Whether or not this function examines the entire contents of the blob is
** officially unspecified.
**
************************************ WARNING **********************************
*/
int looks_like_utf16(const Blob *pContent, int bReverse){
  const WCHAR_T *z = (WCHAR_T *)blob_buffer(pContent);
  unsigned int n = blob_size(pContent);
  int j, c, flags = LOOK_NONE;  /* Assume UTF-16 text, prove otherwise */

  if( n==0 ) return flags;  /* Empty file -> text */
  if( n%sizeof(WCHAR_T) ){
    flags |= LOOK_ODD;  /* Odd number of bytes -> binary (UTF-8?) */
    if( n<sizeof(WCHAR_T) ) return flags;  /* One byte -> binary (UTF-8?) */
  }
  c = *z;
  if( bReverse ){
    c = UTF16_SWAP(c);
  }
  if( c==0 ){
    flags |= LOOK_NUL;  /* NUL character in a file -> binary */
  }else if( c=='\r' ){
    flags |= LOOK_CR;
    if( n<=sizeof(WCHAR_T) || UTF16_SWAP_IF(bReverse, z[1])!='\n' ){
      flags |= LOOK_LONE_CR;  /* More chars, next char is not LF */
    }
  }
  j = (c!='\n');
  if( !j ) flags |= (LOOK_LF | LOOK_LONE_LF);  /* Found LF as first char */
  while( 1 ){
    int c2 = c;
    n -= sizeof(WCHAR_T);
    if( n<sizeof(WCHAR_T) ) break;
    c = *++z;
    if( bReverse ){
      c = UTF16_SWAP(c);
    }
    ++j;
    if( c==0 ){
      flags |= LOOK_NUL;  /* NUL character in a file -> binary */
    }else if( c=='\n' ){
      flags |= LOOK_LF;
      if( c2=='\r' ){
        flags |= (LOOK_CR | LOOK_CRLF);  /* Found LF preceded by CR */
      }else{
        flags |= LOOK_LONE_LF;
      }
      if( j>UTF16_LENGTH_MASK ){
        flags |= LOOK_LONG;  /* Very long line -> binary */
      }
      j = 0;
    }else if( c=='\r' ){
      flags |= LOOK_CR;
      if( n<=sizeof(WCHAR_T) || UTF16_SWAP_IF(bReverse, z[1])!='\n' ){
        flags |= LOOK_LONE_CR;  /* More chars, next char is not LF */
      }
    }
  }
  if( j>UTF16_LENGTH_MASK ){
    flags |= LOOK_LONG;  /* Very long line -> binary */
  }
  return flags;
}

/*
** This function returns an array of bytes representing the byte-order-mark
** for UTF-8.
*/
const unsigned char *get_utf8_bom(int *pnByte){
  static const unsigned char bom[] = {
    0xEF, 0xBB, 0xBF, 0x00, 0x00, 0x00
  };
  if( pnByte ) *pnByte = 3;
  return bom;
}

/*
** This function returns non-zero if the blob starts with a UTF-8
** byte-order-mark (BOM).
*/
int starts_with_utf8_bom(const Blob *pContent, int *pnByte){
  const char *z = blob_buffer(pContent);
  int bomSize = 0;
  const unsigned char *bom = get_utf8_bom(&bomSize);

  if( pnByte ) *pnByte = bomSize;
  if( blob_size(pContent)<bomSize ) return 0;
  return memcmp(z, bom, bomSize)==0;
}

/*
** This function returns non-zero if the blob starts with a UTF-16
** byte-order-mark (BOM), either in the endianness of the machine
** or in reversed byte order. The UTF-32 BOM is ruled out by checking
** if the UTF-16 BOM is not immediately followed by (utf16) 0.
** pnByte and pbReverse are only set when the function returns 1.
*/
int starts_with_utf16_bom(
  const Blob *pContent, /* IN: Blob content to perform BOM detection on. */
  int *pnByte,          /* OUT: The number of bytes used for the BOM. */
  int *pbReverse        /* OUT: Non-zero for BOM in reverse byte-order. */
){
  const unsigned short *z = (unsigned short *)blob_buffer(pContent);
  int bomSize = sizeof(unsigned short);
  int size = blob_size(pContent);

  if( size<bomSize ) return 0;  /* No: cannot read BOM. */
  if( size>=(2*bomSize) && z[1]==0 ) return 0;  /* No: possible UTF-32. */
  if( z[0]==0xfffe ){
    if( pbReverse ) *pbReverse = 1;
  }else if( z[0]==0xfeff ){
    if( pbReverse ) *pbReverse = 0;
  }else{
    return 0; /* No: UTF-16 byte-order-mark not found. */
  }
  if( pnByte ) *pnByte = bomSize;
  return 1; /* Yes. */
}

/*
** Returns non-zero if the specified content could be valid UTF-16.
*/
int could_be_utf16(const Blob *pContent, int *pbReverse){
  return (blob_size(pContent) % sizeof(WCHAR_T) == 0) ?
      starts_with_utf16_bom(pContent, 0, pbReverse) : 0;
}

/*
** Return true if two DLine elements are identical.
*/
static int same_dline(DLine *pA, DLine *pB){
  return pA->h==pB->h && memcmp(pA->z,pB->z,pA->h & LENGTH_MASK)==0;
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
    char *zHtml;
    if( pRe && re_dline_match(pRe, pLine, 1)==0 ){
      cPrefix = ' ';
    }else if( cPrefix=='+' ){
      blob_append(pOut, "<span class=\"diffadd\">", -1);
    }else if( cPrefix=='-' ){
      blob_append(pOut, "<span class=\"diffrm\">", -1);
    }
    zHtml = htmlize(pLine->z, (pLine->h & LENGTH_MASK));
    blob_append(pOut, zHtml, -1);
    fossil_free(zHtml);
    if( cPrefix!=' ' ){
      blob_append(pOut, "</span>", -1);
    }
  }else{
    blob_append(pOut, pLine->z, pLine->h & LENGTH_MASK);
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
  int nChunk = 0;  /* Number of diff chunks seen so far */
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
        blob_appendf(pOut, "<a name=\"chunk%d\"></a>\n", nChunk);
      }else{
        blob_appendf(pOut, "%.80c\n", '.');
      }
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
          appendDiffLine(pOut, ' ', &B[b+j], html, 0);
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
      appendDiffLine(pOut, ' ', &B[b+j], html, 0);
    }
  }
}

/*
** Status of a single output line
*/
typedef struct SbsLine SbsLine;
struct SbsLine {
  char *zLine;             /* The output line under construction */
  int n;                   /* Index of next unused slot in the zLine[] */
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
** Flags for sbsWriteText()
*/
#define SBS_NEWLINE      0x0001   /* End with \n\000 */
#define SBS_PAD          0x0002   /* Pad output to width spaces */

/*
** Write up to width characters of pLine into p->zLine[].  Translate tabs into
** spaces.  Add a newline if SBS_NEWLINE is set.  Translate HTML characters
** if SBS_HTML is set.  Pad the rendering out width bytes if SBS_PAD is set.
**
** This comment contains multibyte unicode characters (ü, Æ, ð) in order
** to test the ability of the diff code to handle such characters.
*/
static void sbsWriteText(SbsLine *p, DLine *pLine, unsigned flags){
  int n = pLine->h & LENGTH_MASK;
  int i;   /* Number of input characters consumed */
  int j;   /* Number of output characters generated */
  int k;   /* Cursor position */
  int needEndSpan = 0;
  const char *zIn = pLine->z;
  char *z = &p->zLine[p->n];
  int w = p->width;
  int colorize = p->escHtml;
  if( colorize && p->pRe && re_dline_match(p->pRe, pLine, 1)==0 ){
    colorize = 0;
  }
  for(i=j=k=0; k<w && i<n; i++, k++){
    char c = zIn[i];
    if( colorize ){
      if( i==p->iStart ){
        int x = strlen(p->zStart);
        memcpy(z+j, p->zStart, x);
        j += x;
        needEndSpan = 1;
        if( p->iStart2 ){
          p->iStart = p->iStart2;
          p->zStart = p->zStart2;
          p->iStart2 = 0;
        }
      }else if( i==p->iEnd ){
        memcpy(z+j, "</span>", 7);
        j += 7;
        needEndSpan = 0;
        if( p->iEnd2 ){
          p->iEnd = p->iEnd2;
          p->iEnd2 = 0;
        }
      }
    }
    if( c=='\t' ){
      z[j++] = ' ';
      while( (k&7)!=7 && k<w ){ z[j++] = ' '; k++; }
    }else if( c=='\r' || c=='\f' ){
      z[j++] = ' ';
    }else if( c=='<' && p->escHtml ){
      memcpy(&z[j], "&lt;", 4);
      j += 4;
    }else if( c=='&' && p->escHtml ){
      memcpy(&z[j], "&amp;", 5);
      j += 5;
    }else if( c=='>' && p->escHtml ){
      memcpy(&z[j], "&gt;", 4);
      j += 4;
    }else if( c=='"' && p->escHtml ){
      memcpy(&z[j], "&quot;", 6);
      j += 6;
    }else{
      z[j++] = c;
      if( (c&0xc0)==0x80 ) k--;
    }
  }
  if( needEndSpan ){
    memcpy(&z[j], "</span>", 7);
    j += 7;
  }
  if( (flags & SBS_PAD)!=0 ){
    while( k<w ){ k++;  z[j++] = ' '; }
  }
  if( flags & SBS_NEWLINE ){
    z[j++] = '\n';
  }
  p->n += j;
}

/*
** Append a string to an SbSLine without coding, interpretation, or padding.
*/
static void sbsWrite(SbsLine *p, const char *zIn, int nIn){
  memcpy(p->zLine+p->n, zIn, nIn);
  p->n += nIn;
}

/*
** Append n spaces to the string.
*/
static void sbsWriteSpace(SbsLine *p, int n){
  while( n-- ) p->zLine[p->n++] = ' ';
}

/*
** Append a string to the output only if we are rendering HTML.
*/
static void sbsWriteHtml(SbsLine *p, const char *zIn){
  if( p->escHtml ) sbsWrite(p, zIn, strlen(zIn));
}

/*
** Write a 6-digit line number followed by a single space onto the line.
*/
static void sbsWriteLineno(SbsLine *p, int ln){
  sbsWriteHtml(p, "<span class=\"diffln\">");
  sqlite3_snprintf(7, &p->zLine[p->n], "%5d ", ln+1);
  p->n += 6;
  sbsWriteHtml(p, "</span>");
  p->zLine[p->n++] = ' ';
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

  nLeft = pLeft->h & LENGTH_MASK;
  zLeft = pLeft->z;
  nRight = pRight->h & LENGTH_MASK;
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
  if( nPrefix+nSuffix > nShort ) nPrefix = nShort - nSuffix;


  /* A single chunk of text inserted on the right */
  if( nPrefix+nSuffix==nLeft ){
    sbsWriteLineno(p, lnLeft);
    p->iStart2 = p->iEnd2 = 0;
    p->iStart = p->iEnd = -1;
    sbsWriteText(p, pLeft, SBS_PAD);
    if( nLeft==nRight && zLeft[nLeft]==zRight[nRight] ){
      sbsWrite(p, "   ", 3);
    }else{
      sbsWrite(p, " | ", 3);
    }
    sbsWriteLineno(p, lnRight);
    p->iStart = nPrefix;
    p->iEnd = nRight - nSuffix;
    p->zStart = zClassAdd;
    sbsWriteText(p, pRight, SBS_NEWLINE);
    return;
  }

  /* A single chunk of text deleted from the left */
  if( nPrefix+nSuffix==nRight ){
    /* Text deleted from the left */
    sbsWriteLineno(p, lnLeft);
    p->iStart2 = p->iEnd2 = 0;
    p->iStart = nPrefix;
    p->iEnd = nLeft - nSuffix;
    p->zStart = zClassRm;
    sbsWriteText(p, pLeft, SBS_PAD);
    sbsWrite(p, " | ", 3);
    sbsWriteLineno(p, lnRight);
    p->iStart = p->iEnd = -1;
    sbsWriteText(p, pRight, SBS_NEWLINE);
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
    sbsWriteLineno(p, lnLeft);
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
    sbsSimplifyLine(p, zLeft+nPrefix);
    sbsWriteText(p, pLeft, SBS_PAD);
    sbsWrite(p, " | ", 3);
    sbsWriteLineno(p, lnRight);
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
    sbsSimplifyLine(p, zRight+nPrefix);
    sbsWriteText(p, pRight, SBS_NEWLINE);
    return;
  }

  /* If all else fails, show a single big change between left and right */
  sbsWriteLineno(p, lnLeft);
  p->iStart2 = p->iEnd2 = 0;
  p->iStart = nPrefix;
  p->iEnd = nLeft - nSuffix;
  p->zStart = zClassChng;
  sbsWriteText(p, pLeft, SBS_PAD);
  sbsWrite(p, " | ", 3);
  sbsWriteLineno(p, lnRight);
  p->iEnd = nRight - nSuffix;
  sbsWriteText(p, pRight, SBS_NEWLINE);
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
  nA = pA->h & LENGTH_MASK;
  nB = pB->h & LENGTH_MASK;
  while( nA>0 && fossil_isspace(zA[0]) ){ nA--; zA++; }
  while( nA>0 && fossil_isspace(zA[nA-1]) ){ nA--; }
  while( nB>0 && fossil_isspace(zB[0]) ){ nB--; zB++; }
  while( nB>0 && fossil_isspace(zB[nB-1]) ){ nB--; }
  if( nA>250 ) nA = 250;
  if( nB>250 ) nB = 250;
  avg = (nA+nB)/2;
  if( avg==0 ) return 0;
  if( nA==nB && memcmp(zA, zB, nA)==0 ) return 0;
  memset(aFirst, 0, sizeof(aFirst));
  zA--; zB--;   /* Make both zA[] and zB[] 1-indexed */
  for(i=nB; i>0; i--){
    c = (unsigned char)zB[i];
    aNext[i] = aFirst[c];
    aFirst[c] = i;
  }
  best = 0;
  for(i=1; i<=nA-best; i++){
    c = (unsigned char)zA[i];
    for(j=aFirst[c]; j>0 && j<nB-best; j = aNext[j]){
      int limit = minInt(nA-i, nB-j);
      for(k=1; k<=limit && zA[k+i]==zB[k+j]; k++){}
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
   DLine *aRight, int nRight      /* Text on the right */
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
  if( nLeft*nRight>100000 ){
    memset(aM, 4, mnLen);
    if( nLeft>mnLen )  memset(aM+mnLen, 1, nLeft-mnLen);
    if( nRight>mnLen ) memset(aM+mnLen, 2, nRight-mnLen);
    return aM;
  }

  if( nRight < (sizeof(aBuf)/sizeof(aBuf[0]))-1 ){
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
  int nChunk = 0; /* Number of chunks of diff output seen so far */
  SbsLine s;    /* Output line buffer */
  int nContext; /* Lines of context above and below each change */
  int showDivider = 0;  /* True to show the divider */

  memset(&s, 0, sizeof(s));
  s.width = diff_width(diffFlags);
  s.zLine = fossil_malloc( 15*s.width + 200 );
  if( s.zLine==0 ) return;
  nContext = diff_context_lines(diffFlags);
  s.escHtml = (diffFlags & DIFF_HTML)!=0;
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
        blob_appendf(pOut, "<span class=\"diffhr\">%.*c</span>\n",
                           s.width*2+16, '.');
      }else{
        blob_appendf(pOut, "%.*c\n", s.width*2+16, '.');
      }
    }
    showDivider = 1;
    nChunk++;
    if( s.escHtml ){
      blob_appendf(pOut, "<a name=\"chunk%d\"></a>\n", nChunk);
    }

    /* Show the initial common area */
    a += skip;
    b += skip;
    m = R[r] - skip;
    for(j=0; j<m; j++){
      s.n = 0;
      sbsWriteLineno(&s, a+j);
      s.iStart = s.iEnd = -1;
      sbsWriteText(&s, &A[a+j], SBS_PAD);
      sbsWrite(&s, "   ", 3);
      sbsWriteLineno(&s, b+j);
      sbsWriteText(&s, &B[b+j], SBS_NEWLINE);
      blob_append(pOut, s.zLine, s.n);
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

      alignment = sbsAlignment(&A[a], ma, &B[b], mb);
      for(j=0; ma+mb>0; j++){
        if( alignment[j]==1 ){
          /* Delete one line from the left */
          s.n = 0;
          sbsWriteLineno(&s, a);
          s.iStart = 0;
          s.zStart = "<span class=\"diffrm\">";
          s.iEnd = LENGTH(&A[a]);
          sbsWriteText(&s, &A[a], SBS_PAD);
          if( s.escHtml ){
            sbsWrite(&s, " &lt;\n", 6);
          }else{
            sbsWrite(&s, " <\n", 3);
          }
          blob_append(pOut, s.zLine, s.n);
          assert( ma>0 );
          ma--;
          a++;
        }else if( alignment[j]==3 ){
          /* The left line is changed into the right line */
          s.n = 0;
          sbsWriteLineChange(&s, &A[a], a, &B[b], b);
          blob_append(pOut, s.zLine, s.n);
          assert( ma>0 && mb>0 );
          ma--;
          mb--;
          a++;
          b++;
        }else if( alignment[j]==2 ){
          /* Insert one line on the right */
          s.n = 0;
          sbsWriteSpace(&s, s.width + 7);
          if( s.escHtml ){
            sbsWrite(&s, " &gt; ", 6);
          }else{
            sbsWrite(&s, " > ", 3);
          }
          sbsWriteLineno(&s, b);
          s.iStart = 0;
          s.zStart = "<span class=\"diffadd\">";
          s.iEnd = LENGTH(&B[b]);
          sbsWriteText(&s, &B[b], SBS_NEWLINE);
          blob_append(pOut, s.zLine, s.n);
          assert( mb>0 );
          mb--;
          b++;
        }else{
          /* Delete from the left and insert on the right */
          s.n = 0;
          sbsWriteLineno(&s, a);
          s.iStart = 0;
          s.zStart = "<span class=\"diffrm\">";
          s.iEnd = LENGTH(&A[a]);
          sbsWriteText(&s, &A[a], SBS_PAD);
          sbsWrite(&s, " | ", 3);
          sbsWriteLineno(&s, b);
          s.iStart = 0;
          s.zStart = "<span class=\"diffadd\">";
          s.iEnd = LENGTH(&B[b]);
          sbsWriteText(&s, &B[b], SBS_NEWLINE);
          blob_append(pOut, s.zLine, s.n);
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
          s.n = 0;
          sbsWriteLineno(&s, a+j);
          s.iStart = s.iEnd = -1;
          sbsWriteText(&s, &A[a+j], SBS_PAD);
          sbsWrite(&s, "   ", 3);
          sbsWriteLineno(&s, b+j);
          sbsWriteText(&s, &B[b+j], SBS_NEWLINE);
          blob_append(pOut, s.zLine, s.n);
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
      s.n = 0;
      sbsWriteLineno(&s, a+j);
      s.iStart = s.iEnd = -1;
      sbsWriteText(&s, &A[a+j], SBS_PAD);
      sbsWrite(&s, "   ", 3);
      sbsWriteLineno(&s, b+j);
      sbsWriteText(&s, &B[b+j], SBS_NEWLINE);
      blob_append(pOut, s.zLine, s.n);
    }
  }
  free(s.zLine);
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
  int i, j, k;               /* Loop counters */
  int n;                     /* Loop limit */
  DLine *pA, *pB;            /* Pointers to lines */
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
    pA = &p->aFrom[iSX-1];
    pB = &p->aTo[iSY-1];
    n = minInt(iSX-iS1, iSY-iS2);
    for(k=0; k<n && same_dline(pA,pB); k++, pA--, pB--){}
    iSX -= k;
    iSY -= k;
    iEX = i+1;
    iEY = j;
    pA = &p->aFrom[iEX];
    pB = &p->aTo[iEY];
    n = minInt(iE1-iEX, iE2-iEY);
    for(k=0; k<n && same_dline(pA,pB); k++, pA++, pB++){}
    iEX += k;
    iEY += k;
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
  /* printf("LCS(%d..%d/%d..%d) = %d..%d/%d..%d\n",
     iS1, iE1, iS2, iE2, *piSX, *piEX, *piSY, *piEY);  */
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
      if( same_dline(pTop, pBtm)==0 ) break;
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
      if( same_dline(pTop, pBtm)==0 ) break;
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
      if( same_dline(pTop, pBtm)==0 ) break;
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
      if( same_dline(pTop, pBtm)==0 ) break;
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
  int ignoreEolWs; /* Ignore whitespace at the end of lines */
  DContext c;

  if( diffFlags & DIFF_INVERT ){
    Blob *pTemp = pA_Blob;
    pA_Blob = pB_Blob;
    pB_Blob = pTemp;
  }
  ignoreEolWs = (diffFlags & DIFF_IGNORE_EOLWS)!=0;

  /* Prepare the input files */
  memset(&c, 0, sizeof(c));
  c.aFrom = break_into_lines(blob_str(pA_Blob), blob_size(pA_Blob),
                             &c.nFrom, ignoreEolWs);
  c.aTo = break_into_lines(blob_str(pB_Blob), blob_size(pB_Blob),
                           &c.nTo, ignoreEolWs);
  if( c.aFrom==0 || c.aTo==0 ){
    fossil_free(c.aFrom);
    fossil_free(c.aTo);
    if( pOut ){
      blob_appendf(pOut, DIFF_CANNOT_COMPUTE_BINARY);
    }
    return 0;
  }

  /* Compute the difference */
  diff_all(&c);
  if( (diffFlags & DIFF_NOTTOOBIG)!=0 ){
    int i, m, n;
    int *a = c.aEdit;
    int mx = c.nEdit;
    for(i=m=n=0; i<mx; i+=3){ m += a[i]; n += a[i+1]+a[i+2]; }
    if( n>10000 ){
      fossil_free(c.aFrom);
      fossil_free(c.aTo);
      fossil_free(c.aEdit);
      if( diffFlags & DIFF_HTML ){
        blob_append(pOut, DIFF_TOO_MANY_CHANGES_HTML, -1);
      }else{
        blob_append(pOut, DIFF_TOO_MANY_CHANGES_TXT, -1);
      }
      return 0;
    }
  }
  if( (diffFlags & DIFF_NOOPT)==0 ){
    diff_optimize(&c);
  }

  if( pOut ){
    /* Compute a context or side-by-side diff into pOut */
    if( diffFlags & DIFF_SIDEBYSIDE ){
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
**   --brief                Show filenames only    DIFF_BRIEF
**   --context|-c N         N lines of context.    DIFF_CONTEXT_MASK
**   --html                 Format for HTML        DIFF_HTML
**   --invert               Invert the diff        DIFF_INVERT
**   --linenum|-n           Show line numbers      DIFF_LINENO
**   --noopt                Disable optimization   DIFF_NOOPT
**   --side-by-side|-y      Side-by-side diff.     DIFF_SIDEBYSIDE
**   --unified              Unified diff.          ~DIFF_SIDEBYSIDE
**   --width|-W N           N character lines.     DIFF_WIDTH_MASK
*/
u64 diff_options(void){
  u64 diffFlags = 0;
  const char *z;
  int f;
  if( find_option("side-by-side","y",0)!=0 ) diffFlags |= DIFF_SIDEBYSIDE;
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
  if( find_option("invert",0,0)!=0 ) diffFlags |= DIFF_INVERT;
  if( find_option("brief",0,0)!=0 ) diffFlags |= DIFF_BRIEF;
  return diffFlags;
}

/*
** COMMAND: test-rawdiff
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
  fossil_free(p->c.aEdit);
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
#define ANN_FILE_VERS    0x01   /* Show file vers rather than commit vers */
#define ANN_FILE_ANCEST  0x02   /* Prefer check-ins in the ANCESTOR table */

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
  Stmt ins;            /* Inserts into the temporary VSEEN table */
  int cnt = 0;         /* Number of versions examined */

  /* Initialize the annotation */
  rid = db_int(0, "SELECT fid FROM mlink WHERE mid=%d AND fnid=%d",mid,fnid);
  if( rid==0 ){
    fossil_panic("file #%d is unchanged in manifest #%d", fnid, mid);
  }
  if( !content_get(rid, &toAnnotate) ){
    fossil_panic("unable to retrieve content of artifact #%d", rid);
  }
  if( iLimit<=0 ) iLimit = 1000000000;
  annotation_start(p, &toAnnotate);
  db_begin_transaction();
  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS vseen(rid INTEGER PRIMARY KEY);"
     "DELETE FROM vseen;"
  );

  db_prepare(&ins, "INSERT OR IGNORE INTO vseen(rid) VALUES(:rid)");
  db_prepare(&q,
    "SELECT (SELECT uuid FROM blob WHERE rid=mlink.%s),"
    "       date(event.mtime),"
    "       coalesce(event.euser,event.user),"
    "       mlink.pid"
    "  FROM mlink, event"
    " WHERE mlink.fid=:rid"
    "   AND event.objid=mlink.mid"
    "   AND mlink.pid NOT IN vseen"
    " ORDER BY %s event.mtime",
    (annFlags & ANN_FILE_VERS)!=0 ? "fid" : "mid",
    (annFlags & ANN_FILE_ANCEST)!=0 ?
         "(mlink.mid IN (SELECT rid FROM ancestor)) DESC,":""
  );

  db_bind_int(&q, ":rid", rid);
  if( iLimit==0 ) iLimit = 1000000000;
  while( rid && iLimit>cnt && db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zUser = db_column_text(&q, 2);
    int prevId = db_column_int(&q, 3);
    if( webLabel ){
      zLabel = mprintf(
          "<a href='%R/info/%s' target='infowindow'>%.10s</a> %s %13.13s",
          zUuid, zUuid, zDate, zUser
      );
    }else{
      zLabel = mprintf("%.10s %s %13.13s", zUuid, zDate, zUser);
    }
    p->nVers++;
    p->azVers = fossil_realloc(p->azVers, p->nVers*sizeof(p->azVers[0]) );
    p->azVers[p->nVers-1] = zLabel;
    content_get(rid, &step);
    annotation_step(p, &step, zLabel);
    db_bind_int(&ins, ":rid", rid);
    db_step(&ins);
    db_reset(&ins);
    blob_reset(&step);
    db_reset(&q);
    rid = prevId;
    db_bind_int(&q, ":rid", prevId);
    cnt++;
  }
  db_finalize(&q);
  db_finalize(&ins);
  db_end_transaction(0);
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
  int annFlags = ANN_FILE_ANCEST;
  int showLn = 0;        /* True if line numbers should be shown */
  char zLn[10];          /* Line number buffer */
  char zFormat[10];      /* Format string for line numbers */
  Annotator ann;

  showLn = P("ln")!=0;
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(); return; }
  mid = name_to_typed_rid(PD("checkin","0"),"ci");
  fnid = db_int(0, "SELECT fnid FROM filename WHERE name=%Q", P("filename"));
  if( mid==0 || fnid==0 ){ fossil_redirect_home(); }
  iLimit = atoi(PD("limit","-1"));
  if( !db_exists("SELECT 1 FROM mlink WHERE mid=%d AND fnid=%d",mid,fnid) ){
    fossil_redirect_home();
  }
  compute_direct_ancestors(mid, 10000000);
  style_header("File Annotation");
  if( P("filevers") ) annFlags |= ANN_FILE_VERS;
  annotate_file(&ann, fnid, mid, g.perm.Hyperlink, iLimit, annFlags);
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
  if( showLn ){
    sqlite3_snprintf(sizeof(zLn), zLn, "%d", ann.nOrig+1);
    sqlite3_snprintf(sizeof(zFormat), zFormat, "%%%dd:", strlen(zLn));
  }else{
    zLn[0] = 0;
  }
  @ <pre>
  for(i=0; i<ann.nOrig; i++){
    ((char*)ann.aOrig[i].z)[ann.aOrig[i].n] = 0;
    if( showLn ) sqlite3_snprintf(sizeof(zLn), zLn, zFormat, i+1);
    @ %s(ann.aOrig[i].zSrc):%s(zLn) %h(ann.aOrig[i].z)
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
**
** See also: info, finfo, timeline
*/
void annotate_cmd(void){
  int fnid;         /* Filename ID */
  int fid;          /* File instance ID */
  int mid;          /* Manifest where file was checked in */
  int cid;          /* Checkout ID */
  Blob treename;    /* FILENAME translated to canonical form */
  char *zFilename;  /* Canonical filename */
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
  if( g.argc<3 ) {
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
  cid = db_lget_int("checkout", 0);
  if( cid == 0 ){
    fossil_fatal("Not in a checkout");
  }
  if( iLimit<=0 ) iLimit = 1000000000;
  compute_direct_ancestors(cid, iLimit);
  mid = db_int(0, "SELECT mlink.mid FROM mlink, ancestor "
          " WHERE mlink.fid=%d AND mlink.fnid=%d AND mlink.mid=ancestor.rid"
          " ORDER BY ancestor.generation ASC LIMIT 1",
          fid, fnid);
  if( mid==0 ){
    fossil_panic("unable to find manifest");
  }
  if( fileVers ) annFlags |= ANN_FILE_VERS;
  annFlags |= ANN_FILE_ANCEST;
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

/*
** COMMAND: test-looks-like-utf
**
** Usage:  %fossil test-looks-like-utf FILENAME
**
** FILENAME is the name of a file to check for textual content in the UTF-8
** and/or UTF-16 encodings.
*/
void looks_like_utf_test_cmd(void){
  Blob blob;     /* the contents of the specified file */
  int fUtf8;     /* return value of starts_with_utf8_bom() */
  int fUtf16;    /* return value of starts_with_utf16_bom() */
  int fUnicode;  /* return value of could_be_utf16() */
  int lookFlags; /* output flags from looks_like_utf8/utf16() */
  int bRevUtf16 = 0; /* non-zero -> UTF-16 byte order reversed */
  int bRevUnicode = 0; /* non-zero -> UTF-16 byte order reversed */
  if( g.argc!=3 ) usage("FILENAME");
  blob_read_from_file(&blob, g.argv[2]);
  fUtf8 = starts_with_utf8_bom(&blob, 0);
  fUtf16 = starts_with_utf16_bom(&blob, 0, &bRevUtf16);
  fUnicode = could_be_utf16(&blob, &bRevUnicode);
  lookFlags = fUnicode ? looks_like_utf16(&blob, bRevUnicode) :
                         looks_like_utf8(&blob);
  fossil_print("File \"%s\" has %d bytes.\n",g.argv[2],blob_size(&blob));
  fossil_print("Starts with UTF-8 BOM: %s\n",fUtf8?"yes":"no");
  fossil_print("Starts with UTF-16 BOM: %s\n",
               fUtf16?(bRevUtf16?"reversed":"yes"):"no");
  fossil_print("Looks like UTF-%s: %s\n",fUnicode?"16":"8",
               (lookFlags&LOOK_BINARY)?"no":"yes");
  fossil_print("Has flag LOOK_NUL: %s\n",(lookFlags&LOOK_NUL)?"yes":"no");
  fossil_print("Has flag LOOK_CR: %s\n",(lookFlags&LOOK_CR)?"yes":"no");
  fossil_print("Has flag LOOK_LONE_CR: %s\n",
               (lookFlags&LOOK_LONE_CR)?"yes":"no");
  fossil_print("Has flag LOOK_LF: %s\n",(lookFlags&LOOK_LF)?"yes":"no");
  fossil_print("Has flag LOOK_LONE_LF: %s\n",
               (lookFlags&LOOK_LONE_LF)?"yes":"no");
  fossil_print("Has flag LOOK_CRLF: %s\n",(lookFlags&LOOK_CRLF)?"yes":"no");
  fossil_print("Has flag LOOK_LONG: %s\n",(lookFlags&LOOK_LONG)?"yes":"no");
  fossil_print("Has flag LOOK_INVALID: %s\n",
               (lookFlags&LOOK_INVALID)?"yes":"no");
  fossil_print("Has flag LOOK_ODD: %s\n",(lookFlags&LOOK_ODD)?"yes":"no");
  fossil_print("Has flag LOOK_SHORT: %s\n",(lookFlags&LOOK_SHORT)?"yes":"no");
  blob_reset(&blob);
}
