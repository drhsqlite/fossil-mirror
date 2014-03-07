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

/* Make sure that in Win32 MinGW builds, _USE_32BIT_TIME_T is always defined. */
#if defined(_WIN32) && !defined(_WIN64) && !defined(_MSC_VER) && !defined(_USE_32BIT_TIME_T)
#  define _USE_32BIT_TIME_T
#endif

#ifdef HAVE_AUTOCONFIG_H
#include "autoconfig.h"
#endif

#ifndef _RC_COMPILE_

/*
** System header files used by all modules
*/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
/* #include <ctype.h> // do not use - causes problems */
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
** Utility macro to wrap an argument with double quotes.
*/
#if !defined(COMPILER_STRINGIFY)
#  define COMPILER_STRINGIFY(x)  COMPILER_STRINGIFY1(x)
#  define COMPILER_STRINGIFY1(x) #x
#endif

/*
** Define the compiler variant, used to compile the project
*/
#if !defined(COMPILER_NAME)
#  if defined(__DMC__)
#    if defined(COMPILER_VERSION) && !defined(NO_COMPILER_VERSION)
#      define COMPILER_NAME "dmc-" COMPILER_VERSION
#    else
#      define COMPILER_NAME "dmc"
#    endif
#  elif defined(__POCC__)
#    if defined(_M_X64)
#      if defined(COMPILER_VERSION) && !defined(NO_COMPILER_VERSION)
#        define COMPILER_NAME "pellesc64-" COMPILER_VERSION
#      else
#        define COMPILER_NAME "pellesc64"
#      endif
#    else
#      if defined(COMPILER_VERSION) && !defined(NO_COMPILER_VERSION)
#        define COMPILER_NAME "pellesc32-" COMPILER_VERSION
#      else
#        define COMPILER_NAME "pellesc32"
#      endif
#    endif
#  elif defined(_MSC_VER)
#    if !defined(COMPILER_VERSION)
#      define COMPILER_VERSION COMPILER_STRINGIFY(_MSC_VER)
#    endif
#    if defined(COMPILER_VERSION) && !defined(NO_COMPILER_VERSION)
#      define COMPILER_NAME "msc-" COMPILER_VERSION
#    else
#      define COMPILER_NAME "msc"
#    endif
#  elif defined(__MINGW32__)
#    if !defined(COMPILER_VERSION)
#      if defined(__MINGW_VERSION)
#        if defined(__GNUC__)
#          if defined(__VERSION__)
#            define COMPILER_VERSION COMPILER_STRINGIFY(__MINGW_VERSION) "-gcc-" __VERSION__
#          else
#            define COMPILER_VERSION COMPILER_STRINGIFY(__MINGW_VERSION) "-gcc"
#          endif
#        else
#          define COMPILER_VERSION COMPILER_STRINGIFY(__MINGW_VERSION)
#        endif
#      elif defined(__MINGW32_VERSION)
#        if defined(__GNUC__)
#          if defined(__VERSION__)
#            define COMPILER_VERSION COMPILER_STRINGIFY(__MINGW32_VERSION) "-gcc-" __VERSION__
#          else
#            define COMPILER_VERSION COMPILER_STRINGIFY(__MINGW32_VERSION) "-gcc"
#          endif
#        else
#          define COMPILER_VERSION COMPILER_STRINGIFY(__MINGW32_VERSION)
#        endif
#      endif
#    endif
#    if defined(COMPILER_VERSION) && !defined(NO_COMPILER_VERSION)
#      define COMPILER_NAME "mingw32-" COMPILER_VERSION
#    else
#      define COMPILER_NAME "mingw32"
#    endif
#  elif defined(_WIN32)
#    define COMPILER_NAME "win32"
#  elif defined(__GNUC__)
#    if !defined(COMPILER_VERSION)
#      if defined(__VERSION__)
#        define COMPILER_VERSION __VERSION__
#      endif
#    endif
#    if defined(COMPILER_VERSION) && !defined(NO_COMPILER_VERSION)
#      define COMPILER_NAME "gcc-" COMPILER_VERSION
#    else
#      define COMPILER_NAME "gcc"
#    endif
#  else
#    define COMPILER_NAME "unknown"
#  endif
#endif

#if !defined(_RC_COMPILE_) && !defined(SQLITE_AMALGAMATION)

#include "sqlite3.h"

/*
** On Solaris, getpass() will only return up to 8 characters. getpassphrase() returns up to 257.
*/
#if HAVE_GETPASSPHRASE
  #define getpass getpassphrase
#endif

/*
** Typedef for a 64-bit integer
*/
typedef sqlite3_int64 i64;
typedef sqlite3_uint64 u64;

/*
** 8-bit types
*/
typedef unsigned char u8;
typedef signed char i8;

/* In the timeline, check-in messages are truncated at the first space
** that is more than MX_CKIN_MSG from the beginning, or at the first
** paragraph break that is more than MN_CKIN_MSG from the beginning.
*/
#define MN_CKIN_MSG   100
#define MX_CKIN_MSG   300

/*
** The following macros are used to cast pointers to integers and
** integers to pointers.  The way you do this varies from one compiler
** to the next, so we have developed the following set of #if statements
** to generate appropriate macros for a wide range of compilers.
**
** The correct "ANSI" way to do this is to use the intptr_t type.
** Unfortunately, that typedef is not available on all compilers, or
** if it is available, it requires an #include of specific headers
** that vary from one machine to the next.
*/
#if defined(__PTRDIFF_TYPE__)  /* This case should work for GCC */
# define FOSSIL_INT_TO_PTR(X)  ((void*)(__PTRDIFF_TYPE__)(X))
# define FOSSIL_PTR_TO_INT(X)  ((int)(__PTRDIFF_TYPE__)(X))
#elif !defined(__GNUC__)       /* Works for compilers other than LLVM */
# define FOSSIL_INT_TO_PTR(X)  ((void*)&((char*)0)[X])
# define FOSSIL_PTR_TO_INT(X)  ((int)(((char*)X)-(char*)0))
#else                          /* Generates a warning - but it always works */
# define FOSSIL_INT_TO_PTR(X)  ((void*)(X))
# define FOSSIL_PTR_TO_INT(X)  ((int)(X))
#endif

/*
** A marker for functions that never return.
*/
#if defined(__GNUC__) || defined(__clang__)
# define NORETURN __attribute__((__noreturn__))
#else
# define NORETURN
#endif

#endif /* _RC_COMPILE_ */
