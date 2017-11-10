#ifdef FOSSIL_ENABLE_JSON
/*
** Copyright (c) 2011 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*/
#include "VERSION.h"
#include "config.h"
#include "json_branch.h"

#if INTERFACE
#include "json_detail.h"
#endif


static cson_value * json_branch_list();
static cson_value * json_branch_create();
/*
** Mapping of /json/branch/XXX commands/paths to callbacks.
*/
static const JsonPageDef JsonPageDefs_Branch[] = {
{"create", json_branch_create, 0},
{"list", json_branch_list, 0},
{"new", json_branch_create, -1/* for compat with non-JSON branch command.*/},
/* Last entry MUST have a NULL name. */
{NULL,NULL,0}
};

/*
** Implements the /json/branch family of pages/commands. Far from
** complete.
**
*/
cson_value * json_page_branch(){
  return json_page_dispatch_helper(&JsonPageDefs_Branch[0]);
}

/*
** Impl for /json/branch/list
**
**
** CLI mode options:
**
**  --range X | -r X, where X is one of (open,closed,all)
**    (only the first letter is significant, default=open).
**  -a (same as --range a)
**  -c (same as --range c)
**
** HTTP mode options:
**
** "range" GET/POST.payload parameter. FIXME: currently we also use
** POST, but really want to restrict this to POST.payload.
*/
static cson_value * json_branch_list(){
  cson_value * payV;
  cson_object * pay;
  cson_value * listV;
  cson_array * list;
  char const * range = NULL;
  int branchListFlags = BRL_OPEN_ONLY;
  char * sawConversionError = NULL;
  Stmt q;
  if( !g.perm.Read ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'o' permissions.");
    return NULL;
  }
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);
  listV = cson_value_new_array();
  list = cson_value_get_array(listV);
  if(fossil_has_json()){
      range = json_getenv_cstr("range");
  }

  range = json_find_option_cstr("range",NULL,"r");
  if((!range||!*range) && !g.isHTTP){
    range = find_option("all","a",0);
    if(range && *range){
      range = "a";
    }else{
      range = find_option("closed","c",0);
      if(range&&*range){
        range = "c";
      }
    }
  }

  if(!range || !*range){
    range = "o";
  }
  /* Normalize range values... */
  switch(*range){
    case 'c':
      range = "closed";
      branchListFlags = BRL_CLOSED_ONLY;
      break;
    case 'a':
      range = "all";
      branchListFlags = BRL_BOTH;
      break;
    default:
      range = "open";
      branchListFlags = BRL_OPEN_ONLY;
      break;
  };
  cson_object_set(pay,"range",json_new_string(range));

  if( g.localOpen ){ /* add "current" property (branch name). */
    int vid = db_lget_int("checkout", 0);
    char const * zCurrent = vid
      ? db_text(0, "SELECT value FROM tagxref"
                " WHERE rid=%d AND tagid=%d",
                vid, TAG_BRANCH)
      : 0;
    if(zCurrent){
      cson_object_set(pay,"current",json_new_string(zCurrent));
    }
  }


  branch_prepare_list_query(&q, branchListFlags);
  cson_object_set(pay,"branches",listV);
  while((SQLITE_ROW==db_step(&q))){
    cson_value * v = cson_sqlite3_column_to_value(q.pStmt,0);
    if(v){
      cson_array_append(list,v);
    }else if(!sawConversionError){
      sawConversionError = mprintf("Column-to-json failed @ %s:%d",
                                   __FILE__,__LINE__);
    }
  }
  if( sawConversionError ){
    json_warn(FSL_JSON_W_COL_TO_JSON_FAILED,sawConversionError);
    free(sawConversionError);
  }
  return payV;
}

/*
** Parameters for the create-branch operation.
*/
typedef struct BranchCreateOptions{
  char const * zName;
  char const * zBasis;
  char const * zColor;
  char isPrivate;
  /**
     Might be set to an error string by
     json_branch_new().
   */
  char const * rcErrMsg;
} BranchCreateOptions;

