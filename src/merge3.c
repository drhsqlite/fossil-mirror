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
** This module implements a 3-way merge
*/
#include "config.h"
#include "merge3.h"

#if 1
# define DEBUG1(X) X
#else
# define DEBUG1(X)
#endif
#if 0
#define DEBUG2(X) X
/*
** For debugging:
** Print 16 characters of text from zBuf
*/
static const char *print16(const char *z){
  int i;
  static char zBuf[20];
  for(i=0; i<16; i++){
    if( z[i]>=0x20 && z[i]<=0x7e ){
      zBuf[i] = z[i];
    }else{
      zBuf[i] = '.';
    }
  }
  zBuf[i] = 0;
  return zBuf;
}
#else
# define DEBUG2(X)
#endif

/*
** Must be a 32-bit integer
*/
typedef unsigned int u32;

/*
** Must be a 16-bit value 
*/
typedef unsigned short int u16;

/*
** The width of a hash window in bytes.  The algorithm only works if this
** is a power of 2.
*/
#define NHASH 16

/*
** The current state of the rolling hash.
**
** z[] holds the values that have been hashed.  z[] is a circular buffer.
** z[i] is the first entry and z[(i+NHASH-1)%NHASH] is the last entry of 
** the window.
**
** Hash.a is the sum of all elements of hash.z[].  Hash.b is a weighted
** sum.  Hash.b is z[i]*NHASH + z[i+1]*(NHASH-1) + ... + z[i+NHASH-1]*1.
** (Each index for z[] should be module NHASH, of course.  The %NHASH operator
** is omitted in the prior expression for brevity.)
*/
typedef struct hash hash;
struct hash {
  u16 a, b;         /* Hash values */
  u16 i;            /* Start of the hash window */
  char z[NHASH];    /* The values that have been hashed */
};

/*
** Initialize the rolling hash using the first NHASH characters of z[]
*/
static void hash_init(hash *pHash, const char *z){
  u16 a, b, i;
  a = b = 0;
  for(i=0; i<NHASH; i++){
    a += z[i];
    b += (NHASH-i)*z[i];
    pHash->z[i] = z[i];
  }
  pHash->a = a & 0xffff;
  pHash->b = b & 0xffff;
  pHash->i = 0;
}

/*
** Advance the rolling hash by a single character "c"
*/
static void hash_next(hash *pHash, int c){
  u16 old = pHash->z[pHash->i];
  pHash->z[pHash->i] = c;
  pHash->i = (pHash->i+1)&(NHASH-1);
  pHash->a = pHash->a - old + c;
  pHash->b = pHash->b - NHASH*old + pHash->a;
}

/*
** Return a 32-bit hash value
*/
static u32 hash_32bit(hash *pHash){
  return (pHash->a & 0xffff) | (((u32)(pHash->b & 0xffff))<<16);
}

/*
** Maximum number of landmarks to set in the source file.
*/
#define MX_LANDMARK (1024*128)

/*
** A mapping structure is used to record which parts of two
** files contain the same text.  There are zero or more mapping
** entries in a mapping.  Each entry maps a segment of text in
** the source file into a segment of the output file.
**
**     fromFirst...fromLast ->  toFirst...toLast
**
** Extra text might be inserted in the output file after a 
** mapping.  The nExtra parameter records the number of bytes
** of extra text to insert.
*/
typedef struct Mapping Mapping;
struct Mapping {
  int nMap;
  int nUsed;
  struct Mapping_entry {
    int fromFirst, fromLast;
    int toFirst, toLast;
    int nExtra;
  } *aMap;
};

/*
** Free malloced memory associated with a mapping.
*/
static void MappingClear(Mapping *p){
  free(p->aMap);
  memset(p, 0, sizeof(*p));
}

