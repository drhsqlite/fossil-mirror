/*
** Copyright (c) 2008 D. Richard Hipp
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

#define CONFIGSET_ALL    0xffffff     /* Everything */

#endif /* INTERFACE */

/*
** Names of the configuration sets
*/
static struct {
  const char *zName;   /* Name of the configuration set */
  int groupMask;       /* Mask for that configuration set */
} aGroupName[] = {
  { "skin",         CONFIGSET_SKIN },
  { "ticket",       CONFIGSET_TKT  },
  { "project",      CONFIGSET_PROJ },
  { "all",          CONFIGSET_ALL  },
};


/*
** The following is a list of settings that we are willing to
** transfer.
*/
static struct {
  const char *zName;   /* Name of the configuration parameter */
  int groupMask;       /* Which config groups is it part of */
} aConfig[] = {
  { "css",                   CONFIGSET_SKIN },
  { "header",                CONFIGSET_SKIN },
  { "footer",                CONFIGSET_SKIN },
  { "project-name",          CONFIGSET_PROJ },
  { "project-description",   CONFIGSET_PROJ },
  { "index-page",            CONFIGSET_SKIN },
  { "timeline-block-markup", CONFIGSET_SKIN },
  { "timeline-max-comment",  CONFIGSET_SKIN },
  { "ticket-table",          CONFIGSET_TKT  },
  { "ticket-common",         CONFIGSET_TKT  },
  { "ticket-newpage",        CONFIGSET_TKT  },
  { "ticket-viewpage",       CONFIGSET_TKT  },
  { "ticket-editpage",       CONFIGSET_TKT  },
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
** Return TRUE if a particular configuration parameter zName is
** safely exportable.
*/
int configure_is_exportable(const char *zName){
  int i;
  for(i=0; i<count(aConfig); i++){
    if( strcmp(zName, aConfig[i].zName)==0 ){
      return aConfig[i].groupMask;
    }
  }
  return 0;
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
  fossil_fatal("no such configuration area: \"%s\"", z);
  return 0;
}


/*
** COMMAND: configuration
**
** Usage: %fossil configure METHOD ...
**
** Where METHOD is one of: export import pull reset.  All methods
** accept the -R or --repository option to specific a repository.
**
**    %fossil configuration export AREA FILENAME
**
**         Write to FILENAME exported configuraton information for AREA.
**         AREA can be one of:  all ticket skin project
**
**    %fossil configuration import FILENAME
**
**         Read a configuration from FILENAME, overwriting the current
**         configuration.  Warning:  Do not read a configuration from
**         an untrusted source since the configuration is not checked
**         for safety and can introduce security threats.
**
**    %fossil configuration pull AREA URL
**
**         Pull and install the configuration from a different server
**         identified by URL.  AREA is as in "export".
**
**    %fossil configuration reset AREA
**
**         Restore the configuration to the default.  AREA as above.
*/
void configuration_cmd(void){
  int n;
  const char *zMethod;
  if( g.argc<3 ){
    usage("METHOD ...");
  }
  db_find_and_open_repository(1);
  zMethod = g.argv[2];
  n = strlen(zMethod);
  if( strncmp(zMethod, "export", n)==0 ){
    int i;
    int mask;
    const char *zSep;
    Blob sql;
    Stmt q;
    Blob out;
    if( g.argc!=5 ){
      usage("export AREA FILENAME");
    }
    mask = find_area(g.argv[3]);
    blob_zero(&sql);
    blob_zero(&out);
    blob_appendf(&sql, 
       "SELECT 'REPLACE INTO config(name,value) VALUES('''"
       "         || name || ''',' || quote(value) || ');'"
       "  FROM config WHERE name IN "
    );
    zSep = "(";
    for(i=0; i<count(aConfig); i++){
      if( aConfig[i].groupMask & mask ){
        blob_appendf(&sql, "%s'%s'", zSep, aConfig[i].zName);
        zSep = ",";
      }
    }
    blob_appendf(&sql, ") ORDER BY name");
    db_prepare(&q, blob_str(&sql));
    blob_reset(&sql);
    blob_appendf(&out, 
        "-- The \"%s\" configuration exported from\n"
        "-- repository \"%s\"\n"
        "-- on %s\n",
        g.argv[3], g.zRepositoryName,
        db_text(0, "SELECT datetime('now')")
    );
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&out, "%s\n", db_column_text(&q, 0));
    }
    db_finalize(&q);
    blob_write_to_file(&out, g.argv[4]);
    blob_reset(&out);
  }else
  if( strncmp(zMethod, "import", n)==0 ){
    Blob in;
    if( g.argc!=4 ) usage("import FILENAME");
    blob_read_from_file(&in, g.argv[3]);
    db_begin_transaction();
    db_multi_exec("%s", blob_str(&in));
    db_end_transaction(0);
  }else
  if( strncmp(zMethod, "pull", n)==0 ){
    int mask;
    url_proxy_options();
    if( g.argc!=5 ) usage("pull AREA URL");
    mask = find_area(g.argv[3]);
    url_parse(g.argv[4]);
    if( g.urlIsFile ){
      fossil_fatal("network sync only");
    }
    user_select();
    client_sync(0,0,0,mask);
  }else
  if( strncmp(zMethod, "reset", n)==0 ){
    int mask, i;
    if( g.argc!=4 ) usage("reset AREA");
    mask = find_area(g.argv[3]);
    db_begin_transaction();
    for(i=0; i<count(aConfig); i++){
      if( (aConfig[i].groupMask & mask)==0 ) continue;
      db_multi_exec("DELETE FROM config WHERE name=%Q", aConfig[i].zName);
    }
    db_end_transaction(0);
  }else
  {
    fossil_fatal("METHOD should be one of:  export import pull reset");
  }
}
