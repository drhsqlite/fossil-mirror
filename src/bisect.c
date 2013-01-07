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
  if( bisect.bad==0 ){
    fossil_fatal("no \"bad\" version has been identified");
  }
  bisect.good = db_lget_int("bisect-good", 0);
  if( bisect.good==0 ){
    fossil_fatal("no \"good\" version has been identified");
  }
  p = path_shortest(bisect.good, bisect.bad, bisect_option("direct-only"), 0);
  if( p==0 ){
    char *zBad = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", bisect.bad);
    char *zGood = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", bisect.good);
    fossil_fatal("no path from good ([%S]) to bad ([%S]) or back",
                 zGood, zBad);
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
};

/*
** Return the value of a boolean bisect option.
*/
int bisect_option(const char *zName){
  unsigned int i;
  int r = -1;
  for(i=0; i<sizeof(aBisectOption)/sizeof(aBisectOption[0]); i++){
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
**   fossil bisect vlist|ls ?--all?
**
**     List the versions in between "bad" and "good".
*/
void bisect_cmd(void){
  int n;
  const char *zCmd;
  db_must_be_within_tree();
  if( g.argc<3 ){
    usage("bisect SUBCOMMAND ARGS...");
  }
  zCmd = g.argv[2];
  n = strlen(zCmd);
  if( n==0 ) zCmd = "-";
  if( memcmp(zCmd, "bad", n)==0 ){
    int ridBad;
    if( g.argc==3 ){
      ridBad = db_lget_int("checkout",0);
    }else{
      ridBad = name_to_typed_rid(g.argv[3], "ci");
    }
    db_lset_int("bisect-bad", ridBad);
    if( ridBad>0
     && bisect_option("auto-next")
     && db_lget_int("bisect-good",0)>0
    ){
      zCmd = "next";
      n = 4;
    }else{
      return;
    }
  }else if( memcmp(zCmd, "good", n)==0 ){
    int ridGood;
    if( g.argc==3 ){
      ridGood = db_lget_int("checkout",0);
    }else{
      ridGood = name_to_typed_rid(g.argv[3], "ci");
    }
    db_lset_int("bisect-good", ridGood);
    if( ridGood>0
     && bisect_option("auto-next")
     && db_lget_int("bisect-bad",0)>0
    ){
      zCmd = "next";
      n = 4;
    }else{
      return;
    }
  }
  if( memcmp(zCmd, "next", n)==0 ){
    PathNode *pMid;
    bisect_path();
    pMid = path_midpoint();
    if( pMid==0 ){
      fossil_fatal("bisect is done - there are no more intermediate versions");
    }
    g.argv[1] = "update";
    g.argv[2] = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", pMid->rid);
    g.argc = 3;
    g.fNoSync = 1;
    update_cmd();
    bisect_list(1);
  }else if( memcmp(zCmd, "options", n)==0 ){
    if( g.argc==3 ){
      unsigned int i;
      for(i=0; i<sizeof(aBisectOption)/sizeof(aBisectOption[0]); i++){
        char *z = mprintf("bisect-%s", aBisectOption[i].zName);
        fossil_print("  %-15s  %-6s  ", aBisectOption[i].zName,
               db_lget(z, (char*)aBisectOption[i].zDefault));
        fossil_free(z);
        comment_print(aBisectOption[i].zDesc, 27, 79);
      }
    }else if( g.argc==4 || g.argc==5 ){
      unsigned int i;
      n = strlen(g.argv[3]);
      for(i=0; i<sizeof(aBisectOption)/sizeof(aBisectOption[0]); i++){
        if( memcmp(g.argv[3], aBisectOption[i].zName, n)==0 ){
          char *z = mprintf("bisect-%s", aBisectOption[i].zName);
          if( g.argc==5 ){
            db_lset(z, g.argv[4]);
          }
          fossil_print("%s\n", db_lget(z, (char*)aBisectOption[i].zDefault));
          fossil_free(z);
          break;
        }
      }
      if( i>=sizeof(aBisectOption)/sizeof(aBisectOption[0]) ){
        fossil_fatal("no such bisect option: %s", g.argv[3]);
      }
    }else{
      usage("bisect option ?NAME? ?VALUE?");
    }
  }else if( memcmp(zCmd, "reset", n)==0 ){
    db_multi_exec(
      "DELETE FROM vvar WHERE name IN ('bisect-good', 'bisect-bad');"
    );
  }else if( memcmp(zCmd, "vlist", n)==0 || memcmp(zCmd, "ls", n)==0 ){
    int fAll = find_option("all", 0, 0)!=0;
    bisect_list(!fAll);
  }else{
    usage("bad|good|next|reset|vlist ...");
  }
}
