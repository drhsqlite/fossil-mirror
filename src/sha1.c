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
** This implementation of SHA1.
*/
#include "config.h"
#include <sys/types.h>
#include "sha1.h"


/*
** SHA1 Implementation #1 is the hardened SHA1 implementation by
** Marc Stevens.  Code obtained from GitHub
**
**     https://github.com/cr-marcstevens/sha1collisiondetection
**
** Downloaded on 2017-03-01 then repackaged to work with Fossil
** and makeheaders.
*/
#if FOSSIL_HARDENED_SHA1

#if INTERFACE
typedef void(*collision_block_callback)(uint64_t, const uint32_t*, const uint32_t*, const uint32_t*, const uint32_t*);
struct SHA1_CTX {
  uint64_t total;
  uint32_t ihv[5];
  unsigned char buffer[64];
  int bigendian;
  int found_collision;
  int safe_hash;
  int detect_coll;
  int ubc_check;
  int reduced_round_coll;
  collision_block_callback callback;

  uint32_t ihv1[5];
  uint32_t ihv2[5];
  uint32_t m1[80];
  uint32_t m2[80];
  uint32_t states[80][5];
};
#endif
void SHA1DCInit(SHA1_CTX*);
void SHA1DCUpdate(SHA1_CTX*, const unsigned char*, unsigned);
int SHA1DCFinal(unsigned char[20], SHA1_CTX*);

#define SHA1Context SHA1_CTX
#define SHA1Init SHA1DCInit
#define SHA1Update SHA1DCUpdate
#define SHA1Final SHA1DCFinal

/*
** SHA1 Implementation #2: use the SHA1 algorithm built into SSL
*/
#elif  defined(FOSSIL_ENABLE_SSL)

# include <openssl/sha.h>
# define SHA1Context SHA_CTX
# define SHA1Init SHA1_Init
# define SHA1Update SHA1_Update
# define SHA1Final SHA1_Final

/*
** SHA1 Implementation #3:  If none of the previous two SHA1
** algorithms work, there is this built-in.  This built-in was the
** original implementation used by Fossil.
*/
#else
/*
** The SHA1 implementation below is adapted from:
**
**  $NetBSD: sha1.c,v 1.6 2009/11/06 20:31:18 joerg Exp $
**  $OpenBSD: sha1.c,v 1.9 1997/07/23 21:12:32 kstailey Exp $
**
** SHA-1 in C
** By Steve Reid <steve@edmweb.com>
** 100% Public Domain
*/
typedef struct SHA1Context SHA1Context;
struct SHA1Context {
  unsigned int state[5];
  unsigned int count[2];
  unsigned char buffer[64];
};

/*
 * blk0() and blk() perform the initial expand.
 * I got the idea of expanding during the round function from SSLeay
 *
 * blk0le() for little-endian and blk0be() for big-endian.
 */
#if __GNUC__ && (defined(__i386__) || defined(__x86_64__))
/*
 * GCC by itself only generates left rotates.  Use right rotates if
 * possible to be kinder to dinky implementations with iterative rotate
 * instructions.
 */
#define SHA_ROT(op, x, k) \
        ({ unsigned int y; asm(op " %1,%0" : "=r" (y) : "I" (k), "0" (x)); y; })
#define rol(x,k) SHA_ROT("roll", x, k)
#define ror(x,k) SHA_ROT("rorl", x, k)

#else
/* Generic C equivalent */
#define SHA_ROT(x,l,r) ((x) << (l) | (x) >> (r))
#define rol(x,k) SHA_ROT(x,k,32-(k))
#define ror(x,k) SHA_ROT(x,32-(k),k)
#endif





#define blk0le(i) (block[i] = (ror(block[i],8)&0xFF00FF00) \
    |(rol(block[i],8)&0x00FF00FF))
#define blk0be(i) block[i]
#define blk(i) (block[i&15] = rol(block[(i+13)&15]^block[(i+8)&15] \
    ^block[(i+2)&15]^block[i&15],1))

/*
 * (R0+R1), R2, R3, R4 are the different operations (rounds) used in SHA1
 *
 * Rl0() for little-endian and Rb0() for big-endian.  Endianness is
 * determined at run-time.
 */
#define Rl0(v,w,x,y,z,i) \
    z+=((w&(x^y))^y)+blk0le(i)+0x5A827999+rol(v,5);w=ror(w,2);
