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
** Implementation of SMTP (Simple Mail Transport Protocol) according
** to RFC 5321.
*/
#include "config.h"
#include "smtp.h"
#include <assert.h>
#if (HAVE_DN_EXPAND || HAVE___NS_NAME_UNCOMPRESS || HAVE_NS_NAME_UNCOMPRESS) \
     && (HAVE_NS_PARSERR || HAVE___NS_PARSERR) && !defined(FOSSIL_OMIT_DNS)
#  include <sys/types.h>
#  include <netinet/in.h>
#  if defined(HAVE_BIND_RESOLV_H)
#    include <bind/resolv.h>
#    include <bind/arpa/nameser_compat.h>
#  else
#    include <arpa/nameser.h>
#    include <resolv.h>
#  endif
#  if defined(HAVENS_NAME_UNCOMPRESS) && !defined(dn_expand)
#    define dn_expand ns_name_uncompress
#  endif
#  if defined(HAVE__NS_NAME_UNCOMPRESS) && !defined(dn_expand)
#    define dn_expand __ns_name_uncompress
#  endif
#  define FOSSIL_UNIX_STYLE_DNS 1
#endif
#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
#  include <windows.h>
#  include <windns.h>
#  define FOSSIL_WINDOWS_STYLE_DNS 1
#endif


/*
** Find the hostname for receiving email for the domain given
** in zDomain.  Return NULL if not found or not implemented.
** If multiple email receivers are advertized, pick the one with
** the lowest preference number.
**
** The returned string is obtained from fossil_malloc()
** and should be released using fossil_free().
*/
char *smtp_mx_host(const char *zDomain){
#if defined(FOSSIL_UNIX_STYLE_DNS)
  int nDns;                       /* Length of the DNS reply */
  int rc;                         /* Return code from various APIs */
  int i;                          /* Loop counter */
  int iBestPriority = 9999999;    /* Best priority */
  int nRec;                       /* Number of answers */
  ns_msg h;                       /* DNS reply parser */
  const unsigned char *pBest = 0; /* RDATA for the best answer */
  unsigned char aDns[5000];       /* Raw DNS reply content */
  char zHostname[5000];           /* Hostname for the MX */

  nDns = res_query(zDomain, C_IN, T_MX, aDns, sizeof(aDns));
  if( nDns<=0 ) return 0;
  res_init();
  rc = ns_initparse(aDns,nDns,&h);
  if( rc ) return 0;
  nRec = ns_msg_count(h, ns_s_an);
  for(i=0; i<nRec; i++){
    ns_rr x;
    int priority, sz;
    const unsigned char *p;
    rc = ns_parserr(&h, ns_s_an, i, &x);
    if( rc ) continue;
    p = ns_rr_rdata(x);
    sz = ns_rr_rdlen(x);
    if( sz>2 ){
      priority = p[0]*256 + p[1];
      if( priority<iBestPriority ){
        pBest = p;
        iBestPriority = priority;
      }
    }
  }
  if( pBest ){
    dn_expand(aDns, aDns+nDns, pBest+2, zHostname, sizeof(zHostname));
    return fossil_strdup(zHostname);
  }
  return 0;
#elif defined(FOSSIL_WINDOWS_STYLE_DNS)
  DNS_STATUS status;           /* Return status */
  PDNS_RECORDA pDnsRecord, p;  /* Pointer to DNS_RECORD structure */
  int iBestPriority = 9999999; /* Best priority */
  char *pBest = 0;             /* RDATA for the best answer */

  status = DnsQuery_UTF8(zDomain,            /* Domain name */
                         DNS_TYPE_MX,        /* DNS record type */
                         DNS_QUERY_STANDARD, /* Query options */
                         NULL,               /* List of DNS servers */
                         &pDnsRecord,        /* Query results */
                         NULL);              /* Reserved */
  if( status ) return NULL;

  p = pDnsRecord;
  while( p ){
    if( p->Data.MX.wPreference<iBestPriority ){
      iBestPriority = p->Data.MX.wPreference;
      pBest = p->Data.MX.pNameExchange;
    }
    p = p->pNext;
  }
  if( pBest ){
    pBest = fossil_strdup(pBest);
  }
  DnsRecordListFree(pDnsRecord, DnsFreeRecordListDeep);
  return pBest;
#else
  return 0;
#endif /* defined(FOSSIL_WINDOWS_STYLE_DNS) */
}

