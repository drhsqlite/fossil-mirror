/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** This file contains code used to format and print comments or other
** text on a TTY.
*/
#include "config.h"
#include "comformat.h"
#include <assert.h>

#if INTERFACE
#define COMMENT_PRINT_NONE       ((u32)0x00000000) /* No flags */
#define COMMENT_PRINT_CANONICAL  ((u32)0x00000001) /* Use canonical algorithm */
#define COMMENT_PRINT_DEFAULT    COMMENT_PRINT_CANONICAL  /* Default */
#define COMMENT_PRINT_UNSET      (-1)              /* Not initialized */

/* The canonical comment printing algorithm is recommended.  We make
** no promise of on-going support for any of the following flags:
*/
#define COMMENT_PRINT_TRIM_CRLF  ((u32)0x00000002) /* Trim leading CR/LF. */
#define COMMENT_PRINT_TRIM_SPACE ((u32)0x00000004) /* Trim leading/trailing. */
#define COMMENT_PRINT_WORD_BREAK ((u32)0x00000008) /* Break lines on words. */
#define COMMENT_PRINT_ORIG_BREAK ((u32)0x00000010) /* Break before original. */
#endif

/********* Code copied from SQLite src/shell.c.in on 2024-09-30 **********/
/* Lookup table to estimate the number of columns consumed by a Unicode
** character.
*/
static const struct {
  unsigned char w;    /* Width of the character in columns */
  int iFirst;         /* First character in a span having this width */
} aUWidth[] = {
   /* {1, 0x00000}, */
  {0, 0x00300},  {1, 0x00370},  {0, 0x00483},  {1, 0x00487},  {0, 0x00488},
  {1, 0x0048a},  {0, 0x00591},  {1, 0x005be},  {0, 0x005bf},  {1, 0x005c0},
  {0, 0x005c1},  {1, 0x005c3},  {0, 0x005c4},  {1, 0x005c6},  {0, 0x005c7},
  {1, 0x005c8},  {0, 0x00600},  {1, 0x00604},  {0, 0x00610},  {1, 0x00616},
  {0, 0x0064b},  {1, 0x0065f},  {0, 0x00670},  {1, 0x00671},  {0, 0x006d6},
  {1, 0x006e5},  {0, 0x006e7},  {1, 0x006e9},  {0, 0x006ea},  {1, 0x006ee},
  {0, 0x0070f},  {1, 0x00710},  {0, 0x00711},  {1, 0x00712},  {0, 0x00730},
  {1, 0x0074b},  {0, 0x007a6},  {1, 0x007b1},  {0, 0x007eb},  {1, 0x007f4},
  {0, 0x00901},  {1, 0x00903},  {0, 0x0093c},  {1, 0x0093d},  {0, 0x00941},
  {1, 0x00949},  {0, 0x0094d},  {1, 0x0094e},  {0, 0x00951},  {1, 0x00955},
  {0, 0x00962},  {1, 0x00964},  {0, 0x00981},  {1, 0x00982},  {0, 0x009bc},
  {1, 0x009bd},  {0, 0x009c1},  {1, 0x009c5},  {0, 0x009cd},  {1, 0x009ce},
  {0, 0x009e2},  {1, 0x009e4},  {0, 0x00a01},  {1, 0x00a03},  {0, 0x00a3c},
  {1, 0x00a3d},  {0, 0x00a41},  {1, 0x00a43},  {0, 0x00a47},  {1, 0x00a49},
  {0, 0x00a4b},  {1, 0x00a4e},  {0, 0x00a70},  {1, 0x00a72},  {0, 0x00a81},
  {1, 0x00a83},  {0, 0x00abc},  {1, 0x00abd},  {0, 0x00ac1},  {1, 0x00ac6},
  {0, 0x00ac7},  {1, 0x00ac9},  {0, 0x00acd},  {1, 0x00ace},  {0, 0x00ae2},
  {1, 0x00ae4},  {0, 0x00b01},  {1, 0x00b02},  {0, 0x00b3c},  {1, 0x00b3d},
  {0, 0x00b3f},  {1, 0x00b40},  {0, 0x00b41},  {1, 0x00b44},  {0, 0x00b4d},
  {1, 0x00b4e},  {0, 0x00b56},  {1, 0x00b57},  {0, 0x00b82},  {1, 0x00b83},
  {0, 0x00bc0},  {1, 0x00bc1},  {0, 0x00bcd},  {1, 0x00bce},  {0, 0x00c3e},
  {1, 0x00c41},  {0, 0x00c46},  {1, 0x00c49},  {0, 0x00c4a},  {1, 0x00c4e},
  {0, 0x00c55},  {1, 0x00c57},  {0, 0x00cbc},  {1, 0x00cbd},  {0, 0x00cbf},
  {1, 0x00cc0},  {0, 0x00cc6},  {1, 0x00cc7},  {0, 0x00ccc},  {1, 0x00cce},
  {0, 0x00ce2},  {1, 0x00ce4},  {0, 0x00d41},  {1, 0x00d44},  {0, 0x00d4d},
  {1, 0x00d4e},  {0, 0x00dca},  {1, 0x00dcb},  {0, 0x00dd2},  {1, 0x00dd5},
  {0, 0x00dd6},  {1, 0x00dd7},  {0, 0x00e31},  {1, 0x00e32},  {0, 0x00e34},
  {1, 0x00e3b},  {0, 0x00e47},  {1, 0x00e4f},  {0, 0x00eb1},  {1, 0x00eb2},
  {0, 0x00eb4},  {1, 0x00eba},  {0, 0x00ebb},  {1, 0x00ebd},  {0, 0x00ec8},
  {1, 0x00ece},  {0, 0x00f18},  {1, 0x00f1a},  {0, 0x00f35},  {1, 0x00f36},
  {0, 0x00f37},  {1, 0x00f38},  {0, 0x00f39},  {1, 0x00f3a},  {0, 0x00f71},
  {1, 0x00f7f},  {0, 0x00f80},  {1, 0x00f85},  {0, 0x00f86},  {1, 0x00f88},
  {0, 0x00f90},  {1, 0x00f98},  {0, 0x00f99},  {1, 0x00fbd},  {0, 0x00fc6},
  {1, 0x00fc7},  {0, 0x0102d},  {1, 0x01031},  {0, 0x01032},  {1, 0x01033},
  {0, 0x01036},  {1, 0x01038},  {0, 0x01039},  {1, 0x0103a},  {0, 0x01058},
  {1, 0x0105a},  {2, 0x01100},  {0, 0x01160},  {1, 0x01200},  {0, 0x0135f},
  {1, 0x01360},  {0, 0x01712},  {1, 0x01715},  {0, 0x01732},  {1, 0x01735},
  {0, 0x01752},  {1, 0x01754},  {0, 0x01772},  {1, 0x01774},  {0, 0x017b4},
  {1, 0x017b6},  {0, 0x017b7},  {1, 0x017be},  {0, 0x017c6},  {1, 0x017c7},
  {0, 0x017c9},  {1, 0x017d4},  {0, 0x017dd},  {1, 0x017de},  {0, 0x0180b},
  {1, 0x0180e},  {0, 0x018a9},  {1, 0x018aa},  {0, 0x01920},  {1, 0x01923},
  {0, 0x01927},  {1, 0x01929},  {0, 0x01932},  {1, 0x01933},  {0, 0x01939},
  {1, 0x0193c},  {0, 0x01a17},  {1, 0x01a19},  {0, 0x01b00},  {1, 0x01b04},
  {0, 0x01b34},  {1, 0x01b35},  {0, 0x01b36},  {1, 0x01b3b},  {0, 0x01b3c},
  {1, 0x01b3d},  {0, 0x01b42},  {1, 0x01b43},  {0, 0x01b6b},  {1, 0x01b74},
  {0, 0x01dc0},  {1, 0x01dcb},  {0, 0x01dfe},  {1, 0x01e00},  {0, 0x0200b},
  {1, 0x02010},  {0, 0x0202a},  {1, 0x0202f},  {0, 0x02060},  {1, 0x02064},
  {0, 0x0206a},  {1, 0x02070},  {0, 0x020d0},  {1, 0x020f0},  {2, 0x02329},
  {1, 0x0232b},  {2, 0x02e80},  {0, 0x0302a},  {2, 0x03030},  {1, 0x0303f},
  {2, 0x03040},  {0, 0x03099},  {2, 0x0309b},  {1, 0x0a4d0},  {0, 0x0a806},
  {1, 0x0a807},  {0, 0x0a80b},  {1, 0x0a80c},  {0, 0x0a825},  {1, 0x0a827},
  {2, 0x0ac00},  {1, 0x0d7a4},  {2, 0x0f900},  {1, 0x0fb00},  {0, 0x0fb1e},
  {1, 0x0fb1f},  {0, 0x0fe00},  {2, 0x0fe10},  {1, 0x0fe1a},  {0, 0x0fe20},
  {1, 0x0fe24},  {2, 0x0fe30},  {1, 0x0fe70},  {0, 0x0feff},  {2, 0x0ff00},
  {1, 0x0ff61},  {2, 0x0ffe0},  {1, 0x0ffe7},  {0, 0x0fff9},  {1, 0x0fffc},
  {0, 0x10a01},  {1, 0x10a04},  {0, 0x10a05},  {1, 0x10a07},  {0, 0x10a0c},
  {1, 0x10a10},  {0, 0x10a38},  {1, 0x10a3b},  {0, 0x10a3f},  {1, 0x10a40},
  {0, 0x1d167},  {1, 0x1d16a},  {0, 0x1d173},  {1, 0x1d183},  {0, 0x1d185},
  {1, 0x1d18c},  {0, 0x1d1aa},  {1, 0x1d1ae},  {0, 0x1d242},  {1, 0x1d245},
  {2, 0x20000},  {1, 0x2fffe},  {2, 0x30000},  {1, 0x3fffe},  {0, 0xe0001},
  {1, 0xe0002},  {0, 0xe0020},  {1, 0xe0080},  {0, 0xe0100},  {1, 0xe01f0}
};

