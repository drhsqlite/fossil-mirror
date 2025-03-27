/*
** Copyright (c) 2007 D. Richard Hipp
** Copyright (c) 2008 Stephan Beal
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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

#define has_prefix(literal_prfx, zStr) \
  (fossil_strncmp((zStr), "" literal_prfx, (sizeof literal_prfx)-1)==0)

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
    style_set_current_feature("wiki");
    style_header("Wiki Page Name Error");
    @ The wiki name "<span class="wikiError">%h(z)</span>" is not well-formed.
    @ Rules for wiki page names:
    well_formed_wiki_name_rules();
    style_finish_page();
    return 1;
  }
  return 0;
}

/*
** Return the tagid associated with a particular wiki page.
*/
int wiki_tagid(const char *zPageName){
  return db_int(0, "SELECT tagid FROM tag WHERE tagname='wiki-%q'",zPageName);
}
int wiki_tagid2(const char *zPrefix, const char *zPageName){
  return db_int(0, "SELECT tagid FROM tag WHERE tagname='wiki-%q/%q'",
                zPrefix, zPageName);
}

/*
** Return the RID of the next or previous version of a wiki page.
** Return 0 if rid is the last/first version.
*/
int wiki_next(int tagid, double mtime){
  return db_int(0,
     "SELECT srcid FROM tagxref"
     " WHERE tagid=%d AND mtime>%.16g"
     " ORDER BY mtime ASC LIMIT 1",
     tagid, mtime);
}
int wiki_prev(int tagid, double mtime){
  return db_int(0,
     "SELECT srcid FROM tagxref"
     " WHERE tagid=%d AND mtime<%.16g"
     " ORDER BY mtime DESC LIMIT 1",
     tagid, mtime);
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
  cgi_check_for_malice();
  if( zIndexPage ){
    const char *zPathInfo = P("PATH_INFO");
    while( zIndexPage[0]=='/' ) zIndexPage++;
    while( zPathInfo[0]=='/' ) zPathInfo++;
    if( fossil_strcmp(zIndexPage, zPathInfo)==0 ) zIndexPage = 0;
  }
  if( zIndexPage ){
    cgi_redirectf("%R/%s", zIndexPage);
  }
  if( !g.perm.RdWiki ){
    cgi_redirectf("%R/login?g=home");
  }
  if( zPageName ){
    login_check_credentials();
    g.zExtra = zPageName;
    cgi_set_parameter_nocopy("name", g.zExtra, 1);
    g.isHome = 1;
    wiki_page();
    return;
  }
  style_set_current_feature("wiki");
  style_header("Home");
  @ <p>This is a stub home-page for the project.
  @ To fill in this page, first go to
  @ %z(href("%R/setup_config"))setup/config</a>
  @ and establish a "Project Name".  Then create a
  @ wiki page with that name.  The content of that wiki page
  @ will be displayed in place of this message.</p>
  style_finish_page();
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
**   text/x-pikchr           Pikchr
**   anything else...        Plain text
**
** If zMimetype is a null pointer, then use "text/x-fossil-wiki".
*/
void wiki_render_by_mimetype(Blob *pWiki, const char *zMimetype){
  if( zMimetype==0 || fossil_strcmp(zMimetype, "text/x-fossil-wiki")==0 ){
    wiki_convert(pWiki, 0, 0);
  }else if( fossil_strcmp(zMimetype, "text/x-markdown")==0 ){
    Blob tail = BLOB_INITIALIZER;
    markdown_to_html(pWiki, 0, &tail);
    safe_html(&tail);
    @ %s(blob_str(&tail))
    blob_reset(&tail);
  }else if( fossil_strcmp(zMimetype, "text/x-pikchr")==0 ){
    int isPopup = P("popup")!=0;
    const char *zPikchr = blob_str(pWiki);
    int w, h;
    char *zOut = pikchr(zPikchr, "pikchr", 0, &w, &h);
    if( w>0 ){
      if( isPopup ) cgi_set_content_type("image/svg+xml");
      else{
        @ <div class="pikchr-svg" style="max-width:%d(w)px">
      }
      @ %s(zOut)
      if( !isPopup){
        @ </div>
      }
    }else{
      @ <pre class='error'>
      @ %h(zOut)
      @ </pre>
    }
    free(zOut);
  }else{
    @ <pre class='textPlain'>
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
  style_set_current_feature("wiki");
  style_header("Markdown Formatting Rules");
  if( fTxt ){
    style_submenu_element("Formatted", "%R/md_rules");
  }else{
    style_submenu_element("Plain-Text", "%R/md_rules?txt=1");
  }
  style_submenu_element("Wiki", "%R/wiki_rules");
  blob_init(&x, builtin_text("markdown.md"), -1);
  blob_materialize(&x);
  interwiki_append_map_table(&x);
  safe_html_context(DOCSRC_TRUSTED);
  wiki_render_by_mimetype(&x, fTxt ? "text/plain" : "text/x-markdown");
  blob_reset(&x);
  style_finish_page();
}

/*
** WEBPAGE: wiki_rules
**
** Show a summary of the wiki formatting rules.
*/
void wiki_rules_page(void){
  Blob x;
  int fTxt = P("txt")!=0;
  style_set_current_feature("wiki");
  style_header("Wiki Formatting Rules");
  if( fTxt ){
    style_submenu_element("Formatted", "%R/wiki_rules");
  }else{
    style_submenu_element("Plain-Text", "%R/wiki_rules?txt=1");
  }
  style_submenu_element("Markdown","%R/md_rules");
  blob_init(&x, builtin_text("wiki.wiki"), -1);
  blob_materialize(&x);
  interwiki_append_map_table(&x);
  safe_html_context(DOCSRC_TRUSTED);
  wiki_render_by_mimetype(&x, fTxt ? "text/plain" : "text/x-fossil-wiki");
  blob_reset(&x);
  style_finish_page();
}

/*
** WEBPAGE: markup_help
**
** Show links to the md_rules and wiki_rules pages.
*/
void markup_help_page(void){
  style_set_current_feature("wiki");
  style_header("Fossil Markup Styles");
  @ <ul>
  @ <li><p>%z(href("%R/wiki_rules"))Fossil Wiki Formatting Rules</a></p></li>
  @ <li><p>%z(href("%R/md_rules"))Markdown Formatting Rules</a></p></li>
  @ </ul>
  style_finish_page();
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
  if( (ok & W_SANDBOX)!=0 ){
    style_submenu_element("Sandbox", "%R/wikiedit?name=Sandbox");
  }
}

/*
** WEBPAGE: wikihelp
** A generic landing page for wiki.
*/
void wiki_helppage(void){
  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  style_set_current_feature("wiki");
  style_header("Wiki Help");
  wiki_standard_submenu(W_ALL_BUT(W_HELP));
  @ <h2>Wiki Links</h2>
  @ <ul>
  @ <li> %z(href("%R/timeline?y=w"))Recent changes</a> to wiki pages.</li>
  @ <li> Formatting rules for %z(href("%R/wiki_rules"))Fossil Wiki</a> and for
  @ %z(href("%R/md_rules"))Markdown Wiki</a>.</li>
  @ <li> Use the %z(href("%R/wikiedit?name=Sandbox"))Sandbox</a>
  @      to experiment.</li>
  if( g.perm.NewWiki ){
    @ <li>  Create a %z(href("%R/wikinew"))new wiki page</a>.</li>
    if( g.perm.Write ){
      @ <li>   Create a %z(href("%R/technoteedit"))new tech-note</a>.</li>
    }
  }
  @ <li> %z(href("%R/wcontent"))List of All Wiki Pages</a>
  @      available on this server.</li>
  @ <li> %z(href("%R/timeline?y=e"))List of All Tech-notes</a>
  @      available on this server.</li>
  if( g.perm.ModWiki ){
    @ <li> %z(href("%R/modreq"))Tend to pending moderation requests</a></li>
  }
  if( search_restrict(SRCH_WIKI)!=0 ){
    @ <li> %z(href("%R/wikisrch"))Search</a> for wiki pages containing key
    @ words</li>
  }
  @ </ul>
  style_finish_page();
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
  style_set_current_feature("wiki");
  style_header("Wiki Search");
  wiki_standard_submenu(W_HELP|W_LIST|W_SANDBOX);
  search_screen(SRCH_WIKI, 0);
  style_finish_page();
}

/* Return values from wiki_page_type() */
#if INTERFACE
# define WIKITYPE_UNKNOWN    (-1)
# define WIKITYPE_NORMAL     0
# define WIKITYPE_BRANCH     1
# define WIKITYPE_CHECKIN    2
# define WIKITYPE_TAG        3
# define WIKITYPE_TICKET     4
#endif

/*
** Figure out what type of wiki page we are dealing with.
*/
int wiki_page_type(const char *zPageName){
  if( db_get_boolean("wiki-about",1)==0 ){
    return WIKITYPE_NORMAL;
  }else
  if( has_prefix("checkin/", zPageName)
   && db_exists("SELECT 1 FROM blob WHERE uuid=%Q",zPageName+8)
  ){
    return WIKITYPE_CHECKIN;
  }else
  if( has_prefix("branch/", zPageName) ){
    return WIKITYPE_BRANCH;
  }else
  if( has_prefix("tag/", zPageName) ){
    return WIKITYPE_TAG;
  }else
  if( has_prefix("ticket/", zPageName) ){
    return WIKITYPE_TICKET;
  }
  return WIKITYPE_NORMAL;
}

/*
** Returns a JSON-friendly string form of the integer value returned
** by wiki_page_type(zPageName).
*/
const char * wiki_page_type_name(const char *zPageName){
  switch(wiki_page_type(zPageName)){
    case WIKITYPE_CHECKIN: return "checkin";
    case WIKITYPE_BRANCH: return "branch";
    case WIKITYPE_TAG: return "tag";
    case WIKITYPE_TICKET: return "ticket";
    case WIKITYPE_NORMAL:
    default: return "normal";
  }
}

/*
** Add an appropriate style_header() for either the /wiki or /wikiedit page
** for zPageName.  zExtra is an empty string for /wiki but has the text
** "Edit: " for /wikiedit.
**
** If the page is /wiki and the page is one of the special times (check-in,
** branch, or tag) and the "p" query parameter is omitted, then do a
** redirect to the display of the check-in, branch, or tag rather than
** continuing to the plain wiki display.
*/
static int wiki_page_header(
  int eType,                /* Page type.  Might be WIKITYPE_UNKNOWN */
  const char *zPageName,    /* Name of the page */
  const char *zExtra        /* Extra prefix text on the page header */
){
  style_set_current_feature("wiki");
  if( eType==WIKITYPE_UNKNOWN ) eType = wiki_page_type(zPageName);
  switch( eType ){
    case WIKITYPE_NORMAL: {
      style_header("%s%s", zExtra, zPageName);
      break;
    }
    case WIKITYPE_CHECKIN: {
      zPageName += 8;
      if( zExtra[0]==0 && !P("p") ){
        cgi_redirectf("%R/info/%s",zPageName);
      }else{
        style_header("Notes About Check-in %S", zPageName);
        style_submenu_element("Check-in Timeline","%R/timeline?f=%s",
                              zPageName);
        style_submenu_element("Check-in Info","%R/info/%s", zPageName);
      }
      break;
    }
    case WIKITYPE_BRANCH: {
      zPageName += 7;
      if( zExtra[0]==0 && !P("p") ){
        cgi_redirectf("%R/timeline?r=%t", zPageName);
      }else{
        style_header("Notes About Branch %h", zPageName);
        style_submenu_element("Branch Timeline","%R/timeline?r=%t", zPageName);
      }
      break;
    }
    case WIKITYPE_TAG: {
      zPageName += 4;
      if( zExtra[0]==0 && !P("p") ){
        cgi_redirectf("%R/timeline?t=%t",zPageName);
      }else{
        style_header("Notes About Tag %h", zPageName);
        style_submenu_element("Tag Timeline","%R/timeline?t=%t",zPageName);
      }
      break;
    }
    case WIKITYPE_TICKET: {
      zPageName += 7;
      if( zExtra[0]==0 && !P("p") ){
        cgi_redirectf("%R/tktview/%s",zPageName);
      }else{
        style_header("Notes About Ticket %h", zPageName);
        style_submenu_element("Ticket","%R/tktview/%s",zPageName);
      }
      break;
    }
  }
  return eType;
}

/*
** Wiki pages with special names "branch/...", "checkin/...", and "tag/..."
** requires perm.Write privilege in addition to perm.WrWiki in order
** to write.  This function determines whether the extra perm.Write
** is required and available.  Return true if writing to the wiki page
** may proceed, and return false if permission is lacking.
*/
static int wiki_special_permission(const char *zPageName){
  if( strncmp(zPageName,"branch/",7)!=0
   && strncmp(zPageName,"checkin/",8)!=0
   && strncmp(zPageName,"tag/",4)!=0
   && strncmp(zPageName,"ticket/",7)!=0
  ){
    return 1;
  }
  if( db_get_boolean("wiki-about",1)==0 ){
    return 1;
  }
  if( strncmp(zPageName,"ticket/",7)==0 ){
    return g.perm.WrTkt;
  }
  return g.perm.Write;
}

/*
** WEBPAGE: wiki
**
** Display a wiki page.  Example:  /wiki?name=PAGENAME
**
** Query parameters:
**
**    name=NAME        Name of the wiki page to display.  Required.
**    nsm              Omit the submenu if present.  (Mnemonic: No SubMenu)
**    p                Always show just the wiki page.  For special
**                     pages for check-ins, branches, or tags, there will
**                     be a redirect to the associated /info page unless
**                     this query parameter is present.
**    popup            Suppress the header and footer and other page
**                     boilerplate and only return the formatted content
**                     of the wiki page.
*/
void wiki_page(void){
  char *zTag;
  int rid = 0;
  int isSandbox;
  unsigned submenuFlags = W_HELP;
  Blob wiki;
  Manifest *pWiki = 0;
  const char *zPageName;
  const char *zMimetype = 0;
  int isPopup = P("popup")!=0;
  char *zBody = mprintf("%s","<i>Empty Page</i>");
  int noSubmenu = P("nsm")!=0 || g.isHome;

  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  zPageName = P("name");
  (void)P("s")/*for cgi_check_for_malice(). "s" == search stringy*/;
  cgi_check_for_malice();
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
  if( !noSubmenu ){
    if( ((rid && g.perm.WrWiki) || (!rid && g.perm.NewWiki))
     && wiki_special_permission(zPageName)
    ){
      style_submenu_element("Edit", "%R/wikiedit?name=%T", zPageName);
    }else if( rid && g.perm.ApndWiki ){
      style_submenu_element("Edit", "%R/wikiappend?name=%T", zPageName);
    }
    if( g.perm.Hyperlink ){
      style_submenu_element("History", "%R/whistory?name=%T", zPageName);
    }
  }
  if( !isPopup ){
    style_set_current_page("%T?name=%T", g.zPath, zPageName);
    wiki_page_header(WIKITYPE_UNKNOWN, zPageName, "");
    if( !noSubmenu ){
      wiki_standard_submenu(submenuFlags);
    }
  }
  if( zBody[0]==0 ){
    @ <i>This page has been deleted</i>
  }else{
    blob_init(&wiki, zBody, -1);
    safe_html_context(DOCSRC_WIKI);
    wiki_render_by_mimetype(&wiki, zMimetype);
    blob_reset(&wiki);
  }
  manifest_destroy(pWiki);
  if( !isPopup ){
    char * zLabel = mprintf("<h2><a href='%R/attachlist?page=%T'>"
                            "Attachments</a>:</h2>",
                            zPageName);
    attachment_list(zPageName, zLabel, 1);
    fossil_free(zLabel);
    document_emit_js(/*for optional pikchr support*/);
    style_finish_page();
  }
}

/*
** Write a wiki artifact into the repository
*/
int wiki_put(Blob *pWiki, int parent, int needMod){
  int nrid;
  if( !needMod ){
    nrid = content_put_ex(pWiki, 0, 0, 0, 0);
    if( parent ) content_deltify(parent, &nrid, 1, 0);
  }else{
    nrid = content_put_ex(pWiki, 0, 0, 0, 1);
    moderation_table_create();
    db_multi_exec("INSERT INTO modreq(objid) VALUES(%d)", nrid);
  }
  db_add_unsent(nrid);
  db_multi_exec("INSERT OR IGNORE INTO unclustered VALUES(%d);", nrid);
  manifest_crosslink(nrid, pWiki, MC_NONE);
  if( login_is_individual() ){
    alert_user_contact(login_name());
  }
  return nrid;
}

/*
** Output a selection box from which the user can select the
** wiki mimetype.  Arguments:
**
**     zMimetype      -     The current value of the query parameter
**     zParam         -     The name of the query parameter
*/
void mimetype_option_menu(const char *zMimetype, const char *zParam){
  unsigned i;
  @ <select name="%s(zParam)" size="1">
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
  for(i=6; i>=0; i-=3){
    if( zMimetype && fossil_strcmp(zMimetype, azStyles[i])==0 ){
      return azStyles[i+1];
    }
  }
  return azStyles[1];
}

/*
 ** Tries to fetch a wiki page for the given name. If found, it
 ** returns true, else false.
 **
 ** versionsBack specifies how many versions back in the history to
 ** fetch. Use 0 for the latest version, 1 for its parent, etc.
 **
 ** If pRid is not NULL then if a result is found *pRid is set to its
 ** RID. If ppWiki is not NULL then if found *ppWiki is set to the
 ** loaded wiki object, which the caller is responsible for passing to
 ** manifest_destroy().
 */
static int wiki_fetch_by_name( const char *zPageName,
                               unsigned int versionsBack,
                               int * pRid, Manifest **ppWiki ){
  Manifest *pWiki = 0;
  char *zTag = mprintf("wiki-%s", zPageName);
  Stmt q = empty_Stmt;
  int rid = 0;

  db_prepare(&q, "SELECT rid FROM tagxref"
             " WHERE tagid=(SELECT tagid FROM tag WHERE"
             "   tagname=%Q) "
             " ORDER BY mtime DESC LIMIT -1 OFFSET %u", zTag,
             versionsBack);
  fossil_free(zTag);
  if(SQLITE_ROW == db_step(&q)){
    rid = db_column_int(&q, 0);
  }
  db_finalize(&q);
  if( rid == 0 ){
    return 0;
  }
  else if(pRid){
    *pRid = rid;
  }
  if(ppWiki){
    pWiki = manifest_get(rid, CFTYPE_WIKI, 0);
    if( pWiki==0 ){
      /* "Cannot happen." */
      return 0;
    }
    *ppWiki = pWiki;
  }
  return 1;
}

/*
** Determines whether the wiki page with the given name can be edited
** or created by the current user. If not, an AJAX error is queued and
** false is returned, else true is returned. A NULL, empty, or
** malformed name is considered non-writable, regardless of the user.
**
** If pRid is not NULL then this function writes the page's rid to
** *pRid (whether or not access is granted). On error or if the page
** does not yet exist, *pRid will be set to 0.
**
** Note that the sandbox is a special case: it is a pseudo-page with
** no rid and the /wikiajax API does not allow anyone to actually save
** a sandbox page, but it is reported as writable here (with rid 0).
*/
static int wiki_ajax_can_write(const char *zPageName, int * pRid){
  int rid = 0;
  const char * zErr = 0;

  if(pRid) *pRid = 0;
  if(!zPageName || !*zPageName
     || !wiki_name_is_wellformed((unsigned const char *)zPageName)){
    zErr = "Invalid page name.";
  }else if(is_sandbox(zPageName)){
    return 1;
  }else{
    wiki_fetch_by_name(zPageName, 0, &rid, 0);
    if(pRid) *pRid = rid;
    if(!wiki_special_permission(zPageName)){
      zErr = "Editing this page requires non-wiki write permissions.";
    }else if( (rid && g.perm.WrWiki) || (!rid && g.perm.NewWiki) ){
      return 3;
    }else if(rid && !g.perm.WrWiki){
      zErr = "Requires wiki-write permissions.";
    }else if(!rid && !g.perm.NewWiki){
      zErr = "Requires new-wiki permissions.";
    }else{
      zErr = "Cannot happen! Please report this as a bug.";
    }
  }
  ajax_route_error(403, "%s", zErr);
  return 0;
}


/*
** Emits an array of attachment info records for the given wiki page
** artifact.
**
** Output format:
**
** [{
**   "uuid": attachment artifact hash,
**   "src": hash of the attachment blob,
**   "target": wiki page name or ticket/event ID,
**   "filename": filename of attachment,
**   "mtime": ISO-8601 timestamp UTC,
**   "isLatest": true this is the latest version of this file
**               else false,
** }, ...once per attachment]
**
** If there are no matching attachments then it will emit a JSON
** null (if nullIfEmpty) or an empty JSON array.
**
** If latestOnly is true then only the most recent entry for a given
** attachment is emitted, else all versions are emitted in descending
** mtime order.
*/
static void wiki_ajax_emit_page_attachments(Manifest * pWiki,
                                            int latestOnly,
                                            int nullIfEmpty){
  int i = 0;
  Stmt q = empty_Stmt;
  db_prepare(&q,
     "SELECT datetime(mtime), src, target, filename, isLatest,"
     "  (SELECT uuid FROM blob WHERE rid=attachid) uuid"
     "  FROM attachment"
     "  WHERE target=%Q"
     "  AND (isLatest OR %d)"
     "  ORDER BY target, isLatest DESC, mtime DESC",
     pWiki->zWikiTitle, !latestOnly
  );
  while(SQLITE_ROW == db_step(&q)){
    const char * zTime = db_column_text(&q, 0);
    const char * zSrc = db_column_text(&q, 1);
    const char * zTarget = db_column_text(&q, 2);
    const char * zName = db_column_text(&q, 3);
    const int isLatest = db_column_int(&q, 4);
    const char * zUuid = db_column_text(&q, 5);
    if(!i++){
      CX("[");
    }else{
      CX(",");
    }
    CX("{");
    CX("\"uuid\": %!j, \"src\": %!j, \"target\": %!j, "
       "\"filename\": %!j, \"mtime\": %!j, \"isLatest\": %s}",
       zUuid, zSrc, zTarget,
       zName, zTime, isLatest ? "true" : "false");
  }
  db_finalize(&q);
  if(!i){
    if(nullIfEmpty){
      CX("null");
    }else{
      CX("[]");
    }
  }else{
    CX("]");
  }
}

/*
** Proxy for wiki_ajax_emit_page_attachments() which attempts to load
** the given wiki page artifact. Returns true if it can load the given
** page, else false. If it returns false then it queues up a 404 ajax
** error response.
*/
static int wiki_ajax_emit_page_attachments2(const char *zPageName,
                                            int latestOnly,
                                            int nullIfEmpty){
  Manifest * pWiki = 0;
  if( !wiki_fetch_by_name(zPageName, 0, 0, &pWiki) ){
    ajax_route_error(404, "Wiki page could not be loaded: %s",
                     zPageName);
    return 0;
  }
  wiki_ajax_emit_page_attachments(pWiki, latestOnly, nullIfEmpty);
  manifest_destroy(pWiki);
  return 1;
}


/*
** Loads the given wiki page, sets the response type to
** application/json, and emits it as a JSON object.  If zPageName is a
** sandbox page then a "fake" object is emitted, as the wikiajax API
** does not permit saving the sandbox.
**
** Returns true on success, false on error, and on error it
** queues up a JSON-format error response.
**
** Output JSON format:
**
** { name: "page name",
**   type: "normal" | "tag" | "checkin" | "branch" | "sandbox",
**   mimetype: "mimetype",
**   version: UUID string or null for a sandbox page,
**   parent: "parent uuid" or null if no parent,
**   isDeleted: true if the page has no content (is "deleted")
**              else not set (making it "falsy" in JS),
**   attachments: see wiki_ajax_emit_page_attachments(),
**   content: "page content" (only if includeContent is true)
** }
**
** If includeContent is false then the content member is elided.
*/
static int wiki_ajax_emit_page_object(const char *zPageName,
                                      int includeContent){
  Manifest * pWiki = 0;
  char * zUuid;

  if( is_sandbox(zPageName) ){
    char * zMimetype =
      db_get("sandbox-mimetype","text/x-fossil-wiki");
    char * zBody = db_get("sandbox","");
    CX("{\"name\": %!j, \"type\": \"sandbox\", "
       "\"mimetype\": %!j, \"version\": null, \"parent\": null",
       zPageName, zMimetype);
    if(includeContent){
      CX(", \"content\": %!j",
       zBody);
    }
    CX("}");
    fossil_free(zMimetype);
    fossil_free(zBody);
    return 1;
  }else if( !wiki_fetch_by_name(zPageName, 0, 0, &pWiki) ){
    ajax_route_error(404, "Wiki page could not be loaded: %s",
                     zPageName);
    return 0;
  }else{
    zUuid = rid_to_uuid(pWiki->rid);
    CX("{\"name\": %!j, \"type\": %!j, "
       "\"version\": %!j, "
       "\"mimetype\": %!j, ",
       pWiki->zWikiTitle,
       wiki_page_type_name(pWiki->zWikiTitle),
       zUuid,
       pWiki->zMimetype ? pWiki->zMimetype : "text/x-fossil-wiki");
    CX("\"parent\": ");
    if(pWiki->nParent){
      CX("%!j", pWiki->azParent[0]);
    }else{
      CX("null");
    }
    if(!pWiki->zWiki || !pWiki->zWiki[0]){
      CX(", \"isEmpty\": true");
    }
    if(includeContent){
      CX(", \"content\": %!j", pWiki->zWiki);
    }
    CX(", \"attachments\": ");
    wiki_ajax_emit_page_attachments(pWiki, 0, 1);
    CX("}");
    fossil_free(zUuid);
    manifest_destroy(pWiki);
    return 2;
  }
}

/*
** Ajax route handler for /wikiajax/save.
**
** URL params:
**
**  page = the wiki page name.
**  mimetype = content mimetype.
**  content = page content. Fossil considers an empty page to
**            be "deleted".
**  isnew = 1 if the page is to be newly-created, else 0 or
**          not send.
**
** Responds with JSON. On error, an object in the form documented by
** ajax_route_error(). On success, an object in the form documented
** for wiki_ajax_emit_page_object().
**
** The wikiajax API disallows saving of a sandbox pseudo-page, and
** will respond with an error if asked to save one. Should we want to
** enable it, it's implemented like this for any saved page for which
** is_sandbox(zPageName) is true:
**
**  db_set("sandbox",zBody,0);
**  db_set("sandbox-mimetype",zMimetype,0);
**
*/
static void wiki_ajax_route_save(void){
  const char *zPageName = P("page");
  const char *zMimetype = P("mimetype");
  const char *zContent = P("content");
  const int isNew = ajax_p_bool("isnew");
  Blob content = empty_blob;
  int parentRid = 0;
  int rollback = 0;

  if(!wiki_ajax_can_write(zPageName, &parentRid)){
    return;
  }else if(is_sandbox(zPageName)){
    ajax_route_error(403,"Saving a sandbox page is prohibited.");
    return;
  }
  /* These isNew checks are just me being pedantic. We could just as
     easily derive isNew based on whether or not the page already
     exists. */
  if(isNew){
    if(parentRid>0){
      ajax_route_error(403,"Requested a new page, "
                       "but it already exists with RID %d: %s",
                       parentRid, zPageName);
      return;
    }
  }else if(parentRid==0){
    ajax_route_error(403,"Creating new page [%s] requires passing "
                     "isnew=1.", zPageName);
    return;
  }
  blob_init(&content, zContent ? zContent : "", -1);
  cgi_set_content_type("application/json");
  db_begin_transaction();
  wiki_cmd_commit(zPageName, parentRid, &content, zMimetype, 0);
  rollback = wiki_ajax_emit_page_object(zPageName, 1) ? 0 : 1;
  db_end_transaction(rollback);
}

/*
** Ajax route handler for /wikiajax/fetch.
**
** URL params:
**
**  page = the wiki page name
**
** Responds with JSON. On error, an object in the form documented by
** ajax_route_error(). On success, an object in the form documented
** for wiki_ajax_emit_page_object().
*/
static void wiki_ajax_route_fetch(void){
  const char * zPageName = P("page");

  if( zPageName==0 || zPageName[0]==0 ){
    ajax_route_error(400,"Missing page name.");
    return;
  }
  cgi_set_content_type("application/json");
  wiki_ajax_emit_page_object(zPageName, 1);
}

/*
** Ajax route handler for /wikiajax/attachments.
**
** URL params:
**
**  page = the wiki page name
**  latestOnly = if set, only latest version of each attachment
**               is emitted.
**
** Responds with JSON: see wiki_ajax_emit_page_attachments()
**
** If there are no attachments it emits an empty array instead of null
** so that the output can be used as a top-level JSON response.
**
** On error, an object in the form documented by
** ajax_route_error(). On success, an object in the form documented
** for wiki_ajax_emit_page_attachments().
*/
static void wiki_ajax_route_attachments(void){
  const char * zPageName = P("page");
  const int fLatestOnly = P("latestOnly")!=0;
  if( zPageName==0 || zPageName[0]==0 ){
    ajax_route_error(400,"Missing page name.");
    return;
  }
  cgi_set_content_type("application/json");
  wiki_ajax_emit_page_attachments2(zPageName, fLatestOnly, 0);
}

/*
** Ajax route handler for /wikiajax/diff.
**
** URL params:
**
**  page = the wiki page name
**  content = the new/edited wiki page content
**
** Requires that the user have write access solely to avoid some
** potential abuse cases. It does not actually write anything.
*/
static void wiki_ajax_route_diff(void){
  const char * zPageName = P("page");
  Blob contentNew = empty_blob, contentOrig = empty_blob;
  Manifest * pParent = 0;
  const char * zContent = P("content");
  u64 diffFlags = DIFF_HTML | DIFF_NOTTOOBIG | DIFF_STRIP_EOLCR;
  char * zParentUuid = 0;

  if( zPageName==0 || zPageName[0]==0 ){
    ajax_route_error(400,"Missing page name.");
    return;
  }else if(!wiki_ajax_can_write(zPageName, 0)){
    return;
  }
  switch(atoi(PD("sbs","0"))){
    case 0: diffFlags |= DIFF_LINENO; break;
    default: diffFlags |= DIFF_SIDEBYSIDE;
  }
  switch(atoi(PD("ws","2"))){
    case 1: diffFlags |= DIFF_IGNORE_EOLWS; break;
    case 2: diffFlags |= DIFF_IGNORE_ALLWS; break;
    default: break;
  }
  wiki_fetch_by_name( zPageName, 0, 0, &pParent );
  if( pParent ){
    zParentUuid = rid_to_uuid(pParent->rid);
  }
  if( pParent && pParent->zWiki && *pParent->zWiki ){
    blob_init(&contentOrig, pParent->zWiki, -1);
  }else{
    blob_init(&contentOrig, "", 0);
  }
  blob_init(&contentNew, zContent ? zContent : "", -1);
  cgi_set_content_type("text/html");
  ajax_render_diff(&contentOrig, zParentUuid, &contentNew, diffFlags);
  blob_reset(&contentNew);
  blob_reset(&contentOrig);
  fossil_free(zParentUuid);
  manifest_destroy(pParent);
}

/*
** Ajax route handler for /wikiajax/preview.
**
** URL params:
**
**  mimetype = the wiki page mimetype (determines rendering style)
**  content = the wiki page content
*/
static void wiki_ajax_route_preview(void){
  const char * zContent = P("content");

  if( zContent==0 ){
    ajax_route_error(400,"Missing content to preview.");
    return;
  }else{
    Blob content = empty_blob;
    const char * zMimetype = PD("mimetype","text/x-fossil-wiki");

    blob_init(&content, zContent, -1);
    cgi_set_content_type("text/html");
    wiki_render_by_mimetype(&content, zMimetype);
    blob_reset(&content);
  }
}

/*
** Outputs the wiki page list in JSON form. If verbose is false then
** it emits an array of strings (page names). If verbose is true it outputs
** an array of objects in this form:
**
** { name: string, version: string or null of sandbox box,
**   parent: uuid or null for first version or sandbox,
**   mimetype: string,
**   type: string (normal, branch, tag, check-in, or sandbox)
** }
**
** If includeContent is true, the object contains a "content" member
** with the raw page content. includeContent is ignored if verbose is
** false.
**
*/
static void wiki_render_page_list_json(int verbose, int includeContent){
  Stmt q = empty_Stmt;
  int n = 0;
  db_begin_transaction();
  db_prepare(&q, "SELECT"
             " substr(tagname,6) AS name"
             " FROM tag JOIN tagxref USING('tagid')"
             " WHERE tagname GLOB 'wiki-*'"
             " AND TYPEOF(tagxref.value+0)='integer'"
             /* ^^^ elide wiki- tags which are not wiki pages */
             " UNION SELECT 'Sandbox' AS name"
             " ORDER BY name COLLATE NOCASE");
  CX("[");
  while( SQLITE_ROW==db_step(&q) ){
    char const * zName = db_column_text(&q,0);
    if(n++){
      CX(",");
    }
    if(verbose==0){
      CX("%!j", zName);
    }else{
      wiki_ajax_emit_page_object(zName, includeContent);
    }
  }
  CX("]");
  db_finalize(&q);
  db_end_transaction(0);
}

/*
** Ajax route handler for /wikiajax/list.
**
** Optional parameters: verbose, includeContent (see below).
**
** Responds with JSON. On error, an object in the form documented by
** ajax_route_error().
**
** On success, it emits an array of strings (page names) sorted
** case-insensitively. If the "verbose" parameter is passed in then
** the result list contains objects in the format documented for
** wiki_ajax_emit_page_object(). The content of each object is elided
** unless the "includeContent" parameter is passed on with a
** "non-false" value..
**
** The result list always contains an entry named "Sandbox" which
** represents the sandbox pseudo-page.
*/
static void wiki_ajax_route_list(void){
  const int verbose = ajax_p_bool("verbose");
  const int includeContent = ajax_p_bool("includeContent");

  cgi_set_content_type("application/json");
  wiki_render_page_list_json(verbose, includeContent);
}

/*
** WEBPAGE: wikiajax hidden
**
** An internal dispatcher for wiki AJAX operations. Not for direct
** client use. All routes defined by this interface are app-internal,
** subject to change
*/
void wiki_ajax_page(void){
  const char * zName = P("name");
  AjaxRoute routeName = {0,0,0,0};
  const AjaxRoute * pRoute = 0;
  const AjaxRoute routes[] = {
  /* Keep these sorted by zName (for bsearch()) */
  {"attachments", wiki_ajax_route_attachments, 0, 0},
  {"diff", wiki_ajax_route_diff, 1, 1},
  {"fetch", wiki_ajax_route_fetch, 0, 0},
  {"list", wiki_ajax_route_list, 0, 0},
  {"preview", wiki_ajax_route_preview, 0, 1},
  {"save", wiki_ajax_route_save, 1, 1}
  };

  if(zName==0 || zName[0]==0){
    ajax_route_error(400,"Missing required [route] 'name' parameter.");
    return;
  }
  routeName.zName = zName;
  pRoute = (const AjaxRoute *)bsearch(&routeName, routes,
                                      count(routes), sizeof routes[0],
                                      cmp_ajax_route_name);
  if(pRoute==0){
    ajax_route_error(404,"Ajax route not found.");
    return;
  }
  login_check_credentials();
  if( pRoute->bWriteMode!=0 && g.perm.WrWiki==0 ){
    ajax_route_error(403,"Write permissions required.");
    return;
  }else if( pRoute->bWriteMode==0 && g.perm.RdWiki==0 ){
    ajax_route_error(403,"Read-Wiki permissions required.");
    return;
  }else if(0==cgi_csrf_safe(pRoute->bPost)){
    ajax_route_error(403,
                     "CSRF violation (make sure sending of HTTP "
                     "Referer headers is enabled for XHR "
                     "connections).");
    return;
  }
  pRoute->xCallback();
}

/*
** Emits a preview-toggle option widget for /wikiedit and /fileedit.
*/
void wikiedit_emit_toggle_preview(void){
  CX("<div class='input-with-label'>"
     "<input type='checkbox' id='edit-shift-enter-preview' "
     "></input><label for='edit-shift-enter-preview'>"
     "Shift-enter previews</label>"
     "<div class='help-buttonlet'>"
     "When enabled, shift-enter switches between preview and edit modes. "
     "Some software-based keyboards misinteract with this, so it can be "
     "disabled when needed."
     "</div>"
     "</div>");
}

/*
** WEBPAGE: wikiedit
** URL: /wikedit?name=PAGENAME
**
** The main front-end for the Ajax-based wiki editor app. Passing
** in the name of an unknown page will trigger the creation
** of a new page (which is not actually created in the database
** until the user explicitly saves it). If passed no page name,
** the user may select a page from the list on the first UI tab.
**
** When creating a new page, the mimetype URL parameter may optionally
** be used to set its mimetype to one of text/x-fossil-wiki,
** text/x-markdown, or text/plain, defaulting to the former.
*/
void wikiedit_page(void){
  const char *zPageName;
  const char * zMimetype = P("mimetype");
  int isSandbox;
  int found = 0;

  login_check_credentials();
  zPageName = PD("name","");
  if(zPageName && *zPageName){
    if( check_name(zPageName) ) return;
  }
  isSandbox = is_sandbox(zPageName);
  if( isSandbox ){
    if( !g.perm.RdWiki ){
      login_needed(g.anon.RdWiki);
      return;
    }
    found = 1;
  }else if( zPageName!=0 && zPageName[0]!=0){
    int rid = 0;
    if( !wiki_special_permission(zPageName) ){
      login_needed(0);
      return;
    }
    found = wiki_fetch_by_name(zPageName, 0, &rid, 0);
    if( (rid && !g.perm.RdWiki) || (!rid && !g.perm.NewWiki) ){
      login_needed(rid ? g.anon.RdWiki : g.anon.NewWiki);
      return;
    }
  }else{
    if( !g.perm.RdWiki ){
      login_needed(g.anon.RdWiki);
      return;
    }
  }
  style_set_current_feature("wiki");
  style_header("Wiki Editor");
  style_emit_noscript_for_js_page();

  /* Status bar */
  CX("<div id='fossil-status-bar' "
     "title='Status message area. Double-click to clear them.'>"
     "Status messages will go here.</div>\n"
     /* will be moved into the tab container via JS */);

  CX("<div id='wikiedit-edit-status'>"
     "<span class='name'></span>"
     "<span class='links'></span>"
     "</div>");

  /* Main tab container... */
  CX("<div id='wikiedit-tabs' class='tab-container'>Loading...</div>");
  /* The .hidden class on the following tab elements is to help lessen
     the FOUC effect of the tabs before JS re-assembles them. */

  /******* Page list *******/
  {
    CX("<div id='wikiedit-tab-pages' "
       "data-tab-parent='wikiedit-tabs' "
       "data-tab-label='Wiki Page List' "
       "class='hidden'"
       ">");
    CX("<div>Loading wiki pages list...</div>");
    CX("</div>"/*#wikiedit-tab-pages*/);
  }

  /******* Content tab *******/
  {
    CX("<div id='wikiedit-tab-content' "
       "data-tab-parent='wikiedit-tabs' "
       "data-tab-label='Editor' "
       "class='hidden'"
       ">");
    CX("<div class='"
       "wikiedit-options flex-container flex-row child-gap-small'>");
    CX("<div class='input-with-label'>"
       "<label>Mime type</label>");
    mimetype_option_menu("text/x-markdown", "mimetype");
    CX("</div>");
    style_select_list_int("select-font-size",
                          "editor_font_size", "Editor font size",
                          NULL/*tooltip*/,
                          100,
                          "100%", 100, "125%", 125,
                          "150%", 150, "175%", 175,
                          "200%", 200, NULL);
    CX("<div class='input-with-label'>"
       /*will get moved around dynamically*/
       "<button class='wikiedit-save'>"
       "Save</button>"
       "<button class='wikiedit-save-close'>"
       "Save &amp; Close</button>"
       "<div class='help-buttonlet'>"
       "Save edits to this page and optionally return "
       "to the wiki page viewer."
       "</div>"
       "</div>" /*will get moved around dynamically*/);
    CX("<span class='save-button-slot'></span>");

    CX("<div class='input-with-label'>"
       "<button class='wikiedit-content-reload' "
       ">Discard &amp; Reload</button>"
       "<div class='help-buttonlet'>"
       "Reload the file from the server, discarding "
       "any local edits. To help avoid accidental loss of "
       "edits, it requires confirmation (a second click) within "
       "a few seconds or it will not reload."
       "</div>"
       "</div>");
    wikiedit_emit_toggle_preview();
    CX("</div>");
    CX("<div class='flex-container flex-column stretch'>");
    CX("<textarea name='content' id='wikiedit-content-editor' "
       "class='wikiedit' rows='25'>");
    CX("</textarea>");
    CX("</div>"/*textarea wrapper*/);
    CX("</div>"/*#tab-file-content*/);
  }
  /****** Preview tab ******/
  {
    CX("<div id='wikiedit-tab-preview' "
       "data-tab-parent='wikiedit-tabs' "
       "data-tab-label='Preview' "
       "class='hidden'"
       ">");
    CX("<div class='wikiedit-options flex-container "
       "flex-row child-gap-small'>");
    CX("<button id='btn-preview-refresh' "
       "data-f-preview-from='wikiContent' "
       /* ^^^ fossil.page[methodName]() OR text source elem ID,
      ** but we need a method in order to support clients swapping out
      ** the text editor with their own. */
       "data-f-preview-via='_postPreview' "
       /* ^^^ fossil.page[methodName](content, callback) */
       "data-f-preview-to='_previewTo' "
       /* ^^^ dest elem ID or fossil.page[methodName]*/
       ">Refresh</button>");
    /* Toggle auto-update of preview when the Preview tab is selected. */
    CX("<div class='input-with-label'>"
       "<input type='checkbox' value='1' "
       "id='cb-preview-autorefresh' checked>"
       "<label for='cb-preview-autorefresh'>Auto-refresh?</label>"
       "</div>");
    CX("<span class='save-button-slot'></span>");
    CX("</div>"/*.wikiedit-options*/);
    CX("<div id='wikiedit-tab-preview-wrapper'></div>");
    CX("</div>"/*#wikiedit-tab-preview*/);
  }

  /****** Diff tab ******/
  {
    CX("<div id='wikiedit-tab-diff' "
       "data-tab-parent='wikiedit-tabs' "
       "data-tab-label='Diff' "
       "class='hidden'"
       ">");

    CX("<div class='wikiedit-options flex-container "
       "flex-row child-gap-small' "
       "id='wikiedit-tab-diff-buttons'>");
    CX("<div class='input-with-label'>"
       "<button class='sbs'>Side-by-side</button>"
       "<button class='unified'>Unified</button>"
       "</div>");
    CX("<span class='save-button-slot'></span>");
    CX("</div>");
    CX("<div id='wikiedit-tab-diff-wrapper'>"
       "Diffs will be shown here."
       "</div>");
    CX("</div>"/*#wikiedit-tab-diff*/);
  }

  /****** The obligatory "Misc" tab ******/
  {
    CX("<div id='wikiedit-tab-misc' "
       "data-tab-parent='wikiedit-tabs' "
       "data-tab-label='Misc.' "
       "class='hidden'"
       ">");
    CX("<fieldset id='attachment-wrapper'>");
    CX("<legend>Attachments</legend>");
    CX("<div>No attachments for the current page.</div>");
    CX("</fieldset>");
    CX("<h2>Wiki formatting rules</h2>");
    CX("<ul>");
    CX("<li><a href='%R/wiki_rules'>Fossil wiki format</a></li>");
    CX("<li><a href='%R/md_rules'>Markdown format</a></li>");
    CX("<li>Plain-text pages use no special formatting.</li>");
    CX("</ul>");
    CX("<h2>The \"Sandbox\" Page</h2>");
    CX("<p>The page named \"Sandbox\" is not a real wiki page. "
       "It provides a place where users may test out wiki syntax "
       "without having to actually save anything, nor pollute "
       "the repo with endless test runs. Any attempt to save the "
       "sandbox page will fail.</p>");
    CX("<h2>Wiki Name Rules</h2>");
    well_formed_wiki_name_rules();
    CX("</div>"/*#wikiedit-tab-save*/);
  }
  builtin_fossil_js_bundle_or("fetch", "dom", "tabs", "confirmer",
                              "storage", "popupwidget", "copybutton",
                              "pikchr", NULL);
  builtin_fossil_js_bundle_or("diff", NULL);
  builtin_request_js("fossil.page.wikiedit.js");
  builtin_fulfill_js_requests();
  /* Dynamically populate the editor... */
  style_script_begin(__FILE__,__LINE__);
  {
    /* Render the current page list to save us an XHR request
       during page initialization. This must be OUTSIDE of
       an onPageLoad() handler or else it does not get applied
       until after the wiki list widget is initialized. Similarly,
       it must come *after* window.fossil is initialized. */
    CX("\nfossil.page.initialPageList = ");
    wiki_render_page_list_json(1, 0);
    CX(";\n");
  }
  CX("fossil.onPageLoad(function(){\n");
  CX("const P = fossil.page;\n"
     "try{\n");
  if(!found && zPageName && *zPageName){
    /* For a new page, stick a dummy entry in the JS-side stash
       and "load" it from there. */
    CX("const winfo = {"
       "\"name\": %!j, \"mimetype\": %!j, "
       "\"type\": %!j, "
       "\"parent\": null, \"version\": null"
       "};\n",
       zPageName,
       zMimetype ? zMimetype : "text/x-fossil-wiki",
       wiki_page_type_name(zPageName));
    /* If the JS-side stash already has this page, load that
       copy from the stash, otherwise inject a new stash entry
       for it and load *that* one... */
    CX("if(!P.$stash.getWinfo(winfo)){"
       "P.$stash.updateWinfo(winfo,'');"
       "}\n");
  }
  if(zPageName && *zPageName){
    CX("P.loadPage(%!j);\n", zPageName);
  }
  CX("}catch(e){"
     "fossil.error(e); console.error('Exception:',e);"
     "}\n");
  CX("});\n"/*fossil.onPageLoad()*/);
  style_script_end();
  style_finish_page();
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
    cgi_redirectf("wikiedit?name=%T&mimetype=%s", zName, zMimetype);
  }
  style_set_current_feature("wiki");
  style_header("Create A New Wiki Page");
  wiki_standard_submenu(W_ALL_BUT(W_NEW));
  @ <p>Rules for wiki page names:</p>
  well_formed_wiki_name_rules();
  form_begin(0, "%R/wikinew");
  @ <p>Name of new wiki page:
  @ <input style="width: 35;" type="text" name="name" value="%h(zName)"><br>
  @ %z(href("%R/markup_help"))Markup style</a>:
  mimetype_option_menu("text/x-markdown", "mimetype");
  @ <br><input type="submit" value="Create">
  @ </p></form>
  if( zName[0] ){
    @ <p><span class="wikiError">
    @ "%h(zName)" is not a valid wiki page name!</span></p>
  }
  style_finish_page();
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
    blob_appendf(p, "\n\n<hr><div id=\"%s\"><i>On %s UTC %h",
      zId, zDate, login_name());
    if( zUser[0] && fossil_strcmp(zUser,login_name()) ){
      blob_appendf(p, " (claiming to be %h)", zUser);
    }
    blob_appendf(p, " added:</i><br>\n%s</div id=\"%s\">", zRemark, zId);
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
  const char *zPageName;
  const char *zUser;
  const char *zMimetype;
  int goodCaptcha = 1;
  const char *zFormat;
  Manifest *pWiki = 0;
  int isSandbox;

  login_check_credentials();
  if( !g.perm.ApndWiki ){
    login_needed(g.anon.ApndWiki);
    return;
  }
  zPageName = PD("name","");
  zMimetype = wiki_filter_mimetypes(P("mimetype"));
  if( check_name(zPageName) ) return;
  isSandbox = is_sandbox(zPageName);
  if(!isSandbox){
    zTag = mprintf("wiki-%s", zPageName);
    rid = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
      " ORDER BY mtime DESC", zTag
    );
    free(zTag);
    pWiki = rid ? manifest_get(rid, CFTYPE_WIKI, 0) : 0;
    if( !pWiki ){
      fossil_redirect_home();
      return;
    }
    zMimetype = wiki_filter_mimetypes(pWiki->zMimetype)
      /* see https://fossil-scm.org/forum/forumpost/0acfdaac80 */;
  }
  if( !isSandbox && P("submit")!=0 && P("r")!=0 && P("u")!=0
   && (goodCaptcha = captcha_is_correct(0))
   && cgi_csrf_safe(2)
  ){
    char *zDate;
    Blob cksum;
    Blob body;
    Blob wiki;

    blob_zero(&body);
    blob_append(&body, pWiki->zWiki, -1);
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
    manifest_destroy(pWiki);
    cgi_redirectf("wiki?name=%T", zPageName);
    return;
  }
  if( !isSandbox && P("cancel")!=0 ){
    manifest_destroy(pWiki);
    cgi_redirectf("wiki?name=%T", zPageName);
    return;
  }
  style_set_current_page("%T?name=%T", g.zPath, zPageName);
  style_set_current_feature("wiki");
  style_header("Append Comment To: %s", zPageName);
  if( !goodCaptcha ){
    @ <p class="generalError">Error: Incorrect security code.</p>
  }
  if( isSandbox ){
    @ <p class="generalError">Error: the Sandbox page may not
    @ be appended to.</p>
  }
  if( !isSandbox && P("preview")!=0 ){
    Blob preview;
    blob_zero(&preview);
    appendRemark(&preview, zMimetype);
    @ Preview:<hr>
    safe_html_context(DOCSRC_WIKI);
    wiki_render_by_mimetype(&preview, zMimetype);
    @ <hr>
    blob_reset(&preview);
  }
  zUser = PD("u", g.zLogin);
  form_begin(0, "%R/wikiappend");
  @ <input type="hidden" name="name" value="%h(zPageName)">
  @ <input type="hidden" name="mimetype" value="%h(zMimetype)">
  @ Your Name:
  @ <input type="text" name="u" size="20" value="%h(zUser)"><br>
  zFormat = mimetype_common_name(zMimetype);
  @ Comment to append (formatted as %s(zFormat)):<br>
  @ <textarea name="r" class="wikiedit" cols="80"
  @  rows="10" wrap="virtual">%h(PD("r",""))</textarea>
  @ <br>
  @ <input type="submit" name="preview" value="Preview Your Comment">
  @ <input type="submit" name="submit" value="Append Your Changes">
  @ <input type="submit" name="cancel" value="Cancel">
  captcha_generate(0);
  @ </form>
  manifest_destroy(pWiki);
  style_finish_page();
}

