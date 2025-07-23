/*
** Copyright (c) 2008 D. Richard Hipp
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
** This file implements a very simple (and low-performance) HTTP server
** for windows. It also implements a Windows Service which allows the HTTP
** server to be run without any user logged on.
*/
#include "config.h"
#ifdef _WIN32
/* This code is for win32 only */
# if !defined(_WIN32_WINNT)
#  define _WIN32_WINNT 0x0501
# endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include "winhttp.h"

#ifndef IPV6_V6ONLY
# define IPV6_V6ONLY 27  /* Because this definition is missing in MinGW */
#endif

/*
** The SocketAddr structure holds a SOCKADDR_STORAGE and its content size.
*/
typedef struct SocketAddr SocketAddr;
struct SocketAddr {
  SOCKADDR_STORAGE addr;
  int len;
};

static char* SocketAddr_toString(const SocketAddr* pAddr){
  SocketAddr addr;
  char* zIp;
  DWORD nIp = 50;
  assert( pAddr!=NULL );
  memcpy(&addr, pAddr, sizeof(SocketAddr));
  if( addr.len==sizeof(SOCKADDR_IN6) ){
    ((SOCKADDR_IN6*)&addr)->sin6_port = 0;
  }else{
    ((SOCKADDR_IN*)&addr)->sin_port = 0;
  }
  zIp = fossil_malloc(nIp);
  if( WSAAddressToStringA((SOCKADDR*)&addr, addr.len, NULL, zIp, &nIp)!=0 ){
    zIp[0] = 0;
  }
  return zIp;
}

/*
** The DualAddr structure holds two SocketAddr (one IPv4 and on IPv6).
*/
typedef struct DualAddr DualAddr;
struct DualAddr {
  SocketAddr a4;  /* IPv4 SOCKADDR_IN */
  SocketAddr a6;  /* IPv6 SOCKADDR_IN6 */
};

static void DualAddr_init(DualAddr* pDA){
  assert( pDA!=NULL );
  memset(pDA, 0, sizeof(DualAddr));
  pDA->a4.len = sizeof(SOCKADDR_IN);
  pDA->a6.len = sizeof(SOCKADDR_IN6);
}

/*
** The DualSocket structure holds two SOCKETs. One or both could be
** used or INVALID_SOCKET.  One is dedicated to IPv4, the other to IPv6.
*/
typedef struct DualSocket DualSocket;
struct DualSocket {
  SOCKET s4;    /* IPv4 socket or INVALID_SOCKET */
  SOCKET s6;    /* IPv6 socket or INVALID_SOCKET */
};

/*
** Initializes a DualSocket.
*/
static void DualSocket_init(DualSocket* ds){
  assert( ds!=NULL );
  ds->s4 = INVALID_SOCKET;
  ds->s6 = INVALID_SOCKET;
};

/*
** Close and reset a DualSocket.
*/
static void DualSocket_close(DualSocket* ds){
  assert( ds!=NULL );
  if( ds->s4!=INVALID_SOCKET ){
    closesocket(ds->s4);
    ds->s4 = INVALID_SOCKET;
  }
  if( ds->s6!=INVALID_SOCKET ){
    closesocket(ds->s6);
    ds->s6 = INVALID_SOCKET;
  }
};

/*
** When ip is "W", listen to wildcard address (IPv4/IPv6 as available).
** When ip is "L", listen to loopback address (IPv4/IPv6 as available).
** Else listen only the specified ip, which is either IPv4 or IPv6 or invalid.
** Returns 1 on success, 0 on failure.
*/
static int DualSocket_listen(DualSocket* ds, const char* zIp, int iPort){
  SOCKADDR_IN addr4;
  SOCKADDR_IN6 addr6;
  assert( ds!=NULL && zIp!=NULL && iPort!=0 );
  DualSocket_close(ds);
  memset(&addr4, 0, sizeof(addr4));
  memset(&addr6, 0, sizeof(addr6));
  if (strcmp(zIp, "W")==0 || strcmp(zIp, "L")==0 ){
    ds->s4 = socket(AF_INET, SOCK_STREAM, 0);
    ds->s6 = socket(AF_INET6, SOCK_STREAM, 0);
    if( ds->s4==INVALID_SOCKET && ds->s6==INVALID_SOCKET ){
      return 0;
    }
    if (ds->s4!=INVALID_SOCKET ) {
      addr4.sin_family = AF_INET;
      addr4.sin_port = htons(iPort);
      if( strcmp(zIp, "L")==0 ){
        addr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      }else{
        addr4.sin_addr.s_addr = INADDR_ANY;
      }
    }
    if( ds->s6!=INVALID_SOCKET ) {
      DWORD ipv6only = 1; /* don't want a dual-stack socket */
      setsockopt(ds->s6, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only,
                 sizeof(ipv6only));
      addr6.sin6_family = AF_INET6;
      addr6.sin6_port = htons(iPort);
      if( strcmp(zIp, "L")==0 ){
        memcpy(&addr6.sin6_addr, &in6addr_loopback, sizeof(in6addr_loopback));
      }else{
        memcpy(&addr6.sin6_addr, &in6addr_any, sizeof(in6addr_any));
      }
    }
  }else{
    if( strstr(zIp, ".") ){
      int addrlen = sizeof(addr4);
      ds->s4 = socket(AF_INET, SOCK_STREAM, 0);
      if( ds->s4==INVALID_SOCKET ){
        return 0;
      }
      addr4.sin_family = AF_INET;
      if (WSAStringToAddress((char*)zIp, AF_INET, NULL,
                             (struct sockaddr *)&addr4, &addrlen) != 0){
        return 0;
      }
      addr4.sin_port = htons(iPort);
    }else{
      DWORD ipv6only = 1; /* don't want a dual-stack socket */
      int addrlen = sizeof(addr6);
      ds->s6 = socket(AF_INET6, SOCK_STREAM, 0);
      if( ds->s6==INVALID_SOCKET ){
        return 0;
      }
      setsockopt(ds->s6, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only,
                 sizeof(ipv6only));
      addr6.sin6_family = AF_INET6;
      if (WSAStringToAddress((char*)zIp, AF_INET6, NULL,
                             (struct sockaddr *)&addr6, &addrlen) != 0){
        return 0;
      }
      addr6.sin6_port = htons(iPort);
    }
  }
  assert( ds->s4!=INVALID_SOCKET || ds->s6!=INVALID_SOCKET );
  if( ds->s4!=INVALID_SOCKET && bind(ds->s4, (struct sockaddr*)&addr4,
                                 sizeof(addr4))==SOCKET_ERROR ){
    return 0;
  }
  if( ds->s6!=INVALID_SOCKET && bind(ds->s6, (struct sockaddr*)&addr6,
                                 sizeof(addr6))==SOCKET_ERROR ){
    return 0;
  }
  if( ds->s4!=INVALID_SOCKET && listen(ds->s4, SOMAXCONN)==SOCKET_ERROR ){
    return 0;
  }
  if( ds->s6!=INVALID_SOCKET && listen(ds->s6, SOMAXCONN)==SOCKET_ERROR ){
    return 0;
  }
  return 1;
};

