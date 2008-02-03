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
tagview_strxform_f is a typedef for funcs with the following policy:

They accept a const string which they then transform into some other
form. They return a transformed copy, which the caller is responsible
for freeing.

The intention of this is to provide a way for a generic query routine
to format specific column data (e.g. transform an object ID into a
link to that object).
*/
typedef char * (*tagview_strxform_f)( char const * );

#if 0
/** A no-op transformer which can be used as a placeholder. */
static char * tagview_xf_copy( char const * uuid )
{
  int len = strlen(uuid) + 1;
  char * ret = (char *) malloc( len );
  ret[len] = '\0';
  strncpy( ret, uuid, len-1 );
  return ret;
}
#endif

/** Returns a hyperlink to uuid. */
static char * tagview_xf_link_to_uuid( char const * uuid )
{
  const int offset = 10;
  char shortname[offset+1];
  shortname[offset] = '\0';
  memcpy( shortname, uuid, offset );
  return mprintf( "<tt><a href='%s/vinfo/%s'><span style='font-size:1.5em'>%s</span>%s</a></tt>",
                  g.zBaseURL, uuid, shortname, uuid+offset );
}

/** Returns a hyperlink to the given tag. */
static char * tagview_xf_link_to_tagid( char const * tagid )
{
  return mprintf( "<a href='%s/tagview?tagid=%s'>%s</a>",
                  g.zBaseURL, tagid, tagid );
}

/** Returns a hyperlink to the named tag. */
static char * tagview_xf_link_to_tagname( char const * tagid )
{
  return mprintf( "<a href='%s/tagview/%s'>%s</a>",
                  g.zBaseURL, tagid, tagid );
}



/**
* tagview_run_query():
*
* A very primitive helper to run an SQL query and table-ize the
* results.
*
* The sql parameter should be a single, complete SQL statement.
*
* The coln parameter is optional (it may be 0). If it is 0 then the
* column names used in the output will be taken directly from the
* SQL. If it is not null then it must have as many entries as the SQL
* result has columns. Each entry is a column name for the SQL result
* column of the same index. Any given entry may be 0, in which case
* the column name from the SQL is used.
*
* The xform argument is an array of transformation functions (type
* tagview_strxform_f). The array, or any single entry, may be 0, but
* if the array is non-0 then it must have at least as many entries as
* colnames does. Each index corresponds directly to an entry in
* colnames and the SQL results.  Any given entry may be 0. If it has
* fewer, undefined behaviour results.  If a column has an entry in
* xform, then the xform function will be called to transform the
* column data before rendering it. This function takes care of freeing
* the strings created by the xform functions.
*
* Example:
*
*  char const * const colnames[] = {
*   "Tag ID", "Tag Name", "Something Else", "UUID"
*  };
*  tagview_strxform_f xf[] = {
*    tagview_xf_link_to_tagid,
*    tagview_xf_link_to_tagname,
*    0,
*    tagview_xf_link_to_uuid
*  };
*  tagview_run_query( "select a,b,c,d from foo", colnames, xf );
*
*/
static void tagview_run_query(
  char const * sql,
  char const * const * coln,
  tagview_strxform_f * xform )
{
  
  Stmt st;
  @ <table cellpadding='4px' border='1'><tbody>
  int i = 0;
  int rc = db_prepare( &st, sql );
  /**
    Achtung: makeheaders apparently can't pull the function
    name from this:
   if( SQLITE_OK != db_prepare( &st, sql ) )
  */
  if( SQLITE_OK != rc )
  {
    @ tagview_run_query(): Error processing SQL: [%s(sql)]
    return;
  }
  int colc = db_column_count(&st);
  @ <tr>
  for( i = 0; i < colc; ++i ) {
    if( coln )
    {
      @ <th>%s(coln[i] ? coln[i] : db_column_name(&st,i))</th>
    }
    else
    {
      @ <td>%s(db_column_name(&st,i))</td>
    }
  }
  @ </tr>

  while( SQLITE_ROW == db_step(&st) ){
    @ <tr>
      for( i = 0; i < colc; ++i ) {
        char * xf = 0;
        char const * xcf = 0;
        xcf = (xform && xform[i])
          ? (xf=(xform[i])(db_column_text(&st,i)))
          : db_column_text(&st,i);
        @ <td>%s(xcf)</td>
        if( xf ) free( xf );
      }
    @ </tr>
  }
  db_finalize( &st );
  @ </tbody></table>
}

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
  tagview_strxform_f xf[] = {
    tagview_xf_link_to_tagid,
    tagview_xf_link_to_tagname,
    0,
    tagview_xf_link_to_uuid
  };
  tagview_run_query( sql, colnames, xf );
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
  tagview_strxform_f xf[] = {
      tagview_xf_link_to_tagname,
      0,
      tagview_xf_link_to_uuid
  };
  tagview_run_query( sql, colnames, xf );
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
  tagview_strxform_f xf[] = {
      tagview_xf_link_to_tagid,
      0,
      tagview_xf_link_to_uuid
  };
  tagview_run_query( sql, colnames, xf );
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
