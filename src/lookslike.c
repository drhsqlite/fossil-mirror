/*
** Copyright (c) 2013 D. Richard Hipp
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
** This file contains code used to try to guess if a particular file is
** text or binary, what types of line endings it uses, is it UTF8 or
** UTF16, etc.
*/
#include "config.h"
#include "lookslike.h"
#include <assert.h>


#if INTERFACE

/*
** This macro is designed to return non-zero if the specified blob contains
** data that MAY be binary in nature; otherwise, zero will be returned.
*/
#define looks_like_binary(blob) \
    ((looks_like_utf8((blob), LOOK_BINARY) & LOOK_BINARY) != LOOK_NONE)

/*
** Output flags for the looks_like_utf8() and looks_like_utf16() routines used
** to convey status information about the blob content.
*/
#define LOOK_NONE    ((int)0x00000000) /* Nothing special was found. */
#define LOOK_NUL     ((int)0x00000001) /* One or more NUL chars were found. */
#define LOOK_CR      ((int)0x00000002) /* One or more CR chars were found. */
#define LOOK_LONE_CR ((int)0x00000004) /* An unpaired CR char was found. */
#define LOOK_LF      ((int)0x00000008) /* One or more LF chars were found. */
#define LOOK_LONE_LF ((int)0x00000010) /* An unpaired LF char was found. */
#define LOOK_CRLF    ((int)0x00000020) /* One or more CR/LF pairs were found. */
#define LOOK_LONG    ((int)0x00000040) /* An over length line was found. */
#define LOOK_ODD     ((int)0x00000080) /* An odd number of bytes was found. */
#define LOOK_SHORT   ((int)0x00000100) /* Unable to perform full check. */
#define LOOK_INVALID ((int)0x00000200) /* Invalid sequence was found. */
#define LOOK_BINARY  (LOOK_NUL | LOOK_LONG | LOOK_SHORT) /* May be binary. */
#define LOOK_EOL     (LOOK_LONE_CR | LOOK_LONE_LF | LOOK_CRLF) /* Line seps. */
#endif /* INTERFACE */

/* definitions for various UTF-8 sequence lengths, encoded as start value
 * and size of each valid range belonging to some lead byte*/
#define US2A  0x80, 0x01 /* for lead byte 0xC0 */
#define US2B  0x80, 0x40 /* for lead bytes 0xC2-0xDF */
#define US3A  0xA0, 0x20 /* for lead byte 0xE0 */
#define US3B  0x80, 0x40 /* for lead bytes 0xE1-0xEF */
#define US4A  0x90, 0x30 /* for lead byte 0xF0 */
#define US4B  0x80, 0x40 /* for lead bytes 0xF1-0xF3 */
#define US4C  0x80, 0x10 /* for lead byte 0xF4 */
#define US0A  0x00, 0x00 /* for any other lead byte */

/* a table used for quick lookup of the definition that goes with a
 * particular lead byte */
static const unsigned char lb_tab[] = {
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A,
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A,
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A,
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A,
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A,
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A,
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A,
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A,
  US2A, US0A, US2B, US2B, US2B, US2B, US2B, US2B,
  US2B, US2B, US2B, US2B, US2B, US2B, US2B, US2B,
  US2B, US2B, US2B, US2B, US2B, US2B, US2B, US2B,
  US2B, US2B, US2B, US2B, US2B, US2B, US2B, US2B,
  US3A, US3B, US3B, US3B, US3B, US3B, US3B, US3B,
  US3B, US3B, US3B, US3B, US3B, US3B, US3B, US3B,
  US4A, US4B, US4B, US4B, US4C, US0A, US0A, US0A,
  US0A, US0A, US0A, US0A, US0A, US0A, US0A, US0A
};

