/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code to implement the file transfer protocol.
*/
#include "config.h"
#include "xfer.h"

#include <time.h>

/*
** Maximum number of HTTP redirects that any http_exchange() call will
** follow before throwing a fatal error. Most browsers use a limit of 20.
*/
#define MAX_REDIRECTS 20

/*
** This structure holds information about the current state of either
** a client or a server that is participating in xfer.
*/
typedef struct Xfer Xfer;
struct Xfer {
  Blob *pIn;          /* Input text from the other side */
  Blob *pOut;         /* Compose our reply here */
  Blob line;          /* The current line of input */
  Blob aToken[6];     /* Tokenized version of line */
  Blob err;           /* Error message text */
  int nToken;         /* Number of tokens in line */
  int nIGotSent;      /* Number of "igot" cards sent */
  int nGimmeSent;     /* Number of gimme cards sent */
  int nFileSent;      /* Number of files sent */
  int nDeltaSent;     /* Number of deltas sent */
  int nFileRcvd;      /* Number of files received */
  int nDeltaRcvd;     /* Number of deltas received */
  int nDanglingFile;  /* Number of dangling deltas received */
  int mxSend;         /* Stop sending "file" when pOut reaches this size */
  int resync;         /* Send igot cards for all holdings */
  u8 syncPrivate;     /* True to enable syncing private content */
  u8 nextIsPrivate;   /* If true, next "file" received is a private */
  u32 clientVersion;  /* Version of the client software */
  time_t maxTime;     /* Time when this transfer should be finished */
};


/*
** The input blob contains an artifact.  Convert it into a record ID.
** Create a phantom record if no prior record exists and
** phantomize is true.
**
** Compare to uuid_to_rid().  This routine takes a blob argument
** and does less error checking.
*/
static int rid_from_uuid(Blob *pUuid, int phantomize, int isPrivate){
  static Stmt q;
  int rid;

  db_static_prepare(&q, "SELECT rid FROM blob WHERE uuid=:uuid");
  db_bind_str(&q, ":uuid", pUuid);
  if( db_step(&q)==SQLITE_ROW ){
    rid = db_column_int(&q, 0);
  }else{
    rid = 0;
  }
  db_reset(&q);
  if( rid==0 && phantomize ){
    rid = content_new(blob_str(pUuid), isPrivate);
  }
  return rid;
}

/*
** Remember that the other side of the connection already has a copy
** of the file rid.
*/
static void remote_has(int rid){
  if( rid ){
    static Stmt q;
    db_static_prepare(&q, "INSERT OR IGNORE INTO onremote VALUES(:r)");
    db_bind_int(&q, ":r", rid);
    db_step(&q);
    db_reset(&q);
  }
}

/*
** The aToken[0..nToken-1] blob array is a parse of a "file" line
** message.  This routine finishes parsing that message and does
** a record insert of the file.
**
** The file line is in one of the following two forms:
**
**      file HASH SIZE \n CONTENT
**      file HASH DELTASRC SIZE \n CONTENT
**
** The content is SIZE bytes immediately following the newline.
** If DELTASRC exists, then the CONTENT is a delta against the
** content of DELTASRC.
**
** If any error occurs, write a message into pErr which has already
** be initialized to an empty string.
**
** Any artifact successfully received by this routine is considered to
** be public and is therefore removed from the "private" table.
*/
static void xfer_accept_file(
  Xfer *pXfer,
  int cloneFlag,
  char **pzUuidList,
  int *pnUuidList
){
  int n;
  int rid;
  int srcid = 0;
  Blob content;
  int isPriv;
  Blob *pUuid;

  isPriv = pXfer->nextIsPrivate;
  pXfer->nextIsPrivate = 0;
  if( pXfer->nToken<3
   || pXfer->nToken>4
   || !blob_is_hname(&pXfer->aToken[1])
   || !blob_is_int(&pXfer->aToken[pXfer->nToken-1], &n)
   || n<0
   || (pXfer->nToken==4 && !blob_is_hname(&pXfer->aToken[2]))
  ){
    blob_appendf(&pXfer->err, "malformed file line");
    return;
  }
  blob_zero(&content);
  blob_extract(pXfer->pIn, n, &content);
  pUuid = &pXfer->aToken[1];
  if( !cloneFlag && uuid_is_shunned(blob_str(pUuid)) ){
    /* Ignore files that have been shunned */
    blob_reset(&content);
    return;
  }
  if( isPriv && !g.perm.Private ){
    /* Do not accept private files if not authorized */
    blob_reset(&content);
    return;
  }
  if( cloneFlag ){
    if( pXfer->nToken==4 ){
      srcid = rid_from_uuid(&pXfer->aToken[2], 1, isPriv);
      pXfer->nDeltaRcvd++;
    }else{
      srcid = 0;
      pXfer->nFileRcvd++;
    }
    rid = content_put_ex(&content, blob_str(pUuid), srcid,
                         0, isPriv);
    Th_AppendToList(pzUuidList, pnUuidList, blob_str(pUuid),
                    blob_size(pUuid));
    remote_has(rid);
    blob_reset(&content);
    return;
  }
  if( pXfer->nToken==4 ){
    Blob src, next;
    srcid = rid_from_uuid(&pXfer->aToken[2], 1, isPriv);
    if( content_get(srcid, &src)==0 ){
      rid = content_put_ex(&content, blob_str(pUuid), srcid,
                           0, isPriv);
      Th_AppendToList(pzUuidList, pnUuidList, blob_str(pUuid),
                      blob_size(pUuid));
      pXfer->nDanglingFile++;
      db_multi_exec("DELETE FROM phantom WHERE rid=%d", rid);
      if( !isPriv ) content_make_public(rid);
      blob_reset(&src);
      blob_reset(&content);
      return;
    }
    pXfer->nDeltaRcvd++;
    blob_delta_apply(&src, &content, &next);
    blob_reset(&src);
    blob_reset(&content);
    content = next;
  }else{
    pXfer->nFileRcvd++;
  }
  if( hname_verify_hash(&content, blob_buffer(pUuid), blob_size(pUuid))==0 ){
    blob_appendf(&pXfer->err, "wrong hash on received artifact: %b", pUuid);
  }
  rid = content_put_ex(&content, blob_str(pUuid), 0, 0, isPriv);
  Th_AppendToList(pzUuidList, pnUuidList, blob_str(pUuid), blob_size(pUuid));
  if( rid==0 ){
    blob_appendf(&pXfer->err, "%s", g.zErrMsg);
    blob_reset(&content);
  }else{
    if( !isPriv ) content_make_public(rid);
    manifest_crosslink(rid, &content, MC_NO_ERRORS);
  }
  assert( blob_is_reset(&content) );
  remote_has(rid);
}

/*
** The aToken[0..nToken-1] blob array is a parse of a "cfile" line
** message.  This routine finishes parsing that message and does
** a record insert of the file.  The difference between "file" and
** "cfile" is that with "cfile" the content is already compressed.
**
** The file line is in one of the following two forms:
**
**      cfile HASH USIZE CSIZE \n CONTENT
**      cfile HASH DELTASRC USIZE CSIZE \n CONTENT
**
** The content is CSIZE bytes immediately following the newline.
** If DELTASRC exists, then the CONTENT is a delta against the
** content of DELTASRC.
**
** The original size of the HASH artifact is USIZE.
**
** If any error occurs, write a message into pErr which has already
** be initialized to an empty string.
**
** Any artifact successfully received by this routine is considered to
** be public and is therefore removed from the "private" table.
*/
static void xfer_accept_compressed_file(
  Xfer *pXfer,
  char **pzUuidList,
  int *pnUuidList
){
  int szC;   /* CSIZE */
  int szU;   /* USIZE */
  int rid;
  int srcid = 0;
  Blob content;
  int isPriv;

  isPriv = pXfer->nextIsPrivate;
  pXfer->nextIsPrivate = 0;
  if( pXfer->nToken<4
   || pXfer->nToken>5
   || !blob_is_hname(&pXfer->aToken[1])
   || !blob_is_int(&pXfer->aToken[pXfer->nToken-2], &szU)
   || !blob_is_int(&pXfer->aToken[pXfer->nToken-1], &szC)
   || szC<0 || szU<0
   || (pXfer->nToken==5 && !blob_is_hname(&pXfer->aToken[2]))
  ){
    blob_appendf(&pXfer->err, "malformed cfile line");
    return;
  }
  if( isPriv && !g.perm.Private ){
    /* Do not accept private files if not authorized */
    return;
  }
  blob_zero(&content);
  blob_extract(pXfer->pIn, szC, &content);
  if( uuid_is_shunned(blob_str(&pXfer->aToken[1])) ){
    /* Ignore files that have been shunned */
    blob_reset(&content);
    return;
  }
  if( pXfer->nToken==5 ){
    srcid = rid_from_uuid(&pXfer->aToken[2], 1, isPriv);
    pXfer->nDeltaRcvd++;
  }else{
    srcid = 0;
    pXfer->nFileRcvd++;
  }
  rid = content_put_ex(&content, blob_str(&pXfer->aToken[1]), srcid,
                       szC, isPriv);
  Th_AppendToList(pzUuidList, pnUuidList, blob_str(&pXfer->aToken[1]),
                  blob_size(&pXfer->aToken[1]));
  remote_has(rid);
  blob_reset(&content);
}

