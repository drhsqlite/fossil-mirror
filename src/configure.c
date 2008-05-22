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
} aSafeConfig[] = {
  { "css",                   CONFIGSET_SKIN },
  { "header",                CONFIGSET_SKIN },
  { "footer",                CONFIGSET_SKIN },
  { "project-name",          CONFIGSET_PROJ },
  { "project-description",   CONFIGSET_PROJ },
  { "index-page",            CONFIGSET_SKIN },
  { "timeline-block-markup", CONFIGSET_SKIN },
  { "timeline-max-comment",  CONFIGSET_SKIN },
};

/*
** Return TRUE if a particular configuration parameter zName is
** safely exportable.
*/
int configure_is_exportable(const char *zName){
  int i;
  for(i=0; i<count(aSafeConfig); i++){
    if( strcmp(zName, aSafeConfig[i].zName)==0 ) return 1;
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
** COMMAND: configure
**
** Usage: %fossil configure METHOD ...
**
** Where METHOD is one of: export import pull reset.  All methods
** accept the -R or --repository option to specific a repository.
**
**    %fossil config export AREA FILENAME
**
**         Write to FILENAME exported configuraton information for AREA.
**         AREA can be one of:  all ticket skin project
**
**    %fossil config import FILENAME
**
**         Read a configuration from FILENAME, overwriting the current
**         configuration.  Warning:  Do not read a configuration from
**         an untrusted source since the configuration is not checked
**         for safety and can introduce security threats.
**
**    %fossil config pull AREA URL
**
**         Pull and install the configuration from a different server
**         identified by URL.  AREA is as in "export".
**
**    %fossil configure reset AREA
**
**         Restore the configuration to the default.  AREA as above.
*/
void configure_cmd(void){
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
    for(i=0; i<count(aSafeConfig); i++){
      if( aSafeConfig[i].groupMask & mask ){
        blob_appendf(&sql, "%s'%s'", zSep, aSafeConfig[i].zName);
        zSep = ",";
      }
    }
    blob_appendf(&sql, ") ORDER BY name");
    db_prepare(&q, blob_str(&sql));
    blob_reset(&sql);
    blob_appendf(&out, 
        "-- The \"%s\" configuration exported from\n"
        "-- repository \"%s\"\n"
        "-- on %s\nBEGIN;\n",
        g.argv[3], g.zRepositoryName,
        db_text(0, "SELECT datetime('now')")
    );
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&out, "%s\n", db_column_text(&q, 0));
    }
    db_finalize(&q);
    blob_appendf(&out, "COMMIT;\n");
    blob_write_to_file(&out, g.argv[4]);
    blob_reset(&out);
  }else
  if( strncmp(zMethod, "import", n)==0 ){
  }else
  if( strncmp(zMethod, "pull", n)==0 ){
  }else
  if( strncmp(zMethod, "reset", n)==0 ){
  }else
  {
    fossil_fatal("METHOD should be one of:  export import pull reset");
  }
}