/*
** This function attempts to scan each logical line within the blob to
** determine the type of content it appears to contain.  The return value
** is a combination of one or more of the LOOK_XXX flags (see above):
**
** !LOOK_BINARY -- The content appears to consist entirely of text; however,
**                 the encoding may not be UTF-8.
**
** LOOK_BINARY -- The content appears to be binary because it contains one
**                or more embedded NUL characters or an extremely long line.
**                Since this function does not understand UTF-16, it may
**                falsely consider UTF-16 text to be binary.
**
** Additional flags (i.e. those other than the ones included in LOOK_BINARY)
** may be present in the result as well; however, they should not impact the
** determination of text versus binary content.
**
************************************ WARNING **********************************
**
** This function does not validate that the blob content is properly formed
** UTF-8.  It assumes that all code points are the same size.  It does not
** validate any code points.  It makes no attempt to detect if any [invalid]
** switches between UTF-8 and other encodings occur.
**
** The only code points that this function cares about are the NUL character,
** carriage-return, and line-feed.
**
** This function examines the contents of the blob until one of the flags
** specified in "stopFlags" is set.
**
************************************ WARNING **********************************
*/
int looks_like_utf8(const Blob *pContent, int stopFlags){
  const char *z = blob_buffer(pContent);
  unsigned int n = blob_size(pContent);
  int j, c, flags = LOOK_NONE;  /* Assume UTF-8 text, prove otherwise */

  if( n==0 ) return flags;  /* Empty file -> text */
  c = *z;
  if( c==0 ){
    flags |= LOOK_NUL;  /* NUL character in a file -> binary */
  }else if( c=='\r' ){
    flags |= LOOK_CR;
    if( n<=1 || z[1]!='\n' ){
      flags |= LOOK_LONE_CR;  /* Not enough chars or next char not LF */
    }
  }
  j = (c!='\n');
  if( !j ) flags |= (LOOK_LF | LOOK_LONE_LF);  /* Found LF as first char */
  while( !(flags&stopFlags) && --n>0 ){
    int c2 = c;
    c = *++z; ++j;
    if( c==0 ){
      flags |= LOOK_NUL;  /* NUL character in a file -> binary */
    }else if( c=='\n' ){
      flags |= LOOK_LF;
      if( c2=='\r' ){
        flags |= (LOOK_CR | LOOK_CRLF);  /* Found LF preceded by CR */
      }else{
        flags |= LOOK_LONE_LF;
      }
      if( j>LENGTH_MASK ){
        flags |= LOOK_LONG;  /* Very long line -> binary */
      }
      j = 0;
    }else if( c=='\r' ){
      flags |= LOOK_CR;
      if( n<=1 || z[1]!='\n' ){
        flags |= LOOK_LONE_CR;  /* Not enough chars or next char not LF */
      }
    }
  }
  if( n ){
    flags |= LOOK_SHORT;  /* The whole blob was not examined */
  }
  if( j>LENGTH_MASK ){
    flags |= LOOK_LONG;  /* Very long line -> binary */
  }
  return flags;
}

/*
** Checks for proper UTF-8. It uses the method described in:
**   http://en.wikipedia.org/wiki/UTF-8#Invalid_byte_sequences
** except for the "overlong form" of \u0000 which is not considered
** invalid here: Some languages like Java and Tcl use it. This function
** also considers valid the derivatives CESU-8 & WTF-8 (as described in
** the same wikipedia article referenced previously). For UTF-8 characters
** > 0x7f, the variable 'c' not necessary means the real lead byte.
** It's number of higher 1-bits indicate the number of continuation
** bytes that are expected to be followed. E.g. when 'c' has a value
** in the range 0xc0..0xdf it means that after 'c' a single continuation
** byte is expected. A value 0xe0..0xef means that after 'c' two more
** continuation bytes are expected.
*/

