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

void forum_render_to_html(struct Blob*, const char*, const char*);
char *technote_render_to_html(struct Blob*, int);

/*
** Append text to pOut, escaping any CDATA terminators.
*/
static void rss_cdata_append(Blob *pOut, const char *zIn, int nIn){
  const char *zEnd;
  const char *zDelim;
  if( pOut==0 ) return;
  if( zIn==0 ) zIn = "";
  if( nIn<0 ) nIn = (int)strlen(zIn);
  zEnd = zIn + nIn;
  while( zIn<zEnd && (zDelim = strstr(zIn, "]]>") )!=0 ){
    if( zDelim>=zEnd ) break;
    blob_append(pOut, zIn, (int)(zDelim - zIn));
    blob_append_literal(pOut, "]]]]><![CDATA[>");
    zIn = zDelim + 3;
  }
  if( zIn<zEnd ){
    blob_append(pOut, zIn, (int)(zEnd - zIn));
  }
}

/*
** Return true if zIn looks like an absolute URL.
*/
static int rss_is_absolute_url(const char *zIn, int nIn){
  int i;
  if( zIn==0 || nIn<=0 ) return 0;
  if( nIn>=2 && zIn[0]=='/' && zIn[1]=='/' ) return 1;
  if( !((zIn[0]>='a' && zIn[0]<='z') || (zIn[0]>='A' && zIn[0]<='Z')) ){
    return 0;
  }
  for(i=1; i<nIn; i++){
    char c = zIn[i];
    if( (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9')
        || c=='+' || c=='-' || c=='.' ){
      continue;
    }
    return c==':' ? 1 : 0;
  }
  return 0;
}

/*
** Return the length of zBase without trailing slashes.
*/
static int rss_trim_base(const char *zBase){
  int n = zBase ? (int)strlen(zBase) : 0;
  while( n>0 && zBase[n-1]=='/' ) n--;
  return n;
}

/*
** Compute the repository top path from zBase and return it.
*/
static const char *rss_top_from_base(Blob *pTop, const char *zBase){
  const char *z = zBase;
  const char *zSlash = 0;
  int n;
  if( zBase==0 ) return "";
  if( strncmp(zBase, "http://", 7)==0 ){
    z = zBase + 7;
  }else if( strncmp(zBase, "https://", 8)==0 ){
    z = zBase + 8;
  }else{
    return "";
  }
  zSlash = strchr(z, '/');
  if( zSlash==0 ) return "";
  n = (int)strlen(zSlash);
  while( n>1 && zSlash[n-1]=='/' ) n--;
  blob_init(pTop, zSlash, n);
  return blob_str(pTop);
}

/*
** Append an absolute URL to pOut, using zBase/zTop as the base.
*/
static void rss_append_abs_url(
  Blob *pOut,
  const char *zBase,
  int nBase,
  const char *zTop,
  int nTop,
  const char *zRel,
  int nRel
){
  if( pOut==0 ) return;
  if( zRel==0 || nRel<=0 ) return;
  if( zBase==0 || zBase[0]==0 ){
    blob_append(pOut, zRel, nRel);
    return;
  }
  if( zRel[0]=='#' || rss_is_absolute_url(zRel, nRel) ){
    blob_append(pOut, zRel, nRel);
    return;
  }
  if( zRel[0]=='/' ){
    if( nTop>1 && strncmp(zRel, zTop, nTop)==0 ){
      blob_append(pOut, zBase, nBase);
      blob_append(pOut, zRel + nTop, nRel - nTop);
    }else{
      blob_append(pOut, zBase, nBase);
      blob_append(pOut, zRel, nRel);
    }
  }else{
    blob_append(pOut, zBase, nBase);
    blob_append_char(pOut, '/');
    blob_append(pOut, zRel, nRel);
  }
}

/*
** Convert relative href/src attributes in zIn to absolute URLs.
*/
static void rss_make_abs_links(
  Blob *pOut,
  const char *zBase,
  const char *zTop,
  const char *zIn,
  int nIn
){
  const char *z = zIn;
  const char *zEnd = zIn + nIn;
  const char *zLast = zIn;
  int nBase = rss_trim_base(zBase);
  int nTop = zTop ? (int)strlen(zTop) : 0;
  if( pOut==0 || zIn==0 ) return;
  while( z<zEnd ){
    int nAttr = 0;
    if( (z>zIn && !(z[-1]==' ' || (z[-1]>='\t' && z[-1]<='\r'))
         && z[-1]!='<') ){
      z++;
      continue;
    }
    if( zEnd - z >= 5
     && (z[0]=='h' || z[0]=='H')
     && (z[1]=='r' || z[1]=='R')
     && (z[2]=='e' || z[2]=='E')
     && (z[3]=='f' || z[3]=='F')
     && z[4]=='='
    ){
      nAttr = 5;
    }else if( zEnd - z >= 4
           && (z[0]=='s' || z[0]=='S')
           && (z[1]=='r' || z[1]=='R')
           && (z[2]=='c' || z[2]=='C')
           && z[3]=='='
    ){
      nAttr = 4;
    }
    if( nAttr==0 ){
      z++;
      continue;
    }
    {
      const char *zVal = z + nAttr;
      const char *zValEnd = zVal;
      char quote = 0;
      if( zVal>=zEnd ) break;
      if( *zVal=='"' || *zVal=='\'' ){
        quote = *zVal;
        zVal++;
      }
      zValEnd = zVal;
      while( zValEnd<zEnd ){
        if( quote ){
          if( *zValEnd==quote ) break;
        }else if( *zValEnd==' ' || (*zValEnd>='\t' && *zValEnd<='\r')
                || *zValEnd=='>' ){
          break;
        }
        zValEnd++;
      }
      blob_append(pOut, zLast, zVal - zLast);
      rss_append_abs_url(pOut, zBase, nBase, zTop, nTop,
                         zVal, (int)(zValEnd - zVal));
      zLast = zValEnd;
      z = zValEnd;
    }
  }
  if( zLast<zEnd ) blob_append(pOut, zLast, (int)(zEnd - zLast));
}

/*
** WEBPAGE: timeline.rss
** URL:  /timeline.rss?y=TYPE&n=LIMIT&tkt=HASH&tag=TAG&wiki=NAME&name=FILENAME
**
** Produce an RSS feed of the timeline.
**
** TYPE may be: all, ci (show check-ins only), t (show ticket changes only),
** w (show wiki only), e (show tech notes only), f (show forum posts only),
** g (show tag/branch changes only).
**
** LIMIT is the number of items to show.
**
** tkt=HASH filters for only those events for the specified ticket. tag=TAG
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
  Blob base = BLOB_INITIALIZER;
  Blob top = BLOB_INITIALIZER;
  const char *zType = PD("y","all"); /* Type of events.  All if NULL */
  const char *zTicketUuid = PD("tkt",NULL);
  const char *zTag = PD("tag",NULL);
  const char *zFilename = PD("name",NULL);
  const char *zWiki = PD("wiki",NULL);
  int nLimit = atoi(PD("n","20"));
  int nTagId;
  int bHasForum;
  const char zSQL1[] =
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   event.mtime,
    @   event.type,
    @   coalesce(ecomment,comment),
    @   coalesce(euser,user),
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid),
    @   (SELECT group_concat(substr(tagname,5), ', ') FROM tag, tagxref
    @     WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @       AND tagxref.rid=blob.rid AND tagxref.tagtype>0) AS tags
    @ FROM event, blob
    @ WHERE blob.rid=event.objid
  ;

  login_check_credentials();
  if( !g.perm.Read && !g.perm.RdTkt && !g.perm.RdWiki && !g.perm.RdForum ){
    return;
  }
  bHasForum = db_table_exists("repository","forumpost");

  blob_zero(&bSQL);
  blob_append_sql( &bSQL, "%s", zSQL1/*safe-for-%s*/ );
  if( bHasForum ){
    blob_append_sql(&bSQL,
      " AND (event.type!='f' OR event.objid IN ("
      "SELECT fpid FROM forumpost "
      "WHERE fpid NOT IN (SELECT fprev FROM forumpost WHERE fprev IS NOT NULL)"
      "))"
    );
  }else{
    blob_append_sql(&bSQL, " AND event.type!='f'");
  }

  if( zType[0]!='a' ){
    if( zType[0]=='c' && !g.perm.Read ) zType = "x";
    else if( (zType[0]=='w' || zType[0]=='e') && !g.perm.RdWiki ) zType = "x";
    else if( zType[0]=='t' && !g.perm.RdTkt ) zType = "x";
    else if( zType[0]=='f' && !g.perm.RdForum ) zType = "x";
    blob_append_sql(&bSQL, " AND event.type=%Q", zType);
  }else{
    blob_append_sql(&bSQL, " AND event.type in (");
    if( g.perm.Read ){
      blob_append_sql(&bSQL, "'ci',");
    }
    if( g.perm.RdTkt ){
      blob_append_sql(&bSQL, "'t',");
    }
    if( g.perm.RdWiki ){
      blob_append_sql(&bSQL, "'w','e',");
    }
    if( g.perm.RdForum ){
      blob_append_sql(&bSQL, "'f',");
    }
    blob_append_sql(&bSQL, "'x')");
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
    blob_append_sql(&bSQL, " AND 0");
  }else if( nTagId!=0 ){
    blob_append_sql(&bSQL, " AND (EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid))", nTagId);
  }

  if( zFilename ){
    blob_append_sql(&bSQL,
      " AND (SELECT mlink.fnid FROM mlink WHERE event.objid=mlink.mid) "
      " IN (SELECT fnid FROM filename WHERE name=%Q %s)",
        zFilename, filename_collation()
    );
  }

  blob_append_sql( &bSQL, " ORDER BY event.mtime DESC" );

  cgi_set_content_type("application/rss+xml");

  zProjectName = db_get("project-name", 0);
  if( zProjectName==0 ){
    zFreeProjectName = zProjectName =
      mprintf("Fossil source repository for: %s", g.zBaseURL);
  }
  zProjectDescr = db_get("project-description", 0);
  if( zProjectDescr==0 ){
    zProjectDescr = zProjectName;
  }

  zPubDate = cgi_rfc822_datestamp(time(NULL));
  blob_append(&base, g.zBaseURL, -1);
  rss_top_from_base(&top, g.zBaseURL);

  @ <?xml version="1.0"?>
  @ <rss xmlns:dc="http://purl.org/dc/elements/1.1/" \
  @  xmlns:content="http://purl.org/rss/1.0/modules/content/" \
  @  version="2.0">
  @   <channel>
  @     <title>%h(zProjectName)</title>
  @     <link>%s(g.zBaseURL)</link>
  @     <description>%h(zProjectDescr)</description>
  @     <pubDate>%s(zPubDate)</pubDate>
  @     <generator>Fossil version %s(MANIFEST_VERSION) %s(MANIFEST_DATE)</generator>
  free(zPubDate);
  db_prepare(&q, "%s", blob_sql_text(&bSQL));
  blob_reset( &bSQL );
  while( db_step(&q)==SQLITE_ROW && nLine<nLimit ){
    int rid = db_column_int(&q, 0);
    const char *zId = db_column_text(&q, 1);
    const char *zEType = db_column_text(&q, 3);
    const char *zCom = db_column_text(&q, 4);
    const char *zAuthor = db_column_text(&q, 5);
    char *zPrefix = "";
    char *zSuffix = 0;
    char *zDate;
    int nChild = db_column_int(&q, 6);
    int nParent = db_column_int(&q, 7);
    const char *zTagList = db_column_text(&q, 8);
    Manifest *pPost = 0;
    char *zTechnoteId = 0;
    Blob contentHtml = BLOB_INITIALIZER;
    int bForumContent = 0;
    time_t ts;

    if( zTagList && zTagList[0]==0 ) zTagList = 0;
    ts = (time_t)((db_column_double(&q,2) - 2440587.5)*86400.0);
    zDate = cgi_rfc822_datestamp(ts);

    if( zEType[0]=='c' ){
      if( nParent>1 && nChild>1 ){
        zPrefix = "*MERGE/FORK* ";
      }else if( nParent>1 ){
        zPrefix = "*MERGE* ";
      }else if( nChild>1 ){
        zPrefix = "*FORK* ";
      }
    }else if( zEType[0]=='w' ){
      switch(zCom ? zCom[0] : 0){
        case ':': zPrefix = "Edit wiki page: "; break;
        case '+': zPrefix = "Add wiki page: "; break;
        case '-': zPrefix = "Delete wiki page: "; break;
      }
      if(*zPrefix) ++zCom;
    }

    if( zTagList ){
      zSuffix = mprintf(" (tags: %s)", zTagList);
    }

    if( zEType[0]=='f' ){
      if( !g.perm.ModForum && content_is_private(rid) ){
        free(zDate);
        free(zSuffix);
        continue;
      }
      pPost = manifest_get(rid, CFTYPE_FORUM, 0);
      if( pPost ){
        forum_render_to_html(&contentHtml, pPost->zMimetype, pPost->zWiki);
        if( blob_size(&contentHtml)>0 ){
          Blob normalized = BLOB_INITIALIZER;
          rss_make_abs_links(&normalized, blob_str(&base),
                             blob_str(&top), blob_str(&contentHtml),
                             blob_size(&contentHtml));
          blob_reset(&contentHtml);
          blob_append(&contentHtml, blob_str(&normalized),
                      blob_size(&normalized));
          blob_reset(&normalized);
          bForumContent = 1;
        }
      }
    }else if( zEType[0]=='e' ){
      zTechnoteId = technote_render_to_html(&contentHtml, rid);
      if( blob_size(&contentHtml)>0 ){
        Blob normalized = BLOB_INITIALIZER;
        rss_make_abs_links(&normalized, blob_str(&base),
                           blob_str(&top), blob_str(&contentHtml),
                           blob_size(&contentHtml));
        blob_reset(&contentHtml);
        blob_append(&contentHtml, blob_str(&normalized),
                    blob_size(&normalized));
        blob_reset(&normalized);
        bForumContent = 1;
      }
    }
    @     <item>
    @       <title>%s(zPrefix)%h(zCom)%h(zSuffix)</title>
    if( zEType[0]=='e' && zTechnoteId!=0 ){
      @       <link>%s(g.zBaseURL)/technote/%s(zTechnoteId)</link>
    }else{
      @       <link>%s(g.zBaseURL)/info/%s(zId)</link>
    }
    @       <description>%s(zPrefix)%h(zCom)%h(zSuffix)</description>
    @       <pubDate>%s(zDate)</pubDate>
    @       <dc:creator>%h(zAuthor)</dc:creator>
    @       <guid>%s(g.zBaseURL)/info/%s(zId)</guid>
    if( bForumContent ){
      Blob cdata = BLOB_INITIALIZER;
      @       <dc:format>text/html</dc:format>
      @       <content:encoded><![CDATA[
      rss_cdata_append(&cdata, blob_str(&contentHtml),
                       blob_size(&contentHtml));
      cgi_append_content(blob_str(&cdata), blob_size(&cdata));
      blob_reset(&cdata);
      @ ]]></content:encoded>
    }
    @     </item>
    if( pPost ) manifest_destroy(pPost);
    free(zTechnoteId);
    blob_reset(&contentHtml);
    free(zDate);
    free(zSuffix);
    nLine++;
  }

  db_finalize(&q);
  blob_reset(&base);
  blob_reset(&top);
  @   </channel>
  @ </rss>

  if( zFreeProjectName != 0 ){
    free( zFreeProjectName );
  }
}

