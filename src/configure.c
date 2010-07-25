/*
** Copyright (c) 2008 D. Richard Hipp
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
** This file contains code used to manage repository configurations.
** By "responsitory configure" we mean the local state of a repository
** distinct from the versioned files.
*/
#include "config.h"
#include "configure.h"
#include <assert.h>

#if INTERFACE
/*
** Configuration transfers occur in groups.  These are the allowed
** groupings:
*/
#define CONFIGSET_SKIN   0x000001     /* WWW interface appearance */
#define CONFIGSET_TKT    0x000002     /* Ticket configuration */
#define CONFIGSET_PROJ   0x000004     /* Project name */
#define CONFIGSET_SHUN   0x000008     /* Shun settings */
#define CONFIGSET_USER   0x000010     /* The USER table */
#define CONFIGSET_ADDR   0x000020     /* The CONCEALED table */

#define CONFIGSET_ALL    0xffffff     /* Everything */

#endif /* INTERFACE */

/*
** Names of the configuration sets
*/
static struct {
  const char *zName;   /* Name of the configuration set */
  int groupMask;       /* Mask for that configuration set */
  const char *zHelp;   /* What it does */
} aGroupName[] = {
  { "email",        CONFIGSET_ADDR,  "Concealed email addresses in tickets" },
  { "project",      CONFIGSET_PROJ,  "Project name and description"         },
  { "skin",         CONFIGSET_SKIN,  "Web interface apparance settings"     },
  { "shun",         CONFIGSET_SHUN,  "List of shunned artifacts"            },
  { "ticket",       CONFIGSET_TKT,   "Ticket setup",                        },
  { "user",         CONFIGSET_USER,  "Users and privilege settings"         },
  { "all",          CONFIGSET_ALL,   "All of the above"                     },
};


/*
** The following is a list of settings that we are willing to
** transfer.  
**
** Setting names that begin with an alphabetic characters refer to
** single entries in the CONFIG table.  Setting names that begin with
** "@" are for special processing.
*/
static struct {
  const char *zName;   /* Name of the configuration parameter */
  int groupMask;       /* Which config groups is it part of */
} aConfig[] = {
  { "css",                    CONFIGSET_SKIN },
  { "header",                 CONFIGSET_SKIN },
  { "footer",                 CONFIGSET_SKIN },
  { "logo-mimetype",          CONFIGSET_SKIN },
  { "logo-image",             CONFIGSET_SKIN },
  { "project-name",           CONFIGSET_PROJ },
  { "project-description",    CONFIGSET_PROJ },
  { "index-page",             CONFIGSET_SKIN },
  { "timeline-block-markup",  CONFIGSET_SKIN },
  { "timeline-max-comment",   CONFIGSET_SKIN },
  { "ticket-table",           CONFIGSET_TKT  },
  { "ticket-common",          CONFIGSET_TKT  },
  { "ticket-newpage",         CONFIGSET_TKT  },
  { "ticket-viewpage",        CONFIGSET_TKT  },
  { "ticket-editpage",        CONFIGSET_TKT  },
  { "ticket-reportlist",      CONFIGSET_TKT  },
  { "ticket-report-template", CONFIGSET_TKT  },
  { "ticket-key-template",    CONFIGSET_TKT  },
  { "ticket-title-expr",      CONFIGSET_TKT  },
  { "ticket-closed-expr",     CONFIGSET_TKT  },
  { "@reportfmt",             CONFIGSET_TKT  },
  { "@user",                  CONFIGSET_USER },
  { "@concealed",             CONFIGSET_ADDR },
  { "@shun",                  CONFIGSET_SHUN },
};
static int iConfig = 0;

/*
** Return name of first configuration property matching the given mask.
*/
const char *configure_first_name(int iMask){
  iConfig = 0;
  return configure_next_name(iMask);
}
const char *configure_next_name(int iMask){
  while( iConfig<count(aConfig) ){
    if( aConfig[iConfig].groupMask & iMask ){
      return aConfig[iConfig++].zName;
    }else{
      iConfig++;
    }
  }
  return 0;
}

/*
** Return the mask for the named configuration parameter if it can be
** safely exported.  Return 0 if the parameter is not safe to export.
*/
int configure_is_exportable(const char *zName){
  int i;
  for(i=0; i<count(aConfig); i++){
    if( strcmp(zName, aConfig[i].zName)==0 ){
      int m = aConfig[i].groupMask;
      if( !g.okAdmin ){
        m &= ~CONFIGSET_USER;
      }
      if( !g.okRdAddr ){
        m &= ~CONFIGSET_ADDR;
      }
      return m;
    }
  }
  return 0;
}

