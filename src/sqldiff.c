/*
** Copyright (c) 2024 D. Richard Hipp
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
** This file contains code used to compute a "diff" between two SQLite
** database files for display by Fossil.
**
** Fossil normally only computes diffs on text files.  But I was inspired
** by a Hacker News post to add support for diffs of other kinds of files
** as well.  The HN post in question is:
**
**      https://news.ycombinator.com/item?id=42141370
**	
**      eternityforest | on: On Building Git for Lawyers
**      I really think Git should just add builtin support for binaries,
**      and diffing for SQLite and .zip. it's not like it would be all
**      that much code....
**
** This file borrows a lot of code from the "sqldiff.c" module of
** SQLite. (https://sqlite.org/src/file/tool/sqldiff.c)
*/
#include "config.h"
#include "sqldiff.h"
#include <ctype.h>

#if INTERFACE
/*
** Context for an SQL diff
*/
struct SqlDiffCtx {
  int bSchemaOnly;          /* Only show schema differences */
  int bSchemaPK;            /* Use the schema-defined PK, not the true PK */
  int bHandleVtab;          /* Handle fts3, fts4, fts5 and rtree vtabs */
  unsigned fDebug;          /* Debug flags */
  int bSchemaCompare;       /* Doing single-table sqlite_schema compare */
  int nErr;                 /* Number of errors encountered */
  Blob *out;                /* Write the diff output here */
  sqlite3 *db;              /* The database connection */
};

/*
** Allowed values for SqlDiffCtx.fDebug
*/
#define SQLDIFF_COLUMN_NAMES  0x000001
#define SQLDIFF_DIFF_SQL      0x000002
#define SQLDIFF_SHOW_ERRORS   0x000004

#endif /* INTERFACE */


/*
** Return true if the input Blob superficially resembles an SQLite
** database file.
*/
static int looks_like_sqlite_db(const Blob *pDb){
  int sz = blob_size(pDb);
  const u8 *a = (const u8*)blob_buffer(pDb);
  static const u8 aSqliteHeader[16] = {
    0x53, 0x51, 0x4c, 0x69, 0x74, 0x65, 0x20, 0x66,
    0x6f, 0x72, 0x6d, 0x61, 0x74, 0x20, 0x33, 0x00
  };

  if( sz<512 ) return 0;
  if( (sz%512)!=0 ) return 0;
  if( memcmp(aSqliteHeader,a,16)!=0 ) return 0;
  return 1;
}

/*
** Clear and free an sqlite3_str object
*/
static void strFree(sqlite3_str *pStr){
  sqlite3_free(sqlite3_str_finish(pStr));
}

/*
** Print an error message for an error that occurs at runtime.
*/
static void sqldiffError(SqlDiffCtx *p, const char *zFormat, ...){
  if( p->fDebug & SQLDIFF_SHOW_ERRORS ){
    sqlite3_str *pOut = sqlite3_str_new(0);
    va_list ap;
    va_start(ap, zFormat);
    sqlite3_str_vappendf(pOut, zFormat, ap);
    va_end(ap);
    fossil_print("%s\n", sqlite3_str_value(pOut));
    strFree(pOut);
  }
  p->nErr++;
}

/* Safely quote an SQL identifier.  Use the minimum amount of transformation
** necessary to allow the string to be used with %s.
**
** Space to hold the returned string is obtained from sqlite3_malloc().  The
** caller is responsible for ensuring this space is freed when no longer
** needed.
*/
static char *safeId(const char *zId){
  int i, x;
  char c;
  if( zId[0]==0 ) return sqlite3_mprintf("\"\"");
  for(i=x=0; (c = zId[i])!=0; i++){
    if( !isalpha(c) && c!='_' ){
      if( i>0 && isdigit(c) ){
        x++;
      }else{
        return sqlite3_mprintf("\"%w\"", zId);
      }
    }
  }
  if( x || !sqlite3_keyword_check(zId,i) ){
    return sqlite3_mprintf("%s", zId);
  }
  return sqlite3_mprintf("\"%w\"", zId);
}

/*
** Prepare a new SQL statement.  Print an error and abort if anything
** goes wrong.
*/
static sqlite3_stmt *sqldiff_vprepare(
  SqlDiffCtx *p,
  const char *zFormat,
  va_list ap
){
  char *zSql;
  int rc;
  sqlite3_stmt *pStmt;

  zSql = sqlite3_vmprintf(zFormat, ap);
  if( zSql==0 ) fossil_fatal("out of memory\n");
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
  if( rc ){
    sqldiffError(p, "SQL statement error: %s\n\"%s\"", sqlite3_errmsg(p->db),
                 zSql);
    sqlite3_finalize(pStmt);
    pStmt = 0;
  }
  sqlite3_free(zSql);
  return pStmt;
}
static sqlite3_stmt *sqldiff_prepare(SqlDiffCtx *p, const char *zFormat, ...){
  va_list ap;
  sqlite3_stmt *pStmt;
  va_start(ap, zFormat);
  pStmt = sqldiff_vprepare(p, zFormat, ap);
  va_end(ap);
  return pStmt;
}

