/*
** Copyright (c) 2006 D. Richard Hipp
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
** A Blob is a variable-length containers for arbitrary string
** or binary data.
*/
#include "config.h"
#if defined(FOSSIL_ENABLE_MINIZ)
#  define MINIZ_HEADER_FILE_ONLY
#  include "miniz.c"
#else
#  include <zlib.h>
#endif
#include "blob.h"
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#if INTERFACE
/*
** A Blob can hold a string or a binary object of arbitrary size.  The
** size changes as necessary.
*/
struct Blob {
  unsigned int nUsed;            /* Number of bytes used in aData[] */
  unsigned int nAlloc;           /* Number of bytes allocated for aData[] */
  unsigned int iCursor;          /* Next character of input to parse */
  unsigned int blobFlags;        /* One or more BLOBFLAG_* bits */
  char *aData;                   /* Where the information is stored */
  void (*xRealloc)(Blob*, unsigned int); /* Function to reallocate the buffer */
};

/*
** Allowed values for Blob.blobFlags
*/
#define BLOBFLAG_NotSQL  0x0001      /* Non-SQL text */

/*
** The current size of a Blob
*/
#define blob_size(X)  ((X)->nUsed)

/*
** The buffer holding the blob data
*/
#define blob_buffer(X)  ((X)->aData)

/*
** Seek whence parameter values
*/
#define BLOB_SEEK_SET 1
#define BLOB_SEEK_CUR 2
#define BLOB_SEEK_END 3

#endif /* INTERFACE */

/*
** Make sure a blob is initialized
*/
#define blob_is_init(x) \
  assert((x)->xRealloc==blobReallocMalloc || (x)->xRealloc==blobReallocStatic)

/*
** Make sure a blob does not contain malloced memory.
**
** This might fail if we are unlucky and x is uninitialized.  For that
** reason it should only be used locally for debugging.  Leave it turned
** off for production.
*/
#if 0  /* Enable for debugging only */
#define assert_blob_is_reset(x) assert(blob_is_reset(x))
#else
#define assert_blob_is_reset(x)
#endif



/*
** We find that the built-in isspace() function does not work for
** some international character sets.  So here is a substitute.
*/
int fossil_isspace(char c){
  return c==' ' || (c<='\r' && c>='\t');
}

/*
** Other replacements for ctype.h functions.
*/
int fossil_islower(char c){ return c>='a' && c<='z'; }
int fossil_isupper(char c){ return c>='A' && c<='Z'; }
int fossil_isdigit(char c){ return c>='0' && c<='9'; }
int fossil_tolower(char c){
  return fossil_isupper(c) ? c - 'A' + 'a' : c;
}
int fossil_toupper(char c){
  return fossil_islower(c) ? c - 'a' + 'A' : c;
}
int fossil_isalpha(char c){
  return (c>='a' && c<='z') || (c>='A' && c<='Z');
}
int fossil_isalnum(char c){
  return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');
}


/*
** COMMAND: test-isspace
**
** Verify that the fossil_isspace() routine is working correctly by
** testing it on all possible inputs.
*/
void isspace_cmd(void){
  int i;
  for(i=0; i<=255; i++){
    if( i==' ' || i=='\n' || i=='\t' || i=='\v'
        || i=='\f' || i=='\r' ){
      assert( fossil_isspace((char)i) );
    }else{
      assert( !fossil_isspace((char)i) );
    }
  }
  fossil_print("All 256 characters OK\n");
}

/*
** This routine is called if a blob operation fails because we
** have run out of memory.
*/
static void blob_panic(void){
  static const char zErrMsg[] = "out of memory\n";
  fputs(zErrMsg, stderr);
  fossil_exit(1);
}

/*
** A reallocation function that assumes that aData came from malloc().
** This function attempts to resize the buffer of the blob to hold
** newSize bytes.
**
** No attempt is made to recover from an out-of-memory error.
** If an OOM error occurs, an error message is printed on stderr
** and the program exits.
*/
void blobReallocMalloc(Blob *pBlob, unsigned int newSize){
  if( newSize==0 ){
    free(pBlob->aData);
    pBlob->aData = 0;
    pBlob->nAlloc = 0;
    pBlob->nUsed = 0;
    pBlob->iCursor = 0;
    pBlob->blobFlags = 0;
  }else if( newSize>pBlob->nAlloc || newSize<pBlob->nAlloc-4000 ){
    char *pNew = fossil_realloc(pBlob->aData, newSize);
    pBlob->aData = pNew;
    pBlob->nAlloc = newSize;
    if( pBlob->nUsed>pBlob->nAlloc ){
      pBlob->nUsed = pBlob->nAlloc;
    }
  }
}

/*
** An initializer for Blobs
*/
#if INTERFACE
#define BLOB_INITIALIZER  {0,0,0,0,0,blobReallocMalloc}
#endif
const Blob empty_blob = BLOB_INITIALIZER;

/*
** A reallocation function for when the initial string is in unmanaged
** space.  Copy the string to memory obtained from malloc().
*/
static void blobReallocStatic(Blob *pBlob, unsigned int newSize){
  if( newSize==0 ){
    *pBlob = empty_blob;
  }else{
    char *pNew = fossil_malloc( newSize );
    if( pBlob->nUsed>newSize ) pBlob->nUsed = newSize;
    memcpy(pNew, pBlob->aData, pBlob->nUsed);
    pBlob->aData = pNew;
    pBlob->xRealloc = blobReallocMalloc;
    pBlob->nAlloc = newSize;
  }
}

