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
      if( zVal==0 ) zVal = "";
      for(j=0; j<nField; j++){
        if( strcmp(azField[j],zName)==0 ){
          azValue[j] = mprintf("%s", zVal);
          break;
        }
      }
      if( Th_Fetch(zName, &size)==0 ){
        Th_Store(db_column_name(&q,i), zVal);
      }
    }
  }else{
    db_finalize(&q);
    db_prepare(&q, "PRAGMA table_info(ticket)");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zField = db_column_text(&q, 1);
      if( Th_Fetch(zField, &size)==0 ){
        Th_Store(zField, "");
      }
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
** Rebuild all tickets named in the _pending_ticket table.
**
** This routine is called just prior to commit after new
** out-of-sequence ticket changes have been added.
*/
static int ticket_rebuild_at_commit(void){
  Stmt q;
  db_multi_exec(
    "DELETE FROM ticket WHERE tkt_uuid IN _pending_ticket"
  );
  db_prepare(&q, "SELECT uuid FROM _pending_ticket");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    ticket_rebuild_entry(zUuid);
  }
  db_multi_exec(
    "DELETE FROM _pending_ticket"
  );
  return 0;
}

/*
** Update an entry of the TICKET table according to the information
** in the control file given in p.  Attempt to create the appropriate
** TICKET table entry if createFlag is true.  If createFlag is false,
** that means we already know the entry exists and so we can save the
** work of trying to create it.
*/
void ticket_insert(Manifest *p, int createFlag, int checkTime){
  Blob sql;
  Stmt q;
  int i;
  const char *zSep;

  getAllTicketFields();
  if( createFlag ){  
    db_multi_exec("INSERT OR IGNORE INTO ticket(tkt_uuid, tkt_mtime) "
                  "VALUES(%Q, 0)", p->zTicketUuid);
  }
  blob_zero(&sql);
  blob_appendf(&sql, "UPDATE OR REPLACE ticket SET tkt_mtime=:mtime");
  zSep = "SET";
  for(i=0; i<p->nField; i++){
    const char *zName = p->aField[i].zName;
    if( zName[0]=='+' ){
      zName++;
      if( fieldId(zName)<0 ) continue;
      blob_appendf(&sql,", %s=%s || %Q", zName, zName, p->aField[i].zValue);
    }else{
      if( fieldId(zName)<0 ) continue;
      blob_appendf(&sql,", %s=%Q", zName, p->aField[i].zValue);
    }
  }
  blob_appendf(&sql, " WHERE tkt_uuid='%s' AND tkt_mtime<:mtime",
                     p->zTicketUuid);
  db_prepare(&q, "%s", blob_str(&sql));
  db_bind_double(&q, ":mtime", p->rDate);
  db_step(&q);
  db_finalize(&q);
  if( checkTime && db_changes()==0 ){
    static int isInit = 0;
    if( !isInit ){
      db_multi_exec("CREATE TEMP TABLE _pending_ticket(uuid TEXT UNIQUE)");
      db_commit_hook(ticket_rebuild_at_commit, 1);
      isInit = 1;
    }
    db_multi_exec("INSERT OR IGNORE INTO _pending_ticket "
                  "VALUES(%Q)", p->zTicketUuid);
  }
  blob_reset(&sql);
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
    ticket_insert(&manifest, createFlag, 0);
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
  Th_Eval(g.interp, 0, (const uchar*)zConfig, -1);
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
  login_check_credentials();
  if( !g.okRdTkt ){ login_needed(); return; }
  if( g.okWrTkt ){
    style_submenu_element("Edit", "Edit The Ticket", "%s/tktedit?name=%T",
        g.zTop, PD("name",""));
  }
  style_header("View Ticket");
  ticket_init();
  initializeVariablesFromDb();
  zScript = ticket_viewpage_code();
  Th_Render(zScript);
  style_footer();
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
  const unsigned char **argv, 
  int *argl
){
  int idx;

  if( argc!=3 ){
    return Th_WrongNumArgs(interp, "append_field FIELD STRING");
  }
  for(idx=0; idx<nField; idx++){
    if( strncmp(azField[idx], (const char*)argv[1], argl[1])==0
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
** Construct and submit a new ticket artifact.
*/
static int submitTicketCmd(
  Th_Interp *interp, 
  void *pUuid, 
  int argc, 
  const unsigned char **argv, 
  int *argl
){
  char *zDate;
  const char *zUuid;
  int i;
  int rid;
  Blob tktchng, cksum;

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
        if( strncmp(zValue, azValue[i], nValue)
                || strlen(azValue[i])!=nValue ){
          blob_appendf(&tktchng, "J %s %z\n",
             azField[i], fossilize(zValue,nValue));
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

  if( strncmp(g.zPath,"debug_",6)==0 ){
    @ <hr><pre>
    @ %h(blob_str(&tktchng))
    @ </pre><hr>
    blob_zero(&tktchng);
    return TH_OK;
  }

  rid = content_put(&tktchng, 0, 0);
  if( rid==0 ){
    fossil_panic("trouble committing ticket: %s", g.zErrMsg);
  }
  manifest_crosslink(rid, &tktchng);
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
  style_header("New Ticket");
  ticket_init();
  getAllTicketFields();
  initializeVariablesFromDb();
  initializeVariablesFromCGI();
  @ <form method="POST" action="%s(g.zBaseURL)/%s(g.zPath)">
  zScript = ticket_newpage_code();
  Th_Store("login", g.zLogin);
  Th_Store("date", db_text(0, "SELECT datetime('now')"));
  Th_CreateCommand(g.interp, "submit_ticket", submitTicketCmd,
                   (void*)&zNewUuid, 0);
  if( Th_Render(zScript)==TH_RETURN && zNewUuid ){
    cgi_redirect(mprintf("%s/tktview/%s", g.zBaseURL, zNewUuid));
    return;
  }
  @ </form>
  style_footer();
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
  style_header("Edit Ticket");
  zName = P("name");
  if( zName==0 || (nName = strlen(zName))<4 || nName>UUID_SIZE
          || !validate16(zName,nName) ){
    @ <font color="red"><b>Not a valid ticket id: \"%h(zName)\"</b></font>
    style_footer();
    return;
  }
  nRec = db_int(0, "SELECT count(*) FROM ticket WHERE tkt_uuid GLOB '%q*'",
                zName);
  if( nRec==0 ){
    @ <font color="red"><b>No such ticket: \"%h(zName)\"</b></font>
    style_footer();
    return;
  }
  if( nRec>1 ){
    @ <font color="red"><b>%d(nRec) tickets begin with: \"%h(zName)\"</b></font>
    style_footer();
    return;
  }
  ticket_init();
  getAllTicketFields();
  initializeVariablesFromCGI();
  initializeVariablesFromDb();
  @ <form method="POST" action="%s(g.zBaseURL)/%s(g.zPath)">
  @ <input type="hidden" name="name" value="%s(zName)">
  zScript = ticket_editpage_code();
  Th_Store("login", g.zLogin);
  Th_Store("date", db_text(0, "SELECT datetime('now')"));
  Th_CreateCommand(g.interp, "append_field", appendRemarkCmd, 0, 0);
  Th_CreateCommand(g.interp, "submit_ticket", submitTicketCmd, (void*)&zName,0);
  if( Th_Render(zScript)==TH_RETURN && zName ){
    cgi_redirect(mprintf("%s/tktview/%s", g.zBaseURL, zName));
    return;
  }
  @ </form>
  style_footer();
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
