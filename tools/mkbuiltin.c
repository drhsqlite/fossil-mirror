/*
** Copyright (c) 2014 D. Richard Hipp
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
** This is a stand-alone utility program that is part of the Fossil build
** process.  This program reads files named on the command line and converts
** them into ANSI-C static char array variables.  Output is written onto
** standard output.
**
** Additionally, the input files may be listed in a separate list file (one
** resource name per line, optionally enclosed in double quotes). Pass the list
** via '--reslist <the-list-file>' option. Both lists, from the command line and
** the list file, are merged; duplicate file names skipped from processing.
** This option is useful to get around the command line length limitations
** under some OS, like Windows.
**
** The makefiles use this utility to package various resources (large scripts,
** GIF images, etc) that are separate files in the source code as byte
** arrays in the resulting executable.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
** Read the entire content of the file named zFilename into memory obtained
** from malloc() and return a pointer to that memory.  Write the size of the
** file into *pnByte.
*/
static unsigned char *read_file(const char *zFilename, int *pnByte){
  FILE *in;
  unsigned char *z;
  int nByte;
  int got;
  in = fopen(zFilename, "rb");
  if( in==0 ){
    return 0;
  }
  fseek(in, 0, SEEK_END);
  *pnByte = nByte = ftell(in);
  fseek(in, 0, SEEK_SET);
  z = malloc( nByte+1 );
  if( z==0 ){
    fprintf(stderr, "failed to allocate %d bytes\n", nByte+1);
    exit(1);
  }
  got = fread(z, 1, nByte, in);
  fclose(in);
  z[got] = 0;
  return z;
}

#ifndef FOSSIL_DEBUG
/*
** Try to compress a javascript file by removing unnecessary whitespace.
**
** Warning:  This compression routine does not necessarily work for any
** arbitrary Javascript source file.  But it should work ok for the
** well-behaved source files in this project.
*/
static void compressJavascript(unsigned char *z, int *pn){
  int n = *pn;
  int i, j, k;
  for(i=j=0; i<n; i++){
    unsigned char c = z[i];
    if( c=='/' && (i==0 || z[i-1]!=':')){
      if( z[i+1]=='*' ){
        while( j>0 && (z[j-1]==' ' || z[j-1]=='\t') ){ j--; }
        for(k=i+3; k<n && (z[k]!='/' || z[k-1]!='*'); k++){}
        i = k;
        continue;
      }else if( z[i+1]=='/' ){
        while( j>0 && (z[j-1]==' ' || z[j-1]=='\t') ){ j--; }
        for(k=i+2; k<n && z[k]!='\n'; k++){}
        i = k-1;
        continue;
      }
    }
    if( c=='\n' ){
      if( j==0 ) continue;
      while( j>0 && isspace(z[j-1]) ) j--;
      z[j++] = '\n';
      while( i+1<n && isspace(z[i+1]) ) i++;
      continue;
    }
    z[j++] = c;
  }
  z[j] = 0;
  *pn = j;
}
#endif /* FOSSIL_DEBUG */

/*
** There is an instance of the following for each file translated.
*/
typedef struct Resource Resource;
struct Resource {
  char *zName;
  int nByte;
  int idx;
};

typedef struct ResourceList ResourceList;
struct ResourceList {
    Resource *aRes;
    int nRes;
    char *buf;
    long bufsize;
};


