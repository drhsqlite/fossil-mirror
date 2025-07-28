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
** This file contains code used to merge two or more branches into
** a single tree.
*/
#include "config.h"
#include "merge.h"
#include <assert.h>


/*
** Bring up a Tcl/Tk GUI to show details of the most recent merge.
*/
static void merge_info_tk(int bDark, int bAll, int nContext){
  int i;
  Blob script;
  const char *zTempFile = 0;
  int bDebug;
  char *zCmd;
  const char *zTclsh;
  zTclsh = find_option("tclsh",0,1);
  if( zTclsh==0 ){
    zTclsh = db_get("tclsh",0);
  }
  /* The undocumented --script FILENAME option causes the Tk script to
  ** be written into the FILENAME instead of being run.  This is used
  ** for testing and debugging. */
  zTempFile = find_option("script",0,1);
  bDebug = find_option("tkdebug",0,0)!=0;
  verify_all_options();

  blob_zero(&script);
  blob_appendf(&script, "set ncontext %d\n", nContext);
  blob_appendf(&script, "set fossilexe {\"%/\"}\n", g.nameOfExe);
  blob_appendf(&script, "set fossilcmd {| \"%/\" merge-info}\n",
               g.nameOfExe);
  blob_appendf(&script, "set filelist [list");
  if( g.argc==2 ){
    /* No files named on the command-line.  Use every file mentioned
    ** in the MERGESTAT table to generate the file list. */
    Stmt q;
    int cnt = 0;
    db_prepare(&q,
       "WITH priority(op,pri) AS (VALUES('CONFLICT',0),('ERROR',0),"
                                       "('MERGE',1),('ADDED',2),('UPDATE',2))"
       "SELECT coalesce(fnr,fn), op FROM mergestat JOIN priority USING(op)"
           " %s ORDER BY pri, 1",
       bAll ? "" : "WHERE op IN ('MERGE','CONFLICT')" /*safe-for-%s*/
    );
    while( db_step(&q)==SQLITE_ROW ){
      blob_appendf(&script," %s ", db_column_text(&q,1));
      blob_append_tcl_literal(&script, db_column_text(&q,0),
                              db_column_bytes(&q,0));
      cnt++;
    }
    db_finalize(&q);
    if( cnt==0 ){
      fossil_print(
        "No interesting changes in this merge. Use --all to see everything\n"
      );
      return;
    }
  }else{
    /* Use only files named on the command-line in the file list.
    ** But verify each file named is actually found in the MERGESTAT
    ** table first. */
    for(i=2; i<g.argc; i++){
      char *zFile;          /* Input filename */
      char *zTreename;      /* Name of the file in the tree */
      Blob fname;           /* Filename relative to root */
      char *zOp;            /* Operation on this file */
      zFile = mprintf("%/", g.argv[i]);
      file_tree_name(zFile, &fname, 0, 1);
      fossil_free(zFile);
      zTreename = blob_str(&fname);
      zOp = db_text(0, "SELECT op FROM mergestat WHERE fn=%Q or fnr=%Q",
                       zTreename, zTreename);
      blob_appendf(&script, " %s ", zOp);
      fossil_free(zOp);
      blob_append_tcl_literal(&script, zTreename, (int)strlen(zTreename));
      blob_reset(&fname);
    }
  }
  blob_appendf(&script, "]\n");
  blob_appendf(&script, "set darkmode %d\n", bDark!=0);
  blob_appendf(&script, "set debug %d\n", bDebug!=0);
  blob_appendf(&script, "%s", builtin_file("merge.tcl", 0));
  if( zTempFile ){
    blob_write_to_file(&script, zTempFile);
    fossil_print("To see the merge, run: %s \"%s\"\n", zTclsh, zTempFile);
  }else{
#if defined(FOSSIL_ENABLE_TCL)
    Th_FossilInit(TH_INIT_DEFAULT);
    if( evaluateTclWithEvents(g.interp, &g.tcl, blob_str(&script),
                              blob_size(&script), 1, 1, 0)==TCL_OK ){
      blob_reset(&script);
      return;
    }
    /*
     * If evaluation of the Tcl script fails, the reason may be that Tk
     * could not be found by the loaded Tcl, or that Tcl cannot be loaded
     * dynamically (e.g. x64 Tcl with x86 Fossil).  Therefore, fallback
     * to using the external "tclsh", if available.
     */
#endif
    zTempFile = write_blob_to_temp_file(&script);
    zCmd = mprintf("%$ %$", zTclsh, zTempFile);
    if( bDebug ){
      fossil_print("%s\n", zCmd);
      fflush(stdout);
    }
    fossil_system(zCmd);
    file_delete(zTempFile);
    fossil_free(zCmd);
  }
  blob_reset(&script);
}

/*
** Generate a TCL list on standard output that can be fed into the
** merge.tcl script to show the details of the most recent merge
** command associated with file "zFName".  zFName must be the filename
** relative to the root of the check-in - in other words a "tree name".
**
** When this routine is called, we know that the mergestat table
** exists, but we do not know if zFName is mentioned in that table.
**
** The diffMode variable has these values:
**
**     0       Standard 3-way diff
**     12      2-way diff between baseline and local
**     13      2-way diff between baseline and merge-in
**     23      2-way diff between local and merge-in
*/
static void merge_info_tcl(const char *zFName, int nContext, int diffMode){
  const char *zTreename;/* Name of the file in the tree */
  Stmt q;               /* To query the MERGESTAT table */
  MergeBuilder mb;      /* The merge builder object */
  Blob pivot,v1,v2,out; /* Blobs for holding content */
  const char *zFN;      /* A filename */
  int rid;              /* RID value */
  int sz;               /* File size value */

  zTreename = zFName;
  db_prepare(&q,
       /*   0    1     2   3     4   5    6     7  */
    "SELECT fnp, ridp, fn, ridv, sz, fnm, ridm, fnr"
    "  FROM mergestat"
    " WHERE fnp=%Q OR fnr=%Q",
    zTreename, zTreename
  );
  if( db_step(&q)!=SQLITE_ROW ){
    db_finalize(&q);
    fossil_print("ERROR {don't know anything about file: %s}\n", zTreename);
    return;
  }
  mergebuilder_init_tcl(&mb);
  mb.nContext = nContext;

  blob_zero(&pivot);
  if( diffMode!=23 ){
    /* Set up the pivot or baseline */
    zFN = db_column_text(&q, 0);
    if( zFN==0 ){
      /* No pivot because the file was added */
      mb.zPivot = "(no baseline)";
    }else{
      mb.zPivot = mprintf("%s (baseline)", file_tail(zFN));
      rid = db_column_int(&q, 1);
      content_get(rid, &pivot);
    }
    mb.pPivot = &pivot;
  }

  blob_zero(&v2);
  if( diffMode!=12 ){
    /* Set up the merge-in as V2 */
    zFN = db_column_text(&q, 5);
    if( zFN==0 ){
      /* File deleted in the merged-in branch */
      mb.zV2 = "(deleted file)";
    }else{
      mb.zV2 = mprintf("%s (merge-in)", file_tail(zFN));
      rid = db_column_int(&q, 6);
      content_get(rid, &v2);
    }
    mb.pV2 = &v2;
  }

  blob_zero(&v1);
  if( diffMode!=13 ){
    /* Set up the local content as V1 */
    zFN = db_column_text(&q, 2);
    if( zFN==0 ){
      /* File added by merge */
      mb.zV1 = "(no original)";
    }else{
      mb.zV1 = mprintf("%s (local)", file_tail(zFN));
      rid = db_column_int(&q, 3);
      sz = db_column_int(&q, 4);
      if( rid==0 && sz>0 ){
        /* The origin file had been edited so we'll have to pull its
        ** original content out of the undo buffer */
        Stmt q2;
        db_prepare(&q2, 
          "SELECT content FROM undo"
          " WHERE pathname=%Q AND octet_length(content)=%d",
          zFN, sz
        );
        blob_zero(&v1);
        if( db_step(&q2)==SQLITE_ROW ){
          db_column_blob(&q2, 0, &v1);
        }else{
          mb.zV1 = "(local content missing)";
        }
        db_finalize(&q2);
      }else{
        /* The origin file was unchanged when the merge first occurred */
        content_get(rid, &v1);
      }
    }
    mb.pV1 = &v1;
  }

  blob_zero(&out);
  if( diffMode==0 ){
    /* Set up the output and do a 3-way diff */
    zFN = db_column_text(&q, 7);
    if( zFN==0 ){
      mb.zOut = "(Merge Result)";
    }else{
      mb.zOut = mprintf("%s (after merge)", file_tail(zFN));
    }
    mb.pOut = &out;
    merge_three_blobs(&mb);
  }else{
    /* Set up to do a two-way diff */
    Blob *pLeft, *pRight;
    const char *zTagLeft, *zTagRight;
    DiffConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.diffFlags = DIFF_TCL;
    cfg.nContext = mb.nContext;
    if( diffMode==12 || diffMode==13 ){
      pLeft = &pivot;
      zTagLeft = "baseline";
    }else{
      pLeft = &v1;
      zTagLeft = "local";
    }
    if( diffMode==12 ){
      pRight = &v1;
      zTagRight = "local";
    }else{
      pRight = &v2;
      zTagRight = "merge-in";
    }
    cfg.azLabel[0] = mprintf("%s (%s)", zFName, zTagLeft);
    cfg.azLabel[1] = mprintf("%s (%s)", zFName, zTagRight);
    diff_print_filenames("", "", &cfg, &out);
    text_diff(pLeft, pRight, &out, &cfg);
    fossil_free((char*)cfg.azLabel[0]);
    fossil_free((char*)cfg.azLabel[1]);
  }

  blob_write_to_file(&out, "-");
  mb.xDestroy(&mb);
  blob_reset(&pivot);
  blob_reset(&v1);
  blob_reset(&v2);
  blob_reset(&out);
  db_finalize(&q);
}

