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
** ancestor node.  If tagtype is 0 it means a branch tag is
** being cancelled.
*/
void tag_propagate(
  int pid,             /* Propagate the tag to children of this node */
  int tagid,           /* Tag to propagate */
  int tagType,         /* 2 for a propagating tag.  0 for an antitag */
  const char *zValue,  /* Value of the tag.  Might be NULL */
  double mtime         /* Timestamp on the tag */
){
  PQueue queue;
  Stmt s, ins, eventupdate;

  assert( tagType==0 || tagType==2 );
  pqueue_init(&queue);
  pqueue_insert(&queue, pid, 0.0);
  db_prepare(&s, 
     "SELECT cid, plink.mtime,"
     "       coalesce(srcid=0 AND tagxref.mtime<:mtime, %d) AS doit"
     "  FROM plink LEFT JOIN tagxref ON cid=rid AND tagid=%d"
     " WHERE pid=:pid AND isprim",
     tagType!=0, tagid
  );
  db_bind_double(&s, ":mtime", mtime);
  if( tagType==2 ){
    db_prepare(&ins,
       "REPLACE INTO tagxref(tagid, tagtype, srcid, value, mtime, rid)"
       "VALUES(%d,2,0,%Q,:mtime,:rid)",
       tagid, zValue
    );
    db_bind_double(&ins, ":mtime", mtime);
  }else{
    zValue = 0;
    db_prepare(&ins,
       "DELETE FROM tagxref WHERE tagid=%d AND rid=:rid", tagid
    );
  }
  if( tagid==TAG_BGCOLOR ){
    db_prepare(&eventupdate,
      "UPDATE event SET brbgcolor=%Q WHERE objid=:rid", zValue
    );
  }
  while( (pid = pqueue_extract(&queue))!=0 ){
    db_bind_int(&s, ":pid", pid);
    while( db_step(&s)==SQLITE_ROW ){
      int doit = db_column_int(&s, 2);
      if( doit ){
        int cid = db_column_int(&s, 0);
        double mtime = db_column_double(&s, 1);
        pqueue_insert(&queue, cid, mtime);
        db_bind_int(&ins, ":rid", cid);
        db_step(&ins);
        db_reset(&ins);
        if( tagid==TAG_BGCOLOR ){
          db_bind_int(&eventupdate, ":rid", cid);
          db_step(&eventupdate);
          db_reset(&eventupdate);
        }
      }
    }
    db_reset(&s);
  }
  pqueue_clear(&queue);
  db_finalize(&ins);
  db_finalize(&s);
  if( tagid==TAG_BGCOLOR ){
    db_finalize(&eventupdate);
  }
}

