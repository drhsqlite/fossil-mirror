/*
** Copyright (c) 2010 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@sqlite.org
**
*******************************************************************************
**
** This file contains code used to implement the "bisect" command.
**
** This file also contains logic used to compute the closure of filename
** changes that have occurred across multiple check-ins.
*/
#include "config.h"
#include "bisect.h"
#include <assert.h>

/*
** Local variables for this module
*/
static struct {
  int bad;                /* The bad version */
  int good;               /* The good version */
} bisect;

/*
** Find the shortest path between bad and good.
*/
void bisect_path(void){
  PathNode *p;
  bisect.bad = db_lget_int("bisect-bad", 0);
  bisect.good = db_lget_int("bisect-good", 0);
  if( bisect.good>0 && bisect.bad==0 ){
    path_shortest(bisect.good, bisect.good, 0, 0);
  }else if( bisect.bad>0 && bisect.good==0 ){
    path_shortest(bisect.bad, bisect.bad, 0, 0);
  }else if( bisect.bad==0 && bisect.good==0 ){
    fossil_fatal("neither \"good\" nor \"bad\" versions have been identified");
  }else{
    p = path_shortest(bisect.good, bisect.bad, bisect_option("direct-only"), 0);
    if( p==0 ){
      char *zBad = db_text(0,"SELECT uuid FROM blob WHERE rid=%d",bisect.bad);
      char *zGood = db_text(0,"SELECT uuid FROM blob WHERE rid=%d",bisect.good);
      fossil_fatal("no path from good ([%S]) to bad ([%S]) or back",
                   zGood, zBad);
    }
  }
}

/*
** The set of all bisect options.
*/
static const struct {
  const char *zName;
  const char *zDefault;
  const char *zDesc;
} aBisectOption[] = {
  { "auto-next",    "on",    "Automatically run \"bisect next\" after each "
                             "\"bisect good\" or \"bisect bad\"" },
  { "direct-only",  "on",    "Follow only primary parent-child links, not "
                             "merges\n" },
  { "display",    "chart",   "Command to run after \"next\".  \"chart\", "
                             "\"log\", \"status\", or \"none\"" },
};

/*
** Return the value of a boolean bisect option.
*/
int bisect_option(const char *zName){
  unsigned int i;
  int r = -1;
  for(i=0; i<count(aBisectOption); i++){
    if( fossil_strcmp(zName, aBisectOption[i].zName)==0 ){
      char *zLabel = mprintf("bisect-%s", zName);
      char *z = db_lget(zLabel, (char*)aBisectOption[i].zDefault);
      if( is_truth(z) ) r = 1;
      if( is_false(z) ) r = 0;
      if( r<0 ) r = is_truth(aBisectOption[i].zDefault);
      free(zLabel);
      break;
    }
  }
  assert( r>=0 );
  return r;
}

/*
** List a bisect path.
*/
static void bisect_list(int abbreviated){
  PathNode *p;
  int vid = db_lget_int("checkout", 0);
  int n;
  Stmt s;
  int nStep;
  int nHidden = 0;
  bisect_path();
  db_prepare(&s, "SELECT blob.uuid, datetime(event.mtime) "
                 "  FROM blob, event"
                 " WHERE blob.rid=:rid AND event.objid=:rid"
                 "   AND event.type='ci'");
  nStep = path_length();
  if( abbreviated ){
    for(p=path_last(); p; p=p->pFrom) p->isHidden = 1;
    for(p=path_last(), n=0; p; p=p->pFrom, n++){
      if( p->rid==bisect.good
       || p->rid==bisect.bad
       || p->rid==vid
       || (nStep>1 && n==nStep/2)
      ){
        p->isHidden = 0;
        if( p->pFrom ) p->pFrom->isHidden = 0;
      }
    }
    for(p=path_last(); p; p=p->pFrom){
      if( p->pFrom && p->pFrom->isHidden==0 ) p->isHidden = 0;
    }
  }
  for(p=path_last(), n=0; p; p=p->pFrom, n++){
    if( p->isHidden && (nHidden || (p->pFrom && p->pFrom->isHidden)) ){
      nHidden++;
      continue;
    }else if( nHidden ){
      fossil_print("  ... %d other check-ins omitted\n", nHidden);
      nHidden = 0;
    }
    db_bind_int(&s, ":rid", p->rid);
    if( db_step(&s)==SQLITE_ROW ){
      const char *zUuid = db_column_text(&s, 0);
      const char *zDate = db_column_text(&s, 1);
      fossil_print("%s %S", zDate, zUuid);
      if( p->rid==bisect.good ) fossil_print(" GOOD");
      if( p->rid==bisect.bad ) fossil_print(" BAD");
      if( p->rid==vid ) fossil_print(" CURRENT");
      if( nStep>1 && n==nStep/2 ) fossil_print(" NEXT");
      fossil_print("\n");
    }
    db_reset(&s);
  }
  db_finalize(&s);
}

