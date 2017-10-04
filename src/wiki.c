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
#include "config.h"
#include <assert.h>
#include <ctype.h>
#include "wiki.h"

/*
** Return true if the input string is a well-formed wiki page name.
**
** Well-formed wiki page names do not begin or end with whitespace,
** and do not contain tabs or other control characters and do not
** contain more than a single space character in a row.  Well-formed
** names must be between 1 and 100 characters in length, inclusive.
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
  if( i<1 || i>100 ) return 0;
  return 1;
}

/*
** Output rules for well-formed wiki pages
*/
static void well_formed_wiki_name_rules(void){
  @ <ul>
  @ <li> Must not begin or end with a space.</li>
  @ <li> Must not contain any control characters, including tab or
  @      newline.</li>
  @ <li> Must not have two or more spaces in a row internally.</li>
  @ <li> Must be between 1 and 100 characters in length.</li>
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
**
** The /home, /index, and /not_found pages all redirect to the homepage
** configured by the administrator.
*/
void home_page(void){
  char *zPageName = db_get("project-name",0);
  char *zIndexPage = db_get("index-page",0);
  login_check_credentials();
  if( zIndexPage ){
    const char *zPathInfo = P("PATH_INFO");
    while( zIndexPage[0]=='/' ) zIndexPage++;
    while( zPathInfo[0]=='/' ) zPathInfo++;
    if( fossil_strcmp(zIndexPage, zPathInfo)==0 ) zIndexPage = 0;
  }
  if( zIndexPage ){
    cgi_redirectf("%s/%s", g.zTop, zIndexPage);
  }
  if( !g.perm.RdWiki ){
    cgi_redirectf("%s/login?g=%s/home", g.zTop, g.zTop);
  }
  if( zPageName ){
    login_check_credentials();
    g.zExtra = zPageName;
    cgi_set_parameter_nocopy("name", g.zExtra, 1);
    g.isHome = 1;
    wiki_page();
    return;
  }
  style_header("Home");
  @ <p>This is a stub home-page for the project.
  @ To fill in this page, first go to
  @ %z(href("%R/setup_config"))setup/config</a>
  @ and establish a "Project Name".  Then create a
  @ wiki page with that name.  The content of that wiki page
  @ will be displayed in place of this message.</p>
  style_footer();
}

/*
** Return true if the given pagename is the name of the sandbox
*/
static int is_sandbox(const char *zPagename){
  return fossil_stricmp(zPagename,"sandbox")==0 ||
         fossil_stricmp(zPagename,"sand box")==0;
}

/*
** Formal, common and short names for the various wiki styles.
*/
static const char *const azStyles[] = {
  "text/x-fossil-wiki", "Fossil Wiki", "wiki",
  "text/x-markdown",    "Markdown",    "markdown",
  "text/plain",         "Plain Text",  "plain"
};

/*
** Only allow certain mimetypes through.
** All others become "text/x-fossil-wiki"
*/
const char *wiki_filter_mimetypes(const char *zMimetype){
  if( zMimetype!=0 ){
    int i;
    for(i=0; i<count(azStyles); i+=3){
      if( fossil_strcmp(zMimetype,azStyles[i+2])==0 ){
        return azStyles[i];
      }
    }
    if(  fossil_strcmp(zMimetype, "text/x-markdown")==0
        || fossil_strcmp(zMimetype, "text/plain")==0 ){
      return zMimetype;
    }
  }
  return "text/x-fossil-wiki";
}

/*
** Render wiki text according to its mimetype.
**
**   text/x-fossil-wiki      Fossil wiki
**   text/x-markdown         Markdown
**   anything else...        Plain text
*/
void wiki_render_by_mimetype(Blob *pWiki, const char *zMimetype){
  if( zMimetype==0 || fossil_strcmp(zMimetype, "text/x-fossil-wiki")==0 ){
    wiki_convert(pWiki, 0, 0);
  }else if( fossil_strcmp(zMimetype, "text/x-markdown")==0 ){
    Blob tail = BLOB_INITIALIZER;
    markdown_to_html(pWiki, 0, &tail);
    @ %s(blob_str(&tail))
    blob_reset(&tail);
  }else{
    @ <pre>
    @ %h(blob_str(pWiki))
    @ </pre>
  }
}

/*
** WEBPAGE: md_rules
**
** Show a summary of the Markdown wiki formatting rules.
*/
void markdown_rules_page(void){
  Blob x;
  int fTxt = P("txt")!=0;
  style_header("Markdown Formatting Rules");
  if( fTxt ){
    style_submenu_element("Formatted", "%R/md_rules");
  }else{
    style_submenu_element("Plain-Text", "%R/md_rules?txt=1");
  }
  blob_init(&x, builtin_text("markdown.md"), -1);
  wiki_render_by_mimetype(&x, fTxt ? "text/plain" : "text/x-markdown");
  blob_reset(&x);
  style_footer();
}

/*
** Returns non-zero if moderation is required for wiki changes and wiki
** attachments.
*/
int wiki_need_moderation(
  int localUser /* Are we being called for a local interactive user? */
){
  /*
  ** If the FOSSIL_FORCE_WIKI_MODERATION variable is set, *ALL* changes for
  ** wiki pages will be required to go through moderation (even those performed
  ** by the local interactive user via the command line).  This can be useful
  ** for local (or remote) testing of the moderation subsystem and its impact
  ** on the contents and status of wiki pages.
  */
  if( fossil_getenv("FOSSIL_FORCE_WIKI_MODERATION")!=0 ){
    return 1;
  }
  if( localUser ){
    return 0;
  }
  return g.perm.ModWiki==0 && db_get_boolean("modreq-wiki",0)==1;
}

/* Standard submenu items for wiki pages */
#define W_SRCH        0x00001
#define W_LIST        0x00002
#define W_HELP        0x00004
#define W_NEW         0x00008
#define W_BLOG        0x00010
#define W_SANDBOX     0x00020
#define W_ALL         0x0001f
#define W_ALL_BUT(x)  (W_ALL&~(x))

/*
** Add some standard submenu elements for wiki screens.
*/
static void wiki_standard_submenu(unsigned int ok){
  if( (ok & W_SRCH)!=0 && search_restrict(SRCH_WIKI)!=0 ){
    style_submenu_element("Search", "%R/wikisrch");
  }
  if( (ok & W_LIST)!=0 ){
    style_submenu_element("List", "%R/wcontent");
  }
  if( (ok & W_HELP)!=0 ){
    style_submenu_element("Help", "%R/wikihelp");
  }
  if( (ok & W_NEW)!=0 && g.anon.NewWiki ){
    style_submenu_element("New", "%R/wikinew");
  }
#if 0
  if( (ok & W_BLOG)!=0
#endif
  if( (ok & W_SANDBOX)!=0 ){
    style_submenu_element("Sandbox", "%R/wiki?name=Sandbox");
  }
}

/*
** WEBPAGE: wikihelp
** A generic landing page for wiki.
*/
void wiki_helppage(void){
  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  style_header("Wiki Help");
  wiki_standard_submenu(W_ALL_BUT(W_HELP));
  @ <h2>Wiki Links</h2>
  @ <ul>
  { char *zWikiHomePageName = db_get("index-page",0);
    if( zWikiHomePageName ){
      @ <li> %z(href("%R%s",zWikiHomePageName))
      @      %h(zWikiHomePageName)</a> wiki home page.</li>
    }
  }
  { char *zHomePageName = db_get("project-name",0);
    if( zHomePageName ){
      @ <li> %z(href("%R/wiki?name=%t",zHomePageName))
      @      %h(zHomePageName)</a> project home page.</li>
    }
  }
  @ <li> %z(href("%R/timeline?y=w"))Recent changes</a> to wiki pages.</li>
  @ <li> Formatting rules for %z(href("%R/wiki_rules"))Fossil Wiki</a> and for
  @ %z(href("%R/md_rules"))Markdown Wiki</a>.</li>
  @ <li> Use the %z(href("%R/wiki?name=Sandbox"))Sandbox</a>
  @      to experiment.</li>
  if( g.anon.NewWiki ){
    @ <li>  Create a %z(href("%R/wikinew"))new wiki page</a>.</li>
    if( g.anon.Write ){
      @ <li>   Create a %z(href("%R/technoteedit"))new tech-note</a>.</li>
    }
  }
  @ <li> %z(href("%R/wcontent"))List of All Wiki Pages</a>
  @      available on this server.</li>
  if( g.anon.ModWiki ){
    @ <li> %z(href("%R/modreq"))Tend to pending moderation requests</a></li>
  }
  if( search_restrict(SRCH_WIKI)!=0 ){
    @ <li> %z(href("%R/wikisrch"))Search</a> for wiki pages containing key
    @ words</li>
  }
  @ </ul>
  style_footer();
  return;
}

/*
** WEBPAGE: wikisrch
** Usage:  /wikisrch?s=PATTERN
**
** Full-text search of all current wiki text
*/
void wiki_srchpage(void){
  login_check_credentials();
  style_header("Wiki Search");
  wiki_standard_submenu(W_HELP|W_LIST|W_SANDBOX);
  search_screen(SRCH_WIKI, 0);
  style_footer();
}

/*
** WEBPAGE: wiki
** URL: /wiki?name=PAGENAME
*/
void wiki_page(void){
  char *zTag;
  int rid = 0;
  int isSandbox;
  char *zUuid;
  unsigned submenuFlags = W_ALL;
  Blob wiki;
  Manifest *pWiki = 0;
  const char *zPageName;
  const char *zMimetype = 0;
  char *zBody = mprintf("%s","<i>Empty Page</i>");

  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  zPageName = P("name");
  if( zPageName==0 ){
    if( search_restrict(SRCH_WIKI)!=0 ){
      wiki_srchpage();
    }else{
      wiki_helppage();
    }
    return;
  }
  if( check_name(zPageName) ) return;
  isSandbox = is_sandbox(zPageName);
  if( isSandbox ){
    submenuFlags &= ~W_SANDBOX;
    zBody = db_get("sandbox",zBody);
    zMimetype = db_get("sandbox-mimetype","text/x-fossil-wiki");
    rid = 0;
  }else{
    const char *zUuid = P("id");
    if( zUuid==0 || (rid = symbolic_name_to_rid(zUuid,"w"))==0 ){
      zTag = mprintf("wiki-%s", zPageName);
      rid = db_int(0,
        "SELECT rid FROM tagxref"
        " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
        " ORDER BY mtime DESC", zTag
      );
      free(zTag);
    }
    pWiki = manifest_get(rid, CFTYPE_WIKI, 0);
    if( pWiki ){
      zBody = pWiki->zWiki;
      zMimetype = pWiki->zMimetype;
    }
  }
  zMimetype = wiki_filter_mimetypes(zMimetype);
  if( !g.isHome ){
    if( rid ){
      style_submenu_element("Diff", "%R/wdiff?name=%T&a=%d", zPageName, rid);
      zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      style_submenu_element("Details", "%R/info/%s", zUuid);
    }
    if( (rid && g.anon.WrWiki) || (!rid && g.anon.NewWiki) ){
      if( db_get_boolean("wysiwyg-wiki", 0) ){
        style_submenu_element("Edit", "%s/wikiedit?name=%T&wysiwyg=1",
             g.zTop, zPageName);
      }else{
        style_submenu_element("Edit", "%s/wikiedit?name=%T", g.zTop, zPageName);
      }
    }
    if( rid && g.anon.ApndWiki && g.anon.Attach ){
      style_submenu_element("Attach",
           "%s/attachadd?page=%T&from=%s/wiki%%3fname=%T",
           g.zTop, zPageName, g.zTop, zPageName);
    }
    if( rid && g.anon.ApndWiki ){
      style_submenu_element("Append", "%s/wikiappend?name=%T&mimetype=%s",
           g.zTop, zPageName, zMimetype);
    }
    if( g.perm.Hyperlink ){
      style_submenu_element("History", "%s/whistory?name=%T",
           g.zTop, zPageName);
    }
  }
  style_set_current_page("%T?name=%T", g.zPath, zPageName);
  style_header("%s", zPageName);
  wiki_standard_submenu(submenuFlags);
  blob_init(&wiki, zBody, -1);
  wiki_render_by_mimetype(&wiki, zMimetype);
  blob_reset(&wiki);
  attachment_list(zPageName, "<hr /><h2>Attachments:</h2><ul>");
  manifest_destroy(pWiki);
  style_footer();
}

/*
** Write a wiki artifact into the repository
*/
static void wiki_put(Blob *pWiki, int parent, int needMod){
  int nrid;
  if( !needMod ){
    nrid = content_put_ex(pWiki, 0, 0, 0, 0);
    if( parent) content_deltify(parent, &nrid, 1, 0);
  }else{
    nrid = content_put_ex(pWiki, 0, 0, 0, 1);
    moderation_table_create();
    db_multi_exec("INSERT INTO modreq(objid) VALUES(%d)", nrid);
  }
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", nrid);
  db_multi_exec("INSERT OR IGNORE INTO unclustered VALUES(%d);", nrid);
  manifest_crosslink(nrid, pWiki, MC_NONE);
}

/*
** Output a selection box from which the user can select the
** wiki mimetype.
*/
void mimetype_option_menu(const char *zMimetype){
  unsigned i;
  @ <select name="mimetype" size="1">
  for(i=0; i<count(azStyles); i+=3){
    if( fossil_strcmp(zMimetype,azStyles[i])==0 ){
      @ <option value="%s(azStyles[i])" selected>%s(azStyles[i+1])</option>
    }else{
      @ <option value="%s(azStyles[i])">%s(azStyles[i+1])</option>
    }
  }
  @ </select>
}

/*
** Given a mimetype, return its common name.
*/
static const char *mimetype_common_name(const char *zMimetype){
  int i;
  for(i=4; i>=2; i-=2){
    if( zMimetype && fossil_strcmp(zMimetype, azStyles[i])==0 ){
      return azStyles[i+1];
    }
  }
  return azStyles[1];
}

/*
** WEBPAGE: wikiedit
** URL: /wikiedit?name=PAGENAME
**
** Edit a wiki page.
*/
void wikiedit_page(void){
  char *zTag;
  int rid = 0;
  int isSandbox;
  Blob wiki;
  Manifest *pWiki = 0;
  const char *zPageName;
  int n;
  const char *z;
  char *zBody = (char*)P("w");
  const char *zMimetype = wiki_filter_mimetypes(P("mimetype"));
  int isWysiwyg = P("wysiwyg")!=0;
  int goodCaptcha = 1;

  if( P("edit-wysiwyg")!=0 ){ isWysiwyg = 1; zBody = 0; }
  if( P("edit-markup")!=0 ){ isWysiwyg = 0; zBody = 0; }
  if( zBody ){
    if( isWysiwyg ){
      Blob body;
      blob_zero(&body);
      htmlTidy(zBody, &body);
      zBody = blob_str(&body);
    }else{
      zBody = mprintf("%s", zBody);
    }
  }
  login_check_credentials();
  zPageName = PD("name","");
  if( check_name(zPageName) ) return;
  isSandbox = is_sandbox(zPageName);
  if( isSandbox ){
    if( !g.perm.WrWiki ){
      login_needed(g.anon.WrWiki);
      return;
    }
    if( zBody==0 ){
      zBody = db_get("sandbox","");
      zMimetype = db_get("sandbox-mimetype","text/x-fossil-wiki");
    }
  }else{
    zTag = mprintf("wiki-%s", zPageName);
    rid = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
      " ORDER BY mtime DESC", zTag
    );
    free(zTag);
    if( (rid && !g.perm.WrWiki) || (!rid && !g.perm.NewWiki) ){
      login_needed(rid ? g.anon.WrWiki : g.anon.NewWiki);
      return;
    }
    if( zBody==0 && (pWiki = manifest_get(rid, CFTYPE_WIKI, 0))!=0 ){
      zBody = pWiki->zWiki;
      zMimetype = pWiki->zMimetype;
    }
  }
  if( P("submit")!=0 && zBody!=0
   && (goodCaptcha = captcha_is_correct())
  ){
    char *zDate;
    Blob cksum;
    blob_zero(&wiki);
    db_begin_transaction();
    if( isSandbox ){
      db_set("sandbox",zBody,0);
      db_set("sandbox-mimetype",zMimetype,0);
    }else{
      login_verify_csrf_secret();
      zDate = date_in_standard_format("now");
      blob_appendf(&wiki, "D %s\n", zDate);
      free(zDate);
      blob_appendf(&wiki, "L %F\n", zPageName);
      if( fossil_strcmp(zMimetype,"text/x-fossil-wiki")!=0 ){
        blob_appendf(&wiki, "N %s\n", zMimetype);
      }
      if( rid ){
        char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
        blob_appendf(&wiki, "P %s\n", zUuid);
        free(zUuid);
      }
      if( !login_is_nobody() ){
        blob_appendf(&wiki, "U %F\n", login_name());
      }
      blob_appendf(&wiki, "W %d\n%s\n", strlen(zBody), zBody);
      md5sum_blob(&wiki, &cksum);
      blob_appendf(&wiki, "Z %b\n", &cksum);
      blob_reset(&cksum);
      wiki_put(&wiki, 0, wiki_need_moderation(0));
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
  style_set_current_page("%T?name=%T", g.zPath, zPageName);
  style_header("Edit: %s", zPageName);
  if( !goodCaptcha ){
    @ <p class="generalError">Error:  Incorrect security code.</p>
  }
  blob_zero(&wiki);
  blob_append(&wiki, zBody, -1);
  if( P("preview")!=0 ){
    @ Preview:<hr />
    wiki_render_by_mimetype(&wiki, zMimetype);
    @ <hr />
    blob_reset(&wiki);
  }
  for(n=2, z=zBody; z[0]; z++){
    if( z[0]=='\n' ) n++;
  }
  if( n<20 ) n = 20;
  if( n>30 ) n = 30;
  if( !isWysiwyg ){
    /* Traditional markup-only editing */
    form_begin(0, "%R/wikiedit");
    @ <div>Markup style:
    mimetype_option_menu(zMimetype);
    @ <br /><textarea name="w" class="wikiedit" cols="80"
    @  rows="%d(n)" wrap="virtual">%h(zBody)</textarea>
    @ <br />
    if( db_get_boolean("wysiwyg-wiki", 0) ){
      @ <input type="submit" name="edit-wysiwyg" value="Wysiwyg Editor"
      @  onclick='return confirm("Switching to WYSIWYG-mode\nwill erase your markup\nedits. Continue?")' />
    }
    @ <input type="submit" name="preview" value="Preview Your Changes" />
  }else{
    /* Wysiwyg editing */
    Blob html, temp;
    form_begin("onsubmit='wysiwygSubmit()'", "%R/wikiedit");
    @ <div>
    @ <input type="hidden" name="wysiwyg" value="1" />
    blob_zero(&temp);
    wiki_convert(&wiki, &temp, 0);
    blob_zero(&html);
    htmlTidy(blob_str(&temp), &html);
    blob_reset(&temp);
    wysiwygEditor("w", blob_str(&html), 60, n);
    blob_reset(&html);
    @ <br />
    @ <input type="submit" name="edit-markup" value="Markup Editor"
    @  onclick='return confirm("Switching to markup-mode\nwill erase your WYSIWYG\nedits. Continue?")' />
  }
  login_insert_csrf_secret();
  @ <input type="submit" name="submit" value="Apply These Changes" />
  @ <input type="hidden" name="name" value="%h(zPageName)" />
  @ <input type="submit" name="cancel" value="Cancel"
  @  onclick='confirm("Abandon your changes?")' />
  @ </div>
  captcha_generate(0);
  @ </form>
  manifest_destroy(pWiki);
  blob_reset(&wiki);
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
  const char *zMimetype;
  login_check_credentials();
  if( !g.perm.NewWiki ){
    login_needed(g.anon.NewWiki);
    return;
  }
  zName = PD("name","");
  zMimetype = wiki_filter_mimetypes(P("mimetype"));
  if( zName[0] && wiki_name_is_wellformed((const unsigned char *)zName) ){
    if( fossil_strcmp(zMimetype,"text/x-fossil-wiki")==0
     && db_get_boolean("wysiwyg-wiki", 0)
    ){
      cgi_redirectf("wikiedit?name=%T&wysiwyg=1", zName);
    }else{
      cgi_redirectf("wikiedit?name=%T&mimetype=%s", zName, zMimetype);
    }
  }
  style_header("Create A New Wiki Page");
  wiki_standard_submenu(W_ALL_BUT(W_NEW));
  @ <p>Rules for wiki page names:</p>
  well_formed_wiki_name_rules();
  form_begin(0, "%R/wikinew");
  @ <p>Name of new wiki page:
  @ <input style="width: 35;" type="text" name="name" value="%h(zName)" /><br />
  @ Markup style:
  mimetype_option_menu("text/x-fossil-wiki");
  @ <br /><input type="submit" value="Create" />
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
static void appendRemark(Blob *p, const char *zMimetype){
  char *zDate;
  const char *zUser;
  const char *zRemark;
  char *zId;

  zDate = db_text(0, "SELECT datetime('now')");
  zRemark = PD("r","");
  zUser = PD("u",g.zLogin);
  if( fossil_strcmp(zMimetype, "text/x-fossil-wiki")==0 ){
    zId = db_text(0, "SELECT lower(hex(randomblob(8)))");
    blob_appendf(p, "\n\n<hr /><div id=\"%s\"><i>On %s UTC %h",
      zId, zDate, login_name());
    if( zUser[0] && fossil_strcmp(zUser,login_name()) ){
      blob_appendf(p, " (claiming to be %h)", zUser);
    }
    blob_appendf(p, " added:</i><br />\n%s</div id=\"%s\">", zRemark, zId);
  }else if( fossil_strcmp(zMimetype, "text/x-markdown")==0 ){
    blob_appendf(p, "\n\n------\n*On %s UTC %h", zDate, login_name());
    if( zUser[0] && fossil_strcmp(zUser,login_name()) ){
      blob_appendf(p, " (claiming to be %h)", zUser);
    }
    blob_appendf(p, " added:*\n\n%s\n", zRemark);
  }else{
    blob_appendf(p, "\n\n------------------------------------------------\n"
                    "On %s UTC %s", zDate, login_name());
    if( zUser[0] && fossil_strcmp(zUser,login_name()) ){
      blob_appendf(p, " (claiming to be %s)", zUser);
    }
    blob_appendf(p, " added:\n\n%s\n", zRemark);
  }
  fossil_free(zDate);
}

/*
** WEBPAGE: wikiappend
** URL: /wikiappend?name=PAGENAME&mimetype=MIMETYPE
**
** Append text to the end of a wiki page.
*/
void wikiappend_page(void){
  char *zTag;
  int rid = 0;
  int isSandbox;
  const char *zPageName;
  const char *zUser;
  const char *zMimetype;
  int goodCaptcha = 1;
  const char *zFormat;

  login_check_credentials();
  zPageName = PD("name","");
  zMimetype = wiki_filter_mimetypes(P("mimetype"));
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
  if( !g.perm.ApndWiki ){
    login_needed(g.anon.ApndWiki);
    return;
  }
  if( P("submit")!=0 && P("r")!=0 && P("u")!=0
   && (goodCaptcha = captcha_is_correct())
  ){
    char *zDate;
    Blob cksum;
    Blob body;
    Blob wiki;
    Manifest *pWiki = 0;

    blob_zero(&body);
    if( isSandbox ){
      blob_append(&body, db_get("sandbox",""), -1);
      appendRemark(&body, zMimetype);
      db_set("sandbox", blob_str(&body), 0);
    }else{
      login_verify_csrf_secret();
      pWiki = manifest_get(rid, CFTYPE_WIKI, 0);
      if( pWiki ){
        blob_append(&body, pWiki->zWiki, -1);
        manifest_destroy(pWiki);
      }
      blob_zero(&wiki);
      db_begin_transaction();
      zDate = date_in_standard_format("now");
      blob_appendf(&wiki, "D %s\n", zDate);
      blob_appendf(&wiki, "L %F\n", zPageName);
      if( fossil_strcmp(zMimetype, "text/x-fossil-wiki")!=0 ){
        blob_appendf(&wiki, "N %s\n", zMimetype);
      }
      if( rid ){
        char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
        blob_appendf(&wiki, "P %s\n", zUuid);
        free(zUuid);
      }
      if( !login_is_nobody() ){
        blob_appendf(&wiki, "U %F\n", login_name());
      }
      appendRemark(&body, zMimetype);
      blob_appendf(&wiki, "W %d\n%s\n", blob_size(&body), blob_str(&body));
      md5sum_blob(&wiki, &cksum);
      blob_appendf(&wiki, "Z %b\n", &cksum);
      blob_reset(&cksum);
      wiki_put(&wiki, rid, wiki_need_moderation(0));
      db_end_transaction(0);
    }
    cgi_redirectf("wiki?name=%T", zPageName);
  }
  if( P("cancel")!=0 ){
    cgi_redirectf("wiki?name=%T", zPageName);
    return;
  }
  style_set_current_page("%T?name=%T", g.zPath, zPageName);
  style_header("Append Comment To: %s", zPageName);
  if( !goodCaptcha ){
    @ <p class="generalError">Error: Incorrect security code.</p>
  }
  if( P("preview")!=0 ){
    Blob preview;
    blob_zero(&preview);
    appendRemark(&preview, zMimetype);
    @ Preview:<hr />
    wiki_render_by_mimetype(&preview, zMimetype);
    @ <hr />
    blob_reset(&preview);
  }
  zUser = PD("u", g.zLogin);
  form_begin(0, "%R/wikiappend");
  login_insert_csrf_secret();
  @ <input type="hidden" name="name" value="%h(zPageName)" />
  @ <input type="hidden" name="mimetype" value="%h(zMimetype)" />
  @ Your Name:
  @ <input type="text" name="u" size="20" value="%h(zUser)" /><br />
  zFormat = mimetype_common_name(zMimetype);
  @ Comment to append (formatted as %s(zFormat)):<br />
  @ <textarea name="r" class="wikiedit" cols="80"
  @  rows="10" wrap="virtual">%h(PD("r",""))</textarea>
  @ <br />
  @ <input type="submit" name="preview" value="Preview Your Comment" />
  @ <input type="submit" name="submit" value="Append Your Changes" />
  @ <input type="submit" name="cancel" value="Cancel" />
  captcha_generate(0);
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
    @ %z(href("%R/wdiff?name=%t&a=%d",zWikiPageName,rid))[diff]</a>
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
  const char *zPageName;
  login_check_credentials();
  if( !g.perm.Hyperlink ){ login_needed(g.anon.Hyperlink); return; }
  zPageName = PD("name","");
  style_header("History Of %s", zPageName);

  db_prepare(&q, "%s AND event.objid IN "
                 "  (SELECT rid FROM tagxref WHERE tagid="
                       "(SELECT tagid FROM tag WHERE tagname='wiki-%q')"
                 "   UNION SELECT attachid FROM attachment"
                          " WHERE target=%Q)"
                 "ORDER BY mtime DESC",
                 timeline_query_for_www(), zPageName, zPageName);
  zWikiPageName = zPageName;
  www_print_timeline(&q, TIMELINE_ARTID, 0, 0, 0, wiki_history_extra);
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
  int rid1, rid2;
  const char *zPageName;
  Manifest *pW1, *pW2 = 0;
  Blob w1, w2, d;
  u64 diffFlags;

  login_check_credentials();
  rid1 = atoi(PD("a","0"));
  if( !g.perm.Hyperlink ){ login_needed(g.anon.Hyperlink); return; }
  if( rid1==0 ) fossil_redirect_home();
  rid2 = atoi(PD("b","0"));
  zPageName = PD("name","");
  style_header("Changes To %s", zPageName);

  if( rid2==0 ){
    rid2 = db_int(0,
      "SELECT objid FROM event JOIN tagxref ON objid=rid AND tagxref.tagid="
                        "(SELECT tagid FROM tag WHERE tagname='wiki-%q')"
      " WHERE event.mtime<(SELECT mtime FROM event WHERE objid=%d)"
      " ORDER BY event.mtime DESC LIMIT 1",
      zPageName, rid1
    );
  }
  pW1 = manifest_get(rid1, CFTYPE_WIKI, 0);
  if( pW1==0 ) fossil_redirect_home();
  blob_init(&w1, pW1->zWiki, -1);
  blob_zero(&w2);
  if( rid2 && (pW2 = manifest_get(rid2, CFTYPE_WIKI, 0))!=0 ){
    blob_init(&w2, pW2->zWiki, -1);
  }
  blob_zero(&d);
  diffFlags = construct_diff_flags(1,0);
  text_diff(&w2, &w1, &d, 0, diffFlags | DIFF_HTML | DIFF_LINENO);
  @ <pre class="udiff">
  @ %s(blob_str(&d))
  @ <pre>
  manifest_destroy(pW1);
  manifest_destroy(pW2);
  style_footer();
}

/*
** prepare()s pStmt with a query requesting:
**
** - wiki page name
** - tagxref (whatever that really is!)
**
** Used by wcontent_page() and the JSON wiki code.
*/
void wiki_prepare_page_list( Stmt * pStmt ){
  db_prepare(pStmt,
    "SELECT"
    "  substr(tagname, 6) as name,"
    "  (SELECT value FROM tagxref WHERE tagid=tag.tagid"
    "    ORDER BY mtime DESC) as tagXref"
    "  FROM tag WHERE tagname GLOB 'wiki-*'"
    " ORDER BY lower(tagname) /*sort*/"
  );
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
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  style_header("Available Wiki Pages");
  if( showAll ){
    style_submenu_element("Active", "%s/wcontent", g.zTop);
  }else{
    style_submenu_element("All", "%s/wcontent?all=1", g.zTop);
  }
  wiki_standard_submenu(W_ALL_BUT(W_LIST));
  @ <ul>
  wiki_prepare_page_list(&q);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    int size = db_column_int(&q, 1);
    if( size>0 ){
      @ <li>%z(href("%R/wiki?name=%T",zName))%h(zName)</a></li>
    }else if( showAll ){
      @ <li>%z(href("%R/wiki?name=%T",zName))<s>%h(zName)</s></a></li>
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
  const char *zTitle;
  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  zTitle = PD("title","*");
  style_header("Wiki Pages Found");
  @ <ul>
  db_prepare(&q,
    "SELECT substr(tagname, 6, 1000) FROM tag WHERE tagname like 'wiki-%%%q%%'"
    " ORDER BY lower(tagname) /*sort*/" ,
    zTitle);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    @ <li>%z(href("%R/wiki?name=%T",zName))%h(zName)</a></li>
  }
  db_finalize(&q);
  @ </ul>
  style_footer();
}

/*
** WEBPAGE: wiki_rules
**
** Show the formatting rules for Fossil wiki.
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
  @ <li>Indented paragraphs begin with a tab or two spaces.</li>
  @ <li>Hyperlinks are contained with square brackets:  "[target]" or
  @ "[target|name]".</li>
  @ <li>Most ordinary HTML works.</li>
  @ <li>&lt;verbatim&gt; and &lt;nowiki&gt;.</li>
  @ </ol>
  @ <p>We call the first five rules above "wiki" formatting rules.  The
  @ last two rules are the HTML formatting rule.</p>
  @ <h2>Formatting Rule Details</h2>
  @ <ol>
  @ <li> <p><span class="wikiruleHead">Paragraphs</span>.
  @ Any sequence of one or more blank lines forms
  @ a paragraph break.  Centered or right-justified paragraphs are not
  @ supported by wiki markup, but you can do these things if you need them
  @ using HTML.</p></li>
  @ <li> <p><span class="wikiruleHead">Bullet Lists</span>.
  @ A bullet list item is a line that begins with a single "*" character
  @ surrounded on
  @ both sides by two or more spaces or by a tab.  Only a single level
  @ of bullet list is supported by wiki.  For nested lists, use HTML.</p></li>
  @ <li> <p><span class="wikiruleHead">Enumeration Lists</span>.
  @ An enumeration list item is a line that begins with a single "#" character
  @ surrounded on both sides by two or more spaces or by a tab.  Only a single
  @ level of enumeration list is supported by wiki.  For nested lists or for
  @ enumerations that count using letters or roman numerials, use HTML.</p></li>
  @ <li> <p><span class="wikiruleHead">Indented Paragraphs</span>.
  @ Any paragraph that begins with two or more spaces or a tab and
  @ which is not a bullet or enumeration list item is rendered
  @ indented.  Only a single level of indentation is supported by wiki; use
  @ HTML for deeper indentation.</p></li>
  @ <li> <p><span class="wikiruleHead">Hyperlinks</span>.
  @ Text within square brackets ("[...]") becomes a hyperlink.  The
  @ target can be a wiki page name, the artifact ID of a check-in or ticket,
  @ the name of an image, or a URL.  By default, the target is displayed
  @ as the text of the hyperlink.  But you can specify alternative text
  @ after the target name separated by a "|" character.</p>
  @ <p>You can also link to internal anchor names using [#anchor-name],
  @ providing
  @ you have added the necessary "&lt;a name='anchor-name'&gt;&lt;/a&gt;"
  @ tag to your wiki page.</p></li>
  @ <li> <p><span class="wikiruleHead">HTML</span>.
  @ The following standard HTML elements may be used:
  show_allowed_wiki_markup();
  @ . There are two non-standard elements available:
  @ &lt;verbatim&gt; and &lt;nowiki&gt;.
  @ No other elements are allowed.  All attributes are checked and
  @ only a few benign attributes are allowed on each element.
  @ In particular, any attributes that specify javascript or CSS
  @ are elided.</p></li>
  @ <li><p><span class="wikiruleHead">Special Markup.</span>
  @ The &lt;nowiki&gt; tag disables all wiki formatting rules
  @ through the matching &lt;/nowiki&gt; element.
  @ The &lt;verbatim&gt; tag works like &lt;pre&gt; with the addition
  @ that it also disables all wiki and HTML markup
  @ through the matching &lt;/verbatim&gt;.</p></li>
  @ </ol>
  style_footer();
}

/*
** Add a new wiki page to the repository.  The page name is
** given by the zPageName parameter.  rid must be zero to create
** a new page otherwise the page identified by rid is updated.
**
** The content of the new page is given by the blob pContent.
**
** zMimeType specifies the N-card for the wiki page. If it is 0,
** empty, or "text/x-fossil-wiki" (the default format) then it is
** ignored.
*/
int wiki_cmd_commit(const char *zPageName, int rid, Blob *pContent,
                    const char *zMimeType, int localUser){
  Blob wiki;              /* Wiki page content */
  Blob cksum;             /* wiki checksum */
  char *zDate;            /* timestamp */
  char *zUuid;            /* uuid for rid */

  blob_zero(&wiki);
  zDate = date_in_standard_format("now");
  blob_appendf(&wiki, "D %s\n", zDate);
  free(zDate);
  blob_appendf(&wiki, "L %F\n", zPageName );
  if( zMimeType && *zMimeType
      && 0!=fossil_strcmp(zMimeType,"text/x-fossil-wiki") ){
    blob_appendf(&wiki, "N %F\n", zMimeType);
  }
  if( rid ){
    zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    blob_appendf(&wiki, "P %s\n", zUuid);
    free(zUuid);
  }
  user_select();
  if( !login_is_nobody() ){
      blob_appendf(&wiki, "U %F\n", login_name());
  }
  blob_appendf( &wiki, "W %d\n%s\n", blob_size(pContent),
                blob_str(pContent) );
  md5sum_blob(&wiki, &cksum);
  blob_appendf(&wiki, "Z %b\n", &cksum);
  blob_reset(&cksum);
  db_begin_transaction();
  wiki_put(&wiki, 0, wiki_need_moderation(localUser));
  db_end_transaction(0);
  return 1;
}

/*
** Determine the rid for a tech note given either its id or its
** timestamp. Returns 0 if there is no such item and -1 if the details
** are ambiguous and could refer to multiple items.
*/
int wiki_technote_to_rid(const char *zETime) {
  int rid=0;                    /* Artifact ID of the tech note */
  int nETime = strlen(zETime);
  Stmt q;
  if( nETime>=4 && nETime<=HNAME_MAX && validate16(zETime, nETime) ){
    char zUuid[HNAME_MAX+1];
    memcpy(zUuid, zETime, nETime+1);
    canonical16(zUuid, nETime);
    db_prepare(&q,
      "SELECT e.objid"
      "  FROM event e, tag t"
      " WHERE e.type='e' AND e.tagid IS NOT NULL AND t.tagid=e.tagid"
      "   AND t.tagname GLOB 'event-%q*'",
      zUuid
    );
    if( db_step(&q)==SQLITE_ROW ){
      rid = db_column_int(&q, 0);
      if( db_step(&q)==SQLITE_ROW ) rid = -1;
    }
    db_finalize(&q);
  }
  if (!rid) {
    if (strlen(zETime)>4) {
      rid = db_int(0, "SELECT objid"
                      "  FROM event"
                      " WHERE datetime(mtime)=datetime('%q')"
                      "   AND type='e'"
                      "   AND tagid IS NOT NULL"
                      " ORDER BY objid DESC LIMIT 1",
                   zETime);
    }
  }
  return rid;
}

/*
** COMMAND: wiki*
**
** Usage: %fossil wiki (export|create|commit|list) WikiName
**
** Run various subcommands to work with wiki entries or tech notes.
**
**    %fossil wiki export PAGENAME ?FILE?
**    %fossil wiki export ?FILE? -t|--technote DATETIME|TECHNOTE-ID
**
**       Sends the latest version of either a wiki page or of a tech note
**       to the given file or standard output.
**       If PAGENAME is provided, the wiki page will be output. For
**       a tech note either DATETIME or TECHNOTE-ID must be specified. If
**       DATETIME is used, the most recently modified tech note with that
**       DATETIME will be sent.
**
**    %fossil wiki (create|commit) PAGENAME ?FILE? ?OPTIONS?
**
**       Create a new or commit changes to an existing wiki page or
**       technote from FILE or from standard input. PAGENAME is the
**       name of the wiki entry or the timeline comment of the
**       technote.
**
**       Options:
**         -M|--mimetype TEXT-FORMAT   The mime type of the update.
**                                     Defaults to the type used by
**                                     the previous version of the
**                                     page, or text/x-fossil-wiki.
**                                     Valid values are: text/x-fossil-wiki,
**                                     text/markdown and text/plain. fossil,
**                                     markdown or plain can be specified as
**                                     synonyms of these values.
**         -t|--technote DATETIME      Specifies the timestamp of
**                                     the technote to be created or
**                                     updated. When updating a tech note
**                                     the most recently modified tech note
**                                     with the specified timestamp will be
**                                     updated.
**         -t|--technote TECHNOTE-ID   Specifies the technote to be
**                                     updated by its technote id.
**         --technote-tags TAGS        The set of tags for a technote.
**         --technote-bgcolor COLOR    The color used for the technote
**                                     on the timeline.
**
**    %fossil wiki list ?OPTIONS?
**    %fossil wiki ls ?OPTIONS?
**
**       Lists all wiki entries, one per line, ordered
**       case-insensitively by name.
**
**       Options:
**         -t|--technote               Technotes will be listed instead of
**                                     pages. The technotes will be in order
**                                     of timestamp with the most recent
**                                     first.
**         -s|--show-technote-ids      The id of the tech note will be listed
**                                     along side the timestamp. The tech note
**                                     id will be the first word on each line.
**                                     This option only applies if the
**                                     --technote option is also specified.
**
** DATETIME may be "now" or "YYYY-MM-DDTHH:MM:SS.SSS". If in
** year-month-day form, it may be truncated, the "T" may be replaced by
** a space, and it may also name a timezone offset from UTC as "-HH:MM"
** (westward) or "+HH:MM" (eastward). Either no timezone suffix or "Z"
** means UTC.
**
*/
void wiki_cmd(void){
  int n;
  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    goto wiki_cmd_usage;
  }
  n = strlen(g.argv[2]);
  if( n==0 ){
    goto wiki_cmd_usage;
  }

  if( strncmp(g.argv[2],"export",n)==0 ){
    const char *zPageName;        /* Name of the wiki page to export */
    const char *zFile;            /* Name of the output file (0=stdout) */
    const char *zETime;           /* The name of the technote to export */
    int rid;                      /* Artifact ID of the wiki page */
    int i;                        /* Loop counter */
    char *zBody = 0;              /* Wiki page content */
    Blob body;                    /* Wiki page content */
    Manifest *pWiki = 0;          /* Parsed wiki page content */

    zETime = find_option("technote","t",1);
    if( !zETime ){
      if( (g.argc!=4) && (g.argc!=5) ){
        usage("export PAGENAME ?FILE?");
      }
      zPageName = g.argv[3];
      rid = db_int(0, "SELECT x.rid FROM tag t, tagxref x"
        " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q'"
        " ORDER BY x.mtime DESC LIMIT 1",
        zPageName
      );
      if( (pWiki = manifest_get(rid, CFTYPE_WIKI, 0))!=0 ){
        zBody = pWiki->zWiki;
      }
      if( zBody==0 ){
        fossil_fatal("wiki page [%s] not found",zPageName);
      }
      zFile = (g.argc==4) ? "-" : g.argv[4];
    }else{
      if( (g.argc!=3) && (g.argc!=4) ){
        usage("export ?FILE? --technote DATETIME|TECHNOTE-ID");
      }
      rid = wiki_technote_to_rid(zETime);
      if ( rid==-1 ){
        fossil_fatal("ambiguous tech note id: %s", zETime);
      }
      if( (pWiki = manifest_get(rid, CFTYPE_EVENT, 0))!=0 ){
        zBody = pWiki->zWiki;
      }
      if( zBody==0 ){
        fossil_fatal("technote [%s] not found",zETime);
      }
      zFile = (g.argc==3) ? "-" : g.argv[3];
    }
    for(i=strlen(zBody); i>0 && fossil_isspace(zBody[i-1]); i--){}
    zBody[i] = 0;
    blob_init(&body, zBody, -1);
    blob_append(&body, "\n", 1);
    blob_write_to_file(&body, zFile);
    blob_reset(&body);
    manifest_destroy(pWiki);
    return;
  }else if( strncmp(g.argv[2],"commit",n)==0
            || strncmp(g.argv[2],"create",n)==0 ){
    const char *zPageName;        /* page name */
    Blob content;                 /* Input content */
    int rid = 0;
    Manifest *pWiki = 0;          /* Parsed wiki page content */
    const char *zMimeType = find_option("mimetype", "M", 1);
    const char *zETime = find_option("technote", "t", 1);
    const char *zTags = find_option("technote-tags", NULL, 1);
    const char *zClr = find_option("technote-bgcolor", NULL, 1);
    if( g.argc!=4 && g.argc!=5 ){
      usage("commit|create PAGENAME ?FILE? [--mimetype TEXT-FORMAT]"
            " [--technote DATETIME] [--technote-tags TAGS]"
            " [--technote-bgcolor COLOR]");
    }
    zPageName = g.argv[3];
    if( g.argc==4 ){
      blob_read_from_channel(&content, stdin, -1);
    }else{
      blob_read_from_file(&content, g.argv[4]);
    }
    if( !zMimeType || !*zMimeType ){
      /* Try to deduce the mime type based on the prior version. */
      if ( !zETime ){
        rid = db_int(0, "SELECT x.rid FROM tag t, tagxref x"
                     " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q'"
                     " ORDER BY x.mtime DESC LIMIT 1",
                     zPageName
                     );
        if( rid>0 && (pWiki = manifest_get(rid, CFTYPE_WIKI, 0))!=0
           && (pWiki->zMimetype && *pWiki->zMimetype) ){
          zMimeType = pWiki->zMimetype;
        }
      }else{
        rid = wiki_technote_to_rid(zETime);
        if( rid>0 && (pWiki = manifest_get(rid, CFTYPE_EVENT, 0))!=0
           && (pWiki->zMimetype && *pWiki->zMimetype) ){
          zMimeType = pWiki->zMimetype;
        }
      }
    }else{
      zMimeType = wiki_filter_mimetypes(zMimeType);
    }
    if( g.argv[2][1]=='r' && rid>0 ){
      if ( !zETime ){
        fossil_fatal("wiki page %s already exists", zPageName);
      }else{
        /* Creating a tech note with same timestamp is permitted
           and should create a new tech note */
        rid = 0;
      }
    }else if( g.argv[2][1]=='o' && rid == 0 ){
      if ( !zETime ){
        fossil_fatal("no such wiki page: %s", zPageName);
      }else{
        fossil_fatal("no such tech note: %s", zETime);
      }
    }

    if( !zETime ){
      wiki_cmd_commit(zPageName, rid, &content, zMimeType, 1);
      if( g.argv[2][1]=='r' ){
        fossil_print("Created new wiki page %s.\n", zPageName);
      }else{
        fossil_print("Updated wiki page %s.\n", zPageName);
      }
    }else{
      if( rid != -1 ){
        char *zMETime;          /* Normalized, mutable version of zETime */
        zMETime = db_text(0, "SELECT coalesce(datetime(%Q),datetime('now'))",
                          zETime);
        event_cmd_commit(zMETime, rid, &content, zMimeType, zPageName,
                         zTags, zClr);
        if( g.argv[2][1]=='r' ){
          fossil_print("Created new tech note %s.\n", zMETime);
        }else{
          fossil_print("Updated tech note %s.\n", zMETime);
        }
        free(zMETime);
      }else{
        fossil_fatal("ambiguous tech note id: %s", zETime);
      }
    }
    manifest_destroy(pWiki);
    blob_reset(&content);
  }else if( strncmp(g.argv[2],"delete",n)==0 ){
    if( g.argc!=5 ){
      usage("delete PAGENAME");
    }
    fossil_fatal("delete not yet implemented.");
  }else if(( strncmp(g.argv[2],"list",n)==0 )
          || ( strncmp(g.argv[2],"ls",n)==0 )){
    Stmt q;
    int showIds = 0;

    if ( !find_option("technote","t",0) ){
      db_prepare(&q,
        "SELECT substr(tagname, 6) FROM tag WHERE tagname GLOB 'wiki-*'"
        " ORDER BY lower(tagname) /*sort*/"
      );
    }else{
      showIds = find_option("show-technote-ids","s",0)!=0;
      db_prepare(&q,
        "SELECT datetime(e.mtime), substr(t.tagname,7)"
         " FROM event e, tag t"
        " WHERE e.type='e'"
          " AND e.tagid IS NOT NULL"
          " AND t.tagid=e.tagid"
        " ORDER BY e.mtime DESC /*sort*/"
      );
    }

    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      if( showIds ){
        const char *zUuid = db_column_text(&q, 1);
        fossil_print("%s ",zUuid);
      }
      fossil_print( "%s\n",zName);
    }
    db_finalize(&q);
  }else{
    goto wiki_cmd_usage;
  }
  return;

wiki_cmd_usage:
  usage("export|create|commit|list ...");
}

/*
** COMMAND: test-markdown-render
**
** Usage: %fossil test-markdown-render FILE
**
** Render markdown wiki from FILE to stdout.
**
*/
void test_markdown_render(void){
  Blob in, out;
  verify_all_options();
  if( g.argc!=3 ) usage("FILE");
  blob_zero(&out);
  blob_read_from_file(&in, g.argv[2]);
  markdown_to_html(&in, 0, &out);
  blob_write_to_file(&out, "-");
}