/*
** Free a list of strings
*/
static void namelistFree(char **az){
  if( az ){
    int i;
    for(i=0; az[i]; i++) sqlite3_free(az[i]);
    sqlite3_free(az);
  }
}

/*
** Return a list of column names [a] for the table zDb.zTab.  Space to
** hold the list is obtained from sqlite3_malloc() and should released
** using namelistFree() when no longer needed.
**
** Primary key columns are listed first, followed by data columns.
** The number of columns in the primary key is returned in *pnPkey.
**
** Normally [a], the "primary key" in the previous sentence is the true
** primary key - the rowid or INTEGER PRIMARY KEY for ordinary tables
** or the declared PRIMARY KEY for WITHOUT ROWID tables.  However, if
** the p->bSchemaPK flag is set, then the schema-defined PRIMARY KEY is
** used in all cases.  In that case, entries that have NULL values in
** any of their primary key fields will be excluded from the analysis.
**
** If the primary key for a table is the rowid but rowid is inaccessible,
** then this routine returns a NULL pointer.
**
** [a. If the lone, named table is "sqlite_schema", "rootpage" column is
**  omitted and the "type" and "name" columns are made to be the PK.]
**
** Examples:
**    CREATE TABLE t1(a INT UNIQUE, b INTEGER, c TEXT, PRIMARY KEY(c));
**    *pnPKey = 1;
**    az = { "rowid", "a", "b", "c", 0 }  // Normal case
**    az = { "c", "a", "b", 0 }           // g.bSchemaPK==1
**
**    CREATE TABLE t2(a INT UNIQUE, b INTEGER, c TEXT, PRIMARY KEY(b));
**    *pnPKey = 1;
**    az = { "b", "a", "c", 0 }
**
**    CREATE TABLE t3(x,y,z,PRIMARY KEY(y,z));
**    *pnPKey = 1                         // Normal case
**    az = { "rowid", "x", "y", "z", 0 }  // Normal case
**    *pnPKey = 2                         // g.bSchemaPK==1
**    az = { "y", "x", "z", 0 }           // g.bSchemaPK==1
**
**    CREATE TABLE t4(x,y,z,PRIMARY KEY(y,z)) WITHOUT ROWID;
**    *pnPKey = 2
**    az = { "y", "z", "x", 0 }
**
**    CREATE TABLE t5(rowid,_rowid_,oid);
**    az = 0     // The rowid is not accessible
*/
static char **columnNames(
  SqlDiffCtx *p,                  /* Diffing context */
  const char *zDb,                /* Database ("aaa" or "bbb") to query */
  const char *zTab,               /* Name of table to return details of */
  int *pnPKey,                    /* OUT: Number of PK columns */
  int *pbRowid                    /* OUT: True if PK is an implicit rowid */
){
  char **az = 0;           /* List of column names to be returned */
  int naz = 0;             /* Number of entries in az[] */
  sqlite3_stmt *pStmt;     /* SQL statement being run */
  char *zPkIdxName = 0;    /* Name of the PRIMARY KEY index */
  int truePk = 0;          /* PRAGMA table_info indentifies the PK to use */
  int nPK = 0;             /* Number of PRIMARY KEY columns */
  int i, j;                /* Loop counters */

  if( p->bSchemaPK==0 ){
    /* Normal case:  Figure out what the true primary key is for the table.
    **   *  For WITHOUT ROWID tables, the true primary key is the same as
    **      the schema PRIMARY KEY, which is guaranteed to be present.
    **   *  For rowid tables with an INTEGER PRIMARY KEY, the true primary
    **      key is the INTEGER PRIMARY KEY.
    **   *  For all other rowid tables, the rowid is the true primary key.
    */
    pStmt = sqldiff_prepare(p, "PRAGMA %s.index_list=%Q", zDb, zTab);
    while( SQLITE_ROW==sqlite3_step(pStmt) ){
      if( sqlite3_stricmp((const char*)sqlite3_column_text(pStmt,3),"pk")==0 ){
        zPkIdxName = sqlite3_mprintf("%s", sqlite3_column_text(pStmt, 1));
        break;
      }
    }
    sqlite3_finalize(pStmt);
    if( zPkIdxName ){
      int nKey = 0;
      int nCol = 0;
      truePk = 0;
      pStmt = sqldiff_prepare(p, "PRAGMA %s.index_xinfo=%Q", zDb, zPkIdxName);
      while( SQLITE_ROW==sqlite3_step(pStmt) ){
        nCol++;
        if( sqlite3_column_int(pStmt,5) ){ nKey++; continue; }
        if( sqlite3_column_int(pStmt,1)>=0 ) truePk = 1;
      }
      if( nCol==nKey ) truePk = 1;
      if( truePk ){
        nPK = nKey;
      }else{
        nPK = 1;
      }
      sqlite3_finalize(pStmt);
      sqlite3_free(zPkIdxName);
    }else{
      truePk = 1;
      nPK = 1;
    }
    pStmt = sqldiff_prepare(p, "PRAGMA %s.table_info=%Q", zDb, zTab);
  }else{
    /* The p->bSchemaPK==1 case:  Use whatever primary key is declared
    ** in the schema.  The "rowid" will still be used as the primary key
    ** if the table definition does not contain a PRIMARY KEY.
    */
    nPK = 0;
    pStmt = sqldiff_prepare(p, "PRAGMA %s.table_info=%Q", zDb, zTab);
    while( SQLITE_ROW==sqlite3_step(pStmt) ){
      if( sqlite3_column_int(pStmt,5)>0 ) nPK++;
    }
    sqlite3_reset(pStmt);
    if( nPK==0 ) nPK = 1;
    truePk = 1;
  }
  if( p->bSchemaCompare ){
    assert( sqlite3_stricmp(zTab,"sqlite_schema")==0
            || sqlite3_stricmp(zTab,"sqlite_master")==0 );
    /* For sqlite_schema, will use type and name as the PK. */
    nPK = 2;
    truePk = 0;
  }
  *pnPKey = nPK;
  naz = nPK;
  az = sqlite3_malloc( sizeof(char*)*(nPK+1) );
  if( az==0 ) fossil_fatal("out of memory\n");
  memset(az, 0, sizeof(char*)*(nPK+1));
  if( p->bSchemaCompare ){
    az[0] = sqlite3_mprintf("%s", "type");
    az[1] = sqlite3_mprintf("%s", "name");
  }
  while( SQLITE_ROW==sqlite3_step(pStmt) ){
    char * sid = safeId((char*)sqlite3_column_text(pStmt,1));
    int iPKey;
    if( truePk && (iPKey = sqlite3_column_int(pStmt,5))>0 ){
      az[iPKey-1] = sid;
    }else{
      if( !p->bSchemaCompare
          || !(strcmp(sid,"rootpage")==0
               ||strcmp(sid,"name")==0
               ||strcmp(sid,"type")==0)){
        az = sqlite3_realloc(az, sizeof(char*)*(naz+2) );
        if( az==0 ) fossil_fatal("out of memory\n");
        az[naz++] = sid;
      }
    }
  }
  sqlite3_finalize(pStmt);
  if( az ) az[naz] = 0;

  /* If it is non-NULL, set *pbRowid to indicate whether or not the PK of 
  ** this table is an implicit rowid (*pbRowid==1) or not (*pbRowid==0).  */
  if( pbRowid ) *pbRowid = (az[0]==0);

  /* If this table has an implicit rowid for a PK, figure out how to refer
  ** to it. There are usually three options - "rowid", "_rowid_" and "oid".
  ** Any of these will work, unless the table has an explicit column of the
  ** same name or the sqlite_schema tables are to be compared. In the latter
  ** case, pretend that the "true" primary key is the name column, which
  ** avoids extraneous diffs against the schemas due to rowid variance. */
  if( az[0]==0 ){
    const char *azRowid[] = { "rowid", "_rowid_", "oid" };
    for(i=0; i<sizeof(azRowid)/sizeof(azRowid[0]); i++){
      for(j=1; j<naz; j++){
        if( sqlite3_stricmp(az[j], azRowid[i])==0 ) break;
      }
      if( j>=naz ){
        az[0] = sqlite3_mprintf("%s", azRowid[i]);
        break;
      }
    }
    if( az[0]==0 ){
      for(i=1; i<naz; i++) sqlite3_free(az[i]);
      sqlite3_free(az);
      az = 0;
    }
  }
  return az;
}