/*
** Reset a blob to be an empty container.
*/
void blob_reset(Blob *pBlob){
  blob_is_init(pBlob);
  pBlob->xRealloc(pBlob, 0);
}


/*
** Return true if the blob has been zeroed - in other words if it contains
** no malloced memory.  This only works reliably if the blob has been
** initialized - it can return a false negative on an uninitialized blob.
*/
int blob_is_reset(Blob *pBlob){
  if( pBlob==0 ) return 1;
  if( pBlob->nUsed ) return 0;
  if( pBlob->xRealloc==blobReallocMalloc && pBlob->nAlloc ) return 0;
  return 1;
}

/*
** Initialize a blob to a string or byte-array constant of a specified length.
** Any prior data in the blob is discarded.
*/
void blob_init(Blob *pBlob, const char *zData, int size){
  assert_blob_is_reset(pBlob);
  if( zData==0 ){
    *pBlob = empty_blob;
  }else{
    if( size<=0 ) size = strlen(zData);
    pBlob->nUsed = pBlob->nAlloc = size;
    pBlob->aData = (char*)zData;
    pBlob->iCursor = 0;
    pBlob->blobFlags = 0;
    pBlob->xRealloc = blobReallocStatic;
  }
}

/*
** Initialize a blob to a nul-terminated string.
** Any prior data in the blob is discarded.
*/
void blob_set(Blob *pBlob, const char *zStr){
  blob_init(pBlob, zStr, -1);
}

/*
** Initialize a blob to a nul-terminated string obtained from fossil_malloc().
** The blob will take responsibility for freeing the string.
*/
void blob_set_dynamic(Blob *pBlob, char *zStr){
  blob_init(pBlob, zStr, -1);
  pBlob->xRealloc = blobReallocMalloc;
}

/*
** Initialize a blob to an empty string.
*/
void blob_zero(Blob *pBlob){
  static const char zEmpty[] = "";
  assert_blob_is_reset(pBlob);
  pBlob->nUsed = 0;
  pBlob->nAlloc = 1;
  pBlob->aData = (char*)zEmpty;
  pBlob->iCursor = 0;
  pBlob->blobFlags = 0;
  pBlob->xRealloc = blobReallocStatic;
}

/*
** Append text or data to the end of a blob.
*/
void blob_append(Blob *pBlob, const char *aData, int nData){
  assert( aData!=0 || nData==0 );
  blob_is_init(pBlob);
  if( nData<0 ) nData = strlen(aData);
  if( nData==0 ) return;
  if( pBlob->nUsed + nData >= pBlob->nAlloc ){
    pBlob->xRealloc(pBlob, pBlob->nUsed + nData + pBlob->nAlloc + 100);
    if( pBlob->nUsed + nData >= pBlob->nAlloc ){
      blob_panic();
    }
  }
  memcpy(&pBlob->aData[pBlob->nUsed], aData, nData);
  pBlob->nUsed += nData;
  pBlob->aData[pBlob->nUsed] = 0;   /* Blobs are always nul-terminated */
}

/*
** Copy a blob
*/
void blob_copy(Blob *pTo, Blob *pFrom){
  blob_is_init(pFrom);
  blob_zero(pTo);
  blob_append(pTo, blob_buffer(pFrom), blob_size(pFrom));
}

/*
** Return a pointer to a null-terminated string for a blob.
*/
char *blob_str(Blob *p){
  blob_is_init(p);
  if( p->nUsed==0 ){
    blob_append(p, "", 1); /* NOTE: Changes nUsed. */
    p->nUsed = 0;
  }
  if( p->aData[p->nUsed]!=0 ){
    blob_materialize(p);
  }
  return p->aData;
}

/*
** Return a pointer to a null-terminated string for a blob that has
** been created using blob_append_sql() and not blob_appendf().  If
** text was ever added using blob_appendf() then throw an error.
*/
char *blob_sql_text(Blob *p){
  blob_is_init(p);
  if( (p->blobFlags & BLOBFLAG_NotSQL) ){
    fossil_fatal("Internal error: Use of blob_appendf() to construct SQL text");
  }
  return blob_str(p);
}


/*
** Return a pointer to a null-terminated string for a blob.
**
** WARNING:  If the blob is ephemeral, it might cause a '\000'
** character to be inserted into the middle of the parent blob.
** Example:  Suppose p is a token extracted from some larger
** blob pBig using blob_token().  If you call this routine on p,
** then a '\000' character will be inserted in the middle of
** pBig in order to cause p to be nul-terminated.  If pBig
** should not be modified, then use blob_str() instead of this
** routine.  blob_str() will make a copy of the p if necessary
** to avoid modifying pBig.
*/
char *blob_terminate(Blob *p){
  blob_is_init(p);
  if( p->nUsed==0 ) return "";
  p->aData[p->nUsed] = 0;
  return p->aData;
}

