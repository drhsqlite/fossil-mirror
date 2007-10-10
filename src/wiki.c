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
** Return true if the input string is a well-formed wiki page name.
**
** Well-formed wiki page names do not begin or end with whitespace,
** and do not contain tabs or other control characters and do not
** contain more than a single space character in a row.  Well-formed
** names must be between 3 and 100 chracters in length, inclusive.
*/
int wiki_name_is_wellformed(const char *z){
  int i;
  if( z[0]<=0x20 ){
    return 0;
  }
  for(i=1; z[i]; i++){
    if( z[i]<0x20 ) return 0;
    if( z[i]==0x20 && z[i-1]==0x20 ) return 0;
  }
  if( z[i-1]==' ' ) return 0;
  if( i<3 || i>100 ) return 0;
  return 1;
}

/*
** Check a wiki name.  If it is not well-formed, then issue an error
** and return true.  If it is well-formed, return false.
*/
static int check_name(const char *z){
  if( !wiki_name_is_wellformed(z) ){
    style_header("Wiki Page Name Error");
    @ The wiki name "<b>%h(z)</b>" is not well-formed.  Rules for
    @ wiki page names:
    @ <ul>
    @ <li> Must not begin or end with a space.
    @ <li> Must not contain any control characters, including tab or
    @      newline.
    @ <li> Must not have two or more spaces in a row internally.
    @ <li> Must be between 3 and 100 characters in length.
    @ </ul>
    style_footer();
    return 1;
  }
  return 0;
}