/*
** COMMAND: test-find-mx
**
** Usage: %fossil test-find-mx DOMAIN ...
**
** Do a DNS MX lookup to find the hostname for sending email for
** DOMAIN.
*/
void test_find_mx(void){
  int i;
  if( g.argc<=2 ){
    usage("DOMAIN ...");
  }
  for(i=2; i<g.argc; i++){
    char *z = smtp_mx_host(g.argv[i]);
    fossil_print("%s: %s\n", g.argv[i], z);
    fossil_free(z);
  }
}

#if INTERFACE
/*
** Information about a single SMTP connection.
*/
struct SmtpSession {
  const char *zFrom;        /* Domain from which we are sending */
  const char *zDest;        /* Domain that will receive the email */
  char *zHostname;          /* Hostname of SMTP server for zDest */
  u32 smtpFlags;            /* Flags changing the operation */
  FILE *logFile;            /* Write session transcript to this log file */
  Blob *pTranscript;        /* Record session transcript here */
  int bOpen;                /* True if connection is Open */
  int bFatal;               /* Error is fatal.  Do not retry */
  char *zErr;               /* Error message */
  Blob inbuf;               /* Input buffer */
  UrlData url;              /* Address of the server */
};

/* Allowed values for SmtpSession.smtpFlags */
#define SMTP_TRACE_STDOUT   0x00001     /* Debugging info to console */
#define SMTP_TRACE_FILE     0x00002     /* Debugging info to logFile */
#define SMTP_TRACE_BLOB     0x00004     /* Record transcript */
#define SMTP_DIRECT         0x00008     /* Skip the MX lookup */
#define SMTP_PORT           0x00010     /* Use an alternate port number */

#endif

/*
** Shutdown an SmtpSession
*/
void smtp_session_free(SmtpSession *pSession){
  socket_close();
  blob_reset(&pSession->inbuf);
  fossil_free(pSession->zHostname);
  fossil_free(pSession->zErr);
  fossil_free(pSession);
}

/*
** Set an error message on the SmtpSession
*/
static void smtp_set_error(
  SmtpSession *p,             /* The SMTP context */
  int bFatal,                 /* Fatal error.  Reset and retry is pointless */
  const char *zFormat,        /* Error message. */
  ...
){
  if( bFatal ) p->bFatal = 1;
  if( p->zErr==0 ){
    va_list ap;
    va_start(ap, zFormat);
    p->zErr = vmprintf(zFormat, ap);
    va_end(ap);
  }
  if( p->bOpen ){
    socket_close();
    p->bOpen = 0;
  }
}

/*
** Allocate a new SmtpSession object.
**
** Both zFrom and zDest must be specified.  smtpFlags may not contain
** either SMTP_TRACE_FILE or SMTP_TRACE_BLOB as those settings must be
** added by a subsequent call to smtp_session_config().
**
** The iPort option is ignored unless SMTP_PORT is set in smtpFlags
*/
SmtpSession *smtp_session_new(
  const char *zFrom,    /* Domain for the client */
  const char *zDest,    /* Domain of the server */
  u32 smtpFlags,        /* Flags */
  int iPort             /* TCP port if the SMTP_PORT flags is present */
){
  SmtpSession *p;

  p = fossil_malloc( sizeof(*p) );
  memset(p, 0, sizeof(*p));
  p->zFrom = zFrom;
  p->zDest = zDest;
  p->smtpFlags = smtpFlags;
  p->url.port = 25;
  blob_init(&p->inbuf, 0, 0);
  if( smtpFlags & SMTP_PORT ){
    p->url.port = iPort;
  }
  if( (smtpFlags & SMTP_DIRECT)!=0 ){
    int i;
    p->zHostname = fossil_strdup(zDest);
    for(i=0; p->zHostname[i] && p->zHostname[i]!=':'; i++){}
    if( p->zHostname[i]==':' ){
      p->zHostname[i] = 0;
      p->url.port = atoi(&p->zHostname[i+1]);
    }
  }else{
    p->zHostname = smtp_mx_host(zDest);
  }
  if( p->zHostname==0 ){
    smtp_set_error(p, 1, "cannot locate SMTP server for \"%s\"", zDest);
    return p;
  }
  p->url.name = p->zHostname;
  socket_global_init();
  p->bOpen = 0;
  return p;
}

