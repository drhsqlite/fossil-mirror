/* auto-generated! Do not edit! */
#include "cson_amalgamation.h"
/* begin file parser/JSON_parser.h */
/* See JSON_parser.c for copyright information and licensing. */

#ifndef JSON_PARSER_H
#define JSON_PARSER_H

/* JSON_parser.h */


#include <stddef.h>

/* Windows DLL stuff */
#ifdef JSON_PARSER_DLL
#   ifdef _MSC_VER
#	    ifdef JSON_PARSER_DLL_EXPORTS
#		    define JSON_PARSER_DLL_API __declspec(dllexport)
#	    else
#		    define JSON_PARSER_DLL_API __declspec(dllimport)
#       endif
#   else
#	    define JSON_PARSER_DLL_API 
#   endif
#else
#	define JSON_PARSER_DLL_API 
#endif

/* Determine the integer type use to parse non-floating point numbers */
#if __STDC_VERSION__ >= 199901L || HAVE_LONG_LONG == 1
typedef long long JSON_int_t;
#define JSON_PARSER_INTEGER_SSCANF_TOKEN "%lld"
#define JSON_PARSER_INTEGER_SPRINTF_TOKEN "%lld"
#else 
typedef long JSON_int_t;
#define JSON_PARSER_INTEGER_SSCANF_TOKEN "%ld"
#define JSON_PARSER_INTEGER_SPRINTF_TOKEN "%ld"
#endif


#ifdef __cplusplus
extern "C" {
#endif 

typedef enum 
{
    JSON_E_NONE = 0,
    JSON_E_INVALID_CHAR,
    JSON_E_INVALID_KEYWORD,
    JSON_E_INVALID_ESCAPE_SEQUENCE,
    JSON_E_INVALID_UNICODE_SEQUENCE,
    JSON_E_INVALID_NUMBER,
    JSON_E_NESTING_DEPTH_REACHED,
    JSON_E_UNBALANCED_COLLECTION,
    JSON_E_EXPECTED_KEY,
    JSON_E_EXPECTED_COLON,
    JSON_E_OUT_OF_MEMORY
} JSON_error;

typedef enum 
{
    JSON_T_NONE = 0,
    JSON_T_ARRAY_BEGIN,
    JSON_T_ARRAY_END,
    JSON_T_OBJECT_BEGIN,
    JSON_T_OBJECT_END,
    JSON_T_INTEGER,
    JSON_T_FLOAT,
    JSON_T_NULL,
    JSON_T_TRUE,
    JSON_T_FALSE,
    JSON_T_STRING,
    JSON_T_KEY,
    JSON_T_MAX
} JSON_type;

typedef struct JSON_value_struct {
    union {
        JSON_int_t integer_value;
        
        double float_value;
        
        struct {
            const char* value;
            size_t length;
        } str;
    } vu;
} JSON_value;

typedef struct JSON_parser_struct* JSON_parser;

/*! \brief JSON parser callback 

    \param ctx The pointer passed to new_JSON_parser.
    \param type An element of JSON_type but not JSON_T_NONE.    
    \param value A representation of the parsed value. This parameter is NULL for
        JSON_T_ARRAY_BEGIN, JSON_T_ARRAY_END, JSON_T_OBJECT_BEGIN, JSON_T_OBJECT_END,
        JSON_T_NULL, JSON_T_TRUE, and JSON_T_FALSE. String values are always returned
        as zero-terminated C strings.

    \return Non-zero if parsing should continue, else zero.
*/    
typedef int (*JSON_parser_callback)(void* ctx, int type, const struct JSON_value_struct* value);


/**
   A typedef for allocator functions semantically compatible with malloc().
*/
typedef void* (*JSON_malloc_t)(size_t n);
/**
   A typedef for deallocator functions semantically compatible with free().
*/
typedef void (*JSON_free_t)(void* mem);

/*! \brief The structure used to configure a JSON parser object 
*/
typedef struct {
    /** Pointer to a callback, called when the parser has something to tell
        the user. This parameter may be NULL. In this case the input is
        merely checked for validity.
    */
    JSON_parser_callback    callback;
    /**
       Callback context - client-specified data to pass to the
       callback function. This parameter may be NULL.
    */
    void*                   callback_ctx;
    /** Specifies the levels of nested JSON to allow. Negative numbers yield unlimited nesting.
        If negative, the parser can parse arbitrary levels of JSON, otherwise
        the depth is the limit.
    */
    int                     depth;
    /**
       To allow C style comments in JSON, set to non-zero.
    */
    int                     allow_comments;
    /**
       To decode floating point numbers manually set this parameter to
       non-zero.
    */
    int                     handle_floats_manually;
    /**
       The memory allocation routine, which must be semantically
       compatible with malloc(3). If set to NULL, malloc(3) is used.

       If this is set to a non-NULL value then the 'free' member MUST be
       set to the proper deallocation counterpart for this function.
       Failure to do so results in undefined behaviour at deallocation
       time.
    */
    JSON_malloc_t       malloc;
    /**
       The memory deallocation routine, which must be semantically
       compatible with free(3). If set to NULL, free(3) is used.

       If this is set to a non-NULL value then the 'alloc' member MUST be
       set to the proper allocation counterpart for this function.
       Failure to do so results in undefined behaviour at deallocation
       time.
    */
    JSON_free_t         free;
} JSON_config;

/*! \brief Initializes the JSON parser configuration structure to default values.

    The default configuration is
    - 127 levels of nested JSON (depends on JSON_PARSER_STACK_SIZE, see json_parser.c)
    - no parsing, just checking for JSON syntax
    - no comments
    - Uses realloc() for memory de/allocation.

    \param config. Used to configure the parser.
*/
JSON_PARSER_DLL_API void init_JSON_config(JSON_config * config);

/*! \brief Create a JSON parser object 

    \param config. Used to configure the parser. Set to NULL to use
        the default configuration. See init_JSON_config.  Its contents are
        copied by this function, so it need not outlive the returned
        object.
    
    \return The parser object, which is owned by the caller and must eventually
    be freed by calling delete_JSON_parser().
*/
JSON_PARSER_DLL_API JSON_parser new_JSON_parser(JSON_config const* config);

/*! \brief Destroy a previously created JSON parser object. */
JSON_PARSER_DLL_API void delete_JSON_parser(JSON_parser jc);

/*! \brief Parse a character.

    \return Non-zero, if all characters passed to this function are part of are valid JSON.
*/
JSON_PARSER_DLL_API int JSON_parser_char(JSON_parser jc, int next_char);

/*! \brief Finalize parsing.

    Call this method once after all input characters have been consumed.
    
    \return Non-zero, if all parsed characters are valid JSON, zero otherwise.
*/
JSON_PARSER_DLL_API int JSON_parser_done(JSON_parser jc);

/*! \brief Determine if a given string is valid JSON white space 

    \return Non-zero if the string is valid, zero otherwise.
*/
JSON_PARSER_DLL_API int JSON_parser_is_legal_white_space_string(const char* s);

/*! \brief Gets the last error that occurred during the use of JSON_parser.

    \return A value from the JSON_error enum.
*/
JSON_PARSER_DLL_API int JSON_parser_get_last_error(JSON_parser jc);

/*! \brief Re-sets the parser to prepare it for another parse run.

    \return True (non-zero) on success, 0 on error (e.g. !jc).
*/
JSON_PARSER_DLL_API int JSON_parser_reset(JSON_parser jc);


#ifdef __cplusplus
}
#endif 
    

#endif /* JSON_PARSER_H */
/* end file parser/JSON_parser.h */
/* begin file parser/JSON_parser.c */
/*
Copyright (c) 2005 JSON.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

The Software shall be used for Good, not Evil.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
    Callbacks, comments, Unicode handling by Jean Gressmann (jean@0x42.de), 2007-2010.
    
    
    Changelog:
        2010-11-25
            Support for custom memory allocation (sgbeal@googlemail.com).
                        
        2010-05-07
            Added error handling for memory allocation failure (sgbeal@googlemail.com). 
            Added diagnosis errors for invalid JSON.
            
        2010-03-25
            Fixed buffer overrun in grow_parse_buffer & cleaned up code.
            
        2009-10-19
            Replaced long double in JSON_value_struct with double after reports 
            of strtold being broken on some platforms (charles@transmissionbt.com).
            
        2009-05-17 
            Incorporated benrudiak@googlemail.com fix for UTF16 decoding.
            
        2009-05-14 
            Fixed float parsing bug related to a locale being set that didn't
            use '.' as decimal point character (charles@transmissionbt.com).
            
        2008-10-14 
            Renamed states.IN to states.IT to avoid name clash which IN macro
            defined in windef.h (alexey.pelykh@gmail.com)
            
        2008-07-19 
            Removed some duplicate code & debugging variable (charles@transmissionbt.com)
        
        2008-05-28 
            Made JSON_value structure ansi C compliant. This bug was report by 
            trisk@acm.jhu.edu
        
        2008-05-20 
            Fixed bug reported by charles@transmissionbt.com where the switching 
            from static to dynamic parse buffer did not copy the static parse 
            buffer's content.
*/



#include <assert.h>
#include <ctype.h>
#include <float.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>


#ifdef _MSC_VER
#   if _MSC_VER >= 1400 /* Visual Studio 2005 and up */
#      pragma warning(disable:4996) /* unsecure sscanf */
#      pragma warning(disable:4127) /* conditional expression is constant */
#   endif
#endif


#define true  1
#define false 0
#define __   -1     /* the universal error code */

/* values chosen so that the object size is approx equal to one page (4K) */
#ifndef JSON_PARSER_STACK_SIZE
#   define JSON_PARSER_STACK_SIZE 128
#endif

#ifndef JSON_PARSER_PARSE_BUFFER_SIZE
#   define JSON_PARSER_PARSE_BUFFER_SIZE 3500
#endif

typedef void* (*JSON_debug_malloc_t)(size_t bytes, const char* reason);

#ifdef JSON_PARSER_DEBUG_MALLOC
#   define JSON_parser_malloc(func, bytes, reason) ((JSON_debug_malloc_t)func)(bytes, reason)
#else
#   define JSON_parser_malloc(func, bytes, reason) func(bytes)
#endif

typedef unsigned short UTF16;

struct JSON_parser_struct {
    JSON_parser_callback callback;
    void* ctx;
    signed char state, before_comment_state, type, escaped, comment, allow_comments, handle_floats_manually, error;
    char decimal_point;
    UTF16 utf16_high_surrogate;
    int current_char;
    int depth;
    int top;
    int stack_capacity;
    signed char* stack;
    char* parse_buffer;
    size_t parse_buffer_capacity;
    size_t parse_buffer_count;
    signed char static_stack[JSON_PARSER_STACK_SIZE];
    char static_parse_buffer[JSON_PARSER_PARSE_BUFFER_SIZE];
    JSON_malloc_t malloc;
    JSON_free_t free;
};

#define COUNTOF(x) (sizeof(x)/sizeof(x[0])) 

/*
    Characters are mapped into these character classes. This allows for
    a significant reduction in the size of the state transition table.
*/



enum classes {
    C_SPACE,  /* space */
    C_WHITE,  /* other whitespace */
    C_LCURB,  /* {  */
    C_RCURB,  /* } */
    C_LSQRB,  /* [ */
    C_RSQRB,  /* ] */
    C_COLON,  /* : */
    C_COMMA,  /* , */
    C_QUOTE,  /* " */
    C_BACKS,  /* \ */
    C_SLASH,  /* / */
    C_PLUS,   /* + */
    C_MINUS,  /* - */
    C_POINT,  /* . */
    C_ZERO ,  /* 0 */
    C_DIGIT,  /* 123456789 */
    C_LOW_A,  /* a */
    C_LOW_B,  /* b */
    C_LOW_C,  /* c */
    C_LOW_D,  /* d */
    C_LOW_E,  /* e */
    C_LOW_F,  /* f */
    C_LOW_L,  /* l */
    C_LOW_N,  /* n */
    C_LOW_R,  /* r */
    C_LOW_S,  /* s */
    C_LOW_T,  /* t */
    C_LOW_U,  /* u */
    C_ABCDF,  /* ABCDF */
    C_E,      /* E */
    C_ETC,    /* everything else */
    C_STAR,   /* * */   
    NR_CLASSES
};

static const signed char ascii_class[128] = {
/*
    This array maps the 128 ASCII characters into character classes.
    The remaining Unicode characters should be mapped to C_ETC.
    Non-whitespace control characters are errors.
*/
    __,      __,      __,      __,      __,      __,      __,      __,
    __,      C_WHITE, C_WHITE, __,      __,      C_WHITE, __,      __,
    __,      __,      __,      __,      __,      __,      __,      __,
    __,      __,      __,      __,      __,      __,      __,      __,

    C_SPACE, C_ETC,   C_QUOTE, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_STAR,   C_PLUS,  C_COMMA, C_MINUS, C_POINT, C_SLASH,
    C_ZERO,  C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT,
    C_DIGIT, C_DIGIT, C_COLON, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,

    C_ETC,   C_ABCDF, C_ABCDF, C_ABCDF, C_ABCDF, C_E,     C_ABCDF, C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_LSQRB, C_BACKS, C_RSQRB, C_ETC,   C_ETC,

    C_ETC,   C_LOW_A, C_LOW_B, C_LOW_C, C_LOW_D, C_LOW_E, C_LOW_F, C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_LOW_L, C_ETC,   C_LOW_N, C_ETC,
    C_ETC,   C_ETC,   C_LOW_R, C_LOW_S, C_LOW_T, C_LOW_U, C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_LCURB, C_ETC,   C_RCURB, C_ETC,   C_ETC
};


/*
    The state codes.
*/
enum states {
    GO,  /* start    */
    OK,  /* ok       */
    OB,  /* object   */
    KE,  /* key      */
    CO,  /* colon    */
    VA,  /* value    */
    AR,  /* array    */
    ST,  /* string   */
    ES,  /* escape   */
    U1,  /* u1       */
    U2,  /* u2       */
    U3,  /* u3       */
    U4,  /* u4       */
    MI,  /* minus    */
    ZE,  /* zero     */
    IT,  /* integer  */
    FR,  /* fraction */
    E1,  /* e        */
    E2,  /* ex       */
    E3,  /* exp      */
    T1,  /* tr       */
    T2,  /* tru      */
    T3,  /* true     */
    F1,  /* fa       */
    F2,  /* fal      */
    F3,  /* fals     */
    F4,  /* false    */
    N1,  /* nu       */
    N2,  /* nul      */
    N3,  /* null     */
    C1,  /* /        */
    C2,  /* / *     */
    C3,  /* *        */
    FX,  /* *.* *eE* */
    D1,  /* second UTF-16 character decoding started by \ */
    D2,  /* second UTF-16 character proceeded by u */
    NR_STATES
};

enum actions
{
    CB = -10, /* comment begin */
    CE = -11, /* comment end */
    FA = -12, /* false */
    TR = -13, /* false */
    NU = -14, /* null */
    DE = -15, /* double detected by exponent e E */
    DF = -16, /* double detected by fraction . */
    SB = -17, /* string begin */
    MX = -18, /* integer detected by minus */
    ZX = -19, /* integer detected by zero */
    IX = -20, /* integer detected by 1-9 */
    EX = -21, /* next char is escaped */
    UC = -22  /* Unicode character read */
};


