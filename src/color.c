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
** 140 standard CSS color names and their corresponding RGB values,
** in alphabetical order by name so that we can do a binary search
** for lookup.
*/
static const struct CssColors {
  const char *zName;     /* CSS Color name, lower case */
  unsigned int iRGB;     /* Corresponding RGB value */
} aCssColors[] = {
  { "aliceblue",            0xf0f8ff },
  { "antiquewhite",         0xfaebd7 },
  { "aqua",                 0x00ffff },
  { "aquamarine",           0x7fffd4 },
  { "azure",                0xf0ffff },
  { "beige",                0xf5f5dc },
  { "bisque",               0xffe4c4 },
  { "black",                0x000000 },
  { "blanchedalmond",       0xffebcd },
  { "blue",                 0x0000ff },
  { "blueviolet",           0x8a2be2 },
  { "brown",                0xa52a2a },
  { "burlywood",            0xdeb887 },
  { "cadetblue",            0x5f9ea0 },
  { "chartreuse",           0x7fff00 },
  { "chocolate",            0xd2691e },
  { "coral",                0xff7f50 },
  { "cornflowerblue",       0x6495ed },
  { "cornsilk",             0xfff8dc },
  { "crimson",              0xdc143c },
  { "cyan",                 0x00ffff },
  { "darkblue",             0x00008b },
  { "darkcyan",             0x008b8b },
  { "darkgoldenrod",        0xb8860b },
  { "darkgray",             0xa9a9a9 },
  { "darkgreen",            0x006400 },
  { "darkkhaki",            0xbdb76b },
  { "darkmagenta",          0x8b008b },
  { "darkolivegreen",       0x556b2f },
  { "darkorange",           0xff8c00 },
  { "darkorchid",           0x9932cc },
  { "darkred",              0x8b0000 },
  { "darksalmon",           0xe9967a },
  { "darkseagreen",         0x8fbc8f },
  { "darkslateblue",        0x483d8b },
  { "darkslategray",        0x2f4f4f },
  { "darkturquoise",        0x00ced1 },
  { "darkviolet",           0x9400d3 },
  { "deeppink",             0xff1493 },
  { "deepskyblue",          0x00bfff },
  { "dimgray",              0x696969 },
  { "dodgerblue",           0x1e90ff },
  { "firebrick",            0xb22222 },
  { "floralwhite",          0xfffaf0 },
  { "forestgreen",          0x228b22 },
  { "fuchsia",              0xff00ff },
  { "gainsboro",            0xdcdcdc },
  { "ghostwhite",           0xf8f8ff },
  { "gold",                 0xffd700 },
  { "goldenrod",            0xdaa520 },
  { "gray",                 0x808080 },
  { "green",                0x008000 },
  { "greenyellow",          0xadff2f },
  { "honeydew",             0xf0fff0 },
  { "hotpink",              0xff69b4 },
  { "indianred",            0xcd5c5c },
  { "indigo",               0x4b0082 },
  { "ivory",                0xfffff0 },
  { "khaki",                0xf0e68c },
  { "lavender",             0xe6e6fa },
  { "lavenderblush",        0xfff0f5 },
  { "lawngreen",            0x7cfc00 },
  { "lemonchiffon",         0xfffacd },
  { "lightblue",            0xadd8e6 },
  { "lightcoral",           0xf08080 },
  { "lightcyan",            0xe0ffff },
  { "lightgoldenrodyellow", 0xfafad2 },
  { "lightgrey",            0xd3d3d3 },
  { "lightgreen",           0x90ee90 },
  { "lightpink",            0xffb6c1 },
  { "lightsalmon",          0xffa07a },
  { "lightseagreen",        0x20b2aa },
  { "lightskyblue",         0x87cefa },
  { "lightslategray",       0x778899 },
  { "lightsteelblue",       0xb0c4de },
  { "lightyellow",          0xffffe0 },
  { "lime",                 0x00ff00 },
  { "limegreen",            0x32cd32 },
  { "linen",                0xfaf0e6 },
  { "magenta",              0xff00ff },
  { "maroon",               0x800000 },
  { "mediumaquamarine",     0x66cdaa },
  { "mediumblue",           0x0000cd },
  { "mediumorchid",         0xba55d3 },
  { "mediumpurple",         0x9370d8 },
  { "mediumseagreen",       0x3cb371 },
  { "mediumslateblue",      0x7b68ee },
  { "mediumspringgreen",    0x00fa9a },
  { "mediumturquoise",      0x48d1cc },
  { "mediumvioletred",      0xc71585 },
  { "midnightblue",         0x191970 },
  { "mintcream",            0xf5fffa },
  { "mistyrose",            0xffe4e1 },
  { "moccasin",             0xffe4b5 },
  { "navajowhite",          0xffdead },
  { "navy",                 0x000080 },
  { "oldlace",              0xfdf5e6 },
  { "olive",                0x808000 },
  { "olivedrab",            0x6b8e23 },
  { "orange",               0xffa500 },
  { "orangered",            0xff4500 },
  { "orchid",               0xda70d6 },
  { "palegoldenrod",        0xeee8aa },
  { "palegreen",            0x98fb98 },
  { "paleturquoise",        0xafeeee },
  { "palevioletred",        0xd87093 },
  { "papayawhip",           0xffefd5 },
  { "peachpuff",            0xffdab9 },
  { "peru",                 0xcd853f },
  { "pink",                 0xffc0cb },
  { "plum",                 0xdda0dd },
  { "powderblue",           0xb0e0e6 },
  { "purple",               0x800080 },
  { "red",                  0xff0000 },
  { "rosybrown",            0xbc8f8f },
  { "royalblue",            0x4169e1 },
  { "saddlebrown",          0x8b4513 },
  { "salmon",               0xfa8072 },
  { "sandybrown",           0xf4a460 },
  { "seagreen",             0x2e8b57 },
  { "seashell",             0xfff5ee },
  { "sienna",               0xa0522d },
  { "silver",               0xc0c0c0 },
  { "skyblue",              0x87ceeb },
  { "slateblue",            0x6a5acd },
  { "slategray",            0x708090 },
  { "snow",                 0xfffafa },
  { "springgreen",          0x00ff7f },
  { "steelblue",            0x4682b4 },
  { "tan",                  0xd2b48c },
  { "teal",                 0x008080 },
  { "thistle",              0xd8bfd8 },
  { "tomato",               0xff6347 },
  { "turquoise",            0x40e0d0 },
  { "violet",               0xee82ee },
  { "wheat",                0xf5deb3 },
  { "white",                0xffffff },
  { "whitesmoke",           0xf5f5f5 },
  { "yellow",               0xffff00 },
  { "yellowgreen",          0x9acd32 },
};

