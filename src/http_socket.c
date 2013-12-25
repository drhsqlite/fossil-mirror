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
int socket_open(UrlData *pUrlData){
  int error = 0;
#ifdef HAVE_GETADDRINFO
  struct addrinfo hints;
  struct addrinfo* res;
  struct addrinfo* i;
  char ip[INET6_ADDRSTRLEN];
  void* addr;
  char* sPort;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_flags = AI_ADDRCONFIG;
#ifdef WITH_IPV6
  hints.ai_family = PF_UNSPEC;
#else
  hints.ai_family = PF_INET;
#endif
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  sPort = mprintf("%d", g.urlPort);

  if(getaddrinfo(g.urlName, sPort, &hints, &res)) {
    socket_set_errmsg("can't resolve host name: %s", g.urlName);
    free(sPort);
    return 1;
  }
  for(i = res; i; i = i->ai_next) {
    iSocket = socket(i->ai_family, i->ai_socktype, i->ai_protocol);
    if(iSocket < 0) {
      continue;
    }
    if(connect(iSocket, i->ai_addr, i->ai_addrlen) < 0) {
      close(iSocket);
      iSocket = -1;
      continue;
    }
    if(!getnameinfo(i->ai_addr, i->ai_addrlen, ip, sizeof(ip),
                    NULL, 0, NI_NUMERICHOST))
        g.zIpAddr = mprintf("%s", ip);
    break;
  }
  if(iSocket == -1) {
    socket_set_errmsg("cannot connect to host %s:%s", g.urlName, sPort);
    error = 1;
  }
  free(sPort);
  freeaddrinfo(res);
#else
  static struct sockaddr_in addr;  /* The server address */
  static int addrIsInit = 0;       /* True once addr is initialized */

  socket_global_init();
  if( !addrIsInit ){
    addr.sin_family = AF_INET;
    addr.sin_port = htons(pUrlData->port);
    *(int*)&addr.sin_addr = inet_addr(pUrlData->name);
    if( -1 == *(int*)&addr.sin_addr ){
#ifndef FOSSIL_STATIC_LINK
      struct hostent *pHost;
      pHost = gethostbyname(pUrlData->name);
      if( pHost!=0 ){
        memcpy(&addr.sin_addr,pHost->h_addr_list[0],pHost->h_length);
      }else
#endif
      {
        socket_set_errmsg("can't resolve host name: %s", pUrlData->name);
        return 1;
      }
    }
    addrIsInit = 1;

    /* Set the Global.zIpAddr variable to the server we are talking to.
    ** This is used to populate the ipaddr column of the rcvfrom table,
    ** if any files are received from the server.
    */
    g.zIpAddr = mprintf("%s", inet_ntoa(addr.sin_addr));
  }
  iSocket = socket(AF_INET,SOCK_STREAM,0);
  if( iSocket<0 ){
    socket_set_errmsg("cannot create a socket");
    return 1;
  }
  if( connect(iSocket,(struct sockaddr*)&addr,sizeof(addr))<0 ){
    socket_set_errmsg("cannot connect to host %s:%d", pUrlData->name,
                      pUrlData->port);
    socket_close();
    error = 1;
  }
#endif
#if !defined(_WIN32)
  if(!error)
    signal(SIGPIPE, SIG_IGN);
#endif
  return error;
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
** Attempt to resolve g.urlName to IP and setup g.zIpAddr so rcvfrom gets
** populated. For hostnames with more than one IP (or if overridden in
** ~/.ssh/config) the rcvfrom may not match the host to which we connect.
*/
void socket_ssh_resolve_addr(UrlData *pUrlData){
  struct hostent *pHost;        /* Used to make best effort for rcvfrom */
  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(addr));
  pHost = gethostbyname(pUrlData->name);
  if( pHost!=0 ){
    memcpy(&addr.sin_addr,pHost->h_addr_list[0],pHost->h_length);
    g.zIpAddr = mprintf("%s", inet_ntoa(addr.sin_addr));
  }
}
