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

static cson_value * json_config_get(void);
static cson_value * json_config_save(void);

/*
** Mapping of /json/config/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Config[] = {
{"get", json_config_get, 0},
{"save", json_config_save, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

static cson_value * json_settings_get(void);
/*
** Mapping of /json/settings/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Settings[] = {
{"get", json_settings_get, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};


/*
** Implements the /json/config family of pages/commands.
**
*/
cson_value * json_page_config(void){
  return json_page_dispatch_helper(&JsonPageDefs_Config[0]);
}

/*
** Implements the /json/settings family of pages/commands.
**
*/
cson_value * json_page_settings(void){
  return json_page_dispatch_helper(&JsonPageDefs_Settings[0]);
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
{ "icon-mimetype",          CONFIGSET_SKIN },
{ "icon-image",             CONFIGSET_SKIN },
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
static cson_value * json_config_get(void){
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
static cson_value * json_config_save(void){
  json_set_err(FSL_JSON_E_NYI, NULL);
  return NULL;
}

/*
** Impl of /json/settings/get.
*/
static cson_value * json_settings_get(void){
  cson_array * pay = cson_new_array();
  int nSetting, i;
  const Setting *aSetting = setting_info(&nSetting);
  Stmt q = empty_Stmt;

  if( !g.perm.Read ){
    json_set_err( FSL_JSON_E_DENIED, "Fetching settings requires 'o' access." );
    return NULL;
  }
  if( g.localOpen ){
    db_prepare(&q,
       "SELECT 'checkout', value FROM vvar WHERE name=:name"
       " UNION ALL "
       "SELECT 'repo', value FROM config WHERE name=:name"
    );
  }else{
    db_prepare(&q,
      "SELECT 'repo', value FROM config WHERE name=:name"
    );
  }
  for(i=0; i<nSetting; ++i){
    const Setting *pSet = &aSetting[i];
    cson_object * jSet;
    cson_value * pVal = 0, * pSrc = 0;
    if( pSet->sensitive && !g.perm.Setup ){
      continue;
    }
    jSet = cson_new_object();
    cson_array_append(pay, cson_object_value(jSet));
    cson_object_set(jSet, "name", json_new_string(pSet->name));
    cson_object_set(jSet, "versionable", cson_value_new_bool(pSet->versionable));
    cson_object_set(jSet, "sensitive", cson_value_new_bool(pSet->sensitive));
    cson_object_set(jSet, "defaultValue", (pSet->def && pSet->def[0])
                    ? json_new_string(pSet->def)
                    : cson_value_null());
    if( pSet->versionable ){
      /* Check to see if this is overridden by a versionable settings file */
      if( g.localOpen ){
        /* Pull value from a local .fossil-settings/X file, if one exists. */
        Blob versionedPathname;
        blob_zero(&versionedPathname);
        blob_appendf(&versionedPathname, "%s.fossil-settings/%s",
                     g.zLocalRoot, pSet->name);
        if( file_size(blob_str(&versionedPathname), ExtFILE)>=0 ){
          Blob content;
          blob_zero(&content);
          blob_read_from_file(&content, blob_str(&versionedPathname), ExtFILE);
          pSrc = json_new_string("versioned");
          pVal = json_new_string(blob_str(&content));
          blob_reset(&content);
        }
        blob_reset(&versionedPathname);
      }
      if( 0==pSrc ){
        /*
        ** TODO? No checkout-local versioned setting found. We could
        ** fish the file out of the repository but we'd need to make
        ** an assumption about which branch to pull it from or require
        ** the user to pass in a version.
        */
      }
    }
    if( 0==pSrc ){
      /* We had no checkout-local value, so use the value from
      ** repo.config. */
      db_bind_text(&q, ":name", pSet->name);
      if( SQLITE_ROW==db_step(&q) ){
        pSrc = json_new_string(db_column_text(&q, 0));
        pVal = json_new_string(db_column_text(&q, 1));
      }
      db_reset(&q);
    }
    cson_object_set(jSet, "valueSource", pSrc ? pSrc : cson_value_null());
    cson_object_set(jSet, "value", pVal ? pVal : cson_value_null());
  }
  db_finalize(&q);
  return cson_array_value(pay);
}

#endif /* FOSSIL_ENABLE_JSON */
