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
** This program reads Fossil source code files and tries to verify that
** printf-style format strings are correct.
**
** This program implements a compile-time validation step on the Fossil
** source code.  Running this program is entirely optional.  Its role is
** similar to the -Wall compiler switch on gcc, or the scan-build utility
** of clang, or other static analyzers.  The purpose is to try to identify
** problems in the source code at compile-time.  The difference is that this
** static checker is specifically designed for the particular printf formatter
** implementation used by Fossil.
**
** Checks include:
**
**    *  Verify that vararg formatting routines like blob_printf() or
**       db_multi_exec() have the correct number of arguments for their
**       format string.
**
**    *  For routines designed to generate SQL or HTML or a URL or JSON,
**       detect and warn about possible injection attacks.
*/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

/*
** Debugging switch
*/
static int eVerbose = 0;

/*
** Malloc, aborting if it fails.
*/
void *safe_malloc(int nByte){
  void *x = malloc(nByte);
  if( x==0 ){
    fprintf(stderr, "failed to allocate %d bytes\n", nByte);
    exit(1);
  }
  return x;
}
void *safe_realloc(void *pOld, int nByte){
  void *x = realloc(pOld, nByte);
  if( x==0 ){
    fprintf(stderr, "failed to allocate %d bytes\n", nByte);
    exit(1);
  }
  return x;
}

/*
** Read the entire content of the file named zFilename into memory obtained
** from malloc().   Add a zero-terminator to the end.
** Return a pointer to that memory.
*/
static char *read_file(const char *zFilename){
  FILE *in;
  char *z;
  int nByte;
  int got;
  in = fopen(zFilename, "rb");
  if( in==0 ){
    return 0;
  }
  fseek(in, 0, SEEK_END);
  nByte = ftell(in);
  fseek(in, 0, SEEK_SET);
  z = safe_malloc( nByte+1 );
  got = fread(z, 1, nByte, in);
  z[got] = 0;
  fclose(in);
  return z;
}

/*
** When parsing the input file, the following token types are recognized.
*/
#define TK_SPACE      1      /* Whitespace or comments */
#define TK_ID         2      /* An identifier */
#define TK_STR        3      /* A string literal in double-quotes */
#define TK_OTHER      4      /* Any other token */
#define TK_EOF       99      /* End of file */

/*
** Determine the length and type of the token beginning at z[0]
*/
static int token_length(const char *z, int *pType, int *pLN){
  int i;
  if( z[0]==0 ){
    *pType = TK_EOF;
    return 0;
  }
  if( z[0]=='"' || z[0]=='\'' ){
    for(i=1; z[i] && z[i]!=z[0]; i++){
      if( z[i]=='\\' && z[i+1]!=0 ){
        if( z[i+1]=='\n' ) (*pLN)++;
        i++;
      }
    }
    if( z[i]!=0 ) i++;
    *pType = z[0]=='"' ? TK_STR : TK_OTHER;
    return i;
  }
  if( isalnum(z[0]) || z[0]=='_' ){
    for(i=1; isalnum(z[i]) || z[i]=='_'; i++){}
    *pType = isalpha(z[0]) || z[0]=='_' ? TK_ID : TK_OTHER;
    return i;
  }
  if( isspace(z[0]) ){
    if( z[0]=='\n' ) (*pLN)++;
    for(i=1; isspace(z[i]); i++){
      if( z[i]=='\n' ) (*pLN)++;
    }
    *pType = TK_SPACE;
    return i;
  }
  if( z[0]=='/' && z[1]=='*' ){
    for(i=2; z[i] && (z[i]!='*' || z[i+1]!='/'); i++){
      if( z[i]=='\n' ) (*pLN)++;
    }
    if( z[i] ) i += 2;
    *pType = TK_SPACE;
    return i;
  }
  if( z[0]=='/' && z[1]=='/' ){
    for(i=2; z[i] && z[i]!='\n'; i++){}
    if( z[i] ){
      (*pLN)++;
      i++;
    }
    *pType = TK_SPACE;
    return i;
  }
  if( z[0]=='\\' && (z[1]=='\n' || (z[1]=='\r' && z[2]=='\n')) ){
    *pType = TK_SPACE;
    return 1;
  }
  *pType = TK_OTHER;
  return 1;
}

