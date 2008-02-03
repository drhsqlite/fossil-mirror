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
#include "tagview.h"


/*
** Output a single entry for a menu generated using an HTML table.
** If zLink is not NULL or an empty string, then it is the page that
** the menu entry will hyperlink to.  If zLink is NULL or "", then
** the menu entry has no hyperlink - it is disabled.
*/
void tagview_menu_entry(
  const char *zTitle,
  const char *zLink,
  const char *zDesc
){
  @ <tr><td valign="top" align="right">
  if( zLink && zLink[0] ){
    @ <a href="%s(g.zBaseURL)/%s(zLink)">%h(zTitle)</a>
  }else{
    @ %h(zTitle)
  }
  @ </td><td valign="top">%h(zDesc)</td></tr>
}

static void tagview_page_list_tags( char const * like )
{
  Stmt st;
  char * likeclause = 0;
  const int limit = 10;
  char * limitstr = 0;
  if( like && strlen(like) )
  {
    likeclause = mprintf( "AND t.tagname LIKE '%%%%%q%%%%'", like );
    @ <h2>Tags matching [%s(likeclause)]:</h2>
  }
  else
  {
    limitstr = mprintf( "LIMIT %d", limit );
    @ <h2>%d(limit) most recent tags:</h2>
  }
  @ <table cellpadding='4px' border='1'><tbody>
  @ <tr>
  @ <th>Tag ID</th>
  @ <th>Tag name</th>
  @ <th>Timestamp</th>
  @ <th>Version</th>
  @ </tr>
  char * sql = mprintf( 
    "SELECT t.tagid, t.tagname, DATETIME(tx.mtime), b.uuid "
    "FROM tag t, tagxref tx, blob b "
    "WHERE (t.tagid=tx.tagid) and (tx.srcid=b.rid) "
    "AND (tx.tagtype != 0) %s "
    "ORDER BY tx.mtime DESC %s",
    likeclause ? likeclause : " ",
    limitstr ? limitstr : " "
    );
  db_prepare( &st, sql );
  if( likeclause ) free( likeclause );
  free( sql );
  while( SQLITE_ROW == db_step(&st) ){
    int tagid = db_column_int( &st, 0 );
    char const * tagname = db_column_text( &st, 1 );
    char const * tagtime = db_column_text( &st, 2 );
    char const * uuid = db_column_text( &st, 3 );
    const int offset = 10;
    char shortname[offset+1];
    shortname[offset] = '\0';
    memcpy( shortname, uuid, offset );
    @ <tr>
    @ <td><tt>
    @ <a href='%s(g.zBaseURL)/tagview?tagid=%d(tagid)'>%d(tagid)</a>
    @ </tt></td>
    @ <td><tt><a href='%s(g.zBaseURL)/tagview/%q(tagname)'>%s(tagname)</a></tt></td>
    @ <td align='center'><tt>%s(tagtime)</tt></td>
    @ <td><tt>
    @ <a href='%s(g.zBaseURL)/vinfo/%s(uuid)'><strong>%s(shortname)</strong>%s(uuid+offset)</a>
    @ </tt></td></tr>
  }
  db_finalize( &st );
  @ </tbody></table>
  @ <hr/>TODOs include:
  @ <ul>
  @  <li>Page through long tags lists.</li>
  @  <li>Refactor the internal report routines to be reusable.</li>
  @  <li>Allow different sorting.</li>
  @  <li>Selectively filter out wiki/ticket/baseline</li>
  @  <li>?</li>
  @ </ul>

}

static void tagview_page_search_miniform(void){
  char const * like = P("like");
  @ <div style='font-size:smaller'>
  @ <form action='/tagview' method='post'>
  @ Search for tags: 
  @ <input type='text' name='like' value='%s((like?like:""))' size='10'/>
  @ <input type='submit'/>
  @ </form>
  @ </div>
}


static void tagview_page_default(void){
  tagview_page_list_tags( 0 );
}

static void tagview_page_tag_by_id( int tagid )
{
  Stmt st;
  char * sql = mprintf( 
    "SELECT DISTINCT (t.tagname), DATETIME(tx.mtime), b.uuid "
    "FROM tag t, tagxref tx, blob b "
    "WHERE (t.tagid=%d) AND (t.tagid=tx.tagid) AND (tx.srcid=b.rid) "
    "ORDER BY tx.mtime DESC",
  tagid);
  db_prepare( &st, sql );
  free( sql );
  @ <h2>Tag ID %d(tagid):</h2>
  @ <table cellpadding='4px' border='1'><tbody>
  @ <tr><th>Tag name</th><th>Timestamp</th><th>Version</th></tr>
  while( SQLITE_ROW == db_step(&st) )
  {
    char const * tagname = db_column_text( &st, 0 );
    char const * tagtime = db_column_text( &st, 1 );
    char const * uuid = db_column_text( &st, 2 );
    const int offset = 10;
    char shortname[offset+1];
    shortname[offset] = '\0';
    memcpy( shortname, uuid, offset );
    @ <tr>
    @ <td><tt><a href='%s(g.zBaseURL)/tagview/%q(tagname)'>%s(tagname)</a></tt></td>
    @ <td align='center'><tt>%s(tagtime)</tt></td>
    @ <td><tt>
    @ <a href='%s(g.zBaseURL)/vinfo/%s(uuid)'><strong>%s(shortname)</strong>%s(uuid+offset)</a>
    @ </tt></td></tr>
  }
  @ </tbody></table>
  db_finalize( &st );
}

static void tagview_page_tag_by_name( char const * tagname )
{
  Stmt st;
  char * sql = mprintf( 
    "SELECT DISTINCT t.tagid, DATETIME(tx.mtime), b.uuid "
    "FROM tag t, tagxref tx, blob b "
    "WHERE (t.tagname='%q') AND (t.tagid=tx.tagid) AND (tx.srcid=b.rid) "
    "ORDER BY tx.mtime DESC",
    tagname);
  db_prepare( &st, sql );
  free( sql );
  @ <h2>Tag '%s(tagname)':</h2>
  @ <table cellpadding='4px' border='1'><tbody>
  @ <tr><th>Tag ID</th><th>Timestamp</th><th>Version</th></tr>
  while( SQLITE_ROW == db_step(&st) )
  {
    int tagid = db_column_int( &st, 0 );
    char const * tagtime = db_column_text( &st, 1 );
    char const * uuid = db_column_text( &st, 2 );
    const int offset = 10;
    char shortname[offset+1];
    shortname[offset] = '\0';
    memcpy( shortname, uuid, offset );
    @ <tr>
    @ <td><tt><a href='%s(g.zBaseURL)/tagview?tagid=%d(tagid)'>%d(tagid)</a></tt></td>
    @ <td align='center'><tt>%s(tagtime)</tt></td>
    @ <td><tt>
    @ <a href='%s(g.zBaseURL)/vinfo/%s(uuid)'><strong>%s(shortname)</strong>%s(uuid+offset)</a>
    @ </tt></td></tr>
  }
  @ </tbody></table>
  db_finalize( &st );
}


/*
** WEBPAGE: /tagview
*/
void tagview_page(void){

  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }
  style_header("Tags");
  tagview_page_search_miniform();
  @ <hr/>
  char const * check = 0;
  if( 0 != (check = P("tagid")) )
  {
    tagview_page_tag_by_id( atoi(check) );
  }
  else if( 0 != (check = P("like")) )
  {
    tagview_page_list_tags( check );
  }
  else if( 0 != (check = P("name")) )
  {
    tagview_page_tag_by_name( check );
  }
  else
  {
    tagview_page_default();
  }
  style_footer();
}
