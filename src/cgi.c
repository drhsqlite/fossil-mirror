/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** This file began as a set of C functions and procedures used intepret
** CGI environment variables for Fossil web pages that were invoked by
** CGI.  That's where the file name comes from.  But over the years it
** has grown to incorporate lots of related functionality, including:
**
**   *  Interpreting CGI environment variables when Fossil is run as
**      CGI (the original purpose).
**
**   *  Interpreting HTTP requests received directly or via an SSH tunnel.
**
**   *  Interpreting SCGI requests
**
**   *  Generating appropriate replies to CGI, SCGI, and HTTP requests.
**
**   *  Listening for incoming HTTP requests and dispatching them.
**      (Used by "fossil ui" and "fossil server", for example).
**
** So, even though the name of this file implies that it only deals with
** CGI, in fact, the code in this file is used to interpret webpage requests
** received by a variety of means, and to generate well-formatted replies
** to those requests.
**
** The code in this file abstracts the web-request so that downstream
** modules that generate the body of the reply (based on the requested page)
** do not need to know if the request is coming from CGI, direct HTTP,
** SCGI, or some other means.
**
** This module gathers information about web page request into a key/value
** store.  Keys and values come from:
**
**    *  Query parameters
**    *  POST parameter
**    *  Cookies
**    *  Environment variables
**
** The parameters are accessed using cgi_parameter() and similar functions
** or their convenience macros P() and similar.
**
** Environment variable parameters are set as if the request were coming
** in over CGI even if the request arrived via SCGI or direct HTTP.  Thus
** the downstream modules that are trying to interpret the request do not
** need to know the request protocol - they can just request the values
** of environment variables and everything will always work.
**
** This file contains routines used by Fossil when it is acting as a
** CGI client.  For the code used by Fossil when it is acting as a
** CGI server (for the /ext webpage) see the "extcgi.c" source file.
*/
#include "config.h"
#ifdef _WIN32
# if !defined(_WIN32_WINNT)
#  define _WIN32_WINNT 0x0501
# endif
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <sys/socket.h>
# include <sys/un.h>
# include <netinet/in.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <sys/times.h>
# include <sys/time.h>
# include <sys/wait.h>
# include <sys/select.h>
# include <errno.h>
#endif
#ifdef __EMX__
  typedef int socklen_t;
#endif
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "cgi.h"
#include "cygsup.h"

#if INTERFACE
/*
** Shortcuts for cgi_parameter.  P("x") returns the value of query parameter
** or cookie "x", or NULL if there is no such parameter or cookie.  PD("x","y")
** does the same except "y" is returned in place of NULL if there is not match.
*/
#define P(x)          cgi_parameter((x),0)
#define PD(x,y)       cgi_parameter((x),(y))
#define PT(x)         cgi_parameter_trimmed((x),0)
#define PDT(x,y)      cgi_parameter_trimmed((x),(y))
#define PB(x)         cgi_parameter_boolean(x)
#define PCK(x)        cgi_parameter_checked(x,1)
#define PIF(x,y)      cgi_parameter_checked(x,y)
#define P_NoBot(x)    cgi_parameter_no_attack((x),0)
#define PD_NoBot(x,y) cgi_parameter_no_attack((x),(y))

/*
** Shortcut for the cgi_printf() routine.  Instead of using the
**
**    @ ...
**
** notation provided by the translate.c utility, you can also
** optionally use:
**
**    CX(...)
*/
#define CX cgi_printf

/*
** Destinations for output text.
*/
#define CGI_HEADER   0
#define CGI_BODY     1

/*
** Flags for SSH HTTP clients
*/
#define CGI_SSH_CLIENT           0x0001     /* Client is SSH */
#define CGI_SSH_COMPAT           0x0002     /* Compat for old SSH transport */
#define CGI_SSH_FOSSIL           0x0004     /* Use new Fossil SSH transport */

#endif /* INTERFACE */

/*
** The reply content is generated in two pieces: the header and the body.
** These pieces are generated separately because they are not necessarily
** produced in order.  Parts of the header might be built after all or
** part of the body.  The header and body are accumulated in separate
** Blob structures then output sequentially once everything has been
** built.
**
** Do not confuse the content header with the HTTP header. The content header
** is generated by downstream code.  The HTTP header is generated by the
** cgi_reply() routine below.
**
** The content header and contenty body are *approximately* the <head>
** element and the <body> elements for HTML replies.  However this is only
** approximate. The content header also includes parts of <body> that
** show the banner and menu bar at the top of each page.  Also note that
** not all replies are HTML, but there can still be separate header and
** body sections of the content.
**
** The cgi_destination() interface switches between the buffers.
*/
static Blob cgiContent[2] = { BLOB_INITIALIZER, BLOB_INITIALIZER };
static Blob *pContent = &cgiContent[0];

/*
** Set the destination buffer into which to accumulate CGI content.
*/
void cgi_destination(int dest){
  switch( dest ){
    case CGI_HEADER: {
      pContent = &cgiContent[0];
      break;
    }
    case CGI_BODY: {
      pContent = &cgiContent[1];
      break;
    }
    default: {
      cgi_panic("bad destination");
    }
  }
}

/*
** Check to see if the content header or body contains the zNeedle string.
** Return true if it does and false if it does not.
*/
int cgi_header_contains(const char *zNeedle){
  return strstr(blob_str(&cgiContent[0]), zNeedle)!=0;
}
int cgi_body_contains(const char *zNeedle){
  return strstr(blob_str(&cgiContent[1]), zNeedle)!=0;
}

/*
** Append new reply content to what already exists.
*/
void cgi_append_content(const char *zData, int nAmt){
  blob_append(pContent, zData, nAmt);
}

/*
** Reset both reply content buffers to be empty.
*/
void cgi_reset_content(void){
  blob_reset(&cgiContent[0]);
  blob_reset(&cgiContent[1]);
}

/*
** Return a pointer to Blob that is currently accumulating reply content.
*/
Blob *cgi_output_blob(void){
  return pContent;
}

/*
** Return the content header as a text string
*/
const char *cgi_header(void){
  return blob_str(&cgiContent[0]);
}

/*
** Combine the header and body content all into the header buffer.
** In other words, append the body content to the end of the header
** content.
*/
static void cgi_combine_header_and_body(void){
  int size = blob_size(&cgiContent[1]);
  if( size>0 ){
    blob_append(&cgiContent[0], blob_buffer(&cgiContent[1]), size);
    blob_reset(&cgiContent[1]);
  }
}

/*
** Return a pointer to the combined header+body content.
*/
char *cgi_extract_content(void){
  cgi_combine_header_and_body();
  return blob_buffer(&cgiContent[0]);
}

/*
** Additional information used to form the HTTP reply
*/
static const char *zReplyMimeType = "text/html"; /* Content type of the reply */
static const char *zReplyStatus = "OK";          /* Reply status description */
static int iReplyStatus = 200;               /* Reply status code */
static Blob extraHeader = BLOB_INITIALIZER;  /* Extra header text */
static int rangeStart = 0;                   /* Start of Range: */
static int rangeEnd = 0;                     /* End of Range: plus 1 */

/*
** Set the reply content type.
**
** The reply content type defaults to "text/html".  It only needs to be
** changed (by calling this routine) in the exceptional case where some
** other content type is being returned.
*/
void cgi_set_content_type(const char *zType){
  int i;
  for(i=0; zType[i]>='+' && zType[i]<='z'; i++){}
  zReplyMimeType = fossil_strndup(zType, i);
}

/*
** Erase any existing reply content.  Replace is with a pNewContent.
**
** This routine erases pNewContent.  In other words, it move pNewContent
** into the content buffer.
*/
void cgi_set_content(Blob *pNewContent){
  cgi_reset_content();
  cgi_destination(CGI_HEADER);
  cgiContent[0] = *pNewContent;
  blob_zero(pNewContent);
}

/*
** Set the reply status code
*/
void cgi_set_status(int iStat, const char *zStat){
  zReplyStatus = fossil_strdup(zStat);
  iReplyStatus = iStat;
}

/*
** Append text to the content header buffer.
*/
void cgi_append_header(const char *zLine){
  blob_append(&extraHeader, zLine, -1);
}
void cgi_printf_header(const char *zLine, ...){
  va_list ap;
  va_start(ap, zLine);
  blob_vappendf(&extraHeader, zLine, ap);
  va_end(ap);
}

/*
** Set a cookie by queuing up the appropriate HTTP header output. If
** !g.isHTTP, this is a no-op.
**
** Zero lifetime implies a session cookie. A negative one expires
** the cookie immediately.
*/
void cgi_set_cookie(
  const char *zName,    /* Name of the cookie */
  const char *zValue,   /* Value of the cookie.  Automatically escaped */
  const char *zPath,    /* Path cookie applies to.  NULL means "/" */
  int lifetime          /* Expiration of the cookie in seconds from now */
){
  char const *zSecure = "";
  if(!g.isHTTP) return /* e.g. JSON CLI mode, where g.zTop is not set */;
  else if( zPath==0 ){
    zPath = g.zTop;
    if( zPath[0]==0 ) zPath = "/";
  }
  if( g.zBaseURL!=0 && fossil_strncmp(g.zBaseURL, "https:", 6)==0 ){
    zSecure = " secure;";
  }
  if( lifetime!=0 ){
    blob_appendf(&extraHeader,
       "Set-Cookie: %s=%t; Path=%s; max-age=%d; HttpOnly; %s\r\n",
       zName, lifetime>0 ? zValue : "null", zPath, lifetime, zSecure);
  }else{
    blob_appendf(&extraHeader,
       "Set-Cookie: %s=%t; Path=%s; HttpOnly; %s\r\n",
       zName, zValue, zPath, zSecure);
  }
}


/*
** Return true if the response should be sent with Content-Encoding: gzip.
*/
static int is_gzippable(void){
  if( g.fNoHttpCompress ) return 0;
  if( strstr(PD("HTTP_ACCEPT_ENCODING", ""), "gzip")==0 ) return 0;
  /* Maintenance note: this oddball structure is intended to make
  ** adding new mimetypes to this list less of a performance hit than
  ** doing a strcmp/glob over a growing set of compressible types. */
  switch(zReplyMimeType ? *zReplyMimeType : 0){
    case (int)'a':
      if(0==fossil_strncmp("application/",zReplyMimeType,12)){
        const char * z = &zReplyMimeType[12];
        switch(*z){
          case (int)'j':
            return fossil_strcmp("javascript", z)==0
                || fossil_strcmp("json", z)==0;
          case (int)'w': return fossil_strcmp("wasm", z)==0;
          case (int)'x':
            return fossil_strcmp("x-tcl", z)==0
                || fossil_strcmp("x-tar", z)==0;
          default:
            return sqlite3_strglob("*xml", z)==0;
        }
      }
      break;
    case (int)'i':
      return fossil_strcmp(zReplyMimeType, "image/svg+xml")==0
        || fossil_strcmp(zReplyMimeType, "image/vnd.microsoft.icon")==0;
    case (int)'t':
      return fossil_strncmp(zReplyMimeType, "text/", 5)==0;
  }
  return 0;
}


/*
** The following routines read or write content from/to the wire for
** an HTTP request.  Depending on settings the content might be coming
** from or going to a socket, or a file, or it might come from or go
** to an SSL decoder/encoder.
*/
/*
** Works like fgets():
**
** Read a single line of input into s[].  Ensure that s[] is zero-terminated.
** The s[] buffer is size bytes and so at most size-1 bytes will be read.
**
** Return a pointer to s[] on success, or NULL at end-of-input.
*/
static char *cgi_fgets(char *s, int size){
  if( !g.httpUseSSL ){
    return fgets(s, size, g.httpIn);
  }
#ifdef FOSSIL_ENABLE_SSL
  return ssl_gets(g.httpSSLConn, s, size);
#else
  fossil_fatal("SSL not available");
#endif
}

/* Works like fread():
**
** Read as many as bytes of content as we can, up to a maximum of nmemb
** bytes.  Return the number of bytes read.  Return 0 if there is no
** further input or if an I/O error occurs.
*/
size_t cgi_fread(void *ptr, size_t nmemb){
  if( !g.httpUseSSL ){
    return fread(ptr, 1, nmemb, g.httpIn);
  }
#ifdef FOSSIL_ENABLE_SSL
  return ssl_read_server(g.httpSSLConn, ptr, nmemb, 1);
#else
  fossil_fatal("SSL not available");
  /* NOT REACHED */
  return 0;
#endif
}

/* Works like feof():
**
** Return true if end-of-input has been reached.
*/
int cgi_feof(void){
  if( !g.httpUseSSL ){
    return feof(g.httpIn);
  }
#ifdef FOSSIL_ENABLE_SSL
  return ssl_eof(g.httpSSLConn);
#else
  return 1;
#endif
}

/* Works like fwrite():
**
** Try to output nmemb bytes of content.  Return the number of
** bytes actually written.
*/
static size_t cgi_fwrite(void *ptr, size_t nmemb){
  if( !g.httpUseSSL ){
    return fwrite(ptr, 1, nmemb, g.httpOut);
  }
#ifdef FOSSIL_ENABLE_SSL
  return ssl_write_server(g.httpSSLConn, ptr, nmemb);
#else
  fossil_fatal("SSL not available");
#endif
}

/* Works like fflush():
**
** Make sure I/O has completed.
*/
static void cgi_fflush(void){
  if( !g.httpUseSSL ){
    fflush(g.httpOut);
  }
}

/*
** Given a Content-Type value, returns a string suitable for appending
** to the Content-Type header for adding (or not) the "; charset=..."
** part. It returns an empty string for most types or if zReplyMimeType
** is NULL.
**
** See forum post f60dece061c364d1 for the discussions which lead to
** this. Previously we always appended the charset, but WASM loaders
** are pedantic and refuse to load any responses which have a
** charset. Also, adding a charset is not strictly appropriate for
** most types (and not required for many others which may ostensibly
** benefit from one, as detailed in that forum post).
*/
static const char * content_type_charset(const char *zReplyMimeType){
  if(0==fossil_strncmp(zReplyMimeType,"text/",5)){
    return "; charset=utf-8";
  }
  return "";
}

