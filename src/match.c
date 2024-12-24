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
** This file contains code to implement string comparisons using a
** variety of algorithm.  The comparison algorithm can be any of:
**
**      MS_EXACT            The string must exactly match the pattern.
**
**      MS_BRLIST           The pattern is a space- and/or comma-separated
**                          list of strings, any one of which may match
**                          the input string.
**
**      MS_GLOB             Like BRLIST, except each component of the pattern
**                          is a GLOB expression.
**
**      MS_LIKE             Like BRLIST, except each component of the pattern
**                          is an SQL LIKE expression.
**
**      MS_REGEXP           Like BRLIST, except each component of the pattern
**                          is a regular expression.
**     
*/
#include "config.h"
#include <string.h>
#include "match.h"

#if INTERFACE
/*
** Types of comparisons that we are able to perform:
*/
typedef enum {
  MS_EXACT=1,   /* Exact string comparison */
  MS_GLOB=2,    /* Matches against a list of GLOB patterns. */
  MS_LIKE=3,    /* Matches against a list of LIKE patterns. */
  MS_REGEXP=4,  /* Matches against a list of regular expressions. */
  MS_BRLIST=5,  /* Matches any element of a list */
} MatchStyle;

/*
** The following object represents a precompiled pattern to use for
** string matching.
**
**    *  Create an instance of this object using match_create().
**    *  Do comparisons using match_text().
**    *  Destroy using match_free() when you are done.
**
*/
struct Matcher {
  MatchStyle style;   /* Which algorithm to use */
  int nPattern;       /* How many patterns are their */
  char **azPattern;   /* List of patterns */
  ReCompiled **aRe;   /* List of compiled regular expressions */
};
  
#endif /*INTERFACE*/

/*
** Translate a "match style" text name into the MS_* enum value.
** Return eDflt if no match is found.
*/
MatchStyle match_style(const char *zStyle, MatchStyle eDflt){
  if( zStyle==0 )                           return eDflt;
  if( fossil_stricmp(zStyle, "brlist")==0 ) return MS_BRLIST;
  if( fossil_stricmp(zStyle, "list")==0 )   return MS_BRLIST;
  if( fossil_stricmp(zStyle, "regexp")==0 ) return MS_REGEXP;
  if( fossil_stricmp(zStyle, "re")==0 )     return MS_REGEXP;
  if( fossil_stricmp(zStyle, "glob")==0 )   return MS_GLOB;
  if( fossil_stricmp(zStyle, "like")==0 )   return MS_LIKE;
  if( fossil_stricmp(zStyle, "exact")==0 )  return MS_EXACT;
  return eDflt;
}


/*
** Create a new Matcher object using the pattern provided.
*/
Matcher *match_create(MatchStyle style, const char *zPat){
  char cDel;         /* Delimiter character */
  int i;             /* Loop counter */
  Matcher *p;        /* The new Matcher to be constructed */
  char *zOne;        /* One element of the pattern */

  if( zPat==0 ) return 0;
  p = fossil_malloc( sizeof(*p) );
  memset(p, 0, sizeof(*p));
  p->style = style;

  if( style==MS_EXACT ){
    p->nPattern = 1;
    p->azPattern = fossil_malloc( sizeof(p->azPattern[0]) );
    p->azPattern[0] = fossil_strdup(zPat);
    return p;
  }

  while( 1 ){
    /* Skip leading delimiters. */
    for( ; fossil_isspace(*zPat) || *zPat==','; ++zPat );

    /* Next non-delimiter character determines quoting. */
    if( zPat[0]==0 ){
      /* Terminate loop at end of string. */
      break;
    }else if( zPat[0]=='\'' || zPat[0]=='"' ){
      /* If word is quoted, prepare to stop at end quote. */
      cDel = zPat[0];
      ++zPat;
    }else{
      /* If word is not quoted, prepare to stop at delimiter. */
      cDel = ',';
    }

    /* Find the next delimiter character or end of string. */
    for( i=0; zPat[i] && zPat[i]!=cDel; ++i ){
      /* If delimiter is comma, also recognize spaces as delimiters. */
      if( cDel==',' && fossil_isspace(zPat[i]) ){
        break;
      }

      /* In regexp mode, ignore delimiters following backslashes. */
      if( style==MS_REGEXP && zPat[i]=='\\' && zPat[i+1] ){
        ++i;
      }
    }

    /* zOne is a zero-terminated copy of the pattern, without delimiters */
    zOne = fossil_strndup(zPat, i);
    zPat += i;
    if( zPat[0] ) zPat++;

    /* Check for regular expression syntax errors. */
    if( style==MS_REGEXP ){
      ReCompiled *regexp;
      const char *zFail = re_compile(&regexp, zOne, 0);
      if( zFail ){
        re_free(regexp);
        continue;
      }
      p->nPattern++;
      p->aRe = fossil_realloc(p->aRe, sizeof(p->aRe)*p->nPattern);
      p->aRe[p->nPattern-1] = regexp;
      fossil_free(zOne);
    }else{
      p->nPattern++;
      p->azPattern = fossil_realloc(p->azPattern, sizeof(char*)*p->nPattern);
      p->azPattern[p->nPattern-1] = zOne;
    }
  }
  return p;
}