/*
** Print the sqlite3_value X as an SQL literal.
*/
static void printQuoted(Blob *out, sqlite3_value *X){
  switch( sqlite3_value_type(X) ){
    case SQLITE_FLOAT: {
      double r1;
      char zBuf[50];
      r1 = sqlite3_value_double(X);
      sqlite3_snprintf(sizeof(zBuf), zBuf, "%!.15g", r1);
      blob_appendf(out, "%s", zBuf);
      break;
    }
    case SQLITE_INTEGER: {
      blob_appendf(out, "%lld", sqlite3_value_int64(X));
      break;
    }
    case SQLITE_BLOB: {
      const unsigned char *zBlob = sqlite3_value_blob(X);
      int nBlob = sqlite3_value_bytes(X);
      if( zBlob ){
        int i;
        blob_appendf(out, "x'");
        for(i=0; i<nBlob; i++){
          blob_appendf(out, "%02x", zBlob[i]);
        }
        blob_appendf(out, "'");
      }else{
        /* Could be an OOM, could be a zero-byte blob */
        blob_appendf(out, "X''");
      }
      break;
    }
    case SQLITE_TEXT: {
      const unsigned char *zArg = sqlite3_value_text(X);

      if( zArg==0 ){
        blob_appendf(out, "NULL");
      }else{
        int inctl = 0;
        int i, j;
        blob_appendf(out, "'");
        for(i=j=0; zArg[i]; i++){
          char c = zArg[i];
          int ctl = iscntrl((unsigned char)c);
          if( ctl>inctl ){
            inctl = ctl;
            blob_appendf(out, "%.*s'||X'%02x", i-j, &zArg[j], c);
            j = i+1;
          }else if( ctl ){
            blob_appendf(out, "%02x", c);
            j = i+1;
          }else{
            if( inctl ){
              inctl = 0;
              blob_appendf(out, "'\n||'");
            }
            if( c=='\'' ){
              blob_appendf(out, "%.*s'", i-j+1, &zArg[j]);
              j = i+1;
            }
          }
        }
        blob_appendf(out, "%s'", &zArg[j]);
      }
      break;
    }
    case SQLITE_NULL: {
      blob_appendf(out, "NULL");
      break;
    }
  }
}