/*
** Return an estimate of the width, in columns, for the single Unicode
** character c.  For normal characters, the answer is always 1.  But the
** estimate might be 0 or 2 for zero-width and double-width characters.
**
** Different display devices display unicode using different widths.  So
** it is impossible to know that true display width with 100% accuracy.
** Inaccuracies in the width estimates might cause columns to be misaligned.
** Unfortunately, there is nothing we can do about that.
*/
static int cli_wcwidth(int c){
  int iFirst, iLast;

  /* Fast path for common characters */
  if( c<=0x300 ) return 1;

  /* The general case */
  iFirst = 0;
  iLast = sizeof(aUWidth)/sizeof(aUWidth[0]) - 1;
  while( iFirst<iLast-1 ){
    int iMid = (iFirst+iLast)/2;
    int cMid = aUWidth[iMid].iFirst;
    if( cMid < c ){
      iFirst = iMid;
    }else if( cMid > c ){
      iLast = iMid - 1;
    }else{
      return aUWidth[iMid].w;
    }
  }
  if( aUWidth[iLast].iFirst > c ) return aUWidth[iFirst].w;
  return aUWidth[iLast].w;
}
/******* End of code copied from SQLite *************************************/

/*
** This is the previous value used by most external callers when they
** needed to specify a default maximum line length to be used with the
** comment_print() function.
*/
#ifndef COMMENT_LEGACY_LINE_LENGTH
# define COMMENT_LEGACY_LINE_LENGTH    (78)
#endif

