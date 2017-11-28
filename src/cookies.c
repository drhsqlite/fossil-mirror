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
** cookie_parse(zNameOfCookie);
**
**    Identify a cookie that will be used to remember display choices
**    made by the user, so that those same choices are selected automatically
**    the next time the page is presented.  This routine must be invoked
**    first, to initialize this module.
**
** cookie_read_parameter(zQP, zPName);
**
**    If query parameter zQP does not exist but zPName does exist in
**    the parsed cookie, then initialize zQP to hold the same value
**    as the zPName element in the parsed cookie.
**
** cookie_write_parameter(zQP, zPName);
**
**    If query parameter zQP exists and if it has a different value from
**    the zPName parameter in the parsed cookie, then replace the value of
**    zPName with the value of zQP.  If zQP exists but zPName does not
**    exist, then zPName is created.  If zQP does not exist or if it has
**    the same value as zPName, then this routine is a no-op.
**
** cookie_link_parameter(zQP, zPName);
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
*/
#include "cookies.h"
#include <assert.h>
#include <string.h>


/*
** State information private to this module
*/
#define COOKIE_NPARAM  10
static struct {
  const char *zCookieName;    /* name of the user preferences cookie */
  char *zCookieValue;         /* Value of the user preferences cookie */
  int bChanged;               /* True if any value has changed */
  int nParam;                 /* Number of parameters in the cookie */
  struct {
    const char *zPName;         /* Name of a parameter */
    char *zPValue;              /* Value of that parameter */
  } aParam[COOKIE_NPARAM];
} cookies;

/* Initialize this module by parsing the content of the cookie named
** by zCookieName
*/
void cookie_parse(const char *zCookieName){
  char *z;
  assert( cookies.zCookieName==0 );
  cookies.zCookieName = zCookieName;
  z = (char*)P(zCookieName);
  if( z==0 ) z = "";
  cookies.zCookieValue = z = mprintf("%s", z);
  while( cookies.nParam<COOKIE_NPARAM ){
    while( fossil_isspace(z[0]) ) z++;
    if( z[0]==0 ) break;
    cookies.aParam[cookies.nParam].zPName = z;
    while( *z && *z!='=' && *z!='&' ){ z++; }
    if( *z=='=' ){
      *z = 0;
      z++;
      cookies.aParam[cookies.nParam].zPValue = z;
      while( *z && *z!='&' ){ z++; }
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
static void cookie_readwrite(const char *zQP, const char *zPName, int flags){
  const char *zQVal = P(zQP);
  int i;
  assert( cookies.zCookieName!=0 );
  for(i=0; i<cookies.nParam && strcmp(zPName,cookies.aParam[i].zPName); i++){}
  if( (flags & COOKIE_READ)!=0 && zQVal==0 && i<cookies.nParam ){
    cgi_set_parameter_nocopy(zQP, cookies.aParam[i].zPValue, 1);
    return;
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
  cookie_readwrite(zQP, zPName, COOKIE_READ);
}

/* Update the zPName value of the user preference cookie to match
** the value of query parameter zQP.
*/
void cookie_write_parameter(const char *zQP, const char *zPName){
  cookie_readwrite(zQP, zPName, COOKIE_WRITE);
}

/* Use the zPName user preference value as a default for zQP and record
** any changes to the zQP value back into the cookie.
*/
void cookie_link_parameter(const char *zQP, const char *zPName){
  cookie_readwrite(zQP, zPName, COOKIE_READ|COOKIE_WRITE);
}

/* Update the user preferences cookie, if necessary, and shut down this
** module
*/
void cookie_render(void){
  assert( cookies.zCookieName!=0 );
  if( cookies.bChanged ){
    Blob new;
    int i;
    blob_init(&new, 0, 0);
    for(i=0;i<cookies.nParam;i++){
      if( i>0 ) blob_append(&new, "&", 1);
      blob_appendf(&new, "%s=%t",
          cookies.aParam[i].zPName, cookies.aParam[i].zPValue);
    }
    cgi_set_cookie(cookies.zCookieName, blob_str(&new), 0, 31536000);
  }
  cookies.zCookieName = 0;
}

/*
** WEBPAGE:  cookies
**
** Show the current display settings contained in the
** "fossil_display_settings" cookie.
*/
void cookie_page(void){
  int i;
  cookie_parse("fossil_display_settings");
  style_header("User Preference Cookie Values");
  @ <p>The following are user preference settings held in the
  @ "fossil_display_settings" cookie.
  @ <ul>
  @ <li>Raw cookie value: "%h(PD("fossil_display_settings",""))"
  for(i=0; i<cookies.nParam; i++){
    @ <li>%h(cookies.aParam[i].zPName): "%h(cookies.aParam[i].zPValue)"
  }
  @ </ul>
  style_footer();
}
