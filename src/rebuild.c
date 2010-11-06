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
** This file contains code used to rebuild the database.
*/
#include "config.h"
#include "rebuild.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>

/*
** Schema changes
*/
static const char zSchemaUpdates[] =
@ -- Index on the delta table
@ --
@ CREATE INDEX IF NOT EXISTS delta_i1 ON delta(srcid);
@
@ -- Artifacts that should not be processed are identified in the
@ -- "shun" table.  Artifacts that are control-file forgeries or
@ -- spam or artifacts whose contents violate administrative policy
@ -- can be shunned in order to prevent them from contaminating
@ -- the repository.
@ --
@ -- Shunned artifacts do not exist in the blob table.  Hence they
@ -- have not artifact ID (rid) and we thus must store their full
@ -- UUID.
@ --
@ CREATE TABLE IF NOT EXISTS shun(uuid UNIQUE);
@
@ -- Artifacts that should not be pushed are stored in the "private"
@ -- table.  
@ --
@ CREATE TABLE IF NOT EXISTS private(rid INTEGER PRIMARY KEY);
@
@ -- An entry in this table describes a database query that generates a
@ -- table of tickets.
@ --
@ CREATE TABLE IF NOT EXISTS reportfmt(
@    rn integer primary key,  -- Report number
@    owner text,              -- Owner of this report format (not used)
@    title text,              -- Title of this report
@    cols text,               -- A color-key specification
@    sqlcode text             -- An SQL SELECT statement for this report
@ );
@
@ -- Some ticket content (such as the originators email address or contact
@ -- information) needs to be obscured to protect privacy.  This is achieved
@ -- by storing an SHA1 hash of the content.  For display, the hash is
@ -- mapped back into the original text using this table.  
@ --
@ -- This table contains sensitive information and should not be shared
@ -- with unauthorized users.
@ --
@ CREATE TABLE IF NOT EXISTS concealed(
@   hash TEXT PRIMARY KEY,
@   content TEXT
@ );
;

/*
** Variables used to store state information about an on-going "rebuild"
** or "deconstruct".
*/
static int totalSize;       /* Total number of artifacts to process */
static int processCnt;      /* Number processed so far */
static int ttyOutput;       /* Do progress output */
static Bag bagDone;         /* Bag of records rebuilt */

static char *zFNameFormat;  /* Format string for filenames on deconstruct */
static int prefixLength;    /* Length of directory prefix for deconstruct */


/*
** Draw the percent-complete message.
** The input is actually the permill complete.
*/
static void percent_complete(int permill){
  static int lastOutput = -1;
  if( permill>lastOutput ){
    printf("  %d.%d%% complete...\r", permill/10, permill%10);
    fflush(stdout);
    lastOutput = permill;
  }
}


/*
** Called after each artifact is processed
*/
static void rebuild_step_done(rid){
  /* assert( bag_find(&bagDone, rid)==0 ); */
  bag_insert(&bagDone, rid);
  if( ttyOutput ){
    processCnt++;
    if (!g.fQuiet && totalSize>0) {
      percent_complete((processCnt*1000)/totalSize);
    }
  }
}

