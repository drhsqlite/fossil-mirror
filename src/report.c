/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License as published by the Free Software Foundation; either
** version 2 of the License, or (at your option) any later version.
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
** Code to generate the bug report listings
*/
#include "config.h"
#include "report.h"
#include <assert.h>

/* Forward references to static routines */
static void report_format_hints(void);

/*
** WEBPAGE: /reportlist
*/
void view_list(void){
  Stmt q;
  int rn = 0;
  int cnt = 0;

  login_check_credentials();
  if( !g.okRdTkt && !g.okNewTkt ){ login_needed(); return; }
  style_header("Bug Report Main Menu");
  if( g.okNewTkt ){
    @ <p>Enter a new bug report:</p>
    @ <ol><li value="1"><a href="tktnew">New bug report</a></li></ol>
    @
    cnt++;
  }
  if( !g.okRdTkt ){
    @ <p>You are not authorized to view existing bug reports.</p>
  }else{
    db_prepare(&q, "SELECT rn, title, owner FROM reportfmt ORDER BY title");
    @ <p>Choose a report format from the following list:</p>
    @ <ol>
    while( db_step(&q)==SQLITE_ROW ){
      const char *zTitle = db_column_text(&q, 1);
      const char *zOwner = db_column_text(&q, 2);
      rn = db_column_int(&q, 0);
      cnt++;
      @ <li value="%d(cnt)"><a href="rptview?rn=%d(rn)"
      @        rel="nofollow">%h(zTitle)</a>&nbsp;&nbsp;&nbsp;
      if( g.okWrite && zOwner && zOwner[0] ){
        @ (by <i>%h(zOwner)</i>)
      }
      if( g.okTktFmt ){
        @ [<a href="rptedit?rn=%d(rn)&amp;copy=1" rel="nofollow">copy</a>]
      }
      if( g.okAdmin || (g.okWrTkt && zOwner && strcmp(g.zLogin,zOwner)==0) ){
        @ [<a href="rptedit?rn=%d(rn)" rel="nofollow">edit</a>]
      }
      if( g.okTktFmt ){
        @ [<a href="rptsql?rn=%d(rn)" rel="nofollow">sql</a>]
      }
      @ </li>
    }
  }
  @ </ol>
  if( g.okTktFmt ){
    @ <p>Create a new bug report display format:</p>
    @ <ol>
    @ <li value="%d(cnt+1)"><a href="rptnew">New report format</a></li>
    @ </ol>
  }
  style_footer();
}

/*
** Remove whitespace from both ends of a string.
*/
char *trim_string(const char *zOrig){
  int i;
  while( isspace(*zOrig) ){ zOrig++; }
  i = strlen(zOrig);
  while( i>0 && isspace(zOrig[i-1]) ){ i--; }
  return mprintf("%.*s", i, zOrig);
}