#define Rb0(v,w,x,y,z,i) \
    z+=((w&(x^y))^y)+blk0be(i)+0x5A827999+rol(v,5);w=ror(w,2);
#define R1(v,w,x,y,z,i) \
    z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=ror(w,2);
#define R2(v,w,x,y,z,i) \
    z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=ror(w,2);
#define R3(v,w,x,y,z,i) \
    z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=ror(w,2);
#define R4(v,w,x,y,z,i) \
    z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=ror(w,2);

/*
 * Hash a single 512-bit block. This is the core of the algorithm.
 */
#define a qq[0]
#define b qq[1]
#define c qq[2]
#define d qq[3]
#define e qq[4]

void SHA1Transform(unsigned int state[5], const unsigned char buffer[64])
{
  unsigned int qq[5]; /* a, b, c, d, e; */
  static int one = 1;
  unsigned int block[16];
  memcpy(block, buffer, 64);
  memcpy(qq,state,5*sizeof(unsigned int));

  /* Copy context->state[] to working vars */
  /*
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  */

  /* 4 rounds of 20 operations each. Loop unrolled. */
  if( 1 == *(unsigned char*)&one ){
    Rl0(a,b,c,d,e, 0); Rl0(e,a,b,c,d, 1); Rl0(d,e,a,b,c, 2); Rl0(c,d,e,a,b, 3);
    Rl0(b,c,d,e,a, 4); Rl0(a,b,c,d,e, 5); Rl0(e,a,b,c,d, 6); Rl0(d,e,a,b,c, 7);
    Rl0(c,d,e,a,b, 8); Rl0(b,c,d,e,a, 9); Rl0(a,b,c,d,e,10); Rl0(e,a,b,c,d,11);
    Rl0(d,e,a,b,c,12); Rl0(c,d,e,a,b,13); Rl0(b,c,d,e,a,14); Rl0(a,b,c,d,e,15);
  }else{
    Rb0(a,b,c,d,e, 0); Rb0(e,a,b,c,d, 1); Rb0(d,e,a,b,c, 2); Rb0(c,d,e,a,b, 3);
    Rb0(b,c,d,e,a, 4); Rb0(a,b,c,d,e, 5); Rb0(e,a,b,c,d, 6); Rb0(d,e,a,b,c, 7);
    Rb0(c,d,e,a,b, 8); Rb0(b,c,d,e,a, 9); Rb0(a,b,c,d,e,10); Rb0(e,a,b,c,d,11);
    Rb0(d,e,a,b,c,12); Rb0(c,d,e,a,b,13); Rb0(b,c,d,e,a,14); Rb0(a,b,c,d,e,15);
  }
  R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
  R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
  R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
  R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
  R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
  R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
  R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
  R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
  R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
  R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
  R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
  R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
  R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
  R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
  R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
  R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

  /* Add the working vars back into context.state[] */
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}


/*
 * SHA1Init - Initialize new context
 */
static void SHA1Init(SHA1Context *context){
    /* SHA1 initialization constants */
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}


/*
 * Run your data through this.
 */
static void SHA1Update(
  SHA1Context *context,
  const unsigned char *data,
  unsigned int len
){
    unsigned int i, j;

    j = context->count[0];
    if ((context->count[0] += len << 3) < j)
        context->count[1] += (len>>29)+1;
    j = (j >> 3) & 63;
    if ((j + len) > 63) {
        (void)memcpy(&context->buffer[j], data, (i = 64-j));
        SHA1Transform(context->state, context->buffer);
        for ( ; i + 63 < len; i += 64)
            SHA1Transform(context->state, &data[i]);
        j = 0;
    } else {
        i = 0;
    }
    (void)memcpy(&context->buffer[j], &data[i], len - i);
}


/*
 * Add padding and return the message digest.
 */
static void SHA1Final(unsigned char *digest, SHA1Context *context){
    unsigned int i;
    unsigned char finalcount[8];

    for (i = 0; i < 8; i++) {
        finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)]
         >> ((3-(i & 3)) * 8) ) & 255); /* Endian independent */
    }
    SHA1Update(context, (const unsigned char *)"\200", 1);
    while ((context->count[0] & 504) != 448)
        SHA1Update(context, (const unsigned char *)"\0", 1);
    SHA1Update(context, finalcount, 8);  /* Should cause a SHA1Transform() */

    if (digest) {
        for (i = 0; i < 20; i++)
            digest[i] = (unsigned char)
                ((context->state[i>>2] >> ((3-(i & 3)) * 8) ) & 255);
    }
}
#endif /* Built-in SHA1 implemenation */

