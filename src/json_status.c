#ifdef FOSSIL_ENABLE_JSON
/*
** Copyright (c) 2013 D. Richard Hipp
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
#include "json_status.h"

#if INTERFACE
#include "json_detail.h"
#endif

/*
Reminder to check if a column exists:

PRAGMA table_info(table_name)

and search for a row where the 'name' field matches.

That assumes, of course, that table_info()'s output format
is stable.
*/

/*
** Implementation of the /json/status page.
**
*/
cson_value * json_page_status(){
  Stmt q = empty_Stmt;
  cson_object * oPay;
  /*cson_object * files;*/
  int vid, nErr = 0;
  cson_object * tmpO;
  char * zTmp;
  i64 iMtime;
  cson_array * aFiles;

  if(!db_open_local(0)){
    json_set_err(FSL_JSON_E_DB_NEEDS_CHECKOUT, NULL);
    return NULL;
  }
  oPay = cson_new_object();
  cson_object_set(oPay, "repository",
                  json_new_string(db_repository_filename()));
  cson_object_set(oPay, "localRoot",
                  json_new_string(g.zLocalRoot));
  vid = db_lget_int("checkout", 0);
  if(!vid){
      json_set_err( FSL_JSON_E_UNKNOWN, "Can this even happen?" );
      return 0;
  }
  /* TODO: dupe show_common_info() state */
  tmpO = cson_new_object();
  cson_object_set(oPay, "checkout", cson_object_value(tmpO));

  zTmp = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
  cson_object_set(tmpO, "uuid", json_new_string(zTmp) );
  free(zTmp);

  cson_object_set( tmpO, "tags", json_tags_for_checkin_rid(vid, 0) );

  /* FIXME: optimize the datetime/timestamp queries into 1 query. */
  zTmp = db_text(0, "SELECT datetime(mtime) || "
                 "' UTC' FROM event WHERE objid=%d",
                 vid);
  cson_object_set(tmpO, "datetime", json_new_string(zTmp));
  free(zTmp);
  iMtime = db_int64(0, "SELECT CAST(strftime('%%s',mtime) AS INTEGER) "
                    "FROM event WHERE objid=%d", vid);
  cson_object_set(tmpO, "timestamp",
                  cson_value_new_integer((cson_int_t)iMtime));
#if 0
    /* TODO: add parent artifact info */
  tmpO = cson_new_object();
  cson_object_set( oPay, "parent", cson_object_value(tmpO) );
  cson_object_set( tmpO, "uuid", TODO );
  cson_object_set( tmpO, "timestamp", TODO );
#endif

  /* Now get the list of non-pristine files... */
  aFiles = cson_new_array();
  cson_object_set( oPay, "files", cson_array_value( aFiles ) );

  db_prepare(&q,
    "SELECT pathname, deleted, chnged, rid, coalesce(origname!=pathname,0)"
    "  FROM vfile "
    " WHERE is_selected(id)"
    "   AND (chnged OR deleted OR rid=0 OR pathname!=origname) ORDER BY 1"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPathname = db_column_text(&q,0);
    int isDeleted = db_column_int(&q, 1);
    int isChnged = db_column_int(&q,2);
    int isNew = db_column_int(&q,3)==0;
    int isRenamed = db_column_int(&q,4);
    cson_object * oFile;
    char const * zStatus = "???";
    char * zFullName = mprintf("%s%s", g.zLocalRoot, zPathname);
    if( isDeleted ){
      zStatus = "deleted";
    }else if( isNew ){
      zStatus = "new" /* maintenance reminder: MUST come
                         BEFORE the isChnged checks. */;
    }else if( isRenamed ){
      zStatus = "renamed";
    }else if( !file_wd_isfile_or_link(zFullName) ){
      if( file_access(zFullName, F_OK)==0 ){
        zStatus = "notAFile";
        ++nErr;
      }else{
        zStatus = "missing";
        ++nErr;
      }
    }else if( 2==isChnged ){
      zStatus = "updatedByMerge";
    }else if( 3==isChnged ){
      zStatus = "addedByMerge";
    }else if( 4==isChnged ){
      zStatus = "updatedByIntegrate";
    }else if( 5==isChnged ){
      zStatus = "addedByIntegrate";
    }else if( 1==isChnged ){
      if( file_contains_merge_marker(zFullName) ){
        zStatus = "conflict";
      }else{
        zStatus = "edited";
      }
    }

    oFile = cson_new_object();
    cson_array_append( aFiles, cson_object_value(oFile) );
    /* optimization potential: move these keys into cson_strings
       to take advantage of refcounting. */
    cson_object_set( oFile, "name", json_new_string( zPathname ) );
    cson_object_set( oFile, "status", json_new_string( zStatus ) );

    free(zFullName);
  }
  cson_object_set( oPay, "errorCount", json_new_int( nErr ) );
  db_finalize(&q);

#if 0
  /* TODO: add "merged with" status.  First need (A) to decide on a
     structure and (B) to set up some tests for the multi-merge
     case.*/
  db_prepare(&q, "SELECT uuid, id FROM vmerge JOIN blob ON merge=rid"
                 " WHERE id<=0");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zLabel = "MERGED_WITH";
    switch( db_column_int(&q, 1) ){
      case -1:  zLabel = "CHERRYPICK ";  break;
      case -2:  zLabel = "BACKOUT    ";  break;
      case -4:  zLabel = "INTEGRATE  ";  break;
    }
    blob_append(report, zPrefix, nPrefix);
    blob_appendf(report, "%s %s\n", zLabel, db_column_text(&q, 0));
  }
  db_finalize(&q);
  if( nErr ){
    fossil_fatal("aborting due to prior errors");
  }
#endif
  return cson_object_value( oPay );
}

#endif /* FOSSIL_ENABLE_JSON */
