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
** This file contains code to implement the ticket configuration
** setup screens.
*/
#include "config.h"
#include "tktsetup.h"
#include <assert.h>

/*
** Main sub-menu for configuring the ticketing system.
** WEBPAGE: /tktsetup
*/
void tktsetup_page(void){
  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }

  style_header("Ticket Setup");
  @ <dl id="setup">
  setup_menu_entry("Load", "tktsetup_load",
    "Load a predefined ticket configuration");
  setup_menu_entry("Save", "tktsetup_save",
    "Save the current ticket configuration as an artifact");
  setup_menu_entry("Fields", "tktsetup_fields",
    "View or edit the fields allowed in tickets");
  setup_menu_entry("New", "tktsetup_template?type=new",
    "View or edit the template page used for creating a new ticket");
  setup_menu_entry("View", "tktsetup_template?type=view",
    "View or edit the template page used for viewing a ticket");
  setup_menu_entry("Edit", "tktsetup_template?type=edit",
    "View or edit the template page used for editing a ticket");
  @ </dl>

  style_footer();
}

/*
** Load the ticket configuration in the artifact with rid.
** If an error occurs, return 1 and leave an error message.
*/
static int load_config(int rid, Blob *pErr){
  Blob content;
  int rc;
  if( content_get(rid, &content)==0 ){
    blob_appendf(pErr, "no such artifact: %d", rid);
    return 1;
  }
  rc = ticket_config_parse(&content, 0, pErr);
  blob_reset(&content);
  return rc;
}

/*
** WEBPAGE: /tktsetup_load
*/
void tktsetup_load_page(void){
  int loadrid;
  Blob err;
  Stmt s;

  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }
  if( P("dflt")!=0 ){
    ticket_load_default_config();
    cgi_redirect("tktsetup");
  }
  loadrid = atoi(PD("id","0"));
  blob_zero(&err);
  if( loadrid ){
    if( load_config(loadrid, &err) ){
      style_header("Configuration Error");
      @ <p>The following error occurred while trying to load
      @ the configuration in artifact #%d(loadrid):</p>
      @ <blockquote><b>%h(blob_str(&err))</b></blockquote>
      @ <p>Return to the <a href="tktsetup">ticket setup menu</a>.</p>
      style_footer();
    }else{
      cgi_redirect("tktsetup");
    }
    return;
  }
  style_header("Load Configuration");
  @ <p>Select one of the following ticket configurations to load:</p>
  @ <ul>
  @ <li><p>[<a href="tktsetup_load?dflt=1">default</a>]
  @        The default built-in ticket configuration.</p></li>
  db_prepare(&s,
    "SELECT blob.uuid, tagxref.rid, datetime(tagxref.mtime, 'localtime')"
    "  FROM tagxref, blob"
    " WHERE tagxref.tagid=(SELECT tagid FROM tag "
                           "WHERE tagname='ticket_configuration')"
    "   AND blob.rid=tagxref.rid"
    " ORDER BY tagxref.mtime DESC"
  );
  while( db_step(&s)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&s, 0);
    int rid = db_column_int(&s, 1);
    const char *zWhen = db_column_text(&s, 2);
    @ <li><p>[<a href="tktsetup_load?id=%d(rid)">%s(zUuid)</a>].
    @        Configuration created on %s(zWhen).</p></li>
  }
  db_finalize(&s);
  @ <li><p>[<a href="tktsetup">Cancel</a>].  Return to the main
  @        ticket configuration setup menu.</p></li>
  @ </ul>
  style_footer();
}
