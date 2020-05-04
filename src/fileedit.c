/*
** Copyright (c) 2020 D. Richard Hipp
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
*******************************************************************************
**
** This file contains code for the /fileedit page and related code.
*/
#include "config.h"
#include "fileedit.h"
#include <assert.h>
#include <stdarg.h>

/*
** State for the "mini-checkin" infrastructure, which enables the
** ability to commit changes to a single file without a checkout
** db, e.g. for use via an HTTP request.
**
** Use CheckinMiniInfo_init() to cleanly initialize one to a known
** valid/empty default state.
**
** Memory for all non-const (char *) members is owned by the
** CheckinMiniInfo instance and is freed by CheckinMiniInfo_cleanup().
*/
struct CheckinMiniInfo {
  Manifest * pParent;  /* parent checkin. Memory is owned by this
                          object. */
  char *zParentUuid;   /* Full UUID of pParent */
  char *zFilename;     /* Name of single file to commit. Must be
                          relative to the top of the repo. */
  Blob fileContent;    /* Content of file referred to by zFilename. */
  Blob fileHash;       /* Hash of this->fileContent, using the repo's
                          preferred hash method. */
  Blob comment;        /* Check-in comment text */
  char *zCommentMimetype;  /* Mimetype of comment. May be NULL */
  char *zUser;         /* User name */
  char *zDate;         /* Optionally force this date string (anything
                          supported by date_in_standard_format()).
                          Maybe be NULL. */
  Blob *pMfOut;        /* If not NULL, checkin_mini() will write a
                          copy of the generated manifest here. This
                          memory is NOT owned by CheckinMiniInfo. */
  int filePerm;        /* Permissions (via file_perm()) of the input
                          file. We need to store this before calling
                          checkin_mini() because the real input file
                          name may differ from the repo-centric
                          this->zFilename, and checkin_mini() requires
                          the permissions of the original file. For
                          web commits, set this to PERM_REG or (when
                          editing executable scripts) PERM_EXE before
                          calling checkin_mini(). */
  int flags;           /* Bitmask of fossil_cimini_flags. */
};
typedef struct CheckinMiniInfo CheckinMiniInfo;

/*
** CheckinMiniInfo::flags values.
*/
enum fossil_cimini_flags {
CIMINI_NONE = 0,
/*
** Tells checkin_mini() to use dry-run mode.
*/
CIMINI_DRY_RUN = 1,
/*
** Tells checkin_mini() to allow forking from a non-leaf commit.
*/
CIMINI_ALLOW_FORK = 1<<1,
/*
** Tells checkin_mini() to dump its generated manifest to stdout.
*/
CIMINI_DUMP_MANIFEST = 1<<2,

/*
** By default, content containing what appears to be a merge conflict
** marker is not permitted. This flag relaxes that requirement.
*/
CIMINI_ALLOW_MERGE_MARKER = 1<<3,

/*
** By default mini-checkins are not allowed to be "older"
** than their parent. i.e. they may not have a timestamp
** which predates their parent. This flag bypasses that
** check.
*/
CIMINI_ALLOW_OLDER = 1<<4,

/*
** Indicates that the content of the newly-checked-in file is
** converted, if needed, to use the same EOL style as the previous
** version of that file. Only the in-memory/in-repo copies are
** affected, not the original file (if any).
*/
CIMINI_CONVERT_EOL_INHERIT = 1<<5,
/*
** Indicates that the input's EOLs should be converted to Unix-style.
*/
CIMINI_CONVERT_EOL_UNIX = 1<<6,
/*
** Indicates that the input's EOLs should be converted to Windows-style.
*/
CIMINI_CONVERT_EOL_WINDOWS = 1<<7,
/*
** A hint to checkin_mini() to "prefer" creation of a delta manifest.
** It may decide not to for various reasons.
*/
CIMINI_PREFER_DELTA = 1<<8,
/*
** A "stronger hint" to checkin_mini() to prefer creation of a delta
** manifest if it at all can. It will decide not to only if creation
** of a delta is not a realistic option. For this to work, it must be
** set together with the CIMINI_PREFER_DELTA flag, but the two cannot
** be combined in this enum.
**
** This option is ONLY INTENDED FOR TESTING, used in bypassing
** heuristics which may otherwise disable generation of a delta on the
** grounds of efficiency (e.g. not generating a delta if the parent
** non-delta only has a few F-cards).
**
** The forbid-delta-manifests repo config option trumps this.
*/
CIMINI_STRONGLY_PREFER_DELTA = 1<<9,
/*
** Tells checkin_mini() to permit the addition of a new file. Normally
** this is disabled because there are many cases where it could cause
** the inadvertent addition of a new file when an update to an
** existing was intended, as a side-effect of name-case differences.
*/
CIMINI_ALLOW_NEW_FILE = 1<<10
};

/*
** Initializes p to a known-valid default state.
*/
static void CheckinMiniInfo_init( CheckinMiniInfo * p ){
  memset(p, 0, sizeof(CheckinMiniInfo));
  p->flags = CIMINI_NONE;
  p->filePerm = -1;
  p->comment = p->fileContent = p->fileHash = empty_blob;
}

/*
** Frees all memory owned by p, but does not free p.
 */
static void CheckinMiniInfo_cleanup( CheckinMiniInfo * p ){
  blob_reset(&p->comment);
  blob_reset(&p->fileContent);
  blob_reset(&p->fileHash);
  if(p->pParent){
    manifest_destroy(p->pParent);
  }
  fossil_free(p->zFilename);
  fossil_free(p->zDate);
  fossil_free(p->zParentUuid);
  fossil_free(p->zCommentMimetype);
  fossil_free(p->zUser);
  CheckinMiniInfo_init(p);
}

/*
** Internal helper which returns an F-card perms string suitable for
** writing into a manifest.
*/
static const char * mfile_permint_mstring(int perm){
  switch(perm){
    case PERM_EXE: return " x";
    case PERM_LNK: return " l";
    default: return "";
  }
}

/*
** Given a ManifestFile permission string (or NULL), it returns one of
** PERM_REG, PERM_EXE, or PERM_LNK.
*/
static int mfile_permstr_int(const char *zPerm){
  if(!zPerm || !*zPerm) return PERM_REG;
  else if(strstr(zPerm,"x")) return PERM_EXE;
  else if(strstr(zPerm,"l")) return PERM_LNK;
  else return PERM_REG/*???*/;
}

static const char * mfile_perm_mstring(const ManifestFile * p){
  return mfile_permint_mstring(manifest_file_mperm(p));
}

