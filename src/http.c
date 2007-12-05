/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code that implements the client-side HTTP protocol
*/
#include "config.h"
#include "http.h"
#ifdef __MINGW32__
#  include <windows.h>
#  include <winsock2.h>
#else
#  include <arpa/inet.h>
#  include <sys/socket.h>
#  include <netdb.h>
#  include <netinet/in.h>
#endif
#include <assert.h>
#include <sys/types.h>
#include <signal.h>

/*
** Persistent information about the HTTP connection.
*/

#ifdef __MINGW32__
static WSADATA ws_info;
static int pSocket = 0;     /* The socket on which we talk to the server on */
#else
static FILE *pSocket = 0;   /* The socket filehandle on which we talk to the server */
#endif

/*
** Winsock must be initialize before use.  This helper method allows us to
** always call ws_init in our code regardless of platform but only actually
** initialize winsock on the windows platform.
*/
static void ws_init(){
#ifdef __MINGW32__
  if (WSAStartup(MAKEWORD(2,0), &ws_info) != 0){
    fossil_panic("can't initialize winsock");
  }
#endif
}

/*
** Like ws_init, winsock must also be cleaned up after.
*/
static void ws_cleanup(){
#ifdef __MINGW32__
  WSACleanup();
#endif
}

/*
** Open a socket connection to the server.  Return 0 on success and
** non-zero if an error occurs.
*/
static int http_open_socket(void){
  static struct sockaddr_in addr;  /* The server address */
  static int addrIsInit = 0;       /* True once addr is initialized */
  int s;

  ws_init();
  
  if( !addrIsInit ){

    addr.sin_family = AF_INET;
    addr.sin_port = htons(g.urlPort);
    *(int*)&addr.sin_addr = inet_addr(g.urlName);
    if( -1 == *(int*)&addr.sin_addr ){
#ifndef FOSSIL_STATIC_LINK
      struct hostent *pHost;
      pHost = gethostbyname(g.urlName);
      if( pHost!=0 ){
        memcpy(&addr.sin_addr,pHost->h_addr_list[0],pHost->h_length);
      }else
#endif
      {
        fossil_panic("can't resolve host name: %s\n", g.urlName);
      }
    }
    addrIsInit = 1;

    /* Set the Global.zIpAddr variable to the server we are talking to.
    ** This is used to populate the ipaddr column of the rcvfrom table,
    ** if any files are received from the server.
    */
    g.zIpAddr = mprintf("%s", inet_ntoa(addr.sin_addr));
  }
  s = socket(AF_INET,SOCK_STREAM,0);
  if( s<0 ){
    fossil_panic("cannot create a socket");
  }
  if( connect(s,(struct sockaddr*)&addr,sizeof(addr))<0 ){
    fossil_panic("cannot connect to host %s:%d", g.urlName, g.urlPort);
  }
#ifdef __MINGW32__
  pSocket = s;
#else
  pSocket = fdopen(s,"r+");
  signal(SIGPIPE, SIG_IGN);
#endif
  return 0;
}

#ifdef __MINGW32__
/*
** Read the socket until a newline '\n' is found.  Return the number
** of characters read. pSockId contains the socket handel.  pOut
** contains a pointer to the buffer to write to.  pOutSize contains
** the maximum size of the line that pOut can handle.
*/
static int socket_recv_line(int pSockId, char* pOut, int pOutSize){
  int received=0;
  char letter;
  memset(pOut,0,pOutSize);
  for(; received<pOutSize-1;received++){
    if( recv(pSockId,(char*)&letter,1,0)>0 ){
      pOut[received]=letter;
      if( letter=='\n' ){
        break;
      }
    }else{
      break;
    }
  }
  return received;
}

/*
** Initialize a blob to the data on an input socket.  return
** the number of bytes read into the blob.  Any prior content
** of the blob is discarded, not freed.
**
** The function was placed here in http.c due to it's socket
** nature and we did not want to introduce socket headers into
** the socket netural blob.c file.
*/
int socket_read_blob(Blob *pBlob, int pSockId, int nToRead){
  int i=0,read=0;
  char rbuf[50];
  blob_zero(pBlob);
  while ( i<nToRead ){
    read = recv(pSockId, rbuf, 50, 0);
    i += read;
    if( read<0 ){
      return 0;
    }
    blob_append(pBlob, rbuf, read);
  }
  return blob_size(pBlob);
}
#endif

