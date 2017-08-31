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
** Code to generate the ticket listings
*/
#include "config.h"
#include <time.h>
#include "report.h"
#include <assert.h>

/* Forward references to static routines */
static void report_format_hints(void);

#ifndef SQLITE_RECURSIVE
#  define SQLITE_RECURSIVE            33
#endif

/*
** WEBPAGE: reportlist
**
** Main menu for Tickets.
*/
void view_list(void){
  const char *zScript;
  Blob ril;   /* Report Item List */
  Stmt q;
  int rn = 0;
  int cnt = 0;

  login_check_credentials();
  if( !g.perm.RdTkt && !g.perm.NewTkt ){
    login_needed(g.anon.RdTkt || g.anon.NewTkt);
    return;
  }
  style_header("Ticket Main Menu");
  ticket_standard_submenu(T_ALL_BUT(T_REPLIST));
  if( g.thTrace ) Th_Trace("BEGIN_REPORTLIST<br />\n", -1);
  zScript = ticket_reportlist_code();
  if( g.thTrace ) Th_Trace("BEGIN_REPORTLIST_SCRIPT<br />\n", -1);

  blob_zero(&ril);
  ticket_init();

  db_prepare(&q, "SELECT rn, title, owner FROM reportfmt ORDER BY title");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTitle = db_column_text(&q, 1);
    const char *zOwner = db_column_text(&q, 2);
    if( zTitle[0] =='_' && !g.perm.TktFmt ){
      continue;
    }
    rn = db_column_int(&q, 0);
    cnt++;
    blob_appendf(&ril, "<li>");
    if( zTitle[0] == '_' ){
      blob_appendf(&ril, "%s", zTitle);
    } else {
      blob_appendf(&ril, "%z%h</a>", href("%R/rptview?rn=%d", rn), zTitle);
    }
    blob_appendf(&ril, "&nbsp;&nbsp;&nbsp;");
    if( g.perm.Write && zOwner && zOwner[0] ){
      blob_appendf(&ril, "(by <i>%h</i>) ", zOwner);
    }
    if( g.perm.TktFmt ){
      blob_appendf(&ril, "[%zcopy</a>] ",
                   href("%R/rptedit?rn=%d&copy=1", rn));
    }
    if( g.perm.Admin
     || (g.perm.WrTkt && zOwner && fossil_strcmp(g.zLogin,zOwner)==0)
    ){
      blob_appendf(&ril, "[%zedit</a>]",
                         href("%R/rptedit?rn=%d", rn));
    }
    if( g.perm.TktFmt ){
      blob_appendf(&ril, "[%zsql</a>]",
                         href("%R/rptsql?rn=%d", rn));
    }
    blob_appendf(&ril, "</li>\n");
  }
  db_finalize(&q);

  Th_Store("report_items", blob_str(&ril));

  Th_Render(zScript);

  blob_reset(&ril);
  if( g.thTrace ) Th_Trace("END_REPORTLIST<br />\n", -1);

  style_footer();
}

/*
** Remove whitespace from both ends of a string.
*/
char *trim_string(const char *zOrig){
  int i;
  while( fossil_isspace(*zOrig) ){ zOrig++; }
  i = strlen(zOrig);
  while( i>0 && fossil_isspace(zOrig[i-1]) ){ i--; }
  return mprintf("%.*s", i, zOrig);
}

/*
** Extract a numeric (integer) value from a string.
*/
char *extract_integer(const char *zOrig){
  if( zOrig == NULL || zOrig[0] == 0 ) return "";
  while( *zOrig && !fossil_isdigit(*zOrig) ){ zOrig++; }
  if( *zOrig ){
    /* we have a digit. atoi() will get as much of the number as it
    ** can. We'll run it through mprintf() to get a string. Not
    ** an efficient way to do it, but effective.
    */
    return mprintf("%d", atoi(zOrig));
  }
  return "";
}

/*
** Remove blank lines from the beginning of a string and
** all whitespace from the end. Removes whitespace preceding a LF,
** which also converts any CRLF sequence into a single LF.
*/
char *remove_blank_lines(const char *zOrig){
  int i, j, n;
  char *z;
  for(i=j=0; fossil_isspace(zOrig[i]); i++){ if( zOrig[i]=='\n' ) j = i+1; }
  n = strlen(&zOrig[j]);
  while( n>0 && fossil_isspace(zOrig[j+n-1]) ){ n--; }
  z = mprintf("%.*s", n, &zOrig[j]);
  for(i=j=0; z[i]; i++){
    if( z[i+1]=='\n' && z[i]!='\n' && fossil_isspace(z[i]) ){
      z[j] = z[i];
      while(fossil_isspace(z[j]) && z[j] != '\n' ){ j--; }
      j++;
      continue;
    }

    z[j++] = z[i];
  }
  z[j] = 0;
  return z;
}


/*********************************************************************/

/*
** This is the SQLite authorizer callback used to make sure that the
** SQL statements entered by users do not try to do anything untoward.
** If anything suspicious is tried, set *(char**)pError to an error
** message obtained from malloc.
*/
static int report_query_authorizer(
  void *pError,
  int code,
  const char *zArg1,
  const char *zArg2,
  const char *zArg3,
  const char *zArg4
){
  int rc = SQLITE_OK;
  if( *(char**)pError ){
    /* We've already seen an error.  No need to continue. */
    return SQLITE_DENY;
  }
  switch( code ){
    case SQLITE_SELECT:
    case SQLITE_RECURSIVE:
    case SQLITE_FUNCTION: {
      break;
    }
    case SQLITE_READ: {
      static const char *const azAllowed[] = {
         "ticket",
         "ticketchng",
         "blob",
         "filename",
         "mlink",
         "plink",
         "event",
         "tag",
         "tagxref",
         "unversioned",
      };
      int i;
      if( fossil_strncmp(zArg1, "fx_", 3)==0 ){
        break;
      }
      for(i=0; i<count(azAllowed); i++){
        if( fossil_stricmp(zArg1, azAllowed[i])==0 ) break;
      }
      if( i>=count(azAllowed) ){
        *(char**)pError = mprintf("access to table \"%s\" is restricted",zArg1);
        rc = SQLITE_DENY;
      }else if( !g.perm.RdAddr && strncmp(zArg2, "private_", 8)==0 ){
        rc = SQLITE_IGNORE;
      }
      break;
    }
    default: {
      *(char**)pError = mprintf("only SELECT statements are allowed");
      rc = SQLITE_DENY;
      break;
    }
  }
  return rc;
}

