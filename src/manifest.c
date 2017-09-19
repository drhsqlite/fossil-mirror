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
** File permissions used by Fossil internally.
*/
#define PERM_REG          0     /*  regular file  */
#define PERM_EXE          1     /*  executable    */
#define PERM_LNK          2     /*  symlink       */

/*
** Flags for use with manifest_crosslink().
*/
#define MC_NONE           0  /*  default handling           */
#define MC_PERMIT_HOOKS   1  /*  permit hooks to execute    */
#define MC_NO_ERRORS      2  /*  do not issue errors for a bad parse */

/*
** A single F-card within a manifest
*/
struct ManifestFile {
  char *zName;           /* Name of a file */
  char *zUuid;           /* Artifact hash for the file */
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
  char *zMimetype;      /* Mime type of wiki or comment text.  N card.  */
  double rEventDate;    /* Date of an event.  E card. */
  char *zEventId;       /* Artifact hash for an event.  E card. */
  char *zTicketUuid;    /* UUID for a ticket. K card. */
  char *zAttachName;    /* Filename of an attachment. A card. */
  char *zAttachSrc;     /* Artifact hash for document being attached. A card. */
  char *zAttachTarget;  /* Ticket or wiki that attachment applies to.  A card */
  int nFile;            /* Number of F cards */
  int nFileAlloc;       /* Slots allocated in aFile[] */
  int iFile;            /* Index of current file in iterator */
  ManifestFile *aFile;  /* One entry for each F-card */
  int nParent;          /* Number of parents. */
  int nParentAlloc;     /* Slots allocated in azParent[] */
  char **azParent;      /* Hashes of parents.  One for each P card argument */
  int nCherrypick;      /* Number of entries in aCherrypick[] */
  struct {
    char *zCPTarget;    /* Hash for cherry-picked version w/ +|- prefix */
    char *zCPBase;      /* Hash for cherry-pick baseline. NULL for singletons */
  } *aCherrypick;
  int nCChild;          /* Number of cluster children */
  int nCChildAlloc;     /* Number of closts allocated in azCChild[] */
  char **azCChild;      /* Hashes of referenced objects in a cluster. M cards */
  int nTag;             /* Number of T Cards */
  int nTagAlloc;        /* Slots allocated in aTag[] */
  struct TagType {
    char *zName;           /* Name of the tag */
    char *zUuid;           /* Hash of artifact that the tag is applied to */
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
** True if manifest_crosslink_begin() has been called but
** manifest_crosslink_end() is still pending.
*/
static int manifest_crosslink_busy = 0;

/*
** Clear the memory allocated in a manifest object
*/
void manifest_destroy(Manifest *p){
  if( p ){
    blob_reset(&p->content);
    fossil_free(p->aFile);
    fossil_free(p->azParent);
    fossil_free(p->azCChild);
    fossil_free(p->aTag);
    fossil_free(p->aField);
    fossil_free(p->aCherrypick);
    if( p->pBaseline ) manifest_destroy(p->pBaseline);
    memset(p, 0, sizeof(*p));
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
** Return true if z points to the first character after a blank line.
** Tolerate either \r\n or \n line endings.
*/
static int after_blank_line(const char *z){
  if( z[-1]!='\n' ) return 0;
  if( z[-2]=='\n' ) return 1;
  if( z[-2]=='\r' && z[-3]=='\n' ) return 1;
  return 0;
}

/*
** Remove the PGP signature from the artifact, if there is one.
*/
static void remove_pgp_signature(char **pz, int *pn){
  char *z = *pz;
  int n = *pn;
  int i;
  if( strncmp(z, "-----BEGIN PGP SIGNED MESSAGE-----", 34)!=0 ) return;
  for(i=34; i<n && !after_blank_line(z+i); i++){}
  if( i>=n ) return;
  z += i;
  n -= i;
  *pz = z;
  for(i=n-1; i>=0; i--){
    if( z[i]=='\n' && strncmp(&z[i],"\n-----BEGIN PGP SIGNATURE-", 25)==0 ){
      n = i+1;
      break;
    }
  }
  *pn = n;
  return;
}

/*
** Verify the Z-card checksum on the artifact, if there is such a
** checksum.  Return 0 if there is no Z-card.  Return 1 if the Z-card
** exists and is correct.  Return 2 if the Z-card exists and has the wrong
** value.
**
**   0123456789 123456789 123456789 123456789
**   Z aea84f4f863865a8d59d0384e4d2a41c
*/
static int verify_z_card(const char *z, int n){
  if( n<35 ) return 0;
  if( z[n-35]!='Z' || z[n-34]!=' ' ) return 0;
  md5sum_init();
  md5sum_step_text(z, n-35);
  if( memcmp(&z[n-33], md5sum_finish(0), 32)==0 ){
    return 1;
  }else{
    return 2;
  }
}

/*
** A structure used for rapid parsing of the Manifest file
*/
typedef struct ManifestText ManifestText;
struct ManifestText {
  char *z;           /* The first character of the next token */
  char *zEnd;        /* One character beyond the end of the manifest */
  int atEol;         /* True if z points to the start of a new line */
};

/*
** Return a pointer to the next token.  The token is zero-terminated.
** Return NULL if there are no more tokens on the current line.
*/
static char *next_token(ManifestText *p, int *pLen){
  char *z;
  char *zStart;
  int c;
  if( p->atEol ) return 0;
  zStart = z = p->z;
  while( (c=(*z))!=' ' && c!='\n' ){ z++; }
  *z = 0;
  p->z = &z[1];
  p->atEol = c=='\n';
  if( pLen ) *pLen = z - zStart;
  return zStart;
}

/*
** Return the card-type for the next card.  Or, return 0 if there are no
** more cards or if we are not at the end of the current card.
*/
static char next_card(ManifestText *p){
  char c;
  if( !p->atEol || p->z>=p->zEnd ) return 0;
  c = p->z[0];
  if( p->z[1]==' ' ){
    p->z += 2;
    p->atEol = 0;
  }else if( p->z[1]=='\n' ){
    p->z += 2;
    p->atEol = 1;
  }else{
    c = 0;
  }
  return c;
}

/*
** Shorthand for a control-artifact parsing error
*/
#define SYNTAX(T)  {zErr=(T); goto manifest_syntax_error;}

/*
** Parse a blob into a Manifest object.  The Manifest object
** takes over the input blob and will free it when the
** Manifest object is freed.  Zeros are inserted into the blob
** as string terminators so that blob should not be used again.
**
** Return a pointer to an allocated Manifest object if the content
** really is a structural artifact of some kind.  The returned Manifest
** object needs to be freed by a subsequent call to manifest_destroy().
** Return NULL if there are syntax errors or if the input blob does
** not describe a valid structural artifact.
**
** This routine is strict about the format of a structural artifacts.
** The format must match exactly or else it is rejected.  This
** rule minimizes the risk that a content artifact will be mistaken
** for a structural artifact simply because they look the same.
**
** The pContent is reset.  If a pointer is returned, then pContent will
** be reset when the Manifest object is cleared.  If NULL is
** returned then the Manifest object is cleared automatically
** and pContent is reset before the return.
**
** The entire input blob can be PGP clear-signed.  The signature is ignored.
** The artifact consists of zero or more cards, one card per line.
** (Except: the content of the W card can extend of multiple lines.)
** Each card is divided into tokens by a single space character.
** The first token is a single upper-case letter which is the card type.
** The card type determines the other parameters to the card.
** Cards must occur in lexicographical order.
*/
Manifest *manifest_parse(Blob *pContent, int rid, Blob *pErr){
  Manifest *p;
  int seenZ = 0;
  int i, lineNo=0;
  ManifestText x;
  char cPrevType = 0;
  char cType;
  char *z;
  int n;
  char *zUuid;
  int sz = 0;
  int isRepeat, hasSelfRefTag = 0;
  static Bag seen;
  const char *zErr = 0;

  if( rid==0 ){
    isRepeat = 1;
  }else if( bag_find(&seen, rid) ){
    isRepeat = 1;
  }else{
    isRepeat = 0;
    bag_insert(&seen, rid);
  }

  /* Every structural artifact ends with a '\n' character.  Exit early
  ** if that is not the case for this artifact.
  */
  if( !isRepeat ) g.parseCnt[0]++;
  z = blob_materialize(pContent);
  n = blob_size(pContent);
  if( n<=0 || z[n-1]!='\n' ){
    blob_reset(pContent);
    blob_appendf(pErr, "%s", n ? "not terminated with \\n" : "zero-length");
    return 0;
  }

  /* Strip off the PGP signature if there is one.
  */
  remove_pgp_signature(&z, &n);

  /* Verify that the first few characters of the artifact look like
  ** a control artifact.
  */
  if( n<10 || z[0]<'A' || z[0]>'Z' || z[1]!=' ' ){
    blob_reset(pContent);
    blob_appendf(pErr, "line 1 not recognized");
    return 0;
  }
  /* Then verify the Z-card.
  */
  if( verify_z_card(z, n)==2 ){
    blob_reset(pContent);
    blob_appendf(pErr, "incorrect Z-card cksum");
    return 0;
  }

  /* Allocate a Manifest object to hold the parsed control artifact.
  */
  p = fossil_malloc( sizeof(*p) );
  memset(p, 0, sizeof(*p));
  memcpy(&p->content, pContent, sizeof(p->content));
  p->rid = rid;
  blob_zero(pContent);
  pContent = &p->content;

  /* Begin parsing, card by card.
  */
  x.z = z;
  x.zEnd = &z[n];
  x.atEol = 1;
  while( (cType = next_card(&x))!=0 && cType>=cPrevType ){
    lineNo++;
    switch( cType ){
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
        int nTarget = 0, nSrc = 0;
        zName = next_token(&x, 0);
        zTarget = next_token(&x, &nTarget);
        zSrc = next_token(&x, &nSrc);
        if( zName==0 || zTarget==0 ) goto manifest_syntax_error;
        if( p->zAttachName!=0 ) goto manifest_syntax_error;
        defossilize(zName);
        if( !file_is_simple_pathname(zName, 0) ){
          SYNTAX("invalid filename on A-card");
        }
        defossilize(zTarget);
        if( !hname_validate(zTarget,nTarget)
           && !wiki_name_is_wellformed((const unsigned char *)zTarget) ){
          SYNTAX("invalid target on A-card");
        }
        if( zSrc && !hname_validate(zSrc,nSrc) ){
          SYNTAX("invalid source on A-card");
        }
        p->zAttachName = (char*)file_tail(zName);
        p->zAttachSrc = zSrc;
        p->zAttachTarget = zTarget;
        break;
      }

      /*
      **    B <uuid>
      **
      ** A B-line gives the artifact hash for the baseline of a delta-manifest.
      */
      case 'B': {
        if( p->zBaseline ) SYNTAX("more than one B-card");
        p->zBaseline = next_token(&x, &sz);
        if( p->zBaseline==0 ) SYNTAX("missing hash on B-card");
        if( !hname_validate(p->zBaseline,sz) ){
          SYNTAX("invalid hash on B-card");
        }
        break;
      }


      /*
      **     C <comment>
      **
      ** Comment text is fossil-encoded.  There may be no more than
      ** one C line.  C lines are required for manifests, are optional
      ** for Events and Attachments, and are disallowed on all other
      ** control files.
      */
      case 'C': {
        if( p->zComment!=0 ) SYNTAX("more than one C-card");
        p->zComment = next_token(&x, 0);
        if( p->zComment==0 ) SYNTAX("missing comment text on C-card");
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
        if( p->rDate>0.0 ) SYNTAX("more than one D-card");
        p->rDate = db_double(0.0, "SELECT julianday(%Q)", next_token(&x,0));
        if( p->rDate<=0.0 ) SYNTAX("cannot parse date on D-card");
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
        if( p->rEventDate>0.0 ) SYNTAX("more than one E-card");
        p->rEventDate = db_double(0.0,"SELECT julianday(%Q)", next_token(&x,0));
        if( p->rEventDate<=0.0 ) SYNTAX("malformed date on E-card");
        p->zEventId = next_token(&x, &sz);
        if( !hname_validate(p->zEventId, sz) ){
          SYNTAX("malformed hash on E-card");
        }
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
        char *zName, *zPerm, *zPriorName;
        zName = next_token(&x,0);
        if( zName==0 ) SYNTAX("missing filename on F-card");
        defossilize(zName);
        if( !file_is_simple_pathname(zName, 0) ){
          SYNTAX("F-card filename is not a simple path");
        }
        zUuid = next_token(&x, &sz);
        if( p->zBaseline==0 || zUuid!=0 ){
          if( !hname_validate(zUuid,sz) ){
            SYNTAX("F-card hash invalid");
          }
        }
        zPerm = next_token(&x,0);
        zPriorName = next_token(&x,0);
        if( zPriorName ){
          defossilize(zPriorName);
          if( !file_is_simple_pathname(zPriorName, 0) ){
            SYNTAX("F-card old filename is not a simple path");
          }
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
        if( i>0 && fossil_strcmp(p->aFile[i-1].zName, zName)>=0 ){
          SYNTAX("incorrect F-card sort order");
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
        zName = next_token(&x,0);
        zValue = next_token(&x,0);
        if( zName==0 ) SYNTAX("name missing from J-card");
        if( zValue==0 ) zValue = "";
        defossilize(zValue);
        if( p->nField>=p->nFieldAlloc ){
          p->nFieldAlloc = p->nFieldAlloc*2 + 10;
          p->aField = fossil_realloc(p->aField,
                               p->nFieldAlloc*sizeof(p->aField[0]) );
        }
        i = p->nField++;
        p->aField[i].zName = zName;
        p->aField[i].zValue = zValue;
        if( i>0 && fossil_strcmp(p->aField[i-1].zName, zName)>=0 ){
          SYNTAX("incorrect J-card sort order");
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
        if( p->zTicketUuid!=0 ) SYNTAX("more than one K-card");
        p->zTicketUuid = next_token(&x, &sz);
        if( sz!=UUID_SIZE ) SYNTAX("K-card UUID is the wrong size");
        if( !validate16(p->zTicketUuid, UUID_SIZE) ){
          SYNTAX("invalid K-card UUID");
        }
        break;
      }

      /*
      **     L <wikititle>
      **
      ** The wiki page title is fossil-encoded.  There may be no more than
      ** one L line.
      */
      case 'L': {
        if( p->zWikiTitle!=0 ) SYNTAX("more than one L-card");
        p->zWikiTitle = next_token(&x,0);
        if( p->zWikiTitle==0 ) SYNTAX("missing title on L-card");
        defossilize(p->zWikiTitle);
        if( !wiki_name_is_wellformed((const unsigned char *)p->zWikiTitle) ){
          SYNTAX("L-card has malformed wiki name");
        }
        break;
      }

      /*
      **    M <hash>
      **
      ** An M-line identifies another artifact by its hash.  M-lines
      ** occur in clusters only.
      */
      case 'M': {
        zUuid = next_token(&x, &sz);
        if( zUuid==0 ) SYNTAX("missing hash on M-card");
        if( !hname_validate(zUuid,sz) ){
          SYNTAX("Invalid hash on M-card");
        }
        if( p->nCChild>=p->nCChildAlloc ){
          p->nCChildAlloc = p->nCChildAlloc*2 + 10;
          p->azCChild = fossil_realloc(p->azCChild
                                 , p->nCChildAlloc*sizeof(p->azCChild[0]) );
        }
        i = p->nCChild++;
        p->azCChild[i] = zUuid;
        if( i>0 && fossil_strcmp(p->azCChild[i-1], zUuid)>=0 ){
          SYNTAX("M-card in the wrong order");
        }
        break;
      }

      /*
      **    N <uuid>
      **
      ** An N-line identifies the mimetype of wiki or comment text.
      */
      case 'N': {
        if( p->zMimetype!=0 ) SYNTAX("more than one N-card");
        p->zMimetype = next_token(&x,0);
        if( p->zMimetype==0 ) SYNTAX("missing mimetype on N-card");
        defossilize(p->zMimetype);
        break;
      }

      /*
      **     P <uuid> ...
      **
      ** Specify one or more other artifacts which are the parents of
      ** this artifact.  The first parent is the primary parent.  All
      ** others are parents by merge. Note that the initial empty
      ** check-in historically has an empty P-card, so empty P-cards
      ** must be accepted.
      */
      case 'P': {
        while( (zUuid = next_token(&x, &sz))!=0 ){
          if( !hname_validate(zUuid, sz) ){
             SYNTAX("invalid hash on P-card");
          }
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
      **     Q (+|-)<uuid> ?<uuid>?
      **
      ** Specify one or a range of check-ins that are cherrypicked into
      ** this check-in ("+") or backed out of this check-in ("-").
      */
      case 'Q': {
        if( (zUuid=next_token(&x, &sz))==0 ) SYNTAX("missing hash on Q-card");
        if( zUuid[0]!='+' && zUuid[0]!='-' ){
          SYNTAX("Q-card does not begin with '+' or '-'");
        }
        if( !hname_validate(&zUuid[1], sz-1) ){
          SYNTAX("invalid hash on Q-card");
        }
        n = p->nCherrypick;
        p->nCherrypick++;
        p->aCherrypick = fossil_realloc(p->aCherrypick,
                                 p->nCherrypick*sizeof(p->aCherrypick[0]));
        p->aCherrypick[n].zCPTarget = zUuid;
        p->aCherrypick[n].zCPBase = zUuid = next_token(&x, &sz);
        if( zUuid && !hname_validate(zUuid,sz) ){
          SYNTAX("invalid second hash on Q-card");
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
        if( p->zRepoCksum!=0 ) SYNTAX("more than one R-card");
        p->zRepoCksum = next_token(&x, &sz);
        if( sz!=32 ) SYNTAX("wrong size cksum on R-card");
        if( !validate16(p->zRepoCksum, 32) ) SYNTAX("malformed R-card cksum");
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
        char *zName, *zValue;
        zName = next_token(&x, 0);
        if( zName==0 ) SYNTAX("missing name on T-card");
        zUuid = next_token(&x, &sz);
        if( zUuid==0 ) SYNTAX("missing artifact hash on T-card");
        zValue = next_token(&x, 0);
        if( zValue ) defossilize(zValue);
        if( hname_validate(zUuid, sz) ){
          /* A valid artifact hash */
          if( p->zEventId ) SYNTAX("non-self-referential T-card in event");
        }else if( sz==1 && zUuid[0]=='*' ){
          zUuid = 0;
          hasSelfRefTag = 1;
          if( p->zEventId && zName[0]!='+' ){
            SYNTAX("propagating T-card in event");
          }
        }else{
          SYNTAX("malformed artifact hash on T-card");
        }
        defossilize(zName);
        if( zName[0]!='-' && zName[0]!='+' && zName[0]!='*' ){
          SYNTAX("T-card name does not begin with '-', '+', or '*'");
        }
        if( validate16(&zName[1], strlen(&zName[1])) ){
          /* Do not allow tags whose names look like a hash */
          SYNTAX("T-card name looks like a hexadecimal hash");
        }
        if( p->nTag>=p->nTagAlloc ){
          p->nTagAlloc = p->nTagAlloc*2 + 10;
          p->aTag = fossil_realloc(p->aTag, p->nTagAlloc*sizeof(p->aTag[0]) );
        }
        i = p->nTag++;
        p->aTag[i].zName = zName;
        p->aTag[i].zUuid = zUuid;
        p->aTag[i].zValue = zValue;
        if( i>0 ){
          int c = fossil_strcmp(p->aTag[i-1].zName, zName);
          if( c>0 || (c==0 && fossil_strcmp(p->aTag[i-1].zUuid, zUuid)>=0) ){
            SYNTAX("T-card in the wrong order");
          }
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
        if( p->zUser!=0 ) SYNTAX("more than one U-card");
        p->zUser = next_token(&x, 0);
        if( p->zUser==0 || p->zUser[0]==0 ){
          p->zUser = "anonymous";
        }else{
          defossilize(p->zUser);
        }
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
        char *zSize;
        unsigned size, oldsize, c;
        Blob wiki;
        zSize = next_token(&x, 0);
        if( zSize==0 ) SYNTAX("missing size on W-card");
        if( x.atEol==0 ) SYNTAX("no content after W-card");
        for(oldsize=size=0; (c = zSize[0])>='0' && c<='9'; zSize++){
           size = oldsize*10 + c - '0';
           if( size<oldsize ) SYNTAX("size overflow on W-card");
           oldsize = size;
        }
        if( p->zWiki!=0 ) SYNTAX("more than one W-card");
        blob_zero(&wiki);
        if( (&x.z[size+1])>=x.zEnd )SYNTAX("not enough content after W-card");
        p->zWiki = x.z;
        x.z += size;
        if( x.z[0]!='\n' ) SYNTAX("W-card content no \\n terminated");
        x.z[0] = 0;
        x.z++;
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
        zUuid = next_token(&x, &sz);
        if( sz!=32 ) SYNTAX("wrong size for Z-card cksum");
        if( !validate16(zUuid, 32) ) SYNTAX("malformed Z-card cksum");
        seenZ = 1;
        break;
      }
      default: {
        SYNTAX("unrecognized card");
      }
    }
  }
  if( x.z<x.zEnd ) SYNTAX("extra characters at end of card");

  if( p->nCChild>0 ){
    if( p->zAttachName
     || p->zBaseline
     || p->zComment
     || p->rDate>0.0
     || p->zEventId
     || p->nFile>0
     || p->nField>0
     || p->zTicketUuid
     || p->zWikiTitle
     || p->zMimetype
     || p->nParent>0
     || p->nCherrypick>0
     || p->zRepoCksum
     || p->nTag>0
     || p->zUser
     || p->zWiki
    ){
      SYNTAX("cluster contains a card other than M- or Z-");
    }
    if( !seenZ ) SYNTAX("missing Z-card on cluster");
    p->type = CFTYPE_CLUSTER;
  }else if( p->zEventId ){
    if( p->zAttachName ) SYNTAX("A-card in event");
    if( p->zBaseline ) SYNTAX("B-card in event");
    if( p->rDate<=0.0 ) SYNTAX("missing date on event");
    if( p->nFile>0 ) SYNTAX("F-card in event");
    if( p->nField>0 ) SYNTAX("J-card in event");
    if( p->zTicketUuid ) SYNTAX("K-card in event");
    if( p->zWikiTitle!=0 ) SYNTAX("L-card in event");
    if( p->zRepoCksum ) SYNTAX("R-card in event");
    if( p->zWiki==0 ) SYNTAX("missing W-card on event");
    if( !seenZ ) SYNTAX("missing Z-card on event");
    p->type = CFTYPE_EVENT;
  }else if( p->zWiki!=0 || p->zWikiTitle!=0 ){
    if( p->zAttachName ) SYNTAX("A-card in wiki");
    if( p->zBaseline ) SYNTAX("B-card in wiki");
    if( p->rDate<=0.0 ) SYNTAX("missing date on wiki");
    if( p->nFile>0 ) SYNTAX("F-card in wiki");
    if( p->nField>0 ) SYNTAX("J-card in wiki");
    if( p->zTicketUuid ) SYNTAX("K-card in wiki");
    if( p->zWikiTitle==0 ) SYNTAX("missing L-card on wiki");
    if( p->zRepoCksum ) SYNTAX("R-card in wiki");
    if( p->nTag>0 ) SYNTAX("T-card in wiki");
    if( p->zWiki==0 ) SYNTAX("missing W-card on wiki");
    if( !seenZ ) SYNTAX("missing Z-card on wiki");
    p->type = CFTYPE_WIKI;
  }else if( hasSelfRefTag || p->nFile>0 || p->zRepoCksum!=0 || p->zBaseline
      || p->nParent>0 ){
    if( p->zAttachName ) SYNTAX("A-card in manifest");
    if( p->rDate<=0.0 ) SYNTAX("missing date on manifest");
    if( p->nField>0 ) SYNTAX("J-card in manifest");
    if( p->zTicketUuid ) SYNTAX("K-card in manifest");
    p->type = CFTYPE_MANIFEST;
  }else if( p->nField>0 || p->zTicketUuid!=0 ){
    if( p->zAttachName ) SYNTAX("A-card in ticket");
    if( p->rDate<=0.0 ) SYNTAX("missing date on ticket");
    if( p->nField==0 ) SYNTAX("missing J-card on ticket");
    if( p->zTicketUuid==0 ) SYNTAX("missing K-card on ticket");
    if( p->zMimetype) SYNTAX("N-card in ticket");
    if( p->nTag>0 ) SYNTAX("T-card in ticket");
    if( p->zUser==0 ) SYNTAX("missing U-card on ticket");
    if( !seenZ ) SYNTAX("missing Z-card on ticket");
    p->type = CFTYPE_TICKET;
  }else if( p->zAttachName ){
    if( p->rDate<=0.0 ) SYNTAX("missing date on attachment");
    if( p->nTag>0 ) SYNTAX("T-card in attachment");
    if( !seenZ ) SYNTAX("missing Z-card on attachment");
    p->type = CFTYPE_ATTACHMENT;
  }else{
    if( p->rDate<=0.0 ) SYNTAX("missing date on control");
    if( p->zMimetype ) SYNTAX("N-card in control");
    if( !seenZ ) SYNTAX("missing Z-card on control");
    p->type = CFTYPE_CONTROL;
  }
  md5sum_init();
  if( !isRepeat ) g.parseCnt[p->type]++;
  return p;

manifest_syntax_error:
  {
    char *zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    if( zUuid ){
      blob_appendf(pErr, "manifest [%s] ", zUuid);
      fossil_free(zUuid);
    }
  }
  if( zErr ){
    blob_appendf(pErr, "line %d: %s", lineNo, zErr);
  }else{
    blob_appendf(pErr, "unknown error on line %d", lineNo);
  }
  md5sum_init();
  manifest_destroy(p);
  return 0;
}

/*
** Get a manifest given the rid for the control artifact.  Return
** a pointer to the manifest on success or NULL if there is a failure.
*/
Manifest *manifest_get(int rid, int cfType, Blob *pErr){
  Blob content;
  Manifest *p;
  if( !rid ) return 0;
  p = manifest_cache_find(rid);
  if( p ){
    if( cfType!=CFTYPE_ANY && cfType!=p->type ){
      manifest_cache_insert(p);
      p = 0;
    }
    return p;
  }
  content_get(rid, &content);
  p = manifest_parse(&content, rid, pErr);
  if( p && cfType!=CFTYPE_ANY && cfType!=p->type ){
    manifest_destroy(p);
    p = 0;
  }
  return p;
}

/*
** Given a check-in name, load and parse the manifest for that check-in.
** Throw a fatal error if anything goes wrong.
*/
Manifest *manifest_get_by_name(const char *zName, int *pRid){
  int rid;
  Manifest *p;

  rid = name_to_typed_rid(zName, "ci");
  if( !is_a_version(rid) ){
    fossil_fatal("no such check-in: %s", zName);
  }
  if( pRid ) *pRid = rid;
  p = manifest_get(rid, CFTYPE_MANIFEST, 0);
  if( p==0 ){
    fossil_fatal("cannot parse manifest for check-in: %s", zName);
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
  sqlite3_open(":memory:", &g.db);
  if( g.argc!=3 && g.argc!=4 ){
    usage("FILENAME");
  }
  blob_read_from_file(&b, g.argv[2]);
  if( g.argc>3 ) n = atoi(g.argv[3]);
  for(i=0; i<n; i++){
    Blob b2;
    Blob err;
    blob_copy(&b2, &b);
    blob_zero(&err);
    p = manifest_parse(&b2, 0, &err);
    if( p==0 ) fossil_print("ERROR: %s\n", blob_str(&err));
    blob_reset(&err);
    manifest_destroy(p);
  }
}

/*
** Fetch the baseline associated with the delta-manifest p.
** Return 0 on success.  If unable to parse the baseline,
** throw an error.  If the baseline is a manifest, throw an
** error if throwError is true, or record that p is an orphan
** and return 1 if throwError is false.
*/
static int fetch_baseline(Manifest *p, int throwError){
  if( p->zBaseline!=0 && p->pBaseline==0 ){
    int rid = uuid_to_rid(p->zBaseline, 1);
    p->pBaseline = manifest_get(rid, CFTYPE_MANIFEST, 0);
    if( p->pBaseline==0 ){
      if( !throwError ){
        db_multi_exec(
           "INSERT OR IGNORE INTO orphan(rid, baseline) VALUES(%d,%d)",
           p->rid, rid
        );
        return 1;
      }
      fossil_fatal("cannot access baseline manifest %S", p->zBaseline);
    }
  }
  return 0;
}

/*
** Rewind a manifest-file iterator back to the beginning of the manifest.
*/
void manifest_file_rewind(Manifest *p){
  p->iFile = 0;
  fetch_baseline(p, 1);
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
      }else if( (cmp = fossil_strcmp(pB->aFile[pB->iFile].zName,
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
      }else if( p->aFile[p->iFile].zUuid ){
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
** Compute an appropriate mlink.mperm integer for the permission string
** of a file.
*/
int manifest_file_mperm(ManifestFile *pFile){
  int mperm = PERM_REG;
  if( pFile && pFile->zPerm){
    if( strstr(pFile->zPerm,"x")!=0 ){
      mperm = PERM_EXE;
    }else if( strstr(pFile->zPerm,"l")!=0 ){
      mperm = PERM_LNK;
    }
  }
  return mperm;
}

/*
** Add a single entry to the mlink table.  Also add the filename to
** the filename table if it is not there already.
**
** An mlink entry is always created if isPrimary is true.  But if
** isPrimary is false (meaning that pmid is a merge parent of mid)
** then the mlink entry is only created if there is already an mlink
** from primary parent for the same file.
*/
static void add_one_mlink(
  int pmid,                 /* The parent manifest */
  const char *zFromUuid,    /* Artifact hash for content in parent */
  int mid,                  /* The record ID of the manifest */
  const char *zToUuid,      /* artifact hash for content in child */
  const char *zFilename,    /* Filename */
  const char *zPrior,       /* Previous filename. NULL if unchanged */
  int isPublic,             /* True if mid is not a private manifest */
  int isPrimary,            /* pmid is the primary parent of mid */
  int mperm                 /* 1: exec, 2: symlink */
){
  int fnid, pfnid, pid, fid;
  int doInsert;
  static Stmt s1, s2;

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
    if( isPublic ) content_make_public(fid);
  }
  if( isPrimary ){
    doInsert = 1;
  }else{
    db_static_prepare(&s2,
      "SELECT 1 FROM mlink WHERE mid=:m AND fnid=:n AND NOT isaux"
    );
    db_bind_int(&s2, ":m", mid);
    db_bind_int(&s2, ":n", fnid);
    doInsert = db_step(&s2)==SQLITE_ROW;
    db_reset(&s2);
  }
  if( doInsert ){
    db_static_prepare(&s1,
      "INSERT INTO mlink(mid,fid,pmid,pid,fnid,pfnid,mperm,isaux)"
      "VALUES(:m,:f,:pm,:p,:n,:pfn,:mp,:isaux)"
    );
    db_bind_int(&s1, ":m", mid);
    db_bind_int(&s1, ":f", fid);
    db_bind_int(&s1, ":pm", pmid);
    db_bind_int(&s1, ":p", pid);
    db_bind_int(&s1, ":n", fnid);
    db_bind_int(&s1, ":pfn", pfnid);
    db_bind_int(&s1, ":mp", mperm);
    db_bind_int(&s1, ":isaux", isPrimary==0);
    db_exec(&s1);
  }
  if( pid && fid ){
    content_deltify(pid, &fid, 1, 0);
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
static ManifestFile *manifest_file_seek_base(
  Manifest *p,             /* Manifest to search */
  const char *zName,       /* Name of the file we are looking for */
  int bBest                /* 0: exact match only.  1: closest match */
){
  int lwr, upr;
  int c;
  int i;
  lwr = 0;
  upr = p->nFile - 1;
  if( p->iFile>=lwr && p->iFile<upr ){
    c = fossil_strcmp(p->aFile[p->iFile+1].zName, zName);
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
    c = fossil_strcmp(p->aFile[i].zName, zName);
    if( c<0 ){
      lwr = i+1;
    }else if( c>0 ){
      upr = i-1;
    }else{
      p->iFile = i;
      return &p->aFile[i];
    }
  }
  if( bBest ){
    if( lwr>=p->nFile ) lwr = p->nFile-1;
    i = (int)strlen(zName);
    if( strncmp(zName, p->aFile[lwr].zName, i)==0 ) return &p->aFile[lwr];
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
ManifestFile *manifest_file_seek(Manifest *p, const char *zName, int bBest){
  ManifestFile *pFile;

  pFile = manifest_file_seek_base(p, zName, p->zBaseline ? 0 : bBest);
  if( pFile && pFile->zUuid==0 ) return 0;
  if( pFile==0 && p->zBaseline ){
    fetch_baseline(p, 1);
    pFile = manifest_file_seek_base(p->pBaseline, zName,bBest);
  }
  return pFile;
}

/*
** Look for a file in a manifest, taking the case-sensitive option
** into account.  If case-sensitive is off, then files in any case
** will match.
*/
ManifestFile *manifest_file_find(Manifest *p, const char *zName){
  int i;
  Manifest *pBase;
  if( filenames_are_case_sensitive() ){
    return manifest_file_seek(p, zName, 0);
  }
  for(i=0; i<p->nFile; i++){
    if( fossil_stricmp(zName, p->aFile[i].zName)==0 ){
      return &p->aFile[i];
    }
  }
  if( p->zBaseline==0 ) return 0;
  fetch_baseline(p, 1);
  pBase = p->pBaseline;
  if( pBase==0 ) return 0;
  for(i=0; i<pBase->nFile; i++){
    if( fossil_stricmp(zName, pBase->aFile[i].zName)==0 ){
      return &pBase->aFile[i];
    }
  }
  return 0;
}

/*
** Add mlink table entries associated with manifest cid, pChild.  The
** parent manifest is pid, pParent.  One of either pChild or pParent
** will be NULL and it will be computed based on cid/pid.
**
** A single mlink entry is added for every file that changed content,
** name, and/or permissions going from pid to cid.
**
** Deleted files have mlink.fid=0.
** Added files have mlink.pid=0.
** File added by merge have mlink.pid=-1
** Edited files have both mlink.pid!=0 and mlink.fid!=0
**
** Many mlink entries for merge parents will only be added if another mlink
** entry already exists for the same file from the primary parent.  Therefore,
** to ensure that all merge-parent mlink entries are properly created:
**
**    (1) Make this routine a no-op if pParent is a merge parent and the
**        primary parent is a phantom.
**    (2) Invoke this routine recursively for merge-parents if pParent is the
**        primary parent.
*/
static void add_mlink(
  int pmid, Manifest *pParent,  /* Parent check-in */
  int mid,  Manifest *pChild,   /* The child check-in */
  int isPrim                    /* TRUE if pmid is the primary parent of mid */
){
  Blob otherContent;
  int otherRid;
  int i, rc;
  ManifestFile *pChildFile, *pParentFile;
  Manifest **ppOther;
  static Stmt eq;
  int isPublic;                /* True if pChild is non-private */

  /* If mlink table entires are already exist for the pmid-to-mid transition,
  ** then abort early doing no work.
  */
  db_static_prepare(&eq, "SELECT 1 FROM mlink WHERE mid=:mid AND pmid=:pmid");
  db_bind_int(&eq, ":mid", mid);
  db_bind_int(&eq, ":pmid", pmid);
  rc = db_step(&eq);
  db_reset(&eq);
  if( rc==SQLITE_ROW ) return;

  /* Compute the value of the missing pParent or pChild parameter.
  ** Fetch the baseline check-ins for both.
  */
  assert( pParent==0 || pChild==0 );
  if( pParent==0 ){
    ppOther = &pParent;
    otherRid = pmid;
  }else{
    ppOther = &pChild;
    otherRid = mid;
  }
  if( (*ppOther = manifest_cache_find(otherRid))==0 ){
    content_get(otherRid, &otherContent);
    if( blob_size(&otherContent)==0 ) return;
    *ppOther = manifest_parse(&otherContent, otherRid, 0);
    if( *ppOther==0 ) return;
  }
  if( fetch_baseline(pParent, 0) || fetch_baseline(pChild, 0) ){
    manifest_destroy(*ppOther);
    return;
  }
  isPublic = !content_is_private(mid);

  /* If pParent is not the primary parent of pChild, and the primary
  ** parent of pChild is a phantom, then abort this routine without
  ** doing any work.  The mlink entries will be computed when the
  ** primary parent dephantomizes.
  */
  if( !isPrim && otherRid==mid
   && !db_exists("SELECT 1 FROM blob WHERE uuid=%Q AND size>0",
                 pChild->azParent[0])
  ){
    manifest_cache_insert(*ppOther);
    return;
  }

  /* Try to make the parent manifest a delta from the child, if that
  ** is an appropriate thing to do.  For a new baseline, make the
  ** previous baseline a delta from the current baseline.
  */
  if( (pParent->zBaseline==0)==(pChild->zBaseline==0) ){
    content_deltify(pmid, &mid, 1, 0);
  }else if( pChild->zBaseline==0 && pParent->zBaseline!=0 ){
    content_deltify(pParent->pBaseline->rid, &mid, 1, 0);
  }

  /* Remember all children less than a few seconds younger than their parent,
  ** as we might want to fudge the times for those children.
  */
  if( pChild->rDate<pParent->rDate+AGE_FUDGE_WINDOW
      && manifest_crosslink_busy
  ){
    db_multi_exec(
       "INSERT OR REPLACE INTO time_fudge VALUES(%d, %.17g, %d, %.17g);",
       pParent->rid, pParent->rDate, pChild->rid, pChild->rDate
    );
  }

  /* First look at all files in pChild, ignoring its baseline.  This
  ** is where most of the changes will be found.
  */
  for(i=0, pChildFile=pChild->aFile; i<pChild->nFile; i++, pChildFile++){
    int mperm = manifest_file_mperm(pChildFile);
    if( pChildFile->zPrior ){
       pParentFile = manifest_file_seek(pParent, pChildFile->zPrior, 0);
       if( pParentFile ){
         /* File with name change */
         add_one_mlink(pmid, pParentFile->zUuid, mid, pChildFile->zUuid,
                       pChildFile->zName, pChildFile->zPrior,
                       isPublic, isPrim, mperm);
       }else{
         /* File name changed, but the old name is not found in the parent!
         ** Treat this like a new file. */
         add_one_mlink(pmid, 0, mid, pChildFile->zUuid, pChildFile->zName, 0,
                       isPublic, isPrim, mperm);
       }
    }else{
       pParentFile = manifest_file_seek(pParent, pChildFile->zName, 0);
       if( pParentFile==0 ){
         if( pChildFile->zUuid ){
           /* A new file */
           add_one_mlink(pmid, 0, mid, pChildFile->zUuid, pChildFile->zName, 0,
                         isPublic, isPrim, mperm);
         }
       }else if( fossil_strcmp(pChildFile->zUuid, pParentFile->zUuid)!=0
              || manifest_file_mperm(pParentFile)!=mperm ){
         /* Changes in file content or permissions */
         add_one_mlink(pmid, pParentFile->zUuid, mid, pChildFile->zUuid,
                       pChildFile->zName, 0, isPublic, isPrim, mperm);
       }
    }
  }
  if( pParent->zBaseline && pChild->zBaseline ){
    /* Both parent and child are delta manifests.  Look for files that
    ** are deleted or modified in the parent but which reappear or revert
    ** to baseline in the child and show such files as being added or changed
    ** in the child. */
    for(i=0, pParentFile=pParent->aFile; i<pParent->nFile; i++, pParentFile++){
      if( pParentFile->zUuid ){
        pChildFile = manifest_file_seek_base(pChild, pParentFile->zName, 0);
        if( pChildFile==0 ){
          /* The child file reverts to baseline.  Show this as a change */
          pChildFile = manifest_file_seek(pChild, pParentFile->zName, 0);
          if( pChildFile ){
            add_one_mlink(pmid, pParentFile->zUuid, mid, pChildFile->zUuid,
                          pChildFile->zName, 0, isPublic, isPrim,
                          manifest_file_mperm(pChildFile));
          }
        }
      }else{
        pChildFile = manifest_file_seek(pChild, pParentFile->zName, 0);
        if( pChildFile ){
          /* File resurrected in the child after having been deleted in
          ** the parent.  Show this as an added file. */
          add_one_mlink(pmid, 0, mid, pChildFile->zUuid, pChildFile->zName, 0,
                        isPublic, isPrim, manifest_file_mperm(pChildFile));
        }
      }
    }
  }else if( pChild->zBaseline==0 ){
    /* pChild is a baseline.  Look for files that are present in pParent
    ** but are missing from pChild and mark them as having been deleted. */
    manifest_file_rewind(pParent);
    while( (pParentFile = manifest_file_next(pParent,0))!=0 ){
      pChildFile = manifest_file_seek(pChild, pParentFile->zName, 0);
      if( pChildFile==0 && pParentFile->zUuid!=0 ){
        add_one_mlink(pmid, pParentFile->zUuid, mid, 0, pParentFile->zName, 0,
                      isPublic, isPrim, 0);
      }
    }
  }
  manifest_cache_insert(*ppOther);

  /* If pParent is the primary parent of pChild, also run this analysis
  ** for all merge parents of pChild
  */
  if( isPrim ){
    for(i=1; i<pChild->nParent; i++){
      pmid = uuid_to_rid(pChild->azParent[i], 0);
      if( pmid<=0 ) continue;
      add_mlink(pmid, 0, mid, pChild, 0);
    }
  }
}

/*
** For a check-in with RID "rid" that has nParent parent check-ins given
** by the hashes in azParent[], create all appropriate plink and mlink table
** entries.
**
** The primary parent is the first hash on the azParent[] list.
**
** Return the RID of the primary parent.
*/
static int manifest_add_checkin_linkages(
  int rid,                   /* The RID of the check-in */
  Manifest *p,               /* Manifest for this check-in */
  int nParent,               /* Number of parents for this check-in */
  char **azParent            /* hashes for each parent */
){
  int i;
  int parentid = 0;
  char zBaseId[30];    /* Baseline manifest RID for deltas.  "NULL" otherwise */
  Stmt q;

  if( p->zBaseline ){
     sqlite3_snprintf(sizeof(zBaseId), zBaseId, "%d",
                      uuid_to_rid(p->zBaseline,1));
  }else{
     sqlite3_snprintf(sizeof(zBaseId), zBaseId, "NULL");
  }
  for(i=0; i<nParent; i++){
    int pid = uuid_to_rid(azParent[i], 1);
    db_multi_exec(
       "INSERT OR IGNORE INTO plink(pid, cid, isprim, mtime, baseid)"
        "VALUES(%d, %d, %d, %.17g, %s)",
       pid, rid, i==0, p->rDate, zBaseId/*safe-for-%s*/);
    if( i==0 ) parentid = pid;
  }
  add_mlink(parentid, 0, rid, p, 1);
  if( nParent>1 ){
    /* Change MLINK.PID from 0 to -1 for files that are added by merge. */
    db_multi_exec(
      "UPDATE mlink SET pid=-1"
      " WHERE mid=%d"
      "   AND pid=0"
      "   AND fnid IN "
      "  (SELECT fnid FROM mlink WHERE mid=%d GROUP BY fnid"
      "    HAVING count(*)<%d)",
      rid, rid, nParent
    );
  }
  db_prepare(&q, "SELECT cid, isprim FROM plink WHERE pid=%d", rid);
  while( db_step(&q)==SQLITE_ROW ){
    int cid = db_column_int(&q, 0);
    int isprim = db_column_int(&q, 1);
    add_mlink(rid, p, cid, 0, isprim);
  }
  db_finalize(&q);
  if( nParent==0 ){
    /* For root files (files without parents) add mlink entries
    ** showing all content as new. */
    int isPublic = !content_is_private(rid);
    for(i=0; i<p->nFile; i++){
      add_one_mlink(0, 0, rid, p->aFile[i].zUuid, p->aFile[i].zName, 0,
                    isPublic, 1, manifest_file_mperm(&p->aFile[i]));
    }
  }
  return parentid;
}

/*
** There exists a "parent" tag against checkin rid that has value zValue.
** If value is well-formed (meaning that it is a list of hashes), then use
** zValue to reparent check-in rid.
*/
void manifest_reparent_checkin(int rid, const char *zValue){
  int nParent = 0;
  char *zCopy = 0;
  char **azParent = 0;
  Manifest *p = 0;
  int i, j;
  int n = (int)strlen(zValue);
  int mxParent = (n+1)/(HNAME_MIN+1);

  if( mxParent<1 ) return;
  zCopy = fossil_strdup(zValue);
  azParent = fossil_malloc( sizeof(azParent[0])*mxParent );
  for(nParent=0, i=0; zCopy[i]; i++){
    char *z = &zCopy[i];
    azParent[nParent++] = z;
    if( nParent>mxParent ) goto reparent_abort;
    for(j=HNAME_MIN; z[j]>' '; j++){}
    if( !hname_validate(z, j) ) goto reparent_abort;
    if( z[j]==0 ) break;
    z[j] = 0;
    i += j;
  }
  if( !db_exists("SELECT 1 FROM plink WHERE cid=%d AND pid=%d",
                 rid, uuid_to_rid(azParent[0],0))
  ){
    p = manifest_get(rid, CFTYPE_MANIFEST, 0);
  }
  if( p!=0 ){
    db_multi_exec(
       "DELETE FROM plink WHERE cid=%d;"
       "DELETE FROM mlink WHERE mid=%d;",
       rid, rid
    );
    manifest_add_checkin_linkages(rid,p,nParent,azParent);
  }
  manifest_destroy(p);
reparent_abort:
  fossil_free(azParent);
  fossil_free(zCopy);
}

/*
** Setup to do multiple manifest_crosslink() calls.
**
** This routine creates TEMP tables for holding information for
** processing that must be deferred until all artifacts have been
** seen at least once.  The deferred processing is accomplished
** by the call to manifest_crosslink_end().
*/
void manifest_crosslink_begin(void){
  assert( manifest_crosslink_busy==0 );
  manifest_crosslink_busy = 1;
  db_begin_transaction();
  db_multi_exec(
     "CREATE TEMP TABLE pending_tkt(uuid TEXT UNIQUE);"
     "CREATE TEMP TABLE time_fudge("
     "  mid INTEGER PRIMARY KEY,"    /* The rid of a manifest */
     "  m1 REAL,"                    /* The timestamp on mid */
     "  cid INTEGER,"                /* A child or mid */
     "  m2 REAL"                     /* Timestamp on the child */
     ");"
  );
}

#if INTERFACE
/* Timestamps might be adjusted slightly to ensure that check-ins appear
** on the timeline in chronological order.  This is the maximum amount
** of the adjustment window, in days.
*/
#define AGE_FUDGE_WINDOW      (2.0/86400.0)       /* 2 seconds */

/* This is increment (in days) by which timestamps are adjusted for
** use on the timeline.
*/
#define AGE_ADJUST_INCREMENT  (25.0/86400000.0)   /* 25 milliseconds */

#endif /* LOCAL_INTERFACE */

/*
** Finish up a sequence of manifest_crosslink calls.
*/
int manifest_crosslink_end(int flags){
  Stmt q, u;
  int i;
  int rc = TH_OK;
  int permitHooks = (flags & MC_PERMIT_HOOKS);
  const char *zScript = 0;
  assert( manifest_crosslink_busy==1 );
  if( permitHooks ){
    rc = xfer_run_common_script();
    if( rc==TH_OK ){
      zScript = xfer_ticket_code();
    }
  }
  db_prepare(&q,
     "SELECT rid, value FROM tagxref"
     " WHERE tagid=%d AND tagtype=1",
     TAG_PARENT
  );
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q,0);
    const char *zValue = db_column_text(&q,1);
    manifest_reparent_checkin(rid, zValue);
  }
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid FROM pending_tkt");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    ticket_rebuild_entry(zUuid);
    if( permitHooks && rc==TH_OK ){
      rc = xfer_run_script(zScript, zUuid, 0);
    }
  }
  db_finalize(&q);
  db_multi_exec("DROP TABLE pending_tkt");

  /* If multiple check-ins happen close together in time, adjust their
  ** times by a few milliseconds to make sure they appear in chronological
  ** order.
  */
  db_prepare(&q,
      "UPDATE time_fudge SET m1=m2-:incr WHERE m1>=m2 AND m1<m2+:window"
  );
  db_bind_double(&q, ":incr", AGE_ADJUST_INCREMENT);
  db_bind_double(&q, ":window", AGE_FUDGE_WINDOW);
  db_prepare(&u,
      "UPDATE time_fudge SET m2="
         "(SELECT x.m1 FROM time_fudge AS x WHERE x.mid=time_fudge.cid)"
  );
  for(i=0; i<30; i++){
    db_step(&q);
    db_reset(&q);
    if( sqlite3_changes(g.db)==0 ) break;
    db_step(&u);
    db_reset(&u);
  }
  db_finalize(&q);
  db_finalize(&u);
  if( db_exists("SELECT 1 FROM time_fudge") ){
    db_multi_exec(
      "UPDATE event SET mtime=(SELECT m1 FROM time_fudge WHERE mid=objid)"
      " WHERE objid IN (SELECT mid FROM time_fudge);"
    );
  }
  db_multi_exec("DROP TABLE time_fudge;");

  db_end_transaction(0);
  manifest_crosslink_busy = 0;
  return ( rc!=TH_ERROR );
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
    "SELECT \"%w\" FROM ticket WHERE tkt_uuid=%Q",
    zTitleExpr, pManifest->zTicketUuid
  );
  if( !isNew ){
    for(i=0; i<pManifest->nField; i++){
      if( fossil_strcmp(pManifest->aField[i].zName, zStatusColumn)==0 ){
        zNewStatus = pManifest->aField[i].zValue;
      }
    }
    if( zNewStatus ){
      blob_appendf(&comment, "%h ticket [%!S|%S]: <i>%h</i>",
         zNewStatus, pManifest->zTicketUuid, pManifest->zTicketUuid, zTitle
      );
      if( pManifest->nField>1 ){
        blob_appendf(&comment, " plus %d other change%s",
          pManifest->nField-1, pManifest->nField==2 ? "" : "s");
      }
      blob_appendf(&brief, "%h ticket [%!S|%S].",
                   zNewStatus, pManifest->zTicketUuid, pManifest->zTicketUuid);
    }else{
      zNewStatus = db_text("unknown",
         "SELECT \"%w\" FROM ticket WHERE tkt_uuid=%Q",
         zStatusColumn, pManifest->zTicketUuid
      );
      blob_appendf(&comment, "Ticket [%!S|%S] <i>%h</i> status still %h with "
           "%d other change%s",
           pManifest->zTicketUuid, pManifest->zTicketUuid, zTitle, zNewStatus,
           pManifest->nField, pManifest->nField==1 ? "" : "s"
      );
      fossil_free(zNewStatus);
      blob_appendf(&brief, "Ticket [%!S|%S]: %d change%s",
           pManifest->zTicketUuid, pManifest->zTicketUuid, pManifest->nField,
           pManifest->nField==1 ? "" : "s"
      );
    }
  }else{
    blob_appendf(&comment, "New ticket [%!S|%S] <i>%h</i>.",
      pManifest->zTicketUuid, pManifest->zTicketUuid, zTitle
    );
    blob_appendf(&brief, "New ticket [%!S|%S].", pManifest->zTicketUuid,
        pManifest->zTicketUuid);
  }
  fossil_free(zTitle);
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
** Add an extra line of text to the end of a manifest to prevent it being
** recognized as a valid manifest.
**
** This routine is called prior to writing out the text of a manifest as
** the "manifest" file in the root of a repository when
** "fossil setting manifest on" is enabled.  That way, if the files of
** the project are imported into a different Fossil project, the manifest
** file will not be interpreted as a control artifact in that other project.
**
** Normally it is sufficient to simply append the extra line of text.
** However, if the manifest is PGP signed then the extra line has to be
** inserted before the PGP signature (thus invalidating the signature).
*/
void sterilize_manifest(Blob *p){
  char *z, *zOrig;
  int n, nOrig;
  static const char zExtraLine[] =
      "# Remove this line to create a well-formed manifest.\n";

  z = zOrig = blob_materialize(p);
  n = nOrig = blob_size(p);
  remove_pgp_signature(&z, &n);
  if( z==zOrig ){
    blob_append(p, zExtraLine, -1);
  }else{
    int iEnd;
    Blob copy;
    memcpy(&copy, p, sizeof(copy));
    blob_init(p, 0, 0);
    iEnd = (int)(&z[n] - zOrig);
    blob_append(p, zOrig, iEnd);
    blob_append(p, zExtraLine, -1);
    blob_append(p, &zOrig[iEnd], -1);
    blob_zero(&copy);
  }
}

/*
** This is the comparison function used to sort the tag array.
*/
static int tag_compare(const void *a, const void *b){
  struct TagType *pA = (struct TagType*)a;
  struct TagType *pB = (struct TagType*)b;
  int c;
  c = fossil_strcmp(pA->zUuid, pB->zUuid);
  if( c==0 ){
    c = fossil_strcmp(pA->zName, pB->zName);
  }
  return c;
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
** This routine always resets the pContent blob before returning.
**
** Historical note:  This routine original processed manifests only.
** Processing for other control artifacts was added later.  The name
** of the routine, "manifest_crosslink", and the name of this source
** file, is a legacy of its original use.
*/
int manifest_crosslink(int rid, Blob *pContent, int flags){
  int i, rc = TH_OK;
  Manifest *p;
  int parentid = 0;
  int permitHooks = (flags & MC_PERMIT_HOOKS);
  const char *zScript = 0;
  const char *zUuid = 0;

  if( (p = manifest_cache_find(rid))!=0 ){
    blob_reset(pContent);
  }else if( (p = manifest_parse(pContent, rid, 0))==0 ){
    assert( blob_is_reset(pContent) || pContent==0 );
    if( (flags & MC_NO_ERRORS)==0 ){
      fossil_error(1, "syntax error in manifest [%S]",
                   db_text(0, "SELECT uuid FROM blob WHERE rid=%d",rid));
    }
    return 0;
  }
  if( g.xlinkClusterOnly && p->type!=CFTYPE_CLUSTER ){
    manifest_destroy(p);
    assert( blob_is_reset(pContent) );
    if( (flags & MC_NO_ERRORS)==0 ) fossil_error(1, "no manifest");
    return 0;
  }
  if( p->type==CFTYPE_MANIFEST && fetch_baseline(p, 0) ){
    manifest_destroy(p);
    assert( blob_is_reset(pContent) );
    if( (flags & MC_NO_ERRORS)==0 ){
      fossil_error(1, "cannot fetch baseline for manifest [%S]",
                   db_text(0, "SELECT uuid FROM blob WHERE rid=%d",rid));
    }
    return 0;
  }
  db_begin_transaction();
  if( p->type==CFTYPE_MANIFEST ){
    if( permitHooks ){
      zScript = xfer_commit_code();
      zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
    }
    if( !db_exists("SELECT 1 FROM mlink WHERE mid=%d", rid) ){
      char *zCom;
      parentid = manifest_add_checkin_linkages(rid,p,p->nParent,p->azParent);
      search_doc_touch('c', rid, 0);
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,user,comment,"
                           "bgcolor,euser,ecomment,omtime)"
        "VALUES('ci',"
        "  coalesce("
        "    (SELECT julianday(value) FROM tagxref WHERE tagid=%d AND rid=%d),"
        "    %.17g"
        "  ),"
        "  %d,%Q,%Q,"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d AND tagtype>0),"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d),"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d),%.17g);",
        TAG_DATE, rid, p->rDate,
        rid, p->zUser, p->zComment,
        TAG_BGCOLOR, rid,
        TAG_USER, rid,
        TAG_COMMENT, rid, p->rDate
      );
      zCom = db_text(0, "SELECT coalesce(ecomment, comment) FROM event"
                        " WHERE rowid=last_insert_rowid()");
      wiki_extract_links(zCom, rid, 0, p->rDate, 1, WIKI_INLINE);
      fossil_free(zCom);

      /* If this is a delta-manifest, record the fact that this repository
      ** contains delta manifests, to free the "commit" logic to generate
      ** new delta manifests.
      */
      if( p->zBaseline!=0 ){
        static int once = 1;
        if( once ){
          db_set_int("seen-delta-manifest", 1, 0);
          once = 0;
        }
      }
    }
  }
  if( p->type==CFTYPE_CLUSTER ){
    static Stmt del1;
    tag_insert("cluster", 1, 0, rid, p->rDate, rid);
    db_static_prepare(&del1, "DELETE FROM unclustered WHERE rid=:rid");
    for(i=0; i<p->nCChild; i++){
      int mid;
      mid = uuid_to_rid(p->azCChild[i], 1);
      if( mid>0 ){
        db_bind_int(&del1, ":rid", mid);
        db_step(&del1);
        db_reset(&del1);
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
          case '-':  type = 0;  break;  /* Cancel prior occurrences */
          case '+':  type = 1;  break;  /* Apply to target only */
          case '*':  type = 2;  break;  /* Propagate to descendants */
          default:
            fossil_error(1, "unknown tag type in manifest: %s", p->aTag);
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
    fossil_free(zTag);
    prior = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=%d AND mtime<%.17g"
      " ORDER BY mtime DESC",
      tagid, p->rDate
    );
    if( prior ){
      content_deltify(prior, &rid, 1, 0);
    }
    if( nWiki>0 ){
      zComment = mprintf("Changes to wiki page [%h]", p->zWikiTitle);
    }else{
      zComment = mprintf("Deleted wiki page [%h]", p->zWikiTitle);
    }
    search_doc_touch('w',rid,p->zWikiTitle);
    db_multi_exec(
      "REPLACE INTO event(type,mtime,objid,user,comment,"
      "                  bgcolor,euser,ecomment)"
      "VALUES('w',%.17g,%d,%Q,%Q,"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d AND tagtype>1),"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d),"
      "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d));",
      p->rDate, rid, p->zUser, zComment,
      TAG_BGCOLOR, rid,
      TAG_USER, rid,
      TAG_COMMENT, rid
    );
    fossil_free(zComment);
  }
  if( p->type==CFTYPE_EVENT ){
    char *zTag = mprintf("event-%s", p->zEventId);
    int tagid = tag_findid(zTag, 1);
    int prior, subsequent;
    int nWiki;
    char zLength[40];
    Stmt qatt;
    while( fossil_isspace(p->zWiki[0]) ) p->zWiki++;
    nWiki = strlen(p->zWiki);
    sqlite3_snprintf(sizeof(zLength), zLength, "%d", nWiki);
    tag_insert(zTag, 1, zLength, rid, p->rDate, rid);
    fossil_free(zTag);
    prior = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=%d AND mtime<%.17g AND rid!=%d"
      " ORDER BY mtime DESC",
      tagid, p->rDate, rid
    );
    subsequent = db_int(0,
      "SELECT rid FROM tagxref"
      " WHERE tagid=%d AND mtime>=%.17g AND rid!=%d"
      " ORDER BY mtime",
      tagid, p->rDate, rid
    );
    if( prior ){
      content_deltify(prior, &rid, 1, 0);
      if( !subsequent ){
        db_multi_exec(
          "DELETE FROM event"
          " WHERE type='e'"
          "   AND tagid=%d"
          "   AND objid IN (SELECT rid FROM tagxref WHERE tagid=%d)",
          tagid, tagid
        );
      }
    }
    if( subsequent ){
      content_deltify(rid, &subsequent, 1, 0);
    }else{
      search_doc_touch('e',rid,0);
      db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,tagid,user,comment,bgcolor)"
        "VALUES('e',%.17g,%d,%d,%Q,%Q,"
        "  (SELECT value FROM tagxref WHERE tagid=%d AND rid=%d));",
        p->rEventDate, rid, tagid, p->zUser, p->zComment,
        TAG_BGCOLOR, rid
      );
    }
    /* Locate and update comment for any attachments */
    db_prepare(&qatt,
       "SELECT attachid, src, target, filename FROM attachment"
       " WHERE target=%Q",
       p->zEventId
    );
    while( db_step(&qatt)==SQLITE_ROW ){
      const char *zAttachId = db_column_text(&qatt, 0);
      const char *zSrc = db_column_text(&qatt, 1);
      const char *zTarget = db_column_text(&qatt, 2);
      const char *zName = db_column_text(&qatt, 3);
      const char isAdd = (zSrc && zSrc[0]) ? 1 : 0;
      char *zComment;
      if( isAdd ){
        zComment = mprintf(
             "Add attachment [/artifact/%!S|%h] to"
             " tech note [/technote/%!S|%S]",
             zSrc, zName, zTarget, zTarget);
      }else{
        zComment = mprintf(
             "Delete attachment \"%h\" from"
             " tech note [/technote/%!S|%S]",
             zName, zTarget, zTarget);
      }
      db_multi_exec("UPDATE event SET comment=%Q, type='e'"
                       " WHERE objid=%Q",
                    zComment, zAttachId);
      fossil_free(zComment);
    }
    db_finalize(&qatt);
  }
  if( p->type==CFTYPE_TICKET ){
    char *zTag;
    Stmt qatt;
    assert( manifest_crosslink_busy==1 );
    zTag = mprintf("tkt-%s", p->zTicketUuid);
    tag_insert(zTag, 1, 0, rid, p->rDate, rid);
    fossil_free(zTag);
    db_multi_exec("INSERT OR IGNORE INTO pending_tkt VALUES(%Q)",
                  p->zTicketUuid);
    /* Locate and update comment for any attachments */
    db_prepare(&qatt,
       "SELECT attachid, src, target, filename FROM attachment"
       " WHERE target=%Q",
       p->zTicketUuid
    );
    while( db_step(&qatt)==SQLITE_ROW ){
      const char *zAttachId = db_column_text(&qatt, 0);
      const char *zSrc = db_column_text(&qatt, 1);
      const char *zTarget = db_column_text(&qatt, 2);
      const char *zName = db_column_text(&qatt, 3);
      const char isAdd = (zSrc && zSrc[0]) ? 1 : 0;
      char *zComment;
      if( isAdd ){
        zComment = mprintf(
             "Add attachment [/artifact/%!S|%h] to ticket [%!S|%S]",
             zSrc, zName, zTarget, zTarget);
      }else{
        zComment = mprintf("Delete attachment \"%h\" from ticket [%!S|%S]",
             zName, zTarget, zTarget);
      }
      db_multi_exec("UPDATE event SET comment=%Q, type='t'"
                       " WHERE objid=%Q",
                    zComment, zAttachId);
      fossil_free(zComment);
    }
    db_finalize(&qatt);
  }
  if( p->type==CFTYPE_ATTACHMENT ){
    char *zComment = 0;
    const char isAdd = (p->zAttachSrc && p->zAttachSrc[0]) ? 1 : 0;
    /* We assume that we're attaching to a wiki page until we
    ** prove otherwise (which could on a later artifact if we
    ** process the attachment artifact before the artifact to
    ** which it is attached!) */
    char attachToType = 'w';
    if( fossil_is_uuid(p->zAttachTarget) ){
      if( db_exists("SELECT 1 FROM tag WHERE tagname='tkt-%q'",
            p->zAttachTarget)
        ){
        attachToType = 't';          /* Attaching to known ticket */
      }else if( db_exists("SELECT 1 FROM tag WHERE tagname='event-%q'",
                  p->zAttachTarget)
            ){
        attachToType = 'e';          /* Attaching to known tech note */
      }
    }
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
    if( 'w' == attachToType ){
      if( isAdd ){
        zComment = mprintf(
             "Add attachment [/artifact/%!S|%h] to wiki page [%h]",
             p->zAttachSrc, p->zAttachName, p->zAttachTarget);
      }else{
        zComment = mprintf("Delete attachment \"%h\" from wiki page [%h]",
             p->zAttachName, p->zAttachTarget);
      }
    }else if( 'e' == attachToType ){
      if( isAdd ){
        zComment = mprintf(
          "Add attachment [/artifact/%!S|%h] to tech note [/technote/%!S|%S]",
          p->zAttachSrc, p->zAttachName, p->zAttachTarget, p->zAttachTarget);
      }else{
        zComment = mprintf(
             "Delete attachment \"/artifact/%!S|%h\" from"
             " tech note [/technote/%!S|%S]",
             p->zAttachName, p->zAttachName,
             p->zAttachTarget,p->zAttachTarget);
      }
    }else{
      if( isAdd ){
        zComment = mprintf(
             "Add attachment [/artifact/%!S|%h] to ticket [%!S|%S]",
             p->zAttachSrc, p->zAttachName, p->zAttachTarget, p->zAttachTarget);
      }else{
        zComment = mprintf("Delete attachment \"%h\" from ticket [%!S|%S]",
             p->zAttachName, p->zAttachTarget, p->zAttachTarget);
      }
    }
    db_multi_exec(
        "REPLACE INTO event(type,mtime,objid,user,comment)"
        "VALUES('%c',%.17g,%d,%Q,%Q)",
        attachToType, p->rDate, rid, p->zUser, zComment
    );
    fossil_free(zComment);
  }
  if( p->type==CFTYPE_CONTROL ){
    Blob comment;
    int i;
    const char *zName;
    const char *zValue;
    const char *zTagUuid;
    int branchMove = 0;
    blob_zero(&comment);
    if( p->zComment ){
      blob_appendf(&comment, " %s.", p->zComment);
    }
    /* Next loop expects tags to be sorted on hash, so sort it. */
    qsort(p->aTag, p->nTag, sizeof(p->aTag[0]), tag_compare);
    for(i=0; i<p->nTag; i++){
      zTagUuid = p->aTag[i].zUuid;
      if( !zTagUuid ) continue;
      if( i==0 || fossil_strcmp(zTagUuid, p->aTag[i-1].zUuid)!=0 ){
        blob_appendf(&comment,
           " Edit [%!S|%S]:",
           zTagUuid, zTagUuid);
        branchMove = 0;
        if( permitHooks && db_exists("SELECT 1 FROM event, blob"
            " WHERE event.type='ci' AND event.objid=blob.rid"
            " AND blob.uuid=%Q", zTagUuid) ){
          zScript = xfer_commit_code();
          zUuid = zTagUuid;
        }
      }
      zName = p->aTag[i].zName;
      zValue = p->aTag[i].zValue;
      if( strcmp(zName, "*branch")==0 ){
        blob_appendf(&comment,
           " Move to branch [/timeline?r=%h&nd&dp=%!S&unhide | %h].",
           zValue, zTagUuid, zValue);
        branchMove = 1;
        continue;
      }else if( strcmp(zName, "*bgcolor")==0 ){
        blob_appendf(&comment,
           " Change branch background color to \"%h\".", zValue);
        continue;
      }else if( strcmp(zName, "+bgcolor")==0 ){
        blob_appendf(&comment,
           " Change background color to \"%h\".", zValue);
        continue;
      }else if( strcmp(zName, "-bgcolor")==0 ){
        blob_appendf(&comment, " Cancel background color");
      }else if( strcmp(zName, "+comment")==0 ){
        blob_appendf(&comment, " Edit check-in comment.");
        continue;
      }else if( strcmp(zName, "+user")==0 ){
        blob_appendf(&comment, " Change user to \"%h\".", zValue);
        continue;
      }else if( strcmp(zName, "+date")==0 ){
        blob_appendf(&comment, " Timestamp %h.", zValue);
        continue;
      }else if( memcmp(zName, "-sym-",5)==0 ){
        if( !branchMove ){
          blob_appendf(&comment, " Cancel tag \"%h\"", &zName[5]);
        }else{
          continue;
        }
      }else if( memcmp(zName, "*sym-",5)==0 ){
        if( !branchMove ){
          blob_appendf(&comment, " Add propagating tag \"%h\"", &zName[5]);
        }else{
          continue;
        }
      }else if( memcmp(zName, "+sym-",5)==0 ){
        blob_appendf(&comment, " Add tag \"%h\"", &zName[5]);
      }else if( strcmp(zName, "+closed")==0 ){
        blob_append(&comment, " Mark \"Closed\"", -1);
      }else if( strcmp(zName, "-closed")==0 ){
        blob_append(&comment, " Remove the \"Closed\" mark", -1);
      }else {
        if( zName[0]=='-' ){
          blob_appendf(&comment, " Cancel \"%h\"", &zName[1]);
        }else if( zName[0]=='+' ){
          blob_appendf(&comment, " Add \"%h\"", &zName[1]);
        }else{
          blob_appendf(&comment, " Add propagating \"%h\"", &zName[1]);
        }
        if( zValue && zValue[0] ){
          blob_appendf(&comment, " with value \"%h\".", zValue);
        }else{
          blob_appendf(&comment, ".");
        }
        continue;
      }
      if( zValue && zValue[0] ){
        blob_appendf(&comment, " with note \"%h\".", zValue);
      }else{
        blob_appendf(&comment, ".");
      }
    }
    /*blob_appendf(&comment, " &#91;[/info/%S | details]&#93;");*/
    if( blob_size(&comment)==0 ) blob_append(&comment, " ", 1);
    db_multi_exec(
      "REPLACE INTO event(type,mtime,objid,user,comment)"
      "VALUES('g',%.17g,%d,%Q,%Q)",
      p->rDate, rid, p->zUser, blob_str(&comment)+1
    );
    blob_reset(&comment);
  }
  db_end_transaction(0);
  if( permitHooks ){
    rc = xfer_run_common_script();
    if( rc==TH_OK ){
      rc = xfer_run_script(zScript, zUuid, 0);
    }
  }
  if( p->type==CFTYPE_MANIFEST ){
    manifest_cache_insert(p);
  }else{
    manifest_destroy(p);
  }
  assert( blob_is_reset(pContent) );
  return ( rc!=TH_ERROR );
}

/*
** COMMAND: test-crosslink
**
** Usage:  %fossil test-crosslink RECORDID
**
** Run the manifest_crosslink() routine on the artifact with the given
** record ID.  This is typically done in the debugger.
*/
void test_crosslink_cmd(void){
  int rid;
  Blob content;
  db_find_and_open_repository(0, 0);
  if( g.argc!=3 ) usage("RECORDID");
  rid = name_to_rid(g.argv[2]);
  content_get(rid, &content);
  manifest_crosslink(rid, &content, MC_NONE);
}
