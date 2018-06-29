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
  int atEof;                /* True after connection closes */
  char *zErr;               /* Error message */
  Blob inbuf;               /* Input buffer */
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
** Allocate a new SmtpSession object.
**
** Both zFrom and zDest must be specified.
**
** The ... arguments are in this order:
**
**    SMTP_PORT:            int
**    SMTP_TRACE_FILE:      FILE*
**    SMTP_TRACE_BLOB:      Blob*
*/
SmtpSession *smtp_session_new(
  const char *zFrom,    /* Domain for the client */
  const char *zDest,    /* Domain of the server */
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
  p->smtpFlags = smtpFlags;
  memset(&url, 0, sizeof(url));
  url.port = 25;
  blob_init(&p->inbuf, 0, 0);
  va_start(ap, smtpFlags);
  if( smtpFlags & SMTP_PORT ){
    url.port = va_arg(ap, int);
  }
  if( smtpFlags & SMTP_TRACE_FILE ){
    p->logFile = va_arg(ap, FILE*);
  }
  if( smtpFlags & SMTP_TRACE_BLOB ){
    p->pTranscript = va_arg(ap, Blob*);
  }
  va_end(ap);
  if( (smtpFlags & SMTP_DIRECT)!=0 ){
    p->zHostname = fossil_strdup(zDest);
  }else{
    p->zHostname = smtp_mx_host(zDest);
  }
  if( p->zHostname==0 ){
    p->atEof = 1;
    p->zErr = mprintf("cannot locate SMTP server for \"%s\"", zDest);
    return p;
  }
  url.name = p->zHostname;
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
  p->atEof = 1;
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
**
** Options:
**
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
** an line contain using "." should be converted to "..".
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
    if( n==1 && z[0]=='.' ){
      blob_append(&out, "..\r\n", 4);
    }else{
      blob_append(&out, z, n);
      blob_append(&out, "\r\n", 2);
    }
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
** format with the surrounding "<..>".  This routine will add the
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
  smtp_send_line(p, "MAIL FROM:<%s>\r\n", zFrom);
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=250 ) return 1;
  for(i=0; i<nTo; i++){
    smtp_send_line(p, "RCPT TO:<%s>\r\n", azTo[i]);
    do{
      smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
    }while( bMore );
    if( iCode!=250 ) return 1;
  }
  smtp_send_line(p, "DATA\r\n");
  do{
    smtp_get_reply_from_server(p, &in, &iCode, &bMore, &zArg);
  }while( bMore );
  if( iCode!=354 ) return 1;
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
  if( iCode!=250 ) return 1;
  return 0;
}