/*
** Return the next non-whitespace token
*/
const char *next_non_whitespace(const char *z, int *pLen, int *pType){
  int len;
  int eType;
  int ln = 0;
  while( (len = token_length(z, &eType, &ln))>0 && eType==TK_SPACE ){
    z += len;
  }
  *pLen = len;
  *pType = eType;
  return z;
}

/*
** Return index into z[] for the first balanced TK_OTHER token with
** value cValue.
*/
static int distance_to(const char *z, char cVal){
  int len;
  int dist = 0;
  int eType;
  int nNest = 0;
  int ln = 0;
  while( z[0] && (len = token_length(z, &eType, &ln))>0 ){
    if( eType==TK_OTHER ){
      if( z[0]==cVal && nNest==0 ){
        break;
      }else if( z[0]=='(' ){
        nNest++;
      }else if( z[0]==')' ){
        nNest--;
      }
    }
    dist += len;
    z += len;
  }
  return dist;
}

/*
** Return the first non-whitespace characters in z[]
*/
static const char *skip_space(const char *z){
  while( isspace(z[0]) ){ z++; }
  return z;
}

/*
** Remove excess whitespace and nested "()" from string z.
*/
static char *simplify_expr(char *z){
  int n = (int)strlen(z);
  while( n>0 ){
    if( isspace(z[0]) ){
      z++;
      n--;
      continue;
    }
    if( z[0]=='(' && z[n-1]==')' ){
      z++;
      n -= 2;
      continue;
    }
    break;
  }
  z[n] = 0;
  return z;
}

/*
** Return true if the input is a string literal.
*/
static int is_string_lit(const char *z){
  int nu1, nu2;
  z = next_non_whitespace(z, &nu1, &nu2);
  if( strcmp(z, "NULL")==0 ) return 1;
  return z[0]=='"';
}

/*
** Return true if the input is an expression of string literals:
**
**      EXPR ? "..." : "..."
*/
static int is_string_expr(const char *z){
  int len = 0, eType;
  const char *zOrig = z;
  len = distance_to(z, '?');
  if( z[len]==0 && skip_space(z)[0]=='(' ){
    z = skip_space(z) + 1;
    len = distance_to(z, '?');
  }
  z += len;
  if( z[0]=='?' ){
    z++;
    z = next_non_whitespace(z, &len, &eType);
    if( eType==TK_STR ){
      z += len;
      z = next_non_whitespace(z, &len, &eType);
      if( eType==TK_OTHER && z[0]==':' ){
        z += len;
        z = next_non_whitespace(z, &len, &eType);
        if( eType==TK_STR ){
          z += len;
          z = next_non_whitespace(z, &len, &eType);
          if( eType==TK_EOF ) return 1;
          if( eType==TK_OTHER && z[0]==')' && skip_space(zOrig)[0]=='(' ){
            z += len;
            z = next_non_whitespace(z, &len, &eType);
            if( eType==TK_EOF ) return 1;
          }
        }
      }
    }
  }
  return 0;
}

/*
** A list of functions that return strings that are safe to insert into
** SQL using %s.
*/
static const char *azSafeFunc[] = {
  "filename_collation",
  "leaf_is_closed_sql",
  "timeline_query_for_www",
  "timeline_query_for_tty",
  "blob_sql_text",
  "glob_expr",
  "fossil_all_reserved_names",
  "configure_inop_rhs",
  "db_setting_inop_rhs",
};

