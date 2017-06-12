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
static struct tktFieldInfo {
  char *zName;             /* Name of the database field */
  char *zValue;            /* Value to store */
  char *zAppend;           /* Value to append */
  unsigned mUsed;          /* 01: TICKET  02: TICKETCHNG */
} *aField;
#define USEDBY_TICKET      01
#define USEDBY_TICKETCHNG  02
#define USEDBY_BOTH        03
static u8 haveTicket = 0;        /* True if the TICKET table exists */
static u8 haveTicketCTime = 0;   /* True if TICKET.TKT_CTIME exists */
static u8 haveTicketChng = 0;    /* True if the TICKETCHNG table exists */
static u8 haveTicketChngRid = 0; /* True if TICKETCHNG.TKT_RID exists */

/*
** Compare two entries in aField[] for sorting purposes
*/
static int nameCmpr(const void *a, const void *b){
  return fossil_strcmp(((const struct tktFieldInfo*)a)->zName,
                       ((const struct tktFieldInfo*)b)->zName);
}

/*
** Return the index into aField[] of the given field name.
** Return -1 if zFieldName is not in aField[].
*/
static int fieldId(const char *zFieldName){
  int i;
  for(i=0; i<nField; i++){
    if( fossil_strcmp(aField[i].zName, zFieldName)==0 ) return i;
  }
  return -1;
}

/*
** Obtain a list of all fields of the TICKET and TICKETCHNG tables.  Put them
** in sorted order in aField[].
**
** The haveTicket and haveTicketChng variables are set to 1 if the TICKET and
** TICKETCHANGE tables exist, respectively.
*/
static void getAllTicketFields(void){
  Stmt q;
  int i;
  static int once = 0;
  if( once ) return;
  once = 1;
  db_prepare(&q, "PRAGMA table_info(ticket)");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFieldName = db_column_text(&q, 1);
    haveTicket = 1;
    if( memcmp(zFieldName,"tkt_",4)==0 ){
      if( strcmp(zFieldName, "tkt_ctime")==0 ) haveTicketCTime = 1;
      continue;
    }
    if( nField%10==0 ){
      aField = fossil_realloc(aField, sizeof(aField[0])*(nField+10) );
    }
    aField[nField].zName = mprintf("%s", zFieldName);
    aField[nField].mUsed = USEDBY_TICKET;
    nField++;
  }
  db_finalize(&q);
  db_prepare(&q, "PRAGMA table_info(ticketchng)");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFieldName = db_column_text(&q, 1);
    haveTicketChng = 1;
    if( memcmp(zFieldName,"tkt_",4)==0 ){
      if( strcmp(zFieldName,"tkt_rid")==0 ) haveTicketChngRid = 1;
      continue;
    }
    if( (i = fieldId(zFieldName))>=0 ){
      aField[i].mUsed |= USEDBY_TICKETCHNG;
      continue;
    }
    if( nField%10==0 ){
      aField = fossil_realloc(aField, sizeof(aField[0])*(nField+10) );
    }
    aField[nField].zName = mprintf("%s", zFieldName);
    aField[nField].mUsed = USEDBY_TICKETCHNG;
    nField++;
  }
  db_finalize(&q);
  qsort(aField, nField, sizeof(aField[0]), nameCmpr);
  for(i=0; i<nField; i++){
    aField[i].zValue = "";
    aField[i].zAppend = 0;
  }
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
** expanded using the db_reveal() function.  If g.perm.RdAddr is
** true, then the db_reveal() function will decode the content
** using the CONCEALED table so that the content legible.
** Otherwise, db_reveal() is a no-op and the content remains
** obscured.
*/
static void initializeVariablesFromDb(void){
  const char *zName;
  Stmt q;
  int i, n, size, j;

  zName = PD("name","-none-");
  db_prepare(&q, "SELECT datetime(tkt_mtime,toLocal()) AS tkt_datetime, *"
                 "  FROM ticket WHERE tkt_uuid GLOB '%q*'",
                 zName);
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
      if( (j = fieldId(zName))>=0 ){
        aField[j].zValue = mprintf("%s", zVal);
      }else if( memcmp(zName, "tkt_", 4)==0 && Th_Fetch(zName, &size)==0 ){
        Th_Store(zName, zVal);
      }
      free(zRevealed);
    }
  }
  db_finalize(&q);
  for(i=0; i<nField; i++){
    if( Th_Fetch(aField[i].zName, &size)==0 ){
      Th_Store(aField[i].zName, aField[i].zValue);
    }
  }
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
** Update an entry of the TICKET and TICKETCHNG tables according to the
** information in the ticket artifact given in p.  Attempt to create
** the appropriate TICKET table entry if tktid is zero.  If tktid is nonzero
** then it will be the ROWID of an existing TICKET entry.
**
** Parameter rid is the recordID for the ticket artifact in the BLOB table.
**
** Return the new rowid of the TICKET table entry.
*/
static int ticket_insert(const Manifest *p, int rid, int tktid){
  Blob sql1, sql2, sql3;
  Stmt q;
  int i, j;
  char *aUsed;

  if( tktid==0 ){
    db_multi_exec("INSERT INTO ticket(tkt_uuid, tkt_mtime) "
                  "VALUES(%Q, 0)", p->zTicketUuid);
    tktid = db_last_insert_rowid();
  }
  blob_zero(&sql1);
  blob_zero(&sql2);
  blob_zero(&sql3);
  blob_append_sql(&sql1, "UPDATE OR REPLACE ticket SET tkt_mtime=:mtime");
  if( haveTicketCTime ){
    blob_append_sql(&sql1, ", tkt_ctime=coalesce(tkt_ctime,:mtime)");
  }
  aUsed = fossil_malloc( nField );
  memset(aUsed, 0, nField);
  for(i=0; i<p->nField; i++){
    const char *zName = p->aField[i].zName;
    const char *zBaseName = zName[0]=='+' ? zName+1 : zName;
    j = fieldId(zBaseName);
    if( j<0 ) continue;
    aUsed[j] = 1;
    if( aField[j].mUsed & USEDBY_TICKET ){
      const char *zUsedByName = zName;
      if( zUsedByName[0]=='+' ){
        zUsedByName++;
        blob_append_sql(&sql1,", \"%w\"=coalesce(\"%w\",'') || %Q",
                        zUsedByName, zUsedByName, p->aField[i].zValue);
      }else{
        blob_append_sql(&sql1,", \"%w\"=%Q", zUsedByName, p->aField[i].zValue);
      }
    }
    if( aField[j].mUsed & USEDBY_TICKETCHNG ){
      const char *zUsedByName = zName;
      if( zUsedByName[0]=='+' ){
        zUsedByName++;
      }
      blob_append_sql(&sql2, ",\"%w\"", zUsedByName);
      blob_append_sql(&sql3, ",%Q", p->aField[i].zValue);
    }
    if( rid>0 ){
      wiki_extract_links(p->aField[i].zValue, rid, 1, p->rDate, i==0, 0);
    }
  }
  blob_append_sql(&sql1, " WHERE tkt_id=%d", tktid);
  db_prepare(&q, "%s", blob_sql_text(&sql1));
  db_bind_double(&q, ":mtime", p->rDate);
  db_step(&q);
  db_finalize(&q);
  blob_reset(&sql1);
  if( blob_size(&sql2)>0 || haveTicketChngRid ){
    int fromTkt = 0;
    if( haveTicketChngRid ){
      blob_append(&sql2, ",tkt_rid", -1);
      blob_append_sql(&sql3, ",%d", rid);
    }
    for(i=0; i<nField; i++){
      if( aUsed[i]==0
       && (aField[i].mUsed & USEDBY_BOTH)==USEDBY_BOTH
      ){
        const char *z = aField[i].zName;
        if( z[0]=='+' ) z++;
        fromTkt = 1;
        blob_append_sql(&sql2, ",\"%w\"", z);
        blob_append_sql(&sql3, ",\"%w\"", z);
      }
    }
    if( fromTkt ){
      db_prepare(&q, "INSERT INTO ticketchng(tkt_id,tkt_mtime%s)"
                     "SELECT %d,:mtime%s FROM ticket WHERE tkt_id=%d",
                     blob_sql_text(&sql2), tktid,
                     blob_sql_text(&sql3), tktid);
    }else{
      db_prepare(&q, "INSERT INTO ticketchng(tkt_id,tkt_mtime%s)"
                     "VALUES(%d,:mtime%s)",
                     blob_sql_text(&sql2), tktid, blob_sql_text(&sql3));
    }
    db_bind_double(&q, ":mtime", p->rDate);
    db_step(&q);
    db_finalize(&q);
  }
  blob_reset(&sql2);
  blob_reset(&sql3);
  fossil_free(aUsed);
  return tktid;
}