/*
** Add an entry to a mapping structure.  The mapping is:
**
**     a...b  ->   c...d
**
** The nExtra parameter is initially zero.  It will be changed
** later if necessary.
*/
static void MappingInsert(Mapping *p, int a, int b, int c, int d){
  struct Mapping_entry *pEntry;
  int i;
  for(i=0, pEntry=p->aMap; i<p->nUsed; i++, pEntry++){
    if( pEntry->fromFirst==a && pEntry->fromLast==b && pEntry->toFirst==c ){
      DEBUG2( printf("DUPLICATE: %6d..%-6d %6d..%d\n", a, b, c, d); )
      return;
    }
  }
  if( p->nUsed>=p->nMap ){
    p->nMap = p->nMap * 2 + 10;
    p->aMap = realloc(p->aMap, p->nMap*sizeof(p->aMap[0]) );
    if( p->aMap==0 ) exit(1);
  }
  pEntry = &p->aMap[p->nUsed];
  pEntry->fromFirst = a;
  pEntry->fromLast = b;
  pEntry->toFirst = c;
  pEntry->toLast = d;
  pEntry->nExtra = 0;
  p->nUsed++;
}

DEBUG1(
/*
** For debugging purposes:
** Print the content of a mapping.
*/
static void MappingPrint(Mapping *pMap){
  int i;
  struct Mapping_entry *p;
  for(i=0, p=pMap->aMap; i<pMap->nUsed; i++, p++){
    printf("%6d..%-6d %6d..%-6d  %d\n",
       p->fromFirst, p->fromLast,
       p->toFirst, p->toLast, p->nExtra);
  }
}
)

/*
** Remove deleted entries from a mapping.  Deleted enties have 
** an fromFirst of less than 0.
*/
static void MappingPurgeDeletedEntries(Mapping *p){
  int i, j;
  for(i=j=0; i<p->nUsed; i++){
    if( p->aMap[i].fromFirst<0 ) continue;
    if( j<i ){
      p->aMap[j] = p->aMap[i];
    }
    j++;
  }
  p->nUsed = j;
}

/*
** Comparisons functions used for sorting elements of a Mapping
*/
static int intAbs(int x){ return x<0 ? -x : x; }
static int compareSize(const void *a, const void *b){
  const struct Mapping_entry *A = (const struct Mapping_entry*)a;
  const struct Mapping_entry *B = (const struct Mapping_entry*)b;
  int rc;
  rc = (B->fromLast - B->fromFirst) - (A->fromLast - A->fromFirst);
  if( rc==0 ){
    rc = intAbs(A->toFirst - A->fromFirst) -
              intAbs(B->toFirst - B->fromFirst);
  }
  return rc;
}
static int compareFrom(const void *a, const void *b){
  const struct Mapping_entry *A = (const struct Mapping_entry*)a;
  const struct Mapping_entry *B = (const struct Mapping_entry*)b;
  return A->fromFirst - B->fromFirst;
}
static int compareTo(const void *a, const void *b){
  const struct Mapping_entry *A = (const struct Mapping_entry*)a;
  const struct Mapping_entry *B = (const struct Mapping_entry*)b;
  return A->toFirst - B->toFirst;
}

/*
** Routines for sorting the entries of a mapping.  SortSize sorts
** the entries in order of decreasing size (largest first.)  
** SortFrom and SortTo sort the entries in order of increasing
** fromFirst and toFirst.
*/
static void MappingSortSize(Mapping *p){
  qsort(p->aMap, p->nUsed, sizeof(p->aMap[0]), compareSize);
}
static void MappingSortFrom(Mapping *p){
  qsort(p->aMap, p->nUsed, sizeof(p->aMap[0]), compareFrom);
}
static void MappingSortTo(Mapping *p){
  qsort(p->aMap, p->nUsed, sizeof(p->aMap[0]), compareTo);
}