/*
** The aToken[0..nToken-1] blob array is a parse of a "uvfile" line
** message.  This routine finishes parsing that message and adds the
** unversioned file to the "unversioned" table.
**
** The file line is in one of the following two forms:
**
**      uvfile NAME MTIME HASH SIZE FLAGS
**      uvfile NAME MTIME HASH SIZE FLAGS \n CONTENT
**
** If the 0x0001 bit of FLAGS is set, that means the file has been
** deleted, SIZE is zero, the HASH is "-", and the "\n CONTENT" is omitted.
**
** SIZE is the number of bytes of CONTENT.  The CONTENT is uncompressed.
** HASH is the artifact hash of CONTENT.
**
** If the 0x0004 bit of FLAGS is set, that means the CONTENT is omitted.
** The sender might have omitted the content because it is too big to
** transmit, or because it is unchanged and this record exists purely
** to update the MTIME.
*/
static void xfer_accept_unversioned_file(Xfer *pXfer, int isWriter){
  sqlite3_int64 mtime;    /* The MTIME */
  Blob *pHash;            /* The HASH value */
  int sz;                 /* The SIZE */
  int flags;              /* The FLAGS */
  Blob content;           /* The CONTENT */
  Blob x;                 /* Compressed content */
  Stmt q;                 /* SQL statements for comparison and insert */
  int isDelete;           /* HASH is "-" indicating this is a delete */
  int nullContent;        /* True of CONTENT is NULL */
  int iStatus;            /* Result from unversioned_status() */

  pHash = &pXfer->aToken[3];
  if( pXfer->nToken==5
   || !blob_is_filename(&pXfer->aToken[1])
   || !blob_is_int64(&pXfer->aToken[2], &mtime)
   || (!blob_eq(pHash,"-") && !blob_is_hname(pHash))
   || !blob_is_int(&pXfer->aToken[4], &sz)
   || !blob_is_int(&pXfer->aToken[5], &flags)
  ){
    blob_appendf(&pXfer->err, "malformed uvfile line");
    return;
  }
  blob_init(&content, 0, 0);
  blob_init(&x, 0, 0);
  if( sz>0 && (flags & 0x0005)==0 ){
    blob_extract(pXfer->pIn, sz, &content);
    nullContent = 0;
    if( hname_verify_hash(&content, blob_buffer(pHash), blob_size(pHash))==0 ){
      blob_appendf(&pXfer->err, "in uvfile line, HASH does not match CONTENT");
      goto end_accept_unversioned_file;
    }
  }else{
    nullContent = 1;
  }

  /* The isWriter flag must be true in order to land the new file */
  if( !isWriter ) goto end_accept_unversioned_file;

  /* Make sure we have a valid g.rcvid marker */
  content_rcvid_init(0);

  /* Check to see if current content really should be overwritten.  Ideally,
  ** a uvfile card should never have been sent unless the overwrite should
  ** occur.  But do not trust the sender.  Double-check.
  */
  iStatus = unversioned_status(blob_str(&pXfer->aToken[1]), mtime,
                               blob_str(pHash));
  if( iStatus>=3 ) goto end_accept_unversioned_file;

  /* Store the content */
  isDelete = blob_eq(pHash, "-");
  if( isDelete ){
    db_prepare(&q,
      "UPDATE unversioned"
      "   SET rcvid=:rcvid, mtime=:mtime, hash=NULL,"
      "       sz=0, encoding=0, content=NULL"
      " WHERE name=:name"
    );
    db_bind_int(&q, ":rcvid", g.rcvid);
  }else if( iStatus==2 ){
    db_prepare(&q, "UPDATE unversioned SET mtime=:mtime WHERE name=:name");
  }else{
    db_prepare(&q,
      "REPLACE INTO unversioned(name,rcvid,mtime,hash,sz,encoding,content)"
      " VALUES(:name,:rcvid,:mtime,:hash,:sz,:encoding,:content)"
    );
    db_bind_int(&q, ":rcvid", g.rcvid);
    db_bind_text(&q, ":hash", blob_str(pHash));
    db_bind_int(&q, ":sz", blob_size(&content));
    if( !nullContent ){
      blob_compress(&content, &x);
      if( blob_size(&x) < 0.8*blob_size(&content) ){
        db_bind_blob(&q, ":content", &x);
        db_bind_int(&q, ":encoding", 1);
      }else{
        db_bind_blob(&q, ":content", &content);
        db_bind_int(&q, ":encoding", 0);
      }
    }else{
      db_bind_int(&q, ":encoding", 0);
    }
  }
  db_bind_text(&q, ":name", blob_str(&pXfer->aToken[1]));
  db_bind_int64(&q, ":mtime", mtime);
  db_step(&q);
  db_finalize(&q);
  db_unset("uv-hash", 0);

end_accept_unversioned_file:
  blob_reset(&x);
  blob_reset(&content);
}

/*
** Try to send a file as a delta against its parent.
** If successful, return the number of bytes in the delta.
** If we cannot generate an appropriate delta, then send
** nothing and return zero.
**
** Never send a delta against a private artifact.
*/
static int send_delta_parent(
  Xfer *pXfer,            /* The transfer context */
  int rid,                /* record id of the file to send */
  int isPrivate,          /* True if rid is a private artifact */
  Blob *pContent,         /* The content of the file to send */
  Blob *pUuid             /* The HASH of the file to send */
){
  static const char *const azQuery[] = {
    "SELECT pid FROM plink x"
    " WHERE cid=%d"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=pid)",

    "SELECT pid, min(mtime) FROM mlink, event ON mlink.mid=event.objid"
    " WHERE fid=%d"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=pid)"
  };
  int i;
  Blob src, delta;
  int size = 0;
  int srcId = 0;

  for(i=0; srcId==0 && i<count(azQuery); i++){
    srcId = db_int(0, azQuery[i] /*works-like:"%d"*/, rid);
  }
  if( srcId>0
   && (pXfer->syncPrivate || !content_is_private(srcId))
   && content_get(srcId, &src)
  ){
    char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", srcId);
    blob_delta_create(&src, pContent, &delta);
    size = blob_size(&delta);
    if( size>=blob_size(pContent)-50 ){
      size = 0;
    }else if( uuid_is_shunned(zUuid) ){
      size = 0;
    }else{
       if( isPrivate ) blob_append(pXfer->pOut, "private\n", -1);
      blob_appendf(pXfer->pOut, "file %b %s %d\n", pUuid, zUuid, size);
      blob_append(pXfer->pOut, blob_buffer(&delta), size);
    }
    blob_reset(&delta);
    free(zUuid);
    blob_reset(&src);
  }
  return size;
}

/*
** Try to send a file as a native delta.
** If successful, return the number of bytes in the delta.
** If we cannot generate an appropriate delta, then send
** nothing and return zero.
**
** Never send a delta against a private artifact.
*/
static int send_delta_native(
  Xfer *pXfer,            /* The transfer context */
  int rid,                /* record id of the file to send */
  int isPrivate,          /* True if rid is a private artifact */
  Blob *pUuid             /* The HASH of the file to send */
){
  Blob src, delta;
  int size = 0;
  int srcId;

  srcId = db_int(0, "SELECT srcid FROM delta WHERE rid=%d", rid);
  if( srcId>0
   && (pXfer->syncPrivate || !content_is_private(srcId))
  ){
    blob_zero(&src);
    db_blob(&src, "SELECT uuid FROM blob WHERE rid=%d", srcId);
    if( uuid_is_shunned(blob_str(&src)) ){
      blob_reset(&src);
      return 0;
    }
    blob_zero(&delta);
    db_blob(&delta, "SELECT content FROM blob WHERE rid=%d", rid);
    blob_uncompress(&delta, &delta);
    if( isPrivate ) blob_append(pXfer->pOut, "private\n", -1);
    blob_appendf(pXfer->pOut, "file %b %b %d\n",
                pUuid, &src, blob_size(&delta));
    blob_append(pXfer->pOut, blob_buffer(&delta), blob_size(&delta));
    size = blob_size(&delta);
    blob_reset(&delta);
    blob_reset(&src);
  }else{
    size = 0;
  }
  return size;
}

/*
** Push an error message to alert the older client that the repository
** has SHA3 content and cannot be synced or cloned.
*/
static void xfer_cannot_send_sha3_error(Xfer *pXfer){
  blob_appendf(pXfer->pOut,
    "error Fossil\\sversion\\s2.0\\sor\\slater\\srequired.\n"
  );
}


/*
** Send the file identified by rid.
**
** The pUuid can be NULL in which case the correct hash is computed
** from the rid.
**
** Try to send the file as a native delta if nativeDelta is true, or
** as a parent delta if nativeDelta is false.
**
** It should never be the case that rid is a private artifact.  But
** as a precaution, this routine does check on rid and if it is private
** this routine becomes a no-op.
*/
static void send_file(Xfer *pXfer, int rid, Blob *pUuid, int nativeDelta){
  Blob content, uuid;
  int size = 0;
  int isPriv = content_is_private(rid);

  if( pXfer->syncPrivate==0 && isPriv ) return;
  if( db_exists("SELECT 1 FROM onremote WHERE rid=%d", rid) ){
     return;
  }
  blob_zero(&uuid);
  db_blob(&uuid, "SELECT uuid FROM blob WHERE rid=%d AND size>=0", rid);
  if( blob_size(&uuid)==0 ){
    return;
  }
  if( blob_size(&uuid)>HNAME_LEN_SHA1 && pXfer->clientVersion<20000 ){
    xfer_cannot_send_sha3_error(pXfer);
    return;
  }
  if( pUuid ){
    if( blob_compare(pUuid, &uuid)!=0 ){
      blob_reset(&uuid);
      return;
    }
  }else{
    pUuid = &uuid;
  }
  if( uuid_is_shunned(blob_str(pUuid)) ){
    blob_reset(&uuid);
    return;
  }
  if( (pXfer->maxTime != -1 && time(NULL) >= pXfer->maxTime) ||
       pXfer->mxSend<=blob_size(pXfer->pOut) ){
    const char *zFormat = isPriv ? "igot %b 1\n" : "igot %b\n";
    blob_appendf(pXfer->pOut, zFormat /*works-like:"%b"*/, pUuid);
    pXfer->nIGotSent++;
    blob_reset(&uuid);
    return;
  }
  if( nativeDelta ){
    size = send_delta_native(pXfer, rid, isPriv, pUuid);
    if( size ){
      pXfer->nDeltaSent++;
    }
  }
  if( size==0 ){
    content_get(rid, &content);

    if( !nativeDelta && blob_size(&content)>100 ){
      size = send_delta_parent(pXfer, rid, isPriv, &content, pUuid);
    }
    if( size==0 ){
      int size = blob_size(&content);
      if( isPriv ) blob_append(pXfer->pOut, "private\n", -1);
      blob_appendf(pXfer->pOut, "file %b %d\n", pUuid, size);
      blob_append(pXfer->pOut, blob_buffer(&content), size);
      pXfer->nFileSent++;
    }else{
      pXfer->nDeltaSent++;
    }
    blob_reset(&content);
  }
  remote_has(rid);
  blob_reset(&uuid);
#if 0
  if( blob_buffer(pXfer->pOut)[blob_size(pXfer->pOut)-1]!='\n' ){
    blob_append(pXfer->pOut, "\n", 1);
  }
#endif
}