/*
** Internal helper for checkin_mini() and friends. Appends an F-card
** for p to pOut.
*/
static void checkin_mini_append_fcard(Blob *pOut, const ManifestFile *p){
  if(p->zUuid){
    assert(*p->zUuid);
    blob_appendf(pOut, "F %F %s%s", p->zName,
                 p->zUuid, mfile_perm_mstring(p));
    if(p->zPrior){
      assert(*p->zPrior);
      blob_appendf(pOut, " %F\n", p->zPrior);
    }else{
      blob_append(pOut, "\n", 1);
    }
  }else{
    /* File was removed from parent delta. */
    blob_appendf(pOut, "F %F\n", p->zName);
  }
}
/*
** Handles the F-card parts for create_manifest_mini().
**
** If asDelta is true, F-cards will be handled as for a delta
** manifest, and the caller MUST have added a B-card to pOut before
** calling this.
**
** Returns 1 on success, 0 on error, and writes any error message to
** pErr (if it's not NULL). The only non-immediately-fatal/panic error
** is if pCI->filePerm is PERM_LNK or pCI would update a PERM_LNK
** in-repo file.
*/
static int create_manifest_mini_fcards( Blob * pOut,
                                        CheckinMiniInfo * pCI,
                                        int asDelta,
                                        Blob * pErr){
  int wroteThisCard = 0;
  const ManifestFile * pFile;
  int (*fncmp)(char const *, char const *) =  /* filename comparator */
    filenames_are_case_sensitive()
    ? fossil_strcmp
    : fossil_stricmp;
#define mf_err(EXPR) if(pErr) blob_appendf EXPR; return 0
#define write_this_card(NAME) \
  blob_appendf(pOut, "F %F %b%s\n", (NAME), &pCI->fileHash, \
               mfile_permint_mstring(pCI->filePerm)); \
  wroteThisCard = 1

  assert(pCI->filePerm!=PERM_LNK && "This should have been validated before.");
  assert(pCI->filePerm==PERM_REG || pCI->filePerm==PERM_EXE);
  if(PERM_LNK==pCI->filePerm){
    goto err_no_symlink;
  }
  manifest_file_rewind(pCI->pParent);
  if(asDelta!=0 && (pCI->pParent->zBaseline==0
                    || pCI->pParent->nFile==0)){
    /* Parent is a baseline or a delta with no F-cards, so this is
    ** the simplest case: create a delta with a single F-card.
    */
    pFile = manifest_file_find(pCI->pParent, pCI->zFilename);
    if(pFile!=0 && manifest_file_mperm(pFile)==PERM_LNK){
      goto err_no_symlink;
    }
    write_this_card(pFile ? pFile->zName : pCI->zFilename);
    return 1;
  }
  while(1){
    int cmp;
    if(asDelta==0){
      pFile = manifest_file_next(pCI->pParent, 0);
    }else{
      /* Parent is a delta manifest with F-cards. Traversal of delta
      ** manifest file entries is normally done via
      ** manifest_file_next(), which takes into account the
      ** differences between the delta and its parent and returns
      ** F-cards from both. Each successive delta from the same
      ** baseline includes all F-card changes from the previous
      ** deltas, so we instead clone the parent's F-cards except for
      ** the one (if any) which matches the new file.
      */
      pFile = pCI->pParent->iFile < pCI->pParent->nFile
        ? &pCI->pParent->aFile[pCI->pParent->iFile++]
        : 0;
    }
    if(0==pFile) break;
    cmp = fncmp(pFile->zName, pCI->zFilename);
    if(cmp<0){
      checkin_mini_append_fcard(pOut,pFile);
    }else{
      if(cmp==0 || 0==wroteThisCard){
        assert(0==wroteThisCard);
        if(PERM_LNK==manifest_file_mperm(pFile)){
          goto err_no_symlink;
        }
        write_this_card(cmp==0 ? pFile->zName : pCI->zFilename);
      }
      if(cmp>0){
        assert(wroteThisCard!=0);
        checkin_mini_append_fcard(pOut,pFile);
      }
    }
  }
  if(wroteThisCard==0){
    write_this_card(pCI->zFilename);
  }
  return 1;
err_no_symlink:
  mf_err((pErr,"Cannot commit or overwrite symlinks "
          "via mini-checkin."));
  return 0;
#undef write_this_card
#undef mf_err
}

/*
** Creates a manifest file, written to pOut, from the state in the
** fully-populated and semantically valid pCI argument. pCI is not
** *semantically* modified but cannot be const because blob_str() may
** need to NUL-terminate any given blob.
**
** Returns true on success. On error, returns 0 and, if pErr is not
** NULL, writes an error message there.
**
** Intended only to be called via checkin_mini() or routines which
** have already completely vetted pCI.
*/
static int create_manifest_mini( Blob * pOut, CheckinMiniInfo * pCI,
                                 Blob * pErr){
  Blob zCard = empty_blob;     /* Z-card checksum */
  int asDelta = 0;
#define mf_err(EXPR) if(pErr) blob_appendf EXPR; return 0

  assert(blob_str(&pCI->fileHash));
  assert(pCI->pParent);
  assert(pCI->zFilename);
  assert(pCI->zUser);
  assert(pCI->zDate);

  /* Potential TODOs include...
  **
  ** - Maybe add support for tags. Those can be edited via /info page,
  **   and feel like YAGNI/feature creep for this purpose.
  */
  blob_zero(pOut);
  manifest_file_rewind(pCI->pParent) /* force load of baseline */;
  /* Determine whether we want to create a delta manifest... */
  if((CIMINI_PREFER_DELTA & pCI->flags)
     && ((CIMINI_STRONGLY_PREFER_DELTA & pCI->flags)
         || (pCI->pParent->pBaseline
             ? pCI->pParent->pBaseline
             : pCI->pParent)->nFile > 15
         /* 15 is arbitrary: don't create a delta when there is only a
         ** tiny gain for doing so. That heuristic is not *quite*
         ** right, in that when we're deriving from another delta, we
         ** really should compare the F-card count between it and its
         ** baseline, and create a delta if the baseline has (say)
         ** twice or more as many F-cards as the previous delta. */)
     && !db_get_boolean("forbid-delta-manifests",0)
     ){
    asDelta = 1;
    blob_appendf(pOut, "B %s\n",
                 pCI->pParent->zBaseline
                 ? pCI->pParent->zBaseline
                 : pCI->zParentUuid);
  }
  blob_reserve(pOut, 1024 *
               (asDelta ? 2 : pCI->pParent->nFile/11+1
                /* In the fossil core repo, each 12-ish F-cards (on
                ** average) take up roughly 1kb */));
  if(blob_size(&pCI->comment)!=0){
    blob_appendf(pOut, "C %F\n", blob_str(&pCI->comment));
  }else{
    blob_append(pOut, "C (no\\scomment)\n", 16);
  }
  blob_appendf(pOut, "D %s\n", pCI->zDate);
  if(create_manifest_mini_fcards(pOut,pCI,asDelta,pErr)==0){
    return 0;
  }
  if(pCI->zCommentMimetype!=0 && pCI->zCommentMimetype[0]!=0){
    blob_appendf(pOut, "N %F\n", pCI->zCommentMimetype);
  }
  blob_appendf(pOut, "P %s\n", pCI->zParentUuid);
  blob_appendf(pOut, "U %F\n", pCI->zUser);
  md5sum_blob(pOut, &zCard);
  blob_appendf(pOut, "Z %b\n", &zCard);
  blob_reset(&zCard);
  return 1;
#undef mf_err
}

