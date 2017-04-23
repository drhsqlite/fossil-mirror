/*
** Copyright (c) 2009 D. Richard Hipp
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
** This file manages low-level SSL communications.
**
** This file implements a singleton.  A single SSL connection may be active
** at a time.  State information is stored in static variables.  The identity
** of the server is held in global variables that are set by url_parse().
**
** SSL support is abstracted out into this module because Fossil can
** be compiled without SSL support (which requires OpenSSL library)
*/

#include "config.h"

#ifdef FOSSIL_ENABLE_SSL

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "http_ssl.h"
#include <assert.h>
#include <sys/types.h>

/*
** There can only be a single OpenSSL IO connection open at a time.
** State information about that IO is stored in the following
** local variables:
*/
static int sslIsInit = 0;    /* True after global initialization */
static BIO *iBio = 0;        /* OpenSSL I/O abstraction */
static char *sslErrMsg = 0;  /* Text of most recent OpenSSL error */
static SSL_CTX *sslCtx;      /* SSL context */
static SSL *ssl;


/*
** Clear the SSL error message
*/
static void ssl_clear_errmsg(void){
  free(sslErrMsg);
  sslErrMsg = 0;
}

/*
** Set the SSL error message.
*/
void ssl_set_errmsg(const char *zFormat, ...){
  va_list ap;
  ssl_clear_errmsg();
  va_start(ap, zFormat);
  sslErrMsg = vmprintf(zFormat, ap);
  va_end(ap);
}

/*
** Return the current SSL error message
*/
const char *ssl_errmsg(void){
  return sslErrMsg;
}

/*
** When a server requests a client certificate that hasn't been provided,
** display a warning message explaining what to do next.
*/
static int ssl_client_cert_callback(SSL *ssl, X509 **x509, EVP_PKEY **pkey){
  fossil_warning("The remote server requested a client certificate for "
    "authentication. Specify the pathname to a file containing the PEM "
    "encoded certificate and private key with the --ssl-identity option "
    "or the ssl-identity setting.");
  return 0; /* no cert available */
}

/*
** Call this routine once before any other use of the SSL interface.
** This routine does initial configuration of the SSL module.
*/
void ssl_global_init(void){
  const char *zCaSetting = 0, *zCaFile = 0, *zCaDirectory = 0;
  const char *identityFile;

  if( sslIsInit==0 ){
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
    sslCtx = SSL_CTX_new(SSLv23_client_method());
    /* Disable SSLv2 and SSLv3 */
    SSL_CTX_set_options(sslCtx, SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);

    /* Set up acceptable CA root certificates */
    zCaSetting = db_get("ssl-ca-location", 0);
    if( zCaSetting==0 || zCaSetting[0]=='\0' ){
      /* CA location not specified, use platform's default certificate store */
      X509_STORE_set_default_paths(SSL_CTX_get_cert_store(sslCtx));
    }else{
      /* User has specified a CA location, make sure it exists and use it */
      switch( file_isdir(zCaSetting) ){
        case 0: { /* doesn't exist */
          fossil_fatal("ssl-ca-location is set to '%s', "
              "but is not a file or directory", zCaSetting);
          break;
        }
        case 1: { /* directory */
          zCaDirectory = zCaSetting;
          break;
        }
        case 2: { /* file */
          zCaFile = zCaSetting;
          break;
        }
      }
      if( SSL_CTX_load_verify_locations(sslCtx, zCaFile, zCaDirectory)==0 ){
        fossil_fatal("Failed to use CA root certificates from "
          "ssl-ca-location '%s'", zCaSetting);
      }
    }

    /* Load client SSL identity, preferring the filename specified on the
    ** command line */
    if( g.zSSLIdentity!=0 ){
      identityFile = g.zSSLIdentity;
    }else{
      identityFile = db_get("ssl-identity", 0);
    }
    if( identityFile!=0 && identityFile[0]!='\0' ){
      if( SSL_CTX_use_certificate_file(sslCtx,identityFile,SSL_FILETYPE_PEM)!=1
       || SSL_CTX_use_PrivateKey_file(sslCtx,identityFile,SSL_FILETYPE_PEM)!=1
      ){
        fossil_fatal("Could not load SSL identity from %s", identityFile);
      }
    }
    /* Register a callback to tell the user what to do when the server asks
    ** for a cert */
    SSL_CTX_set_client_cert_cb(sslCtx, ssl_client_cert_callback);

    sslIsInit = 1;
  }
}