/*
** Send the file identified by rid as a compressed artifact.  Basically,
** send the content exactly as it appears in the BLOB table using
** a "cfile" card.
*/
static void send_compressed_file(Xfer *pXfer, int rid){
  const char *zContent;
  const char *zUuid;
  const char *zDelta;
  int szU;
  int szC;
  int rc;
  int isPrivate;
  int srcIsPrivate;
  static Stmt q1;
  Blob fullContent;

  isPrivate = content_is_private(rid);
  if( isPrivate && pXfer->syncPrivate==0 ) return;
  db_static_prepare(&q1,
    "SELECT uuid, size, content, delta.srcid IN private,"
         "  (SELECT uuid FROM blob WHERE rid=delta.srcid)"
    " FROM blob LEFT JOIN delta ON (blob.rid=delta.rid)"
    " WHERE blob.rid=:rid"
    "   AND blob.size>=0"
    "   AND NOT EXISTS(SELECT 1 FROM shun WHERE shun.uuid=blob.uuid)"
  );
  db_bind_int(&q1, ":rid", rid);
  rc = db_step(&q1);
  if( rc==SQLITE_ROW ){
    zUuid = db_column_text(&q1, 0);
    szU = db_column_int(&q1, 1);
    szC = db_column_bytes(&q1, 2);
    zContent = db_column_raw(&q1, 2);
    srcIsPrivate = db_column_int(&q1, 3);
    zDelta = db_column_text(&q1, 4);
    if( isPrivate ) blob_append(pXfer->pOut, "private\n", -1);
    if( pXfer->clientVersion<20000 && db_column_bytes(&q1,0)!=HNAME_LEN_SHA1 ){
      xfer_cannot_send_sha3_error(pXfer);
      db_reset(&q1);
      return;
    }
    blob_appendf(pXfer->pOut, "cfile %s ", zUuid);
    if( !isPrivate && srcIsPrivate ){
      content_get(rid, &fullContent);
      szU = blob_size(&fullContent);
      blob_compress(&fullContent, &fullContent);
      szC = blob_size(&fullContent);
      zContent = blob_buffer(&fullContent);
      zDelta = 0;
    }
    if( zDelta ){
      blob_appendf(pXfer->pOut, "%s ", zDelta);
      pXfer->nDeltaSent++;
    }else{
      pXfer->nFileSent++;
    }
    blob_appendf(pXfer->pOut, "%d %d\n", szU, szC);
    blob_append(pXfer->pOut, zContent, szC);
    if( blob_buffer(pXfer->pOut)[blob_size(pXfer->pOut)-1]!='\n' ){
      blob_append(pXfer->pOut, "\n", 1);
    }
    if( !isPrivate && srcIsPrivate ){
      blob_reset(&fullContent);
    }
  }
  db_reset(&q1);
}

/*
** Send the unversioned file identified by zName by generating the
** appropriate "uvfile" card.
**
**     uvfile NAME MTIME HASH SIZE FLAGS \n CONTENT
**
** If the noContent flag is set, omit the CONTENT and set the 0x0004
** flag in FLAGS.
*/
static void send_unversioned_file(
  Xfer *pXfer,            /* Transfer context */
  const char *zName,      /* Name of unversioned file to be sent */
  int noContent           /* True to omit the content */
){
  Stmt q1;

  if( blob_size(pXfer->pOut)>=pXfer->mxSend ) noContent = 1;
  if( noContent ){
    db_prepare(&q1,
      "SELECT mtime, hash, encoding, sz FROM unversioned WHERE name=%Q",
      zName
    );
  }else{
    db_prepare(&q1,
      "SELECT mtime, hash, encoding, sz, content FROM unversioned"
      " WHERE name=%Q",
      zName
    );
  }
  if( db_step(&q1)==SQLITE_ROW ){
    sqlite3_int64 mtime = db_column_int64(&q1, 0);
    const char *zHash = db_column_text(&q1, 1);
    if( pXfer->clientVersion<20000 && db_column_bytes(&q1,1)>HNAME_LEN_SHA1 ){
      xfer_cannot_send_sha3_error(pXfer);
      db_reset(&q1);
      return;
    }
    if( blob_size(pXfer->pOut)>=pXfer->mxSend ){
      /* If we have already reached the send size limit, send a (short)
      ** uvigot card rather than a uvfile card.  This only happens on the
      ** server side.  The uvigot card will provoke the client to resend
      ** another uvgimme on the next cycle. */
      blob_appendf(pXfer->pOut, "uvigot %s %lld %s %d\n",
                   zName, mtime, zHash, db_column_int(&q1,3));
    }else{
      blob_appendf(pXfer->pOut, "uvfile %s %lld", zName, mtime);
      if( zHash==0 ){
        blob_append(pXfer->pOut, " - 0 1\n", -1);
      }else if( noContent ){
        blob_appendf(pXfer->pOut, " %s %d 4\n", zHash, db_column_int(&q1,3));
      }else{
        Blob content;
        blob_init(&content, 0, 0);
        db_column_blob(&q1, 4, &content);
        if( db_column_int(&q1, 2) ){
          blob_uncompress(&content, &content);
        }
        blob_appendf(pXfer->pOut, " %s %d 0\n", zHash, blob_size(&content));
        blob_append(pXfer->pOut, blob_buffer(&content), blob_size(&content));
        blob_reset(&content);
      }
    }
  }
  db_finalize(&q1);
}

/*
** Send a gimme message for every phantom.
**
** Except: do not request shunned artifacts.  And do not request
** private artifacts if we are not doing a private transfer.
*/
static void request_phantoms(Xfer *pXfer, int maxReq){
  Stmt q;
  db_prepare(&q,
    "SELECT uuid FROM phantom CROSS JOIN blob USING(rid) /*scan*/"
    " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid) %s",
    (pXfer->syncPrivate ? "" :
         "   AND NOT EXISTS(SELECT 1 FROM private WHERE rid=blob.rid)")
  );
  while( db_step(&q)==SQLITE_ROW && maxReq-- > 0 ){
    const char *zUuid = db_column_text(&q, 0);
    blob_appendf(pXfer->pOut, "gimme %s\n", zUuid);
    pXfer->nGimmeSent++;
  }
  db_finalize(&q);
}

/*
** Compute an hash on the tail of pMsg.  Verify that it matches the
** the hash given in pHash.  Return non-zero for an error and 0 on success.
**
** The type of hash computed (SHA1, SHA3-256) is determined by
** the length of the input hash in pHash.
*/
static int check_tail_hash(Blob *pHash, Blob *pMsg){
  Blob tail;
  int rc;
  blob_tail(pMsg, &tail);
  rc = hname_verify_hash(&tail, blob_buffer(pHash), blob_size(pHash));
  blob_reset(&tail);
  return rc==HNAME_ERROR;
}

/*
** Check the signature on an application/x-fossil payload received by
** the HTTP server.  The signature is a line of the following form:
**
**        login LOGIN NONCE SIGNATURE
**
** The NONCE is the SHA1 hash of the remainder of the input.
** SIGNATURE is the SHA1 checksum of the NONCE concatenated
** with the users password.
**
** The parameters to this routine are ephemeral blobs holding the
** LOGIN, NONCE and SIGNATURE.
**
** This routine attempts to locate the user and verify the signature.
** If everything checks out, the USER.CAP column for the USER table
** is consulted to set privileges in the global g variable.
**
** If anything fails to check out, no changes are made to privileges.
**
** Signature generation on the client side is handled by the
** http_exchange() routine.
**
** Return non-zero for a login failure and zero for success.
*/
int check_login(Blob *pLogin, Blob *pNonce, Blob *pSig){
  Stmt q;
  int rc = -1;
  char *zLogin = blob_terminate(pLogin);
  defossilize(zLogin);

  if( fossil_strcmp(zLogin, "nobody")==0
   || fossil_strcmp(zLogin,"anonymous")==0
  ){
    return 0;   /* Anybody is allowed to sync as "nobody" or "anonymous" */
  }
  if( fossil_strcmp(P("REMOTE_USER"), zLogin)==0
      && db_get_boolean("remote_user_ok",0) ){
    return 0;   /* Accept Basic Authorization */
  }
  db_prepare(&q,
     "SELECT pw, cap, uid FROM user"
     " WHERE login=%Q"
     "   AND login NOT IN ('anonymous','nobody','developer','reader')"
     "   AND length(pw)>0",
     zLogin
  );
  if( db_step(&q)==SQLITE_ROW ){
    int szPw;
    Blob pw, combined, hash;
    blob_zero(&pw);
    db_ephemeral_blob(&q, 0, &pw);
    szPw = blob_size(&pw);
    blob_zero(&combined);
    blob_copy(&combined, pNonce);
    blob_append(&combined, blob_buffer(&pw), szPw);
    sha1sum_blob(&combined, &hash);
    assert( blob_size(&hash)==40 );
    rc = blob_constant_time_cmp(&hash, pSig);
    blob_reset(&hash);
    blob_reset(&combined);
    if( rc!=0 && szPw!=40 ){
      /* If this server stores cleartext passwords and the password did not
      ** match, then perhaps the client is sending SHA1 passwords.  Try
      ** again with the SHA1 password.
      */
      const char *zPw = db_column_text(&q, 0);
      char *zSecret = sha1_shared_secret(zPw, blob_str(pLogin), 0);
      blob_zero(&combined);
      blob_copy(&combined, pNonce);
      blob_append(&combined, zSecret, -1);
      free(zSecret);
      sha1sum_blob(&combined, &hash);
      rc = blob_constant_time_cmp(&hash, pSig);
      blob_reset(&hash);
      blob_reset(&combined);
    }
    if( rc==0 ){
      const char *zCap;
      zCap = db_column_text(&q, 1);
      login_set_capabilities(zCap, 0);
      g.userUid = db_column_int(&q, 2);
      g.zLogin = mprintf("%b", pLogin);
      g.zNonce = mprintf("%b", pNonce);
    }
  }
  db_finalize(&q);
  return rc;
}

/*
** Send the content of all files in the unsent table.
**
** This is really just an optimization.  If you clear the
** unsent table, all the right files will still get transferred.
** It just might require an extra round trip or two.
*/
static void send_unsent(Xfer *pXfer){
  Stmt q;
  db_prepare(&q, "SELECT rid FROM unsent EXCEPT SELECT rid FROM private");
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    send_file(pXfer, rid, 0, 0);
  }
  db_finalize(&q);
  db_multi_exec("DELETE FROM unsent");
}

/*
** Check to see if the number of unclustered entries is greater than
** 100 and if it is, form a new cluster.  Unclustered phantoms do not
** count toward the 100 total.  And phantoms are never added to a new
** cluster.
*/
void create_cluster(void){
  Blob cluster, cksum;
  Blob deleteWhere;
  Stmt q;
  int nUncl;
  int nRow = 0;
  int rid;

#if 0
  /* We should not ever get any private artifacts in the unclustered table.
  ** But if we do (because of a bug) now is a good time to delete them. */
  db_multi_exec(
    "DELETE FROM unclustered WHERE rid IN (SELECT rid FROM private)"
  );
#endif

  nUncl = db_int(0, "SELECT count(*) FROM unclustered /*scan*/"
                    " WHERE NOT EXISTS(SELECT 1 FROM phantom"
                                      " WHERE rid=unclustered.rid)");
  if( nUncl>=100 ){
    blob_zero(&cluster);
    blob_zero(&deleteWhere);
    db_prepare(&q, "SELECT uuid FROM unclustered, blob"
                   " WHERE NOT EXISTS(SELECT 1 FROM phantom"
                   "                   WHERE rid=unclustered.rid)"
                   "   AND unclustered.rid=blob.rid"
                   "   AND NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
                   " ORDER BY 1");
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&cluster, "M %s\n", db_column_text(&q, 0));
      nRow++;
      if( nRow>=800 && nUncl>nRow+100 ){
        md5sum_blob(&cluster, &cksum);
        blob_appendf(&cluster, "Z %b\n", &cksum);
        blob_reset(&cksum);
        rid = content_put(&cluster);
        manifest_crosslink(rid, &cluster, MC_NONE);
        blob_reset(&cluster);
        nUncl -= nRow;
        nRow = 0;
        blob_append_sql(&deleteWhere, ",%d", rid);
      }
    }
    db_finalize(&q);
    db_multi_exec(
      "DELETE FROM unclustered WHERE rid NOT IN (0 %s)"
      "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=unclustered.rid)",
      blob_sql_text(&deleteWhere)
    );
    blob_reset(&deleteWhere);
    if( nRow>0 ){
      md5sum_blob(&cluster, &cksum);
      blob_appendf(&cluster, "Z %b\n", &cksum);
      blob_reset(&cksum);
      rid = content_put(&cluster);
      manifest_crosslink(rid, &cluster, MC_NONE);
      blob_reset(&cluster);
    }
  }
}

