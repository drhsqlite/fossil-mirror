/*
** Copyright (c) 2002 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License as published by the Free Software Foundation; either
** version 2 of the License, or (at your option) any later version.
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
** Build a static hash table that maps URLs into functions to generate
** web pages.
**
** The source code is scanned for comment lines of the form:
**
**       WEBPAGE:  /abc/xyz
**
** This comment should be followed by a function definition of the
** form:
**
**       void function_name(void){
**
** This routine creates C source code for a constant table that maps
** webpage name into pointers to the function.
**
** We also scan for comments lines of this form:
**
**       COMMAND:  cmdname
**
** These entries build a constant table used to map command names into
** functions.
*/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>

/*
** Each entry looks like this:
*/
typedef struct Entry {
  int eType;
  char *zFunc;
  char *zPath;
} Entry;

/*
** Maximum number of entries
*/
#define N_ENTRY 500

/*
** Table of entries
*/
Entry aEntry[N_ENTRY];

/*
** How many entries are used
*/
int nUsed;
int nFixed;

/*
** Current filename and line number
*/
char *zFile;
int nLine;

/*
** Duplicate N characters of a string.
*/
char *string_dup(const char *zSrc, int n){
  char *z;
  if( n<0 ) n = strlen(zSrc);
  z = malloc( n+1 );
  if( z==0 ){ fprintf(stderr,"Out of memory!\n"); exit(1); }
  strncpy(z, zSrc, n);
  z[n] = 0;
  return z;
}

/*
** Scan a line looking for comments containing zLabel.  Make
** new entries if found.
*/
void scan_for_label(const char *zLabel, char *zLine, int eType){
  int i, j;
  int len = strlen(zLabel);
  if( nUsed>=N_ENTRY ) return;
  for(i=0; isspace(zLine[i]) || zLine[i]=='*'; i++){}
  if( zLine[i]!=zLabel[0] ) return;
  if( strncmp(&zLine[i],zLabel, len)==0 ){
    i += len;
  }else{
    return;
  }
  while( isspace(zLine[i]) ){ i++; }
  if( zLine[i]=='/' ) i++;
  for(j=0; zLine[i+j] && !isspace(zLine[i+j]); j++){}
  aEntry[nUsed].eType = eType;
  aEntry[nUsed].zPath = string_dup(&zLine[i], j);
  aEntry[nUsed].zFunc = 0;
  nUsed++;
}

/*
** Scan a line for a function that implements a web page or command.
*/
void scan_for_func(char *zLine){
  int i,j,k; 
  if( nUsed<=nFixed ) return;
  for(i=0; isspace(zLine[i]); i++){}
  if( zLine[i]==0 ) return;
  if( strncmp(&zLine[i],"void",4)!=0 ){
    if( zLine[i]!='*' ) goto page_skip;
    return;
  }
  i += 4;
  if( !isspace(zLine[i]) ) goto page_skip;
  while( isspace(zLine[i]) ){ i++; }
  for(j=0; isalnum(zLine[i+j]) || zLine[i+j]=='_'; j++){}
  if( j==0 ) goto page_skip;
  for(k=nFixed; k<nUsed; k++){
    aEntry[k].zFunc = string_dup(&zLine[i], j);
  }
  i+=j;
  while( isspace(zLine[i]) ){ i++; }
  if( zLine[i]!='(' ) goto page_skip;
  nFixed = nUsed;
  return;

page_skip:   
   for(i=nFixed; i<nUsed; i++){
      fprintf(stderr,"%s:%d: skipping page \"%s\"\n",
         zFile, nLine, aEntry[i].zPath);
   }
   nUsed = nFixed;
}

/*
** Compare two entries
*/
int e_compare(const void *a, const void *b){
  const Entry *pA = (const Entry*)a;
  const Entry *pB = (const Entry*)b;
  int x = pA->eType - pB->eType;
  if( x==0 ){
    x = strcmp(pA->zPath, pB->zPath);
  }
  return x;
}

/*
** Build the binary search table.
*/
void build_table(void){
  int i;

  qsort(aEntry, nFixed, sizeof(aEntry[0]), e_compare);
  for(i=0; i<nFixed; i++){
    printf("extern void %s(void);\n", aEntry[i].zFunc);
  }
  printf(
    "typedef struct NameMap NameMap;\n"
    "struct NameMap {\n"
    "  const char *zName;\n"
    "  void (*xFunc)(void);\n"
    "};\n"
    "static const NameMap aWebpage[] = {\n"
  );
  for(i=0; i<nFixed && aEntry[i].eType==0; i++){
    printf("  { \"%s\",%*s %s },\n",
      aEntry[i].zPath, (int)(25-strlen(aEntry[i].zPath)), "",
      aEntry[i].zFunc
    );
  }
  printf("};\n");
  printf(
    "static const NameMap aCommand[] = {\n"
  );
  for(; i<nFixed && aEntry[i].eType==1; i++){
    printf("  { \"%s\",%*s %s },\n",
      aEntry[i].zPath, (int)(25-strlen(aEntry[i].zPath)), "",
      aEntry[i].zFunc
    );
  }
  printf("};\n");
}

/*
** Process a single file of input
*/
void process_file(void){
  FILE *in = fopen(zFile, "r");
  char zLine[2000];
  if( in==0 ){
    fprintf(stderr,"%s: cannot open\n", zFile);
    return;
  }
  nLine = 0;
  while( fgets(zLine, sizeof(zLine), in) ){
    nLine++;
    scan_for_label("WEBPAGE:",zLine,0);
    scan_for_label("COMMAND:",zLine,1);
    scan_for_func(zLine);
  }
  fclose(in);
  nUsed = nFixed; 
}

int main(int argc, char **argv){
  int i;
  for(i=1; i<argc; i++){
    zFile = argv[i];
    process_file();
  }
  build_table();
  return 0;
}