/*
** Output SQL that will recreate the bbb.zTab table.
*/
static void dump_table(SqlDiffCtx *p, const char *zTab){
  char *zId = safeId(zTab); /* Name of the table */
  char **az = 0;            /* List of columns */
  int nPk;                  /* Number of true primary key columns */
  int nCol;                 /* Number of data columns */
  int i;                    /* Loop counter */
  sqlite3_stmt *pStmt;      /* SQL statement */
  const char *zSep;         /* Separator string */
  sqlite3_str *pIns;        /* Beginning of the INSERT statement */

  pStmt = sqldiff_prepare(p, 
              "SELECT sql FROM bbb.sqlite_schema WHERE name=%Q", zTab);
  if( SQLITE_ROW==sqlite3_step(pStmt) ){
    blob_appendf(p->out, "%s;\n", sqlite3_column_text(pStmt,0));
  }
  sqlite3_finalize(pStmt);
  if( !p->bSchemaOnly ){
    az = columnNames(p, "bbb", zTab, &nPk, 0);
    pIns = sqlite3_str_new(0);
    if( az==0 ){
      pStmt = sqldiff_prepare(p, "SELECT * FROM bbb.%s", zId);
      sqlite3_str_appendf(pIns,"INSERT INTO %s VALUES", zId);
    }else{
      sqlite3_str *pSql = sqlite3_str_new(0);
      zSep =  "SELECT";
      for(i=0; az[i]; i++){
        sqlite3_str_appendf(pSql, "%s %s", zSep, az[i]);
        zSep = ",";
      }
      sqlite3_str_appendf(pSql," FROM bbb.%s", zId);
      zSep = " ORDER BY";
      for(i=1; i<=nPk; i++){
        sqlite3_str_appendf(pSql, "%s %d", zSep, i);
        zSep = ",";
      }
      pStmt = sqldiff_prepare(p, "%s", sqlite3_str_value(pSql));
      strFree(pSql);
      sqlite3_str_appendf(pIns, "INSERT INTO %s", zId);
      zSep = "(";
      for(i=0; az[i]; i++){
        sqlite3_str_appendf(pIns, "%s%s", zSep, az[i]);
        zSep = ",";
      }
      sqlite3_str_appendf(pIns,") VALUES");
      namelistFree(az);
    }
    nCol = sqlite3_column_count(pStmt);
    while( SQLITE_ROW==sqlite3_step(pStmt) ){
      blob_appendf(p->out, "%s",sqlite3_str_value(pIns));
      zSep = "(";
      for(i=0; i<nCol; i++){
        blob_appendf(p->out, "%s",zSep);
        printQuoted(p->out, sqlite3_column_value(pStmt,i));
        zSep = ",";
      }
      blob_appendf(p->out, ");\n");
    }
    sqlite3_finalize(pStmt);
    strFree(pIns);
  } /* endif !p->bSchemaOnly */
  pStmt = sqldiff_prepare(p, "SELECT sql FROM bbb.sqlite_schema"
                     " WHERE type='index' AND tbl_name=%Q AND sql IS NOT NULL",
                     zTab);
  while( SQLITE_ROW==sqlite3_step(pStmt) ){
    blob_appendf(p->out, "%s;\n", sqlite3_column_text(pStmt,0));
  }
  sqlite3_finalize(pStmt);
  sqlite3_free(zId);
}