/*
** Call this routine to shutdown the SSL module prior to program exit.
*/
void ssl_global_shutdown(void){
  if( sslIsInit ){
    SSL_CTX_free(sslCtx);
    ssl_clear_errmsg();
    sslIsInit = 0;
  }
}

/*
** Close the currently open SSL connection.  If no connection is open,
** this routine is a no-op.
*/
void ssl_close(void){
  if( iBio!=NULL ){
    (void)BIO_reset(iBio);
    BIO_free_all(iBio);
    iBio = NULL;
  }
}

/* See RFC2817 for details */
static int establish_proxy_tunnel(UrlData *pUrlData, BIO *bio){
  int rc, httpVerMin;
  char *bbuf;
  Blob snd, reply;
  int done=0,end=0;
  blob_zero(&snd);
  blob_appendf(&snd, "CONNECT %s:%d HTTP/1.1\r\n", pUrlData->hostname,
      pUrlData->proxyOrigPort);
  blob_appendf(&snd, "Host: %s:%d\r\n", pUrlData->hostname, pUrlData->proxyOrigPort);
  if( pUrlData->proxyAuth ){
    blob_appendf(&snd, "Proxy-Authorization: %s\r\n", pUrlData->proxyAuth);
  }
  blob_append(&snd, "Proxy-Connection: keep-alive\r\n", -1);
  blob_appendf(&snd, "User-Agent: %s\r\n", get_user_agent());
  blob_append(&snd, "\r\n", 2);
  BIO_write(bio, blob_buffer(&snd), blob_size(&snd));
  blob_reset(&snd);

  /* Wait for end of reply */
  blob_zero(&reply);
  do{
    int len;
    char buf[256];
    len = BIO_read(bio, buf, sizeof(buf));
    blob_append(&reply, buf, len);

    bbuf = blob_buffer(&reply);
    len = blob_size(&reply);
    while(end < len) {
      if(bbuf[end] == '\r') {
        if(len - end < 4) {
          /* need more data */
          break;
        }
        if(memcmp(&bbuf[end], "\r\n\r\n", 4) == 0) {
          done = 1;
          break;
        }
      }
      end++;
    }
  }while(!done);
  sscanf(bbuf, "HTTP/1.%d %d", &httpVerMin, &rc);
  blob_reset(&reply);
  return rc;
}

