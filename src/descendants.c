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
** This file contains code used to find descendants of a version
** or leaves of a version tree.
*/
#include "config.h"
#include "descendants.h"
#include <assert.h>


/*
** Create a temporary table named "leaves" if it does not
** already exist.  Load this table with the RID of all
** check-ins that are leaves which are descended from
** check-in iBase.
**
** A "leaf" is a check-in that has no children in the same branch.
** There is a separate permanent table LEAF that contains all leaves
** in the tree.  This routine is used to compute a subset of that
** table consisting of leaves that are descended from a single check-in.
**
** The closeMode flag determines behavior associated with the "closed"
** tag:
**
**    closeMode==0       Show all leaves regardless of the "closed" tag.
**
**    closeMode==1       Show only leaves without the "closed" tag.
**
**    closeMode==2       Show only leaves with the "closed" tag.
**
** The default behavior is to ignore closed leaves (closeMode==0).  To
** Show all leaves, use closeMode==1.  To show only closed leaves, use
** closeMode==2.
*/
void compute_leaves(int iBase, int closeMode){

  /* Create the LEAVES table if it does not already exist.  Make sure
  ** it is empty.
  */
  db_multi_exec(
    "CREATE TEMP TABLE IF NOT EXISTS leaves("
    "  rid INTEGER PRIMARY KEY"
    ");"
    "DELETE FROM leaves;"
  );

  if( iBase>0 ){
    Bag seen;     /* Descendants seen */
    Bag pending;  /* Unpropagated descendants */
    Stmt q1;      /* Query to find children of a check-in */
    Stmt isBr;    /* Query to check to see if a check-in starts a new branch */
    Stmt ins;     /* INSERT statement for a new record */

    /* Initialize the bags. */
    bag_init(&seen);
    bag_init(&pending);
    bag_insert(&pending, iBase);

    /* This query returns all non-branch-merge children of check-in :rid.
    **
    ** If a child is a merge of a fork within the same branch, it is
    ** returned.  Only merge children in different branches are excluded.
    */
    db_prepare(&q1,
      "SELECT cid FROM plink"
      " WHERE pid=:rid"
      "   AND (isprim"
      "        OR coalesce((SELECT value FROM tagxref"
                        "   WHERE tagid=%d AND rid=plink.pid), 'trunk')"
                 "=coalesce((SELECT value FROM tagxref"
                        "   WHERE tagid=%d AND rid=plink.cid), 'trunk'))",
      TAG_BRANCH, TAG_BRANCH
    );

    /* This query returns a single row if check-in :rid is the first
    ** check-in of a new branch.
    */
    db_prepare(&isBr,
       "SELECT 1 FROM tagxref"
       " WHERE rid=:rid AND tagid=%d AND tagtype=2"
       "   AND srcid>0",
       TAG_BRANCH
    );

    /* This statement inserts check-in :rid into the LEAVES table.
    */
    db_prepare(&ins, "INSERT OR IGNORE INTO leaves VALUES(:rid)");

    while( bag_count(&pending) ){
      int rid = bag_first(&pending);
      int cnt = 0;
      bag_remove(&pending, rid);
      db_bind_int(&q1, ":rid", rid);
      while( db_step(&q1)==SQLITE_ROW ){
        int cid = db_column_int(&q1, 0);
        if( bag_insert(&seen, cid) ){
          bag_insert(&pending, cid);
        }
        db_bind_int(&isBr, ":rid", cid);
        if( db_step(&isBr)==SQLITE_DONE ){
          cnt++;
        }
        db_reset(&isBr);
      }
      db_reset(&q1);
      if( cnt==0 && !is_a_leaf(rid) ){
        cnt++;
      }
      if( cnt==0 ){
        db_bind_int(&ins, ":rid", rid);
        db_step(&ins);
        db_reset(&ins);
      }
    }
    db_finalize(&ins);
    db_finalize(&isBr);
    db_finalize(&q1);
    bag_clear(&pending);
    bag_clear(&seen);
  }else{
    db_multi_exec(
      "INSERT INTO leaves"
      "  SELECT leaf.rid FROM leaf"
    );
  }
  if( closeMode==1 ){
    db_multi_exec(
      "DELETE FROM leaves WHERE rid IN"
      "  (SELECT leaves.rid FROM leaves, tagxref"
      "    WHERE tagxref.rid=leaves.rid "
      "      AND tagxref.tagid=%d"
      "      AND tagxref.tagtype>0)",
      TAG_CLOSED
    );
  }else if( closeMode==2 ){
    db_multi_exec(
      "DELETE FROM leaves WHERE rid NOT IN"
      "  (SELECT leaves.rid FROM leaves, tagxref"
      "    WHERE tagxref.rid=leaves.rid "
      "      AND tagxref.tagid=%d"
      "      AND tagxref.tagtype>0)",
      TAG_CLOSED
    );
  }
}

