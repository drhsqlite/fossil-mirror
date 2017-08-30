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
** This file contains C functions and procedures that provide useful
** services to CGI programs.  There are procedures for parsing and
** dispensing QUERY_STRING parameters and cookies, the "mprintf()"
** formatting function and its cousins, and routines to encode and
** decode strings in HTML or HTTP.
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
# include <netinet/in.h>
# include <arpa/inet.h>
# include <sys/times.h>
# include <sys/time.h>
# include <sys/wait.h>
# include <sys/select.h>
#endif
#ifdef __EMX__
  typedef int socklen_t;
#endif
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "cgi.h"
#include "cygsup.h"

#if INTERFACE
/*
** Shortcuts for cgi_parameter.  P("x") returns the value of query parameter
** or cookie "x", or NULL if there is no such parameter or cookie.  PD("x","y")
** does the same except "y" is returned in place of NULL if there is not match.
*/
#define P(x)        cgi_parameter((x),0)
#define PD(x,y)     cgi_parameter((x),(y))
#define PT(x)       cgi_parameter_trimmed((x),0)
#define PDT(x,y)    cgi_parameter_trimmed((x),(y))
#define PB(x)       cgi_parameter_boolean(x)


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
** The HTTP reply is generated in two pieces: the header and the body.
** These pieces are generated separately because they are not necessarily
** produced in order.  Parts of the header might be built after all or
** part of the body.  The header and body are accumulated in separate
** Blob structures then output sequentially once everything has been
** built.
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
** Check to see if the header contains the zNeedle string.  Return true
** if it does and false if it does not.
*/
int cgi_header_contains(const char *zNeedle){
  return strstr(blob_str(&cgiContent[0]), zNeedle)!=0;
}
int cgi_body_contains(const char *zNeedle){
  return strstr(blob_str(&cgiContent[1]), zNeedle)!=0;
}

/*
** Append reply content to what already exists.
*/
void cgi_append_content(const char *zData, int nAmt){
  blob_append(pContent, zData, nAmt);
}

/*
** Reset the HTTP reply text to be an empty string.
*/
void cgi_reset_content(void){
  blob_reset(&cgiContent[0]);
  blob_reset(&cgiContent[1]);
}

/*
** Return a pointer to the CGI output blob.
*/
Blob *cgi_output_blob(void){
  return pContent;
}

/*
** Combine the header and body of the CGI into a single string.
*/
static void cgi_combine_header_and_body(void){
  int size = blob_size(&cgiContent[1]);
  if( size>0 ){
    blob_append(&cgiContent[0], blob_buffer(&cgiContent[1]), size);
    blob_reset(&cgiContent[1]);
  }
}

/*
** Return a pointer to the HTTP reply text.
*/
char *cgi_extract_content(void){
  cgi_combine_header_and_body();
  return blob_buffer(&cgiContent[0]);
}

/*
** Additional information used to form the HTTP reply
*/
static char *zContentType = "text/html";     /* Content type of the reply */
static char *zReplyStatus = "OK";            /* Reply status description */
static int iReplyStatus = 200;               /* Reply status code */
static Blob extraHeader = BLOB_INITIALIZER;  /* Extra header text */

/*
** Set the reply content type
*/
void cgi_set_content_type(const char *zType){
  zContentType = mprintf("%s", zType);
}

/*
** Set the reply content to the specified BLOB.
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
  zReplyStatus = mprintf("%s", zStat);
  iReplyStatus = iStat;
}

/*
** Append text to the header of an HTTP reply
*/
void cgi_append_header(const char *zLine){
  blob_append(&extraHeader, zLine, -1);
}

/*
** Set a cookie.
**
** Zero lifetime implies a session cookie.
*/
void cgi_set_cookie(
  const char *zName,    /* Name of the cookie */
  const char *zValue,   /* Value of the cookie.  Automatically escaped */
  const char *zPath,    /* Path cookie applies to.  NULL means "/" */
  int lifetime          /* Expiration of the cookie in seconds from now */
){
  char *zSecure = "";
  if( zPath==0 ) zPath = g.zTop;
  if( g.zBaseURL!=0 && strncmp(g.zBaseURL, "https:", 6)==0 ){
    zSecure = " secure;";
  }
  if( lifetime>0 ){
    lifetime += (int)time(0);
    blob_appendf(&extraHeader,
       "Set-Cookie: %s=%t; Path=%s; expires=%z; HttpOnly;%s Version=1\r\n",
        zName, zValue, zPath, cgi_rfc822_datestamp(lifetime), zSecure);
  }else{
    blob_appendf(&extraHeader,
       "Set-Cookie: %s=%t; Path=%s; HttpOnly;%s Version=1\r\n",
       zName, zValue, zPath, zSecure);
  }
}