/*
** EXPERIMENTAL! Subject to change or removal at any time.
**
** A so-called "single-file/mini/web checkin" is a slimmed-down form
** of the checkin command which accepts only a single file and is
** intended to accept edits to a file via the web interface or from
** the CLI from outside of a checkout.
**
** Being fully non-interactive is a requirement for this function,
** thus it cannot perform autosync or similar activities.
**
** This routine uses the state from the given fully-populated pCI
** argument to add pCI->fileContent to the database, and create and
** save a manifest for that change. Ownership of pCI and its contents
** are unchanged.
**
** This function may may modify pCI as follows:
**
** - If one of Manifest pCI->pParent or pCI->zParentUuid are NULL,
**   then the other will be assigned based on its counterpart. Both
**   may not be NULL.
**
** - pCI->zDate is normalized to/replaced with a valid date/time
**   string. If its original value cannot be validated then
**   this function fails. If pCI->zDate is NULL, the current time
**   is used.
**
** - If the CIMINI_CONVERT_EOL_INHERIT flag is set,
**   pCI->fileContent appears to be plain text, and its line-ending
**   style differs from its previous version, it is converted to the
**   same EOL style as the previous version. If this is done, the
**   pCI->fileHash is re-computed. Note that only pCI->fileContent,
**   not the original file, is affected by the conversion.
**
** - If pCI->fileHash is empty, this routine populates it with the
**   repository's preferred hash algorithm.
**
** - pCI->comment may be converted to Unix-style newlines.
**
** pCI's ownership is not modified.
**
** This function validates several of the inputs and fails if any
** validation fails.
**
** On error, returns false (0) and, if pErr is not NULL, writes a
** diagnostic message there.
** 
** Returns true on success. If pRid is not NULL, the RID of the
** resulting manifest is written to *pRid.
**
** The checkin process is largely influenced by pCI->flags, and that
** must be populated before calling this. See the fossil_cimini_flags
** enum for the docs for each flag.
*/
static int checkin_mini(CheckinMiniInfo * pCI, int *pRid, Blob * pErr){
  Blob mf = empty_blob;             /* output manifest */
  int rid = 0, frid = 0;            /* various RIDs */
  int isPrivate;                    /* whether this is private content
                                       or not */
  ManifestFile * zFilePrev;         /* file entry from pCI->pParent */
  int prevFRid = 0;                 /* RID of file's prev. version */
#define ci_err(EXPR) if(pErr!=0){blob_appendf EXPR;} goto ci_error

  if(!(pCI->flags & CIMINI_DRY_RUN)){
    /* Until this feature is fully vetted, disallow it in the main
    ** fossil repo unless dry-run mode is being used. */
    char * zProjCode = db_get("project-code",0);
    assert(zProjCode);
    if(0==fossil_stricmp("CE59BB9F186226D80E49D1FA2DB29F935CCA0333",
                         zProjCode)){
      fossil_fatal("Never, ever run this in/on the core fossil repo "
                   "in non-dry-run mode until it's been well-vetted. "
                   "Use a temp/test repo.");
    }
    fossil_free(zProjCode);
  }
  db_begin_transaction();

  if(pCI->pParent==0 && pCI->zParentUuid==0){
    ci_err((pErr, "Cannot determine parent version."));
  }
  else if(pCI->pParent==0){
    pCI->pParent = manifest_get_by_name(pCI->zParentUuid, 0);
    if(pCI->pParent==0){
      ci_err((pErr,"Cannot load manifest for [%S].", pCI->zParentUuid));
    }
  }else if(pCI->zParentUuid==0){
    pCI->zParentUuid = rid_to_uuid(pCI->pParent->rid);
    assert(pCI->zParentUuid);
  }

  assert(pCI->pParent->rid>0);
  if(leaf_is_closed(pCI->pParent->rid)){
    ci_err((pErr,"Cannot commit to a closed leaf."));
    /* Remember that in order to override this we'd also need to
    ** cancel TAG_CLOSED on pCI->pParent. There would seem to be no
    ** reason we can't do that via the generated manifest, but the
    ** commit command does not offer that option, so mini-checkin
    ** probably shouldn't, either.
    */
  }
  if( !db_exists("SELECT 1 FROM user WHERE login=%Q", pCI->zUser) ){
    ci_err((pErr,"No such user: %s", pCI->zUser));
  }
  if(!(CIMINI_ALLOW_FORK & pCI->flags)
     && !is_a_leaf(pCI->pParent->rid)){
    ci_err((pErr,"Parent [%S] is not a leaf and forking is disabled.",
            pCI->zParentUuid));
  }
  if(!(CIMINI_ALLOW_MERGE_MARKER & pCI->flags)
     && contains_merge_marker(&pCI->fileContent)){
    ci_err((pErr,"Content appears to contain a merge conflict marker."));
  }
  if(!file_is_simple_pathname(pCI->zFilename, 1)){
    ci_err((pErr,"Invalid filename for use in a repository: %s",
            pCI->zFilename));
  }
  if(!(CIMINI_ALLOW_OLDER & pCI->flags)
     && !checkin_is_younger(pCI->pParent->rid, pCI->zDate)){
    ci_err((pErr,"Checkin time (%s) may not be older "
            "than its parent (%z).",
            pCI->zDate,
            db_text(0, "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%f',%lf)",
                    pCI->pParent->rDate)
            ));
  }
  {
    /*
    ** Normalize the timestamp. We don't use date_in_standard_format()
    ** because that has side-effects we don't want to trigger here.
    */
    char * zDVal = db_text(
         0, "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%f',%Q)",
         pCI->zDate ? pCI->zDate : "now");
    if(zDVal==0 || zDVal[0]==0){
      fossil_free(zDVal);
      ci_err((pErr,"Invalid timestamp string: %s", pCI->zDate));
    }
    fossil_free(pCI->zDate);
    pCI->zDate = zDVal;
  }
  { /* Confirm that only one EOL policy is in place. */
    int n = 0;
    if(CIMINI_CONVERT_EOL_INHERIT & pCI->flags) ++n;
    if(CIMINI_CONVERT_EOL_UNIX & pCI->flags) ++n;
    if(CIMINI_CONVERT_EOL_WINDOWS & pCI->flags) ++n;
    if(n>1){
      ci_err((pErr,"More than 1 EOL conversion policy was specified."));
    }
  }
  /* Potential TODOs include:
  **
  ** - Commit allows an empty checkin only with a flag, but we
  **   currently disallow it entirely. Conform with commit?
  **
  ** Non-TODOs:
  **
  ** - Check for a commit lock would require auto-sync, which this
  **   code cannot do if it's going to be run via a web page.
  */

  /*
  ** Confirm that pCI->zFilename can be found in pCI->pParent.  If
  ** not, fail unless the CIMINI_ALLOW_NEW_FILE flag is set. This is
  ** admittedly an artificial limitation, not strictly necessary. We
  ** do it to hopefully reduce the chance of an "oops" where file
  ** X/Y/z gets committed as X/Y/Z or X/y/z due to a typo or
  ** case-sensitivity mismatch between the user/repo/filesystem, or
  ** some such.
  */
  manifest_file_rewind(pCI->pParent);
  zFilePrev = manifest_file_find(pCI->pParent, pCI->zFilename);
  if(!(CIMINI_ALLOW_NEW_FILE & pCI->flags)
     && (!zFilePrev
         || !zFilePrev->zUuid/*was removed from parent delta manifest*/)
     ){
    ci_err((pErr,"File [%s] not found in manifest [%S]. "
            "Adding new files is currently not permitted.",
            pCI->zFilename, pCI->zParentUuid));
  }else if(zFilePrev
           && manifest_file_mperm(zFilePrev)==PERM_LNK){
    ci_err((pErr,"Cannot save a symlink via a mini-checkin."));
  }
  if(zFilePrev){
    prevFRid = fast_uuid_to_rid(zFilePrev->zUuid);
  }

  if(((CIMINI_CONVERT_EOL_INHERIT & pCI->flags)
      || (CIMINI_CONVERT_EOL_UNIX & pCI->flags)
      || (CIMINI_CONVERT_EOL_WINDOWS & pCI->flags))
     && blob_size(&pCI->fileContent)>0
     ){
    /* Convert to the requested EOL style. Note that this inherently
    ** runs a risk of breaking content, e.g. string literals which
    ** contain embedded newlines. Note that HTML5 specifies that
    ** form-submitted TEXTAREA content gets normalized to CRLF-style:
    **
    ** https://html.spec.whatwg.org/multipage/form-elements.html#the-textarea-element
    */
    const int pseudoBinary = LOOK_LONG | LOOK_NUL;
    const int lookFlags = LOOK_CRLF | pseudoBinary;
    const int lookNew = looks_like_utf8( &pCI->fileContent, lookFlags );
    if(!(pseudoBinary & lookNew)){
      int rehash = 0;
      if(CIMINI_CONVERT_EOL_INHERIT & pCI->flags){
        Blob contentPrev = empty_blob;
        int lookOrig, nOrig;
        content_get(prevFRid, &contentPrev);
        lookOrig = looks_like_utf8(&contentPrev, lookFlags);
        nOrig = blob_size(&contentPrev);
        blob_reset(&contentPrev);
        if(nOrig>0 && lookOrig!=lookNew){
          /* If there is a newline-style mismatch, adjust the new
          ** content version to the previous style, then re-hash the
          ** content. Note that this means that what we insert is NOT
          ** what's in the filesystem.
          */
          if(!(lookOrig & LOOK_CRLF) && (lookNew & LOOK_CRLF)){
            /* Old has Unix-style, new has Windows-style. */
            blob_to_lf_only(&pCI->fileContent);
            rehash = 1;
          }else if((lookOrig & LOOK_CRLF) && !(lookNew & LOOK_CRLF)){
            /* Old has Windows-style, new has Unix-style. */
            blob_add_cr(&pCI->fileContent);
            rehash = 1;
          }
        }
      }else{
        const int oldSize = blob_size(&pCI->fileContent);
        if(CIMINI_CONVERT_EOL_UNIX & pCI->flags){
          blob_to_lf_only(&pCI->fileContent);
        }else{
          assert(CIMINI_CONVERT_EOL_WINDOWS & pCI->flags);
          blob_add_cr(&pCI->fileContent);
        }
        if(blob_size(&pCI->fileContent)!=oldSize){
          rehash = 1;
        }
      }
      if(rehash!=0){
        hname_hash(&pCI->fileContent, 0, &pCI->fileHash);
      }
    }
  }/* end EOL conversion */

  if(blob_size(&pCI->fileHash)==0){
    /* Hash the content if it's not done already... */
    hname_hash(&pCI->fileContent, 0, &pCI->fileHash);
    assert(blob_size(&pCI->fileHash)>0);
  }
  if(zFilePrev){
    /* Has this file been changed since its previous commit?  Note
    ** that we have to delay this check until after the potentially
    ** expensive EOL conversion. */
    assert(blob_size(&pCI->fileHash));
    if(0==fossil_strcmp(zFilePrev->zUuid, blob_str(&pCI->fileHash))
       && manifest_file_mperm(zFilePrev)==pCI->filePerm){
      ci_err((pErr,"File is unchanged. Not saving."));
    }
  }
#if 1
  /* Do we really want to normalize comment EOLs? Web-posting will
  ** submit them in CRLF format. */
  blob_to_lf_only(&pCI->comment);
#endif
  /* Create, save, deltify, and crosslink the manifest... */
  if(create_manifest_mini(&mf, pCI, pErr)==0){
    return 0;
  }
  isPrivate = content_is_private(pCI->pParent->rid);
  rid = content_put_ex(&mf, 0, 0, 0, isPrivate);
  if(pCI->flags & CIMINI_DUMP_MANIFEST){
    fossil_print("%b", &mf);
  }
  if(pCI->pMfOut!=0){
    /* Cross-linking clears mf, so we have to copy it,
    ** instead of taking over its memory. */
    blob_reset(pCI->pMfOut);
    blob_append(pCI->pMfOut, blob_buffer(&mf), blob_size(&mf));
  }
  content_deltify(rid, &pCI->pParent->rid, 1, 0);
  manifest_crosslink(rid, &mf, 0);
  blob_reset(&mf);
  /* Save and deltify the file content... */
  frid = content_put_ex(&pCI->fileContent, blob_str(&pCI->fileHash),
                        0, 0, isPrivate);
  if(zFilePrev!=0){
    assert(prevFRid>0);
    content_deltify(frid, &prevFRid, 1, 0);
  }
  db_end_transaction((CIMINI_DRY_RUN & pCI->flags) ? 1 : 0);
  if(pRid!=0){
    *pRid = rid;
  }
  return 1;
ci_error:
  assert(db_transaction_nesting_depth()>0);
  db_end_transaction(1);
  return 0;
#undef ci_err
}