/*
** Accepts connections on DualSocket.
*/
static void DualSocket_accept(DualSocket* pListen, DualSocket* pClient,
                              DualAddr* pClientAddr){
  fd_set rs;
  int rs_count = 0;
  assert( pListen!=NULL && pClient!=NULL && pClientAddr!= NULL );
  DualSocket_init(pClient);
  DualAddr_init(pClientAddr);
  FD_ZERO(&rs);
  if( pListen->s4!=INVALID_SOCKET ){
    FD_SET(pListen->s4, &rs);
    ++rs_count;
  }
  if( pListen->s6!=INVALID_SOCKET ){
    FD_SET(pListen->s6, &rs);
    ++rs_count;
  }
  if( select(rs_count, &rs, 0, 0, 0 /*blocking*/)==SOCKET_ERROR ){
    return;
  }
  if( FD_ISSET(pListen->s4, &rs) ){
    pClient->s4 = accept(pListen->s4, (struct sockaddr*)&pClientAddr->a4.addr,
                         &pClientAddr->a4.len);
  }
  if( FD_ISSET(pListen->s6, &rs) ){
    pClient->s6 = accept(pListen->s6, (struct sockaddr*)&pClientAddr->a6.addr,
                         &pClientAddr->a6.len);
  }
}

/*
** The HttpServer structure holds information about an instance of
** the HTTP server itself.
*/
typedef struct HttpServer HttpServer;
struct HttpServer {
  HANDLE hStoppedEvent; /* Event to signal when server is stopped,
                        ** must be closed by callee. */
  char *zStopper;       /* The stopper file name, must be freed by
                        ** callee. */
  DualSocket listener;  /* Sockets on which the server is listening,
                        ** may be closed by callee. */
};

/*
** The HttpRequest structure holds information about each incoming
** HTTP request.
*/
typedef struct HttpRequest HttpRequest;
struct HttpRequest {
  int id;                /* ID counter */
  SOCKET s;              /* Socket on which to receive data */
  SocketAddr addr;       /* Address from which data is coming */
  int flags;             /* Flags passed to win32_http_server() */
  const char *zOptions;  /* --baseurl, --notfound, --localauth, --th-trace */
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
      if( fossil_strnicmp(&zHdr[1], "content-length:", 15)==0 ){
        return atoi(&zHdr[17]);
      }
    }
    zHdr++;
  }
  return 0;
}

/*
** Issue a fatal error.
*/
static NORETURN void winhttp_fatal(
  const char *zOp,
  const char *zService,
  const char *zErr
){
  fossil_fatal("unable to %s service '%s': %s", zOp, zService, zErr);
}

/*
** Make sure the server stops as soon as possible after the stopper file
** is found.  If there is no stopper file name, do nothing.
*/
static void win32_server_stopper(void *pAppData){
  HttpServer *p = (HttpServer*)pAppData;
  if( p!=0 ){
    HANDLE hStoppedEvent = p->hStoppedEvent;
    const char *zStopper = p->zStopper;
    if( hStoppedEvent!=NULL && zStopper!=0 ){
      while( 1 ){
        DWORD dwResult = WaitForMultipleObjectsEx(1, &hStoppedEvent, FALSE,
                                                  1000, TRUE);
        if( dwResult!=WAIT_IO_COMPLETION && dwResult!=WAIT_TIMEOUT ){
          /* The event is either invalid, signaled, or abandoned.  Bail
          ** out now because those conditions should indicate the parent
          ** thread is dead or dying. */
          break;
        }
        if( file_size(zStopper, ExtFILE)>=0 ){
          /* The stopper file has been found.  Attempt to close the server
          ** listener socket now and then exit. */
          DualSocket_close(&p->listener);
          break;
        }
      }
    }
    if( hStoppedEvent!=NULL ){
      CloseHandle(hStoppedEvent);
      p->hStoppedEvent = NULL;
    }
    if( zStopper!=0 ){
      fossil_free(p->zStopper);
      p->zStopper = 0;
    }
    fossil_free(p);
  }
}

/*
** Process a single incoming HTTP request.
*/
static void win32_http_request(void *pAppData){
  HttpRequest *p = (HttpRequest*)pAppData;
  FILE *in = 0, *out = 0, *aux = 0;
  int amt, got, i;
  int wanted = 0;
  char *z;
  char *zIp;
  void *sslConn = 0;
  char zCmdFName[MAX_PATH];
  char zRequestFName[MAX_PATH];
  char zReplyFName[MAX_PATH];
  char zCmd[2000];          /* Command-line to process the request */
  char zBuf[65536];         /* The HTTP request header */
  const int szHdr = 4000;   /* Reduced header size */

  sqlite3_snprintf(MAX_PATH, zCmdFName,
                   "%s_%06d_cmd.txt", zTempPrefix, p->id);
  sqlite3_snprintf(MAX_PATH, zRequestFName,
                   "%s_%06d_in.txt", zTempPrefix, p->id);
  sqlite3_snprintf(MAX_PATH, zReplyFName,
                   "%s_%06d_out.txt", zTempPrefix, p->id);
  amt = 0;
  if( g.httpUseSSL ){
#ifdef FOSSIL_ENABLE_SSL
    sslConn = ssl_new_server(p->s);
#endif
  }
  while( amt<szHdr ){
    if( sslConn ){
#ifdef FOSSIL_ENABLE_SSL
      got = ssl_read_server(sslConn, &zBuf[amt], szHdr-1-amt, 0);
#endif
    }else{
      got = recv(p->s, &zBuf[amt], szHdr-1-amt, 0);
      if( got==SOCKET_ERROR ) goto end_request;
    }
    if( got==0 ){
      wanted = 0;
      break;
    }
    amt += got;
    zBuf[amt] = 0;
    z = strstr(zBuf, "\r\n\r\n");
    if( z ){
      wanted = find_content_length(zBuf) + (&z[4]-zBuf) - amt;
      break;
    }else{
      z = strstr(zBuf, "\n\n");
      if( z ){
        wanted = find_content_length(zBuf) + (&z[2]-zBuf) - amt;
        break;
      }
    }
  }
  if( amt>=szHdr ) goto end_request;
  out = fossil_fopen(zRequestFName, "wb");
  if( out==0 ) goto end_request;
  fwrite(zBuf, 1, amt, out);
  while( wanted>0 ){
    if( sslConn ){
#ifdef FOSSIL_ENABLE_SSL
      got = ssl_read_server(sslConn, zBuf, min(wanted, sizeof(zBuf)), 1);
#endif
    }else{
      got = recv(p->s, zBuf, sizeof(zBuf), 0);
      if( got==SOCKET_ERROR ) goto end_request;
    }
    if( got>0 ){
      fwrite(zBuf, 1, got, out);
    }else{
      break;
    }
    wanted -= got;
  }

  /*
  ** The repository name is only needed if there was no open check-out.  This
  ** is designed to allow the open check-out for the interactive user to work
  ** with the local Fossil server started via the "ui" command.
  */
  aux = fossil_fopen(zCmdFName, "wb");
  if( aux==0 ) goto end_request;
  fprintf(aux, "%s--in %s\n", get_utf8_bom(0), zRequestFName);
  zIp = SocketAddr_toString(&p->addr);
  fprintf(aux, "--out %s\n--ipaddr %s\n", zReplyFName, zIp);
  fossil_free(zIp);
  fprintf(aux, "--as %s\n", g.zCmdName);
  if( g.zErrlog && g.zErrlog[0] ){
    fprintf(aux,"--errorlog %s\n", g.zErrlog);
  }
  if( (p->flags & HTTP_SERVER_HAD_CHECKOUT)==0 ){
    fprintf(aux,"%s",g.zRepositoryName);
  }

  sqlite3_snprintf(sizeof(zCmd), zCmd,
    "\"%s\" http -args \"%s\"%s%s",
    g.nameOfExe, zCmdFName,
    g.httpUseSSL ? "" : " --nossl", p->zOptions
  );
  in = fossil_fopen(zReplyFName, "w+b");
  fflush(out);
  fflush(aux);
  if( g.fHttpTrace ){
    fossil_print("%s\n", zCmd);
  }
  fossil_system(zCmd);
  if( in ){
    while( (got = fread(zBuf, 1, sizeof(zBuf), in))>0 ){
      if( sslConn ){
#ifdef FOSSIL_ENABLE_SSL
        ssl_write_server(sslConn, zBuf, got);
#endif
      }else{
        send(p->s, zBuf, got, 0);
      }
    }
  }

end_request:
  if( out ) fclose(out);
  if( aux ) fclose(aux);
  if( in ) fclose(in);
  /* Initiate shutdown prior to closing the socket */
  if( sslConn!=0 ){
#ifdef FOSSIL_ENABLE_SSL
    ssl_close_server(sslConn);
#endif
  }
  if( shutdown(p->s,1)==0 ) shutdown(p->s,0);
  closesocket(p->s);
  /* Make multiple attempts to delete the temporary files.  Sometimes AV
  ** software keeps the files open for a few seconds, preventing the file
  ** from being deleted on the first try. */
  if( !g.fHttpTrace ){
    for(i=1; i<=10 && file_delete(zRequestFName); i++){ Sleep(1000*i); }
    for(i=1; i<=10 && file_delete(zCmdFName); i++){ Sleep(1000*i); }
    for(i=1; i<=10 && file_delete(zReplyFName); i++){ Sleep(1000*i); }
  }
  fossil_free(p);
}

