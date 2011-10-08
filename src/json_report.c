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
{"run", json_report_run, 1},
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
  return json_page_dispatch_helper(&JsonPageDefs_Report[0]);
}


static cson_value * json_report_create(){
  return NULL;
}

static cson_value * json_report_get(){
  return NULL;
}

static cson_value * json_report_list(){
  Blob sql = empty_blob;
  cson_value * pay = NULL;
  blob_append(&sql, "SELECT"
              " rn number,"
              " title as title,"
              " owner as owner"
              " FROM reportfmt", -1);
  if(!g.perm.TktFmt){
    blob_append(&sql,
                " AND title NOT LIKE '_%'", -1);
  }
  blob_append(&sql," ORDER BY title",-1);
  pay = json_sql_to_array_of_obj(&sql, 1);
  if(!pay){
    json_set_err(FSL_JSON_E_UNKNOWN,
                 "Quite unexpected: no ticket reports found.");
  }
  return pay;
}

static cson_value * json_report_run(){
  return NULL;
}

static cson_value * json_report_save(){
  return NULL;
}