/*
** COMMAND: test-ci-mini
**
** This is an on-going experiment, subject to change or removal at
** any time.
**
** Usage: %fossil test-ci-mini ?OPTIONS? FILENAME
**
** where FILENAME is a repo-relative name as it would appear in the
** vfile table.
**
** Options:
**
**   --repository|-R REPO      The repository file to commit to.
**   --as FILENAME             The repository-side name of the input
**                             file, relative to the top of the
**                             repository. Default is the same as the
**                             input file name.
**   --comment|-m COMMENT      Required checkin comment.
**   --comment-file|-M FILE    Reads checkin comment from the given file.
**   --revision|-r VERSION     Commit from this version. Default is
**                             the checkout version (if available) or
**                             trunk (if used without a checkout).
**   --allow-fork              Allows the commit to be made against a
**                             non-leaf parent. Note that no autosync
**                             is performed beforehand.
**   --allow-merge-conflict    Allows checkin of a file even if it
**                             appears to contain a fossil merge conflict
**                             marker.
**   --user-override USER      USER to use instead of the current
**                             default.
**   --date-override DATETIME  DATE to use instead of 'now'.
**   --allow-older             Allow a commit to be older than its
**                             ancestor.
**   --convert-eol             Convert EOL style of the checkin to match
**                             the previous version's content. Does not
**                             modify the input file, only the checked-in
**                             content.
**   --delta                   Prefer to generate a delta manifest, if
**                             able. The forbid-delta-manifests repo
**                             config option trumps this, as do certain
**                             heuristics.
**   --allow-new-file          Allow addition of a new file this way.
**                             Disabled by default to avoid that case-
**                             sensitivity errors inadvertently lead to
**                             adding a new file where an update is
**                             intended.
**   --dump-manifest|-d        Dumps the generated manifest to stdout
**                             immediately after it's generated.
**   --save-manifest FILE      Saves the generated manifest to a file
**                             after successfully processing it.
**   --wet-run                 Disables the default dry-run mode.
**
** Example:
**
** %fossil test-ci-mini -R REPO -m ... -r foo --as src/myfile.c myfile.c
**
*/
void test_ci_mini_cmd(){
  CheckinMiniInfo cimi;       /* checkin state */
  int newRid = 0;                /* RID of new version */
  const char * zFilename;        /* argv[2] */
  const char * zComment;         /* -m comment */
  const char * zCommentFile;     /* -M FILE */
  const char * zAsFilename;      /* --as filename */
  const char * zRevision;        /* --revision|-r [=trunk|checkout] */
  const char * zUser;            /* --user-override */
  const char * zDate;            /* --date-override */
  char const * zManifestFile = 0;/* --save-manifest FILE */

  /* This function should perform only the minimal "business logic" it
  ** needs in order to fully/properly populate the CheckinMiniInfo and
  ** then pass it on to checkin_mini() to do most of the validation
  ** and work. The point of this is to avoid duplicate code when a web
  ** front-end is added for checkin_mini().
  */
  CheckinMiniInfo_init(&cimi);
  zComment = find_option("comment","m",1);
  zCommentFile = find_option("comment-file","M",1);
  zAsFilename = find_option("as",0,1);
  zRevision = find_option("revision","r",1);
  zUser = find_option("user-override",0,1);
  zDate = find_option("date-override",0,1);
  zManifestFile = find_option("save-manifest",0,1);
  if(find_option("wet-run",0,0)==0){
    cimi.flags |= CIMINI_DRY_RUN;
  }
  if(find_option("allow-fork",0,0)!=0){
    cimi.flags |= CIMINI_ALLOW_FORK;
  }
  if(find_option("dump-manifest","d",0)!=0){
    cimi.flags |= CIMINI_DUMP_MANIFEST;
  }
  if(find_option("allow-merge-conflict",0,0)!=0){
    cimi.flags |= CIMINI_ALLOW_MERGE_MARKER;
  }
  if(find_option("allow-older",0,0)!=0){
    cimi.flags |= CIMINI_ALLOW_OLDER;
  }
  if(find_option("convert-eol-prev",0,0)!=0){
    cimi.flags |= CIMINI_CONVERT_EOL_INHERIT;
  }
  if(find_option("delta",0,0)!=0){
    cimi.flags |= CIMINI_PREFER_DELTA;
  }
  if(find_option("delta2",0,0)!=0){
    /* Undocumented. For testing only. */
    cimi.flags |= CIMINI_PREFER_DELTA | CIMINI_STRONGLY_PREFER_DELTA;
  }
  if(find_option("allow-new-file",0,0)!=0){
    cimi.flags |= CIMINI_ALLOW_NEW_FILE;
  }
  db_find_and_open_repository(0, 0);
  verify_all_options();
  user_select();
  if(g.argc!=3){
    usage("INFILE");
  }
  if(zComment && zCommentFile){
    fossil_fatal("Only one of -m or -M, not both, may be used.");
  }else{
    if(zCommentFile && *zCommentFile){
      blob_read_from_file(&cimi.comment, zCommentFile, ExtFILE);
    }else if(zComment && *zComment){
      blob_append(&cimi.comment, zComment, -1);
    }
    if(!blob_size(&cimi.comment)){
      fossil_fatal("Non-empty checkin comment is required.");
    }
  }
  db_begin_transaction();
  zFilename = g.argv[2];
  cimi.zFilename = mprintf("%/", zAsFilename ? zAsFilename : zFilename);
  cimi.filePerm = file_perm(zFilename, ExtFILE);
  cimi.zUser = mprintf("%s", zUser ? zUser : login_name());
  if(zDate){
    cimi.zDate = mprintf("%s", zDate);
  }
  if(zRevision==0 || zRevision[0]==0){
    if(g.localOpen/*checkout*/){
      zRevision = db_lget("checkout-hash", 0)/*leak*/;
    }else{
      zRevision = "trunk";
    }
  }
  name_to_uuid2(zRevision, "ci", &cimi.zParentUuid);
  if(cimi.zParentUuid==0){
    fossil_fatal("Cannot determine version to commit to.");
  }
  blob_read_from_file(&cimi.fileContent, zFilename, ExtFILE);
  {
    Blob theManifest = empty_blob; /* --save-manifest target */
    Blob errMsg = empty_blob;
    int rc;
    if(zManifestFile){
      cimi.pMfOut = &theManifest;
    }
    rc = checkin_mini(&cimi, &newRid, &errMsg);
    if(rc){
      assert(blob_size(&errMsg)==0);
    }else{
      assert(blob_size(&errMsg));
      fossil_fatal("%b", &errMsg);
    }
    if(zManifestFile){
      fossil_print("Writing manifest to: %s\n", zManifestFile);
      assert(blob_size(&theManifest)>0);
      blob_write_to_file(&theManifest, zManifestFile);
      blob_reset(&theManifest);
    }
  }
  if(newRid!=0){
    fossil_print("New version%s: %z\n",
                 (cimi.flags & CIMINI_DRY_RUN) ? " (dry run)" : "",
                 rid_to_uuid(newRid));
  }
  db_end_transaction(0/*checkin_mini() will have triggered it to roll
                      ** back in dry-run mode, but we need access to
                      ** the transaction-written db state in this
                      ** routine.*/);
  if(!(cimi.flags & CIMINI_DRY_RUN) && newRid!=0 && g.localOpen!=0){
    fossil_warning("The checkout state is now out of sync "
                   "with regards to this commit. It needs to be "
                   "'update'd or 'close'd and re-'open'ed.");
  }
  CheckinMiniInfo_cleanup(&cimi);
}


