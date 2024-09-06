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
#endif

#if INTERFACE
/*
** Flags for url_parse()
*/
#define URL_PROMPT_PW        0x0001  /* Prompt for password if needed */
#define URL_REMEMBER         0x0002  /* Remember the url for later reuse */
#define URL_ASK_REMEMBER_PW  0x0004  /* Ask whether to remember prompted pw */
#define URL_REMEMBER_PW      0x0008  /* Should remember pw */
#define URL_PROMPTED         0x0010  /* Prompted for PW already */
#define URL_OMIT_USER        0x0020  /* Omit the user name from URL */
#define URL_USE_CONFIG       0x0040  /* Use remembered URLs from CONFIG table */
#define URL_USE_PARENT       0x0080  /* Use the URL of the parent project */
#define URL_SSH_PATH         0x0100  /* Include PATH= on SSH syncs */
#define URL_SSH_RETRY        0x0200  /* This a retry of an SSH */
#define URL_SSH_EXE          0x0400  /* ssh: URL contains fossil= query param*/

/*
** The URL related data used with this subsystem.
*/
struct UrlData {
  int isFile;           /* True if a "file:" url */
  int isHttps;          /* True if a "https:" url */
  int isSsh;            /* True if an "ssh:" url */
  int isAlias;          /* Input URL was an alias */
  char *name;           /* Hostname for http: or filename for file: */
  char *hostname;       /* The HOST: parameter on http headers */
  const char *protocol; /* "http" or "https" or "ssh" or "file" */
  int port;             /* TCP port number for http: or https: */
  int dfltPort;         /* The default port for the given protocol */
  char *path;           /* Pathname for http: */
  char *user;           /* User id for http: */
  char *passwd;         /* Password for http: */
  char *canonical;      /* Canonical representation of the URL */
  char *proxyAuth;      /* Proxy-Authorizer: string */
  char *fossil;         /* The fossil query parameter on ssh: */
  char *pwConfig;       /* CONFIG table entry that gave us the password */
  unsigned flags;       /* Boolean flags controlling URL processing */
  int useProxy;         /* Used to remember that a proxy is in use */
  int proxyOrigPort;       /* Tunneled port number for https through proxy */
  char *proxyUrlPath;      /* Remember path when proxy is use */
  char *proxyUrlCanonical; /* Remember canonical path when proxy is use */
};
#endif /* INTERFACE */