/*
** Generate the reply to a web request.  The output might be an
** full HTTP response, or a CGI response, depending on how things have
** be set up.
**
** The reply consists of a response header (an HTTP or CGI response header)
** followed by the concatenation of the content header and content body.
*/
void cgi_reply(void){
  Blob hdr = BLOB_INITIALIZER;
  int total_size;
  if( iReplyStatus<=0 ){
    iReplyStatus = 200;
    zReplyStatus = "OK";
  }

  if( g.fullHttpReply ){
    if( rangeEnd>0
     && iReplyStatus==200
     && fossil_strcmp(P("REQUEST_METHOD"),"GET")==0
    ){
      iReplyStatus = 206;
      zReplyStatus = "Partial Content";
    }
    blob_appendf(&hdr, "HTTP/1.0 %d %s\r\n", iReplyStatus, zReplyStatus);
    blob_appendf(&hdr, "Date: %s\r\n", cgi_rfc822_datestamp(time(0)));
    blob_appendf(&hdr, "Connection: close\r\n");
    blob_appendf(&hdr, "X-UA-Compatible: IE=edge\r\n");
  }else{
    assert( rangeEnd==0 );
    blob_appendf(&hdr, "Status: %d %s\r\n", iReplyStatus, zReplyStatus);
  }
  if( etag_tag()[0]!=0
   && iReplyStatus==200
   && strcmp(zReplyMimeType,"text/html")!=0
  ){
    /* Do not cache HTML replies as those will have been generated and
    ** will likely, therefore, contains a nonce and we want that nonce to
    ** be different every time. */
    blob_appendf(&hdr, "ETag: \"%s\"\r\n", etag_tag());
    blob_appendf(&hdr, "Cache-Control: max-age=%d\r\n", etag_maxage());
    if( etag_mtime()>0 ){
      blob_appendf(&hdr, "Last-Modified: %s\r\n",
              cgi_rfc822_datestamp(etag_mtime()));
    }
  }else if( g.isConst ){
    /* isConst means that the reply is guaranteed to be invariant, even
    ** after configuration changes and/or Fossil binary recompiles. */
    blob_appendf(&hdr, "Cache-Control: max-age=315360000, immutable\r\n");
  }else{
    blob_appendf(&hdr, "Cache-control: no-cache\r\n");
  }

  if( blob_size(&extraHeader)>0 ){
    blob_appendf(&hdr, "%s", blob_buffer(&extraHeader));
  }

  /* Add headers to turn on useful security options in browsers. */
  blob_appendf(&hdr, "X-Frame-Options: SAMEORIGIN\r\n");
  /* The previous stops fossil pages appearing in frames or iframes, preventing
  ** click-jacking attacks on supporting browsers.
  **
  ** Other good headers would be
  **   Strict-Transport-Security: max-age=62208000
  ** if we're using https. However, this would break sites which serve different
  ** content on http and https protocols. Also,
  **   X-Content-Security-Policy: allow 'self'
  ** would help mitigate some XSS and data injection attacks, but will break
  ** deliberate inclusion of external resources, such as JavaScript syntax
  ** highlighter scripts.
  **
  ** These headers are probably best added by the web server hosting fossil as
  ** a CGI script.
  */

  if( iReplyStatus!=304 ) {
    blob_appendf(&hdr, "Content-Type: %s%s\r\n", zReplyMimeType,
                 content_type_charset(zReplyMimeType));
    if( fossil_strcmp(zReplyMimeType,"application/x-fossil")==0 ){
      cgi_combine_header_and_body();
      blob_compress(&cgiContent[0], &cgiContent[0]);
    }

    if( is_gzippable() && iReplyStatus!=206 ){
      int i;
      gzip_begin(0);
      for( i=0; i<2; i++ ){
        int size = blob_size(&cgiContent[i]);
        if( size>0 ) gzip_step(blob_buffer(&cgiContent[i]), size);
        blob_reset(&cgiContent[i]);
      }
      gzip_finish(&cgiContent[0]);
      blob_appendf(&hdr, "Content-Encoding: gzip\r\n");
      blob_appendf(&hdr, "Vary: Accept-Encoding\r\n");
    }
    total_size = blob_size(&cgiContent[0]) + blob_size(&cgiContent[1]);
    if( iReplyStatus==206 ){
      blob_appendf(&hdr, "Content-Range: bytes %d-%d/%d\r\n",
              rangeStart, rangeEnd-1, total_size);
      total_size = rangeEnd - rangeStart;
    }
    blob_appendf(&hdr, "Content-Length: %d\r\n", total_size);
  }else{
    total_size = 0;
  }
  blob_appendf(&hdr, "\r\n");
  cgi_fwrite(blob_buffer(&hdr), blob_size(&hdr));
  blob_reset(&hdr);
  if( total_size>0
   && iReplyStatus!=304
   && fossil_strcmp(P("REQUEST_METHOD"),"HEAD")!=0
  ){
    int i, size;
    for(i=0; i<2; i++){
      size = blob_size(&cgiContent[i]);
      if( size<=rangeStart ){
        rangeStart -= size;
      }else{
        int n = size - rangeStart;
        if( n>total_size ){
          n = total_size;
        }
        cgi_fwrite(blob_buffer(&cgiContent[i])+rangeStart, n);
        rangeStart = 0;
        total_size -= n;
      }
    }
  }
  cgi_fflush();
  CGIDEBUG(("-------- END cgi ---------\n"));

  /* After the webpage has been sent, do any useful background
  ** processing.
  */
  g.cgiOutput = 2;
  if( g.db!=0 && iReplyStatus==200 ){
    backoffice_check_if_needed();
  }
}

/*
** Generate an HTTP or CGI redirect response that causes a redirect
** to the URL given in the argument.
**
** The URL must be relative to the base of the fossil server.
*/
NORETURN void cgi_redirect_with_status(
  const char *zURL,
  int iStat,
  const char *zStat
){
  char *zLocation;
  CGIDEBUG(("redirect to %s\n", zURL));
  if( fossil_strncmp(zURL,"http:",5)==0
      || fossil_strncmp(zURL,"https:",6)==0 ){
    zLocation = mprintf("Location: %s\r\n", zURL);
  }else if( *zURL=='/' ){
    int n1 = (int)strlen(g.zBaseURL);
    int n2 = (int)strlen(g.zTop);
    if( g.zBaseURL[n1-1]=='/' ) zURL++;
    zLocation = mprintf("Location: %.*s%s\r\n", n1-n2, g.zBaseURL, zURL);
  }else{
    zLocation = mprintf("Location: %s/%s\r\n", g.zBaseURL, zURL);
  }
  cgi_append_header(zLocation);
  cgi_reset_content();
  cgi_printf("<html>\n<p>Redirect to %h</p>\n</html>\n", zLocation);
  cgi_set_status(iStat, zStat);
  free(zLocation);
  cgi_reply();
  fossil_exit(0);
}
NORETURN void cgi_redirect_perm(const char *zURL){
  cgi_redirect_with_status(zURL, 301, "Moved Permanently");
}
NORETURN void cgi_redirect(const char *zURL){
  cgi_redirect_with_status(zURL, 302, "Moved Temporarily");
}
NORETURN void cgi_redirect_with_method(const char *zURL){
  cgi_redirect_with_status(zURL, 307, "Temporary Redirect");
}
NORETURN void cgi_redirectf(const char *zFormat, ...){
  va_list ap;
  va_start(ap, zFormat);
  cgi_redirect(vmprintf(zFormat, ap));
  va_end(ap);
}

/*
** Add a "Content-disposition: attachment; filename=%s" header to the reply.
*/
void cgi_content_disposition_filename(const char *zFilename){
  char *z;
  int i, n;

           /*  0123456789 123456789 123456789 123456789 123456*/
  z = mprintf("Content-Disposition: attachment; filename=\"%s\";\r\n",
                    file_tail(zFilename));
  n = (int)strlen(z);
  for(i=43; i<n-4; i++){
    char c = z[i];
    if( fossil_isalnum(c) ) continue;
    if( c=='.' || c=='-' || c=='/' ) continue;
    z[i] = '_';
  }
  cgi_append_header(z);
  fossil_free(z);
}

/*
** Return the URL for the caller.  This is obtained from either the
** "referer" CGI parameter, if it exists, or the HTTP_REFERER HTTP parameter.
** If neither exist, return zDefault.
*/
const char *cgi_referer(const char *zDefault){
  const char *zRef = P("referer");
  if( zRef==0 ){
    zRef = P("HTTP_REFERER");
    if( zRef==0 ) zRef = zDefault;
  }
  return zRef;
}


/*
** Return true if the current request is coming from the same origin.
**
** If the request comes from a different origin and bErrorLog is true, then
** put a warning message on the error log as this was a possible hack
** attempt.
*/
int cgi_same_origin(int bErrorLog){
  const char *zRef;
  char *zToFree = 0;
  int nBase;
  int rc;
  if( g.zBaseURL==0 ) return 0;
  zRef = P("HTTP_REFERER");
  if( zRef==0 ) return 0;
  if( strchr(zRef,'%')!=0 ){
    zToFree = strdup(zRef);
    dehttpize(zToFree);
    zRef = zToFree;
  }
  nBase = (int)strlen(g.zBaseURL);
  if( fossil_strncmp(g.zBaseURL,zRef,nBase)!=0 ){
    rc = 0;
  }else if( zRef[nBase]!=0 && zRef[nBase]!='/' ){
    rc = 0;
  }else{
    rc = 1;
  }
  if( rc==0 && bErrorLog && fossil_strcmp(P("REQUST_METHOD"),"POST")==0 ){
    fossil_errorlog("warning: POST from different origin");
  }
  fossil_free(zToFree);
  return rc;
}

/*
** Return true if the current CGI request is a POST request
*/
static int cgi_is_post_request(void){
  const char *zMethod = P("REQUEST_METHOD");
  if( zMethod==0 ) return 0;
  if( strcmp(zMethod,"POST")!=0 ) return 0;
  return  1;
}

/*
** Return true if the current request appears to be safe from a
** Cross-Site Request Forgery (CSRF) attack.  The level of checking
** is determined by the parameter.  The higher the number, the more
** secure we are:
**
**    0:     Request must come from the same origin
**    1:     Same origin and must be a POST request
**    2:     All of the above plus must have a valid CSRF token
**
** Results are cached in the g.okCsrf variable.  The g.okCsrf value
** has meaning as follows:
**
**    -1:   Not a secure request
**     0:   Status unknown
**     1:   Request comes from the same origin
**     2:   (1) plus it is a POST request
**     3:   (2) plus there is a valid "csrf" token in the request
*/
int cgi_csrf_safe(int securityLevel){
  if( g.okCsrf<0 ) return 0;
  if( g.okCsrf==0 ){
    if( !cgi_same_origin(1) ){
      g.okCsrf = -1;
    }else{
      g.okCsrf = 1;
      if( cgi_is_post_request() ){
        g.okCsrf = 2;
        if( fossil_strcmp(P("csrf"), g.zCsrfToken)==0 ){
          g.okCsrf = 3;
        }
      }
    }
  }
  return g.okCsrf >= (securityLevel+1);
}

/*
** Verify that CSRF defenses are maximal - that the request comes from
** the same origin, that it is a POST request, and that there is a valid
** "csrf" token.  If this is not the case, fail immediately.
*/
void cgi_csrf_verify(void){
  if( !cgi_csrf_safe(2) ){
    fossil_fatal("Cross-site Request Forgery detected");
  }
}

/*
** Information about all query parameters, post parameter, cookies and
** CGI environment variables are stored in a hash table as follows:
*/
static int nAllocQP = 0; /* Space allocated for aParamQP[] */
static int nUsedQP = 0;  /* Space actually used in aParamQP[] */
static int sortQP = 0;   /* True if aParamQP[] needs sorting */
static int seqQP = 0;    /* Sequence numbers */
static struct QParam {   /* One entry for each query parameter or cookie */
  const char *zName;        /* Parameter or cookie name */
  const char *zValue;       /* Value of the query parameter or cookie */
  int seq;                  /* Order of insertion */
  char isQP;                /* True for query parameters */
  char cTag;                /* Tag on query parameters */
  char isFetched;           /* 1 if the var is requested via P/PD() */
} *aParamQP;             /* An array of all parameters and cookies */

/*
** Add another query parameter or cookie to the parameter set.
** zName is the name of the query parameter or cookie and zValue
** is its fully decoded value.
**
** zName and zValue are not copied and must not change or be
** deallocated after this routine returns.
*/
void cgi_set_parameter_nocopy(const char *zName, const char *zValue, int isQP){
  if( nAllocQP<=nUsedQP ){
    nAllocQP = nAllocQP*2 + 10;
    if( nAllocQP>1000 ){
      /* Prevent a DOS service attack against the framework */
      fossil_fatal("Too many query parameters");
    }
    aParamQP = fossil_realloc( aParamQP, nAllocQP*sizeof(aParamQP[0]) );
  }
  aParamQP[nUsedQP].zName = zName;
  aParamQP[nUsedQP].zValue = zValue;
  if( g.fHttpTrace ){
    fprintf(stderr, "# cgi: %s = [%s]\n", zName, zValue);
  }
  aParamQP[nUsedQP].seq = seqQP++;
  aParamQP[nUsedQP].isQP = isQP;
  aParamQP[nUsedQP].cTag = 0;
  aParamQP[nUsedQP].isFetched = 0;
  nUsedQP++;
  sortQP = 1;
}