/*
** Attempt to translate a CSS color name into an integer that
** represents the equivalent RGB value.  Ignore alpha if provided.
** If the name cannot be translated, return -1.
*/
int color_name_to_rgb(const char *zName){
  if( zName==0 || zName[0]==0 ) return -1;
  if( zName[0]=='#' ){
    int i, v = 0;
    for(i=1; i<=6 && fossil_isxdigit(zName[i]); i++){
      v = v*16 + fossil_hexvalue(zName[i]);
    }
    if( i==4 ){
      v = fossil_hexvalue(zName[1])*0x110000 +
          fossil_hexvalue(zName[2])*0x1100 +
          fossil_hexvalue(zName[3])*0x11;
      return v;
    }
    if( i==7 ){
      return v;
    }
    return -1;
  }else{
    int iMin = 0;
    int iMax = count(aCssColors)-1;
    while( iMin<=iMax ){
      int iMid = (iMin+iMax)/2;
      int c = sqlite3_stricmp(aCssColors[iMid].zName, zName);
      if( c==0 ) return aCssColors[iMid].iRGB;
      if( c<0 ){
        iMin = iMid+1;
      }else{
        iMax = iMid-1;
      }
    }
    return -1;
  }
}

/*
** SETTING: raw-bgcolor                  boolean default=off
**
** Fossil usually tries to adjust user-specified background colors
** for checkins so that the text is readable and so that the color
** is not too garish. This setting disables that filter.  When
** this setting is on, the user-selected background colors are shown
** exactly as requested.
*/