/*
** zName is one of the special configuration names that refers to an entire
** table rather than a single entry in CONFIG.  Special names are "@reportfmt"
** and "@shun" and "@user".  This routine writes SQL text into pOut that when
** evaluated will populate the corresponding table with data.
*/
void configure_render_special_name(const char *zName, Blob *pOut){
  Stmt q;
  if( strcmp(zName, "@shun")==0 ){
    db_prepare(&q, "SELECT uuid FROM shun");
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(pOut, "INSERT OR IGNORE INTO shun VALUES('%s');\n", 
        db_column_text(&q, 0)
      );
    }
    db_finalize(&q);
  }else if( strcmp(zName, "@reportfmt")==0 ){
    db_prepare(&q, "SELECT title, cols, sqlcode FROM reportfmt");
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(pOut, "INSERT INTO _xfer_reportfmt(title,cols,sqlcode)"
                         " VALUES(%Q,%Q,%Q);\n", 
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2)
      );
    }
    db_finalize(&q);
  }else if( strcmp(zName, "@user")==0 ){
    db_prepare(&q, 
        "SELECT login, CASE WHEN length(pw)==40 THEN pw END,"
        "       cap, info, quote(photo) FROM user");
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(pOut, "INSERT INTO _xfer_user(login,pw,cap,info,photo)"
                         " VALUES(%Q,%Q,%Q,%Q,%s);\n",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2),
        db_column_text(&q, 3),
        db_column_text(&q, 4)
      );
    }
    db_finalize(&q);
  }else if( strcmp(zName, "@concealed")==0 ){
    db_prepare(&q, "SELECT hash, content FROM concealed");
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(pOut, "INSERT OR IGNORE INTO concealed(hash,content)"
                         " VALUES(%Q,%Q);\n",
        db_column_text(&q, 0),
        db_column_text(&q, 1)
      );
    }
    db_finalize(&q);
  }
}

/*
** Two SQL functions:
**
**        flag_test(int)
**        flag_clear(int)
**
** The flag_test() function takes the integer valued argument and
** ANDs it against the static variable "flag_value" below.  The
** function returns TRUE or false depending on the result.  The
** flag_clear() function masks off the bits from "flag_value" that
** are given in the argument.
**
** These functions are used below in the WHEN clause of a trigger to
** get the trigger to fire exactly once.
*/
static int flag_value = 0xffff;
static void flag_test_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int m = sqlite3_value_int(argv[0]);
  sqlite3_result_int(context, (flag_value&m)!=0 );
}
static void flag_clear_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int m = sqlite3_value_int(argv[0]);
  flag_value &= ~m;
}