/*
** Compare two blobs.  Return negative, zero, or positive if the first
** blob is less then, equal to, or greater than the second.
*/
int blob_compare(Blob *pA, Blob *pB){
  int szA, szB, sz, rc;
  blob_is_init(pA);
  blob_is_init(pB);
  szA = blob_size(pA);
  szB = blob_size(pB);
  sz = szA<szB ? szA : szB;
  rc = memcmp(blob_buffer(pA), blob_buffer(pB), sz);
  if( rc==0 ){
    rc = szA - szB;
  }
  return rc;
}

/*
** Compare two blobs in constant time and return zero if they are equal.
** Constant time comparison only applies for blobs of the same length.
** If lengths are different, immediately returns 1.
*/
int blob_constant_time_cmp(Blob *pA, Blob *pB){
  int szA, szB, i;
  unsigned char *buf1, *buf2;
  unsigned char rc = 0;

  blob_is_init(pA);
  blob_is_init(pB);
  szA = blob_size(pA);
  szB = blob_size(pB);
  if( szA!=szB || szA==0 ) return 1;

  buf1 = (unsigned char*)blob_buffer(pA);
  buf2 = (unsigned char*)blob_buffer(pB);

  for( i=0; i<szA; i++ ){
    rc = rc | (buf1[i] ^ buf2[i]);
  }

  return rc;
}

/*
** Compare a blob to a string.  Return TRUE if they are equal.
*/
int blob_eq_str(Blob *pBlob, const char *z, int n){
  Blob t;
  blob_is_init(pBlob);
  if( n<=0 ) n = strlen(z);
  t.aData = (char*)z;
  t.nUsed = n;
  t.xRealloc = blobReallocStatic;
  return blob_compare(pBlob, &t)==0;
}

/*
** This macro compares a blob against a string constant.  We use the sizeof()
** operator on the string constant twice, so it really does need to be a
** string literal or character array - not a character pointer.
*/
#if INTERFACE
# define blob_eq(B,S) \
     ((B)->nUsed==sizeof(S)-1 && memcmp((B)->aData,S,sizeof(S)-1)==0)
#endif


/*
** Attempt to resize a blob so that its internal buffer is
** nByte in size.  The blob is truncated if necessary.
*/
void blob_resize(Blob *pBlob, unsigned int newSize){
  pBlob->xRealloc(pBlob, newSize+1);
  pBlob->nUsed = newSize;
  pBlob->aData[newSize] = 0;
}

/*
** Make sure a blob is nul-terminated and is not a pointer to unmanaged
** space.  Return a pointer to the data.
*/
char *blob_materialize(Blob *pBlob){
  blob_resize(pBlob, pBlob->nUsed);
  return pBlob->aData;
}


/*
** Call dehttpize on a blob.  This causes an ephemeral blob to be
** materialized.
*/
void blob_dehttpize(Blob *pBlob){
  blob_materialize(pBlob);
  pBlob->nUsed = dehttpize(pBlob->aData);
}

/*
** Extract N bytes from blob pFrom and use it to initialize blob pTo.
** Return the actual number of bytes extracted.
**
** After this call completes, pTo will be an ephemeral blob.
*/
int blob_extract(Blob *pFrom, int N, Blob *pTo){
  blob_is_init(pFrom);
  assert_blob_is_reset(pTo);
  if( pFrom->iCursor + N > pFrom->nUsed ){
    N = pFrom->nUsed - pFrom->iCursor;
    if( N<=0 ){
      blob_zero(pTo);
      return 0;
    }
  }
  pTo->nUsed = N;
  pTo->nAlloc = N;
  pTo->aData = &pFrom->aData[pFrom->iCursor];
  pTo->iCursor = 0;
  pTo->xRealloc = blobReallocStatic;
  pFrom->iCursor += N;
  return N;
}

/*
** Rewind the cursor on a blob back to the beginning.
*/
void blob_rewind(Blob *p){
  p->iCursor = 0;
}

/*
** Seek the cursor in a blob to the indicated offset.
*/
int blob_seek(Blob *p, int offset, int whence){
  if( whence==BLOB_SEEK_SET ){
    p->iCursor = offset;
  }else if( whence==BLOB_SEEK_CUR ){
    p->iCursor += offset;
  }else if( whence==BLOB_SEEK_END ){
    p->iCursor = p->nUsed + offset - 1;
  }
  if( p->iCursor>p->nUsed ){
    p->iCursor = p->nUsed;
  }
  return p->iCursor;
}

/*
** Return the current offset into the blob
*/
int blob_tell(Blob *p){
  return p->iCursor;
}

/*
** Extract a single line of text from pFrom beginning at the current
** cursor location and use that line of text to initialize pTo.
** pTo will include the terminating \n.  Return the number of bytes
** in the line including the \n at the end.  0 is returned at
** end-of-file.
**
** The cursor of pFrom is left pointing at the first byte past the
** \n that terminated the line.
**
** pTo will be an ephermeral blob.  If pFrom changes, it might alter
** pTo as well.
*/
int blob_line(Blob *pFrom, Blob *pTo){
  char *aData = pFrom->aData;
  int n = pFrom->nUsed;
  int i = pFrom->iCursor;

  while( i<n && aData[i]!='\n' ){ i++; }
  if( i<n ){
    assert( aData[i]=='\n' );
    i++;
  }
  blob_extract(pFrom, i-pFrom->iCursor, pTo);
  return pTo->nUsed;
}

