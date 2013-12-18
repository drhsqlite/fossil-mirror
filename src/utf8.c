/*
** Copyright (c) 2012 D. Richard Hipp
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
** This file contains utilities for converting text between UTF-8 (which
** is always used internally) and whatever encodings are used by the underlying
** filesystem and operating system.
*/
#include "config.h"
#include "utf8.h"
#include <sqlite3.h>
#ifdef _WIN32
# include <windows.h>
#endif
#include "cygsup.h"

#ifdef _WIN32
/*
** Translate MBCS to UTF-8.  Return a pointer to the translated text.
** Call fossil_mbcs_free() to deallocate any memory used to store the
** returned pointer when done.
*/
char *fossil_mbcs_to_utf8(const char *zMbcs){
  extern char *sqlite3_win32_mbcs_to_utf8(const char*);
  return sqlite3_win32_mbcs_to_utf8(zMbcs);
}

/*
** After translating from UTF-8 to MBCS, invoke this routine to deallocate
** any memory used to hold the translation
*/
void fossil_mbcs_free(char *zOld){
  sqlite3_free(zOld);
}
#endif /* _WIN32 */

/*
** Translate Unicode text into UTF-8.
** Return a pointer to the translated text.
** Call fossil_unicode_free() to deallocate any memory used to store the
** returned pointer when done.
*/
char *fossil_unicode_to_utf8(const void *zUnicode){
#if defined(_WIN32) || defined(__CYGWIN__)
  int nByte = WideCharToMultiByte(CP_UTF8, 0, zUnicode, -1, 0, 0, 0, 0);
  char *zUtf = sqlite3_malloc( nByte );
  if( zUtf==0 ){
    return 0;
  }
  WideCharToMultiByte(CP_UTF8, 0, zUnicode, -1, zUtf, nByte, 0, 0);
  return zUtf;
#else
  return fossil_strdup(zUnicode);  /* TODO: implement for unix */
#endif
}

/*
** Translate UTF-8 to unicode for use in system calls.  Return a pointer to the
** translated text..  Call fossil_unicode_free() to deallocate any memory
** used to store the returned pointer when done.
*/
void *fossil_utf8_to_unicode(const char *zUtf8){
#if defined(_WIN32) || defined(__CYGWIN__)
  int nByte = MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, 0, 0);
  wchar_t *zUnicode = sqlite3_malloc( nByte * 2 );
  if( zUnicode==0 ){
    return 0;
  }
  MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, zUnicode, nByte);
  return zUnicode;
#else
  return fossil_strdup(zUtf8);  /* TODO: implement for unix */
#endif
}

/*
** Deallocate any memory that was previously allocated by
** fossil_unicode_to_utf8().
*/
void fossil_unicode_free(void *pOld){
#if defined(_WIN32) || defined(__CYGWIN__)
  sqlite3_free(pOld);
#else
  fossil_free(pOld);
#endif
}

#if defined(__APPLE__) && !defined(WITHOUT_ICONV)
# include <iconv.h>
#endif

