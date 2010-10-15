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
** This file contains code used to cross link control files and
** manifests.  The file is named "manifest.c" because it was
** original only used to parse manifests.  Then later clusters
** and control files and wiki pages and tickets were added.
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
#define CFTYPE_WIKI       4
#define CFTYPE_TICKET     5
#define CFTYPE_ATTACHMENT 6
#define CFTYPE_EVENT      7

/*
** A parsed manifest or cluster.
*/
struct Manifest {
  Blob content;         /* The original content blob */
  int type;             /* Type of artifact.  One of CFTYPE_xxxxx */
  char *zComment;       /* Decoded comment.  The C card. */
  double rDate;         /* Date and time from D card.  0.0 if no D card. */
  char *zUser;          /* Name of the user from the U card. */
  char *zRepoCksum;     /* MD5 checksum of the baseline content.  R card. */
  char *zWiki;          /* Text of the wiki page.  W card. */
  char *zWikiTitle;     /* Name of the wiki page. L card. */
  double rEventDate;    /* Date of an event.  E card. */
  char *zEventId;       /* UUID for an event.  E card. */
  char *zTicketUuid;    /* UUID for a ticket. K card. */
  char *zAttachName;    /* Filename of an attachment. A card. */
  char *zAttachSrc;     /* UUID of document being attached. A card. */
  char *zAttachTarget;  /* Ticket or wiki that attachment applies to.  A card */
  int nFile;            /* Number of F cards */
  int nFileAlloc;       /* Slots allocated in aFile[] */
  struct ManifestFile { 
    char *zName;           /* Name of a file */
    char *zUuid;           /* UUID of the file */
    char *zPerm;           /* File permissions */
    char *zPrior;          /* Prior name if the name was changed */
    int iRename;           /* index of renamed name in prior/next manifest */
  } *aFile;             /* One entry for each F card */
  int nParent;          /* Number of parents. */
  int nParentAlloc;     /* Slots allocated in azParent[] */
  char **azParent;      /* UUIDs of parents.  One for each P card argument */
  int nCChild;          /* Number of cluster children */
  int nCChildAlloc;     /* Number of closts allocated in azCChild[] */
  char **azCChild;      /* UUIDs of referenced objects in a cluster. M cards */
  int nTag;             /* Number of T Cards */
  int nTagAlloc;        /* Slots allocated in aTag[] */
  struct { 
    char *zName;           /* Name of the tag */
    char *zUuid;           /* UUID that the tag is applied to */
    char *zValue;          /* Value if the tag is really a property */
  } *aTag;              /* One for each T card */
  int nField;           /* Number of J cards */
  int nFieldAlloc;      /* Slots allocated in aField[] */
  struct { 
    char *zName;           /* Key or field name */
    char *zValue;          /* Value of the field */
  } *aField;            /* One for each J card */
};
#endif

/*
** A cache of parsed manifests.  This reduces the number of
** calls to manifest_parse() when doing a rebuild.
*/
#define MX_MANIFEST_CACHE 4
static struct {
  int nxAge;
  int aRid[MX_MANIFEST_CACHE];
  int aAge[MX_MANIFEST_CACHE];
  Manifest aLine[MX_MANIFEST_CACHE];
} manifestCache;


/*
** Clear the memory allocated in a manifest object
*/
void manifest_clear(Manifest *p){
  blob_reset(&p->content);
  free(p->aFile);
  free(p->azParent);
  free(p->azCChild);
  free(p->aTag);
  free(p->aField);
  memset(p, 0, sizeof(*p));
}

/*
** Add an element to the manifest cache using LRU replacement.
*/
void manifest_cache_insert(int rid, Manifest *p){
  int i;
  for(i=0; i<MX_MANIFEST_CACHE; i++){
    if( manifestCache.aRid[i]==0 ) break;
  }
  if( i>=MX_MANIFEST_CACHE ){
    int oldest = 0;
    int oldestAge = manifestCache.aAge[0];
    for(i=1; i<MX_MANIFEST_CACHE; i++){
      if( manifestCache.aAge[i]<oldestAge ){
        oldest = i;
        oldestAge = manifestCache.aAge[i];
      }
    }
    manifest_clear(&manifestCache.aLine[oldest]);
    i = oldest;
  }
  manifestCache.aAge[i] = ++manifestCache.nxAge;
  manifestCache.aRid[i] = rid;
  manifestCache.aLine[i] = *p;
}

/*
** Try to extract a line from the manifest cache. Return 1 if found.
** Return 0 if not found.
*/
int manifest_cache_find(int rid, Manifest *p){
  int i;
  for(i=0; i<MX_MANIFEST_CACHE; i++){
    if( manifestCache.aRid[i]==rid ){
      *p = manifestCache.aLine[i];
      manifestCache.aRid[i] = 0;
      return 1;
    }
  }
  return 0;
}

/*
** Clear the manifest cache.
*/
void manifest_cache_clear(void){
  int i;
  for(i=0; i<MX_MANIFEST_CACHE; i++){
    if( manifestCache.aRid[i]>0 ){
      manifest_clear(&manifestCache.aLine[i]);
    }
  }
  memset(&manifestCache, 0, sizeof(manifestCache));
}

#ifdef FOSSIL_DONT_VERIFY_MANIFEST_MD5SUM
# define md5sum_init(X)
# define md5sum_step_text(X,Y)
#endif

