/*
** Copyright (c) 2011 D. Richard Hipp
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
** This file contains code used to pattern matching using "glob" syntax.
*/
#include "config.h"
#include "glob.h"
#include <assert.h>

/*
** Construct and return a string which is an SQL expression that will
** be TRUE if value zVal matches any of the GLOB expressions in the list
** zGlobList.  For example:
**
**    zVal:       "x"
**    zGlobList:  "*.o,*.obj"
**
**    Result:     "(x GLOB '*.o' OR x GLOB '*.obj')"
**
** Each element of the GLOB list may optionally be enclosed in either '...'
** or "...".  This allows commas in the expression.  Whitespace at the
** beginning and end of each GLOB pattern is ignored, except when enclosed
** within '...' or "...".
**
** This routine makes no effort to free the memory space it uses.
*/
char *glob_expr(const char *zVal, const char *zGlobList){
  Blob expr;
  char *zSep = "(";
  int nTerm = 0;
  int i;
  int cTerm;

  if( zGlobList==0 || zGlobList[0]==0 ) return "0";
  blob_zero(&expr);
  while( zGlobList[0] ){
    while( fossil_isspace(zGlobList[0]) || zGlobList[0]==',' ) zGlobList++;
    if( zGlobList[0]==0 ) break;
    if( zGlobList[0]=='\'' || zGlobList[0]=='"' ){
      cTerm = zGlobList[0];
      zGlobList++;
    }else{
      cTerm = ',';
    }
    for(i=0; zGlobList[i] && zGlobList[i]!=cTerm && zGlobList[i]!='\n'; i++){}
    if( cTerm==',' ){
      while( i>0 && fossil_isspace(zGlobList[i-1]) ){ i--; }
    }
    blob_appendf(&expr, "%s%s GLOB '%#q'", zSep, zVal, i, zGlobList);
    zSep = " OR ";
    if( cTerm!=',' && zGlobList[i] ) i++;
    zGlobList += i;
    if( zGlobList[0] ) zGlobList++;
    nTerm++;
  }
  if( nTerm ){
    blob_appendf(&expr, ")");
    return blob_str(&expr);
  }else{
    return "0";
  }
}

#if INTERFACE
/*
** A Glob object holds a set of patterns read to be matched against
** a string.
*/
struct Glob {
  int nPattern;        /* Number of patterns */
  char **azPattern;    /* Array of pointers to patterns */
};
#endif /* INTERFACE */

/*
** zPatternList is a comma-separate list of glob patterns.  Parse up
** that list and use it to create a new Glob object.
**
** Elements of the glob list may be optionally enclosed in single our
** double-quotes.  This allows a comma to be part of a glob.
**
** Leading and trailing spaces on unquoted glob patterns are ignored.
**
** An empty or null pattern list results in a null glob, which will
** match nothing.
*/
Glob *glob_create(const char *zPatternList){
  int nList;         /* Size of zPatternList in bytes */
  int i, j;          /* Loop counters */
  Glob *p;           /* The glob being created */
  char *z;           /* Copy of the pattern list */
  char delimiter;    /* '\'' or '\"' or 0 */

  if( zPatternList==0 || zPatternList[0]==0 ) return 0;
  nList = strlen(zPatternList);
  p = fossil_malloc( sizeof(*p) + nList+1 );
  memset(p, 0, sizeof(*p));
  z = (char*)&p[1];
  memcpy(z, zPatternList, nList+1);
  while( z[0] ){
    while( z[0]==',' || z[0]==' ' || z[0]=='\n' || z[0]=='\r' ){
      z++;  /* Skip leading spaces and newlines */
    }
    if( z[0]=='\'' || z[0]=='"' ){
      delimiter = z[0];
      z++;
    }else{
      delimiter = ',';
    }
    if( z[0]==0 ) break;
    p->azPattern = fossil_realloc(p->azPattern, (p->nPattern+1)*sizeof(char*) );
    p->azPattern[p->nPattern++] = z;
    for(i=0; z[i] && z[i]!=delimiter && z[i]!='\n' && z[i]!='\r'; i++){}
    if( delimiter==',' ){
      /* Remove trailing spaces / newlines on a comma-delimited pattern */
      for(j=i; j>1 && (z[j-1]==' ' || z[j-1]=='\n' || z[j-1]=='\r'); j--){}
      if( j<i ) z[j] = 0;
    }
    if( z[i]==0 ) break;
    z[i] = 0;
    z += i+1;
  }
  return p;
}

