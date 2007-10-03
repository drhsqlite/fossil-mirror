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
** This file contains code used to parser ticket configuration
** artificates.
*/
#include "config.h"
#include "tktconf.h"
#include <assert.h>

/*
** Return TRUE if the given token is a valid field name for
** the ticket table.  The name must be all letters, digits, 
** and underscores.
*/
static int is_valid_name(Blob *p){
  const char *z = blob_buffer(p);
  int n = blob_size(p);
  int i;

  for(i=0; i<n; i++){
    if( !isalnum(z[i]) && z[i]!='_' ){
      return 0;
    }
  }
  return 1;
}

/*
** Return TRUE if the given token is a valid enumeration value.
** The token must consist of the following characters:
**
** a-zA-Z0-9_%/.-
*/
static int is_valid_enum(Blob *p){
  const char *z = blob_buffer(p);
  int n = blob_size(p);
  int i;

  for(i=0; i<n; i++){
    int c = z[i];
    if( !isalnum(c) && c!='_' && c!='%' && c!='/' && c!='.' && c!='-' ){
      return 0;
    }
  }
  return 1;
}


/*
** A ticket configuration record is a single artifact that defines
** the ticket configuration for a server.  The file format is as
** follows:
**
**     ticket-configuration
**     field <fieldname> <fieldtype> <width> <param> ...
**     template <type> <delimiter>
**     <text>
**     description <delimiter>
**     <text>
**
** All lines are separated by \n.  Trailing whitespace is
** ignored.  The first line must be "ticket-configuration".
** Subsequent lines are either "field" or "template" lines.
** There must be exactly three template lines and one or more
** field lines (usually more).
**
** Field lines define the fields of the "ticket" table in the
** database.  The fields appear in the table in the order in 
** which they appear in the configuration artifact.  The <fieldname>
** must consist of alphanumerics and underscores.  <fieldtype>
** is one of:  text, ctext, enum, date, uuid, baseline, private.  All
** types have at least a <width> parameter.  Text and Ctext types
** have a height parameter.  Enum has a list of allowed values.
**
** The <type> of a template is one of: new, view, edit.  There must
** be one template of each type.  <delimiter> is an arbitrary string
** that terminates the template.  The body of the template is subsequent
** lines of text up to but not including the <delimiter>.  Trailing
** whitespace on the delimiter is ignored.
**
** There should be one description entry.  The text that follows
** is a human-readable plaintext description of this ticket
** configuration.  The description is visible to the administrator
** and is used to help identify this configuration among several
** options.  The first line of the description is a one-line
** summary.  Subsequent lines are details.
**
** The pConfig parameter is the complete text of the configuration
** file to be parsed.  testFlag is 1 to cause the results to be printed
** on stdout or 0 to cause results to update the database.
**
** Return the number of errors.  If there is an error and pErr!=NULL
** then leave an error message in pErr.  We assume that pErr has already
** been initialized.
*/
int ticket_config_parse(Blob *pConfig, int testFlag, Blob *pErr){
  int rc = 1;
  Blob line;
  Blob token;
  Blob name;
  Blob type;
  Blob arg;
  Blob sql;
  Blob tbldef;
  Blob err;
  int seen_template = 0;
  int lineno = 0;

  blob_zero(&sql);
  blob_zero(&tbldef);
  blob_zero(&token);
  blob_zero(&name);
  blob_zero(&type);
  blob_zero(&arg);
  blob_zero(&err);

  /* The configuration file must begin with a line that
  ** says "ticket-configuration"
  */
  blob_line(pConfig, &line);
  blob_token(&line, &token);
  if( !blob_eq(&token, "ticket-configuration") ){
    blob_appendf(&err, "missing initialization keyword");
    goto bad_config_file;
  }
  lineno++;

  /* Begin accumulating SQL text that will implement the 
  ** ticket configuration.  tbldef will hold the ticket table
  ** definition.  sql will hold text to initialize and define
  ** the tktfield table and to insert template text into the
  ** config table
  */
  blob_appendf(&tbldef, 
     "DROP TABLE IF EXISTS ticket;\n"
     "CREATE TABLE repository.ticket(\n"
     "  tktid INTEGER PRIMARY KEY,\n"
     "  tktuuid TEXT UNIQUE,\n"
     "  starttime DATETIME,\n"
     "  lastmod DATETIME"
  );
  blob_appendf(&sql,
     "DROP TABLE IF EXISTS tktfield;\n"
     "CREATE TABLE repository.tktfield(\n"
     "  fidx INTEGER PRIMARY KEY,\n"
     "  name TEXT UNIQUE,\n"
     "  type TEXT,\n"
     "  width INTEGER,\n"
     "  arg\n"
     ");\n"
  );

  /* Process the remainder of the configuration file (the part that
  ** comes after the "ticket-configuration" header) line by line
  */
  while( blob_line(pConfig, &line) ){
    char *z;
    lineno++;
    if( blob_token(&line, &token)==0 ){
      /* Ignore blank lines */
      continue;
    }
    z = blob_buffer(&token);
    if( z[0]=='#' ){
      /* Ignore comment lines */
      continue;
    }

    /*
    **    field <name> <type> <width> <args...>
    */
    if( blob_eq(&token, "field")
     && blob_token(&line,&name)
     && blob_token(&line,&type)
     && blob_token(&line,&arg)
    ){
      int width;

      if( !is_valid_name(&name) ){
        blob_appendf(&err, "invalid field name: %b", &name);
        goto bad_config_file;
      }
      if( !blob_is_int(&arg, &width) ){
        blob_appendf(&err, "invalid field width: %b", &arg);
        goto bad_config_file;
      }
      if( width<1 || width>200 ){
        blob_appendf(&err, "width less than 1 or greater than 200");
        goto bad_config_file;
      }
      blob_appendf(&tbldef, ",\n  tkt_%b", &name);
      if( blob_eq(&type,"text") || blob_eq(&type,"ctext")
            || blob_eq(&type,"private") ){
        int height;
        if( !blob_token(&line, &arg) || !blob_is_int(&arg, &height) ){
          blob_appendf(&err, "invalid height: %b", &arg);
          goto bad_config_file;
        }
        if( height<1 || height>1000 ){
          blob_appendf(&err, "height less than 1 or greater than 1000");
          goto bad_config_file;
        }
        blob_appendf(&sql,
          "INSERT INTO tktfield(name,type,width,arg)"
          "VALUES('%b','%b',%d,%d);\n",
          &name, &type, width, height
        );
      }else if( blob_eq(&type,"enum") ){
        int cnt = 0;
        const char *zDelim = "'";
        blob_appendf(&sql,
          "INSERT INTO tktfield(name,type,width,arg)"
          "VALUES('%b','%b',%d,",
          &name, &type, width
        );
        while( blob_token(&line, &arg) ){
          if( !is_valid_enum(&arg) ){
            blob_appendf(&err, "invalid enumeration value: %b", &arg);
            goto bad_config_file;
          }
          cnt++;
          blob_appendf(&sql, "%s%b", zDelim, &arg);
          zDelim = " ";
        }
        if( cnt<2 ){
          blob_appendf(&err, "less than 2 enumeration values");
          goto bad_config_file;
        }
        blob_appendf(&sql,"');\n");
      }else if( blob_eq(&type,"uuid") || blob_eq(&type,"baseline") ||
                blob_eq(&type,"date") ){
        blob_appendf(&sql,
          "INSERT INTO tktfield(name,type,width)"
          "VALUES('%b','%b',%d);\n",
          &name, &type, width
        );
      }else{
        blob_appendf(&err, "unknown field type: %b", &type);
        goto bad_config_file;
      }
    }else    
  
    /*
    **  template <type> <delimiter>
    **  <text>
    */      
    if( blob_eq(&token, "template")
     && blob_token(&line, &type)
     && blob_token(&line, &arg)
    ){
      int idx;
      Blob content;
      int start;
      int end;

      if( blob_eq(&type,"new") ){
        idx = 0;
      }else if( blob_eq(&type, "view") ){
        idx = 1;
      }else if( blob_eq(&type, "edit") ){
        idx = 2;
      }else{
        blob_appendf(&err, "unknown template type: %b", &type);
        goto bad_config_file;
      }
      if( (seen_template & (1<<idx))!=0 ){
        blob_appendf(&err, "more than one %b template", &type);
        goto bad_config_file;
      }
      seen_template |= 1<<idx;
      start = end = blob_tell(pConfig);
      while( blob_line(pConfig, &line) ){
         blob_token(&line, &token);
         if( blob_compare(&token, &arg)==0 ) break;
         end = blob_tell(pConfig);
      }
      blob_init(&content, &blob_buffer(pConfig)[start], end - start);
      blob_appendf(&sql, 
        "REPLACE INTO config(name, value) VALUES('tkt-%b-template',%B);\n",
        &type, &content
      );
      blob_reset(&content);
    }else

    /*
    **  description <delimiter>
    **  <text>
    */      
    if( blob_eq(&token, "description")
     && blob_token(&line, &arg)
    ){
      Blob content;
      int start;
      int end;

      start = end = blob_tell(pConfig);
      while( blob_line(pConfig, &line) ){
         blob_token(&line, &token);
         if( blob_compare(&token, &arg)==0 ) break;
         end = blob_tell(pConfig);
      }
      blob_init(&content, &blob_buffer(pConfig)[start], end - start);
      blob_appendf(&sql, 
        "REPLACE INTO config(name, value) VALUES('tkt-desc',%B);\n",
         &content
      );
      blob_reset(&content);
    }else

    {
      blob_appendf(&err, "unknown command: %b", &token);
      goto bad_config_file;
    }
  }
  if( seen_template != 0x7 ){
    blob_appendf(&err, "missing templates");
    goto bad_config_file;
  }

  /* If we reach this point it means the parse was successful
  */
  rc = 0;
  blob_appendf(&tbldef, "\n);\n");
  if( testFlag ){
    blob_write_to_file(&tbldef, "-");
    blob_write_to_file(&sql, "-");
  }else{
    db_multi_exec("%b", &tbldef);
    db_multi_exec("%b", &sql);
  }

bad_config_file:
  if( rc && pErr ){
    blob_appendf(pErr, "line %d: %b", lineno, &err);
  }
  blob_reset(&token);
  blob_reset(&line);
  blob_reset(&name);
  blob_reset(&type);
  blob_reset(&arg);
  blob_reset(&sql);
  blob_reset(&tbldef);
  blob_reset(&err);
  return rc;
}

