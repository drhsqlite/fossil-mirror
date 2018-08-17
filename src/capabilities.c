/*
** Copyright (c) 2018 D. Richard Hipp
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
** This file contains code used managing user capability strings.
*/
#include "config.h"
#include "capabilities.h"
#include <assert.h>

#if INTERFACE
/*
** A capability string object holds all defined capabilities in a
** vector format that is subject to boolean operations.
*/
struct CapabilityString {
  unsigned char x[128];
};
#endif

/*
** Add capabilities to a CapabilityString.  If pIn is NULL, then create
** a new capability string.
**
** Call capability_free() on the allocated CapabilityString object to
** deallocate.
*/
CapabilityString *capability_add(CapabilityString *pIn, const char *zCap){
  int c;
  int i;
  if( pIn==0 ){
    pIn = fossil_malloc( sizeof(*pIn) );
    memset(pIn, 0, sizeof(*pIn));
  }
  if( zCap ){
    for(i=0; (c = zCap[i])!=0; i++){
      if( c>='0' && c<='z' ) pIn->x[c] = 1;
    }
  }
  return pIn;
}

/*
** Remove capabilities from a CapabilityString.
*/
CapabilityString *capability_remove(CapabilityString *pIn, const char *zCap){
  int c;
  int i;
  if( pIn==0 ){
    pIn = fossil_malloc( sizeof(*pIn) );
    memset(pIn, 0, sizeof(*pIn));
  }
  if( zCap ){
    for(i=0; (c = zCap[i])!=0; i++){
      if( c>='0' && c<='z' ) pIn->x[c] = 0;
    }
  }
  return pIn;
}

/*
** Return true if any of the capabilities in zNeeded are found in pCap
*/
int capability_has_any(CapabilityString *p, const char *zNeeded){
  if( p==0 ) return 0;
  if( zNeeded==0 ) return 0;
  while( zNeeded[0] ){
    int c = zNeeded[0];
    if( fossil_isalnum(c) && p->x[c] ) return 1;
    zNeeded++;
  }
  return 0;
}

/*
** Delete a CapabilityString object.
*/
void capability_free(CapabilityString *p){
  fossil_free(p);
}

/*
** Expand the capability string by including all capabilities for 
** special users "nobody" and "anonymous".  Also include "reader"
** if "u" is present and "developer" if "v" is present.
*/
void capability_expand(CapabilityString *pIn){
  static char *zNobody = 0;
  static char *zAnon = 0;
  static char *zReader = 0;
  static char *zDev = 0;
  int doneV = 0;

  if( pIn==0 ){
    fossil_free(zNobody); zNobody = 0;
    fossil_free(zAnon);   zAnon = 0;
    fossil_free(zReader); zReader = 0;
    fossil_free(zDev);    zDev = 0;
    return;
  }
  if( zNobody==0 ){
    zNobody = db_text(0, "SELECT cap FROM user WHERE login='nobody'");
    zAnon = db_text(0, "SELECT cap FROM user WHERE login='anonymous'");
    zReader = db_text(0, "SELECT cap FROM user WHERE login='reader'");
    zDev = db_text(0, "SELECT cap FROM user WHERE login='developer'");
  }
  pIn = capability_add(pIn, zAnon);
  pIn = capability_add(pIn, zNobody);
  if( pIn->x['v'] ){
    pIn = capability_add(pIn, zDev);
    doneV = 1;
  }
  if( pIn->x['u'] ){
    pIn = capability_add(pIn, zReader);
    if( pIn->x['v'] && !doneV ){
      pIn = capability_add(pIn, zDev);
    }
  }
}

/*
** Render a capability string in canonical string format.  Space to hold
** the returned string is obtained from fossil_malloc() can should be freed
** by the caller.
*/
char *capability_string(CapabilityString *p){
  Blob out;
  int i;
  int j = 0;
  char buf[100];
  blob_init(&out, 0, 0);
  for(i='a'; i<='z'; i++){
    if( p->x[i] ) buf[j++] = i;
  }
  for(i='0'; i<='9'; i++){
    if( p->x[i] ) buf[j++] = i;
  }
  for(i='A'; i<='Z'; i++){
    if( p->x[i] ) buf[j++] = i;
  }
  buf[j] = 0;
  return fossil_strdup(buf);
}