/*
** Rebuild cross-referencing information for the artifact
** rid with content pBase and all of its descendants.  This
** routine clears the content buffer before returning.
**
** If the zFNameFormat variable is set, then this routine is
** called to run "fossil deconstruct" instead of the usual
** "fossil rebuild".  In that case, instead of rebuilding the
** cross-referencing information, write the file content out
** to the approriate directory.
**
** In both cases, this routine automatically recurses to process
** other artifacts that are deltas off of the current artifact.
** This is the most efficient way to extract all of the original
** artifact content from the Fossil repository.
*/
static void rebuild_step(int rid, int size, Blob *pBase){
  static Stmt q1;
  Bag children;
  Blob copy;
  Blob *pUse;
  int nChild, i, cid;

  while( rid>0 ){

    /* Fix up the "blob.size" field if needed. */
    if( size!=blob_size(pBase) ){
      db_multi_exec(
         "UPDATE blob SET size=%d WHERE rid=%d", blob_size(pBase), rid
      );
    }
  
    /* Find all children of artifact rid */
    db_static_prepare(&q1, "SELECT rid FROM delta WHERE srcid=:rid");
    db_bind_int(&q1, ":rid", rid);
    bag_init(&children);
    while( db_step(&q1)==SQLITE_ROW ){
      int cid = db_column_int(&q1, 0);
      if( !bag_find(&bagDone, cid) ){
        bag_insert(&children, cid);
      }
    }
    nChild = bag_count(&children);
    db_reset(&q1);
  
    /* Crosslink the artifact */
    if( nChild==0 ){
      pUse = pBase;
    }else{
      blob_copy(&copy, pBase);
      pUse = &copy;
    }
    if( zFNameFormat==0 ){
      /* We are doing "fossil rebuild" */
      manifest_crosslink(rid, pUse);
    }else{
      /* We are doing "fossil deconstruct" */
      char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      char *zFile = mprintf(zFNameFormat, zUuid, zUuid+prefixLength);
      blob_write_to_file(pUse,zFile);
      free(zFile);
      free(zUuid);
    }
    blob_reset(pUse);
    rebuild_step_done(rid);
  
    /* Call all children recursively */
    rid = 0;
    for(cid=bag_first(&children), i=1; cid; cid=bag_next(&children, cid), i++){
      static Stmt q2;
      int sz;
      db_static_prepare(&q2, "SELECT content, size FROM blob WHERE rid=:rid");
      db_bind_int(&q2, ":rid", cid);
      if( db_step(&q2)==SQLITE_ROW && (sz = db_column_int(&q2,1))>=0 ){
        Blob delta, next;
        db_ephemeral_blob(&q2, 0, &delta);
        blob_uncompress(&delta, &delta);
        blob_delta_apply(pBase, &delta, &next);
        blob_reset(&delta);
        db_reset(&q2);
        if( i<nChild ){
          rebuild_step(cid, sz, &next);
        }else{
          /* Tail recursion */
          rid = cid;
          size = sz;
          blob_reset(pBase);
          *pBase = next;
        }
      }else{
        db_reset(&q2);
        blob_reset(pBase);
      }
    }
    bag_clear(&children);
  }
}

/*
** Check to see if the "sym-trunk" tag exists.  If not, create it
** and attach it to the very first check-in.
*/
static void rebuild_tag_trunk(void){
  int tagid = db_int(0, "SELECT 1 FROM tag WHERE tagname='sym-trunk'");
  int rid;
  char *zUuid;

  if( tagid>0 ) return;
  rid = db_int(0, "SELECT pid FROM plink AS x WHERE NOT EXISTS("
                  "  SELECT 1 FROM plink WHERE cid=x.pid)");
  if( rid==0 ) return;

  /* Add the trunk tag to the root of the whole tree */
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  if( zUuid==0 ) return;
  tag_add_artifact("sym-", "trunk", zUuid, 0, 2, 0, 0);
  tag_add_artifact("", "branch", zUuid, "trunk", 2, 0, 0);
}

