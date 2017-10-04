#ifdef FOSSIL_ENABLE_JSON
/*
** Copyright (c) 2011 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** Code for the JSON API.
**
** For notes regarding the public JSON interface, please see:
**
** https://docs.google.com/document/d/1fXViveNhDbiXgCuE7QDXQOKeFzf2qNUkBEgiUvoqFN4/view
**
**
** Notes for hackers...
**
** Here's how command/page dispatching works: json_page_top() (in HTTP mode) or
** json_cmd_top() (in CLI mode) catch the "json" path/command. Those functions then
** dispatch to a JSON-mode-specific command/page handler with the type fossil_json_f().
** See the API docs for that typedef (below) for the semantics of the callbacks.
**
**
*/
#include "VERSION.h"
#include "config.h"
#include "json.h"
#include <assert.h>
#include <time.h>

#if INTERFACE
#include "json_detail.h" /* workaround for apparent enum limitation in makeheaders */
#endif

const FossilJsonKeys_ FossilJsonKeys = {
  "anonymousSeed" /*anonymousSeed*/,
  "authToken"  /*authToken*/,
  "COMMAND_PATH" /*commandPath*/,
  "mtime" /*mtime*/,
  "payload" /* payload */,
  "requestId" /*requestId*/,
  "resultCode" /*resultCode*/,
  "resultText" /*resultText*/,
  "timestamp" /*timestamp*/
};



/*
** Returns true (non-0) if fossil appears to be running in JSON mode.
*/
int fossil_has_json(){
  return g.json.isJsonMode && (g.isHTTP || g.json.post.o);
}

/*
** Placeholder /json/XXX page impl for NYI (Not Yet Implemented)
** (but planned) pages/commands.
*/
cson_value * json_page_nyi(){
  g.json.resultCode = FSL_JSON_E_NYI;
  return NULL;
}

/*
** Given a FossilJsonCodes value, it returns a string suitable for use
** as a resultCode string. Returns some unspecified non-empty string
** if errCode is not one of the FossilJsonCodes values.
*/
static char const * json_err_cstr( int errCode ){
  switch( errCode ){
    case 0: return "Success";
#define C(X,V) case FSL_JSON_E_ ## X: return V

    C(GENERIC,"Generic error");
    C(INVALID_REQUEST,"Invalid request");
    C(UNKNOWN_COMMAND,"Unknown command or subcommand");
    C(UNKNOWN,"Unknown error");
    C(TIMEOUT,"Timeout reached");
    C(ASSERT,"Assertion failed");
    C(ALLOC,"Resource allocation failed");
    C(NYI,"Not yet implemented");
    C(PANIC,"x");
    C(MANIFEST_READ_FAILED,"Reading artifact manifest failed");
    C(FILE_OPEN_FAILED,"Opening file failed");

    C(AUTH,"Authentication error");
    C(MISSING_AUTH,"Authentication info missing from request");
    C(DENIED,"Access denied");
    C(WRONG_MODE,"Request not allowed (wrong operation mode)");
    C(LOGIN_FAILED,"Login failed");
    C(LOGIN_FAILED_NOSEED,"Anonymous login attempt was missing password seed");
    C(LOGIN_FAILED_NONAME,"Login failed - name not supplied");
    C(LOGIN_FAILED_NOPW,"Login failed - password not supplied");
    C(LOGIN_FAILED_NOTFOUND,"Login failed - no match found");

    C(USAGE,"Usage error");
    C(INVALID_ARGS,"Invalid argument(s)");
    C(MISSING_ARGS,"Missing argument(s)");
    C(AMBIGUOUS_UUID,"Resource identifier is ambiguous");
    C(UNRESOLVED_UUID,"Provided uuid/tag/branch could not be resolved");
    C(RESOURCE_ALREADY_EXISTS,"Resource already exists");
    C(RESOURCE_NOT_FOUND,"Resource not found");

    C(DB,"Database error");
    C(STMT_PREP,"Statement preparation failed");
    C(STMT_BIND,"Statement parameter binding failed");
    C(STMT_EXEC,"Statement execution/stepping failed");
    C(DB_LOCKED,"Database is locked");
    C(DB_NEEDS_REBUILD,"Fossil repository needs to be rebuilt");
    C(DB_NOT_FOUND,"Fossil repository db file could not be found.");
    C(DB_NOT_VALID, "Fossil repository db file is not valid.");
    C(DB_NEEDS_CHECKOUT, "Command requires a local checkout.");
#undef C
    default:
      return "Unknown Error";
  }
}

/*
** Implements the cson_data_dest_f() interface and outputs the data to
** a fossil Blob object.  pState must be-a initialized (Blob*), to
** which n bytes of src will be appended.
**/
int cson_data_dest_Blob(void * pState, void const * src, unsigned int n){
  Blob * b = (Blob*)pState;
  blob_append( b, (char const *)src, (int)n ) /* will die on OOM */;
  return 0;
}

/*
** Implements the cson_data_source_f() interface and reads input from
** a fossil Blob object. pState must be-a (Blob*) populated with JSON
** data.
*/
int cson_data_src_Blob(void * pState, void * dest, unsigned int * n){
  Blob * b = (Blob*)pState;
  *n = blob_read( b, dest, *n );
  return 0;
}

/*
** Convenience wrapper around cson_output() which appends the output
** to pDest. pOpt may be NULL, in which case g.json.outOpt will be used.
*/
int cson_output_Blob( cson_value const * pVal, Blob * pDest, cson_output_opt const * pOpt ){
  return cson_output( pVal, cson_data_dest_Blob,
                      pDest, pOpt ? pOpt : &g.json.outOpt );
}

/*
** Convenience wrapper around cson_parse() which reads its input
** from pSrc. pSrc is rewound before parsing.
**
** pInfo may be NULL. If it is not NULL then it will contain details
** about the parse state when this function returns.
**
** On success a new JSON Object or Array is returned (owned by the
** caller). On error NULL is returned.
*/
cson_value * cson_parse_Blob( Blob * pSrc, cson_parse_info * pInfo ){
  cson_value * root = NULL;
  blob_rewind( pSrc );
  cson_parse( &root, cson_data_src_Blob, pSrc, NULL, pInfo );
  return root;
}

/*
** Implements the cson_data_dest_f() interface and outputs the data to
** cgi_append_content(). pState is ignored.
**/
int cson_data_dest_cgi(void * pState, void const * src, unsigned int n){
  cgi_append_content( (char const *)src, (int)n );
  return 0;
}

/*
** Returns a string in the form FOSSIL-XXXX, where XXXX is a
** left-zero-padded value of code. The returned buffer is static, and
** must be copied if needed for later.  The returned value will always
** be 11 bytes long (not including the trailing NUL byte).
**
** In practice we will only ever call this one time per app execution
** when constructing the JSON response envelope, so the static buffer
** "shouldn't" be a problem.
**
*/
char const * json_rc_cstr( int code ){
  enum { BufSize = 12 };
  static char buf[BufSize] = {'F','O','S','S','I','L','-',0};
  assert((code >= 1000) && (code <= 9999) && "Invalid Fossil/JSON code.");
  sprintf(buf+7,"%04d", code);
  return buf;
}

/*
** Adds v to the API-internal cleanup mechanism. key is ignored
** (legacy) but might be re-introduced and "should" be a unique
** (app-wide) value.  Failure to insert an item may be caused by any
** of the following:
**
** - Allocation error.
** - g.json.gc.a is NULL
** - key is NULL or empty.
**
** Returns 0 on success.
**
** Ownership of v is transfered to (or shared with) g.json.gc, and v
** will be valid until that object is cleaned up or some internal code
** incorrectly removes it from the gc (which we never do). If this
** function fails, it is fatal to the app (as it indicates an
** allocation error (more likely than not) or a serious internal error
** such as numeric overflow).
*/
void json_gc_add( char const * key, cson_value * v ){
  int const rc = cson_array_append( g.json.gc.a, v );

  assert( NULL != g.json.gc.a );
  if( 0 != rc ){
    cson_value_free( v );
  }
  assert( (0==rc) && "Adding item to GC failed." );
  if(0!=rc){
    fprintf(stderr,"%s: FATAL: alloc error.\n", g.argv[0])
        /* reminder: allocation error is the only reasonable cause of
           error here, provided g.json.gc.a and v are not NULL.
        */
        ;
    fossil_exit(1)/*not fossil_panic() b/c it might land us somewhere
                    where this function is called again.
                  */;
  }
}


/*
** Returns the value of json_rc_cstr(code) as a new JSON
** string, which is owned by the caller and must eventually
** be cson_value_free()d or transfered to a JSON container.
*/
cson_value * json_rc_string( int code ){
  return cson_value_new_string( json_rc_cstr(code), 11 );
}

cson_value * json_new_string( char const * str ){
  return str
    ? cson_value_new_string(str,strlen(str))
    : NULL;
}

cson_value * json_new_string_f( char const * fmt, ... ){
  cson_value * v;
  char * zStr;
  va_list vargs;
  va_start(vargs,fmt);
  zStr = vmprintf(fmt,vargs);
  va_end(vargs);
  v = cson_value_new_string(zStr, strlen(zStr));
  free(zStr);
  return v;
}

cson_value * json_new_int( i64 v ){
  return cson_value_new_integer((cson_int_t)v);
}

