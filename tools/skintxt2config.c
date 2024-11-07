/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/*
** Copyright (c) 2021 Stephan Beal (https://wanderinghorse.net/home/stephan/)
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
*******************************************************************************
**
** This application reads in Fossil SCM skin configuration files and emits
** them in a form suitable for importing directly into a fossil database
** using the (fossil config import) command.
**
** As input it requires one or more skin configuration files (css.txt,
** header.txt, footer.txt, details.txt, js.txt) and all output goes to
** stdout unless redirected using the -o FILENAME flag.
**
** Run it with no arguments or one of (help, --help, -?) for help text.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

static struct App_ {
  const char * argv0;
  time_t now;
  FILE * ostr;
} App = {
0, 0, 0
};

static void err(const char *zFmt, ...){
  va_list vargs;
  va_start(vargs, zFmt);
  fputs("ERROR: ",stderr);
  vfprintf(stderr, zFmt, vargs);
  fputc('\n', stderr);
  va_end(vargs);
}

static void app_usage(int isErr){
  FILE * const ios = isErr ? stderr : stdout;
  fprintf(ios, "Usage: %s ?OPTIONS? input-filename...\n\n",
          App.argv0);
  fprintf(ios, "Each filename must be one file which is conventionally "
          "part of a Fossil SCM skin set:\n"
          "  css.txt, header.txt, footer.txt, details.txt, js.txt\n");
  fprintf(ios, "\nOptions:\n");
  fprintf(ios, "\n\t-o FILENAME = send output to the given file. "
          "'-' means stdout (the default).\n");
  fputc('\n', ios);
}

/*
** Reads file zFilename, stores its contents in *zContent, and sets the
** length of its contents to *nContent.
**
** Returns 0 on success. On error, *zContent and *nContent are not
** modified and it may emit a message describing the problem.
*/
int read_file(char const *zFilename, unsigned char ** zContent,
              int * nContent){
  long fpos;
  int rc = 0;
  unsigned char * zMem = 0;
  FILE * f = fopen(zFilename, "rb");
  if(!f){
    err("Cannot open file %s. Errno=%d", zFilename, errno);
    return errno;
  }
  fseek(f, 0L, SEEK_END);
  rc = errno;
  if(rc){
    err("Cannot seek() file %s. Errno=%d", zFilename, rc);
    goto end;
  }
  fpos = ftell(f);
  fseek(f, 0L, SEEK_SET);
  zMem = (unsigned char *)malloc((size_t)fpos + 1);
  if(!zMem){
    err("Malloc failed.");
    rc = ENOMEM;
    goto end;
  }
  zMem[fpos] = 0;
  if(fpos && (size_t)1 != fread(zMem, (size_t)fpos, 1, f)){
    rc = EIO;
    err("Error #%d reading file %s", rc, zFilename);
    goto end;
  }
  end:
  fclose(f);
  if(rc){
    free(zMem);
  }else{
    *zContent = zMem;
    *nContent = fpos;
  }
  return rc;
}

/*
** Expects zFilename to be one of the conventional skin filename
** parts. This routine converts it to config format and emits it to
** App.ostr.
*/
int dispatch_file(char const *zFilename){
  const char * zKey = 0;
  int nContent = 0, nContent2 = 0, nOut = 0, nTime = 0, rc = 0;
  time_t theTime = App.now;
  unsigned char * zContent = 0;
  unsigned char * z = 0;
  if(strstr(zFilename, "css.txt")){
    zKey = "css";
  }else if(strstr(zFilename, "header.txt")){
    zKey = "header";
  }else if(strstr(zFilename, "footer.txt")){
    zKey = "footer";
  }else if(strstr(zFilename, "details.txt")){
    zKey = "details";
  }else if(strstr(zFilename, "js.txt")){
    zKey = "js";
  }else {
    err("Cannot determine skin part from filename: %s", zFilename);
    return 1;
  }
  rc = read_file(zFilename, &zContent, &nContent);
  if(rc) return rc;
  for( z = zContent; z < zContent + nContent; ++z ){
    /* Count file content length with ' characters doubled */
    nContent2 += ('\'' == *z) ? 2 : 1;
  }
  while(theTime > 0){/* # of digits in time */
    ++nTime;
    theTime /= 10;
  }
  fprintf(App.ostr, "config /config %d\n",
          (int)(nTime + 12/*"value"+spaces+quotes*/
                + (int)strlen(zKey) + nContent2));
  fprintf(App.ostr, "%d '%s' value '", (int)App.now, zKey);
  for( z = zContent; z < zContent + nContent; ++z ){
    /* Emit file content with ' characters doubled */
    if('\'' == (char)*z){
      fputc('\'', App.ostr);
    }
    fputc((char)*z, App.ostr);
  }
  free(zContent);
  fprintf(App.ostr, "'\n");
  return 0;
}

int main(int argc, char const * const * argv){
  int rc = 0, i ;
  App.argv0 = argv[0];
  App.ostr = stdout;
  if(argc<2){
    app_usage(1);
    rc = 1;
    goto end;
  }
  App.now = time(0);
  for( i = 1; i < argc; ++i ){
    const char * zArg = argv[i];
    if(0==strcmp(zArg,"help") ||
       0==strcmp(zArg,"--help") ||
       0==strcmp(zArg,"-?")){
      app_usage(0);
      rc = 0;
      break;
    }else if(0==strcmp(zArg,"-o")){
      /* -o OUTFILE (- == stdout) */
      ++i;
      if(i==argc){
        err("Missing filename for -o flag");
        rc = 1;
        break;
      }else{
        const char *zOut = argv[i];
        if(App.ostr != stdout){
          err("Cannot specify -o more than once.");
          rc = 1;
          break;
        }
        if(0!=strcmp("-",zOut)){
          FILE * o = fopen(zOut, "wb");
          if(!o){
            err("Could not open file %s for writing. Errno=%d",
                zOut, errno);
            rc = errno;
            break;
          }
          App.ostr = o;
        }
      }
    }else if('-' == zArg[0]){
      err("Unhandled argument: %s", zArg);
      rc = 1;
      break;
    }else{
      rc = dispatch_file(zArg);
      if(rc) break;
    }
  }
  end:
  if(App.ostr != stdout){
    fclose(App.ostr);
  }
  return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
