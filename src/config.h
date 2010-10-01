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
typedef sqlite_int64 i64;
typedef sqlite_uint64 u64;

/*
** Unsigned character type
*/
typedef unsigned char u8;

/*
** Standard colors.  These colors can also be changed using a stylesheet.
*/

/* A blue border and background.  Used for the title bar and for dates
** in a timeline.
*/
#define BORDER1       "#a0b5f4"      /* Stylesheet class: border1 */
#define BG1           "#d0d9f4"      /* Stylesheet class: bkgnd1 */

/* A red border and background.  Use for releases in the timeline.
*/
#define BORDER2       "#ec9898"      /* Stylesheet class: border2 */
#define BG2           "#f7c0c0"      /* Stylesheet class: bkgnd2 */

/* A gray background.  Used for column headers in the Wiki Table of Contents
** and to highlight ticket properties.
*/
#define BG3           "#d0d0d0"      /* Stylesheet class: bkgnd3 */

/* A light-gray background.  Used for title bar, menus, and rlog alternation
*/
#define BG4           "#f0f0f0"      /* Stylesheet class: bkgnd4 */

/* A deeper gray background.  Used for branches
*/
#define BG5           "#dddddd"      /* Stylesheet class: bkgnd5 */

/* Default HTML page header */
#define HEADER "<html>\n" \
               "<head>\n" \
               "<link rel=\"alternate\" type=\"application/rss+xml\"\n" \
               "   title=\"%N Timeline Feed\" href=\"%B/timeline.rss\">\n" \
               "<title>%N: %T</title>\n</head>\n" \
               "<body bgcolor=\"white\">"

/* Default HTML page footer */
#define FOOTER "<div id=\"footer\"><small><small>\n" \
               "<a href=\"about\">Fossil version %V</a>\n" \
               "</small></small></div>\n" \
               "</body></html>\n"

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

#endif
