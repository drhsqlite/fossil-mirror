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
** This file contains code used to import the content of a Git 
** repository in the git-fast-import format as a new Fossil
** repository.
*/
#include "config.h"
#include "import.h"
#include <assert.h>

/*
** State information about an on-going fast-import parse.
*/
static struct {
  void (*xFinish)(void);      /* Function to finish a prior record */
  int nData;                  /* Bytes of data */
  char *zTag;                 /* Name of a tag */
  char *zBranch;              /* Name of a branch for a commit */
  char *aData;                /* Data content */
  char *zMark;                /* The current mark */
  char *zDate;                /* Date/time stamp */
  char *zUser;                /* User name */
  char *zComment;             /* Comment of a commit */
  char *zFrom;                /* from value */
  int nMerge;                 /* Number of merge values */
  int nMergeAlloc;            /* Number of slots in azMerge[] */
  char **azMerge;             /* Merge values */
  int nFile;                  /* Number of aFile values */
  int nFileAlloc;             /* Number of slots in aFile[] */
  ManifestFile *aFile;        /* Information about files in a commit */
} gg;

/*
** A no-op "xFinish" method
*/
static void finish_noop(void){}
   
/*
** Deallocate the state information.
**
** The azMerge[] and aFile[] arrays are zeroed by allocated space is
** retained unless the freeAll flag is set.
*/
static void import_reset(int freeAll){
  int i;
  gg.xFinish = 0;
  fossil_free(gg.zTag); gg.zTag = 0;
  fossil_free(gg.zBranch); gg.zBranch = 0;
  fossil_free(gg.aData); gg.aData = 0;
  fossil_free(gg.zMark); gg.zMark = 0;
  fossil_free(gg.zDate); gg.zDate = 0;
  fossil_free(gg.zUser); gg.zUser = 0;
  fossil_free(gg.zComment); gg.zComment = 0;
  fossil_free(gg.zFrom); gg.zFrom = 0;
  for(i=0; i<gg.nMerge; i++){
    fossil_free(gg.azMerge[i]); gg.azMerge[i] = 0;
  }
  gg.nMerge = 0;
  for(i=0; i<gg.nFile; i++){
    fossil_free(gg.aFile[i].zName);
    fossil_free(gg.aFile[i].zUuid);
    fossil_free(gg.aFile[i].zPerm);
    fossil_free(gg.aFile[i].zPrior);
  }
  memset(gg.aFile, 0, gg.nFile*sizeof(gg.aFile[0]));
  gg.nFile = 0;
  if( freeAll ){
    fossil_free(gg.azMerge);
    fossil_free(gg.aFile);
    memset(&gg, 0, sizeof(gg));
  }
  gg.xFinish = finish_noop;
}

/*
** Insert an artifact into the BLOB table if it isn't there already.
** If zMark is not zero, create a cross-reference from that mark back
** to the newly inserted artifact.
*/
static int fast_insert_content(Blob *pContent, const char *zMark){
  Blob hash;
  Blob cmpr;
  int rid;

  sha1sum_blob(pContent, &hash);
  rid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &hash);
  if( rid==0 ){
    static Stmt ins;
    db_static_prepare(&ins,
        "INSERT INTO blob(uuid, size, content) VALUES(:uuid, :size, :content)"
    );
    db_bind_text(&ins, ":uuid", blob_str(&hash));
    db_bind_int(&ins, ":size", gg.nData);
    blob_compress(pContent, &cmpr);
    db_bind_blob(&ins, ":content", &cmpr);
    db_step(&ins);
    db_reset(&ins);
    blob_reset(&cmpr);
    rid = db_last_insert_rowid();
  }
  if( zMark ){
    db_multi_exec(
        "INSERT INTO xtag(tname, trid, tuuid)"
        "VALUES(%Q,%d,%B)", 
        zMark, rid, &hash
    );
    db_multi_exec(
        "INSERT INTO xtag(tname, trid, tuuid)"
        "VALUES(%B,%d,%B)", 
        &hash, rid, &hash
    );
  }
  blob_reset(&hash);
  return rid;
}

