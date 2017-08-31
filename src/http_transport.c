/*
** Copyright (c) 2009 D. Richard Hipp
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
** This module implements the transport layer for the client side HTTP
** connection.  The purpose of this layer is to provide a common interface
** for both HTTP and HTTPS and to provide a common "fetch one line"
** interface that is used for parsing the reply.
*/
#include "config.h"
#include "http_transport.h"

/*
** State information
*/
static struct {
  int isOpen;             /* True when the transport layer is open */
  char *pBuf;             /* Buffer used to hold the reply */
  int nAlloc;             /* Space allocated for transportBuf[] */
  int nUsed ;             /* Space of transportBuf[] used */
  int iCursor;            /* Next unread by in transportBuf[] */
  i64 nSent;              /* Number of bytes sent */
  i64 nRcvd;              /* Number of bytes received */
  FILE *pFile;            /* File I/O for FILE: */
  char *zOutFile;         /* Name of outbound file for FILE: */
  char *zInFile;          /* Name of inbound file for FILE: */
  FILE *pLog;             /* Log output here */
} transport = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
** Information about the connection to the SSH subprocess when
** using the ssh:// sync method.
*/
static int sshPid;             /* Process id of ssh subprocess */
static int sshIn;              /* From ssh subprocess to this process */
static FILE *sshOut;           /* From this to ssh subprocess */


/*
** Return the current transport error message.
*/
const char *transport_errmsg(UrlData *pUrlData){
  #ifdef FOSSIL_ENABLE_SSL
  if( pUrlData->isHttps ){
    return ssl_errmsg();
  }
  #endif
  return socket_errmsg();
}

/*
** Retrieve send/receive counts from the transport layer.  If "resetFlag"
** is true, then reset the counts.
*/
void transport_stats(i64 *pnSent, i64 *pnRcvd, int resetFlag){
  if( pnSent ) *pnSent = transport.nSent;
  if( pnRcvd ) *pnRcvd = transport.nRcvd;
  if( resetFlag ){
    transport.nSent = 0;
    transport.nRcvd = 0;
  }
}

/*
** Check zFossil to see if it is a reasonable "fossil" command to
** run on the server.  Do not allow an attacker to substitute something
** like "/bin/rm".
*/
static int is_safe_fossil_command(const char *zFossil){
  static const char *azSafe[] = { "*/fossil", "*/echo" };
  int i;
  for(i=0; i<sizeof(azSafe)/sizeof(azSafe[0]); i++){
    if( sqlite3_strglob(azSafe[i], zFossil)==0 ) return 1;
    if( strcmp(azSafe[i]+2, zFossil)==0 ) return 1;
  }
  return 0;
}

/*
** Default SSH command
*/
#ifdef _WIN32
static const char zDefaultSshCmd[] = "plink -ssh -T";
#else
static const char zDefaultSshCmd[] = "ssh -e none -T";
#endif

/*
** SSH initialization of the transport layer
*/
int transport_ssh_open(UrlData *pUrlData){
  /* For SSH we need to create and run SSH fossil http
  ** to talk to the remote machine.
  */
  char *zSsh;        /* The base SSH command */
  Blob zCmd;         /* The SSH command */
  char *zHost;       /* The host name to contact */

  socket_ssh_resolve_addr(pUrlData);
  zSsh = db_get("ssh-command", zDefaultSshCmd);
  blob_init(&zCmd, zSsh, -1);
  if( pUrlData->port!=pUrlData->dfltPort && pUrlData->port ){
#ifdef _WIN32
    blob_appendf(&zCmd, " -P %d", pUrlData->port);
#else
    blob_appendf(&zCmd, " -p %d", pUrlData->port);
#endif
  }
  if( pUrlData->user && pUrlData->user[0] ){
    zHost = mprintf("%s@%s", pUrlData->user, pUrlData->name);
    blob_append_escaped_arg(&zCmd, zHost);
    fossil_free(zHost);
  }else{
    blob_append_escaped_arg(&zCmd, pUrlData->name);
  }
  if( !is_safe_fossil_command(pUrlData->fossil) ){
    fossil_fatal("the ssh:// URL is asking to run an unsafe command [%s] on "
                 "the server.", pUrlData->fossil);
  }
  blob_append_escaped_arg(&zCmd, pUrlData->fossil);
  blob_append(&zCmd, " test-http", 10);
  if( pUrlData->path && pUrlData->path[0] ){
    blob_append_escaped_arg(&zCmd, pUrlData->path);
  }else{
    fossil_fatal("ssh:// URI does not specify a path to the repository");
  }
  if( g.fSshTrace ){
    fossil_print("%s\n", blob_str(&zCmd));  /* Show the whole SSH command */
  }
  popen2(blob_str(&zCmd), &sshIn, &sshOut, &sshPid);
  if( sshPid==0 ){
    socket_set_errmsg("cannot start ssh tunnel using [%b]", &zCmd);
  }
  blob_reset(&zCmd);
  return sshPid==0;
}