/*
** Initialize pMap to contain a set of similarities between two files.
*/
static void MappingInit(
  const char *zSrc,      /* The source or pattern file */
  int lenSrc,            /* Length of the source file */
  const char *zOut,      /* The target file */
  int lenOut,            /* Length of the target file */
  Mapping *pMap          /* Write the map of similaries here */
){
  int i, j, base, prefix;
  int changes;
  hash h;
  int *collide;
  int origLenOut = lenOut;
  struct Mapping_entry *aMap;
  int landmark[MX_LANDMARK];

  /*
  ** Initialize the map
  */
  memset(pMap, 0, sizeof(*pMap));

  /*
  ** Find common prefix and suffix
  */
  if( lenSrc<=NHASH || lenOut<=NHASH ){
    MappingInsert(pMap, 0, 0, 0, 0);
    goto add_nextra;
  }
  for(i=0; i<lenSrc && i<lenOut && zSrc[i]==zOut[i]; i++){}
  if( i>=NHASH ){
    MappingInsert(pMap, 0, i-1, 0, i-1);
    lenSrc -= i;
    zSrc += i;
    lenOut -= i;
    zOut += i;
    if( lenSrc<=0 || lenOut<=0 ) goto add_nextra;
    prefix = i;
  }else{
    prefix = 0;
  }
  for(i=1; i<=lenSrc && i<=lenOut && zSrc[lenSrc-i]==zOut[lenOut-i]; i++){}
  if( i>NHASH ){
    MappingInsert(pMap, prefix+lenSrc-i+1, prefix+lenSrc-1,
                        prefix+lenOut-i+1, prefix+lenOut-1);
    lenSrc -= i;
    lenOut -= i;
  }

  /* If the source file is very small, it means that we have no
  ** chance of ever finding any matches.  We can leave early.
  */
  if( lenSrc<=NHASH ) goto add_nextra;

  /* Compute the hash table used to locate matching sections in the
  ** source file.
  */
  collide = malloc( lenSrc*sizeof(int)/NHASH );
  if( collide==0 ) return;
  memset(landmark, -1, sizeof(landmark));
  memset(collide, -1, lenSrc*sizeof(int)/NHASH );
  for(i=0; i<lenSrc-NHASH; i+=NHASH){
    int hv;
    hash_init(&h, &zSrc[i]);
    hv = hash_32bit(&h) & (MX_LANDMARK-1);
    collide[i/NHASH] = landmark[hv];
    landmark[hv] = i/NHASH;
  }

  /* Begin scanning the target file and generating mappings.  In this
  ** step, we generate as many mapping entries is we can.  Many of these
  ** entries might overlap.  The overlapping entries are removed by
  ** the loop the follows.
  */
  base = 0;    /* We have already checked everything before zOut[base] */
  while( base<lenOut-NHASH ){
    int iSrc, iBlock, nextBase, nextBaseI;
    hash_init(&h, &zOut[base]);
    i = 0;     /* Trying to match a landmark against zOut[base+i] */
    nextBaseI = NHASH;
    nextBase = base;
    while(1){
      int hv;

      hv = hash_32bit(&h) & (MX_LANDMARK-1);
      DEBUG2( printf("LOOKING: %d+%d+%d=%d [%s]\n",
              prefix,base,i,prefix+base+i, print16(&zOut[base+i])); )
      iBlock = landmark[hv];
      while( iBlock>=0 ){
        /*
        ** The hash window has identified a potential match against 
        ** landmark block iBlock.  But we need to investigate further.
        ** 
        ** Look for a region in zOut that matches zSrc. Anchor the search
        ** at zSrc[iSrc] and zOut[base+i].
        **
        ** Set cnt equal to the length of the match and set ofst so that
        ** zSrc[ofst] is the first element of the match. 
        */
        int cnt, ofstSrc;
        int j, k, x, y;

        /* Beginning at iSrc, match forwards as far as we can.  j counts
        ** the number of characters that match */
        iSrc = iBlock*NHASH;
        for(j=0, x=iSrc, y=base+i; x<lenSrc && y<lenOut; j++, x++, y++){
          if( zSrc[x]!=zOut[y] ) break;
        }
        j--;

        /* Beginning at iSrc-1, match backwards as far as we can.  k counts
        ** the number of characters that match */
        for(k=1; k<iSrc && k<=base+i; k++){
          if( zSrc[iSrc-k]!=zOut[base+i-k] ) break;
        }
        k--;

        /* Compute the offset and size of the matching region zSrc */
        ofstSrc = iSrc-k;
        cnt = j+k+1;
        DEBUG2( printf("MATCH %d bytes at SRC[%d..%d]: [%s]\n",
                 cnt, ofstSrc, ofstSrc+cnt-1, print16(&zSrc[ofstSrc])); )
        if( cnt>NHASH ){
          int ofstOut = base+i-k;
          DEBUG2( printf("COPY %6d..%-6d %6d..%d\n",
            prefix+ofstSrc, prefix+ofstSrc+cnt-1,
            prefix+ofstOut, prefix+ofstOut+cnt-1); )
          MappingInsert(pMap,
            prefix+ofstSrc, prefix+ofstSrc+cnt-1,
            prefix+ofstOut, prefix+ofstOut+cnt-1);
          if( nextBase < ofstOut+cnt-1 ){
            nextBase = ofstOut+cnt-1;
            nextBaseI = i+NHASH;
          }
        }

        /* Check the next matching block */
        iBlock = collide[iBlock];
      }

      /* If we found a match, then jump out to the outer loop and begin
      ** a new cycle.
      */
      if( nextBase>base && i>=nextBaseI ){
        base = nextBase;
        break;
      }

      /* Advance the hash by one character.  Keep looking for a match */
      if( base+i+NHASH>=lenOut ){
        base = lenOut;
        break;
      }
      hash_next(&h, zOut[base+i+NHASH]);
      i++;
    }
  }
  free(collide);
#if 0
  DEBUG1(
   printf("after creation:\n");
   MappingPrint(pMap);
  )
#endif

  /* In this step, we will remove overlapping entries from the mapping.
  **
  ** We use a greedy algorithm.  Select the largest mapping first and
  ** remove all overlapping mappings.  Then take the next largest
  ** mapping and remove others that overlap with it.  Keep going until
  ** all mappings have been processed.
  */
  MappingSortSize(pMap);
  do{
    changes = 0;
    for(i=0; i<pMap->nUsed; i++){
      int sortNeeded = 0;
      int purgeNeeded = 0;
      struct Mapping_entry *pA;
      pA = &pMap->aMap[i];
      for(j=i+1; j<pMap->nUsed; j++){
        int diff;
        struct Mapping_entry *pB;
        pB = &pMap->aMap[j];
        if( pB->fromLast<pA->fromFirst || pB->fromFirst>pA->fromLast ){
          /* No overlap.  Do nothing */
        }else if( pB->fromFirst>=pA->fromFirst && pB->fromLast<=pA->fromLast ){
          /* B is contained entirely within A.  Drop B */
          pB->fromFirst = -1;
          purgeNeeded = 1;
          continue;
        }else if( pB->fromFirst<pA->fromFirst ){
          /* The tail B overlaps the head of A */
          assert( pB->fromLast>=pA->fromFirst && pB->fromLast<=pA->fromLast );
          diff = pB->fromLast + 1 - pA->fromFirst;
          pB->fromLast -= diff;
          pB->toLast -= diff;
          sortNeeded = 1;
        }else{
          /* The head of B overlaps the tail of A */
          assert( pB->fromFirst<=pA->fromLast && pB->fromLast>pA->fromLast );
          diff = pA->fromLast + 1 - pB->fromFirst;
          pB->fromFirst += diff;
          pB->toFirst += diff;
          sortNeeded = 1;
        }
        if( pB->toLast<pA->toFirst || pB->toFirst>pA->toLast ){
          /* No overlap.  Do nothing */
        }else if( pB->toFirst>=pA->toFirst && pB->toLast<=pA->toLast ){
          /* B is contained entirely within A.  Drop B */
          pB->fromFirst = -1;
          purgeNeeded = 1;
        }else if( pB->toFirst<pA->toFirst ){
          /* The tail of B overlaps the head of A */
          assert( pB->toLast>=pA->toFirst && pB->toLast<=pA->toLast );
          diff = pB->toLast + 1 - pA->toFirst;
          pB->fromLast -= diff;
          pB->toLast -= diff;
          sortNeeded = 1;
        }else{
          /* The head of B overlaps the tail of A */
          assert( pB->toFirst<=pA->toLast && pB->toLast>pA->toLast );
          diff = pA->toLast + 1 - pB->toFirst;
          pB->fromFirst += diff;
          pB->toFirst += diff;
          sortNeeded = 1;
        }
      }
      if( purgeNeeded ){
        MappingPurgeDeletedEntries(pMap);
        /* changes++; */
      }
      if( sortNeeded && i<pMap->nUsed-2 ){
        MappingSortSize(pMap);
        /* changes++; */
      }
    }
  }while( changes );
  
  /* Final step:  Arrange the mapping entires so that they are in the
  ** order of the output file.  Then fill in the nExtra values.
  */
add_nextra:
  MappingSortTo(pMap);
  aMap = pMap->aMap;
  for(i=0; i<pMap->nUsed-1; i++){
    aMap[i].nExtra = aMap[i+1].toFirst - aMap[i].toLast - 1;
  }
  if( pMap->nUsed>0 && origLenOut > aMap[i].toLast+1 ){
    aMap[i].nExtra = origLenOut - aMap[i].toLast - 1;
  }
}