/*
** Parse the URL in the zUrl argument. Store results in the pUrlData object.
** Populate members of pUrlData as follows:
**
**      isFile      True if FILE:
**      isHttps     True if HTTPS:
**      isSsh       True if SSH:
**      protocol    "http" or "https" or "file" or "ssh"
**      name        Hostname for HTTP:, HTTPS:, SSH:.  Filename for FILE:
**      port        TCP port number for HTTP or HTTPS.
**      dfltPort    Default TCP port number (80 or 443).
**      path        Path name for HTTP or HTTPS.
**      user        Userid.
**      passwd      Password.
**      hostname    HOST:PORT or just HOST if port is the default.
**      canonical   The URL in canonical form, omitting the password
**
** If URL_USECONFIG is set and zUrl is NULL or "default", then parse the
** URL stored in last-sync-url and last-sync-pw of the CONFIG table.  Or if
** URL_USE_PARENT is also set, then use parent-project-url and
** parent-project-pw from the CONFIG table instead of last-sync-url
** and last-sync-pw.
**
** If URL_USE_CONFIG is set and zUrl is a symbolic name, then look up
** the URL in sync-url:%Q and sync-pw:%Q elements of the CONFIG table where
** %Q is the symbolic name.
**
** This routine differs from url_parse() in that this routine stores the
** results in pUrlData and does not change the values of global variables.
** The url_parse() routine puts its result in g.url.
*/
void url_parse_local(
  const char *zUrl,
  unsigned int urlFlags,
  UrlData *pUrlData
){
  int i, j, c;
  char *zFile = 0;

  memset(pUrlData, 0, sizeof(*pUrlData));
  if( urlFlags & URL_USE_CONFIG ){
    if( zUrl==0 || strcmp(zUrl,"default")==0 ){
      const char *zPwConfig = "last-sync-pw";
      if( urlFlags & URL_USE_PARENT ){
        zUrl = db_get("parent-project-url", 0);
        if( zUrl==0 ){
          zUrl = db_get("last-sync-url",0);
        }else{
          zPwConfig = "parent-project-pw";
        }
      }else{
        zUrl = db_get("last-sync-url", 0);
      }
      if( zUrl==0 ) return;
      if( pUrlData->passwd==0 ){
        pUrlData->passwd = unobscure(db_get(zPwConfig, 0));
        pUrlData->pwConfig = fossil_strdup(zPwConfig);
      }
      pUrlData->isAlias = 1;
    }else{
      char *zKey = sqlite3_mprintf("sync-url:%q", zUrl);
      char *zAlt = db_get(zKey, 0);
      if( zAlt ){
        pUrlData->pwConfig = mprintf("sync-pw:%q", zUrl);
        pUrlData->passwd = unobscure(
          db_text(0, "SELECT value FROM config WHERE name='sync-pw:%q'",zUrl)
        );
        zUrl = zAlt;
        urlFlags |= URL_REMEMBER_PW;
        pUrlData->isAlias = 1;
      }else{
        pUrlData->isAlias = 0;
      }
      sqlite3_free(zKey);
    }
  }else{
    if( zUrl==0 ) return;
  }

  if( strncmp(zUrl, "http://", 7)==0
   || strncmp(zUrl, "https://", 8)==0
   || strncmp(zUrl, "ssh://", 6)==0
  ){
    int iStart;
    char *zLogin;
    char *zExe;
    char cQuerySep = '?';

    if( zUrl[4]=='s' ){
      pUrlData->isHttps = 1;
      pUrlData->protocol = "https";
      pUrlData->dfltPort = 443;
      iStart = 8;
    }else if( zUrl[0]=='s' ){
      pUrlData->isSsh = 1;
      pUrlData->protocol = "ssh";
      pUrlData->dfltPort = 22;
      pUrlData->fossil = fossil_strdup("fossil");
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
    fossil_strtolwr(pUrlData->name);
    if( c==':' ){
      pUrlData->port = 0;
      i++;
      while( (c = zUrl[i])!=0 && fossil_isdigit(c) ){
        pUrlData->port = pUrlData->port*10 + c - '0';
        i++;
      }
      if( c!=0 && c!='/' ) fossil_fatal("url missing '/' after port number");
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
        fossil_free(pUrlData->fossil);
        pUrlData->fossil = fossil_strdup(zValue);
        dehttpize(pUrlData->fossil);
        fossil_free(zExe);
        zExe = mprintf("%cfossil=%T", cQuerySep, pUrlData->fossil);
        cQuerySep = '&';
        urlFlags |= URL_SSH_EXE;
      }
    }

    dehttpize(pUrlData->path);
    if( pUrlData->dfltPort==pUrlData->port ){
      pUrlData->canonical = mprintf(
        "%s://%s%T%T%z",
        pUrlData->protocol, zLogin, pUrlData->name, pUrlData->path, zExe
      );
    }else{
      pUrlData->canonical = mprintf(
        "%s://%s%T:%d%T%z",
        pUrlData->protocol, zLogin, pUrlData->name, pUrlData->port,
        pUrlData->path, zExe
      );
    }
    if( pUrlData->isSsh && pUrlData->path[1] ){
      char *zOld = pUrlData->path;
      pUrlData->path = mprintf("%s", zOld+1);
      fossil_free(zOld);
    }
    free(zLogin);
  }else if( strncmp(zUrl, "file:", 5)==0 ){
    pUrlData->isFile = 1;
    if( zUrl[5]=='/' && zUrl[6]=='/' ){
      i = 7;
    }else{
      i = 5;
    }
    zFile = mprintf("%s", &zUrl[i]);
  }else if( file_isfile(zUrl, ExtFILE) ){
    pUrlData->isFile = 1;
    zFile = mprintf("%s", zUrl);
  }else if( file_isdir(zUrl, ExtFILE)==1 ){
    zFile = mprintf("%s/FOSSIL", zUrl);
    if( file_isfile(zFile, ExtFILE) ){
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
    pUrlData->path = mprintf("");
    pUrlData->name = mprintf("%b", &cfile);
    pUrlData->canonical = mprintf("file://%T", pUrlData->name);
    blob_reset(&cfile);
  }else if( pUrlData->user!=0 && pUrlData->passwd==0
         && (urlFlags & URL_PROMPT_PW)!=0 ){
    url_prompt_for_password_local(pUrlData);
  }else if( pUrlData->user!=0 && ( urlFlags & URL_ASK_REMEMBER_PW ) ){
    if( fossil_isatty(fossil_fileno(stdin))
        && ( urlFlags & URL_REMEMBER_PW )==0 ){
      if( save_password_prompt(pUrlData->passwd) ){
        pUrlData->flags = urlFlags |= URL_REMEMBER_PW;
      }else{
        pUrlData->flags = urlFlags &= ~URL_REMEMBER_PW;
      }
    }
  }
}

/*
** Construct the complete URL for a UrlData object, including the
** login name and password, into memory obtained from fossil_malloc()
** and return a pointer to that URL text.
*/
char *url_full(const UrlData *p){
  Blob x = BLOB_INITIALIZER;
  if( p->isFile || p->user==0 || p->user[0]==0 ){
    return fossil_strdup(p->canonical);
  }
  blob_appendf(&x, "%s://", p->protocol);
  if( p->user && p->user[0] ){
    blob_appendf(&x, "%t", p->user);
    if( p->passwd && p->passwd[0] ){
      blob_appendf(&x, ":%t", p->passwd);
    }
    blob_appendf(&x, "@");
  }
  blob_appendf(&x, "%T", p->name);
  if( p->dfltPort!=p->port ){
    blob_appendf(&x, ":%d", p->port);
  }
  blob_appendf(&x, "%T", p->path);
  (void)blob_str(&x);
  return x.aData;
}

/*
** Construct a URL for a UrlData object that omits the
** login name and password, into memory obtained from fossil_malloc()
** and return a pointer to that URL text.
*/
char *url_nouser(const UrlData *p){
  Blob x = BLOB_INITIALIZER;
  if( p->isFile || p->user==0 || p->user[0]==0 ){
    return fossil_strdup(p->canonical);
  }
  blob_appendf(&x, "%s://", p->protocol);
  blob_appendf(&x, "%T", p->name);
  if( p->dfltPort!=p->port ){
    blob_appendf(&x, ":%d", p->port);
  }
  blob_appendf(&x, "%T", p->path);
  (void)blob_str(&x);
  return x.aData;
}

/*
** SQL function to remove the username/password from a URL
*/
void url_nouser_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zOrig = (const char*)sqlite3_value_text(argv[0]);
  UrlData x;
  if( zOrig==0 ) return;
  memset(&x, 0, sizeof(x));
  url_parse_local(zOrig, URL_OMIT_USER, &x);
  sqlite3_result_text(context, x.canonical, -1, SQLITE_TRANSIENT);
  url_unparse(&x);
}

