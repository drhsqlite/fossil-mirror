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
#define HNAME_NONE   (-1)   /* Not a valid hash */
#define HNAME_SHA1   0      /* SHA1 */
#define HNAME_K224   1      /* SHA3-224 */
#define HNAME_K256   2      /* SHA3-256 */

/*
** Minimum and maximum lengths for a hash value when hex encoded.
*/
#define HNAME_LEN_MIN  40     /* Length for SHA1 */
#define HNAME_LEN_MAX  64     /* Length for SHA3-256 */

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
  if( aid==HNAME_K256 ) return "SHA3-256";
  return "SHA1";
}

/*
** Given a hash algorithm name, return its appropriate number.  Return -1
** if the name is unknown.
*/
int hname_algid(const char *zName){
  if( fossil_stricmp(zName,"sha1")==0 ) return HNAME_SHA1;
  if( fossil_stricmp(zName,"sha3-224")==0 ) return HNAME_K224;
  if( fossil_stricmp(zName,"sha3-256")==0 ) return HNAME_K256;
  return -1;
}

/*
** Return the integer hash algorithm code number (ex: HNAME_K224) for
** the hash string provided.  Or return HNAME_NONE if the input string
** is not a valid artifact hash string.
*/
int hname_validate(const char *zHash, int nHash){
  int id;
  switch( nHash ){
    case HNAME_LEN_SHA1:   id = HNAME_SHA1;  break;
    case HNAME_LEN_K224:   id = HNAME_K224;  break;
    case HNAME_LEN_K256:   id = HNAME_K256;  break;
    default:               return HNAME_NONE;
  }
  if( !validate16(zHash, nHash) ) return HNAME_NONE;
  return id;
}
