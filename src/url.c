/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code for parsing URLs that appear on the command-line
*/
#include "config.h"
#include "url.h"
#include <stdio.h>

#ifdef _WIN32
#include <io.h>
#ifndef isatty
#define isatty(d) _isatty(d)
#endif
#ifndef fileno
#define fileno(s) _fileno(s)
#endif
#endif

#if INTERFACE
/*
** Flags for url_parse()
*/
#define URL_PROMPT_PW        0x001  /* Prompt for password if needed */
#define URL_REMEMBER         0x002  /* Remember the url for later reuse */
#define URL_ASK_REMEMBER_PW  0x004  /* Ask whether to remember prompted pw */
#define URL_REMEMBER_PW      0x008  /* Should remember pw */
#define URL_PROMPTED         0x010  /* Prompted for PW already */
#define URL_OMIT_USER        0x020  /* Omit the user name from URL */

/*
** The URL related data used with this subsystem.
*/
struct UrlData {
  int isFile;      /* True if a "file:" url */
  int isHttps;     /* True if a "https:" url */
  int isSsh;       /* True if an "ssh:" url */
  char *name;      /* Hostname for http: or filename for file: */
  char *hostname;  /* The HOST: parameter on http headers */
  char *protocol;  /* "http" or "https" */
  int port;        /* TCP port number for http: or https: */
  int dfltPort;    /* The default port for the given protocol */
  char *path;      /* Pathname for http: */
  char *user;      /* User id for http: */
  char *passwd;    /* Password for http: */
  char *canonical; /* Canonical representation of the URL */
  char *proxyAuth; /* Proxy-Authorizer: string */
  char *fossil;    /* The fossil query parameter on ssh: */
  unsigned flags;  /* Boolean flags controlling URL processing */
  int useProxy;    /* Used to remember that a proxy is in use */
  char *proxyUrlPath;
  int proxyOrigPort; /* Tunneled port number for https through proxy */
};
#endif /* INTERFACE */


/*
** Convert a string to lower-case.
*/
static void url_tolower(char *z){
  while( *z ){
     *z = fossil_tolower(*z);
     z++;
  }
}