/*
** WEBPAGE: whistory
** URL: /whistory?name=PAGENAME
**
** Additional parameters:
**
**     showid          Show RID values
**
** Show the complete change history for a single wiki page.
*/
void whistory_page(void){
  Stmt q;
  const char *zPageName;
  double rNow;
  int showRid;
  char zAuthor[64];
  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  zPageName = PD("name","");
  style_set_current_feature("wiki");
  style_header("History Of %s", zPageName);
  showRid = P("showid")!=0;
  db_prepare(&q,
    "SELECT"
    "  event.mtime,"
    "  blob.uuid,"
    "  coalesce(event.euser,event.user),"
    "  event.objid,"
    "  datetime(event.mtime)"
    " FROM event, blob, tag, tagxref"
    " WHERE event.type='w' AND blob.rid=event.objid"
    "   AND tag.tagname='wiki-%q'"
    "   AND tagxref.tagid=tag.tagid AND tagxref.srcid=event.objid"
    " ORDER BY event.mtime DESC",
    zPageName
  );
  @ <h2>History of <a href="%R/wiki?name=%T(zPageName)">%h(zPageName)</a></h2>
  form_begin( "id='wh-form'", "%R/wdiff" );
  @   <input id="wh-pid" name="pid" type="radio" hidden>
  @   <input id="wh-id"  name="id"  type="hidden">
  @ </form>
  @ <style> .wh-clickable { cursor: pointer; } </style>
  @ <div class="brlist">
  @ <table>
  @ <thead><tr>
  @ <th>Age</th>
  @ <th>Hash</th>
  @ <th><span title="Baseline from which diffs are computed (click to unset)"
  @      id="wh-cleaner" class="wh-clickable">&#9875;</span></th>
  @ <th>User<span hidden class="wh-clickable"
  @                   id="wh-collapser">&emsp;&#9842;</span></th>
  if( showRid ){
    @ <th>RID</th>
  }
  @ <th>&nbsp;</th>
  @ </tr></thead><tbody>
  rNow = db_double(0.0, "SELECT julianday('now')");
  memset( zAuthor, 0, sizeof(zAuthor) );
  while( db_step(&q)==SQLITE_ROW ){
    double rMtime = db_column_double(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zUser = db_column_text(&q, 2);
    int wrid = db_column_int(&q, 3);
    const char *zWhen = db_column_text(&q, 4);
    /* sqlite3_int64 iMtime = (sqlite3_int64)(rMtime*86400.0); */
    char *zAge = human_readable_age(rNow - rMtime);
    if( strncmp( zAuthor, zUser, sizeof(zAuthor) - 1 ) == 0 ) {
      @ <tr class="wh-intermediate" title="%s(zWhen)">
    }
    else {
      strncpy( zAuthor, zUser, sizeof(zAuthor) - 1 );
      @ <tr class="wh-major" title="%s(zWhen)">
    }
    /* @ <td data-sortkey="%016llx(iMtime)">%s(zAge)</td> */
    @ <td>%s(zAge)</td>
    fossil_free(zAge);
    @ <td>%z(href("%R/info/%s",zUuid))%S(zUuid)</a></td>
    @ <td><input disabled type="radio" name="baseline" value="%S(zUuid)"/></td>
    @ <td>%h(zUser)<span class="wh-iterations" hidden></td>
    if( showRid ){
      @ <td>%z(href("%R/artifact/%S",zUuid))%d(wrid)</a></td>
    }
    @ <td>%z(chref("wh-difflink","%R/wdiff?id=%S",zUuid))diff</a></td>
    @ </tr>
  }
  @ </tbody></table></div>
  db_finalize(&q);
  builtin_request_js("fossil.page.whistory.js");
  /* style_table_sorter(); */
  style_finish_page();
}

/*
** WEBPAGE: wdiff
**
** Show the changes to a wiki page.
**
** Query parameters:
**
**      id=HASH           Hash prefix for the child version to be diffed.
**      rid=INTEGER       RecordID for the child version
**      pid=HASH          Hash prefix for the parent.
**
** The "id" query parameter is required.  "pid" is optional.  If "pid"
** is omitted, then the diff is against the first parent of the child.
*/
void wdiff_page(void){
  const char *zId;
  const char *zIdFull;
  const char *zPid;
  Manifest *pW1, *pW2 = 0;
  int rid1, rid2, nextRid;
  Blob w1, w2, d;
  DiffConfig DCfg;

  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  zId = P("id");
  if( zId==0 ){
    rid1 = atoi(PD("rid","0"));
  }else{
    rid1 = name_to_typed_rid(zId, "w");
  }
  zIdFull = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid1);
  if( zIdFull==0 ){
    if( zId ){
      webpage_notfound_error("No such wiki page: \"%s\"", zId);
    }else{
      webpage_notfound_error("No such wiki page: %d", rid1);
    }
    return;
  }
  zId = zIdFull;
  pW1 = manifest_get(rid1, CFTYPE_WIKI, 0);
  if( pW1==0 ) fossil_redirect_home();
  blob_init(&w1, pW1->zWiki, -1);
  zPid = P("pid");
  if( ( zPid==0 || zPid[0] == 0 ) && pW1->nParent ){
    zPid = pW1->azParent[0];
  }
  cgi_check_for_malice();
  if( zPid && zPid[0] != 0 ){
    char *zDate;
    rid2 = name_to_typed_rid(zPid, "w");
    pW2 = manifest_get(rid2, CFTYPE_WIKI, 0);
    blob_init(&w2, pW2->zWiki, -1);
    @ <h2>Changes to \
    @ "%z(href("%R/whistory?name=%s",pW1->zWikiTitle))%h(pW1->zWikiTitle)</a>" \
    zDate = db_text(0, "SELECT datetime(%.16g,toLocal())",pW2->rDate);
    @ between %z(href("%R/info/%s",zPid))%z(zDate)</a> \
    zDate = db_text(0, "SELECT datetime(%.16g,toLocal())",pW1->rDate);
    @ and %z(href("%R/info/%s",zId))%z(zDate)</a></h2>
    style_submenu_element("Previous", "%R/wdiff?id=%S", zPid);
  }else{
    blob_zero(&w2);
    @ <h2>Initial version of \
    @ "%z(href("%R/whistory?name=%s",pW1->zWikiTitle))%h(pW1->zWikiTitle)</a>"\
    @ </h2>
  }
  nextRid = wiki_next(wiki_tagid(pW1->zWikiTitle),pW1->rDate);
  if( nextRid ){
    style_submenu_element("Next", "%R/wdiff?rid=%d", nextRid);
  }
  style_set_current_feature("wiki");
  style_header("Changes To %s", pW1->zWikiTitle);
  blob_zero(&d);
  construct_diff_flags(1, &DCfg);
  DCfg.diffFlags |= DIFF_HTML | DIFF_LINENO;
  text_diff(&w2, &w1, &d, &DCfg);
  @ %s(blob_str(&d))
  manifest_destroy(pW1);
  manifest_destroy(pW2);
  style_finish_page();
}

