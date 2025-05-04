/*
** Copyright (c) 2006 D. Richard Hipp
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
** This file contains code used to resolved user-supplied object names.
*/
#include "config.h"
#include "name.h"
#include <assert.h>

#if INTERFACE
/*
** An upper boundary on RIDs, provided in order to be able to
** distinguish real RID values from RID_CKOUT and any future
** RID_... values.
*/
#define RID_MAX        0x7ffffff0
/*
** A "magic" RID representing the current checkout in some contexts.
*/
#define RID_CKOUT      (RID_MAX+1)
#endif

/*
** Return TRUE if the string begins with something that looks roughly
** like an ISO date/time string.  The SQLite date/time functions will
** have the final say-so about whether or not the date/time string is
** well-formed.
*/
int fossil_isdate(const char *z){
  if( !fossil_isdigit(z[0]) ) return 0;
  if( !fossil_isdigit(z[1]) ) return 0;
  if( !fossil_isdigit(z[2]) ) return 0;
  if( !fossil_isdigit(z[3]) ) return 0;
  if( z[4]!='-') return 0;
  if( !fossil_isdigit(z[5]) ) return 0;
  if( !fossil_isdigit(z[6]) ) return 0;
  if( z[7]!='-') return 0;
  if( !fossil_isdigit(z[8]) ) return 0;
  if( !fossil_isdigit(z[9]) ) return 0;
  return 1;
}

/*
** Check to see if the string might be a compact date/time that omits
** the punctuation.  Example:  "20190327084549" instead of
** "2019-03-27 08:45:49".  If the string is of the appropriate form,
** then return an alternative string (in static space) that is the same
** string with punctuation inserted.
**
** If the bRoundUp parameter is true, then round the resulting date-time
** up to the largest date/time that is consistent with the input value.
** This is because the result will be used for an mtime<=julianday($DATE)
** comparison.  In other words:
**
**     20250317123421 -> 2025-03-17 12:34:21.999
**                                          ^^^^--- Added
**
**     202503171234   -> 2025-03-17 12:34:59.999
**                                       ^^^^^^^--- Added
**     20250317       -> 2025-03-17 23:59:59.999
**                                 ^^^^^^^^^^^^^--- Added
**
** If the bVerifyNotAHash flag is true, then a check is made to see if
** the input string is a hash prefix and NULL is returned if it is.  If the
** bVerifyNotAHash flag is false, then the result is determined by syntax
** of the input string only, without reference to the artifact table.
*/
char *fossil_expand_datetime(const char *zIn,int bVerifyNotAHash,int bRoundUp){
  static char zEDate[24];
  static const char aPunct[] = { 0, 0, '-', '-', ' ', ':', ':' };
  int n = (int)strlen(zIn);
  int i, j;
  int addZulu = 0;

  /* These forms are allowed:
  **
  **        123456789 1234           123456789 123456789 1234
  **   (1)  YYYYMMDD            =>   YYYY-MM-DD 23:59:59.999
  **   (2)  YYYYMMDDHHMM        =>   YYYY-MM-DD HH:MM:59.999
  **   (3)  YYYYMMDDHHMMSS      =>   YYYY-MM-DD HH:MM:SS.999
  **
  ** An optional "Z" zulu timezone designator is allowed at the end.
  */
  if( n>0 && (zIn[n-1]=='Z' || zIn[n-1]=='z') ){
    n--;
    addZulu = 1;
  }
  if( n!=8 && n!=12 && n!=14 ){
    return 0;
  }

  /* Every character must be a digit */
  for(i=0; fossil_isdigit(zIn[i]); i++){}
  if( i!=n && (!addZulu || i!=n+1) ) return 0;

  /* Expand the date */
  for(i=j=0; i<n; i++){
    if( i>=4 && (i%2)==0 ){
      zEDate[j++] = aPunct[i/2];
    }
    zEDate[j++] = zIn[i];
  }
  if( bRoundUp ){
    if( j==10 ){
      memcpy(&zEDate[10], " 23:59:59.999", 13);
      j += 13;
    }else if( j==16 ){
      memcpy(&zEDate[16], ":59.999",7);
      j += 7;
    }else if( j==19 ){
      memcpy(&zEDate[19], ".999", 4);
      j += 4;
    }
  }
  if( addZulu ){
    zEDate[j++] = 'Z';
  }
  zEDate[j] = 0;

  /* Check for reasonable date values.
  ** Offset references:
  **    YYYY-MM-DD HH:MM:SS
  **    0123456789 12345678
  */

  i = atoi(zEDate);
  if( i<1970 || i>2100 ) return 0;
  i = atoi(zEDate+5);
  if( i<1 || i>12 ) return 0;
  i = atoi(zEDate+8);
  if( i<1 || i>31 ) return 0;
  if( n>8 ){
    i = atoi(zEDate+11);
    if( i>24 ) return 0;
    i = atoi(zEDate+14);
    if( i>60 ) return 0;
    if( n==14 && atoi(zEDate+17)>60 ) return 0;
  }

  /* The string is not also a hash prefix */
  if( bVerifyNotAHash && !addZulu ){
    if( db_exists("SELECT 1 FROM blob WHERE uuid GLOB '%q*'",zIn) ) return 0;
  }

  /* It looks like this may be a date.  Return it with punctuation added. */
  return zEDate;
}

/*
** The data-time string in the argument is going to be used as an
** upper bound like this:    mtime<=julianday(zDate,'localtime').
** But if the zDate parameter omits the fractional seconds or the
** seconds, or the time, that might mess up the == part of the
** comparison.  So add in missing factional seconds or seconds or time.
**
** The returned string is held in a static buffer that is overwritten
** with each call, or else is just a copy of its input if there are
** no changes.
**
** For reference:
**
**        0123456789 123456789 1234
**        YYYY-MM-DD HH:MM:SS.SSSz
*/
const char *fossil_roundup_date(const char *zDate){
  static char zUp[28];
  int n = (int)strlen(zDate);
  int addZ = 0;
  if( n>10 && (zDate[n-1]=='z' || zDate[n-1]=='Z') ){
    n--;
    addZ = 1;
  }
  if( n==19 ){  /* YYYY-MM-DD HH:MM:SS */
    memcpy(zUp, zDate, 19);
    memcpy(zUp+19, ".999z", 6);
    if( !addZ ) zUp[23] = 0;
    return zUp;
  }
  if( n==16 ){ /* YYYY-MM-DD HH:MM */
    memcpy(zUp, zDate, 16);
    memcpy(zUp+16, ":59.999z", 8);
    if( !addZ ) zUp[23] = 0;
    return zUp;
  }
  if( n==10 ){ /* YYYY-MM-DD */
    memcpy(zUp, zDate, 10);
    memcpy(zUp+10, " 23:59:59.999z", 14);
    if( !addZ ) zUp[23] = 0;
    return zUp;
  }
  return zDate;
}


/*
** Return the RID that is the "root" of the branch that contains
** check-in "rid".  Details depending on eType:
**
**    eType==0    The check-in of the parent branch off of which
**                the branch containing RID originally diverged.
**
**    eType==1    The first check-in of the branch that contains RID.
**
**    eType==2    The youngest ancestor of RID that is on the branch
**                from which the branch containing RID diverged.
*/
int start_of_branch(int rid, int eType){
  Stmt q;
  int rc;
  int ans = rid;
  char *zBr = branch_of_rid(rid);
  db_prepare(&q,
    "WITH RECURSIVE"
    "  par(pid, ex, cnt) as ("
    "    SELECT pid, EXISTS(SELECT 1 FROM tagxref"
    "                        WHERE tagid=%d AND tagtype>0"
    "                          AND value=%Q AND rid=plink.pid), 1"
    "    FROM plink WHERE cid=%d AND isprim"
    "    UNION ALL "
    "    SELECT plink.pid, EXISTS(SELECT 1 FROM tagxref "
    "                              WHERE tagid=%d AND tagtype>0"
    "                                AND value=%Q AND rid=plink.pid),"
    "           1+par.cnt"
    "      FROM plink, par"
    "     WHERE cid=par.pid AND isprim AND par.ex "
    "     LIMIT 100000 "
    "  )"
    " SELECT pid FROM par WHERE ex>=%d ORDER BY cnt DESC LIMIT 1",
    TAG_BRANCH, zBr, ans, TAG_BRANCH, zBr, eType%2
  );
  fossil_free(zBr);
  rc = db_step(&q);
  if( rc==SQLITE_ROW ){
    ans = db_column_int(&q, 0);
  }
  db_finalize(&q);
  if( eType==2 && ans>0 ){
    zBr = branch_of_rid(ans);
    ans = compute_youngest_ancestor_in_branch(rid, zBr);
    fossil_free(zBr);
  }
  return ans;
}

/*
** Find the RID of the most recent object with symbolic tag zTag
** and having a type that matches zType.
**
** Return 0 if there are no matches.
**
** This is a tricky query to do efficiently.
** If the tag is very common (ex: "trunk") then
** we want to use the query identified below as Q1 - which searches
** the most recent EVENT table entries for the most recent with the tag.
** But if the tag is relatively scarce (anything other than "trunk", basically)
** then we want to do the indexed search shown below as Q2.
*/
static int most_recent_event_with_tag(const char *zTag, const char *zType){
  return db_int(0,
    "SELECT objid FROM ("
      /* Q1:  Begin by looking for the tag in the 30 most recent events */
      "SELECT objid"
       " FROM (SELECT * FROM event ORDER BY mtime DESC LIMIT 30) AS ex"
      " WHERE type GLOB '%q'"
        " AND EXISTS(SELECT 1 FROM tagxref, tag"
                     " WHERE tag.tagname='sym-%q'"
                       " AND tagxref.tagid=tag.tagid"
                       " AND tagxref.tagtype>0"
                       " AND tagxref.rid=ex.objid)"
      " ORDER BY mtime DESC LIMIT 1"
    ") UNION ALL SELECT * FROM ("
      /* Q2: If the tag is not found in the 30 most recent events, then using
      ** the tagxref table to index for the tag */
      "SELECT event.objid"
       " FROM tag, tagxref, event"
      " WHERE tag.tagname='sym-%q'"
        " AND tagxref.tagid=tag.tagid"
        " AND tagxref.tagtype>0"
        " AND event.objid=tagxref.rid"
        " AND event.type GLOB '%q'"
      " ORDER BY event.mtime DESC LIMIT 1"
    ") LIMIT 1;",
    zType, zTag, zTag, zType
  );
}

