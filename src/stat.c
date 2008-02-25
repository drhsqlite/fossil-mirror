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
** This file contains code to implement the stat web page
**
*/
#include <string.h>
#include "config.h"
#include "stat.h"

/*
** WEBPAGE: stat
**
** Show statistics and global information about the repository.
*/
void stat_page(void){
  int n, m, k;
  login_check_credentials();
  if( !g.okRead ){ login_needed(); return; }
  style_header("Repository Statistics");
  @ <p><table class="label-value">
  @ <tr><th>Repository&nbsp;Size:</th><td>
  n = file_size(g.zRepositoryName);
  @ %d(n) bytes
  @ </td></tr>
  @ <tr><th>Number&nbsp;Of&nbsp;Artifacts:</th><td>
  n = db_int(0, "SELECT count(*) FROM blob");
  m = db_int(0, "SELECT count(*) FROM delta");
  @ %d(n-m) complete, %d(m) deltas, %d(n) total
  @ </td></tr>
  if( n>0 ){
    @ <tr><th>Uncompressed&nbsp;Artifact&nbsp;Size:</th><td>
    k = db_int(0, "SELECT total(size) FROM blob WHERE size>0");
    @ %d((int)(((double)k)/(double)n)) bytes average, %d(k) bytes total
    @ </td></tr>
  }
  @ <tr><th>Number&nbsp;Of&nbsp;Baselines:</th><td>
  n = db_int(0, "SELECT count(distinct mid) FROM mlink");
  @ %d(n)
  @ </td></tr>
  @ <tr><th>Number&nbsp;Of&nbsp;Files:</th><td>
  n = db_int(0, "SELECT count(*) FROM filename");
  @ %d(n)
  @ </td></tr>
  @ <tr><th>Number&nbsp;Of&nbsp;Wiki&nbsp;Pages:</th><td>
  n = db_int(0, "SELECT count(*) FROM tag WHERE +tagname GLOB 'wiki-*'");
  @ %d(n)
  @ </td></tr>
  @ <tr><th>Number&nbsp;Of&nbsp;Tickets:</th><td>
  n = db_int(0, "SELECT count(*) FROM tag WHERE +tagname GLOB 'ticket-*'");
  @ %d(n)
  @ </td></tr>
  @ <tr><th>Duration&nbsp;Of&nbsp;Project:</th><td>
  n = db_int(0, "SELECT julianday('now') - (SELECT min(mtime) FROM event)");
  @ %d(n) days
  @ </td></tr>
  @ <tr><th>Project&nbsp;ID:</th><td>
  @ %h(db_get("project-code",""))
  @ </td></tr>
  @ <tr><th>Server&nbsp;ID:</th><td>
  @ %h(db_get("server-code",""))
  @ </td></tr>
  @ </table></p>
  style_footer();
}