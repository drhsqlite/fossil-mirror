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


#if !defined(FOSSIL_OMIT_SMTP)
#if defined(_WIN32)
   /* Don't yet know how to do this on windows */
#else
#  include <sys/types.h>
#  include <netinet/in.h>
#  include <arpa/nameser.h>
#  include <resolv.h>
#endif
#endif /* !defined(FOSSIL_OMIT_SMTP) */


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
#if !defined(_WIN32) && !defined(FOSSIL_OMIT_SMTP)
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
    ns_name_uncompress(aDns, aDns+nDns, pBest+2,
                       zHostname, sizeof(zHostname));
    return fossil_strdup(zHostname);
  }
#endif /* not windows */
  return 0;
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
  if( g.argc<2 ){
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
  const char *zLabel;       /* Either "CS" or "SC" */
  int atEof;                /* True after connection closes */
  char *zErr;               /* Error message */
  Blob inbuf;               /* Input buffer */
};

/* Allowed values for SmtpSession.smtpFlags */
#define SMTP_TRACE_STDOUT   0x00001     /* Debugging info to console */
#define SMTP_TRACE_FILE     0x00002     /* Debugging info to logFile */
#define SMTP_TRACE_BLOB     0x00004     /* Record transcript */

#endif

/*
** Shutdown an SmtpSession
*/
void smtp_session_free(SmtpSession *pSession){
  socket_close();
  blob_zero(&pSession->inbuf);
  fossil_free(pSession->zHostname);
  fossil_free(pSession->zErr);
  fossil_free(pSession);
}

/*
** Allocate a new SmtpSession object.
**
** Both zFrom and zDest must be specified for a client side SMTP connection.
** For a server-side, specify only zFrom.
*/
SmtpSession *smtp_session_new(
  const char *zFrom,    /* Domain name of our end. */
  const char *zDest,    /* Domain of the other end. */
  u32 smtpFlags,        /* Flags */
  ...                   /* Arguments depending on the flags */
){
  SmtpSession *p;
  va_list ap;
  UrlData url;

  p = fossil_malloc( sizeof(*p) );
  memset(p, 0, sizeof(*p));
  p->zFrom = zFrom;
  p->zDest = zDest;
  p->zLabel = zDest==0 ? "CS" : "SC";
  p->smtpFlags = smtpFlags;
  blob_init(&p->inbuf, 0, 0);
  va_start(ap, smtpFlags);
  if( smtpFlags & SMTP_TRACE_FILE ){
    p->logFile = va_arg(ap, FILE*);
  }else if( smtpFlags & SMTP_TRACE_BLOB ){
    p->pTranscript = va_arg(ap, Blob*);
  }
  va_end(ap);
  p->zHostname = smtp_mx_host(zDest);
  if( p->zHostname==0 ){
    p->atEof = 1;
    p->zErr = mprintf("cannot locate SMTP server for \"%s\"", zDest);
    return p;
  }
  memset(&url, 0, sizeof(url));
  url.name = p->zHostname;
  url.port = 25;
  socket_global_init();
  if( socket_open(&url) ){
    p->atEof = 1;
    p->zErr = socket_errmsg();
    socket_close();
  }
  return p;
}

/*
** Send a single line of output the SMTP client to the server.
*/
static void smtp_send_line(SmtpSession *p, const char *zFormat, ...){
  Blob b = empty_blob;
  va_list ap;
  char *z;
  int n;
  if( p->atEof ) return;
  va_start(ap, zFormat);
  blob_vappendf(&b, zFormat, ap);
  va_end(ap);
  z = blob_buffer(&b);
  n = blob_size(&b);
  assert( n>=2 );
  assert( z[n-1]=='\n' );
  assert( z[n-2]=='\r' );
  if( p->smtpFlags & SMTP_TRACE_STDOUT ){
    fossil_print("%c: %.*s\n", p->zLabel[1], n-2, z);
  }
  if( p->smtpFlags & SMTP_TRACE_FILE ){
    fprintf(p->logFile, "%c: %.*s\n", p->zLabel[1], n-2, z);
  }
  if( p->smtpFlags & SMTP_TRACE_BLOB ){
    blob_appendf(p->pTranscript, "%c: %.*s\n", p->zLabel[1], n-2, z);
  }
  socket_send(0, z, n);
  blob_zero(&b);
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
  }else if( p->atEof ){
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
        p->zErr = mprintf("timeout");
        socket_close();
        p->atEof = 1;
        return;
      }else{
        sqlite3_sleep(25);
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
    fossil_print("%c: %.*s\n", p->zLabel[0], n, z);
  }
  if( p->smtpFlags & SMTP_TRACE_FILE ){
    fprintf(p->logFile, "%c: %.*s\n", p->zLabel[0], n, z);
  }
  if( p->smtpFlags & SMTP_TRACE_BLOB ){
    blob_appendf(p->pTranscript, "%c: %.*s\n", p->zLabel[0], n-2, z);
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
  smtp_send_line(p, "QUIT\r\n");
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  socket_close();
  return 0;
}

/*
** Begin a client SMTP session.  Wait for the initial 220 then send
** the EHLO and wait for a 250.
**
** Return 0 on success and non-zero for a failure.
*/
int smtp_client_startup(SmtpSession *p){
  Blob in = BLOB_INITIALIZER;
  int iCode = 0;
  int bMore = 0;
  char *zArg = 0;
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=220 ){
    smtp_client_quit(p);
    return 1;
  }
  smtp_send_line(p, "EHLO %s\r\n", p->zFrom);
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=250 ){
    smtp_client_quit(p);
    return 1;
  }
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
*/
void test_smtp_probe(void){
  SmtpSession *p;
  int rc;
  const char *zDomain;
  const char *zSelf;
  if( g.argc!=3 && g.argc!=4 ) usage("DOMAIN [ME]");
  zDomain = g.argv[2];
  zSelf = g.argc==4 ? g.argv[3] : "fossil-scm.org";
  p = smtp_session_new(zSelf, zDomain, SMTP_TRACE_STDOUT);
  if( p->zErr ){
    fossil_fatal("%s", p->zErr);
  }
  fossil_print("Connection to \"%s\"\n", p->zHostname);
  rc = smtp_client_startup(p);
  if( !rc ) smtp_client_quit(p);
  if( p->zErr ){
    fossil_fatal("ERROR: %s\n", p->zErr);
  }
  smtp_session_free(p);
}
