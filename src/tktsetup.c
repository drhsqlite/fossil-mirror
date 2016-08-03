/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code to implement the ticket configuration
** setup screens.
*/
#include "config.h"
#include "tktsetup.h"
#include <assert.h>

/*
** WEBPAGE: tktsetup
** Main sub-menu for configuring the ticketing system.
*/
void tktsetup_page(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }

  style_header("Ticket Setup");
  @ <table border="0" cellspacing="20">
  setup_menu_entry("Table", "tktsetup_tab",
    "Specify the schema of the  \"ticket\" table in the database.");
  setup_menu_entry("Timeline", "tktsetup_timeline",
    "How to display ticket status in the timeline");
  setup_menu_entry("Common", "tktsetup_com",
    "Common TH1 code run before all ticket processing.");
  setup_menu_entry("Change", "tktsetup_change",
    "The TH1 code run after a ticket is edited or created.");
  setup_menu_entry("New Ticket Page", "tktsetup_newpage",
    "HTML with embedded TH1 code for the \"new ticket\" webpage.");
  setup_menu_entry("View Ticket Page", "tktsetup_viewpage",
    "HTML with embedded TH1 code for the \"view ticket\" webpage.");
  setup_menu_entry("Edit Ticket Page", "tktsetup_editpage",
    "HTML with embedded TH1 code for the \"edit ticket\" webpage.");
  setup_menu_entry("Report List Page", "tktsetup_reportlist",
    "HTML with embedded TH1 code for the \"report list\" webpage.");
  setup_menu_entry("Report Template", "tktsetup_rpttplt",
    "The default ticket report format.");
  setup_menu_entry("Key Template", "tktsetup_keytplt",
    "The default color key for reports.");
  @ </table>
  style_footer();
}

/*
** NOTE:  When changing the table definition below, also change the
** equivalent definition found in schema.c.
*/
/* @-comment: ** */
static const char zDefaultTicketTable[] =
@ CREATE TABLE ticket(
@   -- Do not change any column that begins with tkt_
@   tkt_id INTEGER PRIMARY KEY,
@   tkt_uuid TEXT UNIQUE,
@   tkt_mtime DATE,
@   tkt_ctime DATE,
@   -- Add as many fields as required below this line
@   type TEXT,
@   status TEXT,
@   subsystem TEXT,
@   priority TEXT,
@   severity TEXT,
@   foundin TEXT,
@   private_contact TEXT,
@   resolution TEXT,
@   title TEXT,
@   comment TEXT
@ );
@ CREATE TABLE ticketchng(
@   -- Do not change any column that begins with tkt_
@   tkt_id INTEGER REFERENCES ticket,
@   tkt_rid INTEGER REFERENCES blob,
@   tkt_mtime DATE,
@   -- Add as many fields as required below this line
@   login TEXT,
@   username TEXT,
@   mimetype TEXT,
@   icomment TEXT
@ );
@ CREATE INDEX ticketchng_idx1 ON ticketchng(tkt_id, tkt_mtime);
;

/*
** Return the ticket table definition
*/
const char *ticket_table_schema(void){
  return db_get("ticket-table", zDefaultTicketTable);
}

