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
** This file contains code used to format and print comments or other
** text on a TTY.
*/
#include "config.h"
#include "comformat.h"
#include <assert.h>
#ifdef _WIN32
# include <windows.h>
#else
# include <termios.h>
# include <sys/ioctl.h>
#endif

#if INTERFACE
#define COMMENT_PRINT_NONE       ((u32)0x00000000) /* No flags. */
#define COMMENT_PRINT_LEGACY     ((u32)0x00000001) /* Use legacy algorithm. */
#define COMMENT_PRINT_TRIM_CRLF  ((u32)0x00000002) /* Trim leading CR/LF. */
#define COMMENT_PRINT_TRIM_SPACE ((u32)0x00000004) /* Trim leading/trailing. */
#define COMMENT_PRINT_WORD_BREAK ((u32)0x00000008) /* Break lines on words. */
#define COMMENT_PRINT_ORIG_BREAK ((u32)0x00000010) /* Break before original. */
#define COMMENT_PRINT_DEFAULT    (COMMENT_PRINT_LEGACY) /* Defaults. */
#endif

/*
** This is the previous value used by most external callers when they
** needed to specify a default maximum line length to be used with the
** comment_print() function.
*/
#ifndef COMMENT_LEGACY_LINE_LENGTH
# define COMMENT_LEGACY_LINE_LENGTH    (78)
#endif

/*
** This is the number of spaces to print when a tab character is seen.
*/
#ifndef COMMENT_TAB_WIDTH
# define COMMENT_TAB_WIDTH             (8)
#endif

/*
** This function sets the maximum number of characters to print per line
** based on the detected terminal line width, if available; otherwise, it
** uses the legacy default terminal line width minus the amount to indent.
**
** Zero is returned to indicate any failure.  One is returned to indicate
** the successful detection of the terminal line width.  Negative one is
** returned to indicate the terminal line width is using the hard-coded
** legacy default value.
*/
static int comment_set_maxchars(
  int indent,
  int *pMaxChars
){
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  memset(&csbi, 0, sizeof(CONSOLE_SCREEN_BUFFER_INFO));
  if( GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi) ){
    *pMaxChars = csbi.srWindow.Right - csbi.srWindow.Left - indent;
    return 1;
  }
  return 0;
#elif defined(TIOCGWINSZ)
  struct winsize w;
  memset(&w, 0, sizeof(struct winsize));
  if( ioctl(0, TIOCGWINSZ, &w)!=-1 ){
    *pMaxChars = w.ws_col - indent;
    return 1;
  }
  return 0;
#else
  /*
  ** Fallback to using more-or-less the "legacy semantics" of hard-coding
  ** the maximum line length to a value reasonable for the vast majority
  ** of supported systems.
  */
  *pMaxChars = COMMENT_LEGACY_LINE_LENGTH - indent;
  return -1;
#endif
}

/*
** This function checks the current line being printed against the original
** comment text.  Upon matching, it emits a new line and updates the provided
** character and line counts, if applicable.
*/
static int comment_check_orig(
  const char *zOrigText, /* [in] Original comment text ONLY, may be NULL. */
  const char *zLine,     /* [in] The comment line to print. */
  int *pCharCnt,         /* [in/out] Pointer to the line character count. */
  int *pLineCnt          /* [in/out] Pointer to the total line count. */
){
  if( zOrigText && fossil_strcmp(zLine, zOrigText)==0 ){
    fossil_print("\n");
    if( pCharCnt ) *pCharCnt = 0;
    if( pLineCnt ) (*pLineCnt)++;
    return 1;
  }
  return 0;
}

/*
** This function scans the specified comment line starting just after the
** initial index and returns the index of the next spacing character -OR-
** zero if such a character cannot be found.  For the purposes of this
** algorithm, the NUL character is treated the same as a spacing character.
*/
static int comment_next_space(
  const char *zLine, /* [in] The comment line being printed. */
  int index          /* [in] The current character index being handled. */
){
  int nextIndex = index + 1;
  for(;;){
    char c = zLine[nextIndex];
    if( c==0 || fossil_isspace(c) ){
      return nextIndex;
    }
    nextIndex++;
  }
  return 0; /* NOT REACHED */
}

