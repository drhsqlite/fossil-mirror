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
** This file manages low-level client socket communications.  The socket
** might be for a simple HTTP request or for an encrypted HTTPS request.
**
** This file implements a singleton.  A single client socket may be active
** at a time.  State information is stored in static variables.  The identity
** of the server is held in global variables that are set by url_parse().
**
** Low-level sockets are abstracted out into this module because they 
** are handled different on Unix and windows.
*/

#include "config.h"
#include "http_socket.h"
#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/socket.h>
#  include <netdb.h>
#endif
#include <assert.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

/*
** There can only be a single socket connection open at a time.
** State information about that socket is stored in the following
** local variables:
*/
static int socketIsInit = 0;    /* True after global initialization */
#if defined(_WIN32)
static WSADATA socketInfo;      /* Windows socket initialize data */
#endif
static int iSocket = -1;        /* The socket on which we talk to the server */
static char *socketErrMsg = 0;  /* Text of most recent socket error */


/*
** Clear the socket error message
*/
static void socket_clear_errmsg(void){
  free(socketErrMsg);
  socketErrMsg = 0;
}

/*
** Set the socket error message.
*/
void socket_set_errmsg(char *zFormat, ...){
  va_list ap;
  socket_clear_errmsg();
  va_start(ap, zFormat);
  socketErrMsg = vmprintf(zFormat, ap);
  va_end(ap);
}

/*
** Return the current socket error message
*/
const char *socket_errmsg(void){
  return socketErrMsg;
}

/*
** Call this routine once before any other use of the socket interface.
** This routine does initial configuration of the socket module.
*/
void socket_global_init(void){
  if( socketIsInit==0 ){
#if defined(_WIN32)
    if( WSAStartup(MAKEWORD(2,0), &socketInfo)!=0 ){
      fossil_panic("can't initialize winsock");
    }
#endif
    socketIsInit = 1;
  }
}

/*
** Call this routine to shutdown the socket module prior to program
** exit.
*/
void socket_global_shutdown(void){
  if( socketIsInit ){
#if defined(_WIN32)
    WSACleanup();
#endif
    socket_clear_errmsg();
    socketIsInit = 0;
  }
}

/*
** Close the currently open socket.  If no socket is open, this routine
** is a no-op.
*/
void socket_close(void){
  if( iSocket>=0 ){
#if defined(_WIN32)
    closesocket(iSocket);
#else
    close(iSocket);
#endif
    iSocket = -1;
  }
}

/*
** Open a socket connection.  The identify of the server is determined
** by global variables that are set using url_parse():
**
**    g.urlName       Name of the server.  Ex: www.fossil-scm.org
**    g.urlPort       TCP/IP port to use.  Ex: 80
**
** Return the number of errors.
*/
int socket_open(void){
  static struct sockaddr_in addr;  /* The server address */
  static int addrIsInit = 0;       /* True when initialized once */
  static struct addrinfo *p = 0;   /* Succcessful open */

  socket_global_init();
  if( !addrIsInit ){
    struct addrinfo sHints;
    int rc;
    char zPort[30];
    
    memset(&sHints, 0, sizeof(sHints));
    sHints.ai_family = AF_UNSPEC;
    sHints.ai_socktype = SOCK_STREAM;
    sHints.ai_flags = 0;
    sHints.ai_protocol = 0;
    sqlite3_snprintf(sizeof(zPort), zPort, "%d", g.urlPort);
    rc = getaddrinfo(g.urlName, zPort, &sHints, &p);
    if( rc!=0 ){
      fossil_fatal("getaddrinfo(\"%s\",\"%s\",...): %s",
                   g.urlName, zPort, gai_strerror(rc));
    }
    if( p==0 ){
      fossil_fatal("no IP addresses returned by getaddrinfo()");
    }
    addrIsInit = 1;
  }

  while( p ){
    char zHost[NI_MAXHOST];
    iSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if( iSocket<0 ){
      fossil_warning("socket() failed: %s", strerror(errno));
      p = p->ai_next;
      continue;
    }
    if( connect(iSocket, p->ai_addr, p->ai_addrlen)<0 ){
      fossil_warning("connect() failed: %s", strerror(errno));
      p = p->ai_next;
      socket_close();
      continue;
    }
    p->ai_next = 0;
    if( getnameinfo(p->ai_addr, p->ai_addrlen, zHost, sizeof(zHost),
                    0, 0, NI_NUMERICHOST)==0 ){
      g.zIpAddr = mprintf("%s", zHost);
    }else{
      fossil_fatal("cannot find numeric host IP address");
    }
    break;
  }
  if( p==0 ){
    socket_set_errmsg("cannot create a socket");
    return 1;
  }
#if !defined(_WIN32)
  signal(SIGPIPE, SIG_IGN);
#endif
  return 0;
}

/*
** Send content out over the open socket connection.
*/
size_t socket_send(void *NotUsed, void *pContent, size_t N){
  size_t sent;
  size_t total = 0;
  while( N>0 ){
    sent = send(iSocket, pContent, N, 0);
    if( sent<=0 ) break;
    total += sent;
    N -= sent;
    pContent = (void*)&((char*)pContent)[sent];
  }
  return total;
}

/*
** Receive content back from the open socket connection.
*/
size_t socket_receive(void *NotUsed, void *pContent, size_t N){
  ssize_t got;
  size_t total = 0;
  while( N>0 ){
    got = recv(iSocket, pContent, N, 0);
    if( got<=0 ) break;
    total += (size_t)got;
    N -= (size_t)got;
    pContent = (void*)&((char*)pContent)[got];
  }
  return total;
}