/*
** Return true if the input is an argument that is safe to use with %s
** while building an SQL statement.
*/
static int is_sql_safe(const char *z){
  int len, eType;
  int i;

  /* A string literal is safe for use with %s */
  if( is_string_lit(z) ) return 1;

  /* Certain functions are guaranteed to return a string that is safe
  ** for use with %s */
  z = next_non_whitespace(z, &len, &eType);
  for(i=0; i<sizeof(azSafeFunc)/sizeof(azSafeFunc[0]); i++){
    if( eType==TK_ID
     && strncmp(z, azSafeFunc[i], len)==0
     && strlen(azSafeFunc[i])==len
    ){
      return 1;
    }
  }

  /* Expressions of the form:  EXPR ? "..." : "...." can count as
  ** a string literal. */
  if( is_string_expr(z) ) return 1;

  /* If the "safe-for-%s" comment appears in the argument, then
  ** let it through */
  if( strstr(z, "/*safe-for-%s*/")!=0 ) return 1;

  return 0;
}

/*
** Return true if the input is an argument that is never safe for use
** with %s.
*/
static int never_safe(const char *z){
  if( strstr(z,"/*safe-for-%s*/")!=0 ) return 0;
  if( z[0]=='P' ){
    if( strncmp(z,"PIF(",4)==0 ) return 0;
    if( strncmp(z,"PCK(",4)==0 ) return 0;
    return 1;
  }
  if( strncmp(z,"cgi_param",9)==0 ) return 1;
  return 0;
}

/*
** Processing flags
*/
#define FMT_SQL   0x00001     /* Generator for SQL text */
#define FMT_HTML  0x00002     /* Generator for HTML text */
#define FMT_URL   0x00004     /* Generator for URLs */
#define FMT_JSON  0x00008     /* Generator for JSON */
#define FMT_SAFE  0x00010     /* Generator for human-readable text */
#define FMT_LIT   0x00020     /* Just verify that a string literal */
#define FMT_PX    0x00040     /* Must have a literal prefix in format string */