/*
** Returns non-zero if moderation is required for ticket changes and ticket
** attachments.
*/
int ticket_need_moderation(
  int localUser /* Are we being called for a local interactive user? */
){
  /*
  ** If the FOSSIL_FORCE_TICKET_MODERATION variable is set, *ALL* changes for
  ** tickets will be required to go through moderation (even those performed
  ** by the local interactive user via the command line).  This can be useful
  ** for local (or remote) testing of the moderation subsystem and its impact
  ** on the contents and status of tickets.
  */
  if( fossil_getenv("FOSSIL_FORCE_TICKET_MODERATION")!=0 ){
    return 1;
  }
  if( localUser ){
    return 0;
  }
  return g.perm.ModTkt==0 && db_get_boolean("modreq-tkt",0)==1;
}

/*
** Rebuild an entire entry in the TICKET table
*/
void ticket_rebuild_entry(const char *zTktUuid){
  char *zTag = mprintf("tkt-%s", zTktUuid);
  int tagid = tag_findid(zTag, 1);
  Stmt q;
  Manifest *pTicket;
  int tktid;
  int createFlag = 1;

  fossil_free(zTag);
  getAllTicketFields();
  if( haveTicket==0 ) return;
  tktid = db_int(0, "SELECT tkt_id FROM ticket WHERE tkt_uuid=%Q", zTktUuid);
  search_doc_touch('t', tktid, 0);
  if( haveTicketChng ){
    db_multi_exec("DELETE FROM ticketchng WHERE tkt_id=%d;", tktid);
  }
  db_multi_exec("DELETE FROM ticket WHERE tkt_id=%d", tktid);
  tktid = 0;
  db_prepare(&q, "SELECT rid FROM tagxref WHERE tagid=%d ORDER BY mtime",tagid);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    pTicket = manifest_get(rid, CFTYPE_TICKET, 0);
    if( pTicket ){
      tktid = ticket_insert(pTicket, rid, tktid);
      manifest_ticket_event(rid, pTicket, createFlag, tagid);
      manifest_destroy(pTicket);
    }
    createFlag = 0;
  }
  db_finalize(&q);
}


