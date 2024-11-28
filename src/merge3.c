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
** This module implements a 3-way merge
*/
#include "config.h"
#include "merge3.h"

#if 0
#define DEBUG(X)  X
#define ISDEBUG 1
#else
#define DEBUG(X)
#define ISDEBUG 0
#endif

/* The minimum of two integers */
#ifndef min
#  define min(A,B)  (A<B?A:B)
#endif

/*
** Compare N lines of text from pV1 and pV2.  If the lines
** are the same, return true.  Return false if one or more of the N
** lines are different.
**
** The cursors on both pV1 and pV2 is unchanged by this comparison.
*/
static int sameLines(Blob *pV1, Blob *pV2, int N){
  char *z1, *z2;
  int i;
  char c;

  if( N==0 ) return 1;
  z1 = &blob_buffer(pV1)[blob_tell(pV1)];
  z2 = &blob_buffer(pV2)[blob_tell(pV2)];
  for(i=0; (c=z1[i])==z2[i]; i++){
    if( c=='\n' || c==0 ){
      N--;
      if( N==0 || c==0 ) return 1;
    }
  }
  return 0;
}

/*
** Look at the next edit triple in both aC1 and aC2.  (An "edit triple" is
** three integers describing the number of copies, deletes, and inserts in
** moving from the original to the edited copy of the file.) If the three
** integers of the edit triples describe an identical edit, then return 1.
** If the edits are different, return 0.
*/
static int sameEdit(
  int *aC1,      /* Array of edit integers for file 1 */
  int *aC2,      /* Array of edit integers for file 2 */
  Blob *pV1,     /* Text of file 1 */
  Blob *pV2      /* Text of file 2 */
){
  if( aC1[0]!=aC2[0] ) return 0;
  if( aC1[1]!=aC2[1] ) return 0;
  if( aC1[2]!=aC2[2] ) return 0;
  if( sameLines(pV1, pV2, aC1[2]) ) return 1;
  return 0;
}

/*
** The aC[] array contains triples of integers.  Within each triple, the
** elements are:
**
**   (0)  The number of lines to copy
**   (1)  The number of lines to delete
**   (2)  The number of liens to insert
**
** Suppose we want to advance over sz lines of the original file.  This routine
** returns true if that advance would land us on a copy operation.  It
** returns false if the advance would end on a delete.
*/
static int ends_at_CPY(int *aC, int sz){
  while( sz>0 && (aC[0]>0 || aC[1]>0 || aC[2]>0) ){
    if( aC[0]>=sz ) return 1;
    sz -= aC[0];
    if( aC[1]>sz ) return 0;
    sz -= aC[1];
    aC += 3;
  }
  return 1;
}

/*
** Text of boundary markers for merge conflicts.
*/
static const char *const mergeMarker[] = {
 /*123456789 123456789 123456789 123456789 123456789 123456789 123456789*/
  "<<<<<<< BEGIN MERGE CONFLICT: local copy shown first <<<<<<<<<<<<",
  "||||||| COMMON ANCESTOR content follows |||||||||||||||||||||||||",
  "======= MERGED IN content follows ===============================",
  ">>>>>>> END MERGE CONFLICT >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
};

/*
** Return true if the input blob contains any CR/LF pairs on the first
** ten lines. This should be enough to detect files that use mainly CR/LF
** line endings without causing a performance impact for LF only files.
*/
int contains_crlf(Blob *p){
  int i;
  int j = 0;
  const int maxL = 10;             /* Max lines to check */
  const char *z = blob_buffer(p);
  int n = blob_size(p)+1;
  for(i=1; i<n; ){
    if( z[i-1]=='\r' && z[i]=='\n' ) return 1;
    while( i<n && z[i]!='\n' ){ i++; }
    j++;
    if( j>maxL ) return 0;
  }
  return 0;
}

/*
** Ensure that the text in pBlob ends with a new line.
** If useCrLf is true adds "\r\n" otherwise '\n'.
*/
void ensure_line_end(Blob *pBlob, int useCrLf){
  if( pBlob->nUsed<=0 ) return;
  if( pBlob->aData[pBlob->nUsed-1]!='\n' ){
    if( useCrLf ) blob_append_char(pBlob, '\r');
    blob_append_char(pBlob, '\n');
  }
}

