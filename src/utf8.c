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

#if defined(_WIN32)
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
  char *zUtf = fossil_malloc( nByte );
  WideCharToMultiByte(CP_UTF8, 0, zUnicode, -1, zUtf, nByte, 0, 0);
  return zUtf;
#else
  static Stmt q;
  char *zUtf8;
  db_static_prepare(&q, "SELECT :utf8");
  db_bind_text16(&q, ":utf8", zUnicode);
  db_step(&q);
  zUtf8 = fossil_strdup(db_column_text(&q, 0));
  db_reset(&q);
  return zUtf8;
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
  wchar_t *zUnicode = fossil_malloc( nByte*2 );
  MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, zUnicode, nByte);
  return zUnicode;
#else
  assert( 0 );  /* Never used in unix */
  return fossil_strdup(zUtf8);  /* TODO: implement for unix */
#endif
}

/*
** Deallocate any memory that was previously allocated by
** fossil_unicode_to_utf8() or fossil_utf8_to_unicode().
*/
void fossil_unicode_free(void *pOld){
  fossil_free(pOld);
}

#if defined(__APPLE__) && !defined(WITHOUT_ICONV)
# include <iconv.h>
#endif

/*
** Translate text from the filename character set into UTF-8.
** Return a pointer to the translated text.
** Call fossil_path_free() to deallocate any memory used to store the
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
char *fossil_path_to_utf8(const void *zPath){
#if defined(_WIN32)
  int nByte = WideCharToMultiByte(CP_UTF8, 0, zPath, -1, 0, 0, 0, 0);
  char *zUtf = sqlite3_malloc( nByte );
  char *pUtf, *qUtf;
  if( zUtf==0 ){
    return 0;
  }
  WideCharToMultiByte(CP_UTF8, 0, zPath, -1, zUtf, nByte, 0, 0);
  pUtf = qUtf = zUtf;
  while( *pUtf ) {
    if( *pUtf == (char)0xef ){
      wchar_t c = ((pUtf[1]&0x3f)<<6)|(pUtf[2]&0x3f);
      /* Only really convert it when the resulting char is in range. */
      if( c && ((c < ' ') || wcschr(L"\"*:<>?|", c)) ){
        *qUtf++ = c; pUtf+=3; continue;
      }
    }
    *qUtf++ = *pUtf++;
  }
  *qUtf = 0;
  return zUtf;
#elif defined(__CYGWIN__)
  char *zOut;
  zOut = fossil_strdup(zPath);
  return zOut;
#elif defined(__APPLE__) && !defined(WITHOUT_ICONV)
  char *zIn = (char*)zPath;
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
    zOut = fossil_strdup(zPath);
  }
  return zOut;
#else
  return (char *)zPath;  /* No-op on non-mac unix */
#endif
}

