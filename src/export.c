/*
** Copyright (c) 2010 D. Richard Hipp
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
**
*******************************************************************************
**
** This file contains code used to export the content of a Fossil
** repository in the git-fast-import format.
*/
#include "config.h"
#include "export.h"
#include <assert.h>

/*
** Output a "committer" record for the given user.
*/
static void print_person(const char *zUser){
  static Stmt q;
  const char *zContact;
  char *zName;
  char *zEmail;
  int i, j;

  if( zUser==0 ){
    printf(" <unknown>");
    return;
  }
  db_static_prepare(&q, "SELECT info FROM user WHERE login=:user");
  db_bind_text(&q, ":user", zUser);
  if( db_step(&q)!=SQLITE_ROW ){
    db_reset(&q);
    for(i=0; zUser[i] && zUser[i]!='>' && zUser[i]!='<'; i++){}
    if( zUser[i]==0 ){
      printf(" %s <%s>", zUser, zUser);
      return;
    }
    zName = mprintf("%s", zUser);
    for(i=j=0; zName[i]; i++){
      if( zName[i]!='<' && zName[i]!='>' ){
        zName[j++] = zName[i];
      }
    }
    zName[j] = 0;
    printf(" %s <%s>", zName, zUser);
    free(zName);
    return;
  }
  /*
  ** We have contact information.
  ** It may or may not contain an email address.
   */
  zContact = db_column_text(&q, 0);
  for(i=0; zContact[i] && zContact[i]!='>' && zContact[i]!='<'; i++){}
  if( zContact[i]==0 ){
    /* No email address found. Take as user info if not empty */
    printf(" %s <%s>", zContact[0] ? zContact : zUser, zUser);
    db_reset(&q);
    return;
  }
  if( zContact[i]=='<' ){
    /*
    ** Found beginning of email address. Look for the end and extract
    ** the part.
     */
    zEmail = mprintf("%s", &zContact[i]);
    for(i=0; zEmail[i] && zEmail[i]!='>'; i++){}
    if( zEmail[i]=='>' ) zEmail[i+1] = 0;
  }else{
    /*
    ** Found an end marker for email, but nothing else.
     */
    zEmail = mprintf("<%s>", zUser);
  }
  /*
  ** Here zContact[i] either '<' or '>'. Extract the string _before_
  ** either as user name.
  */
  zName = mprintf("%.*s", i-1, zContact);
  for(i=j=0; zName[i]; i++){
    if( zName[i]!='"' ) zName[j++] = zName[i];
  }
  zName[j] = 0;
  printf(" %s %s", zName, zEmail);
  free(zName);
  free(zEmail);
  db_reset(&q);
}

#define BLOBMARK(rid)   ((rid) * 2)
#define COMMITMARK(rid) ((rid) * 2 + 1)

