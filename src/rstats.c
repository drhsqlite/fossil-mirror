/*
** Copyright (c) 2008 D. Richard Hipp, Kevin Kinnell
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
**   kkinnell@megagate.com
**
*******************************************************************************
**
** This file contains code to implement the metrics command.  This is a
** command-line version of the stats page.
**
*/
#include <string.h>
#include "config.h"
#include "metrics.h"

/*
** COMMAND: rstats
**
** Usage: %fossil rstats
**
** Deliver a report of the repository statistics for the
** current checkout.
*/
void rstats_cmd(void){
  i64 t;
  int n, m, fsize, vid;
  char zBuf[100];

  db_must_be_within_tree();
  vid = db_lget_int("checkout",0);
  if( vid==0 ){
    fossil_panic("no checkout");
  }
  fsize = file_size(g.zRepositoryName);
  n = db_int(0, "SELECT count(*) FROM blob");
  m = db_int(0, "SELECT count(*) FROM delta");
  printf(" Number of Artifacts: %d\n", n);
  printf("  %d full text + %d delta blobs\n", (n-m), m);
  if( n>0 ){
    int a, b;
    t = db_int64(0, "SELECT total(size) FROM blob WHERE size>0");
    sqlite3_snprintf(sizeof(zBuf), zBuf, "%lld", t);
    if( t/fsize < 5 ){
      b = 10;
      fsize /= 10;
    }else{
      b = 1;
    }
    a = t/fsize;
    printf(" %d bytes average, %s bytes total\n\n",
	   ((int)(((double)t)/(double)n)),
	   (zBuf));
  }
  n = db_int(0, "SELECT count(distinct mid) FROM mlink");
  printf("  Number Of Checkins: %d\n", n);
  n = db_int(0, "SELECT count(*) FROM filename");
  printf("     Number Of Files: %d\n", n);
  n = db_int(0, "SELECT count(*) FROM tag WHERE +tagname GLOB 'wiki-*'");
  printf("Number Of Wiki Pages: %d\n", n);
  n = db_int(0, "SELECT count(*) FROM tag WHERE +tagname GLOB 'tkt-*'");
  printf("   Number Of Tickets: %d\n", n);
  n = db_int(0, "SELECT julianday('now') - (SELECT min(mtime) FROM event) + 0.99");
  printf(" Duration Of Project: %d days\n", n);
}
