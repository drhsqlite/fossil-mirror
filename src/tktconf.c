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
** is one of:  text, ctext, enum, date, uuid, baseline.  All
** types have at least a <width> parameter.  Text and Ctext types
** have a height parameter.  Enum has a list of allowed values.
**
** The <type> of a template is one of: new, view, edit.  There must
** be one template of each type.  <delimiter> is an arbitrary string
** that terminates the template.  The body of the template is subsequent
** lines of text up to but not including the <delimiter>.  Trailing
** whitespace on the delimiter is ignored.
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
     "CREATE TABLE ticket(\n"
     "  tktid INTEGER PRIMARY KEY,\n"
     "  tktuuid TEXT UNIQUE,\n"
     "  starttime DATETIME,\n"
     "  lastmod DATETIME"
  );
  blob_appendf(&sql,
     "DROP TABLE IF EXISTS tktfield;\n"
     "CREATE TABLE tktfield(\n"
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
      if( blob_eq(&type,"text") || blob_eq(&type,"ctext") ){
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
      
    /*
    **  template <type> <delimiter>
    **  <text>
    */      
    }else if( blob_eq(&token, "template")
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
    }else{
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
void test_tktconfig_cmd(void){
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
