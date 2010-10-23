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
#define CFTYPE_ANY        0
#define CFTYPE_MANIFEST   1
#define CFTYPE_CLUSTER    2
#define CFTYPE_CONTROL    3
#define CFTYPE_WIKI       4
#define CFTYPE_TICKET     5
#define CFTYPE_ATTACHMENT 6
#define CFTYPE_EVENT      7

/*
** A single F-card within a manifest
*/
struct ManifestFile { 
  char *zName;           /* Name of a file */
  char *zUuid;           /* UUID of the file */
  char *zPerm;           /* File permissions */
  char *zPrior;          /* Prior name if the name was changed */
};


/*
** A parsed manifest or cluster.
*/
struct Manifest {
  Blob content;         /* The original content blob */
  int type;             /* Type of artifact.  One of CFTYPE_xxxxx */
  int rid;              /* The blob-id for this manifest */
  char *zBaseline;      /* Baseline manifest.  The B card. */
  Manifest *pBaseline;  /* The actual baseline manifest */
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
  int iFile;            /* Index of current file in iterator */
  ManifestFile *aFile;  /* One entry for each F-card */
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
#define MX_MANIFEST_CACHE 6
static struct {
  int nxAge;
  int aAge[MX_MANIFEST_CACHE];
  Manifest *apManifest[MX_MANIFEST_CACHE];
} manifestCache;


/*
** Clear the memory allocated in a manifest object
*/
void manifest_destroy(Manifest *p){
  if( p ){
    blob_reset(&p->content);
    free(p->aFile);
    free(p->azParent);
    free(p->azCChild);
    free(p->aTag);
    free(p->aField);
    if( p->pBaseline ) manifest_destroy(p->pBaseline);
    fossil_free(p);
  }
}

/*
** Add an element to the manifest cache using LRU replacement.
*/
void manifest_cache_insert(Manifest *p){
  while( p ){
    int i;
    Manifest *pBaseline = p->pBaseline;
    p->pBaseline = 0;
    for(i=0; i<MX_MANIFEST_CACHE; i++){
      if( manifestCache.apManifest[i]==0 ) break;
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
      manifest_destroy(manifestCache.apManifest[oldest]);
      i = oldest;
    }
    manifestCache.aAge[i] = ++manifestCache.nxAge;
    manifestCache.apManifest[i] = p;
    p = pBaseline;
  }
}

/*
** Try to extract a line from the manifest cache. Return 1 if found.
** Return 0 if not found.
*/
static Manifest *manifest_cache_find(int rid){
  int i;
  Manifest *p;
  for(i=0; i<MX_MANIFEST_CACHE; i++){
    if( manifestCache.apManifest[i] && manifestCache.apManifest[i]->rid==rid ){
      p = manifestCache.apManifest[i];
      manifestCache.apManifest[i] = 0;
      return p;
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
    if( manifestCache.apManifest[i] ){
      manifest_destroy(manifestCache.apManifest[i]);
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
static Manifest *manifest_parse(Blob *pContent, int rid){
  Manifest *p;
  int seenHeader = 0;
  int seenZ = 0;
  int i, lineNo=0;
  Blob line, a1, a2, a3, a4;
  char cPrevType = 0;

  /* Every control artifact ends with a '\n' character.  Exit early
  ** if that is not the case for this artifact. */
  i = blob_size(pContent);
  if( i<=0 || blob_buffer(pContent)[i-1]!='\n' ){
    blob_reset(pContent);
    return 0;
  }

  p = fossil_malloc( sizeof(*p) );
  memset(p, 0, sizeof(*p));
  memcpy(&p->content, pContent, sizeof(p->content));
  p->rid = rid;
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
    if( blob_size(&line)>1 && z[1]!=' ' ) goto manifest_syntax_error;
    line.iCursor = 2;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a2)==0 ) goto manifest_syntax_error;
        if( p->zAttachName!=0 ) goto manifest_syntax_error;
        zName = blob_terminate(&a1);
        zTarget = blob_terminate(&a2);
        fast_token(&line, &a3);
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
      **    B <uuid>
      **
      ** A B-line gives the UUID for the baselinen of a delta-manifest.
      */
      case 'B': {
        char *zBaseline;
        if( p->zBaseline ) goto manifest_syntax_error;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        zBaseline = blob_terminate(&a1);
        if( blob_size(&a1)!=UUID_SIZE ) goto manifest_syntax_error;
        if( !validate16(zBaseline, UUID_SIZE) ) goto manifest_syntax_error;
        p->zBaseline = zBaseline;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a2)!=0 ) goto manifest_syntax_error;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a2)!=0 ) goto manifest_syntax_error;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a2)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a3)!=0 ) goto manifest_syntax_error;
        zEDate = blob_terminate(&a1);
        p->rEventDate = db_double(0.0, "SELECT julianday(%Q)", zEDate);
        if( p->rEventDate<=0.0 ) goto manifest_syntax_error;
        if( blob_size(&a2)!=UUID_SIZE ) goto manifest_syntax_error;
        p->zEventId = blob_terminate(&a2);
        if( !validate16(p->zEventId, UUID_SIZE) ) goto manifest_syntax_error;
        break;
      }

      /*
      **     F <filename> ?<uuid>? ?<permissions>? ?<old-name>?
      **
      ** Identifies a file in a manifest.  Multiple F lines are
      ** allowed in a manifest.  F lines are not allowed in any
      ** other control file.  The filename and old-name are fossil-encoded.
      */
      case 'F': {
        char *zName, *zUuid, *zPerm, *zPriorName;
        md5sum_step_text(blob_buffer(&line), blob_size(&line));
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        zName = blob_terminate(&a1);
        fast_token(&line, &a2);
        zUuid = blob_terminate(&a2);
        if( p->zBaseline==0 || zUuid[0]!=0 ){
          if( blob_size(&a2)!=UUID_SIZE ) goto manifest_syntax_error;
          if( !validate16(zUuid, UUID_SIZE) ) goto manifest_syntax_error;
        }
        fast_token(&line, &a3);
        zPerm = blob_terminate(&a3);
        defossilize(zName);
        if( !file_is_simple_pathname(zName) ){
          goto manifest_syntax_error;
        }
        fast_token(&line, &a4);
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        fast_token(&line, &a2);
        if( fast_token(&line, &a3)!=0 ) goto manifest_syntax_error;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a2)!=0 ) goto manifest_syntax_error;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
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
        while( fast_token(&line, &a1) ){
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a2)!=0 ) goto manifest_syntax_error;
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
        if( fast_token(&line, &a1)==0 ){
          goto manifest_syntax_error;
        }
        if( fast_token(&line, &a2)==0 ){
          goto manifest_syntax_error;
        }
        zName = blob_terminate(&a1);
        zUuid = blob_terminate(&a2);
        if( fast_token(&line, &a3)==0 ){
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
        if( fast_token(&line, &a1)==0 ){
          p->zUser = "anonymous";
        }else{
          p->zUser = blob_terminate(&a1);
          defossilize(p->zUser);
        }
        if( fast_token(&line, &a2)!=0 ) goto manifest_syntax_error;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a2)!=0 ) goto manifest_syntax_error;
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
        if( fast_token(&line, &a1)==0 ) goto manifest_syntax_error;
        if( fast_token(&line, &a2)!=0 ) goto manifest_syntax_error;
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

  if( p->nFile>0 || p->zRepoCksum!=0 || p->zBaseline ){
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
  return p;

manifest_syntax_error:
  /*fprintf(stderr, "Manifest error on line %i\n", lineNo);fflush(stderr);*/
  md5sum_init();
  manifest_destroy(p);
  return 0;
}