/*
** A query that returns information about all wiki pages.
**
**    wname         Name of the wiki page
**    wsort         Sort names by this label
**    wrid          rid of the most recent version of the page
**    wmtime        time most recent version was created
**    wcnt          Number of versions of this wiki page
**
** The wrid value is zero for deleted wiki pages.
*/
static const char listAllWikiPages[] =
@ SELECT
@   substr(tag.tagname, 6) AS wname,
@   lower(substr(tag.tagname, 6)) AS sortname,
@   tagxref.value+0 AS wrid,
@   max(tagxref.mtime) AS wmtime,
@   count(*) AS wcnt
@ FROM
@   tag,
@   tagxref
@ WHERE
@   tag.tagname GLOB 'wiki-*'
@   AND tagxref.tagid=tag.tagid
@   AND TYPEOF(wrid)='integer' -- only wiki- tags which are wiki pages
@ GROUP BY 1
@ ORDER BY 2;
;

/*
** WEBPAGE: wcontent
**
**     all=1         Show deleted pages
**     showid        Show rid values for each page.
**
** List all available wiki pages with date created and last modified.
*/
void wcontent_page(void){
  Stmt q;
  double rNow;
  int showAll = P("all")!=0;
  int showRid = P("showid")!=0;
  int showCkBr;

  login_check_credentials();
  if( !g.perm.RdWiki ){ login_needed(g.anon.RdWiki); return; }
  style_set_current_feature("wiki");
  style_header("Available Wiki Pages");
  if( showAll ){
    style_submenu_element("Active", "%R/wcontent");
  }else{
    style_submenu_element("All", "%R/wcontent?all=1");
  }
  cgi_check_for_malice();
  showCkBr = db_exists(
    "SELECT tag.tagname AS tn FROM tag JOIN tagxref USING(tagid) "
    "WHERE ( tn GLOB 'wiki-checkin/*' OR tn GLOB 'wiki-branch/*' OR "
    "        tn GLOB 'wiki-tag/*'     OR tn GLOB 'wiki-ticket/*' ) "
    "  AND TYPEOF(tagxref.value+0)='integer'" );
  if( showCkBr ){
    showCkBr = P("showckbr")!=0;
    style_submenu_checkbox("showckbr", "Show associated wikis", 0, 0);
  }
  wiki_standard_submenu(W_ALL_BUT(W_LIST));
  db_prepare(&q, listAllWikiPages/*works-like:""*/);
  @ <div class="brlist">
  @ <table class='sortable' data-column-types='tKN' data-init-sort='1'>
  @ <thead><tr>
  @ <th>Name</th>
  @ <th>Last Change</th>
  @ <th>Versions</th>
  if( showRid ){
    @ <th>RID</th>
  }
  @ </tr></thead><tbody>
  rNow = db_double(0.0, "SELECT julianday('now')");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zWName = db_column_text(&q, 0);
    const char *zSort = db_column_text(&q, 1);
    int wrid = db_column_int(&q, 2);
    double rWmtime = db_column_double(&q, 3);
    sqlite3_int64 iMtime = (sqlite3_int64)(rWmtime*86400.0);
    char *zAge;
    int wcnt = db_column_int(&q, 4);
    char *zWDisplayName;

    if( !showCkBr &&
        (has_prefix("checkin/", zWName) ||
         has_prefix("branch/",  zWName) ||
         has_prefix("tag/",     zWName) ||
         has_prefix("ticket/",  zWName) )){
      continue;
    }
    if( has_prefix("checkin/",zWName) || has_prefix("ticket/",zWName) ){
      zWDisplayName = mprintf("%.25s...", zWName);
    }else{
      zWDisplayName = fossil_strdup(zWName);
    }
    if( wrid==0 ){
      if( !showAll ) continue;
      @ <tr><td data-sortkey="%h(zSort)">\
      @ %z(href("%R/whistory?name=%T",zWName))<s>%h(zWDisplayName)</s></a></td>
    }else{
      @ <tr><td data-sortkey="%h(zSort)">\
      @ %z(href("%R/wiki?name=%T&p",zWName))%h(zWDisplayName)</a></td>
    }
    zAge = human_readable_age(rNow - rWmtime);
    @ <td data-sortkey="%016llx(iMtime)">%s(zAge)</td>
    fossil_free(zAge);
    @ <td>%z(href("%R/whistory?name=%T",zWName))%d(wcnt)</a></td>
    if( showRid ){
      @ <td>%d(wrid)</td>
    }
    @ </tr>
    fossil_free(zWDisplayName);
  }
  @ </tbody></table></div>
  db_finalize(&q);
  style_table_sorter();
  style_finish_page();
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
  cgi_check_for_malice();
  style_set_current_feature("wiki");
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
  style_finish_page();
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
** Determine the rid for a tech note given either its id, its timestamp,
** or its tag. Returns 0 if there is no such item and -1 if the details
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
  if( !rid ) {
      /*
      ** At present, technote tags are prefixed with 'sym-', which shouldn't
      ** be the case, so we check for both with and without the prefix until
      ** such time as tags have the errant prefix dropped.
      */
      rid = db_int(0, "SELECT e.objid"
          "  FROM event e, tag t, tagxref tx"
          " WHERE e.type='e'"
          "   AND e.tagid IS NOT NULL"
          "   AND e.objid IN"
                      "       (SELECT rid FROM tagxref"
                      "         WHERE tagid=(SELECT tagid FROM tag"
                      "                       WHERE tagname GLOB '%q'))"
          "    OR e.objid IN"
                      "       (SELECT rid FROM tagxref"
                      "         WHERE tagid=(SELECT tagid FROM tag"
                      "                       WHERE tagname GLOB 'sym-%q'))"
          "   ORDER BY e.mtime DESC LIMIT 1",
       zETime, zETime);
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
** > fossil wiki export ?OPTIONS? PAGENAME ?FILE?
** > fossil wiki export ?OPTIONS? -t|--technote DATETIME|TECHNOTE-ID|TAG ?FILE?
**
**       Sends the latest version of either a wiki page or of a tech
**       note to the given file or standard output.  A filename of "-"
**       writes the output to standard output.  The directory parts of
**       the output filename are created if needed.
**       If PAGENAME is provided, the named wiki page will be output.
**
**       Options:
**         -t|--technote DATETIME|TECHNOTE-ID|TAG
**                    Specifies that a technote, rather than a wiki page,
**                    will be exported. If DATETIME is used, the most
**                    recently modified tech note with that DATETIME will
**                    output. If TAG is used, the most recently modified
**                    tech note with that TAG will be output.
**         -h|--html  The body (only) is rendered in HTML form, without
**                    any page header/foot or HTML/BODY tag wrappers.
**         -H|--HTML  Works like -h|-html but wraps the output in
**                    <html><body>...</body></html>.
**         -p|--pre   If -h|-H is used and the page or technote has
**                    the text/plain mimetype, its HTML-escaped output
**                    will be wrapped in <pre>...</pre>.
**
** > fossil wiki (create|commit) (PAGENAME | TECHNOTE-COMMENT) ?FILE? ?OPTIONS?
**
**       Create a new or commit changes to an existing wiki page or
**       technote from FILE or from standard input. PAGENAME is the
**       name of the wiki entry. TECHNOTE-COMMENT is the timeline comment of
**       the technote.
**
**       Options:
**         -M|--mimetype TEXT-FORMAT   The mime type of the update.
**                                     Defaults to the type used by
**                                     the previous version of the
**                                     page, or text/x-fossil-wiki.
**                                     Valid values are: text/x-fossil-wiki,
**                                     text/x-markdown and text/plain. fossil,
**                                     markdown or plain can be specified as
**                                     synonyms of these values.
**         -t|--technote DATETIME      Specifies the timestamp of
**                                     the technote to be created or
**                                     updated. The timestamp specifies when
**                                     this technote appears in the timeline
**                                     and is its permanent handle although
**                                     it may not be unique. When updating
**                                     a technote the most recently modified
**                                     tech note with the specified timestamp
**                                     will be updated.
**         -t|--technote TECHNOTE-ID   Specifies the technote to be
**                                     updated by its technote id, which is
**                                     its UUID.
**         --technote-tags TAGS        The set of tags for a technote.
**         --technote-bgcolor COLOR    The color used for the technote
**                                     on the timeline.
**
** > fossil wiki list ?OPTIONS?
** > fossil wiki ls ?OPTIONS?
**
**       Lists all wiki entries, one per line, ordered
**       case-insensitively by name.  Wiki pages associated with
**       check-ins and branches are NOT shown, unless -a is given.
**
**       Options:
**         --all                       Include "deleted" pages in output.
**                                     By default deleted pages are elided.
**         -t|--technote               Technotes will be listed instead of
**                                     pages. The technotes will be in order
**                                     of timestamp with the most recent
**                                     first.
**         -a|--show-associated        Show wiki pages associated with
**                                     check-ins and branches.
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
** The "Sandbox" wiki pseudo-page is a special case. Its name is
** checked case-insensitively and either "create" or "commit" may be
** used to update its contents.
*/
void wiki_cmd(void){
  int n;
  int isSandbox = 0;     /* true if dealing with sandbox pseudo-page */
  const int showAll = find_option("all", 0, 0)!=0;

  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    goto wiki_cmd_usage;
  }
  n = strlen(g.argv[2]);
  if( n==0 ){
    goto wiki_cmd_usage;
  }

  if( strncmp(g.argv[2],"export",n)==0 ){
    const char *zPageName = 0;    /* Name of the wiki page to export */
    const char *zFile;            /* Name of the output file (0=stdout) */
    const char *zETime;           /* The name of the technote to export */
    int rid = 0;                  /* Artifact ID of the wiki page */
    int i;                        /* Loop counter */
    char *zBody = 0;              /* Wiki page content */
    Blob body = empty_blob;       /* Wiki page content */
    Manifest *pWiki = 0;          /* Parsed wiki page content */
    int fHtml = 0;                /* Export in HTML form */
    FILE * pFile = 0;             /* Output file */
    int fPre = 0;                 /* Indicates that -h|-H should be
                                  ** wrapped in <pre>...</pre> if pWiki
                                  ** has the text/plain mimetype. */
    fHtml = find_option("HTML","H",0)!=0
      ? 2
      : (find_option("html","h",0)!=0 ? 1 : 0)
      /* 1 == -html, 2 == -HTML */;
    fPre = fHtml==0 ? 0 : find_option("pre","p",0)!=0;
    zETime = find_option("technote","t",1);
    verify_all_options();
    if( !zETime ){
      if( (g.argc!=4) && (g.argc!=5) ){
        usage("export ?-html? PAGENAME ?FILE?");
      }
      zPageName = g.argv[3];
      isSandbox = is_sandbox(zPageName);
      if(isSandbox){
        zBody = db_get("sandbox", 0);
      }else{
        wiki_fetch_by_name(zPageName, 0, &rid, &pWiki);
        if(pWiki){
          zBody = pWiki->zWiki;
        }
      }
      if( zBody==0 ){
        fossil_fatal("wiki page [%s] not found",zPageName);
      }
      zFile = (g.argc==4) ? "-" : g.argv[4];
    }else{
      if( (g.argc!=3) && (g.argc!=4) ){
        usage("export ?-html? ?FILE? --technote "
              "DATETIME|TECHNOTE-ID");
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
    if(fHtml==0){
      blob_append(&body, "\n", 1);
    }else{
      Blob html = empty_blob;   /* HTML-ized content */
      const char * zMimetype = isSandbox
        ? db_get("sandbox-mimetype", "text/x-fossil-wiki")
        : wiki_filter_mimetypes(pWiki->zMimetype);
      if( fossil_strcmp(zMimetype, "text/x-fossil-wiki")==0 ){
        wiki_convert(&body,&html,0);
      }else if( fossil_strcmp(zMimetype, "text/x-markdown")==0 ){
        markdown_to_html(&body,0,&html);
        safe_html_context(DOCSRC_WIKI);
        safe_html(&html);
      }else if( fossil_strcmp(zMimetype, "text/plain")==0 ){
        htmlize_to_blob(&html,zBody,i);
      }else{
        fossil_fatal("Unsupported MIME type '%s' for wiki page '%s'.",
                     zMimetype, pWiki ? pWiki->zWikiTitle : zPageName );
      }
      blob_reset(&body);
      body = html /* transfer memory */;
    }
    pFile = fossil_fopen_for_output(zFile);
    if(fHtml==2){
      fwrite("<html><body>", 1, 12, pFile);
    }
    if(fPre!=0){
      fwrite("<pre>", 1, 5, pFile);
    }
    fwrite(blob_buffer(&body), 1, blob_size(&body), pFile);
    if(fPre!=0){
      fwrite("</pre>", 1, 6, pFile);
    }
    if(fHtml==2){
      fwrite("</body></html>\n", 1, 15, pFile);
    }
    fossil_fclose(pFile);
    blob_reset(&body);
    manifest_destroy(pWiki);
    return;
  }else if( strncmp(g.argv[2],"commit",n)==0
            || strncmp(g.argv[2],"create",n)==0 ){
    const char *zPageName;        /* page name */
    Blob content;                 /* Input content */
    int rid = 0;
    Manifest *pWiki = 0;          /* Parsed wiki page content */
    const int isCreate = 'r'==g.argv[2][1] /* else "commit" */;
    const char *zMimeType = find_option("mimetype", "M", 1);
    const char *zETime = find_option("technote", "t", 1);
    const char *zTags = find_option("technote-tags", NULL, 1);
    const char *zClr = find_option("technote-bgcolor", NULL, 1);
    verify_all_options();
    if( g.argc!=4 && g.argc!=5 ){
      usage("commit|create PAGENAME ?FILE? [--mimetype TEXT-FORMAT]"
            " [--technote DATETIME] [--technote-tags TAGS]"
            " [--technote-bgcolor COLOR]");
    }
    zPageName = g.argv[3];
    if( g.argc==4 ){
      blob_read_from_channel(&content, stdin, -1);
    }else{
      blob_read_from_file(&content, g.argv[4], ExtFILE);
    }
    isSandbox = is_sandbox(zPageName);
    if ( !zETime ){
      if( !isSandbox ){
        wiki_fetch_by_name(zPageName, 0, &rid, &pWiki);
      }
    }else{
      rid = wiki_technote_to_rid(zETime);
      if( rid>0 ){
        pWiki = manifest_get(rid, CFTYPE_EVENT, 0);
      }
    }
    if( !zMimeType || !*zMimeType ){
      /* Try to deduce the mimetype based on the prior version. */
      if(isSandbox){
        zMimeType =
          wiki_filter_mimetypes(db_get("sandbox-mimetype",
                                       "text/x-fossil-wiki"));
      }else if( pWiki!=0 && (pWiki->zMimetype && *pWiki->zMimetype) ){
        zMimeType = pWiki->zMimetype;
      }
    }else{
      zMimeType = wiki_filter_mimetypes(zMimeType);
    }
    if( isCreate && rid>0 ){
      if ( !zETime ){
        fossil_fatal("wiki page %s already exists", zPageName);
      }else{
        /* Creating a tech note with same timestamp is permitted
           and should create a new tech note */
        rid = 0;
      }
    }else if( !isCreate && rid==0 && isSandbox==0 ){
      if ( !zETime ){
        fossil_fatal("no such wiki page: %s", zPageName);
      }else{
        fossil_fatal("no such tech note: %s", zETime);
      }
    }

    if( !zETime ){
      if(isSandbox){
        db_set("sandbox",blob_str(&content),0);
        db_set("sandbox-mimetype",zMimeType,0);
        fossil_print("Updated sandbox pseudo-page.\n");
      }else{
        wiki_cmd_commit(zPageName, rid, &content, zMimeType, 1);
        if( g.argv[2][1]=='r' ){
          fossil_print("Created new wiki page %s.\n", zPageName);
        }else{
          fossil_print("Updated wiki page %s.\n", zPageName);
        }
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
    if( g.argc!=4 ){
      usage("delete PAGENAME");
    }
    fossil_fatal("delete not yet implemented.");
  }else if(( strncmp(g.argv[2],"list",n)==0 )
          || ( strncmp(g.argv[2],"ls",n)==0 )){
    Stmt q;
    const int fTechnote = find_option("technote","t",0)!=0;
    const int showIds = find_option("show-technote-ids","s",0)!=0;
    const int showCkBr = find_option("show-associated","a",0)!=0;
    verify_all_options();
    if (fTechnote==0){
      db_prepare(&q, listAllWikiPages/*works-like:""*/);
    }else{
      db_prepare(&q,
        "SELECT datetime(e.mtime), substr(t.tagname,7), e.objid"
         " FROM event e, tag t"
        " WHERE e.type='e'"
          " AND e.tagid IS NOT NULL"
          " AND t.tagid=e.tagid"
        " ORDER BY e.mtime DESC /*sort*/"
      );
    }
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      const int wrid = db_column_int(&q, 2);
      if(!showAll && !wrid){
        continue;
      }
      if( !showCkBr &&
          (has_prefix("checkin/", zName) ||
           has_prefix("branch/",  zName) ||
           has_prefix("tag/",     zName) ||
           has_prefix("ticket/",  zName) ) ){
        continue;
      }
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
** Allowed flags for wiki_render_associated
*/
#if INTERFACE
#define WIKIASSOC_FULL_TITLE  0x00001   /* Full title */
#define WIKIASSOC_MENU_READ   0x00002   /* Add submenu link to read wiki */
#define WIKIASSOC_MENU_WRITE  0x00004   /* Add submenu link to add wiki */
#define WIKIASSOC_ALL         0x00007   /* All of the above */
#endif

/*
** Show the default Section label for an associated wiki page.
*/
static void wiki_section_label(
  const char *zPrefix,   /* "branch", "tag", or "checkin" */
  const char *zName,     /* Name of the object */
  unsigned int mFlags    /* Zero or more WIKIASSOC_* flags */
){
  if( (mFlags & WIKIASSOC_FULL_TITLE)==0 ){
    @ <div class="section accordion">About</div>
  }else if( zPrefix[0]=='c' ){  /* checkin/... */
    @ <div class="section accordion">About check-in %.20h(zName)</div>
  }else{
    @ <div class="section accordion">About %s(zPrefix) %h(zName)</div>
  }
}

/*
** Add an "Wiki" button in a submenu that links to the read-wiki page.
*/
static void wiki_submenu_to_read_wiki(
  const char *zPrefix,   /* "branch", "tag", or "checkin" */
  const char *zName,     /* Name of the object */
  unsigned int mFlags    /* Zero or more WIKIASSOC_* flags */
){
  if( g.perm.RdWiki && (mFlags & WIKIASSOC_MENU_READ)!=0 ){
    style_submenu_element("Wiki", "%R/wiki?name=%s/%t", zPrefix, zName);
  }
}

/*
** Add an "Edit Wiki" button in a submenu that links to the edit-wiki page.
*/
static void wiki_submenu_to_edit_wiki(
  const char *zPrefix,   /* "branch", "tag", or "checkin" */
  const char *zName,     /* Name of the object */
  unsigned int mFlags   /* Zero or more WIKIASSOC_* flags */
){
  if( g.perm.WrWiki && (mFlags & WIKIASSOC_MENU_WRITE)!=0 ){
    style_submenu_element("Edit Wiki", "%R/wikiedit?name=%s/%t", zPrefix, zName);
  }
}

/*
** Check to see if there exists a wiki page with a name zPrefix/zName.
** If there is, then render a <div class='section'>..</div> and
** return true.
**
** If there is no such wiki page, return false.
*/
int wiki_render_associated(
  const char *zPrefix,   /* "branch", "tag", "ticket", or "checkin" */
  const char *zName,     /* Name of the object */
  unsigned int mFlags    /* Zero or more WIKIASSOC_* flags */
){
  int rid;
  Manifest *pWiki;
  if( !db_get_boolean("wiki-about",1) ) return 0;
  rid = db_int(0,
    "SELECT rid FROM tagxref"
    " WHERE tagid=(SELECT tagid FROM tag WHERE tagname='wiki-%q/%q')"
    " ORDER BY mtime DESC LIMIT 1",
    zPrefix, zName
  );
  pWiki = rid==0 ? 0 : manifest_get(rid, CFTYPE_WIKI, 0);
  if( pWiki==0 || pWiki->zWiki==0 || pWiki->zWiki[0]==0 ){
    if( g.perm.WrWiki && g.perm.Write && (mFlags & WIKIASSOC_MENU_WRITE)!=0 ){
      style_submenu_element("Add Wiki", "%R/wikiedit?name=%s/%t",
                            zPrefix, zName);
    }
    return 0;
  }
  if( fossil_strcmp(pWiki->zMimetype, "text/x-markdown")==0 ){
    Blob tail = BLOB_INITIALIZER;
    Blob title = BLOB_INITIALIZER;
    Blob markdown;
    blob_init(&markdown, pWiki->zWiki, -1);
    markdown_to_html(&markdown, &title, &tail);
    if( blob_size(&title) ){
      @ <div class="section accordion">%h(blob_str(&title))</div>
    }else{
      wiki_section_label(zPrefix, zName, mFlags);
    }
    wiki_submenu_to_read_wiki(zPrefix, zName, mFlags);
    wiki_submenu_to_edit_wiki(zPrefix, zName, mFlags);
    @ <div class="accordion_panel">
    safe_html_context(DOCSRC_WIKI);
    safe_html(&tail);
    convert_href_and_output(&tail);
    @ </div>
    blob_reset(&tail);
    blob_reset(&title);
    blob_reset(&markdown);
  }else if( fossil_strcmp(pWiki->zMimetype, "text/plain")==0 ){
    wiki_section_label(zPrefix, zName, mFlags);
    wiki_submenu_to_edit_wiki(zPrefix, zName, mFlags);
    @ <div class="accordion_panel"><pre>
    @ %h(pWiki->zWiki)
    @ </pre></div>
  }else{
    Blob tail = BLOB_INITIALIZER;
    Blob title = BLOB_INITIALIZER;
    Blob wiki;
    Blob *pBody;
    blob_init(&wiki, pWiki->zWiki, -1);
    if( wiki_find_title(&wiki, &title, &tail) ){
      @ <div class="section accordion">%h(blob_str(&title))</div>
      pBody = &tail;
    }else{
      wiki_section_label(zPrefix, zName, mFlags);
      pBody = &wiki;
    }
    wiki_submenu_to_edit_wiki(zPrefix, zName, mFlags);
    @ <div class="accordion_panel"><div class="wiki">
    wiki_convert(pBody, 0, WIKI_BUTTONS);
    @ </div></div>
    blob_reset(&tail);
    blob_reset(&title);
    blob_reset(&wiki);
  }
  manifest_destroy(pWiki);
  builtin_request_js("accordion.js");
  return 1;
}
