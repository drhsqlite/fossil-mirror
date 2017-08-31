/*
** Copyright (c) 2006 D. Richard Hipp
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
** Commands and procedures used for creating, processing, editing, and
** querying information about users.
*/
#include "config.h"
#include "user.h"

/*
** Strip leading and trailing space from a string and add the string
** onto the end of a blob.
*/
static void strip_string(Blob *pBlob, char *z){
  int i;
  blob_reset(pBlob);
  while( fossil_isspace(*z) ){ z++; }
  for(i=0; z[i]; i++){
    if( z[i]=='\r' || z[i]=='\n' ){
       while( i>0 && fossil_isspace(z[i-1]) ){ i--; }
       z[i] = 0;
       break;
    }
    if( z[i]>0 && z[i]<' ' ) z[i] = ' ';
  }
  blob_append(pBlob, z, -1);
}

#if defined(_WIN32) || defined(__BIONIC__)
#ifdef _WIN32
#include <conio.h>
#endif

/*
** getpass() for Windows and Android.
*/
static char *zPwdBuffer = 0;
static size_t nPwdBuffer = 0;

static char *getpass(const char *prompt){
  char *zPwd;
  size_t nPwd;
  size_t i;
#if defined(_WIN32)
  int useGetch = _isatty(_fileno(stderr));
#endif

  if( zPwdBuffer==0 ){
    zPwdBuffer = fossil_secure_alloc_page(&nPwdBuffer);
    assert( zPwdBuffer );
  }else{
    fossil_secure_zero(zPwdBuffer, nPwdBuffer);
  }
  zPwd = zPwdBuffer;
  nPwd = nPwdBuffer;
  fputs(prompt,stderr);
  fflush(stderr);
  assert( zPwd!=0 );
  assert( nPwd>0 );
  for(i=0; i<nPwd-1; ++i){
#if defined(_WIN32)
    zPwd[i] = useGetch ? _getch() : getc(stdin);
#else
    zPwd[i] = getc(stdin);
#endif
    if(zPwd[i]=='\r' || zPwd[i]=='\n'){
      break;
    }
    /* BS or DEL */
    else if(i>0 && (zPwd[i]==8 || zPwd[i]==127)){
      i -= 2;
      continue;
    }
    /* CTRL-C */
    else if(zPwd[i]==3) {
      i=0;
      break;
    }
    /* ESC */
    else if(zPwd[i]==27){
      i=0;
      break;
    }
    else{
#if defined(_WIN32)
      if( useGetch )
#endif
      fputc('*',stderr);
    }
  }
  zPwd[i]='\0';
  fputs("\n", stderr);
  assert( zPwd==zPwdBuffer );
  return zPwd;
}
void freepass(){
  if( !zPwdBuffer ) return;
  assert( nPwdBuffer>0 );
  fossil_secure_free_page(zPwdBuffer, nPwdBuffer);
}
#endif

#if defined(_WIN32) || defined(WIN32)
# include <io.h>
# include <fcntl.h>
# undef popen
# define popen _popen
# undef pclose
# define pclose _pclose
#endif

/*
** Scramble substitution matrix:
*/
static char aSubst[256];

/*
** Descramble the password
*/
static void userDescramble(char *z){
  int i;
  for(i=0; z[i]; i++) z[i] = aSubst[(unsigned char)z[i]];
}

/* Print a string in 5-letter groups */
static void printFive(const unsigned char *z){
  int i;
  for(i=0; z[i]; i++){
    if( i>0 && (i%5)==0 ) putchar(' ');
    putchar(z[i]);
  }
  putchar('\n');
}

/* Return a pseudo-random integer between 0 and N-1 */
static int randint(int N){
  unsigned char x;
  assert( N<256 );
  sqlite3_randomness(1, &x);
  return x % N;
}

