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
#include "admin_sql_page.h"


static void admin_sql_page_form()
{
  char const * sql = P("sql");
  @ <hr/><h2>SQL:</h2>
  @ <span class='achtung'>You can enter arbitrary SQL here, to execute
  @ against the repo database.
  @ With great power comes great responsibility...</span><br/>
  @ <form action='' method='post'>
  @ <textarea style='border:2px solid black' name='sql' cols='80' rows='5'>%s(sql?sql:"")</textarea>
  @ <br/><input type='submit' name='sql_submit'/> <input type='reset'/>
  @ </form>
}

/*
** WEBPAGE: /admin/sql
*/
void admin_sql_page(void){
  /**
     TODOs: refactor db_generic_query_view() to use sqlite3 directly,
     instead of the db_prepare()/db_step() API, so we can do our own
     handling. Currently SQL-level failures will cause the page to
     immediately stop (e.g. page footer won't get a chance to render).
  */
  login_check_credentials();
  style_header("Admin SQL");
  if( !g.okAdmin ){
    @ <strong>Access Denied!</strong> You must be an Admin to use this tool.
    style_footer();
    return;
  }
  admin_sql_page_form();
  char const * sql = P("sql");
  if( sql )
  {
    db_generic_query_view( sql, 0, 0 );
  }
  style_footer();
}