/*
** Write out one of the four merge-marks.
*/
void append_merge_mark(Blob *pOut, int iMark, int ln, int useCrLf){
  ensure_line_end(pOut, useCrLf);
  blob_append(pOut, mergeMarker[iMark], -1);
  if( ln>0 ) blob_appendf(pOut, " (line %d)", ln);
  ensure_line_end(pOut, useCrLf);
}

/*
** This is an abstract class for constructing a merge.
** Subclasses of this object format the merge output in different ways.
**
** To subclass, create an instance of the MergeBuilder object and fill
** in appropriate method implementations.
*/
typedef struct MergeBuilder MergeBuilder;
struct MergeBuilder {
  void (*xStart)(MergeBuilder*);
  void (*xSame)(MergeBuilder*, unsigned int);
  void (*xChngV1)(MergeBuilder*, unsigned int, unsigned int);
  void (*xChngV2)(MergeBuilder*, unsigned int, unsigned int);
  void (*xChngBoth)(MergeBuilder*, unsigned int, unsigned int);
  void (*xConflict)(MergeBuilder*, unsigned int, unsigned int, unsigned int);
  void (*xEnd)(MergeBuilder*);
  void (*xDestroy)(MergeBuilder*);
  const char *zPivot;        /* Label or name for the pivot */
  const char *zV1;           /* Label or name for the V1 file */
  const char *zV2;           /* Label or name for the V2 file */
  const char *zOut;          /* Label or name for the output */
  Blob *pPivot;              /* The common ancestor */
  Blob *pV1;                 /* First variant */
  Blob *pV2;                 /* Second variant */
  Blob *pOut;                /* Write merge results here */
  int useCrLf;               /* Use CRLF line endings */
  unsigned int lnPivot;      /* Lines read from pivot */
  unsigned int lnV1;         /* Lines read from v1 */
  unsigned int lnV2;         /* Lines read from v2 */
  unsigned int lnOut;        /* Lines written to out */
  unsigned int nConflict;    /* Number of conflicts seen */
};


/************************* Generic MergeBuilder ******************************/
/* These are generic methods for MergeBuilder.  They just output debugging
** information.  But some of them are useful as base methods for other useful
** implementations of MergeBuilder.
*/

/* xStart() and xEnd() are called to generate header and fotter information
** in the output.  This is a no-op in the generic implementation.
*/
static void dbgStartEnd(MergeBuilder *p){  (void)p; }

/* The next N lines of PIVOT are unchanged in both V1 and V2
*/
static void dbgSame(MergeBuilder *p, unsigned int N){
  blob_appendf(p->pOut, "COPY %u lines (%u..%u) FROM BASELINE\n",
               N, p->lnPivot, p->lnPivot+N-1);
  p->lnPivot += N;
  p->lnV1 += N;
  p->lnV2 += N;
}

/* The next nPivot lines of the PIVOT are changed into nV1 lines by V1
*/
static void dbgChngV1(MergeBuilder *p, unsigned int nPivot, unsigned int nV1){
  blob_appendf(p->pOut, "COPY %u lines (%u..%u) FROM V1\n",
               nV1, p->lnV1, p->lnV1+nV1-1);
  p->lnPivot += nPivot;
  p->lnV2 += nPivot;
  p->lnV1 += nV1;
}

/* The next nPivot lines of the PIVOT are changed into nV2 lines by V2
*/
static void dbgChngV2(MergeBuilder *p, unsigned int nPivot, unsigned int nV2){
  blob_appendf(p->pOut, "COPY %u lines (%u..%u) FROM V2\n",
               nV2, p->lnV2, p->lnV2+nV2-1);
  p->lnPivot += nPivot;
  p->lnV1 += nPivot;
  p->lnV2 += nV2;
}

