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
** interface logic.
**
** "hname" is intended to be an abbreviation of "hash name".
*/
#include "config.h"
#include "hname.h"


#if INTERFACE
/*
** Code numbers for the allowed hash algorithms.
*/
#define HNAME_ERROR  0      /* Not a valid hash */
#define HNAME_SHA1   1      /* SHA1 */
#define HNAME_K256   2      /* SHA3-256 */

/*
** Minimum and maximum lengths for a hash value when hex encoded.
*/
#define HNAME_MIN  40     /* Length for SHA1 */
#define HNAME_MAX  64     /* Length for SHA3-256 */

/*
** Hash lengths for the various algorithms
*/
#define HNAME_LEN_SHA1   40
#define HNAME_LEN_K256   64

/*
** The number of distinct hash algorithms:
*/
#define HNAME_COUNT 2     /* Just SHA1 and SHA3-256. Let's keep it that way! */

/*
** Hash naming policies
*/
#define HPOLICY_SHA1           0      /* Use SHA1 hashes */
#define HPOLICY_AUTO           1      /* SHA1 but auto-promote to SHA3 */
#define HPOLICY_SHA3           2      /* Use SHA3 hashes */
#define HPOLICY_SHA3_ONLY      3      /* Use SHA3 hashes exclusively */
#define HPOLICY_SHUN_SHA1      4      /* Shun all SHA1 objects */

#endif /* INTERFACE */

/*
** Return a human-readable name for the hash algorithm given a hash with
** a length of nHash hexadecimal digits.
*/
const char *hname_alg(int nHash){
  if( nHash==HNAME_LEN_SHA1 ) return "SHA1";
  if( nHash==HNAME_LEN_K256 ) return "SHA3-256";
  return "?";
}

