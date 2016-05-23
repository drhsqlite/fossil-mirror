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
#include "VERSION.h"
#include "config.h"
#include "json_config.h"

#if INTERFACE
#include "json_detail.h"
#endif

static cson_value * json_config_get();
static cson_value * json_config_save();

/*
** Mapping of /json/config/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Config[] = {
{"get", json_config_get, 0},
{"save", json_config_save, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};


/*
** Implements the /json/config family of pages/commands.
**
*/
cson_value * json_page_config(){
  return json_page_dispatch_helper(&JsonPageDefs_Config[0]);
}


/*
** JSON-internal mapping of config options to config groups.  This is
** mostly a copy of the config options in configure.c, but that data
** is private and cannot be re-used directly here.
*/
static const struct JsonConfigProperty {
  char const * name;
  int groupMask;
} JsonConfigProperties[] = {
{ "css",                    CONFIGSET_CSS },
{ "header",                 CONFIGSET_SKIN },
{ "footer",                 CONFIGSET_SKIN },
{ "details",                CONFIGSET_SKIN },
{ "logo-mimetype",          CONFIGSET_SKIN },
{ "logo-image",             CONFIGSET_SKIN },
{ "background-mimetype",    CONFIGSET_SKIN },
{ "background-image",       CONFIGSET_SKIN },
{ "timeline-block-markup",  CONFIGSET_SKIN },
{ "timeline-max-comment",   CONFIGSET_SKIN },
{ "timeline-plaintext",     CONFIGSET_SKIN },
{ "adunit",                 CONFIGSET_SKIN },
{ "adunit-omit-if-admin",   CONFIGSET_SKIN },
{ "adunit-omit-if-user",    CONFIGSET_SKIN },

{ "project-name",           CONFIGSET_PROJ },
{ "short-project-name",     CONFIGSET_PROJ },
{ "project-description",    CONFIGSET_PROJ },
{ "index-page",             CONFIGSET_PROJ },
{ "manifest",               CONFIGSET_PROJ },
{ "binary-glob",            CONFIGSET_PROJ },
{ "clean-glob",             CONFIGSET_PROJ },
{ "ignore-glob",            CONFIGSET_PROJ },
{ "keep-glob",              CONFIGSET_PROJ },
{ "crlf-glob",              CONFIGSET_PROJ },
{ "crnl-glob",              CONFIGSET_PROJ },
{ "encoding-glob",          CONFIGSET_PROJ },
{ "empty-dirs",             CONFIGSET_PROJ },
{ "allow-symlinks",         CONFIGSET_PROJ },
{ "dotfiles",               CONFIGSET_PROJ },

{ "ticket-table",           CONFIGSET_TKT  },
{ "ticket-common",          CONFIGSET_TKT  },
{ "ticket-change",          CONFIGSET_TKT  },
{ "ticket-newpage",         CONFIGSET_TKT  },
{ "ticket-viewpage",        CONFIGSET_TKT  },
{ "ticket-editpage",        CONFIGSET_TKT  },
{ "ticket-reportlist",      CONFIGSET_TKT  },
{ "ticket-report-template", CONFIGSET_TKT  },
{ "ticket-key-template",    CONFIGSET_TKT  },
{ "ticket-title-expr",      CONFIGSET_TKT  },
{ "ticket-closed-expr",     CONFIGSET_TKT  },

{NULL, 0}
};


/*
** Impl of /json/config/get. Requires setup rights.
**
*/
static cson_value * json_config_get(){
  cson_object * pay = NULL;
  Stmt q = empty_Stmt;
  Blob sql = empty_blob;
  char const * zName = NULL;
  int confMask = 0;
  char optSkinBackups = 0;
  unsigned int i;
  if(!g.perm.Setup){
    json_set_err(FSL_JSON_E_DENIED, "Requires 's' permissions.");
    return NULL;
  }

  i = g.json.dispatchDepth + 1;
  zName = json_command_arg(i);
  for( ; zName; zName = json_command_arg(++i) ){
    if(0==(strcmp("all", zName))){
      confMask = CONFIGSET_ALL;
    }else if(0==(strcmp("project", zName))){
      confMask |= CONFIGSET_PROJ;
    }else if(0==(strcmp("skin", zName))){
      confMask |= (CONFIGSET_CSS|CONFIGSET_SKIN);
    }else if(0==(strcmp("ticket", zName))){
      confMask |= CONFIGSET_TKT;
    }else if(0==(strcmp("skin-backup", zName))){
      optSkinBackups = 1;
    }else{
      json_set_err( FSL_JSON_E_INVALID_ARGS,
                    "Unknown config area: %s", zName);
      return NULL;
    }
  }

  if(!confMask && !optSkinBackups){
    json_set_err(FSL_JSON_E_MISSING_ARGS, "No configuration area(s) selected.");
  }
  blob_append(&sql,
              "SELECT name, value"
              " FROM config "
              " WHERE 0 ", -1);
  {
    const struct JsonConfigProperty * prop = &JsonConfigProperties[0];
    blob_append(&sql," OR name IN (",-1);
    for( i = 0; prop->name; ++prop ){
      if(prop->groupMask & confMask){
        if( i++ ){
          blob_append(&sql,",",1);
        }
        blob_append_sql(&sql, "%Q", prop->name);
      }
    }
    blob_append(&sql,") ", -1);
  }


  if( optSkinBackups ){
    blob_append(&sql, " OR name GLOB 'skin:*'", -1);
  }
  blob_append(&sql," ORDER BY name", -1);
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  pay = cson_new_object();
  while( (SQLITE_ROW==db_step(&q)) ){
    cson_object_set(pay,
                    db_column_text(&q,0),
                    json_new_string(db_column_text(&q,1)));
  }
  db_finalize(&q);
  return cson_object_value(pay);
}

/*
** Impl of /json/config/save.
**
** TODOs:
*/
static cson_value * json_config_save(){
  json_set_err(FSL_JSON_E_NYI, NULL);
  return NULL;
}
#endif /* FOSSIL_ENABLE_JSON */
