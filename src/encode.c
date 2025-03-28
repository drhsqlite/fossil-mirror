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
** Routines for encoding and decoding text.
*/
#include "config.h"
#include "encode.h"

/*
** Make the given string safe for HTML by converting every "<" into "&lt;",
** every ">" into "&gt;" and every "&" into "&amp;".  Return a pointer
** to a new string obtained from malloc().
**
** We also encode " as &quot; and ' as &#39; so they can appear as an argument
** to markup.
*/
char *htmlize(const char *z, int n){
  unsigned char c;
  int i = 0;
  int count = 0;
  unsigned char *zOut;
  const unsigned char *zIn = (const unsigned char*)z;

  if( n<0 ) n = strlen(z);
  while( i<n ){
    switch( zIn[i] ){
      case '<':   count += 3;       break;
      case '>':   count += 3;       break;
      case '&':   count += 4;       break;
      case '"':   count += 5;       break;
      case '\'':  count += 4;       break;
      case 0:     n = i;            break;
    }
    i++;
  }
  i = 0;
  zOut = fossil_malloc( count+n+1 );
  if( count==0 ){
    memcpy(zOut, zIn, n);
    zOut[n] = 0;
    return (char*)zOut;
  }
  while( n-->0 ){
    c = *(zIn++);
    switch( c ){
      case '<':
        zOut[i++] = '&';
        zOut[i++] = 'l';
        zOut[i++] = 't';
        zOut[i++] = ';';
        break;
      case '>':
        zOut[i++] = '&';
        zOut[i++] = 'g';
        zOut[i++] = 't';
        zOut[i++] = ';';
        break;
      case '&':
        zOut[i++] = '&';
        zOut[i++] = 'a';
        zOut[i++] = 'm';
        zOut[i++] = 'p';
        zOut[i++] = ';';
        break;
      case '"':
        zOut[i++] = '&';
        zOut[i++] = 'q';
        zOut[i++] = 'u';
        zOut[i++] = 'o';
        zOut[i++] = 't';
        zOut[i++] = ';';
        break;
      case '\'':
        zOut[i++] = '&';
        zOut[i++] = '#';
        zOut[i++] = '3';
        zOut[i++] = '9';
        zOut[i++] = ';';
        break;
      default:
        zOut[i++] = c;
        break;
    }
  }
  zOut[i] = 0;
  return (char*)zOut;
}

/*
** Append HTML-escaped text to a Blob.
*/
void htmlize_to_blob(Blob *p, const char *zIn, int n){
  int c, i, j;
  if( n<0 ) n = strlen(zIn);
  for(i=j=0; i<n; i++){
    c = zIn[i];
    switch( c ){
      case '<':
        if( j<i ) blob_append(p, zIn+j, i-j);
        blob_append(p, "&lt;", 4);
        j = i+1;
        break;
      case '>':
        if( j<i ) blob_append(p, zIn+j, i-j);
        blob_append(p, "&gt;", 4);
        j = i+1;
        break;
      case '&':
        if( j<i ) blob_append(p, zIn+j, i-j);
        blob_append(p, "&amp;", 5);
        j = i+1;
        break;
      case '"':
        if( j<i ) blob_append(p, zIn+j, i-j);
        blob_append(p, "&quot;", 6);
        j = i+1;
        break;
      case '\'':
        if( j<i ) blob_append(p, zIn+j, i-j);
        blob_append(p, "&#39;", 5);
        j = i+1;
        break;
      case '\r':
        if( j<i ) blob_append(p, zIn+j, i-j);
        blob_append(p, " ", 1);
        j = i+1;
        break;
    }
  }
  if( j<i ) blob_append(p, zIn+j, i-j);
}