/*
** Trim whitespace off of the end of a blob.  Return the number
** of characters remaining.
**
** All this does is reduce the length counter.  This routine does
** not insert a new zero terminator.
*/
int blob_trim(Blob *p){
  char *z = p->aData;
  int n = p->nUsed;
  while( n>0 && fossil_isspace(z[n-1]) ){ n--; }
  p->nUsed = n;
  return n;
}

/*
** Extract a single token from pFrom and use it to initialize pTo.
** Return the number of bytes in the token.  If no token is found,
** return 0.
**
** A token consists of one or more non-space characters.  Leading
** whitespace is ignored.
**
** The cursor of pFrom is left pointing at the first character past
** the end of the token.
**
** pTo will be an ephermeral blob.  If pFrom changes, it might alter
** pTo as well.
*/
int blob_token(Blob *pFrom, Blob *pTo){
  char *aData = pFrom->aData;
  int n = pFrom->nUsed;
  int i = pFrom->iCursor;
  while( i<n && fossil_isspace(aData[i]) ){ i++; }
  pFrom->iCursor = i;
  while( i<n && !fossil_isspace(aData[i]) ){ i++; }
  blob_extract(pFrom, i-pFrom->iCursor, pTo);
  while( i<n && fossil_isspace(aData[i]) ){ i++; }
  pFrom->iCursor = i;
  return pTo->nUsed;
}

/*
** Extract a single SQL token from pFrom and use it to initialize pTo.
** Return the number of bytes in the token.  If no token is found,
** return 0.
**
** An SQL token consists of one or more non-space characters.  If the
** first character is ' then the token is terminated by a matching '
** (ignoring double '') or by the end of the string
**
** The cursor of pFrom is left pointing at the first character past
** the end of the token.
**
** pTo will be an ephermeral blob.  If pFrom changes, it might alter
** pTo as well.
*/
int blob_sqltoken(Blob *pFrom, Blob *pTo){
  char *aData = pFrom->aData;
  int n = pFrom->nUsed;
  int i = pFrom->iCursor;
  while( i<n && fossil_isspace(aData[i]) ){ i++; }
  pFrom->iCursor = i;
  if( aData[i]=='\'' ){
    i++;
    while( i<n ){
      if( aData[i]=='\'' ){
        if( aData[++i]!='\'' ) break;
      }
      i++;
    }
  }else{
    while( i<n && !fossil_isspace(aData[i]) ){ i++; }
  }
  blob_extract(pFrom, i-pFrom->iCursor, pTo);
  while( i<n && fossil_isspace(aData[i]) ){ i++; }
  pFrom->iCursor = i;
  return pTo->nUsed;
}

/*
** Extract everything from the current cursor to the end of the blob
** into a new blob.  The new blob is an ephemerial reference to the
** original blob.  The cursor of the original blob is unchanged.
*/
int blob_tail(Blob *pFrom, Blob *pTo){
  int iCursor = pFrom->iCursor;
  blob_extract(pFrom, pFrom->nUsed-pFrom->iCursor, pTo);
  pFrom->iCursor = iCursor;
  return pTo->nUsed;
}

/*
** Copy N lines of text from pFrom into pTo.  The copy begins at the
** current cursor position of pIn.  The pIn cursor is left pointing
** at the first character past the last \n copied.
**
** If pTo==NULL then this routine simply skips over N lines.
*/
void blob_copy_lines(Blob *pTo, Blob *pFrom, int N){
  char *z = pFrom->aData;
  int i = pFrom->iCursor;
  int n = pFrom->nUsed;
  int cnt = 0;

  if( N==0 ) return;
  while( i<n ){
    if( z[i]=='\n' ){
      cnt++;
      if( cnt==N ){
        i++;
        break;
      }
    }
    i++;
  }
  if( pTo ){
    blob_append(pTo, &pFrom->aData[pFrom->iCursor], i - pFrom->iCursor);
  }
  pFrom->iCursor = i;
}

/*
** Return true if the blob contains a valid base16 identifier artifact hash.
**
** The value returned is actually one of HNAME_SHA1 OR HNAME_K256 if the
** hash is valid.  Both of these are non-zero and therefore "true".
** If the hash is not valid, then HNAME_ERROR is returned, which is zero or
** false.
*/
int blob_is_hname(Blob *pBlob){
  return hname_validate(blob_buffer(pBlob), blob_size(pBlob));
}

/*
** Return true if the blob contains a valid filename
*/
int blob_is_filename(Blob *pBlob){
  return file_is_simple_pathname(blob_str(pBlob), 1);
}

/*
** Return true if the blob contains a valid 32-bit integer.  Store
** the integer value in *pValue.
*/
int blob_is_int(Blob *pBlob, int *pValue){
  const char *z = blob_buffer(pBlob);
  int i, n, c, v;
  n = blob_size(pBlob);
  v = 0;
  for(i=0; i<n && (c = z[i])!=0 && c>='0' && c<='9'; i++){
    v = v*10 + c - '0';
  }
  if( i==n ){
    *pValue = v;
    return 1;
  }else{
    return 0;
  }
}

