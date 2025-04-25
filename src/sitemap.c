/*
** Copyright (c) 2014 D. Richard Hipp
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
** This file contains code to implement the sitemap webpage.
*/
#include "config.h"
#include "sitemap.h"
#include <assert.h>

/*
** WEBPAGE: sitemap
**
** List some of the web pages offered by the Fossil web engine.  This
** page is intended as a supplement to the menu bar on the main screen.
** That is, this page is designed to hold links that are omitted from
** the main menu due to lack of space.
**
** Additional entries defined by the "sitemap-extra" setting are included
** in the sitemap.  "sitemap-extra" should be a TCL script with three
** values per entry:
**
**    *    The displayed text
**
**    *    The URL
**
**    *    A "capexpr" expression that determines whether or not to include
**         the entry based on user capabilities.  "*" means always include
**         the entry and "{}" means never.
**
** If the "e=1" query parameter is present, then the standard content
** is omitted and only the sitemap-extra content is shown.  If "e=2" is
** present, then only the standard content is shown and sitemap-extra
** content is omitted.
**
** If the "popup" query parameter is present and this is a POST request
** from the same origin, then the normal HTML header and footer information
** is omitted and the HTML text returned is just a raw "<ul>...</ul>".
*/
void sitemap_page(void){
  int srchFlags;
  int inSublist = 0;
  int i;
  int isPopup = 0;         /* This is an XMLHttpRequest() for /sitemap */
  int e = atoi(PD("e","0"));
  const char *zExtra;

  login_check_credentials();
  if( P("popup")!=0 ){
    /* The "popup" query parameter
    ** then disable anti-robot defenses */
    isPopup = 1;
    g.perm.Hyperlink = 1;
    g.jsHref = 0;
  }
  srchFlags = search_restrict(SRCH_ALL);
  if( !isPopup ){
    style_header("Site Map");
    style_adunit_config(ADUNIT_RIGHT_OK);
  }

  @ <ul id="sitemap" class="columns" style="column-width:20em">
  if( (e&1)==0 ){
    @ <li>%z(href("%R/home"))Home Page</a>
  }

  zExtra = db_get("sitemap-extra",0);
  if( zExtra && (e&2)==0 ){
    int rc;
    char **azExtra = 0;
    int *anExtra;
    int nExtra = 0;
    if( isPopup ) Th_FossilInit(0);
    if( (e&1)!=0 ) inSublist = 1;
    rc = Th_SplitList(g.interp, zExtra, (int)strlen(zExtra),
                      &azExtra, &anExtra, &nExtra);
    if( rc==TH_OK && nExtra ){
      for(i=0; i+2<nExtra; i+=3){
        int nResult = 0;
        const char *zResult;
        int iCond = 0;
        rc = capexprCmd(g.interp, 0, 2,
                (const char**)&azExtra[i+1], (int*)&anExtra[i+1]);
        if( rc!=TH_OK ) continue;
        zResult = Th_GetResult(g.interp, &nResult);
        Th_ToInt(g.interp, zResult, nResult, &iCond);
        if( iCond==0 ) continue;
        if( !inSublist ){
          @ <ul>
          inSublist = 1;
        }
        if( azExtra[i+1][0]=='/' ){
          @ <li>%z(href("%R%s",azExtra[i+1]))%h(azExtra[i])</a></li>
        }else{
          @ <li>%z(href("%s",azExtra[i+1]))%s(azExtra[i])</a></li>
        }
      }
    }
    Th_Free(g.interp, azExtra);
  }
  if( (e&1)!=0 ) goto end_of_sitemap;

  if( inSublist ){
    @ </ul>
    inSublist = 0;
  }
  @ </li>
  if( cgi_is_loopback(g.zIpAddr) && db_open_local(0) ){
    @ <li>%z(href("%R/ckout"))Checkout Status</a></li>
  }
  if( g.perm.Read ){
    const char *zEditGlob = db_get("fileedit-glob","");
    @ <li>%z(href("%R/tree"))File Browser</a>
    @   <ul>
    @   <li>%z(href("%R/tree?type=tree&ci=trunk"))Tree-view,
    @        Trunk Check-in</a></li>
    @   <li>%z(href("%R/tree?type=flat"))Flat-view</a></li>
    @   <li>%z(href("%R/fileage?name=trunk"))File ages for Trunk</a></li>
    @   <li>%z(href("%R/uvlist"))Unversioned Files</a>
    if( g.perm.Write && zEditGlob[0]!=0 ){
      @   <li>%z(href("%R/fileedit"))On-line File Editor</li>
    }
    @ </ul>
  }
  if( g.perm.Read ){
    @ <li>%z(href("%R/timeline"))Project Timeline</a>
    @ <ul>
    @   <li>%z(href("%R/reports"))Activity Reports</a></li>
    @   <li>%z(href("%R/sitemap-timeline"))Other timelines</a></li>
    @ </ul>
    @ </li>
  }
  if( g.perm.Read ){
    @ <li>%z(href("%R/brlist"))Branches</a>
    @ <ul>
    @   <li>%z(href("%R/taglist"))Tags</a></li>
    @   <li>%z(href("%R/leaves"))Leaf Check-ins</a></li>
    @ </ul>
    @ </li>
  }
  if( srchFlags ){
    @ <li>%z(href("%R/search"))Search</a></li>
  }
  if( g.perm.Chat ){
    @ <li>%z(href("%R/chat"))Chat</a></li>
  }
  if( g.perm.RdForum ){
    const char *zTitle = db_get("forum-title","Forum");
    @ <li>%z(href("%R/forum"))%h(zTitle)</a>
    @ <ul>
    @   <li>%z(href("%R/timeline?y=f"))Recent activity</a></li>
    @ </ul>
    @ </li>
  }
  if( g.perm.RdTkt ){
    @ <li>%z(href("%R/reportlist"))Tickets/Bug Reports</a>
    @   <ul>
    if( srchFlags & SRCH_TKT ){
      @   <li>%z(href("%R/tktsrch"))Ticket Search</a></li>
    }
    @   <li>%z(href("%R/timeline?y=t"))Recent activity</a></li>
    @   <li>%z(href("%R/attachlist"))List of Attachments</a></li>
    @   </ul>
    @ </li>
  }
  if( g.perm.RdWiki ){
    @ <li>%z(href("%R/wikihelp"))Wiki</a>
    @   <ul>
    if( srchFlags & SRCH_WIKI ){
      @     <li>%z(href("%R/wikisrch"))Wiki Search</a></li>
    }
    @     <li>%z(href("%R/wcontent"))List of Wiki Pages</a></li>
    @     <li>%z(href("%R/timeline?y=w"))Recent activity</a></li>
    @     <li>%z(href("%R/wikiedit?name=Sandbox"))Wiki Sandbox</a></li>
    @     <li>%z(href("%R/attachlist"))List of Attachments</a></li>
    @     <li>%z(href("%R/pikchrshow"))Pikchr Sandbox</a></li>
    @   </ul>
    @ </li>
  }

  if( !g.zLogin ){
    @ <li>%z(href("%R/login"))Login</a>
    @ <ul>
    if( login_self_register_available(0) ){
       @ <li>%z(href("%R/register"))Create a new account</a></li>
    }
  }else {
    @ <li>%z(href("%R/logout"))Logout from %h(g.zLogin)</a>
    @ <ul>
    if( g.perm.Password ){
      @ <li>%z(href("%R/logout"))Change Password for %h(g.zLogin)</a></li>
    }
  }
  if( alert_enabled() && g.perm.EmailAlert ){
    if( login_is_individual() ){
      @ <li>%z(href("%R/alerts"))Email Alerts for %h(g.zLogin)</a></li>
    }else{
      @ <li>%z(href("%R/subscribe"))Subscribe to Email Alerts</a></li>
    }
  }
  @ <li>%z(href("%R/cookies"))Cookies</a></li>
  @ </ul>
  @ </li>

  if( g.perm.Read ){
    @ <li>%z(href("%R/stat"))Repository Status</a>
    @   <ul>
    @   <li>%z(href("%R/hash-collisions"))Collisions on hash prefixes</a></li>
    if( g.perm.Admin ){
      @   <li>%z(href("%R/urllist"))List of URLs used to access
      @       this repository</a></li>
    }
    @   <li>%z(href("%R/bloblist"))List of Artifacts</a></li>
    @   </ul>
    @ </li>
  }
  @ <li>%z(href("%R/help"))Help</a>
  @   <ul>
  if( g.perm.Admin || g.perm.Write ||
      g.perm.WrForum || g.perm.WrTForum ||
      g.perm.NewWiki || g.perm.ApndWiki || g.perm.WrWiki || g.perm.ModWiki ||
      g.perm.NewTkt  || g.perm.ApndTkt  || g.perm.WrTkt  || g.perm.ModTkt ){
    @   <li>%z(href("%R/wiki_rules"))Wiki Formatting Rules</a></li>
    @   <li>%z(href("%R/md_rules"))Markdown Formatting Rules</a></li>
  }
  @   <li>%z(href("%R/test-all-help"))All "help" text on a single page</a></li>
  if( g.perm.Admin || g.perm.Write || g.perm.WrUnver ){
    @   <li>%z(href("%R/mimetype_list"))\
    @ Filename suffix to MIME type map</a></li>
  }
  @   </ul></li>
  if( g.perm.Admin ){
    @ <li><a href="%R/setup">Administration Pages</a>
    @   <ul>
    @   <li><a href="%R/secaudit0">Security Audit</a></li>
    @   <li><a href="%R/modreq">Pending Moderation Requests</a></li>
    @   </ul></li>
  }
  @ <li>%z(href("%R/skins"))Skins</a></li>
  @ <li>%z(href("%R/sitemap-test"))Test Pages</a></li>
  if( isPopup ){
    @ <li>%z(href("%R/sitemap"))Site Map</a></li>
  }

end_of_sitemap:
  @ </ul>
  if( !isPopup ){
    style_finish_page();
  }
}