/*
** Encode a string for HTTP.  This means converting lots of
** characters into the "%HH" where H is a hex digit.  It also
** means converting spaces to "+".
**
** This is the opposite of DeHttpizeString below.
*/
static char *EncodeHttp(const char *zIn, int n, int encodeSlash){
  int c;
  int i = 0;
  int count = 0;
  char *zOut;
# define IsSafeChar(X)  \
     (fossil_isalnum(X) || (X)=='.' || (X)=='$' \
      || (X)=='~' || (X)=='-' || (X)=='_' \
      || (!encodeSlash && ((X)=='/' || (X)==':')))

  if( zIn==0 ) return 0;
  if( n<0 ) n = strlen(zIn);
  while( i<n && (c = zIn[i])!=0 ){
    if( IsSafeChar(c) || c==' ' ){
      count++;
    }else{
      count += 3;
    }
    i++;
  }
  i = 0;
  zOut = fossil_malloc( count+1 );
  while( n-->0 && (c = *zIn)!=0 ){
    if( IsSafeChar(c) ){
      zOut[i++] = c;
    }else if( c==' ' ){
      zOut[i++] = '+';
    }else{
      zOut[i++] = '%';
      zOut[i++] = "0123456789ABCDEF"[(c>>4)&0xf];
      zOut[i++] = "0123456789ABCDEF"[c&0xf];
    }
    zIn++;
  }
  zOut[i] = 0;
#undef IsSafeChar
  return zOut;
}

/*
** Convert the input string into a form that is suitable for use as
** a token in the HTTP protocol.  Spaces are encoded as '+' and special
** characters are encoded as "%HH" where HH is a two-digit hexadecimal
** representation of the character.  The "/" character is encoded
** as "%2F".
*/
char *httpize(const char *z, int n){
  return EncodeHttp(z, n, 1);
}

/*
** Convert the input string into a form that is suitable for use as
** a token in the HTTP protocol.  Spaces are encoded as '+' and special
** characters are encoded as "%HH" where HH is a two-digit hexidecimal
** representation of the character.  The "/" character is not encoded
** by this routine.
*/
char *urlize(const char *z, int n){
  return EncodeHttp(z, n, 0);
}

/*
** If input string does not contain quotes (neither ' nor ")
** then return the argument itself. Otherwise return a newly allocated
** copy of input with all quotes %-escaped.
*/
const char* escape_quotes(const char *zIn){
  char *zRet, *zOut;
  size_t i, n = 0;
  for(i=0; zIn[i]; i++){
    if( zIn[i]== '"' || zIn[i]== '\'' ) n++;
  }
  if( !n ) return zIn;
  zRet = zOut = fossil_malloc( i + 2*n + 1 );
  for(i=0; zIn[i]; i++){
    if( zIn[i]=='"' ){
      *(zOut++) = '%';
      *(zOut++) = '2';
      *(zOut++) = '2';
    }else if( zIn[i]=='\'' ){
      *(zOut++) = '%';
      *(zOut++) = '2';
      *(zOut++) = '7';
    }else{
      *(zOut++) = zIn[i];
    }
  }
  *zOut = 0;
  return zRet;
}

/*
** Convert a single HEX digit to an integer
*/
int fossil_hexvalue(int c){
  if( c>='a' && c<='f' ){
    c += 10 - 'a';
  }else if( c>='A' && c<='F' ){
    c += 10 - 'A';
  }else if( c>='0' && c<='9' ){
    c -= '0';
  }else{
    c = 0;
  }
  return c;
}

/*
** Remove the HTTP encodings from a string.  The conversion is done
** in-place.  Return the length of the string after conversion.
*/
int dehttpize(char *z){
  int i, j;

  /* Treat a null pointer as a zero-length string. */
  if( !z ) return 0;

  i = j = 0;
  while( z[i] ){
    switch( z[i] ){
      case '%':
        if( z[i+1] && z[i+2] ){
          z[j] = fossil_hexvalue(z[i+1]) << 4;
          z[j] |= fossil_hexvalue(z[i+2]);
          i += 2;
        }
        break;
      case '+':
        z[j] = ' ';
        break;
      default:
        z[j] = z[i];
        break;
    }
    i++;
    j++;
  }
  z[j] = 0;
  return j;
}

