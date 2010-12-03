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

#if INTERFACE
/*
** A single file change record.
*/
struct ImportFile { 
  char *zName;           /* Name of a file */
  char *zUuid;           /* UUID of the file */
  char *zPrior;          /* Prior name if the name was changed */
  char isFrom;           /* True if obtained from the parent */
  char isExe;            /* True if executable */
};
#endif


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
  char *zFrom;                /* from value as a UUID */
  char *zFromMark;            /* The mark of the "from" field */
  int nMerge;                 /* Number of merge values */
  int nMergeAlloc;            /* Number of slots in azMerge[] */
  char **azMerge;             /* Merge values */
  int nFile;                  /* Number of aFile values */
  int nFileAlloc;             /* Number of slots in aFile[] */
  ImportFile *aFile;          /* Information about files in a commit */
  int fromLoaded;             /* True zFrom content loaded into aFile[] */
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
  fossil_free(gg.zFromMark); gg.zFromMark = 0;
  for(i=0; i<gg.nMerge; i++){
    fossil_free(gg.azMerge[i]); gg.azMerge[i] = 0;
  }
  gg.nMerge = 0;
  for(i=0; i<gg.nFile; i++){
    fossil_free(gg.aFile[i].zName);
    fossil_free(gg.aFile[i].zUuid);
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
  blob_init(&content, gg.aData, gg.nData);
  fast_insert_content(&content, gg.zMark);
  blob_reset(&content);
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
    blob_appendf(&record, "D %s\n", gg.zDate);
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
** Compare two ImportFile objects for sorting
*/
static int mfile_cmp(const void *pLeft, const void *pRight){
  const ImportFile *pA = (const ImportFile*)pLeft;
  const ImportFile *pB = (const ImportFile*)pRight;
  return strcmp(pA->zName, pB->zName);
}

/* Forward reference */
static void import_prior_files(void);

/*
** Use data accumulated in gg from a "commit" record to add a new 
** manifest artifact to the BLOB table.
*/
static void finish_commit(void){
  int i;
  char *zFromBranch;
  Blob record, cksum;
  import_prior_files();
  qsort(gg.aFile, gg.nFile, sizeof(gg.aFile[0]), mfile_cmp);
  blob_zero(&record);
  blob_appendf(&record, "C %F\n", gg.zComment);
  blob_appendf(&record, "D %s\n", gg.zDate);
  for(i=0; i<gg.nFile; i++){
    const char *zUuid = gg.aFile[i].zUuid;
    if( zUuid==0 ) continue;
    blob_appendf(&record, "F %F %s", gg.aFile[i].zName, zUuid);
    if( gg.aFile[i].isExe ){
      blob_append(&record, " x\n", 3);
    }else{
      blob_append(&record, "\n", 1);
    }
  }
  if( gg.zFrom ){
    blob_appendf(&record, "P %s", gg.zFrom);
    for(i=0; i<gg.nMerge; i++){
      blob_appendf(&record, " %s", gg.azMerge[i]);
    }
    blob_append(&record, "\n", 1);
    zFromBranch = db_text(0, "SELECT brnm FROM xbranch WHERE tname=%Q",
                              gg.zFromMark);
  }else{
    zFromBranch = 0;
  }
  if( zFromBranch==0 || strcmp(zFromBranch, gg.zBranch)!=0 ){
    blob_appendf(&record, "T *branch * %F\n", gg.zBranch);
    blob_appendf(&record, "T *sym-%F *\n", gg.zBranch);
    if( zFromBranch ){
      blob_appendf(&record, "T -sym-%F *\n", zFromBranch);
    }
  }
  free(zFromBranch);
  if( gg.zFrom==0 ){
    blob_appendf(&record, "T +sym-trunk *\n");
  }
  db_multi_exec("INSERT INTO xbranch(tname, brnm) VALUES(%Q,%Q)",
                gg.zMark, gg.zBranch);
  blob_appendf(&record, "U %F\n", gg.zUser);
  md5sum_blob(&record, &cksum);
  blob_appendf(&record, "Z %b\n", &cksum);
  fast_insert_content(&record, gg.zMark);
  blob_reset(&record);
  blob_reset(&cksum);
  import_reset(0);  
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
** Duplicate a string.
*/
static char *import_strdup(const char *zOrig){
  char *z = 0;
  if( zOrig ){
    int n = strlen(zOrig);
    z = fossil_malloc( n+1 );
    memcpy(z, zOrig, n+1);
  }
  return z;
}

/*
** Get a token from a line of text.  Return a pointer to the first
** character of the token and zero-terminate the token.  Make
** *pzIn point to the first character past the end of the zero
** terminator, or at the zero-terminator at EOL.
*/
static char *next_token(char **pzIn){
  char *z = *pzIn;
  int i;
  if( z[0]==0 ) return z;
  for(i=0; z[i] && z[i]!=' ' && z[i]!='\n'; i++){}
  if( z[i] ){
    z[i] = 0;
    *pzIn = &z[i+1];
  }else{
    *pzIn = &z[i];
  }
  return z;
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
** Create a new entry in the gg.aFile[] array
*/
static ImportFile *import_add_file(void){
  ImportFile *pFile;
  if( gg.nFile>=gg.nFileAlloc ){
    gg.nFileAlloc = gg.nFileAlloc*2 + 100;
    gg.aFile = fossil_realloc(gg.aFile, gg.nFileAlloc*sizeof(gg.aFile[0]));
  }
  pFile = &gg.aFile[gg.nFile++];
  memset(pFile, 0, sizeof(*pFile));
  return pFile;
}


/*
** Load all file information out of the gg.zFrom check-in
*/
static void import_prior_files(void){
  Manifest *p;
  int rid;
  ManifestFile *pOld;
  ImportFile *pNew;
  if( gg.fromLoaded ) return;
  gg.fromLoaded = 1;
  if( gg.zFrom==0 ) return;
  rid = fast_uuid_to_rid(gg.zFrom);
  if( rid==0 ) return;
  p = manifest_get(rid, CFTYPE_MANIFEST);
  if( p==0 ) return;
  manifest_file_rewind(p);
  while( (pOld = manifest_file_next(p, 0))!=0 ){
    pNew = import_add_file();
    pNew->zName = import_strdup(pOld->zName);
    pNew->isExe = pOld->zPerm && strstr(pOld->zPerm, "x")!=0;
    pNew->zUuid = import_strdup(pOld->zUuid);
    pNew->isFrom = 1;
  }
  manifest_destroy(p);
}

/*
** Locate a file in the gg.aFile[] array by its name.  Begin the search
** with the *pI-th file.  Update *pI to be one past the file found.
** Do not search past the mx-th file.
*/
static ImportFile *import_find_file(const char *zName, int *pI, int mx){
  int i = *pI;
  int nName = strlen(zName);
  while( i<mx ){
    const char *z = gg.aFile[i].zName;
    if( memcmp(zName, z, nName)==0 && (z[nName]==0 || z[nName]=='/') ){
      *pI = i+1;
      return &gg.aFile[i];
    }
    i++;
  }
  return 0;
}


/*
** Read the git-fast-import format from pIn and insert the corresponding
** content into the database.
*/
static void git_fast_import(FILE *pIn){
  ImportFile *pFile, *pNew;
  int i, mx;
  char *z;
  char *zUuid;
  char *zName;
  char *zPerm;
  char *zFrom;
  char *zTo;
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
      z = &zLine[7];
      for(i=strlen(z)-1; i>=0 && z[i]!='/'; i--){}
      if( z[i+1]!=0 ) z += i+1;
      gg.zBranch = import_strdup(z);
      gg.fromLoaded = 0;
    }else
    if( memcmp(zLine, "tag ", 4)==0 ){
      gg.xFinish();
      gg.xFinish = finish_tag;
      trim_newline(&zLine[4]);
      gg.zTag = import_strdup(&zLine[4]);
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
        gg.aData[got] = 0;
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
      gg.zMark = import_strdup(&zLine[5]);
    }else
    if( memcmp(zLine, "tagger ", 7)==0 || memcmp(zLine, "committer ",10)==0 ){
      sqlite3_int64 secSince1970;
      for(i=0; zLine[i] && zLine[i]!='<'; i++){}
      if( zLine[i]==0 ) goto malformed_line;
      z = &zLine[i+1];
      for(i=i+1; zLine[i] && zLine[i]!='>'; i++){}
      if( zLine[i]==0 ) goto malformed_line;
      zLine[i] = 0;
      fossil_free(gg.zUser);
      gg.zUser = import_strdup(z);
      secSince1970 = 0;
      for(i=i+2; fossil_isdigit(zLine[i]); i++){
        secSince1970 = secSince1970*10 + zLine[i] - '0';
      }
      fossil_free(gg.zDate);
      gg.zDate = db_text(0, "SELECT datetime(%lld, 'unixepoch')", secSince1970);
      gg.zDate[10] = 'T';
    }else
    if( memcmp(zLine, "from ", 5)==0 ){
      trim_newline(&zLine[5]);
      fossil_free(gg.zFromMark);
      gg.zFromMark = import_strdup(&zLine[5]);
      fossil_free(gg.zFrom);
      gg.zFrom = resolve_committish(&zLine[5]);
    }else
    if( memcmp(zLine, "merge ", 6)==0 ){
      trim_newline(&zLine[6]);
      if( gg.nMerge>=gg.nMergeAlloc ){
        gg.nMergeAlloc = gg.nMergeAlloc*2 + 10;
        gg.azMerge = fossil_realloc(gg.azMerge, gg.nMergeAlloc*sizeof(char*));
      }
      gg.azMerge[gg.nMerge] = resolve_committish(&zLine[6]);
      if( gg.azMerge[gg.nMerge] ) gg.nMerge++;
    }else
    if( memcmp(zLine, "M ", 2)==0 ){
      import_prior_files();
      z = &zLine[2];
      zPerm = next_token(&z);
      zUuid = next_token(&z);
      zName = next_token(&z);
      i = 0;
      pFile = import_find_file(zName, &i, gg.nFile);
      if( pFile==0 ){
        pFile = import_add_file();
        pFile->zName = import_strdup(zName);
      }
      pFile->isExe = (strcmp(zPerm, "100755")==0);
      fossil_free(pFile->zUuid);
      pFile->zUuid = resolve_committish(zUuid);
      pFile->isFrom = 0;
    }else
    if( memcmp(zLine, "D ", 2)==0 ){
      import_prior_files();
      z = &zLine[2];
      zName = next_token(&z);
      i = 0;
      while( (pFile = import_find_file(zName, &i, gg.nFile))!=0 ){
        if( pFile->isFrom==0 ) continue;
        fossil_free(pFile->zName);
        fossil_free(pFile->zPrior);
        fossil_free(pFile->zUuid);
        *pFile = gg.aFile[--gg.nFile];
        i--;
      }
    }else
    if( memcmp(zLine, "C ", 2)==0 ){
      int nFrom;
      import_prior_files();
      z = &zLine[2];
      zFrom = next_token(&z);
      zTo = next_token(&z);
      i = 0;
      mx = gg.nFile;
      nFrom = strlen(zFrom);
      while( (pFile = import_find_file(zFrom, &i, mx))!=0 ){
        if( pFile->isFrom==0 ) continue;
        pNew = import_add_file();
        pFile = &gg.aFile[i-1];
        if( strlen(pFile->zName)>nFrom ){
          pNew->zName = mprintf("%s%s", zTo, pFile->zName[nFrom]);
        }else{
          pNew->zName = import_strdup(pFile->zName);
        }
        pNew->isExe = pFile->isExe;
        pNew->zUuid = import_strdup(pFile->zUuid);
        pNew->isFrom = 0;
      }
    }else
    if( memcmp(zLine, "R ", 2)==0 ){
      int nFrom;
      import_prior_files();
      z = &zLine[2];
      zFrom = next_token(&z);
      zTo = next_token(&z);
      i = 0;
      nFrom = strlen(zFrom);
      while( (pFile = import_find_file(zFrom, &i, gg.nFile))!=0 ){
        if( pFile->isFrom==0 ) continue;
        pNew = import_add_file();
        pFile = &gg.aFile[i-1];
        if( strlen(pFile->zName)>nFrom ){
          pNew->zName = mprintf("%s%s", zTo, pFile->zName[nFrom]);
        }else{
          pNew->zName = import_strdup(pFile->zName);
        }
        pNew->zPrior = pFile->zName;
        pNew->isExe = pFile->isExe;
        pNew->zUuid = pFile->zUuid;
        pNew->isFrom = 0;
        gg.nFile--;
        *pFile = *pNew;
        memset(pNew, 0, sizeof(*pNew));
      }
      fossil_fatal("cannot handle R records, use --full-tree");
    }else
    if( memcmp(zLine, "deleteall", 9)==0 ){
      gg.fromLoaded = 1;
    }else
    if( memcmp(zLine, "N ", 2)==0 ){
      /* No-op */
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
** Usage: %fossil import --git NEW-REPOSITORY
**
** Read text generated by the git-fast-export command and use it to
** construct a new Fossil repository named by the NEW-REPOSITORY
** argument.  The get-fast-export text is read from standard input.
**
** The git-fast-export file format is currently the only VCS interchange
** format that is understood, though other interchange formats may be added
** in the future.
*/
void git_import_cmd(void){
  char *zPassword;
  FILE *pIn;
  int forceFlag = find_option("force", "f", 0)!=0;
  find_option("git",0,0);  /* Skip the --git option for now */
  verify_all_options();
  if( g.argc!=3  && g.argc!=4 ){
    usage("REPOSITORY-NAME");
  }
  if( g.argc==4 ){
    pIn = fopen(g.argv[3], "rb");
  }else{
    pIn = stdin;
    fossil_binary_mode(pIn);
  }
  if( forceFlag ) unlink(g.argv[2]);
  db_create_repository(g.argv[2]);
  db_open_repository(g.argv[2]);
  db_open_config(0);
  db_multi_exec(
     "CREATE TEMP TABLE xtag(tname TEXT UNIQUE, trid INT, tuuid TEXT);"
     "CREATE TEMP TABLE xbranch(tname TEXT UNIQUE, brnm TEXT);"
  );
  db_begin_transaction();
  db_initial_setup(0, 0, 1);
  git_fast_import(pIn);
  db_end_transaction(0);
  db_begin_transaction();
  printf("Rebuilding repository meta-data...\n");
  rebuild_db(0, 1);
  verify_cancel();
  db_end_transaction(0);
  printf("Vacuuming..."); fflush(stdout);
  db_multi_exec("VACUUM");
  printf(" ok\n");
  printf("project-id: %s\n", db_get("project-code", 0));
  printf("server-id:  %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  printf("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
}
