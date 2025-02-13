#ifdef FOSSIL_ENABLE_JSON
#ifndef CSON_FOSSIL_MODE
#define CSON_FOSSIL_MODE
#endif
/* auto-generated! Do not edit! */
/* begin file include/wh/cson/cson.h */
#if !defined(WANDERINGHORSE_NET_CSON_H_INCLUDED)
#define WANDERINGHORSE_NET_CSON_H_INCLUDED 1

/*#include <stdint.h> C99: fixed-size int types. */
#include <stdio.h> /* FILE decl */

#include <stdarg.h>

/** @page page_cson cson JSON API

cson (pronounced "season") is an object-oriented C API for generating
and consuming JSON (http://www.json.org) data.

Its main claim to fame is that it can parse JSON from, and output it
to, damned near anywhere. The i/o routines use a callback function to
fetch/emit JSON data, allowing clients to easily plug in their own
implementations. Implementations are provided for string- and
FILE-based i/o.

Project home page: https://fossil.wanderinghorse.net/r/cson

Author: Stephan Beal (https://www.wanderinghorse.net/home/stephan/)

License: Dual Public Domain/MIT

The full license text is at the bottom of the main header file
(cson.h).

Examples of how to use the library are scattered throughout
the API documentation, in the test.c file in the source repo,
and in the wiki on the project's home page.
*/

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(_WIN32) || defined(_WIN64)
#  define CSON_ENABLE_UNIX 0
#else
#  define CSON_ENABLE_UNIX 1
#endif


/** @typedef some_long_int_type cson_int_t

Typedef for JSON-like integer types. This is (long long) where feasible,
otherwise (long).
*/
#ifdef _WIN32
typedef __int64 cson_int_t;
#define CSON_INT_T_SFMT "I64d"
#define CSON_INT_T_PFMT "I64d"
#elif (__STDC_VERSION__ >= 199901L) || (HAVE_LONG_LONG == 1)
typedef long long cson_int_t;
#define CSON_INT_T_SFMT "lld"
#define CSON_INT_T_PFMT "lld"
#else 
typedef long cson_int_t;
#define CSON_INT_T_SFMT "ld"
#define CSON_INT_T_PFMT "ld"
#endif

/** @typedef double_or_long_double cson_double_t

    This is the type of double value used by the library.
    It is only lightly tested with long double, and when using
    long double the memory requirements for such values goes
    up.

    Note that by default cson uses C-API defaults for numeric
    precision. To use a custom precision throughout the library, one
    needs to define the macros CSON_DOUBLE_T_SFMT and/or
    CSON_DOUBLE_T_PFMT macros to include their desired precision, and
    must build BOTH cson AND the client using these same values. For
    example:

    @code
    #define CSON_DOUBLE_T_PFMT ".8Lf" // for Modified Julian Day values
    #define HAVE_LONG_DOUBLE
    @endcode

    (Only CSON_DOUBLE_T_PFTM should be needed for most
    purposes.)
*/

#if defined(HAVE_LONG_DOUBLE)
   typedef long double cson_double_t;
#  ifndef CSON_DOUBLE_T_SFMT
#    define CSON_DOUBLE_T_SFMT "Lf"
#  endif
#  ifndef CSON_DOUBLE_T_PFMT
#    define CSON_DOUBLE_T_PFMT "Lf"
#  endif
#else
   typedef double cson_double_t;
#  ifndef CSON_DOUBLE_T_SFMT
#    define CSON_DOUBLE_T_SFMT "f"
#  endif
#  ifndef CSON_DOUBLE_T_PFMT
#    define CSON_DOUBLE_T_PFMT "f"
#  endif
#endif

/** @def CSON_VOID_PTR_IS_BIG

ONLY define this to a true value if you know that

(sizeof(cson_int_t) <= sizeof(void*))

If that is the case, cson does not need to dynamically
allocate integers. However, enabling this may cause
compilation warnings in 32-bit builds even though the code
being warned about cannot ever be called. To get around such
warnings, when building on a 64-bit environment you can define
this to 1 to get "big" integer support. HOWEVER, all clients must
also use the same value for this macro. If i knew a halfway reliable
way to determine this automatically at preprocessor-time, i would
automate this. We might be able to do halfway reliably by looking
for a large INT_MAX value?
*/
#if !defined(CSON_VOID_PTR_IS_BIG)

/* Largely taken from http://predef.sourceforge.net/prearch.html

See also: http://poshlib.hookatooka.com/poshlib/trac.cgi/browser/posh.h
*/
#  if defined(_WIN64) || defined(__LP64__)/*gcc*/ \
    || defined(_M_X64) || defined(__amd64__) || defined(__amd64) \
    ||  defined(__x86_64__) || defined(__x86_64) \
    || defined(__ia64__) || defined(__ia64) || defined(_IA64) || defined(__IA64__) \
    || defined(_M_IA64) \
    || defined(__sparc_v9__) || defined(__sparcv9) || defined(_ADDR64) \
    || defined(__64BIT__)
#    define CSON_VOID_PTR_IS_BIG 1
#  else
#    define CSON_VOID_PTR_IS_BIG 0
#  endif
#endif

/** @def CSON_INT_T_SFMT

scanf()-compatible format token for cson_int_t.
*/

/** @def CSON_INT_T_PFMT

printf()-compatible format token for cson_int_t.
*/


/** @def CSON_DOUBLE_T_SFMT

scanf()-compatible format token for cson_double_t.
*/

/** @def CSON_DOUBLE_T_PFMT

printf()-compatible format token for cson_double_t.
*/

/**
    Type IDs corresponding to JavaScript/JSON types.

    These are only in the public API to allow O(1) client-side
    dispatching based on cson_value types.
*/
enum cson_type_id {
  /**
    The special "undefined" value constant.

    Its value must be 0 for internal reasons.
 */
 CSON_TYPE_UNDEF = 0,
 /**
    The special "null" value constant.
 */
 CSON_TYPE_NULL = 1,
 /**
    The bool value type.
 */
 CSON_TYPE_BOOL = 2,
 /**
    The integer value type, represented in this library
    by cson_int_t.
 */
 CSON_TYPE_INTEGER = 3,
 /**
    The double value type, represented in this library
    by cson_double_t.
 */
 CSON_TYPE_DOUBLE = 4,
 /** The immutable string type. This library stores strings
    as immutable UTF8.
 */
 CSON_TYPE_STRING = 5,
 /** The "Array" type. */
 CSON_TYPE_ARRAY = 6,
 /** The "Object" type. */
 CSON_TYPE_OBJECT = 7
};
/**
   Convenience typedef.
*/
typedef enum cson_type_id cson_type_id;


/**
   Convenience typedef.
*/
typedef struct cson_value cson_value;

/** @struct cson_value
   
   The core value type of this API. It is opaque to clients, and
   only the cson public API should be used for setting or
   inspecting their values.

   This class is opaque because stack-based usage can easily cause
   leaks if one does not intimately understand the underlying
   internal memory management (which sometimes changes).

   It is (as of 20110323) legal to insert a given value instance into
   multiple containers (they will share ownership using reference
   counting) as long as those insertions do not cause cycles. However,
   be very aware that such value re-use uses a reference to the
   original copy, meaning that if its value is changed once, it is
   changed everywhere. Also beware that multi-threaded write
   operations on such references leads to undefined behaviour.
   
   PLEASE read the ACHTUNGEN below...

   ACHTUNG #1:

   cson_values MUST NOT form cycles (e.g. via object or array
   entries).

   Not abiding th Holy Law Of No Cycles will lead to double-frees and
   the like (i.e. undefined behaviour, likely crashes due to infinite
   recursion or stepping on invalid (freed) pointers).

   ACHTUNG #2:
   
   ALL cson_values returned as non-const cson_value pointers from any
   public functions in the cson API are to be treated as if they are
   heap-allocated, and MUST be freed by client by doing ONE of:
   
   - Passing it to cson_value_free().
   
   - Adding it to an Object or Array, in which case the object/array
   takes over ownership. As of 20110323, a value may be inserted into
   a single container multiple times, or into multiple containers,
   in which case they all share ownership (via reference counting)
   of the original value (meaning any changes to it are visible in
   all references to it).
   
   Each call to cson_value_new_xxx() MUST eventually be followed up
   by one of those options.
   
   Some cson_value_new_XXX() implementations do not actually allocate
   memory, but this is an internal implementation detail. Client code
   MUST NOT rely on this behaviour and MUST treat each object
   returned by such a function as if it was a freshly-allocated copy
   (even if their pointer addresses are the same).
   
   ACHTUNG #3:

   Note that ACHTUNG #2 tells us that we must always free (or transfer
   ownership of) all pointers returned bycson_value_new_xxx(), but
   that two calls to (e.g.) cson_value_new_bool(1) will (or might)
   return the same address. The client must not rely on the
   "non-allocation" policy of such special cases, and must pass each
   returned value to cson_value_free(), even if two of them have the
   same address.  Some special values (e.g. null, true, false, integer
   0, double 0.0, and empty strings) use shared copies and in other
   places reference counting is used internally to figure out when it
   is safe to destroy an object.


   @see cson_value_new_array()
   @see cson_value_new_object()
   @see cson_value_new_string()
   @see cson_value_new_integer()
   @see cson_value_new_double()
   @see cson_value_new_bool()
   @see cson_value_true()
   @see cson_value_false()
   @see cson_value_null()
   @see cson_value_free()
   @see cson_value_type_id()
*/

/** @var cson_rc

   Deprecated: clients are encouraged to use the CSON_RC_xxx values
   which correspond to cson_rc.xxx, as those are more efficient.  Some
   docs and code may still refer to cson_rc, though.

   This object defines the error codes used by cson.

   Library routines which return int values almost always return a
   value from this structure. None of the members in this struct have
   published values except for the OK member, which has the value 0.
   All other values might be incidentally defined where clients
   can see them, but the numbers might change from release to
   release, so clients should only use the symbolic names.

   Client code is expected to access these values via the shared
   cson_rc object, and use them as demonstrated here:

   @code
   int rc = cson_some_func(...);
   if( 0 == rc ) {...success...}
   else if( cson_rc.ArgError == rc ) { ... some argument was wrong ... }
   else if( cson_rc.AllocError == rc ) { ... allocation error ... }
   ...
   @endcode

   Or with the preferred/newer method:

   @code
   int rc = cson_some_func(...);
   switch(rc){
     case 0: ...success...;
     case CSON_RC_ArgError: ... some argument was wrong ...
     case CSON_RC_AllocError: ... allocation error ...
     ...
   }
   @endcode
   
   The entries named Parse_XXX are generally only returned by
   cson_parse() and friends.

   @deprecated
*/