/*
** The "fossilize" encoding is used in the headers of records
** (aka "content files") to escape special characters.  The
** fossilize encoding passes most characters through unchanged.
** The changes are these:
**
**        space    ->   \s
**        tab      ->   \t
**        newline  ->   \n
**        cr       ->   \r
**        formfeed ->   \f
**        vtab     ->   \v
**        nul      ->   \0
**        \        ->   \\
**
** The fossilize() routine does an encoding of its input and
** returns a pointer to the encoding in space obtained from
** malloc.
*/
char *fossilize(const char *zIn, int nIn){
  int n, i, j, c;
  char *zOut;
  if( nIn<0 ) nIn = strlen(zIn);
  for(i=n=0; i<nIn; i++){
    c = zIn[i];
    if( c==0 || c==' ' || c=='\n' || c=='\t' || c=='\r' || c=='\f' || c=='\v'
             || c=='\\' ) n++;
  }
  n += nIn;
  zOut = fossil_malloc( n+1 );
  if( zOut ){
    for(i=j=0; i<nIn; i++){
      int c = zIn[i];
      if( c==0 ){
        zOut[j++] = '\\';
        zOut[j++] = '0';
      }else if( c=='\\' ){
        zOut[j++] = '\\';
        zOut[j++] = '\\';
      }else if( fossil_isspace(c) ){
        zOut[j++] = '\\';
        switch( c ){
          case '\n':  c = 'n'; break;
          case ' ':   c = 's'; break;
          case '\t':  c = 't'; break;
          case '\r':  c = 'r'; break;
          case '\v':  c = 'v'; break;
          case '\f':  c = 'f'; break;
        }
        zOut[j++] = c;
      }else{
        zOut[j++] = c;
      }
    }
    zOut[j] = 0;
  }
  return zOut;
}

/*
** Decode a fossilized string in-place.
*/
void defossilize(char *z){
  int i, j, c;
  char *zSlash = strchr(z, '\\');
  if( zSlash==0 ) return;
  i = zSlash - z;
  for(j=i; (c=z[i])!=0; i++){
    if( c=='\\' && z[i+1] ){
      i++;
      switch( z[i] ){
        case 'n':  c = '\n';  break;
        case 's':  c = ' ';   break;
        case 't':  c = '\t';  break;
        case 'r':  c = '\r';  break;
        case 'v':  c = '\v';  break;
        case 'f':  c = '\f';  break;
        case '0':  c = 0;     break;
        case '\\': c = '\\';  break;
        default:   c = z[i];  break;
      }
    }
    z[j++] = c;
  }
  if( z[j] ) z[j] = 0;
}


/*
** The *pz variable points to a UTF8 string.  Read the next character
** off of that string and return its codepoint value.  Advance *pz to the
** next character
*/
u32 fossil_utf8_read(
  const unsigned char **pz    /* Pointer to string from which to read char */
){
  unsigned int c;

  /*
  ** This lookup table is used to help decode the first byte of
  ** a multi-byte UTF8 character.
  */
  static const unsigned char utf8Trans1[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00,
  };

  c = *((*pz)++);
  if( c>=0xc0 ){
    c = utf8Trans1[c-0xc0];
    while( (*(*pz) & 0xc0)==0x80 ){
      c = (c<<6) + (0x3f & *((*pz)++));
    }
    if( c<0x80
        || (c&0xFFFFF800)==0xD800
        || (c&0xFFFFFFFE)==0xFFFE ){  c = 0xFFFD; }
  }
  return c;
}

/*
** Encode a UTF8 string as a JSON string literal (with or without the
** surrounding "...", depending on whether the 2nd argument is true or
** false) and return a pointer to the encoding.  Space to hold the
** encoding is obtained from fossil_malloc() and must be freed by the
** caller.
**
** If nOut is not NULL then it is assigned to the length, in bytes, of
** the returned string (its strlen(), not counting the terminating
** NUL).
*/
char *encode_json_string_literal(const char *zStr, int fAddQuotes,
                                 int * nOut){
  const unsigned char *z;
  char *zOut;
  u32 c;
  int n, i, j;
  z = (const unsigned char*)zStr;
  n = 0;
  while( (c = *(z++))!=0 ){
    if( c=='\\' || c=='"' ){
      n += 2;
    }else if( c<' ' ){
      if( c=='\n' || c=='\r' ){
        n += 2;
      }else{
        n += 6;
      }
    }else{
      n++;
    }
  }
  if(fAddQuotes){
    n += 2;
  }
  zOut = fossil_malloc(n+1);
  if( zOut==0 ) return 0;
  z = (const unsigned char*)zStr;
  i = 0;
  if(fAddQuotes){
    zOut[i++] = '"';
  }
  while( (c = *(z++))!=0 ){
    if( c=='\\' || c=='"' ){
      zOut[i++] = '\\';
      zOut[i++] = c;
    }else if( c<' ' ){
      zOut[i++] = '\\';
      if( c=='\n' ){
        zOut[i++] = 'n';
      }else if( c=='\r' ){
        zOut[i++] = 'r';
      }else{
        zOut[i++] = 'u';
        for(j=3; j>=0; j--){
          zOut[i+j] = "0123456789abcdef"[c&0xf];
          c >>= 4;
        }
        i += 4;
      }
    }else{
      zOut[i++] = c;
    }
  }
  if(fAddQuotes){
    zOut[i++] = '"';
  }
  zOut[i] = 0;
  if(nOut!=0){
    *nOut = i;
  }
  return zOut;
}

