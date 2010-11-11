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
      printf(" <%s>", zUser);
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
  zContact = db_column_text(&q, 0);
  for(i=0; zContact[i] && zContact[i]!='>' && zContact[i]!='<'; i++){}
  if( zContact[i]==0 ){
    printf(" %s <%s>", zContact, zUser);
    db_reset(&q);
    return;
  }
  if( zContact[i]=='<' ){
    zEmail = mprintf("%s", &zContact[i]);
    for(i=0; zEmail[i] && zEmail[i]!='>'; i++){}
    if( zEmail[i]=='>' ) zEmail[i+1] = 0;
  }else{
    zEmail = mprintf("<%s>", zUser);
  }
  zName = mprintf("%.*s", i, zContact);
  for(i=j=0; zName[i]; i++){
    if( zName[i]!='"' ) zName[j++] = zName[i];
  }
  zName[j] = 0;
  printf(" %s %s", zName, zEmail);
  free(zName);
  free(zEmail);
  db_reset(&q);
}


/*
** COMMAND: export
**
** Usage: %fossil export
**
** Write an export of all check-ins to standard output.  The export is
** written in the Git "fast-import" format.
**
** Run this command within a checkout.  Or use the -R or --repository
** option to specify a Fossil repository to be exported.
**
** Only check-ins are exported.  Git does not support tickets or wiki
** or events or attachments, so none of that is exported.
*/
void export_cmd(void){
  Stmt q;
  int i;
  Bag blobs, vers;
  bag_init(&blobs);
  bag_init(&vers);

  db_find_and_open_repository(1);

  /* Step 1:  Generate "blob" records for every artifact that is part
  ** of a check-in 
  */
  db_prepare(&q, "SELECT DISTINCT fid FROM mlink WHERE fid>0");
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    Blob content;
    content_get(rid, &content);
    printf("blob\nmark :%d\ndata %d\n", rid, blob_size(&content));
    bag_insert(&blobs, rid);
    fwrite(blob_buffer(&content), 1, blob_size(&content), stdout);
    printf("\n");
    blob_reset(&content);
  }
  db_finalize(&q);

  /* Output the commit records.
  */
  db_prepare(&q,
    "SELECT strftime('%%s',mtime), objid, coalesce(comment,ecomment),"
    "       coalesce(user,euser),"
    "       (SELECT value FROM tagxref WHERE rid=objid AND tagid=%d)"
    "  FROM event"
    " WHERE type='ci'"
    " ORDER BY mtime ASC",
    TAG_BRANCH
  );
  while( db_step(&q)==SQLITE_ROW ){
    sqlite3_int64 secondsSince1970 = db_column_int64(&q, 0);
    int ckinId = db_column_int(&q, 1);
    const char *zComment = db_column_text(&q, 2);
    const char *zUser = db_column_text(&q, 3);
    const char *zBranch = db_column_text(&q, 4);
    char *zBr;
    Manifest *p;
    ManifestFile *pFile;
    const char *zFromType;

    bag_insert(&vers, ckinId);
    if( zBranch==0 ) zBranch = "trunk";
    zBr = mprintf("%s", zBranch);
    for(i=0; zBr[i]; i++){
      if( !fossil_isalnum(zBr[i]) ) zBr[i] = '_';
    }
    printf("commit refs/heads/%s\nmark :%d\n", zBr, ckinId);
    free(zBr);
    printf("committer");
    print_person(zUser);
    printf(" %lld +0000\n", secondsSince1970);
    if( zComment==0 ) zComment = "null comment";
    printf("data %d\n%s\n", (int)strlen(zComment), zComment);
    p = manifest_get(ckinId, CFTYPE_ANY);
    zFromType = "from";
    for(i=0; i<p->nParent; i++){
      int pid = fast_uuid_to_rid(p->azParent[i]);
      if( pid==0 || !bag_find(&vers, pid) ) continue;
      printf("%s :%d\n", zFromType, fast_uuid_to_rid(p->azParent[i]));
      zFromType = "merge";
    }
    printf("deleteall\n");
    manifest_file_rewind(p);
    while( (pFile=manifest_file_next(p, 0))!=0 ){
      int fid = fast_uuid_to_rid(pFile->zUuid);
      const char *zPerm = "100644";
      if( fid==0 ) continue;
      if( pFile->zPerm && strstr(pFile->zPerm,"x") ) zPerm = "100755";
      if( !bag_find(&blobs, fid) ) continue;
      printf("M %s :%d %s\n", zPerm, fid, pFile->zName);
    }
    manifest_destroy(p);
    printf("\n");
  }
  db_finalize(&q);
  bag_clear(&blobs);


  /* Output tags */
  db_prepare(&q,
     "SELECT tagname, rid, strftime('%%s',mtime)"
     "  FROM tagxref JOIN tag USING(tagid)"
     " WHERE tagtype=1 AND tagname GLOB 'sym-*'"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTagname = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    sqlite3_int64 secSince1970 = db_column_int64(&q, 2);
    if( rid==0 || !bag_find(&vers, rid) ) continue;
    zTagname += 4;
    printf("tag %s\n", zTagname);
    printf("from :%d\n", rid);
    printf("tagger <tagger> %lld +0000\n", secSince1970);
    printf("data 0\n");
  }
  db_finalize(&q);
  bag_clear(&vers);
}