/*
** WEBPAGE: home
** WEBPAGE: index
** WEBPAGE: not_found
*/
void home_page(void){
  char *zPageName = db_get("project-name",0);
  if( zPageName ){
    login_check_credentials();
    g.zExtra = zPageName;
    g.okRdWiki = 1;
    g.okApndWiki = 0;
    g.okWrWiki = 0;
    g.okHistory = 0;
    wiki_page();
    return;
  }
  style_header("Home");
  @ <p>This is a stub home-page for the project.
  @ To fill in this page, first go to
  @ <a href="%s(g.zBaseURL)/setup_config">setup/config</a>
  @ and establish a "Project Name".  Then create a
  @ wiki page with that name.  The content of that wiki page
  @ will be displayed in place of this message.
  style_footer();
}

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
  if( check_name(zPageName) ) return;
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
  if( (rid && g.okWrWiki) || (!rid && g.okNewWiki) ){
    style_submenu_element("Edit", "Edit Wiki Page", 
       mprintf("%s/wikiedit/%s", g.zTop, g.zExtra));
  }
  if( rid && g.okApndWiki ){
    style_submenu_element("Append", "Add A Comment", 
       mprintf("%s/wikiappend/%s", g.zTop, g.zExtra));
  }
  if( g.okHistory ){
    style_submenu_element("History", "History", 
         mprintf("%s/whistory/%s", g.zTop, g.zExtra));
  }
  zHtmlPageName = mprintf("%h", zPageName);
  style_header(zHtmlPageName);
  blob_init(&wiki, zBody, -1);
  wiki_convert(&wiki, 0);
  blob_reset(&wiki);
  manifest_clear(&m);
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
  if( check_name(zPageName) ) return;
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
  if( P("cancel")!=0 ){
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
  @ <form method="POST" action="%s(g.zBaseURL)/wikiedit/%t(g.zExtra)">
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
** Append the wiki text for an remark to the end of the given BLOB.
*/
static void appendRemark(Blob *p){
  char *zDate;
  const char *zUser;
  const char *zRemark;

  zDate = db_text(0, "SELECT datetime('now')");
  blob_appendf(p, "On %s UTC %h", zDate, g.zLogin);
  free(zDate);
  zUser = PD("u",g.zLogin);
  if( zUser[0] && strcmp(zUser,g.zLogin) ){
    blob_appendf(p, " (claiming to be %h)", zUser);
  }
  zRemark = PD("r","");
  blob_appendf(p, " added:\n\n%s", zRemark);
}

/*
** WEBPAGE: wikiappend
** URL: /wikiappend/PAGENAME
*/
void wikiappend_page(void){
  char *zTag;
  int rid;
  char *zPageName;
  char *zHtmlPageName;
  const char *zUser;

  login_check_credentials();
  zPageName = mprintf("%s", g.zExtra);
  dehttpize(zPageName);
  if( check_name(zPageName) ) return;
  zTag = mprintf("wiki-%s", zPageName);
  rid = db_int(0, 
    "SELECT rid FROM tagxref"
    " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
    " ORDER BY mtime DESC", zTag
  );
  free(zTag);
  if( !rid ){
    cgi_redirect("index");
    return;
  }
  if( !g.okApndWiki ){
    login_needed();
    return;
  }
  if( P("submit")!=0 && P("r")!=0 && P("u")!=0 ){
    char *zDate;
    Blob cksum;
    int nrid;
    Blob body;
    Blob content;
    Blob wiki;
    Manifest m;

    content_get(rid, &content);
    manifest_parse(&m, &content);
    blob_zero(&body);
    if( m.type==CFTYPE_WIKI ){
      blob_appendf(&body, m.zWiki, -1);
    }
    manifest_clear(&m);
    blob_zero(&wiki);
    db_begin_transaction();
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10] = 'T';
    blob_appendf(&wiki, "D %s\n", zDate);
    blob_appendf(&wiki, "L %F\n", zPageName);
    if( rid ){
      char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      blob_appendf(&wiki, "P %s\n", zUuid);
      free(zUuid);
    }
    if( g.zLogin ){
      blob_appendf(&wiki, "U %F\n", g.zLogin);
    }
    blob_appendf(&body, "\n<hr>\n");
    appendRemark(&body);
    blob_appendf(&wiki, "W %d\n%s\n", blob_size(&body), blob_str(&body));
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
  if( P("cancel")!=0 ){
    cgi_redirect(mprintf("wiki/%s", g.zExtra));
    return;
  }
  zHtmlPageName = mprintf("Append Comment To: %h", zPageName);
  style_header(zHtmlPageName);
  if( P("preview")!=0 ){
    Blob preview;
    blob_zero(&preview);
    appendRemark(&preview);
    @ Preview:<hr>
    wiki_convert(&preview, 0);
    @ <hr>
    blob_reset(&preview);
  }
  zUser = PD("u", g.zLogin);
  @ <form method="POST" action="%s(g.zBaseURL)/wikiappend/%t(g.zExtra)">
  @ Your Name:
  @ <input type="text" name="u" size="20" value="%h(zUser)"><br>
  @ Comment to append:<br>
  @ <textarea name="r" class="wikiedit" cols="80" 
  @  rows="10" wrap="virtual">%h(PD("r",""))</textarea>
  @ <br>
  @ <input type="submit" name="preview" value="Preview Your Comment">
  @ <input type="submit" name="submit" value="Append Your Changes">
  @ <input type="submit" name="cancel" value="Cancel">
  @ </form>
  style_footer();
}

/*
** WEBPAGE: whistory
**
** Show the complete change history for a single wiki page.  The name
** of the wiki is in g.zExtra
*/
void whistory_page(void){
  Stmt q;
  char *zTitle;
  char *zSQL;
  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  zTitle = mprintf("History Of %h", g.zExtra);
  style_header(zTitle);
  free(zTitle);

  zSQL = mprintf("%s AND event.objid IN "
                 "  (SELECT rid FROM tagxref WHERE tagid="
                       "(SELECT tagid FROM tag WHERE tagname='wiki-%q'))"
                 "ORDER BY mtime DESC",
                 timeline_query_for_www(), g.zExtra);
  db_prepare(&q, zSQL);
  free(zSQL);
  www_print_timeline(&q, 0, 0, 0, 0);
  db_finalize(&q);
  style_footer();
}

/*
** WEBPAGE: wcontent
**
** List all available wiki pages with date created and last modified.
*/
void wcontent_page(void){
  Stmt q;
  login_check_credentials();
  if( !g.okRdWiki ){ login_needed(); return; }
  style_header("Available Wiki Pages");
  @ <ul>
  db_prepare(&q, 
    "SELECT substr(tagname, 6, 1000) FROM tag WHERE tagname GLOB 'wiki-*'"
    " ORDER BY lower(tagname)"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    @ <li><a href="%s(g.zBaseURL)/wiki/%t(zName)">%h(zName)</a></li>
  }
  db_finalize(&q);
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
