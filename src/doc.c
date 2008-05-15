/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
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
** This file contains code to implement the "/doc" web page and related
** pages.
*/
#include "config.h"
#include "doc.h"
#include <assert.h>

/*
** Guess the mime-type of a document based on its name.
*/
const char *mimetype_from_name(const char *zName){
  const char *z;
  int i;
  char zSuffix[20];
  static const struct {
    const char *zSuffix;
    const char *zMimetype;
  } aMime[] = {
    { "html",     "text/html"                 },
    { "htm",      "text/html"                 },
    { "wiki",     "application/x-fossil-wiki" },
    { "txt",      "text/plain"                },
    { "jpg",      "image/jpeg"                },
    { "jpeg",     "image/jpeg"                },
    { "gif",      "image/gif"                 },
    { "png",      "image/png"                 },
    { "css",      "text/css"                  },
  };

  z = zName;
  for(i=0; zName[i]; i++){
    if( zName[i]=='.' ) z = &zName[i+1];
  }
  i = strlen(z);
  if( i<sizeof(zSuffix)-1 ){
    strcpy(zSuffix, z);
    for(i=0; zSuffix[i]; i++) zSuffix[i] = tolower(zSuffix[i]);
    for(i=0; i<sizeof(aMime)/sizeof(aMime[0]); i++){
      if( strcmp(zSuffix, aMime[i].zSuffix)==0 ){
        return aMime[i].zMimetype;
      }
    }
  }
  return "application/x-fossil-artifact";
}

/*
** WEBPAGE: doc
** URL: /doc?name=BASELINE/PATH
**
** BASELINE can be either a baseline uuid prefix or magic words "tip"
** to me the most recently checked in baseline or "ckout" to mean the
** content of the local checkout, if any.  PATH is the relative pathname
** of some file.  This method returns the file content.
**
** If PATH matches the patterns *.wiki or *.txt then formatting content
** is added before returning the file.  For all other names, the content
** is returned straight without any interpretation or processing.
*/
void doc_page(void){
  const char *zName;                /* Argument to the /doc page */
  const char *zMime;                /* Document MIME type */
  int vid = 0;                      /* Artifact of baseline */
  int rid = 0;                      /* Artifact of file */
  int i;                            /* Loop counter */
  Blob filebody;                    /* Content of the documentation file */
  char zBaseline[UUID_SIZE+1];      /* Baseline UUID */

  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  zName = PD("name", "tip/index.wiki");
  for(i=0; zName[i] && zName[i]!='/'; i++){}
  if( zName[i]==0 || i>UUID_SIZE ){
    goto doc_not_found;
  }
  memcpy(zBaseline, zName, i);
  zBaseline[i] = 0;
  zName += i;
  while( zName[0]=='/' ){ zName++; }
  if( !file_is_simple_pathname(zName) ){
    goto doc_not_found;
  }
  if( strcmp(zBaseline,"ckout")==0 ){
    /* Read from the local checkout */
    char *zFullpath;
    db_must_be_within_tree();
    zFullpath = mprintf("%s/%s", g.zLocalRoot, zName);
    if( !file_isfile(zFullpath) ){
      goto doc_not_found;
    }
    if( blob_read_from_file(&filebody, zFullpath)<0 ){
      goto doc_not_found;
    }
  }else{
    db_begin_transaction();
    if( strcmp(zBaseline,"tip")==0 ){
      vid = db_int(0, "SELECT objid FROM event WHERE type='ci'"
                      " ORDER BY mtime DESC LIMIT 1");
    }else{
      vid = name_to_rid(zBaseline);
    }

    /* Create the baseline cache if it does not already exist */
    db_multi_exec(
      "CREATE TABLE IF NOT EXISTS vcache(\n"
      "  vid INTEGER,         -- baseline ID\n"
      "  fname TEXT,          -- filename\n"
      "  rid INTEGER,         -- artifact ID\n"
      "  UNIQUE(vid,fname,rid)\n"
      ")"
    );

    /* Check to see if the documentation file artifact ID is contained
    ** in the baseline cache */
    rid = db_int(0, "SELECT rid FROM vcache"
                    " WHERE vid=%d AND fname=%Q", vid, zName);
    if( rid==0 && db_exists("SELECT 1 FROM vcache WHERE vid=%d", vid) ){
      goto doc_not_found;
    }

    if( rid==0 ){
      Stmt s;
      Blob baseline;
      Manifest m;

      /* Add the vid baseline to the cache */
      if( db_int(0, "SELECT count(*) FROM vcache")>10000 ){
        db_multi_exec("DELETE FROM vcache");
      }
      if( content_get(vid, &baseline)==0 ){
        goto doc_not_found;
      }
      if( manifest_parse(&m, &baseline)==0 || m.type!=CFTYPE_MANIFEST ){
        goto doc_not_found;
      }
      db_prepare(&s,
        "INSERT INTO vcache(vid,fname,rid)"
        " SELECT %d, :fname, rid FROM blob"
        "  WHERE uuid=:uuid",
        vid
      );
      for(i=0; i<m.nFile; i++){
        db_bind_text(&s, ":fname", m.aFile[i].zName);
        db_bind_text(&s, ":uuid", m.aFile[i].zUuid);
        db_step(&s);
        db_reset(&s);
      }
      db_finalize(&s);
      manifest_clear(&m);

      /* Try again to find the file */
      rid = db_int(0, "SELECT rid FROM vcache"
                      " WHERE vid=%d AND fname=%Q", vid, zName);
    }
    if( rid==0 ){
      goto doc_not_found;
    }

    /* Get the file content */
    if( content_get(rid, &filebody)==0 ){
      goto doc_not_found;
    }
    db_end_transaction(0);
  }

  /* The file is now contained in the filebody blob.  Deliver the
  ** file to the user 
  */
  zMime = mimetype_from_name(zName);
  if( strcmp(zMime, "application/x-fossil-wiki")==0 ){
    style_header("Documentation");
    wiki_convert(&filebody, 0, 0);
    style_footer();
  }else if( strcmp(zMime, "text/plain")==0 ){
    style_header("Documentation");
    @ <blockquote><pre>
    @ %h(blob_str(&filebody))
    @ </pre></blockquote>
    style_footer();
  }else{
    cgi_set_content_type(zMime);
    cgi_set_content(&filebody);
  }
  return;

doc_not_found:
  /* Jump here when unable to locate the document */
  db_end_transaction(0);
  style_header("Document Not Found");
  @ <p>No such document: %h(PD("name","tip/index.wiki"))</p>
  style_footer();
  return;  
}