/*
** Return true if the blob contains a valid 64-bit integer.  Store
** the integer value in *pValue.
*/
int blob_is_int64(Blob *pBlob, sqlite3_int64 *pValue){
  const char *z = blob_buffer(pBlob);
  int i, n, c;
  sqlite3_int64 v;
  n = blob_size(pBlob);
  v = 0;
  for(i=0; i<n && (c = z[i])!=0 && c>='0' && c<='9'; i++){
    v = v*10 + c - '0';
  }
  if( i==n ){
    *pValue = v;
    return 1;
  }else{
    return 0;
  }
}

/*
** Zero or reset an array of Blobs.
*/
void blobarray_zero(Blob *aBlob, int n){
  int i;
  for(i=0; i<n; i++) blob_zero(&aBlob[i]);
}
void blobarray_reset(Blob *aBlob, int n){
  int i;
  for(i=0; i<n; i++) blob_reset(&aBlob[i]);
}

/*
** Parse a blob into space-separated tokens.  Store each token in
** an element of the blobarray aToken[].  aToken[] is nToken elements in
** size.  Return the number of tokens seen.
*/
int blob_tokenize(Blob *pIn, Blob *aToken, int nToken){
  int i;
  for(i=0; i<nToken && blob_token(pIn, &aToken[i]); i++){}
  return i;
}

/*
** Do printf-style string rendering and append the results to a blob.
**
** The blob_appendf() version sets the BLOBFLAG_NotSQL bit in Blob.blobFlags
** whereas blob_append_sql() does not.
*/
void blob_appendf(Blob *pBlob, const char *zFormat, ...){
  if( pBlob ){
    va_list ap;
    va_start(ap, zFormat);
    vxprintf(pBlob, zFormat, ap);
    va_end(ap);
    pBlob->blobFlags |= BLOBFLAG_NotSQL;
  }
}
void blob_append_sql(Blob *pBlob, const char *zFormat, ...){
  if( pBlob ){
    va_list ap;
    va_start(ap, zFormat);
    vxprintf(pBlob, zFormat, ap);
    va_end(ap);
  }
}
void blob_vappendf(Blob *pBlob, const char *zFormat, va_list ap){
  if( pBlob ) vxprintf(pBlob, zFormat, ap);
}

/*
** Initialize a blob to the data on an input channel.  Return
** the number of bytes read into the blob.  Any prior content
** of the blob is discarded, not freed.
*/
int blob_read_from_channel(Blob *pBlob, FILE *in, int nToRead){
  size_t n;
  blob_zero(pBlob);
  if( nToRead<0 ){
    char zBuf[10000];
    while( !feof(in) ){
      n = fread(zBuf, 1, sizeof(zBuf), in);
      if( n>0 ){
        blob_append(pBlob, zBuf, n);
      }
    }
  }else{
    blob_resize(pBlob, nToRead);
    n = fread(blob_buffer(pBlob), 1, nToRead, in);
    blob_resize(pBlob, n);
  }
  return blob_size(pBlob);
}

/*
** Initialize a blob to be the content of a file.  If the filename
** is blank or "-" then read from standard input.
**
** Any prior content of the blob is discarded, not freed.
**
** Return the number of bytes read. Calls fossil_fatal() on error (i.e.
** it exit()s and does not return).
*/
sqlite3_int64 blob_read_from_file(Blob *pBlob, const char *zFilename){
  sqlite3_int64 size, got;
  FILE *in;
  if( zFilename==0 || zFilename[0]==0
        || (zFilename[0]=='-' && zFilename[1]==0) ){
    return blob_read_from_channel(pBlob, stdin, -1);
  }
  size = file_wd_size(zFilename);
  blob_zero(pBlob);
  if( size<0 ){
    fossil_fatal("no such file: %s", zFilename);
  }
  if( size==0 ){
    return 0;
  }
  blob_resize(pBlob, size);
  in = fossil_fopen(zFilename, "rb");
  if( in==0 ){
    fossil_fatal("cannot open %s for reading", zFilename);
  }
  got = fread(blob_buffer(pBlob), 1, size, in);
  fclose(in);
  if( got<size ){
    blob_resize(pBlob, got);
  }
  return got;
}

/*
** Reads symlink destination path and puts int into blob.
** Any prior content of the blob is discarded, not freed.
**
** Returns length of destination path.
**
** On windows, zeros blob and returns 0.
*/
int blob_read_link(Blob *pBlob, const char *zFilename){
#if !defined(_WIN32)
  char zBuf[1024];
  ssize_t len = readlink(zFilename, zBuf, 1023);
  if( len < 0 ){
    fossil_fatal("cannot read symbolic link %s", zFilename);
  }
  zBuf[len] = 0;   /* null-terminate */
  blob_zero(pBlob);
  blob_appendf(pBlob, "%s", zBuf);
  return len;
#else
  blob_zero(pBlob);
  return 0;
#endif
}


