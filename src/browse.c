/*
** Copyright (c) 2008 D. Richard Hipp
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
** This file contains code to implement the file browser web interface.
*/
#include "config.h"
#include "browse.h"
#include <assert.h>

/*
** This is the implementation of the "pathelement(X,N)" SQL function.
**
** If X is a unix-like pathname (with "/" separators) and N is an
** integer, then skip the initial N characters of X and return the
** name of the path component that begins on the N+1th character
** (numbered from 0).  If the path component is a directory (if
** it is followed by other path components) then prepend "/".
**
** Examples:
**
**      pathelement('abc/pqr/xyz', 4)  ->  '/pqr'
**      pathelement('abc/pqr', 4)      ->  'pqr'
**      pathelement('abc/pqr/xyz', 0)  ->  '/abc'
*/
void pathelementFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *z;
  int len, n, i;
  char *zOut;

  assert( argc==2 );
  z = sqlite3_value_text(argv[0]);
  if( z==0 ) return;
  len = sqlite3_value_bytes(argv[0]);
  n = sqlite3_value_int(argv[1]);
  if( len<=n ) return;
  if( n>0 && z[n-1]!='/' ) return;
  for(i=n; i<len && z[i]!='/'; i++){}
  if( i==len ){
    sqlite3_result_text(context, (char*)&z[n], len-n, SQLITE_TRANSIENT);
  }else{
    zOut = sqlite3_mprintf("/%.*s", i-n, &z[n]);
    sqlite3_result_text(context, zOut, i-n+1, sqlite3_free);
  }
}

/*
** Flag arguments for hyperlinked_path()
*/
#if INTERFACE
# define LINKPATH_FINFO  0x0001       /* Link final term to /finfo */
# define LINKPATH_FILE   0x0002       /* Link final term to /file */
#endif

/*
** Given a pathname which is a relative path from the root of
** the repository to a file or directory, compute a string which
** is an HTML rendering of that path with hyperlinks on each
** directory component of the path where the hyperlink redirects
** to the "dir" page for the directory.
**
** There is no hyperlink on the file element of the path.
**
** The computed string is appended to the pOut blob.  pOut should
** have already been initialized.
*/
void hyperlinked_path(
  const char *zPath,    /* Path to render */
  Blob *pOut,           /* Write into this blob */
  const char *zCI,      /* check-in name, or NULL */
  const char *zURI,     /* "dir" or "tree" */
  const char *zREx,     /* Extra query parameters */
  unsigned int mFlags   /* Extra flags */
){
  int i, j;
  char *zSep = "";

  for(i=0; zPath[i]; i=j){
    for(j=i; zPath[j] && zPath[j]!='/'; j++){}
    if( zPath[j]==0 ){
      if( mFlags & LINKPATH_FILE ){
        zURI = "file";
      }else if( mFlags & LINKPATH_FINFO ){
        zURI = "finfo";
      }else{
        blob_appendf(pOut, "/%h", zPath+i);
        break;
      }
    }
    if( zCI ){
      char *zLink = href("%R/%s?name=%#T%s&ci=%T", zURI, j, zPath, zREx,zCI);
      blob_appendf(pOut, "%s%z%#h</a>",
                   zSep, zLink, j-i, &zPath[i]);
    }else{
      char *zLink = href("%R/%s?name=%#T%s", zURI, j, zPath, zREx);
      blob_appendf(pOut, "%s%z%#h</a>",
                   zSep, zLink, j-i, &zPath[i]);
    }
    zSep = "/";
    while( zPath[j]=='/' ){ j++; }
  }
}

/*
** WEBPAGE: docdir
**
** Show the files and subdirectories within a single directory of the
** source tree.  This works similarly to /dir but with the following
** differences:
**
**    *   Links to files go to /doc (showing the file content directly,
**        depending on mimetype) rather than to /file (which always shows
**        the file embedded in a standard Fossil page frame).
**
**    *   The submenu and the page title is now show.  The page is plain.
**
** The /docdir page is a shorthand for /dir with the "dx" query parameter.
**
** Query parameters:
**
**    ci=LABEL         Show only files in this check-in.  If omitted, the
**                     "trunk" directory is used.
**    name=PATH        Directory to display.  Optional.  Top-level if missing
**    re=REGEXP        Show only files matching REGEXP
**    noreadme         Do not attempt to display the README file.
**    dx               File links to go to /doc instead of /file or /finfo.
*/
void page_docdir(void){ page_dir(); }

