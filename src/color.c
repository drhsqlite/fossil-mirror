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
** This file contains code used to select colors based on branch and
** user names.
**
*/
#include "config.h"
#include <string.h>
#include "color.h"

/*
** Compute a hash on a branch or user name
*/
static unsigned int hash_of_name(const char *z){
  unsigned int h = 0;
  int i;
  for(i=0; z[i]; i++ ){
    h = (h<<11) ^ (h<<1) ^ (h>>3) ^ z[i];
  }
  return h;
}

/*
** Hash a string and use the hash to determine a background color.
**
** This value returned is in static space and is overwritten with
** each subsequent call.
*/
char *hash_color(const char *z){
  unsigned int h = 0;          /* Hash on the branch name */
  int r, g, b;                 /* Values for red, green, and blue */
  int h1, h2, h3, h4;          /* Elements of the hash value */
  int mx, mn;                  /* Components of HSV */
  static char zColor[10];      /* The resulting color */
  static int ix[3] = {0,0};    /* Color chooser parameters */

  if( ix[0]==0 ){
    if( skin_detail_boolean("white-foreground") ){
      ix[0] = 0x50;
      ix[1] = 0x20;
    }else{
      ix[0] = 0xf8;
      ix[1] = 0x20;
    }
  }
  h = hash_of_name(z);
  h1 = h % 6;  h /= 6;
  h3 = h % 10; h /= 10;
  h4 = h % 10; h /= 10;
  mx = ix[0] - h3;
  mn = mx - h4 - ix[1];
  h2 = (h%(mx - mn)) + mn;
  switch( h1 ){
    case 0:  r = mx; g = h2, b = mn;  break;
    case 1:  r = h2; g = mx, b = mn;  break;
    case 2:  r = mn; g = mx, b = h2;  break;
    case 3:  r = mn; g = h2, b = mx;  break;
    case 4:  r = h2; g = mn, b = mx;  break;
    default: r = mx; g = mn, b = h2;  break;
  }
  sqlite3_snprintf(8, zColor, "#%02x%02x%02x", r,g,b);
  return zColor;
}

/*
** Determine a color for users based on their login string.
**
** SETTING: user-color-map          width=40 block-text
**
** The user-color-map setting can be used to override user color choices.
** The setting is a list of space-separated words pairs.  The first word
** of each pair is a login name.  The second word is an alternative name
** used by the color chooser algorithm.
**
** This list is intended to be relatively short.  The idea is to only use
** this map to resolve color collisions between common users.
**
** Visit /hash-color-test?rand for a list of suggested names for the
** second word of each pair in the list.
*/
char *user_color(const char *zLogin){
  static int once = 0;
  static int nMap = 0;
  static char **azMap = 0;
  static int *anMap = 0;
  int i;
  if( !once ){
    char *zMap = (char*)db_get("user-color-map",0);
    once = 1;
    if( zMap && zMap[0] ){
      if( !g.interp ) Th_FossilInit(0);
      Th_SplitList(g.interp, zMap, (int)strlen(zMap),
                   &azMap, &anMap, &nMap);
      for(i=0; i<nMap; i++) azMap[i][anMap[i]] = 0;
    }
  }
  for(i=0; i<nMap-1; i+=2){
    if( strcmp(zLogin, azMap[i])==0 ) return hash_color(azMap[i+1]);
  }
  return hash_color(zLogin);
}

/*
** COMMAND: test-hash-color
**
** Usage: %fossil test-hash-color TAG ...
**
** Print out the color names associated with each tag.  Used for
** testing the hash_color() function.
*/
void test_hash_color(void){
  int i;
  for(i=2; i<g.argc; i++){
    fossil_print("%20s: %s\n", g.argv[i], hash_color(g.argv[i]));
  }
}

/*
** WEBPAGE: hash-color-test
**
** Print out the color names associated with each tag.  Used for
** testing the hash_color() function.
*/
void test_hash_color_page(void){
  const char *zBr;
  char zNm[10];
  int i, cnt;
  login_check_credentials();
  if( P("rand")!=0 ){
    int j;
    for(i=0; i<10; i++){
      sqlite3_uint64 u;
      char zClr[10];
      sqlite3_randomness(sizeof(u), &u);
      cnt = 3+(u%2);
      u /= 2;
      for(j=0; j<cnt; j++){
         zClr[j] = 'a' + (u%26);
         u /= 26;
       }
      zClr[j] = 0;
      sqlite3_snprintf(sizeof(zNm),zNm,"b%d",i);
      cgi_replace_parameter(fossil_strdup(zNm), fossil_strdup(zClr));
    }
  }
  style_set_current_feature("test");
  style_header("Hash Color Test");
  for(i=cnt=0; i<10; i++){
    sqlite3_snprintf(sizeof(zNm),zNm,"b%d",i);
    zBr = P(zNm);
    if( zBr && zBr[0] ){
      @ <p style='border:1px solid;background-color:%s(hash_color(zBr));'>
      @ %h(zBr) - hash 0x%x(hash_of_name(zBr)) - color %s(hash_color(zBr)) -
      @ Omnes nos quasi oves erravimus unusquisque in viam
      @ suam declinavit.</p>
      cnt++;
    }
  }
  if( cnt ){
    @ <hr>
  }
  @ <form method="POST">
  @ <p>Enter candidate branch names below and see them displayed in their
  @ default background colors above.</p>
  for(i=0; i<10; i++){
    sqlite3_snprintf(sizeof(zNm),zNm,"b%d",i);
    zBr = P(zNm);
    @ <input type="text" size="30" name='%s(zNm)' value='%h(PD(zNm,""))'><br>
  }
  @ <input type="submit" value="Submit">
  @ <input type="submit" name="rand" value="Random">
  @ </form>
  style_finish_page();
}
