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
** This file contains code used render and control ticket entry
** and display pages.
*/
#include "config.h"
#include "tkt.h"
#include <assert.h>

/*
** The list of database user-defined fields in the TICKET table.
** The real table also contains some addition fields for internal
** used.  The internal-use fields begin with "tkt_".
*/
static int nField = 0;
static char **azField = 0;    /* Names of database fields */
static char **azValue = 0;    /* Original values */
static char **azAppend = 0;   /* Value to be appended */

/*
** Compare two entries in azField for sorting purposes
*/
static int nameCmpr(const void *a, const void *b){
  return strcmp(*(char**)a, *(char**)b);
}

/*
** Obtain a list of all fields of the TICKET table.  Put them 
** in sorted order in azField[].
**
** Also allocate space for azValue[] and azAppend[] and initialize
** all the values there to zero.
*/
static void getAllTicketFields(void){
  Stmt q;
  int i;
  if( nField>0 ) return;
  db_prepare(&q, "PRAGMA table_info(ticket)");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zField = db_column_text(&q, 1);
    if( strncmp(zField,"tkt_",4)==0 ) continue;
    if( nField%10==0 ){
      azField = realloc(azField, sizeof(azField)*3*(nField+10) );
      if( azField==0 ){
        fossil_fatal("out of memory");
      }
    }
    azField[nField] = mprintf("%s", zField);
    nField++;
  }
  db_finalize(&q);
  qsort(azField, nField, sizeof(azField[0]), nameCmpr);
  azAppend = &azField[nField];
  memset(azAppend, 0, sizeof(azAppend[0])*nField);
  azValue = &azAppend[nField];
  for(i=0; i<nField; i++){
    azValue[i] = "";
  }
}

/*
** Return the index into azField[] of the given field name.
** Return -1 if zField is not in azField[].
*/
static int fieldId(const char *zField){
  int i;
  for(i=0; i<nField; i++){
    if( strcmp(azField[i], zField)==0 ) return i;
  }
  return -1;
}

/*
** Query the database for all TICKET fields for the specific
** ticket whose name is given by the "name" CGI parameter.
** Load the values for all fields into the interpreter.
**
** Only load those fields which do not already exist as
** variables.
**
** Fields of the TICKET table that begin with "private_" are
** expanded using the db_reveal() function.  If g.okRdAddr is
** true, then the db_reveal() function will decode the content
** using the CONCEALED table so that the content legable.
** Otherwise, db_reveal() is a no-op and the content remains
** obscured.
*/
static void initializeVariablesFromDb(void){
  const char *zName;
  Stmt q;
  int i, n, size, j;

  zName = PD("name","-none-");
  db_prepare(&q, "SELECT datetime(tkt_mtime) AS tkt_datetime, *"
                 "  FROM ticket WHERE tkt_uuid GLOB '%q*'", zName);
  if( db_step(&q)==SQLITE_ROW ){
    n = db_column_count(&q);
    for(i=0; i<n; i++){
      const char *zVal = db_column_text(&q, i);
      const char *zName = db_column_name(&q, i);
      char *zRevealed = 0;
      if( zVal==0 ){
        zVal = "";
      }else if( strncmp(zName, "private_", 8)==0 ){
        zVal = zRevealed = db_reveal(zVal);
      }
      for(j=0; j<nField; j++){
        if( strcmp(azField[j],zName)==0 ){
          azValue[j] = mprintf("%s", zVal);
          break;
        }
      }
      if( Th_Fetch(zName, &size)==0 ){
        Th_Store(zName, zVal);
      }
      free(zRevealed);
    }
  }else{
    db_finalize(&q);
    db_prepare(&q, "PRAGMA table_info(ticket)");
    if( Th_Fetch("tkt_uuid",&size)==0 ){
      Th_Store("tkt_uuid",zName);
    }
    while( db_step(&q)==SQLITE_ROW ){
      const char *zField = db_column_text(&q, 1);
      if( Th_Fetch(zField, &size)==0 ){
        Th_Store(zField, "");
      }
    }
    if( Th_Fetch("tkt_datetime",&size)==0 ){
      Th_Store("tkt_datetime","");
    }
  }
  db_finalize(&q);
}

/*
** Transfer all CGI parameters to variables in the interpreter.
*/
static void initializeVariablesFromCGI(void){
  int i;
  const char *z;

  for(i=0; (z = cgi_parameter_name(i))!=0; i++){
    Th_Store(z, P(z));
  }
}