/*
** Find the RID for a check-in that is the most recent check-in with
** tag zTag that occurs on or prior to rDate.
**
** See also the performance note on most_recent_event_with_tag() which
** applies to this routine too.
*/
int last_checkin_with_tag_before_date(const char *zTag, double rLimit){
  Stmt s;
  int rid = 0;
  if( strncmp(zTag, "tag:", 4)==0 ) zTag += 4;
  db_prepare(&s,
    "SELECT objid FROM ("
      /* Q1:  Begin by looking for the tag in the 30 most recent events */
      "SELECT objid"
       " FROM (SELECT * FROM event WHERE mtime<=:datelimit"
             " ORDER BY mtime DESC LIMIT 30) AS ex"
      " WHERE type='ci'"
        " AND EXISTS(SELECT 1 FROM tagxref, tag"
                     " WHERE tag.tagname='sym-%q'"
                       " AND tagxref.tagid=tag.tagid"
                       " AND tagxref.tagtype>0"
                       " AND tagxref.rid=ex.objid)"
      " ORDER BY mtime DESC LIMIT 1"
    ") UNION ALL SELECT * FROM ("
      /* Q2: If the tag is not found in the 30 most recent events, then using
      ** the tagxref table to index for the tag */
      "SELECT event.objid"
       " FROM tag, tagxref, event"
      " WHERE tag.tagname='sym-%q'"
        " AND tagxref.tagid=tag.tagid"
        " AND tagxref.tagtype>0"
        " AND event.objid=tagxref.rid"
        " AND event.type='ci'"
        " AND event.mtime<=:datelimit"
      " ORDER BY event.mtime DESC LIMIT 1"
    ") LIMIT 1;",
    zTag, zTag
  );
  db_bind_double(&s, ":datelimit", rLimit);
  if( db_step(&s)==SQLITE_ROW ){
    rid = db_column_int(&s,0);
  }
  db_finalize(&s);
  return rid;
}

/*
** Find the RID of the first check-in (chronologically) after rStart that
** has tag zTag.
**
** See also the performance note on most_recent_event_with_tag() which
** applies to this routine too.
*/
int first_checkin_with_tag_after_date(const char *zTag, double rStart){
  Stmt s;
  int rid = 0;
  if( strncmp(zTag, "tag:", 4)==0 ) zTag += 4;
  db_prepare(&s,
    "SELECT objid FROM ("
      /* Q1:  Begin by looking for the tag in the 30 most recent events */
      "SELECT objid"
       " FROM (SELECT * FROM event WHERE mtime>=:startdate"
             " ORDER BY mtime LIMIT 30) AS ex"
      " WHERE type='ci'"
        " AND EXISTS(SELECT 1 FROM tagxref, tag"
                     " WHERE tag.tagname='sym-%q'"
                       " AND tagxref.tagid=tag.tagid"
                       " AND tagxref.tagtype>0"
                       " AND tagxref.rid=ex.objid)"
      " ORDER BY mtime LIMIT 1"
    ") UNION ALL SELECT * FROM ("
      /* Q2: If the tag is not found in the 30 most recent events, then using
      ** the tagxref table to index for the tag */
      "SELECT event.objid"
       " FROM tag, tagxref, event"
      " WHERE tag.tagname='sym-%q'"
        " AND tagxref.tagid=tag.tagid"
        " AND tagxref.tagtype>0"
        " AND event.objid=tagxref.rid"
        " AND event.type='ci'"
        " AND event.mtime>=:startdate"
      " ORDER BY event.mtime LIMIT 1"
    ") LIMIT 1;",
    zTag, zTag
  );
  db_bind_double(&s, ":startdate", rStart);
  if( db_step(&s)==SQLITE_ROW ){
    rid = db_column_int(&s,0);
  }
  db_finalize(&s);
  return rid;
}

/*
** Return true if character "c" is a character that might have been
** accidentally appended to the end of a URL.
*/
static int is_trailing_punct(char c){
  return c=='.' || c=='_' || c==')' || c=='>' || c=='!' || c=='?' || c==',';
}


/*
** Convert a symbolic name into a RID.  Acceptable forms:
**
**   *  artifact hash (optionally enclosed in [...])
**   *  4-character or larger prefix of an artifact
**   *  Symbolic Name
**   *  "tag:" + symbolic name
**   *  Date or date-time
**   *  "date:" + Date or date-time
**   *  symbolic-name ":" date-time
**   *  "tip"
**
** The following additional forms are available in local checkouts:
**
**   *  "current"
**   *  "prev" or "previous"
**   *  "next"
**
** The following modifier prefixes may be applied to the above forms:
**
**   *  "root:BR" = The origin of the branch named BR.
**   *  "start:BR" = The first check-in of the branch named BR.
**   *  "merge-in:BR" = The most recent merge-in for the branch named BR.
**
** In those forms, BR may be any symbolic form but is assumed to be a
** check-in. Thus root:2021-02-01 would resolve to a check-in, possibly
** in a branch and possibly in the trunk, but never a wiki edit or
** forum post.
**
** Return the RID of the matching artifact.  Or return 0 if the name does not
** match any known object.  Or return -1 if the name is ambiguous.
**
** The zType parameter specifies the type of artifact: ci, t, w, e, g, f.
** If zType is NULL or "" or "*" then any type of artifact will serve.
** If zType is "br" then find the first check-in of the named branch
** rather than the last.
**
** zType is "ci" in most use cases since we are usually searching for
** a check-in. A value of "ci+" works like "ci" but adds these
** semantics: if zTag is "ckout" and a checkout is open, "ci+" causes
** RID_CKOUT to be returned, in which case g.localOpen will hold the
** RID of the checkout. Conversely, passing in the hash, or another
** symbolic name of the local checkout version, will always result in
** its RID being returned.
**
** Note that the input zTag for types "t" and "e" is the artifact hash of
** the ticket-change or technote-change artifact, not the randomly generated
** hexadecimal identifier assigned to tickets and events.  Those identifiers
** live in a separate namespace.
*/
int symbolic_name_to_rid(const char *zTag, const char *zType){
  int rid = 0;
  int ridCkout = 0;
  int nTag;
  int i;
  int startOfBranch = 0;
  const char *zXTag;     /* zTag with optional [...] removed */
  int nXTag;             /* Size of zXTag */
  const char *zDate;     /* Expanded date-time string */
  int isCheckin = 0;     /* zType==ci = 1, zType==ci+ = 2 */

  if( zType==0 || zType[0]==0 ){
    zType = "*";
  }else if( zType[0]=='b' ){
    zType = "ci";
    startOfBranch = 1;
  }
  if( zTag==0 || zTag[0]==0 ) return 0;
  else if( 'c'==zType[0] ){
    if( fossil_strcmp(zType,"ci")==0 ){
      isCheckin = 1;
    }else if( fossil_strcmp(zType,"ci+")==0 ){
      isCheckin = 2;
      zType = "ci";
    }
  }

  /* special keyword: "tip" */
  if( fossil_strcmp(zTag, "tip")==0 && (zType[0]=='*' || isCheckin!=0) ){
    rid = db_int(0,
      "SELECT objid"
      "  FROM event"
      " WHERE type='ci'"
      " ORDER BY event.mtime DESC"
    );
    if( rid ) return rid;
  }

  if( g.localOpen ) {
    ridCkout = db_lget_int("checkout",0);
  }

  /* special keywords: "prev", "previous", "current", "ckout", and
  ** "next" */
  if( (zType[0]=='*' || isCheckin!=0) && 0<ridCkout ){
    if( fossil_strcmp(zTag, "current")==0 ){
      rid = ridCkout;
    }else if( fossil_strcmp(zTag, "prev")==0
              || fossil_strcmp(zTag, "previous")==0 ){
      rid = db_int(0, "SELECT pid FROM plink WHERE cid=%d AND isprim",
                   ridCkout);
    }else if( fossil_strcmp(zTag, "next")==0 ){
      rid = db_int(0, "SELECT cid FROM plink WHERE pid=%d"
                      "  ORDER BY isprim DESC, mtime DESC", ridCkout);
    }else if( isCheckin>1 && fossil_strcmp(zTag, "ckout")==0 ){
      rid = RID_CKOUT;
      assert(ridCkout>0);
      g.localOpen = ridCkout;
    }
    if( rid ) return rid;
  }

  /* Date and times */
  if( memcmp(zTag, "date:", 5)==0 ){
    zDate = fossil_expand_datetime(&zTag[5],0,1);
    if( zDate==0 ) zDate = &zTag[5];
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday(%Q,fromLocal()) AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      fossil_roundup_date(zDate), zType);
    return rid;
  }
  if( fossil_isdate(zTag) ){
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday(%Q,fromLocal()) AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      fossil_roundup_date(zTag), zType);
    if( rid) return rid;
  }

  /* Deprecated date & time formats:   "local:" + date-time and
  ** "utc:" + date-time */
  if( memcmp(zTag, "local:", 6)==0 ){
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday(%Q) AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      &zTag[6], zType);
    return rid;
  }
  if( memcmp(zTag, "utc:", 4)==0 ){
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday('%qz') AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      fossil_roundup_date(&zTag[4]), zType);
    return rid;
  }

  /* "tag:" + symbolic-name */
  if( memcmp(zTag, "tag:", 4)==0 ){
    rid = most_recent_event_with_tag(&zTag[4], zType);
    if( startOfBranch ) rid = start_of_branch(rid,1);
    return rid;
  }

  /* root:BR -> The origin of the branch named BR */
  if( strncmp(zTag, "root:", 5)==0 ){
    rid = symbolic_name_to_rid(zTag+5, zType);
    return start_of_branch(rid, 0);
  }

  /* start:BR -> The first check-in on branch named BR */
  if( strncmp(zTag, "start:", 6)==0 ){
    rid = symbolic_name_to_rid(zTag+6, zType);
    return start_of_branch(rid, 1);
  }

  /* merge-in:BR -> Most recent merge-in for the branch named BR */
  if( strncmp(zTag, "merge-in:", 9)==0 ){
    rid = symbolic_name_to_rid(zTag+9, zType);
    return start_of_branch(rid, 2);
  }

  /* symbolic-name ":" date-time */
  nTag = strlen(zTag);
  for(i=0; i<nTag-8 && zTag[i]!=':'; i++){}
  if( zTag[i]==':'
   && (fossil_isdate(&zTag[i+1]) || fossil_expand_datetime(&zTag[i+1],0,0)!=0)
  ){
    char *zDate = mprintf("%s", &zTag[i+1]);
    char *zTagBase = mprintf("%.*s", i, zTag);
    char *zXDate;
    int nDate = strlen(zDate);
    if( sqlite3_strnicmp(&zDate[nDate-3],"utc",3)==0 ){
      zDate[nDate-3] = 'z';
      zDate[nDate-2] = 0;
    }
    zXDate = fossil_expand_datetime(zDate,0,1);
    if( zXDate==0 ) zXDate = zDate;
    rid = db_int(0,
      "SELECT event.objid, max(event.mtime)"
      "  FROM tag, tagxref, event"
      " WHERE tag.tagname='sym-%q' "
      "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype>0 "
      "   AND event.objid=tagxref.rid "
      "   AND event.mtime<=julianday(%Q,fromLocal())"
      "   AND event.type GLOB '%q'",
      zTagBase, fossil_roundup_date(zXDate), zType
    );
    fossil_free(zDate);
    fossil_free(zTagBase);
    return rid;
  }

  /* Remove optional [...] */
  zXTag = zTag;
  nXTag = nTag;
  if( zXTag[0]=='[' ){
    zXTag++;
    nXTag--;
  }
  if( nXTag>0 && zXTag[nXTag-1]==']' ){
    nXTag--;
  }

  /* artifact hash or prefix */
  if( nXTag>=4 && nXTag<=HNAME_MAX && validate16(zXTag, nXTag) ){
    Stmt q;
    char zUuid[HNAME_MAX+1];
    memcpy(zUuid, zXTag, nXTag);
    zUuid[nXTag] = 0;
    canonical16(zUuid, nXTag);
    rid = 0;
    if( zType[0]=='*' ){
      db_prepare(&q, "SELECT rid FROM blob WHERE uuid GLOB '%q*'", zUuid);
    }else{
      db_prepare(&q,
        "SELECT blob.rid"
        "  FROM blob CROSS JOIN event"
        " WHERE blob.uuid GLOB '%q*'"
        "   AND event.objid=blob.rid"
        "   AND event.type GLOB '%q'",
        zUuid, zType
      );
    }
    if( db_step(&q)==SQLITE_ROW ){
      rid = db_column_int(&q, 0);
      if( db_step(&q)==SQLITE_ROW ) rid = -1;
    }
    db_finalize(&q);
    if( rid ) return rid;
  }

  if( zType[0]=='w' ){
    rid = db_int(0,
      "SELECT event.objid, max(event.mtime)"
      "  FROM tag, tagxref, event"
      " WHERE tag.tagname='wiki-%q' "
      "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype>0 "
      "   AND event.objid=tagxref.rid "
      "   AND event.type GLOB '%q'",
      zTag, zType
    );
  }else{
    rid = most_recent_event_with_tag(zTag, zType);
  }

  if( rid>0 ){
    if( startOfBranch ) rid = start_of_branch(rid,1);
    return rid;
  }

  /* Pure numeric date/time */
  zDate = fossil_expand_datetime(zTag, 0,1);
  if( zDate ){
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday(%Q,fromLocal()) AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      fossil_roundup_date(zDate), zType);
    if( rid) return rid;
  }


  /* Undocumented:  numeric tags get translated directly into the RID */
  if( memcmp(zTag, "rid:", 4)==0 ){
    zTag += 4;
    for(i=0; fossil_isdigit(zTag[i]); i++){}
    if( zTag[i]==0 ){
      if( strcmp(zType,"*")==0 ){
        rid = atoi(zTag);
      }else{
        rid = db_int(0,
          "SELECT event.objid"
          "  FROM event"
          " WHERE event.objid=%s"
          "   AND event.type GLOB '%q'", zTag /*safe-for-%s*/, zType);
      }
    }
    return rid;
  }

  /* If nothing matches and the name ends with punctuation,
  ** then the name might have originated from a URL in plain text
  ** that was incorrectly extracted from the text.  Try to remove
  ** the extra punctuation and rerun the match.
  */
  if( nTag>4
   && is_trailing_punct(zTag[nTag-1])
   && !is_trailing_punct(zTag[nTag-2])
  ){
    char *zNew = fossil_strndup(zTag, nTag-1);
    rid = symbolic_name_to_rid(zNew,zType);
    fossil_free(zNew);
  }else
  if( nTag>5
   && is_trailing_punct(zTag[nTag-1])
   && is_trailing_punct(zTag[nTag-2])
   && !is_trailing_punct(zTag[nTag-3])
  ){
    char *zNew = fossil_strndup(zTag, nTag-2);
    rid = symbolic_name_to_rid(zNew,zType);
    fossil_free(zNew);
  }
  return rid;
}

