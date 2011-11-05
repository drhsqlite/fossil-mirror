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
#include "json_user.h"

#if INTERFACE
#include "json_detail.h"
#endif

static cson_value * json_user_get();
static cson_value * json_user_list();
static cson_value * json_user_save();
#if 0
static cson_value * json_user_create();

#endif

/*
** Mapping of /json/user/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_User[] = {
{"create", json_page_nyi, 0},
{"save", json_user_save, 0},
{"get", json_user_get, 0},
{"list", json_user_list, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};


/*
** Implements the /json/user family of pages/commands.
**
*/
cson_value * json_page_user(){
  return json_page_dispatch_helper(&JsonPageDefs_User[0]);
}


/*
** Impl of /json/user/list. Requires admin rights.
*/
static cson_value * json_user_list(){
  cson_value * payV = NULL;
  Stmt q;
  if(!g.perm.Admin){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }
  db_prepare(&q,"SELECT uid AS uid,"
             " login AS name,"
             " cap AS capabilities,"
             " info AS info,"
             " mtime AS mtime"
             " FROM user ORDER BY login");
  payV = json_stmt_to_array_of_obj(&q, NULL);
  db_finalize(&q);
  if(NULL == payV){
    json_set_err(FSL_JSON_E_UNKNOWN,
                 "Could not convert user list to JSON.");
  }
  return payV;  
}

/*
** Creates a new JSON Object based on the db state of
** the given user name. On error (no record found)
** it returns NULL, else the caller owns the returned
** object.
*/
static cson_value * json_load_user_by_name(char const * zName){
  cson_value * u = NULL;
  Stmt q;
  db_prepare(&q,"SELECT uid AS uid,"
             " login AS name,"
             " cap AS capabilities,"
             " info AS info,"
             " mtime AS mtime"
             " FROM user"
             " WHERE login=%Q",
             zName);
  if( (SQLITE_ROW == db_step(&q)) ){
    u = cson_sqlite3_row_to_object(q.pStmt);
  }
  db_finalize(&q);
  return u;  
}

/*
** Identical to _load_user_by_name(), but expects a user ID.  Returns
** NULL if no user found with that ID.
*/
static cson_value * json_load_user_by_id(int uid){
  cson_value * u = NULL;
  Stmt q;
  db_prepare(&q,"SELECT uid AS uid,"
             " login AS name,"
             " cap AS capabilities,"
             " info AS info,"
             " mtime AS mtime"
             " FROM user"
             " WHERE uid=%d",
             uid);
  if( (SQLITE_ROW == db_step(&q)) ){
    u = cson_sqlite3_row_to_object(q.pStmt);
  }
  db_finalize(&q);
  return u;  
}


/*
** Impl of /json/user/get. Requires admin rights.
*/
static cson_value * json_user_get(){
  cson_value * payV = NULL;
  char const * pUser = NULL;
  if(!g.perm.Admin){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'a' privileges.");
    return NULL;
  }
  pUser = json_command_arg(g.json.dispatchDepth+1);
  if( g.isHTTP && (!pUser || !*pUser) ){
    pUser = json_getenv_cstr("name")
      /* ACHTUNG: fossil apparently internally sets name=user/get/XYZ
         if we pass the name as part of the path, which is why we check
         with json_command_path() before trying to get("name").
      */;
  }
  if(!pUser || !*pUser){
    json_set_err(FSL_JSON_E_MISSING_ARGS,"Missing 'name' property.");
    return NULL;
  }
  payV = json_load_user_by_name(pUser);
  if(!payV){
    json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,"User not found.");
  }
  return payV;  
}