/*
** Compute all differences for a single table, except if the
** table name is sqlite_schema, ignore the rootpage column.
*/
static void diff_one_table(SqlDiffCtx *p, const char *zTab){
  char *zId = safeId(zTab); /* Name of table (translated for us in SQL) */
  char **az = 0;            /* Columns in aaa */
  char **az2 = 0;           /* Columns in bbb */
  int nPk;                  /* Primary key columns in aaa */
  int nPk2;                 /* Primary key columns in bbb */
  int n = 0;                /* Number of columns in aaa */
  int n2;                   /* Number of columns in bbb */
  int nQ;                   /* Number of output columns in the diff query */
  int i;                    /* Loop counter */
  const char *zSep;         /* Separator string */
  sqlite3_str *pSql;        /* Comparison query */
  sqlite3_stmt *pStmt;      /* Query statement to do the diff */
  const char *zLead =       /* Becomes line-comment for sqlite_schema */
    (p->bSchemaCompare)? "-- " : "";

  pSql = sqlite3_str_new(0);
  if( p->fDebug==SQLDIFF_COLUMN_NAMES ){
    /* Simply run columnNames() on all tables of the origin
    ** database and show the results.  This is used for testing
    ** and debugging of the columnNames() function.
    */
    az = columnNames(p, "bbb",zTab, &nPk, 0);
    if( az==0 ){
      fossil_print("Rowid not accessible for %s\n", zId);
    }else{
      fossil_print("%s:", zId);
      for(i=0; az[i]; i++){
        fossil_print(" %s", az[i]);
        if( i+1==nPk ) fossil_print(" *");
      }
      fossil_print("\n");
    }
    goto end_diff_one_table;
  }

  if( sqlite3_table_column_metadata(p->db,"bbb",zTab,0,0,0,0,0,0) ){
    if( !sqlite3_table_column_metadata(p->db,"aaa",zTab,0,0,0,0,0,0) ){
      /* Table missing from second database. */
      if( p->bSchemaCompare ){
        blob_appendf(p->out, "-- 2nd DB has no %s table\n", zTab);
      }else{
        blob_appendf(p->out, "DROP TABLE %s;\n", zId);
      }
    }
    goto end_diff_one_table;
  }

  if( sqlite3_table_column_metadata(p->db,"aaa",zTab,0,0,0,0,0,0) ){
    /* Table missing from source */
    if( p->bSchemaCompare ){
      blob_appendf(p->out, "-- 1st DB has no %s table\n", zTab);
    }else{
      dump_table(p, zTab);
    }
    goto end_diff_one_table;
  }

  az = columnNames(p, "aaa", zTab, &nPk, 0);
  az2 = columnNames(p, "bbb", zTab, &nPk2, 0);
  if( az && az2 ){
    for(n=0; az[n] && az2[n]; n++){
      if( sqlite3_stricmp(az[n],az2[n])!=0 ) break;
    }
  }
  if( az==0
   || az2==0
   || nPk!=nPk2
   || az[n]
  ){
    /* Schema mismatch */
    blob_appendf(p->out, "%sDROP TABLE %s; -- due to schema mismatch\n",
                 zLead, zId);
    dump_table(p, zTab);
    goto end_diff_one_table;
  }

  /* Build the comparison query */
  for(n2=n; az2[n2]; n2++){
    char *zNTab = safeId(az2[n2]);
    blob_appendf(p->out, "ALTER TABLE %s ADD COLUMN %s;\n", zId, zNTab);
    sqlite3_free(zNTab);
  }
  nQ = nPk2+1+2*(n2-nPk2);
  if( n2>nPk2 ){
    zSep = "SELECT ";
    for(i=0; i<nPk; i++){
      sqlite3_str_appendf(pSql, "%sB.%s", zSep, az[i]);
      zSep = ", ";
    }
    sqlite3_str_appendf(pSql, ", 1 /* changed row */");
    while( az[i] ){
      sqlite3_str_appendf(pSql, ", A.%s IS NOT B.%s, B.%s",
                az[i], az2[i], az2[i]);
      i++;
    }
    while( az2[i] ){
      sqlite3_str_appendf(pSql, ", B.%s IS NOT NULL, B.%s",
                az2[i], az2[i]);
      i++;
    }
    sqlite3_str_appendf(pSql, "\n  FROM aaa.%s A, bbb.%s B\n", zId, zId);
    zSep = " WHERE";
    for(i=0; i<nPk; i++){
      sqlite3_str_appendf(pSql, "%s A.%s=B.%s", zSep, az[i], az[i]);
      zSep = " AND";
    }
    zSep = "\n   AND (";
    while( az[i] ){
      sqlite3_str_appendf(pSql, "%sA.%s IS NOT B.%s%s\n",
                zSep, az[i], az2[i], az2[i+1]==0 ? ")" : "");
      zSep = "        OR ";
      i++;
    }
    while( az2[i] ){
      sqlite3_str_appendf(pSql, "%sB.%s IS NOT NULL%s\n",
                zSep, az2[i], az2[i+1]==0 ? ")" : "");
      zSep = "        OR ";
      i++;
    }
    sqlite3_str_appendf(pSql, " UNION ALL\n");
  }
  zSep = "SELECT ";
  for(i=0; i<nPk; i++){
    sqlite3_str_appendf(pSql, "%sA.%s", zSep, az[i]);
    zSep = ", ";
  }
  sqlite3_str_appendf(pSql, ", 2 /* deleted row */");
  while( az2[i] ){
    sqlite3_str_appendf(pSql, ", NULL, NULL");
    i++;
  }
  sqlite3_str_appendf(pSql, "\n  FROM aaa.%s A\n", zId);
  sqlite3_str_appendf(pSql, " WHERE NOT EXISTS(SELECT 1 FROM bbb.%s B\n", zId);
  zSep =          "                   WHERE";
  for(i=0; i<nPk; i++){
    sqlite3_str_appendf(pSql, "%s A.%s=B.%s", zSep, az[i], az[i]);
    zSep = " AND";
  }
  sqlite3_str_appendf(pSql, ")\n");
  zSep = " UNION ALL\nSELECT ";
  for(i=0; i<nPk; i++){
    sqlite3_str_appendf(pSql, "%sB.%s", zSep, az[i]);
    zSep = ", ";
  }
  sqlite3_str_appendf(pSql, ", 3 /* inserted row */");
  while( az2[i] ){
    sqlite3_str_appendf(pSql, ", 1, B.%s", az2[i]);
    i++;
  }
  sqlite3_str_appendf(pSql, "\n  FROM bbb.%s B\n", zId);
  sqlite3_str_appendf(pSql, " WHERE NOT EXISTS(SELECT 1 FROM aaa.%s A\n", zId);
  zSep =          "                   WHERE";
  for(i=0; i<nPk; i++){
    sqlite3_str_appendf(pSql, "%s A.%s=B.%s", zSep, az[i], az[i]);
    zSep = " AND";
  }
  sqlite3_str_appendf(pSql, ")\n ORDER BY");
  zSep = " ";
  for(i=1; i<=nPk; i++){
    sqlite3_str_appendf(pSql, "%s%d", zSep, i);
    zSep = ", ";
  }
  sqlite3_str_appendf(pSql, ";\n");

  if( p->fDebug & SQLDIFF_DIFF_SQL ){ 
    fossil_print("SQL for %s:\n%s\n", zId, sqlite3_str_value(pSql));
    goto end_diff_one_table;
  }

  /* Drop indexes that are missing in the destination */
  pStmt = sqldiff_prepare(p, 
    "SELECT name FROM aaa.sqlite_schema"
    " WHERE type='index' AND tbl_name=%Q"
    "   AND sql IS NOT NULL"
    "   AND sql NOT IN (SELECT sql FROM bbb.sqlite_schema"
    "                    WHERE type='index' AND tbl_name=%Q"
    "                      AND sql IS NOT NULL)",
    zTab, zTab);
  while( SQLITE_ROW==sqlite3_step(pStmt) ){
    char *z = safeId((const char*)sqlite3_column_text(pStmt,0));
    blob_appendf(p->out, "DROP INDEX %s;\n", z);
    sqlite3_free(z);
  }
  sqlite3_finalize(pStmt);

  /* Run the query and output differences */
  if( !p->bSchemaOnly ){
    pStmt = sqldiff_prepare(p, "%s", sqlite3_str_value(pSql));
    while( SQLITE_ROW==sqlite3_step(pStmt) ){
      int iType = sqlite3_column_int(pStmt, nPk);
      if( iType==1 || iType==2 ){
        if( iType==1 ){       /* Change the content of a row */
          blob_appendf(p->out, "%sUPDATE %s", zLead, zId);
          zSep = " SET";
          for(i=nPk+1; i<nQ; i+=2){
            if( sqlite3_column_int(pStmt,i)==0 ) continue;
            blob_appendf(p->out, "%s %s=", zSep, az2[(i+nPk-1)/2]);
            zSep = ",";
            printQuoted(p->out, sqlite3_column_value(pStmt,i+1));
          }
        }else{                /* Delete a row */
          blob_appendf(p->out, "%sDELETE FROM %s", zLead, zId);
        }
        zSep = " WHERE";
        for(i=0; i<nPk; i++){
          blob_appendf(p->out, "%s %s=", zSep, az2[i]);
          printQuoted(p->out, sqlite3_column_value(pStmt,i));
          zSep = " AND";
        }
        blob_appendf(p->out, ";\n");
      }else{                  /* Insert a row */
        blob_appendf(p->out, "%sINSERT INTO %s(%s", zLead, zId, az2[0]);
        for(i=1; az2[i]; i++) blob_appendf(p->out, ",%s", az2[i]);
        blob_appendf(p->out, ") VALUES");
        zSep = "(";
        for(i=0; i<nPk2; i++){
          blob_appendf(p->out, "%s", zSep);
          zSep = ",";
          printQuoted(p->out, sqlite3_column_value(pStmt,i));
        }
        for(i=nPk2+2; i<nQ; i+=2){
          blob_appendf(p->out, ",");
          printQuoted(p->out, sqlite3_column_value(pStmt,i));
        }
        blob_appendf(p->out, ");\n");
      }
    }
    sqlite3_finalize(pStmt);
  } /* endif !p->bSchemaOnly */

  /* Create indexes that are missing in the source */
  pStmt = sqldiff_prepare(p,
    "SELECT sql FROM bbb.sqlite_schema"
    " WHERE type='index' AND tbl_name=%Q"
    "   AND sql IS NOT NULL"
    "   AND sql NOT IN (SELECT sql FROM aaa.sqlite_schema"
    "                    WHERE type='index' AND tbl_name=%Q"
    "                      AND sql IS NOT NULL)",
    zTab, zTab);
  while( SQLITE_ROW==sqlite3_step(pStmt) ){
    blob_appendf(p->out, "%s;\n", sqlite3_column_text(pStmt,0));
  }
  sqlite3_finalize(pStmt);

end_diff_one_table:
  strFree(pSql);
  sqlite3_free(zId);
  namelistFree(az);
  namelistFree(az2);
  return;
}