/**
   The CSON_RC_xxx values are intended to replace the older
   cson_rc.xxx values.
*/
enum cson_rc_values {
  /** The generic success value. Guaranteed to be 0. */
 CSON_RC_OK = 0,
 /** Signifies an error in one or more arguments (e.g. NULL where it is not allowed). */
 CSON_RC_ArgError,
 /** Signifies that some argument is not in a valid range. */
 CSON_RC_RangeError,
 /** Signifies that some argument is not of the correct logical cson type. */
 CSON_RC_TypeError,
 /** Signifies an input/ouput error. */
 CSON_RC_IOError,
 /** Signifies an out-of-memory error. */
 CSON_RC_AllocError,
 /** Signifies that the called code is "NYI" (Not Yet Implemented). */
 CSON_RC_NYIError,
 /** Signifies that an internal error was triggered. If it happens, please report this as a bug! */
 CSON_RC_InternalError,
 /** Signifies that the called operation is not supported in the
     current environment. e.g.  missing support from 3rd-party or
     platform-specific code.
 */
 CSON_RC_UnsupportedError,
 /**
    Signifies that the request resource could not be found.
 */
 CSON_RC_NotFoundError,
 /**
    Signifies an unknown error, possibly because an underlying
    3rd-party API produced an error and we have no other reasonable
    error code to convert it to.
 */
 CSON_RC_UnknownError,
 /**
    Signifies that the parser found an unexpected character.
 */
 CSON_RC_Parse_INVALID_CHAR,
 /**
    Signifies that the parser found an invalid keyword (possibly
    an unquoted string).
 */
 CSON_RC_Parse_INVALID_KEYWORD,
 /**
    Signifies that the parser found an invalid escape sequence.
 */
 CSON_RC_Parse_INVALID_ESCAPE_SEQUENCE,
 /**
    Signifies that the parser found an invalid Unicode character
    sequence.
 */
 CSON_RC_Parse_INVALID_UNICODE_SEQUENCE,
 /**
    Signifies that the parser found an invalid numeric token.
 */
 CSON_RC_Parse_INVALID_NUMBER,
 /**
    Signifies that the parser reached its maximum defined
    parsing depth before finishing the input.
 */
 CSON_RC_Parse_NESTING_DEPTH_REACHED,
 /**
    Signifies that the parser found an unclosed object or array.
 */
 CSON_RC_Parse_UNBALANCED_COLLECTION,
 /**
    Signifies that the parser found an key in an unexpected place.
 */
 CSON_RC_Parse_EXPECTED_KEY,
 /**
    Signifies that the parser expected to find a colon but
    found none (e.g. between keys and values in an object).
 */
 CSON_RC_Parse_EXPECTED_COLON
};

/** @struct cson_rc_
   See \ref cson_rc for details.
*/
static const struct cson_rc_
{
    /** The generic success value. Guaranteed to be 0. */
    const int OK;
    /** Signifies an error in one or more arguments (e.g. NULL where it is not allowed). */
    const int ArgError;
    /** Signifies that some argument is not in a valid range. */
    const int RangeError;
    /** Signifies that some argument is not of the correct logical cson type. */
    const int TypeError;
    /** Signifies an input/ouput error. */
    const int IOError;
    /** Signifies an out-of-memory error. */
    const int AllocError;
    /** Signifies that the called code is "NYI" (Not Yet Implemented). */
    const int NYIError;
    /** Signifies that an internal error was triggered. If it happens, please report this as a bug! */
    const int InternalError;
    /** Signifies that the called operation is not supported in the
        current environment. e.g.  missing support from 3rd-party or
        platform-specific code.
    */
    const int UnsupportedError;
    /**
       Signifies that the request resource could not be found.
     */
    const int NotFoundError;
    /**
       Signifies an unknown error, possibly because an underlying
       3rd-party API produced an error and we have no other reasonable
       error code to convert it to.
     */
    const int UnknownError;
    /**
       Signifies that the parser found an unexpected character.
     */
    const int Parse_INVALID_CHAR;
    /**
       Signifies that the parser found an invalid keyword (possibly
       an unquoted string).
     */
    const int Parse_INVALID_KEYWORD;
    /**
       Signifies that the parser found an invalid escape sequence.
     */
    const int Parse_INVALID_ESCAPE_SEQUENCE;
    /**
       Signifies that the parser found an invalid Unicode character
       sequence.
     */
    const int Parse_INVALID_UNICODE_SEQUENCE;
    /**
       Signifies that the parser found an invalid numeric token.
     */
    const int Parse_INVALID_NUMBER;
    /**
       Signifies that the parser reached its maximum defined
       parsing depth before finishing the input.
     */
    const int Parse_NESTING_DEPTH_REACHED;
    /**
       Signifies that the parser found an unclosed object or array.
     */
    const int Parse_UNBALANCED_COLLECTION;
    /**
       Signifies that the parser found an key in an unexpected place.
     */
    const int Parse_EXPECTED_KEY;
    /**
       Signifies that the parser expected to find a colon but
       found none (e.g. between keys and values in an object).
     */
    const int Parse_EXPECTED_COLON;
} cson_rc = {
    CSON_RC_OK,
    CSON_RC_ArgError,
    CSON_RC_RangeError,
    CSON_RC_TypeError,
    CSON_RC_IOError,
    CSON_RC_AllocError,
    CSON_RC_NYIError,
    CSON_RC_InternalError,
    CSON_RC_UnsupportedError,
    CSON_RC_NotFoundError,
    CSON_RC_UnknownError,
    CSON_RC_Parse_INVALID_CHAR,
    CSON_RC_Parse_INVALID_KEYWORD,
    CSON_RC_Parse_INVALID_ESCAPE_SEQUENCE,
    CSON_RC_Parse_INVALID_UNICODE_SEQUENCE,
    CSON_RC_Parse_INVALID_NUMBER,
    CSON_RC_Parse_NESTING_DEPTH_REACHED,
    CSON_RC_Parse_UNBALANCED_COLLECTION,
    CSON_RC_Parse_EXPECTED_KEY,
    CSON_RC_Parse_EXPECTED_COLON
};

/**
   Returns the string form of the cson_rc code corresponding to rc, or
   some unspecified, non-NULL string if it is an unknown code.

   The returned bytes are static and do not changing during the
   lifetime of the application.
*/
char const * cson_rc_string(int rc);

/** @struct cson_parse_opt
   Client-configurable options for the cson_parse() family of
   functions.
*/
struct cson_parse_opt
{
    /**
       Maximum object/array depth to traverse.
    */
    unsigned short maxDepth;
    /**
       Whether or not to allow C-style comments.  Do not rely on this
       option being available. If the underlying parser is replaced,
       this option might no longer be supported.
    */
    char allowComments;
};
typedef struct cson_parse_opt cson_parse_opt;

/**
   Empty-initialized cson_parse_opt object.
*/
#define cson_parse_opt_empty_m { 25/*maxDepth*/, 0/*allowComments*/}


/**
   A class for holding JSON parser information. It is primarily
   intended for finding the position of a parse error.
*/
struct cson_parse_info
{
    /**
       1-based line number.
    */
    unsigned int line;
    /**
       0-based column number.
     */
    unsigned int col;

    /**
       Length, in bytes.
    */
    unsigned int length;
    
    /**
       Error code of the parse run (0 for no error).
    */
    int errorCode;

    /**
       The total number of object keys successfully processed by the
       parser.
    */
    unsigned int totalKeyCount;

    /**
       The total number of object/array values successfully processed
       by the parser, including the root node.
     */
    unsigned int totalValueCount;
};
typedef struct cson_parse_info cson_parse_info;

/**
   Empty-initialized cson_parse_info object.
*/
#define cson_parse_info_empty_m {1/*line*/,\
            0/*col*/,                                   \
            0/*length*/,                                \
            0/*errorCode*/,                             \
            0/*totalKeyCount*/,                         \
            0/*totalValueCount*/                        \
            }
/**
   Empty-initialized cson_parse_info object.
*/
extern const cson_parse_info cson_parse_info_empty;

/**
   Empty-initialized cson_parse_opt object.
*/
extern const cson_parse_opt cson_parse_opt_empty;

/**
    Client-configurable options for the cson_output() family of
    functions.
*/
struct cson_output_opt
{
    /**
       Specifies how to indent (or not) output. The values
       are:

       (0) == no extra indentation.
       
       (1) == 1 TAB character for each level.

       (>1) == that number of SPACES for each level.
    */
    unsigned char indentation;

    /**
       Maximum object/array depth to traverse. Traversing deeply can
       be indicative of cycles in the object/array tree, and this
       value is used to figure out when to abort the traversal.
    */
    unsigned short maxDepth;
    
    /**
       If true, a newline will be added to generated output,
       else not.
    */
    char addNewline;

    /**
       If true, a space will be added after the colon operator
       in objects' key/value pairs.
    */
    char addSpaceAfterColon;

    /**
       If set to 1 then objects/arrays containing only a single value
       will not indent an extra level for that value (but will indent
       on subsequent levels if that value contains multiple values).
    */
    char indentSingleMemberValues;

    /**
       The JSON format allows, but does not require, JSON generators
       to backslash-escape forward slashes. This option enables/disables
       that feature. According to JSON's inventor, Douglas Crockford:

       <quote>
       It is allowed, not required. It is allowed so that JSON can be
       safely embedded in HTML, which can freak out when seeing
       strings containing "</". JSON tolerates "<\/" for this reason.
       </quote>

       (from an email on 2011-04-08)

       The default value is 0 (because it's just damned ugly).
    */
    char escapeForwardSlashes;
};
typedef struct cson_output_opt cson_output_opt;

/**
   Empty-initialized cson_output_opt object.
*/
#define cson_output_opt_empty_m { 0/*indentation*/,\
            25/*maxDepth*/,                             \
            0/*addNewline*/,                            \
            0/*addSpaceAfterColon*/,                    \
            0/*indentSingleMemberValues*/,              \
            0/*escapeForwardSlashes*/                   \
            }

/**
   Empty-initialized cson_output_opt object.
*/
extern const cson_output_opt cson_output_opt_empty;

/**
   Typedef for functions which act as an input source for
   the cson JSON parser.

   The arguments are:

   - state: implementation-specific state needed by the function.

   - n: when called, *n will be the number of bytes the function
   should read and copy to dest. The function MUST NOT copy more than
   *n bytes to dest. Before returning, *n must be set to the number of
   bytes actually copied to dest. If that number is smaller than the
   original *n value, the input is assumed to be completed (thus this
   is not useful with non-blocking readers).

   - dest: the destination memory to copy the data do.

   Must return 0 on success, non-0 on error (preferably a value from
   cson_rc).

   The parser allows this routine to return a partial character from a
   UTF multi-byte character. The input routine does not need to
   concern itself with character boundaries.
*/
typedef int (*cson_data_source_f)( void * state, void * dest, unsigned int * n );

/**
   Typedef for functions which act as an output destination for
   generated JSON.

   The arguments are:

   - state: implementation-specific state needed by the function.

   - n: the length, in bytes, of src.

   - src: the source bytes which the output function should consume.
   The src pointer will be invalidated shortly after this function
   returns, so the implementation must copy or ignore the data, but not
   hold a copy of the src pointer.

   Must return 0 on success, non-0 on error (preferably a value from
   cson_rc).

   These functions are called relatively often during the JSON-output
   process, and should try to be fast.   
*/
typedef int (*cson_data_dest_f)( void * state, void const * src, unsigned int n );