/*
** Activate the query authorizer
*/
void report_restrict_sql(char **pzErr){
  sqlite3_set_authorizer(g.db, report_query_authorizer, (void*)pzErr);
  sqlite3_limit(g.db, SQLITE_LIMIT_VDBE_OP, 10000);
}
void report_unrestrict_sql(void){
  sqlite3_set_authorizer(g.db, 0, 0);
}


/*
** Check the given SQL to see if is a valid query that does not
** attempt to do anything dangerous.  Return 0 on success and a
** pointer to an error message string (obtained from malloc) if
** there is a problem.
*/
char *verify_sql_statement(char *zSql){
  int i;
  char *zErr = 0;
  const char *zTail;
  sqlite3_stmt *pStmt;
  int rc;

  /* First make sure the SQL is a single query command by verifying that
  ** the first token is "SELECT" or "WITH" and that there are no unquoted
  ** semicolons.
  */
  for(i=0; fossil_isspace(zSql[i]); i++){}
  if( fossil_strnicmp(&zSql[i], "select", 6)!=0
      && fossil_strnicmp(&zSql[i], "with", 4)!=0 ){
    return mprintf("The SQL must be a SELECT or WITH statement");
  }
  for(i=0; zSql[i]; i++){
    if( zSql[i]==';' ){
      int bad;
      int c = zSql[i+1];
      zSql[i+1] = 0;
      bad = sqlite3_complete(zSql);
      zSql[i+1] = c;
      if( bad ){
        /* A complete statement basically means that an unquoted semi-colon
        ** was found. We don't actually check what's after that.
        */
        return mprintf("Semi-colon detected! "
                       "Only a single SQL statement is allowed");
      }
    }
  }

  /* Compile the statement and check for illegal accesses or syntax errors. */
  report_restrict_sql(&zErr);
  rc = sqlite3_prepare_v2(g.db, zSql, -1, &pStmt, &zTail);
  if( rc!=SQLITE_OK ){
    zErr = mprintf("Syntax error: %s", sqlite3_errmsg(g.db));
  }
  if( !sqlite3_stmt_readonly(pStmt) ){
    zErr = mprintf("SQL must not modify the database");
  }
  if( pStmt ){
    sqlite3_finalize(pStmt);
  }
  report_unrestrict_sql();
  return zErr;
}

/*
** WEBPAGE: rptsql
** URL: /rptsql?rn=N
**
** Display the SQL query used to generate a ticket report.  The rn=N
** query parameter identifies the specific report number to be displayed.
*/
void view_see_sql(void){
  int rn;
  const char *zTitle;
  const char *zSQL;
  const char *zOwner;
  const char *zClrKey;
  Stmt q;

  login_check_credentials();
  if( !g.perm.TktFmt ){
    login_needed(g.anon.TktFmt);
    return;
  }
  rn = atoi(PD("rn","0"));
  db_prepare(&q, "SELECT title, sqlcode, owner, cols "
                   "FROM reportfmt WHERE rn=%d",rn);
  style_header("SQL For Report Format Number %d", rn);
  if( db_step(&q)!=SQLITE_ROW ){
    @ <p>Unknown report number: %d(rn)</p>
    style_footer();
    db_finalize(&q);
    return;
  }
  zTitle = db_column_text(&q, 0);
  zSQL = db_column_text(&q, 1);
  zOwner = db_column_text(&q, 2);
  zClrKey = db_column_text(&q, 3);
  @ <table cellpadding=0 cellspacing=0 border=0>
  @ <tr><td valign="top" align="right">Title:</td><td width=15></td>
  @ <td colspan="3">%h(zTitle)</td></tr>
  @ <tr><td valign="top" align="right">Owner:</td><td></td>
  @ <td colspan="3">%h(zOwner)</td></tr>
  @ <tr><td valign="top" align="right">SQL:</td><td></td>
  @ <td valign="top"><pre>
  @ %h(zSQL)
  @ </pre></td>
  @ <td width=15></td><td valign="top">
  output_color_key(zClrKey, 0, "border=0 cellspacing=0 cellpadding=3");
  @ </td>
  @ </tr></table>
  report_format_hints();
  style_footer();
  db_finalize(&q);
}