/*
** Common implementation for the ticket setup editor pages.
*/
static void tktsetup_generic(
  const char *zTitle,           /* Page title */
  const char *zDbField,         /* Configuration field being edited */
  const char *zDfltValue,       /* Default text value */
  const char *zDesc,            /* Description of this field */
  char *(*xText)(const char*),  /* Validity test or NULL */
  void (*xRebuild)(void),       /* Run after successful update */
  int height                    /* Height of the edit box */
){
  const char *z;
  int isSubmit;

  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }
  if( PB("setup") ){
    cgi_redirect("tktsetup");
  }
  isSubmit = P("submit")!=0;
  z = P("x");
  if( z==0 ){
    z = db_get(zDbField, zDfltValue);
  }
  style_header("Edit %s", zTitle);
  if( P("clear")!=0 ){
    login_verify_csrf_secret();
    db_unset(zDbField, 0);
    if( xRebuild ) xRebuild();
    cgi_redirect("tktsetup");
  }else if( isSubmit ){
    char *zErr = 0;
    login_verify_csrf_secret();
    if( xText && (zErr = xText(z))!=0 ){
      @ <p class="tktsetupError">ERROR: %h(zErr)</p>
    }else{
      db_set(zDbField, z, 0);
      if( xRebuild ) xRebuild();
      cgi_redirect("tktsetup");
    }
  }
  @ <form action="%s(g.zTop)/%s(g.zPath)" method="post"><div>
  login_insert_csrf_secret();
  @ <p>%s(zDesc)</p>
  @ <textarea name="x" rows="%d(height)" cols="80">%h(z)</textarea>
  @ <blockquote><p>
  @ <input type="submit" name="submit" value="Apply Changes" />
  @ <input type="submit" name="clear" value="Revert To Default" />
  @ <input type="submit" name="setup" value="Cancel" />
  @ </p></blockquote>
  @ </div></form>
  @ <hr />
  @ <h2>Default %s(zTitle)</h2>
  @ <blockquote><pre>
  @ %h(zDfltValue)
  @ </pre></blockquote>
  style_footer();
}

/*
** WEBPAGE: tktsetup_tab
** Administrative page for defining the "ticket" table used
** to hold ticket information.
*/
void tktsetup_tab_page(void){
  static const char zDesc[] =
  @ Enter a valid CREATE TABLE statement for the "ticket" table.  The
  @ table must contain columns named "tkt_id", "tkt_uuid", and "tkt_mtime"
  @ with an unique index on "tkt_uuid" and "tkt_mtime".
  ;
  tktsetup_generic(
    "Ticket Table Schema",
    "ticket-table",
    zDefaultTicketTable,
    zDesc,
    ticket_schema_check,
    ticket_rebuild,
    20
  );
}

static const char zDefaultTicketCommon[] =
@ set type_choices {
@    Code_Defect
@    Build_Problem
@    Documentation
@    Feature_Request
@    Incident
@ }
@ set priority_choices {
@   Immediate
@   High
@   Medium
@   Low
@   Zero
@ }
@ set severity_choices {
@   Critical
@   Severe
@   Important
@   Minor
@   Cosmetic
@ }
@ set resolution_choices {
@   Open
@   Fixed
@   Rejected
@   Workaround
@   Unable_To_Reproduce
@   Works_As_Designed
@   External_Bug
@   Not_A_Bug
@   Duplicate
@   Overcome_By_Events
@   Drive_By_Patch
@   Misconfiguration
@ }
@ set status_choices {
@   Open
@   Verified
@   Review
@   Deferred
@   Fixed
@   Tested
@   Closed
@ }
@ set subsystem_choices {
@ }
;

/*
** Return the ticket common code.
*/
const char *ticket_common_code(void){
  return db_get("ticket-common", zDefaultTicketCommon);
}

/*
** WEBPAGE: tktsetup_com
** Administrative page used to define TH1 script that is
** common to all ticket screens.
*/
void tktsetup_com_page(void){
  static const char zDesc[] =
  @ Enter TH1 script that initializes variables prior to generating
  @ any of the ticket view, edit, or creation pages.
  ;
  tktsetup_generic(
    "Ticket Common Script",
    "ticket-common",
    zDefaultTicketCommon,
    zDesc,
    0,
    0,
    30
  );
}

static const char zDefaultTicketChange[] =
@ return
;

/*
** Return the ticket change code.
*/
const char *ticket_change_code(void){
  return db_get("ticket-change", zDefaultTicketChange);
}

/*
** WEBPAGE: tktsetup_change
** Administrative screen used to view or edit the TH1 script
** that shows ticket changes.
*/
void tktsetup_change_page(void){
  static const char zDesc[] =
  @ Enter TH1 script that runs after processing the ticket editing
  @ and creation pages.
  ;
  tktsetup_generic(
    "Ticket Change Script",
    "ticket-change",
    zDefaultTicketChange,
    zDesc,
    0,
    0,
    30
  );
}