/*
** Add another query parameter or cookie to the parameter set.
** zName is the name of the query parameter or cookie and zValue
** is its fully decoded value.  zName will be modified to be an
** all lowercase string.
**
** zName and zValue are not copied and must not change or be
** deallocated after this routine returns.  This routine changes
** all ASCII alphabetic characters in zName to lower case.  The
** caller must not change them back.
*/
void cgi_set_parameter_nocopy_tolower(
  char *zName,
  const char *zValue,
  int isQP
){
  int i;
  for(i=0; zName[i]; i++){ zName[i] = fossil_tolower(zName[i]); }
  cgi_set_parameter_nocopy(zName, zValue, isQP);
}

/*
** Add another query parameter or cookie to the parameter set.
** zName is the name of the query parameter or cookie and zValue
** is its fully decoded value.
**
** Copies are made of both the zName and zValue parameters.
*/
void cgi_set_parameter(const char *zName, const char *zValue){
  cgi_set_parameter_nocopy(fossil_strdup(zName),fossil_strdup(zValue), 0);
}
void cgi_set_query_parameter(const char *zName, const char *zValue){
  cgi_set_parameter_nocopy(fossil_strdup(zName),fossil_strdup(zValue), 1);
}

/*
** Replace a parameter with a new value.
*/
void cgi_replace_parameter(const char *zName, const char *zValue){
  int i;
  for(i=0; i<nUsedQP; i++){
    if( fossil_strcmp(aParamQP[i].zName,zName)==0 ){
      aParamQP[i].zValue = zValue;
      return;
    }
  }
  cgi_set_parameter_nocopy(zName, zValue, 0);
}
void cgi_replace_query_parameter(const char *zName, const char *zValue){
  int i;
  for(i=0; i<nUsedQP; i++){
    if( fossil_strcmp(aParamQP[i].zName,zName)==0 ){
      aParamQP[i].zValue = zValue;
      assert( aParamQP[i].isQP );
      return;
    }
  }
  cgi_set_parameter_nocopy(zName, zValue, 1);
}
void cgi_replace_query_parameter_tolower(char *zName, const char *zValue){
  int i;
  for(i=0; zName[i]; i++){ zName[i] = fossil_tolower(zName[i]); }
  cgi_replace_query_parameter(zName, zValue);
}

/*
** Delete a parameter.
*/
void cgi_delete_parameter(const char *zName){
  int i;
  for(i=0; i<nUsedQP; i++){
    if( fossil_strcmp(aParamQP[i].zName,zName)==0 ){
      --nUsedQP;
      if( i<nUsedQP ){
        memmove(aParamQP+i, aParamQP+i+1, sizeof(*aParamQP)*(nUsedQP-i));
      }
      return;
    }
  }
}
void cgi_delete_query_parameter(const char *zName){
  int i;
  for(i=0; i<nUsedQP; i++){
    if( fossil_strcmp(aParamQP[i].zName,zName)==0 ){
      assert( aParamQP[i].isQP );
      --nUsedQP;
      if( i<nUsedQP ){
        memmove(aParamQP+i, aParamQP+i+1, sizeof(*aParamQP)*(nUsedQP-i));
      }
      return;
    }
  }
}

/*
** Return the number of query parameters.  Cookies and environment variables
** do not count.  Also, do not count the special QP "name".
*/
int cgi_qp_count(void){
  int cnt = 0;
  int i;
  for(i=0; i<nUsedQP; i++){
    if( aParamQP[i].isQP && fossil_strcmp(aParamQP[i].zName,"name")!=0 ) cnt++;
  }
  return cnt;
}

/*
** Add an environment varaible value to the parameter set.  The zName
** portion is fixed but a copy is be made of zValue.
*/
void cgi_setenv(const char *zName, const char *zValue){
  cgi_set_parameter_nocopy(zName, fossil_strdup(zValue), 0);
}

/*
** Returns true if NUL-terminated z contains any non-NUL
** control characters (<0x20, 32d).
*/
static int contains_ctrl(const char *z){
  assert(z);
  for( ; *z>=0x20; ++z ){}
  return 0!=*z;
}

/*
** Add a list of query parameters or cookies to the parameter set.
**
** Each parameter is of the form NAME=VALUE.  Both the NAME and the
** VALUE may be url-encoded ("+" for space, "%HH" for other special
** characters).  But this routine assumes that NAME contains no
** special character and therefore does not decode it.
**
** If NAME begins with another other than a lower-case letter then
** the entire NAME=VALUE term is ignored.  Hence:
**
**      *  cookies and query parameters that have uppercase names
**         are ignored.
**
**      *  it is impossible for a cookie or query parameter to
**         override the value of an environment variable since
**         environment variables always have uppercase names.
**
** 2018-03-29:  Also ignore the entry if NAME that contains any characters
** other than [-a-zA-Z0-9_].  There are no known exploits involving unusual
** names that contain characters outside that set, but it never hurts to
** be extra cautious when sanitizing inputs.
**
** Parameters are separated by the "terminator" character.  Whitespace
** before the NAME is ignored.
**
** The input string "z" is modified but no copies is made.  "z"
** should not be deallocated or changed again after this routine
** returns or it will corrupt the parameter table.
**
** If bPermitCtrl is false and the decoded value of any entry in z
** contains control characters (<0x20, 32d) then that key/value pair
** are skipped.
*/
static void add_param_list(char *z, int terminator, int bPermitCtrl){
  int isQP = terminator=='&';
  while( *z ){
    char *zName;
    char *zValue;
    while( fossil_isspace(*z) ){ z++; }
    zName = z;
    while( *z && *z!='=' && *z!=terminator ){ z++; }
    if( *z=='=' ){
      *z = 0;
      z++;
      zValue = z;
      while( *z && *z!=terminator ){ z++; }
      if( *z ){
        *z = 0;
        z++;
      }
      dehttpize(zValue);
    }else{
      if( *z ){ *z++ = 0; }
      zValue = "";
    }
    if( zName[0] && fossil_no_strange_characters(zName+1) ){
      if( 0==bPermitCtrl && contains_ctrl(zValue) ){
        continue /* Reject it. An argument could be made
                 ** for break instead of continue. */;
      }else if( fossil_islower(zName[0]) ){
        cgi_set_parameter_nocopy(zName, zValue, isQP);
      }else if( fossil_isupper(zName[0]) ){
        cgi_set_parameter_nocopy_tolower(zName, zValue, isQP);
      }
    }
#ifdef FOSSIL_ENABLE_JSON
    json_setenv( zName, cson_value_new_string(zValue,strlen(zValue)) );
#endif /* FOSSIL_ENABLE_JSON */
  }
}

/*
** *pz is a string that consists of multiple lines of text.  This
** routine finds the end of the current line of text and converts
** the "\n" or "\r\n" that ends that line into a "\000".  It then
** advances *pz to the beginning of the next line and returns the
** previous value of *pz (which is the start of the current line.)
*/
static char *get_line_from_string(char **pz, int *pLen){
  char *z = *pz;
  int i;
  if( z[0]==0 ) return 0;
  for(i=0; z[i]; i++){
    if( z[i]=='\n' ){
      if( i>0 && z[i-1]=='\r' ){
        z[i-1] = 0;
      }else{
        z[i] = 0;
      }
      i++;
      break;
    }
  }
  *pz = &z[i];
  *pLen -= i;
  return z;
}

/*
** The input *pz points to content that is terminated by a "\r\n"
** followed by the boundary marker zBoundary.  An extra "--" may or
** may not be appended to the boundary marker.  There are *pLen characters
** in *pz.
**
** This routine adds a "\000" to the end of the content (overwriting
** the "\r\n") and returns a pointer to the content.  The *pz input
** is adjusted to point to the first line following the boundary.
** The length of the content is stored in *pnContent.
*/
static char *get_bounded_content(
  char **pz,         /* Content taken from here */
  int *pLen,         /* Number of bytes of data in (*pz)[] */
  char *zBoundary,    /* Boundary text marking the end of content */
  int *pnContent     /* Write the size of the content here */
){
  char *z = *pz;
  int len = *pLen;
  int i;
  int nBoundary = strlen(zBoundary);
  *pnContent = len;
  for(i=0; i<len; i++){
    if( z[i]=='\n' && fossil_strncmp(zBoundary, &z[i+1],
                                     nBoundary)==0 ){
      if( i>0 && z[i-1]=='\r' ) i--;
      z[i] = 0;
      *pnContent = i;
      i += nBoundary;
      break;
    }
  }
  *pz = &z[i];
  get_line_from_string(pz, pLen);
  return z;
}

/*
** Tokenize a line of text into as many as nArg tokens.  Make
** azArg[] point to the start of each token.
**
** Tokens consist of space or semi-colon delimited words or
** strings inside double-quotes.  Example:
**
**    content-disposition: form-data; name="fn"; filename="index.html"
**
** The line above is tokenized as follows:
**
**    azArg[0] = "content-disposition:"
**    azArg[1] = "form-data"
**    azArg[2] = "name="
**    azArg[3] = "fn"
**    azArg[4] = "filename="
**    azArg[5] = "index.html"
**    azArg[6] = 0;
**
** '\000' characters are inserted in z[] at the end of each token.
** This routine returns the total number of tokens on the line, 6
** in the example above.
*/
static int tokenize_line(char *z, int mxArg, char **azArg){
  int i = 0;
  while( *z ){
    while( fossil_isspace(*z) || *z==';' ){ z++; }
    if( *z=='"' && z[1] ){
      *z = 0;
      z++;
      if( i<mxArg-1 ){ azArg[i++] = z; }
      while( *z && *z!='"' ){ z++; }
      if( *z==0 ) break;
      *z = 0;
      z++;
    }else{
      if( i<mxArg-1 ){ azArg[i++] = z; }
      while( *z && !fossil_isspace(*z) && *z!=';' && *z!='"' ){ z++; }
      if( *z && *z!='"' ){
        *z = 0;
        z++;
      }
    }
  }
  azArg[i] = 0;
  return i;
}

/*
** Scan the multipart-form content and make appropriate entries
** into the parameter table.
**
** The content string "z" is modified by this routine but it is
** not copied.  The calling function must not deallocate or modify
** "z" after this routine finishes or it could corrupt the parameter
** table.
*/
static void process_multipart_form_data(char *z, int len){
  char *zLine;
  int nArg, i;
  char *zBoundary;
  char *zValue;
  char *zName = 0;
  int showBytes = 0;
  char *azArg[50];

  zBoundary = get_line_from_string(&z, &len);
  if( zBoundary==0 ) return;
  while( (zLine = get_line_from_string(&z, &len))!=0 ){
    if( zLine[0]==0 ){
      int nContent = 0;
      zValue = get_bounded_content(&z, &len, zBoundary, &nContent);
      if( zName && zValue ){
        if( fossil_islower(zName[0]) ){
          cgi_set_parameter_nocopy(zName, zValue, 1);
          if( showBytes ){
            cgi_set_parameter_nocopy(mprintf("%s:bytes", zName),
                 mprintf("%d",nContent), 1);
          }
        }else if( fossil_isupper(zName[0]) ){
          cgi_set_parameter_nocopy_tolower(zName, zValue, 1);
          if( showBytes ){
            cgi_set_parameter_nocopy_tolower(mprintf("%s:bytes", zName),
                 mprintf("%d",nContent), 1);
          }
        }
      }
      zName = 0;
      showBytes = 0;
    }else{
      nArg = tokenize_line(zLine, count(azArg), azArg);
      for(i=0; i<nArg; i++){
        int c = fossil_tolower(azArg[i][0]);
        int n = strlen(azArg[i]);
        if( c=='c' && sqlite3_strnicmp(azArg[i],"content-disposition:",n)==0 ){
          i++;
        }else if( c=='n' && sqlite3_strnicmp(azArg[i],"name=",n)==0 ){
          zName = azArg[++i];
        }else if( c=='f' && sqlite3_strnicmp(azArg[i],"filename=",n)==0 ){
          char *z = azArg[++i];
          if( zName && z ){
            if( fossil_islower(zName[0]) ){
              cgi_set_parameter_nocopy(mprintf("%s:filename",zName), z, 1);
            }else if( fossil_isupper(zName[0]) ){
              cgi_set_parameter_nocopy_tolower(mprintf("%s:filename",zName),
                                               z, 1);
            }
          }
          showBytes = 1;
        }else if( c=='c' && sqlite3_strnicmp(azArg[i],"content-type:",n)==0 ){
          char *z = azArg[++i];
          if( zName && z ){
            if( fossil_islower(zName[0]) ){
              cgi_set_parameter_nocopy(mprintf("%s:mimetype",zName), z, 1);
            }else if( fossil_isupper(zName[0]) ){
              cgi_set_parameter_nocopy_tolower(mprintf("%s:mimetype",zName),
                                               z, 1);
            }
          }
        }
      }
    }
  }
}


#ifdef FOSSIL_ENABLE_JSON
/*
** Reads a JSON object from the given blob, which is assumed to have
** been populated by the caller from stdin, the SSL API, or a file, as
** appropriate for the particular use case. On success g.json.post is
** updated to hold the content. On error a FSL_JSON_E_INVALID_REQUEST
** response is output and fossil_exit() is called (in HTTP mode exit
** code 0 is used).
*/
void cgi_parse_POST_JSON( Blob * pIn ){
  cson_value * jv = NULL;
  cson_parse_info pinfo = cson_parse_info_empty;
  assert(g.json.gc.a && "json_bootstrap_early() was not called!");
  jv = cson_parse_Blob(pIn, &pinfo);
  if( jv==NULL ){
    goto invalidRequest;
  }else{
    json_gc_add( "POST.JSON", jv );
    g.json.post.v = jv;
    g.json.post.o = cson_value_get_object( jv );
    if( !g.json.post.o ){ /* we don't support non-Object (Array) requests */
      goto invalidRequest;
    }
  }
  return;
  invalidRequest:
  cgi_set_content_type(json_guess_content_type());
  if(0 != pinfo.errorCode){ /* fancy error message */
      char * msg = mprintf("JSON parse error at line %u, column %u, "
                           "byte offset %u: %s",
                           pinfo.line, pinfo.col, pinfo.length,
                           cson_rc_string(pinfo.errorCode));
      json_err( FSL_JSON_E_INVALID_REQUEST, msg, 1 );
      fossil_free(msg);
  }else if(jv && !g.json.post.o){
      json_err( FSL_JSON_E_INVALID_REQUEST,
                "Request envelope must be a JSON Object (not array).", 1 );
  }else{ /* generic error message */
      json_err( FSL_JSON_E_INVALID_REQUEST, NULL, 1 );
  }
  fossil_exit( g.isHTTP ? 0 : 1);
}
#endif /* FOSSIL_ENABLE_JSON */