/*
** Make a single attempt to talk to the server.  Return TRUE on success
** and FALSE on a failure.
**
** pHeader contains the HTTP header.  pPayload contains the content.
** The content of the reply is written into pReply.  pReply is assumed
** to be uninitialized prior to this call.
**
** If an error occurs, this routine return false, resets pReply and
** closes the persistent connection, if any.
*/
static int http_send_recv(Blob *pHeader, Blob *pPayload, Blob *pReply){
  int closeConnection=1;   /* default to closing the connection */
  int rc;
  int iLength;
  int iHttpVersion;
  int i;
  int nRead;
  char zLine[2000];

  if( pSocket==0 && http_open_socket() ){
    return 0;
  }
  iLength = -1;
#ifdef __MINGW32__
  /*
  ** Use recv/send on the windows platform as winsock does not allow
  ** sockets to be used as FILE handles, thus fdopen, fwrite, fgets
  ** does not function on windows for sockets.
  */
  rc = send(pSocket, blob_buffer(pHeader), blob_size(pHeader), 0);
  if( rc!=blob_size(pHeader) ) goto write_err;
  rc = send(pSocket, blob_buffer(pPayload), blob_size(pPayload), 0);
  if( rc!=blob_size(pPayload) ) goto write_err;
  
  /* Read the response */
  while( socket_recv_line(pSocket, zLine, 2000) ){
    for( i=0; zLine[i] && zLine[i]!='\n' && zLine[i]!='\r'; i++ ){}
    if( i==0 ) break;
    zLine[i] = 0;
    if( strncasecmp(zLine, "http/1.", 7)==0 ){
      if( sscanf(zLine, "HTTP/1.%d %d", &iHttpVersion, &rc)!=2 ) goto write_err;
      if( rc!=200 ) goto write_err;
      if( iHttpVersion==0 ){
        closeConnection = 1;
      }else{
        closeConnection = 0;
      }
    } else if( strncasecmp(zLine, "content-length:", 15)==0 ){
      iLength = atoi(&zLine[16]);
    }else if( strncasecmp(zLine, "connection:", 11)==0 ){
      for(i=12; isspace(zLine[i]); i++){}
      if( zLine[i]=='c' || zLine[i]=='C' ){
        closeConnection = 1;
      }else if( zLine[i]=='k' || zLine[i]=='K' ){
        closeConnection = 0;
      }
    }
  }
  if( iLength<0 ) goto write_err;
  nRead = socket_read_blob(pReply, pSocket, iLength);
#else
  rc = fwrite(blob_buffer(pHeader), 1, blob_size(pHeader), pSocket);
  if( rc!=blob_size(pHeader) ) goto write_err;
  rc = fwrite(blob_buffer(pPayload), 1, blob_size(pPayload), pSocket);
  if( rc!=blob_size(pPayload) ) goto write_err;
  if( fflush(pSocket) ) goto write_err;
  if( fgets(zLine, sizeof(zLine), pSocket)==0 ) goto write_err;
  if( sscanf(zLine, "HTTP/1.%d %d", &iHttpVersion, &rc)!=2 ) goto write_err;
  if( rc!=200 ) goto write_err;
  if( iHttpVersion==0 ){
    closeConnection = 1;   /* Connection: close */
  }else{
    closeConnection = 0;   /* Connection: keep-alive */
  }
  while( fgets(zLine, sizeof(zLine), pSocket) ){
    for(i=0; zLine[i] && zLine[i]!='\n' && zLine[i]!='\r'; i++){}
    if( i==0 ) break;
    zLine[i] = 0;
    if( strncasecmp(zLine,"content-length:",15)==0 ){
      iLength = atoi(&zLine[16]);
    }else if( strncasecmp(zLine, "connection:", 11)==0 ){
      for(i=12; isspace(zLine[i]); i++){}
      if( zLine[i]=='c' || zLine[i]=='C' ){
        closeConnection = 1;   /* Connection: close */
      }else if( zLine[i]=='k' || zLine[i]=='K' ){
        closeConnection = 0;   /* Connection: keep-alive */
      }
    }
  }
  if( iLength<0 ) goto write_err;
  nRead = blob_read_from_channel(pReply, pSocket, iLength);
#endif
  if( nRead!=iLength ){
    blob_reset(pReply);
    goto write_err;
  }
  if( closeConnection ){
    http_close();
  }
  return 1;  

write_err:
  http_close();
  return 0;
}

