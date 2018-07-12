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
  Blob content;           /* Encoded content for this segment */
};

/*
** An instance of the following object describes the struture of
** an rfc-2822 email message.
*/
struct EmailToc {
  int nHdr;              /* Number of header lines */
  int nHdrAlloc;         /* Number of header lines allocated */
  int *aHdr;             /* Two integers for each hdr line, offset and length */
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
  fossil_free(p->aHdr);
  for(i=0; i<p->nBody; i++){
    fossil_free(p->aBody[i].zFilename);
    blob_reset(&p->aBody[i].content);
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
  pNew->content = empty_blob;
  return pNew;
}

/*
** Add a new header line to the EmailToc.
*/
void emailtoc_new_header_line(EmailToc *p, int iOfst, int nAmt){
  p->nHdr++;
  if( p->nHdr>p->nHdrAlloc ){
    p->nHdrAlloc = (p->nHdrAlloc+1)*2;
    p->aHdr = fossil_realloc(p->aHdr, sizeof(int)*2*p->nHdrAlloc);
  }
  p->aHdr[p->nHdr*2-2] = iOfst;
  p->aHdr[p->nHdr*2-1] = nAmt;
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
** Return a pointer to the first non-whitespace character in z
*/
static const char *firstToken(const char *z, int n){
  while( n>0 && fossil_isspace(*z) ){
    n--;
    z++;
  }
  return n>0 ? z : 0;
}

/*
** The n-bytes of content in z are a multipart/ body component for
** an email message.  Decode this into its individual segments.
**
** The component should start and end with a boundary line.  There
** may be additional boundary lines in the middle.
*/
static void emailtoc_add_multipart(
  EmailToc *p,          /* Append the segments here */
  Blob *pEmail,         /* The original full email raw text */
  const char *z,        /* The body component */
  int n                 /* Bytes of content in z[] */
){
  return;
}


/*
** Compute a table-of-contents (EmailToc) for the email message
** provided on the input.
*/
EmailToc *emailtoc_from_email(Blob *pEmail){
  const char *z;
  int i;
  int n;
  int multipartBody = 0;
  EmailToc *p = emailtoc_alloc();
  EmailBody *pBody = emailtoc_new_body(p);
  blob_terminate(pEmail);
  z = blob_buffer(pEmail);
  i = 0; 
  while( z[i] ){
    n = email_line_length(&z[i]);
    if( (n==2 && z[i]=='\r' && z[i+1]=='\n') || z[i]=='\n' || n==0 ){
      /* This is the blank line at the end of the header */
      i += n;
      break;
    }
    if( sqlite3_strnicmp(z+i, "Content-Type:", 13)==0 ){
      const char *z2 = firstToken(z+i+13, n-13);
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
      const char *z2 = firstToken(z+(i+26), n-26);
      if( z2 && sqlite3_strnicmp(z2, "base64", 6)==0 ){
        pBody->encoding = EMAILENC_B64;
                                 /*  123456789 123456 */
      }else if( sqlite3_strnicmp(z2, "quoted-printable", 16)==0 ){
        pBody->encoding = EMAILENC_QUOTED;
      }else{
        pBody->encoding = EMAILENC_NONE;
      }
    }
    emailtoc_new_header_line(p, i, n);
    i += n;
  }
  n = blob_size(pEmail) - i;
  if( multipartBody ){
    p->nBody--;
    emailtoc_add_multipart(p, pEmail, z+i, n);
  }else{
    blob_init(&pBody->content, z+i, n);
  }
  return p;
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
  const char *z;
  verify_all_options();
  if( g.argc!=3 ) usage("FILE");
  blob_read_from_file(&email, g.argv[2], ExtFILE);
  p = emailtoc_from_email(&email);
  z = blob_buffer(&email);
  fossil_print("%d header line and %d content segments\n",
               p->nHdr, p->nBody);
  for(i=0; i<p->nHdr; i++){
    fossil_print("%3d: %.*s", i, p->aHdr[i*2+1], z+p->aHdr[i*2]);
  }
  for(i=0; i<p->nBody; i++){
    fossil_print("\nBODY %d mime \"%s\" encoding %d:\n",
                 i, p->aBody[i].zMimetype, p->aBody[i].encoding);
    fossil_print("%s\n", blob_str(&p->aBody[i].content));
  }
  emailtoc_free(p);
  blob_reset(&email);
}

/*
** WEBPAGE:  webmail
**
** This page can be used to read content from the EMAILBOX table
** that contains email received by the "fossil smtpd" command.
*/
void webmail_page(void){
  int emailid;
  Stmt q;
  Blob sql;
  int showAll = 0;
  login_check_credentials();
  if( g.zLogin==0 ){
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
  if( emailid>0 ){
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
      style_header("Message %d",emailid);
      @ <pre>%h(db_column_text(&q, 0))</pre>
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
  if( g.perm.Admin ){
    const char *zUser = P("user");
    if( P("all")!=0 ){
      /* Show all email messages */
      showAll = 1;
    }else{
      style_submenu_element("All", "%R/webmail?all");
      if( zUser ){
        blob_append_sql(&sql, " WHERE euser=%Q", zUser);
      }else{
        blob_append_sql(&sql, " WHERE euser=%Q", g.zLogin);
      }
    }
  }else{
    blob_append_sql(&sql, " WHERE euser=%Q", g.zLogin);
  }
  blob_append_sql(&sql, " ORDER BY edate DESC limit 50");
  db_prepare_blob(&q, &sql);
  blob_reset(&sql);
  @ <ol>
  while( db_step(&q)==SQLITE_ROW ){
    int emailid = db_column_int(&q,4);
    const char *zFrom = db_column_text(&q, 0);
    const char *zDate = db_column_text(&q, 1);
    const char *zSubject = db_column_text(&q, 3);
    @ <li>
    if( showAll ){
      const char *zTo = db_column_text(&q,5);
      @ <a href="%R/webmail?user=%t(zTo)">%h(zTo)</a>:
    }
    @ <a href="%R/webmail?id=%d(emailid)">%h(zFrom) &rarr; %h(zSubject)</a>
    @ %h(zDate)
  }
  db_finalize(&q);
  @ </ol>
  style_footer(); 
}
