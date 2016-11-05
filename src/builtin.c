/*
** Copyright (c) 2014 D. Richard Hipp
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
** This file contains built-in string and BLOB resources packaged as
** byte arrays.
*/
#include "config.h"
#include "builtin.h"
#include <assert.h>

/*
** The resources provided by this file are packaged by the "mkbuiltin.c"
** utility program during the built process and stored in the
** builtin_data.h file.  Include that information here:
*/
#include "builtin_data.h"

/*
** Return a pointer to built-in content
*/
const unsigned char *builtin_file(const char *zFilename, int *piSize){
  int lwr, upr, i, c;
  lwr = 0;
  upr = count(aBuiltinFiles) - 1;
  while( upr>=lwr ){
    i = (upr+lwr)/2;
    c = strcmp(aBuiltinFiles[i].zName,zFilename);
    if( c<0 ){
      lwr = i+1;
    }else if( c>0 ){
      upr = i-1;
    }else{
      if( piSize ) *piSize = aBuiltinFiles[i].nByte;
      return aBuiltinFiles[i].pData;
    }
  }
  if( piSize ) *piSize = 0;
  return 0;
}
const char *builtin_text(const char *zFilename){
  return (char*)builtin_file(zFilename, 0);
}

/*
** COMMAND: test-builtin-list
**
** List the names and sizes of all built-in resources.
*/
void test_builtin_list(void){
  int i;
  for(i=0; i<count(aBuiltinFiles); i++){
    fossil_print("%-30s %6d\n", aBuiltinFiles[i].zName,aBuiltinFiles[i].nByte);
  }
}

/*
** COMMAND: test-builtin-get
**
** Usage: %fossil test-builtin-get NAME ?OUTPUT-FILE?
*/
void test_builtin_get(void){
  const unsigned char *pData;
  int nByte;
  Blob x;
  if( g.argc!=3 && g.argc!=4 ){
    usage("NAME ?OUTPUT-FILE?");
  }
  pData = builtin_file(g.argv[2], &nByte);
  if( pData==0 ){
    fossil_fatal("no such built-in file: [%s]", g.argv[2]);
  }
  blob_init(&x, (const char*)pData, nByte);
  blob_write_to_file(&x, g.argc==4 ? g.argv[3] : "-");
  blob_reset(&x);
}