/*
** Core function to rebuild the infomration in the derived tables of a
** fossil repository from the blobs. This function is shared between
** 'rebuild_database' ('rebuild') and 'reconstruct_cmd'
** ('reconstruct'), both of which have to regenerate this information
** from scratch.
**
** If the randomize parameter is true, then the BLOBs are deliberately
** extracted in a random order.  This feature is used to test the
** ability of fossil to accept records in any order and still
** construct a sane repository.
*/
int rebuild_db(int randomize, int doOut){
  Stmt s;
  int errCnt = 0;
  char *zTable;
  int incrSize;

  bag_init(&bagDone);
  ttyOutput = doOut;
  processCnt = 0;
  if (!g.fQuiet) {
    percent_complete(0);
  }
  db_multi_exec(zSchemaUpdates);
  for(;;){
    zTable = db_text(0,
       "SELECT name FROM sqlite_master /*scan*/"
       " WHERE type='table'"
       " AND name NOT IN ('blob','delta','rcvfrom','user',"
                         "'config','shun','private','reportfmt',"
                         "'concealed')"
       " AND name NOT GLOB 'sqlite_*'"
    );
    if( zTable==0 ) break;
    db_multi_exec("DROP TABLE %Q", zTable);
    free(zTable);
  }
  db_multi_exec(zRepositorySchema2);
  ticket_create_table(0);
  shun_artifacts();

  db_multi_exec(
     "INSERT INTO unclustered"
     " SELECT rid FROM blob EXCEPT SELECT rid FROM private"
  );
  db_multi_exec(
     "DELETE FROM unclustered"
     " WHERE rid IN (SELECT rid FROM shun JOIN blob USING(uuid))"
  );
  db_multi_exec(
    "DELETE FROM config WHERE name IN ('remote-code', 'remote-maxid')"
  );
  totalSize = db_int(0, "SELECT count(*) FROM blob");
  incrSize = totalSize/100;
  totalSize += incrSize*2;
  db_prepare(&s,
     "SELECT rid, size FROM blob /*scan*/"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
     "   AND NOT EXISTS(SELECT 1 FROM delta WHERE rid=blob.rid)"
  );
  manifest_crosslink_begin();
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      Blob content;
      content_get(rid, &content);
      rebuild_step(rid, size, &content);
    }
  }
  db_finalize(&s);
  db_prepare(&s,
     "SELECT rid, size FROM blob"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
  );
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      if( !bag_find(&bagDone, rid) ){
        Blob content;
        content_get(rid, &content);
        rebuild_step(rid, size, &content);
      }
    }else{
      db_multi_exec("INSERT OR IGNORE INTO phantom VALUES(%d)", rid);
      rebuild_step_done(rid);
    }
  }
  db_finalize(&s);
  manifest_crosslink_end();
  rebuild_tag_trunk();
  if( !g.fQuiet && totalSize>0 ){
    processCnt += incrSize;
    percent_complete((processCnt*1000)/totalSize);
  }
  create_cluster();
  if( !g.fQuiet && totalSize>0 ){
    processCnt += incrSize;
    percent_complete((processCnt*1000)/totalSize);
  }
  if(!g.fQuiet && ttyOutput ){
    printf("\n");
  }
  return errCnt;
}

/*
** COMMAND:  rebuild
**
** Usage: %fossil rebuild ?REPOSITORY?
**
** Reconstruct the named repository database from the core
** records.  Run this command after updating the fossil
** executable in a way that changes the database schema.
*/
void rebuild_database(void){
  int forceFlag;
  int randomizeFlag;
  int errCnt;
  int omitVerify;

  omitVerify = find_option("noverify",0,0)!=0;
  forceFlag = find_option("force","f",0)!=0;
  randomizeFlag = find_option("randomize", 0, 0)!=0;
  if( g.argc==3 ){
    db_open_repository(g.argv[2]);
  }else{
    db_find_and_open_repository(1);
    if( g.argc!=2 ){
      usage("?REPOSITORY-FILENAME?");
    }
    db_close();
    db_open_repository(g.zRepositoryName);
  }
  db_begin_transaction();
  ttyOutput = 1;
  errCnt = rebuild_db(randomizeFlag, 1);
  if( errCnt && !forceFlag ){
    printf("%d errors. Rolling back changes. Use --force to force a commit.\n",
            errCnt);
    db_end_transaction(1);
  }else{
    if( omitVerify ) verify_cancel();
    db_end_transaction(0);
  }
}

/*
** COMMAND:  test-detach
**
** Change the project-code and make other changes in order to prevent
** the repository from ever again pushing or pulling to other
** repositories.  Used to create a "test" repository for development
** testing by cloning a working project repository.
*/
void test_detach_cmd(void){
  db_find_and_open_repository(1);
  db_begin_transaction();
  db_multi_exec(
    "DELETE FROM config WHERE name='last-sync-url';"
    "UPDATE config SET value=lower(hex(randomblob(20)))"
    " WHERE name='project-code';"
    "UPDATE config SET value='detached-' || value"
    " WHERE name='project-name' AND value NOT GLOB 'detached-*';"
  );
  db_end_transaction(0);
}

/*
** COMMAND:  test-create-clusters
**
** Create clusters for all unclustered artifacts if the number of unclustered
** artifacts exceeds the current clustering threshold.
*/
void test_createcluster_cmd(void){
  if( g.argc==3 ){
    db_open_repository(g.argv[2]);
  }else{
    db_find_and_open_repository(1);
    if( g.argc!=2 ){
      usage("?REPOSITORY-FILENAME?");
    }
    db_close();
    db_open_repository(g.zRepositoryName);
  }
  db_begin_transaction();
  create_cluster();
  db_end_transaction(0);  
}