/*
** COMMAND: merge-info
**
** Usage: %fossil merge-info [OPTIONS]
**
** Display information about the most recent merge operation.
**
** Options:
**   -a|--all             Show all file changes that happened because of
**                        the merge.  Normally only MERGE, CONFLICT, and ERROR
**                        lines are shown
**   -c|--context N       Show N lines of context around each change,
**                        with negative N meaning show all content.  Only
**                        meaningful in combination with --tcl or --tk.
**   --dark               Use dark mode for the Tcl/Tk-based GUI
**   --tk                 Bring up a Tcl/Tk GUI that shows the changes
**                        associated with the most recent merge.
**
** Options used internally by --tk:
**   --diff12 FILE        Bring up a separate --tk diff for just the baseline
**                        and local variants of FILE.
**   --diff13 FILE        Like --diff12 but for baseline versus merge-in
**   --diff23 FILE        Like --diff12 but for local versus merge-in
**   --tcl FILE           Generate (to stdout) a TCL list containing
**                        information needed to display the changes to
**                        FILE caused by the most recent merge.  FILE must
**                        be a pathname relative to the root of the check-out.
**
** Debugging options available only when --tk is used:
**   --tkdebug            Show sub-commands run to implement --tk
**   --script FILE        Write script used to implement --tk into FILE
*/
void merge_info_cmd(void){
  const char *zCnt;
  const char *zTcl;
  int bTk;
  int bDark;
  int bAll;
  int nContext;
  Stmt q;
  const char *zWhere;
  int cnt = 0;
  const char *zDiff2 = 0;
  int diffMode = 0;

  db_must_be_within_tree();
  bTk = find_option("tk", 0, 0)!=0;
  zTcl = find_option("tcl", 0, 1);
  zCnt = find_option("context", "c", 1);
  bDark = find_option("dark", 0, 0)!=0;
  bAll = find_option("all", "a", 0)!=0;
  if( (zDiff2 = find_option("diff12", 0, 1))!=0 ){
    diffMode = 12;
  }else
  if( (zDiff2 = find_option("diff13", 0, 1))!=0 ){
    diffMode = 13;
  }else
  if( (zDiff2 = find_option("diff23", 0, 1))!=0 ){
    diffMode = 23;
  }

  if( zCnt ){
    nContext = atoi(zCnt);
    if( nContext<0 ) nContext = 0xfffffff;
  }else{
    nContext = 6;
  }
  if( !db_table_exists("localdb","mergestat") ){
    if( zTcl ){
      fossil_print("ERROR {no merge data available}\n");
    }else{
      fossil_print("No merge data is available\n");
    }
    return;
  }
  if( bTk ){
    merge_info_tk(bDark, bAll, nContext);
    return;
  }
  if( zTcl ){
    if( diffMode ) zTcl = zDiff2;
    merge_info_tcl(zTcl, nContext, diffMode);
    return;
  }
  if( diffMode ){
    char *zCmd;
    zCmd = mprintf("merge-info --diff%d %!$ -c %d%s",
                   diffMode, zDiff2, nContext, bDark ? " --dark" : "");
    diff_tk(zCmd, g.argc);
    fossil_free(zCmd);
    return;
  }

  verify_all_options();
  if( g.argc>2 ){
    usage("[OPTIONS]");
  }

  if( bAll ){
    zWhere = "";
  }else{
    zWhere = "WHERE op IN ('MERGE','CONFLICT','ERROR')";
  }
  db_prepare(&q,
    "WITH priority(op,pri) AS (VALUES('CONFLICT',0),('ERROR',0),"
                                    "('MERGE',1),('ADDED',2),('UPDATE',2))"

        /*  0   1                 2  */
    "SELECT op, coalesce(fnr,fn), msg"
    "  FROM mergestat JOIN priority USING(op)"
    " %s"
    " ORDER BY pri, coalesce(fnr,fn)",
    zWhere /*safe-for-%s*/
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zOp = db_column_text(&q, 0);
    const char *zName = db_column_text(&q, 1);
    const char *zErr = db_column_text(&q, 2);
    if( zErr && fossil_strcmp(zOp,"CONFLICT")!=0 ){
      fossil_print("%-9s %s  (%s)\n", zOp, zName, zErr);
    }else{
      fossil_print("%-9s %s\n", zOp, zName);
    }
    cnt++;
  }
  db_finalize(&q);
  if( !bAll && cnt==0 ){
    fossil_print(
      "No interesting changes in this merge.  Use --all to see everything.\n"
    );
  }
}

/*
** Erase all information about prior merges.  Do this, for example, after
** a commit.
*/
void merge_info_forget(void){
  db_multi_exec(
    "DROP TABLE IF EXISTS localdb.mergestat;"
    "DELETE FROM localdb.vvar WHERE name glob 'mergestat-*';"
  );
}


/*
** Initialize the MERGESTAT table.
**
** Notes about mergestat:
**
**    *  ridv is a positive integer and sz is NULL if the V file contained
**       no local edits prior to the merge.  If the V file was modified prior
**       to the merge then ridv is NULL and sz is the size of the file prior
**       to merge.
**
**    *  fnp, ridp, fn, ridv, and sz are all NULL for a file that was
**       added by merge.
*/
void merge_info_init(void){
  merge_info_forget();
  db_multi_exec(
    "CREATE TABLE localdb.mergestat(\n"
    "  op TEXT,   -- 'UPDATE', 'ADDED', 'MERGE', etc...\n"
    "  fnp TEXT,  -- Name of the pivot file (P)\n"
    "  ridp INT,  -- RID for the pivot file\n"
    "  fn TEXT,   -- Name of origin file (V)\n"
    "  ridv INT,  -- RID for origin file, or NULL if previously edited\n"
    "  sz INT,    -- Size of origin file in bytes, NULL if unedited\n"
    "  fnm TEXT,  -- Name of the file being merged in (M)\n"
    "  ridm INT,  -- RID for the merge-in file\n"
    "  fnr TEXT,  -- Name of the final output file, after all renaming\n"
    "  nc INT DEFAULT 0,    -- Number of conflicts\n"
    "  msg TEXT   -- Error message\n"
    ");"
  );
}

