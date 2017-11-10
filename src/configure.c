/*
** Copyright (c) 2008 D. Richard Hipp
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
** This file contains code used to manage repository configurations.
**
** By "repository configure" we mean the local state of a repository
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
#define CONFIGSET_CSS       0x000001     /* Style sheet only */
#define CONFIGSET_SKIN      0x000002     /* WWW interface appearance */
#define CONFIGSET_TKT       0x000004     /* Ticket configuration */
#define CONFIGSET_PROJ      0x000008     /* Project name */
#define CONFIGSET_SHUN      0x000010     /* Shun settings */
#define CONFIGSET_USER      0x000020     /* The USER table */
#define CONFIGSET_ADDR      0x000040     /* The CONCEALED table */
#define CONFIGSET_XFER      0x000080     /* Transfer configuration */
#define CONFIGSET_ALIAS     0x000100     /* URL Aliases */

#define CONFIGSET_ALL       0x0001ff     /* Everything */

#define CONFIGSET_OVERWRITE 0x100000     /* Causes overwrite instead of merge */

/*
** This mask is used for the common TH1 configuration settings (i.e. those
** that are not specific to one particular subsystem, such as the transfer
** subsystem).
*/
#define CONFIGSET_TH1       (CONFIGSET_SKIN|CONFIGSET_TKT|CONFIGSET_XFER)

#endif /* INTERFACE */

/*
** Names of the configuration sets
*/
static struct {
  const char *zName;   /* Name of the configuration set */
  int groupMask;       /* Mask for that configuration set */
  const char *zHelp;   /* What it does */
} aGroupName[] = {
  { "/email",        CONFIGSET_ADDR,  "Concealed email addresses in tickets" },
  { "/project",      CONFIGSET_PROJ,  "Project name and description"         },
  { "/skin",         CONFIGSET_SKIN | CONFIGSET_CSS,
                                      "Web interface appearance settings"    },
  { "/css",          CONFIGSET_CSS,   "Style sheet"                          },
  { "/shun",         CONFIGSET_SHUN,  "List of shunned artifacts"            },
  { "/ticket",       CONFIGSET_TKT,   "Ticket setup",                        },
  { "/user",         CONFIGSET_USER,  "Users and privilege settings"         },
  { "/xfer",         CONFIGSET_XFER,  "Transfer setup",                      },
  { "/alias",        CONFIGSET_ALIAS, "URL Aliases",                         },
  { "/all",          CONFIGSET_ALL,   "All of the above"                     },
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
  { "css",                    CONFIGSET_CSS  },
  { "header",                 CONFIGSET_SKIN },
  { "footer",                 CONFIGSET_SKIN },
  { "details",                CONFIGSET_SKIN },
  { "logo-mimetype",          CONFIGSET_SKIN },
  { "logo-image",             CONFIGSET_SKIN },
  { "background-mimetype",    CONFIGSET_SKIN },
  { "background-image",       CONFIGSET_SKIN },
  { "timeline-block-markup",  CONFIGSET_SKIN },
  { "timeline-max-comment",   CONFIGSET_SKIN },
  { "timeline-plaintext",     CONFIGSET_SKIN },
  { "adunit",                 CONFIGSET_SKIN },
  { "adunit-omit-if-admin",   CONFIGSET_SKIN },
  { "adunit-omit-if-user",    CONFIGSET_SKIN },

#ifdef FOSSIL_ENABLE_TH1_DOCS
  { "th1-docs",               CONFIGSET_TH1 },
#endif
#ifdef FOSSIL_ENABLE_TH1_HOOKS
  { "th1-hooks",              CONFIGSET_TH1 },
#endif
  { "th1-setup",              CONFIGSET_TH1 },
  { "th1-uri-regexp",         CONFIGSET_TH1 },

#ifdef FOSSIL_ENABLE_TCL
  { "tcl",                    CONFIGSET_TH1 },
  { "tcl-setup",              CONFIGSET_TH1 },
#endif

  { "project-name",           CONFIGSET_PROJ },
  { "short-project-name",     CONFIGSET_PROJ },
  { "project-description",    CONFIGSET_PROJ },
  { "index-page",             CONFIGSET_PROJ },
  { "manifest",               CONFIGSET_PROJ },
  { "binary-glob",            CONFIGSET_PROJ },
  { "clean-glob",             CONFIGSET_PROJ },
  { "ignore-glob",            CONFIGSET_PROJ },
  { "keep-glob",              CONFIGSET_PROJ },
  { "crlf-glob",              CONFIGSET_PROJ },
  { "crnl-glob",              CONFIGSET_PROJ },
  { "encoding-glob",          CONFIGSET_PROJ },
  { "empty-dirs",             CONFIGSET_PROJ },
  { "allow-symlinks",         CONFIGSET_PROJ },
  { "dotfiles",               CONFIGSET_PROJ },
  { "parent-project-code",    CONFIGSET_PROJ },
  { "parent-project-name",    CONFIGSET_PROJ },
  { "hash-policy",            CONFIGSET_PROJ },

#ifdef FOSSIL_ENABLE_LEGACY_MV_RM
  { "mv-rm-files",            CONFIGSET_PROJ },
#endif

  { "ticket-table",           CONFIGSET_TKT  },
  { "ticket-common",          CONFIGSET_TKT  },
  { "ticket-change",          CONFIGSET_TKT  },
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

  { "@alias",                 CONFIGSET_ALIAS },

  { "xfer-common-script",     CONFIGSET_XFER },
  { "xfer-push-script",       CONFIGSET_XFER },
  { "xfer-commit-script",     CONFIGSET_XFER },
  { "xfer-ticket-script",     CONFIGSET_XFER },

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
  if( iConfig==0 && (iMask & CONFIGSET_ALL)==CONFIGSET_ALL ){
    iConfig = count(aGroupName);
    return "/all";
  }
  while( iConfig<count(aGroupName)-1 ){
    if( aGroupName[iConfig].groupMask & iMask ){
      return aGroupName[iConfig++].zName;
    }else{
      iConfig++;
    }
  }
  return 0;
}