/*
** COMMAND: test-tktconfig-parse
*/
void test_tktconfig_parse_cmd(void){
  Blob config, err;
  if( g.argc!=3 ){
    usage("FILENAME");
  }
  blob_read_from_file(&config, g.argv[2]);
  blob_zero(&err);
  ticket_config_parse(&config, 1, &err);
  if( blob_size(&err) ){
    blob_write_to_file(&err, "-");
  }
}
/*
** COMMAND: test-tktconfig-import
*/
void test_tktconfig_import_cmd(void){
  Blob config, err;
  db_must_be_within_tree();
  if( g.argc!=3 ){
    usage("FILENAME");
  }
  blob_read_from_file(&config, g.argv[2]);
  blob_zero(&err);
  db_begin_transaction();
  ticket_config_parse(&config, 0, &err);
  db_end_transaction(0);
  if( blob_size(&err) ){
    blob_write_to_file(&err, "-");
  }
}

/*
** Load the default ticket configuration.
*/
void ticket_load_default_config(void){
  static const char zDefaultConfig[] = 
    @ ticket-configuration
    @ description END-OF-DESCRIPTION
    @ Default Ticket Configuration
    @ The default ticket configuration for new projects
    @ END-OF-DESCRIPTION
    @ #####################################################################
    @ field title text 60 1
    @ field comment ctext 80 20
    @ field assignedto text 20 1
    @ field subsystem text 20 1
    @ field type enum 12 Code Build_Problem Documentation Feature_Request Incident
    @ field priority enum 10 High Medium Low
    @ field severity enum 10 Critical Severe Important Minor Cosmetic
    @ field sesolution enum 20 Open Fixed Rejected Unable_To_Reproduce Works_As_Designed External_Bug Not_A_Bug Duplicate Overcome_By_Events Drive_By_Patch
    @ field status enum 10 Open Verified In_Process Deferred Fixed Tested Closed
    @ field contact private 50 1
    @ field foundin text 30 1
    @ field assocvers baseline 50
    @ field presentin uuid 50
    @ field fixedin uuid 50
    @ field dueby date 20
    @ field deferuntil date 20
    @ ######################################################################
    @ template new END-OF-NEW-TEMPLATE
    @ <table cellpadding="5">
    @ <tr>
    @ <td cellpadding="2">
    @ Enter a one-line summary of the problem:<br>
    @ [entrywidget title]
    @ </td>
    @ </tr>
    @ 
    @ <tr>
    @ <td align="right">Type:
    @ [entrywidget type]
    @ </td>
    @ <td>What type of ticket is this?</td>
    @ </tr>
    @ 
    @ <tr>
    @ <td align="right">Version: 
    @ [entrywidget foundin]
    @ </td>
    @ <td>In what version or build number do you observer the problem?</td>
    @ </tr>
    @ 
    @ <tr>
    @ <td align="right">Severity:
    @ [entrywidget severity]
    @ </td>
    @ <td>How debilitating is the problem?  How badly does the problem
    @ effect the operation of the product?</td>
    @ </tr>
    @ 
    @ <tr>
    @ <td colspan="2">
    @ Enter a detailed description of the problem.
    @ For code defects, be sure to provide details on exactly how
    @ the problem can be reproduced.  Provide as much detail as
    @ possible.
    @ <br>
    @ [entrywidget comment noappend]
    @ [ifpreview comment]
    @ <hr>
    @ [viewwidget comment]
    @ </hr>
    @ </tr>
    @ 
    @ <tr>
    @ <td align="right">
    @ [submitbutton]
    @ </td>
    @ <td>After filling in the information above, press this button to create
    @ the new ticket</td>
    @ </tr>
    @ </table>
    @ [defaultvalue status Open]
    @ [defaultvalue resolution Open]
    @ END-OF-NEW-TEMPLATE
    @ ######################################################################
    @ template edit END-OF-EDIT-TEMPLATE
    @ <table cellpadding="5">
    @ <tr><td align="right">Title:</td><td>
    @ [entrywidget title]
    @ </td></tr>
    @ <tr><td align="right">Status:</td><td>
    @ [entrywidget status]
    @ </td></tr>
    @ <tr><td align="right">Type:</td><td>
    @ [entrywidget type]
    @ </td></tr>
    @ <tr><td align="right">Severity:</td><td>
    @ [entrywidget severity]
    @ </td></tr>
    @ <tr><td align="right">Priority:</td><td>
    @ [entrywidget priority]
    @ </td></tr>
    @ <tr><td align="right">Resolution:</td><td>
    @ [entrywidget resolution]
    @ </td></tr>
    @ <tr><td align="right">Subsystem:</td><td>
    @ [entrywidget subsystem]
    @ </td></tr>
    @ <tr><td align="right">Assigned&nbsp;To:</td><td>
    @ [entrywidget assignedto]
    @ </td></tr>
    @ <tr><td align="right">Contact:</td><td>
    @ [entrywidget contact]
    @ </td></tr>
    @ <tr><td align="right">Version&nbsp;Found&nbsp;In:</td><td>
    @ [entrywidget foundin]
    @ </td></tr>
    @ <tr><td colspan="2">
    @ [ifappend comment]
    @   New Remarks:<br>
    @   [appendwidget comment]
    @ [else]
    @   Description And Comments:<br>
    @   [entrywidget comment]
    @ [endif]
    @ </td></tr>
    @ <tr><td align="right"></td><td>
    @ [submitbutton]
    @ </td></tr>
    @ </table>
    @ END-OF-EDIT-TEMPLATE
    @ ######################################################################
    @ template view END-OF-VIEW-TEMPLATE
    @ <table cellpadding="5">
    @ <tr><td align="right">Title:</td><td>
    @ [viewwidget title]
    @ </td></tr>
    @ <tr><td align="right">Status:</td><td>
    @ [viewwidget status]
    @ </td></tr>
    @ <tr><td align="right">Type:</td><td>
    @ [viewwidget type]
    @ </td></tr>
    @ <tr><td align="right">Severity:</td><td>
    @ [viewwidget severity]
    @ </td></tr>
    @ <tr><td align="right">Priority:</td><td>
    @ [viewwidget priority]
    @ </td></tr>
    @ <tr><td align="right">Resolution:</td><td>
    @ [viewwidget resolution]
    @ </td></tr>
    @ <tr><td align="right">Subsystem:</td><td>
    @ [viewwidget subsystem]
    @ </td></tr>
    @ <tr><td align="right">Assigned&nbsp;To:</td><td>
    @ [viewwidget assignedto]
    @ </td></tr>
    @ <tr><td align="right">Contact:</td><td>
    @ [viewwidget contact]
    @ </td></tr>
    @ <tr><td align="right">Version&nbsp;Found&nbsp;In:</td><td>
    @ [viewwidget foundin]
    @ </td></tr>
    @ <tr><td colspan="2">
    @ Description And Comments:<br>
    @ [viewwidget comment]
    @ </td></tr>
    @ </table>
    @ END-OF-VIEW-TEMPLATE
  ;
  Blob config, errmsg;
  blob_init(&config, zDefaultConfig, sizeof(zDefaultConfig)-1);
  db_begin_transaction();
  blob_zero(&errmsg);
  ticket_config_parse(&config, 0, &errmsg);
  if( blob_size(&errmsg) ){
    fossil_fatal("%b", &errmsg);
  }
  db_end_transaction(0);
}