/*
** Parse a blob into a Manifest object.  The Manifest object
** takes over the input blob and will free it when the
** Manifest object is freed.  Zeros are inserted into the blob
** as string terminators so that blob should not be used again.
**
** Return TRUE if the content really is a control file of some
** kind.  Return FALSE if there are syntax errors.
**
** This routine is strict about the format of a control file.
** The format must match exactly or else it is rejected.  This
** rule minimizes the risk that a content file will be mistaken
** for a control file simply because they look the same.
**
** The pContent is reset.  If TRUE is returned, then pContent will
** be reset when the Manifest object is cleared.  If FALSE is
** returned then the Manifest object is cleared automatically
** and pContent is reset before the return.
**
** The entire file can be PGP clear-signed.  The signature is ignored.
** The file consists of zero or more cards, one card per line.
** (Except: the content of the W card can extend of multiple lines.)
** Each card is divided into tokens by a single space character.
** The first token is a single upper-case letter which is the card type.
** The card type determines the other parameters to the card.
** Cards must occur in lexicographical order.
*/
int manifest_parse(Manifest *p, Blob *pContent){
  int seenHeader = 0;
  int seenZ = 0;
  int i, lineNo=0;
  Blob line, token, a1, a2, a3, a4;
  char cPrevType = 0;

  memset(p, 0, sizeof(*p));
  memcpy(&p->content, pContent, sizeof(p->content));
  blob_zero(pContent);
  pContent = &p->content;

  blob_zero(&a1);
  blob_zero(&a2);
  blob_zero(&a3);
  md5sum_init();
  while( blob_line(pContent, &line) ){
    char *z = blob_buffer(&line);
    lineNo++;
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
      **     A <filename> <target> ?<source>?
      **
      ** Identifies an attachment to either a wiki page or a ticket.
      ** <source> is the artifact that is the attachment.  <source>
      ** is omitted to delete an attachment.  <target> is the name of
      ** a wiki page or ticket to which that attachment is connected.
      */
      case 'A': {
        char *zName, *zTarget, *zSrc;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)==0 ) goto manifest_syntax_error;
        if( p->zAttachName!=0 ) goto manifest_syntax_error;
        zName = blob_terminate(&a1);
        zTarget = blob_terminate(&a2);
        blob_token(&line, &a3);
        zSrc = blob_terminate(&a3);
        defossilize(zName);
        if( !file_is_simple_pathname(zName) ){
          goto manifest_syntax_error;
        }
        defossilize(zTarget);
        if( (blob_size(&a2)!=UUID_SIZE || !validate16(zTarget, UUID_SIZE))
           && !wiki_name_is_wellformed((const unsigned char *)zTarget) ){
          goto manifest_syntax_error;
        }
        if( blob_size(&a3)>0
         && (blob_size(&a3)!=UUID_SIZE || !validate16(zSrc, UUID_SIZE)) ){
          goto manifest_syntax_error;
        }
        p->zAttachName = (char*)file_tail(zName);
        p->zAttachSrc = zSrc;
        p->zAttachTarget = zTarget;
        break;
      }

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
      **     E <timestamp> <uuid>
      **
      ** An "event" card that contains the timestamp of the event in the 
      ** format YYYY-MM-DDtHH:MM:SS and a unique identifier for the event.
      ** The event timestamp is distinct from the D timestamp.  The D
      ** timestamp is when the artifact was created whereas the E timestamp
      ** is when the specific event is said to occur.
      */
      case 'E': {
        char *zEDate;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( p->rEventDate!=0.0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a3)!=0 ) goto manifest_syntax_error;
        zEDate = blob_terminate(&a1);
        p->rEventDate = db_double(0.0, "SELECT julianday(%Q)", zEDate);
        if( p->rEventDate<=0.0 ) goto manifest_syntax_error;
        if( blob_size(&a2)!=UUID_SIZE ) goto manifest_syntax_error;
        p->zEventId = blob_terminate(&a2);
        if( !validate16(p->zEventId, UUID_SIZE) ) goto manifest_syntax_error;
        break;
      }

      /*
      **     F <filename> <uuid> ?<permissions>? ?<old-name>?
      **
      ** Identifies a file in a manifest.  Multiple F lines are
      ** allowed in a manifest.  F lines are not allowed in any
      ** other control file.  The filename and old-name are fossil-encoded.
      */
      case 'F': {
        char *zName, *zUuid, *zPerm, *zPriorName;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)==0 ) goto manifest_syntax_error;
        zName = blob_terminate(&a1);
        zUuid = blob_terminate(&a2);
        blob_token(&line, &a3);
        zPerm = blob_terminate(&a3);
        if( blob_size(&a2)!=UUID_SIZE ) goto manifest_syntax_error;
        if( !validate16(zUuid, UUID_SIZE) ) goto manifest_syntax_error;
        defossilize(zName);
        if( !file_is_simple_pathname(zName) ){
          goto manifest_syntax_error;
        }
        blob_token(&line, &a4);
        zPriorName = blob_terminate(&a4);
        if( zPriorName[0] ){
          defossilize(zPriorName);
          if( !file_is_simple_pathname(zPriorName) ){
            goto manifest_syntax_error;
          }
        }else{
          zPriorName = 0;
        }
        if( p->nFile>=p->nFileAlloc ){
          p->nFileAlloc = p->nFileAlloc*2 + 10;
          p->aFile = fossil_realloc(p->aFile, 
                                    p->nFileAlloc*sizeof(p->aFile[0]) );
        }
        i = p->nFile++;
        p->aFile[i].zName = zName;
        p->aFile[i].zUuid = zUuid;
        p->aFile[i].zPerm = zPerm;
        p->aFile[i].zPrior = zPriorName;
        p->aFile[i].iRename = -1;
        if( i>0 && strcmp(p->aFile[i-1].zName, zName)>=0 ){
          goto manifest_syntax_error;
        }
        break;
      }

      /*
      **     J <name> ?<value>?
      **
      ** Specifies a name value pair for ticket.  If the first character
      ** of <name> is "+" then the <value> is appended to any preexisting
      ** value.  If <value> is omitted then it is understood to be an
      ** empty string.
      */
      case 'J': {
        char *zName, *zValue;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        blob_token(&line, &a2);
        if( blob_token(&line, &a3)!=0 ) goto manifest_syntax_error;
        zName = blob_terminate(&a1);
        zValue = blob_terminate(&a2);
        defossilize(zValue);
        if( p->nField>=p->nFieldAlloc ){
          p->nFieldAlloc = p->nFieldAlloc*2 + 10;
          p->aField = fossil_realloc(p->aField,
                               p->nFieldAlloc*sizeof(p->aField[0]) );
        }
        i = p->nField++;
        p->aField[i].zName = zName;
        p->aField[i].zValue = zValue;
        if( i>0 && strcmp(p->aField[i-1].zName, zName)>=0 ){
          goto manifest_syntax_error;
        }
        break;
      }


      /*
      **    K <uuid>
      **
      ** A K-line gives the UUID for the ticket which this control file
      ** is amending.
      */
      case 'K': {
        char *zUuid;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        zUuid = blob_terminate(&a1);
        if( blob_size(&a1)!=UUID_SIZE ) goto manifest_syntax_error;
        if( !validate16(zUuid, UUID_SIZE) ) goto manifest_syntax_error;
        if( p->zTicketUuid!=0 ) goto manifest_syntax_error;
        p->zTicketUuid = zUuid;
        break;
      }

      /*
      **     L <wikititle>
      **
      ** The wiki page title is fossil-encoded.  There may be no more than
      ** one L line.
      */
      case 'L': {
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( p->zWikiTitle!=0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        p->zWikiTitle = blob_terminate(&a1);
        defossilize(p->zWikiTitle);
        if( !wiki_name_is_wellformed((const unsigned char *)p->zWikiTitle) ){
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
          p->azCChild = fossil_realloc(p->azCChild
                                 , p->nCChildAlloc*sizeof(p->azCChild[0]) );
        }
        i = p->nCChild++;
        p->azCChild[i] = zUuid;
        if( i>0 && strcmp(p->azCChild[i-1], zUuid)>=0 ){
          goto manifest_syntax_error;
        }
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
            p->azParent = fossil_realloc(p->azParent,
                               p->nParentAlloc*sizeof(char*));
          }
          i = p->nParent++;
          p->azParent[i] = zUuid;
        }
        break;
      }

      /*
      **     R <md5sum>
      **
      ** Specify the MD5 checksum over the name and content of all files
      ** in the manifest.
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
        if( blob_token(&line, &a1)==0 ){
          goto manifest_syntax_error;
        }
        if( blob_token(&line, &a2)==0 ){
          goto manifest_syntax_error;
        }
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
          zUuid = 0;
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
          p->aTag = fossil_realloc(p->aTag, p->nTagAlloc*sizeof(p->aTag[0]) );
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
      **     U ?<login>?
      **
      ** Identify the user who created this control file by their
      ** login.  Only one U line is allowed.  Prohibited in clusters.
      ** If the user name is omitted, take that to be "anonymous".
      */
      case 'U': {
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( p->zUser!=0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a1)==0 ){
          p->zUser = "anonymous";
        }else{
          p->zUser = blob_terminate(&a1);
          defossilize(p->zUser);
        }
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        break;
      }

      /*
      **     W <size>
      **
      ** The next <size> bytes of the file contain the text of the wiki
      ** page.  There is always an extra \n before the start of the next
      ** record.
      */
      case 'W': {
        int size;
        Blob wiki;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        if( !blob_is_int(&a1, &size) ) goto manifest_syntax_error;
        if( size<0 ) goto manifest_syntax_error;
        if( p->zWiki!=0 ) goto manifest_syntax_error;
        blob_zero(&wiki);
        if( blob_extract(pContent, size+1, &wiki)!=size+1 ){
          goto manifest_syntax_error;
        }
        p->zWiki = blob_buffer(&wiki);
        md5sum_step_text(p->zWiki, size+1);
        if( p->zWiki[size]!='\n' ) goto manifest_syntax_error;
        p->zWiki[size] = 0;
        break;
      }


      /*
      **     Z <md5sum>
      **
      ** MD5 checksum on this control file.  The checksum is over all
      ** lines (other than PGP-signature lines) prior to the current
      ** line.  This must be the last record.
      **
      ** This card is required for all control file types except for
      ** Manifest.  It is not required for manifest only for historical
      ** compatibility reasons.
      */
      case 'Z': {
#ifndef FOSSIL_DONT_VERIFY_MANIFEST_MD5SUM
        int rc;
        Blob hash;
#endif
        if( blob_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( blob_token(&line, &a2)!=0 ) goto manifest_syntax_error;
        if( blob_size(&a1)!=32 ) goto manifest_syntax_error;
        if( !validate16(blob_buffer(&a1), 32) ) goto manifest_syntax_error;
#ifndef FOSSIL_DONT_VERIFY_MANIFEST_MD5SUM
        md5sum_finish(&hash);
        rc = blob_compare(&hash, &a1);
        blob_reset(&hash);
        if( rc!=0 ) goto manifest_syntax_error;
#endif
        seenZ = 1;
        break;
      }
      default: {
        goto manifest_syntax_error;
      }
    }
  }
  if( !seenHeader ) goto manifest_syntax_error;

  if( p->nFile>0 || p->zRepoCksum!=0 ){
    if( p->nCChild>0 ) goto manifest_syntax_error;
    if( p->rDate<=0.0 ) goto manifest_syntax_error;
    if( p->nField>0 ) goto manifest_syntax_error;
    if( p->zTicketUuid ) goto manifest_syntax_error;
    if( p->zWiki ) goto manifest_syntax_error;
    if( p->zWikiTitle ) goto manifest_syntax_error;
    if( p->zEventId ) goto manifest_syntax_error;
    if( p->zTicketUuid ) goto manifest_syntax_error;
    if( p->zAttachName ) goto manifest_syntax_error;
    p->type = CFTYPE_MANIFEST;
  }else if( p->nCChild>0 ){
    if( p->rDate>0.0 ) goto manifest_syntax_error;
    if( p->zComment!=0 ) goto manifest_syntax_error;
    if( p->zUser!=0 ) goto manifest_syntax_error;
    if( p->nTag>0 ) goto manifest_syntax_error;
    if( p->nParent>0 ) goto manifest_syntax_error;
    if( p->nField>0 ) goto manifest_syntax_error;
    if( p->zTicketUuid ) goto manifest_syntax_error;
    if( p->zWiki ) goto manifest_syntax_error;
    if( p->zWikiTitle ) goto manifest_syntax_error;
    if( p->zEventId ) goto manifest_syntax_error;
    if( p->zAttachName ) goto manifest_syntax_error;
    if( !seenZ ) goto manifest_syntax_error;
    p->type = CFTYPE_CLUSTER;
  }else if( p->nField>0 ){
    if( p->rDate<=0.0 ) goto manifest_syntax_error;
    if( p->zWiki ) goto manifest_syntax_error;
    if( p->zWikiTitle ) goto manifest_syntax_error;
    if( p->zEventId ) goto manifest_syntax_error;
    if( p->nCChild>0 ) goto manifest_syntax_error;
    if( p->nTag>0 ) goto manifest_syntax_error;
    if( p->zTicketUuid==0 ) goto manifest_syntax_error;
    if( p->zUser==0 ) goto manifest_syntax_error;
    if( p->zAttachName ) goto manifest_syntax_error;
    if( !seenZ ) goto manifest_syntax_error;
    p->type = CFTYPE_TICKET;
  }else if( p->zEventId ){
    if( p->rDate<=0.0 ) goto manifest_syntax_error;
    if( p->nCChild>0 ) goto manifest_syntax_error;
    if( p->zTicketUuid!=0 ) goto manifest_syntax_error;
    if( p->zWikiTitle!=0 ) goto manifest_syntax_error;
    if( p->zWiki==0 ) goto manifest_syntax_error;
    if( p->zAttachName ) goto manifest_syntax_error;
    for(i=0; i<p->nTag; i++){
      if( p->aTag[i].zName[0]!='+' ) goto manifest_syntax_error;
      if( p->aTag[i].zUuid!=0 ) goto manifest_syntax_error;
    }
    if( !seenZ ) goto manifest_syntax_error;
    p->type = CFTYPE_EVENT;
  }else if( p->zWiki!=0 ){
    if( p->rDate<=0.0 ) goto manifest_syntax_error;
    if( p->nCChild>0 ) goto manifest_syntax_error;
    if( p->nTag>0 ) goto manifest_syntax_error;
    if( p->zTicketUuid!=0 ) goto manifest_syntax_error;
    if( p->zWikiTitle==0 ) goto manifest_syntax_error;
    if( p->zAttachName ) goto manifest_syntax_error;
    if( !seenZ ) goto manifest_syntax_error;
    p->type = CFTYPE_WIKI;
  }else if( p->nTag>0 ){
    if( p->rDate<=0.0 ) goto manifest_syntax_error;
    if( p->nParent>0 ) goto manifest_syntax_error;
    if( p->zWikiTitle ) goto manifest_syntax_error;
    if( p->zTicketUuid ) goto manifest_syntax_error;
    if( p->zAttachName ) goto manifest_syntax_error;
    if( !seenZ ) goto manifest_syntax_error;
    p->type = CFTYPE_CONTROL;
  }else if( p->zAttachName ){
    if( p->nCChild>0 ) goto manifest_syntax_error;
    if( p->rDate<=0.0 ) goto manifest_syntax_error;
    if( p->zTicketUuid ) goto manifest_syntax_error;
    if( p->zWikiTitle ) goto manifest_syntax_error;
    if( !seenZ ) goto manifest_syntax_error;
    p->type = CFTYPE_ATTACHMENT;
  }else{
    if( p->nCChild>0 ) goto manifest_syntax_error;
    if( p->rDate<=0.0 ) goto manifest_syntax_error;
    if( p->nParent>0 ) goto manifest_syntax_error;
    if( p->nField>0 ) goto manifest_syntax_error;
    if( p->zTicketUuid ) goto manifest_syntax_error;
    if( p->zWikiTitle ) goto manifest_syntax_error;
    if( p->zTicketUuid ) goto manifest_syntax_error;
    p->type = CFTYPE_MANIFEST;
  }
  md5sum_init();
  return 1;

