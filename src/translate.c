/*
** Copyright (c) 2002 D. Richard Hipp
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
** SYNOPSIS:
**
** Input lines that begin with the "@" character are translated into
** either cgi_printf() statements or string literals and the
** translated code is written on standard output.
**
** The problem this program is attempt to solve is as follows:  When
** writing CGI programs in C, we typically want to output a lot of HTML
** text to standard output.  In pure C code, this involves doing a
** printf() with a big string containing all that text.  But we have
** to insert special codes (ex: \n and \") for many common characters,
** which interferes with the readability of the HTML.
**
** This tool allows us to put raw HTML, without the special codes, in
** the middle of a C program.  This program then translates the text
** into standard C by inserting all necessary backslashes and other
** punctuation.
**
** Enhancement #1:
**
** If the last non-whitespace character prior to the first "@" of a
** @-block is "=" or "," then the @-block is a string literal initializer
** rather than text that is to be output via cgi_printf().  Render it
** as such.
**
** Enhancement #2:
**
** Comments of the form:  "|* @-comment: CC" (where "|" is really "/")
** cause CC to become a comment character for the @-substitution.
** Typical values for CC are "--" (for SQL text) or "#" (for Tcl script)
** or "//" (for C++ code).  Lines of subsequent @-blocks that begin with
** CC are omitted from the output.
**
** Enhancement #3:
**
** If a non-enhancement #1 line ends in backslash, the backslash and the
** newline (\n) are not included in the argument to cgi_printf().  This
** is used to split one long output line across multiple source lines.
*/
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*
** Space to hold arguments at the end of the cgi_printf()
*/
#define MX_ARG_SP 10000
static char zArg[MX_ARG_SP];
static int nArg = 0;

/*
** True if we are currently in a cgi_printf()
*/
static int inPrint = 0;

/*
** True if we are currently doing a free string
*/
static int inStr = 0;

/*
** Terminate an active cgi_printf() or free string
*/
static void end_block(FILE *out){
  if( inPrint ){
    zArg[nArg] = 0;
    fprintf(out, "%s);\n", zArg);
    nArg = 0;
    inPrint = 0;
  }
}

/*
** Translate the input stream into the output stream
*/
static void trans(FILE *in, FILE *out){
  int i, j, k;          /* Loop counters */
  char c1, c2;          /* Characters used to start a comment */
  int lastWasEq = 0;    /* True if last non-whitespace character was "=" */
  int lastWasComma = 0; /* True if last non-whitespace character was "," */
  char zLine[2000];     /* A single line of input */
  char zOut[4000];      /* The input line translated into appropriate output */

  c1 = c2 = '-';
  while( fgets(zLine, sizeof(zLine), in) ){
    for(i=0; zLine[i] && isspace(zLine[i]); i++){}
    if( zLine[i]!='@' ){
      if( inPrint || inStr ) end_block(out);
      fprintf(out,"%s",zLine);
                       /* 0123456789 12345 */
      if( strncmp(zLine, "/* @-comment: ", 14)==0 ){
        c1 = zLine[14];
        c2 = zLine[15];
      }
      i += strlen(&zLine[i]);
      while( i>0 && isspace(zLine[i-1]) ){ i--; }
      lastWasEq    = i>0 && zLine[i-1]=='=';
      lastWasComma = i>0 && zLine[i-1]==',';
    }else if( lastWasEq || lastWasComma){
      /* If the last non-whitespace character before the first @ was
      ** an "="(var init/set) or a ","(const definition in list) then
      ** generate a string literal.  But skip comments
      ** consisting of all text between c1 and c2 (default "--")
      ** and end of line.
      */
      int indent, omitline;
      i++;
      if( isspace(zLine[i]) ){ i++; }
      indent = i - 2;
      if( indent<0 ) indent = 0;
      omitline = 0;
      for(j=0; zLine[i] && zLine[i]!='\r' && zLine[i]!='\n'; i++){
        if( zLine[i]==c1 && (c2==' ' || zLine[i+1]==c2) ){
           omitline = 1; break;
        }
        if( zLine[i]=='"' || zLine[i]=='\\' ){ zOut[j++] = '\\'; }
        zOut[j++] = zLine[i];
      }
      while( j>0 && isspace(zOut[j-1]) ){ j--; }
      zOut[j] = 0;
      if( j<=0 && omitline ){
        fprintf(out,"\n");
      }else{
        fprintf(out,"%*s\"%s\\n\"\n",indent, "", zOut);
      }
    }else{
      /* Otherwise (if the last non-whitespace was not '=') then generate
      ** a cgi_printf() statement whose format is the text following the '@'.
      ** Substrings of the form "%C(...)" (where C is any sequence of
      ** characters other than \000 and '(') will put "%C" in the
      ** format and add the "(...)" as an argument to the cgi_printf call.
      */
      const char *zNewline = "\\n";
      int indent;
      int nC;
      char c;
      i++;
      if( isspace(zLine[i]) ){ i++; }
      indent = i;
      for(j=0; zLine[i] && zLine[i]!='\r' && zLine[i]!='\n'; i++){
        if( zLine[i]=='\\' && (!zLine[i+1] || zLine[i+1]=='\r'
                                           || zLine[i+1]=='\n') ){
          zNewline = "";
          break;
        }
        if( zLine[i]=='"' || zLine[i]=='\\' ){ zOut[j++] = '\\'; }
        zOut[j++] = zLine[i];
        if( zLine[i]!='%' || zLine[i+1]=='%' || zLine[i+1]==0 ) continue;
        for(nC=1; zLine[i+nC] && zLine[i+nC]!='('; nC++){}
        if( zLine[i+nC]!='(' || !isalpha(zLine[i+nC-1]) ) continue;
        while( --nC ) zOut[j++] = zLine[++i];
        zArg[nArg++] = ',';
        k = 0; i++;
        while( (c = zLine[i])!=0 ){
          zArg[nArg++] = c;
          if( c==')' ){
            k--;
            if( k==0 ) break;
          }else if( c=='(' ){
            k++;
          }
          i++;
        }
      }
      zOut[j] = 0;
      if( !inPrint ){
        fprintf(out,"%*scgi_printf(\"%s%s\"",indent-2,"", zOut, zNewline);
        inPrint = 1;
      }else{
        fprintf(out,"\n%*s\"%s%s\"",indent+5, "", zOut, zNewline);
      }
    }
  }
}

int main(int argc, char **argv){
  if( argc==2 ){
    char *arg;
    FILE *in = fopen(argv[1], "r");
    if( in==0 ){
      fprintf(stderr,"can not open %s\n", argv[1]);
      exit(1);
    }
    printf("#line 1 \"");
    for(arg=argv[1]; *arg; arg++){
      if( *arg!='\\' ){
        printf("%c", *arg);
      }else{
        printf("\\\\");
      }
    }
    printf("\"\n");
    trans(in, stdout);
    fclose(in);
  }else{
    trans(stdin, stdout);
  }
  return 0;
}