/*
** WEBPAGE: dir
**
** Show the files and subdirectories within a single directory of the
** source tree.  Only files for a single check-in are shown if the ci=
** query parameter is present.  If ci= is missing, the union of files
** across all check-ins is shown.
**
** Query parameters:
**
**    ci=LABEL         Show only files in this check-in.  Optional.
**    name=PATH        Directory to display.  Optional.  Top-level if missing
**    re=REGEXP        Show only files matching REGEXP
**    type=TYPE        TYPE=flat: use this display
**                     TYPE=tree: use the /tree display instead
**    noreadme         Do not attempt to display the README file.
**    dx               Behave like /docdir
*/
void page_dir(void){
  char *zD = fossil_strdup(P("name"));
  int nD = zD ? strlen(zD)+1 : 0;
  int mxLen;
  char *zPrefix;
  Stmt q;
  const char *zCI = P("ci");
  int rid = 0;
  char *zUuid = 0;
  Manifest *pM = 0;
  const char *zSubdirLink;
  int linkTrunk = 1;
  int linkTip = 1;
  HQuery sURI;
  int isSymbolicCI = 0;   /* ci= is symbolic name, not a hash prefix */
  int isBranchCI = 0;     /* True if ci= refers to a branch name */
  char *zHeader = 0;
  const char *zRegexp;    /* The re= query parameter */
  char *zMatch;           /* Extra title text describing the match */
  int bDocDir = PB("dx") || strncmp(g.zPath, "docdir", 6)==0;

  if( zCI && strlen(zCI)==0 ){ zCI = 0; }
  if( strcmp(PD("type","flat"),"tree")==0 ){ page_tree(); return; }
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  while( nD>1 && zD[nD-2]=='/' ){ zD[(--nD)-1] = 0; }

  /* If the name= parameter is an empty string, make it a NULL pointer */
  if( zD && strlen(zD)==0 ){ zD = 0; }

  /* If a specific check-in is requested, fetch and parse it.  If the
  ** specific check-in does not exist, clear zCI.  zCI==0 will cause all
  ** files from all check-ins to be displayed.
  */
  if( bDocDir && zCI==0 ) zCI = "trunk";
  if( zCI ){
    pM = manifest_get_by_name(zCI, &rid);
    if( pM ){
      int trunkRid = symbolic_name_to_rid("tag:trunk", "ci");
      linkTrunk = trunkRid && rid != trunkRid;
      linkTip = rid != symbolic_name_to_rid("tip", "ci");
      zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      isSymbolicCI = (sqlite3_strnicmp(zUuid, zCI, strlen(zCI))!=0);
      isBranchCI = branch_includes_uuid(zCI, zUuid);
      if( bDocDir ) zCI = mprintf("%S", zUuid);
      Th_StoreUnsafe("current_checkin", zCI);
    }else{
      zCI = 0;
    }
  }

  assert( isSymbolicCI==0 || (zCI!=0 && zCI[0]!=0) );
  if( zD==0 ){
    if( zCI ){
      zHeader = mprintf("Top-level Files of %s", zCI);
    }else{
      zHeader = mprintf("All Top-level Files");
    }
  }else{
    if( zCI ){
      zHeader = mprintf("Files in %s/ of %s", zD, zCI);
    }else{
      zHeader = mprintf("All Files in %s/", zD);
    }
  }
  zRegexp = P("re");
  if( zRegexp ){
    zHeader = mprintf("%z matching \"%s\"", zHeader, zRegexp);
    zMatch = mprintf(" matching \"%h\"", zRegexp);
  }else{
    zMatch = "";
  }
  style_header("%s", zHeader);
  fossil_free(zHeader);
  style_adunit_config(ADUNIT_RIGHT_OK);
  sqlite3_create_function(g.db, "pathelement", 2, SQLITE_UTF8, 0,
                          pathelementFunc, 0, 0);
  url_initialize(&sURI, "dir");
  cgi_check_for_malice();
  cgi_query_parameters_to_url(&sURI);

  /* Compute the title of the page */
  if( bDocDir ){
    zPrefix = zD ? mprintf("%s/",zD) : "";
  }else if( zD ){
    Blob dirname;
    blob_init(&dirname, 0, 0);
    hyperlinked_path(zD, &dirname, zCI, "dir", "", 0);
    @ <h2>Files in directory %s(blob_str(&dirname)) \
    blob_reset(&dirname);
    zPrefix = mprintf("%s/", zD);
    style_submenu_element("Top-Level", "%s",
                          url_render(&sURI, "name", 0, 0, 0));
  }else{
    @ <h2>Files in the top-level directory \
    zPrefix = "";
  }
  if( zCI ){
    if( bDocDir ){
      /* No header for /docdir.  Just give the list of files. */
    }else if( fossil_strcmp(zCI,"tip")==0 ){
      @ from the %z(href("%R/info?name=%T",zCI))latest check-in</a>\
      @ %s(zMatch)</h2>
    }else if( isBranchCI ){
      @ from the %z(href("%R/info?name=%T",zCI))latest check-in</a> \
      @ of branch %z(href("%R/timeline?r=%T",zCI))%h(zCI)</a>\
      @ %s(zMatch)</h2>
    }else {
      @ of check-in %z(href("%R/info?name=%T",zCI))%h(zCI)</a>\
      @ %s(zMatch)</h2>
    }
    if( bDocDir ){
      zSubdirLink = mprintf("%R/docdir?ci=%T&name=%T", zCI, zPrefix);
    }else{
      zSubdirLink = mprintf("%R/dir?ci=%T&name=%T", zCI, zPrefix);
    }
    if( nD==0 && !bDocDir ){
      style_submenu_element("File Ages", "%R/fileage?name=%T", zCI);
    }
  }else{
    @ in any check-in</h2>
    zSubdirLink = mprintf("%R/dir?name=%T", zPrefix);
  }
  if( linkTrunk && !bDocDir ){
    style_submenu_element("Trunk", "%s",
                          url_render(&sURI, "ci", "trunk", 0, 0));
  }
  if( linkTip && !bDocDir ){
    style_submenu_element("Tip", "%s", url_render(&sURI, "ci", "tip", 0, 0));
  }
  if( zD && !bDocDir ){
    style_submenu_element("History","%R/timeline?chng=%T/*", zD);
  }
  if( !bDocDir ){
    style_submenu_element("All", "%s", url_render(&sURI, "ci", 0, 0, 0));
    style_submenu_element("Tree-View", "%s",
                          url_render(&sURI, "type", "tree", 0, 0));
  }

  /* Compute the temporary table "localfiles" containing the names
  ** of all files and subdirectories in the zD[] directory.
  **
  ** Subdirectory names begin with "/".  This causes them to sort
  ** first and it also gives us an easy way to distinguish files
  ** from directories in the loop that follows.
  */
  db_multi_exec(
     "CREATE TEMP TABLE localfiles(x UNIQUE NOT NULL, u);"
  );
  if( zCI ){
    /* Files in the specific checked given by zCI */
    if( zD ){
      db_multi_exec(
        "INSERT OR IGNORE INTO localfiles"
        " SELECT pathelement(filename,%d), uuid"
        "   FROM files_of_checkin(%Q)"
        "  WHERE filename GLOB '%q/*'",
        nD, zCI, zD
      );
    }else{
      db_multi_exec(
        "INSERT OR IGNORE INTO localfiles"
        " SELECT pathelement(filename,%d), uuid"
        "   FROM files_of_checkin(%Q)",
        nD, zCI
      );
    }
  }else{
    /* All files across all check-ins */
    if( zD ){
      db_multi_exec(
        "INSERT OR IGNORE INTO localfiles"
        " SELECT pathelement(name,%d), NULL FROM filename"
        "  WHERE name GLOB '%q/*'",
        nD, zD
      );
    }else{
      db_multi_exec(
        "INSERT OR IGNORE INTO localfiles"
        " SELECT pathelement(name,0), NULL FROM filename"
      );
    }
  }

  /* If the re=REGEXP query parameter is present, filter out names that
  ** do not match the pattern */
  if( zRegexp ){
    db_multi_exec(
      "DELETE FROM localfiles WHERE x NOT REGEXP %Q", zRegexp
    );
  }

  /* Generate a multi-column table listing the contents of zD[]
  ** directory.
  */
  mxLen = db_int(12, "SELECT max(length(x)) FROM localfiles /*scan*/");
  if( mxLen<12 ) mxLen = 12;
  mxLen += (mxLen+9)/10;
  db_prepare(&q,
     "SELECT x, u FROM localfiles ORDER BY x COLLATE uintnocase /*scan*/");
  @ <div class="columns files" style="columns: %d(mxLen)ex auto">
  @ <ul class="browser">
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFN;
    zFN = db_column_text(&q, 0);
    if( zFN[0]=='/' ){
      zFN++;
      @ <li class="dir">%z(href("%s%T",zSubdirLink,zFN))%h(zFN)</a></li>
    }else{
      const char *zLink;
      if( bDocDir ){
        zLink = href("%R/doc/%T/%T%T", zCI, zPrefix, zFN);
      }else if( zCI ){
        zLink = href("%R/file?name=%T%T&ci=%T",zPrefix,zFN,zCI);
      }else{
        zLink = href("%R/finfo?name=%T%T",zPrefix,zFN);
      }
      @ <li class="%z(fileext_class(zFN))">%z(zLink)%h(zFN)</a></li>
    }
  }
  db_finalize(&q);
  manifest_destroy(pM);
  @ </ul></div>

  /* If the "noreadme" query parameter is present, do not try to
  ** show the content of the README file.
  */
  if( P("noreadme")!=0 ){
    style_finish_page();
    return;
  }

  /* If the directory contains a readme file, then display its content below
  ** the list of files
  */
  db_prepare(&q,
    "SELECT x, u FROM localfiles"
    " WHERE x COLLATE nocase IN"
    " ('readme','readme.txt','readme.md','readme.wiki','readme.markdown',"
    " 'readme.html') ORDER BY x COLLATE uintnocase LIMIT 1;"
  );
  if( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q,0);
    const char *zUuid = db_column_text(&q,1);
    if( zUuid ){
      rid = fast_uuid_to_rid(zUuid);
    }else{
      if( zD ){
        rid = db_int(0,
           "SELECT fid FROM filename, mlink, event"
           " WHERE name='%q/%q'"
           "   AND mlink.fnid=filename.fnid"
           "   AND event.objid=mlink.mid"
           " ORDER BY event.mtime DESC LIMIT 1",
           zD, zName
        );
      }else{
        rid = db_int(0,
           "SELECT fid FROM filename, mlink, event"
           " WHERE name='%q'"
           "   AND mlink.fnid=filename.fnid"
           "   AND event.objid=mlink.mid"
           " ORDER BY event.mtime DESC LIMIT 1",
           zName
        );
      }
    }
    if( rid ){
      @ <hr>
      if( sqlite3_strlike("readme.html", zName, 0)==0 ){
        if( zUuid==0 ){
          zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
        }
        @ <iframe src="%R/raw/%s(zUuid)"
        @ width="100%%" frameborder="0" marginwidth="0" marginheight="0"
        @ sandbox="allow-same-origin"
        @ onload="this.height=this.contentDocument.documentElement.scrollHeight;">
        @ </iframe>
      }else{
        Blob content;
        const char *zMime = mimetype_from_name(zName);
        content_get(rid, &content);
        safe_html_context(DOCSRC_FILE);
        wiki_render_by_mimetype(&content, zMime);
        document_emit_js();
      }
    }
  }
  db_finalize(&q);
  style_finish_page();
}