manifest_syntax_error:
  /*fprintf(stderr, "Manifest error on line %i\n", lineNo);fflush(stderr);*/
  md5sum_init();
  manifest_clear(p);
  return 0;
}

/*
** COMMAND: test-parse-manifest
**
** Usage: %fossil test-parse-manifest FILENAME ?N?
**
** Parse the manifest and discarded.  Use for testing only.
*/
void manifest_test_parse_cmd(void){
  Manifest m;
  Blob b;
  int i;
  int n = 1;
  if( g.argc!=3 && g.argc!=4 ){
    usage("FILENAME");
  }
  db_must_be_within_tree();
  blob_read_from_file(&b, g.argv[2]);
  if( g.argc>3 ) n = atoi(g.argv[3]);
  for(i=0; i<n; i++){
    Blob b2;
    blob_copy(&b2, &b);
    manifest_parse(&m, &b2);
    manifest_clear(&m);
  }
}

/*
** Translate a filename into a filename-id (fnid).  Create a new fnid
** if no previously exists.
*/
static int filename_to_fnid(const char *zFilename){
  static Stmt q1, s1;
  int fnid;
  db_static_prepare(&q1, "SELECT fnid FROM filename WHERE name=:fn");
  db_bind_text(&q1, ":fn", zFilename);
  fnid = 0;
  if( db_step(&q1)==SQLITE_ROW ){
    fnid = db_column_int(&q1, 0);
  }
  db_reset(&q1);
  if( fnid==0 ){
    db_static_prepare(&s1, "INSERT INTO filename(name) VALUES(:fn)");
    db_bind_text(&s1, ":fn", zFilename);
    db_exec(&s1);
    fnid = db_last_insert_rowid();
  }
  return fnid;
}

