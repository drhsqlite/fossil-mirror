/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to clear-sign documents using an
** external gpg command.
*/
#include "config.h"
#include "clearsign.h"
#include <assert.h>

/*
** Clearsign the given blob.  Put the signed version in
** pOut.
*/
int clearsign(Blob *pIn, Blob *pOut){
  char *zRand;
  char *zIn;
  char *zOut;
  char *zBase = db_get("pgp-command", "gpg --clearsign -o ");
  int useSsh = 0;
  char *zCmd;
  int rc;
  if( is_false(zBase) ){
    return 0;
  }
  zRand = db_text(0, "SELECT hex(randomblob(10))");
  zOut = mprintf("out-%s", zRand);
  blob_write_to_file(pIn, zOut);
  useSsh = (fossil_strncmp(command_basename(zBase), "ssh", 3)==0);
  if( useSsh ){
    zIn = mprintf("out-%s.sig", zRand);
    zCmd = mprintf("%s %s", zBase, zOut);
  }else{
    zIn = mprintf("in-%z", zRand);
    zCmd = mprintf("%s %s %s", zBase, zIn, zOut);
  }
  rc = fossil_system(zCmd);
  free(zCmd);
  if( rc==0 ){
    if( pOut==pIn ){
      blob_reset(pIn);
    }
    blob_zero(pOut);
    if( useSsh ){
        /* As of 2025, SSH cannot create non-detached SSH signatures */
        /* We put one together */
        Blob tmpBlob;
        blob_zero(&tmpBlob);
        blob_read_from_file(&tmpBlob, zOut, ExtFILE);
        /* Add armor header line and manifest */
        blob_appendf(pOut, "%s", "-----BEGIN SSH SIGNED MESSAGE-----\n\n");
        blob_appendf(pOut, "%s", blob_str(&tmpBlob));
        blob_zero(&tmpBlob);
        blob_read_from_file(&tmpBlob, zIn, ExtFILE);
        /* Add signature - already armored by SSH */
        blob_appendb(pOut, &tmpBlob);
    }else{
      /* Assume that the external command creates non-detached signatures */
      blob_read_from_file(pOut, zIn, ExtFILE);
    }
  }else{
    if( pOut!=pIn ){
      blob_copy(pOut, pIn);
    }
  }
  file_delete(zOut);
  file_delete(zIn);
  free(zOut);
  free(zIn);
  return rc;
}