/*
** Get a manifest given the rid for the control artifact.  Return
** a pointer to the manifest on success or NULL if there is a failure.
*/
Manifest *manifest_get(int rid, int cfType){
  Blob content;
  Manifest *p;
  p = manifest_cache_find(rid);
  if( p ){
    if( cfType!=CFTYPE_ANY && cfType!=p->type ){
      manifest_cache_insert(p);
      p = 0;
    }
    return p;
  }
  content_get(rid, &content);
  p = manifest_parse(&content, rid);
  if( p && cfType!=CFTYPE_ANY && cfType!=p->type ){
    manifest_destroy(p);
    p = 0;
  }
  return p;
}

/*
** Given a checkin name, load and parse the manifest for that checkin.
** Throw a fatal error if anything goes wrong.
*/
Manifest *manifest_get_by_name(const char *zName, int *pRid){
  int rid;
  Manifest *p;

  rid = name_to_rid(zName);
  if( !is_a_version(rid) ){
    fossil_fatal("no such checkin: %s", zName);
  }
  if( pRid ) *pRid = rid;
  p = manifest_get(rid, CFTYPE_MANIFEST);
  if( p==0 ){
    fossil_fatal("cannot parse manifest for checkin: %s", zName);
  }
  return p;
}

/*
** COMMAND: test-parse-manifest
**
** Usage: %fossil test-parse-manifest FILENAME ?N?
**
** Parse the manifest and discarded.  Use for testing only.
*/
void manifest_test_parse_cmd(void){
  Manifest *p;
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
    p = manifest_parse(&b2, 0);
    manifest_destroy(p);
  }
}

