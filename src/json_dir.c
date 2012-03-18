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
#include "json_dir.h"

#if INTERFACE
#include "json_detail.h"
#endif

static cson_value * json_page_dir_list();
/*
** Mapping of /json/wiki/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Dir[] = {
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

#if 0 /* TODO: Not used? */
static char const * json_dir_path_extra(){
  static char const * zP = NULL;
  if( !zP ){
    zP = g.zExtra;
    while(zP && *zP && ('/'==*zP)){
      ++zP;
    }
  }
  return zP;
}
#endif

/*
** Impl of /json/dir. 98% of it was taken directly
** from browse.c::page_dir()
*/
static cson_value * json_page_dir_list(){
  cson_object * zPayload = NULL;
  cson_array * zEntries = NULL;
  cson_object * zEntry = NULL;
  cson_string * zKeyName = NULL;
  cson_string * zKeySize = NULL;
  cson_string * zKeyIsDir = NULL;
  cson_string * zKeyUuid = NULL;
  char * zD = NULL;
  char const * zDX = NULL;
  int nD;
  char * zUuid = NULL;
  char const * zCI = NULL;
  Manifest * pM = NULL;
  Stmt q = empty_Stmt;
  int rid = 0;
  if( !g.perm.History ){
    json_set_err(FSL_JSON_E_DENIED, "Requires 'h' permissions.");
    return NULL;
  }
  zCI = json_find_option_cstr("checkin",NULL,"ci" );

  /* If a specific check-in is requested, fetch and parse it.  If the
  ** specific check-in does not exist, clear zCI.  zCI==0 will cause all
  ** files from all check-ins to be displayed.
  */
  if( zCI && *zCI ){
    pM = manifest_get_by_name(zCI, &rid);
    if( pM ){
      zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    }else{
      json_set_err(FSL_JSON_E_UNRESOLVED_UUID,
                   "Checkin name [%s] is unresolved.",
                   zCI);
      return NULL;
    }
  }
  zDX = json_find_option_cstr("name",NULL,NULL);
  if(!zDX && !g.isHTTP){
    zDX = json_command_arg(g.json.dispatchDepth+1);
  }
  if(zDX && (!*zDX || (0==strcmp(zDX,"/")))){
    zDX = NULL;
  }
  zD = zDX ? fossil_strdup(zDX) : NULL;
  nD = zD ? strlen(zD)+1 : 0;
  while( nD>1 && zD[nD-2]=='/' ){ zD[(--nD)-1] = 0; }

  sqlite3_create_function(g.db, "pathelement", 2, SQLITE_UTF8, 0,
                          pathelementFunc, 0, 0);

  /* Compute the temporary table "localfiles" containing the names
  ** of all files and subdirectories in the zD[] directory.  
  **
  ** Subdirectory names begin with "/".  This causes them to sort
  ** first and it also gives us an easy way to distinguish files
  ** from directories in the loop that follows.
  */
  db_multi_exec(
     "CREATE TEMP TABLE localfiles(n UNIQUE NOT NULL %s, u, sz, mtime DEFAULT NULL);",
     filename_collation()
  );

  if( zCI ){
    Stmt ins;
    ManifestFile *pFile;
    ManifestFile *pPrev = 0;
    int nPrev = 0;
    int c;

    db_prepare(&ins,
       "INSERT OR IGNORE INTO localfiles VALUES("
               /*0*/ "pathelement(:x,0),"
               /*1*/ ":u, "
               /*2*/ "(SELECT size from blob where uuid=:s), "
               /*3: TODO: mtime*/" NULL"
               ")"
    );
    manifest_file_rewind(pM);
    while( (pFile = manifest_file_next(pM,0))!=0 ){
      if( nD>0 
        && ((pFile->zName[nD-1]!='/') || (0!=memcmp(pFile->zName, zD, nD-1)))
      ){
        continue;
      }
      /*printf("zD=%s, nD=%d, pFile->zName=%s\n", zD, nD, pFile->zName);*/
      if( pPrev
       && memcmp(&pFile->zName[nD],&pPrev->zName[nD],nPrev)==0
       && (pFile->zName[nD+nPrev]==0 || pFile->zName[nD+nPrev]=='/')
      ){
        continue;
      }
      db_bind_text(&ins, ":x", &pFile->zName[nD]);
      db_bind_text(&ins, ":u", pFile->zUuid);
      db_bind_text(&ins, ":s", pFile->zUuid);
      /*db_bind_text(&ins, ":u3", pFile->zUuid);*/
      db_step(&ins);
      db_reset(&ins);
      pPrev = pFile;
      for(nPrev=0; (c=pPrev->zName[nD+nPrev]) && c!='/'; nPrev++){}
      if( c=='/' ) nPrev++;
    }
    db_finalize(&ins);
  }else if( zD && *zD ){
    if( filenames_are_case_sensitive() ){
      db_multi_exec(
        "INSERT OR IGNORE INTO localfiles"
        " SELECT pathelement(name,%d), NULL, NULL, NULL FROM filename"
        "  WHERE name GLOB '%q/*'",
        nD, zD
      );
    }else{
      db_multi_exec(
        "INSERT OR IGNORE INTO localfiles"
        " SELECT pathelement(name,%d), NULL, NULL, NULL FROM filename"
        "  WHERE name LIKE '%q/%%'",
        nD, zD
      );
    }
  }else{
    db_multi_exec(
      "INSERT OR IGNORE INTO localfiles"
      " SELECT pathelement(name,0), NULL, NULL, NULL FROM filename"
    );
  }

  if(zCI){
    db_prepare( &q, "SELECT n as name, u as uuid, sz as size FROM localfiles ORDER BY n");
  }else{/* UUIDs are all NULL. */
    db_prepare( &q, "SELECT n as name FROM localfiles ORDER BY n");
  }

  zKeyName = cson_new_string("name",4);
  cson_value_add_reference( cson_string_value(zKeyName) );
  zKeyUuid = cson_new_string("uuid",4);
  cson_value_add_reference( cson_string_value(zKeyUuid) );
  zKeyIsDir = cson_new_string("isDir",5);
  cson_value_add_reference( cson_string_value(zKeyIsDir) );
  if( zCI ){
    zKeySize = cson_new_string("size",4);
    cson_value_add_reference( cson_string_value(zKeySize) );
  }

  zPayload = cson_new_object();
  cson_object_set_s( zPayload, zKeyName,
                     json_new_string((zD&&*zD) ? zD : "/") );
#if 0
  if(zUuid){
    cson_object_set_s( zPayload, zKeyUuid,
                       cson_string_value(cson_new_string(zUuid, strlen(zUuid))) );
  }
#endif
  if( zUuid ){
    cson_object_set( zPayload, "checkin", json_new_string(zUuid) );
  }

  while( (SQLITE_ROW==db_step(&q)) ){
    cson_value * name = NULL;
    char const * n = db_column_text(&q,0);
    char const isDir = ('/'==*n);
    zEntry = cson_new_object();
    if(!zEntries){
      zEntries = cson_new_array();
      cson_object_set( zPayload, "entries", cson_array_value(zEntries) );
    }
    cson_array_append(zEntries, cson_object_value(zEntry) );
    if(isDir){
      name = json_new_string( n+1 );
      cson_object_set_s(zEntry, zKeyIsDir, cson_value_true() );
    } else{
      name = json_new_string( n );
    }
    cson_object_set_s(zEntry, zKeyName, name );
    if( zCI && !isDir){
      /* don't add the uuid/size for dir entries - that data refers
         to one of the files in that directory :/. */
      char const * u = db_column_text(&q,1);
      sqlite_int64 const sz = db_column_int64(&q,2);
      /*sqlite_int64 const ts = db_column_int64(&q,3);*/
      if(u && *u){
        cson_object_set_s(zEntry, zKeyUuid, json_new_string( u ) );
      }
      cson_object_set_s(zEntry, zKeySize,
                        cson_value_new_integer( (cson_int_t)sz ));
      /*cson_object_set(zEntry, "mtime",
          cson_value_new_integer( (cson_int_t)ts ));*/
    }
  }
  db_finalize(&q);
  if(pM){
    manifest_destroy(pM);
  }
  cson_value_free( cson_string_value( zKeyName ) );
  cson_value_free( cson_string_value( zKeyUuid ) );
  cson_value_free( cson_string_value( zKeyIsDir ) );
  cson_value_free( cson_string_value( zKeySize ) );
  
  free( zUuid );
  free( zD );
  return cson_object_value(zPayload);
}

/*
** Implements the /json/dir family of pages/commands.
**
*/
cson_value * json_page_dir(){
#if 1
  return json_page_dir_list();
#else
  return json_page_dispatch_helper(&JsonPageDefs_Dir[0]);
#endif
}

#endif /* FOSSIL_ENABLE_JSON */