/*
** Send igot messages for every private artifact
*/
static int send_private(Xfer *pXfer){
  int cnt = 0;
  Stmt q;
  if( pXfer->syncPrivate ){
    db_prepare(&q, "SELECT uuid FROM private JOIN blob USING(rid)");
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(pXfer->pOut, "igot %s 1\n", db_column_text(&q,0));
      cnt++;
    }
    db_finalize(&q);
  }
  return cnt;
}

/*
** Send an igot message for every entry in unclustered table.
** Return the number of cards sent.
*/
static int send_unclustered(Xfer *pXfer){
  Stmt q;
  int cnt = 0;
  if( pXfer->resync ){
    db_prepare(&q,
      "SELECT uuid, rid FROM blob"
      " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
      "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=blob.rid)"
      "   AND NOT EXISTS(SELECT 1 FROM private WHERE rid=blob.rid)"
      "   AND blob.rid<=%d"
      " ORDER BY blob.rid DESC",
      pXfer->resync
    );
  }else{
    db_prepare(&q,
      "SELECT uuid FROM unclustered JOIN blob USING(rid)"
      " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
      "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=blob.rid)"
      "   AND NOT EXISTS(SELECT 1 FROM private WHERE rid=blob.rid)"
    );
  }
  while( db_step(&q)==SQLITE_ROW ){
    blob_appendf(pXfer->pOut, "igot %s\n", db_column_text(&q, 0));
    cnt++;
    if( pXfer->resync && pXfer->mxSend<blob_size(pXfer->pOut) ){
      pXfer->resync = db_column_int(&q, 1)-1;
    }
  }
  db_finalize(&q);
  if( cnt==0 ) pXfer->resync = 0;
  return cnt;
}

/*
** Send an igot message for every artifact.
*/
static void send_all(Xfer *pXfer){
  Stmt q;
  db_prepare(&q,
    "SELECT uuid FROM blob "
    " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
    "   AND NOT EXISTS(SELECT 1 FROM private WHERE rid=blob.rid)"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=blob.rid)"
  );
  while( db_step(&q)==SQLITE_ROW ){
    blob_appendf(pXfer->pOut, "igot %s\n", db_column_text(&q, 0));
  }
  db_finalize(&q);
}

/*
** pXfer is a "pragma uv-hash HASH" card.
**
** If HASH is different from the unversioned content hash on this server,
** then send a bunch of uvigot cards, one for each entry unversioned file
** on this server.
*/
static void send_unversioned_catalog(Xfer *pXfer){
  unversioned_schema();
  if( !blob_eq(&pXfer->aToken[2], unversioned_content_hash(0)) ){
    int nUvIgot = 0;
    Stmt uvq;
    db_prepare(&uvq,
       "SELECT name, mtime, hash, sz FROM unversioned"
    );
    while( db_step(&uvq)==SQLITE_ROW ){
      const char *zName = db_column_text(&uvq,0);
      sqlite3_int64 mtime = db_column_int64(&uvq,1);
      const char *zHash = db_column_text(&uvq,2);
      int sz = db_column_int(&uvq,3);
      nUvIgot++;
      if( zHash==0 ){ sz = 0; zHash = "-"; }
      blob_appendf(pXfer->pOut, "uvigot %s %lld %s %d\n",
                   zName, mtime, zHash, sz);
    }
    db_finalize(&uvq);
  }
}

/*
** Called when there is an attempt to transfer private content to and
** from a server without authorization.
*/
static void server_private_xfer_not_authorized(void){
  @ error not\sauthorized\sto\ssync\sprivate\scontent
}

/*
** Return the common TH1 code to evaluate prior to evaluating any other
** TH1 transfer notification scripts.
*/
const char *xfer_common_code(void){
  return db_get("xfer-common-script", 0);
}

/*
** Return the TH1 code to evaluate when a push is processed.
*/
const char *xfer_push_code(void){
  return db_get("xfer-push-script", 0);
}

/*
** Return the TH1 code to evaluate when a commit is processed.
*/
const char *xfer_commit_code(void){
  return db_get("xfer-commit-script", 0);
}

/*
** Return the TH1 code to evaluate when a ticket change is processed.
*/
const char *xfer_ticket_code(void){
  return db_get("xfer-ticket-script", 0);
}

/*
** Run the specified TH1 script, if any, and returns 1 on error.
*/
int xfer_run_script(
  const char *zScript,
  const char *zUuidOrList,
  int bIsList
){
  int rc = TH_OK;
  if( !zScript ) return rc;
  Th_FossilInit(TH_INIT_DEFAULT);
  Th_Store(bIsList ? "uuids" : "uuid", zUuidOrList ? zUuidOrList : "");
  rc = Th_Eval(g.interp, 0, zScript, -1);
  if( rc!=TH_OK ){
    fossil_error(1, "%s", Th_GetResult(g.interp, 0));
  }
  return rc;
}

/*
** Runs the pre-transfer TH1 script, if any, and returns its return code.
** This script may be run multiple times.  If the script performs actions
** that cannot be redone, it should use an internal [if] guard similar to
** the following:
**
** if {![info exists common_done]} {
**   # ... code here
**   set common_done 1
** }
*/
int xfer_run_common_script(void){
  return xfer_run_script(xfer_common_code(), 0, 0);
}

/*
** If this variable is set, disable login checks.  Used for debugging
** only.
*/
static int disableLogin = 0;

