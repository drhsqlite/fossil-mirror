/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code used to check-out versions of the project
** from the local repository.
*/
#include "config.h"
#include "checkout.h"
#include <assert.h>

/*
** Check to see if there is an existing checkout that has been
** modified.  Return values:
**
**     0:   There is an existing checkout but it is unmodified
**     1:   There is a modified checkout - there are unsaved changes
**     2:   There is no existing checkout
*/
int unsaved_changes(void){
  int vid;
  db_must_be_within_tree();
  vid = db_lget_int("checkout",0);
  if( vid==0 ) return 2;
  vfile_check_signature(vid);
  return db_exists("SELECT 1 FROM vfile WHERE chnged"
                   " OR coalesce(origname!=pathname,0)");
}


/*
** zTarget is guaranteed to be a UUID.  It might be the UUID of a ticket.
** If it is, store in *pClosed a true or false depending on whether or not
** the ticket is closed and return true. If zTarget
** is not the UUID of a ticket, return false.
*/
static int is_ticket(
  const char *zTarget,    /* Ticket UUID */
  int *pClosed            /* True if the ticket is closed */
){
  fprintf(stderr,"I'm in is_ticket\n");

  static Stmt q;
  static int once = 1;
  int n;
  int rc;
  char zLower[UUID_SIZE+1];
  char zUpper[UUID_SIZE+1];
  n = strlen(zTarget);
  memcpy(zLower, zTarget, n+1);
  canonical16(zLower, n+1);
  memcpy(zUpper, zLower, n+1);
  zUpper[n-1]++;
  if( once ){
    const char *zClosedExpr = db_get("ticket-closed-expr", "status='Closed'");
    db_static_prepare(&q, 
      "SELECT %s FROM ticket "
      " WHERE tkt_uuid>=:lwr AND tkt_uuid<:upr",
      zClosedExpr
    );
    once = 0;
  }
  db_bind_text(&q, ":lwr", zLower);
  db_bind_text(&q, ":upr", zUpper);
  if( db_step(&q)==SQLITE_ROW ){
    rc = 1;
    *pClosed = db_column_int(&q, 0);
  }else{
    rc = 0;
  }
  db_reset(&q);
  return rc;
}

/*
** Check to see if the requested co is in fact "checkout-able"
** Return values:
**   0: Not checkout-able (does not exist, or is not an on-disk artifact)
**   1: Is checkout-able.
*/
int checkoutable(const char *zName){
  int rc; /* return code */
  int throwaway;

  rc = !is_ticket(zName, &throwaway);
  fprintf(stderr,"rc is: %d\n", rc);
  return(rc);

  Blob uuid;
  const char *rid=(char *)NULL;
  Stmt q; // db query
  char *zSQL; //build-up sql

  //  zSQL = mprintf();

  // [create sql statement]
  // db_prepare(&q,sqlstmt);

  blob_init(&uuid, zName, -1);
  if( name_to_uuid(&uuid, 1) ){
    fossil_panic(g.zErrMsg);
  }

  /*  nParent=db_text(0,"select rid from blob where uuid=%s",uuid.nameofobj); */

  /*  int nParent = db_column_int(q, somenum); */
  return rc;
}


/*
** Undo the current check-out.  Unlink all files from the disk.
** Clear the VFILE table.
*/
void uncheckout(int vid){
  if( vid==0 ) return;
  vfile_unlink(vid);
  db_multi_exec("DELETE FROM vfile WHERE vid=%d", vid);
}


/*
** Given the abbreviated UUID name of a version, load the content of that
** version in the VFILE table.  Return the VID for the version.
**
** If anything goes wrong, panic.
*/
int load_vfile(const char *zName){
  Blob uuid;
  int vid;

  blob_init(&uuid, zName, -1);
  if( name_to_uuid(&uuid, 1) ){
    fossil_panic(g.zErrMsg);
  }
  vid = db_int(0, "SELECT rid FROM blob WHERE uuid=%B", &uuid);
  if( vid==0 ){
    fossil_panic("no such version: %s", g.argv[2]);
  }
  load_vfile_from_rid(vid);
  return vid;
}

