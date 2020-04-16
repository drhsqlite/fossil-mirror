/*
** Copyright (c) 2020 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@sqlite.org
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code to implement for managing backlinks and
** the "backlink" table of the repository database.
**
** A backlink is a reference in Fossil-Wiki or Markdown to some other
** object in the repository.
*/
#include "config.h"
#include "backlink.h"
#include <assert.h>


/*
** Show a graph all wiki, tickets, and check-ins that refer to object zUuid.
**
** If zLabel is not NULL and the graph is not empty, then output zLabel as
** a prefix to the graph.
*/
void render_backlink_graph(const char *zUuid, const char *zLabel){
  Blob sql;
  Stmt q;
  char *zGlob;
  zGlob = mprintf("%.5s*", zUuid);
  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
     "DELETE FROM ok;"
     "INSERT OR IGNORE INTO ok"
     " SELECT srcid FROM backlink"
     "  WHERE target GLOB %Q"
     "    AND %Q GLOB (target || '*');",
     zGlob, zUuid
  );
  if( !db_exists("SELECT 1 FROM ok") ) return;
  if( zLabel ) cgi_printf("%s", zLabel);
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_www(), -1);
  blob_append_sql(&sql, " AND event.objid IN ok ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_sql_text(&sql));
  www_print_timeline(&q, TIMELINE_DISJOINT|TIMELINE_GRAPH|TIMELINE_NOSCROLL,
                     0, 0, 0, 0, 0, 0);
  db_finalize(&q);
}

/*
** WEBPAGE: test-backlink-timeline
**
** Show a timeline of all check-ins and other events that have entries
** in the backlink table.  This is used for testing the rendering
** of the "References" section of the /info page.
*/
void backlink_timeline_page(void){
  Blob sql;
  Stmt q;

  login_check_credentials();
  if( !g.perm.Read || !g.perm.RdTkt || !g.perm.RdWiki ){
    login_needed(g.anon.Read && g.anon.RdTkt && g.anon.RdWiki);
    return;
  }
  style_header("Backlink Timeline (Internal Testing Use)");
  db_multi_exec(
     "CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
     "DELETE FROM ok;"
     "INSERT OR IGNORE INTO ok"
     " SELECT blob.rid FROM backlink, blob"
     "  WHERE blob.uuid BETWEEN backlink.target AND (backlink.target||'x')"
  );
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_www(), -1);
  blob_append_sql(&sql, " AND event.objid IN ok ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_sql_text(&sql));
  www_print_timeline(&q, TIMELINE_DISJOINT|TIMELINE_GRAPH|TIMELINE_NOSCROLL,
                     0, 0, 0, 0, 0, 0);
  db_finalize(&q);
  style_footer();
}

/*
** WEBPAGE: test-backlinks
**
** Show a table of all backlinks.  Admin access only.
*/
void backlink_table_page(void){
  Stmt q;
  int n;
  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(g.anon.Admin);
    return;
  }
  style_header("Backlink Table (Internal Testing Use)");
  n = db_int(0, "SELECT count(*) FROM backlink");
  @ <p>%d(n) backlink table entries:</p>
  db_prepare(&q,
    "SELECT target, srctype, srcid, datetime(mtime) FROM backlink"
  );
  style_table_sorter();
  @ <table border="1" cellpadding="2" cellspacing="0" \
  @  class='sortable' data-column-types='ttt' data-init-sort='0'>
  @ <thead><tr><th> Source <th> Target <th> mtime </tr></thead>
  @ <tbody>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTarget = db_column_text(&q, 0);
    int srctype = db_column_int(&q, 1);
    int srcid = db_column_int(&q, 2);
    const char *zMtime = db_column_text(&q, 3);
    static const char *azSrcType[] = { "comment", "ticket", "wiki", "unknown" };
    @ <tr><td><a href="%R/info/%h(zTarget)">%h(zTarget)</a>
    switch( srctype ){
      case BKLNK_COMMENT: {
        @ <td><a href="%R/info?name=rid:%d(srcid)">comment-%d(srcid)</a>
        break;
      }
      case BKLNK_TICKET: {
        @ <td><a href="%R/info?name=rid:%d(srcid)">ticket-%d(srcid)</a>
        break;
      }
      case BKLNK_WIKI: {
        @ <td><a href="%R/info?name=rid:%d(srcid)">wiki-%d(srcid)</a>
        break;
      }
      default: {
        @ <td>unknown(%d(srctype)) - %d(srcid)
        break;
      }
    }
    @ <td>%h(zMtime)</tr>
  }
  @ </tbody>
  @ </table>
  db_finalize(&q);
  style_footer();
}



/*
** Structure used to pass down state information through the
** markup formatters into the BACKLINK generator.
*/
#if INTERFACE
struct Backlink {
  int srcid;             /* srcid for the source document */
  int srctype;           /* One of BKLNK_*.  0=comment 1=ticket 2=wiki */
  double mtime;          /* mtime field for new BACKLINK table entries */
};
#endif


/*
** zTarget is a hyperlink target in some markup format.  If this
** target is a self-reference to some other object in the repository,
** then create an appropriate backlink.
*/
void backlink_create(Backlink *p, const char *zTarget, int nTarget){
  char zLink[HNAME_MAX+4];
  if( zTarget==0 ) return;
  if( nTarget<4 ) return;
  if( nTarget>=10 && strncmp(zTarget,"/info/",6)==0 ){
    zTarget += 6;
    nTarget -= 6;
  }
  if( nTarget>HNAME_MAX ) return;
  if( !validate16(zTarget, nTarget) ) return;
  memcpy(zLink, zTarget, nTarget);
  zLink[nTarget] = 0;
  canonical16(zLink, nTarget);
  db_multi_exec(
    "REPLACE INTO backlink(target,srctype,srcid,mtime)"
    "VALUES(%Q,%d,%d,%.17g)", zLink, p->srctype, p->srcid, p->mtime
  );
}

