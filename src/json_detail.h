#ifdef FOSSIL_ENABLE_JSON
#if !defined(FOSSIL_JSON_DETAIL_H_INCLUDED)
#define FOSSIL_JSON_DETAIL_H_INCLUDED
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

#include "cson_amalgamation.h"

/**
   FOSSIL_JSON_API_VERSION holds the date (YYYYMMDD) of the latest
   "significant" change to the JSON API (a change in an interface or
   new functionality). It is sent as part of the /json/version
   request. We could arguably add it to each response or even add a
   version number to each response type, allowing very fine (too
   fine?) granularity in compatibility change notification. The
   version number could be included in part of the command dispatching
   framework, allowing the top-level dispatching code to deal with it
   (for the most part).
*/
#define FOSSIL_JSON_API_VERSION "20120713"

/*
** Impl details for the JSON API which need to be shared
** across multiple C files.
*/

/*
** The "official" list of Fossil/JSON error codes.  Their values might
** very well change during initial development but after their first
** public release they must stay stable.
**
** Values must be in the range 1000..9999 for error codes and 1..999
** for warning codes.
**
** Numbers evenly dividable by 100 are "categories", and error codes
** for a given category have their high bits set to the category
** value.
**
** Maintenance reminder: when entries are added to this list, update
** the code in json_page_resultCodes() and json_err_cstr() (both in
** json.c)!
**
*/
enum FossilJsonCodes {
FSL_JSON_W_START = 0,
FSL_JSON_W_UNKNOWN /*+1*/,
FSL_JSON_W_ROW_TO_JSON_FAILED /*+2*/,
FSL_JSON_W_COL_TO_JSON_FAILED /*+3*/,
FSL_JSON_W_STRING_TO_ARRAY_FAILED /*+4*/,
FSL_JSON_W_TAG_NOT_FOUND /*+5*/,

FSL_JSON_W_END = 1000,
FSL_JSON_E_GENERIC = 1000,
FSL_JSON_E_GENERIC_SUB1 = FSL_JSON_E_GENERIC + 100,
FSL_JSON_E_INVALID_REQUEST /*+1*/,
FSL_JSON_E_UNKNOWN_COMMAND /*+2*/,
FSL_JSON_E_UNKNOWN /*+3*/,
/*REUSE: +4*/
FSL_JSON_E_TIMEOUT /*+5*/,
FSL_JSON_E_ASSERT /*+6*/,
FSL_JSON_E_ALLOC /*+7*/,
FSL_JSON_E_NYI /*+8*/,
FSL_JSON_E_PANIC /*+9*/,
FSL_JSON_E_MANIFEST_READ_FAILED /*+10*/,
FSL_JSON_E_FILE_OPEN_FAILED /*+11*/,

FSL_JSON_E_AUTH = 2000,
FSL_JSON_E_MISSING_AUTH /*+1*/,
FSL_JSON_E_DENIED /*+2*/,
FSL_JSON_E_WRONG_MODE /*+3*/,

FSL_JSON_E_LOGIN_FAILED = FSL_JSON_E_AUTH +100,
FSL_JSON_E_LOGIN_FAILED_NOSEED /*+1*/,
FSL_JSON_E_LOGIN_FAILED_NONAME /*+2*/,
FSL_JSON_E_LOGIN_FAILED_NOPW /*+3*/,
FSL_JSON_E_LOGIN_FAILED_NOTFOUND /*+4*/,

FSL_JSON_E_USAGE = 3000,
FSL_JSON_E_INVALID_ARGS /*+1*/,
FSL_JSON_E_MISSING_ARGS /*+2*/,
FSL_JSON_E_AMBIGUOUS_UUID /*+3*/,
FSL_JSON_E_UNRESOLVED_UUID /*+4*/,
FSL_JSON_E_RESOURCE_ALREADY_EXISTS /*+5*/,
FSL_JSON_E_RESOURCE_NOT_FOUND /*+6*/,

FSL_JSON_E_DB = 4000,
FSL_JSON_E_STMT_PREP /*+1*/,
FSL_JSON_E_STMT_BIND /*+2*/,
FSL_JSON_E_STMT_EXEC /*+3*/,
FSL_JSON_E_DB_LOCKED /*+4*/,

FSL_JSON_E_DB_NEEDS_REBUILD = FSL_JSON_E_DB + 101,
FSL_JSON_E_DB_NOT_FOUND = FSL_JSON_E_DB + 102,
FSL_JSON_E_DB_NOT_VALID = FSL_JSON_E_DB + 103,
/*
** Maintenance reminder: FSL_JSON_E_DB_NOT_FOUND gets triggered in the
** bootstrapping process before we know whether we need to check for
** FSL_JSON_E_DB_NEEDS_CHECKOUT. Thus the former error trumps the
** latter.
*/
FSL_JSON_E_DB_NEEDS_CHECKOUT = FSL_JSON_E_DB + 104
};