/*
** Add a single entry to the mlink table.  Also add the filename to
** the filename table if it is not there already.
*/
static void add_one_mlink(
  int mid,                  /* The record ID of the manifest */
  const char *zFromUuid,    /* UUID for the mlink.pid field */
  const char *zToUuid,      /* UUID for the mlink.fid field */
  const char *zFilename,    /* Filename */
  const char *zPrior        /* Previous filename.  NULL if unchanged */
){
  int fnid, pfnid, pid, fid;
  static Stmt s1;

  fnid = filename_to_fnid(zFilename);
  if( zPrior==0 ){
    pfnid = 0;
  }else{
    pfnid = filename_to_fnid(zPrior);
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
  db_static_prepare(&s1,
    "INSERT INTO mlink(mid,pid,fid,fnid,pfnid)"
    "VALUES(:m,:p,:f,:n,:pfn)"
  );
  db_bind_int(&s1, ":m", mid);
  db_bind_int(&s1, ":p", pid);
  db_bind_int(&s1, ":f", fid);
  db_bind_int(&s1, ":n", fnid);
  db_bind_int(&s1, ":pfn", pfnid);
  db_exec(&s1);
  if( pid && fid ){
    content_deltify(pid, fid, 0);
  }
}

/*
** Locate a file named zName in the aFile[] array of the given
** manifest.  We assume that filenames are in sorted order.
** Use a binary search.  Return turn the index of the matching
** entry.  Or return -1 if not found.
*/
static int find_file_in_manifest(Manifest *p, const char *zName){
  int lwr, upr;
  int c;
  int i;
  lwr = 0;
  upr = p->nFile - 1;
  while( lwr<=upr ){
    i = (lwr+upr)/2;
    c = strcmp(p->aFile[i].zName, zName);
    if( c<0 ){
      lwr = i+1;
    }else if( c>0 ){
      upr = i-1;
    }else{
      return i;
    }
  }
  return -1;
}

/*
** Add mlink table entries associated with manifest cid.  The
** parent manifest is pid.
**
** A single mlink entry is added for every file that changed content
** and/or name going from pid to cid.
**
** Deleted files have mlink.fid=0.
** Added files have mlink.pid=0.
** Edited files have both mlink.pid!=0 and mlink.fid!=0
*/
static void add_mlink(int pid, Manifest *pParent, int cid, Manifest *pChild){
  Manifest other;
  Blob otherContent;
  int otherRid;
  int i, j;

  if( db_exists("SELECT 1 FROM mlink WHERE mid=%d", cid) ){
    return;
  }
  assert( pParent==0 || pChild==0 );
  if( pParent==0 ){
    pParent = &other;
    otherRid = pid;
  }else{
    pChild = &other;
    otherRid = cid;
  }
  if( manifest_cache_find(otherRid, &other)==0 ){
    content_get(otherRid, &otherContent);
    if( blob_size(&otherContent)==0 ) return;
    if( manifest_parse(&other, &otherContent)==0 ) return;
  }
  content_deltify(pid, cid, 0);

  /* Use the iRename fields to find the cross-linkage between
  ** renamed files.  */
  for(j=0; j<pChild->nFile; j++){
    const char *zPrior = pChild->aFile[j].zPrior;
    if( zPrior && zPrior[0] ){
      i = find_file_in_manifest(pParent, zPrior);
      if( i>=0 ){
        pChild->aFile[j].iRename = i;
        pParent->aFile[i].iRename = j;
      }
    }
  }

  /* Construct the mlink entries */
  for(i=j=0; i<pParent->nFile && j<pChild->nFile; ){
    int c;
    if( pParent->aFile[i].iRename>=0 ){
      i++;
    }else if( (c = strcmp(pParent->aFile[i].zName, pChild->aFile[j].zName))<0 ){
      add_one_mlink(cid, pParent->aFile[i].zUuid,0,pParent->aFile[i].zName,0);
      i++;
    }else if( c>0 ){
      int rn = pChild->aFile[j].iRename;
      if( rn>=0 ){
        add_one_mlink(cid, pParent->aFile[rn].zUuid, pChild->aFile[j].zUuid, 
                      pChild->aFile[j].zName, pParent->aFile[rn].zName);
      }else{
        add_one_mlink(cid, 0, pChild->aFile[j].zUuid, pChild->aFile[j].zName,0);
      }
      j++;
    }else{
      if( strcmp(pParent->aFile[i].zUuid, pChild->aFile[j].zUuid)!=0 ){
        add_one_mlink(cid, pParent->aFile[i].zUuid, pChild->aFile[j].zUuid, 
                      pChild->aFile[j].zName, 0);
      }
      i++;
      j++;
    }
  }
  while( i<pParent->nFile ){
    if( pParent->aFile[i].iRename<0 ){
      add_one_mlink(cid, pParent->aFile[i].zUuid, 0, pParent->aFile[i].zName,0);
    }
    i++;
  }
  while( j<pChild->nFile ){
    int rn = pChild->aFile[j].iRename;
    if( rn>=0 ){
      add_one_mlink(cid, pParent->aFile[rn].zUuid, pChild->aFile[j].zUuid, 
                    pChild->aFile[j].zName, pParent->aFile[rn].zName);
    }else{
      add_one_mlink(cid, 0, pChild->aFile[j].zUuid, pChild->aFile[j].zName,0);
    }
    j++;
  }
  manifest_cache_insert(otherRid, &other);
}

/*
** True if manifest_crosslink_begin() has been called but
** manifest_crosslink_end() is still pending.
*/
static int manifest_crosslink_busy = 0;

/*
** Setup to do multiple manifest_crosslink() calls.
** This is only required if processing ticket changes.
*/
void manifest_crosslink_begin(void){
  assert( manifest_crosslink_busy==0 );
  manifest_crosslink_busy = 1;
  db_begin_transaction();
  db_multi_exec("CREATE TEMP TABLE pending_tkt(uuid TEXT UNIQUE)");
}

/*
** Finish up a sequence of manifest_crosslink calls.
*/
void manifest_crosslink_end(void){
  Stmt q;
  assert( manifest_crosslink_busy==1 );
  db_prepare(&q, "SELECT uuid FROM pending_tkt");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    ticket_rebuild_entry(zUuid);
  }
  db_finalize(&q);
  db_multi_exec("DROP TABLE pending_tkt");
  db_end_transaction(0);
  manifest_crosslink_busy = 0;
}

