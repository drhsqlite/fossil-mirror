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
  setup_menu_entry("New", "tktsetup_newtemplate",
    "View or edit the template page used for creating a new ticket");
  setup_menu_entry("View", "tktsetup_viewtemplate",
    "View or edit the template page used for viewing a ticket");
  setup_menu_entry("Edit", "tktsetup_edittemplate",
    "View or edit the template page used for editing a ticket");
  @ </dl>

  style_footer();
}

/*
** WEBPAGE: /tktsetup_load
*/
void tktsetup_load_page(void){
  int loadrid;
  int loaddflt;

  login_check_credentials();
  if( !g.okSetup ){
    login_needed();
  }
  loadrid = atoi(PD("id","0"));
  loaddflt = P("dflt")!=0;
  if( loaddflt ){
    ticket_load_default_config();
    cgi_redirect("tktsetup");
  }
}