#if 0
/*
** Add an ETag header line
*/
static char *cgi_add_etag(char *zTxt, int nLen){
  MD5Context ctx;
  unsigned char digest[16];
  int i, j;
  char zETag[64];

  MD5Init(&ctx);
  MD5Update(&ctx,zTxt,nLen);
  MD5Final(digest,&ctx);
  for(j=i=0; i<16; i++,j+=2){
    bprintf(&zETag[j],sizeof(zETag)-j,"%02x",(int)digest[i]);
  }
  blob_appendf(&extraHeader, "ETag: %s\r\n", zETag);
  return fossil_strdup(zETag);
}

/*
** Do some cache control stuff. First, we generate an ETag and include it in
** the response headers. Second, we do whatever is necessary to determine if
** the request was asking about caching and whether we need to send back the
** response body. If we shouldn't send a body, return non-zero.
**
** Currently, we just check the ETag against any If-None-Match header.
**
** FIXME: In some cases (attachments, file contents) we could check
** If-Modified-Since headers and always include Last-Modified in responses.
*/
static int check_cache_control(void){
  /* FIXME: there's some gotchas wth cookies and some headers. */
  char *zETag = cgi_add_etag(blob_buffer(&cgiContent),blob_size(&cgiContent));
  char *zMatch = P("HTTP_IF_NONE_MATCH");

  if( zETag!=0 && zMatch!=0 ) {
    char *zBuf = fossil_strdup(zMatch);
    if( zBuf!=0 ){
      char *zTok = 0;
      char *zPos;
      for( zTok = strtok_r(zBuf, ",\"",&zPos);
           zTok && fossil_stricmp(zTok,zETag);
           zTok =  strtok_r(0, ",\"",&zPos)){}
      fossil_free(zBuf);
      if(zTok) return 1;
    }
  }

  return 0;
}
#endif

/*
** Return true if the response should be sent with Content-Encoding: gzip.
*/
static int is_gzippable(void){
  if( strstr(PD("HTTP_ACCEPT_ENCODING", ""), "gzip")==0 ) return 0;
  return strncmp(zContentType, "text/", 5)==0
    || sqlite3_strglob("application/*xml", zContentType)==0
    || sqlite3_strglob("application/*javascript", zContentType)==0;
}

