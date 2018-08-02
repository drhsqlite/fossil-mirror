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
#include "json_report.h"

#if INTERFACE
#include "json_detail.h"
#endif


static cson_value * json_report_create();
static cson_value * json_report_get();
static cson_value * json_report_list();
static cson_value * json_report_run();
static cson_value * json_report_save();

/*
** Mapping of /json/report/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Report[] = {
{"create", json_report_create, 0},
{"get", json_report_get, 0},
{"list", json_report_list, 0},
{"run", json_report_run, 0},
{"save", json_report_save, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};
/*
** Implementation of the /json/report page.
**
**
*/
cson_value * json_page_report(){
  if(!g.perm.RdTkt && !g.perm.NewTkt ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'r' or 'n' permissions.");
    return NULL;
  }
  return json_page_dispatch_helper(JsonPageDefs_Report);
}

/*
** Searches the environment for a "report" parameter
** (CLI: -report/-r #).
**
** If one is not found and argPos is >0 then json_command_arg()
** is checked.
**
** Returns >0 (the report number) on success .
*/
static int json_report_get_number(int argPos){
  int nReport = json_find_option_int("report",NULL,"r",-1);
  if( (nReport<=0) && cson_value_is_integer(g.json.reqPayload.v)){
    nReport = cson_value_get_integer(g.json.reqPayload.v);
  }
  if( (nReport <= 0) && (argPos>0) ){
    char const * arg = json_command_arg(argPos);
    if(arg && fossil_isdigit(*arg)) {
      nReport = atoi(arg);
    }
  }
  return nReport;
}

static cson_value * json_report_create(){
  json_set_err(FSL_JSON_E_NYI, NULL);
  return NULL;
}

static cson_value * json_report_get(){
  int nReport;
  Stmt q = empty_Stmt;
  cson_value * pay = NULL;

  if(!g.perm.TktFmt){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 't' privileges.");
    return NULL;
  }
  nReport = json_report_get_number(3);
  if(nReport <=0){
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "Missing or invalid 'report' (-r) parameter.");
    return NULL;
  }

  db_prepare(&q,"SELECT rn AS report,"
             " owner AS owner,"
             " title AS title,"
             " cast(strftime('%%s',mtime) as int) as timestamp,"
             " cols as columns,"
             " sqlcode as sqlCode"
             " FROM reportfmt"
             " WHERE rn=%d",
             nReport);
  if( SQLITE_ROW != db_step(&q) ){
    db_finalize(&q);
    json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,
                 "Report #%d not found.", nReport);
    return NULL;
  }
  pay = cson_sqlite3_row_to_object(q.pStmt);
  db_finalize(&q);
  return pay;
}

/*
** Impl of /json/report/list.
*/
static cson_value * json_report_list(){
  Blob sql = empty_blob;
  cson_value * pay = NULL;
  if(!g.perm.RdTkt){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'r' privileges.");
    return NULL;
  }
  blob_append(&sql, "SELECT"
              " rn AS report,"
              " title as title,"
              " owner as owner"
              " FROM reportfmt"
              " WHERE 1"
              " ORDER BY title",
              -1);
  pay = json_sql_to_array_of_obj(&sql, NULL, 1);
  if(!pay){
    json_set_err(FSL_JSON_E_UNKNOWN,
                 "Quite unexpected: no ticket reports found.");
  }
  return pay;
}

/*
** Impl for /json/report/run
**
** Options/arguments:
**
** report=int (CLI: -report # or -r #) is the report number to run.
**
** limit=int (CLI: -limit # or -n #) -n is for compat. with other commands.
**
** format=a|o Specifies result format: a=each row is an arry, o=each
** row is an object.  Default=o.
*/
static cson_value * json_report_run(){
  int nReport;
  Stmt q = empty_Stmt;
  cson_object * pay = NULL;
  cson_array * tktList = NULL;
  char const * zFmt;
  char * zTitle = NULL;
  Blob sql = empty_blob;
  int limit = 0;
  cson_value * colNames = NULL;
  int i;

  if(!g.perm.RdTkt){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'r' privileges.");
    return NULL;
  }
  nReport = json_report_get_number(3);
  if(nReport <=0){
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "Missing or invalid 'number' (-n) parameter.");
    goto error;
  }
  zFmt = json_find_option_cstr2("format",NULL,"f",3);
  if(!zFmt) zFmt = "o";
  db_prepare(&q,
             "SELECT sqlcode, "
             " title"
             " FROM reportfmt"
             " WHERE rn=%d",
             nReport);
  if(SQLITE_ROW != db_step(&q)){
    json_set_err(FSL_JSON_E_INVALID_ARGS,
                 "Report number %d not found.",
                 nReport);
    db_finalize(&q);
    goto error;
  }

  limit = json_find_option_int("limit",NULL,"n",-1);


  /* Copy over report's SQL...*/
  blob_append(&sql, db_column_text(&q,0), -1);
  zTitle = mprintf("%s", db_column_text(&q,1));
  db_finalize(&q);
  db_prepare(&q, "%s", blob_sql_text(&sql));

  /** Build the response... */
  pay = cson_new_object();

  cson_object_set(pay, "report", json_new_int(nReport));
  cson_object_set(pay, "title", json_new_string(zTitle));
  if(limit>0){
    cson_object_set(pay, "limit", json_new_int((limit<0) ? 0 : limit));
  }
  free(zTitle);
  zTitle = NULL;

  if(g.perm.TktFmt){
    cson_object_set(pay, "sqlcode",
                    cson_value_new_string(blob_str(&sql),
                                          (unsigned int)blob_size(&sql)));
  }
  blob_reset(&sql);

  colNames = cson_sqlite3_column_names(q.pStmt);
  cson_object_set( pay, "columnNames", colNames);
  for( i = 0 ; ((limit>0) ?(i < limit) : 1)
         && (SQLITE_ROW == db_step(&q));
       ++i){
    cson_value * row = ('a'==*zFmt)
      ? cson_sqlite3_row_to_array(q.pStmt)
      : cson_sqlite3_row_to_object2(q.pStmt,
                                    cson_value_get_array(colNames));
    ;
    if(row && !tktList){
      tktList = cson_new_array();
    }
    cson_array_append(tktList, row);
  }
  db_finalize(&q);
  cson_object_set(pay, "tickets",
                  tktList ? cson_array_value(tktList) : cson_value_null());

  goto end;

  error:
  assert(0 != g.json.resultCode);
  cson_value_free( cson_object_value(pay) );
  pay = NULL;
  end:

  return pay ? cson_object_value(pay) : NULL;

}

static cson_value * json_report_save(){
  return NULL;
}
#endif /* FOSSIL_ENABLE_JSON */