/*
** Sign the content in pSend, compress it, and send it to the server
** via HTTP.  Get a reply, uncompress the reply, and store the reply
** in pRecv.  pRecv is assumed to be uninitialized when
** this routine is called - this routine will initialize it.
**
** The server address is contain in the "g" global structure.  The
** url_parse() routine should have been called prior to this routine
** in order to fill this structure appropriately.
*/
void http_exchange(Blob *pSend, Blob *pRecv){
  Blob login, nonce, sig, pw, payload, hdr;
  const char *zSep;
  int i;
  int cnt = 0;

  blob_zero(&nonce);
  blob_zero(&pw);
  sha1sum_blob(pSend, &nonce);
  blob_copy(&pw, &nonce);
  blob_zero(&login);
  if( g.urlUser==0 ){
    user_select();
    db_blob(&pw, "SELECT pw FROM user WHERE uid=%d", g.userUid);
    sha1sum_blob(&pw, &sig);
    blob_appendf(&login, "login %s %b %b\n", g.zLogin, &nonce, &sig);
  }else{
    if( g.urlPasswd==0 ){
      if( strcmp(g.urlUser,"anonymous")!=0 ){
        char *zPrompt = mprintf("password for %s: ", g.urlUser);
        Blob x;
        prompt_for_password(zPrompt, &x, 0);
        free(zPrompt);
        g.urlPasswd = blob_str(&x);
      }else{
        g.urlPasswd = "";
      }
    }
    blob_append(&pw, g.urlPasswd, -1);
    /* printf("presig=[%s]\n", blob_str(&pw)); */
    sha1sum_blob(&pw, &sig);
    blob_appendf(&login, "login %s %b %b\n", g.urlUser, &nonce, &sig);
  }        
  blob_reset(&nonce);
  blob_reset(&pw);
  blob_reset(&sig);
  if( g.fHttpTrace ){
    payload = login;
    blob_append(&payload, blob_buffer(pSend), blob_size(pSend));
  }else{
    blob_compress2(&login, pSend, &payload);
    blob_reset(&login);
  }
  blob_zero(&hdr);
  i = strlen(g.urlPath);
  if( i>0 && g.urlPath[i-1]=='/' ){
    zSep = "";
  }else{
    zSep = "/";
  }
  blob_appendf(&hdr, "POST %s%sxfer HTTP/1.1\r\n", g.urlPath, zSep);
  blob_appendf(&hdr, "Host: %s:%d\r\n", g.urlName, g.urlPort);
  if( g.fHttpTrace ){
    blob_appendf(&hdr, "Content-Type: application/x-fossil-debug\r\n");
  }else{
    blob_appendf(&hdr, "Content-Type: application/x-fossil\r\n");
  }
  blob_appendf(&hdr, "Content-Length: %d\r\n\r\n", blob_size(&payload));

  if( g.fHttpTrace ){
    /* When tracing, write the transmitted HTTP message both to standard
    ** output and into a file.  The file can then be used to drive the
    ** server-side like this:
    **
    **      ./fossil http <http-trace-1.txt
    */
    static int traceCnt = 0;
    char *zOutFile;
    FILE *out;
    traceCnt++;
    zOutFile = mprintf("http-trace-%d.txt", traceCnt);
    printf("HTTP SEND: (%s)\n%s%s=======================\n", 
        zOutFile, blob_str(&hdr), blob_str(&payload));
    out = fopen(zOutFile, "w");
    if( out ){
      fwrite(blob_buffer(&hdr), 1, blob_size(&hdr), out);
      fwrite(blob_buffer(&payload), 1, blob_size(&payload), out);
      fclose(out);
    }
  }
  for(cnt=0; cnt<2; cnt++){
    if( http_send_recv(&hdr, &payload, pRecv) ) break;
  }
  if( cnt>=2 ){
    fossil_panic("connection to server failed");
  }
  blob_reset(&hdr);
  blob_reset(&payload);
  if( g.fHttpTrace ){
    printf("HTTP RECEIVE:\n%s\n=======================\n", blob_str(pRecv));
  }else{
    blob_uncompress(pRecv, pRecv);
  }
}


/*
** Make sure the socket to the HTTP server is closed 
*/
void http_close(void){
  if( pSocket ){
#ifdef __MINGW32__
    closesocket(pSocket);
#else
    fclose(pSocket);
#endif
    pSocket = 0;
  }
  /*
  ** This is counter productive. Each time we open a connection we initialize
  ** winsock and then when closing we cleanup. It would be better to
  ** initialize winsock once at application start when we know we are going to
  ** use the socket interface and then cleanup once at application exit when
  ** we are all done with all socket operations.
  */
  ws_cleanup();
}