#if 0
/*
** Check that table zTab exists and has the same schema in both the "aaa"
** and "bbb" databases currently opened by the global db handle. If they
** do not, output an error message on stderr and exit(1). Otherwise, if
** the schemas do match, return control to the caller.
*/
static void checkSchemasMatch(SqlDiffCtx *p, const char *zTab){
  sqlite3_stmt *pStmt = sqldiff_prepare(p,
      "SELECT A.sql=B.sql FROM aaa.sqlite_schema A, bbb.sqlite_schema B"
      " WHERE A.name=%Q AND B.name=%Q", zTab, zTab
  );
  if( SQLITE_ROW==sqlite3_step(pStmt) ){
    if( sqlite3_column_int(pStmt,0)==0 ){
      sqldiffError(p, "schema changes for table %s", safeId(zTab));
    }
  }else{
    sqldiffError(p, "table %s missing from one or both databases",safeId(zTab));
  }
  sqlite3_finalize(pStmt);
}
#endif

/*
** Return true if the ascii character passed as the only argument is a
** whitespace character. Otherwise return false.
*/
static int is_whitespace(char x){
  return (x==' ' || x=='\t' || x=='\n' || x=='\r');
}

/*
** Extract the next SQL keyword or quoted string from buffer zIn and copy it
** (or a prefix of it if it will not fit) into buffer zBuf, size nBuf bytes.
** Return a pointer to the character within zIn immediately following 
** the token or quoted string just extracted.
*/
static const char *gobble_token(const char *zIn, char *zBuf, int nBuf){
  const char *p = zIn;
  char *pOut = zBuf;
  char *pEnd = &pOut[nBuf-1];
  char q = 0;                     /* quote character, if any */

  if( p==0 ) return 0;
  while( is_whitespace(*p) ) p++;
  switch( *p ){
    case '"': q = '"'; break;
    case '\'': q = '\''; break;
    case '`': q = '`'; break;
    case '[': q = ']'; break;
  }

  if( q ){
    p++;
    while( *p && pOut<pEnd ){
      if( *p==q ){
        p++;
        if( *p!=q ) break;
      }
      if( pOut<pEnd ) *pOut++ = *p;
      p++;
    }
  }else{
    while( *p && !is_whitespace(*p) && *p!='(' ){
      if( pOut<pEnd ) *pOut++ = *p;
      p++;
    }
  }

  *pOut = '\0';
  return p;
}

