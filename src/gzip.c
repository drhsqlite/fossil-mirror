/*
** Copyright (c) 2011 D. Richard Hipp
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
** This file contains code used to incrementally generate a GZIP compressed
** file.  The GZIP format is described in RFC-1952.
**
** State information is stored in static variables, so this implementation
** can only be building up a single GZIP file at a time.
*/
#include "config.h"
#include <assert.h>
#if defined(FOSSIL_ENABLE_MINIZ)
#  define MINIZ_HEADER_FILE_ONLY
#  include "miniz.c"
#else
#  include <zlib.h>
#endif
#include "gzip.h"

/*
** State information for the GZIP file under construction.
*/
struct gzip_state {
  int eState;           /* 0: idle   1: header  2: compressing */
  int iCRC;             /* The checksum */
  z_stream stream;      /* The working compressor */
  Blob out;             /* Results stored here */
} gzip;

/*
** Write a 32-bit integer as little-endian into the given buffer.
*/
static void put32(char *z, int v){
  z[0] = v & 0xff;
  z[1] = (v>>8) & 0xff;
  z[2] = (v>>16) & 0xff;
  z[3] = (v>>24) & 0xff;
}

/*
** Begin constructing a gzip file.
*/
void gzip_begin(sqlite3_int64 now){
  char aHdr[10];
  assert( gzip.eState==0 );
  blob_zero(&gzip.out);
  aHdr[0] = 0x1f;
  aHdr[1] = 0x8b;
  aHdr[2] = 8;
  aHdr[3] = 0;
  if( now==-1 ){
    now = db_int64(0, "SELECT (julianday('now') - 2440587.5)*86400.0");
  }
  put32(&aHdr[4], now&0xffffffff);
  aHdr[8] = 2;
  aHdr[9] = -1;
  blob_append(&gzip.out, aHdr, 10);
  gzip.iCRC = 0;
  gzip.eState = 1;
}

/*
** Add nIn bytes of content from pIn to the gzip file.
*/
#define GZIP_BUFSZ 100000
void gzip_step(const char *pIn, int nIn){
  char *zOutBuf;
  int nOut;

  nOut = nIn + nIn/10 + 100;
  if( nOut<100000 ) nOut = 100000;
  zOutBuf = fossil_malloc(nOut);
  gzip.stream.avail_in = nIn;
  gzip.stream.next_in = (unsigned char*)pIn;
  gzip.stream.avail_out = nOut;
  gzip.stream.next_out = (unsigned char*)zOutBuf;
  if( gzip.eState==1 ){
    gzip.stream.zalloc = (alloc_func)0;
    gzip.stream.zfree = (free_func)0;
    gzip.stream.opaque = 0;
    deflateInit2(&gzip.stream, 9, Z_DEFLATED, -MAX_WBITS,8,Z_DEFAULT_STRATEGY);
    gzip.eState = 2;
  }
  gzip.iCRC = crc32(gzip.iCRC, gzip.stream.next_in, gzip.stream.avail_in);
  do{
    deflate(&gzip.stream, nIn==0 ? Z_FINISH : 0);
    blob_append(&gzip.out, zOutBuf, nOut - gzip.stream.avail_out);
    gzip.stream.avail_out = nOut;
    gzip.stream.next_out = (unsigned char*)zOutBuf;
  }while( gzip.stream.avail_in>0 );
  fossil_free(zOutBuf);
}

/*
** Finish the gzip file and put the content in *pOut
*/
void gzip_finish(Blob *pOut){
  char aTrailer[8];
  assert( gzip.eState>0 );
  gzip_step("", 0);
  deflateEnd(&gzip.stream);
  put32(aTrailer, gzip.iCRC);
  put32(&aTrailer[4], gzip.stream.total_in);
  blob_append(&gzip.out, aTrailer, 8);
  *pOut = gzip.out;
  blob_zero(&gzip.out);
  gzip.eState = 0;
}

/*
** COMMAND: test-gzip
**
** Usage: %fossil test-gzip FILENAME
**
** Compress a file using gzip.
*/
void test_gzip_cmd(void){
  Blob b;
  char *zOut;
  if( g.argc!=3 ) usage("FILENAME");
  sqlite3_open(":memory:", &g.db);
  gzip_begin(-1);
  blob_read_from_file(&b, g.argv[2]);
  zOut = mprintf("%s.gz", g.argv[2]);
  gzip_step(blob_buffer(&b), blob_size(&b));
  blob_reset(&b);
  gzip_finish(&b);
  blob_write_to_file(&b, zOut);
  blob_reset(&b);
  fossil_free(zOut);
}