/*
** The next two routines implement an aggregate SQL function that
** takes multiple capability strings and in the end returns their
** union.  Example usage:
**
**    SELECT capunion(cap) FROM user WHERE login IN ('nobody','anonymous');
*/
void capability_union_step(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  CapabilityString *p;
  const char *zIn;

  zIn = (const char*)sqlite3_value_text(argv[0]);
  if( zIn==0 ) return;
  p = (CapabilityString*)sqlite3_aggregate_context(context, sizeof(*p));
  p = capability_add(p, zIn);
}
void capability_union_finalize(sqlite3_context *context){
  CapabilityString *p;
  p = sqlite3_aggregate_context(context, 0);
  if( p ){
    char *zOut = capability_string(p);
    sqlite3_result_text(context, zOut, -1, fossil_free);
  }
}

/*
** The next routines takes the raw USER.CAP field and expands it with
** capabilities from special users.  Example:
**
**   SELECT fullcap(cap) FROM user WHERE login=?1
*/
void capability_fullcap(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  CapabilityString *p;
  const char *zIn;
  char *zOut;

  zIn = (const char*)sqlite3_value_text(argv[0]);
  if( zIn==0 ) zIn = "";
  p = capability_add(0, zIn);
  capability_expand(p);
  zOut = capability_string(p);
  sqlite3_result_text(context, zOut, -1, fossil_free);
  capability_free(p);
}

/*
** Generate HTML that lists all of the capability letters together with
** a brief summary of what each letter means.
*/
void capabilities_table(void){
  @ <table>
     @ <tr><th valign="top">a</th>
     @   <td><i>Admin:</i> Create and delete users</td></tr>
     @ <tr><th valign="top">b</th>
     @   <td><i>Attach:</i> Add attachments to wiki or tickets</td></tr>
     @ <tr><th valign="top">c</th>
     @   <td><i>Append-Tkt:</i> Append to tickets</td></tr>
     @ <tr><th valign="top">d</th>
     @   <td><i>Delete:</i> Delete wiki and tickets</td></tr>
     @ <tr><th valign="top">e</th>
     @   <td><i>View-PII:</i> \
     @ View sensitive data such as email addresses</td></tr>
     @ <tr><th valign="top">f</th>
     @   <td><i>New-Wiki:</i> Create new wiki pages</td></tr>
     @ <tr><th valign="top">g</th>
     @   <td><i>Clone:</i> Clone the repository</td></tr>
     @ <tr><th valign="top">h</th>
     @   <td><i>Hyperlinks:</i> Show hyperlinks to detailed
     @   repository history</td></tr>
     @ <tr><th valign="top">i</th>
     @   <td><i>Check-In:</i> Commit new versions in the repository</td></tr>
     @ <tr><th valign="top">j</th>
     @   <td><i>Read-Wiki:</i> View wiki pages</td></tr>
     @ <tr><th valign="top">k</th>
     @   <td><i>Write-Wiki:</i> Edit wiki pages</td></tr>
     @ <tr><th valign="top">l</th>
     @   <td><i>Mod-Wiki:</i> Moderator for wiki pages</td></tr>
     @ <tr><th valign="top">m</th>
     @   <td><i>Append-Wiki:</i> Append to wiki pages</td></tr>
     @ <tr><th valign="top">n</th>
     @   <td><i>New-Tkt:</i> Create new tickets</td></tr>
     @ <tr><th valign="top">o</th>
     @   <td><i>Check-Out:</i> Check out versions</td></tr>
     @ <tr><th valign="top">p</th>
     @   <td><i>Password:</i> Change your own password</td></tr>
     @ <tr><th valign="top">q</th>
     @   <td><i>Mod-Tkt:</i> Moderator for tickets</td></tr>
     @ <tr><th valign="top">r</th>
     @   <td><i>Read-Tkt:</i> View tickets</td></tr>
     @ <tr><th valign="top">s</th>
     @   <td><i>Setup/Super-user:</i> Setup and configure this website</td></tr>
     @ <tr><th valign="top">t</th>
     @   <td><i>Tkt-Report:</i> Create new bug summary reports</td></tr>
     @ <tr><th valign="top">u</th>
     @   <td><i>Reader:</i> Inherit privileges of
     @   user <tt>reader</tt></td></tr>
     @ <tr><th valign="top">v</th>
     @   <td><i>Developer:</i> Inherit privileges of
     @   user <tt>developer</tt></td></tr>
     @ <tr><th valign="top">w</th>
     @   <td><i>Write-Tkt:</i> Edit tickets</td></tr>
     @ <tr><th valign="top">x</th>
     @   <td><i>Private:</i> Push and/or pull private branches</td></tr>
     @ <tr><th valign="top">y</th>
     @   <td><i>Write-Unver:</i> Push unversioned files</td></tr>
     @ <tr><th valign="top">z</th>
     @   <td><i>Zip download:</i> Download a ZIP archive or tarball</td></tr>
     @ <tr><th valign="top">2</th>
     @   <td><i>Forum-Read:</i> Read forum posts by others </td></tr>
     @ <tr><th valign="top">3</th>
     @   <td><i>Forum-Append:</i> Add new forum posts</td></tr>
     @ <tr><th valign="top">4</th>
     @   <td><i>Forum-Trusted:</i> Add pre-approved forum posts </td></tr>
     @ <tr><th valign="top">5</th>
     @   <td><i>Forum-Moderator:</i> Approve or disapprove forum posts</td></tr>
     @ <tr><th valign="top">6</th>
     @   <td><i>Forum-Supervisor:</i> \
     @ Forum administrator: Set or remove capability "4" for other users
     @ <tr><th valign="top">7</th>
     @   <td><i>Email-Alerts:</i> Sign up for email nofications</td></tr>
     @ <tr><th valign="top">A</th>
     @   <td><i>Announce:</i> Send announcements</td></tr>
     @ <tr><th valign="top">D</th>
     @   <td><i>Debug:</i> Enable debugging features</td></tr>
  @ </table>
}