/*
** Gets a POST/POST.payload/GET/COOKIE/ENV value. The returned memory
** is owned by the g.json object (one of its sub-objects). Returns
** NULL if no match is found.
**
** ENV means the system environment (getenv()).
**
** Precedence: POST.payload, GET/COOKIE/non-JSON POST, JSON POST, ENV.
**
** FIXME: the precedence SHOULD be: GET, POST.payload, POST, COOKIE,
** ENV, but the amalgamation of the GET/POST vars makes it difficult
** for me to do that. Since fossil only uses one cookie, cookie
** precedence isn't a real/high-priority problem.
*/
cson_value * json_getenv( char const * zKey ){
  cson_value * rc;
  rc = g.json.reqPayload.o
    ? cson_object_get( g.json.reqPayload.o, zKey )
    : NULL;
  if(rc){
    return rc;
  }
  rc = cson_object_get( g.json.param.o, zKey );
  if( rc ){
    return rc;
  }
  rc = cson_object_get( g.json.post.o, zKey );
  if(rc){
    return rc;
  }else{
    char const * cv = PD(zKey,NULL);
    if(!cv && !g.isHTTP){
      /* reminder to self: in CLI mode i'd like to try
         find_option(zKey,NULL,XYZ) here, but we don't have a sane
         default for the XYZ param here.
      */
      cv = fossil_getenv(zKey);
    }
    if(cv){/*transform it to JSON for later use.*/
      /* use sscanf() to figure out if it's an int,
         and transform it to JSON int if it is.

         FIXME: use strtol(), since it has more accurate
         error handling.
      */
      int intVal = -1;
      char endOfIntCheck;
      int const scanRc = sscanf(cv,"%d%c",&intVal, &endOfIntCheck)
        /* The %c bit there is to make sure that we don't accept 123x
          as a number. sscanf() returns the number of tokens
          successfully parsed, so an RC of 1 will be correct for "123"
          but "123x" will have RC==2.

          But it appears to not be working that way :/
        */
        ;
      if(1==scanRc){
        json_setenv( zKey, cson_value_new_integer(intVal) );
      }else{
        rc = cson_value_new_string(cv,strlen(cv));
        json_setenv( zKey, rc );
      }
      return rc;
    }else{
      return NULL;
    }
  }
}

/*
** Wrapper around json_getenv() which...
**
** If it finds a value and that value is-a JSON number or is a string
** which looks like an integer or is-a JSON bool/null then it is
** converted to an int. If none of those apply then dflt is returned.
*/
int json_getenv_int(char const * pKey, int dflt ){
  cson_value const * v = json_getenv(pKey);
  const cson_type_id type = v ? cson_value_type_id(v) : CSON_TYPE_UNDEF;
  switch(type){
    case CSON_TYPE_INTEGER:
    case CSON_TYPE_DOUBLE:
      return (int)cson_value_get_integer(v);
    case CSON_TYPE_STRING: {
      char const * sv = cson_string_cstr(cson_value_get_string(v));
      assert( (NULL!=sv) && "This is quite unexpected." );
      return sv ? atoi(sv) : dflt;
    }
    case CSON_TYPE_BOOL:
      return cson_value_get_bool(v) ? 1 : 0;
    case CSON_TYPE_NULL:
      return 0;
    default:
      return dflt;
  }
}


/*
** Wrapper around json_getenv() which tries to evaluate a payload/env
** value as a boolean. Uses mostly the same logic as
** json_getenv_int(), with the addition that string values which
** either start with a digit 1..9 or the letters [tTyY] are considered
** to be true. If this function cannot find a matching key/value then
** dflt is returned. e.g. if it finds the key but the value is-a
** Object then dftl is returned.
**
** If an entry is found, this function guarantees that it will return
** either 0 or 1, as opposed to "0 or non-zero", so that clients can
** pass a different value as dflt. Thus they can use, e.g. -1 to know
** whether or not this function found a match (it will return -1 in
** that case).
*/
int json_getenv_bool(char const * pKey, int dflt ){
  cson_value const * v = json_getenv(pKey);
  const cson_type_id type = v ? cson_value_type_id(v) : CSON_TYPE_UNDEF;
  switch(type){
    case CSON_TYPE_INTEGER:
    case CSON_TYPE_DOUBLE:
      return cson_value_get_integer(v) ? 1 : 0;
    case CSON_TYPE_STRING: {
      char const * sv = cson_string_cstr(cson_value_get_string(v));
      assert( (NULL!=sv) && "This is quite unexpected." );
      if(!*sv || ('0'==*sv)){
        return 0;
      }else{
        return ((('1'<=*sv) && ('9'>=*sv))
                || ('t'==*sv) || ('T'==*sv)
                || ('y'==*sv) || ('Y'==*sv)
                )
          ? 1 : 0;
      }
    }
    case CSON_TYPE_BOOL:
      return cson_value_get_bool(v) ? 1 : 0;
    case CSON_TYPE_NULL:
      return 0;
    default:
      return dflt;
  }
}

/*
** Returns the string form of a json_getenv() value, but ONLY If that
** value is-a String. Non-strings are not converted to strings for
** this purpose. Returned memory is owned by g.json or fossil and is
** valid until end-of-app or the given key is replaced in fossil's
** internals via cgi_replace_parameter() and friends or json_setenv().
*/
char const * json_getenv_cstr( char const * zKey ){
  return cson_value_get_cstr( json_getenv(zKey) );
}

/*
** An extended form of find_option() which tries to look up a combo
** GET/POST/CLI argument.
**
** zKey must be the GET/POST parameter key. zCLILong must be the "long
** form" CLI flag (NULL means to use zKey). zCLIShort may be NULL or
** the "short form" CLI flag (if NULL, no short form is used).
**
** If argPos is >=0 and no other match is found,
** json_command_arg(argPos) is also checked.
**
** On error (no match found) NULL is returned.
**
** This ONLY works for String JSON/GET/CLI values, not JSON
** booleans and whatnot.
*/
char const * json_find_option_cstr2(char const * zKey,
                                    char const * zCLILong,
                                    char const * zCLIShort,
                                    int argPos){
  char const * rc = NULL;
  assert(NULL != zKey);
  if(!g.isHTTP){
    rc = find_option(zCLILong ? zCLILong : zKey,
                     zCLIShort, 1);
  }
  if(!rc && fossil_has_json()){
    rc = json_getenv_cstr(zKey);
    if(!rc && zCLIShort){
      rc = cson_value_get_cstr( cson_object_get( g.json.param.o, zCLIShort) );
    }
  }
  if(!rc && (argPos>=0)){
    rc = json_command_arg((unsigned char)argPos);
  }
  return rc;
}

/*
** Short-hand form of json_find_option_cstr2(zKey,zCLILong,zCLIShort,-1).
*/
char const * json_find_option_cstr(char const * zKey,
                                   char const * zCLILong,
                                   char const * zCLIShort){
  return json_find_option_cstr2(zKey, zCLILong, zCLIShort, -1);
}

/*
** The boolean equivalent of json_find_option_cstr().
** If the option is not found, dftl is returned.
*/
int json_find_option_bool(char const * zKey,
                          char const * zCLILong,
                          char const * zCLIShort,
                          int dflt ){
  int rc = -1;
  if(!g.isHTTP){
    if(NULL != find_option(zCLILong ? zCLILong : zKey,
                           zCLIShort, 0)){
      rc = 1;
    }
  }
  if((-1==rc) && fossil_has_json()){
    rc = json_getenv_bool(zKey,-1);
  }
  return (-1==rc) ? dflt : rc;
}

/*
** The integer equivalent of json_find_option_cstr2().
** If the option is not found, dftl is returned.
*/
int json_find_option_int(char const * zKey,
                         char const * zCLILong,
                         char const * zCLIShort,
                         int dflt ){
  enum { Magic = -1947854832 };
  int rc = Magic;
  if(!g.isHTTP){
    /* FIXME: use strtol() for better error/dflt handling. */
    char const * opt = find_option(zCLILong ? zCLILong : zKey,
                                   zCLIShort, 1);
    if(NULL!=opt){
      rc = atoi(opt);
    }
  }
  if(Magic==rc){
    rc = json_getenv_int(zKey,Magic);
  }
  return (Magic==rc) ? dflt : rc;
}


/*
** Adds v to g.json.param.o using the given key. May cause any prior
** item with that key to be destroyed (depends on current reference
** count for that value). On success, transfers (or shares) ownership
** of v to (or with) g.json.param.o. On error ownership of v is not
** modified.
*/
int json_setenv( char const * zKey, cson_value * v ){
  return cson_object_set( g.json.param.o, zKey, v );
}

/*
** Guesses a RESPONSE Content-Type value based (primarily) on the
** HTTP_ACCEPT header.
**
** It will try to figure out if the client can support
** application/json or application/javascript, and will fall back to
** text/plain if it cannot figure out anything more specific.
**
** Returned memory is static and immutable, but if the environment
** changes after calling this then subsequent calls to this function
** might return different (also static/immutable) values.
*/
char const * json_guess_content_type(){
  char const * cset;
  char doUtf8;
  cset = PD("HTTP_ACCEPT_CHARSET",NULL);
  doUtf8 = ((NULL == cset) || (NULL!=strstr("utf-8",cset)))
    ? 1 : 0;
  if( g.json.jsonp ){
    return doUtf8
      ? "application/javascript; charset=utf-8"
      : "application/javascript";
  }else{
    /*
      Content-type

      If the browser does not sent an ACCEPT for application/json
      then we fall back to text/plain.
    */
    char const * cstr;
    cstr = PD("HTTP_ACCEPT",NULL);
    if( NULL == cstr ){
      return doUtf8
        ? "application/json; charset=utf-8"
        : "application/json";
    }else{
      if( strstr( cstr, "application/json" )
          || strstr( cstr, "*/*" ) ){
        return doUtf8
          ? "application/json; charset=utf-8"
          : "application/json";
      }else{
        return "text/plain";
      }
    }
  }
}

