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
** This file contains implementions of routines for formatting output
** (ex: mprintf()) and for output to the console.
*/
#include "config.h"
#include "printf.h"
#if defined(_WIN32)
#   include <io.h>
#   include <fcntl.h>
#endif
#include <time.h>

/* Two custom conversions are used to show a prefix of artifact hashes:
**
**      %!S       Prefix of a length appropriate for URLs
**      %S        Prefix of a length appropriate for human display
**
** The following macros help determine those lengths.  FOSSIL_HASH_DIGITS
** is the default number of digits to display to humans.  This value can
** be overridden using the hash-digits setting.  FOSSIL_HASH_DIGITS_URL
** is the minimum number of digits to be used in URLs.  The number used
** will always be at least 6 more than the number used for human output,
** or 40 if the number of digits in human output is 34 or more.
*/
#ifndef FOSSIL_HASH_DIGITS
# define FOSSIL_HASH_DIGITS 10       /* For %S (human display) */
#endif
#ifndef FOSSIL_HASH_DIGITS_URL
# define FOSSIL_HASH_DIGITS_URL 16   /* For %!S (embedded in URLs) */
#endif

/*
** Return the number of artifact hash digits to display.  The number is for
** human output if the bForUrl is false and is destined for a URL if
** bForUrl is false.
*/
static int hashDigits(int bForUrl){
  static int nDigitHuman = 0;
  static int nDigitUrl = 0;
  if( nDigitHuman==0 ){
    nDigitHuman = db_get_int("hash-digits", FOSSIL_HASH_DIGITS);
    if( nDigitHuman < 6 ) nDigitHuman = 6;
    if( nDigitHuman > 40 ) nDigitHuman = 40;
    nDigitUrl = nDigitHuman + 6;
    if( nDigitUrl < FOSSIL_HASH_DIGITS_URL ) nDigitUrl = FOSSIL_HASH_DIGITS_URL;
    if( nDigitUrl > 40 ) nDigitUrl = 40;
  }
  return bForUrl ? nDigitUrl : nDigitHuman;
}

/*
** Return the number of characters in a %S output.
*/
int length_of_S_display(void){
  return hashDigits(0);
}

/*
** Conversion types fall into various categories as defined by the
** following enumeration.
*/
#define etRADIX       1 /* Integer types.  %d, %x, %o, and so forth */
#define etFLOAT       2 /* Floating point.  %f */
#define etEXP         3 /* Exponential notation. %e and %E */
#define etGENERIC     4 /* Floating or exponential, depending on exponent. %g */
#define etSIZE        5 /* Return number of characters processed so far. %n */
#define etSTRING      6 /* Strings. %s */
#define etDYNSTRING   7 /* Dynamically allocated strings. %z */
#define etPERCENT     8 /* Percent symbol. %% */
#define etCHARX       9 /* Characters. %c */
#define etERROR      10 /* Used to indicate no such conversion type */
/* The rest are extensions, not normally found in printf() */
#define etBLOB       11 /* Blob objects.  %b */
#define etBLOBSQL    12 /* Blob objects quoted for SQL.  %B */
#define etSQLESCAPE  13 /* Strings with '\'' doubled.  %q */
#define etSQLESCAPE2 14 /* Strings with '\'' doubled and enclosed in '',
                          NULL pointers replaced by SQL NULL.  %Q */
#define etSQLESCAPE3 15 /* Double '"' characters within an indentifier.  %w */
#define etPOINTER    16 /* The %p conversion */
#define etHTMLIZE    17 /* Make text safe for HTML */
#define etHTTPIZE    18 /* Make text safe for HTTP.  "/" encoded as %2f */
#define etURLIZE     19 /* Make text safe for HTTP.  "/" not encoded */
#define etFOSSILIZE  20 /* The fossil header encoding format. */
#define etPATH       21 /* Path type */
#define etWIKISTR    22 /* Timeline comment text rendered from a char*: %W */
#define etSTRINGID   23 /* String with length limit for a UUID prefix: %S */
#define etROOT       24 /* String value of g.zTop: %R */


/*
** An "etByte" is an 8-bit unsigned value.
*/
typedef unsigned char etByte;

/*
** Each builtin conversion character (ex: the 'd' in "%d") is described
** by an instance of the following structure
*/
typedef struct et_info {   /* Information about each format field */
  char fmttype;            /* The format field code letter */
  etByte base;             /* The base for radix conversion */
  etByte flags;            /* One or more of FLAG_ constants below */
  etByte type;             /* Conversion paradigm */
  etByte charset;          /* Offset into aDigits[] of the digits string */
  etByte prefix;           /* Offset into aPrefix[] of the prefix string */
} et_info;

/*
** Allowed values for et_info.flags
*/
#define FLAG_SIGNED  1     /* True if the value to convert is signed */
#define FLAG_INTERN  2     /* True if for internal use only */
#define FLAG_STRING  4     /* Allow infinity precision */