/*
** This is the number of spaces to print when a tab character is seen.
*/
#ifndef COMMENT_TAB_WIDTH
# define COMMENT_TAB_WIDTH             (8)
#endif

/*
** This function sets the maximum number of characters to print per line
** based on the detected terminal line width, if available; otherwise, it
** uses the legacy default terminal line width minus the amount to indent.
**
** Zero is returned to indicate any failure.  One is returned to indicate
** the successful detection of the terminal line width.  Negative one is
** returned to indicate the terminal line width is using the hard-coded
** legacy default value.
*/
static int comment_set_maxchars(
  int indent,
  int *pMaxChars
){
  struct TerminalSize ts;
  if ( !terminal_get_size(&ts) ){
    return 0;
  }

  if( ts.nColumns ){
    *pMaxChars = ts.nColumns - indent;
    return 1;
  }else{
    /*
    ** Fallback to using more-or-less the "legacy semantics" of hard-coding
    ** the maximum line length to a value reasonable for the vast majority
    ** of supported systems.
    */
    *pMaxChars = COMMENT_LEGACY_LINE_LENGTH - indent;
    return -1;
  }
}

/*
** This function checks the current line being printed against the original
** comment text.  Upon matching, it updates the provided character and line
** counts, if applicable.  The caller needs to emit a new line, if desired.
*/
static int comment_check_orig(
  const char *zOrigText, /* [in] Original comment text ONLY, may be NULL. */
  const char *zLine,     /* [in] The comment line to print. */
  int *pCharCnt,         /* [in/out] Pointer to the line character count. */
  int *pLineCnt          /* [in/out] Pointer to the total line count. */
){
  if( zOrigText && fossil_strcmp(zLine, zOrigText)==0 ){
    if( pCharCnt ) *pCharCnt = 0;
    if( pLineCnt ) (*pLineCnt)++;
    return 1;
  }
  return 0;
}

/*
** This function scans the specified comment line starting just after the
** initial index and returns the index of the next spacing character -OR-
** zero if such a character cannot be found.  For the purposes of this
** algorithm, the NUL character is treated the same as a spacing character.
*/
static int comment_next_space(
  const char *zLine, /* [in] The comment line being printed. */
  int index,         /* [in] The current character index being handled. */
  int maxChars,      /* [in] Optimization hint to abort before space found. */
  int *sumWidth      /* [out] Summated width of all characters to next space. */
){
  int cchUTF8, utf32, wcwidth = 0;
  int nextIndex = index;
  if( zLine[index]==0 ) return index;
  for(;;){
    char_info_utf8(&zLine[nextIndex],&cchUTF8,&utf32);
    nextIndex += cchUTF8;
    wcwidth += cli_wcwidth(utf32);
    if( zLine[nextIndex]==0 || fossil_isspace(zLine[nextIndex]) ||
        wcwidth>maxChars ){
      *sumWidth = wcwidth;
      return nextIndex;
    }
  }
  return 0; /* NOT REACHED */
}