/*
** Sends pResponse to the output stream as the response object.  This
** function does no validation of pResponse except to assert() that it
** is not NULL. The caller is responsible for ensuring that it meets
** API response envelope conventions.
**
** In CLI mode pResponse is sent to stdout immediately. In HTTP
** mode pResponse replaces any current CGI content but cgi_reply()
** is not called to flush the output.
**
** If g.json.jsonp is not NULL then the content type is set to
** application/javascript and the output is wrapped in a jsonp
** wrapper.
*/
void json_send_response( cson_value const * pResponse ){
  assert( NULL != pResponse );
  if( g.isHTTP ){
    cgi_reset_content();
    if( g.json.jsonp ){
      cgi_printf("%s(",g.json.jsonp);
    }
    cson_output( pResponse, cson_data_dest_cgi, NULL, &g.json.outOpt );
    if( g.json.jsonp ){
      cgi_append_content(")",1);
    }
  }else{/*CLI mode*/
    if( g.json.jsonp ){
      fprintf(stdout,"%s(",g.json.jsonp);
    }
    cson_output_FILE( pResponse, stdout, &g.json.outOpt );
    if( g.json.jsonp ){
      fwrite(")\n", 2, 1, stdout);
    }
  }
}

/*
** Returns the current request's JSON authentication token, or NULL if
** none is found. The token's memory is owned by (or shared with)
** g.json.
**
** If an auth token is found in the GET/POST request data then fossil
** is given that data for use in authentication for this
** session. i.e. the GET/POST data overrides fossil's authentication
** cookie value (if any) and also works with clients which do not
** support cookies.
**
** Must be called once before login_check_credentials() is called or
** we will not be able to replace fossil's internal idea of the auth
** info in time (and future changes to that state may cause unexpected
** results).
**
** The result of this call are cached for future calls.
*/
cson_value * json_auth_token(){
    assert(g.json.gc.a && "json_main_bootstrap() was not called!");
    if( !g.json.authToken ){
    /* Try to get an authorization token from GET parameter, POSTed
       JSON, or fossil cookie (in that order). */
    g.json.authToken = json_getenv(FossilJsonKeys.authToken);
    if(g.json.authToken
       && cson_value_is_string(g.json.authToken)
       && !PD(login_cookie_name(),NULL)){
      /* tell fossil to use this login info.

      FIXME?: because the JSON bits don't carry around
      login_cookie_name(), there is(?) a potential(?) login hijacking
      window here. We may need to change the JSON auth token to be in
      the form: login_cookie_name()=...

      Then again, the hardened cookie value helps ensure that
      only a proper key/value match is valid.
      */
      cgi_replace_parameter( login_cookie_name(), cson_value_get_cstr(g.json.authToken) );
    }else if( g.isHTTP ){
      /* try fossil's conventional cookie. */
      /* Reminder: chicken/egg scenario regarding db access in CLI
         mode because login_cookie_name() needs the db. CLI
         mode does not use any authentication, so we don't need
         to support it here.
      */
      char const * zCookie = P(login_cookie_name());
      if( zCookie && *zCookie ){
        /* Transfer fossil's cookie to JSON for downstream convenience... */
        cson_value * v = cson_value_new_string(zCookie, strlen(zCookie));
        json_gc_add( FossilJsonKeys.authToken, v );
        g.json.authToken = v;
      }
    }
  }
  return g.json.authToken;
}

/*
** If g.json.reqPayload.o is NULL then NULL is returned, else the
** given property is searched for in the request payload.  If found it
** is returned. The returned value is owned by (or shares ownership
** with) g.json, and must NOT be cson_value_free()'d by the
** caller.
*/
cson_value * json_req_payload_get(char const *pKey){
  return g.json.reqPayload.o
    ? cson_object_get(g.json.reqPayload.o,pKey)
    : NULL;
}

/*
** Initializes some JSON bits which need to be initialized relatively
** early on. It should only be called from cgi_init() or
** json_cmd_top() (early on in those functions).
**
** Initializes g.json.gc and g.json.param. This code does not (and
** must not) rely on any of the fossil environment having been set
** up. e.g. it must not use cgi_parameter() and friends because this
** must be called before those data are initialized.
*/
void json_main_bootstrap(){
  cson_value * v;
  assert( (NULL == g.json.gc.v) &&
          "json_main_bootstrap() was called twice!" );

  g.json.timerId = fossil_timer_start();

  /* g.json.gc is our "garbage collector" - where we put JSON values
     which need a long lifetime but don't have a logical parent to put
     them in.
  */
  v = cson_value_new_array();
  g.json.gc.v = v;
  assert(0 != g.json.gc.v);
  g.json.gc.a = cson_value_get_array(v);
  assert(0 != g.json.gc.a);
  cson_value_add_reference(v)
    /* Needed to allow us to include this value in other JSON
       containers without transferring ownership to those containers.
       All other persistent g.json.XXX.v values get appended to
       g.json.gc.a, and therefore already have a live reference
       for this purpose.
    */
    ;

  /*
    g.json.param holds the JSONized counterpart of fossil's
    cgi_parameter_xxx() family of data. We store them as JSON, as
    opposed to using fossil's data directly, because we can retain
    full type information for data this way (as opposed to it always
    being of type string).
  */
  v = cson_value_new_object();
  g.json.param.v = v;
  g.json.param.o = cson_value_get_object(v);
  json_gc_add("$PARAMS", v);
}

/*
** Appends a warning object to the (pending) JSON response.
**
** Code must be a FSL_JSON_W_xxx value from the FossilJsonCodes enum.
**
** A Warning object has this JSON structure:
**
** { "code":integer, "text":"string" }
**
** But the text part is optional.
**
** If msg is non-NULL and not empty then it is used as the "text"
** property's value. It is copied, and need not refer to static
** memory.
**
** CURRENTLY this code only allows a given warning code to be
** added one time, and elides subsequent warnings. The intention
** is to remove that burden from loops which produce warnings.
**
** FIXME: if msg is NULL then use a standard string for
** the given code. If !*msg then elide the "text" property,
** for consistency with how json_err() works.
*/
void json_warn( int code, char const * fmt, ... ){
  cson_object * obj = NULL;
  assert( (code>FSL_JSON_W_START)
          && (code<FSL_JSON_W_END)
          && "Invalid warning code.");
  assert(g.json.gc.a && "json_main_bootstrap() was not called!");
  if(!g.json.warnings){
    g.json.warnings = cson_new_array();
    assert((NULL != g.json.warnings) && "Alloc error.");
    json_gc_add("$WARNINGS",cson_array_value(g.json.warnings));
  }
  obj = cson_new_object();
  cson_array_append(g.json.warnings, cson_object_value(obj));
  cson_object_set(obj,"code",cson_value_new_integer(code));
  if(fmt && *fmt){
    /* FIXME: treat NULL fmt as standard warning message for
       the code, but we don't have those yet.
    */
    va_list vargs;
    char * msg;
    va_start(vargs,fmt);
    msg = vmprintf(fmt,vargs);
    va_end(vargs);
    cson_object_set(obj,"text", cson_value_new_string(msg,strlen(msg)));
    free(msg);
  }
}

/*
** Splits zStr (which must not be NULL) into tokens separated by the
** given separator character. If doDeHttp is true then each element
** will be passed through dehttpize(), otherwise they are used
** as-is. Note that tokenization happens before dehttpize(),
** which is significant if the ENcoded tokens might contain the
** separator character.
**
** Each new element is appended to the given target array object,
** which must not be NULL and ownership of it is not changed by this
** call.
**
** On success, returns the number of tokens _encountered_. On error a
** NEGATIVE number is returned - its absolute value is the number of
** tokens encountered (i.e. it reveals which token in zStr was
** problematic).
**
** Achtung: leading and trailing whitespace of elements are elided.
**
** Achtung: empty elements will be skipped, meaning consecutive empty
** elements are collapsed.
*/
int json_string_split( char const * zStr,
                       char separator,
                       int doDeHttp,
                       cson_array * target ){
  char const * p = zStr /* current byte */;
  char const * head  /* current start-of-token */;
  unsigned int len = 0   /* current token's length */;
  int rc = 0   /* return code (number of added elements)*/;
  assert( zStr && target );
  while( fossil_isspace(*p) ){
    ++p;
  }
  head = p;
  for( ; ; ++p){
    if( !*p || (separator == *p) ){
      if( len ){/* append head..(head+len) as next array
                   element. */
        cson_value * part = NULL;
        char * zPart = NULL;
        ++rc;
        assert( head != p );
        zPart = (char*)fossil_malloc(len+1);
        memcpy(zPart, head, len);
        zPart[len] = 0;
        if(doDeHttp){
          dehttpize(zPart);
        }
        if( *zPart ){ /* should only fail if someone manages to url-encoded a NUL byte */
          part = cson_value_new_string(zPart, strlen(zPart));
          if( 0 != cson_array_append( target, part ) ){
            cson_value_free(part);
            rc = -rc;
            break;
          }
        }else{
          assert(0 && "i didn't think this was possible!");
          fprintf(stderr,"%s:%d: My God! It's full of stars!\n",
                  __FILE__, __LINE__);
          fossil_exit(1)
            /* Not fossil_panic() b/c this code needs to be able to
              run before some of the fossil/json bits are initialized,
              and fossil_panic() calls into the JSON API.
            */
            ;
        }
        fossil_free(zPart);
        len = 0;
      }
      if( !*p ){
        break;
      }
      head = p+1;
      while( *head && fossil_isspace(*head) ){
        ++head;
        ++p;
      }
      if(!*head){
        break;
      }
      continue;
    }
    ++len;
  }
  return rc;
}

/*
** Wrapper around json_string_split(), taking the same first 3
** parameters as this function, but returns the results as a JSON
** Array (if splitting produced tokens) or NULL (if splitting failed
** in any way or produced no tokens).
**
** The returned value is owned by the caller. If not NULL then it
** _will_ have a JSON type of Array.
*/
cson_value * json_string_split2( char const * zStr,
                                 char separator,
                                 int doDeHttp ){
  cson_array * a = cson_new_array();
  int rc = json_string_split( zStr, separator, doDeHttp, a );
  if( 0>=rc ){
    cson_free_array(a);
    a = NULL;
  }
  return a ? cson_array_value(a) : NULL;
}


