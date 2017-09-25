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
** This file contains code to implement a search functions
** against timeline comments, check-in content, wiki pages, and/or tickets.
**
** The search can be either a per-query "grep"-like search that scans
** the entire corpus.  Or it can use the FTS4 or FTS5 search engine of
** SQLite.  The choice is a administrator configuration option.
**
** The first option is referred to as "full-scan search".  The second
** option is called "indexed search".
**
** The code in this file is ordered approximately as follows:
**
**    (1) The full-scan search engine
**    (2) The indexed search engine
**    (3) Higher level interfaces that use either (1) or (b2) according
**        to the current search configuration settings
*/
#include "config.h"
#include "search.h"
#include <assert.h>

#if INTERFACE

/* Maximum number of search terms for full-scan search */
#define SEARCH_MAX_TERM   8

/*
** A compiled search pattern used for full-scan search.
*/
struct Search {
  int nTerm;            /* Number of search terms */
  struct srchTerm {     /* For each search term */
    char *z;               /* Text */
    int n;                 /* length */
  } a[SEARCH_MAX_TERM];
  /* Snippet controls */
  char *zPattern;       /* The search pattern */
  char *zMarkBegin;     /* Start of a match */
  char *zMarkEnd;       /* End of a match */
  char *zMarkGap;       /* A gap between two matches */
  unsigned fSrchFlg;    /* Flags */
  int iScore;           /* Score of the last match attempt */
  Blob snip;            /* Snippet for the most recent match */
};

#define SRCHFLG_HTML    0x01   /* Escape snippet text for HTML */
#define SRCHFLG_STATIC  0x04   /* The static gSearch object */

#endif

/*
** There is a single global Search object:
*/
static Search gSearch;


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
#define ISALNUM(x)  (!isBoundary[(x)&0xff])


/*
** Destroy a full-scan search context.
*/
void search_end(Search *p){
  if( p ){
    fossil_free(p->zPattern);
    fossil_free(p->zMarkBegin);
    fossil_free(p->zMarkEnd);
    fossil_free(p->zMarkGap);
    if( p->iScore ) blob_reset(&p->snip);
    memset(p, 0, sizeof(*p));
    if( p!=&gSearch ) fossil_free(p);
  }
}

/*
** Compile a full-scan search pattern
*/
static Search *search_init(
  const char *zPattern,       /* The search pattern */
  const char *zMarkBegin,     /* Start of a match */
  const char *zMarkEnd,       /* End of a match */
  const char *zMarkGap,       /* A gap between two matches */
  unsigned fSrchFlg           /* Flags */
){
  Search *p;
  char *z;
  int i;

  if( fSrchFlg & SRCHFLG_STATIC ){
    p = &gSearch;
    search_end(p);
  }else{
    p = fossil_malloc(sizeof(*p));
    memset(p, 0, sizeof(*p));
  }
  p->zPattern = z = mprintf("%s", zPattern);
  p->zMarkBegin = mprintf("%s", zMarkBegin);
  p->zMarkEnd = mprintf("%s", zMarkEnd);
  p->zMarkGap = mprintf("%s", zMarkGap);
  p->fSrchFlg = fSrchFlg;
  blob_init(&p->snip, 0, 0);
  while( *z && p->nTerm<SEARCH_MAX_TERM ){
    while( *z && !ISALNUM(*z) ){ z++; }
    if( *z==0 ) break;
    p->a[p->nTerm].z = z;
    for(i=1; ISALNUM(z[i]); i++){}
    p->a[p->nTerm].n = i;
    z += i;
    p->nTerm++;
  }
  return p;
}


/*
** Append n bytes of text to snippet zTxt.  Encode the text appropriately.
*/
static void snippet_text_append(
  Search *p,             /* The search context */
  Blob *pSnip,           /* Append to this snippet */
  const char *zTxt,      /* Text to append */
  int n                  /* How many bytes to append */
){
  if( n>0 ){
    if( p->fSrchFlg & SRCHFLG_HTML ){
      blob_appendf(pSnip, "%#h", n, zTxt);
    }else{
      blob_append(pSnip, zTxt, n);
    }
  }
}

/* This the core search engine for full-scan search.
**
** Compare a search pattern against one or more input strings which
** collectively comprise a document.  Return a match score.  Any
** postive value means there was a match.  Zero means that one or
** more terms are missing.
**
** The score and a snippet are record for future use.
**
** Scoring:
**   *  All terms must match at least once or the score is zero
**   *  One point for each matching term
**   *  Extra points if consecutive words of the pattern are consecutive
**      in the document
*/
static int search_match(
  Search *p,              /* Search pattern and flags */
  int nDoc,               /* Number of strings in this document */
  const char **azDoc      /* Text of each string */
){
  int score;                         /* Final score */
  int i;                             /* Offset into current document */
  int ii;                            /* Loop counter */
  int j;                             /* Loop over search terms */
  int k;                             /* Loop over prior terms */
  int iWord = 0;                     /* Current word number */
  int iDoc;                          /* Current document number */
  int wantGap = 0;                   /* True if a zMarkGap is wanted */
  const char *zDoc;                  /* Current document text */
  const int CTX = 50;                /* Amount of snippet context */
  int anMatch[SEARCH_MAX_TERM];      /* Number of terms in best match */
  int aiBestDoc[SEARCH_MAX_TERM];    /* Document containing best match */
  int aiBestOfst[SEARCH_MAX_TERM];   /* Byte offset to start of best match */
  int aiLastDoc[SEARCH_MAX_TERM];    /* Document containing most recent match */
  int aiLastOfst[SEARCH_MAX_TERM];   /* Byte offset to the most recent match */
  int aiWordIdx[SEARCH_MAX_TERM];    /* Word index of most recent match */

  memset(anMatch, 0, sizeof(anMatch));
  memset(aiWordIdx, 0xff, sizeof(aiWordIdx));
  for(iDoc=0; iDoc<nDoc; iDoc++){
    zDoc = azDoc[iDoc];
    if( zDoc==0 ) continue;
    iWord++;
    for(i=0; zDoc[i]; i++){
      if( !ISALNUM(zDoc[i]) ) continue;
      iWord++;
      for(j=0; j<p->nTerm; j++){
        int n = p->a[j].n;
        if( sqlite3_strnicmp(p->a[j].z, &zDoc[i], n)==0
         && (!ISALNUM(zDoc[i+n]) || p->a[j].z[n]=='*')
        ){
          aiWordIdx[j] = iWord;
          aiLastDoc[j] = iDoc;
          aiLastOfst[j] = i;
          for(k=1; j-k>=0 && anMatch[j-k] && aiWordIdx[j-k]==iWord-k; k++){}
          for(ii=0; ii<k; ii++){
            if( anMatch[j-ii]<k ){
              anMatch[j-ii] = k*(nDoc-iDoc);
              aiBestDoc[j-ii] = aiLastDoc[j-ii];
              aiBestOfst[j-ii] = aiLastOfst[j-ii];
            }
          }
          break;
        }
      }
      while( ISALNUM(zDoc[i]) ){ i++; }
      if( zDoc[i]==0 ) break;
    }
  }

  /* Finished search all documents.
  ** Every term must be seen or else the score is zero
  */
  score = 1;
  for(j=0; j<p->nTerm; j++) score *= anMatch[j];
  blob_reset(&p->snip);
  p->iScore = score;
  if( score==0 ) return score;


  /* Prepare a snippet that describes the matching text.
  */
  while(1){
    int iOfst;
    int iTail;
    int iBest;
    for(ii=0; ii<p->nTerm && anMatch[ii]==0; ii++){}
    if( ii>=p->nTerm ) break;  /* This is where the loop exits */
    iBest = ii;
    iDoc = aiBestDoc[ii];
    iOfst = aiBestOfst[ii];
    for(; ii<p->nTerm; ii++){
      if( anMatch[ii]==0 ) continue;
      if( aiBestDoc[ii]>iDoc ) continue;
      if( aiBestOfst[ii]>iOfst ) continue;
      iDoc = aiBestDoc[ii];
      iOfst = aiBestOfst[ii];
      iBest = ii;
    }
    iTail = iOfst + p->a[iBest].n;
    anMatch[iBest] = 0;
    for(ii=0; ii<p->nTerm; ii++){
      if( anMatch[ii]==0 ) continue;
      if( aiBestDoc[ii]!=iDoc ) continue;
      if( aiBestOfst[ii]<=iTail+CTX*2 ){
        if( iTail<aiBestOfst[ii]+p->a[ii].n ){
          iTail = aiBestOfst[ii]+p->a[ii].n;
        }
        anMatch[ii] = 0;
        ii = -1;
        continue;
      }
    }
    zDoc = azDoc[iDoc];
    iOfst -= CTX;
    if( iOfst<0 ) iOfst = 0;
    while( iOfst>0 && ISALNUM(zDoc[iOfst-1]) ) iOfst--;
    while( zDoc[iOfst] && !ISALNUM(zDoc[iOfst]) ) iOfst++;
    for(ii=0; ii<CTX && zDoc[iTail]; ii++, iTail++){}
    while( ISALNUM(zDoc[iTail]) ) iTail++;
    if( iOfst>0 || wantGap ) blob_append(&p->snip, p->zMarkGap, -1);
    wantGap = zDoc[iTail]!=0;
    zDoc += iOfst;
    iTail -= iOfst;

    /* Add a snippet segment using characters iOfst..iOfst+iTail from zDoc */
    for(i=0; i<iTail; i++){
      if( !ISALNUM(zDoc[i]) ) continue;
      for(j=0; j<p->nTerm; j++){
        int n = p->a[j].n;
        if( sqlite3_strnicmp(p->a[j].z, &zDoc[i], n)==0
         && (!ISALNUM(zDoc[i+n]) || p->a[j].z[n]=='*')
        ){
          snippet_text_append(p, &p->snip, zDoc, i);
          zDoc += i;
          iTail -= i;
          blob_append(&p->snip, p->zMarkBegin, -1);
          if( p->a[j].z[n]=='*' ){
            while( ISALNUM(zDoc[n]) ) n++;
          }
          snippet_text_append(p, &p->snip, zDoc, n);
          zDoc += n;
          iTail -= n;
          blob_append(&p->snip, p->zMarkEnd, -1);
          i = -1;
          break;
        } /* end-if */
      } /* end for(j) */
      if( j<p->nTerm ){
        while( ISALNUM(zDoc[i]) && i<iTail ){ i++; }
      }
    } /* end for(i) */
    snippet_text_append(p, &p->snip, zDoc, iTail);
  }
  if( wantGap ) blob_append(&p->snip, p->zMarkGap, -1);
  return score;
}

