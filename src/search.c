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
** against timeline comments, check-in content, wiki pages, tickets,
** and/or forum posts.
**
** The search can be either a per-query "grep"-like search that scans
** the entire corpus.  Or it can use the FTS5 search engine of SQLite.
** The choice is an administrator configuration option.
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
  p->zPattern = z = fossil_strdup(zPattern);
  p->zMarkBegin = fossil_strdup(zMarkBegin);
  p->zMarkEnd = fossil_strdup(zMarkEnd);
  p->zMarkGap = fossil_strdup(zMarkGap);
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
**
** Options:
**    --begin TEXT        Text to insert before each match
**    --end TEXT          Text to insert after each match
**    --gap TEXT          Text to indicate elided content
**    --html              Input is HTML
**    --static            Use the static Search object
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
    blob_read_from_file(&x, g.argv[i], ExtFILE);
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
** SRCH_TKT, SRCH_FORUM, or SRCH_ALL bits to determine what is to be
** searched.
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
** Remember the results in the global full-scan search object.
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
  static const int enc = SQLITE_UTF8|SQLITE_INNOCUOUS;
  if( once++ ) return;
  sqlite3_create_function(db, "search_match", -1, enc, 0,
     search_match_sqlfunc, 0, 0);
  sqlite3_create_function(db, "search_score", 0, enc, 0,
     search_score_sqlfunc, 0, 0);
  sqlite3_create_function(db, "search_snippet", 0, enc, 0,
     search_snippet_sqlfunc, 0, 0);
  sqlite3_create_function(db, "search_init", -1, enc, 0,
     search_init_sqlfunc, 0, 0);
  sqlite3_create_function(db, "stext", 3, enc, 0,
     search_stext_sqlfunc, 0, 0);
  sqlite3_create_function(db, "title", 3, enc, 0,
     search_title_sqlfunc, 0, 0);
  sqlite3_create_function(db, "body", 3, enc, 0,
     search_body_sqlfunc, 0, 0);
  sqlite3_create_function(db, "urlencode", 1, enc, 0,
     search_urlencode_sqlfunc, 0, 0);
}

/*
** Testing the search function.
**
** COMMAND: search*
**
** Usage: %fossil search [OPTIONS] PATTERN...
**
** Search the repository for PATTERN and show matches.  Depending on
** options and how the administrator has search configured for the
** repository, the search can cover:
**
**    *   check-in comments (-c)
**    *   embedded documentation (--docs)
**    *   forum posts (--forum)
**    *   tickets (--tickets)
**    *   tech notes (--technotes)
**    *   wiki pages (--wiki)
**    *   built-in fossil help text (-h)
**    *   all of the above (-a)
**
** Use options below to select the scope of the search.  The
** default is check-in comments only (-c).
**
** Output is colorized if writing to a TTY and if the NO_COLOR environment
** variable is not set.  Use the "--highlight 0" option to disable colorization
** or use "--highlight 91" to force it on.  Change the argument to --highlight
** to change the color.
**
** Options:
**     -a|--all          Search everything
**     -c|--checkins     Search checkin comments
**     --docs            Search embedded documentation
**     --forum           Search forum posts
**     -h|--bi-help      Search built-in help
**     --highlight N     Used VT100 color N for matching text.  0 means "off".
**     -n|--limit N      Limit output to N matches
**     --technotes       Search tech notes
**     --tickets         Search tickets
**     -W|--width WIDTH  Set display width to WIDTH columns, 0 for
**                       unlimited. Defaults to the terminal's width.
**     --wiki            Search wiki
*/
void search_cmd(void){
  Blob pattern;
  int i;
  Blob sql = empty_blob;
  Stmt q;
  int iBest;
  int srchFlags = 0;
  int bFts = 1;          /* Use FTS search by default now */
  char fAll = NULL != find_option("all", "a", 0);
  const char *zLimit = find_option("limit","n",1);
  const char *zScope = 0;
  const char *zWidth = find_option("width","W",1);
  int bDebug = find_option("debug",0,0)!=0;     /* Undocumented */
  int nLimit = zLimit ? atoi(zLimit) : -1000;
  int width;
  int nTty = 0;          /* VT100 highlight color for matching text */
  const char *zHighlight = 0;
  int bFlags = 0;        /* DB open flags */

  nTty =  terminal_is_vt100();

  /* Undocumented option to change highlight color */
  zHighlight = find_option("highlight",0,1);
  if( zHighlight ) nTty = atoi(zHighlight);

  /* Undocumented option (legacy) */
  zScope = find_option("scope",0,1);

  if( find_option("fts",0,0)!=0 ) bFts = 1;      /* Undocumented legacy */
  if( find_option("legacy",0,0)!=0 ) bFts = 0;   /* Undocumented */

  if( zWidth ){
    width = atoi(zWidth);
    if( (width!=0) && (width<=20) ){
      fossil_fatal("-W|--width value must be >20 or 0");
    }
  }else{
    width = -1;
  }
  if( zScope ){
    for(i=0; zScope[i]; i++){
      switch( zScope[i] ){
        case 'a':  srchFlags = SRCH_ALL;       break;
        case 'c':  srchFlags |= SRCH_CKIN;     break;
        case 'd':  srchFlags |= SRCH_DOC;      break;
        case 'e':  srchFlags |= SRCH_TECHNOTE; break;
        case 'f':  srchFlags |= SRCH_FORUM;    break;
        case 'h':  srchFlags |= SRCH_HELP;     break;
        case 't':  srchFlags |= SRCH_TKT;      break;
        case 'w':  srchFlags |= SRCH_WIKI;     break;
      }
    }
    bFts = 1;
  }
  if( find_option("all","a",0) ){      srchFlags |= SRCH_ALL;      bFts = 1; }
  if( find_option("bi-help","h",0) ){  srchFlags |= SRCH_HELP;     bFts = 1; }
  if( find_option("checkins","c",0) ){ srchFlags |= SRCH_CKIN;     bFts = 1; }
  if( find_option("docs",0,0) ){       srchFlags |= SRCH_DOC;      bFts = 1; }
  if( find_option("forum",0,0) ){      srchFlags |= SRCH_FORUM;    bFts = 1; }
  if( find_option("technotes",0,0) ){  srchFlags |= SRCH_TECHNOTE; bFts = 1; }
  if( find_option("tickets",0,0) ){    srchFlags |= SRCH_TKT;      bFts = 1; }
  if( find_option("wiki",0,0) ){       srchFlags |= SRCH_WIKI;     bFts = 1; }

  /* If no search objects are specified, default to "check-in comments" */
  if( srchFlags==0 ) srchFlags = SRCH_CKIN;

  if( srchFlags==SRCH_HELP ) bFlags = OPEN_OK_NOT_FOUND|OPEN_SUBSTITUTE;
  db_find_and_open_repository(bFlags, 0);
  verify_all_options();
  if( g.argc<3 ) return;
  login_set_capabilities("s", 0);
  if( search_restrict(srchFlags)==0 && (srchFlags & SRCH_HELP)==0 ){
    const char *zC1 = 0, *zPlural = "s";
    if( srchFlags & SRCH_TECHNOTE ){  zC1 = "technote"; }
    if( srchFlags & SRCH_TKT ){       zC1 = "ticket";   }
    if( srchFlags & SRCH_FORUM ){     zC1 = "forum";    zPlural = ""; }
    if( srchFlags & SRCH_DOC ){       zC1 = "document"; }
    if( srchFlags & SRCH_WIKI ){      zC1 = "wiki";     zPlural = ""; }
    if( srchFlags & SRCH_CKIN ){      zC1 = "check-in"; }
    fossil_print(
      "Search of %s%s is disabled on this repository.\n"
      "Enable using \"fossil fts-config enable %s\".\n",
      zC1, zPlural, zC1
    );
    return;
  }

  blob_init(&pattern, g.argv[2], -1);
  for(i=3; i<g.argc; i++){
    blob_appendf(&pattern, " %s", g.argv[i]);
  }
  if( bFts ){
    /* Search using FTS */
    Blob com;
    Blob snip;
    const char *zPattern = blob_str(&pattern);
    search_sql_setup(g.db);
    add_content_sql_commands(g.db);
    db_multi_exec(
      "CREATE TEMP TABLE x(label,url,score,id,date,snip);"
    );
    if( !search_index_exists() ){
      search_fullscan(zPattern, srchFlags);  /* Full-scan search */
    }else{
      search_update_index(srchFlags);        /* Update the index */
      search_indexed(zPattern, srchFlags);   /* Indexed search */
      if( srchFlags & SRCH_HELP ){
        search_fullscan(zPattern, SRCH_HELP);
      }
    }
    db_prepare(&q, "SELECT snip, label, score, id, date"
                   "  FROM x"
                   " ORDER BY score DESC, date DESC;");
    blob_init(&com, 0, 0);
    blob_init(&snip, 0, 0);
    if( width<0 ) width = terminal_get_width(80);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zSnippet = db_column_text(&q, 0);
      const char *zLabel = db_column_text(&q, 1);
      const char *zDate = db_column_text(&q, 4);
      const char *zScore = db_column_text(&q, 2);
      const char *zId = db_column_text(&q, 3);
      char *zOrig;
      blob_appendf(&snip, "%s", zSnippet);
      zOrig = blob_materialize(&snip);
      blob_init(&snip, 0, 0);
      html_to_plaintext(zOrig, &snip, (nTty?HTOT_VT100:0)|HTOT_FLOW|HTOT_TRIM);
      fossil_free(zOrig);
      blob_appendf(&com, "%s\n%s\n%s", zLabel, blob_str(&snip), zDate);
      if( bDebug ){
        blob_appendf(&com," score: %s id: %s", zScore, zId);
      }
      comment_print(blob_str(&com), 0, 5, width,
            COMMENT_PRINT_TRIM_CRLF |
            COMMENT_PRINT_WORD_BREAK |
            COMMENT_PRINT_TRIM_SPACE);
      blob_reset(&com);
      blob_reset(&snip);
      if( nLimit>=1 ){
        nLimit--;
        if( nLimit==0 ) break;
      }
    }
    db_finalize(&q);
    blob_reset(&pattern);
  }else{
    /* Legacy timeline search (the default) */
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
    print_timeline(&q, nLimit, width, 0, 0);
    db_finalize(&q);
  }
}