static const char zDefaultNew[] =
@ <th1>
@   if {![info exists mutype]} {set mutype {[links only]}}
@   if {[info exists submit]} {
@      set status Open
@      if {$mutype eq "HTML"} {
@        set mimetype "text/html"
@      } elseif {$mutype eq "Wiki"} {
@        set mimetype "text/x-fossil-wiki"
@      } elseif {$mutype eq {[links only]}} {
@        set mimetype "text/x-fossil-plain"
@      } else {
@        set mimetype "text/plain"
@      }
@      submit_ticket
@      set preview 1
@   }
@ </th1>
@ <h1 style="text-align: center;">Enter A New Ticket</h1>
@ <table cellpadding="5">
@ <tr>
@ <td colspan="3">
@ Enter a one-line summary of the ticket:<br />
@ <input type="text" name="title" size="60" value="$<title>" />
@ </td>
@ </tr>
@
@ <tr>
@ <td align="right">Type:</td>
@ <td align="left"><th1>combobox type $type_choices 1</th1></td>
@ <td align="left">What type of ticket is this?</td>
@ </tr>
@
@ <tr>
@ <td align="right">Version:</td>
@ <td align="left">
@ <input type="text" name="foundin" size="20" value="$<foundin>" />
@ </td>
@ <td align="left">In what version or build number do you observe
@ the problem?</td>
@ </tr>
@
@ <tr>
@ <td align="right">Severity:</td>
@ <td align="left"><th1>combobox severity $severity_choices 1</th1></td>
@ <td align="left">How debilitating is the problem?  How badly does the problem
@ affect the operation of the product?</td>
@ </tr>
@
@ <tr>
@ <td align="right">EMail:</td>
@ <td align="left">
@ <input type="text" name="private_contact" value="$<private_contact>"
@  size="30" />
@ </td>
@ <td align="left"><u>Not publicly visible</u>
@ Used by developers to contact you with questions.</td>
@ </tr>
@
@ <tr>
@ <td colspan="3">
@ Enter a detailed description of the problem.
@ For code defects, be sure to provide details on exactly how
@ the problem can be reproduced.  Provide as much detail as
@ possible.  Format:
@ <th1>combobox mutype {Wiki HTML {Plain Text} {[links only]}} 1</th1>
@ <br />
@ <th1>set nline [linecount $comment 50 10]</th1>
@ <textarea name="icomment" cols="80" rows="$nline"
@  wrap="virtual" class="wikiedit">$<icomment></textarea><br />
@ </tr>
@
@ <th1>enable_output [info exists preview]</th1>
@ <tr><td colspan="3">
@ Description Preview:<br /><hr />
@ <th1>
@ if {$mutype eq "Wiki"} {
@   wiki $icomment
@ } elseif {$mutype eq "Plain Text"} {
@   set r [randhex]
@   wiki "<verbatim-$r>[string trimright $icomment]\n</verbatim-$r>"
@ } elseif {$mutype eq {[links only]}} {
@   set r [randhex]
@   wiki "<verbatim-$r links>[string trimright $icomment]\n</verbatim-$r>"
@ } else {
@   wiki "<nowiki>$icomment\n</nowiki>"
@ }
@ </th1>
@ <hr /></td></tr>
@ <th1>enable_output 1</th1>
@
@ <tr>
@ <td><td align="left">
@ <input type="submit" name="preview" value="Preview" />
@ </td>
@ <td align="left">See how the description will appear after formatting.</td>
@ </tr>
@
@ <th1>enable_output [info exists preview]</th1>
@ <tr>
@ <td><td align="left">
@ <input type="submit" name="submit" value="Submit" />
@ </td>
@ <td align="left">After filling in the information above, press this
@ button to create the new ticket</td>
@ </tr>
@ <th1>enable_output 1</th1>
@
@ <tr>
@ <td><td align="left">
@ <input type="submit" name="cancel" value="Cancel" />
@ </td>
@ <td>Abandon and forget this ticket</td>
@ </tr>
@ </table>
;