/*
** Translate an index into a file using a mapping.
**
** The mapping "p" shows how blocks in the input file map into blocks
** of the output file.  The index iFrom is an index into the input file.
** This routine returns the index into the output file of the corresponding
** character.
**
** If pInserted!=0 and iFrom points to the last character before a
** insert in the output file, then the return value is adjusted forward
** so that it points to the end of the insertion and the number of
** bytes inserted is written into *pInserted.  If pInserted==0 then
** iFrom always maps directly in the corresponding output file
** index regardless of whether or not it points to the last character
** before an insertion.
*/
static int MappingTranslate(Mapping *p, int iFrom, int *pInserted){
  int i;
  for(i=0; i<p->nUsed; i++){
    if( iFrom>p->aMap[i].fromLast ) continue;
    if( iFrom<=p->aMap[i].fromFirst ){
      return p->aMap[i].toFirst;
    }
    if( pInserted && iFrom==p->aMap[i].fromLast ){
      int n = p->aMap[i].nExtra;
      *pInserted = n;
      return p->aMap[i].toLast + n;
    }else{
      return p->aMap[i].toFirst + iFrom - p->aMap[i].fromFirst;
    }
  }
  i--;
  return p->aMap[i].toLast + p->aMap[i].nExtra;
}

/*
** Do a three-way merge.  Initialize pOut to contain the result.
*/
void blob_merge(Blob *pPivot, Blob *pV1, Blob *pV2, Blob *pOut){
  Mapping map1, map2, map3;
  int i, j;
  const char *zV1, *zV2;
  blob_zero(pOut);
  MappingInit(
    blob_buffer(pPivot), blob_size(pPivot),
    blob_buffer(pV1), blob_size(pV1),
    &map1);
  MappingSortFrom(&map1);
  DEBUG1( 
    printf("map1-final:\n");
    MappingPrint(&map1);
  )
  MappingInit(
    blob_buffer(pPivot), blob_size(pPivot),
    blob_buffer(pV2), blob_size(pV2),
    &map2);
  DEBUG1(
    printf("map2-final:\n");
    MappingPrint(&map2);
  )
  MappingInit(
    blob_buffer(pV1), blob_size(pV1),
    blob_buffer(pV2), blob_size(pV2),
    &map3);
  DEBUG1(
    printf("map3-final:\n");
    MappingPrint(&map3);
  )
  zV1 = blob_buffer(pV1);
  zV2 = blob_buffer(pV2);
  if( map1.aMap[0].toFirst>0 ){
    blob_append(pOut, zV1, map1.aMap[0].toFirst);
    DEBUG1( printf("INSERT %d bytes from V1[0..%d]\n",
            map1.aMap[0].toFirst, map1.aMap[0].toFirst-1); )
  }
  if( map2.aMap[0].toFirst>0 ){
    blob_append(pOut, zV2, map2.aMap[0].toFirst);
    DEBUG1( printf("INSERT %d bytes from V2[0..%d]\n",
            map2.aMap[0].toFirst, map2.aMap[0].toFirst-1); )
  }
  for(i=j=0; i<map2.nUsed; i++){
    int iFirst, iLast, nInsert, iTail;
    struct Mapping_entry *p = &map2.aMap[i];
    while( j<map3.nUsed-1 && map3.aMap[j+1].toFirst>p->toFirst ){ j++; }
    DEBUG1(
      printf("map2: %6d..%-6d %6d..%-6d  %d\n",
        p->fromFirst, p->fromLast, p->toFirst, p->toLast, p->nExtra);
      printf("map3:       j=%-6d %6d..%-6d\n", j, map3.aMap[j].toFirst,
        map3.aMap[j].toLast);
    );
    iTail = p->toLast + p->nExtra;
    if( i<map2.nUsed-1 &&
         map3.aMap[j].toFirst<=p->toFirst && map3.aMap[j].toLast>=iTail ){
      blob_append(pOut, &zV2[p->toFirst], iTail - p->toFirst + 1);
      DEBUG1(
        printf("COPY %d bytes from V2[%d..%d]\n", iTail - p->toFirst+1,
                p->toFirst, iTail);
      )
      continue;
    }
    iFirst = MappingTranslate(&map1, p->fromFirst, 0);
    iLast = MappingTranslate(&map1, p->fromLast, &nInsert);
    blob_append(pOut, &zV1[iFirst], iLast - iFirst + 1);
    DEBUG1(
      printf("COPY %d bytes from V1[%d..%d]\n", iLast-iFirst+1, iFirst, iLast);
    )
    if( p->nExtra>0 ){
      if( p->nExtra==nInsert &&
          memcmp(&zV2[p->toLast+1], &zV1[iLast-nInsert+1], nInsert)==0 ){
        /* Omit a duplicate insert */
        DEBUG1( printf("OMIT duplicate insert\n"); )
      }else{
        blob_append(pOut, &zV2[p->toLast+1], p->nExtra);
        DEBUG1(
          printf("INSERT %d bytes from V2[%d..%d]\n",
                  p->nExtra, p->toLast+1, p->toLast+p->nExtra);
        )
      }
    }
  }
  MappingClear(&map1);
  MappingClear(&map2);
  MappingClear(&map3);
}

