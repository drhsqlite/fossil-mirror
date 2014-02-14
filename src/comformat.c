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
  int si, sk, i, k;
  int doIndent = 0;
  char *zBuf;
  char zBuffer[400];
  int lineCnt = 0;

  if( tlen<=0 ){
    tlen = strlen(zText);
  }
  if( tlen >= (sizeof(zBuffer)) ){
    zBuf = fossil_malloc(tlen+1);
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
    for(sk=si=i=k=0; zText[i] && k<tlen; i++){
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