/*
** Configure debugging options on SmtpSession.  Add all bits in
** smtpFlags to the settings.  The following bits can be added:
**
**    SMTP_FLAG_FILE:     In which case pArg is the FILE* pointer to use
**
**    SMTP_FLAG_BLOB:     In which case pArg is the Blob* poitner to use.
*/
void smtp_session_config(SmtpSession *p, u32 smtpFlags, void *pArg){
  p->smtpFlags = smtpFlags;
  if( smtpFlags & SMTP_TRACE_FILE ){
    p->logFile = (FILE*)pArg;
  }else if( smtpFlags & SMTP_TRACE_BLOB ){
    p->pTranscript = (Blob*)pArg;
  }
}

/*
** Send a single line of output the SMTP client to the server.
*/
static void smtp_send_line(SmtpSession *p, const char *zFormat, ...){
  Blob b = empty_blob;
  va_list ap;
  char *z;
  int n;
  if( !p->bOpen ) return;
  va_start(ap, zFormat);
  blob_vappendf(&b, zFormat, ap);
  va_end(ap);
  z = blob_buffer(&b);
  n = blob_size(&b);
  assert( n>=2 );
  assert( z[n-1]=='\n' );
  assert( z[n-2]=='\r' );
  if( p->smtpFlags & SMTP_TRACE_STDOUT ){
    fossil_print("C: %.*s\n", n-2, z);
  }
  if( p->smtpFlags & SMTP_TRACE_FILE ){
    fprintf(p->logFile, "C: %.*s\n", n-2, z);
  }
  if( p->smtpFlags & SMTP_TRACE_BLOB ){
    blob_appendf(p->pTranscript, "C: %.*s\n", n-2, z);
  }
  socket_send(0, z, n);
  blob_reset(&b);
}

/*
** Read a line of input received from the SMTP server.  Make in point
** to the next input line.
**
** Content is actually read into the p->in buffer.  Then blob_line()
** is used to extract individual lines, passing each to "in".
*/
static void smtp_recv_line(SmtpSession *p, Blob *in){
  int n = blob_size(&p->inbuf);
  char *z = blob_buffer(&p->inbuf);
  int i = blob_tell(&p->inbuf);
  int nDelay = 0;
  if( i<n && z[n-1]=='\n' ){
    blob_line(&p->inbuf, in);
  }else if( !p->bOpen ){
    blob_init(in, 0, 0);
  }else{
    if( n>0 && i>=n ){
      blob_truncate(&p->inbuf, 0);
      blob_rewind(&p->inbuf);
      n = 0;
    }
    do{
      size_t got;
      blob_resize(&p->inbuf, n+1000);
      z = blob_buffer(&p->inbuf);
      got = socket_receive(0, z+n, 1000, 1);
      if( got>0 ){
        in->nUsed += got;
        n += got;
        z[n] = 0;
        if( n>0 && z[n-1]=='\n' ) break;
        if( got==1000 ) continue;
      }
      nDelay++;
      if( nDelay>100 ){
        blob_init(in, 0, 0);
        smtp_set_error(p, 1, "client times out waiting on server response");
        return;
      }else{
        sqlite3_sleep(100);
      }
    }while( n<1 || z[n-1]!='\n' );
    blob_truncate(&p->inbuf, n);
    blob_line(&p->inbuf, in);
  }
  z = blob_buffer(in);
  n = blob_size(in);
  if( n && z[n-1]=='\n' ) n--;
  if( n && z[n-1]=='\r' ) n--;
  if( p->smtpFlags & SMTP_TRACE_STDOUT ){
    fossil_print("S: %.*s\n", n, z);
  }
  if( p->smtpFlags & SMTP_TRACE_FILE ){
    fprintf(p->logFile, "S: %.*s\n", n, z);
  }
  if( p->smtpFlags & SMTP_TRACE_BLOB ){
    blob_appendf(p->pTranscript, "S: %.*s\n", n-2, z);
  }
}

