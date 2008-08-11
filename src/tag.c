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
** %fossil test-tag (+|*|-)TAGNAME UUID ?VALUE?
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
    usage("TAGNAME UUID ?VALUE?");
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
** Prepare an artifact that describes a fork from a certain UUID.
** Furthermore a propagating symbolic tag will be inserted and
** all other propagating symbolic tags will be cancelled.
**
** The changes are appended at the Blob pCtrl. However the manifest
** is not complete at that stage.
*/
static void tag_prepare_fork(
  Blob *pCtrl, 
  const char *zTagname,
  int rid
){
  Stmt q;
  Manifest origin;
  Blob originContent;
  char *zDate;
  int i;

  blob_appendf(pCtrl, "C Create\\snamed\\sfork\\s%s\n", zTagname+4);
  content_get(rid, &originContent);
  manifest_parse(&origin, &originContent);
  zDate = db_text(0, "SELECT datetime('now')");
  zDate[10] = 'T';
  blob_appendf(pCtrl, "D %s\n", zDate);
  for(i=0; i<origin.nFile; ++i){
    blob_appendf(pCtrl, "F %s %s %s\n",
                 origin.aFile[i].zName,
                 origin.aFile[i].zUuid,
                 origin.aFile[i].zPerm);
  }
  if( origin.nParent>0 ){
    blob_appendf(pCtrl, "P %s\n", origin.azParent[0]);
  }
  blob_appendf(pCtrl, "R %s\n", origin.zRepoCksum);
  blob_appendf(pCtrl, "T *%F *", zTagname);

  /* Cancel any sym- tags that propagate */
  db_prepare(&q,
      "SELECT tagname FROM tagxref, tag"
      " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
      "   AND tagtype>0 AND tagname LIKE 'sym-%%'"
      " ORDER BY tagname",
      rid);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zTag = db_column_text(&q, 0);
    blob_appendf(pCtrl, "\nT -%s *", zTag);
  }
  db_finalize(&q);

  /* Cleanup */
  manifest_clear(&origin);
}

