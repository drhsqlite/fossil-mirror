/*
** Copyright (c) 2014 D. Richard Hipp
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
** This file contains code to implement a full-text search function in 
** Fossil using the FTS4 feature of SQLite.
*/
#include "config.h"
#include "ftsearch.h"
#include <assert.h>

/*
** Document Codes:
**
** A "document code" is a string that describes a particular document.
** The first letter is the document type.  Second letter is '-' (for
** human readability.  Subsequent letters are a unique identifier for
** the document.
**
**    c-RID       -  Check-in comment
**    d-MID-FID   -  Diff on file FID from checkin MID
**    e-TAGID     -  Event text
**    f-FNID      -  File content (most recent version)
**    t-TKTID     -  Ticket text
**    w-TAGID     -  Wiki page (most recent version)
**
** The FTSEARCHXREF table provides a mapping between document codes
** (in the FTSID column) to the DOCID of the FTS4 table.
*/

/*
** Return a pointer to string that is the searchable content for a document.
** Return NULL if the document does not exist or if there is an error.
**
** Memory to hold the string is obtained from fossil_malloc() and must be
** released by the caller.
**
** If the second argument is not NULL, then use the second argument to get
** the document identifier.  If the second argument is NULL, then take the
** document identifier from the 3rd and subsequent characters of the
** document type.
*/
char *ftsearch_content(const char *zDocType, const char *zDocId){
  char *zRes = 0;  /* The result to be returned */
  int id;
  if( zDocId==0 ){
    if( zDocType[0]==0 || zDocType[1]==0 ) return 0;
    zDocId = zDocType + 2;
  }
  id = atoi(zDocId);
  switch( zDocType[0] ){
    case 'c': {    /* A check-in comment. zDocId is the RID */
      zRes = db_text(0,
        "SELECT coalesce(ecomment,comment) || char(10) ||"
        "       'user: ' || coalesce(euser,user) || char(10) ||"
        "       'branch: ' || coalesce((SELECT value FROM tagxref"
                                     "   WHERE tagid=%d AND tagtype>0"
                                     "     AND rid=%d),'trunk')"
        "  FROM event"
        " WHERE event.objid=%d"
        "   AND event.type GLOB 'c*'",
        TAG_BRANCH, id, id);
      break;
    }
    case 'f': {   /* A file with zDocId as the filename.fnid */
      zRes = db_text(0,
        "SELECT content(mlink.fid)"
        "  FROM filename, mlink, event"
        " WHERE filename.fnid=%d"
        "   AND mlink.fnid=filename.fnid"
        "   AND event.objid=mlink.mid"
        " ORDER BY event.mtime DESC LIMIT 1",
        id
      );
      break;
    }
    default: {
      /* No-op */
    }
  }
  return zRes;
}

/* Return a human-readable description for the document described by
** the arguments.
**
** See ftsearch_content() for further information
*/
char *ftsearch_description(
  const char *zDocType,
  const char *zDocId,
  int bLink                   /* Provide hyperlink in text if true */
){
  char *zRes = 0;  /* The result to be returned */
  int id;
  if( zDocId==0 ){
    if( zDocType[0]==0 || zDocType[1]==0 ) return 0;
    zDocId = zDocType + 2;
  }
  id = atoi(zDocId);
  switch( zDocType[0] ){
    case 'c': {    /* A check-in comment. zDocId is the RID */
      char *zUuid = db_text("","SELECT uuid FROM blob WHERE rid=%d", id);
      zRes = mprintf("Check-in [%S]", zUuid);
      fossil_free(zUuid);
      break;
    }
    case 'f': {    /* A file.  zDocId is the FNID */
      char *zName = db_text("","SELECT name FROM filename WHERE fnid=%d",id);
      zRes = mprintf("File %s", zName);
      fossil_free(zName);
      break;
    }
    default: {
      /* No-op */
    }
  }
  return zRes;
}