/*
** Create the TH1 interpreter and load the "common" code.
*/
void ticket_init(void){
  const char *zConfig;
  Th_FossilInit(TH_INIT_DEFAULT);
  zConfig = ticket_common_code();
  Th_Eval(g.interp, 0, zConfig, -1);
}

/*
** Create the TH1 interpreter and load the "change" code.
*/
int ticket_change(const char *zUuid){
  const char *zConfig;
  Th_FossilInit(TH_INIT_DEFAULT);
  Th_Store("uuid", zUuid);
  zConfig = ticket_change_code();
  return Th_Eval(g.interp, 0, zConfig, -1);
}

/*
** Recreate the TICKET and TICKETCHNG tables.
*/
void ticket_create_table(int separateConnection){
  const char *zSql;

  db_multi_exec(
    "DROP TABLE IF EXISTS ticket;"
    "DROP TABLE IF EXISTS ticketchng;"
  );
  zSql = ticket_table_schema();
  if( separateConnection ){
    db_end_transaction(0);
    db_init_database(g.zRepositoryName, zSql, 0);
  }else{
    db_multi_exec("%s", zSql/*safe-for-%s*/);
  }
}

/*
** Repopulate the TICKET and TICKETCHNG tables from scratch using all
** available ticket artifacts.
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
** COMMAND: test-ticket-rebuild
**
** Usage: %fossil test-ticket-rebuild TICKETID|all
**
** Rebuild the TICKET and TICKETCHNG tables for the given ticket ID
** or for ALL.
*/
void test_ticket_rebuild(void){
  db_find_and_open_repository(0, 0);
  if( g.argc!=3 ) usage("TICKETID|all");
  if( fossil_strcmp(g.argv[2], "all")==0 ){
    ticket_rebuild();
  }else{
    const char *zUuid;
    zUuid = db_text(0, "SELECT substr(tagname,5) FROM tag"
                       " WHERE tagname GLOB 'tkt-%q*'", g.argv[2]);
    if( zUuid==0 ) fossil_fatal("no such ticket: %s", g.argv[2]);
    ticket_rebuild_entry(zUuid);
  }
}

/*
** For trouble-shooting purposes, render a dump of the aField[] table to
** the webpage currently under construction.
*/
static void showAllFields(void){
  int i;
  @ <div style="color:blue">
  @ <p>Database fields:</p><ul>
  for(i=0; i<nField; i++){
    @ <li>aField[%d(i)].zName = "%h(aField[i].zName)";
    @ originally = "%h(aField[i].zValue)";
    @ currently = "%h(PD(aField[i].zName,""))";
    if( aField[i].zAppend ){
      @ zAppend = "%h(aField[i].zAppend)";
    }
    @ mUsed = %d(aField[i].mUsed);
  }
  @ </ul></div>
}

/*
** WEBPAGE: tktview
** URL:  tktview?name=UUID
**
** View a ticket identified by the name= query parameter.
*/
void tktview_page(void){
  const char *zScript;
  char *zFullName;
  const char *zUuid = PD("name","");

  login_check_credentials();
  if( !g.perm.RdTkt ){ login_needed(g.anon.RdTkt); return; }
  if( g.anon.WrTkt || g.anon.ApndTkt ){
    style_submenu_element("Edit", "%s/tktedit?name=%T", g.zTop, PD("name",""));
  }
  if( g.perm.Hyperlink ){
    style_submenu_element("History", "%s/tkthistory/%T", g.zTop, zUuid);
    style_submenu_element("Timeline", "%s/tkttimeline/%T", g.zTop, zUuid);
    style_submenu_element("Check-ins", "%s/tkttimeline/%T?y=ci", g.zTop, zUuid);
  }
  if( g.anon.NewTkt ){
    style_submenu_element("New Ticket", "%s/tktnew", g.zTop);
  }
  if( g.anon.ApndTkt && g.anon.Attach ){
    style_submenu_element("Attach", "%s/attachadd?tkt=%T&from=%s/tktview/%t",
        g.zTop, zUuid, g.zTop, zUuid);
  }
  if( P("plaintext") ){
    style_submenu_element("Formatted", "%R/tktview/%s", zUuid);
  }else{
    style_submenu_element("Plaintext", "%R/tktview/%s?plaintext", zUuid);
  }
  style_header("View Ticket");
  if( g.thTrace ) Th_Trace("BEGIN_TKTVIEW<br />\n", -1);
  ticket_init();
  initializeVariablesFromCGI();
  getAllTicketFields();
  initializeVariablesFromDb();
  zScript = ticket_viewpage_code();
  if( P("showfields")!=0 ) showAllFields();
  if( g.thTrace ) Th_Trace("BEGIN_TKTVIEW_SCRIPT<br />\n", -1);
  Th_Render(zScript);
  if( g.thTrace ) Th_Trace("END_TKTVIEW<br />\n", -1);

  zFullName = db_text(0,
       "SELECT tkt_uuid FROM ticket"
       " WHERE tkt_uuid GLOB '%q*'", zUuid);
  if( zFullName ){
    attachment_list(zFullName, "<hr /><h2>Attachments:</h2><ul>");
  }

  style_footer();
}

