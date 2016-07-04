/*
** Copyright (c) 2009 D. Richard Hipp
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
** This file contains code to a simple text-based CAPTCHA.  Though easily
** defeated by a sophisticated attacker, this CAPTCHA does at least make
** scripting attacks more difficult.
*/
#include "config.h"
#include <assert.h>
#include "captcha.h"

#if INTERFACE
#define CAPTCHA 3  /* Which captcha rendering to use */
#endif

/*
** Convert a hex digit into a value between 0 and 15
*/
int hex_digit_value(char c){
  if( c>='0' && c<='9' ){
    return c - '0';
  }else if( c>='a' && c<='f' ){
    return c - 'a' + 10;
  }else if( c>='A' && c<='F' ){
    return c - 'A' + 10;
  }else{
    return 0;
  }
}

#if CAPTCHA==1
/*
** A 4x6 pixel bitmap font for hexadecimal digits
*/
static const unsigned int aFont1[] = {
  0x699996,
  0x262227,
  0x69124f,
  0xf16196,
  0x26af22,
  0xf8e196,
  0x68e996,
  0xf12244,
  0x696996,
  0x699716,
  0x699f99,
  0xe9e99e,
  0x698896,
  0xe9999e,
  0xf8e88f,
  0xf8e888,
};

/*
** Render an 8-character hexadecimal string as ascii art.
** Space to hold the result is obtained from malloc() and should be freed
** by the caller.
*/
char *captcha_render(const char *zPw){
  char *z = fossil_malloc( 9*6*strlen(zPw) + 7 );
  int i, j, k, m;

  k = 0;
  for(i=0; i<6; i++){
    for(j=0; zPw[j]; j++){
      unsigned char v = hex_digit_value(zPw[j]);
      v = (aFont1[v] >> ((5-i)*4)) & 0xf;
      for(m=8; m>=1; m = m>>1){
        if( v & m ){
          z[k++] = 'X';
          z[k++] = 'X';
        }else{
          z[k++] = ' ';
          z[k++] = ' ';
        }
      }
      z[k++] = ' ';
      z[k++] = ' ';
    }
    z[k++] = '\n';
  }
  z[k] = 0;
  return z;
}
#endif /* CAPTCHA==1 */


#if CAPTCHA==2
static const char *const azFont2[] = {
 /* 0 */
 "  __  ",
 " /  \\ ",
 "| () |",
 " \\__/ ",

 /* 1 */
 " _ ",
 "/ |",
 "| |",
 "|_|",

 /* 2 */
 " ___ ",
 "|_  )",
 " / / ",
 "/___|",

 /* 3 */
 " ____",
 "|__ /",
 " |_ \\",
 "|___/",

 /* 4 */
 " _ _  ",
 "| | | ",
 "|_  _|",
 "  |_| ",

 /* 5 */
 " ___ ",
 "| __|",
 "|__ \\",
 "|___/",

 /* 6 */
 "  __ ",
 " / / ",
 "/ _ \\",
 "\\___/",

 /* 7 */
 " ____ ",
 "|__  |",
 "  / / ",
 " /_/  ",

 /* 8 */
 " ___ ",
 "( _ )",
 "/ _ \\",
 "\\___/",

 /* 9 */
 " ___ ",
 "/ _ \\",
 "\\_, /",
 " /_/ ",

 /* A */
 "      ",
 "  /\\  ",
 " /  \\ ",
 "/_/\\_\\",

 /* B */
 " ___ ",
 "| _ )",
 "| _ \\",
 "|___/",

 /* C */
 "  ___ ",
 " / __|",
 "| (__ ",
 " \\___|",

 /* D */
 " ___  ",
 "|   \\ ",
 "| |) |",
 "|___/ ",

 /* E */
 " ___ ",
 "| __|",
 "| _| ",
 "|___|",

 /* F */
 " ___ ",
 "| __|",
 "| _| ",
 "|_|  ",
};

/*
** Render an 8-digit hexadecimal string as ascii arg.
** Space to hold the result is obtained from malloc() and should be freed
** by the caller.
*/
char *captcha_render(const char *zPw){
  char *z = fossil_malloc( 7*4*strlen(zPw) + 5 );
  int i, j, k, m;
  const char *zChar;

  k = 0;
  for(i=0; i<4; i++){
    for(j=0; zPw[j]; j++){
      unsigned char v = hex_digit_value(zPw[j]);
      zChar = azFont2[4*v + i];
      for(m=0; zChar[m]; m++){
        z[k++] = zChar[m];
      }
    }
    z[k++] = '\n';
  }
  z[k] = 0;
  return z;
}
#endif /* CAPTCHA==2 */

