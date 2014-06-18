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

/*
** This is the previous value used by most external callers when they
** needed to specify a default maximum line length to be used with the
** comment_print() function.
*/
#ifndef COMMENT_LEGACY_LINE_LENGTH
# define COMMENT_LEGACY_LINE_LENGTH    (78)
#endif

/*
** Given a comment string zText, format that string for printing
** on a TTY.  Assume that the output cursors is indent spaces from
** the left margin and that a single line can contain no more than
** lineLength characters.  Indent all subsequent lines by indent.
**
** Return the number of newlines that are output.
*/
int comment_print(const char *zText, int indent, int lineLength){
  int tlen = lineLength - indent;
  int len = 0;
  int doIndent = 0;
  const char *zBuf;
  int lineCnt = 0;

#if defined(_WIN32)
  if( lineLength<0 ){
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    memset(&csbi, 0, sizeof(CONSOLE_SCREEN_BUFFER_INFO));
    if( GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi) ){
      tlen = csbi.srWindow.Right - csbi.srWindow.Left - indent;
    }
  }
#elif defined(TIOCGWINSZ)
  if( lineLength<0 ){
    struct winsize w;
    memset(&w, 0, sizeof(struct winsize));
    if( ioctl(0, TIOCGWINSZ, &w)!=-1 ){
      tlen = w.ws_col - indent;
    }
  }
#else
  if( lineLength<0 ){
    /*
    ** Fallback to using more-or-less the "legacy semantics" of hard-coding
    ** the maximum line length to a value reasonable for the vast majority
    ** of supported systems.
    */
    tlen = COMMENT_LEGACY_LINE_LENGTH - indent;
  }
#endif
  if( zText==0 ) zText = "(NULL)";
  if( tlen<=0 ){
    tlen = strlen(zText);
  }
  while( fossil_isspace(zText[0]) ){ zText++; }
  if( zText[0]==0 ){
    if( doIndent==0 ){
      fossil_print("\n");
      lineCnt = 1;
    }
    return lineCnt;
  }

  zBuf = zText;
  for(;;){
    if( zText[0]==0 ){
      if( doIndent ){
        fossil_print("%*s", indent, "");
      }
      fossil_print("%.*s\n", zText -zBuf, zBuf);
      lineCnt++;
      break;
    }

    if( zText[0]=='\t' ){
      len += 8;
    }else{
      len++;
    }
    if( (zText[0]=='\n') || len >= tlen ){
      while( !fossil_isspace(zText[0]) ){ zText--; }
      if( doIndent ){
	      fossil_print("%*s", indent, "");
      }
      doIndent = 1;
      fossil_print("%.*s\n", zText -zBuf, zBuf);
      zBuf = zText;
      len=0;
      lineCnt++;
      if( !zBuf++ ) break;
    }
    zText++;
  }
  return lineCnt;
}

/*
** Test the comment printing
**
** COMMAND: test-comment-format
*/
void test_comment_format(void){
  int indent;
  if( g.argc!=4 ){
    usage("PREFIX TEXT");
  }
  indent = strlen(g.argv[2]) + 1;
  fossil_print("%s ", g.argv[2]);
  fossil_print("(%d lines output)\n", comment_print(g.argv[3], indent, 79));
}