/*
** TH1 command: append_field FIELD STRING
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
    if( memcmp(aField[idx].zName, argv[1], argl[1])==0
        && aField[idx].zName[argl[1]]==0 ){
      break;
    }
  }
  if( idx>=nField ){
    Th_ErrorMessage(g.interp, "no such TICKET column: ", argv[1], argl[1]);
    return TH_ERROR;
  }
  aField[idx].zAppend = mprintf("%.*s", argl[2], argv[2]);
  return TH_OK;
}

/*
** Write a ticket into the repository.
*/
static int ticket_put(
  Blob *pTicket,           /* The text of the ticket change record */
  const char *zTktId,      /* The ticket to which this change is applied */
  int needMod              /* True if moderation is needed */
){
  int result;
  int rid;
  manifest_crosslink_begin();
  rid = content_put_ex(pTicket, 0, 0, 0, needMod);
  if( rid==0 ){
    fossil_fatal("trouble committing ticket: %s", g.zErrMsg);
  }
  if( needMod ){
    moderation_table_create();
    db_multi_exec(
      "INSERT INTO modreq(objid, tktid) VALUES(%d,%Q)",
      rid, zTktId
    );
  }else{
    db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d);", rid);
    db_multi_exec("INSERT OR IGNORE INTO unclustered VALUES(%d);", rid);
  }
  result = (manifest_crosslink(rid, pTicket, MC_NONE)==0);
  assert( blob_is_reset(pTicket) );
  if( !result ){
    result = manifest_crosslink_end(MC_PERMIT_HOOKS);
  }else{
    manifest_crosslink_end(MC_NONE);
  }
  return result;
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
  int nJ = 0;
  Blob tktchng, cksum;
  int needMod;

  login_verify_csrf_secret();
  if( !captcha_is_correct() ){
    @ <p class="generalError">Error: Incorrect security code.</p>
    return TH_OK;
  }
  zUuid = (const char *)pUuid;
  blob_zero(&tktchng);
  zDate = date_in_standard_format("now");
  blob_appendf(&tktchng, "D %s\n", zDate);
  free(zDate);
  for(i=0; i<nField; i++){
    if( aField[i].zAppend ){
      blob_appendf(&tktchng, "J +%s %z\n", aField[i].zName,
                   fossilize(aField[i].zAppend, -1));
      ++nJ;
    }
  }
  for(i=0; i<nField; i++){
    const char *zValue;
    int nValue;
    if( aField[i].zAppend ) continue;
    zValue = Th_Fetch(aField[i].zName, &nValue);
    if( zValue ){
      while( nValue>0 && fossil_isspace(zValue[nValue-1]) ){ nValue--; }
      if( ((aField[i].mUsed & USEDBY_TICKETCHNG)!=0 && nValue>0)
       || memcmp(zValue, aField[i].zValue, nValue)!=0
       || strlen(aField[i].zValue)!=nValue
      ){
        if( memcmp(aField[i].zName, "private_", 8)==0 ){
          zValue = db_conceal(zValue, nValue);
          blob_appendf(&tktchng, "J %s %s\n", aField[i].zName, zValue);
        }else{
          blob_appendf(&tktchng, "J %s %#F\n", aField[i].zName, nValue, zValue);
        }
        nJ++;
      }
    }
  }
  if( *(char**)pUuid ){
    zUuid = db_text(0,
       "SELECT tkt_uuid FROM ticket WHERE tkt_uuid GLOB '%q*'", P("name")
    );
  }else{
    zUuid = db_text(0, "SELECT lower(hex(randomblob(20)))");
  }
  *(const char**)pUuid = zUuid;
  blob_appendf(&tktchng, "K %s\n", zUuid);
  blob_appendf(&tktchng, "U %F\n", login_name());
  md5sum_blob(&tktchng, &cksum);
  blob_appendf(&tktchng, "Z %b\n", &cksum);
  if( nJ==0 ){
    blob_reset(&tktchng);
    return TH_OK;
  }
  needMod = ticket_need_moderation(0);
  if( g.zPath[0]=='d' ){
    const char *zNeedMod = needMod ? "required" : "skipped";
    /* If called from /debug_tktnew or /debug_tktedit... */
    @ <div style="color:blue">
    @ <p>Ticket artifact that would have been submitted:</p>
    @ <blockquote><pre>%h(blob_str(&tktchng))</pre></blockquote>
    @ <blockquote><pre>Moderation would be %h(zNeedMod).</pre></blockquote>
    @ </div>
    @ <hr />
    return TH_OK;
  }else{
    if( g.thTrace ){
      Th_Trace("submit_ticket {\n<blockquote><pre>\n%h\n</pre></blockquote>\n"
               "}<br />\n",
         blob_str(&tktchng));
    }
    ticket_put(&tktchng, zUuid, needMod);
  }
  return ticket_change(zUuid);
}