/*
** Return the code used to generate the new ticket page
*/
const char *ticket_newpage_code(void){
  return db_get("ticket-newpage", zDefaultNew);
}

/*
** WEBPAGE: tktsetup_newpage
** Administrative page used to view or edit the TH1 script used
** to enter new tickets.
*/
void tktsetup_newpage_page(void){
  static const char zDesc[] =
  @ Enter HTML with embedded TH1 script that will render the "new ticket"
  @ page
  ;
  tktsetup_generic(
    "HTML For New Tickets",
    "ticket-newpage",
    zDefaultNew,
    zDesc,
    0,
    0,
    40
  );
}

static const char zDefaultView[] =
@ <table cellpadding="5">
@ <tr><td class="tktDspLabel">Ticket&nbsp;UUID:</td>
@ <th1>
@ if {[info exists tkt_uuid]} {
@   if {[hascap s]} {
@     html "<td class='tktDspValue' colspan='3'>$tkt_uuid "
@     html "($tkt_id)</td></tr>\n"
@   } else {
@     html "<td class='tktDspValue' colspan='3'>$tkt_uuid</td></tr>\n"
@   }
@ } else {
@   if {[hascap s]} {
@     html "<td class='tktDspValue' colspan='3'>Deleted "
@     html "(0)</td></tr>\n"
@   } else {
@     html "<td class='tktDspValue' colspan='3'>Deleted</td></tr>\n"
@   }
@ }
@ </th1>
@ <tr><td class="tktDspLabel">Title:</td>
@ <td class="tktDspValue" colspan="3">
@ $<title>
@ </td></tr>
@ <tr><td class="tktDspLabel">Status:</td><td class="tktDspValue">
@ $<status>
@ </td>
@ <td class="tktDspLabel">Type:</td><td class="tktDspValue">
@ $<type>
@ </td></tr>
@ <tr><td class="tktDspLabel">Severity:</td><td class="tktDspValue">
@ $<severity>
@ </td>
@ <td class="tktDspLabel">Priority:</td><td class="tktDspValue">
@ $<priority>
@ </td></tr>
@ <tr><td class="tktDspLabel">Subsystem:</td><td class="tktDspValue">
@ $<subsystem>
@ </td>
@ <td class="tktDspLabel">Resolution:</td><td class="tktDspValue">
@ $<resolution>
@ </td></tr>
@ <tr><td class="tktDspLabel">Last&nbsp;Modified:</td><td class="tktDspValue">
@ <th1>
@ if {[info exists tkt_datetime]} {
@   html $tkt_datetime
@ }
@ </th1>
@ </td>
@ <th1>enable_output [hascap e]</th1>
@   <td class="tktDspLabel">Contact:</td><td class="tktDspValue">
@   $<private_contact>
@   </td>
@ <th1>enable_output 1</th1>
@ </tr>
@ <tr><td class="tktDspLabel">Version&nbsp;Found&nbsp;In:</td>
@ <td colspan="3" valign="top" class="tktDspValue">
@ $<foundin>
@ </td></tr>
@
@ <th1>
@ if {[info exists comment]} {
@   if {[string length $comment]>10} {
@     html {
@       <tr><td class="tktDspLabel">Description:</td></tr>
@       <tr><td colspan="5" class="tktDspValue">
@     }
@     if {[info exists plaintext]} {
@       set r [randhex]
@       wiki "<verbatim-$r links>\n$comment\n</verbatim-$r>"
@     } else {
@       wiki $comment
@     }
@   }
@ }
@ set seenRow 0
@ set alwaysPlaintext [info exists plaintext]
@ query {SELECT datetime(tkt_mtime) AS xdate, login AS xlogin,
@               mimetype as xmimetype, icomment AS xcomment,
@               username AS xusername
@          FROM ticketchng
@         WHERE tkt_id=$tkt_id AND length(icomment)>0} {
@   if {$seenRow} {
@     html "<hr />\n"
@   } else {
@     html "<tr><td class='tktDspLabel'>User Comments:</td></tr>\n"
@     html "<tr><td colspan='5' class='tktDspValue'>\n"
@     set seenRow 1
@   }
@   html "[htmlize $xlogin]"
@   if {$xlogin ne $xusername && [string length $xusername]>0} {
@     html " (claiming to be [htmlize $xusername])"
@   }
@   html " added on $xdate:\n"
@   if {$alwaysPlaintext || $xmimetype eq "text/plain"} {
@     set r [randhex]
@     if {$xmimetype ne "text/plain"} {html "([htmlize $xmimetype])\n"}
@     wiki "<verbatim-$r>[string trimright $xcomment]</verbatim-$r>\n"
@   } elseif {$xmimetype eq "text/x-fossil-wiki"} {
@     wiki "<p>\n[string trimright $xcomment]\n</p>\n"
@   } elseif {$xmimetype eq "text/html"} {
@     wiki "<p><nowiki>\n[string trimright $xcomment]\n</nowiki>\n"
@   } else {
@     set r [randhex]
@     wiki "<verbatim-$r links>[string trimright $xcomment]</verbatim-$r>\n"
@   }
@ }
@ if {$seenRow} {html "</td></tr>\n"}
@ </th1>
@ </table>
;