/*
** This function is called when printing a logical comment line to perform
** the necessary indenting.
*/
static void comment_print_indent(
  const char *zLine, /* [in] The comment line being printed. */
  int indent,        /* [in] Number of spaces to indent, zero for none. */
  int trimCrLf,      /* [in] Non-zero to trim leading/trailing CR/LF. */
  int trimSpace,     /* [in] Non-zero to trim leading/trailing spaces. */
  int *piIndex       /* [in/out] Pointer to first non-space character. */
){
  if( indent>0 ){
    fossil_print("%*s", indent, "");
  }
  if( zLine && piIndex ){
    int index = *piIndex;
    if( trimCrLf ){
      while( zLine[index]=='\r' || zLine[index]=='\n' ){ index++; }
    }
    if( trimSpace ){
      while( fossil_isspace(zLine[index]) ){ index++; }
    }
    *piIndex = index;
  }
}

/*
** This function prints one logical line of a comment, stopping when it hits
** a new line -OR- runs out of space on the logical line.
*/
static void comment_print_line(
  const char *zOrigText, /* [in] Original comment text ONLY, may be NULL. */
  const char *zLine,     /* [in] The comment line to print. */
  int origIndent,        /* [in] Number of spaces to indent before the original
                         **      comment. */
  int indent,            /* [in] Number of spaces to indent, before the line
                         **      to print. */
  int lineChars,         /* [in] Maximum number of characters to print. */
  int trimCrLf,          /* [in] Non-zero to trim leading/trailing CR/LF. */
  int trimSpace,         /* [in] Non-zero to trim leading/trailing spaces. */
  int wordBreak,         /* [in] Non-zero to try breaking on word boundaries. */
  int origBreak,         /* [in] Non-zero to break before original comment. */
  int *pLineCnt,         /* [in/out] Pointer to the total line count. */
  const char **pzLine    /* [out] Pointer to the end of the logical line. */
){
  int index = 0, charCnt = 0, lineCnt = 0, maxChars;
  if( !zLine ) return;
  if( lineChars<=0 ) return;
  comment_print_indent(zLine, indent, trimCrLf, trimSpace, &index);
  maxChars = lineChars;
  for(;;){
    int useChars = 1;
    char c = zLine[index];
    if( c==0 ){
      break;
    }else{
      if( origBreak && index>0 ){
        const char *zCurrent = &zLine[index];
        if( comment_check_orig(zOrigText, zCurrent, &charCnt, &lineCnt) ){
          comment_print_indent(zCurrent, origIndent, trimCrLf, trimSpace,
                               &index);
          maxChars = lineChars;
        }
      }
      index++;
    }
    if( c=='\n' ){
      lineCnt++;
      charCnt = 0;
      useChars = 0;
    }else if( c=='\t' ){
      int nextIndex = comment_next_space(zLine, index);
      if( nextIndex<=0 || (nextIndex-index)>maxChars ){
        break;
      }
      charCnt++;
      useChars = COMMENT_TAB_WIDTH;
      if( maxChars<useChars ){
        fossil_print(" ");
        break;
      }
    }else if( wordBreak && fossil_isspace(c) ){
      int nextIndex = comment_next_space(zLine, index);
      if( nextIndex<=0 || (nextIndex-index)>maxChars ){
        break;
      }
      charCnt++;
    }else{
      charCnt++;
    }
    assert( c!='\n' || charCnt==0 );
    fossil_print("%c", c);
    if( (c&0x80)==0 || (zLine[index+1]&0xc0)!=0xc0 ) maxChars -= useChars;
    if( maxChars<=0 ) break;
    if( c=='\n' ) break;
  }
  if( charCnt>0 ){
    fossil_print("\n");
    lineCnt++;
  }
  if( pLineCnt ){
    *pLineCnt += lineCnt;
  }
  if( pzLine ){
    *pzLine = zLine + index;
  }
}