/*
** Performs some common initialization of JSON-related state.  Must be
** called by the json_page_top() and json_cmd_top() dispatching
** functions to set up the JSON stat used by the dispatched functions.
**
** Implicitly sets up the login information state in CGI mode, but
** does not perform any permissions checking. It _might_ (haven't
** tested this) die with an error if an auth cookie is malformed.
**
** This must be called by the top-level JSON command dispatching code
** before they do any work.
**
** This must only be called once, or an assertion may be triggered.
*/
static void json_mode_bootstrap(){
  static char once = 0  /* guard against multiple runs */;
  char const * zPath = P("PATH_INFO");
  assert(g.json.gc.a && "json_main_bootstrap() was not called!");
  assert( (0==once) && "json_mode_bootstrap() called too many times!");
  if( once ){
    return;
  }else{
    once = 1;
  }
  g.json.isJsonMode = 1;
  g.json.resultCode = 0;
  g.json.cmd.offset = -1;
  g.json.jsonp = PD("jsonp",NULL)
    /* FIXME: do some sanity checking on g.json.jsonp and ignore it
       if it is not halfway reasonable.
    */
    ;
  if( !g.isHTTP && g.fullHttpReply ){
    /* workaround for server mode, so we see it as CGI mode. */
    g.isHTTP = 1;
  }

  if(g.isHTTP){
    cgi_set_content_type(json_guess_content_type())
      /* reminder: must be done after g.json.jsonp is initialized */
      ;
#if 0
    /* Calling this seems to trigger an SQLITE_MISUSE warning???
       Maybe it's not legal to set the logger more than once?
    */
    sqlite3_config(SQLITE_CONFIG_LOG, NULL, 0)
        /* avoids debug messages on stderr in JSON mode */
        ;
#endif
  }

  g.json.cmd.v = cson_value_new_array();
  g.json.cmd.a = cson_value_get_array(g.json.cmd.v);
  json_gc_add( FossilJsonKeys.commandPath, g.json.cmd.v );
  /*
    The following if/else block translates the PATH_INFO path (in
    CLI/server modes) or g.argv (CLI mode) into an internal list so
    that we can simplify command dispatching later on.

    Note that translating g.argv this way is overkill but allows us to
    avoid CLI-only special-case handling in other code, e.g.
    json_command_arg().
  */
  if( zPath ){/* Either CGI or server mode... */
    /* Translate PATH_INFO into JSON array for later convenience. */
    json_string_split(zPath, '/', 1, g.json.cmd.a);
  }else{/* assume CLI mode */
    int i;
    char const * arg;
    cson_value * part;
    for(i = 1/*skip argv[0]*/; i < g.argc; ++i ){
      arg = g.argv[i];
      if( !arg || !*arg ){
        continue;
      }
      if('-' == *arg){
        /* workaround to skip CLI args so that
           json_command_arg() does not see them.
           This assumes that all arguments come LAST
           on the command line.
        */
        break;
      }
      part = cson_value_new_string(arg,strlen(arg));
      cson_array_append(g.json.cmd.a, part);
    }
  }

  while(!g.isHTTP){
    /* Simulate JSON POST data via input file.  Pedantic reminder:
       error handling does not honor user-supplied g.json.outOpt
       because outOpt cannot (generically) be configured until after
       POST-reading is finished.
    */
    FILE * inFile = NULL;
    char const * jfile = find_option("json-input",NULL,1);
    if(!jfile || !*jfile){
      break;
    }
    inFile = (0==strcmp("-",jfile))
      ? stdin
      : fossil_fopen(jfile,"rb");
    if(!inFile){
      g.json.resultCode = FSL_JSON_E_FILE_OPEN_FAILED;
      fossil_fatal("Could not open JSON file [%s].",jfile)
        /* Does not return. */
        ;
    }
    cgi_parse_POST_JSON(inFile, 0);
    if( stdin != inFile ){
      fclose(inFile);
    }
    break;
  }

  /* g.json.reqPayload exists only to simplify some of our access to
     the request payload. We currently only use this in the context of
     Object payloads, not Arrays, strings, etc.
  */
  g.json.reqPayload.v = cson_object_get( g.json.post.o, FossilJsonKeys.payload );
  if( g.json.reqPayload.v ){
    g.json.reqPayload.o = cson_value_get_object( g.json.reqPayload.v )
        /* g.json.reqPayload.o may legally be NULL, which means only that
           g.json.reqPayload.v is-not-a Object.
        */;
  }

  /* Anything which needs json_getenv() and friends should go after
     this point.
  */

  if(1 == cson_array_length_get(g.json.cmd.a)){
    /* special case: if we're at the top path, look for
       a "command" request arg which specifies which command
       to run.
    */
    char const * cmd = json_getenv_cstr("command");
    if(cmd){
      json_string_split(cmd, '/', 0, g.json.cmd.a);
      g.json.cmd.commandStr = cmd;
    }
  }


  if(!g.json.jsonp){
    g.json.jsonp = json_find_option_cstr("jsonp",NULL,NULL);
  }
  if(!g.isHTTP){
    g.json.errorDetailParanoia = 0 /*disable error code dumb-down for CLI mode*/;
  }

  {/* set up JSON output formatting options. */
    int indent = -1;
    indent = json_find_option_int("indent",NULL,"I",-1);
    g.json.outOpt.indentation = (0>indent)
      ? (g.isHTTP ? 0 : 1)
      : (unsigned char)indent;
    g.json.outOpt.addNewline = g.isHTTP
      ? 0
      : (g.json.jsonp ? 0 : 1);
  }

  if( g.isHTTP ){
    json_auth_token()/* will copy our auth token, if any, to fossil's
                        core, which we need before we call
                        login_check_credentials(). */;
    login_check_credentials()/* populates g.perm */;
  }
  else{
    /* FIXME: we need an option which allows us to skip this. At least
       one known command (/json/version) does not need an opened
       repo. The problem here is we cannot know which functions need
       it from here (because command dispatching hasn't yet happened)
       and all other commands rely on the repo being opened before
       they are called. A textbook example of lack of foresight :/.
     */
    db_find_and_open_repository(OPEN_ANY_SCHEMA,0);
  }
}

/*
** Returns the ndx'th item in the "command path", where index 0 is the
** position of the "json" part of the path. Returns NULL if ndx is out
** of bounds or there is no "json" path element.
**
** In CLI mode the "path" is the list of arguments (skipping argv[0]).
** In server/CGI modes the path is taken from PATH_INFO.
**
** The returned bytes are owned by g.json.cmd.v and _may_ be
** invalidated if that object is modified (depending on how it is
** modified).
**
** Note that CLI options are not included in the command path. Use
** find_option() to get those.
**
*/
char const * json_command_arg(unsigned short ndx){
  cson_array * ar = g.json.cmd.a;
  assert((NULL!=ar) && "Internal error. Was json_mode_bootstrap() called?");
  assert((g.argc>1) && "Internal error - we never should have gotten this far.");
  if( g.json.cmd.offset < 0 ){
    /* first-time setup. */
    short i = 0;
#define NEXT cson_string_cstr(          \
                 cson_value_get_string( \
                   cson_array_get(ar,i) \
                   ))
    char const * tok = NEXT;
    while( tok ){
      if( !g.isHTTP/*workaround for "abbreviated name" in CLI mode*/
          ? (0==strcmp(g.argv[1],tok))
          : (0==strncmp("json",tok,4))
          ){
        g.json.cmd.offset = i;
        break;
      }
      ++i;
      tok = NEXT;
    }
  }
#undef NEXT
  if(g.json.cmd.offset < 0){
    return NULL;
  }else{
    ndx = g.json.cmd.offset + ndx;
    return cson_string_cstr(cson_value_get_string(cson_array_get( ar, g.json.cmd.offset + ndx )));
  }
}

/* Returns the C-string form of json_auth_token(), or NULL
** if json_auth_token() returns NULL.
*/
char const * json_auth_token_cstr(){
  return cson_value_get_cstr( json_auth_token() );
}

/*
** Returns the JsonPageDef with the given name, or NULL if no match is
** found.
**
** head must be a pointer to an array of JsonPageDefs in which the
** last entry has a NULL name.
*/
JsonPageDef const * json_handler_for_name( char const * name, JsonPageDef const * head ){
  JsonPageDef const * pageDef = head;
  assert( head != NULL );
  if(name && *name) for( ; pageDef->name; ++pageDef ){
    if( 0 == strcmp(name, pageDef->name) ){
      return pageDef;
    }
  }
  return NULL;
}

/*
** Given a Fossil/JSON result code, this function "dumbs it down"
** according to the current value of g.json.errorDetailParanoia. The
** dumbed-down value is returned.
**
** This function assert()s that code is in the inclusive range 0 to
** 9999.
**
** Note that WARNING codes (1..999) are never dumbed down.
**
*/
static int json_dumbdown_rc( int code ){
  if(!g.json.errorDetailParanoia
     || !code
     || ((code>=FSL_JSON_W_START) && (code<FSL_JSON_W_END))){
    return code;
  }else{
    int modulo = 0;
    assert((code >= 1000) && (code <= 9999) && "Invalid Fossil/JSON code.");
    switch( g.json.errorDetailParanoia ){
      case 1: modulo = 10; break;
      case 2: modulo = 100; break;
      case 3: modulo = 1000; break;
      default: break;
    }
    if( modulo ) code = code - (code % modulo);
    return code;
  }
}

/*
** Convenience routine which converts a Julian time value into a Unix
** Epoch timestamp. Requires the db, so this cannot be used before the
** repo is opened (will trigger a fatal error in db_xxx()). The returned
** value is owned by the caller.
*/
cson_value * json_julian_to_timestamp(double j){
  return cson_value_new_integer((cson_int_t)
           db_int64(0,"SELECT cast(strftime('%%s',%lf) as int)",j)
                                );
}

/*
** Returns a timestamp value.
*/
cson_int_t json_timestamp(){
  return (cson_int_t)time(0);
}