/*
** COMMAND: test-ftsearch-content
**
** Usage: %fossil test-ftsearch-content DOCUMENTCODE
**
** Return the content for the given DOCUMENTCODE.  This command is used
** for testing and debugging the ftsearch_content() method in the
** full-text search module.
*/
void test_doc_content_cmd(void){
  char *zContent = 0;
  char *zDesc = 0;
  db_find_and_open_repository(0, 0);
  verify_all_options();
  if( g.argc!=3 ) usage("DOCUMENTCODE");
  if( strlen(g.argv[2])>3 ){
    zContent = ftsearch_content(g.argv[2],0);
    zDesc = ftsearch_description(g.argv[2],0,0);
  }
  if( zDesc ){
    fossil_print("Description: %s\n", zDesc);
    fossil_free(zDesc);
  }
  if( zContent ){
    fossil_print(
      "Content -------------------------------------------------------------\n"
      "%s\n"
      "---------------------------------------------------------------------\n",
      zContent);
    fossil_free(zContent);
  }
}

/*
** Implementation of the ftsearch_content() SQL function.
*/
static void ftsearch_content_sql_func(
  sqlite3_context *context, 
  int argc, 
  sqlite3_value **argv
){
  const char *zDocType;     /* [cdeftw] */
  const char *zDocId;       /* Identifier based on zDocType */
  char *zRes;               /* Result */

  zDocType = (const char*)sqlite3_value_text(argv[0]);
  zDocId = argc>=2 ? (const char*)sqlite3_value_text(argv[1]) : 0;
  zRes = ftsearch_content(zDocType, zDocId);
  if( zRes ){
    sqlite3_result_text(context, zRes, -1, (void(*)(void*))fossil_free);
  }
}

/*
** Invoke this routine in order to install the ftsearch_content() SQL
** function on an SQLite database connection.
**
**      sqlite3_auto_extension(ftsearch_add_sql_func);
**
** to cause this extension to be automatically loaded into each new
** database connection.
*/
int ftsearch_add_sql_func(sqlite3 *db){
  int rc;
  rc = sqlite3_create_function(db, "ftsearch_content", 1, SQLITE_UTF8, 0,
                               ftsearch_content_sql_func, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "ftsearch_content", 2, SQLITE_UTF8, 0,
                                 ftsearch_content_sql_func, 0, 0);
  }
  return rc;
}

/*
** Delete the ftsearch tables, views, and indexes
*/
void ftsearch_disable_all(void){
  Stmt q;
  Blob sql;
  db_begin_transaction();
  db_prepare(&q,
    "SELECT type, name FROM %s.sqlite_master"
    " WHERE type IN ('table','view')"
    " AND name GLOB 'ftsearch*'"
    " AND name NOT GLOB 'ftsearch_*'",
    db_name("repository")
  );
  blob_init(&sql, 0, 0);
  while( db_step(&q)==SQLITE_ROW ){
    blob_appendf(&sql, "DROP %s IF EXISTS \"%w\";\n", 
        db_column_text(&q,0), db_column_text(&q,1));
  }
  db_finalize(&q);
  db_multi_exec("%s", blob_str(&sql)/*safe-for-%s*/);
  blob_reset(&sql);
  db_end_transaction(0);
}