/*
** Do a normal HTTP reply
*/
void cgi_reply(void){
  int total_size;
  if( iReplyStatus<=0 ){
    iReplyStatus = 200;
    zReplyStatus = "OK";
  }

#if 0
  if( iReplyStatus==200 && check_cache_control() ) {
    /* change the status to "unchanged" and we can skip sending the
    ** actual response body. Obviously we only do this when we _have_ a
    ** body (code 200).
    */
    iReplyStatus = 304;
    zReplyStatus = "Not Modified";
  }
#endif

  if( g.fullHttpReply ){
    fprintf(g.httpOut, "HTTP/1.0 %d %s\r\n", iReplyStatus, zReplyStatus);
    fprintf(g.httpOut, "Date: %s\r\n", cgi_rfc822_datestamp(time(0)));
    fprintf(g.httpOut, "Connection: close\r\n");
    fprintf(g.httpOut, "X-UA-Compatible: IE=edge\r\n");
  }else{
    fprintf(g.httpOut, "Status: %d %s\r\n", iReplyStatus, zReplyStatus);
  }

  if( blob_size(&extraHeader)>0 ){
    fprintf(g.httpOut, "%s", blob_buffer(&extraHeader));
  }

  /* Add headers to turn on useful security options in browsers. */
  fprintf(g.httpOut, "X-Frame-Options: SAMEORIGIN\r\n");
  /* This stops fossil pages appearing in frames or iframes, preventing
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

  if( g.isConst ){
    /* constant means that the input URL will _never_ generate anything
    ** else. In the case of attachments, the contents won't change because
    ** an attempt to change them generates a new attachment number. In the
    ** case of most /getfile calls for specific versions, the only way the
    ** content changes is if someone breaks the SCM. And if that happens, a
    ** stale cache is the least of the problem. So we provide an Expires
    ** header set to a reasonable period (default: one week).
    */
    fprintf(g.httpOut, "Cache-control: max-age=28800\r\n");
  }else{
    fprintf(g.httpOut, "Cache-control: no-cache\r\n");
  }

  /* Content intended for logged in users should only be cached in
  ** the browser, not some shared location.
  */
  fprintf(g.httpOut, "Content-Type: %s; charset=utf-8\r\n", zContentType);
  if( fossil_strcmp(zContentType,"application/x-fossil")==0 ){
    cgi_combine_header_and_body();
    blob_compress(&cgiContent[0], &cgiContent[0]);
  }

  if( iReplyStatus != 304 ) {
    if( is_gzippable() ){
      int i;
      gzip_begin(0);
      for( i=0; i<2; i++ ){
        int size = blob_size(&cgiContent[i]);
        if( size>0 ) gzip_step(blob_buffer(&cgiContent[i]), size);
        blob_reset(&cgiContent[i]);
      }
      gzip_finish(&cgiContent[0]);
      fprintf(g.httpOut, "Content-Encoding: gzip\r\n");
      fprintf(g.httpOut, "Vary: Accept-Encoding\r\n");
    }
    total_size = blob_size(&cgiContent[0]) + blob_size(&cgiContent[1]);
    fprintf(g.httpOut, "Content-Length: %d\r\n", total_size);
  }else{
    total_size = 0;
  }
  fprintf(g.httpOut, "\r\n");
  if( total_size>0 && iReplyStatus != 304
   && fossil_strcmp(P("REQUEST_METHOD"),"HEAD")!=0
  ){
    int i, size;
    for(i=0; i<2; i++){
      size = blob_size(&cgiContent[i]);
      if( size>0 ){
        fwrite(blob_buffer(&cgiContent[i]), 1, size, g.httpOut);
      }
    }
  }
  fflush(g.httpOut);
  CGIDEBUG(("DONE\n"));
}