/*
** COMMAND: scrub
** %fossil scrub [--verily] [--force] [REPOSITORY]
**
** The command removes sensitive information (such as passwords) from a
** repository so that the respository can be sent to an untrusted reader.
**
** By default, only passwords are removed.  However, if the --verily option
** is added, then private branches, concealed email addresses, IP
** addresses of correspondents, and similar privacy-sensitive fields
** are also purged.
**
** This command permanently deletes the scrubbed information.  The effects
** of this command are irreversible.  Use with caution.
**
** The user is prompted to confirm the scrub unless the --force option
** is used.
*/
void scrub_cmd(void){
  int bVerily = find_option("verily",0,0)!=0;
  int bForce = find_option("force", "f", 0)!=0;
  int bNeedRebuild = 0;
  if( g.argc!=2 && g.argc!=3 ) usage("?REPOSITORY?");
  if( g.argc==2 ){
    db_must_be_within_tree();
  }else{
    db_open_repository(g.argv[2]);
  }
  if( !bForce ){
    Blob ans;
    blob_zero(&ans);
    prompt_user("Scrubbing the repository will permanently remove user\n"
                "passwords and other information. Changes cannot be undone.\n"
                "Continue (y/N)? ", &ans);
    if( blob_str(&ans)[0]!='y' ){
      fossil_exit(1);
    }
  }
  db_begin_transaction();
  db_multi_exec(
    "UPDATE user SET pw='';"
    "DELETE FROM config WHERE name GLOB 'last-sync-*';"
  );
  if( bVerily ){
    bNeedRebuild = db_exists("SELECT 1 FROM private");
    db_multi_exec(
      "DELETE FROM concealed;"
      "UPDATE rcvfrom SET ipaddr='unknown';"
      "UPDATE user SET photo=NULL, info='';"
      "INSERT INTO shun SELECT uuid FROM blob WHERE rid IN private;"
    );
  }
  if( !bNeedRebuild ){
    db_end_transaction(0);
    db_multi_exec("VACUUM;");
  }else{
    rebuild_db(0, 1);
    db_end_transaction(0);
  }
}

/*
** Recursively read all files from the directory zPath and install
** every file read as a new artifact in the repository.
*/
void recon_read_dir(char *zPath){
  DIR *d;
  struct dirent *pEntry;
  Blob aContent; /* content of the just read artifact */
  static int nFileRead = 0;

  d = opendir(zPath);
  if( d ){
    while( (pEntry=readdir(d))!=0 ){
      Blob path;
      char *zSubpath;

      if( pEntry->d_name[0]=='.' ){
        continue;
      }
      zSubpath = mprintf("%s/%s",zPath,pEntry->d_name);
      if( file_isdir(zSubpath)==1 ){
        recon_read_dir(zSubpath);
      }
      blob_init(&path, 0, 0);
      blob_appendf(&path, "%s", zSubpath);
      if( blob_read_from_file(&aContent, blob_str(&path))==-1 ){
        fossil_panic("some unknown error occurred while reading \"%s\"", 
                     blob_str(&path));
      }
      content_put(&aContent, 0, 0);
      blob_reset(&path);
      blob_reset(&aContent);
      free(zSubpath);
      printf("\r%d", ++nFileRead);
      fflush(stdout);
    }
  }else {
    fossil_panic("encountered error %d while trying to open \"%s\".",
                  errno, g.argv[3]);
  }
}

/*
** COMMAND: reconstruct
**
** Usage: %fossil reconstruct FILENAME DIRECTORY
**
** This command studies the artifacts (files) in DIRECTORY and
** reconstructs the fossil record from them. It places the new
** fossil repository in FILENAME. Subdirectories are read, files
** with leading '.' in the filename are ignored.
**
*/
void reconstruct_cmd(void) {
  char *zPassword;
  if( g.argc!=4 ){
    usage("FILENAME DIRECTORY");
  }
  if( file_isdir(g.argv[3])!=1 ){
    printf("\"%s\" is not a directory\n\n", g.argv[3]);
    usage("FILENAME DIRECTORY");
  }
  db_create_repository(g.argv[2]);
  db_open_repository(g.argv[2]);
  db_open_config(0);
  db_begin_transaction();
  db_initial_setup(0, 0, 1);

  printf("Reading files from directory \"%s\"...\n", g.argv[3]);
  recon_read_dir(g.argv[3]);
  printf("\nBuilding the Fossil repository...\n");

  rebuild_db(0, 1);

  /* Skip the verify_before_commit() step on a reconstruct.  Most artifacts
  ** will have been changed and verification therefore takes a really, really
  ** long time.
  */
  verify_cancel();
  
  db_end_transaction(0);
  printf("project-id: %s\n", db_get("project-code", 0));
  printf("server-id: %s\n", db_get("server-code", 0));
  zPassword = db_text(0, "SELECT pw FROM user WHERE login=%Q", g.zLogin);
  printf("admin-user: %s (initial password is \"%s\")\n", g.zLogin, zPassword);
}

