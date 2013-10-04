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
** This file contains preprocessor directives used to help integrate with the
** Cygwin runtime and build environment.  The intent of this file is to keep
** the Cygwin-specific preprocessor directives together.
*/

#if defined(__CYGWIN__) && !defined(CYGSUP_H)
#define CYGSUP_H

/*
*******************************************************************************
** Include any Cygwin-specific headers here.                                 **
*******************************************************************************
*/

#include <wchar.h>
#include <sys/cygwin.h>

/*
*******************************************************************************
** Define any Cygwin-specific preprocessor macros here.  All macros defined in
** this section should be wrapped with #ifndef, in order to allow them to be
** externally overridden.
*******************************************************************************
*/

#ifndef CP_UTF8
#  define CP_UTF8            65001
#endif

#ifndef WINBASEAPI
#  define WINBASEAPI         __declspec(dllimport)
#endif

#ifndef WINADVAPI
#  define WINADVAPI          __declspec(dllimport)
#endif

#ifndef SHSTDAPI
#  define SHSTDAPI           __declspec(dllimport)
#endif

#ifndef STDAPI
#  define STDAPI             __stdcall
#endif

#ifndef WINAPI
#  define WINAPI             __stdcall
#endif

/*
*******************************************************************************
** Declare any Cygwin-specific Win32 or other APIs here.  Functions declared in
** this section should use the built-in ANSI C types in order to make sure this
** header file continues to work as a self-contained unit.
**
** On Cygwin64, "long" is 64-bit but in Win64 it's 32-bit.  That's why in the
** signatures below "long" should not be used.  They now use "int" instead.
*******************************************************************************
*/

WINADVAPI extern WINAPI int RegOpenKeyExW(
    void *,          /* HKEY */
    const wchar_t *, /* LPCWSTR */
    unsigned int,    /* DWORD */
    unsigned int,    /* REGSAM */
    void *           /* PHKEY */
    );

WINADVAPI extern WINAPI int RegQueryValueExW(
    void *,          /* HKEY */
    const wchar_t *, /* LPCWSTR */
    unsigned int *,  /* LPDWORD */
    unsigned int *,  /* LPDWORD */
    unsigned char *, /* LPBYTE */
    unsigned int *   /* LPDWORD */
    );

SHSTDAPI extern STDAPI void *ShellExecuteW(
    void *,          /* HWND */
    const wchar_t *, /* LPCWSTR */
    const wchar_t *, /* LPCWSTR */
    const wchar_t *, /* LPCWSTR */
    const wchar_t *, /* LPCWSTR */
    int              /* INT */
    );

WINBASEAPI extern WINAPI int WideCharToMultiByte(
    unsigned int,    /* UINT */
    unsigned int,    /* DWORD */
    const wchar_t *, /* LPCWSTR */
    int,             /* int */
    char *,          /* LPSTR */
    int,             /* int */
    const char *,    /* LPCSTR */
    int *            /* LPBOOL */
    );

WINBASEAPI extern WINAPI int MultiByteToWideChar(
    unsigned int,    /* UINT */
    unsigned int,    /* DWORD */
    const char *,    /* LPCSTR */
    int,             /* int */
    wchar_t *,       /* LPWSTR */
    int              /* int */
    );

#endif /* defined(__CYGWIN__) && !defined(CYGSUP_H) */
