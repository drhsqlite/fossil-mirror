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
** WEBPAGE: wiki
** URL: /wiki/PAGENAME
*/
void wiki_page(void){
  char *zTag;
  int rid;
  Blob wiki;
  Manifest m;
  char *zPageName;
  char *zHtmlPageName;
  char *zBody = mprintf("%s","<i>Empty Page</i>");

  login_check_credentials();
  if( !g.okRdWiki ){ login_needed(); return; }
  zPageName = mprintf("%s", g.zExtra);
  dehttpize(zPageName);
  zTag = mprintf("wiki-%s", zPageName);
  rid = db_int(0, 
    "SELECT rid FROM tagxref"
    " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
    " ORDER BY mtime DESC", zTag
  );
  free(zTag);
  memset(&m, 0, sizeof(m));
  blob_zero(&m.content);
  if( rid ){
    Blob content;
    content_get(rid, &content);
    manifest_parse(&m, &content);
    if( m.type==CFTYPE_WIKI ){
      zBody = m.zWiki;
    }
  }
  zHtmlPageName = mprintf("%h", zPageName);
  style_header(zHtmlPageName);
  blob_init(&wiki, zBody, -1);
  wiki_convert(&wiki, 0);
  blob_reset(&wiki);
  manifest_clear(&m);
  if( zPageName[0] && ((rid && g.okWrWiki) || (!rid && g.okNewWiki)) ){
    @ <hr>
    @ [<a href="%s(g.zBaseURL)/wikiedit/%s(g.zExtra)">Edit</a>]
  }
  style_footer();
}

/*
** WEBPAGE: wikiedit
** URL: /wikiedit/PAGENAME
*/
void wikiedit_page(void){
  char *zTag;
  int rid;
  Blob wiki;
  Manifest m;
  char *zPageName;
  char *zHtmlPageName;
  int n;
  const char *z;
  char *zBody = (char*)P("w");

  if( zBody ){
    zBody = mprintf("%s", zBody);
  }
  login_check_credentials();
  zPageName = mprintf("%s", g.zExtra);
  dehttpize(zPageName);
  zTag = mprintf("wiki-%s", zPageName);
  rid = db_int(0, 
    "SELECT rid FROM tagxref"
    " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
    " ORDER BY mtime DESC", zTag
  );
  free(zTag);
  if( (rid && !g.okWrWiki) || (!rid && !g.okNewWiki) ){
    login_needed();
    return;
  }
  memset(&m, 0, sizeof(m));
  blob_zero(&m.content);
  if( rid && zBody==0 ){
    Blob content;
    content_get(rid, &content);
    manifest_parse(&m, &content);
    if( m.type==CFTYPE_WIKI ){
      zBody = m.zWiki;
    }
  }
  if( P("submit")!=0 && zBody!=0 ){
    char *zDate;
    Blob cksum;
    int nrid;
    blob_zero(&wiki);
    db_begin_transaction();
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10] = 'T';
    blob_appendf(&wiki, "D %s\n", zDate);
    free(zDate);
    blob_appendf(&wiki, "L %F\n", zPageName);
    if( rid ){
      char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      blob_appendf(&wiki, "P %s\n", zUuid);
      free(zUuid);
    }
    if( g.zLogin ){
      blob_appendf(&wiki, "U %F\n", g.zLogin);
    }
    blob_appendf(&wiki, "W %d\n%s\n", strlen(zBody), zBody);
    md5sum_blob(&wiki, &cksum);
    blob_appendf(&wiki, "Z %b\n", &cksum);
    blob_reset(&cksum);
    nrid = content_put(&wiki, 0, 0);
    db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nrid);
    manifest_crosslink(nrid, &wiki);
    blob_reset(&wiki);
    content_deltify(rid, nrid, 0);
    db_end_transaction(0);
    cgi_redirect(mprintf("wiki/%s", g.zExtra));
  }
  if( P("cancel")!=0 || zPageName[0]==0 ){
    cgi_redirect(mprintf("wiki/%s", g.zExtra));
    return;
  }
  if( zBody==0 ){
    zBody = mprintf("<i>Empty Page</i>");
  }
  zHtmlPageName = mprintf("Edit: %h", zPageName);
  style_header(zHtmlPageName);
  if( P("preview")!=0 ){
    blob_zero(&wiki);
    blob_append(&wiki, zBody, -1);
    @ Preview:<hr>
    wiki_convert(&wiki, 0);
    @ <hr>
    blob_reset(&wiki);
  }
  for(n=2, z=zBody; z[0]; z++){
    if( z[0]=='\n' ) n++;
  }
  if( n<20 ) n = 20;
  if( n>200 ) n = 200;
  @ <form method="POST" action="%s(g.zBaseURL)/wikiedit/%s(g.zExtra)">
  @ <textarea name="w" class="wikiedit" cols="80" 
  @  rows="%d(n)" wrap="virtual">%h(zBody)</textarea>
  @ <br>
  @ <input type="submit" name="preview" value="Preview Your Changes">
  @ <input type="submit" name="submit" value="Apply These Changes">
  @ <input type="submit" name="cancel" value="Cancel">
  @ </form>
  manifest_clear(&m);
  style_footer();

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