/*
** Expects pUser to contain fossil user fields in JSON form: name,
** uid, info, capabilities, password.
**
** At least one of (name, uid) must be included. All others are
** optional and their db fields will not be updated if those fields
** are not included in pUser.
**
** If uid is specified then name may refer to a _new_ name
** for a user, otherwise the name must refer to an existing user.
** If uid=-1 then the name must be specified and a new user is
** created (failes if one already exists).
**
** If uid is not set, this function might modify pUser to contain the
** db-found (or inserted) user ID.
**
** On error g.json's error state is set one of the FSL_JSON_E_xxx
** values from FossilJsonCodes is returned.
**
** On success the db record for the given user is updated.
**
** Requires either Admin, Setup, or Password access. Non-admin/setup
** users can only change their own information.
**
** TODOs:
**
** - Admin non-Setup users cannot change the information for Setup
** users.
**
*/
int json_user_update_from_json( cson_object * pUser ){
#define CSTR(X) cson_string_cstr(cson_value_get_string( cson_object_get(pUser, X ) ))
  char const * zName = CSTR("name");
  char const * zNameNew = zName;
  char * zNameFree = NULL;
  char const * zInfo = CSTR("info");
  char const * zCap = CSTR("capabilities");
  char const * zPW = CSTR("password");
  cson_value const * forceLogout = cson_object_get(pUser, "forceLogout");
  int gotFields = 0;
#undef CSTR
  cson_int_t uid = cson_value_get_integer( cson_object_get(pUser, "uid") );
  Blob sql = empty_blob;
  Stmt q = empty_Stmt;

  if(!g.perm.Admin && !g.perm.Setup && !g.perm.Password){
    return json_set_err( FSL_JSON_E_DENIED,
                         "Password change requires 'a', 's', "
                         "or 'p' permissions.");
  }
  
  if(uid<=0 && (!zName||!*zName)){
    return json_set_err(FSL_JSON_E_MISSING_ARGS,
                        "One of 'uid' or 'name' is required.");
  }else if(uid>0){
    zNameFree = db_text(NULL, "SELECT login FROM user WHERE uid=%d",uid);
    if(!zNameFree){
      return json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,
                          "No login found for uid %d.", uid);
    }
    zName = zNameFree;
  }else if(-1==uid){
    /* try to create a new user */
    if(!g.perm.Admin && !g.perm.Setup){
      return json_set_err(FSL_JSON_E_DENIED,
                          "Requires 'a' or 's' privileges.");
    } else if(!zName || !*zName){
      return json_set_err(FSL_JSON_E_MISSING_ARGS,
                          "No name specified for new user.");
    }else if( db_exists("SELECT 1 FROM user WHERE login=%Q", zName) ){
      return json_set_err(FSL_JSON_E_RESOURCE_ALREADY_EXISTS,
                          "User %s already exists.", zName);
    }else{
      Stmt ins = empty_Stmt;
      db_prepare(&ins, "INSERT INTO user (login) VALUES(%Q)",zName);
      db_step( &ins );
      db_finalize(&ins);
      uid = db_int(0,"SELECT uid FROM user WHERE login=%Q", zName);
      assert(uid>0);
      zNameNew = zName;
      cson_object_set( pUser, "uid", cson_value_new_integer(uid) );
    }
  }else{
    uid = db_int(0,"SELECT uid FROM user WHERE login=%Q", zName);
    if(uid<=0){
      return json_set_err(FSL_JSON_E_RESOURCE_NOT_FOUND,
                          "No login found for user [%s].", zName);
    }
    cson_object_set( pUser, "uid", cson_value_new_integer(uid) );
  }

  /* Maintenance note: all error-returns from here on out should go
     via 'goto error' in order to clean up.
  */
  
  if(uid != g.userUid){
    /*
      TODO: do not allow an admin user to modify a setup user
      unless the admin is also a setup user. setup.c uses
      that logic. There is a corner case for a NEW Setup user
      which the admin is just installing. Hmm.      
    */
    if(!g.perm.Admin && !g.perm.Setup){
      json_set_err(FSL_JSON_E_DENIED,
                   "Changing another user's data requires "
                   "'a' or 's' privileges.");
    }
  }
  
  blob_append(&sql, "UPDATE USER SET",-1 );
  blob_append(&sql, " mtime=cast(strftime('%s') AS INTEGER)", -1);

  if((uid>0) && zNameNew){
    /* Check for name change... */
    if( (!g.perm.Admin && !g.perm.Setup)
        && zNameNew && (zName != zNameNew)
        && (0!=strcmp(zNameNew,zName))){
      json_set_err( FSL_JSON_E_DENIED,
                    "Modifying user names requires 'a' or 's' privileges.");
      goto error;
    }
    forceLogout = cson_value_true()
        /* reminders: 1) does not allocate.
         2) we do this because changing a name
         invalidates any login token because the old name
         is part of the token hash.
        */;
    blob_appendf(&sql, ", login=%Q", zNameNew);
    ++gotFields;
  }

  if( zCap ){
    blob_appendf(&sql, ", cap=%Q", zCap);
    ++gotFields;
  }

  if( zPW ){
    char * zPWHash = NULL;
    ++gotFields;
    zPWHash = sha1_shared_secret(zPW, zNameNew ? zNameNew : zName, NULL);
    blob_appendf(&sql, ", pw=%Q", zPWHash);
    free(zPWHash);
  }

  if( zInfo ){
    blob_appendf(&sql, ", info=%Q", zInfo);
    ++gotFields;
  }

  if((g.perm.Admin || g.perm.Setup)
     && forceLogout && cson_value_get_bool(forceLogout)){
    blob_append(&sql, ", cookie=NULL, cexpire=NULL", -1);
    ++gotFields;
  }
  
  if(!gotFields){
    json_set_err( FSL_JSON_E_MISSING_ARGS,
                  "Required user data are missing.");
    goto error;
  }
  assert(uid>0);
  blob_appendf(&sql, " WHERE uid=%d", uid);
  free( zNameFree );
