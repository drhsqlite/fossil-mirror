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
#if defined(_WIN32)
# define _WIN32_WINNT 0x501
#endif
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1  /* IPv6 won't compile on Solaris without this */
#endif
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
void socket_set_errmsg(const char *zFormat, ...){
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
** by pUrlData
**
**    pUrlDAta->name       Name of the server.  Ex: www.fossil-scm.org
**    pUrlDAta->port       TCP/IP port to use.  Ex: 80
**
** Return the number of errors.
*/
int socket_open(UrlData *pUrlData){
  int rc = 0;
  struct addrinfo *ai = 0;
  struct addrinfo *p;
  struct addrinfo hints;
  char zPort[30];
  char zRemote[NI_MAXHOST];

  socket_global_init();
  socket_close();
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = g.fIPv4 ? AF_INET : AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  sqlite3_snprintf(sizeof(zPort),zPort,"%d", pUrlData->port);
  rc = getaddrinfo(pUrlData->name, zPort, &hints, &ai);
  if( rc ){
    socket_set_errmsg("getaddrinfo() fails: %s", gai_strerror(rc));
    goto end_socket_open;
  }
  for(p=ai; p; p=p->ai_next){
    iSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if( iSocket<0 ) continue;
    if( connect(iSocket,p->ai_addr,p->ai_addrlen)<0 ){
      socket_close();
      continue;
    }
    rc = getnameinfo(p->ai_addr, p->ai_addrlen, zRemote, sizeof(zRemote),
                     0, 0, NI_NUMERICHOST);
    if( rc ){
      socket_set_errmsg("getnameinfo() failed: %s", gai_strerror(rc));
      goto end_socket_open;
    }
    g.zIpAddr = mprintf("%s", zRemote);
    break;
  }
  if( p==0 ){
    socket_set_errmsg("cannot connect to host %s:%d", pUrlData->name,
                      pUrlData->port);
    rc = 1;
  }
#if !defined(_WIN32)
  signal(SIGPIPE, SIG_IGN);
#endif
end_socket_open:
  if( rc && iSocket>=0 ) socket_close();
  if( ai ) freeaddrinfo(ai);
  return rc;
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
    /* WinXP fails for large values of N.  So limit it to 64KiB. */
    got = recv(iSocket, pContent, N>65536 ? 65536 : N, 0);
    if( got<=0 ) break;
    total += (size_t)got;
    N -= (size_t)got;
    pContent = (void*)&((char*)pContent)[got];
  }
  return total;
}

/*
** Attempt to resolve pUrlData->name to an IP address and setup g.zIpAddr
** so rcvfrom gets populated. For hostnames with more than one IP (or
** if overridden in ~/.ssh/config) the rcvfrom may not match the host
** to which we connect.
*/
void socket_ssh_resolve_addr(UrlData *pUrlData){
  struct addrinfo *ai = 0;
  struct addrinfo hints;
  char zRemote[NI_MAXHOST];
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  if( getaddrinfo(pUrlData->name, NULL, &hints, &ai)==0
   && ai!=0
   && getnameinfo(ai->ai_addr, ai->ai_addrlen, zRemote,
                  sizeof(zRemote), 0, 0, NI_NUMERICHOST)==0 ){
    g.zIpAddr = mprintf("%s (%s)", zRemote, pUrlData->name);
  }
  if( ai ) freeaddrinfo(ai);
  if( g.zIpAddr==0 ){
    g.zIpAddr = mprintf("%s", pUrlData->name);
  }
}
