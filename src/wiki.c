/*
** Copyright (c) 2007 D. Richard Hipp
** Copyright (c) 2008 Stephan Beal
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
  char *zHomePage;        /* name of home page */
  char *zProjName;        /* name of project */
  zProjName = db_get("project-name",0);
  zHomePage = db_get("project-home", zProjName );
  if( zProjName && zProjName[0] ){
    /* beware: this code causes cyclic redirects on a 404 because
       not_found is directed here.
     */
    int lenP;             /* strncmp() bounder */
    int lenH;             /* length of zProjName */
    if(  zHomePage && ! zHomePage[0] ){
        zHomePage = zProjName;
    }
    lenP = strlen(zProjName);
    lenH = strlen(zHomePage);
    if( lenP < lenH ) lenP = lenH;
    if( (zProjName == zHomePage) || (0==strncmp(zProjName,zHomePage,lenP)) ||
      (0==strncmp(zHomePage,"home",lenP)/*avoid endless loop*/) ){
        login_check_credentials();
        g.zExtra = zHomePage;
        cgi_set_parameter_nocopy("name", g.zExtra);
        g.okRdWiki = 1;
        g.okApndWiki = 0;
        g.okWrWiki = 0;
        g.okHistory = 0;
        wiki_page();
    }else{
        cgi_redirect( zHomePage );
    }
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
  int rid;
  int isSandbox;
  Blob wiki;
  Manifest m;
  const char *zPageName;
  char *zHtmlPageName;
  char *zBody = mprintf("%s","<i>Empty Page</i>");

  login_check_credentials();
  if( !g.okRdWiki ){ login_needed(); return; }
  zPageName = P("name");
  if( zPageName==0 ){
    style_header("Wiki");
    @ <ul>
    @ <li> <a href="%s(g.zBaseURL)/timeline?y=w">Recent changes</a> to wiki
    @      pages. </li>
    @ <li> <a href="%s(g.zBaseURL)/wiki_rules">Formatting rules</a> for 
    @      wiki.</li>
    @ <li> Use the <a href="%s(g.zBaseURL)/wiki?name=Sandbox">Sandbox</a>
    @      to experiment.</li>
    @ <li> <a href="%s(g.zBaseURL)/wcontent">List of All Wiki Pages</a>
    @      available on this server.</li>
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
      if( m.type==CFTYPE_WIKI ){
        zBody = m.zWiki;
      }
    }
  }
  if( isSandbox || (rid && g.okWrWiki) || (!rid && g.okNewWiki) ){
    style_submenu_element("Edit", "Edit Wiki Page", "%s/wikiedit?name=%T",
         g.zTop, zPageName);
  }
  if( isSandbox || (rid && g.okApndWiki) ){
    style_submenu_element("Append", "Add A Comment", "%s/wikiappend?name=%T",
         g.zTop, zPageName);
  }
  if( !isSandbox && g.okHistory ){
    style_submenu_element("History", "History", "%s/whistory?name=%T",
         g.zTop, zPageName);
  }
  zHtmlPageName = mprintf("%h", zPageName);
  style_header(zHtmlPageName);
  blob_init(&wiki, zBody, -1);
  wiki_convert(&wiki, 0, 0);
  blob_reset(&wiki);
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
  zHtmlPageName = mprintf("Edit: %h", zPageName);
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
  if( n>200 ) n = 200;
  @ <form method="POST" action="%s(g.zBaseURL)/wikiedit">
  @ <input type="hidden" name="name" value="%h(zPageName)">
  @ <textarea name="w" class="wikiedit" cols="80" 
  @  rows="%d(n)" wrap="virtual">%h(zBody)</textarea>
  @ <br>
  @ <input type="submit" name="preview" value="Preview Your Changes">
  @ <input type="submit" name="submit" value="Apply These Changes">
  @ <input type="submit" name="cancel" value="Cancel">
  @ </form>
  if( !isSandbox ){
    manifest_clear(&m);
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

  zDate = db_text(0, "SELECT datetime('now')");
  blob_appendf(p, "\n\n<hr><i>On %s UTC %h", zDate, g.zLogin);
  free(zDate);
  zUser = PD("u",g.zLogin);
  if( zUser[0] && strcmp(zUser,g.zLogin) ){
    blob_appendf(p, " (claiming to be %h)", zUser);
  }
  zRemark = PD("r","");
  blob_appendf(p, " added:</i><br />\n%s", zRemark);
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
      cgi_redirect("index");
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
      content_get(rid, &content);
      manifest_parse(&m, &content);
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
    }
    cgi_redirectf("wiki?name=%T", zPageName);
  }
  if( P("cancel")!=0 ){
    cgi_redirectf("wiki?name=%T", zPageName);
    return;
  }
  zHtmlPageName = mprintf("Append Comment To: %h", zPageName);
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
  zTitle = mprintf("History Of %h", zPageName);
  style_header(zTitle);
  free(zTitle);

  zSQL = mprintf("%s AND event.objid IN "
                 "  (SELECT rid FROM tagxref WHERE tagid="
                       "(SELECT tagid FROM tag WHERE tagname='wiki-%q'))"
                 "ORDER BY mtime DESC",
                 timeline_query_for_www(), zPageName);
  db_prepare(&q, zSQL);
  free(zSQL);
  www_print_timeline(&q);
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
  @ <li> Blank lines are paragraph breaks
  @ <li> Bullet list items are a "*" at the beginning of the line.
  @ <li> Enumeration list items are a number at the beginning of a line.
  @ <li> Indented pargraphs begin with a tab or two spaces.
  @ <li> Hyperlinks are contained with square brackets:  "[target]"
  @ <li> Most ordinary HTML works.
  @ <li> &lt;verbatim&gt; and &lt;nowiki&gt;.
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
  @ A bullet list item begins with a single "*" character surrounded on
  @ both sides by two or more spaces or by a tab.  Only a single level
  @ of bullet list is supported by wiki.  For tested lists, use HTML.</p>
  @ <li> <p><b>Enumeration Lists</b>.
  @ An enumeration list item begins with one or more digits optionally
  @ followed by a "." surrounded on both sides by two or more spaces or
  @ by a tab.  The number is significant and becomes the number shown
  @ in the rendered enumeration item.  Only a single level of enumeration
  @ list is supported by wiki.  For nested enumerations or for
  @ enumerations that count using letters or roman numerials, use HTML.</p>
  @ <li> <p><b>Indented Paragraphs</b>.
  @ Any paragraph that begins with two or more spaces or a tab and
  @ which is not a bullet or enumeration list item is rendered 
  @ indented.  Only a single level of indentation is supported by</p>
  @ <li> <p><b>Hyperlinks</b>.
  @ Text within square brackets ("[...]") becomes a hyperlink.  The
  @ target can be a wiki page name, the UUID of a check-in or ticket,
  @ the name of an image, or a URL.  By default, the target is displayed
  @ as the text of the hyperlink.  But you can specify alternative text
  @ after the target name separated by a "|" character.</p>
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
  @ are elided.</p>
  @ <p>The &lt;verbatim&gt; tag disables all wiki and HTML markup
  @ up through the next &lt;/verbatim&gt;.  The &lt;nowiki&gt; tag
  @ disables all wiki formatting rules through the matching
  @ &lt;/nowiki&gt; element.
  @ </ol>
  style_footer();
}