#if 0
  puts(blob_str(&sql));
  cson_output_FILE( cson_object_value(pUser), stdout, NULL );
#endif
  db_prepare(&q, "%s", blob_str(&sql));
  blob_reset(&sql);
  db_exec(&q);
  db_finalize(&q);
  return 0;

  error:
  assert(0 != g.json.resultCode);
  free(zNameFree);
  blob_reset(&sql);
  return g.json.resultCode;
}


/*
** Impl of /json/user/save.
**
** TODOs:
**
** - Return something useful in the payload (at least the id of the
** modified/created user).
*/
static cson_value * json_user_save(){
  /* try to get user info from GET/CLI args and construct
     a JSON form of it... */
  cson_object * u = cson_new_object();
  char const * str = NULL;
  char b = -1;
  int i = -1;
  int uid = -1;
  cson_value * payload = NULL;
#define PROP(LK) str = json_find_option_cstr(LK,NULL,NULL);             \
  if(str){ cson_object_set(u, LK, json_new_string(str)); } (void)0
  PROP("name");
  PROP("password");
  PROP("info");
  PROP("capabilities");
#undef PROP
  
#define PROP(LK,DFLT) b = json_find_option_bool(LK,NULL,NULL,DFLT);     \
  if(DFLT!=b){ cson_object_set(u, LK, cson_value_new_bool(b)); } (void)0
  PROP("forceLogout",-1);
#undef PROP

#define PROP(LK,DFLT) i = json_find_option_int(LK,NULL,NULL,DFLT);      \
  if(DFLT != i){ cson_object_set(u, LK, cson_value_new_integer(i)); } (void)0
  PROP("uid",-99);
#undef PROP
  if( g.json.reqPayload.o ){
    cson_object_merge( u, g.json.reqPayload.o, CSON_MERGE_NO_RECURSE );
  }
  json_user_update_from_json( u );
  if(!g.json.resultCode){
    uid = cson_value_get_integer( cson_object_get(u, "uid") );
    assert((uid>0) && "Something went wrong in json_user_update_from_json()");
    payload = json_load_user_by_id(uid);
  }
  cson_free_object(u);
  return payload;
}
#endif /* FOSSIL_ENABLE_JSON */