/*
** WEBPAGE: tktnew
** WEBPAGE: debug_tktnew
**
** Enter a new ticket.  The tktnew_template script in the ticket
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
  if( !g.perm.NewTkt ){ login_needed(g.anon.NewTkt); return; }
  if( P("cancel") ){
    cgi_redirect("home");
  }
  style_header("New Ticket");
  ticket_standard_submenu(T_ALL_BUT(T_NEW));
  if( g.thTrace ) Th_Trace("BEGIN_TKTNEW<br />\n", -1);
  ticket_init();
  initializeVariablesFromCGI();
  getAllTicketFields();
  initializeVariablesFromDb();
  if( g.zPath[0]=='d' ) showAllFields();
  form_begin(0, "%R/%s", g.zPath);
  login_insert_csrf_secret();
  if( P("date_override") && g.perm.Setup ){
    @ <input type="hidden" name="date_override" value="%h(P("date_override"))">
  }
  zScript = ticket_newpage_code();
  Th_Store("login", login_name());
  Th_Store("date", db_text(0, "SELECT datetime('now')"));
  Th_CreateCommand(g.interp, "submit_ticket", submitTicketCmd,
                   (void*)&zNewUuid, 0);
  if( g.thTrace ) Th_Trace("BEGIN_TKTNEW_SCRIPT<br />\n", -1);
  if( Th_Render(zScript)==TH_RETURN && !g.thTrace && zNewUuid ){
    cgi_redirect(mprintf("%s/tktview/%s", g.zTop, zNewUuid));
    return;
  }
  captcha_generate(0);
  @ </form>
  if( g.thTrace ) Th_Trace("END_TKTVIEW<br />\n", -1);
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
  if( !g.perm.ApndTkt && !g.perm.WrTkt ){
    login_needed(g.anon.ApndTkt || g.anon.WrTkt);
    return;
  }
  zName = P("name");
  if( P("cancel") ){
    cgi_redirectf("tktview?name=%T", zName);
  }
  style_header("Edit Ticket");
  if( zName==0 || (nName = strlen(zName))<4 || nName>UUID_SIZE
          || !validate16(zName,nName) ){
    @ <span class="tktError">Not a valid ticket id: "%h(zName)"</span>
    style_footer();
    return;
  }
  nRec = db_int(0, "SELECT count(*) FROM ticket WHERE tkt_uuid GLOB '%q*'",
                zName);
  if( nRec==0 ){
    @ <span class="tktError">No such ticket: "%h(zName)"</span>
    style_footer();
    return;
  }
  if( nRec>1 ){
    @ <span class="tktError">%d(nRec) tickets begin with:
    @ "%h(zName)"</span>
    style_footer();
    return;
  }
  if( g.thTrace ) Th_Trace("BEGIN_TKTEDIT<br />\n", -1);
  ticket_init();
  getAllTicketFields();
  initializeVariablesFromCGI();
  initializeVariablesFromDb();
  if( g.zPath[0]=='d' ) showAllFields();
  form_begin(0, "%R/%s", g.zPath);
  @ <input type="hidden" name="name" value="%s(zName)" />
  login_insert_csrf_secret();
  zScript = ticket_editpage_code();
  Th_Store("login", login_name());
  Th_Store("date", db_text(0, "SELECT datetime('now')"));
  Th_CreateCommand(g.interp, "append_field", appendRemarkCmd, 0, 0);
  Th_CreateCommand(g.interp, "submit_ticket", submitTicketCmd, (void*)&zName,0);
  if( g.thTrace ) Th_Trace("BEGIN_TKTEDIT_SCRIPT<br />\n", -1);
  if( Th_Render(zScript)==TH_RETURN && !g.thTrace && zName ){
    cgi_redirect(mprintf("%s/tktview/%s", g.zTop, zName));
    return;
  }
  captcha_generate(0);
  @ </form>
  if( g.thTrace ) Th_Trace("BEGIN_TKTEDIT<br />\n", -1);
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
    if( rc!=SQLITE_OK ){
      zErr = mprintf("schema fails to define valid a TICKET "
                     "table containing all required fields");
    }else{
      rc = sqlite3_exec(db, "SELECT tkt_id, tkt_mtime FROM ticketchng", 0,0,0);
      if( rc!=SQLITE_OK ){
        zErr = mprintf("schema fails to define valid a TICKETCHNG "
                       "table containing all required fields");
      }
    }
    sqlite3_close(db);
  }
  return zErr;
}

/*
** WEBPAGE: tkttimeline
** URL: /tkttimeline?name=TICKETUUID&y=TYPE
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
  if( !g.perm.Hyperlink || !g.perm.RdTkt ){
    login_needed(g.anon.Hyperlink && g.anon.RdTkt);
    return;
  }
  zUuid = PD("name","");
  zType = PD("y","a");
  if( zType[0]!='c' ){
    style_submenu_element("Check-ins", "%s/tkttimeline?name=%T&y=ci",
       g.zTop, zUuid);
  }else{
    style_submenu_element("Timeline", "%s/tkttimeline?name=%T", g.zTop, zUuid);
  }
  style_submenu_element("History", "%s/tkthistory/%s", g.zTop, zUuid);
  style_submenu_element("Status", "%s/info/%s", g.zTop, zUuid);
  if( zType[0]=='c' ){
    zTitle = mprintf("Check-ins Associated With Ticket %h", zUuid);
  }else{
    zTitle = mprintf("Timeline Of Ticket %h", zUuid);
  }
  style_header("%z", zTitle);

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
  db_prepare(&q, "%z", zSQL/*safe-for-%s*/);
  www_print_timeline(&q, TIMELINE_ARTID|TIMELINE_DISJOINT|TIMELINE_GRAPH,
                     0, 0, 0, 0);
  db_finalize(&q);
  style_footer();
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
  int nChng = 0;

  login_check_credentials();
  if( !g.perm.Hyperlink || !g.perm.RdTkt ){
    login_needed(g.anon.Hyperlink && g.anon.RdTkt);
    return;
  }
  zUuid = PD("name","");
  zTitle = mprintf("History Of Ticket %h", zUuid);
  style_submenu_element("Status", "%s/info/%s", g.zTop, zUuid);
  style_submenu_element("Check-ins", "%s/tkttimeline?name=%s&y=ci",
    g.zTop, zUuid);
  style_submenu_element("Timeline", "%s/tkttimeline?name=%s", g.zTop, zUuid);
  if( P("plaintext")!=0 ){
    style_submenu_element("Formatted", "%R/tkthistory/%s", zUuid);
  }else{
    style_submenu_element("Plaintext", "%R/tkthistory/%s?plaintext", zUuid);
  }
  style_header("%z", zTitle);

  tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'tkt-%q*'",zUuid);
  if( tagid==0 ){
    @ No such ticket: %h(zUuid)
    style_footer();
    return;
  }
  db_prepare(&q,
    "SELECT datetime(mtime,toLocal()), objid, uuid, NULL, NULL, NULL"
    "  FROM event, blob"
    " WHERE objid IN (SELECT rid FROM tagxref WHERE tagid=%d)"
    "   AND blob.rid=event.objid"
    " UNION "
    "SELECT datetime(mtime,toLocal()), attachid, uuid, src, filename, user"
    "  FROM attachment, blob"
    " WHERE target=(SELECT substr(tagname,5) FROM tag WHERE tagid=%d)"
    "   AND blob.rid=attachid"
    " ORDER BY 1",
    tagid, tagid
  );
  while( db_step(&q)==SQLITE_ROW ){
    Manifest *pTicket;
    const char *zDate = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    const char *zChngUuid = db_column_text(&q, 2);
    const char *zFile = db_column_text(&q, 4);
    if( nChng==0 ){
      @ <ol>
    }
    nChng++;
    if( zFile!=0 ){
      const char *zSrc = db_column_text(&q, 3);
      const char *zUser = db_column_text(&q, 5);
      if( zSrc==0 || zSrc[0]==0 ){
        @
        @ <li><p>Delete attachment "%h(zFile)"
      }else{
        @
        @ <li><p>Add attachment
        @ "%z(href("%R/artifact/%!S",zSrc))%s(zFile)</a>"
      }
      @ [%z(href("%R/artifact/%!S",zChngUuid))%S(zChngUuid)</a>]
      @ (rid %d(rid)) by
      hyperlink_to_user(zUser,zDate," on");
      hyperlink_to_date(zDate, ".</p>");
    }else{
      pTicket = manifest_get(rid, CFTYPE_TICKET, 0);
      if( pTicket ){
        @
        @ <li><p>Ticket change
        @ [%z(href("%R/artifact/%!S",zChngUuid))%S(zChngUuid)</a>]
        @ (rid %d(rid)) by
        hyperlink_to_user(pTicket->zUser,zDate," on");
        hyperlink_to_date(zDate, ":");
        @ </p>
        ticket_output_change_artifact(pTicket, "a");
      }
      manifest_destroy(pTicket);
    }
  }
  db_finalize(&q);
  if( nChng ){
    @ </ol>
  }
  style_footer();
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
void ticket_output_change_artifact(Manifest *pTkt, const char *zListType){
  int i;
  int wikiFlags = WIKI_NOBADLINKS;
  const char *zBlock = "<blockquote>";
  const char *zEnd = "</blockquote>";
  if( P("plaintext")!=0 ){
    wikiFlags |= WIKI_LINKSONLY;
    zBlock = "<blockquote><pre class='verbatim'>";
    zEnd = "</pre></blockquote>";
  }
  if( zListType==0 ) zListType = "1";
  @ <ol type="%s(zListType)">
  for(i=0; i<pTkt->nField; i++){
    Blob val;
    const char *z;
    z = pTkt->aField[i].zName;
    blob_set(&val, pTkt->aField[i].zValue);
    if( z[0]=='+' ){
      @ <li>Appended to %h(&z[1]):%s(zBlock)
      wiki_convert(&val, 0, wikiFlags);
      @ %s(zEnd)</li>
    }else if( blob_size(&val)>50 || contains_newline(&val) ){
      @ <li>Change %h(z) to:%s(zBlock)
      wiki_convert(&val, 0, wikiFlags);
      @ %s(zEnd)</li>
    }else{
      @ <li>Change %h(z) to "%h(blob_str(&val))"</li>
    }
    blob_reset(&val);
  }
  @ </ol>
}