int invalid_utf8(
  const Blob *pContent
){
  const unsigned char *z = (unsigned char *) blob_buffer(pContent);
  unsigned int n = blob_size(pContent);
  unsigned char c; /* lead byte to be handled. */

  if( n==0 ) return 0;  /* Empty file -> OK */
  c = *z;
  while( --n>0 ){
    if( c>=0x80 ){
      const unsigned char *def; /* pointer to range table*/

      c <<= 1; /* multiply by 2 and get rid of highest bit */
      def = &lb_tab[c]; /* search fb's valid range in table */
      if( (unsigned int)(*++z-def[0])>=def[1] ){
        return LOOK_INVALID; /* Invalid UTF-8 */
      }
      c = (c>=0xC0) ? (c|3) : ' '; /* determine next lead byte */
    } else {
      c = *++z;
    }
  }
  return (c>=0x80) ? LOOK_INVALID : 0; /* Final lead byte must be ASCII. */
}

/*
** Define the type needed to represent a Unicode (UTF-16) character.
*/
#ifndef WCHAR_T
#  ifdef _WIN32
#    define WCHAR_T wchar_t
#  else
#    define WCHAR_T unsigned short
#  endif
#endif

/*
** Maximum length of a line in a text file, in UTF-16 characters.  (4096)
** The number of bytes represented by this value cannot exceed LENGTH_MASK
** bytes, because that is the line buffer size used by the diff engine.
*/
#define UTF16_LENGTH_MASK_SZ   (LENGTH_MASK_SZ-(sizeof(WCHAR_T)-sizeof(char)))
#define UTF16_LENGTH_MASK      ((1<<UTF16_LENGTH_MASK_SZ)-1)

/*
** This macro is used to swap the byte order of a UTF-16 character in the
** looks_like_utf16() function.
*/
#define UTF16_SWAP(ch)         ((((ch) << 8) & 0xff00) | (((ch) >> 8) & 0xff))
#define UTF16_SWAP_IF(expr,ch) ((expr) ? UTF16_SWAP((ch)) : (ch))

/*
** This function attempts to scan each logical line within the blob to
** determine the type of content it appears to contain.  The return value
** is a combination of one or more of the LOOK_XXX flags (see above):
**
** !LOOK_BINARY -- The content appears to consist entirely of text; however,
**                 the encoding may not be UTF-16.
**
** LOOK_BINARY -- The content appears to be binary because it contains one
**                or more embedded NUL characters or an extremely long line.
**                Since this function does not understand UTF-8, it may
**                falsely consider UTF-8 text to be binary.
**
** Additional flags (i.e. those other than the ones included in LOOK_BINARY)
** may be present in the result as well; however, they should not impact the
** determination of text versus binary content.
**
************************************ WARNING **********************************
**
** This function does not validate that the blob content is properly formed
** UTF-16.  It assumes that all code points are the same size.  It does not
** validate any code points.  It makes no attempt to detect if any [invalid]
** switches between the UTF-16be and UTF-16le encodings occur.
**
** The only code points that this function cares about are the NUL character,
** carriage-return, and line-feed.
**
** This function examines the contents of the blob until one of the flags
** specified in "stopFlags" is set.
**
************************************ WARNING **********************************
*/
int looks_like_utf16(const Blob *pContent, int bReverse, int stopFlags){
  const WCHAR_T *z = (WCHAR_T *)blob_buffer(pContent);
  unsigned int n = blob_size(pContent);
  int j, c, flags = LOOK_NONE;  /* Assume UTF-16 text, prove otherwise */

  if( n%sizeof(WCHAR_T) ){
    flags |= LOOK_ODD;  /* Odd number of bytes -> binary (UTF-8?) */
  }
  if( n<sizeof(WCHAR_T) ) return flags;  /* Zero or One byte -> binary (UTF-8?) */
  c = *z;
  if( bReverse ){
    c = UTF16_SWAP(c);
  }
  if( c==0 ){
    flags |= LOOK_NUL;  /* NUL character in a file -> binary */
  }else if( c=='\r' ){
    flags |= LOOK_CR;
    if( n<(2*sizeof(WCHAR_T)) || UTF16_SWAP_IF(bReverse, z[1])!='\n' ){
      flags |= LOOK_LONE_CR;  /* Not enough chars or next char not LF */
    }
  }
  j = (c!='\n');
  if( !j ) flags |= (LOOK_LF | LOOK_LONE_LF);  /* Found LF as first char */
  while( !(flags&stopFlags) && ((n-=sizeof(WCHAR_T))>=sizeof(WCHAR_T)) ){
    int c2 = c;
    c = *++z;
    if( bReverse ){
      c = UTF16_SWAP(c);
    }
    ++j;
    if( c==0 ){
      flags |= LOOK_NUL;  /* NUL character in a file -> binary */
    }else if( c=='\n' ){
      flags |= LOOK_LF;
      if( c2=='\r' ){
        flags |= (LOOK_CR | LOOK_CRLF);  /* Found LF preceded by CR */
      }else{
        flags |= LOOK_LONE_LF;
      }
      if( j>UTF16_LENGTH_MASK ){
        flags |= LOOK_LONG;  /* Very long line -> binary */
      }
      j = 0;
    }else if( c=='\r' ){
      flags |= LOOK_CR;
      if( n<(2*sizeof(WCHAR_T)) || UTF16_SWAP_IF(bReverse, z[1])!='\n' ){
        flags |= LOOK_LONE_CR;  /* Not enough chars or next char not LF */
      }
    }
  }
  if( n ){
    flags |= LOOK_SHORT;  /* The whole blob was not examined */
  }
  if( j>UTF16_LENGTH_MASK ){
    flags |= LOOK_LONG;  /* Very long line -> binary */
  }
  return flags;
}

