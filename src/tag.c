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
** This file contains code used to manage tags
*/
#include "config.h"
#include "tag.h"
#include <assert.h>

/*
** Propagate the tag given by tagid to the children of pid.
**
** This routine assumes that tagid is a tag that should be
** propagated and that the tag is already present in pid.
**
** If tagtype is 2 then the tag is being propagated from an
** ancestor node.  If tagtype is 0 it means a propagating tag is
** being blocked.
*/
static void tag_propagate(
  int pid,             /* Propagate the tag to children of this node */
  int tagid,           /* Tag to propagate */
  int tagType,         /* 2 for a propagating tag.  0 for an antitag */
  int origId,          /* Artifact of tag, when tagType==2 */
  const char *zValue,  /* Value of the tag.  Might be NULL */
  double mtime         /* Timestamp on the tag */
){
  PQueue queue;        /* Queue of check-ins to be tagged */
  Stmt s;              /* Query the children of :pid to which to propagate */
  Stmt ins;            /* INSERT INTO tagxref */
  Stmt eventupdate;    /* UPDATE event */

  assert( tagType==0 || tagType==2 );
  pqueuex_init(&queue);
  pqueuex_insert(&queue, pid, 0.0);

  /* Query for children of :pid to which to propagate the tag.
  ** Three returns:  (1) rid of the child.  (2) timestamp of child.
  ** (3) True to propagate or false to block.
  */
  db_prepare(&s,
     "SELECT cid, plink.mtime,"
     "       coalesce(srcid=0 AND tagxref.mtime<:mtime, %d) AS doit"
     "  FROM plink LEFT JOIN tagxref ON cid=rid AND tagid=%d"
     " WHERE pid=:pid AND isprim",
     tagType==2, tagid
  );
  db_bind_double(&s, ":mtime", mtime);

  if( tagType==2 ){
    /* Set the propagated tag marker on check-in :rid */
    db_prepare(&ins,
       "REPLACE INTO tagxref(tagid, tagtype, srcid, origid, value, mtime, rid)"
       "VALUES(%d,2,0,%d,%Q,:mtime,:rid)",
       tagid, origId, zValue
    );
    db_bind_double(&ins, ":mtime", mtime);
  }else{
    /* Remove all references to the tag from check-in :rid */
    zValue = 0;
    db_prepare(&ins,
       "DELETE FROM tagxref WHERE tagid=%d AND rid=:rid", tagid
    );
  }
  if( tagid==TAG_BGCOLOR ){
    db_prepare(&eventupdate,
      "UPDATE event SET bgcolor=%Q WHERE objid=:rid", zValue
    );
  }
  while( (pid = pqueuex_extract(&queue))!=0 ){
    db_bind_int(&s, ":pid", pid);
    while( db_step(&s)==SQLITE_ROW ){
      int doit = db_column_int(&s, 2);
      if( doit ){
        int cid = db_column_int(&s, 0);
        double mtime = db_column_double(&s, 1);
        pqueuex_insert(&queue, cid, mtime);
        db_bind_int(&ins, ":rid", cid);
        db_step(&ins);
        db_reset(&ins);
        if( tagid==TAG_BGCOLOR ){
          db_bind_int(&eventupdate, ":rid", cid);
          db_step(&eventupdate);
          db_reset(&eventupdate);
        }
        if( tagid==TAG_BRANCH ){
          leaf_eventually_check(cid);
        }
      }
    }
    db_reset(&s);
  }
  pqueuex_clear(&queue);
  db_finalize(&ins);
  db_finalize(&s);
  if( tagid==TAG_BGCOLOR ){
    db_finalize(&eventupdate);
  }
}

/*
** Propagate all propagatable tags in pid to the children of pid.
*/
void tag_propagate_all(int pid){
  Stmt q;
  db_prepare(&q,
     "SELECT tagid, tagtype, mtime, value, origid FROM tagxref"
     " WHERE rid=%d",
     pid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int tagid = db_column_int(&q, 0);
    int tagtype = db_column_int(&q, 1);
    double mtime = db_column_double(&q, 2);
    const char *zValue = db_column_text(&q, 3);
    int origid = db_column_int(&q, 4);
    if( tagtype==1 ) tagtype = 0;
    tag_propagate(pid, tagid, tagtype, origid, zValue, mtime);
  }
  db_finalize(&q);
}

/*
** Get a tagid for the given TAG.  Create a new tag if necessary
** if createFlag is 1.
*/
int tag_findid(const char *zTag, int createFlag){
  int id;
  id = db_int(0, "SELECT tagid FROM tag WHERE tagname=%Q", zTag);
  if( id==0 && createFlag ){
    db_multi_exec("INSERT INTO tag(tagname) VALUES(%Q)", zTag);
    id = db_last_insert_rowid();
  }
  return id;
}