/*
** Tries to create a new branch based on the options set in zOpt. If
** an error is encountered, zOpt->rcErrMsg _might_ be set to a
** descriptive string and one of the FossilJsonCodes values will be
** returned. Or fossil_fatal() (or similar) might be called, exiting
** the app.
**
** On success 0 is returned and if zNewRid is not NULL then the rid of
** the new branch is assigned to it.
**
** If zOpt->isPrivate is 0 but the parent branch is private,
** zOpt->isPrivate will be set to a non-zero value and the new branch
** will be private.
*/
static int json_branch_new(BranchCreateOptions * zOpt,
                           int *zNewRid){
  /* Mostly copied from branch.c:branch_new(), but refactored a small
     bit to not produce output or interact with the user. The
     down-side to that is that we dropped the gpg-signing. It was
     either that or abort the creation if we couldn't sign. We can't
     sign over HTTP mode, anyway.
  */
  char const * zBranch = zOpt->zName;
  char const * zBasis = zOpt->zBasis;
  char const * zColor = zOpt->zColor;
  int rootid;            /* RID of the root check-in - what we branch off of */
  int brid;              /* RID of the branch check-in */
  int i;                 /* Loop counter */
  char *zUuid;           /* Artifact ID of origin */
  Stmt q;                /* Generic query */
  char *zDate;           /* Date that branch was created */
  char *zComment;        /* Check-in comment for the new branch */
  Blob branch;           /* manifest for the new branch */
  Manifest *pParent;     /* Parsed parent manifest */
  Blob mcksum;           /* Self-checksum on the manifest */

  /* fossil branch new name */
  if( zBranch==0 || zBranch[0]==0 ){
    zOpt->rcErrMsg = "Branch name may not be null/empty.";
    return FSL_JSON_E_INVALID_ARGS;
  }
  if( db_exists(
        "SELECT 1 FROM tagxref"
        " WHERE tagtype>0"
        "   AND tagid=(SELECT tagid FROM tag WHERE tagname='sym-%q')",
        zBranch)!=0 ){
    zOpt->rcErrMsg = "Branch already exists.";
    return FSL_JSON_E_RESOURCE_ALREADY_EXISTS;
  }

  db_begin_transaction();
  rootid = name_to_typed_rid(zBasis, "ci");
  if( rootid==0 ){
    zOpt->rcErrMsg = "Basis branch not found.";
    return FSL_JSON_E_RESOURCE_NOT_FOUND;
  }

  pParent = manifest_get(rootid, CFTYPE_MANIFEST, 0);
  if( pParent==0 ){
    zOpt->rcErrMsg = "Could not read parent manifest.";
    return FSL_JSON_E_UNKNOWN;
  }

  /* Create a manifest for the new branch */
  blob_zero(&branch);
  if( pParent->zBaseline ){
    blob_appendf(&branch, "B %s\n", pParent->zBaseline);
  }
  zComment = mprintf("Create new branch named \"%s\" "
                     "from \"%s\".", zBranch, zBasis);
  blob_appendf(&branch, "C %F\n", zComment);
  free(zComment);
  zDate = date_in_standard_format("now");
  blob_appendf(&branch, "D %s\n", zDate);
  free(zDate);

  /* Copy all of the content from the parent into the branch */
  for(i=0; i<pParent->nFile; ++i){
    blob_appendf(&branch, "F %F", pParent->aFile[i].zName);
    if( pParent->aFile[i].zUuid ){
      blob_appendf(&branch, " %s", pParent->aFile[i].zUuid);
      if( pParent->aFile[i].zPerm && pParent->aFile[i].zPerm[0] ){
        blob_appendf(&branch, " %s", pParent->aFile[i].zPerm);
      }
    }
    blob_append(&branch, "\n", 1);
  }
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rootid);
  blob_appendf(&branch, "P %s\n", zUuid);
  free(zUuid);
  if( pParent->zRepoCksum ){
    blob_appendf(&branch, "R %s\n", pParent->zRepoCksum);
  }
  manifest_destroy(pParent);

  /* Add the symbolic branch name and the "branch" tag to identify
  ** this as a new branch */
  if( content_is_private(rootid) ) zOpt->isPrivate = 1;
  if( zOpt->isPrivate && zColor==0 ) zColor = "#fec084";
  if( zColor!=0 ){
    blob_appendf(&branch, "T *bgcolor * %F\n", zColor);
  }
  blob_appendf(&branch, "T *branch * %F\n", zBranch);
  blob_appendf(&branch, "T *sym-%F *\n", zBranch);
  if( zOpt->isPrivate ){
    blob_appendf(&branch, "T +private *\n");
  }

  /* Cancel all other symbolic tags */
  db_prepare(&q,
      "SELECT tagname FROM tagxref, tag"
      " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
      "   AND tagtype>0 AND tagname GLOB 'sym-*'"
      " ORDER BY tagname",
      rootid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTag = db_column_text(&q, 0);
    blob_appendf(&branch, "T -%F *\n", zTag);
  }
  db_finalize(&q);

  blob_appendf(&branch, "U %F\n", g.zLogin);
  md5sum_blob(&branch, &mcksum);
  blob_appendf(&branch, "Z %b\n", &mcksum);

  brid = content_put(&branch);
  if( brid==0 ){
    fossil_fatal("Problem committing manifest: %s", g.zErrMsg);
  }
  db_multi_exec("INSERT OR IGNORE INTO unsent VALUES(%d)", brid);
  if( manifest_crosslink(brid, &branch, MC_PERMIT_HOOKS)==0 ){
    fossil_fatal("%s", g.zErrMsg);
  }
  assert( blob_is_reset(&branch) );
  content_deltify(rootid, &brid, 1, 0);
  if( zNewRid ){
    *zNewRid = brid;
  }

  /* Commit */
  db_end_transaction(0);