/*
** Return information about the next (single- or multi-byte) character in
** z[0].  Two values are computed:
**
**     *   The number of bytes needed to represent the character.
**     *   The UTF code point value.
**
** Incomplete, ill-formed and overlong sequences are consumed together as
** one invalid code point. The invalid lead bytes 0xC0 to 0xC1 and 0xF5 to
** 0xF7 are allowed to initiate (ill-formed) 2- and 4-byte sequences,
** respectively, the other invalid lead bytes 0xF8 to 0xFF are treated
** as invalid 1-byte sequences (as lone trail bytes), all resulting
** in one invalid code point. Invalid UTF-8 sequences encoding a
** non-scalar code point (UTF-16 surrogates U+D800 to U+DFFF) are allowed.
**
** ANSI escape sequences of the form "\033[...X" are interpreted as a
** zero-width character.
*/
void char_info_utf8(
  const char *z,       /* The character to be analyzed */
  int *pCchUTF8,       /* OUT: The number of bytes used by this character */
  int *pUtf32          /* OUT: The UTF8 code point (used to determine width) */
){
  int i = 0;                              /* Counted bytes. */
  int cchUTF8 = 1;                        /* Code units consumed. */
  int maxUTF8 = 1;                        /* Expected sequence length. */
  char c = z[i++];
  if( c==0x1b && z[i]=='[' ){
    i++;
    while( z[i]>=0x30 && z[i]<=0x3f ){ i++; }
    while( z[i]>=0x20 && z[i]<=0x2f ){ i++; }
    if( z[i]>=0x40 && z[i]<=0x7e ){
      *pCchUTF8 = i+1;
      *pUtf32 = 0x301;  /* A zero-width character */
      return;
    }
  }
  if( (c&0x80)==0x00 ){                   /* 7-bit ASCII character. */
    *pCchUTF8 = 1;
    *pUtf32 = (int)z[0];
    return;
  }
  else if( (c&0xe0)==0xc0 ) maxUTF8 = 2;  /* UTF-8 lead byte 110vvvvv */
  else if( (c&0xf0)==0xe0 ) maxUTF8 = 3;  /* UTF-8 lead byte 1110vvvv */
  else if( (c&0xf8)==0xf0 ) maxUTF8 = 4;  /* UTF-8 lead byte 11110vvv */
  while( cchUTF8<maxUTF8 &&
          (z[i]&0xc0)==0x80 ){            /* UTF-8 trail byte 10vvvvvv */
    cchUTF8++;
    i++;
  }
  *pCchUTF8 = cchUTF8;
  if( cchUTF8!=maxUTF8 ||                 /* Incomplete UTF-8 sequence. */
      ( cchUTF8==1 && (c&0x80)==0x80 )){  /* Lone UTF-8 trail byte. */
    *pUtf32 = 0xfffd;                     /* U+FFFD Replacement Character */
#ifdef FOSSIL_DEBUG
    assert( *pUtf32!=0xfffd );            /* Invalid UTF-8 sequence. */
#endif
    return;
  }
  switch( cchUTF8 ){
    case 4:
      *pUtf32 =
        ( (z[0] & 0x0f)<<18 ) |
        ( (z[1] & 0x3f)<<12 ) |
        ( (z[2] & 0x3f)<< 6 ) |
        ( (z[4] & 0x3f)<< 0 ) ;
      break;
    case 3:
      *pUtf32 =
        ( (z[0] & 0x0f)<<12 ) | 
        ( (z[1] & 0x3f)<< 6 ) | 
        ( (z[2] & 0x3f)<< 0 ) ;
      break;
    case 2:
      *pUtf32 =
        ( (z[0] & 0x1f)<< 6 ) | 
        ( (z[1] & 0x3f)<< 0 ) ;
      break;
    default:
      *pUtf32 = 0xfffd;                   /* U+FFFD Replacement Character */
      break;
  }
#ifdef FOSSIL_DEBUG
  assert(
    *pUtf32>=0 && *pUtf32<=0x10ffff &&    /* Valid range U+0000 to U+10FFFF. */
    *pUtf32<0xd800 && *pUtf32>0xdfff      /* Non-scalar (UTF-16 surrogates). */
  );
#endif
}