/*
** Returns true if the given filename qualifies for online editing by
** the current user, else returns false.
**
** Editing requires that the user have the Write permission and that
** the filename match the glob defined by the fileedit-glob setting.
** A missing or empty value for that glob disables all editing.
*/
int fileedit_is_editable(const char *zFilename){
  static Glob * pGlobs = 0;
  static int once = 0;
  if(0==g.perm.Write || zFilename==0 || *zFilename==0
     || (once!=0 && pGlobs==0)){
    return 0;
  }else if(0==pGlobs){
    char * zGlobs = db_get("fileedit-glob",0);
    once = 1;
    if(0==zGlobs) return 0;
    pGlobs = glob_create(zGlobs);
    fossil_free(zGlobs);
  }
  return glob_match(pGlobs, zFilename);
}

static void fileedit_emit_script(int phase){
  if(0==phase){
    CX("<script nonce='%s'>", style_nonce());
  }else{
    CX("</script>\n");
  }
}

/*
** Emits a script tag which defines window.fossilFetch(), which works
** similarly (not identically) to the not-quite-ubiquitous global
** fetch().
**
** JS usages:
**
** fossilFetch( URI, onLoadCallback );
**
** fossilFetch( URI, optionsObject );
**
** Where the optionsObject may be an object with any of these
** properties:
**
** - onload: callback(responseData) (default = output response to
**   console).
**
** - onerror: callback(XHR onload event) (default = no-op)
**
** - method: 'POST' | 'GET' (default = 'GET')
**
** Noting that URI must be relative to the top of the repository and
** must not start with a slash. It gets %R/ prepended to it.
**
** TODOs, if needed, include:
**
** optionsObject.params: object map of key/value pairs to append to the
** URI.
**
** optionsObject.payload: string or JSON-able object to POST as the
** payload.
**
*/
static void fileedit_emit_script_fetch(){
  fileedit_emit_script(0);
  CX("window.fossilFetch = function(path,opt){\n");
  CX("  if('function'===typeof opt){\n");
  CX("    opt={onload:opt};\n");
  CX("  }else{\n");
  CX("    opt=opt||{onload:function(r){console.debug('response:',r)}}\n");
  CX("  }\n");
  CX("  const url='%R/'+path, x=new XMLHttpRequest();\n");
  CX("  x.open(opt.method||'GET', url, true);\n");
  CX("  x.responseType=opt.responseType||'text';\n");
  CX("  if(opt.onload){\n");
  CX("    x.onload = function(e){\n");
  CX("      if(200!==this.status){\n");
  CX("        if(opt.onerror) opt.onerror(e);\n");
  CX("        return;\n");
  CX("      }\n");
  CX("      opt.onload(this.response);\n");
  CX("    }\n");
  CX("  }\n");
  CX("  x.send();");
  CX("};\n");
  fileedit_emit_script(1);
};

/*
** Outputs a labeled checkbox element:
**
** <span class='input-with-label' title={{zTip}}>
**   <input type='checkbox' name={{zFieldName}} value={{zValue}}
**          {{isChecked ? " checked : ""}}/>
**   <span>{{zLabel}}</span>
** </span>
**
** zFieldName, zLabel, and zValue are required. zTip is optional.
*/
static void style_labeled_checkbox(const char *zFieldName,
                                   const char * zLabel,
                                   const char * zValue,
                                   const char * zTip,
                                   int isChecked){
  CX("<div class='input-with-label'");
  if(zTip && *zTip){
    CX(" title='%h'", zTip);
  }
  CX("><input type='checkbox' name='%s' value='%T'%s/>",
     zFieldName,
     zValue ? zValue : "", isChecked ? " checked" : "");
  CX("<span>%h</span></div>", zLabel);
}

enum fileedit_render_preview_flags {
FE_PREVIEW_LINE_NUMBERS = 1
};
enum fileedit_render_modes {
FE_RENDER_GUESS = 0,
FE_RENDER_PLAIN_TEXT,
FE_RENDER_HTML,
FE_RENDER_WIKI
};

static int fileedit_render_mode_for_mimetype(const char * zMimetype){
  int rc = FE_RENDER_PLAIN_TEXT;
  if( zMimetype ){
    if( fossil_strcmp(zMimetype, "text/html")==0 ){
      rc = FE_RENDER_HTML;
    }else if( fossil_strcmp(zMimetype, "text/x-fossil-wiki")==0
              || fossil_strcmp(zMimetype, "text/x-markdown")==0 ){
      rc = FE_RENDER_WIKI;
    }
  }
  return rc;
}