/*
** Write the content of a blob into a file.
**
** If the filename is blank or "-" then write to standard output.
**
** Return the number of bytes written.
*/
int blob_write_to_file(Blob *pBlob, const char *zFilename){
  FILE *out;
  int nWrote;

  if( zFilename[0]==0 || (zFilename[0]=='-' && zFilename[1]==0) ){
    blob_is_init(pBlob);
#if defined(_WIN32)
    nWrote = fossil_utf8_to_console(blob_buffer(pBlob), blob_size(pBlob), 0);
    if( nWrote>=0 ) return nWrote;
    fflush(stdout);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    nWrote = fwrite(blob_buffer(pBlob), 1, blob_size(pBlob), stdout);
#if defined(_WIN32)
    fflush(stdout);
    _setmode(_fileno(stdout), _O_TEXT);
#endif
  }else{
    file_mkfolder(zFilename, 1, 0);
    out = fossil_fopen(zFilename, "wb");
    if( out==0 ){
#if _WIN32
      const char *zReserved = file_is_win_reserved(zFilename);
      if( zReserved ){
        fossil_fatal("cannot open \"%s\" because \"%s\" is "
             "a reserved name on Windows", zFilename, zReserved);
      }
#endif
      fossil_fatal_recursive("unable to open file \"%s\" for writing",
                             zFilename);
      return 0;
    }
    blob_is_init(pBlob);
    nWrote = fwrite(blob_buffer(pBlob), 1, blob_size(pBlob), out);
    fclose(out);
    if( nWrote!=blob_size(pBlob) ){
      fossil_fatal_recursive("short write: %d of %d bytes to %s", nWrote,
         blob_size(pBlob), zFilename);
    }
  }
  return nWrote;
}

/*
** Compress a blob pIn.  Store the result in pOut.  It is ok for pIn and
** pOut to be the same blob.
**
** pOut must either be the same as pIn or else uninitialized.
*/
void blob_compress(Blob *pIn, Blob *pOut){
  unsigned int nIn = blob_size(pIn);
  unsigned int nOut = 13 + nIn + (nIn+999)/1000;
  unsigned long int nOut2;
  unsigned char *outBuf;
  Blob temp;
  blob_zero(&temp);
  blob_resize(&temp, nOut+4);
  outBuf = (unsigned char*)blob_buffer(&temp);
  outBuf[0] = nIn>>24 & 0xff;
  outBuf[1] = nIn>>16 & 0xff;
  outBuf[2] = nIn>>8 & 0xff;
  outBuf[3] = nIn & 0xff;
  nOut2 = (long int)nOut;
  compress(&outBuf[4], &nOut2,
           (unsigned char*)blob_buffer(pIn), blob_size(pIn));
  if( pOut==pIn ) blob_reset(pOut);
  assert_blob_is_reset(pOut);
  *pOut = temp;
  blob_resize(pOut, nOut2+4);
}

/*
** COMMAND: test-compress
**
** Usage: %fossil test-compress INPUTFILE OUTPUTFILE
**
** Run compression on INPUTFILE and write the result into OUTPUTFILE.
**
** This is used to test and debug the blob_compress() routine.
*/
void compress_cmd(void){
  Blob f;
  if( g.argc!=4 ) usage("INPUTFILE OUTPUTFILE");
  blob_read_from_file(&f, g.argv[2]);
  blob_compress(&f, &f);
  blob_write_to_file(&f, g.argv[3]);
}

/*
** Compress the concatenation of a blobs pIn1 and pIn2.  Store the result
** in pOut.
**
** pOut must be either uninitialized or must be the same as either pIn1 or
** pIn2.
*/
void blob_compress2(Blob *pIn1, Blob *pIn2, Blob *pOut){
  unsigned int nIn = blob_size(pIn1) + blob_size(pIn2);
  unsigned int nOut = 13 + nIn + (nIn+999)/1000;
  unsigned char *outBuf;
  z_stream stream;
  Blob temp;
  blob_zero(&temp);
  blob_resize(&temp, nOut+4);
  outBuf = (unsigned char*)blob_buffer(&temp);
  outBuf[0] = nIn>>24 & 0xff;
  outBuf[1] = nIn>>16 & 0xff;
  outBuf[2] = nIn>>8 & 0xff;
  outBuf[3] = nIn & 0xff;
  stream.zalloc = (alloc_func)0;
  stream.zfree = (free_func)0;
  stream.opaque = 0;
  stream.avail_out = nOut;
  stream.next_out = &outBuf[4];
  deflateInit(&stream, 9);
  stream.avail_in = blob_size(pIn1);
  stream.next_in = (unsigned char*)blob_buffer(pIn1);
  deflate(&stream, 0);
  stream.avail_in = blob_size(pIn2);
  stream.next_in = (unsigned char*)blob_buffer(pIn2);
  deflate(&stream, 0);
  deflate(&stream, Z_FINISH);
  blob_resize(&temp, stream.total_out + 4);
  deflateEnd(&stream);
  if( pOut==pIn1 ) blob_reset(pOut);
  if( pOut==pIn2 ) blob_reset(pOut);
  assert_blob_is_reset(pOut);
  *pOut = temp;
}

/*
** COMMAND: test-compress-2
**
** Usage: %fossil test-compress-2 IN1 IN2 OUT
**
** Read files IN1 and IN2, concatenate the content, compress the
** content, then write results into OUT.
**
** This is used to test and debug the blob_compress2() routine.
*/
void compress2_cmd(void){
  Blob f1, f2;
  if( g.argc!=5 ) usage("INPUTFILE1 INPUTFILE2 OUTPUTFILE");
  blob_read_from_file(&f1, g.argv[2]);
  blob_read_from_file(&f2, g.argv[3]);
  blob_compress2(&f1, &f2, &f1);
  blob_write_to_file(&f1, g.argv[4]);
}