/*
** Generate a "capability summary table" that shows the major capabilities
** against the various user categories.
*/
void capability_summary(void){
  Stmt q;
  db_prepare(&q,
    "WITH t(id,seq) AS (VALUES('nobody',1),('anonymous',2),('reader',3),"
                       "('developer',4))"
    " SELECT id, fullcap(user.cap),seq,1"
    "   FROM t LEFT JOIN user ON t.id=user.login"
    " UNION ALL"
    " SELECT 'New User Default', fullcap(%Q), 10, 1"
    " UNION ALL"
    " SELECT 'Regular User', fullcap(capunion(cap)), 20, count(*) FROM user"
    " WHERE cap NOT GLOB '*[as]*'"
    " UNION ALL"
    " SELECT 'Adminstator', fullcap(capunion(cap)), 30, count(*) FROM user"
    " WHERE cap GLOB '*[as]*'"
    " ORDER BY 3 ASC",
    db_get("default-perms","")
  );
  @ <table id='capabilitySummary' cellpadding="0" cellspacing="0" border="1">
  @ <tr><th>&nbsp;<th>Code<th>Forum<th>Tickets<th>Wiki\
  @ <th>Unversioned Content</th></tr>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zId = db_column_text(&q, 0);
    const char *zCap = db_column_text(&q, 1);
    int n = db_column_int(&q, 3);
    int eType;
    static const char *azType[] = { "off", "read", "write" };
    static const char *azClass[] = { "capsumOff", "capsumRead", "capsumWrite" };

    if( n==0 ) continue;

    /* Code */
    if( db_column_int(&q,2)<10 ){
      @ <tr><th align="right"><tt>"%h(zId)"</tt></th>
    }else if( n>1 ){
      @ <tr><th align="right">%d(n) %h(zId)s</th>
    }else{
      @ <tr><th align="right">%h(zId)</th>
    }
    if( sqlite3_strglob("*[asi]*",zCap)==0 ){
      eType = 2;
    }else if( sqlite3_strglob("*[oz]*",zCap)==0 ){
      eType = 1;
    }else{
      eType = 0;
    }
    @ <td class="%s(azClass[eType])">%s(azType[eType])</td>

    /* Forum */
    if( sqlite3_strglob("*[as3456]*",zCap)==0 ){
      eType = 2;
    }else if( sqlite3_strglob("*2*",zCap)==0 ){
      eType = 1;
    }else{
      eType = 0;
    }
    @ <td class="%s(azClass[eType])">%s(azType[eType])</td>

    /* Ticket */
    if( sqlite3_strglob("*[ascdnqtw]*",zCap)==0 ){
      eType = 2;
    }else if( sqlite3_strglob("*r*",zCap)==0 ){
      eType = 1;
    }else{
      eType = 0;
    }
    @ <td class="%s(azClass[eType])">%s(azType[eType])</td>

    /* Wiki */
    if( sqlite3_strglob("*[asdfklm]*",zCap)==0 ){
      eType = 2;
    }else if( sqlite3_strglob("*j*",zCap)==0 ){
      eType = 1;
    }else{
      eType = 0;
    }
    @ <td class="%s(azClass[eType])">%s(azType[eType])</td>

    /* Unversioned */
    if( sqlite3_strglob("*y*",zCap)==0 ){
      eType = 2;
    }else if( sqlite3_strglob("*[ioas]*",zCap)==0 ){
      eType = 1;
    }else{
      eType = 0;
    }
    @ <td class="%s(azClass[eType])">%s(azType[eType])</td>

  }
  db_finalize(&q);
  @ </table>
}