/*
** The characters used for HTTP base64 encoding.
*/
static unsigned char zBase[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
** Translate nData bytes of content from zData into
** ((nData+2)/3)*4) bytes of base64 encoded content and
** put the result in z64.  Add a zero-terminator at the end.
*/
int translateBase64(const char *zData, int nData, char *z64){
  int i, n;
  for(i=n=0; i+2<nData; i+=3){
    z64[n++] = zBase[ (zData[i]>>2) & 0x3f ];
    z64[n++] = zBase[ ((zData[i]<<4) & 0x30) | ((zData[i+1]>>4) & 0x0f) ];
    z64[n++] = zBase[ ((zData[i+1]<<2) & 0x3c) | ((zData[i+2]>>6) & 0x03) ];
    z64[n++] = zBase[ zData[i+2] & 0x3f ];
  }
  if( i+1<nData ){
    z64[n++] = zBase[ (zData[i]>>2) & 0x3f ];
    z64[n++] = zBase[ ((zData[i]<<4) & 0x30) | ((zData[i+1]>>4) & 0x0f) ];
    z64[n++] = zBase[ ((zData[i+1]<<2) & 0x3c) ];
    z64[n++] = '=';
  }else if( i<nData ){
    z64[n++] = zBase[ (zData[i]>>2) & 0x3f ];
    z64[n++] = zBase[ ((zData[i]<<4) & 0x30) ];
    z64[n++] = '=';
    z64[n++] = '=';
  }
  z64[n] = 0;
  return n;
}

/*
** Encode a string using a base-64 encoding.
** The encoding can be reversed using the <b>decode64</b> function.
**
** Space to hold the result comes from malloc().
*/
char *encode64(const char *zData, int nData){
  char *z64;
  if( nData<=0 ){
    nData = strlen(zData);
  }
  z64 = fossil_malloc( (nData*4)/3 + 8 );
  translateBase64(zData, nData, z64);
  return z64;
}

/*
** COMMAND: test-encode64
**
** Usage: %fossil test-encode64 STRING
*/
void test_encode64_cmd(void){
  char *z;
  int i;
  for(i=2; i<g.argc; i++){
    z = encode64(g.argv[i], -1);
    fossil_print("%s\n", z);
    free(z);
  }
}


/* Decode base64 text.  Write the output into zData.  The caller
** must ensure that zData is large enough.  It is ok for z64 and
** zData to be the same buffer.  In other words, it is ok to decode
** in-place.  A zero terminator is always placed at the end of zData.
*/
void decodeBase64(const char *z64, int *pnByte, char *zData){
  const unsigned char *zIn = (const unsigned char*)z64;
  int i, j, k;
  int x[4];
  static int isInit = 0;
  static signed char trans[256];

  if( !isInit ){
    for(i=0; i<256; i++){ trans[i] = -1; }
    for(i=0; zBase[i]; i++){ trans[zBase[i] & 0x7f] = i; }
    isInit = 1;
  }
  for(j=k=0; zIn[0]; zIn++){
    int v = trans[zIn[0]];
    if( v>=0 ){
      x[k++] = v;
      if( k==4 ){
        zData[j++] = ((x[0]<<2) & 0xfc) | ((x[1]>>4) & 0x03);
        zData[j++] = ((x[1]<<4) & 0xf0) | ((x[2]>>2) & 0x0f);
        zData[j++] = ((x[2]<<6) & 0xc0) | (x[3] & 0x3f);
        k = 0;
      }
    }
  }
  if( k>=2 ){
    zData[j++] = ((x[0]<<2) & 0xfc) | ((x[1]>>4) & 0x03);
    if( k==3 ){
      zData[j++] = ((x[1]<<4) & 0xf0) | ((x[2]>>2) & 0x0f);
    }
  }
  zData[j] = 0;
  *pnByte = j;
}


