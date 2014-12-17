/*
** Copyright (c) 2009 D. Richard Hipp
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
** This file contains code to implement a very simple search function
** against timeline comments, checkin content, wiki pages, and/or tickets.
**
** The search is full-text like in that it is looking for words and ignores
** punctuation and capitalization.  But it is more akin to "grep" in that
** it scans the entire corpus for the search, and it does not support the
** full functionality of FTS4.
*/
#include "config.h"
#include "search.h"
#include <assert.h>

#if INTERFACE
/*
** A compiled search pattern
*/
struct Search {
  int nTerm;            /* Number of search terms */
  struct srchTerm {     /* For each search term */
    char *z;               /* Text */
    int n;                 /* length */
  } a[8];               
};
#endif

/*
** Compile a search pattern
*/
Search *search_init(const char *zPattern){
  int nPattern = strlen(zPattern);
  Search *p;
  char *z;
  int i;

  p = fossil_malloc( nPattern + sizeof(*p) + 1);
  z = (char*)&p[1];
  memcpy(z, zPattern, nPattern+1);
  memset(p, 0, sizeof(*p));
  while( *z && p->nTerm<sizeof(p->a)/sizeof(p->a[0]) ){
    while( !fossil_isalnum(*z) && *z ){ z++; }
    if( *z==0 ) break;
    p->a[p->nTerm].z = z;
    for(i=1; fossil_isalnum(z[i]) || z[i]=='_'; i++){}
    p->a[p->nTerm].n = i;
    z += i;
    p->nTerm++;
  }
  return p;
}


/*
** Destroy a search context.
*/
void search_end(Search *p){
  free(p);
}

/*
** Theses characters constitute a word boundary
*/
static const char isBoundary[] = {
  1, 1, 1, 1, 1, 1, 1, 1,     1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,     1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,     1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 1, 1, 1, 1, 1, 1,
  1, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 1, 1, 1, 1, 0,
  1, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
};

/*
** Compare a search pattern against an input string and return a score.
**
** Scoring:
**   *  All terms must match at least once or the score is zero
**   *  10 bonus points if the first occurrence is an exact match
**   *  1 additional point for each subsequent match of the same word
**   *  Extra points of two consecutive words of the pattern are consecutive
**      in the document
*/
int search_score(Search *p, int nDoc, const char **azDoc){
  int iPrev = 999;
  int score = 10;
  int iBonus = 0;
  int i, j, k;
  const char *zDoc;
  unsigned char seen[8];

  memset(seen, 0, sizeof(seen));
  for(k=0; k<nDoc; k++){
    zDoc = azDoc[k];
    if( zDoc==0 ) continue;
    for(i=0; zDoc[i]; i++){
      char c = zDoc[i];
      if( isBoundary[c&0xff] ) continue;
      for(j=0; j<p->nTerm; j++){
        int n = p->a[j].n;
        if( sqlite3_strnicmp(p->a[j].z, &zDoc[i], n)==0 ){
          score += 1;
          if( !seen[j] ){
            if( isBoundary[zDoc[i+n]&0xff] ) score += 10;
            seen[j] = 1;
          }
          if( j==iPrev+1 ){
            score += iBonus;
          }
          i += n-1;
          iPrev = j;
          iBonus = 50;
          break;
        }
      }
      iBonus /= 2;
      while( !isBoundary[zDoc[i]&0xff] ){ i++; }
    }
  }
  
  /* Every term must be seen or else the score is zero */
  for(j=0; j<p->nTerm; j++){
    if( !seen[j] ) return 0;
  }

  return score;
}

/*
** This is an SQLite function that scores its input using
** a pre-computed pattern.
*/
static void search_score_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  Search *p = (Search*)sqlite3_user_data(context);
  const char **azDoc;
  int score;
  int i;

  azDoc = fossil_malloc( sizeof(const char*)*(argc+1) );
  for(i=0; i<argc; i++) azDoc[i] = (const char*)sqlite3_value_text(argv[i]);
  score = search_score(p, argc, azDoc);
  fossil_free(azDoc);
  sqlite3_result_int(context, score);
}

/*
** Register the "score()" SQL function to score its input text
** using the given Search object.  Once this function is registered,
** do not delete the Search object.
*/
void search_sql_setup(Search *p){
  sqlite3_create_function(g.db, "score", -1, SQLITE_UTF8, p,
     search_score_sqlfunc, 0, 0);
}

/*
** Testing the search function.
**
** COMMAND: test-search*
** %fossil test-search [-all|-a] [-limit|-n #] [-width|-W #] pattern...
**
** Search for timeline entries matching all words
** provided on the command line. Whole-word matches
** scope more highly than partial matches.
**
** Outputs, by default, some top-N fraction of the
** results. The -all option can be used to output
** all matches, regardless of their search score.
** The -limit option can be used to limit the number
** of entries returned.  The -width option can be
** used to set the output width used when printing
** matches.
*/
void search_cmd(void){
  Search *p;
  Blob pattern;
  int i;
  Blob sql = empty_blob;
  Stmt q;
  int iBest;
  char fAll = NULL != find_option("all", "a", 0); /* If set, do not lop
                                                     off the end of the
                                                     results. */
  const char *zLimit = find_option("limit","n",1);
  const char *zWidth = find_option("width","W",1);
  int nLimit = zLimit ? atoi(zLimit) : -1000;   /* Max number of matching
                                                   lines/entries to list */
  int width;
  if( zWidth ){
    width = atoi(zWidth);
    if( (width!=0) && (width<=20) ){
      fossil_fatal("-W|--width value must be >20 or 0");
    }
  }else{
    width = -1;
  }

  db_must_be_within_tree();
  if( g.argc<2 ) return;
  blob_init(&pattern, g.argv[2], -1);
  for(i=3; i<g.argc; i++){
    blob_appendf(&pattern, " %s", g.argv[i]);
  }
  p = search_init(blob_str(&pattern));
  blob_reset(&pattern);
  search_sql_setup(p);

  db_multi_exec(
     "CREATE TEMP TABLE srch(rid,uuid,date,comment,x);"
     "CREATE INDEX srch_idx1 ON srch(x);"
     "INSERT INTO srch(rid,uuid,date,comment,x)"
     "   SELECT blob.rid, uuid, datetime(event.mtime%s),"
     "          coalesce(ecomment,comment),"
     "          score(coalesce(ecomment,comment)) AS y"
     "     FROM event, blob"
     "    WHERE blob.rid=event.objid AND y>0;",
     timeline_utc()
  );
  iBest = db_int(0, "SELECT max(x) FROM srch");
  blob_append(&sql,
              "SELECT rid, uuid, date, comment, 0, 0 FROM srch "
              "WHERE 1 ", -1);
  if(!fAll){
    blob_append_sql(&sql,"AND x>%d ", iBest/3);
  }
  blob_append(&sql, "ORDER BY x DESC, date DESC ", -1);
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  print_timeline(&q, nLimit, width, 0);
  db_finalize(&q);
}
