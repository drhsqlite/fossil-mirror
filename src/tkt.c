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
** A subscript interpreter used for processing Tickets.
*/
static struct Subscript *pInterp = 0;

/*
** Compare two entries in azField for sorting purposes
*/
static int nameCmpr(const void *a, const void *b){
  return strcmp(*(char**)a, *(char**)b);
}

/*
** Obtain a list of all fields of the TICKET table.  Put them 
** in sorted order.
*/
static void getAllTicketFields(void){
  Stmt q;
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
  memset(azAppend, 0, sizeof(azAppend[0])*nField*2);
  azValue = &azAppend[nField];
}

/*
** Return true if zField is a field within the TICKET table.
*/
static int isTicketField(const char *zField){
  int i;
  for(i=0; i<nField; i++){
    if( strcmp(azField[i], zField)==0 ) return 1;
  }
  return 0;
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

  zName = PD("name","");
  db_prepare(&q, "SELECT * FROM ticket WHERE tkt_uuid GLOB '%q*'", zName);
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
      if( SbS_Fetch(pInterp, zName, -1, &size)==0 ){
        SbS_Store(pInterp, db_column_name(&q,i), zVal, 1);
      }
    }
  }else{
    db_finalize(&q);
    db_prepare(&q, "PRAGMA table_info(ticket)");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zField = db_column_text(&q, 1);
      if( SbS_Fetch(pInterp, zField, -1, &size)==0 ){
        SbS_Store(pInterp, zField, "", 0);
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
    SbS_Store(pInterp, z, P(z), 0);
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
  blob_appendf(&sql, "UPDATE ticket SET tkt_mtime=:mtime");
  zSep = "SET";
  for(i=0; i<p->nField; i++){
    const char *zName = p->aField[i].zName;
    if( zName[0]=='+' ){
      zName++;
      if( !isTicketField(zName) ) continue;
      blob_appendf(&sql,", %s=%s || %Q", zName, zName, p->aField[i].zValue);
    }else{
      if( !isTicketField(zName) ) continue;
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
    db_multi_exec("INSERT OR IGNORE INTO _pending_ticket"
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
** Create the subscript interpreter and load the ticket configuration.
*/
void ticket_init(void){
  char *zConfig;
  if( pInterp ) return;
  pInterp = SbS_Create();
  zConfig = db_text((char*)zDefaultTicketConfig,
             "SELECT value FROM config WHERE name='ticket-configuration'");
  SbS_Eval(pInterp, zConfig, -1);
}

/*
** Recreate the ticket table.
*/
void ticket_create_table(int separateConnection){
  char *zSql;
  int nSql;

  db_multi_exec("DROP TABLE IF EXISTS ticket;");
  ticket_init();
  zSql = (char*)SbS_Fetch(pInterp, "ticket_sql", -1, &nSql);
  if( zSql==0 ){
    fossil_panic("no ticket_sql defined by ticket configuration");
  }
  if( separateConnection ){
    zSql = mprintf("%.*s", nSql, zSql);
    db_init_database(g.zRepositoryName, zSql, 0);
    free(zSql);
  }else{
    db_multi_exec("%.*s", nSql, zSql);
  }
}

/*
** Repopulate the ticket table
*/
void ticket_rebuild(void){
  Stmt q;
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
*/
void tktview_page(void){
  char *zScript;
  int nScript;
  login_check_credentials();
  if( !g.okRdTkt ){ login_needed(); return; }
  style_header("View Ticket");
  ticket_init();
  initializeVariablesFromDb();
  zScript = (char*)SbS_Fetch(pInterp, "tktview_template", -1, &nScript);
  zScript = mprintf("%.*s", nScript, zScript);
  SbS_Render(pInterp, zScript);
  style_footer();
}

/*
** Subscript command:   LABEL submit_new_ticket
**
** If the variable named LABEL exists, then submit a new ticket
** based on the values of other defined variables.
*/
static int submitNewCmd(struct Subscript *p, void *pNotify){
  const char *zLabel;
  int nLabel, size;
  if( SbS_RequireStack(p, 1, "submit_new_ticket") ) return 1;
  zLabel = SbS_StackValue(p, 0, &nLabel);
  if( SbS_Fetch(p, zLabel, nLabel, &size)!=0 ){
    char *zDate, *zUuid;
    int i;
    int rid;
    Blob tktchng, cksum;

    blob_zero(&tktchng);
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10] = 'T';
    blob_appendf(&tktchng, "D %s\n", zDate);
    free(zDate);
    for(i=0; i<nField; i++){
      const char *zValue;
      int nValue;
      zValue = SbS_Fetch(p, azField[i], -1, &nValue);
      if( zValue ){
        while( nValue>0 && isspace(zValue[nValue-1]) ){ nValue--; }
        blob_appendf(&tktchng, "J %s %z\n",
           azField[i], fossilize(zValue,nValue));
      }
    }
    zUuid = db_text(0, "SELECT lower(hex(randomblob(20)))");
    blob_appendf(&tktchng, "K %s\n", zUuid);
    (*(char**)pNotify) = zUuid;
    blob_appendf(&tktchng, "U %F\n", g.zLogin ? g.zLogin : "");
    md5sum_blob(&tktchng, &cksum);
    blob_appendf(&tktchng, "Z %b\n", &cksum);

#if 0
    @ <hr><pre>
    @ %h(blob_str(&tktchng))
    @ </pre><hr>
    blob_zero(&tktchng)
    SbS_Pop(p, 1);
    return SBS_OK;
#endif

    rid = content_put(&tktchng, 0, 0);
    if( rid==0 ){
      fossil_panic("trouble committing ticket: %s", g.zErrMsg);
    }
    manifest_crosslink(rid, &tktchng);
    return SBS_RETURN;
  }
  SbS_Pop(p, 1);
  return SBS_OK;  
}

/*
** WEBPAGE: tktnew
*/
void tktnew_page(void){
  char *zScript;
  int nScript;
  char *zNewUuid = 0;

  login_check_credentials();
  if( !g.okNewTkt ){ login_needed(); return; }
  style_header("New Ticket");
  ticket_init();
  getAllTicketFields();
  initializeVariablesFromCGI();
  @ <form method="POST" action="%s(g.zBaseURL)/tktnew">
  zScript = (char*)SbS_Fetch(pInterp, "tktnew_template", -1, &nScript);
  zScript = mprintf("%.*s", nScript, zScript);
  SbS_AddVerb(pInterp, "submit_new_ticket", submitNewCmd, (void*)&zNewUuid);
  if( SbS_Render(pInterp, zScript)==SBS_RETURN && zNewUuid ){
    cgi_redirect(mprintf("%s/tktview/%s", g.zBaseURL, zNewUuid));
    return;
  }
  @ </form>
  style_footer();
}



/*
** Subscript command:   STR1 STR2 USERVAR APPENDVAR FIELD append_remark
**
** FIELD is the name of a database column to which we might want
** to append text.  APPENDVAR is the name of a CGI parameter which
** (if it exists) contains the text to be appended.  The append
** operation will only happen if APPENDVAR exists.  USERVAR is
** a CGI parameter which contains the name that the user wants to
** to be known by.  STR1 and STR2 are prefixes that are prepended
** to the text in the APPENDVAR CGI parameter.  STR1 is used if
** USERVAR is the same as g.zLogin or if USERVAR does not exist.
** STR2 is used if USERVAR exists and is different than g.zLogin.
** Within STR1 and STR2, the following substitutions occur:
**
**     %LOGIN%    The value of g.zLogin
**     %USER%     The value of the USERVAR CGI parameter
**     %DATE%     The current date and time
**
** The concatenation STR1 or STR2 with the content of APPENDVAR
** is written into azApnd[] in the FIELD slot so that it will be
** picked up and used by the submit_ticket_change command.
*/
static int appendRemarkCmd(struct Subscript *p, void *notUsed){
  int i, j, idx;
  const char *zField, *zAppendVar, *zUserVar, *zStr, *zValue, *zUser;
  int nField, nAppendVar, nUserVar, nStr, nValue, nUser;

  if( SbS_RequireStack(p, 5, "append_remark") ) return 1;
  zField = SbS_StackValue(p, 0, &nField);
  for(idx=0; idx<nField; idx++){
    if( strncmp(azField[idx], zField, nField)==0 && azField[idx][nField]==0 ){
      break;
    }
  }
  if( idx>=nField ){
    SbS_SetErrorMessage(p, "no such TICKET column: %.*s", nField, zField);
    return SBS_ERROR;
  }
  zAppendVar = SbS_StackValue(p, 1, &nAppendVar);
  zValue = SbS_Fetch(p, zAppendVar, nAppendVar, &nValue);
  if( zValue ){
    Blob out;
    blob_zero(&out);
    zUserVar = SbS_StackValue(p, 2, &nUserVar);
    zUser = SbS_Fetch(p, zUserVar, nUserVar, &nUser);
    if( zUser && (strncmp(zUser, g.zLogin, nUser) || g.zLogin[nUser]!=0) ){
      zStr = SbS_StackValue(p, 3, &nStr);
    }else{
      zStr = SbS_StackValue(p, 4, &nStr);
    }
    for(i=j=0; i<nStr; i++){
      if( zStr[i]!='%' ) continue;
      if( i>j ){
        blob_append(&out, &zStr[j], i-j);
        j = i;
      }
      if( strncmp(&zStr[j], "%USER%", 6)==0 ){
        blob_appendf(&out, "%z", htmlize(zUser, nUser));
        i += 5;
        j = i+1;
      }else if( strncmp(&zStr[j], "%LOGIN%", 7)==0 ){
        blob_appendf(&out, "%z", htmlize(g.zLogin, -1));
        i += 6;
        j = i+1;
      }else if( strncmp(&zStr[j], "%DATE%", 6)==0 ){
        blob_appendf(&out, "%z", db_text(0, "SELECT datetime('now')"));
        i += 5;
        j = i+1;
      }
    }
    if( i>j ){
      blob_append(&out, &zStr[j], i-j);     
    }
    blob_append(&out, zValue, nValue);
    azAppend[idx] = blob_str(&out);
  }
  SbS_Pop(p, 5);
  return SBS_OK;
}

/*
** Subscript command:   LABEL submit_ticket_change
**
** If the variable named LABEL exists, then submit a change to
** the ticket identified by the "name" CGI parameter.
*/
static int submitEditCmd(struct Subscript *p, void *pNotify){
  const char *zLabel;
  int nLabel, size;

  if( SbS_RequireStack(p, 1, "submit_ticket_change") ) return 1;
  zLabel = SbS_StackValue(p, 0, &nLabel);
  if( SbS_Fetch(p, zLabel, nLabel, &size)!=0 ){
    char *zDate, *zUuid;
    int i;
    int rid;
    Blob tktchng, cksum;

    (*(int*)pNotify) = 1;
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
        zValue = SbS_Fetch(p, azField[i], -1, &nValue);
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
    zUuid = db_text(0, 
       "SELECT tkt_uuid FROM ticket WHERE tkt_uuid GLOB '%s*'",
       P("name")
    );
    blob_appendf(&tktchng, "K %s\n", zUuid);
    (*(char**)pNotify) = zUuid;
    blob_appendf(&tktchng, "U %F\n", g.zLogin ? g.zLogin : "");
    md5sum_blob(&tktchng, &cksum);
    blob_appendf(&tktchng, "Z %b\n", &cksum);

#if 1
    @ <hr><pre>
    @ %h(blob_str(&tktchng))
    @ </pre><hr>
    blob_zero(&tktchng);
    SbS_Pop(p, 1);
    return SBS_OK;
#endif

    rid = content_put(&tktchng, 0, 0);
    if( rid==0 ){
      fossil_panic("trouble committing ticket: %s", g.zErrMsg);
    }
    manifest_crosslink(rid, &tktchng);
    return SBS_RETURN;
  }
  SbS_Pop(p, 1);
  return SBS_OK;  
}

/*
** WEBPAGE: tktedit
*/
void tktedit_page(void){
  char *zScript;
  int nScript;
  int chnged = 0;
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
  @ <form method="POST" action="%s(g.zBaseURL)/tktedit">
  @ <input type="hidden" name="name" value="%s(zName)">
  zScript = (char*)SbS_Fetch(pInterp, "tktedit_template", -1, &nScript);
  zScript = mprintf("%.*s", nScript, zScript);
  SbS_AddVerb(pInterp, "append_remark", appendRemarkCmd, 0);
  SbS_AddVerb(pInterp, "submit_ticket_change", submitEditCmd, (void*)&chnged);
  if( SbS_Render(pInterp, zScript)==SBS_RETURN && chnged ){
    cgi_redirect(mprintf("%s/tktview/%s", g.zBaseURL, zName));
    return;
  }
  @ </form>
  style_footer();
}