/*
** This is the legacy comment printing algorithm.  It is being retained
** for backward compatibility.
**
** Given a comment string, format that string for printing on a TTY.
** Assume that the output cursors is indent spaces from the left margin
** and that a single line can contain no more than 'width' characters.
** Indent all subsequent lines by 'indent'.
**
** Returns the number of new lines emitted.
*/
static int comment_print_legacy(
  const char *zText, /* The comment text to be printed. */
  int indent,        /* Number of spaces to indent each non-initial line. */
  int width          /* Maximum number of characters per line. */
){
  int maxChars = width - indent;
  int si, sk, i, k;
  int doIndent = 0;
  char *zBuf;
  char zBuffer[400];
  int lineCnt = 0;

  if( width<0 ){
    comment_set_maxchars(indent, &maxChars);
  }
  if( zText==0 ) zText = "(NULL)";
  if( maxChars<=0 ){
    maxChars = strlen(zText);
  }
  if( maxChars >= (sizeof(zBuffer)) ){
    zBuf = fossil_malloc(maxChars+1);
  }else{
    zBuf = zBuffer;
  }
  for(;;){
    while( fossil_isspace(zText[0]) ){ zText++; }
    if( zText[0]==0 ){
      if( doIndent==0 ){
        fossil_print("\n");
        lineCnt = 1;
      }
      if( zBuf!=zBuffer) fossil_free(zBuf);
      return lineCnt;
    }
    for(sk=si=i=k=0; zText[i] && k<maxChars; i++){
      char c = zText[i];
      if( fossil_isspace(c) ){
        si = i;
        sk = k;
        if( k==0 || zBuf[k-1]!=' ' ){
          zBuf[k++] = ' ';
        }
      }else{
        zBuf[k] = c;
        if( c=='-' && k>0 && fossil_isalpha(zBuf[k-1]) ){
          si = i+1;
          sk = k+1;
        }
        k++;
      }
    }
    if( doIndent ){
      fossil_print("%*s", indent, "");
    }
    doIndent = 1;
    if( sk>0 && zText[i] ){
      zText += si;
      zBuf[sk] = 0;
    }else{
      zText += i;
      zBuf[k] = 0;
    }
    fossil_print("%s\n", zBuf);
    lineCnt++;
  }
}

/*
** This is the comment printing function.  The comment printing algorithm
** contained within it attempts to preserve the formatting present within
** the comment string itself while honoring line width limitations.  There
** are several flags that modify the default behavior of this function:
**
**         COMMENT_PRINT_LEGACY: Forces use of the legacy comment printing
**                               algorithm.  For backward compatibility,
**                               this is the default.
**
**      COMMENT_PRINT_TRIM_CRLF: Trims leading and trailing carriage-returns
**                               and line-feeds where they do not materially
**                               impact pre-existing formatting (i.e. at the
**                               start of the comment string -AND- right
**                               before line indentation).  This flag does
**                               not apply to the legacy comment printing
**                               algorithm.  This flag may be combined with
**                               COMMENT_PRINT_TRIM_SPACE.
**
**     COMMENT_PRINT_TRIM_SPACE: Trims leading and trailing spaces where they
**                               do not materially impact the pre-existing
**                               formatting (i.e. at the start of the comment
**                               string -AND- right before line indentation).
**                               This flag does not apply to the legacy
**                               comment printing algorithm.  This flag may
**                               be combined with COMMENT_PRINT_TRIM_CRLF.
**
**     COMMENT_PRINT_WORD_BREAK: Attempts to break lines on word boundaries
**                               while honoring the logical line length.
**                               If this flag is not specified, honoring the
**                               logical line length may result in breaking
**                               lines in the middle of words.  This flag
**                               does not apply to the legacy comment
**                               printing algorithm.
**
**     COMMENT_PRINT_ORIG_BREAK: Looks for the original comment text within
**                               the text being printed.  Upon matching, a
**                               new line will be emitted, thus preserving
**                               more of the pre-existing formatting.
**
** Given a comment string, format that string for printing on a TTY.
** Assume that the output cursors is indent spaces from the left margin
** and that a single line can contain no more than 'width' characters.
** Indent all subsequent lines by 'indent'.
**
** Returns the number of new lines emitted.
*/
int comment_print(
  const char *zText,     /* The comment text to be printed. */
  const char *zOrigText, /* Original comment text ONLY, may be NULL. */
  int indent,            /* Spaces to indent each non-initial line. */
  int width,             /* Maximum number of characters per line. */
  int flags              /* Zero or more "COMMENT_PRINT_*" flags. */
){
  int maxChars = width - indent;
  int legacy = flags & COMMENT_PRINT_LEGACY;
  int trimCrLf = flags & COMMENT_PRINT_TRIM_CRLF;
  int trimSpace = flags & COMMENT_PRINT_TRIM_SPACE;
  int wordBreak = flags & COMMENT_PRINT_WORD_BREAK;
  int origBreak = flags & COMMENT_PRINT_ORIG_BREAK;
  int lineCnt = 0;
  const char *zLine;

  if( legacy ){
    return comment_print_legacy(zText, indent, width);
  }
  if( width<0 ){
    comment_set_maxchars(indent, &maxChars);
  }
  if( zText==0 ) zText = "(NULL)";
  if( maxChars<=0 ){
    maxChars = strlen(zText);
  }
  if( trimSpace ){
    while( fossil_isspace(zText[0]) ){ zText++; }
  }
  if( zText[0]==0 ){
    fossil_print("\n");
    lineCnt++;
    return lineCnt;
  }
  zLine = zText;
  for(;;){
    comment_print_line(zOrigText, zLine, indent, zLine>zText ? indent : 0,
                       maxChars, trimCrLf, trimSpace, wordBreak, origBreak,
                       &lineCnt, &zLine);
    if( !zLine || !zLine[0] ) break;
  }
  return lineCnt;
}