/*
** Update an entry of the TICKET table according to the information
** in the control file given in p.  Attempt to create the appropriate
** TICKET table entry if createFlag is true.  If createFlag is false,
** that means we already know the entry exists and so we can save the
** work of trying to create it.
**
** Return TRUE if a new TICKET entry was created and FALSE if an
** existing entry was revised.
*/
int ticket_insert(const Manifest *p, int createFlag, int rid){
  Blob sql;
  Stmt q;
  int i;
  const char *zSep;
  int rc = 0;

  getAllTicketFields();
  if( createFlag ){  
    db_multi_exec("INSERT OR IGNORE INTO ticket(tkt_uuid, tkt_mtime) "
                  "VALUES(%Q, 0)", p->zTicketUuid);
    rc = db_changes();
  }
  blob_zero(&sql);
  blob_appendf(&sql, "UPDATE OR REPLACE ticket SET tkt_mtime=:mtime");
  zSep = "SET";
  for(i=0; i<p->nField; i++){
    const char *zName = p->aField[i].zName;
    if( zName[0]=='+' ){
      zName++;
      if( fieldId(zName)<0 ) continue;
      blob_appendf(&sql,", %s=coalesce(%s,'') || %Q",
                   zName, zName, p->aField[i].zValue);
    }else{
      if( fieldId(zName)<0 ) continue;
      blob_appendf(&sql,", %s=%Q", zName, p->aField[i].zValue);
    }
    if( rid>0 ){
      wiki_extract_links(p->aField[i].zValue, rid, 1, p->rDate, i==0, 0);
    }
  }
  blob_appendf(&sql, " WHERE tkt_uuid='%s' AND tkt_mtime<:mtime",
                     p->zTicketUuid);
  db_prepare(&q, "%s", blob_str(&sql));
  db_bind_double(&q, ":mtime", p->rDate);
  db_step(&q);
  db_finalize(&q);
  blob_reset(&sql);
  return rc;
}

/*
** Rebuild an entire entry in the TICKET table
*/
void ticket_rebuild_entry(const char *zTktUuid){
  char *zTag = mprintf("tkt-%s", zTktUuid);
  int tagid = tag_findid(zTag, 1);
  Stmt q;
  Manifest manifest;
  Blob content;
  int createFlag = 1;
  
  db_multi_exec(
     "DELETE FROM ticket WHERE tkt_uuid=%Q", zTktUuid
  );
  db_prepare(&q, "SELECT rid FROM tagxref WHERE tagid=%d ORDER BY mtime",tagid);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    content_get(rid, &content);
    manifest_parse(&manifest, &content);
    ticket_insert(&manifest, createFlag, rid);
    manifest_ticket_event(rid, &manifest, createFlag, tagid);
    manifest_clear(&manifest);
    createFlag = 0;
  }
  db_finalize(&q);
}

/*
** Create the subscript interpreter and load the "common" code.
*/
void ticket_init(void){
  const char *zConfig;
  Th_FossilInit();
  zConfig = ticket_common_code();
  Th_Eval(g.interp, 0, zConfig, -1);
}

/*
** Recreate the ticket table.
*/
void ticket_create_table(int separateConnection){
  const char *zSql;

  db_multi_exec("DROP TABLE IF EXISTS ticket;");
  zSql = ticket_table_schema();
  if( separateConnection ){
    db_init_database(g.zRepositoryName, zSql, 0);
  }else{
    db_multi_exec("%s", zSql);
  }
}

/*
** Repopulate the ticket table
*/
void ticket_rebuild(void){
  Stmt q;
  ticket_create_table(1);
  db_begin_transaction();
  db_prepare(&q,"SELECT tagname FROM tag WHERE tagname GLOB 'tkt-*'");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    int len;
    zName += 4;
    len = strlen(zName);
    if( len<20 || !validate16(zName, len) ) continue;
    ticket_rebuild_entry(zName);
  }
  db_finalize(&q);
  db_end_transaction(0);
}