/*
** Reclaim malloced memory from a UrlData object
*/
void url_unparse(UrlData *p){
  if( p==0 ){
    p = &g.url;
  }
  fossil_free(p->canonical);
  fossil_free(p->name);
  fossil_free(p->path);
  fossil_free(p->user);
  fossil_free(p->passwd);
  fossil_free(p->fossil);
  fossil_free(p->pwConfig);
  memset(p, 0, sizeof(*p));
}

/*
** Move a URL parse from one UrlData object to another.
*/
void url_move_parse(UrlData *pTo, UrlData *pFrom){
  url_unparse(pTo);
  memcpy(pTo, pFrom, sizeof(*pTo));
  memset(pFrom, 0, sizeof(*pFrom));
}

/*
** Parse the given URL, which describes a sync server.  Populate variables
** in the global "g.url" structure as shown below.  If zUrl is NULL, then
** parse the URL given in the last-sync-url setting, taking the password
** form last-sync-pw.
**
**      g.url.isFile      True if FILE:
**      g.url.isHttps     True if HTTPS:
**      g.url.isSsh       True if SSH:
**      g.url.protocol    "http" or "https" or "file" or "ssh"
**      g.url.name        Hostname for HTTP:, HTTPS:, SSH:.  Filename for FILE:
**      g.url.port        TCP port number for HTTP or HTTPS.
**      g.url.dfltPort    Default TCP port number (80 or 443).
**      g.url.path        Path name for HTTP or HTTPS.
**      g.url.user        Userid.
**      g.url.passwd      Password.
**      g.url.hostname    HOST:PORT or just HOST if port is the default.
**      g.url.canonical   The URL in canonical form, omitting the password
**      g.url.pwConfig    Name of CONFIG table entry containing the password
**
** HTTP url format as follows (HTTPS is the same with a different scheme):
**
**     http://userid:password@host:port/path
**
** SSH url format is:
**
**     ssh://userid@host:port/path?fossil=path/to/fossil.exe
**
** If URL_USE_CONFIG is set then the URL and password might be pulled from
** the CONFIG table rather than from the zUrl parameter.  If zUrl is NULL
** or "default" then the URL is given by the "last-sync-url" setting and
** the password comes form the "last-sync-pw" setting.  If zUrl is a symbolic
** name, then the URL comes from "sync-url:NAME" and the password from
** "sync-pw:NAME" where NAME is the input zUrl string.  Whenever the
** password is taken from the CONFIG table, the g.url.pwConfig field is
** set to the CONFIG.NAME value from which that password is taken.  Otherwise,
** g.url.pwConfig is NULL.
*/
void url_parse(const char *zUrl, unsigned int urlFlags){
  url_parse_local(zUrl, urlFlags, &g.url);
}

