/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
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

/*
** This structure holds information about the current state of either
** a client or a server that is participating in xfer.
*/
typedef struct Xfer Xfer;
struct Xfer {
  Blob *pIn;          /* Input text from the other side */
  Blob *pOut;         /* Compose our reply here */
  Blob line;          /* The current line of input */
  Blob aToken[5];     /* Tokenized version of line */
  Blob err;           /* Error message text */
  int nToken;         /* Number of tokens in line */
  int nIGot;          /* Number of "igot" messages sent */
  int nFile;          /* Number of files sent or received */
  int nDelta;         /* Number of deltas sent or received */
  int nDanglingFile;  /* Number of dangling deltas received */
  int mxSend;         /* Stop sending "file" with pOut reaches this size */
};


/*
** The input blob contains a UUID.  Convert it into a record ID.
** Create a phantom record if no prior record exists and
** phantomize is true.
**
** Compare to uuid_to_rid().  This routine takes a blob argument
** and does less error checking.
*/
static int rid_from_uuid(Blob *pUuid, int phantomize){
  int rid = db_int(0, "SELECT rid FROM blob WHERE uuid='%b'", pUuid);
  if( rid==0 && phantomize ){
    rid = content_put(0, blob_str(pUuid), 0);
  }
  return rid;
}


/*
** The aToken[0..nToken-1] blob array is a parse of a "file" line 
** message.  This routine finishes parsing that message and does
** a record insert of the file.
**
** The file line is in one of the following two forms:
**
**      file UUID SIZE \n CONTENT
**      file UUID DELTASRC SIZE \n CONTENT
**
** The content is SIZE bytes immediately following the newline.
** If DELTASRC exists, then the CONTENT is a delta against the
** content of DELTASRC.
**
** If any error occurs, write a message into pErr which has already
** be initialized to an empty string.
*/
static void xfer_accept_file(Xfer *pXfer){
  int n;
  int rid;
  Blob content, hash;
  
  if( pXfer->nToken<3 
   || pXfer->nToken>4
   || !blob_is_uuid(&pXfer->aToken[1])
   || !blob_is_int(&pXfer->aToken[pXfer->nToken-1], &n)
   || n<=0
   || (pXfer->nToken==4 && !blob_is_uuid(&pXfer->aToken[2]))
  ){
    blob_appendf(&pXfer->err, "malformed file line");
    return;
  }
  blob_zero(&content);
  blob_zero(&hash);
  blob_extract(pXfer->pIn, n, &content);
  if( pXfer->nToken==4 ){
    Blob src;
    int srcid = rid_from_uuid(&pXfer->aToken[2], 1);
    if( content_get(srcid, &src)==0 ){
      content_put(&content, blob_str(&hash), srcid);
      blob_appendf(pXfer->pOut, "gimme %b\n", &pXfer->aToken[2]);
      pXfer->nDanglingFile++;
      return;
    }
    pXfer->nDelta++;
    blob_delta_apply(&src, &content, &content);
    blob_reset(&src);
  }else{
    pXfer->nFile++;
  }
  sha1sum_blob(&content, &hash);
  if( !blob_eq_str(&pXfer->aToken[1], blob_str(&hash), -1) ){
    blob_appendf(&pXfer->err, "content does not match sha1 hash");
  }
  blob_reset(&hash);
  rid = content_put(&content, 0, 0);
  if( rid==0 ){
    blob_appendf(&pXfer->err, "%s", g.zErrMsg);
  }else{
    manifest_crosslink(rid, &content);
  }
}