/*
** This function is called when printing a logical comment line to calculate
** the necessary indenting.  The caller needs to emit the indenting spaces.
*/
static void comment_calc_indent(
  const char *zLine, /* [in] The comment line being printed. */
  int indent,        /* [in] Number of spaces to indent, zero for none. */
  int trimCrLf,      /* [in] Non-zero to trim leading/trailing CR/LF. */
  int trimSpace,     /* [in] Non-zero to trim leading/trailing spaces. */
  int *piIndex       /* [in/out] Pointer to first non-space character. */
){
  if( zLine && piIndex ){
    int index = *piIndex;
    if( trimCrLf ){
      while( zLine[index]=='\r' || zLine[index]=='\n' ){ index++; }
    }
    if( trimSpace ){
      while( fossil_isspace(zLine[index]) ){ index++; }
    }
    *piIndex = index;
  }
}

/*
** This function prints one logical line of a comment, stopping when it hits
** a new line -OR- runs out of space on the logical line.
*/
static void comment_print_line(
  const char *zOrigText, /* [in] Original comment text ONLY, may be NULL. */
  const char *zLine,     /* [in] The comment line to print. */
  int origIndent,        /* [in] Number of spaces to indent before the original
                         **      comment. */
  int indent,            /* [in] Number of spaces to indent, before the line
                         **      to print. */
  int lineChars,         /* [in] Maximum number of characters to print. */
  int trimCrLf,          /* [in] Non-zero to trim leading/trailing CR/LF. */
  int trimSpace,         /* [in] Non-zero to trim leading/trailing spaces. */
  int wordBreak,         /* [in] Non-zero to try breaking on word boundaries. */
  int origBreak,         /* [in] Non-zero to break before original comment. */
  int *pLineCnt,         /* [in/out] Pointer to the total line count. */
  const char **pzLine    /* [out] Pointer to the end of the logical line. */
){
  int index = 0, charCnt = 0, lineCnt = 0, maxChars, i;
  char zBuf[400]; int iBuf=0; /* Output buffer and counter. */
  if( !zLine ) return;
  if( lineChars<=0 ) return;
#if 0
  assert( indent<sizeof(zBuf)-5 );       /* See following comments to explain */
  assert( origIndent<sizeof(zBuf)-5 );   /* these limits. */
#endif
  if( indent>(int)sizeof(zBuf)-6 ){
    /* Limit initial indent to fit output buffer. */
    indent = sizeof(zBuf)-6;
  }
  comment_calc_indent(zLine, indent, trimCrLf, trimSpace, &index);
  if( indent>0 ){
    for(i=0; i<indent; i++){
      zBuf[iBuf++] = ' ';
    }
  }
  if( origIndent>(int)sizeof(zBuf)-6 ){
    /* Limit line indent to fit output buffer. */
    origIndent = sizeof(zBuf)-6;
  }
  maxChars = lineChars;
  for(;;){
    int cchUTF8, utf32;
    int useChars = 1;
    char c = zLine[index];
    /* Flush the output buffer if there's no space left for at least one more
    ** (potentially 4-byte) UTF-8 sequence, one level of indentation spaces,
    ** a new line, and a terminating NULL. */
    if( iBuf>(int)sizeof(zBuf)-origIndent-6 ){
      zBuf[iBuf]=0;
      iBuf=0;
      fossil_print("%s", zBuf);
    }
    if( c==0 ){
      break;
    }else{
      if( origBreak && index>0 ){
        const char *zCurrent = &zLine[index];
        if( comment_check_orig(zOrigText, zCurrent, &charCnt, &lineCnt) ){
          zBuf[iBuf++] = '\n';
          comment_calc_indent(zLine, origIndent, trimCrLf, trimSpace, &index);
          for( i=0; i<origIndent; i++ ){
            zBuf[iBuf++] = ' ';
          }
          maxChars = lineChars;
        }
      }
      index++;
    }
    if( c=='\n' ){
      lineCnt++;
      charCnt = 0;
      useChars = 0;
    }else if( c=='\t' ){
      int sumWidth;
      int nextIndex = comment_next_space(zLine, index, maxChars, &sumWidth);
      if( nextIndex<=0 || sumWidth>maxChars ){
        break;
      }
      charCnt++;
      useChars = COMMENT_TAB_WIDTH;
      if( maxChars<useChars ){
        zBuf[iBuf++] = ' ';
        break;
      }
    }else if( wordBreak && fossil_isspace(c) ){
      int sumWidth;
      int nextIndex = comment_next_space(zLine, index, maxChars, &sumWidth);
      if( nextIndex<=0 || sumWidth>=maxChars ){
        break;
      }
      charCnt++;
    }else{
      charCnt++;
    }
    assert( c!='\n' || charCnt==0 );
    zBuf[iBuf++] = c;
    char_info_utf8(&zLine[index-1],&cchUTF8,&utf32);
    if( cchUTF8>1 ){
      int wcwidth;
      wcwidth = cli_wcwidth(utf32);
      if( wcwidth>maxChars && lineChars>=wcwidth ){ /* rollback */
        index--;
        iBuf--;
        zBuf[iBuf] = 0;
        break;
      }
      for( ; cchUTF8>1; cchUTF8-- ){
        zBuf[iBuf++] = zLine[index++];
      }
      useChars += wcwidth - 1;
    }
    maxChars -= useChars;
    if( maxChars<=0 ) break;
    if( c=='\n' ) break;
  }
  if( charCnt>0 ){
    zBuf[iBuf++] = '\n';
    lineCnt++;
  }
  /* Flush the remaining output buffer. */
  if( iBuf>0 ){
    zBuf[iBuf]=0;
    iBuf=0;
    fossil_print("%s", zBuf);
  }
  if( pLineCnt ){
    *pLineCnt += lineCnt;
  }
  if( pzLine ){
    *pzLine = zLine + index;
  }
}

