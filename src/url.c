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

#if INTERFACE
/*
** Flags for url_parse()
*/
#define URL_PROMPT_PW        0x001  /* Prompt for password if needed */
#define URL_REMEMBER         0x002  /* Remember the url for later reuse */
#define URL_ASK_REMEMBER_PW  0x004  /* Ask whether to remember prompted pw */
#define URL_REMEMBER_PW      0x008  /* Should remember pw */
#define URL_PROMPTED         0x010  /* Prompted for PW already */

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
** Parse the given URL, which describes a sync server.  Populate variables 
** in the global "g" structure as follows:
**
**      g.urlIsFile      True if FILE:
**      g.urlIsHttps     True if HTTPS: 
**      g.urlIsSsh       True if SSH:
**      g.urlProtocol    "http" or "https" or "file"
**      g.urlName        Hostname for HTTP:, HTTPS:, SSH:.  Filename for FILE:
**      g.urlPort        TCP port number for HTTP or HTTPS.
**      g.urlDfltPort    Default TCP port number (80 or 443).
**      g.urlPath        Path name for HTTP or HTTPS.
**      g.urlUser        Userid.
**      g.urlPasswd      Password.
**      g.urlHostname    HOST:PORT or just HOST if port is the default.
**      g.urlCanonical   The URL in canonical form, omitting the password
**
** HTTP url format as follows (HTTPS is the same with a different scheme):
**
**     http://userid:password@host:port/path
**
** SSH url format is:
**
**     ssh://userid:password@host:port/path?fossil=path/to/fossil.exe
**
*/
void url_parse(const char *zUrl, unsigned int urlFlags){
  int i, j, c;
  char *zFile = 0;
  int bPrompted = 0;
  int bSetUrl = 1;
 
  if( zUrl==0 ){
    zUrl = db_get("last-sync-url", 0);
    if( zUrl==0 ) return;
    g.urlPasswd = unobscure(db_get("last-sync-pw", 0));
    bSetUrl = 0;
  }

  if( strncmp(zUrl, "http://", 7)==0
   || strncmp(zUrl, "https://", 8)==0
   || strncmp(zUrl, "ssh://", 6)==0
  ){
    int iStart;
    char *zLogin;
    char *zExe;
    char cQuerySep = '?';

    g.urlIsFile = 0;
    if( zUrl[4]=='s' ){
      g.urlIsHttps = 1;
      g.urlProtocol = "https";
      g.urlDfltPort = 443;
      iStart = 8;
    }else if( zUrl[0]=='s' ){
      g.urlIsSsh = 1;
      g.urlProtocol = "ssh";
      g.urlDfltPort = 22;
      g.urlFossil = "fossil";
      iStart = 6;
    }else{
      g.urlIsHttps = 0;
      g.urlProtocol = "http";
      g.urlDfltPort = 80;
      iStart = 7;
    }
    for(i=iStart; (c=zUrl[i])!=0 && c!='/' && c!='@'; i++){}
    if( c=='@' ){
      /* Parse up the user-id and password */
      for(j=iStart; j<i && zUrl[j]!=':'; j++){}
      g.urlUser = mprintf("%.*s", j-iStart, &zUrl[iStart]);
      dehttpize(g.urlUser);
      if( j<i ){
        urlFlags |= URL_ASK_REMEMBER_PW;
        g.urlPasswd = mprintf("%.*s", i-j-1, &zUrl[j+1]);
        dehttpize(g.urlPasswd);
      }
      if( g.urlIsSsh && g.urlPasswd ){
        zLogin = mprintf("%t:*@", g.urlUser);
      }else{
        zLogin = mprintf("%t@", g.urlUser);
      }
      for(j=i+1; (c=zUrl[j])!=0 && c!='/' && c!=':'; j++){}
      g.urlName = mprintf("%.*s", j-i-1, &zUrl[i+1]);
      i = j;
    }else{
      for(i=iStart; (c=zUrl[i])!=0 && c!='/' && c!=':'; i++){}
      g.urlName = mprintf("%.*s", i-iStart, &zUrl[iStart]);
      zLogin = mprintf("");
    }
    url_tolower(g.urlName);
    if( c==':' ){
      g.urlPort = 0;
      i++;
      while( (c = zUrl[i])!=0 && fossil_isdigit(c) ){
        g.urlPort = g.urlPort*10 + c - '0';
        i++;
      }
      g.urlHostname = mprintf("%s:%d", g.urlName, g.urlPort);
    }else{
      g.urlPort = g.urlDfltPort;
      g.urlHostname = g.urlName;
    }
    dehttpize(g.urlName);
    g.urlPath = mprintf("%s", &zUrl[i]);
    for(i=0; g.urlPath[i] && g.urlPath[i]!='?'; i++){}
    if( g.urlPath[i] ){
      g.urlPath[i] = 0;
      i++;
    }
    zExe = mprintf("");
    while( g.urlPath[i]!=0 ){
      char *zName, *zValue;
      zName = &g.urlPath[i];
      zValue = zName;
      while( g.urlPath[i] && g.urlPath[i]!='=' ){ i++; }
      if( g.urlPath[i]=='=' ){
        g.urlPath[i] = 0;
        i++;
        zValue = &g.urlPath[i];
        while( g.urlPath[i] && g.urlPath[i]!='&' ){ i++; }
      }
      if( g.urlPath[i] ){
        g.urlPath[i] = 0;
        i++;
      }
      if( fossil_strcmp(zName,"fossil")==0 ){
        g.urlFossil = zValue;
        dehttpize(g.urlFossil);
        zExe = mprintf("%cfossil=%T", cQuerySep, g.urlFossil);
        cQuerySep = '&';
      }
    }

    dehttpize(g.urlPath);
    if( g.urlDfltPort==g.urlPort ){
      g.urlCanonical = mprintf(
        "%s://%s%T%T%s", 
        g.urlProtocol, zLogin, g.urlName, g.urlPath, zExe
      );
    }else{
      g.urlCanonical = mprintf(
        "%s://%s%T:%d%T%s",
        g.urlProtocol, zLogin, g.urlName, g.urlPort, g.urlPath, zExe
      );
    }
    if( g.urlIsSsh && g.urlPath[1] ) g.urlPath++;
    free(zLogin);
  }else if( strncmp(zUrl, "file:", 5)==0 ){
    g.urlIsFile = 1;
    if( zUrl[5]=='/' && zUrl[6]=='/' ){
      i = 7;
    }else{
      i = 5;
    }
    zFile = mprintf("%s", &zUrl[i]);
  }else if( file_isfile(zUrl) ){
    g.urlIsFile = 1;
    zFile = mprintf("%s", zUrl);
  }else if( file_isdir(zUrl)==1 ){
    zFile = mprintf("%s/FOSSIL", zUrl);
    if( file_isfile(zFile) ){
      g.urlIsFile = 1;
    }else{
      free(zFile);
      fossil_fatal("unknown repository: %s", zUrl);
    }
  }else{
    fossil_fatal("unknown repository: %s", zUrl);
  }
  g.urlFlags = urlFlags;
  if( g.urlIsFile ){
    Blob cfile;
    dehttpize(zFile);  
    file_canonical_name(zFile, &cfile, 0);
    free(zFile);
    g.urlProtocol = "file";
    g.urlPath = "";
    g.urlName = mprintf("%b", &cfile);
    g.urlCanonical = mprintf("file://%T", g.urlName);
    blob_reset(&cfile);
  }else if( g.urlUser!=0 && g.urlPasswd==0 && (urlFlags & URL_PROMPT_PW) ){
    url_prompt_for_password();
    bPrompted = 1;
  }else if( g.urlUser!=0 && (urlFlags & URL_ASK_REMEMBER_PW ) &&
            save_password_prompt() ){
    g.urlFlags = urlFlags |= URL_REMEMBER_PW;
  }
  if( urlFlags & URL_REMEMBER ){
    if( bSetUrl ){
      db_set("last-sync-url", g.urlCanonical, 0);
    }
    if( !bPrompted && g.urlPasswd && g.urlUser ){
      db_set("last-sync-pw", obscure(g.urlPasswd), 0);
    }
  }
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
    fossil_print("g.urlIsFile    = %d\n", g.urlIsFile);
    fossil_print("g.urlIsHttps   = %d\n", g.urlIsHttps);
    fossil_print("g.urlIsSsh     = %d\n", g.urlIsSsh);
    fossil_print("g.urlProtocol  = %s\n", g.urlProtocol);
    fossil_print("g.urlName      = %s\n", g.urlName);
    fossil_print("g.urlPort      = %d\n", g.urlPort);
    fossil_print("g.urlDfltPort  = %d\n", g.urlDfltPort);
    fossil_print("g.urlHostname  = %s\n", g.urlHostname);
    fossil_print("g.urlPath      = %s\n", g.urlPath);
    fossil_print("g.urlUser      = %s\n", g.urlUser);
    fossil_print("g.urlPasswd    = %s\n", g.urlPasswd);
    fossil_print("g.urlCanonical = %s\n", g.urlCanonical);
    fossil_print("g.urlFossil    = %s\n", g.urlFossil);
    fossil_print("g.urlFlags     = 0x%02x\n", g.urlFlags);
    if( g.urlIsFile || g.urlIsSsh ) break;
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
    if( zProxy==0 || zProxy[0]==0 || is_truth(zProxy) ){
      zProxy = fossil_getenv("http_proxy");
    }
  }
  if( zProxy && zProxy[0] && !is_false(zProxy)
      && !g.urlIsSsh && !g.urlIsFile ){
    char *zOriginalUrl = g.urlCanonical;
    char *zOriginalHost = g.urlHostname;
    char *zOriginalUser = g.urlUser;
    char *zOriginalPasswd = g.urlPasswd;
    unsigned uOriginalFlags = g.urlFlags;
    g.urlUser = 0;
    g.urlPasswd = "";
    url_parse(zProxy, 0);
    if( zMsg ) fossil_print("%s%s\n", zMsg, g.urlCanonical);
    g.urlPath = zOriginalUrl;
    g.urlHostname = zOriginalHost;
    if( g.urlUser ){
      char *zCredentials1 = mprintf("%s:%s", g.urlUser, g.urlPasswd);
      char *zCredentials2 = encode64(zCredentials1, -1);
      g.urlProxyAuth = mprintf("Basic %z", zCredentials2);
      free(zCredentials1);
    }
    g.urlUser = zOriginalUser;
    g.urlPasswd = zOriginalPasswd;
    g.urlFlags = uOriginalFlags;
  }
}