/*
** This function returns an array of bytes representing the byte-order-mark
** for UTF-8.
*/
const unsigned char *get_utf8_bom(int *pnByte){
  static const unsigned char bom[] = {
    0xef, 0xbb, 0xbf, 0x00, 0x00, 0x00
  };
  if( pnByte ) *pnByte = 3;
  return bom;
}

/*
** This function returns non-zero if the blob starts with a UTF-8
** byte-order-mark (BOM).
*/
int starts_with_utf8_bom(const Blob *pContent, int *pnByte){
  const char *z = blob_buffer(pContent);
  int bomSize = 0;
  const unsigned char *bom = get_utf8_bom(&bomSize);

  if( pnByte ) *pnByte = bomSize;
  if( blob_size(pContent)<bomSize ) return 0;
  return memcmp(z, bom, bomSize)==0;
}

/*
** This function returns non-zero if the blob starts with a UTF-16
** byte-order-mark (BOM), either in the endianness of the machine
** or in reversed byte order. The UTF-32 BOM is ruled out by checking
** if the UTF-16 BOM is not immediately followed by (utf16) 0.
** pnByte is only set when the function returns 1.
**
** pbReverse is always set, even when no BOM is found. Without a BOM,
** it is set to 1 on little-endian and 0 on big-endian platforms. See
** clause D98 of conformance (section 3.10) of the Unicode standard.
*/
int starts_with_utf16_bom(
  const Blob *pContent, /* IN: Blob content to perform BOM detection on. */
  int *pnByte,          /* OUT: The number of bytes used for the BOM. */
  int *pbReverse        /* OUT: Non-zero for BOM in reverse byte-order. */
){
  const unsigned short *z = (unsigned short *)blob_buffer(pContent);
  int bomSize = sizeof(unsigned short);
  int size = blob_size(pContent);

  if( size<bomSize ) goto noBom;  /* No: cannot read BOM. */
  if( size>=(2*bomSize) && z[1]==0 ) goto noBom;  /* No: possible UTF-32. */
  if( z[0]==0xfeff ){
    if( pbReverse ) *pbReverse = 0;
  }else if( z[0]==0xfffe ){
    if( pbReverse ) *pbReverse = 1;
  }else{
    static const int one = 1;
  noBom:
    if( pbReverse ) *pbReverse = *(char *) &one;
    return 0; /* No: UTF-16 byte-order-mark not found. */
  }
  if( pnByte ) *pnByte = bomSize;
  return 1; /* Yes. */
}

/*
** Returns non-zero if the specified content could be valid UTF-16.
*/
int could_be_utf16(const Blob *pContent, int *pbReverse){
  return (blob_size(pContent) % sizeof(WCHAR_T) == 0) ?
      starts_with_utf16_bom(pContent, 0, pbReverse) : 0;
}