/*
** If RID refers to a check-in, return the mtime of that check-in - the
** julian day number of when the check-in occurred.
*/
double mtime_of_rid(int rid, double mtime){
  static Stmt q;
  db_static_prepare(&q,"SELECT mtime FROM event WHERE objid=:rid");
  db_bind_int(&q, ":rid", rid);
  if( db_step(&q)==SQLITE_ROW ){
    mtime = db_column_double(&q,0);
  }
  db_reset(&q);
  return mtime;
}



/*
** Load the record ID rid and up to |N|-1 closest ancestors into
** the "ok" table.  If N is zero, no limit.  If ridBackTo is not zero
** then stop the search upon reaching the ancestor with rid==ridBackTo.
*/
void compute_ancestors(int rid, int N, int directOnly, int ridBackTo){
  if( !N ){
     N = -1;
  }else if( N<0 ){
     N = -N;
  }
  if( directOnly ){
    /* Direct mode means to show primary parents only */
    db_multi_exec(
      "WITH RECURSIVE "
      "  ancestor(rid, mtime) AS ("
      "    SELECT %d, mtime FROM event WHERE objid=%d "
      "    UNION "
      "    SELECT plink.pid, event.mtime"
      "      FROM ancestor, plink, event"
      "     WHERE plink.cid=ancestor.rid"
      "       AND event.objid=plink.pid"
      "       AND plink.isPrim"
      "     ORDER BY mtime DESC LIMIT %d"
      "  )"
      "INSERT INTO ok"
      "  SELECT rid FROM ancestor;",
      rid, rid, N
    );
  }else{
    /* If not in directMode, also include merge parents, including
    ** cherrypick merges.  Except, terminate searches at the cherrypick
    ** merge parent itself.  In other words, include:
    **    (1)  Primary parents
    **    (2)  Merge parents
    **    (3)  Cherrypick merge parents.
    **    (4)  All ancestores of 1 and 2 but not of 3.
    */
    double rLimitMtime = 0.0;
    if( ridBackTo ){
      rLimitMtime = mtime_of_rid(ridBackTo, 0.0);
    }
    db_multi_exec(
      "WITH RECURSIVE\n"
      "  parent(pid,cid,isCP) AS (\n"
      "    SELECT plink.pid, plink.cid, 0 AS xisCP FROM plink\n"
      "    UNION ALL\n"
      "    SELECT parentid, childid, 1 FROM cherrypick WHERE NOT isExclude\n"
      "  ),\n"
      "  ancestor(rid, mtime, isCP) AS (\n"
      "    SELECT %d, mtime, 0 FROM event WHERE objid=%d\n"
      "    UNION\n"
      "    SELECT parent.pid, event.mtime, parent.isCP\n"
      "      FROM ancestor, parent, event\n"
      "     WHERE parent.cid=ancestor.rid\n"
      "       AND event.objid=parent.pid\n"
      "       AND NOT ancestor.isCP\n"
      "       AND (event.mtime>=%.17g OR parent.pid=%d)\n"
      "     ORDER BY mtime DESC LIMIT %d\n"
      "  )\n"
      "INSERT OR IGNORE INTO ok SELECT rid FROM ancestor;",
      rid, rid, rLimitMtime, ridBackTo, N
    );
    if( ridBackTo && db_changes()>1 ){
      db_multi_exec("INSERT OR IGNORE INTO ok VALUES(%d)", ridBackTo);
    }
  }
}

/*
** Compute the youngest ancestor of record ID rid that is a member of
** branch zBranch.
*/
int compute_youngest_ancestor_in_branch(int rid, const char *zBranch){
  return db_int(0,
    "WITH RECURSIVE "
    "  ancestor(rid, mtime) AS ("
    "    SELECT %d, mtime FROM event WHERE objid=%d "
    "    UNION "
    "    SELECT plink.pid, event.mtime"
    "      FROM ancestor, plink, event"
    "     WHERE plink.cid=ancestor.rid"
    "       AND event.objid=plink.pid"
    "     ORDER BY mtime DESC"
    "  )"
    "  SELECT ancestor.rid FROM ancestor"
    "   WHERE EXISTS(SELECT 1 FROM tagxref"
                    " WHERE tagid=%d AND tagxref.rid=ancestor.rid"
                    "   AND value=%Q AND tagtype>0)"
    "  ORDER BY mtime DESC"
    "  LIMIT 1",
    rid, rid, TAG_BRANCH, zBranch
  );
}

