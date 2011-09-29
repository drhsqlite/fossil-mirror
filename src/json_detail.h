#if !defined(FOSSIL_JSON_DETAIL_H_INCLUDED)
#define FOSSIL_JSON_DETAIL_H_INCLUDED

#include "cson_amalgamation.h"
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
*/
enum FossilJsonCodes {
FSL_JSON_W_START = 0,
FSL_JSON_W_UNKNOWN = FSL_JSON_W_START + 1,
FSL_JSON_W_ROW_TO_JSON_FAILED = FSL_JSON_W_START + 2,
FSL_JSON_W_COL_TO_JSON_FAILED = FSL_JSON_W_START + 3,
FSL_JSON_W_STRING_TO_ARRAY_FAILED = FSL_JSON_W_START + 4,

FSL_JSON_W_END = 1000,
FSL_JSON_E_GENERIC = 1000,
FSL_JSON_E_GENERIC_SUB1 = FSL_JSON_E_GENERIC + 100,
FSL_JSON_E_INVALID_REQUEST = FSL_JSON_E_GENERIC_SUB1 + 1,
FSL_JSON_E_UNKNOWN_COMMAND = FSL_JSON_E_GENERIC_SUB1 + 2,
FSL_JSON_E_UNKNOWN = FSL_JSON_E_GENERIC_SUB1 + 3,
FSL_JSON_E_RESOURCE_NOT_FOUND = FSL_JSON_E_GENERIC_SUB1 + 4,
FSL_JSON_E_TIMEOUT = FSL_JSON_E_GENERIC_SUB1 + 5,
FSL_JSON_E_ASSERT = FSL_JSON_E_GENERIC_SUB1 + 6,
FSL_JSON_E_ALLOC = FSL_JSON_E_GENERIC_SUB1 + 7,
FSL_JSON_E_NYI = FSL_JSON_E_GENERIC_SUB1 + 8,
FSL_JSON_E_PANIC = FSL_JSON_E_GENERIC_SUB1 + 9,

FSL_JSON_E_AUTH = 2000,
FSL_JSON_E_MISSING_AUTH = FSL_JSON_E_AUTH + 1,
FSL_JSON_E_DENIED = FSL_JSON_E_AUTH + 2,
FSL_JSON_E_WRONG_MODE = FSL_JSON_E_AUTH + 3,
FSL_JSON_E_RESOURCE_ALREADY_EXISTS = FSL_JSON_E_AUTH + 4,

FSL_JSON_E_LOGIN_FAILED = FSL_JSON_E_AUTH + 100,
FSL_JSON_E_LOGIN_FAILED_NOSEED = FSL_JSON_E_LOGIN_FAILED + 1,
FSL_JSON_E_LOGIN_FAILED_NONAME = FSL_JSON_E_LOGIN_FAILED + 2,
FSL_JSON_E_LOGIN_FAILED_NOPW = FSL_JSON_E_LOGIN_FAILED + 3,
FSL_JSON_E_LOGIN_FAILED_NOTFOUND = FSL_JSON_E_LOGIN_FAILED + 4,

FSL_JSON_E_USAGE = 3000,
FSL_JSON_E_INVALID_ARGS = FSL_JSON_E_USAGE + 1,
FSL_JSON_E_MISSING_ARGS = FSL_JSON_E_USAGE + 2,

FSL_JSON_E_DB = 4000,
FSL_JSON_E_STMT_PREP = FSL_JSON_E_DB + 1,
FSL_JSON_E_STMT_BIND = FSL_JSON_E_DB + 2,
FSL_JSON_E_STMT_EXEC = FSL_JSON_E_DB + 3,
FSL_JSON_E_DB_LOCKED = FSL_JSON_E_DB + 4,

FSL_JSON_E_DB_NEEDS_REBUILD = FSL_JSON_E_DB + 101

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
** tranferred to the caller). On error they should set
** g.json.resultCode to one of the FossilJsonCodes values and return
** either their payload object or NULL. Note that NULL is a legal
** success value - it simply means the response will contain no
** payload. If g.json.resultCode is non-zero when this function
** returns then the top-level dispatcher will destroy any payload
** returned by this function and will output a JSON error response
** instead.
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
typedef cson_value * (*fossil_json_f)();

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
** Holds common keys used for various JSON API properties.
*/
typedef struct FossilJsonKeys_{
  /** maintainers: please keep alpha sorted (case-insensitive) */
  char const * anonymousSeed;
  char const * authToken;
  char const * commandPath;
  char const * payload;
  char const * requestId;
  char const * resultCode;
  char const * resultText;
  char const * timestamp;
} FossilJsonKeys_;
const FossilJsonKeys_ FossilJsonKeys;

/*
** A page/command dispatch helper for fossil_json_f() implementations.
** pages must be an array of JsonPageDef commands which we can
** dispatch. The final item in the array MUST have a NULL name
** element.
**
** This function takes the command specified in
** json_comand_arg(1+g.json.dispatchDepth) and searches pages for a
** matching name. If found then that page's func() is called to fetch
** the payload, which is returned to the caller.
**
** On error, g.json.resultCode is set to one of the FossilJsonCodes
** values and NULL is returned. If non-NULL is returned, ownership is
** transfered to the caller.
*/
cson_value * json_page_dispatch_helper(JsonPageDef const * pages);

/*
** Implements the /json/wiki family of pages/commands.
**
*/
cson_value * json_page_wiki();

/*
** Implements /json/timeline/wiki and /json/wiki/timeline.
*/
cson_value * json_timeline_wiki();

/*
** Implements /json/timeline family of functions.
*/
cson_value * json_page_timeline();

#endif/*FOSSIL_JSON_DETAIL_H_INCLUDED*/
