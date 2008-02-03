/*
** Copyright (c) 2007 D. Richard Hipp
** Copyright (c) 2008 Stephan Beal
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
** Implementation of the Tag View page
*/
#include <assert.h>
#include "config.h"
#include "my_page.h"

/**
Renders a logout button.
*/
static void mypage_logout_button()
{
  if( g.zLogin ){
    @ <br clear="both"/><hr/>
    @ <strong>Logout (or "log out", if you prefer):</strong><br/>
    @ <form action='login' method='POST'>
    @ <p>To log off the system (and delete your login cookie)
    @  press the following button:<br>
    @ <input type="submit" name="out" value="Logout"/></p>
    @ </form>
  }
}

/**
Renders a password changer.
*/
static void mypage_password_changer()
{
  if( g.okPassword ){
    @ <br clear="both"/><hr/>
    @ <strong>Change Password:</strong><br/>
    @ <p>To change your password, enter your old password and your
    @ new password twice below then press the "Change Password"
    @ button.</p>
    @ <form action="login" method="POST">
    @ <input type='hidden' name='g' value='my'/>
    @ <table><tbody>
    @ <tr><td align="right">Old Password:</td>
    @ <td><input type="password" name="p" size=30></td></tr>
    @ <tr><td align="right">New Password:</td>
    @ <td><input type="password" name="n1" size=30></td></tr>
    @ <tr><td align="right">Repeat New Password:</td>
    @ <td><input type="password" name="n2" size=30></td></tr>
    @ <tr><td></td>
    @ <td><input type="submit" value="Change Password"></td></tr>
    @ </tbody></table>
    @ </form>
  }

}

/**
Default page rendered for /my.
*/
static void mypage_page_default()
{
  int uid = g.userUid;
  char * sql = mprintf( "SELECT login,cap,info FROM user WHERE uid=%d",
			uid );
  Stmt st;
  db_prepare( &st, sql );
  free( sql );
  db_step(&st);
  char const * uname = db_column_text( &st, 0 );
  char const * ucap = db_column_text( &st, 1 );
  char const * uinfo = db_column_text( &st, 2 );

  @ <h2>Welcome, %s(uname)!</h2>
  @ Your user ID is: %d(uid)<br/>
  @ Your Fossil permissions are: [%s(ucap)]<br/>
  @ Your additional info: [%s(uinfo)]<br/>
  mypage_logout_button();
  mypage_password_changer();

  @ <hr/><h2>TODOs:</h2><ul>
  @ <li>Change "additional info" field.</li>
  @ <li>Search for changes made by you.</li>
  @ <li>Search for files/wiki pages/tickets related to you.</li>
  @ <li>Allow per-user setup of the page (e.g. reports).</li>
  @ <li>... the list goes on ...</li>
  @ </ul>

  db_finalize( &st );

}

/*
** WEBPAGE: /my
*/
void mypage_page(void){
  login_check_credentials();
  if( !g.okRdWiki ){
    login_needed();
  }
  style_header("Your Home");
  char const * name = P("name");
  if( name )
  {
    if( 0 == strcmp(name,"tickets") )
    {
      @ TODO: Tickets page.
    }
    else
    {
      @ TODO: handle /my/%s(name)
    }
  }
  else
  {
    mypage_page_default();
  }
  style_footer();
}