/**
    Reads JSON-formatted string data (in ASCII, UTF8, or UTF16), using the
    src function to fetch all input. This function fetches each input character
    from the source function, which is calls like src(srcState, buffer, bufferSize),
    and processes them. If anything is not JSON-kosher then this function
    fails and returns one of the non-0 cson_rc codes.

    This function is only intended to read root nodes of a JSON tree, either
    a single object or a single array, containing any number of child elements.

    On success, *tgt is assigned the value of the root node of the
    JSON input, and the caller takes over ownership of that memory.
    On error, *tgt is not modified and the caller need not do any
    special cleanup, except possibly for the input source.


    The opt argument may point to an initialized cson_parse_opt object
    which contains any settings the caller wants. If it is NULL then
    default settings (the values defined in cson_parse_opt_empty) are
    used.

    The info argument may be NULL. If it is not NULL then the parser
    populates it with information which is useful in error
    reporting. Namely, it contains the line/column of parse errors.
    
    The srcState argument is ignored by this function but is passed on to src,
    so any output-destination-specific state can be stored there and accessed
    via the src callback.
    
    Non-parse error conditions include:

    - (!tgt) or !src: CSON_RC_ArgError
    - CSON_RC_AllocError can happen at any time during the input phase

    Here's a complete example of using a custom input source:

    @code
    // Internal type to hold state for a JSON input string.
    typedef struct
    {
        char const * str; // start of input string
        char const * pos; // current internal cursor position
        char const * end; // logical EOF (one-past-the-end)
    } StringSource;

    // cson_data_source_f() impl which uses StringSource.
    static int cson_data_source_StringSource( void * state, void * dest,
                                              unsigned int * n )
    {
        StringSource * ss = (StringSource*) state;
        unsigned int i;
        unsigned char * tgt = (unsigned char *)dest;
        if( ! ss || ! n || !dest ) return CSON_RC_ArgError;
        else if( !*n ) return CSON_RC_RangeError;
        for( i = 0;
             (i < *n) && (ss->pos < ss->end);
             ++i, ++ss->pos, ++tgt )
        {
             *tgt = *ss->pos;
        }
        *n = i;
        return 0;
    }

    ...
    // Now use StringSource together with cson_parse()
    StringSource ss;
    cson_value * root = NULL;
    char const * json = "{\"k1\":123}";
    ss.str = ss.pos = json;
    ss.end = json + strlen(json);
    int rc = cson_parse( &root, cson_data_source_StringSource, &ss, NULL, NULL );
    @endcode

    It is recommended that clients wrap such utility code into
    type-safe wrapper functions which also initialize the internal
    state object and check the user-provided parameters for legality
    before passing them on to cson_parse(). For examples of this, see
    cson_parse_FILE() or cson_parse_string().

    TODOs:

    - Buffer the input in larger chunks. We currently read
    byte-by-byte, but i'm too tired to write/test the looping code for
    the buffering.
    
    @see cson_parse_FILE()
    @see cson_parse_string()
*/
int cson_parse( cson_value ** tgt, cson_data_source_f src, void * srcState,
                cson_parse_opt const * opt, cson_parse_info * info );
/**
   A cson_data_source_f() implementation which requires the state argument
   to be a readable (FILE*) handle.
*/
int cson_data_source_FILE( void * state, void * dest, unsigned int * n );

/**
   Equivalent to cson_parse( tgt, cson_data_source_FILE, src, opt ).

   @see cson_parse_filename()
*/
int cson_parse_FILE( cson_value ** tgt, FILE * src,
                     cson_parse_opt const * opt, cson_parse_info * info );

/**
   Convenience wrapper around cson_parse_FILE() which opens the given filename.

   Returns CSON_RC_IOError if the file cannot be opened.

   @see cson_parse_FILE()
*/
int cson_parse_filename( cson_value ** tgt, char const * src,
                         cson_parse_opt const * opt, cson_parse_info * info );

/**
   Uses an internal helper class to pass src through cson_parse().
   See that function for the return value and argument semantics.

   src must be a string containing JSON code, at least len bytes long,
   and the parser will attempt to parse exactly len bytes from src.

   If len is less than 2 (the minimum length of a legal top-node JSON
   object) then CSON_RC_RangeError is returned.
*/
int cson_parse_string( cson_value ** tgt, char const * src, unsigned int len,
                       cson_parse_opt const * opt, cson_parse_info * info );



/**
   Outputs the given value as a JSON-formatted string, sending all
   output to the given callback function. It is intended for top-level
   objects or arrays, but can be used with any cson_value.

   If opt is NULL then default options (the values defined in
   cson_output_opt_empty) are used.

   If opt->maxDepth is exceeded while traversing the value tree,
   CSON_RC_RangeError is returned.

   The destState parameter is ignored by this function and is passed
   on to the dest function.

   Returns 0 on success. On error, any amount of output might have been
   generated before the error was triggered.
   
   Example:

   @code
   int rc = cson_output( myValue, cson_data_dest_FILE, stdout, NULL );
   // basically equivalent to: cson_output_FILE( myValue, stdout, NULL );
   // but note that cson_output_FILE() actually uses different defaults
   // for the output options.
   @endcode
*/
int cson_output( cson_value const * src, cson_data_dest_f dest, void * destState, cson_output_opt const * opt );


/**
   A cson_data_dest_f() implementation which requires the state argument
   to be a writable (FILE*) handle.
*/
int cson_data_dest_FILE( void * state, void const * src, unsigned int n );

/**
   Almost equivalent to cson_output( src, cson_data_dest_FILE, dest, opt ),
   with one minor difference: if opt is NULL then the default options
   always include the addNewline option, since that is normally desired
   for FILE output.

   @see cson_output_filename()
*/
int cson_output_FILE( cson_value const * src, FILE * dest, cson_output_opt const * opt );
/**
   Convenience wrapper around cson_output_FILE() which writes to the given filename, destroying
   any existing contents. Returns CSON_RC_IOError if the file cannot be opened.

   @see cson_output_FILE()
*/
int cson_output_filename( cson_value const * src, char const * dest, cson_output_opt const * fmt );

/**
   Returns the virtual type of v, or CSON_TYPE_UNDEF if !v.
*/
cson_type_id cson_value_type_id( cson_value const * v );


/** Returns true if v is null, v->api is NULL, or v holds the special undefined value. */
char cson_value_is_undef( cson_value const * v );
/** Returns true if v contains a null value. */
char cson_value_is_null( cson_value const * v );
/** Returns true if v contains a bool value. */
char cson_value_is_bool( cson_value const * v );
/** Returns true if v contains an integer value. */
char cson_value_is_integer( cson_value const * v );
/** Returns true if v contains a double value. */
char cson_value_is_double( cson_value const * v );
/** Returns true if v contains a number (double, integer) value. */
char cson_value_is_number( cson_value const * v );
/** Returns true if v contains a string value. */
char cson_value_is_string( cson_value const * v );
/** Returns true if v contains an array value. */
char cson_value_is_array( cson_value const * v );
/** Returns true if v contains an object value. */
char cson_value_is_object( cson_value const * v );

/** @struct cson_object

    cson_object is an opaque handle to an Object value.

    They are used like:

    @code
    cson_object * obj = cson_value_get_object(myValue);
    ...
    @endcode

    They can be created like:

    @code
    cson_value * objV = cson_value_new_object();
    cson_object * obj = cson_value_get_object(objV);
    // obj is owned by objV and objV must eventually be freed
    // using cson_value_free() or added to a container
    // object/array (which transfers ownership to that container).
    @endcode

    @see cson_value_new_object()
    @see cson_value_get_object()
    @see cson_value_free()
*/

typedef struct cson_object cson_object;

/** @struct cson_array

    cson_array is an opaque handle to an Array value.

    They are used like:

    @code
    cson_array * obj = cson_value_get_array(myValue);
    ...
    @endcode

    They can be created like:

    @code
    cson_value * arV = cson_value_new_array();
    cson_array * ar = cson_value_get_array(arV);
    // ar is owned by arV and arV must eventually be freed
    // using cson_value_free() or added to a container
    // object/array (which transfers ownership to that container).
    @endcode

    @see cson_value_new_array()
    @see cson_value_get_array()
    @see cson_value_free()

*/
typedef struct cson_array cson_array;

/** @struct cson_string

   cson-internal string type, opaque to client code. Strings in cson
   are immutable and allocated only by library internals, never
   directly by client code.

   The actual string bytes are to be allocated together in the same
   memory chunk as the cson_string object, which saves us 1 malloc()
   and 1 pointer member in this type (because we no longer have a
   direct pointer to the memory).

   Potential TODOs:

   @see cson_string_cstr()
*/
typedef struct cson_string cson_string;

/**
   Converts the given value to a boolean, using JavaScript semantics depending
   on the concrete type of val:

   undef or null: false
   
   boolean: same
   
   integer, double: 0 or 0.0 == false, else true
   
   object, array: true

   string: length-0 string is false, else true.

   Returns 0 on success and assigns *v (if v is not NULL) to either 0 or 1.
   On error (val is NULL) then v is not modified.
*/
int cson_value_fetch_bool( cson_value const * val, char * v );

/**
   Similar to cson_value_fetch_bool(), but fetches an integer value.

   The conversion, if any, depends on the concrete type of val:

   NULL, null, undefined: *v is set to 0 and 0 is returned.
   
   string, object, array: *v is set to 0 and
   CSON_RC_TypeError is returned. The error may normally be safely
   ignored, but it is provided for those wanted to know whether a direct
   conversion was possible.

   integer: *v is set to the int value and 0 is returned.
   
   double: *v is set to the value truncated to int and 0 is returned.
*/
int cson_value_fetch_integer( cson_value const * val, cson_int_t * v );

/**
   The same conversions and return values as
   cson_value_fetch_integer(), except that the roles of int/double are
   swapped.
*/
int cson_value_fetch_double( cson_value const * val, cson_double_t * v );

/**
   If cson_value_is_string(val) then this function assigns *str to the
   contents of the string. str may be NULL, in which case this function
   functions like cson_value_is_string() but returns 0 on success.

   Returns 0 if val is-a string, else non-0, in which case *str is not
   modified.

   The bytes are owned by the given value and may be invalidated in any of
   the following ways:

   - The value is cleaned up or freed.

   - An array or object containing the value peforms a re-allocation
   (it shrinks or grows).

   And thus the bytes should be consumed before any further operations
   on val or any container which holds it.

   Note that this routine does not convert non-String values to their
   string representations. (Adding that ability would add more
   overhead to every cson_value instance.)
*/
int cson_value_fetch_string( cson_value const * val, cson_string ** str );