#if INTERFACE
/*
** An instance of this object is used to build a URL with query parameters.
*/
struct HQuery {
  Blob url;                  /* The URL */
  const char *zBase;         /* The base URL */
  int nParam;                /* Number of parameters.  Max 10 */
  const char *azName[15];    /* Parameter names */
  const char *azValue[15];   /* Parameter values */
};
#endif

/*
** Initialize the URL object.
*/
void url_initialize(HQuery *p, const char *zBase){
  blob_zero(&p->url);
  p->zBase = zBase;
  p->nParam = 0;
}

/*
** Resets the given URL object, deallocating any memory
** it uses.
*/
void url_reset(HQuery *p){
  blob_reset(&p->url);
  url_initialize(p, p->zBase);
}

/*
** Add a fixed parameter to an HQuery.
*/
void url_add_parameter(HQuery *p, const char *zName, const char *zValue){
  assert( p->nParam < count(p->azName) );
  assert( p->nParam < count(p->azValue) );
  p->azName[p->nParam] = zName;
  p->azValue[p->nParam] = zValue;
  p->nParam++;
}

/*
** Render the URL with a parameter override.
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
** Prompt the user for the password for g.urlUser.  Store the result
** in g.urlPasswd.
*/
void url_prompt_for_password(void){
  if( g.urlIsSsh || g.urlIsFile ) return;
  if( isatty(fileno(stdin))
   && (g.urlFlags & URL_PROMPT_PW)!=0
   && (g.urlFlags & URL_PROMPTED)==0
  ){
    g.urlFlags |= URL_PROMPTED;
    g.urlPasswd = prompt_for_user_password(g.urlUser);
    if( g.urlPasswd[0]
     && (g.urlFlags & (URL_REMEMBER|URL_ASK_REMEMBER_PW))!=0
    ){
      if( save_password_prompt() ){
        g.urlFlags |= URL_REMEMBER_PW;
        if( g.urlFlags & URL_REMEMBER ){
          db_set("last-sync-pw", obscure(g.urlPasswd), 0);
        }
      }
    }
  }else{
    fossil_fatal("missing or incorrect password for user \"%s\"",
                 g.urlUser);
  }
}

/*
** Remember the URL if requested.
*/
void url_remember(void){
  db_set("last-sync-url", g.urlCanonical, 0);
  if( g.urlFlags & URL_REMEMBER_PW ){
    db_set("last-sync-pw", obscure(g.urlPasswd), 0);
  }
  g.urlFlags |= URL_REMEMBER;
}

/* Preemptively prompt for a password if a username is given in the
** URL but no password.
*/
void url_get_password_if_needed(void){
  if( (g.urlUser && g.urlUser[0])
   && (g.urlPasswd==0 || g.urlPasswd[0]==0)
   && isatty(fileno(stdin)) 
  ){
    url_prompt_for_password();
  }
}