/*
** Process a single incoming SCGI request.
*/
static void win32_scgi_request(void *pAppData){
  HttpRequest *p = (HttpRequest*)pAppData;
  FILE *in = 0, *out = 0;
  int amt, got, nHdr, i;
  int wanted = 0;
  char *zIp;
  char zRequestFName[MAX_PATH];
  char zReplyFName[MAX_PATH];
  char zCmd[2000];          /* Command-line to process the request */
  char zHdr[4000];          /* The SCGI request header */

  sqlite3_snprintf(MAX_PATH, zRequestFName,
                   "%s_%06d_in.txt", zTempPrefix, p->id);
  sqlite3_snprintf(MAX_PATH, zReplyFName,
                   "%s_%06d_out.txt", zTempPrefix, p->id);
  out = fossil_fopen(zRequestFName, "wb");
  if( out==0 ) goto end_request;
  amt = 0;
  got = recv(p->s, zHdr, sizeof(zHdr), 0);
  if( got==SOCKET_ERROR ) goto end_request;
  amt = fwrite(zHdr, 1, got, out);
  nHdr = 0;
  for(i=0; zHdr[i]>='0' && zHdr[i]<='9'; i++){
    nHdr = 10*nHdr + zHdr[i] - '0';
  }
  wanted = nHdr + i + 1;
  if( strcmp(zHdr+i+1, "CONTENT_LENGTH")==0 ){
    wanted += atoi(zHdr+i+15);
  }
  while( wanted>amt ){
    got = recv(p->s, zHdr, wanted<sizeof(zHdr) ? wanted : sizeof(zHdr), 0);
    if( got<=0 ) break;
    fwrite(zHdr, 1, got, out);
    wanted += got;
  }
  assert( g.zRepositoryName && g.zRepositoryName[0] );
  zIp = SocketAddr_toString(&p->addr);
  sqlite3_snprintf(sizeof(zCmd), zCmd,
    "\"%s\" http --in \"%s\" --out \"%s\" --ipaddr %s \"%s\""
    " --scgi --nossl%s",
    g.nameOfExe, zRequestFName, zReplyFName, zIp,
    g.zRepositoryName, p->zOptions
  );
  fossil_free(zIp);
  in = fossil_fopen(zReplyFName, "w+b");
  fflush(out);
  fossil_system(zCmd);
  if( in ){
    while( (got = fread(zHdr, 1, sizeof(zHdr), in))>0 ){
      send(p->s, zHdr, got, 0);
    }
  }

end_request:
  if( out ) fclose(out);
  if( in ) fclose(in);
  /* Initiate shutdown prior to closing the socket */
  if( shutdown(p->s,1)==0 ) shutdown(p->s,0);
  closesocket(p->s);
  /* Make multiple attempts to delete the temporary files.  Sometimes AV
  ** software keeps the files open for a few seconds, preventing the file
  ** from being deleted on the first try. */
  for(i=1; i<=10 && file_delete(zRequestFName); i++){ Sleep(1000*i); }
  for(i=1; i<=10 && file_delete(zReplyFName); i++){ Sleep(1000*i); }
  fossil_free(p);
}

/* forward reference */
static void win32_http_service_running(DualSocket* pS);

