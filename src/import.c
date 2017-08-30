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
** This file contains code used to import the content of a Git/SVN
** repository in the git-fast-import/svn-dump formats as a new Fossil
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
  char isLink;           /* True if symlink */
};
#endif

/*
** State information common to all import types.
*/
static struct {
  const char *zTrunkName;     /* Name of trunk branch */
  const char *zBranchPre;     /* Prepended to non-trunk branch names */
  const char *zBranchSuf;     /* Appended to non-trunk branch names */
  const char *zTagPre;        /* Prepended to non-trunk tag names */
  const char *zTagSuf;        /* Appended to non-trunk tag names */
} gimport;

/*
** State information about an on-going fast-import parse.
*/
static struct {
  void (*xFinish)(void);      /* Function to finish a prior record */
  int nData;                  /* Bytes of data */
  char *zTag;                 /* Name of a tag */
  char *zBranch;              /* Name of a branch for a commit */
  char *zPrevBranch;          /* The branch of the previous check-in */
  char *aData;                /* Data content */
  char *zMark;                /* The current mark */
  char *zDate;                /* Date/time stamp */
  char *zUser;                /* User name */
  char *zComment;             /* Comment of a commit */
  char *zFrom;                /* from value as a UUID */
  char *zPrevCheckin;         /* Name of the previous check-in */
  char *zFromMark;            /* The mark of the "from" field */
  int nMerge;                 /* Number of merge values */
  int nMergeAlloc;            /* Number of slots in azMerge[] */
  char **azMerge;             /* Merge values */
  int nFile;                  /* Number of aFile values */
  int nFileAlloc;             /* Number of slots in aFile[] */
  ImportFile *aFile;          /* Information about files in a commit */
  int fromLoaded;             /* True zFrom content loaded into aFile[] */
  int tagCommit;              /* True if the commit adds a tag */
} gg;

/*
** Duplicate a string.
*/
char *fossil_strndup(const char *zOrig, int len){
  char *z = 0;
  if( zOrig ){
    int n;
    if( len<0 ){
      n = strlen(zOrig);
    }else{
      for( n=0; zOrig[n] && n<len; ++n );
    }
    z = fossil_malloc( n+1 );
    memcpy(z, zOrig, n);
    z[n] = 0;
  }
  return z;
}
char *fossil_strdup(const char *zOrig){
  return fossil_strndup(zOrig, -1);
}

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
    fossil_free(gg.zPrevBranch);
    fossil_free(gg.zPrevCheckin);
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
**
** If saveUuid is true, then pContent is a commit record.  Record its
** UUID in gg.zPrevCheckin.
*/
static int fast_insert_content(
  Blob *pContent,          /* Content to insert */
  const char *zMark,       /* Label using this mark, if not NULL */
  int saveUuid,            /* Save artifact hash in gg.zPrevCheckin */
  int doParse              /* Invoke manifest_crosslink() */
){
  Blob hash;
  Blob cmpr;
  int rid;

  hname_hash(pContent, 0, &hash);
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
    if( doParse ){
      manifest_crosslink(rid, pContent, MC_NONE);
    }
  }
  if( zMark ){
    db_multi_exec(
        "INSERT OR IGNORE INTO xmark(tname, trid, tuuid)"
        "VALUES(%Q,%d,%B)",
        zMark, rid, &hash
    );
    db_multi_exec(
        "INSERT OR IGNORE INTO xmark(tname, trid, tuuid)"
        "VALUES(%B,%d,%B)",
        &hash, rid, &hash
    );
  }
  if( saveUuid ){
    fossil_free(gg.zPrevCheckin);
    gg.zPrevCheckin = fossil_strdup(blob_str(&hash));
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
  fast_insert_content(&content, gg.zMark, 0, 0);
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
    blob_appendf(&record, "T +sym-%F%F%F %s", gimport.zTagPre, gg.zTag,
        gimport.zTagSuf, gg.zFrom);
    if( gg.zComment ){
      blob_appendf(&record, " %F", gg.zComment);
    }
    blob_appendf(&record, "\nU %F\n", gg.zUser);
    md5sum_blob(&record, &cksum);
    blob_appendf(&record, "Z %b\n", &cksum);
    fast_insert_content(&record, 0, 0, 1);
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
  return fossil_strcmp(pA->zName, pB->zName);
}