/*
** Try to send a file as a delta.  If successful, return the number
** of bytes in the delta.  If not, return zero.
**
** If srcId is specified, use it.  If not, try to figure out a
** reasonable srcId.
*/
static int send_as_delta(
  Xfer *pXfer,            /* The transfer context */
  int rid,                /* record id of the file to send */
  Blob *pContent,         /* The content of the file to send */
  Blob *pUuid,            /* The UUID of the file to send */
  int srcId               /* Send as a delta against this record */
){
  static const char *azQuery[] = {
    "SELECT srcid FROM delta JOIN pending ON pending.rid=delta.srcid"
    " WHERE delta.rid=%d"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=srcid)",

    "SELECT delta.rid FROM delta JOIN pending ON pending.rid=delta.rid"
    " WHERE srcid=%d"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=delta.rid)",

    "SELECT pid FROM plink JOIN pending ON rid=pid"
    " WHERE cid=%d"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=pid)",

    "SELECT cid FROM plink JOIN pending ON rid=cid"
    " WHERE pid=%d"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=cid)",

    "SELECT pid FROM mlink JOIN pending ON rid=pid"
    " WHERE fid=%d"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=pid)",

    "SELECT fid FROM mlink JOIN pending ON rid=fid"
    " WHERE pid=%d"
    "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=fid)",
  };
  int i;
  Blob src, delta;
  int size = 0;

  for(i=0; srcId==0 && i<count(azQuery); i++){
    srcId = db_int(0, azQuery[i], rid);
  }
  if( srcId && content_get(srcId, &src) ){
    char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", srcId);
    blob_delta_create(&src, pContent, &delta);
    size = blob_size(&delta);
    if( size>=blob_size(pContent)-50 ){
      size = 0;
    }else{
      blob_appendf(pXfer->pOut, "file %b %s %d\n", pUuid, zUuid, size);
      blob_append(pXfer->pOut, blob_buffer(&delta), size);
      blob_appendf(pXfer->pOut, "\n", 1);
    }
    blob_reset(&delta);
    free(zUuid);
    blob_reset(&src);
  }
  return size;
}

/*
** Send the file identified by rid.
*/
static void send_file(Xfer *pXfer, int rid, Blob *pUuid, int srcId){
  Blob content, uuid;
  int size = 0;

  if( db_exists("SELECT 1 FROM sent WHERE rid=%d", rid) ){
     return;
  }
  blob_zero(&uuid);
  if( pUuid==0 ){
    db_blob(&uuid, "SELECT uuid FROM blob WHERE rid=%d AND size>=0", rid);
    if( blob_size(&uuid)==0 ){
      return;
    }
    pUuid = &uuid;
  }
  if( pXfer->mxSend<=blob_size(pXfer->pOut) ){
    blob_appendf(pXfer->pOut, "igot %b\n", pUuid);
    pXfer->nIGot++;
    blob_reset(&uuid);
    return;
  }
  content_get(rid, &content);

  if( blob_size(&content)>100 ){
    size = send_as_delta(pXfer, rid, &content, pUuid, srcId);
  }
  if( size==0 ){
    int size = blob_size(&content);
    blob_appendf(pXfer->pOut, "file %b %d\n", pUuid, size);
    blob_append(pXfer->pOut, blob_buffer(&content), size);
    pXfer->nFile++;
  }else{
    pXfer->nDelta++;
  }
  db_multi_exec("INSERT INTO sent VALUES(%d)", rid);
  blob_reset(&uuid);
}

/*
** This routine runs when either client or server is notified that
** the other side things rid is a leaf manifest.  If we hold
** children of rid, then send them over to the other side.
*/
static void leaf_response(Xfer *pXfer, int rid){
  Stmt q1, q2;
  db_prepare(&q1,
      "SELECT cid, uuid FROM plink, blob"
      " WHERE blob.rid=plink.cid"
      "   AND plink.pid=%d",
      rid
  );
  while( db_step(&q1)==SQLITE_ROW ){
    Blob uuid;
    int cid;

    cid = db_column_int(&q1, 0);
    db_ephemeral_blob(&q1, 1, &uuid);
    send_file(pXfer, cid, &uuid, rid);
    db_prepare(&q2,
       "SELECT pid, uuid, fid FROM mlink, blob"
       " WHERE rid=fid AND mid=%d",
       cid
    );
    while( db_step(&q2)==SQLITE_ROW ){
      int pid, fid;
      pid = db_column_int(&q2, 0);
      db_ephemeral_blob(&q2, 1, &uuid);
      fid = db_column_int(&q2, 2);
      send_file(pXfer, fid, &uuid, pid);
    }
    db_finalize(&q2);
    if( blob_size(pXfer->pOut)<pXfer->mxSend ){
      leaf_response(pXfer, cid);
    }
  }
}

/*
** Sent a leaf message for every leaf.
*/
static void send_leaves(Xfer *pXfer){
  Stmt q;
  db_prepare(&q, 
    "SELECT uuid FROM blob WHERE rid IN"
    "  (SELECT cid FROM plink EXCEPT SELECT pid FROM plink)"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    blob_appendf(pXfer->pOut, "leaf %s\n", zUuid);
  }
  db_finalize(&q);
}

/*
** Sen a gimme message for every phantom.
*/
static void request_phantoms(Xfer *pXfer){
  Stmt q;
  db_prepare(&q, "SELECT uuid FROM phantom JOIN blob USING(rid)");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    blob_appendf(pXfer->pOut, "gimme %s\n", zUuid);
  }
  db_finalize(&q);
}