/*
** WEBPAGE: tktview
** URL:  tktview?name=UUID
**
** View a ticket.
*/
void tktview_page(void){
  const char *zScript;
  char *zFullName;
  const char *zUuid = PD("name","");

  login_check_credentials();
  if( !g.okRdTkt ){ login_needed(); return; }
  if( g.okWrTkt || g.okApndTkt ){
    style_submenu_element("Edit", "Edit The Ticket", "%s/tktedit?name=%T",
        g.zTop, PD("name",""));
  }
  if( g.okHistory ){
    style_submenu_element("History", "History Of This Ticket", 
        "%s/tkthistory/%T", g.zTop, zUuid);
    style_submenu_element("Timeline", "Timeline Of This Ticket", 
        "%s/tkttimeline/%T", g.zTop, zUuid);
    style_submenu_element("Check-ins", "Check-ins Of This Ticket", 
        "%s/tkttimeline/%T?y=ci", g.zTop, zUuid);
  }
  if( g.okNewTkt ){
    style_submenu_element("New Ticket", "Create a new ticket",
        "%s/tktnew", g.zTop);
  }
  if( g.okApndTkt && g.okAttach ){
    style_submenu_element("Attach", "Add An Attachment",
        "%s/attachadd?tkt=%T&amp;from=%s/tktview/%t",
        g.zTop, zUuid, g.zTop, zUuid);
  }
  style_header("View Ticket");
  if( g.thTrace ) Th_Trace("BEGIN_TKTVIEW<br />\n", -1);
  ticket_init();
  initializeVariablesFromDb();
  zScript = ticket_viewpage_code();
  if( g.thTrace ) Th_Trace("BEGIN_TKTVIEW_SCRIPT<br />\n", -1);
  Th_Render(zScript);
  if( g.thTrace ) Th_Trace("END_TKTVIEW<br />\n", -1);

  zFullName = db_text(0, 
       "SELECT tkt_uuid FROM ticket"
       " WHERE tkt_uuid GLOB '%q*'", zUuid);
  if( zFullName ){
    int cnt = 0;
    Stmt q;
    db_prepare(&q,
       "SELECT datetime(mtime,'localtime'), filename, user"
       "  FROM attachment"
       " WHERE isLatest AND src!='' AND target=%Q"
       " ORDER BY mtime DESC",
       zFullName);
    while( db_step(&q)==SQLITE_ROW ){
      const char *zDate = db_column_text(&q, 0);
      const char *zFile = db_column_text(&q, 1);
      const char *zUser = db_column_text(&q, 2);
      if( cnt==0 ){
        @ <hr /><h2>Attachments:</h2>
        @ <ul>
      }
      cnt++;
      @ <li>
      if( g.okRead && g.okHistory ){
        @ <a href="%s(g.zTop)/attachview?tkt=%s(zFullName)&amp;file=%t(zFile)">
        @ %h(zFile)</a>
      }else{
        @ %h(zFile)
      }
      @ added by %h(zUser) on
      hyperlink_to_date(zDate, ".");
      if( g.okWrTkt && g.okAttach ){
        @ [<a href="%s(g.zTop)/attachdelete?tkt=%s(zFullName)&amp;file=%t(zFile)&amp;from=%s(g.zTop)/tktview%%3fname=%s(zFullName)">delete</a>]
      }
      @ </li>
    }
    if( cnt ){
      @ </ul>
    }
    db_finalize(&q);
  }
 
  style_footer_cmdref("info",0);
}

/*
** TH command:   append_field FIELD STRING
**
** FIELD is the name of a database column to which we might want
** to append text.  STRING is the text to be appended to that
** column.  The append does not actually occur until the
** submit_ticket command is run.
*/
static int appendRemarkCmd(
  Th_Interp *interp, 
  void *p, 
  int argc, 
  const char **argv, 
  int *argl
){
  int idx;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "append_field FIELD STRING");
  }
  if( g.thTrace ){
    Th_Trace("append_field %#h {%#h}<br />\n",
              argl[1], argv[1], argl[2], argv[2]);
  }
  for(idx=0; idx<nField; idx++){
    if( strncmp(azField[idx], argv[1], argl[1])==0
        && azField[idx][argl[1]]==0 ){
      break;
    }
  }
  if( idx>=nField ){
    Th_ErrorMessage(g.interp, "no such TICKET column: ", argv[1], argl[1]);
    return TH_ERROR;
  }
  azAppend[idx] = mprintf("%.*s", argl[2], argv[2]);
  return TH_OK;
}

