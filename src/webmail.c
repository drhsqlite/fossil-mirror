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
** Implementation of web pages for managing the email storage tables
** (if they exist):
**
**     emailbox
**     emailblob
**     emailroute
*/
#include "config.h"
#include "webmail.h"
#include <assert.h>


#if INTERFACE

/* Recognized content encodings */
#define EMAILENC_NONE   0         /* No encoding */
#define EMAILENC_B64    1         /* Base64 encoded */
#define EMAILENC_QUOTED 2         /* Quoted printable */

/* An instance of the following object records the location of important
** attributes on a single element in a multipart email message body.
*/
struct EmailBody {
  char zMimetype[32];     /* Mimetype */
  u8 encoding;            /* Type of encoding */
  char *zFilename;        /* From content-disposition: */
  char *zContent;         /* Content.  \0 terminator inserted */
};

/*
** An instance of the following object describes the struture of
** an rfc-2822 email message.
*/
struct EmailToc {
  int nHdr;              /* Number of header lines */
  int nHdrAlloc;         /* Number of header lines allocated */
  char **azHdr;          /* Pointer to header line.  \0 terminator inserted */
  int nBody;             /* Number of body segments */
  int nBodyAlloc;        /* Number of body segments allocated */
  EmailBody *aBody;      /* Location of body information */
};
#endif

/*
** Free An EmailToc object
*/
void emailtoc_free(EmailToc *p){
  int i;
  fossil_free(p->azHdr);
  for(i=0; i<p->nBody; i++){
    fossil_free(p->aBody[i].zFilename);
  }
  fossil_free(p->aBody);
  fossil_free(p);
}

/*
** Allocate a new EmailToc object
*/
EmailToc *emailtoc_alloc(void){
  EmailToc *p = fossil_malloc( sizeof(*p) );
  memset(p, 0, sizeof(*p));
  return p;
}

/*
** Add a new body element to an EmailToc.
*/
EmailBody *emailtoc_new_body(EmailToc *p){
  EmailBody *pNew;
  p->nBody++;
  if( p->nBody>p->nBodyAlloc ){
    p->nBodyAlloc = (p->nBodyAlloc+1)*2;
    p->aBody = fossil_realloc(p->aBody, sizeof(p->aBody[0])*p->nBodyAlloc);
  }
  pNew = &p->aBody[p->nBody-1];
  memset(pNew, 0, sizeof(*pNew));
  return pNew;
}

/*
** Add a new header line to the EmailToc.
*/
void emailtoc_new_header_line(EmailToc *p, char *z){
  p->nHdr++;
  if( p->nHdr>p->nHdrAlloc ){
    p->nHdrAlloc = (p->nHdrAlloc+1)*2;
    p->azHdr = fossil_realloc(p->azHdr, sizeof(p->azHdr[0])*p->nHdrAlloc);
  }
  p->azHdr[p->nHdr-1] = z;
}

/*
** Return the length of a line in an email header.  Continuation lines
** are included.  Hence, this routine returns the number of bytes up to
** and including the first \n character that is followed by something
** other than whitespace.
*/
static int email_line_length(const char *z){
  int i;
  for(i=0; z[i] && (z[i]!='\n' || z[i+1]==' ' || z[i+1]=='\t'); i++){}
  if( z[i]=='\n' ) i++;
  return i;
}

/*
** Look for a parameter of the form NAME=VALUE in the given email
** header line.  Return a copy of VALUE in space obtained from 
** fossil_malloc().  Or return NULL if there is no such parameter.
*/
static char *email_hdr_value(const char *z, const char *zName){
  int nName = (int)strlen(zName);
  int i;
  const char *z2 = strstr(z, zName);
  if( z2==0 ) return 0;
  z2 += nName;
  if( z2[0]!='=' ) return 0;
  z2++;
  if( z2[0]=='"' ){
    z2++;
    for(i=0; z2[i] && z2[i]!='"'; i++){}
    if( z2[i]!='"' ) return 0;
  }else{
    for(i=0; z2[i] && !fossil_isspace(z2[i]); i++){}
  }
  return mprintf("%.*s", i, z2);
}

/*
** Return a pointer to the first non-whitespace character in z
*/
static const char *firstToken(const char *z){
  while( fossil_isspace(*z) ){
    z++;
  }
  return z;
}