/*
** Check the signature on an application/x-fossil payload received by
** the HTTP server.  The signature is a line of the following form:
**
**        login LOGIN NONCE SIGNATURE
**
** The NONCE is a random string.  The server will never accept a
** repeat NONCE.  SIGNATURE is the SHA1 checksum of the NONCE 
** concatenated with the users password.
**
** The parameters to this routine are ephermeral blobs holding the
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
*/
void check_login(Blob *pLogin, Blob *pNonce, Blob *pSig){
  Stmt q;
  int rc;

  if( db_exists("SELECT 1 FROM rcvfrom WHERE nonce=%B", pNonce) ){
    return;  /* Never accept a repeated nonce */
  }
  db_prepare(&q, "SELECT pw, cap, uid FROM user WHERE login=%B", pLogin);
  if( db_step(&q)==SQLITE_ROW ){
    Blob pw, combined, hash;
    blob_zero(&pw);
    db_ephemeral_blob(&q, 0, &pw);
    blob_zero(&combined);
    blob_copy(&combined, pNonce);
    blob_append(&combined, blob_buffer(&pw), blob_size(&pw));
    /* CGIDEBUG(("presig=[%s]\n", blob_str(&combined))); */
    sha1sum_blob(&combined, &hash);
    rc = blob_compare(&hash, pSig);
    blob_reset(&hash);
    blob_reset(&combined);
    if( rc==0 ){
      const char *zCap;
      zCap = db_column_text(&q, 1);
      login_set_capabilities(zCap);
      g.userUid = db_column_int(&q, 2);
      g.zLogin = mprintf("%b", pLogin);
      g.zNonce = mprintf("%b", pNonce);
    }
  }
  db_reset(&q);
}


/*
** If this variable is set, disable login checks.  Used for debugging
** only.
*/
static int disableLogin = 0;

