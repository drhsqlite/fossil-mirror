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

#if INTERFACE
/*
** struct mark_t
**   holds information for translating between git commits
**   and fossil commits.
**   -git_name: This is the mark name that identifies the commit to git.
**              It will always begin with a ':'.
**   -rid: The unique object ID that identifies this commit within the
**         repository database.
**   -uuid: The SHA-1 of artifact corresponding to rid.
*/
struct mark_t{
  char *name;
  int rid;
  char uuid[41];
};
#endif

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
    for(j=0; zEmail[j] && zEmail[j]!='>'; j++){}
    if( zEmail[j]=='>' ) zEmail[j+1] = 0;
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
** insert_commit_xref()
**   Insert a new (mark,rid,uuid) entry into the 'xmark' table.
**   zName and zUuid must be non-null and must point to NULL-terminated strings.
*/
void insert_commit_xref(int rid, const char *zName, const char *zUuid){
  db_multi_exec(
    "INSERT OR IGNORE INTO xmark(tname, trid, tuuid)"
    "VALUES(%Q,%d,%Q)",
    zName, rid, zUuid
  );
}

/*
** create_mark()
**   Create a new (mark,rid,uuid) entry for the given rid in the 'xmark' table,
**   and return that information as a struct mark_t in *mark.
**   This function returns -1 in the case where 'rid' does not exist, otherwise
**   it returns 0.
**   mark->name is dynamically allocated and is owned by the caller upon return.
*/
int create_mark(int rid, struct mark_t *mark){
  char sid[13];
  char *zUuid = rid_to_uuid(rid);
  if(!zUuid){
    fossil_trace("Undefined rid=%d\n", rid);
    return -1;
  }
  mark->rid = rid;
  sqlite3_snprintf(sizeof(sid), sid, ":%d", COMMITMARK(rid));
  mark->name = fossil_strdup(sid);
  sqlite3_snprintf(sizeof(mark->uuid), mark->uuid, "%s", zUuid);
  free(zUuid);
  insert_commit_xref(mark->rid, mark->name, mark->uuid);
  return 0;
}

/*
** mark_name_from_rid()
**   Find the mark associated with the given rid.  Mark names always start
**   with ':', and are pulled from the 'xmark' temporary table.
**   This function returns NULL if the rid does not exist in the 'xmark' table.
**   Otherwise, it returns the name of the mark, which is dynamically allocated
**   and is owned by the caller of this function.
*/
char * mark_name_from_rid(int rid){
  char *zMark = db_text(0, "SELECT tname FROM xmark WHERE trid=%d", rid);
  if(zMark==NULL){
    struct mark_t mark;
    if(create_mark(rid, &mark)==0){
      zMark = mark.name;
    }else{
      return NULL;
    }
  }
  return zMark;
}

/*
** parse_mark()
**   Create a new (mark,rid,uuid) entry in the 'xmark' table given a line
**   from a marks file.  Return the cross-ref information as a struct mark_t
**   in *mark.
**   This function returns -1 in the case that the line is blank, malformed, or
**   the rid/uuid named in 'line' does not match what is in the repository
**   database.  Otherwise, 0 is returned.
**   mark->name is dynamically allocated, and owned by the caller.
*/
int parse_mark(char *line, struct mark_t *mark){
  char *cur_tok;
  cur_tok = strtok(line, " \t");
  if(!cur_tok||strlen(cur_tok)<2){
    return -1;
  }
  mark->rid = atoi(&cur_tok[1]);
  if(cur_tok[0]!='c'){
    /* This is probably a blob mark */
    mark->name = NULL;
    return 0;
  }

  cur_tok = strtok(NULL, " \t");
  if(!cur_tok){
    /* This mark was generated by an older version of Fossil and doesn't
    ** include the mark name and uuid.  create_mark() will name the new mark
    ** exactly as it was when exported to git, so that we should have a
    ** valid mapping from git sha1<->mark name<->fossil sha1. */
    return create_mark(mark->rid, mark);
  }else{
    mark->name = fossil_strdup(cur_tok);
  }

  cur_tok = strtok(NULL, "\n");
  if(!cur_tok||strlen(cur_tok)!=40){
    free(mark->name);
    fossil_trace("Invalid SHA-1 in marks file: %s\n", cur_tok);
    return -1;
  }else{
    sqlite3_snprintf(sizeof(mark->uuid), mark->uuid, "%s", cur_tok);
  }

  /* make sure that rid corresponds to UUID */
  if(fast_uuid_to_rid(mark->uuid)!=mark->rid){
    free(mark->name);
    fossil_trace("Non-existent SHA-1 in marks file: %s\n", mark->uuid);
    return -1;
  }

  /* insert a cross-ref into the 'xmark' table */
  insert_commit_xref(mark->rid, mark->name, mark->uuid);
  return 0;
}