/*
** This routine is called by the markdown formatter for each hyperlink.
** If the hyperlink is a backlink, add it to the BACKLINK table.
*/
static int backlink_md_link(
  Blob *ob,         /* Write output text here (not used in this case) */
  Blob *target,     /* The hyperlink target */
  Blob *title,      /* Hyperlink title */
  Blob *content,    /* Content of the link */
  void *opaque
){
  Backlink *p = (Backlink*)opaque;
  char *zTarget = blob_buffer(target);
  int nTarget = blob_size(target);

  backlink_create(p, zTarget, nTarget);
  return 1;    
}

/*
** Scan markdown text and add self-hyperlinks to the BACKLINK table.
*/
void markdown_extract_links(
  char *zInputText,
  Backlink *p
){
  struct mkd_renderer html_renderer = {
    0,  /* prolog */
    0,  /* epilog */
    0,  /* blockcode */
    0,  /* blockquote */
    0,  /* raw_block */
    0,  /* header */
    0,  /* hrule */
    0,  /* list */
    0,  /* list_item */
    0,  /* paragraph */
    0,  /* table */
    0,  /* table_cell */
    0,  /* table_row */
    0,  /* autolink */
    0,  /* code_span */
    0,  /* double-emphasis */
    0,  /* emphasis */
    0,  /* image */
    0,  /* line_break */
    backlink_md_link,  /* link */
    0,  /* raw_span */
    0,  /* triple_emphasis */
    0,  /* entity */
    0,  /* normal_text */
    "*_", /* emphasis characters */
    0   /* client data */
  };
  Blob out, in;
  html_renderer.opaque = (void*)p;
  blob_init(&out, 0, 0);
  blob_init(&in, zInputText, -1);
  markdown(&out, &in, &html_renderer);
  blob_reset(&out);
  blob_reset(&in);
}

/*
** Parse text looking for hyperlinks.  Insert references into the
** BACKLINK table.
*/
void backlink_extract(
  char *zSrc,            /* Input text from which links are extracted */
  const char *zMimetype, /* Mimetype of input.  NULL means fossil-wiki */
  int srcid,             /* srcid for the source document */
  int srctype,           /* One of BKLNK_*.  0=comment 1=ticket 2=wiki */
  double mtime,          /* mtime field for new BACKLINK table entries */
  int replaceFlag        /* True to overwrite prior BACKLINK entries */
){
  Backlink bklnk;
  if( replaceFlag ){
    db_multi_exec("DELETE FROM backlink WHERE srctype=%d AND srcid=%d",
                  srctype, srcid);
  }
  bklnk.srcid = srcid;
  assert( srctype>=BKLNK_COMMENT && srctype<=BKLNK_WIKI );
  bklnk.srctype = srctype;
  bklnk.mtime = mtime;
  if( zMimetype==0 || strstr(zMimetype,"wiki")!=0 ){
    wiki_extract_links(zSrc, &bklnk, srctype==BKLNK_COMMENT ? WIKI_INLINE : 0);
  }else if( strstr(zMimetype,"markdown")!=0 ){
    markdown_extract_links(zSrc, &bklnk);
  }
}

/*
** COMMAND: test-backlinks
**
** Usage: %fossil test-backlinks SRCID SRCTYPE ?OPTIONS? INPUT-FILE
**
** Read the content of INPUT-FILE and pass it into the backlink_extract()
** routine.  But instead of adding backlinks to the backlink table,
** just print them on stdout.  SRCID and SRCTYPE are integers.
**
** Options:
**    --mtime DATETIME        Use an alternative date/time.  Defaults to the
**                            current date/time.
**    --mimetype TYPE         Use an alternative mimetype.
*/
void test_backlinks_cmd(void){
  const char *zMTime = find_option("mtime",0,1);
  const char *zMimetype = find_option("mimetype",0,1);
  Blob in;
  int srcid;
  int srctype;
  double mtime;

  verify_all_options();
  if( g.argc!=5 ){
    usage("SRCTYPE SRCID INPUTFILE");
  }
  srctype = atoi(g.argv[2]);
  if( srctype<0 || srctype>2 ){
    fossil_fatal("SRCTYPE should be a integer 0, 1, or 2");
  }
  srcid = atoi(g.argv[3]);
  blob_read_from_file(&in, g.argv[4], ExtFILE);
  sqlite3_open(":memory:",&g.db);
  if( zMTime==0 ) zMTime = "now";
  mtime = db_double(1721059.5,"SELECT julianday(%Q)",zMTime);
  g.fSqlPrint = 1;
  sqlite3_create_function(g.db, "print", -1, SQLITE_UTF8, 0,db_sql_print,0,0);
  db_multi_exec(
    "CREATE TEMP TABLE backlink(target,srctype,srcid,mtime);\n"
    "CREATE TRIGGER backlink_insert BEFORE INSERT ON backlink BEGIN\n"
    "  SELECT print("
    " 'target='||quote(new.target)||"
    " ' srctype='||quote(new.srctype)||"
    " ' srcid='||quote(new.srcid)||"
    " ' mtime='||datetime(new.mtime));\n"
    "  SELECT raise(ignore);\n"
    "END;"
  );
  backlink_extract(blob_str(&in),zMimetype,srcid,srctype,mtime,0);
  blob_reset(&in);
}