/*
** Generate and print a random scrambling of letters a through z (omitting x)
** and set up the aSubst[] matrix to descramble.
*/
static void userGenerateScrambleCode(void){
  unsigned char zOrig[30];
  unsigned char zA[30];
  unsigned char zB[30];
  int nA = 25;
  int nB = 0;
  int i;
  memcpy(zOrig, "abcdefghijklmnopqrstuvwyz", nA+1);
  memcpy(zA, zOrig, nA+1);
  assert( nA==(int)strlen((char*)zA) );
  for(i=0; i<sizeof(aSubst); i++) aSubst[i] = i;
  printFive(zA);
  while( nA>0 ){
    int x = randint(nA);
    zB[nB++] = zA[x];
    zA[x] = zA[--nA];
  }
  assert( nB==25 );
  zB[nB] = 0;
  printFive(zB);
  for(i=0; i<nB; i++) aSubst[zB[i]] = zOrig[i];
}

/*
** Return the value of the FOSSIL_SECURITY_LEVEL environment variable.
** Or return 0 if that variable does not exist.
*/
int fossil_security_level(void){
  const char *zLevel = fossil_getenv("FOSSIL_SECURITY_LEVEL");
  if( zLevel==0 ) return 0;
  return atoi(zLevel);
}


/*
** Do a single prompt for a passphrase.  Store the results in the blob.
**
**
** The return value is a pointer to a static buffer that is overwritten
** on subsequent calls to this same routine.
*/
static void prompt_for_passphrase(const char *zPrompt, Blob *pPassphrase){
  char *z;
#if 0
  */
  ** If the FOSSIL_PWREADER environment variable is set, then it will
  ** be the name of a program that prompts the user for their password/
  ** passphrase in a secure manner.  The program should take one or more
  ** arguments which are the prompts and should output the acquired
  ** passphrase as a single line on stdout.  This function will read the
  ** output using popen().
  **
  ** If FOSSIL_PWREADER is not set, or if it is not the name of an
  ** executable, then use the C-library getpass() routine.
  */
  const char *zProg = fossil_getenv("FOSSIL_PWREADER");
  if( zProg && zProg[0] ){
    static char zPass[100];
    Blob cmd;
    FILE *in;
    blob_zero(&cmd);
    blob_appendf(&cmd, "%s \"Fossil Passphrase\" \"%s\"", zProg, zPrompt);
    zPass[0] = 0;
    in = popen(blob_str(&cmd), "r");
    fgets(zPass, sizeof(zPass), in);
    pclose(in);
    blob_reset(&cmd);
    z = zPass;
  }else
#endif
  if( fossil_security_level()>=2 ){
    userGenerateScrambleCode();
    z = getpass(zPrompt);
    if( z ) userDescramble(z);
    printf("\033[3A\033[J");  /* Erase previous three lines */
    fflush(stdout);
  }else{
    z = getpass(zPrompt);
  }
  strip_string(pPassphrase, z);
}

/*
** Prompt the user for a password.  Store the result in the pPassphrase
** blob.
**
** Behavior is controlled by the verify parameter:
**
**     0     Just ask once.
**
**     1     If the first answer is a non-empty string, ask for
**           verification.  Repeat if the two strings do not match.
**
**     2     Ask twice, repeat if the strings do not match.
*/
void prompt_for_password(
  const char *zPrompt,
  Blob *pPassphrase,
  int verify
){
  Blob secondTry;
  blob_zero(pPassphrase);
  blob_zero(&secondTry);
  while(1){
    prompt_for_passphrase(zPrompt, pPassphrase);
    if( verify==0 ) break;
    if( verify==1 && blob_size(pPassphrase)==0 ) break;
    prompt_for_passphrase("Retype new password: ", &secondTry);
    if( blob_compare(pPassphrase, &secondTry) ){
      fossil_print("Passphrases do not match.  Try again...\n");
    }else{
      break;
    }
  }
  blob_reset(&secondTry);
}

/*
** Prompt to save Fossil user password
*/
int save_password_prompt(const char *passwd){
  Blob x;
  char c;
  const char *old = db_get("last-sync-pw", 0);
  if( (old!=0) && fossil_strcmp(unobscure(old), passwd)==0 ){
     return 0;
  }
  if( fossil_security_level()>=1 ) return 0;
  prompt_user("remember password (Y/n)? ", &x);
  c = blob_str(&x)[0];
  blob_reset(&x);
  return ( c!='n' && c!='N' );
}