/*
** Return the code used to generate the view ticket page
*/
const char *ticket_viewpage_code(void){
  return db_get("ticket-viewpage", zDefaultView);
}

/*
** WEBPAGE: tktsetup_viewpage
** Administrative page used to view or edit the TH1 script that
** displays individual tickets.
*/
void tktsetup_viewpage_page(void){
  static const char zDesc[] =
  @ Enter HTML with embedded TH1 script that will render the "view ticket" page
  ;
  tktsetup_generic(
    "HTML For Viewing Tickets",
    "ticket-viewpage",
    zDefaultView,
    zDesc,
    0,
    0,
    40
  );
}

static const char zDefaultEdit[] =
@ <th1>
@   if {![info exists mutype]} {set mutype {[links only]}}
@   if {![info exists icomment]} {set icomment {}}
@   if {![info exists username]} {set username $login}
@   if {[info exists submit]} {
@     if {$mutype eq "Wiki"} {
@       set mimetype text/x-fossil-wiki
@     } elseif {$mutype eq "HTML"} {
@       set mimetype text/html
@     } elseif {$mutype eq {[links only]}} {
@       set mimetype text/x-fossil-plain
@     } else {
@       set mimetype text/plain
@     }
@     submit_ticket
@     set preview 1
@   }
@ </th1>
@ <table cellpadding="5">
@ <tr><td class="tktDspLabel">Title:</td><td>
@ <input type="text" name="title" value="$<title>" size="60" />
@ </td></tr>
@
@ <tr><td class="tktDspLabel">Status:</td><td>
@ <th1>combobox status $status_choices 1</th1>
@ </td></tr>
@
@ <tr><td class="tktDspLabel">Type:</td><td>
@ <th1>combobox type $type_choices 1</th1>
@ </td></tr>
@
@ <tr><td class="tktDspLabel">Severity:</td><td>
@ <th1>combobox severity $severity_choices 1</th1>
@ </td></tr>
@
@ <tr><td class="tktDspLabel">Priority:</td><td>
@ <th1>combobox priority $priority_choices 1</th1>
@ </td></tr>
@
@ <tr><td class="tktDspLabel">Resolution:</td><td>
@ <th1>combobox resolution $resolution_choices 1</th1>
@ </td></tr>
@
@ <tr><td class="tktDspLabel">Subsystem:</td><td>
@ <th1>combobox subsystem $subsystem_choices 1</th1>
@ </td></tr>
@
@ <th1>enable_output [hascap e]</th1>
@   <tr><td class="tktDspLabel">Contact:</td><td>
@   <input type="text" name="private_contact" size="40"
@    value="$<private_contact>" />
@   </td></tr>
@ <th1>enable_output 1</th1>
@
@ <tr><td class="tktDspLabel">Version&nbsp;Found&nbsp;In:</td><td>
@ <input type="text" name="foundin" size="50" value="$<foundin>" />
@ </td></tr>
@
@ <tr><td colspan="2">
@   Append Remark with format
@   <th1>combobox mutype {Wiki HTML {Plain Text} {[links only]}} 1</th1>
@   from
@   <input type="text" name="username" value="$<username>" size="30" />:<br />
@   <textarea name="icomment" cols="80" rows="15"
@    wrap="virtual" class="wikiedit">$<icomment></textarea>
@ </td></tr>
@
@ <th1>enable_output [info exists preview]</th1>
@ <tr><td colspan="2">
@ Description Preview:<br /><hr />
@ <th1>
@ if {$mutype eq "Wiki"} {
@   wiki $icomment
@ } elseif {$mutype eq "Plain Text"} {
@   set r [randhex]
@   wiki "<verbatim-$r>\n[string trimright $icomment]\n</verbatim-$r>"
@ } elseif {$mutype eq {[links only]}} {
@   set r [randhex]
@   wiki "<verbatim-$r links>\n[string trimright $icomment]</verbatim-$r>"
@ } else {
@   wiki "<nowiki>\n[string trimright $icomment]\n</nowiki>"
@ }
@ </th1>
@ <hr />
@ </td></tr>
@ <th1>enable_output 1</th1>
@
@ <tr>
@ <td align="right">
@ <input type="submit" name="preview" value="Preview" />
@ </td>
@ <td align="left">See how the description will appear after formatting.</td>
@ </tr>
@
@ <th1>enable_output [info exists preview]</th1>
@ <tr>
@ <td align="right">
@ <input type="submit" name="submit" value="Submit" />
@ </td>
@ <td align="left">Apply the changes shown above</td>
@ </tr>
@ <th1>enable_output 1</th1>
@
@ <tr>
@ <td align="right">
@ <input type="submit" name="cancel" value="Cancel" />
@ </td>
@ <td>Abandon this edit</td>
@ </tr>
@
@ </table>
;