/*
** Log HTTP traffic to a file.  Begin the log on first use.  Close the log
** when the argument is NULL.
*/
void cgi_trace(const char *z){
  static FILE *pLog = 0;
  if( g.fHttpTrace==0 ) return;
  if( z==0 ){
    if( pLog ) fclose(pLog);
    pLog = 0;
    return;
  }
  if( pLog==0 ){
    char zFile[50];
#if defined(_WIN32)
    unsigned r;
    sqlite3_randomness(sizeof(r), &r);
    sqlite3_snprintf(sizeof(zFile), zFile, "httplog-%08x.txt", r);
#else
    sqlite3_snprintf(sizeof(zFile), zFile, "httplog-%05d.txt", getpid());
#endif
    pLog = fossil_fopen(zFile, "wb");
    if( pLog ){
      fprintf(stderr, "# open log on %s\n", zFile);
    }else{
      fprintf(stderr, "# failed to open %s\n", zFile);
      return;
    }
  }
  fputs(z, pLog);
}

/* Forward declaration */
static NORETURN void malformed_request(const char *zMsg, ...);

/*
** Checks the QUERY_STRING environment variable, sets it up via
** add_param_list() and, if found, applies its "skin" setting. Returns
** 0 if no QUERY_STRING is set, else it returns a bitmask of:
**
** 0x01 = QUERY_STRING was set up
** 0x02 = "skin" URL param arg was processed
** 0x04 = "x-f-l-c" cookie arg was processed.
**
*  In the case of the skin, the cookie may still need flushing
** by the page, via cookie_render().
*/
int cgi_setup_query_string(void){
  int rc = 0;
  char * z = (char*)P("QUERY_STRING");
  if( z ){
    rc = 0x01;
    z = fossil_strdup(z);
    add_param_list(z, '&', 0);
    z = (char*)P("skin");
    if( z ){
      char *zErr = skin_use_alternative(z, 2, SKIN_FROM_QPARAM);
      rc |= 0x02;
      if( !zErr && P("once")==0 ){
        cookie_write_parameter("skin","skin",z);
        /* Per /chat discussion, passing ?skin=... without "once"
        ** implies the "udc" argument, so we force that into the
        ** environment here. */
        cgi_set_parameter_nocopy("udc", "1", 1);
      }
      fossil_free(zErr);
    }
  }
  if( !g.syncInfo.zLoginCard && 0!=(z=(char*)P("x-f-l-c")) ){
    /* x-f-l-c (X-Fossil-Login-Card card transmitted via cookie
    ** instead of in the sync payload. */
    rc |= 0x04;
    g.syncInfo.zLoginCard = fossil_strdup(z);
    g.syncInfo.fLoginCardMode |= 0x02;
    cgi_delete_parameter("x-f-l-c");
  }
  return rc;
}

/*
** Initialize the query parameter database.  Information is pulled from
** the QUERY_STRING environment variable (if it exists), from standard
** input if there is POST data, and from HTTP_COOKIE.
**
** REQUEST_URI, PATH_INFO, and SCRIPT_NAME are related as follows:
**
**      REQUEST_URI == SCRIPT_NAME + PATH_INFO
**
** Or if QUERY_STRING is not empty:
**
**      REQUEST_URI == SCRIPT_NAME + PATH_INFO + '?' + QUERY_STRING
**
** Where "+" means concatenate.  Fossil requires SCRIPT_NAME.  If
** REQUEST_URI is provided but PATH_INFO is not, then PATH_INFO is
** computed from REQUEST_URI and SCRIPT_NAME.  If PATH_INFO is provided
** but REQUEST_URI is not, then compute REQUEST_URI from PATH_INFO and
** SCRIPT_NAME.  If neither REQUEST_URI nor PATH_INFO are provided, then
** assume that PATH_INFO is an empty string and set REQUEST_URI equal
** to PATH_INFO.
**
** Sometimes PATH_INFO is missing and SCRIPT_NAME is not a prefix of
** REQUEST_URI.  (See https://fossil-scm.org/forum/forumpost/049e8650ed)
** In that case, truncate SCRIPT_NAME so that it is a proper prefix
** of REQUEST_URI.
**
** SCGI typically omits PATH_INFO.  CGI sometimes omits REQUEST_URI and
** PATH_INFO when it is empty.
**
** CGI Parameter quick reference:
**
**                                   REQUEST_URI
**                           _____________|________________
**                          /                              \
**    https://fossil-scm.org/forum/info/12736b30c072551a?t=c
**    \___/   \____________/\____/\____________________/ \_/
**      |           |          |             |            |
**      |       HTTP_HOST      |        PATH_INFO     QUERY_STRING
**      |                      |
**    REQUEST_SCHEMA         SCRIPT_NAME
**
*/
void cgi_init(void){
  char *z;
  const char *zType;
  char *zSemi;
  int len;
  const char *zRequestUri = cgi_parameter("REQUEST_URI",0);
  const char *zScriptName = cgi_parameter("SCRIPT_NAME",0);
  const char *zPathInfo = cgi_parameter("PATH_INFO",0);
  const char *zContentLength = 0;
#ifdef _WIN32
  const char *zServerSoftware = cgi_parameter("SERVER_SOFTWARE",0);
#endif

#ifdef FOSSIL_ENABLE_JSON
  const int noJson = P("no_json")!=0;
#endif
  g.isHTTP = 1;
  cgi_destination(CGI_BODY);

  /* We must have SCRIPT_NAME. If the web server did not supply it, try
  ** to compute it from REQUEST_URI and PATH_INFO. */
  if( zScriptName==0 ){
    if( zRequestUri==0 || zPathInfo==0 ){
      malformed_request("missing SCRIPT_NAME");  /* Does not return */
    }
    z = strstr(zRequestUri,zPathInfo);
    if( z==0 ){
      malformed_request("PATH_INFO not found in REQUEST_URI");
    }
    zScriptName = fossil_strndup(zRequestUri,(int)(z-zRequestUri));
    cgi_set_parameter("SCRIPT_NAME", zScriptName);
  }

#ifdef _WIN32
  /* The Microsoft IIS web server does not define REQUEST_URI, instead it uses
  ** PATH_INFO for virtually the same purpose.  Define REQUEST_URI the same as
  ** PATH_INFO and redefine PATH_INFO with SCRIPT_NAME removed from the
  ** beginning. */
  if( zServerSoftware && strstr(zServerSoftware, "Microsoft-IIS") ){
    int i, j;
    cgi_set_parameter("REQUEST_URI", zPathInfo);
    for(i=0; zPathInfo[i]==zScriptName[i] && zPathInfo[i]; i++){}
    for(j=i; zPathInfo[j] && zPathInfo[j]!='?'; j++){}
    zPathInfo = fossil_strndup(zPathInfo+i, j-i);
    cgi_replace_parameter("PATH_INFO", zPathInfo);
  }
#endif
  if( zRequestUri==0 ){
    const char *z = zPathInfo;
    const char *zQS = cgi_parameter("QUERY_STRING",0);
    if( zPathInfo==0 ){
      malformed_request("missing PATH_INFO and/or REQUEST_URI");
    }
    if( z[0]=='/' ) z++;
    if( zQS && zQS[0] ){
      zRequestUri = mprintf("%s/%s?%s", zScriptName, z, zQS);
    }else{
      zRequestUri = mprintf("%s/%s", zScriptName, z);
    }
    cgi_set_parameter("REQUEST_URI", zRequestUri);
  }
  if( zPathInfo==0 ){
    int i, j;
    for(i=0; zRequestUri[i]==zScriptName[i] && zRequestUri[i]; i++){}
    for(j=i; zRequestUri[j] && zRequestUri[j]!='?'; j++){}
    zPathInfo = fossil_strndup(zRequestUri+i, j-i);
    cgi_set_parameter_nocopy("PATH_INFO", zPathInfo, 0);
    if( j>i && zScriptName[i]!=0 ){
      /* If SCRIPT_NAME is not a prefix of REQUEST_URI, truncate it so
      ** that it is.  See https://fossil-scm.org/forum/forumpost/049e8650ed
      */
      char *zNew = fossil_strndup(zScriptName, i);
      cgi_replace_parameter("SCRIPT_NAME", zNew);
    }
  }
#ifdef FOSSIL_ENABLE_JSON
  if(noJson==0 && json_request_is_json_api(zPathInfo)){
    /* We need to change some following behaviour depending on whether
    ** we are operating in JSON mode or not. We cannot, however, be
    ** certain whether we should/need to be in JSON mode until the
    ** PATH_INFO is set up.
    */
    g.json.isJsonMode = 1;
    json_bootstrap_early();
  }else{
    assert(!g.json.isJsonMode &&
           "Internal misconfiguration of g.json.isJsonMode");
  }
#endif
  z = (char*)P("HTTP_COOKIE");
  if( z ){
    z = fossil_strdup(z);
    add_param_list(z, ';', 0);
    z = (char*)cookie_value("skin",0);
    if(z){
      skin_use_alternative(z, 2, SKIN_FROM_COOKIE);
    }
  }

  cgi_setup_query_string();

  z = (char*)P("REMOTE_ADDR");
  if( z ){
    g.zIpAddr = fossil_strdup(z);
  }

  zContentLength = P("CONTENT_LENGTH");
  if( zContentLength==0 ){
    len = 0;
    if( sqlite3_stricmp(PD("REQUEST_METHOD",""),"POST")==0 ){
      malformed_request("missing CONTENT_LENGTH on a POST method");
    }
  }else{
    len = atoi(zContentLength);
  }
  zType = P("CONTENT_TYPE");
  zSemi = zType ? strchr(zType, ';') : 0;
  if( zSemi ){
    g.zContentType = fossil_strndup(zType, (int)(zSemi-zType));
    zType = g.zContentType;
  }else{
    g.zContentType = zType;
  }
  blob_zero(&g.cgiIn);
  if( len>0 && zType ){
    if( blob_read_from_cgi(&g.cgiIn, len)<len ){
      char *zMsg = mprintf("CGI content-length mismatch:  Wanted %d bytes"
                           " but got only %d\n", len, blob_size(&g.cgiIn));
      malformed_request(zMsg);
    }
    if( fossil_strcmp(zType, "application/x-fossil")==0 ){
      blob_uncompress(&g.cgiIn, &g.cgiIn);
    }
#ifdef FOSSIL_ENABLE_JSON
    if( noJson==0 && g.json.isJsonMode!=0
        && json_can_consume_content_type(zType)!=0 ){
      cgi_parse_POST_JSON(&g.cgiIn);
      cgi_set_content_type(json_guess_content_type());
    }
#endif /* FOSSIL_ENABLE_JSON */
  }
}

/*
** Decode POST parameter information in the cgiIn content, if any.
*/
void cgi_decode_post_parameters(void){
  int len = blob_size(&g.cgiIn);
  if( len==0 ) return;
  if( fossil_strcmp(g.zContentType,"application/x-www-form-urlencoded")==0
   || fossil_strncmp(g.zContentType,"multipart/form-data",19)==0
  ){
    char *z = blob_str(&g.cgiIn);
    cgi_trace(z);
    if( g.zContentType[0]=='a' ){
      add_param_list(z, '&', 1);
    }else{
      process_multipart_form_data(z, len);
    }
    blob_init(&g.cgiIn, 0, 0);
  }
}

/*
** This is the comparison function used to sort the aParamQP[] array of
** query parameters and cookies.
*/
static int qparam_compare(const void *a, const void *b){
  struct QParam *pA = (struct QParam*)a;
  struct QParam *pB = (struct QParam*)b;
  int c;
  c = fossil_strcmp(pA->zName, pB->zName);
  if( c==0 ){
    c = pA->seq - pB->seq;
  }
  return c;
}

/*
** Return the value of a query parameter or cookie whose name is zName.
** If there is no query parameter or cookie named zName and the first
** character of zName is uppercase, then check to see if there is an
** environment variable by that name and return it if there is.  As
** a last resort when nothing else matches, return zDefault.
*/
const char *cgi_parameter(const char *zName, const char *zDefault){
  int lo, hi, mid, c;

  /* The sortQP flag is set whenever a new query parameter is inserted.
  ** It indicates that we need to resort the query parameters.
  */
  if( sortQP ){
    int i, j;
    qsort(aParamQP, nUsedQP, sizeof(aParamQP[0]), qparam_compare);
    sortQP = 0;
    /* After sorting, remove duplicate parameters.  The secondary sort
    ** key is aParamQP[].seq and we keep the first entry.  That means
    ** with duplicate calls to cgi_set_parameter() the second and
    ** subsequent calls are effectively no-ops. */
    for(i=j=1; i<nUsedQP; i++){
      if( fossil_strcmp(aParamQP[i].zName,aParamQP[i-1].zName)==0 ){
        continue;
      }
      if( j<i ){
        memcpy(&aParamQP[j], &aParamQP[i], sizeof(aParamQP[j]));
      }
      j++;
    }
    nUsedQP = j;
  }

  /* Invoking with a NULL zName is just a way to cause the parameters
  ** to be sorted.  So go ahead and bail out in that case */
  if( zName==0 || zName[0]==0 ) return 0;

  /* Do a binary search for a matching query parameter */
  lo = 0;
  hi = nUsedQP-1;
  while( lo<=hi ){
    mid = (lo+hi)/2;
    c = fossil_strcmp(aParamQP[mid].zName, zName);
    if( c==0 ){
      CGIDEBUG(("mem-match [%s] = [%s]\n", zName, aParamQP[mid].zValue));
      aParamQP[mid].isFetched = 1;
      return aParamQP[mid].zValue;
    }else if( c>0 ){
      hi = mid-1;
    }else{
      lo = mid+1;
    }
  }

  /* If no match is found and the name begins with an upper-case
  ** letter, then check to see if there is an environment variable
  ** with the given name.
  */
  if( fossil_isupper(zName[0]) ){
    const char *zValue = fossil_getenv(zName);
    if( zValue ){
      cgi_set_parameter_nocopy(zName, zValue, 0);
      CGIDEBUG(("env-match [%s] = [%s]\n", zName, zValue));
      return zValue;
    }
  }
  CGIDEBUG(("no-match [%s]\n", zName));
  return zDefault;
}