/*
** This function treats its input as a base-64 string and returns the
** decoded value of that string.  Characters of input that are not
** valid base-64 characters (such as spaces and newlines) are ignored.
**
** Space to hold the decoded string is obtained from malloc().
**
** The number of bytes decoded is returned in *pnByte
*/
char *decode64(const char *z64, int *pnByte){
  char *zData;
  int n64 = (int)strlen(z64);
  while( n64>0 && z64[n64-1]=='=' ) n64--;
  zData = fossil_malloc( (n64*3)/4 + 4 );
  decodeBase64(z64, pnByte, zData);
  return zData;
}

/*
** COMMAND: test-decode64
**
** Usage: %fossil test-decode64 STRING
*/
void test_decode64_cmd(void){
  char *z;
  int i, n;
  for(i=2; i<g.argc; i++){
    z = decode64(g.argv[i], &n);
    fossil_print("%d: %s\n", n, z);
    fossil_free(z);
  }
}

/*
** The base-16 encoding using the following characters:
**
**         0123456789abcdef
**
*/

/*
** The array used for encoding
*/                           /* 123456789 12345  */
static const char zEncode[] = "0123456789abcdef";

/*
** Encode a N-digit base-256 in base-16.  Return zero on success
** and non-zero if there is an error.
*/
int encode16(const unsigned char *pIn, unsigned char *zOut, int N){
  int i;
  for(i=0; i<N; i++){
    *(zOut++) = zEncode[pIn[i]>>4];
    *(zOut++) = zEncode[pIn[i]&0xf];
  }
  *zOut = 0;
  return 0;
}

/*
** An array for translating single base-16 characters into a value.
** Disallowed input characters have a value of 64.  Upper and lower
** case is the same.
*/
static const char zDecode[] = {
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
   0,  1,  2,  3,  4,  5,  6,  7,   8,  9, 64, 64, 64, 64, 64, 64,
  64, 10, 11, 12, 13, 14, 15, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 10, 11, 12, 13, 14, 15, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64,
};

/*
** Decode a N-character base-16 number into base-256.  N must be a
** multiple of 2.  The output buffer must be at least N/2 characters
** in length
*/
int decode16(const unsigned char *zIn, unsigned char *pOut, int N){
  int i, j;
  if( (N&1)!=0 ) return 1;
  for(i=j=0; i<N; i += 2, j++){
    int v1, v2, a;
    a = zIn[i];
    if( (a & 0x80)!=0 || (v1 = zDecode[a])==64 ) return 1;
    a = zIn[i+1];
    if( (a & 0x80)!=0 || (v2 = zDecode[a])==64 ) return 1;
    pOut[j] = (v1<<4) + v2;
  }
  return 0;
}


/*
** Return true if the input string contains only valid base-16 digits.
** If any invalid characters appear in the string, return false.
*/
int validate16(const char *zIn, int nIn){
  int i;
  if( nIn<0 ) nIn = (int)strlen(zIn);
  if( zIn[nIn]==0 ){
    return (int)strspn(zIn,"0123456789abcdefABCDEF")==nIn;
  }
  for(i=0; i<nIn; i++, zIn++){
    if( zDecode[zIn[0]&0xff]>63 ){
      return zIn[0]==0;
    }
  }
  return 1;
}

/*
** The input string is a base16 value.  Convert it into its canonical
** form.  This means that digits are all lower case and that conversions
** like "l"->"1" and "O"->"0" occur.
*/
void canonical16(char *z, int n){
  while( *z && n-- ){
    *z = zEncode[zDecode[(*z)&0x7f]&0x1f];
    z++;
  }
}