/*
** Parse the given URL.  Populate members of the provided UrlData structure
** as follows:
**
**      isFile      True if FILE:
**      isHttps     True if HTTPS:
**      isSsh       True if SSH:
**      protocol    "http" or "https" or "file"
**      name        Hostname for HTTP:, HTTPS:, SSH:.  Filename for FILE:
**      port        TCP port number for HTTP or HTTPS.
**      dfltPort    Default TCP port number (80 or 443).
**      path        Path name for HTTP or HTTPS.
**      user        Userid.
**      passwd      Password.
**      hostname    HOST:PORT or just HOST if port is the default.
**      canonical   The URL in canonical form, omitting the password
**
*/
void url_parse_local(
  const char *zUrl,
  unsigned int urlFlags,
  UrlData *pUrlData
){
  int i, j, c;
  char *zFile = 0;

  if( zUrl==0 ){
    zUrl = db_get("last-sync-url", 0);
    if( zUrl==0 ) return;
    if( pUrlData->passwd==0 ){
      pUrlData->passwd = unobscure(db_get("last-sync-pw", 0));
    }
  }

  if( strncmp(zUrl, "http://", 7)==0
   || strncmp(zUrl, "https://", 8)==0
   || strncmp(zUrl, "ssh://", 6)==0
  ){
    int iStart;
    char *zLogin;
    char *zExe;
    char cQuerySep = '?';

    pUrlData->isFile = 0;
    pUrlData->useProxy = 0;
    if( zUrl[4]=='s' ){
      pUrlData->isHttps = 1;
      pUrlData->protocol = "https";
      pUrlData->dfltPort = 443;
      iStart = 8;
    }else if( zUrl[0]=='s' ){
      pUrlData->isSsh = 1;
      pUrlData->protocol = "ssh";
      pUrlData->dfltPort = 22;
      pUrlData->fossil = "fossil";
      iStart = 6;
    }else{
      pUrlData->isHttps = 0;
      pUrlData->protocol = "http";
      pUrlData->dfltPort = 80;
      iStart = 7;
    }
    for(i=iStart; (c=zUrl[i])!=0 && c!='/' && c!='@'; i++){}
    if( c=='@' ){
      /* Parse up the user-id and password */
      for(j=iStart; j<i && zUrl[j]!=':'; j++){}
      pUrlData->user = mprintf("%.*s", j-iStart, &zUrl[iStart]);
      dehttpize(pUrlData->user);
      if( j<i ){
        if( ( urlFlags & URL_REMEMBER ) && pUrlData->isSsh==0 ){
          urlFlags |= URL_ASK_REMEMBER_PW;
        }
        pUrlData->passwd = mprintf("%.*s", i-j-1, &zUrl[j+1]);
        dehttpize(pUrlData->passwd);
      }
      if( pUrlData->isSsh ){
        urlFlags &= ~URL_ASK_REMEMBER_PW;
      }
      if( urlFlags & URL_OMIT_USER ){
        zLogin = mprintf("");
      }else{
        zLogin = mprintf("%t@", pUrlData->user);
      }
      for(j=i+1; (c=zUrl[j])!=0 && c!='/' && c!=':'; j++){}
      pUrlData->name = mprintf("%.*s", j-i-1, &zUrl[i+1]);
      i = j;
    }else{
      int inSquare = 0;
      int n;
      for(i=iStart; (c=zUrl[i])!=0 && c!='/' && (inSquare || c!=':'); i++){
        if( c=='[' ) inSquare = 1;
        if( c==']' ) inSquare = 0;
      }
      pUrlData->name = mprintf("%.*s", i-iStart, &zUrl[iStart]);
      n = strlen(pUrlData->name);
      if( pUrlData->name[0]=='[' && n>2 && pUrlData->name[n-1]==']' ){
        pUrlData->name++;
        pUrlData->name[n-2] = 0;
      }
      zLogin = mprintf("");
    }
    url_tolower(pUrlData->name);
    if( c==':' ){
      pUrlData->port = 0;
      i++;
      while( (c = zUrl[i])!=0 && fossil_isdigit(c) ){
        pUrlData->port = pUrlData->port*10 + c - '0';
        i++;
      }
      pUrlData->hostname = mprintf("%s:%d", pUrlData->name, pUrlData->port);
    }else{
      pUrlData->port = pUrlData->dfltPort;
      pUrlData->hostname = pUrlData->name;
    }
    dehttpize(pUrlData->name);
    pUrlData->path = mprintf("%s", &zUrl[i]);
    for(i=0; pUrlData->path[i] && pUrlData->path[i]!='?'; i++){}
    if( pUrlData->path[i] ){
      pUrlData->path[i] = 0;
      i++;
    }
    zExe = mprintf("");
    while( pUrlData->path[i]!=0 ){
      char *zName, *zValue;
      zName = &pUrlData->path[i];
      zValue = zName;
      while( pUrlData->path[i] && pUrlData->path[i]!='=' ){ i++; }
      if( pUrlData->path[i]=='=' ){
        pUrlData->path[i] = 0;
        i++;
        zValue = &pUrlData->path[i];
        while( pUrlData->path[i] && pUrlData->path[i]!='&' ){ i++; }
      }
      if( pUrlData->path[i] ){
        pUrlData->path[i] = 0;
        i++;
      }
      if( fossil_strcmp(zName,"fossil")==0 ){
        pUrlData->fossil = zValue;
        dehttpize(pUrlData->fossil);
        zExe = mprintf("%cfossil=%T", cQuerySep, pUrlData->fossil);
        cQuerySep = '&';
      }
    }

    dehttpize(pUrlData->path);
    if( pUrlData->dfltPort==pUrlData->port ){
      pUrlData->canonical = mprintf(
        "%s://%s%T%T%s",
        pUrlData->protocol, zLogin, pUrlData->name, pUrlData->path, zExe
      );
    }else{
      pUrlData->canonical = mprintf(
        "%s://%s%T:%d%T%s",
        pUrlData->protocol, zLogin, pUrlData->name, pUrlData->port,
        pUrlData->path, zExe
      );
    }
    if( pUrlData->isSsh && pUrlData->path[1] ) pUrlData->path++;
    free(zLogin);
  }else if( strncmp(zUrl, "file:", 5)==0 ){
    pUrlData->isFile = 1;
    if( zUrl[5]=='/' && zUrl[6]=='/' ){
      i = 7;
    }else{
      i = 5;
    }
    zFile = mprintf("%s", &zUrl[i]);
  }else if( file_isfile(zUrl) ){
    pUrlData->isFile = 1;
    zFile = mprintf("%s", zUrl);
  }else if( file_isdir(zUrl)==1 ){
    zFile = mprintf("%s/FOSSIL", zUrl);
    if( file_isfile(zFile) ){
      pUrlData->isFile = 1;
    }else{
      free(zFile);
      zFile = 0;
      fossil_fatal("unknown repository: %s", zUrl);
    }
  }else{
    fossil_fatal("unknown repository: %s", zUrl);
  }
  if( urlFlags ) pUrlData->flags = urlFlags;
  if( pUrlData->isFile ){
    Blob cfile;
    dehttpize(zFile);
    file_canonical_name(zFile, &cfile, 0);
    free(zFile);
    zFile = 0;
    pUrlData->protocol = "file";
    pUrlData->path = "";
    pUrlData->name = mprintf("%b", &cfile);
    pUrlData->canonical = mprintf("file://%T", pUrlData->name);
    blob_reset(&cfile);
  }else if( pUrlData->user!=0 && pUrlData->passwd==0 && (urlFlags & URL_PROMPT_PW) ){
    url_prompt_for_password_local(pUrlData);
  }else if( pUrlData->user!=0 && ( urlFlags & URL_ASK_REMEMBER_PW ) ){
    if( isatty(fileno(stdin)) ){
      if( save_password_prompt(pUrlData->passwd) ){
        pUrlData->flags = urlFlags |= URL_REMEMBER_PW;
      }else{
        pUrlData->flags = urlFlags &= ~URL_REMEMBER_PW;
      }
    }
  }
}

