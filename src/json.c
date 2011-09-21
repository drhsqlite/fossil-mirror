/*
** Copyright (c) 2007 D. Richard Hipp
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
** https://docs.google.com/document/d/1fXViveNhDbiXgCuE7QDXQOKeFzf2qNUkBEgiUvoqFN4/edit
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
#include "config.h"
#include "VERSION.h"
#include "json.h"
#include <assert.h>
#include <time.h>

#if INTERFACE
#include "cson_amalgamation.h"
#include "json_detail.h" /* workaround for apparent enum limitation in makeheaders */
#endif

/*
** Signature for JSON page/command callbacks. Each callback is
** responsible for handling one JSON request/command and/or
** dispatching to sub-commands.
**
** By the time the callback is called, json_page_top() (HTTP mode) or
** json_cmd_top() (CLI mode) will have set up the JSON-related
** environment. Implementations may generate a "result payload" of any
** JSON type by returning its value from this function (ownership is
** tranferred to the caller). On error they should set
** g.json.resultCode to one of the FossilJsonCodes values and return
** either their payload object or NULL. Note that NULL is a legal
** success value - it simply means the response will contain no
** payload. If g.json.resultCode is non-zero when this function
** returns then the top-level dispatcher will destroy any payload
** returned by this function and will output a JSON error response
** instead.
**
** The depth parameter comes from the dispatcher and tells the
** callback what index in json_command_arg() its command/path starts.
** e.g. when dispatching /json/foo/bar, the foo callback will get a
** depth of 1 and the bar callback will get a depth of 2. This is only
** useful for callbacks which use json_command_arg(), and the only
** callbacks which do that are ones which dispatch to sub-pages
** (e.g. /json/wiki/...).  This is a parameter, as opposed to simply
** hard-coding the offset in each callback impl, so that refactoring
** will (or should) be easier if we later reimplement the path/command
** handling and arguments/path parts get moved around.
**
** All of the setup/response code is handled by the top dispatcher
** functions and the callbacks concern themselves only with generating
** the payload.
**
** It is imperitive that NO callback functions EVER output ANYTHING to
** stdout, as that will effectively corrupt any JSON output, and
** almost certainly will corrupt any HTTP response headers. Output
** sent to stderr ends up in my apache log, so that might be useful
** for debuggering in some cases, but so such code should be left
** enabled for non-debuggering builds.
*/
typedef cson_value * (*fossil_json_f)(unsigned int depth);

/*
** Placeholder /json/XXX page impl for NYI (Not Yet Implemented)
** (but planned) pages/commands.
*/
static cson_value * json_page_nyi(unsigned int depth){
  g.json.resultCode = FSL_JSON_E_NYI;
  return NULL;
}

/*
** Holds keys used for various JSON API properties.
*/
static const struct FossilJsonKeys_{
  /** maintainers: please keep alpha sorted (case-insensitive) */
  char const * commandPath;
  char const * anonymousSeed;
  char const * authToken;
  char const * payload;
  char const * requestId;
  char const * resultCode;
  char const * resultText;
  char const * timestamp;
} FossilJsonKeys = {
  "COMMAND_PATH" /*commandPath*/,
  "anonymousSeed" /*anonymousSeed*/,
  "authToken"  /*authToken*/,
  "payload" /* payload */,
  "requestId" /*requestId*/,
  "resultCode" /*resultCode*/,
  "resultText" /*resultText*/,
  "timestamp" /*timestamp*/
};