/*
** COMMAND: rss*
**
** Usage: %fossil rss ?OPTIONS?
**
** The CLI variant of the /timeline.rss page, this produces an RSS
** feed of the timeline to stdout.
**
** Options:
**   -type|y FLAG    May be: all (default), ci (show check-ins only),
**                   t (show tickets only), w (show wiki only),
**                   e (show tech notes only), f (show forum posts only)
**
**   -limit|n LIMIT  The maximum number of items to show
**
**   -tkt HASH       Filter for only those events for the specified ticket
**
**   -tag TAG        Filter for a tag
**
**   -wiki NAME      Filter on a specific wiki page
**
** Only one of -tkt, -tag, or -wiki may be used.
**
**   -name FILENAME  Filter for a specific file. This may be combined
**                   with one of the other filters (useful for looking
**                   at a specific branch).
**
**   -url STRING     Set the RSS feed's root URL to the given string.
**                   The default is "URL-PLACEHOLDER" (without quotes).
*/
void cmd_timeline_rss(void){
  Stmt q;
  int nLine=0;
  char *zPubDate, *zProjectName, *zProjectDescr, *zFreeProjectName=0;
  Blob bSQL;
  Blob base = BLOB_INITIALIZER;
  Blob top = BLOB_INITIALIZER;
  const char *zType = find_option("type","y",1); /* Type of events;All if NULL*/
  const char *zTicketUuid = find_option("tkt",NULL,1);
  const char *zTag = find_option("tag",NULL,1);
  const char *zFilename = find_option("name",NULL,1);
  const char *zWiki = find_option("wiki",NULL,1);
  const char *zLimit = find_option("limit", "n",1);
  const char *zBaseURL = find_option("url", NULL, 1);
  int nLimit = atoi( (zLimit && *zLimit) ? zLimit : "20" );
  int nTagId;
  int bHasForum;
  const char zSQL1[] =
    @ SELECT
    @   blob.rid,
    @   uuid,
    @   event.mtime,
    @   event.type,
    @   coalesce(ecomment,comment),
    @   coalesce(euser,user),
    @   (SELECT count(*) FROM plink WHERE pid=blob.rid AND isprim),
    @   (SELECT count(*) FROM plink WHERE cid=blob.rid),
    @   (SELECT group_concat(substr(tagname,5), ', ') FROM tag, tagxref
    @     WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid
    @       AND tagxref.rid=blob.rid AND tagxref.tagtype>0) AS tags
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

  /* We should be done with options.. */
    verify_all_options();

  blob_zero(&bSQL);
  blob_append( &bSQL, zSQL1, -1 );
  bHasForum = db_table_exists("repository","forumpost");
  if( bHasForum ){
    blob_append_sql(&bSQL,
      " AND (event.type!='f' OR event.objid IN ("
      "SELECT fpid FROM forumpost "
      "WHERE fpid NOT IN (SELECT fprev FROM forumpost WHERE fprev IS NOT NULL)"
      "))"
    );
  }else{
    blob_append_sql(&bSQL, " AND event.type!='f'");
  }

  if( zType[0]!='a' ){
    blob_append_sql(&bSQL, " AND event.type=%Q", zType);
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
    blob_append_sql(&bSQL, " AND 0");
  }else if( nTagId!=0 ){
    blob_append_sql(&bSQL, " AND (EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid))", nTagId);
  }

  if( zFilename ){
    blob_append_sql(&bSQL,
      " AND (SELECT mlink.fnid FROM mlink WHERE event.objid=mlink.mid) "
      " IN (SELECT fnid FROM filename WHERE name=%Q %s)",
        zFilename, filename_collation()
    );
  }

  blob_append( &bSQL, " ORDER BY event.mtime DESC", -1 );

  zProjectName = db_get("project-name", 0);
  if( zProjectName==0 ){
    zFreeProjectName = zProjectName =
      mprintf("Fossil source repository for: %s", zBaseURL);
  }
  zProjectDescr = db_get("project-description", 0);
  if( zProjectDescr==0 ){
    zProjectDescr = zProjectName;
  }

  zPubDate = cgi_rfc822_datestamp(time(NULL));
  blob_append(&base, zBaseURL, -1);
  rss_top_from_base(&top, zBaseURL);

  fossil_print("<?xml version=\"1.0\"?>");
  fossil_print("<rss xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
               "  xmlns:content=\"http://purl.org/rss/1.0/modules/content/\" "
               "  version=\"2.0\">\n");
  fossil_print("<channel>\n");
  fossil_print("<title>%h</title>\n", zProjectName);
  fossil_print("<link>%s</link>\n", zBaseURL);
  fossil_print("<description>%h</description>\n", zProjectDescr);
  fossil_print("<pubDate>%s</pubDate>\n", zPubDate);
  fossil_print("<generator>Fossil version %s %s</generator>\n",
               MANIFEST_VERSION, MANIFEST_DATE);
  free(zPubDate);
  db_prepare(&q, "%s", blob_sql_text(&bSQL));
  blob_reset( &bSQL );
  while( db_step(&q)==SQLITE_ROW && nLine<nLimit ){
    int rid = db_column_int(&q, 0);
    const char *zId = db_column_text(&q, 1);
    const char *zEType = db_column_text(&q, 3);
    const char *zCom = db_column_text(&q, 4);
    const char *zAuthor = db_column_text(&q, 5);
    char *zPrefix = "";
    char *zSuffix = 0;
    char *zDate;
    int nChild = db_column_int(&q, 6);
    int nParent = db_column_int(&q, 7);
    const char *zTagList = db_column_text(&q, 8);
    Manifest *pPost = 0;
    char *zTechnoteId = 0;
    Blob contentHtml = BLOB_INITIALIZER;
    int bForumContent = 0;
    time_t ts;

    if( zTagList && zTagList[0]==0 ) zTagList = 0;
    ts = (time_t)((db_column_double(&q,2) - 2440587.5)*86400.0);
    zDate = cgi_rfc822_datestamp(ts);

    if( zEType[0]=='c' ){
      if( nParent>1 && nChild>1 ){
        zPrefix = "*MERGE/FORK* ";
      }else if( nParent>1 ){
        zPrefix = "*MERGE* ";
      }else if( nChild>1 ){
        zPrefix = "*FORK* ";
      }
    }else if( zEType[0]=='w' ){
      switch(zCom ? zCom[0] : 0){
        case ':': zPrefix = "Edit wiki page: "; break;
        case '+': zPrefix = "Add wiki page: "; break;
        case '-': zPrefix = "Delete wiki page: "; break;
      }
      if(*zPrefix) ++zCom;
    }

    if( zTagList ){
      zSuffix = mprintf(" (tags: %s)", zTagList);
    }

    if( zEType[0]=='f' ){
      pPost = manifest_get(rid, CFTYPE_FORUM, 0);
      if( pPost ){
        forum_render_to_html(&contentHtml, pPost->zMimetype, pPost->zWiki);
        if( blob_size(&contentHtml)>0 ){
          Blob normalized = BLOB_INITIALIZER;
          rss_make_abs_links(&normalized, blob_str(&base),
                             blob_str(&top), blob_str(&contentHtml),
                             blob_size(&contentHtml));
          blob_reset(&contentHtml);
          blob_append(&contentHtml, blob_str(&normalized),
                      blob_size(&normalized));
          blob_reset(&normalized);
          bForumContent = 1;
        }
      }
    }else if( zEType[0]=='e' ){
      zTechnoteId = technote_render_to_html(&contentHtml, rid);
      if( blob_size(&contentHtml)>0 ){
        Blob normalized = BLOB_INITIALIZER;
        rss_make_abs_links(&normalized, blob_str(&base),
                           blob_str(&top), blob_str(&contentHtml),
                           blob_size(&contentHtml));
        blob_reset(&contentHtml);
        blob_append(&contentHtml, blob_str(&normalized),
                    blob_size(&normalized));
        blob_reset(&normalized);
        bForumContent = 1;
      }
    }
    fossil_print("<item>");
    fossil_print("<title>%s%h%h</title>\n", zPrefix, zCom, zSuffix);
    if( zEType[0]=='e' && zTechnoteId!=0 ){
      fossil_print("<link>%s/technote/%s</link>\n", zBaseURL, zTechnoteId);
    }else{
      fossil_print("<link>%s/info/%s</link>\n", zBaseURL, zId);
    }
    fossil_print("<description>%s%h%h</description>\n", zPrefix, zCom, zSuffix);
    fossil_print("<pubDate>%s</pubDate>\n", zDate);
    fossil_print("<dc:creator>%h</dc:creator>\n", zAuthor);
    fossil_print("<guid>%s/info/%s</guid>\n", g.zBaseURL, zId);
    if( bForumContent ){
      Blob cdata = BLOB_INITIALIZER;
      fossil_print("<dc:format>text/html</dc:format>\n");
      fossil_print("<content:encoded><![CDATA[");
      rss_cdata_append(&cdata, blob_str(&contentHtml),
                       blob_size(&contentHtml));
      fossil_print("%s", blob_str(&cdata));
      blob_reset(&cdata);
      fossil_print("]]></content:encoded>\n");
    }
    fossil_print("</item>\n");
    if( pPost ) manifest_destroy(pPost);
    free(zTechnoteId);
    blob_reset(&contentHtml);
    free(zDate);
    free(zSuffix);
    nLine++;
  }

  db_finalize(&q);
  blob_reset(&base);
  blob_reset(&top);
  fossil_print("</channel>\n");
  fossil_print("</rss>\n");

  if( zFreeProjectName != 0 ){
    free( zFreeProjectName );
  }
}