/*
** Objects used by the "tree" webpage.
*/
typedef struct FileTreeNode FileTreeNode;
typedef struct FileTree FileTree;

/*
** A single line of the file hierarchy
*/
struct FileTreeNode {
  FileTreeNode *pNext;      /* Next entry in an ordered list of them all */
  FileTreeNode *pParent;    /* Directory containing this entry */
  FileTreeNode *pSibling;   /* Next element in the same subdirectory */
  FileTreeNode *pChild;     /* List of child nodes */
  FileTreeNode *pLastChild; /* Last child on the pChild list */
  char *zName;              /* Name of this entry.  The "tail" */
  char *zFullName;          /* Full pathname of this entry */
  char *zUuid;              /* Artifact hash of this file.  May be NULL. */
  double mtime;             /* Modification time for this entry */
  double sortBy;            /* Either mtime or size, depending on desired
                               sort order */
  int iSize;                /* Size for this entry */
  unsigned nFullName;       /* Length of zFullName */
  unsigned iLevel;          /* Levels of parent directories */
};

/*
** A complete file hierarchy
*/
struct FileTree {
  FileTreeNode *pFirst;     /* First line of the list */
  FileTreeNode *pLast;      /* Last line of the list */
  FileTreeNode *pLastTop;   /* Last top-level node */
};