/*
** COMMAND:  test-3-way-merge
**
** Combine change in going from PIVOT->VERSION1 with the change going
** from PIVOT->VERSION2 and write the combined changes into MERGED.
*/
void delta_3waymerge_cmd(void){
  Blob pivot, v1, v2, merged;
  if( g.argc!=6 ){
    fprintf(stderr,"Usage: %s %s PIVOT V1 V2 MERGED\n", g.argv[0], g.argv[1]);
    exit(1);
  }
  if( blob_read_from_file(&pivot, g.argv[2])<0 ){
    fprintf(stderr,"cannot read %s\n", g.argv[2]);
    exit(1);
  }
  if( blob_read_from_file(&v1, g.argv[3])<0 ){
    fprintf(stderr,"cannot read %s\n", g.argv[3]);
    exit(1);
  }
  if( blob_read_from_file(&v2, g.argv[4])<0 ){
    fprintf(stderr,"cannot read %s\n", g.argv[4]);
    exit(1);
  }
  blob_merge(&pivot, &v1, &v2, &merged);
  if( blob_write_to_file(&merged, g.argv[5])<blob_size(&merged) ){
    fprintf(stderr,"cannot write %s\n", g.argv[4]);
    exit(1);
  }
  blob_reset(&pivot);
  blob_reset(&v1);
  blob_reset(&v2);
  blob_reset(&merged);
}