/*
** The n-bytes of content in z is a single multipart mime segment
** with its own header and body.  Decode this one segment and add it to p;
**
** Rows of the header of the segment are added to p if bAddHeader is
** true.
*/
LOCAL void emailtoc_add_multipart_segment(
  EmailToc *p,          /* Append the segments here */
  char *z,              /* The body component */
  int bAddHeader        /* True to add header lines to p */
){
  int i, j;
  int n;
  int multipartBody = 0;
  EmailBody *pBody = emailtoc_new_body(p);
  i = 0;
  while( z[i] ){
    n = email_line_length(&z[i]);
    if( (n==2 && z[i]=='\r' && z[i+1]=='\n') || z[i]=='\n' || n==0 ){
      /* This is the blank line at the end of the header */
      i += n;
      break;
    }
    for(j=i+n; j>i && fossil_isspace(z[j-1]); j--){}
    z[j] = 0;
    if( sqlite3_strnicmp(z+i, "Content-Type:", 13)==0 ){
      const char *z2 = firstToken(z+i+13);
      if( z2 && strncmp(z2, "multipart/", 10)==0 ){
        multipartBody = 1;
      }else{
        int j;
        for(j=0; z2[j]=='/' || fossil_isalnum(z2[j]); j++){}
        if( j>=sizeof(pBody->zMimetype) ) j = sizeof(pBody->zMimetype);
        memcpy(pBody->zMimetype, z2, j);
        pBody->zMimetype[j] = 0;
      }
    }
                           /*  123456789 123456789 123456 */
    if( sqlite3_strnicmp(z+i, "Content-Transfer-Encoding:", 26)==0 ){
      const char *z2 = firstToken(z+(i+26));
      if( z2 && sqlite3_strnicmp(z2, "base64", 6)==0 ){
        pBody->encoding = EMAILENC_B64;
                                 /*  123456789 123456 */
      }else if( sqlite3_strnicmp(z2, "quoted-printable", 16)==0 ){
        pBody->encoding = EMAILENC_QUOTED;
      }else{
        pBody->encoding = EMAILENC_NONE;
      }
    }
    if( bAddHeader ){
      emailtoc_new_header_line(p, z+i);
    }else if( sqlite3_strnicmp(z+i, "Content-Disposition:", 20)==0 ){
                                /*   123456789 123456789  */
       fossil_free(pBody->zFilename);
       pBody->zFilename = email_hdr_value(z+i, "filename");
    }
    i += n;
  }
  if( multipartBody ){
    p->nBody--;
    emailtoc_add_multipart(p, z+i);
  }else{
    pBody->zContent = z+i;
  }
}

/*
** The n-bytes of content in z are a multipart/ body component for
** an email message.  Decode this into its individual segments.
**
** The component should start and end with a boundary line.  There
** may be additional boundary lines in the middle.
*/
LOCAL void emailtoc_add_multipart(
  EmailToc *p,          /* Append the segments here */
  char *z               /* The body component.  zero-terminated */
){
  int nB;               /* Size of the boundary string */
  int iStart;           /* Start of the coding region past boundary mark */
  int i;                /* Loop index */
  char *zBoundary = 0;  /* Boundary marker */

  /* Skip forward to the beginning of the boundary mark.  The boundary
  ** mark always begins with "--" */
  while( z[0]!='-' || z[1]!='-' ){
    while( z[0] && z[0]!='\n' ) z++;
    if( z[0]==0 ) return;
    z++;
  }

  /* Find the length of the boundary mark. */
  zBoundary = z;
  for(nB=0; z[nB] && !fossil_isspace(z[nB]); nB++){}
  if( nB==0 ) return;

  z += nB;
  while( fossil_isspace(z[0]) ) z++;
  zBoundary[nB] = 0;
  for(i=iStart=0; z[i]; i++){
    if( z[i]=='\n' && strncmp(z+i+1, zBoundary, nB)==0 ){
      z[i+1] = 0;
      emailtoc_add_multipart_segment(p, z+iStart, 0);
      iStart = i+nB;
      if( z[iStart]=='-' && z[iStart+1]=='-' ) return;
      while( fossil_isspace(z[iStart]) ) iStart++;
      i = iStart;
    }
  }
}

/*
** Compute a table-of-contents (EmailToc) for the email message
** provided on the input.
**
** This routine will cause pEmail to become zero-terminated if it is
** not already.  It will also insert zero characters into parts of
** the message, to delimit the various components.
*/
EmailToc *emailtoc_from_email(Blob *pEmail){
  char *z;
  EmailToc *p = emailtoc_alloc();
  blob_terminate(pEmail);
  z = blob_buffer(pEmail);
  emailtoc_add_multipart_segment(p, z, 1);
  return p;
}

