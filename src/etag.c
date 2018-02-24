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
#define ETAG_DYNAMIC  0x08 /* Output is always different */
#endif

/* Set of all etag requirements */
static int mEtag = 0;

/* Add one or more new etag requirements */
void etag_require(int code){
  mEtag |= code;
}

/* Return an appropriate max-age */
int etag_maxage(void){
  if( mEtag ) return 1;
  return 3600*24;
}

/* Generate an appropriate ETags value that captures all requirements.
** Space is obtained from fossil_malloc().
*/
char *etag_generate(int m){
  Blob x = BLOB_INITIALIZER;
  int mtime;
  if( m<0 ) m = mEtag;
  if( m & ETAG_DYNAMIC ) return 0;
  mtime = file_mtime(g.nameOfExe, ExtFILE);
  blob_appendf(&x,"%d%x", m, mtime);
  if( m & ETAG_DATA ){
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
*/
void test_etag_cmd(void){
  int iKey;
  char *zTag;
  db_find_and_open_repository(0, 0);
  iKey = g.argc>2 ? atoi(g.argv[2]) : 0;
  zTag = etag_generate(iKey);
  fossil_print("%s\n", zTag);
  fossil_free(zTag);
}

/* Check an ETag to see if all conditions are valid.  If all conditions are
** valid, then return true.
*/
int etag_valid(const char *zTag){
  int iKey;
  char *zCk;
  int rc;
  int nTag;
  if( zTag==0 || zTag[0]<=0 || zTag[0]>=5 ) return 0;
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
