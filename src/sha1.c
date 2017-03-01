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
#include <stdint.h>
#include "sha1.h"


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
void SHA1DCUpdate(SHA1_CTX*, const char*, unsigned);
int SHA1DCFinal(unsigned char[20], SHA1_CTX*);

#define SHA1Context SHA1_CTX
#define SHA1Init SHA1DCInit
#define SHA1Update SHA1DCUpdate
#define SHA1Final SHA1DCFinal

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