/*
** Add one or more new FileTreeNodes to the FileTree object so that the
** leaf object zPathname is at the end of the node list.
**
** The caller invokes this routine once for each leaf node (each file
** as opposed to each directory).  This routine fills in any missing
** intermediate nodes automatically.
**
** When constructing a list of FileTreeNodes, all entries that have
** a common directory prefix must be added consecutively in order for
** the tree to be constructed properly.
*/
static void tree_add_node(
  FileTree *pTree,         /* Tree into which nodes are added */
  const char *zPath,       /* The full pathname of file to add */
  const char *zUuid,       /* Hash of the file.  Might be NULL. */
  double mtime,            /* Modification time for this entry */
  int size,                /* Size for this entry */
  int sortOrder            /* 0: filename, 1: mtime, 2: size */
){
  int i;
  FileTreeNode *pParent;   /* Parent (directory) of the next node to insert */

  /* Make pParent point to the most recent ancestor of zPath, or
  ** NULL if there are no prior entires that are a container for zPath.
  */
  pParent = pTree->pLast;
  while( pParent!=0 &&
      ( strncmp(pParent->zFullName, zPath, pParent->nFullName)!=0
        || zPath[pParent->nFullName]!='/' )
  ){
    pParent = pParent->pParent;
  }
  i = pParent ? pParent->nFullName+1 : 0;
  while( zPath[i] ){
    FileTreeNode *pNew;
    int iStart = i;
    int nByte;
    while( zPath[i] && zPath[i]!='/' ){ i++; }
    nByte = sizeof(*pNew) + i + 1;
    if( zUuid!=0 && zPath[i]==0 ) nByte += HNAME_MAX+1;
    pNew = fossil_malloc( nByte );
    memset(pNew, 0, sizeof(*pNew));
    pNew->zFullName = (char*)&pNew[1];
    memcpy(pNew->zFullName, zPath, i);
    pNew->zFullName[i] = 0;
    pNew->nFullName = i;
    if( zUuid!=0 && zPath[i]==0 ){
      pNew->zUuid = pNew->zFullName + i + 1;
      memcpy(pNew->zUuid, zUuid, strlen(zUuid)+1);
    }
    pNew->zName = pNew->zFullName + iStart;
    if( pTree->pLast ){
      pTree->pLast->pNext = pNew;
    }else{
      pTree->pFirst = pNew;
    }
    pTree->pLast = pNew;
    pNew->pParent = pParent;
    if( pParent ){
      if( pParent->pChild ){
        pParent->pLastChild->pSibling = pNew;
      }else{
        pParent->pChild = pNew;
      }
      pNew->iLevel = pParent->iLevel + 1;
      pParent->pLastChild = pNew;
    }else{
      if( pTree->pLastTop ) pTree->pLastTop->pSibling = pNew;
      pTree->pLastTop = pNew;
    }
    pNew->mtime = mtime;
    pNew->iSize = size;
    if( sortOrder ){
      pNew->sortBy = sortOrder==1 ? mtime : (double)size;
    }else{
      pNew->sortBy = 0.0;
    }
    while( zPath[i]=='/' ){ i++; }
    pParent = pNew;
  }
  while( pParent && pParent->pParent ){
    if( pParent->pParent->mtime < pParent->mtime ){
      pParent->pParent->mtime = pParent->mtime;
    }
    pParent = pParent->pParent;
  }
}

/* Comparison function for two FileTreeNode objects.  Sort first by
** sortBy (larger numbers first) and then by zName (smaller names first).
**
** The sortBy field will be the same as mtime in order to sort by time,
** or the same as iSize to sort by file size.
**
** Return negative if pLeft<pRight.
** Return positive if pLeft>pRight.
** Return zero if pLeft==pRight.
*/
static int compareNodes(FileTreeNode *pLeft, FileTreeNode *pRight){
  if( pLeft->sortBy>pRight->sortBy ) return -1;
  if( pLeft->sortBy<pRight->sortBy ) return +1;
  return fossil_stricmp(pLeft->zName, pRight->zName);
}

/* Merge together two sorted lists of FileTreeNode objects */
static FileTreeNode *mergeNodes(FileTreeNode *pLeft,  FileTreeNode *pRight){
  FileTreeNode *pEnd;
  FileTreeNode base;
  pEnd = &base;
  while( pLeft && pRight ){
    if( compareNodes(pLeft,pRight)<=0 ){
      pEnd = pEnd->pSibling = pLeft;
      pLeft = pLeft->pSibling;
    }else{
      pEnd = pEnd->pSibling = pRight;
      pRight = pRight->pSibling;
    }
  }
  if( pLeft ){
    pEnd->pSibling = pLeft;
  }else{
    pEnd->pSibling = pRight;
  }
  return base.pSibling;
}

/* Sort a list of FileTreeNode objects in sortmtime order. */
static FileTreeNode *sortNodes(FileTreeNode *p){
  FileTreeNode *a[30];
  FileTreeNode *pX;
  int i;

  memset(a, 0, sizeof(a));
  while( p ){
    pX = p;
    p = pX->pSibling;
    pX->pSibling = 0;
    for(i=0; i<count(a)-1 && a[i]!=0; i++){
      pX = mergeNodes(a[i], pX);
      a[i] = 0;
    }
    a[i] = mergeNodes(a[i], pX);
  }
  pX = 0;
  for(i=0; i<count(a); i++){
    pX = mergeNodes(a[i], pX);
  }
  return pX;
}

/* Sort an entire FileTreeNode tree by mtime
**
** This routine invalidates the following fields:
**
**     FileTreeNode.pLastChild
**     FileTreeNode.pNext
**
** Use relinkTree to reconnect the pNext pointers.
*/
static FileTreeNode *sortTree(FileTreeNode *p){
  FileTreeNode *pX;
  for(pX=p; pX; pX=pX->pSibling){
    if( pX->pChild ) pX->pChild = sortTree(pX->pChild);
  }
  return sortNodes(p);
}

/* Reconstruct the FileTree by reconnecting the FileTreeNode.pNext
** fields in sequential order.
*/
static void relinkTree(FileTree *pTree, FileTreeNode *pRoot){
  while( pRoot ){
    if( pTree->pLast ){
      pTree->pLast->pNext = pRoot;
    }else{
      pTree->pFirst = pRoot;
    }
    pTree->pLast = pRoot;
    if( pRoot->pChild ) relinkTree(pTree, pRoot->pChild);
    pRoot = pRoot->pSibling;
  }
  if( pTree->pLast ) pTree->pLast->pNext = 0;
}