/*
** WEBPAGE: rptnew
** WEBPAGE: rptedit
**
** Create (/rptnew) or edit (/rptedit) a ticket report format.
** Query parameters:
**
**     rn=N           Ticket report number. (required)
**     t=TITLE        Title of the report format
**     w=USER         Owner of the report format
**     s=SQL          SQL text used to implement the report
**     k=KEY          Color key
*/
void view_edit(void){
  int rn;
  const char *zTitle;
  const char *z;
  const char *zOwner;
  const char *zClrKey;
  char *zSQL;
  char *zErr = 0;

  login_check_credentials();
  if( !g.perm.TktFmt ){
    login_needed(g.anon.TktFmt);
    return;
  }
  /*view_add_functions(0);*/
  rn = atoi(PD("rn","0"));
  zTitle = P("t");
  zOwner = PD("w",g.zLogin);
  z = P("s");
  zSQL = z ? trim_string(z) : 0;
  zClrKey = trim_string(PD("k",""));
  if( rn>0 && P("del2") ){
    login_verify_csrf_secret();
    db_multi_exec("DELETE FROM reportfmt WHERE rn=%d", rn);
    cgi_redirect("reportlist");
    return;
  }else if( rn>0 && P("del1") ){
    zTitle = db_text(0, "SELECT title FROM reportfmt "
                         "WHERE rn=%d", rn);
    if( zTitle==0 ) cgi_redirect("reportlist");

    style_header("Are You Sure?");
    @ <form action="rptedit" method="post">
    @ <p>You are about to delete all traces of the report
    @ <strong>%h(zTitle)</strong> from
    @ the database.  This is an irreversible operation.  All records
    @ related to this report will be removed and cannot be recovered.</p>
    @
    @ <input type="hidden" name="rn" value="%d(rn)">
    login_insert_csrf_secret();
    @ <input type="submit" name="del2" value="Delete The Report">
    @ <input type="submit" name="can" value="Cancel">
    @ </form>
    style_footer();
    return;
  }else if( P("can") ){
    /* user cancelled */
    cgi_redirect("reportlist");
    return;
  }
  if( zTitle && zSQL ){
    if( zSQL[0]==0 ){
      zErr = "Please supply an SQL query statement";
    }else if( (zTitle = trim_string(zTitle))[0]==0 ){
      zErr = "Please supply a title";
    }else{
      zErr = verify_sql_statement(zSQL);
    }
    if( zErr==0
     && db_exists("SELECT 1 FROM reportfmt WHERE title=%Q and rn<>%d",
                  zTitle, rn)
    ){
      zErr = mprintf("There is already another report named \"%h\"", zTitle);
    }
    if( zErr==0 ){
      login_verify_csrf_secret();
      if( rn>0 ){
        db_multi_exec("UPDATE reportfmt SET title=%Q, sqlcode=%Q,"
                      " owner=%Q, cols=%Q, mtime=now() WHERE rn=%d",
           zTitle, zSQL, zOwner, zClrKey, rn);
      }else{
        db_multi_exec("INSERT INTO reportfmt(title,sqlcode,owner,cols,mtime) "
           "VALUES(%Q,%Q,%Q,%Q,now())",
           zTitle, zSQL, zOwner, zClrKey);
        rn = db_last_insert_rowid();
      }
      cgi_redirect(mprintf("rptview?rn=%d", rn));
      return;
    }
  }else if( rn==0 ){
    zTitle = "";
    zSQL = ticket_report_template();
    zClrKey = ticket_key_template();
  }else{
    Stmt q;
    db_prepare(&q, "SELECT title, sqlcode, owner, cols "
                     "FROM reportfmt WHERE rn=%d",rn);
    if( db_step(&q)==SQLITE_ROW ){
      zTitle = db_column_malloc(&q, 0);
      zSQL = db_column_malloc(&q, 1);
      zOwner = db_column_malloc(&q, 2);
      zClrKey = db_column_malloc(&q, 3);
    }
    db_finalize(&q);
    if( P("copy") ){
      rn = 0;
      zTitle = mprintf("Copy Of %s", zTitle);
      zOwner = g.zLogin;
    }
  }
  if( zOwner==0 ) zOwner = g.zLogin;
  style_submenu_element("Cancel", "reportlist");
  if( rn>0 ){
    style_submenu_element("Delete", "rptedit?rn=%d&del1=1", rn);
  }
  style_header("%s", rn>0 ? "Edit Report Format":"Create New Report Format");
  if( zErr ){
    @ <blockquote class="reportError">%h(zErr)</blockquote>
  }
  @ <form action="rptedit" method="post"><div>
  @ <input type="hidden" name="rn" value="%d(rn)" />
  @ <p>Report Title:<br />
  @ <input type="text" name="t" value="%h(zTitle)" size="60" /></p>
  @ <p>Enter a complete SQL query statement against the "TICKET" table:<br />
  @ <textarea name="s" rows="20" cols="80">%h(zSQL)</textarea>
  @ </p>
  login_insert_csrf_secret();
  if( g.perm.Admin ){
    @ <p>Report owner:
    @ <input type="text" name="w" size="20" value="%h(zOwner)" />
    @ </p>
  } else {
    @ <input type="hidden" name="w" value="%h(zOwner)" />
  }
  @ <p>Enter an optional color key in the following box.  (If blank, no
  @ color key is displayed.)  Each line contains the text for a single
  @ entry in the key.  The first token of each line is the background
  @ color for that line.<br />
  @ <textarea name="k" rows="8" cols="50">%h(zClrKey)</textarea>
  @ </p>
  if( !g.perm.Admin && fossil_strcmp(zOwner,g.zLogin)!=0 ){
    @ <p>This report format is owned by %h(zOwner).  You are not allowed
    @ to change it.</p>
    @ </form>
    report_format_hints();
    style_footer();
    return;
  }
  @ <input type="submit" value="Apply Changes" />
  if( rn>0 ){
    @ <input type="submit" value="Delete This Report" name="del1" />
  }
  @ </div></form>
  report_format_hints();
  style_footer();
}