/*
** import_marks()
**   Import the marks specified in file 'f' into the 'xmark' table.
**   If 'blobs' is non-null, insert all blob marks into it.
**   If 'vers' is non-null, insert all commit marks into it.
**   Each line in the file must be at most 100 characters in length.  This
**   seems like a reasonable maximum for a 40-character uuid, and 1-13
**   character rid.
**   The function returns -1 if any of the lines in file 'f' are malformed,
**   or the rid/uuid information doesn't match what is in the repository
**   database.  Otherwise, 0 is returned.
*/
int import_marks(FILE* f, Bag *blobs, Bag *vers){
  char line[101];
  while(fgets(line, sizeof(line), f)){
    struct mark_t mark;
    if(strlen(line)==100&&line[99]!='\n'){
      /* line too long */
      return -1;
    }
    if( parse_mark(line, &mark)<0 ){
      return -1;
    }else if( line[0]=='b' ){
      /* Don't import blob marks into 'xmark' table--git doesn't use them,
      ** so they need to be left free for git to reuse. */
      if(blobs!=NULL){
        bag_insert(blobs, mark.rid);
      }
    }else if( vers!=NULL ){
      bag_insert(vers, mark.rid);
    }
    free(mark.name);
  }
  return 0;
}

/*
**  If 'blobs' is non-null, it must point to a Bag of blob rids to be
**  written to disk.  Blob rids are written as 'b<rid>'.
**  If 'vers' is non-null, it must point to a Bag of commit rids to be
**  written to disk.  Commit rids are written as 'c<rid> :<mark> <uuid>'.
**  All commit (mark,rid,uuid) tuples are stored in 'xmark' table.
**  This function does not fail, but may produce errors if a uuid cannot
**  be found for an rid in 'vers'.
*/
void export_marks(FILE* f, Bag *blobs, Bag *vers){
  int rid;
  if( blobs!=NULL ){
    rid = bag_first(blobs);
    if(rid!=0){
      do{
        fprintf(f, "b%d\n", rid);
      }while((rid = bag_next(blobs, rid))!=0);
    }
  }
  if( vers!=NULL ){
    rid = bag_first(vers);
    if( rid!=0 ){
      do{
        char *zUuid = rid_to_uuid(rid);
        char *zMark;
        if(zUuid==NULL){
          fossil_trace("No uuid matching rid=%d when exporting marks\n", rid);
          continue;
        }
        zMark = mark_name_from_rid(rid);
        fprintf(f, "c%d %s %s\n", rid, zMark, zUuid);
        free(zMark);
        free(zUuid);
      }while( (rid = bag_next(vers, rid))!=0 );
    }
  }
}

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
** or wiki or tech notes or attachments, so none of those are exported.
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
  db_multi_exec("CREATE TEMP TABLE xmark(tname TEXT UNIQUE, trid INT, tuuid TEXT)");
  if( markfile_in!=0 ){
    Stmt qb,qc;
    FILE *f;
    int rid;

    f = fossil_fopen(markfile_in, "r");
    if( f==0 ){
      fossil_fatal("cannot open %s for reading", markfile_in);
    }
    if(import_marks(f, &blobs, &vers)<0){
      fossil_fatal("error importing marks from file: %s", markfile_in);
    }
    db_prepare(&qb, "INSERT OR IGNORE INTO oldblob VALUES (:rid)");
    db_prepare(&qc, "INSERT OR IGNORE INTO oldcommit VALUES (:rid)");
    rid = bag_first(&blobs);
    if(rid!=0){
      do{
        db_bind_int(&qb, ":rid", rid);
        db_step(&qb);
        db_reset(&qb);
      }while((rid = bag_next(&blobs, rid))!=0);
    }
    rid = bag_first(&vers);
    if(rid!=0){
      do{
        db_bind_int(&qc, ":rid", rid);
        db_step(&qc);
        db_reset(&qc);
      }while((rid = bag_next(&vers, rid))!=0);
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
    "SELECT strftime('%%s',mtime), objid, coalesce(ecomment,comment),"
    "       coalesce(euser,user),"
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
    char *zMark;

    bag_insert(&vers, ckinId);
    db_bind_int(&q2, ":rid", ckinId);
    db_step(&q2);
    db_reset(&q2);
    if( zBranch==0 ) zBranch = "trunk";
    zBr = mprintf("%s", zBranch);
    for(i=0; zBr[i]; i++){
      if( !fossil_isalnum(zBr[i]) ) zBr[i] = '_';
    }
    zMark = mark_name_from_rid(ckinId);
    printf("commit refs/heads/%s\nmark %s\n", zBr, zMark);
    free(zMark);
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
      int pid = db_column_int(&q3, 0);
      zMark = mark_name_from_rid(pid);
      printf("from %s\n", zMark);
      free(zMark);
      db_prepare(&q4,
        "SELECT pid FROM plink"
        " WHERE cid=%d AND NOT isprim"
        "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=pid)"
        " ORDER BY pid",
        ckinId);
      while( db_step(&q4)==SQLITE_ROW ){
        zMark = mark_name_from_rid(db_column_int(&q4, 0));
        printf("merge %s\n", zMark);
        free(zMark);
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

  if( markfile_out!=0 ){
    FILE *f;
    f = fossil_fopen(markfile_out, "w");
    if( f == 0 ){
      fossil_fatal("cannot open %s for writing", markfile_out);
    }
    export_marks(f, &blobs, &vers);
    if( ferror(f)!=0 || fclose(f)!=0 ) {
      fossil_fatal("error while writing %s", markfile_out);
    }
  }
  bag_clear(&blobs);
  bag_clear(&vers);
}
