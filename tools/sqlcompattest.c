/*
** Copyright (c) 2019 D. Richard Hipp
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
** This file is NOT part of the Fossil executable. It is called from
** auto.def in the autosetup system.
**
** This file contains a test program used by ../configure with the
** the --disable-internal-sqlite option to determine whether or
** not the system SQLite library is sufficient to support Fossil.
**
** This must be compiled with -D MINIMUM_SQLITE_VERSION set in auto.def.
**
** It is preferred to statically link Fossil with the sqlite3.c source
** file that is part of the source tree and not use any SQLite shared
** library that is included with the system.  But some packagers do not
** like to do this.  Hence, we provide the option to link Fossil against
** the system SQLite shared library.  But Fossil is very particular about
** the version and build options for SQLite.  Unless a recent version of
** SQLite is available, and unless that SQLite is built using some
** non-default features, the system library won't meet the needs of
** Fossil.  This program attempts to determine if the system library
** SQLite is sufficient for Fossil.
**
** Compile this program, linking it against the system SQLite library,
** and run it.  If it returns with a zero exit code, then all is well.
** But if it returns a non-zero exit code, then the system SQLite library
** lacks some capability that Fossil uses.  A message on stdout describes
** the missing feature.
*/
#include "sqlite3.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv){

#if !defined(MINIMUM_SQLITE_VERSION)
#error "Must set -DMINIMUM_SQLITE_VERSION=nn.nn.nn in auto.def"
#endif

#define QUOTE(VAL) #VAL
#define STR(MACRO_VAL) QUOTE(MACRO_VAL)

  char zMinimumVersionNumber[8]="nn.nn.nn";
  strncpy((char *)&zMinimumVersionNumber,STR(MINIMUM_SQLITE_VERSION),
          sizeof(zMinimumVersionNumber));

  long major, minor, release, version;
  sscanf(zMinimumVersionNumber, "%li.%li.%li", &major, &minor, &release);
  version=(major*1000000)+(minor*1000)+release;

  int i;
  static const char *zRequiredOpts[] = {
    "ENABLE_FTS4",        /* Required for repository search */
    "ENABLE_DBSTAT_VTAB", /* Required by /repo-tabsize page */
  };

  /* Check minimum SQLite version number */
  if( sqlite3_libversion_number()<version ){
    printf("found system SQLite version %s but need %s or later, "
           "consider removing --disable-internal-sqlite\n",
            sqlite3_libversion(),STR(MINIMUM_SQLITE_VERSION));
    return 1;
  }

  for(i=0; i<sizeof(zRequiredOpts)/sizeof(zRequiredOpts[0]); i++){
    if( !sqlite3_compileoption_used(zRequiredOpts[i]) ){
      printf("system SQLite library omits required build option -DSQLITE_%s\n",
             zRequiredOpts[i]);
      return 1;
    }
  }

  /* Success! */
  return 0;
}
