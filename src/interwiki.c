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
  char *zName;
  static Stmt q;
  for(i=0; fossil_isalnum(zTarget[i]); i++){}
  if( zTarget[i]!=':' ) return 0;
  nCode = i;
  if( nCode==4 && strncmp(zTarget,"wiki",4)==0 ) return 0;
  zPage = zTarget + nCode + 1;
  nPage = (int)strlen(zPage);
  db_static_prepare(&q,
     "SELECT value->>'base', value->>'hash', value->>'wiki'"
     " FROM config WHERE name=lower($name) AND json_valid(value)"
  );
  zName = mprintf("interwiki:%.*s", nCode, zTarget);
  db_bind_text(&q, "$name", zName);
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
  db_reset(&q);
  free(zName);
  return zUrl;
}

/*
** If hyperlink target zTarget begins with an interwiki tag that ought
** to be excluded from display, then return the number of characters in
** that tag.
**
** Path interwiki targets always return zero.  In other words, links
** of the form:
**
**       remote:/path/to/file.txt
**
** Do not have the interwiki tag removed.  But Hash and Wiki links are
** transformed:
**
**       src:39cb0a323f2f3fb6  ->  39cb0a323f2f3fb6
**       fossil:To Do List     ->  To Do List
*/
int interwiki_removable_prefix(const char *zTarget){
  int i;
  for(i=0; fossil_isalnum(zTarget[i]); i++){}
  if( zTarget[i]!=':' ) return 0;
  i++;
  if( zTarget[i]==0 || zTarget[i]=='/' ) return 0;
  return i;
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
** Manage the "intermap" that defines the mapping from interwiki tags
** to complete URLs for interwiki links.
**
** > fossil interwiki delete TAG ...
**
**        Delete one or more interwiki maps.
**
** > fossil interwiki edit TAG --base URL --hash PATH --wiki PATH
**
**        Create an interwiki referenced call TAG.  The base URL is
**        the --base option, which is required.  The --hash and --wiki
**        paths are optional.  The TAG must be lower-case alphanumeric
**        and must be unique.  A new entry is created if it does not
**        already exit.
**
** > fossil interwiki list
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
    if( g.argc!=4 ) usage("add TAG ?OPTIONS?");
    zName = g.argv[3];
    if( zBase==0 ){
      fossil_fatal("the --base option is required");
    }
    if( !interwiki_valid_name(zName) ){
      fossil_fatal("not a valid interwiki tag: \"%s\"", zName);
    }
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    db_multi_exec(
       "REPLACE INTO config(name,value,mtime)"
       " VALUES('interwiki:'||lower(%Q),"
              " json_object('base',%Q,'hash',%Q,'wiki',%Q),"
              " now());",
       zName, zBase, zHash, zWiki
    );
    setup_incr_cfgcnt();
    db_protect_pop();
    db_commit_transaction();
  }else
  if( strncmp(zCmd, "delete", nCmd)==0 ){
    int i;
    verify_all_options();
    if( g.argc<4 ) usage("delete ID ...");
    db_begin_write();
    db_unprotect(PROTECT_CONFIG);
    for(i=3; i<g.argc; i++){
      const char *zName = g.argv[i];
      db_multi_exec(
        "DELETE FROM config WHERE name='interwiki:%q'",
        zName
      );
    }
    setup_incr_cfgcnt();
    db_protect_pop();
    db_commit_transaction();
  }else
  if( strncmp(zCmd, "list", nCmd)==0 ){
    Stmt q;
    int n = 0;
    verify_all_options();
    db_prepare(&q,
      "SELECT substr(name,11),"
      "       value->>'base', value->>'hash', value->>'wiki'"
      "  FROM config WHERE name glob 'interwiki:*' AND json_valid(value)"
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


/*
** Append text to the "Markdown" or "Wiki" rules pages that shows
** a table of all interwiki tags available on this system.
*/
void interwiki_append_map_table(Blob *out){
  int n = 0;
  Stmt q;
  db_prepare(&q,
    "SELECT substr(name,11), value->>'base'"
    "  FROM config WHERE name glob 'interwiki:*' AND json_valid(value)"
    " ORDER BY name;"
  );
  blob_append(out, "<blockquote>", -1);
  while( db_step(&q)==SQLITE_ROW ){
    if( n==0 ){
      blob_appendf(out, "<table>\n");
    }
    blob_appendf(out,"<tr><td>%h</td><td>&nbsp;&rarr;&nbsp;</td>",
       db_column_text(&q,0));
    blob_appendf(out,"<td>%h</td></tr>\n",
       db_column_text(&q,1));
    n++;
  }
  db_finalize(&q);
  if( n>0 ){
    blob_appendf(out,"</table></blockquote>\n");
  }else{
    blob_appendf(out,"<i>None</i></blockquote>\n");
  }
}

/*
** WEBPAGE: intermap
**
** View and modify the interwiki tag map or "intermap".
** This page is visible to administrators only.
*/
void interwiki_page(void){
  Stmt q;
  int n = 0;
  const char *z;
  const char *zTag = "";
  const char *zBase = "";
  const char *zHash = "";
  const char *zWiki = "";
  char *zErr = 0;

  login_check_credentials();
  if( !g.perm.Read && !g.perm.RdWiki && ~g.perm.RdTkt ){
    login_needed(0);
    return;
  }
  if( g.perm.Setup && P("submit")!=0 && cgi_csrf_safe(2) ){
    zTag = PT("tag");
    zBase = PT("base");
    zHash = PT("hash");
    zWiki = PT("wiki");
    if( zTag==0 || zTag[0]==0 || !interwiki_valid_name(zTag) ){
      zErr = mprintf("Not a valid interwiki tag name: \"%s\"", zTag?zTag : "");
    }else if( zBase==0 || zBase[0]==0 ){
      db_unprotect(PROTECT_CONFIG);
      db_multi_exec("DELETE FROM config WHERE name='interwiki:%q';", zTag);
      db_protect_pop();
    }else{
      if( zHash && zHash[0]==0 ) zHash = 0;
      if( zWiki && zWiki[0]==0 ) zWiki = 0;
      db_unprotect(PROTECT_CONFIG);
      db_multi_exec(
        "REPLACE INTO config(name,value,mtime)"
        "VALUES('interwiki:'||lower(%Q),"
        " json_object('base',%Q,'hash',%Q,'wiki',%Q),"
        " now());",
        zTag, zBase, zHash, zWiki);
      db_protect_pop();
    }
  }

  style_set_current_feature("interwiki");
  style_header("Interwiki Map Configuration");
  @ <p>Interwiki links are hyperlink targets of the form
  @ <blockquote><i>Tag</i><b>:</b><i>PageName</i></blockquote>
  @ <p>Such links resolve to links to <i>PageName</i> on a separate server
  @ identified by <i>Tag</i>.  The Interwiki Map or "intermap" is a mapping
  @ from <i>Tags</i> to complete Server URLs.
  db_prepare(&q,
    "SELECT substr(name,11),"
    "       value->>'base', value->>'hash', value->>'wiki'"
    "  FROM config WHERE name glob 'interwiki:*' AND json_valid(value)"
  );
  while( db_step(&q)==SQLITE_ROW ){
    if( n==0 ){
      @ The current mapping is as follows:
      @ <ol>
    }
    @ <li><p> %h(db_column_text(&q,0))
    @ <ul>
    @ <li> Base-URL: <tt>%h(db_column_text(&q,1))</tt>
    z = db_column_text(&q,2);
    if( z==0 ){
      @ <li> Hash-path: <i>NULL</i>
    }else{
      @ <li> Hash-path: <tt>%h(z)</tt>
    }
    z = db_column_text(&q,3);
    if( z==0 ){
      @ <li> Wiki-path: <i>NULL</i>
    }else{
      @ <li> Wiki-path: <tt>%h(z)</tt>
    }
    @ </ul>
    n++;
  }
  db_finalize(&q);
  if( n ){
    @ </ol>
  }else{
    @ No mappings are currently defined.
  }

  if( !g.perm.Setup ){
    /* Do not show intermap editing fields to non-setup users */
    style_finish_page();
    return;
  }

  @ <p>To add a new mapping, fill out the form below providing a unique name
  @ for the tag.  To edit an exist mapping, fill out the form and use the
  @ existing name as the tag.  To delete an existing mapping, fill in the
  @ tag field but leave the "Base URL" field blank.</p>
  if( zErr ){
    @ <p class="error">%h(zErr)</p>
  }
  @ <form method="POST" action="%R/intermap">
  login_insert_csrf_secret();
  @ <table border="0">
  @ <tr><td class="form_label" id="imtag">Tag:</td>
  @ <td><input type="text" id="tag" aria-labeledby="imtag" name="tag" \
  @ size="15" value="%h(zTag)"></td></tr>
  @ <tr><td class="form_label" id="imbase">Base&nbsp;URL:</td>
  @ <td><input type="text" id="base" aria-labeledby="imbase" name="base" \
  @ size="70" value="%h(zBase)"></td></tr>
  @ <tr><td class="form_label" id="imhash">Hash-path:</td>
  @ <td><input type="text" id="hash" aria-labeledby="imhash" name="hash" \
  @ size="20" value="%h(zHash)">
  @ (use "<tt>/info/</tt>" when the target is Fossil)</td></tr>
  @ <tr><td class="form_label" id="imwiki">Wiki-path:</td>
  @ <td><input type="text" id="wiki" aria-labeledby="imwiki" name="wiki" \
  @ size="20" value="%h(zWiki)">
  @ (use "<tt>/wiki?name=</tt>" when the target is Fossil)</td></tr>
  @ <tr><td></td>
  @ <td><input type="submit" name="submit" value="Apply Changes"></td></tr>
  @ </table>
  @ </form>

  style_finish_page();
}
