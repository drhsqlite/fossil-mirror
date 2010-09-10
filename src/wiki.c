/*
** Copyright (c) 2007 D. Richard Hipp
** Copyright (c) 2008 Stephan Beal
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
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
#include <ctype.h>
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
int wiki_name_is_wellformed(const unsigned char *z){
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
** Output rules for well-formed wiki pages
*/
static void well_formed_wiki_name_rules(void){
  @ <ul>
  @ <li> Must not begin or end with a space.
  @ <li> Must not contain any control characters, including tab or
  @      newline.
  @ <li> Must not have two or more spaces in a row internally.
  @ <li> Must be between 3 and 100 characters in length.
  @ </ul>
}

/*
** Check a wiki name.  If it is not well-formed, then issue an error
** and return true.  If it is well-formed, return false.
*/
static int check_name(const char *z){
  if( !wiki_name_is_wellformed((const unsigned char *)z) ){
    style_header("Wiki Page Name Error");
    @ The wiki name "<span class="wikiError">%h(z)</span>" is not well-formed.
    @ Rules for wiki page names:
    well_formed_wiki_name_rules();
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
  char *zIndexPage = db_get("index-page",0);
  login_check_credentials();
  if( !g.okRdWiki ){
    cgi_redirectf("%s/login?g=%s/home", g.zBaseURL, g.zBaseURL);
  }
  if( zIndexPage ){
    while( zIndexPage[0]=='/' ) zIndexPage++;
    if( strcmp(zIndexPage, P("PATH_INFO"))==0 ) zIndexPage = 0;
  }
  if( zIndexPage ){
    cgi_redirectf("%s/%s", g.zBaseURL, zIndexPage);
  }
  if( zPageName ){
    login_check_credentials();
    g.zExtra = zPageName;
    cgi_set_parameter_nocopy("name", g.zExtra);
    g.isHome = 1;
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
** Return true if the given pagename is the name of the sandbox
*/
static int is_sandbox(const char *zPagename){
  return strcasecmp(zPagename,"sandbox")==0 ||
         strcasecmp(zPagename,"sand box")==0;
}

/*
** WEBPAGE: wiki
** URL: /wiki?name=PAGENAME
*/
void wiki_page(void){
  char *zTag;
  int rid = 0;
  int isSandbox;
  Blob wiki;
  Manifest m;
  const char *zPageName;
  char *zBody = mprintf("%s","<i>Empty Page</i>");
  Stmt q;
  int cnt = 0;

  login_check_credentials();
  if( !g.okRdWiki ){ login_needed(); return; }
  zPageName = P("name");
  if( zPageName==0 ){
    style_header("Wiki");
    @ <ul>
    { char *zHomePageName = db_get("project-name",0);
      if( zHomePageName ){
        @ <li> <a href="%s(g.zBaseURL)/wiki?name=%t(zHomePageName)">
        @      %h(zHomePageName)</a> wiki home page.</li>
      }
    }
    @ <li> <a href="%s(g.zBaseURL)/timeline?y=w">Recent changes</a> to wiki
    @      pages. </li>
    @ <li> <a href="%s(g.zBaseURL)/wiki_rules">Formatting rules</a> for 
    @      wiki.</li>
    @ <li> Use the <a href="%s(g.zBaseURL)/wiki?name=Sandbox">Sandbox</a>
    @      to experiment.</li>
    if( g.okNewWiki ){
      @ <li>  Create a <a href="%s(g.zBaseURL)/wikinew">new wiki page</a>.</li>
    }
    @ <li> <a href="%s(g.zBaseURL)/wcontent">List of All Wiki Pages</a>
    @      available on this server.</li>
	@ <li> <form method="get" action="%s(g.zBaseURL)/wfind"><div>
	@     Search wiki titles: <input type="text" name="title"/>
        @  &nbsp; <input type="submit" /></div></form>
	@ </li>
    @ </ul>
    style_footer();
    return;
  }
  if( check_name(zPageName) ) return;
  isSandbox = is_sandbox(zPageName);
  if( isSandbox ){
    zBody = db_get("sandbox",zBody);
  }else{
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
      if( m.type==CFTYPE_WIKI && m.zWiki ){
        while( isspace(m.zWiki[0]) ) m.zWiki++;
        if( m.zWiki[0] ) zBody = m.zWiki;
      }
    }
  }
  if( !g.isHome ){
    if( (rid && g.okWrWiki) || (!rid && g.okNewWiki) ){
      style_submenu_element("Edit", "Edit Wiki Page", "%s/wikiedit?name=%T",
           g.zTop, zPageName);
    }
    if( rid && g.okApndWiki && g.okAttach ){
      style_submenu_element("Attach", "Add An Attachment",
           "%s/attachadd?page=%T&from=%s/wiki%%3fname=%T",
           g.zTop, zPageName, g.zTop, zPageName);
    }
    if( rid && g.okApndWiki ){
      style_submenu_element("Append", "Add A Comment", "%s/wikiappend?name=%T",
           g.zTop, zPageName);
    }
    if( g.okHistory ){
      style_submenu_element("History", "History", "%s/whistory?name=%T",
           g.zTop, zPageName);
    }
  }
  style_header(zPageName);
  blob_init(&wiki, zBody, -1);
  wiki_convert(&wiki, 0, 0);
  blob_reset(&wiki);

  db_prepare(&q,
     "SELECT datetime(mtime,'localtime'), filename, user"
     "  FROM attachment"
     " WHERE isLatest AND src!='' AND target=%Q"
     " ORDER BY mtime DESC",
     zPageName);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 0);
    const char *zFile = db_column_text(&q, 1);
    const char *zUser = db_column_text(&q, 2);
    if( cnt==0 ){
      @ <hr><h2>Attachments:</h2>
      @ <ul>
    }
    cnt++;
    if( g.okHistory && g.okRead ){
      @ <li><a href="%s(g.zTop)/attachview?page=%s(zPageName)&file=%t(zFile)">
      @ %h(zFile)</a>
    }else{
      @ <li>%h(zFile)
    }
    @ added by %h(zUser) on
    hyperlink_to_date(zDate, ".");
    if( g.okWrWiki && g.okAttach ){
      @ [<a href="%s(g.zTop)/attachdelete?page=%s(zPageName)&file=%t(zFile)&from=%s(g.zTop)/wiki%%3fname=%s(zPageName)">delete</a>]
    }
  }
  if( cnt ){
    @ </ul>
  }
  db_finalize(&q);
 
  if( !isSandbox ){
    manifest_clear(&m);
  }
  style_footer();
}

