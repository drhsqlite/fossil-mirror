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
# if defined(_WIN32_WINNT)
#  undef _WIN32_WINNT
# endif
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
#  include <errno.h>
#endif
#include <assert.h>
#include <sys/types.h>
#include <signal.h>

#if defined(__EMX__) || defined(__morphos__)
  typedef int socklen_t;
#endif

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
char *socket_errmsg(void){
  char *zResult = socketErrMsg;
  socketErrMsg = 0;
  return zResult;
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
    if( shutdown(iSocket,1)==0 ) shutdown(iSocket,0);
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
**    pUrlData->name       Name of the server.  Ex: fossil-scm.org
**    pUrlData->port       TCP/IP port to use.  Ex: 80
**
** Return the number of errors.
*/
int socket_open(UrlData *pUrlData){
  int rc = 0;
#ifdef AF_INET6
  struct addrinfo *ai = 0;
  struct addrinfo *p;
  struct addrinfo hints;
  char zPort[30];
  char zRemote[NI_MAXHOST];
#else
  struct hostent *hostent;
  struct sockaddr_in sa;
  socklen_t size = sizeof(sa);
  int i;
#endif

  socket_global_init();
  socket_close();
#ifdef AF_INET6
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
#else
  hostent = gethostbyname(pUrlData->name);
  if ( !hostent ){
    socket_set_errmsg("gethostbyname() failed: %s", strerror(errno));
    goto end_socket_open;
  }
  iSocket = socket(AF_INET, SOCK_STREAM, 0);
  if( iSocket< 0 ){
    socket_set_errmsg("socket() failed: %s", strerror(errno));
    goto end_socket_open;
  }
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(pUrlData->port);
  for (i=0; hostent->h_addr_list[i]!=0; i++){
    sa.sin_addr = *(struct in_addr *)hostent->h_addr_list[i];
    if( connect(iSocket,(struct sockaddr *)&sa,size)<0 ){
      sa.sin_addr.s_addr = 0;
      continue;
    }
    g.zIpAddr = mprintf("%s", inet_ntoa(sa.sin_addr));
  }
  if( sa.sin_addr.s_addr==0 ){
    socket_set_errmsg("cannot connect to host %s:%d", pUrlData->name,
                      pUrlData->port);
    rc = 1;
  }
#endif
#if !defined(_WIN32)
  signal(SIGPIPE, SIG_IGN);
#endif
end_socket_open:
  if( rc && iSocket>=0 ) socket_close();
#ifdef AF_INET6
  if( ai ) freeaddrinfo(ai);
#endif
  return rc;
}

/*
** Send content out over the open socket connection.
*/
size_t socket_send(void *NotUsed, const void *pContent, size_t N){
  ssize_t sent;
  size_t total = 0;
  while( N>0 ){
    sent = send(iSocket, pContent, N, 0);
    if( sent<=0 ) break;
    total += (size_t)sent;
    N -= (size_t)sent;
    pContent = (void*)&((char*)pContent)[sent];
  }
  return total;
}

/*
** Receive content back from the open socket connection.
** Return the number of bytes read.
**
** When bDontBlock is false, this function blocks until all N bytes
** have been read.
*/
size_t socket_receive(void *NotUsed, void *pContent, size_t N, int bDontBlock){
  ssize_t got;
  size_t total = 0;
  int flags = 0;
#ifdef MSG_DONTWAIT
  if( bDontBlock ) flags |= MSG_DONTWAIT;
#endif
  while( N>0 ){
    /* WinXP fails for large values of N.  So limit it to 64KiB. */
    got = recv(iSocket, pContent, N>65536 ? 65536 : N, flags);
    if( got<=0 ) break;
    total += (size_t)got;
    N -= (size_t)got;
    pContent = (void*)&((char*)pContent)[got];
  }
  return total;
}