/*
** Subscript command:   submit_ticket
**
** Construct and submit a new ticket artifact.  The fields of the artifact
** are the names of the columns in the TICKET table.  The content is
** taken from TH variables.  If the content is unchanged, the field is
** omitted from the artifact.  Fields whose names begin with "private_"
** are concealed using the db_conceal() function.
*/
static int submitTicketCmd(
  Th_Interp *interp, 
  void *pUuid, 
  int argc, 
  const char **argv, 
  int *argl
){
  char *zDate;
  const char *zUuid;
  int i;
  int rid;
  Blob tktchng, cksum;

  login_verify_csrf_secret();
  zUuid = (const char *)pUuid;
  blob_zero(&tktchng);
  zDate = db_text(0, "SELECT datetime('now')");
  zDate[10] = 'T';
  blob_appendf(&tktchng, "D %s\n", zDate);
  free(zDate);
  for(i=0; i<nField; i++){
    const char *zValue;
    int nValue;
    if( azAppend[i] ){
      blob_appendf(&tktchng, "J +%s %z\n", azField[i],
                   fossilize(azAppend[i], -1));
    }else{
      zValue = Th_Fetch(azField[i], &nValue);
      if( zValue ){
        while( nValue>0 && isspace(zValue[nValue-1]) ){ nValue--; }
        if( strncmp(zValue, azValue[i], nValue) || strlen(azValue[i])!=nValue ){
          if( strncmp(azField[i], "private_", 8)==0 ){
            zValue = db_conceal(zValue, nValue);
            blob_appendf(&tktchng, "J %s %s\n", azField[i], zValue);
          }else{
            blob_appendf(&tktchng, "J %s %#F\n", azField[i], nValue, zValue);
          }
        }
      }
    }
  }
  if( *(char**)pUuid ){
    zUuid = db_text(0, 
       "SELECT tkt_uuid FROM ticket WHERE tkt_uuid GLOB '%s*'", P("name")
    );
  }else{
    zUuid = db_text(0, "SELECT lower(hex(randomblob(20)))");
  }
  *(const char**)pUuid = zUuid;
  blob_appendf(&tktchng, "K %s\n", zUuid);
  blob_appendf(&tktchng, "U %F\n", g.zLogin ? g.zLogin : "");
  md5sum_blob(&tktchng, &cksum);
  blob_appendf(&tktchng, "Z %b\n", &cksum);
  if( g.thTrace ){
    Th_Trace("submit_ticket {\n<blockquote><pre>\n%h\n</pre></blockquote>\n"
             "}<br />\n",
       blob_str(&tktchng));
  }else{
    rid = content_put(&tktchng, 0, 0);
    if( rid==0 ){
      fossil_panic("trouble committing ticket: %s", g.zErrMsg);
    }
    manifest_crosslink_begin();
    manifest_crosslink(rid, &tktchng);
    manifest_crosslink_end();
  }
  return TH_RETURN;
}


/*
** WEBPAGE: tktnew
** WEBPAGE: debug_tktnew
**
** Enter a new ticket.  the tktnew_template script in the ticket
** configuration is used.  The /tktnew page is the official ticket
** entry page.  The /debug_tktnew page is used for debugging the
** tktnew_template in the ticket configuration.  /debug_tktnew works
** just like /tktnew except that it does not really save the new ticket
** when you press submit - it just prints the ticket artifact at the
** top of the screen.
*/
void tktnew_page(void){
  const char *zScript;
  char *zNewUuid = 0;

  login_check_credentials();
  if( !g.okNewTkt ){ login_needed(); return; }
  if( P("cancel") ){
    cgi_redirect("home");
  }
  style_header("New Ticket");
  if( g.thTrace ) Th_Trace("BEGIN_TKTNEW<br />\n", -1);
  ticket_init();
  getAllTicketFields();
  initializeVariablesFromDb();
  initializeVariablesFromCGI();
  @ <form method="post" action="%s(g.zBaseURL)/%s(g.zPath)"><p>
  login_insert_csrf_secret();
  @ </p>
  zScript = ticket_newpage_code();
  Th_Store("login", g.zLogin);
  Th_Store("date", db_text(0, "SELECT datetime('now')"));
  Th_CreateCommand(g.interp, "submit_ticket", submitTicketCmd,
                   (void*)&zNewUuid, 0);
  if( g.thTrace ) Th_Trace("BEGIN_TKTNEW_SCRIPT<br />\n", -1);
  if( Th_Render(zScript)==TH_RETURN && !g.thTrace && zNewUuid ){
    cgi_redirect(mprintf("%s/tktview/%s", g.zBaseURL, zNewUuid));
    return;
  }
  @ </form>
  if( g.thTrace ) Th_Trace("END_TKTVIEW<br />\n", -1);
  style_footer_cmdref("ticket","add");
}