/*
** Print information about a particular check-in.
*/
void print_checkin_description(int rid, int indent, const char *zLabel){
  Stmt q;
  db_prepare(&q,
     "SELECT datetime(mtime,toLocal()),"
     "       coalesce(euser,user), coalesce(ecomment,comment),"
     "       (SELECT uuid FROM blob WHERE rid=%d),"
     "       (SELECT group_concat(substr(tagname,5), ', ') FROM tag, tagxref"
     "         WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid"
     "           AND tagxref.rid=%d AND tagxref.tagtype>0)"
     "  FROM event WHERE objid=%d", rid, rid, rid);
  if( db_step(&q)==SQLITE_ROW ){
    const char *zTagList = db_column_text(&q, 4);
    char *zCom;
    if( zTagList && zTagList[0] ){
      zCom = mprintf("%s (%s)", db_column_text(&q, 2), zTagList);
    }else{
      zCom = fossil_strdup(db_column_text(&q,2));
    }
    fossil_print("%-*s [%S] by %s on %s\n%*s",
       indent-1, zLabel,
       db_column_text(&q, 3),
       db_column_text(&q, 1),
       db_column_text(&q, 0),
       indent, "");
    comment_print(zCom, db_column_text(&q,2), indent, -1, get_comment_format());
    fossil_free(zCom);
  }
  db_finalize(&q);
}


/* Pick the most recent leaf that is (1) not equal to vid and (2)
** has not already been merged into vid and (3) the leaf is not
** closed and (4) the leaf is in the same branch as vid.
**
** Set vmergeFlag to control whether the vmerge table is checked.
*/
int fossil_find_nearest_fork(int vid, int vmergeFlag){
  Blob sql;
  Stmt q;
  int rid = 0;

  blob_zero(&sql);
  blob_append_sql(&sql,
    "SELECT leaf.rid"
    "  FROM leaf, event"
    " WHERE leaf.rid=event.objid"
    "   AND leaf.rid!=%d",                                /* Constraint (1) */
    vid
  );
  if( vmergeFlag ){
    blob_append_sql(&sql,
      "   AND leaf.rid NOT IN (SELECT merge FROM vmerge)"  /* Constraint (2) */
    );
  }
  blob_append_sql(&sql,
    "   AND NOT EXISTS(SELECT 1 FROM tagxref"            /* Constraint (3) */
                  "     WHERE rid=leaf.rid"
                  "       AND tagid=%d"
                  "       AND tagtype>0)"
    "   AND (SELECT value FROM tagxref"                  /* Constraint (4) */
          "   WHERE tagid=%d AND rid=%d AND tagtype>0) ="
          " (SELECT value FROM tagxref"
          "   WHERE tagid=%d AND rid=leaf.rid AND tagtype>0)"
    " ORDER BY event.mtime DESC LIMIT 1",
    TAG_CLOSED, TAG_BRANCH, vid, TAG_BRANCH
  );
  db_prepare(&q, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  if( db_step(&q)==SQLITE_ROW ){
    rid = db_column_int(&q, 0);
  }
  db_finalize(&q);
  return rid;
}

/*
** Check content that was received with rcvid and return true if any
** fork was created.
*/
int fossil_any_has_fork(int rcvid){
  static Stmt q;
  int fForkSeen = 0;

  if( rcvid==0 ) return 0;
  db_static_prepare(&q,
    "  SELECT pid FROM plink WHERE pid>0 AND isprim"
    "     AND cid IN (SELECT blob.rid FROM blob"
    "   WHERE rcvid=:rcvid)");
  db_bind_int(&q, ":rcvid", rcvid);
  while( !fForkSeen && db_step(&q)==SQLITE_ROW ){
    int pid = db_column_int(&q, 0);
    if( count_nonbranch_children(pid)>1 ){
      compute_leaves(pid,1);
      if( db_int(0, "SELECT count(*) FROM leaves")>1 ){
        int rid = db_int(0, "SELECT rid FROM leaves, event"
                            " WHERE event.objid=leaves.rid"
                            " ORDER BY event.mtime DESC LIMIT 1");
        fForkSeen = fossil_find_nearest_fork(rid, db_open_local(0))!=0;
      }
    }
  }
  db_finalize(&q);
  return fForkSeen;
}

/*
** Add an entry to the FV table for all files renamed between
** version N and the version specified by vid.
*/
static void add_renames(
  const char *zFnCol, /* The FV column for the filename in vid */
  int vid,            /* The desired version's- RID */
  int nid,            /* The check-in rid for the name pivot */
  int revOK,          /* OK to move backwards (child->parent) if true */
  const char *zDebug  /* Generate trace output if not NULL */
){
  int nChng;  /* Number of file name changes */
  int *aChng; /* An array of file name changes */
  int i;      /* Loop counter */
  find_filename_changes(nid, vid, revOK, &nChng, &aChng, zDebug);
  if( nChng==0 ) return;
  for(i=0; i<nChng; i++){
    char *zN, *zV;
    zN = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2]);
    zV = db_text(0, "SELECT name FROM filename WHERE fnid=%d", aChng[i*2+1]);
    db_multi_exec(
      "INSERT OR IGNORE INTO fv(%s,fnn) VALUES(%Q,%Q)",
      zFnCol /*safe-for-%s*/, zV, zN
    );
    if( db_changes()==0 ){
      db_multi_exec(
        "UPDATE fv SET %s=%Q WHERE fnn=%Q",
        zFnCol /*safe-for-%s*/, zV, zN
      );
    }
    free(zN);
    free(zV);
  }
  free(aChng);
}

/* Make an entry in the vmerge table for the given id, and rid.
*/
static void vmerge_insert(int id, int rid){
  db_multi_exec(
    "INSERT OR IGNORE INTO vmerge(id,merge,mhash)"
    "VALUES(%d,%d,(SELECT uuid FROM blob WHERE rid=%d))",
    id, rid, rid
  );
}

/*
** Print the contents of the "fv" table on standard output, for debugging
** purposes.
**
** Only show entries where a file has changed, unless showAll is true.
*/
static void debug_fv_dump(int showAll){
  Stmt q;
  if( showAll ){
    db_prepare(&q,
       "SELECT rowid, fn, fnp, fnm, chnged, ridv, ridp, ridm, "
       "       isexe, islinkv, islinkm, fnn FROM fv"
    );
  }else{
    db_prepare(&q,
       "SELECT rowid, fn, fnp, fnm, chnged, ridv, ridp, ridm, "
       "       isexe, islinkv, islinkm, fnn FROM fv"
       " WHERE chnged OR (ridv!=ridm AND ridm!=ridp)"
    );
  }
  while( db_step(&q)==SQLITE_ROW ){
     fossil_print("%3d: ridv=%-4d ridp=%-4d ridm=%-4d chnged=%d isexe=%d "
                  " islinkv=%d islinkm=%d\n",
        db_column_int(&q, 0),
        db_column_int(&q, 5),
        db_column_int(&q, 6),
        db_column_int(&q, 7),
        db_column_int(&q, 4),
        db_column_int(&q, 8),
        db_column_int(&q, 9),
        db_column_int(&q, 10));
     fossil_print("     fn  = [%s]\n", db_column_text(&q, 1));
     fossil_print("     fnp = [%s]\n", db_column_text(&q, 2));
     fossil_print("     fnm = [%s]\n", db_column_text(&q, 3));
     fossil_print("     fnn = [%s]\n", db_column_text(&q, 11));
  }
  db_finalize(&q);
}