/**
   If cson_value_is_object(val) then this function assigns *obj to the underlying
   object value and returns 0, otherwise non-0 is returned and *obj is not modified.

   obj may be NULL, in which case this function works like cson_value_is_object()
   but with inverse return value semantics (0==success) (and it's a few
   CPU cycles slower).

   The *obj pointer is owned by val, and will be invalidated when val
   is cleaned up.

   Achtung: for best results, ALWAYS pass a pointer to NULL as the
   second argument, e.g.:

   @code
   cson_object * obj = NULL;
   int rc = cson_value_fetch_object( val, &obj );

   // Or, more simply:
   obj = cson_value_get_object( val );
   @endcode

   @see cson_value_get_object()
*/
int cson_value_fetch_object( cson_value const * val, cson_object ** obj );

/**
   Identical to cson_value_fetch_object(), but works on array values.

   @see cson_value_get_array()
*/
int cson_value_fetch_array( cson_value const * val, cson_array ** tgt );

/**
   Simplified form of cson_value_fetch_bool(). Returns 0 if val
   is NULL.
*/
char cson_value_get_bool( cson_value const * val );

/**
   Simplified form of cson_value_fetch_integer(). Returns 0 if val
   is NULL.
*/
cson_int_t cson_value_get_integer( cson_value const * val );

/**
   Simplified form of cson_value_fetch_double(). Returns 0.0 if val
   is NULL.
*/
cson_double_t cson_value_get_double( cson_value const * val );

/**
   Simplified form of cson_value_fetch_string(). Returns NULL if val
   is-not-a string value.
*/
cson_string * cson_value_get_string( cson_value const * val );

/**
   Returns a pointer to the NULL-terminated string bytes of str.
   The bytes are owned by string and will be invalided when it
   is cleaned up.

   If str is NULL then NULL is returned. If the string has a length
   of 0 then "" is returned.

   @see cson_string_length_bytes()
   @see cson_value_get_string()
*/
char const * cson_string_cstr( cson_string const * str );

/**
   Convenience function which returns the string bytes of
   the given value if it is-a string, otherwise it returns
   NULL. Note that this does no conversion of non-string types
   to strings.

   Equivalent to cson_string_cstr(cson_value_get_string(val)).
*/
char const * cson_value_get_cstr( cson_value const * val );

/**
   Equivalent to cson_string_cmp_cstr_n(lhs, cson_string_cstr(rhs), cson_string_length_bytes(rhs)).
*/
int cson_string_cmp( cson_string const * lhs, cson_string const * rhs );

/**
   Compares lhs to rhs using memcmp()/strcmp() semantics. Generically
   speaking it returns a negative number if lhs is less-than rhs, 0 if
   they are equivalent, or a positive number if lhs is greater-than
   rhs. It has the following rules for equivalence:

   - The maximum number of bytes compared is the lesser of rhsLen and
   the length of lhs. If the strings do not match, but compare equal
   up to the just-described comparison length, the shorter string is
   considered to be less-than the longer one.
   
   - If lhs and rhs are both NULL, or both have a length of 0 then they will
   compare equal.

   - If lhs is null/length-0 but rhs is not then lhs is considered to be less-than
   rhs.

   - If rhs is null/length-0 but lhs is not then rhs is considered to be less-than
   rhs.

   - i have no clue if the results are exactly correct for UTF strings.

*/
int cson_string_cmp_cstr_n( cson_string const * lhs, char const * rhs, unsigned int rhsLen );

/**
   Equivalent to cson_string_cmp_cstr_n( lhs, rhs, (rhs&&*rhs)?strlen(rhs):0 ).
*/
int cson_string_cmp_cstr( cson_string const * lhs, char const * rhs );

/**
   Returns the length, in bytes, of str, or 0 if str is NULL. This is
   an O(1) operation.

   TODO: add cson_string_length_chars() (is O(N) unless we add another
   member to store the char length).
   
   @see cson_string_cstr()
*/
unsigned int cson_string_length_bytes( cson_string const * str );

/**
    Returns the number of UTF8 characters in str. This value will
    be at most as long as cson_string_length_bytes() for the
    same string, and less if it has multi-byte characters.

    Returns 0 if str is NULL.
*/
unsigned int cson_string_length_utf8( cson_string const * str );

/**
   Like cson_value_get_string(), but returns a copy of the underying
   string bytes, which the caller owns and must eventually free
   using free().
*/
char * cson_value_get_string_copy( cson_value const * val );

/**
   Simplified form of cson_value_fetch_object(). Returns NULL if val
   is-not-a object value.
*/
cson_object * cson_value_get_object( cson_value const * val );

/**
   Simplified form of cson_value_fetch_array(). Returns NULL if val
   is-not-a array value.
*/
cson_array * cson_value_get_array( cson_value const * val );

/**
   Const-correct form of cson_value_get_array().
*/
cson_array const * cson_value_get_array_c( cson_value const * val );

/**
   If ar is-a array and is at least (pos+1) entries long then *v (if v is not NULL)
   is assigned to the value at that position (which may be NULL).

   Ownership of the *v return value is unchanged by this call. (The
   containing array may share ownership of the value with other
   containers.)

   If pos is out of range, non-0 is returned and *v is not modified.

   If v is NULL then this function returns 0 if pos is in bounds, but does not
   otherwise return a value to the caller.
*/
int cson_array_value_fetch( cson_array const * ar, unsigned int pos, cson_value ** v );

/**
   Simplified form of cson_array_value_fetch() which returns NULL if
   ar is NULL, pos is out of bounds or if ar has no element at that
   position.
*/
cson_value * cson_array_get( cson_array const * ar, unsigned int pos );

/**
   Ensures that ar has allocated space for at least the given
   number of entries. This never shrinks the array and never
   changes its logical size, but may pre-allocate space in the
   array for storing new (as-yet-unassigned) values.

   Returns 0 on success, or non-zero on error:

   - If ar is NULL: CSON_RC_ArgError

   - If allocation fails: CSON_RC_AllocError
*/
int cson_array_reserve( cson_array * ar, unsigned int size );

/**
   If ar is not NULL, sets *v (if v is not NULL) to the length of the array
   and returns 0. Returns CSON_RC_ArgError if ar is NULL.
*/
int cson_array_length_fetch( cson_array const * ar, unsigned int * v );

/**
   Simplified form of cson_array_length_fetch() which returns 0 if ar
   is NULL.
*/
unsigned int cson_array_length_get( cson_array const * ar );

/**
   Sets the given index of the given array to the given value.

   If ar already has an item at that index then it is cleaned up and
   freed before inserting the new item.

   ar is expanded, if needed, to be able to hold at least (ndx+1)
   items, and any new entries created by that expansion are empty
   (NULL values).

   On success, 0 is returned and ownership of v is transfered to ar.
  
   On error ownership of v is NOT modified, and the caller may still
   need to clean it up. For example, the following code will introduce
   a leak if this function fails:

   @code
   cson_array_append( myArray, cson_value_new_integer(42) );
   @endcode

   Because the value created by cson_value_new_integer() has no owner
   and is not cleaned up. The "more correct" way to do this is:

   @code
   cson_value * v = cson_value_new_integer(42);
   int rc = cson_array_append( myArray, v );
   if( 0 != rc ) {
      cson_value_free( v );
      ... handle error ...
   }
   @endcode

*/
int cson_array_set( cson_array * ar, unsigned int ndx, cson_value * v );

/**
   Appends the given value to the given array, transfering ownership of
   v to ar. On error, ownership of v is not modified. Ownership of ar
   is never changed by this function.

   This is functionally equivalent to
   cson_array_set(ar,cson_array_length_get(ar),v), but this
   implementation has slightly different array-preallocation policy
   (it grows more eagerly).
   
   Returns 0 on success, non-zero on error. Error cases include:

   - ar or v are NULL: CSON_RC_ArgError

   - Array cannot be expanded to hold enough elements: CSON_RC_AllocError.

   - Appending would cause a numeric overlow in the array's size:
   CSON_RC_RangeError.  (However, you'll get an AllocError long before
   that happens!)

   On error ownership of v is NOT modified, and the caller may still
   need to clean it up. See cson_array_set() for the details.

*/
int cson_array_append( cson_array * ar, cson_value * v );


/**
   Creates a new cson_value from the given boolean value.

   Ownership of the new value is passed to the caller, who must
   eventually either free the value using cson_value_free() or
   inserting it into a container (array or object), which transfers
   ownership to the container. See the cson_value class documentation
   for more details.

   Semantically speaking this function Returns NULL on allocation
   error, but the implementation never actually allocates for this
   case. Nonetheless, it must be treated as if it were an allocated
   value.
*/
cson_value * cson_value_new_bool( char v );


/**
   Alias for cson_value_new_bool(v).
*/
cson_value * cson_new_bool(char v);

/**
   Returns the special JSON "null" value. When outputing JSON,
   its string representation is "null" (without the quotes).
   
   See cson_value_new_bool() for notes regarding the returned
   value's memory.
*/
cson_value * cson_value_null( void );

/**
   Equivalent to cson_value_new_bool(1).
*/
cson_value * cson_value_true( void );

/**
   Equivalent to cson_value_new_bool(0).
*/
cson_value * cson_value_false( void );

/**
   Semantically the same as cson_value_new_bool(), but for integers.
*/
cson_value * cson_value_new_integer( cson_int_t v );

/**
   Alias for cson_value_new_integer(v).
*/
cson_value * cson_new_int(cson_int_t v);

/**
   Semantically the same as cson_value_new_bool(), but for doubles.
*/
cson_value * cson_value_new_double( cson_double_t v );

/**
   Alias for cson_value_new_double(v).
*/
cson_value * cson_new_double(cson_double_t v);

/**
   Semantically the same as cson_value_new_bool(), but for strings.
   This creates a JSON value which copies the first n bytes of str.
   The string will automatically be NUL-terminated.
   
   Note that if str is NULL or n is 0, this function still
   returns non-NULL value representing that empty string.
   
   Returns NULL on allocation error.
   
   See cson_value_new_bool() for important information about the
   returned memory.
*/
cson_value * cson_value_new_string( char const * str, unsigned int n );

/**
   Allocates a new "object" value and transfers ownership of it to the
   caller. It must eventually be destroyed, by the caller or its
   owning container, by passing it to cson_value_free().

   Returns NULL on allocation error.

   Post-conditions: cson_value_is_object(value) will return true.

   @see cson_value_new_array()
   @see cson_value_free()
*/
cson_value * cson_value_new_object( void );

/**
   This works like cson_value_new_object() but returns an Object
   handle directly.

   The value handle for the returned object can be fetched with
   cson_object_value(theObject).
   
   Ownership is transfered to the caller, who must eventually free it
   by passing the Value handle (NOT the Object handle) to
   cson_value_free() or passing ownership to a parent container.

   Returns NULL on error (out of memory).
*/
cson_object * cson_new_object( void );

/**
   Identical to cson_new_object() except that it creates
   an Array.
*/
cson_array * cson_new_array( void );

/**
   Identical to cson_new_object() except that it creates
   a String.
*/
cson_string * cson_new_string(char const * val, unsigned int len);

/**
   Equivalent to cson_value_free(cson_object_value(x)).
*/
void cson_free_object(cson_object *x);

/**
   Equivalent to cson_value_free(cson_array_value(x)).
*/
void cson_free_array(cson_array *x);