/*
** COMMAND: ticket*
**
** Usage: %fossil ticket SUBCOMMAND ...
**
** Run various subcommands to control tickets
**
**   %fossil ticket show (REPORTTITLE|REPORTNR) ?TICKETFILTER? ?OPTIONS?
**
**     Options:
**       -l|--limit LIMITCHAR
**       -q|--quote
**       -R|--repository FILE
**
**     Run the ticket report, identified by the report format title
**     used in the GUI. The data is written as flat file on stdout,
**     using TAB as separator. The separator can be changed using
**     the -l or --limit option.
**
**     If TICKETFILTER is given on the commandline, the query is
**     limited with a new WHERE-condition.
**       example:  Report lists a column # with the uuid
**                 TICKETFILTER may be [#]='uuuuuuuuu'
**       example:  Report only lists rows with status not open
**                 TICKETFILTER: status != 'open'
**
**     If --quote is used, the tickets are encoded by quoting special
**     chars (space -> \\s, tab -> \\t, newline -> \\n, cr -> \\r,
**     formfeed -> \\f, vtab -> \\v, nul -> \\0, \\ -> \\\\).
**     Otherwise, the simplified encoding as on the show report raw page
**     in the GUI is used. This has no effect in JSON mode.
**
**     Instead of the report title it's possible to use the report
**     number; the special report number 0 lists all columns defined in
**     the ticket table.
**
**   %fossil ticket list fields
**   %fossil ticket ls fields
**
**     List all fields defined for ticket in the fossil repository.
**
**   %fossil ticket list reports
**   %fossil ticket ls reports
**
**     List all ticket reports defined in the fossil repository.
**
**   %fossil ticket set TICKETUUID (FIELD VALUE)+ ?-q|--quote?
**   %fossil ticket change TICKETUUID (FIELD VALUE)+ ?-q|--quote?
**
**     Change ticket identified by TICKETUUID to set the values of
**     each field FIELD to VALUE.
**
**     Field names as defined in the TICKET table.  By default, these
**     names include: type, status, subsystem, priority, severity, foundin,
**     resolution, title, and comment, but other field names can be added
**     or substituted in customized installations.
**
**     If you use +FIELD, the VALUE is appended to the field FIELD.  You
**     can use more than one field/value pair on the commandline.  Using
**     --quote enables the special character decoding as in "ticket
**     show", which allows setting multiline text or text with special
**     characters.
**
**   %fossil ticket add FIELD VALUE ?FIELD VALUE .. ? ?-q|--quote?
**
**     Like set, but create a new ticket with the given values.
**
**   %fossil ticket history TICKETUUID
**
**     Show the complete change history for the ticket
**
** Note that the values in set|add are not validated against the
** definitions given in "Ticket Common Script".
*/
void ticket_cmd(void){
  int n;
  const char *zUser;
  const char *zDate;
  const char *zTktUuid;

  /* do some ints, we want to be inside a checkout */
  db_find_and_open_repository(0, 0);
  user_select();

  zUser = find_option("user-override",0,1);
  if( zUser==0 ) zUser = login_name();
  zDate = find_option("date-override",0,1);
  if( zDate==0 ) zDate = "now";
  zDate = date_in_standard_format(zDate);
  zTktUuid = find_option("uuid-override",0,1);
  if( zTktUuid && (strlen(zTktUuid)!=40 || !validate16(zTktUuid,40)) ){
    fossil_fatal("invalid --uuid-override: must be 40 characters of hex");
  }

  /*
  ** Check that the user exists.
  */
  if( !db_exists("SELECT 1 FROM user WHERE login=%Q", zUser) ){
    fossil_fatal("no such user: %s", zUser);
  }

  if( g.argc<3 ){
    usage("add|change|list|set|show|history");
  }
  n = strlen(g.argv[2]);
  if( n==1 && g.argv[2][0]=='s' ){
    /* set/show cannot be distinguished, so show the usage */
    usage("add|change|list|set|show|history");
  }
  if(( strncmp(g.argv[2],"list",n)==0 ) || ( strncmp(g.argv[2],"ls",n)==0 )){
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
          printf("%s\n",aField[i].zName);
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
      enum { set,add,history,err } eCmd = err;
      int i = 0;
      Blob tktchng, cksum;

      /* get command type (set/add) and get uuid, if needed for set */
      if( strncmp(g.argv[2],"set",n)==0 || strncmp(g.argv[2],"change",n)==0 ||
         strncmp(g.argv[2],"history",n)==0 ){
        if( strncmp(g.argv[2],"history",n)==0 ){
          eCmd = history;
        }else{
          eCmd = set;
        }
        if( g.argc==3 ){
          usage("set|change|history TICKETUUID");
        }
        zTktUuid = db_text(0,
          "SELECT tkt_uuid FROM ticket WHERE tkt_uuid GLOB '%q*'", g.argv[3]
        );
        if( !zTktUuid ){
          fossil_fatal("unknown ticket: '%s'!",g.argv[3]);
        }
        i=4;
      }else if( strncmp(g.argv[2],"add",n)==0 ){
        eCmd = add;
        i = 3;
        if( zTktUuid==0 ){
          zTktUuid = db_text(0, "SELECT lower(hex(randomblob(20)))");
        }
      }
      /* none of set/add, so show the usage! */
      if( eCmd==err ){
        usage("add|fieldlist|set|show|history");
      }

      /* we just handle history separately here, does not get out */
      if( eCmd==history ){
        Stmt q;
        int tagid;

        if( i != g.argc ){
          fossil_fatal("no other parameters expected to %s!",g.argv[2]);
        }
        tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'tkt-%q*'",
                       zTktUuid);
        if( tagid==0 ){
          fossil_fatal("no such ticket %h", zTktUuid);
        }
        db_prepare(&q,
          "SELECT datetime(mtime,toLocal()), objid, NULL, NULL, NULL"
          "  FROM event, blob"
          " WHERE objid IN (SELECT rid FROM tagxref WHERE tagid=%d)"
          "   AND blob.rid=event.objid"
          " UNION "
          "SELECT datetime(mtime,toLocal()), attachid, filename, "
          "       src, user"
          "  FROM attachment, blob"
          " WHERE target=(SELECT substr(tagname,5) FROM tag WHERE tagid=%d)"
          "   AND blob.rid=attachid"
          " ORDER BY 1 DESC",
          tagid, tagid
        );
        while( db_step(&q)==SQLITE_ROW ){
          Manifest *pTicket;
          const char *zDate = db_column_text(&q, 0);
          int rid = db_column_int(&q, 1);
          const char *zFile = db_column_text(&q, 2);
          if( zFile!=0 ){
            const char *zSrc = db_column_text(&q, 3);
            const char *zUser = db_column_text(&q, 4);
            if( zSrc==0 || zSrc[0]==0 ){
              fossil_print("Delete attachment %s\n", zFile);
            }else{
              fossil_print("Add attachment %s\n", zFile);
            }
            fossil_print(" by %s on %s\n", zUser, zDate);
          }else{
            pTicket = manifest_get(rid, CFTYPE_TICKET, 0);
            if( pTicket ){
              int i;

              fossil_print("Ticket Change by %s on %s:\n",
                           pTicket->zUser, zDate);
              for(i=0; i<pTicket->nField; i++){
                Blob val;
                const char *z;
                z = pTicket->aField[i].zName;
                blob_set(&val, pTicket->aField[i].zValue);
                if( z[0]=='+' ){
                  fossil_print("  Append to ");
            z++;
          }else{
            fossil_print("  Change ");
          }
          fossil_print("%h: ",z);
          if( blob_size(&val)>50 || contains_newline(&val)) {
                  fossil_print("\n    ");
                  comment_print(blob_str(&val),0,4,-1,g.comFmtFlags);
                }else{
                  fossil_print("%s\n",blob_str(&val));
                }
                blob_reset(&val);
              }
            }
            manifest_destroy(pTicket);
          }
        }
        db_finalize(&q);
        return;
      }
      /* read all given ticket field/value pairs from command line */
      if( i==g.argc ){
        fossil_fatal("empty %s command aborted!",g.argv[2]);
      }
      getAllTicketFields();
      /* read commandline and assign fields in the aField[].zValue array */
      while( i<g.argc ){
        char *zFName;
        char *zFValue;
        int j;
        int append = 0;

        zFName = g.argv[i++];
        if( i==g.argc ){
          fossil_fatal("missing value for '%s'!",zFName);
        }
        zFValue = g.argv[i++];
        if( tktEncoding == tktFossilize ){
          zFValue=mprintf("%s",zFValue);
          defossilize(zFValue);
        }
        append = (zFName[0] == '+');
        if( append ){
          zFName++;
        }
        j = fieldId(zFName);
        if( j == -1 ){
          fossil_fatal("unknown field name '%s'!",zFName);
        }else{
          if( append ){
            aField[j].zAppend = zFValue;
          }else{
            aField[j].zValue = zFValue;
          }
        }
      }

      /* now add the needed artifacts to the repository */
      blob_zero(&tktchng);
      /* add the time to the ticket manifest */
      blob_appendf(&tktchng, "D %s\n", zDate);
      /* append defined elements */
      for(i=0; i<nField; i++){
        char *zValue = 0;
        char *zPfx;

        if( aField[i].zAppend && aField[i].zAppend[0] ){
          zPfx = " +";
          zValue = aField[i].zAppend;
        }else if( aField[i].zValue && aField[i].zValue[0] ){
          zPfx = " ";
          zValue = aField[i].zValue;
        }else{
          continue;
        }
        if( memcmp(aField[i].zName, "private_", 8)==0 ){
          zValue = db_conceal(zValue, strlen(zValue));
          blob_appendf(&tktchng, "J%s%s %s\n", zPfx, aField[i].zName, zValue);
        }else{
          blob_appendf(&tktchng, "J%s%s %#F\n", zPfx,
                       aField[i].zName, strlen(zValue), zValue);
        }
      }
      blob_appendf(&tktchng, "K %s\n", zTktUuid);
      blob_appendf(&tktchng, "U %F\n", zUser);
      md5sum_blob(&tktchng, &cksum);
      blob_appendf(&tktchng, "Z %b\n", &cksum);
      if( ticket_put(&tktchng, zTktUuid, ticket_need_moderation(1)) ){
        fossil_fatal("%s", g.zErrMsg);
      }else{
        fossil_print("ticket %s succeeded for %s\n",
             (eCmd==set?"set":"add"),zTktUuid);
      }
    }
  }
}