/*
** WEBPAGE: tree
**
** Show the files using a tree-view.  If the ci= query parameter is present
** then show only the files for the check-in identified.  If ci= is omitted,
** then show the union of files over all check-ins.
**
** The type=tree query parameter is required or else the /dir format is
** used.
**
** Query parameters:
**
**    type=tree        Required to prevent use of /dir format
**    name=PATH        Directory to display.  Optional
**    ci=LABEL         Show only files in this check-in.  Optional.
**    re=REGEXP        Show only files matching REGEXP.  Optional.
**    expand           Begin with the tree fully expanded.
**    nofiles          Show directories (folders) only.  Omit files.
**    sort             0: by filename, 1: by mtime, 2: by size
*/
void page_tree(void){
  char *zD = fossil_strdup(P("name"));
  int nD = zD ? strlen(zD)+1 : 0;
  const char *zCI = P("ci");
  int rid = 0;
  char *zUuid = 0;
  Blob dirname;
  Manifest *pM = 0;
  double rNow = 0;
  char *zNow = 0;
  int useMtime = atoi(PD("mtime","0"));
  int sortOrder = atoi(PD("sort",useMtime?"1":"0"));
  int linkTrunk = 1;       /* include link to "trunk" */
  int linkTip = 1;         /* include link to "tip" */
  const char *zRE;         /* the value for the re=REGEXP query parameter */
  const char *zObjType;    /* "files" by default or "folders" for "nofiles" */
  char *zREx = "";         /* Extra parameters for path hyperlinks */
  ReCompiled *pRE = 0;     /* Compiled regular expression */
  FileTreeNode *p;         /* One line of the tree */
  FileTree sTree;          /* The complete tree of files */
  HQuery sURI;             /* Hyperlink */
  int startExpanded;       /* True to start out with the tree expanded */
  int showDirOnly;         /* Show directories only.  Omit files */
  int nDir = 0;            /* Number of directories. Used for ID attributes */
  char *zProjectName = db_get("project-name", 0);
  int isSymbolicCI = 0;    /* ci= is a symbolic name, not a hash prefix */
  int isBranchCI = 0;      /* ci= refers to a branch name */
  char *zHeader = 0;

  if( zCI && strlen(zCI)==0 ){ zCI = 0; }
  if( strcmp(PD("type","flat"),"flat")==0 ){ page_dir(); return; }
  memset(&sTree, 0, sizeof(sTree));
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  while( nD>1 && zD[nD-2]=='/' ){ zD[(--nD)-1] = 0; }
  sqlite3_create_function(g.db, "pathelement", 2, SQLITE_UTF8, 0,
                          pathelementFunc, 0, 0);
  url_initialize(&sURI, "tree");
  cgi_query_parameters_to_url(&sURI);
  if( PB("nofiles") ){
    showDirOnly = 1;
  }else{
    showDirOnly = 0;
  }
  style_adunit_config(ADUNIT_RIGHT_OK);
  if( PB("expand") ){
    startExpanded = 1;
  }else{
    startExpanded = 0;
  }

  /* If a regular expression is specified, compile it */
  zRE = P("re");
  if( zRE ){
    re_compile(&pRE, zRE, 0);
    zREx = mprintf("&re=%T", zRE);
  }
  cgi_check_for_malice();

  /* If the name= parameter is an empty string, make it a NULL pointer */
  if( zD && strlen(zD)==0 ){ zD = 0; }

  /* If a specific check-in is requested, fetch and parse it.  If the
  ** specific check-in does not exist, clear zCI.  zCI==0 will cause all
  ** files from all check-ins to be displayed.
  */
  if( zCI ){
    pM = manifest_get_by_name(zCI, &rid);
    if( pM ){
      int trunkRid = symbolic_name_to_rid("tag:trunk", "ci");
      linkTrunk = trunkRid && rid != trunkRid;
      linkTip = rid != symbolic_name_to_rid("tip", "ci");
      zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
      rNow = db_double(0.0, "SELECT mtime FROM event WHERE objid=%d", rid);
      zNow = db_text("", "SELECT datetime(mtime,toLocal())"
                         " FROM event WHERE objid=%d", rid);
      isSymbolicCI = (sqlite3_strnicmp(zUuid, zCI, strlen(zCI)) != 0);
      isBranchCI = branch_includes_uuid(zCI, zUuid);
      Th_StoreUnsafe("current_checkin", zCI);
    }else{
      zCI = 0;
    }
  }
  if( zCI==0 ){
    rNow = db_double(0.0, "SELECT max(mtime) FROM event");
    zNow = db_text("", "SELECT datetime(max(mtime),toLocal()) FROM event");
  }

  assert( isSymbolicCI==0 || (zCI!=0 && zCI[0]!=0) );
  if( zD==0 ){
    if( zCI ){
      zHeader = mprintf("Top-level Files of %s", zCI);
    }else{
      zHeader = mprintf("All Top-level Files");
    }
  }else{
    if( zCI ){
      zHeader = mprintf("Files in %s/ of %s", zD, zCI);
    }else{
      zHeader = mprintf("All Files in %s/", zD);
    }
  }
  style_header("%s", zHeader);
  fossil_free(zHeader);

  /* Compute the title of the page */
  blob_zero(&dirname);
  if( zD ){
    blob_append(&dirname, "within directory ", -1);
    hyperlinked_path(zD, &dirname, zCI, "tree", zREx, 0);
    if( zRE ) blob_appendf(&dirname, " matching \"%s\"", zRE);
    style_submenu_element("Top-Level", "%s",
                          url_render(&sURI, "name", 0, 0, 0));
  }else if( zRE ){
    blob_appendf(&dirname, "matching \"%s\"", zRE);
  }
  {
    static const char *const sort_orders[] = {
       "0", "Sort By Filename",
       "1", "Sort By Age",
       "2", "Sort By Size"
    };
    style_submenu_multichoice("sort", 3, sort_orders, 0);
  }
  if( zCI ){
    style_submenu_element("All", "%s", url_render(&sURI, "ci", 0, 0, 0));
    if( nD==0 && !showDirOnly ){
      style_submenu_element("File Ages", "%R/fileage?name=%T", zCI);
    }
  }
  if( linkTrunk ){
    style_submenu_element("Trunk", "%s",
                          url_render(&sURI, "ci", "trunk", 0, 0));
  }
  if( linkTip ){
    style_submenu_element("Tip", "%s", url_render(&sURI, "ci", "tip", 0, 0));
  }
  style_submenu_element("Flat-View", "%s",
                        url_render(&sURI, "type", "flat", 0, 0));

  /* Compute the file hierarchy.
  */
  if( zCI ){
    Stmt q;
    compute_fileage(rid, 0);
    db_prepare(&q,
       "SELECT filename.name, blob.uuid, blob.size, fileage.mtime\n"
       "  FROM fileage, filename, blob\n"
       " WHERE filename.fnid=fileage.fnid\n"
       "   AND blob.rid=fileage.fid\n"
       " ORDER BY filename.name COLLATE uintnocase;"
    );
    while( db_step(&q)==SQLITE_ROW ){
      const char *zFile = db_column_text(&q,0);
      const char *zUuid = db_column_text(&q,1);
      int size = db_column_int(&q,2);
      double mtime = db_column_double(&q,3);
      if( nD>0 && (fossil_strncmp(zFile, zD, nD-1)!=0 || zFile[nD-1]!='/') ){
        continue;
      }
      if( pRE && re_match(pRE, (const unsigned char*)zFile, -1)==0 ) continue;
      tree_add_node(&sTree, zFile, zUuid, mtime, size, sortOrder);
    }
    db_finalize(&q);
  }else{
    Stmt q;
    db_prepare(&q,
      "WITH mx(fnid,fid,mtime) AS (\n"
      "  SELECT fnid, fid, max(event.mtime)\n"
      "    FROM mlink, event\n"
      "   WHERE event.objid=mlink.mid\n"
      "   GROUP BY 1\n"
      ")\n"
      "SELECT\n"
      "  filename.name,\n"
      "  blob.uuid,\n"
      "  blob.size,\n"
      "  mx.mtime\n"
      "FROM mx\n"
      " LEFT JOIN filename ON filename.fnid=mx.fnid\n"
      " LEFT JOIN blob ON blob.rid=mx.fid\n"
      " ORDER BY 1 COLLATE uintnocase;");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zName = db_column_text(&q, 0);
      const char *zUuid = db_column_text(&q,1);
      int size = db_column_int(&q,2);
      double mtime = db_column_double(&q,3);
      if( nD>0 && (fossil_strncmp(zName, zD, nD-1)!=0 || zName[nD-1]!='/') ){
        continue;
      }
      if( pRE && re_match(pRE, (const u8*)zName, -1)==0 ) continue;
      tree_add_node(&sTree, zName, zUuid, mtime, size, sortOrder);
    }
    db_finalize(&q);
  }
  style_submenu_checkbox("nofiles", "Folders Only", 0, 0);

  if( showDirOnly ){
    zObjType = "Folders";
  }else{
    zObjType = "Files";
  }

  if( zCI && strcmp(zCI,"tip")==0 ){
    @ <h2>%s(zObjType) in the %z(href("%R/info?name=tip"))latest check-in</a>
  }else if( isBranchCI ){
    @ <h2>%s(zObjType) in the %z(href("%R/info?name=%T",zCI))latest check-in\
    @ </a> for branch %z(href("%R/timeline?r=%T",zCI))%h(zCI)</a>
    if( blob_size(&dirname) ){
      @ and %s(blob_str(&dirname))
    }
  }else if( zCI ){
    @ <h2>%s(zObjType) for check-in \
    @ %z(href("%R/info?name=%T",zCI))%h(zCI)</a>
    if( blob_size(&dirname) ){
      @ and %s(blob_str(&dirname))
    }
  }else{
    int n = db_int(0, "SELECT count(*) FROM plink");
    @ <h2>%s(zObjType) from all %d(n) check-ins %s(blob_str(&dirname))
  }
  if( sortOrder==1 ){
    @ sorted by modification time</h2>
  }else if( sortOrder==2 ){
    @ sorted by size</h2>
  }else{
    @ sorted by filename</h2>
  }

  if( zNow ){
    @ <p>File ages are expressed relative to the check-in time of
    @ %z(href("%R/timeline?c=%t",zNow))%s(zNow)</a>.</p>
  }

  /* Generate tree of lists.
  **
  ** Each file and directory is a list element: <li>.  Files have class=file
  ** and if the filename as the suffix "xyz" the file also has class=file-xyz.
  ** Directories have class=dir.  The directory specfied by the name= query
  ** parameter (or the top-level directory if there is no name= query parameter)
  ** adds class=subdir.
  **
  ** The <li> element for directories also contains a sublist <ul>
  ** for the contents of that directory.
  */
  @ <div class="filetree"><ul>
  if( nD ){
    @ <li class="dir last">
  }else{
    @ <li class="dir subdir last">
  }
  @ <div class="filetreeline">
  @ %z(href("%s",url_render(&sURI,"name",0,0,0)))%h(zProjectName)</a>
  if( zNow ){
    @ <div class="filetreeage">Last Change</div>
    @ <div class="filetreesize">Size</div>
  }
  @ </div>
  @ <ul>
  if( sortOrder ){
    p = sortTree(sTree.pFirst);
    memset(&sTree, 0, sizeof(sTree));
    relinkTree(&sTree, p);
  }
  for(p=sTree.pFirst, nDir=0; p; p=p->pNext){
    const char *zLastClass = p->pSibling==0 ? " last" : "";
    if( p->pChild ){
      const char *zSubdirClass = (int)(p->nFullName)==nD-1 ? " subdir" : "";
      @ <li class="dir%s(zSubdirClass)%s(zLastClass)"><div class="filetreeline">
      @ %z(href("%s",url_render(&sURI,"name",p->zFullName,0,0)))%h(p->zName)</a>
      if( p->mtime>0.0 ){
        char *zAge = human_readable_age(rNow - p->mtime);
        @ <div class="filetreeage">%s(zAge)</div>
        @ <div class="filetreesize"></div>
      }
      @ </div>
      if( startExpanded || (int)(p->nFullName)<=nD ){
        @ <ul id="dir%d(nDir)">
      }else{
        @ <ul id="dir%d(nDir)" class="collapsed">
      }
      nDir++;
    }else if( !showDirOnly ){
      const char *zFileClass = fileext_class(p->zName);
      char *zLink;
      if( zCI ){
        zLink = href("%R/file?name=%T&ci=%T",p->zFullName,zCI);
      }else{
        zLink = href("%R/finfo?name=%T",p->zFullName);
      }
      @ <li class="%z(zFileClass)%s(zLastClass)"><div class="filetreeline">
      @ %z(zLink)%h(p->zName)</a>
      if( p->mtime>0 ){
        char *zAge = human_readable_age(rNow - p->mtime);
        @ <div class="filetreeage">%s(zAge)</div>
        @ <div class="filetreesize">%s(p->iSize ? mprintf("%,d",p->iSize) : "-")</div>
      }
      @ </div>
    }
    if( p->pSibling==0 ){
      int nClose = p->iLevel - (p->pNext ? p->pNext->iLevel : 0);
      while( nClose-- > 0 ){
        @ </ul>
      }
    }
  }
  @ </ul>
  @ </ul></div>
  builtin_request_js("tree.js");
  style_finish_page();

  /* We could free memory used by sTree here if we needed to.  But
  ** the process is about to exit, so doing so would not really accomplish
  ** anything useful. */
}