/*
** Output a bunch of text that provides information about report
** formats
*/
static void report_format_hints(void){
  char *zSchema;
  zSchema = db_text(0,"SELECT sql FROM sqlite_master WHERE name='ticket'");
  if( zSchema==0 ){
    zSchema = db_text(0,"SELECT sql FROM repository.sqlite_master"
                        " WHERE name='ticket'");
  }
  @ <hr /><h3>TICKET Schema</h3>
  @ <blockquote><pre>
  @ %h(zSchema)
  @ </pre></blockquote>
  @ <h3>Notes</h3>
  @ <ul>
  @ <li><p>The SQL must consist of a single SELECT statement</p></li>
  @
  @ <li><p>If a column of the result set is named "#" then that column
  @ is assumed to hold a ticket number.  A hyperlink will be created from
  @ that column to a detailed view of the ticket.</p></li>
  @
  @ <li><p>If a column of the result set is named "bgcolor" then the content
  @ of that column determines the background color of the row.</p></li>
  @
  @ <li><p>The text of all columns prior to the first column whose name begins
  @ with underscore ("_") is shown character-for-character as it appears in
  @ the database.  In other words, it is assumed to have a mimetype of
  @ text/plain.
  @
  @ <li><p>The first column whose name begins with underscore ("_") and all
  @ subsequent columns are shown on their own rows in the table and with
  @ wiki formatting.  In other words, such rows are shown with a mimetype
  @ of text/x-fossil-wiki.  This is recommended for the "description" field
  @ of tickets.
  @ </p></li>
  @
  @ <li><p>The query can join other tables in the database besides TICKET.
  @ </p></li>
  @ </ul>
  @
  @ <h3>Examples</h3>
  @ <p>In this example, the first column in the result set is named
  @ "bgcolor".  The value of this column is not displayed.  Instead, it
  @ selects the background color of each row based on the TICKET.STATUS
  @ field of the database.  The color key at the right shows the various
  @ color codes.</p>
  @ <table class="rpteditex">
  @ <tr style="background-color:#f2dcdc;"><td class="rpteditex">new or active</td></tr>
  @ <tr style="background-color:#e8e8bd;"><td class="rpteditex">review</td></tr>
  @ <tr style="background-color:#cfe8bd;"><td class="rpteditex">fixed</td></tr>
  @ <tr style="background-color:#bde5d6;"><td class="rpteditex">tested</td></tr>
  @ <tr style="background-color:#cacae5;"><td class="rpteditex">defer</td></tr>
  @ <tr style="background-color:#c8c8c8;"><td class="rpteditex">closed</td></tr>
  @ </table>
  @ <blockquote><pre>
  @ SELECT
  @   CASE WHEN status IN ('new','active') THEN '#f2dcdc'
  @        WHEN status='review' THEN '#e8e8bd'
  @        WHEN status='fixed' THEN '#cfe8bd'
  @        WHEN status='tested' THEN '#bde5d6'
  @        WHEN status='defer' THEN '#cacae5'
  @        ELSE '#c8c8c8' END as 'bgcolor',
  @   tn AS '#',
  @   type AS 'Type',
  @   status AS 'Status',
  @   sdate(origtime) AS 'Created',
  @   owner AS 'By',
  @   subsystem AS 'Subsys',
  @   sdate(changetime) AS 'Changed',
  @   assignedto AS 'Assigned',
  @   severity AS 'Svr',
  @   priority AS 'Pri',
  @   title AS 'Title'
  @ FROM ticket
  @ </pre></blockquote>
  @ <p>To base the background color on the TICKET.PRIORITY or
  @ TICKET.SEVERITY fields, substitute the following code for the
  @ first column of the query:</p>
  @ <table class="rpteditex">
  @ <tr style="background-color:#f2dcdc;"><td class="rpteditex">1</td></tr>
  @ <tr style="background-color:#e8e8bd;"><td class="rpteditex">2</td></tr>
  @ <tr style="background-color:#cfe8bd;"><td class="rpteditex">3</td></tr>
  @ <tr style="background-color:#cacae5;"><td class="rpteditex">4</td></tr>
  @ <tr style="background-color:#c8c8c8;"><td class="rpteditex">5</td></tr>
  @ </table>
  @ <blockquote><pre>
  @ SELECT
  @   CASE priority WHEN 1 THEN '#f2dcdc'
  @        WHEN 2 THEN '#e8e8bd'
  @        WHEN 3 THEN '#cfe8bd'
  @        WHEN 4 THEN '#cacae5'
  @        ELSE '#c8c8c8' END as 'bgcolor',
  @ ...
  @ FROM ticket
  @ </pre></blockquote>
#if 0
  @ <p>You can, of course, substitute different colors if you choose.
  @ Here is a palette of suggested background colors:</p>
  @ <blockquote>
  @ <table border=1 cellspacing=0 width=300>
  @ <tr><td align="center" bgcolor="#ffbdbd">#ffbdbd</td>
  @     <td align="center" bgcolor="#f2dcdc">#f2dcdc</td></tr>
  @ <tr><td align="center" bgcolor="#ffffbd">#ffffbd</td>
  @     <td align="center" bgcolor="#e8e8bd">#e8e8bd</td></tr>
  @ <tr><td align="center" bgcolor="#c0ebc0">#c0ebc0</td>
  @     <td align="center" bgcolor="#cfe8bd">#cfe8bd</td></tr>
  @ <tr><td align="center" bgcolor="#c0c0f4">#c0c0f4</td>
  @     <td align="center" bgcolor="#d6d6e8">#d6d6e8</td></tr>
  @ <tr><td align="center" bgcolor="#d0b1ff">#d0b1ff</td>
  @     <td align="center" bgcolor="#d2c0db">#d2c0db</td></tr>
  @ <tr><td align="center" bgcolor="#bbbbbb">#bbbbbb</td>
  @     <td align="center" bgcolor="#d0d0d0">#d0d0d0</td></tr>
  @ </table>
  @ </blockquote>
#endif
  @ <p>To see the TICKET.DESCRIPTION and TICKET.REMARKS fields, include
  @ them as the last two columns of the result set and given them names
  @ that begin with an underscore.  Like this:</p>
  @ <blockquote><pre>
  @  SELECT
  @    tn AS '#',
  @    type AS 'Type',
  @    status AS 'Status',
  @    sdate(origtime) AS 'Created',
  @    owner AS 'By',
  @    subsystem AS 'Subsys',
  @    sdate(changetime) AS 'Changed',
  @    assignedto AS 'Assigned',
  @    severity AS 'Svr',
  @    priority AS 'Pri',
  @    title AS 'Title',
  @    description AS '_Description',  -- When the column name begins with '_'
  @    remarks AS '_Remarks'           -- content is rendered as wiki
  @  FROM ticket
  @ </pre></blockquote>
  @
}

/*
** The state of the report generation.
*/
struct GenerateHTML {
  int rn;          /* Report number */
  int nCount;      /* Row number */
  int nCol;        /* Number of columns */
  int isMultirow;  /* True if multiple table rows per query result row */
  int iNewRow;     /* Index of first column that goes on separate row */
  int iBg;         /* Index of column that defines background color */
  int wikiFlags;   /* Flags passed into wiki_convert() */
  const char *zWikiStart;    /* HTML before display of multi-line wiki */
  const char *zWikiEnd;      /* HTML after display of multi-line wiki */
};