/*
** Use data accumulated in gg from a "blob" record to add a new file
** to the BLOB table.
*/
static void finish_blob(void){
  Blob content;

  if( gg.nData>0 ){
    blob_init(&content, gg.aData, gg.nData);
    fast_insert_content(&content, gg.zMark);
    blob_reset(&content);
  }
  import_reset(0);
}

/*
** Use data accumulated in gg from a "tag" record to add a new 
** control artifact to the BLOB table.
*/
static void finish_tag(void){
  Blob record, cksum;
  if( gg.zDate && gg.zTag && gg.zFrom && gg.zUser ){
    blob_zero(&record);
    blob_appendf(&record, "D %z\n", gg.zDate);
    blob_appendf(&record, "T +%F %s\n", gg.zTag, gg.zFrom);
    blob_appendf(&record, "U %F\n", gg.zUser);
    md5sum_blob(&record, &cksum);
    blob_appendf(&record, "Z %b\n", &cksum);
    fast_insert_content(&record, 0);
    blob_reset(&record);
    blob_reset(&cksum);
  }
  import_reset(0);
}

/*
** Use data accumulated in gg from a "commit" record to add a new 
** manifest artifact to the BLOB table.
*/
static void finish_commit(void){
  /* TBD... */
  import_reset(0);
}


/*
** Turn the first \n in the input string into a \000
*/
static void trim_newline(char *z){
  while( z[0] && z[0]!='\n' ){ z++; }
  z[0] = 0;
}

/*
** Convert a "mark" or "committish" into the UUID.
*/
static char *resolve_committish(const char *zCommittish){
  char *zRes;

  zRes = db_text(0, "SELECT tuuid FROM xtag WHERE tname=%Q", zCommittish);
  return zRes;
}