/*
** Prompt for Fossil user password
*/
char *prompt_for_user_password(const char *zUser){
  char *zPrompt = mprintf("\rpassword for %s: ", zUser);
  char *zPw;
  Blob x;
  fossil_force_newline();
  prompt_for_password(zPrompt, &x, 0);
  free(zPrompt);
  zPw = mprintf("%b", &x);
  blob_reset(&x);
  return zPw;
}

/*
** Prompt the user to enter a single line of text.
*/
void prompt_user(const char *zPrompt, Blob *pIn){
  char *z;
  char zLine[1000];
  blob_zero(pIn);
  fossil_force_newline();
  fossil_print("%s", zPrompt);
  fflush(stdout);
  z = fgets(zLine, sizeof(zLine), stdin);
  if( z ){
    int n = (int)strlen(z);
    if( n>0 && z[n-1]=='\n' ) fossil_new_line_started();
    strip_string(pIn, z);
  }
}

/*
** COMMAND: user*
**
** Usage: %fossil user SUBCOMMAND ...  ?-R|--repository FILE?
**
** Run various subcommands on users of the open repository or of
** the repository identified by the -R or --repository option.
**
**    %fossil user capabilities USERNAME ?STRING?
**
**        Query or set the capabilities for user USERNAME
**
**    %fossil user default ?USERNAME?
**
**        Query or set the default user.  The default user is the
**        user for command-line interaction.
**
**    %fossil user list
**    %fossil user ls
**
**        List all users known to the repository
**
**    %fossil user new ?USERNAME? ?CONTACT-INFO? ?PASSWORD?
**
**        Create a new user in the repository.  Users can never be
**        deleted.  They can be denied all access but they must continue
**        to exist in the database.
**
**    %fossil user password USERNAME ?PASSWORD?
**
**        Change the web access password for a user.
*/
void user_cmd(void){
  int n;
  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    usage("capabilities|default|list|new|password ...");
  }
  n = strlen(g.argv[2]);
  if( n>=2 && strncmp(g.argv[2],"new",n)==0 ){
    Blob passwd, login, caps, contact;
    char *zPw;
    blob_init(&caps, db_get("default-perms", "u"), -1);

    if( g.argc>=4 ){
      blob_init(&login, g.argv[3], -1);
    }else{
      prompt_user("login: ", &login);
    }
    if( db_exists("SELECT 1 FROM user WHERE login=%B", &login) ){
      fossil_fatal("user %b already exists", &login);
    }
    if( g.argc>=5 ){
      blob_init(&contact, g.argv[4], -1);
    }else{
      prompt_user("contact-info: ", &contact);
    }
    if( g.argc>=6 ){
      blob_init(&passwd, g.argv[5], -1);
    }else{
      prompt_for_password("password: ", &passwd, 1);
    }
    zPw = sha1_shared_secret(blob_str(&passwd), blob_str(&login), 0);
    db_multi_exec(
      "INSERT INTO user(login,pw,cap,info,mtime)"
      "VALUES(%B,%Q,%B,%B,now())",
      &login, zPw, &caps, &contact
    );
    free(zPw);
  }else if( n>=2 && strncmp(g.argv[2],"default",n)==0 ){
    if( g.argc==3 ){
      user_select();
      fossil_print("%s\n", g.zLogin);
    }else{
      if( !db_exists("SELECT 1 FROM user WHERE login=%Q", g.argv[3]) ){
        fossil_fatal("no such user: %s", g.argv[3]);
      }
      if( g.localOpen ){
        db_lset("default-user", g.argv[3]);
      }else{
        db_set("default-user", g.argv[3], 0);
      }
    }
  }else if(( n>=2 && strncmp(g.argv[2],"list",n)==0 ) || ( n>=2 && strncmp(g.argv[2],"ls",n)==0 )){
    Stmt q;
    db_prepare(&q, "SELECT login, info FROM user ORDER BY login");
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%-12s %s\n", db_column_text(&q, 0), db_column_text(&q, 1));
    }
    db_finalize(&q);
  }else if( n>=2 && strncmp(g.argv[2],"password",2)==0 ){
    char *zPrompt;
    int uid;
    Blob pw;
    if( g.argc!=4 && g.argc!=5 ) usage("password USERNAME ?NEW-PASSWORD?");
    uid = db_int(0, "SELECT uid FROM user WHERE login=%Q", g.argv[3]);
    if( uid==0 ){
      fossil_fatal("no such user: %s", g.argv[3]);
    }
    if( g.argc==5 ){
      blob_init(&pw, g.argv[4], -1);
    }else{
      zPrompt = mprintf("New password for %s: ", g.argv[3]);
      prompt_for_password(zPrompt, &pw, 1);
    }
    if( blob_size(&pw)==0 ){
      fossil_print("password unchanged\n");
    }else{
      char *zSecret = sha1_shared_secret(blob_str(&pw), g.argv[3], 0);
      db_multi_exec("UPDATE user SET pw=%Q, mtime=now() WHERE uid=%d",
                    zSecret, uid);
      free(zSecret);
    }
  }else if( n>=2 && strncmp(g.argv[2],"capabilities",2)==0 ){
    int uid;
    if( g.argc!=4 && g.argc!=5 ){
      usage("capabilities USERNAME ?PERMISSIONS?");
    }
    uid = db_int(0, "SELECT uid FROM user WHERE login=%Q", g.argv[3]);
    if( uid==0 ){
      fossil_fatal("no such user: %s", g.argv[3]);
    }
    if( g.argc==5 ){
      db_multi_exec(
        "UPDATE user SET cap=%Q, mtime=now() WHERE uid=%d",
        g.argv[4], uid
      );
    }
    fossil_print("%s\n", db_text(0, "SELECT cap FROM user WHERE uid=%d", uid));
  }else{
    fossil_fatal("user subcommand should be one of: "
                 "capabilities default list new password");
  }
}