/*
** Open an SSL connection.  The identify of the server is determined
** as follows:
**
**    g.url.name      Name of the server.  Ex: www.fossil-scm.org
**    pUrlData->port  TCP/IP port to use.  Ex: 80
**
** Return the number of errors.
*/
int ssl_open(UrlData *pUrlData){
  X509 *cert;
  int hasSavedCertificate = 0;
  int trusted = 0;
  unsigned long e;

  ssl_global_init();

  /* Get certificate for current server from global config and
   * (if we have it in config) add it to certificate store.
   */
  cert = ssl_get_certificate(pUrlData, &trusted);
  if ( cert!=NULL ){
    X509_STORE_add_cert(SSL_CTX_get_cert_store(sslCtx), cert);
    X509_free(cert);
    hasSavedCertificate = 1;
  }

  if( pUrlData->useProxy ){
    int rc;
    char *connStr = mprintf("%s:%d", g.url.name, pUrlData->port);
    BIO *sBio = BIO_new_connect(connStr);
    free(connStr);
    if( BIO_do_connect(sBio)<=0 ){
      ssl_set_errmsg("SSL: cannot connect to proxy %s:%d (%s)",
            pUrlData->name, pUrlData->port, ERR_reason_error_string(ERR_get_error()));
      ssl_close();
      return 1;
    }
    rc = establish_proxy_tunnel(pUrlData, sBio);
    if( rc<200||rc>299 ){
      ssl_set_errmsg("SSL: proxy connect failed with HTTP status code %d", rc);
      return 1;
    }

    pUrlData->path = pUrlData->proxyUrlPath;

    iBio = BIO_new_ssl(sslCtx, 1);
    BIO_push(iBio, sBio);
  }else{
    iBio = BIO_new_ssl_connect(sslCtx);
  }
  if( iBio==NULL ) {
    ssl_set_errmsg("SSL: cannot open SSL (%s)",
                    ERR_reason_error_string(ERR_get_error()));
    return 1;
  }
  BIO_get_ssl(iBio, &ssl);

#if (SSLEAY_VERSION_NUMBER >= 0x00908070) && !defined(OPENSSL_NO_TLSEXT)
  if( !SSL_set_tlsext_host_name(ssl, (pUrlData->useProxy?pUrlData->hostname:pUrlData->name)) ){
    fossil_warning("WARNING: failed to set server name indication (SNI), "
                  "continuing without it.\n");
  }
#endif

  SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

  if( !pUrlData->useProxy ){
    char *connStr = mprintf("%s:%d", pUrlData->name, pUrlData->port);
    BIO_set_conn_hostname(iBio, connStr);
    free(connStr);
    if( BIO_do_connect(iBio)<=0 ){
      ssl_set_errmsg("SSL: cannot connect to host %s:%d (%s)",
          pUrlData->name, pUrlData->port, ERR_reason_error_string(ERR_get_error()));
      ssl_close();
      return 1;
    }
  }

  if( BIO_do_handshake(iBio)<=0 ) {
    ssl_set_errmsg("Error establishing SSL connection %s:%d (%s)",
        pUrlData->useProxy?pUrlData->hostname:pUrlData->name,
        pUrlData->useProxy?pUrlData->proxyOrigPort:pUrlData->port,
        ERR_reason_error_string(ERR_get_error()));
    ssl_close();
    return 1;
  }
  /* Check if certificate is valid */
  cert = SSL_get_peer_certificate(ssl);

  if ( cert==NULL ){
    ssl_set_errmsg("No SSL certificate was presented by the peer");
    ssl_close();
    return 1;
  }

  if( trusted<=0 && (e = SSL_get_verify_result(ssl)) != X509_V_OK ){
    char *desc, *prompt;
    const char *warning = "";
    Blob ans;
    char cReply;
    BIO *mem;
    unsigned char md[32];
    unsigned int mdLength = 31;

    mem = BIO_new(BIO_s_mem());
    X509_NAME_print_ex(mem, X509_get_subject_name(cert), 2, XN_FLAG_MULTILINE);
    BIO_puts(mem, "\n\nIssued By:\n\n");
    X509_NAME_print_ex(mem, X509_get_issuer_name(cert), 2, XN_FLAG_MULTILINE);
    BIO_puts(mem, "\n\nSHA1 Fingerprint:\n\n ");
    if(X509_digest(cert, EVP_sha1(), md, &mdLength)){
      int j;
      for( j = 0; j < mdLength; ++j ) {
        BIO_printf(mem, " %02x", md[j]);
      }
    }
    BIO_write(mem, "", 1); /* nul-terminate mem buffer */
    BIO_get_mem_data(mem, &desc);

    if( hasSavedCertificate ){
      warning = "WARNING: Certificate doesn't match the "
                "saved certificate for this host!";
    }
    prompt = mprintf("\nSSL verification failed: %s\n"
        "Certificate received: \n\n%s\n\n%s\n"
        "Either:\n"
        " * verify the certificate is correct using the "
        "SHA1 fingerprint above\n"
        " * use the global ssl-ca-location setting to specify your CA root\n"
        "   certificates list\n\n"
        "If you are not expecting this message, answer no and "
        "contact your server\nadministrator.\n\n"
        "Accept certificate for host %s (a=always/y/N)? ",
        X509_verify_cert_error_string(e), desc, warning,
        pUrlData->useProxy?pUrlData->hostname:pUrlData->name);
    BIO_free(mem);

    prompt_user(prompt, &ans);
    free(prompt);
    cReply = blob_str(&ans)[0];
    blob_reset(&ans);
    if( cReply!='y' && cReply!='Y' && cReply!='a' && cReply!='A') {
      X509_free(cert);
      ssl_set_errmsg("SSL certificate declined");
      ssl_close();
      return 1;
    }
    if( cReply=='a' || cReply=='A') {
      if ( trusted==0 ){
        prompt_user("\nSave this certificate as fully trusted (a=always/N)? ",
                    &ans);
        cReply = blob_str(&ans)[0];
        trusted = ( cReply=='a' || cReply=='A' );
        blob_reset(&ans);
      }
      ssl_save_certificate(pUrlData, cert, trusted);
    }
  }

  /* Set the Global.zIpAddr variable to the server we are talking to.
  ** This is used to populate the ipaddr column of the rcvfrom table,
  ** if any files are received from the server.
  */
  {
    /* IPv4 only code */
    const unsigned char *ip = (const unsigned char *) BIO_ptr_ctrl(iBio,BIO_C_GET_CONNECT,2);
    g.zIpAddr = mprintf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  }

  X509_free(cert);
  return 0;
}