/*
** Parse the given URL, which describes a sync server.  Populate variables
** in the global "g" structure as follows:
**
**      g.url.isFile      True if FILE:
**      g.url.isHttps     True if HTTPS:
**      g.url.isSsh       True if SSH:
**      g.url.protocol    "http" or "https" or "file"
**      g.url.name        Hostname for HTTP:, HTTPS:, SSH:.  Filename for FILE:
**      g.url.port        TCP port number for HTTP or HTTPS.
**      g.url.dfltPort    Default TCP port number (80 or 443).
**      g.url.path        Path name for HTTP or HTTPS.
**      g.url.user        Userid.
**      g.url.passwd      Password.
**      g.url.hostname    HOST:PORT or just HOST if port is the default.
**      g.url.canonical   The URL in canonical form, omitting the password
**
** HTTP url format as follows (HTTPS is the same with a different scheme):
**
**     http://userid:password@host:port/path
**
** SSH url format is:
**
**     ssh://userid@host:port/path?fossil=path/to/fossil.exe
**
*/
void url_parse(const char *zUrl, unsigned int urlFlags){
  url_parse_local(zUrl, urlFlags, &g.url);
}

/*
** COMMAND: test-urlparser
**
** Usage: %fossil test-urlparser URL ?options?
**
**    --remember      Store results in last-sync-url
**    --prompt-pw     Prompt for password if missing
*/
void cmd_test_urlparser(void){
  int i;
  unsigned fg = 0;
  url_proxy_options();
  if( find_option("remember",0,0) ){
    db_must_be_within_tree();
    fg |= URL_REMEMBER;
  }
  if( find_option("prompt-pw",0,0) ) fg |= URL_PROMPT_PW;
  if( g.argc!=3 && g.argc!=4 ){
    usage("URL");
  }
  url_parse(g.argv[2], fg);
  for(i=0; i<2; i++){
    fossil_print("g.url.isFile    = %d\n", g.url.isFile);
    fossil_print("g.url.isHttps   = %d\n", g.url.isHttps);
    fossil_print("g.url.isSsh     = %d\n", g.url.isSsh);
    fossil_print("g.url.protocol  = %s\n", g.url.protocol);
    fossil_print("g.url.name      = %s\n", g.url.name);
    fossil_print("g.url.port      = %d\n", g.url.port);
    fossil_print("g.url.dfltPort  = %d\n", g.url.dfltPort);
    fossil_print("g.url.hostname  = %s\n", g.url.hostname);
    fossil_print("g.url.path      = %s\n", g.url.path);
    fossil_print("g.url.user      = %s\n", g.url.user);
    fossil_print("g.url.passwd    = %s\n", g.url.passwd);
    fossil_print("g.url.canonical = %s\n", g.url.canonical);
    fossil_print("g.url.fossil    = %s\n", g.url.fossil);
    fossil_print("g.url.flags     = 0x%02x\n", g.url.flags);
    if( g.url.isFile || g.url.isSsh ) break;
    if( i==0 ){
      fossil_print("********\n");
      url_enable_proxy("Using proxy: ");
    }
  }
}