/*
** Return TRUE if the specific parameter exists and is a query parameter.
** Return FALSE if the parameter is a cookie or environment variable.
*/
int cgi_is_qp(const char *zName){
  int i;
  if( zName==0 || fossil_isupper(zName[0]) ) return 0;
  for(i=0; i<nUsedQP; i++){
    if( fossil_strcmp(aParamQP[i].zName,zName)==0 ){
      return aParamQP[i].isQP;
    }
  }
  return 0;
}

/*
** Renders the "begone, spider" page and exits.
*/
static void cgi_begone_spider(const char *zName){
  Blob content = empty_blob;
  cgi_set_content(&content);
  style_set_current_feature("test");
  style_submenu_enable(0);
  style_header("Malicious Query Detected");
  @ <h2>Begone, Knave!</h2>
  @ <p>This page was generated because Fossil detected an (unsuccessful)
  @ SQL injection attack or other nefarious content in your HTTP request.
  @
  @ <p>If you believe you are innocent and have reached this page in error,
  @ contact the Fossil developers on the Fossil-SCM Forum.  Type
  @ "fossil-scm forum" into any search engine to locate the Fossil-SCM Forum.
  style_finish_page();
  cgi_set_status(418,"I'm a teapot");
  cgi_reply();
  fossil_errorlog("Xpossible hack attempt - 418 response on \"%s\"", zName);
  exit(0);
}

/*
** If looks_like_attack() returns true for the given string, call
** cgi_begone_spider() and does not return, else this function has no
** side effects. The range of checks performed by this function may
** be extended in the future.
**
** Checks are omitted for any logged-in user.
**
** This is the primary defense against attack.  Fossil should easily be
** proof against SQL injection and XSS attacks even without without this
** routine.  Rather, this is an attempt to avoid denial-of-service caused
** by persistent spiders that hammer the server with dozens or hundreds of
** probes per seconds as they look for vulnerabilities. In other
** words, this is an effort to reduce the CPU load imposed by malicious
** spiders.  Though those routine might help make attacks harder, it is
** not itself an impenetrably barrier against attack and should not be
** relied upon as the only defense.
*/
void cgi_value_spider_check(const char *zTxt, const char *zName){
  if( g.zLogin==0 && looks_like_attack(zTxt) ){
    cgi_begone_spider(zName);
  }
}

/*
** A variant of cgi_parameter() with the same semantics except that if
** cgi_parameter(zName,zDefault) returns a value other than zDefault
** then it passes that value to cgi_value_spider_check().
*/
const char *cgi_parameter_no_attack(const char *zName, const char *zDefault){
  const char *zTxt = cgi_parameter(zName, zDefault);

  if( zTxt!=zDefault ){
    cgi_value_spider_check(zTxt, zName);
  }
  return zTxt;
}

/*
** Return the value of the first defined query parameter or cookie whose
** name appears in the list of arguments.  Or if no parameter is found,
** return NULL.
*/
const char *cgi_coalesce(const char *zName, ...){
  va_list ap;
  const char *z;
  const char *zX;
  if( zName==0 ) return 0;
  z = cgi_parameter(zName, 0);
  va_start(ap, zName);
  while( z==0 && (zX = va_arg(ap,const char*))!=0 ){
    z = cgi_parameter(zX, 0);
  }
  va_end(ap);
  return z;
}

/*
** Return the value of a CGI parameter with leading and trailing
** spaces removed and with internal \r\n changed to just \n
*/
char *cgi_parameter_trimmed(const char *zName, const char *zDefault){
  const char *zIn;
  char *zOut, c;
  int i, j;
  zIn = cgi_parameter(zName, 0);
  if( zIn==0 ) zIn = zDefault;
  if( zIn==0 ) return 0;
  while( fossil_isspace(zIn[0]) ) zIn++;
  zOut = fossil_strdup(zIn);
  for(i=j=0; (c = zOut[i])!=0; i++){
    if( c=='\r' && zOut[i+1]=='\n' ) continue;
    zOut[j++] = c;
  }
  zOut[j] = 0;
  while( j>0 && fossil_isspace(zOut[j-1]) ) zOut[--j] = 0;
  return zOut;
}

/*
** Return true if the CGI parameter zName exists and is not equal to 0,
** or "no" or "off".
*/
int cgi_parameter_boolean(const char *zName){
  const char *zIn = cgi_parameter(zName, 0);
  if( zIn==0 ) return 0;
  return zIn[0]==0 || is_truth(zIn);
}

/*
** Return either an empty string "" or the string "checked" depending
** on whether or not parameter zName has value iValue.  If parameter
** zName does not exist, that is assumed to be the same as value 0.
**
** This routine implements the PCK(x) and PIF(x,y) macros.  The PIF(x,y)
** macro generateds " checked" if the value of parameter x equals integer y.
** PCK(x) is the same as PIF(x,1).  These macros are used to generate
** the "checked" attribute on checkbox and radio controls of forms.
*/
const char *cgi_parameter_checked(const char *zName, int iValue){
  const char *zIn = cgi_parameter(zName,0);
  int x;
  if( zIn==0 ){
    x = 0;
  }else if( !fossil_isdigit(zIn[0]) ){
    x = is_truth(zIn);
  }else{
    x = atoi(zIn);
  }
  return x==iValue ? "checked" : "";
}

/*
** Return the name of the i-th CGI parameter.  Return NULL if there
** are fewer than i registered CGI parameters.
*/
const char *cgi_parameter_name(int i){
  if( i>=0 && i<nUsedQP ){
    return aParamQP[i].zName;
  }else{
    return 0;
  }
}

/*
** Print CGI debugging messages.
*/
void cgi_debug(const char *zFormat, ...){
  va_list ap;
  if( g.fDebug ){
    va_start(ap, zFormat);
    vfprintf(g.fDebug, zFormat, ap);
    va_end(ap);
    fflush(g.fDebug);
  }
}

/*
** Return true if any of the query parameters in the argument
** list are defined.
*/
int cgi_any(const char *z, ...){
  va_list ap;
  char *z2;
  if( cgi_parameter(z,0)!=0 ) return 1;
  va_start(ap, z);
  while( (z2 = va_arg(ap, char*))!=0 ){
    if( cgi_parameter(z2,0)!=0 ) return 1;
  }
  va_end(ap);
  return 0;
}

/*
** Return true if all of the query parameters in the argument list
** are defined.
*/
int cgi_all(const char *z, ...){
  va_list ap;
  char *z2;
  if( cgi_parameter(z,0)==0 ) return 0;
  va_start(ap, z);
  while( (z2 = va_arg(ap, char*))==0 ){
    if( cgi_parameter(z2,0)==0 ) return 0;
  }
  va_end(ap);
  return 1;
}

/*
** Load all relevant environment variables into the parameter buffer.
** Invoke this routine prior to calling cgi_print_all() in order to see
** the full CGI environment.  This routine intended for debugging purposes
** only.
*/
void cgi_load_environment(void){
  /* The following is a list of environment variables that Fossil considers
  ** to be "relevant". */
  static const char *const azCgiVars[] = {
    "COMSPEC", "DOCUMENT_ROOT", "GATEWAY_INTERFACE", "SCGI",
    "HTTP_ACCEPT", "HTTP_ACCEPT_CHARSET", "HTTP_ACCEPT_ENCODING",
    "HTTP_ACCEPT_LANGUAGE", "HTTP_AUTHENTICATION",
    "HTTP_CONNECTION", "HTTP_HOST",
    "HTTP_IF_NONE_MATCH", "HTTP_IF_MODIFIED_SINCE",
    "HTTP_USER_AGENT", "HTTP_REFERER", "PATH_INFO", "PATH_TRANSLATED",
    "QUERY_STRING", "REMOTE_ADDR", "REMOTE_PORT",
    "REMOTE_USER", "REQUEST_METHOD", "REQUEST_SCHEME",
    "REQUEST_URI", "SCRIPT_FILENAME", "SCRIPT_NAME", "SERVER_NAME",
    "SERVER_PROTOCOL", "HOME", "FOSSIL_HOME", "USERNAME", "USER",
    "FOSSIL_USER", "SQLITE_TMPDIR", "TMPDIR",
    "TEMP", "TMP", "FOSSIL_VFS",
    "FOSSIL_FORCE_TICKET_MODERATION", "FOSSIL_FORCE_WIKI_MODERATION",
    "FOSSIL_TCL_PATH", "TH1_DELETE_INTERP", "TH1_ENABLE_DOCS",
    "TH1_ENABLE_HOOKS", "TH1_ENABLE_TCL", "REMOTE_HOST",
    "CONTENT_TYPE", "CONTENT_LENGTH",
  };
  int i;
  for(i=0; i<count(azCgiVars); i++) (void)P(azCgiVars[i]);
}

/*
** Print all query parameters on standard output.
** This is used for testing and debugging.
**
** Omit the values of the cookies unless showAll is true.
**
** The eDest parameter determines where the output is shown:
**
**     eDest==0:    Rendering as HTML into the CGI reply
**     eDest==1:    Written to fossil_trace
**     eDest==2:    Written to cgi_debug
**     eDest==3:    Written to out  (Used only by fossil_errorlog())
*/
void cgi_print_all(int showAll, unsigned int eDest, FILE *out){
  int i;
  cgi_parameter("","");  /* Force the parameters into sorted order */
  for(i=0; i<nUsedQP; i++){
    const char *zName = aParamQP[i].zName;
    const char *zValue = aParamQP[i].zValue;
    if( fossil_stricmp("HTTP_COOKIE",zName)==0
     || fossil_strnicmp("fossil-",zName,7)==0
    ){
      if( !showAll ) continue;
      if( eDest==3 ) zValue = "...";
    }
    switch( eDest ){
      case 0: {
        cgi_printf("%h = %h  <br>\n", zName, zValue);
        break;
      }
      case 1: {
        fossil_trace("%s = %s\n", zName, zValue);
        break;
      }
      case 2: {
        cgi_debug("%s = %s\n", zName, zValue);
        break;
      }
      case 3: {
        if( zValue!=0 && strlen(zValue)>100 ){
          fprintf(out,"%s = %.100s...\n", zName, zValue);
        }else{
          fprintf(out,"%s = %s\n", zName, zValue);
        }
        break;
      }
    }
  }
}

/*
** Put information about the N-th parameter into arguments.
** Return non-zero on success, and return 0 if there is no N-th parameter.
*/
int cgi_param_info(
  int N,
  const char **pzName,
  const char **pzValue,
  int *pbIsQP
){
  if( N>=0 && N<nUsedQP ){
    *pzName = aParamQP[N].zName;
    *pzValue = aParamQP[N].zValue;
    *pbIsQP = aParamQP[N].isQP;
    return 1;
  }else{
    *pzName = 0;
    *pzValue = 0;
    *pbIsQP = 0;
    return 0;
  }
}

/*
** Export all untagged query parameters (but not cookies or environment
** variables) as hidden values of a form.
*/
void cgi_query_parameters_to_hidden(void){
  int i;
  const char *zN, *zV;
  for(i=0; i<nUsedQP; i++){
    if( aParamQP[i].isQP==0 || aParamQP[i].cTag ) continue;
    zN = aParamQP[i].zName;
    zV = aParamQP[i].zValue;
    @ <input type="hidden" name="%h(zN)" value="%h(zV)">
  }
}

/*
** Export all untagged query parameters (but not cookies or environment
** variables) to the HQuery object.
*/
void cgi_query_parameters_to_url(HQuery *p){
  int i;
  for(i=0; i<nUsedQP; i++){
    if( aParamQP[i].isQP==0 || aParamQP[i].cTag ) continue;
    url_add_parameter(p, aParamQP[i].zName, aParamQP[i].zValue);
  }
}

/*
** Reconstruct the URL into memory obtained from fossil_malloc() and
** return a pointer to that URL.
*/
char *cgi_reconstruct_original_url(void){
  int i;
  char cSep = '?';
  Blob url;
  blob_init(&url, 0, 0);
  blob_appendf(&url, "%s/%s", g.zBaseURL, g.zPath);
  for(i=0; i<nUsedQP; i++){
    if( aParamQP[i].isQP ){
      struct QParam *p = &aParamQP[i];
      if( p->zValue && p->zValue[0] ){
        blob_appendf(&url, "%c%t=%t", cSep, p->zName, p->zValue);
      }else{
        blob_appendf(&url, "%c%t", cSep, p->zName);
      }
      cSep = '&';
    }
  }
  return blob_str(&url);
}

/*
** Tag query parameter zName so that it is not exported by
** cgi_query_parameters_to_hidden().  Or if zName==0, then
** untag all query parameters.
*/
void cgi_tag_query_parameter(const char *zName){
  int i;
  if( zName==0 ){
    for(i=0; i<nUsedQP; i++) aParamQP[i].cTag = 0;
  }else{
    for(i=0; i<nUsedQP; i++){
      if( strcmp(zName,aParamQP[i].zName)==0 ) aParamQP[i].cTag = 1;
    }
  }
}