/*
** Open a connection to the server.  The server is defined by the following
** variables:
**
**   pUrlData->name        Name of the server.  Ex: www.fossil-scm.org
**   pUrlData->port        TCP/IP port.  Ex: 80
**   pUrlData->isHttps     Use TLS for the connection
**
** Return the number of errors.
*/
int transport_open(UrlData *pUrlData){
  int rc = 0;
  if( transport.isOpen==0 ){
    if( pUrlData->isSsh ){
      rc = transport_ssh_open(pUrlData);
      if( rc==0 ) transport.isOpen = 1;
    }else if( pUrlData->isHttps ){
      #ifdef FOSSIL_ENABLE_SSL
      rc = ssl_open(pUrlData);
      if( rc==0 ) transport.isOpen = 1;
      #else
      socket_set_errmsg("HTTPS: Fossil has been compiled without SSL support");
      rc = 1;
      #endif
    }else if( pUrlData->isFile ){
      sqlite3_uint64 iRandId;
      sqlite3_randomness(sizeof(iRandId), &iRandId);
      transport.zOutFile = mprintf("%s-%llu-out.http",
                                       g.zRepositoryName, iRandId);
      transport.zInFile = mprintf("%s-%llu-in.http",
                                       g.zRepositoryName, iRandId);
      transport.pFile = fossil_fopen(transport.zOutFile, "wb");
      if( transport.pFile==0 ){
        fossil_fatal("cannot output temporary file: %s", transport.zOutFile);
      }
      transport.isOpen = 1;
    }else{
      rc = socket_open(pUrlData);
      if( rc==0 ) transport.isOpen = 1;
    }
  }
  return rc;
}

/*
** Close the current connection
*/
void transport_close(UrlData *pUrlData){
  if( transport.isOpen ){
    free(transport.pBuf);
    transport.pBuf = 0;
    transport.nAlloc = 0;
    transport.nUsed = 0;
    transport.iCursor = 0;
    if( transport.pLog ){
      fclose(transport.pLog);
      transport.pLog = 0;
    }
    if( pUrlData->isSsh ){
      transport_ssh_close();
    }else if( pUrlData->isHttps ){
      #ifdef FOSSIL_ENABLE_SSL
      ssl_close();
      #endif
    }else if( pUrlData->isFile ){
      if( transport.pFile ){
        fclose(transport.pFile);
        transport.pFile = 0;
      }
      file_delete(transport.zInFile);
      file_delete(transport.zOutFile);
      free(transport.zInFile);
      free(transport.zOutFile);
    }else{
      socket_close();
    }
    transport.isOpen = 0;
  }
}

/*
** Send content over the wire.
*/
void transport_send(UrlData *pUrlData, Blob *toSend){
  char *z = blob_buffer(toSend);
  int n = blob_size(toSend);
  transport.nSent += n;
  if( pUrlData->isSsh ){
    fwrite(z, 1, n, sshOut);
    fflush(sshOut);
  }else if( pUrlData->isHttps ){
    #ifdef FOSSIL_ENABLE_SSL
    int sent;
    while( n>0 ){
      sent = ssl_send(0, z, n);
      /* printf("Sent %d of %d bytes\n", sent, n); fflush(stdout); */
      if( sent<=0 ) break;
      n -= sent;
    }
    #endif
  }else if( pUrlData->isFile ){
    fwrite(z, 1, n, transport.pFile);
  }else{
    int sent;
    while( n>0 ){
      sent = socket_send(0, z, n);
      /* printf("Sent %d of %d bytes\n", sent, n); fflush(stdout); */
      if( sent<=0 ) break;
      n -= sent;
    }
  }
}

/*
** This routine is called when the outbound message is complete and
** it is time to being receiving a reply.
*/
void transport_flip(UrlData *pUrlData){
  if( pUrlData->isFile ){
    char *zCmd;
    fclose(transport.pFile);
    zCmd = mprintf("\"%s\" http \"%s\" \"%s\" 127.0.0.1 \"%s\" --localauth",
       g.nameOfExe, transport.zOutFile, transport.zInFile, pUrlData->name
    );
    fossil_system(zCmd);
    free(zCmd);
    transport.pFile = fossil_fopen(transport.zInFile, "rb");
  }
}

/*
** Log all input to a file.  The transport layer will take responsibility
** for closing the log file when it is done.
*/
void transport_log(FILE *pLog){
  if( transport.pLog ){
    fclose(transport.pLog);
    transport.pLog = 0;
  }
  transport.pLog = pLog;
}

/*
** This routine is called when the inbound message has been received
** and it is time to start sending again.
*/
void transport_rewind(UrlData *pUrlData){
  if( pUrlData->isFile ){
    transport_close(pUrlData);
  }
}