/*
** WEBPAGE: sitemap-test
**
** List some of the web pages offered by the Fossil web engine for testing
** purposes.  This is similar to /sitemap, but is focused only on showing
** pages associated with testing.
*/
void sitemap_test_page(void){
  int isPopup = 0;         /* This is an XMLHttpRequest() for /sitemap */

  login_check_credentials();
  style_set_current_feature("sitemap");
  if( P("popup")!=0 && cgi_csrf_safe(0) ){
    /* If this is a POST from the same origin with the popup=1 parameter,
    ** then disable anti-robot defenses */
    isPopup = 1;
    g.perm.Hyperlink = 1;
    g.jsHref = 0;
  }
  if( !isPopup ){
    style_header("Test Page Map");
    style_adunit_config(ADUNIT_RIGHT_OK);
  }
  @ <ul id="sitemap" class="columns" style="column-width:20em">
  if( g.perm.Admin || db_get_boolean("test_env_enable",0) ){
    @ <li>%z(href("%R/test-env"))CGI Environment Test</a></li>
  }
  if( g.perm.Read ){
    @ <li>%z(href("%R/test-rename-list"))List of file renames</a></li>
  }
  @ <li>%z(href("%R/test-builtin-files"))List of built-in files</a></li>
  @ <li>%z(href("%R/mimetype_list"))List of MIME types</a></li>
  @ <li>%z(href("%R/hash-color-test"))Hash color test</a>
  @ <li>%z(href("%R/test-bgcolor"))Background color test</a>
  if( g.perm.Admin ){
    @ <li>%z(href("%R/test-backlinks"))List of backlinks</a></li>
    @ <li>%z(href("%R/test-backlink-timeline"))Backlink timeline</a></li>
    @ <li>%z(href("%R/phantoms"))List of phantom artifacts</a></li>
    @ <li>%z(href("%R/test-warning"))Error Log test page</a></li>
    @ <li>%z(href("%R/repo_stat1"))Repository <tt>sqlite_stat1</tt> table</a>
    @ <li>%z(href("%R/repo_schema"))Repository schema</a></li>
  }
  if( g.perm.Read && g.perm.Hyperlink ){
    @ <li>%z(href("%R/timewarps"))Timeline of timewarps</a></li>
  }
  @ <li>%z(href("%R/cookies"))Content of display preference cookie</a></li>
  @ <li>%z(href("%R/test-captcha"))Random ASCII-art Captcha image</a></li>
  @ <li>%z(href("%R/test-piechart"))Pie-Chart generator test</a></li>
  if( !isPopup ){
    style_finish_page();
  }
}