/*
** Insert a tag into the database.
**
** Also translate zTag into a tagid and return the tagid.  (In other words
** if zTag is "bgcolor" then return TAG_BGCOLOR.)
*/
int tag_insert(
  const char *zTag,        /* Name of the tag (w/o the "+" or "-" prefix */
  int tagtype,             /* 0:cancel  1:singleton  2:propagated */
  const char *zValue,      /* Value if the tag is really a property */
  int srcId,               /* Artifact that contains this tag */
  double mtime,            /* Timestamp.  Use default if <=0.0 */
  int rid                  /* Artifact to which the tag is to attached */
){
  Stmt s;
  const char *zCol;
  int tagid = tag_findid(zTag, 1);
  int rc;

  if( mtime<=0.0 ){
    mtime = db_double(0.0, "SELECT julianday('now')");
  }
  db_prepare(&s,
    "SELECT 1 FROM tagxref"
    " WHERE tagid=%d"
    "   AND rid=%d"
    "   AND mtime>=:mtime",
    tagid, rid
  );
  db_bind_double(&s, ":mtime", mtime);
  rc = db_step(&s);
  db_finalize(&s);
  if( rc==SQLITE_ROW ){
    /* Another entry that is more recent already exists.  Do nothing */
    return tagid;
  }
  db_prepare(&s,
    "REPLACE INTO tagxref(tagid,tagtype,srcId,origid,value,mtime,rid)"
    " VALUES(%d,%d,%d,%d,%Q,:mtime,%d)",
    tagid, tagtype, srcId, rid, zValue, rid
  );
  db_bind_double(&s, ":mtime", mtime);
  db_step(&s);
  db_finalize(&s);
  if( tagid==TAG_BRANCH ) leaf_eventually_check(rid);
  if( tagtype==0 ){
    zValue = 0;
  }
  zCol = 0;
  switch( tagid ){
    case TAG_BGCOLOR: {
      zCol = "bgcolor";
      break;
    }
    case TAG_COMMENT: {
      zCol = "ecomment";
      break;
    }
    case TAG_USER: {
      zCol = "euser";
      break;
    }
    case TAG_PRIVATE: {
      db_multi_exec(
        "INSERT OR IGNORE INTO private(rid) VALUES(%d);",
        rid
      );
    }
  }
  if( zCol ){
    db_multi_exec("UPDATE event SET \"%w\"=%Q WHERE objid=%d",
                  zCol, zValue, rid);
    if( tagid==TAG_COMMENT && zValue!=0 ){
      char *zCopy = fossil_strdup(zValue);
      backlink_extract(zCopy, MT_NONE, rid, BKLNK_COMMENT, mtime, 1);
      free(zCopy);
    }
  }
  if( tagid==TAG_DATE ){
    db_multi_exec("UPDATE event "
                  "   SET mtime=julianday(%Q),"
                  "       omtime=coalesce(omtime,mtime)"
                  " WHERE objid=%d",
                  zValue, rid);
  }
  if( tagid==TAG_PARENT && tagtype==1 ){
    manifest_reparent_checkin(rid, zValue);
  }
  if( tagtype==1 ) tagtype = 0;
  tag_propagate(rid, tagid, tagtype, rid, zValue, mtime);
  return tagid;
}


/*
** COMMAND: test-tag
**
** Usage: %fossil test-tag (+|*|-)TAGNAME ARTIFACT-ID ?VALUE?
**
** Add a tag or anti-tag to the rebuildable tables of the local repository.
** No tag artifact is created so the new tag is erased the next
** time the repository is rebuilt.  This routine is for testing
** use only.
*/
void testtag_cmd(void){
  const char *zTag;
  const char *zValue;
  int rid;
  int tagtype;
  db_must_be_within_tree();
  if( g.argc!=4 && g.argc!=5 ){
    usage("TAGNAME ARTIFACT-ID ?VALUE?");
  }
  zTag = g.argv[2];
  switch( zTag[0] ){
    case '+':  tagtype = 1;  break;
    case '*':  tagtype = 2;  break;
    case '-':  tagtype = 0;  break;
    default:
      fossil_fatal("tag should begin with '+', '*', or '-'");
      return;
  }
  rid = name_to_rid(g.argv[3]);
  if( rid==0 ){
    fossil_fatal("no such object: %s", g.argv[3]);
  }
  g.markPrivate = content_is_private(rid);
  zValue = g.argc==5 ? g.argv[4] : 0;
  db_begin_transaction();
  tag_insert(zTag, tagtype, zValue, -1, 0.0, rid);
  db_end_transaction(0);
}

