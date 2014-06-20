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
#endif

#if INTERFACE
#define COMMENT_PRINT_NONE       ((u32)0x00000000)  /* No flags. */
#define COMMENT_PRINT_TRIM_SPACE ((u32)0x00000001)  /* Trim leading/trailing. */
#define COMMENT_PRINT_WORD_BREAK ((u32)0x00000002)  /* Break lines on words. */
#define COMMENT_PRINT_DEFAULT    (COMMENT_PRINT_TRIM_SPACE) /* Defaults. */
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
  int trimSpace,     /* [in] Non-zero to trim leading/trailing spaces. */
  int *piIndex       /* [in/out] Pointer to first non-space character. */
){
  if( indent>0 ){
    fossil_print("%*s", indent, "");
    if( trimSpace && zLine && piIndex ){
      int index = *piIndex;
      while( fossil_isspace(zLine[index]) ){ index++; }
      *piIndex = index;
    }
  }
}

/*
** This function prints one logical line of a comment, stopping when it hits
** a new line -OR- runs out of space on the logical line.
*/
static void comment_print_line(
  const char *zLine,  /* [in] The comment line to print. */
  int indent,         /* [in] Number of spaces to indent, zero for none. */
  int lineChars,      /* [in] Maximum number of characters to print. */
  int trimSpace,      /* [in] Non-zero to trim leading/trailing spaces. */
  int wordBreak,      /* [in] Non-zero to try breaking on word boundaries. */
  int *pLineCnt,      /* [in/out] Pointer to the total line count. */
  const char **pzLine /* [out] Pointer to the end of the logical line. */
){
  int index = 0, charCnt = 0, lineCnt = 0, maxChars;
  if( !zLine ) return;
  if( lineChars<=0 ) return;
  comment_print_indent(zLine, indent, trimSpace, &index);
  maxChars = lineChars;
  for(;;){
    char c = zLine[index];
    if( c==0 ){
      break;
    }else{
      index++;
    }
    if( c=='\n' ){
      charCnt = 0;
      lineCnt++;
    }else if( c=='\t' ){
      int nextIndex = comment_next_space(zLine, index);
      if( nextIndex<=0 || (nextIndex-index)>maxChars ){
        break;
      }
      charCnt++;
      if( maxChars<COMMENT_TAB_WIDTH ){
        fossil_print(" ");
        break;
      }
      maxChars -= COMMENT_TAB_WIDTH;
    }else if( wordBreak && fossil_isspace(c) ){
      int nextIndex = comment_next_space(zLine, index);
      if( nextIndex<=0 || (nextIndex-index)>maxChars ){
        break;
      }
      charCnt++;
      maxChars--;
    }else{
      charCnt++;
      maxChars--;
    }
    fossil_print("%c", c);
    if( maxChars==0 ) break;
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
** Given a comment string zText, format that string for printing
** on a TTY.  Assume that the output cursors is indent spaces from
** the left margin and that a single line can contain no more than
** width characters.  Indent all subsequent lines by indent.
**
** Return the number of newlines that are output.
*/
int comment_print(
  const char *zText, /* The comment text to be printed. */
  int indent,        /* Number of spaces to indent each non-initial line. */
  int width,         /* Maximum number of characters per line. */
  int flags          /* Zero or more "COMMENT_PRINT_*" flags, see above. */
){
  int maxChars = width - indent;
  int trimSpace = flags & COMMENT_PRINT_TRIM_SPACE;
  int wordBreak = flags & COMMENT_PRINT_WORD_BREAK;
  int lineCnt = 0;
  const char *zLine;

#if defined(_WIN32)
  if( width<0 ){
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    memset(&csbi, 0, sizeof(CONSOLE_SCREEN_BUFFER_INFO));
    if( GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi) ){
      maxChars = csbi.srWindow.Right - csbi.srWindow.Left - indent;
    }
  }
#elif defined(TIOCGWINSZ)
  if( width<0 ){
    struct winsize w;
    memset(&w, 0, sizeof(struct winsize));
    if( ioctl(0, TIOCGWINSZ, &w)!=-1 ){
      maxChars = w.ws_col - indent;
    }
  }
#else
  if( width<0 ){
    /*
    ** Fallback to using more-or-less the "legacy semantics" of hard-coding
    ** the maximum line length to a value reasonable for the vast majority
    ** of supported systems.
    */
    maxChars = COMMENT_LEGACY_LINE_LENGTH - indent;
  }
#endif
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
    comment_print_line(zLine, zLine>zText ? indent : 0, maxChars,
                       trimSpace, wordBreak, &lineCnt, &zLine);
    if( !zLine || !zLine[0] ) break;
  }
  return lineCnt;
}

/*
**
** COMMAND: test-comment-format
**
** Usage: %fossil test-comment-format ?OPTIONS? PREFIX TEXT ?WIDTH?
**
** Test comment formatting and printing.  Use for testing only.
**
** Options:
**   --decode         Decode the text using the same method used when
**                    handling the value of a C-card from a manifest.
**   --wordbreak      Attempt to break lines on word boundaries.
*/
void test_comment_format(void){
  const char *zPrefix;
  char *zText;
  int indent, width;
  int decode = find_option("decode", 0, 0)!=0;
  int flags = COMMENT_PRINT_DEFAULT;
  if( find_option("wordbreak", 0, 0) ){
    flags |= COMMENT_PRINT_WORD_BREAK;
  }
  if( g.argc!=4 && g.argc!=5 ){
    usage("PREFIX TEXT ?WIDTH?");
  }
  zPrefix = g.argv[2];
  if( decode ){
    zText = mprintf("%s", g.argv[3]);
    defossilize(zText);
  }else{
    zText = g.argv[3];
  }
  indent = strlen(zPrefix);
  if( g.argc==5 ){
    width = atoi(g.argv[4]);
  }else{
    width = -1; /* automatic */
  }
  if( indent>0 ){
    fossil_print("%s", zPrefix);
  }
  fossil_print("(%d lines output)\n",
               comment_print(zText, indent, width, flags));
  if( zText!=g.argv[3] ) fossil_free(zText);
}