static const signed char state_transition_table[NR_STATES][NR_CLASSES] = {
/*
    The state transition table takes the current state and the current symbol,
    and returns either a new state or an action. An action is represented as a
    negative number. A JSON text is accepted if at the end of the text the
    state is OK and if the mode is MODE_DONE.

                 white                                      1-9                                   ABCDF  etc
             space |  {  }  [  ]  :  ,  "  \  /  +  -  .  0  |  a  b  c  d  e  f  l  n  r  s  t  u  |  E  |  * */
/*start  GO*/ {GO,GO,-6,__,-5,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*ok     OK*/ {OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*object OB*/ {OB,OB,__,-9,__,__,__,__,SB,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*key    KE*/ {KE,KE,__,__,__,__,__,__,SB,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*colon  CO*/ {CO,CO,__,__,__,__,-2,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*value  VA*/ {VA,VA,-6,__,-5,__,__,__,SB,__,CB,__,MX,__,ZX,IX,__,__,__,__,__,FA,__,NU,__,__,TR,__,__,__,__,__},
/*array  AR*/ {AR,AR,-6,__,-5,-7,__,__,SB,__,CB,__,MX,__,ZX,IX,__,__,__,__,__,FA,__,NU,__,__,TR,__,__,__,__,__},
/*string ST*/ {ST,__,ST,ST,ST,ST,ST,ST,-4,EX,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST},
/*escape ES*/ {__,__,__,__,__,__,__,__,ST,ST,ST,__,__,__,__,__,__,ST,__,__,__,ST,__,ST,ST,__,ST,U1,__,__,__,__},
/*u1     U1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U2,U2,U2,U2,U2,U2,U2,U2,__,__,__,__,__,__,U2,U2,__,__},
/*u2     U2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U3,U3,U3,U3,U3,U3,U3,U3,__,__,__,__,__,__,U3,U3,__,__},
/*u3     U3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,U4,U4,U4,U4,U4,U4,U4,U4,__,__,__,__,__,__,U4,U4,__,__},
/*u4     U4*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,UC,UC,UC,UC,UC,UC,UC,UC,__,__,__,__,__,__,UC,UC,__,__},
/*minus  MI*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,ZE,IT,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*zero   ZE*/ {OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,DF,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*int    IT*/ {OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,DF,IT,IT,__,__,__,__,DE,__,__,__,__,__,__,__,__,DE,__,__},
/*frac   FR*/ {OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,__,FR,FR,__,__,__,__,E1,__,__,__,__,__,__,__,__,E1,__,__},
/*e      E1*/ {__,__,__,__,__,__,__,__,__,__,__,E2,E2,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*ex     E2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*exp    E3*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*tr     T1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T2,__,__,__,__,__,__,__},
/*tru    T2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T3,__,__,__,__},
/*true   T3*/ {__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__,__},
/*fa     F1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*fal    F2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F3,__,__,__,__,__,__,__,__,__},
/*fals   F3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F4,__,__,__,__,__,__},
/*false  F4*/ {__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__,__},
/*nu     N1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N2,__,__,__,__},
/*nul    N2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N3,__,__,__,__,__,__,__,__,__},
/*null   N3*/ {__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__},
/*/      C1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,C2},
/*/*     C2*/ {C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C3},
/**      C3*/ {C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,CE,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C3},
/*_.     FX*/ {OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,FR,FR,__,__,__,__,E1,__,__,__,__,__,__,__,__,E1,__,__},
/*\      D1*/ {__,__,__,__,__,__,__,__,__,D2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*\      D2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U1,__,__,__,__},
};


/*
    These modes can be pushed on the stack.
*/
enum modes {
    MODE_ARRAY = 1, 
    MODE_DONE = 2,  
    MODE_KEY = 3,   
    MODE_OBJECT = 4
};

static void set_error(JSON_parser jc)
{
    switch (jc->state) {
        case GO:
            switch (jc->current_char) {
            case '{': case '}': case '[': case ']': 
                jc->error = JSON_E_UNBALANCED_COLLECTION;
                break;
            default:
                jc->error = JSON_E_INVALID_CHAR;
                break;    
            }
            break;
        case OB:
            jc->error = JSON_E_EXPECTED_KEY;
            break;
        case AR:
            jc->error = JSON_E_UNBALANCED_COLLECTION;
            break;
        case CO:
            jc->error = JSON_E_EXPECTED_COLON;
            break;
        case KE:
            jc->error = JSON_E_EXPECTED_KEY;
            break;
        /* \uXXXX\uYYYY */
        case U1: case U2: case U3: case U4: case D1: case D2:
            jc->error = JSON_E_INVALID_UNICODE_SEQUENCE;
            break;
        /* true, false, null */
        case T1: case T2: case T3: case F1: case F2: case F3: case F4: case N1: case N2: case N3:
            jc->error = JSON_E_INVALID_KEYWORD;
            break;
        /* minus, integer, fraction, exponent */
        case MI: case ZE: case IT: case FR: case E1: case E2: case E3:
            jc->error = JSON_E_INVALID_NUMBER;
            break;
        default:
            jc->error = JSON_E_INVALID_CHAR;
            break;
    }
}

static int
push(JSON_parser jc, int mode)
{
/*
    Push a mode onto the stack. Return false if there is overflow.
*/
    assert(jc->top <= jc->stack_capacity);
    
    if (jc->depth < 0) {
        if (jc->top == jc->stack_capacity) {
            const size_t bytes_to_copy = jc->stack_capacity * sizeof(jc->stack[0]);
            const size_t new_capacity = jc->stack_capacity * 2;
            const size_t bytes_to_allocate = new_capacity * sizeof(jc->stack[0]);
            void* mem = JSON_parser_malloc(jc->malloc, bytes_to_allocate, "stack");
            if (!mem) {
                jc->error = JSON_E_OUT_OF_MEMORY;
                return false;
            }
            jc->stack_capacity = (int)new_capacity;
            memcpy(mem, jc->stack, bytes_to_copy);
            if (jc->stack != &jc->static_stack[0]) {
                jc->free(jc->stack);
            }
            jc->stack = (signed char*)mem;
        }
    } else {
        if (jc->top == jc->depth) {
            jc->error = JSON_E_NESTING_DEPTH_REACHED;
            return false;
        }
    }
    jc->stack[++jc->top] = (signed char)mode;
    return true;
}


static int
pop(JSON_parser jc, int mode)
{
/*
    Pop the stack, assuring that the current mode matches the expectation.
    Return false if there is underflow or if the modes mismatch.
*/
    if (jc->top < 0 || jc->stack[jc->top] != mode) {
        return false;
    }
    jc->top -= 1;
    return true;
}


#define parse_buffer_clear(jc) \
    do {\
        jc->parse_buffer_count = 0;\
        jc->parse_buffer[0] = 0;\
    } while (0)
    
#define parse_buffer_pop_back_char(jc)\
    do {\
        assert(jc->parse_buffer_count >= 1);\
        --jc->parse_buffer_count;\
        jc->parse_buffer[jc->parse_buffer_count] = 0;\
    } while (0)    



void delete_JSON_parser(JSON_parser jc)
{
    if (jc) {
        if (jc->stack != &jc->static_stack[0]) {
            jc->free((void*)jc->stack);
        }
        if (jc->parse_buffer != &jc->static_parse_buffer[0]) {
            jc->free((void*)jc->parse_buffer);
        }
        jc->free((void*)jc);
     }   
}

int JSON_parser_reset(JSON_parser jc)
{
    if (NULL == jc) {
        return false;
    }
    
    jc->state = GO;
    jc->top = -1;

    /* parser has been used previously? */
    if (NULL == jc->parse_buffer) {
    
        /* Do we want non-bound stack? */
        if (jc->depth > 0) {
            jc->stack_capacity = jc->depth;
            if (jc->depth <= (int)COUNTOF(jc->static_stack)) {
                jc->stack = &jc->static_stack[0];
            } else {
                const size_t bytes_to_alloc = jc->stack_capacity * sizeof(jc->stack[0]);
                jc->stack = (signed char*)JSON_parser_malloc(jc->malloc, bytes_to_alloc, "stack");
                if (jc->stack == NULL) {
                    return false;
                }
            }
        } else {
            jc->stack_capacity = (int)COUNTOF(jc->static_stack);
            jc->depth = -1;
            jc->stack = &jc->static_stack[0];
        }
        
        /* set up the parse buffer */
        jc->parse_buffer = &jc->static_parse_buffer[0];
        jc->parse_buffer_capacity = COUNTOF(jc->static_parse_buffer);
    }
    
    /* set parser to start */
    push(jc, MODE_DONE);
    parse_buffer_clear(jc);
    
    return true;
}

JSON_parser
new_JSON_parser(JSON_config const * config)
{
/*
    new_JSON_parser starts the checking process by constructing a JSON_parser
    object. It takes a depth parameter that restricts the level of maximum
    nesting.

    To continue the process, call JSON_parser_char for each character in the
    JSON text, and then call JSON_parser_done to obtain the final result.
    These functions are fully reentrant.
*/

    int use_std_malloc = false;
    JSON_config default_config;
    JSON_parser jc;
    JSON_malloc_t alloc;
    
    /* set to default configuration if none was provided */
    if (NULL == config) {
        /* initialize configuration */
        init_JSON_config(&default_config);
        config = &default_config;
    }
    
    /* use std malloc if either the allocator or deallocator function isn't set */
    use_std_malloc = NULL == config->malloc || NULL == config->free;
    
    alloc = use_std_malloc ? malloc : config->malloc;
    
    jc = JSON_parser_malloc(alloc, sizeof(*jc), "parser");    
    
    if (NULL == jc) {
        return NULL;
    }
    
    /* configure the parser */
    memset(jc, 0, sizeof(*jc));
    jc->malloc = alloc;
    jc->free = use_std_malloc ? free : config->free;
    jc->callback = config->callback;
    jc->ctx = config->callback_ctx;
    jc->allow_comments = (signed char)(config->allow_comments != 0);
    jc->handle_floats_manually = (signed char)(config->handle_floats_manually != 0);
    jc->decimal_point = *localeconv()->decimal_point;
    /* We need to be able to push at least one object */
    jc->depth = config->depth == 0 ? 1 : config->depth;
    
    /* reset the parser */
    if (!JSON_parser_reset(jc)) {
        jc->free(jc);
        return NULL;
    }
    
    return jc;
}

static int parse_buffer_grow(JSON_parser jc)
{
    const size_t bytes_to_copy = jc->parse_buffer_count * sizeof(jc->parse_buffer[0]);
    const size_t new_capacity = jc->parse_buffer_capacity * 2;
    const size_t bytes_to_allocate = new_capacity * sizeof(jc->parse_buffer[0]);
    void* mem = JSON_parser_malloc(jc->malloc, bytes_to_allocate, "parse buffer");
    
    if (mem == NULL) {
        jc->error = JSON_E_OUT_OF_MEMORY;
        return false;
    }
    
    assert(new_capacity > 0);
    memcpy(mem, jc->parse_buffer, bytes_to_copy);
    
    if (jc->parse_buffer != &jc->static_parse_buffer[0]) {
        jc->free(jc->parse_buffer);
    }
    
    jc->parse_buffer = (char*)mem;
    jc->parse_buffer_capacity = new_capacity;
    
    return true;
}

static int parse_buffer_reserve_for(JSON_parser jc, unsigned chars)
{
    while (jc->parse_buffer_count + chars + 1 > jc->parse_buffer_capacity) {
        if (!parse_buffer_grow(jc)) {
            assert(jc->error == JSON_E_OUT_OF_MEMORY);
            return false;
        }
    }
    
    return true;
}

#define parse_buffer_has_space_for(jc, count) \
    (jc->parse_buffer_count + (count) + 1 <= jc->parse_buffer_capacity)

#define parse_buffer_push_back_char(jc, c)\
    do {\
        assert(parse_buffer_has_space_for(jc, 1)); \
        jc->parse_buffer[jc->parse_buffer_count++] = c;\
        jc->parse_buffer[jc->parse_buffer_count]   = 0;\
    } while (0)

#define assert_is_non_container_type(jc) \
    assert( \
        jc->type == JSON_T_NULL || \
        jc->type == JSON_T_FALSE || \
        jc->type == JSON_T_TRUE || \
        jc->type == JSON_T_FLOAT || \
        jc->type == JSON_T_INTEGER || \
        jc->type == JSON_T_STRING)
    

static int parse_parse_buffer(JSON_parser jc)
{
    if (jc->callback) {
        JSON_value value, *arg = NULL;
        
        if (jc->type != JSON_T_NONE) {
            assert_is_non_container_type(jc);
        
            switch(jc->type) {
                case JSON_T_FLOAT:
                    arg = &value;
                    if (jc->handle_floats_manually) {
                        value.vu.str.value = jc->parse_buffer;
                        value.vu.str.length = jc->parse_buffer_count;
                    } else { 
                        /* not checking with end pointer b/c there may be trailing ws */
                        value.vu.float_value = strtod(jc->parse_buffer, NULL);
                    }
                    break;
                case JSON_T_INTEGER:
                    arg = &value;
                    sscanf(jc->parse_buffer, JSON_PARSER_INTEGER_SSCANF_TOKEN, &value.vu.integer_value);
                    break;
                case JSON_T_STRING:
                    arg = &value;
                    value.vu.str.value = jc->parse_buffer;
                    value.vu.str.length = jc->parse_buffer_count;
                    break;
            }
            
            if (!(*jc->callback)(jc->ctx, jc->type, arg)) {
                return false;
            }
        }
    }
    
    parse_buffer_clear(jc);
    
    return true;
}

#define IS_HIGH_SURROGATE(uc) (((uc) & 0xFC00) == 0xD800)
#define IS_LOW_SURROGATE(uc)  (((uc) & 0xFC00) == 0xDC00)
#define DECODE_SURROGATE_PAIR(hi,lo) ((((hi) & 0x3FF) << 10) + ((lo) & 0x3FF) + 0x10000)
static const unsigned char utf8_lead_bits[4] = { 0x00, 0xC0, 0xE0, 0xF0 };

static int decode_unicode_char(JSON_parser jc)
{
    int i;
    unsigned uc = 0;
    char* p;
    int trail_bytes;
    
    assert(jc->parse_buffer_count >= 6);
    
    p = &jc->parse_buffer[jc->parse_buffer_count - 4];
    
    for (i = 12; i >= 0; i -= 4, ++p) {
        unsigned x = *p;
        
        if (x >= 'a') {
            x -= ('a' - 10);
        } else if (x >= 'A') {
            x -= ('A' - 10);
        } else {
            x &= ~0x30u;
        }
        
        assert(x < 16);
        
        uc |= x << i;
    }
    
    /* clear UTF-16 char from buffer */
    jc->parse_buffer_count -= 6;
    jc->parse_buffer[jc->parse_buffer_count] = 0;
    
    /* attempt decoding ... */
    if (jc->utf16_high_surrogate) {
        if (IS_LOW_SURROGATE(uc)) {
            uc = DECODE_SURROGATE_PAIR(jc->utf16_high_surrogate, uc);
            trail_bytes = 3;
            jc->utf16_high_surrogate = 0;
        } else {
            /* high surrogate without a following low surrogate */
            return false;
        }
    } else {
        if (uc < 0x80) {
            trail_bytes = 0;
        } else if (uc < 0x800) {
            trail_bytes = 1;
        } else if (IS_HIGH_SURROGATE(uc)) {
            /* save the high surrogate and wait for the low surrogate */
            jc->utf16_high_surrogate = (UTF16)uc;
            return true;
        } else if (IS_LOW_SURROGATE(uc)) {
            /* low surrogate without a preceding high surrogate */
            return false;
        } else {
            trail_bytes = 2;
        }
    }
    
    jc->parse_buffer[jc->parse_buffer_count++] = (char) ((uc >> (trail_bytes * 6)) | utf8_lead_bits[trail_bytes]);
    
    for (i = trail_bytes * 6 - 6; i >= 0; i -= 6) {
        jc->parse_buffer[jc->parse_buffer_count++] = (char) (((uc >> i) & 0x3F) | 0x80);
    }

    jc->parse_buffer[jc->parse_buffer_count] = 0;
    
    return true;
}

static int add_escaped_char_to_parse_buffer(JSON_parser jc, int next_char)
{
    assert(parse_buffer_has_space_for(jc, 1));
    
    jc->escaped = 0;
    /* remove the backslash */
    parse_buffer_pop_back_char(jc);
    switch(next_char) {
        case 'b':
            parse_buffer_push_back_char(jc, '\b');
            break;
        case 'f':
            parse_buffer_push_back_char(jc, '\f');
            break;
        case 'n':
            parse_buffer_push_back_char(jc, '\n');
            break;
        case 'r':
            parse_buffer_push_back_char(jc, '\r');
            break;
        case 't':
            parse_buffer_push_back_char(jc, '\t');
            break;
        case '"':
            parse_buffer_push_back_char(jc, '"');
            break;
        case '\\':
            parse_buffer_push_back_char(jc, '\\');
            break;
        case '/':
            parse_buffer_push_back_char(jc, '/');
            break;
        case 'u':
            parse_buffer_push_back_char(jc, '\\');
            parse_buffer_push_back_char(jc, 'u');
            break;
        default:
            return false;
    }

    return true;
}

static int add_char_to_parse_buffer(JSON_parser jc, int next_char, int next_class)
{
    if (!parse_buffer_reserve_for(jc, 1)) {
        assert(JSON_E_OUT_OF_MEMORY == jc->error);
        return false;
    }
    
    if (jc->escaped) {
        if (!add_escaped_char_to_parse_buffer(jc, next_char)) {
            jc->error = JSON_E_INVALID_ESCAPE_SEQUENCE;
            return false; 
        }
    } else if (!jc->comment) {
        if ((jc->type != JSON_T_NONE) | !((next_class == C_SPACE) | (next_class == C_WHITE)) /* non-white-space */) {
            parse_buffer_push_back_char(jc, (char)next_char);
        }
    }
    
    return true;
}

#define assert_type_isnt_string_null_or_bool(jc) \
    assert(jc->type != JSON_T_FALSE); \
    assert(jc->type != JSON_T_TRUE); \
    assert(jc->type != JSON_T_NULL); \
    assert(jc->type != JSON_T_STRING)


int
JSON_parser_char(JSON_parser jc, int next_char)
{
/*
    After calling new_JSON_parser, call this function for each character (or
    partial character) in your JSON text. It can accept UTF-8, UTF-16, or
    UTF-32. It returns true if things are looking ok so far. If it rejects the
    text, it returns false.
*/
    int next_class, next_state;

/*
    Store the current char for error handling
*/    
    jc->current_char = next_char;
    
/*
    Determine the character's class.
*/
    if (next_char < 0) {
        jc->error = JSON_E_INVALID_CHAR;
        return false;
    }
    if (next_char >= 128) {
        next_class = C_ETC;
    } else {
        next_class = ascii_class[next_char];
        if (next_class <= __) {
            set_error(jc);
            return false;
        }
    }
    
    if (!add_char_to_parse_buffer(jc, next_char, next_class)) {
        return false;
    }
    
/*
    Get the next state from the state transition table.
*/
    next_state = state_transition_table[jc->state][next_class];
    if (next_state >= 0) {
/*
    Change the state.
*/
        jc->state = (signed char)next_state;
    } else {
/*
    Or perform one of the actions.
*/
        switch (next_state) {
/* Unicode character */        
        case UC:
            if(!decode_unicode_char(jc)) {
                jc->error = JSON_E_INVALID_UNICODE_SEQUENCE;
                return false;
            }
            /* check if we need to read a second UTF-16 char */
            if (jc->utf16_high_surrogate) {
                jc->state = D1;
            } else {
                jc->state = ST;
            }
            break;
/* escaped char */
        case EX:
            jc->escaped = 1;
            jc->state = ES;
            break;
/* integer detected by minus */
        case MX:
            jc->type = JSON_T_INTEGER;
            jc->state = MI;
            break;  
/* integer detected by zero */            
        case ZX:
            jc->type = JSON_T_INTEGER;
            jc->state = ZE;
            break;  
/* integer detected by 1-9 */            
        case IX:
            jc->type = JSON_T_INTEGER;
            jc->state = IT;
            break;  
            
/* floating point number detected by exponent*/
        case DE:
            assert_type_isnt_string_null_or_bool(jc);
            jc->type = JSON_T_FLOAT;
            jc->state = E1;
            break;   
        
/* floating point number detected by fraction */
        case DF:
            assert_type_isnt_string_null_or_bool(jc);
            if (!jc->handle_floats_manually) {
/*
    Some versions of strtod (which underlies sscanf) don't support converting 
    C-locale formated floating point values.
*/           
                assert(jc->parse_buffer[jc->parse_buffer_count-1] == '.');
                jc->parse_buffer[jc->parse_buffer_count-1] = jc->decimal_point;
            }            
            jc->type = JSON_T_FLOAT;
            jc->state = FX;
            break;   
/* string begin " */
        case SB:
            parse_buffer_clear(jc);
            assert(jc->type == JSON_T_NONE);
            jc->type = JSON_T_STRING;
            jc->state = ST;
            break;        
        
/* n */
        case NU:
            assert(jc->type == JSON_T_NONE);
            jc->type = JSON_T_NULL;
            jc->state = N1;
            break;        
/* f */
        case FA:
            assert(jc->type == JSON_T_NONE);
            jc->type = JSON_T_FALSE;
            jc->state = F1;
            break;        
/* t */
        case TR:
            assert(jc->type == JSON_T_NONE);
            jc->type = JSON_T_TRUE;
            jc->state = T1;
            break;        
        
/* closing comment */
        case CE:
            jc->comment = 0;
            assert(jc->parse_buffer_count == 0);
            assert(jc->type == JSON_T_NONE);
            jc->state = jc->before_comment_state;
            break;        
        
/* opening comment  */
        case CB:
            if (!jc->allow_comments) {
                return false;
            }
            parse_buffer_pop_back_char(jc);
            if (!parse_parse_buffer(jc)) {
                return false;
            }
            assert(jc->parse_buffer_count == 0);
            assert(jc->type != JSON_T_STRING);
            switch (jc->stack[jc->top]) {
            case MODE_ARRAY:
            case MODE_OBJECT:   
                switch(jc->state) {
                case VA:
                case AR:
                    jc->before_comment_state = jc->state;
                    break;
                default:
                    jc->before_comment_state = OK;
                    break;
                }
                break;
            default:
                jc->before_comment_state = jc->state;
                break;
            }
            jc->type = JSON_T_NONE;
            jc->state = C1;
            jc->comment = 1;
            break;
/* empty } */
        case -9:        
            parse_buffer_clear(jc);
            if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_OBJECT_END, NULL)) {
                return false;
            }
            if (!pop(jc, MODE_KEY)) {
                return false;
            }
            jc->state = OK;
            break;

/* } */ case -8:
            parse_buffer_pop_back_char(jc);
            if (!parse_parse_buffer(jc)) {
                return false;
            }
            if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_OBJECT_END, NULL)) {
                return false;
            }
            if (!pop(jc, MODE_OBJECT)) {
                jc->error = JSON_E_UNBALANCED_COLLECTION;
                return false;
            }
            jc->type = JSON_T_NONE;
            jc->state = OK;
            break;

/* ] */ case -7:
            parse_buffer_pop_back_char(jc);
            if (!parse_parse_buffer(jc)) {
                return false;
            }
            if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_ARRAY_END, NULL)) {
                return false;
            }
            if (!pop(jc, MODE_ARRAY)) {
                jc->error = JSON_E_UNBALANCED_COLLECTION;
                return false;
            }
            
            jc->type = JSON_T_NONE;
            jc->state = OK;
            break;

/* { */ case -6:
            parse_buffer_pop_back_char(jc);
            if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_OBJECT_BEGIN, NULL)) {
                return false;
            }
            if (!push(jc, MODE_KEY)) {
                return false;
            }
            assert(jc->type == JSON_T_NONE);
            jc->state = OB;
            break;

/* [ */ case -5:
            parse_buffer_pop_back_char(jc);
            if (jc->callback && !(*jc->callback)(jc->ctx, JSON_T_ARRAY_BEGIN, NULL)) {
                return false;
            }
            if (!push(jc, MODE_ARRAY)) {
                return false;
            }
            assert(jc->type == JSON_T_NONE);
            jc->state = AR;
            break;

/* string end " */ case -4:
            parse_buffer_pop_back_char(jc);
            switch (jc->stack[jc->top]) {
            case MODE_KEY:
                assert(jc->type == JSON_T_STRING);
                jc->type = JSON_T_NONE;
                jc->state = CO;
                
                if (jc->callback) {
                    JSON_value value;
                    value.vu.str.value = jc->parse_buffer;
                    value.vu.str.length = jc->parse_buffer_count;
                    if (!(*jc->callback)(jc->ctx, JSON_T_KEY, &value)) {
                        return false;
                    }
                }
                parse_buffer_clear(jc);
                break;
            case MODE_ARRAY:
            case MODE_OBJECT:
                assert(jc->type == JSON_T_STRING);
                if (!parse_parse_buffer(jc)) {
                    return false;
                }
                jc->type = JSON_T_NONE;
                jc->state = OK;
                break;
            default:
                return false;
            }
            break;

/* , */ case -3:
            parse_buffer_pop_back_char(jc);
            if (!parse_parse_buffer(jc)) {
                return false;
            }
            switch (jc->stack[jc->top]) {
            case MODE_OBJECT:
/*
    A comma causes a flip from object mode to key mode.
*/
                if (!pop(jc, MODE_OBJECT) || !push(jc, MODE_KEY)) {
                    return false;
                }
                assert(jc->type != JSON_T_STRING);
                jc->type = JSON_T_NONE;
                jc->state = KE;
                break;
            case MODE_ARRAY:
                assert(jc->type != JSON_T_STRING);
                jc->type = JSON_T_NONE;
                jc->state = VA;
                break;
            default:
                return false;
            }
            break;

/* : */ case -2:
/*
    A colon causes a flip from key mode to object mode.
*/
            parse_buffer_pop_back_char(jc);
            if (!pop(jc, MODE_KEY) || !push(jc, MODE_OBJECT)) {
                return false;
            }
            assert(jc->type == JSON_T_NONE);
            jc->state = VA;
            break;
/*
    Bad action.
*/
        default:
            set_error(jc);
            return false;
        }
    }
    return true;
}

