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
** pSrc contains an edited file where aC[] describes the edit.  Part of
** pSrc has already been output.  This routine outputs additional lines
** of pSrc - lines that correspond to the next sz lines of the original
** unedited file.
**
** Note that sz counts the number of lines of text in the original file.
** But text is output from the edited file.  So the number of lines transfer
** to pOut might be different from sz.  Fewer lines appear in pOut if there
** are deletes.  More lines appear if there are inserts.
**
** The aC[] array is updated and the new index into aC[] is returned.
*/
static int output_one_side(
  Blob *pOut,     /* Write to this blob */
  Blob *pSrc,     /* The edited file that is to be copied to pOut */
  int *aC,        /* Array of integer triples describing the edit */
  int i,          /* Index in aC[] of current location in pSrc */
  int sz          /* Number of lines in unedited source to output */
){
  while( sz>0 ){
    if( aC[i]==0 && aC[i+1]==0 && aC[i+2]==0 ) break;
    if( aC[i]>=sz ){
      blob_copy_lines(pOut, pSrc, sz);
      aC[i] -= sz;
      break;
    }
    blob_copy_lines(pOut, pSrc, aC[i]);
    blob_copy_lines(pOut, pSrc, aC[i+2]);
    sz -= aC[i] + aC[i+1];
    i += 3;
  }
  return i;
}