/*
** Given a FossilJsonCodes value, it returns a string suitable for use
** as a resultText string. Returns some unspecified string if errCode
** is not one of the FossilJsonCodes values.
*/
char const * json_err_str( int errCode ){
  switch( errCode ){
    case 0: return "Success";
#define C(X,V) case FSL_JSON_E_ ## X: return V

    C(GENERIC,"Generic error");
    C(INVALID_REQUEST,"Invalid request");
    C(UNKNOWN_COMMAND,"Unknown Command");
    C(UNKNOWN,"Unknown error");
    C(RESOURCE_NOT_FOUND,"Resource not found");
    C(TIMEOUT,"Timeout reached");
    C(ASSERT,"Assertion failed");
    C(ALLOC,"Resource allocation failed");
    C(NYI,"Not yet implemented");
    C(AUTH,"Authentication error");
    C(LOGIN_FAILED,"Login failed");
    C(LOGIN_FAILED_NOSEED,"Anonymous login attempt was missing password seed");
    C(LOGIN_FAILED_NONAME,"Login failed - name not supplied");
    C(LOGIN_FAILED_NOPW,"Login failed - password not supplied");
    C(LOGIN_FAILED_NOTFOUND,"Login failed - no match found");
    C(MISSING_AUTH,"Authentication info missing from request");
    C(DENIED,"Access denied");
    C(WRONG_MODE,"Request not allowed (wrong operation mode)");

    C(USAGE,"Usage error");
    C(INVALID_ARGS,"Invalid arguments");
    C(MISSING_ARGS,"Missing arguments");

    C(DB,"Database error");
    C(STMT_PREP,"Statement preparation failed");
    C(STMT_BIND,"Statement parameter binding failed");
    C(STMT_EXEC,"Statement execution/stepping failed");
    C(DB_LOCKED,"Database is locked");
    C(DB_NEEDS_REBUILD,"Fossil repository needs to be rebuilt");
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
** Implements the cson_data_source_f() interface and reads
** input from a fossil Blob object. pState must be-a (Blob*).
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
** On success a new JSON Object or Array is returned. On error NULL is
** returned.
*/
cson_value * cson_parse_Blob( Blob * pSrc, cson_parse_info * pInfo ){
  cson_value * root = NULL;
  blob_rewind( pSrc );
  cson_parse( &root, cson_data_src_Blob, pSrc, NULL, pInfo );
  return root;
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
** Adds v to the API-internal cleanup mechanism. key must be a unique
** key for the given element. Adding another item with that key may
** free the previous one (depending on its reference count). If
** freeOnError is true then v is passed to cson_value_free() if the
** key cannot be inserted, otherweise ownership of v is not changed on
** error. Failure to insert a key may be caused by any of the
** following:
**
** - Allocation error.
** - g.json.gc.o is NULL
** - key is NULL or empty.
**
** Returns 0 on success.
**
** On success, ownership of v is transfered to (or shared with)
** g.json.gc, and v will be valid until that object is cleaned up or
** its key is replaced via another call to this function.
*/
int json_gc_add( char const * key, cson_value * v, char freeOnError ){
  int const rc = cson_object_set( g.json.gc.o, key, v );
  assert( NULL != g.json.gc.o );
  if( (0 != rc) && freeOnError ){
    cson_value_free( v );
  }
  return rc;
}


/*
** Returns the value of json_rc_cstr(code) as a new JSON
** string, which is owned by the caller and must eventually
** be cson_value_free()d or transfered to a JSON container.
*/
cson_value * json_rc_string( int code ){
  return cson_value_new_string( json_rc_cstr(code), 11 );
}


/*
** Gets a POST/GET/COOKIE/ENV value. The returned memory is owned by the
** g.json object (one of its sub-objects). Returns NULL if no match is
** found.
**
** ENV means the system environment (getenv()).
**
** Precedence: GET, COOKIE, POST, ENV. COOKIE _should_ be after POST
** but currently is not for internal order-of-init reasons. Since
** fossil only uses one cookie, this is not a real/high-priority
** problem.
*/
cson_value * json_getenv( char const * zKey ){
  cson_value * rc;
  rc = cson_object_get( g.json.param.o, zKey );
  if( rc ){
    return rc;
  }else{
    rc = cson_object_get( g.json.post.o, zKey );
    if(rc){
      return rc;
    }else{
      char const * cv = PD(zKey,NULL);
      if(!cv){
        cv = getenv(zKey);
      }
      if(cv){/*transform it to JSON for later use.*/
        rc = cson_value_new_string(cv,strlen(cv));
        cson_object_set( g.json.param.o, zKey, rc );
        return rc;
      }
    }
  }
  return NULL;
}


/*
** Returns the string form of a json_getenv() value, but ONLY If that
** value is-a String. Non-strings are not converted to strings for
** this purpose. Returned memory is owned by g.json or fossil and is
** valid until end-of-app or the given key is replaced in fossil's
** internals via cgi_replace_parameter() and friends or json_setenv().
*/
static char const * json_getenv_cstr( char const * zKey ){
  return cson_value_get_cstr( json_getenv(zKey) );
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
** Returned memory is static and immutable.
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
static cson_value * json_auth_token(){
  if( !g.json.authToken ){
    /* Try to get an authorization token from GET parameter, POSTed
       JSON, or fossil cookie (in that order). */
    g.json.authToken = json_getenv(FossilJsonKeys.authToken);
    if(g.json.authToken
       && cson_value_is_string(g.json.authToken)
       && !PD(login_cookie_name(),NULL)){
      /* tell fossil to use this login info.

      FIXME?: because the JSON bits don't carry around
      login_cookie_name(), there is a potential login hijacking
      window here. We may need to change the JSON auth token to be
      in the form: login_cookie_name()=...

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
        if(0 == json_gc_add( FossilJsonKeys.authToken, v, 1 )){
          g.json.authToken = v;
        }
      }
    }
  }
  return g.json.authToken;
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
  assert( (NULL == g.json.gc.v) && "cgi_json_bootstrap() was called twice!" );

  /* g.json.gc is our "garbage collector" - where we put JSON values
     which need a long lifetime but don't have a logical parent to put
     them in.
  */
  v = cson_value_new_object();
  g.json.gc.v = v;
  g.json.gc.o = cson_value_get_object(v);

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
  json_gc_add("$PARAMS", v, 1);
}


/*
** Splits zStr (which must not be NULL) into tokens separated by the
** given separator character. If doDeHttp is true then each element
** will be passed through dehttpize(), otherwise they are used
** as-is. Each new element is appended to the given target array
** object, which must not be NULL and ownership of it is not changed
** by this call.
**
** On success, returns the number of items appended to target. On
** error a NEGATIVE number is returned - its absolute value is the
** number of items inserted before the error occurred. There is a
** corner case here if we fail on the 1st element, which will cause 0
** to be returned, which the client cannot immediately distinguish as
** success or error.
**
** Achtung: leading whitespace of elements is NOT elided. We should
** add an option to do that, but don't have one yet.
**
** Achtung: empty elements will be skipped, meaning consecutive
** empty elements are collapsed.
*/
int json_string_split( char const * zStr,
                       char separator,
                       char doDeHttp,
                       cson_array * target ){
  char const * p = zStr /* current byte */;
  char const * head = p  /* current start-of-token */;
  unsigned int len = 0   /* current token's length */;
  int rc = 0   /* return code (number of added elements)*/;
  assert( zStr && target );
  for( ; ; ++p){
    if( !*p || (separator == *p) ){
      if( len ){/* append head..(head+len) as next array
                   element. */
        cson_value * part = NULL;
        char * zPart = NULL;
        assert( head != p );
        zPart = (char*)malloc(len+1);
        assert( (zPart != NULL) && "malloc failure" );
        memcpy(zPart, head, len);
        zPart[len] = 0;
        if(doDeHttp){
          dehttpize(zPart);
        }
        if( *zPart ){ /* should only fail if someone manages to url-encoded a NUL byte */
          part = cson_value_new_string(zPart, strlen(zPart));
          if( 0 == cson_array_append( target, part ) ){
            ++rc;
          }else{
            cson_value_free(part);
            rc = rc ? -rc : 0;
            break;
          }
        }else{
          assert(0 && "i didn't think this was possible!");
        }
        free(zPart);
        len = 0;
      }
      if( !*p ){
        break;
      }
      head = p+1;
      continue;
    }
    ++len;
  }
  return rc;
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
  cson_value * pathSplit = NULL;
  assert( (0==once) && "json_mode_bootstrap() called too many times!");
  if( once ){
    return;
  }else{
    once = 1;
  }
  g.json.isJsonMode = 1;
  g.json.resultCode = 0;
  g.json.cmd.offset = -1;
  if( !g.isHTTP && g.fullHttpReply ){
    /* workaround for server mode, so we see it as CGI mode. */
    g.isHTTP = 1;
  }
  if(! g.json.post.v ){
    /* If cgi_init() reads POSTed JSON then it sets the content type.
       If it did not then we need to set it.
    */
    cgi_set_content_type(json_guess_content_type());
  }

#if defined(NDEBUG)
  /* avoids debug messages on stderr in JSON mode */
  sqlite3_config(SQLITE_CONFIG_LOG, NULL, 0);
#endif

  g.json.cmd.v = cson_value_new_array();
  g.json.cmd.a = cson_value_get_array(g.json.cmd.v);
  json_gc_add( FossilJsonKeys.commandPath, g.json.cmd.v, 1 );
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
      part = cson_value_new_string(arg,strlen(arg));
      cson_array_append(g.json.cmd.a, part);
    }
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

  {/* set up JSON output formatting options. */
    unsigned char indent = g.isHTTP ? 0 : 1;
    cson_value const * indentV = json_getenv("indent");
    if(indentV){
      if(cson_value_is_string(indentV)){
        int const n = atoi(cson_string_cstr(cson_value_get_string(indentV)));
        indent = (n>0)
          ? (unsigned char)n
          : 0;
      }else if(cson_value_is_number(indentV)){
        cson_int_t const n = cson_value_get_integer(indentV);
        indent = (n>0) ? (unsigned char)n : 0;
      }
    }
    g.json.outOpt.indentation = indent;
    g.json.outOpt.addNewline = g.isHTTP ? 0 : 1;
  }

  if( g.isHTTP ){
    json_auth_token()/* will copy our auth token, if any, to fossil's
                        core, which we need before we call
                        login_check_credentials(). */;
    login_check_credentials()/* populates g.perm */;
  }
  else{
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
*/
static char const * json_command_arg(unsigned char ndx){
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

/*
** If g.json.reqPayload.o is NULL then NULL is returned, else the
** given property is searched for in the request payload.  If found it
** is returned. The returned value is owned by (or shares ownership
** with) g.json, and must NOT be cson_value_free()'d by the
** caller.
*/
static cson_value * json_payload_property( char const * key ){
  return g.json.reqPayload.o ?
    cson_object_get( g.json.reqPayload.o, key )
    : NULL;
}


/* Returns the C-string form of json_auth_token(), or NULL
** if json_auth_token() returns NULL.
*/
char const * json_auth_token_cstr(){
  return cson_value_get_cstr( json_auth_token() );
}


/*
** Holds name-to-function mappings for JSON page/command dispatching.
**
*/
typedef struct JsonPageDef{
  /*
  ** The commmand/page's name (path, not including leading /json/).
  **
  ** Reminder to self: we cannot use sub-paths with commands this way
  ** without additional string-splitting downstream. e.g. foo/bar.
  ** Alternately, we can create different JsonPageDef arrays for each
  ** subset.
  */
  char const * name;
  /*
  ** Returns a payload object for the response.  If it returns a
  ** non-NULL value, the caller owns it.  To trigger an error this
  ** function should set g.json.resultCode to a value from the
  ** FossilJsonCodes enum. If it sets an error value and returns
  ** a payload, the payload will be destroyed (not sent with the
  ** response).
  */
  fossil_json_f func;
  /*
  ** Which mode(s) of execution does func() support:
  **
  ** <0 = CLI only, >0 = HTTP only, 0==both
  */
  char runMode;
} JsonPageDef;

/*
** Returns the JsonPageDef with the given name, or NULL if no match is
** found.
**
** head must be a pointer to an array of JsonPageDefs in which the
** last entry has a NULL name.
*/
static JsonPageDef const * json_handler_for_name( char const * name, JsonPageDef const * head ){
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
** This function assert()s that code is either 0
** or between the range of 1000 and 9999.
*/
static int json_dumbdown_rc( int code ){
  if( !code ){
    return 0;
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
** Creates a new Fossil/JSON response envelope skeleton.  It is owned
** by the caller, who must eventually free it using cson_value_free(),
** or add it to a cson container to transfer ownership. Returns NULL
** on error.
**
** If payload is not NULL and resultCode is 0 then it is set as the
** "payload" property of the returned object.  If resultCode is
** non-zero and payload is not NULL then this function calls
** cson_value_free(payload) and does not insert the payload into the
** response. In either case, onwership of payload is transfered to
** this function.
**
** pMsg is an optional message string property (resultText) of the
** response. If resultCode is non-0 and pMsg is NULL then
** json_err_str() is used to get the error string. The caller may
** provide his own or may use an empty string to suppress the
** resultText property.
**
*/
cson_value * json_create_response( int resultCode,
                                   cson_value * payload,
                                   char const * pMsg ){
  cson_value * v = NULL;
  cson_value * tmp = NULL;
  cson_object * o = NULL;
  int rc;
  resultCode = json_dumbdown_rc(resultCode);
  v = cson_value_new_object();
  o = cson_value_get_object(v);
  if( ! o ) return NULL;
#define SET(K) if(!tmp) goto cleanup; \
  rc = cson_object_set( o, K, tmp ); \
  if(rc) do{\
    cson_value_free(tmp); \
    tmp = NULL; \
    goto cleanup; \
  }while(0)

  tmp = cson_value_new_string(MANIFEST_UUID,strlen(MANIFEST_UUID));
  SET("fossil");
 
  {/* timestamp */
    cson_int_t jsTime;
#if 1
    jsTime = (cson_int_t)time(0);
#elif 1
    /* Ge Weijers has pointed out that time(0) commonly returns
       UTC, but is not required to by The Standard.

       There is a mkfmtime() function in cgi.c but it requires
       a (tm *), and i don't have that without calling gmtime()
       or populating the tm myself (which is what i'm trying to
       have done for me!).
    */
    time_t const t = (time_t)time(0);
    struct tm gt = *gmtime(&t);
    gt.tm_isdst = -1;
    jsTime = (cson_int_t)mktime(&gt);
#else
    /* i'm not 100% sure that the above actually does what i expect,
       but we can't use the following because this function can be
       called in response to error handling if the db cannot be opened
       (or before that).
    */
    jsTime = (cson_int_t)db_int64(0, "SELECT strftime('%%s','now')");
#endif
    tmp = cson_value_new_integer(jsTime);
    SET(FossilJsonKeys.timestamp);
  }
  if( 0 != resultCode ){
    if( ! pMsg ) pMsg = json_err_str(resultCode);
    tmp = json_rc_string(resultCode);
    SET(FossilJsonKeys.resultCode);
  }
  if( pMsg && *pMsg ){
    tmp = cson_value_new_string(pMsg,strlen(pMsg));
    SET(FossilJsonKeys.resultText);
  }
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
    if(0){/*Only for debuggering, add some info to the response.*/
      tmp = cson_value_new_integer( g.json.cmd.offset );
      cson_object_set( o, "cmd.offset", tmp );
      cson_object_set( o, "isCGI", cson_value_new_bool( g.isHTTP ) );
    }
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
** !g.isHTTP then alsoOutput is ignored and all output is sent to
** stdout immediately.
**
*/
void json_err( int code, char const * msg, char alsoOutput ){
  int rc = code ? code : (g.json.resultCode
                          ? g.json.resultCode
                          : FSL_JSON_E_UNKNOWN);
  cson_value * resp = NULL;
  rc = json_dumbdown_rc(rc);
  if( rc && !msg ){
    msg = json_err_str(rc);
  }
  resp = json_create_response(rc, NULL, msg);
  if(!resp){
    /* about the only error case here is out-of-memory. DO NOT
       call fossil_panic() here because that calls this function.
    */
    fprintf(stderr, "%s: Fatal error: could not allocate "
            "response object.\n", fossil_nameofexe());
    fossil_exit(1);
  }
  if( g.isHTTP ){
    Blob buf = empty_blob;
    cgi_reset_content();
    cson_output_Blob( resp, &buf, &g.json.outOpt );
    cgi_set_content(&buf);
    if( alsoOutput ){
      cgi_reply();
    }
  }else{
    cson_output_FILE( resp, stdout, &g.json.outOpt );
  }
  cson_value_free(resp);
}

/*
** /json/version implementation.
**
** Returns the payload object (owned by the caller).
*/
cson_value * json_page_version(unsigned int depth){
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
#undef FSET
  cson_object_set( jobj, "releaseVersionNumber",
                   cson_value_new_integer(RELEASE_VERSION_NUMBER) );
  cson_object_set( jobj, "resultCodeParanoiaLevel",
                   cson_value_new_integer(g.json.errorDetailParanoia) );
  return jval;
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
cson_value * json_page_cap(unsigned int depth){
  cson_value * payload = cson_value_new_object();
  cson_value * sub = cson_value_new_object();
  char * zCap;
  Stmt q;
  cson_object * obj = cson_value_get_object(payload);
  db_prepare(&q, "SELECT login, cap FROM user WHERE uid=%d", g.userUid);
  if( db_step(&q)==SQLITE_ROW ){
    /* reminder: we don't use g.zLogin because it's 0 for the guest
       user and the HTML UI appears to currently allow the name to be
       changed (but doing so would break other code). */
    char const * str = (char const *)sqlite3_column_text(q.pStmt,0);
    if( str ){
      cson_object_set( obj, "userName",
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

#define ADD(X) cson_object_set(obj, #X, cson_value_new_bool(g.perm.X))
  ADD(Setup);
  ADD(Admin);
  ADD(Delete);
  ADD(Password);
  ADD(Query);
  ADD(Write);
  ADD(Read);
  ADD(History);
  ADD(Clone);
  ADD(RdWiki);
  ADD(NewWiki);
  ADD(ApndWiki);
  ADD(WrWiki);
  ADD(RdTkt);
  ADD(NewTkt);
  ADD(ApndTkt);
  ADD(WrTkt);
  ADD(Attach);
  ADD(TktFmt);
  ADD(RdAddr);
  ADD(Zip);
  ADD(Private);
#undef ADD
  return payload;
}

/*
** Implementation of the /json/login page.
**
*/
cson_value * json_page_login(unsigned int depth){
  static char preciseErrors = /* if true, "complete" JSON error codes are used,
                                 else they are "dumbed down" to a generic login
                                 error code.
                              */
#if 0
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
  char const * name = cson_value_get_cstr(json_payload_property("name"));
  char const * pw = NULL;
  char const * anonSeed = NULL;
  cson_value * payload = NULL;
  int uid = 0;
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
  
  pw = cson_value_get_cstr(json_payload_property("password"));
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
      jseed = json_payload_property(FossilJsonKeys.anonymousSeed);
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
#else
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
    if(anonSeed){
      login_set_anon_cookie(NULL, &cookie);
    }else{
      login_set_user_cookie(name, uid, &cookie);
    }
    payload = cookie
      ? cson_value_new_string( cookie, strlen(cookie) )
      : cson_value_null()/*why null instead of NULL?*/;
    free(cookie);
    return payload;
  }
#endif
}

/*
** Impl of /json/logout.
**
*/
cson_value * json_page_logout(unsigned int depth){
  cson_value const *token = g.json.authToken;
    /* Remember that json_mode_bootstrap() replaces the login cookie
       with the JSON auth token if the request contains it. If the
       reqest is missing the auth token then this will fetch fossil's
       original cookie. Either way, it's what we want :).

       We require the auth token to avoid someone maliciously
       trying to log someone else out (not 100% sure if that
       would be possible, given fossil's hardened cookie, but
       i'll assume it would be for the time being).
    */
    ;
  if(!token){
    g.json.resultCode = FSL_JSON_E_MISSING_AUTH;
  }else{
    login_clear_login_data();
    g.json.authToken = NULL /* memory is owned elsewhere.*/;
  }
  return NULL;
}

/*
** Implementation of the /json/anonymousPassword page.
*/
cson_value * json_page_anon_password(unsigned int depth){
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
** Implementation of the /json/stat page/command.
**
*/
cson_value * json_page_stat(unsigned int depth){
  i64 t, fsize;
  int n, m;
  const char *zDb;
  enum { BufLen = 1000 };
  char zBuf[BufLen];
  cson_value * jv = NULL;
  cson_object * jo = NULL;
  cson_value * jv2 = NULL;
  cson_object * jo2 = NULL;
  if( !g.perm.Read ){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }
#define SETBUF(O,K) cson_object_set(O, K, cson_value_new_string(zBuf, strlen(zBuf)));

  jv = cson_value_new_object();
  jo = cson_value_get_object(jv);

  sqlite3_snprintf(BufLen, zBuf, db_get("project-name",""));
  SETBUF(jo, "projectName");
  /* FIXME: don't include project-description until we ensure that
     zBuf will always be big enough. We "should" replace zBuf
     with a blob for this purpose.
  */
  fsize = file_size(g.zRepositoryName);
  cson_object_set(jo, "repositorySize", cson_value_new_integer((cson_int_t)fsize));

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
  n = db_int(0, "SELECT julianday('now') - (SELECT min(mtime) FROM event)"
                " + 0.99");
  cson_object_set(jo, "ageDays", cson_value_new_integer((cson_int_t)n));
  cson_object_set(jo, "ageYears", cson_value_new_double(n/365.24));
  sqlite3_snprintf(BufLen, zBuf, db_get("project-code",""));
  SETBUF(jo, "projectCode");
  sqlite3_snprintf(BufLen, zBuf, db_get("server-code",""));
  SETBUF(jo, "serverCode");
  cson_object_set(jo, "compiler", cson_value_new_string(COMPILER_NAME, strlen(COMPILER_NAME)));

  jv2 = cson_value_new_object();
  jo2 = cson_value_get_object(jv2);
  cson_object_set(jo, "sqlite", jv2);
  sqlite3_snprintf(BufLen, zBuf, "%.19s [%.10s] (%s)",
                   SQLITE_SOURCE_ID, &SQLITE_SOURCE_ID[20], SQLITE_VERSION);
  SETBUF(jo2, "version");
  zDb = db_name("repository");
  cson_object_set(jo2, "pageCount", cson_value_new_integer((cson_int_t)db_int(0, "PRAGMA %s.page_count", zDb)));
  cson_object_set(jo2, "pageSize", cson_value_new_integer((cson_int_t)db_int(0, "PRAGMA %s.page_size", zDb)));
  cson_object_set(jo2, "freeList", cson_value_new_integer((cson_int_t)db_int(0, "PRAGMA %s.freelist_count", zDb)));
  sqlite3_snprintf(BufLen, zBuf, "%s", db_text(0, "PRAGMA %s.encoding", zDb));
  SETBUF(jo2, "encoding");
  sqlite3_snprintf(BufLen, zBuf, "%s", db_text(0, "PRAGMA %s.journal_mode", zDb));
  cson_object_set(jo2, "journalMode", *zBuf ? cson_value_new_string(zBuf, strlen(zBuf)) : cson_value_null());
  return jv;
#undef SETBUF
}


static cson_value * json_wiki_list(unsigned int depth);

/*
** Mapping of /json/wiki/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Wiki[] = {
{"get", json_page_nyi, 0},
{"list", json_wiki_list, 0},
{"save", json_page_nyi, 1},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** A page/command dispatch helper for fossil_json_f() implementations.
** depth should be the depth parameter passed to the fossil_json_f().
** pages must be an array of JsonPageDef commands which we can
** dispatch.  The final item in the array MUST have a NULL name
** element.
**
** This function takes json_comand_arg(1+depth) and searches pages
** for a matching name. If found then that page's func() is called
** to fetch the payload, which is returned to the caller.
**
** On error, g.json.resultCode is set to one of the FossilJsonCodes
** values.
*/
static cson_value * json_page_dispatch_helper(unsigned int depth,
                                              JsonPageDef const * pages){
  JsonPageDef const * def;
  char const * cmd = json_command_arg(1+depth);
  assert( NULL != pages );
  if( ! cmd ){
    g.json.resultCode = FSL_JSON_E_MISSING_ARGS;
    return NULL;
  }
  def = json_handler_for_name( cmd, pages );
  if(!def){
    g.json.resultCode = FSL_JSON_E_UNKNOWN_COMMAND;
    return NULL;
  }
  else{
    return (*def->func)(1+depth);
  }
}

/*
** Implements the /json/wiki family of pages/commands. Far from
** complete.
**
*/
static cson_value * json_page_wiki(unsigned int depth){
  return json_page_dispatch_helper(depth,&JsonPageDefs_Wiki[0]);
}

/*
** INTERIM implementation of /json/wiki/list
*/
static cson_value * json_wiki_list(unsigned int depth){
  cson_value * listV = NULL;
  cson_array * list = NULL;
  Stmt q;
  db_prepare(&q,"SELECT"
             " substr(tagname,6) as name"
             " FROM tag WHERE tagname GLOB 'wiki-*'"
             " ORDER BY lower(name)");
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  while( SQLITE_ROW == db_step(&q) ){
    cson_value * v = cson_sqlite3_column_to_value(q.pStmt,0);
    if(!v){
      goto error;
    }else if( 0 != cson_array_append( list, v ) ){
      cson_value_free(v);
      goto error;
    }
  }
  goto end;
  error:
  g.json.resultCode = FSL_JSON_E_UNKNOWN;
  cson_value_free(listV);
  listV = NULL;
  end:
  db_finalize(&q);
  return listV;
}


static cson_value * json_timeline_ci(unsigned int depth);
/*
** Mapping of /json/timeline/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Timeline[] = {
{"ci", json_timeline_ci, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** Implements the /json/timeline family of pages/commands. Far from
** complete.
**
*/
static cson_value * json_page_timeline(unsigned int depth){
  return json_page_dispatch_helper(depth,&JsonPageDefs_Timeline[0]);
}

static int json_getenv_int(char const * pKey, int dflt ){
  cson_value const * v = json_getenv(pKey);
  if(!v){
    return dflt;
  }else if( cson_value_is_number(v) ){
    return (int)cson_value_get_integer(v);
  }else if( cson_value_is_string(v) ){
    char const * sv = cson_string_cstr(cson_value_get_string(v));
    assert( (NULL!=sv) && "This is quite unexpected." );
    return sv ? atoi(sv) : dflt;
  }else if( cson_value_is_bool(v) ){
    return cson_value_get_bool(v) ? 1 : 0;
  }else{
    /* we should arguably treat JSON null as 0. */
    return dflt;
  }
}

/*
** Create a temporary table suitable for storing timeline data.
*/
static void json_timeline_temp_table(void){
  /* Field order MUST match that from json_timeline_query_XXX()!!! */
  static const char zSql[] = 
    @ CREATE TEMP TABLE IF NOT EXISTS json_timeline(
    @   rid INTEGER PRIMARY KEY,
    @   uuid TEXT,
    @   mtime INTEGER,
    @   timestampString TEXT,
    @   comment TEXT,
    @   user TEXT,
    @   isLeaf BOOLEAN,
    @   bgColor TEXT,
    @   eventType TEXT,
    @   tags TEXT,
    @   tagId INTEGER,
    @   brief TEXT
    @ )
  ;
  db_multi_exec(zSql);
}

/*
** Return a pointer to a constant string that forms the basis
** for a timeline query for the JSON "checkin" interface.
*/
const char const * json_timeline_query_ci(void){
  /* Field order MUST match that from json_timeline_temp_table()!!! */
  static const char zBaseSql[] =
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   strftime('%%s',event.mtime),
    @   datetime(event.mtime,'utc'),
    @   coalesce(ecomment, comment),
    @   coalesce(euser, user),
    @   blob.rid IN leaf,
    @   bgcolor,
    @   event.type,
    @   (SELECT group_concat(substr(tagname,5), ',') FROM tag, tagxref
    @     WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @       AND tagxref.rid=blob.rid AND tagxref.tagtype>0),
    @   tagid,
    @   brief
    @  FROM event JOIN blob 
    @ WHERE blob.rid=event.objid
  ;
  return zBaseSql;
}


/*
** /json/timeline/ci
**
** Far from complete.
*/
static cson_value * json_timeline_ci(unsigned int depth){
  static const int defaultLimit = 10;
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  cson_value * tmp = NULL;
  cson_value * listV = NULL;
  cson_array * list = NULL;
  int limit = json_getenv_int("n",defaultLimit);
  int check = 0;
  Stmt q;
  Blob sql = empty_blob;
  if( !g.perm.Read/* && !g.perm.RdTkt && !g.perm.RdWiki*/ ){
    g.json.resultCode = FSL_JSON_E_DENIED;
    return NULL;
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  if( limit < 0 ) limit = defaultLimit;
  json_timeline_temp_table();
  blob_append(&sql, "INSERT OR IGNORE INTO json_timeline ", -1);
  blob_append(&sql, json_timeline_query_ci(), -1 );
  blob_append(&sql, "AND event.type IN('ci') ", -1);
  blob_append(&sql, "ORDER BY mtime DESC ", -1);
#define SET(K) if(0!=(check=cson_object_set(pay,K,tmp))){ \
    g.json.resultCode = (cson_rc.AllocError==check) \
      ? FSL_JSON_E_ALLOC : FSL_JSON_E_UNKNOWN; \
    goto error;\
  }
  if(limit){
    blob_appendf(&sql,"LIMIT %d ",limit);
    tmp = cson_value_new_integer(limit);
    SET("limit");
  }
  db_multi_exec(blob_buffer(&sql));

#if 0
  /* only for testing! */
  tmp = cson_value_new_string(blob_buffer(&sql),strlen(blob_buffer(&sql)));
  SET("timelineSql");
#endif

  blob_reset(&sql);
  blob_append(&sql, "SELECT rid AS rid,"
              " uuid AS uuid,"
              " mtime AS timestamp,"
              " timestampString AS timestampString,"
              " comment AS comment, "
              " user AS user,"
              " isLeaf AS isLeaf," /*FIXME: convert to JSON bool */
              " bgColor AS bgColor,"
              " eventType AS eventType,"
              " tags AS tags," /*FIXME: split this into
                                 a JSON array*/
              " tagId AS tagId,"
              " brief AS briefText"
              " FROM json_timeline"
              " ORDER BY mtime DESC",
              -1);
  db_prepare(&q,blob_buffer(&sql));
  tmp = NULL;
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  tmp = listV;
  SET("timeline");
  while( (SQLITE_ROW == db_step(&q) )){
    /* convert each row into a JSON object...*/
    cson_value * rowV = cson_sqlite3_row_to_object(q.pStmt);
    cson_object * row = cson_value_get_object(rowV);
    cson_string const * tagsStr = NULL;
    if(!row){
      /* need a way of warning about this */
      continue;
    }

    /* Split tags string field into JSON Array... */
    cson_array_append(list, rowV);
    tagsStr = cson_value_get_string(cson_object_get(row,"tags"));
    if(tagsStr){
      cson_value * tagsV = cson_value_new_array();
      cson_array * tags = cson_value_get_array(tagsV);
      if( 0 < json_string_split( cson_string_cstr(tagsStr), ',', 0, tags)){
        cson_object_set(row,"tags",tagsV)
          /*replaces/deletes old tags value, invalidating tagsStr!*/;
      }else{
        cson_value_free(tagsV);
      }
    }
  }
  db_finalize(&q);
#undef SET
  goto ok;
  error:
  cson_value_free(payV);
  payV = NULL;
  ok:
  return payV;
}


/*
** Mapping of names to JSON pages/commands.  Each name is a subpath of
** /json (in CGI mode) or a subcommand of the json command in CLI mode
*/
static const JsonPageDef JsonPageDefs[] = {
/* please keep alphabetically sorted (case-insensitive) for maintenance reasons. */
{"anonymousPassword",json_page_anon_password, 1},
{"branch", json_page_nyi,0},
{"cap", json_page_cap, 0},
{"dir", json_page_nyi, 0},
{"HAI",json_page_version,0},
{"login",json_page_login,1},
{"logout",json_page_logout,1},
{"stat",json_page_stat,0},
{"tag", json_page_nyi,0},
{"ticket", json_page_nyi,0},
{"timeline", json_page_timeline,0},
{"user", json_page_nyi,0},
{"version",json_page_version,0},
{"wiki",json_page_wiki,0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};


/*
** Mapping of /json/ticket/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Ticket[] = {
{"get", json_page_nyi, 0},
{"list", json_page_nyi, 0},
{"save", json_page_nyi, 1},
{"create", json_page_nyi, 1},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** Mapping of /json/artifact/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Artifact[] = {
{"vinfo", json_page_nyi, 0},
{"finfo", json_page_nyi, 0},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** Mapping of /json/branch/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Branch[] = {
{"list", json_page_nyi, 0},
{"create", json_page_nyi, 1},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** Mapping of /json/tag/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Tag[] = {
{"list", json_page_nyi, 0},
{"create", json_page_nyi, 1},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};


/*
** WEBPAGE: json
**
** Pages under /json/... must be entered into JsonPageDefs.
** This function dispatches them, and is the HTTP equivalent of
** json_cmd_top().
*/
void json_page_top(void){
  int rc = FSL_JSON_E_UNKNOWN_COMMAND;
  Blob buf = empty_blob;
  char const * cmd;
  cson_value * payload = NULL;
  cson_value * root = NULL;
  JsonPageDef const * pageDef = NULL;
  json_mode_bootstrap();
  cmd = json_command_arg(1);
  /*cgi_printf("{\"cmd\":\"%s\"}\n",cmd); return;*/
  pageDef = json_handler_for_name(cmd,&JsonPageDefs[0]);
  if( ! pageDef ){
    json_err( FSL_JSON_E_UNKNOWN_COMMAND, NULL, 0 );
    return;
  }else if( pageDef->runMode < 0 /*CLI only*/){
    rc = FSL_JSON_E_WRONG_MODE;
  }else{
    rc = 0;
    payload = (*pageDef->func)(1);
  }
  if( g.json.resultCode ){
    json_err(g.json.resultCode, NULL, 0);
  }else{
    blob_zero(&buf);
    root = json_create_response(rc, payload, NULL);
    cson_output_Blob( root, &buf, NULL );
    cson_value_free(root);
    cgi_set_content(&buf)/*takes ownership of the buf memory*/;
  }
}

/*
** This function dispatches json commands and is the CLI equivalent of
** json_page_top().
**
** COMMAND: json
**
** Usage: %fossil json SUBCOMMAND
**
** The commands include:
**
**   cap
**   stat
**   version (alias: HAI)
**
**
** TODOs:
**
**   branch
**   tag
**   ticket
**   timeline
**   wiki
**   ...
**
*/
void json_cmd_top(void){
  char const * cmd = NULL;
  int rc = FSL_JSON_E_UNKNOWN_COMMAND;
  cson_value * payload = NULL;
  JsonPageDef const * pageDef;
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
  if( g.argc<3 ){
    goto usage;
  }
  db_find_and_open_repository(0, 0);
  cmd = json_command_arg(1);
  if( !cmd || !*cmd ){
    goto usage;
  }
  pageDef = json_handler_for_name(cmd,&JsonPageDefs[0]);
  if( ! pageDef ){
    json_err( FSL_JSON_E_UNKNOWN_COMMAND, NULL, 1 );
    return;
  }else if( pageDef->runMode > 0 /*HTTP only*/){
    rc = FSL_JSON_E_WRONG_MODE;
  }else{
    rc = 0;
    payload = (*pageDef->func)(1);
  }
  if( g.json.resultCode ){
    json_err(g.json.resultCode, NULL, 1);
  }else{
    payload = json_create_response(rc, payload, NULL);
    cson_output_FILE( payload, stdout, &g.json.outOpt );
    cson_value_free( payload );
    if((0 != rc) && !g.isHTTP){
      /* FIXME: we need a way of passing this error back
         up to the routine which called this callback.
         e.g. add g.errCode.
      */
      fossil_exit(1);
    }
  }
  return;
  usage:
  usage("subcommand");
}