/*
** Convert a symbolic name used as an argument to the a=, b=, or c=
** query parameters of timeline into a julianday mtime value.
**
** If pzDisplay is not null, then display text for the symbolic name might
** be written into *pzDisplay.  But that is not guaranteed.
**
** If bRoundUp is true and the symbolic name is a timestamp with less
** than millisecond resolution, then the timestamp is rounding up to the
** largest millisecond consistent with that timestamp.  If bRoundUp is
** false, then the resulting time is obtained by extending the timestamp
** with zeros (hence rounding down).  Use bRoundUp==1 if the result
** will be used in mtime<=$RESULT and use bRoundUp==0 if the result
** will be used in mtime>=$RESULT.
*/
double symbolic_name_to_mtime(
  const char *z,              /* Input symbolic name */
  const char **pzDisplay,     /* Perhaps write display text here, if not NULL */
  int bRoundUp                /* Round up if true */
){
  double mtime;
  int rid;
  const char *zDate;
  if( z==0 ) return -1.0;
  if( fossil_isdate(z) ){
    mtime = db_double(0.0, "SELECT julianday(%Q,fromLocal())", z);
    if( mtime>0.0 ) return mtime;
  }
  zDate = fossil_expand_datetime(z, 1, bRoundUp);
  if( zDate!=0 ){
    mtime = db_double(0.0, "SELECT julianday(%Q,fromLocal())",
                      bRoundUp ? fossil_roundup_date(zDate) : zDate);
    if( mtime>0.0 ){
      if( pzDisplay ){
        zDate = fossil_expand_datetime(z,0,0);
        *pzDisplay = fossil_strdup(zDate);
      }
      return mtime;
    }
  }
  rid = symbolic_name_to_rid(z, "*");
  if( rid ){
    mtime = mtime_of_rid(rid, 0.0);
  }else{
    mtime = db_double(-1.0,
        "SELECT max(event.mtime) FROM event, tag, tagxref"
        " WHERE tag.tagname GLOB 'event-%q*'"
        "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype"
        "   AND event.objid=tagxref.rid",
        z
    );
  }
  return mtime;
}

/*
** This routine takes a user-entered string and tries to convert it to
** an artifact hash.
**
** We first try to treat the string as an artifact hash, or at least a
** unique prefix of an artifact hash. The input may be in mixed case.
** If we are passed such a string, this routine has the effect of
** converting the hash [prefix] to canonical form.
**
** If the input is not a hash or a hash prefix, then try to resolve
** the name as a tag.  If multiple tags match, pick the latest.
** A caller can force this routine to skip the hash case above by
** prefixing the string with "tag:", a useful property when the tag
** may be misinterpreted as a hex ASCII string. (e.g. "decade" or "facade")
**
** If the input is not a tag, then try to match it as an ISO-8601 date
** string YYYY-MM-DD HH:MM:SS and pick the nearest check-in to that date.
** If the input is of the form "date:*" then always resolve the name as
** a date. The forms "utc:*" and "local:" are deprecated.
**
** Return 0 on success.  Return 1 if the name cannot be resolved.
** Return 2 name is ambiguous.
*/
int name_to_uuid(Blob *pName, int iErrPriority, const char *zType){
  char *zName = blob_str(pName);
  int rid = symbolic_name_to_rid(zName, zType);
  if( rid<0 ){
    fossil_error(iErrPriority, "ambiguous name: %s", zName);
    return 2;
  }else if( rid==0 ){
    fossil_error(iErrPriority, "cannot resolve name: %s", zName);
    return 1;
  }else{
    blob_reset(pName);
    if( RID_CKOUT==rid ) {
      rid = g.localOpen;
    }
    db_blob(pName, "SELECT uuid FROM blob WHERE rid=%d", rid);
    return 0;
  }
}

/*
** This routine is similar to name_to_uuid() except in the form it
** takes its parameters and returns its value, and in that it does not
** treat errors as fatal. zName must be an artifact hash or prefix of
** a hash. zType is also as described for name_to_uuid(). If
** zName does not resolve, 0 is returned. If it is ambiguous, a
** negative value is returned. On success the rid is returned and
** pUuid (if it is not NULL) is set to a newly-allocated string,
** the full hash, which must eventually be free()d by the caller.
*/
int name_to_uuid2(const char *zName, const char *zType, char **pUuid){
  int rid = symbolic_name_to_rid(zName, zType);
  if((rid>0) && pUuid){
    *pUuid = (rid<RID_MAX)
      ? db_text(NULL, "SELECT uuid FROM blob WHERE rid=%d", rid)
      : NULL;
  }
  return rid;
}


/*
** name_collisions searches through events, blobs, and tickets for
** collisions of a given hash based on its length, counting only
** hashes greater than or equal to 4 hex ASCII characters (16 bits)
** in length.
*/
int name_collisions(const char *zName){
  int c = 0;         /* count of collisions for zName */
  int nLen;          /* length of zName */
  nLen = strlen(zName);
  if( nLen>=4 && nLen<=HNAME_MAX && validate16(zName, nLen) ){
    c = db_int(0,
      "SELECT"
      " (SELECT count(*) FROM ticket"
      "   WHERE tkt_uuid GLOB '%q*') +"
      " (SELECT count(*) FROM tag"
      "   WHERE tagname GLOB 'event-%q*') +"
      " (SELECT count(*) FROM blob"
      "   WHERE uuid GLOB '%q*');",
      zName, zName, zName
    );
    if( c<2 ) c = 0;
  }
  return c;
}

/*
** COMMAND: test-name-to-id
**
** Usage:  %fossil test-name-to-id [--count N] [--type ARTIFACT_TYPE] NAME
**
** Convert a NAME to a full artifact ID.  Repeat the conversion N
** times (for timing purposes) if the --count option is given.
*/
void test_name_to_id(void){
  int i;
  int n = 0;
  Blob name;
  const char *zType;

  db_must_be_within_tree();
  if( (zType = find_option("type","t",1))==0 ){
    zType = "*";
  }
  for(i=2; i<g.argc; i++){
    if( strcmp(g.argv[i],"--count")==0 && i+1<g.argc ){
      i++;
      n = atoi(g.argv[i]);
      continue;
    }
    do{
      blob_init(&name, g.argv[i], -1);
      fossil_print("%s -> ", g.argv[i]);
      if( name_to_uuid(&name, 1, zType) ){
        fossil_print("ERROR: %s\n", g.zErrMsg);
        fossil_error_reset();
      }else{
        fossil_print("%s\n", blob_buffer(&name));
      }
      blob_reset(&name);
    }while( n-- > 0 );
  }
}

/*
** Convert a name to a rid.  If the name can be any of the various forms
** accepted:
**
**   * artifact hash or prefix thereof
**   * symbolic name
**   * date
**   * label:date
**   * prev, previous
**   * next
**   * tip
**
** This routine is used by command-line routines to resolve command-line inputs
** into a rid.
*/
int name_to_typed_rid(const char *zName, const char *zType){
  int rid;

  if( zName==0 || zName[0]==0 ) return 0;
  rid = symbolic_name_to_rid(zName, zType);
  if( rid<0 ){
    fossil_fatal("ambiguous name: %s", zName);
  }else if( rid==0 ){
    fossil_fatal("cannot resolve name: %s", zName);
  }
  return rid;
}
int name_to_rid(const char *zName){
  return name_to_typed_rid(zName, "*");
}

/*
** Try to resolve zQP1 into a check-in name.  If zQP1 does not exist,
** return 0.  If zQP1 exists but cannot be resolved, then also try to
** resolve zQP2 if it exists.  If zQP1 cannot be resolved but zQP2 does
** not exist, then raise an error.  If both zQP1 and zQP2 exists but
** neither can be resolved, also raise an error.
**
** If pzPick is not a NULL pointer, then *pzPick to be the value of whichever
** query parameter ended up being used.
*/
int name_choice(const char *zQP1, const char *zQP2, const char **pzPick){
  const char *zName, *zName2;
  int rid;
  zName = P(zQP1);
  if( zName==0 || zName[0]==0 ) return 0;
  rid = symbolic_name_to_rid(zName, "ci");
  if( rid>0 ){
    if( pzPick ) *pzPick = zName;
    return rid;
  }
  if( rid<0 ){
    fossil_fatal("ambiguous name: %s", zName);
  }
  zName2 = P(zQP2);
  if( zName2==0 || zName2[0]==0 ){
    fossil_fatal("cannot resolve name: %s", zName);
  }
  if( pzPick ) *pzPick = zName2;
  return name_to_typed_rid(zName2, "ci");
}

