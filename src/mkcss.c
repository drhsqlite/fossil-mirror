/*
** This C program generates the "default_css.h" header file from
** "default_css.txt" source file.
**
** The default_css.h header contains a definition of a structure
** with lots of default CSS snippets.  This information is used to
** generate the /style.css page as follows:
**
**    (1) Read the repository-specific CSS page from the skin
**    (2) Initialize the output to a copy of the repo-CSS from (1).
**    (3) For each entry in the cssDefaultList[], if the selector
**        described by cssDefaultList[i] is not found in the
**        repo-CSS, then append it to the output.
**
** The input file, "default_css.txt", is plain text with lots of
** comments.  This routine strips out the comments and breaks the
** text up into individual cssDefaultList[] elements.
**
** To run this program:
**
**       ./mkcss default_css.txt default_css.h
**
** In other words, there are two arguments.  The first is the name of
** the input file and the second is the name of the output file.
** Either argument can be "-" to indicate standard input or output.
**
** Input Format Summary:
**
**     # comment
**     selector {
**       rule; # comment
**     }
**     # comment
**
** It would be much easier to do this using a script, but that would
** make the Fossil source-code less cross-platform because it would then
** require that the script engine be installed on the build platform.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static FILE *open_for_reading(const char *zFilename){
  FILE *f;
  if( strcmp(zFilename, "-")==0 ) return stdin;
  f = fopen(zFilename, "r");
  if( f==0 ){
    fprintf(stderr, "cannot open \"%s\" for reading\n", zFilename);
    exit(1);
  }
  return f;
}
static FILE *open_for_writing(const char *zFilename){
  FILE *f;
  if( strcmp(zFilename, "-")==0 ) return stdout;
  f = fopen(zFilename, "w");
  if( f==0 ){
    fprintf(stderr, "cannot open \"%s\" for writing\n", zFilename);
    exit(1);
  }
  return f;
}
static void close_file(FILE *f){
  if( f!=stdin && f!=stdout){
    fclose(f);
  }
}

/*
** Print a string as a quoted C-language string.
*/
static void clang_puts(FILE *out, const char *z){
  int i;
  while( z[0] ){
    for(i=0; z[i] && z[i]!='"' && z[i]!='\\'; i++){}
    fprintf(out, "%.*s", i, z);
    if( z[i] ){
      fprintf(out, "\\%c", z[i]);
      z += i+1;
    }else{
      z += i;
    }
  }
}

int main(int argc, char *argv[]){
  FILE *in, *out;
  int inRules = 0;
  int nLine = 0;
  int iStart = 0;
  const char *zInFile;
  const char *zOutFile;
  char z[1000];
  if( argc!=3 ){
    fprintf(stderr, "Usage: %s INPUTFILE OUTPUTFILE\n", argv[0]);
    return 1;
  }
  zInFile = argv[1];
  zOutFile = argv[2];
  in = open_for_reading(zInFile);
  out = open_for_writing(zOutFile);

  fprintf(out,
     "/* DO NOT EDIT\n"
     "** This code is generated automatically using 'mkcss.c'\n"
     "*/\n"
     "const struct strctCssDefaults {\n"
     "  const char *elementClass;  /* Name of element needed */\n"
     "  const char *value;         /* CSS text */\n"
     "} cssDefaultList[] = {\n"
  );
  while( fgets(z, sizeof(z), in) ){
    int n;  /* Line length */
    int i;
    nLine++;
    if( z[0]=='/' && z[1]=='/' ) continue;  /* Skip comments */
    if( z[0]=='-' && z[1]=='-' ) continue;  /* Skip comments */
    if( z[0]=='#' && !isalnum(z[1]) ) continue;  /* Skip comments */
    n = (int)strlen(z);
    while( n>0 && isspace(z[n-1]) ){ z[--n] = 0; }
    if( z[0]==0 ) continue;  /* Blank lines */
    if( isspace(z[0]) ){
      if( !inRules ){
        fprintf(stderr, "%s:%d: CSS rule not within a selector\n",
                zInFile, nLine);
        exit(1);
      }
      for(i=0; isspace(z[i]); i++){}
      fprintf(out, "    \"  ");
      clang_puts(out, z+i);
      fprintf(out, "\\n\"\n");
    }else if( z[0]=='}' ){
      if( !inRules ){
        fprintf(stderr, "%s:%d: surplus CSS rule terminator\n",
                zInFile, nLine);
        exit(1);
      }
      fprintf(out, "  },\n");
      inRules = 0;
    }else if( z[n-1]=='{' ){
      if( inRules ){
        fprintf(stderr, "%s:%d: selector where there should be rule\n",
                zInFile, nLine);
        exit(1);
      }
      inRules = 1;
      iStart = nLine;
      fprintf(out, "  { \"");
      n--;
      while( n>0 && isspace(z[n-1]) ){ z[--n] = 0; }
      clang_puts(out, z);
      fprintf(out, "\",\n");
    }else{
      fprintf(stderr, "%s:%d: syntax error\n",
              zInFile, nLine);
      exit(1);
    }
  }
  if( inRules ){
    fprintf(stderr, "%s:%d: unterminated CSS rule\n", zInFile, iStart);
    exit(1);
  }
  close_file(in);
  fprintf(out, "  {0,0}\n};\n");
  close_file(out);
  return 0;
}