/*
** WEBPAGE: tktedit
** WEBPAGE: debug_tktedit
**
** Edit a ticket.  The ticket is identified by the name CGI parameter.
** /tktedit is the official page.  The /debug_tktedit page does the same
** thing except that it does not save the ticket change record when you
** press submit - it instead prints the ticket change record at the top
** of the page.  The /debug_tktedit page is intended to be used when
** debugging ticket configurations.
*/
void tktedit_page(void){
  const char *zScript;
  int nName;
  const char *zName;
  int nRec;

  login_check_credentials();
  if( !g.okApndTkt && !g.okWrTkt ){ login_needed(); return; }
  zName = P("name");
  if( P("cancel") ){
    cgi_redirectf("tktview?name=%T", zName);
  }
  style_header("Edit Ticket");
  if( zName==0 || (nName = strlen(zName))<4 || nName>UUID_SIZE
          || !validate16(zName,nName) ){
    @ <span class="tktError">Not a valid ticket id: \"%h(zName)\"</span>
    style_footer();
    return;
  }
  nRec = db_int(0, "SELECT count(*) FROM ticket WHERE tkt_uuid GLOB '%q*'",
                zName);
  if( nRec==0 ){
    @ <span class="tktError">No such ticket: \"%h(zName)\"</span>
    style_footer();
    return;
  }
  if( nRec>1 ){
    @ <span class="tktError">%d(nRec) tickets begin with:
    @ \"%h(zName)\"</span>
    style_footer();
    return;
  }
  if( g.thTrace ) Th_Trace("BEGIN_TKTEDIT<br />\n", -1);
  ticket_init();
  getAllTicketFields();
  initializeVariablesFromCGI();
  initializeVariablesFromDb();
  @ <form method="post" action="%s(g.zBaseURL)/%s(g.zPath)"><p>
  @ <input type="hidden" name="name" value="%s(zName)" />
  login_insert_csrf_secret();
  @ </p>
  zScript = ticket_editpage_code();
  Th_Store("login", g.zLogin);
  Th_Store("date", db_text(0, "SELECT datetime('now')"));
  Th_CreateCommand(g.interp, "append_field", appendRemarkCmd, 0, 0);
  Th_CreateCommand(g.interp, "submit_ticket", submitTicketCmd, (void*)&zName,0);
  if( g.thTrace ) Th_Trace("BEGIN_TKTEDIT_SCRIPT<br />\n", -1);
  if( Th_Render(zScript)==TH_RETURN && !g.thTrace && zName ){
    cgi_redirect(mprintf("%s/tktview/%s", g.zBaseURL, zName));
    return;
  }
  @ </form>
  if( g.thTrace ) Th_Trace("BEGIN_TKTEDIT<br />\n", -1);
  style_footer_cmdref("ticket","change");
}

/*
** Check the ticket table schema in zSchema to see if it appears to
** be well-formed.  If everything is OK, return NULL.  If something is
** amiss, then return a pointer to a string (obtained from malloc) that
** describes the problem.
*/
char *ticket_schema_check(const char *zSchema){
  char *zErr = 0;
  int rc;
  sqlite3 *db;
  rc = sqlite3_open(":memory:", &db);
  if( rc==SQLITE_OK ){
    rc = sqlite3_exec(db, zSchema, 0, 0, &zErr);
    if( rc!=SQLITE_OK ){
      sqlite3_close(db);
      return zErr;
    }
    rc = sqlite3_exec(db, "SELECT tkt_id, tkt_uuid, tkt_mtime FROM ticket",
                      0, 0, 0);
    sqlite3_close(db);
    if( rc!=SQLITE_OK ){
      zErr = mprintf("schema fails to define a valid ticket table "
                     "containing all required fields");
      return zErr;
    }
  }
  return 0;
}

/*
** WEBPAGE: tkttimeline
** URL: /tkttimeline?name=TICKETUUID&amp;y=TYPE
**
** Show the change history for a single ticket in timeline format.
*/
void tkttimeline_page(void){
  Stmt q;
  char *zTitle;
  char *zSQL;
  const char *zUuid;
  char *zFullUuid;
  int tagid;
  char zGlobPattern[50];
  const char *zType;

  login_check_credentials();
  if( !g.okHistory || !g.okRdTkt ){ login_needed(); return; }
  zUuid = PD("name","");
  zType = PD("y","a");
  if( zType[0]!='c' ){
    style_submenu_element("Check-ins", "Check-ins",
       "%s/tkttimeline?name=%T&amp;y=ci", g.zTop, zUuid);
  }else{
    style_submenu_element("Timeline", "Timeline",
       "%s/tkttimeline?name=%T", g.zTop, zUuid);
  }
  style_submenu_element("History", "History",
    "%s/tkthistory/%s", g.zTop, zUuid);
  style_submenu_element("Status", "Status",
    "%s/info/%s", g.zTop, zUuid);
  if( zType[0]=='c' ){
    zTitle = mprintf("Check-Ins Associated With Ticket %h", zUuid);
  }else{
    zTitle = mprintf("Timeline Of Ticket %h", zUuid);
  }
  style_header(zTitle);
  free(zTitle);

  sqlite3_snprintf(6, zGlobPattern, "%s", zUuid);
  canonical16(zGlobPattern, strlen(zGlobPattern));
  tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'tkt-%q*'",zUuid);
  if( tagid==0 ){
    @ No such ticket: %h(zUuid)
    style_footer();
    return;
  }
  zFullUuid = db_text(0, "SELECT substr(tagname, 5) FROM tag WHERE tagid=%d",
                         tagid);
  if( zType[0]=='c' ){
    zSQL = mprintf(
         "%s AND event.objid IN "
         "   (SELECT srcid FROM backlink WHERE target GLOB '%.4s*' "
                                         "AND '%s' GLOB (target||'*')) "
         "ORDER BY mtime DESC",
         timeline_query_for_www(), zFullUuid, zFullUuid
    );
  }else{
    zSQL = mprintf(
         "%s AND event.objid IN "
         "  (SELECT rid FROM tagxref WHERE tagid=%d"
         "   UNION SELECT srcid FROM backlink"
                  " WHERE target GLOB '%.4s*'"
                  "   AND '%s' GLOB (target||'*')"
         "   UNION SELECT attachid FROM attachment"
                  " WHERE target=%Q) "
         "ORDER BY mtime DESC",
         timeline_query_for_www(), tagid, zFullUuid, zFullUuid, zFullUuid
    );
  }
  db_prepare(&q, zSQL);
  free(zSQL);
  www_print_timeline(&q, TIMELINE_ARTID, 0);
  db_finalize(&q);
  style_footer_cmdref("timeline",0);
}