/*
** WEBPAGE: ambiguous
** URL: /ambiguous?name=NAME&src=WEBPAGE
**
** The NAME given by the name parameter is ambiguous.  Display a page
** that shows all possible choices and let the user select between them.
**
** The src= query parameter is optional.  If omitted it defaults
** to "info".
*/
void ambiguous_page(void){
  Stmt q;
  const char *zName = P("name");
  const char *zSrc = PD("src","info");
  char *z;

  if( zName==0 || zName[0]==0 || zSrc==0 || zSrc[0]==0 ){
    fossil_redirect_home();
  }
  style_header("Ambiguous Artifact ID");
  @ <p>The artifact hash prefix <b>%h(zName)</b> is ambiguous and might
  @ mean any of the following:
  @ <ol>
  z = mprintf("%s", zName);
  canonical16(z, strlen(z));
  db_prepare(&q, "SELECT uuid, rid FROM blob WHERE uuid GLOB '%q*'", z);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    @ <li><p><a href="%R/%T(zSrc)/%!S(zUuid)">
    @ %s(zUuid)</a> -
    object_description(rid, 0, 0, 0);
    @ </p></li>
  }
  db_finalize(&q);
  db_prepare(&q,
    "   SELECT tkt_rid, tkt_uuid, title"
    "     FROM ticket, ticketchng"
    "    WHERE ticket.tkt_id = ticketchng.tkt_id"
    "      AND tkt_uuid GLOB '%q*'"
    " GROUP BY tkt_uuid"
    " ORDER BY tkt_ctime DESC", z);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zTitle = db_column_text(&q, 2);
    @ <li><p><a href="%R/%T(zSrc)/%!S(zUuid)">
    @ %s(zUuid)</a> -
    @ <ul></ul>
    @ Ticket
    hyperlink_to_version(zUuid);
    @ - %h(zTitle).
    @ <ul><li>
    object_description(rid, 0, 0, 0);
    @ </li></ul>
    @ </p></li>
  }
  db_finalize(&q);
  db_prepare(&q,
    "SELECT rid, uuid FROM"
    "  (SELECT tagxref.rid AS rid, substr(tagname, 7) AS uuid"
    "     FROM tagxref, tag WHERE tagxref.tagid = tag.tagid"
    "      AND tagname GLOB 'event-%q*') GROUP BY uuid", z);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0);
    const char* zUuid = db_column_text(&q, 1);
    @ <li><p><a href="%R/%T(zSrc)/%!S(zUuid)">
    @ %s(zUuid)</a> -
    @ <ul><li>
    object_description(rid, 0, 0, 0);
    @ </li></ul>
    @ </p></li>
  }
  @ </ol>
  db_finalize(&q);
  style_finish_page();
}

/*
** Convert the name in CGI parameter zParamName into a rid and return that
** rid.  If the CGI parameter is missing or is not a valid artifact tag,
** return 0.  If the CGI parameter is ambiguous, redirect to a page that
** shows all possibilities and do not return.
*/
int name_to_rid_www(const char *zParamName){
  int rid;
  const char *zName = P(zParamName);
#ifdef FOSSIL_ENABLE_JSON
  if(!zName && fossil_has_json()){
    zName = json_find_option_cstr(zParamName,NULL,NULL);
  }
#endif
  if( zName==0 || zName[0]==0 ) return 0;
  rid = symbolic_name_to_rid(zName, "*");
  if( rid<0 ){
    cgi_redirectf("%R/ambiguous/%T?src=%t", zName, g.zPath);
    rid = 0;
  }
  return rid;
}

/*
** Given an RID of a structural artifact, which is assumed to be
** valid, this function returns one of the CFTYPE_xxx values
** describing the record type, or 0 if the RID does not refer to an
** artifact record (as determined by reading the event table).
**
** Note that this function never returns CFTYPE_ATTACHMENT or
** CFTYPE_CLUSTER because those are not used in the event table. Thus
** it cannot be used to distinguish those artifact types from
** non-artifact file content.
**
** Potential TODO: if the rid is not found in the timeline, try to
** match it to a client file and return a hypothetical new
** CFTYPE_OPAQUE or CFTYPE_NONARTIFACT if a match is found.
*/
int whatis_rid_type(int rid){
  Stmt q = empty_Stmt;
  int type = 0;
    /* Check for entries on the timeline that reference this object */
  db_prepare(&q,
     "SELECT type FROM event WHERE objid=%d", rid);
  if( db_step(&q)==SQLITE_ROW ){
    switch( db_column_text(&q,0)[0] ){
      case 'c':  type = CFTYPE_MANIFEST; break;
      case 'w':  type = CFTYPE_WIKI;     break;
      case 'e':  type = CFTYPE_EVENT;    break;
      case 'f':  type = CFTYPE_FORUM;    break;
      case 't':  type = CFTYPE_TICKET;   break;
      case 'g':  type = CFTYPE_CONTROL;  break;
      default:   break;
    }
  }
  db_finalize(&q);
  return type;
}

/*
** A proxy for whatis_rid_type() which returns a brief string (in
** static memory) describing the record type. Returns NULL if rid does
** not refer to an artifact record (as determined by reading the event
** table). The returned string is intended to be used in headers which
** can refer to different artifact types. It is not "definitive," in
** that it does not distinguish between closely-related types like
** wiki creation, edit, and removal.
*/
char const * whatis_rid_type_label(int rid){
  char const * zType = 0;
  switch( whatis_rid_type(rid) ){
    case CFTYPE_MANIFEST: zType = "Check-in";      break;
    case CFTYPE_WIKI:     zType = "Wiki-edit";     break;
    case CFTYPE_EVENT:    zType = "Technote";      break;
    case CFTYPE_FORUM:    zType = "Forum-post";    break;
    case CFTYPE_TICKET:   zType = "Ticket-change"; break;
    case CFTYPE_CONTROL:  zType = "Tag-change";    break;
    default:   break;
  }
  return zType;
}

/*
** Flag values for whatis_rid().
*/
#if INTERFACE
#define WHATIS_VERBOSE   0x01    /* Extra output */
#define WHATIS_BRIEF     0x02    /* Omit unnecessary output */
#define WHATIS_REPO      0x04    /* Show repository name */
#define WHATIS_OMIT_UNK  0x08    /* Do not show "unknown" lines */
#endif

/*
** Generate a description of artifact "rid"
*/
void whatis_rid(int rid, int flags){
  Stmt q;
  int cnt;

  /* Basic information about the object. */
  db_prepare(&q,
     "SELECT uuid, size, datetime(mtime,toLocal()), ipaddr"
     "  FROM blob, rcvfrom"
     " WHERE rid=%d"
     "   AND rcvfrom.rcvid=blob.rcvid",
     rid);
  if( db_step(&q)==SQLITE_ROW ){
    if( flags & WHATIS_VERBOSE ){
      fossil_print("artifact:   %s (%d)\n", db_column_text(&q,0), rid);
      fossil_print("size:       %d bytes\n", db_column_int(&q,1));
      fossil_print("received:   %s from %s\n",
         db_column_text(&q, 2),
         db_column_text(&q, 3));
    }else{
      fossil_print("artifact:   %s\n", db_column_text(&q,0));
      fossil_print("size:       %d bytes\n", db_column_int(&q,1));
    }
  }
  db_finalize(&q);

  /* Report any symbolic tags on this artifact */
  db_prepare(&q,
    "SELECT substr(tagname,5)"
    "  FROM tag JOIN tagxref ON tag.tagid=tagxref.tagid"
    " WHERE tagxref.rid=%d"
    "   AND tagxref.tagtype<>0"
    "   AND tagname GLOB 'sym-*'"
    " ORDER BY 1",
    rid
  );
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPrefix = cnt++ ? ", " : "tags:       ";
    fossil_print("%s%s", zPrefix, db_column_text(&q,0));
  }
  if( cnt ) fossil_print("\n");
  db_finalize(&q);

  /* Report any HIDDEN, PRIVATE, CLUSTER, or CLOSED tags on this artifact */
  db_prepare(&q,
    "SELECT tagname"
    "  FROM tag JOIN tagxref ON tag.tagid=tagxref.tagid"
    " WHERE tagxref.rid=%d"
    "   AND tag.tagid IN (5,6,7,9)"
    " ORDER BY 1",
    rid
  );
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPrefix = cnt++ ? ", " : "raw-tags:   ";
    fossil_print("%s%s", zPrefix, db_column_text(&q,0));
  }
  if( cnt ) fossil_print("\n");
  db_finalize(&q);

  /* Check for entries on the timeline that reference this object */
  db_prepare(&q,
     "SELECT type, datetime(mtime,toLocal()),"
     "       coalesce(euser,user), coalesce(ecomment,comment)"
     "  FROM event WHERE objid=%d", rid);
  if( db_step(&q)==SQLITE_ROW ){
    const char *zType;
    switch( db_column_text(&q,0)[0] ){
      case 'c':  zType = "Check-in";       break;
      case 'w':  zType = "Wiki-edit";      break;
      case 'e':  zType = "Technote";       break;
      case 'f':  zType = "Forum-post";     break;
      case 't':  zType = "Ticket-change";  break;
      case 'g':  zType = "Tag-change";     break;
      default:   zType = "Unknown";        break;
    }
    fossil_print("type:       %s by %s on %s\n", zType, db_column_text(&q,2),
                 db_column_text(&q, 1));
    fossil_print("comment:    ");
    comment_print(db_column_text(&q,3), 0, 12, -1, get_comment_format());
    cnt++;
  }
  db_finalize(&q);

  /* Check to see if this object is used as a file in a check-in */
  db_prepare(&q,
    "SELECT filename.name, blob.uuid, datetime(event.mtime,toLocal()),"
    "       coalesce(euser,user), coalesce(ecomment,comment)"
    "  FROM mlink, filename, blob, event"
    " WHERE mlink.fid=%d"
    "   AND filename.fnid=mlink.fnid"
    "   AND event.objid=mlink.mid"
    "   AND blob.rid=mlink.mid"
    " ORDER BY event.mtime %s /*sort*/",
    rid,
    (flags & WHATIS_BRIEF) ? "LIMIT 1" : "DESC");
  while( db_step(&q)==SQLITE_ROW ){
    if( flags & WHATIS_BRIEF ){
      fossil_print("mtime:      %s\n", db_column_text(&q,2));
    }
    fossil_print("file:       %s\n", db_column_text(&q,0));
    fossil_print("            part of [%S] by %s on %s\n",
      db_column_text(&q, 1),
      db_column_text(&q, 3),
      db_column_text(&q, 2));
    fossil_print("            ");
    comment_print(db_column_text(&q,4), 0, 12, -1, get_comment_format());
    cnt++;
  }
  db_finalize(&q);

  /* Check to see if this object is used as an attachment */
  db_prepare(&q,
    "SELECT attachment.filename,"
    "       attachment.comment,"
    "       attachment.user,"
    "       datetime(attachment.mtime,toLocal()),"
    "       attachment.target,"
    "       CASE WHEN EXISTS(SELECT 1 FROM tag WHERE tagname=('tkt-'||target))"
    "            THEN 'ticket'"
    "       WHEN EXISTS(SELECT 1 FROM tag WHERE tagname=('wiki-'||target))"
    "            THEN 'wiki' END,"
    "       attachment.attachid,"
    "       (SELECT uuid FROM blob WHERE rid=attachid)"
    "  FROM attachment JOIN blob ON attachment.src=blob.uuid"
    " WHERE blob.rid=%d",
    rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("attachment: %s\n", db_column_text(&q,0));
    fossil_print("            attached to %s %s\n",
                 db_column_text(&q,5), db_column_text(&q,4));
    if( flags & WHATIS_VERBOSE ){
      fossil_print("            via %s (%d)\n",
                   db_column_text(&q,7), db_column_int(&q,6));
    }else{
      fossil_print("            via %s\n",
                   db_column_text(&q,7));
    }
    fossil_print("            by user %s on %s\n",
                 db_column_text(&q,2), db_column_text(&q,3));
    fossil_print("            ");
    comment_print(db_column_text(&q,1), 0, 12, -1, get_comment_format());
    cnt++;
  }
  db_finalize(&q);

  /* If other information available, try to describe the object */
  if( cnt==0 ){
    char *zWhere = mprintf("=%d", rid);
    char *zDesc;
    describe_artifacts(zWhere);
    free(zWhere);
    zDesc = db_text(0,
       "SELECT printf('%%-12s%%s %%s',type||':',summary,substr(ref,1,16))"
       "  FROM description WHERE rid=%d", rid);
    fossil_print("%s\n", zDesc);
    fossil_free(zDesc);
  }
}

