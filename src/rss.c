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
** This file contains code used to create a RSS feed for the CGI interface.
*/
#include "config.h"
#include "rss.h"
#include <assert.h>
#include <time.h>

time_t rss_datetime_to_time_t(const char *dt){
  struct tm the_tm;

  the_tm.tm_year = atoi(dt)-1900;
  the_tm.tm_mon  = atoi(&dt[5])-1;
  the_tm.tm_mday = atoi(&dt[8]);
  the_tm.tm_hour = atoi(&dt[11]);
  the_tm.tm_min  = atoi(&dt[14]);
  the_tm.tm_sec  = atoi(&dt[17]);

  return mktime(&the_tm);
}

/*
** WEBPAGE: timeline.rss
*/

void page_timeline_rss(void){
  Stmt q;
  int nLine=0;
  char *zPubDate, *zProjectName, *zProjectDescr, *zFreeProjectName=0;
  Blob bSQL;
  const char *zType = PD("y","all"); /* Type of events.  All if NULL */
  const char zSQL1[] =
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   datetime(event.mtime),
    @   coalesce(ecomment,comment),
    @   coalesce(euser,user),
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid)
    @ FROM event, blob
    @ WHERE blob.rid=event.objid
  ;
  blob_zero(&bSQL);
  blob_append( &bSQL, zSQL1, -1 );
  
  if( zType[0]!='a' ){
      blob_appendf(&bSQL, " AND event.type=%Q", zType);
  }

  blob_append( &bSQL, " ORDER BY event.mtime DESC", -1 );

  cgi_set_content_type("application/rss+xml");

  zProjectName = db_get("project-name", 0);
  if( zProjectName==0 ){
    zFreeProjectName = zProjectName = mprintf("Fossil source repository for: %s",
      g.zBaseURL);
  }
  zProjectDescr = db_get("project-description", 0);
  if( zProjectDescr==0 ){
    zProjectDescr = zProjectName;
  }

  zPubDate = cgi_rfc822_datestamp(time(NULL));

  @ <?xml version="1.0"?>
  @ <rss version="2.0">
  @   <channel>
  @     <title>%s(zProjectName)</title>
  @     <link>%s(g.zBaseURL)</link>
  @     <description>%s(zProjectDescr)</description>
  @     <pubDate>%s(zPubDate)</pubDate>
  @     <generator>Fossil version %s(MANIFEST_VERSION) %s(MANIFEST_DATE)</generator>
  db_prepare(&q, blob_buffer(&bSQL));
  blob_reset( &bSQL );
  while( db_step(&q)==SQLITE_ROW && nLine<=20 ){
    const char *zId = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zCom = db_column_text(&q, 3);
    const char *zAuthor = db_column_text(&q, 4);
    char *zPrefix = "";
    int nChild = db_column_int(&q, 5);
    int nParent = db_column_int(&q, 6);

    zDate = cgi_rfc822_datestamp(rss_datetime_to_time_t(zDate));

    if( nParent>1 && nChild>1 ){
      zPrefix = "*MERGE/FORK* ";
    }else if( nParent>1 ){
      zPrefix = "*MERGE* ";
    }else if( nChild>1 ){
      zPrefix = "*FORK* ";
    }

    @     <item>
    @       <title>%s(zPrefix)%s(zCom)</title>
    @       <link>%s(g.zBaseURL)/vinfo/%s(zId)</link>
    @       <description>%s(zPrefix)%s(zCom)</description>
    @       <pubDate>%s(zDate)</pubDate>
    @       <author>%s(zAuthor)</author>
    @       <guid>%s(g.zBaseURL)/vinfo/%s(zId)</guid>
    @     </item>
    nLine++;
  }

  db_finalize(&q);
  @   </channel>
  @ </rss>

  if( zFreeProjectName != 0 ){
    free( zFreeProjectName );
  }
}
