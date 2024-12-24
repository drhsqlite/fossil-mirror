/*
** Copyright (c) 2002 D. Richard Hipp
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
** Name of files being processed
*/
static const char *zInFile = "(stdin)";

/*
** The `fossil_isspace()' function copied from the Fossil source code.
** Some MSVC runtime library versions of `isspace()' break with an `assert()' if
** the input is smaller than -1 or greater than 255 in debug builds, due to sign
** extension when promoting `signed char' to `int' for non-ASCII characters. Use
** an `isspace()' replacement instead of explicit type casts to `unsigned char'.
*/
int fossil_isspace(char c){
  return c==' ' || (c<='\r' && c>='\t');
}

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
  int lineNo = 0;       /* Line number */
  char zLine[2000];     /* A single line of input */
  char zOut[4000];      /* The input line translated into appropriate output */

  c1 = c2 = '-';
  while( fgets(zLine, sizeof(zLine), in) ){
    lineNo++;
    for(i=0; zLine[i] && fossil_isspace(zLine[i]); i++){}
    if( zLine[i]!='@' ){
      if( inPrint || inStr ) end_block(out);
      fprintf(out,"%s",zLine);
                       /* 0123456789 12345 */
      if( strncmp(zLine, "/* @-comment: ", 14)==0 ){
        c1 = zLine[14];
        c2 = zLine[15];
      }
      i += strlen(&zLine[i]);
      while( i>0 && fossil_isspace(zLine[i-1]) ){ i--; }
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
      char *zNewline = "\\n";
      i++;
      if( fossil_isspace(zLine[i]) ){ i++; }
      indent = i - 2;
      if( indent<0 ) indent = 0;
      omitline = 0;
      for(j=0; zLine[i] && zLine[i]!='\r' && zLine[i]!='\n'; i++){
        if( zLine[i]==c1 && (c2==' ' || zLine[i+1]==c2) ){
           omitline = 1; break;
        }
        if( zLine[i]=='\\' && (zLine[i+1]==0 || zLine[i+1]=='\r'
                                 || zLine[i+1]=='\n') ){
          zLine[i] = 0;
          zNewline = "";
          /* fprintf(stderr, "%s:%d: omit newline\n", zInFile, lineNo); */
          break;
        }
        if( zLine[i]=='\\' || zLine[i]=='"' ){ zOut[j++] = '\\'; }
        zOut[j++] = zLine[i];
      }
      if( zNewline[0] ) while( j>0 && fossil_isspace(zOut[j-1]) ){ j--; }
      zOut[j] = 0;
      if( j<=0 && omitline ){
        fprintf(out,"\n");
      }else{
        fprintf(out,"%*s\"%s%s\"\n",indent, "", zOut, zNewline);
      }
    }else{
      /* Otherwise (if the last non-whitespace was not '=') then generate a
      ** cgi_printf() statement whose format is the text following the '@'.
      ** Substrings of the form "%C(...)" (where C is any sequence of characters
      ** other than \000 and '(') will put "%C" in the format and add the
      ** "(...)" as an argument to the cgi_printf call.  Each '*' character
      ** present in C (max two) causes one more "(...)" sequence to be consumed.
      ** For example, "%*.*d(4)(2)(1)" converts to "%*.*d" with arguments "4",
      ** "2", and "1", which will be used as the field width, precision, and
      ** value, respectively, producing a final formatted result of "  01".
      */
      const char *zNewline = "\\n";
      int indent;
      int nC;
      int nParam;
      char c;
      i++;
      if( fossil_isspace(zLine[i]) ){ i++; }
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
        nParam=1;
        for(nC=1; zLine[i+nC] && zLine[i+nC]!='('; nC++){
          if( zLine[i+nC]=='*' && nParam < 3 ) nParam++;
        }
        if( zLine[i+nC]!='(' || !isalpha(zLine[i+nC-1]) ) continue;
        while( --nC ) zOut[j++] = zLine[++i];
        do{
          zArg[nArg++] = ',';
          k = 0; i++;
          if( zLine[i]!='(' ) break;
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
        }while( --nParam );
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

static void print_source_ref(const char *zSrcFile, FILE *out){
/* Set source line reference to the original source file.
 * This makes compiler show the original file name in the compile error
 * messages, instead of referring to the translated file.
 * NOTE: This somewhat complicates stepping in debugger, as the resuling
 * code would not match the referenced sources.
 */
#ifndef FOSSIL_DEBUG
  const char *arg;
  if( !*zSrcFile ){
    return;
  }
  fprintf(out,"#line 1 \"");
  for(arg=zSrcFile; *arg; arg++){
    if( *arg!='\\' ){
      fprintf(out,"%c", *arg);
    }else{
      fprintf(out,"\\\\");
    }
  }
  fprintf(out,"\"\n");
#endif
}

int main(int argc, char **argv){
  if( argc==2 ){
    FILE *in = fopen(argv[1], "r");
    if( in==0 ){
      fprintf(stderr,"can not open %s\n", argv[1]);
      exit(1);
    }
    zInFile = argv[1];
    print_source_ref(zInFile, stdout);
    trans(in, stdout);
    fclose(in);
  }else{
    trans(stdin, stdout);
  }
  return 0;
}