/*
** Returns a new JSON value (owned by the caller) representing
** a timestamp. If timeVal is < 0 then time(0) is used to fetch
** the time, else timeVal is used as-is. The returned value is
** owned by the caller.
*/
cson_value * json_new_timestamp(cson_int_t timeVal){
  return cson_value_new_integer((timeVal<0) ? (cson_int_t)time(0) : timeVal);
}

/*
** Internal helper for json_create_response(). Appends the first
** g.json.dispatchDepth elements of g.json.cmd.a, skipping the first
** one (the "json" part), to a string and returns that string value
** (which is owned by the caller).
*/
static cson_value * json_response_command_path(){
  if(!g.json.cmd.a){
    return NULL;
  }else{
    cson_value * rc = NULL;
    Blob path = empty_blob;
    unsigned int aLen = g.json.dispatchDepth+1; /*cson_array_length_get(g.json.cmd.a);*/
    unsigned int i = 1;
    for( ; i < aLen; ++i ){
      char const * part = cson_string_cstr(cson_value_get_string(cson_array_get(g.json.cmd.a, i)));
      if(!part){
#if 1
          fossil_warning("Iterating further than expected in %s.",
                       __FILE__);
#endif
          break;
      }
      blob_appendf(&path,"%s%s", (i>1 ? "/": ""), part);
    }
    rc = json_new_string((blob_size(&path)>0)
                         ? blob_buffer(&path)
                         : "")
      /* reminder; we need an empty string instead of NULL
         in this case, to avoid what outwardly looks like
         (but is not) an allocation error in
         json_create_response().
      */
      ;
    blob_reset(&path);
    return rc;
  }
}

/*
** Returns a JSON Object representation of the global g object.
** Returned value is owned by the caller.
*/
cson_value * json_g_to_json(){
  cson_object * o = NULL;
  cson_object * pay = NULL;
  pay = o = cson_new_object();

#define INT(OBJ,K) cson_object_set(o, #K, json_new_int(OBJ.K))
#define CSTR(OBJ,K) cson_object_set(o, #K, OBJ.K ? json_new_string(OBJ.K) : cson_value_null())
#define VAL(K,V) cson_object_set(o, #K, (V) ? (V) : cson_value_null())
  VAL(capabilities, json_cap_value());
  INT(g, argc);
  INT(g, isConst);
  CSTR(g, zConfigDbName);
  INT(g, repositoryOpen);
  INT(g, localOpen);
  INT(g, minPrefix);
  INT(g, fSqlTrace);
  INT(g, fSqlStats);
  INT(g, fSqlPrint);
  INT(g, fQuiet);
  INT(g, fHttpTrace);
  INT(g, fSystemTrace);
  INT(g, fNoSync);
  INT(g, iErrPriority);
  INT(g, sslNotAvailable);
  INT(g, cgiOutput);
  INT(g, xferPanic);
  INT(g, fullHttpReply);
  INT(g, xlinkClusterOnly);
  INT(g, fTimeFormat);
  INT(g, markPrivate);
  INT(g, clockSkewSeen);
  INT(g, isHTTP);
  INT(g.url, isFile);
  INT(g.url, isHttps);
  INT(g.url, isSsh);
  INT(g.url, port);
  INT(g.url, dfltPort);
  INT(g, useLocalauth);
  INT(g, noPswd);
  INT(g, userUid);
  INT(g, rcvid);
  INT(g, okCsrf);
  INT(g, thTrace);
  INT(g, isHome);
  INT(g, nAux);
  INT(g, allowSymlinks);

  CSTR(g, zOpenRevision);
  CSTR(g, zLocalRoot);
  CSTR(g, zPath);
  CSTR(g, zExtra);
  CSTR(g, zBaseURL);
  CSTR(g, zTop);
  CSTR(g, zContentType);
  CSTR(g, zErrMsg);
  CSTR(g.url, name);
  CSTR(g.url, hostname);
  CSTR(g.url, protocol);
  CSTR(g.url, path);
  CSTR(g.url, user);
  CSTR(g.url, passwd);
  CSTR(g.url, canonical);
  CSTR(g.url, proxyAuth);
  CSTR(g.url, fossil);
  CSTR(g, zLogin);
  CSTR(g, zSSLIdentity);
  CSTR(g, zIpAddr);
  CSTR(g, zNonce);
  CSTR(g, zCsrfToken);

  o = cson_new_object();
  cson_object_set(pay, "json", cson_object_value(o) );
  INT(g.json, isJsonMode);
  INT(g.json, resultCode);
  INT(g.json, errorDetailParanoia);
  INT(g.json, dispatchDepth);
  VAL(authToken, g.json.authToken);
  CSTR(g.json, jsonp);
  VAL(gc, g.json.gc.v);
  VAL(cmd, g.json.cmd.v);
  VAL(param, g.json.param.v);
  VAL(POST, g.json.post.v);
  VAL(warnings, cson_array_value(g.json.warnings));
  /*cson_output_opt outOpt;*/


#undef INT
#undef CSTR
#undef VAL
  return cson_object_value(pay);
}


/*
** Creates a new Fossil/JSON response envelope skeleton.  It is owned
** by the caller, who must eventually free it using cson_value_free(),
** or add it to a cson container to transfer ownership. Returns NULL
** on error.
**
** If payload is not NULL and resultCode is 0 then it is set as the
** "payload" property of the returned object.  If resultCode is 0 then
** it defaults to g.json.resultCode. If resultCode is (or defaults to)
** non-zero and payload is not NULL then this function calls
** cson_value_free(payload) and does not insert the payload into the
** response. In either case, ownership of payload is transfered to (or
** shared with, if the caller holds a reference) this function.
**
** pMsg is an optional message string property (resultText) of the
** response. If resultCode is non-0 and pMsg is NULL then
** json_err_cstr() is used to get the error string. The caller may
** provide his own or may use an empty string to suppress the
** resultText property.
**
*/
static cson_value * json_create_response( int resultCode,
                                          char const * pMsg,
                                          cson_value * payload){
  cson_value * v = NULL;
  cson_value * tmp = NULL;
  cson_object * o = NULL;
  int rc;
  resultCode = json_dumbdown_rc(resultCode ? resultCode : g.json.resultCode);
  o = cson_new_object();
  v = cson_object_value(o);
  if( ! o ) return NULL;
#define SET(K) if(!tmp) goto cleanup; \
  rc = cson_object_set( o, K, tmp ); \
  if(rc) do{\
    cson_value_free(tmp); \
    tmp = NULL; \
    goto cleanup; \
  }while(0)


  tmp = json_new_string(MANIFEST_UUID);
  SET("fossil");

  tmp = json_new_timestamp(-1);
  SET(FossilJsonKeys.timestamp);

  if( 0 != resultCode ){
    if( ! pMsg ){
      pMsg = g.zErrMsg;
      if(!pMsg){
        pMsg = json_err_cstr(resultCode);
      }
    }
    tmp = json_new_string(json_rc_cstr(resultCode));
    SET(FossilJsonKeys.resultCode);
  }

  if( pMsg && *pMsg ){
    tmp = json_new_string(pMsg);
    SET(FossilJsonKeys.resultText);
  }

  if(g.json.cmd.commandStr){
    tmp = json_new_string(g.json.cmd.commandStr);
  }else{
    tmp = json_response_command_path();
  }
  SET("command");

  tmp = json_getenv(FossilJsonKeys.requestId);
  if( tmp ) cson_object_set( o, FossilJsonKeys.requestId, tmp );

  if(0){/* these are only intended for my own testing...*/
    if(g.json.cmd.v){
      tmp = g.json.cmd.v;
      SET("$commandPath");
    }
    if(g.json.param.v){
      tmp = g.json.param.v;
      SET("$params");
    }
    if(0){/*Only for debugging, add some info to the response.*/
      tmp = cson_value_new_integer( g.json.cmd.offset );
      cson_object_set( o, "cmd.offset", tmp );
      cson_object_set( o, "isCGI", cson_value_new_bool( g.isHTTP ) );
    }
  }

  if(fossil_timer_is_active(g.json.timerId)){
    /* This is, philosophically speaking, not quite the right place
       for ending the timer, but this is the one function which all of
       the JSON exit paths use (and they call it after processing,
       just before they end).
    */
    sqlite3_uint64 span = fossil_timer_stop(g.json.timerId);
    /* I'm actually seeing sub-uSec runtimes in some tests, but a time of
       0 is "just kinda wrong".
    */
    cson_object_set(o,"procTimeUs", cson_value_new_integer((cson_int_t)span));
    span /= 1000/*for milliseconds */;
    cson_object_set(o,"procTimeMs", cson_value_new_integer((cson_int_t)span));
    assert(!fossil_timer_is_active(g.json.timerId));
    g.json.timerId = -1;

  }
  if(g.json.warnings){
    tmp = cson_array_value(g.json.warnings);
    SET("warnings");
  }

  /* Only add the payload to SUCCESS responses. Else delete it. */
  if( NULL != payload ){
    if( resultCode ){
      cson_value_free(payload);
      payload = NULL;
    }else{
      tmp = payload;
      SET(FossilJsonKeys.payload);
    }
  }

  if(json_find_option_bool("debugFossilG","json-debug-g",NULL,0)
     &&(g.perm.Admin||g.perm.Setup)){
    tmp = json_g_to_json();
    SET("g");
  }

#undef SET
  goto ok;
  cleanup:
  cson_value_free(v);
  v = NULL;
  ok:
  return v;
}

