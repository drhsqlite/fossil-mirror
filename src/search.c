/*
** Copyright (c) 2009 D. Richard Hipp
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
** This file contains code to implement the "/doc" web page and related
** pages.
*/
#include "config.h"
#include "search.h"
#include <assert.h>

#if INTERFACE
/*
** A compiled search patter
*/
struct Search {
  int nTerm;
  struct srchTerm {
    char *z;
    int n;
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

  p = malloc( nPattern + sizeof(*p) + 1);
  if( p==0 ) fossil_panic("out of memory");
  z = (char*)&p[1];
  strcpy(z, zPattern);
  memset(p, 0, sizeof(*p));
  while( *z && p->nTerm<sizeof(p->a)/sizeof(p->a[0]) ){
    while( !isalnum(*z) && *z ){ z++; }
    if( *z==0 ) break;
    p->a[p->nTerm].z = z;
    for(i=1; isalnum(z[i]) || z[i]=='_'; i++){}
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
**   *  10 bonus points if the first occurrance is an exact match
**   *  1 additional point for each subsequent match of the same word
**   *  Extra points of two consecutive words of the pattern are consecutive
**      in the document
*/
int search_score(Search *p, const char *zDoc){
  int iPrev = 999;
  int score = 10;
  int iBonus = 0;
  int i, j;
  unsigned char seen[8];

  memset(seen, 0, sizeof(seen));
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
  int score = search_score(p, (const char*)sqlite3_value_text(argv[0]));
  sqlite3_result_int(context, score);
}

/*
** Register the "score()" SQL function to score its input text
** using the given Search object.  Once this function is registered,
** do not delete the Search object.
*/
void search_sql_setup(Search *p){
  sqlite3_create_function(g.db, "score", 1, SQLITE_UTF8, p,
     search_score_sqlfunc, 0, 0);
}

/*
** Testing the search function.
**
** COMMAND: test-search
** %fossil test-search pattern...
**
** search for check-ins matching the pattern.
*/
void search_test(void){
  Search *p;
  Blob pattern;
  int i;
  Stmt q;

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
     "CREATE TEMP TABLE srch(x,text);"
     "INSERT INTO srch(text) SELECT coalesce(ecomment,comment) FROM event;"
     "UPDATE srch SET x=score(text);"
  );
  db_prepare(&q, "SELECT x, text FROM srch WHERE x>0 ORDER BY x DESC");
  while( db_step(&q)==SQLITE_ROW ){
    int score = db_column_int(&q, 0);
    const char *z = db_column_text(&q, 1);

    score = search_score(p, z);
    if( score ){
      printf("%5d: %s\n", score, z);
    }
  }
  db_finalize(&q);
}