/*
** wiki_cmd_commit() is the implementation of "wiki commit ...".
**
** As arguments it expects:
**
** zPageName = the wiki entry's name.
**
** in = input file. The file is read until EOF but is not closed
** by this function (it might be stdin!).
**
** Returns 0 on error, non-zero on success.
**
** TODOs:
** - take EITHER zPageName OR rid. We don't need both.
** - make use of the return value. Add more error checking.
** - give the uuid back to the caller so it can be shown
**   in the status output. ("committed version XXXXX of page ...")
** - return some status telling the user if there were no diffs
** (i.e. no commit). How can we find this out?
*/
int wiki_cmd_commit( char const * zPageName, FILE * in )
{
  Blob wiki;              /* Wiki page content */
  Blob content;           /* read-in content */
  Blob cksum;             /* wiki checksum */
  int rid;                /* rid of existing entry. */
  int nrid;               /* not really sure */
  char * zDate;           /* timestamp */
  char * zUuid;           /* uuid for rid */

  rid = db_int(0, "SELECT x.rid FROM tag t, tagxref x"
               " WHERE x.tagid=t.tagid AND t.tagname='wiki-%q'"
	       " ORDER BY x.mtime DESC LIMIT 1",
               zPageName
               );
  if( ! rid ){
    fossil_fatal("wiki commit NewEntry not yet implemented.");
  }


  blob_read_from_channel( &content, in, -1 );
  // ^^^ Reminder: we should allow empty (zero-byte) entries, so don't exit
  // if read returns 0.
  blob_zero(&wiki);
  zDate = db_text(0, "SELECT datetime('now')");
  zDate[10] = 'T';
  blob_appendf(&wiki, "D %s\n", zDate);
  free(zDate);
  blob_appendf(&wiki, "L %F\n", zPageName );
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  blob_appendf(&wiki, "P %s\n", zUuid);
  free(zUuid);
  user_select();
  if( g.zLogin ){
      blob_appendf(&wiki, "U %F\n", g.zLogin);
  }
  blob_appendf( &wiki, "W %d\n%s\n", blob_size(&content),
                blob_buffer(&content) );
  blob_reset(&content);
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
  return 1;
}