/*
** Append a new entry to the bisect log.  Update the bisect-good or
** bisect-bad values as appropriate.
**
** The bisect-log consists of a list of token.  Each token is an
** integer RID of a check-in.  The RID is negative for "bad" check-ins
** and positive for "good" check-ins.
*/
static void bisect_append_log(int rid){
  if( rid<0 ){
    if( db_lget_int("bisect-bad",0)==(-rid) ) return;
    db_lset_int("bisect-bad", -rid);
  }else{
    if( db_lget_int("bisect-good",0)==rid ) return;
    db_lset_int("bisect-good", rid);
  }
  db_multi_exec(
     "REPLACE INTO vvar(name,value) VALUES('bisect-log',"
       "COALESCE((SELECT value||' ' FROM vvar WHERE name='bisect-log'),'')"
       " || '%d')", rid);
}

/*
** Create a TEMP table named "bilog" that contains the complete history
** of the current bisect.
*/
void bisect_create_bilog_table(int iCurrent){
  char *zLog = db_lget("bisect-log","");
  Blob log, id;
  Stmt q;
  int cnt = 0;
  blob_init(&log, zLog, -1);
  db_multi_exec(
     "CREATE TEMP TABLE bilog("
     "  seq INTEGER PRIMARY KEY,"  /* Sequence of events */
     "  stat TEXT,"                /* Type of occurrence */
     "  rid INTEGER UNIQUE"        /* Check-in number */
     ");"
  );
  db_prepare(&q, "INSERT OR IGNORE INTO bilog(seq,stat,rid)"
                 " VALUES(:seq,:stat,:rid)");
  while( blob_token(&log, &id) ){
    int rid = atoi(blob_str(&id));
    db_bind_int(&q, ":seq", ++cnt);
    db_bind_text(&q, ":stat", rid>0 ? "GOOD" : "BAD");
    db_bind_int(&q, ":rid", rid>=0 ? rid : -rid);
    db_step(&q);
    db_reset(&q);
  }
  if( iCurrent>0 ){
    db_bind_int(&q, ":seq", ++cnt);
    db_bind_text(&q, ":stat", "CURRENT");
    db_bind_int(&q, ":rid", iCurrent);
    db_step(&q);
  }
  db_finalize(&q);
}

/*
** Show a chart of bisect "good" and "bad" versions.  The chart can be
** sorted either chronologically by bisect time, or by check-in time.
*/
static void bisect_chart(int sortByCkinTime){
  Stmt q;
  int iCurrent = db_lget_int("checkout",0);
  bisect_create_bilog_table(iCurrent);
  db_prepare(&q,
    "SELECT bilog.seq, bilog.stat,"
    "       substr(blob.uuid,1,16), datetime(event.mtime),"
    "       blob.rid==%d"
    "  FROM bilog, blob, event"
    " WHERE blob.rid=bilog.rid AND event.objid=bilog.rid"
    "   AND event.type='ci'"
    " ORDER BY %s bilog.rowid ASC",
    iCurrent, (sortByCkinTime ? "event.mtime DESC, " : "")
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zGoodBad = db_column_text(&q, 1);
    fossil_print("%3d %-7s %s %s%s\n",
        db_column_int(&q, 0),
        zGoodBad,
        db_column_text(&q, 3),
        db_column_text(&q, 2),
        (db_column_int(&q, 4) && zGoodBad[0]!='C') ? " CURRENT" : "");
  }
  db_finalize(&q);
}