/*
** Convert a digest into base-16.  digest should be declared as
** "unsigned char digest[20]" in the calling function.  The SHA1
** digest is stored in the first 20 bytes.  zBuf should
** be "char zBuf[41]".
*/
static void DigestToBase16(unsigned char *digest, char *zBuf){
  static const char zEncode[] = "0123456789abcdef";
  int ix;

  for(ix=0; ix<20; ix++){
    *zBuf++ = zEncode[(*digest>>4)&0xf];
    *zBuf++ = zEncode[*digest++ & 0xf];
  }
  *zBuf = '\0';
}

/*
** The state of a incremental SHA1 checksum computation.  Only one
** such computation can be underway at a time, of course.
*/
static SHA1Context incrCtx;
static int incrInit = 0;

/*
** Add more text to the incremental SHA1 checksum.
*/
void sha1sum_step_text(const char *zText, int nBytes){
  if( !incrInit ){
    SHA1Init(&incrCtx);
    incrInit = 1;
  }
  if( nBytes<=0 ){
    if( nBytes==0 ) return;
    nBytes = strlen(zText);
  }
  SHA1Update(&incrCtx, (unsigned char*)zText, nBytes);
}

/*
** Add the content of a blob to the incremental SHA1 checksum.
*/
void sha1sum_step_blob(Blob *p){
  sha1sum_step_text(blob_buffer(p), blob_size(p));
}

/*
** Finish the incremental SHA1 checksum.  Store the result in blob pOut
** if pOut!=0.  Also return a pointer to the result.
**
** This resets the incremental checksum preparing for the next round
** of computation.  The return pointer points to a static buffer that
** is overwritten by subsequent calls to this function.
*/
char *sha1sum_finish(Blob *pOut){
  unsigned char zResult[20];
  static char zOut[41];
  sha1sum_step_text(0,0);
  SHA1Final(zResult, &incrCtx);
  incrInit = 0;
  DigestToBase16(zResult, zOut);
  if( pOut ){
    blob_zero(pOut);
    blob_append(pOut, zOut, 40);
  }
  return zOut;
}


/*
** Compute the SHA1 checksum of a file on disk.  Store the resulting
** checksum in the blob pCksum.  pCksum is assumed to be initialized.
**
** Return the number of errors.
*/
int sha1sum_file(const char *zFilename, Blob *pCksum){
  FILE *in;
  SHA1Context ctx;
  unsigned char zResult[20];
  char zBuf[10240];

  if( file_wd_islink(zFilename) ){
    /* Instead of file content, return sha1 of link destination path */
    Blob destinationPath;
    int rc;

    blob_read_link(&destinationPath, zFilename);
    rc = sha1sum_blob(&destinationPath, pCksum);
    blob_reset(&destinationPath);
    return rc;
  }

  in = fossil_fopen(zFilename,"rb");
  if( in==0 ){
    return 1;
  }
  SHA1Init(&ctx);
  for(;;){
    int n;
    n = fread(zBuf, 1, sizeof(zBuf), in);
    if( n<=0 ) break;
    SHA1Update(&ctx, (unsigned char*)zBuf, (unsigned)n);
  }
  fclose(in);
  blob_zero(pCksum);
  blob_resize(pCksum, 40);
  SHA1Final(zResult, &ctx);
  DigestToBase16(zResult, blob_buffer(pCksum));
  return 0;
}

/*
** Compute the SHA1 checksum of a blob in memory.  Store the resulting
** checksum in the blob pCksum.  pCksum is assumed to be either
** uninitialized or the same blob as pIn.
**
** Return the number of errors.
*/
int sha1sum_blob(const Blob *pIn, Blob *pCksum){
  SHA1Context ctx;
  unsigned char zResult[20];

  SHA1Init(&ctx);
  SHA1Update(&ctx, (unsigned char*)blob_buffer(pIn), blob_size(pIn));
  if( pIn==pCksum ){
    blob_reset(pCksum);
  }else{
    blob_zero(pCksum);
  }
  blob_resize(pCksum, 40);
  SHA1Final(zResult, &ctx);
  DigestToBase16(zResult, blob_buffer(pCksum));
  return 0;
}