/*
** OR this value into the tagtype argument to tag_add_artifact to
** cause the tag to be displayed on standard output rather than be
** inserted.  Used for --dry-run options and debugging.
*/
#if INTERFACE
#define TAG_ADD_DRYRUN  0x04
#endif

/*
** Add a control record to the repository that either creates
** or cancels a tag.
**
** tagtype should normally be 0, 1, or 2.  But if the TAG_ADD_DRYRUN bit
** is also set, then simply print the text of the tag on standard output
** (for testing purposes) rather than create the tag.
*/
void tag_add_artifact(
  const char *zPrefix,        /* Prefix to prepend to tag name */
  const char *zTagname,       /* The tag to add or cancel */
  const char *zObjName,       /* Name of object attached to */
  const char *zValue,         /* Value for the tag.  Might be NULL */
  int tagtype,                /* 0:cancel 1:singleton 2:propagated */
  const char *zDateOvrd,      /* Override date string */
  const char *zUserOvrd       /* Override user name */
){
  int rid;
  int nrid;
  char *zDate;
  Blob uuid;
  Blob ctrl;
  Blob cksum;
  static const char zTagtype[] = { '-', '+', '*' };
  int dryRun = 0;

  if( tagtype & TAG_ADD_DRYRUN ){
    tagtype &= ~TAG_ADD_DRYRUN;
    dryRun = 1;
  }
  assert( tagtype>=0 && tagtype<=2 );
  user_select();
  blob_zero(&uuid);
  blob_append(&uuid, zObjName, -1);
  if( name_to_uuid(&uuid, 9, "*") ){
    fossil_fatal("%s", g.zErrMsg);
    return;
  }
  rid = name_to_rid(blob_str(&uuid));
  g.markPrivate = content_is_private(rid);
  blob_zero(&ctrl);

#if 0
  if( validate16(zTagname, strlen(zTagname)) ){
    fossil_fatal(
       "invalid tag name \"%s\" - might be confused with"
       " a hexadecimal artifact ID",
       zTagname
    );
  }
#endif
  zDate = date_in_standard_format(zDateOvrd ? zDateOvrd : "now");
  blob_appendf(&ctrl, "D %s\n", zDate);
  blob_appendf(&ctrl, "T %c%s%F %b",
               zTagtype[tagtype], zPrefix, zTagname, &uuid);
  if( tagtype>0 && zValue && zValue[0] ){
    blob_appendf(&ctrl, " %F\n", zValue);
  }else{
    blob_appendf(&ctrl, "\n");
  }
  blob_appendf(&ctrl, "U %F\n", zUserOvrd ? zUserOvrd : login_name());
  md5sum_blob(&ctrl, &cksum);
  blob_appendf(&ctrl, "Z %b\n", &cksum);
  if( dryRun ){
    fossil_print("%s", blob_str(&ctrl));
    blob_reset(&ctrl);
  }else{
    nrid = content_put(&ctrl);
    manifest_crosslink(nrid, &ctrl, MC_PERMIT_HOOKS);
  }
  assert( blob_is_reset(&ctrl) );
  if( g.localOpen ){
    manifest_to_disk(rid);
  }
}

/*
** If zTag is NULL or valid for use as a tag for the `tag add` and
** `tag cancel` commands, returns without side effects, else emits a
** fatal error message. We reject certain prefixes to avoid that
** clients cause undue grief by improperly tagging artifacts as being,
** e.g., wiki pages or tickets.
**
** Note that we intentionally allow the "sym-" prefix, partly for
** historical compatibility and partly because it can be applied
** properly, whereas the other reserved name types have special
** meanings for fossil and cannot be sensibly manually manipulated.
*/
static void tag_cmd_tagname_check(const char *zTag){
  if(zTag && *zTag &&
     (strncmp(zTag,"wiki-",5)==0
      || strncmp(zTag,"tkt-",4)==0
      || strncmp(zTag,"event-",6)==0)){
    fossil_fatal("Invalid prefix for tag name: %s", zTag);
  }
}