/*
** Return a CSS class name based on the given filename's extension.
** Result must be freed by the caller.
**/
const char *fileext_class(const char *zFilename){
  char *zClass;
  const char *zExt = strrchr(zFilename, '.');
  int isExt = zExt && zExt!=zFilename && zExt[1];
  int i;
  for( i=1; isExt && zExt[i]; i++ ) isExt &= fossil_isalnum(zExt[i]);
  if( isExt ){
    zClass = mprintf("file file-%s", zExt+1);
    for( i=5; zClass[i]; i++ ) zClass[i] = fossil_tolower(zClass[i]);
  }else{
    zClass = mprintf("file");
  }
  return zClass;
}

/*
** SQL used to compute the age of all files in check-in :ckin whose
** names match :glob
*/
static const char zComputeFileAgeSetup[] =
@ CREATE TABLE IF NOT EXISTS temp.fileage(
@   fnid INTEGER PRIMARY KEY,
@   fid INTEGER,
@   mid INTEGER,
@   mtime DATETIME,
@   pathname TEXT
@ );
@ CREATE VIRTUAL TABLE IF NOT EXISTS temp.foci USING files_of_checkin;
;

static const char zComputeFileAgeRun[] =
@ WITH RECURSIVE
@  ckin(x) AS (VALUES(:ckin)
@              UNION
@              SELECT plink.pid
@                FROM ckin, plink
@               WHERE plink.cid=ckin.x)
@ INSERT OR IGNORE INTO fileage(fnid, fid, mid, mtime, pathname)
@   SELECT filename.fnid, mlink.fid, mlink.mid, event.mtime, filename.name
@     FROM foci, filename, blob, mlink, event
@    WHERE foci.checkinID=:ckin
@      AND foci.filename GLOB :glob
@      AND filename.name=foci.filename
@      AND blob.uuid=foci.uuid
@      AND mlink.fid=blob.rid
@      AND mlink.fid!=mlink.pid
@      AND mlink.mid IN (SELECT x FROM ckin)
@      AND event.objid=mlink.mid
@  ORDER BY event.mtime ASC;
;