/*
** Return a pointer to a string that contains the RHS of an IN operator
** that will select CONFIG table names that are part of the configuration
** that matches iMatch.
*/
const char *configure_inop_rhs(int iMask){
  Blob x;
  int i;
  const char *zSep = "";

  blob_zero(&x);
  blob_append_sql(&x, "(");
  for(i=0; i<count(aConfig); i++){
    if( (aConfig[i].groupMask & iMask)==0 ) continue;
    if( aConfig[i].zName[0]=='@' ) continue;
    blob_append_sql(&x, "%s'%q'", zSep/*safe-for-%s*/, aConfig[i].zName);
    zSep = ",";
  }
  blob_append_sql(&x, ")");
  return blob_sql_text(&x);
}

/*
** Return the mask for the named configuration parameter if it can be
** safely exported.  Return 0 if the parameter is not safe to export.
**
** "Safe" in the previous paragraph means the permission is created to
** export the property.  In other words, the requesting side has presented
** login credentials and has sufficient capabilities to access the requested
** information.
*/
int configure_is_exportable(const char *zName){
  int i;
  int n = strlen(zName);
  if( n>2 && zName[0]=='\'' && zName[n-1]=='\'' ){
    zName++;
    n -= 2;
  }
  for(i=0; i<count(aConfig); i++){
    if( strncmp(zName, aConfig[i].zName, n)==0 && aConfig[i].zName[n]==0 ){
      int m = aConfig[i].groupMask;
      if( !g.perm.Admin ){
        m &= ~CONFIGSET_USER;
      }
      if( !g.perm.RdAddr ){
        m &= ~CONFIGSET_ADDR;
      }
      return m;
    }
  }
  if( strncmp(zName, "walias:/", 8)==0 ){
    return CONFIGSET_ALIAS;
  }
  return 0;
}

/*
** A mask of all configuration tables that have been reset already.
*/
static int configHasBeenReset = 0;

/*
** Mask of modified configuration sets
*/
static int rebuildMask = 0;

/*
** Rebuild auxiliary tables as required by configuration changes.
*/
void configure_rebuild(void){
  if( rebuildMask & CONFIGSET_TKT ){
    ticket_rebuild();
  }
  rebuildMask = 0;
}