/*
** Translate text from the filename character set into UTF-8.
** Return a pointer to the translated text.
** Call fossil_filename_free() to deallocate any memory used to store the
** returned pointer when done.
**
** This function must not convert '\' to '/' on windows/cygwin, as it is
** used in places where we are not sure it's really filenames we are handling,
** e.g. fossil_getenv() or handling the argv arguments from main().
**
** On Windows, translate some characters in the in the range
** U+F001 - U+F07F (private use area) to ASCII. Cygwin sometimes
** generates such filenames. See:
** <http://cygwin.com/cygwin-ug-net/using-specialnames.html>
*/
char *fossil_filename_to_utf8(const void *zFilename){
#if defined(_WIN32)
  int nByte = WideCharToMultiByte(CP_UTF8, 0, zFilename, -1, 0, 0, 0, 0);
  char *zUtf = sqlite3_malloc( nByte );
  char *pUtf, *qUtf;
  if( zUtf==0 ){
    return 0;
  }
  WideCharToMultiByte(CP_UTF8, 0, zFilename, -1, zUtf, nByte, 0, 0);
  pUtf = qUtf = zUtf;
  while( *pUtf ) {
    if( *pUtf == (char)0xef ){
      wchar_t c = ((pUtf[1]&0x3f)<<6)|(pUtf[2]&0x3f);
      /* Only really convert it when the resulting char is in range. */
      if ( c && ((c < ' ') || wcschr(L"\"*:<>?|", c)) ){
        *qUtf++ = c; pUtf+=3; continue;
      }
    }
    *qUtf++ = *pUtf++;
  }
  *qUtf = 0;
  return zUtf;
#elif defined(__CYGWIN__)
  char *zOut;
  zOut = fossil_strdup(zFilename);
  return zOut;
#elif defined(__APPLE__) && !defined(WITHOUT_ICONV)
  char *zIn = (char*)zFilename;
  char *zOut;
  iconv_t cd;
  size_t n, x;
  for(n=0; zIn[n]>0 && zIn[n]<=0x7f; n++){}
  if( zIn[n]!=0 && (cd = iconv_open("UTF-8", "UTF-8-MAC"))!=(iconv_t)-1 ){
    char *zOutx;
    char *zOrig = zIn;
    size_t nIn, nOutx;
    nIn = n = strlen(zIn);
    nOutx = nIn+100;
    zOutx = zOut = fossil_malloc( nOutx+1 );
    x = iconv(cd, &zIn, &nIn, &zOutx, &nOutx);
    if( x==(size_t)-1 ){
      fossil_free(zOut);
      zOut = fossil_strdup(zOrig);
    }else{
      zOut[n+100-nOutx] = 0;
    }
    iconv_close(cd);
  }else{
    zOut = fossil_strdup(zFilename);
  }
  return zOut;
#else
  return (char *)zFilename;  /* No-op on non-mac unix */
#endif
}

/*
** Translate text from UTF-8 to the filename character set.
** Return a pointer to the translated text.
** Call fossil_filename_free() to deallocate any memory used to store the
** returned pointer when done.
**
** On Windows, characters in the range U+0001 to U+0031 and the
** characters '"', '*', ':', '<', '>', '?' and '|' are invalid
** to be used. Therefore, translate those to characters in the
** in the range U+F001 - U+F07F (private use area), so those
** characters never arrive in any Windows API. The filenames might
** look strange in Windows explorer, but in the cygwin shell
** everything looks as expected.
**
** See: <http://cygwin.com/cygwin-ug-net/using-specialnames.html>
**
*/
void *fossil_utf8_to_filename(const char *zUtf8){
#ifdef _WIN32
  int nChar = MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, 0, 0);
  /* Overallocate 6 chars, making some room for extended paths */
  wchar_t *zUnicode = sqlite3_malloc( (nChar+6) * sizeof(wchar_t) );
  wchar_t *wUnicode = zUnicode;
  if( zUnicode==0 ){
    return 0;
  }
  if( fossil_isalpha(zUtf8[0]) && zUtf8[1]==':'
           && (zUtf8[2]=='\\' || zUtf8[2]=='/')) {
    /* If path starts with "<drive>:[/\]", don't process the ':' */
    if( nChar>MAX_PATH ) {
      memcpy(zUnicode, L"\\\\?\\", 8);
      wUnicode += 4;
      MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, wUnicode, nChar);
      wUnicode[2] = '\\';
      wUnicode += 3;
    }else{
      zUnicode[0] = zUtf8[0];
      memcpy(&zUnicode[1], L":\\", 2 * sizeof(wchar_t));
      wUnicode += 3;
      MultiByteToWideChar(CP_UTF8, 0, zUtf8+3, -1, wUnicode, nChar-3);
    }
  }else if( (zUtf8[0]=='\\' || zUtf8[0]=='/') &&
      (zUtf8[1]=='\\' || zUtf8[1]=='/') ) {
    if( zUtf8[2]=='?' && nChar>7 ){
      /* Don't postprocess [?:] in extended path, but do '/' -> '\' */
      memcpy(zUnicode, L"\\\\", 2 * sizeof(wchar_t));
      MultiByteToWideChar(CP_UTF8, 0, zUtf8+2, -1, zUnicode+2, nChar-2);
      if( zUtf8[3]=='/' ) zUnicode[3]='\\';
      wUnicode += 6;
    }else if( nChar>MAX_PATH ){
      /* Convert to extended UNC path. */
      memcpy(zUnicode, L"\\\\?\\UNC\\", 16);
      wUnicode += 8;
      MultiByteToWideChar(CP_UTF8, 0, zUtf8+2, -1, wUnicode, nChar-2);
    }else{
      MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, zUnicode, nChar);
    }
  }
  while( *wUnicode != '\0' ){
    if ( (*wUnicode < ' ') || wcschr(L"\"*:<>?|", *wUnicode) ){
      *wUnicode |= 0xF000;
    }else if( *wUnicode == '/' ){
      *wUnicode = '\\';
    }
    ++wUnicode;
  }
  return zUnicode;