/*
** Signature for JSON page/command callbacks. Each callback is
** responsible for handling one JSON request/command and/or
** dispatching to sub-commands.
**
** By the time the callback is called, json_page_top() (HTTP mode) or
** json_cmd_top() (CLI mode) will have set up the JSON-related
** environment. Implementations may generate a "result payload" of any
** JSON type by returning its value from this function (ownership is
** transferred to the caller). On error they should set
** g.json.resultCode to one of the FossilJsonCodes values and return
** either their payload object or NULL. Note that NULL is a legal
** success value - it simply means the response will contain no
** payload. If g.json.resultCode is non-zero when this function
** returns then the top-level dispatcher will destroy any payload
** returned by this function and will output a JSON error response
** instead.
**
** All of the setup/response code is handled by the top dispatcher
** functions and the callbacks concern themselves only with:
**
** a) Permissions checking (inspecting g.perm).
** b) generating a response payload (if applicable)
** c) Setting g.json's error state (if applicable). See json_set_err().
**
** It is imperative that NO callback functions EVER output ANYTHING to
** stdout, as that will effectively corrupt any JSON output, and
** almost certainly will corrupt any HTTP response headers. Output
** sent to stderr ends up in my apache log, so that might be useful
** for debugging in some cases, but no such code should be left
** enabled for non-debugging builds.
*/
typedef cson_value * (*fossil_json_f)();

/*
** Holds name-to-function mappings for JSON page/command dispatching.
**
** Internally we model page dispatching lists as arrays of these
** objects, where the final entry in the array has a NULL name value
** to act as the end-of-list sentinel.
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
  **
  ** Now that we can simulate POST in CLI mode, the distinction
  ** between them has disappeared in most (or all) cases, so 0 is
  ** the standard value.
  */
  int runMode;
} JsonPageDef;

/*
** Holds common keys used for various JSON API properties.
*/
typedef struct FossilJsonKeys_{
  /** maintainers: please keep alpha sorted (case-insensitive) */
  char const * anonymousSeed;
  char const * authToken;
  char const * commandPath;
  char const * mtime;
  char const * payload;
  char const * requestId;
  char const * resultCode;
  char const * resultText;
  char const * timestamp;
} FossilJsonKeys_;
extern const FossilJsonKeys_ FossilJsonKeys;

/*
** A page/command dispatch helper for fossil_json_f() implementations.
** pages must be an array of JsonPageDef commands which we can
** dispatch. The final item in the array MUST have a NULL name
** element.
**
** This function takes the command specified in
** json_command_arg(1+g.json.dispatchDepth) and searches pages for a
** matching name. If found then that page's func() is called to fetch
** the payload, which is returned to the caller.
**
** On error, g.json.resultCode is set to one of the FossilJsonCodes
** values and NULL is returned. If non-NULL is returned, ownership is
** transfered to the caller (but the g.json error state might still be
** set in that case, so the caller must check that or pass it on up
** the dispatch chain).
*/
cson_value * json_page_dispatch_helper(JsonPageDef const * pages);

/*
** Convenience wrapper around cson_value_new_string().
** Returns NULL if str is NULL or on allocation error.
*/
cson_value * json_new_string( char const * str );

/*
** Similar to json_new_string(), but takes a printf()-style format
** specifiers. Supports the printf extensions supported by fossil's
** mprintf().  Returns NULL if str is NULL or on allocation error.
**
** Maintenance note: json_new_string() is NOT variadic because by the
** time the variadic form was introduced we already had use cases
** which segfaulted via json_new_string() because they contain printf
** markup (e.g. wiki content). Been there, debugged that.
*/
cson_value * json_new_string_f( char const * fmt, ... );

/*
** Returns true if fossil is running in JSON mode and we are either
** running in HTTP mode OR g.json.post.o is not NULL (meaning POST
** data was fed in from CLI mode).
**
** Specifically, it will return false when any of these apply:
**
** a) Not running in JSON mode (via json command or /json path).
**
** b) We are running in JSON CLI mode, but no POST data has been fed
** in.
**
** Whether or not we need to take args from CLI or POST data makes a
** difference in argument/parameter handling in many JSON routines,
** and thus this distinction.
*/
int fossil_has_json();

enum json_get_changed_files_flags {
    json_get_changed_files_ELIDE_PARENT = 1 << 0
};

#endif/*FOSSIL_JSON_DETAIL_H_INCLUDED*/
#endif /* FOSSIL_ENABLE_JSON */