/*
** Fetch the baseline associated with the delta-manifest p.
** Print a fatal-error and quit if unable to load the baseline.
*/
static void fetch_baseline(Manifest *p){
  if( p->zBaseline!=0 && p->pBaseline==0 ){
    p->pBaseline = manifest_get_by_name(p->zBaseline, 0);
    if( p->pBaseline==0 ){
      fossil_fatal("cannot access baseline manifest %S", p->zBaseline);
    }
  }
}

/*
** Rewind a manifest-file iterator back to the beginning of the manifest.
*/
void manifest_file_rewind(Manifest *p){
  p->iFile = 0;
  fetch_baseline(p);
  if( p->pBaseline ){
    p->pBaseline->iFile = 0;
  }
}

/*
** Advance to the next manifest-file.
**
** Return NULL for end-of-records or if there is an error.  If an error
** occurs and pErr!=0 then store 1 in *pErr.
*/
ManifestFile *manifest_file_next(
  Manifest *p,   
  int *pErr
){
  ManifestFile *pOut = 0;
  if( pErr ) *pErr = 0;
  if( p->pBaseline==0 ){
    /* Manifest p is a baseline-manifest.  Just scan down the list
    ** of files. */
    if( p->iFile<p->nFile ) pOut = &p->aFile[p->iFile++];
  }else{
    /* Manifest p is a delta-manifest.  Scan the baseline but amend the
    ** file list in the baseline with changes described by p.
    */
    Manifest *pB = p->pBaseline;
    int cmp;
    while(1){
      if( pB->iFile>=pB->nFile ){
        /* We have used all entries out of the baseline.  Return the next
        ** entry from the delta. */
        if( p->iFile<p->nFile ) pOut = &p->aFile[p->iFile++];
        break;
      }else if( p->iFile>=p->nFile ){
        /* We have used all entries from the delta.  Return the next
        ** entry from the baseline. */
        if( pB->iFile<pB->nFile ) pOut = &pB->aFile[pB->iFile++];
        break;
      }else if( (cmp = strcmp(pB->aFile[pB->iFile].zName,
                              p->aFile[p->iFile].zName)) < 0 ){
        /* The next baseline entry comes before the next delta entry.
        ** So return the baseline entry. */
        pOut = &pB->aFile[pB->iFile++];
        break;
      }else if( cmp>0 ){
        /* The next delta entry comes before the next baseline
        ** entry so return the delta entry */
        pOut = &p->aFile[p->iFile++];
        break;
      }else if( p->aFile[p->iFile].zUuid[0] ){
        /* The next delta entry is a replacement for the next baseline
        ** entry.  Skip the baseline entry and return the delta entry */
        pB->iFile++;
        pOut = &p->aFile[p->iFile++];
        break;
      }else{
        /* The next delta entry is a delete of the next baseline
        ** entry.  Skip them both.  Repeat the loop to find the next
        ** non-delete entry. */
        pB->iFile++;
        p->iFile++;
        continue;
      }
    }
  }
  return pOut;
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
  const char *zFromUuid,    /* UUID for the mlink.pid. "" to add file */
  const char *zToUuid,      /* UUID for the mlink.fid. "" to delele */
  const char *zFilename,    /* Filename */
  const char *zPrior        /* Previous filename. NULL if unchanged */
){
  int fnid, pfnid, pid, fid;
  static Stmt s1;

  fnid = filename_to_fnid(zFilename);
  if( zPrior==0 ){
    pfnid = 0;
  }else{
    pfnid = filename_to_fnid(zPrior);
  }
  if( zFromUuid==0 || zFromUuid[0]==0 ){
    pid = 0;
  }else{
    pid = uuid_to_rid(zFromUuid, 1);
  }
  if( zToUuid==0 || zToUuid[0]==0 ){
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
** Do a binary search to find a file in the p->aFile[] array.  
**
** As an optimization, guess that the file we seek is at index p->iFile.
** That will usually be the case.  If it is not found there, then do the
** actual binary search.
**
** Update p->iFile to be the index of the file that is found.
*/
static ManifestFile *manifest_file_seek_base(Manifest *p, const char *zName){
  int lwr, upr;
  int c;
  int i;
  lwr = 0;
  upr = p->nFile - 1;
  if( p->iFile>=lwr && p->iFile<upr ){
    c = strcmp(p->aFile[p->iFile+1].zName, zName);
    if( c==0 ){
      return &p->aFile[++p->iFile];
    }else if( c>0 ){
      upr = p->iFile;
    }else{
      lwr = p->iFile+1;
    }
  }
  while( lwr<=upr ){
    i = (lwr+upr)/2;
    c = strcmp(p->aFile[i].zName, zName);
    if( c<0 ){
      lwr = i+1;
    }else if( c>0 ){
      upr = i-1;
    }else{
      p->iFile = i;
      return &p->aFile[i];
    }
  }
  return 0;
}

/*
** Locate a file named zName in the aFile[] array of the given manifest.
** Return a pointer to the appropriate ManifestFile object.  Return NULL
** if not found.
**
** This routine works even if p is a delta-manifest.  The pointer 
** returned might be to the baseline.
**
** We assume that filenames are in sorted order and use a binary search.
*/
ManifestFile *manifest_file_seek(Manifest *p, const char *zName){
  ManifestFile *pFile;
  
  pFile = manifest_file_seek_base(p, zName);
  if( pFile && pFile->zUuid[0]==0 ) return 0;
  if( pFile==0 && p->zBaseline ){
    fetch_baseline(p);
    pFile = manifest_file_seek_base(p->pBaseline, zName);
  }
  return pFile;
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
  Blob otherContent;
  int otherRid;
  int i, rc;
  ManifestFile *pChildFile, *pParentFile;
  Manifest **ppOther;
  static Stmt eq;

  db_static_prepare(&eq, "SELECT 1 FROM mlink WHERE mid=:mid");
  db_bind_int(&eq, ":mid", cid);
  rc = db_step(&eq);
  db_reset(&eq);
  if( rc==SQLITE_ROW ) return;

  assert( pParent==0 || pChild==0 );
  if( pParent==0 ){
    ppOther = &pParent;
    otherRid = pid;
  }else{
    ppOther = &pChild;
    otherRid = cid;
  }
  if( (*ppOther = manifest_cache_find(otherRid))==0 ){
    content_get(otherRid, &otherContent);
    if( blob_size(&otherContent)==0 ) return;
    *ppOther = manifest_parse(&otherContent, otherRid);
    if( *ppOther==0 ) return;
  }
  if( (pParent->zBaseline==0)==(pChild->zBaseline==0) ){
    content_deltify(pid, cid, 0); 
  }
  
  for(i=0, pChildFile=pChild->aFile; i<pChild->nFile; i++, pChildFile++){
    if( pChildFile->zPrior ){
       pParentFile = manifest_file_seek(pParent, pChildFile->zPrior);
       if( pParentFile ){
         add_one_mlink(cid, pParentFile->zUuid, pChildFile->zUuid,
                       pChildFile->zName, pChildFile->zPrior);
       }
    }else{
       pParentFile = manifest_file_seek(pParent, pChildFile->zName);
       if( pParentFile==0 ){
         add_one_mlink(cid, 0, pChildFile->zUuid, pChildFile->zName, 0);
       }else if( strcmp(pChildFile->zUuid, pParentFile->zUuid)!=0 ){
         add_one_mlink(cid, pParentFile->zUuid, pChildFile->zUuid,
                       pChildFile->zName, 0);
       }
    }
  }
  manifest_cache_insert(*ppOther);
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
  Manifest *p;
  Stmt q;
  int parentid = 0;

  if( (p = manifest_cache_find(rid))!=0 ){
    blob_reset(pContent);
  }else if( (p = manifest_parse(pContent, rid))==0 ){
    return 0;
  }
  if( g.xlinkClusterOnly && p->type!=CFTYPE_CLUSTER ){
    manifest_destroy(p);
    return 0;
  }
  db_begin_transaction();
  if( p->type==CFTYPE_MANIFEST ){
    if( !db_exists("SELECT 1 FROM mlink WHERE mid=%d", rid) ){
      char *zCom;
      for(i=0; i<p->nParent; i++){
        int pid = uuid_to_rid(p->azParent[i], 1);
        db_multi_exec("INSERT OR IGNORE INTO plink(pid, cid, isprim, mtime)"
                      "VALUES(%d, %d, %d, %.17g)", pid, rid, i==0, p->rDate);
        if( i==0 ){
          add_mlink(pid, 0, rid, p);
          parentid = pid;
        }
      }
      db_prepare(&q, "SELECT cid FROM plink WHERE pid=%d AND isprim", rid);
      while( db_step(&q)==SQLITE_ROW ){
        int cid = db_column_int(&q, 0);
        add_mlink(rid, p, cid, 0);
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
        TAG_DATE, rid, p->rDate,
        rid, p->zUser, p->zComment, 
        TAG_BGCOLOR, rid,
        TAG_USER, rid,
        TAG_COMMENT, rid
      );
      zCom = db_text(0, "SELECT coalesce(ecomment, comment) FROM event"
                        " WHERE rowid=last_insert_rowid()");
      wiki_extract_links(zCom, rid, 0, p->rDate, 1, WIKI_INLINE);
      free(zCom);

      /* If this is a delta-manifest, record the fact that this repository
      ** contains delta manifests, to free the "commit" logic to generate
      ** new delta manifests.
      */
      if( p->zBaseline!=0 ){
        static int once = 0;
        if( !once ){
          db_set_int("seen-delta-manifest", 1, 0);
          once = 0;
        }
      }
    }
  }
  if( p->type==CFTYPE_CLUSTER ){
    tag_insert("cluster", 1, 0, rid, p->rDate, rid);
    for(i=0; i<p->nCChild; i++){
      int mid;
      mid = uuid_to_rid(p->azCChild[i], 1);
      if( mid>0 ){
        db_multi_exec("DELETE FROM unclustered WHERE rid=%d", mid);
      }
    }
  }
  if( p->type==CFTYPE_CONTROL
   || p->type==CFTYPE_MANIFEST
   || p->type==CFTYPE_EVENT
  ){
    for(i=0; i<p->nTag; i++){
      int tid;
      int type;
      if( p->aTag[i].zUuid ){
        tid = uuid_to_rid(p->aTag[i].zUuid, 1);
      }else{
        tid = rid;
      }
      if( tid ){
        switch( p->aTag[i].zName[0] ){
          case '-':  type = 0;  break;  /* Cancel prior occurances */
          case '+':  type = 1;  break;  /* Apply to target only */
          case '*':  type = 2;  break;  /* Propagate to descendants */
          default:
            fossil_fatal("unknown tag type in manifest: %s", p->aTag);
            return 0;
        }
        tag_insert(&p->aTag[i].zName[1], type, p->aTag[i].zValue, 
                   rid, p->rDate, tid);
      }
    }
    if( parentid ){
      tag_propagate_all(parentid);
    }
  }
  if( p->type==CFTYPE_WIKI ){
    char *zTag = mprintf("wiki-%s", p->zWikiTitle);
    int tagid = tag_findid(zTag, 1);
    int prior;
    char *zComment;
    int nWiki;
    char zLength[40];
    while( fossil_isspace(p->zWiki[0]) ) p->zWiki++;
    nWiki = strlen(p->zWiki);
    sqlite3_snprintf(sizeof(zLength), zLength, "%d", nWiki);
    tag_insert(zTag, 1, zLength, rid, p->rDate, rid);
    free(zTag);
    prior = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=%d AND mtime<%.17g"
      " ORDER BY mtime DESC",
      tagid, p->rDate
    );
    if( prior ){
      content_deltify(prior, rid, 0);
    }
    if( nWiki>0 ){
      zComment = mprintf("Changes to wiki page [%h]", p->zWikiTitle);
    }else{
      zComment = mprintf("Deleted wiki page [%h]", p->zWikiTitle);
    }
    db_multi_exec(
      "REPLACE INTO event(type,mtime,objid,user,comment,"
      "                  bgcolor,euser,ecomment)"
      "VALUES('w',%.17g,%d,%Q,%Q,"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d AND tagtype>1),"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d),"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d));",
      p->rDate, rid, p->zUser, zComment, 
      TAG_BGCOLOR, rid,
      TAG_BGCOLOR, rid,
      TAG_USER, rid,
      TAG_COMMENT, rid
    );
    free(zComment);
  }
  if( p->type==CFTYPE_EVENT ){
    char *zTag = mprintf("event-%s", p->zEventId);
    int tagid = tag_findid(zTag, 1);
    int prior, subsequent;
    int nWiki;
    char zLength[40];
    while( fossil_isspace(p->zWiki[0]) ) p->zWiki++;
    nWiki = strlen(p->zWiki);
    sqlite3_snprintf(sizeof(zLength), zLength, "%d", nWiki);
    tag_insert(zTag, 1, zLength, rid, p->rDate, rid);
    free(zTag);
    prior = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=%d AND mtime<%.17g"
      " ORDER BY mtime DESC",
      tagid, p->rDate
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
      tagid, p->rDate
    );
    if( subsequent ){
      content_deltify(rid, subsequent, 0);
    }else{
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,tagid,user,comment,bgcolor)"
        "VALUES('e',%.17g,%d,%d,%Q,%Q,"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d));",
        p->rEventDate, rid, tagid, p->zUser, p->zComment, 
        TAG_BGCOLOR, rid
      );
    }
  }
  if( p->type==CFTYPE_TICKET ){
    char *zTag;

    assert( manifest_crosslink_busy==1 );
    zTag = mprintf("tkt-%s", p->zTicketUuid);
    tag_insert(zTag, 1, 0, rid, p->rDate, rid);
    free(zTag);
    db_multi_exec("INSERT OR IGNORE INTO pending_tkt VALUES(%Q)",
                  p->zTicketUuid);
  }
  if( p->type==CFTYPE_ATTACHMENT ){
    db_multi_exec(
       "INSERT INTO attachment(attachid, mtime, src, target,"
                                        "filename, comment, user)"
       "VALUES(%d,%.17g,%Q,%Q,%Q,%Q,%Q);",
       rid, p->rDate, p->zAttachSrc, p->zAttachTarget, p->zAttachName,
       (p->zComment ? p->zComment : ""), p->zUser
    );
    db_multi_exec(
       "UPDATE attachment SET isLatest = (mtime=="
          "(SELECT max(mtime) FROM attachment"
          "  WHERE target=%Q AND filename=%Q))"
       " WHERE target=%Q AND filename=%Q",
       p->zAttachTarget, p->zAttachName,
       p->zAttachTarget, p->zAttachName
    );
    if( strlen(p->zAttachTarget)!=UUID_SIZE
     || !validate16(p->zAttachTarget, UUID_SIZE) 
    ){
      char *zComment;
      if( p->zAttachSrc && p->zAttachSrc[0] ){
        zComment = mprintf("Add attachment \"%h\" to wiki page [%h]",
             p->zAttachName, p->zAttachTarget);
      }else{
        zComment = mprintf("Delete attachment \"%h\" from wiki page [%h]",
             p->zAttachName, p->zAttachTarget);
      }
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,user,comment)"
        "VALUES('w',%.17g,%d,%Q,%Q)",
        p->rDate, rid, p->zUser, zComment
      );
      free(zComment);
    }else{
      char *zComment;
      if( p->zAttachSrc && p->zAttachSrc[0] ){
        zComment = mprintf("Add attachment \"%h\" to ticket [%.10s]",
             p->zAttachName, p->zAttachTarget);
      }else{
        zComment = mprintf("Delete attachment \"%h\" from ticket [%.10s]",
             p->zAttachName, p->zAttachTarget);
      }
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,user,comment)"
        "VALUES('t',%.17g,%d,%Q,%Q)",
        p->rDate, rid, p->zUser, zComment
      );
      free(zComment);
    }
  }
  db_end_transaction(0);
  if( p->type==CFTYPE_MANIFEST ){
    manifest_cache_insert(p);
  }else{
    manifest_destroy(p);
  }
  return 1;
}
