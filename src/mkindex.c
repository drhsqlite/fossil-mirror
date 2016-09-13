/*
** Copyright (c) 2002 D. Richard Hipp
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
** This program scans Fossil source code files looking for special
** comments that indicate a command-line command or a webpage.  This
** routine collects information about these entry points and then
** generates (on standard output) C code used by Fossil to dispatch
** to those entry points.
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
** functions.  If cmdname ends with "*" then the command is a second-tier
** command that is not displayed by the "fossil help" command.  The
** final "*" is not considered to be part of the command name.
**
** Comment text following COMMAND: through the end of the comment is
** understood to be help text for the command specified.  This help
** text is accumulated and a table containing the text for each command
** is generated.  That table is used implement the "fossil help" command
** and the "/help" HTTP method.
**
** Multiple occurrences of WEBPAGE: or COMMAND: (but not both) can appear
** before each function name.  In this way, webpages and commands can
** have aliases.
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
  int eType;        /* 0: webpage,  1: command */
  char *zIf;        /* Enclose in #if */
  char *zFunc;      /* Name of implementation */
  char *zPath;      /* Webpage or command name */
  char *zHelp;      /* Help text */
  int iHelp;        /* Index of Help text */
} Entry;

/*
** Maximum number of entries
*/
#define N_ENTRY 5000

/*
** Maximum size of a help message
*/
#define MX_HELP 250000

/*
** Table of entries
*/
Entry aEntry[N_ENTRY];

/*
** Current help message accumulator
*/
char zHelp[MX_HELP];
int nHelp;

/*
** Most recently encountered #if
*/
char zIf[2000];

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
** Check to see if the current line is an #if and if it is, add it to
** the zIf[] string.  If the current line is an #endif or #else or #elif
** then cancel the current zIf[] string.
*/
void scan_for_if(const char *zLine){
  int i;
  int len;
  if( zLine[0]!='#' ) return;
  for(i=1; isspace(zLine[i]); i++){}
  if( zLine[i]==0 ) return;
  len = strlen(&zLine[i]);
  if( strncmp(&zLine[i],"if",2)==0 ){
    zIf[0] = '#';
    memcpy(&zIf[1], &zLine[i], len+1);
  }else if( zLine[i]=='e' ){
    zIf[0] = 0;
  }
}

/*
** Scan a line for a function that implements a web page or command.
*/
void scan_for_func(char *zLine){
  int i,j,k;
  char *z;
  if( nUsed<=nFixed ) return;
  if( strncmp(zLine, "**", 2)==0
   && isspace(zLine[2])
   && strlen(zLine)<sizeof(zHelp)-nHelp-1
   && nUsed>nFixed
   && strncmp(zLine,"** COMMAND:",11)!=0
   && strncmp(zLine,"** WEBPAGE:",11)!=0
  ){
    if( zLine[2]=='\n' ){
      zHelp[nHelp++] = '\n';
    }else{
      if( strncmp(&zLine[3], "Usage: ", 6)==0 ) nHelp = 0;
      strcpy(&zHelp[nHelp], &zLine[3]);
      nHelp += strlen(&zHelp[nHelp]);
    }
    return;
  }
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
  for(k=nHelp-1; k>=0 && isspace(zHelp[k]); k--){}
  nHelp = k+1;
  zHelp[nHelp] = 0;
  for(k=0; k<nHelp && isspace(zHelp[k]); k++){}
  if( k<nHelp ){
    z = string_dup(&zHelp[k], nHelp-k);
  }else{
    z = "";
  }
  for(k=nFixed; k<nUsed; k++){
    aEntry[k].zIf = zIf[0] ? string_dup(zIf, -1) : 0;
    aEntry[k].zFunc = string_dup(&zLine[i], j);
    aEntry[k].zHelp = z;
    z = 0;
    aEntry[k].iHelp = nFixed;
  }
  i+=j;
  while( isspace(zLine[i]) ){ i++; }
  if( zLine[i]!='(' ) goto page_skip;
  nFixed = nUsed;
  nHelp = 0;
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
  int nWeb = 0;

  qsort(aEntry, nFixed, sizeof(aEntry[0]), e_compare);

  /* Output declarations for all the action functions */
  for(i=0; i<nFixed; i++){
    if( aEntry[i].zIf ) printf("%s", aEntry[i].zIf);
    printf("extern void %s(void);\n", aEntry[i].zFunc);
    if( aEntry[i].zIf ) printf("#endif\n");
  }

  /* Output strings for all the help text */
  for(i=0; i<nFixed; i++){
    char *z = aEntry[i].zHelp;
    if( z==0 ) continue;
    if( aEntry[i].zIf ) printf("%s", aEntry[i].zIf);
    printf("static const char zHelp%03d[] = \n", aEntry[i].iHelp);
    printf("  \"");
    while( *z ){
      if( *z=='\n' ){
        printf("\\n\"\n  \"");
      }else if( *z=='"' ){
        printf("\\\"");
      }else{
        putchar(*z);
      }
      z++;
    }
    printf("\";\n");
    if( aEntry[i].zIf ) printf("#endif\n");
  }

  /* Generate the aCommand[] table */
  printf("static const CmdOrPage aCommand[] = {\n");
  for(i=0; i<nFixed; i++){
    const char *z = aEntry[i].zPath;
    int n = strlen(z);
    int cmdFlags = (1==aEntry[i].eType) ? 0x01 : 0x08;
    if( 0x01==cmdFlags ){
      if( z[n-1]=='*' ){
        n--;
        cmdFlags = 0x02;
      }else if( strncmp(z, "test-", 5)==0 ){
        cmdFlags = 0x04;
      }
    }
    if( aEntry[i].zIf ){
      printf("%s", aEntry[i].zIf);
    }else if( aEntry[i].eType==0 ){
      nWeb++;
    }
    printf("  { \"%s%.*s\",%*s%s,%*szHelp%03d, %d },\n",
      (0x08 & cmdFlags) ? "/" : "", n, z,
      25-n-((0x8&cmdFlags)!=0), "",
      aEntry[i].zFunc,
      (int)(30-strlen(aEntry[i].zFunc)), "",
      aEntry[i].iHelp,
      cmdFlags
    );
    if( aEntry[i].zIf ) printf("#endif\n");
  }
  printf("};\n");
  printf("#define FOSSIL_FIRST_CMD %d\n", nWeb);
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
    scan_for_if(zLine);
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