/*
** Text of boundary markers for merge conflicts.
*/
static const char *const mergeMarker[] = {
 /*123456789 123456789 123456789 123456789 123456789 123456789 123456789*/
  "<<<<<<< BEGIN MERGE CONFLICT: local copy shown first <<<<<<<<<<<<<<<\n",
  "======= COMMON ANCESTOR content follows ============================\n",
  "======= MERGED IN content follows ==================================\n",
  ">>>>>>> END MERGE CONFLICT >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n"
};


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
static int blob_merge(Blob *pPivot, Blob *pV1, Blob *pV2, Blob *pOut){
  int *aC1;              /* Changes from pPivot to pV1 */
  int *aC2;              /* Changes from pPivot to pV2 */
  int i1, i2;            /* Index into aC1[] and aC2[] */
  int nCpy, nDel, nIns;  /* Number of lines to copy, delete, or insert */
  int limit1, limit2;    /* Sizes of aC1[] and aC2[] */
  int nConflict = 0;     /* Number of merge conflicts seen so far */

  blob_zero(pOut);         /* Merge results stored in pOut */

  /* Compute the edits that occur from pPivot => pV1 (into aC1)
  ** and pPivot => pV2 (into aC2).  Each of the aC1 and aC2 arrays is
  ** an array of integer triples.  Within each triple, the first integer
  ** is the number of lines of text to copy directly from the pivot,
  ** the second integer is the number of lines of text to omit from the
  ** pivot, and the third integer is the number of lines of text that are
  ** inserted.  The edit array ends with a triple of 0,0,0.
  */
  aC1 = text_diff(pPivot, pV1, 0, 0, 0);
  aC2 = text_diff(pPivot, pV2, 0, 0, 0);
  if( aC1==0 || aC2==0 ){
    free(aC1);
    free(aC2);
    return -1;
  }

  blob_rewind(pV1);        /* Rewind inputs:  Needed to reconstruct output */
  blob_rewind(pV2);
  blob_rewind(pPivot);

  /* Determine the length of the aC1[] and aC2[] change vectors */
  for(i1=0; aC1[i1] || aC1[i1+1] || aC1[i1+2]; i1+=3){}
  limit1 = i1;
  for(i2=0; aC2[i2] || aC2[i2+1] || aC2[i2+2]; i2+=3){}
  limit2 = i2;

  DEBUG(
    for(i1=0; i1<limit1; i1+=3){
      printf("c1: %4d %4d %4d\n", aC1[i1], aC1[i1+1], aC1[i1+2]);
    }
    for(i2=0; i2<limit2; i2+=3){
      printf("c2: %4d %4d %4d\n", aC2[i2], aC2[i2+1], aC2[i2+2]);
    }
  )

  /* Loop over the two edit vectors and use them to compute merged text
  ** which is written into pOut.  i1 and i2 are multiples of 3 which are
  ** indices into aC1[] and aC2[] to the edit triple currently being
  ** processed
  */
  i1 = i2 = 0;
  while( i1<limit1 && i2<limit2 ){
    DEBUG( printf("%d: %2d %2d %2d   %d: %2d %2d %2d\n",
           i1/3, aC1[i1], aC1[i1+1], aC1[i1+2],
           i2/3, aC2[i2], aC2[i2+1], aC2[i2+2]); )

    if( aC1[i1]>0 && aC2[i2]>0 ){
      /* Output text that is unchanged in both V1 and V2 */
      nCpy = min(aC1[i1], aC2[i2]);
      DEBUG( printf("COPY %d\n", nCpy); )
      blob_copy_lines(pOut, pPivot, nCpy);
      blob_copy_lines(0, pV1, nCpy);
      blob_copy_lines(0, pV2, nCpy);
      aC1[i1] -= nCpy;
      aC2[i2] -= nCpy;
    }else
    if( aC1[i1] >= aC2[i2+1] && aC1[i1]>0 && aC2[i2+1]+aC2[i2+2]>0 ){
      /* Output edits to V2 that occurs within unchanged regions of V1 */
      nDel = aC2[i2+1];
      nIns = aC2[i2+2];
      DEBUG( printf("EDIT -%d+%d left\n", nDel, nIns); )
      blob_copy_lines(0, pPivot, nDel);
      blob_copy_lines(0, pV1, nDel);
      blob_copy_lines(pOut, pV2, nIns);
      aC1[i1] -= nDel;
      i2 += 3;
    }else
    if( aC2[i2] >= aC1[i1+1] && aC2[i2]>0 && aC1[i1+1]+aC1[i1+2]>0 ){
      /* Output edits to V1 that occur within unchanged regions of V2 */
      nDel = aC1[i1+1];
      nIns = aC1[i1+2];
      DEBUG( printf("EDIT -%d+%d right\n", nDel, nIns); )
      blob_copy_lines(0, pPivot, nDel);
      blob_copy_lines(0, pV2, nDel);
      blob_copy_lines(pOut, pV1, nIns);
      aC2[i2] -= nDel;
      i1 += 3;
    }else
    if( sameEdit(&aC1[i1], &aC2[i2], pV1, pV2) ){
      /* Output edits that are identical in both V1 and V2. */
      assert( aC1[i1]==0 );
      nDel = aC1[i1+1];
      nIns = aC1[i1+2];
      DEBUG( printf("EDIT -%d+%d both\n", nDel, nIns); )
      blob_copy_lines(0, pPivot, nDel);
      blob_copy_lines(pOut, pV1, nIns);
      blob_copy_lines(0, pV2, nIns);
      i1 += 3;
      i2 += 3;
    }else
    {
      /* We have found a region where different edits to V1 and V2 overlap.
      ** This is a merge conflict.  Find the size of the conflict, then
      ** output both possible edits separated by distinctive marks.
      */
      int sz = 1;    /* Size of the conflict in lines */
      nConflict++;
      while( !ends_at_CPY(&aC1[i1], sz) || !ends_at_CPY(&aC2[i2], sz) ){
        sz++;
      }
      DEBUG( printf("CONFLICT %d\n", sz); )
      blob_append(pOut, mergeMarker[0], -1);
      i1 = output_one_side(pOut, pV1, aC1, i1, sz);
      blob_append(pOut, mergeMarker[1], -1);
      blob_copy_lines(pOut, pPivot, sz);
      blob_append(pOut, mergeMarker[2], -1);
      i2 = output_one_side(pOut, pV2, aC2, i2, sz);
      blob_append(pOut, mergeMarker[3], -1);
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
  DEBUG( printf("%d: %2d %2d %2d   %d: %2d %2d %2d\n",
         i1/3, aC1[i1], aC1[i1+1], aC1[i1+2],
         i2/3, aC2[i2], aC2[i2+1], aC2[i2+2]); )
  if( i1<limit1 && aC1[i1+2]>0 ){
    DEBUG( printf("INSERT +%d left\n", aC1[i1+2]); )
    blob_copy_lines(pOut, pV1, aC1[i1+2]);
  }else if( i2<limit2 && aC2[i2+2]>0 ){
    DEBUG( printf("INSERT +%d right\n", aC2[i2+2]); )
    blob_copy_lines(pOut, pV2, aC2[i2+2]);
  }

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
      if( memcmp(&z[i], mergeMarker[j], len)==0 ) return 1;
    }
    while( i<n && z[i]!='\n' ){ i++; }
    while( i<n && z[i]=='\n' ){ i++; }
  }
  return 0;
}

