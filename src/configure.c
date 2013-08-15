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

#define CONFIGSET_ALL       0x0000ff     /* Everything */

#define CONFIGSET_OVERWRITE 0x100000     /* Causes overwrite instead of merge */
#define CONFIGSET_OLDFORMAT 0x200000     /* Use the legacy format */

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
  { "logo-mimetype",          CONFIGSET_SKIN },
  { "logo-image",             CONFIGSET_SKIN },
  { "background-mimetype",    CONFIGSET_SKIN },
  { "background-image",       CONFIGSET_SKIN },
  { "index-page",             CONFIGSET_SKIN },
  { "timeline-block-markup",  CONFIGSET_SKIN },
  { "timeline-max-comment",   CONFIGSET_SKIN },
  { "timeline-plaintext",     CONFIGSET_SKIN },
  { "adunit",                 CONFIGSET_SKIN },
  { "adunit-omit-if-admin",   CONFIGSET_SKIN },
  { "adunit-omit-if-user",    CONFIGSET_SKIN },
  { "th1-setup",              CONFIGSET_ALL },

#ifdef FOSSIL_ENABLE_TCL
  { "tcl",                    CONFIGSET_SKIN|CONFIGSET_TKT|CONFIGSET_XFER },
  { "tcl-setup",              CONFIGSET_SKIN|CONFIGSET_TKT|CONFIGSET_XFER },