/*
** The callback function for db_query
*/
static int generate_html(
  void *pUser,     /* Pointer to output state */
  int nArg,        /* Number of columns in this result row */
  const char **azArg, /* Text of data in all columns */
  const char **azName /* Names of the columns */
){
  struct GenerateHTML *pState = (struct GenerateHTML*)pUser;
  int i;
  const char *zTid;  /* Ticket UUID.  (value of column named '#') */
  const char *zBg = 0; /* Use this background color */

  /* Do initialization
  */
  if( pState->nCount==0 ){
    /* Turn off the authorizer.  It is no longer doing anything since the
    ** query has already been prepared.
    */
    sqlite3_set_authorizer(g.db, 0, 0);

    /* Figure out the number of columns, the column that determines background
    ** color, and whether or not this row of data is represented by multiple
    ** rows in the table.
    */
    pState->nCol = 0;
    pState->isMultirow = 0;
    pState->iNewRow = -1;
    pState->iBg = -1;
    for(i=0; i<nArg; i++){
      if( azName[i][0]=='b' && fossil_strcmp(azName[i],"bgcolor")==0 ){
        pState->iBg = i;
        continue;
      }
      if( g.perm.Write && azName[i][0]=='#' ){
        pState->nCol++;
      }
      if( !pState->isMultirow ){
        if( azName[i][0]=='_' ){
          pState->isMultirow = 1;
          pState->iNewRow = i;
          pState->wikiFlags = WIKI_NOBADLINKS;
          pState->zWikiStart = "";
          pState->zWikiEnd = "";
          if( P("plaintext") ){
            pState->wikiFlags |= WIKI_LINKSONLY;
            pState->zWikiStart = "<pre class='verbatim'>";
            pState->zWikiEnd = "</pre>";
            style_submenu_element("Formatted", "%R/rptview?rn=%d", pState->rn);
          }else{
            style_submenu_element("Plaintext", "%R/rptview?rn=%d&plaintext",
                                  pState->rn);
          }
        }else{
          pState->nCol++;
        }
      }
    }

    /* The first time this routine is called, output a table header
    */
    @ <thead><tr>
    zTid = 0;
    for(i=0; i<nArg; i++){
      const char *zName = azName[i];
      if( i==pState->iBg ) continue;
      if( pState->iNewRow>=0 && i>=pState->iNewRow ){
        if( g.perm.Write && zTid ){
          @ <th>&nbsp;</th>
          zTid = 0;
        }
        if( zName[0]=='_' ) zName++;
        @ </tr><tr><th colspan=%d(pState->nCol)>%h(zName)</th>
      }else{
        if( zName[0]=='#' ){
          zTid = zName;
        }
        @ <th>%h(zName)</th>
      }
    }
    if( g.perm.Write && zTid ){
      @ <th>&nbsp;</th>
    }
    @ </tr></thead><tbody>
  }
  if( azArg==0 ){
    @ <tr><td colspan="%d(pState->nCol)">
    @ <i>No records match the report criteria</i>
    @ </td></tr>
    return 0;
  }
  ++pState->nCount;

  /* Output the separator above each entry in a table which has multiple lines
  ** per database entry.
  */
  if( pState->iNewRow>=0 ){
    @ <tr><td colspan=%d(pState->nCol)><font size=1>&nbsp;</font></td></tr>
  }

  /* Output the data for this entry from the database
  */
  zBg = pState->iBg>=0 ? azArg[pState->iBg] : 0;
  if( zBg==0 ) zBg = "white";
  @ <tr style="background-color:%h(zBg)">
  zTid = 0;
  for(i=0; i<nArg; i++){
    const char *zData;
    if( i==pState->iBg ) continue;
    zData = azArg[i];
    if( zData==0 ) zData = "";
    if( pState->iNewRow>=0 && i>=pState->iNewRow ){
      if( zTid && g.perm.Write ){
        @ <td valign="top">%z(href("%R/tktedit/%h",zTid))edit</a></td>
        zTid = 0;
      }
      if( zData[0] ){
        Blob content;
        @ </tr>
        @ <tr style="background-color:%h(zBg)"><td colspan=%d(pState->nCol)>
        @ %s(pState->zWikiStart)
        blob_init(&content, zData, -1);
        wiki_convert(&content, 0, pState->wikiFlags);
        blob_reset(&content);
        @ %s(pState->zWikiEnd)
      }
    }else if( azName[i][0]=='#' ){
      zTid = zData;
      @ <td valign="top">%z(href("%R/tktview?name=%h",zData))%h(zData)</a></td>
    }else if( zData[0]==0 ){
      @ <td valign="top">&nbsp;</td>
    }else{
      @ <td valign="top">
      @ %h(zData)
      @ </td>
    }
  }
  if( zTid && g.perm.Write ){
    @ <td valign="top">%z(href("%R/tktedit/%h",zTid))edit</a></td>
  }
  @ </tr>
  return 0;
}

/*
** Output the text given in the argument.  Convert tabs and newlines into
** spaces.
*/
static void output_no_tabs(const char *z){
  while( z && z[0] ){
    int i, j;
    for(i=0; z[i] && (!fossil_isspace(z[i]) || z[i]==' '); i++){}
    if( i>0 ){
      cgi_printf("%.*s", i, z);
    }
    for(j=i; fossil_isspace(z[j]); j++){}
    if( j>i ){
      cgi_printf("%*s", j-i, "");
    }
    z += j;
  }
}

/*
** Output a row as a tab-separated line of text.
*/
static int output_tab_separated(
  void *pUser,     /* Pointer to row-count integer */
  int nArg,        /* Number of columns in this result row */
  const char **azArg, /* Text of data in all columns */
  const char **azName /* Names of the columns */
){
  int *pCount = (int*)pUser;
  int i;

  if( *pCount==0 ){
    for(i=0; i<nArg; i++){
      output_no_tabs(azName[i]);
      cgi_printf("%c", i<nArg-1 ? '\t' : '\n');
    }
  }
  ++*pCount;
  for(i=0; i<nArg; i++){
    output_no_tabs(azArg[i]);
    cgi_printf("%c", i<nArg-1 ? '\t' : '\n');
  }
  return 0;
}

/*
** Generate HTML that describes a color key.
*/
void output_color_key(const char *zClrKey, int horiz, char *zTabArgs){
  int i, j, k;
  const char *zSafeKey;
  char *zToFree;
  while( fossil_isspace(*zClrKey) ) zClrKey++;
  if( zClrKey[0]==0 ) return;
  @ <table %s(zTabArgs)>
  if( horiz ){
    @ <tr>
  }
  zSafeKey = zToFree = mprintf("%h", zClrKey);
  while( zSafeKey[0] ){
    while( fossil_isspace(*zSafeKey) ) zSafeKey++;
    for(i=0; zSafeKey[i] && !fossil_isspace(zSafeKey[i]); i++){}
    for(j=i; fossil_isspace(zSafeKey[j]); j++){}
    for(k=j; zSafeKey[k] && zSafeKey[k]!='\n' && zSafeKey[k]!='\r'; k++){}
    if( !horiz ){
      cgi_printf("<tr style=\"background-color: %.*s;\"><td>%.*s</td></tr>\n",
        i, zSafeKey, k-j, &zSafeKey[j]);
    }else{
      cgi_printf("<td style=\"background-color: %.*s;\">%.*s</td>\n",
        i, zSafeKey, k-j, &zSafeKey[j]);
    }
    zSafeKey += k;
  }
  free(zToFree);
  if( horiz ){
    @ </tr>
  }
  @ </table>
}