/*
** Return non-zero if string z matches glob pattern zGlob and zero if the
** pattern does not match.
**
** Globbing rules:
**
**      '*'       Matches any sequence of zero or more characters.
**
**      '?'       Matches exactly one character.
**
**     [...]      Matches one character from the enclosed list of
**                characters.
**
**     [^...]     Matches one character not in the enclosed list.
*/
int strglob(const char *zGlob, const char *z){
  int c, c2;
  int invert;
  int seen;

  while( (c = (*(zGlob++)))!=0 ){
    if( c=='*' ){
      while( (c=(*(zGlob++))) == '*' || c=='?' ){
        if( c=='?' && (*(z++))==0 ) return 0;
      }
      if( c==0 ){
        return 1;
      }else if( c=='[' ){
        while( *z && strglob(zGlob-1,z)==0 ){
          z++;
        }
        return (*z)!=0;
      }
      while( (c2 = (*(z++)))!=0 ){
        while( c2!=c ){
          c2 = *(z++);
          if( c2==0 ) return 0;
        }
        if( strglob(zGlob,z) ) return 1;
      }
      return 0;
    }else if( c=='?' ){
      if( (*(z++))==0 ) return 0;
    }else if( c=='[' ){
      int prior_c = 0;
      seen = 0;
      invert = 0;
      c = *(z++);
      if( c==0 ) return 0;
      c2 = *(zGlob++);
      if( c2=='^' ){
        invert = 1;
        c2 = *(zGlob++);
      }
      if( c2==']' ){
        if( c==']' ) seen = 1;
        c2 = *(zGlob++);
      }
      while( c2 && c2!=']' ){
        if( c2=='-' && zGlob[0]!=']' && zGlob[0]!=0 && prior_c>0 ){
          c2 = *(zGlob++);
          if( c>=prior_c && c<=c2 ) seen = 1;
          prior_c = 0;
        }else{
          if( c==c2 ){
            seen = 1;
          }
          prior_c = c2;
        }
        c2 = *(zGlob++);
      }
      if( c2==0 || (seen ^ invert)==0 ) return 0;
    }else{
      if( c!=(*(z++)) ) return 0;
    }
  }
  return *z==0;
}

/*
** Return true (non-zero) if zString matches any of the patterns in
** the Glob.  The value returned is actually a 1-based index of the pattern
** that matched.  Return 0 if none of the patterns match zString.
**
** A NULL glob matches nothing.
*/
int glob_match(Glob *pGlob, const char *zString){
  int i;
  if( pGlob==0 ) return 0;
  for(i=0; i<pGlob->nPattern; i++){
    if( strglob(pGlob->azPattern[i], zString) ) return i+1;
  }
  return 0;
}

/*
** Free all memory associated with the given Glob object
*/
void glob_free(Glob *pGlob){
  if( pGlob ){
    fossil_free(pGlob->azPattern);
    fossil_free(pGlob);
  }
}

/*
** COMMAND: test-glob
**
** Usage:  %fossil test-glob PATTERN STRING...
**
** PATTERN is a comma-separated list of glob patterns.  Show which of
** the STRINGs that follow match the PATTERN.
*/
void glob_test_cmd(void){
  Glob *pGlob;
  int i;
  if( g.argc<4 ) usage("PATTERN STRING ...");
  fossil_print("SQL expression: %s\n", glob_expr("x", g.argv[2]));
  pGlob = glob_create(g.argv[2]);
  for(i=0; i<pGlob->nPattern; i++){
    fossil_print("pattern[%d] = [%s]\n", i, pGlob->azPattern[i]);
  }
  for(i=3; i<g.argc; i++){
    fossil_print("%d %s\n", glob_match(pGlob, g.argv[i]), g.argv[i]);
  }
  glob_free(pGlob);
}