/*
** WEBPAGE: wikiedit
** URL: /wikiedit?name=PAGENAME
*/
void wikiedit_page(void){
  char *zTag;
  int rid = 0;
  int isSandbox;
  Blob wiki;
  Manifest m;
  const char *zPageName;
  char *zHtmlPageName;
  int n;
  const char *z;
  char *zBody = (char*)P("w");

  if( zBody ){
    zBody = mprintf("%s", zBody);
  }
  login_check_credentials();
  zPageName = PD("name","");
  if( check_name(zPageName) ) return;
  isSandbox = is_sandbox(zPageName);
  if( isSandbox ){
    if( !g.okWrWiki ){
      login_needed();
      return;
    }
    if( zBody==0 ){
      zBody = db_get("sandbox","");
    }
  }else{
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
  }
  if( P("submit")!=0 && zBody!=0 ){
    char *zDate;
    Blob cksum;
    int nrid;
    blob_zero(&wiki);
    db_begin_transaction();
    if( isSandbox ){
      db_set("sandbox",zBody,0);
    }else{
      login_verify_csrf_secret();
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
    }
    db_end_transaction(0);
    cgi_redirectf("wiki?name=%T", zPageName);
  }
  if( P("cancel")!=0 ){
    cgi_redirectf("wiki?name=%T", zPageName);
    return;
  }
  if( zBody==0 ){
    zBody = mprintf("<i>Empty Page</i>");
  }
  zHtmlPageName = mprintf("Edit: %s", zPageName);
  style_header(zHtmlPageName);
  if( P("preview")!=0 ){
    blob_zero(&wiki);
    blob_append(&wiki, zBody, -1);
    @ Preview:<hr>
    wiki_convert(&wiki, 0, 0);
    @ <hr>
    blob_reset(&wiki);
  }
  for(n=2, z=zBody; z[0]; z++){
    if( z[0]=='\n' ) n++;
  }
  if( n<20 ) n = 20;
  if( n>40 ) n = 40;
  @ <form method="post" action="%s(g.zBaseURL)/wikiedit"><div>
  login_insert_csrf_secret();
  @ <input type="hidden" name="name" value="%h(zPageName)" />
  @ <textarea name="w" class="wikiedit" cols="80" 
  @  rows="%d(n)" wrap="virtual">%h(zBody)</textarea>
  @ <br>
  @ <input type="submit" name="preview" value="Preview Your Changes" />
  @ <input type="submit" name="submit" value="Apply These Changes" />
  @ <input type="submit" name="cancel" value="Cancel" />
  @ </div></form>
  if( !isSandbox ){
    manifest_clear(&m);
  }
  style_footer();
}