/*
** Uncompress blob pIn and store the result in pOut.  It is ok for pIn and
** pOut to be the same blob.
**
** pOut must be either uninitialized or the same as pIn.
*/
int blob_uncompress(Blob *pIn, Blob *pOut){
  unsigned int nOut;
  unsigned char *inBuf;
  unsigned int nIn = blob_size(pIn);
  Blob temp;
  int rc;
  unsigned long int nOut2;
  if( nIn<=4 ){
    return 0;
  }
  inBuf = (unsigned char*)blob_buffer(pIn);
  nOut = (inBuf[0]<<24) + (inBuf[1]<<16) + (inBuf[2]<<8) + inBuf[3];
  blob_zero(&temp);
  blob_resize(&temp, nOut+1);
  nOut2 = (long int)nOut;
  rc = uncompress((unsigned char*)blob_buffer(&temp), &nOut2,
                  &inBuf[4], nIn - 4);
  if( rc!=Z_OK ){
    blob_reset(&temp);
    return 1;
  }
  blob_resize(&temp, nOut2);
  if( pOut==pIn ) blob_reset(pOut);
  assert_blob_is_reset(pOut);
  *pOut = temp;
  return 0;
}

/*
** COMMAND: test-uncompress
**
** Usage: %fossil test-uncompress IN OUT
**
** Read the content of file IN, uncompress that content, and write the
** result into OUT.  This command is intended for testing of the
** blob_compress() function.
*/
void uncompress_cmd(void){
  Blob f;
  if( g.argc!=4 ) usage("INPUTFILE OUTPUTFILE");
  blob_read_from_file(&f, g.argv[2]);
  blob_uncompress(&f, &f);
  blob_write_to_file(&f, g.argv[3]);
}

/*
** COMMAND: test-cycle-compress
**
** Compress and uncompress each file named on the command line.
** Verify that the original content is recovered.
*/
void test_cycle_compress(void){
  int i;
  Blob b1, b2, b3;
  for(i=2; i<g.argc; i++){
    blob_read_from_file(&b1, g.argv[i]);
    blob_compress(&b1, &b2);
    blob_uncompress(&b2, &b3);
    if( blob_compare(&b1, &b3) ){
      fossil_fatal("compress/uncompress cycle failed for %s", g.argv[i]);
    }
    blob_reset(&b1);
    blob_reset(&b2);
    blob_reset(&b3);
  }
  fossil_print("ok\n");
}

#if defined(_WIN32) || defined(__CYGWIN__)
/*
** Convert every \n character in the given blob into \r\n.
*/
void blob_add_cr(Blob *p){
  char *z = p->aData;
  int j   = p->nUsed;
  int i, n;
  for(i=n=0; i<j; i++){
    if( z[i]=='\n' ) n++;
  }
  j += n;
  if( j>=p->nAlloc ){
    blob_resize(p, j);
    z = p->aData;
  }
  p->nUsed = j;
  z[j] = 0;
  while( j>i ){
    if( (z[--j] = z[--i]) =='\n' ){
      z[--j] = '\r';
    }
  }
}
#endif

/*
** Remove every \r character from the given blob, replacing each one with
** a \n character if it was not already part of a \r\n pair.
*/
void blob_to_lf_only(Blob *p){
  int i, j;
  char *z = blob_materialize(p);
  for(i=j=0; z[i]; i++){
    if( z[i]!='\r' ) z[j++] = z[i];
    else if( z[i+1]!='\n' ) z[j++] = '\n';
  }
  z[j] = 0;
  p->nUsed = j;
}

/*
** Convert blob from cp1252 to UTF-8. As cp1252 is a superset
** of iso8859-1, this is useful on UNIX as well.
**
** This table contains the character translations for 0x80..0xA0.
*/

static const unsigned short cp1252[32] = {
  0x20ac,   0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152,   0x8D, 0x017D,   0x8F,
    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
   0x2DC, 0x2122, 0x0161, 0x203A, 0x0153,   0x9D, 0x017E, 0x0178
};

void blob_cp1252_to_utf8(Blob *p){
  unsigned char *z = (unsigned char *)p->aData;
  int j   = p->nUsed;
  int i, n;
  for(i=n=0; i<j; i++){
    if( z[i]>=0x80 ){
      if( (z[i]<0xa0) && (cp1252[z[i]&0x1f]>=0x800) ){
        n++;
      }
      n++;
    }
  }
  j += n;
  if( j>=p->nAlloc ){
    blob_resize(p, j);
    z = (unsigned char *)p->aData;
  }
  p->nUsed = j;
  z[j] = 0;
  while( j>i ){
    if( z[--i]>=0x80 ){
      if( z[i]<0xa0 ){
        unsigned short sym = cp1252[z[i]&0x1f];
        if( sym>=0x800 ){
          z[--j] = 0x80 | (sym&0x3f);
          z[--j] = 0x80 | ((sym>>6)&0x3f);
          z[--j] = 0xe0 | (sym>>12);
        }else{
          z[--j] = 0x80 | (sym&0x3f);
          z[--j] = 0xc0 | (sym>>6);
        }
      }else{
        z[--j] = 0x80 | (z[i]&0x3f);
        z[--j] = 0xC0 | (z[i]>>6);
      }
    }else{
      z[--j] = z[i];
    }
  }
}