/*
** This routine works like "printf" except that it has the
** extra formatting capabilities such as %h and %t.
*/
void cgi_printf(const char *zFormat, ...){
  va_list ap;
  va_start(ap,zFormat);
  vxprintf(pContent,zFormat,ap);
  va_end(ap);
}

/*
** This routine works like "vprintf" except that it has the
** extra formatting capabilities such as %h and %t.
*/
void cgi_vprintf(const char *zFormat, va_list ap){
  vxprintf(pContent,zFormat,ap);
}


/*
** Send a reply indicating that the HTTP request was malformed
*/
static NORETURN void malformed_request(const char *zMsg, ...){
  va_list ap;
  char *z;
  va_start(ap, zMsg);
  z = vmprintf(zMsg, ap);
  va_end(ap);
  cgi_set_status(400, "Bad Request");
  zReplyMimeType = "text/plain";
  if( g.zReqType==0 ) g.zReqType = "WWW";
  if( g.zReqType[0]=='C' && PD("SERVER_SOFTWARE",0)!=0 ){
    const char *zServer = PD("SERVER_SOFTWARE","");
    cgi_printf("Bad CGI Request from \"%s\": %s\n",zServer,z);
  }else{
    cgi_printf("Bad %s Request: %s\n", g.zReqType, z);
  }
  fossil_free(z);
  cgi_reply();
  fossil_exit(0);
}

/*
** Panic and die while processing a webpage.
*/
NORETURN void cgi_panic(const char *zFormat, ...){
  va_list ap;
  cgi_reset_content();
#ifdef FOSSIL_ENABLE_JSON
  if( g.json.isJsonMode ){
    char * zMsg;
    va_start(ap, zFormat);
    zMsg = vmprintf(zFormat,ap);
    va_end(ap);
    json_err( FSL_JSON_E_PANIC, zMsg, 1 );
    free(zMsg);
    fossil_exit( g.isHTTP ? 0 : 1 );
  }else
#endif /* FOSSIL_ENABLE_JSON */
  {
    cgi_set_status(500, "Internal Server Error");
    cgi_printf(
               "<html><body><h1>Internal Server Error</h1>\n"
               "<plaintext>"
               );
    va_start(ap, zFormat);
    vxprintf(pContent,zFormat,ap);
    va_end(ap);
    cgi_reply();
    fossil_exit(1);
  }
}

/* z[] is the value of an X-FORWARDED-FOR: line in an HTTP header.
** Return a pointer to a string containing the real IP address, or a
** NULL pointer to stick with the IP address previously computed and
** loaded into g.zIpAddr.
*/
static const char *cgi_accept_forwarded_for(const char *z){
  int i;
  if( !cgi_is_loopback(g.zIpAddr) ){
    /* Only accept X-FORWARDED-FOR if input coming from the local machine */
    return 0;
  }
  i = strlen(z)-1;
  while( i>=0 && z[i]!=',' && !fossil_isspace(z[i]) ) i--;
  return &z[++i];
}

/*
** Remove the first space-delimited token from a string and return
** a pointer to it.  Add a NULL to the string to terminate the token.
** Make *zLeftOver point to the start of the next token.
*/
static char *extract_token(char *zInput, char **zLeftOver){
  char *zResult = 0;
  if( zInput==0 ){
    if( zLeftOver ) *zLeftOver = 0;
    return 0;
  }
  while( fossil_isspace(*zInput) ){ zInput++; }
  zResult = zInput;
  while( *zInput && !fossil_isspace(*zInput) ){ zInput++; }
  if( *zInput ){
    *zInput = 0;
    zInput++;
    while( fossil_isspace(*zInput) ){ zInput++; }
  }
  if( zLeftOver ){ *zLeftOver = zInput; }
  return zResult;
}

/*
** All possible forms of an IP address.  Needed to work around GCC strict
** aliasing rules.
*/
typedef union {
  struct sockaddr sa;              /* Abstract superclass */
  struct sockaddr_in sa4;          /* IPv4 */
  struct sockaddr_in6 sa6;         /* IPv6 */
  struct sockaddr_storage sas;     /* Should be the maximum of the above 3 */
} address;

/*
** Determine the IP address on the other side of a connection.
** Return a pointer to a string.  Or return 0 if unable.
**
** The string is held in a static buffer that is overwritten on
** each call.
*/
char *cgi_remote_ip(int fd){
  address remoteAddr;
  socklen_t size = sizeof(remoteAddr);
  static char zHost[NI_MAXHOST];
  if( getpeername(0, &remoteAddr.sa, &size) ){
    return 0;
  }
  if( getnameinfo(&remoteAddr.sa, size, zHost, sizeof(zHost), 0, 0,
                  NI_NUMERICHOST) ){
    return 0;
  }
  return zHost;
}

/*
** This routine handles a single HTTP request which is coming in on
** g.httpIn and which replies on g.httpOut
**
** The HTTP request is read from g.httpIn and is used to initialize
** entries in the cgi_parameter() hash, as if those entries were
** environment variables.  A call to cgi_init() completes
** the setup.  Once all the setup is finished, this procedure returns
** and subsequent code handles the actual generation of the webpage.
*/
void cgi_handle_http_request(const char *zIpAddr){
  char *z, *zToken;
  int i;
  const char *zScheme = "http";
  char zLine[2000];     /* A single line of input. */
  g.fullHttpReply = 1;
  g.zReqType = "HTTP";

  if( cgi_fgets(zLine, sizeof(zLine))==0 ){
    malformed_request("missing header");
  }
  blob_append(&g.httpHeader, zLine, -1);
  cgi_trace(zLine);
  zToken = extract_token(zLine, &z);
  if( zToken==0 ){
    malformed_request("malformed HTTP header");
  }
  if( fossil_strcmp(zToken,"GET")!=0
   && fossil_strcmp(zToken,"POST")!=0
   && fossil_strcmp(zToken,"HEAD")!=0
  ){
    malformed_request("unsupported HTTP method: \"%s\" - Fossil only supports "
                      "GET, POST, and HEAD", zToken);
  }
  cgi_setenv("GATEWAY_INTERFACE","CGI/1.0");
  cgi_setenv("REQUEST_METHOD",zToken);
  zToken = extract_token(z, &z);
  if( zToken==0 ){
    malformed_request("malformed URI in the HTTP header");
  }
  cgi_setenv("REQUEST_URI", zToken);
  cgi_setenv("SCRIPT_NAME", "");
  for(i=0; zToken[i] && zToken[i]!='?'; i++){}
  if( zToken[i] ) zToken[i++] = 0;
  cgi_setenv("PATH_INFO", zToken);
  cgi_setenv("QUERY_STRING", &zToken[i]);
  if( zIpAddr==0 ){
    zIpAddr = cgi_remote_ip(fossil_fileno(g.httpIn));
  }
  if( zIpAddr ){
    cgi_setenv("REMOTE_ADDR", zIpAddr);
    g.zIpAddr = fossil_strdup(zIpAddr);
  }

  /* Get all the optional fields that follow the first line.
  */
  while( cgi_fgets(zLine,sizeof(zLine)) ){
    char *zFieldName;
    char *zVal;

    cgi_trace(zLine);
    blob_append(&g.httpHeader, zLine, -1);
    zFieldName = extract_token(zLine,&zVal);
    if( zFieldName==0 || *zFieldName==0 ) break;
    while( fossil_isspace(*zVal) ){ zVal++; }
    i = strlen(zVal);
    while( i>0 && fossil_isspace(zVal[i-1]) ){ i--; }
    zVal[i] = 0;
    for(i=0; zFieldName[i]; i++){
      zFieldName[i] = fossil_tolower(zFieldName[i]);
    }
    if( fossil_strcmp(zFieldName,"accept-encoding:")==0 ){
      cgi_setenv("HTTP_ACCEPT_ENCODING", zVal);
    }else if( fossil_strcmp(zFieldName,"content-length:")==0 ){
      cgi_setenv("CONTENT_LENGTH", zVal);
    }else if( fossil_strcmp(zFieldName,"content-type:")==0 ){
      cgi_setenv("CONTENT_TYPE", zVal);
    }else if( fossil_strcmp(zFieldName,"cookie:")==0 ){
      cgi_setenv("HTTP_COOKIE", zVal);
    }else if( fossil_strcmp(zFieldName,"https:")==0 ){
      cgi_setenv("HTTPS", zVal);
      zScheme = "https";
    }else if( fossil_strcmp(zFieldName,"host:")==0 ){
      char *z;
      cgi_setenv("HTTP_HOST", zVal);
      z = strchr(zVal, ':');
      if( z ) z[0] = 0;
      cgi_setenv("SERVER_NAME", zVal);
    }else if( fossil_strcmp(zFieldName,"if-none-match:")==0 ){
      cgi_setenv("HTTP_IF_NONE_MATCH", zVal);
    }else if( fossil_strcmp(zFieldName,"if-modified-since:")==0 ){
      cgi_setenv("HTTP_IF_MODIFIED_SINCE", zVal);
    }else if( fossil_strcmp(zFieldName,"referer:")==0 ){
      cgi_setenv("HTTP_REFERER", zVal);
    }else if( fossil_strcmp(zFieldName,"user-agent:")==0 ){
      cgi_setenv("HTTP_USER_AGENT", zVal);
    }else if( fossil_strcmp(zFieldName,"authorization:")==0 ){
      cgi_setenv("HTTP_AUTHORIZATION", zVal);
    }else if( fossil_strcmp(zFieldName,"accept-language:")==0 ){
      cgi_setenv("HTTP_ACCEPT_LANGUAGE", zVal);
    }else if( fossil_strcmp(zFieldName,"x-forwarded-for:")==0 ){
      const char *zIpAddr = cgi_accept_forwarded_for(zVal);
      if( zIpAddr!=0 ){
        g.zIpAddr = fossil_strdup(zIpAddr);
        cgi_replace_parameter("REMOTE_ADDR", g.zIpAddr);
      }
    }else if( fossil_strcmp(zFieldName,"range:")==0 ){
      int x1 = 0;
      int x2 = 0;
      if( sscanf(zVal,"bytes=%d-%d",&x1,&x2)==2 && x1>=0 && x1<=x2 ){
        rangeStart = x1;
        rangeEnd = x2+1;
      }
    }
  }
  cgi_setenv("REQUEST_SCHEME",zScheme);
  cgi_init();
  cgi_trace(0);
}

/*
** This routine handles a single HTTP request from an SSH client which is
** coming in on g.httpIn and which replies on g.httpOut
**
** Once all the setup is finished, this procedure returns
** and subsequent code handles the actual generation of the webpage.
**
** It is called in a loop so some variables will need to be replaced
*/
void cgi_handle_ssh_http_request(const char *zIpAddr){
  static int nCycles = 0;
  static char *zCmd = 0;
  char *z, *zToken;
  const char *zType = 0;
  int i, content_length = 0;
  char zLine[2000];     /* A single line of input. */

  assert( !g.httpUseSSL );
#ifdef FOSSIL_ENABLE_JSON
  if( nCycles==0 ){ json_bootstrap_early(); }
#endif
  if( zIpAddr ){
    if( nCycles==0 ){
      cgi_setenv("REMOTE_ADDR", zIpAddr);
      g.zIpAddr = fossil_strdup(zIpAddr);
    }
  }else{
    fossil_fatal("missing SSH IP address");
  }
  g.zReqType = "HTTP";
  if( fgets(zLine, sizeof(zLine),g.httpIn)==0 ){
    malformed_request("missing HTTP header");
  }
  cgi_trace(zLine);
  zToken = extract_token(zLine, &z);
  if( zToken==0 ){
    malformed_request("malformed HTTP header");
  }

  if( fossil_strcmp(zToken, "echo")==0 ){
    /* start looking for probes to complete transport_open */
    zCmd = cgi_handle_ssh_probes(zLine, sizeof(zLine), z, zToken);
    if( fgets(zLine, sizeof(zLine),g.httpIn)==0 ){
      malformed_request("missing HTTP header");
    }
    cgi_trace(zLine);
    zToken = extract_token(zLine, &z);
    if( zToken==0 ){
      malformed_request("malformed HTTP header");
    }
  }else if( zToken && strlen(zToken)==0 && zCmd ){
    /* transport_flip request and continued transport_open */
    cgi_handle_ssh_transport(zCmd);
    if( fgets(zLine, sizeof(zLine),g.httpIn)==0 ){
      malformed_request("missing HTTP header");
    }
    cgi_trace(zLine);
    zToken = extract_token(zLine, &z);
    if( zToken==0 ){
      malformed_request("malformed HTTP header");
    }
  }

  if( fossil_strcmp(zToken,"GET")!=0 && fossil_strcmp(zToken,"POST")!=0
      && fossil_strcmp(zToken,"HEAD")!=0 ){
    malformed_request("unsupported HTTP method");
  }

  if( nCycles==0 ){
    cgi_setenv("GATEWAY_INTERFACE","CGI/1.0");
    cgi_setenv("REQUEST_METHOD",zToken);
  }

  zToken = extract_token(z, &z);
  if( zToken==0 ){
    malformed_request("malformed URL in HTTP header");
  }
  if( nCycles==0 ){
    cgi_setenv("REQUEST_URI", zToken);
    cgi_setenv("SCRIPT_NAME", "");
  }

  for(i=0; zToken[i] && zToken[i]!='?'; i++){}
  if( zToken[i] ) zToken[i++] = 0;
  if( nCycles==0 ){
    cgi_setenv("PATH_INFO", zToken);
  }else{
    cgi_replace_parameter("PATH_INFO", fossil_strdup(zToken));
  }

  /* Get all the optional fields that follow the first line.
  */
  while( fgets(zLine,sizeof(zLine),g.httpIn) ){
    char *zFieldName;
    char *zVal;

    cgi_trace(zLine);
    zFieldName = extract_token(zLine,&zVal);
    if( zFieldName==0 || *zFieldName==0 ) break;
    while( fossil_isspace(*zVal) ){ zVal++; }
    i = strlen(zVal);
    while( i>0 && fossil_isspace(zVal[i-1]) ){ i--; }
    zVal[i] = 0;
    for(i=0; zFieldName[i]; i++){
      zFieldName[i] = fossil_tolower(zFieldName[i]);
    }
    if( fossil_strcmp(zFieldName,"content-length:")==0 ){
      content_length = atoi(zVal);
    }else if( fossil_strcmp(zFieldName,"content-type:")==0 ){
      g.zContentType = zType = fossil_strdup(zVal);
    }else if( fossil_strcmp(zFieldName,"host:")==0 ){
      if( nCycles==0 ){
        cgi_setenv("HTTP_HOST", zVal);
      }
    }else if( fossil_strcmp(zFieldName,"user-agent:")==0 ){
      if( nCycles==0 ){
        cgi_setenv("HTTP_USER_AGENT", zVal);
      }
    }else if( fossil_strcmp(zFieldName,"x-fossil-transport:")==0 ){
      if( fossil_strnicmp(zVal, "ssh", 3)==0 ){
        if( nCycles==0 ){
          g.fSshClient |= CGI_SSH_FOSSIL;
          g.fullHttpReply = 0;
        }
      }
    }
  }

  if( nCycles==0 ){
    if( ! ( g.fSshClient & CGI_SSH_FOSSIL ) ){
      /* did not find new fossil ssh transport */
      g.fSshClient &= ~CGI_SSH_CLIENT;
      g.fullHttpReply = 1;
      cgi_replace_parameter("REMOTE_ADDR", "127.0.0.1");
    }
  }

  cgi_reset_content();
  cgi_destination(CGI_BODY);

  if( content_length>0 && zType ){
    blob_zero(&g.cgiIn);
    if( fossil_strcmp(zType, "application/x-fossil")==0 ){
      blob_read_from_channel(&g.cgiIn, g.httpIn, content_length);
      blob_uncompress(&g.cgiIn, &g.cgiIn);
    }else if( fossil_strcmp(zType, "application/x-fossil-debug")==0 ){
      blob_read_from_channel(&g.cgiIn, g.httpIn, content_length);
    }else if( fossil_strcmp(zType, "application/x-fossil-uncompressed")==0 ){
      blob_read_from_channel(&g.cgiIn, g.httpIn, content_length);
    }
  }
  cgi_trace(0);
  nCycles++;
}

