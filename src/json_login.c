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
#include "json_login.h"

#if INTERFACE
#include "json_detail.h"
#endif


/*
** Implementation of the /json/login page.
**
*/
cson_value * json_page_login(){
  char preciseErrors = /* if true, "complete" JSON error codes are used,
                          else they are "dumbed down" to a generic login
                          error code.
                       */
#if 1
    g.json.errorDetailParanoia ? 0 : 1
#else
    0
#endif
    ;
  /*
    FIXME: we want to check the GET/POST args in this order:

    - GET: name, n, password, p
    - POST: name, password

    but a bug in cgi_parameter() is breaking that, causing PD() to
    return the last element of the PATH_INFO instead.

    Summary: If we check for P("name") first, then P("n"),
    then ONLY a GET param of "name" will match ("n"
    is not recognized). If we reverse the order of the
    checks then both forms work. Strangely enough, the
    "p"/"password" check is not affected by this.
   */
  char const * name = cson_value_get_cstr(json_req_payload_get("name"));
  char const * pw = NULL;
  char const * anonSeed = NULL;
  cson_value * payload = NULL;
  int uid = 0;
  /* reminder to self: Fossil internally (for the sake of /wiki)
     interprets paths in the form /foo/bar/baz such that P("name") ==
     "bar/baz". This collides with our name/password checking, and
     thus we do some rather elaborate name=... checking.
  */
  pw = cson_value_get_cstr(json_req_payload_get("password"));
  if( !pw ){
    pw = PD("p",NULL);
    if( !pw ){
      pw = PD("password",NULL);
    }
  }
  if(!pw){
    g.json.resultCode = preciseErrors
      ? FSL_JSON_E_LOGIN_FAILED_NOPW
      : FSL_JSON_E_LOGIN_FAILED;
    return NULL;
  }

  if( !name ){
    name = PD("n",NULL);
    if( !name ){
      name = PD("name",NULL);
      if( !name ){
        g.json.resultCode = preciseErrors
          ? FSL_JSON_E_LOGIN_FAILED_NONAME
          : FSL_JSON_E_LOGIN_FAILED;
        return NULL;
      }
    }
  }

  if(0 == strcmp("anonymous",name)){
    /* check captcha/seed values... */
    enum { SeedBufLen = 100 /* in some JSON tests i once actually got an
                           80-digit number.
                        */
    };
    static char seedBuffer[SeedBufLen];
    cson_value const * jseed = json_getenv(FossilJsonKeys.anonymousSeed);
    seedBuffer[0] = 0;
    if( !jseed ){
      jseed = json_req_payload_get(FossilJsonKeys.anonymousSeed);
      if( !jseed ){
        jseed = json_getenv("cs") /* name used by HTML interface */;
      }
    }
    if(jseed){
      if( cson_value_is_number(jseed) ){
        sprintf(seedBuffer, "%"CSON_INT_T_PFMT, cson_value_get_integer(jseed));
        anonSeed = seedBuffer;
      }else if( cson_value_is_string(jseed) ){
        anonSeed = cson_string_cstr(cson_value_get_string(jseed));
      }
    }
    if(!anonSeed){
      g.json.resultCode = preciseErrors
        ? FSL_JSON_E_LOGIN_FAILED_NOSEED
        : FSL_JSON_E_LOGIN_FAILED;
      return NULL;
    }
  }

#if 0
  {
    /* only for debugging the PD()-incorrect-result problem */
    cson_object * o = NULL;
    uid = login_search_uid( name, pw );
    payload = cson_value_new_object();
    o = cson_value_get_object(payload);
    cson_object_set( o, "n", cson_value_new_string(name,strlen(name)));
    cson_object_set( o, "p", cson_value_new_string(pw,strlen(pw)));
    return payload;
  }
#endif
  uid = anonSeed
    ? login_is_valid_anonymous(name, pw, anonSeed)
    : login_search_uid(name, pw)
    ;
  if( !uid ){
    g.json.resultCode = preciseErrors
      ? FSL_JSON_E_LOGIN_FAILED_NOTFOUND
      : FSL_JSON_E_LOGIN_FAILED;
    return NULL;
  }else{
    char * cookie = NULL;
    cson_object * po;
    char * cap = NULL;
    if(anonSeed){
      login_set_anon_cookie(NULL, &cookie);
    }else{
      login_set_user_cookie(name, uid, &cookie);
    }
    payload = cson_value_new_object();
    po = cson_value_get_object(payload);
    cson_object_set(po, "authToken", json_new_string(cookie));
    free(cookie);
    cson_object_set(po, "name", json_new_string(name));
    cap = db_text(NULL, "SELECT cap FROM user WHERE login=%Q", name);
    cson_object_set(po, "capabilities", cap ? json_new_string(cap) : cson_value_null() );
    free(cap);
    cson_object_set(po, "loginCookieName", json_new_string( login_cookie_name() ) );
    /* TODO: add loginExpiryTime to the payload. To do this properly
       we "should" add an ([unsigned] int *) to
       login_set_user_cookie() and login_set_anon_cookie(), to which
       the expiry time is assigned. (Remember that JSON doesn't do
       unsigned int.)

       For non-anonymous users we could also simply query the
       user.cexpire db field after calling login_set_user_cookie(),
       but for anonymous we need to get the time when the cookie is
       set because anon does not get a db entry like normal users
       do. Anonymous cookies currently have a hard-coded lifetime in
       login_set_anon_cookie() (currently 6 hours), which we "should
       arguably" change to use the time configured for non-anonymous
       users (see login_set_user_cookie() for details).
    */
    return payload;
  }
}