/*
** This is the canonical comment printing algorithm.  This is the algorithm
** that is recommended and that is used unless the administrator has made
** special arrangements to use a customized algorithm.
**
** Given a comment string, format that string for printing on a TTY.
** Assume that the output cursor is indent spaces from the left margin
** and that a single line can contain no more than 'width' characters.
** Indent all subsequent lines by 'indent'.
**
** Formatting features:
**
**   *  Leading whitespace is removed.
**   *  Internal whitespace sequences are changed into a single space (0x20)
**      character.
**   *  Lines are broken at a space, or at a hyphen ("-") whenever possible.
**
** Returns the number of new lines emitted.
*/
static int comment_print_canonical(
  const char *zText, /* The comment text to be printed. */
  int indent,        /* Number of spaces to indent each non-initial line. */
  int width          /* Maximum number of characters per line. */
){
  int maxChars = width - indent;
  int si, sk, i, k, kc;
  int doIndent = 0;
  char *zBuf;
  char zBuffer[400];
  int lineCnt = 0;

  if( width<0 ){
    comment_set_maxchars(indent, &maxChars);
  }
  if( zText==0 ) zText = "(NULL)";
  if( maxChars<=0 ){
    maxChars = strlen(zText);
  }
  /* Ensure the buffer can hold the longest-possible UTF-8 sequences. */
  if( maxChars >= ((int)sizeof(zBuffer)/4-1) ){
    zBuf = fossil_malloc(maxChars*4+1);
  }else{
    zBuf = zBuffer;
  }
  for(;;){
    while( fossil_isspace(zText[0]) ){ zText++; }
    if( zText[0]==0 ){
      if( doIndent==0 ){
        fossil_print("\n");
        lineCnt = 1;
      }
      if( zBuf!=zBuffer) fossil_free(zBuf);
      return lineCnt;
    }
    for(sk=si=i=k=kc=0; zText[i] && kc<maxChars; i++){
      int cchUTF8, utf32;
      char c = zText[i];
      kc++; /* Count complete UTF-8 sequences. */
      char_info_utf8(&zText[i],&cchUTF8,&utf32);
      if( cchUTF8>1 ){
        int wcwidth;
        wcwidth = cli_wcwidth(utf32);
        if( kc+wcwidth-1>maxChars && maxChars>=wcwidth ){ /* rollback */
          kc--;
          break;
        }
        for( i--; cchUTF8>0; cchUTF8-- ){
          zBuf[k++] = zText[++i];
        }
        kc += wcwidth - 1;
      }
      else if( fossil_isspace(c) ){
        si = i;
        sk = k;
        if( k==0 || zBuf[k-1]!=' ' ){
          zBuf[k++] = ' ';
        }
      }else{
        zBuf[k] = c;
        if( c=='-' && k>0 && fossil_isalpha(zBuf[k-1]) ){
          si = i+1;
          sk = k+1;
        }
        k++;
      }
    }
    if( doIndent ){
      fossil_print("%*s", indent, "");
    }
    doIndent = 1;
    if( sk>0 && zText[i] ){
      zText += si;
      zBuf[sk] = 0;
    }else{
      zText += i;
      zBuf[k] = 0;
    }
    fossil_print("%s\n", zBuf);
    lineCnt++;
  }
}

