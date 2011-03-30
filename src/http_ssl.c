/*
** Copyright (c) 2009 D. Richard Hipp
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
static BIO *iBio;            /* OpenSSL I/O abstraction */
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
void ssl_set_errmsg(char *zFormat, ...){
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
** Call this routine once before any other use of the SSL interface.
** This routine does initial configuration of the SSL module.
*/
void ssl_global_init(void){
  if( sslIsInit==0 ){
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();    
    sslCtx = SSL_CTX_new(SSLv23_client_method());
    X509_STORE_set_default_paths(SSL_CTX_get_cert_store(sslCtx));
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
  }
}

/*
** Open an SSL connection.  The identify of the server is determined
** by global varibles that are set using url_parse():
**
**    g.urlName       Name of the server.  Ex: www.fossil-scm.org
**    g.urlPort       TCP/IP port to use.  Ex: 80
**
** Return the number of errors.
*/
int ssl_open(void){
  X509 *cert;
  int hasSavedCertificate = 0;
  char *connStr;
  ssl_global_init();

  /* If client certificate/key has been set, load them into the SSL context. */
  ssl_load_client_authfiles();

  /* Get certificate for current server from global config and
  ** (if we have it in config) add it to certificate store.
  */
  cert = ssl_get_certificate();
  if ( cert!=NULL ){
    X509_STORE_add_cert(SSL_CTX_get_cert_store(sslCtx), cert);
    X509_free(cert);
    hasSavedCertificate = 1;
  }

  iBio = BIO_new_ssl_connect(sslCtx);
  BIO_get_ssl(iBio, &ssl);
  SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
  if( iBio==NULL ){
    ssl_set_errmsg("SSL: cannot open SSL (%s)", 
                    ERR_reason_error_string(ERR_get_error()));
    return 1;
  }
  
  connStr = mprintf("%s:%d", g.urlName, g.urlPort);
  BIO_set_conn_hostname(iBio, connStr);
  free(connStr);
  
  if( BIO_do_connect(iBio)<=0 ){
    ssl_set_errmsg("SSL: cannot connect to host %s:%d (%s)", 
        g.urlName, g.urlPort, ERR_reason_error_string(ERR_get_error()));
    ssl_close();
    return 1;
  }
  
  if( BIO_do_handshake(iBio)<=0 ) {
    ssl_set_errmsg("Error establishing SSL connection %s:%d (%s)", 
        g.urlName, g.urlPort, ERR_reason_error_string(ERR_get_error()));
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

  if( SSL_get_verify_result(ssl) != X509_V_OK ){
    char *desc, *prompt;
    char *warning = "";
    Blob ans;
    BIO *mem;
    
    mem = BIO_new(BIO_s_mem());
    X509_NAME_print_ex(mem, X509_get_subject_name(cert), 2, XN_FLAG_MULTILINE);
    BIO_puts(mem, "\n\nIssued By:\n\n");
    X509_NAME_print_ex(mem, X509_get_issuer_name(cert), 2, XN_FLAG_MULTILINE);
    BIO_write(mem, "", 1); // null-terminate mem buffer
    BIO_get_mem_data(mem, &desc);
    
    if( hasSavedCertificate ){
      warning = "WARNING: Certificate doesn't match the "
                "saved certificate for this host!";
    }
    prompt = mprintf("\nUnknown SSL certificate:\n\n%s\n\n%s\n"
                     "Accept certificate [a=always/y/N]? ", desc, warning);
    BIO_free(mem);

    prompt_user(prompt, &ans);
    free(prompt);
    if( blob_str(&ans)[0]!='y' && blob_str(&ans)[0]!='a' ) {
      X509_free(cert);
      ssl_set_errmsg("SSL certificate declined");
      ssl_close();
      return 1;
    }
    if( blob_str(&ans)[0]=='a' ) {
      ssl_save_certificate(cert);
    }
    blob_reset(&ans);
  }
  X509_free(cert);
  return 0;
}

/*
** Save certificate to global config.
*/
void ssl_save_certificate(X509 *cert){
  BIO *mem;
  char *zCert, *zHost;

  mem = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(mem, cert);
  BIO_write(mem, "", 1); // null-terminate mem buffer
  BIO_get_mem_data(mem, &zCert);
  zHost = mprintf("cert:%s", g.urlName);
  db_set(zHost, zCert, 1);
  free(zHost);
  BIO_free(mem);  
}

/*
** Get certificate for g.urlName from global config.
** Return NULL if no certificate found.
*/
X509 *ssl_get_certificate(void){
  char *zHost, *zCert;
  BIO *mem;
  X509 *cert;

  zHost = mprintf("cert:%s", g.urlName);
  zCert = db_get(zHost, NULL);
  free(zHost);
  if ( zCert==NULL )
    return NULL;
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
  size_t sent;
  size_t total = 0;
  while( N>0 ){
    sent = BIO_write(iBio, pContent, N);
    if( sent<=0 ) break;
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
  size_t got;
  size_t total = 0;
  while( N>0 ){
    got = BIO_read(iBio, pContent, N);
    if( got<=0 ) break;
    total += got;
    N -= got;
    pContent = (void*)&((char*)pContent)[got];
  }
  return total;
}

/*
** Read client certificate and key, if set, and store them in the SSL context
** to allow communication with servers which are configured to verify client
** certificates and certificate chains.
** We only support PEM and don't support password protected keys.
**
** Always try the environment variables first, and if they aren't set, then
** use the global config.
*/
void ssl_load_client_authfiles(void){
  char *cafile;
  char *capath;
  char *certfile;
  char *keyfile;

  cafile = ssl_get_and_set_file_ref("FOSSIL_CAFILE", "cafile");
  capath = ssl_get_and_set_file_ref("FOSSIL_CAPATH", "capath");

  if( cafile || capath ){
    /* The OpenSSL documentation warns that if several CA certificates match
    ** the same name, key identifier and serial number conditions, only the
    ** first will be examined. The caveat situation is when one stores an
    ** expired CA certificate among the valid ones.
    ** Simply put: Do not mix expired and valid certificates.
    */
    if( SSL_CTX_load_verify_locations(sslCtx, cafile, capath) == 0){
      fossil_fatal("SSL: Unable to load CA verification file/path");
    }
  }else{
    fossil_warning("SSL: CA file/path missing for certificate verification.");
  }

  certfile = ssl_get_and_set_file_ref("FOSSIL_CCERT", "ccert");
  if( !certfile ){
     free(capath);
     free(cafile);
     return;
  }

  keyfile = ssl_get_and_set_file_ref("FOSSIL_CKEY", "ckey");

  /* Assume the key is in the certificate file if key file was not specified */
  if( certfile && !keyfile ){
    keyfile = certfile;
  }

  if( SSL_CTX_use_certificate_file(sslCtx, certfile, SSL_FILETYPE_PEM) <= 0 ){
    fossil_fatal("SSL: Unable to open client certificate in %s.", certfile);
  }
  if( SSL_CTX_use_PrivateKey_file(sslCtx, keyfile, SSL_FILETYPE_PEM) <= 0 ){
    fossil_fatal("SSL: Unable to open client key in %s.", keyfile);
  }

  if( !SSL_CTX_check_private_key(sslCtx) ){
    fossil_fatal("SSL: Private key does not match the certificate public "
        "key.");
  }

  free(keyfile);
  free(certfile);
  free(capath);
  free(cafile);
}

/*
** Get SSL authentication file reference from environment variable. If set,
** then store varaible in global config. If environment variable was not set,
** attempt to get variable from global config.
**/
char *ssl_get_and_set_file_ref(const char *envvar, const char *dbvar){
  char *zVar;
  char *zTmp;

  zTmp = mprintf("%s:%s", dbvar, g.urlName);

  zVar = getenv(envvar);
  if( zVar ){
    zVar = strdup(zVar);
    if( zVar == NULL ){
      fossil_fatal("Unable to allocate memory for %s value.", envvar);
    }
    db_set(zTmp, zVar, 1);
  }else{
    zVar = db_get(zTmp, NULL);
  }
  free(zTmp);

  return zVar;
}

/*
** COMMAND: cert
**
** Usage: %fossil cert SUBCOMMAND ...
**
** Manage/group PKI keys/certificates to be able to use client
** certificates and register CA certificates for SSL verifications.
**
**    %fossil cert add NAME ?--key KEYFILE? ?--cert CERTFILE?
**           ?--cafile CAFILE? ?--capath CAPATH?
**
**        Create a certificate group NAME with the associated
**        certificates/keys. If a client certificate is specified but no
**        key, it is assumed that the key is located in the client
**        certificate file. The file format must be PEM.
**
**    %fossil cert list
**
**        List all credential groups, their values and their URL
**        associations.
**
**    %fossil cert disassociate URL
**
**        Disassociate URL from any credential group(s).
**
**    %fossil cert delete NAME
**
**        Remove the credential group NAME and all it's associated URL
**        associations.
*/
void cert_cmd(void){
  int n;
  const char *zCmd = "list";
  if( g.argc>=3 ){
    zCmd = g.argv[2];
  }
  n = strlen(zCmd);
  if( strncmp(zCmd, "add", n)==0 ){
    const char *zContainer;
    const char *zCKey;
    const char *zCCert;
    const char *zCAFile;
    const char *zCAPath;
    if( g.argc<5 ){
      usage("add NAME ?--key CLIENTKEY? ?--cert CLIENTCERT? ?--cafile CAFILE? "
          "?--capath CAPATH?");
    }
    zContainer = g.argv[3];
    zCKey = find_option("key",0,1);
    zCCert = find_option("cert",0,1);
    zCAFile = find_option("cafile",0,1);
    zCAPath = find_option("capath",0,1);

    /* If a client certificate was specified, but a key was not, assume the
     * key is stored in the same file as the certificate.
     */
    if( !zCKey && zCCert ){
      zCKey = zCCert;
    }

    db_open_config(0);
    db_swap_connections();
    if( db_exists(
        "SELECT 1 FROM certs"
        " WHERE name='%s'",
        zContainer)!=0 ){
      fossil_fatal("certificate group \"%s\" already exists", zContainer);
    }
    db_begin_transaction();
    if( zCKey ){
      db_multi_exec("INSERT INTO certs (name,type,filepath) "
          "VALUES(%Q,'ckey',%Q)",
          zContainer, zCKey);
    }
    if( zCCert ){
      db_multi_exec("INSERT INTO certs (name,type,filepath) "
          "VALUES(%Q,'ccert',%Q)",
          zContainer, zCCert);
    }
    if( zCAFile ){
      db_multi_exec("INSERT INTO certs (name,type,filepath) "
          "VALUES(%Q,'cafile',%Q)",
          zContainer, zCAFile);
    }
    if( zCAPath ){
      db_multi_exec("INSERT INTO certs (name,type,filepath) "
          "VALUES(%Q,'capath',%Q)",
          zContainer, zCAPath);
    }
    db_end_transaction(0);
    db_swap_connections();
  }else if(strncmp(zCmd, "list", n)==0){
    Stmt q;
    char *grp = NULL;

    db_open_config(0);
    db_swap_connections();

    db_prepare(&q, "SELECT name,type,filepath FROM certs"
                   " WHERE type NOT IN ('server')"
                   " ORDER BY name,type");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zCont = db_column_text(&q, 0);
      const char *zType = db_column_text(&q, 1);
      const char *zFilePath = db_column_text(&q, 2);
      if( fossil_strcmp(zCont, grp)!=0 ){
        free(grp);
        grp = strdup(zCont);
        puts(zCont);
      }
      printf("\t%s=%s\n", zType, zFilePath);
    }
    db_finalize(&q);

    /* List the URL associations. */
    db_prepare(&q, "SELECT name FROM global_config"
                   " WHERE name LIKE 'certgroup:%%' AND value=%Q"
                   " ORDER BY name", grp);
    free(grp);

    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      static int first = 1;
      if( first ) {
        puts("\tAssociations");
        first = 0;
      }
      printf("\t\t%s\n", zName+10);
    }

    db_swap_connections();
  }else if(strncmp(zCmd, "disassociate", n)==0){
    const char *zURL;
    if( g.argc<4 ){
      usage("disassociate URL");
    }
    zURL = g.argv[3];

    db_open_config(0);
    db_swap_connections();
    db_begin_transaction();

    db_multi_exec("DELETE FROM global_config WHERE name='certgroup:%s'",
        zURL);
    if( db_changes() == 0 ){
      fossil_warning("No certificate group associated with URL \"%s\".",
          zURL);
    }else{
      printf("%s disassociated from its certificate group.\n", zURL);
    }
    db_end_transaction(0);
    db_swap_connections();

  }else if(strncmp(zCmd, "delete", n)==0){
    const char *zContainer;
    if( g.argc<4 ){
      usage("delete NAME");
    }
    zContainer = g.argv[3];

    db_open_config(0);
    db_swap_connections();
    db_begin_transaction();
    db_multi_exec("DELETE FROM certs WHERE name=%Q", zContainer);
    if( db_changes() == 0 ){
      fossil_warning("No certificate group named \"%s\" found",
          zContainer);
    }else{
      printf("%d entries removed\n", db_changes());
    }
    db_multi_exec("DELETE FROM global_config WHERE name LIKE 'certgroup:%%'"
        " AND value=%Q", zContainer);
    if( db_changes() > 0 ){
      printf("%d associations removed\n", db_changes());
    }
    db_end_transaction(0);
    db_swap_connections();
  }else{
    fossil_panic("cert subcommand should be one of: "
                 "add list disassociate delete");
  }
}

#endif /* FOSSIL_ENABLE_SSL */