/*
** Start a listening socket and process incoming HTTP requests on
** that socket.
*/
void win32_http_server(
  int mnPort, int mxPort,   /* Range of allowed TCP port numbers */
  const char *zBrowser,     /* Command to launch browser.  (Or NULL) */
  const char *zStopper,     /* Stop server when this file is exists (Or NULL) */
  const char *zBaseUrl,     /* The --baseurl option, or NULL */
  const char *zNotFound,    /* The --notfound option, or NULL */
  const char *zFileGlob,    /* The --fileglob option, or NULL */
  const char *zIpAddr,      /* Bind to this IP address, if not NULL */
  int flags                 /* One or more HTTP_SERVER_ flags */
){
  HANDLE hStoppedEvent;
  WSADATA wd;
  DualSocket ds;
  int idCnt = 0;
  int iPort = mnPort;
  Blob options;
  wchar_t zTmpPath[MAX_PATH];
  char *zTempSubDirPath;
  const char *zTempSubDir = "fossil";
  const char *zSkin;
#if USE_SEE
  const char *zSavedKey = 0;
  size_t savedKeySize = 0;
#endif

  blob_zero(&options);
  if( PB("HTTPS") ){
    blob_appendf(&options, " --https");
  }
  if( zBaseUrl ){
    blob_appendf(&options, " --baseurl ");
    blob_append_escaped_arg(&options, zBaseUrl, 0);
  }
  if( zNotFound ){
    blob_appendf(&options, " --notfound ");
    blob_append_escaped_arg(&options, zNotFound, 1);
  }
  if( g.zCkoutAlias ){
    blob_appendf(&options, " --ckout-alias ");
    blob_append_escaped_arg(&options, g.zCkoutAlias, 0);
  }
  if( zFileGlob ){
    blob_appendf(&options, " --files-urlenc %T", zFileGlob);
  }
  if( g.useLocalauth ){
    blob_appendf(&options, " --localauth");
  }
  if( g.thTrace ){
    blob_appendf(&options, " --th-trace");
  }
  if( flags & HTTP_SERVER_REPOLIST ){
    blob_appendf(&options, " --repolist");
  }
  if( g.zExtRoot && g.zExtRoot[0] ){
    blob_appendf(&options, " --extroot");
    blob_append_escaped_arg(&options, g.zExtRoot, 1);
  }
  zSkin = skin_in_use();
  if( zSkin ){
    blob_appendf(&options, " --skin %s", zSkin);
  }
  if( g.zMainMenuFile ){
    blob_appendf(&options, " --mainmenu ");
    blob_append_escaped_arg(&options, g.zMainMenuFile, 1);
  }
  if( builtin_get_js_delivery_mode()!=0 /* JS_INLINE==0 may change? */ ){
    blob_appendf(&options, " --jsmode ");
    blob_append_escaped_arg(&options, builtin_get_js_delivery_mode_name(), 0);
  }
#if USE_SEE
  zSavedKey = db_get_saved_encryption_key();
  savedKeySize = db_get_saved_encryption_key_size();
  if( db_is_valid_saved_encryption_key(zSavedKey, savedKeySize) ){
    blob_appendf(&options, " --usepidkey %lu:%p:%u", GetCurrentProcessId(),
                 zSavedKey, savedKeySize);
  }
#endif
  if( WSAStartup(MAKEWORD(2,0), &wd) ){
    fossil_panic("unable to initialize winsock");
  }
  DualSocket_init(&ds);
  while( iPort<=mxPort ){
    if( zIpAddr ){
      if( DualSocket_listen(&ds, zIpAddr, iPort)==0 ){
        iPort++;
        continue;
      }
    }else{
      if( DualSocket_listen(&ds,
                            (flags & HTTP_SERVER_LOCALHOST) ? "L" : "W",
                            iPort
                           )==0 ){
        iPort++;
        continue;
      }
    }
    break;
  }
  if( iPort>mxPort ){
    /* These exits are merely fatal because firewall settings can cause them. */
    if( mnPort==mxPort ){
      fossil_fatal("unable to open listening socket on port %d", mnPort);
    }else{
      fossil_fatal("unable to open listening socket on any"
                   " port in the range %d..%d", mnPort, mxPort);
    }
  }
  if( !GetTempPathW(MAX_PATH, zTmpPath) ){
    fossil_panic("unable to get path to the temporary directory.");
  }
  /* Use a subdirectory for temp files (can then be excluded from virus scan) */
  zTempSubDirPath = mprintf("%s%s\\",fossil_path_to_utf8(zTmpPath),zTempSubDir);
  if ( !file_mkdir(zTempSubDirPath, ExtFILE, 0) ||
        file_isdir(zTempSubDirPath, ExtFILE)==1 ){
    wcscpy(zTmpPath, fossil_utf8_to_path(zTempSubDirPath, 1));
  }
  if( g.fHttpTrace ){
    zTempPrefix = mprintf("httptrace");
  }else{
    zTempPrefix = mprintf("%sfossil_server_P%d",
                          fossil_unicode_to_utf8(zTmpPath), iPort);
  }
  fossil_print("Temporary files: %s*\n", zTempPrefix);
  fossil_print("Listening for %s requests on TCP port %d\n",
               (flags&HTTP_SERVER_SCGI)!=0 ? "SCGI" :
               g.httpUseSSL ? "TLS-encrypted HTTPS" : "HTTP", iPort);
  if( zBrowser ){
    zBrowser = mprintf(zBrowser /*works-like:"%d"*/, iPort);
    fossil_print("Launch webbrowser: %s\n", zBrowser);
    fossil_system(zBrowser);
  }
  fossil_print("Type Ctrl-C to stop the HTTP server\n");
  /* Create an event used to signal when this server is exiting. */
  hStoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  assert( hStoppedEvent!=NULL );
  /* If there is a stopper file name, start the dedicated thread now.
  ** It will attempt to close the listener socket within 1 second of
  ** the stopper file being created. */
  if( zStopper ){
    HttpServer *pServer = fossil_malloc(sizeof(HttpServer));
    memset(pServer, 0, sizeof(HttpServer));
    DuplicateHandle(GetCurrentProcess(), hStoppedEvent,
                    GetCurrentProcess(), &pServer->hStoppedEvent,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
    assert( pServer->hStoppedEvent!=NULL );
    pServer->zStopper = fossil_strdup(zStopper);
    pServer->listener = ds;
    file_delete(zStopper);
    _beginthread(win32_server_stopper, 0, (void*)pServer);
  }
  /* Set the service status to running and pass the listener socket to the
  ** service handling procedures. */
  win32_http_service_running(&ds);
  for(;;){
    DualSocket client;
    DualAddr client_addr;
    HttpRequest *pRequest;
    int wsaError;

    DualSocket_accept(&ds, &client, &client_addr);
    if( client.s4==INVALID_SOCKET && client.s6==INVALID_SOCKET ){
      /* If the service control handler has closed the listener socket,
      ** cleanup and return, otherwise report a fatal error. */
      wsaError =  WSAGetLastError();
      DualSocket_close(&ds);
      if( (wsaError==WSAEINTR) || (wsaError==WSAENOTSOCK) ){
        WSACleanup();
        return;
      }else{
        WSACleanup();
        fossil_panic("error from accept()");
      }
    }
    if( client.s4!=INVALID_SOCKET ){
      pRequest = fossil_malloc(sizeof(HttpRequest));
      pRequest->id = ++idCnt;
      pRequest->s = client.s4;
      memcpy(&pRequest->addr, &client_addr.a4, sizeof(client_addr.a4));
      pRequest->flags = flags;
      pRequest->zOptions = blob_str(&options);
      if( flags & HTTP_SERVER_SCGI ){
        _beginthread(win32_scgi_request, 0, (void*)pRequest);
      }else{
        _beginthread(win32_http_request, 0, (void*)pRequest);
      }
    }
    if( client.s6!=INVALID_SOCKET ){
      pRequest = fossil_malloc(sizeof(HttpRequest));
      pRequest->id = ++idCnt;
      pRequest->s = client.s6;
      memcpy(&pRequest->addr, &client_addr.a6, sizeof(client_addr.a6));
      pRequest->flags = flags;
      pRequest->zOptions = blob_str(&options);
      if( flags & HTTP_SERVER_SCGI ){
        _beginthread(win32_scgi_request, 0, (void*)pRequest);
      }else{
        _beginthread(win32_http_request, 0, (void*)pRequest);
      }
    }
  }
  DualSocket_close(&ds);
  WSACleanup();
  SetEvent(hStoppedEvent);
  CloseHandle(hStoppedEvent);
}

/*
** The HttpService structure is used to pass information to the service main
** function and to the service control handler function.
*/
typedef struct HttpService HttpService;
struct HttpService {
  int port;                 /* Port on which the http server should run */
  const char *zBaseUrl;     /* The --baseurl option, or NULL */
  const char *zNotFound;    /* The --notfound option, or NULL */
  const char *zFileGlob;    /* The --files option, or NULL */
  int flags;                /* One or more HTTP_SERVER_ flags */
  int isRunningAsService;   /* Are we running as a service ? */
  const wchar_t *zServiceName;/* Name of the service */
  DualSocket s;             /* Sockets on which the http server listens */
};

/*
** Variables used for running as windows service.
*/
static HttpService hsData = {8080, NULL, NULL, NULL, 0, 0, NULL,
                             {INVALID_SOCKET, INVALID_SOCKET}};
static SERVICE_STATUS ssStatus;
static SERVICE_STATUS_HANDLE sshStatusHandle;

/*
** Get message string of the last system error. Return a pointer to the
** message string. Call fossil_unicode_free() to deallocate any memory used
** to store the message string when done.
*/
static char *win32_get_last_errmsg(void){
  DWORD nMsg;
  DWORD nErr = GetLastError();
  LPWSTR tmp = NULL;
  char *zMsg = NULL;

  /* Try first to get the error text in English. */
  nMsg = FormatMessageW(
           FORMAT_MESSAGE_ALLOCATE_BUFFER |
           FORMAT_MESSAGE_FROM_SYSTEM     |
           FORMAT_MESSAGE_IGNORE_INSERTS,
           NULL,
           nErr,
           MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
           (LPWSTR) &tmp,
           0,
           NULL
         );
  if( !nMsg ){
    /* No english, get what the system has available. */
    nMsg = FormatMessageW(
             FORMAT_MESSAGE_ALLOCATE_BUFFER |
             FORMAT_MESSAGE_FROM_SYSTEM     |
             FORMAT_MESSAGE_IGNORE_INSERTS,
             NULL,
             nErr,
             0,
             (LPWSTR) &tmp,
             0,
             NULL
           );
  }
  if( nMsg ){
    zMsg = fossil_unicode_to_utf8(tmp);
  }else{
    fossil_panic("unable to get system error message.");
  }
  if( tmp ){
    LocalFree((HLOCAL) tmp);
  }
  return zMsg;
}

/*
** Report the current status of the service to the service control manager.
** Make sure that during service startup no control codes are accepted.
*/
static void win32_report_service_status(
  DWORD dwCurrentState,     /* The current state of the service */
  DWORD dwWin32ExitCode,    /* The error code to report */
  DWORD dwWaitHint          /* The estimated time for a pending operation */
){
  if( dwCurrentState==SERVICE_START_PENDING ){
    ssStatus.dwControlsAccepted = 0;
  }else{
    ssStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  }
  ssStatus.dwCurrentState = dwCurrentState;
  ssStatus.dwWin32ExitCode = dwWin32ExitCode;
  ssStatus.dwWaitHint = dwWaitHint;

  if( (dwCurrentState==SERVICE_RUNNING) ||
      (dwCurrentState==SERVICE_STOPPED) ){
    ssStatus.dwCheckPoint = 0;
  }else{
    ssStatus.dwCheckPoint++;
  }
  SetServiceStatus(sshStatusHandle, &ssStatus);
  return ;
}

/*
** Handle control codes sent from the service control manager.
** The control dispatcher in the main thread of the service process invokes
** this function whenever it receives a control request from the service
** control manager.
*/
static void WINAPI win32_http_service_ctrl(
  DWORD dwCtrlCode
){
  switch( dwCtrlCode ){
    case SERVICE_CONTROL_STOP: {
      DualSocket_close(&hsData.s);
      win32_report_service_status(SERVICE_STOP_PENDING, NO_ERROR, 0);
      break;
    }
    default: {
      break;
    }
  }
  return;
}

/*
** This is the main entry point for the service.
** When the service control manager receives a request to start the service,
** it starts the service process (if it is not already running). The main
** thread of the service process calls the StartServiceCtrlDispatcher
** function with a pointer to an array of SERVICE_TABLE_ENTRY structures.
** Then the service control manager sends a start request to the service
** control dispatcher for this service process. The service control dispatcher
** creates a new thread to execute the ServiceMain function (this function)
** of the service being started.
*/
static void WINAPI win32_http_service_main(
  DWORD argc,              /* Number of arguments in argv */
  LPWSTR *argv             /* Arguments passed */
){

  /* Update the service information. */
  hsData.isRunningAsService = 1;
  if( argc>0 ){
    hsData.zServiceName = argv[0];
  }

  /* Register the service control handler function */
  sshStatusHandle = RegisterServiceCtrlHandlerW(L"", win32_http_service_ctrl);
  if( !sshStatusHandle ){
    win32_report_service_status(SERVICE_STOPPED, NO_ERROR, 0);
    return;
  }

  /* Set service specific data and report that the service is starting. */
  ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  ssStatus.dwServiceSpecificExitCode = 0;
  win32_report_service_status(SERVICE_START_PENDING, NO_ERROR, 3000);

   /* Execute the http server */
  win32_http_server(hsData.port, hsData.port,
                    NULL, NULL, hsData.zBaseUrl, hsData.zNotFound,
                    hsData.zFileGlob, 0, hsData.flags);

  /* Service has stopped now. */
  win32_report_service_status(SERVICE_STOPPED, NO_ERROR, 0);
  return;
}

/*
** When running as service, update the HttpService structure with the
** listener socket and update the service status. This procedure must be
** called from the http server when he is ready to accept connections.
*/
static void win32_http_service_running(DualSocket *pS){
  if( hsData.isRunningAsService ){
    hsData.s = *pS;
    win32_report_service_status(SERVICE_RUNNING, NO_ERROR, 0);
  }
}

/*
** Try to start the http server as a windows service. If we are running in
** an interactive console session, this routine fails and returns a non zero
** integer value. When running as service, this routine does not return until
** the service is stopped. In this case, the return value is zero.
*/
int win32_http_service(
  int nPort,                /* TCP port number */
  const char *zBaseUrl,     /* The --baseurl option, or NULL */
  const char *zNotFound,    /* The --notfound option, or NULL */
  const char *zFileGlob,    /* The --files option, or NULL */
  int flags                 /* One or more HTTP_SERVER_ flags */
){
  /* Define the service table. */
  SERVICE_TABLE_ENTRYW ServiceTable[] =
    {{L"", (LPSERVICE_MAIN_FUNCTIONW)win32_http_service_main}, {NULL, NULL}};

  /* Initialize the HttpService structure. */
  hsData.port = nPort;
  hsData.zBaseUrl = zBaseUrl;
  hsData.zNotFound = zNotFound;
  hsData.zFileGlob = zFileGlob;
  hsData.flags = flags;

  if( GetStdHandle(STD_INPUT_HANDLE)!=NULL ){ return 1; }

  /* Try to start the control dispatcher thread for the service. */
  if( !StartServiceCtrlDispatcherW(ServiceTable) ){
    if( GetLastError()==ERROR_FAILED_SERVICE_CONTROLLER_CONNECT ){
      return 1;
    }else{
      fossil_fatal("error from StartServiceCtrlDispatcher()");
    }
  }
  return 0;
}

/* Duplicate #ifdef needed for mkindex */
#ifdef _WIN32
/*
** COMMAND: winsrv*
**
** Usage: %fossil winsrv METHOD ?SERVICE-NAME? ?OPTIONS?
**
** Where METHOD is one of: create delete show start stop.
**
** The winsrv command manages Fossil as a Windows service.  This allows
** (for example) Fossil to be running in the background when no user
** is logged in.
**
** In the following description of the methods, "Fossil-DSCM" will be
** used as the default SERVICE-NAME:
**
**    %fossil winsrv create ?SERVICE-NAME? ?OPTIONS?
**
**         Creates a service. Available options include:
**
**         -D|--display DISPLAY-NAME
**
**              Sets the display name of the service. This name is shown
**              by graphical interface programs. By default, the display name
**              is equal to the service name.
**
**         -S|--start TYPE
**
**              Sets the start type of the service. TYPE can be "manual",
**              which means you need to start the service yourself with the
**              'fossil winsrv start' command or with the "net start" command
**              from the operating system. If TYPE is set to "auto", the service
**              will be started automatically by the system during startup.
**
**         --username USERNAME
**
**              Specifies the user account which will be used to run the
**              service. The account needs the "Logon as a service" right
**              enabled in its profile. Specify local accounts as follows:
**              ".\\USERNAME". By default, the "LocalSystem" account will be
**              used.
**
**         -W|--password PASSWORD
**
**              Password for the user account.
**
**         The following options are more or less the same as for the "server"
**         command and influence the behavior of the http server:
**
**         --baseurl URL
**
**              Use URL as the base (useful for reverse proxies)
**
**         -P|--port TCPPORT
**
**              Specifies the TCP port (default port is 8080) on which the
**              server should listen.
**
**         -R|--repository REPO
**
**              Specifies the name of the repository to be served.
**              The repository option may be omitted if the working directory
**              is within an open check-out.
**              The REPOSITORY can be a directory (aka folder) that contains
**              one or more repositories with names ending in ".fossil".
**              In that case, the first element of the URL is used to select
**              among the various repositories.
**
**         --notfound URL
**
**              If REPOSITORY is a directory that contains one or more
**              repositories with names of the form "*.fossil" then the
**              first element of the URL  pathname selects among the various
**              repositories. If the pathname does not select a valid
**              repository and the --notfound option is available,
**              then the server redirects (HTTP code 302) to the URL of
**              --notfound.
**
**         --localauth
**
**              Enables automatic login if the --localauth option is present
**              and the "localauth" setting is off and the connection is from
**              localhost.
**
**         --repolist
**
**              If REPOSITORY is directory, URL "/" lists all repositories.
**
**         --scgi
**
**              Create an SCGI server instead of an HTTP server
**
**
**    %fossil winsrv delete ?SERVICE-NAME?
**
**         Deletes a service. If the service is currently running, it will be
**         stopped first and then deleted.
**
**
**    %fossil winsrv show ?SERVICE-NAME?
**
**         Shows how the service is configured and its current state.
**
**
**    %fossil winsrv start ?SERVICE-NAME?
**
**         Start the service.
**
**
**    %fossil winsrv stop ?SERVICE-NAME?
**
**         Stop the service.
**
**
** NOTE: This command is available on Windows operating systems only and
**       requires administrative rights on the machine executed.
**
*/
void cmd_win32_service(void){
  int n;
  const char *zMethod;
  const char *zSvcName = "Fossil-DSCM";    /* Default service name */

  if( g.argc<3 ){
    usage("create|delete|show|start|stop ...");
  }
  zMethod = g.argv[2];
  n = strlen(zMethod);

  if( strncmp(zMethod, "create", n)==0 ){
    SC_HANDLE hScm;
    SC_HANDLE hSvc;
    SERVICE_DESCRIPTIONW
      svcDescr = {L"Fossil - Distributed Software Configuration Management"};
    DWORD dwStartType = SERVICE_DEMAND_START;
    const char *zAltBase    = find_option("baseurl", 0, 1);
    const char *zDisplay    = find_option("display", "D", 1);
    const char *zStart      = find_option("start", "S", 1);
    const char *zUsername   = find_option("username", 0, 1);
    const char *zPassword   = find_option("password", "W", 1);
    const char *zPort       = find_option("port", "P", 1);
    const char *zNotFound   = find_option("notfound", 0, 1);
    const char *zFileGlob   = find_option("files", 0, 1);
    const char *zLocalAuth  = find_option("localauth", 0, 0);
    const char *zRepository = find_repository_option();
    int useSCGI             = find_option("scgi", 0, 0)!=0;
    int allowRepoList       = find_option("repolist",0,0)!=0;
    Blob binPath;

    verify_all_options();
    if( g.argc==4 ){
      zSvcName = g.argv[3];
    }else if( g.argc>4 ){
      fossil_fatal("too many arguments for create method.");
    }
    /* Process service creation specific options. */
    if( !zDisplay ){
      zDisplay = zSvcName;
    }
    /* Per MSDN, the password parameter cannot be NULL.  Must use empty
    ** string instead (i.e. in the call to CreateServiceW). */
    if( !zPassword ){
      zPassword = "";
    }
    if( zStart ){
      if( strncmp(zStart, "auto", strlen(zStart))==0 ){
        dwStartType = SERVICE_AUTO_START;
      }else if( strncmp(zStart, "manual", strlen(zStart))==0 ){
        dwStartType = SERVICE_DEMAND_START;
      }else{
        winhttp_fatal("create", zSvcName,
                     "specify 'auto' or 'manual' for the '-S|--start' option");
      }
    }
    /* Process options for Fossil running as server. */
    if( zPort && (atoi(zPort)<=0) ){
      winhttp_fatal("create", zSvcName,
                   "port number must be in the range 1 - 65535.");
    }
    if( !zRepository ){
      db_must_be_within_tree();
    }else if( file_isdir(zRepository, ExtFILE)==1 ){
      g.zRepositoryName = fossil_strdup(zRepository);
      file_simplify_name(g.zRepositoryName, -1, 0);
    }else{
      db_open_repository(zRepository);
    }
    db_close(0);
    /* Build the fully-qualified path to the service binary file. */
    blob_zero(&binPath);
    blob_appendf(&binPath, "\"%s\" server", g.nameOfExe);
    if( zAltBase ) blob_appendf(&binPath, " --baseurl %s", zAltBase);
    if( zPort ) blob_appendf(&binPath, " --port %s", zPort);
    if( useSCGI ) blob_appendf(&binPath, " --scgi");
    if( allowRepoList ) blob_appendf(&binPath, " --repolist");
    if( zNotFound ) blob_appendf(&binPath, " --notfound \"%s\"", zNotFound);
    if( zFileGlob ) blob_appendf(&binPath, " --files-urlenc %T", zFileGlob);
    if( zLocalAuth ) blob_append(&binPath, " --localauth", -1);
    blob_appendf(&binPath, " \"%s\"", g.zRepositoryName);
    /* Create the service. */
    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if( !hScm ) winhttp_fatal("create", zSvcName, win32_get_last_errmsg());
    hSvc = CreateServiceW(
             hScm,                                    /* Handle to the SCM */
             fossil_utf8_to_unicode(zSvcName),        /* Name of the service */
             fossil_utf8_to_unicode(zDisplay),        /* Display name */
             SERVICE_ALL_ACCESS,                      /* Desired access */
             SERVICE_WIN32_OWN_PROCESS,               /* Service type */
             dwStartType,                             /* Start type */
             SERVICE_ERROR_NORMAL,                    /* Error control */
             fossil_utf8_to_unicode(blob_str(&binPath)), /* Binary path */
             NULL,                                    /* Load ordering group */
             NULL,                                    /* Tag value */
             NULL,                                    /* Service dependencies */
             zUsername ? fossil_utf8_to_unicode(zUsername) : 0, /* Account */
             fossil_utf8_to_unicode(zPassword)        /* Account password */
           );
    if( !hSvc ) winhttp_fatal("create", zSvcName, win32_get_last_errmsg());
    /* Set the service description. */
    ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &svcDescr);
    fossil_print("Service '%s' successfully created.\n", zSvcName);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
  }else
  if( strncmp(zMethod, "delete", n)==0 ){
    SC_HANDLE hScm;
    SC_HANDLE hSvc;
    SERVICE_STATUS sstat;

    verify_all_options();
    if( g.argc==4 ){
      zSvcName = g.argv[3];
    }else if( g.argc>4 ){
      fossil_fatal("too many arguments for delete method.");
    }
    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if( !hScm ) winhttp_fatal("delete", zSvcName, win32_get_last_errmsg());
    hSvc = OpenServiceW(hScm, fossil_utf8_to_unicode(zSvcName),
                        SERVICE_ALL_ACCESS);
    if( !hSvc ) winhttp_fatal("delete", zSvcName, win32_get_last_errmsg());
    QueryServiceStatus(hSvc, &sstat);
    if( sstat.dwCurrentState!=SERVICE_STOPPED ){
      fossil_print("Stopping service '%s'", zSvcName);
      if( sstat.dwCurrentState!=SERVICE_STOP_PENDING ){
        if( !ControlService(hSvc, SERVICE_CONTROL_STOP, &sstat) ){
          winhttp_fatal("delete", zSvcName, win32_get_last_errmsg());
        }
        QueryServiceStatus(hSvc, &sstat);
      }
      while( sstat.dwCurrentState==SERVICE_STOP_PENDING  ||
             sstat.dwCurrentState==SERVICE_RUNNING ){
        Sleep(100);
        fossil_print(".");
        QueryServiceStatus(hSvc, &sstat);
      }
      if( sstat.dwCurrentState==SERVICE_STOPPED ){
        fossil_print("\nService '%s' stopped.\n", zSvcName);
      }else{
        winhttp_fatal("delete", zSvcName, win32_get_last_errmsg());
      }
    }
    if( !DeleteService(hSvc) ){
      if( GetLastError()==ERROR_SERVICE_MARKED_FOR_DELETE ){
        fossil_warning("Service '%s' already marked for delete.\n", zSvcName);
      }else{
        winhttp_fatal("delete", zSvcName, win32_get_last_errmsg());
      }
    }else{
      fossil_print("Service '%s' successfully deleted.\n", zSvcName);
    }
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
  }else
  if( strncmp(zMethod, "show", n)==0 ){
    SC_HANDLE hScm;
    SC_HANDLE hSvc;
    SERVICE_STATUS sstat;
    LPQUERY_SERVICE_CONFIGW pSvcConfig;
    LPSERVICE_DESCRIPTIONW pSvcDescr;
    BOOL bStatus;
    DWORD nRequired;
    static const char *const zSvcTypes[] = {
      "Driver service",
      "File system driver service",
      "Service runs in its own process",
      "Service shares a process with other services",
      "Service can interact with the desktop"
    };
    const char *zSvcType = "";
    static const char *const zSvcStartTypes[] = {
      "Started by the system loader",
      "Started by the IoInitSystem function",
      "Started automatically by the service control manager",
      "Started manually",
      "Service cannot be started"
    };
    const char *zSvcStartType = "";
    static const char *const zSvcStates[] = {
      "Stopped", "Starting", "Stopping", "Running",
      "Continue pending", "Pause pending", "Paused"
    };
    const char *zSvcState = "";

    verify_all_options();
    if( g.argc==4 ){
      zSvcName = g.argv[3];
    }else if( g.argc>4 ){
      fossil_fatal("too many arguments for show method.");
    }
    hScm = OpenSCManagerW(NULL, NULL, GENERIC_READ);
    if( !hScm ) winhttp_fatal("show", zSvcName, win32_get_last_errmsg());
    hSvc = OpenServiceW(hScm, fossil_utf8_to_unicode(zSvcName), GENERIC_READ);
    if( !hSvc ) winhttp_fatal("show", zSvcName, win32_get_last_errmsg());
    /* Get the service configuration */
    bStatus = QueryServiceConfigW(hSvc, NULL, 0, &nRequired);
    if( !bStatus && GetLastError()!=ERROR_INSUFFICIENT_BUFFER ){
      winhttp_fatal("show", zSvcName, win32_get_last_errmsg());
    }
    pSvcConfig = fossil_malloc(nRequired);
    bStatus = QueryServiceConfigW(hSvc, pSvcConfig, nRequired, &nRequired);
    if( !bStatus ) winhttp_fatal("show", zSvcName, win32_get_last_errmsg());
    /* Translate the service type */
    switch( pSvcConfig->dwServiceType ){
      case SERVICE_KERNEL_DRIVER:       zSvcType = zSvcTypes[0]; break;
      case SERVICE_FILE_SYSTEM_DRIVER:  zSvcType = zSvcTypes[1]; break;
      case SERVICE_WIN32_OWN_PROCESS:   zSvcType = zSvcTypes[2]; break;
      case SERVICE_WIN32_SHARE_PROCESS: zSvcType = zSvcTypes[3]; break;
      case SERVICE_INTERACTIVE_PROCESS: zSvcType = zSvcTypes[4]; break;
    }
    /* Translate the service start type */
    switch( pSvcConfig->dwStartType ){
      case SERVICE_BOOT_START:    zSvcStartType = zSvcStartTypes[0]; break;
      case SERVICE_SYSTEM_START:  zSvcStartType = zSvcStartTypes[1]; break;
      case SERVICE_AUTO_START:    zSvcStartType = zSvcStartTypes[2]; break;
      case SERVICE_DEMAND_START:  zSvcStartType = zSvcStartTypes[3]; break;
      case SERVICE_DISABLED:      zSvcStartType = zSvcStartTypes[4]; break;
    }
    /* Get the service description. */
    bStatus = QueryServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION,
                                  NULL, 0, &nRequired);
    if( !bStatus && GetLastError()!=ERROR_INSUFFICIENT_BUFFER ){
      winhttp_fatal("show", zSvcName, win32_get_last_errmsg());
    }
    pSvcDescr = fossil_malloc(nRequired);
    bStatus = QueryServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION,
                                  (LPBYTE)pSvcDescr, nRequired, &nRequired);
    if( !bStatus ) winhttp_fatal("show", zSvcName, win32_get_last_errmsg());
    /* Retrieves the current status of the specified service. */
    bStatus = QueryServiceStatus(hSvc, &sstat);
    if( !bStatus ) winhttp_fatal("show", zSvcName, win32_get_last_errmsg());
    /* Translate the current state. */
    switch( sstat.dwCurrentState ){
      case SERVICE_STOPPED:          zSvcState = zSvcStates[0]; break;
      case SERVICE_START_PENDING:    zSvcState = zSvcStates[1]; break;
      case SERVICE_STOP_PENDING:     zSvcState = zSvcStates[2]; break;
      case SERVICE_RUNNING:          zSvcState = zSvcStates[3]; break;
      case SERVICE_CONTINUE_PENDING: zSvcState = zSvcStates[4]; break;
      case SERVICE_PAUSE_PENDING:    zSvcState = zSvcStates[5]; break;
      case SERVICE_PAUSED:           zSvcState = zSvcStates[6]; break;
    }
    /* Print service information to terminal */
    fossil_print("Service name .......: %s\n", zSvcName);
    fossil_print("Display name .......: %s\n",
                 fossil_unicode_to_utf8(pSvcConfig->lpDisplayName));
    fossil_print("Service description : %s\n",
                 fossil_unicode_to_utf8(pSvcDescr->lpDescription));
    fossil_print("Service type .......: %s.\n", zSvcType);
    fossil_print("Service start type .: %s.\n", zSvcStartType);
    fossil_print("Binary path name ...: %s\n",
                 fossil_unicode_to_utf8(pSvcConfig->lpBinaryPathName));
    fossil_print("Service username ...: %s\n",
                 fossil_unicode_to_utf8(pSvcConfig->lpServiceStartName));
    fossil_print("Current state ......: %s.\n", zSvcState);
    /* Cleanup */
    fossil_free(pSvcConfig);
    fossil_free(pSvcDescr);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
  }else
  if( strncmp(zMethod, "start", n)==0 ){
    SC_HANDLE hScm;
    SC_HANDLE hSvc;
    SERVICE_STATUS sstat;

    verify_all_options();
    if( g.argc==4 ){
      zSvcName = g.argv[3];
    }else if( g.argc>4 ){
      fossil_fatal("too many arguments for start method.");
    }
    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if( !hScm ) winhttp_fatal("start", zSvcName, win32_get_last_errmsg());
    hSvc = OpenServiceW(hScm, fossil_utf8_to_unicode(zSvcName),
                        SERVICE_ALL_ACCESS);
    if( !hSvc ) winhttp_fatal("start", zSvcName, win32_get_last_errmsg());
    QueryServiceStatus(hSvc, &sstat);
    if( sstat.dwCurrentState!=SERVICE_RUNNING ){
      fossil_print("Starting service '%s'", zSvcName);
      if( sstat.dwCurrentState!=SERVICE_START_PENDING ){
        if( !StartServiceW(hSvc, 0, NULL) ){
          winhttp_fatal("start", zSvcName, win32_get_last_errmsg());
        }
        QueryServiceStatus(hSvc, &sstat);
      }
      while( sstat.dwCurrentState==SERVICE_START_PENDING ||
             sstat.dwCurrentState==SERVICE_STOPPED ){
        Sleep(100);
        fossil_print(".");
        QueryServiceStatus(hSvc, &sstat);
      }
      if( sstat.dwCurrentState==SERVICE_RUNNING ){
        fossil_print("\nService '%s' started.\n", zSvcName);
      }else{
        winhttp_fatal("start", zSvcName, win32_get_last_errmsg());
      }
    }else{
      fossil_print("Service '%s' is already started.\n", zSvcName);
    }
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
  }else
  if( strncmp(zMethod, "stop", n)==0 ){
    SC_HANDLE hScm;
    SC_HANDLE hSvc;
    SERVICE_STATUS sstat;

    verify_all_options();
    if( g.argc==4 ){
      zSvcName = g.argv[3];
    }else if( g.argc>4 ){
      fossil_fatal("too many arguments for stop method.");
    }
    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if( !hScm ) winhttp_fatal("stop", zSvcName, win32_get_last_errmsg());
    hSvc = OpenServiceW(hScm, fossil_utf8_to_unicode(zSvcName),
                        SERVICE_ALL_ACCESS);
    if( !hSvc ) winhttp_fatal("stop", zSvcName, win32_get_last_errmsg());
    QueryServiceStatus(hSvc, &sstat);
    if( sstat.dwCurrentState!=SERVICE_STOPPED ){
      fossil_print("Stopping service '%s'", zSvcName);
      if( sstat.dwCurrentState!=SERVICE_STOP_PENDING ){
        if( !ControlService(hSvc, SERVICE_CONTROL_STOP, &sstat) ){
          winhttp_fatal("stop", zSvcName, win32_get_last_errmsg());
        }
        QueryServiceStatus(hSvc, &sstat);
      }
      while( sstat.dwCurrentState==SERVICE_STOP_PENDING ||
             sstat.dwCurrentState==SERVICE_RUNNING ){
        Sleep(100);
        fossil_print(".");
        QueryServiceStatus(hSvc, &sstat);
      }
      if( sstat.dwCurrentState==SERVICE_STOPPED ){
        fossil_print("\nService '%s' stopped.\n", zSvcName);
      }else{
        winhttp_fatal("stop", zSvcName, win32_get_last_errmsg());
      }
    }else{
      fossil_print("Service '%s' is already stopped.\n", zSvcName);
    }
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
  }else
  {
    fossil_fatal("METHOD should be one of:"
                 " create delete show start stop");
  }
  return;
}
#endif /* _WIN32 -- dupe needed for mkindex */
#endif /* _WIN32 -- This code is for win32 only */
