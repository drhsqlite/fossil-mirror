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
*/
void sitemap_page(void){
  int srchFlags;
  login_check_credentials();
  srchFlags = search_restrict(SRCH_ALL);
  style_header("Site Map");
  style_adunit_config(ADUNIT_RIGHT_OK);
#if 0
  @ <p>
  @ The following links are just a few of the many web-pages available for
  @ this Fossil repository:
  @ </p>
  @
#endif
  @ <ul>
  @ <li>%z(href("%R/home"))Home Page</a>
  if( srchFlags & SRCH_DOC ){
    @   <ul>
    @   <li>%z(href("%R/docsrch"))Search Project Documentation</a></li>
    @   </ul>
  }
  @ </li>
  if( g.perm.Read ){
    @ <li>%z(href("%R/tree"))File Browser</a></li>
    @   <ul>
    @   <li>%z(href("%R/tree?type=tree&ci=trunk"))Tree-view,
    @        Trunk Check-in</a></li>
    @   <li>%z(href("%R/tree?type=flat"))Flat-view</a></li>
    @   <li>%z(href("%R/fileage?name=trunk"))File ages for Trunk</a></li>
    @ </ul>
  }
  if( g.perm.Read ){
    @ <li>%z(href("%R/timeline?n=200"))Project Timeline</a></li>
    @ <ul>
    @   <li>%z(href("%R/reports"))Activity Reports</a></li>
    @   <li>%z(href("%R/timeline?n=all&namechng"))File name changes</a></li>
    @   <li>%z(href("%R/timeline?n=all&forks"))Forks</a></li>
    @   <li>%z(href("%R/timeline?a=1970-01-01&y=ci&n=10"))First 10
    @       check-ins</a></li>
    @ </ul>
  }
  if( g.perm.Read ){
    @ <li>%z(href("%R/brlist"))Branches</a></li>
    @ <ul>
    @   <li>%z(href("%R/leaves"))Leaf Check-ins</a></li>
    @   <li>%z(href("%R/taglist"))List of Tags</a></li>
    @ </ul>
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
    @     <li>%z(href("%R/wiki_rules"))Wiki Formatting Rules</a></li>
    @     <li>%z(href("%R/md_rules"))Markdown Formatting Rules</a></li>
    @     <li>%z(href("%R/wiki?name=Sandbox"))Sandbox</a></li>
    @     <li>%z(href("%R/attachlist"))List of Attachments</a></li>
    @   </ul>
    @ </li>
  }
  if( g.perm.RdTkt ){
    @ <li>%z(href("%R/reportlist"))Tickets</a>
    @   <ul>
    if( srchFlags & SRCH_TKT ){
      @   <li>%z(href("%R/tktsrch"))Ticket Search</a></li>
    }
    @   <li>%z(href("%R/timeline?y=t"))Recent activity</a></li>
    @   <li>%z(href("%R/attachlist"))List of Attachments</a></li>
    @   </ul>
    @ </li>
  }
  if( g.perm.Read ){
    @ <li>%z(href("%R/uvlist"))Unversioned Files</a>
  }
  if( srchFlags ){
    @ <li>%z(href("%R/search"))Full-Text Search</a></li>
  }
  @ <li>%z(href("%R/login"))Login/Logout/Change Password</a></li>
  if( g.perm.Read ){
    @ <li>%z(href("%R/stat"))Repository Status</a>
    @   <ul>
    @   <li>%z(href("%R/hash-collisions"))Collisions on hash prefixes</a></li>
    if( g.perm.Admin ){
      @   <li>%z(href("%R/urllist"))List of URLs used to access
      @       this repository</a></li>
    }
    @   <li>%z(href("%R/bloblist"))List of Artifacts</a></li>
    @   <li>%z(href("%R/timewarps"))List of "Timewarp" Check-ins</a></li>
    @   </ul>
    @ </li>
  }
  @ <li>On-line Documentation
  @   <ul>
  @   <li>%z(href("%R/help"))List of All Commands and Web Pages</a></li>
  @   <li>%z(href("%R/test-all-help"))All "help" text on a single page</a></li>
  @   <li>%z(href("%R/mimetype_list"))Filename suffix to mimetype map</a></li>
  @   </ul></li>
  if( g.perm.Admin ){
    @ <li>%z(href("%R/setup"))Administration Pages</a>
    @   <ul>
    @   <li>%z(href("%R/modreq"))Pending Moderation Requests</a></li>
    @   <li>%z(href("%R/admin_log"))Admin log</a></li>
    @   <li>%z(href("%R/cachestat"))Status of the web-page cache</a></li>
    @   </ul></li>
  }
  @ <li>Test Pages
  @   <ul>
  if( g.perm.Admin || db_get_boolean("test_env_enable",0) ){
    @   <li>%z(href("%R/test_env"))CGI Environment Test</a></li>
  }
  if( g.perm.Read ){
    @   <li>%z(href("%R/test-rename-list"))List of file renames</a></li>
  }
  @   <li>%z(href("%R/hash-color-test"))Page to experiment with the automatic
  @       colors assigned to branch names</a>
  @   <li>%z(href("%R/test-captcha"))Random ASCII-art Captcha image</a></li>
  @   </ul></li>
  @ </ul></li>
  style_footer();
}