/* The next nPivot lines of the PIVOT are changed into nV lines from V1 and
** V2, which should be the same.  In other words, the same change is found
** in both V1 and V2.
*/
static void dbgChngBoth(MergeBuilder *p, unsigned int nPivot, unsigned int nV){
  blob_appendf(p->pOut, "COPY %u lines (%u..%u) FROM V1\n",
               nV, p->lnV1, p->lnV1+nV-1);
  p->lnPivot += nPivot;
  p->lnV1 += nV;
  p->lnV2 += nV;
}

/* V1 and V2 have different and overlapping changes.  The next nPivot lines
** of the PIVOT are converted into nV1 lines of V1 and nV2 lines of V2.
*/
static void dbgConflict(
  MergeBuilder *p,
  unsigned int nPivot,
  unsigned int nV1,
  unsigned int nV2
){
  blob_appendf(p->pOut, 
     "CONFLICT  BASELINE(%u..%u) versus V1(%u..%u) versus V2(%u..%u)\n",
               p->lnPivot, p->lnPivot+nPivot-1,
               p->lnV1, p->lnPivot+nV1-1,
               p->lnV2, p->lnPivot+nV2-1);
  p->lnV1 += nV1;
  p->lnPivot += nPivot;
  p->lnV2 += nV2;
}

/* Generic destructor for the MergeBuilder object
*/
static void dbgDestroy(MergeBuilder *p){
  memset(p, 0, sizeof(*p));
}

/* Generic initializer for a MergeBuilder object
*/
static void mergebuilder_init(MergeBuilder *p){
  memset(p, 0, sizeof(*p));
  p->xStart = dbgStartEnd;
  p->xSame = dbgSame;
  p->xChngV1 = dbgChngV1;
  p->xChngV2 = dbgChngV2;
  p->xChngBoth = dbgChngBoth;
  p->xConflict = dbgConflict;
  p->xEnd = dbgStartEnd;
  p->xDestroy = dbgDestroy;
}


/************************* MergeBuilderText **********************************/
/* This version of MergeBuilder actually performs a merge on file and puts
** the result in pOut
*/
static void txtStart(MergeBuilder *p){
  /* If both pV1 and pV2 start with a UTF-8 byte-order-mark (BOM),
  ** keep it in the output. This should be secure enough not to cause
  ** unintended changes to the merged file and consistent with what
  ** users are using in their source files.
  */
  if( starts_with_utf8_bom(p->pV1, 0) && starts_with_utf8_bom(p->pV2, 0) ){
    blob_append(p->pOut, (char*)get_utf8_bom(0), -1);
  }
  if( contains_crlf(p->pV1) && contains_crlf(p->pV2) ){
    p->useCrLf = 1;
  }
}
static void txtSame(MergeBuilder *p, unsigned int N){
  blob_copy_lines(p->pOut, p->pPivot, N);  p->lnPivot += N;
  blob_copy_lines(0, p->pV1, N);           p->lnV1 += N;
  blob_copy_lines(0, p->pV2, N);           p->lnV2 += N;
}
static void txtChngV1(MergeBuilder *p, unsigned int nPivot, unsigned int nV1){
  blob_copy_lines(0, p->pPivot, nPivot);   p->lnPivot += nPivot;
  blob_copy_lines(0, p->pV2, nPivot);      p->lnV2 += nPivot;
  blob_copy_lines(p->pOut, p->pV1, nV1);   p->lnV1 += nV1;
}
static void txtChngV2(MergeBuilder *p, unsigned int nPivot, unsigned int nV2){
  blob_copy_lines(0, p->pPivot, nPivot);   p->lnPivot += nPivot;
  blob_copy_lines(0, p->pV1, nPivot);      p->lnV1 += nPivot;
  blob_copy_lines(p->pOut, p->pV2, nV2);   p->lnV2 += nV2;
}
static void txtChngBoth(MergeBuilder *p, unsigned int nPivot, unsigned int nV){
  blob_copy_lines(0, p->pPivot, nPivot);   p->lnPivot += nPivot;
  blob_copy_lines(0, p->pV1, nPivot);      p->lnV1 += nV;
  blob_copy_lines(p->pOut, p->pV2, nV);    p->lnV2 += nV;
}
static void txtConflict(
  MergeBuilder *p,
  unsigned int nPivot,
  unsigned int nV1,
  unsigned int nV2
){
  append_merge_mark(p->pOut, 0, p->lnV1, p->useCrLf);
  blob_copy_lines(p->pOut, p->pV1, nV1);         p->lnV1 += nV1;

  append_merge_mark(p->pOut, 1, p->lnPivot, p->useCrLf);
  blob_copy_lines(p->pOut, p->pPivot, nPivot);   p->lnPivot += nPivot;

  append_merge_mark(p->pOut, 2, p->lnV2, p->useCrLf);
  blob_copy_lines(p->pOut, p->pV2, nV2);         p->lnV2 += nV2;

  append_merge_mark(p->pOut, 3, -1, p->useCrLf);
}
static void mergebuilder_init_text(MergeBuilder *p){
  mergebuilder_init(p);
  p->xStart = txtStart;
  p->xSame = txtSame;
  p->xChngV1 = txtChngV1;
  p->xChngV2 = txtChngV2;
  p->xChngBoth = txtChngBoth;
  p->xConflict = txtConflict;
}
/*****************************************************************************/

