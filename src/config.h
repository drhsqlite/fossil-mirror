/*
** Copyright (c) 2006 D. Richard Hipp
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
** A common header file used by all modules.
*/

/* The following macros are necessary for large-file support under
** some linux distributions, and possibly other unixes as well.
*/
#define _LARGE_FILE       1
#ifndef _FILE_OFFSET_BITS
#  define _FILE_OFFSET_BITS 64
#endif
#define _LARGEFILE_SOURCE 1

#ifndef _RC_COMPILE_

/*
** System header files used by all modules
*/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#endif

#if defined( __MINGW32__) ||  defined(__DMC__) || defined(_MSC_VER) || defined(__POCC__)
#  if defined(__DMC__)  || defined(_MSC_VER) || defined(__POCC__)
     typedef int socklen_t;
#  endif
#  ifndef _WIN32
#    define _WIN32
#  endif
#else
# include <sys/types.h>
# include <signal.h>
# include <pwd.h>
#endif

/*
** Define the compiler variant, used to compile the project
*/
#if !defined(COMPILER_NAME)
#  if defined(__DMC__)
#    define COMPILER_NAME "dmc"
#  elif defined(__POCC__)
#    if defined(_M_X64)
#      define COMPILER_NAME "pellesc64"
#    else
#      define COMPILER_NAME "pellesc32"
#    endif
#  elif defined(_MSC_VER)
#    define COMPILER_NAME "msc"
#  elif defined(__MINGW32__)
#    define COMPILER_NAME "mingw32"
#  elif defined(_WIN32)
#    define COMPILER_NAME "win32"
#  elif defined(__GNUC__)
#    define COMPILER_NAME "gcc-" __VERSION__
#  else
#    define COMPILER_NAME "unknown"
#  endif
#endif

#ifndef _RC_COMPILE_

#include "sqlite3.h"

/*
** Typedef for a 64-bit integer
*/
typedef sqlite3_int64 i64;
typedef sqlite3_uint64 u64;

/*
** Unsigned character type
*/
typedef unsigned char u8;

/* In the timeline, check-in messages are truncated at the first space
** that is more than MX_CKIN_MSG from the beginning, or at the first
** paragraph break that is more than MN_CKIN_MSG from the beginning.
*/
#define MN_CKIN_MSG   100
#define MX_CKIN_MSG   300

/* Unset the following to disable internationalization code. */
#ifndef FOSSIL_I18N
# define FOSSIL_I18N 1
#endif

#if FOSSIL_I18N
# include <locale.h>
# include <langinfo.h>
#endif
#ifndef CODESET
# undef FOSSIL_I18N
# define FOSSIL_I18N 0
#endif

#endif /* _RC_COMPILE_ */
