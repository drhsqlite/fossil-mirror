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
** Return the index in the aBuiltinFiles[] array for the file
** whose name is zFilename.  Or return -1 if the file is not
** found.
*/
static int builtin_file_index(const char *zFilename){
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
      return i;
    }
  }
  return -1;
}

/*
** Return a pointer to built-in content
*/
const unsigned char *builtin_file(const char *zFilename, int *piSize){
  int i = builtin_file_index(zFilename);
  if( i>=0 ){
    if( piSize ) *piSize = aBuiltinFiles[i].nByte;
    return aBuiltinFiles[i].pData;
  }else{
    if( piSize ) *piSize = 0;
    return 0;
  }
}
const char *builtin_text(const char *zFilename){
  return (char*)builtin_file(zFilename, 0);
}

/*
** COMMAND: test-builtin-list
**
** If -verbose is used, it outputs a line at the end
** with the total item count and size.
**
** List the names and sizes of all built-in resources.
*/
void test_builtin_list(void){
  int i, size = 0;;
  for(i=0; i<count(aBuiltinFiles); i++){
    const int n = aBuiltinFiles[i].nByte;
    fossil_print("%3d. %-45s %6d\n", i+1, aBuiltinFiles[i].zName,n);
    size += n;
  }
  if(find_option("verbose","v",0)!=0){
    fossil_print("%d entries totaling %d bytes\n", i, size);
  }
}

