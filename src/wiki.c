/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
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
** This file contains code to do formatting of wiki text.
*/
#include <assert.h>
#include "config.h"
#include "wiki.h"


/*
** Create a fake replicate of the "vfile" table as a TEMP table
** using the manifest identified by manid.
*/
static void create_fake_vfile(int manid){
  static const char zVfileDef[] = 
    @ CREATE TEMP TABLE vfile(
    @   id INTEGER PRIMARY KEY,     -- ID of the checked out file
    @   vid INTEGER REFERENCES blob, -- The version this file is part of.
    @   chnged INT DEFAULT 0,       -- 0:unchnged 1:edited 2:m-chng 3:m-add
    @   deleted BOOLEAN DEFAULT 0,  -- True if deleted 
    @   rid INTEGER,                -- Originally from this repository record
    @   mrid INTEGER,               -- Based on this record due to a merge
    @   pathname TEXT,              -- Full pathname
    @   UNIQUE(pathname,vid)
    @ );
    ;
  db_multi_exec(zVfileDef);
  load_vfile_from_rid(manid);
}

/*
** Locate the wiki page with the name zPageName and render it.
*/
static void locate_and_render_wikipage(const char *zPageName){
  Stmt q;
  int id = 0;
  int rid = 0;
  int chnged = 0;
  char *zPathname = 0;
  db_prepare(&q,
     "SELECT id, rid, chnged, pathname FROM vfile"
     " WHERE (pathname='%q.wiki' OR pathname LIKE '%%/%q.wiki')"
     "   AND NOT deleted", zPageName, zPageName
  );
  if( db_step(&q)==SQLITE_ROW ){
    id = db_column_int(&q, 0);
    rid = db_column_int(&q, 1);
    chnged = db_column_int(&q, 2);
    if( chnged || rid==0 ){
      zPathname = db_column_malloc(&q, 3);
    }
  }
  db_finalize(&q);
  if( id ){
    Blob page, src;
    char *zTitle = "wiki";
    char *z;
    blob_zero(&src);
    if( zPathname ){
      zPathname = mprintf("%s/%z", g.zLocalRoot, zPathname);
      blob_read_from_file(&src, zPathname);
      free(zPathname);
    }else{
      content_get(rid, &src);
    }

    /* The wiki page content is now in src.  Check to see if
    ** there is a <readonly/> or <appendonly/> element at the
    ** beginning of the content.
    */
    z = blob_str(&src);
    while( isspace(*z) ) z++;
    if( strncmp(z, "<readonly/>", 11)==0 ){
      z += 11;
    }else if( strncmp(z, "<appendonly/>", 13)==0 ){
      z += 13;
    }
    
    /* Check for <title>...</title> markup and remove it if present. */
    while( isspace(*z) ) z++;
    if( strncmp(z, "<title>", 7)==0 ){
      int i;
      for(i=7; z[i] && z[i]!='<'; i++){}
      if( z[i]=='<' && strncmp(&z[i], "</title>", 8)==0 ){
        zTitle = htmlize(&z[7], i-7);
        z = &z[i+8];
      }
    }
    
    /* Render the page */
    style_header(zTitle);
    blob_init(&page, z, -1);
    wiki_convert(&page, cgi_output_blob(), WIKI_HTML);
    blob_reset(&src);
  }else{
    style_header("Unknown Wiki Page");
    @ The wiki page "%h(zPageName)" does not exist.
  }
  style_footer();
}

/*
** WEBPAGE: wiki
** URL: /wiki/PAGENAME
**
** If the local database is available (which only happens if run
** as "server" instead of "cgi" or "http") then the file is taken
** from the local checkout.  If there is no local checkout, then
** the content is taken from the "head" baseline.
*/
void wiki_page(void){
  login_check_credentials();
  if( !g.okRdWiki ){ login_needed(); return; }
  if( !g.localOpen ){
    int headid = db_int(0,
       "SELECT cid FROM plink ORDER BY mtime DESC LIMIT 1"
    );
    create_fake_vfile(headid);
  }
  locate_and_render_wikipage(g.zExtra);
}

/*
** The g.zExtra value is of the form UUID/otherstuff.
** Extract the UUID and convert it to a record id.  Leave
** g.zExtra holding just otherstuff.  If UUID does not exist
** or is malformed, return 0 and leave g.zExtra unchanged.
*/
int extract_uuid_from_url(void){
  int i, rid;
  Blob uuid;
  for(i=0; g.zExtra[i] && g.zExtra[i]!='/'; i++){}
  blob_zero(&uuid);
  blob_append(&uuid, g.zExtra, i);
  rid = name_to_uuid(&uuid, 0);
  blob_reset(&uuid);
  if( rid ){
    while( g.zExtra[i]=='/' ){ i++; }
    g.zExtra = &g.zExtra[i];
  }
  return rid;  
}

/*
** WEBPAGE: bwiki
** URL: /bwiki/UUID/PAGENAME
**
** UUID specifies a baseline.  Render the wiki page PAGENAME as
** it appears in that baseline.
*/
void bwiki_page(void){
  int headid;
  login_check_credentials();
  if( !g.okRdWiki || !g.okHistory ){ login_needed(); return; }
  headid = extract_uuid_from_url();
  if( headid ){
    create_fake_vfile(headid);
  }
  locate_and_render_wikipage(g.zExtra);
}

/*
** WEBPAGE: ambiguous
**
** This is the destination for UUID hyperlinks that are ambiguous.
** Show all possible choices for the destination with links to each.
**
** The ambiguous UUID prefix is in g.zExtra
*/
void ambiguous_page(void){
  Stmt q;
  style_header("Ambiguous UUID");
  @ <p>The link <a href="%s(g.zBaseURL)/ambiguous/%T(g.zExtra)">
  @ [%h(g.zExtra)]</a> is ambiguous.  It might mean any of the following:</p>
  @ <ul>
  db_prepare(&q, "SELECT uuid, rid FROM blob WHERE uuid>=%Q AND uuid<'%qz'"
                 " ORDER BY uuid", g.zExtra, g.zExtra);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    @ <li> %s(zUuid) - %d(rid)
  }
  db_finalize(&q);
  @ </ul>
  style_footer();
}