int
JSON_parser_done(JSON_parser jc)
{
    if ((jc->state == OK || jc->state == GO) && pop(jc, MODE_DONE))
    {
        return true;
    }

    jc->error = JSON_E_UNBALANCED_COLLECTION;
    return false;
}


int JSON_parser_is_legal_white_space_string(const char* s)
{
    int c, char_class;
    
    if (s == NULL) {
        return false;
    }
    
    for (; *s; ++s) {   
        c = *s;
        
        if (c < 0 || c >= 128) {
            return false;
        }
        
        char_class = ascii_class[c];
        
        if (char_class != C_SPACE && char_class != C_WHITE) {
            return false;
        }
    }
    
    return true;
}

int JSON_parser_get_last_error(JSON_parser jc)
{
    return jc->error;
}


void init_JSON_config(JSON_config* config)
{
    if (config) {
        memset(config, 0, sizeof(*config));
        
        config->depth = JSON_PARSER_STACK_SIZE - 1;
        config->malloc = malloc;
        config->free = free;
    }
}
/* end file parser/JSON_parser.c */
/* begin file ./cson.c */
#include <assert.h>
#include <stdlib.h> /* malloc()/free() */
#include <string.h>

#ifdef _MSC_VER
#   if _MSC_VER >= 1400 /* Visual Studio 2005 and up */
#     pragma warning( push )
#     pragma warning(disable:4996) /* unsecure sscanf (but snscanf() isn't in c89) */
#     pragma warning(disable:4244) /* complaining about data loss due
                                      to integer precision in the
                                      sqlite3 utf decoding routines */
#   endif
#endif

#if 1
#include <stdio.h>
#define MARKER if(1) printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__); if(1) printf
#else
static void noop_printf(char const * fmt, ...) {}
#define MARKER if(0) printf
#endif