#elif defined(__CYGWIN__)
  char *zPath, *p;
  if( fossil_isalpha(zUtf8[0]) && (zUtf8[1]==':')
      && (zUtf8[2]=='\\' || zUtf8[2]=='/')) {
    /* win32 absolute path starting with drive specifier. */
    int nByte;
    wchar_t zUnicode[2000];
    wchar_t *wUnicode = zUnicode;
    MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, zUnicode, count(zUnicode));
    while( *wUnicode != '\0' ){
      if( *wUnicode == '/' ){
        *wUnicode = '\\';
      }
      ++wUnicode;
    }
    nByte = cygwin_conv_path(CCP_WIN_W_TO_POSIX, zUnicode, NULL, 0);
    zPath = fossil_malloc(nByte);
    cygwin_conv_path(CCP_WIN_W_TO_POSIX, zUnicode, zPath, nByte);
  }else{
    zPath = fossil_strdup(zUtf8);
    zUtf8 = p = zPath;
    while( (*p = *zUtf8++) != 0){
      if( *p++ == '\\' ) {
        p[-1] = '/';
      }
    }
  }
  return zPath;
#elif defined(__APPLE__) && !defined(WITHOUT_ICONV)
  return fossil_strdup(zUtf8);
#else
  return (void *)zUtf8;  /* No-op on unix */
#endif
}

/*
** Deallocate any memory that was previously allocated by
** fossil_filename_to_utf8() or fossil_utf8_to_filename().
*/
void fossil_filename_free(void *pOld){
#if defined(_WIN32)
  sqlite3_free(pOld);
#elif (defined(__APPLE__) && !defined(WITHOUT_ICONV)) || defined(__CYGWIN__)
  fossil_free(pOld);
#else
  /* No-op on all other unix */
#endif
}

/*
** Display UTF-8 on the console.  Return the number of
** Characters written. If stdout or stderr is redirected
** to a file, -1 is returned and nothing is written
** to the console.
*/
int fossil_utf8_to_console(const char *zUtf8, int nByte, int toStdErr){
#ifdef _WIN32
  int nChar, written = 0;
  wchar_t *zUnicode; /* Unicode version of zUtf8 */
  DWORD dummy;

  static int istty[2] = { -1, -1 };
  if( istty[toStdErr] == -1 ){
    istty[toStdErr] = _isatty(toStdErr + 1) != 0;
  }
  if( !istty[toStdErr] ){
    /* stdout/stderr is not a console. */
    return -1;
  }

  nChar = MultiByteToWideChar(CP_UTF8, 0, zUtf8, nByte, NULL, 0);
  zUnicode = malloc( (nChar + 1) *sizeof(zUnicode[0]) );
  if( zUnicode==0 ){
    return 0;
  }
  nChar = MultiByteToWideChar(CP_UTF8, 0, zUtf8, nByte, zUnicode, nChar);
  /* Split WriteConsoleW call into multiple chunks, if necessary. See:
   * <https://connect.microsoft.com/VisualStudio/feedback/details/635230> */
  while( written < nChar ){
    int size = nChar-written;
    if( size > 26000 ) size = 26000;
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE - toStdErr), zUnicode+written,
        size, &dummy, 0);
    written += size;
  }
  free(zUnicode);
  return nChar;
#else
  return -1;  /* No-op on unix */
#endif
}