/*
** Attempt to set the user to zLogin
*/
static int attempt_user(const char *zLogin){
  int uid;

  if( zLogin==0 ){
    return 0;
  }
  uid = db_int(0, "SELECT uid FROM user WHERE login=%Q", zLogin);
  if( uid ){
    g.userUid = uid;
    g.zLogin = mprintf("%s", zLogin);
    return 1;
  }
  return 0;
}

/*
** Figure out what user is at the controls.
**
**   (1)  Use the --user and -U command-line options.
**
**   (2)  If the local database is open, check in VVAR.
**
**   (3)  Check the default user in the repository
**
**   (4)  Try the FOSSIL_USER environment variable.
**
**   (5)  Try the USER environment variable.
**
**   (6)  Try the LOGNAME environment variable.
**
**   (7)  Try the USERNAME environment variable.
**
**   (8)  Check if the user can be extracted from the remote URL.
**
** The user name is stored in g.zLogin.  The uid is in g.userUid.
*/
void user_select(void){
  if( g.userUid ) return;
  if( g.zLogin ){
    if( attempt_user(g.zLogin)==0 ){
      fossil_fatal("no such user: %s", g.zLogin);
    }else{
      return;
    }
  }

  if( g.localOpen && attempt_user(db_lget("default-user",0)) ) return;

  if( attempt_user(db_get("default-user", 0)) ) return;

  if( attempt_user(fossil_getenv("FOSSIL_USER")) ) return;

  if( attempt_user(fossil_getenv("USER")) ) return;

  if( attempt_user(fossil_getenv("LOGNAME")) ) return;

  if( attempt_user(fossil_getenv("USERNAME")) ) return;

  url_parse(0, 0);
  if( g.url.user && attempt_user(g.url.user) ) return;

  fossil_print(
    "Cannot figure out who you are!  Consider using the --user\n"
    "command line option, setting your USER environment variable,\n"
    "or setting a default user with \"fossil user default USER\".\n"
  );
  fossil_fatal("cannot determine user");
}