/*
** Compare two strings for sorting.
*/
static int string_cmp(const void *pLeft, const void *pRight){
  const char *zLeft = *(const char **)pLeft;
  const char *zRight = *(const char **)pRight;
  return fossil_strcmp(zLeft, zRight);
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
  char *aTCard[4];                /* Array of T cards for manifest */
  int nTCard = 0;                 /* Entries used in aTCard[] */
  Blob record, cksum;

  import_prior_files();
  qsort(gg.aFile, gg.nFile, sizeof(gg.aFile[0]), mfile_cmp);
  blob_zero(&record);
  blob_appendf(&record, "C %F\n", gg.zComment);
  blob_appendf(&record, "D %s\n", gg.zDate);
  if( !g.fQuiet ) fossil_print("%.10s\r", gg.zDate);
  for(i=0; i<gg.nFile; i++){
    const char *zUuid = gg.aFile[i].zUuid;
    if( zUuid==0 ) continue;
    blob_appendf(&record, "F %F %s", gg.aFile[i].zName, zUuid);
    if( gg.aFile[i].isExe ){
      blob_append(&record, " x\n", 3);
    }else if( gg.aFile[i].isLink ){
      blob_append(&record, " l\n", 3);
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

  /* Add the required "T" cards to the manifest. Make sure they are added
  ** in sorted order and without any duplicates. Otherwise, fossil will not
  ** recognize the document as a valid manifest. */
  if( !gg.tagCommit && fossil_strcmp(zFromBranch, gg.zBranch)!=0 ){
    aTCard[nTCard++] = mprintf("T *branch * %F%F%F\n", gimport.zBranchPre,
        gg.zBranch, gimport.zBranchSuf);
    aTCard[nTCard++] = mprintf("T *sym-%F%F%F *\n", gimport.zBranchPre,
        gg.zBranch, gimport.zBranchSuf);
    if( zFromBranch ){
      aTCard[nTCard++] = mprintf("T -sym-%F%F%F *\n", gimport.zBranchPre,
          zFromBranch, gimport.zBranchSuf);
    }
  }
  if( gg.zFrom==0 ){
    aTCard[nTCard++] = mprintf("T *sym-%F *\n", gimport.zTrunkName);
  }
  qsort(aTCard, nTCard, sizeof(char *), string_cmp);
  for(i=0; i<nTCard; i++){
    if( i==0 || fossil_strcmp(aTCard[i-1], aTCard[i]) ){
      blob_appendf(&record, "%s", aTCard[i]);
    }
  }
  for(i=0; i<nTCard; i++) free(aTCard[i]);

  free(zFromBranch);
  db_multi_exec("INSERT INTO xbranch(tname, brnm) VALUES(%Q,%Q)",
                gg.zMark, gg.zBranch);
  blob_appendf(&record, "U %F\n", gg.zUser);
  md5sum_blob(&record, &cksum);
  blob_appendf(&record, "Z %b\n", &cksum);
  fast_insert_content(&record, gg.zMark, 1, 1);
  blob_reset(&cksum);

  /* The "git fast-export" command might output multiple "commit" lines
  ** that reference a tag using "refs/tags/TAGNAME".  The tag should only
  ** be applied to the last commit that is output.  The problem is we do not
  ** know at this time if the current commit is the last one to hold this
  ** tag or not.  So make an entry in the XTAG table to record this tag
  ** but overwrite that entry if a later instance of the same tag appears.
  **
  ** This behavior seems like a bug in git-fast-export, but it is easier
  ** to work around the problem than to fix git-fast-export.
  */
  if( gg.tagCommit && gg.zDate && gg.zUser && gg.zFrom ){
    blob_appendf(&record, "D %s\n", gg.zDate);
    blob_appendf(&record, "T +sym-%F%F%F %s\n", gimport.zBranchPre, gg.zBranch,
        gimport.zBranchSuf, gg.zPrevCheckin);
    blob_appendf(&record, "U %F\n", gg.zUser);
    md5sum_blob(&record, &cksum);
    blob_appendf(&record, "Z %b\n", &cksum);
    db_multi_exec(
       "INSERT OR REPLACE INTO xtag(tname, tcontent)"
       " VALUES(%Q,%Q)", gg.zBranch, blob_str(&record)
    );
    blob_reset(&record);
    blob_reset(&cksum);
  }

  fossil_free(gg.zPrevBranch);
  gg.zPrevBranch = gg.zBranch;
  gg.zBranch = 0;
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
** Return a token that is all text up to (but omitting) the next \n
** or \r\n.
*/
static char *rest_of_line(char **pzIn){
  char *z = *pzIn;
  int i;
  if( z[0]==0 ) return z;
  for(i=0; z[i] && z[i]!='\r' && z[i]!='\n'; i++){}
  if( z[i] ){
    if( z[i]=='\r' && z[i+1]=='\n' ){
      z[i] = 0;
      i++;
    }else{
      z[i] = 0;
    }
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

  zRes = db_text(0, "SELECT tuuid FROM xmark WHERE tname=%Q", zCommittish);
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
  if( gg.zFrom==0 && gg.zPrevCheckin!=0
   && fossil_strcmp(gg.zBranch, gg.zPrevBranch)==0
  ){
     gg.zFrom = gg.zPrevCheckin;
     gg.zPrevCheckin = 0;
  }
  if( gg.zFrom==0 ) return;
  rid = fast_uuid_to_rid(gg.zFrom);
  if( rid==0 ) return;
  p = manifest_get(rid, CFTYPE_MANIFEST, 0);
  if( p==0 ) return;
  manifest_file_rewind(p);
  while( (pOld = manifest_file_next(p, 0))!=0 ){
    pNew = import_add_file();
    pNew->zName = fossil_strdup(pOld->zName);
    pNew->isExe = pOld->zPerm && strstr(pOld->zPerm, "x")!=0;
    pNew->isLink = pOld->zPerm && strstr(pOld->zPerm, "l")!=0;
    pNew->zUuid = fossil_strdup(pOld->zUuid);
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
    if( strncmp(zName, z, nName)==0 && (z[nName]==0 || z[nName]=='/') ){
      *pI = i+1;
      return &gg.aFile[i];
    }
    i++;
  }
  return 0;
}

/*
** Dequote a fast-export filename.  Filenames are normally unquoted.  But
** if the contain some obscure special characters, quotes might be added.
*/
static void dequote_git_filename(char *zName){
  int n, i, j;
  if( zName==0 || zName[0]!='"' ) return;
  n = (int)strlen(zName);
  if( zName[n-1]!='"' ) return;
  for(i=0, j=1; j<n-1; j++){
    char c = zName[j];
    int x;
    if( c=='\\' ){
      if( j+3 <= n-1
       && zName[j+1]>='0' && zName[j+1]<='3'
       && zName[j+2]>='0' && zName[j+2]<='7'
       && zName[j+3]>='0' && zName[j+3]<='7'
       && (x = 64*(zName[j+1]-'0') + 8*(zName[j+2]-'0') + zName[j+3]-'0')!=0
      ){
        c = (unsigned char)x;
        j += 3;
      }else{
        c = zName[++j];
      }
    }
    zName[i++] = c;
  }
  zName[i] = 0;
}


static struct{
  const char *zMasterName;    /* Name of master branch */
} ggit;

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
    if( strncmp(zLine, "blob", 4)==0 ){
      gg.xFinish();
      gg.xFinish = finish_blob;
    }else
    if( strncmp(zLine, "commit ", 7)==0 ){
      const char *zRefName;
      gg.xFinish();
      gg.xFinish = finish_commit;
      trim_newline(&zLine[7]);
      zRefName = &zLine[7];

      /* The argument to the "commit" line might match either of these
      ** patterns:
      **
      **   (A)  refs/heads/BRANCHNAME
      **   (B)  refs/tags/TAGNAME
      **
      ** If pattern A is used, then the branchname used is as shown.
      ** Except, the "master" branch which is the default branch name in
      ** Git is changed to "trunk" which is the default name in Fossil.
      ** If the pattern is B, then the new commit should be on the same
      ** branch as its parent.  And, we might need to add the TAGNAME
      ** tag to the new commit.  However, if there are multiple instances
      ** of pattern B with the same TAGNAME, then only put the tag on the
      ** last commit that holds that tag.
      **
      ** None of the above is explained in the git-fast-export
      ** documentation.  We had to figure it out via trial and error.
      */
      for(i=5; i<strlen(zRefName) && zRefName[i]!='/'; i++){}
      gg.tagCommit = strncmp(&zRefName[5], "tags", 4)==0;  /* True for pattern B */
      if( zRefName[i+1]!=0 ) zRefName += i+1;
      if( fossil_strcmp(zRefName, "master")==0 ) zRefName = ggit.zMasterName;
      gg.zBranch = fossil_strdup(zRefName);
      gg.fromLoaded = 0;
    }else
    if( strncmp(zLine, "tag ", 4)==0 ){
      gg.xFinish();
      gg.xFinish = finish_tag;
      trim_newline(&zLine[4]);
      gg.zTag = fossil_strdup(&zLine[4]);
    }else
    if( strncmp(zLine, "reset ", 6)==0 ){
      gg.xFinish();
    }else
    if( strncmp(zLine, "checkpoint", 10)==0 ){
      gg.xFinish();
    }else
    if( strncmp(zLine, "feature", 7)==0 ){
      gg.xFinish();
    }else
    if( strncmp(zLine, "option", 6)==0 ){
      gg.xFinish();
    }else
    if( strncmp(zLine, "progress ", 9)==0 ){
      gg.xFinish();
      trim_newline(&zLine[9]);
      fossil_print("%s\n", &zLine[9]);
      fflush(stdout);
    }else
    if( strncmp(zLine, "data ", 5)==0 ){
      fossil_free(gg.aData); gg.aData = 0;
      gg.nData = atoi(&zLine[5]);
      if( gg.nData ){
        int got;
        gg.aData = fossil_malloc( gg.nData+1 );
        got = fread(gg.aData, 1, gg.nData, pIn);
        if( got!=gg.nData ){
          fossil_fatal("short read: got %d of %d bytes", got, gg.nData);
        }
        gg.aData[got] = '\0';
        if( gg.zComment==0 &&
            (gg.xFinish==finish_commit || gg.xFinish==finish_tag) ){
	  /* Strip trailing newline, it's appended to the comment. */
	  if( gg.aData[got-1] == '\n' )
	    gg.aData[got-1] = '\0';
          gg.zComment = gg.aData;
          gg.aData = 0;
          gg.nData = 0;
        }
      }
    }else
    if( strncmp(zLine, "author ", 7)==0 ){
      /* No-op */
    }else
    if( strncmp(zLine, "mark ", 5)==0 ){
      trim_newline(&zLine[5]);
      fossil_free(gg.zMark);
      gg.zMark = fossil_strdup(&zLine[5]);
    }else
    if( strncmp(zLine, "tagger ", 7)==0 || strncmp(zLine, "committer ",10)==0 ){
      sqlite3_int64 secSince1970;
      z = strchr(zLine, ' ');
      while( fossil_isspace(*z) ) z++;
      if( (zTo=strchr(z, '>'))==NULL ) goto malformed_line;
      *(++zTo) = '\0';
      /* Lookup user by contact info. */
      fossil_free(gg.zUser);
      gg.zUser = db_text(0, "SELECT login FROM user WHERE info=%Q", z);
      if( gg.zUser==NULL ){
        /* If there is no user with this contact info,
	 * then use the email address as the username. */
        if ( (z=strchr(z, '<'))==NULL ) goto malformed_line;
        z++;
        *(zTo-1) = '\0';
        gg.zUser = fossil_strdup(z);
      }
      secSince1970 = 0;
      for(zTo++; fossil_isdigit(*zTo); zTo++){
        secSince1970 = secSince1970*10 + *zTo - '0';
      }
      fossil_free(gg.zDate);
      gg.zDate = db_text(0, "SELECT datetime(%lld, 'unixepoch')", secSince1970);
      gg.zDate[10] = 'T';
    }else
    if( strncmp(zLine, "from ", 5)==0 ){
      trim_newline(&zLine[5]);
      fossil_free(gg.zFromMark);
      gg.zFromMark = fossil_strdup(&zLine[5]);
      fossil_free(gg.zFrom);
      gg.zFrom = resolve_committish(&zLine[5]);
    }else
    if( strncmp(zLine, "merge ", 6)==0 ){
      trim_newline(&zLine[6]);
      if( gg.nMerge>=gg.nMergeAlloc ){
        gg.nMergeAlloc = gg.nMergeAlloc*2 + 10;
        gg.azMerge = fossil_realloc(gg.azMerge, gg.nMergeAlloc*sizeof(char*));
      }
      gg.azMerge[gg.nMerge] = resolve_committish(&zLine[6]);
      if( gg.azMerge[gg.nMerge] ) gg.nMerge++;
    }else
    if( strncmp(zLine, "M ", 2)==0 ){
      import_prior_files();
      z = &zLine[2];
      zPerm = next_token(&z);
      zUuid = next_token(&z);
      zName = rest_of_line(&z);
      dequote_git_filename(zName);
      i = 0;
      pFile = import_find_file(zName, &i, gg.nFile);
      if( pFile==0 ){
        pFile = import_add_file();
        pFile->zName = fossil_strdup(zName);
      }
      pFile->isExe = (fossil_strcmp(zPerm, "100755")==0);
      pFile->isLink = (fossil_strcmp(zPerm, "120000")==0);
      fossil_free(pFile->zUuid);
      pFile->zUuid = resolve_committish(zUuid);
      pFile->isFrom = 0;
    }else
    if( strncmp(zLine, "D ", 2)==0 ){
      import_prior_files();
      z = &zLine[2];
      zName = rest_of_line(&z);
      dequote_git_filename(zName);
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
    if( strncmp(zLine, "C ", 2)==0 ){
      int nFrom;
      import_prior_files();
      z = &zLine[2];
      zFrom = next_token(&z);
      zTo = rest_of_line(&z);
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
          pNew->zName = fossil_strdup(pFile->zName);
        }
        pNew->isExe = pFile->isExe;
        pNew->isLink = pFile->isLink;
        pNew->zUuid = fossil_strdup(pFile->zUuid);
        pNew->isFrom = 0;
      }
    }else
    if( strncmp(zLine, "R ", 2)==0 ){
      int nFrom;
      import_prior_files();
      z = &zLine[2];
      zFrom = next_token(&z);
      zTo = rest_of_line(&z);
      i = 0;
      nFrom = strlen(zFrom);
      while( (pFile = import_find_file(zFrom, &i, gg.nFile))!=0 ){
        if( pFile->isFrom==0 ) continue;
        pNew = import_add_file();
        pFile = &gg.aFile[i-1];
        if( strlen(pFile->zName)>nFrom ){
          pNew->zName = mprintf("%s%s", zTo, pFile->zName[nFrom]);
        }else{
          pNew->zName = fossil_strdup(pFile->zName);
        }
        pNew->zPrior = pFile->zName;
        pNew->isExe = pFile->isExe;
        pNew->isLink = pFile->isLink;
        pNew->zUuid = pFile->zUuid;
        pNew->isFrom = 0;
        gg.nFile--;
        *pFile = *pNew;
        memset(pNew, 0, sizeof(*pNew));
      }
      fossil_fatal("cannot handle R records, use --full-tree");
    }else
    if( strncmp(zLine, "deleteall", 9)==0 ){
      gg.fromLoaded = 1;
    }else
    if( strncmp(zLine, "N ", 2)==0 ){
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

static struct{
  int rev;                    /* SVN revision number */
  char *zDate;                /* Date/time stamp */
  char *zUser;                /* User name */
  char *zComment;             /* Comment of a commit */
  const char *zTrunk;         /* Name of trunk folder in repo root */
  int lenTrunk;               /* String length of zTrunk */
  const char *zBranches;      /* Name of branches folder in repo root */
  int lenBranches;            /* String length of zBranches */
  const char *zTags;          /* Name of tags folder in repo root */
  int lenTags;                /* String length of zTags */
  Bag newBranches;            /* Branches that were created in this revision */
  int revFlag;                /* Add svn-rev-nn tags on every checkin */
  const char *zRevPre;        /* Prepended to revision tag names */
  const char *zRevSuf;        /* Appended to revision tag names */
  const char **azIgnTree;     /* NULL-terminated list of dirs to ignore */
} gsvn;
typedef struct {
  char *zKey;
  char *zVal;
} KeyVal;
typedef struct {
  KeyVal *aHeaders;
  int nHeaders;
  char *pRawProps;
  KeyVal *aProps;
  int nProps;
  Blob content;
  int contentFlag;
} SvnRecord;

#define svn_find_header(rec, zHeader) \
  svn_find_keyval((rec).aHeaders, (rec).nHeaders, (zHeader))
#define svn_find_prop(rec, zProp) \
  svn_find_keyval((rec).aProps, (rec).nProps, (zProp))
static char *svn_find_keyval(
  KeyVal *aKeyVal,
  int nKeyVal,
  const char *zKey
){
  int i;
  for(i=0; i<nKeyVal; i++){
    if( fossil_strcmp(aKeyVal[i].zKey, zKey)==0 ){
      return aKeyVal[i].zVal;
    }
  }
  return 0;
}

static void svn_free_rec(SvnRecord *rec){
  int i;
  for(i=0; i<rec->nHeaders; i++){
    fossil_free(rec->aHeaders[i].zKey);
  }
  fossil_free(rec->aHeaders);
  fossil_free(rec->aProps);
  fossil_free(rec->pRawProps);
  blob_reset(&rec->content);
}

static int svn_read_headers(FILE *pIn, SvnRecord *rec){
  char zLine[1000];

  rec->aHeaders = 0;
  rec->nHeaders = 0;
  while( fgets(zLine, sizeof(zLine), pIn) ){
    if( zLine[0]!='\n' ) break;
  }
  if( feof(pIn) ) return 0;
  do{
    char *sep;
    if( zLine[0]=='\n' ) break;
    rec->nHeaders += 1;
    rec->aHeaders = fossil_realloc(rec->aHeaders,
      sizeof(rec->aHeaders[0])*rec->nHeaders);
    rec->aHeaders[rec->nHeaders-1].zKey = mprintf("%s", zLine);
    sep = strchr(rec->aHeaders[rec->nHeaders-1].zKey, ':');
    if( !sep ){
      trim_newline(zLine);
      fossil_fatal("bad header line: [%s]", zLine);
    }
    *sep = 0;
    rec->aHeaders[rec->nHeaders-1].zVal = sep+1;
    sep = strchr(rec->aHeaders[rec->nHeaders-1].zVal, '\n');
    *sep = 0;
    while(rec->aHeaders[rec->nHeaders-1].zVal
       && fossil_isspace(*(rec->aHeaders[rec->nHeaders-1].zVal)) )
    {
      rec->aHeaders[rec->nHeaders-1].zVal++;
    }
  }while( fgets(zLine, sizeof(zLine), pIn) );
  if( zLine[0]!='\n' ){
      trim_newline(zLine);
      fossil_fatal("svn-dump data ended unexpectedly");
  }
  return 1;
}

static void svn_read_props(FILE *pIn, SvnRecord *rec){
  int nRawProps = 0;
  char *pRawProps;
  const char *zLen;

  rec->pRawProps = 0;
  rec->aProps = 0;
  rec->nProps = 0;
  zLen = svn_find_header(*rec, "Prop-content-length");
  if( zLen ){
    nRawProps = atoi(zLen);
  }
  if( nRawProps ){
    int got;
    char *zLine;
    rec->pRawProps = pRawProps = fossil_malloc( nRawProps );
    got = fread(rec->pRawProps, 1, nRawProps, pIn);
    if( got!=nRawProps ){
      fossil_fatal("short read: got %d of %d bytes", got, nRawProps);
    }
    if( memcmp(&pRawProps[got-10], "PROPS-END\n", 10)!=0 ){
      fossil_fatal("svn-dump data ended unexpectedly");
    }
    zLine = pRawProps;
    while( zLine<(pRawProps+nRawProps-10) ){
      char *eol;
      int propLen;
      if( zLine[0]=='D' ){
        propLen = atoi(&zLine[2]);
        eol = strchr(zLine, '\n');
        zLine = eol+1+propLen+1;
      }else{
        if( zLine[0]!='K' ){
          fossil_fatal("svn-dump data format broken");
        }
        propLen = atoi(&zLine[2]);
        eol = strchr(zLine, '\n');
        zLine = eol+1;
        eol = zLine+propLen;
        if( *eol!='\n' ){
          fossil_fatal("svn-dump data format broken");
        }
        *eol = 0;
        rec->nProps += 1;
        rec->aProps = fossil_realloc(rec->aProps,
          sizeof(rec->aProps[0])*rec->nProps);
        rec->aProps[rec->nProps-1].zKey = zLine;
        zLine = eol+1;
        if( zLine[0]!='V' ){
          fossil_fatal("svn-dump data format broken");
        }
        propLen = atoi(&zLine[2]);
        eol = strchr(zLine, '\n');
        zLine = eol+1;
        eol = zLine+propLen;
        if( *eol!='\n' ){
          fossil_fatal("svn-dump data format broken");
        }
        *eol = 0;
        rec->aProps[rec->nProps-1].zVal = zLine;
        zLine = eol+1;
      }
    }
  }
}

static int svn_read_rec(FILE *pIn, SvnRecord *rec){
  const char *zLen;
  int nLen = 0;
  if( svn_read_headers(pIn, rec)==0 ) return 0;
  svn_read_props(pIn, rec);
  blob_zero(&rec->content);
  zLen = svn_find_header(*rec, "Text-content-length");
  if( zLen ){
    rec->contentFlag = 1;
    nLen = atoi(zLen);
    blob_read_from_channel(&rec->content, pIn, nLen);
    if( blob_size(&rec->content)!=nLen ){
      fossil_fatal("short read: got %d of %d bytes",
        blob_size(&rec->content), nLen
      );
    }
  }else{
    rec->contentFlag = 0;
  }
  return 1;
}

/*
** Returns the UUID for the RID, or NULL if not found.
** The returned string is allocated via db_text() and must be
** free()d by the caller.
*/
char *rid_to_uuid(int rid){
  return db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
}

#define SVN_UNKNOWN   0
#define SVN_TRUNK     1
#define SVN_BRANCH    2
#define SVN_TAG       3

#define MAX_INT_32    (0x7FFFFFFFL)

static void svn_finish_revision(){
  Blob manifest;
  static Stmt getChanges;
  static Stmt getFiles;
  static Stmt setRid;
  Blob mcksum;

  blob_zero(&manifest);
  db_static_prepare(&getChanges, "SELECT tid, tname, ttype, tparent"
                                 " FROM xrevisions, xbranches ON (tbranch=tid)"
                                 " WHERE trid ISNULL");
  db_static_prepare(&getFiles, "SELECT tpath, tuuid, tperm FROM xfiles"
                               " WHERE tbranch=:branch ORDER BY tpath");
  db_prepare(&setRid, "UPDATE xrevisions SET trid=:rid"
                             " WHERE trev=%d AND tbranch=:branch", gsvn.rev);
  while( db_step(&getChanges)==SQLITE_ROW ){
    int branchId = db_column_int(&getChanges, 0);
    const char *zBranch = db_column_text(&getChanges, 1);
    int branchType = db_column_int(&getChanges, 2);
    int parentRid = db_column_int(&getChanges, 3);
    int mergeRid = parentRid;
    Manifest *pParentManifest = 0;
    ManifestFile *pParentFile = 0;
    int sameAsParent = 1;
    int parentBranch = 0;
    if( !bag_find(&gsvn.newBranches, branchId) ){
      parentRid = db_int(0, "SELECT trid, max(trev) FROM xrevisions"
                            " WHERE trev<%d AND tbranch=%d",
                         gsvn.rev, branchId);
    }
    if( parentRid>0 ){
      pParentManifest = manifest_get(parentRid, CFTYPE_MANIFEST, 0);
      if( pParentManifest ){
        pParentFile = manifest_file_next(pParentManifest, 0);
        parentBranch = db_int(0, "SELECT tbranch FROM xrevisions WHERE trid=%d",
                              parentRid);
        if( parentBranch!=branchId && branchType!=SVN_TAG ){
          sameAsParent = 0;
        }
      }
    }
    if( mergeRid<MAX_INT_32 ){
      if( gsvn.zComment ){
        blob_appendf(&manifest, "C %F\n", gsvn.zComment);
      }else{
        blob_append(&manifest, "C (no\\scomment)\n", 16);
      }
      blob_appendf(&manifest, "D %s\n", gsvn.zDate);
      db_bind_int(&getFiles, ":branch", branchId);
      while( db_step(&getFiles)==SQLITE_ROW ){
        const char *zFile = db_column_text(&getFiles, 0);
        const char *zUuid = db_column_text(&getFiles, 1);
        const char *zPerm = db_column_text(&getFiles, 2);
        if( zPerm ){
          blob_appendf(&manifest, "F %F %s %s\n", zFile, zUuid, zPerm);
        }else{
          blob_appendf(&manifest, "F %F %s\n", zFile, zUuid);
        }
        if( sameAsParent ){
          if( !pParentFile
           || fossil_strcmp(pParentFile->zName,zFile)!=0
           || fossil_strcmp(pParentFile->zUuid,zUuid)!=0
           || fossil_strcmp(pParentFile->zPerm,zPerm)!=0
          ){
            sameAsParent = 0;
          }else{
            pParentFile = manifest_file_next(pParentManifest, 0);
          }
        }
      }
      if( pParentFile ){
        sameAsParent = 0;
      }
      db_reset(&getFiles);
      if( !sameAsParent ){
        if( parentRid>0 ){
          char *zParentUuid = rid_to_uuid(parentRid);
          if( parentRid==mergeRid || mergeRid==0){
            char *zParentBranch =
              db_text(0, "SELECT tname FROM xbranches WHERE tid=%d",
                      parentBranch
              );
            blob_appendf(&manifest, "P %s\n", zParentUuid);
            blob_appendf(&manifest, "T *branch * %F%F%F\n", gimport.zBranchPre,
                zBranch, gimport.zBranchSuf);
            blob_appendf(&manifest, "T *sym-%F%F%F *\n", gimport.zBranchPre,
                zBranch, gimport.zBranchSuf);
            if( gsvn.revFlag ){
              blob_appendf(&manifest, "T +sym-%Fr%d%F *\n", gimport.zTagPre,
                  gsvn.rev, gimport.zTagSuf);
            }
            blob_appendf(&manifest, "T -sym-%F%F%F *\n", gimport.zBranchPre,
                zParentBranch, gimport.zBranchSuf);
            fossil_free(zParentBranch);
          }else{
            char *zMergeUuid = rid_to_uuid(mergeRid);
            blob_appendf(&manifest, "P %s %s\n", zParentUuid, zMergeUuid);
            if( gsvn.revFlag ){
              blob_appendf(&manifest, "T +sym-%F%d%F *\n", gsvn.zRevPre,
                  gsvn.rev, gsvn.zRevSuf);
            }
            fossil_free(zMergeUuid);
          }
          fossil_free(zParentUuid);
        }else{
          blob_appendf(&manifest, "T *branch * %F%F%F\n",
              gimport.zBranchPre, zBranch, gimport.zBranchSuf);
          blob_appendf(&manifest, "T *sym-%F%F%F *\n", gimport.zBranchPre,
              zBranch, gimport.zBranchSuf);
          if( gsvn.revFlag ){
            blob_appendf(&manifest, "T +sym-%F%d%F *\n", gsvn.zRevPre, gsvn.rev,
                gsvn.zRevSuf);
          }
        }
      }else if( branchType==SVN_TAG ){
        char *zParentUuid = rid_to_uuid(parentRid);
        blob_reset(&manifest);
        blob_appendf(&manifest, "D %s\n", gsvn.zDate);
        blob_appendf(&manifest, "T +sym-%F%F%F %s\n", gimport.zTagPre, zBranch,
            gimport.zTagSuf, zParentUuid);
        fossil_free(zParentUuid);
      }
    }else{
      char *zParentUuid = rid_to_uuid(parentRid);
      blob_appendf(&manifest, "D %s\n", gsvn.zDate);
      if( branchType!=SVN_TAG ){
        blob_appendf(&manifest, "T +closed %s\n", zParentUuid);
      }else{
        blob_appendf(&manifest, "T -sym-%F%F%F %s\n", gimport.zBranchPre,
            zBranch, gimport.zBranchSuf, zParentUuid);
      }
      fossil_free(zParentUuid);
    }
    if( gsvn.zUser ){
      blob_appendf(&manifest, "U %F\n", gsvn.zUser);
    }else{
      const char *zUserOvrd = find_option("user-override",0,1);
      blob_appendf(&manifest, "U %F\n", zUserOvrd ? zUserOvrd : login_name());
    }
    md5sum_blob(&manifest, &mcksum);
    blob_appendf(&manifest, "Z %b\n", &mcksum);
    blob_reset(&mcksum);
    if( !sameAsParent ){
      int rid = content_put(&manifest);
      db_bind_int(&setRid, ":branch", branchId);
      db_bind_int(&setRid, ":rid", rid);
      db_step(&setRid);
      db_reset(&setRid);
    }else if( branchType==SVN_TAG ){
      content_put(&manifest);
      db_bind_int(&setRid, ":branch", branchId);
      db_bind_int(&setRid, ":rid", parentRid);
      db_step(&setRid);
      db_reset(&setRid);
    }else if( mergeRid==MAX_INT_32 ){
      content_put(&manifest);
      db_multi_exec("DELETE FROM xrevisions WHERE tbranch=%d AND trev=%d",
                    branchId, gsvn.rev);
    }else{
      db_multi_exec("DELETE FROM xrevisions WHERE tbranch=%d AND trev=%d",
                    branchId, gsvn.rev);
    }
    blob_reset(&manifest);
    manifest_destroy(pParentManifest);
  }
  db_reset(&getChanges);
  db_finalize(&setRid);
}

static u64 svn_get_varint(const char **pz){
  unsigned int v = 0;
  do{
    v = (v<<7) | ((*pz)[0]&0x7f);
  }while( (*pz)++[0]&0x80 );
  return v;
}

static void svn_apply_svndiff(Blob *pDiff, Blob *pSrc, Blob *pOut){
  const char *zDiff = blob_buffer(pDiff);
  char *zOut;
  if( blob_size(pDiff)<4 || memcmp(zDiff, "SVN", 4)!=0 ){
    fossil_fatal("Invalid svndiff0 format");
  }
  zDiff += 4;
  blob_zero(pOut);
  while( zDiff<(blob_buffer(pDiff)+blob_size(pDiff)) ){
    u64 lenOut, lenInst, lenData, lenOld;
    const char *zInst;
    const char *zData;

    u64 offSrc = svn_get_varint(&zDiff);
    /*lenSrc =*/ svn_get_varint(&zDiff);
    lenOut = svn_get_varint(&zDiff);
    lenInst = svn_get_varint(&zDiff);
    lenData = svn_get_varint(&zDiff);
    zInst = zDiff;
    zData = zInst+lenInst;
    lenOld = blob_size(pOut);
    blob_resize(pOut, lenOut+lenOld);
    zOut = blob_buffer(pOut)+lenOld;
    while( zDiff<zInst+lenInst ){
      u64 lenCpy = (*zDiff)&0x3f;
      const char *zCpy;
      switch( (*zDiff)&0xC0 ){
        case 0x00: zCpy = blob_buffer(pSrc)+offSrc; break;
        case 0x40: zCpy = blob_buffer(pOut); break;
        case 0x80: zCpy = zData; break;
        default: fossil_fatal("Invalid svndiff0 instruction");
      }
      zDiff++;
      if( lenCpy==0 ){
        lenCpy = svn_get_varint(&zDiff);
      }
      if( zCpy!=zData ){
        zCpy += svn_get_varint(&zDiff);
      }else{
        zData += lenCpy;
      }
      while( lenCpy-- > 0 ){
        *zOut++ = *zCpy++;
      }
    }
    zDiff += lenData;
  }
}

/*
** Extract the branch or tag that the given path is on. Return the branch ID.
** Return 0 if not a branch, tag, or trunk, or if ignored by --ignore-tree.
*/
static int svn_parse_path(char *zPath, char **zFile, int *type){
  char *zBranch = 0;
  int branchId = 0;
  if( gsvn.azIgnTree ){
    const char **pzIgnTree;
    unsigned nPath = strlen(zPath);
    for( pzIgnTree = gsvn.azIgnTree; *pzIgnTree; ++pzIgnTree ){
      const char *zIgn = *pzIgnTree;
      int nIgn = strlen(zIgn);
      if( strncmp(zPath, zIgn, nIgn) == 0
       && ( nPath == nIgn || (nPath > nIgn && zPath[nIgn] == '/')) ){
        return 0;
      }
    }
  }
  *type = SVN_UNKNOWN;
  *zFile = 0;
  if( gsvn.lenTrunk==0 ){
    zBranch = "trunk";
    *zFile = zPath;
    *type = SVN_TRUNK;
  }else
  if( strncmp(zPath, gsvn.zTrunk, gsvn.lenTrunk-1)==0 ){
    if( zPath[gsvn.lenTrunk-1]=='/' || zPath[gsvn.lenTrunk-1]==0 ){
      zBranch = "trunk";
      *zFile = zPath+gsvn.lenTrunk;
      *type = SVN_TRUNK;
    }else{
      zBranch = 0;
      *type = SVN_UNKNOWN;
    }
  }else{
    if( strncmp(zPath, gsvn.zBranches, gsvn.lenBranches)==0 ){
      *zFile = zBranch = zPath+gsvn.lenBranches;
      *type = SVN_BRANCH;
    }else
    if( strncmp(zPath, gsvn.zTags, gsvn.lenTags)==0 ){
      *zFile = zBranch = zPath+gsvn.lenTags;
      *type = SVN_TAG;
    }else{ /* Not a branch, tag or trunk */
      return 0;
    }
    while( **zFile && **zFile!='/' ){ (*zFile)++; }
    if( **zFile ){
      **zFile = '\0';
      (*zFile)++;
    }
  }
  if( *type!=SVN_UNKNOWN ){
    branchId = db_int(0,
                      "SELECT tid FROM xbranches WHERE tname=%Q AND ttype=%d",
                      zBranch, *type);
    if( branchId==0 ){
      db_multi_exec("INSERT INTO xbranches (tname, ttype) VALUES(%Q, %d)",
                    zBranch, *type);
      branchId = db_last_insert_rowid();
    }
  }
  return branchId;
}

/*
** Insert content of corresponding content blob into the database.
** If content is identified as a symbolic link, then trailing
** "link " characters are removed from content.
**
** content is considered to be a symlink if zPerm contains at least
** one "l" character.
*/
static int svn_handle_symlinks(const char *perms, Blob *content){
  Blob link_blob;
  if( perms && strstr(perms, "l")!=0 ){
    if( blob_size(content)>5 ){
      /* Skip trailing 'link ' characters */
      blob_seek(content, 5, BLOB_SEEK_SET);
      blob_tail(content, &link_blob);
      return content_put(&link_blob);
    }else{
      fossil_fatal("Too short symbolic link path");
    }
  }else{
    return content_put(content);
  }
}

/*
** Read the svn-dump format from pIn and insert the corresponding
** content into the database.
*/
static void svn_dump_import(FILE *pIn){
  SvnRecord rec;
  int ver;
  char *zTemp;
  const char *zUuid;
  Stmt addFile;
  Stmt delPath;
  Stmt addRev;
  Stmt cpyPath;
  Stmt cpyRoot;
  Stmt revSrc;

  /* version */
  if( svn_read_rec(pIn, &rec)
   && (zTemp = svn_find_header(rec, "SVN-fs-dump-format-version")) ){
    ver = atoi(zTemp);
    if( ver!=2 && ver!=3 ){
      fossil_fatal("Unknown svn-dump format version: %d", ver);
    }
  }else{
    fossil_fatal("Input is not an svn-dump!");
  }
  svn_free_rec(&rec);
  /* UUID */
  if( !svn_read_rec(pIn, &rec) || !(zUuid = svn_find_header(rec, "UUID")) ){
    /* Removed the following line since UUID is not actually used
     fossil_fatal("Missing UUID!"); */
  }
  svn_free_rec(&rec);

  /* content */
  db_prepare(&addFile,
    "INSERT INTO xfiles (tpath, tbranch, tuuid, tperm)"
    " VALUES(:path, :branch, (SELECT uuid FROM blob WHERE rid=:rid), :perm)"
  );
  db_prepare(&delPath,
    "DELETE FROM xfiles"
    " WHERE (tpath=:path OR (tpath>:path||'/' AND tpath<:path||'0'))"
    "   AND tbranch=:branch"
  );
  db_prepare(&addRev,
    "INSERT OR IGNORE INTO xrevisions (trev, tbranch) VALUES(:rev, :branch)"
  );
  db_prepare(&cpyPath,
    "INSERT INTO xfiles (tpath, tbranch, tuuid, tperm)"
    " SELECT :path||:sep||substr(filename, length(:srcpath)+2), :branch, uuid, perm"
    " FROM xfoci"
    " WHERE checkinID=:rid"
    "   AND filename>:srcpath||'/'"
    "   AND filename<:srcpath||'0'"
  );
  db_prepare(&cpyRoot,
    "INSERT INTO xfiles (tpath, tbranch, tuuid, tperm)"
    " SELECT :path||:sep||filename, :branch, uuid, perm"
    " FROM xfoci"
    " WHERE checkinID=:rid"
  );
  db_prepare(&revSrc,
    "UPDATE xrevisions SET tparent=:parent"
    " WHERE trev=:rev AND tbranch=:branch AND tparent<:parent"
  );
  gsvn.rev = -1;
  bag_init(&gsvn.newBranches);
  while( svn_read_rec(pIn, &rec) ){
    if( (zTemp = svn_find_header(rec, "Revision-number")) ){ /* revision node */
      /* finish previous revision */
      char *zDate = NULL;
      if( gsvn.rev>=0 ){
        svn_finish_revision();
        fossil_free(gsvn.zUser);
        fossil_free(gsvn.zComment);
        fossil_free(gsvn.zDate);
        bag_clear(&gsvn.newBranches);
      }
      /* start new revision */
      gsvn.rev = atoi(zTemp);
      gsvn.zUser = mprintf("%s", svn_find_prop(rec, "svn:author"));
      gsvn.zComment = mprintf("%s", svn_find_prop(rec, "svn:log"));
      zDate = svn_find_prop(rec, "svn:date");
      if( zDate ){
        gsvn.zDate = date_in_standard_format(zDate);
      }else{
        gsvn.zDate = date_in_standard_format("now");
      }
      db_bind_int(&addRev, ":rev", gsvn.rev);
      fossil_print("\rImporting SVN revision: %d", gsvn.rev);
    }else
    if( (zTemp = svn_find_header(rec, "Node-path")) ){ /* file/dir node */
      char *zFile;
      int branchType;
      int branchId = svn_parse_path(zTemp, &zFile, &branchType);
      char *zAction = svn_find_header(rec, "Node-action");
      char *zKind = svn_find_header(rec, "Node-kind");
      char *zPerm = svn_find_prop(rec, "svn:executable") ? "x" : 0;
      int deltaFlag = 0;
      int srcRev = 0;

      if ( zPerm==0 ){
        zPerm = svn_find_prop(rec, "svn:special") ? "l" : 0;
      }
      if( branchId==0 ){
        svn_free_rec(&rec);
        continue;
      }
      if( (zTemp = svn_find_header(rec, "Text-delta")) ){
        deltaFlag = strncmp(zTemp, "true", 4)==0;
      }
      if( strncmp(zAction, "delete", 6)==0
       || strncmp(zAction, "replace", 7)==0 )
      {
        db_bind_int(&addRev, ":branch", branchId);
        db_step(&addRev);
        db_reset(&addRev);
        if( zFile[0]!=0 ){
          db_bind_text(&delPath, ":path", zFile);
          db_bind_int(&delPath, ":branch", branchId);
          db_step(&delPath);
          db_reset(&delPath);
        }else{
          db_multi_exec("DELETE FROM xfiles WHERE tbranch=%d", branchId);
          db_bind_int(&revSrc, ":parent", MAX_INT_32);
          db_bind_int(&revSrc, ":rev", gsvn.rev);
          db_bind_int(&revSrc, ":branch", branchId);
          db_step(&revSrc);
          db_reset(&revSrc);
        }
      } /* no 'else' here since 'replace' does both a 'delete' and an 'add' */
      if( strncmp(zAction, "add", 3)==0
       || strncmp(zAction, "replace", 7)==0 )
      {
        char *zSrcPath = svn_find_header(rec, "Node-copyfrom-path");
        char *zSrcFile;
        int srcRid = 0;
        if( zSrcPath ){
          int srcBranch;
          zTemp = svn_find_header(rec, "Node-copyfrom-rev");
          if( zTemp ){
            srcRev = atoi(zTemp);
          }else{
            fossil_fatal("Missing copyfrom-rev");
          }
          srcBranch = svn_parse_path(zSrcPath, &zSrcFile, &branchType);
          if( srcBranch==0 ){
            fossil_fatal("Copy from path outside the import paths");
          }
          srcRid = db_int(0, "SELECT trid, max(trev) FROM xrevisions"
                             " WHERE trev<=%d AND tbranch=%d",
                          srcRev, srcBranch);
          if( srcRid>0 && srcBranch!=branchId ){
            db_bind_int(&addRev, ":branch", branchId);
            db_step(&addRev);
            db_reset(&addRev);
            db_bind_int(&revSrc, ":parent", srcRid);
            db_bind_int(&revSrc, ":rev", gsvn.rev);
            db_bind_int(&revSrc, ":branch", branchId);
            db_step(&revSrc);
            db_reset(&revSrc);
          }
        }
        if( zKind==0 ){
          fossil_fatal("Missing Node-kind");
        }else if( strncmp(zKind, "dir", 3)==0 ){
          if( zSrcPath ){
            if( srcRid>0 ){
              if( zSrcFile[0]==0 ){
                db_bind_text(&cpyRoot, ":path", zFile);
                if( zFile[0]!=0 ){
                  db_bind_text(&cpyRoot, ":sep", "/");
                }else{
                  db_bind_text(&cpyRoot, ":sep", "");
                }
                db_bind_int(&cpyRoot, ":branch", branchId);
                db_bind_int(&cpyRoot, ":rid", srcRid);
                db_step(&cpyRoot);
                db_reset(&cpyRoot);
              }else{
                db_bind_text(&cpyPath, ":path", zFile);
                if( zFile[0]!=0 ){
                  db_bind_text(&cpyPath, ":sep", "/");
                }else{
                  db_bind_text(&cpyPath, ":sep", "");
                }
                db_bind_int(&cpyPath, ":branch", branchId);
                db_bind_text(&cpyPath, ":srcpath", zSrcFile);
                db_bind_int(&cpyPath, ":rid", srcRid);
                db_step(&cpyPath);
                db_reset(&cpyPath);
              }
            }
          }
          if( zFile[0]==0 ){
            bag_insert(&gsvn.newBranches, branchId);
          }
        }else{
          int rid = 0;
          if( zSrcPath ){
            rid = db_int(0, "SELECT rid FROM blob WHERE uuid=("
                            " SELECT uuid FROM xfoci"
                            "  WHERE checkinID=%d AND filename=%Q"
                            ")",
                         srcRid, zSrcFile);
          }
          if( deltaFlag ){
            Blob deltaSrc;
            Blob target;
            if( rid!=0 ){
              content_get(rid, &deltaSrc);
            }else{
              blob_zero(&deltaSrc);
            }
            svn_apply_svndiff(&rec.content, &deltaSrc, &target);
            rid = svn_handle_symlinks(zPerm, &target);
          }else if( rec.contentFlag ){
            rid = svn_handle_symlinks(zPerm, &rec.content);
          }else if( zSrcPath ){
            if ( zPerm==0 ){
              zPerm = db_text(0, "SELECT tperm FROM xfiles"
                                 " WHERE tpath=%Q AND tbranch=%d"
                                 "", zSrcPath, branchId);
            }
          }
          db_bind_text(&addFile, ":path", zFile);
          db_bind_int(&addFile, ":branch", branchId);
          db_bind_int(&addFile, ":rid", rid);
          db_bind_text(&addFile, ":perm", zPerm);
          db_step(&addFile);
          db_reset(&addFile);
          db_bind_int(&addRev, ":branch", branchId);
          db_step(&addRev);
          db_reset(&addRev);
        }
      }else
      if( strncmp(zAction, "change", 6)==0 ){
        int rid = 0;
        if( zKind==0 ){
          fossil_fatal("Missing Node-kind");
        }
        if( rec.contentFlag && strncmp(zKind, "dir", 3)!=0 ){
          if ( zPerm==0 ){
            zPerm = db_text(0, "SELECT tperm FROM xfiles"
                               " WHERE tpath=%Q AND tbranch=%d"
                               "", zFile, branchId);
          }

          if( deltaFlag ){
            Blob deltaSrc;
            Blob target;
            rid = db_int(0, "SELECT rid FROM blob WHERE uuid=("
                            " SELECT tuuid FROM xfiles"
                            "  WHERE tpath=%Q AND tbranch=%d"
                            ")", zFile, branchId);
            content_get(rid, &deltaSrc);
            svn_apply_svndiff(&rec.content, &deltaSrc, &target);
            rid = svn_handle_symlinks(zPerm, &target);
          }else{
            rid = svn_handle_symlinks(zPerm, &rec.content);
          }
          db_bind_text(&addFile, ":path", zFile);
          db_bind_int(&addFile, ":branch", branchId);
          db_bind_int(&addFile, ":rid", rid);
          db_bind_text(&addFile, ":perm", zPerm);
          db_step(&addFile);
          db_reset(&addFile);
          db_bind_int(&addRev, ":branch", branchId);
          db_step(&addRev);
          db_reset(&addRev);
        }
      }else
      if( strncmp(zAction, "delete", 6)!=0 ){ /* already did this one above */
        fossil_fatal("Unknown Node-action");
      }
    }else{
      fossil_fatal("Unknown record type");
    }
    svn_free_rec(&rec);
  }
  svn_finish_revision();
  fossil_free(gsvn.zUser);
  fossil_free(gsvn.zComment);
  fossil_free(gsvn.zDate);
  db_finalize(&addFile);
  db_finalize(&delPath);
  db_finalize(&addRev);
  db_finalize(&cpyPath);
  db_finalize(&cpyRoot);
  db_finalize(&revSrc);
  fossil_print(" Done!\n");
}

/*
** COMMAND: import
**
** Usage: %fossil import ?--git? ?OPTIONS? NEW-REPOSITORY ?INPUT-FILE?
**    or: %fossil import --svn ?OPTIONS? NEW-REPOSITORY ?INPUT-FILE?
**
** Read interchange format generated by another VCS and use it to
** construct a new Fossil repository named by the NEW-REPOSITORY
** argument.  If no input file is supplied the interchange format
** data is read from standard input.
**
** The following formats are currently understood by this command
**
**   --git        Import from the git-fast-export file format (default)
**                Options:
**                  --import-marks  FILE Restore marks table from FILE
**                  --export-marks  FILE Save marks table to FILE
**                  --rename-master NAME Renames the master branch to NAME
**
**   --svn        Import from the svnadmin-dump file format.  The default
**                behaviour (unless overridden by --flat) is to treat 3
**                folders in the SVN root as special, following the
**                common layout of SVN repositories.  These are (by
**                default) trunk/, branches/ and tags/.  The SVN --deltas
**                format is supported but not required.
**                Options:
**                  --trunk FOLDER     Name of trunk folder
**                  --branches FOLDER  Name of branches folder
**                  --tags FOLDER      Name of tags folder
**                  --base PATH        Path to project root in repository
**                  --flat             The whole dump is a single branch
**                  --rev-tags         Tag each revision, implied by -i
**                  --no-rev-tags      Disables tagging effect of -i
**                  --rename-rev PAT   Rev tag names, default "svn-rev-%"
**                  --ignore-tree DIR  Ignores subtree rooted at DIR
**
** Common Options:
**   -i|--incremental     allow importing into an existing repository
**   -f|--force           overwrite repository if already exists
**   -q|--quiet           omit progress output
**   --no-rebuild         skip the "rebuilding metadata" step
**   --no-vacuum          skip the final VACUUM of the database file
**   --rename-trunk NAME  use NAME as name of imported trunk branch
**   --rename-branch PAT  rename all branch names using PAT pattern
**   --rename-tag PAT     rename all tag names using PAT pattern
**
** The --incremental option allows an existing repository to be extended
** with new content.  The --rename-* options may be useful to avoid name
** conflicts when using the --incremental option.
**
** The argument to --rename-* contains one "%" character to be replaced
** with the original name.  For example, "--rename-tag svn-%-tag" renames
** the tag called "release" to "svn-release-tag".
**
** --ignore-tree is useful for importing Subversion repositories which
** move branches to subdirectories of "branches/deleted" instead of
** deleting them.  It can be supplied multiple times if necessary.
**
** See also: export
*/
void import_cmd(void){
  char *zPassword;
  FILE *pIn;
  Stmt q;
  int forceFlag = find_option("force", "f", 0)!=0;
  int svnFlag = find_option("svn", 0, 0)!=0;
  int gitFlag = find_option("git", 0, 0)!=0;
  int omitRebuild = find_option("no-rebuild",0,0)!=0;
  int omitVacuum = find_option("no-vacuum",0,0)!=0;

  /* Options common to all input formats */
  int incrFlag = find_option("incremental", "i", 0)!=0;

  /* Options for --svn only */
  const char *zBase = "";
  int flatFlag = 0;

  /* Options for --git only */
  const char *markfile_in = 0;
  const char *markfile_out = 0;

  /* Interpret --rename-* options.  Use a table to avoid code duplication. */
  const struct {
    const char *zOpt, **varPre, *zDefaultPre, **varSuf, *zDefaultSuf;
    int format; /* 1=git, 2=svn, 3=any */
  } renOpts[] = {
    {"rename-branch", &gimport.zBranchPre,   "", &gimport.zBranchSuf, "", 3},
    {"rename-tag"   , &gimport.zTagPre   ,   "", &gimport.zTagSuf   , "", 3},
    {"rename-rev"   , &gsvn.zRevPre, "svn-rev-", &gsvn.zRevSuf      , "", 2},
  }, *renOpt = renOpts;
  int i;
  for( i = 0; i < count(renOpts); ++i, ++renOpt ){
    if( 1 << svnFlag & renOpt->format ){
      const char *zArgument = find_option(renOpt->zOpt, 0, 1);
      if( zArgument ){
         const char *sep = strchr(zArgument, '%');
         if( !sep ){
           fossil_fatal("missing '%%' in argument to --%s", renOpt->zOpt);
         }else if( strchr(sep + 1, '%') ){
           fossil_fatal("multiple '%%' in argument to --%s", renOpt->zOpt);
         }
         *renOpt->varPre = fossil_malloc(sep - zArgument + 1);
         memcpy((char *)*renOpt->varPre, zArgument, sep - zArgument);
         ((char *)*renOpt->varPre)[sep - zArgument] = 0;
         *renOpt->varSuf = sep + 1;
       }else{
         *renOpt->varPre = renOpt->zDefaultPre;
         *renOpt->varSuf = renOpt->zDefaultSuf;
       }
    }
  }
  if( !(gimport.zTrunkName = find_option("rename-trunk", 0, 1)) ){
    gimport.zTrunkName = "trunk";
  }

  if( svnFlag ){
    /* Get --svn related options here, so verify_all_options() fails when
     * svn-only options are specified with --git
     */
    const char *zIgnTree;
    unsigned nIgnTree = 0;
    while( (zIgnTree = find_option("ignore-tree", 0, 1)) ){
      if ( *zIgnTree ){
        gsvn.azIgnTree = fossil_realloc((void *)gsvn.azIgnTree,
            sizeof(*gsvn.azIgnTree) * (nIgnTree + 2));
        gsvn.azIgnTree[nIgnTree++] = zIgnTree;
        gsvn.azIgnTree[nIgnTree] = 0;
      }
    }
    zBase = find_option("base", 0, 1);
    flatFlag = find_option("flat", 0, 0)!=0;
    gsvn.zTrunk = find_option("trunk", 0, 1);
    gsvn.zBranches = find_option("branches", 0, 1);
    gsvn.zTags = find_option("tags", 0, 1);
    gsvn.revFlag = find_option("rev-tags", 0, 0)
                || (incrFlag && !find_option("no-rev-tags", 0, 0));
  }else if( gitFlag ){
    markfile_in = find_option("import-marks", 0, 1);
    markfile_out = find_option("export-marks", 0, 1);
    if( !(ggit.zMasterName = find_option("rename-master", 0, 1)) ){
      ggit.zMasterName = "master";
    }
  }
  verify_all_options();

  if( g.argc!=3 && g.argc!=4 ){
    usage("--git|--svn ?OPTIONS? NEW-REPOSITORY ?INPUT-FILE?");
  }
  if( g.argc==4 ){
    pIn = fossil_fopen(g.argv[3], "rb");
    if( pIn==0 ) fossil_fatal("cannot open input file \"%s\"", g.argv[3]);
  }else{
    pIn = stdin;
    fossil_binary_mode(pIn);
  }
  if( !incrFlag ){
    if( forceFlag ) file_delete(g.argv[2]);
    db_create_repository(g.argv[2]);
  }
  db_open_repository(g.argv[2]);
  db_open_config(0, 0);

  db_begin_transaction();
  if( !incrFlag ){
    db_initial_setup(0, 0, 0);
    db_set("main-branch", gimport.zTrunkName, 0);
  }

  if( svnFlag ){
    db_multi_exec(
       "CREATE TEMP TABLE xrevisions("
       " trev INTEGER, tbranch INT, trid INT, tparent INT DEFAULT 0,"
       " UNIQUE(tbranch, trev)"
       ");"
       "CREATE INDEX temp.i_xrevisions ON xrevisions(trid);"
       "CREATE TEMP TABLE xfiles("
       " tpath TEXT, tbranch INT, tuuid TEXT, tperm TEXT,"
       " UNIQUE (tbranch, tpath) ON CONFLICT REPLACE"
       ");"
       "CREATE TEMP TABLE xbranches("
       " tid INTEGER PRIMARY KEY, tname TEXT, ttype INT,"
       " UNIQUE(tname, ttype)"
       ");"
       "CREATE VIRTUAL TABLE temp.xfoci USING files_of_checkin;"
    );
    if( zBase==0 ){ zBase = ""; }
    if( strlen(zBase)>0 ){
      if( zBase[strlen(zBase)-1]!='/' ){
        zBase = mprintf("%s/", zBase);
      }
    }
    if( flatFlag ){
      gsvn.zTrunk = zBase;
      gsvn.zBranches = 0;
      gsvn.zTags = 0;
      gsvn.lenTrunk = strlen(zBase);
      gsvn.lenBranches = 0;
      gsvn.lenTags = 0;
    }else{
      if( gsvn.zTrunk==0 ){ gsvn.zTrunk = "trunk/"; }
      if( gsvn.zBranches==0 ){ gsvn.zBranches = "branches/"; }
      if( gsvn.zTags==0 ){ gsvn.zTags = "tags/"; }
      gsvn.zTrunk = mprintf("%s%s", zBase, gsvn.zTrunk);
      gsvn.zBranches = mprintf("%s%s", zBase, gsvn.zBranches);
      gsvn.zTags = mprintf("%s%s", zBase, gsvn.zTags);
      gsvn.lenTrunk = strlen(gsvn.zTrunk);
      gsvn.lenBranches = strlen(gsvn.zBranches);
      gsvn.lenTags = strlen(gsvn.zTags);
      if( gsvn.zTrunk[gsvn.lenTrunk-1]!='/' ){
        gsvn.zTrunk = mprintf("%s/", gsvn.zTrunk);
        gsvn.lenTrunk++;
      }
      if( gsvn.zBranches[gsvn.lenBranches-1]!='/' ){
        gsvn.zBranches = mprintf("%s/", gsvn.zBranches);
        gsvn.lenBranches++;
      }
      if( gsvn.zTags[gsvn.lenTags-1]!='/' ){
        gsvn.zTags = mprintf("%s/", gsvn.zTags);
        gsvn.lenTags++;
      }
    }
    svn_dump_import(pIn);
  }else{
    Bag blobs, vers;
    bag_init(&blobs);
    bag_init(&vers);
    /* The following temp-tables are used to hold information needed for
    ** the import.
    **
    ** The XMARK table provides a mapping from fast-import "marks" and symbols
    ** into artifact ids (UUIDs - the 40-byte hex SHA1 hash of artifacts).
    ** Given any valid fast-import symbol, the corresponding fossil rid and
    ** uuid can found by searching against the xmark.tname field.
    **
    ** The XBRANCH table maps commit marks and symbols into the branch those
    ** commits belong to.  If xbranch.tname is a fast-import symbol for a
    ** check-in then xbranch.brnm is the branch that check-in is part of.
    **
    ** The XTAG table records information about tags that need to be applied
    ** to various branches after the import finishes.  The xtag.tcontent field
    ** contains the text of an artifact that will add a tag to a check-in.
    ** The git-fast-export file format might specify the same tag multiple
    ** times but only the last tag should be used.  And we do not know which
    ** occurrence of the tag is the last until the import finishes.
    */
    db_multi_exec(
       "CREATE TEMP TABLE xmark(tname TEXT UNIQUE, trid INT, tuuid TEXT);"
       "CREATE INDEX temp.i_xmark ON xmark(trid);"
       "CREATE TEMP TABLE xbranch(tname TEXT UNIQUE, brnm TEXT);"
       "CREATE TEMP TABLE xtag(tname TEXT UNIQUE, tcontent TEXT);"
    );

    if( markfile_in ){
      FILE *f = fossil_fopen(markfile_in, "r");
      if( !f ){
        fossil_fatal("cannot open %s for reading", markfile_in);
      }
      if( import_marks(f, &blobs, NULL, NULL)<0 ){
        fossil_fatal("error importing marks from file: %s", markfile_in);
      }
      fclose(f);
    }

    manifest_crosslink_begin();
    git_fast_import(pIn);
    db_prepare(&q, "SELECT tcontent FROM xtag");
    while( db_step(&q)==SQLITE_ROW ){
      Blob record;
      db_ephemeral_blob(&q, 0, &record);
      fast_insert_content(&record, 0, 0, 1);
      import_reset(0);
    }
    db_finalize(&q);
    if( markfile_out ){
      int rid;
      Stmt q_marks;
      FILE *f;
      db_prepare(&q_marks, "SELECT DISTINCT trid FROM xmark");
      while( db_step(&q_marks)==SQLITE_ROW ){
        rid = db_column_int(&q_marks, 0);
        if( db_int(0, "SELECT count(objid) FROM event"
                      " WHERE objid=%d AND type='ci'", rid)==0 ){
          /* Blob marks exported by git aren't saved between runs, so they need
          ** to be left free for git to re-use in the future.
          */
        }else{
          bag_insert(&vers, rid);
        }
      }
      db_finalize(&q_marks);
      f = fossil_fopen(markfile_out, "w");
      if( !f ){
        fossil_fatal("cannot open %s for writing", markfile_out);
      }
      export_marks(f, &blobs, &vers);
      fclose(f);
      bag_clear(&blobs);
      bag_clear(&vers);
    }
    manifest_crosslink_end(MC_NONE);
  }

  verify_cancel();
  db_end_transaction(0);
  fossil_print("                               \r");
  if( omitRebuild ){
    omitVacuum = 1;
  }else{
    db_begin_transaction();
    fossil_print("Rebuilding repository meta-data...\n");
    rebuild_db(0, 1, !incrFlag);
    verify_cancel();
    db_end_transaction(0);
  }
  if( !omitVacuum ){
    fossil_print("Vacuuming..."); fflush(stdout);
    db_multi_exec("VACUUM");
  }
  fossil_print(" ok\n");
  if( !incrFlag ){
    fossil_print("project-id: %s\n", db_get("project-code", 0));
    fossil_print("server-id:  %s\n", db_get("server-code", 0));
    zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
    fossil_print("admin-user: %s (password is \"%s\")\n", g.zLogin, zPassword);
  }
}