/*
** Return true if z[] is not a "safe" SQL token.  A safe token is one of:
**
**   *   A string literal
**   *   A blob literal
**   *   An integer literal  (no floating point)
**   *   NULL
*/
static int safeSql(const char *z){
  int i;
  if( z==0 || z[0]==0 ) return 0;
  if( (z[0]=='x' || z[0]=='X') && z[1]=='\'' ) z++;
  if( z[0]=='\'' ){
    for(i=1; z[i]; i++){
      if( z[i]=='\'' ){
        i++;
        if( z[i]=='\'' ){ continue; }
        return z[i]==0;
      }
    }
    return 0;
  }else{
    char c;
    for(i=0; (c = z[i])!=0; i++){
      if( !fossil_isalnum(c) ) return 0;
    }
  }
  return 1;
}

/*
** Return true if z[] consists of nothing but digits
*/
static int safeInt(const char *z){
  int i;
  if( z==0 || z[0]==0 ) return 0;
  for(i=0; fossil_isdigit(z[i]); i++){}
  return z[i]==0;
}

/*
** Process a single "config" card received from the other side of a
** sync session.
**
** Mask consists of one or more CONFIGSET_* values ORed together, to
** designate what types of configuration we are allowed to receive.
**
** NEW FORMAT:
**
** zName is one of "/config", "/user", "/shun", "/reportfmt", or "/concealed".
** zName indicates the table that holds the configuration information being
** transferred.  pContent is a string that consist of alternating Fossil
** and SQL tokens.  The First token is a timestamp in seconds since 1970.
** The second token is a primary key for the table identified by zName.  If
** The entry with the corresponding primary key exists and has a more recent
** mtime, then nothing happens.  If the entry does not exist or if it has
** an older mtime, then the content described by subsequent token pairs is
** inserted.  The first element of each token pair is a column name and
** the second is its value.
**
** In overview, we have:
**
**    NAME        CONTENT
**    -------     -----------------------------------------------------------
**    /config     $MTIME $NAME value $VALUE
**    /user       $MTIME $LOGIN pw $VALUE cap $VALUE info $VALUE photo $VALUE
**    /shun       $MTIME $UUID scom $VALUE
**    /reportfmt  $MTIME $TITLE owner $VALUE cols $VALUE sqlcode $VALUE
**    /concealed  $MTIME $HASH content $VALUE
**
** OLD FORMAT:
**
** The old format is retained for backwards compatibility, but is deprecated.
** The cutover from old format to new was on 2011-04-25.  After sufficient
** time has passed, support for the old format will be removed.
** Update: Support for the old format was remoed on 2017-09-20.
**
** zName is either the NAME of an element of the CONFIG table, or else
** one of the special names "@shun", "@reportfmt", "@user", or "@concealed".
** If zName is a CONFIG table name, then CONTENT replaces (overwrites) the
** element in the CONFIG table.  For one of the @-labels, CONTENT is raw
** SQL that is evaluated.  Note that the raw SQL in CONTENT might not
** insert directly into the target table but might instead use a proxy
** table like _fer_reportfmt or _xfer_user.  Such tables must be created
** ahead of time using configure_prepare_to_receive().  Then after multiple
** calls to this routine, configure_finalize_receive() to transfer the
** information received into the true target table.
*/
void configure_receive(const char *zName, Blob *pContent, int groupMask){
  if( zName[0]=='/' ){
    /* The new format */
    char *azToken[12];
    int nToken = 0;
    int ii, jj;
    int thisMask;
    Blob name, value, sql;
    static const struct receiveType {
      const char *zName;
      const char *zPrimKey;
      int nField;
      const char *azField[4];
    } aType[] = {
      { "/config",    "name",  1, { "value", 0, 0, 0 }              },
      { "@user",      "login", 4, { "pw", "cap", "info", "photo" }  },
      { "@shun",      "uuid",  1, { "scom", 0, 0, 0 }               },
      { "@reportfmt", "title", 3, { "owner", "cols", "sqlcode", 0 } },
      { "@concealed", "hash",  1, { "content", 0, 0, 0 }            },
    };
    for(ii=0; ii<count(aType); ii++){
      if( fossil_strcmp(&aType[ii].zName[1],&zName[1])==0 ) break;
    }
    if( ii>=count(aType) ) return;
    while( blob_token(pContent, &name) && blob_sqltoken(pContent, &value) ){
      char *z = blob_terminate(&name);
      if( !safeSql(z) ) return;
      if( nToken>0 ){
        for(jj=0; jj<aType[ii].nField; jj++){
          if( fossil_strcmp(aType[ii].azField[jj], z)==0 ) break;
        }
        if( jj>=aType[ii].nField ) continue;
      }else{
        if( !safeInt(z) ) return;
      }
      azToken[nToken++] = z;
      azToken[nToken++] = z = blob_terminate(&value);
      if( !safeSql(z) ) return;
      if( nToken>=count(azToken) ) break;
    }
    if( nToken<2 ) return;
    if( aType[ii].zName[0]=='/' ){
      thisMask = configure_is_exportable(azToken[1]);
    }else{
      thisMask = configure_is_exportable(aType[ii].zName);
    }
    if( (thisMask & groupMask)==0 ) return;

    blob_zero(&sql);
    if( groupMask & CONFIGSET_OVERWRITE ){
      if( (thisMask & configHasBeenReset)==0 && aType[ii].zName[0]!='/' ){
        db_multi_exec("DELETE FROM \"%w\"", &aType[ii].zName[1]);
        configHasBeenReset |= thisMask;
      }
      blob_append_sql(&sql, "REPLACE INTO ");
    }else{
      blob_append_sql(&sql, "INSERT OR IGNORE INTO ");
    }
    blob_append_sql(&sql, "\"%w\"(\"%w\", mtime", &zName[1], aType[ii].zPrimKey);
    for(jj=2; jj<nToken; jj+=2){
       blob_append_sql(&sql, ",\"%w\"", azToken[jj]);
    }
    blob_append_sql(&sql,") VALUES(%s,%s",
       azToken[1] /*safe-for-%s*/, azToken[0] /*safe-for-%s*/);
    for(jj=2; jj<nToken; jj+=2){
       blob_append_sql(&sql, ",%s", azToken[jj+1] /*safe-for-%s*/);
    }
    db_multi_exec("%s)", blob_sql_text(&sql));
    if( db_changes()==0 ){
      blob_reset(&sql);
      blob_append_sql(&sql, "UPDATE \"%w\" SET mtime=%s",
                      &zName[1], azToken[0]/*safe-for-%s*/);
      for(jj=2; jj<nToken; jj+=2){
        blob_append_sql(&sql, ", \"%w\"=%s",
                        azToken[jj], azToken[jj+1]/*safe-for-%s*/);
      }
      blob_append_sql(&sql, " WHERE \"%w\"=%s AND mtime<%s",
                   aType[ii].zPrimKey, azToken[1]/*safe-for-%s*/,
                   azToken[0]/*safe-for-%s*/);
      db_multi_exec("%s", blob_sql_text(&sql));
    }
    blob_reset(&sql);
    rebuildMask |= thisMask;
  }
}