/*
** Return the code used to generate the edit ticket page
*/
const char *ticket_editpage_code(void){
  return db_get("ticket-editpage", zDefaultEdit);
}

/*
** WEBPAGE: tktsetup_editpage
** Administrative page for viewing or editing the TH1 script that
** drives the ticket editing page.
*/
void tktsetup_editpage_page(void){
  static const char zDesc[] =
  @ Enter HTML with embedded TH1 script that will render the "edit ticket" page
  ;
  tktsetup_generic(
    "HTML For Editing Tickets",
    "ticket-editpage",
    zDefaultEdit,
    zDesc,
    0,
    0,
    40
  );
}

/*
** The default report list page
*/
static const char zDefaultReportList[] =
@ <th1>
@ if {[anoncap n]} {
@   html "<p>Enter a new ticket:</p>"
@   html "<ul><li><a href='tktnew'>New ticket</a></li></ul>"
@ }
@ </th1>
@
@ <p>Choose a report format from the following list:</p>
@ <ol>
@ <th1>html $report_items</th1>
@ </ol>
@
@ <th1>
@ if {[anoncap t q]} {
@   html "<p>Other options:</p>\n<ul>\n"
@   if {[anoncap t]} {
@     html "<li><a href='rptnew'>New report format</a></li>\n"
@   }
@   if {[anoncap q]} {
@     html "<li><a href='modreq'>Tend to pending moderation requests</a></li>\n"
@   }
@ }
@ </th1>
;

/*
** Return the code used to generate the report list
*/
const char *ticket_reportlist_code(void){
  return db_get("ticket-reportlist", zDefaultReportList);
}

/*
** WEBPAGE: tktsetup_reportlist
** Administrative page used to view or edit the TH1 script that
** defines the "report list" page.
*/
void tktsetup_reportlist(void){
  static const char zDesc[] =
  @ Enter HTML with embedded TH1 script that will render the "report list" page
  ;
  tktsetup_generic(
    "HTML For Report List",
    "ticket-reportlist",
    zDefaultReportList,
    zDesc,
    0,
    0,
    40
  );
}