/*
** COMMAND: test-match
**
** Usage: %fossil test-match SEARCHSTRING FILE1 FILE2 ...
**
** Run the full-scan search algorithm using SEARCHSTRING against
** the text of the files listed.  Output matches and snippets.
*/
void test_match_cmd(void){
  Search *p;
  int i;
  Blob x;
  int score;
  char *zDoc;
  int flg = 0;
  char *zBegin = (char*)find_option("begin",0,1);
  char *zEnd = (char*)find_option("end",0,1);
  char *zGap = (char*)find_option("gap",0,1);
  if( find_option("html",0,0)!=0 ) flg |= SRCHFLG_HTML;
  if( find_option("static",0,0)!=0 ) flg |= SRCHFLG_STATIC;
  verify_all_options();
  if( g.argc<4 ) usage("SEARCHSTRING FILE1...");
  if( zBegin==0 ) zBegin = "[[";
  if( zEnd==0 ) zEnd = "]]";
  if( zGap==0 ) zGap = " ... ";
  p = search_init(g.argv[2], zBegin, zEnd, zGap, flg);
  for(i=3; i<g.argc; i++){
    blob_read_from_file(&x, g.argv[i]);
    zDoc = blob_str(&x);
    score = search_match(p, 1, (const char**)&zDoc);
    fossil_print("%s: %d\n", g.argv[i], p->iScore);
    blob_reset(&x);
    if( score ){
      fossil_print("%.78c\n%s\n%.78c\n\n", '=', blob_str(&p->snip), '=');
    }
  }
  search_end(p);
}

/*
** An SQL function to initialize the full-scan search pattern:
**
**     search_init(PATTERN,BEGIN,END,GAP,FLAGS)
**
** All arguments are optional.  PATTERN is the search pattern.  If it
** is omitted, then the global search pattern is reset.  BEGIN and END
** and GAP are the strings used to construct snippets.  FLAGS is an
** integer bit pattern containing the various SRCH_CKIN, SRCH_DOC,
** SRCH_TKT, or SRCH_ALL bits to determine what is to be searched.
*/
static void search_init_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zPattern = 0;
  const char *zBegin = "<mark>";
  const char *zEnd = "</mark>";
  const char *zGap = " ... ";
  unsigned int flg = SRCHFLG_HTML;
  switch( argc ){
    default:
      flg = (unsigned int)sqlite3_value_int(argv[4]);
    case 4:
      zGap = (const char*)sqlite3_value_text(argv[3]);
    case 3:
      zEnd = (const char*)sqlite3_value_text(argv[2]);
    case 2:
      zBegin = (const char*)sqlite3_value_text(argv[1]);
    case 1:
      zPattern = (const char*)sqlite3_value_text(argv[0]);
  }
  if( zPattern && zPattern[0] ){
    search_init(zPattern, zBegin, zEnd, zGap, flg | SRCHFLG_STATIC);
  }else{
    search_end(&gSearch);
  }
}

/*     search_match(TEXT, TEXT, ....)
**
** Using the full-scan search engine created by the most recent call
** to search_init(), match the input the TEXT arguments.
** Remember the results global full-scan search object.
** Return non-zero on a match and zero on a miss.
*/
static void search_match_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *azDoc[5];
  int nDoc;
  int rc;
  for(nDoc=0; nDoc<count(azDoc) && nDoc<argc; nDoc++){
    azDoc[nDoc] = (const char*)sqlite3_value_text(argv[nDoc]);
    if( azDoc[nDoc]==0 ) azDoc[nDoc] = "";
  }
  rc = search_match(&gSearch, nDoc, azDoc);
  sqlite3_result_int(context, rc);
}


/*      search_score()
**
** Return the match score for the last successful search_match call.
*/
static void search_score_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  sqlite3_result_int(context, gSearch.iScore);
}

/*      search_snippet()
**
** Return a snippet for the last successful search_match() call.
*/
static void search_snippet_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  if( blob_size(&gSearch.snip)>0 ){
    sqlite3_result_text(context, blob_str(&gSearch.snip), -1, fossil_free);
    blob_init(&gSearch.snip, 0, 0);
  }
}

/*       stext(TYPE, RID, ARG)
**
** This is an SQLite function that computes the searchable text.
** It is a wrapper around the search_stext() routine.  See the
** search_stext() routine for further detail.
*/
static void search_stext_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zType = (const char*)sqlite3_value_text(argv[0]);
  int rid = sqlite3_value_int(argv[1]);
  const char *zName = (const char*)sqlite3_value_text(argv[2]);
  sqlite3_result_text(context, search_stext_cached(zType[0],rid,zName,0), -1,
                      SQLITE_TRANSIENT);
}

/*       title(TYPE, RID, ARG)
**
** Return the title of the document to be search.
*/
static void search_title_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zType = (const char*)sqlite3_value_text(argv[0]);
  int rid = sqlite3_value_int(argv[1]);
  const char *zName = (const char*)sqlite3_value_text(argv[2]);
  int nHdr = 0;
  char *z = search_stext_cached(zType[0], rid, zName, &nHdr);
  if( nHdr || zType[0]!='d' ){
    sqlite3_result_text(context, z, nHdr, SQLITE_TRANSIENT);
  }else{
    sqlite3_result_value(context, argv[2]);
  }
}

/*       body(TYPE, RID, ARG)
**
** Return the body of the document to be search.
*/
static void search_body_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zType = (const char*)sqlite3_value_text(argv[0]);
  int rid = sqlite3_value_int(argv[1]);
  const char *zName = (const char*)sqlite3_value_text(argv[2]);
  int nHdr = 0;
  char *z = search_stext_cached(zType[0], rid, zName, &nHdr);
  sqlite3_result_text(context, z+nHdr+1, -1, SQLITE_TRANSIENT);
}

/*      urlencode(X)
**
** Encode a string for use as a query parameter in a URL.  This is
** the equivalent of printf("%T",X).
*/
static void search_urlencode_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  char *z = mprintf("%T",sqlite3_value_text(argv[0]));
  sqlite3_result_text(context, z, -1, fossil_free);
}

/*
** Register the various SQL functions (defined above) needed to implement
** full-scan search.
*/
void search_sql_setup(sqlite3 *db){
  static int once = 0;
  if( once++ ) return;
  sqlite3_create_function(db, "search_match", -1, SQLITE_UTF8, 0,
     search_match_sqlfunc, 0, 0);
  sqlite3_create_function(db, "search_score", 0, SQLITE_UTF8, 0,
     search_score_sqlfunc, 0, 0);
  sqlite3_create_function(db, "search_snippet", 0, SQLITE_UTF8, 0,
     search_snippet_sqlfunc, 0, 0);
  sqlite3_create_function(db, "search_init", -1, SQLITE_UTF8, 0,
     search_init_sqlfunc, 0, 0);
  sqlite3_create_function(db, "stext", 3, SQLITE_UTF8, 0,
     search_stext_sqlfunc, 0, 0);
  sqlite3_create_function(db, "title", 3, SQLITE_UTF8, 0,
     search_title_sqlfunc, 0, 0);
  sqlite3_create_function(db, "body", 3, SQLITE_UTF8, 0,
     search_body_sqlfunc, 0, 0);
  sqlite3_create_function(db, "urlencode", 1, SQLITE_UTF8, 0,
     search_urlencode_sqlfunc, 0, 0);
}