/*
** COMMAND: deconstruct
**
** Usage %fossil deconstruct ?-R|--repository REPOSITORY? ?-L|--prefixlength N? DESTINATION
**
** This command exports all artifacts of o given repository and
** writes all artifacts to the file system. The DESTINATION directory
** will be populated with subdirectories AA and files AA/BBBBBBBBB.., where
** AABBBBBBBBB.. is the 40 character artifact ID, AA the first 2 characters.
** If -L|--prefixlength is given, the length (default 2) of the directory
** prefix can be set to 0,1,..,9 characters.
*/
void deconstruct_cmd(void){
  const char *zDestDir;
  const char *zPrefixOpt;
  Stmt        s;

  /* check number of arguments */
  if( (g.argc != 3) && (g.argc != 5)  && (g.argc != 7)){
    usage ("?-R|--repository REPOSITORY? ?-L|--prefixlength N? DESTINATION");
  }
  /* get and check argument destination directory */
  zDestDir = g.argv[g.argc-1];
  if( !*zDestDir  || !file_isdir(zDestDir)) {
    fossil_panic("DESTINATION(%s) is not a directory!",zDestDir);
  }
  /* get and check prefix length argument and build format string */
  zPrefixOpt=find_option("prefixlength","L",1);
  if( !zPrefixOpt ){
    prefixLength = 2;
  }else{
    if( zPrefixOpt[0]>='0' && zPrefixOpt[0]<='9' && !zPrefixOpt[1] ){
      prefixLength = (int)(*zPrefixOpt-'0');
    }else{
      fossil_fatal("N(%s) is not a a valid prefix length!",zPrefixOpt);
    }
  }
#ifndef _WIN32
  if( access(zDestDir, W_OK) ){
    fossil_fatal("DESTINATION(%s) is not writeable!",zDestDir);
  }
#else
  /* write access on windows is not checked, errors will be
  ** dected on blob_write_to_file
  */
#endif
  if( prefixLength ){
    zFNameFormat = mprintf("%s/%%.%ds/%%s",zDestDir,prefixLength);
  }else{
    zFNameFormat = mprintf("%s/%%s",zDestDir);
  }
  /* open repository and open query for all artifacts */
  db_find_and_open_repository(1);
  bag_init(&bagDone);
  ttyOutput = 1;
  processCnt = 0;
  if (!g.fQuiet) {
    printf("0 (0%%)...\r");
    fflush(stdout);
  }
  totalSize = db_int(0, "SELECT count(*) FROM blob");
  db_prepare(&s,
     "SELECT rid, size FROM blob /*scan*/"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
     "   AND NOT EXISTS(SELECT 1 FROM delta WHERE rid=blob.rid)"
  );
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      Blob content;
      content_get(rid, &content);
      rebuild_step(rid, size, &content);
    }
  }
  db_finalize(&s);
  db_prepare(&s,
     "SELECT rid, size FROM blob"
     " WHERE NOT EXISTS(SELECT 1 FROM shun WHERE uuid=blob.uuid)"
  );
  while( db_step(&s)==SQLITE_ROW ){
    int rid = db_column_int(&s, 0);
    int size = db_column_int(&s, 1);
    if( size>=0 ){
      if( !bag_find(&bagDone, rid) ){
        Blob content;
        content_get(rid, &content);
        rebuild_step(rid, size, &content);
      }
    }
  }
  db_finalize(&s);
  if(!g.fQuiet && ttyOutput ){
    printf("\n");
  }

  /* free filename format string */
  free(zFNameFormat);
  zFNameFormat = 0;
}