/**
   Equivalent to cson_value_free(cson_string_value(x)).
*/
void cson_free_string(cson_string *x);


/**
   Allocates a new "array" value and transfers ownership of it to the
   caller. It must eventually be destroyed, by the caller or its
   owning container, by passing it to cson_value_free().

   Returns NULL on allocation error.

   Post-conditions: cson_value_is_array(value) will return true.

   @see cson_value_new_object()
   @see cson_value_free()
*/
cson_value * cson_value_new_array( void );

/**
   Frees any resources owned by v, then frees v. If v is a container
   type (object or array) its children are also freed (recursively).

   If v is NULL, this is a no-op.

   This function decrements a reference count and only destroys the
   value if its reference count drops to 0. Reference counts are
   increased by either inserting the value into a container or via
   cson_value_add_reference(). Even if this function does not
   immediately destroy the value, the value must be considered, from
   the perspective of that client code, to have been
   destroyed/invalidated by this call.

   
   @see cson_value_new_object()
   @see cson_value_new_array()
   @see cson_value_add_reference()
*/
void cson_value_free(cson_value * v);

/**
   Alias for cson_value_free().
*/
void cson_free_value(cson_value * v);


/**
   Functionally similar to cson_array_set(), but uses a string key
   as an index. Like arrays, if a value already exists for the given key,
   it is destroyed by this function before inserting the new value.

   If v is NULL then this call is equivalent to
   cson_object_unset(obj,key). Note that (v==NULL) is treated
   differently from v having the special null value. In the latter
   case, the key is set to the special null value.

   The key may be encoded as ASCII or UTF8. Results are undefined
   with other encodings, and the errors won't show up here, but may
   show up later, e.g. during output.
   
   Returns 0 on success, non-0 on error. It has the following error
   cases:

   - CSON_RC_ArgError: obj or key are NULL or strlen(key) is 0.

   - CSON_RC_AllocError: an out-of-memory error

   On error ownership of v is NOT modified, and the caller may still
   need to clean it up. For example, the following code will introduce
   a leak if this function fails:

   @code
   cson_object_set( myObj, "foo", cson_value_new_integer(42) );
   @endcode

   Because the value created by cson_value_new_integer() has no owner
   and is not cleaned up. The "more correct" way to do this is:

   @code
   cson_value * v = cson_value_new_integer(42);
   int rc = cson_object_set( myObj, "foo", v );
   if( 0 != rc ) {
      cson_value_free( v );
      ... handle error ...
   }
   @endcode

   Potential TODOs:

   - Add an overload which takes a cson_value key instead. To get
   any value out of that we first need to be able to convert arbitrary
   value types to strings. We could simply to-JSON them and use those
   as keys.
*/
int cson_object_set( cson_object * obj, char const * key, cson_value * v );

/**
   Functionaly equivalent to cson_object_set(), but takes a
   cson_string() as its KEY type. The string will be reference-counted
   like any other values, and the key may legally be used within this
   same container (as a value) or others (as a key or value) at the
   same time.

   Returns 0 on success. On error, ownership (i.e. refcounts) of key
   and value are not modified. On success key and value will get
   increased refcounts unless they are replacing themselves (which is
   a harmless no-op).
*/
int cson_object_set_s( cson_object * obj, cson_string * key, cson_value * v );

/**
   Removes a property from an object.
   
   If obj contains the given key, it is removed and 0 is returned. If
   it is not found, CSON_RC_NotFoundError is returned (which can
   normally be ignored by client code).

   CSON_RC_ArgError is returned if obj or key are NULL or key has
   a length of 0.

   Returns 0 if the given key is found and removed.

   This is functionally equivalent calling
   cson_object_set(obj,key,NULL).
*/
int cson_object_unset( cson_object * obj, char const * key );

/**
   Searches the given object for a property with the given key. If found,
   it is returned. If no match is found, or any arguments are NULL, NULL is
   returned. The returned object is owned by obj, and may be invalidated
   by ANY operations which change obj's property list (i.e. add or remove
   properties).

   FIXME: allocate the key/value pairs like we do for cson_array,
   to get improve the lifetimes of fetched values.

   @see cson_object_fetch_sub()
   @see cson_object_get_sub()
*/
cson_value * cson_object_get( cson_object const * obj, char const * key );

/**
   Equivalent to cson_object_get() but takes a cson_string argument
   instead of a C-style string.
*/
cson_value * cson_object_get_s( cson_object const * obj, cson_string const *key );

/**
   Similar to cson_object_get(), but removes the value from the parent
   object's ownership. If no item is found then NULL is returned, else
   the object (now owned by the caller or possibly shared with other
   containers) is returned.

   Returns NULL if either obj or key are NULL or key has a length
   of 0.

   This function reduces the returned value's reference count but has
   the specific property that it does not treat refcounts 0 and 1
   identically, meaning that the returned object may have a refcount
   of 0. This behaviour works around a corner-case where we want to
   extract a child element from its parent and then destroy the parent
   (which leaves us in an undesireable (normally) reference count
   state).
*/
cson_value * cson_object_take( cson_object * obj, char const * key );

/**
    Fetches a property from a child (or [great-]*grand-child) object.

    obj is the object to search.

    path is a delimited string, where the delimiter is the given
    separator character.

    This function searches for the given path, starting at the given object
    and traversing its properties as the path specifies. If a given part of the
    path is not found, then this function fails with CSON_RC_NotFoundError.

    If it finds the given path, it returns the value by assiging *tgt
    to it.  If tgt is NULL then this function has no side-effects but
    will return 0 if the given path is found within the object, so it can be used
    to test for existence without fetching it.
    
    Returns 0 if it finds an entry, CSON_RC_NotFoundError if it finds
    no item, and any other non-zero error code on a "real" error. Errors include:

   - obj or path are NULL: CSON_RC_ArgError
    
    - separator is 0, or path is an empty string or contains only
    separator characters: CSON_RC_RangeError

    - There is an upper limit on how long a single path component may
    be (some "reasonable" internal size), and CSON_RC_RangeError is
    returned if that length is violated.

    
    Limitations:

    - It has no way to fetch data from arrays this way. i could
    imagine, e.g., a path of "subobj.subArray.0" for
    subobj.subArray[0], or "0.3.1" for [0][3][1]. But i'm too
    lazy/tired to add this.

    Example usage:
    

    Assume we have a JSON structure which abstractly looks like:

    @code
    {"subobj":{"subsubobj":{"myValue":[1,2,3]}}}
    @endcode

    Out goal is to get the value of myValue. We can do that with:

    @code
    cson_value * v = NULL;
    int rc = cson_object_fetch_sub( object, &v, "subobj.subsubobj.myValue", '.' );
    @endcode

    Note that because keys in JSON may legally contain a '.', the
    separator must be specified by the caller. e.g. the path
    "subobj/subsubobj/myValue" with separator='/' is equivalent the
    path "subobj.subsubobj.myValue" with separator='.'. The value of 0
    is not legal as a separator character because we cannot
    distinguish that use from the real end-of-string without requiring
    the caller to also pass in the length of the string.
   
    Multiple successive separators in the list are collapsed into a
    single separator for parsing purposes. e.g. the path "a...b...c"
    (separator='.') is equivalent to "a.b.c".

    @see cson_object_get_sub()
    @see cson_object_get_sub2()
*/
int cson_object_fetch_sub( cson_object const * obj, cson_value ** tgt, char const * path, char separator );

/**
   Similar to cson_object_fetch_sub(), but derives the path separator
   character from the first byte of the path argument. e.g. the
   following arg equivalent:

   @code
   cson_object_fetch_sub( obj, &tgt, "foo.bar.baz", '.' );
   cson_object_fetch_sub2( obj, &tgt, ".foo.bar.baz" );
   @endcode
*/
int cson_object_fetch_sub2( cson_object const * obj, cson_value ** tgt, char const * path );

/**
   Convenience form of cson_object_fetch_sub() which returns NULL if the given
   item is not found.
*/
cson_value * cson_object_get_sub( cson_object const * obj, char const * path, char sep );

/**
   Convenience form of cson_object_fetch_sub2() which returns NULL if the given
   item is not found.
*/
cson_value * cson_object_get_sub2( cson_object const * obj, char const * path );

/** @enum CSON_MERGE_FLAGS

    Flags for cson_object_merge().
*/
enum CSON_MERGE_FLAGS {
    CSON_MERGE_DEFAULT = 0,
    CSON_MERGE_REPLACE = 0x01,
    CSON_MERGE_NO_RECURSE = 0x02
};

/**
   "Merges" the src object's properties into dest. Each property in
   src is copied (using reference counting, not cloning) into dest. If
   dest already has the given property then behaviour depends on the
   flags argument:

   If flag has the CSON_MERGE_REPLACE bit set then this function will
   by default replace non-object properties with the src property. If
   src and dest both have the property AND it is an Object then this
   function operates recursively on those objects. If
   CSON_MERGE_NO_RECURSE is set then objects are not recursed in this
   manner, and will be completely replaced if CSON_MERGE_REPLACE is
   set.

   Array properties in dest are NOT recursed for merging - they are
   either replaced or left as-is, depending on whether flags contains
   he CSON_MERGE_REPLACE bit.

   Returns 0 on success. The error conditions are:

   - dest or src are NULL or (dest==src) returns CSON_RC_ArgError.

   - dest or src contain cyclic references - this will likely cause a
   crash due to endless recursion.

   Potential TODOs:

   - Add a flag to copy clones, not the original values.
*/
int cson_object_merge( cson_object * dest, cson_object const * src, int flags );


/**
   An iterator type for traversing object properties.

   Its values must be considered private, not to be touched by client
   code.

   @see cson_object_iter_init()
   @see cson_object_iter_next()
*/
struct cson_object_iterator
{
    
    /** @internal
        The underlying object.
    */
    cson_object const * obj;
    /** @internal
        Current position in the property list.
     */
    unsigned int pos;
};
typedef struct cson_object_iterator cson_object_iterator;

/**
   Empty-initialized cson_object_iterator object.
*/
#define cson_object_iterator_empty_m {NULL/*obj*/,0/*pos*/}

/**
   Empty-initialized cson_object_iterator object.
*/
extern const cson_object_iterator cson_object_iterator_empty;

/**
   Initializes the given iterator to point at the start of obj's
   properties. Returns 0 on success or CSON_RC_ArgError if !obj
   or !iter.

   obj must outlive iter, or results are undefined. Results are also
   undefined if obj is modified while the iterator is active.

   @see cson_object_iter_next()
*/
int cson_object_iter_init( cson_object const * obj, cson_object_iterator * iter );

/** @struct cson_kvp

This class represents a key/value pair and is used for storing
object properties. It is opaque to client code, and the public
API only uses this type for purposes of iterating over cson_object
properties using the cson_object_iterator interfaces.
*/

typedef struct cson_kvp cson_kvp;