/*
** The CGI/HTTP preprocessor always redirects requests with a content-type
** of application/x-fossil or application/x-fossil-debug to this page,
** regardless of what path was specified in the HTTP header.  This allows
** clone clients to specify a URL that omits default pathnames, such
** as "http://fossil-scm.org/" instead of "http://fossil-scm.org/index.cgi".
**
** WEBPAGE: xfer
**
** This is the transfer handler on the server side.  The transfer
** message has been uncompressed and placed in the g.cgiIn blob.
** Process this message and form an appropriate reply.
*/
void page_xfer(void){
  int isPull = 0;
  int isPush = 0;
  int nErr = 0;
  Xfer xfer;
  int deltaFlag = 0;
  int isClone = 0;
  int nGimme = 0;
  int size;
  char *zNow;
  int rc;
  const char *zScript = 0;
  char *zUuidList = 0;
  int nUuidList = 0;
  char **pzUuidList = 0;
  int *pnUuidList = 0;
  int uvCatalogSent = 0;

  if( fossil_strcmp(PD("REQUEST_METHOD","POST"),"POST") ){
     fossil_redirect_home();
  }
  g.zLogin = "anonymous";
  login_set_anon_nobody_capabilities();
  login_check_credentials();
  memset(&xfer, 0, sizeof(xfer));
  blobarray_zero(xfer.aToken, count(xfer.aToken));
  cgi_set_content_type(g.zContentType);
  cgi_reset_content();
  if( db_schema_is_outofdate() ){
    @ error database\sschema\sis\sout-of-date\son\sthe\sserver.
    return;
  }
  blob_zero(&xfer.err);
  xfer.pIn = &g.cgiIn;
  xfer.pOut = cgi_output_blob();
  xfer.mxSend = db_get_int("max-download", 5000000);
  xfer.maxTime = db_get_int("max-download-time", 30);
  if( xfer.maxTime<1 ) xfer.maxTime = 1;
  xfer.maxTime += time(NULL);
  g.xferPanic = 1;

  db_begin_transaction();
  db_multi_exec(
     "CREATE TEMP TABLE onremote(rid INTEGER PRIMARY KEY);"
  );
  manifest_crosslink_begin();
  rc = xfer_run_common_script();
  if( rc==TH_ERROR ){
    cgi_reset_content();
    @ error common\sscript\sfailed:\s%F(g.zErrMsg)
    nErr++;
  }
  zScript = xfer_push_code();
  if( zScript ){ /* NOTE: Are TH1 transfer hooks enabled? */
    pzUuidList = &zUuidList;
    pnUuidList = &nUuidList;
  }
  while( blob_line(xfer.pIn, &xfer.line) ){
    if( blob_buffer(&xfer.line)[0]=='#' ) continue;
    if( blob_size(&xfer.line)==0 ) continue;
    xfer.nToken = blob_tokenize(&xfer.line, xfer.aToken, count(xfer.aToken));

    /*   file HASH SIZE \n CONTENT
    **   file HASH DELTASRC SIZE \n CONTENT
    **
    ** Accept a file from the client.
    */
    if( blob_eq(&xfer.aToken[0], "file") ){
      if( !isPush ){
        cgi_reset_content();
        @ error not\sauthorized\sto\swrite
        nErr++;
        break;
      }
      xfer_accept_file(&xfer, 0, pzUuidList, pnUuidList);
      if( blob_size(&xfer.err) ){
        cgi_reset_content();
        @ error %T(blob_str(&xfer.err))
        nErr++;
        break;
      }
    }else

    /*   cfile HASH USIZE CSIZE \n CONTENT
    **   cfile HASH DELTASRC USIZE CSIZE \n CONTENT
    **
    ** Accept a file from the client.
    */
    if( blob_eq(&xfer.aToken[0], "cfile") ){
      if( !isPush ){
        cgi_reset_content();
        @ error not\sauthorized\sto\swrite
        nErr++;
        break;
      }
      xfer_accept_compressed_file(&xfer, pzUuidList, pnUuidList);
      if( blob_size(&xfer.err) ){
        cgi_reset_content();
        @ error %T(blob_str(&xfer.err))
        nErr++;
        break;
      }
    }else

    /*   uvfile NAME MTIME HASH SIZE FLAGS \n CONTENT
    **
    ** Accept an unversioned file from the client.
    */
    if( blob_eq(&xfer.aToken[0], "uvfile") ){
      xfer_accept_unversioned_file(&xfer, g.perm.WrUnver);
      if( blob_size(&xfer.err) ){
        cgi_reset_content();
        @ error %T(blob_str(&xfer.err))
        nErr++;
        break;
      }
    }else

    /*   gimme HASH
    **
    ** Client is requesting a file.  Send it.
    */
    if( blob_eq(&xfer.aToken[0], "gimme")
     && xfer.nToken==2
     && blob_is_hname(&xfer.aToken[1])
    ){
      nGimme++;
      if( isPull ){
        int rid = rid_from_uuid(&xfer.aToken[1], 0, 0);
        if( rid ){
          send_file(&xfer, rid, &xfer.aToken[1], deltaFlag);
        }
      }
    }else

    /*   uvgimme NAME
    **
    ** Client is requesting an unversioned file.  Send it.
    */
    if( blob_eq(&xfer.aToken[0], "uvgimme")
     && xfer.nToken==2
     && blob_is_filename(&xfer.aToken[1])
    ){
      send_unversioned_file(&xfer, blob_str(&xfer.aToken[1]), 0);
    }else

    /*   igot HASH ?ISPRIVATE?
    **
    ** Client announces that it has a particular file.  If the ISPRIVATE
    ** argument exists and is non-zero, then the file is a private file.
    */
    if( xfer.nToken>=2
     && blob_eq(&xfer.aToken[0], "igot")
     && blob_is_hname(&xfer.aToken[1])
    ){
      if( isPush ){
        if( xfer.nToken==2 || blob_eq(&xfer.aToken[2],"1")==0 ){
          rid_from_uuid(&xfer.aToken[1], 1, 0);
        }else if( g.perm.Private ){
          rid_from_uuid(&xfer.aToken[1], 1, 1);
        }else{
          server_private_xfer_not_authorized();
        }
      }
    }else


    /*    pull  SERVERCODE  PROJECTCODE
    **    push  SERVERCODE  PROJECTCODE
    **
    ** The client wants either send or receive.  The server should
    ** verify that the project code matches.  The server code is ignored.
    */
    if( xfer.nToken==3
     && (blob_eq(&xfer.aToken[0], "pull") || blob_eq(&xfer.aToken[0], "push"))
     && blob_is_hname(&xfer.aToken[2])
    ){
      const char *zPCode;
      zPCode = db_get("project-code", 0);
      if( zPCode==0 ){
        fossil_panic("missing project code");
      }
      if( !blob_eq_str(&xfer.aToken[2], zPCode, -1) ){
        cgi_reset_content();
        @ error wrong\sproject
        nErr++;
        break;
      }
      login_check_credentials();
      if( blob_eq(&xfer.aToken[0], "pull") ){
        if( !g.perm.Read ){
          cgi_reset_content();
          @ error not\sauthorized\sto\sread
          nErr++;
          break;
        }
        isPull = 1;
      }else{
        if( !g.perm.Write ){
          if( !isPull ){
            cgi_reset_content();
            @ error not\sauthorized\sto\swrite
            nErr++;
          }else{
            @ message pull\sonly\s-\snot\sauthorized\sto\spush
          }
        }else{
          isPush = 1;
        }
      }
    }else

    /*    clone   ?PROTOCOL-VERSION?  ?SEQUENCE-NUMBER?
    **
    ** The client knows nothing.  Tell all.
    */
    if( blob_eq(&xfer.aToken[0], "clone") ){
      int iVers;
      login_check_credentials();
      if( !g.perm.Clone ){
        cgi_reset_content();
        @ push %s(db_get("server-code", "x")) %s(db_get("project-code", "x"))
        @ error not\sauthorized\sto\sclone
        nErr++;
        break;
      }
      if( db_get_boolean("uv-sync",0) && !uvCatalogSent ){
        @ pragma uv-pull-only
        send_unversioned_catalog(&xfer);
        uvCatalogSent = 1;
      }
      if( xfer.nToken==3
       && blob_is_int(&xfer.aToken[1], &iVers)
       && iVers>=2
      ){
        int seqno, max;
        if( iVers>=3 ){
          cgi_set_content_type("application/x-fossil-uncompressed");
        }
        blob_is_int(&xfer.aToken[2], &seqno);
        max = db_int(0, "SELECT max(rid) FROM blob");
        while( xfer.mxSend>blob_size(xfer.pOut) && seqno<=max){
          if( time(NULL) >= xfer.maxTime ) break;
          if( iVers>=3 ){
            send_compressed_file(&xfer, seqno);
          }else{
            send_file(&xfer, seqno, 0, 1);
          }
          seqno++;
        }
        if( seqno>max ) seqno = 0;
        @ clone_seqno %d(seqno)
      }else{
        isClone = 1;
        isPull = 1;
        deltaFlag = 1;
      }
      @ push %s(db_get("server-code", "x")) %s(db_get("project-code", "x"))
    }else

    /*    login  USER  NONCE  SIGNATURE
    **
    ** Check for a valid login.  This has to happen before anything else.
    ** The client can send multiple logins.  Permissions are cumulative.
    */
    if( blob_eq(&xfer.aToken[0], "login")
     && xfer.nToken==4
    ){
      if( disableLogin ){
        g.perm.Read = g.perm.Write = g.perm.Private = g.perm.Admin = 1;
      }else{
        if( check_tail_hash(&xfer.aToken[2], xfer.pIn)
         || check_login(&xfer.aToken[1], &xfer.aToken[2], &xfer.aToken[3])
        ){
          cgi_reset_content();
          @ error login\sfailed
          nErr++;
          break;
        }
      }
    }else

    /*    reqconfig  NAME
    **
    ** Request a configuration value
    */
    if( blob_eq(&xfer.aToken[0], "reqconfig")
     && xfer.nToken==2
    ){
      if( g.perm.Read ){
        char *zName = blob_str(&xfer.aToken[1]);
        if( zName[0]=='/' ){
          /* New style configuration transfer */
          int groupMask = configure_name_to_mask(&zName[1], 0);
          if( !g.perm.Admin ) groupMask &= ~CONFIGSET_USER;
          if( !g.perm.RdAddr ) groupMask &= ~CONFIGSET_ADDR;
          configure_send_group(xfer.pOut, groupMask, 0);
        }
      }
    }else

    /*   config NAME SIZE \n CONTENT
    **
    ** Receive a configuration value from the client.  This is only
    ** permitted for high-privilege users.
    */
    if( blob_eq(&xfer.aToken[0],"config") && xfer.nToken==3
        && blob_is_int(&xfer.aToken[2], &size) ){
      const char *zName = blob_str(&xfer.aToken[1]);
      Blob content;
      blob_zero(&content);
      blob_extract(xfer.pIn, size, &content);
      if( !g.perm.Admin ){
        cgi_reset_content();
        @ error not\sauthorized\sto\spush\sconfiguration
        nErr++;
        break;
      }
      configure_receive(zName, &content, CONFIGSET_ALL);
      blob_reset(&content);
      blob_seek(xfer.pIn, 1, BLOB_SEEK_CUR);
    }else


    /*    cookie TEXT
    **
    ** A cookie contains a arbitrary-length argument that is server-defined.
    ** The argument must be encoded so as not to contain any whitespace.
    ** The server can optionally send a cookie to the client.  The client
    ** might then return the same cookie back to the server on its next
    ** communication.  The cookie might record information that helps
    ** the server optimize a push or pull.
    **
    ** The client is not required to return a cookie.  So the server
    ** must not depend on the cookie.  The cookie should be an optimization
    ** only.  The client might also send a cookie that came from a different
    ** server.  So the server must be prepared to distinguish its own cookie
    ** from cookies originating from other servers.  The client might send
    ** back several different cookies to the server.  The server should be
    ** prepared to sift through the cookies and pick the one that it wants.
    */
    if( blob_eq(&xfer.aToken[0], "cookie") && xfer.nToken==2 ){
      /* Process the cookie */
    }else


    /*    private
    **
    ** This card indicates that the next "file" or "cfile" will contain
    ** private content.
    */
    if( blob_eq(&xfer.aToken[0], "private") ){
      if( !g.perm.Private ){
        server_private_xfer_not_authorized();
      }else{
        xfer.nextIsPrivate = 1;
      }
    }else


    /*    pragma NAME VALUE...
    **
    ** The client issue pragmas to try to influence the behavior of the
    ** server.  These are requests only.  Unknown pragmas are silently
    ** ignored.
    */
    if( blob_eq(&xfer.aToken[0], "pragma") && xfer.nToken>=2 ){

      /*   pragma send-private
      **
      ** If the user has the "x" privilege (which must be set explicitly -
      ** it is not automatic with "a" or "s") then this pragma causes
      ** private information to be pulled in addition to public records.
      */
      if( blob_eq(&xfer.aToken[1], "send-private") ){
        login_check_credentials();
        if( !g.perm.Private ){
          server_private_xfer_not_authorized();
        }else{
          xfer.syncPrivate = 1;
        }
      }

      /*   pragma send-catalog
      **
      ** Send igot cards for all known artifacts.
      */
      if( blob_eq(&xfer.aToken[1], "send-catalog") ){
        xfer.resync = 0x7fffffff;
      }

      /*   pragma client-version VERSION
      **
      ** Let the server know what version of Fossil is running on the client.
      */
      if( xfer.nToken>=3 && blob_eq(&xfer.aToken[1], "client-version") ){
        xfer.clientVersion = atoi(blob_str(&xfer.aToken[2]));
      }

      /*   pragma uv-hash HASH
      **
      ** The client wants to make sure that unversioned files are all synced.
      ** If the HASH does not match, send a complete catalog of
      ** "uvigot" cards.
      */
      if( blob_eq(&xfer.aToken[1], "uv-hash")
       && blob_is_hname(&xfer.aToken[2])
      ){
        if( !uvCatalogSent ){
          if( g.perm.Read && g.perm.WrUnver ){
            @ pragma uv-push-ok
            send_unversioned_catalog(&xfer);
          }else if( g.perm.Read ){
            @ pragma uv-pull-only
            send_unversioned_catalog(&xfer);
          }
        }
        uvCatalogSent = 1;
      }
    }else

    /* Unknown message
    */
    {
      cgi_reset_content();
      @ error bad\scommand:\s%F(blob_str(&xfer.line))
    }
    blobarray_reset(xfer.aToken, xfer.nToken);
    blob_reset(&xfer.line);
  }
  if( isPush ){
    if( rc==TH_OK ){
      rc = xfer_run_script(zScript, zUuidList, 1);
      if( rc==TH_ERROR ){
        cgi_reset_content();
        @ error push\sscript\sfailed:\s%F(g.zErrMsg)
        nErr++;
      }
    }
    request_phantoms(&xfer, 500);
  }
  if( zUuidList ){
    Th_Free(g.interp, zUuidList);
  }
  if( isClone && nGimme==0 ){
    /* The initial "clone" message from client to server contains no
    ** "gimme" cards. On that initial message, send the client an "igot"
    ** card for every artifact currently in the repository.  This will
    ** cause the client to create phantoms for all artifacts, which will
    ** in turn make sure that the entire repository is sent efficiently
    ** and expeditiously.
    */
    send_all(&xfer);
    if( xfer.syncPrivate ) send_private(&xfer);
  }else if( isPull ){
    create_cluster();
    send_unclustered(&xfer);
    if( xfer.syncPrivate ) send_private(&xfer);
  }
  db_multi_exec("DROP TABLE onremote");
  manifest_crosslink_end(MC_PERMIT_HOOKS);

  /* Send the server timestamp last, in case prior processing happened
  ** to use up a significant fraction of our time window.
  */
  zNow = db_text(0, "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%S', 'now')");
  @ # timestamp %s(zNow)
  free(zNow);

  db_end_transaction(0);
  configure_rebuild();
}