#if INTERFACE
/* What to search for */
#define SRCH_CKIN     0x0001    /* Search over check-in comments */
#define SRCH_DOC      0x0002    /* Search over embedded documents */
#define SRCH_TKT      0x0004    /* Search over tickets */
#define SRCH_WIKI     0x0008    /* Search over wiki */
#define SRCH_TECHNOTE 0x0010    /* Search over tech notes */
#define SRCH_FORUM    0x0020    /* Search over forum messages */
#define SRCH_HELP     0x0040    /* Search built-in help (full-scan only) */
#define SRCH_ALL      0x007f    /* Search over everything */
#endif

/*
** Remove bits from srchFlags which are disallowed by either the
** current server configuration or by user permissions.  Return
** the revised search flags mask.
**
** If bFlex is true, that means allow through the SRCH_HELP option
** even if it is not explicitly enabled.
*/
unsigned int search_restrict(unsigned int srchFlags){
  static unsigned int knownGood = 0;
  static unsigned int knownBad = 0;
  static const struct { unsigned m; const char *zKey; } aSetng[] = {
     { SRCH_CKIN,     "search-ci"       },
     { SRCH_DOC,      "search-doc"      },
     { SRCH_TKT,      "search-tkt"      },
     { SRCH_WIKI,     "search-wiki"     },
     { SRCH_TECHNOTE, "search-technote" },
     { SRCH_FORUM,    "search-forum"    },
     { SRCH_HELP,     "search-help"     },
  };
  int i;
  if( g.perm.Read==0 )   srchFlags &= ~(SRCH_CKIN|SRCH_DOC|SRCH_TECHNOTE);
  if( g.perm.RdTkt==0 )  srchFlags &= ~(SRCH_TKT);
  if( g.perm.RdWiki==0 ) srchFlags &= ~(SRCH_WIKI);
  if( g.perm.RdForum==0) srchFlags &= ~(SRCH_FORUM);
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
LOCAL void search_fullscan(
  const char *zPattern,       /* The query pattern */
  unsigned int srchFlags      /* What to search over */
){
  search_init(zPattern, "<mark>", "</mark>", " ... ",
          SRCHFLG_STATIC|SRCHFLG_HTML);
  if( (srchFlags & SRCH_DOC)!=0 ){
    char *zDocGlob = db_get("doc-glob","");
    char *zDocBr = db_get("doc-branch","trunk");
    if( zDocGlob && zDocGlob[0] && zDocBr && zDocBr[0] ){
      Glob * pGlob = glob_create(zDocBr)
        /* We're misusing a Glob as a list of comma-/space-delimited
        ** tokens. We're not actually doing glob matches here. */;
      int i;
      db_multi_exec(
        "CREATE VIRTUAL TABLE IF NOT EXISTS temp.foci USING files_of_checkin;"
      );
      for( i = 0; i < pGlob->nPattern; ++i ){
        const char * zBranch = pGlob->azPattern[i];
        db_multi_exec(
          "INSERT INTO x(label,url,score,id,date,snip)"
          "  SELECT printf('Document: %%s',title('d',blob.rid,foci.filename)),"
          "         printf('/doc/%T/%%s',foci.filename),"
          "         search_score(),"
          "         'd'||blob.rid,"
          "         (SELECT datetime(event.mtime) FROM event"
          "            WHERE objid=symbolic_name_to_rid(%Q)),"
          "         search_snippet()"
          "    FROM foci CROSS JOIN blob"
          "   WHERE checkinID=symbolic_name_to_rid(%Q)"
          "     AND blob.uuid=foci.uuid"
          "     AND search_match(title('d',blob.rid,foci.filename),"
          "                      body('d',blob.rid,foci.filename))"
          "     AND %z",
          zBranch, zBranch, zBranch, glob_expr("foci.filename", zDocGlob)
        );
      }
      glob_free(pGlob);
    }
    fossil_free(zDocGlob);
    fossil_free(zDocBr);
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
      "         printf('/timeline?c=%%s',uuid),"
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
  if( (srchFlags & SRCH_FORUM)!=0 ){
    db_multi_exec(
      "INSERT INTO x(label,url,score,id,date,snip)"
      "  SELECT 'Forum '||comment,"
      "         '/forumpost/'||uuid,"
      "         search_score(),"
      "         'f'||rid,"
      "         datetime(event.mtime),"
      "         search_snippet()"
      "    FROM event JOIN blob on event.objid=blob.rid"
      "   WHERE search_match('',body('f',rid,NULL));"
    );
  }
  if( (srchFlags & SRCH_HELP)!=0 ){
    const char *zPrefix;
    helptext_vtab_register(g.db);
    if( srchFlags==SRCH_HELP ){
      zPrefix = "The";
    }else{
      zPrefix = "Built-in help for the";
    }
    db_multi_exec(
      "INSERT INTO x(label,url,score,id,snip)"
      "  SELECT format('%q \"%%s\" %%s',name,type),"
      "         '/help?cmd='||name,"
      "         search_score(),"
      "         'h'||rowid,"
      "         search_snippet()"
      "    FROM helptext"
      "   WHERE search_match(format('the \"%%s\" %%s',name,type),"
      "                      helptext.helptext);",
      zPrefix
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
** Expects a search pattern string. Makes a copy of the string,
** replaces all non-alphanum ASCII characters with a space, and
** lower-cases all upper-case ASCII characters. The intent is to avoid
** causing errors in FTS5 searches with inputs which contain AND, OR,
** and symbols like #. The caller is responsible for passing the
** result to fossil_free().
*/
char *search_simplify_pattern(const char * zPattern){
  char *zPat = fossil_strdup(zPattern);
  int i;
  for(i=0; zPat[i]; i++){
    if( (zPat[i]&0x80)==0 && !fossil_isalnum(zPat[i]) ) zPat[i] = ' ';
    if( fossil_isupper(zPat[i]) ) zPat[i] = fossil_tolower(zPat[i]);
  }
  for(i--; i>=0 && zPat[i]==' '; i--){}
  if( i<0 ){
    fossil_free(zPat);
    zPat = mprintf("\"\"");
  }
  return zPat;
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
LOCAL void search_indexed(
  const char *zPattern,       /* The query pattern */
  unsigned int srchFlags      /* What to search over */
){
  Blob sql;
  char *zPat;
  static const char *zSnippetCall;
  if( srchFlags==0 ) return;
  sqlite3_create_function(g.db, "rank", 1, SQLITE_UTF8|SQLITE_INNOCUOUS, 0,
     search_rank_sqlfunc, 0, 0);
  zPat = search_simplify_pattern(zPattern);
  blob_init(&sql, 0, 0);
  if( search_index_type(0)==4 ){
    /* If this repo is still using the legacy FTS5 search index, then
    ** the snippet() function is slightly different */
    zSnippetCall = "snippet(ftsidx,'<mark>','</mark>',' ... ',-1,35)";
  }else{
    /* This is the common case - Using newer FTS5 search index */
    zSnippetCall = "snippet(ftsidx,-1,'<mark>','</mark>',' ... ',35)";
  }
  blob_appendf(&sql,
    "INSERT INTO x(label,url,score,id,date,snip) "
    " SELECT ftsdocs.label,"
    "        ftsdocs.url,"
    "        rank(matchinfo(ftsidx,'pcsx')),"
    "        ftsdocs.type || ftsdocs.rid,"
    "        datetime(ftsdocs.mtime),"
    "        %s"
    "   FROM ftsidx CROSS JOIN ftsdocs"
    "  WHERE ftsidx MATCH %Q"
    "    AND ftsdocs.rowid=ftsidx.rowid",
    zSnippetCall /*safe-for-%s*/, zPat
  );
  fossil_free(zPat);
  if( srchFlags!=SRCH_ALL ){
    const char *zSep = " AND (";
    static const struct { unsigned m; char c; } aMask[] = {
       { SRCH_CKIN,     'c' },
       { SRCH_DOC,      'd' },
       { SRCH_TKT,      't' },
       { SRCH_WIKI,     'w' },
       { SRCH_TECHNOTE, 'e' },
       { SRCH_FORUM,    'f' },
       { SRCH_HELP,     'h' },
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
  if( zSnip==0 ) zSnip = "";
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
  int nLimit = db_get_int("search-limit", 100);

  if( P("searchlimit")!=0 ){
    nLimit = atoi(P("searchlimit"));
  }
  srchFlags = search_restrict(srchFlags) | (srchFlags & SRCH_HELP);
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
    if( srchFlags & SRCH_HELP ){
      search_fullscan(zPattern, SRCH_HELP);
    }
  }
  db_prepare(&q, "SELECT url, snip, label, score, id, substr(date,1,10)"
                 "  FROM x"
                 " ORDER BY score DESC, date DESC;");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUrl = db_column_text(&q, 0);
    const char *zSnippet = db_column_text(&q, 1);
    const char *zLabel = db_column_text(&q, 2);
    const char *zDate = db_column_text(&q, 5);
    if( nRow==0 ){
      @ <ol>
    }
    nRow++;
    @ <li><p><a href='%R%s(zUrl)'>%h(zLabel)</a>
    if( fDebug ){
      @ (%e(db_column_double(&q,3)), %s(db_column_text(&q,4))
    }
    @ <br><span class='snippet'>%z(cleanSnippet(zSnippet)) \
    if( zLabel && zDate && zDate[0] && strstr(zLabel,zDate)==0 ){
      @ <small>(%h(zDate))</small>
    }
    @ </span></li>
    if( nLimit && nRow>=nLimit ) break;
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
** The mFlags value controls options:
**
**     0x01    If the y= query parameter is present, use it as an addition
**             restriction what to search.
**
**     0x02    Show nothing if search is disabled.
**
** Return true if there are search results.
*/
int search_screen(unsigned srchAllowed, int mFlags){
  const char *zType = 0;
  const char *zClass = 0;
  const char *zDisable1;
  const char *zDisable2;
  const char *zPattern;
  int fDebug = PB("debug");
  int haveResult = 0;
  int srchThisTime;
  const char *zY = PD("y","all");
  if( zY[0]=='h' && zY[1]==0 ){
    srchAllowed = search_restrict(srchAllowed) | (srchAllowed & SRCH_HELP);
  }else{
    srchAllowed = search_restrict(srchAllowed);
  }
  switch( srchAllowed ){
    case SRCH_CKIN:     zType = " Check-ins";  zClass = "Ckin"; break;
    case SRCH_DOC:      zType = " Docs";       zClass = "Doc";  break;
    case SRCH_TKT:      zType = " Tickets";    zClass = "Tkt";  break;
    case SRCH_WIKI:     zType = " Wiki";       zClass = "Wiki"; break;
    case SRCH_TECHNOTE: zType = " Tech Notes"; zClass = "Note"; break;
    case SRCH_FORUM:    zType = " Forum";      zClass = "Frm";  break;
    case SRCH_HELP:     zType = " Help";       zClass = "Hlp";  break;
  }
  if( srchAllowed==0 ){
    if( mFlags & 0x02 ) return 0;
    zDisable1 = " disabled";
    zDisable2 = " disabled";
    zPattern = "";
  }else{
    zDisable1 = ""; /* Was: " autofocus" */
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
  srchThisTime = srchAllowed;
  if( (mFlags & 0x01)!=0 && (srchAllowed & (srchAllowed-1))!=0 ){
    static const struct {
      const char *z;
      const char *zNm;
      unsigned m;
    } aY[] = {
       { "all",  "All",        SRCH_ALL      },
       { "c",    "Check-ins",  SRCH_CKIN     },
       { "d",    "Docs",       SRCH_DOC      },
       { "t",    "Tickets",    SRCH_TKT      },
       { "w",    "Wiki",       SRCH_WIKI     },
       { "e",    "Tech Notes", SRCH_TECHNOTE },
       { "f",    "Forum",      SRCH_FORUM    },
       { "h",    "Help",       SRCH_HELP     },
    };
    int i;
    @ <select size='1' name='y'>
    for(i=0; i<count(aY); i++){
      if( (aY[i].m & srchAllowed)==0 ) continue;
      if( aY[i].m==SRCH_HELP && fossil_strcmp(zY,"h")!=0
       && search_restrict(SRCH_HELP)==0 ) continue;
      cgi_printf("<option value='%s'", aY[i].z);
      if( fossil_strcmp(zY,aY[i].z)==0 ){
        srchThisTime &= aY[i].m;
        cgi_printf(" selected");
      }
      cgi_printf(">%s</option>\n", aY[i].zNm);
    }
    @ </select>
  }
  if( fDebug ){
    @ <input type="hidden" name="debug" value="1">
  }
  @ <input type="submit" value="Search%s(zType)"%s(zDisable2)>
  if( srchAllowed==0 && srchThisTime==0 ){
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
    if( search_run_and_output(zPattern, srchThisTime, fDebug)==0 ){
      @ <p class='searchEmpty'>No matches for: <span>%h(zPattern)</span></p>
    }
    @ </div>
    haveResult = 1;
  }
  return haveResult;
}

/*
** WEBPAGE: search
**
** Search for check-in comments, documents, tickets, or wiki that
** match a user-supplied pattern.
**
**    s=PATTERN       Specify the full-text pattern to search for
**    y=TYPE          What to search.
**                      c -> check-ins,
**                      d -> documentation,
**                      t -> tickets,
**                      w -> wiki,
**                      e -> tech notes,
**                      f -> forum,
**                      h -> built-in help,
**                    all -> everything.
*/
void search_page(void){
  const int isSearch = P("s")!=0;
  login_check_credentials();
  style_header("Search%s", isSearch ? " Results" : "");
  cgi_check_for_malice();
  search_screen(SRCH_ALL, 1);
  style_finish_page();
}


/*
** This is a helper function for search_stext().  Writing into pOut
** the search text obtained from pIn according to zMimetype.
**
** If a title is not specified in zTitle (e.g. for wiki pages that do not
** include the title in the body), it is determined from the page content.
**
** The title of the document is the first line of text.  All subsequent
** lines are the body.  If the document has no title, the first line
** is blank.
*/
static void get_stext_by_mimetype(
  Blob *pIn,
  const char *zMimetype,
  const char *zTitle,
  Blob *pOut
){
  Blob html, title;
  Blob *pHtml = &html;
  blob_init(&html, 0, 0);
  if( zTitle==0 ){
    blob_init(&title, 0, 0);
  }else{
    blob_init(&title, zTitle, -1);
  }
  if( zMimetype==0 ) zMimetype = "text/plain";
  if( fossil_strcmp(zMimetype,"text/x-fossil-wiki")==0 ){
    if( blob_size(&title) ){
      wiki_convert(pIn, &html, 0);
    }else{
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
    }
    html_to_plaintext(blob_str(&html), pOut, 0);
  }else if( fossil_strcmp(zMimetype,"text/x-markdown")==0 ){
    markdown_to_html(pIn, blob_size(&title) ? NULL : &title, &html);
  }else if( fossil_strcmp(zMimetype,"text/html")==0 ){
    if( blob_size(&title)==0 ) doc_is_embedded_html(pIn, &title);
    pHtml = pIn;
  }
  blob_appendf(pOut, "%s\n", blob_str(&title));
  if( blob_size(pHtml) ){
    html_to_plaintext(blob_str(pHtml), pOut, 0);
  }else{
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
      get_stext_by_mimetype(&txt, zMime, NULL, pAccum);
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
**                      f      Forum
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
      get_stext_by_mimetype(&doc, mimetype_from_name(zName), NULL, pOut);
      blob_reset(&doc);
      break;
    }
    case 'f':     /* Forum messages */
    case 'e':     /* Tech Notes */
    case 'w': {   /* Wiki */
      Manifest *pWiki = manifest_get(rid,
          cType == 'e' ? CFTYPE_EVENT :
          cType == 'f' ? CFTYPE_FORUM : CFTYPE_WIKI, 0);
      Blob wiki;
      if( pWiki==0 ) break;
      if( cType=='f' ){
        blob_init(&wiki, 0, 0);
        if( pWiki->zThreadTitle ){
          blob_appendf(&wiki, "<h1>%h</h1>\n", pWiki->zThreadTitle);
        }
        blob_appendf(&wiki, "From %s:\n\n%s", pWiki->zUser, pWiki->zWiki);
      }else{
        blob_init(&wiki, pWiki->zWiki, -1);
      }
      get_stext_by_mimetype(&wiki, wiki_filter_mimetypes(pWiki->zMimetype),
                            cType=='w' ? pWiki->zWikiTitle : NULL, pOut);
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
          get_stext_by_mimetype(&x, "text/x-fossil-wiki", NULL, pOut);
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
  blob_read_from_file(&in, g.argv[2], ExtFILE);
  blob_init(&out, 0, 0);
  get_stext_by_mimetype(&in, g.argv[3], NULL, &out);
  fossil_print("%s\n",blob_str(&out));
  blob_reset(&in);
  blob_reset(&out);
}

/*
** The schema for the full-text index. The %s part must be an empty
** string or a comma followed by additional flags for the FTS virtual
** table.
*/
static const char zFtsSchema[] =
@ -- One entry for each possible search result
@ CREATE TABLE IF NOT EXISTS repository.ftsdocs(
@   rowid INTEGER PRIMARY KEY, -- Maps to the ftsidx.rowid
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
@   USING fts5(content="ftscontent", title, body%s);
;
static const char zFtsDrop[] =
@ DROP TABLE IF EXISTS repository.ftsidx;
@ DROP VIEW IF EXISTS repository.ftscontent;
@ DROP TABLE IF EXISTS repository.ftsdocs;
@ DROP TABLE IF EXISTS repository.chatfts1;
;

#if INTERFACE
/*
** Values for the search-tokenizer config option.
*/
#define FTS5TOK_NONE      0 /* disabled */
#define FTS5TOK_PORTER    1 /* porter stemmer */
#define FTS5TOK_UNICODE61 2 /* unicode61 tokenizer */
#define FTS5TOK_TRIGRAM   3 /* trigram tokenizer */
#endif

/*
** Cached FTS5TOK_xyz value for search_tokenizer_type() and
** friends.
*/
static int iFtsTokenizer = -1;

/*
** Returns one of the FTS5TOK_xyz values, depending on the value of
** the search-tokenizer config entry, defaulting to FTS5TOK_NONE. The
** result of the first call is cached for subsequent calls unless
** bRecheck is true.
*/
int search_tokenizer_type(int bRecheck){
  char *z;
  if( iFtsTokenizer>=0 && bRecheck==0 ){
    return iFtsTokenizer;
  }
  z = db_get("search-tokenizer",0);
  if( 0==z ){
    iFtsTokenizer = FTS5TOK_NONE;
  }else if(0==fossil_strcmp(z,"porter")){
    iFtsTokenizer = FTS5TOK_PORTER;
  }else if(0==fossil_strcmp(z,"unicode61")){
    iFtsTokenizer = FTS5TOK_UNICODE61;
  }else if(0==fossil_strcmp(z,"trigram")){
    iFtsTokenizer = FTS5TOK_TRIGRAM;
  }else{
    iFtsTokenizer = is_truth(z) ? FTS5TOK_PORTER : FTS5TOK_NONE;
  }
  fossil_free(z);
  return iFtsTokenizer;
}

/*
** Returns a string in the form ",tokenize=X", where X is the string
** counterpart of the given FTS5TOK_xyz value.  Returns "" if tokType
** does not correspond to a known FTS5 tokenizer.
*/
const char * search_tokenize_arg_for_type(int tokType){
  switch( tokType ){
    case FTS5TOK_PORTER: return ",tokenize=porter";
    case FTS5TOK_UNICODE61: return ",tokenize=unicode61";
    case FTS5TOK_TRIGRAM: return ",tokenize=trigram";
    case FTS5TOK_NONE:
    default: return "";
  }
}

/*
** Returns a string value suitable for use as the search-tokenizer
** setting's value, depending on the value of z. If z is 0 then the
** current search-tokenizer value is used as the basis for formulating
** the result (which may differ from the current value but will have
** the same meaning). Any unknown/unsupported value is interpreted as
** "off".
*/
const char *search_tokenizer_for_string(const char *z){
  char * zTmp = 0;
  const char *zRc = 0;

  if( 0==z ){
    z = zTmp = db_get("search-tokenizer",0);
  }
  if( 0==z ){
    zRc = "off";
  }else if( 0==fossil_strcmp(z,"porter") ){
    zRc = "porter";
  }else if( 0==fossil_strcmp(z,"unicode61") ){
    zRc = "unicode61";
  }else if( 0==fossil_strcmp(z,"trigram") ){
    zRc = "trigram";
  }else{
    zRc = is_truth(z) ? "porter" : "off";
  }
  fossil_free(zTmp);
  return zRc;
}

/*
** Sets the search-tokenizer config setting to the value of
** search_tokenizer_for_string(zName).
*/
void search_set_tokenizer(const char *zName){
  db_set("search-tokenizer", search_tokenizer_for_string( zName ), 0);
  iFtsTokenizer = -1;
}

/*
** Create or drop the tables associated with a full-text index.
*/
static int searchIdxExists = -1;
void search_create_index(void){
  const char *zExtra =
    search_tokenize_arg_for_type(search_tokenizer_type(0));
  assert( zExtra );
  search_sql_setup(g.db);
  db_multi_exec(zFtsSchema/*works-like:"%s"*/, zExtra/*safe-for-%s*/);
  searchIdxExists = 1;
}
void search_drop_index(void){
  db_multi_exec(zFtsDrop/*works-like:""*/);
  searchIdxExists = 0;
}

/*
** Return true if the full-text search index exists.  See also the
** search_index_type() function.
*/
int search_index_exists(void){
  if( searchIdxExists<0 ){
    searchIdxExists = db_table_exists("repository","ftsdocs");
  }
  return searchIdxExists;
}

/*
** Determine which full-text search index is currently being used to
** add searching.  Return values:
**
**     0          No search index is available
**     4          FTS3/4
**     5          FTS5
**
** Results are cached.  Make the argument 1 to reset the cache.  See
** also the search_index_exists() routine.
*/
int search_index_type(int bReset){
  static int idxType = -1;
  if( idxType<0 || bReset ){
    idxType = db_int(0,
        "SELECT CASE WHEN sql GLOB '*fts4*' THEN 4 ELSE 5 END"
        " FROM repository.sqlite_schema WHERE name='ftsidx'"
    );
  }
  return idxType;
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
    "  SELECT type, objid, comment, 0 FROM event WHERE type IN ('e','f');"
  );
}

/*
** The document described by cType,rid,zName is about to be added or
** updated.  If the document has already been indexed, then unindex it
** now while we still have access to the old content.  Add the document
** to the queue of documents that need to be indexed or reindexed.
*/
void search_doc_touch(char cType, int rid, const char *zName){
  if( search_index_exists() && !content_is_private(rid) ){
    char zType[2];
    zType[0] = cType;
    zType[1] = 0;
    search_sql_setup(g.db);
    db_multi_exec(
       "DELETE FROM ftsidx WHERE rowid IN"
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
        "DELETE FROM ftsidx WHERE rowid IN"
        "    (SELECT rowid FROM ftsdocs WHERE type='%c' AND name=%Q AND idxed)",
        cType, zName
      );
      db_multi_exec(
        "DELETE FROM ftsdocs WHERE type='%c' AND name=%Q AND rid!=%d",
        cType, zName, rid
      );
    }
    /* All forum posts are always indexed */
  }
}

/*
** If the doc-glob and doc-br settings are valid for document search
** and if the latest check-in on doc-br is in the unindexed set of
** check-ins, then update all 'd' entries in FTSDOCS that have
** changed.
*/
static void search_update_doc_index(void){
  const char *zDocBranches = db_get("doc-branch","trunk");
  int i;
  Glob * pGlob = glob_create(zDocBranches)
    /* We're misusing a Glob as a list of comma-/space-delimited
    ** tokens. We're not actually doing glob matches here. */;
  if( !pGlob ) return;
  db_multi_exec(
    "CREATE TEMP TABLE current_docs(rid INTEGER PRIMARY KEY, name);"
    "CREATE VIRTUAL TABLE IF NOT EXISTS temp.foci USING files_of_checkin;"
  );
  for( i = 0; i < pGlob->nPattern; ++i ){
    const char *zDocBr = pGlob->azPattern[i];
    int ckid = symbolic_name_to_rid(zDocBr,"ci");
    double rTime;
    if( !db_exists("SELECT 1 FROM ftsdocs WHERE type='c' AND rid=%d"
                   "   AND NOT idxed", ckid) ) continue;
    /* If we get this far, it means that changes to 'd' entries are
    ** required. */
    rTime = db_double(0.0, "SELECT mtime FROM event WHERE objid=%d", ckid);
    db_multi_exec(
      "INSERT OR IGNORE INTO current_docs(rid, name)"
      "  SELECT blob.rid, foci.filename FROM foci, blob"
      "   WHERE foci.checkinID=%d AND blob.uuid=foci.uuid"
      "     AND %z",
      ckid, glob_expr("foci.filename", db_get("doc-glob",""))
    );
    db_multi_exec(
      "DELETE FROM ftsidx WHERE rowid IN"
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
      "INSERT INTO ftsidx(rowid,title,body)"
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
  glob_free(pGlob);
}

/*
** Deal with all of the unindexed 'c' terms in FTSDOCS
*/
static void search_update_checkin_index(void){
  db_multi_exec(
    "INSERT INTO ftsidx(rowid,title,body)"
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
    "INSERT INTO ftsidx(rowid,title,body)"
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
    "INSERT INTO ftsidx(rowid,title,body)"
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
** Deal with all of the unindexed 'f' terms in FTSDOCS
*/
static void search_update_forum_index(void){
  db_multi_exec(
    "INSERT INTO ftsidx(rowid,title,body)"
    " SELECT rowid, title('f',rid,NULL),body('f',rid,NULL) FROM ftsdocs"
    "  WHERE type='f' AND NOT idxed;"
  );
  if( db_changes()==0 ) return;
  db_multi_exec(
    "UPDATE ftsdocs SET idxed=1, name=NULL,"
    " (label,url,mtime) = "
    "  (SELECT 'Forum '||event.comment,"
    "          '/forumpost/'||blob.uuid,"
    "          event.mtime"
    "     FROM event, blob"
    "    WHERE event.objid=ftsdocs.rid"
    "      AND blob.rid=ftsdocs.rid)"
    "WHERE ftsdocs.type='f' AND NOT ftsdocs.idxed"
  );
}

/*
** Deal with all of the unindexed 'e' terms in FTSDOCS
*/
static void search_update_technote_index(void){
  db_multi_exec(
    "INSERT INTO ftsidx(rowid,title,body)"
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
  db_unprotect(PROTECT_READONLY);
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
  if( srchFlags & SRCH_FORUM ){
    search_update_forum_index();
  }
  db_protect_pop();
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
  if( db_table_exists("repository","chat") ){
    chat_rebuild_index(1);
  }
  fossil_print(" done\n");
}

/*
** COMMAND: fts-config*                    abbrv-subcom
**
** Usage: fossil fts-config ?SUBCOMMAND? ?ARGUMENT?
**
** The "fossil fts-config" command configures the full-text search capabilities
** of the repository.  Subcommands:
**
**    reindex            Rebuild the search index.  This is a no-op if
**                       index search is disabled
**
**    index (on|off)     Turn the search index on or off
**
**    enable TYPE ..     Enable search for TYPE.  TYPE is one of:
**                       check-in, document, ticket, wiki, technote, 
**                       forum, help, or all
**
**    disable TYPE ...   Disable search for TYPE
**
**    tokenizer VALUE    Select a tokenizer for indexed search. VALUE
**                       may be one of (porter, on, off, trigram, unicode61),
**                       and "on" is equivalent to "porter". Unindexed
**                       search never uses tokenization or stemming.
**
** The current search settings are displayed after any changes are applied.
** Run this command with no arguments to simply see the settings.
*/
void fts_config_cmd(void){
  static const struct {
    int iCmd;
    const char *z;
  } aCmd[] = {
     { 1,  "reindex"  },
     { 2,  "index"    },
     { 3,  "disable"  },
     { 4,  "enable"   },
     { 5,  "tokenizer"},
  };
  static const struct {
    const char *zSetting;
    const char *zName;
    const char *zSw;
  } aSetng[] = {
     { "search-ci",       "check-in search:",       "c" },
     { "search-doc",      "document search:",       "d" },
     { "search-tkt",      "ticket search:",         "t" },
     { "search-wiki",     "wiki search:",           "w" },
     { "search-technote", "technote search:",       "e" },
     { "search-forum",    "forum search:",          "f" },
     { "search-help",     "built-in help search:",  "h" },
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
      fossil_fatal("unknown \"%s\" - should be one of:%s",
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
    int k;
    const char *zCtrl;
    for(k=2; k<g.argc; k++){
      if( k==2 ){
        if( g.argc<4 ){
          zCtrl = "all";
        }else{
          zCtrl = g.argv[3];
          k++;
        }
      }else{
        zCtrl = g.argv[k];
      }
      if( fossil_strcmp(zCtrl,"all")==0 ){
        zCtrl = "cdtwefh";
      }
      if( strlen(zCtrl)>=4 ){
        /* If the argument to "enable" or "disable" is a string of at least
        ** 4 characters which matches part of any aSetng.zName, then use that
        ** one aSetng value only. */
        char *zGlob = mprintf("*%s*", zCtrl);
        for(j=0; j<count(aSetng); j++){
          if( sqlite3_strglob(zGlob, aSetng[j].zName)==0 ){
            db_set_int(aSetng[j].zSetting/*works-like:"x"*/, iCmd-3, 0);
            zCtrl = 0;
            break;
          }
        }
        fossil_free(zGlob);
      }
      if( zCtrl ){
        for(j=0; j<count(aSetng); j++){
          if( strchr(zCtrl, aSetng[j].zSw[0])!=0 ){
            db_set_int(aSetng[j].zSetting/*works-like:"x"*/, iCmd-3, 0);
          }
        }
      }
    }
  }else if( iCmd==5 ){
    int iOldTokenizer, iNewTokenizer;
    if( g.argc<4 ) usage("tokenizer porter|on|off|trigram|unicode61");
    iOldTokenizer = search_tokenizer_type(0);
    db_set("search-tokenizer",
           search_tokenizer_for_string(g.argv[3]), 0);
    iNewTokenizer = search_tokenizer_type(1);
    if( iOldTokenizer!=iNewTokenizer ){
      /* Drop or rebuild index if tokenizer changes. */
      iAction = 1 + ((iOldTokenizer && iNewTokenizer)
                     ? 1 : (iNewTokenizer ? 1 : 0));
    }
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
    fossil_print("%-21s %s\n", aSetng[i].zName,
       db_get_boolean(aSetng[i].zSetting,0) ? "on" : "off");
  }
  fossil_print("%-21s %s\n", "tokenizer:",
       search_tokenizer_for_string(0));
  if( search_index_exists() ){
    int pgsz = db_int64(0, "PRAGMA repository.page_size;");
    i64 nTotal = db_int64(0, "PRAGMA repository.page_count;")*pgsz;
    i64 nFts = db_int64(0, "SELECT count(*) FROM dbstat"
                               " WHERE schema='repository'"
                               " AND name LIKE 'fts%%'")*pgsz;
    char zSize[50];
    fossil_print("%-21s FTS%d\n", "full-text index:", search_index_type(1));
    fossil_print("%-21s %d\n", "documents:",
       db_int(0, "SELECT count(*) FROM ftsdocs"));
    approxSizeName(sizeof(zSize), zSize, nFts);
    fossil_print("%-21s %s (%.1f%% of repository)\n", "space used",
       zSize, 100.0*((double)nFts/(double)nTotal));
  }else{
    fossil_print("%-21s disabled\n", "full-text index:");
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
  int cnt1 = 0, cnt2 = 0, cnt3 = 0;
  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(0); return; }
  style_set_current_feature("test");
  if( !search_index_exists() ){
    @ <p>Indexed search is disabled
    style_finish_page();
    return;
  }
  search_sql_setup(g.db);
  style_submenu_element("Setup","%R/srchsetup");
  if( zId!=0 && (id = atoi(zId))>0 ){
    /* Show information about a single ftsdocs entry */
    style_header("Information about ftsdoc entry %d", id);
    style_submenu_element("Summary","%R/test-ftsdocs");
    db_prepare(&q,
      "SELECT type||rid, name, idxed, label, url, datetime(mtime)"
      "  FROM ftsdocs WHERE rowid=%d", id
    );
    if( db_step(&q)==SQLITE_ROW ){
      const char *zUrl = db_column_text(&q,4);
      const char *zDocId = db_column_text(&q,0);
      char *zName;
      char *z;
      @ <table border=0>
      @ <tr><td align='right'>rowid:<td>&nbsp;&nbsp;<td>%d(id)
      @ <tr><td align='right'>id:<td><td>%s(zDocId)
      @ <tr><td align='right'>name:<td><td>%h(db_column_text(&q,1))
      @ <tr><td align='right'>idxed:<td><td>%d(db_column_int(&q,2))
      @ <tr><td align='right'>label:<td><td>%h(db_column_text(&q,3))
      @ <tr><td align='right'>url:<td><td>
      @ <a href='%R%s(zUrl)'>%h(zUrl)</a>
      @ <tr><td align='right'>mtime:<td><td>%s(db_column_text(&q,5))
      z = db_text(0, "SELECT title FROM ftsidx WHERE rowid=%d",id);
      if( z && z[0] ){
        @ <tr><td align="right">title:<td><td>%h(z)
        fossil_free(z);
      }
      z = db_text(0, "SELECT body FROM ftsidx WHERE rowid=%d",id);
      if( z && z[0] ){
        @ <tr><td align="right" valign="top">body:<td><td>%h(z)
        fossil_free(z);
      }
      @ </table>
      zName = mprintf("Indexed '%c' docs",zDocId[0]);
      style_submenu_element(zName,"%R/test-ftsdocs?y=%c&ixed=1",zDocId[0]);
      zName = mprintf("Unindexed '%c' docs",zDocId[0]);
      style_submenu_element(zName,"%R/test-ftsdocs?y=%c&ixed=0",zDocId[0]);
    }
    db_finalize(&q);
    style_finish_page();
    return;
  }
  if( zType!=0 && zType[0]!=0 && zType[1]==0 &&
      zIdxed!=0 && (zIdxed[0]=='1' || zIdxed[0]=='0') && zIdxed[1]==0
  ){
    int ixed = zIdxed[0]=='1';
    char *zName;
    style_header("List of '%c' documents that are%s indexed",
                 zType[0], ixed ? "" : " not");
    style_submenu_element("Summary","%R/test-ftsdocs");
    if( ixed==0 ){
      zName = mprintf("Indexed '%c' docs",zType[0]);
      style_submenu_element(zName,"%R/test-ftsdocs?y=%c&ixed=1",zType[0]);
    }else{
      zName = mprintf("Unindexed '%c' docs",zType[0]);
      style_submenu_element(zName,"%R/test-ftsdocs?y=%c&ixed=0",zType[0]);
    }
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
    style_finish_page();
    return;
  }
  style_header("Summary of ftsdocs");
  db_prepare(&q,
     "SELECT type, sum(idxed IS TRUE), sum(idxed IS FALSE), count(*)"
     "  FROM ftsdocs"
     " GROUP BY 1 ORDER BY 4 DESC"
  );
  @ <table border=1 cellpadding=3 cellspacing=0>
  @ <thead>
  @ <tr><th>Type<th>Indexed<th>Unindexed<th>Total
  @ </thead>
  @ <tbody>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zType = db_column_text(&q,0);
    int nIndexed = db_column_int(&q, 1);
    int nUnindexed = db_column_int(&q, 2);
    int nTotal = db_column_int(&q, 3);
    @ <tr><td>%h(zType)
    if( nIndexed>0 ){
      @ <td align="right"><a href='%R/test-ftsdocs?y=%s(zType)&ixed=1'>\
      @ %d(nIndexed)</a>
    }else{
      @ <td align="right">0
    }
    if( nUnindexed>0 ){
      @ <td align="right"><a href='%R/test-ftsdocs?y=%s(zType)&ixed=0'>\
      @ %d(nUnindexed)</a>
    }else{
      @ <td align="right">0
    }
    @ <td align="right">%d(nTotal)
    @ </tr>
    cnt1 += nIndexed;
    cnt2 += nUnindexed;
    cnt3 += nTotal;
  }
  db_finalize(&q);
  @ </tbody><tfooter>
  @ <tr><th>Total<th align="right">%d(cnt1)<th align="right">%d(cnt2)
  @ <th align="right">%d(cnt3)
  @ </tfooter>
  @ </table>
  style_finish_page();
}


/*
** The Fts5MatchinfoCtx bits were all taken verbatim from:
**
** https://sqlite.org/src/finfo?name=ext/fts5/fts5_test_mi.c
*/

typedef struct Fts5MatchinfoCtx Fts5MatchinfoCtx;

#if INTERFACE
#ifndef SQLITE_AMALGAMATION
typedef unsigned int u32;
#endif
#endif

struct Fts5MatchinfoCtx {
  int nCol;                       /* Number of cols in FTS5 table */
  int nPhrase;                    /* Number of phrases in FTS5 query */
  char *zArg;                     /* nul-term'd copy of 2nd arg */
  int nRet;                       /* Number of elements in aRet[] */
  u32 *aRet;                      /* Array of 32-bit unsigned ints to return */
};


/*
** Return a pointer to the fts5_api pointer for database connection db.
** If an error occurs, return NULL and leave an error in the database
** handle (accessible using sqlite3_errcode()/errmsg()).
*/
static int fts5_api_from_db(sqlite3 *db, fts5_api **ppApi){
  sqlite3_stmt *pStmt = 0;
  int rc;

  *ppApi = 0;
  rc = sqlite3_prepare(db, "SELECT fts5(?1)", -1, &pStmt, 0);
  if( rc==SQLITE_OK ){
    sqlite3_bind_pointer(pStmt, 1, (void*)ppApi, "fts5_api_ptr", 0);
    (void)sqlite3_step(pStmt);
    rc = sqlite3_finalize(pStmt);
  }

  return rc;
}


/*
** Argument f should be a flag accepted by matchinfo() (a valid character
** in the string passed as the second argument). If it is not, -1 is
** returned. Otherwise, if f is a valid matchinfo flag, the value returned
** is the number of 32-bit integers added to the output array if the
** table has nCol columns and the query nPhrase phrases.
*/
static int fts5MatchinfoFlagsize(int nCol, int nPhrase, char f){
  int ret = -1;
  switch( f ){
    case 'p': ret = 1; break;
    case 'c': ret = 1; break;
    case 'x': ret = 3 * nCol * nPhrase; break;
    case 'y': ret = nCol * nPhrase; break;
    case 'b': ret = ((nCol + 31) / 32) * nPhrase; break;
    case 'n': ret = 1; break;
    case 'a': ret = nCol; break;
    case 'l': ret = nCol; break;
    case 's': ret = nCol; break;
  }
  return ret;
}

static int fts5MatchinfoIter(
  const Fts5ExtensionApi *pApi,   /* API offered by current FTS version */
  Fts5Context *pFts,              /* First arg to pass to pApi functions */
  Fts5MatchinfoCtx *p,
  int(*x)(const Fts5ExtensionApi*,Fts5Context*,Fts5MatchinfoCtx*,char,u32*)
){
  int i;
  int n = 0;
  int rc = SQLITE_OK;
  char f;
  for(i=0; (f = p->zArg[i]); i++){
    rc = x(pApi, pFts, p, f, &p->aRet[n]);
    if( rc!=SQLITE_OK ) break;
    n += fts5MatchinfoFlagsize(p->nCol, p->nPhrase, f);
  }
  return rc;
}

static int fts5MatchinfoXCb(
  const Fts5ExtensionApi *pApi,
  Fts5Context *pFts,
  void *pUserData
){
  Fts5PhraseIter iter;
  int iCol, iOff;
  u32 *aOut = (u32*)pUserData;
  int iPrev = -1;

  for(pApi->xPhraseFirst(pFts, 0, &iter, &iCol, &iOff);
      iCol>=0;
      pApi->xPhraseNext(pFts, &iter, &iCol, &iOff)
  ){
    aOut[iCol*3+1]++;
    if( iCol!=iPrev ) aOut[iCol*3 + 2]++;
    iPrev = iCol;
  }

  return SQLITE_OK;
}

static int fts5MatchinfoGlobalCb(
  const Fts5ExtensionApi *pApi,
  Fts5Context *pFts,
  Fts5MatchinfoCtx *p,
  char f,
  u32 *aOut
){
  int rc = SQLITE_OK;
  switch( f ){
    case 'p':
      aOut[0] = p->nPhrase;
      break;

    case 'c':
      aOut[0] = p->nCol;
      break;

    case 'x': {
      int i;
      for(i=0; i<p->nPhrase && rc==SQLITE_OK; i++){
        void *pPtr = (void*)&aOut[i * p->nCol * 3];
        rc = pApi->xQueryPhrase(pFts, i, pPtr, fts5MatchinfoXCb);
      }
      break;
    }

    case 'n': {
      sqlite3_int64 nRow;
      rc = pApi->xRowCount(pFts, &nRow);
      aOut[0] = (u32)nRow;
      break;
    }

    case 'a': {
      sqlite3_int64 nRow = 0;
      rc = pApi->xRowCount(pFts, &nRow);
      if( nRow==0 ){
        memset(aOut, 0, sizeof(u32) * p->nCol);
      }else{
        int i;
        for(i=0; rc==SQLITE_OK && i<p->nCol; i++){
          sqlite3_int64 nToken;
          rc = pApi->xColumnTotalSize(pFts, i, &nToken);
          if( rc==SQLITE_OK){
            aOut[i] = (u32)((2*nToken + nRow) / (2*nRow));
          }
        }
      }
      break;
    }

  }
  return rc;
}

static int fts5MatchinfoLocalCb(
  const Fts5ExtensionApi *pApi,
  Fts5Context *pFts,
  Fts5MatchinfoCtx *p,
  char f,
  u32 *aOut
){
  int i;
  int rc = SQLITE_OK;

  switch( f ){
    case 'b': {
      int iPhrase;
      int nInt = ((p->nCol + 31) / 32) * p->nPhrase;
      for(i=0; i<nInt; i++) aOut[i] = 0;

      for(iPhrase=0; iPhrase<p->nPhrase; iPhrase++){
        Fts5PhraseIter iter;
        int iCol;
        for(pApi->xPhraseFirstColumn(pFts, iPhrase, &iter, &iCol);
            iCol>=0;
            pApi->xPhraseNextColumn(pFts, &iter, &iCol)
        ){
          aOut[iPhrase * ((p->nCol+31)/32) + iCol/32] |= ((u32)1 << iCol%32);
        }
      }

      break;
    }

    case 'x':
    case 'y': {
      int nMul = (f=='x' ? 3 : 1);
      int iPhrase;

      for(i=0; i<(p->nCol*p->nPhrase); i++) aOut[i*nMul] = 0;

      for(iPhrase=0; iPhrase<p->nPhrase; iPhrase++){
        Fts5PhraseIter iter;
        int iOff, iCol;
        for(pApi->xPhraseFirst(pFts, iPhrase, &iter, &iCol, &iOff);
            iOff>=0;
            pApi->xPhraseNext(pFts, &iter, &iCol, &iOff)
        ){
          aOut[nMul * (iCol + iPhrase * p->nCol)]++;
        }
      }

      break;
    }

    case 'l': {
      for(i=0; rc==SQLITE_OK && i<p->nCol; i++){
        int nToken;
        rc = pApi->xColumnSize(pFts, i, &nToken);
        aOut[i] = (u32)nToken;
      }
      break;
    }

    case 's': {
      int nInst;

      memset(aOut, 0, sizeof(u32) * p->nCol);

      rc = pApi->xInstCount(pFts, &nInst);
      for(i=0; rc==SQLITE_OK && i<nInst; i++){
        int iPhrase, iOff, iCol = 0;
        int iNextPhrase;
        int iNextOff;
        u32 nSeq = 1;
        int j;

        rc = pApi->xInst(pFts, i, &iPhrase, &iCol, &iOff);
        iNextPhrase = iPhrase+1;
        iNextOff = iOff+pApi->xPhraseSize(pFts, 0);
        for(j=i+1; rc==SQLITE_OK && j<nInst; j++){
          int ip, ic, io;
          rc = pApi->xInst(pFts, j, &ip, &ic, &io);
          if( ic!=iCol || io>iNextOff ) break;
          if( ip==iNextPhrase && io==iNextOff ){
            nSeq++;
            iNextPhrase = ip+1;
            iNextOff = io + pApi->xPhraseSize(pFts, ip);
          }
        }

        if( nSeq>aOut[iCol] ) aOut[iCol] = nSeq;
      }

      break;
    }
  }
  return rc;
}

static Fts5MatchinfoCtx *fts5MatchinfoNew(
  const Fts5ExtensionApi *pApi,   /* API offered by current FTS version */
  Fts5Context *pFts,              /* First arg to pass to pApi functions */
  sqlite3_context *pCtx,          /* Context for returning error message */
  const char *zArg                /* Matchinfo flag string */
){
  Fts5MatchinfoCtx *p;
  int nCol;
  int nPhrase;
  int i;
  int nInt;
  sqlite3_int64 nByte;
  int rc;

  nCol = pApi->xColumnCount(pFts);
  nPhrase = pApi->xPhraseCount(pFts);

  nInt = 0;
  for(i=0; zArg[i]; i++){
    int n = fts5MatchinfoFlagsize(nCol, nPhrase, zArg[i]);
    if( n<0 ){
      char *zErr = sqlite3_mprintf("unrecognized matchinfo flag: %c", zArg[i]);
      sqlite3_result_error(pCtx, zErr, -1);
      sqlite3_free(zErr);
      return 0;
    }
    nInt += n;
  }

  nByte = sizeof(Fts5MatchinfoCtx)          /* The struct itself */
         + sizeof(u32) * nInt               /* The p->aRet[] array */
         + (i+1);                           /* The p->zArg string */
  p = (Fts5MatchinfoCtx*)sqlite3_malloc64(nByte);
  if( p==0 ){
    sqlite3_result_error_nomem(pCtx);
    return 0;
  }
  memset(p, 0, nByte);

  p->nCol = nCol;
  p->nPhrase = nPhrase;
  p->aRet = (u32*)&p[1];
  p->nRet = nInt;
  p->zArg = (char*)&p->aRet[nInt];
  memcpy(p->zArg, zArg, i);

  rc = fts5MatchinfoIter(pApi, pFts, p, fts5MatchinfoGlobalCb);
  if( rc!=SQLITE_OK ){
    sqlite3_result_error_code(pCtx, rc);
    sqlite3_free(p);
    p = 0;
  }

  return p;
}

static void fts5MatchinfoFunc(
  const Fts5ExtensionApi *pApi,   /* API offered by current FTS version */
  Fts5Context *pFts,              /* First arg to pass to pApi functions */
  sqlite3_context *pCtx,          /* Context for returning result/error */
  int nVal,                       /* Number of values in apVal[] array */
  sqlite3_value **apVal           /* Array of trailing arguments */
){
  const char *zArg;
  Fts5MatchinfoCtx *p;
  int rc = SQLITE_OK;

  if( nVal>0 ){
    zArg = (const char*)sqlite3_value_text(apVal[0]);
  }else{
    zArg = "pcx";
  }

  p = (Fts5MatchinfoCtx*)pApi->xGetAuxdata(pFts, 0);
  if( p==0 || sqlite3_stricmp(zArg, p->zArg) ){
    p = fts5MatchinfoNew(pApi, pFts, pCtx, zArg);
    if( p==0 ){
      rc = SQLITE_NOMEM;
    }else{
      rc = pApi->xSetAuxdata(pFts, p, sqlite3_free);
    }
  }

  if( rc==SQLITE_OK ){
    rc = fts5MatchinfoIter(pApi, pFts, p, fts5MatchinfoLocalCb);
  }
  if( rc!=SQLITE_OK ){
    sqlite3_result_error_code(pCtx, rc);
  }else{
    /* No errors has occured, so return a copy of the array of integers. */
    int nByte = p->nRet * sizeof(u32);
    sqlite3_result_blob(pCtx, (void*)p->aRet, nByte, SQLITE_TRANSIENT);
  }
}

int db_register_fts5(sqlite3 *db){
  int rc;                         /* Return code */
  fts5_api *pApi;                 /* FTS5 API functions */

  /* Extract the FTS5 API pointer from the database handle. The
  ** fts5_api_from_db() function above is copied verbatim from the
  ** FTS5 documentation. Refer there for details. */
  rc = fts5_api_from_db(db, &pApi);
  if( rc!=SQLITE_OK ) return rc;

  /* If fts5_api_from_db() returns NULL, then either FTS5 is not registered
  ** with this database handle, or an error (OOM perhaps?) has occurred.
  **
  ** Also check that the fts5_api object is version 2 or newer.
  */
  if( pApi==0 || pApi->iVersion<2 ){
    return SQLITE_ERROR;
  }

  /* Register the implementation of matchinfo() */
  rc = pApi->xCreateFunction(pApi, "matchinfo", 0, fts5MatchinfoFunc, 0);

  return rc;
}