#if CAPTCHA==3
static const char *const azFont3[] = {
  /* 0 */
  "  ___  ",
  " / _ \\ ",
  "| | | |",
  "| | | |",
  "| |_| |",
  " \\___/ ",

  /* 1 */
  " __ ",
  "/_ |",
  " | |",
  " | |",
  " | |",
  " |_|",

  /* 2 */
  " ___  ",
  "|__ \\ ",
  "   ) |",
  "  / / ",
  " / /_ ",
  "|____|",

  /* 3 */
  " ____  ",
  "|___ \\ ",
  "  __) |",
  " |__ < ",
  " ___) |",
  "|____/ ",

  /* 4 */
  " _  _   ",
  "| || |  ",
  "| || |_ ",
  "|__   _|",
  "   | |  ",
  "   |_|  ",

  /* 5 */
  " _____ ",
  "| ____|",
  "| |__  ",
  "|___ \\ ",
  " ___) |",
  "|____/ ",

  /* 6 */
  "   __  ",
  "  / /  ",
  " / /_  ",
  "| '_ \\ ",
  "| (_) |",
  " \\___/ ",

  /* 7 */
  " ______ ",
  "|____  |",
  "    / / ",
  "   / /  ",
  "  / /   ",
  " /_/    ",

  /* 8 */
  "  ___  ",
  " / _ \\ ",
  "| (_) |",
  " > _ < ",
  "| (_) |",
  " \\___/ ",

  /* 9 */
  "  ___  ",
  " / _ \\ ",
  "| (_) |",
  " \\__, |",
  "   / / ",
  "  /_/  ",

  /* A */
  "          ",
  "    /\\    ",
  "   /  \\   ",
  "  / /\\ \\  ",
  " / ____ \\ ",
  "/_/    \\_\\",

  /* B */
  " ____  ",
  "|  _ \\ ",
  "| |_) |",
  "|  _ < ",
  "| |_) |",
  "|____/ ",

  /* C */
  "  _____ ",
  " / ____|",
  "| |     ",
  "| |     ",
  "| |____ ",
  " \\_____|",

  /* D */
  " _____  ",
  "|  __ \\ ",
  "| |  | |",
  "| |  | |",
  "| |__| |",
  "|_____/ ",

  /* E */
  " ______ ",
  "|  ____|",
  "| |__   ",
  "|  __|  ",
  "| |____ ",
  "|______|",

  /* F */
  " ______ ",
  "|  ____|",
  "| |__   ",
  "|  __|  ",
  "| |     ",
  "|_|     ",
};

/*
** Render an 8-digit hexadecimal string as ascii arg.
** Space to hold the result is obtained from malloc() and should be freed
** by the caller.
*/
char *captcha_render(const char *zPw){
  char *z = fossil_malloc( 10*6*strlen(zPw) + 7 );
  int i, j, k, m;
  const char *zChar;
  unsigned char x;
  int y;

  k = 0;
  for(i=0; i<6; i++){
    x = 0;
    for(j=0; zPw[j]; j++){
      unsigned char v = hex_digit_value(zPw[j]);
      x = (x<<4) + v;
      switch( x ){
        case 0x7a:
        case 0xfa:
          y = 3;
          break;
        case 0x47:
          y = 2;
          break;
        case 0xf6:
        case 0xa9:
        case 0xa4:
        case 0xa1:
        case 0x9a:
        case 0x76:
        case 0x61:
        case 0x67:
        case 0x69:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x4a:
          y = 1;
          break;
        default:
          y = 0;
          break;
      }
      zChar = azFont3[6*v + i];
      while( y && zChar[0]==' ' ){ y--; zChar++; }
      while( y && z[k-1]==' ' ){ y--; k--; }
      for(m=0; zChar[m]; m++){
        z[k++] = zChar[m];
      }
    }
    z[k++] = '\n';
  }
  z[k] = 0;
  return z;
}
#endif /* CAPTCHA==3 */

/*
** COMMAND: test-captcha
**
** Render an ASCII-art captcha for numbers given on the command line.
*/
void test_captcha(void){
  int i;
  unsigned int v;
  char *z;

  for(i=2; i<g.argc; i++){
    char zHex[30];
    v = (unsigned int)atoi(g.argv[i]);
    sqlite3_snprintf(sizeof(zHex), zHex, "%x", v);
    z = captcha_render(zHex);
    fossil_print("%s:\n%s", zHex, z);
    free(z);
  }
}

/*
** Compute a seed value for a captcha.  The seed is public and is sent
** as a hidden parameter with the page that contains the captcha.  Knowledge
** of the seed is insufficient for determining the captcha without additional
** information held only on the server and never revealed.
*/
unsigned int captcha_seed(void){
  unsigned int x;
  sqlite3_randomness(sizeof(x), &x);
  x &= 0x7fffffff;
  return x;
}