/*
** Compute all direct ancestors (merge ancestors do not count)
** for the check-in rid and put them in a table named "ancestor".
** Label each generation with consecutive integers going backwards
** in time such that rid has the smallest generation number and the oldest
** direct ancestor as the largest generation number.
*/
void compute_direct_ancestors(int rid){
  db_multi_exec(
    "CREATE TEMP TABLE IF NOT EXISTS ancestor(rid INTEGER UNIQUE NOT NULL,"
                                            " generation INTEGER PRIMARY KEY);"
    "DELETE FROM ancestor;"
    "WITH RECURSIVE g(x,i) AS ("
    "  VALUES(%d,1)"
    "  UNION ALL"
    "  SELECT plink.pid, g.i+1 FROM plink, g"
    "   WHERE plink.cid=g.x AND plink.isprim)"
    "INSERT INTO ancestor(rid,generation) SELECT x,i FROM g;",
    rid
  );
}

/*
** Compute the "mtime" of the file given whose blob.rid is "fid" that
** is part of check-in "vid".  The mtime will be the mtime on vid or
** some ancestor of vid where fid first appears.
*/
int mtime_of_manifest_file(
  int vid,       /* The check-in that contains fid */
  int fid,       /* The id of the file whose check-in time is sought */
  i64 *pMTime    /* Write result here */
){
  static int prevVid = -1;
  static Stmt q;

  if( prevVid!=vid ){
    prevVid = vid;
    db_multi_exec("CREATE TEMP TABLE IF NOT EXISTS ok(rid INTEGER PRIMARY KEY);"
                  "DELETE FROM ok;");
    compute_ancestors(vid, 100000000, 1, 0);
  }
  db_static_prepare(&q,
    "SELECT (max(event.mtime)-2440587.5)*86400 FROM mlink, event"
    " WHERE mlink.mid=event.objid"
    "   AND +mlink.mid IN ok"
    "   AND mlink.fid=:fid");
  db_bind_int(&q, ":fid", fid);
  if( db_step(&q)!=SQLITE_ROW ){
    db_reset(&q);
    return 1;
  }
  *pMTime = db_column_int64(&q, 0);
  db_reset(&q);
  return 0;
}

/*
** Load the record ID rid and up to |N|-1 closest descendants into
** the "ok" table.  If N is zero, no limit.
*/
void compute_descendants(int rid, int N){
  if( !N ){
     N = -1;
  }else if( N<0 ){
     N = -N;
  }
  db_multi_exec(
    "WITH RECURSIVE\n"
    "  dx(rid,mtime) AS (\n"
    "     SELECT %d, 0\n"
    "     UNION\n"
    "     SELECT plink.cid, plink.mtime FROM dx, plink\n"
    "      WHERE plink.pid=dx.rid\n"
    "      ORDER BY 2\n"
    "  )\n"
    "INSERT OR IGNORE INTO ok SELECT rid FROM dx LIMIT %d",
    rid, N
  );
}

/*
** COMMAND: descendants*
**
** Usage: %fossil descendants ?CHECKIN? ?OPTIONS?
**
** Find all leaf descendants of the check-in specified or if the argument
** is omitted, of the check-in currently checked out.
**
** Options:
**    -R|--repository REPO       Extract info from repository REPO
**    -W|--width N               Width of lines (default is to auto-detect).
**                               Must be greater than 20 or else 0 for no
**                               limit, resulting in a one line per entry.
**
** See also: [[finfo]], [[info]], [[leaves]]
*/
void descendants_cmd(void){
  Stmt q;
  int base, width;
  const char *zWidth;

  db_find_and_open_repository(0,0);
  zWidth = find_option("width","W",1);
  if( zWidth ){
    width = atoi(zWidth);
    if( (width!=0) && (width<=20) ){
      fossil_fatal("-W|--width value must be >20 or 0");
    }
  }else{
    width = -1;
  }

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc==2 ){
    base = db_lget_int("checkout", 0);
  }else{
    base = name_to_typed_rid(g.argv[2], "ci");
  }
  if( base==0 ) return;
  compute_leaves(base, 0);
  db_prepare(&q,
    "%s"
    "   AND event.objid IN (SELECT rid FROM leaves)"
    " ORDER BY event.mtime DESC",
    timeline_query_for_tty()
  );
  print_timeline(&q, 0, width, 0, 0);
  db_finalize(&q);
}