/*
** Capture a single-line server reply.
*/
static void smtp_get_reply_from_server(
  SmtpSession *p,   /* The SMTP connection */
  Blob *in,         /* Buffer used to hold the reply */
  int *piCode,      /* The return code */
  int *pbMore,      /* True if the reply is not complete */
  char **pzArg      /* Argument */
){
  int n;
  char *z;
  blob_truncate(in, 0);
  smtp_recv_line(p, in);
  blob_trim(in);
  z = blob_str(in);
  n = blob_size(in);
  if( z[0]=='#' ){
    *piCode = 0;
    *pbMore = 1;
    *pzArg = z;
  }else{
    *piCode = atoi(z);
    *pbMore = n>=4 && z[3]=='-';
    *pzArg = n>=4 ? z+4 : "";
  }
}

/*
** Have the client send a QUIT message.
*/
int smtp_client_quit(SmtpSession *p){
  Blob in = BLOB_INITIALIZER;
  int iCode = 0;
  int bMore = 0;
  char *zArg = 0;
  if( p->bOpen ){
    smtp_send_line(p, "QUIT\r\n");
    do{
      smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
    }while( bMore );
    p->bOpen = 0;
    socket_close();
  }
  return 0;
}

/*
** Begin a client SMTP session.  Wait for the initial 220 then send
** the EHLO and wait for a 250.
**
** Return 0 on success and non-zero for a failure.
*/
static int smtp_client_startup(SmtpSession *p){
  Blob in = BLOB_INITIALIZER;
  int iCode = 0;
  int bMore = 0;
  char *zArg = 0;
  if( p==0 || p->bFatal ) return 1;
  if( socket_open(&p->url) ){
    smtp_set_error(p, 1, "can't open socket: %z", socket_errmsg());
    return 1;
  }
  p->bOpen = 1;
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=220 ){
    smtp_set_error(p, 1, "conversation begins with: \"%d %s\"",iCode,zArg);
    smtp_client_quit(p);
    return 1;
  }
  smtp_send_line(p, "EHLO %s\r\n", p->zFrom);
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=250 ){
    smtp_set_error(p, 1, "reply to EHLO with: \"%d %s\"",iCode, zArg);
    smtp_client_quit(p);
    return 1;
  }
  fossil_free(p->zErr);
  p->zErr = 0;
  return 0;
}

/*
** COMMAND: test-smtp-probe
**
** Usage: %fossil test-smtp-probe DOMAIN [ME]
**
** Interact with the SMTP server for DOMAIN by setting up a connection
** and then immediately shutting it back down.  Log all interaction
** on the console.  Use ME as the domain name of the sender.
**
** Options:
**    --direct              Use DOMAIN directly without going through MX
**    --port N              Talk on TCP port N
*/
void test_smtp_probe(void){
  SmtpSession *p;
  const char *zDomain;
  const char *zSelf;
  const char *zPort;
  int iPort = 25;
  u32 smtpFlags = SMTP_TRACE_STDOUT|SMTP_PORT;

  if( find_option("direct",0,0)!=0 ) smtpFlags |= SMTP_DIRECT;
  zPort = find_option("port",0,1);
  if( zPort ) iPort = atoi(zPort);
  verify_all_options();
  if( g.argc!=3 && g.argc!=4 ) usage("DOMAIN [ME]");
  zDomain = g.argv[2];
  zSelf = g.argc==4 ? g.argv[3] : "fossil-scm.org";
  p = smtp_session_new(zSelf, zDomain, smtpFlags, iPort);
  if( p->zErr ){
    fossil_fatal("%s", p->zErr);
  }
  fossil_print("Connection to \"%s\"\n", p->zHostname);
  smtp_client_startup(p);
  smtp_client_quit(p);
  if( p->zErr ){
    fossil_fatal("ERROR: %s\n", p->zErr);
  }
  smtp_session_free(p);
}