/*
** Save certificate to global config.
*/
void ssl_save_certificate(UrlData *pUrlData, X509 *cert, int trusted){
  BIO *mem;
  char *zCert, *zHost;

  mem = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(mem, cert);
  BIO_write(mem, "", 1); /* nul-terminate mem buffer */
  BIO_get_mem_data(mem, &zCert);
  zHost = mprintf("cert:%s", pUrlData->useProxy?pUrlData->hostname:pUrlData->name);
  db_set(zHost, zCert, 1);
  free(zHost);
  zHost = mprintf("trusted:%s", pUrlData->useProxy?pUrlData->hostname:pUrlData->name);
  db_set_int(zHost, trusted, 1);
  free(zHost);
  BIO_free(mem);
}

/*
** Get certificate for pUrlData->urlName from global config.
** Return NULL if no certificate found.
*/
X509 *ssl_get_certificate(UrlData *pUrlData, int *pTrusted){
  char *zHost, *zCert;
  BIO *mem;
  X509 *cert;

  zHost = mprintf("cert:%s",
      pUrlData->useProxy ? pUrlData->hostname : pUrlData->name);
  zCert = db_get(zHost, NULL);
  free(zHost);
  if ( zCert==NULL )
    return NULL;

  if ( pTrusted!=0 ){
    zHost = mprintf("trusted:%s",
             pUrlData->useProxy ? pUrlData->hostname : pUrlData->name);
    *pTrusted = db_get_int(zHost, 0);
    free(zHost);
  }

  mem = BIO_new(BIO_s_mem());
  BIO_puts(mem, zCert);
  cert = PEM_read_bio_X509(mem, NULL, 0, NULL);
  free(zCert);
  BIO_free(mem);
  return cert;
}

/*
** Send content out over the SSL connection.
*/
size_t ssl_send(void *NotUsed, void *pContent, size_t N){
  size_t total = 0;
  while( N>0 ){
    int sent = BIO_write(iBio, pContent, N);
    if( sent<=0 ){
      if( BIO_should_retry(iBio) ){
        continue;
      }
      break;
    }
    total += sent;
    N -= sent;
    pContent = (void*)&((char*)pContent)[sent];
  }
  return total;
}

/*
** Receive content back from the SSL connection.
*/
size_t ssl_receive(void *NotUsed, void *pContent, size_t N){
  size_t total = 0;
  while( N>0 ){
    int got = BIO_read(iBio, pContent, N);
    if( got<=0 ){
      if( BIO_should_retry(iBio) ){
        continue;
      }
      break;
    }
    total += got;
    N -= got;
    pContent = (void*)&((char*)pContent)[got];
  }
  return total;
}

#endif /* FOSSIL_ENABLE_SSL */