/*
** Inplace-unfolding of an email header line.
**
** Actually - this routine works by converting all contiguous sequences
** of whitespace into a single space character.
*/
static void email_hdr_unfold(char *z){
  int i, j;
  char c;
  for(i=j=0; (c = z[i])!=0; i++){
    if( fossil_isspace(c) ){
      c = ' ';
      if( j && z[j-1]==' ' ) continue;
    }
    z[j++] = c;
  }
  z[j] = 0;
}

/*
** COMMAND: test-decode-email
**
** Usage: %fossil test-decode-email FILE
**
** Read an rfc-2822 formatted email out of FILE, then write a decoding
** to stdout.  Use for testing and validating the email decoder.
*/
void test_email_decode_cmd(void){
  Blob email;
  EmailToc *p;
  int i;
  verify_all_options();
  if( g.argc!=3 ) usage("FILE");
  blob_read_from_file(&email, g.argv[2], ExtFILE);
  p = emailtoc_from_email(&email);
  fossil_print("%d header line and %d content segments\n",
               p->nHdr, p->nBody);
  for(i=0; i<p->nHdr; i++){
    email_hdr_unfold(p->azHdr[i]);
    fossil_print("%3d: %s\n", i, p->azHdr[i]);
  }
  for(i=0; i<p->nBody; i++){
    fossil_print("\nBODY %d mime \"%s\" encoding %d",
                 i, p->aBody[i].zMimetype, p->aBody[i].encoding);
    if( p->aBody[i].zFilename ){
      fossil_print(" filename \"%s\"", p->aBody[i].zFilename);
    }
    fossil_print("\n");
    if( strncmp(p->aBody[i].zMimetype,"text/",5)!=0 ) continue;
    switch( p->aBody[i].encoding ){
      case EMAILENC_B64: {
        int n = 0;
        decodeBase64(p->aBody[i].zContent, &n, p->aBody[i].zContent);
        fossil_print("%s", p->aBody[i].zContent);
        if( n && p->aBody[i].zContent[n-1]!='\n' ) fossil_print("\n");
        break;
      }
      case EMAILENC_QUOTED: {
        int n = 0;
        decodeQuotedPrintable(p->aBody[i].zContent, &n);
        fossil_print("%s", p->aBody[i].zContent);
        if( n && p->aBody[i].zContent[n-1]!='\n' ) fossil_print("\n");
        break;
      }
      default: {
        fossil_print("%s\n", p->aBody[i].zContent);
        break;
      }
    }
  }
  emailtoc_free(p);
  blob_reset(&email);
}