/*
** Execute a single read-only SQL statement.  Invoke xCallback() on each
** row.
*/
static int db_exec_readonly(
  sqlite3 *db,                /* The database on which the SQL executes */
  const char *zSql,           /* The SQL to be executed */
  int (*xCallback)(void*,int,const char**, const char**),
                              /* Invoke this callback routine */
  void *pArg,                 /* First argument to xCallback() */
  char **pzErrMsg             /* Write error messages here */
){
  int rc = SQLITE_OK;         /* Return code */
  const char *zLeftover;      /* Tail of unprocessed SQL */
  sqlite3_stmt *pStmt = 0;    /* The current SQL statement */
  const char **azCols = 0;    /* Names of result columns */
  int nCol;                   /* Number of columns of output */
  const char **azVals = 0;    /* Text of all output columns */
  int i;                      /* Loop counter */
  int nVar;                   /* Number of parameters */

  pStmt = 0;
  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, &zLeftover);
  assert( rc==SQLITE_OK || pStmt==0 );
  if( rc!=SQLITE_OK ){
    return rc;
  }
  if( !pStmt ){
    /* this happens for a comment or white-space */
    return SQLITE_OK;
  }
  if( !sqlite3_stmt_readonly(pStmt) ){
    sqlite3_finalize(pStmt);
    return SQLITE_ERROR;
  }

  nVar = sqlite3_bind_parameter_count(pStmt);
  for(i=1; i<=nVar; i++){
    const char *zVar = sqlite3_bind_parameter_name(pStmt, i);
    if( zVar==0 ) continue;
    if( zVar[0]!='$' && zVar[0]!='@' && zVar[0]!=':' ) continue;
    if( !fossil_islower(zVar[1]) ) continue;
    if( strcmp(zVar, "$login")==0 ){
      sqlite3_bind_text(pStmt, i, g.zLogin, -1, SQLITE_TRANSIENT);
    }else{
      sqlite3_bind_text(pStmt, i, P(zVar+1), -1, SQLITE_TRANSIENT);
    }
  }
  nCol = sqlite3_column_count(pStmt);
  azVals = fossil_malloc(2*nCol*sizeof(const char*) + 1);
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){
    if( azCols==0 ){
      azCols = &azVals[nCol];
      for(i=0; i<nCol; i++){
        azCols[i] = sqlite3_column_name(pStmt, i);
      }
    }
    for(i=0; i<nCol; i++){
      azVals[i] = (const char *)sqlite3_column_text(pStmt, i);
    }
    if( xCallback(pArg, nCol, azVals, azCols) ){
      break;
    }
  }
  rc = sqlite3_finalize(pStmt);
  fossil_free((void *)azVals);
  return rc;
}