/*
** aC[] is an "edit triple" for changes from A to B.  Advance through
** this triple to determine the number of lines to bypass on B in order
** to match an advance of sz lines on A.
*/
static int skip_conflict(
  int *aC,             /* Array of integer triples describing the edit */
  int i,               /* Index in aC[] of current location */
  int sz,              /* Lines of A that have been skipped */
  unsigned int *pLn    /* OUT: Lines of B to skip to keep aligment with A */
){
  *pLn = 0;
  while( sz>0 ){
    if( aC[i]==0 && aC[i+1]==0 && aC[i+2]==0 ) break;
    if( aC[i]>=sz ){
      aC[i] -= sz;
      break;
    }
    *pLn += aC[i];
    *pLn += aC[i+2];
    sz -= aC[i] + aC[i+1];
    i += 3;
  }
  return i;
}

/*
** Do a three-way merge.  Initialize pOut to contain the result.
**
** The merge is an edit against pV2.  Both pV1 and pV2 have a
** common origin at pPivot.  Apply the changes of pPivot ==> pV1
** to pV2.
**
** The return is 0 upon complete success. If any input file is binary,
** -1 is returned and pOut is unmodified.  If there are merge
** conflicts, the merge proceeds as best as it can and the number
** of conflicts is returns
*/
static int blob_merge(MergeBuilder *p){
  int *aC1;              /* Changes from pPivot to pV1 */
  int *aC2;              /* Changes from pPivot to pV2 */
  int i1, i2;            /* Index into aC1[] and aC2[] */
  int nCpy, nDel, nIns;  /* Number of lines to copy, delete, or insert */
  int limit1, limit2;    /* Sizes of aC1[] and aC2[] */
  int nConflict = 0;     /* Number of merge conflicts seen so far */
  DiffConfig DCfg;

  /* Compute the edits that occur from pPivot => pV1 (into aC1)
  ** and pPivot => pV2 (into aC2).  Each of the aC1 and aC2 arrays is
  ** an array of integer triples.  Within each triple, the first integer
  ** is the number of lines of text to copy directly from the pivot,
  ** the second integer is the number of lines of text to omit from the
  ** pivot, and the third integer is the number of lines of text that are
  ** inserted.  The edit array ends with a triple of 0,0,0.
  */
  diff_config_init(&DCfg, 0);
  aC1 = text_diff(p->pPivot, p->pV1, 0, &DCfg);
  aC2 = text_diff(p->pPivot, p->pV2, 0, &DCfg);
  if( aC1==0 || aC2==0 ){
    free(aC1);
    free(aC2);
    return -1;
  }

  blob_rewind(p->pV1);        /* Rewind inputs:  Needed to reconstruct output */
  blob_rewind(p->pV2);
  blob_rewind(p->pPivot);

  /* Determine the length of the aC1[] and aC2[] change vectors */
  for(i1=0; aC1[i1] || aC1[i1+1] || aC1[i1+2]; i1+=3){}
  limit1 = i1;
  for(i2=0; aC2[i2] || aC2[i2+1] || aC2[i2+2]; i2+=3){}
  limit2 = i2;

  /* Output header text and do any other required initialization */
  p->xStart(p);

  /* Loop over the two edit vectors and use them to compute merged text
  ** which is written into pOut.  i1 and i2 are multiples of 3 which are
  ** indices into aC1[] and aC2[] to the edit triple currently being
  ** processed
  */
  i1 = i2 = 0;
  while( i1<limit1 && i2<limit2 ){
    if( aC1[i1]>0 && aC2[i2]>0 ){
      /* Output text that is unchanged in both V1 and V2 */
      nCpy = min(aC1[i1], aC2[i2]);
      p->xSame(p, nCpy);
      aC1[i1] -= nCpy;
      aC2[i2] -= nCpy;
    }else
    if( aC1[i1] >= aC2[i2+1] && aC1[i1]>0 && aC2[i2+1]+aC2[i2+2]>0 ){
      /* Output edits to V2 that occurs within unchanged regions of V1 */
      nDel = aC2[i2+1];
      nIns = aC2[i2+2];
      p->xChngV2(p, nDel, nIns);
      aC1[i1] -= nDel;
      i2 += 3;
    }else
    if( aC2[i2] >= aC1[i1+1] && aC2[i2]>0 && aC1[i1+1]+aC1[i1+2]>0 ){
      /* Output edits to V1 that occur within unchanged regions of V2 */
      nDel = aC1[i1+1];
      nIns = aC1[i1+2];
      p->xChngV2(p, nDel, nIns);
      aC2[i2] -= nDel;
      i1 += 3;
    }else
    if( sameEdit(&aC1[i1], &aC2[i2], p->pV1, p->pV2) ){
      /* Output edits that are identical in both V1 and V2. */
      assert( aC1[i1]==0 );
      nDel = aC1[i1+1];
      nIns = aC1[i1+2];
      p->xChngBoth(p, nDel, nIns);
      i1 += 3;
      i2 += 3;
    }else
    {
      /* We have found a region where different edits to V1 and V2 overlap.
      ** This is a merge conflict.  Find the size of the conflict, then
      ** output both possible edits separated by distinctive marks.
      */
      unsigned int sz = 1;    /* Size of the conflict in lines */
      unsigned int nV1, nV2;
      nConflict++;
      while( !ends_at_CPY(&aC1[i1], sz) || !ends_at_CPY(&aC2[i2], sz) ){
        sz++;
      }
      i1 = skip_conflict(aC1, i1, sz, &nV1);
      i2 = skip_conflict(aC2, i2, sz, &nV2);
      p->xConflict(p, sz, nV1, nV2);
    }
 
    /* If we are finished with an edit triple, advance to the next
    ** triple.
    */
    if( i1<limit1 && aC1[i1]==0 && aC1[i1+1]==0 && aC1[i1+2]==0 ) i1+=3;
    if( i2<limit2 && aC2[i2]==0 && aC2[i2+1]==0 && aC2[i2+2]==0 ) i2+=3;
  }

  /* When one of the two edit vectors reaches its end, there might still
  ** be an insert in the other edit vector.  Output this remaining
  ** insert.
  */
  if( i1<limit1 && aC1[i1+2]>0 ){
    p->xChngV1(p, 0, aC1[i1+2]);
  }else if( i2<limit2 && aC2[i2+2]>0 ){
    p->xChngV1(p, 0, aC2[i2+2]);
  }

  /* Output footer text */
  p->xEnd(p);

  free(aC1);
  free(aC2);
  return nConflict;
}