/*
** COMMAND: tag
**
** Usage: %fossil tag SUBCOMMAND ...
**
** Run various subcommands to control tags and properties.
**
** > fossil tag add ?OPTIONS? TAGNAME ARTIFACT-ID ?VALUE?
**
**         Add a new tag or property to an artifact referenced by
**         ARTIFACT-ID. For check-ins, the tag will be usable instead
**         of a CHECK-IN in commands such as update and merge. If the
**         --propagate flag is present and ARTIFACT-ID refers to a
**         wiki page, forum post, technote, or check-in, the tag
**         propagates to all descendants of that artifact.
**
**         Options:
**           --date-override DATETIME   Set date and time added
**           -n|--dry-run               Display the tag text, but do not
**                                      actually insert it into the database
**           --propagate                Propagating tag
**           --raw                      Raw tag name. Ignored for
**                                      non-CHECK-IN artifacts.
**           --user-override USER       Name USER when adding the tag
**
**         The --date-override and --user-override options support
**         importing history from other SCM systems. DATETIME has
**         the form 'YYYY-MM-DD HH:MM:SS'.
**
**         Note that fossil uses some tag prefixes internally and this
**         command will reject tags with these prefixes to avoid
**         causing problems or confusion: "wiki-", "tkt-", "event-".
**
** > fossil tag cancel ?--raw? TAGNAME ARTIFACT-ID
**
**         Remove the tag TAGNAME from the artifact referenced by
**         ARTIFACT-ID, and also remove the propagation of the tag to
**         any descendants.  Use the the -n|--dry-run option to see
**         what would have happened. Certain tag name prefixes are
**         forbidden, as documented for the 'add' subcommand.
**
**         Options:
**           --date-override DATETIME    Set date and time deleted
**           -n|--dry-run                Display the control artifact, but do
**                                       not insert it into the database
**           --raw                       Raw tag name. Ignored for
**                                       non-CHECK-IN artifacts.
**           --user-override USER        Name USER when deleting the tag
**
** > fossil tag find ?OPTIONS? TAGNAME
**
**         List all objects that use TAGNAME.
**
**         Options:
**           -n|--limit N    Limit to N results
**           --raw           Interprets tag as a raw name instead of a
**                           branch name and matches any type of artifact.
**                           Changes the output to include only the
**                           hashes of matching objects.
**           -t|--type TYPE  One of: ci (check-in), w (wiki),
**                           e (event/technote), f (forum post),
**                           t (ticket). Default is all types. Ignored
**                           if --raw is used.
**
** > fossil tag list|ls ?OPTIONS? ?ARTIFACT-ID?
**
**         List all tags or, if ARTIFACT-ID is supplied, all tags and
**         their values for that artifact. The tagtype option accepts
**         one of: propagated, singleton, cancel.  For historical
**         scripting compatibility, the internal tag types "wiki-",
**         "tkt-", and "event-" (technote) are elided by default
**         unless the --raw or --prefix options are used.
**
**         Options:
**           -v|--inverse    Inverse the meaning of --tagtype TYPE
**           --prefix        List only tags with the given prefix
**                           Fossil-internal prefixes include "sym-"
**                           (branch name), "wiki-", "event-"
**                           (technote), and "tkt-" (ticket). The
**                           prefix is stripped from the resulting
**                           list unless --raw is provided. Ignored if
**                           ARTIFACT-ID is provided.
**           --raw           List raw names of tags
**           --sep SEP       Separator when concatenating values
**           --tagtype TYPE  List only tags of type TYPE, which must
**                           be one of: cancel, singleton, propagated
**           --values        List tag values
**                           If --sep is supplied, list all values of a tag on
**                           the same line, separated by SEP; otherwise list
**                           each value on its own line.
**
** The option --raw allows the manipulation of all types of tags
** used for various internal purposes in fossil. It also shows
** "cancel" tags for the "find" and "list" subcommands. You should
** not use this option to make changes unless you are sure what
** you are doing.
**
** If you need to use a tagname that might be confused with
** a hexadecimal baseline or artifact ID, you can explicitly
** disambiguate it by prefixing it with "tag:". For instance:
**
**   fossil update decaf
**
** will be taken as an artifact or baseline ID and fossil will
** probably complain that no such revision was found. However
**
**   fossil update tag:decaf
**
** will assume that "decaf" is a tag/branch name.
**
*/
void tag_cmd(void){
  int n;

  db_find_and_open_repository(0, 0);
  if( g.argc<3 ){
    goto tag_cmd_usage;
  }
  n = strlen(g.argv[2]);
  if( n==0 ){
    goto tag_cmd_usage;
  }

  if( strncmp(g.argv[2],"add",n)==0 ){
    char *zValue;
    int dryRun = 0;
    int fRaw = find_option("raw","",0)!=0;
    const char *zPrefix = "";
    int fPropagate = find_option("propagate","",0)!=0;
    const char *zDateOvrd = find_option("date-override",0,1);
    const char *zUserOvrd = find_option("user-override",0,1);
    const char *zTag;
    const char *zObjId;
    int objType;
    if( find_option("dry-run","n",0)!=0 ) dryRun = TAG_ADD_DRYRUN;
    if( g.argc!=5 && g.argc!=6 ){
      usage("add ?options? TAGNAME ARTIFACT-ID ?VALUE?");
    }
    zTag = g.argv[3];
    tag_cmd_tagname_check(zTag);
    zObjId = g.argv[4];
    zValue = g.argc==6 ? g.argv[5] : 0;
    objType = whatis_rid_type(symbolic_name_to_rid(zObjId, 0));
    switch(objType){
      case 0:
        fossil_fatal("Cannot resolve artifact ID: %s", zObjId);
        break;
      case CFTYPE_MANIFEST:
        zPrefix = fRaw ? "" : "sym-";
        break;
      default: break;
    }
    db_begin_transaction();
    tag_add_artifact(zPrefix, zTag, zObjId, zValue,
                     1+fPropagate+dryRun,zDateOvrd,zUserOvrd);
    db_end_transaction(0);
  }else

  if( strncmp(g.argv[2],"branch",n)==0 ){
    fossil_fatal("the \"fossil tag branch\" command is discontinued\n"
                 "Use the \"fossil branch new\" command instead.");
  }else

  if( strncmp(g.argv[2],"cancel",n)==0 ){
    int dryRun = 0;
    int fRaw = find_option("raw","",0)!=0;
    const char *zPrefix = "";
    const char *zDateOvrd = find_option("date-override",0,1);
    const char *zUserOvrd = find_option("user-override",0,1);
    const char *zTag;
    const char *zObjId;
    int objType;
    if( find_option("dry-run","n",0)!=0 ) dryRun = TAG_ADD_DRYRUN;
    if( g.argc!=5 ){
      usage("cancel ?options? TAGNAME ARTIFACT-ID");
    }
    zTag = g.argv[3];
    tag_cmd_tagname_check(zTag);
    zObjId = g.argv[4];
    objType = whatis_rid_type(symbolic_name_to_rid(zObjId, 0));
    switch(objType){
      case 0:
        fossil_fatal("Cannot resolve artifact ID: %s", zObjId);
        break;
      case CFTYPE_MANIFEST:
        zPrefix = fRaw ? "" : "sym-";
        break;
      default: break;
    }
    db_begin_transaction();
    tag_add_artifact(zPrefix, zTag, zObjId, 0, dryRun,
                     zDateOvrd, zUserOvrd);
    db_end_transaction(0);
  }else

  if( strncmp(g.argv[2],"find",n)==0 ){
    Stmt q;
    int fRaw = find_option("raw","",0)!=0;
    const char *zFindLimit = find_option("limit","n",1);
    const int nFindLimit = zFindLimit ? atoi(zFindLimit) : -2000;
    const char *zType = find_option("type","t",1);
    Blob sql = empty_blob;
    if( zType==0 || zType[0]==0 ) zType = "*";
    if( g.argc!=4 ){
      usage("find ?--raw? ?-t|--type TYPE? ?-n|--limit #? TAGNAME");
    }
    if( fRaw ){
      blob_append_sql(&sql,
        "SELECT blob.uuid FROM tagxref, blob"
        " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
        "   AND tagxref.tagtype>0"
        "   AND blob.rid=tagxref.rid",
        g.argv[3]
      );
      if( nFindLimit>0 ){
        blob_append_sql(&sql, " LIMIT %d", nFindLimit);
      }
      db_prepare(&q, "%s", blob_sql_text(&sql));
      blob_reset(&sql);
      while( db_step(&q)==SQLITE_ROW ){
        fossil_print("%s\n", db_column_text(&q, 0));
      }
      db_finalize(&q);
    }else{
      int tagid = db_int(0, "SELECT tagid FROM tag "
                         "WHERE tagname='%s%q'",
                         (zType && 'c'==zType[0])
                         ? "sym-" : ""/*safe-for-%s*/,
                         g.argv[3]);
      if( tagid>0 ){
        blob_append_sql(&sql,
          "%s"
          "  AND event.type GLOB '%q'"
          "  AND blob.rid IN ("
                    " SELECT rid FROM tagxref"
                    "  WHERE tagtype>0 AND tagid=%d"
                    ")"
          " ORDER BY event.mtime DESC /*sort*/",
          timeline_query_for_tty(), zType, tagid
        );
        db_prepare(&q, "%s", blob_sql_text(&sql));
        blob_reset(&sql);
        print_timeline(&q, nFindLimit, 79, 0, 0);
        db_finalize(&q);
      }
    }
  }else

  if(( strncmp(g.argv[2],"list",n)==0 )||( strncmp(g.argv[2],"ls",n)==0 )){
    Stmt q;
    const int fRaw = find_option("raw","",0)!=0;
    const char *zTagType = find_option("tagtype","t",1);
    const int fInverse = find_option("inverse","v",0)!=0;
    const char *zTagPrefix = find_option("prefix","",1);
    int nTagType = fRaw ? -1 : 0;
    int fValues = find_option("values","",0)!=0;
    const char *zSep = find_option("sep","",1);


    if( zTagType!=0 ){
      int l = strlen(zTagType);
      if( strncmp(zTagType,"cancel",l)==0 ){
        nTagType = 0;
      }else if( strncmp(zTagType,"singleton",l)==0 ){
        nTagType = 1;
      }else if( strncmp(zTagType,"propagated",l)==0 ){
        nTagType = 2;
      }else{
        fossil_fatal("unrecognized tag type");
      }
    }
    if( g.argc==3 ){
      const int nTagPrefix = zTagPrefix ? (int)strlen(zTagPrefix) : 0;
      if( !fValues ){
        db_prepare(&q,
          "SELECT tagname FROM tag"
          " WHERE EXISTS(SELECT 1 FROM tagxref"
          "               WHERE tagid=tag.tagid"
          "                 AND tagtype%s%d)"
          " AND CASE WHEN %Q IS NULL THEN 1 ELSE tagname GLOB %Q||'*' "
          " END ORDER BY tagname COLLATE uintnocase",
          zTagType!=0 ? (fInverse!=0?"<>":"=") : ">"/*safe-for-%s*/,
          nTagType, zTagPrefix, zTagPrefix
        );
      }else{
        if( zSep ){
          db_prepare(&q,
            /* work around group_concat() with DISTINCT and custom separator */
            "SELECT tagname,"
            "  rtrim(replace(group_concat(DISTINCT value||'@!' "
            "                             ORDER BY value ASC), '@!,', %Q),'@!')"
            " FROM tagxref, tag"
            " WHERE tagxref.tagid=tag.tagid AND tagtype%s%d"
            " AND CASE WHEN %Q IS NULL THEN 1 ELSE tagname GLOB %Q||'*' END"
            " GROUP BY tagname"
            " ORDER BY tagname COLLATE uintnocase",
            ( zSep && strlen(zSep)>0 ) ? zSep : ",",
            zTagType!=0 ? (fInverse!=0?"<>":"=") : ">"/*safe-for-%s*/,
            nTagType, zTagPrefix, zTagPrefix
          );
        }else{
          db_prepare(&q,
            "SELECT DISTINCT tagname, value"
            " FROM tagxref, tag"
            " WHERE tagxref.tagid=tag.tagid AND tagtype%s%d"
            " AND CASE WHEN %Q IS NULL THEN 1 ELSE tagname GLOB %Q||'*' END"
            " ORDER BY tagname, value COLLATE uintnocase",
            zTagType!=0 ? (fInverse!=0?"<>":"=") : ">"/*safe-for-%s*/,
            nTagType, zTagPrefix, zTagPrefix
          );
        }
      }
      while( db_step(&q)==SQLITE_ROW ){
        const char *zName = db_column_text(&q, 0);
        const char *zValue = db_column_text(&q, 1);
        int nWidth = fValues ? 20 : 0;
        const char *zzValue = (fValues && zValue) ? mprintf(" %s", zValue) : "";
        if( fRaw ){
          fossil_print("%-*s%s\n", nWidth, zName, zzValue);
        }else if( nTagPrefix>0 ){
          assert(db_column_bytes(&q,0)>=nTagPrefix);
          fossil_print("%-*s%s\n", nWidth, &zName[nTagPrefix], zzValue);
        }else if( strncmp(zName, "sym-", 4)==0 ){
          fossil_print("%-*s%s\n", nWidth, &zName[4], zzValue);
        }
      }
      db_finalize(&q);
    }else if( g.argc==4 ){
      char const *zObjId = g.argv[3];
      const int rid = name_to_rid(zObjId);
      const int objType = whatis_rid_type(rid);
      int nTagOffset = 0;

      zTagPrefix = 0;
      if(objType<=0){
        fossil_fatal("Cannot resolve artifact ID: %s", zObjId);
      }else if(fRaw==0){
        /* Figure out the tag prefix to strip */
        switch(objType){
          case CFTYPE_MANIFEST: zTagPrefix = "sym-"; break;
          case CFTYPE_WIKI: zTagPrefix = "wiki-"; break;
          case CFTYPE_TICKET: zTagPrefix = "tkt-"; break;
          case CFTYPE_EVENT: zTagPrefix = "event-"; break;
          default: break;
        }
        if(zTagPrefix!=0){
          nTagOffset = (int)strlen(zTagPrefix);
        }
      }
      db_prepare(&q,
        "SELECT tagname, value FROM tagxref, tag"
        " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
        "   AND tagtype%s%d"
        " ORDER BY tagname COLLATE uintnocase",
        rid,
        zTagType!=0 ? (fInverse!=0?"<>":"=") : ">"/*safe-for-%s*/,
        nTagType
      );
      while( db_step(&q)==SQLITE_ROW ){
        const char *zName = db_column_text(&q, 0);
        const char *zValue = db_column_text(&q, 1);
        if( zTagPrefix && strncmp(zName, zTagPrefix, nTagOffset)==0 ){
          zName += nTagOffset;
        }
        if( zValue && zValue[0] ){
          fossil_print("%s=%s\n", zName, zValue);
        }else{
          fossil_print("%s\n", zName);
        }
      }
      db_finalize(&q);
    }else{
      usage("list ?OPTIONS? ?CHECK-IN?");
    }
  }else
  {
    goto tag_cmd_usage;
  }

  /* Cleanup */
  return;

tag_cmd_usage:
  usage("add|cancel|find|list ...");
}