/*
** WEBPAGE: tkthistory
** URL: /tkthistory?name=TICKETUUID
**
** Show the complete change history for a single ticket
*/
void tkthistory_page(void){
  Stmt q;
  char *zTitle;
  const char *zUuid;
  int tagid;

  login_check_credentials();
  if( !g.okHistory || !g.okRdTkt ){ login_needed(); return; }
  zUuid = PD("name","");
  zTitle = mprintf("History Of Ticket %h", zUuid);
  style_submenu_element("Status", "Status",
    "%s/info/%s", g.zTop, zUuid);
  style_submenu_element("Check-ins", "Check-ins",
    "%s/tkttimeline?name=%s?y=ci", g.zTop, zUuid);
  style_submenu_element("Timeline", "Timeline",
    "%s/tkttimeline?name=%s", g.zTop, zUuid);
  style_header(zTitle);
  free(zTitle);

  tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'tkt-%q*'",zUuid);
  if( tagid==0 ){
    @ No such ticket: %h(zUuid)
    style_footer();
    return;
  }
  db_prepare(&q,
    "SELECT datetime(mtime,'localtime'), objid, uuid, NULL, NULL, NULL"
    "  FROM event, blob"
    " WHERE objid IN (SELECT rid FROM tagxref WHERE tagid=%d)"
    "   AND blob.rid=event.objid"
    " UNION "
    "SELECT datetime(mtime,'localtime'), attachid, uuid, src, filename, user"
    "  FROM attachment, blob"
    " WHERE target=(SELECT substr(tagname,5) FROM tag WHERE tagid=%d)"
    "   AND blob.rid=attachid"
    " ORDER BY 1 DESC",
    tagid, tagid
  );
  while( db_step(&q)==SQLITE_ROW ){
    Blob content;
    Manifest m;
    char zShort[12];
    const char *zDate = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    const char *zChngUuid = db_column_text(&q, 2);
    const char *zFile = db_column_text(&q, 4);
    memcpy(zShort, zChngUuid, 10);
    zShort[10] = 0;
    if( zFile!=0 ){
      const char *zSrc = db_column_text(&q, 3);
      const char *zUser = db_column_text(&q, 5);
      if( zSrc==0 || zSrc[0]==0 ){
        @ 
        @ <p>Delete attachment "%h(zFile)"
      }else{
        @ 
        @ <p>Add attachment "%h(zFile)"
      }
      @ [<a href="%s(g.zTop)/artifact/%T(zChngUuid)">%s(zShort)</a>]
      @ (rid %d(rid)) by
      hyperlink_to_user(zUser,zDate," on");
      hyperlink_to_date(zDate, ".</p>");
    }else{
      content_get(rid, &content);
      if( manifest_parse(&m, &content) && m.type==CFTYPE_TICKET ){
        @
        @ <p>Ticket change
        @ [<a href="%s(g.zTop)/artifact/%T(zChngUuid)">%s(zShort)</a>]
        @ (rid %d(rid)) by
        hyperlink_to_user(m.zUser,zDate," on");
        hyperlink_to_date(zDate, ":");
        @ </p>
        ticket_output_change_artifact(&m);
      }
      manifest_clear(&m);
    }
  }
  db_finalize(&q);
  style_footer_cmdref("timeline",0);
}

/*
** Return TRUE if the given BLOB contains a newline character.
*/
static int contains_newline(Blob *p){
  const char *z = blob_str(p);
  while( *z ){
    if( *z=='\n' ) return 1;
    z++;
  }
  return 0;
}