/*
**
** COMMAND: test-comment-format
**
** Usage: %fossil test-comment-format ?OPTIONS? PREFIX TEXT ?ORIGTEXT?
**
** Test comment formatting and printing.  Use for testing only.
**
** Options:
**   --file           The comment text is really just a file name to
**                    read it from.
**   --decode         Decode the text using the same method used when
**                    handling the value of a C-card from a manifest.
**   --legacy         Use the legacy comment printing algorithm.
**   --trimcrlf       Enable trimming of leading/trailing CR/LF.
**   --trimspace      Enable trimming of leading/trailing spaces.
**   --wordbreak      Attempt to break lines on word boundaries.
**   --origbreak      Attempt to break when the original comment text
**                    is detected.
**   --indent         Number of spaces to indent (default (-1) is to
**                    auto-detect).  Zero means no indent.
**   -W|--width <num> Width of lines (default (-1) is to auto-detect).
**                    Zero means no limit.
*/
void test_comment_format(void){
  const char *zWidth;
  const char *zIndent;
  const char *zPrefix;
  char *zText;
  char *zOrigText;
  int indent, width;
  int fromFile = find_option("file", 0, 0)!=0;
  int decode = find_option("decode", 0, 0)!=0;
  int flags = COMMENT_PRINT_NONE;
  if( find_option("legacy", 0, 0) ){
    flags |= COMMENT_PRINT_LEGACY;
  }
  if( find_option("trimcrlf", 0, 0) ){
    flags |= COMMENT_PRINT_TRIM_CRLF;
  }
  if( find_option("trimspace", 0, 0) ){
    flags |= COMMENT_PRINT_TRIM_SPACE;
  }
  if( find_option("wordbreak", 0, 0) ){
    flags |= COMMENT_PRINT_WORD_BREAK;
  }
  if( find_option("origbreak", 0, 0) ){
    flags |= COMMENT_PRINT_ORIG_BREAK;
  }
  zWidth = find_option("width","W",1);
  if( zWidth ){
    width = atoi(zWidth);
  }else{
    width = -1; /* automatic */
  }
  zIndent = find_option("indent",0,1);
  if( zIndent ){
    indent = atoi(zIndent);
  }else{
    indent = -1; /* automatic */
  }
  if( g.argc!=4 && g.argc!=5 ){
    usage("?OPTIONS? PREFIX TEXT ?ORIGTEXT?");
  }
  zPrefix = g.argv[2];
  zText = g.argv[3];
  if( g.argc==5 ){
    zOrigText = g.argv[4];
  }else{
    zOrigText = 0;
  }
  if( fromFile ){
    Blob fileData;
    blob_read_from_file(&fileData, zText);
    zText = mprintf("%s", blob_str(&fileData));
    blob_reset(&fileData);
    if( zOrigText ){
      blob_read_from_file(&fileData, zOrigText);
      zOrigText = mprintf("%s", blob_str(&fileData));
      blob_reset(&fileData);
    }
  }
  if( decode ){
    zText = mprintf(fromFile?"%z":"%s" /*works-like:"%s"*/, zText);
    defossilize(zText);
    if( zOrigText ){
      zOrigText = mprintf(fromFile?"%z":"%s" /*works-like:"%s"*/, zOrigText);
      defossilize(zOrigText);
    }
  }
  if( indent<0 ){
    indent = strlen(zPrefix);
  }
  if( zPrefix && *zPrefix ){
    fossil_print("%s", zPrefix);
  }
  fossil_print("(%d lines output)\n",
               comment_print(zText, zOrigText, indent, width, flags));
  if( zOrigText && zOrigText!=g.argv[4] ) fossil_free(zOrigText);
  if( zText && zText!=g.argv[3] ) fossil_free(zText);
}