/*
** A list of internal Fossil interfaces that take a printf-style format
** string.
*/
struct FmtFunc {
  const char *zFName;    /* Name of the function */
  int iFmtArg;           /* Index of format argument.  Leftmost is 1. */
  unsigned fmtFlags;     /* Processing flags */
} aFmtFunc[] = {
  { "admin_log",                  1, FMT_SAFE },
  { "ajax_route_error",           2, FMT_SAFE },
  { "audit_append",               3, FMT_SAFE },
  { "backofficeTrace",            1, FMT_SAFE },
  { "backoffice_log",             1, FMT_SAFE },
  { "blob_append_sql",            2, FMT_SQL },
  { "blob_appendf",               2, FMT_SAFE },
  { "cgi_debug",                  1, FMT_SAFE },
  { "cgi_panic",                  1, FMT_SAFE },
  { "cgi_printf",                 1, FMT_HTML },
  { "cgi_printf_header",          1, FMT_HTML },
  { "cgi_redirectf",              1, FMT_URL },
  { "chref",                      2, FMT_URL },
  { "CX",                         1, FMT_HTML },
  { "db_blob",                    2, FMT_SQL },
  { "db_debug",                   1, FMT_SQL },
  { "db_double",                  2, FMT_SQL },
  { "db_err",                     1, FMT_SAFE },
  { "db_exists",                  1, FMT_SQL },
  { "db_get_mprintf",             2, FMT_SAFE },
  { "db_int",                     2, FMT_SQL },
  { "db_int64",                   2, FMT_SQL },
  { "db_lset",                    1, FMT_LIT },
  { "db_lset_int",                1, FMT_LIT },
  { "db_multi_exec",              1, FMT_SQL },
  { "db_optional_sql",            2, FMT_SQL },
  { "db_prepare",                 2, FMT_SQL },
  { "db_prepare_ignore_error",    2, FMT_SQL },
  { "db_set",                     1, FMT_LIT },
  { "db_set_int",                 1, FMT_LIT },
  { "db_set_mprintf",             3, FMT_PX },
  { "db_static_prepare",          2, FMT_SQL },
  { "db_text",                    2, FMT_SQL },
  { "db_unset",                   1, FMT_LIT },
  { "db_unset_mprintf",           2, FMT_PX },
  { "emailerError",               2, FMT_SAFE },
  { "entry_attribute",            4, FMT_LIT },
  { "fileedit_ajax_error",        2, FMT_SAFE },
  { "form_begin",                 2, FMT_URL },
  { "fossil_error",               2, FMT_SAFE },
  { "fossil_errorlog",            1, FMT_SAFE },
  { "fossil_fatal",               1, FMT_SAFE },
  { "fossil_fatal_recursive",     1, FMT_SAFE },
  { "fossil_panic",               1, FMT_SAFE },
  { "fossil_print",               1, FMT_SAFE },
  { "fossil_trace",               1, FMT_SAFE },
  { "fossil_warning",             1, FMT_SAFE },
  { "gitmirror_message",          2, FMT_SAFE },
  { "href",                       1, FMT_URL },
  { "json_new_string_f",          1, FMT_SAFE },
  { "json_set_err",               2, FMT_SAFE },
  { "json_warn",                  2, FMT_SAFE },
  { "mprintf",                    1, FMT_SAFE },
  { "multiple_choice_attribute",  3, FMT_LIT },
  { "onoff_attribute",            3, FMT_LIT },
  { "pop3_print",                 2, FMT_SAFE },
  { "smtp_send_line",             2, FMT_SAFE },
  { "smtp_server_send",           2, FMT_SAFE },
  { "socket_set_errmsg",          1, FMT_SAFE },
  { "ssl_set_errmsg",             1, FMT_SAFE },
  { "style_copy_button",          5, FMT_SAFE },
  { "style_header",               1, FMT_HTML },
  { "style_set_current_page",     1, FMT_URL },
  { "style_submenu_element",      2, FMT_URL },
  { "style_submenu_sql",          3, FMT_SQL },
  { "textarea_attribute",         5, FMT_LIT },
  { "tktsetup_generic",           1, FMT_LIT },
  { "webpage_error",              1, FMT_SAFE },
  { "webpage_notfound_error",     1, FMT_SAFE },
  { "xfersetup_generic",          1, FMT_LIT },
  { "xhref",                      2, FMT_URL },
};

/*
** Comparison function for two FmtFunc entries
*/
static int fmtfunc_cmp(const void *pAA, const void *pBB){
  const struct FmtFunc *pA = (const struct FmtFunc*)pAA;
  const struct FmtFunc *pB = (const struct FmtFunc*)pBB;
  return strcmp(pA->zFName, pB->zFName);
}

/*
** Determine if the indentifier zIdent of length nIndent is a Fossil
** internal interface that uses a printf-style argument.  Return zero if not.
** Return the index of the format string if true with the left-most
** argument having an index of 1.
*/
static int isFormatFunc(const char *zIdent, int nIdent, unsigned *pFlags){
  int upr, lwr;
  lwr = 0;
  upr = sizeof(aFmtFunc)/sizeof(aFmtFunc[0]) - 1;
  while( lwr<=upr ){
    unsigned x = (lwr + upr)/2;
    int c = strncmp(zIdent, aFmtFunc[x].zFName, nIdent);
    if( c==0 ){
      if( aFmtFunc[x].zFName[nIdent]==0 ){
        *pFlags = aFmtFunc[x].fmtFlags;
        return aFmtFunc[x].iFmtArg;
      }
      c = -1;
    }
    if( c<0 ){
      upr = x - 1;
    }else{
      lwr = x + 1;
    }
  }
  *pFlags = 0;
  return 0;
}