/*
** WEBPAGE: xfer
**
** This is the transfer handler on the server side.  The transfer
** message has been uncompressed and placed in the g.cgiIn blob.
** Process this message and form an appropriate reply.
*/
void page_xfer(void){
  int nToken;
  int isPull = 0;
  int isPush = 0;
  int nErr = 0;
  Xfer xfer;

  memset(&xfer, 0, sizeof(xfer));
  blobarray_zero(xfer.aToken, count(xfer.aToken));
  cgi_set_content_type(g.zContentType);
  blob_zero(&xfer.err);
  xfer.pIn = &g.cgiIn;
  xfer.pOut = cgi_output_blob();

  db_begin_transaction();
  db_multi_exec(
     "CREATE TEMP TABLE sent(rid INTEGER PRIMARY KEY);"
  );
  while( blob_line(xfer.pIn, &xfer.line) ){
    xfer.nToken = blob_tokenize(&xfer.line, xfer.aToken, count(xfer.aToken));

    /*   file UUID SIZE \n CONTENT
    **   file UUID DELTASRC SIZE \n CONTENT
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
      xfer_accept_file(&xfer);
      if( blob_size(&xfer.err) ){
        cgi_reset_content();
        @ error %T(blob_str(&xfer.err))
        nErr++;
        break;
      }
    }else

    /*   gimme UUID
    **
    ** Client is requesting a file
    */
    if( blob_eq(&xfer.aToken[0], "gimme")
     && xfer.nToken==2
     && blob_is_uuid(&xfer.aToken[1])
    ){
      if( isPull ){
        int rid = rid_from_uuid(&xfer.aToken[1], 0);
        if( rid ){
          send_file(&xfer, rid, &xfer.aToken[1], 0);
        }
      }
    }else

    /*   igot UUID
    **
    ** Client announces that it has a particular file
    */
    if( xfer.nToken==2
     && blob_eq(&xfer.aToken[0], "igot")
     && blob_is_uuid(&xfer.aToken[1])
    ){
      if( isPush ){
        rid_from_uuid(&xfer.aToken[1], 1);
      }
    }else
  
    
    /*   leaf UUID
    **
    ** Client announces that it has a particular manifest
    */
    if( xfer.nToken==2
     && blob_eq(&xfer.aToken[0], "leaf")
     && blob_is_uuid(&xfer.aToken[1])
    ){
      if( isPull ){
        int rid = rid_from_uuid(&xfer.aToken[1], 0);
        leaf_response(&xfer, rid);
      }
    }else

    /*    pull  SERVERCODE  PROJECTCODE
    **    push  SERVERCODE  PROJECTCODE
    **
    ** The client wants either send or receive
    */
    if( nToken==3
     && (blob_eq(&xfer.aToken[0], "pull") || blob_eq(&xfer.aToken[0], "push"))
     && blob_is_uuid(&xfer.aToken[1])
     && blob_is_uuid(&xfer.aToken[2])
    ){
      const char *zSCode;
      const char *zPCode;

      zSCode = db_get("server-code", 0);
      if( zSCode==0 ){
        fossil_panic("missing server code");
      }
      if( blob_eq_str(&xfer.aToken[1], zSCode, -1) ){
        cgi_reset_content();
        @ error server\sloop
        nErr++;
        break;
      }
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
        if( !g.okRead ){
          cgi_reset_content();
          @ error not\sauthorized\sto\sread
          nErr++;
          break;
        }
        isPull = 1;
      }else{
        if( !g.okWrite ){
          cgi_reset_content();
          @ error not\sauthorized\sto\swrite
          nErr++;
          break;
        }
        send_leaves(&xfer);
        isPush = 1;
      }
    }else

    /*    clone
    **
    ** The client knows nothing.  Tell all.
    */
    if( blob_eq(&xfer.aToken[0], "clone") ){
      login_check_credentials();
      if( !g.okRead || !g.okHistory ){
        cgi_reset_content();
        @ error not\sauthorized\sto\sclone
        nErr++;
        break;
      }
      isPull = 1;
      @ push %s(db_get("server-code", "x")) %s(db_get("project-code", "x"))
      send_leaves(&xfer);
    }else

    /*    login  USER  NONCE  SIGNATURE
    **
    ** Check for a valid login.  This has to happen before anything else.
    */
    if( blob_eq(&xfer.aToken[0], "login")
     && nToken==4
    ){
      if( disableLogin ){
        g.okRead = g.okWrite = 1;
      }else{
        check_login(&xfer.aToken[1], &xfer.aToken[2], &xfer.aToken[3]);
      }
    }else

    /* Unknown message
    */
    {
      cgi_reset_content();
      @ error bad\scommand:\s%F(blob_str(&xfer.line))
    }
    blobarray_reset(xfer.aToken, xfer.nToken);
  }
  if( isPush ){
    request_phantoms(&xfer);
  }
  db_end_transaction(0);
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
  int notUsed;
  if( g.argc!=2 && g.argc!=3 ){
    usage("?MESSAGEFILE?");
  }
  db_must_be_within_tree();
  blob_zero(&g.cgiIn);
  blob_read_from_file(&g.cgiIn, g.argc==2 ? "-" : g.argv[2]);
  disableLogin = 1;
  page_xfer();
  printf("%s\n", cgi_extract_content(&notUsed));
}


