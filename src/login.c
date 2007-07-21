/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License as published by the Free Software Foundation; either
** version 2 of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code for generating the login and logout screens.
*/
#include "config.h"
#include "login.h"
#include <time.h>

/*
** Return the name of the login cookie
*/
static char *login_cookie_name(void){
  return "fossil_login";
}

/*
** WEBPAGE: /login
** WEBPAGE: /logout
**
** Generate the login page
*/
void login_page(void){
  const char *zUsername, *zPasswd, *zGoto;
  const char *zNew1, *zNew2;
  char *zErrMsg = "";

  login_check_credentials();
  zUsername = P("u");
  zPasswd = P("p");
  zGoto = PD("g","index");
  if( P("out")!=0 ){
    const char *zCookieName = login_cookie_name();
    cgi_set_cookie(zCookieName, "", 0, -86400);
    cgi_redirect(zGoto);
  }
  if( !g.isAnon && zPasswd && (zNew1 = P("n1"))!=0 && (zNew2 = P("n2"))!=0 ){
    if( db_int(1, "SELECT 0 FROM user"
                  " WHERE uid=%d AND pw=%Q", g.userUid, zPasswd) ){
      sleep(1);
      zErrMsg = 
         @ <p><font color="red">
         @ You entered an incorrect old password while attempting to change
         @ your password.  Your password is unchanged.
         @ </font></p>
      ;
    }else if( strcmp(zNew1,zNew2)!=0 ){
      zErrMsg = 
         @ <p><font color="red">
         @ The two copies of your new passwords do not match.
         @ Your password is unchanged.
         @ </font></p>
      ;
    }else{
      db_multi_exec(
         "UPDATE user SET pw=%Q WHERE uid=%d", zNew1, g.userUid
      );
      cgi_redirect("index");
      return;
    }
  }
  if( zUsername!=0 && zPasswd!=0 && strcmp(zUsername,"anonymous")!=0 ){
    int uid = db_int(0,
        "SELECT uid FROM user"
        " WHERE login=%Q AND pw=%B", zUsername, zPasswd);
    if( uid<=0 ){
      sleep(1);
      zErrMsg = 
         @ <p><font color="red">
         @ You entered an unknown user or an incorrect password.
         @ </font></p>
      ;
    }else{
      char *zCookie;
      const char *zCookieName = login_cookie_name();
      const char *zIpAddr = PD("REMOTE_ADDR","x");
      const char *zExpire = db_get("cookie-expire","8766");
      int expires;

      zCookie = db_text(0, "SELECT '%d/' || hex(randomblob(25))", uid);
      expires = atoi(zExpire)*3600;
      cgi_set_cookie(zCookieName, zCookie, 0, expires);
      db_multi_exec(
        "UPDATE user SET cookie=%Q, ipaddr=%Q, "
        "  cexpire=julianday('now')+%d/86400.0 WHERE uid=%d",
        zCookie, zIpAddr, expires, uid
      );
      cgi_redirect(zGoto);
    }
  }
  style_header();
  @ %s(zErrMsg)
  @ <form action="login" method="POST">
  if( P("g") ){
    @ <input type="hidden" name="nxp" value="%h(P("g"))">
  }
  @ <table align="left" hspace="10">
  @ <tr>
  @   <td align="right">User ID:</td>
  @   <td><input type="text" name="u" value="" size=30></td>
  @ </tr>
  @ <tr>
  @  <td align="right">Password:</td>
  @   <td><input type="password" name="p" value="" size=30></td>
  @ </tr>
  @ <tr>
  @   <td></td>
  @   <td><input type="submit" name="in" value="Login"></td>
  @ </tr>
  @ </table>
  if( g.isAnon || g.zLogin==0 || g.zLogin[0]==0 ){
    @ <p>To login
  }else{
    @ <p>You are current logged in as <b>%h(g.zLogin)</b></p>
    @ <p>To change your login to a different user
  }
  @ enter the user-id and password at the left and press the
  @ "Login" button.  Your user name will be stored in a browser cookie.
  @ You must configure your web browser to accept cookies in order for
  @ the login to take.</p>
  if( db_exists("SELECT uid FROM user WHERE login='anonymous'") ){
    @ <p>This server is configured to allow limited access to users
    @ who are not logged in.</p>
  }
  if( !g.isAnon ){
    @ <br clear="both"><hr>
    @ <p>To log off the system (and delete your login cookie)
    @  press the following button:<br>
    @ <input type="submit" name="out" value="Logout"></p>
  }
  @ </form>
  if( !g.isAnon ){
    @ <br clear="both"><hr>
    @ <p>To change your password, enter your old password and your
    @ new password twice below then press the "Change Password"
    @ button.</p>
    @ <form action="login" method="POST">
    @ <table>
    @ <tr><td align="right">Old Password:</td>
    @ <td><input type="password" name="p" size=30></td></tr>
    @ <tr><td align="right">New Password:</td>
    @ <td><input type="password" name="n1" size=30></td></tr>
    @ <tr><td align="right">Repeat New Password:</td>
    @ <td><input type="password" name="n2" size=30></td></tr>
    @ <tr><td></td>
    @ <td><input type="submit" value="Change Password"></td></tr>
    @ </table>
    @ </form>
  }
  style_footer();
}