/*
** This function is the implementation of SQL scalar function "module_name":
**
**   module_name(SQL)
**
** The only argument should be an SQL statement of the type that may appear
** in the sqlite_schema table. If the statement is a "CREATE VIRTUAL TABLE"
** statement, then the value returned is the name of the module that it
** uses. Otherwise, if the statement is not a CVT, NULL is returned.
*/
static void module_name_func(
  sqlite3_context *pCtx, 
  int nVal, sqlite3_value **apVal
){
  const char *zSql;
  char zToken[32];

  assert( nVal==1 );
  zSql = (const char*)sqlite3_value_text(apVal[0]);

  zSql = gobble_token(zSql, zToken, sizeof(zToken));
  if( zSql==0 || sqlite3_stricmp(zToken, "create") ) return;
  zSql = gobble_token(zSql, zToken, sizeof(zToken));
  if( zSql==0 || sqlite3_stricmp(zToken, "virtual") ) return;
  zSql = gobble_token(zSql, zToken, sizeof(zToken));
  if( zSql==0 || sqlite3_stricmp(zToken, "table") ) return;
  zSql = gobble_token(zSql, zToken, sizeof(zToken));
  if( zSql==0 ) return;
  zSql = gobble_token(zSql, zToken, sizeof(zToken));
  if( zSql==0 || sqlite3_stricmp(zToken, "using") ) return;
  zSql = gobble_token(zSql, zToken, sizeof(zToken));
  
  sqlite3_result_text(pCtx, zToken, -1, SQLITE_TRANSIENT);
}