/*
** Proxy specified on the command-line using the --proxy option.
** If there is no --proxy option on the command-line then this
** variable holds a NULL pointer.
*/
static const char *zProxyOpt = 0;

/*
** Extract any proxy options from the command-line.
**
**    --proxy URL|off
**
** This also happens to be a convenient function to use to look for
** the --nosync option that will temporarily disable the "autosync"
** feature.
*/
void url_proxy_options(void){
  zProxyOpt = find_option("proxy", 0, 1);
  if( find_option("nosync",0,0) ) g.fNoSync = 1;
  if( find_option("ipv4",0,0) ) g.fIPv4 = 1;
}

/*
** If the "proxy" setting is defined, then change the URL settings
** (initialized by a prior call to url_parse()) so that the HTTP
** header will be appropriate for the proxy and so that the TCP/IP
** connection will be opened to the proxy rather than to the server.
**
** If zMsg is not NULL and a proxy is used, then print zMsg followed
** by the canonical name of the proxy (with userid and password suppressed).
*/
void url_enable_proxy(const char *zMsg){
  const char *zProxy;
  zProxy = zProxyOpt;
  if( zProxy==0 ){
    zProxy = db_get("proxy", 0);
    if( zProxy==0 || zProxy[0]==0 || is_false(zProxy) ){
      zProxy = fossil_getenv("http_proxy");
    }
  }
  if( zProxy && zProxy[0] && !is_false(zProxy)
      && !g.url.isSsh && !g.url.isFile ){
    char *zOriginalUrl = g.url.canonical;
    char *zOriginalHost = g.url.hostname;
    int fOriginalIsHttps = g.url.isHttps;
    char *zOriginalUser = g.url.user;
    char *zOriginalPasswd = g.url.passwd;
    char *zOriginalUrlPath = g.url.path;
    int iOriginalPort = g.url.port;
    unsigned uOriginalFlags = g.url.flags;
    g.url.user = 0;
    g.url.passwd = "";
    url_parse(zProxy, 0);
    if( zMsg ) fossil_print("%s%s\n", zMsg, g.url.canonical);
    g.url.path = zOriginalUrl;
    g.url.hostname = zOriginalHost;
    if( g.url.user ){
      char *zCredentials1 = mprintf("%s:%s", g.url.user, g.url.passwd);
      char *zCredentials2 = encode64(zCredentials1, -1);
      g.url.proxyAuth = mprintf("Basic %z", zCredentials2);
      free(zCredentials1);
    }
    g.url.user = zOriginalUser;
    g.url.passwd = zOriginalPasswd;
    g.url.isHttps = fOriginalIsHttps;
    g.url.useProxy = 1;
    g.url.proxyUrlPath = zOriginalUrlPath;
    g.url.proxyOrigPort = iOriginalPort;
    g.url.flags = uOriginalFlags;
  }
}

#if INTERFACE
/*
** An instance of this object is used to build a URL with query parameters.
*/
struct HQuery {
  Blob url;                  /* The URL */
  const char *zBase;         /* The base URL */
  int nParam;                /* Number of parameters. */
  int nAlloc;                /* Number of allocated slots */
  const char **azName;       /* Parameter names */
  const char **azValue;      /* Parameter values */
};
#endif

/*
** Initialize the URL object.
*/
void url_initialize(HQuery *p, const char *zBase){
  memset(p, 0, sizeof(*p));
  blob_zero(&p->url);
  p->zBase = zBase;
}

/*
** Resets the given URL object, deallocating any memory
** it uses.
*/
void url_reset(HQuery *p){
  blob_reset(&p->url);
  fossil_free((void *)p->azName);
  fossil_free((void *)p->azValue);
  url_initialize(p, p->zBase);
}