/**
   Returns the next property from the given iterator's object, or NULL
   if the end of the property list as been reached.

   Note that the order of object properties is undefined by the API,
   and may change from version to version.

   The returned memory belongs to the underlying object and may be
   invalidated by any changes to that object.

   Example usage:

   @code
   cson_object_iterator it;
   cson_object_iter_init( myObject, &it ); // only fails if either arg is 0
   cson_kvp * kvp;
   cson_string const * key;
   cson_value const * val;
   while( (kvp = cson_object_iter_next(&it) ) )
   {
       key = cson_kvp_key(kvp);
       val = cson_kvp_value(kvp);
       ...
   }
   @endcode

   There is no need to clean up an iterator, as it holds no dynamic resources.
   
   @see cson_kvp_key()
   @see cson_kvp_value()
*/
cson_kvp * cson_object_iter_next( cson_object_iterator * iter );


/**
   Returns the key associated with the given key/value pair,
   or NULL if !kvp. The memory is owned by the object which contains
   the key/value pair, and may be invalidated by any modifications
   to that object.
*/
cson_string * cson_kvp_key( cson_kvp const * kvp );

/**
   Returns the value associated with the given key/value pair,
   or NULL if !kvp. The memory is owned by the object which contains
   the key/value pair, and may be invalidated by any modifications
   to that object.
*/
cson_value * cson_kvp_value( cson_kvp const * kvp );

/** @typedef some unsigned int type cson_size_t

*/
typedef unsigned int cson_size_t;

/**
   A generic buffer class.

   They can be used like this:

   @code
   cson_buffer b = cson_buffer_empty;
   int rc = cson_buffer_reserve( &buf, 100 );
   if( 0 != rc ) { ... allocation error ... }
   ... use buf.mem ...
   ... then free it up ...
   cson_buffer_reserve( &buf, 0 );
   @endcode

   To take over ownership of a buffer's memory:

   @code
   void * mem = b.mem;
   // mem is b.capacity bytes long, but only b.used
   // bytes of it has been "used" by the API.
   b = cson_buffer_empty;
   @endcode

   The memory now belongs to the caller and must eventually be
   free()d.
*/
struct cson_buffer
{
    /**
       The number of bytes allocated for this object.
       Use cson_buffer_reserve() to change its value.
     */
    cson_size_t capacity;
    /**
       The number of bytes "used" by this object. It is not needed for
       all use cases, and management of this value (if needed) is up
       to the client. The cson_buffer public API does not use this
       member. The intention is that this can be used to track the
       length of strings which are allocated via cson_buffer, since
       they need an explicit length and/or null terminator.
     */
    cson_size_t used;

    /**
       This is a debugging/metric-counting value
       intended to help certain malloc()-conscious
       clients tweak their memory reservation sizes.
       Each time cson_buffer_reserve() expands the
       buffer, it increments this value by 1.
    */
    cson_size_t timesExpanded;

    /**
       The memory allocated for and owned by this buffer.
       Use cson_buffer_reserve() to change its size or
       free it. To take over ownership, do:

       @code
       void * myptr = buf.mem;
       buf = cson_buffer_empty;
       @endcode

       (You might also need to store buf.used and buf.capacity,
       depending on what you want to do with the memory.)
       
       When doing so, the memory must eventually be passed to free()
       to deallocate it.
    */
    unsigned char * mem;
};
/** Convenience typedef. */
typedef struct cson_buffer cson_buffer;

/** An empty-initialized cson_buffer object. */
#define cson_buffer_empty_m {0/*capacity*/,0/*used*/,0/*timesExpanded*/,NULL/*mem*/}
/** An empty-initialized cson_buffer object. */
extern const cson_buffer cson_buffer_empty;

/**
   Uses cson_output() to append all JSON output to the given buffer
   object. The semantics for the (v, opt) parameters, and the return
   value, are as documented for cson_output(). buf must be a non-NULL
   pointer to a properly initialized buffer (see example below).

   Ownership of buf is not changed by calling this.

   On success 0 is returned and the contents of buf.mem are guaranteed
   to be NULL-terminated. On error the buffer might contain partial
   contents, and it should not be used except to free its contents.

   On error non-zero is returned. Errors include:

   - Invalid arguments: CSON_RC_ArgError

   - Buffer cannot be expanded (runs out of memory): CSON_RC_AllocError
   
   Example usage:

   @code
   cson_buffer buf = cson_buffer_empty;
   // optional: cson_buffer_reserve(&buf, 1024 * 10);
   int rc = cson_output_buffer( myValue, &buf, NULL );
   if( 0 != rc ) {
       ... error! ...
   }
   else {
       ... use buffer ...
       puts((char const*)buf.mem);
   }
   // In both cases, we eventually need to clean up the buffer:
   cson_buffer_reserve( &buf, 0 );
   // Or take over ownership of its memory:
   {
       char * mem = (char *)buf.mem;
       buf = cson_buffer_empty;
       ...
       free(mem);
   }
   @endcode
   
   @see cson_output()
   
*/
int cson_output_buffer( cson_value const * v, cson_buffer * buf,
                        cson_output_opt const * opt );

/**
   This works identically to cson_parse_string(), but takes a
   cson_buffer object as its input.  buf->used bytes of buf->mem are
   assumed to be valid JSON input, but it need not be NUL-terminated
   (we only read up to buf->used bytes). The value of buf->used is
   assumed to be the "string length" of buf->mem, i.e. not including
   the NUL terminator.

   Returns 0 on success, non-0 on error.

   See cson_parse() for the semantics of the tgt, opt, and err
   parameters.
*/
int cson_parse_buffer( cson_value ** tgt, cson_buffer const * buf,
                       cson_parse_opt const * opt, cson_parse_info * err );


/**
   Reserves the given amount of memory for the given buffer object.

   If n is 0 then buf->mem is freed and its state is set to
   NULL/0 values.

   If buf->capacity is less than or equal to n then 0 is returned and
   buf is not modified.

   If n is larger than buf->capacity then buf->mem is (re)allocated
   and buf->capacity contains the new length. Newly-allocated bytes
   are filled with zeroes.

   On success 0 is returned. On error non-0 is returned and buf is not
   modified.

   buf->mem is owned by buf and must eventually be freed by passing an
   n value of 0 to this function.

   buf->used is never modified by this function unless n is 0, in which case
   it is reset.
*/
int cson_buffer_reserve( cson_buffer * buf, cson_size_t n );

/**
   Fills all bytes of the given buffer with the given character.
   Returns the number of bytes set (buf->capacity), or 0 if
   !buf or buf has no memory allocated to it.
*/
cson_size_t cson_buffer_fill( cson_buffer * buf, char c );

/**
    Uses a cson_data_source_f() function to buffer input into a
    cson_buffer.

   dest must be a non-NULL, initialized (though possibly empty)
   cson_buffer object. Its contents, if any, will be overwritten by
   this function, and any memory it holds might be re-used.

   The src function is called, and passed the state parameter, to
   fetch the input. If it returns non-0, this function returns that
   error code. src() is called, possibly repeatedly, until it reports
   that there is no more data.

   Whether or not this function succeeds, dest still owns any memory
   pointed to by dest->mem, and the client must eventually free it by
   calling cson_buffer_reserve(dest,0).

   dest->mem might (and possibly will) be (re)allocated by this
   function, so any pointers to it held from before this call might be
   invalidated by this call.
   
   On error non-0 is returned and dest has almost certainly been
   modified but its state must be considered incomplete.

   Errors include:

   - dest or src are NULL (CSON_RC_ArgError)

   - Allocation error (CSON_RC_AllocError)

   - src() returns an error code

   Whether or not the state parameter may be NULL depends on
   the src implementation requirements.

   On success dest will contain the contents read from the input
   source. dest->used will be the length of the read-in data, and
   dest->mem will point to the memory. dest->mem is automatically
   NUL-terminated if this function succeeds, but dest->used does not
   count that terminator. On error the state of dest->mem must be
   considered incomplete, and is not guaranteed to be NUL-terminated.

    Example usage:

    @code
    cson_buffer buf = cson_buffer_empty;
    int rc = cson_buffer_fill_from( &buf,
                                    cson_data_source_FILE,
                                    stdin );
    if( rc )
    {
        fprintf(stderr,"Error %d (%s) while filling buffer.\n",
                rc, cson_rc_string(rc));
        cson_buffer_reserve( &buf, 0 );
        return ...;
    }
    ... use the buf->mem ...
    ... clean up the buffer ...
    cson_buffer_reserve( &buf, 0 );
    @endcode

    To take over ownership of the buffer's memory, do:

    @code
    void * mem = buf.mem;
    buf = cson_buffer_empty;
    @endcode

    In which case the memory must eventually be passed to free() to
    free it.    
*/
int cson_buffer_fill_from( cson_buffer * dest, cson_data_source_f src, void * state );


/**
   Increments the reference count for the given value. This is a
   low-level operation and should not normally be used by client code
   without understanding exactly what side-effects it introduces.
   Mis-use can lead to premature destruction or cause a value instance
   to never be properly destructed (i.e. a memory leak).

   This function is probably only useful for the following cases:

   - You want to hold a reference to a value which is itself contained
   in one or more containers, and you need to be sure that your
   reference outlives the container(s) and/or that you can free your
   copy of the reference without invaliding any references to the same
   value held in containers.

   - You want to implement "value sharing" behaviour without using an
   object or array to contain the shared value. This can be used to
   ensure the lifetime of the shared value instance. Each sharing
   point adds a reference and simply passed the value to
   cson_value_free() when they're done. The object will be kept alive
   for other sharing points which added a reference.

   Normally any such value handles would be invalidated when the
   parent container(s) is/are cleaned up, but this function can be
   used to effectively delay the cleanup.
   
   This function, at its lowest level, increments the value's
   reference count by 1.

   To decrement the reference count, pass the value to
   cson_value_free(), after which the value must be considered, from
   the perspective of that client code, to be destroyed (though it
   will not be if there are still other live references to
   it). cson_value_free() will not _actually_ destroy the value until
   its reference count drops to 0.

   Returns 0 on success. The only error conditions are if v is NULL
   (CSON_RC_ArgError) or if the reference increment would overflow
   (CSON_RC_RangeError). In theory a client would get allocation
   errors long before the reference count could overflow (assuming
   those reference counts come from container insertions, as opposed
   to via this function).

   Insider notes which clients really need to know:
   
   For shared/constant value instances, such as those returned by
   cson_value_true() and cson_value_null(), this function has no side
   effects - it does not actually modify the reference count because
   (A) those instances are shared across all client code and (B) those
   objects are static and never get cleaned up. However, that is an
   implementation detail which client code should not rely on. In
   other words, if you call cson_value_add_reference() 3 times using
   the value returned by cson_value_true() (which is incidentally a
   shared cson_value instance), you must eventually call
   cson_value_free() 3 times to (semantically) remove those
   references. However, internally the reference count for that
   specific cson_value instance will not be modified and those
   objects will never be freed (they're stack-allocated).

   It might be interesting to note that newly-created objects
   have a reference count of 0 instead of 1. This is partly because
   if the initial reference is counted then it makes ownership
   problematic when inserting values into containers. e.g. consider the
   following code:

   @code
   // ACHTUNG: this code is hypothetical and does not reflect
   // what actually happens!
   cson_value * v =
        cson_value_new_integer( 42 ); // v's refcount = 1
   cson_array_append( myArray, v ); // v's refcount = 2
   @endcode

   If that were the case, the client would be forced to free his own
   reference after inserting it into the container (which is a bit
   counter-intuitive as well as intrusive). It would look a bit like
   the following and would have to be done after every create/insert
   operation:

   @code
   // ACHTUNG: this code is hypothetical and does not reflect
   // what actually happens!
   cson_array_append( myArray, v ); // v's refcount = 2
   cson_value_free( v ); // v's refcount = 1
   @endcode

   (As i said: it's counter-intuitive and intrusive.)

   Instead, values start with a refcount of 0 and it is only increased
   when the value is added to an object/array container or when this
   function is used to manually increment it. cson_value_free() treats
   a refcount of 0 or 1 equivalently, destroying the value
   instance. The only semantic difference between 0 and 1, for
   purposes of cleaning up, is that a value with a non-0 refcount has
   been had its refcount adjusted, whereas a 0 refcount indicates a
   fresh, "unowned" reference.
*/
int cson_value_add_reference( cson_value * v );