/*
** COMMAND: test-looks-like-utf
**
** Usage:  %fossil test-looks-like-utf FILENAME
**
** Options:
**    -n|--limit <num> Repeat looks-like function <num> times, for
**                     performance measurement. Default = 1;
**    --utf8           Ignoring BOM and file size, force UTF-8 checking
**    --utf16          Ignoring BOM and file size, force UTF-16 checking
**
** FILENAME is the name of a file to check for textual content in the UTF-8
** and/or UTF-16 encodings.
*/
void looks_like_utf_test_cmd(void){
  Blob blob;         /* the contents of the specified file */
  int fUtf8 = 0;     /* return value of starts_with_utf8_bom() */
  int fUtf16 = 0;    /* return value of starts_with_utf16_bom() */
  int fUnicode = 0;  /* return value of could_be_utf16() */
  int lookFlags = 0; /* output flags from looks_like_utf8/utf16() */
  int bRevUtf16 = 0; /* non-zero -> UTF-16 byte order reversed */
  int fForceUtf8 = find_option("utf8",0,0)!=0;
  int fForceUtf16 = find_option("utf16",0,0)!=0;
  const char *zCount = find_option("limit","n",1);
  int nRepeat = 1;

  if( g.argc!=3 ) usage("FILENAME");
  if( zCount ){
    nRepeat = atoi(zCount);
  }
  blob_read_from_file(&blob, g.argv[2]);
  while( --nRepeat >= 0 ){
    fUtf8 = starts_with_utf8_bom(&blob, 0);
    fUtf16 = starts_with_utf16_bom(&blob, 0, &bRevUtf16);
    if( fForceUtf8 ){
      fUnicode = 0;
    }else{
      fUnicode = could_be_utf16(&blob, 0) || fForceUtf16;
    }
    if( fUnicode ){
      lookFlags = looks_like_utf16(&blob, bRevUtf16, 0);
    }else{
      lookFlags = looks_like_utf8(&blob, 0) | invalid_utf8(&blob);
    }
  }
  fossil_print("File \"%s\" has %d bytes.\n",g.argv[2],blob_size(&blob));
  fossil_print("Starts with UTF-8 BOM: %s\n",fUtf8?"yes":"no");
  fossil_print("Starts with UTF-16 BOM: %s\n",
               fUtf16?(bRevUtf16?"reversed":"yes"):"no");
  fossil_print("Looks like UTF-%s: %s\n",fUnicode?"16":"8",
               (lookFlags&LOOK_BINARY)?"no":"yes");
  fossil_print("Has flag LOOK_NUL: %s\n",(lookFlags&LOOK_NUL)?"yes":"no");
  fossil_print("Has flag LOOK_CR: %s\n",(lookFlags&LOOK_CR)?"yes":"no");
  fossil_print("Has flag LOOK_LONE_CR: %s\n",
               (lookFlags&LOOK_LONE_CR)?"yes":"no");
  fossil_print("Has flag LOOK_LF: %s\n",(lookFlags&LOOK_LF)?"yes":"no");
  fossil_print("Has flag LOOK_LONE_LF: %s\n",
               (lookFlags&LOOK_LONE_LF)?"yes":"no");
  fossil_print("Has flag LOOK_CRLF: %s\n",(lookFlags&LOOK_CRLF)?"yes":"no");
  fossil_print("Has flag LOOK_LONG: %s\n",(lookFlags&LOOK_LONG)?"yes":"no");
  fossil_print("Has flag LOOK_INVALID: %s\n",
               (lookFlags&LOOK_INVALID)?"yes":"no");
  fossil_print("Has flag LOOK_ODD: %s\n",(lookFlags&LOOK_ODD)?"yes":"no");
  fossil_print("Has flag LOOK_SHORT: %s\n",(lookFlags&LOOK_SHORT)?"yes":"no");
  blob_reset(&blob);
}