/*
** Read N bytes of content directly from the wire and write into
** the buffer.
*/
static int transport_fetch(UrlData *pUrlData, char *zBuf, int N){
  int got;
  if( sshIn ){
    int x;
    int wanted = N;
    got = 0;
    while( wanted>0 ){
      x = read(sshIn, &zBuf[got], wanted);
      if( x<=0 ) break;
      got += x;
      wanted -= x;
    }
  }else if( pUrlData->isHttps ){
    #ifdef FOSSIL_ENABLE_SSL
    got = ssl_receive(0, zBuf, N);
    #else
    got = 0;
    #endif
  }else if( pUrlData->isFile ){
    got = fread(zBuf, 1, N, transport.pFile);
  }else{
    got = socket_receive(0, zBuf, N);
  }
  /* printf("received %d of %d bytes\n", got, N); fflush(stdout); */
  if( transport.pLog ){
    fwrite(zBuf, 1, got, transport.pLog);
    fflush(transport.pLog);
  }
  return got;
}

/*
** Read N bytes of content from the wire and store in the supplied buffer.
** Return the number of bytes actually received.
*/
int transport_receive(UrlData *pUrlData, char *zBuf, int N){
  int onHand;       /* Bytes current held in the transport buffer */
  int nByte = 0;    /* Bytes of content received */

  onHand = transport.nUsed - transport.iCursor;
  if( g.fSshTrace){
    printf("Reading %d bytes with %d on hand...  ", N, onHand);
    fflush(stdout);
  }
  if( onHand>0 ){
    int toMove = onHand;
    if( toMove>N ) toMove = N;
    /* printf("bytes on hand: %d of %d\n", toMove, N); fflush(stdout); */
    memcpy(zBuf, &transport.pBuf[transport.iCursor], toMove);
    transport.iCursor += toMove;
    if( transport.iCursor>=transport.nUsed ){
      transport.nUsed = 0;
      transport.iCursor = 0;
    }
    N -= toMove;
    zBuf += toMove;
    nByte += toMove;
  }
  if( N>0 ){
    int got = transport_fetch(pUrlData, zBuf, N);
    if( got>0 ){
      nByte += got;
      transport.nRcvd += got;
    }
  }
  if( g.fSshTrace ) printf("Got %d bytes\n", nByte);
  return nByte;
}

/*
** Load up to N new bytes of content into the transport.pBuf buffer.
** The buffer itself might be moved.  And the transport.iCursor value
** might be reset to 0.
*/
static void transport_load_buffer(UrlData *pUrlData, int N){
  int i, j;
  if( transport.nAlloc==0 ){
    transport.nAlloc = N;
    transport.pBuf = fossil_malloc( N );
    transport.iCursor = 0;
    transport.nUsed = 0;
  }
  if( transport.iCursor>0 ){
    for(i=0, j=transport.iCursor; j<transport.nUsed; i++, j++){
      transport.pBuf[i] = transport.pBuf[j];
    }
    transport.nUsed -= transport.iCursor;
    transport.iCursor = 0;
  }
  if( transport.nUsed + N > transport.nAlloc ){
    char *pNew;
    transport.nAlloc = transport.nUsed + N;
    pNew = fossil_realloc(transport.pBuf, transport.nAlloc);
    transport.pBuf = pNew;
  }
  if( N>0 ){
    i = transport_fetch(pUrlData, &transport.pBuf[transport.nUsed], N);
    if( i>0 ){
      transport.nRcvd += i;
      transport.nUsed += i;
    }
  }
}

/*
** Fetch a single line of input where a line is all text up to the next
** \n character or until the end of input.  Remove all trailing whitespace
** from the received line and zero-terminate the result.  Return a pointer
** to the line.
**
** Each call to this routine potentially overwrites the returned buffer.
*/
char *transport_receive_line(UrlData *pUrlData){
  int i;
  int iStart;

  i = iStart = transport.iCursor;
  while(1){
    if( i >= transport.nUsed ){
      transport_load_buffer(pUrlData, pUrlData->isSsh ? 2 : 1000);
      i -= iStart;
      iStart = 0;
      if( i >= transport.nUsed ){
        transport.pBuf[i] = 0;
        transport.iCursor = i;
        break;
      }
    }
    if( transport.pBuf[i]=='\n' ){
      transport.iCursor = i+1;
      while( i>=iStart && fossil_isspace(transport.pBuf[i]) ){
        transport.pBuf[i] = 0;
        i--;
      }
      break;
    }
    i++;
  }
  if( g.fSshTrace ) printf("Got line: [%s]\n", &transport.pBuf[iStart]);
  return &transport.pBuf[iStart];
}

/*
** Global transport shutdown
*/
void transport_global_shutdown(UrlData *pUrlData){
  if( pUrlData->isSsh ){
    transport_ssh_close();
  }
  if( pUrlData->isHttps ){
    #ifdef FOSSIL_ENABLE_SSL
    ssl_global_shutdown();
    #endif
  }else{
    socket_global_shutdown();
  }
}

/*
** Close SSH transport.
*/
void transport_ssh_close(void){
  if( sshPid ){
    /*printf("Closing SSH tunnel: ");*/
    fflush(stdout);
    pclose2(sshIn, sshOut, sshPid);
    sshPid = 0;
  }
}
