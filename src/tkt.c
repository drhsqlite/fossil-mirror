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
#if 0
#include "config.h"
#include "tkt.h"
#include <assert.h>

/*
** The list of database user-defined fields in the TICKET table.
** The real table also contains some addition fields for internal
** used.  The internal-use fields begin with "tkt_".
*/
static int nField = 0;
static char **azField = 0;

/*
** A subscript interpreter used for processing Tickets.
*/
struct Subscript *pInterp = 0;

/*
** Compare two entries in azField for sorting purposes
*/
static int nameCmpr(void *a, void *b){
  return strcmp((char*)a, (char*)b);
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
      azField = realloc(azField, sizeof(azField)*(nField+10) );
      if( azField==0 ){
        fossil_fatal("out of memory");
      }
    }
    azField[nField] = mprintf("%s", zField);
    nField++;
  }
  db_finalize(&q);
  qsort(azField, nField, sizeof(azField[0]), nameCmpr);
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

  if( createFlag ){  
    db_multi_exec("INSERT OR IGNORE INTO ticket(tkt_uuid) "
                  "VALUES(%Q)", p->zTicketUuid);
  }
  blob_zero(&sql);
  blob_appendf(&sql, "UPDATE ticket SET tkt_mtime=:mtime");
  zSep = "SET";
  for(i=0; i<p->nField; i++){
    const char *zName = p->aField[i].zName;
    if( zName[0]=='+' ){
      if( !isTicketField(zName) ) continue;
      blob_appendf(&sql,", %s=%s || %Q", zName, zName, p->aField[i].zValue);
    }else{
      if( !isTicketField(zName) ) continue;
      blob_appendf(&sql,", %s=%Q", zName, p->aField[i].zValue);
    }
  }
  blob_appendf(&sql, " WHERE tkt_uuid='%s'", p->zTicketUuid);
  db_prepare(&q, "%s", blob_str(&sql));
  db_bind_double(&q, ":mtime", p->rDate);
  db_step(&q);
  db_finalize(&q);
  blob_reset(&sql);
}

/*
** Rebuild an entire entry in the TICKET table
*/
void ticket_rebuild_entry(const char *zTktUuid){
  char *zTag = mprintf("tkt-%s", zTktUuid);
  int tagid = tag_findid(zTag, 1);
  Stmt *q;
  Manifest manifest;
  Blob content;
  int clearFlag = 1;
  
  db_multi_exec(
     "DELETE FROM ticket WHERE tkt_uuid=%Q", zTktUuid
  );
  db_prepare(&q, "SELECT rid FROM tagxref WHERE tagid=%d ORDER BY mtime",tagid);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    content_get(rid, &entry);
    manifest_parse(&manifest, &entry);
    ticket_insert(&manifest, clearFlag);
    manifest_clear(&manifest);
    clearFlag = 0;
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
  zConfig = db_text(zDefaultTicketConfig,
             "SELECT value FROM config WHERE name='ticket-configuration'");
  SbS_Eval(pInterp, zConfig);
}

/*
** Rebuild the entire TICKET table.
*/
void ticket_rebuild(void){
  char *zSql;
  int nSql;
  db_multi_exec("DROP TABLE IF EXISTS ticket;");
  ticket_init();
  zSql = SbS_Fetch(pInterp, "ticket_sql", &nSql);
  if( zSql==0 ){
    fossil_error("no ticket_sql defined by ticket configuration");
  }
  zSql = mprintf("%.*s", nSql, zSql);
  db_init_database(g.zRepositoryName, zSql, 0);
  free(zSql);
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
}
#endif