/*
** Do a redirect request to the URL given in the argument.
**
** The URL must be relative to the base of the fossil server.
*/
NORETURN static void cgi_redirect_with_status(
  const char *zURL,
  int iStat,
  const char *zStat
){
  char *zLocation;
  CGIDEBUG(("redirect to %s\n", zURL));
  if( strncmp(zURL,"http:",5)==0 || strncmp(zURL,"https:",6)==0 ){
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
** Return the URL for the caller.  This is obtained from either the
** referer CGI parameter, if it exists, or the HTTP_REFERER HTTP parameter.
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
** Information about all query parameters and cookies are stored
** in these variables.
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
  nUsedQP++;
  sortQP = 1;
}

/*
** Add another query parameter or cookie to the parameter set.
** zName is the name of the query parameter or cookie and zValue
** is its fully decoded value.
**
** Copies are made of both the zName and zValue parameters.
*/
void cgi_set_parameter(const char *zName, const char *zValue){
  cgi_set_parameter_nocopy(mprintf("%s",zName), mprintf("%s",zValue), 0);
}
void cgi_set_query_parameter(const char *zName, const char *zValue){
  cgi_set_parameter_nocopy(mprintf("%s",zName), mprintf("%s",zValue), 1);
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
** Add a query parameter.  The zName portion is fixed but a copy
** must be made of zValue.
*/
void cgi_setenv(const char *zName, const char *zValue){
  cgi_set_parameter_nocopy(zName, mprintf("%s",zValue), 0);
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
** Parameters are separated by the "terminator" character.  Whitespace
** before the NAME is ignored.
**
** The input string "z" is modified but no copies is made.  "z"
** should not be deallocated or changed again after this routine
** returns or it will corrupt the parameter table.
*/
static void add_param_list(char *z, int terminator){
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
    if( fossil_islower(zName[0]) ){
      cgi_set_parameter_nocopy(zName, zValue, isQP);
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
** followed by the boundry marker zBoundry.  An extra "--" may or
** may not be appended to the boundry marker.  There are *pLen characters
** in *pz.
**
** This routine adds a "\000" to the end of the content (overwriting
** the "\r\n") and returns a pointer to the content.  The *pz input
** is adjusted to point to the first line following the boundry.
** The length of the content is stored in *pnContent.
*/
static char *get_bounded_content(
  char **pz,         /* Content taken from here */
  int *pLen,         /* Number of bytes of data in (*pz)[] */
  char *zBoundry,    /* Boundry text marking the end of content */
  int *pnContent     /* Write the size of the content here */
){
  char *z = *pz;
  int len = *pLen;
  int i;
  int nBoundry = strlen(zBoundry);
  *pnContent = len;
  for(i=0; i<len; i++){
    if( z[i]=='\n' && strncmp(zBoundry, &z[i+1], nBoundry)==0 ){
      if( i>0 && z[i-1]=='\r' ) i--;
      z[i] = 0;
      *pnContent = i;
      i += nBoundry;
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
  char *zBoundry;
  char *zValue;
  char *zName = 0;
  int showBytes = 0;
  char *azArg[50];

  zBoundry = get_line_from_string(&z, &len);
  if( zBoundry==0 ) return;
  while( (zLine = get_line_from_string(&z, &len))!=0 ){
    if( zLine[0]==0 ){
      int nContent = 0;
      zValue = get_bounded_content(&z, &len, zBoundry, &nContent);
      if( zName && zValue && fossil_islower(zName[0]) ){
        cgi_set_parameter_nocopy(zName, zValue, 1);
        if( showBytes ){
          cgi_set_parameter_nocopy(mprintf("%s:bytes", zName),
               mprintf("%d",nContent), 1);
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
          if( zName && z && fossil_islower(zName[0]) ){
            cgi_set_parameter_nocopy(mprintf("%s:filename",zName), z, 1);
          }
          showBytes = 1;
        }else if( c=='c' && sqlite3_strnicmp(azArg[i],"content-type:",n)==0 ){
          char *z = azArg[++i];
          if( zName && z && fossil_islower(zName[0]) ){
            cgi_set_parameter_nocopy(mprintf("%s:mimetype",zName), z, 1);
          }
        }
      }
    }
  }
}


#ifdef FOSSIL_ENABLE_JSON
/*
** Internal helper for cson_data_source_FILE_n().
*/
typedef struct CgiPostReadState_ {
    FILE * fh;
    unsigned int len;
    unsigned int pos;
} CgiPostReadState;

/*
** cson_data_source_f() impl which reads only up to
** a specified amount of data from its input FILE.
** state MUST be a full populated (CgiPostReadState*).
*/
static int cson_data_source_FILE_n( void * state,
                                    void * dest,
                                    unsigned int * n ){
    if( ! state || !dest || !n ) return cson_rc.ArgError;
    else {
      CgiPostReadState * st = (CgiPostReadState *)state;
      if( st->pos >= st->len ){
        *n = 0;
        return 0;
      }else if( !*n || ((st->pos + *n) > st->len) ){
        return cson_rc.RangeError;
      }else{
        unsigned int rsz = (unsigned int)fread( dest, 1, *n, st->fh );
        if( ! rsz ){
          *n = rsz;
          return feof(st->fh) ? 0 : cson_rc.IOError;
        }else{
          *n = rsz;
          st->pos += *n;
          return 0;
        }
      }
    }
}

/*
** Reads a JSON object from the first contentLen bytes of zIn.  On
** g.json.post is updated to hold the content. On error a
** FSL_JSON_E_INVALID_REQUEST response is output and fossil_exit() is
** called (in HTTP mode exit code 0 is used).
**
** If contentLen is 0 then the whole file is read.
*/
void cgi_parse_POST_JSON( FILE * zIn, unsigned int contentLen ){
  cson_value * jv = NULL;
  int rc;
  CgiPostReadState state;
  cson_parse_opt popt = cson_parse_opt_empty;
  cson_parse_info pinfo = cson_parse_info_empty;
  assert(g.json.gc.a && "json_main_bootstrap() was not called!");
  popt.maxDepth = 15;
  state.fh = zIn;
  state.len = contentLen;
  state.pos = 0;
  rc = cson_parse( &jv,
                   contentLen ? cson_data_source_FILE_n : cson_data_source_FILE,
                   contentLen ? (void *)&state : (void *)zIn, &popt, &pinfo );
  if(rc){
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
      free(msg);
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
    unsigned r;
    sqlite3_randomness(sizeof(r), &r);
    sqlite3_snprintf(sizeof(zFile), zFile, "httplog-%08x.txt", r);
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
static NORETURN void malformed_request(const char *zMsg);

/*
** Initialize the query parameter database.  Information is pulled from
** the QUERY_STRING environment variable (if it exists), from standard
** input if there is POST data, and from HTTP_COOKIE.
**
** REQUEST_URI, PATH_INFO, and SCRIPT_NAME are related as follows:
**
**      REQUEST_URI == SCRIPT_NAME + PATH_INFO
**
** Where "+" means concatenate.  Fossil requires SCRIPT_NAME.  If
** REQUEST_URI is provided but PATH_INFO is not, then PATH_INFO is
** computed from REQUEST_URI and SCRIPT_NAME.  If PATH_INFO is provided
** but REQUEST_URI is not, then compute REQUEST_URI from PATH_INFO and
** SCRIPT_NAME.  If neither REQUEST_URI nor PATH_INFO are provided, then
** assume that PATH_INFO is an empty string and set REQUEST_URI equal
** to PATH_INFO.
**
** SCGI typically omits PATH_INFO.  CGI sometimes omits REQUEST_URI and
** PATH_INFO when it is empty.
*/
void cgi_init(void){
  char *z;
  const char *zType;
  int len;
  const char *zRequestUri = cgi_parameter("REQUEST_URI",0);
  const char *zScriptName = cgi_parameter("SCRIPT_NAME",0);
  const char *zPathInfo = cgi_parameter("PATH_INFO",0);

#ifdef FOSSIL_ENABLE_JSON
  json_main_bootstrap();
#endif
  g.isHTTP = 1;
  cgi_destination(CGI_BODY);
  if( zScriptName==0 ) malformed_request("missing SCRIPT_NAME");
  if( zRequestUri==0 ){
    const char *z = zPathInfo;
    if( zPathInfo==0 ){
      malformed_request("missing PATH_INFO and/or REQUEST_URI");
    }
    if( z[0]=='/' ) z++;
    zRequestUri = mprintf("%s/%s", zScriptName, z);
    cgi_set_parameter("REQUEST_URI", zRequestUri);
  }
  if( zPathInfo==0 ){
    int i, j;
    for(i=0; zRequestUri[i]==zScriptName[i] && zRequestUri[i]; i++){}
    for(j=i; zRequestUri[j] && zRequestUri[j]!='?'; j++){}
    cgi_set_parameter("PATH_INFO", mprintf("%.*s", j-i, zRequestUri+i));
  }

  z = (char*)P("HTTP_COOKIE");
  if( z ){
    z = mprintf("%s",z);
    add_param_list(z, ';');
  }

  z = (char*)P("QUERY_STRING");
  if( z ){
    z = mprintf("%s",z);
    add_param_list(z, '&');
  }

  z = (char*)P("REMOTE_ADDR");
  if( z ){
    g.zIpAddr = mprintf("%s", z);
  }

  len = atoi(PD("CONTENT_LENGTH", "0"));
  g.zContentType = zType = P("CONTENT_TYPE");
  blob_zero(&g.cgiIn);
  if( len>0 && zType ){
    if( fossil_strcmp(zType,"application/x-www-form-urlencoded")==0
         || strncmp(zType,"multipart/form-data",19)==0 ){
      z = fossil_malloc( len+1 );
      len = fread(z, 1, len, g.httpIn);
      z[len] = 0;
      cgi_trace(z);
      if( zType[0]=='a' ){
        add_param_list(z, '&');
      }else{
        process_multipart_form_data(z, len);
      }
    }else if( fossil_strcmp(zType, "application/x-fossil")==0 ){
      blob_read_from_channel(&g.cgiIn, g.httpIn, len);
      blob_uncompress(&g.cgiIn, &g.cgiIn);
    }else if( fossil_strcmp(zType, "application/x-fossil-debug")==0 ){
      blob_read_from_channel(&g.cgiIn, g.httpIn, len);
    }else if( fossil_strcmp(zType, "application/x-fossil-uncompressed")==0 ){
      blob_read_from_channel(&g.cgiIn, g.httpIn, len);
    }
#ifdef FOSSIL_ENABLE_JSON
    else if( fossil_strcmp(zType, "application/json")
              || fossil_strcmp(zType,"text/plain")/*assume this MIGHT be JSON*/
              || fossil_strcmp(zType,"application/javascript")){
      g.json.isJsonMode = 1;
      cgi_parse_POST_JSON(g.httpIn, (unsigned int)len);
      /* FIXMEs:

      - See if fossil really needs g.cgiIn to be set for this purpose
      (i don't think it does). If it does then fill g.cgiIn and
      refactor to parse the JSON from there.

      - After parsing POST JSON, copy the "first layer" of keys/values
      to cgi_setenv(), honoring the upper-case distinction used
      in add_param_list(). However...

      - If we do that then we might get a disconnect in precedence of
      GET/POST arguments. i prefer for GET entries to take precedence
      over like-named POST entries, but in order for that to happen we
      need to process QUERY_STRING _after_ reading the POST data.
      */
      cgi_set_content_type(json_guess_content_type());
    }
#endif /* FOSSIL_ENABLE_JSON */
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

  /* Do a binary search for a matching query parameter */
  lo = 0;
  hi = nUsedQP-1;
  while( lo<=hi ){
    mid = (lo+hi)/2;
    c = fossil_strcmp(aParamQP[mid].zName, zName);
    if( c==0 ){
      CGIDEBUG(("mem-match [%s] = [%s]\n", zName, aParamQP[mid].zValue));
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
  if( zName && fossil_isupper(zName[0]) ){
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
** Return the value of a CGI parameter with leading and trailing
** spaces removed.
*/
char *cgi_parameter_trimmed(const char *zName, const char *zDefault){
  const char *zIn;
  char *zOut;
  int i;
  zIn = cgi_parameter(zName, 0);
  if( zIn==0 ) zIn = zDefault;
  while( fossil_isspace(zIn[0]) ) zIn++;
  zOut = fossil_strdup(zIn);
  for(i=0; zOut[i]; i++){}
  while( i>0 && fossil_isspace(zOut[i-1]) ) zOut[--i] = 0;
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
** Print all query parameters on standard output.  Format the
** parameters as HTML.  This is used for testing and debugging.
**
** Omit the values of the cookies unless showAll is true.
*/
void cgi_print_all(int showAll){
  int i;
  cgi_parameter("","");  /* Force the parameters into sorted order */
  for(i=0; i<nUsedQP; i++){
    const char *zName = aParamQP[i].zName;
    if( !showAll ){
      if( fossil_stricmp("HTTP_COOKIE",zName)==0 ) continue;
      if( fossil_strnicmp("fossil-",zName,7)==0 ) continue;
    }
    cgi_printf("%h = %h  <br />\n", zName, aParamQP[i].zValue);
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
static NORETURN void malformed_request(const char *zMsg){
  cgi_set_status(501, "Not Implemented");
  cgi_printf(
    "<html><body><p>Bad Request: %s</p></body></html>\n", zMsg
  );
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
  if( fossil_strcmp(g.zIpAddr, "127.0.0.1")!=0 ) return 0;

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
  struct sockaddr_in remoteName;
  socklen_t size = sizeof(struct sockaddr_in);
  char zLine[2000];     /* A single line of input. */
  g.fullHttpReply = 1;
  if( fgets(zLine, sizeof(zLine),g.httpIn)==0 ){
    malformed_request("missing HTTP header");
  }
  blob_append(&g.httpHeader, zLine, -1);
  cgi_trace(zLine);
  zToken = extract_token(zLine, &z);
  if( zToken==0 ){
    malformed_request("malformed HTTP header");
  }
  if( fossil_strcmp(zToken,"GET")!=0 && fossil_strcmp(zToken,"POST")!=0
      && fossil_strcmp(zToken,"HEAD")!=0 ){
    malformed_request("unsupported HTTP method");
  }
  cgi_setenv("GATEWAY_INTERFACE","CGI/1.0");
  cgi_setenv("REQUEST_METHOD",zToken);
  zToken = extract_token(z, &z);
  if( zToken==0 ){
    malformed_request("malformed URL in HTTP header");
  }
  cgi_setenv("REQUEST_URI", zToken);
  cgi_setenv("SCRIPT_NAME", "");
  for(i=0; zToken[i] && zToken[i]!='?'; i++){}
  if( zToken[i] ) zToken[i++] = 0;
  cgi_setenv("PATH_INFO", zToken);
  cgi_setenv("QUERY_STRING", &zToken[i]);
  if( zIpAddr==0 &&
        getpeername(fileno(g.httpIn), (struct sockaddr*)&remoteName,
                                &size)>=0
  ){
    zIpAddr = inet_ntoa(remoteName.sin_addr);
  }
  if( zIpAddr ){
    cgi_setenv("REMOTE_ADDR", zIpAddr);
    g.zIpAddr = mprintf("%s", zIpAddr);
  }

  /* Get all the optional fields that follow the first line.
  */
  while( fgets(zLine,sizeof(zLine),g.httpIn) ){
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
    }else if( fossil_strcmp(zFieldName,"host:")==0 ){
      cgi_setenv("HTTP_HOST", zVal);
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
    }else if( fossil_strcmp(zFieldName,"x-forwarded-for:")==0 ){
      const char *zIpAddr = cgi_accept_forwarded_for(zVal);
      if( zIpAddr!=0 ){
        g.zIpAddr = mprintf("%s", zIpAddr);
        cgi_replace_parameter("REMOTE_ADDR", g.zIpAddr);
      }
    }
  }
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

  if( zIpAddr ){
    if( nCycles==0 ){
      cgi_setenv("REMOTE_ADDR", zIpAddr);
      g.zIpAddr = mprintf("%s", zIpAddr);
    }
  }else{
    fossil_panic("missing SSH IP address");
  }
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
    cgi_replace_parameter("PATH_INFO", mprintf("%s",zToken));
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
      g.zContentType = zType = mprintf("%s", zVal);
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
  return mprintf("%s", zToken);
}

/*
** This routine handles the old fossil SSH transport_flip
** and transport_open communications if detected.
*/
void cgi_handle_ssh_transport(const char *zCmd){
  char *z, *zToken;
  char zLine[2000];     /* A single line of input. */

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

#endif /* INTERFACE */

/*
** Maximum number of child processes that we can have running
** at one time before we start slowing things down.
*/
#define MAX_PARALLEL 2

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
  int listener = -1;           /* The server socket */
  int connection;              /* A socket for each individual connection */
  fd_set readfds;              /* Set of file descriptors for select() */
  socklen_t lenaddr;           /* Length of the inaddr structure */
  int child;                   /* PID of the child process */
  int nchildren = 0;           /* Number of child processes */
  struct timeval delay;        /* How long to wait inside select() */
  struct sockaddr_in inaddr;   /* The socket address */
  int opt = 1;                 /* setsockopt flag */
  int iPort = mnPort;

  while( iPort<=mxPort ){
    memset(&inaddr, 0, sizeof(inaddr));
    inaddr.sin_family = AF_INET;
    if( zIpAddr ){
      inaddr.sin_addr.s_addr = inet_addr(zIpAddr);
      if( inaddr.sin_addr.s_addr == (-1) ){
        fossil_fatal("not a valid IP address: %s", zIpAddr);
      }
    }else if( flags & HTTP_SERVER_LOCALHOST ){
      inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }else{
      inaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    inaddr.sin_port = htons(iPort);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if( listener<0 ){
      iPort++;
      continue;
    }

    /* if we can't terminate nicely, at least allow the socket to be reused */
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    if( bind(listener, (struct sockaddr*)&inaddr, sizeof(inaddr))<0 ){
      close(listener);
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
  if( iPort>mxPort ) return 1;
  listen(listener,10);
  fossil_print("Listening for %s requests on TCP port %d\n",
     (flags & HTTP_SERVER_SCGI)!=0?"SCGI":"HTTP",  iPort);
  fflush(stdout);
  if( zBrowser ){
    assert( strstr(zBrowser,"%d")!=0 );
    zBrowser = mprintf(zBrowser /*works-like:"%d"*/, iPort);
#if defined(__CYGWIN__)
    /* On Cygwin, we can do better than "echo" */
    if( strncmp(zBrowser, "echo ", 5)==0 ){
      wchar_t *wUrl = fossil_utf8_to_unicode(zBrowser+5);
      wUrl[wcslen(wUrl)-2] = 0; /* Strip terminating " &" */
      if( (size_t)ShellExecuteW(0, L"open", wUrl, 0, 0, 1)<33 ){
        fossil_warning("cannot start browser\n");
      }
    }else
#endif
    if( system(zBrowser)<0 ){
      fossil_warning("cannot start browser: %s\n", zBrowser);
    }
  }
  while( 1 ){
    if( nchildren>MAX_PARALLEL ){
      /* Slow down if connections are arriving too fast */
      sleep( nchildren-MAX_PARALLEL );
    }
    delay.tv_sec = 60;
    delay.tv_usec = 0;
    FD_ZERO(&readfds);
    assert( listener>=0 );
    FD_SET( listener, &readfds);
    select( listener+1, &readfds, 0, 0, &delay);
    if( FD_ISSET(listener, &readfds) ){
      lenaddr = sizeof(inaddr);
      connection = accept(listener, (struct sockaddr*)&inaddr, &lenaddr);
      if( connection>=0 ){
        child = fork();
        if( child!=0 ){
          if( child>0 ) nchildren++;
          close(connection);
        }else{
          int nErr = 0, fd;
          close(0);
          fd = dup(connection);
          if( fd!=0 ) nErr++;
          close(1);
          fd = dup(connection);
          if( fd!=1 ) nErr++;
          if( !g.fAnyTrace ){
            close(2);
            fd = dup(connection);
            if( fd!=2 ) nErr++;
          }
          close(connection);
          return nErr;
        }
      }
    }
    /* Bury dead children */
    while( waitpid(0, 0, WNOHANG)>0 ){
      nchildren--;
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
** string obtained from mprintf() and must be freed using free() to
** avoid a memory leak.
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
    return mprintf("%s, %d %s %02d %02d:%02d:%02d GMT",
                   azDays[pTm->tm_wday], pTm->tm_mday, azMonths[pTm->tm_mon],
                   pTm->tm_year+1900, pTm->tm_hour, pTm->tm_min, pTm->tm_sec);
  }
}

/*
** Parse an RFC822-formatted timestamp as we'd expect from HTTP and return
** a Unix epoch time. <= zero is returned on failure.
**
** Note that this won't handle all the _allowed_ HTTP formats, just the
** most popular one (the one generated by cgi_rfc822_datestamp(), actually).
*/
time_t cgi_rfc822_parsedate(const char *zDate){
  struct tm t;
  char zIgnore[16];
  char zMonth[16];

  memset(&t, 0, sizeof(t));
  if( 7==sscanf(zDate, "%12[A-Za-z,] %d %12[A-Za-z] %d %d:%d:%d", zIgnore,
                       &t.tm_mday, zMonth, &t.tm_year, &t.tm_hour, &t.tm_min,
                       &t.tm_sec)){

    if( t.tm_year > 1900 ) t.tm_year -= 1900;
    for(t.tm_mon=0; azMonths[t.tm_mon]; t.tm_mon++){
      if( !fossil_strnicmp( azMonths[t.tm_mon], zMonth, 3 )){
        return mkgmtime(&t);
      }
    }
  }

  return 0;
}

/*
** Convert a struct tm* that represents a moment in UTC into the number
** of seconds in 1970, UTC.
*/
time_t mkgmtime(struct tm *p){
  time_t t;
  int nDay;
  int isLeapYr;
  /* Days in each month:       31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 */
  static int priorDays[]   = {  0, 31, 59, 90,120,151,181,212,243,273,304,334 };
  if( p->tm_mon<0 ){
    int nYear = (11 - p->tm_mon)/12;
    p->tm_year -= nYear;
    p->tm_mon += nYear*12;
  }else if( p->tm_mon>11 ){
    p->tm_year += p->tm_mon/12;
    p->tm_mon %= 12;
  }
  isLeapYr = p->tm_year%4==0 && (p->tm_year%100!=0 || (p->tm_year+300)%400==0);
  p->tm_yday = priorDays[p->tm_mon] + p->tm_mday - 1;
  if( isLeapYr && p->tm_mon>1 ) p->tm_yday++;
  nDay = (p->tm_year-70)*365 + (p->tm_year-69)/4 -p->tm_year/100 +
         (p->tm_year+300)/400 + p->tm_yday;
  t = ((nDay*24 + p->tm_hour)*60 + p->tm_min)*60 + p->tm_sec;
  return t;
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
    char *zSshClient = mprintf("%s",zSshConn);
    if( (zIndex = strchr(zSshClient,' '))!=0 ){
      zSshClient[zIndex-zSshClient] = '\0';
      return zSshClient;
    }
  }
  return zDefault;
}