/*
** Generate a description of artifact from it symbolic name.
*/
void whatis_artifact(
    const char *zName,    /* Symbolic name or full hash */
    const char *zFileName,/* Optional: original filename (in file mode) */
    const char *zType,    /* Artifact type filter */
    int mFlags            /* WHATIS_* flags */
){
  int rid = symbolic_name_to_rid(zName, zType);
  if( rid<0 ){
    Stmt q;
    int cnt = 0;
    if( mFlags & WHATIS_REPO ){
      fossil_print("\nrepository: %s\n", g.zRepositoryName);
    }
    if( zFileName ){
      fossil_print("%-12s%s\n", "name:", zFileName);
    }
    fossil_print("%-12s%s (ambiguous)\n", "hash:", zName);
    db_prepare(&q,
        "SELECT rid FROM blob WHERE uuid>=lower(%Q) AND uuid<(lower(%Q)||'z')",
        zName, zName
        );
    while( db_step(&q)==SQLITE_ROW ){
      if( cnt++ ) fossil_print("%12s---- meaning #%d ----\n", " ", cnt);
      whatis_rid(db_column_int(&q, 0), mFlags);
    }
    db_finalize(&q);
  }else if( rid==0 ){
    if( (mFlags & WHATIS_OMIT_UNK)==0 ){
                 /* 0123456789 12 */
      if( zFileName ){
        fossil_print("%-12s%s\n", "name:", zFileName);
      }
      fossil_print("unknown:    %s\n", zName);
    }
  }else{
    if( mFlags & WHATIS_REPO ){
      fossil_print("\nrepository: %s\n", g.zRepositoryName);
    }
    if( zFileName ){
      zName = zFileName;
    }
    fossil_print("%-12s%s\n", "name:", zName);
    whatis_rid(rid, mFlags);
  }
}

/*
** COMMAND: whatis*
**
** Usage: %fossil whatis NAME
**
** Resolve the symbol NAME into its canonical artifact hash
** artifact name and provide a description of what role that artifact
** plays.
**
** Options:
**    -f|--file            Find artifacts with the same hash as file NAME.
**                         If NAME is "-", read content from standard input.
**    -q|--quiet           Show nothing if NAME is not found
**    --type TYPE          Only find artifacts of TYPE (one of: 'ci', 't',
**                         'w', 'g', or 'e')
**    -v|--verbose         Provide extra information (such as the RID)
*/
void whatis_cmd(void){
  int mFlags = 0;
  int fileFlag;
  int i;
  const char *zType = 0;
  db_find_and_open_repository(0,0);
  if( find_option("verbose","v",0)!=0 ){
    mFlags |= WHATIS_VERBOSE;
  }
  if( find_option("quiet","q",0)!=0 ){
    mFlags |= WHATIS_OMIT_UNK | WHATIS_REPO;
  }
  fileFlag = find_option("file","f",0)!=0;
  zType = find_option("type",0,1);

  /* We should be done with options.. */
  verify_all_options();

  if( g.argc<3 ) usage("NAME ...");
  for(i=2; i<g.argc; i++){
    const char *zName = g.argv[i];
    if( i>2 ) fossil_print("%.79c\n",'-');
    if( fileFlag ){
      Blob in;
      Blob hash = empty_blob;
      const char *zHash;
      /* Always follow symlinks (when applicable) */
      blob_read_from_file(&in, zName, ExtFILE);

      /* First check the auxiliary hash to see if there is already an artifact
      ** that uses the auxiliary hash name */
      hname_hash(&in, 1, &hash);
      zHash = (const char*)blob_str(&hash);
      if( fast_uuid_to_rid(zHash)==0 ){
        /* No existing artifact with the auxiliary hash name.  Therefore, use
        ** the primary hash name. */
        blob_reset(&hash);
        hname_hash(&in, 0, &hash);
        zHash = (const char*)blob_str(&hash);
      }
      whatis_artifact(zHash, zName, zType, mFlags);
      blob_reset(&hash);
    }else{
      whatis_artifact(zName, 0, zType, mFlags);
    }
  }
}

/*
** COMMAND: test-whatis-all
**
** Usage: %fossil test-whatis-all
**
** Show "whatis" information about every artifact in the repository
*/
void test_whatis_all_cmd(void){
  Stmt q;
  int cnt = 0;
  db_find_and_open_repository(0,0);
  db_prepare(&q, "SELECT rid FROM blob ORDER BY rid");
  while( db_step(&q)==SQLITE_ROW ){
    if( cnt++ ) fossil_print("%.79c\n", '-');
    whatis_rid(db_column_int(&q,0), 1);
  }
  db_finalize(&q);
}


/*
** COMMAND: test-ambiguous
**
** Usage: %fossil test-ambiguous [--minsize N]
**
** Show a list of ambiguous artifact hash abbreviations of N characters or
** more where N defaults to 4.  Change N to a different value using
** the "--minsize N" command-line option.
*/
void test_ambiguous_cmd(void){
  Stmt q, ins;
  int i;
  int minSize = 4;
  const char *zMinsize;
  char zPrev[100];
  db_find_and_open_repository(0,0);
  zMinsize = find_option("minsize",0,1);
  if( zMinsize && atoi(zMinsize)>0 ) minSize = atoi(zMinsize);
  db_multi_exec("CREATE TEMP TABLE dups(uuid, cnt)");
  db_prepare(&ins,"INSERT INTO dups(uuid) VALUES(substr(:uuid,1,:cnt))");
  db_prepare(&q,
    "SELECT uuid FROM blob "
    "UNION "
    "SELECT substr(tagname,7) FROM tag WHERE tagname GLOB 'event-*' "
    "UNION "
    "SELECT tkt_uuid FROM ticket "
    "ORDER BY 1"
  );
  zPrev[0] = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    for(i=0; zUuid[i]==zPrev[i] && zUuid[i]!=0; i++){}
    if( i>=minSize ){
      db_bind_int(&ins, ":cnt", i);
      db_bind_text(&ins, ":uuid", zUuid);
      db_step(&ins);
      db_reset(&ins);
    }
    sqlite3_snprintf(sizeof(zPrev), zPrev, "%s", zUuid);
  }
  db_finalize(&ins);
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid FROM dups ORDER BY length(uuid) DESC, uuid");
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%s\n", db_column_text(&q, 0));
  }
  db_finalize(&q);
}

/*
** Schema for the description table
*/
static const char zDescTab[] =
@ CREATE TEMP TABLE IF NOT EXISTS description(
@   rid INTEGER PRIMARY KEY,       -- RID of the object
@   uuid TEXT,                     -- hash of the object
@   ctime DATETIME,                -- Time of creation
@   isPrivate BOOLEAN DEFAULT 0,   -- True for unpublished artifacts
@   type TEXT,                     -- file, check-in, wiki, ticket, etc.
@   rcvid INT,                     -- When the artifact was received
@   summary TEXT,                  -- Summary comment for the object
@   ref TEXT                       -- hash of an object to link against
@ );
@ CREATE INDEX IF NOT EXISTS desctype
@   ON description(summary) WHERE summary='unknown';
;

/*
** Attempt to describe all phantom artifacts.  The artifacts are
** already loaded into the description table and have summary='unknown'.
** This routine attempts to generate a better summary, and possibly
** fill in the ref field.
*/
static void describe_unknown_artifacts(){
  /* Try to figure out the origin of unknown artifacts */
  db_multi_exec(
    "REPLACE INTO description(rid,uuid,isPrivate,type,summary,ref)\n"
    "  SELECT description.rid, description.uuid, isPrivate, type,\n"
    "         CASE WHEN plink.isprim THEN '' ELSE 'merge ' END ||\n"
    "         'parent of check-in', blob.uuid\n"
    "    FROM description, plink, blob\n"
    "   WHERE description.summary='unknown'\n"
    "     AND plink.pid=description.rid\n"
    "     AND blob.rid=plink.cid;"
  );
  db_multi_exec(
    "REPLACE INTO description(rid,uuid,isPrivate,type,summary,ref)\n"
    "  SELECT description.rid, description.uuid, isPrivate, type,\n"
    "         'child of check-in', blob.uuid\n"
    "    FROM description, plink, blob\n"
    "   WHERE description.summary='unknown'\n"
    "     AND plink.cid=description.rid\n"
    "     AND blob.rid=plink.pid;"
  );
  db_multi_exec(
    "REPLACE INTO description(rid,uuid,isPrivate,type,summary,ref)\n"
    "  SELECT description.rid, description.uuid, isPrivate, type,\n"
    "         'check-in referenced by \"'||tag.tagname ||'\" tag',\n"
    "         blob.uuid\n"
    "    FROM description, tagxref, tag, blob\n"
    "   WHERE description.summary='unknown'\n"
    "     AND tagxref.origid=description.rid\n"
    "     AND tag.tagid=tagxref.tagid\n"
    "     AND blob.rid=tagxref.srcid;"
  );
  db_multi_exec(
    "REPLACE INTO description(rid,uuid,isPrivate,type,summary,ref)\n"
    "  SELECT description.rid, description.uuid, isPrivate, type,\n"
    "         'file \"'||filename.name||'\"',\n"
    "         blob.uuid\n"
    "    FROM description, mlink, filename, blob\n"
    "   WHERE description.summary='unknown'\n"
    "     AND mlink.fid=description.rid\n"
    "     AND blob.rid=mlink.mid\n"
    "     AND filename.fnid=mlink.fnid;"
  );
  if( !db_exists("SELECT 1 FROM description WHERE summary='unknown'") ){
    return;
  }
  add_content_sql_commands(g.db);
  db_multi_exec(
    "REPLACE INTO description(rid,uuid,isPrivate,type,summary,ref)\n"
    "  SELECT description.rid, description.uuid, isPrivate, type,\n"
    "         'referenced by cluster', blob.uuid\n"
    "    FROM description, tagxref, blob\n"
    "   WHERE description.summary='unknown'\n"
    "     AND tagxref.tagid=(SELECT tagid FROM tag WHERE tagname='cluster')\n"
    "     AND blob.rid=tagxref.rid\n"
    "     AND CAST(content(blob.uuid) AS text)"
    "                   GLOB ('*M '||description.uuid||'*');"
  );
}