/*
** Create the temporary _xfer_reportfmt and _xfer_user tables that are
** necessary in order to evalute the SQL text generated by the
** configure_render_special_name() routine.
**
** If replaceFlag is true, then the setup is such that the content in
** the SQL text will completely replace the current repository configuration.
** If replaceFlag is false, then the SQL text will be merged with the
** existing configuration.  When merging, existing values take priority
** over SQL text values.
*/
void configure_prepare_to_receive(int replaceFlag){
  static const char zSQL1[] =
    @ CREATE TEMP TABLE _xfer_reportfmt(
    @    rn integer primary key,  -- Report number
    @    owner text,              -- Owner of this report format (not used)
    @    title text UNIQUE ON CONFLICT IGNORE,  -- Title of this report
    @    cols text,               -- A color-key specification
    @    sqlcode text             -- An SQL SELECT statement for this report
    @ );
    @ CREATE TEMP TABLE _xfer_user(
    @   uid INTEGER PRIMARY KEY,        -- User ID
    @   login TEXT UNIQUE ON CONFLICT IGNORE,   -- login name of the user
    @   pw TEXT,                        -- password
    @   cap TEXT,                       -- Capabilities of this user
    @   cookie TEXT,                    -- WWW login cookie
    @   ipaddr TEXT,                    -- IP address for which cookie is valid
    @   cexpire DATETIME,               -- Time when cookie expires
    @   info TEXT,                      -- contact information
    @   photo BLOB                      -- JPEG image of this user
    @ );
    @ INSERT INTO _xfer_reportfmt SELECT * FROM reportfmt;
    @ INSERT INTO _xfer_user SELECT * FROM user;
  ;
  db_multi_exec(zSQL1);
  
  /* When the replace flag is set, add triggers that run the first time
  ** that new data is seen.  The triggers run only once and delete all the
  ** existing data.
  */
  if( replaceFlag ){
    static const char zSQL2[] =
      @ CREATE TRIGGER _xfer_r1 BEFORE INSERT ON _xfer_reportfmt
      @ WHEN flag_test(1) BEGIN
      @   DELETE FROM _xfer_reportfmt;
      @   SELECT flag_clear(1);
      @ END;
      @ CREATE TRIGGER _xfer_r2 BEFORE INSERT ON _xfer_user
      @ WHEN flag_test(2) BEGIN
      @   DELETE FROM _xfer_user;
      @   SELECT flag_clear(2);
      @ END;
      @ CREATE TEMP TRIGGER _xfer_r3 BEFORE INSERT ON shun
      @ WHEN flag_test(4) BEGIN
      @   DELETE FROM shun;
      @   SELECT flag_clear(4);
      @ END;
    ;
    sqlite3_create_function(g.db, "flag_test", 1, SQLITE_UTF8, 0,
         flag_test_function, 0, 0);
    sqlite3_create_function(g.db, "flag_clear", 1, SQLITE_UTF8, 0,
         flag_clear_function, 0, 0);
    flag_value = 0xffff;
    db_multi_exec(zSQL2);
  }
}

/*
** After receiving configuration data, call this routine to transfer
** the results into the main database.
*/
void configure_finalize_receive(void){
  static const char zSQL[] =
    @ DELETE FROM user;
    @ INSERT INTO user SELECT * FROM _xfer_user;
    @ DELETE FROM reportfmt;
    @ INSERT INTO reportfmt SELECT * FROM _xfer_reportfmt;
    @ DROP TABLE _xfer_user;
    @ DROP TABLE _xfer_reportfmt;
  ;
  db_multi_exec(zSQL);
}

/*
** Identify a configuration group by name.  Return its mask.
** Throw an error if no match.
*/
static int find_area(const char *z){
  int i;
  int n = strlen(z);
  for(i=0; i<count(aGroupName); i++){
    if( strncmp(z, aGroupName[i].zName, n)==0 ){
      return aGroupName[i].groupMask;
    }
  }
  printf("Available configuration areas:\n");
  for(i=0; i<count(aGroupName); i++){
    printf("  %-10s %s\n", aGroupName[i].zName, aGroupName[i].zHelp);
  }
  fossil_fatal("no such configuration area: \"%s\"", z);
  return 0;
}

/*
** Write SQL text into file zFilename that will restore the configuration
** area identified by mask to its current state from any other state.
*/
static void export_config(
  int mask,                 /* Mask indicating which configuration to export */
  const char *zMask,        /* Name of the configuration */
  const char *zFilename     /* Write into this file */
){
  int i;
  Blob out;
  blob_zero(&out);
  blob_appendf(&out, 
    "-- The \"%s\" configuration exported from\n"
    "-- repository \"%s\"\n"
    "-- on %s\n",
    zMask, g.zRepositoryName,
    db_text(0, "SELECT datetime('now')")
  );
  for(i=0; i<count(aConfig); i++){
    if( (aConfig[i].groupMask & mask)!=0 ){
      const char *zName = aConfig[i].zName;
      if( zName[0]!='@' ){
        char *zValue = db_text(0, 
            "SELECT quote(value) FROM config WHERE name=%Q", zName);
        if( zValue ){
          blob_appendf(&out,"REPLACE INTO config VALUES(%Q,%s);\n", 
                       zName, zValue);
        }
        free(zValue);
      }else{
        configure_render_special_name(zName, &out);
      }
    }
  }
  blob_write_to_file(&out, zFilename);
  blob_reset(&out);
}


