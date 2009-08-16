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
    case SQLITE_READ: {
      if( strcmp(zArg2,"pw")==0 ){
        rc = SQLITE_IGNORE;
      }else{
        rc = SQLITE_OK;
      }
      break;
    }
    case SQLITE_FUNCTION:
    case SQLITE_SELECT: {
      rc = SQLITE_OK;
      break;
    }
  }
  return rc;
}

/*
** WEBPAGE: admin_sql
*/
void admin_sql_page(void){
  const char *zSql = PD("sql","");
  login_check_credentials();
  if( !g.okAdmin ){
    login_needed();
    return;
  }
  style_header("Admin SQL");
  @ <h2>SQL:</h2>
  @ You can enter only SELECT statements here, and some SQL-side functions
  @ are also restricted.<br/>
  @ <form action='' method='post'>
  login_insert_csrf_secret();
  @ <textarea style='border:2px solid black' name='sql'
  @  cols='80' rows='5'>%h(zSql)</textarea>
  @ <br/><input type='submit' name='sql_submit'/> <input type='reset'/>
  @ </form>
  if( zSql[0] ){
    login_verify_csrf_secret();
    sqlite3_set_authorizer(g.db, selectOnly, 0);
    db_generic_query_view(zSql, 0);
    sqlite3_set_authorizer(g.db, 0, 0);
  }
  style_footer();
}
