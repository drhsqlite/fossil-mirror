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
** This file contains code used to cross link control files and
** manifests.  The file is named "manifest.c" because it was
** original only used to parse manifests.  Then later clusters
** and control files were added.
*/
#include "config.h"
#include "manifest.h"
#include <assert.h>

#if INTERFACE
/*
** Types of control files
*/
#define CFTYPE_MANIFEST   1
#define CFTYPE_CLUSTER    2
#define CFTYPE_CONTROL    3

/*
** A parsed manifest or cluster.
*/
struct Manifest {
  Blob content;         /* The original content blob */
  int type;             /* Type of file */
  char *zComment;       /* Decoded comment */
  char zUuid[UUID_SIZE+1];  /* Self UUID */
  double rDate;         /* Time in the "D" line */
  char *zUser;          /* Name of the user */
  char *zRepoCksum;     /* MD5 checksum of the baseline content */
  int nFile;            /* Number of F lines */
  int nFileAlloc;       /* Slots allocated in aFile[] */
  struct { 
    char *zName;           /* Name of a file */
    char *zUuid;           /* UUID of the file */
  } *aFile;
  int nParent;          /* Number of parents */
  int nParentAlloc;     /* Slots allocated in azParent[] */
  char **azParent;      /* UUIDs of parents */
  int nCChild;          /* Number of cluster children */
  int nCChildAlloc;     /* Number of closts allocated in azCChild[] */
  char **azCChild;      /* UUIDs of referenced objects in a cluster */
  int nTag;             /* Number of T lines */
  int nTagAlloc;        /* Slots allocated in aTag[] */
  struct { 
    char *zName;           /* Name of the tag */
    char *zUuid;           /* UUID that the tag is applied to */
    char *zValue;          /* Value if the tag is really a property */
  } *aTag;
};
#endif


/*
** Clear the memory allocated in a manifest object
*/
void manifest_clear(Manifest *p){
  blob_reset(&p->content);
  free(p->aFile);
  free(p->azParent);
  free(p->azCChild);
  memset(p, 0, sizeof(*p));
}

