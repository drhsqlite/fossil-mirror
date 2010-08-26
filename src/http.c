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
** This file contains code that implements the client-side HTTP protocol
*/
#include "config.h"
#include "http.h"
#include <assert.h>

/*
** Construct the "login" card with the client credentials.
**
**       login LOGIN NONCE SIGNATURE
**
** The LOGIN is the user id of the client.  NONCE is the sha1 checksum
** of all payload that follows the login card.  SIGNATURE is the sha1
** checksum of the nonce followed by the user password.
**
** Write the constructed login card into pLogin.  pLogin is initialized
** by this routine.
*/
static void http_build_login_card(Blob *pPayload, Blob *pLogin){
  Blob nonce;          /* The nonce */
  const char *zLogin;  /* The user login name */
  const char *zPw;     /* The user password */
  Blob pw;             /* The nonce with user password appended */
  Blob sig;            /* The signature field */

  blob_zero(pLogin);
  if( g.urlUser==0 || strcmp(g.urlUser, "anonymous")==0 ){
     return;  /* If no login card for users "nobody" and "anonymous" */
  }
  blob_zero(&nonce);
  blob_zero(&pw);
  sha1sum_blob(pPayload, &nonce);
  blob_copy(&pw, &nonce);
  zLogin = g.urlUser;
  if( g.urlPasswd ){
    zPw = g.urlPasswd;
  }else if( g.cgiOutput ){
    /* Password failure while doing a sync from the web interface */
    cgi_printf("*** incorrect or missing password for user %h\n", zLogin);
    zPw = 0;
  }else{
    /* Password failure while doing a sync from the command-line interface */
    url_prompt_for_password();
    zPw = g.urlPasswd;
    if( !g.dontKeepUrl ) db_set("last-sync-pw", zPw, 0);
  }

  /* The login card wants the SHA1 hash of the password, so convert the
  ** password to its SHA1 hash it it isn't already a SHA1 hash.
  **
  ** Except, if the password begins with "*" then use the characters
  ** after the "*" as a cleartext password.  Put an "*" at the beginning
  ** of the password to trick a newer client to use the cleartext password
  ** protocol required by legacy servers.
  */
  if( zPw && zPw[0] ){
    if( zPw[0]=='*' ){
      zPw++;
    }else{
      zPw = sha1_shared_secret(zPw, zLogin);
    }
  }

  blob_append(&pw, zPw, -1);
  sha1sum_blob(&pw, &sig);
  blob_appendf(pLogin, "login %F %b %b\n", zLogin, &nonce, &sig);
  blob_reset(&pw);
  blob_reset(&sig);
  blob_reset(&nonce);
}

/*
** Construct an appropriate HTTP request header.  Write the header
** into pHdr.  This routine initializes the pHdr blob.  pPayload is
** the complete payload (including the login card) already compressed.
*/
static void http_build_header(Blob *pPayload, Blob *pHdr){
  int i;
  const char *zSep;

  blob_zero(pHdr);
  i = strlen(g.urlPath);
  if( i>0 && g.urlPath[i-1]=='/' ){
    zSep = "";
  }else{
    zSep = "/";
  }
  blob_appendf(pHdr, "POST %s%sxfer HTTP/1.0\r\n", g.urlPath, zSep);
  if( g.urlProxyAuth ){
    blob_appendf(pHdr, "Proxy-Authorization: %s\n", g.urlProxyAuth);
  }
  blob_appendf(pHdr, "Host: %s\r\n", g.urlHostname);
  blob_appendf(pHdr, "User-Agent: Fossil/" MANIFEST_VERSION "\r\n");
  if( g.fHttpTrace ){
    blob_appendf(pHdr, "Content-Type: application/x-fossil-debug\r\n");
  }else{
    blob_appendf(pHdr, "Content-Type: application/x-fossil\r\n");
  }
  blob_appendf(pHdr, "Content-Length: %d\r\n\r\n", blob_size(pPayload));
}

