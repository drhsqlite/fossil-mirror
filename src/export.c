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
** State information common to all export types.
*/
static struct {
  const char *zTrunkName;     /* Name of trunk branch */
} gexport;

#if INTERFACE
/*
** Each line in a git-fast-export "marK" file is an instance of
** this object.
*/
struct mark_t {
  char *name;       /* Name of the mark.  Also starts with ":" */
  int rid;          /* Corresponding object in the BLOB table */
  char uuid[65];    /* The GIT hash name for this object */
};
#endif

/*
** Output a "committer" record for the given user.
** NOTE: the given user name may be an email itself.
*/
static void print_person(const char *zUser){
  static Stmt q;
  const char *zContact;
  char *zName;
  char *zEmail;
  int i, j;
  int isBracketed, atEmailFirst, atEmailLast;

  if( zUser==0 ){
    printf(" <unknown>");
    return;
  }
  db_static_prepare(&q, "SELECT info FROM user WHERE login=:user");
  db_bind_text(&q, ":user", zUser);
  if( db_step(&q)!=SQLITE_ROW ){
    db_reset(&q);
    zName = fossil_strdup(zUser);
    for(i=j=0; zName[i]; i++){
      if( zName[i]!='<' && zName[i]!='>' && zName[i]!='"' ){
        zName[j++] = zName[i];
      }
    }
    zName[j] = 0;
    printf(" %s <%s>", zName, zName);
    free(zName);
    return;
  }

  /*
  ** We have contact information.
  ** It may or may not contain an email address.
  **
  ** ASSUME:
  ** - General case:"Name Unicoded" <email@address.com> other info
  ** - If contact information contains more than an email address,
  **   then the email address is enclosed between <>
  ** - When only email address is specified, then it's stored verbatim
  ** - When name part is absent or all-blanks, use zUser instead
   */
  zName = NULL;
  zEmail = NULL;
  zContact = db_column_text(&q, 0);
  atEmailFirst = -1;
  atEmailLast = -1;
  isBracketed = 0;
  for(i=0; zContact[i] && zContact[i]!='@'; i++){
     if( zContact[i]=='<' ){
        isBracketed = 1;
        atEmailFirst = i+1;
     }
     else if( zContact[i]=='>' ){
        isBracketed = 0;
        atEmailFirst = i+1;
     }
     else if( zContact[i]==' ' && !isBracketed ){
        atEmailFirst = i+1;
     }
  }
  if( zContact[i]==0 ){
    /* No email address found. Take as user info if not empty */
    zName = fossil_strdup(zContact[0] ? zContact : zUser);
    for(i=j=0; zName[i]; i++){
      if( zName[i]!='<' && zName[i]!='>' && zName[i]!='"' ){
        zName[j++] = zName[i];
      }
    }
    zName[j] = 0;

    printf(" %s <%s>",  zName, zName);
    free(zName);
    db_reset(&q);
    return;
  }
  for(j=i+1; zContact[j] && zContact[j]!=' '; j++){
     if( zContact[j]=='>' )
        atEmailLast = j-1;
  }
  if ( atEmailLast==-1 ) atEmailLast = j-1;
  if ( atEmailFirst==-1 ) atEmailFirst = 0; /* Found only email */

  /*
  ** Found beginning and end of email address.
  ** Extract the address (trimmed and sanitized).
  */
  for(j=atEmailFirst; zContact[j] && zContact[j]==' '; j++){}
  zEmail = mprintf("%.*s", atEmailLast-j+1, &zContact[j]);

  for(i=j=0; zEmail[i]; i++){
     if( zEmail[i]!='<' && zEmail[i]!='>' ){
         zEmail[j++] = zEmail[i];
     }
  }
  zEmail[j] = 0;

  /*
  ** When bracketed email, extract the string _before_
  ** email as user name (may be enquoted).
  ** If missing or all-blank name, use zUser.
  */
  if( isBracketed && (atEmailFirst-1) > 0){
     for(i=atEmailFirst-2; i>=0 && zContact[i] && zContact[i]==' '; i--){}
     if( i>=0 ){
         for(j=0; j<i && zContact[j] && zContact[j]==' '; j++){}
         zName = mprintf("%.*s", i-j+1, &zContact[j]);
     }
  }

  if( zName==NULL ) zName = fossil_strdup(zUser);
  for(i=j=0; zName[i]; i++){
     if( zName[i]!='<' && zName[i]!='>' && zName[i]!='"' ){
         zName[j++] = zName[i];
     }
  }
  zName[j] = 0;

  printf(" %s <%s>", zName, zEmail);
  free(zName);
  free(zEmail);
  db_reset(&q);
}

#define REFREPLACEMENT        '_'

