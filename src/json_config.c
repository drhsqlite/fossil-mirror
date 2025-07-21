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
static cson_value * json_settings_set(void);
/*
** Mapping of /json/settings/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Settings[] = {
{"get", json_settings_get, 0},
{"set", json_settings_set, 0},
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
{ "css",                    CONFIGSET_CSS  },
{ "header",                 CONFIGSET_SKIN },
{ "mainmenu",               CONFIGSET_SKIN },
{ "footer",                 CONFIGSET_SKIN },
{ "details",                CONFIGSET_SKIN },
{ "js",                     CONFIGSET_SKIN },
{ "default-skin",           CONFIGSET_SKIN },
{ "logo-mimetype",          CONFIGSET_SKIN },
{ "logo-image",             CONFIGSET_SKIN },
{ "background-mimetype",    CONFIGSET_SKIN },
{ "background-image",       CONFIGSET_SKIN },
{ "icon-mimetype",          CONFIGSET_SKIN },
{ "icon-image",             CONFIGSET_SKIN },
{ "timeline-date-format",   CONFIGSET_SKIN },
{ "timeline-default-style", CONFIGSET_SKIN },
{ "timeline-dwelltime",     CONFIGSET_SKIN },
{ "timeline-closetime",     CONFIGSET_SKIN },
{ "timeline-hard-newlines", CONFIGSET_SKIN },
{ "timeline-max-comment",   CONFIGSET_SKIN },
{ "timeline-plaintext",     CONFIGSET_SKIN },
{ "timeline-truncate-at-blank", CONFIGSET_SKIN },
{ "timeline-tslink-info",   CONFIGSET_SKIN },
{ "timeline-utc",           CONFIGSET_SKIN },
{ "adunit",                 CONFIGSET_SKIN },
{ "adunit-omit-if-admin",   CONFIGSET_SKIN },
{ "adunit-omit-if-user",    CONFIGSET_SKIN },
{ "default-csp",            CONFIGSET_SKIN },
{ "sitemap-extra",          CONFIGSET_SKIN },
{ "safe-html",              CONFIGSET_SKIN },

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
{ "parent-project-code",    CONFIGSET_PROJ },
{ "parent-project-name",    CONFIGSET_PROJ },
{ "hash-policy",            CONFIGSET_PROJ },
{ "comment-format",         CONFIGSET_PROJ },
{ "mimetypes",              CONFIGSET_PROJ },
{ "forbid-delta-manifests", CONFIGSET_PROJ },
{ "mv-rm-files",            CONFIGSET_PROJ },

{ "user-color-map",         CONFIGSET_USER },

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
  cson_object * pay = cson_new_object();   /* output payload */
  int nSetting, i;                       /* setting count and loop var */
  const Setting *aSetting = setting_info(&nSetting);
  const char * zRevision = 0;            /* revision to look for
                                            versioned settings in */
  char * zUuid = 0;                      /* Resolved UUID of zRevision */
  Stmt q = empty_Stmt;                   /* Config-search query */
  Stmt qFoci = empty_Stmt;               /* foci query */

  if( !g.perm.Read ){
    json_set_err( FSL_JSON_E_DENIED, "Fetching settings requires 'o' access." );
    return NULL;
  }
  zRevision = json_find_option_cstr("version",NULL,NULL);
  if( 0!=zRevision ){
    int rid = name_to_uuid2(zRevision, "ci", &zUuid);
    if(rid<=0){
      json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,
                   "Cannot find the given version.");
      return NULL;
    }
    db_multi_exec("CREATE VIRTUAL TABLE IF NOT EXISTS "
                  "temp.foci USING files_of_checkin;");
    db_prepare(&qFoci,
               "SELECT uuid FROM temp.foci WHERE "
               "checkinID=%d AND filename='.fossil-settings/' || :name",
               rid);
  }
  zRevision = 0;

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
    jSet = cson_new_object();
    cson_object_set(pay, pSet->name, cson_object_value(jSet));
    cson_object_set(jSet, "versionable",cson_value_new_bool(pSet->versionable));
    cson_object_set(jSet, "sensitive", cson_value_new_bool(pSet->sensitive));
    cson_object_set(jSet, "defaultValue", (pSet->def && pSet->def[0])
                    ? json_new_string(pSet->def)
                    : cson_value_null());
    if( 0==pSet->sensitive || 0!=g.perm.Setup ){
      if( pSet->versionable ){
        /* Check to see if this is overridden by a versionable
        ** settings file */
        if( 0!=zUuid ){
          /* Attempt to find a versioned setting stored in the given
          ** check-in version. */
          db_bind_text(&qFoci, ":name", pSet->name);
          if( SQLITE_ROW==db_step(&qFoci) ){
            int frid = fast_uuid_to_rid(db_column_text(&qFoci, 0));
            Blob content;
            blob_zero(&content);
            if( 0!=content_get(frid, &content) ){
              pSrc = json_new_string("versioned");
              pVal = json_new_string(blob_str(&content));
            }
            blob_reset(&content);
          }
          db_reset(&qFoci);
        }
        if( 0==pSrc && g.localOpen ){
          /* Pull value from a checkout-local .fossil-settings/X file,
          ** if one exists. */
          Blob versionedPathname;
          blob_zero(&versionedPathname);
          blob_appendf(&versionedPathname, "%s.fossil-settings/%s",
                       g.zLocalRoot, pSet->name);
          if( file_size(blob_str(&versionedPathname), ExtFILE)>=0 ){
            Blob content;
            blob_zero(&content);
            blob_read_from_file(&content, blob_str(&versionedPathname),ExtFILE);
            pSrc = json_new_string("versioned");
            pVal = json_new_string(blob_str(&content));
            blob_reset(&content);
          }
          blob_reset(&versionedPathname);
        }
      }
      if( 0==pSrc ){
        /* Setting is not versionable or we had no versioned value, so
        ** use the value from localdb.vvar or repository.config (in
        ** that order). */
        db_bind_text(&q, ":name", pSet->name);
        if( SQLITE_ROW==db_step(&q) ){
          pSrc = json_new_string(db_column_text(&q, 0));
          pVal = json_new_string(db_column_text(&q, 1));
        }
        db_reset(&q);
      }
    }
    cson_object_set(jSet, "valueSource", pSrc ? pSrc : cson_value_null());
    cson_object_set(jSet, "value", pVal ? pVal : cson_value_null());
  }/*aSetting loop*/
  db_finalize(&q);
  db_finalize(&qFoci);
  fossil_free(zUuid);
  return cson_object_value(pay);
}

