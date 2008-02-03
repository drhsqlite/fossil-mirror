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


#if 1
#  define TAGVIEW_DEFAULT_FILTER "AND t.tagname NOT GLOB 'wiki-*' "
#else
#  define TAGVIEW_DEFAULT_FILTER
#endif

/**
  Lists all tags matching the given LIKE clause (which
may be 0).
*/
static void tagview_page_list_tags( char const * like )
{
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
  char * sql = mprintf( 
    "SELECT t.tagid, t.tagname, DATETIME(tx.mtime), b.uuid "
    "FROM tag t, tagxref tx, blob b "
    "WHERE (t.tagid=tx.tagid) and (tx.srcid=b.rid) "
    "AND (tx.tagtype != 0) %s "
    TAGVIEW_DEFAULT_FILTER
    "ORDER BY tx.mtime DESC %s",
    likeclause ? likeclause : " ",
    limitstr ? limitstr : " "
    );
  if( limitstr ) free(limitstr);
  if( likeclause ) free(likeclause);
  char const * const colnames[] = {
    "Tag ID", "Name", "Timestamp", "Version"
  };
  string_unary_xform_f xf[] = {
    strxform_link_to_tagid,
    strxform_link_to_tagname,
    0,
    strxform_link_to_uuid
  };
  db_generic_query_view( sql, colnames, xf );
  free( sql );
}

/**
A small search form which forwards to ?like=SEARCH_STRING
*/
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

/**
 tagview_page_default() renders the default page for tagview_page().
*/
static void tagview_page_default(void){
  tagview_page_list_tags( 0 );
}

/**
  Lists all tags matching the given tagid.
*/
static void tagview_page_tag_by_id( int tagid )
{
  @ <h2>Tag #%d(tagid):</h2>
  char * sql = mprintf( 
    "SELECT DISTINCT (t.tagname), DATETIME(tx.mtime), b.uuid "
    "FROM tag t, tagxref tx, blob b "
    "WHERE (t.tagid=%d) AND (t.tagid=tx.tagid) AND (tx.srcid=b.rid) "
    TAGVIEW_DEFAULT_FILTER
    "ORDER BY tx.mtime DESC",
  tagid);
  char const * const colnames[] = {
      "Tag Name", "Timestamp", "Version"
  };
  string_unary_xform_f xf[] = {
      strxform_link_to_tagname,
      0,
      strxform_link_to_uuid
  };
  db_generic_query_view( sql, colnames, xf );
  free(sql);
}

/**
  Lists all tags matching the given tag name.
*/
static void tagview_page_tag_by_name( char const * tagname )
{
  @ <h2>Tag '%s(tagname)':</h2>
  char * sql = mprintf( 
    "SELECT DISTINCT t.tagid, DATETIME(tx.mtime), b.uuid "
    "FROM tag t, tagxref tx, blob b "
    "WHERE (t.tagname='%q') AND (t.tagid=tx.tagid) AND (tx.srcid=b.rid) "
    TAGVIEW_DEFAULT_FILTER
    "ORDER BY tx.mtime DESC",
    tagname);
  char const * const colnames[] = {
      "Tag ID", "Timestamp", "Version"
  };
  string_unary_xform_f xf[] = {
      strxform_link_to_tagid,
      0,
      strxform_link_to_uuid
  };
  db_generic_query_view( sql, colnames, xf );
  free( sql );
}


/*
** WEBPAGE: /tagview
*/
void tagview_page(void){
  login_check_credentials();
  if( !g.okRdWiki ){
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

#undef TAGVIEW_DEFAULT_FILTER
