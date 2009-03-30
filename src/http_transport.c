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
  int isOpen;     /* True when the transport layer is open */
  char *pBuf;     /* Buffer used to hold the reply */
  int nAlloc;     /* Space allocated for transportBuf[] */
  int nUsed ;     /* Space of transportBuf[] used */
  int iCursor;    /* Next unread by in transportBuf[] */
} transport = {
  0, 0, 0, 0
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
  if( g.urlIsHttps ){
    socket_set_errmsg("TLS is not yet implemented");
    rc = 1;
  }else{
    rc = socket_open();
    if( rc==0 ) transport.isOpen = 1;
  }
  return rc;
}

/*
** Close the current connection
*/
void transport_close(void){
  if( transport.isOpen ){
    socket_close();
    free(transport.pBuf);
    transport.pBuf = 0;
    transport.nAlloc = 0;
    transport.nUsed = 0;
    transport.iCursor = 0;
  }
}

/*
** Send content over the wire.
*/
void transport_send(Blob *toSend){
  char *z = blob_buffer(toSend);
  int n = blob_size(toSend);
  int sent;
  while( n>0 ){
    sent = socket_send(0, z, n);
    if( sent<=0 ) break;
    n -= sent;
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
    int got = socket_receive(0, zBuf, N);
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
  return &transport.pBuf[iStart];
}