/*
** Compute the SHA1 checksum of a zero-terminated string.  The
** result is held in memory obtained from mprintf().
*/
char *sha1sum(const char *zIn){
  SHA1Context ctx;
  unsigned char zResult[20];
  char zDigest[41];

  SHA1Init(&ctx);
  SHA1Update(&ctx, (unsigned const char*)zIn, strlen(zIn));
  SHA1Final(zResult, &ctx);
  DigestToBase16(zResult, zDigest);
  return mprintf("%s", zDigest);
}

/*
** Convert a cleartext password for a specific user into a SHA1 hash.
**
** The algorithm here is:
**
**       SHA1( project-code + "/" + login + "/" + password )
**
** In words: The users login name and password are appended to the
** project ID code and the SHA1 hash of the result is computed.
**
** The result of this function is the shared secret used by a client
** to authenticate to a server for the sync protocol.  It is also the
** value stored in the USER.PW field of the database.  By mixing in the
** login name and the project id with the hash, different shared secrets
** are obtained even if two users select the same password, or if a
** single user selects the same password for multiple projects.
*/
char *sha1_shared_secret(
  const char *zPw,        /* The password to encrypt */
  const char *zLogin,     /* Username */
  const char *zProjCode   /* Project-code.  Use built-in project code if NULL */
){
  static char *zProjectId = 0;
  SHA1Context ctx;
  unsigned char zResult[20];
  char zDigest[41];

  SHA1Init(&ctx);
  if( zProjCode==0 ){
    if( zProjectId==0 ){
      zProjectId = db_get("project-code", 0);

      /* On the first xfer request of a clone, the project-code is not yet
      ** known.  Use the cleartext password, since that is all we have.
      */
      if( zProjectId==0 ){
        return mprintf("%s", zPw);
      }
    }
    zProjCode = zProjectId;
  }
  SHA1Update(&ctx, (unsigned char*)zProjCode, strlen(zProjCode));
  SHA1Update(&ctx, (unsigned char*)"/", 1);
  SHA1Update(&ctx, (unsigned char*)zLogin, strlen(zLogin));
  SHA1Update(&ctx, (unsigned char*)"/", 1);
  SHA1Update(&ctx, (unsigned const char*)zPw, strlen(zPw));
  SHA1Final(zResult, &ctx);
  DigestToBase16(zResult, zDigest);
  return mprintf("%s", zDigest);
}

/*
** Implement the shared_secret() SQL function.  shared_secret() takes two or
** three arguments; the third argument is optional.
**
** (1) The cleartext password
** (2) The login name
** (3) The project code
**
** Returns sha1($password/$login/$projcode).
*/
void sha1_shared_secret_sql_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zPw;
  const char *zLogin;
  const char *zProjid;

  assert( argc==2 || argc==3 );
  zPw = (const char*)sqlite3_value_text(argv[0]);
  if( zPw==0 || zPw[0]==0 ) return;
  zLogin = (const char*)sqlite3_value_text(argv[1]);
  if( zLogin==0 ) return;
  if( argc==3 ){
    zProjid = (const char*)sqlite3_value_text(argv[2]);
    if( zProjid && zProjid[0]==0 ) zProjid = 0;
  }else{
    zProjid = 0;
  }
  sqlite3_result_text(context, sha1_shared_secret(zPw, zLogin, zProjid), -1,
                      fossil_free);
}

/*
** COMMAND: sha1sum*
**
** Usage: %fossil sha1sum FILE...
**
** Compute an SHA1 checksum of all files named on the command-line.
** If a file is named "-" then take its content from standard input.
*/
void sha1sum_test(void){
  int i;
  Blob in;
  Blob cksum;

  for(i=2; i<g.argc; i++){
    blob_init(&cksum, "************** not found ***************", -1);
    if( g.argv[i][0]=='-' && g.argv[i][1]==0 ){
      blob_read_from_channel(&in, stdin, -1);
      sha1sum_blob(&in, &cksum);
    }else{
      sha1sum_file(g.argv[i], &cksum);
    }
    fossil_print("%s  %s\n", blob_str(&cksum), g.argv[i]);
    blob_reset(&cksum);
  }
}