/*
** COMMAND: reparent*
**
** Usage: %fossil reparent [OPTIONS] CHECK-IN PARENT ...
**
** Create a "parent" tag that causes CHECK-IN to be interpreted as a
** child of PARENT.  If multiple PARENTs are listed, then the first is
** the primary parent and others are merge ancestors.
**
** This is an experts-only command.  It is used to patch up a repository
** that has been damaged by a shun or that has been pieced together from
** two or more separate repositories.  You should never need to reparent
** during normal operations.
**
** Reparenting is accomplished by adding a parent tag.  So to undo the
** reparenting operation, simply delete the tag.
**
**    --test           Make database entries but do not add the tag artifact.
**                     So the reparent operation will be undone by the next
**                     "fossil rebuild" command.
**    -n|--dry-run     Print the tag that would have been created but do not
**                     actually change the database in any way.
**    --date-override DATETIME  Set the change time on the control artifact
**    --user-override USER      Set the user name on the control artifact
*/
void reparent_cmd(void){
  int bTest = find_option("test","",0)!=0;
  int rid;
  int i;
  Blob value;
  char *zUuid;
  int dryRun = 0;
  const char *zDateOvrd;  /* The change time on the control artifact */
  const char *zUserOvrd;  /* The user name on the control artifact */

  if( find_option("dry-run","n",0)!=0 ) dryRun = TAG_ADD_DRYRUN;
  zDateOvrd = find_option("date-override",0,1);
  zUserOvrd = find_option("user-override",0,1);
  db_find_and_open_repository(0, 0);
  verify_all_options();
  if( g.argc<4 ){
    usage("[OPTIONS] CHECK-IN PARENT ...");
  }
  rid = name_to_typed_rid(g.argv[2], "ci");
  blob_init(&value, 0, 0);
  for(i=3; i<g.argc; i++){
    int pid = name_to_typed_rid(g.argv[i], "ci");
    if( i>3 ) blob_append(&value, " ", 1);
    zUuid = rid_to_uuid(pid);
    blob_append(&value, zUuid, strlen(zUuid));
    fossil_free(zUuid);
  }
  if( bTest && !dryRun ){
    tag_insert("parent", 1, blob_str(&value), -1, 0.0, rid);
  }else{
    zUuid = rid_to_uuid(rid);
    tag_add_artifact("","parent",zUuid,blob_str(&value),1|dryRun,
                     zDateOvrd,zUserOvrd);
  }
}