#if defined(__cplusplus)
extern "C" {
#endif


    
/**
   Type IDs corresponding to JavaScript/JSON types.
*/
enum cson_type_id {
  /**
    The special "null" value constant.

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
typedef enum cson_type_id cson_type_id;

/**
   This type holds the "vtbl" for type-specific operations when
   working with cson_value objects.

   All cson_values of a given logical type share a pointer to a single
   library-internal instance of this class.
*/
struct cson_value_api
{
    /**
       The logical JavaScript/JSON type associated with
       this object.
     */
    const cson_type_id typeID;
    /**
       Must free any memory associated with self,
       but not free self. If self is NULL then
       this function must do nothing.
    */
    void (*cleanup)( cson_value * self );
    /**
       POSSIBLE TODOs:

       // Deep copy.
       int (*clone)( cson_value const * self, cson_value ** tgt );

       // Using JS semantics for true/value
       char (*bool_value)( cson_value const * self );

       // memcmp() return value semantics
       int (*compare)( cson_value const * self, cson_value const * other );
     */
};

typedef struct cson_value_api cson_value_api;

/**
   Empty-initialized cson_value_api object.
*/
#define cson_value_api_empty_m {           \
        CSON_TYPE_UNDEF/*typeID*/,         \
        NULL/*cleanup*/\
      }
/**
   Empty-initialized cson_value_api object.
*/
static const cson_value_api cson_value_api_empty = cson_value_api_empty_m;


typedef unsigned int cson_counter_t;
struct cson_value
{
    /** The "vtbl" of type-specific operations. All instances
        of a given logical value type share a single api instance.

        Results are undefined if this value is NULL.
    */
    cson_value_api const * api;

    /** The raw value. Its interpretation depends on the value of the
        api member. Some value types require dynamically-allocated
        memory, so one must always call cson_value_free() to destroy a
        value when it is no longer needed. For stack-allocated values
        (which client could SHOULD NOT USE unless they are intimately
        familiar with the memory management rules and don't mind an
        occasional leak or crash), use cson_value_clean() instead of
        cson_value_free().
    */
    void * value;

    /**
       We use this to allow us to store cson_value instances in
       multiple containers or multiple times within a single container
       (provided no cycles are introduced).

       Notes about the rc implementation:

       - The refcount is for the cson_value instance itself, not its
       value pointer.

       - Instances start out with a refcount of 0 (not 1). Adding them
       to a container will increase the refcount. Cleaning up the container
       will decrement the count.

       - cson_value_free() decrements the refcount (if it is not already
       0) and cleans/frees the value only when the refcount is 0.

       - Some places in the internals add an "extra" reference to
       objects to avoid a premature deletion. Don't try this at home.
    */
    cson_counter_t refcount;
};


/**
   Empty-initialized cson_value object.
*/
#define cson_value_empty_m { &cson_value_api_empty/*api*/, NULL/*value*/, 0/*refcount*/ }
/**
   Empty-initialized cson_value object.
*/
extern const cson_value cson_value_empty;

const cson_value cson_value_empty = cson_value_empty_m;
const cson_parse_opt cson_parse_opt_empty = cson_parse_opt_empty_m;
const cson_output_opt cson_output_opt_empty = cson_output_opt_empty_m;
const cson_object_iterator cson_object_iterator_empty = cson_object_iterator_empty_m;
const cson_buffer cson_buffer_empty = cson_buffer_empty_m;
const cson_parse_info cson_parse_info_empty = cson_parse_info_empty_m;

static void cson_value_destroy_zero_it( cson_value * self );
static void cson_value_destroy_free( cson_value * self );
static void cson_value_destroy_object( cson_value * self );
static void cson_value_destroy_integer( cson_value * self );
/**
   If self is-a array then this function destroys its contents,
   else this function does nothing.
*/
static void cson_value_destroy_array( cson_value * self );

/**
   If self is-a string then this function destroys its contents,
   else this function does nothing.
*/
static void cson_value_destroy_string( cson_value * self );

static const cson_value_api cson_value_api_null = { CSON_TYPE_NULL, cson_value_destroy_zero_it };
static const cson_value_api cson_value_api_undef = { CSON_TYPE_UNDEF, cson_value_destroy_zero_it };
static const cson_value_api cson_value_api_bool = { CSON_TYPE_BOOL, cson_value_destroy_zero_it };
static const cson_value_api cson_value_api_integer = { CSON_TYPE_INTEGER, cson_value_destroy_integer };
static const cson_value_api cson_value_api_double = { CSON_TYPE_DOUBLE, cson_value_destroy_free };
static const cson_value_api cson_value_api_string = { CSON_TYPE_STRING, cson_value_destroy_string };
static const cson_value_api cson_value_api_array = { CSON_TYPE_ARRAY, cson_value_destroy_array };
static const cson_value_api cson_value_api_object = { CSON_TYPE_OBJECT, cson_value_destroy_object };

static const cson_value cson_value_undef = { &cson_value_api_undef, NULL, 0 };
static const cson_value cson_value_bool_empty = { &cson_value_api_bool, NULL, 0 };
static const cson_value cson_value_integer_empty = { &cson_value_api_integer, NULL, 0 };
static const cson_value cson_value_double_empty = { &cson_value_api_double, NULL, 0 };
static const cson_value cson_value_string_empty = { &cson_value_api_string, NULL, 0 };
static const cson_value cson_value_array_empty = { &cson_value_api_array, NULL, 0 };
static const cson_value cson_value_object_empty = { &cson_value_api_object, NULL, 0 };


struct cson_string
{
    unsigned int length;
};
#define cson_string_empty_m {0/*length*/}

/**
 
 Holds special shared "constant" (though they are non-const)
 values.
 
*/
static struct CSON_EMPTY_HOLDER_
{
    char trueValue;
    cson_string stringValue;
} CSON_EMPTY_HOLDER = {
    1/*trueValue*/,
    cson_string_empty_m
};

/**
    Indexes into the CSON_SPECIAL_VALUES array.
    
    If this enum changes in any way,
    makes damned sure that CSON_SPECIAL_VALUES is updated
    to match!!!
*/
enum CSON_INTERNAL_VALUES {
    
    CSON_VAL_UNDEF = 0,
    CSON_VAL_NULL = 1,
    CSON_VAL_TRUE = 2,
    CSON_VAL_FALSE = 3,
    CSON_VAL_INT_0 = 4,
    CSON_VAL_DBL_0 = 5,
    CSON_VAL_STR_EMPTY = 6,
    CSON_INTERNAL_VALUES_LENGTH
};

/**
  Some "special" shared cson_value instances.

  These values MUST be initialized in the order specified
  by the CSON_INTERNAL_VALUES enum.
   
  Note that they are not const because they are used as
  shared-allocation objects in non-const contexts. However, the
  public API provides no way to modifying them, and clients who
  modify values directly are subject to The Wrath of Undefined
  Behaviour.
*/
static cson_value CSON_SPECIAL_VALUES[] = {
{ &cson_value_api_undef, NULL, 0 }, /* UNDEF */
{ &cson_value_api_null, NULL, 0 }, /* NULL */
{ &cson_value_api_bool, &CSON_EMPTY_HOLDER.trueValue, 0 }, /* TRUE */
{ &cson_value_api_bool, NULL, 0 }, /* FALSE */
{ &cson_value_api_integer, NULL, 0 }, /* INT_0 */
{ &cson_value_api_double, NULL, 0 }, /* DBL_0 */
{ &cson_value_api_string, &CSON_EMPTY_HOLDER.stringValue, 0 }, /* STR_EMPTY */
{ 0, NULL, 0 }
};


/**
    Returns non-0 (true) if m is one of our special
    "built-in" values, e.g. from CSON_SPECIAL_VALUES and some
    "empty" values.
     
    If this returns true, m MUST NOT be free()d!
 */
static char cson_value_is_builtin( void const * m )
{
    if((m >= (void const *)&CSON_EMPTY_HOLDER)
        && ( m < (void const *)(&CSON_EMPTY_HOLDER+1)))
        return 1;
    else return
        ((m > (void const *)&CSON_SPECIAL_VALUES[0])
        && ( m < (void const *)&CSON_SPECIAL_VALUES[CSON_INTERNAL_VALUES_LENGTH]) )
        ? 1
        : 0;
}

char const * cson_rc_string(int rc)
{
    if(0 == rc) return "OK";
#define CHECK(N) else if(cson_rc.N == rc ) return #N
    CHECK(OK);
    CHECK(ArgError);
    CHECK(RangeError);
    CHECK(TypeError);
    CHECK(IOError);
    CHECK(AllocError);
    CHECK(NYIError);
    CHECK(InternalError);
    CHECK(UnsupportedError);
    CHECK(NotFoundError);
    CHECK(UnknownError);
    CHECK(Parse_INVALID_CHAR);
    CHECK(Parse_INVALID_KEYWORD);
    CHECK(Parse_INVALID_ESCAPE_SEQUENCE);
    CHECK(Parse_INVALID_UNICODE_SEQUENCE);
    CHECK(Parse_INVALID_NUMBER);
    CHECK(Parse_NESTING_DEPTH_REACHED);
    CHECK(Parse_UNBALANCED_COLLECTION);
    CHECK(Parse_EXPECTED_KEY);
    CHECK(Parse_EXPECTED_COLON);
    else return "UnknownError";
#undef CHECK
}

/**
   If CSON_LOG_ALLOC is true then the cson_malloc/realloc/free() routines
   will log a message to stderr.
*/
#define CSON_LOG_ALLOC 0

/**
   A test/debug macro for simulating an OOM after the given number of
   bytes have been allocated.
*/
#define CSON_SIMULATE_OOM 0
#if CSON_SIMULATE_OOM
static unsigned int cson_totalAlloced = 0;
#endif

/** Simple proxy for malloc(). descr is a description of the allocation. */
static void * cson_malloc( size_t n, char const * descr )
{
#if CSON_LOG_ALLOC
    fprintf(stderr, "Allocating %u bytes [%s].\n", (unsigned int)n, descr);
#endif
#if CSON_SIMULATE_OOM
    cson_totalAlloced += n;
    if( cson_totalAlloced > CSON_SIMULATE_OOM )
    {
        return NULL;
    }
#endif
    return malloc(n);
}

/** Simple proxy for free(). descr is a description of the memory being freed. */
static void cson_free( void * p, char const * descr )
{
#if CSON_LOG_ALLOC
    fprintf(stderr, "Freeing @%p [%s].\n", p, descr);
#endif
    if( !cson_value_is_builtin(p) )
    {
        free( p );
    }
}
/** Simple proxy for realloc(). descr is a description of the (re)allocation. */
static void * cson_realloc( void * hint, size_t n, char const * descr )
{
#if CSON_LOG_ALLOC
    fprintf(stderr, "%sllocating %u bytes [%s].\n",
            hint ? "Rea" : "A",
            (unsigned int)n, descr);
#endif
#if CSON_SIMULATE_OOM
    cson_totalAlloced += n;
    if( cson_totalAlloced > CSON_SIMULATE_OOM )
    {
        return NULL;
    }
#endif
    if( 0==n )
    {
         cson_free(hint, descr);
         return NULL;
    }
    else
    {
        return realloc( hint, n );
    }
}


#undef CSON_LOG_ALLOC
#undef CSON_SIMULATE_OOM



/**
   CLIENTS CODE SHOULD NEVER USE THIS because it opens up doors to
   memory leaks if it is not used in very controlled circumstances.
   Users must be very aware of how the underlying memory management
   works.

   Frees any resources owned by val, but does not free val itself
   (which may be stack-allocated). If !val or val->api or
   val->api->cleanup are NULL then this is a no-op.

   If v is a container type (object or array) its children are also
   cleaned up (BUT NOT FREED), recursively.

   After calling this, val will have the special "undefined" type.
*/
static void cson_value_clean( cson_value * val );

/**
   Increments cv's reference count by 1.  As a special case, values
   for which cson_value_is_builtin() returns true are not
   modified. assert()s if (NULL==cv).
*/
static void cson_refcount_incr( cson_value * cv )
{
    assert( NULL != cv );
    if( cson_value_is_builtin( cv ) )
    { /* do nothing: we do not want to modify the shared
         instances.
      */
        return;
    }
    else
    {
        ++cv->refcount;
    }
}

#if 0
int cson_value_refcount_set( cson_value * cv, unsigned short rc )
{
    if( NULL == cv ) return cson_rc.ArgError;
    else
    {
        cv->refcount = rc;
        return 0;
    }
}
#endif

int cson_value_add_reference( cson_value * cv )
{
    if( NULL == cv ) return cson_rc.ArgError;
    else if( (cv->refcount+1) < cv->refcount )
    {
        return cson_rc.RangeError;
    }
    else
    {
        cson_refcount_incr( cv );
        return 0;
    }
}

/**
   If cv is NULL or cson_value_is_builtin(cv) returns true then this
   function does nothing and returns 0, otherwise...  If
   cv->refcount is 0 or 1 then cson_value_clean(cv) is called, cv is
   freed, and 0 is returned. If cv->refcount is any other value then
   it is decremented and the new value is returned.
*/
static cson_counter_t cson_refcount_decr( cson_value * cv )
{
    if( (NULL == cv) || cson_value_is_builtin(cv) ) return 0;
    else if( (0 == cv->refcount) || (0 == --cv->refcount) )
    {
        cson_value_clean(cv);
        cson_free(cv,"cson_value::refcount=0");
        return 0;
    }
    else return cv->refcount;
}

/**
   Allocates a new cson_string object with enough space for
   the given number of bytes. A byte for a NUL terminator
   is added automatically. Use cson_string_str() to get
   access to the string bytes, which will be len bytes long.

   len may be 0, in which case the internal string is "", as opposed
   to null. This is because the string bytes and the cson_string are
   allocated in a single chunk of memory, and the cson_string object
   does not directly provide (or have) a pointer to the string bytes.
*/
static cson_string * cson_string_alloc(unsigned int len)
{
    if( ! len ) return &CSON_EMPTY_HOLDER.stringValue;
    else
    {
        cson_string * s = NULL;
        const size_t msz = sizeof(cson_string) + len + 1 /*NUL*/;
        unsigned char * mem = NULL;
        if( msz < (sizeof(cson_string)+len) ) /*overflow*/ return NULL;
        mem = (unsigned char *)cson_malloc( msz, "cson_string_alloc" );
        if( mem )
        {
            memset( mem, 0, msz );
            s = (cson_string *)mem;
            s->length = len;
        }
        return s;
    }
}

unsigned int cson_string_length_bytes( cson_string const * str )
{
    return str ? str->length : 0;
}


/**
   Fetches v's string value as a non-const string.

   cson_strings are supposed to be immutable, but this form provides
   access to the immutable bits, which are v->length bytes long. A
   length-0 string is returned as NULL from here, as opposed to
   "". (This is a side-effect of the string allocation mechanism.)
   Returns NULL if !v.
*/
static char * cson_string_str(cson_string *v)
{
    /*
      See http://groups.google.com/group/comp.lang.c.moderated/browse_thread/thread/2e0c0df5e8a0cd6a
    */
#if 1
    if( !v || (&CSON_EMPTY_HOLDER.stringValue == v) ) return NULL;
    else return (char *)((unsigned char *)( v+1 ));
#else
    static char empty[2] = {0,0};
    return ( NULL == v )
        ? NULL
        : (v->length
           ? (char *) (((unsigned char *)v) + sizeof(cson_string))
           : empty)
        ;
#endif
}

/**
   Fetches v's string value as a const string.
*/
char const * cson_string_cstr(cson_string const *v)
{
    /*
      See http://groups.google.com/group/comp.lang.c.moderated/browse_thread/thread/2e0c0df5e8a0cd6a
    */
#if 1
    if( ! v ) return NULL;
    else if( v == &CSON_EMPTY_HOLDER.stringValue ) return "";
    else return (char *)((unsigned char *)(v+1));
#else
    return (NULL == v)
        ? NULL
        : (v->length
           ? (char const *) ((unsigned char const *)(v+1))
           : "");
#endif
}


#if 0
/**
   Just like strndup(3), in that neither are C89/C99-standard and both
   are documented in detail in strndup(3).
*/
static char * cson_strdup( char const * src, size_t n )
{
    char * rc = (char *)cson_malloc(n+1, "cson_strdup");
    if( ! rc ) return NULL;
    memset( rc, 0, n+1 );
    rc[n] = 0;
    return strncpy( rc, src, n );
}
#endif

/**
   Allocates a new cson_string() from the the first n bytes of src.
   Returns NULL on allocation error, else the caller owns the returned
   object and must eventually free() it.
*/
static cson_string * cson_string_strdup( char const * src, size_t n )
{
    cson_string * cs = cson_string_alloc(n);
    if( ! cs ) return NULL;
    else if( &CSON_EMPTY_HOLDER.stringValue == cs ) return cs;
    else
    {
        char * cstr = cson_string_str(cs);
        assert( cs->length == n );
        if( cstr && n )
        {
            strncpy( cstr, src, n );
        }
        return cs;
    }
}


int cson_string_cmp_cstr_n( cson_string const * str, char const * other, unsigned int otherLen )
{
    if( ! other && !str ) return 0;
    else if( other && !str ) return 1;
    else if( str && !other ) return -1;
    else if( !otherLen ) return  str->length ? 1 : 0;
    else if( !str->length ) return otherLen ? -1 : 0;
    else
    {
        unsigned const int max = (otherLen > str->length) ? otherLen : str->length;
        int const rc = strncmp( cson_string_cstr(str), other, max );
        return ( (0 == rc) && (otherLen != str->length) )
            ? (str->length < otherLen) ? -1 : 1
            : rc;
    }
}

int cson_string_cmp_cstr( cson_string const * lhs, char const * rhs )
{
    return cson_string_cmp_cstr_n( lhs, rhs, (rhs&&*rhs) ? strlen(rhs) : 0 );
}
int cson_string_cmp( cson_string const * lhs, cson_string const * rhs )
{
    return cson_string_cmp_cstr_n( lhs, cson_string_cstr(rhs), rhs ? rhs->length : 0 );
}


/**
   If self is not NULL, *self is overwritten to have the undefined
   type. self is not cleaned up or freed.
*/
void cson_value_destroy_zero_it( cson_value * self )
{
    if( self )
    {
        *self = cson_value_undef;
    }
}

/**
   If self is not null, free(self->value) is called.  *self is then
   overwritten to have the undefined type. self is not freed.

*/
void cson_value_destroy_free( cson_value * self )
{
    if(self) {
        if( self->value )
        {
            cson_free(self->value,"cson_value_destroy_free()");
        }
        *self = cson_value_undef;
    }
}



/**
   A key/value pair collection.

   Each of these objects owns its key/value pointers, and they
   are cleaned up by cson_kvp_clean().
*/
struct cson_kvp
{
    cson_string * key;
    cson_value * value;
};
#define cson_kvp_empty_m {NULL,NULL}
static const cson_kvp cson_kvp_empty = cson_kvp_empty_m;

/** @def CSON_OBJECT_PROPS_SORT

   If CSON_OBJECT_PROPS_SORT is set to a true value then
   qsort() and bsearch() are used to sort (upon insertion)
   and search cson_object::kvp property lists. This costs us
   a re-sort on each insertion but searching is O(log n)
   average/worst case (and O(1) best-case).

   i'm not yet convinced that the overhead of the qsort() justifies
   the potentially decreased search times - it has not been
   measured. Object property lists tend to be relatively short in
   JSON, and a linear search which uses the cson_string::length
   property as a quick check is quite fast when one compares it with
   the sort overhead required by the bsearch() approach.
*/
#define CSON_OBJECT_PROPS_SORT 0

/** @def CSON_OBJECT_PROPS_SORT_USE_LENGTH

    Don't use this - i'm not sure that it works how i'd like.

    If CSON_OBJECT_PROPS_SORT_USE_LENGTH is true then
    we use string lengths as quick checks when sorting
    property keys. This leads to a non-intuitive sorting
    order but "should" be faster.

    This is ignored if CSON_OBJECT_PROPS_SORT is false.

*/
#define CSON_OBJECT_PROPS_SORT_USE_LENGTH 0

#if CSON_OBJECT_PROPS_SORT

#if 0
/* i would prefer case-insensitive sorting but keys need
   to be case-sensitive for search purposes.

   TODO? write a comparitor which reverses the default
   sort order of upper/lower-case. The current behaviour
   is a bit non-intuitive, where 'Z' < 'a'.
*/
static int cson_kvp_strcmp( char const * l, char const * r )
{
    char cl;
    char cr;
    for( ; *l && *r; ++l, ++r )
    {
        cl = tolower(*l);
        cr = tolower(*r);
        if( cl < cr ) return -1;
        else if( cl > cr ) return 1;
    }
    if( !*l && !*r ) return 0;
    else return (*l) ? 1 : -1;
}
#endif

/**
   cson_kvp comparator for use with qsort(). ALMOST compares with
   strcmp() semantics, but it uses the strings' lengths as a quicker
   approach. This might give non-intuitive results, but it's faster.
 */
static int cson_kvp_cmp( void const * lhs, void const * rhs )
{
    cson_kvp const * lk = *((cson_kvp const * const*)lhs);
    cson_kvp const * rk = *((cson_kvp const * const*)rhs);
    cson_string const * l = lk->key;
    cson_string const * r = rk->key;
#if CSON_OBJECT_PROPS_SORT_USE_LENGTH
    if( l->length < r->length ) return -1;
    else if( l->length > r->length ) return 1;
    else return strcmp( cson_string_cstr( l ), cson_string_cstr( r ) );
#else
    return strcmp( cson_string_cstr( l ),
                   cson_string_cstr( r ) );
#endif /*CSON_OBJECT_PROPS_SORT_USE_LENGTH*/
}
#endif /*CSON_OBJECT_PROPS_SORT*/


#if CSON_OBJECT_PROPS_SORT
/**
   A bsearch() comparison function which requires that lhs be a (char
   const *) and rhs be-a (cson_kvp const * const *). It compares lhs
   to rhs->key's value, using strcmp() semantics.
 */
static int cson_kvp_cmp_vs_cstr( void const * lhs, void const * rhs )
{
    char const * lk = (char const *)lhs;
    cson_kvp const * rk =
        *((cson_kvp const * const*)rhs)
        ;
#if CSON_OBJECT_PROPS_SORT_USE_LENGTH
    unsigned int llen = strlen(lk);
    if( llen < rk->key->length ) return -1;
    else if( llen > rk->key->length ) return 1;
    else return strcmp( lk, cson_string_cstr( rk->key ) );
#else
    return strcmp( lk, cson_string_cstr( rk->key ) );
#endif /*CSON_OBJECT_PROPS_SORT_USE_LENGTH*/
}
#endif /*CSON_OBJECT_PROPS_SORT*/


struct cson_kvp_list
{
    cson_kvp ** list;
    unsigned int count;
    unsigned int alloced;
};
typedef struct cson_kvp_list cson_kvp_list;
#define cson_kvp_list_empty_m {NULL/*list*/,0/*count*/,0/*alloced*/}
static const cson_kvp_list cson_kvp_list_empty = cson_kvp_list_empty_m;

struct cson_object
{
    cson_kvp_list kvp;
};
/*typedef struct cson_object cson_object;*/
#define cson_object_empty_m { cson_kvp_list_empty_m/*kvp*/ }
static const cson_object cson_object_empty = cson_object_empty_m;

struct cson_value_list
{
    cson_value ** list;
    unsigned int count;
    unsigned int alloced;
};
typedef struct cson_value_list cson_value_list;
#define cson_value_list_empty_m {NULL/*list*/,0/*count*/,0/*alloced*/}
static const cson_value_list cson_value_list_empty = cson_value_list_empty_m;

struct cson_array
{
    cson_value_list list;
};
/*typedef struct cson_array cson_array;*/
#define cson_array_empty_m { cson_value_list_empty_m/*list*/ }
static const cson_array cson_array_empty = cson_array_empty_m;


struct cson_parser
{
    JSON_parser p;
    cson_value * root;
    cson_value * node;
    cson_array stack;
    cson_string * ckey;
    int errNo;
    unsigned int totalKeyCount;
    unsigned int totalValueCount;
};
typedef struct cson_parser cson_parser;
static const cson_parser cson_parser_empty = {
NULL/*p*/,
NULL/*root*/,
NULL/*node*/,
cson_array_empty_m/*stack*/,
NULL/*ckey*/,
0/*errNo*/,
0/*totalKeyCount*/,
0/*totalValueCount*/
};

#if 1
/* The following funcs are declared in generated code (cson_lists.h),
   but we need early access to their decls for the Amalgamation build.
*/
static unsigned int cson_value_list_reserve( cson_value_list * self, unsigned int n );
static unsigned int cson_kvp_list_reserve( cson_kvp_list * self, unsigned int n );
static int cson_kvp_list_append( cson_kvp_list * self, cson_kvp * cp );
static void cson_kvp_list_clean( cson_kvp_list * self,
                                 void (*cleaner)(cson_kvp * obj) );
#if 0
static int cson_value_list_append( cson_value_list * self, cson_value * cp );
static void cson_value_list_clean( cson_value_list * self, void (*cleaner)(cson_value * obj));
static int cson_kvp_list_visit( cson_kvp_list * self,
                                int (*visitor)(cson_kvp * obj, void * visitorState ),
                                void * visitorState );
static int cson_value_list_visit( cson_value_list * self,
                                  int (*visitor)(cson_value * obj, void * visitorState ),
                                  void * visitorState );
#endif
#endif
    
#if 0
#  define LIST_T cson_value_list
#  define VALUE_T cson_value *
#  define VALUE_T_IS_PTR 1
#  define LIST_T cson_kvp_list
#  define VALUE_T cson_kvp *
#  define VALUE_T_IS_PTR 1
#else
#endif

/*
  Reminders to self:

  - 20110126: moved cson_value_new() and cson_value_set_xxx() out of the
  public API because:

  a) They can be easily mis-used to cause memory leaks, even when used in
  a manner which seems relatively intuitive.

  b) Having them in the API prohibits us from eventually doing certain
  allocation optimizations like not allocating Booleans,
  Integer/Doubles with the value 0, or empty Strings. The main problem
  is that cson_value_set_xxx() cannot be implemented properly if we
  add that type of optimization.
*/

/**
   Allocates a new value with the "undefined" value and transfers
   ownership of it to the caller. Use The cson_value_set_xxx() family
   of functions to assign a typed value to it. It must eventually be
   destroyed, by the caller or its owning container, by passing it to
   cson_value_free().

   Returns NULL on allocation error.

   @see cson_value_new_array()
   @see cson_value_new_object()
   @see cson_value_new_string()
   @see cson_value_new_integer()
   @see cson_value_new_double()
   @see cson_value_new_bool()
   @see cson_value_free()
*/
static cson_value * cson_value_new();
/**
   Cleans any existing contents of val and sets its new value
   to the special NULL value.

   Returns 0 on success.

*/
#if 0
static int cson_value_set_null( cson_value * val );
#endif
/**
   Cleans any existing contents of val and sets its new value
   to v.

   Returns 0 on success.

*/
#if 0
static int cson_value_set_bool( cson_value * val, char v );
#endif
/**
   Cleans any existing contents of val and sets its new value
   to v.

   Returns 0 on success.

*/
static int cson_value_set_integer( cson_value * val, cson_int_t v );
/**
   Cleans any existing contents of val and sets its new value
   to v.

   Returns 0 on success.
*/
static int cson_value_set_double( cson_value * val, cson_double_t v );

/**
   Cleans any existing contents of val and sets its new value to
   str. On success, ownership of str is passed on to val. On error
   ownership is not changed.

   Returns 0 on success.

   If str is NULL, (!*str), or (!len) then this function does not
   allocate any memory for a new string, and cson_value_fetch_string()
   will return an empty string as opposed to a NULL string.
*/
static int cson_value_set_string( cson_value * val, char const * str, unsigned int len );


cson_value * cson_value_new()
{
    cson_value * v = (cson_value *)cson_malloc(sizeof(cson_value),"cson_value_new");
    if( v ) *v = cson_value_undef;
    return v;
}


void cson_value_free(cson_value *v)
{
    cson_refcount_decr( v );
}

#if 0 /* we might actually want this later on. */
/** Returns true if v is not NULL and has the given type ID. */
static char cson_value_is_a( cson_value const * v, cson_type_id is )
{
    return (v && v->api && (v->api->typeID == is)) ? 1 : 0;
}
#endif

#if 0
cson_type_id cson_value_type_id( cson_value const * v )
{
    return (v && v->api) ? v->api->typeID : CSON_TYPE_UNDEF;
}
#endif

char cson_value_is_undef( cson_value const * v )
{
    /**
       This special-case impl is needed because the underlying
       (generic) list operations do not know how to populate
       new entries
     */
    return ( !v || !v->api || (v->api==&cson_value_api_undef))
        ? 1 : 0;
}
#define ISA(T,TID) char cson_value_is_##T( cson_value const * v ) {       \
        /*return (v && v->api) ? cson_value_is_a(v,CSON_TYPE_##TID) : 0;*/ \
        return (v && (v->api == &cson_value_api_##T)) ? 1 : 0; \
    } static const char bogusPlaceHolderForEmacsIndention##TID = CSON_TYPE_##TID
ISA(null,NULL);
ISA(bool,BOOL);
ISA(integer,INTEGER);
ISA(double,DOUBLE);
ISA(string,STRING);
ISA(array,ARRAY);
ISA(object,OBJECT);
#undef ISA
char cson_value_is_number( cson_value const * v )
{
    return cson_value_is_integer(v) || cson_value_is_double(v);
}


void cson_value_clean( cson_value * val )
{
    if( val && val->api && val->api->cleanup )
    {
        if( ! cson_value_is_builtin( val ) )
        {
            cson_counter_t const rc = val->refcount;
            val->api->cleanup(val);
            *val = cson_value_undef;
            val->refcount = rc;
        }
    }
}

static void cson_value_destroy_integer( cson_value * self )
{
    if( self )
    {
#if !CSON_VOID_PTR_IS_BIG
        cson_free(self->value,"cson_int_t");
#endif
        *self = cson_value_empty;
    }    
}
static int cson_value_set_integer( cson_value * val, cson_int_t v )
{
    if( ! val ) return cson_rc.ArgError;
    else
    {
#if CSON_VOID_PTR_IS_BIG
        cson_value_clean( val );
        val->value = (void *)v;
#else
        cson_int_t * iv = NULL;
        cson_value_clean( val );
        iv = (cson_int_t*)cson_malloc(sizeof(cson_int_t), "cson_int_t");
        if( ! iv ) return cson_rc.AllocError;
        *iv = v;
        val->value = iv;
#endif
        val->api = &cson_value_api_integer;
        return 0;
    }
}

static int cson_value_set_double( cson_value * val, cson_double_t v )
{
    if( ! val ) return cson_rc.ArgError;
    else
    {
        cson_double_t * rv = NULL;
        cson_value_clean( val );
        val->api = &cson_value_api_double;
        if( 0.0 != v )
        {
            /*
              Reminder: we can't re-use val if it alreay is-a double
              because we have no reference counting.
            */
            rv = (cson_double_t*)cson_malloc(sizeof(cson_double_t),"double");
            if( ! rv ) return cson_rc.AllocError;
        }
        if(NULL != rv) *rv = v;
        val->value = rv;
        return 0;
    }
}

static cson_string * cson_string_shared_empty()
{
    /**
       We have code in place elsewhere to avoid that
       cson_string_cstr(&bob) and cson_string_str(&bob) will misbehave
       by accessing the bytes directly after bob (which are undefined
       in this case).
    */
#if 0
    static cson_string bob[2] = {cson_string_empty_m,
                                 cson_string_empty_m/*trick to 0-init bob[0]'s tail*/};
    return &bob[0];
#else
    static cson_string bob = cson_string_empty_m;
    return &bob;
#endif
}

static int cson_value_set_string( cson_value * val, char const * str, unsigned int len )
{
    cson_string * jstr = NULL;
    if( ! val ) return cson_rc.ArgError;
    cson_value_clean( val );
    val->api = &cson_value_api_string;
    if( !str || !*str || !len )
    {
        val->value = cson_string_shared_empty();
        return 0;
    }
    else
    {
        jstr = cson_string_alloc( len );
        if( NULL == jstr ) return cson_rc.AllocError;
        else
        {
            if( len )
            {
                char * dest = cson_string_str( jstr );
                val->value = jstr;
                strncpy( dest, str, len );
            }
            /* else it's the empty string special value */
            return 0;
        }
    }
}


static cson_value * cson_value_array_alloc()
{
    cson_value * v = (cson_value*)cson_malloc(sizeof(cson_value),"cson_value_array");
    if( NULL != v )
    {
        cson_array * ar = (cson_array *)cson_malloc(sizeof(cson_array),"cson_array");
        if( ! ar )
        {
            cson_free(v,"cson_array");
            v = NULL;
        }
        else
        {
            *ar = cson_array_empty;
            *v = cson_value_array_empty;
            v->value = ar;
        }
    }
    return v;
}

static cson_value * cson_value_object_alloc()
{
    cson_value * v = (cson_value*)cson_malloc(sizeof(cson_value),"cson_value_object");
    if( NULL != v )
    {
        cson_object * obj = (cson_object*)cson_malloc(sizeof(cson_object),"cson_value");
        if( ! obj )
        {
            cson_free(v,"cson_value_object");
            v = NULL;
        }
        else
        {
            *obj = cson_object_empty;
            *v = cson_value_object_empty;
            v->value = obj;
        }
    }
    return v;
}

cson_value * cson_value_new_object()
{
    return cson_value_object_alloc();
}

cson_value * cson_value_new_array()
{
    return cson_value_array_alloc();
}

/**
   Frees kvp->key and kvp->value and sets them to NULL, but does not free
   kvp. If !kvp then this is a no-op.
*/
static void cson_kvp_clean( cson_kvp * kvp )
{
    if( kvp )
    {
        if(kvp->key)
        {
            cson_free(kvp->key,"cson_kvp::key");
            kvp->key = NULL;
        }
        if(kvp->value)
        {
            cson_value_free( kvp->value );
            kvp->value = NULL;
        }
    }
}

cson_string const * cson_kvp_key( cson_kvp const * kvp )
{
    return kvp ? kvp->key : NULL;
}
cson_value * cson_kvp_value( cson_kvp const * kvp )
{
    return kvp ? kvp->value : NULL;
}


/**
   Calls cson_kvp_clean(kvp) and then frees kvp.
*/
static void cson_kvp_free( cson_kvp * kvp )
{
    if( kvp )
    {
        cson_kvp_clean(kvp);
        cson_free(kvp,"cson_kvp");
    }
}

/**
   cson_value_api::destroy_value() impl for Object
   values. Cleans up self-owned memory and overwrites
   self to have the undefined value, but does not
   free self.

   If self->value == cson_string_shared_empty()
   then this function does not actually free it.
*/
static void cson_value_destroy_string( cson_value * self )
{
    if(self && self->value) {
        cson_string * obj = (cson_string *)self->value;
        if( obj != cson_string_shared_empty() )
        {
            cson_free(self->value,"cson_string");
        }
        *self = cson_value_undef;
    }
}


/**
   cson_value_api::destroy_value() impl for Object
   values. Cleans up self-owned memory and overwrites
   self to have the undefined value, but does not
   free self.
*/
static void cson_value_destroy_object( cson_value * self )
{
    if(self && self->value) {
        cson_object * obj = (cson_object *)self->value;
        assert( self->value == obj );
        cson_kvp_list_clean( &obj->kvp, cson_kvp_free );
        cson_free(self->value,"cson_object");
        *self = cson_value_undef;
    }
}

/**
   Cleans up the contents of ar->list, but does not free ar.

   After calling this, ar will have a length of 0.

   If properlyCleanValues is 1 then cson_value_free() is called on
   each non-NULL item, otherwise the outer list is destroyed but the
   individual items are assumed to be owned by someone else and are
   not freed.
*/
static void cson_array_clean( cson_array * ar, char properlyCleanValues )
{
    if( ar )
    {
        unsigned int i = 0;
        cson_value * val = NULL;
        for( ; i < ar->list.count; ++i )
        {
            val = ar->list.list[i];
            if(val)
            {
                ar->list.list[i] = NULL;
                if( properlyCleanValues )
                {
                    cson_value_free( val );
                }
            }
        }
        cson_value_list_reserve(&ar->list,0);
        ar->list = cson_value_list_empty
            /* Pedantic note: reserve(0) already clears the list-specific
               fields, but we do this just in case we ever add new fields
               to cson_value_list which are not used in the reserve() impl.
             */
            ;
    }
}

/**
   cson_value_api::destroy_value() impl for Array
   values. Cleans up self-owned memory and overwrites
   self to have the undefined value, but does not
   free self.
*/
static void cson_value_destroy_array( cson_value * self )
{
    cson_array * ar = cson_value_get_array(self);
    if(ar) {
        assert( self->value == ar );
        cson_array_clean( ar, 1 );
        cson_free(ar,"cson_array");
        *self = cson_value_undef;
    }
}


#if 0
static void cson_kvp_list_item_clean( void * kvp );
static void cson_value_list_item_clean( void * val );
void cson_value_list_item_clean( void * val_ )
{
    cson_value * val = (cson_value*)val_;
    if( val )
    {
        cson_value_clean(val);
    }
}
void cson_kvp_list_item_clean( void * val )
{
    cson_kvp * kvp = (cson_kvp *)val;
    if( kvp )
    {
        cson_free(kvp->key,"cson_kvp::key");
        cson_value_clean( &kvp->value );
    }
}
#endif

int cson_buffer_fill_from( cson_buffer * dest, cson_data_source_f src, void * state )
{
    int rc;
    enum { BufSize = 1024 * 4 };
    char rbuf[BufSize];
    size_t total = 0;
    unsigned int rlen = 0;
    if( ! dest || ! src ) return cson_rc.ArgError;
    dest->used = 0;
    while(1)
    {
        rlen = BufSize;
        rc = src( state, rbuf, &rlen );
        if( rc ) break;
        total += rlen;
        if( dest->capacity < (total+1) )
        {
            rc = cson_buffer_reserve( dest, total + 1);
            if( 0 != rc ) break;
        }
        memcpy( dest->mem + dest->used, rbuf, rlen );
        dest->used += rlen;
        if( rlen < BufSize ) break;
    }
    if( !rc && dest->used )
    {
        assert( dest->used < dest->capacity );
        dest->mem[dest->used] = 0;
    }
    return rc;
}

int cson_data_source_FILE( void * state, void * dest, unsigned int * n )
{
    FILE * f = (FILE*) state;
    if( ! state || ! n || !dest ) return cson_rc.ArgError;
    else if( !*n ) return cson_rc.RangeError;
    *n = (unsigned int)fread( dest, 1, *n, f );
    if( !*n )
    {
        return feof(f) ? 0 : cson_rc.IOError;
    }
    return 0;
}

int cson_parse_FILE( cson_value ** tgt, FILE * src,
                     cson_parse_opt const * opt, cson_parse_info * err )
{
    return cson_parse( tgt, cson_data_source_FILE, src, opt, err );
}


int cson_value_fetch_bool( cson_value const * val, char * v )
{
    /**
       FIXME: move the to-bool operation into cson_value_api, like we
       do in the C++ API.
     */
    if( ! val || !val->api ) return cson_rc.ArgError;
    else
    {
        int rc = 0;
        char b = 0;
        switch( val->api->typeID )
        {
          case CSON_TYPE_ARRAY:
          case CSON_TYPE_OBJECT:
              b = 1;
              break;
          case CSON_TYPE_STRING: {
              char const * str = cson_string_cstr(cson_value_get_string(val));
              b = (str && *str) ? 1 : 0;
              break;
          }
          case CSON_TYPE_UNDEF:
          case CSON_TYPE_NULL:
              break;
          case CSON_TYPE_BOOL:
              b = (NULL==val->value) ? 0 : 1;
              break;
          case CSON_TYPE_INTEGER: {
              cson_int_t i = 0;
              cson_value_fetch_integer( val, &i );
              b = i ? 1 : 0;
              break;
          }
          case CSON_TYPE_DOUBLE: {
              cson_double_t d = 0.0;
              cson_value_fetch_double( val, &d );
              b = (0.0==d) ? 0 : 1;
              break;
          }
          default:
              rc = cson_rc.TypeError;
              break;
        }
        if( v ) *v = b;
        return rc;
    }
}

char cson_value_get_bool( cson_value const * val )
{
    char i = 0;
    cson_value_fetch_bool( val, &i );
    return i;
}

int cson_value_fetch_integer( cson_value const * val, cson_int_t * v )
{
    if( ! val || !val->api ) return cson_rc.ArgError;
    else
    {
        cson_int_t i = 0;
        int rc = 0;
        switch(val->api->typeID)
        {
            case CSON_TYPE_UNDEF: 
            case CSON_TYPE_NULL:
              i = 0;
              break;
            case CSON_TYPE_BOOL: {
              char b = 0;
              cson_value_fetch_bool( val, &b );
              i = b;
              break;
            }
            case CSON_TYPE_INTEGER: {
#if CSON_VOID_PTR_IS_BIG
                i = (cson_int_t)val->value;
#else
                cson_int_t const * x = (cson_int_t const *)val->value;
                i = x ? *x : 0;
#endif
                break;
            }
            case CSON_TYPE_DOUBLE: {
              cson_double_t d = 0.0;
              cson_value_fetch_double( val, &d );
              i = (cson_int_t)d;
              break;
            }
            case CSON_TYPE_STRING:
            case CSON_TYPE_ARRAY:
            case CSON_TYPE_OBJECT:
            default:
              break;
        }
        if(v) *v = i;
        return rc;
    }
}

cson_int_t cson_value_get_integer( cson_value const * val )
{
    cson_int_t i = 0;
    cson_value_fetch_integer( val, &i );
    return i;
}

int cson_value_fetch_double( cson_value const * val, cson_double_t * v )
{
    if( ! val || !val->api ) return cson_rc.ArgError;
    else
    {
        cson_double_t d = 0.0;
        int rc = 0;
        switch(val->api->typeID)
        {
          case CSON_TYPE_UNDEF: 
          case CSON_TYPE_NULL:
              d = 0;
              break;
          case CSON_TYPE_BOOL: {
              char b = 0;
              cson_value_fetch_bool( val, &b );
              d = b ? 1.0 : 0.0;
              break;
          }
          case CSON_TYPE_INTEGER: {
              cson_int_t i = 0;
              cson_value_fetch_integer( val, &i );
              d = i;
              break;
          }
          case CSON_TYPE_DOUBLE: {
              cson_double_t const* dv = (cson_double_t const *)val->value;
              d = dv ? *dv : 0.0;
              break;
          }
          default:
              rc = cson_rc.TypeError;
              break;
        }
        if(v) *v = d;
        return rc;
    }
}

cson_double_t cson_value_get_double( cson_value const * val )
{
    cson_double_t i = 0.0;
    cson_value_fetch_double( val, &i );
    return i;
}

int cson_value_fetch_string( cson_value const * val, cson_string const ** dest )
{
    if( ! val || ! dest ) return cson_rc.ArgError;
    else if( ! cson_value_is_string(val) ) return cson_rc.TypeError;
    else
    {
        if( dest ) *dest = (cson_string const *)val->value;
        return 0;
    }
}

cson_string const * cson_value_get_string( cson_value const * val )
{
    cson_string const * rc = NULL;
    cson_value_fetch_string( val, &rc );
    return rc;
}

char const * cson_value_get_cstr( cson_value const * val )
{
    return cson_string_cstr( cson_value_get_string(val) );
}

int cson_value_fetch_object( cson_value const * val, cson_object ** obj )
{
    if( ! val ) return cson_rc.ArgError;
    else if( ! cson_value_is_object(val) ) return cson_rc.TypeError;
    else
    {
        if(obj) *obj = (cson_object*)val->value;
        return 0;
    }
}
cson_object * cson_value_get_object( cson_value const * v )
{
    cson_object * obj = NULL;
    cson_value_fetch_object( v, &obj );
    return obj;
}

int cson_value_fetch_array( cson_value const * val, cson_array ** ar)
{
    if( ! val ) return cson_rc.ArgError;
    else if( ! cson_value_is_array(val) ) return cson_rc.TypeError;
    else
    {
        if(ar) *ar = (cson_array*)val->value;
        return 0;
    }
}

cson_array * cson_value_get_array( cson_value const * v )
{
    cson_array * ar = NULL;
    cson_value_fetch_array( v, &ar );
    return ar;
}

cson_kvp * cson_kvp_alloc()
{
    cson_kvp * kvp = (cson_kvp*)cson_malloc(sizeof(cson_kvp),"cson_kvp");
    if( kvp )
    {
        *kvp = cson_kvp_empty;
    }
    return kvp;
}



int cson_array_append( cson_array * ar, cson_value * v )
{
    if( !ar || !v ) return cson_rc.ArgError;
    else if( (ar->list.count+1) < ar->list.count ) return cson_rc.RangeError;
    else
    {
        if( !ar->list.alloced || (ar->list.count == ar->list.alloced-1))
        {
            unsigned int const n = ar->list.count ? (ar->list.count*2) : 7;
            if( n > cson_value_list_reserve( &ar->list, n ) )
            {
                return cson_rc.AllocError;
            }
        }
        return cson_array_set( ar, ar->list.count, v );
    }
}

#if 0
/**
   Removes and returns the last value from the given array,
   shrinking its size by 1. Returns NULL if ar is NULL,
   ar->list.count is 0, or the element at that index is NULL.
   

   If removeRef is true then cson_value_free() is called to remove
   ar's reference count for the value. In that case NULL is returned,
   even if the object still has live references. If removeRef is false
   then the caller takes over ownership of that reference count point.

   If removeRef is false then the caller takes over ownership
   of the return value, otherwise ownership is effectively
   determined by any remaining references for the returned
   value.
*/
static cson_value * cson_array_pop_back( cson_array * ar,
                                         char removeRef )
{
    if( !ar ) return NULL;
    else if( ! ar->list.count ) return NULL;
    else
    {
        unsigned int const ndx = --ar->list.count;
        cson_value * v = ar->list.list[ndx];
        ar->list.list[ndx] = NULL;
        if( removeRef )
        {
            cson_value_free( v );
            v = NULL;
        }
        return v;
    }
}
#endif

cson_value * cson_value_new_bool( char v )
{
    return v ? &CSON_SPECIAL_VALUES[CSON_VAL_TRUE] : &CSON_SPECIAL_VALUES[CSON_VAL_FALSE];
}

cson_value * cson_value_true()
{
    return &CSON_SPECIAL_VALUES[CSON_VAL_TRUE];
}
cson_value * cson_value_false()
{
    return &CSON_SPECIAL_VALUES[CSON_VAL_FALSE];
}

cson_value * cson_value_null()
{
    return &CSON_SPECIAL_VALUES[CSON_VAL_NULL];
}

cson_value * cson_value_new_integer( cson_int_t v )
{
    if( 0 == v ) return &CSON_SPECIAL_VALUES[CSON_VAL_INT_0];
    else
    {
        cson_value * c = cson_value_new();
        if( c )
        {
            if( 0 != cson_value_set_integer( c, v ) )
            {
                cson_value_free(c);
                c = NULL;
            }
        }
        return c;
    }
}

cson_value * cson_value_new_double( cson_double_t v )
{
    if( 0.0 == v ) return &CSON_SPECIAL_VALUES[CSON_VAL_DBL_0];
    else
    {
        cson_value * c = cson_value_new();
        if( c )
        {
            if( 0 != cson_value_set_double( c, v ) )
            {
                cson_value_free(c);
                c = NULL;
            }
        }
        return c;
    }
}
cson_value * cson_value_new_string( char const * str, unsigned int len )
{
    if( !str || !len ) return &CSON_SPECIAL_VALUES[CSON_VAL_STR_EMPTY];
    else
    {
        cson_value * c = cson_value_new();
        if( c )
        {
            if( 0 != cson_value_set_string( c, str, len ) )
            {
                cson_value_free(c);
                c = NULL;
            }
        }
        return c;
    }
}

int cson_array_value_fetch( cson_array const * ar, unsigned int pos, cson_value ** v )
{
    if( !ar) return cson_rc.ArgError;
    if( pos >= ar->list.count ) return cson_rc.RangeError;
    else
    {
        if(v) *v = ar->list.list[pos];
        return 0;
    }
}

cson_value * cson_array_get( cson_array const * ar, unsigned int pos )
{
    cson_value *v = NULL;
    cson_array_value_fetch(ar, pos, &v);
    return v;
}

int cson_array_length_fetch( cson_array const * ar, unsigned int * v )
{
    if( ! ar || !v ) return cson_rc.ArgError;
    else
    {
        if(v) *v = ar->list.count;
        return 0;
    }
}

unsigned int cson_array_length_get( cson_array const * ar )
{
    unsigned int i = 0;
    cson_array_length_fetch(ar, &i);
    return i;
}

int cson_array_reserve( cson_array * ar, unsigned int size )
{
    if( ! ar ) return cson_rc.ArgError;
    else if( size <= ar->list.alloced )
    {
        /* We don't want to introduce a can of worms by trying to
           handle the cleanup from here.
        */
        return 0;
    }
    else
    {
        return (ar->list.alloced > cson_value_list_reserve( &ar->list, size ))
            ? cson_rc.AllocError
            : 0
            ;
    }
}

int cson_array_set( cson_array * ar, unsigned int ndx, cson_value * v )
{
    if( !ar || !v ) return cson_rc.ArgError;
    else if( (ndx+1) < ndx) /* overflow */return cson_rc.RangeError;
    else
    {
        unsigned const int len = cson_value_list_reserve( &ar->list, ndx+1 );
        if( len <= ndx ) return cson_rc.AllocError;
        else
        {
            cson_value * old = ar->list.list[ndx];
            if( old )
            {
                if(old == v) return 0;
                else cson_value_free(old);
            }
            cson_refcount_incr( v );
            ar->list.list[ndx] = v;
            if( ndx >= ar->list.count )
            {
                ar->list.count = ndx+1;
            }
            return 0;
        }
    }
}

/** @internal

   Searchs for the given key in the given object.

   Returns the found item on success, NULL on error.  If ndx is not
   NULL, it is set to the index (in obj->kvp.list) of the found
   item. *ndx is not modified if no entry is found.
*/
static cson_kvp * cson_object_search_impl( cson_object const * obj, char const * key, unsigned int * ndx )
{
    if( obj && key && *key && obj->kvp.count)
    {
#if CSON_OBJECT_PROPS_SORT
        cson_kvp ** s = (cson_kvp**)
            bsearch( key, obj->kvp.list,
                     obj->kvp.count, sizeof(cson_kvp*),
                     cson_kvp_cmp_vs_cstr );
        if( ndx && s )
        { /* index of found record is required by
             cson_object_unset(). Calculate the offset based on s...*/
#if 0
            *ndx = (((unsigned char const *)s - ((unsigned char const *)obj->kvp.list))
                   / sizeof(cson_kvp*));
#else
            *ndx = s - obj->kvp.list;
#endif
        }
        return s ? *s : NULL;
#else
        cson_kvp_list const * li = &obj->kvp;
        unsigned int i = 0;
        cson_kvp * kvp;
        const unsigned int klen = strlen(key);
        for( ; i < li->count; ++i )
        {
            kvp = li->list[i];
            assert( kvp && kvp->key );
            if( kvp->key->length != klen ) continue;
            else if(0==strcmp(key,cson_string_cstr(kvp->key)))
            {
                if(ndx) *ndx = i;
                return kvp;
            }
        }
#endif
    }
    return NULL;
}

cson_value * cson_object_get( cson_object const * obj, char const * key )
{
    cson_kvp * kvp = cson_object_search_impl( obj, key, NULL );
    return kvp ? kvp->value : NULL;
}

#if CSON_OBJECT_PROPS_SORT
static void cson_object_sort_props( cson_object * obj )
{
    assert( NULL != obj );
    if( obj->kvp.count )
    {
        qsort( obj->kvp.list, obj->kvp.count, sizeof(cson_kvp*),
               cson_kvp_cmp );
    }

}
#endif    

int cson_object_unset( cson_object * obj, char const * key )
{
    if( ! obj || !key || !*key ) return cson_rc.ArgError;
    else
    {
        unsigned int ndx = 0;
        cson_kvp * kvp = cson_object_search_impl( obj, key, &ndx );
        if( ! kvp )
        {
            return cson_rc.NotFoundError;
        }
        assert( obj->kvp.count > 0 );
        assert( obj->kvp.list[ndx] == kvp );
        cson_kvp_free( kvp );
        obj->kvp.list[ndx] = NULL;
        { /* if my brain were bigger i'd use memmove(). */
            unsigned int i = ndx;
            for( ; i < obj->kvp.count; ++i )
            {
                obj->kvp.list[i] =
                    (i < (obj->kvp.alloced-1))
                    ? obj->kvp.list[i+1]
                    : NULL;
            }
        }
        obj->kvp.list[--obj->kvp.count] = NULL;
#if CSON_OBJECT_PROPS_SORT
        cson_object_sort_props( obj );
#endif
        return 0;
    }
}

int cson_object_set( cson_object * obj, char const * key, cson_value * v )
{
    if( ! obj || !key || !*key ) return cson_rc.ArgError;
    else if( NULL == v )
    {
        return cson_object_unset( obj, key );
    }
    else
    {
        cson_kvp * kvp = cson_object_search_impl( obj, key, NULL );
        if( kvp )
        { /* "I told 'em we've already got one!" */
            if( v == kvp->value ) return 0 /* actually a usage error */;
            else
            {
                cson_value_free( kvp->value );
                cson_refcount_incr( v );
                kvp->value = v;
                return 0;
            }
        }
        if( !obj->kvp.alloced || (obj->kvp.count == obj->kvp.alloced-1))
        {
            unsigned int const n = obj->kvp.count ? (obj->kvp.count*2) : 7;
            if( n > cson_kvp_list_reserve( &obj->kvp, n ) )
            {
                return cson_rc.AllocError;
            }
        }
        { /* insert new item... */
            int rc = 0;
            cson_string * keycp = cson_string_strdup(key, strlen(key));
            if( ! keycp )
            {
                return cson_rc.AllocError;
            }
            kvp = cson_kvp_alloc();
            if( ! kvp )
            {
                cson_free(keycp,"cson_parser::key");
                return cson_rc.AllocError;
            }
            kvp->key = keycp /* transfer ownership */;
            rc = cson_kvp_list_append( &obj->kvp, kvp );
            if( 0 != rc )
            {
                cson_kvp_free(kvp);
            }
            else
            {
                cson_refcount_incr( v );
                kvp->value = v /* transfer ownership */;
#if CSON_OBJECT_PROPS_SORT
                cson_object_sort_props( obj );
#endif
            }
            return 0;
        }
    }
}

cson_value * cson_object_take( cson_object * obj, char const * key )
{
    if( ! obj || !key || !*key ) return NULL;
    else
    {
        /* FIXME: this is 90% identical to cson_object_unset(),
           only with different refcount handling.
           Consolidate them.
        */
        unsigned int ndx = 0;
        cson_kvp * kvp = cson_object_search_impl( obj, key, &ndx );
        cson_value * rc = NULL;
        if( ! kvp )
        {
            return NULL;
        }
        assert( obj->kvp.count > 0 );
        assert( obj->kvp.list[ndx] == kvp );
        rc = kvp->value;
        assert( rc );
        kvp->value = NULL;
        cson_kvp_free( kvp );
        assert( rc->refcount > 0 );
        --rc->refcount;
        obj->kvp.list[ndx] = NULL;
        { /* if my brain were bigger i'd use memmove(). */
            unsigned int i = ndx;
            for( ; i < obj->kvp.count; ++i )
            {
                obj->kvp.list[i] =
                    (i < (obj->kvp.alloced-1))
                    ? obj->kvp.list[i+1]
                    : NULL;
            }
        }
        obj->kvp.list[--obj->kvp.count] = NULL;
#if CSON_OBJECT_PROPS_SORT
        cson_object_sort_props( obj );
#endif
        return rc;
    }
}
/** @internal

   If p->node is-a Object then value is inserted into the object
   using p->key. In any other case cson_rc.InternalError is returned.

   Returns cson_rc.AllocError if an allocation fails.

   Returns 0 on success. On error, parsing must be ceased immediately.
   
   Ownership of val is ALWAYS TRANSFERED to this function. If this
   function fails, val will be cleaned up and destroyed. (This
   simplifies error handling in the core parser.)
*/
static int cson_parser_set_key( cson_parser * p, cson_value * val )
{
    assert( p && val );

    if( p->ckey && cson_value_is_object(p->node) )
    {
        int rc;
        cson_object * obj = cson_value_get_object(p->node);
        cson_kvp * kvp = NULL;
        assert( obj && (p->node->value == obj) );
        /**
           FIXME? Use cson_object_set() instead of our custom
           finagling with the object? We do it this way to avoid an
           extra alloc/strcpy of the key data.
        */
        if( !obj->kvp.alloced || (obj->kvp.count == obj->kvp.alloced-1))
        {
            if( obj->kvp.alloced > cson_kvp_list_reserve( &obj->kvp, obj->kvp.count ? (obj->kvp.count*2) : 5 ) )
            {
                cson_value_free(val);
                return cson_rc.AllocError;
            }
        }
        kvp = cson_kvp_alloc();
        if( ! kvp )
        {
            cson_value_free(val);
            return cson_rc.AllocError;
        }
        kvp->key = p->ckey/*transfer ownership*/;
        p->ckey = NULL;
        kvp->value = val;
        cson_refcount_incr( val );
        rc = cson_kvp_list_append( &obj->kvp, kvp );
        if( 0 != rc )
        {
            cson_kvp_free( kvp );
        }
        else
        {
            ++p->totalValueCount;
        }
        return rc;
    }
    else
    {
        if(val) cson_value_free(val);
        return p->errNo = cson_rc.InternalError;
    }

}

/** @internal

    Pushes val into the current object/array parent node, depending on the
    internal state of the parser.

    Ownership of val is always transfered to this function, regardless of
    success or failure.

    Returns 0 on success. On error, parsing must be ceased immediately.
*/
static int cson_parser_push_value( cson_parser * p, cson_value * val )
{
    if( p->ckey )
    { /* we're in Object mode */
        assert( cson_value_is_object( p->node ) );
        return cson_parser_set_key( p, val );
    }
    else if( cson_value_is_array( p->node ) )
    { /* we're in Array mode */
        cson_array * ar = cson_value_get_array( p->node );
        int rc;
        assert( ar && (ar == p->node->value) );
        rc = cson_array_append( ar, val );
        if( 0 != rc )
        {
            cson_value_free(val);
        }
        else
        {
            ++p->totalValueCount;
        }
        return rc;
    }
    else
    { /* WTF? */
        assert( 0 && "Internal error in cson_parser code" );
        return p->errNo = cson_rc.InternalError;
    }
}

/**
   Callback for JSON_parser API. Reminder: it returns 0 (meaning false)
   on error!
*/
static int cson_parse_callback( void * cx, int type, JSON_value const * value )
{
    cson_parser * p = (cson_parser *)cx;
    int rc = 0;
#define ALLOC_V(T,V) cson_value * v = cson_value_new_##T(V); if( ! v ) { rc = cson_rc.AllocError; break; }
    switch(type) {
      case JSON_T_ARRAY_BEGIN:
      case JSON_T_OBJECT_BEGIN: {
          cson_value * obja = (JSON_T_ARRAY_BEGIN == type)
              ? cson_value_new_array()
              : cson_value_new_object();
          if( ! obja )
          {
              p->errNo = cson_rc.AllocError;
              return 0;
          }
          if( 0 != rc ) break;
          if( ! p->root )
          {
              p->root = p->node = obja;
              rc = cson_array_append( &p->stack, obja );
              if( 0 != rc )
              { /* work around a (potential) corner case in the cleanup code. */
                  cson_value_free( p->root );
                  p->root = NULL;
              }
              else
              {
                  cson_refcount_incr( p->root )
                      /* simplifies cleanup later on. */
                      ;
                  ++p->totalValueCount;
              }
          }
          else
          {
              rc = cson_array_append( &p->stack, obja );
              if( 0 == rc ) rc = cson_parser_push_value( p, obja );
              if( 0 == rc ) p->node = obja;
          }
          break;
      }
      case JSON_T_ARRAY_END:
      case JSON_T_OBJECT_END: {
          if( 0 == p->stack.list.count )
          {
              rc = cson_rc.RangeError;
              break;
          }
#if CSON_OBJECT_PROPS_SORT
          if( cson_value_is_object(p->node) )
          {/* kludge: the parser uses custom cson_object property
              insertion as a malloc/strcpy-reduction optimization.
              Because of that, we have to sort the property list
              ourselves...
           */
              cson_object * obj = cson_value_get_object(p->node);
              assert( NULL != obj );
              cson_object_sort_props( obj );
          }
#endif

#if 1
          /* Reminder: do not use cson_array_pop_back( &p->stack )
             because that will clean up the object, and we don't want
             that.  We just want to forget this reference
             to it. The object is either the root or was pushed into
             an object/array in the parse tree (and is owned by that
             object/array).
          */
          --p->stack.list.count;
          assert( p->node == p->stack.list.list[p->stack.list.count] );
          cson_refcount_decr( p->node )
              /* p->node might be owned by an outer object but we
                 need to remove the list's reference. For the
                 root node we manually add a reference to
                 avoid a special case here. Thus when we close
                 the root node, its refcount is still 1.
              */;
          p->stack.list.list[p->stack.list.count] = NULL;
          if( p->stack.list.count )
          {
              p->node = p->stack.list.list[p->stack.list.count-1];
          }
          else
          {
              p->node = p->root;
          }
#else
          /*
             Causing a leak?
           */
          cson_array_pop_back( &p->stack, 1 );
          if( p->stack.list.count )
          {
              p->node = p->stack.list.list[p->stack.list.count-1];
          }
          else
          {
              p->node = p->root;
          }
          assert( p->node && (1==p->node->refcount) );
#endif
          break;
      }
      case JSON_T_INTEGER: {
          ALLOC_V(integer, value->vu.integer_value );
          rc = cson_parser_push_value( p, v );
          break;
      }
      case JSON_T_FLOAT: {
          ALLOC_V(double, value->vu.float_value );
          rc =  cson_parser_push_value( p, v );
          break;
      }
      case JSON_T_NULL: {
          rc = cson_parser_push_value( p, cson_value_null() );
          break;
      }
      case JSON_T_TRUE: {
          rc = cson_parser_push_value( p, cson_value_true() );
          break;
      }
      case JSON_T_FALSE: {
          rc = cson_parser_push_value( p, cson_value_false() );
          break;
      }
      case JSON_T_KEY: {
          assert(!p->ckey);
          p->ckey = cson_string_strdup( value->vu.str.value, value->vu.str.length );
          if( ! p->ckey )
          {
              rc = cson_rc.AllocError;
              break;
          }
          ++p->totalKeyCount;
          break;
      }
      case JSON_T_STRING: {
          cson_value * v = cson_value_new_string( value->vu.str.value, value->vu.str.length );
          rc = ( NULL == v ) 
            ? cson_rc.AllocError
            : cson_parser_push_value( p, v );
          break;
      }
      default:
          assert(0);
          rc = cson_rc.InternalError;
          break;
    }
#undef ALLOC_V
    return ((p->errNo = rc)) ? 0 : 1;
}


/**
   Converts a JSON_error code to one of the cson_rc values.
*/
static int cson_json_err_to_rc( JSON_error jrc )
{
    switch(jrc)
    {
      case JSON_E_NONE: return 0;
      case JSON_E_INVALID_CHAR: return cson_rc.Parse_INVALID_CHAR;
      case JSON_E_INVALID_KEYWORD: return cson_rc.Parse_INVALID_KEYWORD;
      case JSON_E_INVALID_ESCAPE_SEQUENCE: return cson_rc.Parse_INVALID_ESCAPE_SEQUENCE;
      case JSON_E_INVALID_UNICODE_SEQUENCE: return cson_rc.Parse_INVALID_UNICODE_SEQUENCE;
      case JSON_E_INVALID_NUMBER: return cson_rc.Parse_INVALID_NUMBER;
      case JSON_E_NESTING_DEPTH_REACHED: return cson_rc.Parse_NESTING_DEPTH_REACHED;
      case JSON_E_UNBALANCED_COLLECTION: return cson_rc.Parse_UNBALANCED_COLLECTION;
      case JSON_E_EXPECTED_KEY: return cson_rc.Parse_EXPECTED_KEY;
      case JSON_E_EXPECTED_COLON: return cson_rc.Parse_EXPECTED_COLON;
      case JSON_E_OUT_OF_MEMORY: return cson_rc.AllocError;
      default:
          return cson_rc.InternalError;
    }
}

/** @internal

   Cleans up all contents of p but does not free p.

   To properly take over ownership of the parser's root node on a
   successful parse:

   - Copy p->root's pointer and set p->root to NULL.
   - Eventually free up p->root with cson_value_free().
   
   If you do not set p->root to NULL, p->root will be freed along with
   any other items inserted into it (or under it) during the parsing
   process.
*/
static int cson_parser_clean( cson_parser * p )
{
    if( ! p ) return cson_rc.ArgError;
    else
    {
        if( p->p )
        {
            delete_JSON_parser(p->p);
            p->p = NULL;
        }
        cson_free( p->ckey, "cson_parser::key" );
        cson_array_clean( &p->stack, 1 );
        if( p->root )
        {
            cson_value_free( p->root );
        }
        *p = cson_parser_empty;
        return 0;
    }
}


int cson_parse( cson_value ** tgt, cson_data_source_f src, void * state,
                cson_parse_opt const * opt_, cson_parse_info * info_ )
{
    unsigned char ch[2] = {0,0};
    cson_parse_opt const opt = opt_ ? *opt_ : cson_parse_opt_empty;
    int rc = 0;
    unsigned int len = 1;
    cson_parse_info info = info_ ? *info_ : cson_parse_info_empty;
    cson_parser p = cson_parser_empty;
    if( ! tgt || ! src ) return cson_rc.ArgError;
    
    {
        JSON_config jopt = {0};
        init_JSON_config( &jopt );
        jopt.allow_comments = opt.allowComments;
        jopt.depth = opt.maxDepth;
        jopt.callback_ctx = &p;
        jopt.handle_floats_manually = 0;
        jopt.callback = cson_parse_callback;
        p.p = new_JSON_parser(&jopt);
        if( ! p.p )
        {
            return cson_rc.AllocError;
        }
    }

    do
    { /* FIXME: buffer the input in multi-kb chunks. */
        len = 1;
        ch[0] = 0;
        rc = src( state, ch, &len );
        if( 0 != rc ) break;
        else if( !len /* EOF */ ) break;
        ++info.length;
        if('\n' == ch[0])
        {
            ++info.line;
            info.col = 0;
        }
        if( ! JSON_parser_char(p.p, ch[0]) )
        {
            rc = cson_json_err_to_rc( JSON_parser_get_last_error(p.p) );
            if(0==rc) rc = p.errNo;
            if(0==rc) rc = cson_rc.InternalError;
            info.errorCode = rc;
            break;
        }
        if( '\n' != ch[0]) ++info.col;
    } while(1);
    if( info_ )
    {
        info.totalKeyCount = p.totalKeyCount;
        info.totalValueCount = p.totalValueCount;
        *info_ = info;
    }
    if( 0 != rc )
    {
        cson_parser_clean(&p);
        return rc;
    }
    if( ! JSON_parser_done(p.p) )
    {
        rc = cson_json_err_to_rc( JSON_parser_get_last_error(p.p) );
        cson_parser_clean(&p);
        if(0==rc) rc = p.errNo;
        if(0==rc) rc = cson_rc.InternalError;
    }
    else
    {
        cson_value * root = p.root;
        p.root = NULL;
        cson_parser_clean(&p);
        if( root )
        {
            assert( (1 == root->refcount) && "Detected memory mismanagement in the parser." );
            root->refcount = 0
                /* HUGE KLUDGE! Avoids having one too many references
                   in some client code, leading to a leak. Here we're
                   accommodating a memory management workaround in the
                   parser code which manually adds a reference to the
                   root node to keep it from being cleaned up
                   prematurely.
                */;
            *tgt = root;
        }
        else
        { /* then can happen on empty input. */
            rc = cson_rc.UnknownError;
        }
    }
    return rc;
}

/**
   The UTF code was originally taken from sqlite3's public-domain
   source code (http://sqlite.org), modified only slightly for use
   here. This code generates some "possible data loss" warnings on
   MSVC, but if this code is good enough for sqlite3 then it's damned
   well good enough for me, so we disable that warning for Windows
   builds.
*/

/*
** This lookup table is used to help decode the first byte of
** a multi-byte UTF8 character.
*/
static const unsigned char cson_utfTrans1[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00
};


/*
** Translate a single UTF-8 character.  Return the unicode value.
**
** During translation, assume that the byte that zTerm points
** is a 0x00.
**
** Write a pointer to the next unread byte back into *pzNext.
**
** Notes On Invalid UTF-8:
**
**  *  This routine never allows a 7-bit character (0x00 through 0x7f) to
**     be encoded as a multi-byte character.  Any multi-byte character that
**     attempts to encode a value between 0x00 and 0x7f is rendered as 0xfffd.
**
**  *  This routine never allows a UTF16 surrogate value to be encoded.
**     If a multi-byte character attempts to encode a value between
**     0xd800 and 0xe000 then it is rendered as 0xfffd.
**
**  *  Bytes in the range of 0x80 through 0xbf which occur as the first
**     byte of a character are interpreted as single-byte characters
**     and rendered as themselves even though they are technically
**     invalid characters.
**
**  *  This routine accepts an infinite number of different UTF8 encodings
**     for unicode values 0x80 and greater.  It do not change over-length
**     encodings to 0xfffd as some systems recommend.
*/
#define READ_UTF8(zIn, zTerm, c)                           \
  c = *(zIn++);                                            \
  if( c>=0xc0 ){                                           \
    c = cson_utfTrans1[c-0xc0];                          \
    while( zIn!=zTerm && (*zIn & 0xc0)==0x80 ){            \
      c = (c<<6) + (0x3f & *(zIn++));                      \
    }                                                      \
    if( c<0x80                                             \
        || (c&0xFFFFF800)==0xD800                          \
        || (c&0xFFFFFFFE)==0xFFFE ){  c = 0xFFFD; }        \
  }
static int cson_utf8Read(
  const unsigned char *z,         /* First byte of UTF-8 character */
  const unsigned char *zTerm,     /* Pretend this byte is 0x00 */
  const unsigned char **pzNext    /* Write first byte past UTF-8 char here */
){
  int c;
  READ_UTF8(z, zTerm, c);
  *pzNext = z;
  return c;
}
#undef READ_UTF8

#if defined(_WIN32)
#  pragma warning( pop )
#endif

unsigned int cson_string_length_utf8( cson_string const * str )
{
    if( ! str ) return 0;
    else
    {
        char unsigned const * pos = (char unsigned const *)cson_string_cstr(str);
        char unsigned const * end = pos + str->length;
        unsigned int rc = 0;
        for( ; (pos < end) && cson_utf8Read(pos, end, &pos);
            ++rc )
        {
        };
        return rc;
    }
}

/**
   Escapes the first len bytes of the given string as JSON and sends
   it to the given output function (which will be called often - once
   for each logical character). The output is also surrounded by
   double-quotes.

   A NULL str will be escaped as an empty string, though we should
   arguably export it as "null" (without quotes). We do this because
   in JavaScript (typeof null === "object"), and by outputing null
   here we would effectively change the data type from string to
   object.
*/
static int cson_str_to_json( char const * str, unsigned int len,
                             char escapeFwdSlash,
                             cson_data_dest_f f, void * state )
{
    if( NULL == f ) return cson_rc.ArgError;
    else if( !str || !*str || (0 == len) )
    { /* special case for 0-length strings. */
        return f( state, "\"\"", 2 );
    }
    else
    {
        unsigned char const * pos = (unsigned char const *)str;
        unsigned char const * end = (unsigned char const *)(str ? (str + len) : NULL);
        unsigned char const * next = NULL;
        int ch;
        unsigned char clen = 0;
        char escChar[3] = {'\\',0,0};
        enum { UBLen = 8 };
        char ubuf[UBLen];
        int rc = 0;
        rc = f(state, "\"", 1 );
        for( ; (pos < end) && (0 == rc); pos += clen )
        {
            ch = cson_utf8Read(pos, end, &next);
            if( 0 == ch ) break;
            assert( next > pos );
            clen = next - pos;
            assert( clen );
            if( 1 == clen )
            { /* ASCII */
                assert( *pos == ch );
                escChar[1] = 0;
                switch(ch)
                {
                  case '\t': escChar[1] = 't'; break;
                  case '\r': escChar[1] = 'r'; break;
                  case '\n': escChar[1] = 'n'; break;
                  case '\f': escChar[1] = 'f'; break;
                  case '\b': escChar[1] = 'b'; break;
                  case '/':
      /*
        Regarding escaping of forward-slashes. See the main exchange below...

        --------------
        From: Douglas Crockford <douglas@crockford.com>
        To: Stephan Beal <sgbeal@googlemail.com>
        Subject: Re: Is escaping of forward slashes required?

        It is allowed, not required. It is allowed so that JSON can be safely
        embedded in HTML, which can freak out when seeing strings containing
        "</". JSON tolerates "<\/" for this reason.

        On 4/8/2011 2:09 PM, Stephan Beal wrote:
        > Hello, Jsonites,
        >
        > i'm a bit confused on a small grammatic detail of JSON:
        >
        > if i'm reading the grammar chart on http://www.json.org/ correctly,
        > forward slashes (/) are supposed to be escaped in JSON. However, the
        > JSON class provided with my browsers (Chrome and FF, both of which i
        > assume are fairly standards/RFC-compliant) do not escape such characters.
        >
        > Is backslash-escaping forward slashes required? If so, what is the
        > justification for it? (i ask because i find it unnecessary and hard to
        > look at.)
        --------------
      */
                      if( escapeFwdSlash ) escChar[1] = '/';
                      break;
                  case '\\': escChar[1] = '\\'; break;
                  case '"': escChar[1] = '"'; break;
                  default: break;
                }
                if( escChar[1])
                {
                    rc = f(state, escChar, 2);
                }
                else
                {
                    rc = f(state, (char const *)pos, clen);
                }
                continue;
            }
            else
            { /* UTF: transform it to \uXXXX */
                memset(ubuf,0,UBLen);
                rc = sprintf(ubuf, "\\u%04x",ch);
                if( rc != 6 )
                {
                    rc = cson_rc.RangeError;
                    break;
                }
                rc = f( state, ubuf, 6 );
                continue;
            }
        }
        if( 0 == rc )
        {
            rc = f(state, "\"", 1 );
        }
        return rc;
    }
}

int cson_object_iter_init( cson_object const * obj, cson_object_iterator * iter )
{
    if( ! obj || !iter ) return cson_rc.ArgError;
    else
    {
        iter->obj = obj;
        iter->pos = 0;
        return 0;
    }
}

cson_kvp * cson_object_iter_next( cson_object_iterator * iter )
{
    if( ! iter || !iter->obj ) return NULL;
    else if( iter->pos >= iter->obj->kvp.count ) return NULL;
    else
    {
        cson_kvp * rc = iter->obj->kvp.list[iter->pos++];
        while( (NULL==rc) && (iter->pos < iter->obj->kvp.count))
        {
            rc = iter->obj->kvp.list[iter->pos++];
        }
        return rc;
    }
}

static int cson_output_null( cson_data_dest_f f, void * state )
{
    if( !f ) return cson_rc.ArgError;
    else
    {
        return f(state, "null", 4);
    }
}

static int cson_output_bool( cson_value const * src, cson_data_dest_f f, void * state )
{
    if( !f ) return cson_rc.ArgError;
    else
    {
        char const v = cson_value_get_bool(src);
        return f(state, v ? "true" : "false", v ? 4 : 5);
    }
}

static int cson_output_integer( cson_value const * src, cson_data_dest_f f, void * state )
{
    if( !f ) return cson_rc.ArgError;
    else if( !cson_value_is_integer(src) ) return cson_rc.TypeError;
    else
    {
        enum { BufLen = 100 };
        char b[BufLen];
        int rc;
        memset( b, 0, BufLen );
        rc = sprintf( b, "%"CSON_INT_T_PFMT, cson_value_get_integer(src) )
            /* Reminder: snprintf() is C99 */
            ;
        return ( rc<=0 )
            ? cson_rc.RangeError
            : f( state, b, (unsigned int)rc )
            ;
    }
}

static int cson_output_double( cson_value const * src, cson_data_dest_f f, void * state )
{
    if( !f ) return cson_rc.ArgError;
    else if( !cson_value_is_double(src) ) return cson_rc.TypeError;
    else
    {
        enum { BufLen = 128 /* this must be relatively large or huge
                               doubles can cause us to overrun here,
                               resulting in stack-smashing errors.
                            */};
        char b[BufLen];
        int rc;
        memset( b, 0, BufLen );
        rc = sprintf( b, "%"CSON_DOUBLE_T_PFMT, cson_value_get_double(src) )
            /* Reminder: snprintf() is C99 */
            ;
        if( rc<=0 ) return cson_rc.RangeError;
        else if(1)
        { /* Strip trailing zeroes before passing it on... */
            unsigned int urc = (unsigned int)rc;
            char * pos = b + urc - 1;
            for( ; ('0' == *pos) && urc && (*(pos-1) != '.'); --pos, --urc )
            {
                *pos = 0;
            }
            assert(urc && *pos);
            return f( state, b, urc );
        }
        else
        {
            unsigned int urc = (unsigned int)rc;
            return f( state, b, urc );
        }
        return 0;
    }
}

static int cson_output_string( cson_value const * src, char escapeFwdSlash, cson_data_dest_f f, void * state )
{
    if( !f ) return cson_rc.ArgError;
    else if( ! cson_value_is_string(src) ) return cson_rc.TypeError;
    else
    {
        cson_string const * str = cson_value_get_string(src);
        assert( NULL != str );
        return cson_str_to_json(cson_string_cstr(str), str->length, escapeFwdSlash, f, state);
    }
}


/**
   Outputs indention spacing to f().

   blanks: (0)=no indentation, (1)=1 TAB per/level, (>1)=n spaces/level

   depth is the current depth of the output tree, and determines how much
   indentation to generate.

   If blanks is 0 this is a no-op. Returns non-0 on error, and the
   error code will always come from f().
*/
static int cson_output_indent( cson_data_dest_f f, void * state,
                               unsigned char blanks, unsigned int depth )
{
    if( 0 == blanks ) return 0;
    else
    {
#if 0
        /* FIXME: stuff the indention into the buffer and make a single
           call to f().
        */
        enum { BufLen = 200 };
        char buf[BufLen];
#endif
        unsigned int i;
        unsigned int x;
        char const ch = (1==blanks) ? '\t' : ' ';
        int rc = f(state, "\n", 1 );
        for( i = 0; (i < depth) && (0 == rc); ++i )
        {
            for( x = 0; (x < blanks) && (0 == rc); ++x )
            {
                rc = f(state, &ch, 1);
            }
        }
        return rc;
    }
}

static int cson_output_array( cson_value const * src, cson_data_dest_f f, void * state,
                              cson_output_opt const * fmt, unsigned int level );
static int cson_output_object( cson_value const * src, cson_data_dest_f f, void * state,
                               cson_output_opt const * fmt, unsigned int level );
/**
   Main cson_output() implementation. Dispatches to a different impl depending
   on src->api->typeID.

   Returns 0 on success.
*/
static int cson_output_impl( cson_value const * src, cson_data_dest_f f, void * state,
                             cson_output_opt const * fmt, unsigned int level )
{
    if( ! src || !f || !src->api ) return cson_rc.ArgError;
    else
    {
        int rc = 0;
        assert(fmt);
        switch( src->api->typeID )
        {
          case CSON_TYPE_UNDEF:
          case CSON_TYPE_NULL:
              rc = cson_output_null(f, state);
              break;
          case CSON_TYPE_BOOL:
              rc = cson_output_bool(src, f, state);
              break;
          case CSON_TYPE_INTEGER:
              rc = cson_output_integer(src, f, state);
              break;
          case CSON_TYPE_DOUBLE:
              rc = cson_output_double(src, f, state);
              break;
          case CSON_TYPE_STRING:
              rc = cson_output_string(src, fmt->escapeForwardSlashes, f, state);
              break;
          case CSON_TYPE_ARRAY:
              rc = cson_output_array( src, f, state, fmt, level );
              break;
          case CSON_TYPE_OBJECT:
              rc = cson_output_object( src, f, state, fmt, level );
              break;
          default:
              rc = cson_rc.TypeError;
              break;
        }
        return rc;
    }
}


static int cson_output_array( cson_value const * src, cson_data_dest_f f, void * state,
                              cson_output_opt const * fmt, unsigned int level )
{
    if( !src || !f || !fmt ) return cson_rc.ArgError;
    else if( ! cson_value_is_array(src) ) return cson_rc.TypeError;
    else if( level > fmt->maxDepth ) return cson_rc.RangeError;
    else
    {
        int rc;
        unsigned int i;
        cson_value const * v;
        char doIndent = fmt->indentation ? 1 : 0;
        cson_array const * ar = cson_value_get_array(src);
        assert( NULL != ar );
        if( 0 == ar->list.count )
        {
            return f(state, "[]", 2 );
        }
        else if( (1 == ar->list.count) && !fmt->indentSingleMemberValues ) doIndent = 0;
        rc = f(state, "[", 1);
        ++level;
        if( doIndent )
        {
            rc = cson_output_indent( f, state, fmt->indentation, level );
        }
        for( i = 0; (i < ar->list.count) && (0 == rc); ++i )
        {
            v = ar->list.list[i];
            if( v )
            {
                rc = cson_output_impl( v, f, state, fmt, level );
            }
            else
            {
                rc = cson_output_null( f, state );
            }
            if( 0 == rc )
            {
                if(i < (ar->list.count-1))
                {
                    rc = f(state, ",", 1);
                    if( 0 == rc )
                    {
                        rc = doIndent
                            ? cson_output_indent( f, state, fmt->indentation, level )
                            : f( state, " ", 1 );
                    }
                }
            }
        }
        --level;
        if( doIndent && (0 == rc) )
        {
            rc = cson_output_indent( f, state, fmt->indentation, level );
        }
        return (0 == rc)
            ? f(state, "]", 1)
            : rc;
    }
}

static int cson_output_object( cson_value const * src, cson_data_dest_f f, void * state,
                               cson_output_opt const * fmt, unsigned int level )
{
    if( !src || !f || !fmt ) return cson_rc.ArgError;
    else if( ! cson_value_is_object(src) ) return cson_rc.TypeError;
    else if( level > fmt->maxDepth ) return cson_rc.RangeError;
    else
    {
        int rc;
        unsigned int i;
        cson_kvp const * kvp;
        char doIndent = fmt->indentation ? 1 : 0;
        cson_object const * obj = cson_value_get_object(src);
        assert( (NULL != obj) && (NULL != fmt));
        if( 0 == obj->kvp.count )
        {
            return f(state, "{}", 2 );
        }
        else if( (1 == obj->kvp.count) && !fmt->indentSingleMemberValues ) doIndent = 0;
        rc = f(state, "{", 1);
        ++level;
        if( doIndent )
        {
            rc = cson_output_indent( f, state, fmt->indentation, level );
        }
        for( i = 0; (i < obj->kvp.count) && (0 == rc); ++i )
        {
            kvp = obj->kvp.list[i];
            if( kvp && kvp->key )
            {
                rc = cson_str_to_json(cson_string_cstr(kvp->key), kvp->key->length,
                                      fmt->escapeForwardSlashes, f, state);
                if( 0 == rc )
                {
                    rc = fmt->addSpaceAfterColon
                        ? f(state, ": ", 2 )
                        : f(state, ":", 1 )
                        ;
                }
                if( 0 == rc)
                {
                    rc = ( kvp->value )
                        ? cson_output_impl( kvp->value, f, state, fmt, level )
                        : cson_output_null( f, state );
                }
            }
            else
            {
                assert( 0 && "Possible internal error." );
                continue /* internal error? */;
            }
            if( 0 == rc )
            {
                if(i < (obj->kvp.count-1))
                {
                    rc = f(state, ",", 1);
                    if( 0 == rc )
                    {
                        rc = doIndent
                            ? cson_output_indent( f, state, fmt->indentation, level )
                            : f( state, " ", 1 );
                    }
                }
            }
        }
        --level;
        if( doIndent && (0 == rc) )
        {
            rc = cson_output_indent( f, state, fmt->indentation, level );
        }
        return (0 == rc)
            ? f(state, "}", 1)
            : rc;
    }
}

int cson_output( cson_value const * src, cson_data_dest_f f,
                 void * state, cson_output_opt const * fmt )
{
    int rc;
    if(! fmt ) fmt = &cson_output_opt_empty;
    rc = cson_output_impl(src, f, state, fmt, 0 );
    if( (0 == rc) && fmt->addNewline )
    {
        rc = f(state, "\n", 1);
    }
    return rc;
}

int cson_data_dest_FILE( void * state, void const * src, unsigned int n )
{
    if( ! state ) return cson_rc.ArgError;
    else if( !src || !n ) return 0;
    else
    {
        return ( 1 == fwrite( src, n, 1, (FILE*) state ) )
            ? 0
            : cson_rc.IOError;
    }
}

int cson_output_FILE( cson_value const * src, FILE * dest, cson_output_opt const * fmt )
{
    int rc = 0;
    if( fmt )
    {
        rc = cson_output( src, cson_data_dest_FILE, dest, fmt );
    }
    else
    {
        /* We normally want a newline on FILE output. */
        cson_output_opt opt = cson_output_opt_empty;
        opt.addNewline = 1;
        rc = cson_output( src, cson_data_dest_FILE, dest, &opt );
    }
    if( 0 == rc )
    {
        fflush( dest );
    }
    return rc;
}

int cson_output_filename( cson_value const * src, char const * dest, cson_output_opt const * fmt )
{
    if( !src || !dest ) return cson_rc.ArgError;
    else
    {
        FILE * f = fopen(dest,"wb");
        if( !f ) return cson_rc.IOError;
        else
        {
            int const rc = cson_output_FILE( src, f, fmt );
            fclose(f);
            return rc;
        }
    }
}

int cson_parse_filename( cson_value ** tgt, char const * src,
                         cson_parse_opt const * opt, cson_parse_info * err )
{
    if( !src || !tgt ) return cson_rc.ArgError;
    else
    {
        FILE * f = fopen(src, "r");
        if( !f ) return cson_rc.IOError;
        else
        {
            int const rc = cson_parse_FILE( tgt, f, opt, err );
            fclose(f);
            return rc;
        }
    }
}

/** Internal type to hold state for a JSON input string.
 */
typedef struct cson_data_source_StringSource_
{
    /** Start of input string. */
    char const * str;
    /** Current iteration position. Must initially be == str. */
    char const * pos;
    /** Logical EOF, one-past-the-end of str. */
    char const * end;
}  cson_data_source_StringSource_t;

/**
   A cson_data_source_f() implementation which requires the state argument
   to be a properly populated (cson_data_source_StringSource_t*).
*/
static int cson_data_source_StringSource( void * state, void * dest, unsigned int * n )
{
    if( !state || !n || !dest ) return cson_rc.ArgError;
    else if( !*n ) return 0 /* ignore this */;
    else
    {
        unsigned int i;
        cson_data_source_StringSource_t * ss = (cson_data_source_StringSource_t*) state;
        unsigned char * tgt = (unsigned char *)dest;
        for( i = 0; (i < *n) && (ss->pos < ss->end); ++i, ++ss->pos, ++tgt )
        {
            *tgt = *ss->pos;
        }
        *n = i;
        return 0;
    }
}

int cson_parse_string( cson_value ** tgt, char const * src, unsigned int len,
                       cson_parse_opt const * opt, cson_parse_info * err )
{
    if( ! tgt || !src ) return cson_rc.ArgError;
    else if( !*src || (len<2/*2==len of {} and []*/) ) return cson_rc.RangeError;
    else
    {
        cson_data_source_StringSource_t ss;
        ss.str = ss.pos = src;
        ss.end = src + len;
        return cson_parse( tgt, cson_data_source_StringSource, &ss, opt, err );
    }

}

int cson_parse_buffer( cson_value ** tgt,
                       cson_buffer const * buf,
                       cson_parse_opt const * opt,
                       cson_parse_info * err )
{
    return ( !tgt || !buf || !buf->mem || !buf->used )
        ? cson_rc.ArgError
        : cson_parse_string( tgt, (char const *)buf->mem,
                             buf->used, opt, err );
}

int cson_buffer_reserve( cson_buffer * buf, cson_size_t n )
{
    if( ! buf ) return cson_rc.ArgError;
    else if( 0 == n )
    {
        cson_free(buf->mem, "cson_buffer::mem");
        *buf = cson_buffer_empty;
        return 0;
    }
    else if( buf->capacity >= n )
    {
        return 0;
    }
    else
    {
        unsigned char * x = (unsigned char *)realloc( buf->mem, n );
        if( ! x ) return cson_rc.AllocError;
        memset( x + buf->used, 0, n - buf->used );
        buf->mem = x;
        buf->capacity = n;
        ++buf->timesExpanded;
        return 0;
    }
}

cson_size_t cson_buffer_fill( cson_buffer * buf, char c )
{
    if( !buf || !buf->capacity || !buf->mem ) return 0;
    else
    {
        memset( buf->mem, c, buf->capacity );
        return buf->capacity;
    }
}

/**
   cson_data_dest_f() implementation, used by cson_output_buffer().

   arg MUST be a (cson_buffer*). This function appends n bytes at
   position arg->used, expanding the buffer as necessary.
*/
static int cson_data_dest_cson_buffer( void * arg, void const * data_, unsigned int n )
{
    if( ! arg || (n<0) ) return cson_rc.ArgError;
    else if( ! n ) return 0;
    else
    {
        cson_buffer * sb = (cson_buffer*)arg;
        char const * data = (char const *)data_;
        cson_size_t npos = sb->used + n;
        unsigned int i;
        if( npos >= sb->capacity )
        {
            const cson_size_t oldCap = sb->capacity;
            const cson_size_t asz = npos * 2;
            if( asz < npos ) return cson_rc.ArgError; /* overflow */
            else if( 0 != cson_buffer_reserve( sb, asz ) ) return cson_rc.AllocError;
            assert( (sb->capacity > oldCap) && "Internal error in memory buffer management!" );
            /* make sure it gets NULL terminated. */
            memset( sb->mem + oldCap, 0, (sb->capacity - oldCap) );
        }
        for( i = 0; i < n; ++i, ++sb->used )
        {
            sb->mem[sb->used] = data[i];
        }
        return 0;
    }
}


int cson_output_buffer( cson_value const * v, cson_buffer * buf,
                        cson_output_opt const * opt )
{
    int rc = cson_output( v, cson_data_dest_cson_buffer, buf, opt );
    if( 0 == rc )
    { /* Ensure that the buffer is null-terminated. */
        rc = cson_buffer_reserve( buf, buf->used + 1 );
        if( 0 == rc )
        {
            buf->mem[buf->used] = 0;
        }
    }
    return rc;
}

/** @internal

Tokenizes an input string on a given separator. Inputs are:

- (inp) = is a pointer to the pointer to the start of the input.

- (separator) = the separator character

- (end) = a pointer to NULL. i.e. (*end == NULL)

This function scans *inp for the given separator char or a NULL char.
Successive separators at the start of *inp are skipped. The effect is
that, when this function is called in a loop, all neighboring
separators are ignored. e.g. the string "aa.bb...cc" will tokenize to
the list (aa,bb,cc) if the separator is '.' and to (aa.,...cc) if the
separator is 'b'.

Returns 0 (false) if it finds no token, else non-0 (true).

Output:

- (*inp) will be set to the first character of the next token.

- (*end) will point to the one-past-the-end point of the token.

If (*inp == *end) then the end of the string has been reached
without finding a token.

Post-conditions:

- (*end == *inp) if no token is found.

- (*end > *inp) if a token is found.

It is intolerant of NULL values for (inp, end), and will assert() in
debug builds if passed NULL as either parameter.
*/
static char cson_next_token( char const ** inp, char separator, char const ** end )
{
    char const * pos = NULL;
    assert( inp && end && *inp );
    if( *inp == *end ) return 0;
    pos = *inp;
    if( !*pos )
    {
        *end = pos;
        return 0;
    }
    for( ; *pos && (*pos == separator); ++pos) { /* skip preceeding splitters */ }
    *inp = pos;
    for( ; *pos && (*pos != separator); ++pos) { /* find next splitter */ }
    *end = pos;
    return (pos > *inp) ? 1 : 0;
}

int cson_object_fetch_sub( cson_object const * obj, cson_value ** tgt, char const * path, char sep )
{
    if( ! obj || !path ) return cson_rc.ArgError;
    else if( !*path || !sep ) return cson_rc.RangeError;
    else
    {
        char const * beg = path;
        char const * end = NULL;
        int rc;
        unsigned int i, len;
        unsigned int tokenCount = 0;
        cson_value * cv = NULL;
        cson_object const * curObj = obj;
        enum { BufSize = 128 };
        char buf[BufSize];
        memset( buf, 0, BufSize );
        rc = cson_rc.RangeError;

        while( cson_next_token( &beg, sep, &end ) )
        {
            if( beg == end ) break;
            else
            {
                ++tokenCount;
                beg = end;
                end = NULL;
            }
        }
        if( 0 == tokenCount ) return cson_rc.RangeError;
        beg = path;
        end = NULL;
        for( i = 0; i < tokenCount; ++i, beg=end, end=NULL )
        {
            rc = cson_next_token( &beg, sep, &end );
            assert( 1 == rc );
            assert( beg != end );
            assert( end > beg );
            len = end - beg;
            if( len > (BufSize-1) ) return cson_rc.RangeError;
            memset( buf, 0, len + 1 );
            memcpy( buf, beg, len );
            buf[len] = 0;
            cv = cson_object_get( curObj, buf );
            if( NULL == cv ) return cson_rc.NotFoundError;
            else if( i == (tokenCount-1) )
            {
                if(tgt) *tgt = cv;
                return 0;
            }
            else if( cson_value_is_object(cv) )
            {
                curObj = cson_value_get_object(cv);
                assert((NULL != curObj) && "Detected mis-management of internal memory!");
            }
            /* TODO: arrays. Requires numeric parsing for the index. */
            else
            {
                return cson_rc.NotFoundError;
            }
        }
        assert( i == tokenCount );
        return cson_rc.NotFoundError;
    }
}

cson_value * cson_object_get_sub( cson_object const * obj, char const * path, char sep )
{
    cson_value * v = NULL;
    cson_object_fetch_sub( obj, &v, path, sep );
    return v;
}

static cson_value * cson_value_clone_array( cson_value const * orig )
{
    unsigned int i = 0;
    cson_array const * asrc = cson_value_get_array( orig );
    unsigned int alen = cson_array_length_get( asrc );
    cson_value * destV = NULL;
    cson_array * destA = NULL;
    assert( orig && asrc );
    destV = cson_value_new_array();
    if( NULL == destV ) return NULL;
    destA = cson_value_get_array( destV );
    assert( destA );
    if( 0 != cson_array_reserve( destA, alen ) )
    {
        cson_value_free( destV );
        return NULL;
    }
    for( ; i < alen; ++i )
    {
        cson_value * ch = cson_array_get( asrc, i );
        if( NULL != ch )
        {
            cson_value * cl = cson_value_clone( ch );
            if( NULL == cl )
            {
                cson_value_free( destV );
                return NULL;
            }
            if( 0 != cson_array_set( destA, i, cl ) )
            {
                cson_value_free( cl );
                cson_value_free( destV );
                return NULL;
            }
        }
    }
    return destV;
}

static cson_value * cson_value_clone_object( cson_value const * orig )
{
    cson_object const * src = cson_value_get_object( orig );
    cson_value * destV = NULL;
    cson_object * dest = NULL;
    cson_kvp const * kvp = NULL;
    cson_object_iterator iter = cson_object_iterator_empty;
    assert( orig && src );
    if( 0 != cson_object_iter_init( src, &iter ) )
    {
        cson_value_free( destV );
        return NULL;
    }
    destV = cson_value_new_object();
    if( NULL == destV ) return NULL;
    dest = cson_value_get_object( destV );
    assert( dest );
    while( (kvp = cson_object_iter_next( &iter )) )
    {
        cson_string const * key = cson_kvp_key( kvp );
        cson_value const * val = cson_kvp_value( kvp );
        if( 0 != cson_object_set( dest,
                                  cson_string_cstr( key ),
                                  cson_value_clone( val ) ) )
        {
            cson_value_free( destV );
            return NULL;
        }
    }
    return destV;
}

cson_value * cson_value_clone( cson_value const * orig )
{
    if( NULL == orig ) return NULL;
    else
    {
        switch( orig->api->typeID )
        {
          case CSON_TYPE_UNDEF:
              return cson_value_new();
          case CSON_TYPE_NULL:
              return cson_value_null();
          case CSON_TYPE_BOOL:
              return cson_value_new_bool( cson_value_get_bool( orig ) );
          case CSON_TYPE_INTEGER:
              return cson_value_new_integer( cson_value_get_integer( orig ) );
              break;
          case CSON_TYPE_DOUBLE:
              return cson_value_new_double( cson_value_get_double( orig ) );
              break;
          case CSON_TYPE_STRING: {
              cson_string const * str = cson_value_get_string( orig );
              return cson_value_new_string( cson_string_cstr( str ),
                                            cson_string_length_bytes( str ) );
          }
          case CSON_TYPE_ARRAY:
              return cson_value_clone_array( orig );
          case CSON_TYPE_OBJECT:
              return cson_value_clone_object( orig );
        }
        assert( 0 && "We can't get this far." );
        return NULL;
    }
}

#if 0
/* i'm not happy with this... */
char * cson_pod_to_string( cson_value const * orig )
{
    if( ! orig ) return NULL;
    else
    {
        enum { BufSize = 64 };
        char * v = NULL;
        switch( orig->api->typeID )
        {
          case CSON_TYPE_BOOL: {
              char const bv = cson_value_get_bool(orig);
              v = cson_strdup( bv ? "true" : "false",
                               bv ? 4 : 5 );
              break;
          }
          case CSON_TYPE_UNDEF:
          case CSON_TYPE_NULL: {
              v = cson_strdup( "null", 4 );
              break;
          }
          case CSON_TYPE_STRING: {
              cson_string const * jstr = cson_value_get_string(orig);
              unsigned const int slen = cson_string_length_bytes( jstr );
              assert( NULL != jstr );
              v = cson_strdup( cson_string_cstr( jstr ), slen ); 
              break;
          }
          case CSON_TYPE_INTEGER: {
              char buf[BufSize] = {0};
              if( 0 < sprintf( v, "%"CSON_INT_T_PFMT, cson_value_get_integer(orig)) )
              {
                  v = cson_strdup( buf, strlen(buf) );
              }
              break;
          }
          case CSON_TYPE_DOUBLE: {
              char buf[BufSize] = {0};
              if( 0 < sprintf( v, "%"CSON_DOUBLE_T_PFMT, cson_value_get_double(orig)) )
              {
                  v = cson_strdup( buf, strlen(buf) );
              }
              break;
          }
          default:
              break;
        }
        return v;
    }
}
#endif

#if 0
/* i'm not happy with this... */
char * cson_pod_to_string( cson_value const * orig )
{
    if( ! orig ) return NULL;
    else
    {
        enum { BufSize = 64 };
        char * v = NULL;
        switch( orig->api->typeID )
        {
          case CSON_TYPE_BOOL: {
              char const bv = cson_value_get_bool(orig);
              v = cson_strdup( bv ? "true" : "false",
                               bv ? 4 : 5 );
              break;
          }
          case CSON_TYPE_UNDEF:
          case CSON_TYPE_NULL: {
              v = cson_strdup( "null", 4 );
              break;
          }
          case CSON_TYPE_STRING: {
              cson_string const * jstr = cson_value_get_string(orig);
              unsigned const int slen = cson_string_length_bytes( jstr );
              assert( NULL != jstr );
              v = cson_strdup( cson_string_cstr( jstr ), slen ); 
              break;
          }
          case CSON_TYPE_INTEGER: {
              char buf[BufSize] = {0};
              if( 0 < sprintf( v, "%"CSON_INT_T_PFMT, cson_value_get_integer(orig)) )
              {
                  v = cson_strdup( buf, strlen(buf) );
              }
              break;
          }
          case CSON_TYPE_DOUBLE: {
              char buf[BufSize] = {0};
              if( 0 < sprintf( v, "%"CSON_DOUBLE_T_PFMT, cson_value_get_double(orig)) )
              {
                  v = cson_strdup( buf, strlen(buf) );
              }
              break;
          }
          default:
              break;
        }
        return v;
    }
}
#endif

#if defined(__cplusplus)
} /*extern "C"*/
#endif

#undef MARKER
#undef CSON_OBJECT_PROPS_SORT
#undef CSON_OBJECT_PROPS_SORT_USE_LENGTH
/* end file ./cson.c */
/* begin file ./cson_lists.h */
/* Auto-generated from cson_list.h. Edit at your own risk! */
unsigned int cson_value_list_reserve( cson_value_list * self, unsigned int n )
{
    if( !self ) return 0;
    else if(0 == n)
    {
        if(0 == self->alloced) return 0;
        cson_free(self->list, "cson_value_list_reserve");
        self->list = NULL;
        self->alloced = self->count = 0;
        return 0;
    }
    else if( self->alloced >= n )
    {
        return self->alloced;
    }
    else
    {
        size_t const sz = sizeof(cson_value *) * n;
        cson_value * * m = (cson_value **)cson_realloc( self->list, sz, "cson_value_list_reserve" );
        if( ! m ) return self->alloced;

        memset( m + self->alloced, 0, (sizeof(cson_value *)*(n-self->alloced)));
        self->alloced = n;
        self->list = m;
        return n;
    }
}
int cson_value_list_append( cson_value_list * self, cson_value * cp )
{
    if( !self || !cp ) return cson_rc.ArgError;
    else if( self->alloced > cson_value_list_reserve(self, self->count+1) )
    {
        return cson_rc.AllocError;
    }
    else
    {
        self->list[self->count++] = cp;
        return 0;
    }
}
int cson_value_list_visit( cson_value_list * self,

                        int (*visitor)(cson_value * obj, void * visitorState ),



                        void * visitorState )
{
    int rc = cson_rc.ArgError;
    if( self && visitor )
    {
        unsigned int i = 0;
        for( rc = 0; (i < self->count) && (0 == rc); ++i )
        {

            cson_value * obj = self->list[i];



            if(obj) rc = visitor( obj, visitorState );
        }
    }
    return rc;
}
void cson_value_list_clean( cson_value_list * self,

                         void (*cleaner)(cson_value * obj)



                         )
{
    if( self && cleaner && self->count )
    {
        unsigned int i = 0;
        for( ; i < self->count; ++i )
        {

            cson_value * obj = self->list[i];



            if(obj) cleaner(obj);
        }
    }
    cson_value_list_reserve(self,0);
}
unsigned int cson_kvp_list_reserve( cson_kvp_list * self, unsigned int n )
{
    if( !self ) return 0;
    else if(0 == n)
    {
        if(0 == self->alloced) return 0;
        cson_free(self->list, "cson_kvp_list_reserve");
        self->list = NULL;
        self->alloced = self->count = 0;
        return 0;
    }
    else if( self->alloced >= n )
    {
        return self->alloced;
    }
    else
    {
        size_t const sz = sizeof(cson_kvp *) * n;
        cson_kvp * * m = (cson_kvp **)cson_realloc( self->list, sz, "cson_kvp_list_reserve" );
        if( ! m ) return self->alloced;

        memset( m + self->alloced, 0, (sizeof(cson_kvp *)*(n-self->alloced)));
        self->alloced = n;
        self->list = m;
        return n;
    }
}
int cson_kvp_list_append( cson_kvp_list * self, cson_kvp * cp )
{
    if( !self || !cp ) return cson_rc.ArgError;
    else if( self->alloced > cson_kvp_list_reserve(self, self->count+1) )
    {
        return cson_rc.AllocError;
    }
    else
    {
        self->list[self->count++] = cp;
        return 0;
    }
}
int cson_kvp_list_visit( cson_kvp_list * self,

                        int (*visitor)(cson_kvp * obj, void * visitorState ),



                        void * visitorState )
{
    int rc = cson_rc.ArgError;
    if( self && visitor )
    {
        unsigned int i = 0;
        for( rc = 0; (i < self->count) && (0 == rc); ++i )
        {

            cson_kvp * obj = self->list[i];



            if(obj) rc = visitor( obj, visitorState );
        }
    }
    return rc;
}
void cson_kvp_list_clean( cson_kvp_list * self,

                         void (*cleaner)(cson_kvp * obj)



                         )
{
    if( self && cleaner && self->count )
    {
        unsigned int i = 0;
        for( ; i < self->count; ++i )
        {

            cson_kvp * obj = self->list[i];



            if(obj) cleaner(obj);
        }
    }
    cson_kvp_list_reserve(self,0);
}
/* end file ./cson_lists.h */
/* begin file ./cson_sqlite3.c */
/** @file cson_sqlite3.c

This file contains the implementation code for the cson
sqlite3-to-JSON API.

License: the same as the cson core library.

Author: Stephan Beal (http://wanderinghorse.net/home/stephan)
*/
#if CSON_ENABLE_SQLITE3 /* we do this here for the sake of the amalgamation build */
#include <assert.h>
#include <string.h> /* strlen() */

#if 0
#include <stdio.h>
#define MARKER if(1) printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__); if(1) printf
#else
#define MARKER if(0) printf
#endif

#if defined(__cplusplus)
extern "C" {
#endif

cson_value * cson_sqlite3_column_to_value( sqlite3_stmt * st, int col )
{
    if( ! st ) return NULL;
    else
    {
#if 0
        sqlite3_value * val = sqlite3_column_type(st,col);
        int const vtype = val ? sqlite3_value_type(val) : -1;
        if( ! val ) return cson_value_null();
#else
        int const vtype = sqlite3_column_type(st,col);
#endif
        switch( vtype )
        {
          case SQLITE_NULL:
              return cson_value_null();
          case SQLITE_INTEGER:
              /* FIXME: for large integers fall back to Double instead. */
              return cson_value_new_integer( (cson_int_t) sqlite3_column_int64(st, col)  );
          case SQLITE_FLOAT:
              return cson_value_new_double( sqlite3_column_double(st, col) );
          case SQLITE_BLOB: /* arguably fall through... */
          case SQLITE_TEXT: {
              char const * str = (char const *)sqlite3_column_text(st,col);
              return cson_value_new_string(str, str ? strlen(str) : 0);
          }
          default:
              return NULL;
        }
    }
}

cson_value * cson_sqlite3_column_names( sqlite3_stmt * st )
{
    cson_value * aryV = NULL;
    cson_array * ary = NULL;
    char const * colName = NULL;
    int i = 0;
    int rc = 0;
    int colCount = 0;
    assert(st);
    colCount = sqlite3_column_count(st);
    if( colCount <= 0 ) return NULL;
    
    aryV = cson_value_new_array();
    if( ! aryV ) return NULL;
    ary = cson_value_get_array(aryV);
    assert(ary);
    for( i = 0; (0 ==rc) && (i < colCount); ++i )
    {
        colName = sqlite3_column_name( st, i );
        if( ! colName ) rc = cson_rc.AllocError;
        else
        {
            rc = cson_array_set( ary, (unsigned int)i,
                    cson_value_new_string(colName, strlen(colName)) );
        }
    }
    if( 0 == rc ) return aryV;
    else
    {
        cson_value_free(aryV);
        return NULL;
    }
}


cson_value * cson_sqlite3_row_to_object( sqlite3_stmt * st )
{
    cson_value * rootV = NULL;
    cson_object * root = NULL;
    char const * colName = NULL;
    int i = 0;
    int rc = 0;
    cson_value * currentValue = NULL;
    int const colCount = sqlite3_column_count(st);
    if( !colCount ) return NULL;
    rootV = cson_value_new_object();
    if(!rootV) return NULL;
    root = cson_value_get_object(rootV);
    for( i = 0; i < colCount; ++i )
    {
        colName = sqlite3_column_name( st, i );
        if( ! colName ) goto error;
        currentValue = cson_sqlite3_column_to_value(st,i);
        if( ! currentValue ) currentValue = cson_value_null();
        rc = cson_object_set( root, colName, currentValue );
        if( 0 != rc )
        {
            cson_value_free( currentValue );
            goto error;
        }
    }
    goto end;
    error:
    cson_value_free( rootV );
    rootV = NULL;
    end:
    return rootV;
}

cson_value * cson_sqlite3_row_to_array( sqlite3_stmt * st )
{
    cson_value * aryV = NULL;
    cson_array * ary = NULL;
    int i = 0;
    int rc = 0;
    int const colCount = sqlite3_column_count(st);
    if( ! colCount ) return NULL;
    aryV = cson_value_new_array();
    if( ! aryV ) return NULL;
    ary = cson_value_get_array(aryV);
    rc = cson_array_reserve(ary, (unsigned int) colCount );
    if( 0 != rc ) goto error;

    for( i = 0; i < colCount; ++i ){
        cson_value * elem = cson_sqlite3_column_to_value(st,i);
        if( ! elem ) goto error;
        rc = cson_array_append(ary,elem);
        if(0!=rc)
        {
            cson_value_free( elem );
            goto end;
        }
    }
    goto end;
    error:
    cson_value_free(aryV);
    aryV = NULL;
    end:
    return aryV;
}

    
/**
    Internal impl of cson_sqlite3_stmt_to_json() when the 'fat'
    parameter is non-0.
*/
static int cson_sqlite3_stmt_to_json_fat( sqlite3_stmt * st, cson_value ** tgt )
{
#define RETURN(RC) { if(rootV) cson_value_free(rootV); return RC; }
    if( ! tgt || !st ) return cson_rc.ArgError;
    else
    {
        cson_value * rootV = NULL;
        cson_object * root = NULL;
        cson_value * colsV = NULL;
        cson_value * rowsV = NULL;
        cson_array * rows = NULL;
        cson_value * objV = NULL;
        int rc = 0;
        int const colCount = sqlite3_column_count(st);
        if( colCount <= 0 ) return cson_rc.ArgError;
        rootV = cson_value_new_object();
        if( ! rootV ) return cson_rc.AllocError;
        colsV = cson_sqlite3_column_names(st);
        if( ! colsV )
        {
            cson_value_free( rootV );
            RETURN(cson_rc.AllocError);
        }
        root = cson_value_get_object(rootV);
        rc = cson_object_set( root, "columns", colsV );
        if( rc )
        {
            cson_value_free( colsV );
            RETURN(rc);
        }
        colsV = NULL;
        rowsV = cson_value_new_array();
        if( ! rowsV ) RETURN(cson_rc.AllocError);
        rc = cson_object_set( root, "rows", rowsV );
        if( rc )
        {
            cson_value_free( rowsV );
            RETURN(rc);
        }
        rows = cson_value_get_array(rowsV);
        assert(rows);
        while( SQLITE_ROW == sqlite3_step(st) )
        {
            objV = cson_sqlite3_row_to_object(st);
            if( ! objV ) RETURN(cson_rc.UnknownError);
            rc = cson_array_append( rows, objV );
            if( rc )
            {
                cson_value_free( objV );
                RETURN(rc);
            }
        }
        *tgt = rootV;
        return 0;
    }
#undef RETURN
}

/**
    Internal impl of cson_sqlite3_stmt_to_json() when the 'fat'
    parameter is 0.
*/
static int cson_sqlite3_stmt_to_json_slim( sqlite3_stmt * st, cson_value ** tgt )
{
#define RETURN(RC) { if(rootV) cson_value_free(rootV); return RC; }
    if( ! tgt || !st ) return cson_rc.ArgError;
    else
    {
        cson_value * rootV = NULL;
        cson_object * root = NULL;
        cson_value * aryV = NULL;
        cson_array * ary = NULL;
        cson_value * rowsV = NULL;
        cson_array * rows = NULL;
        int rc = 0;
        int const colCount = sqlite3_column_count(st);
        if( colCount <= 0 ) return cson_rc.ArgError;
        rootV = cson_value_new_object();
        if( ! rootV ) return cson_rc.AllocError;
        aryV = cson_sqlite3_column_names(st);
        if( ! aryV )
        {
            cson_value_free( rootV );
            RETURN(cson_rc.AllocError);
        }
        root = cson_value_get_object(rootV);
        rc = cson_object_set( root, "columns", aryV );
        if( rc )
        {
            cson_value_free( aryV );
            RETURN(rc);
        }
        aryV = NULL;
        ary = NULL;
        rowsV = cson_value_new_array();
        if( ! rowsV ) RETURN(cson_rc.AllocError);
        rc = cson_object_set( root, "rows", rowsV );
        if( 0 != rc )
        {
            cson_value_free( rowsV );
            RETURN(rc);
        }
        rows = cson_value_get_array(rowsV);
        assert(rows);
        while( SQLITE_ROW == sqlite3_step(st) )
        {
            aryV = cson_sqlite3_row_to_array(st);
            if( ! aryV ) RETURN(cson_rc.UnknownError);
            rc = cson_array_append( rows, aryV );
            if( 0 != rc )
            {
                cson_value_free( aryV );
                RETURN(rc);
            }
        }
        *tgt = rootV;
        return 0;
    }
#undef RETURN
}

int cson_sqlite3_stmt_to_json( sqlite3_stmt * st, cson_value ** tgt, char fat )
{
    return fat
        ? cson_sqlite3_stmt_to_json_fat(st,tgt)
        : cson_sqlite3_stmt_to_json_slim(st,tgt)
        ;
}

int cson_sqlite3_sql_to_json( sqlite3 * db, cson_value ** tgt, char const * sql, char fat )
{
    if( !db || !tgt || !sql || !*sql ) return cson_rc.ArgError;
    else
    {
        sqlite3_stmt * st = NULL;
        int rc = sqlite3_prepare_v2( db, sql, -1, &st, NULL );
        if( 0 != rc ) return cson_rc.IOError /* FIXME: Better error code? */;
        rc = cson_sqlite3_stmt_to_json( st, tgt, fat );
        sqlite3_finalize( st );
        return rc;
    }        
}

#if defined(__cplusplus)
} /*extern "C"*/
#endif
#undef MARKER
#endif /* CSON_ENABLE_SQLITE3 */
/* end file ./cson_sqlite3.c */