/*
** Return non-zero (true) if the input string matches the pattern
** described by the matcher.
**
** The return value is really the 1-based index of the particular
** pattern that matched.
*/
int match_text(Matcher *p, const char *zText){
  int i;
  if( p==0 ){
    return zText==0;
  }
  switch( p->style ){
    case MS_BRLIST:
    case MS_EXACT: {
      for(i=0; i<p->nPattern; i++){
        if( strcmp(p->azPattern[i], zText)==0 ) return i+1;
      }
      break;
    }
    case MS_GLOB: {
      for(i=0; i<p->nPattern; i++){
        if( sqlite3_strglob(p->azPattern[i], zText)==0 ) return i+1;
      }
      break;
    }
    case MS_LIKE: {
      for(i=0; i<p->nPattern; i++){
        if( sqlite3_strlike(p->azPattern[i], zText, 0)==0 ) return i+1;
      }
      break;
    }
    case MS_REGEXP: {
      int nText = (int)strlen(zText);
      for(i=0; i<p->nPattern; i++){
        if( re_match(p->aRe[i], (const u8*)zText, nText) ) return i+1;
      }
      break;
    }
  }
  return 0;
}


/*
** Destroy a previously allocated Matcher object.
*/
void match_free(Matcher *p){
  int i;
  if( p==0 ) return;
  if( p->style==MS_REGEXP ){
    for(i=0; i<p->nPattern; i++) re_free(p->aRe[i]);
    fossil_free(p->aRe);
  }else{
    for(i=0; i<p->nPattern; i++) fossil_free(p->azPattern[i]);
    fossil_free(p->azPattern);
  }
  memset(p, 0, sizeof(*p));
  fossil_free(p);
}



/*
** Quote a tag string by surrounding it with double quotes and preceding
** internal double quotes and backslashes with backslashes.
*/
static const char *tagQuote(
   int len,         /* Maximum length of zTag, or negative for unlimited */
   const char *zTag /* Tag string */
){
  Blob blob = BLOB_INITIALIZER;
  int i, j;
  blob_zero(&blob);
  blob_append(&blob, "\"", 1);
  for( i=j=0; zTag[j] && (len<0 || j<len); ++j ){
    if( zTag[j]=='\"' || zTag[j]=='\\' ){
      if( j>i ){
        blob_append(&blob, zTag+i, j-i);
      }
      blob_append(&blob, "\\", 1);
      i = j;
    }
  }
  if( j>i ){
    blob_append(&blob, zTag+i, j-i);
  }
  blob_append(&blob, "\"", 1);
  return blob_str(&blob);
}

