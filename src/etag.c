/*
** Copyright (c) 2018 D. Richard Hipp
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
** This file implements ETags: cache control for Fossil
**
** Each ETag value is a text string that represents a sequence of conditionals
** like this:
**
**    if( executable-has-change ) return;
**    if( database-has-changed ) return;
**    if( display-cookie-"n"-attribute-has-changes ) return;
**    Output "304 Not Modified" message and abort;
**
** In other words, if all conditions specified by the ETag are met, then
** Fossil will return a 304 and avoid doing all the work, and all of the
** bandwidth, associating with regenerating the whole page.
**
** To make use of this feature, page generators can invoke the
** etag_require() interface with mask of ETAG_CONST, ETAG_CONFIG,
** ETAG_DATA, and/or ETAG_COOKIE.  Or it can invoke etag_require_hash()
** with some kind of text hash.
**
** Or, in the WEBPAGE: line for the page generator, extra arguments
** can be added.  "const", "config", "data", and/or "cookie"
**
** ETAG_CONST    const     The reply is always the same for the same
**                         build of the fossil binary.  The content
**                         is independent of the repository.
**
** ETAG_CONFIG   config    The reply is the same as long as the repository
**                         config is constant.
**
** ETAG_DATA     data      The reply is the same as long as no new artifacts
**                         are added to the repository
**
** ETAG_COOKIE   cookie    The reply is the same as long as the display
**                         cookie is unchanged.
**
** Page generator routines can also invoke etag_require_hash(HASH) where
** HASH is some string.  In that case, the reply is the same as long as
** the hash is the same.
*/
#include "config.h"
#include "etag.h"

#if INTERFACE
/*
** Things to monitor
*/
#define ETAG_CONST    0x00 /* Output is independent of database or parameters */
#define ETAG_CONFIG   0x01 /* Output depends on the configuration */
#define ETAG_DATA     0x02 /* Output depends on 'event' table */
#define ETAG_COOKIE   0x04 /* Output depends on a display cookie value */
#define ETAG_HASH     0x08 /* Output depends on a hash */
#define ETAG_DYNAMIC  0x10 /* Output is always different */
#endif

/* Set of all etag requirements */
static int mEtag = 0;           /* Mask of requirements */
static const char *zEHash = 0;  /* Hash value if ETAG_HASH is set */


/* Check an ETag to see if all conditions are valid.  If all conditions are
** valid, then return true.  If any condition is false, return false.
*/
static int etag_valid(const char *zTag){
  int iKey;
  char *zCk;
  int rc;
  int nTag;
  if( zTag==0 || zTag[0]<=0 ) return 0;
  nTag = (int)strlen(zTag);
  if( zTag[0]=='"' && zTag[nTag-1]=='"' ){
    zTag++;
    nTag -= 2;
  }
  iKey = zTag[0] - '0';
  zCk = etag_generate(iKey);
  rc = nTag==(int)strlen(zCk) && strncmp(zCk, zTag, nTag)==0;
  fossil_free(zCk);
  if( rc ) mEtag = iKey;
  return rc;
}

/*
** Check to see if there is an If-None-Match: header that
** matches the current etag settings.  If there is, then
** generate a 304 Not Modified reply.
**
** This routine exits and does not return if the 304 Not Modified
** reply is generated.
**
** If the etag does not match, the routine returns normally.
*/
static void etag_check(void){
  const char *zETag = P("HTTP_IF_NONE_MATCH");
  if( zETag==0 ) return;
  if( !etag_valid(zETag) ) return;

  /* If we get this far, it means that the content has
  ** not changed and we can do a 304 reply */
  cgi_reset_content();
  cgi_set_status(304, "Not Modified");
  cgi_reply();
  fossil_exit(0);
}


/* Add one or more new etag requirements.
**
** Page generator logic invokes one or both of these methods to signal
** under what conditions page generation can be skipped
**
** After each call to these routines, the HTTP_IF_NONE_MATCH cookie
** is checked, and if it contains a compatible ETag, then a
** 304 Not Modified return is generated and execution aborts.  This
** routine does not return if the 304 is generated.
*/
void etag_require(int code){
  if( (mEtag & code)==code ) return;
  mEtag |= code;
  etag_check();
}
void etag_require_hash(const char *zHash){
  if( zHash ){
    zEHash = zHash;
    mEtag = ETAG_HASH;
    etag_check();
  }
}

/* Return an appropriate max-age.
*/
int etag_maxage(void){
  if( mEtag ) return 1;
  return 3600*24;
}

/* Generate an appropriate ETags value that captures all requirements.
** Space is obtained from fossil_malloc().
**
** The argument is the mask of attributes to include in the ETag.
** If the argument is -1 then whatever mask is found from prior
** calls to etag_require() and etag_require_hash() is used.
**
** Format:
**
**    <mask><exec-mtime>/<data-or-config-key>/<cookie>/<hash>
**
** The <mask> is a single-character decimal number that is the mask of
** all required attributes:
**
**     ETAG_CONFIG:    1
**     ETAG_DATA:      2
**     ETAG_COOKIE:    4
**     ETAG_HASH:      8
**
** If ETAG_HASH is present, the others are omitted, so the number is
** never greater than 8.
**
** The <exec-mtime> is the mtime of the Fossil executable.  Since this
** is part of the ETag, it means that recompiling or just "touch"-ing the
** fossil binary is sufficient to invalidate all prior caches.
**
** The other elements are only present if the appropriate mask bits
** appear in the first character.
*/
char *etag_generate(int m){
  Blob x = BLOB_INITIALIZER;
  static int mtime = 0;
  if( m<0 ) m = mEtag;
  if( m & ETAG_DYNAMIC ) return 0;
  if( mtime==0 ) mtime = file_mtime(g.nameOfExe, ExtFILE);
  blob_appendf(&x,"%d%x", m, mtime);
  if( m & ETAG_HASH ){
    blob_appendf(&x, "/%s", zEHash);
  }else if( m & ETAG_DATA ){
    int iKey = db_int(0, "SELECT max(rcvid) FROM rcvfrom");
    blob_appendf(&x, "/%x", iKey);
  }else if( m & ETAG_CONFIG ){
    int iKey = db_int(0, "SELECT value FROM config WHERE name='cfgcnt'");
    blob_appendf(&x, "/%x", iKey);
  }
  if( m & ETAG_COOKIE ){
    blob_appendf(&x, "/%s", P(DISPLAY_SETTINGS_COOKIE));
  }
  return blob_str(&x);
}

/* 
** COMMAND: test-etag
**
** Usage:  fossil test-etag -key KEY-NUMBER  -hash HASH
**
** Generate an etag given a KEY-NUMBER and/or a HASH.
**
** KEY-NUMBER is some combination of:
**
**    1   ETAG_CONFIG   The config table version number
**    2   ETAG_DATA     The event table version number
**    4   ETAG_COOKIE   The display cookie
*/
void test_etag_cmd(void){
  char *zTag;
  const char *zHash;
  const char *zKey;
  db_find_and_open_repository(0, 0);
  zKey = find_option("key",0,1);
  zHash = find_option("hash",0,1);
  if( zKey ) etag_require(atoi(zKey));
  if( zHash ) etag_require_hash(zHash);
  zTag = etag_generate(mEtag);
  fossil_print("%s\n", zTag);
  fossil_free(zTag);
}