/*
** Return the integer hash algorithm code number (ex: HNAME_K256) for
** the hash string provided.  Or return HNAME_ERROR (0) if the input string
** is not a valid artifact hash string.
*/
int hname_validate(const char *zHash, int nHash){
  int id;
  switch( nHash ){
    case HNAME_LEN_SHA1:   id = HNAME_SHA1;  break;
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
** (Examples: HNAME_SHA1 or HNAME_K256).  And the return is HNAME_ERROR
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
    case HNAME_LEN_K256: {
      sha3sum_init(256);
      sha3sum_step_blob(pContent);
      if( memcmp(sha3sum_finish(0),zHash,64)==0 ) id = HNAME_K256;
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
** (Examples: HNAME_SHA1 or HNAME_K256).  And the return is HNAME_ERROR
** if the hash does not match.
*/
int hname_verify_file_hash(const char *zFile, const char *zHash, int nHash){
  int id = HNAME_ERROR;
  switch( nHash ){
    case HNAME_LEN_SHA1: {
      Blob hash;
      if( sha1sum_file(zFile, &hash) ) break;
      if( memcmp(blob_buffer(&hash),zHash,HNAME_LEN_SHA1)==0 ) id = HNAME_SHA1;
      blob_reset(&hash);
      break;
    }
    case HNAME_LEN_K256: {
      Blob hash;
      if( sha3sum_file(zFile, 256, &hash) ) break;
      if( memcmp(blob_buffer(&hash),zHash,64)==0 ) id = HNAME_LEN_K256;
      blob_reset(&hash);
      break;
    }
  }
  return id;
}

/*
** Compute a hash on blob pContent.  Write the hash into blob pHashOut.
** This routine assumes that pHashOut is uninitialized.
**
** The preferred hash is used for iHType==0 and the alternative hash is
** used if iHType==1.  (The interface is designed to accommodate more than
** just two hashes, but HNAME_COUNT is currently fixed at 2.)
**
** Depending on the hash policy, the alternative hash may be disallowed.
** If the alterative hash is disallowed, the routine returns 0.  This
** routine returns 1 if iHType>0 and the alternative hash is allowed,
** and it always returns 1 when iHType==0.
**
** Alternative hash is disallowed for all hash policies except auto,
** sha1 and sha3.
*/
int hname_hash(const Blob *pContent, unsigned int iHType, Blob *pHashOut){
  assert( iHType==0 || iHType==1 );
  if( iHType==1 ){
    switch( g.eHashPolicy ){
      case HPOLICY_AUTO:
      case HPOLICY_SHA1:
        sha3sum_blob(pContent, 256, pHashOut);
        return 1;
      case HPOLICY_SHA3:
        sha1sum_blob(pContent, pHashOut);
        return 1;
    }
  }
  if( iHType==0 ){
    switch( g.eHashPolicy ){
      case HPOLICY_SHA1:
      case HPOLICY_AUTO:
        sha1sum_blob(pContent, pHashOut);
        return 1;
      case HPOLICY_SHA3:
      case HPOLICY_SHA3_ONLY:
      case HPOLICY_SHUN_SHA1:
        sha3sum_blob(pContent, 256, pHashOut);
        return 1;
    }
  }
  blob_init(pHashOut, 0, 0);
  return 0;
}

/*
** Return the default hash policy for repositories that do not currently
** have an assigned hash policy.
**
** Make the default HPOLICY_AUTO if there are SHA1 artficates but no SHA3
** artifacts in the repository.  Make the default HPOLICY_SHA3 if there
** are one or more SHA3 artifacts or if the repository is initially empty.
*/
int hname_default_policy(void){
  if( db_exists("SELECT 1 FROM blob WHERE length(uuid)>40")
   || !db_exists("SELECT 1 FROM blob WHERE length(uuid)==40")
  ){
    return HPOLICY_SHA3;
  }else{
    return HPOLICY_AUTO;
  }
}

/*
** Names of the hash policies.
*/
static const char *azPolicy[] = {
  "sha1", "auto", "sha3", "sha3-only", "shun-sha1"
};

/* Return the name of the current hash policy.
*/
const char *hpolicy_name(void){
  return azPolicy[g.eHashPolicy];
}


/*
** COMMAND: hash-policy*
**
** Usage: fossil hash-policy ?NEW-POLICY?
**
** Query or set the hash policy for the current repository.  Available hash
** policies are as follows:
**
**   sha1              New artifact names are created using SHA1
**
**   auto              New artifact names are created using SHA1, but
**                     automatically change the policy to "sha3" when
**                     any SHA3 artifact enters the repository.
**
**   sha3              New artifact names are created using SHA3, but
**                     older artifacts with SHA1 names may be reused.
**
**   sha3-only         Use only SHA3 artifact names.  Do not reuse legacy
**                     SHA1 names.
**
**   shun-sha1         Shun any SHA1 artifacts received by sync operations
**                     other than clones.  Older legacy SHA1 artifacts are
**                     allowed during a clone.
**
** The default hash policy for existing repositories is "auto", which will
** immediately promote to "sha3" if the repository contains one or more
** artifacts with SHA3 names.  The default hash policy for new repositories
** is "shun-sha1".
*/
void hash_policy_command(void){
  int i;
  db_find_and_open_repository(0, 0);
  if( g.argc!=2 && g.argc!=3 ) usage("?NEW-POLICY?");
  if( g.argc==2 ){
    fossil_print("%s\n", azPolicy[g.eHashPolicy]);
    return;
  }
  for(i=HPOLICY_SHA1; i<=HPOLICY_SHUN_SHA1; i++){
    if( fossil_strcmp(g.argv[2],azPolicy[i])==0 ){
      if( i==HPOLICY_AUTO
       && db_exists("SELECT 1 FROM blob WHERE length(uuid)>40")
      ){
        i = HPOLICY_SHA3;
      }
      g.eHashPolicy = i;
      db_set_int("hash-policy", i, 0);
      fossil_print("%s\n", azPolicy[i]);
      return;
    }
  }
  fossil_fatal("unknown hash policy \"%s\" - should be one of: sha1 auto"
               " sha3 sha3-only shun-sha1", g.argv[2]);
}