/*
** Output a sanitized git named reference.
** https://git-scm.com/docs/git-check-ref-format
** This implementation assumes we are only printing
** the branch or tag part of the reference.
*/
static void print_ref(const char *zRef){
  char *zEncoded = fossil_strdup(zRef);
  int i, w;
  if (zEncoded[0]=='@' && zEncoded[1]=='\0'){
    putchar(REFREPLACEMENT);
    return;
  }
  for(i=0, w=0; zEncoded[i]; i++, w++){
    if( i!=0 ){ /* Two letter tests */
      if( (zEncoded[i-1]=='.' && zEncoded[i]=='.') ||
          (zEncoded[i-1]=='@' && zEncoded[i]=='{') ){
        zEncoded[w]=zEncoded[w-1]=REFREPLACEMENT;
        continue;
      }
      if( zEncoded[i-1]=='/' && zEncoded[i]=='/' ){
        w--; /* Normalise to a single / by rolling back w */
        continue;
      }
    }
    /* No control characters */
    if( (unsigned)zEncoded[i]<0x20 || zEncoded[i]==0x7f ){
      zEncoded[w]=REFREPLACEMENT;
      continue;
    }
    switch( zEncoded[i] ){
      case ' ':
      case '^':
      case ':':
      case '?':
      case '*':
      case '[':
      case '\\':
        zEncoded[w]=REFREPLACEMENT;
        break;
    }
  }
  /* Cannot begin with a . or / */
  if( zEncoded[0]=='.' || zEncoded[0] == '/' ) zEncoded[0]=REFREPLACEMENT;
  if( i>0 ){
    i--; w--;
    /* Or end with a . or / */
    if( zEncoded[i]=='.' || zEncoded[i] == '/' ) zEncoded[w]=REFREPLACEMENT;
    /* Cannot end with .lock */
    if ( i>4 && strcmp((zEncoded+i)-5, ".lock")==0 )
      memset((zEncoded+w)-5, REFREPLACEMENT, 5);
  }
  printf("%s", zEncoded);
  free(zEncoded);
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
**   *unused_mark is a value representing a mark that is free for use--that is,
**   it does not appear in the marks file, and has not been used during this
**   export run.  Specifically, it is the supremum of the set of used marks
**   plus one.
**   This function returns -1 in the case where 'rid' does not exist, otherwise
**   it returns 0.
**   mark->name is dynamically allocated and is owned by the caller upon return.
*/
int create_mark(int rid, struct mark_t *mark, unsigned int *unused_mark){
  char sid[13];
  char *zUuid = rid_to_uuid(rid);
  if( !zUuid ){
    fossil_trace("Undefined rid=%d\n", rid);
    return -1;
  }
  mark->rid = rid;
  sqlite3_snprintf(sizeof(sid), sid, ":%d", *unused_mark);
  *unused_mark += 1;
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
**   If the given rid doesn't have a mark associated with it yet, one is
**   created with a value of *unused_mark.
**   *unused_mark functions exactly as in create_mark().
**   This function returns NULL if the rid does not have an associated UUID,
**   (i.e. is not valid).  Otherwise, it returns the name of the mark, which is
**   dynamically allocated and is owned by the caller of this function.
*/
char * mark_name_from_rid(int rid, unsigned int *unused_mark){
  char *zMark = db_text(0, "SELECT tname FROM xmark WHERE trid=%d", rid);
  if( zMark==NULL ){
    struct mark_t mark;
    if( create_mark(rid, &mark, unused_mark)==0 ){
      zMark = mark.name;
    }else{
      return NULL;
    }
  }
  return zMark;
}

/*
** Parse a single line of the mark file.  Store the result in the mark object.
**
** "line" is a single line of input.
** This function returns -1 in the case that the line is blank, malformed, or
** the rid/uuid named in 'line' does not match what is in the repository
** database.  Otherwise, 0 is returned.
**
** mark->name is dynamically allocated, and owned by the caller.
*/
int parse_mark(char *line, struct mark_t *mark){
  char *cur_tok;
  char type_;
  cur_tok = strtok(line, " \t");
  if( !cur_tok || strlen(cur_tok)<2 ){
    return -1;
  }
  mark->rid = atoi(&cur_tok[1]);
  type_ = cur_tok[0];
  if( type_!='c' && type_!='b' ){
    /* This is probably a blob mark */
    mark->name = NULL;
    return 0;
  }

  cur_tok = strtok(NULL, " \t");
  if( !cur_tok ){
    /* This mark was generated by an older version of Fossil and doesn't
    ** include the mark name and uuid.  create_mark() will name the new mark
    ** exactly as it was when exported to git, so that we should have a
    ** valid mapping from git hash<->mark name<->fossil hash. */
    unsigned int mid;
    if( type_=='c' ){
      mid = COMMITMARK(mark->rid);
    }
    else{
      mid = BLOBMARK(mark->rid);
    }
    return create_mark(mark->rid, mark, &mid);
  }else{
    mark->name = fossil_strdup(cur_tok);
  }

  cur_tok = strtok(NULL, "\n");
  if( !cur_tok || (strlen(cur_tok)!=40 && strlen(cur_tok)!=64) ){
    free(mark->name);
    fossil_trace("Invalid SHA-1/SHA-3 in marks file: %s\n", cur_tok);
    return -1;
  }else{
    sqlite3_snprintf(sizeof(mark->uuid), mark->uuid, "%s", cur_tok);
  }

  /* make sure that rid corresponds to UUID */
  if( fast_uuid_to_rid(mark->uuid)!=mark->rid ){
    free(mark->name);
    fossil_trace("Non-existent SHA-1/SHA-3 in marks file: %s\n", mark->uuid);
    return -1;
  }

  /* insert a cross-ref into the 'xmark' table */
  insert_commit_xref(mark->rid, mark->name, mark->uuid);
  return 0;
}

/*
** Import the marks specified in file 'f';
** If 'blobs' is non-null, insert all blob marks into it.
** If 'vers' is non-null, insert all commit marks into it.
** If 'unused_marks' is non-null, upon return of this function, all values
** x >= *unused_marks are free to use as marks, i.e. they do not clash with
** any marks appearing in the marks file.
**
** Each line in the file must be at most 100 characters in length.  This
** seems like a reasonable maximum for a 40-character uuid, and 1-13
** character rid.
**
** The function returns -1 if any of the lines in file 'f' are malformed,
** or the rid/uuid information doesn't match what is in the repository
** database.  Otherwise, 0 is returned.
*/
int import_marks(FILE* f, Bag *blobs, Bag *vers, unsigned int *unused_mark){
  char line[101];
  while(fgets(line, sizeof(line), f)){
    struct mark_t mark;
    if( strlen(line)==100 && line[99]!='\n' ){
      /* line too long */
      return -1;
    }
    if( parse_mark(line, &mark)<0 ){
      return -1;
    }else if( line[0]=='b' ){
      if( blobs!=NULL ){
        bag_insert(blobs, mark.rid);
      }
    }else{
      if( vers!=NULL ){
        bag_insert(vers, mark.rid);
      }
    }
    if( unused_mark!=NULL ){
      unsigned int mid = atoi(mark.name + 1);
      if( mid>=*unused_mark ){
        *unused_mark = mid + 1;
      }
    }
    free(mark.name);
  }
  return 0;
}

void export_mark(FILE* f, int rid, char obj_type)
{
  unsigned int z = 0;
  char *zUuid = rid_to_uuid(rid);
  char *zMark;
  if( zUuid==NULL ){
    fossil_trace("No uuid matching rid=%d when exporting marks\n", rid);
    return;
  }
  /* Since rid is already in the 'xmark' table, the value of z won't be
  ** used, but pass in a valid pointer just to be safe. */
  zMark = mark_name_from_rid(rid, &z);
  fprintf(f, "%c%d %s %s\n", obj_type, rid, zMark, zUuid);
  free(zMark);
  free(zUuid);
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
    if( rid!=0 ){
      do{
        export_mark(f, rid, 'b');
      }while( (rid = bag_next(blobs, rid))!=0 );
    }
  }
  if( vers!=NULL ){
    rid = bag_first(vers);
    if( rid!=0 ){
      do{
        export_mark(f, rid, 'c');
      }while( (rid = bag_next(vers, rid))!=0 );
    }
  }
}

/* This is the original header command (and hence documentation) for
** the "fossil export" command:
**
** Usage: %fossil export --git ?OPTIONS? ?REPOSITORY?
**
** Write an export of all check-ins to standard output.  The export is
** written in the git-fast-export file format assuming the --git option is
** provided.  The git-fast-export format is currently the only VCS
** interchange format supported, though other formats may be added in
** the future.
**
** Run this command within a check-out.  Or use the -R or --repository
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
**   --export-marks FILE          Export rids of exported data to FILE
**   --import-marks FILE          Read rids of data to ignore from FILE
**   --rename-trunk NAME          Use NAME as name of exported trunk branch
**   -R|--repository REPO         Export the given REPOSITORY
**
** See also: import
*/
/*
** COMMAND: export*
**
** Usage: %fossil export --git [REPOSITORY]
**
** This command is deprecated.  Use "fossil git export" instead.
*/
void export_cmd(void){
  Stmt q, q2, q3;
  Bag blobs, vers;
  unsigned int unused_mark = 1;
  const char *markfile_in;
  const char *markfile_out;

  bag_init(&blobs);
  bag_init(&vers);

  find_option("git", 0, 0);   /* Ignore the --git option for now */
  markfile_in = find_option("import-marks", 0, 1);
  markfile_out = find_option("export-marks", 0, 1);

  if( !(gexport.zTrunkName = find_option("rename-trunk", 0, 1)) ){
    gexport.zTrunkName = "trunk";
  }

  db_find_and_open_repository(0, 2);
  verify_all_options();
  if( g.argc!=2 && g.argc!=3 ){ usage("--git ?REPOSITORY?"); }

  db_multi_exec("CREATE TEMPORARY TABLE oldblob(rid INTEGER PRIMARY KEY)");
  db_multi_exec("CREATE TEMPORARY TABLE oldcommit(rid INTEGER PRIMARY KEY)");
  db_multi_exec("CREATE TEMP TABLE xmark(tname TEXT UNIQUE, trid INT,"
                " tuuid TEXT)");
  db_multi_exec("CREATE INDEX xmark_trid ON xmark(trid)");
  if( markfile_in!=0 ){
    Stmt qb,qc;
    FILE *f;
    int rid;

    f = fossil_fopen(markfile_in, "r");
    if( f==0 ){
      fossil_fatal("cannot open %s for reading", markfile_in);
    }
    if( import_marks(f, &blobs, &vers, &unused_mark)<0 ){
      fossil_fatal("error importing marks from file: %s", markfile_in);
    }
    db_prepare(&qb, "INSERT OR IGNORE INTO oldblob VALUES (:rid)");
    db_prepare(&qc, "INSERT OR IGNORE INTO oldcommit VALUES (:rid)");
    rid = bag_first(&blobs);
    if( rid!=0 ){
      do{
        db_bind_int(&qb, ":rid", rid);
        db_step(&qb);
        db_reset(&qb);
      }while((rid = bag_next(&blobs, rid))!=0);
    }
    rid = bag_first(&vers);
    if( rid!=0 ){
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
      char *zMark;
      content_get(rid, &content);
      db_bind_int(&q2, ":rid", rid);
      db_step(&q2);
      db_reset(&q2);
      zMark = mark_name_from_rid(rid, &unused_mark);
      printf("blob\nmark %s\ndata %d\n", zMark, blob_size(&content));
      free(zMark);
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
  topological_sort_checkins(0);
  db_prepare(&q,
    "SELECT strftime('%%s',mtime), objid, coalesce(ecomment,comment),"
    "       coalesce(euser,user),"
    "       (SELECT value FROM tagxref WHERE rid=objid AND tagid=%d)"
    "  FROM toponode, event"
    " WHERE toponode.tid=event.objid"
    "   AND event.type='ci'"
    "   AND NOT EXISTS (SELECT 1 FROM oldcommit WHERE toponode.tid=rid)"
    " ORDER BY toponode.tseq ASC",
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
    char *zMark;

    bag_insert(&vers, ckinId);
    db_bind_int(&q2, ":rid", ckinId);
    db_step(&q2);
    db_reset(&q2);
    if( zBranch==0 || fossil_strcmp(zBranch, "trunk")==0 ){
      zBranch = gexport.zTrunkName;
    }
    zMark = mark_name_from_rid(ckinId, &unused_mark);
    printf("commit refs/heads/");
    print_ref(zBranch);
    printf("\nmark %s\n", zMark);
    free(zMark);
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
      zMark = mark_name_from_rid(pid, &unused_mark);
      printf("from %s\n", zMark);
      free(zMark);
      db_prepare(&q4,
        "SELECT pid FROM plink"
        " WHERE cid=%d AND NOT isprim"
        "   AND NOT EXISTS(SELECT 1 FROM phantom WHERE rid=pid)"
        " ORDER BY pid",
        ckinId);
      while( db_step(&q4)==SQLITE_ROW ){
        zMark = mark_name_from_rid(db_column_int(&q4, 0), &unused_mark);
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
      if( zNew==0 ){
        printf("D %s\n", zName);
      }else if( bag_find(&blobs, zNew) ){
        const char *zPerm;
        zMark = mark_name_from_rid(zNew, &unused_mark);
        switch( mPerm ){
          case PERM_LNK:  zPerm = "120000";   break;
          case PERM_EXE:  zPerm = "100755";   break;
          default:        zPerm = "100644";   break;
        }
        printf("M %s %s %s\n", zPerm, zMark, zName);
        free(zMark);
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
     "SELECT tagname, rid, strftime('%%s',mtime),"
     "       (SELECT coalesce(euser, user) FROM event WHERE objid=rid),"
     "       value"
     "  FROM tagxref JOIN tag USING(tagid)"
     " WHERE tagtype=1 AND tagname GLOB 'sym-*'"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTagname = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    char *zMark = mark_name_from_rid(rid, &unused_mark);
    const char *zSecSince1970 = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    const char *zValue = db_column_text(&q, 4);
    if( rid==0 || !bag_find(&vers, rid) ) continue;
    zTagname += 4;
    printf("tag ");
    print_ref(zTagname);
    printf("\nfrom %s\n", zMark);
    free(zMark);
    printf("tagger");
    print_person(zUser);
    printf(" %s +0000\n", zSecSince1970);
    printf("data %d\n", zValue==NULL?0:(int)strlen(zValue)+1);
    if( zValue!=NULL ) printf("%s\n",zValue);
  }
  db_finalize(&q);

  if( markfile_out!=0 ){
    FILE *f;
    f = fossil_fopen(markfile_out, "w");
    if( f == 0 ){
      fossil_fatal("cannot open %s for writing", markfile_out);
    }
    export_marks(f, &blobs, &vers);
    if( ferror(f)!=0 || fclose(f)!=0 ){
      fossil_fatal("error while writing %s", markfile_out);
    }
  }
  bag_clear(&blobs);
  bag_clear(&vers);
}

/*
** Construct the temporary table toposort as follows:
**
**     CREATE TEMP TABLE toponode(
**        tid INTEGER PRIMARY KEY,   -- Check-in id
**        tseq INT                   -- integer total order on check-ins.
**     );
**
** This table contains all check-ins of the repository in topological
** order.  "Topological order" means that every parent check-in comes
** before all of its children.  Topological order is *almost* the same
** thing as "ORDER BY event.mtime".  Differences only arise when there
** are timewarps.  In as much as Git hates timewarps, we have to compute
** a correct topological order when doing an export.
**
** Since mtime is a usually already nearly in topological order, the
** algorithm is to start with mtime, then make adjustments as necessary
** for timewarps.  This is not a great algorithm for the general case,
** but it is very fast for the overwhelmingly common case where there
** are few timewarps.
*/
int topological_sort_checkins(int bVerbose){
  int nChange = 0;
  Stmt q1;
  Stmt chng;
  db_multi_exec(
    "CREATE TEMP TABLE toponode(\n"
    "  tid INTEGER PRIMARY KEY,\n"
    "  tseq INT\n"
    ");\n"
    "INSERT INTO toponode(tid,tseq) "
    " SELECT objid, CAST(mtime*8640000 AS int) FROM event WHERE type='ci';\n"
    "CREATE TEMP TABLE topolink(\n"
    "  tparent INT,\n"
    "  tchild INT,\n"
    "  PRIMARY KEY(tparent,tchild)\n"
    ") WITHOUT ROWID;"
    "INSERT INTO topolink(tparent,tchild)"
    "  SELECT pid, cid FROM plink;\n"
    "CREATE INDEX topolink_child ON topolink(tchild);\n"
  );

  /* Find a timewarp instance */
  db_prepare(&q1,
    "SELECT P.tseq, C.tid, C.tseq\n"
    "  FROM toponode P, toponode C, topolink X\n"
    " WHERE X.tparent=P.tid\n"
    "   AND X.tchild=C.tid\n"
    "   AND P.tseq>=C.tseq;"
  );

  /* Update the timestamp on :tid to have value :tseq */
  db_prepare(&chng,
    "UPDATE toponode SET tseq=:tseq WHERE tid=:tid"
  );

  while( db_step(&q1)==SQLITE_ROW ){
    i64 iParentTime = db_column_int64(&q1, 0);
    int iChild = db_column_int(&q1, 1);
    i64 iChildTime = db_column_int64(&q1, 2);
    nChange++;
    if( nChange>10000 ){
      fossil_fatal("failed to fix all timewarps after 100000 attempts");
    }
    db_reset(&q1);
    db_bind_int64(&chng, ":tid", iChild);
    db_bind_int64(&chng, ":tseq", iParentTime+1);
    db_step(&chng);
    db_reset(&chng);
    if( bVerbose ){
      fossil_print("moving %d from %lld to %lld\n",
                   iChild, iChildTime, iParentTime+1);
    }
  }

  db_finalize(&q1);
  db_finalize(&chng);
  return nChange;
}

/*
** COMMAND: test-topological-sort
**
** Invoke the topological_sort_checkins() interface for testing
** purposes.
*/
void test_topological_sort(void){
  int n;
  db_find_and_open_repository(0, 0);
  n = topological_sort_checkins(1);
  fossil_print("%d reorderings required\n", n);
}

/***************************************************************************
** Implementation of the "fossil git" command follows.  We hope that the
** new code that follows will largely replace the legacy "fossil export"
** and "fossil import" code above.
*/

/* Verbosity level.  Higher means more output.
**
**    0     print nothing at all
**    1     Errors only
**    2     Progress information (This is the default)
**    3     Extra details
*/
#define VERB_ERROR  1
#define VERB_NORMAL 2
#define VERB_EXTRA  3
static int gitmirror_verbosity = VERB_NORMAL;

/* The main branch in the Git repository.  The "trunk" branch of
** Fossil is renamed to be this branch name.
*/
static const char *gitmirror_mainbranch = 0;

/*
** Output routine that depends on verbosity
*/
static void gitmirror_message(int iLevel, const char *zFormat, ...){
  va_list ap;
  if( iLevel>gitmirror_verbosity ) return;
  va_start(ap, zFormat);
  fossil_vprint(zFormat, ap);
  va_end(ap);
}

/*
** Convert characters of z[] that are not allowed to be in branch or
** tag names into "_".
*/
static void gitmirror_sanitize_name(char *z){
  static unsigned char aSafe[] = {
     /* x0 x1 x2 x3 x4 x5 x6 x7 x8  x9 xA xB xC xD xE xF */
         0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0,  /* 0x */
         0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0,  /* 1x */
         0, 1, 0, 1, 0, 1, 1, 0, 1,  1, 0, 1, 1, 1, 1, 1,  /* 2x */
         1, 1, 1, 1, 1, 1, 1, 1, 1,  1, 0, 0, 1, 1, 1, 0,  /* 3x */
         0, 1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1,  /* 4x */
         1, 1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 0, 0, 1, 0, 1,  /* 5x */
         0, 1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1,  /* 6x */
         1, 1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 0, 0,  /* 7x */
  };
  unsigned char *zu = (unsigned char*)z;
  int i;
  for(i=0; zu[i]; i++){
    if( zu[i]>0x7f || !aSafe[zu[i]] ){
      zu[i] = '_';
    }else if( zu[i]=='/' && (i==0 || zu[i+1]==0 || zu[i+1]=='/') ){
      zu[i] = '_';
    }else if( zu[i]=='.' && (zu[i+1]==0 || zu[i+1]=='.'
                             || (i>0 && zu[i-1]=='.')) ){
      zu[i] = '_';
    }
  }
}

/*
** COMMAND: test-sanitize-name
**
** Usage: %fossil ARG...
**
** This sanitizes each argument and make it part of an "echo" command
** run by the shell.
*/
void test_sanitize_name_cmd(void){
  sqlite3_str *pStr;
  int i;
  char *zCmd;
  pStr = sqlite3_str_new(0);
  sqlite3_str_appendall(pStr, "echo");
  for(i=2; i<g.argc; i++){
    char *z = fossil_strdup(g.argv[i]);
    gitmirror_sanitize_name(z);
    sqlite3_str_appendf(pStr, " \"%s\"", z);
    fossil_free(z);
  }
  zCmd = sqlite3_str_finish(pStr);
  fossil_print("Command: %s\n", zCmd);
  fossil_system(zCmd);
  sqlite3_free(zCmd);
}

/*
** Quote a filename as a C-style string using \\ and \" if necessary.
** If quoting is not necessary, just return a copy of the input string.
**
** The return value is a held in memory obtained from fossil_malloc()
** and must be freed by the caller.
*/
static char *gitmirror_quote_filename_if_needed(const char *zIn){
  int i, j;
  char c;
  int nSpecial = 0;
  char *zOut;
  for(i=0; (c = zIn[i])!=0; i++){
    if( c=='\\' || c=='"' || c=='\n' ){
      nSpecial++;
    }
  }
  if( nSpecial==0 ){
    return fossil_strdup(zIn);
  }
  zOut = fossil_malloc( i+nSpecial+3 );
  zOut[0] = '"';
  for(i=0, j=1; (c = zIn[i])!=0; i++){
    if( c=='\\' || c=='"' || c=='\n' ){
      zOut[j++] = '\\';
      if( c=='\n' ){
        zOut[j++] = 'n';
      }else{
        zOut[j++] = c;
      }
    }else{
      zOut[j++] = c;
    }
  }
  zOut[j++] = '"';
  zOut[j] = 0;
  return zOut;
}

/*
** Find the Git-name corresponding to the Fossil-name zUuid.
**
** If the mark does not exist and if the bCreate flag is false, then
** return NULL.  If the mark does not exist and the bCreate flag is true,
** then create the mark.
**
** The string returned is obtained from fossil_malloc() and should
** be freed by the caller.
*/
static char *gitmirror_find_mark(const char *zUuid, int isFile, int bCreate){
  static Stmt sFind, sIns;
  db_static_prepare(&sFind,
    "SELECT coalesce(githash,printf(':%%d',id))"
    " FROM mirror.mmark WHERE uuid=:uuid AND isfile=:isfile"
  );
  db_bind_text(&sFind, ":uuid", zUuid);
  db_bind_int(&sFind, ":isfile", isFile!=0);
  if( db_step(&sFind)==SQLITE_ROW ){
    char *zMark = fossil_strdup(db_column_text(&sFind, 0));
    db_reset(&sFind);
    return zMark;
  }
  db_reset(&sFind);
  if( !bCreate ){
    return 0;
  }
  db_static_prepare(&sIns,
    "INSERT INTO mirror.mmark(uuid,isfile) VALUES(:uuid,:isfile)"
  );
  db_bind_text(&sIns, ":uuid", zUuid);
  db_bind_int(&sIns, ":isfile", isFile!=0);
  db_step(&sIns);
  db_reset(&sIns);
  return mprintf(":%d", db_last_insert_rowid());
}

/* This is the SHA3-256 hash of an empty file */
static const char zEmptySha3[] =
  "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a";

/*
** Export a single file named by zUuid.
**
** Return 0 on success and non-zero on any failure.
**
** If zUuid is a shunned file, then treat it as if it were any empty file.
** But files that are missing from the repository but have not been officially
** shunned cause an error return.  Except, if bPhantomOk is true, then missing
** files are replaced by an empty file.
*/
static int gitmirror_send_file(FILE *xCmd, const char *zUuid, int bPhantomOk){
  char *zMark;
  int rid;
  int rc;
  Blob data;
  rid = fast_uuid_to_rid(zUuid);
  if( rid<0 ){
    if( bPhantomOk || uuid_is_shunned(zUuid) ){
      gitmirror_message(VERB_EXTRA, "missing file: %s\n", zUuid);
      zUuid = zEmptySha3;
    }else{
      return 1;
    }
  }else{
    rc = content_get(rid, &data);
    if( rc==0 ){
      if( bPhantomOk ){
        blob_init(&data, 0, 0);
        gitmirror_message(VERB_EXTRA, "missing file: %s\n", zUuid);
        zUuid = zEmptySha3;
      }else{
        return 1;
      }
    }
  }
  zMark = gitmirror_find_mark(zUuid, 1, 1);
  if( zMark[0]==':' ){
    fprintf(xCmd, "blob\nmark %s\ndata %d\n", zMark, blob_size(&data));
    fwrite(blob_buffer(&data), 1, blob_size(&data), xCmd);
    fprintf(xCmd, "\n");
  }
  fossil_free(zMark);
  blob_reset(&data);
  return 0;
}

/*
** Transfer a check-in over to the mirror.  "rid" is the BLOB.RID for
** the check-in to export.
**
** If any ancestor of the check-in has not yet been exported, then
** invoke this routine recursively to export the ancestor first.
** This can only happen on a timewarp, so deep nesting is unlikely.
**
** Before sending the check-in, first make sure all associated files
** have already been exported, and send "blob" records for any that
** have not been.  Update the MIRROR.MMARK table so that it holds the
** marks for the exported files.
**
** Return zero on success and non-zero if the export should be stopped.
*/
static int gitmirror_send_checkin(
  FILE *xCmd,           /* Write fast-import text on this pipe */
  int rid,              /* BLOB.RID for the check-in to export */
  const char *zUuid,    /* BLOB.UUID for the check-in to export */
  int *pnLimit          /* Stop when the counter reaches zero */
){
  Manifest *pMan;       /* The check-in to be output */
  int i;                /* Loop counter */
  int iParent;          /* Which immediate ancestor is primary.  -1 for none */
  Stmt q;               /* An SQL query */
  char *zBranch;        /* The branch of the check-in */
  char *zMark;          /* The Git-name of the check-in */
  Blob sql;             /* String of SQL for part of the query */
  Blob comment;         /* The comment text for the check-in */
  int nErr = 0;         /* Number of errors */
  int bPhantomOk;       /* True if phantom files should be ignored */
  char buf[24];
  char *zEmail;         /* Contact info for Git committer field */
  int fManifest;        /* Should the manifest files be included? */
  int fPManifest = 0;   /* OR of the manifest files for all parents */

  pMan = manifest_get(rid, CFTYPE_MANIFEST, 0);
  if( pMan==0 ){
    /* Must be a phantom.  Return without doing anything, and in particular
    ** without creating a mark for this check-in. */
    gitmirror_message(VERB_NORMAL, "missing check-in: %s\n", zUuid);
    return 0;
  }

  /* Check to see if any parent logins have not yet been processed, and
  ** if so, create them */
  for(i=0; i<pMan->nParent; i++){
    char *zPMark = gitmirror_find_mark(pMan->azParent[i], 0, 0);
    if( zPMark==0 ){
      int prid = db_int(0, "SELECT rid FROM blob WHERE uuid=%Q",
                        pMan->azParent[i]);
      int rc = gitmirror_send_checkin(xCmd, prid, pMan->azParent[i],
                                      pnLimit);
      if( rc || *pnLimit<=0 ){
        manifest_destroy(pMan);
        return 1;
      }
    }
    fossil_free(zPMark);
  }

  /* Ignore phantom files on check-ins that are over one year old */
  bPhantomOk = db_int(0, "SELECT %.6f<julianday('now','-1 year')",
                      pMan->rDate);

  /* Make sure all necessary files have been exported */
  db_prepare(&q,
    "SELECT uuid FROM files_of_checkin(%Q)"
    " WHERE uuid NOT IN (SELECT uuid FROM mirror.mmark)",
    zUuid
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFUuid = db_column_text(&q, 0);
    int n = gitmirror_send_file(xCmd, zFUuid, bPhantomOk);
    nErr += n;
    if( n ) gitmirror_message(VERB_ERROR, "missing file: %s\n", zFUuid);
  }
  db_finalize(&q);

  /* If some required files could not be exported, abandon the check-in
  ** export */
  if( nErr ){
    gitmirror_message(VERB_ERROR,
             "export of %s abandoned due to missing files\n", zUuid);
    *pnLimit = 0;
    manifest_destroy(pMan);
    return 1;
  }

  /* Figure out which branch this check-in is a member of */
  zBranch = db_text(0,
    "SELECT value FROM tagxref WHERE tagid=%d AND tagtype>0 AND rid=%d",
    TAG_BRANCH, rid
  );
  if( fossil_strcmp(zBranch,"trunk")==0 ){
    assert( gitmirror_mainbranch!=0 );
    fossil_free(zBranch);
    zBranch = fossil_strdup(gitmirror_mainbranch);
  }else if( zBranch==0 ){
    zBranch = mprintf("unknown");
  }else{
    gitmirror_sanitize_name(zBranch);
  }

  /* Export the check-in */
  fprintf(xCmd, "commit refs/heads/%s\n", zBranch);
  fossil_free(zBranch);
  zMark = gitmirror_find_mark(zUuid,0,1);
  fprintf(xCmd, "mark %s\n", zMark);
  fossil_free(zMark);
  sqlite3_snprintf(sizeof(buf), buf, "%lld",
     (sqlite3_int64)((pMan->rDate-2440587.5)*86400.0)
  );

  /*
  ** Check for 'fx_' table from previous Git import, otherwise take contact info
  ** from user table for <emailaddr> in committer field. If no emailaddr, check
  ** if username is in email form, otherwise use generic 'username@noemail.net'.
  */
  if (db_table_exists("repository", "fx_git")) {
    zEmail = db_text(0, "SELECT email FROM fx_git WHERE user=%Q", pMan->zUser);
  } else {
    zEmail = db_text(0, "SELECT info FROM user WHERE login=%Q", pMan->zUser);
  }

  /* Some repo 'info' fields return an empty string hence the second check */
  if( zEmail==0 ){
    /* If username is in emailaddr form, don't append '@noemail.net' */
    if( pMan->zUser==0 || strchr(pMan->zUser, '@')==0 ){
      zEmail = mprintf("%s@noemail.net", pMan->zUser);
    } else {
      zEmail = fossil_strdup(pMan->zUser);
    }
  }else{
    char *zTmp = strchr(zEmail, '<');
    if( zTmp ){
      char *zTmpEnd = strchr(zTmp+1, '>');
      char *zNew;
      int i;
      if( zTmpEnd ) *(zTmpEnd) = 0;
      zNew = fossil_strdup(zTmp+1);
      fossil_free(zEmail);
      zEmail = zNew;
      for(i=0; zEmail[i] && !fossil_isspace(zEmail[i]); i++){}
      zEmail[i] = 0;
    }
  }
  fprintf(xCmd, "# rid=%d\n", rid);
  fprintf(xCmd, "committer %s <%s> %s +0000\n", pMan->zUser, zEmail, buf);
  fossil_free(zEmail);
  blob_init(&comment, pMan->zComment, -1);
  if( blob_size(&comment)==0 ){
    blob_append(&comment, "(no comment)", -1);
  }
  blob_appendf(&comment, "\n\nFossilOrigin-Name: %s", zUuid);
  fprintf(xCmd, "data %d\n%s\n", blob_strlen(&comment), blob_str(&comment));
  blob_reset(&comment);
  iParent = -1;  /* Which ancestor is the primary parent */
  for(i=0; i<pMan->nParent; i++){
    char *zOther = gitmirror_find_mark(pMan->azParent[i],0,0);
    if( zOther==0 ) continue;
    fPManifest |= db_get_manifest_setting(pMan->azParent[i]);
    if( iParent<0 ){
      iParent = i;
      fprintf(xCmd, "from %s\n", zOther);
    }else{
      fprintf(xCmd, "merge %s\n", zOther);
    }
    fossil_free(zOther);
  }
  if( iParent>=0 ){
    db_prepare(&q,
      "SELECT filename FROM files_of_checkin(%Q)"
      " EXCEPT SELECT filename FROM files_of_checkin(%Q)",
      pMan->azParent[iParent], zUuid
    );
    while( db_step(&q)==SQLITE_ROW ){
      fprintf(xCmd, "D %s\n", db_column_text(&q,0));
    }
    db_finalize(&q);
  }
  blob_init(&sql, 0, 0);
  blob_append_sql(&sql,
    "SELECT filename, uuid, perm FROM files_of_checkin(%Q)",
    zUuid
  );
  if( pMan->nParent ){
    blob_append_sql(&sql,
      " EXCEPT SELECT filename, uuid, perm FROM files_of_checkin(%Q)",
      pMan->azParent[0]);
  }
  db_prepare(&q,
     "SELECT x.filename, x.perm,"
          "  coalesce(mmark.githash,printf(':%%d',mmark.id))"
     "  FROM (%s) AS x, mirror.mmark"
     " WHERE mmark.uuid=x.uuid AND isfile",
     blob_sql_text(&sql)
  );
  blob_reset(&sql);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFilename = db_column_text(&q,0);
    const char *zMode = db_column_text(&q,1);
    const char *zMark = db_column_text(&q,2);
    const char *zGitMode = "100644";
    char *zFNQuoted = 0;
    if( zMode ){
      if( strchr(zMode,'x') ) zGitMode = "100755";
      if( strchr(zMode,'l') ) zGitMode = "120000";
    }
    zFNQuoted = gitmirror_quote_filename_if_needed(zFilename);
    fprintf(xCmd,"M %s %s %s\n", zGitMode, zMark, zFNQuoted);
    fossil_free(zFNQuoted);
  }
  db_finalize(&q);
  manifest_destroy(pMan);
  pMan = 0;

  /* Include Fossil-generated auxiliary files in the check-in */
  fManifest = db_get_manifest_setting(zUuid);
  if( fManifest & MFESTFLG_RAW ){
    Blob manifest;
    content_get(rid, &manifest);
    sterilize_manifest(&manifest, CFTYPE_MANIFEST);
    fprintf(xCmd,"M 100644 inline manifest\ndata %d\n%s\n",
      blob_strlen(&manifest), blob_str(&manifest));
    blob_reset(&manifest);
  }else if( fPManifest & MFESTFLG_RAW ){
    fprintf(xCmd, "D manifest\n");
  }
  if( fManifest & MFESTFLG_UUID ){
    int n = (int)strlen(zUuid);
    fprintf(xCmd,"M 100644 inline manifest.uuid\ndata %d\n%s\n\n", n+1, zUuid);
  }else if( fPManifest & MFESTFLG_UUID ){
    fprintf(xCmd, "D manifest.uuid\n");
  }
  if( fManifest & MFESTFLG_TAGS ){
    Blob tagslist;
    blob_init(&tagslist, 0, 0);
    get_checkin_taglist(rid, &tagslist);
    fprintf(xCmd,"M 100644 inline manifest.tags\ndata %d\n%s\n",
      blob_strlen(&tagslist), blob_str(&tagslist));
    blob_reset(&tagslist);
  }else if( fPManifest & MFESTFLG_TAGS ){
    fprintf(xCmd, "D manifest.tags\n");
  }

  /* The check-in is finished, so decrement the counter */
  (*pnLimit)--;
  return 0;
}

/*
** Create a new Git repository at zMirror to use as the mirror.
** Try to make zMainBr be the main branch for the new repository.
**
** A side-effect of this routine is that current-working directory
** is changed to zMirror.
**
** If zMainBr is initially NULL, then the return value will be the
** name of the default branch to be used by Git.  If zMainBr is
** initially non-NULL, then the return value will be a copy of zMainBr.
*/
static char *gitmirror_init(
  const char *zMirror,
  char *zMainBr
){
  char *zCmd;
  int rc;

  /* Create a new Git repository at zMirror */
  zCmd = mprintf("git init %$", zMirror);
  gitmirror_message(VERB_NORMAL, "%s\n", zCmd);
  rc = fossil_system(zCmd);
  if( rc ){
    fossil_fatal("cannot initialize git repository using: %s", zCmd);
  }
  fossil_free(zCmd);

  /* Must be in the new Git repository directory for subsequent commands */
  rc = file_chdir(zMirror, 0);
  if( rc ){
    fossil_fatal("cannot change to directory \"%s\"", zMirror);
  }

  if( zMainBr ){
    /* Set the current branch to zMainBr */
    zCmd = mprintf("git symbolic-ref HEAD refs/heads/%s", zMainBr);
    gitmirror_message(VERB_NORMAL, "%s\n", zCmd);
    rc = fossil_system(zCmd);
    if( rc ){
      fossil_fatal("git command failed: %s", zCmd);
    }
    fossil_free(zCmd);
  }else{
    /* If zMainBr is not specified, then check to see what branch
    ** name Git chose for itself */
    char *z;
    char zLine[1000];
    FILE *xCmd;
    int i;
    zCmd = "git symbolic-ref --short HEAD";
    gitmirror_message(VERB_NORMAL, "%s\n", zCmd);
    xCmd = popen(zCmd, "r");
    if( xCmd==0 ){
      fossil_fatal("git command failed: %s", zCmd);
    }

    z = fgets(zLine, sizeof(zLine), xCmd);
    pclose(xCmd);
    if( z==0 ){
      fossil_fatal("no output from \"%s\"", zCmd);
    }
    for(i=0; z[i] && !fossil_isspace(z[i]); i++){}
    z[i] = 0;
    zMainBr = fossil_strdup(z);
  }
  return zMainBr;
}

/*
** Implementation of the "fossil git export" command.
*/
void gitmirror_export_command(void){
  const char *zLimit;             /* Text of the --limit flag */
  int nLimit = 0x7fffffff;        /* Numeric value of the --limit flag */
  int nTotal = 0;                 /* Total number of check-ins to export */
  char *zMirror;                  /* Name of the mirror */
  char *z;                        /* Generic string */
  char *zCmd;                     /* git command to run as a subprocess */
  const char *zDebug = 0;         /* Value of the --debug flag */
  const char *zAutoPush = 0;      /* Value of the --autopush flag */
  char *zMainBr = 0;              /* Value of the --mainbranch flag */
  char *zPushUrl;                 /* URL to sync the mirror to */
  double rEnd;                    /* time of most recent export */
  int rc;                         /* Result code */
  int bForce;                     /* Do the export and sync even if no changes*/
  int bNeedRepack = 0;            /* True if we should run repack at the end */
  int bIfExists;                  /* The --if-mirrored flag */
  FILE *xCmd;                     /* Pipe to the "git fast-import" command */
  FILE *pMarks;                   /* Git mark files */
  Stmt q;                         /* Queries */
  char zLine[200];                /* One line of a mark file */

  zDebug = find_option("debug",0,1);
  db_find_and_open_repository(0, 0);
  zLimit = find_option("limit", 0, 1);
  if( zLimit ){
    nLimit = (unsigned int)atoi(zLimit);
    if( nLimit<=0 ) fossil_fatal("--limit must be positive");
  }
  zAutoPush = find_option("autopush",0,1);
  zMainBr = (char*)find_option("mainbranch",0,1);
  bForce = find_option("force","f",0)!=0;
  bIfExists = find_option("if-mirrored",0,0)!=0;
  gitmirror_verbosity = VERB_NORMAL;
  while( find_option("quiet","q",0)!=0 ){ gitmirror_verbosity--; }
  while( find_option("verbose","v",0)!=0 ){ gitmirror_verbosity++; }
  verify_all_options();
  if( g.argc!=4 && g.argc!=3 ){ usage("export ?MIRROR?"); }
  if( g.argc==4 ){
    Blob mirror;
    file_canonical_name(g.argv[3], &mirror, 0);
    db_set("last-git-export-repo", blob_str(&mirror), 0);
    blob_reset(&mirror);
  }
  zMirror = db_get("last-git-export-repo", 0);
  if( zMirror==0 ){
    if( bIfExists ) return;
    fossil_fatal("no Git repository specified");
  }

  if( zMainBr ){
    z = fossil_strdup(zMainBr);
    gitmirror_sanitize_name(z);
    if( strcmp(z, zMainBr) ){
      fossil_fatal("\"%s\" is not a legal branch name for Git", zMainBr);
    }
    fossil_free(z);
  }

  /* Make sure the GIT repository directory exists */
  rc = file_mkdir(zMirror, ExtFILE, 0);
  if( rc ) fossil_fatal("cannot create directory \"%s\"", zMirror);

  /* Make sure GIT has been initialized */
  z = mprintf("%s/.git", zMirror);
  if( !file_isdir(z, ExtFILE) ){
    zMainBr = gitmirror_init(zMirror, zMainBr);
    bNeedRepack = 1;
  }
  fossil_free(z);

  /* Make sure the .mirror_state subdirectory exists */
  z = mprintf("%s/.mirror_state", zMirror);
  rc = file_mkdir(z, ExtFILE, 0);
  if( rc ) fossil_fatal("cannot create directory \"%s\"", z);
  fossil_free(z);

  /* Attach the .mirror_state/db database */
  db_multi_exec("ATTACH '%q/.mirror_state/db' AS mirror;", zMirror);
  db_begin_write();
  db_multi_exec(
    "CREATE TABLE IF NOT EXISTS mirror.mconfig(\n"
    "  key TEXT PRIMARY KEY,\n"
    "  Value ANY\n"
    ") WITHOUT ROWID;\n"
    "CREATE TABLE IF NOT EXISTS mirror.mmark(\n"
    "  id INTEGER PRIMARY KEY,\n"
    "  uuid TEXT,\n"
    "  isfile BOOLEAN,\n"
    "  githash TEXT,\n"
    "  UNIQUE(uuid,isfile)\n"
    ");"
  );
  if( !db_table_has_column("mirror","mmark","isfile") ){
    db_multi_exec(
      "ALTER TABLE mirror.mmark RENAME TO mmark_old;"
      "CREATE TABLE IF NOT EXISTS mirror.mmark(\n"
      "  id INTEGER PRIMARY KEY,\n"
      "  uuid TEXT,\n"
      "  isfile BOOLEAN,\n"
      "  githash TEXT,\n"
      "  UNIQUE(uuid,isfile)\n"
      ");"
      "INSERT OR IGNORE INTO mirror.mmark(id,uuid,githash,isfile)"
      "  SELECT id,uuid,githash,"
      "    NOT EXISTS(SELECT 1 FROM repository.event, repository.blob"
                 " WHERE event.objid=blob.rid"
                 "   AND blob.uuid=mmark_old.uuid)"
      "    FROM mirror.mmark_old;\n"
      "DROP TABLE mirror.mmark_old;\n"
    );
  }

  /* Change the autopush setting if the --autopush flag is present */
  if( zAutoPush ){
    if( is_false(zAutoPush) ){
      db_multi_exec("DELETE FROM mirror.mconfig WHERE key='autopush'");
    }else{
      db_multi_exec(
         "REPLACE INTO mirror.mconfig(key,value)"
         "VALUES('autopush',%Q)",
         zAutoPush
      );
    }
  }

  /* Change the mainbranch setting if the --mainbranch flag is present */
  if( zMainBr && zMainBr[0] ){
    db_multi_exec(
       "REPLACE INTO mirror.mconfig(key,value)"
       "VALUES('mainbranch',%Q)",
       zMainBr
    );
    gitmirror_mainbranch = fossil_strdup(zMainBr);
  }else{
    /* Recover the saved name of the main branch */
    gitmirror_mainbranch = db_text("master",
                "SELECT value FROM mconfig WHERE key='mainbranch'");
  }


  /* See if there is any work to be done.  Exit early if not, before starting
  ** the "git fast-import" command. */
  if( !bForce
   && !db_exists("SELECT 1 FROM event WHERE type IN ('ci','t')"
                 " AND mtime>coalesce((SELECT value FROM mconfig"
                                        " WHERE key='start'),0.0)")
  ){
    gitmirror_message(VERB_NORMAL, "no changes\n");
    db_commit_transaction();
    return;
  }

  /* Change to the MIRROR directory so that the Git commands will work */
  rc = file_chdir(zMirror, 0);
  if( rc ) fossil_fatal("cannot change the working directory to \"%s\"",
                        zMirror);

  /* Start up the git fast-import command */
  if( zDebug ){
    if( fossil_strcmp(zDebug,"stdout")==0 ){
      xCmd = stdout;
    }else{
      xCmd = fopen(zDebug, "wb");
      if( xCmd==0 ) fossil_fatal("cannot open file \"%s\" for writing", zDebug);
    }
  }else{
    zCmd = mprintf("git fast-import"
              " --export-marks=.mirror_state/marks.txt"
              " --quiet --done");
    gitmirror_message(VERB_NORMAL, "%s\n", zCmd);
#ifdef _WIN32
    xCmd = popen(zCmd, "wb");
#else
    xCmd = popen(zCmd, "w");
#endif
    if( xCmd==0 ){
      fossil_fatal("cannot start the \"git fast-import\" command");
    }
    fossil_free(zCmd);
  }

  /* Run the export */
  rEnd = 0.0;
  db_multi_exec(
    "CREATE TEMP TABLE tomirror(objid,mtime,uuid);\n"
    "INSERT INTO tomirror "
    "SELECT objid, mtime, blob.uuid FROM event, blob\n"
    " WHERE type='ci'"
    "   AND mtime>coalesce((SELECT value FROM mconfig WHERE key='start'),0.0)"
    "   AND blob.rid=event.objid"
    "   AND blob.uuid NOT IN (SELECT uuid FROM mirror.mmark WHERE NOT isfile)"
    "   AND NOT EXISTS (SELECT 1 FROM private WHERE rid=blob.rid);"
  );
  nTotal = db_int(0, "SELECT count(*) FROM tomirror");
  if( nLimit<nTotal ){
    nTotal = nLimit;
  }else if( nLimit>nTotal ){
    nLimit = nTotal;
  }
  db_prepare(&q,
    "SELECT objid, mtime, uuid FROM tomirror ORDER BY mtime"
  );
  while( nLimit && db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    double rMTime = db_column_double(&q, 1);
    const char *zUuid = db_column_text(&q, 2);
    if( rMTime>rEnd ) rEnd = rMTime;
    rc = gitmirror_send_checkin(xCmd, rid, zUuid, &nLimit);
    if( rc ) break;
    gitmirror_message(VERB_NORMAL,"%d/%d      \r", nTotal-nLimit, nTotal);
    fflush(stdout);
  }
  db_finalize(&q);
  fprintf(xCmd, "done\n");
  if( zDebug ){
    if( xCmd!=stdout ) fclose(xCmd);
  }else{
    pclose(xCmd);
  }
  gitmirror_message(VERB_NORMAL, "%d check-ins added to the %s\n",
                    nTotal-nLimit, zMirror);

  /* Read the export-marks file.  Transfer the new marks over into
  ** the import-marks file.
  */
  pMarks = fopen(".mirror_state/marks.txt", "rb");
  if( pMarks ){
    db_prepare(&q, "UPDATE mirror.mmark SET githash=:githash WHERE id=:id");
    while( fgets(zLine, sizeof(zLine), pMarks) ){
      int j, k;
      if( zLine[0]!=':' ) continue;
      db_bind_int(&q, ":id", atoi(zLine+1));
      for(j=1; zLine[j] && zLine[j]!=' '; j++){}
      if( zLine[j]!=' ' ) continue;
      j++;
      if( zLine[j]==0 ) continue;
      for(k=j; fossil_isalnum(zLine[k]); k++){}
      zLine[k] = 0;
      db_bind_text(&q, ":githash", &zLine[j]);
      db_step(&q);
      db_reset(&q);
    }
    db_finalize(&q);
    fclose(pMarks);
    file_delete(".mirror_state/marks.txt");
  }else{
    fossil_fatal("git fast-import didn't generate a marks file!");
  }
  db_multi_exec(
    "CREATE INDEX IF NOT EXISTS mirror.mmarkx1 ON mmark(githash);"
  );

  /* Do any tags that have been created since the start time */
  db_prepare(&q,
    "SELECT substr(tagname,5), githash"
    "  FROM (SELECT tagxref.tagid AS xtagid, tagname, rid, max(mtime) AS mtime"
    "          FROM tagxref JOIN tag ON tag.tagid=tagxref.tagid"
    "         WHERE tag.tagname GLOB 'sym-*'"
    "           AND tagxref.tagtype=1"
    "           AND tagxref.mtime > coalesce((SELECT value FROM mconfig"
                                        " WHERE key='start'),0.0)"
    "         GROUP BY tagxref.tagid) AS tx"
    "       JOIN blob ON tx.rid=blob.rid"
    "       JOIN mmark ON mmark.uuid=blob.uuid;"
  );
  while( db_step(&q)==SQLITE_ROW ){
    char *zTagname = fossil_strdup(db_column_text(&q,0));
    const char *zObj = db_column_text(&q,1);
    char *zTagCmd;
    gitmirror_sanitize_name(zTagname);
    zTagCmd = mprintf("git tag -f %$ %$", zTagname, zObj);
    fossil_free(zTagname);
    gitmirror_message(VERB_NORMAL, "%s\n", zTagCmd);
    fossil_system(zTagCmd);
    fossil_free(zTagCmd);
  }
  db_finalize(&q);

  /* Update all references that might have changed since the start time */
  db_prepare(&q,
    "SELECT"
    " tagxref.value AS name,"
    " max(event.mtime) AS mtime,"
    " mmark.githash AS gitckin"
    " FROM tagxref, tag, event, blob, mmark"
    " WHERE tagxref.tagid=tag.tagid"
    " AND tagxref.tagtype>0"
    " AND tag.tagname='branch'"
    " AND event.objid=tagxref.rid"
    " AND event.mtime > coalesce((SELECT value FROM mconfig"
                                  " WHERE key='start'),0.0)"
    " AND blob.rid=tagxref.rid"
    " AND mmark.uuid=blob.uuid"
    " GROUP BY 1"
  );
  while( db_step(&q)==SQLITE_ROW ){
    char *zBrname = fossil_strdup(db_column_text(&q,0));
    const char *zObj = db_column_text(&q,2);
    char *zRefCmd;
    if( fossil_strcmp(zBrname,"trunk")==0 ){
      fossil_free(zBrname);
      zBrname = fossil_strdup(gitmirror_mainbranch);
    }else{
      gitmirror_sanitize_name(zBrname);
    }
    zRefCmd = mprintf("git update-ref \"refs/heads/%s\" %$", zBrname, zObj);
    fossil_free(zBrname);
    gitmirror_message(VERB_NORMAL, "%s\n", zRefCmd);
    fossil_system(zRefCmd);
    fossil_free(zRefCmd);
  }
  db_finalize(&q);

  /* Update the start time */
  if( rEnd>0.0 ){
    db_prepare(&q, "REPLACE INTO mirror.mconfig(key,value) VALUES('start',:x)");
    db_bind_double(&q, ":x", rEnd);
    db_step(&q);
    db_finalize(&q);
  }
  db_commit_transaction();

  /* Maybe run a git repack */
  if( bNeedRepack ){
    const char *zRepack = "git repack -adf";
    gitmirror_message(VERB_NORMAL, "%s\n", zRepack);
    fossil_system(zRepack);
  }

  /* Optionally do a "git push" */
  zPushUrl = db_text(0, "SELECT value FROM mconfig WHERE key='autopush'");
  if( zPushUrl ){
    char *zPushCmd;
    UrlData url;
    if( sqlite3_strglob("http*", zPushUrl)==0 ){
      url_parse_local(zPushUrl, 0, &url);
      zPushCmd = mprintf("git push --mirror %s", url.canonical);
    }else{
      zPushCmd = mprintf("git push --mirror %s", zPushUrl);
    }
    gitmirror_message(VERB_NORMAL, "%s\n", zPushCmd);
    fossil_free(zPushCmd);
    zPushCmd = mprintf("git push --mirror %$", zPushUrl);
    rc = fossil_system(zPushCmd);
    if( rc ){
      fossil_fatal("cannot push content using: %s", zPushCmd);
    }else if( db_is_writeable("repository") ){
      db_unprotect(PROTECT_CONFIG);
      db_multi_exec("REPLACE INTO config(name,value,mtime)"
                    "VALUES('gitpush:%q','{}',now())", zPushUrl);
      db_protect_pop();
    }
    fossil_free(zPushCmd);
  }
}

/*
** Implementation of the "fossil git status" command.
**
** Show the status of a "git export".
*/
void gitmirror_status_command(void){
  char *zMirror;
  char *z;
  int n, k;
  int rc;
  char *zSql;
  int bQuiet = 0;
  int bByAll = 0;   /* Undocumented option meaning this command was invoked
                    ** from "fossil all" and should modify output accordingly */

  db_find_and_open_repository(0, 0);
  bQuiet = find_option("quiet","q",0)!=0;
  bByAll = find_option("by-all",0,0)!=0;
  verify_all_options();
  zMirror = db_get("last-git-export-repo", 0);
  if( zMirror==0 ){
    if( bQuiet ) return;
    if( bByAll ) return;
    fossil_print("Git mirror:  none\n");
    return;
  }
  zSql = sqlite3_mprintf("ATTACH '%q/.mirror_state/db' AS mirror", zMirror);
  if( zSql==0 ) fossil_fatal("out of memory");
  g.dbIgnoreErrors++;
  rc = sqlite3_exec(g.db, zSql, 0, 0, 0);
  g.dbIgnoreErrors--;
  sqlite3_free(zSql);
  if( rc ){
    if( bQuiet ) return;
    if( bByAll ) return;
    fossil_print("Git mirror:  %s  (Inactive)\n", zMirror);
    return;
  }
  if( bByAll ){
    size_t len = strlen(g.zRepositoryName);
    int n;
    if( len>60 ) len = 60;
    n = (int)(65 - len);
    fossil_print("%.12c %s %.*c\n", '*', g.zRepositoryName, n, '*');
  }
  fossil_print("Git mirror:  %s\n", zMirror);
  z = db_text(0, "SELECT datetime(value) FROM mconfig WHERE key='start'");
  if( z ){
    double rAge = db_double(0.0, "SELECT julianday('now') - value"
                              " FROM mconfig WHERE key='start'");
    if( rAge>1.0/86400.0 ){
      fossil_print("Last export: %s (%z ago)\n", z, human_readable_age(rAge));
    }else{
      fossil_print("Last export: %s (moments ago)\n", z);
    }
  }
  z = db_text(0, "SELECT value FROM mconfig WHERE key='autopush'");
  if( z==0 ){
    fossil_print("Autopush:    off\n");
  }else{
    UrlData url;
    if( sqlite3_strglob("http*", z)==0 ){
      url_parse_local(z, 0, &url);
      fossil_print("Autopush:    %s\n", url.canonical);
    }else{
      fossil_print("Autopush:    %s\n", z);
    }
    fossil_free(z);
  }
  n = db_int(0,
    "SELECT count(*) FROM event"
    " WHERE type='ci'"
    "   AND mtime>coalesce((SELECT value FROM mconfig"
                          "  WHERE key='start'),0.0)"
  );
  z = db_text("master", "SELECT value FROM mconfig WHERE key='mainbranch'");
  fossil_print("Main-Branch: %s\n",z);
  if( n==0 ){
    fossil_print("Status:      up-to-date\n");
  }else{
    fossil_print("Status:      %d check-in%s awaiting export\n",
                 n, n==1 ? "" : "s");
  }
  n = db_int(0, "SELECT count(*) FROM mmark WHERE isfile");
  k = db_int(0, "SELECT count(*) FROM mmark WHERE NOT isfile");
  fossil_print("Exported:    %d check-ins and %d file blobs\n", k, n);
}

/*
** COMMAND: git*
**
** Usage: %fossil git SUBCOMMAND
**
** Do incremental import or export operations between Fossil and Git.
** Subcommands:
**
** > fossil git export [MIRROR] [OPTIONS]
**
**       Write content from the Fossil repository into the Git repository
**       in directory MIRROR.  The Git repository is created if it does not
**       already exist.  If the Git repository does already exist, then
**       new content added to fossil since the previous export is appended.
**
**       Repeat this command whenever new check-ins are added to the Fossil
**       repository in order to reflect those changes into the mirror.  If
**       the MIRROR option is omitted, the repository from the previous
**       invocation is used.
**
**       The MIRROR directory will contain a subdirectory named
**       ".mirror_state" that contains information that Fossil needs to
**       do incremental exports.  Do not attempt to manage or edit the files
**       in that directory since doing so can disrupt future incremental
**       exports.
**
**       Options:
**         --autopush URL      Automatically do a 'git push' to URL.  The
**                             URL is remembered and used on subsequent exports
**                             to the same repository.  Or if URL is "off" the
**                             auto-push mechanism is disabled
**         --debug FILE        Write fast-export text to FILE rather than
**                             piping it into "git fast-import"
**         -f|--force          Do the export even if nothing has changed
**         --if-mirrored       No-op if the mirror does not already exist
**         --limit N           Add no more than N new check-ins to MIRROR.
**                             Useful for debugging
**         --mainbranch NAME   Use NAME as the name of the main branch in Git.
**                             The "trunk" branch of the Fossil repository is
**                             mapped into this name.  "master" is used if
**                             this option is omitted.
**         -q|--quiet          Reduce output. Repeat for even less output.
**         -v|--verbose        More output
**
** > fossil git import MIRROR
**
**       TBD...
**
** > fossil git status
**
**       Show the status of the current Git mirror, if there is one.
**
**         -q|--quiet         No output if there is nothing to report
*/
void gitmirror_command(void){
  char *zCmd;
  int nCmd;
  if( g.argc<3 ){
    usage("SUBCOMMAND ...");
  }
  zCmd =  g.argv[2];
  nCmd = (int)strlen(zCmd);
  if( nCmd>2 && strncmp(zCmd,"export",nCmd)==0 ){
    gitmirror_export_command();
  }else
  if( nCmd>2 && strncmp(zCmd,"import",nCmd)==0 ){
    fossil_fatal("not yet implemented - check back later");
  }else
  if( nCmd>2 && strncmp(zCmd,"status",nCmd)==0 ){
    gitmirror_status_command();
  }else
  {
    fossil_fatal("unknown subcommand \"%s\": should be one of "
                 "\"export\", \"import\", \"status\"",
                 zCmd);
  }
}