/*
** COMMAND: test-usernames
**
** Usage: %fossil test-usernames
**
** Print details about sources of fossil usernames.
*/
void test_usernames_cmd(void){
  db_find_and_open_repository(0, 0);

  fossil_print("Initial g.zLogin: %s\n", g.zLogin);
  fossil_print("Initial g.userUid: %d\n", g.userUid);
  fossil_print("checkout default-user: %s\n", g.localOpen ?
               db_lget("default-user","") : "<<no open checkout>>");
  fossil_print("default-user: %s\n", db_get("default-user",""));
  fossil_print("FOSSIL_USER: %s\n", fossil_getenv("FOSSIL_USER"));
  fossil_print("USER: %s\n", fossil_getenv("USER"));
  fossil_print("LOGNAME: %s\n", fossil_getenv("LOGNAME"));
  fossil_print("USERNAME: %s\n", fossil_getenv("USERNAME"));
  url_parse(0, 0);
  fossil_print("URL user: %s\n", g.url.user);
  user_select();
  fossil_print("Final g.zLogin: %s\n", g.zLogin);
  fossil_print("Final g.userUid: %d\n", g.userUid);
}


/*
** COMMAND: test-hash-passwords
**
** Usage: %fossil test-hash-passwords REPOSITORY
**
** Convert all local password storage to use a SHA1 hash of the password
** rather than cleartext.  Passwords that are already stored as the SHA1
** has are unchanged.
*/
void user_hash_passwords_cmd(void){
  if( g.argc!=3 ) usage("REPOSITORY");
  db_open_repository(g.argv[2]);
  sqlite3_create_function(g.db, "shared_secret", 2, SQLITE_UTF8, 0,
                          sha1_shared_secret_sql_function, 0, 0);
  db_multi_exec(
    "UPDATE user SET pw=shared_secret(pw,login), mtime=now()"
    " WHERE length(pw)>0 AND length(pw)!=40"
  );
}

/*
** COMMAND: test-prompt-user
**
** Usage: %fossil test-prompt-user PROMPT
**
** Prompts the user for input and then prints it verbatim (i.e. without
** a trailing line terminator).
*/
void test_prompt_user_cmd(void){
  Blob answer;
  if( g.argc!=3 ) usage("PROMPT");
  prompt_user(g.argv[2], &answer);
  fossil_print("%s\n", blob_str(&answer));
}

/*
** COMMAND: test-prompt-password
**
** Usage: %fossil test-prompt-password PROMPT VERIFY
**
** Prompts the user for a password and then prints it verbatim.
**
** Behavior is controlled by the VERIFY parameter:
**
**     0     Just ask once.
**
**     1     If the first answer is a non-empty string, ask for
**           verification.  Repeat if the two strings do not match.
**
**     2     Ask twice, repeat if the strings do not match.

*/
void test_prompt_password_cmd(void){
  Blob answer;
  int iVerify = 0;
  if( g.argc!=4 ) usage("PROMPT VERIFY");
  iVerify = atoi(g.argv[3]);
  prompt_for_password(g.argv[2], &answer, iVerify);
  fossil_print("[%s]\n", blob_str(&answer));
}

