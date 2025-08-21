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
** at a time.  State information is stored in static variables.
**
** The SSL connections can be either a client or a server.  But all
** connections for a single process must be of the same type, either client
** or server.
**
** SSL support is abstracted out into this module because Fossil can
** be compiled without SSL support (which requires OpenSSL library)
*/

#include "config.h"
#include "http_ssl.h"

#ifdef FOSSIL_ENABLE_SSL

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include <assert.h>
#include <sys/types.h>

/*
** There can only be a single OpenSSL IO connection open at a time.
** State information about that IO is stored in the following
** local variables:
*/
static int sslIsInit = 0;    /* 0: uninit 1: init as client 2: init as server */
static BIO *iBio = 0;        /* OpenSSL I/O abstraction */
static char *sslErrMsg = 0;  /* Text of most recent OpenSSL error */
static SSL_CTX *sslCtx;      /* SSL context */
static SSL *ssl;
static struct {              /* Accept this SSL cert for this session only */
  char *zHost;                  /* Subject or host name */
  char *zHash;                  /* SHA2-256 hash of the cert */
} sException;
static int sslNoCertVerify = 0;  /* Do not verify SSL certs */


/* This is a self-signed cert in the PEM format that can be used when
** no other certs are available.
*/
static const char sslSelfCert[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDMTCCAhkCFGrDmuJkkzWERP/ITBvzwwI2lv0TMA0GCSqGSIb3DQEBCwUAMFQx\n"
"CzAJBgNVBAYTAlVTMQswCQYDVQQIDAJOQzESMBAGA1UEBwwJQ2hhcmxvdHRlMRMw\n"
"EQYDVQQKDApGb3NzaWwtU0NNMQ8wDQYDVQQDDAZGb3NzaWwwIBcNMjExMjI3MTEz\n"
"MTU2WhgPMjEyMTEyMjcxMTMxNTZaMFQxCzAJBgNVBAYTAlVTMQswCQYDVQQIDAJO\n"
"QzESMBAGA1UEBwwJQ2hhcmxvdHRlMRMwEQYDVQQKDApGb3NzaWwtU0NNMQ8wDQYD\n"
"VQQDDAZGb3NzaWwwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCCbTU2\n"
"6GRQHQqLq7vyZ0OxpAxmgfAKCxt6eIz+jBi2ZM/CB5vVXWVh2+SkSiWEA3UZiUqX\n"
"xZlzmS/CglZdiwLLDJML8B4OiV72oivFH/vJ7+cbvh1dTxnYiHuww7GfQngPrLfe\n"
"fiIYPDk1GTUJHBQ7Ue477F7F8vKuHdVgwktF/JDM6M60aSqlo2D/oysirrb+dlur\n"
"Tlv0rjsYOfq6bLAajoL3qi/vek6DNssoywbge4PfbTgS9g7Gcgncbcet5pvaS12J\n"
"avhFcd4JU4Ity49Hl9S/C2MfZ1tE53xVggRwKz4FPj65M5uymTdcxtjKXtCxIE1k\n"
"KxJxXQh7rIYjm+RTAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAFkdtpqcybAzJN8G\n"
"+ONuUm5sXNbWta7JGvm8l0BTSBcCUtJA3hn16iJqXA9KmLnaF2denC4EYk+KlVU1\n"
"QXxskPJ4jB8A5B05jMijYv0nzCxKhviI8CR7GLEEGKzeg9pbW0+O3vaVehoZtdFX\n"
"z3SsCssr9QjCLiApQxMzW1Iv3od2JXeHBwfVMFrWA1VCEUCRs8OSW/VOqDPJLVEi\n"
"G6wxc4kN9dLK+5S29q3nzl24/qzXoF8P9Re5KBCbrwaHgy+OEEceq5jkmfGFxXjw\n"
"pvVCNry5uAhH5NqbXZampUWqiWtM4eTaIPo7Y2mDA1uWhuWtO6F9PsnFJlQHCnwy\n"
"s/TsrXk=\n"
"-----END CERTIFICATE-----\n";

/* This is the private-key corresponding to the cert above
*/
static const char sslSelfPKey[] =
"-----BEGIN PRIVATE KEY-----\n"
"MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCCbTU26GRQHQqL\n"
"q7vyZ0OxpAxmgfAKCxt6eIz+jBi2ZM/CB5vVXWVh2+SkSiWEA3UZiUqXxZlzmS/C\n"
"glZdiwLLDJML8B4OiV72oivFH/vJ7+cbvh1dTxnYiHuww7GfQngPrLfefiIYPDk1\n"
"GTUJHBQ7Ue477F7F8vKuHdVgwktF/JDM6M60aSqlo2D/oysirrb+dlurTlv0rjsY\n"
"Ofq6bLAajoL3qi/vek6DNssoywbge4PfbTgS9g7Gcgncbcet5pvaS12JavhFcd4J\n"
"U4Ity49Hl9S/C2MfZ1tE53xVggRwKz4FPj65M5uymTdcxtjKXtCxIE1kKxJxXQh7\n"
"rIYjm+RTAgMBAAECggEANfTH1vc8yIe7HRzmm9lsf8jF+II4s2705y2H5qY+cvYx\n"
"nKtZJGOG1X0KkYy7CGoFv5K0cSUl3lS5FVamM/yWIzoIex/Sz2C1EIL2aI5as6ez\n"
"jB6SN0/J+XI8+Vt7186/rHxfdIPpxuzjHbxX3HTpScETNWcLrghbrPxakbTPPxwt\n"
"+x7QlPmmkFNuMfvkzToFf9NdwL++44TeBPOpvD/Lrw+eyqdth9RJPq9cM96plh9V\n"
"HuRqeD8+QNafaXBdSQs3FJK/cDK/vWGKZWIfFVSDbDhwYljkXGijreFjtXQfkkpF\n"
"rl1J87/H9Ee7z8fTD2YXQHl+0/rghAVtac3u54dpQQKBgQC2XG3OEeMrOp9dNkUd\n"
"F8VffUg0ecwG+9L3LCe7U71K0kPmXjV6xNnuYcNQu84kptc5vI8wD23p29LaxdNc\n"
"9m0lcw06/YYBOPkNphcHkINYZTvVJF10mL3isymzMaTtwDkZUkOjL1B+MTiFT/qp\n"
"ARKrTYGJ4HxY7+tUkI5pUmg4PQKBgQC3GA4d1Rz3Pb/RRpcsZgWknKsKhoN36mSn\n"
"xFJ3wPBvVv2B1ltTMzh/+the0ty6clzMrvoLERzRcheDsNrc/j/TUVG8sVdBYJwX\n"
"tMZyFW4NVMOErT/1ukh6jBqIMBo6NJL3EV/AKj0yniksgKOr0/AAduAccnGST8Jd\n"
"SHOdjwvHzwKBgGZBq/zqgNTDuYseHGE07CMgcDWkumiMGv8ozlq3mSR0hUiPOTPP\n"
"YFjQjyIdPXnF6FfiyPPtIvgIoNK2LVAqiod+XUPf152l4dnqcW13dn9BvOxGyPTR\n"
"lWCikFaAFviOWjY9r9m4dU1dslDmySqthFd0TZgPvgps9ivkJ0cdw30NAoGAMC/E\n"
"h1VvKiK2OP27C5ROJ+STn1GHiCfIFd81VQ8SODtMvL8NifgRBp2eFFaqgOdYRQZI\n"
"CGGYlAbS6XXCJCdF5Peh62dA75PdgN+y2pOJQzjrvB9cle9Q4++7i9wdCvSLOTr5\n"
"WDnFoWy+qVexu6crovOmR9ZWzYrwPFy1EOJ010ECgYBl7Q+jmjOSqsVwhFZ0U7LG\n"
"diN+vXhWfn1wfOWd8u79oaqU/Oy7xyKW2p3H5z2KFrBM/vib53Lh4EwFZjcX+jVG\n"
"krAmbL+M/hP7z3TD2UbESAzR/c6l7FU45xN84Lsz5npkR8H/uAHuqLgb9e430Mjx\n"
"YNMwdb8rChHHChNZu6zuxw==\n"
"-----END PRIVATE KEY-----\n";

/*
** Read a PEM certificate from memory and push it into an SSL_CTX.
** Return the number of errors.
*/
static int sslctx_use_cert_from_mem(
  SSL_CTX *ctx,
  const char *pData,
  int nData
){
  BIO *in;
  int rc = 1;
  X509 *x = 0;
  X509 *cert = 0;

  in = BIO_new_mem_buf(pData, nData);
  if( in==0 ) goto end_of_ucfm;
  /* x = X509_new_ex(ctx->libctx, ctx->propq); */
  x = X509_new();
  if( x==0 ) goto end_of_ucfm;
  cert = PEM_read_bio_X509(in, &x, 0, 0);
  if( cert==0 ) goto end_of_ucfm;
  rc = SSL_CTX_use_certificate(ctx, x)<=0;
end_of_ucfm:
  X509_free(x);
  BIO_free(in);
  return rc;
}

/*
** Read a PEM private key from memory and add it to an SSL_CTX.
** Return the number of errors.
*/
static int sslctx_use_pkey_from_mem(
  SSL_CTX *ctx,
  const char *pData,
  int nData
){
  int rc = 1;
  BIO *in;
  EVP_PKEY *pkey = 0;

  in = BIO_new_mem_buf(pData, nData);
  if( in==0 ) goto end_of_upkfm;
  pkey = PEM_read_bio_PrivateKey(in, 0, 0, 0);
  if( pkey==0 ) goto end_of_upkfm;
  rc = SSL_CTX_use_PrivateKey(ctx, pkey)<=0;
  EVP_PKEY_free(pkey);
end_of_upkfm:
  BIO_free(in);
  return rc;
}

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
** Convert an OpenSSL ASN1_TIME to an ISO8601 timestamp.
**
** Per RFC 5280, ASN1 timestamps in X.509 certificates must
** be in UTC (Zulu timezone) with no fractional seconds.
**
** If showUtc==1, add " UTC" at the end of the returned string. This is
** not ISO8601-compliant, but makes the displayed value more user-friendly.
*/
static const char *ssl_asn1time_to_iso8601(ASN1_TIME *asn1_time,
                                           int showUtc){
  assert( showUtc==0 || showUtc==1 );
  if( !ASN1_TIME_check(asn1_time) ){
    return mprintf("Bad time value");
  }else{
    char res[20];
    char *pr = res;
    const char *pt = (char *)asn1_time->data;
    /*                   0123456789 1234
    **  UTCTime:         YYMMDDHHMMSSZ      (YY >= 50 ? 19YY : 20YY)
    **  GeneralizedTime: YYYYMMDDHHMMSSZ */
    if( asn1_time->length < 15 ){
      /* UTCTime, fill out century digits */
      *pr++ = pt[0]>='5' ? '1' : '2';
      *pr++ = pt[0]>='5' ? '9' : '0';
    }else{
      /* GeneralizedTime, copy century digits and advance source */
      *pr++ = pt[0]; *pr++ = pt[1];
      pt += 2;
    }
    *pr++ = pt[0]; *pr++ = pt[1]; *pr++ = '-';
    *pr++ = pt[2]; *pr++ = pt[3]; *pr++ = '-';
    *pr++ = pt[4]; *pr++ = pt[5]; *pr++ = ' ';
    *pr++ = pt[6]; *pr++ = pt[7]; *pr++ = ':';
    *pr++ = pt[8]; *pr++ = pt[9]; *pr++ = ':';
    *pr++ = pt[10]; *pr++ = pt[11]; *pr = '\0';
    return mprintf("%s%s", res, (showUtc ? " UTC" : ""));
  }
}

/*
** Call this routine once before any other use of the SSL interface.
** This routine does initial configuration of the SSL module.
*/
static void ssl_global_init_client(void){
  const char *identityFile;

  if( sslIsInit==0 ){
    const char *zFile;
    const char *zCaFile = 0;
    const char *zCaDirectory = 0;
    int i;

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    sslCtx = SSL_CTX_new(SSLv23_client_method());
    /* Disable SSLv2 and SSLv3 */
    SSL_CTX_set_options(sslCtx, SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);

    /* Find the trust store */
    zFile = 0;
    for(i=0; zFile==0 && i<5; i++){
      switch( i ){
        case 0: /* First priority is environmentn variables */
          zFile = fossil_getenv(X509_get_default_cert_file_env());
          break;
        case 1:
          zFile = fossil_getenv(X509_get_default_cert_dir_env());
          break;
        case 2:
          if( !g.repositoryOpen ) db_open_config(0,0);
          zFile = db_get("ssl-ca-location",0);
          break;
        case 3:
          zFile = X509_get_default_cert_file();
          break;
        case 4:
          zFile = X509_get_default_cert_dir();
          break;
      }
      if( zFile==0 ) continue;
      switch( file_isdir(zFile, ExtFILE) ){
        case 0: { /* doesn't exist */
          zFile = 0;
          break;
        }
        case 1: { /* directory */
          zCaFile = 0;
          zCaDirectory = zFile;
          break;
        }
        case 2: { /* file */
          zCaFile = zFile;
          zCaDirectory = 0;
          break;
        }
      }
    }
    if( zFile==0 ){
      /* fossil_fatal("Cannot find a trust store"); */
    }else if( SSL_CTX_load_verify_locations(sslCtx, zCaFile, zCaDirectory)==0 ){
      fossil_fatal("Cannot load CA root certificates from %s", zFile);
    }

/* Enable OpenSSL to use the Windows system ROOT certificate store to search for
** certificates missing in the file and directory trust stores already loaded by
** `SSL_CTX_load_verify_locations()'.
** This feature was introduced with OpenSSL 3.2.0, and may be enabled by default
** for future versions of OpenSSL, and explicit initialization may be redundant.
** NOTE TO HACKERS TWEAKING THEIR OPENSSL CONFIGURATION:
** The following OpenSSL configuration options must not be used for this feature
** to be available: `no-autoalginit', `no-winstore'. The Fossil makefiles do not
** currently set these options when building OpenSSL for Windows. */
#if defined(_WIN32)
#if OPENSSL_VERSION_NUMBER >= 0x030200000
    if( SSLeay()!=0x30500000  /* Don't use for 3.5.0 due to a bug */
     && SSL_CTX_load_verify_store(sslCtx, "org.openssl.winstore:")==0
    ){
      fossil_print("NOTICE: Failed to load the Windows root certificates.\n");
    }
#endif /* OPENSSL_VERSION_NUMBER >= 0x030200000 */
#endif /* _WIN32 */

    /* Load client SSL identity, preferring the filename specified on the
    ** command line */
    if( g.zSSLIdentity!=0 ){
      identityFile = g.zSSLIdentity;
    }else{
      identityFile = db_get("ssl-identity", 0);
    }
    if( identityFile!=0 && identityFile[0]!='\0' ){
      if( SSL_CTX_use_certificate_chain_file(sslCtx,identityFile)!=1
       || SSL_CTX_use_PrivateKey_file(sslCtx,identityFile,SSL_FILETYPE_PEM)!=1
      ){
        fossil_fatal("Could not load SSL identity from %s", identityFile);
      }
    }
    /* Register a callback to tell the user what to do when the server asks
    ** for a cert */
    SSL_CTX_set_client_cert_cb(sslCtx, ssl_client_cert_callback);

    sslIsInit = 1;
  }else{
    assert( sslIsInit==1 );
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
** Close the currently open client SSL connection.  If no connection is open,
** this routine is a no-op.
*/
void ssl_close_client(void){
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
  blob_appendf(&snd, "Host: %s:%d\r\n",
               pUrlData->hostname, pUrlData->proxyOrigPort);
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
      if( bbuf[end]=='\n' ) {
        if( (end+1<len && bbuf[end+1]=='\n')
         || (end+2<len && bbuf[end+1]=='\r' && bbuf[end+2]=='\n')
        ){
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
** Invoke this routine to disable SSL cert verification.  After
** this call is made, any SSL cert that the server provides will
** be accepted.  Communication will still be encrypted, but the
** client has no way of knowing whether it is talking to the
** real server or a man-in-the-middle imposter.
*/
void ssl_disable_cert_verification(void){
  sslNoCertVerify = 1;
}

/*
** Open an SSL connection as a client that is to connect to the server
** identified by pUrlData.
**
*  The identify of the server is determined as follows:
**
**    pUrlData->name  Name of the server.  Ex: fossil-scm.org
**    g.url.name      Name of the proxy server, if proxying.
**    pUrlData->port  TCP/IP port to use.  Ex: 80
**
** Return the number of errors.
*/
int ssl_open_client(UrlData *pUrlData){
  X509 *cert;
  const char *zRemoteHost;

  ssl_global_init_client();
  if( pUrlData->useProxy ){
    int rc;
    char *connStr = mprintf("%s:%d", g.url.name, pUrlData->port);
    BIO *sBio = BIO_new_connect(connStr);
    if( g.fIPv4 ){
#ifdef BIO_FAMILY_IPV4
      BIO_set_conn_ip_family(sBio, BIO_FAMILY_IPV4);
#else
      fossil_warning("The --ipv4 option is not supported in this build\n");
#endif
    }
    fossil_free(connStr);
    if( BIO_do_connect(sBio)<=0 ){
      ssl_set_errmsg("SSL: cannot connect to proxy %s:%d (%s)",
            pUrlData->name, pUrlData->port,
            ERR_reason_error_string(ERR_get_error()));
      ssl_close_client();
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
    zRemoteHost = pUrlData->hostname;
  }else{
    iBio = BIO_new_ssl_connect(sslCtx);
    zRemoteHost = pUrlData->name;
  }
  if( iBio==NULL ) {
    ssl_set_errmsg("SSL: cannot open SSL (%s)",
                    ERR_reason_error_string(ERR_get_error()));
    return 1;
  }
  BIO_get_ssl(iBio, &ssl);

#if (SSLEAY_VERSION_NUMBER >= 0x00908070) && !defined(OPENSSL_NO_TLSEXT)
  if( !SSL_set_tlsext_host_name(ssl, zRemoteHost)){
    fossil_warning("WARNING: failed to set server name indication (SNI), "
                  "continuing without it.\n");
  }
#endif

  SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
#if OPENSSL_VERSION_NUMBER >= 0x010002000
  if( !sslNoCertVerify ){
    X509_VERIFY_PARAM *param = 0;
    param = SSL_get0_param(ssl);
    if( !X509_VERIFY_PARAM_set1_host(param, zRemoteHost, strlen(zRemoteHost)) ){
      fossil_fatal("failed to set hostname.");
    }
    /* SSL_set_verify(ssl, SSL_VERIFY_PEER, 0); */
  }
#endif

  if( !pUrlData->useProxy ){
    char *connStr = mprintf("%s:%d", pUrlData->name, pUrlData->port);
    BIO_set_conn_hostname(iBio, connStr);
    fossil_free(connStr);
    if( g.fIPv4 ){
#ifdef BIO_FAMILY_IPV4
      BIO_set_conn_ip_family(iBio, BIO_FAMILY_IPV4);
#else
      fossil_warning("The --ipv4 option is not supported in this build\n");
#endif
    }
    if( BIO_do_connect(iBio)<=0 ){
      ssl_set_errmsg("SSL: cannot connect to host %s:%d (%s)",
         pUrlData->name, pUrlData->port,
         ERR_reason_error_string(ERR_get_error()));
      ssl_close_client();
      return 1;
    }
  }

  if( BIO_do_handshake(iBio)<=0 ) {
    ssl_set_errmsg("Error establishing SSL connection %s:%d (%s)",
        pUrlData->useProxy?pUrlData->hostname:pUrlData->name,
        pUrlData->useProxy?pUrlData->proxyOrigPort:pUrlData->port,
        ERR_reason_error_string(ERR_get_error()));
    ssl_close_client();
    return 1;
  }
  /* Check if certificate is valid */
  cert = SSL_get_peer_certificate(ssl);

  if ( cert==NULL ){
    ssl_set_errmsg("No SSL certificate was presented by the peer");
    ssl_close_client();
    return 1;
  }

  /* Debugging hint:   On unix-like system, run something like:
  **
  **     SSL_CERT_DIR=/tmp  ./fossil sync
  **
  ** to cause certificate validation to fail, and thus test the fallback
  ** logic.
  */
  if( !sslNoCertVerify && SSL_get_verify_result(ssl)!=X509_V_OK ){
    int x, desclen;
    char *desc, *prompt;
    Blob ans;
    char cReply;
    BIO *mem;
    unsigned char md[EVP_MAX_MD_SIZE];
    char zHash[EVP_MAX_MD_SIZE*2+1];
    unsigned int mdLength = (int)sizeof(md);

    memset(md, 0, sizeof(md));
    zHash[0] = 0;
                            /*  MMNNFFPPS */
#if OPENSSL_VERSION_NUMBER >= 0x010000000
    x = X509_digest(cert, EVP_sha256(), md, &mdLength);
#else
    x = X509_digest(cert, EVP_sha1(), md, &mdLength);
#endif
    if( x ){
      unsigned j;
      for(j=0; j<mdLength && j*2+1<sizeof(zHash); ++j){
        zHash[j*2] = "0123456789abcdef"[md[j]>>4];
        zHash[j*2+1] = "0123456789abcdef"[md[j]&0xf];
      }
      zHash[j*2] = 0;
    }

    if( ssl_certificate_exception_exists(pUrlData, zHash) ){
      /* Ignore the failure because an exception exists */
      ssl_one_time_exception(pUrlData, zHash);
    }else{
      /* Tell the user about the failure and ask what to do */
      mem = BIO_new(BIO_s_mem());
      BIO_puts(mem,     "  subject:   ");
      X509_NAME_print_ex(mem, X509_get_subject_name(cert), 0, XN_FLAG_ONELINE);
      BIO_puts(mem,   "\n  issuer:    ");
      X509_NAME_print_ex(mem, X509_get_issuer_name(cert), 0, XN_FLAG_ONELINE);
      BIO_printf(mem, "\n  notBefore: %s",
                 ssl_asn1time_to_iso8601(X509_get_notBefore(cert), 1));
      BIO_printf(mem, "\n  notAfter:  %s",
                 ssl_asn1time_to_iso8601(X509_get_notAfter(cert), 1));
      BIO_printf(mem, "\n  sha256:    %s", zHash);
      desclen = BIO_get_mem_data(mem, &desc);

      prompt = mprintf("Unable to verify SSL cert from %s\n%.*s\n"
          "accept this cert and continue (y/N/fingerprint)? ",
          pUrlData->name, desclen, desc);
      BIO_free(mem);

      prompt_user(prompt, &ans);
      free(prompt);
      cReply = blob_str(&ans)[0];
      if( cReply!='y' && cReply!='Y'
       && fossil_stricmp(blob_str(&ans),zHash)!=0
      ){
        X509_free(cert);
        ssl_set_errmsg("SSL cert declined");
        ssl_close_client();
        blob_reset(&ans);
        return 1;
      }
      blob_reset(&ans);
      ssl_one_time_exception(pUrlData, zHash);
      prompt_user("remember this exception (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      if( cReply=='y' || cReply=='Y') {
        db_open_config(0,0);
        ssl_remember_certificate_exception(pUrlData, zHash);
      }
      blob_reset(&ans);
    }
  }

  /* Set the Global.zIpAddr variable to the server we are talking to.
  ** This is used to populate the ipaddr column of the rcvfrom table,
  ** if any files are received from the server.
  */
  {
  /* As soon as libressl implements
  ** BIO_ADDR_hostname_string/BIO_get_conn_address.
  ** check here for the correct LIBRESSL_VERSION_NUMBER too. For now: disable
  */
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L \
      && !defined(LIBRESSL_VERSION_NUMBER)
    char *ip = BIO_ADDR_hostname_string(BIO_get_conn_address(iBio),1);
    g.zIpAddr = fossil_strdup(ip);
    OPENSSL_free(ip);
#else
    /* IPv4 only code */
    const unsigned char *ip;
    ip = (const unsigned char*)BIO_ptr_ctrl(iBio,BIO_C_GET_CONNECT,2);
    g.zIpAddr = mprintf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
#endif
  }

  X509_free(cert);
  return 0;
}

/*
** Remember that the cert with the given hash is acceptable for
** use with pUrlData->name.
*/
LOCAL void ssl_remember_certificate_exception(
  UrlData *pUrlData,
  const char *zHash
){
  db_set_mprintf(zHash, 1, "cert:%s", pUrlData->name);
}

/*
** Return true if the there exists a certificate exception for
** pUrlData->name that matches the hash.
*/
LOCAL int ssl_certificate_exception_exists(
  UrlData *pUrlData,
  const char *zHash
){
  char *zName, *zValue;
  if( fossil_strcmp(sException.zHost,pUrlData->name)==0
   && fossil_strcmp(sException.zHash,zHash)==0
  ){
    return 1;
  }
  zName = mprintf("cert:%s", pUrlData->name);
  zValue = db_get(zName,0);
  fossil_free(zName);
  return zValue!=0 && strcmp(zHash,zValue)==0;
}

/*
** Remember zHash as an acceptable certificate for this session only.
*/
LOCAL void ssl_one_time_exception(
  UrlData *pUrlData,
  const char *zHash
){
  fossil_free(sException.zHost);
  sException.zHost = fossil_strdup(pUrlData->name);
  fossil_free(sException.zHash);
  sException.zHash = fossil_strdup(zHash);
}

/*
** Send content out over the SSL connection from the client to
** the server.
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
** Receive content back from the client SSL connection.  In other
** words read the reply back from the server.
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

/*
** Initialize the SSL library so that it is able to handle
** server-side connections.  Invoke fossil_fatal() if there are
** any problems.
**
** If zKeyFile and zCertFile are not NULL, then they are the names
** of disk files that hold the certificate and private-key for the
** server.  If zCertFile is not NULL but zKeyFile is NULL, then
** zCertFile is assumed to be a concatenation of the certificate and
** the private-key in the PEM format.
**
** If zCertFile is "unsafe-builtin", then a built-in self-signed cert
** is used.  This built-in cert is insecure and should only be used for
** testing and debugging.
*/
void ssl_init_server(const char *zCertFile, const char *zKeyFile){
  if( sslIsInit==0 && zCertFile ){
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    sslCtx = SSL_CTX_new(SSLv23_server_method());
    if( sslCtx==0 ){
      ERR_print_errors_fp(stderr);
      fossil_fatal("Error initializing the SSL server");
    }
    if( fossil_strcmp(zCertFile,"unsafe-builtin")==0 ){
      if( sslctx_use_cert_from_mem(sslCtx, sslSelfCert, -1)
       || sslctx_use_pkey_from_mem(sslCtx, sslSelfPKey, -1)
      ){
        fossil_fatal("Error loading self-signed CERT and KEY");
      }
    }else{
      if( SSL_CTX_use_certificate_chain_file(sslCtx,zCertFile)!=1 ){
        ERR_print_errors_fp(stderr);
        fossil_fatal("Error loading CERT file \"%s\"", zCertFile);
      }
      if( zKeyFile==0 ) zKeyFile = zCertFile;
      if( SSL_CTX_use_PrivateKey_file(sslCtx, zKeyFile, SSL_FILETYPE_PEM)<=0 ){
        ERR_print_errors_fp(stderr);
        if( strcmp(zKeyFile,zCertFile)==0 ){
          fossil_fatal("The private key is not found in \"%s\". "
            "Either append the private key to the certification in that "
            "file or use a separate --pkey option to specify the private key.",
            zKeyFile);
        }else{
          fossil_fatal("Error loading the private key from file \"%s\"",
             zKeyFile);
        }
      }
    }
    if( !SSL_CTX_check_private_key(sslCtx) ){
      fossil_fatal("PRIVATE KEY \"%s\" does not match CERT \"%s\"",
           zKeyFile, zCertFile);
    }
    SSL_CTX_set_mode(sslCtx, SSL_MODE_AUTO_RETRY);
    sslIsInit = 2;
  }else{
    assert( sslIsInit==2 );
  }
}

typedef struct SslServerConn {
  SSL *ssl;          /* The SSL codec */
  int iSocket;       /* The socket */
  BIO *bio;          /* BIO object. Needed for EOF detection. */
} SslServerConn;

/*
** Create a new server-side codec.  The argument is the socket's file
** descriptor from which the codec reads and writes. The returned
** memory must eventually be passed to ssl_close_server().
*/
void *ssl_new_server(int iSocket){
  SslServerConn *pServer = fossil_malloc_zero(sizeof(*pServer));
  BIO *b = BIO_new_socket(iSocket, 0);
  pServer->ssl = SSL_new(sslCtx);
  pServer->iSocket = iSocket;
  pServer->bio = b;
  SSL_set_bio(pServer->ssl, b, b);
  SSL_accept(pServer->ssl);
  return (void*)pServer;
}

/*
** Close a server-side code previously returned from ssl_new_server().
*/
void ssl_close_server(void *pServerArg){
  SslServerConn *pServer = (SslServerConn*)pServerArg;
  SSL_free(pServer->ssl);
  fossil_free(pServer);
}

/*
** Return TRUE if there are no more bytes available to be read from
** the client.
*/
int ssl_eof(void *pServerArg){
  SslServerConn *pServer = (SslServerConn*)pServerArg;
  return BIO_eof(pServer->bio);
}

/*
** Read cleartext bytes that have been received from the client and
** decrypted by the SSL server codec.
**
** If the expected payload size unknown, i.e. if the HTTP
** Content-Length: header field has not been parsed, the doLoop
** argument should be 0, or SSL_read() may block and wait for more
** data than is eventually going to arrive (on Windows). On
** non-Windows builds, it has been our experience that the final
** argument must always be true, as discussed at length at:
**
** https://fossil-scm.org/forum/forumpost/2f818850abb72719
*/
size_t ssl_read_server(void *pServerArg, char *zBuf, size_t nBuf, int doLoop){
  int n;
  size_t rc = 0;
  SslServerConn *pServer = (SslServerConn*)pServerArg;
  if( nBuf>0x7fffffff ){ fossil_fatal("SSL read too big"); }
  while( nBuf!=rc && BIO_eof(pServer->bio)==0 ){
    n = SSL_read(pServer->ssl, zBuf + rc, (int)(nBuf - rc));
    if( n>0 ){
      rc += n;
    }
    if( doLoop==0 || n<=0 ){
      break;
    }
  }
  return rc;
}

/*
** Read a single line of text from the client, up to nBuf-1 bytes. On
** success, writes nBuf-1 bytes to zBuf and NUL-terminates zBuf.
** Returns NULL on an I/O error or at EOF.
*/
char *ssl_gets(void *pServerArg, char *zBuf, int nBuf){
  int n = 0;
  int i;
  SslServerConn *pServer = (SslServerConn*)pServerArg;

  if( BIO_eof(pServer->bio) ) return 0;
  for(i=0; i<nBuf-1; i++){
    n = SSL_read(pServer->ssl, &zBuf[i], 1);
    if( n<=0 ){
      return 0;
    }
    if( zBuf[i]=='\n' ) break;
  }
  zBuf[i+1] = 0;
  return zBuf;
}


/*
** Write cleartext bytes into the SSL server codec so that they can
** be encrypted and sent back to the client.
*/
size_t ssl_write_server(void *pServerArg, char *zBuf, size_t nBuf){
  int n;
  SslServerConn *pServer = (SslServerConn*)pServerArg;
  if( nBuf<=0 ) return 0;
  if( nBuf>0x7fffffff ){ fossil_fatal("SSL write too big"); }
  n = SSL_write(pServer->ssl, zBuf, (int)nBuf);
  if( n<=0 ){
    return -SSL_get_error(pServer->ssl, n);
  }else{
    return n;
  }
}

#endif /* FOSSIL_ENABLE_SSL */

#ifdef FOSSIL_ENABLE_SSL
/*
** zPath is a name that might be a file or directory containing a trust
** store.  *pzStore is the name of the trust store to actually use.
**
** If *pzStore is not NULL (meaning no trust store has been found yet)
** and if zPath exists, then set *pzStore to point to zPath.
*/
static void trust_location_usable(const char *zPath, const char **pzStore){
  if( *pzStore!=0 ) return;
  if( file_isdir(zPath, ExtFILE)>0 ) *pzStore = zPath;
}
#endif /* FOSSIL_ENABLE_SSL */

/*
** COMMAND: tls-config*                       abbrv-subcom
** COMMAND: ssl-config                        abbrv-subcom
**
** Usage: %fossil ssl-config [SUBCOMMAND] [OPTIONS...] [ARGS...]
**
** This command is used to view or modify the TLS (Transport Layer
** Security) configuration for Fossil.  TLS (formerly SSL) is the
** encryption technology used for secure HTTPS transport.
**
** Sub-commands:
**
**    remove-exception DOMAINS    Remove TLS cert exceptions for the domains
**                                listed.  Or remove them all if the --all
**                                option is specified.
**
**    scrub ?--force?             Remove all SSL configuration data from the
**                                repository. Use --force to omit the
**                                confirmation.
**
**    show ?-v?                   Show the TLS configuration. Add -v to see
**                                additional explanation
*/
void test_tlsconfig_info(void){
  const char *zCmd;
  size_t nCmd;
  int nHit = 0;

  db_find_and_open_repository(OPEN_OK_NOT_FOUND|OPEN_SUBSTITUTE,0);
  db_open_config(1,0);
  if( g.argc==2 || (g.argc>=3 && g.argv[2][0]=='-') ){
    zCmd = "show";
    nCmd = 4;
  }else{
    zCmd = g.argv[2];
    nCmd = strlen(zCmd);
  }
  if( strncmp("scrub",zCmd,nCmd)==0 && nCmd>4 ){
    int bForce = find_option("force","f",0)!=0;
    verify_all_options();
    if( !bForce ){
      Blob ans;
      char cReply;
      prompt_user(
        "Scrubbing the SSL configuration will permanently delete information.\n"
        "Changes cannot be undone.  Continue (y/N)? ", &ans);
      cReply = blob_str(&ans)[0];
      if( cReply!='y' && cReply!='Y' ){
        fossil_exit(1);
      }
    }
    db_unprotect(PROTECT_ALL);
    db_multi_exec(
      "PRAGMA secure_delete=ON;"
      "DELETE FROM config WHERE name GLOB 'ssl-*';"
    );
    db_protect_pop();
  }else
  if( strncmp("show",zCmd,nCmd)==0 ){
#if defined(FOSSIL_ENABLE_SSL)
    const char *zName, *zValue;
    const char *zUsed = 0;       /* Trust store location actually used */
    size_t nName;
#endif
    Stmt q;
    int verbose = find_option("verbose","v",0)!=0;
    verify_all_options();

#if !defined(FOSSIL_ENABLE_SSL)
    fossil_print("OpenSSL-version:      (none)\n");
    if( verbose ){
      fossil_print("\n"
         "  The OpenSSL library is not used by this build of Fossil\n\n"
      );
    }
#else
    fossil_print("OpenSSL-version:      %s  (0x%09llx)\n",
         SSLeay_version(SSLEAY_VERSION), (unsigned long long)SSLeay());
    if( verbose ){
      fossil_print("\n"
         "  The version of the OpenSSL library being used\n"
         "  by this instance of Fossil.  Version 3.0.0 or\n"
         "  later is recommended.\n\n"
      );
    }

    fossil_print("Trust store location\n");
    zName = X509_get_default_cert_file_env();
    zValue = fossil_getenv(zName);
    if( zValue==0 ) zValue = "";
    trust_location_usable(zValue, &zUsed);
    nName = strlen(zName);
    fossil_print("  %s:%*s%s\n", zName, 19-nName, "", zValue);
    zName = X509_get_default_cert_dir_env();
    zValue = fossil_getenv(zName);
    if( zValue==0 ) zValue = "";
    trust_location_usable(zValue, &zUsed);
    nName = strlen(zName);
    fossil_print("  %s:%*s%s\n", zName, 19-nName, "", zValue);
    if( verbose ){
      fossil_print("\n"
        "    Environment variables that determine alternative locations for\n"
        "    the root certificates used by Fossil when it is acting as a SSL\n"
        "    client. If specified, these alternative locations take top\n"
        "    priority.\n\n"
      );
    }

    zValue = db_get("ssl-ca-location","");
    trust_location_usable(zValue, &zUsed);
    fossil_print("  ssl-ca-location:    %s\n", zValue);
    if( verbose ){
      fossil_print("\n"
         "    This setting is the name of a file or directory that contains\n"
         "    the complete set of root certificates used by Fossil when it\n"
         "    is acting as a SSL client. If defined, this setting takes\n"
         "    priority over built-in paths.\n\n"
      );
    }


    zValue = X509_get_default_cert_file();
    trust_location_usable(zValue, &zUsed);
    fossil_print("  OpenSSL-cert-file:  %s\n", zValue);
    zValue = X509_get_default_cert_dir();
    trust_location_usable(zValue, &zUsed);
    fossil_print("  OpenSSL-cert-dir:   %s\n", X509_get_default_cert_dir());
    if( verbose ){
      fossil_print("\n"
         "    The default locations for the set of root certificates\n"
         "    used by the \"fossil sync\" and similar commands to verify\n"
         "    the identity of servers for \"https:\" URLs. These values\n"
         "    come into play when Fossil is used as a TLS client.  These\n"
         "    values are built into your OpenSSL library.\n\n"
      );
    }

#if defined(_WIN32)
    fossil_print("  OpenSSL-winstore:   %s\n",
         (SSLeay()>=0x30200000 && SSLeay()!=0x30500000) ? "Yes" : "No");
    if( verbose ){
      fossil_print("\n"
         "    OpenSSL 3.2.0, or newer, but not version 3.5.0 due to a bug,\n"
         "    are able to use the root certificates managed by the Windows\n"
         "    operating system. The installed root certificates are listed\n"
         "    by the command:\n\n"
         "        certutil -store \"ROOT\"\n\n"
      );
    }
#endif /* _WIN32 */

    if( zUsed==0 ) zUsed = "";
    fossil_print("  Trust store used:   %s\n", zUsed);
    if( verbose ){
      fossil_print("\n"
         "    The location that is actually used for the root certificates\n"
         "    used to verify the identity of servers for \"https:\" URLs.\n"
         "    This will be one of the first of the five locations listed\n"
         "    above that actually exists.\n\n"
      );
    }


#endif /* FOSSIL_ENABLE_SSL */


    fossil_print("ssl-identity:        %s\n", db_get("ssl-identity",""));
    if( verbose ){
      fossil_print("\n"
        "  This setting is the name of a file that contains the PEM-format\n"
        "  certificate and private-key used by Fossil clients to authenticate\n"
        "  with servers. Few servers actually require this, so this setting\n"
        "  is usually blank.\n\n"
      );
    }

    db_prepare(&q,
       "SELECT name, '', value FROM global_config"
       " WHERE name GLOB 'cert:*'"
       "UNION ALL "
       "SELECT name, date(mtime,'unixepoch'), value FROM config"
       " WHERE name GLOB 'cert:*'"
       " ORDER BY name"
    );
    nHit = 0;
    while( db_step(&q)==SQLITE_ROW ){
                /*  123456789 123456789 123456789 */
      if( verbose ){
        fossil_print("exception:            %-40s %s\n"
                     "     hash:            %.57s\n",
             db_column_text(&q,0)+5, db_column_text(&q,1),
             db_column_text(&q,2));
      }else{
        fossil_print("exception:            %-40s %s\n",
             db_column_text(&q,0)+5, db_column_text(&q,1));
      }
      nHit++;
    }
    db_finalize(&q);
    if( nHit && verbose ){
      fossil_print("\n"
         "  The exceptions are server certificates that the Fossil client\n"
         "  is unable to verify using root certificates, but which should be\n"
         "  accepted anyhow.\n\n"
      );
    }

  }else
  if( strncmp("remove-exception",zCmd,nCmd)==0 ){
    int i;
    Blob sql;
    char *zSep = "(";
    db_begin_transaction();
    blob_init(&sql, 0, 0);
    if( g.argc==4 && find_option("all",0,0)!=0 ){
      blob_append_sql(&sql,
        "DELETE FROM global_config WHERE name GLOB 'cert:*';\n"
        "DELETE FROM global_config WHERE name GLOB 'trusted:*';\n"
        "DELETE FROM config WHERE name GLOB 'cert:*';\n"
        "DELETE FROM config WHERE name GLOB 'trusted:*';\n"
      );
    }else{
      if( g.argc<4 ){
        usage("remove-exception DOMAIN-NAME ...");
      }
      blob_append_sql(&sql,"DELETE FROM global_config WHERE name IN ");
      for(i=3; i<g.argc; i++){
        blob_append_sql(&sql,"%s'cert:%q','trust:%q'",
           zSep/*safe-for-%s*/, g.argv[i], g.argv[i]);
        zSep = ",";
      }
      blob_append_sql(&sql,");\n");
      zSep = "(";
      blob_append_sql(&sql,"DELETE FROM config WHERE name IN ");
      for(i=3; i<g.argc; i++){
        blob_append_sql(&sql,"%s'cert:%q','trusted:%q'",
           zSep/*safe-for-%s*/, g.argv[i], g.argv[i]);
        zSep = ",";
      }
      blob_append_sql(&sql,");");
    }
    db_unprotect(PROTECT_CONFIG);
    db_exec_sql(blob_str(&sql));
    db_protect_pop();
    db_commit_transaction();
    blob_reset(&sql);
  }else
  /*default*/{
    fossil_fatal("unknown sub-command \"%s\".\nshould be one of:"
                 " remove-exception scrub show",
       zCmd);
  }
}

/*
** WEBPAGE: .well-known
**
** If the "--acme" option was supplied to "fossil server" or "fossil http" or
** similar, then this page returns the content of files found in the
** ".well-known" subdirectory of the same directory that contains the
** repository file.  This facilitates Automated Certificate
** Management using tools like "certbot".
**
** The content is returned directly, without any interpretation, using
** a generic mimetype.
*/
void wellknown_page(void){
  char *zPath = 0;
  const char *zTail = P("name");
  Blob content;
  int i;
  char c;
  if( !g.fAllowACME ) goto wellknown_notfound;
  if( g.zRepositoryName==0 ) goto wellknown_notfound;
  if( zTail==0 ) goto wellknown_notfound;
  zPath = mprintf("%z/.well-known/%s", file_dirname(g.zRepositoryName), zTail);
  for(i=0; (c = zTail[i])!=0; i++){
    if( fossil_isalnum(c) ) continue;
    if( c=='.' ){
      if( i==0 || zTail[i-1]=='/' || zTail[i-1]=='.' ) goto wellknown_notfound;
      continue;
    }
    if( c==',' || c!='-' || c=='/' || c==':' || c=='_' || c=='~' ) continue;
    goto wellknown_notfound;
  }
  if( strstr("/..", zPath)!=0 ) goto wellknown_notfound;
  if( !file_isfile(zPath, ExtFILE) ) goto wellknown_notfound;
  blob_read_from_file(&content, zPath, ExtFILE);
  cgi_set_content(&content);
  cgi_set_content_type(mimetype_from_name(zPath));
  cgi_reply();
  return;

wellknown_notfound:
  fossil_free(zPath);
  webpage_notfound_error(0 /*works-like:""*/);
  return;
}

/*
** Return the OpenSSL version number being used.  Space to hold
** this name is obtained from fossil_malloc() and should be
** freed by the caller.
*/
char *fossil_openssl_version(void){
#if defined(FOSSIL_ENABLE_SSL)
  return mprintf("%s (0x%09x)\n",
         SSLeay_version(SSLEAY_VERSION), (sqlite3_uint64)SSLeay());
#else
  return mprintf("none");
#endif
}