/*
** Make an entry in the event table for a ticket change artifact.
*/
void manifest_ticket_event(
  int rid,                    /* Artifact ID of the change ticket artifact */
  const Manifest *pManifest,  /* Parsed content of the artifact */
  int isNew,                  /* True if this is the first event */
  int tktTagId                /* Ticket tag ID */
){
  int i;
  char *zTitle;
  Blob comment;
  Blob brief;
  char *zNewStatus = 0;
  static char *zTitleExpr = 0;
  static char *zStatusColumn = 0;
  static int once = 1;

  blob_zero(&comment);
  blob_zero(&brief);
  if( once ){
    once = 0;
    zTitleExpr = db_get("ticket-title-expr", "title");
    zStatusColumn = db_get("ticket-status-column", "status");
  }
  zTitle = db_text("unknown", 
    "SELECT %s FROM ticket WHERE tkt_uuid='%s'",
    zTitleExpr, pManifest->zTicketUuid
  );
  if( !isNew ){
    for(i=0; i<pManifest->nField; i++){
      if( strcmp(pManifest->aField[i].zName, zStatusColumn)==0 ){
        zNewStatus = pManifest->aField[i].zValue;
      }
    }
    if( zNewStatus ){
      blob_appendf(&comment, "%h ticket [%.10s]: <i>%s</i>",
         zNewStatus, pManifest->zTicketUuid, zTitle
      );
      if( pManifest->nField>1 ){
        blob_appendf(&comment, " plus %d other change%s",
          pManifest->nField-1, pManifest->nField==2 ? "" : "s");
      }
      blob_appendf(&brief, "%h ticket [%.10s].",
                   zNewStatus, pManifest->zTicketUuid);
    }else{
      zNewStatus = db_text("unknown", 
         "SELECT %s FROM ticket WHERE tkt_uuid='%s'",
         zStatusColumn, pManifest->zTicketUuid
      );
      blob_appendf(&comment, "Ticket [%.10s] <i>%s</i> status still %h with "
           "%d other change%s",
           pManifest->zTicketUuid, zTitle, zNewStatus, pManifest->nField,
           pManifest->nField==1 ? "" : "s"
      );
      free(zNewStatus);
      blob_appendf(&brief, "Ticket [%.10s]: %d change%s",
           pManifest->zTicketUuid, pManifest->nField,
           pManifest->nField==1 ? "" : "s"
      );
    }
  }else{
    blob_appendf(&comment, "New ticket [%.10s] <i>%h</i>.",
      pManifest->zTicketUuid, zTitle
    );
    blob_appendf(&brief, "New ticket [%.10s].", pManifest->zTicketUuid);
  }
  free(zTitle);
  db_multi_exec(
    "REPLACE INTO event(type,tagid,mtime,objid,user,comment,brief)"
    "VALUES('t',%d,%.17g,%d,%Q,%Q,%Q)",
    tktTagId, pManifest->rDate, rid, pManifest->zUser,
    blob_str(&comment), blob_str(&brief)
  );
  blob_reset(&comment);
  blob_reset(&brief);
}