/*
** This routine handles the old fossil SSH probes
*/
char *cgi_handle_ssh_probes(char *zLine, int zSize, char *z, char *zToken){
  /* Start looking for probes */
  assert( !g.httpUseSSL );
  while( fossil_strcmp(zToken, "echo")==0 ){
    zToken = extract_token(z, &z);
    if( zToken==0 ){
      malformed_request("malformed probe");
    }
    if( fossil_strncmp(zToken, "test", 4)==0 ||
        fossil_strncmp(zToken, "probe-", 6)==0 ){
      fprintf(g.httpOut, "%s\n", zToken);
      fflush(g.httpOut);
    }else{
      malformed_request("malformed probe");
    }
    if( fgets(zLine, zSize, g.httpIn)==0 ){
      malformed_request("malformed probe");
    }
    cgi_trace(zLine);
    zToken = extract_token(zLine, &z);
    if( zToken==0 ){
      malformed_request("malformed probe");
    }
  }

  /* Got all probes now first transport_open is completed
  ** so return the command that was requested
  */
  g.fSshClient |= CGI_SSH_COMPAT;
  return fossil_strdup(zToken);
}

/*
** This routine handles the old fossil SSH transport_flip
** and transport_open communications if detected.
*/
void cgi_handle_ssh_transport(const char *zCmd){
  char *z, *zToken;
  char zLine[2000];     /* A single line of input. */

  assert( !g.httpUseSSL );
  /* look for second newline of transport_flip */
  if( fgets(zLine, sizeof(zLine),g.httpIn)==0 ){
    malformed_request("incorrect transport_flip");
  }
  cgi_trace(zLine);
  zToken = extract_token(zLine, &z);
  if( zToken && strlen(zToken)==0 ){
    /* look for path to fossil */
    if( fgets(zLine, sizeof(zLine),g.httpIn)==0 ){
      if( zCmd==0 ){
        malformed_request("missing fossil command");
      }else{
        /* no new command so exit */
        fossil_exit(0);
      }
    }
    cgi_trace(zLine);
    zToken = extract_token(zLine, &z);
    if( zToken==0 ){
      malformed_request("malformed fossil command");
    }
    /* see if we've seen the command */
    if( zCmd && zCmd[0] && fossil_strcmp(zToken, zCmd)==0 ){
      return;
    }else{
      malformed_request("transport_open failed");
    }
  }else{
    malformed_request("transport_flip failed");
  }
}

/*
** This routine handles a single SCGI request which is coming in on
** g.httpIn and which replies on g.httpOut
**
** The SCGI request is read from g.httpIn and is used to initialize
** entries in the cgi_parameter() hash, as if those entries were
** environment variables.  A call to cgi_init() completes
** the setup.  Once all the setup is finished, this procedure returns
** and subsequent code handles the actual generation of the webpage.
*/
void cgi_handle_scgi_request(void){
  char *zHdr;
  char *zToFree;
  int nHdr = 0;
  int nRead;
  int c, n, m;

  assert( !g.httpUseSSL );
  while( (c = fgetc(g.httpIn))!=EOF && fossil_isdigit((char)c) ){
    nHdr = nHdr*10 + (char)c - '0';
  }
  if( nHdr<16 ) malformed_request("SCGI header too short");
  zToFree = zHdr = fossil_malloc(nHdr);
  nRead = (int)fread(zHdr, 1, nHdr, g.httpIn);
  if( nRead<nHdr ) malformed_request("cannot read entire SCGI header");
  nHdr = nRead;
  while( nHdr ){
    for(n=0; n<nHdr && zHdr[n]; n++){}
    for(m=n+1; m<nHdr && zHdr[m]; m++){}
    if( m>=nHdr ) malformed_request("SCGI header formatting error");
    cgi_set_parameter(zHdr, zHdr+n+1);
    zHdr += m+1;
    nHdr -= m+1;
  }
  fossil_free(zToFree);
  fgetc(g.httpIn);  /* Read past the "," separating header from content */
  cgi_init();
}

#if INTERFACE
/*
** Bitmap values for the flags parameter to cgi_http_server().
*/
#define HTTP_SERVER_LOCALHOST      0x0001     /* Bind to 127.0.0.1 only */
#define HTTP_SERVER_SCGI           0x0002     /* SCGI instead of HTTP */
#define HTTP_SERVER_HAD_REPOSITORY 0x0004     /* Was the repository open? */
#define HTTP_SERVER_HAD_CHECKOUT   0x0008     /* Was a checkout open? */
#define HTTP_SERVER_REPOLIST       0x0010     /* Allow repo listing */
#define HTTP_SERVER_NOFORK         0x0020     /* Do not call fork() */
#define HTTP_SERVER_UNIXSOCKET     0x0040     /* Use a unix-domain socket */

#endif /* INTERFACE */

/*
** Maximum number of child processes that we can have running
** at one time.  Set this to 0 for "no limit".
*/
#ifndef FOSSIL_MAX_CONNECTIONS
# define FOSSIL_MAX_CONNECTIONS 1000
#endif

