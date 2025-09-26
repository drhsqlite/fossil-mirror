/*
** Copyright (c) 2017 D. Richard Hipp
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
** This file contains code used to manage a cookie that stores user-specific
** display preferences for the web interface.
**
** cookie_parse(void);
**
**    Read and parse the display preferences cookie.
**
** cookie_read_parameter(zQP, zPName);
**
**    If query parameter zQP does not exist but zPName does exist in
**    the parsed cookie, then initialize zQP to hold the same value
**    as the zPName element in the parsed cookie.
**
** cookie_write_parameter(zQP, zPName, zDefault);
**
**    If query parameter zQP exists and if it has a different value from
**    the zPName parameter in the parsed cookie, then replace the value of
**    zPName with the value of zQP.  If zQP exists but zPName does not
**    exist, then zPName is created.  If zQP does not exist or if it has
**    the same value as zPName, then this routine is a no-op.
**
** cookie_link_parameter(zQP, zPName, zDefault);
**
**    This does both cookie_read_parameter() and cookie_write_parameter()
**    all at once.
**
** cookie_render();
**
**    If any prior calls to cookie_write_parameter() have changed the
**    value of the user preferences cookie, this routine will cause the
**    new cookie value to be included in the HTTP header for the current
**    web page.  This routine is a destructor for this module and should
**    be called once.
**
** char *cookie_value(zPName, zDefault);
**
**    Look up the value of a cookie parameter zPName.  Return zDefault if
**    there is no display preferences cookie or if zPName does not exist.
*/
#include "config.h"
#include "cookies.h"
#include <assert.h>
#include <string.h>

#if INTERFACE
/* the standard name of the display settings cookie for fossil */
# define DISPLAY_SETTINGS_COOKIE    "fossil_display_settings"
#endif


/*
** State information private to this module
*/
#define COOKIE_NPARAM  10
static struct {
  char *zCookieValue;         /* Value of the user preferences cookie */
  int bChanged;               /* True if any value has changed */
  int bIsInit;                /* True after initialization */
  int nParam;                 /* Number of parameters in the cookie */
  struct {
    const char *zPName;         /* Name of a parameter */
    char *zPValue;              /* Value of that parameter */
  } aParam[COOKIE_NPARAM];
} cookies;

/* Initialize this module by parsing the content of the cookie named
** by DISPLAY_SETTINGS_COOKIE
*/
void cookie_parse(void){
  char *z;
  if( cookies.bIsInit ) return;
  z = (char*)P(DISPLAY_SETTINGS_COOKIE);
  if( z==0 ) z = "";
  cookies.zCookieValue = z = fossil_strdup(z);
  cookies.bIsInit = 1;
  while( cookies.nParam<COOKIE_NPARAM ){
    while( fossil_isspace(z[0]) ) z++;
    if( z[0]==0 ) break;
    cookies.aParam[cookies.nParam].zPName = z;
    while( *z && *z!='=' && *z!=',' ){ z++; }
    if( *z=='=' ){
      *z = 0;
      z++;
      cookies.aParam[cookies.nParam].zPValue = z;
      while( *z && *z!=',' ){ z++; }
      if( *z ){
        *z = 0;
        z++;
      }
      dehttpize(cookies.aParam[cookies.nParam].zPValue);
    }else{
      if( *z ){ *z++ = 0; }
      cookies.aParam[cookies.nParam].zPValue = "";
    }
    cookies.nParam++;
  }
}

#define COOKIE_READ  1
#define COOKIE_WRITE 2
static void cookie_readwrite(
  const char *zQP,        /* Name of the query parameter */
  const char *zPName,     /* Name of the cooking setting */
  const char *zDflt,      /* Default value for the query parameter */
  int flags               /* READ or WRITE or both */
){
  const char *zQVal = P(zQP);
  int i;
  cookie_parse();
  for(i=0; i<cookies.nParam && strcmp(zPName,cookies.aParam[i].zPName); i++){}
  if( zQVal==0 && (flags & COOKIE_READ)!=0 && i<cookies.nParam ){
    cgi_set_parameter_nocopy(zQP, cookies.aParam[i].zPValue, 1);
    return;
  }
  if( zQVal==0 ){
    zQVal = zDflt;
    if( flags & COOKIE_WRITE ) cgi_set_parameter_nocopy(zQP, zQVal, 1);
  }
  if( (flags & COOKIE_WRITE)!=0
   && i<COOKIE_NPARAM
   && (i==cookies.nParam || strcmp(zQVal, cookies.aParam[i].zPValue))
  ){
    if( i==cookies.nParam ){
      cookies.aParam[i].zPName = zPName;
      cookies.nParam++;
    }
    cookies.aParam[i].zPValue = (char*)zQVal;
    cookies.bChanged = 1;
  }
}

/* If query parameter zQP is missing, initialize it using the zPName
** value from the user preferences cookie
*/
void cookie_read_parameter(const char *zQP, const char *zPName){
  cookie_readwrite(zQP, zPName, 0, COOKIE_READ);
}

/* Update the zPName value of the user preference cookie to match
** the value of query parameter zQP.
*/
void cookie_write_parameter(
  const char *zQP,
  const char *zPName,
  const char *zDflt
){
  cookie_readwrite(zQP, zPName, zDflt, COOKIE_WRITE);
}