/*
** Process a file full of "config" cards.
*/
void configure_receive_all(Blob *pIn, int groupMask){
  Blob line;
  int nToken;
  int size;
  Blob aToken[4];

  configHasBeenReset = 0;
  while( blob_line(pIn, &line) ){
    if( blob_buffer(&line)[0]=='#' ) continue;
    nToken = blob_tokenize(&line, aToken, count(aToken));
    if( blob_eq(&aToken[0],"config")
     && nToken==3
     && blob_is_int(&aToken[2], &size)
    ){
      const char *zName = blob_str(&aToken[1]);
      Blob content;
      blob_zero(&content);
      blob_extract(pIn, size, &content);
      g.perm.Admin = g.perm.RdAddr = 1;
      configure_receive(zName, &content, groupMask);
      blob_reset(&content);
      blob_seek(pIn, 1, BLOB_SEEK_CUR);
    }
  }
}


/*
** Send "config" cards using the new format for all elements of a group
** that have recently changed.
**
** Output goes into pOut.  The groupMask identifies the group(s) to be sent.
** Send only entries whose timestamp is later than or equal to iStart.
**
** Return the number of cards sent.
*/
int configure_send_group(
  Blob *pOut,              /* Write output here */
  int groupMask,           /* Mask of groups to be send */
  sqlite3_int64 iStart     /* Only write values changed since this time */
){
  Stmt q;
  Blob rec;
  int ii;
  int nCard = 0;

  blob_zero(&rec);
  if( groupMask & CONFIGSET_SHUN ){
    db_prepare(&q, "SELECT mtime, quote(uuid), quote(scom) FROM shun"
                   " WHERE mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s scom %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2)
      );
      blob_appendf(pOut, "config /shun %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  if( groupMask & CONFIGSET_USER ){
    db_prepare(&q, "SELECT mtime, quote(login), quote(pw), quote(cap),"
                   "       quote(info), quote(photo) FROM user"
                   " WHERE mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s pw %s cap %s info %s photo %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2),
        db_column_text(&q, 3),
        db_column_text(&q, 4),
        db_column_text(&q, 5)
      );
      blob_appendf(pOut, "config /user %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  if( groupMask & CONFIGSET_TKT ){
    db_prepare(&q, "SELECT mtime, quote(title), quote(owner), quote(cols),"
                   "       quote(sqlcode) FROM reportfmt"
                   " WHERE mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s owner %s cols %s sqlcode %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2),
        db_column_text(&q, 3),
        db_column_text(&q, 4)
      );
      blob_appendf(pOut, "config /reportfmt %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  if( groupMask & CONFIGSET_ADDR ){
    db_prepare(&q, "SELECT mtime, quote(hash), quote(content) FROM concealed"
                   " WHERE mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s content %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2)
      );
      blob_appendf(pOut, "config /concealed %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  if( groupMask & CONFIGSET_ALIAS ){
    db_prepare(&q, "SELECT mtime, quote(name), quote(value) FROM config"
                   " WHERE name GLOB 'walias:/*' AND mtime>=%lld", iStart);
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&rec,"%s %s value %s",
        db_column_text(&q, 0),
        db_column_text(&q, 1),
        db_column_text(&q, 2)
      );
      blob_appendf(pOut, "config /config %d\n%s\n",
                   blob_size(&rec), blob_str(&rec));
      nCard++;
      blob_reset(&rec);
    }
    db_finalize(&q);
  }
  db_prepare(&q, "SELECT mtime, quote(name), quote(value) FROM config"
                 " WHERE name=:name AND mtime>=%lld", iStart);
  for(ii=0; ii<count(aConfig); ii++){
    if( (aConfig[ii].groupMask & groupMask)!=0 && aConfig[ii].zName[0]!='@' ){
      db_bind_text(&q, ":name", aConfig[ii].zName);
      while( db_step(&q)==SQLITE_ROW ){
        blob_appendf(&rec,"%s %s value %s",
          db_column_text(&q, 0),
          db_column_text(&q, 1),
          db_column_text(&q, 2)
        );
        blob_appendf(pOut, "config /config %d\n%s\n",
                     blob_size(&rec), blob_str(&rec));
        nCard++;
        blob_reset(&rec);
      }
      db_reset(&q);
    }
  }
  db_finalize(&q);
  return nCard;
}

/*
** Identify a configuration group by name.  Return its mask.
** Throw an error if no match.
*/
int configure_name_to_mask(const char *z, int notFoundIsFatal){
  int i;
  int n = strlen(z);
  for(i=0; i<count(aGroupName); i++){
    if( strncmp(z, &aGroupName[i].zName[1], n)==0 ){
      return aGroupName[i].groupMask;
    }
  }
  if( notFoundIsFatal ){
    fossil_print("Available configuration areas:\n");
    for(i=0; i<count(aGroupName); i++){
      fossil_print("  %-10s %s\n", &aGroupName[i].zName[1], aGroupName[i].zHelp);
    }
    fossil_fatal("no such configuration area: \"%s\"", z);
  }
  return 0;
}

/*
** Write SQL text into file zFilename that will restore the configuration
** area identified by mask to its current state from any other state.
*/
static void export_config(
  int groupMask,            /* Mask indicating which configuration to export */
  const char *zMask,        /* Name of the configuration */
  sqlite3_int64 iStart,     /* Start date */
  const char *zFilename     /* Write into this file */
){
  Blob out;
  blob_zero(&out);
  blob_appendf(&out,
    "# The \"%s\" configuration exported from\n"
    "# repository \"%s\"\n"
    "# on %s\n",
    zMask, g.zRepositoryName,
    db_text(0, "SELECT datetime('now')")
  );
  configure_send_group(&out, groupMask, iStart);
  blob_write_to_file(&out, zFilename);
  blob_reset(&out);
}


/*
** COMMAND: configuration*
**
** Usage: %fossil configuration METHOD ... ?OPTIONS?
**
** Where METHOD is one of: export import merge pull push reset.  All methods
** accept the -R or --repository option to specify a repository.
**
**    %fossil configuration export AREA FILENAME
**
**         Write to FILENAME exported configuration information for AREA.
**         AREA can be one of:  all email project shun skin ticket user alias
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
**         server is used.  Use the --overwrite flag to completely
**         replace local settings with content received from URL.
**
**    %fossil configuration push AREA ?URL?
**
**         Push the local configuration into the remote server identified
**         by URL.  Admin privilege is required on the remote server for
**         this to work.  When the same record exists both locally and on
**         the remote end, the one that was most recently changed wins.
**
**    %fossil configuration reset AREA
**
**         Restore the configuration to the default.  AREA as above.
**
**    %fossil configuration sync AREA ?URL?
**
**         Synchronize configuration changes in the local repository with
**         the remote repository at URL.
**
** Options:
**    -R|--repository FILE       Extract info from repository FILE
**
** See also: settings, unset
*/
void configuration_cmd(void){
  int n;
  const char *zMethod;
  db_find_and_open_repository(0, 0);
  db_open_config(0, 0);
  if( g.argc<3 ){
    usage("SUBCOMMAND ...");
  }
  zMethod = g.argv[2];
  n = strlen(zMethod);
  if( strncmp(zMethod, "export", n)==0 ){
    int mask;
    const char *zSince = find_option("since",0,1);
    sqlite3_int64 iStart;
    if( g.argc!=5 ){
      usage("export AREA FILENAME");
    }
    mask = configure_name_to_mask(g.argv[3], 1);
    if( zSince ){
      iStart = db_multi_exec(
         "SELECT coalesce(strftime('%%s',%Q),strftime('%%s','now',%Q))+0",
         zSince, zSince
      );
    }else{
      iStart = 0;
    }
    export_config(mask, g.argv[3], iStart, g.argv[4]);
  }else
  if( strncmp(zMethod, "import", n)==0
       || strncmp(zMethod, "merge", n)==0 ){
    Blob in;
    int groupMask;
    if( g.argc!=4 ) usage(mprintf("%s FILENAME",zMethod));
    blob_read_from_file(&in, g.argv[3]);
    db_begin_transaction();
    if( zMethod[0]=='i' ){
      groupMask = CONFIGSET_ALL | CONFIGSET_OVERWRITE;
    }else{
      groupMask = CONFIGSET_ALL;
    }
    configure_receive_all(&in, groupMask);
    db_end_transaction(0);
  }else
  if( strncmp(zMethod, "pull", n)==0
   || strncmp(zMethod, "push", n)==0
   || strncmp(zMethod, "sync", n)==0
  ){
    int mask;
    const char *zServer = 0;
    int overwriteFlag = 0;

    if( strncmp(zMethod,"pull",n)==0 ){
      overwriteFlag = find_option("overwrite",0,0)!=0;
    }
    url_proxy_options();
    if( g.argc!=4 && g.argc!=5 ){
      usage(mprintf("%s AREA ?URL?", zMethod));
    }
    mask = configure_name_to_mask(g.argv[3], 1);
    if( g.argc==5 ){
      zServer = g.argv[4];
    }
    url_parse(zServer, URL_PROMPT_PW);
    if( g.url.protocol==0 ) fossil_fatal("no server URL specified");
    user_select();
    url_enable_proxy("via proxy: ");
    if( overwriteFlag ) mask |= CONFIGSET_OVERWRITE;
    if( strncmp(zMethod, "push", n)==0 ){
      client_sync(0,0,(unsigned)mask);
    }else if( strncmp(zMethod, "pull", n)==0 ){
      client_sync(0,(unsigned)mask,0);
    }else{
      client_sync(0,(unsigned)mask,(unsigned)mask);
    }
  }else
  if( strncmp(zMethod, "reset", n)==0 ){
    int mask, i;
    char *zBackup;
    if( g.argc!=4 ) usage("reset AREA");
    mask = configure_name_to_mask(g.argv[3], 1);
    zBackup = db_text(0,
       "SELECT strftime('config-backup-%%Y%%m%%d%%H%%M%%f','now')");
    db_begin_transaction();
    export_config(mask, g.argv[3], 0, zBackup);
    for(i=0; i<count(aConfig); i++){
      const char *zName = aConfig[i].zName;
      if( (aConfig[i].groupMask & mask)==0 ) continue;
      if( zName[0]!='@' ){
        db_multi_exec("DELETE FROM config WHERE name=%Q", zName);
      }else if( fossil_strcmp(zName,"@user")==0 ){
        db_multi_exec("DELETE FROM user");
        db_create_default_users(0, 0);
      }else if( fossil_strcmp(zName,"@concealed")==0 ){
        db_multi_exec("DELETE FROM concealed");
      }else if( fossil_strcmp(zName,"@shun")==0 ){
        db_multi_exec("DELETE FROM shun");
      }else if( fossil_strcmp(zName,"@reportfmt")==0 ){
        db_multi_exec("DELETE FROM reportfmt");
        assert( strchr(zRepositorySchemaDefaultReports,'%')==0 );
        db_multi_exec(zRepositorySchemaDefaultReports /*works-like:""*/);
      }
    }
    db_end_transaction(0);
    fossil_print("Configuration reset to factory defaults.\n");
    fossil_print("To recover, use:  %s %s import %s\n",
            g.argv[0], g.argv[1], zBackup);
    rebuildMask |= mask;
  }else
  {
    fossil_fatal("METHOD should be one of:"
                 " export import merge pull push reset");
  }
  configure_rebuild();
}


/*
** COMMAND: test-var-list
**
** Usage: %fossil test-var-list ?PATTERN? ?--unset? ?--mtime?
**
** Show the content of the CONFIG table in a repository.  If PATTERN is
** specified, then only show the entries that match that glob pattern.
** Last modification time is shown if the --mtime option is present.
**
** If the --unset option is included, then entries are deleted rather than
** being displayed.  WARNING! This cannot be undone.  Be sure you know what
** you are doing!  The --unset option only works if there is a PATTERN.
** Probably you should run the command once without --unset to make sure
** you know exactly what is being deleted.
**
** If not in an open check-out, use the -R REPO option to specify a
** a repository.
*/
void test_var_list_cmd(void){
  Stmt q;
  int i, j;
  const char *zPattern = 0;
  int doUnset;
  int showMtime;
  Blob sql;
  Blob ans;
  unsigned char zTrans[1000];

  doUnset = find_option("unset",0,0)!=0;
  showMtime = find_option("mtime",0,0)!=0;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  verify_all_options();
  if( g.argc>=3 ){
    zPattern = g.argv[2];
  }
  blob_init(&sql,0,0);
  blob_appendf(&sql, "SELECT name, value, datetime(mtime,'unixepoch')"
                     " FROM config");
  if( zPattern ){
    blob_appendf(&sql, " WHERE name GLOB %Q", zPattern);
  }
  if( showMtime ){
    blob_appendf(&sql, " ORDER BY mtime, name");
  }else{
    blob_appendf(&sql, " ORDER BY name");
  }
  db_prepare(&q, "%s", blob_str(&sql)/*safe-for-%s*/);
  blob_reset(&sql);
#define MX_VAL 40
#define MX_NM  28
#define MX_LONGNM 60
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q,0);
    int nName = db_column_bytes(&q,0);
    const char *zValue = db_column_text(&q,1);
    int szValue = db_column_bytes(&q,1);
    const char *zMTime = db_column_text(&q,2);
    for(i=j=0; j<MX_VAL && zValue[i]; i++){
      unsigned char c = (unsigned char)zValue[i];
      if( c>=' ' && c<='~' ){
        zTrans[j++] = c;
      }else{
        zTrans[j++] = '\\';
        if( c=='\n' ){
          zTrans[j++] = 'n';
        }else if( c=='\r' ){
          zTrans[j++] = 'r';
        }else if( c=='\t' ){
          zTrans[j++] = 't';
        }else{
          zTrans[j++] = '0' + ((c>>6)&7);
          zTrans[j++] = '0' + ((c>>3)&7);
          zTrans[j++] = '0' + (c&7);
        }
      }
    }
    zTrans[j] = 0;
    if( i<szValue ){
      sqlite3_snprintf(sizeof(zTrans)-j, (char*)zTrans+j, "...+%d", szValue-i);
      j += (int)strlen((char*)zTrans+j);
    }
    if( showMtime ){
      fossil_print("%s:%*s%s\n", zName, 58-nName, "", zMTime);
    }else if( nName<MX_NM-2 ){
      fossil_print("%s:%*s%s\n", zName, MX_NM-1-nName, "", zTrans);
    }else if( nName<MX_LONGNM-2 && j<10 ){
      fossil_print("%s:%*s%s\n", zName, MX_LONGNM-1-nName, "", zTrans);
    }else{
      fossil_print("%s:\n%*s%s\n", zName, MX_NM, "", zTrans);
    }
  }
  db_finalize(&q);
  if( zPattern && doUnset ){
    prompt_user("Delete all of the above? (y/N)? ", &ans);
    if( blob_str(&ans)[0]=='y' || blob_str(&ans)[0]=='Y' ){
      db_multi_exec("DELETE FROM config WHERE name GLOB %Q", zPattern);
    }
    blob_reset(&ans);
  }
}

