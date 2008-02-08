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

/*
** Lists all tags matching the given LIKE clause (which
** may be 0).
*/
static void tagview_page_list_tags(const char *zLike){
  char *zLikeClause = 0;
  const int limit = 10;
  char *zLimit = 0;
  char *zSql;

  if( zLike && zLike[0] ){
    zLikeClause = mprintf( "AND t.tagname LIKE '%%%q%%'", zLike );
    zLimit = "";
    @ <h2>Tags matching [%h(zLikeClause)]:</h2>
  }else{
    zLimit = mprintf( "LIMIT %d", limit );
    zLikeClause = "";
    @ <h2>%d(limit) most recent tags:</h2>
  }
  zSql = mprintf( 
    "SELECT "
    "   linktagid(t.tagid) AS 'Tag ID',"
    "   linktagname(t.tagname) AS 'Name',"
    "   DATETIME(tx.mtime) AS 'Timestamp',"
    "   linkuuid(b.uuid) AS 'Version'"
    "  FROM tag t, tagxref tx, blob b "
    " WHERE t.tagid=tx.tagid AND tx.srcid=b.rid"
    "   AND tx.tagtype!=0 %s "
    TAGVIEW_DEFAULT_FILTER
    " ORDER BY tx.mtime DESC %s",
    zLikeClause, zLimit
  );
  db_generic_query_view(zSql, 1);
  free(zSql);
  if( zLikeClause[0] ) free(zLikeClause);
  if( zLimit[0] ) free(zLimit);
}

/*
** A small search form which forwards to ?like=SEARCH_STRING
*/
static void tagview_page_search_miniform(void){
  char const * like = P("like");
  @ <div style='font-size:smaller'>
  @ <form action='/tagview' method='post'>
  @ Search for tags: 
  @ <input type='text' name='like' value='%h((like?like:""))' size='10'/>
  @ <input type='submit'/>
  @ </form>
  @ </div>
}

/*
** tagview_page_default() renders the default page for tagview_page().
*/
static void tagview_page_default(void){
  tagview_page_list_tags( 0 );
}

/*
** Lists all tags matching the given tagid.
*/
static void tagview_page_tag_by_id( int tagid ){
  char *zSql;
  @ <h2>Tag #%d(tagid):</h2>
  zSql = mprintf( 
    "SELECT DISTINCT"
    "       linktagname(t.tagname) AS 'Tag Name',"
    "       DATETIME(tx.mtime) AS 'Timestamp',"
    "       linkuuid(b.uuid) AS 'Version'"
    "  FROM tag t, tagxref tx, blob b"
    " WHERE t.tagid=%d AND t.tagid=tx.tagid AND tx.srcid=b.rid "
    TAGVIEW_DEFAULT_FILTER
    " ORDER BY tx.mtime DESC",
    tagid
  );
  db_generic_query_view(zSql, 1);
  free(zSql);
}

/*
** Lists all tags matching the given tag name.
*/
static void tagview_page_tag_by_name( char const * tagname ){
  char *zSql;
  @ <h2>Tag '%s(tagname)':</h2>
  zSql = mprintf( 
    "SELECT DISTINCT"
    "       linktagid(t.tagid) AS 'Tag ID',"
    "       DATETIME(tx.mtime) AS 'Timestamp',"
    "       linkuuid(b.uuid) AS 'Version'"
    "  FROM tag t, tagxref tx, blob b "
    " WHERE t.tagname='%q' AND t.tagid=tx.tagid AND tx.srcid=b.rid "
    TAGVIEW_DEFAULT_FILTER
    " ORDER BY tx.mtime DESC",
    tagname);
  db_generic_query_view(zSql, 1);
  free(zSql);
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
  if( 0 != (check = P("tagid")) ){
    tagview_page_tag_by_id( atoi(check) );
  }else if( 0 != (check = P("like")) ){
    tagview_page_list_tags( check );
  }else if( 0 != (check = P("name")) ){
    tagview_page_tag_by_name( check );
  }else{
    tagview_page_default();
  }
  style_footer();
}

#undef TAGVIEW_DEFAULT_FILTER