/*
** WEBPAGE:  webmail
**
** This page can be used to read content from the EMAILBOX table
** that contains email received by the "fossil smtpd" command.
**
** Query parameters:
**
**     id=N                 Show a single email entry emailbox.ebid==N
**     f=N                  Display format.  0: decoded 1: raw
**     u=USER               Show mailbox for USER (admin only).
**     u=*                  Show mailbox for all users (admin only).
**     d=N                  0: inbox+unread 1: unread-only 2: trash 3: all
**     eN                   Select email entry emailbox.ebid==N
**     trash                Move selected entries to trash (estate=2)
**     read                 Mark selected entries as read (estate=1)
**     unread               Mark selected entries as unread (estate=0)
**  
*/
void webmail_page(void){
  int emailid;
  Stmt q;
  Blob sql;
  int showAll = 0;
  const char *zUser = 0;
  HQuery url;
  login_check_credentials();
  if( !login_is_individual() ){
    login_needed(0);
    return;
  }
  if( !db_table_exists("repository","emailbox") ){
    style_header("Webmail Not Available");
    @ <p>This repository is not configured to provide webmail</p>
    style_footer();
    return;
  }
  add_content_sql_commands(g.db);
  emailid = atoi(PD("id","0"));
  url_initialize(&url, "webmail");
  if( g.perm.Admin ){
    zUser = P("user");
    if( zUser ){
      url_add_parameter(&url, "user", zUser);
      if( fossil_strcmp(zUser,"*")==0 ){
        showAll = 1;
        zUser = 0;
      }
    }
  }
  if( emailid>0 ){
    style_submenu_element("Index", "%s", url_render(&url,"id",0,0,0));
    blob_init(&sql, 0, 0);
    blob_append_sql(&sql, "SELECT decompress(etxt)"
                          " FROM emailblob WHERE emailid=%d",
                          emailid);
    if( !g.perm.Admin ){
      blob_append_sql(&sql, " AND EXISTS(SELECT 1 FROM emailbox WHERE"
                            " euser=%Q AND emsgid=emailid)", g.zLogin);
    }
    db_prepare_blob(&q, &sql);
    blob_reset(&sql);
    if( db_step(&q)==SQLITE_ROW ){
      Blob msg = db_column_text_as_blob(&q, 0);
      url_add_parameter(&url, "id", P("id"));
      style_header("Message %d",emailid);
      if( PB("raw") ){
        @ <pre>%h(db_column_text(&q, 0))</pre>
        style_submenu_element("Decoded", "%s", url_render(&url,"raw",0,0,0));
      }else{      
        EmailToc *p = emailtoc_from_email(&msg);
        int i, j;
        style_submenu_element("Raw", "%s", url_render(&url,"raw","1",0,0));
        @ <p>
        for(i=0; i<p->nHdr; i++){
          char *z = p->azHdr[i];
          email_hdr_unfold(z);
          for(j=0; z[j] && z[j]!=':'; j++){}
          if( z[j]!=':' ){
            @ %h(z)<br>
          }else{
            z[j] = 0;
            @ <b>%h(z):</b> %h(z+j+1)<br>
          }
        }
        for(i=0; i<p->nBody; i++){
          @ <hr><b>Messsage Body #%d(i): %h(p->aBody[i].zMimetype) \
          if( p->aBody[i].zFilename ){
            @ "%h(p->aBody[i].zFilename)"
          }
          @ </b>
          if( strncmp(p->aBody[i].zMimetype, "text/", 5)!=0 ) continue;
          switch( p->aBody[i].encoding ){
            case EMAILENC_B64: {
              int n = 0;
              decodeBase64(p->aBody[i].zContent, &n, p->aBody[i].zContent);
              break;
            }
            case EMAILENC_QUOTED: {
              int n = 0;
              decodeQuotedPrintable(p->aBody[i].zContent, &n);
              break;
            }
          }
          @ <pre>%h(p->aBody[i].zContent)</pre>
        }
      }      
      style_footer();
      db_finalize(&q);
      return;
    }
    db_finalize(&q);
  }
  style_header("Webmail");
  blob_init(&sql, 0, 0);
  blob_append_sql(&sql,
        /*    0       1                           2        3        4      5 */
    "SELECT efrom, datetime(edate,'unixepoch'), estate, esubject, emsgid, euser"
    " FROM emailbox"
  );
  if( showAll ){
    style_submenu_element("My Emails", "%s", url_render(&url,"user",0,0,0));
  }else if( zUser!=0 ){
    style_submenu_element("All Users", "%s", url_render(&url,"user","*",0,0));
    if( fossil_strcmp(zUser, g.zLogin)!=0 ){
      style_submenu_element("My Emails", "%s", url_render(&url,"user",0,0,0));
    }
    if( zUser ){
      blob_append_sql(&sql, " WHERE euser=%Q", zUser);
    }else{
      blob_append_sql(&sql, " WHERE euser=%Q", g.zLogin);
    }
  }else{
    if( g.perm.Admin ){
      style_submenu_element("All Users", "%s", url_render(&url,"user","*",0,0));
    }
    blob_append_sql(&sql, " WHERE euser=%Q", g.zLogin);
  }
  blob_append_sql(&sql, " ORDER BY edate DESC limit 50");
  db_prepare_blob(&q, &sql);
  blob_reset(&sql);
  @ <ol>
  while( db_step(&q)==SQLITE_ROW ){
    char *zId = db_column_text(&q,4);
    const char *zFrom = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zSubject = db_column_text(&q, 3);
    @ <li>
    if( showAll ){
      const char *zTo = db_column_text(&q,5);
      @ <a href="%s(url_render(&url,"user",zTo,0,0))">%h(zTo)</a>:
    }
    @ <a href="%s(url_render(&url,"id",zId,0,0))">\
    @ %h(zFrom) &rarr; %h(zSubject)</a>
    @ %h(zDate)
  }
  db_finalize(&q);
  @ </ol>
  style_footer(); 
}