/*
** COMMAND: test-var-get
**
** Usage: %fossil test-var-get VAR ?FILE?
**
** Write the text of the VAR variable into FILE.  If FILE is "-"
** or is omitted then output goes to standard output.  VAR can be a
** GLOB pattern.
**
** If not in an open check-out, use the -R REPO option to specify a
** a repository.
*/
void test_var_get_cmd(void){
  const char *zVar;
  const char *zFile;
  int n;
  Blob x;
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  verify_all_options();
  if( g.argc<3 ){
    usage("VAR ?FILE?");
  }
  zVar = g.argv[2];
  zFile = g.argc>=4 ? g.argv[3] : "-";
  n = db_int(0, "SELECT count(*) FROM config WHERE name GLOB %Q", zVar);
  if( n==0 ){
    fossil_fatal("no match for %Q", zVar);
  }
  if( n>1 ){
    fossil_fatal("multiple matches: %s",
      db_text(0, "SELECT group_concat(quote(name),', ') FROM ("
                 " SELECT name FROM config WHERE name GLOB %Q ORDER BY 1)",
                 zVar));
  }
  blob_init(&x,0,0);
  db_blob(&x, "SELECT value FROM config WHERE name GLOB %Q", zVar);
  blob_write_to_file(&x, zFile);
}