/*
** Return true if the named file contains an unresolved merge marker line.
*/
int file_contains_merge_marker(const char *zFullpath){
  Blob file;
  int rc;
  blob_read_from_file(&file, zFullpath);
  rc = contains_merge_marker(&file);
  blob_reset(&file);
  return rc;
}

/*
** COMMAND: 3-way-merge*
**
** Usage: %fossil 3-way-merge BASELINE V1 V2 MERGED
**
** Inputs are files BASELINE, V1, and V2.  The file MERGED is generated
** as output.
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
void delta_3waymerge_cmd(void){
  Blob pivot, v1, v2, merged;
  int nConflict;

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc!=6 ){
    usage("PIVOT V1 V2 MERGED");
  }
  if( blob_read_from_file(&pivot, g.argv[2])<0 ){
    fossil_fatal("cannot read %s", g.argv[2]);
  }
  if( blob_read_from_file(&v1, g.argv[3])<0 ){
    fossil_fatal("cannot read %s", g.argv[3]);
  }
  if( blob_read_from_file(&v2, g.argv[4])<0 ){
    fossil_fatal("cannot read %s", g.argv[4]);
  }
  nConflict = blob_merge(&pivot, &v1, &v2, &merged);
  if( blob_write_to_file(&merged, g.argv[5])<blob_size(&merged) ){
    fossil_fatal("cannot write %s", g.argv[4]);
  }
  blob_reset(&pivot);
  blob_reset(&v1);
  blob_reset(&v2);
  blob_reset(&merged);
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
  Blob v1;            /* Content of zV1 */
  int rc;             /* Return code of subroutines and this routine */

  blob_read_from_file(&v1, zV1);
  rc = blob_merge(pPivot, &v1, pV2, pOut);
  if( rc!=0 && (mergeFlags & MERGE_DRYRUN)==0 ){
    char *zPivot;       /* Name of the pivot file */
    char *zOrig;        /* Name of the original content file */
    char *zOther;       /* Name of the merge file */

    zPivot = file_newname(zV1, "baseline", 1);
    blob_write_to_file(pPivot, zPivot);
    zOrig = file_newname(zV1, "original", 1);
    blob_write_to_file(&v1, zOrig);
    zOther = file_newname(zV1, "merge", 1);
    blob_write_to_file(pV2, zOther);
    if( rc>0 ){
      const char *zGMerge;   /* Name of the gmerge command */

      zGMerge = db_get("gmerge-command", 0);
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
        if( file_wd_size(zOut)>=0 ){
          blob_read_from_file(pOut, zOut);
          file_delete(zPivot);
          file_delete(zOrig);
          file_delete(zOther);
          file_delete(zOut);
        }
        fossil_free(zCmd);
        fossil_free(zOut);
      }
    }
    fossil_free(zPivot);
    fossil_free(zOrig);
    fossil_free(zOther);
  }
  blob_reset(&v1);
  return rc;
}