/*
** Look at all file containing in the version "vid".  Construct a
** temporary table named "fileage" that contains the file-id for each
** files, the pathname, the check-in where the file was added, and the
** mtime on that check-in. If zGlob and *zGlob then only files matching
** the given glob are computed.
*/
int compute_fileage(int vid, const char* zGlob){
  Stmt q;
  db_exec_sql(zComputeFileAgeSetup);
  db_prepare(&q, zComputeFileAgeRun  /*works-like:"constant"*/);
  db_bind_int(&q, ":ckin", vid);
  db_bind_text(&q, ":glob", zGlob && zGlob[0] ? zGlob : "*");
  db_exec(&q);
  db_finalize(&q);
  return 0;
}

/*
** Render the number of days in rAge as a more human-readable time span.
** Different units (seconds, minutes, hours, days, months, years) are
** selected depending on the magnitude of rAge.
**
** The string returned is obtained from fossil_malloc() and should be
** freed by the caller.
*/
char *human_readable_age(double rAge){
  if( rAge*86400.0<120 ){
    if( rAge*86400.0<1.0 ){
      return mprintf("current");
    }else{
      return mprintf("%d seconds", (int)(rAge*86400.0));
    }
  }else if( rAge*1440.0<90 ){
    return mprintf("%.1f minutes", rAge*1440.0);
  }else if( rAge*24.0<36 ){
    return mprintf("%.1f hours", rAge*24.0);
  }else if( rAge<365.0 ){
    return mprintf("%.1f days", rAge);
  }else{
    return mprintf("%.2f years", rAge/365.2425);
  }
}