#if 0
/**
   DO NOT use this unless you know EXACTLY what you're doing.
   It is only in the public API to work around a couple corner
   cases involving extracting child elements and discarding
   their parents.

   This function sets v's reference count to the given value.
   It does not clean up the object if rc is 0.

   Returns 0 on success, non-0 on error.
*/
int cson_value_refcount_set( cson_value * v, unsigned short rc );
#endif

/**
   Deeply copies a JSON value, be it an object/array or a "plain"
   value (e.g. number/string/boolean). If cv is not NULL then this
   function makes a deep clone of it and returns that clone. Ownership
   of the clone is identical t transfered to the caller, who must
   eventually free the value using cson_value_free() or add it to a
   container object/array to transfer ownership to the container. The
   returned object will be of the same logical type as orig.

   ACHTUNG: if orig contains any cyclic references at any depth level
   this function will endlessly recurse. (Having _any_ cyclic
   references violates this library's requirements.)
   
   Returns NULL if orig is NULL or if cloning fails. Assuming that
   orig is in a valid state, the only "likely" error case is that an
   allocation fails while constructing the clone. In other words, if
   cloning fails due to something other than an allocation error then
   either orig is in an invalid state or there is a bug.

   When this function clones Objects or Arrays it shares any immutable
   values (including object keys) between the parent and the
   clone. Mutable values (Objects and Arrays) are copied, however.
   For example, if we clone:

   @code
   { a: 1, b: 2, c:["hi"] }
   @endcode

   The cloned object and the array "c" would be a new Object/Array
   instances but the object keys (a,b,b) and the values of (a,b), as
   well as the string value within the "c" array, would be shared
   between the original and the clone. The "c" array itself would be
   deeply cloned, such that future changes to the clone are not
   visible to the parent, and vice versa, but immutable values within
   the array are shared (in this case the string "hi"). The
   justification for this heuristic is that immutable values can never
   be changed, so there is no harm in sharing them across
   clones. Additionally, such types can never contribute to cycles in
   a JSON tree, so they are safe to share this way. Objects and
   Arrays, on the other hand, can be modified later and can contribute
   to cycles, and thus the clone needs to be an independent instance.
   Note, however, that if this function directly passed a
   non-Object/Array, that value is deeply cloned. The sharing
   behaviour only applies when traversing Objects/Arrays.
*/
cson_value * cson_value_clone( cson_value const * orig );

/**
   Returns the value handle associated with s. The handle itself owns
   s, and ownership of the handle is not changed by calling this
   function. If the returned handle is part of a container, calling
   cson_value_free() on the returned handle invoked undefined
   behaviour (quite possibly downstream when the container tries to
   use it).

   This function only returns NULL if s is NULL. The length of the
   returned string is cson_string_length_bytes().
*/
cson_value * cson_string_value(cson_string const * s);
/**
   The Object form of cson_string_value(). See that function
   for full details.
*/
cson_value * cson_object_value(cson_object const * s);

/**
   The Array form of cson_string_value(). See that function
   for full details.
*/
cson_value * cson_array_value(cson_array const * s);


/**
   Calculates the approximate in-memory-allocated size of v,
   recursively if it is a container type, with the following caveats
   and limitations:

   If a given value is reference counted and encountered multiple
   times within a traversed container, each reference is counted at
   full cost. We have no way of knowing if a given reference has been
   visited already and whether it should or should not be counted, so
   we pessimistically count them even though the _might_ not really
   count for the given object tree (it depends on where the other open
   references live).

   This function returns 0 if any of the following are true:

   - v is NULL

   - v is one of the special singleton values (null, bools, empty
   string, int 0, double 0.0)

   All other values require an allocation, and this will return their
   total memory cost, including the cson-specific internals and the
   native value(s).

   Note that because arrays and objects might have more internal slots
   allocated than used, the alloced size of a container does not
   necessarily increase when a new item is inserted into it. An interesting
   side-effect of this is that when cson_clone()ing an array or object, the
   size of the clone can actually be less than the original.
*/
unsigned int cson_value_msize(cson_value const * v);

/**
   Parses command-line-style arguments into a JSON object.

   It expects arguments to be in any of these forms, and any number
   of leading dashes are treated identically:

   --key : Treats key as a boolean with a true value.

   --key=VAL : Treats VAL as either a double, integer, or string.

   --key= : Treats key as a JSON null (not literal NULL) value.

   Arguments not starting with a dash are skipped.
   
   Each key/value pair is inserted into an object.  If a given key
   appears more than once then only the final entry is actually
   stored.

   argc and argv are expected to be values from main() (or similar,
   possibly adjusted to remove argv[0]).

   tgt must be either a pointer to NULL or a pointer to a
   client-provided Object. If (NULL==*tgt) then this function
   allocates a new object and on success it stores the new object in
   *tgt (it is owned by the caller). If (NULL!=*tgt) then it is
   assumed to be a properly allocated object. DO NOT pass a pointer to
   an unitialized pointer, as that will fool this function into
   thinking it is a valid object and Undefined Behaviour will ensue.

   If count is not NULL then the number of arugments parsed by this
   function are assigned to it. On error, count will be the number of
   options successfully parsed before the error was encountered.

   On success:

   - 0 is returned.

   - If (*tgt==NULL) then *tgt is assigned to a newly-allocated
   object, owned by the caller. Note that even if no arguments are
   parsed, the object is still created.

   On error:

   - non-0 is returned

   - If (*tgt==NULL) then it is not modified.

   - If (*tgt!=NULL) (i.e., the caller provides his own object) then
   it might contain partial results.
*/
int cson_parse_argv_flags( int argc, char const * const * argv,
                           cson_object ** tgt, unsigned int * count );

/**
    Return values for the cson_pack() and cson_unpack() interfaces.
*/
enum cson_pack_retval {
    /** Signals an out-of-memory error. */
    CSON_PACK_ALLOC_ERROR = -1,
    /** Signals a syntax error in the format string. */
    CSON_PACK_ARG_ERROR = -2,
    /**
        Signals an that an internal error has occurred.
        This indicates a bug in this library.
    */
    CSON_PACK_INTERNAL_ERROR = -3,
    /**
       Signals that the JSON document does not validate agains the format
       string passed to cson_unpack().
    */
    CSON_PACK_VALIDATION_ERROR = -4
};

/**
   Construct arbitrarily complex JSON documents from native C types.

   Create a new object or array and add or merge the passed values and
   properties to it according to the supplied format string.

   fmt is a format string, it must at least contain an array or object
   specifier as its root value. Format specifiers start with a percent sign '\%'
   followed by one or more modifiers and a type character. Object properties
   are specified as key-value pairs where the key is specified as a string and
   passed as an argument of const char *. Any space, tab, carriage return, line
   feed, colon and comma characters between format specifiers are ignored.

   | Type  | Description |
   | :--:  | :---------- |
   | s     | creates either a property name or a string value, in case of the former the corresponding argument is a pointer to const char which is a sequence of bytes specifying the name of the property that is to be created, in case of the latter the corresponding argument is a pointer to const char |
   | d     | creates an integer value, the corresponding argument is an int |
   | i     | ^ |
   | f     | creates a floating point value, the corresponding argument is a double |
   | b     | creates a boolean value, the corresponding argument is an int |
   | N     | creates a null value |
   | [...] | creates an array, the corresponding argument is a pointer to a cson_array |
   | {...} | creates an array, the corresponding argument is a pointer to a cson_object |

   | Modifier | Description |
   | :------: | :---------- |
   | l        | specifies that the following d or i specifier applies to an argument which is a pointer to long |
   | ll       | specifies that the following d or i specifier applies to an argument which is a pointer to cson_int_t |

   | Short Form | Expands to
   | :--------: | :--------- |
   | {...}      | %*{...} |
   | [...]      | %*[...] |
   | \%D        | \%lld |


   Returns 0 on success. The error conditions are:

  - CSON_PACK_ARG_ERROR: fmt contains a syntax error

  - CSON_PACK_ALLOC_ERROR: a memory allocation failed

  - CSON_PACK_INTERNAL_ERROR: an internal error has occurred, this is a bug in
    cson

  Example:
  @code
  cson_value * root_value;
  cson_array * arr;
  ...
  rc = cson_pack( root_value, "{%s: %d, %s: %[]}", "foo", 42, "bar", arr );
  if( 0 != rc ) {
    ... error ...
  }
  @endcode
*/
int cson_pack( cson_value **root_valuep, const char *fmt, ... );

/**
   Same as cson_pack() except that it takes a va_list instead of a variable
   number of arguments.
*/
int cson_vpack( cson_value **root_valuep, const char *fmt, va_list args );