/*
** Read the git-fast-import format from pIn and insert the corresponding
** content into the database.
*/
static void git_fast_import(FILE *pIn){
  char zLine[1000];
  gg.xFinish = finish_noop;
  while( fgets(zLine, sizeof(zLine), pIn) ){
    if( zLine[0]=='\n' || zLine[0]=='#' ) continue;
    if( memcmp(zLine, "blob", 4)==0 ){
      gg.xFinish();
      gg.xFinish = finish_blob;
    }else
    if( memcmp(zLine, "commit ", 7)==0 ){
      gg.xFinish();
      gg.xFinish = finish_commit;
      trim_newline(&zLine[7]);
      gg.zBranch = mprintf("%s", &zLine[7]);
    }else
    if( memcmp(zLine, "tag ", 4)==0 ){
      gg.xFinish();
      gg.xFinish = finish_tag;
      trim_newline(&zLine[4]);
      gg.zTag = mprintf("%s", &zLine[4]);
    }else
    if( memcmp(zLine, "reset ", 4)==0 ){
      gg.xFinish();
    }else
    if( memcmp(zLine, "checkpoint", 10)==0 ){
      gg.xFinish();
    }else
    if( memcmp(zLine, "feature", 7)==0 ){
      gg.xFinish();
    }else
    if( memcmp(zLine, "option", 6)==0 ){
      gg.xFinish();
    }else
    if( memcmp(zLine, "progress ", 9)==0 ){
      gg.xFinish();
      trim_newline(&zLine[9]);
      printf("%s\n", &zLine[9]);
      fflush(stdout);
    }else
    if( memcmp(zLine, "data ", 5)==0 ){
      fossil_free(gg.aData); gg.aData = 0;
      gg.nData = atoi(&zLine[5]);
      if( gg.nData ){
        int got;
        gg.aData = fossil_malloc( gg.nData+1 );
        got = fread(gg.aData, 1, gg.nData, pIn);
        if( got!=gg.nData ){
          fossil_fatal("short read: got %d of %d bytes", got, gg.nData);
        }
        if( gg.zComment==0 && gg.xFinish==finish_commit ){
          gg.zComment = gg.aData;
          gg.aData = 0;
          gg.nData = 0;
        }
      }
    }else
    if( memcmp(zLine, "author ", 7)==0 ){
      /* No-op */
    }else
    if( memcmp(zLine, "mark ", 5)==0 ){
      trim_newline(&zLine[5]);
      fossil_free(gg.zMark);
      gg.zMark = mprintf("%s", &zLine[5]);
    }else
    if( memcmp(zLine, "tagger ", 7)==0 || memcmp(zLine, "committer ",9)==0 ){
      int i;
      char *z;
      sqlite3_int64 secSince1970;

      for(i=0; zLine[i] && zLine[i]!='<'; i++){}
      if( zLine[i]==0 ) goto malformed_line;
      z = &zLine[i+1];
      for(i=i+1; zLine[i] && zLine[i]!='>'; i++){}
      if( zLine[i]==0 ) goto malformed_line;
      zLine[i] = 0;
      fossil_free(gg.zUser);
      gg.zUser = mprintf("%s", z);
      secSince1970 = 0;
      for(i=i+1; fossil_isdigit(zLine[i]); i++){
        secSince1970 = secSince1970*10 + zLine[i] - '0';
      }
      fossil_free(gg.zDate);
      gg.zDate = db_text(0, "SELECT datetime(%lld, 'unixepoch')", secSince1970);
      gg.zDate[10] = 'T';
    }else
    if( memcmp(zLine, "from ", 5)==0 ){
      trim_newline(&zLine[5]);
      fossil_free(gg.zFrom);
      gg.zFrom = resolve_committish(&zLine[5]);
    }else
    if( memcmp(zLine, "merge ", 6)==0 ){
      trim_newline(&zLine[6]);
      if( gg.nMerge>=gg.nMergeAlloc ){
        gg.nMergeAlloc = gg.nMergeAlloc*2 + 10;
        gg.azMerge = fossil_realloc(gg.azMerge, gg.nMergeAlloc*sizeof(char*));
      }
      gg.azMerge[gg.nMerge] = resolve_committish(&zLine[5]);
      if( gg.azMerge[gg.nMerge] ) gg.nMerge++;
    }else
    if( memcmp(zLine, "M ", 2)==0 ){
    }else
    if( memcmp(zLine, "D ", 2)==0 ){
    }else
    if( memcmp(zLine, "C ", 2)==0 ){
    }else
    if( memcmp(zLine, "R ", 2)==0 ){
    }else
    if( memcmp(zLine, "deleteall", 9)==0 ){
    }else
    if( memcmp(zLine, "N ", 2)==0 ){
    }else

    {
      goto malformed_line;
    }
  }
  gg.xFinish();
  import_reset(1);
  return;

malformed_line:
  trim_newline(zLine);
  fossil_fatal("bad fast-import line: [%s]", zLine);
  return;
}

/*
** COMMAND: import
**
** Usage: %fossil import NEW-REPOSITORY
**
** Read text generated by the git-fast-export command and use it to
** construct a new Fossil repository named by the NEW-REPOSITORY
** argument.  The get-fast-export text is read from standard input.
*/
void git_import_cmd(void){
  char *zPassword;
  fossil_fatal("this command is still under development....");
  if( g.argc!=3 ){
    usage("REPOSITORY-NAME");
  }
  db_create_repository(g.argv[2]);
  db_open_repository(g.argv[2]);
  db_open_config(0);
  db_multi_exec(
     "ATTACH ':memory:' AS mem1;"
     "CREATE TABLE mem1.xtag(tname TEXT UNIQUE, trid INT, tuuid TEXT);"
  );
  db_begin_transaction();
  db_initial_setup(0, 0, 1);
  git_fast_import(stdin);
  db_end_transaction(0);
  db_begin_transaction();
  printf("Rebuilding repository meta-data...\n");
  rebuild_db(0, 1);
  printf("Vacuuming..."); fflush(stdout);
  db_multi_exec("VACUUM");
  printf(" ok\n");
  printf("project-id: %s\n", db_get("project-code", 0));
  printf("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  printf("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
  db_end_transaction(0);
}