/* Use the zPName user preference value as a default for zQP and record
** any changes to the zQP value back into the cookie.
*/
void cookie_link_parameter(
  const char *zQP,       /* The query parameter */
  const char *zPName,    /* The name of the cookie value */
  const char *zDflt      /* Default value for the parameter */
){
  cookie_readwrite(zQP, zPName, zDflt, COOKIE_READ|COOKIE_WRITE);
}

/* Update the user preferences cookie, if necessary, and shut down
** this module. The cookie is only emitted if its value has actually
** changed since the request started and the "udc" (Update Display
** Cookie) URL argument was provided.
**
** Historical note: from 2021-03-02 [71a2d68a7a113e7c] until
** 2023-01-16, the udc was not observed (it had been prior to that),
** and that led to the unfortunate side effect that a timeline link
** from the /reports page would end up persistently setting a user's
** timeline length preference to the number of items in that
** report. In a /chat discussion it was agreed that updating the
** cookie requires explicit opt-in via the udc argument or ?skin=...,
** which implies udc.
*/
void cookie_render(void){
  if( cookies.bChanged && P("udc")!=0 ){
    Blob new;
    int i;
    blob_init(&new, 0, 0);
    for(i=0;i<cookies.nParam;i++){
      if( i>0 ) blob_append(&new, ",", 1);
      blob_appendf(&new, "%s=%T",
          cookies.aParam[i].zPName, cookies.aParam[i].zPValue);
    }
    cgi_set_cookie(DISPLAY_SETTINGS_COOKIE, blob_str(&new), 0, 31536000);
  }
  cookies.bIsInit = 0;
}

/* Return the value of a preference cookie.
*/
const char *cookie_value(const char *zPName, const char *zDefault){
  int i;
  assert( zPName!=0 );
  cookie_parse();
  for(i=0; i<cookies.nParam && strcmp(zPName,cookies.aParam[i].zPName); i++){}
  return i<cookies.nParam ? cookies.aParam[i].zPValue : zDefault;
}

/* Return the number of characters of hex in the prefix to the
** given string.
*/
static int hex_prefix_length(const char *z){
  int i;
  for(i=0; fossil_isXdigit(z[i]); i++){}
  return i;
}

/*
** WEBPAGE: cookies
**
** Show all cookies associated with Fossil.  This shows the text of the
** login cookie and is hence dangerous if an adversary is looking over
** your shoulder and is able to read and reproduce that cookie.
**
** WEBPAGE: fdscookie
**
** Show the current display settings contained in the
** "fossil_display_settings" cookie.
*/
void cookie_page(void){
  int i;
  int nCookie = 0;
  const char *zName = 0;
  const char *zValue = 0;
  const char *zLoginCookie = login_cookie_name();
  int isQP = 0;
  int bFDSonly = strstr(g.zPath, "fdscookie")!=0;
  cookie_parse();
  if( bFDSonly ){
    style_header("Display Preferences Cookie");
  }else{
    style_header("All Cookies");
  }
  @ <form method="POST">
  @ <ol>
  for(i=0; cgi_param_info(i, &zName, &zValue, &isQP); i++){
    char *zDel;
    if( isQP ) continue;
    if( fossil_isupper(zName[0]) ) continue;
    if( bFDSonly && strcmp(zName, "fossil_display_settings")!=0 ) continue;
    zDel = mprintf("del%s",zName);
    if( P(zDel)!=0 ){
      const char *zPath = fossil_strcmp(ROBOT_COOKIE,zName)==0
        ? "/" : 0;
      cgi_set_cookie(zName, "", zPath, -1);
      cgi_redirect(g.zPath);
    }
    nCookie++;
    @ <li><p><b>%h(zName)</b>: %h(zValue)
    @ <input type="submit" name="%h(zDel)" value="Delete">
    if( fossil_strcmp(zName, DISPLAY_SETTINGS_COOKIE)==0  && cookies.nParam>0 ){
      int j;
      @ <p>This cookie remembers your Fossil display preferences.
      @ <ul>
      for(j=0; j<cookies.nParam; j++){
        @ <li>%h(cookies.aParam[j].zPName): "%h(cookies.aParam[j].zPValue)"
      }
      @ </ul>
    }else
    if( fossil_strcmp(zName, zLoginCookie)==0 ){
      @ <p>This is your login cookie.  If you delete this cookie, you will
      @ be logged out.
    }else
    if( fossil_strncmp(zName, "fossil-", 7)==0
     && strlen(zName)==23
     && hex_prefix_length(&zName[7])==16
     && hex_prefix_length(zValue)>24
    ){
      @ <p>This appears to be a login cookie for another Fossil repository
      @ in the same website.
    }else
    if( fossil_strcmp(zName, ROBOT_COOKIE)==0 ){
      @ <p>This cookie shows that your web-browser has been tested is
      @ believed to be operated by a human, not a robot.
    }
    else {
      @ <p>This cookie was not generated by Fossil.  It might be something
      @ from another program on the same website.
    }
    fossil_free(zDel);
  }
  @ </ol>
  @ </form>
  if( nCookie==0 ){
    if( bFDSonly ){
      @ <p><i>Your browser is not holding a "fossil_display_setting" cookie
      @ for this website</i></p>
    }else{
      @ <p><i>Your browser is not holding any cookies for this website</i></p>
    }
  }
  style_finish_page();
}
