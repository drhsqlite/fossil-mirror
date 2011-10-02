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
#include "VERSION.h"
#include "config.h"
#include "json_branch.h"

#if INTERFACE
#include "json_detail.h"
#endif


static cson_value * json_branch_list();
/*
** Mapping of /json/branch/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Branch[] = {
{"list", json_branch_list, 0},
{"create", json_page_nyi, 1},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** Implements the /json/branch family of pages/commands. Far from
** complete.
**
*/
cson_value * json_page_branch(){
  return json_page_dispatch_helper(&JsonPageDefs_Branch[0]);
}

/*
** Impl for /json/branch/list
**
**
** CLI mode options:
**
**  --range X | -r X, where X is one of (open,closed,all)
**    (only the first letter is significant, default=open).
**  -a (same as --range a)
**  -c (same as --range c)
**
** HTTP mode options:
**
** "range" GET/POST.payload parameter. FIXME: currently we also use
** POST, but really want to restrict this to POST.payload.
*/
static cson_value * json_branch_list(){
  cson_value * payV;
  cson_object * pay;
  cson_value * listV;
  cson_array * list;
  char const * range = NULL;
  int which = 0;
  char * sawConversionError = NULL;
  Stmt q;
  if( !g.perm.Read ){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  if(!g.isHTTP){
    range = find_option("range","r",1);
    if(!range||!*range){
      range = find_option("all","a",0);
      if(range && *range){
        range = "a";
      }else{
        range = find_option("closed","c",0);
        if(range&&*range){
          range = "c";
        }
      }
    }
  }else{
    range = json_getenv_cstr("range");
  }
  if(!range || !*range){
    range = "o";
  }
  assert( (NULL != range) && *range );
  switch(*range){
    case 'c':
      range = "closed";
      which = -1;
      break;
    case 'a':
      range = "all";
      which = 1;
      break;
    default:
      range = "open";
      which = 0;
      break;
  };
  cson_object_set(pay,"range",cson_value_new_string(range,strlen(range)));

  if( g.localOpen ){ /* add "current" property (branch name). */
    int vid = db_lget_int("checkout", 0);
    char const * zCurrent = vid
      ? db_text(0, "SELECT value FROM tagxref"
                " WHERE rid=%d AND tagid=%d",
                vid, TAG_BRANCH)
      : 0;
    if(zCurrent){
      cson_object_set(pay,"current",json_new_string(zCurrent));
    }
  }

  
  branch_prepare_list_query(&q, which);
  cson_object_set(pay,"branches",listV);
  while((SQLITE_ROW==db_step(&q))){
    cson_value * v = cson_sqlite3_column_to_value(q.pStmt,0);
    if(v){
      cson_array_append(list,v);
    }else if(!sawConversionError){
      sawConversionError = mprintf("Column-to-json failed @ %s:%d",
                                   __FILE__,__LINE__);
    }
  }
  if( sawConversionError ){
    json_warn(FSL_JSON_W_COL_TO_JSON_FAILED,sawConversionError);
    free(sawConversionError);
}
  return payV;
}