/*
** Add a fixed parameter to an HQuery.  Or remove the parameters if zValue==0.
*/
void url_add_parameter(HQuery *p, const char *zName, const char *zValue){
  int i;
  for(i=0; i<p->nParam; i++){
    if( fossil_strcmp(p->azName[i],zName)==0 ){
      if( zValue==0 ){
        p->nParam--;
        p->azValue[i] = p->azValue[p->nParam];
        p->azName[i] = p->azName[p->nParam];
      }else{
        p->azValue[i] = zValue;
      }
      return;
    }
  }
  assert( i==p->nParam );
  if( zValue==0 ) return;
  if( i>=p->nAlloc ){
    p->nAlloc = p->nAlloc*2 + 10;
    p->azName = fossil_realloc((void *)p->azName,
                               sizeof(p->azName[0])*p->nAlloc);
    p->azValue = fossil_realloc((void *)p->azValue,
                                sizeof(p->azValue[0])*p->nAlloc);
  }
  p->azName[i] = zName;
  p->azValue[i] = zValue;
  p->nParam++;
}

/*
** Render the URL with a parameter override.
**
** Returned memory is transient and is overwritten on the next call to this
** routine for the same HQuery, or until the HQuery object is destroyed.
*/
char *url_render(
  HQuery *p,              /* Base URL */
  const char *zName1,     /* First override */
  const char *zValue1,    /* First override value */
  const char *zName2,     /* Second override */
  const char *zValue2     /* Second override value */
){
  const char *zSep = "?";
  int i;

  blob_reset(&p->url);
  blob_appendf(&p->url, "%s/%s", g.zTop, p->zBase);
  for(i=0; i<p->nParam; i++){
    const char *z = p->azValue[i];
    if( zName1 && fossil_strcmp(zName1,p->azName[i])==0 ){
      zName1 = 0;
      z = zValue1;
      if( z==0 ) continue;
    }
    if( zName2 && fossil_strcmp(zName2,p->azName[i])==0 ){
      zName2 = 0;
      z = zValue2;
      if( z==0 ) continue;
    }
    blob_appendf(&p->url, "%s%s", zSep, p->azName[i]);
    if( z && z[0] ) blob_appendf(&p->url, "=%T", z);
    zSep = "&";
  }
  if( zName1 && zValue1 ){
    blob_appendf(&p->url, "%s%s", zSep, zName1);
    if( zValue1[0] ) blob_appendf(&p->url, "=%T", zValue1);
  }
  if( zName2 && zValue2 ){
    blob_appendf(&p->url, "%s%s", zSep, zName2);
    if( zValue2[0] ) blob_appendf(&p->url, "=%T", zValue2);
  }
  return blob_str(&p->url);
}

/*
** Prompt the user for the password that corresponds to the "user" member of
** the provided UrlData structure.  Store the result into the "passwd" member
** of the provided UrlData structure.
*/
void url_prompt_for_password_local(UrlData *pUrlData){
  if( pUrlData->isSsh || pUrlData->isFile ) return;
  if( isatty(fileno(stdin))
   && (pUrlData->flags & URL_PROMPT_PW)!=0
   && (pUrlData->flags & URL_PROMPTED)==0
  ){
    pUrlData->flags |= URL_PROMPTED;
    pUrlData->passwd = prompt_for_user_password(pUrlData->user);
    if( pUrlData->passwd[0]
     && (pUrlData->flags & (URL_REMEMBER|URL_ASK_REMEMBER_PW))!=0
    ){
      if( save_password_prompt(pUrlData->passwd) ){
        pUrlData->flags |= URL_REMEMBER_PW;
      }else{
        pUrlData->flags &= ~URL_REMEMBER_PW;
      }
    }
  }else{
    fossil_fatal("missing or incorrect password for user \"%s\"",
                 pUrlData->user);
  }
}

/*
** Prompt the user for the password for g.url.user.  Store the result
** in g.url.passwd.
*/
void url_prompt_for_password(void){
  url_prompt_for_password_local(&g.url);
}

/*
** Remember the URL and password if requested.
*/
void url_remember(void){
  if( g.url.flags & URL_REMEMBER ){
    db_set("last-sync-url", g.url.canonical, 0);
    if( g.url.user!=0 && g.url.passwd!=0 && ( g.url.flags & URL_REMEMBER_PW ) ){
      db_set("last-sync-pw", obscure(g.url.passwd), 0);
    }
  }
}

/* Preemptively prompt for a password if a username is given in the
** URL but no password.
*/
void url_get_password_if_needed(void){
  if( (g.url.user && g.url.user[0])
   && (g.url.passwd==0 || g.url.passwd[0]==0)
   && isatty(fileno(stdin))
  ){
    url_prompt_for_password();
  }
}
