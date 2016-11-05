/*
** Copyright (c) 2006 D. Richard Hipp
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
** This module implements the interface to the delta generator.
*/
#include "config.h"
#include "deltacmd.h"

/*
** Create a delta that describes the change from pOriginal to pTarget
** and put that delta in pDelta.  The pDelta blob is assumed to be
** uninitialized.
*/
int blob_delta_create(Blob *pOriginal, Blob *pTarget, Blob *pDelta){
  const char *zOrig, *zTarg;
  int lenOrig, lenTarg;
  int len;
  char *zRes;
  blob_zero(pDelta);
  zOrig = blob_materialize(pOriginal);
  lenOrig = blob_size(pOriginal);
  zTarg = blob_materialize(pTarget);
  lenTarg = blob_size(pTarget);
  blob_resize(pDelta, lenTarg+16);
  zRes = blob_materialize(pDelta);
  len = delta_create(zOrig, lenOrig, zTarg, lenTarg, zRes);
  blob_resize(pDelta, len);
  return 0;
}

/*
** COMMAND: test-delta-create
**
** Usage: %fossil test-delta-create FILE1 FILE2 DELTA
**
** Create and output a delta that carries FILE1 into FILE2.
** Store the result in DELTA.
*/
void delta_create_cmd(void){
  Blob orig, target, delta;
  if( g.argc!=5 ){
    usage("ORIGIN TARGET DELTA");
  }
  if( blob_read_from_file(&orig, g.argv[2])<0 ){
    fossil_fatal("cannot read %s", g.argv[2]);
  }
  if( blob_read_from_file(&target, g.argv[3])<0 ){
    fossil_fatal("cannot read %s", g.argv[3]);
  }
  blob_delta_create(&orig, &target, &delta);
  if( blob_write_to_file(&delta, g.argv[4])<blob_size(&delta) ){
    fossil_fatal("cannot write %s", g.argv[4]);
  }
  blob_reset(&orig);
  blob_reset(&target);
  blob_reset(&delta);
}

/*
** COMMAND: test-delta-analyze
**
** Usage: %fossil test-delta-analyze FILE1 FILE2
**
** Create and a delta that carries FILE1 into FILE2.  Print the
** number bytes copied and the number of bytes inserted.
*/
void delta_analyze_cmd(void){
  Blob orig, target, delta;
  int nCopy = 0;
  int nInsert = 0;
  int sz1, sz2, sz3;
  if( g.argc!=4 ){
    usage("ORIGIN TARGET");
  }
  if( blob_read_from_file(&orig, g.argv[2])<0 ){
    fossil_fatal("cannot read %s", g.argv[2]);
  }
  if( blob_read_from_file(&target, g.argv[3])<0 ){
    fossil_fatal("cannot read %s", g.argv[3]);
  }
  blob_delta_create(&orig, &target, &delta);
  delta_analyze(blob_buffer(&delta), blob_size(&delta), &nCopy, &nInsert);
  sz1 = blob_size(&orig);
  sz2 = blob_size(&target);
  sz3 = blob_size(&delta);
  blob_reset(&orig);
  blob_reset(&target);
  blob_reset(&delta);
  fossil_print("original size:  %8d\n", sz1);
  fossil_print("bytes copied:   %8d (%.2f%% of target)\n",
               nCopy, (100.0*nCopy)/sz2);
  fossil_print("bytes inserted: %8d (%.2f%% of target)\n",
               nInsert, (100.0*nInsert)/sz2);
  fossil_print("final size:     %8d\n", sz2);
  fossil_print("delta size:     %8d\n", sz3);
}

/*
** Apply the delta in pDelta to the original file pOriginal to generate
** the target file pTarget.  The pTarget blob is initialized by this
** routine.
**
** It works ok for pTarget and pOriginal to be the same blob.
**
** Return the length of the target.  Return -1 if there is an error.
*/
int blob_delta_apply(Blob *pOriginal, Blob *pDelta, Blob *pTarget){
  int len, n;
  Blob out;

  n = delta_output_size(blob_buffer(pDelta), blob_size(pDelta));
  blob_zero(&out);
  if( n<0 ) return -1;
  blob_resize(&out, n);
  len = delta_apply(
     blob_buffer(pOriginal), blob_size(pOriginal),
     blob_buffer(pDelta), blob_size(pDelta),
     blob_buffer(&out));
  if( len<0 ){
    blob_reset(&out);
  }else if( len!=n ){
    blob_resize(&out, len);
  }
  if( pTarget==pOriginal ){
    blob_reset(pOriginal);
  }
  *pTarget = out;
  return len;
}

/*
** COMMAND: test-delta-apply
**
** Usage: %fossil test-delta-apply FILE1 DELTA
**
** Apply DELTA to FILE1 and output the result.
*/
void delta_apply_cmd(void){
  Blob orig, target, delta;
  if( g.argc!=5 ){
    usage("ORIGIN DELTA TARGET");
  }
  if( blob_read_from_file(&orig, g.argv[2])<0 ){
    fossil_fatal("cannot read %s", g.argv[2]);
  }
  if( blob_read_from_file(&delta, g.argv[3])<0 ){
    fossil_fatal("cannot read %s", g.argv[3]);
  }
  blob_delta_apply(&orig, &delta, &target);
  if( blob_write_to_file(&target, g.argv[4])<blob_size(&target) ){
    fossil_fatal("cannot write %s", g.argv[4]);
  }
  blob_reset(&orig);
  blob_reset(&target);
  blob_reset(&delta);
}


/*
** COMMAND: test-delta
**
** Usage: %fossil test-delta FILE1 FILE2
**
** Read two files named on the command-line.  Create and apply deltas
** going in both directions.  Verify that the original files are
** correctly recovered.
*/
void cmd_test_delta(void){
  Blob f1, f2;     /* Original file content */
  Blob d12, d21;   /* Deltas from f1->f2 and f2->f1 */
  Blob a1, a2;     /* Recovered file content */
  if( g.argc!=4 ) usage("FILE1 FILE2");
  blob_read_from_file(&f1, g.argv[2]);
  blob_read_from_file(&f2, g.argv[3]);
  blob_delta_create(&f1, &f2, &d12);
  blob_delta_create(&f2, &f1, &d21);
  blob_delta_apply(&f1, &d12, &a2);
  blob_delta_apply(&f2, &d21, &a1);
  if( blob_compare(&f1,&a1) || blob_compare(&f2, &a2) ){
    fossil_fatal("delta test failed");
  }
  fossil_print("ok\n");
}