/*
** Create the description table if it does not already exists.
** Populate fields of this table with descriptions for all artifacts
** whose RID matches the SQL expression in zWhere.
*/
void describe_artifacts(const char *zWhere){
  db_multi_exec("%s", zDescTab/*safe-for-%s*/);

  /* Describe check-ins */
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, event.mtime, 'checkin',\n"
          " 'check-in to '\n"
          " ||  coalesce((SELECT value FROM tagxref WHERE tagid=%d"
                     "   AND tagtype>0 AND tagxref.rid=blob.rid),'trunk')\n"
          " || ' by ' || coalesce(event.euser,event.user)\n"
          " || ' on ' || strftime('%%Y-%%m-%%d %%H:%%M',event.mtime)\n"
    "  FROM event, blob\n"
    " WHERE (event.objid %s) AND event.type='ci'\n"
    "   AND event.objid=blob.rid;",
    TAG_BRANCH, zWhere /*safe-for-%s*/
  );

  /* Describe files */
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, event.mtime,"
    "       'file', 'file '||filename.name\n"
    "  FROM mlink, blob, event, filename\n"
    " WHERE (mlink.fid %s)\n"
    "   AND mlink.mid=event.objid\n"
    "   AND filename.fnid=mlink.fnid\n"
    "   AND mlink.fid=blob.rid;",
    zWhere /*safe-for-%s*/
  );

  /* Describe tags */
  db_multi_exec(
   "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, tagxref.mtime, 'tag',\n"
    "     'tag '||substr((SELECT uuid FROM blob WHERE rid=tagxref.rid),1,16)\n"
    "  FROM tagxref, blob\n"
    " WHERE (tagxref.srcid %s) AND tagxref.srcid!=tagxref.rid\n"
    "   AND tagxref.srcid=blob.rid;",
    zWhere /*safe-for-%s*/
  );

  /* Cluster artifacts */
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, rcvfrom.mtime,"
    "       'cluster', 'cluster'\n"
    "  FROM tagxref, blob, rcvfrom\n"
    " WHERE (tagxref.rid %s)\n"
    "   AND tagxref.tagid=(SELECT tagid FROM tag WHERE tagname='cluster')\n"
    "   AND blob.rid=tagxref.rid"
    "   AND rcvfrom.rcvid=blob.rcvid;",
    zWhere /*safe-for-%s*/
  );

  /* Ticket change artifacts */
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, tagxref.mtime, 'ticket',\n"
    "       'ticket '||substr(tag.tagname,5,21)\n"
    "  FROM tagxref, tag, blob\n"
    " WHERE (tagxref.rid %s)\n"
    "   AND tag.tagid=tagxref.tagid\n"
    "   AND tag.tagname GLOB 'tkt-*'"
    "   AND blob.rid=tagxref.rid;",
    zWhere /*safe-for-%s*/
  );

  /* Wiki edit artifacts */
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, tagxref.mtime, 'wiki',\n"
    "       printf('wiki \"%%s\"',substr(tag.tagname,6))\n"
    "  FROM tagxref, tag, blob\n"
    " WHERE (tagxref.rid %s)\n"
    "   AND tag.tagid=tagxref.tagid\n"
    "   AND tag.tagname GLOB 'wiki-*'"
    "   AND blob.rid=tagxref.rid;",
    zWhere /*safe-for-%s*/
  );

  /* Event edit artifacts */
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, tagxref.mtime, 'event',\n"
    "       'event '||substr(tag.tagname,7)\n"
    "  FROM tagxref, tag, blob\n"
    " WHERE (tagxref.rid %s)\n"
    "   AND tag.tagid=tagxref.tagid\n"
    "   AND tag.tagname GLOB 'event-*'"
    "   AND blob.rid=tagxref.rid;",
    zWhere /*safe-for-%s*/
  );

  /* Attachments */
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, attachment.mtime,"
    "       'attach-control',\n"
    "       'attachment-control for '||attachment.filename\n"
    "  FROM attachment, blob\n"
    " WHERE (attachment.attachid %s)\n"
    "   AND blob.rid=attachment.attachid",
    zWhere /*safe-for-%s*/
  );
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
    "SELECT blob.rid, blob.uuid, blob.rcvid, attachment.mtime, 'attachment',\n"
    "       'attachment '||attachment.filename\n"
    "  FROM attachment, blob\n"
    " WHERE (blob.rid %s)\n"
    "   AND blob.rid NOT IN (SELECT rid FROM description)\n"
    "   AND blob.uuid=attachment.src",
    zWhere /*safe-for-%s*/
  );

  /* Forum posts */
  if( db_table_exists("repository","forumpost") ){
    db_multi_exec(
      "INSERT OR IGNORE INTO description(rid,uuid,rcvid,ctime,type,summary)\n"
      "SELECT postblob.rid, postblob.uuid, postblob.rcvid,"
      "       forumpost.fmtime, 'forumpost',\n"
      "       CASE WHEN fpid=froot THEN 'forum-post '\n"
      "            ELSE 'forum-reply-to ' END || substr(rootblob.uuid,1,14)\n"
      "  FROM forumpost, blob AS postblob, blob AS rootblob\n"
      " WHERE (forumpost.fpid %s)\n"
      "   AND postblob.rid=forumpost.fpid"
      "   AND rootblob.rid=forumpost.froot",
      zWhere /*safe-for-%s*/
    );
  }

  /* Mark all other artifacts as "unknown" for now */
  db_multi_exec(
    "INSERT OR IGNORE INTO description(rid,uuid,rcvid,type,summary)\n"
    "SELECT blob.rid, blob.uuid,blob.rcvid,\n"
    "       CASE WHEN EXISTS(SELECT 1 FROM phantom WHERE rid=blob.rid)\n"
           " THEN 'phantom' ELSE '' END,\n"
    "       'unknown'\n"
    "  FROM blob\n"
    " WHERE (blob.rid %s)\n"
    "   AND (blob.rid NOT IN (SELECT rid FROM description));",
    zWhere /*safe-for-%s*/
  );

  /* Mark private elements */
  db_multi_exec(
   "UPDATE description SET isPrivate=1 WHERE rid IN private"
  );

  if( db_exists("SELECT 1 FROM description WHERE summary='unknown'") ){
    describe_unknown_artifacts();
  }
}

/*
** Print the content of the description table on stdout.
**
** The description table is computed using the WHERE clause zWhere if
** the zWhere parameter is not NULL.  If zWhere is NULL, then this
** routine assumes that the description table already exists and is
** populated and merely prints the contents.
*/
int describe_artifacts_to_stdout(const char *zWhere, const char *zLabel){
  Stmt q;
  int cnt = 0;
  if( zWhere!=0 ) describe_artifacts(zWhere);
  db_prepare(&q,
    "SELECT uuid, summary, coalesce(ref,''), isPrivate\n"
    "  FROM description\n"
    " ORDER BY ctime, type;"
  );
  while( db_step(&q)==SQLITE_ROW ){
    if( zLabel ){
      fossil_print("%s\n", zLabel);
      zLabel = 0;
    }
    fossil_print("  %.16s %s %s", db_column_text(&q,0),
           db_column_text(&q,1), db_column_text(&q,2));
    if( db_column_int(&q,3) ) fossil_print(" (private)");
    fossil_print("\n");
    cnt++;
  }
  db_finalize(&q);
  if( zWhere!=0 ) db_multi_exec("DELETE FROM description;");
  return cnt;
}

/*
** COMMAND: test-describe-artifacts
**
** Usage: %fossil test-describe-artifacts [--from S] [--count N]
**
** Display a one-line description of every artifact.
*/
void test_describe_artifacts_cmd(void){
  int iFrom = 0;
  int iCnt = 1000000;
  const char *z;
  char *zRange;
  db_find_and_open_repository(0,0);
  z = find_option("from",0,1);
  if( z ) iFrom = atoi(z);
  z = find_option("count",0,1);
  if( z ) iCnt = atoi(z);
  zRange = mprintf("BETWEEN %d AND %d", iFrom, iFrom+iCnt-1);
  describe_artifacts_to_stdout(zRange, 0);
}