/*
** COMMAND: leaves*
**
** Usage: %fossil leaves ?OPTIONS?
**
** Find leaves of all branches.  By default show only open leaves.
** The -a|--all flag causes all leaves (closed and open) to be shown.
** The -c|--closed flag shows only closed leaves.
**
** The --recompute flag causes the content of the "leaf" table in the
** repository database to be recomputed.
**
** Options:
**   -a|--all         Show ALL leaves
**   --bybranch       Order output by branch name
**   -c|--closed      Show only closed leaves
**   -m|--multiple    Show only cases with multiple leaves on a single branch
**   --recompute      Recompute the "leaf" table in the repository DB
**   -W|--width N     Width of lines (default is to auto-detect). Must be
**                    more than 39 or else 0 no limit, resulting in a single
**                    line per entry.
**
** See also: [[descendants]], [[finfo]], [[info]], [[branch]]
*/
void leaves_cmd(void){
  Stmt q;
  Blob sql;
  int showAll = find_option("all", "a", 0)!=0;
  int showClosed = find_option("closed", "c", 0)!=0;
  int recomputeFlag = find_option("recompute",0,0)!=0;
  int byBranch = find_option("bybranch",0,0)!=0;
  int multipleFlag = find_option("multiple","m",0)!=0;
  const char *zWidth = find_option("width","W",1);
  char *zLastBr = 0;
  int n, width;
  char zLineNo[10];
  char * const zMainBranch = db_get("main-branch","trunk");

  if( multipleFlag ) byBranch = 1;
  if( zWidth ){
    width = atoi(zWidth);
    if( (width!=0) && (width<=39) ){
      fossil_fatal("-W|--width value must be >39 or 0");
    }
  }else{
    width = -1;
  }
  db_find_and_open_repository(0,0);

  /* We should be done with options.. */
  verify_all_options();

  if( recomputeFlag ) leaf_rebuild();
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_tty(), -1);
  if( !multipleFlag ){
    /* The usual case - show all leaves */
    blob_append_sql(&sql, " AND blob.rid IN leaf");
  }else{
    /* Show only leaves where two are more occur in the same branch */
    db_multi_exec(
      "CREATE TEMP TABLE openLeaf(rid INTEGER PRIMARY KEY);"
      "INSERT INTO openLeaf(rid)"
      "  SELECT rid FROM leaf"
      "   WHERE NOT EXISTS("
      "     SELECT 1 FROM tagxref"
      "      WHERE tagid=%d AND tagtype>0 AND rid=leaf.rid);",
      TAG_CLOSED
    );
    db_multi_exec(
      "CREATE TEMP TABLE ambiguousBranch(brname TEXT);"
      "INSERT INTO ambiguousBranch(brname)"
      " SELECT (SELECT value FROM tagxref WHERE tagid=%d AND rid=openLeaf.rid)"
      "   FROM openLeaf"
      "  GROUP BY 1 HAVING count(*)>1;",
      TAG_BRANCH
    );
    db_multi_exec(
      "CREATE TEMP TABLE ambiguousLeaf(rid INTEGER PRIMARY KEY);\n"
      "INSERT INTO ambiguousLeaf(rid)\n"
      "  SELECT rid FROM openLeaf\n"
      "   WHERE (SELECT value FROM tagxref WHERE tagid=%d AND rid=openLeaf.rid)"
      "         IN (SELECT brname FROM ambiguousBranch);",
      TAG_BRANCH
    );
    blob_append_sql(&sql, " AND blob.rid IN ambiguousLeaf");
  }
  if( showClosed ){
    blob_append_sql(&sql," AND %z", leaf_is_closed_sql("blob.rid"));
  }else if( !showAll ){
    blob_append_sql(&sql," AND NOT %z", leaf_is_closed_sql("blob.rid"));
  }
  if( byBranch ){
    db_prepare(&q, "%s ORDER BY nullif(branch,'trunk') COLLATE nocase,"
                   " event.mtime DESC",
                   blob_sql_text(&sql));
  }else{
    db_prepare(&q, "%s ORDER BY event.mtime DESC", blob_sql_text(&sql));
  }
  blob_reset(&sql);
  n = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zId = db_column_text(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    const char *zCom = db_column_text(&q, 3);
    const char *zBr = db_column_text(&q, 7);
    char *z = 0;
    char * zBranchPoint = 0;

    if( byBranch && fossil_strcmp(zBr, zLastBr)!=0 ){
      fossil_print("*** %s ***\n", zBr);
      fossil_free(zLastBr);
      zLastBr = fossil_strdup(zBr);
      if( multipleFlag ) n = 0;
    }
    n++;
    sqlite3_snprintf(sizeof(zLineNo), zLineNo, "(%d)", n);
    fossil_print("%6s ", zLineNo);
    if(0!=fossil_strcmp(zBr,zMainBranch)){
      int ridOfRoot;
      z = mprintf("root:%s", zId);
      ridOfRoot = symbolic_name_to_rid(z, "ci");
      if(ridOfRoot>0){
        zBranchPoint = mprintf(" (branched from: [%.*z])", hash_digits(0),
                               rid_to_uuid(ridOfRoot));
      }
      fossil_free(z);
    }
    z = mprintf("%s [%S] %s%s", zDate, zId, zCom,
                zBranchPoint ? zBranchPoint : "");
    comment_print(z, zCom, 7, width, get_comment_format());
    fossil_free(z);
    fossil_free(zBranchPoint);
  }
  fossil_free(zMainBranch);
  fossil_free(zLastBr);
  db_finalize(&q);
}