/*
** Return the length of a string without its trailing whitespace.
*/
static int non_whitespace_length(const char *z){
  int n = strlen(z);
  while( n>0 && isspace(z[n-1]) ){ n--; }
  return n;
}

/*
** Fill the given Blob with text that describes the current
** ticket configuration.  This is the inverse of ticket_config_parse()
*/
void ticket_config_render(Blob *pOut){
  char *zDelim;
  char *zContent;
  Stmt q;
  int n;

  blob_appendf(pOut, "ticket-configuration\n");
  zDelim = db_text(0, "SELECT '--end-of-text--' || hex(random(20))");
  blob_appendf(pOut, "###################################################\n");
  db_prepare(&q, "SELECT name, type, width, arg FROM tktfield");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    const char *zType = db_column_text(&q, 1);
    int width = db_column_int(&q, 2);
    const char *zArg = db_column_text(&q, 3);
    blob_appendf(pOut, "field %s %s %d %s\n", zName, zType, width, zArg);
  }
  db_finalize(&q);
  blob_appendf(pOut, "###################################################\n");
  blob_appendf(pOut, "template new %s\n", zDelim);
  zContent = db_get("tkt-new-template", 0);
  if( zContent ){
    n = non_whitespace_length(zContent);
    blob_appendf(pOut, "%.*s\n", n, zContent);
    free(zContent);
  }
  blob_appendf(pOut, "%s\n", zDelim);
  blob_appendf(pOut, "###################################################\n");
  blob_appendf(pOut, "template edit %s\n", zDelim);
  zContent = db_get("tkt-edit-template", 0);
  if( zContent ){
    n = non_whitespace_length(zContent);
    blob_appendf(pOut, "%.*s\n", n, zContent);
    free(zContent);
  }
  blob_appendf(pOut, "%s\n", zDelim);
  blob_appendf(pOut, "###################################################\n");
  blob_appendf(pOut, "template view %s\n", zDelim);
  zContent = db_get("tkt-view-template", 0);
  if( zContent ){
    n = non_whitespace_length(zContent);
    blob_appendf(pOut, "%.*s\n", n, zContent);
    free(zContent);
  }
  blob_appendf(pOut, "%s\n", zDelim);
  blob_appendf(pOut, "###################################################\n");
  blob_appendf(pOut, "description %s\n", zDelim);
  zContent = db_get("tkt-desc", 0);
  if( zContent ){
    n = non_whitespace_length(zContent);
    blob_appendf(pOut, "%.*s\n", n, zContent);
    free(zContent);
  }
  blob_appendf(pOut, "%s\n", zDelim);
}

/*
** COMMAND: test-tktconfig-export
** Write the current ticket configuration out to a file.
*/
void tktconfig_render_cmd(void){
  Blob config;

  db_must_be_within_tree();
  if( g.argc!=3 ){
    usage("FILENAME");
  }
  blob_zero(&config);
  ticket_config_render(&config);
  blob_write_to_file(&config, g.argv[2]);
  blob_reset(&config);
}