/*
** Impl of /json/settings/set.
**
** Input payload is an object mapping setting names to values. All
** values are set in the repository.config table. It has no response
** payload.
*/
static cson_value * json_settings_set(void){
  Stmt q = empty_Stmt;                   /* Config-set query */
  cson_object_iterator objIter = cson_object_iterator_empty;
  cson_kvp * pKvp;
  int nErr = 0, nProp = 0;

  if( 0==g.perm.Setup ){
    json_set_err( FSL_JSON_E_DENIED, "Setting settings requires 's' access." );
    return NULL;
  }
  else if( 0==g.json.reqPayload.o ){
    json_set_err(FSL_JSON_E_MISSING_ARGS,
                 "Missing payload of setting-to-value mappings.");
    return NULL;
  }

  db_unprotect(PROTECT_CONFIG);
  db_prepare(&q,
      "INSERT OR REPLACE INTO config (name, value, mtime) "
      "VALUES(:name, :value, CAST(strftime('%%s') AS INT))"
  );
  db_begin_transaction();
  cson_object_iter_init( g.json.reqPayload.o, &objIter );
  while( (pKvp = cson_object_iter_next(&objIter)) ){
    char const * zKey = cson_string_cstr( cson_kvp_key(pKvp) );
    cson_value * pVal;
    const Setting *pSetting = db_find_setting( zKey, 0 );
    if( 0==pSetting ){
      nErr = json_set_err(FSL_JSON_E_INVALID_ARGS,
                          "Unknown setting: %s", zKey);
      break;
    }
    pVal = cson_kvp_value(pKvp);
    switch( cson_value_type_id(pVal) ){
      case CSON_TYPE_NULL:
        db_multi_exec("DELETE FROM config WHERE name=%Q", pSetting->name);
        continue;
      case CSON_TYPE_BOOL:
        db_bind_int(&q, ":value", cson_value_get_bool(pVal) ? 1 : 0);
        break;
      case CSON_TYPE_INTEGER:
        db_bind_int64(&q, ":value", cson_value_get_integer(pVal));
        break;
      case CSON_TYPE_DOUBLE:
        db_bind_double(&q, ":value", cson_value_get_double(pVal));
        break;
      case CSON_TYPE_STRING:
        db_bind_text(&q, ":value", cson_value_get_cstr(pVal));
        break;
      default:
        nErr = json_set_err(FSL_JSON_E_USAGE,
                            "Invalid value type for setting '%s'.",
                            pSetting->name);
        break;
    }
    if( 0!=nErr ) break;
    db_bind_text(&q, ":name", zKey);
    db_step(&q);
    db_reset(&q);
    ++nProp;
  }
  db_finalize(&q);
  if( 0==nErr && 0==nProp ){
    nErr = json_set_err(FSL_JSON_E_INVALID_ARGS,
                        "Payload contains no settings to set.");
  }
  db_end_transaction(nErr);
  db_protect_pop();
  return NULL;
}

#endif /* FOSSIL_ENABLE_JSON */
