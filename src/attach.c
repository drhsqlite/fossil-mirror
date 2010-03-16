/*
** Copyright (c) 2010 D. Richard Hipp
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
** This file contains code for dealing with attachments.
*/
#include "config.h"
#include "attach.h"
#include <assert.h>

/*
** WEBPAGE: attachlist
**
**    tkt=TICKETUUID
**    page=WIKIPAGE
**
** List attachments.
*/
void attachlist_page(void){
  const char *zPage = P("page");
  const char *zTkt = P("tkt");
  Blob sql;
  Stmt q;

  if( zPage && zTkt ) zTkt = 0;
  login_check_credentials();
  blob_zero(&sql);
  blob_append(&sql,
     "SELECT datetime(mtime,'localtime'), src, filename, comment, user"
     "  FROM attachment",
     -1
  );
  if( zPage ){
    if( g.okRdWiki==0 ) login_needed();
    style_header("Attachments To %h", zPage);
    blob_appendf(&sql, " WHERE target=%Q", zPage);
  }else if( zTkt ){
    if( g.okRdTkt==0 ) login_needed();
    style_header("Attachments To Ticket %.10s", zTkt);
    blob_appendf(&sql, " WHERE target GLOB '%q*'", zTkt);
  }else{
    if( g.okRdTkt==0 && g.okRdWiki==0 ) login_needed();
    style_header("All Attachments");
  }
  blob_appendf(&sql, " ORDER BY mtime DESC");
  db_prepare(&q, "%s", blob_str(&sql));
  while( db_step(&q)==SQLITE_ROW ){
    const char *zDate = db_column_text(&q, 0);
    const char *zSrc = db_column_text(&q, 1);
    const char *zFilename = db_column_text(&q, 2);
    const char *zComment = db_column_text(&q, 3);
    const char *zUser = db_column_text(&q, 4);
    int i;
    for(i=0; zFilename[i]; i++){
      if( zFilename[i]=='/' && zFilename[i+1]!=0 ){ 
        zFilename = &zFilename[i+1];
        i = -1;
      }
    }
    @ <p><b>%h(zFilename)</b>
    @ %w(zComment)<br>
    @ Added by %h(zUser) on %s(zDate)</p>
    @
  }
  db_finalize(&q);
  style_footer();
  
  return;
}