/*
** The input is a base email address of the form "local@domain".
** Return a pointer to just the "domain" part.
*/
static const char *domainOfAddr(const char *z){
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
**
**      --direct              Go directly to the TO domain.  Bypass MX lookup
**      --port N              Use TCP port N instead of 25
**      --trace               Show the SMTP conversation on the console
*/
void test_smtp_send(void){
  SmtpSession *p;
  const char *zFrom;
  int nTo;
  const char *zToDomain;
  const char *zFromDomain;
  const char **azTo;
  int smtpPort = 25;
  const char *zPort;
  Blob body;
  u32 smtpFlags = SMTP_PORT;
  if( find_option("trace",0,0)!=0 ) smtpFlags |= SMTP_TRACE_STDOUT;
  if( find_option("direct",0,0)!=0 ) smtpFlags |= SMTP_DIRECT;
  zPort = find_option("port",0,1);
  if( zPort ) smtpPort = atoi(zPort);
  verify_all_options();
  if( g.argc<5 ) usage("EMAIL FROM TO ...");
  blob_read_from_file(&body, g.argv[2], ExtFILE);
  zFrom = g.argv[3];
  nTo = g.argc-4;
  azTo = (const char**)g.argv+4;
  zFromDomain = domainOfAddr(zFrom);
  zToDomain = domainOfAddr(azTo[0]);
  p = smtp_session_new(zFromDomain, zToDomain, smtpFlags, smtpPort);
  if( p->zErr ){
    fossil_fatal("%s", p->zErr);
  }
  fossil_print("Connection to \"%s\"\n", p->zHostname);
  smtp_client_startup(p);
  smtp_send_msg(p, zFrom, nTo, azTo, blob_str(&body));
  smtp_client_quit(p);
  if( p->zErr ){
    fossil_fatal("ERROR: %s\n", p->zErr);
  }
  smtp_session_free(p);
  blob_reset(&body);
}

/*****************************************************************************
** Server implementation
*****************************************************************************/

/*
** Schema used by the email processing system.
*/
static const char zEmailSchema[] = 
@ -- bulk storage is in a separate table.  This table can store either
@ -- the body of email messages or transcripts of smtp sessions.
@ CREATE TABLE IF NOT EXISTS repository.emailblob(
@   emailid INTEGER PRIMARY KEY,  -- numeric idea for the entry
@   ets INT,                      -- Corresponding transcript, or NULL
@   etime INT,                    -- insertion time, secs since 1970
@   etxt TEXT                     -- content of this entry
@ );
@
@ -- One row for each mailbox entry.  All users emails are stored in
@ -- this same table.
@ CREATE TABLE IF NOT EXISTS repository.emailbox(
@   euser TEXT,          -- User who received this email
@   edate INT,           -- Date received.  Seconds since 1970
@   efrom TEXT,          -- Who is the email from
@   emsgid INT,          -- Raw email text
@   ets INT,             -- Transcript of the receiving SMTP session
@   estate INT,          -- Unread, read, starred, etc.
@   esubject TEXT        -- Subject line for display
@ );
@
@ -- Information on how to deliver incoming email.
@ CREATE TABLE IF NOT EXISTS repository.emailroute(
@   eaddr TEXT PRIMARY KEY,  -- Email address
@   epolicy TEXT             -- How to handle email sent to this address
@ ) WITHOUT ROWID;
@
@ -- Outgoing email queue
@ CREATE TABLE IF NOT EXISTS repository.emailoutq(
@   edomain TEXT,            -- Destination domain.  (ex: "fossil-scm.org")
@   efrom TEXT,              -- Sender email address
@   eto TEXT,                -- Receipient email address
@   emsgid INT,              -- Message body in the emailblob table
@   ectime INT,              -- Time enqueued.  Seconds since 1970
@   emtime INT,              -- Time of last send attempt.  Sec since 1970
@   ensend INT,              -- Number of send attempts
@   ets INT                  -- Transcript of last failed attempt
@ );
;

/*
** Code used to delete the email tables.
*/
static const char zEmailDrop[] =
@ DROP TABLE IF EXISTS emailblob;
@ DROP TABLE IF EXISTS emailbox;
@ DROP TABLE IF EXISTS emailroute;
@ DROP TABLE IF EXISTS emailqueue;
;

/*
** Populate the schema of a database.
**
**   eForce==0          Fast
**   eForce==1          Run CREATE TABLE statements every time
**   eForce==2          DROP then rerun CREATE TABLE
*/
void smtp_server_schema(int eForce){
  if( eForce==2 ){
    db_multi_exec(zEmailDrop/*works-like:""*/);
  }
  if( eForce==1 || !db_table_exists("repository","emailblob") ){
    db_multi_exec(zEmailSchema/*works-like:""*/);
  }
}

#if LOCAL_INTERFACE
/*
** State information for the server
*/
struct SmtpServer {
  sqlite3_int64 idTranscript; /* Transcript ID number */
  sqlite3_int64 idMsg;        /* Message ID number */
  char *zEhlo;                /* Client domain on the EHLO line */
  char *zFrom;                /* MAIL FROM: argument */
  int nTo;                    /* Number of RCPT TO: lines seen */
  struct SmtpTo {
    char *z;                    /* Address in each RCPT TO line */
    int okRemote;               /* zTo can be in another domain */
  } *aTo;
  u32 srvrFlags;              /* Control flags */
  Blob msg;                   /* Content following DATA */
  Blob transcript;            /* Session transcript */
};

#define SMTPSRV_CLEAR_MSG    1   /* smtp_server_clear() last message only */
#define SMTPSRV_CLEAR_ALL    2   /* smtp_server_clear() everything */
#define SMTPSRV_LOG       0x001  /* Record a transcript of the interaction */
#define SMTPSRV_STDERR    0x002  /* Transcription written to stderr */

#endif /* LOCAL_INTERFACE */

/*
** Clear the SmtpServer object.  Deallocate resources.
** How much to clear depends on eHowMuch 
*/
static void smtp_server_clear(SmtpServer *p, int eHowMuch){
  int i;
  if( eHowMuch>=SMTPSRV_CLEAR_MSG ){
    fossil_free(p->zFrom);
    p->zFrom = 0;
    for(i=0; i<p->nTo; i++) fossil_free(p->aTo[i].z);
    fossil_free(p->aTo);
    p->aTo = 0;
    p->nTo = 0;
    blob_reset(&p->msg);
    p->idMsg = 0;
  }
  if( eHowMuch>=SMTPSRV_CLEAR_ALL ){
    blob_reset(&p->transcript);
    p->idTranscript = 0;
    fossil_free(p->zEhlo);
    p->zEhlo = 0;
  }
}

/*
** Turn raw memory into an SmtpServer object.
*/
static void smtp_server_init(SmtpServer *p){
  memset(p, 0, sizeof(*p));
  blob_init(&p->msg, 0, 0);
  blob_init(&p->transcript, 0, 0);
}

/*
** Append a new TO entry to the SmtpServer object.  Do not do the
** append if the same entry is already on the list.
**
** The zAddr argument is obtained from fossil_malloc().  This
** routine assumes ownership of the allocation.
*/
static void smtp_append_to(SmtpServer *p, char *zAddr, int okRemote){
  int i;
  for(i=0; zAddr[i]; i++){ zAddr[i] = fossil_tolower(zAddr[i]); }
  for(i=0; i<p->nTo; i++){
    if( strcmp(zAddr, p->aTo[i].z)==0 ){
      fossil_free(zAddr);
      if( p->aTo[i].okRemote==0 ) p->aTo[i].okRemote = okRemote;
      return;
    }
  }
  p->aTo = fossil_realloc(p->aTo, (p->nTo+1)*sizeof(p->aTo[0]));
  p->aTo[p->nTo].z = zAddr;
  p->aTo[p->nTo].okRemote = okRemote;
  p->nTo++;
}

/*
** Send a single line of output from the server to the client.
*/
static void smtp_server_send(SmtpServer *p, const char *zFormat, ...){
  Blob b = empty_blob;
  va_list ap;
  char *z;
  int n;
  va_start(ap, zFormat);
  blob_vappendf(&b, zFormat, ap);
  va_end(ap);
  z = blob_buffer(&b);
  n = blob_size(&b);
  assert( n>=2 );
  assert( z[n-1]=='\n' );
  assert( z[n-2]=='\r' );
  if( p->srvrFlags & SMTPSRV_LOG ){
    blob_appendf(&p->transcript, "S: %.*s\n", n-2, z);
  }
  if( p->srvrFlags & SMTPSRV_STDERR ){
    fprintf(stderr, "S: %.*s\n", n-2, z);
  }
  fwrite(z, n, 1, stdout);
  fflush(stdout);
  blob_reset(&b);
}

/*
** Read a single line from the client.
*/
static int smtp_server_gets(SmtpServer *p, char *aBuf, int nBuf){
  int rc = fgets(aBuf, nBuf, stdin)!=0;
  if( rc ){
    if( (p->srvrFlags & SMTPSRV_LOG)!=0 ){
      blob_appendf(&p->transcript, "C: %s", aBuf);
    }
    if( (p->srvrFlags & SMTPSRV_STDERR)!=0 ){
      fprintf(stderr, "C: %s", aBuf);
    }
  }
  return rc;
}

/*
** Capture the incoming email data into the p->msg blob.  Dequote
** lines of "..\r\n" into just ".\r\n".
*/
static void smtp_server_capture_data(SmtpServer *p, char *z, int n){
  int nLine = 0;
  while( fgets(z, n, stdin) ){
    if( strncmp(z, ".\r\n", 3)==0 || strncmp(z, ".\n",2)==0 ) break;
    nLine++;
    if( strncmp(z, "..\r\n", 4)==0 || strncmp(z, "..\n",3)==0 ){
      memmove(z, z+1, 4);
    }
    blob_append(&p->msg, z, -1);
  }
  if( p->srvrFlags & SMTPSRV_LOG ){
    blob_appendf(&p->transcript, "C: # %d lines, %d bytes of content\n",
          nLine, blob_size(&p->msg));
  }
  if( p->srvrFlags & SMTPSRV_STDERR ){
    fprintf(stderr, "C: # %d lines, %d bytes of content\n",
          nLine, blob_size(&p->msg));
  }
}

/*
** Send an email to a single email addess that is registered with
** this system, according to the instructions in emailroute.  If
** zAddr is not in the emailroute table, then this routine is a
** no-op.  Or if zAddr has already been processed, then this
** routine is a no-op.
*/
static void smtp_server_send_one_user(
  SmtpServer *p,         /* The current inbound email */
  const char *zAddr,     /* Who to forward this to */
  int okRemote           /* True if ok to foward to another domain */
){
  char *zPolicy;
  Blob policy, line, token, tail;

  zPolicy = db_text(0, 
    "SELECT epolicy FROM emailroute WHERE eaddr=%Q", zAddr);
  if( zPolicy==0 ){
    if( okRemote ){
      int i;
      for(i=0; zAddr[i] && zAddr[i]!='@'; i++){}
      if( zAddr[i]=='@' && zAddr[i+1]!=0 ){
        db_multi_exec(
          "INSERT INTO emailoutq(edomain,efrom,eto,emsgid,ectime,"
                                "emtime,ensend)"
          "VALUES(%Q,%Q,%Q,%lld,now(),0,0)",
          zAddr+i+1, p->zFrom, zAddr, p->idMsg
        );
      }
    }
    return;
  }
  blob_init(&policy, zPolicy, -1);
  while( blob_line(&policy, &line) ){
    blob_trim(&line);
    blob_token(&line, &token);
    blob_tail(&line, &tail);
    if( blob_size(&tail)==0 ) continue;
    if( blob_eq_str(&token, "mbox", 4) ){
      db_multi_exec(
        "INSERT INTO emailbox(euser,edate,efrom,emsgid,ets,estate)"
        " VALUES(%Q,now(),%Q,%lld,%lld,0)",
          blob_str(&tail), p->zFrom, p->idMsg, p->idTranscript
      );
    }
    if( blob_eq_str(&token, "forward", 7) ){
      smtp_append_to(p, fossil_strdup(blob_str(&tail)), 1);
    }
    blob_reset(&tail);
  }
}

/*
** The SmtpServer object contains a complete incoming email.
** Add this email to the database.
*/
static void smtp_server_route_incoming(SmtpServer *p, int bFinish){
  Stmt s;
  int i, j;
  if( p->zFrom && p->nTo && blob_size(&p->msg) ){
    db_begin_transaction();
    if( p->idTranscript==0 ) smtp_server_schema(0);
    db_prepare(&s,
      "INSERT INTO emailblob(ets,etime,etxt)"
      " VALUES(:ets,now(),:etxt)"
    );
    if( !bFinish && p->idTranscript==0 ){
      db_bind_null(&s, ":ets");
      db_bind_null(&s, ":etxt");
      db_step(&s);
      db_reset(&s);
      p->idTranscript = db_last_insert_rowid();
    }else if( bFinish ){
      if( p->idTranscript ){
        db_multi_exec("UPDATE emailblob SET etxt=%Q WHERE emailid=%lld",
           blob_str(&p->transcript), p->idTranscript);
      }else{
        db_bind_null(&s, ":ets");
        db_bind_str(&s, ":etxt", &p->transcript);
        db_step(&s);
        db_reset(&s);
        p->idTranscript = db_last_insert_rowid();
      }
    }
    db_bind_int64(&s, ":ets", p->idTranscript);
    db_bind_str(&s, ":etxt", &p->msg);
    db_step(&s);
    db_finalize(&s);
    p->idMsg = db_last_insert_rowid();

    /* make entries in emailbox and emailoutq */
    for(i=0; i<p->nTo; i++){
      int okRemote = p->aTo[i].okRemote;
      p->aTo[i].okRemote = 1;
      smtp_server_send_one_user(p, p->aTo[i].z, okRemote);
    }

    /* Finish the transaction after all changes are implemented */
    db_end_transaction(0);
  }
  smtp_server_clear(p, SMTPSRV_CLEAR_MSG);
}

/*
** Make a copy of the input string up to but not including the
** first ">" character.
*/
static char *extractEmail(const char *z){
  int i;
  for(i=0; z[i] && z[i]!='>'; i++){}
  return mprintf("%.*s", i, z);
}

/*
** COMMAND: smtpd
**
** Usage: %fossil smtpd [OPTIONS] REPOSITORY
**
** Begin a SMTP conversation with a client using stdin/stdout.  The
** received email is stored in REPOSITORY
**
*/
void smtp_server(void){
  char *zDbName;
  const char *zDomain;
  SmtpServer x;
  char z[5000];

  smtp_server_init(&x);
  zDomain = find_option("domain",0,1);
  if( zDomain==0 ) zDomain = "";
  x.srvrFlags = SMTPSRV_LOG;
  if( find_option("trace",0,0)!=0 ) x.srvrFlags |= SMTPSRV_STDERR;
  verify_all_options();
  if( g.argc!=3 ) usage("DBNAME");
  zDbName = g.argv[2];
  zDbName = enter_chroot_jail(zDbName, 0);
  db_open_repository(zDbName);
  smtp_server_send(&x, "220 %s ESMTP https://fossil-scm.org/ %s\r\n",
                   zDomain, MANIFEST_VERSION);
  while( smtp_server_gets(&x, z, sizeof(z)) ){
    if( strncmp(z, "EHLO ", 5)==0 ){
      smtp_server_send(&x, "250 ok\r\n");
    }else
    if( strncmp(z, "HELO ", 5)==0 ){
      smtp_server_send(&x, "250 ok\r\n");
    }else
    if( strncmp(z, "MAIL FROM:<", 11)==0 ){
      smtp_server_route_incoming(&x, 0);
      smtp_server_clear(&x, SMTPSRV_CLEAR_MSG);
      x.zFrom = extractEmail(z+11);
      smtp_server_send(&x, "250 ok\r\n");
    }else
    if( strncmp(z, "RCPT TO:<", 9)==0 ){
      char *zAddr = extractEmail(z+9);
      smtp_append_to(&x, zAddr, 0);
      if( x.nTo>=100 ){
        smtp_server_send(&x, "452 too many recipients\r\n");
        continue;
      }
      smtp_server_send(&x, "250 ok\r\n");
    }else
    if( strncmp(z, "DATA", 4)==0 ){
      smtp_server_send(&x, "354 ready\r\n");
      smtp_server_capture_data(&x, z, sizeof(z));
      smtp_server_send(&x, "250 ok\r\n");
    }else
    if( strncmp(z, "QUIT", 4)==0 ){
      smtp_server_send(&x, "221 closing connection\r\n");
      break;
    }else
    {
      smtp_server_send(&x, "500 unknown command\r\n");
    }
  }
  smtp_server_route_incoming(&x, 1);
  smtp_server_clear(&x, SMTPSRV_CLEAR_ALL);
}