/*
** Translate text from UTF-8 to the filename character set.
** Return a pointer to the translated text.
** Call fossil_path_free() to deallocate any memory used to store the
** returned pointer when done.
**
** On Windows, characters in the range U+0001 to U+0031 and the
** characters '"', '*', ':', '<', '>', '?' and '|' are invalid
** to be used, except in the 'extended path' prefix ('?') and
** as drive specifier (':'). Therefore, translate those to characters
** in the range U+F001 - U+F07F (private use area), so those
** characters never arrive in any Windows API. The filenames might
** look strange in Windows explorer, but in the cygwin shell
** everything looks as expected.
**
** See: <http://cygwin.com/cygwin-ug-net/using-specialnames.html>
**
*/
void *fossil_utf8_to_path(const char *zUtf8, int isDir){
#ifdef _WIN32
  int nReserved = isDir ? 12 : 0; /* For dir, need room for "FILENAME.EXT" */
  int nChar = MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, 0, 0);
  /* Overallocate 6 chars, making some room for extended paths */
  wchar_t *zUnicode = sqlite3_malloc( (nChar+6) * sizeof(wchar_t) );
  wchar_t *wUnicode = zUnicode;
  if( zUnicode==0 ){
    return 0;
  }
  MultiByteToWideChar(CP_UTF8, 0, zUtf8, -1, zUnicode, nChar);
  /*
  ** If path starts with "//?/" or "\\?\" (extended path), translate
  ** any slashes to backslashes but leave the '?' intact
  */
  if( (zUtf8[0]=='\\' || zUtf8[0]=='/') && (zUtf8[1]=='\\' || zUtf8[1]=='/')
           && zUtf8[2]=='?' && (zUtf8[3]=='\\' || zUtf8[3]=='/')) {
    wUnicode[0] = wUnicode[1] = wUnicode[3] = '\\';
    zUtf8 += 4;
    wUnicode += 4;
  }
  /*
  ** If there is no "\\?\" prefix but there is a drive or UNC
  ** path prefix and the path is larger than MAX_PATH chars,
  ** no Win32 API function can handle that unless it is
  ** prefixed with the extended path prefix. See:
  ** <http://msdn.microsoft.com/en-us/library/aa365247(VS.85).aspx#maxpath>
  **/
  if( fossil_isalpha(zUtf8[0]) && zUtf8[1]==':'
           && (zUtf8[2]=='\\' || zUtf8[2]=='/') ){
    if( wUnicode==zUnicode && (nChar+nReserved)>MAX_PATH){
      memmove(wUnicode+4, wUnicode, nChar*sizeof(wchar_t));
      memcpy(wUnicode, L"\\\\?\\", 4*sizeof(wchar_t));
      wUnicode += 4;
    }
    /*
    ** If (remainder of) path starts with "<drive>:/" or "<drive>:\",
    ** leave the ':' intact but translate the backslash to a slash.
    */
    wUnicode[2] = '\\';
    wUnicode += 3;
  }else if( wUnicode==zUnicode && (nChar+nReserved)>MAX_PATH
            && (zUtf8[0]=='\\' || zUtf8[0]=='/')
            && (zUtf8[1]=='\\' || zUtf8[1]=='/') && zUtf8[2]!='?'){
    memmove(wUnicode+6, wUnicode, nChar*sizeof(wchar_t));
    memcpy(wUnicode, L"\\\\?\\UNC", 7*sizeof(wchar_t));
    wUnicode += 7;
  }
  /*
  ** In the remainder of the path, translate invalid characters to
  ** characters in the Unicode private use area. This is what makes
  ** Win32 fossil.exe work well in a Cygwin environment even when a
  ** filename contains characters which are invalid for Win32.
  */
  while( *wUnicode != '\0' ){
    if( (*wUnicode < ' ') || wcschr(L"\"*:<>?|", *wUnicode) ){
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
** fossil_path_to_utf8() or fossil_utf8_to_path().
*/
void fossil_path_free(void *pOld){
#if defined(_WIN32)
  sqlite3_free(pOld);
#elif (defined(__APPLE__) && !defined(WITHOUT_ICONV)) || defined(__CYGWIN__)
  fossil_free(pOld);
#else
  /* No-op on all other unix */
#endif
}

/*
** For a given index in a UTF-8 string, return the nearest index that is the
** start of a new code point. The returned index is equal or lower than the
** given index. The end of the string (the null-terminator) is considered a
** valid start index. The given index is returned unchanged if the string
** contains invalid UTF-8 (i.e. overlong runs of trail bytes).
** This function is useful to find code point boundaries for truncation, for
** example, so that no incomplete UTF-8 sequences are left at the end of the
** truncated string.
** This function does not attempt to keep logical and/or visual constructs
** spanning across multiple code points intact, that is no attempts are made
** keep combining characters together with their base characters, or to keep
** more complex grapheme clusters intact.
*/
#define IsUTF8TrailByte(c) ( (c&0xc0)==0x80 )
int utf8_nearest_codepoint(const char *zString, int maxByteIndex){
  int i,n;
  for( n=0, i=maxByteIndex; n<4 && i>=0; n++, i-- ){
    if( !IsUTF8TrailByte(zString[i]) ) return i;
  }
  return maxByteIndex;
}

/*
** Find the byte index corresponding to the given code point index in a UTF-8
** string. If the string contains fewer than the given number of code points,
** the index of the end of the string (the null-terminator) is returned.
** Incomplete, ill-formed and overlong sequences are counted as one sequence.
** The invalid lead bytes 0xC0 to 0xC1 and 0xF5 to 0xF7 are allowed to initiate
** (ill-formed) 2- and 4-byte sequences, respectively, the other invalid lead
** bytes 0xF8 to 0xFF are treated as invalid 1-byte sequences (as lone trail
** bytes).
*/
int utf8_codepoint_index(const char *zString, int nCodePoint){
  int i;       /* Counted bytes. */
  int lenUTF8; /* Counted UTF-8 sequences. */
  if( zString==0 ) return 0;
  for(i=0, lenUTF8=0; zString[i]!=0 && lenUTF8<nCodePoint; i++, lenUTF8++){
    char c = zString[i];
    int cchUTF8=1; /* Code units consumed. */
    int maxUTF8=1; /* Expected sequence length. */
    if( (c&0xe0)==0xc0 )maxUTF8=2;          /* UTF-8 lead byte 110vvvvv */
    else if( (c&0xf0)==0xe0 )maxUTF8=3;     /* UTF-8 lead byte 1110vvvv */
    else if( (c&0xf8)==0xf0 )maxUTF8=4;     /* UTF-8 lead byte 11110vvv */
    while( cchUTF8<maxUTF8 &&
            (zString[i+1]&0xc0)==0x80 ){    /* UTF-8 trail byte 10vvvvvv */
      cchUTF8++;
      i++;
    }
  }
  return i;
}

/*
** Display UTF-8 on the console.  Return the number of
** Characters written. If stdout or stderr is redirected
** to a file, -1 is returned and nothing is written
** to the console.
*/
#ifdef _WIN32
int fossil_utf8_to_console(
  const char *zUtf8,
  int nByte,
  int toStdErr
){
  int nChar, written = 0;
  wchar_t *zUnicode; /* Unicode version of zUtf8 */
  DWORD dummy;
  Blob blob;

  static int istty[2] = { -1, -1 };
  assert( toStdErr==0 || toStdErr==1 );
  if( istty[toStdErr]==-1 ){
    istty[toStdErr] = _isatty(toStdErr + 1) != 0;
  }
  if( !istty[toStdErr] ){
    /* stdout/stderr is not a console. */
    return -1;
  }

  /* If blob to be written to the Windows console is not
   * UTF-8, convert it to UTF-8 first.
   */
  blob_init(&blob, zUtf8, nByte);
  blob_to_utf8_no_bom(&blob, 1);
  nChar = MultiByteToWideChar(CP_UTF8, 0, blob_buffer(&blob),
      blob_size(&blob), NULL, 0);
  zUnicode = fossil_malloc( (nChar+1)*sizeof(zUnicode[0]) );
  if( zUnicode==0 ){
    return 0;
  }
  nChar = MultiByteToWideChar(CP_UTF8, 0, blob_buffer(&blob),
      blob_size(&blob), zUnicode, nChar);
  blob_reset(&blob);
  /* Split WriteConsoleW output into multiple chunks, if necessary.  See:
   * <https://connect.microsoft.com/VisualStudio/feedback/details/635230> */
  while( written<nChar ){
    int size = nChar-written;
    if( size>26000 ) size = 26000;
    WriteConsoleW(GetStdHandle(
        toStdErr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE),
        zUnicode + written, size, &dummy, 0);
    written += size;
  }
  fossil_free(zUnicode);
  return nChar;
}
#endif