#endif

  { "project-name",           CONFIGSET_PROJ },
  { "project-description",    CONFIGSET_PROJ },
  { "manifest",               CONFIGSET_PROJ },
  { "binary-glob",            CONFIGSET_PROJ },
  { "clean-glob",             CONFIGSET_PROJ },
  { "ignore-glob",            CONFIGSET_PROJ },
  { "keep-glob",              CONFIGSET_PROJ },
  { "crnl-glob",              CONFIGSET_PROJ },
  { "encoding-glob",          CONFIGSET_PROJ },
  { "empty-dirs",             CONFIGSET_PROJ },
  { "allow-symlinks",         CONFIGSET_PROJ },

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

  { "xfer-common-script",     CONFIGSET_XFER },
  { "xfer-push-script",       CONFIGSET_XFER },

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
  if( iMask & CONFIGSET_OLDFORMAT ){
    while( iConfig<count(aConfig) ){
      if( aConfig[iConfig].groupMask & iMask ){
        return aConfig[iConfig++].zName;
      }else{
        iConfig++;
      }
    }
  }else{
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
  blob_append(&x, "(", 1);
  for(i=0; i<count(aConfig); i++){
    if( (aConfig[i].groupMask & iMask)==0 ) continue;
    if( aConfig[i].zName[0]=='@' ) continue;
    blob_appendf(&x, "%s'%s'", zSep, aConfig[i].zName);
    zSep = ",";
  }
  blob_append(&x, ")", 1);
  return blob_str(&x);
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
    if( memcmp(zName, aConfig[i].zName, n)==0 && aConfig[i].zName[n]==0 ){
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
  if( fossil_strcmp(zName, "@shun")==0 ){
    db_prepare(&q, "SELECT uuid FROM shun");
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(pOut, "INSERT OR IGNORE INTO shun VALUES('%s');\n",
        db_column_text(&q, 0)
      );
    }
    db_finalize(&q);
  }else if( fossil_strcmp(zName, "@reportfmt")==0 ){
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
  }else if( fossil_strcmp(zName, "@user")==0 ){
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
  }else if( fossil_strcmp(zName, "@concealed")==0 ){
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
**        config_is_reset(int)
**        config_reset(int)
**
** The config_is_reset() function takes the integer valued argument and
** ANDs it against the static variable "configHasBeenReset" below.  The
** function returns TRUE or FALSE depending on the result depending on
** whether or not the corresponding configuration table has been reset.  The
** config_reset() function adds the bits to "configHasBeenReset" that
** are given in the argument.
**
** These functions are used below in the WHEN clause of a trigger to
** get the trigger to fire exactly once.
*/
static int configHasBeenReset = 0;
static void config_is_reset_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int m = sqlite3_value_int(argv[0]);
  sqlite3_result_int(context, (configHasBeenReset&m)!=0 );
}
static void config_reset_function(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int m = sqlite3_value_int(argv[0]);
  configHasBeenReset |= m;
}

/*
** Create the temporary _xfer_reportfmt and _xfer_user tables that are
** necessary in order to evaluate the SQL text generated by the
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
    @ INSERT INTO _xfer_reportfmt
    @    SELECT rn,owner,title,cols,sqlcode FROM reportfmt;
    @ INSERT INTO _xfer_user
    @    SELECT uid,login,pw,cap,cookie,ipaddr,cexpire,info,photo FROM user;
  ;
  db_multi_exec(zSQL1);

  /* When the replace flag is set, add triggers that run the first time
  ** that new data is seen.  The triggers run only once and delete all the
  ** existing data.
  */
  if( replaceFlag ){
    static const char zSQL2[] =
      @ CREATE TRIGGER _xfer_r1 BEFORE INSERT ON _xfer_reportfmt
      @ WHEN NOT config_is_reset(2) BEGIN
      @   DELETE FROM _xfer_reportfmt;
      @   SELECT config_reset(2);
      @ END;
      @ CREATE TRIGGER _xfer_r2 BEFORE INSERT ON _xfer_user
      @ WHEN NOT config_is_reset(16) BEGIN
      @   DELETE FROM _xfer_user;
      @   SELECT config_reset(16);
      @ END;
      @ CREATE TEMP TRIGGER _xfer_r3 BEFORE INSERT ON shun
      @ WHEN NOT config_is_reset(8) BEGIN
      @   DELETE FROM shun;
      @   SELECT config_reset(8);
      @ END;
    ;
    sqlite3_create_function(g.db, "config_is_reset", 1, SQLITE_UTF8, 0,
         config_is_reset_function, 0, 0);
    sqlite3_create_function(g.db, "config_reset", 1, SQLITE_UTF8, 0,
         config_reset_function, 0, 0);
    configHasBeenReset = 0;
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
        db_multi_exec("DELETE FROM %s", &aType[ii].zName[1]);
        configHasBeenReset |= thisMask;
      }
      blob_append(&sql, "REPLACE INTO ", -1);
    }else{
      blob_append(&sql, "INSERT OR IGNORE INTO ", -1);
    }
    blob_appendf(&sql, "%s(%s, mtime", &zName[1], aType[ii].zPrimKey);
    for(jj=2; jj<nToken; jj+=2){
       blob_appendf(&sql, ",%s", azToken[jj]);
    }
    blob_appendf(&sql,") VALUES(%s,%s", azToken[1], azToken[0]);
    for(jj=2; jj<nToken; jj+=2){
       blob_appendf(&sql, ",%s", azToken[jj+1]);
    }
    db_multi_exec("%s)", blob_str(&sql));
    if( db_changes()==0 ){
      blob_reset(&sql);
      blob_appendf(&sql, "UPDATE %s SET mtime=%s", &zName[1], azToken[0]);
      for(jj=2; jj<nToken; jj+=2){
        blob_appendf(&sql, ", %s=%s", azToken[jj], azToken[jj+1]);
      }
      blob_appendf(&sql, " WHERE %s=%s AND mtime<%s",
                   aType[ii].zPrimKey, azToken[1], azToken[0]);
      db_multi_exec("%s", blob_str(&sql));
    }
    blob_reset(&sql);
  }else{
    /* Otherwise, the old format */
    if( (configure_is_exportable(zName) & groupMask)==0 ) return;
    if( fossil_strcmp(zName, "logo-image")==0 ){
      Stmt ins;
      db_prepare(&ins,
        "REPLACE INTO config(name, value, mtime) VALUES(:name, :value, now())"
      );
      db_bind_text(&ins, ":name", zName);
      db_bind_blob(&ins, ":value", pContent);
      db_step(&ins);
      db_finalize(&ins);
    }else if( zName[0]=='@' ){
      /* Notice that we are evaluating arbitrary SQL received from the
      ** client.  But this can only happen if the client has authenticated
      ** as an administrator, so presumably we trust the client at this
      ** point.
      */
      db_multi_exec("%s", blob_str(pContent));
    }else{
      db_multi_exec(
         "REPLACE INTO config(name,value,mtime) VALUES(%Q,%Q,now())",
         zName, blob_str(pContent)
      );
    }
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
** accept the -R or --repository option to specific a repository.
**
**    %fossil configuration export AREA FILENAME
**
**         Write to FILENAME exported configuration information for AREA.
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
**         server is used. Use the --legacy option for the older protocol
**         (when talking to servers compiled prior to 2011-04-27.)  Use
**         the --overwrite flag to completely replace local settings with
**         content received from URL.
**
**    %fossil configuration push AREA ?URL?
**
**         Push the local configuration into the remote server identified
**         by URL.  Admin privilege is required on the remote server for
**         this to work.  When the same record exists both locally and on
**         the remote end, the one that was most recently changed wins.
**         Use the --legacy flag when talking to holder servers.
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
  if( g.argc<3 ){
    usage("export|import|merge|pull|reset ...");
  }
  db_find_and_open_repository(0, 0);
  db_open_config(0);
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
    int legacyFlag = 0;
    int overwriteFlag = 0;

    if( zMethod[0]!='s' ) legacyFlag = find_option("legacy",0,0)!=0;
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
    if( g.urlProtocol==0 ) fossil_fatal("no server URL specified");
    user_select();
    url_enable_proxy("via proxy: ");
    if( legacyFlag ) mask |= CONFIGSET_OLDFORMAT;
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
        db_multi_exec(zRepositorySchemaDefaultReports);
      }
    }
    db_end_transaction(0);
    fossil_print("Configuration reset to factory defaults.\n");
    fossil_print("To recover, use:  %s %s import %s\n",
            g.argv[0], g.argv[1], zBackup);
  }else
  {
    fossil_fatal("METHOD should be one of:"
                 " export import merge pull push reset");
  }
}