/*
** Impl of /json/logout.
**
*/
cson_value * json_page_logout(){
  cson_value const *token = g.json.authToken;
    /* Remember that json_mode_bootstrap() replaces the login cookie
       with the JSON auth token if the request contains it. If the
       request is missing the auth token then this will fetch fossil's
       original cookie. Either way, it's what we want :).

       We require the auth token to avoid someone maliciously
       trying to log someone else out (not 100% sure if that
       would be possible, given fossil's hardened cookie, but
       I'll assume it would be for the time being).
    */
    ;
  if(!token){
    g.json.resultCode = FSL_JSON_E_MISSING_AUTH;
  }else{
    login_clear_login_data();
    g.json.authToken = NULL /* memory is owned elsewhere.*/;
    json_setenv(FossilJsonKeys.authToken, NULL);
  }
  return json_page_whoami();
}

/*
** Implementation of the /json/anonymousPassword page.
*/
cson_value * json_page_anon_password(){
  cson_value * v = cson_value_new_object();
  cson_object * o = cson_value_get_object(v);
  unsigned const int seed = captcha_seed();
  char const * zCaptcha = captcha_decode(seed);
  cson_object_set(o, "seed",
                  cson_value_new_integer( (cson_int_t)seed )
                  );
  cson_object_set(o, "password",
                  cson_value_new_string( zCaptcha, strlen(zCaptcha) )
                  );
  return v;
}



/*
** Implements the /json/whoami page/command.
*/
cson_value * json_page_whoami(){
  cson_value * payload = NULL;
  cson_object * obj = NULL;
  Stmt q;
  if(!g.json.authToken){
      /* assume we just logged out. */
      db_prepare(&q, "SELECT login, cap FROM user WHERE login='nobody'");
  }
  else{
      db_prepare(&q, "SELECT login, cap FROM user WHERE uid=%d",
                 g.userUid);
  }
  if( db_step(&q)==SQLITE_ROW ){

    /* reminder: we don't use g.zLogin because it's 0 for the guest
       user and the HTML UI appears to currently allow the name to be
       changed (but doing so would break other code). */
    char const * str;
    payload = cson_value_new_object();
    obj = cson_value_get_object(payload);
    str = (char const *)sqlite3_column_text(q.pStmt,0);
    if( str ){
      cson_object_set( obj, "name",
                       cson_value_new_string(str,strlen(str)) );
    }
    str = (char const *)sqlite3_column_text(q.pStmt,1);
    if( str ){
      cson_object_set( obj, "capabilities",
                       cson_value_new_string(str,strlen(str)) );
    }
    if( g.json.authToken ){
      cson_object_set( obj, "authToken", g.json.authToken );
    }
  }else{
    g.json.resultCode = FSL_JSON_E_RESOURCE_NOT_FOUND;
  }
  db_finalize(&q);
  return payload;
}
#endif /* FOSSIL_ENABLE_JSON */