/*
** WEBPAGE: leaves
**
** Show leaf check-ins in a timeline.  By default only open leaves
** are listed.
**
** A "leaf" is a check-in with no children in the same branch.  A
** "closed leaf" is a leaf that has a "closed" tag.  An "open leaf"
** is a leaf without a "closed" tag.
**
** Query parameters:
**
**     all           Show all leaves
**     closed        Show only closed leaves
**     ng            No graph
**     nohidden      Hide check-ins with "hidden" tag
**     onlyhidden    Show only check-ins with "hidden" tag
**     brbg          Background color by branch name
**     ubg           Background color by user name
*/
void leaves_page(void){
  Blob sql;
  Stmt q;
  int showAll = P("all")!=0;
  int showClosed = P("closed")!=0;
  int fNg = PB("ng")!=0;           /* Flag for the "ng" query parameter */
  int fNoHidden = PB("nohidden")!=0;      /* "nohidden" query parameter */
  int fOnlyHidden = PB("onlyhidden")!=0;  /* "onlyhidden" query parameter */
  int fBrBg = PB("brbg")!=0;       /* Flag for the "brbg" query parameter */
  int fUBg = PB("ubg")!=0;         /* Flag for the "ubg" query parameter */
  HQuery url;                      /* URL to /leaves plus query parameters */
  int tmFlags;                     /* Timeline display flags */

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  url_initialize(&url, "leaves");
  if( fNg ) url_add_parameter(&url, "ng", "");
  if( fNoHidden ) url_add_parameter(&url, "nohidden", "");
  if( fOnlyHidden ) url_add_parameter(&url, "onlyhidden", "");
  if( fBrBg ) url_add_parameter(&url, "brbg", "");
  if( fUBg ) url_add_parameter(&url, "ubg", "");
  if( !showAll ){
    style_submenu_element("All", "%s", url_render(&url, "all", "", 0, 0));
  }
  if( !showClosed ){
    style_submenu_element("Closed", "%s", url_render(&url, "closed", "", 0, 0));
  }
  if( showClosed || showAll ){
    style_submenu_element("Open", "%s", url_render(&url, 0, 0, 0, 0));
  }
  url_reset(&url);
  cgi_check_for_malice();
  style_set_current_feature("leaves");
  style_header("Leaves");
  login_anonymous_available();
  timeline_ss_submenu();
#if 0
  style_sidebox_begin("Nomenclature:", "33%");
  @ <ol>
  @ <li> A <div class="sideboxDescribed">leaf</div>
  @ is a check-in with no descendants in the same branch.</li>
  @ <li> An <div class="sideboxDescribed">open leaf</div>
  @ is a leaf that does not have a "closed" tag
  @ and is thus assumed to still be in use.</li>
  @ <li> A <div class="sideboxDescribed">closed leaf</div>
  @ has a "closed" tag and is thus assumed to
  @ be historical and no longer in active use.</li>
  @ </ol>
  style_sidebox_end();
#endif

  if( showAll ){
    @ <h1>All leaves, both open and closed:</h1>
  }else if( showClosed ){
    @ <h1>Closed leaves:</h1>
  }else{
    @ <h1>Open leaves:</h1>
  }
  blob_zero(&sql);
  blob_append(&sql, timeline_query_for_www(), -1);
  blob_append_sql(&sql, " AND blob.rid IN leaf");
  if( showClosed ){
    blob_append_sql(&sql," AND %z", leaf_is_closed_sql("blob.rid"));
  }else if( !showAll ){
    blob_append_sql(&sql," AND NOT %z", leaf_is_closed_sql("blob.rid"));
  }
  if( fNoHidden || fOnlyHidden ){
    const char* zUnaryOp = fNoHidden ? "NOT" : "";
    blob_append_sql(&sql,
      " AND %s EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)\n",
      zUnaryOp/*safe-for-%s*/, TAG_HIDDEN);
  }
  db_prepare(&q, "%s ORDER BY event.mtime DESC", blob_sql_text(&sql));
  blob_reset(&sql);
  /* Always specify TIMELINE_DISJOINT, or graph_finish() may fail because of too
  ** many descenders to (off-screen) parents. */
  tmFlags = TIMELINE_LEAFONLY | TIMELINE_DISJOINT | TIMELINE_NOSCROLL;
  if( fNg==0 ) tmFlags |= TIMELINE_GRAPH;
  if( fBrBg ) tmFlags |= TIMELINE_BRCOLOR;
  if( fUBg ) tmFlags |= TIMELINE_UCOLOR;
  www_print_timeline(&q, tmFlags, 0, 0, 0, 0, 0, 0);
  db_finalize(&q);
  @ <br>
  style_finish_page();
}