/*
** Propagate all propagatable tags in pid to its children.
*/
void tag_propagate_all(int pid){
  Stmt q;
  db_prepare(&q,
     "SELECT tagid, tagtype, mtime, value FROM tagxref"
     " WHERE rid=%d"
     "   AND (tagtype=0 OR tagtype=2)",
     pid
  );
  while( db_step(&q)==SQLITE_ROW ){
    int tagid = db_column_int(&q, 0);
    int tagtype = db_column_int(&q, 1);
    double mtime = db_column_double(&q, 2);
    const char *zValue = db_column_text(&q, 3);
    tag_propagate(pid, tagid, tagtype, zValue, mtime);
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
*/
void tag_insert(
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
    /* Another entry this is more recent already exists.  Do nothing */
    return;
  }
  db_prepare(&s, 
    "REPLACE INTO tagxref(tagid,tagtype,srcId,value,mtime,rid)"
    " VALUES(%d,%d,%d,%Q,:mtime,%d)",
    tagid, tagtype, srcId, zValue, rid
  );
  db_bind_double(&s, ":mtime", mtime);
  db_step(&s);
  db_finalize(&s);
  if( tagtype==0 ){
    zValue = 0;
  }
  zCol = 0;
  switch( tagid ){
    case TAG_BGCOLOR: {
      if( tagtype==1 ){
        zCol = "bgcolor";
      }else{
        zCol = "brbgcolor";
      }
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
  }
  if( zCol ){
    db_multi_exec("UPDATE event SET %s=%Q WHERE objid=%d", zCol, zValue, rid);
  }
  if( tagtype==0 || tagtype==2 ){
    tag_propagate(rid, tagid, tagtype, zValue, mtime);
  }
}


/*
** COMMAND: test-tag
** %fossil test-tag (+|*|-)TAGNAME ARTIFACT-ID ?VALUE?
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
  zValue = g.argc==5 ? g.argv[4] : 0;
  db_begin_transaction();
  tag_insert(zTag, tagtype, zValue, -1, 0.0, rid);
  db_end_transaction(0); 
}

/*
** Add a control record to the repository that either creates
** or cancels a tag.
*/
static void tag_add_artifact(
  const char *zPrefix,        /* Prefix to prepend to tag name */
  const char *zTagname,       /* The tag to add or cancel */
  const char *zObjName,       /* Name of object attached to */
  const char *zValue,         /* Value for the tag.  Might be NULL */
  int tagtype                 /* 0:cancel 1:singleton 2:propagated */
){
  int rid;
  int nrid;
  char *zDate;
  Blob uuid;
  Blob ctrl;
  Blob cksum;
  static const char zTagtype[] = { '-', '+', '*' };

  assert( tagtype>=0 && tagtype<=2 );
  user_select();
  blob_zero(&uuid);
  blob_append(&uuid, zObjName, -1);
  if( name_to_uuid(&uuid, 9) ){
    return;
  }
  rid = name_to_rid(blob_str(&uuid));
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
  zDate = db_text(0, "SELECT datetime('now')");
  zDate[10] = 'T';
  blob_appendf(&ctrl, "D %s\n", zDate);
  blob_appendf(&ctrl, "T %c%s%F %b",
               zTagtype[tagtype], zPrefix, zTagname, &uuid);
  if( tagtype>0 && zValue && zValue[0] ){
    blob_appendf(&ctrl, " %F\n", zValue);
  }else{
    blob_appendf(&ctrl, "\n");
  }
  blob_appendf(&ctrl, "U %F\n", g.zLogin);
  md5sum_blob(&ctrl, &cksum);
  blob_appendf(&ctrl, "Z %b\n", &cksum);
  db_begin_transaction();
  nrid = content_put(&ctrl, 0, 0);
  manifest_crosslink(nrid, &ctrl);
  db_end_transaction(0);
  
  /* Do an autosync push if requested */
  autosync(AUTOSYNC_PUSH);
}

/*
** COMMAND: tag
** Usage: %fossil tag SUBCOMMAND ...
**
** Run various subcommands to control tags and properties
**
**     %fossil tag add ?--raw? ?--propagate? TAGNAME CHECK-IN ?VALUE?
**
**         Add a new tag or property to CHECK-IN. The tag will
**         be usable instead of a CHECK-IN in commands such as
**         update and merge.  If the --propagate flag is present,
**         the tag value propages to all descendants of CHECK-IN
**
**     %fossil tag cancel ?--raw? TAGNAME CHECK-IN
**
**         Remove the tag TAGNAME from CHECK-IN, and also remove
**         the propagation of the tag to any descendants.
**
**     %fossil tag find ?--raw? TAGNAME
**
**         List all check-ins that use TAGNAME
**
**     %fossil tag list ?--raw? ?CHECK-IN?
**
**         List all tags, or if CHECK-IN is supplied, list
**         all tags and their values for CHECK-IN.
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
  int fRaw = find_option("raw","",0)!=0;
  int fPropagate = find_option("propagate","",0)!=0;
  const char *zPrefix = fRaw ? "" : "sym-";

  db_find_and_open_repository(1);
  if( g.argc<3 ){
    goto tag_cmd_usage;
  }
  n = strlen(g.argv[2]);
  if( n==0 ){
    goto tag_cmd_usage;
  }

  if( strncmp(g.argv[2],"add",n)==0 ){
    char *zValue;
    if( g.argc!=5 && g.argc!=6 ){
      usage("add ?--raw? ?--propagate? TAGNAME CHECK-IN ?VALUE?");
    }
    zValue = g.argc==6 ? g.argv[5] : 0;
    tag_add_artifact(zPrefix, g.argv[3], g.argv[4], zValue, 1+fPropagate);
  }else

  if( strncmp(g.argv[2],"branch",n)==0 ){
    fossil_fatal("the \"fossil tag branch\" command is discontinued\n"
                 "Use the \"fossil branch new\" command instead.");
  }else

  if( strncmp(g.argv[2],"cancel",n)==0 ){
    if( g.argc!=5 ){
      usage("cancel ?--raw? TAGNAME CHECK-IN");
    }
    tag_add_artifact(zPrefix, g.argv[3], g.argv[4], 0, 0);
  }else

  if( strncmp(g.argv[2],"find",n)==0 ){
    Stmt q;
    if( g.argc!=4 ){
      usage("find ?--raw? TAGNAME");
    }
    if( fRaw ){
      db_prepare(&q,
        "SELECT blob.uuid FROM tagxref, blob"
        " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%Q)"
        "   AND tagxref.tagtype>0"
        "   AND blob.rid=tagxref.rid",
        g.argv[3]
      );
      while( db_step(&q)==SQLITE_ROW ){
        printf("%s\n", db_column_text(&q, 0));
      }
      db_finalize(&q);
    }else{
      int tagid = db_int(0, "SELECT tagid FROM tag WHERE tagname='sym-%q'",
                         g.argv[3]);
      if( tagid>0 ){
        db_prepare(&q,
          "%s"
          "  AND blob.rid IN ("
                    " SELECT rid FROM tagxref"
                    "  WHERE tagtype>0 AND tagid=%d"
                    ")"
          " ORDER BY event.mtime DESC",
          timeline_query_for_tty(), tagid
        );
        print_timeline(&q, 2000);
        db_finalize(&q);
      }
    }
  }else

  if( strncmp(g.argv[2],"list",n)==0 ){
    Stmt q;
    if( g.argc==3 ){
      db_prepare(&q, 
        "SELECT tagname FROM tag"
        " WHERE EXISTS(SELECT 1 FROM tagxref"
        "               WHERE tagid=tag.tagid"
        "                 AND tagtype>0)"
        " ORDER BY tagname"
      );
      while( db_step(&q)==SQLITE_ROW ){
        const char *zName = db_column_text(&q, 0);
        if( fRaw ){
          printf("%s\n", zName);
        }else if( strncmp(zName, "sym-", 4)==0 ){
          printf("%s\n", &zName[4]);
        }
      }
      db_finalize(&q);
    }else if( g.argc==4 ){
      int rid = name_to_rid(g.argv[3]);
      db_prepare(&q,
        "SELECT tagname, value FROM tagxref, tag"
        " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
        "   AND tagtype>%d"
        " ORDER BY tagname",
        rid,
        fRaw ? -1 : 0
      );
      while( db_step(&q)==SQLITE_ROW ){
        const char *zName = db_column_text(&q, 0);
        const char *zValue = db_column_text(&q, 1);
        if( fRaw==0 ){
          if( strncmp(zName, "sym-", 4)!=0 ) continue;
          zName += 4;
        }
        if( zValue && zValue[0] ){
          printf("%s=%s\n", zName, zValue);
        }else{
          printf("%s\n", zName);
        }
      }
      db_finalize(&q);
    }else{
      usage("tag list ?CHECK-IN?");
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
