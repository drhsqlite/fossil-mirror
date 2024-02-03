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
  static char *zAdmin = "bcdefghijklmnopqrtwz234567AD";
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
  if( pIn->x['a'] || pIn->x['s'] ){
    pIn = capability_add(pIn, zAdmin);
  }
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

#if INTERFACE
/*
** Capabilities are grouped into "classes" as follows:
*/
#define CAPCLASS_CODE  0x0001
#define CAPCLASS_WIKI  0x0002
#define CAPCLASS_TKT   0x0004
#define CAPCLASS_FORUM 0x0008
#define CAPCLASS_DATA  0x0010
#define CAPCLASS_ALERT 0x0020
#define CAPCLASS_OTHER 0x0040
#define CAPCLASS_SUPER 0x0080
#define CAPCLASS_ALL   0xffff
#endif /* INTERFACE */


/*
** The following structure holds descriptions of the various capabilities.
*/
static struct Caps {
  char cCap;              /* The capability letter */
  unsigned short eClass;  /* The "class" for this capability */
  unsigned nUser;         /* Number of users with this capability */
  char *zAbbrev;          /* Abbreviated mnemonic name */
  char *zOneLiner;        /* One-line summary */
} aCap[] = {
  { 'a', CAPCLASS_SUPER, 0,
    "Admin", "Create and delete users" },
  { 'b', CAPCLASS_WIKI|CAPCLASS_TKT, 0,
    "Attach", "Add attachments to wiki or tickets" },
  { 'c', CAPCLASS_TKT, 0,
    "Append-Tkt", "Append to existing tickets" },
  /*
  ** d unused since fork from CVSTrac;
  ** see https://fossil-scm.org/forum/forumpost/43c78f4bef
  */
  { 'e', CAPCLASS_DATA, 0,
    "View-PII", "View sensitive info such as email addresses" },
  { 'f', CAPCLASS_WIKI, 0,
    "New-Wiki", "Create new wiki pages" },
  { 'g', CAPCLASS_DATA, 0,
    "Clone", "Clone the repository" },
  { 'h', CAPCLASS_OTHER, 0,
    "Hyperlinks", "Show hyperlinks to detailed repository history" },
  { 'i', CAPCLASS_CODE, 0,
    "Check-In", "Check-in code changes" },
  { 'j', CAPCLASS_WIKI, 0,
    "Read-Wiki", "View wiki pages" },
  { 'k', CAPCLASS_WIKI, 0,
    "Write-Wiki", "Edit wiki pages" },
  { 'l', CAPCLASS_WIKI|CAPCLASS_SUPER, 0,
    "Mod-Wiki", "Moderator for wiki pages" },
  { 'm', CAPCLASS_WIKI, 0,
    "Append-Wiki", "Append to wiki pages" },
  { 'n', CAPCLASS_TKT, 0,
    "New-Tkt", "Create new tickets" },
  { 'o', CAPCLASS_CODE, 0,
    "Check-Out", "Check out code" },
  { 'p', CAPCLASS_OTHER, 0,
    "Password", "Change your own password" },
  { 'q', CAPCLASS_TKT|CAPCLASS_SUPER, 0,
    "Mod-Tkt", "Moderate tickets" },
  { 'r', CAPCLASS_TKT, 0,
    "Read-Tkt", "View tickets" },
  { 's', CAPCLASS_SUPER, 0,
    "Superuser", "Setup and configure the repository" },
  { 't', CAPCLASS_TKT, 0,
    "Reports", "Create new ticket report formats" },
  { 'u', CAPCLASS_OTHER, 0,
    "Reader", "Inherit all the capabilities of the \"reader\" user" },
  { 'v', CAPCLASS_OTHER, 0,
    "Developer", "Inherit all capabilities of the \"developer\" user" },
  { 'w', CAPCLASS_TKT, 0,
    "Write-Tkt", "Edit tickets" },
  { 'x', CAPCLASS_DATA, 0,
    "Private", "Push and/or pull private branches" },
  { 'y', CAPCLASS_SUPER, 0,
    "Write-UV", "Push unversioned content" },
  { 'z', CAPCLASS_CODE, 0,
    "Zip-Download", "Download a ZIP archive, tarball, or SQL archive" },
  { '2', CAPCLASS_FORUM, 0,
    "Forum-Read", "Read forum posts by others" },
  { '3', CAPCLASS_FORUM, 0,
    "Forum-Write", "Create new forum messages" },
  { '4', CAPCLASS_FORUM, 0,
    "Forum-Trusted", "Create forum messages that bypass moderation" },
  { '5', CAPCLASS_FORUM|CAPCLASS_SUPER, 0,
    "Forum-Mod", "Moderator for forum messages" },
  { '6', CAPCLASS_FORUM|CAPCLASS_SUPER, 0,
    "Forum-Admin", "Grant capability '4' to other users" },
  { '7', CAPCLASS_ALERT, 0,
    "Alerts", "Sign up for email alerts" },
  { 'A', CAPCLASS_ALERT|CAPCLASS_SUPER, 0,
    "Announce", "Send announcements to all subscribers" },
  { 'C', CAPCLASS_FORUM, 0,
    "Chat",  "Read and/or writes messages in the chatroom" },
  { 'D', CAPCLASS_OTHER, 0,
    "Debug", "Enable debugging features" },
};