/*
** Sync to the host identified in g.urlName and g.urlPath.  This
** routine is called by the client.
**
** Records are pushed to the server if pushFlag is true.  Records
** are pulled if pullFlag is true.  A full sync occurs if both are
** true.
*/
void client_sync(int pushFlag, int pullFlag, int cloneFlag){
  int go = 1;        /* Loop until zero */
  const char *zSCode = db_get("server-code", "x");
  const char *zPCode = db_get("project-code", 0);
  int nMsg = 0;
  int nReq = 0;
  int nFileSend = 0;
  int nNoFileCycle = 0;
  Blob send;        /* Text we are sending to the server */
  Blob recv;        /* Reply we got back from the server */
  Xfer xfer;        /* Transfer data */

  memset(&xfer, 0, sizeof(xfer));
  xfer.pIn = &recv;
  xfer.pOut = &send;

  assert( pushFlag || pullFlag || cloneFlag );
  assert( !g.urlIsFile );          /* This only works for networking */

  db_begin_transaction();
  db_multi_exec(
    "CREATE TEMP TABLE sent(rid INTEGER PRIMARY KEY);"
  );
  blobarray_zero(xfer.aToken, count(xfer.aToken));
  blob_zero(&send);
  blob_zero(&recv);
  blob_zero(&xfer.err);


  while( go ){
    go = 0;
    nReq = nMsg = 0;

    /* Generate a request to be sent to the server.
    ** Always begin with a clone, pull, or push message
    */
    
    if( cloneFlag ){
      blob_appendf(&send, "clone\n");
      pushFlag = 0;
      pullFlag = 0;
      nMsg++;
    }else if( pullFlag ){
      blob_appendf(&send, "pull %s %s\n", zSCode, zPCode);
      send_leaves(&xfer);
      request_phantoms(&xfer);
      nMsg++;
    }
    if( pushFlag ){
      blob_appendf(&send, "push %s %s\n", zSCode, zPCode);
      nMsg++;
    }

    /* Exchange messages with the server */
    nFileSend = xfer.nFile + xfer.nDelta + xfer.nDanglingFile;
    printf("Send:      %3d files, %3d requests, %3d other msgs, %8d bytes\n",
            nFileSend, nReq, nMsg, blob_size(&send));
    xfer.nFile = 0;
    xfer.nDelta = 0;
    xfer.nDanglingFile = 0;
    nReq = nMsg = 0;
    http_exchange(&send, &recv);
    blob_reset(&send);

    /* Process the reply that came back from the server */
    while( blob_line(&recv, &xfer.line) ){
      xfer.nToken = blob_tokenize(&xfer.line, xfer.aToken, count(xfer.aToken));

      /*   file UUID SIZE \n CONTENT
      **   file UUID DELTASRC SIZE \n CONTENT
      **
      ** Receive a file transmitted from the other side
      */
      if( blob_eq(&xfer.aToken[0],"file") ){
        xfer_accept_file(&xfer);
      }else

      /*   gimme UUID
      **
      ** Server is requesting a file
      */
      if( blob_eq(&xfer.aToken[0], "gimme")
       && xfer.nToken==2
       && blob_is_uuid(&xfer.aToken[1])
      ){
        if( pushFlag ){
          int rid = rid_from_uuid(&xfer.aToken[1], 0);
          send_file(&xfer, rid, &xfer.aToken[1], 0);
        }
      }else
  
      /*   igot UUID
      **
      ** Server announces that it has a particular file
      */
      if( xfer.nToken==2
       && blob_eq(&xfer.aToken[0], "igot")
       && blob_is_uuid(&xfer.aToken[1])
      ){
        if( pullFlag ){
          rid_from_uuid(&xfer.aToken[1], 1);
        }
      }else
    
      
      /*   leaf UUID
      **
      ** Server announces that it has a particular manifest
      */
      if( xfer.nToken==2
       && blob_eq(&xfer.aToken[0], "leaf")
       && blob_is_uuid(&xfer.aToken[1])
      ){
        if( pushFlag ){
          int rid = rid_from_uuid(&xfer.aToken[1], 0);
          leaf_response(&xfer, rid);
        }
      }else
  
  
      /*   push  SERVERCODE  PRODUCTCODE
      **
      ** Should only happen in response to a clone.
      */
      if( blob_eq(&xfer.aToken[0],"push")
       && xfer.nToken==3
       && cloneFlag
       && blob_is_uuid(&xfer.aToken[1])
       && blob_is_uuid(&xfer.aToken[2])
      ){
        if( blob_eq_str(&xfer.aToken[1], zSCode, -1) ){
          fossil_fatal("server loop");
        }
        nMsg++;
        if( zPCode==0 ){
          zPCode = mprintf("%b", &xfer.aToken[2]);
          db_set("project-code", zPCode);
        }
        cloneFlag = 0;
        pullFlag = 1;
      }else

      /*   error MESSAGE
      **
      ** Report an error
      */        
      if( blob_eq(&xfer.aToken[0],"error") && xfer.nToken==2 ){
        char *zMsg = blob_terminate(&xfer.aToken[1]);
        defossilize(zMsg);
        blob_appendf(&xfer.err, "server says: %s", zMsg);
      }else

      /* Unknown message */
      {
        blob_appendf(&xfer.err, "unknown command: %b", &xfer.aToken[0]);
      }

      if( blob_size(&xfer.err) ){
        fossil_fatal("%b", &xfer.err);
      }
      blobarray_reset(xfer.aToken, xfer.nToken);
    }
    printf("Received:  %3d files, %3d requests, %3d other msgs, %8d bytes\n",
            xfer.nFile + xfer.nDelta + xfer.nDanglingFile,
            nReq, nMsg, blob_size(&recv));
    blob_reset(&recv);
    if( nFileSend + xfer.nFile + xfer.nDelta + xfer.nDanglingFile==0 ){
      nNoFileCycle++;
      if( nNoFileCycle>1 ){
        go = 0;
      }
    }else{
      nNoFileCycle = 0;
    }
    nReq = nMsg = 0;
    xfer.nFile = 0;
    xfer.nDelta = 0;
    xfer.nDanglingFile = 0;
  };
  http_close();
  db_end_transaction(0);
}