/*
** The following table is searched linearly, so it is good to put the
** most frequently used conversion types first.
*/
static const char aDigits[] = "0123456789ABCDEF0123456789abcdef";
static const char aPrefix[] = "-x0\000X0";
static const et_info fmtinfo[] = {
  {  'd', 10, 1, etRADIX,      0,  0 },
  {  's',  0, 4, etSTRING,     0,  0 },
  {  'g',  0, 1, etGENERIC,    30, 0 },
  {  'z',  0, 6, etDYNSTRING,  0,  0 },
  {  'q',  0, 4, etSQLESCAPE,  0,  0 },
  {  'Q',  0, 4, etSQLESCAPE2, 0,  0 },
  {  'b',  0, 2, etBLOB,       0,  0 },
  {  'B',  0, 2, etBLOBSQL,    0,  0 },
  {  'W',  0, 2, etWIKISTR,    0,  0 },
  {  'h',  0, 4, etHTMLIZE,    0,  0 },
  {  'R',  0, 0, etROOT,       0,  0 },
  {  't',  0, 4, etHTTPIZE,    0,  0 },  /* "/" -> "%2F" */
  {  'T',  0, 4, etURLIZE,     0,  0 },  /* "/" unchanged */
  {  'w',  0, 4, etSQLESCAPE3, 0,  0 },
  {  'F',  0, 4, etFOSSILIZE,  0,  0 },
  {  'S',  0, 4, etSTRINGID,   0,  0 },
  {  'c',  0, 0, etCHARX,      0,  0 },
  {  'o',  8, 0, etRADIX,      0,  2 },
  {  'u', 10, 0, etRADIX,      0,  0 },
  {  'x', 16, 0, etRADIX,      16, 1 },
  {  'X', 16, 0, etRADIX,      0,  4 },
  {  'f',  0, 1, etFLOAT,      0,  0 },
  {  'e',  0, 1, etEXP,        30, 0 },
  {  'E',  0, 1, etEXP,        14, 0 },
  {  'G',  0, 1, etGENERIC,    14, 0 },
  {  'i', 10, 1, etRADIX,      0,  0 },
  {  'n',  0, 0, etSIZE,       0,  0 },
  {  '%',  0, 0, etPERCENT,    0,  0 },
  {  'p', 16, 0, etPOINTER,    0,  1 },
  {  '/',  0, 0, etPATH,       0,  0 },
};
#define etNINFO count(fmtinfo)

/*
** "*val" is a double such that 0.1 <= *val < 10.0
** Return the ascii code for the leading digit of *val, then
** multiply "*val" by 10.0 to renormalize.
**
** Example:
**     input:     *val = 3.14159
**     output:    *val = 1.4159    function return = '3'
**
** The counter *cnt is incremented each time.  After counter exceeds
** 16 (the number of significant digits in a 64-bit float) '0' is
** always returned.
*/
static int et_getdigit(long double *val, int *cnt){
  int digit;
  long double d;
  if( (*cnt)++ >= 16 ) return '0';
  digit = (int)*val;
  d = digit;
  digit += '0';
  *val = (*val - d)*10.0;
  return digit;
}

/*
** Size of temporary conversion buffer.
*/
#define etBUFSIZE 500

/*
** Find the length of a string as long as that length does not
** exceed N bytes.  If no zero terminator is seen in the first
** N bytes then return N.  If N is negative, then this routine
** is an alias for strlen().
*/
static int StrNLen32(const char *z, int N){
  int n = 0;
  while( (N-- != 0) && *(z++)!=0 ){ n++; }
  return n;
}

/*
** Return an appropriate set of flags for wiki_convert() for displaying
** comments on a timeline.  These flag settings are determined by
** configuration parameters.
**
** The altForm2 argument is true for "%!W" (with the "!" alternate-form-2
** flags) and is false for plain "%W".  The ! indicates that the text is
** to be rendered on a form rather than the timeline and that block markup
** is acceptable even if the "timeline-block-markup" setting is false.
*/
static int wiki_convert_flags(int altForm2){
  static int wikiFlags = 0;
  if( wikiFlags==0 ){
    if( altForm2 || db_get_boolean("timeline-block-markup", 0) ){
      wikiFlags = WIKI_INLINE | WIKI_NOBADLINKS;
    }else{
      wikiFlags = WIKI_INLINE | WIKI_NOBLOCK | WIKI_NOBADLINKS;
    }
    if( db_get_boolean("timeline-plaintext", 0) ){
      wikiFlags |= WIKI_LINKSONLY;
    }
  }
  return wikiFlags;
}