/*
** Send the content of an email message followed by a single
** "." line.  All lines must be \r\n terminated.  Any isolated
** \n line terminators in the input must be converted.  Also,
** a line beginning with "." must have the dot doubled per
** https://tools.ietf.org/html/rfc5321#section-4.5.2
*/
static void smtp_send_email_body(
  const char *zMsg,                          /* Message to send */
  size_t (*xSend)(void*,const void*,size_t), /* Sender callback function */
  void *pArg                                 /* First arg to sender */
){
  Blob in;
  Blob out = BLOB_INITIALIZER;
  Blob line;
  blob_init(&in, zMsg, -1);
  while( blob_line(&in, &line) ){
    char *z = blob_buffer(&line);
    int n = blob_size(&line);
    if( n==0 ) break;
    n--;
    if( n && z[n-1]=='\r' ) n--;
    if( z[0]=='.' ){
      blob_append(&out, "..", 2);   /* RFC 5321 § 4.5.2 */
      blob_append(&out, z+1, n-1);
    }else{
      blob_append(&out, z, n);
    }
    blob_append(&out, "\r\n", 2);
  }
  blob_append(&out, ".\r\n", 3);
  xSend(pArg, blob_buffer(&out), blob_size(&out));
  blob_reset(&out);
  blob_reset(&line);
}

/* A sender function appropriate for use by smtp_send_email_body() to
** send all content to the console, for testing.
*/
static size_t smtp_test_sender(void *NotUsed, const void *pContent, size_t N){
  return fwrite(pContent, 1, N, stdout);
}

/*
** COMMAND: test-smtp-senddata
**
** Usage: %fossil test-smtp-senddata FILE
**
** Read content from FILE, then send it to stdout encoded as if sent
** to the DATA portion of an SMTP session.  This command is used to
** test the encoding logic.
*/
void test_smtp_senddata(void){
  Blob f;
  if( g.argc!=3 ) usage("FILE");
  blob_read_from_file(&f, g.argv[2], ExtFILE);
  smtp_send_email_body(blob_str(&f), smtp_test_sender, 0);
  blob_reset(&f);
}