/*
** This routine examines the login cookie to see if it exists and
** and is valid.  If the login cookie checks out, it then sets 
** g.zUserUuid appropriately.
**
*/
void login_check_credentials(void){
  int uid = 0;
  const char *zCookie;
  const char *zRemoteAddr;
  const char *zCap = 0;

  /* Only run this check once.  */
  if( g.zLogin!=0 ) return;


  /* If the HTTP connection is coming over 127.0.0.1 and if
  ** local login is disabled, then there is no need to check
  ** user credentials.
  */
  zRemoteAddr = PD("REMOTE_ADDR","nil");
  if( strcmp(zRemoteAddr, "127.0.0.1")==0
        && db_get_int("authenticate-localhost",1)==0 ){
    uid = db_int(0, "SELECT uid FROM user WHERE cap LIKE '%s%'");
    g.zLogin = db_text("?", "SELECT login FROM user WHERE uid=%d", uid);
    zCap = "s";
    g.noPswd = 1;
  }

  /* Check the login cookie to see if it matches a known valid user.
  */
  if( uid==0 && (zCookie = P(login_cookie_name()))!=0 ){
    uid = db_int(0, 
            "SELECT 1 FROM user"
            " WHERE uid=%d"
            "   AND cookie=%Q"
            "   AND ipaddr=%Q"
            "   AND cexpire>julianday('now')",
            atoi(zCookie), zCookie, zRemoteAddr
         );
  }

  if( uid==0 ){
    g.zLogin = "";
    zCap = db_get("nologin-cap","onrj");
  }else if( zCap==0 ){
    Stmt s;
    db_prepare(&s, "SELECT login, cap FROM user WHERE uid=%d", uid);
    db_step(&s);
    g.zLogin = db_column_malloc(&s, 0);
    zCap = db_column_malloc(&s, 1);
    db_finalize(&s);
  }
  g.userUid = uid;

  login_set_capabilities(zCap);
}

/*
** Set the global capability flags based on a capability string.
*/
void login_set_capabilities(const char *zCap){
  int i;
  for(i=0; zCap[i]; i++){
    switch( zCap[i] ){
      case 's':   g.okSetup = g.okDelete = 1;
      case 'a':   g.okAdmin = g.okRdTkt = g.okWrTkt = g.okQuery =
                              g.okRdWiki = g.okWrWiki =
                              g.okNewTkt = g.okPassword = 1;
      case 'i':   g.okRead = g.okWrite = 1;                     break;
      case 'd':   g.okDelete = 1;                               break;
      case 'j':   g.okRdWiki = 1;                               break;
      case 'k':   g.okWrWiki = g.okRdWiki = 1;                  break;
      case 'n':   g.okNewTkt = 1;                               break;
      case 'o':   g.okRead = 1;                                 break;
      case 'p':   g.okPassword = 1;                             break;
      case 'q':   g.okQuery = 1;                                break;
      case 'r':   g.okRdTkt = 1;                                break;
      case 'w':   g.okWrTkt = g.okRdTkt = g.okNewTkt = 1;       break;
    }
  }
}

/*
** Call this routine when the credential check fails.  It causes
** a redirect to the "login" page.
*/
void login_needed(void){
  const char *zUrl = PD("REQUEST_URI", "index");
  cgi_redirect(mprintf("login?nxp=%T", zUrl));
  /* NOTREACHED */
  assert(0);
}
