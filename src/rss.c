/*
** Copyright (c) 2007 D. Richard Hipp
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
** This file contains code used to create a RSS feed for the CGI interface.
*/
#include "config.h"
#include <time.h>
#include "rss.h"
#include <assert.h>

/*
** WEBPAGE: timeline.rss
** URL:  /timeline.rss?y=TYPE&n=LIMIT&tkt=UUID&tag=TAG&wiki=NAME&name=FILENAME
**
** Produce an RSS feed of the timeline.
**
** TYPE may be: all, ci (show checkins only), t (show tickets only),
** w (show wiki only). LIMIT is the number of items to show.
**
** tkt=UUID filters for only those events for the specified ticket. tag=TAG
** filters for a tag, and wiki=NAME for a wiki page. Only one may be used.
**
** In addition, name=FILENAME filters for a specific file. This may be
** combined with one of the other filters (useful for looking at a specific
** branch).
*/

void page_timeline_rss(void){
  Stmt q;
  int nLine=0;
  char *zPubDate, *zProjectName, *zProjectDescr, *zFreeProjectName=0;
  Blob bSQL;
  const char *zType = PD("y","all"); /* Type of events.  All if NULL */
  const char *zTicketUuid = PD("tkt",NULL);
  const char *zTag = PD("tag",NULL);
  const char *zFilename = PD("name",NULL);
  const char *zWiki = PD("wiki",NULL);
  int nLimit = atoi(PD("n","20"));
  int nTagId;
  const char zSQL1[] =
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   event.mtime,
    @   coalesce(ecomment,comment),
    @   coalesce(euser,user),
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid)
    @ FROM event, blob
    @ WHERE blob.rid=event.objid
  ;

  login_check_credentials();
  if( !g.perm.Read && !g.perm.RdTkt && !g.perm.RdWiki ){ 
    return;
  }

  blob_zero(&bSQL);
  blob_append( &bSQL, zSQL1, -1 );

  if( zType[0]!='a' ){
    if( zType[0]=='c' && !g.perm.Read ) zType = "x";
    if( zType[0]=='w' && !g.perm.RdWiki ) zType = "x";
    if( zType[0]=='t' && !g.perm.RdTkt ) zType = "x";
    blob_appendf(&bSQL, " AND event.type=%Q", zType);
  }else{
    if( !g.perm.Read ){
      if( g.perm.RdTkt && g.perm.RdWiki ){
        blob_append(&bSQL, " AND event.type!='ci'", -1);
      }else if( g.perm.RdTkt ){
        blob_append(&bSQL, " AND event.type=='t'", -1);
        
      }else{
        blob_append(&bSQL, " AND event.type=='w'", -1);
      }
    }else if( !g.perm.RdWiki ){
      if( g.perm.RdTkt ){
        blob_append(&bSQL, " AND event.type!='w'", -1);
      }else{
        blob_append(&bSQL, " AND event.type=='ci'", -1);
      }
    }else if( !g.perm.RdTkt ){
      assert( !g.perm.RdTkt &&& g.perm.Read && g.perm.RdWiki );
      blob_append(&bSQL, " AND event.type!='t'", -1);
    }
  }

  if( zTicketUuid ){
    nTagId = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'tkt-%q*'",
      zTicketUuid);
    if ( nTagId==0 ){
      nTagId = -1;
    }
  }else if( zTag ){
    nTagId = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'sym-%q*'",
      zTag);
    if ( nTagId==0 ){
      nTagId = -1;
    }
  }else if( zWiki ){
    nTagId = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'wiki-%q*'",
      zWiki);
    if ( nTagId==0 ){
      nTagId = -1;
    }
  }else{
    nTagId = 0;
  }

  if( nTagId==-1 ){
    blob_appendf(&bSQL, " AND 0");
  }else if( nTagId!=0 ){
    blob_appendf(&bSQL, " AND (EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid))", nTagId);
  }

  if( zFilename ){
    blob_appendf(&bSQL,
      " AND (SELECT mlink.fnid FROM mlink WHERE event.objid=mlink.mid) IN (SELECT fnid FROM filename WHERE name=%Q %s)",
        zFilename, filename_collation()
    );
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
  @ <rss xmlns:dc="http://purl.org/dc/elements/1.1/" version="2.0">
  @   <channel>
  @     <title>%h(zProjectName)</title>
  @     <link>%s(g.zBaseURL)</link>
  @     <description>%h(zProjectDescr)</description>
  @     <pubDate>%s(zPubDate)</pubDate>
  @     <generator>Fossil version %s(MANIFEST_VERSION) %s(MANIFEST_DATE)</generator>
  free(zPubDate);
  db_prepare(&q, blob_str(&bSQL));
  blob_reset( &bSQL );
  while( db_step(&q)==SQLITE_ROW && nLine<nLimit ){
    const char *zId = db_column_text(&q, 1);
    const char *zCom = db_column_text(&q, 3);
    const char *zAuthor = db_column_text(&q, 4);
    char *zPrefix = "";
    char *zDate;
    int nChild = db_column_int(&q, 5);
    int nParent = db_column_int(&q, 6);
    time_t ts;

    ts = (time_t)((db_column_double(&q,2) - 2440587.5)*86400.0);
    zDate = cgi_rfc822_datestamp(ts);

    if( nParent>1 && nChild>1 ){
      zPrefix = "*MERGE/FORK* ";
    }else if( nParent>1 ){
      zPrefix = "*MERGE* ";
    }else if( nChild>1 ){
      zPrefix = "*FORK* ";
    }

    @     <item>
    @       <title>%s(zPrefix)%h(zCom)</title>
    @       <link>%s(g.zBaseURL)/info/%s(zId)</link>
    @       <description>%s(zPrefix)%h(zCom)</description>
    @       <pubDate>%s(zDate)</pubDate>
    @       <dc:creator>%h(zAuthor)</dc:creator>
    @       <guid>%s(g.zBaseURL)/info/%s(zId)</guid>
    @     </item>
    free(zDate);
    nLine++;
  }

  db_finalize(&q);
  @   </channel>
  @ </rss>

  if( zFreeProjectName != 0 ){
    free( zFreeProjectName );
  }
}