/*
** Populate the aCap[].nUser values based on the current content
** of the USER table.
*/
void capabilities_count(void){
  int i;
  static int done = 0;
  Stmt q;
  if( done ) return;
  db_prepare(&q, "SELECT fullcap(cap) FROM user");
  while( db_step(&q)==SQLITE_ROW ){
    const char *zCap = db_column_text(&q, 0);
    if( zCap==0 || zCap[0]==0 ) continue;
    for(i=0; i<(int)(sizeof(aCap)/sizeof(aCap[0])); i++){
      if( strchr(zCap, aCap[i].cCap) ) aCap[i].nUser++;
    }
  }
  db_finalize(&q);
  done = 1;
}


/*
** Generate HTML that lists all of the capability letters together with
** a brief summary of what each letter means.
*/
void capabilities_table(unsigned mClass){
  int i;
  if( g.perm.Admin ) capabilities_count();
  @ <table>
  @ <tbody>
  for(i=0; i<(int)(sizeof(aCap)/sizeof(aCap[0])); i++){
    int n;
    if( (aCap[i].eClass & mClass)==0 ) continue;
    @ <tr><th valign="top">%c(aCap[i].cCap)</th>
    @  <td>%h(aCap[i].zAbbrev)</td><td>%h(aCap[i].zOneLiner)</td>\
    n = aCap[i].nUser;
    if( n && g.perm.Admin ){
      @ <td><a href="%R/setup_ulist?with=%c(aCap[i].cCap)">\
      @ %d(n) user%s(n>1?"s":"")</a></td>\
    }
    @ </tr>
  }
  @ </tbody>
  @ </table>
}

/*
** Generate a "capability summary table" that shows the major capabilities
** against the various user categories.
*/
void capability_summary(void){
  Stmt q;
  CapabilityString *pCap;
  char *zSelfCap;
  char *zPubPages = db_get("public-pages",0);
  int hasPubPages = zPubPages && zPubPages[0];

  pCap = capability_add(0, db_get("default-perms","u"));
  capability_expand(pCap);
  zSelfCap = capability_string(pCap);
  capability_free(pCap);

  db_prepare(&q,
    "WITH t(id,seq) AS (VALUES('nobody',1),('anonymous',2),('reader',3),"
                       "('developer',4))"
    " SELECT id, CASE WHEN user.login='nobody' THEN user.cap"
                    " ELSE fullcap(user.cap) END,seq,1"
    "   FROM t LEFT JOIN user ON t.id=user.login"
    " UNION ALL"
    " SELECT 'Public Pages', %Q, 100, %d"
    " UNION ALL"
    " SELECT 'New User Default', %Q, 110, 1"
    " UNION ALL"
    " SELECT 'Regular User', fullcap(capunion(cap)), 200, count(*) FROM user"
    " WHERE cap NOT GLOB '*[as]*' AND login NOT IN (SELECT id FROM t)"
    " UNION ALL"
    " SELECT 'Administrator', fullcap(capunion(cap)), 300, count(*) FROM user"
    " WHERE cap GLOB '*[as]*'"
    " ORDER BY 3 ASC",
    zSelfCap, hasPubPages, zSelfCap
  );
  @ <table id='capabilitySummary' cellpadding="0" cellspacing="0" border="1">
  @ <tr><th>&nbsp;<th>Code<th>Forum<th>Tickets<th>Wiki<th>Chat\
  @ <th>Unversioned Content</th></tr>
  while( db_step(&q)==SQLITE_ROW ){
    const char *zId = db_column_text(&q, 0);
    const char *zCap = db_column_text(&q, 1);
    int n = db_column_int(&q, 3);
    int eType;
    static const char *const azType[] = { "off", "read", "write" };
    static const char *const azClass[] =
        { "capsumOff", "capsumRead", "capsumWrite" };

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
    if( sqlite3_strglob("*[ascnqtw]*",zCap)==0 ){
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

    /* Chat */
    if( sqlite3_strglob("*C*",zCap)==0 ){
      eType = 2;
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