/*
** Performs the PREVIEW mode for /filepage.
*/
static void fileedit_render_preview(Blob * pContent,
                                    const char *zFilename,
                                    int flags, int renderMode,
                                    int nIframeHeightEm){
  const char * zMime;
  zMime = mimetype_from_name(zFilename);
  if(FE_RENDER_GUESS==renderMode){
    renderMode = fileedit_render_mode_for_mimetype(zMime);
  }
  CX("<div class='fileedit-preview'>");
  CX("<div>Preview</div>");
  switch(renderMode){
    case FE_RENDER_HTML:{
      char * z64 = encode64(blob_str(pContent), blob_size(pContent));
      CX("<iframe width='100%%' frameborder='0' marginwidth='0' "
         "style='height:%dem' "
         "marginheight='0' sandbox='allow-same-origin' id='ifm1' "
         "src='data:text/html;base64,%z'"
         "></iframe>", nIframeHeightEm ? nIframeHeightEm : 40,
         z64);
      break;
    }
    case FE_RENDER_WIKI:
      wiki_render_by_mimetype(pContent, zMime);
      break;
    default:{
      const char *zExt = strrchr(zFilename,'.');
      const char *zContent = blob_str(pContent);
      if(FE_PREVIEW_LINE_NUMBERS & flags){
        output_text_with_line_numbers(zContent, "on");
      }else if(zExt && zExt[1]){
        CX("<pre><code class='language-%s'>%h</code></pre>",
           zExt+1, zContent);
      }else{
        CX("<pre>%h</pre>", zExt+1, zContent);
      }
      break;
    }
  }
  CX("</div><!--.fileedit-preview-->\n");
}

/*
** Renders diffs for the /fileedit page. pContent is the
** locally-edited content.  frid is the RID of the file's blob entry
** from which pContent is based.  zManifestUuid is the checkin version
** to which RID belongs - it is purely informational, for labeling the
** diff view. isSbs is true for side-by-side diffs, false for unified.
*/
static void fileedit_render_diff(Blob * pContent, int frid,
                                 const char * zManifestUuid,
                                 int isSbs){
  Blob orig = empty_blob;
  Blob out = empty_blob;
  u64 diffFlags = DIFF_HTML | DIFF_NOTTOOBIG | DIFF_STRIP_EOLCR;

  content_get(frid, &orig);
  if(isSbs){
    diffFlags |=  DIFF_SIDEBYSIDE;
  }else{
    diffFlags |= DIFF_LINENO;
  }
  text_diff(&orig, pContent, &out, 0, diffFlags);
  CX("<div class='fileedit-diff'>");
  CX("<div>Diff <code>[%S]</code> &rarr; Local Edits</div>",
     zManifestUuid);
  if(isSbs){
    CX("%b",&out);
  }else{
    CX("<pre class='udiff'>%b</pre>",&out);
  }
  CX("</div><!--.fileedit-diff-->\n");
  blob_reset(&orig);
  blob_reset(&out);
  /* Wow, that was *easy*. */
}

/*
** Outputs a SELECT list from a compile-time list of integers.
** The vargs must be a list of (const char *, int) pairs, terminated
** with a single NULL. Each pair is interpreted as...
**
** If the (const char *) is NULL, it is the end of the list, else
** a new OPTION entry is created. If the string is empty, the
** label and value of the OPTION is the integer part of the pair.
** If the string is not empty, it becomes the label and the integer
** the value. If that value == selectedValue then that OPTION
** element gets the 'selected' attribute.
**
** Note that the pairs are not in (int, const char *) order because
** there is no well-known integer value which we can definitively use
** as a list terminator.
**
** zFieldName is the value of the form element's name attribute.
**
** zLabel is an optional string to use as a "label" for the element
** (see below).
**
** zTooltip is an optional value for the SELECT's title attribute.
**
** The structure of the emited HTML is:
**
** <div class='input-with-label'>
**   <span>{{zLabel}}</span>
**   <select>...</select>
** </div>
** 
*/
static void style_select_list_int_v(const char *zFieldName,
                                    const char * zLabel,
                                    const char * zToolTip,
                                    int selectedVal, va_list vargs){
  CX("<div class='input-with-label'");
  if(zToolTip && *zToolTip){
    CX(" title='%h'",zToolTip);
  }
  CX(">");
  if(zLabel && *zLabel){
    CX("<span>%h</span>", zLabel);
  }
  CX("<select name='%s'>",zFieldName);
  while(1){
    const char * zOption = va_arg(vargs,char *);
    int v;
    if(NULL==zOption){
      break;
    }
    v = va_arg(vargs,int);
    CX("<option value='%d'%s>",
         v, v==selectedVal ? " selected" : "");
    if(*zOption){
      CX("%s", zOption);
    }else{
      CX("%d",v);
    }
    CX("</option>\n");
  }
  CX("</select>\n");
  if(zLabel && *zLabel){
    CX("</div>\n");
  }
}

/*
** The ellipsis-args counterpart of style_select_list_int_v().
*/
void style_select_list_int(const char *zFieldName,
                           const char * zLabel,
                           const char * zToolTip,
                           int selectedVal, ... ){
  va_list vargs;
  va_start(vargs,selectedVal);
  style_select_list_int_v(zFieldName, zLabel, zToolTip,
                          selectedVal, vargs);
  va_end(vargs);
}