/*
** COMMAND:  test-mapping
*/
void mapping_test(void){
  int i;
  const char *za, *zb;
  Blob a, b;
  Mapping map;
  if( g.argc!=4 ){
    usage("FILE1 FILE2");
  }
  blob_read_from_file(&a, g.argv[2]);
  blob_read_from_file(&b, g.argv[3]);
  memset(&map, 0, sizeof(map));
  MappingInit(blob_buffer(&a), blob_size(&a),
              blob_buffer(&b), blob_size(&b),
              &map);
  DEBUG1(
    printf("map-final:\n");
    MappingPrint(&map);
  )
  za = blob_buffer(&a);
  zb = blob_buffer(&b);
  for(i=0; i<map.nUsed; i++){
    printf("======= %6d..%-6d %6d..%-6d %d\n", 
         map.aMap[i].fromFirst, map.aMap[i].fromLast,
         map.aMap[i].toFirst, map.aMap[i].toLast,
         map.aMap[i].nExtra);
    printf("%.*s\n", map.aMap[i].fromLast - map.aMap[i].fromFirst + 1, 
                     &za[map.aMap[i].fromFirst]);
    if( map.aMap[i].nExtra ){
      printf("======= EXTRA:\n");
      printf("%.*s\n", map.aMap[i].nExtra, &zb[map.aMap[i].toLast+1]);
    }
  }
}