Resource *read_reslist(char *name, ResourceList *list){
#define RESLIST_BUF_MAXBYTES (1L<<20)  /* 1 MB of text */
  FILE *in;
  long filesize = 0L;
  long linecount = 0L;
  char *p = 0;
  char *pb = 0;

  memset(list, 0, sizeof(*list));

  if( (in = fopen(name, "rb"))==0 ){
    return list->aRes;
  }
  fseek(in, 0L, SEEK_END);
  filesize = ftell(in);
  rewind(in);

  if( filesize > RESLIST_BUF_MAXBYTES ){
    fprintf(stderr, "List file [%s] must be smaller than %ld bytes\n", name,
            RESLIST_BUF_MAXBYTES);
    return list->aRes;
  }
  list->bufsize = filesize;
  list->buf = (char *)calloc((list->bufsize + 2), sizeof(list->buf[0]));
  if( list->buf==0 ){
    fprintf(stderr, "failed to allocated %ld bytes\n", list->bufsize + 1);
    list->bufsize = 0L;
    return list->aRes;
  }
  filesize = fread(list->buf, sizeof(list->buf[0]),list->bufsize, in);
  if ( filesize!=list->bufsize ){
    fprintf(stderr, "failed to read [%s]\n", name);
    return list->aRes;
  }
  fclose(in);

  /*
  ** append an extra newline (if missing) for a correct line count
  */
  if( list->buf[list->bufsize-1]!='\n' ) list->buf[list->bufsize]='\n';

  linecount = 0L;
  for( p = strchr(list->buf, '\n');
       p && p <= &list->buf[list->bufsize-1];
       p = strchr(++p, '\n') ){
    ++linecount;
  }

  list->aRes = (Resource *)calloc(linecount+1, sizeof(list->aRes[0]));
  for( pb = list->buf, p = strchr(pb, '\n');
       p && p <= &list->buf[list->bufsize-1];
       pb = ++p, p = strchr(pb, '\n') ){

    char *path = pb;
    char *pe = p - 1;

    /* strip leading and trailing whitespace */
    while( path < p && isspace(*path) ) ++path;
    while( pe > path && isspace(*pe) ){
      *pe = '\0';
      --pe;
    }

    /* strip outer quotes */
    while( path < p && *path=='\"') ++path;
    while( pe > path && *pe=='\"' ){
      *pe = '\0';
      --pe;
    }
    *p = '\0';

    /* skip empty path */
    if( *path ){
      list->aRes[list->nRes].zName = path;
      ++(list->nRes);
    }
  }
  return list->aRes;
}

void free_reslist(ResourceList *list){
  if( list ){
    if( list->buf ) free(list->buf);
    if( list->aRes) free(list->aRes);
    memset(list, 0, sizeof(*list));
  }
}

/*
** Compare two Resource objects for sorting purposes.  They sort
** in zName order so that Fossil can search for resources using
** a binary search.
*/
typedef int (*QsortCompareFunc)(const void *, const void*);

static int compareResource(const Resource *a, const Resource *b){
  return strcmp(a->zName, b->zName);
}

int remove_duplicates(ResourceList *list){
  char dupNameAsc[64] = "\255";
  char dupNameDesc[64] = "";
  Resource dupResAsc;
  Resource dupResDesc;
  Resource *pDupRes;
  int dupcount = 0;
  int i;

  if( list->nRes==0 ){
    return list->nRes;
  }

  /*
  ** scan for duplicates and assign their names to a string that would sort to
  ** the bottom, then re-sort and truncate the duplicates
  */
  memset(dupNameAsc, dupNameAsc[0], sizeof(dupNameAsc)-2);
  memset(dupNameDesc, dupNameDesc[0], sizeof(dupNameDesc)-2);
  memset(&dupResAsc, 0, sizeof(dupResAsc));
  dupResAsc.zName = dupNameAsc;
  memset(&dupResDesc, 0, sizeof(dupResDesc));
  dupResDesc.zName = dupNameDesc;
  pDupRes = (compareResource(&dupResAsc, &dupResDesc) > 0
             ? &dupResAsc : &dupResDesc);

  qsort(list->aRes, list->nRes, sizeof(list->aRes[0]),
       (QsortCompareFunc)compareResource);
  for( i=0; i<list->nRes-1 ; ++i){
    Resource *res = &list->aRes[i];

    while( i<list->nRes-1
           && compareResource(res, &list->aRes[i+1])==0 ){
      fprintf(stderr, "Skipped a duplicate file [%s]\n", list->aRes[i+1].zName);
      memcpy(&list->aRes[i+1], pDupRes, sizeof(list->aRes[0]));
      ++dupcount;

      ++i;
    }
  }
  if( dupcount == 0){
    return list->nRes;
  }
  qsort(list->aRes, list->nRes, sizeof(list->aRes[0]),
       (QsortCompareFunc)compareResource);
  list->nRes -= dupcount;
  memset(&list->aRes[list->nRes], 0, sizeof(list->aRes[0]));

  return list->nRes;
}