/*
** Testing the search function.
**
** COMMAND: search*
**
** Usage: %fossil search [-all|-a] [-limit|-n #] [-width|-W #] pattern...
**
** Search for timeline entries matching all words provided on the
** command line. Whole-word matches scope more highly than partial
** matches.
**
** Outputs, by default, some top-N fraction of the results. The -all
** option can be used to output all matches, regardless of their search
** score.  The -limit option can be used to limit the number of entries
** returned.  The -width option can be used to set the output width used
** when printing matches.
**
** Options:
**
**     -a|--all          Output all matches, not just best matches.
**     -n|--limit N      Limit output to N matches.
**     -W|--width WIDTH  Set display width to WIDTH columns, 0 for
**                       unlimited. Defaults the terminal's width.
*/
void search_cmd(void){
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

  db_find_and_open_repository(0, 0);
  if( g.argc<3 ) return;
  blob_init(&pattern, g.argv[2], -1);
  for(i=3; i<g.argc; i++){
    blob_appendf(&pattern, " %s", g.argv[i]);
  }
  (void)search_init(blob_str(&pattern),"*","*","...",SRCHFLG_STATIC);
  blob_reset(&pattern);
  search_sql_setup(g.db);

  db_multi_exec(
     "CREATE TEMP TABLE srch(rid,uuid,date,comment,x);"
     "CREATE INDEX srch_idx1 ON srch(x);"
     "INSERT INTO srch(rid,uuid,date,comment,x)"
     "   SELECT blob.rid, uuid, datetime(event.mtime,toLocal()),"
     "          coalesce(ecomment,comment),"
     "          search_score()"
     "     FROM event, blob"
     "    WHERE blob.rid=event.objid"
     "      AND search_match(coalesce(ecomment,comment));"
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

#if INTERFACE
/* What to search for */
#define SRCH_CKIN     0x0001    /* Search over check-in comments */
#define SRCH_DOC      0x0002    /* Search over embedded documents */
#define SRCH_TKT      0x0004    /* Search over tickets */
#define SRCH_WIKI     0x0008    /* Search over wiki */
#define SRCH_TECHNOTE 0x0010    /* Search over tech notes */
#define SRCH_ALL      0x001f    /* Search over everything */
#endif

/*
** Remove bits from srchFlags which are disallowed by either the
** current server configuration or by user permissions.
*/
unsigned int search_restrict(unsigned int srchFlags){
  static unsigned int knownGood = 0;
  static unsigned int knownBad = 0;
  static const struct { unsigned m; const char *zKey; } aSetng[] = {
     { SRCH_CKIN,     "search-ci"   },
     { SRCH_DOC,      "search-doc"  },
     { SRCH_TKT,      "search-tkt"  },
     { SRCH_WIKI,     "search-wiki" },
     { SRCH_TECHNOTE, "search-technote" },
  };
  int i;
  if( g.perm.Read==0 )   srchFlags &= ~(SRCH_CKIN|SRCH_DOC|SRCH_TECHNOTE);
  if( g.perm.RdTkt==0 )  srchFlags &= ~(SRCH_TKT);
  if( g.perm.RdWiki==0 ) srchFlags &= ~(SRCH_WIKI);
  for(i=0; i<count(aSetng); i++){
    unsigned int m = aSetng[i].m;
    if( (srchFlags & m)==0 ) continue;
    if( ((knownGood|knownBad) & m)!=0 ) continue;
    if( db_get_boolean(aSetng[i].zKey,0) ){
      knownGood |= m;
    }else{
      knownBad |= m;
    }
  }
  return srchFlags & ~knownBad;
}

/*
** When this routine is called, there already exists a table
**
**       x(label,url,score,id,snip).
**
** label:  The "name" of the document containing the match
** url:    A URL for the document
** score:  How well the document matched
** id:     The document id.  Format: xNNNNN, x: type, N: number
** snip:   A snippet for the match
**
** And the srchFlags parameter has been validated.  This routine
** fills the X table with search results using a full-scan search.
**
** The companion indexed search routine is search_indexed().
*/
static void search_fullscan(
  const char *zPattern,       /* The query pattern */
  unsigned int srchFlags      /* What to search over */
){
  search_init(zPattern, "<mark>", "</mark>", " ... ",
          SRCHFLG_STATIC|SRCHFLG_HTML);
  if( (srchFlags & SRCH_DOC)!=0 ){
    char *zDocGlob = db_get("doc-glob","");
    char *zDocBr = db_get("doc-branch","trunk");
    if( zDocGlob && zDocGlob[0] && zDocBr && zDocBr[0] ){
      db_multi_exec(
        "CREATE VIRTUAL TABLE IF NOT EXISTS temp.foci USING files_of_checkin;"
      );
      db_multi_exec(
        "INSERT INTO x(label,url,score,id,date,snip)"
        "  SELECT printf('Document: %%s',title('d',blob.rid,foci.filename)),"
        "         printf('/doc/%T/%%s',foci.filename),"
        "         search_score(),"
        "         'd'||blob.rid,"
        "         (SELECT datetime(event.mtime) FROM event"
        "            WHERE objid=symbolic_name_to_rid('trunk')),"
        "         search_snippet()"
        "    FROM foci CROSS JOIN blob"
        "   WHERE checkinID=symbolic_name_to_rid('trunk')"
        "     AND blob.uuid=foci.uuid"
        "     AND search_match(title('d',blob.rid,foci.filename),"
        "                      body('d',blob.rid,foci.filename))"
        "     AND %z",
        zDocBr, glob_expr("foci.filename", zDocGlob)
      );
    }
  }
  if( (srchFlags & SRCH_WIKI)!=0 ){
    db_multi_exec(
      "WITH wiki(name,rid,mtime) AS ("
      "  SELECT substr(tagname,6), tagxref.rid, max(tagxref.mtime)"
      "    FROM tag, tagxref"
      "   WHERE tag.tagname GLOB 'wiki-*'"
      "     AND tagxref.tagid=tag.tagid"
      "   GROUP BY 1"
      ")"
      "INSERT INTO x(label,url,score,id,date,snip)"
      "  SELECT printf('Wiki: %%s',name),"
      "         printf('/wiki?name=%%s',urlencode(name)),"
      "         search_score(),"
      "         'w'||rid,"
      "         datetime(mtime),"
      "         search_snippet()"
      "    FROM wiki"
      "   WHERE search_match(title('w',rid,name),body('w',rid,name));"
    );
  }
  if( (srchFlags & SRCH_CKIN)!=0 ){
    db_multi_exec(
      "WITH ckin(uuid,rid,mtime) AS ("
      "  SELECT blob.uuid, event.objid, event.mtime"
      "    FROM event, blob"
      "   WHERE event.type='ci'"
      "     AND blob.rid=event.objid"
      ")"
      "INSERT INTO x(label,url,score,id,date,snip)"
      "  SELECT printf('Check-in [%%.10s] on %%s',uuid,datetime(mtime)),"
      "         printf('/timeline?c=%%s&n=8&y=ci',uuid),"
      "         search_score(),"
      "         'c'||rid,"
      "         datetime(mtime),"
      "         search_snippet()"
      "    FROM ckin"
      "   WHERE search_match('',body('c',rid,NULL));"
    );
  }
  if( (srchFlags & SRCH_TKT)!=0 ){
    db_multi_exec(
      "INSERT INTO x(label,url,score,id,date,snip)"
      "  SELECT printf('Ticket: %%s (%%s)',title('t',tkt_id,NULL),"
                      "datetime(tkt_mtime)),"
      "         printf('/tktview/%%.20s',tkt_uuid),"
      "         search_score(),"
      "         't'||tkt_id,"
      "         datetime(tkt_mtime),"
      "         search_snippet()"
      "    FROM ticket"
      "   WHERE search_match(title('t',tkt_id,NULL),body('t',tkt_id,NULL));"
    );
  }
  if( (srchFlags & SRCH_TECHNOTE)!=0 ){
    db_multi_exec(
      "WITH technote(uuid,rid,mtime) AS ("
      "  SELECT substr(tagname,7), tagxref.rid, max(tagxref.mtime)"
      "    FROM tag, tagxref"
      "   WHERE tag.tagname GLOB 'event-*'"
      "     AND tagxref.tagid=tag.tagid"
      "   GROUP BY 1"
      ")"
      "INSERT INTO x(label,url,score,id,date,snip)"
      "  SELECT printf('Tech Note: %%s',uuid),"
      "         printf('/technote/%%s',uuid),"
      "         search_score(),"
      "         'e'||rid,"
      "         datetime(mtime),"
      "         search_snippet()"
      "    FROM technote"
      "   WHERE search_match('',body('e',rid,NULL));"
    );
  }
}

/*
** Number of significant bits in a u32
*/
static int nbits(u32 x){
  int n = 0;
  while( x ){ n++; x >>= 1; }
  return n;
}

/*
** Implemenation of the rank() function used with rank(matchinfo(*,'pcsx')).
*/
static void search_rank_sqlfunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned *aVal = (unsigned int*)sqlite3_value_blob(argv[0]);
  int nVal = sqlite3_value_bytes(argv[0])/4;
  int nCol;           /* Number of columns in the index */
  int nTerm;          /* Number of search terms in the query */
  int i, j;           /* Loop counter */
  double r = 0.0;     /* Score */
  const unsigned *aX, *aS;

  if( nVal<2 ) return;
  nTerm = aVal[0];
  nCol = aVal[1];
  if( nVal<2+3*nCol*nTerm+nCol ) return;
  aS = aVal+2;
  aX = aS+nCol;
  for(j=0; j<nCol; j++){
    double x;
    if( aS[j]>0 ){
      x = 0.0;
      for(i=0; i<nTerm; i++){
        int hits_this_row;
        int hits_all_rows;
        int rows_with_hit;
        double avg_hits_per_row;

        hits_this_row = aX[j + i*nCol*3];
        if( hits_this_row==0 )continue;
        hits_all_rows = aX[j + i*nCol*3 + 1];
        rows_with_hit = aX[j + i*nCol*3 + 2];
        if( rows_with_hit==0 ) continue;
        avg_hits_per_row = hits_all_rows/(double)rows_with_hit;
        x += hits_this_row/(avg_hits_per_row*nbits(rows_with_hit));
      }
      x *= (1<<((30*(aS[j]-1))/nTerm));
    }else{
      x = 0.0;
    }
    r = r*10.0 + x;
  }
#define SEARCH_DEBUG_RANK 0
#if SEARCH_DEBUG_RANK
  {
    Blob x;
    blob_init(&x,0,0);
    blob_appendf(&x,"%08x", (int)r);
    for(i=0; i<nVal; i++){
      blob_appendf(&x," %d", aVal[i]);
    }
    blob_appendf(&x," r=%g", r);
    sqlite3_result_text(context, blob_str(&x), -1, fossil_free);
  }
#else
  sqlite3_result_double(context, r);
#endif
}

/*
** When this routine is called, there already exists a table
**
**       x(label,url,score,id,snip).
**
** label:  The "name" of the document containing the match
** url:    A URL for the document
** score:  How well the document matched
** id:     The document id.  Format: xNNNNN, x: type, N: number
** snip:   A snippet for the match
**
** And the srchFlags parameter has been validated.  This routine
** fills the X table with search results using FTS indexed search.
**
** The companion full-scan search routine is search_fullscan().
*/
static void search_indexed(
  const char *zPattern,       /* The query pattern */
  unsigned int srchFlags      /* What to search over */
){
  Blob sql;
  if( srchFlags==0 ) return;
  sqlite3_create_function(g.db, "rank", 1, SQLITE_UTF8, 0,
     search_rank_sqlfunc, 0, 0);
  blob_init(&sql, 0, 0);
  blob_appendf(&sql,
    "INSERT INTO x(label,url,score,id,date,snip) "
    " SELECT ftsdocs.label,"
    "        ftsdocs.url,"
    "        rank(matchinfo(ftsidx,'pcsx')),"
    "        ftsdocs.type || ftsdocs.rid,"
    "        datetime(ftsdocs.mtime),"
    "        snippet(ftsidx,'<mark>','</mark>',' ... ',-1,35)"
    "   FROM ftsidx CROSS JOIN ftsdocs"
    "  WHERE ftsidx MATCH %Q"
    "    AND ftsdocs.rowid=ftsidx.docid",
    zPattern
  );
  if( srchFlags!=SRCH_ALL ){
    const char *zSep = " AND (";
    static const struct { unsigned m; char c; } aMask[] = {
       { SRCH_CKIN,     'c' },
       { SRCH_DOC,      'd' },
       { SRCH_TKT,      't' },
       { SRCH_WIKI,     'w' },
       { SRCH_TECHNOTE, 'e' },
    };
    int i;
    for(i=0; i<count(aMask); i++){
      if( srchFlags & aMask[i].m ){
        blob_appendf(&sql, "%sftsdocs.type='%c'", zSep, aMask[i].c);
        zSep = " OR ";
      }
    }
    blob_append(&sql,")",1);
  }
  db_multi_exec("%s",blob_str(&sql)/*safe-for-%s*/);
#if SEARCH_DEBUG_RANK
  db_multi_exec("UPDATE x SET label=printf('%%s (score=%%s)',label,score)");
#endif
}

/*
** If z[] is of the form "<mark>TEXT</mark>" where TEXT contains
** no white-space or punctuation, then return the length of the mark.
*/
static int isSnippetMark(const char *z){
  int n;
  if( strncmp(z,"<mark>",6)!=0 ) return 0;
  n = 6;
  while( fossil_isalnum(z[n]) ) n++;
  if( strncmp(&z[n],"</mark>",7)!=0 ) return 0;
  return n+7;
}

/*
** Return a copy of zSnip (in memory obtained from fossil_malloc()) that
** has all "<" characters, other than those on <mark> and </mark>,
** converted into "&lt;".  This is similar to htmlize() except that
** <mark> and </mark> are preserved.
*/
static char *cleanSnippet(const char *zSnip){
  int i;
  int n = 0;
  char *z;
  for(i=0; zSnip[i]; i++) if( zSnip[i]=='<' ) n++;
  z = fossil_malloc( i+n*4+1 );
  i = 0;
  while( zSnip[0] ){
    if( zSnip[0]=='<' ){
      n = isSnippetMark(zSnip);
      if( n ){
        memcpy(&z[i], zSnip, n);
        zSnip += n;
        i += n;
        continue;
      }else{
        memcpy(&z[i], "&lt;", 4);
        i += 4;
        zSnip++;
      }
    }else{
      z[i++] = zSnip[0];
      zSnip++;
    }
  }
  z[i] = 0;
  return z;
}


/*
** This routine generates web-page output for a search operation.
** Other web-pages can invoke this routine to add search results
** in the middle of the page.
**
** This routine works for both full-scan and indexed search.  The
** appropriate low-level search routine is called according to the
** current configuration.
**
** Return the number of rows.
*/
int search_run_and_output(
  const char *zPattern,       /* The query pattern */
  unsigned int srchFlags,     /* What to search over */
  int fDebug                  /* Extra debugging output */
){
  Stmt q;
  int nRow = 0;

  srchFlags = search_restrict(srchFlags);
  if( srchFlags==0 ) return 0;
  search_sql_setup(g.db);
  add_content_sql_commands(g.db);
  db_multi_exec(
    "CREATE TEMP TABLE x(label,url,score,id,date,snip);"
  );
  if( !search_index_exists() ){
    search_fullscan(zPattern, srchFlags);  /* Full-scan search */
  }else{
    search_update_index(srchFlags);        /* Update the index, if necessary */
    search_indexed(zPattern, srchFlags);   /* Indexed search */
  }
  db_prepare(&q, "SELECT url, snip, label, score, id"
                 "  FROM x"
                 " ORDER BY score DESC, date DESC;");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUrl = db_column_text(&q, 0);
    const char *zSnippet = db_column_text(&q, 1);
    const char *zLabel = db_column_text(&q, 2);
    if( nRow==0 ){
      @ <ol>
    }
    nRow++;
    @ <li><p><a href='%R%s(zUrl)'>%h(zLabel)</a>
    if( fDebug ){
      @ (%e(db_column_double(&q,3)), %s(db_column_text(&q,4))
    }
    @ <br /><span class='snippet'>%z(cleanSnippet(zSnippet))</span></li>
  }
  db_finalize(&q);
  if( nRow ){
    @ </ol>
  }
  return nRow;
}

/*
** Generate some HTML for doing search.  At a minimum include the
** Search-Text entry form.  If the "s" query parameter is present, also
** show search results.
**
** The srchFlags parameter restricts the set of documents to be searched.
** srchFlags should normally be either a single search category or all
** categories.  Any srchFlags with two or more bits set
** is treated like SRCH_ALL for display purposes.
**
** This routine automatically restricts srchFlag according to user
** permissions and the server configuration.  The entry box is shown
** disabled if srchFlags is 0 after these restrictions are applied.
**
** If useYparam is true, then this routine also looks at the y= query
** parameter for further search restrictions.
*/
void search_screen(unsigned srchFlags, int useYparam){
  const char *zType = 0;
  const char *zClass = 0;
  const char *zDisable1;
  const char *zDisable2;
  const char *zPattern;
  int fDebug = PB("debug");
  srchFlags = search_restrict(srchFlags);
  switch( srchFlags ){
    case SRCH_CKIN:     zType = " Check-ins";  zClass = "Ckin"; break;
    case SRCH_DOC:      zType = " Docs";       zClass = "Doc";  break;
    case SRCH_TKT:      zType = " Tickets";    zClass = "Tkt";  break;
    case SRCH_WIKI:     zType = " Wiki";       zClass = "Wiki"; break;
    case SRCH_TECHNOTE: zType = " Tech Notes"; zClass = "Note"; break;
  }
  if( srchFlags==0 ){
    zDisable1 = " disabled";
    zDisable2 = " disabled";
    zPattern = "";
  }else{
    zDisable1 = " autofocus";
    zDisable2 = "";
    zPattern = PD("s","");
  }
  @ <form method='GET' action='%R/%T(g.zPath)'>
  if( zClass ){
    @ <div class='searchForm searchForm%s(zClass)'>
  }else{
    @ <div class='searchForm'>
  }
  @ <input type="text" name="s" size="40" value="%h(zPattern)"%s(zDisable1)>
  if( useYparam && (srchFlags & (srchFlags-1))!=0 && useYparam ){
    static const struct { char *z; char *zNm; unsigned m; } aY[] = {
       { "all",  "All",        SRCH_ALL      },
       { "c",    "Check-ins",  SRCH_CKIN     },
       { "d",    "Docs",       SRCH_DOC      },
       { "t",    "Tickets",    SRCH_TKT      },
       { "w",    "Wiki",       SRCH_WIKI     },
       { "e",    "Tech Notes", SRCH_TECHNOTE },
    };
    const char *zY = PD("y","all");
    unsigned newFlags = srchFlags;
    int i;
    @ <select size='1' name='y'>
    for(i=0; i<count(aY); i++){
      if( (aY[i].m & srchFlags)==0 ) continue;
      cgi_printf("<option value='%s'", aY[i].z);
      if( fossil_strcmp(zY,aY[i].z)==0 ){
        newFlags &= aY[i].m;
        cgi_printf(" selected");
      }
      cgi_printf(">%s</option>\n", aY[i].zNm);
    }
    @ </select>
    srchFlags = newFlags;
  }
  if( fDebug ){
    @ <input type="hidden" name="debug" value="1">
  }
  @ <input type="submit" value="Search%s(zType)"%s(zDisable2)>
  if( srchFlags==0 ){
    @ <p class="generalError">Search is disabled</p>
  }
  @ </div></form>
  while( fossil_isspace(zPattern[0]) ) zPattern++;
  if( zPattern[0] ){
    if( zClass ){
      @ <div class='searchResult searchResult%s(zClass)'>
    }else{
      @ <div class='searchResult'>
    }
    if( search_run_and_output(zPattern, srchFlags, fDebug)==0 ){
      @ <p class='searchEmpty'>No matches for: <span>%h(zPattern)</span></p>
    }
    @ </div>
  }
}

/*
** WEBPAGE: search
**
** Search for check-in comments, documents, tickets, or wiki that
** match a user-supplied pattern.
**
**    s=PATTERN       Specify the full-text pattern to search for
**    y=TYPE          What to search.
**                      c -> check-ins
**                      d -> documentation
**                      t -> tickets
**                      w -> wiki
**                      e -> tech notes
**                    all -> everything
*/
void search_page(void){
  login_check_credentials();
  style_header("Search");
  search_screen(SRCH_ALL, 1);
  style_footer();
}


/*
** This is a helper function for search_stext().  Writing into pOut
** the search text obtained from pIn according to zMimetype.
**
** The title of the document is the first line of text.  All subsequent
** lines are the body.  If the document has no title, the first line
** is blank.
*/
static void get_stext_by_mimetype(
  Blob *pIn,
  const char *zMimetype,
  Blob *pOut
){
  Blob html, title;
  blob_init(&html, 0, 0);
  blob_init(&title, 0, 0);
  if( zMimetype==0 ) zMimetype = "text/plain";
  if( fossil_strcmp(zMimetype,"text/x-fossil-wiki")==0 ){
    Blob tail;
    blob_init(&tail, 0, 0);
    if( wiki_find_title(pIn, &title, &tail) ){
      blob_appendf(pOut, "%s\n", blob_str(&title));
      wiki_convert(&tail, &html, 0);
      blob_reset(&tail);
    }else{
      blob_append(pOut, "\n", 1);
      wiki_convert(pIn, &html, 0);
    }
    html_to_plaintext(blob_str(&html), pOut);
  }else if( fossil_strcmp(zMimetype,"text/x-markdown")==0 ){
    markdown_to_html(pIn, &title, &html);
    if( blob_size(&title) ){
      blob_appendf(pOut, "%s\n", blob_str(&title));
    }else{
      blob_append(pOut, "\n", 1);
    }
    html_to_plaintext(blob_str(&html), pOut);
  }else if( fossil_strcmp(zMimetype,"text/html")==0 ){
    if( doc_is_embedded_html(pIn, &title) ){
      blob_appendf(pOut, "%s\n", blob_str(&title));
    }
    html_to_plaintext(blob_str(pIn), pOut);
  }else{
    blob_append(pOut, "\n", 1);
    blob_append(pOut, blob_buffer(pIn), blob_size(pIn));
  }
  blob_reset(&html);
  blob_reset(&title);
}

/*
** Query pQuery is pointing at a single row of output.  Append a text
** representation of every text-compatible column to pAccum.
*/
static void append_all_ticket_fields(Blob *pAccum, Stmt *pQuery, int iTitle){
  int n = db_column_count(pQuery);
  int i;
  const char *zMime = 0;
  if( iTitle>=0 && iTitle<n ){
    if( db_column_type(pQuery,iTitle)==SQLITE_TEXT ){
      blob_append(pAccum, db_column_text(pQuery,iTitle), -1);
    }
    blob_append(pAccum, "\n", 1);
  }
  for(i=0; i<n; i++){
    const char *zColName = db_column_name(pQuery,i);
    int eType = db_column_type(pQuery,i);
    if( i==iTitle ) continue;
    if( fossil_strnicmp(zColName,"tkt_",4)==0 ) continue;
    if( fossil_strnicmp(zColName,"private_",8)==0 ) continue;
    if( eType==SQLITE_BLOB || eType==SQLITE_NULL ) continue;
    if( fossil_stricmp(zColName,"mimetype")==0 ){
      zMime = db_column_text(pQuery,i);
      if( fossil_strcmp(zMime,"text/plain")==0 ) zMime = 0;
    }else if( zMime==0 || eType!=SQLITE_TEXT ){
      blob_appendf(pAccum, "%s: %s |\n", zColName, db_column_text(pQuery,i));
    }else{
      Blob txt;
      blob_init(&txt, db_column_text(pQuery,i), -1);
      blob_appendf(pAccum, "%s: ", zColName);
      get_stext_by_mimetype(&txt, zMime, pAccum);
      blob_append(pAccum, " |", 2);
      blob_reset(&txt);
    }
  }
}


/*
** Return "search text" - a reduced version of a document appropriate for
** full text search and/or for constructing a search result snippet.
**
**    cType:            d      Embedded documentation
**                      w      Wiki page
**                      c      Check-in comment
**                      t      Ticket text
**                      e      Tech note
**
**    rid               The RID of an artifact that defines the object
**                      being searched.
**
**    zName             Name of the object being searched.  This is used
**                      only to help figure out the mimetype (text/plain,
**                      test/html, test/x-fossil-wiki, or text/x-markdown)
**                      so that the code can know how to simplify the text.
*/
void search_stext(
  char cType,            /* Type of document */
  int rid,               /* BLOB.RID or TAG.TAGID value for document */
  const char *zName,     /* Auxiliary information */
  Blob *pOut             /* OUT: Initialize to the search text */
){
  blob_init(pOut, 0, 0);
  switch( cType ){
    case 'd': {   /* Documents */
      Blob doc;
      content_get(rid, &doc);
      blob_to_utf8_no_bom(&doc, 0);
      get_stext_by_mimetype(&doc, mimetype_from_name(zName), pOut);
      blob_reset(&doc);
      break;
    }
    case 'e':     /* Tech Notes */
    case 'w': {   /* Wiki */
      Manifest *pWiki = manifest_get(rid,
          cType == 'e' ? CFTYPE_EVENT : CFTYPE_WIKI, 0);
      Blob wiki;
      if( pWiki==0 ) break;
      blob_init(&wiki, pWiki->zWiki, -1);
      get_stext_by_mimetype(&wiki, wiki_filter_mimetypes(pWiki->zMimetype),
                            pOut);
      blob_reset(&wiki);
      manifest_destroy(pWiki);
      break;
    }
    case 'c': {   /* Check-in Comments */
      static Stmt q;
      static int isPlainText = -1;
      db_static_prepare(&q,
         "SELECT coalesce(ecomment,comment)"
         "  ||' (user: '||coalesce(euser,user,'?')"
         "  ||', tags: '||"
         "  (SELECT group_concat(substr(tag.tagname,5),',')"
         "     FROM tag, tagxref"
         "    WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid"
         "      AND tagxref.rid=event.objid AND tagxref.tagtype>0)"
         "  ||')'"
         "  FROM event WHERE objid=:x AND type='ci'");
      if( isPlainText<0 ){
        isPlainText = db_get_boolean("timeline-plaintext",0);
      }
      db_bind_int(&q, ":x", rid);
      if( db_step(&q)==SQLITE_ROW ){
        blob_append(pOut, "\n", 1);
        if( isPlainText ){
          db_column_blob(&q, 0, pOut);
        }else{
          Blob x;
          blob_init(&x,0,0);
          db_column_blob(&q, 0, &x);
          get_stext_by_mimetype(&x, "text/x-fossil-wiki", pOut);
          blob_reset(&x);
        }
      }
      db_reset(&q);
      break;
    }
    case 't': {   /* Tickets */
      static Stmt q1;
      static int iTitle = -1;
      db_static_prepare(&q1, "SELECT * FROM ticket WHERE tkt_id=:rid");
      db_bind_int(&q1, ":rid", rid);
      if( db_step(&q1)==SQLITE_ROW ){
        if( iTitle<0 ){
          int n = db_column_count(&q1);
          for(iTitle=0; iTitle<n; iTitle++){
            if( fossil_stricmp(db_column_name(&q1,iTitle),"title")==0 ) break;
          }
        }
        append_all_ticket_fields(pOut, &q1, iTitle);
      }
      db_reset(&q1);
      if( db_table_exists("repository","ticketchng") ){
        static Stmt q2;
        db_static_prepare(&q2, "SELECT * FROM ticketchng WHERE tkt_id=:rid"
                               "  ORDER BY tkt_mtime");
        db_bind_int(&q2, ":rid", rid);
        while( db_step(&q2)==SQLITE_ROW ){
          append_all_ticket_fields(pOut, &q2, -1);
        }
        db_reset(&q2);
      }
      break;
    }
  }
}

/*
** This routine is a wrapper around search_stext().
**
** This routine looks up the search text, stores it in an internal
** buffer, and returns a pointer to the text.  Subsequent requests
** for the same document return the same pointer.  The returned pointer
** is valid until the next invocation of this routine.  Call this routine
** with an eType of 0 to clear the cache.
*/
char *search_stext_cached(
  char cType,            /* Type of document */
  int rid,               /* BLOB.RID or TAG.TAGID value for document */
  const char *zName,     /* Auxiliary information, for mimetype */
  int *pnTitle           /* OUT: length of title in bytes excluding \n */
){
  static struct {
    Blob stext;          /* Cached search text */
    char cType;          /* The type */
    int rid;             /* The RID */
    int nTitle;          /* Number of bytes in title */
  } cache;
  int i;
  char *z;
  if( cType!=cache.cType || rid!=cache.rid ){
    if( cache.rid>0 ){
      blob_reset(&cache.stext);
    }else{
      blob_init(&cache.stext,0,0);
    }
    cache.cType = cType;
    cache.rid = rid;
    if( cType==0 ) return 0;
    search_stext(cType, rid, zName, &cache.stext);
    z  = blob_str(&cache.stext);
    for(i=0; z[i] && z[i]!='\n'; i++){}
    cache.nTitle = i;
  }
  if( pnTitle ) *pnTitle = cache.nTitle;
  return blob_str(&cache.stext);
}

/*
** COMMAND: test-search-stext
**
** Usage: fossil test-search-stext TYPE RID NAME
**
** Compute the search text for document TYPE-RID whose name is NAME.
** The TYPE is one of "c", "d", "t", "w", or "e".  The RID is the document
** ID.  The NAME is used to figure out a mimetype to use for formatting
** the raw document text.
*/
void test_search_stext(void){
  Blob out;
  db_find_and_open_repository(0,0);
  if( g.argc!=5 ) usage("TYPE RID NAME");
  search_stext(g.argv[2][0], atoi(g.argv[3]), g.argv[4], &out);
  fossil_print("%s\n",blob_str(&out));
  blob_reset(&out);
}

/*
** COMMAND: test-convert-stext
**
** Usage: fossil test-convert-stext FILE MIMETYPE
**
** Read the content of FILE and convert it to stext according to MIMETYPE.
** Send the result to standard output.
*/
void test_convert_stext(void){
  Blob in, out;
  db_find_and_open_repository(0,0);
  if( g.argc!=4 ) usage("FILENAME MIMETYPE");
  blob_read_from_file(&in, g.argv[2]);
  blob_init(&out, 0, 0);
  get_stext_by_mimetype(&in, g.argv[3], &out);
  fossil_print("%s\n",blob_str(&out));
  blob_reset(&in);
  blob_reset(&out);
}

/* The schema for the full-text index
*/
static const char zFtsSchema[] =
@ -- One entry for each possible search result
@ CREATE TABLE IF NOT EXISTS repository.ftsdocs(
@   rowid INTEGER PRIMARY KEY, -- Maps to the ftsidx.docid
@   type CHAR(1),              -- Type of document
@   rid INTEGER,               -- BLOB.RID or TAG.TAGID for the document
@   name TEXT,                 -- Additional document description
@   idxed BOOLEAN,             -- True if currently in the index
@   label TEXT,                -- Label to print on search results
@   url TEXT,                  -- URL to access this document
@   mtime DATE,                -- Date when document created
@   bx TEXT,                   -- Temporary "body" content cache
@   UNIQUE(type,rid)
@ );
@ CREATE INDEX repository.ftsdocIdxed ON ftsdocs(type,rid,name) WHERE idxed==0;
@ CREATE INDEX repository.ftsdocName ON ftsdocs(name) WHERE type='w';
@ CREATE VIEW IF NOT EXISTS repository.ftscontent AS
@   SELECT rowid, type, rid, name, idxed, label, url, mtime,
@          title(type,rid,name) AS 'title', body(type,rid,name) AS 'body'
@     FROM ftsdocs;
@ CREATE VIRTUAL TABLE IF NOT EXISTS repository.ftsidx
@   USING fts4(content="ftscontent", title, body%s);
;
static const char zFtsDrop[] =
@ DROP TABLE IF EXISTS repository.ftsidx;
@ DROP VIEW IF EXISTS repository.ftscontent;
@ DROP TABLE IF EXISTS repository.ftsdocs;
;

/*
** Create or drop the tables associated with a full-text index.
*/
static int searchIdxExists = -1;
void search_create_index(void){
  int useStemmer = db_get_boolean("search-stemmer",0);
  const char *zExtra = useStemmer ? ",tokenize=porter" : "";
  search_sql_setup(g.db);
  db_multi_exec(zFtsSchema/*works-like:"%s"*/, zExtra/*safe-for-%s*/);
  searchIdxExists = 1;
}
void search_drop_index(void){
  db_multi_exec(zFtsDrop/*works-like:""*/);
  searchIdxExists = 0;
}

/*
** Return true if the full-text search index exists
*/
int search_index_exists(void){
  if( searchIdxExists<0 ){
    searchIdxExists = db_table_exists("repository","ftsdocs");
  }
  return searchIdxExists;
}

/*
** Fill the FTSDOCS table with unindexed entries for everything
** in the repository.  This uses INSERT OR IGNORE so entries already
** in FTSDOCS are unchanged.
*/
void search_fill_index(void){
  if( !search_index_exists() ) return;
  search_sql_setup(g.db);
  db_multi_exec(
    "INSERT OR IGNORE INTO ftsdocs(type,rid,idxed)"
    "  SELECT 'c', objid, 0 FROM event WHERE type='ci';"
  );
  db_multi_exec(
    "WITH latest_wiki(rid,name,mtime) AS ("
    "  SELECT tagxref.rid, substr(tag.tagname,6), max(tagxref.mtime)"
    "    FROM tag, tagxref"
    "   WHERE tag.tagname GLOB 'wiki-*'"
    "     AND tagxref.tagid=tag.tagid"
    "     AND tagxref.value>0"
    "   GROUP BY 2"
    ") INSERT OR IGNORE INTO ftsdocs(type,rid,name,idxed)"
    "     SELECT 'w', rid, name, 0 FROM latest_wiki;"
  );
  db_multi_exec(
    "INSERT OR IGNORE INTO ftsdocs(type,rid,idxed)"
    "  SELECT 't', tkt_id, 0 FROM ticket;"
  );
  db_multi_exec(
    "INSERT OR IGNORE INTO ftsdocs(type,rid,name,idxed)"
    "  SELECT 'e', objid, comment, 0 FROM event WHERE type='e';"
  );
}

/*
** The document described by cType,rid,zName is about to be added or
** updated.  If the document has already been indexed, then unindex it
** now while we still have access to the old content.  Add the document
** to the queue of documents that need to be indexed or reindexed.
*/
void search_doc_touch(char cType, int rid, const char *zName){
  if( search_index_exists() ){
    char zType[2];
    zType[0] = cType;
    zType[1] = 0;
    search_sql_setup(g.db);
    db_multi_exec(
       "DELETE FROM ftsidx WHERE docid IN"
       "    (SELECT rowid FROM ftsdocs WHERE type=%Q AND rid=%d AND idxed)",
       zType, rid
    );
    db_multi_exec(
       "REPLACE INTO ftsdocs(type,rid,name,idxed)"
       " VALUES(%Q,%d,%Q,0)",
       zType, rid, zName
    );
    if( cType=='w' || cType=='e' ){
      db_multi_exec(
        "DELETE FROM ftsidx WHERE docid IN"
        "    (SELECT rowid FROM ftsdocs WHERE type='%c' AND name=%Q AND idxed)",
        cType, zName
      );
      db_multi_exec(
        "DELETE FROM ftsdocs WHERE type='%c' AND name=%Q AND rid!=%d",
        cType, zName, rid
      );
    }
  }
}

/*
** If the doc-glob and doc-br settings are valid for document search
** and if the latest check-in on doc-br is in the unindexed set of
** check-ins, then update all 'd' entries in FTSDOCS that have
** changed.
*/
static void search_update_doc_index(void){
  const char *zDocBr = db_get("doc-branch","trunk");
  int ckid = zDocBr ? symbolic_name_to_rid(zDocBr,"ci") : 0;
  double rTime;
  if( ckid==0 ) return;
  if( !db_exists("SELECT 1 FROM ftsdocs WHERE type='c' AND rid=%d"
                 "   AND NOT idxed", ckid) ) return;

  /* If we get this far, it means that changes to 'd' entries are
  ** required. */
  rTime = db_double(0.0, "SELECT mtime FROM event WHERE objid=%d", ckid);
  db_multi_exec(
    "CREATE TEMP TABLE current_docs(rid INTEGER PRIMARY KEY, name);"
    "CREATE VIRTUAL TABLE IF NOT EXISTS temp.foci USING files_of_checkin;"
    "INSERT OR IGNORE INTO current_docs(rid, name)"
    "  SELECT blob.rid, foci.filename FROM foci, blob"
    "   WHERE foci.checkinID=%d AND blob.uuid=foci.uuid"
    "     AND %z",
    ckid, glob_expr("foci.filename", db_get("doc-glob",""))
  );
  db_multi_exec(
    "DELETE FROM ftsidx WHERE docid IN"
    "  (SELECT rowid FROM ftsdocs WHERE type='d'"
    "      AND rid NOT IN (SELECT rid FROM current_docs))"
  );
  db_multi_exec(
    "DELETE FROM ftsdocs WHERE type='d'"
    "      AND rid NOT IN (SELECT rid FROM current_docs)"
  );
  db_multi_exec(
    "INSERT OR IGNORE INTO ftsdocs(type,rid,name,idxed,label,bx,url,mtime)"
    "  SELECT 'd', rid, name, 0,"
    "         title('d',rid,name),"
    "         body('d',rid,name),"
    "         printf('/doc/%T/%%s',urlencode(name)),"
    "         %.17g"
    " FROM current_docs",
    zDocBr, rTime
  );
  db_multi_exec(
    "INSERT INTO ftsidx(docid,title,body)"
    "  SELECT rowid, label, bx FROM ftsdocs WHERE type='d' AND NOT idxed"
  );
  db_multi_exec(
    "UPDATE ftsdocs SET"
    "  idxed=1,"
    "  bx=NULL,"
    "  label='Document: '||label"
    " WHERE type='d' AND NOT idxed"
  );
}

/*
** Deal with all of the unindexed 'c' terms in FTSDOCS
*/
static void search_update_checkin_index(void){
  db_multi_exec(
    "INSERT INTO ftsidx(docid,title,body)"
    " SELECT rowid, '', body('c',rid,NULL) FROM ftsdocs"
    "  WHERE type='c' AND NOT idxed;"
  );
  db_multi_exec(
    "UPDATE ftsdocs SET idxed=1, name=NULL,"
    " (label,url,mtime) = "
    "  (SELECT printf('Check-in [%%.16s] on %%s',blob.uuid,"
    "                 datetime(event.mtime)),"
    "          printf('/timeline?y=ci&c=%%.20s',blob.uuid),"
    "          event.mtime"
    "     FROM event, blob"
    "    WHERE event.objid=ftsdocs.rid"
    "      AND blob.rid=ftsdocs.rid)"
    "WHERE ftsdocs.type='c' AND NOT ftsdocs.idxed"
  );
}

/*
** Deal with all of the unindexed 't' terms in FTSDOCS
*/
static void search_update_ticket_index(void){
  db_multi_exec(
    "INSERT INTO ftsidx(docid,title,body)"
    " SELECT rowid, title('t',rid,NULL), body('t',rid,NULL) FROM ftsdocs"
    "  WHERE type='t' AND NOT idxed;"
  );
  if( db_changes()==0 ) return;
  db_multi_exec(
    "UPDATE ftsdocs SET idxed=1, name=NULL,"
    "  (label,url,mtime) ="
    "  (SELECT printf('Ticket: %%s (%%s)',title('t',tkt_id,null),"
    "                 datetime(tkt_mtime)),"
    "          printf('/tktview/%%.20s',tkt_uuid),"
    "          tkt_mtime"
    "     FROM ticket"
    "    WHERE tkt_id=ftsdocs.rid)"
    "WHERE ftsdocs.type='t' AND NOT ftsdocs.idxed"
  );
}

/*
** Deal with all of the unindexed 'w' terms in FTSDOCS
*/
static void search_update_wiki_index(void){
  db_multi_exec(
    "INSERT INTO ftsidx(docid,title,body)"
    " SELECT rowid, title('w',rid,NULL),body('w',rid,NULL) FROM ftsdocs"
    "  WHERE type='w' AND NOT idxed;"
  );
  if( db_changes()==0 ) return;
  db_multi_exec(
    "UPDATE ftsdocs SET idxed=1,"
    "  (name,label,url,mtime) = "
    "    (SELECT ftsdocs.name,"
    "            'Wiki: '||ftsdocs.name,"
    "            '/wiki?name='||urlencode(ftsdocs.name),"
    "            tagxref.mtime"
    "       FROM tagxref WHERE tagxref.rid=ftsdocs.rid)"
    " WHERE ftsdocs.type='w' AND NOT ftsdocs.idxed"
  );
}

/*
** Deal with all of the unindexed 'e' terms in FTSDOCS
*/
static void search_update_technote_index(void){
  db_multi_exec(
    "INSERT INTO ftsidx(docid,title,body)"
    " SELECT rowid, title('e',rid,NULL),body('e',rid,NULL) FROM ftsdocs"
    "  WHERE type='e' AND NOT idxed;"
  );
  if( db_changes()==0 ) return;
  db_multi_exec(
    "UPDATE ftsdocs SET idxed=1,"
    "  (name,label,url,mtime) = "
    "    (SELECT ftsdocs.name,"
    "            'Tech Note: '||ftsdocs.name,"
    "            '/technote/'||substr(tag.tagname,7),"
    "            tagxref.mtime"
    "       FROM tagxref, tag USING (tagid)"
    "      WHERE tagxref.rid=ftsdocs.rid"
    "        AND tagname GLOB 'event-*')"
    " WHERE ftsdocs.type='e' AND NOT ftsdocs.idxed"
  );
}

/*
** Deal with all of the unindexed entries in the FTSDOCS table - that
** is to say, all the entries with FTSDOCS.IDXED=0.  Add them to the
** index.
*/
void search_update_index(unsigned int srchFlags){
  if( !search_index_exists() ) return;
  if( !db_exists("SELECT 1 FROM ftsdocs WHERE NOT idxed") ) return;
  search_sql_setup(g.db);
  if( srchFlags & (SRCH_CKIN|SRCH_DOC) ){
    search_update_doc_index();
    search_update_checkin_index();
  }
  if( srchFlags & SRCH_TKT ){
    search_update_ticket_index();
  }
  if( srchFlags & SRCH_WIKI ){
    search_update_wiki_index();
  }
  if( srchFlags & SRCH_TECHNOTE ){
    search_update_technote_index();
  }
}

/*
** Construct, prepopulate, and then update the full-text index.
*/
void search_rebuild_index(void){
  fossil_print("rebuilding the search index...");
  fflush(stdout);
  search_create_index();
  search_fill_index();
  search_update_index(search_restrict(SRCH_ALL));
  fossil_print(" done\n");
}

/*
** COMMAND: fts-config*
**
** Usage: fossil fts-config ?SUBCOMMAND? ?ARGUMENT?
**
** The "fossil fts-config" command configures the full-text search capabilities
** of the repository.  Subcommands:
**
**     reindex            Rebuild the search index.  This is a no-op if
**                        index search is disabled
**
**     index (on|off)     Turn the search index on or off
**
**     enable cdtwe       Enable various kinds of search. c=Check-ins,
**                        d=Documents, t=Tickets, w=Wiki, e=Tech Notes.
**
**     disable cdtwe      Disable various kinds of search
**
**     stemmer (on|off)   Turn the Porter stemmer on or off for indexed
**                        search.  (Unindexed search is never stemmed.)
**
** The current search settings are displayed after any changes are applied.
** Run this command with no arguments to simply see the settings.
*/
void fts_config_cmd(void){
  static const struct { int iCmd; const char *z; } aCmd[] = {
     { 1,  "reindex"  },
     { 2,  "index"    },
     { 3,  "disable"  },
     { 4,  "enable"   },
     { 5,  "stemmer"  },
  };
  static const struct { char *zSetting; char *zName; char *zSw; } aSetng[] = {
     { "search-ckin",     "check-in search:",  "c" },
     { "search-doc",      "document search:",  "d" },
     { "search-tkt",      "ticket search:",    "t" },
     { "search-wiki",     "wiki search:",      "w" },
     { "search-technote", "tech note search:", "e" },
  };
  char *zSubCmd = 0;
  int i, j, n;
  int iCmd = 0;
  int iAction = 0;
  db_find_and_open_repository(0, 0);
  if( g.argc>2 ){
    zSubCmd = g.argv[2];
    n = (int)strlen(zSubCmd);
    for(i=0; i<count(aCmd); i++){
      if( fossil_strncmp(aCmd[i].z, zSubCmd, n)==0 ) break;
    }
    if( i>=count(aCmd) ){
      Blob all;
      blob_init(&all,0,0);
      for(i=0; i<count(aCmd); i++) blob_appendf(&all, " %s", aCmd[i].z);
      fossil_fatal("unknown \"%s\" - should be on of:%s",
                   zSubCmd, blob_str(&all));
      return;
    }
    iCmd = aCmd[i].iCmd;
  }
  g.perm.Read = 1;
  g.perm.RdTkt = 1;
  g.perm.RdWiki = 1;
  if( iCmd==1 ){
    if( search_index_exists() ) iAction = 2;
  }
  if( iCmd==2 ){
    if( g.argc<3 ) usage("index (on|off)");
    iAction = 1 + is_truth(g.argv[3]);
  }
  db_begin_transaction();

  /* Adjust search settings */
  if( iCmd==3 || iCmd==4 ){
    const char *zCtrl;
    if( g.argc<4 ) usage(mprintf("%s STRING",zSubCmd));
    zCtrl = g.argv[3];
    for(j=0; j<count(aSetng); j++){
      if( strchr(zCtrl, aSetng[j].zSw[0])!=0 ){
        db_set_int(aSetng[j].zSetting, iCmd-3, 0);
      }
    }
  }
  if( iCmd==5 ){
    if( g.argc<4 ) usage("porter ON/OFF");
    db_set_int("search-stemmer", is_truth(g.argv[3]), 0);
  }


  /* destroy or rebuild the index, if requested */
  if( iAction>=1 ){
    search_drop_index();
  }
  if( iAction>=2 ){
    search_rebuild_index();
  }

  /* Always show the status before ending */
  for(i=0; i<count(aSetng); i++){
    fossil_print("%-17s %s\n", aSetng[i].zName,
       db_get_boolean(aSetng[i].zSetting,0) ? "on" : "off");
  }
  fossil_print("%-17s %s\n", "Porter stemmer:",
       db_get_boolean("search-stemmer",0) ? "on" : "off");
  if( search_index_exists() ){
    fossil_print("%-17s enabled\n", "full-text index:");
    fossil_print("%-17s %d\n", "documents:",
       db_int(0, "SELECT count(*) FROM ftsdocs"));
  }else{
    fossil_print("%-17s disabled\n", "full-text index:");
  }
  db_end_transaction(0);
}

/*
** WEBPAGE: test-ftsdocs
**
** Show a table of all documents currently in the search index.
*/
void search_data_page(void){
  Stmt q;
  const char *zId = P("id");
  const char *zType = P("y");
  const char *zIdxed = P("ixed");
  int id;
  int cnt = 0;
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(0); return; }
  if( !search_index_exists() ){
    @ <p>Indexed search is disabled
    style_footer();
    return;
  }
  if( zId!=0 && (id = atoi(zId))>0 ){
    /* Show information about a single ftsdocs entry */
    style_header("Information about ftsdoc entry %d", id);
    db_prepare(&q,
      "SELECT type||rid, name, idxed, label, url, datetime(mtime)"
      "  FROM ftsdocs WHERE rowid=%d", id
    );
    if( db_step(&q)==SQLITE_ROW ){
      const char *zUrl = db_column_text(&q,4);
      @ <table border=0>
      @ <tr><td align='right'>rowid:<td>&nbsp;&nbsp;<td>%d(id)
      @ <tr><td align='right'>id:<td><td>%s(db_column_text(&q,0))
      @ <tr><td align='right'>name:<td><td>%h(db_column_text(&q,1))
      @ <tr><td align='right'>idxed:<td><td>%d(db_column_int(&q,2))
      @ <tr><td align='right'>label:<td><td>%h(db_column_text(&q,3))
      @ <tr><td align='right'>url:<td><td>
      @ <a href='%R%s(zUrl)'>%h(zUrl)</a>
      @ <tr><td align='right'>mtime:<td><td>%s(db_column_text(&q,5))
      @ </table>
    }
    db_finalize(&q);
    style_footer();
    return;
  }
  if( zType!=0 && zType[0]!=0 && zType[1]==0 &&
      zIdxed!=0 && (zIdxed[0]=='1' || zIdxed[0]=='0') && zIdxed[1]==0
  ){
    int ixed = zIdxed[0]=='1';
    style_header("List of '%c' documents that are%s indexed",
                 zType[0], ixed ? "" : " not");
    db_prepare(&q,
      "SELECT rowid, type||rid ||' '|| coalesce(label,'')"
      "  FROM ftsdocs WHERE type='%c' AND %s idxed",
      zType[0], ixed ? "" : "NOT"
    );
    @ <ul>
    while( db_step(&q)==SQLITE_ROW ){
      @ <li> <a href='test-ftsdocs?id=%d(db_column_int(&q,0))'>
      @ %h(db_column_text(&q,1))</a>
    }
    @ </ul>
    db_finalize(&q);
    style_footer();
    return;
  }
  style_header("Summary of ftsdocs");
  db_prepare(&q,
     "SELECT type, idxed, count(*) FROM ftsdocs"
     " GROUP BY 1, 2 ORDER BY 3 DESC"
  );
  @ <table border=1 cellpadding=3 cellspacing=0>
  @ <thead>
  @ <tr><th>Type<th>Indexed?<th>Count<th>Link
  @ </thead>
  @ <tbody>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zType = db_column_text(&q,0);
    int idxed = db_column_int(&q,1);
    int n = db_column_int(&q,2);
    @ <tr><td>%h(zType)<td>%d(idxed)
    @ <td>%d(n)
    @ <td><a href='test-ftsdocs?y=%s(zType)&ixed=%d(idxed)'>listing</a>
    @ </tr>
    cnt += n;
  }
  @ </tbody><tfooter>
  @ <tr><th>Total<th><th>%d(cnt)<th>
  @ </tfooter>
  @ </table>
  style_footer();
}