/*
** Completely rebuild the ftsearch indexes from scratch
*/
void ftsearch_rebuild_all(void){
  const char *zEnables;
  db_begin_transaction();
  ftsearch_disable_all();
  zEnables = db_get("ftsearch-index-type", "cdeftw");

  /* If none of the search categories are enabled, then do not
  ** bother constructing the search tables
  */
  if( sqlite3_strglob("*[cdeftw]*", zEnables) ) return;

  /* The FTSSEARCHXREF table provides a mapping between the integer
  ** document-ids in FTS4 to the "document codes" that describe a 
  ** referenced object 
  */
  db_multi_exec(
    "CREATE TABLE %s.ftsearchxref(\n"
    "  docid INTEGER PRIMARY KEY,\n"  /* Link to ftsearch.docid */
    "  ftsid TEXT UNIQUE,\n"          /* The document code */
    "  mtime DATE\n"                  /* Timestamp on this object */
    ");\n",
    db_name("repository")
  );

  /* The FTSEARCHBODY view provides the content for the FTS4 table
  */
  db_multi_exec(
    "CREATE VIEW %s.ftsearchbody AS"
    " SELECT docid AS rowid, ftsearch_content(ftsid) AS body"
    "   FROM ftsearchxref;\n",
    db_name("repository")
  );

  /* This is the FTS4 table used for searching */
  db_multi_exec(
    "CREATE VIRTUAL TABLE %s.ftsearch"
    " USING fts4(content='ftsearchbody',body);",
    db_name("repository")
  );

  if( strchr(zEnables, 'c')!=0 ){
    /* Populate the FTSEARCHXREF table with references to all check-in
    ** comments currently in the event table
    */
    db_multi_exec(
      "INSERT INTO ftsearchxref(ftsid,mtime)"
      "  SELECT 'c-' || objid, mtime FROM event"
      "   WHERE type='ci';"
    );
  }

  if( strchr(zEnables, 'f')!=0 ){
    /* Populate the FTSEARCHXREF table with references to all files
    */
    db_multi_exec(
      "INSERT INTO ftsearchxref(ftsid,mtime)"
      "  SELECT 'f-' || filename.fnid, max(event.mtime)"
      "    FROM filename, mlink, event"
      "   WHERE mlink.fnid=filename.fnid"
      "     AND event.objid=mlink.mid"
      "     AND %s"
      "   GROUP BY 1",
      glob_expr("filename.name", db_get("search-file-glob","*"))
    );
  }

  /* Index every document mentioned in the FTSEARCHXREF table */
  db_multi_exec(
    "INSERT INTO ftsearch(docid,body)"
    "  SELECT docid, ftsearch_content(ftsid) FROM ftsearchxref;"
  );
  db_end_transaction(0);
}