/*
** Return the expected number of arguments for the format string.
** Return -1 if the value cannot be computed.
**
** For each argument less than nType, store the conversion character
** for that argument in cType[i].
**
** Store the number of initial literal characters of the format string
** in *pInit.
*/
static int formatArgCount(const char *z, int nType, char *cType, int *pInit){
  int nArg = 0;
  int i, k;
  int len;
  int eType;
  int ln = 0;
  *pInit = 0;
  while( z[0] ){
    len = token_length(z, &eType, &ln);
    if( eType==TK_STR ){
      for(i=1; i<len-1 && isalpha(z[i]); i++){}
      *pInit = i-1;
      for(i=1; i<len-1; i++){
        if( z[i]!='%' ) continue;
        if( z[i+1]=='%' ){ i++; continue; }
        for(k=i+1; k<len && !isalpha(z[k]); k++){
          if( z[k]=='*' || z[k]=='#' ){
            if( nArg<nType ) cType[nArg] = z[k];
            nArg++;
          }
        }
        if( z[k]!='R' ){
          if( nArg<nType ) cType[nArg] = z[k];
          nArg++;
        }
      }
    }
    z += len;
  }
  return nArg;
}

/*
** The function call that begins at zFCall[0] (which is on line lnFCall of the
** original file) is a function that uses a printf-style format string
** on argument number fmtArg.  It has processings flags fmtFlags.  Do
** compile-time checking on this function, output any errors, and return
** the number of errors.
*/
static int checkFormatFunc(
  const char *zFilename, /* Name of the file being processed */
  const char *zFCall,    /* Pointer to start of function call */
  int lnFCall,           /* Line number that holds z[0] */
  int fmtArg,            /* Format string should be this argument */
  int fmtFlags           /* Extra processing flags */
){
  int szFName;
  int eToken;
  int ln = lnFCall;
  int len;
  const char *zStart;
  char *z;
  char *zCopy;
  int nArg = 0;
  const char **azArg = 0;
  int i, k;
  int nErr = 0;
  char *acType;
  int nInit = 0;

  szFName = token_length(zFCall, &eToken, &ln);
  zStart = next_non_whitespace(zFCall+szFName, &len, &eToken);
  assert( zStart[0]=='(' && len==1 );
  len = distance_to(zStart+1, ')');
  zCopy = safe_malloc( len + 1 );
  memcpy(zCopy, zStart+1, len);
  zCopy[len] = 0;
  azArg = 0;
  nArg = 0;
  z = zCopy;
  while( z[0] ){
    char cEnd;
    len = distance_to(z, ',');
    cEnd = z[len];
    z[len] = 0;
    azArg = safe_realloc((char*)azArg, (sizeof(azArg[0])+1)*(nArg+1));
    azArg[nArg++] = simplify_expr(z);
    if( cEnd==0 ) break;
    z += len + 1;
  }
  acType = (char*)&azArg[nArg];
  if( fmtArg>nArg ){
    printf("%s:%d: too few arguments to %.*s()\n",
           zFilename, lnFCall, szFName, zFCall);
    nErr++;
  }else{
    const char *zFmt = azArg[fmtArg-1];
    const char *zOverride = strstr(zFmt, "/*works-like:");
    if( zOverride ) zFmt = zOverride + sizeof("/*works-like:")-1;
    if( fmtFlags & FMT_LIT ){
      if( !is_string_lit(zFmt) ){
        printf("%s:%d: argument %d to %.*s() should be a string literal\n",
               zFilename, lnFCall, fmtArg, szFName, zFCall);
        nErr++;
      }
    }else if( !is_string_lit(zFmt) ){
      printf("%s:%d: %.*s() has non-constant format on arg[%d]\n",
             zFilename, lnFCall, szFName, zFCall, fmtArg-1);
      nErr++;
    }else if( (k = formatArgCount(zFmt, nArg, acType, &nInit))>=0
             && nArg!=fmtArg+k ){
      printf("%s:%d: too %s arguments to %.*s() "
             "- got %d and expected %d\n",
             zFilename, lnFCall, (nArg<fmtArg+k ? "few" : "many"),
             szFName, zFCall, nArg, fmtArg+k);
      nErr++;
    }else if( (fmtFlags & FMT_PX)!=0 ){
      if( nInit==0 ){
        printf("%s:%d: format string on %.*s() should have"
               " an ASCII character prefix\n",
          zFilename, lnFCall, szFName, zFCall);
        nErr++;
      }
    }else if( (fmtFlags & FMT_SAFE)==0 ){
      for(i=0; i<nArg && i<k; i++){
        if( (acType[i]=='s' || acType[i]=='z' || acType[i]=='b') ){
          const char *zExpr = azArg[fmtArg+i];
          if( never_safe(zExpr) ){
            printf("%s:%d: Argument %d to %.*s() is not safe for"
                   " a query parameter\n",
               zFilename, lnFCall, i+fmtArg, szFName, zFCall);
             nErr++;

          }else if( (fmtFlags & FMT_SQL)!=0 && !is_sql_safe(zExpr) ){
            printf("%s:%d: Argument %d to %.*s() not safe for SQL\n",
               zFilename, lnFCall, i+fmtArg, szFName, zFCall);
             nErr++;
          }
        }
      }
    }
  }
  if( nErr ){
    for(i=0; i<nArg; i++){
      printf("   arg[%d]: %s\n", i, azArg[i]);
    }
  }else if( eVerbose>1 ){
    printf("%s:%d: %.*s() ok for %d arguments\n",
      zFilename, lnFCall, szFName, zFCall, nArg);
  }
  free((char*)azArg);
  free(zCopy);
  return nErr;
}