/*
** Decode hexadecimal into a string and return the new string.  Space to
** hold the string is obtained from fossil_malloc() and should be released
** by the caller.
**
** If the input is not hex, return NULL.
*/
char *decode16_dup(const char *zIn){
  int nIn = (int)strlen(zIn);
  char *zOut;
  if( !validate16(zIn, nIn) ) return 0;
  zOut = fossil_malloc(nIn/2+1);
  decode16((const u8*)zIn, (u8*)zOut, nIn);
  zOut[nIn/2] = 0;
  return zOut;
}


/*
** Decode a string encoded using "quoted-printable".
**
**   (1)  "=" followed by two hex digits becomes a single
**        byte specified by the two digits
**
** The decoding is done in-place.
*/
void decodeQuotedPrintable(char *z, int *pnByte){
  int i, j, c;
  for(i=j=0; (c = z[i])!=0; i++){
    if( c=='=' ){
      if( z[i+1]!='\r' ){
        decode16((unsigned char*)&z[i+1], (unsigned char*)&z[j], 2);
        j++;
      }
      i += 2;
    }else{
      z[j++] = c;
    }
  }
  if( pnByte ) *pnByte = j;
  z[j] = 0;
}

/* Randomness used for XOR-ing by the obscure() and unobscure() routines */
static const unsigned char aObscurer[16] = {
    0xa7, 0x21, 0x31, 0xe3, 0x2a, 0x50, 0x2c, 0x86,
    0x4c, 0xa4, 0x52, 0x25, 0xff, 0x49, 0x35, 0x85
};


/*
** Obscure plain text so that it is not easily readable.
**
** This is used for storing sensitive information (such as passwords) in a
** way that prevents their exposure through idle browsing.  This is not
** encryption.  Anybody who really wants the password can still get it.
**
** The text is XOR-ed with a repeating pattern then converted to hex.
** Space to hold the returned string is obtained from malloc and should
** be freed by the caller.
*/
char *obscure(const char *zIn){
  int n, i;
  unsigned char salt;
  char *zOut;

  if( zIn==0 ) return 0;
  n = strlen(zIn);
  zOut = fossil_malloc( n*2+3 );
  sqlite3_randomness(1, &salt);
  zOut[n+1] = (char)salt;
  for(i=0; i<n; i++) zOut[i+n+2] = zIn[i]^aObscurer[i&0x0f]^salt;
  encode16((unsigned char*)&zOut[n+1], (unsigned char*)zOut, n+1);
  return zOut;
}

/*
** Undo the obscuring of text performed by obscure().  Or, if the input is
** not hexadecimal (meaning the input is not the output of obscure()) then
** do the equivalent of strdup().
**
** The result is memory obtained from malloc that should be freed by the caller.
*/
char *unobscure(const char *zIn){
  int n, i;
  unsigned char salt;
  char *zOut;

  if( zIn==0 ) return 0;
  n = strlen(zIn);
  zOut = fossil_malloc( n + 1 );
  if( n<2
    || decode16((unsigned char*)zIn, &salt, 2)
    || decode16((unsigned char*)&zIn[2], (unsigned char*)zOut, n-2)
  ){
    memcpy(zOut, zIn, n+1);
  }else{
    n = n/2 - 1;
    for(i=0; i<n; i++) zOut[i] = zOut[i]^aObscurer[i&0x0f]^salt;
    zOut[n] = 0;
  }
  return zOut;
}

/*
** Command to test obscure() and unobscure().  These commands are also useful
** utilities for decoding passwords found in the database.
**
** COMMAND: test-obscure
**
** For each command-line argument X, run both obscure(X) and
** unobscure(obscure(X)) and print the results.  This is used for testing
** and debugging of the obscure() and unobscure() functions.
*/
void test_obscure_cmd(void){
  int i;
  char *z, *z2;
  for(i=2; i<g.argc; i++){
    z = obscure(g.argv[i]);
    z2 = unobscure(z);
    fossil_print("OBSCURE:    %s -> %s (%s)\n", g.argv[i], z, z2);
    free(z);
    free(z2);
    z = unobscure(g.argv[i]);
    fossil_print("UNOBSCURE:  %s -> %s\n", g.argv[i], z);
    free(z);
  }
}