/*
** COMMAND: bisect
**
** Usage: %fossil bisect SUBCOMMAND ...
**
** Run various subcommands useful for searching for bugs.
**
**   fossil bisect bad ?VERSION?
**
**     Identify version VERSION as non-working.  If VERSION is omitted,
**     the current checkout is marked as non-working.
**
**   fossil bisect good ?VERSION?
**
**     Identify version VERSION as working.  If VERSION is omitted,
**     the current checkout is marked as working.
**
**   fossil bisect log
**   fossil bisect chart
**
**     Show a log of "good" and "bad" versions.  "bisect log" shows the
**     events in the order that they were tested.  "bisect chart" shows
**     them in order of check-in.
**
**   fossil bisect next
**
**     Update to the next version that is halfway between the working and
**     non-working versions.
**
**   fossil bisect options ?NAME? ?VALUE?
**
**     List all bisect options, or the value of a single option, or set the
**     value of a bisect option.
**
**   fossil bisect reset
**
**     Reinitialize a bisect session.  This cancels prior bisect history
**     and allows a bisect session to start over from the beginning.
**
**   fossil bisect vlist|ls|status ?-a|--all?
**
**     List the versions in between "bad" and "good".
**
**   fossil bisect ui
**
**     Like "fossil ui" except start on a timeline that shows only the
**     check-ins that are part of the current bisect.
**
**   fossil bisect undo
**
**     Undo the most recent "good" or "bad" command.
**
** Summary:
**
**   fossil bisect bad ?VERSION?
**   fossil bisect good ?VERSION?
**   fossil bisect log
**   fossil bisect chart
**   fossil bisect next
**   fossil bisect options
**   fossil bisect reset
**   fossil bisect status
**   fossil bisect ui
**   fossil bisect undo
*/
void bisect_cmd(void){
  int n;
  const char *zCmd;
  int foundCmd = 0;
  db_must_be_within_tree();
  if( g.argc<3 ){
    usage("bad|good|log|next|options|reset|status|undo");
  }
  zCmd = g.argv[2];
  n = strlen(zCmd);
  if( n==0 ) zCmd = "-";
  if( strncmp(zCmd, "bad", n)==0 ){
    int ridBad;
    foundCmd = 1;
    if( g.argc==3 ){
      ridBad = db_lget_int("checkout",0);
    }else{
      ridBad = name_to_typed_rid(g.argv[3], "ci");
    }
    if( ridBad>0 ){
      bisect_append_log(-ridBad);
      if( bisect_option("auto-next") && db_lget_int("bisect-good",0)>0 ){
        zCmd = "next";
        n = 4;
      }
    }
  }else if( strncmp(zCmd, "good", n)==0 ){
    int ridGood;
    foundCmd = 1;
    if( g.argc==3 ){
      ridGood = db_lget_int("checkout",0);
    }else{
      ridGood = name_to_typed_rid(g.argv[3], "ci");
    }
    if( ridGood>0 ){
      bisect_append_log(ridGood);
      if( bisect_option("auto-next") && db_lget_int("bisect-bad",0)>0 ){
        zCmd = "next";
        n = 4;
      }
    }
  }else if( strncmp(zCmd, "undo", n)==0 ){
    char *zLog;
    Blob log, id;
    int ridBad = 0;
    int ridGood = 0;
    int cnt = 0, i;
    foundCmd = 1;
    db_begin_transaction();
    zLog = db_lget("bisect-log","");
    blob_init(&log, zLog, -1);
    while( blob_token(&log, &id) ){ cnt++; }
    if( cnt==0 ){
      fossil_fatal("no previous bisect steps to undo");
    }
    blob_rewind(&log);
    for(i=0; i<cnt-1; i++){
      int rid;
      blob_token(&log, &id);
      rid = atoi(blob_str(&id));
      if( rid<0 ) ridBad = -rid;
      else        ridGood = rid;
    }
    db_multi_exec(
      "UPDATE vvar SET value=substr(value,1,%d) WHERE name='bisect-log'",
      log.iCursor-1
    );
    db_lset_int("bisect-bad", ridBad);
    db_lset_int("bisect-good", ridGood);
    db_end_transaction(0);
    if( ridBad && ridGood ){
      zCmd = "next";
      n = 4;
    }
  }
  /* No else here so that the above commands can morph themselves into
  ** a "next" command */
  if( strncmp(zCmd, "next", n)==0 ){
    PathNode *pMid;
    char *zDisplay = db_lget("bisect-display","chart");
    int m = (int)strlen(zDisplay);
    bisect_path();
    pMid = path_midpoint();
    if( pMid==0 ){
      fossil_print("bisect complete\n");
    }else{
      g.argv[1] = "update";
      g.argv[2] = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", pMid->rid);
      g.argc = 3;
      g.fNoSync = 1;
      update_cmd();
    }

    if( strncmp(zDisplay,"chart",m)==0 ){
      bisect_chart(1);
    }else if( strncmp(zDisplay, "log", m)==0 ){
      bisect_chart(0);
    }else if( strncmp(zDisplay, "status", m)==0 ){
      bisect_list(1);
    }
  }else if( strncmp(zCmd, "log", n)==0 ){
    bisect_chart(0);
  }else if( strncmp(zCmd, "chart", n)==0 ){
    bisect_chart(1);
  }else if( strncmp(zCmd, "options", n)==0 ){
    if( g.argc==3 ){
      unsigned int i;
      for(i=0; i<count(aBisectOption); i++){
        char *z = mprintf("bisect-%s", aBisectOption[i].zName);
        fossil_print("  %-15s  %-6s  ", aBisectOption[i].zName,
               db_lget(z, (char*)aBisectOption[i].zDefault));
        fossil_free(z);
        comment_print(aBisectOption[i].zDesc, 0, 27, -1, g.comFmtFlags);
      }
    }else if( g.argc==4 || g.argc==5 ){
      unsigned int i;
      n = strlen(g.argv[3]);
      for(i=0; i<count(aBisectOption); i++){
        if( strncmp(g.argv[3], aBisectOption[i].zName, n)==0 ){
          char *z = mprintf("bisect-%s", aBisectOption[i].zName);
          if( g.argc==5 ){
            db_lset(z, g.argv[4]);
          }
          fossil_print("%s\n", db_lget(z, (char*)aBisectOption[i].zDefault));
          fossil_free(z);
          break;
        }
      }
      if( i>=count(aBisectOption) ){
        fossil_fatal("no such bisect option: %s", g.argv[3]);
      }
    }else{
      usage("options ?NAME? ?VALUE?");
    }
  }else if( strncmp(zCmd, "reset", n)==0 ){
    db_multi_exec(
      "DELETE FROM vvar WHERE name IN "
      " ('bisect-good', 'bisect-bad', 'bisect-log')"
    );
  }else if( strcmp(zCmd, "ui")==0 ){
    char *newArgv[8];
    newArgv[0] = g.argv[0];
    newArgv[1] = "ui";
    newArgv[2] = "--page";
    newArgv[3] = "timeline?bisect";
    newArgv[4] = 0;
    g.argv = newArgv;
    g.argc = 4;
    cmd_webserver();
  }else if( strncmp(zCmd, "vlist", n)==0
         || strncmp(zCmd, "ls", n)==0
         || strncmp(zCmd, "status", n)==0
  ){
    int fAll = find_option("all", "a", 0)!=0;
    bisect_list(!fAll);
  }else if( !foundCmd ){
    usage("bad|good|log|next|options|reset|status|ui|undo");
  }
}