/*
** WEBPAGE: wikinew
** URL /wikinew
**
** Prompt the user to enter the name of a new wiki page.  Then redirect
** to the wikiedit screen for that new page.
*/
void wikinew_page(void){
  const char *zName;
  login_check_credentials();
  if( !g.okNewWiki ){
    login_needed();
    return;
  }  
  zName = PD("name","");
  if( zName[0] && wiki_name_is_wellformed((const unsigned char *)zName) ){
    cgi_redirectf("wikiedit?name=%T", zName);
  }
  style_header("Create A New Wiki Page");
  @ <p>Rules for wiki page names:
  well_formed_wiki_name_rules();
  @ </p>
  @ <form method="POST" action="%s(g.zBaseURL)/wikinew">
  @ <p>Name of new wiki page:
  @ <input type="text" width="35" name="name" value="%h(zName)">
  @ <input type="submit" value="Create">
  @ </p></form>
  if( zName[0] ){
    @ <p><span class="wikiError">
    @ "%h(zName)" is not a valid wiki page name!</span></p>
  }
  style_footer();
}


/*
** Append the wiki text for an remark to the end of the given BLOB.
*/
static void appendRemark(Blob *p){
  char *zDate;
  const char *zUser;
  const char *zRemark;
  char *zId;

  zDate = db_text(0, "SELECT datetime('now')");
  zId = db_text(0, "SELECT lower(hex(randomblob(8)))");
  blob_appendf(p, "\n\n<hr><div id=\"%s\"><i>On %s UTC %h", 
    zId, zDate, g.zLogin);
  free(zDate);
  zUser = PD("u",g.zLogin);
  if( zUser[0] && strcmp(zUser,g.zLogin) ){
    blob_appendf(p, " (claiming to be %h)", zUser);
  }
  zRemark = PD("r","");
  blob_appendf(p, " added:</i><br />\n%s</div id=\"%s\">", zRemark, zId);
}