/*
** COMMAND: export
**
** Usage: %fossil export --git ?OPTIONS? ?REPOSITORY?
**
** Write an export of all check-ins to standard output.  The export is
** written in the git-fast-export file format assuming the --git option is
** provided.  The git-fast-export format is currently the only VCS 
** interchange format supported, though other formats may be added in
** the future.
**
** Run this command within a checkout.  Or use the -R or --repository
** option to specify a Fossil repository to be exported.
**
** Only check-ins are exported using --git.  Git does not support tickets 
** or wiki or events or attachments, so none of those are exported.
**
** If the "--import-marks FILE" option is used, it contains a list of
** rids to skip.
**
** If the "--export-marks FILE" option is used, the rid of all commits and
** blobs written on exit for use with "--import-marks" on the next run.
**
** Options:
**   --export-marks FILE          export rids of exported data to FILE
**   --import-marks FILE          read rids of data to ignore from FILE
**   --repository|-R REPOSITORY   export the given REPOSITORY
**   
** See also: import
*/
void export_cmd(void){
  Stmt q, q2, q3;
  int i;
  Bag blobs, vers;
  const char *markfile_in;
  const char *markfile_out;

  bag_init(&blobs);
  bag_init(&vers);

  find_option("git", 0, 0);   /* Ignore the --git option for now */
  markfile_in = find_option("import-marks", 0, 1);
  markfile_out = find_option("export-marks", 0, 1);

  db_find_and_open_repository(0, 2);
  verify_all_options();
  if( g.argc!=2 && g.argc!=3 ){ usage("--git ?REPOSITORY?"); }

  db_multi_exec("CREATE TEMPORARY TABLE oldblob(rid INTEGER PRIMARY KEY)");
  db_multi_exec("CREATE TEMPORARY TABLE oldcommit(rid INTEGER PRIMARY KEY)");
  if( markfile_in!=0 ){
    Stmt qb,qc;
    char line[100];
    FILE *f;

    f = fossil_fopen(markfile_in, "r");
    if( f==0 ){
      fossil_fatal("cannot open %s for reading", markfile_in);
    }
    db_prepare(&qb, "INSERT OR IGNORE INTO oldblob VALUES (:rid)");
    db_prepare(&qc, "INSERT OR IGNORE INTO oldcommit VALUES (:rid)");
    while( fgets(line, sizeof(line), f)!=0 ){
      if( *line == 'b' ){
        db_bind_text(&qb, ":rid", line + 1);
        db_step(&qb);
        db_reset(&qb);
        bag_insert(&blobs, atoi(line + 1));
      }else if( *line == 'c' ){
        db_bind_text(&qc, ":rid", line + 1);
        db_step(&qc);
        db_reset(&qc);
        bag_insert(&vers, atoi(line + 1));
      }else{
        fossil_fatal("bad input from %s: %s", markfile_in, line);
      }
    }
    db_finalize(&qb);
    db_finalize(&qc);
    fclose(f);
  }

  /* Step 1:  Generate "blob" records for every artifact that is part
  ** of a check-in 
  */
  fossil_binary_mode(stdout);
  db_multi_exec("CREATE TEMP TABLE newblob(rid INTEGER KEY, srcid INTEGER)");
  db_multi_exec("CREATE INDEX newblob_src ON newblob(srcid)");
  db_multi_exec(
    "INSERT INTO newblob"
    " SELECT DISTINCT fid,"
    "  CASE WHEN EXISTS(SELECT 1 FROM delta"
                       " WHERE rid=fid"
                       "   AND NOT EXISTS(SELECT 1 FROM oldblob"
                                         " WHERE srcid=fid))"
    "   THEN (SELECT srcid FROM delta WHERE rid=fid)"
    "   ELSE 0"
    "  END"
    " FROM mlink"
    " WHERE fid>0 AND NOT EXISTS(SELECT 1 FROM oldblob WHERE rid=fid)");
  db_prepare(&q,
    "SELECT DISTINCT fid FROM mlink"
    " WHERE fid>0 AND NOT EXISTS(SELECT 1 FROM oldblob WHERE rid=fid)");
  db_prepare(&q2, "INSERT INTO oldblob VALUES (:rid)");
  db_prepare(&q3, "SELECT rid FROM newblob WHERE srcid= (:srcid)");
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    Blob content;

    while( !bag_find(&blobs, rid) ){
      content_get(rid, &content);
      db_bind_int(&q2, ":rid", rid);
      db_step(&q2);
      db_reset(&q2);
      printf("blob\nmark :%d\ndata %d\n", BLOBMARK(rid), blob_size(&content));
      bag_insert(&blobs, rid);
      fwrite(blob_buffer(&content), 1, blob_size(&content), stdout);
      printf("\n");
      blob_reset(&content);

      db_bind_int(&q3, ":srcid", rid);
      if( db_step(&q3) != SQLITE_ROW ){
        db_reset(&q3);
        break;
      }
      rid = db_column_int(&q3, 0);
      db_reset(&q3);
    }
  }
  db_finalize(&q);
  db_finalize(&q2);
  db_finalize(&q3);

  /* Output the commit records.
  */
  db_prepare(&q,
    "SELECT strftime('%%s',mtime), objid, coalesce(comment,ecomment),"
    "       coalesce(user,euser),"
    "       (SELECT value FROM tagxref WHERE rid=objid AND tagid=%d)"
    "  FROM event"
    " WHERE type='ci' AND NOT EXISTS (SELECT 1 FROM oldcommit WHERE objid=rid)"
    " ORDER BY mtime ASC",
    TAG_BRANCH
  );
  db_prepare(&q2, "INSERT INTO oldcommit VALUES (:rid)");
  while( db_step(&q)==SQLITE_ROW ){
    Stmt q4;
    const char *zSecondsSince1970 = db_column_text(&q, 0);
    int ckinId = db_column_int(&q, 1);
    const char *zComment = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    const char *zBranch = db_column_text(&q, 4);
    char *zBr;

    bag_insert(&vers, ckinId);
    db_bind_int(&q2, ":rid", ckinId);
    db_step(&q2);
    db_reset(&q2);
    if( zBranch==0 ) zBranch = "trunk";
    zBr = mprintf("%s", zBranch);
    for(i=0; zBr[i]; i++){
      if( !fossil_isalnum(zBr[i]) ) zBr[i] = '_';
    }
    printf("commit refs/heads/%s\nmark :%d\n", zBr, COMMITMARK(ckinId));
    free(zBr);
    printf("committer");
    print_person(zUser);
    printf(" %s +0000\n", zSecondsSince1970);
    if( zComment==0 ) zComment = "null comment";
    printf("data %d\n%s\n", (int)strlen(zComment), zComment);
    db_prepare(&q3,
      "SELECT pid FROM plink"
      " WHERE cid=%d AND isprim"
      "   AND pid IN (SELECT objid FROM event)",
      ckinId
    );
    if( db_step(&q3) == SQLITE_ROW ){
      printf("from :%d\n", COMMITMARK(db_column_int(&q3, 0)));
      db_prepare(&q4,
        "SELECT pid FROM plink"
        " WHERE cid=%d AND NOT isprim"
        "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=pid)"
        " ORDER BY pid",
        ckinId);
      while( db_step(&q4)==SQLITE_ROW ){
        printf("merge :%d\n", COMMITMARK(db_column_int(&q4,0)));
      }
      db_finalize(&q4);
    }else{
      printf("deleteall\n");
    }

    db_prepare(&q4,
      "SELECT filename.name, mlink.fid, mlink.mperm FROM mlink"
      " JOIN filename ON filename.fnid=mlink.fnid"
      " WHERE mlink.mid=%d",
      ckinId
    );
    while( db_step(&q4)==SQLITE_ROW ){
      const char *zName = db_column_text(&q4,0);
      int zNew = db_column_int(&q4,1);
      int mPerm = db_column_int(&q4,2);
      if( zNew==0)
        printf("D %s\n", zName);
      else if( bag_find(&blobs, zNew) ) {
        const char *zPerm;
        switch( mPerm ){
          case PERM_LNK:  zPerm = "120000";   break;
          case PERM_EXE:  zPerm = "100755";   break;
          default:        zPerm = "100644";   break;
        }
        printf("M %s :%d %s\n", zPerm, BLOBMARK(zNew), zName);
      }
    }
    db_finalize(&q4);
    db_finalize(&q3);
    printf("\n");
  }
  db_finalize(&q2);
  db_finalize(&q);
  bag_clear(&blobs);
  manifest_cache_clear();


  /* Output tags */
  db_prepare(&q,
     "SELECT tagname, rid, strftime('%%s',mtime)"
     "  FROM tagxref JOIN tag USING(tagid)"
     " WHERE tagtype=1 AND tagname GLOB 'sym-*'"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTagname = db_column_text(&q, 0);
    char *zEncoded = 0;
    int rid = db_column_int(&q, 1);
    const char *zSecSince1970 = db_column_text(&q, 2);
    int i;
    if( rid==0 || !bag_find(&vers, rid) ) continue;
    zTagname += 4;
    zEncoded = mprintf("%s", zTagname);
    for(i=0; zEncoded[i]; i++){
      if( !fossil_isalnum(zEncoded[i]) ) zEncoded[i] = '_';
    }
    printf("tag %s\n", zEncoded);
    printf("from :%d\n", COMMITMARK(rid));
    printf("tagger <tagger> %s +0000\n", zSecSince1970);
    printf("data 0\n");
    fossil_free(zEncoded);
  }
  db_finalize(&q);
  bag_clear(&vers);

  if( markfile_out!=0 ){
    FILE *f;
    f = fossil_fopen(markfile_out, "w");
    if( f == 0 ){
      fossil_fatal("cannot open %s for writing", markfile_out);
    }
    db_prepare(&q, "SELECT rid FROM oldblob");
    while( db_step(&q)==SQLITE_ROW ){
      fprintf(f, "b%d\n", db_column_int(&q, 0));
    }
    db_finalize(&q);
    db_prepare(&q, "SELECT rid FROM oldcommit");
    while( db_step(&q)==SQLITE_ROW ){
      fprintf(f, "c%d\n", db_column_int(&q, 0));
    }
    db_finalize(&q);
    if( ferror(f)!=0 || fclose(f)!=0 ) {
      fossil_fatal("error while writing %s", markfile_out);
    }
  }
}