/*
** Extract a numeric (integer) value from a string.
*/
char *extract_integer(const char *zOrig){
  if( zOrig == NULL || zOrig[0] == 0 ) return "";
  while( *zOrig && !isdigit(*zOrig) ){ zOrig++; }
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
** all whitespace from the end. Removes whitespace preceeding a NL,
** which also converts any CRNL sequence into a single NL.
*/
char *remove_blank_lines(const char *zOrig){
  int i, j, n;
  char *z;
  for(i=j=0; isspace(zOrig[i]); i++){ if( zOrig[i]=='\n' ) j = i+1; }
  n = strlen(&zOrig[j]);
  while( n>0 && isspace(zOrig[j+n-1]) ){ n--; }
  z = mprintf("%.*s", n, &zOrig[j]);
  for(i=j=0; z[i]; i++){
    if( z[i+1]=='\n' && z[i]!='\n' && isspace(z[i]) ){
      z[j] = z[i];
      while(isspace(z[j]) && z[j] != '\n' ){ j--; }
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
  char *zError = *(char**)pError;
  if( zError ){
    /* We've already seen an error.  No need to continue. */
    return SQLITE_OK;
  }
  switch( code ){
    case SQLITE_SELECT:
    case SQLITE_FUNCTION: {
      break;
    }
    case SQLITE_READ: {
      static const char *azAllowed[] = {
         "ticket",
         "blob",
         "filename",
         "mlink",
         "plink",
         "event",
         "tag",
         "tagxref",
      };
      int i;
      for(i=0; i<sizeof(azAllowed)/sizeof(azAllowed[0]); i++){
        if( strcasecmp(zArg1, azAllowed[i])==0 ) break;
      }
      if( i>=sizeof(azAllowed)/sizeof(azAllowed[0]) ){
        zError = mprintf("cannot access table %s", zArg1);
      }
      break;
    }
    default: {
      zError = mprintf("only SELECT statements are allowed");
      break;
    }
  }
  return SQLITE_OK;
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
  ** the first token is "SELECT" and that there are no unquoted semicolons.
  */
  for(i=0; isspace(zSql[i]); i++){}
  if( strncasecmp(&zSql[i],"select",6)!=0 ){
    return mprintf("The SQL must be a SELECT statement");
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
  sqlite3_set_authorizer(g.db, report_query_authorizer, (void*)&zErr);
  rc = sqlite3_prepare(g.db, zSql, -1, &pStmt, &zTail);
  if( rc!=SQLITE_OK ){
    zErr = mprintf("Syntax error: %s", sqlite3_errmsg(g.db));
  }
  if( pStmt ){
    sqlite3_finalize(pStmt);
  }
  sqlite3_set_authorizer(g.db, 0, 0);
  return zErr;
}

/*
** WEBPAGE: /rptsql
*/
void view_see_sql(void){
  int rn;
  const char *zTitle;
  const char *zSQL;
  const char *zOwner;
  const char *zClrKey;
  Stmt q;

  login_check_credentials();
  if( !g.okTktFmt ){
    login_needed();
    return;
  }
  rn = atoi(PD("rn","0"));
  db_prepare(&q, "SELECT title, sqlcode, owner, cols "
                   "FROM reportfmt WHERE rn=%d",rn);
  style_header("SQL For Report Format Number %d", rn);
  if( db_step(&q)!=SQLITE_ROW ){
    @ <p>Unknown report number: %d(rn)</p>
    style_footer();
    return;
  }
  zTitle = db_column_text(&q, 0);
  zSQL = db_column_text(&q, 1);
  zOwner = db_column_text(&q, 2);
  zClrKey = db_column_text(&q, 3);
  @ <table cellpadding=0 cellspacing=0 border=0>
  @ <tr><td valign="top" align="right">Title:</td><td width=15></td>
  @ <td colspan=3>%h(zTitle)</td></tr>
  @ <tr><td valign="top" align="right">Owner:</td><td></td>
  @ <td colspan=3>%h(zOwner)</td></tr>
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
}

/*
** WEBPAGE: /rptnew
** WEBPAGE: /rptedit
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
  if( !g.okTktFmt ){
    login_needed();
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
    db_multi_exec("DELETE FROM reportfmt WHERE rn=%d", rn);
    cgi_redirect("reportlist");
    return;
  }else if( rn>0 && P("del1") ){
    zTitle = db_text(0, "SELECT title FROM reportfmt "
                         "WHERE rn=%d", rn);
    if( zTitle==0 ) cgi_redirect("reportlist");

    style_header("Are You Sure?");
    @ <form action="rptedit" method="POST">
    @ <p>You are about to delete all traces of the report
    @ <strong>%h(zTitle)</strong> from
    @ the database.  This is an irreversible operation.  All records
    @ related to this report will be removed and cannot be recovered.</p>
    @
    @ <input type="hidden" name="rn" value="%d(rn)">
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
    if( zErr==0 ){
      if( rn>0 ){
        db_multi_exec("UPDATE reportfmt SET title=%Q, sqlcode=%Q,"
                      " owner=%Q, cols=%Q WHERE rn=%d",
           zTitle, zSQL, zOwner, zClrKey, rn);
      }else{
        db_multi_exec("INSERT INTO reportfmt(title,sqlcode,owner,cols) "
           "VALUES(%Q,%Q,%Q,%Q)",
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
  style_submenu_element("Cancel", "Cancel", "reportlist");
  if( rn>0 ){
    style_submenu_element("Delete", "Delete", "rptedit?rn=%d&del1=1", rn);
  }
  style_header(rn>0 ? "Edit Report Format":"Create New Report Format");
  if( zErr ){
    @ <blockquote><font color="#ff0000"><b>%h(zErr)</b></font></blockquote>
  }
  @ <form action="rptedit" method="POST">
  @ <input type="hidden" name="rn" value="%d(rn)">
  @ <p>Report Title:<br>
  @ <input type="text" name="t" value="%h(zTitle)" size="60"></p>
  @ <p>Enter a complete SQL query statement against the "TICKET" table:<br>
  @ <textarea name="s" rows="20" cols="80">%h(zSQL)</textarea>
  @ </p>
  if( g.okAdmin ){
    @ <p>Report owner:
    @ <input type="text" name="w" size="20" value="%h(zOwner)">
    @ </p>
  } else {
    @ <input type="hidden" name="w" value="%h(zOwner)">
  }
  @ <p>Enter an optional color key in the following box.  (If blank, no
  @ color key is displayed.)  Each line contains the text for a single
  @ entry in the key.  The first token of each line is the background
  @ color for that line.<br>
  @ <textarea name="k" rows="8" cols="50">%h(zClrKey)</textarea>
  @ </p>
  if( !g.okAdmin && strcmp(zOwner,g.zLogin)!=0 ){
    @ <p>This report format is owned by %h(zOwner).  You are not allowed
    @ to change it.</p>
    @ </form>
    report_format_hints();
    style_footer();
    return;
  }
  @ <input type="submit" value="Apply Changes">
  if( rn>0 ){
    @ <input type="submit" value="Delete This Report" name="del1">
  }
  @ </form>
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
  @ <hr><h3>TICKET Schema</h3>
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
  @ <li><p>The <b>user()</b> SQL function returns a string
  @ which is the login of the current user.</p></li>
  @
  @ <li><p>The first column whose name begins with underscore ("_") and all
  @ subsequent columns are shown on their own rows in the table.  This might
  @ be useful for displaying the description of tickets.
  @ </p></li>
  @
  @ <li><p>The <b>aux()</b> SQL function takes a parameter name as an argument
  @ and returns the value that the user enters in the resulting HTML form. A
  @ second optional parameter provides a default value for the field.</p></li>
  @
  @ <li><p>The <b>option()</b> SQL function takes a parameter name
  @ and a quoted SELECT statement as parameters. The query results are
  @ presented as an HTML dropdown menu and the function returns
  @ the currently selected value. Results may be a single value column or
  @ two <b>value,description</b> columns. The first row is the default.</p></li>
  @
  @ <li><p>The <b>cgi()</b> SQL function takes a parameter name as an argument
  @ and returns the value of a corresponding CGI query value. If the CGI
  @ parameter doesn't exist, an optional second argument will be returned
  @ instead.</p></li>
  @
  @ <li><p>If a column is wrapped by the <b>wiki()</b> SQL function, it will
  @ be rendered as wiki formatted content.</p></li>
  @
  @ <li><p>If a column is wrapped by the <b>tkt()</b> SQL function, it will
  @ be shown as a ticket id with a link to the appropriate page</p></li>
  @
  @ <li><p>If a column is wrapped by the <b>chng()</b> SQL function, it will
  @ be shown as a baseline id with a link to the appropriate page.</p></li>
  @
  @ <li><p>The <b>search()</b> SQL function takes a keyword pattern and
  @ a search text. The function returns an integer score which is
  @ higher depending on how well the search went.</p></li>
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
  @ <table align="right" style="margin: 0 5px;" border=1 cellspacing=0 width=125>
  @ <tr bgcolor="#f2dcdc"><td align="center">new or active</td></tr>
  @ <tr bgcolor="#e8e8bd"><td align="center">review</td></tr>
  @ <tr bgcolor="#cfe8bd"><td align="center">fixed</td></tr>
  @ <tr bgcolor="#bde5d6"><td align="center">tested</td></tr>
  @ <tr bgcolor="#cacae5"><td align="center">defer</td></tr>
  @ <tr bgcolor="#c8c8c8"><td align="center">closed</td></tr>
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
  @ <table align="right" style="margin: 0 5px;" border=1 cellspacing=0 width=125>
  @ <tr bgcolor="#f2dcdc"><td align="center">1</td></tr>
  @ <tr bgcolor="#e8e8bd"><td align="center">2</td></tr>
  @ <tr bgcolor="#cfe8bd"><td align="center">3</td></tr>
  @ <tr bgcolor="#cacae5"><td align="center">4</td></tr>
  @ <tr bgcolor="#c8c8c8"><td align="center">5</td></tr>
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
  @    description AS '_Description',   -- When the column name begins with '_'
  @    remarks AS '_Remarks'            -- the data is shown on a separate row.
  @  FROM ticket
  @ </pre></blockquote>
  @
  @ <p>Or, to see part of the description on the same row, use the
  @ <b>wiki()</b> function with some string manipulation. Using the
  @ <b>tkt()</b> function on the ticket number will also generate a linked
  @ field, but without the extra <i>edit</i> column:
  @ </p>
  @ <blockquote><pre>
  @  SELECT
  @    tkt(tn) AS '',
  @    title AS 'Title',
  @    wiki(substr(description,0,80)) AS 'Description'
  @  FROM ticket
  @ </pre></blockquote>
  @
}

static void column_header(int rn,const char *zCol, int nCol, int nSorted,
    const char *zDirection, const char *zExtra
){
  int set = (nCol==nSorted);
  int desc = !strcmp(zDirection,"DESC");

  /*
  ** Clicking same column header 3 times in a row resets any sorting.
  ** Note that we link to rptview, which means embedded reports will get
  ** sent to the actual report view page as soon as a user tries to do
  ** any sorting. I don't see that as a Bad Thing.
  */
  if(set && desc){
    @ <th bgcolor="%s(BG1)" class="bkgnd1">
    @   <a href="rptview?rn=%d(rn)%s(zExtra)">%h(zCol)</a></th>
  }else{
    if(set){
      @ <th bgcolor="%s(BG1)" class="bkgnd1"><a
    }else{
      @ <th><a
    }
    @ href="rptview?rn=%d(rn)&amp;order_by=%d(nCol)&amp;\
    @ order_dir=%s(desc?"ASC":"DESC")\
    @ %s(zExtra)">%h(zCol)</a></th>
  }
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
};

/*
** The callback function for db_query
*/
static int generate_html(
  void *pUser,     /* Pointer to output state */
  int nArg,        /* Number of columns in this result row */
  char **azArg,    /* Text of data in all columns */
  char **azName    /* Names of the columns */
){
  struct GenerateHTML *pState = (struct GenerateHTML*)pUser;
  int i;
  const char *zTid;  /* Ticket UUID.  (value of column named '#') */
  int rn;            /* Report number */
  char *zBg = 0;     /* Use this background color */
  char zPage[30];    /* Text version of the ticket number */

  /* Get the report number
  */
  rn = pState->rn;

  /* Do initialization
  */
  if( pState->nCount==0 ){
    /* Figure out the number of columns, the column that determines background
    ** color, and whether or not this row of data is represented by multiple
    ** rows in the table.
    */
    pState->nCol = 0;
    pState->isMultirow = 0;
    pState->iNewRow = -1;
    pState->iBg = -1;
    for(i=0; i<nArg; i++){
      if( azName[i][0]=='b' && strcmp(azName[i],"bgcolor")==0 ){
        pState->iBg = i;
        continue;
      }
      if( g.okWrite && azName[i][0]=='#' ){
        pState->nCol++;
      }
      if( !pState->isMultirow ){
        if( azName[i][0]=='_' ){
          pState->isMultirow = 1;
          pState->iNewRow = i;
        }else{
          pState->nCol++;
        }
      }
    }

    /* The first time this routine is called, output a table header
    */
    @ <tr>
    zTid = 0;
    for(i=0; i<nArg; i++){
      char *zName = azName[i];
      if( i==pState->iBg ) continue;
      if( pState->iNewRow>=0 && i>=pState->iNewRow ){
        if( g.okWrite && zTid ){
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
    if( g.okWrite && zTid ){
      @ <th>&nbsp;</th>
    }
    @ </tr>
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
  @ <tr bgcolor="%h(zBg)">
  zTid = 0;
  zPage[0] = 0;
  for(i=0; i<nArg; i++){
    char *zData;
    if( i==pState->iBg ) continue;
    zData = azArg[i];
    if( zData==0 ) zData = "";
    if( pState->iNewRow>=0 && i>=pState->iNewRow ){
      if( zTid && g.okWrite ){
        @ <td valign="top"><a href="tktedit/%h(zTid)">edit</a></td>
        zTid = 0;
      }
      if( zData[0] ){
        Blob content;
        @ </tr><tr bgcolor="%h(zBg)"><td colspan=%d(pState->nCol)>
        blob_init(&content, zData, -1);
        wiki_convert(&content, 0, 0);
        blob_reset(&content);
      }
    }else if( azName[i][0]=='#' ){
      zTid = zData;
      @ <td valign="top"><a href="tktview?name=%h(zData)">%h(zData)</a></td>
    }else if( zData[0]==0 ){
      @ <td valign="top">&nbsp;</td>
    }else{
      @ <td valign="top">
      @ %h(zData)
      @ </td>
    }
  }
  if( zTid && g.okWrite ){
    @ <td valign="top"><a href="tktedit/%h(zTid)">edit</a></td>
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
    for(i=0; z[i] && (!isspace(z[i]) || z[i]==' '); i++){}
    if( i>0 ){
      cgi_printf("%.*s", i, z);
    }
    for(j=i; isspace(z[j]); j++){}
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
  char **azArg,    /* Text of data in all columns */
  char **azName    /* Names of the columns */
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
  char *zSafeKey, *zToFree;
  while( isspace(*zClrKey) ) zClrKey++;
  if( zClrKey[0]==0 ) return;
  @ <table %s(zTabArgs)>
  if( horiz ){
    @ <tr>
  }
  zToFree = zSafeKey = mprintf("%h", zClrKey);
  while( zSafeKey[0] ){
    while( isspace(*zSafeKey) ) zSafeKey++;
    for(i=0; zSafeKey[i] && !isspace(zSafeKey[i]); i++){}
    for(j=i; isspace(zSafeKey[j]); j++){}
    for(k=j; zSafeKey[k] && zSafeKey[k]!='\n' && zSafeKey[k]!='\r'; k++){}
    if( !horiz ){
      cgi_printf("<tr bgcolor=\"%.*s\"><td>%.*s</td></tr>\n",
        i, zSafeKey, k-j, &zSafeKey[j]);
    }else{
      cgi_printf("<td bgcolor=\"%.*s\">%.*s</td>\n",
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
** WEBPAGE: /rptview
**
** Generate a report.  The rn query parameter is the report number
** corresponding to REPORTFMT.RN.  If the tablist query parameter exists,
** then the output consists of lines of tab-separated fields instead of
** an HTML table.
*/
void rptview_page(void){
  int count = 0;
  int rn;
  char *zSql;
  char *zTitle;
  char *zOwner;
  char *zClrKey;
  int tabs;
  Stmt q;

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  rn = atoi(PD("rn","0"));
  if( rn==0 ){
    cgi_redirect("reportlist");
    return;
  }
  tabs = P("tablist")!=0;
  /* view_add_functions(tabs); */
  db_prepare(&q,
    "SELECT title, sqlcode, owner, cols FROM reportfmt WHERE rn=%d", rn);
  if( db_step(&q)!=SQLITE_ROW ){
    cgi_redirect("reportlist");
    return;
  }
  zTitle = db_column_malloc(&q, 0);
  zSql = db_column_malloc(&q, 1);
  zOwner = db_column_malloc(&q, 2);
  zClrKey = db_column_malloc(&q, 3);
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
    struct GenerateHTML sState;

    db_multi_exec("PRAGMA empty_result_callbacks=ON");
    style_submenu_element("Raw", "Raw", 
      "rptview?tablist=1&%s", PD("QUERY_STRING",""));
    if( g.okAdmin 
       || (g.okTktFmt && g.zLogin && zOwner && strcmp(g.zLogin,zOwner)==0) ){
      style_submenu_element("Edit", "Edit", "rptedit?rn=%d", rn);
    }
    if( g.okTktFmt ){
      style_submenu_element("SQL", "SQL", "rptsql?rn=%d",rn);
    }
    style_header(zTitle);
    output_color_key(zClrKey, 1, 
        "border=0 cellpadding=3 cellspacing=0 class=\"report\"");
    @ <table border=1 cellpadding=2 cellspacing=0 class="report">
    sState.rn = rn;
    sState.nCount = 0;
    sqlite3_exec(g.db, zSql, generate_html, &sState, 0);
    @ </table>
    style_footer();
  }else{
    sqlite3_exec(g.db, zSql, output_tab_separated, &count, 0);
    cgi_set_content_type("text/plain");
  }
}