/*
** WEBPAGE: fileedit
**
** EXPERIMENTAL and subject to change and removal at any time. The goal
** is to allow online edits of files.
**
** Query parameters:
**
**    file=FILENAME    Repo-relative path to the file.
**    r=VERSION        Checkin version, using any unambiguous
**                     supported symbolic version name.
**
** All other parameters are for internal use only, submitted via the
** form-submission process, and may change with any given revision of
** this code.
*/
void fileedit_page(){
  enum submit_modes {
  SUBMIT_NONE = 0, SUBMIT_SAVE, SUBMIT_PREVIEW,
  SUBMIT_DIFF_SBS, SUBMIT_DIFF_UNIFIED,
  SUBMIT_end /* sentinel for range validation */
  };
  const char * zFilename = PD("file",P("name"));
                                        /* filename. We'll accept 'name'
                                           because that param is handled
                                           specially by the core. */
  const char * zRev = P("r");           /* checkin version */
  const char * zContent = P("content"); /* file content */
  const char * zComment = P("comment"); /* checkin comment */
  const char * zFileMime = 0;           /* File mime type guess */
  CheckinMiniInfo cimi;                 /* Checkin state */
  int submitMode = SUBMIT_NONE;         /* See mapping below */
  int vid, newVid = 0;                  /* checkin rid */
  int frid = 0;                         /* File content rid */
  int previewLn = P("preview_ln")!=0;   /* Line number mode */
  int previewHtmlHeight = 0;            /* iframe height (EMs) */
  int previewRenderMode = FE_RENDER_GUESS; /* preview mode */
  char * zFileUuid = 0;                 /* File content UUID */
  Blob err = empty_blob;                /* Error report */
  const char * zFlagCheck = 0;          /* Temp url flag holder */
  Blob endScript = empty_blob;          /* Script code to run at the
                                           end. This content will be
                                           combined into a single JS
                                           function call, thus each
                                           entry must end with a
                                           semicolon. */
  Stmt stmt = empty_Stmt;
  const int loadMode = 0;               /* See next comment block */
  /* loadMode: How to populate the TEXTAREA:
  **
  ** 0: HTML encode: despite my personal reservations regarding HTML
  ** escaping, this seems to be the only reliable approach
  ** until/unless we completely AJAXify this page.
  **
  ** 1: JSON mode: JSON-izes the file content and injects it, via JS,
  ** into the editor TEXTAREA. This works wonderfully until the input
  ** file contains an raw <SCRIPT> tag, at which points the HTML
  ** parser chokes on it.
  **
  ** 2: AJAX mode: can only load content from the db, not preview/dry-run
  ** content. Unless this page is refactored to work solely over AJAX
  ** (which is a potential TODO), this method is only useful on the
  ** initial hit to this page, where the file is loaded.
  **
  ** loadMode is not generally configurable: change it only for
  ** testing/development purposes.
  */
#define fail(EXPR) blob_appendf EXPR; goto end_footer

  assert(loadMode==0 || loadMode==1 || loadMode==2);
  login_check_credentials();
  if( !g.perm.Write ){
    login_needed(g.anon.Write);
    return;
  }
  db_begin_transaction();
  CheckinMiniInfo_init(&cimi);
  submitMode = atoi(PD("submit","0"));
  if(submitMode < SUBMIT_NONE || submitMode >= SUBMIT_end){
    submitMode = 0;
  }
  zFlagCheck = P("comment_mimetype");
  if(zFlagCheck){
    cimi.zCommentMimetype = mprintf("%s",zFlagCheck);
    zFlagCheck = 0;
  }
  cimi.zUser = mprintf("%s",g.zLogin);

  style_header("File Editor");
  /* As of this point, don't use return or fossil_fatal(), use
  ** fail((&err,...))  instead so that we can be sure to do any
  ** cleanup and end the transaction cleanly.
  */
  if(!zRev || !*zRev || !zFilename || !*zFilename){
    fail((&err,"Missing required URL parameters: "
          "file=FILE and r=CHECKIN"));
  }
  if(0==fileedit_is_editable(zFilename)){
    fail((&err,"Filename <code>%h</code> is disallowed "
          "by the <code>fileedit-glob</code> repository "
          "setting.",
          zFilename));
  }
  vid = symbolic_name_to_rid(zRev, "ci");
  if(0==vid){
    fail((&err,"Could not resolve checkin version."));
  }
  cimi.zFilename = mprintf("%s",zFilename);
  zFileMime = mimetype_from_name(zFilename);

  /* Find the repo-side file entry or fail... */
  cimi.zParentUuid = rid_to_uuid(vid);
  db_prepare(&stmt, "SELECT uuid, perm FROM files_of_checkin "
             "WHERE filename=%Q %s AND checkinID=%d",
             zFilename, filename_collation(), vid);
  if(SQLITE_ROW==db_step(&stmt)){
    const char * zPerm = db_column_text(&stmt, 1);
    cimi.filePerm = mfile_permstr_int(zPerm);
    if(PERM_LNK==cimi.filePerm){
      fail((&err,"Editing symlinks is not permitted."));
    }
    zFileUuid = mprintf("%s",db_column_text(&stmt, 0));
  }
  db_finalize(&stmt);
  if(!zFileUuid){
    fail((&err,"Checkin [%S] does not contain file: "
          "<code>%h</code>",
          cimi.zParentUuid, zFilename));
  }
  frid = fast_uuid_to_rid(zFileUuid);
  assert(frid);

  /* Read file content from submit request or repo... */
  if(zContent==0){
    content_get(frid, &cimi.fileContent);
    zContent = blob_size(&cimi.fileContent)
      ? blob_str(&cimi.fileContent) : NULL;
  }else{
    blob_init(&cimi.fileContent,zContent,-1);
  }
  if(looks_like_binary(&cimi.fileContent)){
    fail((&err,"File appears to be binary. Cannot edit: "
          "<code>%h</code>",zFilename));
  }

  /* All set. Here we go... */

  CX("<h1>Editing:</h1>");
  CX("<p class='fileedit-hint'>");
  CX("File: "
     "[<a id='finfo-link' href='%R/finfo?name=%T&m=%!S'>info</a>] "
     "<code>%h</code><br>",
     zFilename, zFileUuid, zFilename);
  CX("Checkin Version: "
     "[<a id='r-link' href='%R/info/%!S'>info</a>] "
     "<code id='r-label'>%s</code><br>",
     cimi.zParentUuid, cimi.zParentUuid);
  CX("Permalink: <code>"
     "<a id='permalink' href='%R/fileedit?file=%T&r=%!S'>"
     "/fileedit?file=%T&r=%!S</a></code><br>"
     "(Clicking the permalink will reload the page and discard "
     "all edits!)",
     zFilename, cimi.zParentUuid,
     zFilename, cimi.zParentUuid);
  CX("</p>");
  CX("<p>This page is <em>far from complete</em> and may still have "
     "significant bugs. USE AT YOUR OWN RISK, preferably on a test "
     "repo.</p>\n");
  
  CX("<form action='%R/fileedit#options' method='POST' "
     "class='fileedit'>\n");

  /******* Hidden fields *******/
  CX("<input type='hidden' name='r' value='%s'>",
     cimi.zParentUuid);
  CX("<input type='hidden' name='file' value='%T'>",
     zFilename);

  /******* Content *******/
  CX("<h3>File Content</h3>\n");
  CX("<textarea name='content' id='fileedit-content' "
     "rows='20' cols='80'>");
  if(0==loadMode){
    CX("%h",blob_str(&cimi.fileContent));
  }else{
    CX("Loading...");
    /* Performed via JS later on */
  }
  CX("</textarea>\n");
  /******* Flags/options *******/
  CX("<fieldset class='fileedit-options' id='options'>"
     "<legend>Options</legend><div>"
     /* Chrome does not sanely lay out multiple
     ** fieldset children after the <legend>, so
     ** a containing div is necessary. */);
  /*
  ** TODO?: date-override date selection field. Maybe use
  ** an input[type=datetime-local].
  */
  if(SUBMIT_NONE==submitMode || P("dry_run")!=0){
    cimi.flags |= CIMINI_DRY_RUN;
  }
  style_labeled_checkbox("dry_run", "Dry-run?", "1",
                         "In dry-run mode, the Save button performs "
                         "all work needed for saving but then rolls "
                         "back the transaction, and thus does not "
                         "really save.",
                         cimi.flags & CIMINI_DRY_RUN);
  if(P("allow_fork")!=0){
    cimi.flags |= CIMINI_ALLOW_FORK;
  }
  style_labeled_checkbox("allow_fork", "Allow fork?", "1",
                         "Allow saving to create a fork?",
                         cimi.flags & CIMINI_ALLOW_FORK);
  if(P("allow_older")!=0){
    cimi.flags |= CIMINI_ALLOW_OLDER;
  }
  style_labeled_checkbox("allow_older", "Allow older?", "1",
                         "Allow saving against a parent version "
                         "which has a newer timestamp?",
                         cimi.flags & CIMINI_ALLOW_OLDER);
  if(P("exec_bit")!=0){
    cimi.filePerm = PERM_EXE;
  }
  style_labeled_checkbox("exec_bit", "Executable?", "1",
                         "Set the executable bit?",
                         PERM_EXE==cimi.filePerm);
  if(P("allow_merge_conflict")!=0){
    cimi.flags |= CIMINI_ALLOW_MERGE_MARKER;
  }
  style_labeled_checkbox("allow_merge_conflict",
                         "Allow merge conflict markers?", "1",
                         "Allow saving even if the content contains "
                         "what appear to be fossil merge conflict "
                         "markers?",
                         cimi.flags & CIMINI_ALLOW_MERGE_MARKER);
  if(P("prefer_delta")!=0){
    cimi.flags |= CIMINI_PREFER_DELTA;
  }
  style_labeled_checkbox("prefer_delta",
                         "Prefer delta manifest?", "1",
                         "Will create a delta manifest, instead of "
                         "baseline, if conditions are favorable to do "
                         "so. This option is only a suggestion.",
                         cimi.flags & CIMINI_PREFER_DELTA);
  {/* EOL conversion policy... */
    const int eolMode = submitMode==SUBMIT_NONE
      ? 0 : atoi(PD("eol","0"));
    switch(eolMode){
      case 1: cimi.flags |= CIMINI_CONVERT_EOL_UNIX; break;
      case 2: cimi.flags |= CIMINI_CONVERT_EOL_WINDOWS; break;
      default: cimi.flags |= CIMINI_CONVERT_EOL_INHERIT; break;
    }
    style_select_list_int("eol", "EOL Style",
                          "EOL conversion policy, noting that "
                          "form-processing may implicitly change the "
                          "line endings of the input.",
                          eolMode==1||eolMode==2 ? eolMode : 0,
                          "Inherit", 0,
                          "Unix", 1,
                          "Windows", 2,
                          NULL);
  }

  CX("</div></fieldset>") /* end of checkboxes */;

  /******* Comment *******/
  CX("<a id='comment'></a>");
  CX("<fieldset><legend>Commit message</legend><div>");
  CX("<textarea name='comment' rows='3' cols='80'>");
  /* ^^^ adding the 'required' attribute means we cannot even submit
  ** for PREVIEW mode if it's empty :/. */
  if(zComment && *zComment){
    CX("%h"/*%h? %s?*/, zComment);
  }
  CX("</textarea>\n");
  CX("<div class='fileedit-hint'>Comments use the Fossil wiki markup "
     "syntax.</div>\n"/*TODO: select for fossil/md/plain text*/);
  CX("</div></fieldset>\n");


  
  /******* Buttons *******/
  CX("<a id='buttons'></a>");
  CX("<fieldset class='fileedit-options'>"
     "<legend>Tell the server to...</legend><div>");
  CX("<button type='submit' name='submit' value='1'>"
     "Save</button>");
  CX("<button type='submit' name='submit' value='2'>"
     "Preview</button>");
  {
    /* Preview rendering mode selection... */
    previewRenderMode = atoi(PD("preview_render_mode","0"));
    if(0==previewRenderMode){
      previewRenderMode = fileedit_render_mode_for_mimetype(zFileMime);
    }
    style_select_list_int("preview_render_mode",
                          "Preview Mode",
                          "Preview mode format.",
                          previewRenderMode,
                          "Guess", FE_RENDER_GUESS,
                          "Wiki/Markdown", FE_RENDER_WIKI,
                          "HTML (iframe)", FE_RENDER_HTML,
                          "Plain Text", FE_RENDER_PLAIN_TEXT,
                          NULL);
    if(FE_RENDER_HTML==previewRenderMode){
      /* HTML preview mode iframe height... */
      if(submitMode==SUBMIT_PREVIEW){
        previewHtmlHeight = atoi(PD("preview_html_ems","0"));
      }else{
        previewHtmlHeight = 40;
      }
      /* Allow selection of HTML preview iframe height */
      style_select_list_int("preview_html_ems",
                            "Preview IFrame Height (EMs)",
                            "Height (in EMs) of the iframe used for "
                            "HTML preview",
                            previewHtmlHeight,
                            "", 20, "", 40,
                            "", 60, "", 80,
                            "", 100, NULL);
    }
    else if(FE_RENDER_PLAIN_TEXT==previewRenderMode){
      style_labeled_checkbox("preview_ln",
                             "Add line numbers to plain-text previews?",
                             "1",
                             "If on, plain-text files (only) will get "
                             "line numbers added to the preview.",
                             previewLn);
    }
  }
  CX("<button type='submit' name='submit' value='3'>"
     "Diff (SBS)</button>");
  CX("<button type='submit' name='submit' value='4'>"
     "Diff (Unified)</button>");
  CX("</div></fieldset>");

  /******* End of form *******/    
  CX("</form>\n");

  /* Dynamically populate the editor... */
  if(1==loadMode || (2==loadMode && submitMode>SUBMIT_NONE)){
    char const * zQuoted = 0;
    if(blob_size(&cimi.fileContent)>0){
      db_prepare(&stmt, "SELECT json_quote(%B)", &cimi.fileContent);
      db_step(&stmt);
      zQuoted = db_column_text(&stmt,0);
    }
    blob_appendf(&endScript,
                 "/* populate editor form */\n"
                 "document.getElementById('fileedit-content')"
                 ".value=%s;", zQuoted ? zQuoted : "'';\n");
    if(stmt.pStmt){
      db_finalize(&stmt);
    }
  }else if(2==loadMode){
    assert(submitMode==SUBMIT_NONE);
    fileedit_emit_script_fetch();
    blob_appendf(&endScript,
                 "window.fossilFetch('raw/%s',{"
                 "onload: (r)=>document.getElementById('fileedit-content')"
                 ".value=r,"
                 "onerror:()=>document.getElementById('fileedit-content')"
                 ".value="
                 "'Error loading content'"
                 "});\n", zFileUuid);
  }

  if(SUBMIT_SAVE==submitMode){
    Blob manifest = empty_blob;
    char * zNewUuid = 0;
    /*cimi.flags |= CIMINI_STRONGLY_PREFER_DELTA;*/
    if(zComment && *zComment){
      blob_append(&cimi.comment, zComment, -1);
    }else{
      fail((&err,"Empty comment is not permitted."));
    }
    /*cimi.pParent = manifest_get(vid, CFTYPE_MANIFEST, 0);
      assert(cimi.pParent && "We know vid is valid.");*/
    cimi.pMfOut = &manifest;
    checkin_mini(&cimi, &newVid, &err);
    if(newVid!=0){
      zNewUuid = rid_to_uuid(newVid);
      CX("<h3>Manifest%s: %S</h3><pre>"
         "<code class='fileedit-manifest'>%h</code>"
         "</pre>",
         (cimi.flags & CIMINI_DRY_RUN) ? " (dry run)" : "",
         zNewUuid, blob_str(&manifest));
      if(!(CIMINI_DRY_RUN & cimi.flags)){
        /* We need to update certain form fields and UI elements so
        ** they're not left pointing to the previous version. While
        ** we're at it, we'll re-enable dry-run mode for sanity's
        ** sake.
        */
        blob_appendf(&endScript,
                     "/* Toggle dry-run back on */\n"
                     "document.querySelector('input[type=checkbox]"
                     "[name=dry_run]').checked=true;\n");
        blob_appendf(&endScript,
                     "/* Update version number */\n"
                     "document.querySelector('input[name=r]')"
                     ".value=%Q;\n"
                     "document.querySelector('#r-label')"
                     ".innerText=%Q;\n"
                     "document.querySelector('#r-link')"
                     ".setAttribute('href', '%R/info/%!S');\n"
                     "document.querySelector('#finfo-link')"
                     ".setAttribute('href','%R/finfo?name=%T&m=%!S');\n",
                     /*input[name=r]:*/zNewUuid, /*#r-label:*/ zNewUuid,
                     /*#r-link:*/ zNewUuid,
                     /*#finfo-link:*/zFilename, zNewUuid);
        blob_appendf(&endScript,
                     "/* Updated finfo link */"
                     );
        blob_appendf(&endScript,
                     "/* Update permalink */\n"
                     "const urlFull='%R/fileedit?file=%T&r=%!S';\n"
                     "const urlShort='/fileedit?file=%T&r=%!S';\n"
                     "let link=document.querySelector('#permalink');\n"
                     "link.innerText=urlShort;\n"
                     "link.setAttribute('href',urlFull);\n",
                     cimi.zFilename, zNewUuid,
                     cimi.zFilename, zNewUuid);
      }
      fossil_free(zNewUuid);
      zNewUuid = 0;
    }
    /* On error, the error message is in the err blob and will
    ** be emitted below. */
    cimi.pMfOut = 0;
    blob_reset(&manifest);
  }else if(SUBMIT_PREVIEW==submitMode){
    int pflags = 0;
    if(previewLn) pflags |= FE_PREVIEW_LINE_NUMBERS;
    fileedit_render_preview(&cimi.fileContent, cimi.zFilename, pflags,
                            previewRenderMode, previewHtmlHeight);
  }else if(SUBMIT_DIFF_SBS==submitMode
           || SUBMIT_DIFF_UNIFIED==submitMode){
    fileedit_render_diff(&cimi.fileContent, frid, cimi.zParentUuid,
                         SUBMIT_DIFF_SBS==submitMode);
  }else{
    /* Ignore invalid submitMode value */
    goto end_footer;
  }

end_footer:
  zContent = 0;
  fossil_free(zFileUuid);
  if(stmt.pStmt){
    db_finalize(&stmt);
  }
  if(blob_size(&err)){
      CX("<div class='fileedit-error-report'>%s</div>",
         blob_str(&err));
  }
  blob_reset(&err);
  CheckinMiniInfo_cleanup(&cimi);
  if(blob_size(&endScript)>0){
    fileedit_emit_script(0);
    CX("(function(){\n");
    CX("try{\n%b\n}catch(e){console.error('Exception:',e)}\n",
       &endScript);
    CX("})();");
    fileedit_emit_script(1);
  }
  db_end_transaction(0/*noting that dry-run mode will have already
                      ** set this to rollback mode. */);
  style_footer();
}