/*
** The default template ticket report format:
*/
static char zDefaultReport[] =
@ SELECT
@   CASE WHEN status IN ('Open','Verified') THEN '#f2dcdc'
@        WHEN status='Review' THEN '#e8e8e8'
@        WHEN status='Fixed' THEN '#cfe8bd'
@        WHEN status='Tested' THEN '#bde5d6'
@        WHEN status='Deferred' THEN '#cacae5'
@        ELSE '#c8c8c8' END AS 'bgcolor',
@   substr(tkt_uuid,1,10) AS '#',
@   datetime(tkt_mtime) AS 'mtime',
@   type,
@   status,
@   subsystem,
@   title,
@   comment AS '_comments'
@ FROM ticket
;


/*
** Return the template ticket report format:
*/
char *ticket_report_template(void){
  return db_get("ticket-report-template", zDefaultReport);
}

/*
** WEBPAGE: tktsetup_rpttplt
**
** Administrative page used to view or edit the ticket report
** template.
*/
void tktsetup_rpttplt_page(void){
  static const char zDesc[] =
  @ Enter the default ticket report format template.  This is the
  @ template report format that initially appears when creating a
  @ new ticket summary report.
  ;
  tktsetup_generic(
    "Default Report Template",
    "ticket-report-template",
    zDefaultReport,
    zDesc,
    0,
    0,
    20
  );
}

/*
** The default template ticket key:
*/
static const char zDefaultKey[] =
@ #ffffff Key:
@ #f2dcdc Active
@ #e8e8e8 Review
@ #cfe8bd Fixed
@ #bde5d6 Tested
@ #cacae5 Deferred
@ #c8c8c8 Closed
;


/*
** Return the template ticket report format:
*/
const char *ticket_key_template(void){
  return db_get("ticket-key-template", zDefaultKey);
}

/*
** WEBPAGE: tktsetup_keytplt
**
** Administrative page used to view or edit the Key template
** for tickets.
*/
void tktsetup_keytplt_page(void){
  static const char zDesc[] =
  @ Enter the default ticket report color-key template.  This is the
  @ the color-key that initially appears when creating a
  @ new ticket summary report.
  ;
  tktsetup_generic(
    "Default Report Color-Key Template",
    "ticket-key-template",
    zDefaultKey,
    zDesc,
    0,
    0,
    10
  );
}

/*
** WEBPAGE: tktsetup_timeline
**
** Administrative page used ot configure how tickets are
** rendered on timeline views.
*/
void tktsetup_timeline_page(void){
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }

  if( P("setup") ){
    cgi_redirect("tktsetup");
  }
  style_header("Ticket Display On Timelines");
  db_begin_transaction();
  @ <form action="%s(g.zTop)/tktsetup_timeline" method="post"><div>
  login_insert_csrf_secret();

  @ <hr />
  entry_attribute("Ticket Title", 40, "ticket-title-expr", "t",
                  "title", 0);
  @ <p>An SQL expression in a query against the TICKET table that will
  @ return the title of the ticket for display purposes.</p>

  @ <hr />
  entry_attribute("Ticket Status", 40, "ticket-status-column", "s",
                  "status", 0);
  @ <p>The name of the column in the TICKET table that contains the ticket
  @ status in human-readable form.  Case sensitive.</p>

  @ <hr />
  entry_attribute("Ticket Closed", 40, "ticket-closed-expr", "c",
                  "status='Closed'", 0);
  @ <p>An SQL expression that evaluates to true in a TICKET table query if
  @ the ticket is closed.</p>

  @ <hr />
  @ <p>
  @ <input type="submit"  name="submit" value="Apply Changes" />
  @ <input type="submit" name="setup" value="Cancel" />
  @ </p>
  @ </div></form>
  db_end_transaction(0);
  style_footer();

}