/*
** Construct the  SQL expression that goes into the WHERE clause of a join
** that involves the TAG table and that selects a particular tag out of
** that table.
**
** This function is adapted from glob_expr() to support the MS_EXACT, MS_GLOB,
** MS_LIKE, MS_REGEXP, and MS_BRLIST match styles.
**
** For MS_EXACT, the returned expression
** checks for integer match against the tag ID which is looked up directly by
** this function.  For the other modes, the returned SQL expression performs
** string comparisons against the tag names, so it is necessary to join against
** the tag table to access the "tagname" column.
**
** Each pattern is adjusted to to start with "sym-" and be anchored at end.
**
** In MS_REGEXP mode, backslash can be used to protect delimiter characters.
** The backslashes are not removed from the regular expression.
**
** In addition to assembling and returning an SQL expression, this function
** makes an English-language description of the patterns being matched, suitable
** for display in the web interface.
**
** If any errors arise during processing, *zError is set to an error message.
** Otherwise it is set to NULL.
*/
const char *match_tag_sqlexpr(
  MatchStyle matchStyle,        /* Match style code */
  const char *zTag,             /* Tag name, match pattern, or pattern list */
  const char **zDesc,           /* Output expression description string */
  const char **zError           /* Output error string */
){
  Blob expr = BLOB_INITIALIZER; /* SQL expression string assembly buffer */
  Blob desc = BLOB_INITIALIZER; /* English description of match patterns */
  Blob err = BLOB_INITIALIZER;  /* Error text assembly buffer */
  const char *zStart;           /* Text at start of expression */
  const char *zDelimiter;       /* Text between expression terms */
  const char *zEnd;             /* Text at end of expression */
  const char *zPrefix;          /* Text before each match pattern */
  const char *zSuffix;          /* Text after each match pattern */
  const char *zIntro;           /* Text introducing pattern description */
  const char *zPattern = 0;     /* Previous quoted pattern */
  const char *zFail = 0;        /* Current failure message or NULL if okay */
  const char *zOr = " or ";     /* Text before final quoted pattern */
  char cDel;                    /* Input delimiter character */
  int i;                        /* Input match pattern length counter */

  /* Optimize exact matches by looking up the ID in advance to create a simple
   * numeric comparison.  Bypass the remainder of this function. */
  if( matchStyle==MS_EXACT ){
    *zDesc = tagQuote(-1, zTag);
    return mprintf("(tagid=%d)", db_int(-1,
        "SELECT tagid FROM tag WHERE tagname='sym-%q'", zTag));
  }

  /* Decide pattern prefix and suffix strings according to match style. */
  if( matchStyle==MS_GLOB ){
    zStart = "(";
    zDelimiter = " OR ";
    zEnd = ")";
    zPrefix = "tagname GLOB 'sym-";
    zSuffix = "'";
    zIntro = "glob pattern ";
  }else if( matchStyle==MS_LIKE ){
    zStart = "(";
    zDelimiter = " OR ";
    zEnd = ")";
    zPrefix = "tagname LIKE 'sym-";
    zSuffix = "'";
    zIntro = "SQL LIKE pattern ";
  }else if( matchStyle==MS_REGEXP ){
    zStart = "(tagname REGEXP '^sym-(";
    zDelimiter = "|";
    zEnd = ")$')";
    zPrefix = "";
    zSuffix = "";
    zIntro = "regular expression ";
  }else/* if( matchStyle==MS_BRLIST )*/{
    zStart = "tagname IN ('sym-";
    zDelimiter = "','sym-";
    zEnd = "')";
    zPrefix = "";
    zSuffix = "";
    zIntro = "";
  }

  /* Convert the list of matches into an SQL expression and text description. */
  blob_zero(&expr);
  blob_zero(&desc);
  blob_zero(&err);
  while( 1 ){
    /* Skip leading delimiters. */
    for( ; fossil_isspace(*zTag) || *zTag==','; ++zTag );

    /* Next non-delimiter character determines quoting. */
    if( !*zTag ){
      /* Terminate loop at end of string. */
      break;
    }else if( *zTag=='\'' || *zTag=='"' ){
      /* If word is quoted, prepare to stop at end quote. */
      cDel = *zTag;
      ++zTag;
    }else{
      /* If word is not quoted, prepare to stop at delimiter. */
      cDel = ',';
    }

    /* Find the next delimiter character or end of string. */
    for( i=0; zTag[i] && zTag[i]!=cDel; ++i ){
      /* If delimiter is comma, also recognize spaces as delimiters. */
      if( cDel==',' && fossil_isspace(zTag[i]) ){
        break;
      }

      /* In regexp mode, ignore delimiters following backslashes. */
      if( matchStyle==MS_REGEXP && zTag[i]=='\\' && zTag[i+1] ){
        ++i;
      }
    }

    /* Check for regular expression syntax errors. */
    if( matchStyle==MS_REGEXP ){
      ReCompiled *regexp;
      char *zTagDup = fossil_strndup(zTag, i);
      zFail = re_compile(&regexp, zTagDup, 0);
      re_free(regexp);
      fossil_free(zTagDup);
    }

    /* Process success and error results. */
    if( !zFail ){
      /* Incorporate the match word into the output expression.  %q is used to
       * protect against SQL injection attacks by replacing ' with ''. */
      blob_appendf(&expr, "%s%s%#q%s", blob_size(&expr) ? zDelimiter : zStart,
          zPrefix, i, zTag, zSuffix);

      /* Build up the description string. */
      if( !blob_size(&desc) ){
        /* First tag: start with intro followed by first quoted tag. */
        blob_append(&desc, zIntro, -1);
        blob_append(&desc, tagQuote(i, zTag), -1);
      }else{
        if( zPattern ){
          /* Third and subsequent tags: append comma then previous tag. */
          blob_append(&desc, ", ", 2);
          blob_append(&desc, zPattern, -1);
          zOr = ", or ";
        }

        /* Second and subsequent tags: store quoted tag for next iteration. */
        zPattern = tagQuote(i, zTag);
      }
    }else{
      /* On error, skip the match word and build up the error message buffer. */
      if( !blob_size(&err) ){
        blob_append(&err, "Error: ", 7);
      }else{
        blob_append(&err, ", ", 2);
      }
      blob_appendf(&err, "(%s%s: %s)", zIntro, tagQuote(i, zTag), zFail);
    }

    /* Advance past all consumed input characters. */
    zTag += i;
    if( cDel!=',' && *zTag==cDel ){
      ++zTag;
    }
  }

  /* Finalize and extract the pattern description. */
  if( zPattern ){
    blob_append(&desc, zOr, -1);
    blob_append(&desc, zPattern, -1);
  }
  *zDesc = blob_str(&desc);

  /* Finalize and extract the error text. */
  *zError = blob_size(&err) ? blob_str(&err) : 0;

  /* Finalize and extract the SQL expression. */
  if( blob_size(&expr) ){
    blob_append(&expr, zEnd, -1);
    return blob_str(&expr);
  }

  /* If execution reaches this point, the pattern was empty.  Return NULL. */
  return 0;
}