/*
** COMMAND: rss
**
** The CLI variant of the /timeline.rss page, this produces an RSS
** feed of the timeline to stdout. Options:
**
** -type|y FLAG
**    may be: all (default), ci (show checkins only), t (show tickets only),
**    w (show wiki only). LIMIT is the number of items to show.
**
** -tkt UUID
**    Filters for only those events for the specified ticket.
**
** -tag TAG
**    filters for a tag
**
** -wiki NAME
**   Filters on a specific wiki page.
**
** Only one of -tkt, -tag, or -wiki may be used.
**
** -name FILENAME
**   filters for a specific file. This may be combined with one of the other
**   filters (useful for looking at a specific branch).
**
** -url STRING
**   Sets the RSS feed's root URL to the given string. The default is
** "URL-PLACEHOLDER" (without quotes).
*/
void cmd_timeline_rss(void){
  Stmt q;
  int nLine=0;
  char *zPubDate, *zProjectName, *zProjectDescr, *zFreeProjectName=0;
  Blob bSQL;
  const char *zType = find_option("type","y",1); /* Type of events.  All if NULL */
  const char *zTicketUuid = find_option("tkt",NULL,1);
  const char *zTag = find_option("tag",NULL,1);
  const char *zFilename = find_option("name",NULL,1);
  const char *zWiki = find_option("wiki",NULL,1);
  const char *zLimit = find_option("limit", "n",1);
  const char *zBaseURL = find_option("url", NULL, 1);
  int nLimit = atoi( (zLimit && *zLimit) ? zLimit : "20" );
  int nTagId;
  const char zSQL1[] =
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   event.mtime,
    @   coalesce(ecomment,comment),
    @   coalesce(euser,user),
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid)
    @ FROM event, blob
    @ WHERE blob.rid=event.objid
  ;
  if(!zType || !*zType){
    zType = "all";
  }
  if(!zBaseURL || !*zBaseURL){
    zBaseURL = "URL-PLACEHOLDER";
  }

  db_find_and_open_repository(0, 0);

  blob_zero(&bSQL);
  blob_append( &bSQL, zSQL1, -1 );

  if( zType[0]!='a' ){
    blob_appendf(&bSQL, " AND event.type=%Q", zType);
  }

  if( zTicketUuid ){
    nTagId = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'tkt-%q*'",
      zTicketUuid);
    if ( nTagId==0 ){
      nTagId = -1;
    }
  }else if( zTag ){
    nTagId = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'sym-%q*'",
      zTag);
    if ( nTagId==0 ){
      nTagId = -1;
    }
  }else if( zWiki ){
    nTagId = db_int(0, "SELECT tagid FROM tag WHERE tagname GLOB 'wiki-%q*'",
      zWiki);
    if ( nTagId==0 ){
      nTagId = -1;
    }
  }else{
    nTagId = 0;
  }

  if( nTagId==-1 ){
    blob_appendf(&bSQL, " AND 0");
  }else if( nTagId!=0 ){
    blob_appendf(&bSQL, " AND (EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid))", nTagId);
  }

  if( zFilename ){
    blob_appendf(&bSQL,
      " AND (SELECT mlink.fnid FROM mlink WHERE event.objid=mlink.mid) IN (SELECT fnid FROM filename WHERE name=%Q %s)",
        zFilename, filename_collation()
    );
  }

  blob_append( &bSQL, " ORDER BY event.mtime DESC", -1 );

  zProjectName = db_get("project-name", 0);
  if( zProjectName==0 ){
    zFreeProjectName = zProjectName = mprintf("Fossil source repository for: %s",
      zBaseURL);
  }
  zProjectDescr = db_get("project-description", 0);
  if( zProjectDescr==0 ){
    zProjectDescr = zProjectName;
  }

  zPubDate = cgi_rfc822_datestamp(time(NULL));

  fossil_print("<?xml version=\"1.0\"?>");
  fossil_print("<rss xmlns:dc=\"http://purl.org/dc/elements/1.1/\" version=\"2.0\">");
  fossil_print("<channel>\n");
  fossil_print("<title>%h</title>\n", zProjectName);
  fossil_print("<link>%s</link>\n", zBaseURL);
  fossil_print("<description>%h</description>\n", zProjectDescr);
  fossil_print("<pubDate>%s</pubDate>\n", zPubDate);
  fossil_print("<generator>Fossil version %s %s</generator>\n",
               MANIFEST_VERSION, MANIFEST_DATE);
  free(zPubDate);
  db_prepare(&q, blob_str(&bSQL));
  blob_reset( &bSQL );
  while( db_step(&q)==SQLITE_ROW && nLine<nLimit ){
    const char *zId = db_column_text(&q, 1);
    const char *zCom = db_column_text(&q, 3);
    const char *zAuthor = db_column_text(&q, 4);
    char *zPrefix = "";
    char *zDate;
    int nChild = db_column_int(&q, 5);
    int nParent = db_column_int(&q, 6);
    time_t ts;

    ts = (time_t)((db_column_double(&q,2) - 2440587.5)*86400.0);
    zDate = cgi_rfc822_datestamp(ts);

    if( nParent>1 && nChild>1 ){
      zPrefix = "*MERGE/FORK* ";
    }else if( nParent>1 ){
      zPrefix = "*MERGE* ";
    }else if( nChild>1 ){
      zPrefix = "*FORK* ";
    }

    fossil_print("<item>");
    fossil_print("<title>%s%h</title>\n", zPrefix, zCom);
    fossil_print("<link>%s/info/%s</link>\n", zBaseURL, zId);
    fossil_print("<description>%s%h</description>\n", zPrefix, zCom);
    fossil_print("<pubDate>%s</pubDate>\n", zDate);
    fossil_print("<dc:creator>%h</dc:creator>\n", zAuthor);
    fossil_print("<guid>%s/info/%s</guid>\n", g.zBaseURL, zId);
    fossil_print("</item>\n");
    free(zDate);
    nLine++;
  }

  db_finalize(&q);
  fossil_print("</channel>\n");
  fossil_print("</rss>\n");

  if( zFreeProjectName != 0 ){
    free( zFreeProjectName );
  }
}