/*
** COMMAND: configuration
**
** Usage: %fossil configure METHOD ... ?-R|--repository REPOSITORY?
**
** Where METHOD is one of: export import merge pull push reset.  All methods
** accept the -R or --repository option to specific a repository.
**
**    %fossil configuration export AREA FILENAME
**
**         Write to FILENAME exported configuraton information for AREA.
**         AREA can be one of:  all email project shun skin ticket user
**
**    %fossil configuration import FILENAME
**
**         Read a configuration from FILENAME, overwriting the current
**         configuration.
**
**    %fossil configuration merge FILENAME
**
**         Read a configuration from FILENAME and merge its values into
**         the current configuration.  Existing values take priority over
**         values read from FILENAME.
**
**    %fossil configuration pull AREA ?URL?
**
**         Pull and install the configuration from a different server
**         identified by URL.  If no URL is specified, then the default
**         server is used. 
**
**    %fossil configuration push AREA ?URL?
**
**         Push the local configuration into the remote server identified
**         by URL.  Admin privilege is required on the remote server for
**         this to work.
**
**    %fossil configuration reset AREA
**
**         Restore the configuration to the default.  AREA as above.
**
** WARNING: Do not import, merge, or pull configurations from an untrusted
** source.  The inbound configuration is not checked for safety and can
** introduce security vulnerabilities.
*/
void configuration_cmd(void){
  int n;
  const char *zMethod;
  if( g.argc<3 ){
    usage("export|import|merge|pull|reset ...");
  }
  db_find_and_open_repository(1);
  zMethod = g.argv[2];
  n = strlen(zMethod);
  if( strncmp(zMethod, "export", n)==0 ){
    int mask;
    if( g.argc!=5 ){
      usage("export AREA FILENAME");
    }
    mask = find_area(g.argv[3]);
    export_config(mask, g.argv[3], g.argv[4]);
  }else
  if( strncmp(zMethod, "import", n)==0 
       || strncmp(zMethod, "merge", n)==0 ){
    Blob in;
    if( g.argc!=4 ) usage(mprintf("%s FILENAME",zMethod));
    blob_read_from_file(&in, g.argv[3]);
    db_begin_transaction();
    configure_prepare_to_receive(zMethod[0]=='i');
    db_multi_exec("%s", blob_str(&in));
    configure_finalize_receive();
    db_end_transaction(0);
  }else
  if( strncmp(zMethod, "pull", n)==0 || strncmp(zMethod, "push", n)==0 ){
    int mask;
    const char *zServer;
    const char *zPw;
    url_proxy_options();
    if( g.argc!=4 && g.argc!=5 ){
      usage("pull AREA ?URL?");
    }
    mask = find_area(g.argv[3]);
    if( g.argc==5 ){
      zServer = g.argv[4];
      zPw = 0;
      g.dontKeepUrl = 1;
    }else{
      zServer = db_get("last-sync-url", 0);
      if( zServer==0 ){
        fossil_fatal("no server specified");
      }
      zPw = db_get("last-sync-pw", 0);
    }
    url_parse(zServer);
    if( g.urlPasswd==0 && zPw ) g.urlPasswd = mprintf("%s", zPw);
    user_select();
    if( strncmp(zMethod, "push", n)==0 ){
      client_sync(0,0,0,0,mask);
    }else{
      client_sync(0,0,0,mask,0);
    }
  }else
  if( strncmp(zMethod, "reset", n)==0 ){
    int mask, i;
    char *zBackup;
    if( g.argc!=4 ) usage("reset AREA");
    mask = find_area(g.argv[3]);
    zBackup = db_text(0, 
       "SELECT strftime('config-backup-%%Y%%m%%d%%H%%M%%f','now')");
    db_begin_transaction();
    export_config(mask, g.argv[3], zBackup);
    for(i=0; i<count(aConfig); i++){
      const char *zName = aConfig[i].zName;
      if( (aConfig[i].groupMask & mask)==0 ) continue;
      if( zName[0]!='@' ){
        db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
      }else if( strcmp(zName,"@user")==0 ){
        db_multi_exec("DELETE FROM user");
        db_create_default_users(0, 0);
      }else if( strcmp(zName,"@concealed")==0 ){
        db_multi_exec("DELETE FROM concealed");
      }else if( strcmp(zName,"@shun")==0 ){
        db_multi_exec("DELETE FROM shun");
      }else if( strcmp(zName,"@reportfmt")==0 ){
        db_multi_exec("DELETE FROM reportfmt");
      }
    }
    db_end_transaction(0);
    printf("Configuration reset to factory defaults.\n");
    printf("To recover, use:  %s %s import %s\n", 
            g.argv[0], g.argv[1], zBackup);
  }else
  {
    fossil_fatal("METHOD should be one of:"
                 " export import merge pull push reset");
  }
}