/*
** Print the content of the VFILE table on standard output, for
** debugging purposes.
*/
static void debug_show_vfile(void){
  Stmt q;
  int pvid = -1;
  db_prepare(&q,
    "SELECT vid, id, chnged, deleted, isexe, islink, rid, mrid, mtime,"
          " pathname, origname, mhash FROM vfile"
    " ORDER BY vid, pathname"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int vid = db_column_int(&q, 0);
    int chnged = db_column_int(&q, 2);
    int dltd = db_column_int(&q, 3);
    int isexe = db_column_int(&q, 4);
    int islnk = db_column_int(&q, 5);
    int rid = db_column_int(&q, 6);
    int mrid = db_column_int(&q, 7);
    const char *zPath = db_column_text(&q, 9);
    const char *zOrig = db_column_text(&q, 10);
    if( vid!=pvid ){
      fossil_print("VFILE vid=%d (%z):\n", vid,
         db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid));
      pvid = vid;
    }
    fossil_print("   rid %-6d mrid %-6d %4s %3s %3s %3s %s",
       rid, mrid,
       chnged ? "chng" : "",
       dltd   ? "del" : "",
       isexe  ? "exe" : "",
       islnk  ? "lnk" : "", zPath);
    if( zOrig && zOrig[0] ){
      fossil_print(" <- %s\n", zOrig);
    }else{
      fossil_print("\n");
    }
  }
  db_finalize(&q);
}

/*
** COMMAND: test-show-vfile
** Usage:  %fossil test-show-vfile
**
** Show the content of the VFILE table in a local check-out.
*/
void test_show_vfile_cmd(void){
  if( g.argc!=2 ){
    fossil_fatal("unknown arguments to the %s command\n", g.argv[1]);
  }
  verify_all_options();
  db_must_be_within_tree();
  debug_show_vfile();
}


