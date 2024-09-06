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
#define CAPTCHA 2  /* Which captcha rendering to use */
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
  char *z = fossil_malloc( 9*12*3*strlen(zPw) + 8 );
  int i, j, k, m;

  k = 0;
  for(i=0; i<6; i++){
    for(j=0; zPw[j]; j++){
      unsigned char v = hex_digit_value(zPw[j]);
      v = (aFont1[v] >> ((5-i)*4)) & 0xf;
      for(m=8; m>=1; m = m>>1){
        if( v & m ){
          z[k++] = 0xe2;
          z[k++] = 0x96;
          z[k++] = 0x88;
          z[k++] = 0xe2;
          z[k++] = 0x96;
          z[k++] = 0x88;
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
/*
** A 5x7 pixel bitmap font for hexadecimal digits
*/
static const unsigned char aFont2[] = {
 /* 0 */  0x0e, 0x13, 0x15, 0x19, 0x11, 0x11, 0x0e,
 /* 1 */  0x02, 0x06, 0x0A, 0x02, 0x02, 0x02, 0x02,
 /* 2 */  0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f,
 /* 3 */  0x0e, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0e,
 /* 4 */  0x02, 0x06, 0x0A, 0x12, 0x1f, 0x02, 0x02,
 /* 5 */  0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e,
 /* 6 */  0x0e, 0x11, 0x10, 0x1e, 0x11, 0x11, 0x0e,
 /* 7 */  0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08,
 /* 8 */  0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e,
 /* 9 */  0x0e, 0x11, 0x11, 0x0f, 0x01, 0x11, 0x0e,
 /* A */  0x0e, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11,
 /* B */  0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e,
 /* C */  0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e,
 /* D */  0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c,
 /* E */  0x1f, 0x10, 0x10, 0x1c, 0x10, 0x10, 0x1f,
 /* F */  0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10,
};

/*
** Render an 8-character hexadecimal string as ascii art.
** Space to hold the result is obtained from malloc() and should be freed
** by the caller.
*/
char *captcha_render(const char *zPw){
  char *z = fossil_malloc( 160*strlen(zPw) + 9 );
  int i, j, k, m;

  k = 0;
  for(i=0; i<7; i++){
    for(j=0; zPw[j]; j++){
      unsigned char v = hex_digit_value(zPw[j]);
      v = aFont2[v*7+i];
      for(m=16; m>=1; m = m>>1){
        if( v & m ){
          z[k++] = 0xe2;
          z[k++] = 0x96;
          z[k++] = 0x88;
          z[k++] = 0xe2;
          z[k++] = 0x96;
          z[k++] = 0x88;
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
#endif /* CAPTCHA==2 */


#if CAPTCHA==3
static const char *const azFont3[] = {
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
      zChar = azFont3[4*v + i];
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

#if CAPTCHA==4
static const char *const azFont4[] = {
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
      zChar = azFont4[6*v + i];
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
#endif /* CAPTCHA==4 */

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

/* The SQL that will rotate the the captcha-secret. */
static const char captchaSecretRotationSql[] = 
@ SAVEPOINT rotate;
@ DELETE FROM config
@  WHERE name GLOB 'captcha-secret-*'
@    AND mtime<unixepoch('now','-6 hours');
@ UPDATE config
@    SET name=format('captcha-secret-%%d',substr(name,16)+1)
@  WHERE name GLOB 'captcha-secret-*';
@ UPDATE config
@    SET name='captcha-secret-1', mtime=unixepoch()
@  WHERE name='captcha-secret';
@ REPLACE INTO config(name,value,mtime)
@   VALUES('captcha-secret',%Q,unixepoch());
@ RELEASE rotate;
;


/*
** Create a new random captcha-secret.  Rotate the old one into
** the captcha-secret-N backups.  Purge captch-secret-N backups
** older than 6 hours.
**
** Do this on the current database and in all other databases of
** the same login group.
*/
void captcha_secret_rotate(void){
  char *zNew = db_text(0, "SELECT lower(hex(randomblob(20)))");
  char *zSql = mprintf(captchaSecretRotationSql/*works-like:"%Q"*/, zNew);
  char *zErrs = 0;
  fossil_free(zNew);
  db_unprotect(PROTECT_CONFIG);
  db_begin_transaction();
  sqlite3_exec(g.db, zSql, 0, 0, &zErrs);
  db_protect_pop();
  if( zErrs && zErrs[0] ){
    db_rollback_transaction();
    fossil_fatal("Unable to rotate captcha-secret\n%s\nERROR: %s\n",
                 zSql, zErrs);
  }
  db_end_transaction(0);
  login_group_sql(zSql, "", "", &zErrs);
  if( zErrs ){
    sqlite3_free(zErrs);  /* Silently ignore errors on other repos */
  }
  fossil_free(zSql);
}

/*
** Return the value of the N-th more recent captcha-secret.  The
** most recent captch-secret is 0.  Others are prior captcha-secrets
** that have expired, but are retained for a limited period of time
** so that pending anonymous login cookies and/or captcha dialogs
** don't malfunction when the captcha-secret changes.
**
** Clients should start by using the 0-th captcha-secret.  Only if
** that one does not work should they advance to 1 and 2 and so forth,
** until this routine returns a NULL pointer.
**
** The value returned is a string obtained from fossil_malloc() and
** should be freed by the caller.
**
** The 0-th captcha secret is the value of Config.Name='captcha-secret'.
** For N>0, the value is in Config.Name='captcha-secret-$N'.
*/
char *captcha_secret(int N){
  if( N==0 ){
    return db_text(0, "SELECT value FROM config WHERE name='captcha-secret'");
  }else{
    return db_text(0, 
        "SELECT value FROM config"
        " WHERE name='captcha-secret-%d'"
        "   AND mtime>unixepoch('now','-6 hours')", N);
  }
}

/*
** Translate a captcha seed value into the captcha password string.
** The returned string is static and overwritten on each call to
** this function.
**
** Use the N-th captcha secret to compute the password.  When N==0,
** a valid password is always returned.  A new captcha-secret will
** be created if necessary.  But for N>0, the return value might
** be NULL to indicate that there is no N-th captcha-secret.
*/
const char *captcha_decode(unsigned int seed, int N){
  char *zSecret;
  const char *z;
  Blob b;
  static char zRes[20];

  zSecret = captcha_secret(N);
  if( zSecret==0 ){
    if( N>0 ) return 0;
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
      "REPLACE INTO config(name,value)"
      " VALUES('captcha-secret', lower(hex(randomblob(20))));"
    );
    db_protect_pop();
    zSecret = captcha_secret(0);
    assert( zSecret!=0 );
  }
  blob_init(&b, 0, 0);
  blob_appendf(&b, "%s-%x", zSecret, seed);
  sha1sum_blob(&b, &b);
  z = blob_buffer(&b);
  memcpy(zRes, z, 8);
  zRes[8] = 0;
  fossil_free(zSecret);
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
int captcha_is_correct(int bAlwaysNeeded){
  const char *zSeed;
  const char *zEntered;
  const char *zDecode;
  char z[30];
  int i;
  int n = 0;
  if( !bAlwaysNeeded && !captcha_needed() ){
    return 1;  /* No captcha needed */
  }
  zSeed = P("captchaseed");
  if( zSeed==0 ) return 0;
  zEntered = P("captcha");
  if( zEntered==0 || strlen(zEntered)!=8 ) return 0;
  do{
    zDecode = captcha_decode((unsigned int)atoi(zSeed), n++);
    if( zDecode==0 ) return 0;
    assert( strlen(zDecode)==8 );
    for(i=0; i<8; i++){
      char c = zEntered[i];
      if( c>='A' && c<='F' ) c += 'a' - 'A';
      if( c=='O' ) c = '0';
      z[i] = c;
    }
  }while( strncmp(zDecode,z,8)!=0 );
  return 1;
}

/*
** Generate a captcha display together with the necessary hidden parameter
** for the seed and the entry box into which the user will type the text of
** the captcha.  This is typically done at the very bottom of a form.
**
** This routine is a no-op if no captcha is required.
**
** Flag values:
**
**     0x01     Show the "Submit" button in the form.
**     0x02     Always generate the captcha, even if not required
*/
void captcha_generate(int mFlags){
  unsigned int uSeed;
  const char *zDecoded;
  char *zCaptcha;

  if( !captcha_needed() && (mFlags & 0x02)==0 ) return;
  uSeed = captcha_seed();
  zDecoded = captcha_decode(uSeed, 0);
  zCaptcha = captcha_render(zDecoded);
  @ <div class="captcha"><table class="captcha"><tr><td><pre class="captcha">
  @ %h(zCaptcha)
  @ </pre>
  @ Enter security code shown above:
  @ <input type="hidden" name="captchaseed" value="%u(uSeed)">
  @ <input type="text" name="captcha" size="8" autofocus>
  if( mFlags & 0x01 ){
    @ <input type="submit" value="Submit">
  }
  @ <br/>\
  captcha_speakit_button(uSeed, 0);
  @ </td></tr></table></div>
}

/*
** Add a "Speak the captcha" button.
*/
void captcha_speakit_button(unsigned int uSeed, const char *zMsg){
  if( zMsg==0 ) zMsg = "Speak the text";
  @ <input aria-label="%h(zMsg)" type="button" value="%h(zMsg)" \
  @ id="speakthetext">
  @ <script nonce="%h(style_nonce())">/* captcha_speakit_button() */
  @ document.getElementById("speakthetext").onclick = function(){
  @   var audio = window.fossilAudioCaptcha \
  @ || new Audio("%R/captcha-audio/%u(uSeed)");
  @   window.fossilAudioCaptcha = audio;
  @   audio.currentTime = 0;
  @   audio.play();
  @ }
  @ </script>
}

/*
** WEBPAGE: test-captcha
**
** If the name query parameter is provided, then render the hex value of
** the name using the captcha font.
**
** Otherwise render the captcha screen.  The "show-button" parameter causes
** the submit button to be rendered.
*/
void captcha_test(void){
  const char *zPw = P("name");
  if( zPw==0 || zPw[0]==0 ){
    (void)exclude_spiders(1);
    @ <hr><p>The captcha is shown above.  Add a name=HEX query parameter
    @ to see how HEX would be rendered in the current captcha font.
    @ <h2>Debug/Testing Values:</h2>
    @ <ul>
    @ <li> g.isHuman = %d(g.isHuman)
    @ <li> g.zLogin = %h(g.zLogin)
    @ <li> login_cookie_welformed() = %d(login_cookie_wellformed())
    @ <li> captcha_is_correct(1) = %d(captcha_is_correct(1)).
    @ </ul>
    style_finish_page();
  }else{
    style_set_current_feature("test");
    style_header("Captcha Test");
    @ <pre class="captcha">
    @ %s(captcha_render(zPw))
    @ </pre>
    style_finish_page();
  }
}

/*
** Check to see if the current request is coming from an agent that 
** self-identifies as a spider.
**
** If the agent does not claim to be a spider or if the user has logged
** in (even as anonymous), then return 0 without doing anything.
**
** But if the user agent does self-identify as a spider and there is
** no login, offer a captcha challenge to allow the user agent to prove
** that he is human and return non-zero.
**
** If the bTest argument is non-zero, then show the captcha regardless of
** how the agent identifies.  This is used for testing only.
*/
int exclude_spiders(int bTest){
  if( !bTest ){
    if( g.isHuman ) return 0;  /* This user has already proven human */
    if( g.zLogin!=0 ) return 0;  /* Logged in.  Consider them human */
    if( login_cookie_wellformed() ){
      /* Logged into another member of the login group */
      return 0;
    }
  }

  /* This appears to be a spider.  Offer the captcha */
  style_set_current_feature("captcha");
  style_header("I think you are a robot");
  style_submenu_enable(0);
  @ <form method='POST' action='%R/ityaar'>
  @ <p>You seem like a robot.
  @
  @ <p>If you are human, you can prove that by solving the captcha below,
  @ after which you will be allowed to proceed.
  if( bTest ){
    @ <input type="hidden" name="istest" value="1">
  }
  captcha_generate(3);
  @ </form>
  if( !bTest ){
    if( P("fossil-goto")==0 ){
      cgi_set_cookie("fossil-goto", cgi_reconstruct_original_url(), 0, 600);
    }
    cgi_append_header("X-Robot: 1\r\n");
    style_finish_page();
  }
  return 1;
}

/*
** WEBPAGE: ityaar
**
** This is the action for the form that is the captcha.  Not intended
** for external use.  "ityaar" is an acronym "I Think You Are A Robot".
**
** If the captcha is correctly solved, then an anonymous login cookie
** is set.  Regardless of whether or not the captcha was solved, this
** page always redirects to the fossil-goto cookie.
*/
void captcha_callback(void){
  int bTest = atoi(PD("istest","0"));
  if( captcha_is_correct(1) ){
    if( bTest==0 ){
      if( !login_cookie_wellformed() ){
        /* ^^^^--- Don't overwrite a valid login on another repo! */
        login_set_anon_cookie(0, 0);
      }
      cgi_append_header("X-Robot: 0\r\n");
    }
    login_redirect_to_g();
  }else{
    g.isHuman = 0;
    (void)exclude_spiders(bTest);
    if( bTest ){
      @ <hr><p>Wrong code.  Try again
      style_finish_page();
    }
  }
}


/*
** Generate a WAV file that reads aloud the hex digits given by
** zHex.
*/
static void captcha_wav(const char *zHex, Blob *pOut){
  int i;
  const int szWavHdr = 44;
  blob_init(pOut, 0, 0);
  blob_resize(pOut, szWavHdr);  /* Space for the WAV header */
  pOut->nUsed = szWavHdr;
  memset(pOut->aData, 0, szWavHdr);
  if( zHex==0 || zHex[0]==0 ) zHex = "0";
  for(i=0; zHex[i]; i++){
    int v = hex_digit_value(zHex[i]);
    int sz;
    int nData;
    const unsigned char *pData;
    char zSoundName[50];
    sqlite3_snprintf(sizeof(zSoundName),zSoundName,"sounds/%c.wav",
                     "0123456789abcdef"[v]);
    /* Extra silence in between letters */
    if( i>0 ){
      int nQuiet = 3000;
      blob_resize(pOut, pOut->nUsed+nQuiet);
      memset(pOut->aData+pOut->nUsed-nQuiet, 0x80, nQuiet);
    }
    pData = builtin_file(zSoundName, &sz);
    nData = sz - szWavHdr;
    blob_resize(pOut, pOut->nUsed+nData);
    memcpy(pOut->aData+pOut->nUsed-nData, pData+szWavHdr, nData);
    if( zHex[i+1]==0 ){
      int len = pOut->nUsed + 36;
      memcpy(pOut->aData, pData, szWavHdr);
      pOut->aData[4] = (char)(len&0xff);
      pOut->aData[5] = (char)((len>>8)&0xff);
      pOut->aData[6] = (char)((len>>16)&0xff);
      pOut->aData[7] = (char)((len>>24)&0xff);
      len = pOut->nUsed;
      pOut->aData[40] = (char)(len&0xff);
      pOut->aData[41] = (char)((len>>8)&0xff);
      pOut->aData[42] = (char)((len>>16)&0xff);
      pOut->aData[43] = (char)((len>>24)&0xff);
    }
  }
}

/*
** WEBPAGE: /captcha-audio
**
** Return a WAV file that pronounces the digits of the captcha that
** is determined by the seed given in the name= query parameter.
*/
void captcha_wav_page(void){
  const char *zSeed = PD("name","0");
  const char *zDecode = captcha_decode((unsigned int)atoi(zSeed), 0);
  Blob audio;
  captcha_wav(zDecode, &audio);
  cgi_set_content_type("audio/wav");
  cgi_set_content(&audio);
}

/*
** WEBPAGE: /test-captcha-audio
**
** Return a WAV file that pronounces the hex digits of the name=
** query parameter.
*/
void captcha_test_wav_page(void){
  const char *zSeed = P("name");
  Blob audio;
  captcha_wav(zSeed, &audio);
  cgi_set_content_type("audio/wav");
  cgi_set_content(&audio);
}
