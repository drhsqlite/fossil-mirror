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
** This file contains code to do formatting of wiki text.
*/
#include <assert.h>
#include "config.h"
#include "wiki.h"

/*
** WEBPAGE: wiki
**
** Render the wiki page that is named after the /wiki/ part of
** the url.
*/
void wiki_page(void){
  style_header("Wiki");
  @ extra=%h(g.zExtra)
  style_footer();
}

/*
** WEBPAGE: ambiguous
**
** This is the destination for UUID hyperlinks that are ambiguous.
** Show all possible choices for the destination with links to each.
**
** The ambiguous UUID prefix is in g.zExtra
*/
void ambiguous_page(void){
  Stmt q;
  style_header("Ambiguous UUID");
  @ <p>The link <a href="%s(g.zBaseURL)/ambiguous/%T(g.zExtra)">
  @ [%h(g.zExtra)]</a> is ambiguous.  It might mean any of the following:</p>
  @ <ul>
  db_prepare(&q, "SELECT uuid, rid FROM blob WHERE uuid>=%Q AND uuid<'%qz'"
                 " ORDER BY uuid", g.zExtra, g.zExtra);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    @ <li> %s(zUuid) - %d(rid)
  }
  db_finalize(&q);
  @ </ul>
  style_footer();
}