#if 0 /* Do an autosync push, if requested */
  /* arugable for JSON mode? */
  if( !g.isHTTP && !isPrivate ) autosync(SYNC_PUSH);
#endif
  return 0;
}


/*
** Impl of /json/branch/create.
*/
static cson_value * json_branch_create(){
  cson_value * payV = NULL;
  cson_object * pay = NULL;
  int rc = 0;
  BranchCreateOptions opt;
  char * zUuid = NULL;
  int rid = 0;
  if( !g.perm.Write ){
    json_set_err(FSL_JSON_E_DENIED,
                 "Requires 'i' permissions.");
    return NULL;
  }
  memset(&opt,0,sizeof(BranchCreateOptions));
  if(fossil_has_json()){
    opt.zName = json_getenv_cstr("name");
  }

  if(!opt.zName){
    opt.zName = json_command_arg(g.json.dispatchDepth+1);
  }

  if(!opt.zName){
    json_set_err(FSL_JSON_E_MISSING_ARGS, "'name' parameter was not specified." );
    return NULL;
  }

  opt.zColor = json_find_option_cstr("bgColor","bgcolor",NULL);
  opt.zBasis = json_find_option_cstr("basis",NULL,NULL);
  if(!opt.zBasis && !g.isHTTP){
    opt.zBasis = json_command_arg(g.json.dispatchDepth+2);
  }
  if(!opt.zBasis){
    opt.zBasis = "trunk";
  }
  opt.isPrivate = json_find_option_bool("private",NULL,NULL,-1);
  if(-1==opt.isPrivate){
    if(!g.isHTTP){
      opt.isPrivate = (NULL != find_option("private","",0));
    }else{
      opt.isPrivate = 0;
    }
  }

  rc = json_branch_new( &opt, &rid );
  if(rc){
    json_set_err(rc, opt.rcErrMsg);
    goto error;
  }
  assert(0 != rid);
  payV = cson_value_new_object();
  pay = cson_value_get_object(payV);

  cson_object_set(pay,"name",json_new_string(opt.zName));
  cson_object_set(pay,"basis",json_new_string(opt.zBasis));
  cson_object_set(pay,"rid",json_new_int(rid));
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  cson_object_set(pay,"uuid", json_new_string(zUuid));
  cson_object_set(pay, "isPrivate", cson_value_new_bool(opt.isPrivate));
  free(zUuid);
  if(opt.zColor){
    cson_object_set(pay,"bgColor",json_new_string(opt.zColor));
  }

  goto ok;
  error:
  assert( 0 != g.json.resultCode );
  cson_value_free(payV);
  payV = NULL;
  ok:
  return payV;
}

#endif /* FOSSIL_ENABLE_JSON */