/*
** pBlob is a shell command under construction.  This routine safely
** appends argument zIn.
**
** The argument is escaped if it contains white space or other characters
** that need to be escaped for the shell.  If zIn contains characters
** that cannot be safely escaped, then throw a fatal error.
**
** The argument is expected to a filename of some kinds.  As shell commands
** commonly have command-line options that begin with "-" and since we
** do not want an attacker to be able to invoke these switches using
** filenames that begin with "-", if zIn begins with "-", prepend
** an additional "./".
*/
void blob_append_escaped_arg(Blob *pBlob, const char *zIn){
  int i;
  char c;
  int needEscape = 0;
  int n = blob_size(pBlob);
  char *z = blob_buffer(pBlob);
#if defined(_WIN32)
  const char cQuote = '"';    /* Use "..." quoting on windows */
#else
  const char cQuote = '\'';   /* Use '...' quoting on unix */
#endif

  for(i=0; (c = zIn[i])!=0; i++){
    if( c==cQuote || c=='\\' || c<' ' || c==';' || c=='*' || c=='?' || c=='[') {
      Blob bad;
      blob_token(pBlob, &bad);
      fossil_fatal("the [%s] argument to the \"%s\" command contains "
                   "a character (ascii 0x%02x) that is a security risk",
                   zIn, blob_str(&bad), c);
    }
    if( !needEscape && !fossil_isalnum(c) && c!='/' && c!='.' && c!='_' ){
      needEscape = 1;
    }
  }
  if( n>0 && !fossil_isspace(z[n-1]) ){
    blob_append(pBlob, " ", 1);
  }
  if( needEscape ) blob_append(pBlob, &cQuote, 1);
  if( zIn[0]=='-' ) blob_append(pBlob, "./", 2);
  blob_append(pBlob, zIn, -1);
  if( needEscape ) blob_append(pBlob, &cQuote, 1);
}

/*
** A read(2)-like impl for the Blob class. Reads (copies) up to nLen
** bytes from pIn, starting at position pIn->iCursor, and copies them
** to pDest (which must be valid memory at least nLen bytes long).
**
** Returns the number of bytes read/copied, which may be less than
** nLen (if end-of-blob is encountered).
**
** Updates pIn's cursor.
**
** Returns 0 if pIn contains no data.
*/
unsigned int blob_read(Blob *pIn, void * pDest, unsigned int nLen ){
  if( !pIn->aData || (pIn->iCursor >= pIn->nUsed) ){
    return 0;
  } else if( (pIn->iCursor + nLen) > (unsigned int)pIn->nUsed ){
    nLen = (unsigned int) (pIn->nUsed - pIn->iCursor);
  }
  assert( pIn->nUsed > pIn->iCursor );
  assert( (pIn->iCursor+nLen)  <= pIn->nUsed );
  if( nLen ){
    memcpy( pDest, pIn->aData, nLen );
    pIn->iCursor += nLen;
  }
  return nLen;
}

/*
** Swaps the contents of the given blobs. Results
** are unspecified if either value is NULL or both
** point to the same blob.
*/
void blob_swap( Blob *pLeft, Blob *pRight ){
  Blob swap = *pLeft;
  *pLeft = *pRight;
  *pRight = swap;
}

/*
** Strip a possible byte-order-mark (BOM) from the blob. On Windows, if there
** is either no BOM at all or an (le/be) UTF-16 BOM, a conversion to UTF-8 is
** done.  If useMbcs is false and there is no BOM, the input string is assumed
** to be UTF-8 already, so no conversion is done.
*/
void blob_to_utf8_no_bom(Blob *pBlob, int useMbcs){
  char *zUtf8;
  int bomSize = 0;
  int bomReverse = 0;
  if( starts_with_utf8_bom(pBlob, &bomSize) ){
    struct Blob temp;
    zUtf8 = blob_str(pBlob) + bomSize;
    blob_zero(&temp);
    blob_append(&temp, zUtf8, -1);
    blob_swap(pBlob, &temp);
    blob_reset(&temp);
  }else if( starts_with_utf16_bom(pBlob, &bomSize, &bomReverse) ){
    zUtf8 = blob_buffer(pBlob);
    if( bomReverse ){
      /* Found BOM, but with reversed bytes */
      unsigned int i = blob_size(pBlob);
      while( i>0 ){
        /* swap bytes of unicode representation */
        char zTemp = zUtf8[--i];
        zUtf8[i] = zUtf8[i-1];
        zUtf8[--i] = zTemp;
      }
    }
    /* Make sure the blob contains two terminating 0-bytes */
    blob_append(pBlob, "", 1);
    zUtf8 = blob_str(pBlob) + bomSize;
    zUtf8 = fossil_unicode_to_utf8(zUtf8);
    blob_set_dynamic(pBlob, zUtf8);
  }else if( useMbcs && invalid_utf8(pBlob) ){
#if defined(_WIN32) || defined(__CYGWIN__)
    zUtf8 = fossil_mbcs_to_utf8(blob_str(pBlob));
    blob_reset(pBlob);
    blob_append(pBlob, zUtf8, -1);
    fossil_mbcs_free(zUtf8);
#else
    blob_cp1252_to_utf8(pBlob);
#endif /* _WIN32 */
  }
}
