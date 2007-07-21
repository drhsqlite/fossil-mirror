/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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
** lineLength must be less than 400.
**
** Return the number of newlines that are output.
*/
int comment_print(const char *zText, int indent, int lineLength){
  int tlen = lineLength - indent;
  int si, sk, i, k;
  int doIndent = 0;
  char zBuf[400];
  int lineCnt = 0; 

  for(;;){
    while( isspace(zText[0]) ){ zText++; }
    if( zText[0]==0 ){
      if( doIndent==0 ){
        printf("\n");
        lineCnt = 1;
      }
      return lineCnt;
    }
    for(sk=si=i=k=0; zText[i] && k<tlen; i++){
      char c = zText[i];
      if( isspace(c) ){
        si = i;
        sk = k;
        if( k==0 || zBuf[k-1]!=' ' ){
          zBuf[k++] = ' ';
        }
      }else{
        zBuf[k] = c;
        if( c=='-' && k>0 && isalpha(zBuf[k-1]) ){
          si = i+1;
          sk = k+1;
        }
        k++;
      }
    }
    if( doIndent ){
      printf("%*s", indent, "");
    }
    doIndent = 1;
    if( sk>0 && zText[i] ){
      zText += si;
      zBuf[sk++] =  '\n';
      zBuf[sk] = 0;
      printf("%s", zBuf);
    }else{
      zText += i;
      zBuf[k++] =  '\n';
      zBuf[k] = 0;
      printf("%s", zBuf);
    }
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
  printf("%s ", g.argv[2]);
  printf("(%d lines output)\n", comment_print(g.argv[3], indent, 79));
}