/*
** WEBPAGE: bloblist
**
** Return a page showing all artifacts in the repository.  Query parameters:
**
**   n=N         Show N artifacts
**   s=S         Start with artifact number S
**   priv        Show only unpublished or private artifacts
**   phan        Show only phantom artifacts
**   hclr        Color code hash types (SHA1 vs SHA3)
**   recent      Show the most recent N artifacts
*/
void bloblist_page(void){
  Stmt q;
  int s = atoi(PD("s","0"));
  int n = atoi(PD("n","5000"));
  int mx = db_int(0, "SELECT max(rid) FROM blob");
  int privOnly = PB("priv");
  int phantomOnly = PB("phan");
  int hashClr = PB("hclr");
  int bRecent = PB("recent");
  int bUnclst = PB("unclustered");
  char *zRange;
  char *zSha1Bg;
  char *zSha3Bg;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  cgi_check_for_malice();
  style_header("List Of Artifacts");
  style_submenu_element("250 Largest", "bigbloblist");
  if( bRecent==0 || n!=250 ){
    style_submenu_element("Recent","bloblist?n=250&recent");
  }
  if( bUnclst==0 ){
    style_submenu_element("Unclustered","bloblist?unclustered");
  }
  if( g.perm.Admin ){
    style_submenu_element("Xfer Log", "rcvfromlist");
  }
  if( !phantomOnly ){
    style_submenu_element("Phantoms", "bloblist?phan");
  }
  style_submenu_element("Clusters","clusterlist");
  if( g.perm.Private || g.perm.Admin ){
    if( !privOnly ){
      style_submenu_element("Private", "bloblist?priv");
    }
  }else{
    privOnly = 0;
  }
  if( g.perm.Write ){
    style_submenu_element("Artifact Stats", "artifact_stats");
  }
  if( !privOnly && !phantomOnly && mx>n && P("s")==0 && !bRecent && !bUnclst ){
    int i;
    @ <p>Select a range of artifacts to view:</p>
    @ <ul>
    for(i=1; i<=mx; i+=n){
      @ <li> %z(href("%R/bloblist?s=%d&n=%d",i,n))
      @ %d(i)..%d(i+n-1<mx?i+n-1:mx)</a>
    }
    @ <li> %z(href("%R/bloblist?n=250&recent"))250 most recent</a>
    @ <li> %z(href("%R/bloblist?unclustered"))All unclustered</a>
    @ </ul>
    style_finish_page();
    return;
  }
  if( phantomOnly || privOnly || mx>n ){
    style_submenu_element("Index", "bloblist");
  }
  if( privOnly ){
    @ <h2>Private Artifacts</h2>
    zRange = mprintf("IN private");
  }else if( phantomOnly ){
    @ <h2>Phantom Artifacts</h2>
    zRange = mprintf("IN phantom");
  }else if( bUnclst ){
    @ <h2>Unclustered Artifacts</h2>
    zRange = mprintf("IN unclustered");
  }else if( bRecent ){
    @ <h2>%d(n) Most Recent Artifacts</h2>
    zRange = mprintf(">=(SELECT rid FROM blob"
                     " ORDER BY rid DESC LIMIT 1 OFFSET %d)",n);
  }else{
    zRange = mprintf("BETWEEN %d AND %d", s, s+n-1);
  }
  describe_artifacts(zRange);
  fossil_free(zRange);
  db_prepare(&q,
         /*   0     1        2          3               4    5      6 */
    "SELECT rid, uuid, summary, isPrivate, type='phantom', ref, rcvid, "
    "  datetime(rcvfrom.mtime)"
    "  FROM description LEFT JOIN rcvfrom USING(rcvid)"
    " ORDER BY rid %s",
    ((bRecent||bUnclst)?"DESC":"ASC")/*safe-for-%s*/
  );
  if( skin_detail_boolean("white-foreground") ){
    zSha1Bg = "#714417";
    zSha3Bg = "#177117";
  }else{
    zSha1Bg = "#ebffb0";
    zSha3Bg = "#b0ffb0";
  }
  @ <table cellpadding="2" cellspacing="0" border="1">
  @ <tr><th>RID<th>Hash<th>Received<th>Description<th>Ref<th>Remarks
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q,0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDesc = db_column_text(&q, 2);
    int isPriv = db_column_int(&q,3);
    int isPhantom = db_column_int(&q,4);
    const char *zRef = db_column_text(&q,5);
    const char *zDate = db_column_text(&q,7);
    if( isPriv && !isPhantom && !g.perm.Private && !g.perm.Admin ){
      /* Don't show private artifacts to users without Private (x) permission */
      continue;
    }
    if( hashClr ){
      const char *zClr = db_column_bytes(&q,1)>40 ? zSha3Bg : zSha1Bg;
      @ <tr style='background-color:%s(zClr);'><td align="right">%d(rid)</td>
    }else{
      @ <tr><td align="right">%d(rid)</td>
    }
    @ <td>&nbsp;%z(href("%R/info/%!S",zUuid))%S(zUuid)</a>&nbsp;</td>
    if( g.perm.Admin ){
      int rcvid = db_column_int(&q, 6);
      @ <td><a href='%R/rcvfrom?rcvid=%d(rcvid)'>%h(zDate)</a>
    }else{
      @ <td>%h(zDate)
    }
    @ <td align="left">%h(zDesc)</td>
    if( zRef && zRef[0] ){
      @ <td>%z(href("%R/info/%!S",zRef))%S(zRef)</a>
    }else{
      @ <td>&nbsp;
    }
    if( isPriv || isPhantom ){
      if( isPriv==0 ){
        @ <td>phantom</td>
      }else if( isPhantom==0 ){
        @ <td>private</td>
      }else{
        @ <td>private,phantom</td>
      }
    }else{
      @ <td>&nbsp;
    }
    @ </tr>
  }
  @ </table>
  db_finalize(&q);
  style_finish_page();
}

/*
** Output HTML that shows a table of all public phantoms.
*/
void table_of_public_phantoms(void){
  Stmt q;
  char *zRange;
  double rNow;
  zRange = mprintf("IN (SELECT rid FROM phantom EXCEPT"
                   " SELECT rid FROM private)");
  describe_artifacts(zRange);
  fossil_free(zRange);
  db_prepare(&q,
    "SELECT rid, uuid, summary, ref,"
    "  (SELECT mtime FROM blob, rcvfrom"
    "    WHERE blob.uuid=ref AND rcvfrom.rcvid=blob.rcvid)"
    "  FROM description ORDER BY rid"
  );
  rNow = db_double(0.0, "SELECT julianday('now')");
  @ <table cellpadding="2" cellspacing="0" border="1">
  @ <tr><th>RID<th>Description<th>Source<th>Age
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q,0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDesc = db_column_text(&q, 2);
    const char *zRef = db_column_text(&q,3);
    double mtime = db_column_double(&q,4);
    @ <tr><td valign="top">%d(rid)</td>
    @ <td valign="top" align="left">%h(zUuid)<br>%h(zDesc)</td>
    if( zRef && zRef[0] ){
      @ <td valign="top">%z(href("%R/info/%!S",zRef))%!S(zRef)</a>
      if( mtime>0 ){
        char *zAge = human_readable_age(rNow - mtime);
        @ <td valign="top">%h(zAge)
        fossil_free(zAge);
      }else{
        @ <td>&nbsp;
      }
    }else{
      @ <td>&nbsp;<td>&nbsp;
    }
    @ </tr>
  }
  @ </table>
  db_finalize(&q);
}

/*
** WEBPAGE: phantoms
**
** Show a list of all "phantom" artifacts that are not marked as "private".
**
** A "phantom" artifact is an artifact whose hash named appears in some
** artifact but whose content is unknown.  For example, if a manifest
** references a particular SHA3 hash of a file, but that SHA3 hash is
** not on the shunning list and is not in the database, then the file
** is a phantom.  We know it exists, but we do not know its content.
**
** Whenever a sync occurs, both each party looks at its phantom list
** and for every phantom that is not also marked private, it asks the
** other party to send it the content.  This mechanism helps keep all
** repositories synced up.
**
** This page is similar to the /bloblist page in that it lists artifacts.
** But this page is a special case in that it only shows phantoms that
** are not private.  In other words, this page shows all phantoms that
** generate extra network traffic on every sync request.
*/
void phantom_list_page(void){
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_header("Public Phantom Artifacts");
  if( g.perm.Admin ){
    style_submenu_element("Xfer Log", "rcvfromlist");
    style_submenu_element("Artifact List", "bloblist");
  }
  if( g.perm.Write ){
    style_submenu_element("Artifact Stats", "artifact_stats");
  }
  table_of_public_phantoms();
  style_finish_page();
}

/*
** WEBPAGE: bigbloblist
**
** Return a page showing the largest artifacts in the repository in order
** of decreasing size.
**
**   n=N         Show the top N artifacts (default: 250)
*/
void bigbloblist_page(void){
  Stmt q;
  int n = atoi(PD("n","250"));

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  if( g.perm.Admin ){
    style_submenu_element("Xfer Log", "rcvfromlist");
  }
  if( g.perm.Write ){
    style_submenu_element("Artifact Stats", "artifact_stats");
  }
  style_submenu_element("All Artifacts", "bloblist");
  style_header("%d Largest Artifacts", n);
  db_multi_exec(
    "CREATE TEMP TABLE toshow(rid INTEGER PRIMARY KEY);"
    "INSERT INTO toshow(rid)"
    "  SELECT rid FROM blob"
    "   ORDER BY length(content) DESC"
    "   LIMIT %d;", n
  );
  describe_artifacts("IN toshow");
  db_prepare(&q,
    "SELECT description.rid, description.uuid, description.summary,"
    "       length(blob.content), coalesce(delta.srcid,''),"
    "       datetime(description.ctime)"
    "  FROM description, blob LEFT JOIN delta ON delta.rid=blob.rid"
    " WHERE description.rid=blob.rid"
    " ORDER BY length(content) DESC"
  );
  @ <table cellpadding="2" cellspacing="0" border="1" \
  @  class='sortable' data-column-types='NnnttT' data-init-sort='0'>
  @ <thead><tr><th align="right">Size<th align="right">RID
  @ <th align="right">From<th>Hash<th>Description<th>Date</tr></thead>
  @ <tbody>
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q,0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDesc = db_column_text(&q, 2);
    int sz = db_column_int(&q,3);
    const char *zSrcId = db_column_text(&q,4);
    const char *zDate = db_column_text(&q,5);
    @ <tr><td align="right">%d(sz)</td>
    @ <td align="right">%d(rid)</td>
    @ <td align="right">%s(zSrcId)</td>
    @ <td>&nbsp;%z(href("%R/info/%!S",zUuid))%S(zUuid)</a>&nbsp;</td>
    @ <td align="left">%h(zDesc)</td>
    @ <td align="left">%z(href("%R/timeline?c=%T",zDate))%s(zDate)</a></td>
    @ </tr>
  }
  @ </tbody></table>
  db_finalize(&q);
  style_table_sorter();
  style_finish_page();
}

/*
** WEBPAGE: deltachain
**
** Usage:  /deltachain/RID
**
** The RID query parameter is required.  Generate a page with a table
** showing storage characteristics of RID and other artifacts that are
** derived from RID via delta.
*/
void deltachain_page(void){
  Stmt q;
  int id = atoi(PD("name","0"));
  int top;
  i64 nStored = 0;
  i64 nExpanded = 0;

  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  top = db_int(id,
    "WITH RECURSIVE chain(aa,bb) AS (\n"
    "  SELECT rid, srcid FROM delta WHERE rid=%d\n"
    "  UNION ALL\n"
    "  SELECT bb, delta.srcid"
    "    FROM chain LEFT JOIN delta ON delta.rid=bb"
    "   WHERE bb IS NOT NULL\n"
    ")\n"
    "SELECT aa FROM chain WHERE bb IS NULL",
    id
  );
  style_header("Delta Chain Containing Artifact %d", id);
  db_multi_exec(
    "CREATE TEMP TABLE toshow(rid INT, gen INT);\n"
    "WITH RECURSIVE tx(id,px) AS (\n"
    "  VALUES(%d,0)\n"
    "  UNION ALL\n"
    "  SELECT delta.rid, px+1 FROM tx, delta where delta.srcid=tx.id\n"
    "  ORDER BY 2\n"
    ") "
    "INSERT INTO toshow(rid,gen) SELECT id,px FROM tx;",
    top
  );
  db_multi_exec("CREATE INDEX toshow_rid ON toshow(rid);");
  describe_artifacts("IN (SELECT rid FROM toshow)");
  db_prepare(&q,
    "SELECT description.rid, description.uuid, description.summary,"
    "       length(blob.content), coalesce(delta.srcid,''),"
    "       datetime(description.ctime), toshow.gen, blob.size"
    "  FROM description, toshow, blob LEFT JOIN delta ON delta.rid=blob.rid"
    " WHERE description.rid=blob.rid"
    "   AND toshow.rid=description.rid"
    " ORDER BY toshow.gen, description.ctime"
  );
  @ <table cellpadding="2" cellspacing="0" border="1" \
  @  class='sortable' data-column-types='nNnnttT' data-init-sort='0'>
  @ <thead><tr><th align="right">Level</th>
  @ <th align="right">Size<th align="right">RID
  @ <th align="right">From<th>Hash<th>Description<th>Date</tr></thead>
  @ <tbody>
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q,0);
    const char *zUuid = db_column_text(&q, 1);
    const char *zDesc = db_column_text(&q, 2);
    int sz = db_column_int(&q,3);
    const char *zSrcId = db_column_text(&q,4);
    const char *zDate = db_column_text(&q,5);
    int gen = db_column_int(&q,6);
    nExpanded += db_column_int(&q,7);
    nStored += sz;
    @ <tr><td align="right">%d(gen)</td>
    @ <td align="right">%d(sz)</td>
    if( rid==id ){
      @ <td align="right"><b>%d(rid)</b></td>
    }else{
      @ <td align="right">%d(rid)</td>
    }
    @ <td align="right">%s(zSrcId)</td>
    @ <td>&nbsp;%z(href("%R/info/%!S",zUuid))%S(zUuid)</a>&nbsp;</td>
    @ <td align="left">%h(zDesc)</td>
    @ <td align="left">%z(href("%R/timeline?c=%T",zDate))%s(zDate)</a></td>
    @ </tr>
  }
  @ </tbody></table>
  db_finalize(&q);
  style_table_sorter();
  @ <p>
  @ <table border="0" cellspacing="0" cellpadding="0">
  @ <tr><td>Bytes of content</td><td>&nbsp;&nbsp;&nbsp;</td>
  @     <td align="right">%,lld(nExpanded)</td></tr>
  @ <tr><td>Bytes stored in repository</td><td></td>
  @      <td align="right">%,lld(nStored)</td>
  @ </table>
  @ </p>
  style_finish_page();
}

