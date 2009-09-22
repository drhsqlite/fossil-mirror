/*
** Copyright (c) 2008 D. Richard Hipp
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
** This file implements a very simple (and low-performance) HTTP server
** for windows.
*/
#ifdef __MINGW32__           /* This code is for win32 only */
#include "config.h"
#include "winhttp.h"
#include <windows.h>

/*
** The HttpRequest structure holds information about each incoming
** HTTP request.
*/
typedef struct HttpRequest HttpRequest;
struct HttpRequest {
  int id;             /* ID counter */
  SOCKET s;           /* Socket on which to receive data */
  SOCKADDR_IN addr;   /* Address from which data is coming */
};

/*
** Prefix for a temporary file.
*/
static char *zTempPrefix;

/*
** Look at the HTTP header contained in zHdr.  Find the content
** length and return it.  Return 0 if there is no Content-Length:
** header line.
*/
static int find_content_length(const char *zHdr){
  while( *zHdr ){
    if( zHdr[0]=='\n' ){
      if( zHdr[1]=='\r' ) return 0;
      if( strncasecmp(&zHdr[1], "content-length:", 15)==0 ){
        return atoi(&zHdr[17]);
      }
    }
    zHdr++;
  }
  return 0;
}

/*
** Process a single incoming HTTP request.
*/
void win32_process_one_http_request(void *pAppData){
  HttpRequest *p = (HttpRequest*)pAppData;
  FILE *in = 0, *out = 0;
  int amt, got;
  int wanted = 0;
  char *z;
  char zRequestFName[100];
  char zReplyFName[100];
  char zCmd[2000];          /* Command-line to process the request */
  char zHdr[2000];          /* The HTTP request header */

  sprintf(zRequestFName, "%s_in%d.txt", zTempPrefix, p->id);
  sprintf(zReplyFName, "%s_out%d.txt", zTempPrefix, p->id);
  amt = 0;
  while( amt<sizeof(zHdr) ){
    got = recv(p->s, &zHdr[amt], sizeof(zHdr)-1-amt, 0);
    if( got==SOCKET_ERROR ) goto end_request;
    if( got==0 ){
      wanted = 0;
      break;
    }
    amt += got;
    zHdr[amt] = 0;
    z = strstr(zHdr, "\r\n\r\n");
    if( z ){
      wanted = find_content_length(zHdr) + (&z[4]-zHdr) - amt;
      break;
    }
  }
  if( amt>=sizeof(zHdr) ) goto end_request;
  out = fopen(zRequestFName, "wb");
  if( out==0 ) goto end_request;
  fwrite(zHdr, 1, amt, out);
  while( wanted>0 ){
    got = recv(p->s, zHdr, sizeof(zHdr), 0);
    if( got==SOCKET_ERROR ) goto end_request;
    if( got ){
      fwrite(zHdr, 1, got, out);
    }else{
      break;
    }
    wanted -= got;
  }
  fclose(out);
  out = 0;
  sprintf(zCmd, "\"%s\" http \"%s\" %s %s %s",
    g.argv[0], g.zRepositoryName, zRequestFName, zReplyFName, 
    inet_ntoa(p->addr.sin_addr)
  );
  portable_system(zCmd);
  in = fopen(zReplyFName, "rb");
  if( in ){
    while( (got = fread(zHdr, 1, sizeof(zHdr), in))>0 ){
      send(p->s, zHdr, got, 0);
    }
  }

end_request:
  if( out ) fclose(out);
  if( in ) fclose(in);
  closesocket(p->s);
  unlink(zRequestFName);
  unlink(zReplyFName);
  free(p);
}

/*
** Start a listening socket and process incoming HTTP requests on
** that socket.
*/
void win32_http_server(int mnPort, int mxPort, char *zBrowser){
  WSADATA wd;
  SOCKET s;
  SOCKADDR_IN addr;
  int idCnt = 0;
  int iPort = mnPort;

  if( WSAStartup(MAKEWORD(1,1), &wd) ){
    fossil_fatal("unable to initialize winsock");
  }
  while( iPort<=mxPort ){
    s = socket(AF_INET, SOCK_STREAM, 0);
    if( s==INVALID_SOCKET ){
      fossil_fatal("unable to create a socket");
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(iPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if( bind(s, (struct sockaddr*)&addr, sizeof(addr))==SOCKET_ERROR ){
      closesocket(s);
      iPort++;
      continue;
    }
    if( listen(s, SOMAXCONN)==SOCKET_ERROR ){
      closesocket(s);
      iPort++;
      continue;
    }
    break;
  }
  if( iPort>mxPort ){
    if( mnPort==mxPort ){
      fossil_fatal("unable to open listening socket on ports %d", mnPort);
    }else{
      fossil_fatal("unable to open listening socket on any"
                   " port in the range %d..%d", mnPort, mxPort);
    }
  }
  zTempPrefix = mprintf("fossil_server_P%d_", iPort);
  printf("Listening for HTTP requests on TCP port %d\n", iPort);
  if( zBrowser ){
    zBrowser = mprintf(zBrowser, iPort);
    printf("Launch webbrowser: %s\n", zBrowser);
    portable_system(zBrowser);
  }
  printf("Type Ctrl-C to stop the HTTP server\n");
  for(;;){
    SOCKET client;
    SOCKADDR_IN client_addr;
    HttpRequest *p;
    int len = sizeof(client_addr);

    client = accept(s, (struct sockaddr*)&client_addr, &len);
    if( client==INVALID_SOCKET ){
      closesocket(s);
      fossil_fatal("error from accept()");
    }
    p = malloc( sizeof(*p) );
    if( p==0 ){
      fossil_fatal("out of memory");
    }
    p->id = ++idCnt;
    p->s = client;
    p->addr = client_addr;
    _beginthread(win32_process_one_http_request, 0, (void*)p);
  }
  closesocket(s);
  WSACleanup();
}

#endif /* __MINGW32__  -- This code is for win32 only */