/*
** COMMAND: test-xfer
**
** This command is used for debugging the server.  There is a single
** argument which is the uncompressed content of an "xfer" message
** from client to server.  This command interprets that message as
** if had been received by the server.
**
** On the client side, run:
**
**      fossil push http://bogus/ --httptrace
**
** Or a similar command to provide the output.  The content of the
** message will appear on standard output.  Capture this message
** into a file named (for example) out.txt.  Then run the
** server in gdb:
**
**     gdb fossil
**     r test-xfer out.txt
*/
void cmd_test_xfer(void){
  db_find_and_open_repository(0,0);
  if( g.argc!=2 && g.argc!=3 ){
    usage("?MESSAGEFILE?");
  }
  blob_zero(&g.cgiIn);
  blob_read_from_file(&g.cgiIn, g.argc==2 ? "-" : g.argv[2]);
  disableLogin = 1;
  page_xfer();
  fossil_print("%s\n", cgi_extract_content());
}

/*
** Format strings for progress reporting.
*/
static const char zLabelFormat[] = "%-10s %10s %10s %10s %10s\n";
static const char zValueFormat[] = "\r%-10s %10d %10d %10d %10d\n";
static const char zBriefFormat[] =
   "Round-trips: %d   Artifacts sent: %d  received: %d\r";

#if INTERFACE
/*
** Flag options for controlling client_sync()
*/
#define SYNC_PUSH           0x0001    /* push content client to server */
#define SYNC_PULL           0x0002    /* pull content server to client */
#define SYNC_CLONE          0x0004    /* clone the repository */
#define SYNC_PRIVATE        0x0008    /* Also transfer private content */
#define SYNC_VERBOSE        0x0010    /* Extra diagnostics */
#define SYNC_RESYNC         0x0020    /* --verily */
#define SYNC_UNVERSIONED    0x0040    /* Sync unversioned content */
#define SYNC_UV_REVERT      0x0080    /* Copy server unversioned to client */
#define SYNC_FROMPARENT     0x0100    /* Pull from the parent project */
#define SYNC_UV_TRACE       0x0200    /* Describe UV activities */
#define SYNC_UV_DRYRUN      0x0400    /* Do not actually exchange files */
#endif

/*
** Floating-point absolute value
*/
static double fossil_fabs(double x){
  return x>0.0 ? x : -x;
}