/*
** Send a single email message to the SMTP server.
**
** All email addresses (zFrom and azTo) must be plain "local@domain"
** format without the surrounding "<..>".  This routine will add the
** necessary "<..>".
**
** The body of the email should be well-structured.  This routine will
** convert any \n line endings into \r\n and will escape lines containing
** just ".", but will not make any other alterations or corrections to
** the message content.
**
** Return 0 on success.  Otherwise an error code.
*/
int smtp_send_msg(
  SmtpSession *p,        /* The SMTP server to which the message is sent */
  const char *zFrom,     /* Who the message is from */
  int nTo,               /* Number of receipients */
  const char **azTo,     /* Email address of each recipient */
  const char *zMsg       /* Body of the message */
){
  int i;
  int iCode = 0;
  int bMore = 0;
  char *zArg = 0;
  Blob in;
  blob_init(&in, 0, 0);
  if( !p->bOpen ){
    if( !p->bFatal ) smtp_client_startup(p);
    if( !p->bOpen ) return 1;
  }
  smtp_send_line(p, "MAIL FROM:<%s>\r\n", zFrom);
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=250 ){
    smtp_set_error(p, 0,"reply to MAIL FROM: \"%d %s\"",iCode,zArg);
    return 1;
  }
  for(i=0; i<nTo; i++){
    smtp_send_line(p, "RCPT TO:<%s>\r\n", azTo[i]);
    do{
      smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
    }while( bMore );
    if( iCode!=250 ){
      smtp_set_error(p, 0,"reply to RCPT TO: \"%d %s\"",iCode,zArg);
      return 1;
    }
  }
  smtp_send_line(p, "DATA\r\n");
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=354 ){
    smtp_set_error(p, 0, "reply to DATA with: \"%d %s\"",iCode,zArg);
    return 1;
  }
  smtp_send_email_body(zMsg, socket_send, 0);
  if( p->smtpFlags & SMTP_TRACE_STDOUT ){
    fossil_print("C: # message content\nC: .\n");
  }
  if( p->smtpFlags & SMTP_TRACE_FILE ){
    fprintf(p->logFile, "C: # message content\nC: .\n");
  }
  if( p->smtpFlags & SMTP_TRACE_BLOB ){
    blob_appendf(p->pTranscript, "C: # message content\nC: .\n");
  }
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=250 ){
    smtp_set_error(p, 0, "reply to end-of-DATA with: \"%d %s\"",
                   iCode, zArg);
    return 1;
  }
  return 0;
}

/*
** The input is a base email address of the form "local@domain".
** Return a pointer to just the "domain" part, or 0 if the string
** contains no "@".
*/
const char *domain_of_addr(const char *z){
  while( z[0] && z[0]!='@' ) z++;
  if( z[0]==0 ) return 0;
  return z+1;
}


/*
** COMMAND: test-smtp-send
**
** Usage: %fossil test-smtp-send EMAIL FROM TO ...
**
** Use SMTP to send the email message contained in the file named EMAIL
** to the list of users TO.  FROM is the sender of the email.
**
** Options:
**      --direct              Go directly to the TO domain.  Bypass MX lookup
**      --relayhost R         Use R as relay host directly for delivery.
**      --port N              Use TCP port N instead of 25
**      --trace               Show the SMTP conversation on the console
*/
void test_smtp_send(void){
  SmtpSession *p;
  const char *zFrom;
  int nTo;
  const char *zToDomain;
  const char *zFromDomain;
  const char *zRelay;
  const char **azTo;
  int smtpPort = 25;
  const char *zPort;
  Blob body;
  u32 smtpFlags = SMTP_PORT;
  if( find_option("trace",0,0)!=0 ) smtpFlags |= SMTP_TRACE_STDOUT;
  if( find_option("direct",0,0)!=0 ) smtpFlags |= SMTP_DIRECT;
  zPort = find_option("port",0,1);
  if( zPort ) smtpPort = atoi(zPort);
  zRelay = find_option("relayhost",0,1);
  verify_all_options();
  if( g.argc<5 ) usage("EMAIL FROM TO ...");
  blob_read_from_file(&body, g.argv[2], ExtFILE);
  zFrom = g.argv[3];
  nTo = g.argc-4;
  azTo = (const char**)g.argv+4;
  zFromDomain = domain_of_addr(zFrom);
  if( zRelay!=0 && zRelay[0]!= 0) {
    smtpFlags |= SMTP_DIRECT;
    zToDomain = zRelay;
  }else{
    zToDomain = domain_of_addr(azTo[0]);
  }
  p = smtp_session_new(zFromDomain, zToDomain, smtpFlags, smtpPort);
  if( p->zErr ){
    fossil_fatal("%s", p->zErr);
  }
  fossil_print("Connection to \"%s\"\n", p->zHostname);
  smtp_send_msg(p, zFrom, nTo, azTo, blob_str(&body));
  smtp_client_quit(p);
  if( p->zErr ){
    fossil_fatal("ERROR: %s\n", p->zErr);
  }
  smtp_session_free(p);
  blob_reset(&body);
}