/*
** COMMAND: test-var-set
**
** Usage: %fossil test-var-set VAR ?VALUE? ?--file FILE?
**
** Store VALUE or the content of FILE (exactly one of which must be
** supplied) into variable VAR.  Use a FILE of "-" to read from
** standard input.
**
** WARNING: changing the value of a variable can interfere with the
** operation of Fossil.  Be sure you know what you are doing.
**
** Use "--blob FILE" instead of "--file FILE" to load a binary blob
** such as a GIF.
*/
void test_var_set_cmd(void){
  const char *zVar;
  const char *zFile;
  const char *zBlob;
  Blob x;
  Stmt ins;
  zFile = find_option("file",0,1);
  zBlob = find_option("blob",0,1);
  db_find_and_open_repository(OPEN_ANY_SCHEMA, 0);
  verify_all_options();
  if( g.argc<3 || (zFile==0 && zBlob==0 && g.argc<4) ){
    usage("VAR ?VALUE? ?--file FILE?");
  }
  zVar = g.argv[2];
  if( zFile ){
    if( zBlob ) fossil_fatal("cannot do both --file or --blob");
    blob_read_from_file(&x, zFile);
  }else if( zBlob ){
    blob_read_from_file(&x, zBlob);
  }else{
    blob_init(&x,g.argv[3],-1);
  }
  db_prepare(&ins,
     "REPLACE INTO config(name,value,mtime)"
     "VALUES(%Q,:val,now())", zVar);
  if( zBlob ){
    db_bind_blob(&ins, ":val", &x);
  }else{
    db_bind_text(&ins, ":val", blob_str(&x));
  }
  db_step(&ins);
  db_finalize(&ins);
  blob_reset(&x);
}
