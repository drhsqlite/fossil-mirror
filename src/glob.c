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
** Commas and whitespace are considered to be element delimters.  Each
** element of the GLOB list may optionally be enclosed in either '...' or
** "...".  This allows commas and/or whitespace to be used in the elements
** themselves.
**
** This routine makes no effort to free the memory space it uses, which
** currently consists of a blob object and its contents.
*/
char *glob_expr(const char *zVal, const char *zGlobList){
  Blob expr;
  const char *zSep = "(";
  int nTerm = 0;
  int i;
  int cTerm;

  if( zGlobList==0 || zGlobList[0]==0 ) return fossil_strdup("0");
  blob_zero(&expr);
  while( zGlobList[0] ){
    while( fossil_isspace(zGlobList[0]) || zGlobList[0]==',' ){
      zGlobList++;  /* Skip leading commas, spaces, and newlines */
    }
    if( zGlobList[0]==0 ) break;
    if( zGlobList[0]=='\'' || zGlobList[0]=='"' ){
      cTerm = zGlobList[0];
      zGlobList++;
    }else{
      cTerm = ',';
    }
    /* Find the next delimter (or the end of the string). */
    for(i=0; zGlobList[i] && zGlobList[i]!=cTerm; i++){
      if( cTerm!=',' ) continue; /* If quoted, keep going. */
      if( fossil_isspace(zGlobList[i]) ) break; /* If space, stop. */
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
    return fossil_strdup("0");
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
** zPatternList is a comma-separated list of glob patterns.  Parse up
** that list and use it to create a new Glob object.
**
** Elements of the glob list may be optionally enclosed in single our
** double-quotes.  This allows a comma to be part of a glob pattern.
**
** Leading and trailing spaces on unquoted glob patterns are ignored.
**
** An empty or null pattern list results in a null glob, which will
** match nothing.
*/
Glob *glob_create(const char *zPatternList){
  int nList;         /* Size of zPatternList in bytes */
  int i;             /* Loop counters */
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
    while( fossil_isspace(z[0]) || z[0]==',' ){
      z++;  /* Skip leading commas, spaces, and newlines */
    }
    if( z[0]==0 ) break;
    if( z[0]=='\'' || z[0]=='"' ){
      delimiter = z[0];
      z++;
    }else{
      delimiter = ',';
    }
    p->azPattern = fossil_realloc(p->azPattern, (p->nPattern+1)*sizeof(char*) );
    p->azPattern[p->nPattern++] = z;
    /* Find the next delimter (or the end of the string). */
    for(i=0; z[i] && z[i]!=delimiter; i++){
      if( delimiter!=',' ) continue; /* If quoted, keep going. */
      if( fossil_isspace(z[i]) ) break; /* If space, stop. */
    }
    if( z[i]==0 ) break;
    z[i] = 0;
    z += i+1;
  }
  return p;
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
    if( sqlite3_strglob(pGlob->azPattern[i], zString)==0 ) return i+1;
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
** PATTERN is a comma- and whitespace-separated list of optionally
** quoted glob patterns.  Show which of the STRINGs that follow match
** the PATTERN.
**
** If PATTERN begins with "@" the rest of the pattern is understood
** to be a setting name (such as binary-glob, crln-glob, or encoding-glob)
** and the value of that setting is used as the actually glob pattern.
*/
void glob_test_cmd(void){
  Glob *pGlob;
  int i;
  char *zPattern;
  if( g.argc<4 ) usage("PATTERN STRING ...");
  zPattern = g.argv[2];
  if( zPattern[0]=='@' ){
    db_find_and_open_repository(OPEN_ANY_SCHEMA,0);
    zPattern = db_get(zPattern+1, 0);
    if( zPattern==0 ) fossil_fatal("no such setting: %s", g.argv[2]+1);
    fossil_print("GLOB pattern: %s\n", zPattern);
  }
  fossil_print("SQL expression: %s\n", glob_expr("x", zPattern));
  pGlob = glob_create(zPattern);
  for(i=0; i<pGlob->nPattern; i++){
    fossil_print("pattern[%d] = [%s]\n", i, pGlob->azPattern[i]);
  }
  for(i=3; i<g.argc; i++){
    fossil_print("%d %s\n", glob_match(pGlob, g.argv[i]), g.argv[i]);
  }
  glob_free(pGlob);
}