/*
** Scan artifact rid/pContent to see if it is a control artifact of
** any key:
**
**      *  Manifest
**      *  Control
**      *  Wiki Page
**      *  Ticket Change
**      *  Cluster
**      *  Attachment
**      *  Event
**
** If the input is a control artifact, then make appropriate entries
** in the auxiliary tables of the database in order to crosslink the
** artifact.
**
** If global variable g.xlinkClusterOnly is true, then ignore all 
** control artifacts other than clusters.
**
** Historical note:  This routine original processed manifests only.
** Processing for other control artifacts was added later.  The name
** of the routine, "manifest_crosslink", and the name of this source
** file, is a legacy of its original use.
*/
int manifest_crosslink(int rid, Blob *pContent){
  int i;
  Manifest m;
  Stmt q;
  int parentid = 0;

  if( manifest_cache_find(rid, &m) ){
    blob_reset(pContent);
  }else if( manifest_parse(&m, pContent)==0 ){
    return 0;
  }
  if( g.xlinkClusterOnly && m.type!=CFTYPE_CLUSTER ){
    manifest_clear(&m);
    return 0;
  }
  db_begin_transaction();
  if( m.type==CFTYPE_MANIFEST ){
    if( !db_exists("SELECT 1 FROM mlink WHERE mid=%d", rid) ){
      char *zCom;
      for(i=0; i<m.nParent; i++){
        int pid = uuid_to_rid(m.azParent[i], 1);
        db_multi_exec("INSERT OR IGNORE INTO plink(pid, cid, isprim, mtime)"
                      "VALUES(%d, %d, %d, %.17g)", pid, rid, i==0, m.rDate);
        if( i==0 ){
          add_mlink(pid, 0, rid, &m);
          parentid = pid;
        }
      }
      db_prepare(&q, "SELECT cid FROM plink WHERE pid=%d AND isprim", rid);
      while( db_step(&q)==SQLITE_ROW ){
        int cid = db_column_int(&q, 0);
        add_mlink(rid, &m, cid, 0);
      }
      db_finalize(&q);
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,user,comment,"
                           "bgcolor,euser,ecomment)"
        "VALUES('ci',"
        "  coalesce("
        "    (SELECT julianday(value) FROM tagxref WHERE tagid=%d AND rid=%d),"
        "    %.17g"
        "  ),"
        "  %d,%Q,%Q,"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d AND tagtype>0),"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d),"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d));",
        TAG_DATE, rid, m.rDate,
        rid, m.zUser, m.zComment, 
        TAG_BGCOLOR, rid,
        TAG_USER, rid,
        TAG_COMMENT, rid
      );
      zCom = db_text(0, "SELECT coalesce(ecomment, comment) FROM event"
                        " WHERE rowid=last_insert_rowid()");
      wiki_extract_links(zCom, rid, 0, m.rDate, 1, WIKI_INLINE);
      free(zCom);
    }
  }
  if( m.type==CFTYPE_CLUSTER ){
    tag_insert("cluster", 1, 0, rid, m.rDate, rid);
    for(i=0; i<m.nCChild; i++){
      int mid;
      mid = uuid_to_rid(m.azCChild[i], 1);
      if( mid>0 ){
        db_multi_exec("DELETE FROM unclustered WHERE rid=%d", mid);
      }
    }
  }
  if( m.type==CFTYPE_CONTROL
   || m.type==CFTYPE_MANIFEST
   || m.type==CFTYPE_EVENT
  ){
    for(i=0; i<m.nTag; i++){
      int tid;
      int type;
      if( m.aTag[i].zUuid ){
        tid = uuid_to_rid(m.aTag[i].zUuid, 1);
      }else{
        tid = rid;
      }
      if( tid ){
        switch( m.aTag[i].zName[0] ){
          case '-':  type = 0;  break;  /* Cancel prior occurances */
          case '+':  type = 1;  break;  /* Apply to target only */
          case '*':  type = 2;  break;  /* Propagate to descendants */
          default:
            fossil_fatal("unknown tag type in manifest: %s", m.aTag);
            return 0;
        }
        tag_insert(&m.aTag[i].zName[1], type, m.aTag[i].zValue, 
                   rid, m.rDate, tid);
      }
    }
    if( parentid ){
      tag_propagate_all(parentid);
    }
  }
  if( m.type==CFTYPE_WIKI ){
    char *zTag = mprintf("wiki-%s", m.zWikiTitle);
    int tagid = tag_findid(zTag, 1);
    int prior;
    char *zComment;
    int nWiki;
    char zLength[40];
    while( fossil_isspace(m.zWiki[0]) ) m.zWiki++;
    nWiki = strlen(m.zWiki);
    sqlite3_snprintf(sizeof(zLength), zLength, "%d", nWiki);
    tag_insert(zTag, 1, zLength, rid, m.rDate, rid);
    free(zTag);
    prior = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=%d AND mtime<%.17g"
      " ORDER BY mtime DESC",
      tagid, m.rDate
    );
    if( prior ){
      content_deltify(prior, rid, 0);
    }
    if( nWiki>0 ){
      zComment = mprintf("Changes to wiki page [%h]", m.zWikiTitle);
    }else{
      zComment = mprintf("Deleted wiki page [%h]", m.zWikiTitle);
    }
    db_multi_exec(
      "REPLACE INTO event(type,mtime,objid,user,comment,"
      "                  bgcolor,euser,ecomment)"
      "VALUES('w',%.17g,%d,%Q,%Q,"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d AND tagtype>1),"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d),"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d));",
      m.rDate, rid, m.zUser, zComment, 
      TAG_BGCOLOR, rid,
      TAG_BGCOLOR, rid,
      TAG_USER, rid,
      TAG_COMMENT, rid
    );
    free(zComment);
  }
  if( m.type==CFTYPE_EVENT ){
    char *zTag = mprintf("event-%s", m.zEventId);
    int tagid = tag_findid(zTag, 1);
    int prior, subsequent;
    int nWiki;
    char zLength[40];
    while( fossil_isspace(m.zWiki[0]) ) m.zWiki++;
    nWiki = strlen(m.zWiki);
    sqlite3_snprintf(sizeof(zLength), zLength, "%d", nWiki);
    tag_insert(zTag, 1, zLength, rid, m.rDate, rid);
    free(zTag);
    prior = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=%d AND mtime<%.17g"
      " ORDER BY mtime DESC",
      tagid, m.rDate
    );
    if( prior ){
      content_deltify(prior, rid, 0);
      db_multi_exec(
        "DELETE FROM event"
        " WHERE type='e'"
        "   AND tagid=%d"
        "   AND objid IN (SELECT rid FROM tagxref WHERE tagid=%d)",
        tagid, tagid
      );
    }
    subsequent = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=%d AND mtime>%.17g"
      " ORDER BY mtime",
      tagid, m.rDate
    );
    if( subsequent ){
      content_deltify(rid, subsequent, 0);
    }else{
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,tagid,user,comment,bgcolor)"
        "VALUES('e',%.17g,%d,%d,%Q,%Q,"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d));",
        m.rEventDate, rid, tagid, m.zUser, m.zComment, 
        TAG_BGCOLOR, rid
      );
    }
  }
  if( m.type==CFTYPE_TICKET ){
    char *zTag;

    assert( manifest_crosslink_busy==1 );
    zTag = mprintf("tkt-%s", m.zTicketUuid);
    tag_insert(zTag, 1, 0, rid, m.rDate, rid);
    free(zTag);
    db_multi_exec("INSERT OR IGNORE INTO pending_tkt VALUES(%Q)",
                  m.zTicketUuid);
  }
  if( m.type==CFTYPE_ATTACHMENT ){
    db_multi_exec(
       "INSERT INTO attachment(attachid, mtime, src, target,"
                                        "filename, comment, user)"
       "VALUES(%d,%.17g,%Q,%Q,%Q,%Q,%Q);",
       rid, m.rDate, m.zAttachSrc, m.zAttachTarget, m.zAttachName,
       (m.zComment ? m.zComment : ""), m.zUser
    );
    db_multi_exec(
       "UPDATE attachment SET isLatest = (mtime=="
          "(SELECT max(mtime) FROM attachment"
          "  WHERE target=%Q AND filename=%Q))"
       " WHERE target=%Q AND filename=%Q",
       m.zAttachTarget, m.zAttachName,
       m.zAttachTarget, m.zAttachName
    );
    if( strlen(m.zAttachTarget)!=UUID_SIZE
     || !validate16(m.zAttachTarget, UUID_SIZE) 
    ){
      char *zComment;
      if( m.zAttachSrc && m.zAttachSrc[0] ){
        zComment = mprintf("Add attachment \"%h\" to wiki page [%h]",
             m.zAttachName, m.zAttachTarget);
      }else{
        zComment = mprintf("Delete attachment \"%h\" from wiki page [%h]",
             m.zAttachName, m.zAttachTarget);
      }
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,user,comment)"
        "VALUES('w',%.17g,%d,%Q,%Q)",
        m.rDate, rid, m.zUser, zComment
      );
      free(zComment);
    }else{
      char *zComment;
      if( m.zAttachSrc && m.zAttachSrc[0] ){
        zComment = mprintf("Add attachment \"%h\" to ticket [%.10s]",
             m.zAttachName, m.zAttachTarget);
      }else{
        zComment = mprintf("Delete attachment \"%h\" from ticket [%.10s]",
             m.zAttachName, m.zAttachTarget);
      }
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,user,comment)"
        "VALUES('t',%.17g,%d,%Q,%Q)",
        m.rDate, rid, m.zUser, zComment
      );
      free(zComment);
    }
  }
  db_end_transaction(0);
  if( m.type==CFTYPE_MANIFEST ){
    manifest_cache_insert(rid, &m);
  }else{
    manifest_clear(&m);
  }
  return 1;
}

/*
** Given a checkin name, load and parse the manifest for that checkin.
** Throw a fatal error if anything goes wrong.
*/
void manifest_from_name(
  const char *zName,
  Manifest *pM
){
  int rid;
  Blob content;

  rid = name_to_rid(zName);
  if( !is_a_version(rid) ){
    fossil_fatal("no such checkin: %s", zName);
  }
  content_get(rid, &content);
  if( !manifest_parse(pM, &content) ){
    fossil_fatal("cannot parse manifest for checkin: %s", zName);
  }
}