/*
** COMMAND: test-fileage
**
** Usage: %fossil test-fileage CHECKIN
*/
void test_fileage_cmd(void){
  int mid;
  Stmt q;
  const char *zGlob = find_option("glob",0,1);
  db_find_and_open_repository(0,0);
  verify_all_options();
  if( g.argc!=3 ) usage("CHECKIN");
  mid = name_to_typed_rid(g.argv[2],"ci");
  compute_fileage(mid, zGlob);
  db_prepare(&q,
    "SELECT fid, mid, julianday('now') - mtime, pathname"
    "  FROM fileage"
  );
  while( db_step(&q)==SQLITE_ROW ){
    char *zAge = human_readable_age(db_column_double(&q,2));
    fossil_print("%8d %8d %16s %s\n",
      db_column_int(&q,0),
      db_column_int(&q,1),
      zAge,
      db_column_text(&q,3));
    fossil_free(zAge);
  }
  db_finalize(&q);
}

/*
** WEBPAGE: fileage
**
** Show all files in a single check-in (identified by the name= query
** parameter) in order of increasing age.
**
** Parameters:
**   name=VERSION   Selects the check-in version (default=tip).
**   glob=STRING    Only shows files matching this glob pattern
**                  (e.g. *.c or *.txt).
**   showid         Show RID values for debugging
*/
void fileage_page(void){
  int rid;
  const char *zName;
  const char *zGlob;
  const char *zUuid;
  const char *zNow;            /* Time of check-in */
  int isBranchCI;              /* name= is a branch name */
  int showId = PB("showid");
  Stmt q1, q2;
  double baseTime;
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( exclude_spiders(0) ) return;
  zName = P("name");
  if( zName==0 ) zName = "tip";
  rid = symbolic_name_to_rid(zName, "ci");
  if( rid==0 ){
    fossil_fatal("not a valid check-in: %s", zName);
  }
  zUuid = db_text("", "SELECT uuid FROM blob WHERE rid=%d", rid);
  isBranchCI = branch_includes_uuid(zName,zUuid);
  baseTime = db_double(0.0,"SELECT mtime FROM event WHERE objid=%d", rid);
  zNow = db_text("", "SELECT datetime(mtime,toLocal()) FROM event"
                     " WHERE objid=%d", rid);
  style_submenu_element("Tree-View", "%R/tree?ci=%T&mtime=1&type=tree", zName);
  style_header("File Ages");
  zGlob = P("glob");
  cgi_check_for_malice();
  compute_fileage(rid,zGlob);
  db_multi_exec("CREATE INDEX fileage_ix1 ON fileage(mid,pathname);");

  if( fossil_strcmp(zName,"tip")==0 ){
    @ <h1>Files in the %z(href("%R/info?name=tip"))latest check-in</a>
  }else if( isBranchCI ){
    @ <h1>Files in the %z(href("%R/info?name=%T",zName))latest check-in</a>
    @ of branch %z(href("%R/timeline?r=%T",zName))%h(zName)</a>
  }else{
    @ <h1>Files in check-in %z(href("%R/info?name=%T",zName))%h(zName)</a>
  }
  if( zGlob && zGlob[0] ){
    @ that match "%h(zGlob)"
  }
  @ ordered by age</h1>
  @
  @ <p>File ages are expressed relative to the check-in time of
  @ %z(href("%R/timeline?c=%t",zNow))%s(zNow)</a>.</p>
  @
  @ <div class='fileage'><table>
  @ <tr><th>Age</th><th>Files</th><th>Check-in</th></tr>
  db_prepare(&q1,
    "SELECT event.mtime, event.objid, blob.uuid,\n"
    "       coalesce(event.ecomment,event.comment),\n"
    "       coalesce(event.euser,event.user),\n"
    "       coalesce((SELECT value FROM tagxref\n"
    "                  WHERE tagtype>0 AND tagid=%d\n"
    "                    AND rid=event.objid),'trunk')\n"
    "  FROM event, blob\n"
    " WHERE event.objid IN (SELECT mid FROM fileage)\n"
    "   AND blob.rid=event.objid\n"
    " ORDER BY event.mtime DESC;",
    TAG_BRANCH
  );
  db_prepare(&q2,
    "SELECT filename.name, fileage.fid\n"
    "  FROM fileage, filename\n"
    " WHERE fileage.mid=:mid AND filename.fnid=fileage.fnid"
  );
  while( db_step(&q1)==SQLITE_ROW ){
    double age = baseTime - db_column_double(&q1, 0);
    int mid = db_column_int(&q1, 1);
    const char *zUuid = db_column_text(&q1, 2);
    const char *zComment = db_column_text(&q1, 3);
    const char *zUser = db_column_text(&q1, 4);
    const char *zBranch = db_column_text(&q1, 5);
    char *zAge = human_readable_age(age);
    @ <tr><td>%s(zAge)</td>
    @ <td>
    db_bind_int(&q2, ":mid", mid);
    while( db_step(&q2)==SQLITE_ROW ){
      const char *zFile = db_column_text(&q2,0);
      @ %z(href("%R/file?name=%T&ci=%!S",zFile,zUuid))%h(zFile)</a> \
      if( showId ){
        int fid = db_column_int(&q2,1);
        @ (%d(fid))<br>
      }else{
        @ </a><br>
      }
    }
    db_reset(&q2);
    @ </td>
    @ <td>
    @ %W(zComment)
    @ (check-in:&nbsp;%z(href("%R/info/%!S",zUuid))%S(zUuid)</a>,
    if( showId ){
      @ id: %d(mid)
    }
    @ user:&nbsp;%z(href("%R/timeline?u=%t&c=%!S&nd",zUser,zUuid))%h(zUser)</a>,
    @ branch:&nbsp;\
    @ %z(href("%R/timeline?r=%t&c=%!S&nd",zBranch,zUuid))%h(zBranch)</a>)
    @ </td></tr>
    @
    fossil_free(zAge);
  }
  @ </table></div>
  db_finalize(&q1);
  db_finalize(&q2);
  style_finish_page();
}