/*
** COMMAND: wiki
**
** Usage: %fossil wiki (export|commit|list) WikiName
**
** Run various subcommands to fetch wiki entries.
**
**     %fossil wiki export WikiName
**
**        Sends the latest version of the WikiName wiki
**        entry to stdout.
**
**     %fossil wiki commit WikiName
**
**        Commit changes to a wiki page from standard input.
**        It cannot currently create a new entry (this is on the
**        to-fix list).
**
**     %fossil wiki list
**
**        Lists all wiki entries, one per line, ordered
**        case-insentively by name.
**
** TODOs:
**
**     %fossil wiki export ?UUID? ?-f outfile[=stdout]? WikiName
**
**        Outputs the selected version of WikiName to the selected file.
**
**     %fossil wiki delete ?-m MESSAGE? WikiName
**
**        The same as deleting a file entry, but i don't know if fossil
**        supports a commit message for Wiki entries.
**
**     %fossil wiki ?-u? ?-d? ?-s=[|]? list
**
**        Lists the UUID and/or Date of last change for each entry, delimited
**        by the -s char.
**
**     %fossil wiki commit ?-f infile[=stdin]? WikiName
**
**        Commit changes to a wiki page from a file or standard input.
**        It creats a new entry if needed (or is that philosophically
**        wrong?).
**
**     %fossil wiki diff ?UUID? ?-f infile[=stdin]? EntryName
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
    char const *zPageName;            /* Name of the wiki page to export */
    int rid;                /* Artifact ID of the wiki page */
    int i;                  /* Loop counter */
    char *zBody = 0;        /* Wiki page content */
    Manifest m;             /* Parsed wiki page content */
    if( g.argc!=4 ){
      usage("export PAGENAME");
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
    printf("%.*s\n", i, zBody);
    return;
  }else
  if( strncmp(g.argv[2],"commit",n)==0 ){
    char *zPageName;
    if( g.argc!=4 ){
      usage("commit PAGENAME");
    }
    zPageName = g.argv[3];
    wiki_cmd_commit( zPageName, stdin );
    printf("Committed wiki page %s.\n", zPageName);
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
      " ORDER BY lower(tagname)"
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
  usage("export|commit|list ...");
}