/*
** The root program.  All variations call this core.
**
** INPUTS:
**   pBlob  This is the blob where the output will be built.
**
**   fmt    This is the format string, as in the usual print.
**
**   ap     This is a pointer to a list of arguments.  Same as in
**          vfprint.
**
** OUTPUTS:
**          The return value is the total number of characters sent to
**          the function "func".  Returns -1 on a error.
**
** Note that the order in which automatic variables are declared below
** seems to make a big difference in determining how fast this beast
** will run.
*/
int vxprintf(
  Blob *pBlob,                       /* Append output to this blob */
  const char *fmt,                   /* Format string */
  va_list ap                         /* arguments */
){
  int c;                     /* Next character in the format string */
  char *bufpt;               /* Pointer to the conversion buffer */
  int precision;             /* Precision of the current field */
  int length;                /* Length of the field */
  int idx;                   /* A general purpose loop counter */
  int count;                 /* Total number of characters output */
  int width;                 /* Width of the current field */
  etByte flag_leftjustify;   /* True if "-" flag is present */
  etByte flag_plussign;      /* True if "+" flag is present */
  etByte flag_blanksign;     /* True if " " flag is present */
  etByte flag_alternateform; /* True if "#" flag is present */
  etByte flag_altform2;      /* True if "!" flag is present */
  etByte flag_zeropad;       /* True if field width constant starts with zero */
  etByte flag_long;          /* True if "l" flag is present */
  etByte flag_longlong;      /* True if the "ll" flag is present */
  etByte done;               /* Loop termination flag */
  u64 longvalue;             /* Value for integer types */
  long double realvalue;     /* Value for real types */
  const et_info *infop;      /* Pointer to the appropriate info structure */
  char buf[etBUFSIZE];       /* Conversion buffer */
  char prefix;               /* Prefix character.  "+" or "-" or " " or '\0'. */
  etByte errorflag = 0;      /* True if an error is encountered */
  etByte xtype;              /* Conversion paradigm */
  char *zExtra;              /* Extra memory used for etTCLESCAPE conversions */
  static const char spaces[] =
   "                                                                         ";
#define etSPACESIZE (sizeof(spaces)-1)
  int  exp, e2;              /* exponent of real numbers */
  double rounder;            /* Used for rounding floating point values */
  etByte flag_dp;            /* True if decimal point should be shown */
  etByte flag_rtz;           /* True if trailing zeros should be removed */
  etByte flag_exp;           /* True to force display of the exponent */
  int nsd;                   /* Number of significant digits returned */

  count = length = 0;
  bufpt = 0;
  for(; (c=(*fmt))!=0; ++fmt){
    if( c!='%' ){
      int amt;
      bufpt = (char *)fmt;
      amt = 1;
      while( (c=(*++fmt))!='%' && c!=0 ) amt++;
      blob_append(pBlob,bufpt,amt);
      count += amt;
      if( c==0 ) break;
    }
    if( (c=(*++fmt))==0 ){
      errorflag = 1;
      blob_append(pBlob,"%",1);
      count++;
      break;
    }
    /* Find out what flags are present */
    flag_leftjustify = flag_plussign = flag_blanksign =
     flag_alternateform = flag_altform2 = flag_zeropad = 0;
    done = 0;
    do{
      switch( c ){
        case '-':   flag_leftjustify = 1;     break;
        case '+':   flag_plussign = 1;        break;
        case ' ':   flag_blanksign = 1;       break;
        case '#':   flag_alternateform = 1;   break;
        case '!':   flag_altform2 = 1;        break;
        case '0':   flag_zeropad = 1;         break;
        default:    done = 1;                 break;
      }
    }while( !done && (c=(*++fmt))!=0 );
    /* Get the field width */
    width = 0;
    if( c=='*' ){
      width = va_arg(ap,int);
      if( width<0 ){
        flag_leftjustify = 1;
        width = -width;
      }
      c = *++fmt;
    }else{
      while( c>='0' && c<='9' ){
        width = width*10 + c - '0';
        c = *++fmt;
      }
    }
    if( width > etBUFSIZE-10 ){
      width = etBUFSIZE-10;
    }
    /* Get the precision */
    if( c=='.' ){
      precision = 0;
      c = *++fmt;
      if( c=='*' ){
        precision = va_arg(ap,int);
        if( precision<0 ) precision = -precision;
        c = *++fmt;
      }else{
        while( c>='0' && c<='9' ){
          precision = precision*10 + c - '0';
          c = *++fmt;
        }
      }
    }else{
      precision = -1;
    }
    /* Get the conversion type modifier */
    if( c=='l' ){
      flag_long = 1;
      c = *++fmt;
      if( c=='l' ){
        flag_longlong = 1;
        c = *++fmt;
      }else{
        flag_longlong = 0;
      }
    }else{
      flag_long = flag_longlong = 0;
    }
    /* Fetch the info entry for the field */
    infop = 0;
    xtype = etERROR;
    for(idx=0; idx<etNINFO; idx++){
      if( c==fmtinfo[idx].fmttype ){
        infop = &fmtinfo[idx];
        xtype = infop->type;
        break;
      }
    }
    zExtra = 0;

    /* Limit the precision to prevent overflowing buf[] during conversion */
    if( precision>etBUFSIZE-40 && (infop->flags & FLAG_STRING)==0 ){
      precision = etBUFSIZE-40;
    }

    /*
    ** At this point, variables are initialized as follows:
    **
    **   flag_alternateform          TRUE if a '#' is present.
    **   flag_altform2               TRUE if a '!' is present.
    **   flag_plussign               TRUE if a '+' is present.
    **   flag_leftjustify            TRUE if a '-' is present or if the
    **                               field width was negative.
    **   flag_zeropad                TRUE if the width began with 0.
    **   flag_long                   TRUE if the letter 'l' (ell) prefixed
    **                               the conversion character.
    **   flag_longlong               TRUE if the letter 'll' (ell ell) prefixed
    **                               the conversion character.
    **   flag_blanksign              TRUE if a ' ' is present.
    **   width                       The specified field width.  This is
    **                               always non-negative.  Zero is the default.
    **   precision                   The specified precision.  The default
    **                               is -1.
    **   xtype                       The class of the conversion.
    **   infop                       Pointer to the appropriate info struct.
    */
    switch( xtype ){
      case etPOINTER:
        flag_longlong = sizeof(char*)==sizeof(i64);
        flag_long = sizeof(char*)==sizeof(long int);
        /* Fall through into the next case */
      case etRADIX:
        if( infop->flags & FLAG_SIGNED ){
          i64 v;
          if( flag_longlong )   v = va_arg(ap,i64);
          else if( flag_long )  v = va_arg(ap,long int);
          else                  v = va_arg(ap,int);
          if( v<0 ){
            longvalue = -v;
            prefix = '-';
          }else{
            longvalue = v;
            if( flag_plussign )        prefix = '+';
            else if( flag_blanksign )  prefix = ' ';
            else                       prefix = 0;
          }
        }else{
          if( flag_longlong )   longvalue = va_arg(ap,u64);
          else if( flag_long )  longvalue = va_arg(ap,unsigned long int);
          else                  longvalue = va_arg(ap,unsigned int);
          prefix = 0;
        }
        if( longvalue==0 ) flag_alternateform = 0;
        if( flag_zeropad && precision<width-(prefix!=0) ){
          precision = width-(prefix!=0);
        }
        bufpt = &buf[etBUFSIZE-1];
        {
          register const char *cset;      /* Use registers for speed */
          register int base;
          cset = &aDigits[infop->charset];
          base = infop->base;
          do{                                           /* Convert to ascii */
            *(--bufpt) = cset[longvalue%base];
            longvalue = longvalue/base;
          }while( longvalue>0 );
        }
        length = &buf[etBUFSIZE-1]-bufpt;
        for(idx=precision-length; idx>0; idx--){
          *(--bufpt) = '0';                             /* Zero pad */
        }
        if( prefix ) *(--bufpt) = prefix;               /* Add sign */
        if( flag_alternateform && infop->prefix ){      /* Add "0" or "0x" */
          const char *pre;
          char x;
          pre = &aPrefix[infop->prefix];
          if( *bufpt!=pre[0] ){
            for(; (x=(*pre))!=0; pre++) *(--bufpt) = x;
          }
        }
        length = &buf[etBUFSIZE-1]-bufpt;
        break;
      case etFLOAT:
      case etEXP:
      case etGENERIC:
        realvalue = va_arg(ap,double);
        if( precision<0 ) precision = 6;         /* Set default precision */
        if( precision>etBUFSIZE/2-10 ) precision = etBUFSIZE/2-10;
        if( realvalue<0.0 ){
          realvalue = -realvalue;
          prefix = '-';
        }else{
          if( flag_plussign )          prefix = '+';
          else if( flag_blanksign )    prefix = ' ';
          else                         prefix = 0;
        }
        if( xtype==etGENERIC && precision>0 ) precision--;
#if 0
        /* Rounding works like BSD when the constant 0.4999 is used.  Wierd! */
        for(idx=precision, rounder=0.4999; idx>0; idx--, rounder*=0.1);
#else
        /* It makes more sense to use 0.5 */
        for(idx=precision, rounder=0.5; idx>0; idx--, rounder*=0.1);
#endif
        if( xtype==etFLOAT ) realvalue += rounder;
        /* Normalize realvalue to within 10.0 > realvalue >= 1.0 */
        exp = 0;
        if( realvalue>0.0 ){
          while( realvalue>=1e32 && exp<=350 ){ realvalue *= 1e-32; exp+=32; }
          while( realvalue>=1e8 && exp<=350 ){ realvalue *= 1e-8; exp+=8; }
          while( realvalue>=10.0 && exp<=350 ){ realvalue *= 0.1; exp++; }
          while( realvalue<1e-8 && exp>=-350 ){ realvalue *= 1e8; exp-=8; }
          while( realvalue<1.0 && exp>=-350 ){ realvalue *= 10.0; exp--; }
          if( exp>350 || exp<-350 ){
            bufpt = "NaN";
            length = 3;
            break;
          }
        }
        bufpt = buf;
        /*
        ** If the field type is etGENERIC, then convert to either etEXP
        ** or etFLOAT, as appropriate.
        */
        flag_exp = xtype==etEXP;
        if( xtype!=etFLOAT ){
          realvalue += rounder;
          if( realvalue>=10.0 ){ realvalue *= 0.1; exp++; }
        }
        if( xtype==etGENERIC ){
          flag_rtz = !flag_alternateform;
          if( exp<-4 || exp>precision ){
            xtype = etEXP;
          }else{
            precision = precision - exp;
            xtype = etFLOAT;
          }
        }else{
          flag_rtz = 0;
        }
        if( xtype==etEXP ){
          e2 = 0;
        }else{
          e2 = exp;
        }
        nsd = 0;
        flag_dp = (precision>0) | flag_alternateform | flag_altform2;
        /* The sign in front of the number */
        if( prefix ){
          *(bufpt++) = prefix;
        }
        /* Digits prior to the decimal point */
        if( e2<0 ){
          *(bufpt++) = '0';
        }else{
          for(; e2>=0; e2--){
            *(bufpt++) = et_getdigit(&realvalue,&nsd);
          }
        }
        /* The decimal point */
        if( flag_dp ){
          *(bufpt++) = '.';
        }
        /* "0" digits after the decimal point but before the first
        ** significant digit of the number */
        for(e2++; e2<0 && precision>0; precision--, e2++){
          *(bufpt++) = '0';
        }
        /* Significant digits after the decimal point */
        while( (precision--)>0 ){
          *(bufpt++) = et_getdigit(&realvalue,&nsd);
        }
        /* Remove trailing zeros and the "." if no digits follow the "." */
        if( flag_rtz && flag_dp ){
          while( bufpt[-1]=='0' ) *(--bufpt) = 0;
          assert( bufpt>buf );
          if( bufpt[-1]=='.' ){
            if( flag_altform2 ){
              *(bufpt++) = '0';
            }else{
              *(--bufpt) = 0;
            }
          }
        }
        /* Add the "eNNN" suffix */
        if( flag_exp || (xtype==etEXP && exp) ){
          *(bufpt++) = aDigits[infop->charset];
          if( exp<0 ){
            *(bufpt++) = '-'; exp = -exp;
          }else{
            *(bufpt++) = '+';
          }
          if( exp>=100 ){
            *(bufpt++) = (exp/100)+'0';                /* 100's digit */
            exp %= 100;
          }
          *(bufpt++) = exp/10+'0';                     /* 10's digit */
          *(bufpt++) = exp%10+'0';                     /* 1's digit */
        }
        *bufpt = 0;

        /* The converted number is in buf[] and zero terminated. Output it.
        ** Note that the number is in the usual order, not reversed as with
        ** integer conversions. */
        length = bufpt-buf;
        bufpt = buf;

        /* Special case:  Add leading zeros if the flag_zeropad flag is
        ** set and we are not left justified */
        if( flag_zeropad && !flag_leftjustify && length < width){
          int i;
          int nPad = width - length;
          for(i=width; i>=nPad; i--){
            bufpt[i] = bufpt[i-nPad];
          }
          i = prefix!=0;
          while( nPad-- ) bufpt[i++] = '0';
          length = width;
        }
        break;
      case etSIZE:
        *(va_arg(ap,int*)) = count;
        length = width = 0;
        break;
      case etPERCENT:
        buf[0] = '%';
        bufpt = buf;
        length = 1;
        break;
      case etCHARX:
        c = buf[0] = va_arg(ap,int);
        if( precision>=0 ){
          for(idx=1; idx<precision; idx++) buf[idx] = c;
          length = precision;
        }else{
          length =1;
        }
        bufpt = buf;
        break;
      case etPATH: {
        int i;
        int limit = flag_alternateform ? va_arg(ap,int) : -1;
        char *e = va_arg(ap,char*);
        if( e==0 ){e="";}
        length = StrNLen32(e, limit);
        zExtra = bufpt = fossil_malloc(length+1);
        for( i=0; i<length; i++ ){
          if( e[i]=='\\' ){
            bufpt[i]='/';
          }else{
            bufpt[i]=e[i];
          }
        }
        bufpt[length]='\0';
        break;
      }
      case etROOT: {
        bufpt = g.zTop ? g.zTop : "";
        length = (int)strlen(bufpt);
        break;
      }
      case etSTRINGID:
      case etSTRING:
      case etDYNSTRING: {
        int limit = flag_alternateform ? va_arg(ap,int) : -1;
        bufpt = va_arg(ap,char*);
        if( bufpt==0 ){
          bufpt = "";
        }else if( xtype==etDYNSTRING ){
          zExtra = bufpt;
        }else if( xtype==etSTRINGID ){
          precision = hashDigits(flag_altform2);
        }
        length = StrNLen32(bufpt, limit);
        if( precision>=0 && precision<length ) length = precision;
        break;
      }
      case etBLOB: {
        int limit = flag_alternateform ? va_arg(ap, int) : -1;
        Blob *pBlob = va_arg(ap, Blob*);
        bufpt = blob_buffer(pBlob);
        length = blob_size(pBlob);
        if( limit>=0 && limit<length ) length = limit;
        break;
      }
      case etBLOBSQL: {
        int limit = flag_alternateform ? va_arg(ap, int) : -1;
        Blob *pBlob = va_arg(ap, Blob*);
        char *zOrig = blob_buffer(pBlob);
        int i, j, n, cnt;
        n = blob_size(pBlob);
        if( limit>=0 && limit<n ) n = limit;
        for(cnt=i=0; i<n; i++){ if( zOrig[i]=='\'' ) cnt++; }
        if( n+cnt+2 > etBUFSIZE ){
          bufpt = zExtra = fossil_malloc( n + cnt + 2 );
        }else{
          bufpt = buf;
        }
        bufpt[0] = '\'';
        for(i=0, j=1; i<n; i++, j++){
          if( zOrig[i]=='\'' ){ bufpt[j++] = '\''; }
          bufpt[j] = zOrig[i];
        }
        bufpt[j++] = '\'';
        length = j;
        assert( length==n+cnt+2 );
        break;
      }
      case etSQLESCAPE:
      case etSQLESCAPE2:
      case etSQLESCAPE3: {
        int i, j, n, ch, isnull;
        int needQuote;
        int limit = flag_alternateform ? va_arg(ap,int) : -1;
        char q = ((xtype==etSQLESCAPE3)?'"':'\'');  /* Quote characters */
        char *escarg = va_arg(ap,char*);
        isnull = escarg==0;
        if( isnull ) escarg = (xtype==etSQLESCAPE2 ? "NULL" : "(NULL)");
        if( limit<0 ) limit = strlen(escarg);
        for(i=n=0; i<limit; i++){
          if( escarg[i]==q )  n++;
        }
        needQuote = !isnull && xtype==etSQLESCAPE2;
        n += i + 1 + needQuote*2;
        if( n>etBUFSIZE ){
          bufpt = zExtra = fossil_malloc( n );
        }else{
          bufpt = buf;
        }
        j = 0;
        if( needQuote ) bufpt[j++] = q;
        for(i=0; i<limit; i++){
          bufpt[j++] = ch = escarg[i];
          if( ch==q ) bufpt[j++] = ch;
        }
        if( needQuote ) bufpt[j++] = q;
        bufpt[j] = 0;
        length = j;
        if( precision>=0 && precision<length ) length = precision;
        break;
      }
      case etHTMLIZE: {
        int limit = flag_alternateform ? va_arg(ap,int) : -1;
        char *zMem = va_arg(ap,char*);
        if( zMem==0 ) zMem = "";
        zExtra = bufpt = htmlize(zMem, limit);
        length = strlen(bufpt);
        if( precision>=0 && precision<length ) length = precision;
        break;
      }
      case etHTTPIZE: {
        int limit = flag_alternateform ? va_arg(ap,int) : -1;
        char *zMem = va_arg(ap,char*);
        if( zMem==0 ) zMem = "";
        zExtra = bufpt = httpize(zMem, limit);
        length = strlen(bufpt);
        if( precision>=0 && precision<length ) length = precision;
        break;
      }
      case etURLIZE: {
        int limit = flag_alternateform ? va_arg(ap,int) : -1;
        char *zMem = va_arg(ap,char*);
        if( zMem==0 ) zMem = "";
        zExtra = bufpt = urlize(zMem, limit);
        length = strlen(bufpt);
        if( precision>=0 && precision<length ) length = precision;
        break;
      }
      case etFOSSILIZE: {
        int limit = flag_alternateform ? va_arg(ap,int) : -1;
        char *zMem = va_arg(ap,char*);
        if( zMem==0 ) zMem = "";
        zExtra = bufpt = fossilize(zMem, limit);
        length = strlen(bufpt);
        if( precision>=0 && precision<length ) length = precision;
        break;
      }
      case etWIKISTR: {
        int limit = flag_alternateform ? va_arg(ap,int) : -1;
        char *zWiki = va_arg(ap, char*);
        Blob wiki;
        blob_init(&wiki, zWiki, limit);
        wiki_convert(&wiki, pBlob, wiki_convert_flags(flag_altform2));
        blob_reset(&wiki);
        length = width = 0;
        break;
      }
      case etERROR:
        buf[0] = '%';
        buf[1] = c;
        errorflag = 0;
        idx = 1+(c!=0);
        blob_append(pBlob,"%",idx);
        count += idx;
        if( c==0 ) fmt--;
        break;
    }/* End switch over the format type */
    /*
    ** The text of the conversion is pointed to by "bufpt" and is
    ** "length" characters long.  The field width is "width".  Do
    ** the output.
    */
    if( !flag_leftjustify ){
      register int nspace;
      nspace = width-length;
      if( nspace>0 ){
        count += nspace;
        while( nspace>=etSPACESIZE ){
          blob_append(pBlob,spaces,etSPACESIZE);
          nspace -= etSPACESIZE;
        }
        if( nspace>0 ) blob_append(pBlob,spaces,nspace);
      }
    }
    if( length>0 ){
      blob_append(pBlob,bufpt,length);
      count += length;
    }
    if( flag_leftjustify ){
      register int nspace;
      nspace = width-length;
      if( nspace>0 ){
        count += nspace;
        while( nspace>=etSPACESIZE ){
          blob_append(pBlob,spaces,etSPACESIZE);
          nspace -= etSPACESIZE;
        }
        if( nspace>0 ) blob_append(pBlob,spaces,nspace);
      }
    }
    if( zExtra ){
      fossil_free(zExtra);
    }
  }/* End for loop over the format string */
  return errorflag ? -1 : count;
} /* End of function */