/*
** This is the comment printing function.  The comment printing algorithm
** contained within it attempts to preserve the formatting present within
** the comment string itself while honoring line width limitations.  There
** are several flags that modify the default behavior of this function:
**
**      COMMENT_PRINT_CANONICAL: Use the canonical printing algorithm:
**                                  *  Omit leading and trailing whitespace
**                                  *  Collapse internal whitespace into a
**                                     single space (0x20) character.
**                                  *  Attempt to break lines at whitespace
**                                     or hyphens.
**                               This is the recommended algorithm and is
**                               used in most cases.
**
**      COMMENT_PRINT_TRIM_CRLF: Trims leading and trailing carriage-returns
**                               and line-feeds where they do not materially
**                               impact pre-existing formatting (i.e. at the
**                               start of the comment string -AND- right
**                               before line indentation).  This flag does
**                               not apply to the legacy comment printing
**                               algorithm.  This flag may be combined with
**                               COMMENT_PRINT_TRIM_SPACE.
**
**     COMMENT_PRINT_TRIM_SPACE: Trims leading and trailing spaces where they
**                               do not materially impact the pre-existing
**                               formatting (i.e. at the start of the comment
**                               string -AND- right before line indentation).
**                               This flag does not apply to the legacy
**                               comment printing algorithm.  This flag may
**                               be combined with COMMENT_PRINT_TRIM_CRLF.
**
**     COMMENT_PRINT_WORD_BREAK: Attempts to break lines on word boundaries
**                               while honoring the logical line length.
**                               If this flag is not specified, honoring the
**                               logical line length may result in breaking
**                               lines in the middle of words.  This flag
**                               does not apply to the legacy comment
**                               printing algorithm.
**
**     COMMENT_PRINT_ORIG_BREAK: Looks for the original comment text within
**                               the text being printed.  Upon matching, a
**                               new line will be emitted, thus preserving
**                               more of the pre-existing formatting.
**
** Given a comment string, format that string for printing on a TTY.
** Assume that the output cursors is indent spaces from the left margin
** and that a single line can contain no more than 'width' characters.
** Indent all subsequent lines by 'indent'.
**
** Returns the number of new lines emitted.
*/
int comment_print(
  const char *zText,     /* The comment text to be printed. */
  const char *zOrigText, /* Original comment text ONLY, may be NULL. */
  int indent,            /* Spaces to indent each non-initial line. */
  int width,             /* Maximum number of characters per line. */
  int flags              /* Zero or more "COMMENT_PRINT_*" flags. */
){
  int maxChars = width - indent;

  if( flags & COMMENT_PRINT_CANONICAL ){
    /* Use the canonical algorithm.  This is what happens in almost
    ** all cases. */
    return comment_print_canonical(zText, indent, width);
  }else{
    /* The remaining is a more complex formatting algorithm that is very
    ** seldom used and is considered deprecated.
    */
    int trimCrLf = flags & COMMENT_PRINT_TRIM_CRLF;
    int trimSpace = flags & COMMENT_PRINT_TRIM_SPACE;
    int wordBreak = flags & COMMENT_PRINT_WORD_BREAK;
    int origBreak = flags & COMMENT_PRINT_ORIG_BREAK;
    int lineCnt = 0;
    const char *zLine;

    if( width<0 ){
      comment_set_maxchars(indent, &maxChars);
    }
    if( zText==0 ) zText = "(NULL)";
    if( maxChars<=0 ){
      maxChars = strlen(zText);
    }
    if( trimSpace ){
      while( fossil_isspace(zText[0]) ){ zText++; }
    }
    if( zText[0]==0 ){
      fossil_print("\n");
      lineCnt++;
      return lineCnt;
    }
    zLine = zText;
    for(;;){
      comment_print_line(zOrigText, zLine, indent, zLine>zText ? indent : 0,
                         maxChars, trimCrLf, trimSpace, wordBreak, origBreak,
                         &lineCnt, &zLine);
      if( zLine==0 ) break;
      while( fossil_isspace(zLine[0]) ) zLine++;
      if( zLine[0]==0 ) break;
    }
    return lineCnt;
  }
}

/*
** Return the "COMMENT_PRINT_*" flags specified by the following sources,
** evaluated in the following cascading order:
**
**    1. The local (per-repository) "comment-format" setting.
**    2. The global (all-repositories) "comment-format" setting.
**    3. The default value COMMENT_PRINT_DEFAULT.
*/
int get_comment_format(){
  int comFmtFlags;

  /* We must cache this result, else running the timeline can end up
  ** querying the comment-format setting from the global db once per
  ** timeline entry, which brings it to a crawl if that db is
  ** network-mounted. Discussed in:
  ** https://fossil-scm.org/forum/forumpost/9aaefe4e536e01bf */

  /* The global command-line option is present, or the value has been cached. */
  if( g.comFmtFlags!=COMMENT_PRINT_UNSET ){
    return g.comFmtFlags;
  }
  /* Load the local (per-repository) or global (all-repositories) value, and use
  ** g.comFmtFlags as a cache. */
  comFmtFlags = db_get_int("comment-format", COMMENT_PRINT_UNSET);
  if( comFmtFlags!=COMMENT_PRINT_UNSET ){
    g.comFmtFlags = comFmtFlags;
    return comFmtFlags;
  }
  /* Fallback to the default value. */
  g.comFmtFlags = COMMENT_PRINT_DEFAULT;
  return g.comFmtFlags;
}