/*
** WEBPAGE: taglist
**
** List all non-propagating symbolic tags.
*/
void taglist_page(void){
  Stmt q;

  login_check_credentials();
  if( !g.perm.Read ){
    login_needed(g.anon.Read);
  }
  cgi_check_for_malice();
  login_anonymous_available();
  style_header("Tags");
  style_adunit_config(ADUNIT_RIGHT_OK);
  style_submenu_element("Timeline", "tagtimeline");
  @ <h2>Non-propagating tags:</h2>
  @ <table class='sortable' data-column-types='ktn' data-init-sort='2'>
  @ <thead><tr>
  @ <th>Tag Name</th>
  @ <th>Most Recent</th>
  @ <th>Count</th>
  @ </tr></thead><tbody>

  db_prepare(&q,
    "SELECT substr(tagname,5),\n"
           "row_number()OVER(ORDER BY tagname COLLATE uintnocase),\n"
           "substr(datetime(max(event.mtime)),1,16),\n"
           "count(*)\n"
      "FROM tagxref JOIN tag USING(tagid)\n"
          " JOIN event ON event.objid=tagxref.rid\n"
     "WHERE tagname like 'sym-%%'\n"
       "AND tagxref.tagtype=1\n"
     "GROUP BY 1\n"
     "ORDER BY 3 DESC;\n"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    int rn = db_column_int(&q, 1);
    const char *zDate = db_column_text(&q, 2);
    int cnt = db_column_int(&q, 3);
    @ <tr><td data-sortkey="%06x(rn)">\
    if( g.perm.Hyperlink ){
      @ %z(chref("taglink","%R/timeline?t=%T",zName))%h(zName)</a></td>\
    }else{
      @ <span class="tagDsp">%h(zName)</span></td>\
    }
    @ <td>&nbsp;&nbsp;&nbsp;%h(zDate)&nbsp;&nbsp;&nbsp;</td>\
    @ <td align="center">%d(cnt)</td></tr>
  }
  @ </table>
  db_finalize(&q);
  style_table_sorter();
  style_finish_page();
}

