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
** Implementation of the Admin SQL
*/
#include <assert.h>
#include "config.h"
#include "admin.h"

/*
** This SQLite authorizer callback prevents any SQL other than
** SELECT statements from running.
*/
static int selectOnly(
  void *NotUsed,           /* Application data - not used */
  int type,                /* Operation type */
  const char *zArg1,       /* Arguments.... */
  const char *zArg2,
  const char *zArg3,
  const char *zArg4
){
  int rc = SQLITE_DENY;
  switch( type ){
    case SQLITE_READ:
    case SQLITE_SELECT: {
      rc = SQLITE_OK;
      break;
    }
  }
  return rc;
}


void admin_prepare_submenu(){
  if( g.okAdmin ){
    style_submenu_element("Main", "Main admin page", "%s/admin", g.zTop );
    style_submenu_element("SQL", "SQL page", "%s/admin/sql", g.zTop );
    style_submenu_element("Setup", "Setup page", "%s/setup", g.zTop );
  }
}


/*
** WEBPAGE: /admin/sql
*/
void admin_sql_page(void){
  const char *zSql = PD("sql","");
  login_check_credentials();
  if( !g.okAdmin ){
    login_needed();
    return;
  }
  admin_prepare_submenu();
  style_header("Admin SQL");
  @ <h2>SQL:</h2>
  @ You can enter only SELECT statements here, and some SQL-side functions
  @ are also restricted.<br/>
  @ <form action='' method='post'>
  @ <textarea style='border:2px solid black' name='sql'
  @  cols='80' rows='5'>%h(zSql)</textarea>
  @ <br/><input type='submit' name='sql_submit'/> <input type='reset'/>
  @ </form>
  if( zSql[0] ){
    sqlite3_set_authorizer(g.db, selectOnly, 0);
    db_generic_query_view(zSql, 0);
    sqlite3_set_authorizer(g.db, 0, 0);
  }
  style_footer();
}

/*
** WEBPAGE: /admin
*/
void admin_page(void){
  login_check_credentials();
  if( !g.okAdmin ){
    login_needed();
    return;
  }
  if( g.zExtra && g.zExtra[0] ){
    if(g.zExtra == strstr(g.zExtra,"sql")) admin_sql_page();
    /* FIXME: ^^^ this ^^^ is an awful lot of work, especially once
    ** the paths deepen. Figure out a way to simplify dispatching.
    */
    return;
  }
  admin_prepare_submenu();
  style_header("Admin");
  @ <h2>Links:</h2>
  @ <ul>
  @ <li><a href='%s(g.zBaseURL)/setup'>Fossil WWW Setup</a></li>
  @ <li><a href='%s(g.zBaseURL)/admin/sql'>Run SQL queries</a></li>
  @ </ul>
  style_footer();
}