/*
** Do a design-rule check of format strings for the file named zName
** with content zContent.  Write errors on standard output.  Return
** the number of errors.
*/
static int scan_file(const char *zName, const char *zContent){
  const char *z;
  int ln = 0;
  int szToken;
  int eToken;
  const char *zPrev = 0;
  int ePrev = 0;
  int szPrev = 0;
  int lnPrev = 0;
  int nCurly = 0;
  int x;
  unsigned fmtFlags = 0;
  int nErr = 0;

  if( zContent==0 ){
    printf("cannot read file: %s\n", zName);
    return 1;
  }
  for(z=zContent; z[0]; z += szToken){
    szToken = token_length(z, &eToken, &ln);
    if( eToken==TK_SPACE ) continue;
    if( eToken==TK_OTHER ){
      if( z[0]=='{' ){
        nCurly++;
      }else if( z[0]=='}' ){
        nCurly--;
      }else if( nCurly>0 && z[0]=='(' && ePrev==TK_ID
            && (x = isFormatFunc(zPrev,szPrev,&fmtFlags))>0 ){
        nErr += checkFormatFunc(zName, zPrev, lnPrev, x, fmtFlags);
      }
    }
    zPrev = z;
    ePrev = eToken;
    szPrev = szToken;
    lnPrev = ln;
  }
  return nErr;
}

/*
** Check for format-string design rule violations on all files listed
** on the command-line.
**
** The eVerbose global variable is incremented with each "-v" argument.
*/
int main(int argc, char **argv){
  int i;
  int nErr = 0;
  qsort(aFmtFunc, sizeof(aFmtFunc)/sizeof(aFmtFunc[0]),
        sizeof(aFmtFunc[0]), fmtfunc_cmp);
  for(i=1; i<argc; i++){
    char *zFile;
    if( strcmp(argv[i],"-v")==0 ){
      eVerbose++;
      continue;
    }
    if( eVerbose>0 ) printf("Processing %s...\n", argv[i]);
    zFile = read_file(argv[i]);
    nErr += scan_file(argv[i], zFile);
    free(zFile);
  }
  return nErr;
}