/*
** Sync to the host identified in g.url.name and g.url.path.  This
** routine is called by the client.
**
** Records are pushed to the server if pushFlag is true.  Records
** are pulled if pullFlag is true.  A full sync occurs if both are
** true.
*/
int client_sync(
  unsigned syncFlags,     /* Mask of SYNC_* flags */
  unsigned configRcvMask, /* Receive these configuration items */
  unsigned configSendMask /* Send these configuration items */
){
  int go = 1;             /* Loop until zero */
  int nCardSent = 0;      /* Number of cards sent */
  int nCardRcvd = 0;      /* Number of cards received */
  int nCycle = 0;         /* Number of round trips to the server */
  int size;               /* Size of a config value or uvfile */
  int origConfigRcvMask;  /* Original value of configRcvMask */
  int nFileRecv;          /* Number of files received */
  int mxPhantomReq = 200; /* Max number of phantoms to request per comm */
  const char *zCookie;    /* Server cookie */
  i64 nSent, nRcvd;       /* Bytes sent and received (after compression) */
  int cloneSeqno = 1;     /* Sequence number for clones */
  Blob send;              /* Text we are sending to the server */
  Blob recv;              /* Reply we got back from the server */
  Xfer xfer;              /* Transfer data */
  int pctDone;            /* Percentage done with a message */
  int lastPctDone = -1;   /* Last displayed pctDone */
  double rArrivalTime;    /* Time at which a message arrived */
  const char *zSCode = db_get("server-code", "x");
  const char *zPCode = db_get("project-code", 0);
  int nErr = 0;           /* Number of errors */
  int nRoundtrip= 0;      /* Number of HTTP requests */
  int nArtifactSent = 0;  /* Total artifacts sent */
  int nArtifactRcvd = 0;  /* Total artifacts received */
  const char *zOpType = 0;/* Push, Pull, Sync, Clone */
  double rSkew = 0.0;     /* Maximum time skew */
  int uvHashSent = 0;     /* The "pragma uv-hash" message has been sent */
  int uvDoPush = 0;       /* Generate uvfile messages to send to server */
  int nUvGimmeSent = 0;   /* Number of uvgimme cards sent on this cycle */
  int nUvFileRcvd = 0;    /* Number of uvfile cards received on this cycle */
  sqlite3_int64 mtime;    /* Modification time on a UV file */

  if( db_get_boolean("dont-push", 0) ) syncFlags &= ~SYNC_PUSH;
  if( (syncFlags & (SYNC_PUSH|SYNC_PULL|SYNC_CLONE|SYNC_UNVERSIONED))==0
     && configRcvMask==0 && configSendMask==0 ) return 0;
  if( syncFlags & SYNC_FROMPARENT ){
    configRcvMask = 0;
    configSendMask = 0;
    syncFlags &= ~(SYNC_PUSH);
    zPCode = db_get("parent-project-code", 0);
    if( zPCode==0 || db_get("parent-project-name",0)==0 ){
      fossil_fatal("there is no parent project: set the 'parent-project-code'"
                   " and 'parent-project-name' config parameters set in order"
                   " to pull from a parent project");
    }
  }

  transport_stats(0, 0, 1);
  socket_global_init();
  memset(&xfer, 0, sizeof(xfer));
  xfer.pIn = &recv;
  xfer.pOut = &send;
  xfer.mxSend = db_get_int("max-upload", 250000);
  xfer.maxTime = -1;
  xfer.clientVersion = RELEASE_VERSION_NUMBER;
  if( syncFlags & SYNC_PRIVATE ){
    g.perm.Private = 1;
    xfer.syncPrivate = 1;
  }

  blobarray_zero(xfer.aToken, count(xfer.aToken));
  blob_zero(&send);
  blob_zero(&recv);
  blob_zero(&xfer.err);
  blob_zero(&xfer.line);
  origConfigRcvMask = 0;


  /* Send the send-private pragma if we are trying to sync private data */
  if( syncFlags & SYNC_PRIVATE ){
    blob_append(&send, "pragma send-private\n", -1);
  }

  /* When syncing unversioned files, create a TEMP table in which to store
  ** the names of files that need to be sent from client to server.
  **
  ** The initial assumption is that all unversioned files need to be sent
  ** to the other side.  But "uvigot" cards received back from the remote
  ** side will normally cause many of these entries to be removed since they
  ** do not really need to be sent.
  */
  if( (syncFlags & (SYNC_UNVERSIONED|SYNC_CLONE))!=0 ){
    unversioned_schema();
    db_multi_exec(
       "CREATE TEMP TABLE uv_tosend("
       "  name TEXT PRIMARY KEY,"  /* Name of file to send client->server */
       "  mtimeOnly BOOLEAN"       /* True to only send mtime, not content */
       ") WITHOUT ROWID;"
       "INSERT INTO uv_toSend(name,mtimeOnly)"
       "  SELECT name, 0 FROM unversioned WHERE hash IS NOT NULL;"
    );
  }

  /*
  ** Always begin with a clone, pull, or push message
  */
  blob_appendf(&send, "pragma client-version %d\n", RELEASE_VERSION_NUMBER);
  if( syncFlags & SYNC_CLONE ){
    blob_appendf(&send, "clone 3 %d\n", cloneSeqno);
    syncFlags &= ~(SYNC_PUSH|SYNC_PULL);
    nCardSent++;
    /* TBD: Request all transferable configuration values */
    content_enable_dephantomize(0);
    zOpType = "Clone";
  }else if( syncFlags & SYNC_PULL ){
    blob_appendf(&send, "pull %s %s\n", zSCode, zPCode);
    nCardSent++;
    zOpType = (syncFlags & SYNC_PUSH)?"Sync":"Pull";
    if( (syncFlags & SYNC_RESYNC)!=0 && nCycle<2 ){
      blob_appendf(&send, "pragma send-catalog\n");
      nCardSent++;
    }
  }
  if( syncFlags & SYNC_PUSH ){
    blob_appendf(&send, "push %s %s\n", zSCode, zPCode);
    nCardSent++;
    if( (syncFlags & SYNC_PULL)==0 ) zOpType = "Push";
    if( (syncFlags & SYNC_RESYNC)!=0 ) xfer.resync = 0x7fffffff;
  }
  if( syncFlags & SYNC_VERBOSE ){
    fossil_print(zLabelFormat /*works-like:"%s%s%s%s%d"*/,
                 "", "Bytes", "Cards", "Artifacts", "Deltas");
  }

  while( go ){
    int newPhantom = 0;
    char *zRandomness;
    db_begin_transaction();
    db_record_repository_filename(0);
    db_multi_exec(
      "CREATE TEMP TABLE onremote(rid INTEGER PRIMARY KEY);"
    );
    manifest_crosslink_begin();


    /* Send back the most recently received cookie.  Let the server
    ** figure out if this is a cookie that it cares about.
    */
    zCookie = db_get("cookie", 0);
    if( zCookie ){
      blob_appendf(&send, "cookie %s\n", zCookie);
    }

    /* Generate gimme cards for phantoms and leaf cards
    ** for all leaves.
    */
    if( (syncFlags & SYNC_PULL)!=0
     || ((syncFlags & SYNC_CLONE)!=0 && cloneSeqno==1)
    ){
      request_phantoms(&xfer, mxPhantomReq);
    }
    if( syncFlags & SYNC_PUSH ){
      send_unsent(&xfer);
      nCardSent += send_unclustered(&xfer);
      if( syncFlags & SYNC_PRIVATE ) send_private(&xfer);
    }

    /* Send configuration parameter requests.  On a clone, delay sending
    ** this until the second cycle since the login card might fail on
    ** the first cycle.
    */
    if( configRcvMask && ((syncFlags & SYNC_CLONE)==0 || nCycle>0) ){
      const char *zName;
      if( zOpType==0 ) zOpType = "Pull";
      zName = configure_first_name(configRcvMask);
      while( zName ){
        blob_appendf(&send, "reqconfig %s\n", zName);
        zName = configure_next_name(configRcvMask);
        nCardSent++;
      }
      origConfigRcvMask = configRcvMask;
      configRcvMask = 0;
    }

    /* Send a request to sync unversioned files.  On a clone, delay sending
    ** this until the second cycle since the login card might fail on
    ** the first cycle.
    */
    if( (syncFlags & SYNC_UNVERSIONED)!=0
     && ((syncFlags & SYNC_CLONE)==0 || nCycle>0)
     && !uvHashSent
    ){
      blob_appendf(&send, "pragma uv-hash %s\n", unversioned_content_hash(0));
      nCardSent++;
      uvHashSent = 1;
    }

    /* Send configuration parameters being pushed */
    if( configSendMask ){
      if( zOpType==0 ) zOpType = "Push";
      nCardSent += configure_send_group(xfer.pOut, configSendMask, 0);
      configSendMask = 0;
    }

    /* Send unversioned files present here on the client but missing or
    ** obsolete on the server.
    **
    ** Or, if the SYNC_UV_REVERT flag is set, delete the local unversioned
    ** files that do not exist on the server.
    **
    ** This happens on the second exchange, since we do not know what files
    ** need to be sent until after the uvigot cards from the first exchange
    ** have been processed.
    */
    if( uvDoPush ){
      assert( (syncFlags & SYNC_UNVERSIONED)!=0 );
      if( syncFlags & SYNC_UV_DRYRUN ){
        uvDoPush = 0;
      }else if( syncFlags & SYNC_UV_REVERT ){
        db_multi_exec(
          "DELETE FROM unversioned"
          " WHERE name IN (SELECT name FROM uv_tosend);"
          "DELETE FROM uv_tosend;"
        );
        uvDoPush = 0;
      }else{
        Stmt uvq;
        int rc = SQLITE_OK;
        db_prepare(&uvq, "SELECT name, mtimeOnly FROM uv_tosend");
        while( (rc = db_step(&uvq))==SQLITE_ROW ){
          const char *zName = db_column_text(&uvq, 0);
          send_unversioned_file(&xfer, zName, db_column_int(&uvq,1));
          nCardSent++;
          nArtifactSent++;
          db_multi_exec("DELETE FROM uv_tosend WHERE name=%Q", zName);
          if( syncFlags & SYNC_VERBOSE ){
            fossil_print("\rUnversioned-file sent: %s\n", zName);
          }
          if( blob_size(xfer.pOut)>xfer.mxSend ) break;
        }
        db_finalize(&uvq);
        if( rc==SQLITE_DONE ) uvDoPush = 0;
      }
    }

    /* Append randomness to the end of the message.  This makes all
    ** messages unique so that that the login-card nonce will always
    ** be unique.
    */
    zRandomness = db_text(0, "SELECT hex(randomblob(20))");
    blob_appendf(&send, "# %s\n", zRandomness);
    free(zRandomness);

    if( syncFlags & SYNC_VERBOSE ){
      fossil_print("waiting for server...");
    }
    fflush(stdout);
    /* Exchange messages with the server */
    if( http_exchange(&send, &recv, (syncFlags & SYNC_CLONE)==0 || nCycle>0,
        MAX_REDIRECTS) ){
      nErr++;
      go = 2;
      break;
    }

    /* Output current stats */
    if( syncFlags & SYNC_VERBOSE ){
      fossil_print(zValueFormat /*works-like:"%s%d%d%d%d"*/, "Sent:",
                   blob_size(&send), nCardSent+xfer.nGimmeSent+xfer.nIGotSent,
                   xfer.nFileSent, xfer.nDeltaSent);
    }else{
      nRoundtrip++;
      nArtifactSent += xfer.nFileSent + xfer.nDeltaSent;
      fossil_print(zBriefFormat /*works-like:"%d%d%d"*/,
                   nRoundtrip, nArtifactSent, nArtifactRcvd);
    }
    nCardSent = 0;
    nCardRcvd = 0;
    xfer.nFileSent = 0;
    xfer.nDeltaSent = 0;
    xfer.nGimmeSent = 0;
    xfer.nIGotSent = 0;

    lastPctDone = -1;
    blob_reset(&send);
    blob_appendf(&send, "pragma client-version %d\n", RELEASE_VERSION_NUMBER);
    rArrivalTime = db_double(0.0, "SELECT julianday('now')");

    /* Send the send-private pragma if we are trying to sync private data */
    if( syncFlags & SYNC_PRIVATE ){
      blob_append(&send, "pragma send-private\n", -1);
    }

    /* Begin constructing the next message (which might never be
    ** sent) by beginning with the pull or push cards
    */
    if( syncFlags & SYNC_PULL ){
      blob_appendf(&send, "pull %s %s\n", zSCode, zPCode);
      nCardSent++;
    }
    if( syncFlags & SYNC_PUSH ){
      blob_appendf(&send, "push %s %s\n", zSCode, zPCode);
      nCardSent++;
    }
    go = 0;
    nUvGimmeSent = 0;
    nUvFileRcvd = 0;

    /* Process the reply that came back from the server */
    while( blob_line(&recv, &xfer.line) ){
      if( blob_buffer(&xfer.line)[0]=='#' ){
        const char *zLine = blob_buffer(&xfer.line);
        if( memcmp(zLine, "# timestamp ", 12)==0 ){
          char zTime[20];
          double rDiff;
          sqlite3_snprintf(sizeof(zTime), zTime, "%.19s", &zLine[12]);
          rDiff = db_double(9e99, "SELECT julianday('%q') - %.17g",
                            zTime, rArrivalTime);
          if( rDiff>9e98 || rDiff<-9e98 ) rDiff = 0.0;
          if( rDiff*24.0*3600.0 >= -(blob_size(&recv)/5000.0 + 20) ){
            rDiff = 0.0;
          }
          if( fossil_fabs(rDiff)>fossil_fabs(rSkew) ) rSkew = rDiff;
        }
        nCardRcvd++;
        continue;
      }
      xfer.nToken = blob_tokenize(&xfer.line, xfer.aToken, count(xfer.aToken));
      nCardRcvd++;
      if( (syncFlags & SYNC_VERBOSE)!=0 && recv.nUsed>0 ){
        pctDone = (recv.iCursor*100)/recv.nUsed;
        if( pctDone!=lastPctDone ){
          fossil_print("\rprocessed: %d%%         ", pctDone);
          lastPctDone = pctDone;
          fflush(stdout);
        }
      }

      /*   file HASH SIZE \n CONTENT
      **   file HASH DELTASRC SIZE \n CONTENT
      **
      ** Receive a file transmitted from the server.
      */
      if( blob_eq(&xfer.aToken[0],"file") ){
        xfer_accept_file(&xfer, (syncFlags & SYNC_CLONE)!=0, 0, 0);
        nArtifactRcvd++;
      }else

      /*   cfile HASH USIZE CSIZE \n CONTENT
      **   cfile HASH DELTASRC USIZE CSIZE \n CONTENT
      **
      ** Receive a compressed file transmitted from the server.
      */
      if( blob_eq(&xfer.aToken[0],"cfile") ){
        xfer_accept_compressed_file(&xfer, 0, 0);
        nArtifactRcvd++;
      }else

      /*   uvfile NAME MTIME HASH SIZE FLAGS \n CONTENT
      **
      ** Accept an unversioned file from the server.
      */
      if( blob_eq(&xfer.aToken[0], "uvfile") ){
        xfer_accept_unversioned_file(&xfer, 1);
        nArtifactRcvd++;
        nUvFileRcvd++;
        if( syncFlags & SYNC_VERBOSE ){
          fossil_print("\rUnversioned-file received: %s\n",
                       blob_str(&xfer.aToken[1]));
        }
      }else

      /*   gimme HASH
      **
      ** Server is requesting a file.  If the file is a manifest, assume
      ** that the server will also want to know all of the content files
      ** associated with the manifest and send those too.
      */
      if( blob_eq(&xfer.aToken[0], "gimme")
       && xfer.nToken==2
       && blob_is_hname(&xfer.aToken[1])
      ){
        if( syncFlags & SYNC_PUSH ){
          int rid = rid_from_uuid(&xfer.aToken[1], 0, 0);
          if( rid ) send_file(&xfer, rid, &xfer.aToken[1], 0);
        }
      }else

      /*   igot HASH  ?PRIVATEFLAG?
      **
      ** Server announces that it has a particular file.  If this is
      ** not a file that we have and we are pulling, then create a
      ** phantom to cause this file to be requested on the next cycle.
      ** Always remember that the server has this file so that we do
      ** not transmit it by accident.
      **
      ** If the PRIVATE argument exists and is 1, then the file is
      ** private.  Pretend it does not exists if we are not pulling
      ** private files.
      */
      if( xfer.nToken>=2
       && blob_eq(&xfer.aToken[0], "igot")
       && blob_is_hname(&xfer.aToken[1])
      ){
        int rid;
        int isPriv = xfer.nToken>=3 && blob_eq(&xfer.aToken[2],"1");
        rid = rid_from_uuid(&xfer.aToken[1], 0, 0);
        if( rid>0 ){
          if( !isPriv ) content_make_public(rid);
        }else if( isPriv && !g.perm.Private ){
          /* ignore private files */
        }else if( (syncFlags & (SYNC_PULL|SYNC_CLONE))!=0 ){
          rid = content_new(blob_str(&xfer.aToken[1]), isPriv);
          if( rid ) newPhantom = 1;
        }
        remote_has(rid);
      }else

      /*   uvigot NAME MTIME HASH SIZE
      **
      ** Server announces that it has a particular unversioned file.  The
      ** server will only send this card if the client had previously sent
      ** a "pragma uv-hash" card with a hash that does not match.
      **
      ** If the identified file needs to be transferred, then setup for the
      ** transfer.  Generate a "uvgimme" card in the reply if the server
      ** version is newer than the client.  Generate a "uvfile" card if
      ** the client version is newer than the server.  If HASH is "-"
      ** (indicating that the file has been deleted) and MTIME is newer,
      ** then do the deletion.
      */
      if( xfer.nToken==5
       && blob_eq(&xfer.aToken[0], "uvigot")
       && blob_is_filename(&xfer.aToken[1])
       && blob_is_int64(&xfer.aToken[2], &mtime)
       && blob_is_int(&xfer.aToken[4], &size)
       && (blob_eq(&xfer.aToken[3],"-") || blob_is_hname(&xfer.aToken[3]))
      ){
        const char *zName = blob_str(&xfer.aToken[1]);
        const char *zHash = blob_str(&xfer.aToken[3]);
        int iStatus;
        iStatus = unversioned_status(zName, mtime, zHash);
        if( (syncFlags & SYNC_UV_REVERT)!=0 ){
          if( iStatus==4 ) iStatus = 2;
          if( iStatus==5 ) iStatus = 1;
        }
        if( syncFlags & (SYNC_UV_TRACE|SYNC_UV_DRYRUN) ){
          const char *zMsg = 0;
          switch( iStatus ){
            case 0:
            case 1: zMsg = "UV-PULL";             break;
            case 2: zMsg = "UV-PULL-MTIME-ONLY";  break;
            case 4: zMsg = "UV-PUSH-MTIME-ONLY";  break;
            case 5: zMsg = "UV-PUSH";             break;
          }
          if( zMsg ) fossil_print("\r%s: %s\n", zMsg, zName);
          if( syncFlags & SYNC_UV_DRYRUN ){
            iStatus = 99;  /* Prevent any changes or reply messages */
          }
        }
        if( iStatus<=1 ){
          if( zHash[0]!='-' ){
            blob_appendf(xfer.pOut, "uvgimme %s\n", zName);
            nCardSent++;
            nUvGimmeSent++;
            db_multi_exec("DELETE FROM unversioned WHERE name=%Q", zName);
          }else if( iStatus==1 ){
            db_multi_exec(
               "UPDATE unversioned"
               "   SET mtime=%lld, hash=NULL, sz=0, encoding=0, content=NULL"
               " WHERE name=%Q", mtime, zName
            );
            db_unset("uv-hash", 0);
          }
        }else if( iStatus==2 ){
          db_multi_exec(
            "UPDATE unversioned SET mtime=%lld WHERE name=%Q", mtime, zName
          );
          db_unset("uv-hash", 0);
        }
        if( iStatus<=3 ){
          db_multi_exec("DELETE FROM uv_tosend WHERE name=%Q", zName);
        }else if( iStatus==4 ){
          db_multi_exec("UPDATE uv_tosend SET mtimeOnly=1 WHERE name=%Q",zName);
        }else if( iStatus==5 ){
          db_multi_exec("REPLACE INTO uv_tosend(name,mtimeOnly) VALUES(%Q,0)",
                        zName);
        }
      }else

      /*   push  SERVERCODE  PRODUCTCODE
      **
      ** Should only happen in response to a clone.  This message tells
      ** the client what product to use for the new database.
      */
      if( blob_eq(&xfer.aToken[0],"push")
       && xfer.nToken==3
       && (syncFlags & SYNC_CLONE)!=0
       && blob_is_hname(&xfer.aToken[2])
      ){
        if( zPCode==0 ){
          zPCode = mprintf("%b", &xfer.aToken[2]);
          db_set("project-code", zPCode, 0);
        }
        if( cloneSeqno>0 ) blob_appendf(&send, "clone 3 %d\n", cloneSeqno);
        nCardSent++;
      }else

      /*   config NAME SIZE \n CONTENT
      **
      ** Receive a configuration value from the server.
      **
      ** The received configuration setting is silently ignored if it was
      ** not requested by a prior "reqconfig" sent from client to server.
      */
      if( blob_eq(&xfer.aToken[0],"config") && xfer.nToken==3
          && blob_is_int(&xfer.aToken[2], &size) ){
        const char *zName = blob_str(&xfer.aToken[1]);
        Blob content;
        blob_zero(&content);
        blob_extract(xfer.pIn, size, &content);
        g.perm.Admin = g.perm.RdAddr = 1;
        configure_receive(zName, &content, origConfigRcvMask);
        nCardRcvd++;
        nArtifactRcvd++;
        blob_reset(&content);
        blob_seek(xfer.pIn, 1, BLOB_SEEK_CUR);
      }else


      /*    cookie TEXT
      **
      ** The server might include a cookie in its reply.  The client
      ** should remember this cookie and send it back to the server
      ** in its next query.
      **
      ** Each cookie received overwrites the prior cookie from the
      ** same server.
      */
      if( blob_eq(&xfer.aToken[0], "cookie") && xfer.nToken==2 ){
        db_set("cookie", blob_str(&xfer.aToken[1]), 0);
      }else


      /*    private
      **
      ** This card indicates that the next "file" or "cfile" will contain
      ** private content.
      */
      if( blob_eq(&xfer.aToken[0], "private") ){
        xfer.nextIsPrivate = 1;
      }else


      /*    clone_seqno N
      **
      ** When doing a clone, the server tries to send all of its artifacts
      ** in sequence.  This card indicates the sequence number of the next
      ** blob that needs to be sent.  If N<=0 that indicates that all blobs
      ** have been sent.
      */
      if( blob_eq(&xfer.aToken[0], "clone_seqno") && xfer.nToken==2 ){
        blob_is_int(&xfer.aToken[1], &cloneSeqno);
      }else

      /*   message MESSAGE
      **
      ** Print a message.  Similar to "error" but does not stop processing.
      **
      ** If the "login failed" message is seen, clear the sync password prior
      ** to the next cycle.
      */
      if( blob_eq(&xfer.aToken[0],"message") && xfer.nToken==2 ){
        char *zMsg = blob_terminate(&xfer.aToken[1]);
        defossilize(zMsg);
        if( (syncFlags & SYNC_PUSH) && zMsg
            && sqlite3_strglob("pull only *", zMsg)==0 ){
          syncFlags &= ~SYNC_PUSH;
          zMsg = 0;
        }
        if( zMsg && zMsg[0] ){
          fossil_force_newline();
          fossil_print("Server says: %s\n", zMsg);
        }
      }else

      /*    pragma NAME VALUE...
      **
      ** The server can send pragmas to try to convey meta-information to
      ** the client.  These are informational only.  Unknown pragmas are
      ** silently ignored.
      */
      if( blob_eq(&xfer.aToken[0], "pragma") && xfer.nToken>=2 ){
        /* If the server is unwill to accept new unversioned content (because
        ** this client lacks the necessary permissions) then it sends a
        ** "uv-pull-only" pragma so that the client will know not to waste
        ** bandwidth trying to upload unversioned content.  If the server
        ** does accept new unversioned content, it sends "uv-push-ok".
        */
        if( blob_eq(&xfer.aToken[1], "uv-pull-only") ){
          if( syncFlags & SYNC_UV_REVERT ) uvDoPush = 1;
        }else if( blob_eq(&xfer.aToken[1], "uv-push-ok") ){
          uvDoPush = 1;
        }
      }else

      /*   error MESSAGE
      **
      ** Report an error and abandon the sync session.
      **
      ** Except, when cloning we will sometimes get an error on the
      ** first message exchange because the project-code is unknown
      ** and so the login card on the request was invalid.  The project-code
      ** is returned in the reply before the error card, so second and
      ** subsequent messages should be OK.  Nevertheless, we need to ignore
      ** the error card on the first message of a clone.
      */
      if( blob_eq(&xfer.aToken[0],"error") && xfer.nToken==2 ){
        if( (syncFlags & SYNC_CLONE)==0 || nCycle>0 ){
          char *zMsg = blob_terminate(&xfer.aToken[1]);
          defossilize(zMsg);
          fossil_force_newline();
          fossil_print("Error: %s\n", zMsg);
          if( fossil_strcmp(zMsg, "login failed")==0 ){
            if( nCycle<2 ){
              g.url.passwd = 0;
              go = 1;
              if( g.cgiOutput==0 ){
                g.url.flags |= URL_PROMPT_PW;
                g.url.flags &= ~URL_PROMPTED;
                url_prompt_for_password();
                url_remember();
              }
            }else{
              nErr++;
            }
          }else{
            blob_appendf(&xfer.err, "server says: %s\n", zMsg);
            nErr++;
          }
          break;
        }
      }else

      /* Unknown message */
      if( xfer.nToken>0 ){
        if( blob_str(&xfer.aToken[0])[0]=='<' ){
          fossil_warning(
            "server replies with HTML instead of fossil sync protocol:\n%b",
            &recv
          );
          nErr++;
          break;
        }
        blob_appendf(&xfer.err, "unknown command: [%b]\n", &xfer.aToken[0]);
      }

      if( blob_size(&xfer.err) ){
        fossil_force_newline();
        fossil_warning("%b", &xfer.err);
        nErr++;
        break;
      }
      blobarray_reset(xfer.aToken, xfer.nToken);
      blob_reset(&xfer.line);
    }
    origConfigRcvMask = 0;
    if( nCardRcvd>0 && (syncFlags & SYNC_VERBOSE) ){
      fossil_print(zValueFormat /*works-like:"%s%d%d%d%d"*/, "Received:",
                   blob_size(&recv), nCardRcvd,
                   xfer.nFileRcvd, xfer.nDeltaRcvd + xfer.nDanglingFile);
    }else{
      fossil_print(zBriefFormat /*works-like:"%d%d%d"*/,
                   nRoundtrip, nArtifactSent, nArtifactRcvd);
    }
    blob_reset(&recv);
    nCycle++;

    /* If we received one or more files on the previous exchange but
    ** there are still phantoms, then go another round.
    */
    nFileRecv = xfer.nFileRcvd + xfer.nDeltaRcvd + xfer.nDanglingFile;
    if( (nFileRecv>0 || newPhantom) && db_exists("SELECT 1 FROM phantom") ){
      go = 1;
      mxPhantomReq = nFileRecv*2;
      if( mxPhantomReq<200 ) mxPhantomReq = 200;
    }else if( (syncFlags & SYNC_CLONE)!=0 && nFileRecv>0 ){
      go = 1;
    }
    nCardRcvd = 0;
    xfer.nFileRcvd = 0;
    xfer.nDeltaRcvd = 0;
    xfer.nDanglingFile = 0;

    /* If we have one or more files queued to send, then go
    ** another round
    */
    if( xfer.nFileSent+xfer.nDeltaSent>0 || uvDoPush ){
      go = 1;
    }

    /* If this is a clone, the go at least two rounds */
    if( (syncFlags & SYNC_CLONE)!=0 && nCycle==1 ) go = 1;

    /* Stop the cycle if the server sends a "clone_seqno 0" card and
    ** we have gone at least two rounds.  Always go at least two rounds
    ** on a clone in order to be sure to retrieve the configuration
    ** information which is only sent on the second round.
    */
    if( cloneSeqno<=0 && nCycle>1 ) go = 0;

    /* Continue looping as long as new uvfile cards are being received
    ** and uvgimme cards are being sent. */
    if( nUvGimmeSent>0 && (nUvFileRcvd>0 || nCycle<3) ) go = 1;

    db_multi_exec("DROP TABLE onremote");
    if( go ){
      manifest_crosslink_end(MC_PERMIT_HOOKS);
    }else{
      manifest_crosslink_end(MC_PERMIT_HOOKS);
      content_enable_dephantomize(1);
    }
    db_end_transaction(0);
  };
  transport_stats(&nSent, &nRcvd, 1);
  if( (rSkew*24.0*3600.0) > 10.0 ){
     fossil_warning("*** time skew *** server is fast by %s",
                    db_timespan_name(rSkew));
     g.clockSkewSeen = 1;
  }else if( rSkew*24.0*3600.0 < -10.0 ){
     fossil_warning("*** time skew *** server is slow by %s",
                    db_timespan_name(-rSkew));
     g.clockSkewSeen = 1;
  }

  fossil_force_newline();
  fossil_print(
     "%s done, sent: %lld  received: %lld  ip: %s\n",
     zOpType, nSent, nRcvd, g.zIpAddr);
  transport_close(&g.url);
  transport_global_shutdown(&g.url);
  if( nErr && go==2 ){
    db_multi_exec("DROP TABLE onremote");
    manifest_crosslink_end(MC_PERMIT_HOOKS);
    content_enable_dephantomize(1);
    db_end_transaction(0);
  }
  if( (syncFlags & SYNC_CLONE)==0 && g.rcvid && fossil_any_has_fork(g.rcvid) ){
    fossil_warning("***** WARNING: a fork has occurred *****\n"
                   "use \"fossil leaves -multiple\" for more details.");
  }
  return nErr;
}