#if INTERFACE
/* Flag parameters to compute_uses_file() */
#define USESFILE_DELETE   0x01  /* Include the check-ins where file deleted */

#endif

/*
** Append a new VALUES term.
*/
static void uses_file_append_term(Blob *pSql, int *pnCnt, int rid){
  if( *pnCnt==0 ){
    blob_append_sql(pSql, "(%d)", rid);
    *pnCnt = 4;
  }else if( (*pnCnt)%10==9 ){
    blob_append_sql(pSql, ",\n  (%d)", rid);
  }else{
    blob_append_sql(pSql, ",(%d)", rid);
  }
  ++*pnCnt;
}


/*
** Add to table zTab the record ID (rid) of every check-in that contains
** the file fid.
*/
void compute_uses_file(const char *zTab, int fid, int usesFlags){
  Bag seen;
  Bag pending;
  Blob ins = BLOB_INITIALIZER;
  int nIns = 0;
  Stmt q;
  int rid;

  bag_init(&seen);
  bag_init(&pending);
  blob_append_sql(&ins, "INSERT OR IGNORE INTO \"%w\" VALUES", zTab);
  db_prepare(&q, "SELECT mid FROM mlink WHERE fid=%d", fid);
  while( db_step(&q)==SQLITE_ROW ){
    int mid = db_column_int(&q, 0);
    bag_insert(&pending, mid);
    bag_insert(&seen, mid);
    uses_file_append_term(&ins, &nIns, mid);
  }
  db_finalize(&q);

  db_prepare(&q, "SELECT mid FROM mlink WHERE pid=%d", fid);
  while( db_step(&q)==SQLITE_ROW ){
    int mid = db_column_int(&q, 0);
    bag_insert(&seen, mid);
    if( usesFlags & USESFILE_DELETE ){
      uses_file_append_term(&ins, &nIns, mid);
    }
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT cid FROM plink WHERE pid=:rid AND isprim");

  while( (rid = bag_first(&pending))!=0 ){
    bag_remove(&pending, rid);
    db_bind_int(&q, ":rid", rid);
    while( db_step(&q)==SQLITE_ROW ){
      int mid = db_column_int(&q, 0);
      if( bag_find(&seen, mid) ) continue;
      bag_insert(&seen, mid);
      bag_insert(&pending, mid);
      uses_file_append_term(&ins, &nIns, mid);
    }
    db_reset(&q);
  }
  db_finalize(&q);
  db_exec_sql(blob_str(&ins));
  blob_reset(&ins);
  bag_clear(&seen);
  bag_clear(&pending);
}