/*
** Return true if the input string contains a merge marker on a line by
** itself.
*/
int contains_merge_marker(Blob *p){
  int i, j;
  int len = (int)strlen(mergeMarker[0]);
  const char *z = blob_buffer(p);
  int n = blob_size(p) - len + 1;
  assert( len==(int)strlen(mergeMarker[1]) );
  assert( len==(int)strlen(mergeMarker[2]) );
  assert( len==(int)strlen(mergeMarker[3]) );
  assert( count(mergeMarker)==4 );
  for(i=0; i<n; ){
    for(j=0; j<4; j++){
      if( (memcmp(&z[i], mergeMarker[j], len)==0) ){
        return 1;
      }
    }
    while( i<n && z[i]!='\n' ){ i++; }
    while( i<n && (z[i]=='\n' || z[i]=='\r') ){ i++; }
  }
  return 0;
}

/*
** Return true if the named file contains an unresolved merge marker line.
*/
int file_contains_merge_marker(const char *zFullpath){
  Blob file;
  int rc;
  blob_read_from_file(&file, zFullpath, ExtFILE);
  rc = contains_merge_marker(&file);
  blob_reset(&file);
  return rc;
}

/*
** COMMAND: 3-way-merge*
**
** Usage: %fossil 3-way-merge BASELINE V1 V2 [MERGED]
**
** Inputs are files BASELINE, V1, and V2.  The file MERGED is generated
** as output.  If no MERGED file is specified, output is sent to
** stdout.
**
** BASELINE is a common ancestor of two files V1 and V2 that have diverging
** edits.  The generated output file MERGED is the combination of all
** changes in both V1 and V2.
**
** This command has no effect on the Fossil repository.  It is a utility
** command made available for the convenience of users.  This command can
** be used, for example, to help import changes from an upstream project.
**
** Suppose an upstream project has a file named "Xup.c" which is imported
** with modifications to the local project as "Xlocal.c".  Suppose further
** that the "Xbase.c" is an exact copy of the last imported "Xup.c".
** Then to import the latest "Xup.c" while preserving all the local changes:
**
**      fossil 3-way-merge Xbase.c Xlocal.c Xup.c Xlocal.c
**      cp Xup.c Xbase.c
**      # Verify that everything still works
**      fossil commit
**
*/
void merge_3way_cmd(void){
  MergeBuilder s;
  int nConflict;
  Blob pivot, v1, v2, out;

  mergebuilder_init_text(&s);
  if( find_option("debug", 0, 0) ){
    mergebuilder_init(&s);
  }
  blob_zero(&pivot); s.pPivot = &pivot;
  blob_zero(&v1);    s.pV1 = &v1;
  blob_zero(&v2);    s.pV2 = &v2;
  blob_zero(&out);   s.pOut = &out;

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=6 && g.argc!=5 ){
    usage("[OPTIONS] PIVOT V1 V2 [MERGED]");
  }
  if( blob_read_from_file(s.pPivot, g.argv[2], ExtFILE)<0 ){
    fossil_fatal("cannot read %s", g.argv[2]);
  }
  if( blob_read_from_file(s.pV1, g.argv[3], ExtFILE)<0 ){
    fossil_fatal("cannot read %s", g.argv[3]);
  }
  if( blob_read_from_file(s.pV2, g.argv[4], ExtFILE)<0 ){
    fossil_fatal("cannot read %s", g.argv[4]);
  }
  nConflict = blob_merge(&s);
  if( g.argc==6 ){
    blob_write_to_file(s.pOut, g.argv[5]);
  }else{
    blob_write_to_file(s.pOut, "-");
  }
  s.xDestroy(&s);
  blob_reset(&pivot);
  blob_reset(&v1);
  blob_reset(&v2);
  blob_reset(&out);
  if( nConflict>0 ) fossil_warning("WARNING: %d merge conflicts", nConflict);
}