/*
** Shift a color provided by the user so that it is suitable
** for use as a background color in the current skin.
**
** The return value is a #HHHHHH color name contained in
** static space that is overwritten on the next call.
**
** If we cannot make sense of the background color recommendation
** that is the input, then return NULL.
**
** The iFgClr parameter is normally 0.  But for testing purposes, set
** it to 1 for a black foregrounds and 2 for a white foreground.
*/
const char *reasonable_bg_color(const char *zRequested, int iFgClr){
  int iRGB = color_name_to_rgb(zRequested);
  int r, g, b;               /* RGB components of requested color */
  static int systemFg = 0;   /* 1==black-foreground 2==white-foreground */
  int fg;                    /* Foreground color to actually use */
  static char zColor[10];    /* Return value */

  if( iFgClr ){
    fg = iFgClr;
  }else if( systemFg==0 ){
    if( db_get_boolean("raw-bgcolor",0) ){
      fg = systemFg = 3;
    }else{
      fg = systemFg = skin_detail_boolean("white-foreground") ? 2 : 1;
    }
  }else{
    fg = systemFg;
  }
  if( fg>=3 ) return zRequested;

  if( iRGB<0 ) return 0;
  r = (iRGB>>16) & 0xff;
  g = (iRGB>>8) & 0xff;
  b = iRGB & 0xff;
  if( fg==1 ){
    /* Dark text on a light background.  Adjust so that
    ** no color component is less than 255-K, resulting in
    ** a pastel background color.  Color adjustment is quadratic
    ** so that colors that are further out of range have a greater
    ** adjustment. */
    const int K = 79;
    int k, x, m;
    m = r<g ? r : g;
    if( m>b ) m = b;
    k = (m*m)/255 + K;
    x = 255 - k;
    r = (k*r)/255 + x;
    g = (k*g)/255 + x;
    b = (k*b)/255 + x;
  }else{
    /* Light text on a dark background.  Adjust so that 
    ** no color component is greater than K, resulting in
    ** a low-intensity, low-saturation background color.
    ** The color adjustment is quadratic so that colors that
    ** are further out of range have a greater adjustment. */
    const int K = 112;
    int k, m;
    m = r>g ? r : g;
    if( m<b ) m = b;
    k = 255 - (255-K)*(m*m)/65025;
    r = (k*r)/255;
    g = (k*g)/255;
    b = (k*b)/255;
  }
  sqlite3_snprintf(8, zColor, "#%02x%02x%02x", r,g,b);
  return zColor;
}

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

/*
** WEBPAGE: test-bgcolor
**
** Show how user-specified background colors will be rendered
** using the reasonable_bg_color() algorithm.
*/
void test_bgcolor_page(void){
  const char *zReq;      /* Requested color name */
  const char *zBG;       /* Actual color provided */
  const char *zBg1;
  char zNm[10];
  static const char *azDflt[] = {
    "red", "orange", "yellow", "green", "blue", "indigo", "violet",
    "tan", "brown", "gray",
  };
  const int N = count(azDflt);
  int i, cnt, iClr, r, g, b;
  char *zFg;
  login_check_credentials();
  style_set_current_feature("test");
  style_header("Background Color Test");
  for(i=cnt=0; i<N; i++){
    sqlite3_snprintf(sizeof(zNm),zNm,"b%c",'a'+i);
    zReq = PD(zNm,azDflt[i]);
    if( zReq==0 || zReq[0]==0 ) continue;
    if( cnt==0 ){
      @ <table border="1" cellspacing="0" cellpadding="10">
      @ <tr>
      @ <th>Requested Background
      @ <th>Light mode
      @ <th>Dark mode
      @ </tr>
    }
    cnt++;
    zBG = reasonable_bg_color(zReq, 0);
    if( zBG==0 ){
      @ <tr><td colspan="3" align="center">\
      @ "%h(zReq)" is not a recognized color name</td></tr>
      continue;
    }
    iClr = color_name_to_rgb(zReq);
    r = (iClr>>16) & 0xff;
    g = (iClr>>8) & 0xff;
    b = iClr & 0xff;
    if( 3*r + 7*g + b > 6*255 ){
      zFg = "black";
    }else{
      zFg = "white";
    }
    if( zReq[0]!='#' ){
      char zReqRGB[12];
      sqlite3_snprintf(sizeof(zReqRGB),zReqRGB,"#%06x",color_name_to_rgb(zReq));
      @ <tr><td style='color:%h(zFg);background-color:%h(zReq);'>\
      @ Requested color "%h(zReq)" (%h(zReqRGB))</td>
    }else{
      @ <tr><td style='color:%h(zFg);background-color:%s(zReq);'>\
      @ Requested color "%h(zReq)"</td>
    }
    zBg1 = reasonable_bg_color(zReq,1);
    @ <td style='color:black;background-color:%h(zBg1);'>\
    @ Background color for dark text: %h(zBg1)</td>
    zBg1 = reasonable_bg_color(zReq,2);
    @ <td style='color:white;background-color:%h(zBg1);'>\
    @ Background color for light text: %h(zBg1)</td></tr>
  }
  if( cnt ){
    @ </table>
    @ <hr>
  }
  @ <form method="POST">
  @ <p>Enter CSS color names below and see them shifted into corresponding
  @ background colors above.</p>
  for(i=0; i<N; i++){
    sqlite3_snprintf(sizeof(zNm),zNm,"b%c",'a'+i);
    @ <input type="text" size="30" name='%s(zNm)' \
    @ value='%h(PD(zNm,azDflt[i]))'><br>
  }
  @ <input type="submit" value="Submit">
  @ </form>
  style_finish_page();
}