/*
** COMMAND: merge
** COMMAND: cherry-pick  alias
** COMMAND: cherrypick
**
** Usage: %fossil merge ?OPTIONS? ?VERSION ...?
** Or:    %fossil cherrypick ?OPTIONS? ?VERSION ...?
**
** The argument VERSION is a version that should be merged into the
** current check-out.  All changes from VERSION back to the nearest
** common ancestor are merged.  Except, if either of the --cherrypick
** or --backout options are used only the changes associated with the
** single check-in VERSION are merged.  The --backout option causes
** the changes associated with VERSION to be removed from the current
** check-out rather than added.  When invoked with the name
** "cherrypick" instead of "merge", this command works exactly like
** "merge --cherrypick".
**
** Files which are renamed in the merged-in branch will be renamed in
** the current check-out.
**
** If the VERSION argument is omitted, then Fossil attempts to find
** a recent fork on the current branch to merge.
**
** Note that this command does not commit the merge, as that is a
** separate step.
**
** If there are multiple VERSION arguments, then each VERSION is merged
** (or cherrypicked) in the order that they appear on the command-line.
**
** Options:
**   --backout               Do a reverse cherrypick merge against VERSION.
**                           In other words, back out the changes that were
**                           added by VERSION.
**   --baseline BASELINE     Use BASELINE as the "pivot" of the merge instead
**                           of the nearest common ancestor.  This allows
**                           a sequence of changes in a branch to be merged
**                           without having to merge the entire branch.
**   --binary GLOBPATTERN    Treat files that match GLOBPATTERN as binary
**                           and do not try to merge parallel changes.  This
**                           option overrides the "binary-glob" setting.
**   --cherrypick            Do a cherrypick merge VERSION into the current
**                           check-out.  A cherrypick merge pulls in the changes
**                           of the single check-in VERSION, rather than all
**                           changes back to the nearest common ancestor.
**   -f|--force              Force the merge even if it would be a no-op
**   --force-missing         Force the merge even if there is missing content
**   --integrate             Merged branch will be closed when committing
**   -K|--keep-merge-files   On merge conflict, retain the temporary files
**                           used for merging, named *-baseline, *-original,
**                           and *-merge.
**   -n|--dry-run            Do not actually change files on disk
**   --nosync                Do not auto-sync prior to merging
**   --noundo                Do not record changes in the undo log
**   -v|--verbose            Show additional details of the merge
*/
void merge_cmd(void){
  int vid;              /* Current version "V" */
  int mid;              /* Version we are merging from "M" */
  int pid = 0;          /* The pivot version - most recent common ancestor P */
  int nid = 0;          /* The name pivot version "N" */
  int verboseFlag;      /* True if the -v|--verbose option is present */
  int integrateFlag;    /* True if the --integrate option is present */
  int pickFlag;         /* True if the --cherrypick option is present */
  int backoutFlag;      /* True if the --backout option is present */
  int dryRunFlag;       /* True if the --dry-run or -n option is present */
  int forceFlag;        /* True if the --force or -f option is present */
  int forceMissingFlag; /* True if the --force-missing option is present */
  const char *zBinGlob; /* The value of --binary */
  const char *zPivot;   /* The value of --baseline */
  int debugFlag;        /* True if --debug is present */
  int showVfileFlag;    /* True if the --show-vfile flag is present */
  int keepMergeFlag;    /* True if --keep-merge-files is present */
  int nConflict = 0;    /* Number of conflicts seen */
  int nOverwrite = 0;   /* Number of unmanaged files overwritten */
  char vAncestor = 'p'; /* If P is an ancestor of V then 'p', else 'n' */
  const char *zVersion; /* The VERSION argument */
  int bMultiMerge = 0;  /* True if there are two or more VERSION arguments */
  int nMerge = 0;       /* Number of prior merges processed */
  int useUndo = 1;      /* True to record changes in the undo log */
  Stmt q;               /* SQL statment used for merge processing */


  /* Notation:
  **
  **      V     The current check-out
  **      M     The version being merged in
  **      P     The "pivot" - the most recent common ancestor of V and M.
  **      N     The "name pivot" - for detecting renames
  */

  undo_capture_command_line();
  verboseFlag = find_option("verbose","v",0)!=0;
  forceMissingFlag = find_option("force-missing",0,0)!=0;
  if( !verboseFlag ){
    verboseFlag = find_option("detail",0,0)!=0; /* deprecated */
  }
  pickFlag = find_option("cherrypick",0,0)!=0;
  if('c'==*g.zCmdName /*called as cherrypick, possibly a short form*/){
    pickFlag = 1;
  }
  integrateFlag = find_option("integrate",0,0)!=0;
  backoutFlag = find_option("backout",0,0)!=0;
  zBinGlob = find_option("binary",0,1);
  dryRunFlag = find_option("dry-run","n",0)!=0;
  if( !dryRunFlag ){
    dryRunFlag = find_option("nochange",0,0)!=0; /* deprecated */
  }
  if( find_option("nosync",0,0) ) g.fNoSync = 1;
  forceFlag = find_option("force","f",0)!=0;
  zPivot = find_option("baseline",0,1);
  keepMergeFlag = find_option("keep-merge-files", "K",0)!=0;

  /* Undocumented --debug and --show-vfile options:
  **
  ** When included on the command-line, --debug causes lots of state
  ** information to be displayed.  This option is undocumented as it
  ** might change or be eliminated in future releases.
  **
  ** The --show-vfile flag does a dump of the VFILE table for reference.
  **
  ** Hints:
  **   *  Combine --debug and --verbose for still more output.
  **   *  The --dry-run option is also useful in combination with --debug.
  */
  debugFlag = find_option("debug",0,0)!=0;
  if( debugFlag && verboseFlag ) debugFlag = 2;
  showVfileFlag = find_option("show-vfile",0,0)!=0;
  useUndo = find_option("noundo",0,0)==0;
  if( dryRunFlag ) useUndo = 0;

  verify_all_options();
  db_must_be_within_tree();
  if( zBinGlob==0 ) zBinGlob = db_get("binary-glob",0);
  vid = db_lget_int("checkout", 0);
  if( vid==0 ){
    fossil_fatal("nothing is checked out");
  }
  if( forceFlag==0 && leaf_is_closed(vid) ){
    fossil_fatal("cannot merge into a closed leaf. Use --force to override");
  }
  if( !dryRunFlag ){
    if( autosync_loop(SYNC_PULL + SYNC_VERBOSE*verboseFlag, 1, "merge") ){
      fossil_fatal("merge abandoned due to sync failure");
    }
  }

  /*
  ** A "multi-merge" means two or more other check-ins are being merged
  ** into the current check-in.  In other words, there are two or more
  ** VERSION arguments on the command-line.  Multi-merge works by doing
  ** the merges one by one, as long as there are no conflicts.  At the
  ** bottom of this routine, a jump is made back up to this point if there
  ** are more merges yet to be done and no errors have yet been seen.
  **
  ** Related variables:
  **    bMultiMerge       True if there are one or more merges yet to do
  **    zVersion          The name of the current checking being merged in
  **    nMerge            Number of prior merges
  */
merge_next_child:

  /* Find mid, the artifactID of the version to be merged into
  ** the current check-out.
  */
  if( g.argc>=3 ){
    int i;
    /* Mid is specified as an argument on the command-line */
    zVersion = g.argv[2];
    mid = name_to_typed_rid(zVersion, "ci");
    if( mid==0 || !is_a_version(mid) ){
      fossil_fatal("not a version: %s", zVersion);
    }
    bMultiMerge = g.argc>3;
    if( bMultiMerge ){
      for(i=3; i<g.argc; i++) g.argv[i-1] = g.argv[i];
      g.argc--;
    }
  }else if( g.argc==2 ){
    /* No version specified on the command-line so pick the most recent
    ** leaf that is (1) not the version currently checked out and (2)
    ** has not already been merged into the current check-out and (3)
    ** the leaf is not closed and (4) the leaf is in the same branch
    ** as the current check-out.
    */
    Stmt q;
    if( pickFlag || backoutFlag || integrateFlag){
      fossil_fatal("cannot use --backout, --cherrypick or --integrate "
                   "with a fork merge");
    }
    mid = fossil_find_nearest_fork(vid, db_open_local(0));
    if( mid==0 ){
      fossil_fatal("no unmerged forks of branch \"%s\"",
        db_text(0, "SELECT value FROM tagxref"
                   " WHERE tagid=%d AND rid=%d AND tagtype>0",
                   TAG_BRANCH, vid)
      );
    }
    db_prepare(&q,
      "SELECT blob.uuid,"
          "   datetime(event.mtime,toLocal()),"
          "   coalesce(ecomment, comment),"
          "   coalesce(euser, user)"
      "  FROM event, blob"
      " WHERE event.objid=%d AND blob.rid=%d",
      mid, mid
    );
    zVersion = 0;
    if( db_step(&q)==SQLITE_ROW ){
      char *zCom = mprintf("Merging fork [%S] at %s by %s: \"%s\"",
            db_column_text(&q, 0), db_column_text(&q, 1),
            db_column_text(&q, 3), db_column_text(&q, 2));
      comment_print(zCom, db_column_text(&q,2), 0, -1, get_comment_format());
      fossil_free(zCom);
      zVersion = mprintf("%S",db_column_text(&q,0));
    }
    db_finalize(&q);
  }else{
    usage("?OPTIONS? ?VERSION?");
    return;
  }

  if( zPivot ){
    pid = name_to_typed_rid(zPivot, "ci");
    if( pid==0 || !is_a_version(pid) ){
      fossil_fatal("not a version: %s", zPivot);
    }
    if( pickFlag ){
      fossil_fatal("incompatible options: --cherrypick and --baseline");
    }
  }
  if( pickFlag || backoutFlag ){
    if( integrateFlag ){
      fossil_fatal("incompatible options: --integrate and --cherrypick "
                   "with --backout");
    }
    pid = db_int(0, "SELECT pid FROM plink WHERE cid=%d AND isprim", mid);
    if( pid<=0 ){
      fossil_fatal("cannot find an ancestor for %s", zVersion);
    }
  }else{
    if( !zPivot ){
      pivot_set_primary(mid);
      pivot_set_secondary(vid);
      db_prepare(&q, "SELECT merge FROM vmerge WHERE id=0");
      while( db_step(&q)==SQLITE_ROW ){
        pivot_set_secondary(db_column_int(&q,0));
      }
      db_finalize(&q);
      pid = pivot_find(0);
      if( pid<=0 ){
        fossil_fatal("cannot find a common ancestor between the current "
                     "check-out and %s", zVersion);
      }
    }
    pivot_set_primary(mid);
    pivot_set_secondary(vid);
    nid = pivot_find(1);
    if( nid!=pid ){
      pivot_set_primary(nid);
      pivot_set_secondary(pid);
      nid = pivot_find(1);
    }
  }
  if( backoutFlag ){
    int t = pid;
    pid = mid;
    mid = t;
  }
  if( nid==0 ) nid = pid;
  if( !is_a_version(pid) ){
    fossil_fatal("not a version: record #%d", pid);
  }
  if( !forceFlag && mid==pid ){
    fossil_print("Merge skipped because it is a no-op. "
                 " Use --force to override.\n");
    return;
  }
  if( integrateFlag && !is_a_leaf(mid)){
    fossil_warning("ignoring --integrate: %s is not a leaf", zVersion);
    integrateFlag = 0;
  }
  if( integrateFlag && content_is_private(mid) ){
    fossil_warning(
      "ignoring --integrate: %s is on a private branch"
      "\n Use \"fossil amend --close\" (after commit) to close the leaf.",
      zVersion);
    integrateFlag = 0;
  }
  if( verboseFlag ){
    print_checkin_description(mid, 12,
              integrateFlag ? "integrate:" : "merge-from:");
    print_checkin_description(pid, 12, "baseline:");
  }
  vfile_check_signature(vid, CKSIG_ENOTFILE);
  if( nMerge==0 ) db_begin_transaction();
  if( useUndo ) undo_begin();
  if( load_vfile_from_rid(mid) && !forceMissingFlag ){
    fossil_fatal("missing content, unable to merge");
  }
  if( load_vfile_from_rid(pid) && !forceMissingFlag ){
    fossil_fatal("missing content, unable to merge");
  }
  if( zPivot ){
    vAncestor = db_exists(
      "WITH RECURSIVE ancestor(id) AS ("
      "  VALUES(%d)"
      "  UNION"
      "  SELECT pid FROM plink, ancestor"
      "   WHERE cid=ancestor.id AND pid!=%d AND cid!=%d)"
      "SELECT 1 FROM ancestor WHERE id=%d LIMIT 1",
      vid, nid, pid, pid
    ) ? 'p' : 'n';
  }
  if( debugFlag ){
    char *z;
    z = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", nid);
    fossil_print("N=%-4d %z (file rename pivot)\n", nid, z);
    z = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", pid);
    fossil_print("P=%-4d %z (file content pivot)\n", pid, z);
    z = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", mid);
    fossil_print("M=%-4d %z (merged-in version)\n", mid, z);
    z = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", vid);
    fossil_print("V=%-4d %z (current version)\n", vid, z);
    fossil_print("vAncestor = '%c'\n", vAncestor);
  }
  if( showVfileFlag ) debug_show_vfile();

  /*
  ** The vfile.pathname field is used to match files against each other.  The
  ** FV table contains one row for each each unique filename in
  ** in the current check-out, the pivot, and the version being merged.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS fv;"
    "CREATE TEMP TABLE fv(\n"
    "  fn TEXT UNIQUE %s,\n"       /* The filename */
    "  idv INTEGER DEFAULT 0,\n"   /* VFILE entry for current version */
    "  idp INTEGER DEFAULT 0,\n"   /* VFILE entry for the pivot */
    "  idm INTEGER DEFAULT 0,\n"   /* VFILE entry for version merging in */
    "  chnged BOOLEAN,\n"          /* True if current version has been edited */
    "  ridv INTEGER DEFAULT 0,\n"  /* Record ID for current version */
    "  ridp INTEGER DEFAULT 0,\n"  /* Record ID for pivot */
    "  ridm INTEGER DEFAULT 0,\n"  /* Record ID for merge */
    "  isexe BOOLEAN,\n"           /* Execute permission enabled */
    "  fnp TEXT UNIQUE %s,\n"      /* The filename in the pivot */
    "  fnm TEXT UNIQUE %s,\n"      /* The filename in the merged version */
    "  fnn TEXT UNIQUE %s,\n"      /* The filename in the name pivot */
    "  islinkv BOOLEAN,\n"         /* True if current version is a symlink */
    "  islinkm BOOLEAN\n"          /* True if merged version in is a symlink */
    ");",
    filename_collation(), filename_collation(), filename_collation(),
    filename_collation()
  );

  /*
  ** Compute name changes from N to V, P, and M
  */
  add_renames("fn", vid, nid, 0, debugFlag ? "N->V" : 0);
  add_renames("fnp", pid, nid, 0, debugFlag ? "N->P" : 0);
  add_renames("fnm", mid, nid, backoutFlag, debugFlag ? "N->M" : 0);
  if( debugFlag ){
    fossil_print("******** FV after name change search *******\n");
    debug_fv_dump(1);
  }
  if( nid!=pid ){
    /* See forum thread https://fossil-scm.org/forum/forumpost/549700437b
    **
    ** If a filename changes between nid and one of the other check-ins
    ** pid, vid, or mid, then it might not have changed for all of them.
    ** try to fill in the appropriate filename in all slots where the
    ** name is missing.
    **
    ** This does not work if
    **   (1) The filename changes more than once in between nid and vid/mid
    **   (2) Two or more filenames swap places - for example if A is renamed
    **       to B and B is renamed to A.
    ** The Fossil merge algorithm breaks down in those cases.  It will need
    ** to be completely rewritten to handle such complex cases.  Such cases
    ** appear to be rare, and also confusing to humans.
    */
    db_multi_exec(
      "UPDATE OR IGNORE fv SET fnp=vfile.pathname FROM vfile"
      " WHERE fnp IS NULL"
      " AND vfile.pathname = fv.fnn"
      " AND vfile.vid=%d;",
      pid
    );
    db_multi_exec(
      "UPDATE OR IGNORE fv SET fn=vfile.pathname FROM vfile"
      " WHERE fn IS NULL"
      " AND vfile.pathname = coalesce(fv.fnp,fv.fnn)"
      " AND vfile.vid=%d;",
      vid
    );
    db_multi_exec(
      "UPDATE OR IGNORE fv SET fnm=vfile.pathname FROM vfile"
      " WHERE fnm IS NULL"
      " AND vfile.pathname = coalesce(fv.fnp,fv.fnn)"
      " AND vfile.vid=%d;",
      mid
    );
    db_multi_exec(
      "UPDATE OR IGNORE fv SET fnp=vfile.pathname FROM vfile"
      " WHERE fnp IS NULL"
      " AND vfile.pathname IN (fv.fnm,fv.fn)"
      " AND vfile.vid=%d;",
      pid
    );
    db_multi_exec(
      "UPDATE OR IGNORE fv SET fn=vfile.pathname FROM vfile"
      " WHERE fn IS NULL"
      " AND vfile.pathname = fv.fnm"
      " AND vfile.vid=%d;",
      vid
    );
    db_multi_exec(
      "UPDATE OR IGNORE fv SET fnm=vfile.pathname FROM vfile"
      " WHERE fnm IS NULL"
      " AND vfile.pathname = fv.fn"
      " AND vfile.vid=%d;",
      mid
    );
  }
  if( debugFlag ){
    fossil_print("******** FV after name change fill-in *******\n");
    debug_fv_dump(1);
  }

  /*
  ** Add files found in V
  */
  db_multi_exec(
    "UPDATE OR IGNORE fv SET fn=coalesce(fn%c,fnn) WHERE fn IS NULL;"
    "REPLACE INTO fv(fn,fnp,fnm,fnn,idv,ridv,islinkv,isexe,chnged)"
    " SELECT pathname, fnp, fnm, fnn, id, rid, islink, vf.isexe, vf.chnged"
    "   FROM vfile vf"
    "   LEFT JOIN fv ON fn=coalesce(origname,pathname)"
    "    AND rid>0 AND vf.chnged NOT IN (3,5)"
    "  WHERE vid=%d;",
    vAncestor, vid
  );
  if( debugFlag>=2 ){
    fossil_print("******** FV after adding files in current version *******\n");
    debug_fv_dump(1);
  }

  /*
  ** Add files found in P
  */
  db_multi_exec(
    "UPDATE OR IGNORE fv SET fnp=coalesce(fnn,"
    "   (SELECT coalesce(origname,pathname) FROM vfile WHERE id=idv))"
    " WHERE fnp IS NULL;"
    "INSERT OR IGNORE INTO fv(fnp)"
    " SELECT coalesce(origname,pathname) FROM vfile WHERE vid=%d;",
    pid
  );
  if( debugFlag>=2 ){
    fossil_print("******** FV after adding pivot files *******\n");
    debug_fv_dump(1);
  }

  /*
  ** Add files found in M
  */
  db_multi_exec(
    "UPDATE OR IGNORE fv SET fnm=fnp WHERE fnm IS NULL;"
    "INSERT OR IGNORE INTO fv(fnm)"
    " SELECT pathname FROM vfile WHERE vid=%d;",
    mid
  );
  if( debugFlag>=2 ){
    fossil_print("******** FV after adding merge-in files *******\n");
    debug_fv_dump(1);
  }

  /*
  ** Compute the file version ids for P and M
  */
  if( pid==vid ){
    db_multi_exec(
      "UPDATE fv SET idp=idv, ridp=ridv WHERE ridv>0 AND chnged NOT IN (3,5)"
    );
  }else{
    db_multi_exec(
      "UPDATE fv SET idp=coalesce(vfile.id,0), ridp=coalesce(vfile.rid,0)"
      "  FROM vfile"
      " WHERE vfile.vid=%d AND fv.fnp=vfile.pathname",
      pid
    );
  }
  db_multi_exec(
    "UPDATE fv SET"
    " idm=coalesce(vfile.id,0),"
    " ridm=coalesce(vfile.rid,0),"
    " islinkm=coalesce(vfile.islink,0),"
    " isexe=coalesce(vfile.isexe,fv.isexe)"
    " FROM vfile"
    " WHERE vid=%d AND fnm=pathname",
    mid
  );

  /*
  ** Update the execute bit on files where it's changed from P->M but not P->V
  */
  db_prepare(&q,
    "SELECT idv, fn, fv.isexe FROM fv, vfile p, vfile v"
    " WHERE p.id=idp AND v.id=idv AND fv.isexe!=p.isexe AND v.isexe=p.isexe"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    const char *zName = db_column_text(&q, 1);
    int isExe = db_column_int(&q, 2);
    fossil_print("%s %s\n", isExe ? "EXECUTABLE" : "UNEXEC", zName);
    if( !dryRunFlag ){
      char *zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
      file_setexe(zFullPath, isExe);
      free(zFullPath);
      db_multi_exec("UPDATE vfile SET isexe=%d WHERE id=%d", isExe, idv);
    }
  }
  db_finalize(&q);
  if( debugFlag ){
    fossil_print("******** FV final *******\n");
    debug_fv_dump( debugFlag>=2 );
  }

  /************************************************************************
  ** All of the information needed to do the merge is now contained in the
  ** FV table.  Starting here, we begin to actually carry out the merge.
  **
  ** Begin by constructing the localdb.mergestat table. 
  */
  merge_info_init();

  /*
  ** Find files that have changed from P->M but not P->V.
  ** Copy the M content over into V.
  */
  db_prepare(&q,
    /*      0    1     2   3        4    5     6     7   */
    "SELECT idv, ridm, fn, islinkm, fnp, ridp, ridv, fnm FROM fv"
    " WHERE idp>0 AND idv>0 AND idm>0"
    "   AND ridm!=ridp AND ridv=ridp AND NOT chnged"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    int ridm = db_column_int(&q, 1);
    const char *zName = db_column_text(&q, 2);
    int islinkm = db_column_int(&q, 3);
    /* Copy content from idm over into idv.  Overwrite idv. */
    fossil_print("UPDATE %s\n", zName);
    if( useUndo ) undo_save(zName);
    if( !dryRunFlag ){
      db_multi_exec(
        "UPDATE vfile SET mtime=0, mrid=%d, chnged=%d, islink=%d,"
        " mhash=CASE WHEN rid<>%d"
                   " THEN (SELECT uuid FROM blob WHERE blob.rid=%d) END"
        " WHERE id=%d", ridm, integrateFlag?4:2, islinkm, ridm, ridm, idv
      );
      vfile_to_disk(0, idv, 0, 0);
    }
    db_multi_exec(
      "INSERT INTO mergestat(op,fnp,ridp,fn,ridv,fnm,ridm,fnr)"
      "VALUES('UPDATE',%Q,%d,%Q,%d,%Q,%d,%Q)",
      /* fnp   */ db_column_text(&q, 4),
      /* ridp  */ db_column_int(&q,5),
      /* fn    */ zName,
      /* ridv  */ db_column_int(&q,6),
      /* fnm   */ db_column_text(&q, 7),
      /* ridm  */ ridm,
      /* fnr   */ zName
    ); 
  }
  db_finalize(&q);

  /*
  ** Do a three-way merge on files that have changes on both P->M and P->V.
  **
  ** Proceed even if the file doesn't exist on P, just like the common ancestor
  ** of M and V is an empty file. In this case, merge conflict marks will be
  ** added to the file and user will be forced to take a decision.
  */
  db_prepare(&q,
        /*  0     1    2     3     4   5   6      7        8 */
    "SELECT ridm, idv, ridp, ridv, %z, fn, isexe, islinkv, islinkm,"
        /*  9     10   11   */
    "       fnp,  fnm, chnged"
    "  FROM fv"
    " WHERE idv>0 AND idm>0"
    "   AND ridm!=ridp AND (ridv!=ridp OR chnged)",
    glob_expr("fv.fn", zBinGlob)
  );
  while( db_step(&q)==SQLITE_ROW ){
    int ridm = db_column_int(&q, 0);
    int idv = db_column_int(&q, 1);
    int ridp = db_column_int(&q, 2);
    int ridv = db_column_int(&q, 3);
    int isBinary = db_column_int(&q, 4);
    const char *zName = db_column_text(&q, 5);
    int isExe = db_column_int(&q, 6);
    int islinkv = db_column_int(&q, 7);
    int islinkm = db_column_int(&q, 8);
    int chnged = db_column_int(&q, 11);
    int rc;
    char *zFullPath;
    const char *zType = "MERGE";
    Blob m, p, r;
    /* Do a 3-way merge of idp->idm into idp->idv.  The results go into idv. */
    if( verboseFlag ){
      fossil_print("MERGE %s  (pivot=%d v1=%d v2=%d)\n",
                   zName, ridp, ridm, ridv);
    }else{
      fossil_print("MERGE %s\n", zName);
    }
    if( islinkv || islinkm ){
      fossil_print("***** Cannot merge symlink %s\n", zName);
      nConflict++;
      db_multi_exec(
        "INSERT INTO mergestat(op,fnp,ridp,fn,ridv,fnm,ridm,fnr,nc,msg)"
        "VALUES('ERROR',%Q,%d,%Q,%d,%Q,%d,%Q,1,'cannot merge symlink')",
        /* fnp  */ db_column_text(&q, 9),
        /* ridp */ ridp,
        /* fn   */ zName,
        /* ridv */ ridv,
        /* fnm  */ db_column_text(&q, 10),
        /* ridm */ ridm,
        /* fnr  */ zName
      ); 
    }else{
      i64 sz;
      const char *zErrMsg = 0;
      int nc = 0;

      if( useUndo ) undo_save(zName);
      zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
      sz = file_size(zFullPath, ExtFILE);
      content_get(ridp, &p);
      content_get(ridm, &m);
      if( isBinary ){
        rc = -1;
        blob_zero(&r);
      }else{
        unsigned mergeFlags = dryRunFlag ? MERGE_DRYRUN : 0;
        if(keepMergeFlag!=0) mergeFlags |= MERGE_KEEP_FILES;
        rc = merge_3way(&p, zFullPath, &m, &r, mergeFlags);
      }
      if( rc>=0 ){
        if( !dryRunFlag ){
          blob_write_to_file(&r, zFullPath);
          file_setexe(zFullPath, isExe);
        }
        db_multi_exec("UPDATE vfile SET mtime=0 WHERE id=%d", idv);
        if( rc>0 ){
          fossil_print("***** %d merge conflict%s in %s\n",
                       rc, rc>1 ? "s" : "", zName);
          nConflict++;
          nc = rc;
          zErrMsg = "merge conflicts";
          zType = "CONFLICT";
        }
      }else{
        fossil_print("***** Cannot merge binary file %s\n", zName);
        nConflict++;
        nc = 1;
        zErrMsg = "cannot merge binary file";
        zType = "ERROR";
      }
      db_multi_exec(
        "INSERT INTO mergestat(op,fnp,ridp,fn,ridv,sz,fnm,ridm,fnr,nc,msg)"
        "VALUES(%Q,%Q,%d,%Q,iif(%d,%d,NULL),iif(%d,%lld,NULL),%Q,%d,"
               "%Q,%d,%Q)",
        /* op   */ zType,
        /* fnp  */ db_column_text(&q, 9),
        /* ridp */ ridp,
        /* fn   */ zName,
        /* ridv */ chnged==0, ridv,
        /* sz   */ chnged!=0, sz,
        /* fnm  */ db_column_text(&q, 10),
        /* ridm */ ridm,
        /* fnr  */ zName,
        /* nc   */ nc,
        /* msg  */ zErrMsg
      );
      fossil_free(zFullPath);
      blob_reset(&p);
      blob_reset(&m);
      blob_reset(&r);
    }
    vmerge_insert(idv, ridm);
  }
  db_finalize(&q);

  /*
  ** Drop files that are in P and V but not in M
  */
  db_prepare(&q,
    "SELECT idv, fn, chnged, ridv FROM fv"
    " WHERE idp>0 AND idv>0 AND idm=0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    const char *zName = db_column_text(&q, 1);
    int chnged = db_column_int(&q, 2);
    int ridv = db_column_int(&q, 3);
    int sz = -1;
    const char *zErrMsg = 0;
    int nc = 0;
    /* Delete the file idv */
    fossil_print("DELETE %s\n", zName);
    if( chnged ){
      char *zFullPath;
      fossil_warning("WARNING: local edits lost for %s", zName);
      nConflict++;
      ridv = 0;
      nc = 1;
      zErrMsg = "local edits lost";
      zFullPath = mprintf("%s/%s", g.zLocalRoot, zName);
      sz = file_size(zFullPath, ExtFILE);
      fossil_free(zFullPath);
    }
    if( useUndo ) undo_save(zName);
    db_multi_exec(
      "UPDATE vfile SET deleted=1 WHERE id=%d", idv
    );
    if( !dryRunFlag ){
      char *zFullPath = mprintf("%s%s", g.zLocalRoot, zName);
      file_delete(zFullPath);
      free(zFullPath);
    }
    db_multi_exec(
      "INSERT INTO localdb.mergestat(op,fnp,ridp,fn,ridv,sz,fnm,ridm,nc,msg)"
      "VALUES('DELETE',NULL,NULL,%Q,iif(%d,%d,NULL),iif(%d,%d,NULL),"
             "NULL,NULL,%d,%Q)",
      /* fn   */ zName,
      /* ridv */ chnged==0, ridv,
      /* sz   */ chnged!=0, sz,
      /* nc   */ nc,
      /* msg  */ zErrMsg
    );
  }
  db_finalize(&q);

  /* For certain sets of renames (e.g. A -> B and B -> A), a file that is
  ** being renamed must first be moved to a temporary location to avoid
  ** being overwritten by another rename operation. A row is added to the
  ** TMPRN table for each of these temporary renames.
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS tmprn;"
    "CREATE TEMP TABLE tmprn(fn UNIQUE, tmpfn);"
  );

  /*
  ** Rename files that have taken a rename on P->M but which keep the same
  ** name on P->V.  If a file is renamed on P->V only or on both P->V and
  ** P->M then we retain the V name of the file.
  */
  db_prepare(&q,
    "SELECT idv, fnp, fnm, isexe FROM fv"
    " WHERE idv>0 AND idp>0 AND idm>0 AND fnp=fn AND fnm!=fnp"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idv = db_column_int(&q, 0);
    const char *zOldName = db_column_text(&q, 1);
    const char *zNewName = db_column_text(&q, 2);
    int isExe = db_column_int(&q, 3);
    fossil_print("RENAME %s -> %s\n", zOldName, zNewName);
    if( useUndo ) undo_save(zOldName);
    if( useUndo ) undo_save(zNewName);
    db_multi_exec(
      "UPDATE mergestat SET fnr=fnm WHERE fnp=%Q",
      zOldName
    );
    db_multi_exec(
      "UPDATE vfile SET pathname=NULL, origname=pathname"
      " WHERE vid=%d AND pathname=%Q;"
      "UPDATE vfile SET pathname=%Q, origname=coalesce(origname,pathname)"
      " WHERE id=%d;",
      vid, zNewName, zNewName, idv
    );
    if( !dryRunFlag ){
      char *zFullOldPath, *zFullNewPath;
      zFullOldPath = db_text(0,"SELECT tmpfn FROM tmprn WHERE fn=%Q", zOldName);
      if( !zFullOldPath ){
        zFullOldPath = mprintf("%s%s", g.zLocalRoot, zOldName);
      }
      zFullNewPath = mprintf("%s%s", g.zLocalRoot, zNewName);
      if( file_size(zFullNewPath, RepoFILE)>=0 ){
        Blob tmpPath;
        file_tempname(&tmpPath, "", 0);
        db_multi_exec("INSERT INTO tmprn(fn,tmpfn) VALUES(%Q,%Q)",
                      zNewName, blob_str(&tmpPath));
        if( file_islink(zFullNewPath) ){
          symlink_copy(zFullNewPath, blob_str(&tmpPath));
        }else{
          file_copy(zFullNewPath, blob_str(&tmpPath));
        }
        blob_reset(&tmpPath);
      }
      if( file_islink(zFullOldPath) ){
        symlink_copy(zFullOldPath, zFullNewPath);
      }else{
        file_copy(zFullOldPath, zFullNewPath);
      }
      file_setexe(zFullNewPath, isExe);
      file_delete(zFullOldPath);
      free(zFullNewPath);
      free(zFullOldPath);
    }
  }
  db_finalize(&q);

  /* A file that has been deleted and replaced by a renamed file will have a
  ** NULL pathname. Change it to something that makes the output of "status"
  ** and similar commands make sense for such files and that will (most likely)
  ** not be an actual existing pathname.
  */
  db_multi_exec(
    "UPDATE vfile SET pathname=origname || ' (overwritten by rename)'"
    " WHERE pathname IS NULL"
  );

  /*
  ** Insert into V any files that are not in V or P but are in M.
  */
  db_prepare(&q,
    "SELECT idm, fnm, ridm FROM fv"
    " WHERE idp=0 AND idv=0 AND idm>0"
  );
  while( db_step(&q)==SQLITE_ROW ){
    int idm = db_column_int(&q, 0);
    const char *zName;
    char *zFullName;
    db_multi_exec(
      "REPLACE INTO vfile(vid,chnged,deleted,rid,mrid,"
                         "isexe,islink,pathname,mhash)"
      "  SELECT %d,%d,0,rid,mrid,isexe,islink,pathname,"
              "CASE WHEN rid<>mrid"
              "     THEN (SELECT uuid FROM blob WHERE blob.rid=vfile.mrid) END "
              "FROM vfile WHERE id=%d",
      vid, integrateFlag?5:3, idm
    );
    zName = db_column_text(&q, 1);
    zFullName = mprintf("%s%s", g.zLocalRoot, zName);
    if( file_isfile_or_link(zFullName)
        && !db_exists("SELECT 1 FROM fv WHERE fn=%Q", zName) ){
      /* Name of backup file with Original content */
      char *zOrig = file_newname(zFullName, "original", 1);
      /* Backup previously unanaged file before to be overwritten */
      file_copy(zFullName, zOrig);
      fossil_free(zOrig);
      fossil_print("ADDED %s (overwrites an unmanaged file)", zName);
      if( !dryRunFlag ) fossil_print(", original copy backed up locally");
      fossil_print("\n");
      nOverwrite++;
    }else{
      fossil_print("ADDED %s\n", zName);
    }
    fossil_free(zFullName);
    db_multi_exec(
      "INSERT INTO mergestat(op,fnm,ridm,fnr)"
      "VALUES('ADDED',%Q,%d,%Q)",
      /* fnm  */ zName,
      /* ridm */ db_column_int(&q,2),
      /* fnr  */ zName
    );
    if( useUndo ) undo_save(zName);
    if( !dryRunFlag ){
      vfile_to_disk(0, idm, 0, 0);
    }
  }
  db_finalize(&q);

  /* Report on conflicts
  */
  if( nConflict ){
    fossil_warning("WARNING: %d merge conflicts", nConflict);
    if( bMultiMerge ){
      int i;
      Blob msg;
      blob_init(&msg, 0, 0);
      blob_appendf(&msg,
         "The following %ss were not attempted due to prior conflicts:",
         pickFlag ? "cherrypick" : backoutFlag ? "backout" : "merge"
      );
      for(i=2; i<g.argc; i++){
        blob_appendf(&msg, " %s", g.argv[i]);
      }
      fossil_warning("%s", blob_str(&msg));
      blob_zero(&msg);
    }
  }
  if( nOverwrite ){
    fossil_warning("WARNING: %d unmanaged files were overwritten",
                   nOverwrite);
  }
  if( dryRunFlag && !bMultiMerge ){
    fossil_warning("REMINDER: this was a dry run -"
                   " no files were actually changed.");
  }

  /*
  ** Clean up the mid and pid VFILE entries.  Then commit the changes.
  */
  db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
  if( pickFlag ){
    vmerge_insert(-1, mid);
    /* For a cherrypick merge, make the default check-in comment the same
    ** as the check-in comment on the check-in that is being merged in. */
    db_multi_exec(
       "REPLACE INTO vvar(name,value)"
       " SELECT 'ci-comment', coalesce(ecomment,comment) FROM event"
       "  WHERE type='ci' AND objid=%d",
       mid
    );
  }else if( backoutFlag ){
    vmerge_insert(-2, pid);
  }else if( integrateFlag ){
    vmerge_insert(-4, mid);
  }else{
    vmerge_insert(0, mid);
  }
  if( bMultiMerge && nConflict==0 ){
    nMerge++;
    goto merge_next_child;
  }
  if( useUndo ) undo_finish();

  db_end_transaction(dryRunFlag);
}
