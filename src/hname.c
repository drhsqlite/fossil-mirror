/*
** Copyright (c) 2017 D. Richard Hipp
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
** This file contains generic code for dealing with hashes used for
** naming artifacts.  Specific hash algorithms are implemented separately
** (for example in sha1.c and sha3.c).  This file contains the generic
** interface code.
*/
#include "config.h"
#include "hname.h"


#if INTERFACE
/*
** Code numbers for the allowed hash algorithms.
*/
#define HNAME_ERROR  0      /* Not a valid hash */
#define HNAME_SHA1   1      /* SHA1 */
#define HNAME_K224   2      /* SHA3-224 */
#define HNAME_K256   3      /* SHA3-256 */

/*
** Minimum and maximum lengths for a hash value when hex encoded.
*/
#define HNAME_MIN  40     /* Length for SHA1 */
#define HNAME_MAX  64     /* Length for SHA3-256 */

/*
** Hash lengths for the various algorithms
*/
#define HNAME_LEN_SHA1   40
#define HNAME_LEN_K224   56
#define HNAME_LEN_K256   64

#endif /* INTERFACE */


/*
** Convert a hash algorithm code number into a string name for that algorithm.
*/
const char *hname_algname(int aid){
  if( aid==HNAME_K224 ) return "SHA3-224";
  return "SHA1";
}

/*
** Given a hash algorithm name, return its appropriate number.  Return 
** HNAME_ERROR if the name is unknown.
*/
int hname_algid(const char *zName){
  if( fossil_stricmp(zName,"sha1")==0 ) return HNAME_SHA1;
  if( fossil_stricmp(zName,"sha3-224")==0 ) return HNAME_K224;
  if( fossil_stricmp(zName,"sha3-256")==0 ) return HNAME_K256;
  return HNAME_ERROR;
}

/*
** Return the integer hash algorithm code number (ex: HNAME_K224) for
** the hash string provided.  Or return HNAME_ERROR (0) if the input string
** is not a valid artifact hash string.
*/
int hname_validate(const char *zHash, int nHash){
  int id;
  switch( nHash ){
    case HNAME_LEN_SHA1:   id = HNAME_SHA1;  break;
    case HNAME_LEN_K224:   id = HNAME_K224;  break;
    case HNAME_LEN_K256:   id = HNAME_K256;  break;
    default:               return HNAME_ERROR;
  }
  if( !validate16(zHash, nHash) ) return HNAME_ERROR;
  return id;
}

/*
** Verify that zHash is a valid hash for the content in pContent.
** Return true if the hash is correct.  Return false if the content
** does not match the hash.
**
** Actually, the returned value is one of the hash algorithm constants
** corresponding to the hash that matched if the hash is correct.
** (Examples: HNAME_SHA1 or HNAME_K224).  And the return is HNAME_ERROR
** if the hash does not match.
*/
int hname_verify_hash(Blob *pContent, const char *zHash, int nHash){
  int id = HNAME_ERROR;
  switch( nHash ){
    case HNAME_LEN_SHA1: {
      Blob hash;
      sha1sum_blob(pContent, &hash);
      if( memcmp(blob_buffer(&hash),zHash,HNAME_LEN_SHA1)==0 ) id = HNAME_SHA1;
      blob_reset(&hash);
      break;
    }
    case HNAME_LEN_K224:
    case HNAME_LEN_K256: {
      sha3sum_init(nHash*4);
      sha3sum_step_blob(pContent);
      if( memcmp(sha3sum_finish(0),zHash,nHash)==0 ){
        id = nHash==HNAME_LEN_K224 ? HNAME_K224 : HNAME_K256;
      }
      break;
    }
  }
  return id;
}

/*
** Verify that zHash is a valid hash for the content of a file on
** disk named zFile.
**
** Return true if the hash is correct.  Return false if the content
** does not match the hash.
**
** Actually, the returned value is one of the hash algorithm constants
** corresponding to the hash that matched if the hash is correct.
** (Examples: HNAME_SHA1 or HNAME_K224).  And the return is HNAME_ERROR
** if the hash does not match.
*/
int hname_verify_file_hash(const char *zFile, const char *zHash, int nHash){
  int id = HNAME_ERROR;
  switch( nHash ){
    case HNAME_LEN_SHA1: {
      Blob hash;
      sha1sum_file(zFile, &hash);
      if( memcmp(blob_buffer(&hash),zHash,HNAME_LEN_SHA1)==0 ) id = HNAME_SHA1;
      blob_reset(&hash);
      break;
    }
    case HNAME_LEN_K224:
    case HNAME_LEN_K256: {
      Blob hash;
      sha3sum_file(zFile, nHash*4, &hash);
      if( memcmp(blob_buffer(&hash),zHash,nHash)==0 ){
        id = nHash==HNAME_LEN_K224 ? HNAME_K224 : HNAME_K256;
      }
      blob_reset(&hash);
      break;
    }
  }
  return id;
}