/*
** aSubst is an array of string pairs.  The first element of each pair is
** a string that begins with %.  The second element is a replacement for that
** string.
**
** This routine makes a copy of zInput into memory obtained from malloc and
** performance all applicable substitutions on that string.
*/
char *string_subst(const char *zInput, int nSubst, const char **azSubst){
  Blob x;
  int i, j;
  blob_zero(&x);
  while( zInput[0] ){
    for(i=0; zInput[i] && zInput[i]!='%'; i++){}
    if( i>0 ){
      blob_append(&x, zInput, i);
      zInput += i;
    }
    if( zInput[0]==0 ) break;
    for(j=0; j<nSubst; j+=2){
      int n = strlen(azSubst[j]);
      if( strncmp(zInput, azSubst[j], n)==0 ){
        blob_append(&x, azSubst[j+1], -1);
        zInput += n;
        break;
      }
    }
    if( j>=nSubst ){
      blob_append(&x, "%", 1);
      zInput++;
    }
  }
  return blob_str(&x);
}

#if INTERFACE
/*
** Flags to the 3-way merger
*/
#define MERGE_DRYRUN  0x0001
/*
** The MERGE_KEEP_FILES flag specifies that merge_3way() should retain
** its temporary files on error. By default they are removed after the
** merge, regardless of success or failure.
*/
#define MERGE_KEEP_FILES 0x0002
#endif


