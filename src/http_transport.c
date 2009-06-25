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
  FILE *pFile;            /* File I/O for FILE: */
  char *zOutFile;         /* Name of outbound file for FILE: */
  char *zInFile;          /* Name of inbound file for FILE: */
} transport = {
  0, 0, 0, 0, 0
};

/*
** Return the current transport error message.
*/
const char *transport_errmsg(void){
  return socket_errmsg();
}

/*
** Open a connection to the server.  The server is defined by the following
** global variables:
**
**   g.urlName        Name of the server.  Ex: www.fossil-scm.org
**   g.urlPort        TCP/IP port.  Ex: 80
**   g.urlIsHttps     Use TLS for the connection
**
** Return the number of errors.
*/
int transport_open(void){
  int rc = 0;
  if( transport.isOpen==0 ){
    if( g.urlIsHttps ){
      socket_set_errmsg("HTTPS: is not yet implemented");
      rc = 1;
    }else if( g.urlIsFile ){
      sqlite3_uint64 iRandId;
      sqlite3_randomness(sizeof(iRandId), &iRandId);
      transport.zOutFile = mprintf("%s-%llu-out.http", 
                                       g.zRepositoryName, iRandId);
      transport.zInFile = mprintf("%s-%llu-in.http", 
                                       g.zRepositoryName, iRandId);
      transport.pFile = fopen(transport.zOutFile, "wb");
      if( transport.pFile==0 ){
        fossil_fatal("cannot output temporary file: %s", transport.zOutFile);
      }
      transport.isOpen = 1;
    }else{
      rc = socket_open();
      if( rc==0 ) transport.isOpen = 1;
    }
  }
  return rc;
}

/*
** Close the current connection
*/
void transport_close(void){
  if( transport.isOpen ){
    free(transport.pBuf);
    transport.pBuf = 0;
    transport.nAlloc = 0;
    transport.nUsed = 0;
    transport.iCursor = 0;
    if( g.urlIsHttps ){
      /* TBD */
    }else if( g.urlIsFile ){
      if( transport.pFile ){ 
        fclose(transport.pFile);
        transport.pFile = 0;
      }
      unlink(transport.zInFile);
      unlink(transport.zOutFile);
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
void transport_send(Blob *toSend){
  char *z = blob_buffer(toSend);
  int n = blob_size(toSend);
  if( g.urlIsHttps ){
    /* TBD */
  }else if( g.urlIsFile ){
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
** it is time to being recieving a reply.
*/
void transport_flip(void){
  if( g.urlIsFile ){
    char *zCmd;
    fclose(transport.pFile);
    zCmd = mprintf("\"%s\" http \"%s\" \"%s\" \"%s\" 127.0.0.1",
       g.argv[0], g.urlName, transport.zOutFile, transport.zInFile
    );
#ifdef __MINGW32__
    /* Quote the entire command to work around goofiness in system() on
    ** on windows. Ticket [40df1ced7e] */
    zCmd = mprintf("\"%z\"", zCmd);
#endif
    system(zCmd);
    free(zCmd);
    transport.pFile = fopen(transport.zInFile, "rb");
  }
}

/*
** This routine is called when the inbound message has been received
** and it is time to start sending again.
*/
void transport_rewind(void){
  if( g.urlIsFile ){
    transport_close();
  }
}

/*
** Read N bytes of content from the wire and store in the supplied buffer.
** Return the number of bytes actually received.
*/
int transport_receive(char *zBuf, int N){
  int onHand;       /* Bytes current held in the transport buffer */
  int nByte = 0;    /* Bytes of content received */

  onHand = transport.nUsed - transport.iCursor;
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
    int got;
    if( g.urlIsHttps ){
      /* TBD */
      got = 0;
    }else if( g.urlIsFile ){
      got = fread(zBuf, 1, N, transport.pFile);
    }else{
      got = socket_receive(0, zBuf, N);
      /* printf("received %d of %d bytes\n", got, N); fflush(stdout); */
    }
    if( got>0 ){
      nByte += got; 
    }
  }
  return nByte;
}

/*
** Load up to N new bytes of content into the transport.pBuf buffer.
** The buffer itself might be moved.  And the transport.iCursor value
** might be reset to 0.
*/
static void transport_load_buffer(int N){
  int i, j;
  if( transport.nAlloc==0 ){
    transport.nAlloc = N;
    transport.pBuf = malloc( N );
    if( transport.pBuf==0 ) fossil_panic("out of memory");
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
    pNew = realloc(transport.pBuf, transport.nAlloc);
    if( pNew==0 ) fossil_panic("out of memory");
    transport.pBuf = pNew;
  }
  if( N>0 ){
    i = transport_receive(&transport.pBuf[transport.nUsed], N);
    if( i>0 ){
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
char *transport_receive_line(void){
  int i;
  int iStart;

  i = iStart = transport.iCursor;
  while(1){
    if( i >= transport.nUsed ){
      transport_load_buffer(1000);
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
      while( i>=iStart && isspace(transport.pBuf[i]) ){
        transport.pBuf[i] = 0;
        i--;
      }
      break;
    }
    i++;
  }
  /* printf("Got line: [%s]\n", &transport.pBuf[iStart]); */
  return &transport.pBuf[iStart];
}