/*
** Add a control record to the repository that either creates
** or cancels a tag.
*/
static void tag_add_artifact(
  const char *zTagname,       /* The tag to add or cancel */
  const char *zObjName,       /* Name of object attached to */
  const char *zValue,         /* Value for the tag.  Might be NULL */
  int tagtype,                /* 0:cancel 1:singleton 2:propagated */
  int fork                    /* Should a fork created from zObjName? */
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

  if( validate16(zTagname, strlen(zTagname)) ){
    fossil_fatal("invalid tag name \"%s\" - might be confused with a UUID",
                 zTagname);
  }
  if( fork ){
    tag_prepare_fork(&ctrl, zTagname, rid);
  }else{
    zDate = db_text(0, "SELECT datetime('now')");
    zDate[10] = 'T';
    blob_appendf(&ctrl, "D %s\n", zDate);
    blob_appendf(&ctrl, "T %c%F %b", zTagtype[tagtype], zTagname, &uuid);
  }
  if( tagtype && zValue && zValue[0] ){
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
**     %fossil tag add ?--raw? TAGNAME UUID ?VALUE?
**
**         Add a new tag or property to UUID. The tag will
**         be usable instead of a UUID in commands like
**         update and such.
**
**     %fossil tag branch ?--raw? TAGNAME UUID ?VALUE?
**
**         A fork of UUID will be created. Then the new tag
**         or property will be added to the fork that
**         propagate to all direct children.
**
**         Additionally all symbolic tags of that fork
**         inherited from UUID will be cancelled.
**
**         However, if the option '--raw' was given, no
**         fork will be created but the tag/property will be
**         added to UUID directly and no tag will be
**         canceled.
**
**         Please see the description of '--raw' below too.
**
**     %fossil tag cancel ?--raw? TAGNAME UUID
**
**         Cancel the tag TAGNAME from UUID
**
**     %fossil tag find ?--raw? TAGNAME
**
**         List all baselines that use TAGNAME
**
**     %fossil tag list ?--raw? ?UUID?
**
**         List all tags, or if UUID is supplied, list
**         all tags and their values for UUID.
**
** The option ?--raw? is to expose the internal interface 
** for tag handling. This option is not necessary for the
** normal use.
**
** If you use a tagname that might be confused with a UUID,
** you have to explicitly disambiguate it by prefixing it
** with "tag:". For instance:
**
**   fossil update cfcfcfee
**
** is not the same as:
**
**   fossil update tag:cfcfcfee
**
** The first will be taken as UUID and fossil will complain
** if no such revision was found, and the second one expect
** "cfcfcfee" to be a tag/branch name!
**
*/
void tag_cmd(void){
  int n;
  int raw = find_option("raw","",0)!=0;
  const char *prefix = raw ? "" : "sym-";
  int preflen = strlen(prefix);
  Blob tagname;

  db_find_and_open_repository(1);
  if( g.argc<3 ){
    goto tag_cmd_usage;
  }
  n = strlen(g.argv[2]);
  if( n==0 ){
    goto tag_cmd_usage;
  }

  blob_set(&tagname, prefix);

  if( strncmp(g.argv[2],"add",n)==0 ){
    char *zValue;
    if( g.argc!=5 && g.argc!=6 ){
      usage("add ?--raw? TAGNAME UUID ?VALUE?");
    }
    blob_append(&tagname, g.argv[3], strlen(g.argv[3]));
    zValue = g.argc==6 ? g.argv[5] : 0;
    tag_add_artifact(blob_str(&tagname), g.argv[4], zValue, 1, 0);
  }else

  if( strncmp(g.argv[2],"branch",n)==0 ){
    char *zValue;
    if( g.argc!=5 && g.argc!=6 ){
      usage("branch ?--raw? TAGNAME UUID ?VALUE?");
    }
    blob_append(&tagname, g.argv[3], strlen(g.argv[3]));
    zValue = g.argc==6 ? g.argv[5] : 0;
    tag_add_artifact(blob_str(&tagname), g.argv[4], zValue, 2, raw==0);
    if( !raw ){
      const char *zUuid = db_text(0, "SELECT uuid, MAX(rowid) FROM blob");
      printf("New_Fork \"%s\": %s\n", g.argv[3], zUuid);
    }
  }else

  if( strncmp(g.argv[2],"cancel",n)==0 ){
    if( g.argc!=5 ){
      usage("cancel ?--raw? TAGNAME UUID");
    }
    blob_append(&tagname, g.argv[3], strlen(g.argv[3]));
    tag_add_artifact(blob_str(&tagname), g.argv[4], 0, 0, 0);
  }else

  if( strncmp(g.argv[2],"find",n)==0 ){
    Stmt q;
    if( g.argc!=4 ){
      usage("find ?--raw? TAGNAME");
    }
    blob_append(&tagname, g.argv[3], strlen(g.argv[3]));
    db_prepare(&q,
      "SELECT blob.uuid FROM tagxref, blob"
      " WHERE tagid=(SELECT tagid FROM tag WHERE tagname=%B)"
      "   AND blob.rid=tagxref.rid", &tagname
    );
    while( db_step(&q)==SQLITE_ROW ){
      printf("%s\n", db_column_text(&q, 0));
    }
    db_finalize(&q);
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
        const char *name = db_column_text(&q, 0);
        if( raw || strncmp(name, prefix, preflen)==0 ){
          printf("%s\n", name+preflen);
        }
      }
      db_finalize(&q);
    }else if( g.argc==4 ){
      int rid = name_to_rid(g.argv[3]);
      db_prepare(&q,
        "SELECT tagname, value FROM tagxref, tag"
        " WHERE tagxref.rid=%d AND tagxref.tagid=tag.tagid"
        "   AND tagtype>0"
        " ORDER BY tagname",
        rid
      );
      while( db_step(&q)==SQLITE_ROW ){
        const char *zName = db_column_text(&q, 0);
        const char *zValue = db_column_text(&q, 1);
        if( zValue ){
          if( raw || strncmp(zName, prefix, preflen)==0 ){
            printf("%s=%s\n", zName+preflen, zValue);
          }
        }else{
          if( raw || strncmp(zName, prefix, preflen)==0 ){
            printf("%s\n", zName+preflen);
          }
        }
      }
      db_finalize(&q);
    }else{
      usage("tag list ?UUID?");
    }
  }else
  {
    goto tag_cmd_usage;
  }

  /* Cleanup */
  blob_reset(&tagname);
  return;

tag_cmd_usage:
  usage("add|branch|delete|find|list ...");
}