/*
** Parse a manifest blob into a Manifest object.  The Manifest
** object takes over the input blob and will free it when the
** Manifest object is freed.  Zeros are inserted into the blob
** as string terminators so that blob should not be used again.
**
** Return TRUE if the content really is a manifest.  Return FALSE
** if there are syntax errors.
**
** The pContent is reset.  If TRUE is returned, then pContent will
** be reset when the Manifest object is cleared.  If FALSE is
** returned then the Manifest object is cleared automatically
** and pContent is reset before the return.
*/
int manifest_parse(Manifest *p, Blob *pContent){
  int seenHeader = 0;
  int i;
  Blob line, token, a1, a2, a3;
  Blob selfuuid;
  char cPrevType = 0;

  memset(p, 0, sizeof(*p));
  memcpy(&p->content, pContent, sizeof(p->content));
  sha1sum_blob(&p->content, &selfuuid);
  memcpy(p->zUuid, blob_buffer(&selfuuid), UUID_SIZE);
  p->zUuid[UUID_SIZE] = 0;
  blob_zero(pContent);
  pContent = &p->content;

  blob_zero(&a1);
  blob_zero(&a2);
  md5sum_init();
  while( blob_line(pContent, &line) ){
    char *z = blob_buffer(&line);
    if( z[0]=='-' ){
      if( strncmp(z, "-----BEGIN PGP ", 15)!=0 ){
        goto manifest_syntax_error;
      }
      if( seenHeader ){
        break;
      }
      while( blob_line(pContent, &line)>2 ){}
      if( blob_line(pContent, &line)==0 ) break;
      z = blob_buffer(&line);
    }
    if( z[0]<cPrevType ){
      /* Lines of a manifest must occur in lexicographical order */
      goto manifest_syntax_error;
    }
    cPrevType = z[0];
    seenHeader = 1;
    if( blob_token(&line, &token)!=1 ) goto manifest_syntax_error;
    switch( z[0] ){
      /*
      **     C <comment>
      **
      ** Comment text is fossil-encoded.  There may be no more than
      ** one C line.  C lines are required for manifests and are
      ** disallowed on all other control files.
      */
      case 'C': {
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( p->zComment!=0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        p->zComment = blob_terminate(&a1);
        defossilize(p->zComment);
        break;
      }

      /*
      **     D <timestamp>
      **
      ** The timestamp should be ISO 8601.   YYYY-MM-DDtHH:MM:SS
      ** There can be no more than 1 D line.  D lines are required
      ** for all control files except for clusters.
      */
      case 'D': {
        char *zDate;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( p->rDate!=0.0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        zDate = blob_terminate(&a1);
        p->rDate = db_double(0.0, "SELECT julianday(%Q)", zDate);
        break;
      }

      /*
      **     F <filename> <uuid>
      **
      ** Identifies a file in a manifest.  Multiple F lines are
      ** allowed in a manifest.  F lines are not allowed in any
      ** other control file.  The filename is fossil-encoded.
      */
      case 'F': {
        char *zName, *zUuid;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a3)!=0 ) goto manifest_syntax_error;
        zName = blob_terminate(&a1);
        zUuid = blob_terminate(&a2);
        if( blob_size(&a2)!=UUID_SIZE ) goto manifest_syntax_error;
        if( !validate16(zUuid, UUID_SIZE) ) goto manifest_syntax_error;
        defossilize(zName);
        if( !file_is_simple_pathname(zName) ){
          goto manifest_syntax_error;
        }
        if( p->nFile>=p->nFileAlloc ){
          p->nFileAlloc = p->nFileAlloc*2 + 10;
          p->aFile = realloc(p->aFile, p->nFileAlloc*sizeof(p->aFile[0]) );
          if( p->aFile==0 ) fossil_panic("out of memory");
        }
        i = p->nFile++;
        p->aFile[i].zName = zName;
        p->aFile[i].zUuid = zUuid;
        if( i>0 && strcmp(p->aFile[i-1].zName, zName)>=0 ){
          goto manifest_syntax_error;
        }
        break;
      }

      /*
      **    M <uuid>
      **
      ** An M-line identifies another artifact by its UUID.  M-lines
      ** occur in clusters only.
      */
      case 'M': {
        char *zUuid;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        zUuid = blob_terminate(&a1);
        if( blob_size(&a1)!=UUID_SIZE ) goto manifest_syntax_error;
        if( !validate16(zUuid, UUID_SIZE) ) goto manifest_syntax_error;
        if( p->nCChild>=p->nCChildAlloc ){
          p->nCChildAlloc = p->nCChildAlloc*2 + 10;
          p->azCChild = 
             realloc(p->azCChild, p->nCChildAlloc*sizeof(p->azCChild[0]) );
          if( p->azCChild==0 ) fossil_panic("out of memory");
        }
        i = p->nCChild++;
        p->azCChild[i] = zUuid;
        if( i>0 && strcmp(p->azCChild[i-1], zUuid)>=0 ){
          goto manifest_syntax_error;
        }
        break;
      }

      /*
      **    T (+|*|-)<tagname> <uuid> ?<value>?
      **
      ** Create or cancel a tag or property.  The tagname is fossil-encoded.
      ** The first character of the name must be either "+" to create a
      ** singleton tag, "*" to create a propagating tag, or "-" to create
      ** anti-tag that undoes a prior "+" or blocks propagation of of
      ** a "*".
      **
      ** The tag is applied to <uuid>.  If <uuid> is "*" then the tag is
      ** applied to the current manifest.  If <value> is provided then 
      ** the tag is really a property with the given value.
      **
      ** Tags are not allowed in clusters.  Multiple T lines are allowed.
      */
      case 'T': {
        char *zName, *zUuid, *zValue;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)==0 ) goto manifest_syntax_error;
        zName = blob_terminate(&a1);
        zUuid = blob_terminate(&a2);
        if( blob_token(&line, &a3)==0 ){
          zValue = 0;
        }else{
          zValue = blob_terminate(&a3);
          defossilize(zValue);
        }
        if( blob_size(&a2)==UUID_SIZE && validate16(zUuid, UUID_SIZE) ){
          /* A valid uuid */
        }else if( blob_size(&a2)==1 && zUuid[0]=='*' ){
          zUuid = p->zUuid;
        }else{
          goto manifest_syntax_error;
        }
        defossilize(zName);
        if( zName[0]!='-' && zName[0]!='+' && zName[0]!='*' ){
          goto manifest_syntax_error;
        }
        if( validate16(&zName[1], strlen(&zName[1])) ){
          /* Do not allow tags whose names look like UUIDs */
          goto manifest_syntax_error;
        }
        if( p->nTag>=p->nTagAlloc ){
          p->nTagAlloc = p->nTagAlloc*2 + 10;
          p->aTag = realloc(p->aTag, p->nTagAlloc*sizeof(p->aTag[0]) );
          if( p->aTag==0 ) fossil_panic("out of memory");
        }
        i = p->nTag++;
        p->aTag[i].zName = zName;
        p->aTag[i].zUuid = zUuid;
        p->aTag[i].zValue = zValue;
        if( i>0 && strcmp(p->aTag[i-1].zName, zName)>=0 ){
          goto manifest_syntax_error;
        }
        break;
      }

      /*
      **     U <login>
      **
      ** Identify the user who created this control file by their
      ** login.  Only one U line is allowed.  Prohibited in clusters.
      */
      case 'U': {
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( p->zUser!=0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        p->zUser = blob_terminate(&a1);
        defossilize(p->zUser);
        break;
      }

      /*
      **     R <md5sum>
      **
      ** Specify the MD5 checksum of the entire baseline in a
      ** manifest.
      */
      case 'R': {
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( p->zRepoCksum!=0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        if( blob_size(&a1)!=32 ) goto manifest_syntax_error;
        p->zRepoCksum = blob_terminate(&a1);
        if( !validate16(p->zRepoCksum, 32) ) goto manifest_syntax_error;
        break;
      }

      /*
      **     P <uuid> ...
      **
      ** Specify one or more other artifacts where are the parents of
      ** this artifact.  The first parent is the primary parent.  All
      ** others are parents by merge.
      */
      case 'P': {
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        while( blob_token(&line, &a1) ){
          char *zUuid;
          if( blob_size(&a1)!=UUID_SIZE ) goto manifest_syntax_error;
          zUuid = blob_terminate(&a1);
          if( !validate16(zUuid, UUID_SIZE) ) goto manifest_syntax_error;
          if( p->nParent>=p->nParentAlloc ){
            p->nParentAlloc = p->nParentAlloc*2 + 5;
            p->azParent = realloc(p->azParent, p->nParentAlloc*sizeof(char*));
            if( p->azParent==0 ) fossil_panic("out of memory");
          }
          i = p->nParent++;
          p->azParent[i] = zUuid;
        }
        break;
      }

      /*
      **     Z <md5sum>
      **
      ** MD5 checksum on this control file.  The checksum is over all
      ** lines (other than PGP-signature lines) prior to the current
      ** line.  This must be the last record.
      */
      case 'Z': {
        int rc;
        Blob hash;
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        if( blob_size(&a1)!=32 ) goto manifest_syntax_error;
        if( !validate16(blob_buffer(&a1), 32) ) goto manifest_syntax_error;
        md5sum_finish(&hash);
        rc = blob_compare(&hash, &a1);
        blob_reset(&hash);
        if( rc!=0 ) goto manifest_syntax_error;
        break;
      }
      default: {
        goto manifest_syntax_error;
      }
    }
  }
  if( !seenHeader ) goto manifest_syntax_error;

  if( p->nFile>0 ){
    if( p->nCChild>0 ) goto manifest_syntax_error;
    if( p->rDate==0.0 ) goto manifest_syntax_error;
    p->type = CFTYPE_MANIFEST;
  }else if( p->nCChild>0 ){
    if( p->rDate>0.0 ) goto manifest_syntax_error;
    if( p->zComment!=0 ) goto manifest_syntax_error;
    if( p->zUser!=0 ) goto manifest_syntax_error;
    if( p->nTag>0 ) goto manifest_syntax_error;
    if( p->nParent>0 ) goto manifest_syntax_error;
    if( p->zRepoCksum!=0 ) goto manifest_syntax_error;
    p->type = CFTYPE_CLUSTER;
  }else if( p->nTag>0 ){
    if( p->rDate<=0.0 ) goto manifest_syntax_error;
    if( p->zRepoCksum!=0 ) goto manifest_syntax_error;
    if( p->nParent>0 ) goto manifest_syntax_error;
    p->type = CFTYPE_CONTROL;
  }else{
    goto manifest_syntax_error;
  }
    
  md5sum_init();
  return 1;

manifest_syntax_error:
  md5sum_init();
  manifest_clear(p);
  return 0;
}