int main(int argc, char **argv){
  int i, sz;
  int j, n;
  ResourceList resList;
  Resource *aRes;
  int nRes;
  unsigned char *pData;
  int nErr = 0;
  int nSkip;
  int nPrefix = 0;
#ifndef FOSSIL_DEBUG
  int nName;
#endif

  if( argc==1 ){
    fprintf(stderr, "usage\t:%s "
      "[--prefix path] [--reslist file] [resource-file1 ...]\n",
       argv[0]
    );
    return 1;
  }
  if( argc>3 && strcmp(argv[1],"--prefix")==0 ){
    nPrefix = (int)strlen(argv[2]);
    argc -= 2;
    argv += 2;
  }

  memset(&resList, 0, sizeof(resList));
  if( argc>2 && strcmp(argv[1],"--reslist")==0 ){
    if( read_reslist(argv[2], &resList)==0 ){
      fprintf(stderr, "Failed to load resource list from [%s]", argv[2]);
      free_reslist(&resList);
      return 1;
    }
    argc -= 2;
    argv += 2;
  }

  if( argc>1 ){
    aRes = realloc(resList.aRes, (resList.nRes+argc-1)*sizeof(resList.aRes[0]));
    if( aRes==0 || aRes==resList.aRes ){
      fprintf(stderr, "realloc failed\n");
      free_reslist(&resList);
      return 1;
    }
    resList.aRes = aRes;

    for(i=0; i<argc-1; i++){
      resList.aRes[resList.nRes].zName = argv[i+1];
      ++resList.nRes;
    }
  }

  if( resList.nRes==0 ){
      fprintf(stderr,"No resource files to process\n");
      free_reslist(&resList);
      return 1;
  }
  remove_duplicates(&resList);

  nRes = resList.nRes;
  aRes = resList.aRes;
  qsort(aRes, nRes, sizeof(aRes[0]), (QsortCompareFunc)compareResource);

  printf("/* Automatically generated code:  Do not edit.\n**\n"
         "** Rerun the \"mkbuiltin.c\" program or rerun the Fossil\n"
         "** makefile to update this source file.\n"
         "*/\n");
  for(i=0; i<nRes; i++){
    pData = read_file(aRes[i].zName, &sz);
    if( pData==0 ){
      fprintf(stderr, "Cannot open file [%s]\n", aRes[i].zName);
      nErr++;
      continue;
    }

    /* Skip initial lines beginning with # */
    nSkip = 0;
    while( pData[nSkip]=='#' ){
      while( pData[nSkip]!=0 && pData[nSkip]!='\n' ){ nSkip++; }
      if( pData[nSkip]=='\n' ) nSkip++;
    }

#ifndef FOSSIL_DEBUG
    /* Compress javascript source files */
    nName = (int)strlen(aRes[i].zName);
    if( (nName>3 && strcmp(&aRes[i].zName[nName-3],".js")==0)
     || (nName>7  && strcmp(&aRes[i].zName[nName-7], "/js.txt")==0)
    ){
      int x = sz-nSkip;
      compressJavascript(pData+nSkip, &x);
      sz = x + nSkip;
    }
#endif

    aRes[i].nByte = sz - nSkip;
    aRes[i].idx = i;
    printf("/* Content of file %s */\n", aRes[i].zName);
    printf("static const unsigned char bidata%d[%d] = {\n  ",
           i, sz+1-nSkip);
    for(j=nSkip, n=0; j<=sz; j++){
      printf("%3d", pData[j]);
      if( j==sz ){
        printf(" };\n");
      }else if( n==14 ){
        printf(",\n  ");
        n = 0;
      }else{
        printf(", ");
        n++;
      }
    }
    free(pData);
  }
  printf("typedef struct BuiltinFileTable BuiltinFileTable;\n");
  printf("struct BuiltinFileTable {\n");
  printf("  const char *zName;\n");
  printf("  const unsigned char *pData;\n");
  printf("  int nByte;\n");
  printf("};\n");
  printf("static const BuiltinFileTable aBuiltinFiles[] = {\n");
  for(i=0; i<nRes; i++){
    char *z = aRes[i].zName;
    if( strlen(z)>=nPrefix ) z += nPrefix;
    while( z[0]=='.' || z[0]=='/' || z[0]=='\\' ){ z++; }
    aRes[i].zName = z;
    while( z[0] ){
      if( z[0]=='\\' ) z[0] = '/';
      z++;
    }
  }
  qsort(aRes, nRes, sizeof(aRes[0]), (QsortCompareFunc)compareResource);
  for(i=0; i<nRes; i++){
    printf("  { \"%s\", bidata%d, %d },\n",
           aRes[i].zName, aRes[i].idx, aRes[i].nByte);
  }
  printf("};\n");
  free_reslist(&resList);
  return nErr;
}