/*
** Outputs a JSON error response to either the cgi_xxx() family of
** buffers (in CGI/server mode) or stdout (in CLI mode). If rc is 0
** then g.json.resultCode is used. If that is also 0 then the "Unknown
** Error" code is used.
**
** If g.isHTTP then the generated JSON error response object replaces
** any currently buffered page output. Because the output goes via
** the cgi_xxx() family of functions, this function inherits any
** compression which fossil does for its output.
**
** If alsoOutput is true AND g.isHTTP then cgi_reply() is called to
** flush the output (and headers). Generally only do this if you are
** about to call exit().
**
** If !g.isHTTP then alsoOutput is ignored and all output is sent to
** stdout immediately.
**
** For generating the resultText property: if msg is not NULL then it
** is used as-is. If it is NULL then g.zErrMsg is checked, and if that
** is NULL then json_err_cstr(code) is used.
*/
void json_err( int code, char const * msg, int alsoOutput ){
  int rc = code ? code : (g.json.resultCode
                          ? g.json.resultCode
                          : FSL_JSON_E_UNKNOWN);
  cson_value * resp = NULL;
  rc = json_dumbdown_rc(rc);
  if( rc && !msg ){
    msg = g.zErrMsg;
    if(!msg){
      msg = json_err_cstr(rc);
    }
  }
  resp = json_create_response(rc, msg, NULL);
  if(!resp){
    /* about the only error case here is out-of-memory. DO NOT
       call fossil_panic() here because that calls this function.
    */
    fprintf(stderr, "%s: Fatal error: could not allocate "
            "response object.\n", g.argv[0]);
    fossil_exit(1);
  }
  if( g.isHTTP ){
    if(alsoOutput){
      json_send_response(resp);
    }else{
      /* almost a duplicate of json_send_response() :( */
      cgi_reset_content();
      if( g.json.jsonp ){
        cgi_printf("%s(",g.json.jsonp);
      }
      cson_output( resp, cson_data_dest_cgi, NULL, &g.json.outOpt );
      if( g.json.jsonp ){
        cgi_append_content(")",1);
      }
    }
  }else{
    json_send_response(resp);
  }
  cson_value_free(resp);
}

/*
** Sets g.json.resultCode and g.zErrMsg, but does not report the error
** via json_err(). Returns the code passed to it.
**
** code must be in the inclusive range 1000..9999.
*/
int json_set_err( int code, char const * fmt, ... ){
  assert( (code>=1000) && (code<=9999) );
  free(g.zErrMsg);
  g.json.resultCode = code;
  if(!fmt || !*fmt){
    g.zErrMsg = mprintf("%s", json_err_cstr(code));
  }else{
    va_list vargs;
    char * msg;
    va_start(vargs,fmt);
    msg = vmprintf(fmt, vargs);
    va_end(vargs);
    g.zErrMsg = msg;
  }
  return code;
}

/*
** Iterates through a prepared SELECT statement and converts each row
** to a JSON object. If pTgt is not NULL then this function will
** append the results to pTgt and return cson_array_value(pTgt). If
** pTgt is NULL then a new Array object is created and returned (owned
** by the caller). Each row of pStmt is converted to an Object and
** appended to the array. If the result set has no rows AND pTgt is
** NULL then NULL (not an empty array) is returned.
*/
cson_value * json_stmt_to_array_of_obj(Stmt *pStmt,
                                       cson_array * pTgt){
  cson_array * a = pTgt;
  char const * warnMsg = NULL;
  cson_value * colNamesV = NULL;
  cson_array * colNames = NULL;
  while( (SQLITE_ROW==db_step(pStmt)) ){
    cson_value * row = NULL;
    if(!a){
      a = cson_new_array();
      assert(NULL!=a);
    }
    if(!colNames){
      colNamesV = cson_sqlite3_column_names(pStmt->pStmt);
      assert(NULL != colNamesV);
      /*Why? cson_value_add_reference(colNamesV) avoids an ownership problem*/;
      colNames = cson_value_get_array(colNamesV);
      assert(NULL != colNames);
    }
    row = cson_sqlite3_row_to_object2(pStmt->pStmt, colNames);
    if(!row && !warnMsg){
      warnMsg = "Could not convert at least one result row to JSON.";
      continue;
    }
    if( 0 != cson_array_append(a, row) ){
      cson_value_free(row);
      if(pTgt != a) {
        cson_free_array(a);
      }
      assert( 0 && "Alloc error.");
      return NULL;
    }
  }
  cson_value_free(colNamesV);
  if(warnMsg){
    json_warn( FSL_JSON_W_ROW_TO_JSON_FAILED, warnMsg );
  }
  return cson_array_value(a);
}

/*
** Works just like json_stmt_to_array_of_obj(), but each row in the
** result set is represented as an Array of values instead of an
** Object (key/value pairs). If pTgt is NULL and the statement
** has no results then NULL is returned, not an empty array.
*/
cson_value * json_stmt_to_array_of_array(Stmt *pStmt,
                                         cson_array * pTgt){
  cson_array * a = pTgt;
  while( (SQLITE_ROW==db_step(pStmt)) ){
    cson_value * row = NULL;
    if(!a){
      a = cson_new_array();
      assert(NULL!=a);
    }
    row = cson_sqlite3_row_to_array(pStmt->pStmt);
    cson_array_append(a, row);
  }
  return cson_array_value(a);
}

cson_value * json_stmt_to_array_of_values(Stmt *pStmt,
                                          int resultColumn,
                                          cson_array * pTgt){
  cson_array * a = pTgt;
  while( (SQLITE_ROW==db_step(pStmt)) ){
    cson_value * row = cson_sqlite3_column_to_value(pStmt->pStmt,
                                                    resultColumn);
    if(row){
      if(!a){
        a = cson_new_array();
        assert(NULL!=a);
      }
      cson_array_append(a, row);
    }
  }
  return cson_array_value(a);
}

/*
** Executes the given SQL and runs it through
** json_stmt_to_array_of_obj(), returning the result of that
** function. If resetBlob is true then blob_reset(pSql) is called
** after preparing the query.
**
** pTgt has the same semantics as described for
** json_stmt_to_array_of_obj().
**
** FIXME: change this to take a (char const *) instead of a blob,
** to simplify the trivial use-cases (which don't need a Blob).
*/
cson_value * json_sql_to_array_of_obj(Blob * pSql, cson_array * pTgt,
                                      int resetBlob){
  Stmt q = empty_Stmt;
  cson_value * pay = NULL;
  assert( blob_size(pSql) > 0 );
  db_prepare(&q, "%s", blob_str(pSql) /*safe-for-%s*/);
  if(resetBlob){
    blob_reset(pSql);
  }
  pay = json_stmt_to_array_of_obj(&q, pTgt);
  db_finalize(&q);
  return pay;

}

/*
** If the given COMMIT rid has any tags associated with it, this
** function returns a JSON Array containing the tag names (owned by
** the caller), else it returns NULL.
**
** See info_tags_of_checkin() for more details (this is simply a JSON
** wrapper for that function).
**
** If there are no tags then this function returns NULL, not an empty
** Array.
*/
cson_value * json_tags_for_checkin_rid(int rid, int propagatingOnly){
  cson_value * v = NULL;
  char * tags = info_tags_of_checkin(rid, propagatingOnly);
  if(tags){
    if(*tags){
      v = json_string_split2(tags,',',0);
    }
    free(tags);
  }
  return v;
}

/*
 ** Returns a "new" value representing the boolean value of zVal
 ** (false if zVal is NULL). Note that cson does not really allocate
 ** any memory for boolean values, but they "should" (for reasons of
 ** style and philosophy) be cleaned up like any other values (but
 ** it's a no-op for bools).
 */
cson_value * json_value_to_bool(cson_value const * zVal){
  return cson_value_get_bool(zVal)
    ? cson_value_true()
    : cson_value_false();
}