/*
** Add a single entry to the mlink table.  Also add the filename to
** the filename table if it is not there already.
*/
static void add_one_mlink(
  int mid,                  /* The record ID of the manifest */
  const char *zFromUuid,    /* UUID for the mlink.pid field */
  const char *zToUuid,      /* UUID for the mlink.fid field */
  const char *zFilename     /* Filename */
){
  int fnid, pid, fid;

  fnid = db_int(0, "SELECT fnid FROM filename WHERE name=%Q", zFilename);
  if( fnid==0 ){
    db_multi_exec("INSERT INTO filename(name) VALUES(%Q)", zFilename);
    fnid = db_last_insert_rowid();
  }
  if( zFromUuid==0 ){
    pid = 0;
  }else{
    pid = uuid_to_rid(zFromUuid, 1);
  }
  if( zToUuid==0 ){
    fid = 0;
  }else{
    fid = uuid_to_rid(zToUuid, 1);
  }
  db_multi_exec(
    "INSERT INTO mlink(mid,pid,fid,fnid)"
    "VALUES(%d,%d,%d,%d)", mid, pid, fid, fnid
  );
  if( pid && fid ){
    content_deltify(pid, fid, 0);
  }
}

/*
** Add mlink table entries associated with manifest cid.
** There is an mlink entry for every file that changed going
** from pid to cid.
**
** Deleted files have mlink.fid=0.
** Added files have mlink.pid=0.
** Edited files have both mlink.pid!=0 and mlink.fid!=0
*/
static void add_mlink(int pid, Manifest *pParent, int cid, Manifest *pChild){
  Manifest other;
  Blob otherContent;
  int i, j;

  if( db_exists("SELECT 1 FROM mlink WHERE mid=%d", cid) ){
    return;
  }
  assert( pParent==0 || pChild==0 );
  if( pParent==0 ){
    pParent = &other;
    content_get(pid, &otherContent);
  }else{
    pChild = &other;
    content_get(cid, &otherContent);
  }
  if( blob_size(&otherContent)==0 ) return;
  if( manifest_parse(&other, &otherContent)==0 ) return;
  content_deltify(pid, cid, 0);
  for(i=j=0; i<pParent->nFile && j<pChild->nFile; ){
    int c = strcmp(pParent->aFile[i].zName, pChild->aFile[j].zName);
    if( c<0 ){
      add_one_mlink(cid, pParent->aFile[i].zUuid, 0, pParent->aFile[i].zName);
      i++;
    }else if( c>0 ){
      add_one_mlink(cid, 0, pChild->aFile[j].zUuid, pChild->aFile[j].zName);
      j++;
    }else{
      if( strcmp(pParent->aFile[i].zUuid, pChild->aFile[j].zUuid)!=0 ){
      add_one_mlink(cid, pParent->aFile[i].zUuid, pChild->aFile[j].zUuid, 
                    pChild->aFile[j].zName);
      }
      i++;
      j++;
    }
  }
  while( i<pParent->nFile ){
    add_one_mlink(cid, pParent->aFile[i].zUuid, 0, pParent->aFile[i].zName);
    i++;
  }
  while( j<pChild->nFile ){
    add_one_mlink(cid, 0, pChild->aFile[j].zUuid, pChild->aFile[j].zName);
    j++;
  }
  manifest_clear(&other);
}