/*
** Sign the content in pSend, compress it, and send it to the server
** via HTTP or HTTPS.  Get a reply, uncompress the reply, and store the reply
** in pRecv.  pRecv is assumed to be uninitialized when
** this routine is called - this routine will initialize it.
**
** The server address is contain in the "g" global structure.  The
** url_parse() routine should have been called prior to this routine
** in order to fill this structure appropriately.
*/
void http_exchange(Blob *pSend, Blob *pReply, int useLogin){
  Blob login;           /* The login card */
  Blob payload;         /* The complete payload including login card */
  Blob hdr;             /* The HTTP request header */
  int closeConnection;  /* True to close the connection when done */
  int iLength;          /* Length of the reply payload */
  int rc;               /* Result code */
  int iHttpVersion;     /* Which version of HTTP protocol server uses */
  char *zLine;          /* A single line of the reply header */
  int i;                /* Loop counter */

  if( transport_open() ){
    fossil_fatal(transport_errmsg());
  }

  /* Construct the login card and prepare the complete payload */
  blob_zero(&login);
  if( useLogin ) http_build_login_card(pSend, &login);
  if( g.fHttpTrace ){
    payload = login;
    blob_append(&payload, blob_buffer(pSend), blob_size(pSend));
  }else{
    blob_compress2(&login, pSend, &payload);
    blob_reset(&login);
  }

  /* Construct the HTTP request header */
  http_build_header(&payload, &hdr);

  /* When tracing, write the transmitted HTTP message both to standard
  ** output and into a file.  The file can then be used to drive the
  ** server-side like this:
  **
  **      ./fossil http <http-trace-1.txt
  */
  if( g.fHttpTrace ){
    static int traceCnt = 0;
    char *zOutFile;
    FILE *out;
    traceCnt++;
    zOutFile = mprintf("http-trace-%d.txt", traceCnt);
    printf("HTTP SEND: (%s)\n%s%s=======================\n", 
        zOutFile, blob_str(&hdr), blob_str(&payload));
    out = fopen(zOutFile, "w");
    if( out ){
      fwrite(blob_buffer(&hdr), 1, blob_size(&hdr), out);
      fwrite(blob_buffer(&payload), 1, blob_size(&payload), out);
      fclose(out);
    }
  }

  /*
  ** Send the request to the server.
  */
  transport_send(&hdr);
  transport_send(&payload);
  blob_reset(&hdr);
  blob_reset(&payload);
  transport_flip();
  
  /*
  ** Read and interpret the server reply
  */
  closeConnection = 1;
  iLength = -1;
  while( (zLine = transport_receive_line())!=0 && zLine[0]!=0 ){
    if( strncasecmp(zLine, "http/1.", 7)==0 ){
      if( sscanf(zLine, "HTTP/1.%d %d", &iHttpVersion, &rc)!=2 ) goto write_err;
      if( rc!=200 && rc!=302 ){
        int ii;
        for(ii=7; zLine[ii] && zLine[ii]!=' '; ii++){}
        while( zLine[ii]==' ' ) ii++;
        fossil_fatal("server says: %s\n", &zLine[ii]);
        goto write_err;
      }
      if( iHttpVersion==0 ){
        closeConnection = 1;
      }else{
        closeConnection = 0;
      }
    }else if( strncasecmp(zLine, "content-length:", 15)==0 ){
      for(i=15; isspace(zLine[i]); i++){}
      iLength = atoi(&zLine[i]);
    }else if( strncasecmp(zLine, "connection:", 11)==0 ){
      char c;
      for(i=11; isspace(zLine[i]); i++){}
      c = zLine[i];
      if( c=='c' || c=='C' ){
        closeConnection = 1;
      }else if( c=='k' || c=='K' ){
        closeConnection = 0;
      }
    }else if( rc==302 && strncasecmp(zLine, "location:", 9)==0 ){
      int i, j;
      for(i=9; zLine[i] && zLine[i]==' '; i++){}
      if( zLine[i]==0 ) fossil_fatal("malformed redirect: %s", zLine);
      j = strlen(zLine) - 1; 
      if( j>4 && strcmp(&zLine[j-4],"/xfer")==0 ) zLine[j-4] = 0;
      fossil_print("redirect to %s\n", &zLine[i]);
      url_parse(&zLine[i]);
      transport_close();
      http_exchange(pSend, pReply, useLogin);
      return;
    }
  }
  if( rc!=200 ){
    fossil_fatal("\"location:\" missing from 302 redirect reply");
    goto write_err;
  }

  /*
  ** Extract the reply payload that follows the header
  */
  if( iLength<0 ){
    fossil_fatal("server did not reply");
    goto write_err;
  }
  blob_zero(pReply);
  blob_resize(pReply, iLength);
  iLength = transport_receive(blob_buffer(pReply), iLength);
  blob_resize(pReply, iLength);
  if( g.fHttpTrace ){
    printf("HTTP RECEIVE:\n%s\n=======================\n", blob_str(pReply));
  }else{
    blob_uncompress(pReply, pReply);
  }

  /*
  ** Close the connection to the server if appropriate.
  **
  ** FIXME:  There is some bug in the lower layers that prevents the
  ** connection from remaining open.  The easiest fix for now is to
  ** simply close and restart the connection for each round-trip.
  */
  closeConnection = 1; /* FIX ME */
  if( closeConnection ){
    transport_close();
  }else{
    transport_rewind();
  }
  return;

  /* 
  ** Jump to here if an error is seen.
  */
write_err:
  transport_close();
  return;  
}