/*
** Print into memory obtained from fossil_malloc().
*/
char *mprintf(const char *zFormat, ...){
  va_list ap;
  char *z;
  va_start(ap,zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  return z;
}
char *vmprintf(const char *zFormat, va_list ap){
  Blob blob = empty_blob;
  blob_vappendf(&blob, zFormat, ap);
  blob_materialize(&blob);
  return blob.aData;
}

/*
** Record an error message in the global g.zErrMsg variable.
**
** If there is already another error message, only overwrite it if
** the current error has a higher priority.
*/
void fossil_error(int iPriority, const char *zFormat, ...){
  va_list ap;
  if( iPriority<=0 ){
    return;
  }
  if( g.zErrMsg ){
    if( g.iErrPriority>=iPriority ){
      return;
    }
    free(g.zErrMsg);
  }
  va_start(ap, zFormat);
  g.zErrMsg = vmprintf(zFormat, ap);
  va_end(ap);
  g.iErrPriority = iPriority;
}
void fossil_error_reset(void){
  free(g.zErrMsg);
  g.zErrMsg = 0;
  g.iErrPriority = 0;
}

/* True if the last character standard output cursor is setting at
** the beginning of a blank link.  False if a \r has been to move the
** cursor to the beginning of the line or if not at the beginning of
** a line.
** was a \n
*/
static int stdoutAtBOL = 1;

/*
** Write to standard output or standard error.
**
** On windows, transform the output into the current terminal encoding
** if the output is going to the screen.  If output is redirected into
** a file, no translation occurs. Switch output mode to binary to
** properly process line-endings, make sure to switch the mode back to
** text when done.
** No translation ever occurs on unix.
*/
void fossil_puts(const char *z, int toStdErr){
  FILE* out = (toStdErr ? stderr : stdout);
  int n = (int)strlen(z);
  if( n==0 ) return;
  assert( toStdErr==0 || toStdErr==1 );
  if( toStdErr==0 ) stdoutAtBOL = (z[n-1]=='\n');
#if defined(_WIN32)
  if( fossil_utf8_to_console(z, n, toStdErr) >= 0 ){
    return;
  }
  fflush(out);
  _setmode(_fileno(out), _O_BINARY);
#endif
  fwrite(z, 1, n, out);
#if defined(_WIN32)
  fflush(out);
  _setmode(_fileno(out), _O_TEXT);
#endif
}

/*
** Force the standard output cursor to move to the beginning
** of a line, if it is not there already.
*/
int fossil_force_newline(void){
  if( g.cgiOutput==0 && stdoutAtBOL==0 ){
    fossil_puts("\n", 0);
    return 1;
  }
  return 0;
}

/*
** Indicate that the cursor has moved to the start of a line by means
** other than writing to standard output.
*/
void fossil_new_line_started(void){
  stdoutAtBOL = 1;
}

/*
** Write output for user consumption.  If g.cgiOutput is enabled, then
** send the output as part of the CGI reply.  If g.cgiOutput is false,
** then write on standard output.
*/
void fossil_print(const char *zFormat, ...){
  va_list ap;
  va_start(ap, zFormat);
  if( g.cgiOutput ){
    cgi_vprintf(zFormat, ap);
  }else{
    Blob b = empty_blob;
    vxprintf(&b, zFormat, ap);
    fossil_puts(blob_str(&b), 0);
    blob_reset(&b);
  }
  va_end(ap);
}

/*
** Print a trace message on standard error.
*/
void fossil_trace(const char *zFormat, ...){
  va_list ap;
  Blob b;
  va_start(ap, zFormat);
  b = empty_blob;
  vxprintf(&b, zFormat, ap);
  fossil_puts(blob_str(&b), 1);
  blob_reset(&b);
  va_end(ap);
}

/*
** Write a message to the error log, if the error log filename is
** defined.
*/
static void fossil_errorlog(const char *zFormat, ...){
  struct tm *pNow;
  time_t now;
  FILE *out;
  const char *z;
  int i;
  va_list ap;
  static const char *const azEnv[] = { "HTTP_HOST", "HTTP_USER_AGENT",
      "PATH_INFO", "QUERY_STRING", "REMOTE_ADDR", "REQUEST_METHOD",
      "REQUEST_URI", "SCRIPT_NAME" };
  if( g.zErrlog==0 ) return;
  out = fossil_fopen(g.zErrlog, "a");
  if( out==0 ) return;
  now = time(0);
  pNow = gmtime(&now);
  fprintf(out, "------------- %04d-%02d-%02d %02d:%02d:%02d UTC ------------\n",
          pNow->tm_year+1900, pNow->tm_mon+1, pNow->tm_mday+1,
          pNow->tm_hour, pNow->tm_min, pNow->tm_sec);
  va_start(ap, zFormat);
  vfprintf(out, zFormat, ap);
  fprintf(out, "\n");
  va_end(ap);
  for(i=0; i<count(azEnv); i++){
    char *p;
    if( (p = fossil_getenv(azEnv[i]))!=0 ){
      fprintf(out, "%s=%s\n", azEnv[i], p);
      fossil_path_free(p);
    }else if( (z = P(azEnv[i]))!=0 ){
      fprintf(out, "%s=%s\n", azEnv[i], z);
    }
  }
  fclose(out);
}

/*
** The following variable becomes true while processing a fatal error
** or a panic.  If additional "recursive-fatal" errors occur while
** shutting down, the recursive errors are silently ignored.
*/
static int mainInFatalError = 0;

/*
** Print an error message, rollback all databases, and quit.  These
** routines never return.
*/
NORETURN void fossil_panic(const char *zFormat, ...){
  va_list ap;
  int rc = 1;
  char z[1000];
  static int once = 0;

  if( once ) exit(1);
  once = 1;
  mainInFatalError = 1;
  db_force_rollback();
  va_start(ap, zFormat);
  sqlite3_vsnprintf(sizeof(z),z,zFormat, ap);
  va_end(ap);
  fossil_errorlog("panic: %s", z);
#ifdef FOSSIL_ENABLE_JSON
  if( g.json.isJsonMode ){
    json_err( 0, z, 1 );
    if( g.isHTTP ){
      rc = 0 /* avoid HTTP 500 */;
    }
  }
  else
#endif
  {
    if( g.cgiOutput ){
      cgi_printf("<p class=\"generalError\">%h</p>", z);
      cgi_reply();
    }else if( !g.fQuiet ){
      fossil_force_newline();
      fossil_puts("Fossil internal error: ", 1);
      fossil_puts(z, 1);
      fossil_puts("\n", 1);
    }
  }
  exit(rc);
}

NORETURN void fossil_fatal(const char *zFormat, ...){
  char *z;
  int rc = 1;
  va_list ap;
  mainInFatalError = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  fossil_errorlog("fatal: %s", z);
#ifdef FOSSIL_ENABLE_JSON
  if( g.json.isJsonMode ){
    json_err( g.json.resultCode, z, 1 );
    if( g.isHTTP ){
      rc = 0 /* avoid HTTP 500 */;
    }
  }
  else
#endif
  {
    if( g.cgiOutput ){
      g.cgiOutput = 0;
      cgi_printf("<p class=\"generalError\">\n%h\n</p>\n", z);
      cgi_reply();
    }else if( !g.fQuiet ){
      fossil_force_newline();
      fossil_trace("%s\n", z);
    }
  }
  free(z);
  db_force_rollback();
  fossil_exit(rc);
}

/* This routine works like fossil_fatal() except that if called
** recursively, the recursive call is a no-op.
**
** Use this in places where an error might occur while doing
** fatal error shutdown processing.  Unlike fossil_panic() and
** fossil_fatal() which never return, this routine might return if
** the fatal error handing is already in process.  The caller must
** be prepared for this routine to return.
*/
void fossil_fatal_recursive(const char *zFormat, ...){
  char *z;
  va_list ap;
  int rc = 1;
  if( mainInFatalError ) return;
  mainInFatalError = 1;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  fossil_errorlog("fatal: %s", z);
#ifdef FOSSIL_ENABLE_JSON
  if( g.json.isJsonMode ){
    json_err( g.json.resultCode, z, 1 );
    if( g.isHTTP ){
      rc = 0 /* avoid HTTP 500 */;
    }
  } else
#endif
  {
    if( g.cgiOutput ){
      g.cgiOutput = 0;
      cgi_printf("<p class=\"generalError\">\n%h\n</p>\n", z);
      cgi_reply();
    }else{
      fossil_force_newline();
      fossil_trace("%s\n", z);
    }
  }
  db_force_rollback();
  fossil_exit(rc);
}


/* Print a warning message */
void fossil_warning(const char *zFormat, ...){
  char *z;
  va_list ap;
  va_start(ap, zFormat);
  z = vmprintf(zFormat, ap);
  va_end(ap);
  fossil_errorlog("warning: %s", z);
#ifdef FOSSIL_ENABLE_JSON
  if(g.json.isJsonMode){
    json_warn( FSL_JSON_W_UNKNOWN, z );
  }else
#endif
  {
    if( g.cgiOutput ){
      cgi_printf("<p class=\"generalError\">\n%h\n</p>\n", z);
    }else{
      fossil_force_newline();
      fossil_trace("%s\n", z);
    }
  }
  free(z);
}

/*
** Turn off any LF to CRLF translation on the stream given as an
** argument.  This is a no-op on unix but is necessary on windows.
*/
void fossil_binary_mode(FILE *p){
#if defined(_WIN32)
  _setmode(_fileno(p), _O_BINARY);
#endif
#ifdef __EMX__     /* OS/2 */
  setmode(fileno(p), O_BINARY);
#endif
}