/*
** Implement an HTTP server daemon listening on port iPort.
**
** As new connections arrive, fork a child and let child return
** out of this procedure call.  The child will handle the request.
** The parent never returns from this procedure.
**
** Return 0 to each child as it runs.  If unable to establish a
** listening socket, return non-zero.
*/
int cgi_http_server(
  int mnPort, int mxPort,   /* Range of TCP ports to try */
  const char *zBrowser,     /* Run this browser, if not NULL */
  const char *zIpAddr,      /* Bind to this IP address, if not null */
  int flags                 /* HTTP_SERVER_* flags */
){
#if defined(_WIN32)
  /* Use win32_http_server() instead */
  fossil_exit(1);
#else
  int listen4 = -1;            /* Main socket; IPv4 or unix-domain */
  int listen6 = -1;            /* Aux socket for corresponding IPv6 */
  int mxListen = -1;           /* Maximum of listen4 and listen6 */
  int connection;              /* An incoming connection */
  int nRequest = 0;            /* Number of requests handled so far */
  fd_set readfds;              /* Set of file descriptors for select() */
  socklen_t lenaddr;           /* Length of the inaddr structure */
  int child;                   /* PID of the child process */
  int nchildren = 0;           /* Number of child processes */
  struct timeval delay;        /* How long to wait inside select() */
  struct sockaddr_in6 inaddr6; /* Address for IPv6 */
  struct sockaddr_in inaddr4;  /* Address for IPv4 */
  struct sockaddr_un uxaddr;   /* The address for unix-domain sockets */
  int opt = 1;                 /* setsockopt flag */
  int rc;                      /* Result code from system calls */
  int iPort = mnPort;          /* Port to try to use */
  const char *zRequestType;    /* Type of requests to listen for */


  if( flags & HTTP_SERVER_SCGI ){
    zRequestType = "SCGI";
  }else if( g.httpUseSSL ){
    zRequestType = "TLS-encrypted HTTPS";
  }else{
    zRequestType = "HTTP";
  }

  if( flags & HTTP_SERVER_UNIXSOCKET ){
    /* CASE 1:  A unix socket named g.zSockName.  After creation, set the
    **          permissions on the new socket to g.zSockMode and make the
    **          owner of the socket be g.zSockOwner.
    */
    assert( g.zSockName!=0 );
    memset(&uxaddr, 0, sizeof(uxaddr));
    if( strlen(g.zSockName)>sizeof(uxaddr.sun_path) ){
      fossil_fatal("name of unix socket too big: %s\nmax size: %d\n",
                   g.zSockName, (int)sizeof(uxaddr.sun_path));
    }
    if( file_isdir(g.zSockName, ExtFILE)!=0 ){
      if( !file_issocket(g.zSockName) ){
        fossil_fatal("cannot name socket \"%s\" because another object"
                     " with that name already exists", g.zSockName);
      }else{
        unlink(g.zSockName);
      }
    }
    uxaddr.sun_family = AF_UNIX;
    strncpy(uxaddr.sun_path, g.zSockName, sizeof(uxaddr.sun_path)-1);
    listen4 = socket(AF_UNIX, SOCK_STREAM, 0);
    if( listen4<0 ){
      fossil_fatal("unable to create a unix socket named %s",
                   g.zSockName);
    }
    mxListen = listen4;
    listen6 = -1;

    /* Set the access permission for the new socket.  Default to 0660.
    ** But use an alternative specified by --socket-mode if available.
    ** Do this before bind() to avoid a race condition. */
    if( g.zSockMode ){
      file_set_mode(g.zSockName, listen4, g.zSockMode, 0);
    }else{
      file_set_mode(g.zSockName, listen4, "0660", 1);
    }
    rc = bind(listen4, (struct sockaddr*)&uxaddr, sizeof(uxaddr));
    /* Set the owner of the socket if requested by --socket-owner.  This
    ** must wait until after bind(), after the filesystem object has been
    ** created.  See https://lkml.org/lkml/2004/11/1/84 and
    ** https://fossil-scm.org/forum/forumpost/7517680ef9684c57 */
    if( g.zSockOwner ){
      file_set_owner(g.zSockName, listen4, g.zSockOwner);
    }
    fossil_print("Listening for %s requests on unix socket %s\n",
                 zRequestType, g.zSockName);
    fflush(stdout);
  }else if( zIpAddr && strchr(zIpAddr,':')!=0 ){
    /* CASE 2: TCP on IPv6 IP address specified by zIpAddr and on port iPort.
    */
    assert( mnPort==mxPort );
    memset(&inaddr6, 0, sizeof(inaddr6));
    inaddr6.sin6_family = AF_INET6;
    inaddr6.sin6_port = htons(iPort);
    if( inet_pton(AF_INET6, zIpAddr, &inaddr6.sin6_addr)==0 ){
      fossil_fatal("not a valid IPv6 address: %s", zIpAddr);
    }
    listen6 = socket(AF_INET6, SOCK_STREAM, 0);
    if( listen6>0 ){
      opt = 1;
      setsockopt(listen6, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
      rc = bind(listen6, (struct sockaddr*)&inaddr6, sizeof(inaddr6));
      if( rc<0 ){
        close(listen6);
        listen6 = -1;
      }
    }
    if( listen6<0 ){
      fossil_fatal("cannot open a listening socket on [%s]:%d",
                   zIpAddr, mnPort);
    }
    mxListen = listen6;
    listen4 = -1;
    fossil_print("Listening for %s requests on [%s]:%d\n",
                 zRequestType, zIpAddr, iPort);
    fflush(stdout);
  }else if( zIpAddr && zIpAddr[0] ){
    /* CASE 3: TCP on IPv4 IP address specified by zIpAddr and on port iPort.
    */
    assert( mnPort==mxPort );
    memset(&inaddr4, 0, sizeof(inaddr4));
    inaddr4.sin_family = AF_INET;
    inaddr4.sin_port = htons(iPort);
    if( strcmp(zIpAddr, "localhost")==0 ) zIpAddr = "127.0.0.1";
    inaddr4.sin_addr.s_addr = inet_addr(zIpAddr);
    if( inaddr4.sin_addr.s_addr == INADDR_NONE ){
      fossil_fatal("not a valid IPv4 address: %s", zIpAddr);
    }
    listen4 = socket(AF_INET, SOCK_STREAM, 0);
    if( listen4>0 ){
      setsockopt(listen4, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
      rc = bind(listen4, (struct sockaddr*)&inaddr4, sizeof(inaddr4));
      if( rc<0 ){
        close(listen4);
        listen4 = -1;
      }
    }
    if( listen4<0 ){
      fossil_fatal("cannot open a listening socket on %s:%d",
                   zIpAddr, mnPort);
    }
    mxListen = listen4;
    listen6 = -1;
    fossil_print("Listening for %s requests on TCP port %s:%d\n",
                 zRequestType, zIpAddr, iPort);
    fflush(stdout);
  }else{
    /* CASE 4: Listen on all available IP addresses, or on only loopback
    **         addresses (if HTTP_SERVER_LOCALHOST).  The TCP port is the
    **         first available in the range of mnPort..mxPort.  Listen
    **         on both IPv4 and IPv6, if possible.  The TCP port scan is done
    **         on IPv4.
    */
    while( iPort<=mxPort ){
      const char *zProto;
      memset(&inaddr4, 0, sizeof(inaddr4));
      inaddr4.sin_family = AF_INET;
      inaddr4.sin_port = htons(iPort);
      if( flags & HTTP_SERVER_LOCALHOST ){
        inaddr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      }else{
        inaddr4.sin_addr.s_addr = htonl(INADDR_ANY);
      }
      listen4 = socket(AF_INET, SOCK_STREAM, 0);
      if( listen4>0 ){
        setsockopt(listen4, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        rc = bind(listen4, (struct sockaddr*)&inaddr4, sizeof(inaddr4));
        if( rc<0 ){
          close(listen4);
          listen4 = -1;
        }
      }
      if( listen4<0 ){
        iPort++;
        continue;
      }
      mxListen = listen4;

      /* If we get here, that means we found an open TCP port at iPort for
      ** IPv4.  Try to set up a corresponding IPv6 socket on the same port.
      */
      memset(&inaddr6, 0, sizeof(inaddr6));
      inaddr6.sin6_family = AF_INET6;
      inaddr6.sin6_port = htons(iPort);
      if( flags & HTTP_SERVER_LOCALHOST ){
        inaddr6.sin6_addr = in6addr_loopback;
      }else{
        inaddr6.sin6_addr = in6addr_any;
      }
      listen6 = socket(AF_INET6, SOCK_STREAM, 0);
      if( listen6>0 ){
        setsockopt(listen6, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(listen6, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        rc = bind(listen6, (struct sockaddr*)&inaddr6, sizeof(inaddr6));
        if( rc<0 ){
          close(listen6);
          listen6 = -1;
        }
      }
      if( listen6<0 ){
        zProto = "IPv4 only";
      }else{
        zProto = "IPv4 and IPv6";
        if( listen6>listen4 ) mxListen = listen6;
      }

      fossil_print("Listening for %s requests on TCP port %s%d, %s\n",
                   zRequestType, 
                   (flags & HTTP_SERVER_LOCALHOST)!=0 ? "localhost:" : "",
                   iPort, zProto);
      fflush(stdout);
      break;
    }
    if( iPort>mxPort ){
      fossil_fatal("no available TCP ports in the range %d..%d",
                   mnPort, mxPort);
    }
  }

  /* If we get to this point, that means there is at least one listening
  ** socket on either listen4 or listen6 and perhaps on both. */
  assert( listen4>0 || listen6>0 );
  if( listen4>0 ) listen(listen4,10);
  if( listen6>0 ) listen(listen6,10);
  if( zBrowser && (flags & HTTP_SERVER_UNIXSOCKET)==0 ){
    assert( strstr(zBrowser,"%d")!=0 );
    zBrowser = mprintf(zBrowser /*works-like:"%d"*/, iPort);
#if defined(__CYGWIN__)
    /* On Cygwin, we can do better than "echo" */
    if( fossil_strncmp(zBrowser, "echo ", 5)==0 ){
      wchar_t *wUrl = fossil_utf8_to_unicode(zBrowser+5);
      wUrl[wcslen(wUrl)-2] = 0; /* Strip terminating " &" */
      if( (size_t)ShellExecuteW(0, L"open", wUrl, 0, 0, 1)<33 ){
        fossil_warning("cannot start browser\n");
      }
    }else
#endif
    if( fossil_system(zBrowser)<0 ){
      fossil_warning("cannot start browser: %s\n", zBrowser);
    }
  }

  /* What for incomming requests.  For each request, fork() a child process
  ** to deal with that request.  The child process returns.  The parent
  ** keeps on listening and never returns.
  */
  while( 1 ){
#if FOSSIL_MAX_CONNECTIONS>0
    while( nchildren>=FOSSIL_MAX_CONNECTIONS ){
      if( wait(0)>=0 ) nchildren--;
    }
#endif
    delay.tv_sec = 0;
    delay.tv_usec = 100000;
    FD_ZERO(&readfds);
    assert( listen4>0 || listen6>0 );
    if( listen4>0 ) FD_SET( listen4, &readfds);
    if( listen6>0 ) FD_SET( listen6, &readfds);
    select( mxListen+1, &readfds, 0, 0, &delay);
    if( listen4>0 && FD_ISSET(listen4, &readfds) ){
      lenaddr = sizeof(inaddr4);
      connection = accept(listen4, (struct sockaddr*)&inaddr4, &lenaddr);
    }else if( listen6>0 && FD_ISSET(listen6, &readfds) ){
      lenaddr = sizeof(inaddr6);
      connection = accept(listen6, (struct sockaddr*)&inaddr6, &lenaddr);
    }else{
      connection = -1;
    }
    if( connection>=0 ){
      if( flags & HTTP_SERVER_NOFORK ){
        child = 0;
      }else{
        child = fork();
      }
      if( child!=0 ){
        if( child>0 ){
          nchildren++;
          nRequest++;
        }
        close(connection);
      }else{
        int nErr = 0, fd;
        g.zSockName = 0 /* avoid deleting the socket via atexit() */;
        close(0);
        fd = dup(connection);
        if( fd!=0 ) nErr++;
        close(1);
        fd = dup(connection);
        if( fd!=1 ) nErr++;
        if( 0 && !g.fAnyTrace ){
          close(2);
          fd = dup(connection);
          if( fd!=2 ) nErr++;
        }
        close(connection);
        if( listen4>0 ) close(listen4);
        if( listen6>0 ) close(listen6);
        g.nPendingRequest = nchildren+1;
        g.nRequest = nRequest+1;
        return nErr;
      }
    }
    /* Bury dead children */
    if( nchildren ){
      while(1){
        int iStatus = 0;
        pid_t x = waitpid(-1, &iStatus, WNOHANG);
        if( x<=0 ) break;
        if( WIFSIGNALED(iStatus) && g.fAnyTrace ){
          fprintf(stderr, "/***** Child %d exited on signal %d (%s) *****/\n",
                  x, WTERMSIG(iStatus), strsignal(WTERMSIG(iStatus)));
        }
        nchildren--;
      }
    }
  }
  /* NOT REACHED */
  fossil_exit(1);
#endif
  /* NOT REACHED */
  return 0;
}


/*
** Name of days and months.
*/
static const char *const azDays[] =
    {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", 0};
static const char *const azMonths[] =
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", 0};


/*
** Returns an RFC822-formatted time string suitable for HTTP headers.
** The timezone is always GMT.  The value returned is always a
** string obtained from mprintf() and must be freed using fossil_free()
** to avoid a memory leak.
**
** See http://www.faqs.org/rfcs/rfc822.html, section 5
** and http://www.faqs.org/rfcs/rfc2616.html, section 3.3.
*/
char *cgi_rfc822_datestamp(time_t now){
  struct tm *pTm;
  pTm = gmtime(&now);
  if( pTm==0 ){
    return mprintf("");
  }else{
    return mprintf("%s, %d %s %02d %02d:%02d:%02d +0000",
                   azDays[pTm->tm_wday], pTm->tm_mday, azMonths[pTm->tm_mon],
                   pTm->tm_year+1900, pTm->tm_hour, pTm->tm_min, pTm->tm_sec);
  }
}

/*
** Returns an ISO8601-formatted time string suitable for debugging
** purposes.
**
** The value returned is always a string obtained from mprintf() and must
** be freed using fossil_free() to avoid a memory leak.
*/
char *cgi_iso8601_datestamp(void){
  struct tm *pTm;
  time_t now = time(0);
  pTm = gmtime(&now);
  if( pTm==0 ){
    return mprintf("");
  }else{
    return mprintf("%04d-%02d-%02d %02d:%02d:%02d",
                   pTm->tm_year+1900, pTm->tm_mon+1, pTm->tm_mday,
                   pTm->tm_hour, pTm->tm_min, pTm->tm_sec);
  }
}

/*
** COMMAND: test-date
**
** Show the current date and time in both RFC822 and ISO8601.
*/
void test_date(void){
  fossil_print("%z = ", cgi_iso8601_datestamp());
  fossil_print("%z\n", cgi_rfc822_datestamp(time(0)));
}

/*
** Parse an RFC822-formatted timestamp as we'd expect from HTTP and return
** a Unix epoch time. <= zero is returned on failure.
**
** Note that this won't handle all the _allowed_ HTTP formats, just the
** most popular one (the one generated by cgi_rfc822_datestamp(), actually).
*/
time_t cgi_rfc822_parsedate(const char *zDate){
  int mday, mon, year, yday, hour, min, sec;
  char zIgnore[4];
  char zMonth[4];
  static const char *const azMonths[] =
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", 0};
  if( 7==sscanf(zDate, "%3[A-Za-z], %d %3[A-Za-z] %d %d:%d:%d", zIgnore,
                       &mday, zMonth, &year, &hour, &min, &sec)){
    if( year > 1900 ) year -= 1900;
    for(mon=0; azMonths[mon]; mon++){
      if( !fossil_strncmp( azMonths[mon], zMonth, 3 )){
        int nDay;
        int isLeapYr;
        static int priorDays[] =
         {  0, 31, 59, 90,120,151,181,212,243,273,304,334 };
        if( mon<0 ){
          int nYear = (11 - mon)/12;
          year -= nYear;
          mon += nYear*12;
        }else if( mon>11 ){
          year += mon/12;
          mon %= 12;
        }
        isLeapYr = year%4==0 && (year%100!=0 || (year+300)%400==0);
        yday = priorDays[mon] + mday - 1;
        if( isLeapYr && mon>1 ) yday++;
        nDay = (year-70)*365 + (year-69)/4 - year/100 + (year+300)/400 + yday;
        return ((time_t)(nDay*24 + hour)*60 + min)*60 + sec;
      }
    }
  }
  return 0;
}

/*
** Check the objectTime against the If-Modified-Since request header. If the
** object time isn't any newer than the header, we immediately send back
** a 304 reply and exit.
*/
void cgi_modified_since(time_t objectTime){
  const char *zIf = P("HTTP_IF_MODIFIED_SINCE");
  if( zIf==0 ) return;
  if( objectTime > cgi_rfc822_parsedate(zIf) ) return;
  cgi_set_status(304,"Not Modified");
  cgi_reset_content();
  cgi_reply();
  fossil_exit(0);
}

/*
** Check to see if the remote client is SSH and return
** its IP or return default
*/
const char *cgi_ssh_remote_addr(const char *zDefault){
  char *zIndex;
  const char *zSshConn = fossil_getenv("SSH_CONNECTION");

  if( zSshConn && zSshConn[0] ){
    char *zSshClient = fossil_strdup(zSshConn);
    if( (zIndex = strchr(zSshClient,' '))!=0 ){
      zSshClient[zIndex-zSshClient] = '\0';
      return zSshClient;
    }
  }
  return zDefault;
}

/*
** Return true if information is coming from the loopback network.
*/
int cgi_is_loopback(const char *zIpAddr){
  return fossil_strcmp(zIpAddr, "127.0.0.1")==0 ||
         fossil_strcmp(zIpAddr, "::ffff:127.0.0.1")==0 ||
         fossil_strcmp(zIpAddr, "::1")==0;
}

/*
** Return true if the HTTP request is likely to be from a small-screen
** mobile device.
**
** The returned value is a guess.  Use it only for setting up defaults.
*/
int cgi_from_mobile(void){
  const char *zAgent = P("HTTP_USER_AGENT");
  if( zAgent==0 ) return 0;
  if( sqlite3_strglob("*iPad*", zAgent)==0 ) return 0;
  return sqlite3_strlike("%mobile%", zAgent, 0)==0;
}

/*
** Look for query or POST parameters that:
**
**    (1)  Have not been used
**    (2)  Appear to be malicious attempts to break into or otherwise
**         harm the system, for example via SQL injection
**
** If any such parameters are seen, a 418 ("I'm a teapot") return is
** generated and processing aborts - this routine does not return.
**
** When Fossil is launched via CGI from althttpd, the 418 return signals
** the webserver to put the requestor IP address into "timeout", blocking
** subsequent requests for 5 minutes.
**
** Fossil is not subject to any SQL injections, as far as anybody knows.
** This routine is not necessary for the security of the system (though
** an extra layer of security never hurts).  The main purpose here is
** to shutdown malicious attack spiders and prevent them from burning
** lots of CPU cycles and bogging down the website.  In other words, the
** objective of this routine is to help prevent denial-of-service.
**
** Usage Hint: Put a call to this routine as late in the webpage
** implementation as possible, ideally just before it begins doing
** potentially CPU-intensive computations and after all query parameters
** have been consulted.
*/
void cgi_check_for_malice(void){
  struct QParam * pParam;
  int i;
  for(i=0; i<nUsedQP; ++i){
    pParam = &aParamQP[i];
    if( 0==pParam->isFetched
     && pParam->zValue!=0
     && pParam->zName!=0
     && fossil_islower(pParam->zName[0])
    ){
      cgi_value_spider_check(pParam->zValue, pParam->zName);
    }
  }
}