/*
** Return the text of an SQL statement that itself returns the list of
** tables to process within the database.
*/
const char *all_tables_sql(SqlDiffCtx *p){
  if( p->bHandleVtab ){
    int rc;
  
    rc = sqlite3_exec(p->db, 
        "CREATE TEMP TABLE tblmap(module COLLATE nocase, postfix);"
        "INSERT INTO temp.tblmap VALUES"
        "('fts3', '_content'), ('fts3', '_segments'), ('fts3', '_segdir'),"
  
        "('fts4', '_content'), ('fts4', '_segments'), ('fts4', '_segdir'),"
        "('fts4', '_docsize'), ('fts4', '_stat'),"
  
        "('fts5', '_data'), ('fts5', '_idx'), ('fts5', '_content'),"
        "('fts5', '_docsize'), ('fts5', '_config'),"
  
        "('rtree', '_node'), ('rtree', '_rowid'), ('rtree', '_parent');"
        , 0, 0, 0
    );
    assert( rc==SQLITE_OK );
  
    rc = sqlite3_create_function(
        p->db, "module_name", 1, SQLITE_UTF8, 0, module_name_func, 0, 0
    );
    assert( rc==SQLITE_OK );
  
    return 
      "SELECT name FROM aaa.sqlite_schema\n"
      " WHERE type='table' AND (\n"
      "    module_name(sql) IS NULL OR \n"
      "    module_name(sql) IN (SELECT module FROM temp.tblmap)\n"
      " ) AND name NOT IN (\n"
      "  SELECT a.name || b.postfix \n"
        "FROM aaa.sqlite_schema AS a, temp.tblmap AS b \n"
        "WHERE module_name(a.sql) = b.module\n" 
      " )\n"
      "UNION \n"
      "SELECT name FROM bbb.sqlite_schema\n"
      " WHERE type='table' AND (\n"
      "    module_name(sql) IS NULL OR \n"
      "    module_name(sql) IN (SELECT module FROM temp.tblmap)\n"
      " ) AND name NOT IN (\n"
      "  SELECT a.name || b.postfix \n"
        "FROM bbb.sqlite_schema AS a, temp.tblmap AS b \n"
        "WHERE module_name(a.sql) = b.module\n" 
      " )\n"
      " ORDER BY name";
  }else{
    return
      "SELECT name FROM aaa.sqlite_schema\n"
      " WHERE type='table' AND sql NOT LIKE 'CREATE VIRTUAL%%'\n"
      " UNION\n"
      "SELECT name FROM bbb.sqlite_schema\n"
      " WHERE type='table' AND sql NOT LIKE 'CREATE VIRTUAL%%'\n"
      " ORDER BY name";
  }
}

/*
** Check to see if the two input blobs, pA and pB, are both
** SQLite database files.  If they are, then output an SQL diff
** into pOut and return true.  If either of the inputs is not
** a well-formed SQLite database, then return 0.
**
** A semantic-level diff is computed.  In other words, it is the
** content of the database that matters.  If the databases have
** different page sizes or text representations or if the pages
** are in a different order, that does not affect the output.
** Only content differences are shown.
*/
int sqldiff(
  Blob *pA,        /* FROM file */
  Blob *pB,        /* TO file */
  Blob *pOut,      /* Write diff here */
  DiffConfig *pCfg /* Configuration options */
){
  SqlDiffCtx s;
  sqlite3_stmt *pStmt;
  int rc;
  u8 *aA, *aB;
  int szA, szB;
  u8 aModeA[2];
  u8 aModeB[2];

  if( pOut==0 ) return 0;
  if( !looks_like_sqlite_db(pA) ) return 0;
  if( !looks_like_sqlite_db(pB) ) return 0;
  memset(&s, 0, sizeof(s));
  s.out = pOut;
  rc = sqlite3_open(":memory:", &s.db);
  if( rc ){
    fossil_fatal("Unable to open an auxiliary in-memory database\n");
  }
  rc = sqlite3_exec(s.db, "ATTACH ':memory:' AS aaa;", 0, 0, 0);
  if( rc ){
    fossil_fatal("Unable to attach an in-memory database\n");
  }
  rc = sqlite3_exec(s.db, "ATTACH ':memory:' AS bbb;", 0, 0, 0);
  if( rc ){
    fossil_fatal("Unable to attach an in-memory database\n");
  }
  aA = (u8*)blob_buffer(pA);
  szA = blob_size(pA);
  memcpy(aModeA, &aA[18], 2);
  aA[18] = aA[19] = 1;
  aB = (u8*)blob_buffer(pB);
  szB = blob_size(pB);
  memcpy(aModeB, &aB[18], 2);
  aB[18] = aB[19] = 1;
  rc = sqlite3_deserialize(s.db, "aaa", aA, szA, szA,
                           SQLITE_DESERIALIZE_READONLY);
  if( rc ){
    s.nErr++;
    goto not_a_valid_diff;
  }
  rc = sqlite3_deserialize(s.db, "bbb", aB, szB, szB,
                           SQLITE_DESERIALIZE_READONLY);
  if( rc ){
    s.nErr++;
    goto not_a_valid_diff;
  }
  if( pCfg->diffFlags & DIFF_HTML ) blob_appendf(pOut, "<pre>\n");
  pStmt = sqldiff_prepare(&s, "%s", all_tables_sql(&s) );
  if( pStmt ){
    while( SQLITE_ROW==sqlite3_step(pStmt) ){
      diff_one_table(&s, (const char*)sqlite3_column_text(pStmt,0));
    }
    sqlite3_finalize(pStmt);
  }
  if( pCfg->diffFlags & DIFF_HTML ) blob_appendf(pOut, "</pre>\n");

not_a_valid_diff:
  sqlite3_close(s.db);
  if( s.nErr ) blob_reset(pOut);
  memcpy(&aA[18], aModeA, 2);
  memcpy(&aB[18], aModeB, 2);
  return s.nErr==0;  
}