/*
** WEBPAGE: sitemap-timeline
**
** Generate a list of hyperlinks to various (obscure) variations on
** the /timeline page.
*/
void sitemap_timeline_page(void){
  int isPopup = 0;         /* This is an XMLHttpRequest() for /sitemap */

  login_check_credentials();
  style_set_current_feature("sitemap");
  if( P("popup")!=0 && cgi_csrf_safe(0) ){
    /* If this is a POST from the same origin with the popup=1 parameter,
    ** then disable anti-robot defenses */
    isPopup = 1;
    g.perm.Hyperlink = 1;
    g.jsHref = 0;
  }
  if( !isPopup ){
    style_header("Timeline Examples");
    style_adunit_config(ADUNIT_RIGHT_OK);
  }
  @ <ul id="sitemap" class="columns" style="column-width:20em">
  @ <li>%z(href("%R/timeline?ymd"))Current day</a></li>
  @ <li>%z(href("%R/timeline?yw"))Current week</a></li>
  @ <li>%z(href("%R/timeline?ym"))Current month</a></li>
  @ <li>%z(href("%R/thisdayinhistory"))Today in history</a></li>
  @ <li>%z(href("%R/timeline?a=1970-01-01&y=ci&n=10"))First 10
  @     check-ins</a></li>
  @ <li>%z(href("%R/timeline?namechng"))File name changes</a></li>
  @ <li>%z(href("%R/timeline?forks"))Forks</a></li>
  @ <li>%z(href("%R/timeline?cherrypicks"))Cherrypick merges</a></li>
  @ <li>%z(href("%R/timewarps"))Timewarps</a></li>
  @ <li>%z(href("%R/timeline?ubg"))Color-coded by user</a></li>
  @ <li>%z(href("%R/timeline?deltabg"))Delta vs. baseline manifests</a></li>
  @ </ul>
  if( !isPopup ){
    style_finish_page();
  }
}
