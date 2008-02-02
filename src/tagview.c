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
    @ <a href="%s(zLink)">%h(zTitle)</a>
  }else{
    @ %h(zTitle)
  }
  @ </td><td valign="top">%h(zDesc)</td></tr>
}

/*
** WEBPAGE: /tagview
*/
void tagview_page(void){
  Stmt st;

  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }
  style_header("Tags List");
  @ <table cellpadding='4px' border='1'><tbody>
  @ <tr><th>Tag name</th><th>Timestamp</th><th>Version</th></tr>
  db_prepare( &st,
	      "select t.tagname, DATETIME(tx.mtime), b.uuid "
	      "FROM tag t, tagxref tx, blob b "
	      "WHERE t.tagid=tx.tagid and tx.rid=b.rid "
	      "AND tx.tagtype != 0 "
	      "ORDER BY tx.mtime DESC"
	      );
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
	  @ <td><tt>%s(tagname)</tt></td>
	  @ <td align='center'><tt>%s(tagtime)</tt></td>
	  @ <td><tt>
          @ <a href='/vinfo/%s(uuid)'><strong>%s(shortname)</strong>%s(uuid+offset)</a></tt>
	  @ </td></tr>
  }
  db_finalize( &st );
  @ </tbody></table>
  @ <hr/>TODOs include:
  @ <ul>
  @  <li>Page through long tags lists.</li>
  @  <li>Format the timestamp field.</li>
  @  <li>Allow different sorting.</li>
  @  <li>?</li>
  @ </ul>
  style_footer();
}