/*
** WEBPAGE: /tagtimeline
**
** Render a timeline with all check-ins that contain non-propagating
** symbolic tags.
**
** Query parameters:
**
**     ng            No graph
**     nohidden      Hide check-ins with "hidden" tag
**     onlyhidden    Show only check-ins with "hidden" tag
**     brbg          Background color by branch name
**     ubg           Background color by user name
*/
void tagtimeline_page(void){
  Blob sql = empty_blob;
  Stmt q;
  int tmFlags;                            /* Timeline display flags */
  int fNoHidden = PB("nohidden")!=0;      /* The "nohidden" query parameter */
  int fOnlyHidden = PB("onlyhidden")!=0;  /* The "onlyhidden" query parameter */

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }

  style_header("Tagged Check-ins");
  style_submenu_element("List", "taglist");
  login_anonymous_available();
  timeline_ss_submenu();
  @ <h2>Check-ins with non-propagating tags:</h2>
  blob_append(&sql, timeline_query_for_www(), -1);
  blob_append_sql(&sql,
    "AND blob.rid IN (SELECT rid FROM tagxref"
    "                  WHERE tagtype=1 AND srcid>0"
    "                    AND tagid IN (SELECT tagid FROM tag "
    "                                   WHERE tagname GLOB 'sym-*'))");
  if( fNoHidden || fOnlyHidden ){
    const char* zUnaryOp = fNoHidden ? "NOT" : "";
    blob_append_sql(&sql,
      " AND %s EXISTS(SELECT 1 FROM tagxref"
      " WHERE tagid=%d AND tagtype>0 AND rid=blob.rid)\n",
      zUnaryOp/*safe-for-%s*/, TAG_HIDDEN);
  }
  db_prepare(&q, "%s ORDER BY event.mtime DESC /*sort*/", blob_sql_text(&sql));
  blob_reset(&sql);
  /* Always specify TIMELINE_DISJOINT, or graph_finish() may fail because of too
  ** many descenders to (off-screen) parents. */
  tmFlags = TIMELINE_XMERGE | TIMELINE_FILLGAPS | TIMELINE_NOSCROLL;
  if( PB("ng")==0 ) tmFlags |= TIMELINE_GRAPH;
  if( PB("brbg")!=0 ) tmFlags |= TIMELINE_BRCOLOR;
  if( PB("ubg")!=0 ) tmFlags |= TIMELINE_UCOLOR;
  www_print_timeline(&q, tmFlags, 0, 0, 0, 0, 0, 0);
  db_finalize(&q);
  @ <br>
  style_finish_page();
}