/*
** COMMAND: test-unsent
**
** Usage: %fossil test-unsent
**
** Show all artifacts in the unsent table
*/
void test_unsent_cmd(void){
  db_find_and_open_repository(0,0);
  describe_artifacts_to_stdout("IN unsent", 0);
}

/*
** COMMAND: test-unclustered
**
** Usage: %fossil test-unclustered
**
** Show all artifacts in the unclustered table
*/
void test_unclusterd_cmd(void){
  db_find_and_open_repository(0,0);
  describe_artifacts_to_stdout("IN unclustered", 0);
}

/*
** COMMAND: test-phantoms
**
** Usage: %fossil test-phantoms
**
** Show all phantom artifacts.  A phantom artifact is one for which there
** is no content. Options:
**
**   --count            Show only a count of the number of phantoms.
**   --delta            Show all delta-phantoms.  A delta-phantom is a
**                      artifact for which there is a delta but the delta
**                      source is a phantom.
**   --list             Just list the phantoms.  Do not try to describe them.
*/
void test_phatoms_cmd(void){
  int bDelta;
  int bList;
  int bCount;
  unsigned nPhantom = 0;
  unsigned nDeltaPhantom = 0;
  db_find_and_open_repository(0,0);
  bDelta = find_option("delta", 0, 0)!=0;
  bList = find_option("list", 0, 0)!=0;
  bCount = find_option("count", 0, 0)!=0;
  verify_all_options();
  if( bList || bCount ){
    Stmt q1, q2;
    db_prepare(&q1, "SELECT rid, uuid FROM blob WHERE size<0");
    while( db_step(&q1)==SQLITE_ROW ){
      int rid = db_column_int(&q1, 0);
      nPhantom++;
      if( !bCount ){
        fossil_print("%S (%d)\n", db_column_text(&q1,1), rid);
      }
      db_prepare(&q2,
        "WITH RECURSIVE deltasof(rid) AS ("
        "  SELECT rid FROM delta WHERE srcid=%d"
        "  UNION"
        "  SELECT delta.rid FROM deltasof, delta"
        "    WHERE delta.srcid=deltasof.rid)"
        "SELECT deltasof.rid, blob.uuid FROM deltasof LEFT JOIN blob"
        "    ON blob.rid=deltasof.rid", rid
      );
      while( db_step(&q2)==SQLITE_ROW ){
        nDeltaPhantom++;
        if( !bCount ){
          fossil_print("   %S (%d)\n", db_column_text(&q2,1),
                                       db_column_int(&q2,0));
        }
      }
      db_finalize(&q2);
    }
    db_finalize(&q1);
    if( nPhantom ){
      fossil_print("Phantoms: %u    Delta-phantoms: %u\n",
                    nPhantom, nDeltaPhantom);
    }
  }else if( bDelta ){
    describe_artifacts_to_stdout(
       "IN (WITH RECURSIVE delta_phantom(rid) AS (\n"
       "      SELECT delta.rid FROM blob, delta\n"
       "       WHERE blob.size<0 AND delta.srcid=blob.rid\n"
       "      UNION\n"
       "      SELECT delta.rid FROM delta_phantom, delta\n"
       "       WHERE delta.srcid=delta_phantom.rid)\n"
       "    SELECT rid FROM delta_phantom)", 0);
  }else{
    describe_artifacts_to_stdout("IN (SELECT rid FROM blob WHERE size<0)", 0);
  }
}

/* Maximum number of collision examples to remember */
#define MAX_COLLIDE 25

/*
** Generate a report on the number of collisions in artifact hashes
** generated by the SQL given in the argument.
*/
static void collision_report(const char *zSql){
  int i, j;
  int nHash = 0;
  Stmt q;
  char zPrev[HNAME_MAX+1];
  struct {
    int cnt;
    char *azHit[MAX_COLLIDE];
    char z[HNAME_MAX+1];
  } aCollide[HNAME_MAX+1];
  memset(aCollide, 0, sizeof(aCollide));
  memset(zPrev, 0, sizeof(zPrev));
  db_prepare(&q,"%s",zSql/*safe-for-%s*/);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q,0);
    int n = db_column_bytes(&q,0);
    int i;
    nHash++;
    for(i=0; zPrev[i] && zPrev[i]==zUuid[i]; i++){}
    if( i>0 && i<=HNAME_MAX ){
      if( i>=4 && aCollide[i].cnt<MAX_COLLIDE ){
        aCollide[i].azHit[aCollide[i].cnt] = mprintf("%.*s", i, zPrev);
      }
      aCollide[i].cnt++;
      if( aCollide[i].z[0]==0 ) memcpy(aCollide[i].z, zPrev, n+1);
    }
    memcpy(zPrev, zUuid, n+1);
  }
  db_finalize(&q);
  @ <table border=1><thead>
  @ <tr><th>Length<th>Instances<th>First Instance</tr>
  @ </thead><tbody>
  for(i=1; i<=HNAME_MAX; i++){
    if( aCollide[i].cnt==0 ) continue;
    @ <tr><td>%d(i)<td>%d(aCollide[i].cnt)<td>%h(aCollide[i].z)</tr>
  }
  @ </tbody></table>
  @ <p>Total number of hashes: %d(nHash)</p>
  for(i=HNAME_MAX; i>=4; i--){
    if( aCollide[i].cnt==0 ) continue;
    if( aCollide[i].cnt>200 ) break;
    if( aCollide[i].cnt<25 ){
      @ <p>Collisions of length %d(i):
    }else{
      @ <p>First 25 collisions of length %d(i):
    }
    for(j=0; j<aCollide[i].cnt && j<MAX_COLLIDE; j++){
      char *zId = aCollide[i].azHit[j];
      if( zId==0 ) continue;
      @ %z(href("%R/ambiguous/%s",zId))%h(zId)</a>
    }
  }
  for(i=4; i<count(aCollide); i++){
    for(j=0; j<aCollide[i].cnt && j<MAX_COLLIDE; j++){
      fossil_free(aCollide[i].azHit[j]);
    }
  }
}

/*
** WEBPAGE: hash-collisions
**
** Show the number of hash collisions for hash prefixes of various lengths.
*/
void hash_collisions_webpage(void){
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_header("Hash Prefix Collisions");
  style_submenu_element("Activity Reports", "reports");
  style_submenu_element("Stats", "stat");
  @ <h1>Hash Prefix Collisions on Check-ins</h1>
  collision_report("SELECT (SELECT uuid FROM blob WHERE rid=objid)"
                   "  FROM event WHERE event.type='ci'"
                   " ORDER BY 1");
  @ <h1>Hash Prefix Collisions on All Artifacts</h1>
  collision_report("SELECT uuid FROM blob ORDER BY 1");
  style_finish_page();
}

/*
** WEBPAGE: clusterlist
**
** Show information about all cluster artifacts in the database.
*/
void clusterlist_page(void){
  Stmt q;
  int cnt = 1;
  sqlite3_int64 szTotal = 0;
  sqlite3_int64 szCTotal = 0;
  login_check_credentials();
  if( !g.perm.Read ){ login_needed(g.anon.Read); return; }
  style_header("All Cluster Artifacts");
  style_submenu_element("All Artifactst", "bloblist");
  if( g.perm.Admin ){
    style_submenu_element("Xfer Log", "rcvfromlist");
  }
  style_submenu_element("Phantoms", "bloblist?phan");
  if( g.perm.Write ){
    style_submenu_element("Artifact Stats", "artifact_stats");
  }

  db_prepare(&q,
    "SELECT blob.uuid, "
    "       blob.size, "
    "       octet_length(blob.content), "
    "       datetime(rcvfrom.mtime),"
    "       user.login,"
    "       rcvfrom.ipaddr"
    "  FROM tagxref JOIN blob ON tagxref.rid=blob.rid"
    "       LEFT JOIN rcvfrom ON blob.rcvid=rcvfrom.rcvid"
    "       LEFT JOIN user ON user.uid=rcvfrom.uid"
    " WHERE tagxref.tagid=%d"
    " ORDER BY rcvfrom.mtime, blob.uuid",
    TAG_CLUSTER
  );
  @ <table cellpadding="2" cellspacing="0" border="1">
  @ <tr><th>&nbsp;
  @ <th>Hash
  @ <th>Date&nbsp;Received
  @ <th>Size
  @ <th>Compressed&nbsp;Size
  if( g.perm.Admin ){
    @ <th>User<th>IP-Address
  }
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    sqlite3_int64 sz = db_column_int64(&q, 1);
    sqlite3_int64 szC = db_column_int64(&q, 2);
    const char *zDate = db_column_text(&q, 3);
    const char *zUser = db_column_text(&q, 4);
    const char *zIp = db_column_text(&q, 5);
    szTotal += sz;
    szCTotal += szC;
    @ <tr><td align="right">%d(cnt++)
    @ <td><a href="%R/info/%S(zUuid)">%S(zUuid)</a>
    if( zDate ){
      @ <td>%h(zDate)
    }else{
      @ <td>&nbsp;
    }
    @ <td align="right">%,lld(sz)
    @ <td align="right">%,lld(szC)
    if( g.perm.Admin ){
      if( zUser ){
        @ <td>%h(zUser)
      }else{
        @ <td>&nbsp;
      }
      if( zIp ){
        @ <td>%h(zIp)
      }else{
        @ <td>&nbsp;
      }
    }
    @ </tr>
  }
  @ </table>
  db_finalize(&q);
  @ <p>Total size of all clusters: %,lld(szTotal) bytes,
  @ %,lld(szCTotal) bytes compressed</p>
  style_finish_page();
}