/*
** Impl of /json/resultCodes
**
*/
cson_value * json_page_resultCodes(){
    cson_array * list = cson_new_array();
    cson_object * obj = NULL;
    cson_string * kRC;
    cson_string * kSymbol;
    cson_string * kNumber;
    cson_string * kDesc;
    cson_array_reserve( list, 35 );
    kRC = cson_new_string("resultCode",10);
    kSymbol = cson_new_string("cSymbol",7);
    kNumber = cson_new_string("number",6);
    kDesc = cson_new_string("description",11);
#define C(K) obj = cson_new_object(); \
    cson_object_set_s(obj, kRC, json_new_string(json_rc_cstr(FSL_JSON_E_##K)) ); \
    cson_object_set_s(obj, kSymbol, json_new_string("FSL_JSON_E_"#K) );             \
    cson_object_set_s(obj, kNumber, cson_value_new_integer(FSL_JSON_E_##K) );        \
    cson_object_set_s(obj, kDesc, json_new_string(json_err_cstr(FSL_JSON_E_##K))); \
    cson_array_append( list, cson_object_value(obj) ); obj = NULL;

    C(GENERIC);
    C(INVALID_REQUEST);
    C(UNKNOWN_COMMAND);
    C(UNKNOWN);
    C(TIMEOUT);
    C(ASSERT);
    C(ALLOC);
    C(NYI);
    C(PANIC);
    C(MANIFEST_READ_FAILED);
    C(FILE_OPEN_FAILED);

    C(AUTH);
    C(MISSING_AUTH);
    C(DENIED);
    C(WRONG_MODE);
    C(LOGIN_FAILED);
    C(LOGIN_FAILED_NOSEED);
    C(LOGIN_FAILED_NONAME);
    C(LOGIN_FAILED_NOPW);
    C(LOGIN_FAILED_NOTFOUND);

    C(USAGE);
    C(INVALID_ARGS);
    C(MISSING_ARGS);
    C(AMBIGUOUS_UUID);
    C(UNRESOLVED_UUID);
    C(RESOURCE_ALREADY_EXISTS);
    C(RESOURCE_NOT_FOUND);

    C(DB);
    C(STMT_PREP);
    C(STMT_BIND);
    C(STMT_EXEC);
    C(DB_LOCKED);
    C(DB_NEEDS_REBUILD);
    C(DB_NOT_FOUND);
    C(DB_NOT_VALID);
#undef C
    return cson_array_value(list);
}


/*
** /json/version implementation.
**
** Returns the payload object (owned by the caller).
*/
cson_value * json_page_version(){
  cson_value * jval = NULL;
  cson_object * jobj = NULL;
  jval = cson_value_new_object();
  jobj = cson_value_get_object(jval);
#define FSET(X,K) cson_object_set( jobj, K, cson_value_new_string(X,strlen(X)))
  FSET(MANIFEST_UUID,"manifestUuid");
  FSET(MANIFEST_VERSION,"manifestVersion");
  FSET(MANIFEST_DATE,"manifestDate");
  FSET(MANIFEST_YEAR,"manifestYear");
  FSET(RELEASE_VERSION,"releaseVersion");
  cson_object_set( jobj, "releaseVersionNumber",
                   cson_value_new_integer(RELEASE_VERSION_NUMBER) );
  cson_object_set( jobj, "resultCodeParanoiaLevel",
                   cson_value_new_integer(g.json.errorDetailParanoia) );
  FSET(FOSSIL_JSON_API_VERSION, "jsonApiVersion" );
#undef FSET
  return jval;
}


/*
** Returns the current user's capabilities string as a String value.
** Returned value is owned by the caller, and will only be NULL if
** g.userUid is invalid or an out of memory error. Or, it turns out,
** in CLI mode (where there is no logged-in user).
*/
cson_value * json_cap_value(){
  if(g.userUid<=0){
    return NULL;
  }else{
    Stmt q = empty_Stmt;
    cson_value * val = NULL;
    db_prepare(&q, "SELECT cap FROM user WHERE uid=%d", g.userUid);
    if( db_step(&q)==SQLITE_ROW ){
      char const * str = (char const *)sqlite3_column_text(q.pStmt,0);
      if( str ){
        val = json_new_string(str);
      }
    }
    db_finalize(&q);
    return val;
  }
}

/*
** Implementation for /json/cap
**
** Returned object contains details about the "capabilities" of the
** current user (what he may/may not do).
**
** This is primarily intended for debuggering, but may have
** a use in client code. (?)
*/
cson_value * json_page_cap(){
  cson_value * payload = cson_value_new_object();
  cson_value * sub = cson_value_new_object();
  Stmt q;
  cson_object * obj = cson_value_get_object(payload);
  db_prepare(&q, "SELECT login, cap FROM user WHERE uid=%d", g.userUid);
  if( db_step(&q)==SQLITE_ROW ){
    /* reminder: we don't use g.zLogin because it's 0 for the guest
       user and the HTML UI appears to currently allow the name to be
       changed (but doing so would break other code). */
    char const * str = (char const *)sqlite3_column_text(q.pStmt,0);
    if( str ){
      cson_object_set( obj, "name",
                       cson_value_new_string(str,strlen(str)) );
    }
    str = (char const *)sqlite3_column_text(q.pStmt,1);
    if( str ){
      cson_object_set( obj, "capabilities",
                       cson_value_new_string(str,strlen(str)) );
    }
  }
  db_finalize(&q);
  cson_object_set( obj, "permissionFlags", sub );
  obj = cson_value_get_object(sub);

#define ADD(X,K) cson_object_set(obj, K, cson_value_new_bool(g.perm.X))
  ADD(Setup,"setup");
  ADD(Admin,"admin");
  ADD(Delete,"delete");
  ADD(Password,"password");
  ADD(Query,"query"); /* don't think this one is actually used */
  ADD(Write,"checkin");
  ADD(Read,"checkout");
  ADD(Hyperlink,"history");
  ADD(Clone,"clone");
  ADD(RdWiki,"readWiki");
  ADD(NewWiki,"createWiki");
  ADD(ApndWiki,"appendWiki");
  ADD(WrWiki,"editWiki");
  ADD(ModWiki,"moderateWiki");
  ADD(RdTkt,"readTicket");
  ADD(NewTkt,"createTicket");
  ADD(ApndTkt,"appendTicket");
  ADD(WrTkt,"editTicket");
  ADD(ModTkt,"moderateTicket");
  ADD(Attach,"attachFile");
  ADD(TktFmt,"createTicketReport");
  ADD(RdAddr,"readPrivate");
  ADD(Zip,"zip");
  ADD(Private,"xferPrivate");
#undef ADD
  return payload;
}

/*
** Implementation of the /json/stat page/command.
**
*/
cson_value * json_page_stat(){
  i64 t, fsize;
  int n, m;
  int full;
  enum { BufLen = 1000 };
  char zBuf[BufLen];
  cson_value * jv = NULL;
  cson_object * jo = NULL;
  cson_value * jv2 = NULL;
  cson_object * jo2 = NULL;
  char * zTmp = NULL;
  if( !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'o' permissions.");
    return NULL;
  }
  full = json_find_option_bool("full",NULL,"f",
              json_find_option_bool("verbose",NULL,"v",0));
#define SETBUF(O,K) cson_object_set(O, K, cson_value_new_string(zBuf, strlen(zBuf)));

  jv = cson_value_new_object();
  jo = cson_value_get_object(jv);

  zTmp = db_get("project-name",NULL);
  cson_object_set(jo, "projectName", json_new_string(zTmp));
  free(zTmp);
  zTmp = db_get("project-description",NULL);
  cson_object_set(jo, "projectDescription", json_new_string(zTmp));
  free(zTmp);
  zTmp = NULL;
  fsize = file_size(g.zRepositoryName);
  cson_object_set(jo, "repositorySize", cson_value_new_integer((cson_int_t)fsize));

  if(full){
    n = db_int(0, "SELECT count(*) FROM blob");
    m = db_int(0, "SELECT count(*) FROM delta");
    cson_object_set(jo, "blobCount", cson_value_new_integer((cson_int_t)n));
    cson_object_set(jo, "deltaCount", cson_value_new_integer((cson_int_t)m));
    if( n>0 ){
      int a, b;
      Stmt q;
      db_prepare(&q, "SELECT total(size), avg(size), max(size)"
                 " FROM blob WHERE size>0");
      db_step(&q);
      t = db_column_int64(&q, 0);
      cson_object_set(jo, "uncompressedArtifactSize",
                      cson_value_new_integer((cson_int_t)t));
      cson_object_set(jo, "averageArtifactSize",
                      cson_value_new_integer((cson_int_t)db_column_int(&q, 1)));
      cson_object_set(jo, "maxArtifactSize",
                      cson_value_new_integer((cson_int_t)db_column_int(&q, 2)));
      db_finalize(&q);
      if( t/fsize < 5 ){
        b = 10;
        fsize /= 10;
      }else{
        b = 1;
      }
      a = t/fsize;
      sqlite3_snprintf(BufLen,zBuf, "%d:%d", a, b);
      SETBUF(jo, "compressionRatio");
    }
    n = db_int(0, "SELECT count(distinct mid) FROM mlink /*scan*/");
    cson_object_set(jo, "checkinCount", cson_value_new_integer((cson_int_t)n));
    n = db_int(0, "SELECT count(*) FROM filename /*scan*/");
    cson_object_set(jo, "fileCount", cson_value_new_integer((cson_int_t)n));
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
               " WHERE +tagname GLOB 'wiki-*'");
    cson_object_set(jo, "wikiPageCount", cson_value_new_integer((cson_int_t)n));
    n = db_int(0, "SELECT count(*) FROM tag  /*scan*/"
               " WHERE +tagname GLOB 'tkt-*'");
    cson_object_set(jo, "ticketCount", cson_value_new_integer((cson_int_t)n));
  }/*full*/
  n = db_int(0, "SELECT julianday('now') - (SELECT min(mtime) FROM event)"
                " + 0.99");
  cson_object_set(jo, "ageDays", cson_value_new_integer((cson_int_t)n));
  cson_object_set(jo, "ageYears", cson_value_new_double(n/365.2425));
  sqlite3_snprintf(BufLen, zBuf, db_get("project-code",""));
  SETBUF(jo, "projectCode");
  cson_object_set(jo, "compiler", cson_value_new_string(COMPILER_NAME, strlen(COMPILER_NAME)));

  jv2 = cson_value_new_object();
  jo2 = cson_value_get_object(jv2);
  cson_object_set(jo, "sqlite", jv2);
  sqlite3_snprintf(BufLen, zBuf, "%.19s [%.10s] (%s)",
                   sqlite3_sourceid(), &sqlite3_sourceid()[20], sqlite3_libversion());
  SETBUF(jo2, "version");
  cson_object_set(jo2, "pageCount", cson_value_new_integer((cson_int_t)db_int(0, "PRAGMA repository.page_count")));
  cson_object_set(jo2, "pageSize", cson_value_new_integer((cson_int_t)db_int(0, "PRAGMA repository.page_size")));
  cson_object_set(jo2, "freeList", cson_value_new_integer((cson_int_t)db_int(0, "PRAGMA repository.freelist_count")));
  sqlite3_snprintf(BufLen, zBuf, "%s", db_text(0, "PRAGMA repository.encoding"));
  SETBUF(jo2, "encoding");
  sqlite3_snprintf(BufLen, zBuf, "%s", db_text(0, "PRAGMA repository.journal_mode"));
  cson_object_set(jo2, "journalMode", *zBuf ? cson_value_new_string(zBuf, strlen(zBuf)) : cson_value_null());
  return jv;
#undef SETBUF
}




/*
** Creates a comma-separated list of command names
** taken from zPages. zPages must be an array of objects
** whose final entry MUST have a NULL name value or results
** are undefined.
**
** The list is appended to pOut. The number of items (not bytes)
** appended are returned. If filterByMode is non-0 then the result
** list will contain only commands which are able to run in the
** current run mode (CLI vs. HTTP).
*/
static int json_pagedefs_to_string(JsonPageDef const * zPages,
                                   Blob * pOut, int filterByMode){
  int i = 0;
  for( ; zPages->name; ++zPages, ++i ){
    if(filterByMode){
      if(g.isHTTP && zPages->runMode < 0) continue;
      else if(zPages->runMode > 0) continue;
    }
    blob_append(pOut, zPages->name, -1);
    if((zPages+1)->name){
      blob_append(pOut, ", ",2);
    }
  }
  return i;
}

/*
** Creates an error message using zErrPrefix and the given array of
** JSON command definitions, and sets the g.json error state to
** reflect FSL_JSON_E_MISSING_ARGS. If zErrPrefix is NULL then
** some default is used (e.g. "Try one of: "). If it is "" then
** no prefix is used.
**
** The intention is to provide the user (via the response.resultText)
** a list of available commands/subcommands.
**
*/
void json_dispatch_missing_args_err( JsonPageDef const * pCommands,
                                     char const * zErrPrefix ){
  Blob cmdNames = empty_blob;
  blob_init(&cmdNames,NULL,0);
  if( !zErrPrefix ) {
    zErrPrefix = "Try one of: ";
  }
  blob_append( &cmdNames, zErrPrefix, strlen(zErrPrefix) );
  json_pagedefs_to_string(pCommands, &cmdNames, 1);
  json_set_err(FSL_JSON_E_MISSING_ARGS, "%s",
               blob_str(&cmdNames));
  blob_reset(&cmdNames);
}

cson_value * json_page_dispatch_helper(JsonPageDef const * pages){
  JsonPageDef const * def;
  char const * cmd = json_command_arg(1+g.json.dispatchDepth);
  assert( NULL != pages );
  if( ! cmd ){
    json_dispatch_missing_args_err(pages,
                                   "No subcommand specified. "
                                   "Try one of: ");
    return NULL;
  }
  def = json_handler_for_name( cmd, pages );
  if(!def){
    json_set_err(FSL_JSON_E_UNKNOWN_COMMAND,
                 "Unknown subcommand: %s", cmd);
    return NULL;
  }
  else{
    ++g.json.dispatchDepth;
    return (*def->func)();
  }
}


/*
** Impl of /json/rebuild. Requires admin privileges.
*/
static cson_value * json_page_rebuild(){
  if( !g.perm.Admin ){
    json_set_err(FSL_JSON_E_DENIED,"Requires 'a' privileges.");
    return NULL;
  }else{
  /* Reminder: the db_xxx() ops "should" fail via the fossil core
     error handlers, which will cause a JSON error and exit(). i.e. we
     don't handle the errors here. TODO: confirm that all these db
     routine fail gracefully in JSON mode.

     On large repos (e.g. fossil's) this operation is likely to take
     longer than the client timeout, which will cause it to fail (but
     it's sqlite3, so it'll fail gracefully).
  */
    db_close(1);
    db_open_repository(g.zRepositoryName);
    db_begin_transaction();
    rebuild_db(0, 0, 0);
    db_end_transaction(0);
    return NULL;
  }
}

/*
** Impl of /json/g. Requires admin/setup rights.
*/
static cson_value * json_page_g(){
  if(!g.perm.Admin || !g.perm.Setup){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'a' or 's' privileges.");
    return NULL;
  }
  return json_g_to_json();
}

/* Impl in json_login.c. */
cson_value * json_page_anon_password();
/* Impl in json_artifact.c. */
cson_value * json_page_artifact();
/* Impl in json_branch.c. */
cson_value * json_page_branch();
/* Impl in json_diff.c. */
cson_value * json_page_diff();
/* Impl in json_dir.c. */
cson_value * json_page_dir();
/* Impl in json_login.c. */
cson_value * json_page_login();
/* Impl in json_login.c. */
cson_value * json_page_logout();
/* Impl in json_query.c. */
cson_value * json_page_query();
/* Impl in json_report.c. */
cson_value * json_page_report();
/* Impl in json_tag.c. */
cson_value * json_page_tag();
/* Impl in json_user.c. */
cson_value * json_page_user();
/* Impl in json_config.c. */
cson_value * json_page_config();
/* Impl in json_finfo.c. */
cson_value * json_page_finfo();
/* Impl in json_status.c. */
cson_value * json_page_status();

/*
** Mapping of names to JSON pages/commands.  Each name is a subpath of
** /json (in CGI mode) or a subcommand of the json command in CLI mode
*/
static const JsonPageDef JsonPageDefs[] = {
/* please keep alphabetically sorted (case-insensitive) for maintenance reasons. */
{"anonymousPassword", json_page_anon_password, 0},
{"artifact", json_page_artifact, 0},
{"branch", json_page_branch,0},
{"cap", json_page_cap, 0},
{"config", json_page_config, 0 },
{"diff", json_page_diff, 0},
{"dir", json_page_dir, 0},
{"finfo", json_page_finfo, 0},
{"g", json_page_g, 0},
{"HAI",json_page_version,0},
{"login",json_page_login,0},
{"logout",json_page_logout,0},
{"query",json_page_query,0},
{"rebuild",json_page_rebuild,0},
{"report", json_page_report, 0},
{"resultCodes", json_page_resultCodes,0},
{"stat",json_page_stat,0},
{"status", json_page_status, 0},
{"tag", json_page_tag,0},
/*{"ticket", json_page_nyi,0},*/
{"timeline", json_page_timeline,0},
{"user",json_page_user,0},
{"version",json_page_version,0},
{"whoami",json_page_whoami,0},
{"wiki",json_page_wiki,0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** Internal helper for json_cmd_top() and json_page_top().
**
** Searches JsonPageDefs for a command with the given name. If found,
** it is used to generate and output a JSON response. If not found, it
** generates a JSON-style error response. Returns 0 on success, non-0
** on error. On error it will set g.json's error state.
*/
static int json_dispatch_root_command( char const * zCommand ){
  int rc = 0;
  cson_value * payload = NULL;
  JsonPageDef const * pageDef = NULL;
  pageDef = json_handler_for_name(zCommand,&JsonPageDefs[0]);
  if( ! pageDef ){
    rc = FSL_JSON_E_UNKNOWN_COMMAND;
    json_set_err( rc, "Unknown command: %s", zCommand );
  }else if( pageDef->runMode < 0 /*CLI only*/){
    rc = FSL_JSON_E_WRONG_MODE;
  }else if( (g.isHTTP && (pageDef->runMode < 0 /*CLI only*/))
            ||
            (!g.isHTTP && (pageDef->runMode > 0 /*HTTP only*/))
            ){
    rc = FSL_JSON_E_WRONG_MODE;
  }
  else{
    rc = 0;
    g.json.dispatchDepth = 1;
    payload = (*pageDef->func)();
  }
  payload = json_create_response(rc, NULL, payload);
  json_send_response(payload);
  cson_value_free(payload);
  return rc;
}

#ifdef FOSSIL_ENABLE_JSON
/* dupe ifdef needed for mkindex */
/*
** WEBPAGE: json
**
** Pages under /json/... must be entered into JsonPageDefs.
** This function dispatches them, and is the HTTP equivalent of
** json_cmd_top().
*/
void json_page_top(void){
  char const * zCommand;
  assert(g.json.gc.a && "json_main_bootstrap() was not called!");
  json_mode_bootstrap();
  zCommand = json_command_arg(1);
  if(!zCommand || !*zCommand){
    json_dispatch_missing_args_err( JsonPageDefs,
                                    "No command (sub-path) specified."
                                    " Try one of: ");
    return;
  }
  json_dispatch_root_command( zCommand );
}
#endif /* FOSSIL_ENABLE_JSON for mkindex */

#ifdef FOSSIL_ENABLE_JSON
/* dupe ifdef needed for mkindex */
/*
** This function dispatches json commands and is the CLI equivalent of
** json_page_top().
**
** COMMAND: json
**
** Usage: %fossil json SUBCOMMAND ?OPTIONS?
**
** In CLI mode, the -R REPO common option is supported. Due to limitations
** in the argument dispatching code, any -FLAGS must come after the final
** sub- (or subsub-) command.
**
** The -json-input FILE option can be used to read JSON data and process
** it like the HTTP interface would. For example:
**
**   %fossil json -json-input my.json
**
** The commands include:
**
**   anonymousPassword
**   artifact
**   branch
**   cap
**   config
**   diff
**   dir
**   g
**   login
**   logout
**   query
**   rebuild
**   report
**   resultCodes
**   stat
**   tag
**   timeline
**   user
**   version (alias: HAI)
**   whoami
**   wiki
**
** Run '%fossil json' without any subcommand to see the full list (but be
** aware that some listed might not yet be fully implemented).
**
*/
void json_cmd_top(void){
  char const * cmd = NULL;
  int rc = 0;
  memset( &g.perm, 0xff, sizeof(g.perm) )
    /* In CLI mode fossil does not use permissions
       and they all default to false. We enable them
       here because (A) fossil doesn't use them in local
       mode but (B) having them set gives us one less
       difference in the CLI/CGI/Server-mode JSON
       handling.
    */
    ;
  json_main_bootstrap();
  json_mode_bootstrap();
  if( 2 > cson_array_length_get(g.json.cmd.a) ){
    goto usage;
  }
#if 0
  json_warn(FSL_JSON_W_ROW_TO_JSON_FAILED, "Just testing.");
  json_warn(FSL_JSON_W_ROW_TO_JSON_FAILED, "Just testing again.");
#endif
  cmd = json_command_arg(1);
  if( !cmd || !*cmd ){
    goto usage;
  }
  rc = json_dispatch_root_command( cmd );
  if(0 != rc){
    /* FIXME: we need a way of passing this error back
       up to the routine which called this callback.
       e.g. add g.errCode.
    */
    fossil_exit(1);
  }
  return;
  usage:
  {
    cson_value * payload;
    json_dispatch_missing_args_err( JsonPageDefs,
                                    "No subcommand specified."
                                    " Try one of: ");
    payload = json_create_response(0, NULL, NULL);
    json_send_response(payload);
    cson_value_free(payload);
    fossil_exit(1);
  }
}
#endif /* FOSSIL_ENABLE_JSON for mkindex */

#endif /* FOSSIL_ENABLE_JSON */