#if INTERFACE
/* Standard submenu items for wiki pages */
#define T_SRCH        0x00001
#define T_REPLIST     0x00002
#define T_NEW         0x00004
#define T_ALL         0x00007
#define T_ALL_BUT(x)  (T_ALL&~(x))
#endif

/*
** Add some standard submenu elements for ticket screens.
*/
void ticket_standard_submenu(unsigned int ok){
  if( (ok & T_SRCH)!=0 && search_restrict(SRCH_TKT)!=0 ){
    style_submenu_element("Search", "%R/tktsrch");
  }
  if( (ok & T_REPLIST)!=0 ){
    style_submenu_element("Reports", "%R/reportlist");
  }
  if( (ok & T_NEW)!=0 && g.anon.NewTkt ){
    style_submenu_element("New", "%R/tktnew");
  }
}

/*
** WEBPAGE: ticket
**
** This is intended to be the primary "Ticket" page.  Render as
** either ticket-search (if search is enabled) or as the
** /reportlist page (if ticket search is disabled).
*/
void tkt_home_page(void){
  login_check_credentials();
  if( search_restrict(SRCH_TKT)!=0 ){
    tkt_srchpage();
  }else{
    view_list();
  }
}

/*
** WEBPAGE: tktsrch
** Usage:  /tktsrch?s=PATTERN
**
** Full-text search of all current tickets
*/
void tkt_srchpage(void){
  login_check_credentials();
  style_header("Ticket Search");
  ticket_standard_submenu(T_ALL_BUT(T_SRCH));
  search_screen(SRCH_TKT, 0);
  style_footer();
}