/*
** This routine is a wrapper around blob_merge() with the following
** enhancements:
**
**    (1) If the merge-command is defined, then use the external merging
**        program specified instead of the built-in blob-merge to do the
**        merging.  Panic if the external merger fails.
**        ** Not currently implemented **
**
**    (2) If gmerge-command is defined and there are merge conflicts in
**        blob_merge() then invoke the external graphical merger to resolve
**        the conflicts.
**
**    (3) If a merge conflict occurs and gmerge-command is not defined,
**        then write the pivot, original, and merge-in files to the
**        filesystem.
*/
int merge_3way(
  Blob *pPivot,       /* Common ancestor (older) */
  const char *zV1,    /* Name of file for version merging into (mine) */
  Blob *pV2,          /* Version merging from (yours) */
  Blob *pOut,         /* Output written here */
  unsigned mergeFlags /* Flags that control operation */
){
  Blob v1;               /* Content of zV1 */
  int rc;                /* Return code of subroutines and this routine */
  const char *zGMerge;   /* Name of the gmerge command */
  MergeBuilder s;        /* The merge state */

  mergebuilder_init_text(&s);
  s.pPivot = pPivot;
  s.pV1 = &v1;
  s.pV2 = pV2;
  blob_zero(pOut);
  s.pOut = pOut;
  blob_read_from_file(s.pV1, zV1, ExtFILE);
  rc = blob_merge(&s);
  zGMerge = rc<=0 ? 0 : db_get("gmerge-command", 0);
  if( (mergeFlags & MERGE_DRYRUN)==0
      && ((zGMerge!=0 && zGMerge[0]!=0)
          || (rc!=0 && (mergeFlags & MERGE_KEEP_FILES)!=0)) ){
    char *zPivot;       /* Name of the pivot file */
    char *zOrig;        /* Name of the original content file */
    char *zOther;       /* Name of the merge file */

    zPivot = file_newname(zV1, "baseline", 1);
    blob_write_to_file(s.pPivot, zPivot);
    zOrig = file_newname(zV1, "original", 1);
    blob_write_to_file(s.pV1, zOrig);
    zOther = file_newname(zV1, "merge", 1);
    blob_write_to_file(s.pV2, zOther);
    if( rc>0 ){
      if( zGMerge && zGMerge[0] ){
        char *zOut;     /* Temporary output file */
        char *zCmd;     /* Command to invoke */
        const char *azSubst[8];  /* Strings to be substituted */
        zOut = file_newname(zV1, "output", 1);
        azSubst[0] = "%baseline";  azSubst[1] = zPivot;
        azSubst[2] = "%original";  azSubst[3] = zOrig;
        azSubst[4] = "%merge";     azSubst[5] = zOther;
        azSubst[6] = "%output";    azSubst[7] = zOut;
        zCmd = string_subst(zGMerge, 8, azSubst);
        printf("%s\n", zCmd); fflush(stdout);
        fossil_system(zCmd);
        if( file_size(zOut, RepoFILE)>=0 ){
          blob_read_from_file(pOut, zOut, ExtFILE);
          file_delete(zOut);
        }
        fossil_free(zCmd);
        fossil_free(zOut);
      }
    }
    if( (mergeFlags & MERGE_KEEP_FILES)==0 ){
      file_delete(zPivot);
      file_delete(zOrig);
      file_delete(zOther);
    }
    fossil_free(zPivot);
    fossil_free(zOrig);
    fossil_free(zOther);
  }
  s.xDestroy(&s);
  return rc;
}