/*
** Load a vfile from a record ID.
*/
void load_vfile_from_rid(int vid){
  Blob manifest;

  if( db_exists("SELECT 1 FROM vfile WHERE vid=%d", vid) ){
    return;
  }
  content_get(vid, &manifest);
  vfile_build(vid, &manifest);
  blob_reset(&manifest);
}

/*
** Read the manifest file given by vid out of the repository
** and store it in the root of the local check-out.
*/
void manifest_to_disk(int vid){
  char *zManFile;
  Blob manifest;
  Blob hash;
  Blob filename;
  int baseLen;
  int i;
  Manifest m;

  blob_zero(&manifest);
  zManFile = mprintf("%smanifest", g.zLocalRoot);
  content_get(vid, &manifest);
  blob_write_to_file(&manifest, zManFile);
  free(zManFile);
  blob_zero(&hash);
  sha1sum_blob(&manifest, &hash);
  zManFile = mprintf("%smanifest.uuid", g.zLocalRoot);
  blob_append(&hash, "\n", 1);
  blob_write_to_file(&hash, zManFile);
  free(zManFile);
  blob_reset(&hash);
  manifest_parse(&m, &manifest);
  blob_zero(&filename);
  blob_appendf(&filename, "%s/", g.zLocalRoot);
  baseLen = blob_size(&filename);
  for(i=0; i<m.nFile; i++){ 
    int isExe;
    blob_append(&filename, m.aFile[i].zName, -1);
    isExe = m.aFile[i].zPerm && strstr(m.aFile[i].zPerm, "x");
    file_setexe(blob_str(&filename), isExe);
    blob_resize(&filename, baseLen);
  }
  blob_reset(&filename);
  manifest_clear(&m);
}

/*
** COMMAND: checkout
** COMMAND: co
**
** Usage: %fossil checkout VERSION ?-f|--force?
**
** Check out a version specified on the command-line.  This command
** will not overwrite edited files in the current checkout unless
** the --force option appears on the command-line.
**
** See also the "update" command.
*/
void checkout_cmd(void){
  int forceFlag;
  int noWrite;
  int vid, prior;
  Blob cksum1, cksum1b, cksum2;
  
  db_must_be_within_tree();
  db_begin_transaction();
  forceFlag = find_option("force","f",0)!=0;
  noWrite = find_option("dontwrite",0,0)!=0;
  if( g.argc!=3 ) usage("?--force? VERSION");
  if( !forceFlag && unsaved_changes()==1 ){
    fossil_fatal("there are unsaved changes in the current checkout");
  }
  if(!checkoutable(g.argv[2])){
    fossil_fatal("the VERSION you requested is not a checkout-able artifact");
  }
  if( forceFlag ){
    db_multi_exec("DELETE FROM vfile");
    prior = 0;
  }else{
    prior = db_lget_int("checkout",0);
  }
  vid = load_vfile(g.argv[2]);
  if( prior==vid ){
    return;
  }
  if( !noWrite ){
    uncheckout(prior);
  }
  db_multi_exec("DELETE FROM vfile WHERE vid!=%d", vid);
  if( !noWrite ){
    vfile_to_disk(vid, 0, 1);
    manifest_to_disk(vid);
    db_lset_int("checkout", vid);
    undo_reset();
  }
  db_multi_exec("DELETE FROM vmerge");
  vfile_aggregate_checksum_manifest(vid, &cksum1, &cksum1b);
  vfile_aggregate_checksum_disk(vid, &cksum2);
  if( blob_compare(&cksum1, &cksum2) ){
    printf("WARNING: manifest checksum does not agree with disk\n");
  }
  if( blob_compare(&cksum1, &cksum1b) ){
    printf("WARNING: manifest checksum does not agree with manifest\n");
  }
  db_end_transaction(0);
}

/*
** COMMAND: close
**
** Usage: %fossil close ?-f|--force?
**
** The opposite of "open".  Close the current database connection.
** Require a -f or --force flag if there are unsaved changed in the
** current check-out.
*/
void close_cmd(void){
  int forceFlag = find_option("force","f",0)!=0;
  db_must_be_within_tree();
  if( !forceFlag && unsaved_changes()==1 ){
    fossil_fatal("there are unsaved changes in the current checkout");
  }
  db_close();
  unlink(mprintf("%s_FOSSIL_", g.zLocalRoot));
  unlink(mprintf("%s_FOSSIL_-journal", g.zLocalRoot));
}