/*
** WEBPAGE: test-builtin-files
**
** Show all built-in text files.
*/
void test_builtin_list_page(void){
  int i;
  style_header("Built-in Text Files");
  @ <ol>
  for(i=0; i<count(aBuiltinFiles); i++){
    const char *z = aBuiltinFiles[i].zName;
    char *zUrl = href("%R/builtin?name=%T&id=%.8s&mimetype=text/plain",
           z,fossil_exe_id());
    @ <li>%z(zUrl)%h(z)</a>
  }
  @ </ol>
  style_footer();
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

/*
** Input zList is a list of numeric identifiers for files in
** aBuiltinFiles[].  Return the concatenation of all of those
** files using mimetype zType, or as application/javascript if
** zType is 0.
*/
static void builtin_deliver_multiple_js_files(
  const char *zList,   /* List of numeric identifiers */
  const char *zType    /* Override mimetype */
){
  Blob *pOut;
  if( zType==0 ) zType = "application/javascript";
  cgi_set_content_type(zType);
  pOut = cgi_output_blob();
  while( zList[0] ){
    int i = atoi(zList);
    if( i>0 && i<=count(aBuiltinFiles) ){
      blob_appendf(pOut, "/* %s */\n", aBuiltinFiles[i-1].zName);
      blob_append(pOut, (const char*)aBuiltinFiles[i-1].pData,
                  aBuiltinFiles[i-1].nByte);
    }
    while( fossil_isdigit(zList[0]) ) zList++;
    if( zList[0]==',' ) zList++;
  }
  return;
}

/*
** WEBPAGE: builtin
**
** Return one of many built-in content files.  Query parameters:
**
**    name=FILENAME       Return the single file whose name is FILENAME.
**    mimetype=TYPE       Override the mimetype in the returned file to
**                        be TYPE.  If this query parameter is omitted
**                        (the usual case) then the mimetype is inferred
**                        from the suffix on FILENAME
**    m=IDLIST            IDLIST is a comma-separated list of integers
**                        that specify multiple javascript files to be
**                        concatenated and returned all at once.
**    id=UNIQUEID         Version number of the "builtin" files.  Used
**                        for cache control only.
**
** At least one of the name= or m= query parameters must be present.
**
** If the id= query parameter is present, then Fossil assumes that the
** result is immutable and sets a very large cache retention time (1 year).
*/
void builtin_webpage(void){
  Blob out;
  const char *zName = P("name");
  const char *zTxt = 0;
  const char *zId = P("id");
  const char *zType = P("mimetype");
  int nId;
  if( zName ) zTxt = builtin_text(zName);
  if( zTxt==0 ){
    const char *zM = P("m");
    if( zM ){
      if( zId && (nId = (int)strlen(zId))>=8
       && strncmp(zId,fossil_exe_id(),nId)==0
      ){
        g.isConst = 1;
      }
      etag_check(0,0);
      builtin_deliver_multiple_js_files(zM, zType);
      return;
    }
    cgi_set_status(404, "Not Found");
    @ File "%h(zName)" not found
    return;
  }
  if( zType==0 ){
    if( sqlite3_strglob("*.js", zName)==0 ){
      zType = "application/javascript";
    }else{
      zType = mimetype_from_name(zName);
    }
  }
  cgi_set_content_type(zType);
  if( zId
   && (nId = (int)strlen(zId))>=8
   && strncmp(zId,fossil_exe_id(),nId)==0
  ){
    g.isConst = 1;
  }
  etag_check(0,0);
  blob_init(&out, zTxt, -1);
  cgi_set_content(&out);
}

/* Variables controlling the JS cache.
*/
static struct {
  int aReq[30];        /* Indexes of all requested built-in JS files */
  int nReq;            /* Number of slots in aReq[] currently used */
  int nSent;           /* Number of slots in aReq[] fulfilled */
  int eDelivery;       /* Delivery mechanism */
} builtin;

#if INTERFACE
/* Various delivery mechanisms.  The 0 option is the default.
*/
#define JS_INLINE   0    /* inline, batched together at end of file */
#define JS_SEPARATE 1    /* Separate HTTP request for each JS file */
#define JS_BUNDLED  2    /* One HTTP request to load all JS files */
                         /* concatenated together into a bundle */
#endif /* INTERFACE */

/*
** The argument is a request to change the javascript delivery mode.
** The argument is a string which is a command-line option or CGI
** parameter.  Try to match it against one of the delivery options
** and set things up accordingly.  Throw an error if no match unless
** bSilent is true.
*/
void builtin_set_js_delivery_mode(const char *zMode, int bSilent){
  if( zMode==0 ) return;
  if( strcmp(zMode, "inline")==0 ){
    builtin.eDelivery = JS_INLINE;
  }else
  if( strcmp(zMode, "separate")==0 ){
    builtin.eDelivery = JS_SEPARATE;
  }else
  if( strcmp(zMode, "bundled")==0 ){
    builtin.eDelivery = JS_BUNDLED;
  }else if( !bSilent ){
    fossil_fatal("unknown javascript delivery mode \"%s\" - should be"
                 " one of: inline separate bundled", zMode);
  }
}

/*
** The caller wants the Javascript file named by zFilename to be
** included in the generated page.  Add the file to the queue of
** requested javascript resources, if it is not there already.
**
** The current implementation queues the file to be included in the
** output later.  However, the caller should not depend on that
** behavior.  In the future, this routine might decide to insert
** the requested javascript inline, immedaitely, or to insert
** a <script src=..> element to reference the javascript as a
** separate resource.  The exact behavior might change in the future
** so pages that use this interface must not rely on any particular
** behavior.
**
** All this routine guarantees is that the named javascript file
** will be requested by the browser at some point.  This routine
** does not guarantee when the javascript will be included, and it
** does not guarantee whether the javascript will be added inline or
** delivered as a separate resource.
*/
void builtin_request_js(const char *zFilename){
  int i = builtin_file_index(zFilename);
  int j;
  if( i<0 ){
    fossil_panic("unknown javascript file: \"%s\"", zFilename);
  }
  for(j=0; j<builtin.nReq; j++){
    if( builtin.aReq[j]==i ) return;  /* Already queued or sent */
  }
  if( builtin.nReq>=count(builtin.aReq) ){
    fossil_panic("too many javascript files requested");
  }
  builtin.aReq[builtin.nReq++] = i;
}

/*
** Fulfill all pending requests for javascript files.
**
** The current implementation delivers all javascript in-line.  However,
** the caller should not depend on this.  Future changes to this routine
** might choose to deliver javascript as separate resources.
*/
void builtin_fulfill_js_requests(void){
  if( builtin.nSent>=builtin.nReq ) return;  /* nothing to do */
  switch( builtin.eDelivery ){
    case JS_INLINE: {
      CX("<script nonce='%h'>\n",style_nonce());
      do{
        int i = builtin.aReq[builtin.nSent++];
        CX("/* %s */\n", aBuiltinFiles[i].zName);
        cgi_append_content((const char*)aBuiltinFiles[i].pData,
                           aBuiltinFiles[i].nByte);
      }while( builtin.nSent<builtin.nReq );
      CX("</script>\n");
      break;
    }
    case JS_BUNDLED: {
      if( builtin.nSent+1<builtin.nReq ){
        Blob aList;
        blob_init(&aList,0,0);
        while( builtin.nSent<builtin.nReq ){
          blob_appendf(&aList, ",%d", builtin.aReq[builtin.nSent++]+1);
        }
        CX("<script src='%R/builtin?m=%s&id=%.8s'></script>\n",
           blob_str(&aList)+1, fossil_exe_id());
        blob_reset(&aList);
        break;
      }
      /* If there is only one JS file, fall through into the
      ** JS_SEPARATE case below. */
      /*FALLTHROUGH*/
    }
    case JS_SEPARATE: {
      /* Each JS file as a separate resource */
      while( builtin.nSent<builtin.nReq ){
        int i = builtin.aReq[builtin.nSent++];
        CX("<script src='%R/builtin?name=%t&id=%.8s'></script>\n",
              aBuiltinFiles[i].zName, fossil_exe_id());
      }
      break;
    }
  }
}

/*****************************************************************************
** A virtual table for accessing the information in aBuiltinFiles[].
*/

/* builtinVtab_vtab is a subclass of sqlite3_vtab which is
** underlying representation of the virtual table
*/
typedef struct builtinVtab_vtab builtinVtab_vtab;
struct builtinVtab_vtab {
  sqlite3_vtab base;  /* Base class - must be first */
  /* Add new fields here, as necessary */
};

/* builtinVtab_cursor is a subclass of sqlite3_vtab_cursor which will
** serve as the underlying representation of a cursor that scans
** over rows of the result
*/
typedef struct builtinVtab_cursor builtinVtab_cursor;
struct builtinVtab_cursor {
  sqlite3_vtab_cursor base;  /* Base class - must be first */
  /* Insert new fields here.  For this builtinVtab we only keep track
  ** of the rowid */
  sqlite3_int64 iRowid;      /* The rowid */
};

/*
** The builtinVtabConnect() method is invoked to create a new
** builtin virtual table.
**
** Think of this routine as the constructor for builtinVtab_vtab objects.
**
** All this routine needs to do is:
**
**    (1) Allocate the builtinVtab_vtab object and initialize all fields.
**
**    (2) Tell SQLite (via the sqlite3_declare_vtab() interface) what the
**        result set of queries against the virtual table will look like.
*/
static int builtinVtabConnect(
  sqlite3 *db,
  void *pAux,
  int argc, const char *const*argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  builtinVtab_vtab *pNew;
  int rc;

  rc = sqlite3_declare_vtab(db,
           "CREATE TABLE x(name,size,data)"
       );
  if( rc==SQLITE_OK ){
    pNew = sqlite3_malloc( sizeof(*pNew) );
    *ppVtab = (sqlite3_vtab*)pNew;
    if( pNew==0 ) return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
  }
  return rc;
}

/*
** This method is the destructor for builtinVtab_vtab objects.
*/
static int builtinVtabDisconnect(sqlite3_vtab *pVtab){
  builtinVtab_vtab *p = (builtinVtab_vtab*)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

/*
** Constructor for a new builtinVtab_cursor object.
*/
static int builtinVtabOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor){
  builtinVtab_cursor *pCur;
  pCur = sqlite3_malloc( sizeof(*pCur) );
  if( pCur==0 ) return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

/*
** Destructor for a builtinVtab_cursor.
*/
static int builtinVtabClose(sqlite3_vtab_cursor *cur){
  builtinVtab_cursor *pCur = (builtinVtab_cursor*)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}


/*
** Advance a builtinVtab_cursor to its next row of output.
*/
static int builtinVtabNext(sqlite3_vtab_cursor *cur){
  builtinVtab_cursor *pCur = (builtinVtab_cursor*)cur;
  pCur->iRowid++;
  return SQLITE_OK;
}

/*
** Return values of columns for the row at which the builtinVtab_cursor
** is currently pointing.
*/
static int builtinVtabColumn(
  sqlite3_vtab_cursor *cur,   /* The cursor */
  sqlite3_context *ctx,       /* First argument to sqlite3_result_...() */
  int i                       /* Which column to return */
){
  builtinVtab_cursor *pCur = (builtinVtab_cursor*)cur;
  const struct BuiltinFileTable *pFile = aBuiltinFiles + pCur->iRowid;
  switch( i ){
    case 0:  /* name */
      sqlite3_result_text(ctx, pFile->zName, -1, SQLITE_STATIC);
      break;
    case 1:  /* size */
      sqlite3_result_int(ctx, pFile->nByte);
      break;
    case 2:  /* data */
      sqlite3_result_blob(ctx, pFile->pData, pFile->nByte, SQLITE_STATIC);
      break;
  }
  return SQLITE_OK;
}

/*
** Return the rowid for the current row.  In this implementation, the
** rowid is the same as the output value.
*/
static int builtinVtabRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  builtinVtab_cursor *pCur = (builtinVtab_cursor*)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

/*
** Return TRUE if the cursor has been moved off of the last
** row of output.
*/
static int builtinVtabEof(sqlite3_vtab_cursor *cur){
  builtinVtab_cursor *pCur = (builtinVtab_cursor*)cur;
  return pCur->iRowid>=count(aBuiltinFiles);
}

/*
** This method is called to "rewind" the builtinVtab_cursor object back
** to the first row of output.  This method is always called at least
** once prior to any call to builtinVtabColumn() or builtinVtabRowid() or 
** builtinVtabEof().
*/
static int builtinVtabFilter(
  sqlite3_vtab_cursor *pVtabCursor, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  builtinVtab_cursor *pCur = (builtinVtab_cursor *)pVtabCursor;
  pCur->iRowid = 1;
  return SQLITE_OK;
}

/*
** SQLite will invoke this method one or more times while planning a query
** that uses the virtual table.  This routine needs to create
** a query plan for each invocation and compute an estimated cost for that
** plan.
*/
static int builtinVtabBestIndex(
  sqlite3_vtab *tab,
  sqlite3_index_info *pIdxInfo
){
  pIdxInfo->estimatedCost = (double)count(aBuiltinFiles);
  pIdxInfo->estimatedRows = count(aBuiltinFiles);
  return SQLITE_OK;
}

/*
** This following structure defines all the methods for the 
** virtual table.
*/
static sqlite3_module builtinVtabModule = {
  /* iVersion    */ 0,
  /* xCreate     */ 0,  /* The builtin vtab is eponymous and read-only */
  /* xConnect    */ builtinVtabConnect,
  /* xBestIndex  */ builtinVtabBestIndex,
  /* xDisconnect */ builtinVtabDisconnect,
  /* xDestroy    */ 0,
  /* xOpen       */ builtinVtabOpen,
  /* xClose      */ builtinVtabClose,
  /* xFilter     */ builtinVtabFilter,
  /* xNext       */ builtinVtabNext,
  /* xEof        */ builtinVtabEof,
  /* xColumn     */ builtinVtabColumn,
  /* xRowid      */ builtinVtabRowid,
  /* xUpdate     */ 0,
  /* xBegin      */ 0,
  /* xSync       */ 0,
  /* xCommit     */ 0,
  /* xRollback   */ 0,
  /* xFindMethod */ 0,
  /* xRename     */ 0,
  /* xSavepoint  */ 0,
  /* xRelease    */ 0,
  /* xRollbackTo */ 0,
  /* xShadowName */ 0
};


/*
** Register the builtin virtual table
*/
int builtin_vtab_register(sqlite3 *db){
  int rc = sqlite3_create_module(db, "builtin", &builtinVtabModule, 0);
  return rc;
}
/* End of the builtin virtual table
******************************************************************************/