/*
** WEBPAGE: wikiappend
** URL: /wikiappend?name=PAGENAME
*/
void wikiappend_page(void){
  char *zTag;
  int rid = 0;
  int isSandbox;
  const char *zPageName;
  char *zHtmlPageName;
  const char *zUser;

  login_check_credentials();
  zPageName = PD("name","");
  if( check_name(zPageName) ) return;
  isSandbox = is_sandbox(zPageName);
  if( !isSandbox ){
    zTag = mprintf("wiki-%s", zPageName);
    rid = db_int(0, 
      "SELECT rid FROM tagxref"
      " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
      " ORDER BY mtime DESC", zTag
    );
    free(zTag);
    if( !rid ){
      fossil_redirect_home();
      return;
    }
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

    blob_zero(&body);
    if( isSandbox ){
      blob_appendf(&body, db_get("sandbox",""));
      appendRemark(&body);
      db_set("sandbox", blob_str(&body), 0);
    }else{
      login_verify_csrf_secret();
      content_get(rid, &content);
      manifest_parse(&m, &content);
      if( m.type==CFTYPE_WIKI ){
        blob_append(&body, m.zWiki, -1);
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
    }
    cgi_redirectf("wiki?name=%T", zPageName);
  }
  if( P("cancel")!=0 ){
    cgi_redirectf("wiki?name=%T", zPageName);
    return;
  }
  zHtmlPageName = mprintf("Append Comment To: %s", zPageName);
  style_header(zHtmlPageName);
  if( P("preview")!=0 ){
    Blob preview;
    blob_zero(&preview);
    appendRemark(&preview);
    @ Preview:<hr>
    wiki_convert(&preview, 0, 0);
    @ <hr>
    blob_reset(&preview);
  }
  zUser = PD("u", g.zLogin);
  @ <form method="POST" action="%s(g.zBaseURL)/wikiappend">
  login_insert_csrf_secret();
  @ <input type="hidden" name="name" value="%h(zPageName)">
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
** Name of the wiki history page being generated
*/
static const char *zWikiPageName;

/*
** Function called to output extra text at the end of each line in
** a wiki history listing.
*/
static void wiki_history_extra(int rid){
  if( db_exists("SELECT 1 FROM tagxref WHERE rid=%d", rid) ){
    @ <a href="%s(g.zTop)/wdiff?name=%t(zWikiPageName)&a=%d(rid)">[diff]</a>
  }
}

/*
** WEBPAGE: whistory
** URL: /whistory?name=PAGENAME
**
** Show the complete change history for a single wiki page.
*/
void whistory_page(void){
  Stmt q;
  char *zTitle;
  char *zSQL;
  const char *zPageName;
  login_check_credentials();
  if( !g.okHistory ){ login_needed(); return; }
  zPageName = PD("name","");
  zTitle = mprintf("History Of %s", zPageName);
  style_header(zTitle);
  free(zTitle);

  zSQL = mprintf("%s AND event.objid IN "
                 "  (SELECT rid FROM tagxref WHERE tagid="
                       "(SELECT tagid FROM tag WHERE tagname='wiki-%q')"
                 "   UNION SELECT attachid FROM attachment"
                          " WHERE target=%Q)"
                 "ORDER BY mtime DESC",
                 timeline_query_for_www(), zPageName, zPageName);
  db_prepare(&q, zSQL);
  free(zSQL);
  zWikiPageName = zPageName;
  www_print_timeline(&q, TIMELINE_ARTID, wiki_history_extra);
  db_finalize(&q);
  style_footer();
}

/*
** WEBPAGE: wdiff
** URL: /whistory?name=PAGENAME&a=RID1&b=RID2
**
** Show the difference between two wiki pages.
*/
void wdiff_page(void){
  char *zTitle;
  int rid1, rid2;
  const char *zPageName;
  Blob content1, content2;
  Manifest m1, m2;
  Blob w1, w2, d;

  login_check_credentials();
  rid1 = atoi(PD("a","0"));
  if( !g.okHistory ){ login_needed(); return; }
  if( rid1==0 ) fossil_redirect_home();
  rid2 = atoi(PD("b","0"));
  zPageName = PD("name","");
  zTitle = mprintf("Changes To %s", zPageName);
  style_header(zTitle);
  free(zTitle);

  if( rid2==0 ){
    rid2 = db_int(0,
      "SELECT objid FROM event JOIN tagxref ON objid=rid AND tagxref.tagid="
                        "(SELECT tagid FROM tag WHERE tagname='wiki-%q')"
      " WHERE event.mtime<(SELECT mtime FROM event WHERE objid=%d)"
      " ORDER BY event.mtime DESC LIMIT 1",
      zPageName, rid1
    );
  }
  content_get(rid1, &content1);
  manifest_parse(&m1, &content1);
  if( m1.type!=CFTYPE_WIKI ) fossil_redirect_home();
  blob_init(&w1, m1.zWiki, -1);
  blob_zero(&w2);
  if( rid2 ){
    content_get(rid2, &content2);
    manifest_parse(&m2, &content2);
    if( m2.type==CFTYPE_WIKI ){
      blob_init(&w2, m2.zWiki, -1);
    }
  }
  blob_zero(&d);
  text_diff(&w2, &w1, &d, 5, 1);
  @ <pre>
  @ %h(blob_str(&d))
  @ </pre>
  style_footer();
}

/*
** WEBPAGE: wcontent
**
**     all=1         Show deleted pages
**
** List all available wiki pages with date created and last modified.
*/
void wcontent_page(void){
  Stmt q;
  int showAll = P("all")!=0;

  login_check_credentials();
  if( !g.okRdWiki ){ login_needed(); return; }
  style_header("Available Wiki Pages");
  if( showAll ){
    style_submenu_element("Active", "Only Active Pages", "%s/wcontent", g.zTop);
  }else{
    style_submenu_element("All", "All", "%s/wcontent?all=1", g.zTop);
  }
  @ <ul>
  db_prepare(&q, 
    "SELECT"
    "  substr(tagname, 6),"
    "  (SELECT value FROM tagxref WHERE tagid=tag.tagid ORDER BY mtime DESC)"
    "  FROM tag WHERE tagname GLOB 'wiki-*'"
    " ORDER BY lower(tagname) /*sort*/"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    int size = db_column_int(&q, 1);
    if( size>0 ){
      @ <li><a href="%s(g.zTop)/wiki?name=%T(zName)">%h(zName)</a></li>
    }else if( showAll ){
      @ <li><a href="%s(g.zTop)/wiki?name=%T(zName)"><s>%h(zName)</s></a></li>
    }
  }
  db_finalize(&q);
  @ </ul>
  style_footer();
}

/*
** WEBPAGE: wfind
**
** URL: /wfind?title=TITLE
** List all wiki pages whose titles contain the search text
*/
void wfind_page(void){
  Stmt q;
  const char * zTitle;
  login_check_credentials();
  if( !g.okRdWiki ){ login_needed(); return; }
  zTitle = PD("title","*");
  style_header("Wiki Pages Found");
  @ <ul>
  db_prepare(&q, 
    "SELECT substr(tagname, 6, 1000) FROM tag WHERE tagname like 'wiki-%%%q%%'"
    " ORDER BY lower(tagname) /*sort*/" ,
	zTitle);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    @ <li><a href="%s(g.zBaseURL)/wiki?name=%T(zName)">%h(zName)</a></li>
  }
  db_finalize(&q);
  @ </ul>
  style_footer();
}

/*
** WEBPAGE: wiki_rules
*/
void wikirules_page(void){
  style_header("Wiki Formatting Rules");
  @ <h2>Formatting Rule Summary</h2>
  @ <ol>
  @ <li>Blank lines are paragraph breaks</li>
  @ <li>Bullets are "*" surrounded by two spaces at the beginning of the
  @ line.</li>
  @ <li>Enumeration items are "#" surrounded by two spaces at the beginning of
  @ a line.</li>
  @ <li>Indented pargraphs begin with a tab or two spaces.</li>
  @ <li>Hyperlinks are contained with square brackets:  "[target]" or
  @ "[target|name]".</li>
  @ <li>Most ordinary HTML works.</li>
  @ <li>&lt;verbatim&gt; and &lt;nowiki&gt;.</li>
  @ </ol>
  @ <p>We call the first five rules above "wiki" formatting rules.  The
  @ last two rules are the HTML formatting rule.</p>
  @ <h2>Formatting Rule Details</h2>
  @ <ol>
  @ <li> <p><b>Paragraphs</b>.  Any sequence of one or more blank lines forms
  @ a paragraph break.  Centered or right-justified paragraphs are not
  @ supported by wiki markup, but you can do these things if you need them
  @ using HTML.</p>
  @ <li> <p><b>Bullet Lists</b>.
  @ A bullet list item is a line that begins with a single "*" character
  @ surrounded on
  @ both sides by two or more spaces or by a tab.  Only a single level
  @ of bullet list is supported by wiki.  For nested lists, use HTML.</p>
  @ <li> <p><b>Enumeration Lists</b>.
  @ An enumeration list item is a line that begins with a single "#" character
  @ surrounded on both sides by two or more spaces or by a tab.  Only a single
  @ level of enumeration list is supported by wiki.  For nested lists or for
  @ enumerations that count using letters or roman numerials, use HTML.</p>
  @ <li> <p><b>Indented Paragraphs</b>.
  @ Any paragraph that begins with two or more spaces or a tab and
  @ which is not a bullet or enumeration list item is rendered 
  @ indented.  Only a single level of indentation is supported by wiki; use
  @ HTML for deeper indentation.</p>
  @ <li> <p><b>Hyperlinks</b>.
  @ Text within square brackets ("[...]") becomes a hyperlink.  The
  @ target can be a wiki page name, the artifact ID of a check-in or ticket,
  @ the name of an image, or a URL.  By default, the target is displayed
  @ as the text of the hyperlink.  But you can specify alternative text
  @ after the target name separated by a "|" character.</p>
  @ <p>You can also link to internal anchor names using [#anchor-name], providing
  @ you have added the necessary "&lt;a name="anchor-name"&gt;&lt;/a&gt;"
  @ tag to your wiki page.</p>
  @ <li> <p><b>HTML</b>.
  @ The following standard HTML elements may be used:
  @ &lt;a&gt;
  @ &lt;address&gt;
  @ &lt;b&gt;
  @ &lt;big&gt;
  @ &lt;blockquote&gt;
  @ &lt;br&gt;
  @ &lt;center&gt;
  @ &lt;cite&gt;
  @ &lt;code&gt;
  @ &lt;dd&gt;
  @ &lt;dfn&gt;
  @ &lt;div&gt;
  @ &lt;dl&gt;
  @ &lt;dt&gt;
  @ &lt;em&gt;
  @ &lt;font&gt;
  @ &lt;h1&gt;
  @ &lt;h2&gt;
  @ &lt;h3&gt;
  @ &lt;h4&gt;
  @ &lt;h5&gt;
  @ &lt;h6&gt;
  @ &lt;hr&gt;
  @ &lt;img&gt;
  @ &lt;i&gt;
  @ &lt;kbd&gt;
  @ &lt;li&gt;
  @ &lt;nobr&gt;
  @ &lt;ol&gt;
  @ &lt;p&gt;
  @ &lt;pre&gt;
  @ &lt;s&gt;
  @ &lt;samp&gt;
  @ &lt;small&gt;
  @ &lt;strike&gt;
  @ &lt;strong&gt;
  @ &lt;sub&gt;
  @ &lt;sup&gt;
  @ &lt;table&gt;
  @ &lt;td&gt;
  @ &lt;th&gt;
  @ &lt;tr&gt;
  @ &lt;tt&gt;
  @ &lt;u&gt;
  @ &lt;ul&gt;
  @ &lt;var&gt;.
  @ In addition, there are two non-standard elements available:
  @ &lt;verbatim&gt; and &lt;nowiki&gt;.
  @ No other elements are allowed.  All attributes are checked and
  @ only a few benign attributes are allowed on each element.
  @ In particular, any attributes that specify javascript or CSS
  @ are elided.</p></li>
  @ <li><p><b>Special Markup.</b>
  @ The &lt;nowiki&gt; tag disables all wiki formatting rules
  @ through the matching &lt;/nowiki&gt; element.
  @ The &lt;verbatim&gt; tag works like &lt;pre&gt; with the addition
  @ that it also disables all wiki and HTML markup
  @ through the matching &lt;/verbatim&gt;.
  @ </ol>
  style_footer();
}

/*
** Add a new wiki page to the respository.  The page name is
** given by the zPageName parameter.  isNew must be true to create
** a new page.  If no previous page with the name zPageName exists
** and isNew is false, then this routine throws an error.
**
** The content of the new page is given by the blob pContent.
*/
int wiki_cmd_commit(char const * zPageName, int isNew, Blob *pContent){
  Blob wiki;              /* Wiki page content */
  Blob cksum;             /* wiki checksum */
  int rid;                /* artifact ID of parent page */
  int nrid;               /* artifact ID of new wiki page */
  char *zDate;            /* timestamp */
  char *zUuid;            /* uuid for rid */

  rid = db_int(0,
     "SELECT x.rid FROM tag t, tagxref x"
     " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q'"
     " ORDER BY x.mtime DESC LIMIT 1",
     zPageName
  );
  if( rid==0 && !isNew ){
    fossil_fatal("no such wiki page: %s", zPageName);
  }
  if( rid!=0 && isNew ){
    fossil_fatal("wiki page %s already exists", zPageName);
  }

  blob_zero(&wiki);
  zDate = db_text(0, "SELECT datetime('now')");
  zDate[10] = 'T';
  blob_appendf(&wiki, "D %s\n", zDate);
  free(zDate);
  blob_appendf(&wiki, "L %F\n", zPageName );
  if( rid ){
    zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    blob_appendf(&wiki, "P %s\n", zUuid);
    free(zUuid);
  }
  user_select();
  if( g.zLogin ){
      blob_appendf(&wiki, "U %F\n", g.zLogin);
  }
  blob_appendf( &wiki, "W %d\n%s\n", blob_size(pContent),
                blob_str(pContent) );
  md5sum_blob(&wiki, &cksum);
  blob_appendf(&wiki, "Z %b\n", &cksum);
  blob_reset(&cksum);
  db_begin_transaction();
  nrid = content_put( &wiki, 0, 0 );
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nrid);
  manifest_crosslink(nrid,&wiki);
  blob_reset(&wiki);
  content_deltify(rid,nrid,0);
  db_end_transaction(0);
  autosync(AUTOSYNC_PUSH);  
  return 1;
}

/*
** COMMAND: wiki
**
** Usage: %fossil wiki (export|create|commit|list) WikiName
**
** Run various subcommands to work with wiki entries.
**
**     %fossil wiki export PAGENAME ?FILE?
**
**        Sends the latest version of the PAGENAME wiki
**        entry to the given file or standard output.
**
**     %fossil wiki commit PAGENAME ?FILE?
**
**        Commit changes to a wiki page from FILE or from standard
**        input.
**
**     %fossil wiki create PAGENAME ?FILE?
**
**        Create a new wiki page with initial content taken from
**        FILE or from standard input.
**
**     %fossil wiki list
**
**        Lists all wiki entries, one per line, ordered
**        case-insentively by name.
**
** TODOs:
**
**     %fossil wiki export ?-u ARTIFACT? WikiName ?FILE?
**
**        Outputs the selected version of WikiName.
**
**     %fossil wiki delete ?-m MESSAGE? WikiName
**
**        The same as deleting a file entry, but i don't know if fossil
**        supports a commit message for Wiki entries.
**
**     %fossil wiki ?-u? ?-d? ?-s=[|]? list
**
**        Lists the artifact ID and/or Date of last change along with
**        each entry name, delimited by the -s char.
**
**     %fossil wiki diff ?ARTIFACT? ?-f infile[=stdin]? EntryName
**
**        Diffs the local copy of a page with a given version (defaulting
**        to the head version).
*/
void wiki_cmd(void){
  int n;
  db_find_and_open_repository(1);
  if( g.argc<3 ){
    goto wiki_cmd_usage;
  }
  n = strlen(g.argv[2]);
  if( n==0 ){
    goto wiki_cmd_usage;
  }

  if( strncmp(g.argv[2],"export",n)==0 ){
    char const *zPageName;        /* Name of the wiki page to export */
    char const *zFile;            /* Name of the output file (0=stdout) */
    int rid;                /* Artifact ID of the wiki page */
    int i;                  /* Loop counter */
    char *zBody = 0;        /* Wiki page content */
    Manifest m;             /* Parsed wiki page content */
    if( (g.argc!=4) && (g.argc!=5) ){
      usage("export PAGENAME ?FILE?");
    }
    zPageName = g.argv[3];
    rid = db_int(0, "SELECT x.rid FROM tag t, tagxref x"
      " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q'"
      " ORDER BY x.mtime DESC LIMIT 1",
      zPageName 
    );
    if( rid ){
      Blob content;
      content_get(rid, &content);
      manifest_parse(&m, &content);
      if( m.type==CFTYPE_WIKI ){
        zBody = m.zWiki;
      }
    }
    if( zBody==0 ){
      fossil_fatal("wiki page [%s] not found",zPageName);
    }
    for(i=strlen(zBody); i>0 && isspace(zBody[i-1]); i--){}
    zFile  = (g.argc==4) ? 0 : g.argv[4];
    if( zFile ){
      FILE * zF;
      short doClose = 0;
      if( (1 == strlen(zFile)) && ('-'==zFile[0]) ){
        zF = stdout;
      }else{
        zF = fopen( zFile, "w" );
        doClose = zF ? 1 : 0;
      }
      if( ! zF ){
        fossil_fatal("wiki export could not open output file for writing.");
      }
      fprintf(zF,"%.*s\n", i, zBody);
      if( doClose ) fclose(zF);
    }else{
	printf("%.*s\n", i, zBody);
    }
    return;
  }else
  if( strncmp(g.argv[2],"commit",n)==0
      || strncmp(g.argv[2],"create",n)==0 ){
    char *zPageName;
    Blob content;
    if( g.argc!=4 && g.argc!=5 ){
      usage("commit PAGENAME ?FILE?");
    }
    zPageName = g.argv[3];
    if( g.argc==4 ){
      blob_read_from_channel(&content, stdin, -1);
    }else{
      blob_read_from_file(&content, g.argv[4]);
    }
    if( g.argv[2][1]=='r' ){
      wiki_cmd_commit(zPageName, 1, &content);
      printf("Created new wiki page %s.\n", zPageName);
    }else{
      wiki_cmd_commit(zPageName, 0, &content);
      printf("Updated wiki page %s.\n", zPageName);
    }
    blob_reset(&content);
  }else
  if( strncmp(g.argv[2],"delete",n)==0 ){
    if( g.argc!=5 ){
      usage("delete PAGENAME");
    }
    fossil_fatal("delete not yet implemented.");
  }else
  if( strncmp(g.argv[2],"list",n)==0 ){
    Stmt q;
    db_prepare(&q, 
      "SELECT substr(tagname, 6) FROM tag WHERE tagname GLOB 'wiki-*'"
      " ORDER BY lower(tagname) /*sort*/"
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      printf( "%s\n",zName);
    }
    db_finalize(&q);
  }else
  {
    goto wiki_cmd_usage;
  }
  return;

wiki_cmd_usage:
  usage("export|create|commit|list ...");
}