/*
** Print the content of g.url
*/
void urlparse_print(int showPw){
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
  if( showPw || g.url.pwConfig==0 ){
    fossil_print("g.url.passwd    = %s\n", g.url.passwd);
  }else{
    fossil_print("g.url.passwd    = ************\n");
  }
  fossil_print("g.url.pwConfig  = %s\n", g.url.pwConfig);
  fossil_print("g.url.canonical = %s\n", g.url.canonical);
  fossil_print("g.url.fossil    = %s\n", g.url.fossil);
  fossil_print("g.url.flags     = 0x%04x\n", g.url.flags);
  fossil_print("url_full(g.url) = %z\n", url_full(&g.url));
}

/*
** COMMAND: test-urlparser
**
** Usage: %fossil test-urlparser URL ?options?
**
**    --prompt-pw     Prompt for password if missing
**    --remember      Store results in last-sync-url
**    --show-pw       Show the CONFIG-derived password in the output
**    --use-config    Pull URL and password from the CONFIG table
**    --use-parent    Use the parent project URL
*/
void cmd_test_urlparser(void){
  int i;
  unsigned fg = 0;
  int showPw = 0;
  db_must_be_within_tree();
  url_proxy_options();
  if( find_option("remember",0,0) )    fg |= URL_REMEMBER;
  if( find_option("prompt-pw",0,0) )   fg |= URL_PROMPT_PW;
  if( find_option("use-parent",0,0) )  fg |= URL_USE_PARENT|URL_USE_CONFIG;
  if( find_option("use-config",0,0) )  fg |= URL_USE_CONFIG;
  if( find_option("show-pw",0,0) )     showPw = 1;
  if( (fg & URL_USE_CONFIG)==0 )       showPw = 1;
  if( g.argc!=3 && g.argc!=4 ){
    usage("URL");
  }
  url_parse(g.argv[2], fg);
  for(i=0; i<2; i++){
    urlparse_print(showPw);
    if( g.url.isFile || g.url.isSsh ) break;
    if( i==0 ){
      fossil_print("********\n");
      url_enable_proxy("Using proxy: ");
    }
    url_unparse(0);
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
** The original purpose of this routine is the above.  But this
** also happens to be a convenient place to look for other
** network-related options:
**
**    --nosync             Temporarily disable "autosync"
**
**    --ipv4               Disallow IPv6.  Use only IPv4.
**
**    --accept-any-cert    Disable server SSL cert validation. Accept
**                         any SSL cert that the server provides.
**                         WARNING: this option opens you up to
**                         forged-DNS and man-in-the-middle attacks!
*/
void url_proxy_options(void){
  zProxyOpt = find_option("proxy", 0, 1);
  if( find_option("nosync",0,0) ) g.fNoSync = 1;
  if( find_option("ipv4",0,0) ) g.fIPv4 = 1;
#ifdef FOSSIL_ENABLE_SSL
  if( find_option("accept-any-cert",0,0) ){
    ssl_disable_cert_verification();
  }
#endif /* FOSSIL_ENABLE_SSL */
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
    zProxy = db_get("proxy", "system");
    if( fossil_strcmp(zProxy, "system")==0 ){
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
    g.url.proxyUrlCanonical = zOriginalUrl;;
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
  blob_appendf(&p->url, "%R/%s", p->zBase);
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
    zSep = "&";
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
  if( fossil_isatty(fossil_fileno(stdin))
   && (pUrlData->flags & URL_PROMPT_PW)!=0
   && (pUrlData->flags & URL_PROMPTED)==0
  ){
    pUrlData->flags |= URL_PROMPTED;
    pUrlData->passwd = prompt_for_user_password(pUrlData->canonical);
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
    const char *url;
    if( g.url.useProxy ){
      url = g.url.proxyUrlCanonical;
    }else{
      url = g.url.canonical;
    }
    if( g.url.flags & URL_USE_PARENT ){
      db_set("parent-project-url", url, 0);
    }else{
      db_set("last-sync-url", url, 0);
    }
    if( g.url.user!=0 && g.url.passwd!=0 && ( g.url.flags & URL_REMEMBER_PW ) ){
      if( g.url.flags & URL_USE_PARENT ){
        db_set("parent-project-pw", obscure(g.url.passwd), 0);
      }else{
        db_set("last-sync-pw", obscure(g.url.passwd), 0);
      }
    }
  }
}

/* Preemptively prompt for a password if a username is given in the
** URL but no password.
*/
void url_get_password_if_needed(void){
  if( (g.url.user && g.url.user[0])
   && (g.url.passwd==0 || g.url.passwd[0]==0)
   && fossil_isatty(fossil_fileno(stdin))
  ){
    url_prompt_for_password();
  }
}

/*
** Given a URL for a remote repository clone point, try to come up with a
** reasonable basename of a local clone of that repository.
**
**    *  If the URL has a path, use the tail of the path, with any suffix
**       elided.
**
**    *  If the URL is just a domain name, without a path, then use the
**       first element of the domain name, except skip over "www." if
**       present and if there is a ".com" or ".org" or similar suffix.
**
** The string returned is obtained from fossil_malloc().  NULL might be
** returned if there is an error.
*/
char *url_to_repo_basename(const char *zUrl){
  const char *zTail = 0;
  int i;
  if( zUrl==0 ) return 0;
  for(i=0; zUrl[i]; i++){
    if( zUrl[i]=='?' ) break;
    if( (zUrl[i]=='/' || zUrl[i]=='@') && zUrl[i+1]!=0 ) zTail = &zUrl[i+1];
  }
  if( zTail==0 ) return 0;
  if( sqlite3_strnicmp(zTail, "www.", 4)==0 && strchr(zTail+4,'.')!=0 ){
    /* Remove the "www." prefix if there are more "." characters later.
    ** But don't remove the "www." prefix if what follows is the suffix.
    ** forum:/forumpost/74e111a2ee */
    zTail += 4;
  }
  if( zTail[0]==0 ) return 0;
  for(i=0; zTail[i] && zTail[i]!='.' && zTail[i]!='?' &&
           zTail[i]!=':' && zTail[i]!='/'; i++){}
  if( i==0 ) return 0;
  return mprintf("%.*s", i, zTail);
}

/*
** COMMAND: test-url-basename
** Usage: %fossil test-url-basenames URL ...
**
** This command is used for unit testing of the url_to_repo_basename()
** routine.  The command-line arguments are URL, presumably for remote
** Fossil repositories.  This command runs url_to_repo_basename() on each
** of those inputs and displays the result.
*/
void cmd_test_url_basename(void){
  int i;
  char *z;
  for(i=2; i<g.argc; i++){
    z = url_to_repo_basename(g.argv[i]);
    fossil_print("%s -> %s\n", g.argv[i], z);
    fossil_free(z);
  }
}
