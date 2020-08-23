/*
** Copyright (c) 2020 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
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
** This file contains subroutines used for recognizing, configuring, and
** handling interwiki hyperlinks.
*/
#include "config.h"
#include "interwiki.h"


/*
** If zTarget is an interwiki link, return a pointer to a URL for that
** link target in memory obtained from fossil_malloc().  If zTarget is
** not a valid interwiki link, return NULL.
**
** An interwiki link target is of the form:
**
**       Code:PageName
**
** "Code" is a brief code that describes the intended target wiki.
** The code must be ASCII alpha-numeric.  No symbols or non-ascii
** characters are allows.  Case is ignored for the code.
** Codes are assigned by "intermap:*" entries in the CONFIG table.
** The link is only valid if there exists an entry in the CONFIG table
** that matches "intermap:Code".
**
** Each value of each intermap:Code entry in the CONFIG table is a JSON
** object with the following fields:
**
**    {
**      "base":  Base URL for the remote site.
**      "hash":  Append this to "base" for Hash targets.
**      "wiki":  Append this to "base" for Wiki targets.
**    }
**
** If the remote wiki is Fossil, then the correct value for "hash" 
** is "/info/" and the correct value for "wiki" is "/wiki?name=".
** If (for example) Wikipedia is the remote, then "hash" should be
** omitted and the correct value for "wiki" is "/wiki/".  
**
** PageName is link name of the target wiki.  Several different forms
** of PageName are recognized.
**
**    Path       If PageName is empty or begins with a "/" character, then
**               it is a pathname that is appended to "base".
**
**    Hash       If PageName is a hexadecimal string of 4 or more
**               characters, then PageName is appended to "hash" which
**               is then appended to "base".
**
**    Wiki       If PageName does not start with "/" and it is
**               not a hexadecimal string of 4 or more characters, then
**               PageName is appended to "wiki" and that combination is
**               appended to "base".
**
** See https://en.wikipedia.org/wiki/Interwiki_links for further information
** on interwiki links.
*/
char *interwiki_url(const char *zTarget){
  int nCode;
  int i;
  const char *zPage;
  int nPage;
  char *zUrl = 0;
  Stmt q;
  for(i=0; fossil_isalnum(zTarget[i]); i++){}
  if( zTarget[i]!=':' ) return 0;
  nCode = i;
  if( nCode==4 && strncmp(zTarget,"wiki",4)==0 ) return 0;
  zPage = zTarget + nCode + 1;
  nPage = (int)strlen(zPage);
  db_prepare(&q, 
     "SELECT json_extract(value,'$.base'),"
           " json_extract(value,'$.hash'),"
           " json_extract(value,'$.wiki')"
     " FROM config WHERE name=lower('interwiki:%.*q')",
     nCode, zTarget);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zBase = db_column_text(&q,0);
    if( zBase==0 || zBase[0]==0 ) break;
    if( nPage==0 || zPage[0]=='/' ){
      /* Path */
      zUrl = mprintf("%s%s", zBase, zPage);
    }else if( nPage>=4 && validate16(zPage,nPage) ){
      /* Hash */
      const char *zHash = db_column_text(&q,1);
      if( zHash && zHash[0] ){
        zUrl = mprintf("%s%s%s", zBase, zHash, zPage);
      }
    }else{
      /* Wiki */
      const char *zWiki = db_column_text(&q,2);
      if( zWiki && zWiki[0] ){
        zUrl = mprintf("%s%s%s", zBase, zWiki, zPage);
      }
    }
    break;
  }
  db_finalize(&q);
  return zUrl;
}

/*
** Verify that a name is a valid interwiki "Code".  Rules:
**
**     *    ascii
**     *    alphanumeric
*/
static int interwiki_valid_name(const char *zName){
  int i;
  for(i=0; zName[i]; i++){
    if( !fossil_isalnum(zName[i]) ) return 0;
  }
  return 1;
}

/*
** COMMAND: interwiki*
**
** Usage: %fossil interwiki COMMAND ...
**
** Manage interwiki labels using the following commands:
**
** >  fossil interwiki delete NAME ...
**
**        Delete one or more interwiki maps.
**
** >  fossil interwiki edit NAME --base URL --hash PATH --wiki PATH
**
**        Create a interwiki referenced call NAME.  The base URL is
**        the --base option, which is required.  The --hash and --wiki
**        paths are optional.  The NAME must be lower-case alphanumeric
**        and must be unique.  A new entry is created if it does not
**        already exit.
**
** >  fossil interwiki list
**
**        Show all interwiki mappings.
*/
void interwiki_cmd(void){
  const char *zCmd;
  int nCmd;
  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    usage("SUBCOMMAND ...");
  }
  zCmd = g.argv[2];
  nCmd = (int)strlen(zCmd);
  if( strncmp(zCmd,"edit",nCmd)==0 ){
    const char *zName;
    const char *zBase = find_option("base",0,1);
    const char *zHash = find_option("hash",0,1);
    const char *zWiki = find_option("wiki",0,1);
    verify_all_options();
    if( g.argc!=4 ) usage("add NAME ?OPTIONS?");
    zName = g.argv[3];
    if( zBase){
      fossil_fatal("the --base option is required");
    }
    if( !interwiki_valid_name(zName) ){
      fossil_fatal("not a valid interwiki tag: \"%s\"", zName);
    }
    db_begin_write();
    db_multi_exec(
       "REPLACE INTO config(name,value,mtime)"
       " VALUES('interwiki:'||lower(%Q),"
              " json_object('base',%Q,'hash',%Q,'wiki',%Q),"
              " now());",
       zName, zBase, zHash, zWiki
    );
    setup_incr_cfgcnt();
    db_commit_transaction();
  }else
  if( strncmp(zCmd, "delete", nCmd)==0 ){
    int i;
    verify_all_options();
    if( g.argc<4 ) usage("delete ID ...");
    db_begin_write();
    for(i=3; i<g.argc; i++){
      const char *zName = g.argv[i];
      db_multi_exec(
        "DELETE FROM config WHERE name='interwiki:%q'", 
        zName
      );
    }
    setup_incr_cfgcnt();
    db_commit_transaction();
  }else
  if( strncmp(zCmd, "list", nCmd)==0 ){
    Stmt q;
    int n = 0;
    verify_all_options();
    db_prepare(&q,
      "SELECT substr(name,11),"
      "       json_extract(value,'$.base'),"
      "       json_extract(value,'$.hash'),"
      "       json_extract(value,'$.wiki')"
      "  FROM config WHERE name glob 'interwiki:*'"
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zBase, *z, *zName;
      if( n++ ) fossil_print("\n");
      zName = db_column_text(&q,0);
      zBase = db_column_text(&q,1);
      fossil_print("%-15s %s\n", zName, zBase);
      z = db_column_text(&q,2);
      if( z ){
        fossil_print("%15s %s%s\n", "", zBase, z);
      }
      z = db_column_text(&q,3);
      if( z ){
        fossil_print("%15s %s%s\n", "", zBase, z);
      }
    }
    db_finalize(&q);
  }else
  {
     fossil_fatal("unknown command \"%s\" - should be one of: "
                  "delete edit list", zCmd);
  }
}
