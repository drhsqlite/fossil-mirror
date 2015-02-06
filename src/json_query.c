#ifdef FOSSIL_ENABLE_JSON
/*
** Copyright (c) 2011 D. Richard Hipp
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
*/

#include "config.h"
#include "json_query.h"

#if INTERFACE
#include "json_detail.h"
#endif


/*
** Implementation of the /json/query page.
**
** Requires admin privileges. Intended primarily to assist me in
** coming up with JSON output structures for pending features.
**
** Options/parameters:
**
** sql=string - a SELECT statement
**
** format=string 'a' means each row is an Array of values, 'o'
** (default) creates each row as an Object.
**
** TODO: in CLI mode (only) use -S FILENAME to read the sql
** from a file.
*/
cson_value * json_page_query(){
  char const * zSql = NULL;
  cson_value * payV;
  char const * zFmt;
  Stmt q = empty_Stmt;
  int check;
  if(!g.perm.Admin && !g.perm.Setup){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'a' or 's' privileges.");
    return NULL;
  }

  if( cson_value_is_string(g.json.reqPayload.v) ){
    zSql = cson_string_cstr(cson_value_get_string(g.json.reqPayload.v));
  }else{
    zSql = json_find_option_cstr2("sql",NULL,"s",2);
  }

  if(!zSql || !*zSql){
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "'sql' (-s) argument is missing.");
    return NULL;
  }

  zFmt = json_find_option_cstr2("format",NULL,"f",3);
  if(!zFmt) zFmt = "o";
  db_prepare(&q,"%s", zSql/*safe-for-%s*/);
  if( 0 == sqlite3_column_count( q.pStmt ) ){
      json_set_err(FSL_JSON_E_USAGE,
                   "Input query has no result columns. "
                   "Only SELECT-like queries are supported.");
      db_finalize(&q);
      return NULL;
  }
  switch(*zFmt){
    case 'a':
      check = cson_sqlite3_stmt_to_json(q.pStmt, &payV, 0);
      break;
    case 'o':
    default:
      check = cson_sqlite3_stmt_to_json(q.pStmt, &payV, 1);
  };
  db_finalize(&q);
  if(0 != check){
    json_set_err(FSL_JSON_E_UNKNOWN,
                 "Conversion to JSON failed with cson code #%d (%s).",
                 check, cson_rc_string(check));
    assert(NULL==payV);
  }
  return payV;

}

#endif /* FOSSIL_ENABLE_JSON */