/*
** COMMAND: search-config
**
** Usage: %fossil search PATTERN
**        %fossil search-config SUBCOMMAND ....
**
** The "search" command locates resources that contain the given web-search
** style PATTERN.  This only works if the repository has be configured to
** enable searching.
**
** The "search-config" is used to setup the search feature of the repository.
** Subcommands are:
**
**   fossil search-config doclist
**
**      List all the documents currently indexed
**
**   fossil search-config rebuild
**
**      Completely rebuild the search index.
**
**   fossil search-config reset
**
**      Disable search and remove the search indexes from the repository.
**
**   fossil search-config setting NAME ?VALUE?
**
**      Set or query a search setting.  NAMES are:
**         file-glob             Comma-separated list of GLOBs for file search
**         ticket-expr           SQL expression to render TICKET content
**         ticketchng-expr       SQL expression to render TICKETCHNG content
**         index-type            Zero or more characters from [cdeftw]
**
**      The index-type determines what resources are indexed and available for
**      searching.  If the index-type is an empty string, the search is
**      complete disabled.  These are the valid index-types:
**         c: check-in comments
**         d: check-in difference marks
**         e: event text
**         f: file text (subject to the file-glob)
**         t: ticket text (requires ticket-expr and ticketchng-expr)
**         w: wiki pages
**
**      It is necessary to run "fossil search-config rebuild" after making
**      setting changes in order to reconstruct the search index
**
**   fossil search-config status
**
**      Report on the status of the search configuration.
*/
void ftsearch_cmd(void){
  static const char *azSettings[] = {
     "file-glob", "index-type", "ticket-expr", "ticketchng-expr"
  };
  const char *zSubCmd;
  int nSubCmd;
  db_find_and_open_repository(0, 0);
  verify_all_options();
  if( g.argc<3 ) usage("search PATTERN");
  zSubCmd = g.argv[2];
  nSubCmd = (int)strlen(zSubCmd);
  db_begin_transaction();
  if( strlen(g.argv[1])<=6 && g.argc==3 ){
    /* This must be the "fossil search PATTERN" command */
    Stmt q;
    int i = 0;
#ifdef _WIN32
    const char *zMark1 = "*";
    const char *zMark2 = "*";
#else
    const char *zMark1 = "\033[1m";
    const char *zMark2 = "\033[0m";
#endif
    if( !db_table_exists("repository","ftsearch") ){
      fossil_fatal("search is disabled - see \"fossil help search\""
                   " for more information");
    }
    db_prepare(&q, "SELECT "
      "       snippet(ftsearch,%Q,%Q,'...'),"
      "       ftsearchxref.ftsid,"
      "       date(ftsearchxref.mtime)"
      "  FROM ftsearch, ftsearchxref"
      " WHERE ftsearch.body MATCH %Q"
      "   AND ftsearchxref.docid=ftsearch.docid"
      " ORDER BY ftsearchxref.mtime DESC LIMIT 50;",
      zMark1, zMark2, zSubCmd);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zSnippet = db_column_text(&q,0);
      char *zDesc = ftsearch_description(db_column_text(&q,1),0,0);
      const char *zDate = db_column_text(&q,2);
      if( i++ > 0 ){
        fossil_print("----------------------------------------------------\n");
      }
      fossil_print("%s (%s)\n%s\n", zDesc, zDate, zSnippet);
      fossil_free(zDesc);
    }
    db_finalize(&q);
  }else if( strncmp(zSubCmd, "doclist", nSubCmd)==0 ){
    if( db_table_exists("repository","ftsearch") ){
      Stmt q;
      db_prepare(&q, "SELECT ftsid, date(mtime) FROM ftsearchxref"
                     " ORDER BY mtime DESC");
      while( db_step(&q)==SQLITE_ROW ){
        const char *zDate = db_column_text(&q,1);
        const char *zFtsid = db_column_text(&q,0);
        char *zDesc = ftsearch_description(zFtsid,0,0);
        fossil_print("%s (%s)\n", zDesc, zDate);
        fossil_free(zDesc);
      }
      db_finalize(&q);
    }
  }else if( strncmp(zSubCmd, "rebuild", nSubCmd)==0 ){
    ftsearch_rebuild_all();
  }else if( strncmp(zSubCmd, "reset", nSubCmd)==0 ){
    ftsearch_disable_all();
  }else if( strncmp(zSubCmd, "settings", nSubCmd)==0 ){
    const char *zName = g.argv[3];
    const char *zValue = g.argc>=5 ? g.argv[4] : 0;
    char *zFullname;
    int i;
    if( g.argc<4 ) usage("setting NAME ?VALUE?");
    for(i=0; i<count(azSettings); i++){
      if( strcmp(zName, azSettings[i])==0 ) break;
    }
    if( i>=count(azSettings) ){
      Blob x;
      blob_init(&x,0,0);
      for(i=0; i<count(azSettings); i++) blob_appendf(&x," %s", azSettings[i]);
      fossil_fatal("unknown setting \"%s\" - should be one of:%s",
          zName, blob_str(&x));
    }
    zFullname = mprintf("search-%s", zName);
    if( zValue==0 ){
      zValue = db_get(zFullname, 0);
    }else{
      db_set(zFullname, zValue, 0);
    }
    if( zValue==0 ){
      fossil_print("%s is not defined\n", zName);
    }else{
      fossil_print("%s: %s\n", zName, zValue);
    }
  }else if( strncmp(zSubCmd, "status", nSubCmd)==0 ){
    int i;
    fossil_print("search settings:\n");
    for(i=0; i<count(azSettings); i++){
      char *zFullname = mprintf("search-%s", azSettings[i]);
      char *zValue = db_get(zFullname, 0);
      if( zValue==0 ){
        fossil_print("  %s is undefined\n", azSettings[i]);
      }else{
        fossil_print("  %s: %s\n", azSettings[i], zValue);
      }
      fossil_free(zFullname);
    }
    if( db_table_exists("repository","ftsearchxref") ){
      int n = db_int(0, "SELECT count(*) FROM ftsearchxref");
      fossil_print("search is enabled with %d documents indexed\n", n);
    }else{
      fossil_print("search is disabled\n");
    }
  }
  db_end_transaction(0);
}