/*
** Output Javascript code that will enables sorting of the table with
** the id zTableId by clicking.
**
** The javascript was originally derived from:
**
**     http://www.webtoolkit.info/sortable-html-table.html
**
** But there have been extensive modifications.
**
** This variation allows column types to be expressed using the second
** argument.  Each character of the second argument represent a column.
**
**       t      Sort by text
**       n      Sort numerically
**       k      Sort by the data-sortkey property
**       x      This column is not sortable
**
** Capital letters mean sort in reverse order.
** If there are fewer characters in zColumnTypes[] than their are columns,
** then all extra columns assume type "t" (text).
**
** The third parameter is the column that was initially sorted (using 1-based
** column numbers, like SQL).  Make this value 0 if none of the columns are
** initially sorted.  Make the value negative if the column is initially sorted
** in reverse order.
**
** Clicking on the same column header twice in a row inverts the sort.
*/
void output_table_sorting_javascript(
  const char *zTableId,      /* ID of table to sort */
  const char *zColumnTypes,  /* String for column types */
  int iInitSort              /* Initially sorted column. Leftmost is 1. 0 for NONE */
){
  @ <script>
  @ function SortableTable(tableEl,columnTypes,initSort){
  @   this.tbody = tableEl.getElementsByTagName('tbody');
  @   this.columnTypes = columnTypes;
  @   var ncols = tableEl.rows[0].cells.length;
  @   for(var i = columnTypes.length; i<=ncols; i++){this.columnTypes += 't';}
  @   this.sort = function (cell) {
  @     var column = cell.cellIndex;
  @     var sortFn;
  @     switch( cell.sortType ){
  if( strchr(zColumnTypes,'n') ){
    @       case "n": sortFn = this.sortNumeric;  break;
  }
  if( strchr(zColumnTypes,'N') ){
    @       case "N": sortFn = this.sortReverseNumeric;  break;
  }
  @       case "t": sortFn = this.sortText;  break;
  if( strchr(zColumnTypes,'T') ){
    @       case "T": sortFn = this.sortReverseText;  break;
  }
  if( strchr(zColumnTypes,'k') ){
    @       case "k": sortFn = this.sortKey;  break;
  }
  if( strchr(zColumnTypes,'K') ){
    @       case "K": sortFn = this.sortReverseKey;  break;
  }
  @       default:  return;
  @     }
  @     this.sortIndex = column;
  @     var newRows = new Array();
  @     for (j = 0; j < this.tbody[0].rows.length; j++) {
  @        newRows[j] = this.tbody[0].rows[j];
  @     }
  @     if( this.sortIndex==Math.abs(this.prevColumn)-1 ){
  @       newRows.reverse();
  @       this.prevColumn = -this.prevColumn;
  @     }else{
  @       newRows.sort(sortFn);
  @       this.prevColumn = this.sortIndex+1;
  @     }
  @     for (i=0;i<newRows.length;i++) {
  @       this.tbody[0].appendChild(newRows[i]);
  @     }
  @     this.setHdrIcons();
  @   }
  @   this.setHdrIcons = function() {
  @     for (var i=0; i<this.hdrRow.cells.length; i++) {
  @       if( this.columnTypes[i]=='x' ) continue;
  @       var sortType;
  @       if( this.prevColumn==i+1 ){
  @         sortType = 'asc';
  @       }else if( this.prevColumn==(-1-i) ){
  @         sortType = 'desc'
  @       }else{
  @         sortType = 'none';
  @       }
  @       var hdrCell = this.hdrRow.cells[i];
  @       var clsName = hdrCell.className.replace(/\s*\bsort\s*\w+/, '');
  @       clsName += ' sort ' + sortType;
  @       hdrCell.className = clsName;
  @     }
  @   }
  @   this.sortText = function(a,b) {
  @     var i = thisObject.sortIndex;
  @     aa = a.cells[i].textContent.replace(/^\W+/,'').toLowerCase();
  @     bb = b.cells[i].textContent.replace(/^\W+/,'').toLowerCase();
  @     if(aa<bb) return -1;
  @     if(aa==bb) return a.rowIndex-b.rowIndex;
  @     return 1;
  @   }
  if( strchr(zColumnTypes,'T') ){
    @   this.sortReverseText = function(a,b) {
    @     var i = thisObject.sortIndex;
    @     aa = a.cells[i].textContent.replace(/^\W+/,'').toLowerCase();
    @     bb = b.cells[i].textContent.replace(/^\W+/,'').toLowerCase();
    @     if(aa<bb) return +1;
    @     if(aa==bb) return a.rowIndex-b.rowIndex;
    @     return -1;
    @   }
  }
  if( strchr(zColumnTypes,'n') ){
    @   this.sortNumeric = function(a,b) {
    @     var i = thisObject.sortIndex;
    @     aa = parseFloat(a.cells[i].textContent);
    @     if (isNaN(aa)) aa = 0;
    @     bb = parseFloat(b.cells[i].textContent);
    @     if (isNaN(bb)) bb = 0;
    @     if(aa==bb) return a.rowIndex-b.rowIndex;
    @     return aa-bb;
    @   }
  }
  if( strchr(zColumnTypes,'N') ){
    @   this.sortReverseNumeric = function(a,b) {
    @     var i = thisObject.sortIndex;
    @     aa = parseFloat(a.cells[i].textContent);
    @     if (isNaN(aa)) aa = 0;
    @     bb = parseFloat(b.cells[i].textContent);
    @     if (isNaN(bb)) bb = 0;
    @     if(aa==bb) return a.rowIndex-b.rowIndex;
    @     return bb-aa;
    @   }
  }
  if( strchr(zColumnTypes,'k') ){
    @   this.sortKey = function(a,b) {
    @     var i = thisObject.sortIndex;
    @     aa = a.cells[i].getAttribute("data-sortkey");
    @     bb = b.cells[i].getAttribute("data-sortkey");
    @     if(aa<bb) return -1;
    @     if(aa==bb) return a.rowIndex-b.rowIndex;
    @     return 1;
    @   }
  }
  if( strchr(zColumnTypes,'K') ){
    @   this.sortReverseKey = function(a,b) {
    @     var i = thisObject.sortIndex;
    @     aa = a.cells[i].getAttribute("data-sortkey");
    @     bb = b.cells[i].getAttribute("data-sortkey");
    @     if(aa<bb) return +1;
    @     if(aa==bb) return a.rowIndex-b.rowIndex;
    @     return -1;
    @   }
  }
  @   var x = tableEl.getElementsByTagName('thead');
  @   if(!(this.tbody && this.tbody[0].rows && this.tbody[0].rows.length>0)){
  @     return;
  @   }
  @   if(x && x[0].rows && x[0].rows.length > 0) {
  @     this.hdrRow = x[0].rows[0];
  @   } else {
  @     return;
  @   }
  @   var thisObject = this;
  @   this.prevColumn = initSort;
  @   for (var i=0; i<this.hdrRow.cells.length; i++) {
  @     if( columnTypes[i]=='x' ) continue;
  @     var hdrcell = this.hdrRow.cells[i];
  @     hdrcell.sTable = this;
  @     hdrcell.style.cursor = "pointer";
  @     hdrcell.sortType = columnTypes[i] || 't';
  @     hdrcell.onclick = function () {
  @       this.sTable.sort(this);
  @       return false;
  @     }
  @   }
  @   this.setHdrIcons()
  @ }
  @ var t = new SortableTable(gebi("%s(zTableId)"),"%s(zColumnTypes)",%d(iInitSort));
  @ </script>
}


/*
** WEBPAGE: rptview
**
** Generate a report.  The rn query parameter is the report number
** corresponding to REPORTFMT.RN.  If the tablist query parameter exists,
** then the output consists of lines of tab-separated fields instead of
** an HTML table.
*/
void rptview_page(void){
  int count = 0;
  int rn, rc;
  char *zSql;
  char *zTitle;
  char *zOwner;
  char *zClrKey;
  int tabs;
  Stmt q;
  char *zErr1 = 0;
  char *zErr2 = 0;

  login_check_credentials();
  if( !g.perm.RdTkt ){ login_needed(g.anon.RdTkt); return; }
  tabs = P("tablist")!=0;
  db_prepare(&q,
    "SELECT title, sqlcode, owner, cols, rn FROM reportfmt WHERE rn=%d",
     atoi(PD("rn","0")));
  rc = db_step(&q);
  if( rc!=SQLITE_ROW ){
    db_finalize(&q);
    db_prepare(&q,
      "SELECT title, sqlcode, owner, cols, rn FROM reportfmt WHERE title GLOB %Q",
      P("title"));
    rc = db_step(&q);
  }
  if( rc!=SQLITE_ROW ){
    db_finalize(&q);
    cgi_redirect("reportlist");
    return;
  }
  zTitle = db_column_malloc(&q, 0);
  zSql = db_column_malloc(&q, 1);
  zOwner = db_column_malloc(&q, 2);
  zClrKey = db_column_malloc(&q, 3);
  rn = db_column_int(&q,4);
  db_finalize(&q);

  if( P("order_by") ){
    /*
    ** If the user wants to do a column sort, wrap the query into a sub
    ** query and then sort the results. This is a whole lot easier than
    ** trying to insert an ORDER BY into the query itself, especially
    ** if the query is already ordered.
    */
    int nField = atoi(P("order_by"));
    if( nField > 0 ){
      const char* zDir = PD("order_dir","");
      zDir = !strcmp("ASC",zDir) ? "ASC" : "DESC";
      zSql = mprintf("SELECT * FROM (%s) ORDER BY %d %s", zSql, nField, zDir);
    }
  }

  count = 0;
  if( !tabs ){
    struct GenerateHTML sState = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    db_multi_exec("PRAGMA empty_result_callbacks=ON");
    style_submenu_element("Raw", "rptview?tablist=1&%h", PD("QUERY_STRING",""));
    if( g.perm.Admin
       || (g.perm.TktFmt && g.zLogin && fossil_strcmp(g.zLogin,zOwner)==0) ){
      style_submenu_element("Edit", "rptedit?rn=%d", rn);
    }
    if( g.perm.TktFmt ){
      style_submenu_element("SQL", "rptsql?rn=%d",rn);
    }
    if( g.perm.NewTkt ){
      style_submenu_element("New Ticket", "%s/tktnew", g.zTop);
    }
    style_header("%s", zTitle);
    output_color_key(zClrKey, 1,
        "border=\"0\" cellpadding=\"3\" cellspacing=\"0\" class=\"report\"");
    @ <table border="1" cellpadding="2" cellspacing="0" class="report"
    @  id="reportTable">
    sState.rn = rn;
    sState.nCount = 0;
    report_restrict_sql(&zErr1);
    db_exec_readonly(g.db, zSql, generate_html, &sState, &zErr2);
    report_unrestrict_sql();
    @ </tbody></table>
    if( zErr1 ){
      @ <p class="reportError">Error: %h(zErr1)</p>
    }else if( zErr2 ){
      @ <p class="reportError">Error: %h(zErr2)</p>
    }
    output_table_sorting_javascript("reportTable","",0);
    style_footer();
  }else{
    report_restrict_sql(&zErr1);
    db_exec_readonly(g.db, zSql, output_tab_separated, &count, &zErr2);
    report_unrestrict_sql();
    cgi_set_content_type("text/plain");
  }
}