/*
** WEBPAGE: access_log
**
** Show login attempts, including timestamp and IP address.
** Requires Admin privileges.
**
** Query parameters:
**
**    y=N      1: success only.  2: failure only.  3: both (default: 3)
**    n=N      Number of entries to show (default: 200)
**    o=N      Skip this many entries (default: 0)
*/
void access_log_page(void){
  int y = atoi(PD("y","3"));
  int n = atoi(PD("n","200"));
  int skip = atoi(PD("o","0"));
  Blob sql;
  Stmt q;
  int cnt = 0;
  int rc;
  int fLogEnabled;

  login_check_credentials();
  if( !g.perm.Admin ){ login_needed(0); return; }
  create_accesslog_table();


  if( P("delall") && P("delallbtn") ){
    db_multi_exec("DELETE FROM accesslog");
    cgi_redirectf("%s/access_log?y=%d&n=%d&o=%o", g.zTop, y, n, skip);
    return;
  }
  if( P("delanon") && P("delanonbtn") ){
    db_multi_exec("DELETE FROM accesslog WHERE uname='anonymous'");
    cgi_redirectf("%s/access_log?y=%d&n=%d&o=%o", g.zTop, y, n, skip);
    return;
  }
  if( P("delfail") && P("delfailbtn") ){
    db_multi_exec("DELETE FROM accesslog WHERE NOT success");
    cgi_redirectf("%s/access_log?y=%d&n=%d&o=%o", g.zTop, y, n, skip);
    return;
  }
  if( P("delold") && P("deloldbtn") ){
    db_multi_exec("DELETE FROM accesslog WHERE rowid in"
                  "(SELECT rowid FROM accesslog ORDER BY rowid DESC"
                  " LIMIT -1 OFFSET 200)");
    cgi_redirectf("%s/access_log?y=%d&n=%d", g.zTop, y, n);
    return;
  }
  style_header("Access Log");
  blob_zero(&sql);
  blob_append_sql(&sql,
    "SELECT uname, ipaddr, datetime(mtime,toLocal()), success"
    "  FROM accesslog"
  );
  if( y==1 ){
    blob_append(&sql, "  WHERE success", -1);
  }else if( y==2 ){
    blob_append(&sql, "  WHERE NOT success", -1);
  }
  blob_append_sql(&sql,"  ORDER BY rowid DESC LIMIT %d OFFSET %d", n+1, skip);
  if( skip ){
    style_submenu_element("Newer", "%s/access_log?o=%d&n=%d&y=%d",
              g.zTop, skip>=n ? skip-n : 0, n, y);
  }
  rc = db_prepare_ignore_error(&q, "%s", blob_sql_text(&sql));
  fLogEnabled = db_get_boolean("access-log", 0);
  @ <div align="center">Access logging is %s(fLogEnabled?"on":"off").
  @ (Change this on the <a href="setup_settings">settings</a> page.)</div>
  @ <table border="1" cellpadding="5" id="logtable" align="center">
  @ <thead><tr><th width="33%%">Date</th><th width="34%%">User</th>
  @ <th width="33%%">IP Address</th></tr></thead><tbody>
  while( rc==SQLITE_OK && db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zIP = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    int bSuccess = db_column_int(&q, 3);
    cnt++;
    if( cnt>n ){
      style_submenu_element("Older", "%s/access_log?o=%d&n=%d&y=%d",
                  g.zTop, skip+n, n, y);
      break;
    }
    if( bSuccess ){
      @ <tr>
    }else{
      @ <tr bgcolor="#ffacc0">
    }
    @ <td>%s(zDate)</td><td>%h(zName)</td><td>%h(zIP)</td></tr>
  }
  if( skip>0 || cnt>n ){
    style_submenu_element("All", "%s/access_log?n=10000000", g.zTop);
  }
  @ </tbody></table>
  db_finalize(&q);
  @ <hr />
  @ <form method="post" action="%s(g.zTop)/access_log">
  @ <label><input type="checkbox" name="delold">
  @ Delete all but the most recent 200 entries</input></label>
  @ <input type="submit" name="deloldbtn" value="Delete"></input>
  @ </form>
  @ <form method="post" action="%s(g.zTop)/access_log">
  @ <label><input type="checkbox" name="delanon">
  @ Delete all entries for user "anonymous"</input></label>
  @ <input type="submit" name="delanonbtn" value="Delete"></input>
  @ </form>
  @ <form method="post" action="%s(g.zTop)/access_log">
  @ <label><input type="checkbox" name="delfail">
  @ Delete all failed login attempts</input></label>
  @ <input type="submit" name="delfailbtn" value="Delete"></input>
  @ </form>
  @ <form method="post" action="%s(g.zTop)/access_log">
  @ <label><input type="checkbox" name="delall">
  @ Delete all entries</input></label>
  @ <input type="submit" name="delallbtn" value="Delete"></input>
  @ </form>
  output_table_sorting_javascript("logtable", "Ttt", 1);
  style_footer();
}
