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
#define DIFFERENT_ENCODING(eType1, eType2) \
    (((eType1)^(eType2))&LOOK_TEXT)

/*
** Output flags for the looks_like_utf8() and looks_like_utf16() routines used
** to convey status information about the blob content.
*/
#define LOOK_NONE    ((int)0x00000000) /* Nothing special was found. */
#define LOOK_UNICODE ((int)0x00000002) /* Might contain valid Unicode. */
#define LOOK_TEXT    ((int)0x00000003) /* 0=binary,1=text,2=UTF16,3=reversed-UTF16. */
#define LOOK_NUL     ((int)0x00000004) /* One or more NUL chars were found. */
#define LOOK_LONE_CR ((int)0x00000008) /* An unpaired CR char was found. */
#define LOOK_LONE_LF ((int)0x00000010) /* An unpaired LF char was found. */
#define LOOK_CRLF    ((int)0x00000020) /* One or more CR/LF pairs were found. */
#define LOOK_LONG    ((int)0x00000040) /* An over length line was found. */
#define LOOK_ODD     ((int)0x00000080) /* An odd number of bytes was found. */
#define LOOK_SHORT   ((int)0x00000100) /* Unable to perform full check. */
#define LOOK_INVALID ((int)0x00000200) /* Invalid sequence was found. */
#define LOOK_BINARY  (LOOK_NUL | LOOK_LONG) /* Binary. */
#define LOOK_CR      (LOOK_LONE_CR | LOOK_CRLF) /* One or more CR chars were found. */
#define LOOK_LF      (LOOK_LONE_LF | LOOK_CRLF) /* One or more LF chars were found. */
#define LOOK_EOL     (LOOK_CR | LOOK_LONE_LF) /* Line seps. */
#endif /* INTERFACE */


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
** carriage-return, and line-feed. For the algorithm use in CR/LF detection,
** see the comments in looks_like_utf16.
**
** Checks for proper UTF-8. It uses the method described in:
**   http://en.wikipedia.org/wiki/UTF-8#Invalid_byte_sequences
** except for the "overlong form" which is not considered
** invalid: Some languages like Java and Tcl use it. For UTF-8 characters
** > 7f, the variable 'c2' not necessary means the previous character.
** It's number of higher 1-bits indicate the number of continuation bytes
** that are expected to be followed. E.g. when 'c2' has a value in the range
** 0xc0..0xdf it means that 'c' is expected to contain the last continuation
** byte of a UTF-8 character. A value 0xe0..0xef means that after 'c' one
** more continuation byte is expected.
**
** This function examines the contents of the blob until one of the flags
** specified in "stopFlags" is set.
**
************************************ WARNING **********************************
*/
int looks_like_utf8(const Blob *pContent, int stopFlags){
  const unsigned char *z = (unsigned char *) blob_buffer(pContent);
  unsigned int n = blob_size(pContent);
  unsigned char c;
  int j = 1, flags = LOOK_NONE;  /* Assume UTF-8 text, prove otherwise */

  if( n==0 ) return flags;  /* Empty file -> text */
  c = *z;
  if( c=='\n' ){
    j = 0;
    flags |= LOOK_LONE_LF;  /* previous character cannot be CR */
  } else if( c==0 ){
    flags |= LOOK_NUL;  /* NUL character in a file */
  }
  while( !(flags&stopFlags) && --n>0 ){
    unsigned char c2 = c;
    c = *++z; ++j;
    if( c2>=0x80 ){
      if( (c2>=0xC0) && (c2<0xF8) && ((c&0xC0)==0x80) ){
        /* Valid UTF-8, so far. */
        c = (c2 >= 0xE0) ? (c2<<1) : ' ';
        continue;
      }
   	  flags |= LOOK_INVALID;
    }
    if( c=='\n' ){
      if( c2=='\r' ){
        flags |= LOOK_CRLF;  /* Found LF preceded by CR */
      }else{
        flags |= LOOK_LONE_LF;  /* Found LF not preceded by CR */
      }
      if( j>LENGTH_MASK ){
        flags |= LOOK_LONG;  /* Very long line */
      }
      j = 0;
      /* Make sure the LOOK_LONE_CR flag will not be set */
      continue;
    } else if( c==0 ){
      flags |= LOOK_NUL;  /* NUL character in a file */
    }
    if( c2=='\r' ){
      flags |= LOOK_LONE_CR;  /* More chars, next char is not LF */
    }
  }
  if( c>=0x80 ){
    /* Last byte must be ASCII, there are no continuation bytes. */
    flags |= LOOK_INVALID;
  } else if( c=='\r' ){
    flags |= LOOK_LONE_CR;  /* next character cannot be LF */
  }
  if( n ){
    flags |= LOOK_SHORT;  /* Not the whole blob is examined */
  }else if( !(flags&LOOK_NUL) ){
    flags |= 1;
  }
  if( j>LENGTH_MASK ){
    flags |= LOOK_LONG;  /* Very long line -> binary */
  }
  return flags;
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
** This macro is used to swap the byte order of a UTF-16 character in the
** looks_like_utf16() function.
*/
#define UTF16_SWAP(ch)         ((((ch) << 8) & 0xFF00) | (((ch) >> 8) & 0xFF))

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
** UTF-16.  It assumes that all code points are the same size.
**
** The only code points that this function cares about are the NUL character,
** carriage-return, line-feed, 0xFFFE and 0xFFFF.
**
** The algorithm used is based on the importance of the relation between CR
** and LF as a pair. Assume that we have two consecutive characters available,
** 'c2' and 'c'. In the algorithm, we compare 'c2' with CR, and 'c' with LF
** in a loop. If both compares return as equal, we have a CRLF pair, other
** combinations result in LONE CR/LF characters. If 'c2' is not equal to CR,
** we compare it with NUL as well. Within the loop that gives 6 possible code
** paths while executing only 3 'if' statements. The only thing to watch out
** for is not to forget the first and the last characters of the blob: Those
** cannot be checked for inside the loop, because they cannot form a pair with
** characters outside the blob.
**
** For determining the LOOK_LONG flag, the UTF-8 length of the characters is
** taken. Surrogate pairs are not handled, which might result in a small
** (irrelevant) over-estimation of the real line length.
**
** The LOOK_UNICODE flag is incompatible with LOOK_NUL and LOOK_SHORT: Only
** when the blob is fully checked not to contain NUL characters it could
** be determined to possibly be UTF-16. The presence of LOOK_INVALID and
** LOOK_LONG is not taken into account for LOOK_UNICODE.
**
** This function examines the contents of the blob until one of the flags
** specified in "stopFlags" is set.
**
************************************ WARNING **********************************
*/
int looks_like_utf16(const Blob *pContent, int bReverse, int stopFlags){
  const WCHAR_T *z = (WCHAR_T *)blob_buffer(pContent);
  unsigned int n = blob_size(pContent);
  int j = 1, c, flags = LOOK_NONE;  /* Assume UTF-16 text, prove otherwise */

  if( n==0 ) return flags;  /* Empty file -> text */
  if( n%sizeof(WCHAR_T) ){
    flags |= LOOK_ODD|LOOK_SHORT;  /* Odd number of bytes -> binary (UTF-8?) */
    if( n<sizeof(WCHAR_T) ) return flags;  /* One byte -> binary (UTF-8?) */
  }
  c = *z;
  if( bReverse ){
    c = UTF16_SWAP(c);
  }
  if( c>0x7f ){
    j += (c > 0x7ff) ? 2 : 1;
    if( c>=0xfffe ){
      flags |= LOOK_INVALID;
    }
  }else if( c=='\n' ){
    j = 0;
    flags |= LOOK_LONE_LF;  /* previous character cannot be CR */
  } else if( c==0 ){
    flags |= LOOK_NUL;  /* NUL character in a file */
  }
  while( 1 ){
    int c2 = c;
    n -= sizeof(WCHAR_T);
    if( (flags&stopFlags) || n<sizeof(WCHAR_T) ) break;
    c = *++z;
    if( bReverse ){
    	c = UTF16_SWAP(c);
    }
    ++j;
    if( c>0x7f ){
      j += (c > 0x7ff) ? 2 : 1;
      if( c>=0xfffe ){
        flags |= LOOK_INVALID;
      }
    }else if( c=='\n' ){
      if( c2=='\r' ){
        flags |= LOOK_CRLF;  /* Found LF preceded by CR */
      }else{
        flags |= LOOK_LONE_LF;  /* Found LF not preceded by CR */
      }
      if( j>LENGTH_MASK ){
        flags |= LOOK_LONG;  /* Very long line */
      }
      j = 0;
      /* Make sure the LOOK_LONE_CR flag will not be set */
      continue;
    }else if( c==0 ){
      flags |= LOOK_NUL;  /* NUL character in a file */
    }
    if( c2=='\r' ){
      flags |= LOOK_LONE_CR;  /* More chars, next char is not LF */
    }
  }
  if( c=='\r' ){
    flags |= LOOK_LONE_CR;  /* next character cannot be LF */
  }
  if( n ){
    flags |= LOOK_SHORT;  /* Not the whole blob is examined */
  }else if( !(flags&LOOK_NUL) ){
    flags |= (LOOK_UNICODE|bReverse);
  }
  if( j>LENGTH_MASK ){
    flags |= LOOK_LONG;  /* Very long line -> binary */
  }
  return flags;
}

/*
** This function is designed to return 0 if the specified blob is binary
** in nature (contains NUL bytes), a combination of LOOK_??? flags otherwise.
*/
int looks_like_text(const Blob *pContent){
  int bReverse = 0;
  int lookFlags = 0;

  if ((blob_size(pContent) % sizeof(WCHAR_T) != 0) ){
    lookFlags = looks_like_utf8(pContent, LOOK_NUL);
  }else if( starts_with_utf16_bom(pContent, 0, &bReverse) ) {
    lookFlags = looks_like_utf16(pContent, bReverse, LOOK_NUL);
  }else{
    lookFlags = looks_like_utf8(pContent, LOOK_NUL);
    if( lookFlags&LOOK_NUL ){
      /* Might be UTF-16 without BOM in big-endian order. See clause
       * D98 of conformance (section 3.10) of the Unicode standard. */
      int tryFlags = looks_like_utf16(pContent, bReverse, LOOK_NUL|LOOK_INVALID);
      if( !(tryFlags&LOOK_NUL) ){
        if ( !(tryFlags&LOOK_INVALID) && (tryFlags&LOOK_EOL)){
          lookFlags = tryFlags;
        }else{
          /* Try UTF-16 without BOM in little-endian order as well. */
          tryFlags = looks_like_utf16(pContent, !bReverse, LOOK_INVALID);
          if ( !(tryFlags&LOOK_INVALID) && (tryFlags&LOOK_EOL)){
            lookFlags = tryFlags;
          }
        }
      }
    }
  }
  return (lookFlags&LOOK_NUL) ? 0 : lookFlags;
}

/*
** This function returns an array of bytes representing the byte-order-mark
** for UTF-8.
*/
const unsigned char *get_utf8_bom(int *pnByte){
  static const unsigned char bom[] = {
    0xEF, 0xBB, 0xBF, 0x00, 0x00, 0x00
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
**    --utf8           Ignoring BOM and file size, force UTF-8 checking
**    --utf16          Ignoring BOM and file size, force UTF-16 checking
**
** FILENAME is the name of a file to check for textual content in the UTF-8
** and/or UTF-16 encodings.
*/
void looks_like_utf_test_cmd(void){
  Blob blob;     /* the contents of the specified file */
  int fUtf8;     /* return value of starts_with_utf8_bom() */
  int fUtf16;    /* return value of starts_with_utf16_bom() */
  int fUnicode;  /* return value of could_be_utf16() */
  int lookFlags; /* output flags from looks_like_utf8/utf16() */
  int bRevUtf16 = 0; /* non-zero -> UTF-16 byte order reversed */
  int bRevUnicode = 0; /* non-zero -> UTF-16 byte order reversed */
  int fForceUtf8 = find_option("utf8",0,0)!=0;
  int fForceUtf16 = find_option("utf16",0,0)!=0;
  if( g.argc!=3 ) usage("FILENAME");
  blob_read_from_file(&blob, g.argv[2]);
  fUtf8 = starts_with_utf8_bom(&blob, 0);
  fUtf16 = starts_with_utf16_bom(&blob, 0, &bRevUtf16);
  if( fForceUtf8 ){
    fUnicode = 0;
  }else{
    fUnicode = could_be_utf16(&blob, &bRevUnicode) || fForceUtf16;
  }
  lookFlags = fUnicode ? looks_like_utf16(&blob, bRevUnicode, 0) :
                         looks_like_utf8(&blob, 0);
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