/*
**
** COMMAND: test-comment-format
**
** Usage: %fossil test-comment-format [OPTIONS] TEXT [PREFIX] [ORIGTEXT]
**
** Test comment formatting and printing.  Use for testing only.
**
** The default (canonical) formatting algorithm is:
**
**    *  Omit leading/trailing whitespace
**    *  Collapse internal whitespace into a single space character.
**    *  Attempt to break lines at whitespace or at a hyphen.
**
** Use --whitespace, --origbreak, --trimcrlf, --trimspace,
** and/or --wordbreak to disable the canonical processing and do
** the special processing specified by those other options.
**
** Options:
**   --decode           Decode the text using the same method used when
**                      handling the value of a C-card from a manifest.
**   --file FILE        Omit the TEXT argument and read the comment text
**                      from FILE.
**   --indent           Number of spaces to indent (default (-1) is to
**                      auto-detect).  Zero means no indent.
**   --orig FILE        Take the value for the ORIGTEXT argumetn from FILE.
**   --origbreak        Attempt to break when the original comment text
**                      is detected.
**   --trimcrlf         Enable trimming of leading/trailing CR/LF.
**   --trimspace        Enable trimming of leading/trailing spaces.
**   --whitespace       Keep all internal whitespace.
**   --wordbreak        Attempt to break lines on word boundaries.
**   -W|--width NUM     Width of lines (default (-1) is to auto-detect).
**                      Zero means no limit.
*/
void test_comment_format(void){
  const char *zWidth;
  const char *zIndent;
  const char *zPrefix = 0;
  char *zText = 0;
  char *zOrigText = 0;
  int indent, width;
  int i;
  const char *fromFile = find_option("file", 0, 1);
  int decode = find_option("decode", 0, 0)!=0;
  int flags = COMMENT_PRINT_CANONICAL;
  const char *fromOrig = find_option("orig", 0, 1);
  if( find_option("whitespace",0,0) ){
    flags = 0;
  }
  if( find_option("trimcrlf", 0, 0) ){
    flags = COMMENT_PRINT_TRIM_CRLF;
  }
  if( find_option("trimspace", 0, 0) ){
    flags |= COMMENT_PRINT_TRIM_SPACE;
    flags &= COMMENT_PRINT_CANONICAL;
  }
  if( find_option("wordbreak", 0, 0) ){
    flags |= COMMENT_PRINT_WORD_BREAK;
    flags &= COMMENT_PRINT_CANONICAL;
  }
  if( find_option("origbreak", 0, 0) ){
    flags |= COMMENT_PRINT_ORIG_BREAK;
    flags &= COMMENT_PRINT_CANONICAL;
  }
  zWidth = find_option("width","W",1);
  if( zWidth ){
    width = atoi(zWidth);
  }else{
    width = -1; /* automatic */
  }
  zIndent = find_option("indent",0,1);
  if( zIndent ){
    indent = atoi(zIndent);
  }else{
    indent = -1; /* automatic */
  }
  verify_all_options();
  zPrefix = zText = zOrigText = 0;
  if( fromFile ){
    Blob fileData;
    blob_read_from_file(&fileData, fromFile, ExtFILE);
    zText = mprintf("%s", blob_str(&fileData));
    blob_reset(&fileData);
  }
  if( fromOrig ){
    Blob fileData;
    blob_read_from_file(&fileData, fromOrig, ExtFILE);
    zOrigText = mprintf("%s", blob_str(&fileData));
    blob_reset(&fileData);
  }
  for(i=2; i<g.argc; i++){
    if( zText==0 ){
      zText = g.argv[i];
      continue;
    }
    if( zPrefix==0 ){
      zPrefix = g.argv[i];
      continue;
    }
    if( zOrigText==0 ){
      zOrigText = g.argv[i];
      continue;
    }
    usage("[OPTIONS] TEXT [PREFIX] [ORIGTEXT]");
  }
  if( decode ){
    zText = mprintf(fromFile?"%z":"%s" /*works-like:"%s"*/, zText);
    defossilize(zText);
    if( zOrigText ){
      zOrigText = mprintf(fromFile?"%z":"%s" /*works-like:"%s"*/, zOrigText);
      defossilize(zOrigText);
    }
  }
  if( zPrefix==0 ) zPrefix = "00:00:00 ";
  if( indent<0 ){
    indent = strlen(zPrefix);
  }
  if( zPrefix && *zPrefix ){
    fossil_print("%s", zPrefix);
  }
  fossil_print("(%d lines output)\n",
               comment_print(zText, zOrigText, indent, width, flags));
}
