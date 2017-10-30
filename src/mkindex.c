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
** This utility program scans Fossil source text looking for specially
** formatted comments and generates C source code for constant tables
** that define the behavior of commands, webpages, and settings.
**
** The source code is scanned for comment lines of the form:
**
**       WEBPAGE:  /abc/xyz
**       COMMAND:  cmdname
**       SETTING:  access-log
**
** The WEBPAGE and COMMAND comments should be followed by a function that
** implements the webpage or command.  The form of this function is:
**
**       void function_name(void){
**
** Command names can divided into three classes:  1st-tier, 2nd-tier,
** and test.  1st-tier commands are the most frequently used and the
** ones that show up with "fossil help".  2nd-tier are seldom-used and/or
** legacy command.  Test commands are unsupported commands used for testing
** and analysis only.
**
** Commands are 1st-tier by default.  If the command name begins with
** "test-" or if the command name has a "test" argument, then it becomes
** a test command.  If the command name has a "2nd-tier" argument or ends
** with a "*" character, it is second tier.  Examples:
**
**        COMMAND:  abcde*
**        COMMAND:  fghij        2nd-tier
**        COMMAND:  test-xyzzy
**        COMMAND:  xyzzy        test
**
** A SETTING: may be followed by arguments that give additional attributes
** to that setting:
**
**        SETTING:  clean-blob   versionable width=40 block-text
**        SETTING:  auto-shun    boolean default=on
**
** New arguments may be added in future releases that set additional
** bits in the eCmdFlags field.
**
** Additional lines of comment after the COMMAND: or WEBPAGE: or SETTING:
** become the built-in help text for that command or webpage or setting.
**
** Multiple COMMAND: entries can be attached to the same command, thus
** creating multiple aliases for that command.  Similarly, multiple
** WEBPAGE: entries can be attached to the same webpage function, to give
** that page aliases.
**
** For SETTING: entries, the default value for the setting can be specified
** using a default=VALUE argument if the default contains no spaces.  If the
** default value does contain spaces, use a separate line like this:
**
**        SETTING: pgp-command
**        DEFAULT: gpg --clearsign -o
**
** If no default is supplied, the default is assumed to be an empty string
** or "off" in the case of a boolean.
*/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/***************************************************************************
** These macros must match similar macros in dispatch.c.
**
** Allowed values for CmdOrPage.eCmdFlags. */
#define CMDFLAG_1ST_TIER    0x0001      /* Most important commands */
#define CMDFLAG_2ND_TIER    0x0002      /* Obscure and seldom used commands */
#define CMDFLAG_TEST        0x0004      /* Commands for testing only */
#define CMDFLAG_WEBPAGE     0x0008      /* Web pages */
#define CMDFLAG_COMMAND     0x0010      /* A command */
#define CMDFLAG_SETTING     0x0020      /* A setting */
#define CMDFLAG_VERSIONABLE 0x0040      /* A versionable setting */
#define CMDFLAG_BLOCKTEXT   0x0080      /* Multi-line text setting */
#define CMDFLAG_BOOLEAN     0x0100      /* A boolean setting */
/**************************************************************************/

/*
** Each entry looks like this:
*/
typedef struct Entry {
  int eType;        /* CMDFLAG_* values */
  char *zIf;        /* Enclose in #if */
  char *zFunc;      /* Name of implementation */
  char *zPath;      /* Webpage or command name */
  char *zHelp;      /* Help text */
  char *zDflt;      /* Default value for settings */
  char *zVar;       /* config.name for settings, if different from zPath */
  int iHelp;        /* Index of Help text */
  int iWidth;       /* Display width for SETTING: values */
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
** Number of errors
*/
int nErr = 0;

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
** Safe isspace macro.  Works with signed characters.
*/
int fossil_isspace(char c){
  return c==' ' || (c<='\r' && c>='\t');
}

/*
** Safe isident macro.  Works with signed characters.
*/
int fossil_isident(char c){
  if( c>='a' && c<='z' ) return 1;
  if( c>='A' && c<='Z' ) return 1;
  if( c>='0' && c<='9' ) return 1;
  if( c=='_' ) return 1;
  return 0;
}

/*
** Scan a line looking for comments containing zLabel.  Make
** new entries if found.
*/
void scan_for_label(const char *zLabel, char *zLine, int eType){
  int i, j;
  int len = strlen(zLabel);
  if( nUsed>=N_ENTRY ) return;
  for(i=0; fossil_isspace(zLine[i]) || zLine[i]=='*'; i++){}
  if( zLine[i]!=zLabel[0] ) return;
  if( strncmp(&zLine[i],zLabel, len)==0 ){
    i += len;
  }else{
    return;
  }
  while( fossil_isspace(zLine[i]) ){ i++; }
  if( zLine[i]=='/' ) i++;
  for(j=0; zLine[i+j] && !fossil_isspace(zLine[i+j]); j++){}
  aEntry[nUsed].eType = eType;
  if( eType & CMDFLAG_WEBPAGE ){
    aEntry[nUsed].zPath = string_dup(&zLine[i-1], j+1);
    aEntry[nUsed].zPath[0] = '/';
  }else{
    aEntry[nUsed].zPath = string_dup(&zLine[i], j);
  }
  aEntry[nUsed].zFunc = 0;
  if( (eType & CMDFLAG_COMMAND)!=0 ){
    if( strncmp(&zLine[i], "test-", 5)==0 ){
      /* Commands that start with "test-" are test-commands */
      aEntry[nUsed].eType |= CMDFLAG_TEST;
    }else if( zLine[i+j-1]=='*' ){
      /* If the command name ends in '*', remove the '*' from the name
      ** but move the command into the second tier */
      aEntry[nUsed].zPath[j-1] = 0;
      aEntry[nUsed].eType |= CMDFLAG_2ND_TIER;
    }else{
      /* Otherwise, this is a first-tier command */
      aEntry[nUsed].eType |= CMDFLAG_1ST_TIER;
    }
  }

  /* Process additional flags that might follow the command name */
  while( zLine[i+j]!=0 ){
    i += j;
    while( fossil_isspace(zLine[i]) ){ i++; }
    if( zLine[i]==0 ) break;
    for(j=0; zLine[i+j] && !fossil_isspace(zLine[i+j]); j++){}
    if( j==8 && strncmp(&zLine[i], "1st-tier", j)==0 ){
      aEntry[nUsed].eType &= ~(CMDFLAG_2ND_TIER|CMDFLAG_TEST);
      aEntry[nUsed].eType |= CMDFLAG_1ST_TIER;
    }else if( j==8 && strncmp(&zLine[i], "2nd-tier", j)==0 ){
      aEntry[nUsed].eType &= ~(CMDFLAG_1ST_TIER|CMDFLAG_TEST);
      aEntry[nUsed].eType |= CMDFLAG_2ND_TIER;
    }else if( j==4 && strncmp(&zLine[i], "test", j)==0 ){
      aEntry[nUsed].eType &= ~(CMDFLAG_1ST_TIER|CMDFLAG_2ND_TIER);
      aEntry[nUsed].eType |= CMDFLAG_TEST;
    }else if( j==7 && strncmp(&zLine[i], "boolean", j)==0 ){
      aEntry[nUsed].eType &= ~(CMDFLAG_BLOCKTEXT);
      aEntry[nUsed].iWidth = 0;
      aEntry[nUsed].eType |= CMDFLAG_BOOLEAN;
    }else if( j==10 && strncmp(&zLine[i], "block-text", j)==0 ){
      aEntry[nUsed].eType &= ~(CMDFLAG_BOOLEAN);
      aEntry[nUsed].eType |= CMDFLAG_BLOCKTEXT;
    }else if( j==11 && strncmp(&zLine[i], "versionable", j)==0 ){
      aEntry[nUsed].eType |= CMDFLAG_VERSIONABLE;
    }else if( j>6 && strncmp(&zLine[i], "width=", 6)==0 ){
      aEntry[nUsed].iWidth = atoi(&zLine[i+6]);
    }else if( j>8 && strncmp(&zLine[i], "default=", 8)==0 ){
      aEntry[nUsed].zDflt = string_dup(&zLine[i+8], j-8);
    }else if( j>9 && strncmp(&zLine[i], "variable=", 9)==0 ){
      aEntry[nUsed].zVar = string_dup(&zLine[i+9], j-9);
    }else{
      fprintf(stderr, "%s:%d: unknown option: '%.*s'\n",
              zFile, nLine, j, &zLine[i]);
      nErr++;
    }
  }

  nUsed++;
  return;
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
  for(i=1; fossil_isspace(zLine[i]); i++){}
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
** Check to see if the current line is a "** DEFAULT: ..." line for a
** SETTING definition.  If so, remember the default value.
*/
void scan_for_default(const char *zLine){
  int len;
  const char *z;
  if( nUsed<1 ) return;
  if( (aEntry[nUsed-1].eType & CMDFLAG_SETTING)==0 ) return;
  if( strncmp(zLine, "** DEFAULT: ", 12)!=0 ) return;
  z = zLine + 12;
  while( fossil_isspace(z[0]) ) z++;
  len = (int)strlen(z);
  while( len>0 && fossil_isspace(z[len-1]) ){ len--; }
  aEntry[nUsed-1].zDflt = string_dup(z,len);
}

/*
** Scan a line for a function that implements a web page or command.
*/
void scan_for_func(char *zLine){
  int i,j,k;
  char *z;
  int isSetting;
  if( nUsed<=nFixed ) return;
  if( strncmp(zLine, "**", 2)==0
   && fossil_isspace(zLine[2])
   && strlen(zLine)<sizeof(zHelp)-nHelp-1
   && nUsed>nFixed
   && strncmp(zLine,"** COMMAND:",11)!=0
   && strncmp(zLine,"** WEBPAGE:",11)!=0
   && strncmp(zLine,"** SETTING:",11)!=0
   && strncmp(zLine,"** DEFAULT:",11)!=0
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
  for(i=0; fossil_isspace(zLine[i]); i++){}
  if( zLine[i]==0 ) return;
  isSetting = (aEntry[nFixed].eType & CMDFLAG_SETTING)!=0;
  if( !isSetting ){
    if( strncmp(&zLine[i],"void",4)!=0 ){
      if( zLine[i]!='*' ) goto page_skip;
      return;
    }
    i += 4;
    if( !fossil_isspace(zLine[i]) ) goto page_skip;
    while( fossil_isspace(zLine[i]) ){ i++; }
    for(j=0; fossil_isident(zLine[i+j]); j++){}
    if( j==0 ) goto page_skip;
  }
  for(k=nHelp-1; k>=0 && fossil_isspace(zHelp[k]); k--){}
  nHelp = k+1;
  zHelp[nHelp] = 0;
  for(k=0; k<nHelp && fossil_isspace(zHelp[k]); k++){}
  if( k<nHelp ){
    z = string_dup(&zHelp[k], nHelp-k);
  }else{
    z = "";
  }
  for(k=nFixed; k<nUsed; k++){
    aEntry[k].zIf = zIf[0] ? string_dup(zIf, -1) : 0;
    aEntry[k].zFunc = isSetting ? "0" : string_dup(&zLine[i], j);
    aEntry[k].zHelp = z;
    z = 0;
    aEntry[k].iHelp = nFixed;
  }
  if( !isSetting ){
    i+=j;
    while( fossil_isspace(zLine[i]) ){ i++; }
    if( zLine[i]!='(' ) goto page_skip;
  }
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
  return strcmp(pA->zPath, pB->zPath);
}

/*
** Build the binary search table.
*/
void build_table(void){
  int i;
  int nWeb = 0;

  qsort(aEntry, nFixed, sizeof(aEntry[0]), e_compare);

  printf(
    "/* Automatically generated code\n"
    "** DO NOT EDIT!\n"
    "**\n"
    "** This file was generated by the mkindex.exe program based on\n"
    "** comments in other Fossil source files.\n"
    "*/\n"
  );

  /* Output declarations for all the action functions */
  for(i=0; i<nFixed; i++){
    if( aEntry[i].eType & CMDFLAG_SETTING ) continue;
    if( aEntry[i].zIf ) printf("%s", aEntry[i].zIf);
    printf("extern void %s(void);\n", aEntry[i].zFunc);
    if( aEntry[i].zIf ) printf("#endif\n");
  }

  /* Output strings for all the help text */
  for(i=0; i<nFixed; i++){
    char *z = aEntry[i].zHelp;
    if( z==0 ) continue;
    if( aEntry[i].zIf ) printf("%s", aEntry[i].zIf);
    printf("static const char zHelp%03d[] =\n  \"", aEntry[i].iHelp);
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
    if( aEntry[i].zIf ){
      printf("%s", aEntry[i].zIf);
    }else if( (aEntry[i].eType & CMDFLAG_WEBPAGE)!=0 ){
      nWeb++;
    }
    printf("  { \"%.*s\",%*s%s,%*szHelp%03d, 0x%03x },\n",
      n, z,
      25-n, "",
      aEntry[i].zFunc,
      (int)(29-strlen(aEntry[i].zFunc)), "",
      aEntry[i].iHelp,
      aEntry[i].eType
    );
    if( aEntry[i].zIf ) printf("#endif\n");
  }
  printf("};\n");
  printf("#define FOSSIL_FIRST_CMD %d\n", nWeb);

  /* Generate the aSetting[] table */
  printf("const Setting aSetting[] = {\n");
  for(i=0; i<nFixed; i++){
    const char *z;
    const char *zVar;
    const char *zDef;
    if( (aEntry[i].eType & CMDFLAG_SETTING)==0 ) continue;
    z = aEntry[i].zPath;
    zVar = aEntry[i].zVar;
    zDef = aEntry[i].zDflt;
    if( zDef==0 ) zDef = "";
    if( aEntry[i].zIf ){
      printf("%s", aEntry[i].zIf);
    }
    printf("  { \"%s\",%*s", z, (int)(20-strlen(z)), "");
    if( zVar ){
      printf(" \"%s\",%*s", zVar, (int)(15-strlen(zVar)), "");
    }else{
      printf(" 0,%*s", 16, "");
    }
    printf(" %3d, %d, %d, \"%s\"%*s },\n",
      aEntry[i].iWidth,
      (aEntry[i].eType & CMDFLAG_VERSIONABLE)!=0,
      (aEntry[i].eType & CMDFLAG_BLOCKTEXT)!=0,
      zDef, (int)(10-strlen(zDef)), ""
    );
    if( aEntry[i].zIf ){
      printf("#endif\n");
    }
  }
  printf("{0,0,0,0,0,0}};\n");

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
    scan_for_label("WEBPAGE:",zLine,CMDFLAG_WEBPAGE);
    scan_for_label("COMMAND:",zLine,CMDFLAG_COMMAND);
    scan_for_func(zLine);
    scan_for_label("SETTING:",zLine,CMDFLAG_SETTING);
    scan_for_default(zLine);
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
  return nErr;
}
