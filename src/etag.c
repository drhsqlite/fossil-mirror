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
** An ETag is a hash that encodes attributes which must be the same for
** the page to continue to be valid.  Attributes that might be contained
** in the ETag include:
**
**   (1)  The mtime on the Fossil executable
**   (2)  The last change to the CONFIG table
**   (3)  The last change to the EVENT table
**   (4)  The value of the display cookie
**   (5)  A hash value supplied by the page generator
**
** Item (1) is always included in the ETag.  The other elements are
** optional.  Because (1) is always included as part of the ETag, all
** outstanding ETags can be invalidated by touching the fossil executable.
**
** A page generator routine invokes etag_check() exactly once, with
** arguments that indicates which of the above elements to include in the
** hash.  If the request contained an If-None-Match header which matches
** the generated ETag, then a 304 Not Modified reply is generated and
** the process exits.  In other words, etag_check() never returns.  But
** if there is no If-None_Match header or if the ETag does not match,
** then etag_check() returns normally.  Later, during reply generation,
** the cgi.c module will invoke etag_tag() to recover the generated tag
** and include it in the reply header.
**
** 2018-02-25:
**
** Also support Last-Modified: and If-Modified-Since:.  The
** etag_last_modified(mtime) API records a timestamp for the page in
** seconds since 1970.  This causes a Last-Modified: header to be
** issued in the reply.  Or, if the request contained If-Modified-Since:
** and the new mtime is not greater than the mtime associated with
** If-Modified-Since, then a 304 Not Modified reply is generated and
** the etag_last_modified() API never returns.
*/
#include "config.h"
#include "etag.h"

#if INTERFACE
/*
** Things to monitor
*/
#define ETAG_CONFIG   0x01 /* Output depends on the CONFIG table */
#define ETAG_DATA     0x02 /* Output depends on the EVENT table */
#define ETAG_COOKIE   0x04 /* Output depends on a display cookie value */
#define ETAG_HASH     0x08 /* Output depends on a hash */
#endif

static char zETag[33];      /* The generated ETag */
static int iMaxAge = 0;     /* The max-age parameter in the reply */
static sqlite3_int64 iEtagMtime = 0;  /* Last-Modified time */

/*
** Generate an ETag
*/
void etag_check(unsigned eFlags, const char *zHash){
  sqlite3_int64 mtime;
  const char *zIfNoneMatch;
  char zBuf[50];
  assert( zETag[0]==0 );  /* Only call this routine once! */

  iMaxAge = 86400;
  md5sum_init();

  /* Always include the mtime of the executable as part of the hash */
  mtime = file_mtime(g.nameOfExe, ExtFILE);
  sqlite3_snprintf(sizeof(zBuf),zBuf,"mtime: %lld\n", mtime);
  md5sum_step_text(zBuf, -1);
  
  if( (eFlags & ETAG_HASH)!=0 && zHash ){
    md5sum_step_text("hash: ", -1);
    md5sum_step_text(zHash, -1);
    md5sum_step_text("\n", 1);
    iMaxAge = 0;
  }else if( eFlags & ETAG_DATA ){
    int iKey = db_int(0, "SELECT max(rcvid) FROM rcvfrom");
    sqlite3_snprintf(sizeof(zBuf),zBuf,"%d",iKey);
    md5sum_step_text("data: ", -1);
    md5sum_step_text(zBuf, -1);
    md5sum_step_text("\n", 1);
    iMaxAge = 60;
  }else if( eFlags & ETAG_CONFIG ){
    int iKey = db_int(0, "SELECT value FROM config WHERE name='cfgcnt'");
    sqlite3_snprintf(sizeof(zBuf),zBuf,"%d",iKey);
    md5sum_step_text("config: ", -1);
    md5sum_step_text(zBuf, -1);
    md5sum_step_text("\n", 1);
    iMaxAge = 3600;
  }

  /* Include the display cookie */
  if( eFlags & ETAG_COOKIE ){
    md5sum_step_text("display-cookie: ", -1);
    md5sum_step_text(PD(DISPLAY_SETTINGS_COOKIE,""), -1);
    md5sum_step_text("\n", 1);
    iMaxAge = 0;
  }

  /* Generate the ETag */
  memcpy(zETag, md5sum_finish(0), 33);

  /* Check to see if the generated ETag matches If-None-Match and
  ** generate a 304 reply if it does. */
  zIfNoneMatch = P("HTTP_IF_NONE_MATCH");
  if( zIfNoneMatch==0 ) return;
  if( strcmp(zIfNoneMatch,zETag)!=0 ) return;

  /* If we get this far, it means that the content has
  ** not changed and we can do a 304 reply */
  cgi_reset_content();
  cgi_set_status(304, "Not Modified");
  cgi_reply();
  db_close(0);
  fossil_exit(0);
}

/*
** Accept a new Last-Modified time.  This routine should be called by
** page generators that know a valid last-modified time.  This routine
** might generate a 304 Not Modified reply and exit(), never returning.
** Or, if not, it will cause a Last-Modified: header to be included in the
** reply.
*/
void etag_last_modified(sqlite3_int64 mtime){
  const char *zIfModifiedSince;
  sqlite3_int64 x;
  assert( iEtagMtime==0 );   /* Only call this routine once */
  assert( mtime>0 );         /* Only call with a valid mtime */
  iEtagMtime = mtime;

  /* Check to see the If-Modified-Since constraint is satisfied */
  zIfModifiedSince = P("HTTP_IF_MODIFIED_SINCE");
  if( zIfModifiedSince==0 ) return;
  x = cgi_rfc822_parsedate(zIfModifiedSince);
  if( x<mtime ) return;

#if 0  
  /* If the Fossil executable is more recent than If-Modified-Since,
  ** go ahead and regenerate the resource. */
  if( file_mtime(g.nameOfExe, ExtFILE)>x ) return;
#endif

  /* If we reach this point, it means that the resource has not changed
  ** and that we should generate a 304 Not Modified reply */
  cgi_reset_content();
  cgi_set_status(304, "Not Modified");
  cgi_reply();
  db_close(0);
  fossil_exit(0);
}

/* Return the ETag, if there is one.
*/
const char *etag_tag(void){
  return zETag;
}

/* Return the recommended max-age
*/
int etag_maxage(void){
  return iMaxAge;
}

/* Return the last-modified time in seconds since 1970.  Or return 0 if
** there is no last-modified time.
*/
sqlite3_int64 etag_mtime(void){
  return iEtagMtime;
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
  const char *zHash = 0;
  const char *zKey;
  int iKey = 0;
  db_find_and_open_repository(0, 0);
  zKey = find_option("key",0,1);
  zHash = find_option("hash",0,1);
  if( zKey ) iKey = atoi(zKey);
  etag_check(iKey, zHash);
  fossil_print("%s\n", etag_tag());
}