/**
   Iterate over the given object or array and convert an arbitrary number of
   JSON values into their native C types or validates them according to the
   given format string fmt.

   fmt is a format string, it must at least contain an array or object
   specifier as its root value. Format specifiers start with a percent sign '\%'
   followed by one or more modifiers and a type character. Object properties
   are specified as key-value pairs where the key is specified as a string and
   passed as an argument of const char *. Any space, tab, carriage return, line
   feed, colon and comma characters between format specifiers are ignored.

   | Type  | Description |
   | :--:  | :---------- |
   | s     | matches a either a property name or a string value, in case of the former the corresponding argument is a pointer to const char which is a sequence of bytes specifying the name of the property that is to be matched, in case of the latter the corresponding argument is a pointer to a pointer to const char unless the 'm' modifier is specified where the the corresponding argument is a pointer to a pointer to char |
   | d     | matches an integer value and must be used in with the "ll" modifier, the corresponding argument is a pointer to cson_int_t |
   | i     | ^ |
   | f     | matches a floating point value, the corresponding argument is a pointer to double |
   | b     | matches a boolean value, the corresponding argument is a pointer to int |
   | N     | matches a null value |
   | [...] | matches an array, the corresponding argument is a pointer to a pointer to a cson_array |
   | {...} | matches an array, the corresponding argument is a pointer to a pointer to a cson_object |

   | Modifier | Description |
   | :------: | :---------- |
   | ?        | specifies that the property reffered to by the given property name is optional |
   | *        | suppresses assignment, only check for the presence and type of the specified value |
   | m        | allocates a memory buffer for the extracted string |
   | ll       | specifies that the following d or i specifier applies to an argument which is a pointer to cson_int_t |

   | Short Form | Expands to
   | :--------: | :--------- |
   | {...}      | %*{...} |
   | [...]      | %*[...] |
   | \%D        | \%lld |

   Returns 0 on success. The error conditions are:

  - CSON_PACK_ARG_ERROR: fmt contains a syntax error

  - CSON_PACK_ALLOC_ERROR: a memory allocation failed

  - CSON_PACK_VALIDATION_ERROR: validation failed, the JSON document structure
    differs from that described by the format string

  - CSON_PACK_INTERNAL_ERROR: an internal error has occurred, this
    indicates a bug in this library.

  Example:
  @code
  cson_value * root_value;
  cson_int_t x = 0;
  cson_array * arr = NULL;
  const char *str = NULL;
  ...
  rc = cson_unpack( root_value, "{%s: %d, %s: %[], %?s: %s}", "foo", &x, "bar", &arr, "baz", &str );
  if( rc < 3 && rc >= 0  ) {
    ... optional property is missing ...
  } else if ( CSON_PACK_ALLOC_ERROR == rc ) {
    ... out of memory error ...
  } else if ( CSON_PACK_VALIDATION_ERROR == rc ) {
    ... unexpected JSON document structure ...
  } else if ( rc ) {
    ... internal error ...
  }
  @endcode

*/
int cson_unpack( cson_value *root_value, const char *fmt, ... );

/**
   Same as cson_unpack() except that it takes a va_list instead of a variable
   number of arguments.
*/
int cson_vunpack( cson_value *root_value, const char *fmt, va_list args );

/* LICENSE

This software's source code, including accompanying documentation and
demonstration applications, are licensed under the following
conditions...

Certain files are imported from external projects and have their own
licensing terms. Namely, the JSON_parser.* files. See their files for
their official licenses, but the summary is "do what you want [with
them] but leave the license text and copyright in place."

The author (Stephan G. Beal [http://wanderinghorse.net/home/stephan/])
explicitly disclaims copyright in all jurisdictions which recognize
such a disclaimer. In such jurisdictions, this software is released
into the Public Domain.

In jurisdictions which do not recognize Public Domain property
(e.g. Germany as of 2011), this software is Copyright (c) 2011 by
Stephan G. Beal, and is released under the terms of the MIT License
(see below).

In jurisdictions which recognize Public Domain property, the user of
this software may choose to accept it either as 1) Public Domain, 2)
under the conditions of the MIT License (see below), or 3) under the
terms of dual Public Domain/MIT License conditions described here, as
they choose.

The MIT License is about as close to Public Domain as a license can
get, and is described in clear, concise terms at:

    http://en.wikipedia.org/wiki/MIT_License

The full text of the MIT License follows:

--
Copyright (c) 2011 Stephan G. Beal (http://wanderinghorse.net/home/stephan/)

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

--END OF MIT LICENSE--

For purposes of the above license, the term "Software" includes
documentation and demonstration source code which accompanies
this software. ("Accompanies" = is contained in the Software's
primary public source code repository.)

*/

#if defined(__cplusplus)
} /*extern "C"*/
#endif

#endif /* WANDERINGHORSE_NET_CSON_H_INCLUDED */
/* end file include/wh/cson/cson.h */
/* begin file include/wh/cson/cson_sqlite3.h */
/** @file cson_sqlite3.h

This file contains cson's public sqlite3-to-JSON API declarations
and API documentation. If CSON_ENABLE_SQLITE3 is not defined,
or is defined to 0, then including this file will have no side-effects
other than defining CSON_ENABLE_SQLITE3 (if it was not defined) to 0
and defining a few include guard macros. i.e. if CSON_ENABLE_SQLITE3
is not set to a true value then the API is not visible.

This API requires that <sqlite3.h> be in the INCLUDES path and that
the client eventually link to (or directly embed) the sqlite3 library.
*/
#if !defined(WANDERINGHORSE_NET_CSON_SQLITE3_H_INCLUDED)
#define WANDERINGHORSE_NET_CSON_SQLITE3_H_INCLUDED 1
#if !defined(CSON_ENABLE_SQLITE3)
#  if defined(DOXYGEN)
#define CSON_ENABLE_SQLITE3 1
#  else
#define CSON_ENABLE_SQLITE3 1
#  endif
#endif

#if CSON_ENABLE_SQLITE3 /* we do this here for the sake of the amalgamation build */
#include "sqlite3.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
   Converts a single value from a single 0-based column index to its JSON
   equivalent.

   On success it returns a new JSON value, which will have a different concrete
   type depending on the field type reported by sqlite3_column_type(st,col):

   Integer, double, null, or string (TEXT and BLOB data, though not
   all blob data is legal for a JSON string).

   st must be a sqlite3_step()'d row and col must be a 0-based column
   index within that result row.
 */       
cson_value * cson_sqlite3_column_to_value( sqlite3_stmt * st, int col );

/**
   Creates a JSON Array object containing the names of all columns
   of the given prepared statement handle. 
    
   Returns a new array value on success, which the caller owns. Its elements
   are in the same order as in the underlying query.

   On error NULL is returned.
    
   st is not traversed or freed by this function - only the column
   count and names are read.
*/
cson_value * cson_sqlite3_column_names( sqlite3_stmt * st );

/**
   Creates a JSON Object containing key/value pairs corresponding
   to the result columns in the current row of the given statement
   handle. st must be a sqlite3_step()'d row result.

   On success a new Object is returned which is owned by the
   caller. On error NULL is returned.

   cson_sqlite3_column_to_value() is used to convert each column to a
   JSON value, and the column names are taken from
   sqlite3_column_name().
*/
cson_value * cson_sqlite3_row_to_object( sqlite3_stmt * st );
/**
   Functionally almost identical to cson_sqlite3_row_to_object(), the
   only difference being how the result objects gets its column names.
   st must be a freshly-step()'d handle holding a result row.
   colNames must be an Array with at least the same number of columns
   as st. If it has fewer, NULL is returned and this function has
   no side-effects.

   For each column in the result set, the colNames entry at the same
   index is used for the column key. If a given entry is-not-a String
   then conversion will fail and NULL will be returned.

   The one reason to prefer this over cson_sqlite3_row_to_object() is
   that this one can share the keys across multiple rows (or even
   other JSON containers), whereas the former makes fresh copies of
   the column names for each row.

*/
cson_value * cson_sqlite3_row_to_object2( sqlite3_stmt * st,
                                          cson_array * colNames );

/**
   Similar to cson_sqlite3_row_to_object(), but creates an Array
   value which contains the JSON-form values of the given result
   set row.
*/
cson_value * cson_sqlite3_row_to_array( sqlite3_stmt * st );
/**
    Converts the results of an sqlite3 SELECT statement to JSON,
    in the form of a cson_value object tree.
    
    st must be a prepared, but not yet traversed, SELECT query.
    tgt must be a pointer to NULL (see the example below). If
    either of those arguments are NULL, cson_rc.ArgError is returned.
    
    This walks the query results and returns a JSON object which
    has a different structure depending on the value of the 'fat'
    argument.
    
    
    If 'fat' is 0 then the structure is:
    
    @code
    {
        "columns":["colName1",..."colNameN"],
        "rows":[
            [colVal0, ... colValN],
            [colVal0, ... colValN],
            ...
        ]
    }
    @endcode
    
    In the "non-fat" format the order of the columns and row values is
    guaranteed to be the same as that of the underlying query.
    
    If 'fat' is not 0 then the structure is:
    
    @code
    {
        "columns":["colName1",..."colNameN"],
        "rows":[
            {"colName1":value1,..."colNameN":valueN},
            {"colName1":value1,..."colNameN":valueN},
            ...
        ]
    }
    @endcode

    In the "fat" format, the order of the "columns" entries is guaranteed
    to be the same as the underlying query fields, but the order
    of the keys in the "rows" might be different and might in fact
    change when passed through different JSON implementations,
    depending on how they implement object key/value pairs.

    On success it returns 0 and assigns *tgt to a newly-allocated
    JSON object tree (using the above structure), which the caller owns.
    If the query returns no rows, the "rows" value will be an empty
    array, as opposed to null.
    
    On error non-0 is returned and *tgt is not modified.
    
    The error code cson_rc.IOError is used to indicate a db-level
    error, and cson_rc.TypeError is returned if sqlite3_column_count(st)
    returns 0 or less (indicating an invalid or non-SELECT statement).
    
    The JSON data types are determined by the column type as reported
    by sqlite3_column_type():
    
    SQLITE_INTEGER: integer
    
    SQLITE_FLOAT: double
    
    SQLITE_TEXT or SQLITE_BLOB: string, and this will only work if
    the data is UTF8 compatible.
    
    If the db returns a literal or SQL NULL for a value it is converted
    to a JSON null. If it somehow finds a column type it cannot handle,
    the value is also converted to a NULL in the output.

    Example
    
    @code
    cson_value * json = NULL;
    int rc = cson_sqlite3_stmt_to_json( myStatement, &json, 1 );
    if( 0 != rc ) { ... error ... }
    else {
        cson_output_FILE( json, stdout, NULL );
        cson_value_free( json );
    }
    @endcode
*/
int cson_sqlite3_stmt_to_json( sqlite3_stmt * st, cson_value ** tgt, char fat );

/**
    A convenience wrapper around cson_sqlite3_stmt_to_json(), which
    takes SQL instead of a sqlite3_stmt object. It has the same
    return value and argument semantics as that function.
*/
int cson_sqlite3_sql_to_json( sqlite3 * db, cson_value ** tgt, char const * sql, char fat );

/**
   Binds a JSON value to a 1-based parameter index in a prepared SQL
   statement. v must be NULL or one of one of the types (null, string,
   integer, double, boolean, array). Booleans are bound as integer 0
   or 1. NULL or null are bound as SQL NULL. Integers are bound as
   64-bit ints. Strings are bound using sqlite3_bind_text() (as
   opposed to text16), but we could/should arguably bind them as
   blobs.

   If v is an Array then ndx is is used as a starting position
   (1-based) and each item in the array is bound to the next parameter
   position (starting and ndx, though the array uses 0-based offsets).

   TODO: add Object support for named parameters.

   Returns 0 on success, non-0 on error.
 */
int cson_sqlite3_bind_value( sqlite3_stmt * st, int ndx, cson_value const * v );
    
#if defined(__cplusplus)
} /*extern "C"*/
#endif
    
#endif /* CSON_ENABLE_SQLITE3 */
#endif /* WANDERINGHORSE_NET_CSON_SQLITE3_H_INCLUDED */
/* end file include/wh/cson/cson_sqlite3.h */
#endif /* FOSSIL_ENABLE_JSON */