/*
** The pTkt object is a ticket change artifact.  Output a detailed
** description of this object.
*/
void ticket_output_change_artifact(Manifest *pTkt){
  int i;
  @ <ol>
  for(i=0; i<pTkt->nField; i++){
    Blob val;
    const char *z;
    z = pTkt->aField[i].zName;
    blob_set(&val, pTkt->aField[i].zValue);
    if( z[0]=='+' ){
      @ <li>Appended to %h(&z[1]):<blockquote>
      wiki_convert(&val, 0, 0);
      @ </blockquote></li>
    }else if( blob_size(&val)<=50 && contains_newline(&val) ){
      @ <li>Change %h(z) to:<blockquote>
      wiki_convert(&val, 0, 0);
      @ </blockquote></li>
    }else{
      @ <li>Change %h(z) to "%h(blob_str(&val))"</li>
    }
    blob_reset(&val);
  }
  @ </ol>
}

/*
** COMMAND: ticket
** Usage: %fossil ticket SUBCOMMAND ...
**
** Run various subcommands to control tickets
**
**     %fossil ticket show (REPORTTITLE|REPORTNR) ?TICKETFILTER? ?options?
**
**         options can be:
**           ?-l|--limit LIMITCHAR?
**           ?-q|--quote?
**
**         Run the ticket report, identified by the report format title
**         used in the gui. The data is written as flat file on stdout,
**         using "," as separator. The seperator "," can be changed using
**         the -l or --limit option.
**         If TICKETFILTER is given on the commandline, the query is
**         limited with a new WHERE-condition.
**           example:  Report lists a column # with the uuid
**                     TICKETFILTER may be [#]='uuuuuuuuu'
**           example:  Report only lists rows with status not open
**                     TICKETFILTER: status != 'open'
**         If the option -q|--quote is used, the tickets are encoded by
**         quoting special chars(space -> \\s, tab -> \\t, newline -> \\n,
**         cr -> \\r, formfeed -> \\f, vtab -> \\v, nul -> \\0, \\ -> \\\\).
**         Otherwise, the simplified encoding as on the show report raw
**         page in the gui is used.
**
**         Instead of the report title its possible to use the report
**         number. Using the special report number 0 list all columns,
**         defined in the ticket table.
**
**     %fossil ticket list fields
**
**         list all fields, defined for ticket in the fossil repository
**
**     %fossil ticket list reports
**
**         list all ticket reports, defined in the fossil repository
**
**     %fossil ticket set TICKETUUID FIELD VALUE ?FIELD VALUE .. ? ?-q|--quote?
**     %fossil ticket change TICKETUUID FIELD VALUE ?FIELD VALUE .. ? ?-q|--quote?
**
**         change ticket identified by TICKETUUID and set the value of
**         field FIELD to VALUE. Valid field descriptions are:
**            status, type, severity, priority, resolution,
**            foundin, private_contact, resolution, title or comment
**         Field names given above are the ones, defined in a standard
**         fossil environment. If you have added, deleted columns, you
**         change the all your configured columns.
**         You can use more than one field/value pair on the commandline.
**         Using -q|--quote  enables the special character decoding as
**         in "ticket show". So it's possible, to set multiline text or
**         text with special characters.
**
**     %fossil ticket add FIELD VALUE ?FIELD VALUE .. ? ?-q|--quote?
**
**         like set, but create a new ticket with the given values.
**
** The values in set|add are not validated against the definitions
** given in "Ticket Common Script".
**
** All this stuff can also be done in the gui:
**  * Go the the <a href="reportlist">Tickets</a> page
*/
void ticket_cmd(void){
  int n;

  /* do some ints, we want to be inside a checkout */
  db_must_be_within_tree();
  db_find_and_open_repository(1);
  user_select();
  /*
  ** Check that the user exists.
  */
  if( !db_exists("SELECT 1 FROM user WHERE login=%Q", g.zLogin) ){
    fossil_fatal("no such user: %s", g.zLogin);
  }

  if( g.argc<3 ){
    usage("add|fieldlist|set|show");
  }else{
    n = strlen(g.argv[2]);
    if( n==1 && g.argv[2][0]=='s' ){
      /* set/show cannot be distinguished, so show the usage */
      usage("add|fieldlist|set|show");
    }else if( strncmp(g.argv[2],"list",n)==0 ){
      if( g.argc==3 ){
        usage("list fields|reports");
      }else{
        n = strlen(g.argv[3]);
        if( !strncmp(g.argv[3],"fields",n) ){
          /* simply show all field names */
          int i;

          /* read all available ticket fields */
          getAllTicketFields();
          for(i=0; i<nField; i++){
            printf("%s\n",azField[i]);
          }
        }else if( !strncmp(g.argv[3],"reports",n) ){
          rpt_list_reports();
        }else{
          fossil_fatal("unknown ticket list option '%s'!",g.argv[3]);
        }
      }
    }else{
      /* add a new ticket or set fields on existing tickets */
      tTktShowEncoding tktEncoding;

      tktEncoding = find_option("quote","q",0) ? tktFossilize : tktNoTab;
      
      if( strncmp(g.argv[2],"show",n)==0 ){
        if( g.argc==3 ){
          usage("show REPORTNR");
        }else{
          const char *zRep = 0;
          const char *zSep = 0;
          const char *zFilterUuid = 0;

          zSep = find_option("limit","l",1);
          zRep = g.argv[3];
          if( !strcmp(zRep,"0") ){
            zRep = 0;
          }
          if( g.argc>4 ){
            zFilterUuid = g.argv[4];
          }

          rptshow( zRep, zSep, zFilterUuid, tktEncoding );

        }
      }else{
        /* add a new ticket or update an existing ticket */
        enum { set,add,err } eCmd = err;
        int i = 0;
        int rid;
        const char *zTktUuid = 0;
        Blob tktchng, cksum;

        /* get command type (set/add) and get uuid, if needed for set */
        if( strncmp(g.argv[2],"set",n)==0 || strncmp(g.argv[2],"change",n)==0 ){
          eCmd = set;
          if( g.argc==3 ){
            usage("set TICKETUUID");
          }
          zTktUuid = db_text(0, 
            "SELECT tkt_uuid FROM ticket WHERE tkt_uuid GLOB '%s*'", g.argv[3]
          );
          if( !zTktUuid ){
            fossil_fatal("unknown ticket: '%s'!",g.argv[3]);
          }
          i=4;
        }else if( strncmp(g.argv[2],"add",n)==0 ){
          eCmd = add;
          i = 3;
          zTktUuid = db_text(0, "SELECT lower(hex(randomblob(20)))");
        }
        /* none of set/add, so show the usage! */
        if( eCmd==err ){
          usage("add|fieldlist|set|show");
        }
        
        /* read all given ticket field/value pairs from command line */
        if( i==g.argc ){
          fossil_fatal("empty %s command aborted!",g.argv[2]);
        }
        getAllTicketFields();
        /* read commandline and assign fields in the azValue array */
        while( i<g.argc ){
          char *zFName;
          char *zFValue;
          int j;

          zFName = g.argv[i++];
          if( i==g.argc ){
            fossil_fatal("missing value for '%s'!",zFName);
          }
          zFValue = g.argv[i++];
          j = fieldId(zFName);
          if( tktEncoding == tktFossilize ){
            zFValue=mprintf("%s",zFValue);
            defossilize(zFValue);
          }
          if( j == -1 ){
            fossil_fatal("unknown field name '%s'!",zFName);
          }else{
            azValue[j] = zFValue;
          }
        }

        /* now add the needed artifacts to the repository */
        blob_zero(&tktchng);
        { /* add the time to the ticket manifest */
          char *zDate;

          zDate = db_text(0, "SELECT datetime('now')");
          zDate[10] = 'T';
          blob_appendf(&tktchng, "D %s\n", zDate);
          free(zDate);
        }
        /* append defined elements */
        for(i=0; i<nField; i++){
          char *zValue;

          zValue = azValue[i];
          if( azValue[i] && azValue[i][0] ){
            if( strncmp(azField[i], "private_", 8)==0 ){
              zValue = db_conceal(zValue, strlen(zValue));
              blob_appendf(&tktchng, "J %s %s\n", azField[i], zValue);
            }else{
              blob_appendf(&tktchng, "J %s %#F\n",
                           azField[i], strlen(zValue), zValue);
            }
            if( tktEncoding == tktFossilize ){
              free(azValue[i]);
            }
          }
        }
        blob_appendf(&tktchng, "K %s\n", zTktUuid);
        blob_appendf(&tktchng, "U %F\n", g.zLogin);
        md5sum_blob(&tktchng, &cksum);
        blob_appendf(&tktchng, "Z %b\n", &cksum);
        rid = content_put(&tktchng, 0, 0);
        if( rid==0 ){
          fossil_panic("trouble committing ticket: %s", g.zErrMsg);
        }
        manifest_crosslink_begin();
        manifest_crosslink(rid, &tktchng);
        manifest_crosslink_end();
	printf("ticket %s succeeded for UID %s\n",
	       (eCmd==set?"set":"add"),zTktUuid);
      }
    }
  }
}