/*
** Scan record rid/pContent to see if it is a manifest.  If
** it is a manifest, then populate the mlink, plink,
** filename, and event tables with cross-reference information.
**
** (Later:) Also check to see if pContent is a cluster.  If it
** is a cluster then remove all referenced elements from the
** unclustered table and create phantoms for any unknown elements.
*/
int manifest_crosslink(int rid, Blob *pContent){
  int i;
  Manifest m;
  Stmt q;

  if( manifest_parse(&m, pContent)==0 ){
    return 0;
  }
  db_begin_transaction();
  if( m.type==CFTYPE_MANIFEST ){
    if( !db_exists("SELECT 1 FROM mlink WHERE mid=%d", rid) ){
      for(i=0; i<m.nParent; i++){
        int pid = uuid_to_rid(m.azParent[i], 1);
        db_multi_exec("INSERT OR IGNORE INTO plink(pid, cid, isprim, mtime)"
                      "VALUES(%d, %d, %d, %.17g)", pid, rid, i==0, m.rDate);
        if( i==0 ){
          add_mlink(pid, 0, rid, &m);
        }
      }
      db_prepare(&q, "SELECT cid FROM plink WHERE pid=%d AND isprim", rid);
      while( db_step(&q)==SQLITE_ROW ){
        int cid = db_column_int(&q, 0);
        add_mlink(rid, &m, cid, 0);
      }
      db_finalize(&q);
      db_multi_exec(
        "INSERT INTO event(type,mtime,objid,user,comment)"
        "VALUES('ci',%.17g,%d,%Q,%Q)",
        m.rDate, rid, m.zUser, m.zComment
      );
    }
  }
  if( m.type==CFTYPE_CLUSTER ){
    for(i=0; i<m.nCChild; i++){
      int mid;
      mid = uuid_to_rid(m.azCChild[i], 1);
      if( mid>0 ){
        db_multi_exec("DELETE FROM unclustered WHERE rid=%d", mid);
      }
    }
  }
  if( m.type==CFTYPE_CONTROL || m.type==CFTYPE_MANIFEST ){
    for(i=0; i<m.nTag; i++){
      int tid;
      int type;
      tid = uuid_to_rid(m.aTag[i].zUuid, 1);
      switch( m.aTag[i].zName[0] ){
        case '+':  type = 1; break;
        case '*':  type = 2; break;
        case '-':  type = 0; break;
      }
      tag_insert(&m.aTag[i].zName[1], type, m.aTag[i].zValue, 
                 rid, m.rDate, tid);
    }
  }
  db_end_transaction(0);
  manifest_clear(&m);
  return 1;
}