/*
** Translate a captcha seed value into the captcha password string.
** The returned string is static and overwritten on each call to
** this function.
*/
const char *captcha_decode(unsigned int seed){
  const char *zSecret;
  const char *z;
  Blob b;
  static char zRes[20];

  zSecret = db_get("captcha-secret", 0);
  if( zSecret==0 ){
    db_multi_exec(
      "REPLACE INTO config(name,value)"
      " VALUES('captcha-secret', lower(hex(randomblob(20))));"
    );
    zSecret = db_get("captcha-secret", 0);
    assert( zSecret!=0 );
  }
  blob_init(&b, 0, 0);
  blob_appendf(&b, "%s-%x", zSecret, seed);
  sha1sum_blob(&b, &b);
  z = blob_buffer(&b);
  memcpy(zRes, z, 8);
  zRes[8] = 0;
  return zRes;
}

/*
** Return true if a CAPTCHA is required for editing wiki or tickets or for
** adding attachments.
**
** A CAPTCHA is required in those cases if the user is not logged in (if they
** are user "nobody") and if the "require-captcha" setting is true.  The
** "require-captcha" setting is controlled on the Admin/Access page.  It
** defaults to true.
*/
int captcha_needed(void){
  return login_is_nobody() && db_get_boolean("require-captcha", 1);
}

/*
** If a captcha is required but the correct captcha code is not supplied
** in the query parameters, then return false (0).
**
** If no captcha is required or if the correct captcha is supplied, return
** true (non-zero).
**
** The query parameters examined are "captchaseed" for the seed value and
** "captcha" for text that the user types in response to the captcha prompt.
*/
int captcha_is_correct(void){
  const char *zSeed;
  const char *zEntered;
  const char *zDecode;
  char z[30];
  int i;
  if( !captcha_needed() ){
    return 1;  /* No captcha needed */
  }
  zSeed = P("captchaseed");
  if( zSeed==0 ) return 0;
  zEntered = P("captcha");
  if( zEntered==0 || strlen(zEntered)!=8 ) return 0;
  zDecode = captcha_decode((unsigned int)atoi(zSeed));
  assert( strlen(zDecode)==8 );
  if( strlen(zEntered)!=8 ) return 0;
  for(i=0; i<8; i++){
    char c = zEntered[i];
    if( c>='A' && c<='F' ) c += 'a' - 'A';
    if( c=='O' ) c = '0';
    z[i] = c;
  }
  if( strncmp(zDecode,z,8)!=0 ) return 0;
  return 1;
}

/*
** Generate a captcha display together with the necessary hidden parameter
** for the seed and the entry box into which the user will type the text of
** the captcha.  This is typically done at the very bottom of a form.
**
** This routine is a no-op if no captcha is required.
*/
void captcha_generate(int showButton){
  unsigned int uSeed;
  const char *zDecoded;
  char *zCaptcha;

  if( !captcha_needed() ) return;
  uSeed = captcha_seed();
  zDecoded = captcha_decode(uSeed);
  zCaptcha = captcha_render(zDecoded);
  @ <div class="captcha"><table class="captcha"><tr><td><pre>
  @ %h(zCaptcha)
  @ </pre>
  @ Enter security code shown above:
  @ <input type="hidden" name="captchaseed" value="%u(uSeed)" />
  @ <input type="text" name="captcha" size=8 />
  if( showButton ){
    @ <input type="submit" value="Submit">
  }
  @ </td></tr></table></div>
}

/*
** WEBPAGE: test-captcha
** Test the captcha-generator by rendering the value of the name= query
** parameter using ascii-art.  If name= is omitted, show a random 16-digit
** hexadecimal number.
*/
void captcha_test(void){
  const char *zPw = P("name");
  if( zPw==0 || zPw[0]==0 ){
    u64 x;
    sqlite3_randomness(sizeof(x), &x);
    zPw = mprintf("%016llx", x);
  }
  style_header("Captcha Test");
  @ <pre>
  @ %s(captcha_render(zPw))
  @ </pre>
  style_footer();
}

/*
** Check to see if the current request is coming from an agent that might
** be a spider.  If the agent is not a spider, then return 0 without doing
** anything.  But if the user agent appears to be a spider, offer
** a captcha challenge to allow the user agent to prove that it is human
** and return non-zero.
*/
int exclude_spiders(void){
  const char *zCookieValue;
  char *zCookieName;
  if( g.isHuman ) return 0;
#if 0
  {
    const char *zReferer = P("HTTP_REFERER");
    if( zReferer && strncmp(g.zBaseURL, zReferer, strlen(g.zBaseURL))==0 ){
      return 0;
    }
  }
#endif
  zCookieName = mprintf("fossil-cc-%.10s", db_get("project-code","x"));
  zCookieValue = P(zCookieName);
  if( zCookieValue && atoi(zCookieValue)==1 ) return 0;
  if( captcha_is_correct() ){
    cgi_set_cookie(zCookieName, "1", login_cookie_path(), 8*3600);
    return 0;
  }

  /* This appears to be a spider.  Offer the captcha */
  style_header("Verification");
  @ <form method='POST' action='%s(g.zPath)'>
  cgi_query_parameters_to_hidden();
  @ <p>Please demonstrate that you are human, not a spider or robot</p>
  captcha_generate(1);
  @ </form>
  style_footer();
  return 1;
}
