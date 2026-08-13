/*
** Copyright (c) 2019 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
*
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
** This file contains code to connect Fossil to libFuzzer.  Do a web search
** for "libfuzzer" for details about that fuzzing platform.
**
** To build on linux (the only platform for which this works at
** present) first do
**
**     ./configure
**
** Then edit the Makefile as follows:
**
**   (1)  Change CC to be "clang-6.0" or some other compiler that
**        supports libFuzzer
**
**   (2)  Change APPNAME to "fossil-fuzz"
**
**   (3)  Add "-fsanitize=fuzzer" and "-DFOSSIL_FUZZ" to TCCFLAGS.  Perhaps
**        make the first change "-fsanitize=fuzzer,undefined,address" for
**        extra, but slower, testing.
**
** Then build the fuzzer using:
**
**   make clean fossil-fuzz
**
** To run the fuzzer, create a working directory ("cases"):
**
**   mkdir cases
**
** Then seed the working directory with example input files.  For example,
** if fuzzing the wiki formatter, perhaps copy *.wiki into cases.  Then
** run the fuzzer thusly:
**
**   fossil-fuzz cases
**
** The default is to fuzz the Fossil-wiki translator.  Use the --fuzztype TYPE
** option to fuzz different aspects of the system.
*/
#include "config.h"
#include "fuzz.h"

#if LOCAL_INTERFACE
/*
** Type of fuzzing:
*/
#define FUZZ_WIKI       0      /* The Fossil-Wiki formatter */
#define FUZZ_MARKDOWN   1      /* The Markdown formatter */
#define FUZZ_ARTIFACT   2      /* Fuzz the artifact parser */
#define FUZZ_WIKI2      3      /* FOSSIL_WIKI and FOSSIL_MARKDOWN */
#define FUZZ_COMFORMAT  4      /* comment_print() */
#endif

/* The type of fuzzing to do */
static int eFuzzType = FUZZ_WIKI;

/* The fuzzer invokes this routine once for each fuzzer input
*/
int LLVMFuzzerTestOneInput(const uint8_t *aData, size_t nByte){
  Blob in, out;
  blob_init(&in, 0, 0);
  blob_append(&in, (char*)aData, (int)nByte);
  blob_zero(&out);
  switch( eFuzzType ){
    case FUZZ_WIKI: {
      wiki_convert(&in, &out, 0);
      blob_reset(&out);
      break;
    }
    case FUZZ_MARKDOWN: {
      Blob title = BLOB_INITIALIZER;
      blob_reset(&out);
      markdown_to_html(&in, &title, &out);
      blob_reset(&title);
      break;
    }
    case FUZZ_WIKI2: {
      Blob title = BLOB_INITIALIZER;
      wiki_convert(&in, &out, 0);
      blob_reset(&out);
      markdown_to_html(&in, &title, &out);
      blob_reset(&title);
      break;
    }
    case FUZZ_ARTIFACT: {
      fossil_fatal("FUZZ_ARTIFACT is not implemented.");
      break;
    }
    case FUZZ_COMFORMAT: {
      if( nByte>=3 && aData[1]!=0 && memchr(&aData[1], 0, nByte-1)!=0 ){
        int flags = (int)aData[0];
        comment_print((const char*)&aData[1],0,15,80,flags);
      }
    }
  }
  blob_reset(&in);
  blob_reset(&out);
  return 0;
}

/*
** Check fuzzer command-line options.
*/
static void fuzzer_options(void){
  const char *zType;
  db_find_and_open_repository(OPEN_OK_NOT_FOUND|OPEN_SUBSTITUTE,0);
  db_multi_exec("PRAGMA query_only=1;");
  zType = find_option("fuzztype",0,1);
  if( zType==0 || fossil_strcmp(zType,"wiki")==0 ){
    eFuzzType = FUZZ_WIKI;
  }else if( fossil_strcmp(zType,"markdown")==0 ){
    eFuzzType = FUZZ_MARKDOWN;
  }else if( fossil_strcmp(zType,"wiki2")==0 ){
    eFuzzType = FUZZ_WIKI2;
  }else if( fossil_strcmp(zType,"comformat")==0 ){
    eFuzzType = FUZZ_COMFORMAT;
  }else{
    fossil_fatal("unknown fuzz type: \"%s\"", zType);
  }
}

/* Libfuzzer invokes this routine once prior to start-up to
** process command-line options.
*/
int LLVMFuzzerInitialize(int *pArgc, char ***pArgv){
  expand_args_option(*pArgc, *pArgv);
  fuzzer_options();
  *pArgc = g.argc;
  *pArgv = g.argv;
  return 0;
}

/*
** COMMAND: test-fuzz
**
** Usage: %fossil test-fuzz [-fuzztype TYPE] INPUTFILE...
**
** Run a fuzz test using INPUTFILE as the test data.  TYPE can be one of:
**
**     comformat             Fuzz the comment_print() routine
**     wiki                  Fuzz the Fossil-wiki translator
**     markdown              Fuzz the markdown translator
**     artifact              Fuzz the artifact parser
**     wiki2                 Fuzz the Fossil-wiki and markdown translator
*/
void fuzz_command(void){
  Blob in;
  int i;
  fuzzer_options();
  verify_all_options();
  for(i=2; i<g.argc; i++){
    blob_read_from_file(&in, g.argv[i], ExtFILE);
    LLVMFuzzerTestOneInput((const uint8_t*)in.aData, (size_t)in.nUsed);
    fossil_print("%s\n", g.argv[i]);
    blob_reset(&in);
  }
}