/*
** report number for full table ticket export
*/
static const char zFullTicketRptRn[] = "0";

/*
** report title for full table ticket export
*/
static const char zFullTicketRptTitle[] = "full ticket export";

/*
** show all reports, which can be used for ticket show.
** Output is written to stdout as tab delimited table
*/
void rpt_list_reports(void){
  Stmt q;
  fossil_print("Available reports:\n");
  fossil_print("%s\t%s\n","report number","report title");
  fossil_print("%s\t%s\n",zFullTicketRptRn,zFullTicketRptTitle);
  db_prepare(&q,"SELECT rn,title FROM reportfmt ORDER BY rn");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zRn = db_column_text(&q, 0);
    const char *zTitle = db_column_text(&q, 1);

    fossil_print("%s\t%s\n",zRn,zTitle);
  }
  db_finalize(&q);
}

/*
** user defined separator used by ticket show command
*/
static const char *zSep = 0;

/*
** select the quoting algorithm for "ticket show"
*/
#if INTERFACE
typedef enum eTktShowEnc { tktNoTab=0, tktFossilize=1 } tTktShowEncoding;
#endif
static tTktShowEncoding tktEncode = tktNoTab;

/*
** Output the text given in the argument.  Convert tabs and newlines into
** spaces.
*/
static void output_no_tabs_file(const char *z){
  switch( tktEncode ){
    case tktFossilize:
      { char *zFosZ;

        if( z && *z ){
          zFosZ = fossilize(z,-1);
          fossil_print("%s",zFosZ);
          free(zFosZ);
        }
        break;
      }
    default:
      while( z && z[0] ){
        int i, j;
        for(i=0; z[i] && (!fossil_isspace(z[i]) || z[i]==' '); i++){}
        if( i>0 ){
          fossil_print("%.*s", i, z);
        }
        for(j=i; fossil_isspace(z[j]); j++){}
        if( j>i ){
          fossil_print("%*s", j-i, "");
        }
        z += j;
      }
      break;
  }
}

/*
** Output a row as a tab-separated line of text.
*/
int output_separated_file(
  void *pUser,     /* Pointer to row-count integer */
  int nArg,        /* Number of columns in this result row */
  const char **azArg, /* Text of data in all columns */
  const char **azName /* Names of the columns */
){
  int *pCount = (int*)pUser;
  int i;

  if( *pCount==0 ){
    for(i=0; i<nArg; i++){
      output_no_tabs_file(azName[i]);
      fossil_print("%s", i<nArg-1 ? (zSep?zSep:"\t") : "\n");
    }
  }
  ++*pCount;
  for(i=0; i<nArg; i++){
    output_no_tabs_file(azArg[i]);
    fossil_print("%s", i<nArg-1 ? (zSep?zSep:"\t") : "\n");
  }
  return 0;
}

/*
** Generate a report.  The rn query parameter is the report number.
** The output is written to stdout as flat file. The zFilter parameter
** is a full WHERE-condition.
*/
void rptshow(
    const char *zRep,
    const char *zSepIn,
    const char *zFilter,
    tTktShowEncoding enc
){
  Stmt q;
  char *zSql;
  char *zErr1 = 0;
  char *zErr2 = 0;
  int count = 0;
  int rn;

  if( !zRep || !strcmp(zRep,zFullTicketRptRn) || !strcmp(zRep,zFullTicketRptTitle) ){
    zSql = "SELECT * FROM ticket";
  }else{
    rn = atoi(zRep);
    if( rn ){
      db_prepare(&q,
       "SELECT sqlcode FROM reportfmt WHERE rn=%d", rn);
    }else{
      db_prepare(&q,
       "SELECT sqlcode FROM reportfmt WHERE title=%Q", zRep);
    }
    if( db_step(&q)!=SQLITE_ROW ){
      db_finalize(&q);
      rpt_list_reports();
      fossil_fatal("unknown report format(%s)!",zRep);
    }
    zSql = db_column_malloc(&q, 0);
    db_finalize(&q);
  }
  if( zFilter ){
    zSql = mprintf("SELECT * FROM (%s) WHERE %s",zSql,zFilter);
  }
  count = 0;
  tktEncode = enc;
  zSep = zSepIn;
  report_restrict_sql(&zErr1);
  db_exec_readonly(g.db, zSql, output_separated_file, &count, &zErr2);
  report_unrestrict_sql();
  if( zFilter ){
    free(zSql);
  }
}