/*
** Returns true if the given blob.rid value has the given tag ID
** applied to it, else false.
*/
int rid_has_tag(int rid, int tagId){
  return db_exists(
     "SELECT tag.tagid FROM tagxref, tag"
     " WHERE tagxref.rid=%d AND tagtype>0 "
     " AND tag.tagid=%d"
     " AND tagxref.tagid=tag.tagid",
     rid, tagId
  );
}


/*
** Returns tagxref.rowid if the given blob.rid has a tagxref.rid entry
** of an active (non-cancelled) tag matching the given rid and tag
** name string, else returns 0. Note that this function does not
** distinguish between a non-existent tag and a cancelled tag.
**
** Design note: the return value is the tagxref.rowid because that
** gives us an easy way to fetch the value of the tag later on, if
** needed.
*/
int rid_has_active_tag_name(int rid, const char *zTagName){
  static Stmt q = empty_Stmt_m;
  int rc;

  assert( 0 != zTagName );
  if( !q.pStmt ){
    db_static_prepare(&q,
       "SELECT x.rowid FROM tagxref x, tag t"
       " WHERE x.rid=$rid AND x.tagtype>0 "
       " AND x.tagid=t.tagid"
       " AND t.tagname=$tagname"
    );
  }
  db_bind_int(&q, "$rid", rid);
  db_bind_text(&q, "$tagname", zTagName);
  rc = (SQLITE_ROW==db_step(&q)) ? db_column_int(&q, 0) : 0;
  db_reset(&q);
  return rc;
}
